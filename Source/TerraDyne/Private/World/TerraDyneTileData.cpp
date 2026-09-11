// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneTileData.h"

UTerraDyneTileData::UTerraDyneTileData()
{
	Resolution = 128;
	RealWorldSize = 10000.0f; // Default 100m
	BakedZScale = 100.0f;
	GridCoordinate = FIntPoint(0, 0);
}

int32 UTerraDyneTileData::GetMemoryFootprint() const
{
	int32 HeightBytes = InitialHeightMap.Num() * sizeof(uint16);
	int32 WeightBytes = InitialWeightMap.Num() * sizeof(FColor);
	return HeightBytes + WeightBytes;
}

FTerraDyneChunkData UTerraDyneTileData::BuildChunkData() const
{
	FTerraDyneChunkData Data;
	Data.Coordinate = GridCoordinate;
	Data.Resolution = Resolution;
	Data.ZScale = BakedZScale;

	const int32 NumSamples = Resolution * Resolution;
	Data.HeightData.SetNumZeroed(NumSamples);
	Data.BaseData.SetNumZeroed(NumSamples);
	Data.SculptData.SetNumZeroed(NumSamples);
	Data.DetailData.SetNumZeroed(NumSamples);

	for (int32 Index = 0; Index < NumSamples && Index < InitialHeightMap.Num(); ++Index)
	{
		const float NormalizedHeight = static_cast<float>(InitialHeightMap[Index]) / 65535.0f;
		Data.HeightData[Index] = NormalizedHeight;
		Data.BaseData[Index] = NormalizedHeight;
	}

	Data.WeightData.SetNumZeroed(NumSamples * 4);
	for (int32 Index = 0; Index < NumSamples && Index < InitialWeightMap.Num(); ++Index)
	{
		const FColor& Sample = InitialWeightMap[Index];
		const int32 WriteIndex = Index * 4;
		Data.WeightData[WriteIndex + 0] = Sample.R;
		Data.WeightData[WriteIndex + 1] = Sample.G;
		Data.WeightData[WriteIndex + 2] = Sample.B;
		Data.WeightData[WriteIndex + 3] = Sample.A;
	}

	Data.bTransferredFoliageFollowsTerrain = bTransferredFoliageFollowsTerrain;
	Data.FoliageStaticMeshPaths = FoliageStaticMeshPaths;
	Data.FoliageMaterialCounts = FoliageMaterialCounts;
	Data.FoliageOverrideMaterialPaths = FoliageOverrideMaterialPaths;
	Data.FoliageDefinitionIndices = FoliageDefinitionIndices;
	Data.FoliageInstanceLocalTransforms = FoliageInstanceLocalTransforms;
	Data.FoliageInstanceTerrainOffsets = FoliageInstanceTerrainOffsets;
	Data.ActorFoliageClassPaths = ActorFoliageClassPaths;
	Data.ActorFoliageAttachFlags = ActorFoliageAttachFlags;
	Data.ActorFoliageDefinitionIndices = ActorFoliageDefinitionIndices;
	Data.ActorFoliageInstanceLocalTransforms = ActorFoliageInstanceLocalTransforms;
	Data.ActorFoliageInstanceTerrainOffsets = ActorFoliageInstanceTerrainOffsets;
	return Data;
}
