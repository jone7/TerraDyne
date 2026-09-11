// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/NetSerialization.h"
#include "Core/TerraDyneSaveGame.h"
#include "TerraDyneTileData.generated.h"

UENUM(BlueprintType)
enum class ETerraDyneLayer : uint8
{
	Base    UMETA(DisplayName = "Base"),
	Sculpt  UMETA(DisplayName = "Sculpt"),
	Detail  UMETA(DisplayName = "Detail"),
	Active  UMETA(DisplayName = "Active Selection")
};

UENUM(BlueprintType)
enum class ETerraDyneBrushMode : uint8
{
	Raise   UMETA(DisplayName = "Raise"),
	Lower   UMETA(DisplayName = "Lower"),
	Flatten UMETA(DisplayName = "Flatten"),
	Smooth  UMETA(DisplayName = "Smooth"),
	Paint   UMETA(DisplayName = "Paint")
};

/** Parameters for a single brush application, sent over the network. */
USTRUCT(BlueprintType)
struct FTerraDyneBrushParams
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize WorldLocation = FVector::ZeroVector;

	UPROPERTY()
	float Radius = 2000.f;

	UPROPERTY()
	float Strength = 1.f;

	UPROPERTY()
	ETerraDyneBrushMode BrushMode = ETerraDyneBrushMode::Raise;

	UPROPERTY()
	ETerraDyneLayer TargetLayer = ETerraDyneLayer::Sculpt;

	UPROPERTY()
	int32 WeightLayerIndex = 0;

	UPROPERTY()
	float FlattenHeight = 0.f;

	UPROPERTY()
	bool bIsStrokeStart = false;
};

/**
 * UTerraDyneTileData
 * 
 * A static data asset containing baked terrain information for a specific chunk.
 * 
 * Workflow:
 * 1. Editor: Use UTerraDyneBaker to convert Landscape Components into these assets.
 * 2. Disk: Stored as compressed .uasset files.
 * 3. Runtime: Converted to FTerraDyneChunkData and loaded into TerraDyne chunks.
 */
UCLASS(BlueprintType)
class TERRADYNE_API UTerraDyneTileData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UTerraDyneTileData();

	//--- Data Payloads ---//

	/** 
	 * Quantized Height Data (16-bit Unsigned).
	 * Range: 0 (MinHeight) to 65535 (MaxHeight).
	 * This array is usually (Resolution * Resolution) in length.
	 * Using uint16 reduces memory footprint by 50% compared to float.
	 */
	 // Blueprints cannot read uint16 directly. We rely on C++ accessors if needed.
	UPROPERTY(VisibleAnywhere, Category = "Baked Data")
	TArray<uint16> InitialHeightMap;

	/**
	 * Layer Weight Data (RGBA).
	 * R = Layer 1 opacity
	 * G = Layer 2 opacity
	 * B = Layer 3 opacity
	 * A = Layer 4 opacity
	 * For >4 layers, you would need a second array or a custom struct.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Baked Data")
	TArray<FColor> InitialWeightMap;

	//--- Metadata ---//

	/** The vertex resolution of the tile (e.g., 128, 256). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dimensions")
	int32 Resolution;

	/** The physical size of this tile in Unreal Units (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dimensions")
	float RealWorldSize;

	//--- Scaling Info ---//

	/** 
	 * The Z-Scale multiplier used during baking.
	 * Used to de-quantize the uint16 data back to World Z floats.
	 * Standard calculation: Height(float) = (Data / 65535.0f) * BakedZScale;
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scaling")
	float BakedZScale;

	/** 
	 * TerraDyne chunk grid coordinate for this tile.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FIntPoint GridCoordinate;

	/** Static-mesh foliage transferred from the source Landscape component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<FString> FoliageStaticMeshPaths;

	/** Number of override materials stored per foliage definition. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<int32> FoliageMaterialCounts;

	/** Flattened list of override material paths for transferred foliage. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<FString> FoliageOverrideMaterialPaths;

	/** Per-instance mapping into FoliageStaticMeshPaths. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<int32> FoliageDefinitionIndices;

	/** Per-instance local transforms relative to the TerraDyne chunk actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<FTransform> FoliageInstanceLocalTransforms;

	/** Preserved offset above the terrain surface for each foliage instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<float> FoliageInstanceTerrainOffsets;

	/** Actor foliage class paths transferred from the source Landscape component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<FString> ActorFoliageClassPaths;

	/** Whether the transferred actor foliage should attach to the chunk root. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<uint8> ActorFoliageAttachFlags;

	/** Per-instance mapping into ActorFoliageClassPaths. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<int32> ActorFoliageDefinitionIndices;

	/** Per-instance actor foliage transforms relative to the TerraDyne chunk actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<FTransform> ActorFoliageInstanceLocalTransforms;

	/** Preserved offset above the terrain surface for each transferred actor foliage instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	TArray<float> ActorFoliageInstanceTerrainOffsets;

	/** Whether transferred foliage should follow later runtime terrain deformation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Foliage")
	bool bTransferredFoliageFollowsTerrain = false;

#if WITH_EDITORONLY_DATA
	/** Source Landscape Component name (for re-baking traceability). */
	UPROPERTY(VisibleAnywhere, Category = "Source Info")
	FString SourceComponentName;

	/** Original Landscape section base in landscape-space quads. */
	UPROPERTY(VisibleAnywhere, Category = "Source Info")
	FIntPoint SourceSectionBase = FIntPoint::ZeroValue;
#endif

public:
	/**
	 * Helper to get the number of bytes this asset consumes in memory.
	 */
	UFUNCTION(BlueprintPure, Category = "TerraDyne")
	int32 GetMemoryFootprint() const;

	/** Converts the baked asset payload into the runtime chunk serialization format. */
	UFUNCTION(BlueprintPure, Category = "TerraDyne")
	FTerraDyneChunkData BuildChunkData() const;
};
