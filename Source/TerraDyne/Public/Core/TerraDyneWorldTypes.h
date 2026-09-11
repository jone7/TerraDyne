// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "TerraDyneWorldTypes.generated.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;

UENUM(BlueprintType)
enum class ETerraDynePopulationKind : uint8
{
	Prop UMETA(DisplayName = "Prop"),
	Harvestable UMETA(DisplayName = "Harvestable"),
	Destroyable UMETA(DisplayName = "Destroyable"),
	RuntimePlaced UMETA(DisplayName = "Runtime Placed"),
	ProceduralSpawn UMETA(DisplayName = "Procedural Spawn")
};

UENUM(BlueprintType)
enum class ETerraDynePopulationState : uint8
{
	Active UMETA(DisplayName = "Active"),
	Harvested UMETA(DisplayName = "Harvested"),
	Destroyed UMETA(DisplayName = "Destroyed"),
	Regrowing UMETA(DisplayName = "Regrowing")
};

UENUM(BlueprintType)
enum class ETerraDyneBuildPermission : uint8
{
	Inherit UMETA(DisplayName = "Inherit"),
	Allowed UMETA(DisplayName = "Allowed"),
	Blocked UMETA(DisplayName = "Blocked")
};

UENUM(BlueprintType)
enum class ETerraDyneNotifySeverity : uint8
{
	Info    UMETA(DisplayName = "Info"),
	Warning UMETA(DisplayName = "Warning"),
	Error   UMETA(DisplayName = "Error")
};

USTRUCT(BlueprintType)
struct FTerraDynePopulationDescriptor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	ETerraDynePopulationKind Kind = ETerraDynePopulationKind::Prop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FName TypeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bReplicates = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bAttachToChunk = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bSnapToTerrain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	float TerrainOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bAllowRegrowth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	float RegrowthDelaySeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FName BiomeTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	TArray<FName> Tags;
};

USTRUCT(BlueprintType)
struct FTerraDynePersistentPopulationEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FGuid PopulationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FTerraDynePopulationDescriptor Descriptor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FTransform LocalTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	ETerraDynePopulationState State = ETerraDynePopulationState::Active;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	float RemainingRegrowthTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bSpawnedFromRule = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName SourceRuleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName CachedBiomeTag = NAME_None;
};

USTRUCT(BlueprintType)
struct FTerraDynePopulationSpawnRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FName RuleId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FTerraDynePopulationDescriptor Descriptor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bAllowOnAuthoredChunks = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bAllowOnProceduralChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FName RequiredBiomeTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 MinDistanceFromAuthored = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 MaxDistanceFromAuthored = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 MinInstancesPerChunk = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 MaxInstancesPerChunk = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	float ChunkPadding = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	float MinSpacing = 300.0f;
};

USTRUCT(BlueprintType)
struct FTerraDyneBiomeOverlay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FName BiomeTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 WeightLayerIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	float MinimumPaintWeight = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bApplyToAuthoredChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bApplyToProceduralChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	float ProceduralNoiseMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	float ProceduralNoiseMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 Priority = 0;
};

USTRUCT(BlueprintType)
struct FTerraDyneProceduralWorldSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 WorldSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bEnableSeededOutskirts = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bEnableInfiniteEdgeGrowth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 ProceduralOutskirtsExtent = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bGeneratePopulationFromRules = true;
};

USTRUCT(BlueprintType)
struct FTerraDyneProceduralChunkState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FIntPoint Coordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 ChunkSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bIsAuthoredChunk = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bGeneratedOutskirtsChunk = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bPopulationGenerated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 DistanceFromAuthored = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName PrimaryBiomeTag = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FName> OverlayBiomeTags;
};

USTRUCT(BlueprintType)
struct FTerraDyneAISpawnZone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FName ZoneId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FBox LocalBounds = FBox(FVector(-5000.0f, -5000.0f, -500.0f), FVector(5000.0f, 5000.0f, 5000.0f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	bool bProceduralOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FName RequiredBiomeTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	int32 MaxConcurrentSpawns = 6;
};

USTRUCT(BlueprintType)
struct FTerraDyneBuildPermissionZone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FName ZoneId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FBox LocalBounds = FBox(FVector(-5000.0f, -5000.0f, -500.0f), FVector(5000.0f, 5000.0f, 5000.0f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	ETerraDyneBuildPermission Permission = ETerraDyneBuildPermission::Inherit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	FString Reason;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne")
	float MaxSlopeDegrees = 45.0f;
};

USTRUCT(BlueprintType)
struct FTerraDynePCGPoint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName BiomeTag = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName SourceId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	ETerraDynePopulationKind PopulationKind = ETerraDynePopulationKind::Prop;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	float Density = 1.0f;
};

USTRUCT(BlueprintType)
struct FTerraDyneTerrainChangeEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FBox WorldBounds = FBox(EForceInit::ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bAffectsNavigation = false;
};

USTRUCT(BlueprintType)
struct FTerraDyneFoliageChangeEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	int32 ImportedFoliageInstanceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bGrassRegenerationQueued = false;
};

USTRUCT(BlueprintType)
struct FTerraDynePopulationChangeEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FGuid PopulationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName TypeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	ETerraDynePopulationKind Kind = ETerraDynePopulationKind::Prop;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	ETerraDynePopulationState State = ETerraDynePopulationState::Active;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName Reason = NAME_None;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTerraDyneTerrainChangedDelegate, const FTerraDyneTerrainChangeEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTerraDyneFoliageChangedDelegate, const FTerraDyneFoliageChangeEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTerraDynePopulationChangedDelegate, const FTerraDynePopulationChangeEvent&, Event);
