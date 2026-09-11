// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Baking/TerraDyneBaker.h"
#include "TerraDyneEditorModule.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneWorldPreset.h"
#include "World/TerraDyneLandscapeAssetSet.h"
#include "World/TerraDyneTileData.h"

// Engine Includes
#include "FoliageType.h"
#include "FoliageType_Actor.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliageActor.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "Landscape.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionLandscapeLayerSwitch.h"
#include "Materials/MaterialExpressionLandscapeLayerWeight.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UObject/SavePackage.h"

/**
 * UE Landscape stores height as 16-bit values with 128 cm/unit vertical scale.
 * This constant converts component transform scale to the full height range.
 */
static constexpr float LandscapeHeightScale = 512.0f;

namespace
{
	static FName ResolveLandscapeLayerName(const ULandscapeLayerInfoObject* LayerInfo)
	{
		if (!LayerInfo)
		{
			return NAME_None;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		return LayerInfo->GetLayerName() != NAME_None ? LayerInfo->GetLayerName() : LayerInfo->GetFName();
#else
		return LayerInfo->LayerName != NAME_None ? LayerInfo->LayerName : LayerInfo->GetFName();
#endif
	}

	static FIntPoint ResolveChunkCoordinate(const ULandscapeComponent* Component)
	{
		if (!Component || Component->ComponentSizeQuads <= 0)
		{
			return FIntPoint::ZeroValue;
		}

		return FIntPoint(
			Component->SectionBaseX / Component->ComponentSizeQuads,
			Component->SectionBaseY / Component->ComponentSizeQuads);
	}

	static bool UsesLandscapeOnlyMaterialExpressions(const UMaterialInterface* MaterialInterface)
	{
		const UMaterial* Material = MaterialInterface ? MaterialInterface->GetMaterial() : nullptr;
		if (!Material)
		{
			return false;
		}

		return Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeGrassOutput>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerBlend>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerCoords>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerSample>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerSwitch>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerWeight>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeVisibilityMask>();
	}

	static FString MakeSanitizedObjectName(const FString& RawName)
	{
		const FString Sanitized = ObjectTools::SanitizeObjectName(RawName);
		return Sanitized.IsEmpty() ? FString(TEXT("TerraDyneAsset")) : Sanitized;
	}

	static bool SaveAssetPackage(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			return false;
		}

		const FString PackageName = Package->GetName();
		const FString PackageFilename =
			FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
	}

	static float SampleTileHeightAtLocalLocation(const UTerraDyneTileData* TileData, const FVector& LocalLocation)
	{
		if (!TileData || TileData->Resolution < 2 || TileData->InitialHeightMap.Num() != TileData->Resolution * TileData->Resolution)
		{
			return 0.0f;
		}

		const float HalfSize = TileData->RealWorldSize * 0.5f;
		const float NormalizedX = FMath::Clamp((LocalLocation.X + HalfSize) / TileData->RealWorldSize, 0.0f, 1.0f);
		const float NormalizedY = FMath::Clamp((LocalLocation.Y + HalfSize) / TileData->RealWorldSize, 0.0f, 1.0f);
		const float GridX = NormalizedX * static_cast<float>(TileData->Resolution - 1);
		const float GridY = NormalizedY * static_cast<float>(TileData->Resolution - 1);

		const int32 X0 = FMath::FloorToInt(GridX);
		const int32 Y0 = FMath::FloorToInt(GridY);
		const int32 X1 = FMath::Min(X0 + 1, TileData->Resolution - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, TileData->Resolution - 1);

		const float FX = GridX - static_cast<float>(X0);
		const float FY = GridY - static_cast<float>(Y0);

		auto Sample = [TileData](int32 X, int32 Y) -> float
		{
			const int32 Index = (Y * TileData->Resolution) + X;
			return TileData->InitialHeightMap.IsValidIndex(Index)
				? static_cast<float>(TileData->InitialHeightMap[Index]) / 65535.0f
				: 0.0f;
		};

		const float H00 = Sample(X0, Y0);
		const float H10 = Sample(X1, Y0);
		const float H01 = Sample(X0, Y1);
		const float H11 = Sample(X1, Y1);

		float Height;
		if (FY <= FX)
		{
			Height = H00 + FX * (H10 - H00) + FY * (H11 - H10);
		}
		else
		{
			Height = H00 + FX * (H11 - H01) + FY * (H01 - H00);
		}

		return Height * TileData->BakedZScale;
	}

