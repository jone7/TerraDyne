// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Core/TerraDyneWorldTypes.h"
#include "TerraDyneSaveGame.generated.h"

USTRUCT(BlueprintType)
struct FTerraDyneChunkData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FIntPoint Coordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 Resolution = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	float ZScale = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<float> HeightData;

	// Separate height layers (absent on old saves → graceful fallback to HeightData)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<float> BaseData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<float> SculptData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<float> DetailData;

	// 4 weight layers packed as RGBA8: size = 4 * Resolution * Resolution
	// Empty on old saves → zeroed weight buffers on load (graceful)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<uint8> WeightData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bTransferredFoliageFollowsTerrain = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FString> FoliageStaticMeshPaths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<int32> FoliageMaterialCounts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FString> FoliageOverrideMaterialPaths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<int32> FoliageDefinitionIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTransform> FoliageInstanceLocalTransforms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<float> FoliageInstanceTerrainOffsets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FString> ActorFoliageClassPaths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<uint8> ActorFoliageAttachFlags;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<int32> ActorFoliageDefinitionIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTransform> ActorFoliageInstanceLocalTransforms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<float> ActorFoliageInstanceTerrainOffsets;
};

USTRUCT(BlueprintType)
struct FTerraDyneLandscapeLayerMapping
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName SourceLayerName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 TerraDyneWeightLayerIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct FTerraDyneLandscapeMigrationState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bWasImportedFromLandscape = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bImportedPaintLayers = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bRegenerateGrassFromImportedLayers = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bSourceLandscapeHidden = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bAdoptedLandscapeMaterial = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bTransferredPlacedFoliage = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bTransferredFoliageFollowsTerrain = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FString SourceLandscapeName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FString SourceLandscapePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FString SourceLandscapeMaterialPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FVector SourceLandscapeLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FVector SourceLandscapeScale = FVector::OneVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FVector RuntimeManagerLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	float ImportedChunkSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 ImportedResolution = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 ImportedComponentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 ImportedWeightLayerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 ImportedFoliageDefinitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 ImportedFoliageInstanceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FString ImportedAtIso8601;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDyneLandscapeLayerMapping> LayerMappings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FString> UnmappedLayerNames;
};

UCLASS()
class TERRADYNE_API UTerraDyneSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Explicit schema marker for project-level world saves. Zero identifies legacy saves. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 SaveFormatVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FDateTime Timestamp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FVector ManagerLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	float SavedGlobalChunkSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FString MasterMaterialPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FString ActiveGrassProfilePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FTerraDyneLandscapeMigrationState LandscapeMigration;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FTerraDyneProceduralWorldSettings ProceduralWorldSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDyneBiomeOverlay> BiomeOverlays;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDynePopulationSpawnRule> PopulationSpawnRules;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDyneAISpawnZone> AISpawnZones;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDyneBuildPermissionZone> BuildPermissionZones;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDyneProceduralChunkState> ProceduralChunks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDynePersistentPopulationEntry> PersistentPopulationEntries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FIntPoint> AuthoredChunkCoords;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TMap<FIntPoint, FTerraDyneChunkData> Chunks;
};
