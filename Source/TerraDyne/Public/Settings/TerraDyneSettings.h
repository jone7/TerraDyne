// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "TerraDyneSettings.generated.h"

/**
 * Global configuration for the TerraDyne plugin.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "TerraDyne Settings"))
class TERRADYNE_API UTerraDyneSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTerraDyneSettings();

	//--- Asset Paths ---//
	UPROPERTY(Config, EditAnywhere, Category = "Asset Paths",
	    meta = (AllowedClasses = "/Script/Engine.MaterialInterface", ToolTip = "Primary terrain material applied to chunk meshes. Must be a standard PBR material (not landscape/VHFM)."))
	FSoftObjectPath MasterMaterialPath;

	UPROPERTY(Config, EditAnywhere, Category = "Asset Paths",
	    meta = (AllowedClasses = "/Script/Engine.MaterialInterface", ToolTip = "Material used by the GPU brush compute pass for height sculpting."))
	FSoftObjectPath HeightBrushMaterialPath;

	UPROPERTY(Config, EditAnywhere, Category = "Asset Paths",
	    meta = (AllowedClasses = "/Script/UMG.WidgetBlueprint", ToolTip = "Widget Blueprint class for the terrain editing HUD. Leave empty to use the default built-in UI."))
	FSoftObjectPath HUDWidgetPath;

	//--- Defaults ---//
	UPROPERTY(Config, EditAnywhere, Category = "Defaults",
	    meta = (ClampMin = "100.0", ToolTip = "World-space size of each chunk in Unreal Units. Larger = fewer chunks but less granular streaming."))
	float DefaultChunkSize;

	UPROPERTY(Config, EditAnywhere, Category = "Defaults",
	    meta = (ClampMin = "32", ClampMax = "1024", ToolTip = "Vertex resolution per chunk edge. Higher = more detail but more memory. 64 is a good starting point."))
	int32 DefaultResolution;

	//--- Performance ---//
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ToolTip = "Distance in World Units at which chunks disable complex collision."))
	float LODDistanceThreshold;

	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ToolTip = "Time in seconds to wait after an edit before rebuilding physics."))
	float CollisionDebounceTime;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
	    meta = (ToolTip = "Seconds to wait after a sculpt/paint edit before regenerating grass.", ClampMin = "0.05"))
	float GrassDebounceTime;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
	    meta = (ToolTip = "Maximum number of chunk mesh surface applies allowed back onto the game thread each frame.", ClampMin = "1", ClampMax = "16"))
	int32 MaxMeshBuildAppliesPerFrame;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
	    meta = (ToolTip = "Maximum number of collision rebuilds allowed on the game thread each frame.", ClampMin = "1", ClampMax = "8"))
	int32 MaxCollisionUpdatesPerFrame;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
		meta = (ToolTip = "Enable GPU brush resources on render-capable clients. Dedicated servers always use the CPU path."))
	bool bEnableGPUBrushes;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
		meta = (ToolTip = "Seconds to coalesce overlapping navigation dirty areas before submitting them.", ClampMin = "0.0", ClampMax = "10.0"))
	float NavigationDirtyDebounceTime;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
		meta = (ToolTip = "Maximum separate navigation dirty regions retained before an early flush.", ClampMin = "1", ClampMax = "128"))
	int32 MaxPendingNavigationDirtyAreas;

	//--- Streaming ---//
	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Radius in chunk units within which chunks are loaded (diamond shape).", ClampMin = "1", ClampMax = "20"))
	int32 ChunkLoadRadius;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Radius in chunk units beyond which chunks are unloaded. Must be > LoadRadius for hysteresis.", ClampMin = "2", ClampMax = "25"))
	int32 ChunkUnloadRadius;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Max chunk spawn or teardown operations per streaming tick.", ClampMin = "1", ClampMax = "8"))
	int32 MaxChunkOpsPerTick;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Half-width of the world grid. Grid spans -N..+N = (2N+1)x(2N+1) chunks.", ClampMin = "1", ClampMax = "50"))
	int32 GridExtent;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Subdirectory under SaveGames/ for per-chunk cache files."))
	FString ChunkSaveDir;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
		meta = (ToolTip = "Compress and atomically write dirty chunk cache entries on a worker thread."))
	bool bUseAsyncChunkCacheWrites;

	//--- Undo/Redo ---//
	UPROPERTY(Config, EditAnywhere, Category = "Undo/Redo",
	    meta = (ToolTip = "Maximum number of undo entries stored per player.", ClampMin = "1", ClampMax = "100"))
	int32 MaxUndoHistory;

	//--- Multiplayer ---//
	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
	    meta = (ToolTip = "Max brush RPCs accepted per player per second (server-side rate limit).", ClampMin = "1.0", ClampMax = "120.0"))
	float MaxBrushRPCsPerSecond;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
	    meta = (ToolTip = "Maximum brush radius the server will accept.", ClampMin = "100.0"))
	float MaxBrushRadius;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
	    meta = (ToolTip = "Maximum brush strength the server will accept.", ClampMin = "0.0"))
	float MaxBrushStrength;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
		meta = (ToolTip = "Maximum distance from the controlled pawn/view target to a client-requested edit. Zero disables the distance check.", ClampMin = "0.0"))
	float MaxBrushDistanceFromOwner;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
		meta = (ToolTip = "Maximum number of active chunks one client brush request may touch.", ClampMin = "1", ClampMax = "256"))
	int32 MaxAffectedChunksPerBrush;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
		meta = (ToolTip = "Maximum flatten target delta from the current terrain height.", ClampMin = "0.0"))
	float MaxFlattenHeightDelta;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|Relevancy",
		meta = (ToolTip = "When true the replicated manager is globally relevant. Disable this when a project supplies its own Replication Graph policy."))
	bool bManagerAlwaysRelevant;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|Relevancy",
		meta = (ToolTip = "Maximum distance from a connection's pawn/view target for brush and chunk-state delivery. Zero sends to all TerraDyne replication components.", ClampMin = "0.0"))
	float TerrainReplicationRadius;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Maximum payload bytes in one reliable state fragment. Keep below the engine's constructed-bunch limit.", ClampMin = "1024", ClampMax = "49152"))
	int32 StateFragmentSizeBytes;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Maximum compressed bytes accepted for one chunk-state packet.", ClampMin = "65536", ClampMax = "67108864"))
	int32 MaxCompressedChunkStateBytes;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Maximum uncompressed bytes accepted for one chunk state.", ClampMin = "65536", ClampMax = "134217728"))
	int32 MaxUncompressedChunkStateBytes;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Maximum number of chunk-state transfers reassembled concurrently per client.", ClampMin = "1", ClampMax = "64"))
	int32 MaxPendingStateTransfers;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Discard an incomplete chunk-state transfer after this many seconds.", ClampMin = "1.0", ClampMax = "120.0"))
	float StateTransferTimeoutSeconds;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Maximum chunks sent by an automatic late-join synchronization.", ClampMin = "1", ClampMax = "4096"))
	int32 MaxFullSyncChunksPerConnection;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Maximum client chunk-state requests accepted per second.", ClampMin = "1.0", ClampMax = "120.0"))
	float MaxChunkStateRequestsPerSecond;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Maximum reliable state fragments emitted by one replication component per tick.", ClampMin = "1", ClampMax = "64"))
	int32 MaxStateFragmentsPerTick;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer|State Transfer",
		meta = (ToolTip = "Maximum encoded chunk-state bytes queued per connection.", ClampMin = "1048576", ClampMax = "134217728"))
	int32 MaxQueuedStateBytesPerConnection;

	//--- Collision ---//
	UPROPERTY(Config, EditAnywhere, Category = "Collision",
		meta = (ToolTip = "Collision profile applied to generated chunk meshes."))
	FName ChunkCollisionProfileName;

	UPROPERTY(Config, EditAnywhere, Category = "Collision",
		meta = (ToolTip = "Collision object channel applied after the chunk collision profile."))
	TEnumAsByte<ECollisionChannel> ChunkCollisionObjectType;

	//--- Notifications ---//
	UPROPERTY(Config, EditAnywhere, Category = "Notifications",
	    meta = (ToolTip = "When true, runtime error toasts are displayed on screen. Editor toasts always show."))
	bool bShowRuntimeNotifications = true;
};