	static void CaptureTransferredFoliageForComponent(
		ULandscapeComponent* Component,
		const FTransform& ChunkTransform,
		UTerraDyneTileData* TileData,
		bool bFollowsTerrain,
		TSet<FString>& ImportedFoliageDefinitions,
		int32& ImportedFoliageInstances)
	{
		if (!Component || !TileData)
		{
			return;
		}

		AInstancedFoliageActor* FoliageActor =
			AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Component->GetComponentLevel(), false);
		if (!FoliageActor)
		{
			return;
		}

		const TMap<UFoliageType*, TArray<const FFoliageInstancePlacementInfo*>> FoliageByType =
			FoliageActor->GetInstancesForComponent(Component);
		if (FoliageByType.Num() == 0)
		{
			return;
		}

		TArray<FString> StaticMeshPaths;
		TArray<int32> MaterialCounts;
		TArray<FString> OverrideMaterialPaths;
		TArray<int32> DefinitionIndices;
		TArray<FTransform> LocalTransforms;
		TArray<float> TerrainOffsets;
		TMap<FString, int32> DefinitionLookup;

		TArray<FString> ActorClassPaths;
		TArray<uint8> ActorAttachFlags;
		TArray<int32> ActorDefinitionIndices;
		TArray<FTransform> ActorLocalTransforms;
		TArray<float> ActorTerrainOffsets;
		TMap<FString, int32> ActorDefinitionLookup;

