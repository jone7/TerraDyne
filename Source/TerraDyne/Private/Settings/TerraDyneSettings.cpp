// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Settings/TerraDyneSettings.h"

UTerraDyneSettings::UTerraDyneSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("TerraDyne");

	// Default Paths
	MasterMaterialPath = FSoftObjectPath(TEXT("/TerraDyne/Materials/VHFM/M_TerraDyne_Master.M_TerraDyne_Master"));
	HeightBrushMaterialPath = FSoftObjectPath(TEXT("/TerraDyne/Materials/Tools/M_HeightBrush.M_HeightBrush"));
	HUDWidgetPath = FSoftObjectPath();

	// Default Values
	DefaultChunkSize = 10000.0f;
	DefaultResolution = 128;

	// Performance Defaults
	LODDistanceThreshold = 50000.0f; // 500m
	CollisionDebounceTime = 0.2f;
	GrassDebounceTime = 0.5f;
	MaxMeshBuildAppliesPerFrame = 2;
	MaxCollisionUpdatesPerFrame = 1;
	NavigationDirtyDebounceTime = 0.5f;
	MaxPendingNavigationDirtyAreas = 32;

	// Streaming Defaults
	ChunkLoadRadius = 5;
	ChunkUnloadRadius = 7;
	MaxChunkOpsPerTick = 2;
	GridExtent = 10;
	ChunkSaveDir = TEXT("TerraDyne/ChunkCache");
	bUseAsyncChunkCacheWrites = true;

	// Undo/Redo Defaults
	MaxUndoHistory = 20;

	// Multiplayer Defaults
	MaxBrushRPCsPerSecond = 30.0f;
	MaxBrushRadius = 10000.0f;
	MaxBrushStrength = 5000.0f;
	MaxBrushDistanceFromOwner = 25000.0f;
	MaxAffectedChunksPerBrush = 16;
	MaxFlattenHeightDelta = 10000.0f;
	bManagerAlwaysRelevant = false;
	TerrainReplicationRadius = 150000.0f;
	StateFragmentSizeBytes = 48 * 1024;
	MaxCompressedChunkStateBytes = 8 * 1024 * 1024;
	MaxUncompressedChunkStateBytes = 64 * 1024 * 1024;
	MaxPendingStateTransfers = 8;
	StateTransferTimeoutSeconds = 15.0f;
	MaxFullSyncChunksPerConnection = 128;
	MaxChunkStateRequestsPerSecond = 16.0f;
	MaxStateFragmentsPerTick = 4;
	MaxQueuedStateBytesPerConnection = 32 * 1024 * 1024;

	// Collision defaults preserve existing behavior while allowing projects to use a dedicated terrain channel.
	ChunkCollisionProfileName = TEXT("BlockAll");
	ChunkCollisionObjectType = ECC_WorldStatic;
}
