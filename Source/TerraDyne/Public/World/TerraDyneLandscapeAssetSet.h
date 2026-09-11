// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/TerraDyneSaveGame.h"
#include "TerraDyneLandscapeAssetSet.generated.h"

class UMaterialInterface;
class UTerraDyneTileData;

/**
 * Runtime-safe authored-world payload generated from a Landscape conversion.
 * Packaged builds can initialize TerraDyne from this asset set without any editor-only APIs.
 */
UCLASS(BlueprintType)
class TERRADYNE_API UTerraDyneLandscapeAssetSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Captured metadata describing the original Landscape conversion. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FTerraDyneLandscapeMigrationState MigrationState;

	/** Optional master material adopted from the source Landscape and safe for TerraDyne runtime meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TObjectPtr<UMaterialInterface> AdoptedMasterMaterial = nullptr;

	/** All authored chunk tiles that belong to this converted Landscape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<TObjectPtr<UTerraDyneTileData>> Tiles;

	UFUNCTION(BlueprintPure, Category = "TerraDyne")
	int32 GetTileCount() const { return Tiles.Num(); }

	UFUNCTION(BlueprintPure, Category = "TerraDyne")
	TArray<FIntPoint> GetAuthoredChunkCoordinates() const;

	const UTerraDyneTileData* FindTileForCoordinate(FIntPoint Coord) const;
};