		for (const auto& FoliagePair : FoliageByType)
		{
			const UFoliageType_InstancedStaticMesh* StaticMeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliagePair.Key);
			const UFoliageType_Actor* ActorType = Cast<UFoliageType_Actor>(FoliagePair.Key);
			if (StaticMeshType && StaticMeshType->Mesh)
			{
				const FString MeshPath = StaticMeshType->Mesh->GetPathName();
				TArray<FString> MaterialPaths;
				MaterialPaths.Reserve(StaticMeshType->OverrideMaterials.Num());
				for (UMaterialInterface* OverrideMaterial : StaticMeshType->OverrideMaterials)
				{
					if (!OverrideMaterial)
					{
						MaterialPaths.Add(FString());
						continue;
					}

					const FString MaterialPath = OverrideMaterial->GetPathName();
					MaterialPaths.Add(MaterialPath.StartsWith(TEXT("/Engine/Transient")) ? FString() : MaterialPath);
				}

				FString DefinitionKey = MeshPath;
				for (const FString& MaterialPath : MaterialPaths)
				{
					DefinitionKey += TEXT("|");
					DefinitionKey += MaterialPath;
				}

				int32 DefinitionIndex = INDEX_NONE;
				if (const int32* ExistingIndex = DefinitionLookup.Find(DefinitionKey))
				{
					DefinitionIndex = *ExistingIndex;
				}
				else
				{
					DefinitionIndex = StaticMeshPaths.Add(MeshPath);
					DefinitionLookup.Add(DefinitionKey, DefinitionIndex);
					MaterialCounts.Add(MaterialPaths.Num());
					OverrideMaterialPaths.Append(MaterialPaths);
					ImportedFoliageDefinitions.Add(DefinitionKey);
				}

				for (const FFoliageInstancePlacementInfo* PlacementInfo : FoliagePair.Value)
				{
					if (!PlacementInfo)
					{
						continue;
					}

					const FTransform WorldTransform(
						FQuat(PlacementInfo->Rotation),
						PlacementInfo->Location,
						FVector(PlacementInfo->DrawScale3D));
					const FTransform LocalTransform = WorldTransform.GetRelativeTransform(ChunkTransform);

					DefinitionIndices.Add(DefinitionIndex);
					LocalTransforms.Add(LocalTransform);
					TerrainOffsets.Add(
						LocalTransform.GetLocation().Z -
						SampleTileHeightAtLocalLocation(TileData, LocalTransform.GetLocation()));
				}
			}
			else if (ActorType && ActorType->ActorClass)
			{
				const FString ActorClassPath = ActorType->ActorClass->GetPathName();
				const FString DefinitionKey = FString::Printf(
					TEXT("Actor:%s|%d"),
					*ActorClassPath,
					ActorType->bShouldAttachToBaseComponent ? 1 : 0);

				int32 DefinitionIndex = INDEX_NONE;
				if (const int32* ExistingIndex = ActorDefinitionLookup.Find(DefinitionKey))
				{
					DefinitionIndex = *ExistingIndex;
				}
				else
				{
					DefinitionIndex = ActorClassPaths.Add(ActorClassPath);
					ActorDefinitionLookup.Add(DefinitionKey, DefinitionIndex);
					ActorAttachFlags.Add(ActorType->bShouldAttachToBaseComponent ? 1 : 0);
					ImportedFoliageDefinitions.Add(DefinitionKey);
				}

				for (const FFoliageInstancePlacementInfo* PlacementInfo : FoliagePair.Value)
				{
					if (!PlacementInfo)
					{
						continue;
					}

					const FTransform WorldTransform(
						FQuat(PlacementInfo->Rotation),
						PlacementInfo->Location,
						FVector(PlacementInfo->DrawScale3D));
					const FTransform LocalTransform = WorldTransform.GetRelativeTransform(ChunkTransform);

					ActorDefinitionIndices.Add(DefinitionIndex);
					ActorLocalTransforms.Add(LocalTransform);
					ActorTerrainOffsets.Add(
						LocalTransform.GetLocation().Z -
						SampleTileHeightAtLocalLocation(TileData, LocalTransform.GetLocation()));
				}
			}
		}

		if (LocalTransforms.Num() == 0 && ActorLocalTransforms.Num() == 0)
		{
			return;
		}

		ImportedFoliageInstances += LocalTransforms.Num() + ActorLocalTransforms.Num();

		TileData->bTransferredFoliageFollowsTerrain = bFollowsTerrain;
		TileData->FoliageStaticMeshPaths = MoveTemp(StaticMeshPaths);
		TileData->FoliageMaterialCounts = MoveTemp(MaterialCounts);
		TileData->FoliageOverrideMaterialPaths = MoveTemp(OverrideMaterialPaths);
		TileData->FoliageDefinitionIndices = MoveTemp(DefinitionIndices);
		TileData->FoliageInstanceLocalTransforms = MoveTemp(LocalTransforms);
		TileData->FoliageInstanceTerrainOffsets = MoveTemp(TerrainOffsets);
		TileData->ActorFoliageClassPaths = MoveTemp(ActorClassPaths);
		TileData->ActorFoliageAttachFlags = MoveTemp(ActorAttachFlags);
		TileData->ActorFoliageDefinitionIndices = MoveTemp(ActorDefinitionIndices);
		TileData->ActorFoliageInstanceLocalTransforms = MoveTemp(ActorLocalTransforms);
		TileData->ActorFoliageInstanceTerrainOffsets = MoveTemp(ActorTerrainOffsets);
	}
}

