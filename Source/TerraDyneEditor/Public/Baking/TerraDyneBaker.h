// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/TerraDyneManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TerraDyneBaker.generated.h"

// Forward Declarations
class ULandscapeComponent;
class ALandscapeProxy;
class UTerraDyneLandscapeAssetSet;
class UTerraDyneTileData;

/**
 * UTerraDyneBaker
 * 
 * Editor-only utility library responsible for converting stock Unreal Landscapes
 * into TerraDyne's authored-world runtime assets.
 * 
 * This processes high-res textures (height/weight) and quantizes/compresses 
 * them for efficient runtime streaming.
 */
UCLASS()
class TERRADYNEEDITOR_API UTerraDyneBaker : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Bakes a Landscape into a runtime-safe authored-world asset set and saves the generated packages.
	 * This is the packaged-build path used by the unified authored conversion workflow.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Baking")
	static UTerraDyneLandscapeAssetSet* BakeLandscapeToAssetSet(
		ALandscapeProxy* SourceLandscape,
		FString DestinationPath,
		const FTerraDyneLandscapeMigrationOptions& Options);

	/**
	 * Bakes a single Landscape Component into a Data Asset.
	 * Useful for partial updates or selective baking.
	 * 
	 * @param Component         The specific component to process.
	 * @param DestinationPath   Directory to save the asset.
	 * @param AssetNameOverride Optional name. IF Key is empty, generates name based on Grid Coordinates.
	 * @return                  The created and saved Data Asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Baking")
	static UTerraDyneTileData* BakeComponent(ULandscapeComponent* Component, FString DestinationPath, FString AssetNameOverride = TEXT(""));

private:
	/**
	 * Internal helper to lock and read Landscape Source Texture data.
	 * Extracts 16-bit height values from the specific RG channel encoding used by UE Landscape.
	 */
	static bool ExtractHeightmapData(ULandscapeComponent* Comp, TArray<uint16>& OutData, int32& OutRes);

	/**
	 * Internal helper to read Weightmap textures.
	 * Extracts channel usage for the first 4 layers (RGBA).
	 */
	static bool ExtractWeightmapData(
		ULandscapeComponent* Comp,
		TArray<FColor>& OutData,
		int32& OutRes,
		const TArray<FName>* CanonicalLayerNames = nullptr);

	static bool HasVisibilityHoles(ULandscapeComponent* Comp);

	static UTerraDyneTileData* BakeComponentInternal(
		ULandscapeComponent* Component,
		FString DestinationPath,
		FString AssetNameOverride,
		const TArray<FName>* CanonicalLayerNames);
};
