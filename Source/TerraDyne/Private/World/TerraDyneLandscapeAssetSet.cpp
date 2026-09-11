// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneLandscapeAssetSet.h"

#include "World/TerraDyneTileData.h"

TArray<FIntPoint> UTerraDyneLandscapeAssetSet::GetAuthoredChunkCoordinates() const
{
	TArray<FIntPoint> Coords;
	Coords.Reserve(Tiles.Num());

	for (const UTerraDyneTileData* Tile : Tiles)
	{
		if (Tile)
		{
			Coords.Add(Tile->GridCoordinate);
		}
	}

	return Coords;
}

const UTerraDyneTileData* UTerraDyneLandscapeAssetSet::FindTileForCoordinate(FIntPoint Coord) const
{
	for (const UTerraDyneTileData* Tile : Tiles)
	{
		if (Tile && Tile->GridCoordinate == Coord)
		{
			return Tile;
		}
	}

	return nullptr;
}