UTerraDyneLandscapeAssetSet* UTerraDyneBaker::BakeLandscapeToAssetSet(
	ALandscapeProxy* SourceLandscape,
	FString DestinationPath,
	const FTerraDyneLandscapeMigrationOptions& Options)
{
	if (!SourceLandscape)
	{
		UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: Invalid Source Landscape for baked asset set."));
		return nullptr;
	}

	const FString NormalizedDestinationPath = DestinationPath.EndsWith(TEXT("/"))
		? DestinationPath.LeftChop(1)
		: DestinationPath;
	if (!FPackageName::IsValidLongPackageName(NormalizedDestinationPath))
	{
		UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: Invalid baked asset destination path '%s'."), *DestinationPath);
		return nullptr;
	}

	ALandscapeProxy* MetadataLandscape = SourceLandscape;
	if (ALandscape* RootLandscape = SourceLandscape->GetLandscapeActor())
	{
		MetadataLandscape = RootLandscape;
	}

	TArray<ULandscapeComponent*> SourceComponents;
	if (ULandscapeInfo* LandscapeInfo = SourceLandscape->GetLandscapeInfo())
	{
		LandscapeInfo->ForAllLandscapeComponents([&SourceComponents](ULandscapeComponent* Component)
		{
			if (Component)
			{
				SourceComponents.Add(Component);
			}
		});
	}

	if (SourceComponents.Num() == 0)
	{
		for (ULandscapeComponent* Component : SourceLandscape->LandscapeComponents)
		{
			if (Component)
			{
				SourceComponents.Add(Component);
			}
		}
	}

	SourceComponents.Sort([](const ULandscapeComponent& A, const ULandscapeComponent& B)
	{
		if (A.SectionBaseX == B.SectionBaseX) return A.SectionBaseY < B.SectionBaseY;
		return A.SectionBaseX < B.SectionBaseX;
	});

	if (SourceComponents.Num() == 0)
	{
		UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: Landscape %s has no loaded components."), *SourceLandscape->GetName());
		return nullptr;
	}

	if (Options.bRejectLandscapeVisibilityHoles)
	{
		for (ULandscapeComponent* Component : SourceComponents)
		{
			if (HasVisibilityHoles(Component))
			{
				UE_LOG(LogTerraDyneEditor, Error,
					TEXT("TerraDyneBaker: refusing to bake %s because component %s contains Landscape visibility holes. TerraDyne 0.6 does not serialize hole topology."),
					*SourceLandscape->GetName(), *GetNameSafe(Component));
				return nullptr;
			}
		}
	}

	if (!DestinationPath.EndsWith(TEXT("/")))
	{
		DestinationPath += TEXT("/");
	}

	const FString AssetSetName = FString::Printf(
		TEXT("TD_%s_LandscapeSet"),
		*MakeSanitizedObjectName(MetadataLandscape->GetName()));
	const FString PackageName = DestinationPath + AssetSetName;

	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();

	UTerraDyneLandscapeAssetSet* AssetSet = FindObject<UTerraDyneLandscapeAssetSet>(Package, *AssetSetName);
	if (!AssetSet)
	{
		AssetSet = NewObject<UTerraDyneLandscapeAssetSet>(Package, *AssetSetName, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(AssetSet);
	}

	AssetSet->Tiles.Reset();
	AssetSet->AdoptedMasterMaterial = nullptr;
	AssetSet->MigrationState = FTerraDyneLandscapeMigrationState();
	AssetSet->MigrationState.bWasImportedFromLandscape = true;
	AssetSet->MigrationState.SourceLandscapeName = MetadataLandscape->GetName();
	AssetSet->MigrationState.SourceLandscapePath = MetadataLandscape->GetPathName();
	AssetSet->MigrationState.SourceLandscapeLocation = MetadataLandscape->GetActorLocation();
	AssetSet->MigrationState.SourceLandscapeScale = MetadataLandscape->GetActorScale3D();
	AssetSet->MigrationState.SourceLandscapeMaterialPath =
		MetadataLandscape->LandscapeMaterial ? MetadataLandscape->LandscapeMaterial->GetPathName() : FString();
	AssetSet->MigrationState.bImportedPaintLayers = Options.bImportWeightLayers;
	AssetSet->MigrationState.bRegenerateGrassFromImportedLayers = Options.bRegenerateGrassFromImportedLayers;
	AssetSet->MigrationState.bSourceLandscapeHidden = Options.bHideSourceLandscape;
	AssetSet->MigrationState.ImportedAtIso8601 = FDateTime::UtcNow().ToIso8601();

	const FVector LandscapeScale = MetadataLandscape->GetActorScale3D();
	const float GlobalChunkSize = SourceComponents[0]->ComponentSizeQuads * LandscapeScale.X;
	AssetSet->MigrationState.ImportedChunkSize = GlobalChunkSize;
	AssetSet->MigrationState.RuntimeManagerLocation = FVector(
		MetadataLandscape->GetActorLocation().X + (GlobalChunkSize * 0.5f),
		MetadataLandscape->GetActorLocation().Y + (GlobalChunkSize * 0.5f),
		MetadataLandscape->GetActorLocation().Z - (256.0f * LandscapeScale.Z));

	if (Options.bAdoptLandscapeMaterialAsMasterMaterial && MetadataLandscape->LandscapeMaterial)
	{
		if (UsesLandscapeOnlyMaterialExpressions(MetadataLandscape->LandscapeMaterial))
		{
			UE_LOG(LogTerraDyneEditor, Warning,
				TEXT("TerraDyneBaker: Skipping adopted master material for %s because it uses Landscape-only expressions."),
				*MetadataLandscape->LandscapeMaterial->GetName());
		}
		else
		{
			AssetSet->AdoptedMasterMaterial = MetadataLandscape->LandscapeMaterial;
			AssetSet->MigrationState.bAdoptedLandscapeMaterial = true;
		}
	}

	if (Options.bImportWeightLayers || Options.bCaptureLayerMappings)
	{
		TSet<FName> SeenMappedLayers;
		TSet<FName> SeenUnmappedLayers;
		for (ULandscapeComponent* Component : SourceComponents)
		{
			if (!Component)
			{
				continue;
			}

			for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations())
			{
				if (!Allocation.LayerInfo || Allocation.LayerInfo == Component->GetVisibilityLayer())
				{
					continue;
				}

				const FName LayerName = ResolveLandscapeLayerName(Allocation.LayerInfo);

				if (SeenMappedLayers.Contains(LayerName) || SeenUnmappedLayers.Contains(LayerName))
				{
					continue;
				}

				if (AssetSet->MigrationState.LayerMappings.Num() < 4)
				{
					FTerraDyneLandscapeLayerMapping Mapping;
					Mapping.SourceLayerName = LayerName;
					Mapping.TerraDyneWeightLayerIndex = AssetSet->MigrationState.LayerMappings.Num();
					AssetSet->MigrationState.LayerMappings.Add(Mapping);
					SeenMappedLayers.Add(LayerName);
				}
				else
				{
					AssetSet->MigrationState.UnmappedLayerNames.Add(LayerName.ToString());
					SeenUnmappedLayers.Add(LayerName);
				}
			}
		}
	}

	AssetSet->MigrationState.ImportedWeightLayerCount = AssetSet->MigrationState.LayerMappings.Num();
	AssetSet->MigrationState.bImportedPaintLayers =
		Options.bImportWeightLayers && AssetSet->MigrationState.ImportedWeightLayerCount > 0;
	TArray<FName> CanonicalWeightLayerNames;
	for (const FTerraDyneLandscapeLayerMapping& Mapping : AssetSet->MigrationState.LayerMappings)
	{
		CanonicalWeightLayerNames.Add(Mapping.SourceLayerName);
	}

	TSet<FString> ImportedFoliageDefinitions;
	int32 ImportedFoliageInstances = 0;
	int32 ImportedResolution = 0;

	for (ULandscapeComponent* Component : SourceComponents)
	{
		if (!Component)
		{
			continue;
		}

		const FIntPoint ChunkCoord = ResolveChunkCoordinate(Component);
		const FString TileName = FString::Printf(TEXT("TD_Tile_%d_%d"), ChunkCoord.X, ChunkCoord.Y);
		UTerraDyneTileData* TileData = BakeComponentInternal(
			Component,
			DestinationPath,
			TileName,
			Options.bImportWeightLayers ? &CanonicalWeightLayerNames : nullptr);
		if (!TileData)
		{
			continue;
		}

		const FVector ChunkLocation =
			AssetSet->MigrationState.RuntimeManagerLocation +
			FVector(ChunkCoord.X * GlobalChunkSize, ChunkCoord.Y * GlobalChunkSize, 0.0f);
		const FTransform ChunkTransform(FRotator::ZeroRotator, ChunkLocation);

		if (Options.bTransferPlacedFoliage)
		{
			CaptureTransferredFoliageForComponent(
				Component,
				ChunkTransform,
				TileData,
				Options.bTransferredFoliageFollowsTerrain,
				ImportedFoliageDefinitions,
				ImportedFoliageInstances);
			TileData->MarkPackageDirty();
			SaveAssetPackage(TileData);
		}

		AssetSet->Tiles.Add(TileData);
		ImportedResolution = FMath::Max(ImportedResolution, TileData->Resolution);
	}

	if (AssetSet->Tiles.Num() == 0)
	{
		UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: No tile assets were produced for %s."), *MetadataLandscape->GetName());
		return nullptr;
	}

	AssetSet->MigrationState.ImportedComponentCount = AssetSet->Tiles.Num();
	AssetSet->MigrationState.ImportedResolution = ImportedResolution;
	AssetSet->MigrationState.bTransferredPlacedFoliage =
		Options.bTransferPlacedFoliage && ImportedFoliageInstances > 0;
	AssetSet->MigrationState.bTransferredFoliageFollowsTerrain =
		AssetSet->MigrationState.bTransferredPlacedFoliage && Options.bTransferredFoliageFollowsTerrain;
	AssetSet->MigrationState.ImportedFoliageDefinitionCount = ImportedFoliageDefinitions.Num();
	AssetSet->MigrationState.ImportedFoliageInstanceCount = ImportedFoliageInstances;

	AssetSet->MarkPackageDirty();
	if (!SaveAssetPackage(AssetSet))
	{
		UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: Failed to save baked Landscape asset set %s."), *AssetSetName);
	}

	UE_LOG(
		LogTerraDyneEditor,
		Log,
		TEXT("TerraDyneBaker: Created baked Landscape asset set %s with %d tile assets."),
		*AssetSetName,
		AssetSet->Tiles.Num());
	return AssetSet;
}

