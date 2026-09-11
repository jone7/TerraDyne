// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneOrchestrator.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

namespace TerraDyneMigrationTests
{
	class FWaitForMigrationSaveCommand final : public IAutomationLatentCommand
	{
	public:
		FWaitForMigrationSaveCommand(
			FAutomationTestBase* InTest,
			ATerraDyneManager* InManager,
			FString InSlotName)
			: Test(InTest)
			, Manager(InManager)
			, SlotName(MoveTemp(InSlotName))
			, DeadlineSeconds(FPlatformTime::Seconds() + 10.0)
		{
		}

		virtual bool Update() override
		{
			if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
			{
				if (FPlatformTime::Seconds() < DeadlineSeconds)
				{
					return false;
				}
				Test->AddError(FString::Printf(TEXT("Timed out waiting for async migration save slot '%s'."), *SlotName));
				return true;
			}

			ATerraDyneManager* ManagerPtr = Manager.Get();
			if (!ManagerPtr)
			{
				Test->AddError(TEXT("Manager was destroyed before migration save validation."));
				UGameplayStatics::DeleteGameInSlot(SlotName, 0);
				return true;
			}

			ManagerPtr->LoadWorld(SlotName);
			Test->TestEqual(TEXT("Manager location restored"), ManagerPtr->GetActorLocation(), FVector(123.0f, 456.0f, 789.0f));
			Test->TestEqual(TEXT("GlobalChunkSize restored"), ManagerPtr->GlobalChunkSize, 2048.0f);
			Test->TestTrue(TEXT("Landscape migration flag restored"), ManagerPtr->LandscapeMigrationState.bWasImportedFromLandscape);
			Test->TestEqual(TEXT("Source landscape name restored"), ManagerPtr->LandscapeMigrationState.SourceLandscapeName, FString(TEXT("TestLandscape")));
			Test->TestEqual(TEXT("Layer mapping count restored"), ManagerPtr->LandscapeMigrationState.LayerMappings.Num(), 2);
			Test->TestEqual(TEXT("Unmapped layer count restored"), ManagerPtr->LandscapeMigrationState.UnmappedLayerNames.Num(), 1);
			Test->TestEqual(TEXT("Procedural seed restored"), ManagerPtr->ProceduralWorldSettings.WorldSeed, 20260314);
			Test->TestEqual(TEXT("Biome overlay count restored"), ManagerPtr->BiomeOverlays.Num(), 1);
			Test->TestEqual(TEXT("Build permission zone count restored"), ManagerPtr->BuildPermissionZones.Num(), 1);
			Test->TestNotNull(TEXT("Master material restored"), ManagerPtr->MasterMaterial.Get());

			UGameplayStatics::DeleteGameInSlot(SlotName, 0);
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<ATerraDyneManager> Manager;
		FString SlotName;
		double DeadlineSeconds = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneMigrationSaveLoadTest,
	"TerraDyne.Migration.SaveLoadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneMigrationSaveLoadTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull("Manager", Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->SetActorLocation(FVector(123.0f, 456.0f, 789.0f));
	Manager->GlobalChunkSize = 2048.0f;
	Manager->MasterMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	Manager->LandscapeMigrationState.bWasImportedFromLandscape = true;
	Manager->LandscapeMigrationState.bImportedPaintLayers = true;
	Manager->LandscapeMigrationState.bRegenerateGrassFromImportedLayers = true;
	Manager->LandscapeMigrationState.bSourceLandscapeHidden = true;
	Manager->LandscapeMigrationState.SourceLandscapeName = TEXT("TestLandscape");
	Manager->LandscapeMigrationState.SourceLandscapePath = TEXT("/Game/Test/TestLandscape");
	Manager->LandscapeMigrationState.SourceLandscapeMaterialPath = TEXT("/Game/Test/M_Landscape");
	Manager->LandscapeMigrationState.ImportedChunkSize = 2048.0f;
	Manager->LandscapeMigrationState.ImportedComponentCount = 4;
	Manager->LandscapeMigrationState.ImportedWeightLayerCount = 2;
	Manager->LandscapeMigrationState.ImportedAtIso8601 = TEXT("2026-03-14T12:00:00Z");
	Manager->ProceduralWorldSettings.WorldSeed = 20260314;
	Manager->ProceduralWorldSettings.bEnableSeededOutskirts = true;
	Manager->ProceduralWorldSettings.ProceduralOutskirtsExtent = 3;

	FTerraDyneBiomeOverlay ForestBiome;
	ForestBiome.BiomeTag = TEXT("Forest");
	ForestBiome.bApplyToProceduralChunks = true;
	ForestBiome.ProceduralNoiseMin = 0.0f;
	ForestBiome.ProceduralNoiseMax = 1.0f;
	ForestBiome.Priority = 1;
	Manager->BiomeOverlays.Add(ForestBiome);

	FTerraDyneBuildPermissionZone BuildZone;
	BuildZone.ZoneId = TEXT("NoBuildCore");
	BuildZone.Permission = ETerraDyneBuildPermission::Blocked;
	BuildZone.Reason = TEXT("Core settlement reserve");
	Manager->BuildPermissionZones.Add(BuildZone);

	FTerraDyneLandscapeLayerMapping GrassLayer;
	GrassLayer.SourceLayerName = TEXT("Grass");
	GrassLayer.TerraDyneWeightLayerIndex = 0;
	Manager->LandscapeMigrationState.LayerMappings.Add(GrassLayer);

	FTerraDyneLandscapeLayerMapping RockLayer;
	RockLayer.SourceLayerName = TEXT("Rock");
	RockLayer.TerraDyneWeightLayerIndex = 1;
	Manager->LandscapeMigrationState.LayerMappings.Add(RockLayer);

	Manager->LandscapeMigrationState.UnmappedLayerNames.Add(TEXT("Snow"));

	const FString SlotName = FString::Printf(TEXT("TerraDyneMigration_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));

	Manager->SaveWorld(SlotName);

	Manager->SetActorLocation(FVector::ZeroVector);
	Manager->GlobalChunkSize = 1.0f;
	Manager->MasterMaterial = nullptr;
	Manager->LandscapeMigrationState = FTerraDyneLandscapeMigrationState();

	ADD_LATENT_AUTOMATION_COMMAND(
		TerraDyneMigrationTests::FWaitForMigrationSaveCommand(this, Manager, SlotName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneShowcaseMaterialSelectionTest,
	"TerraDyne.Demo.ShowcaseMaterialSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneShowcaseMaterialSelectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	UGameplayStatics::DeleteGameInSlot(TEXT("TerraDyne_ShowcaseBaseline"), 0);

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull("Manager", Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->bSpawnDefaultChunksOnBeginPlay = false;
	Manager->bSetupDefaultLightingOnBeginPlay = false;
	Manager->bSpawnShowcaseOnBeginPlay = false;
	Manager->MasterMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Manager->LandscapeMigrationState.SourceLandscapeMaterialPath =
		TEXT("/Game/MWLandscapeAutoMaterial/Materials/Landscape/MTL_MWAM_Landscape_MountainRangeExample.MTL_MWAM_Landscape_MountainRangeExample");

	ATerraDyneOrchestrator* Orchestrator = World->SpawnActor<ATerraDyneOrchestrator>();
	TestNotNull("Orchestrator", Orchestrator);
	if (!Orchestrator)
	{
		return false;
	}

	Orchestrator->DispatchBeginPlay();

	TestNotNull("Showcase material assigned", Manager->MasterMaterial.Get());
	if (!Manager->MasterMaterial)
	{
		return false;
	}

	TestEqual(
		TEXT("Showcase uses the TerraDyne mesh-compatible terrain material"),
		Manager->MasterMaterial->GetPathName(),
		FString(TEXT("/TerraDyne/Materials/VHFM/M_TerraDyne_Master.M_TerraDyne_Master")));

	ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(FIntPoint::ZeroValue);
	TestNotNull("Showcase authored chunk spawned", Chunk);

	UGameplayStatics::DeleteGameInSlot(TEXT("TerraDyne_ShowcaseBaseline"), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneChunkMaterialBindingTest,
	"TerraDyne.Rendering.MaterialBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneChunkMaterialBindingTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint::ZeroValue;
	Chunk->WorldSize = 2048.0f;
	Chunk->ChunkSizeWorldUnits = 2048.0f;
	Chunk->ZScale = 768.0f;
	Chunk->BrushMaterialBase = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/TerraDyne/Materials/Tools/M_HeightBrush.M_HeightBrush"));
	UMaterialInterface* MasterMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/TerraDyne/Materials/VHFM/M_TerraDyne_Master.M_TerraDyne_Master"));
	TestNotNull("Height brush material", Chunk->BrushMaterialBase.Get());
	TestNotNull("Master material", MasterMaterial);
	if (!Chunk->BrushMaterialBase || !MasterMaterial)
	{
		return false;
	}

	Chunk->Initialize(32, 2048.0f);
	const bool bRenderResourcesExpected = FApp::CanEverRender() && !IsRunningDedicatedServer();
	if (!bRenderResourcesExpected)
	{
		AddInfo(TEXT("Headless runtime detected; validating that rendering resources stay unallocated while CPU terrain state remains usable."));
		TestNull("Headless chunk should not allocate a height RT", Chunk->HeightRT.Get());
		TestNull("Headless chunk should not allocate a weight texture", Chunk->WeightTexture.Get());
		Chunk->SetMaterial(MasterMaterial);
		TestNull("Headless chunk should not create a dynamic terrain MID",
			Cast<UMaterialInstanceDynamic>(Chunk->DynamicMeshComp->GetMaterial(0)));

		const int32 NumSamples = Chunk->Resolution * Chunk->Resolution;
		Chunk->BaseBuffer.Init(0.6f, NumSamples);
		Chunk->SculptBuffer.Init(0.0f, NumSamples);
		Chunk->DetailBuffer.Init(0.0f, NumSamples);
		Chunk->HeightBuffer.Init(0.6f, NumSamples);
		const FTerraDyneChunkData SerializedData = Chunk->GetSerializedData();
		TestEqual("Serialized height sample count", SerializedData.HeightData.Num(), NumSamples);
		TestTrue("CPU height state remains serializable in headless mode",
			SerializedData.HeightData.Num() > 0 &&
			FMath::IsNearlyEqual(SerializedData.HeightData[0], 0.6f, 0.02f));
		return true;
	}

	if (!Chunk->IsUsingGPU())
	{
		AddInfo(TEXT("GPU terrain path unavailable under the current automation RHI; validating material bindings with CPU fallback."));
	}
	TestNotNull("Height RT created", Chunk->HeightRT.Get());
	TestNotNull("Weight texture created", Chunk->WeightTexture.Get());

	Chunk->SetMaterial(MasterMaterial);
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Chunk->DynamicMeshComp->GetMaterial(0));
	TestNotNull("Dynamic terrain MID", MID);
	if (!MID)
	{
		return false;
	}

	TestEqual("HeightMap bound to chunk RT", MID->K2_GetTextureParameterValue(TEXT("HeightMap")), static_cast<UTexture*>(Chunk->HeightRT));
	TestEqual("WeightMap bound to chunk texture", MID->K2_GetTextureParameterValue(TEXT("WeightMap")), static_cast<UTexture*>(Chunk->WeightTexture));
	TestTrue("WorldSize scalar synced",
		FMath::IsNearlyEqual(MID->K2_GetScalarParameterValue(TEXT("WorldSize")), Chunk->WorldSize, KINDA_SMALL_NUMBER));
	TestTrue("ZScale scalar synced",
		FMath::IsNearlyEqual(MID->K2_GetScalarParameterValue(TEXT("ZScale")), Chunk->ZScale, KINDA_SMALL_NUMBER));
	TestTrue("TexelStep scalar set", MID->K2_GetScalarParameterValue(TEXT("TexelStep")) > 0.0f);

	const int32 NumSamples = Chunk->Resolution * Chunk->Resolution;
	Chunk->BaseBuffer.Init(0.6f, NumSamples);
	Chunk->SculptBuffer.Init(0.0f, NumSamples);
	Chunk->DetailBuffer.Init(0.0f, NumSamples);
	Chunk->HeightBuffer.Init(0.6f, NumSamples);
	Chunk->RebuildPhysicsMesh();

	const FTerraDyneChunkData SerializedData = Chunk->GetSerializedData();
	TestEqual("Serialized height sample count", SerializedData.HeightData.Num(), NumSamples);
	TestTrue("Serialized height stays synchronized after CPU mesh rebuild",
		SerializedData.HeightData.Num() > 0 &&
		FMath::IsNearlyEqual(SerializedData.HeightData[0], 0.6f, 0.02f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneChunkSerializationSanitizesResolutionTest,
	"TerraDyne.Rendering.SerializationSanitizesResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneChunkSerializationSanitizesResolutionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	ATerraDyneChunk* SourceChunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Source chunk", SourceChunk);
	if (!SourceChunk)
	{
		return false;
	}

	SourceChunk->GridCoordinate = FIntPoint::ZeroValue;
	SourceChunk->WorldSize = 1000.0f;
	SourceChunk->ChunkSizeWorldUnits = 1000.0f;
	SourceChunk->Initialize(128, 1000.0f);

	const int32 TrueResolution = SourceChunk->Resolution;
	TestEqual("Source chunk initialized to 128", TrueResolution, 128);

	SourceChunk->Resolution = 96;
	const FTerraDyneChunkData SerializedData = SourceChunk->GetSerializedData();
	TestEqual("Serialized resolution derived from buffer size", SerializedData.Resolution, TrueResolution);
	TestEqual("Serialized height sample count preserved", SerializedData.HeightData.Num(), TrueResolution * TrueResolution);

	ATerraDyneChunk* LoadedChunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Loaded chunk", LoadedChunk);
	if (!LoadedChunk)
	{
		return false;
	}

	LoadedChunk->GridCoordinate = FIntPoint::ZeroValue;
	LoadedChunk->WorldSize = 1000.0f;
	LoadedChunk->ChunkSizeWorldUnits = 1000.0f;
	LoadedChunk->LoadFromData(SerializedData);

	TestEqual("Loaded resolution corrected from serialized data", LoadedChunk->Resolution, TrueResolution);
	TestEqual("Loaded height data count matches corrected resolution", LoadedChunk->HeightBuffer.Num(), TrueResolution * TrueResolution);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneTransferredFoliageRoundTripTest,
	"TerraDyne.Migration.TransferredFoliageRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneTransferredFoliageRoundTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	ATerraDyneChunk* SourceChunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Source chunk", SourceChunk);
	if (!SourceChunk)
	{
		return false;
	}

	SourceChunk->GridCoordinate = FIntPoint::ZeroValue;
	SourceChunk->WorldSize = 1000.0f;
	SourceChunk->ChunkSizeWorldUnits = 1000.0f;
	SourceChunk->Initialize(16, 1000.0f);

	const int32 NumSamples = SourceChunk->Resolution * SourceChunk->Resolution;
	SourceChunk->BaseBuffer.Init(0.25f, NumSamples);
	SourceChunk->SculptBuffer.Init(0.0f, NumSamples);
	SourceChunk->DetailBuffer.Init(0.0f, NumSamples);
	SourceChunk->HeightBuffer.Init(0.25f, NumSamples);
	SourceChunk->RebuildPhysicsMesh();

	const FVector LocalPointA(-100.0f, -100.0f, 0.0f);
	const FVector LocalPointB(120.0f, 150.0f, 0.0f);
	const float HeightA = SourceChunk->GetHeightAtLocation(LocalPointA);
	const float HeightB = SourceChunk->GetHeightAtLocation(LocalPointB);

	TArray<FString> StaticMeshPaths;
	StaticMeshPaths.Add(TEXT("/Engine/BasicShapes/Cube.Cube"));

	TArray<int32> MaterialCounts;
	MaterialCounts.Add(0);

	TArray<int32> DefinitionIndices;
	DefinitionIndices.Add(0);
	DefinitionIndices.Add(0);

	TArray<FTransform> LocalTransforms;
	{
		FTransform TransformA;
		TransformA.SetLocation(FVector(LocalPointA.X, LocalPointA.Y, HeightA + 50.0f));
		TransformA.SetScale3D(FVector(1.0f, 1.0f, 1.0f));
		LocalTransforms.Add(TransformA);

		FTransform TransformB;
		TransformB.SetLocation(FVector(LocalPointB.X, LocalPointB.Y, HeightB + 10.0f));
		TransformB.SetScale3D(FVector(0.75f, 0.75f, 1.25f));
		LocalTransforms.Add(TransformB);
	}

	TArray<float> TerrainOffsets;
	TerrainOffsets.Add(50.0f);
	TerrainOffsets.Add(10.0f);

	SourceChunk->SetTransferredFoliageData(
		StaticMeshPaths,
		MaterialCounts,
		TArray<FString>(),
		DefinitionIndices,
		LocalTransforms,
		TerrainOffsets,
		true);

	TestEqual("Transferred foliage definition count", SourceChunk->GetTransferredFoliageDefinitionCount(), 1);
	TestEqual("Transferred foliage instance count", SourceChunk->GetTransferredFoliageInstanceCount(), 2);
	TestEqual("Transferred foliage component count", SourceChunk->TransferredFoliageISMs.Num(), 1);

	FTransform InitialInstanceTransform;
	TestTrue("Initial foliage instance available",
		SourceChunk->TransferredFoliageISMs[0] &&
		SourceChunk->TransferredFoliageISMs[0]->GetInstanceTransform(0, InitialInstanceTransform, false));
	TestTrue("Initial foliage Z snapped to terrain plus offset",
		FMath::IsNearlyEqual(
			InitialInstanceTransform.GetLocation().Z,
			static_cast<float>(HeightA + 50.0f),
			UE_KINDA_SMALL_NUMBER));

	SourceChunk->BaseBuffer.Init(0.5f, NumSamples);
	SourceChunk->HeightBuffer.Init(0.5f, NumSamples);
	SourceChunk->RebuildPhysicsMesh();
	SourceChunk->RefreshTransferredFoliagePlacement(true);

	FTransform UpdatedInstanceTransform;
	TestTrue("Updated foliage instance available",
		SourceChunk->TransferredFoliageISMs[0] &&
		SourceChunk->TransferredFoliageISMs[0]->GetInstanceTransform(0, UpdatedInstanceTransform, false));
	TestTrue("Updated foliage Z follows terrain plus offset",
		FMath::IsNearlyEqual(
			UpdatedInstanceTransform.GetLocation().Z,
			static_cast<float>(SourceChunk->GetHeightAtLocation(LocalPointA) + 50.0f),
			UE_KINDA_SMALL_NUMBER));

	const FTerraDyneChunkData SerializedData = SourceChunk->GetSerializedData();

	ATerraDyneChunk* LoadedChunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Loaded chunk", LoadedChunk);
	if (!LoadedChunk)
	{
		return false;
	}

	LoadedChunk->GridCoordinate = FIntPoint::ZeroValue;
	LoadedChunk->WorldSize = 1000.0f;
	LoadedChunk->ChunkSizeWorldUnits = 1000.0f;
	LoadedChunk->Initialize(16, 1000.0f);
	LoadedChunk->LoadFromData(SerializedData);

	TestEqual("Loaded chunk transferred foliage definition count", LoadedChunk->GetTransferredFoliageDefinitionCount(), 1);
	TestEqual("Loaded chunk transferred foliage instance count", LoadedChunk->GetTransferredFoliageInstanceCount(), 2);
	TestEqual("Loaded chunk foliage component count", LoadedChunk->TransferredFoliageISMs.Num(), 1);

	FTransform LoadedInstanceTransform;
	TestTrue("Loaded foliage instance available",
		LoadedChunk->TransferredFoliageISMs[0] &&
		LoadedChunk->TransferredFoliageISMs[0]->GetInstanceTransform(0, LoadedInstanceTransform, false));
	TestEqual("Loaded foliage Z preserved", LoadedInstanceTransform.GetLocation().Z, UpdatedInstanceTransform.GetLocation().Z);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneTransferredActorFoliageRoundTripTest,
	"TerraDyne.Migration.TransferredActorFoliageRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneTransferredActorFoliageRoundTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	ATerraDyneChunk* SourceChunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Source chunk", SourceChunk);
	if (!SourceChunk)
	{
		return false;
	}

	SourceChunk->GridCoordinate = FIntPoint::ZeroValue;
	SourceChunk->WorldSize = 1000.0f;
	SourceChunk->ChunkSizeWorldUnits = 1000.0f;
	SourceChunk->Initialize(16, 1000.0f);

	const int32 NumSamples = SourceChunk->Resolution * SourceChunk->Resolution;
	SourceChunk->BaseBuffer.Init(0.20f, NumSamples);
	SourceChunk->SculptBuffer.Init(0.0f, NumSamples);
	SourceChunk->DetailBuffer.Init(0.0f, NumSamples);
	SourceChunk->HeightBuffer.Init(0.20f, NumSamples);
	SourceChunk->RebuildPhysicsMesh();

	const FVector LocalPoint(-80.0f, 110.0f, 0.0f);
	const float InitialHeight = SourceChunk->GetHeightAtLocation(LocalPoint);

	TArray<FString> ActorClassPaths;
	ActorClassPaths.Add(TEXT("/Script/Engine.StaticMeshActor"));

	TArray<uint8> AttachFlags;
	AttachFlags.Add(1);

	TArray<int32> DefinitionIndices;
	DefinitionIndices.Add(0);

	TArray<FTransform> LocalTransforms;
	FTransform ActorLocalTransform;
	ActorLocalTransform.SetLocation(FVector(LocalPoint.X, LocalPoint.Y, InitialHeight + 75.0f));
	LocalTransforms.Add(ActorLocalTransform);

	TArray<float> TerrainOffsets;
	TerrainOffsets.Add(75.0f);

	SourceChunk->SetTransferredActorFoliageData(
		ActorClassPaths,
		AttachFlags,
		DefinitionIndices,
		LocalTransforms,
		TerrainOffsets);
	SourceChunk->SetTransferredFoliageFollowsTerrain(true);
	SourceChunk->RefreshTransferredActorFoliagePlacement(true);

	TestEqual("Transferred actor foliage definition count", SourceChunk->GetTransferredActorFoliageDefinitionCount(), 1);
	TestEqual("Transferred actor foliage instance count", SourceChunk->GetTransferredActorFoliageInstanceCount(), 1);

	FTransform InitialActorTransform;
	TestTrue("Transferred actor foliage instance available",
		SourceChunk->GetTransferredActorFoliageInstanceTransform(0, InitialActorTransform));
	TestTrue("Transferred actor foliage Z snapped to terrain plus offset",
		FMath::IsNearlyEqual(
			InitialActorTransform.GetLocation().Z,
			static_cast<float>(InitialHeight + 75.0f),
			UE_KINDA_SMALL_NUMBER));

	SourceChunk->BaseBuffer.Init(0.45f, NumSamples);
	SourceChunk->HeightBuffer.Init(0.45f, NumSamples);
	SourceChunk->RebuildPhysicsMesh();
	SourceChunk->RefreshTransferredActorFoliagePlacement(true);

	FTransform UpdatedActorTransform;
	TestTrue("Updated transferred actor foliage instance available",
		SourceChunk->GetTransferredActorFoliageInstanceTransform(0, UpdatedActorTransform));
	TestTrue("Transferred actor foliage follows terrain plus offset",
		FMath::IsNearlyEqual(
			UpdatedActorTransform.GetLocation().Z,
			static_cast<float>(SourceChunk->GetHeightAtLocation(LocalPoint) + 75.0f),
			UE_KINDA_SMALL_NUMBER));

	const FTerraDyneChunkData SerializedData = SourceChunk->GetSerializedData();

	ATerraDyneChunk* LoadedChunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Loaded chunk", LoadedChunk);
	if (!LoadedChunk)
	{
		return false;
	}

	LoadedChunk->GridCoordinate = FIntPoint::ZeroValue;
	LoadedChunk->WorldSize = 1000.0f;
	LoadedChunk->ChunkSizeWorldUnits = 1000.0f;
	LoadedChunk->Initialize(16, 1000.0f);
	LoadedChunk->LoadFromData(SerializedData);

	TestEqual("Loaded actor foliage definition count", LoadedChunk->GetTransferredActorFoliageDefinitionCount(), 1);
	TestEqual("Loaded actor foliage instance count", LoadedChunk->GetTransferredActorFoliageInstanceCount(), 1);

	FTransform LoadedActorTransform;
	TestTrue("Loaded transferred actor foliage instance available",
		LoadedChunk->GetTransferredActorFoliageInstanceTransform(0, LoadedActorTransform));
	TestEqual("Loaded transferred actor foliage Z preserved",
		LoadedActorTransform.GetLocation().Z,
		UpdatedActorTransform.GetLocation().Z);

	return true;
}
#endif