UTerraDyneTileData* UTerraDyneBaker::BakeComponent(ULandscapeComponent* Component, FString DestinationPath, FString AssetNameOverride)
{
	return BakeComponentInternal(Component, MoveTemp(DestinationPath), MoveTemp(AssetNameOverride), nullptr);
}

UTerraDyneTileData* UTerraDyneBaker::BakeComponentInternal(
	ULandscapeComponent* Component,
	FString DestinationPath,
	FString AssetNameOverride,
	const TArray<FName>* CanonicalLayerNames)
{
    if (!Component) return nullptr;

    const FString NormalizedDestinationPath = DestinationPath.EndsWith(TEXT("/"))
        ? DestinationPath.LeftChop(1)
        : DestinationPath;
    if (!FPackageName::IsValidLongPackageName(NormalizedDestinationPath))
    {
        UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: Invalid destination path '%s'."), *DestinationPath);
        return nullptr;
    }

    // 1. Determine Asset Name and Path
    FString AssetName = AssetNameOverride;
    if (AssetName.IsEmpty())
    {
        AssetName = FString::Printf(TEXT("TD_Tile_%d_%d"), Component->SectionBaseX, Component->SectionBaseY);
    }
    
    // Ensure path ends with /
    if (!DestinationPath.EndsWith(TEXT("/")))
    {
        DestinationPath += TEXT("/");
    }

    FString PackageName = DestinationPath + AssetName;

    // 2. Create the Asset Package
    UPackage* Package = CreatePackage(*PackageName);
    Package->FullyLoad();

    // Create the UObject
    UTerraDyneTileData* NewData = FindObject<UTerraDyneTileData>(Package, *AssetName);
    if (!NewData)
    {
        NewData = NewObject<UTerraDyneTileData>(Package, *AssetName, RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(NewData);
    }

    // 3. Extract Metadata
    float QuadCount = Component->ComponentSizeQuads; // e.g., 63
    float ScaleX = Component->GetComponentTransform().GetScale3D().X;
    float ScaleZ = Component->GetComponentTransform().GetScale3D().Z;

    NewData->RealWorldSize = QuadCount * ScaleX;
    NewData->BakedZScale = ScaleZ * LandscapeHeightScale;
    NewData->GridCoordinate = ResolveChunkCoordinate(Component);
    NewData->SourceComponentName = Component->GetName();
    NewData->SourceSectionBase = FIntPoint(Component->SectionBaseX, Component->SectionBaseY);
    NewData->FoliageStaticMeshPaths.Reset();
    NewData->FoliageMaterialCounts.Reset();
    NewData->FoliageOverrideMaterialPaths.Reset();
    NewData->FoliageDefinitionIndices.Reset();
    NewData->FoliageInstanceLocalTransforms.Reset();
    NewData->FoliageInstanceTerrainOffsets.Reset();
    NewData->ActorFoliageClassPaths.Reset();
    NewData->ActorFoliageAttachFlags.Reset();
    NewData->ActorFoliageDefinitionIndices.Reset();
    NewData->ActorFoliageInstanceLocalTransforms.Reset();
    NewData->ActorFoliageInstanceTerrainOffsets.Reset();
    NewData->bTransferredFoliageFollowsTerrain = false;

    // 4. Extract Heavy Data
    int32 HeightRes = 0;
    if (ExtractHeightmapData(Component, NewData->InitialHeightMap, HeightRes))
    {
        NewData->Resolution = HeightRes;
    }
    else
    {
        UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: Failed to extract Heightmap for %s"), *AssetName);
    }

    int32 WeightRes = 0;
    if (ExtractWeightmapData(Component, NewData->InitialWeightMap, WeightRes, CanonicalLayerNames))
    {
		if (HeightRes > 0 && WeightRes != HeightRes)
		{
			UE_LOG(LogTerraDyneEditor, Error,
				TEXT("TerraDyneBaker: rejecting weight data for %s because height resolution %d does not match weight resolution %d."),
				*AssetName, HeightRes, WeightRes);
			NewData->InitialWeightMap.Reset();
		}
    }

    // 5. Save and Register
    NewData->MarkPackageDirty();
    SaveAssetPackage(NewData);

    return NewData;
}

bool UTerraDyneBaker::ExtractHeightmapData(ULandscapeComponent* Comp, TArray<uint16>& OutData, int32& OutRes)
{
	if (!Comp || Comp->ComponentSizeQuads <= 0)
	{
		return false;
	}
	ALandscapeProxy* Proxy = Comp->GetLandscapeProxy();
	ULandscapeInfo* Info = Proxy ? Proxy->GetLandscapeInfo() : nullptr;
	if (!Info)
	{
		UE_LOG(LogTerraDyneEditor, Warning,
			TEXT("TerraDyneBaker: LandscapeInfo unavailable for height extraction on %s."), *GetNameSafe(Comp));
		return false;
	}

	const int32 Quads = Comp->ComponentSizeQuads;
	const int32 Size = Quads + 1;
	const int32 X1 = Comp->SectionBaseX;
	const int32 Y1 = Comp->SectionBaseY;
	const int32 X2 = X1 + Quads;
	const int32 Y2 = Y1 + Quads;
	OutRes = Size;
	OutData.SetNumZeroed(Size * Size);

	// Read the component rect through Landscape's edit interface. Reading the backing texture mip
	// directly is incorrect when several components share a packed heightmap atlas.
	FLandscapeEditDataInterface EditData(Info);
	EditData.GetHeightDataFast(X1, Y1, X2, Y2, OutData.GetData(), Size);
	return OutData.Num() == Size * Size;
}

bool UTerraDyneBaker::ExtractWeightmapData(
	ULandscapeComponent* Comp,
	TArray<FColor>& OutData,
	int32& OutRes,
	const TArray<FName>* CanonicalLayerNames)
{
    // Use FLandscapeEditDataInterface — the public editor API for reading landscape weight data.
    // This avoids private WeightmapTextures access restrictions in UE 5.6.
    ALandscapeProxy* Proxy = Comp->GetLandscapeProxy();
    if (!Proxy)
    {
        UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: Component has no LandscapeProxy."));
        return false;
    }

    ULandscapeInfo* Info = Proxy->GetLandscapeInfo();
    if (!Info)
    {
        UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyneBaker: LandscapeInfo not available. Ensure landscape is registered."));
        return false;
    }

	TMap<FName, ULandscapeLayerInfoObject*> LayerInfoByName;
	TArray<ULandscapeLayerInfoObject*> LocalLayerOrder;
	for (const FWeightmapLayerAllocationInfo& Allocation : Comp->GetWeightmapLayerAllocations())
	{
		if (!Allocation.LayerInfo || Allocation.LayerInfo == Comp->GetVisibilityLayer())
		{
			continue;
		}
		const FName LayerName = ResolveLandscapeLayerName(Allocation.LayerInfo);
		LayerInfoByName.FindOrAdd(LayerName) = Allocation.LayerInfo;
		LocalLayerOrder.AddUnique(Allocation.LayerInfo);
	}

	// Asset-set bakes always use the same canonical RGBA meaning. A component that does not use
	// one of those layers writes zero into that channel instead of shifting another layer into it.
	TArray<ULandscapeLayerInfoObject*> LayerInfos;
	if (CanonicalLayerNames && CanonicalLayerNames->Num() > 0)
	{
		for (int32 Channel = 0; Channel < FMath::Min(4, CanonicalLayerNames->Num()); ++Channel)
		{
			ULandscapeLayerInfoObject* const* Found = LayerInfoByName.Find((*CanonicalLayerNames)[Channel]);
			LayerInfos.Add(Found ? *Found : nullptr);
		}
	}
	else
	{
		for (ULandscapeLayerInfoObject* LayerInfo : LocalLayerOrder)
		{
			LayerInfos.Add(LayerInfo);
			if (LayerInfos.Num() >= 4)
			{
				break;
			}
		}
	}

	if (LayerInfos.Num() == 0)
	{
		UE_LOG(LogTerraDyneEditor, Log, TEXT("TerraDyneBaker: No paint layers found on component — skipping weight extraction."));
		return false;
	}

    int32 Quads = Comp->ComponentSizeQuads;
    int32 Size = Quads + 1; // Vertex count = quads + 1
    OutRes = Size;
    OutData.SetNumZeroed(Size * Size);

    // Component bounds in landscape space
    int32 X1 = Comp->SectionBaseX;
    int32 Y1 = Comp->SectionBaseY;
    int32 X2 = X1 + Quads;
    int32 Y2 = Y1 + Quads;

    FLandscapeEditDataInterface EditData(Info);

	for (int32 LayerIdx = 0; LayerIdx < LayerInfos.Num(); ++LayerIdx)
	{
		ULandscapeLayerInfoObject* LayerInfo = LayerInfos[LayerIdx];
		if (!LayerInfo)
		{
			continue;
		}

		TArray<uint8> WeightData;
		WeightData.SetNumZeroed(Size * Size);
		EditData.GetWeightDataFast(LayerInfo, X1, Y1, X2, Y2, WeightData.GetData(), Size);
		for (int32 SampleIndex = 0; SampleIndex < WeightData.Num(); ++SampleIndex)
		{
			const uint8 Value = WeightData[SampleIndex];
			switch (LayerIdx)
			{
			case 0: OutData[SampleIndex].R = Value; break;
			case 1: OutData[SampleIndex].G = Value; break;
			case 2: OutData[SampleIndex].B = Value; break;
			case 3: OutData[SampleIndex].A = Value; break;
			default: break;
			}
		}

		UE_LOG(LogTerraDyneEditor, Log, TEXT("TerraDyneBaker: Extracted canonical channel %d (%s) — %d pixels"),
			LayerIdx, *ResolveLandscapeLayerName(LayerInfo).ToString(), WeightData.Num());
	}

    return true;
}

bool UTerraDyneBaker::HasVisibilityHoles(ULandscapeComponent* Comp)
{
	if (!Comp || Comp->ComponentSizeQuads <= 0)
	{
		return false;
	}
	ULandscapeLayerInfoObject* VisibilityLayer = Comp->GetVisibilityLayer();
	ALandscapeProxy* Proxy = Comp->GetLandscapeProxy();
	ULandscapeInfo* Info = Proxy ? Proxy->GetLandscapeInfo() : nullptr;
	if (!VisibilityLayer || !Info)
	{
		return false;
	}

	const int32 Quads = Comp->ComponentSizeQuads;
	const int32 Size = Quads + 1;
	const int32 X1 = Comp->SectionBaseX;
	const int32 Y1 = Comp->SectionBaseY;
	TArray<uint8> VisibilityData;
	VisibilityData.SetNumZeroed(Size * Size);
	// TODO: Instantiate FLandscapeEditDataInterface once before the calling loop
	// instead of per-component to reduce lock acquisition overhead.
	FLandscapeEditDataInterface EditData(Info);
	EditData.GetWeightDataFast(
		VisibilityLayer,
		X1,
		Y1,
		X1 + Quads,
		Y1 + Quads,
		VisibilityData.GetData(),
		Size);
	return VisibilityData.ContainsByPredicate([](uint8 Value) { return Value > 0; });
}
