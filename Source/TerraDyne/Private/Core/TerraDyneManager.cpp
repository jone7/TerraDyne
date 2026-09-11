// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "TerraDyneModule.h"
#include "Core/TerraDyneSaveGame.h"
#include "Core/TerraDyneWorldPreset.h"
#include "Settings/TerraDyneSettings.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneLandscapeAssetSet.h"
#include "World/TerraDyneOrchestrator.h"
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneReplicationComponent.h"
#include "IO/TerraDyneAsyncSaver.h"
#include "IO/TerraDyneSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/App.h"
#include "Engine/StaticMeshActor.h"
#include "LandscapeLayerInfoObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#if WITH_EDITOR
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "LandscapeDataAccess.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionLandscapeLayerSwitch.h"
#include "Materials/MaterialExpressionLandscapeLayerWeight.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "InstancedFoliageActor.h"
#include "FoliageType_Actor.h"
#include "FoliageType_InstancedStaticMesh.h"
#endif

namespace
{
	static void PreparePopulationActorForRuntimePlacement(AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}

		TArray<USceneComponent*> SceneComponents;
		Actor->GetComponents<USceneComponent>(SceneComponents);
		for (USceneComponent* SceneComponent : SceneComponents)
		{
			if (SceneComponent && SceneComponent->Mobility != EComponentMobility::Movable)
			{
				SceneComponent->SetMobility(EComponentMobility::Movable);
			}
		}
	}
}

namespace TerraDyneWorldFramework
{
	static FBox MakeChunkBounds(const ATerraDyneManager& Manager, const FIntPoint Coord, float ChunkSize, float ZExtent = 4000.0f)
	{
		const FVector Center = Manager.GetActorLocation() + FVector(Coord.X * ChunkSize, Coord.Y * ChunkSize, 0.0f);
		return FBox::BuildAABB(Center, FVector(ChunkSize * 0.5f, ChunkSize * 0.5f, ZExtent));
	}

	static FTransform MakeWorldTransform(const FTransform& LocalTransform, const FTransform& ParentTransform)
	{
		return LocalTransform * ParentTransform;
	}

	static int32 ChunkIndexFromLocalAxis(float LocalAxis, float ChunkSize)
	{
		if (ChunkSize <= KINDA_SMALL_NUMBER)
		{
			return 0;
		}

		return FMath::FloorToInt((LocalAxis + (ChunkSize * 0.5f)) / ChunkSize);
	}

#if WITH_EDITOR
	static bool UsesLandscapeOnlyMaterialExpressions(const UMaterialInterface* MaterialInterface)
	{
		const UMaterial* Material = MaterialInterface ? MaterialInterface->GetMaterial() : nullptr;
		if (!Material)
		{
			return false;
		}

		return Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeGrassOutput>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerBlend>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerCoords>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerSample>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerSwitch>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeLayerWeight>() ||
			Material->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeVisibilityMask>();
	}

	static void HideLandscapeHierarchy(ALandscapeProxy* SourceLandscape)
	{
		if (!SourceLandscape)
		{
			return;
		}

		TSet<ALandscapeProxy*> LandscapesToHide;
		LandscapesToHide.Add(SourceLandscape);

		if (ULandscapeInfo* LandscapeInfo = SourceLandscape->GetLandscapeInfo())
		{
			LandscapeInfo->ForEachLandscapeProxy([&LandscapesToHide](ALandscapeProxy* Proxy)
			{
				if (Proxy)
				{
					LandscapesToHide.Add(Proxy);
				}

				return true;
			});
		}

		for (ALandscapeProxy* LandscapeProxy : LandscapesToHide)
		{
			if (!LandscapeProxy)
			{
				continue;
			}

			LandscapeProxy->SetActorHiddenInGame(true);
			LandscapeProxy->SetIsTemporarilyHiddenInEditor(true);
		}
	}
#endif
}

ATerraDyneManager::ATerraDyneManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	bAlwaysRelevant = Settings ? Settings->bManagerAlwaysRelevant : false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	GlobalChunkSize = 10000.0f;
	ChunkClass = ATerraDyneChunk::StaticClass();
	bAutoImportAtRuntime = false;
	bSpawnDefaultChunksOnBeginPlay = false;
	bSetupDefaultLightingOnBeginPlay = false;
	bSpawnShowcaseOnBeginPlay = false;
	PopulationMaintenanceTimer = 0.25f;
}

void ATerraDyneManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATerraDyneManager, GlobalChunkSize);
	DOREPLIFETIME(ATerraDyneManager, ChunkClass);
	DOREPLIFETIME(ATerraDyneManager, ActiveLayer);
	DOREPLIFETIME(ATerraDyneManager, MasterMaterial);
	DOREPLIFETIME(ATerraDyneManager, ActiveGrassProfile);
	DOREPLIFETIME(ATerraDyneManager, LandscapeMigrationState);
	DOREPLIFETIME(ATerraDyneManager, ProceduralWorldSettings);
	DOREPLIFETIME(ATerraDyneManager, BiomeOverlays);
	DOREPLIFETIME(ATerraDyneManager, AISpawnZones);
	DOREPLIFETIME(ATerraDyneManager, BuildPermissionZones);
}

void ATerraDyneManager::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyneManager: Starting initialization..."));

	// Register with subsystem
	if (UTerraDyneSubsystem* Subsystem = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
	{
		Subsystem->RegisterManager(this);
	}

	if (WorldPreset)
	{
		ApplyWorldPreset(WorldPreset);
	}

	// Load materials (needed on all roles for rendering)
	LoadMaterials();

	// Capture any pre-placed chunks before bootstrap logic runs.
	RebuildChunkMap();
	if (LandscapeMigrationState.bWasImportedFromLandscape && ImportedChunkCoords.Num() == 0)
	{
		for (const auto& Pair : ActiveChunkMap)
		{
			ImportedChunkCoords.Add(Pair.Key);
		}
	}
	ApplyMaterialToActiveChunks();
	ApplyGrassProfileToActiveChunks(false);
	RebuildWorldStateIndices();
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			EnsureChunkFrameworkState(Pair.Key, Pair.Value);
		}
	}

	// Only the server spawns world actors — clients receive them via replication
	if (HasAuthority())
	{
		bool bChunkMapDirty = false;
#if WITH_EDITOR
		if (bAutoImportAtRuntime && TargetLandscapeSource)
		{
			ImportFromLandscapeWithOptions(TargetLandscapeSource, LandscapeMigrationOptions);
			bChunkMapDirty = true;
		}
		else
#endif
		if (bSpawnDefaultChunksOnBeginPlay && ActiveChunkMap.Num() == 0)
		{
			SpawnDefaultSandboxChunk();
			bChunkMapDirty = true;
		}

		if (bSetupDefaultLightingOnBeginPlay)
		{
			SetupLighting();
		}

		// Showcase spawning is now exclusively handled by ATerraDyneSceneSetup.
		// bSpawnShowcaseOnBeginPlay is deprecated and safely ignored.
		/*
		if (bSpawnShowcaseOnBeginPlay)
		{
			TArray<AActor*> ExistingOrch;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneOrchestrator::StaticClass(), ExistingOrch);
			if (ExistingOrch.Num() == 0)
			{
				GetWorld()->SpawnActor<ATerraDyneOrchestrator>(ATerraDyneOrchestrator::StaticClass(), FVector(0, 0, 5000), FRotator::ZeroRotator);
			}
		}
		*/

		if (bChunkMapDirty)
		{
			RebuildChunkMap();
			ApplyMaterialToActiveChunks();
			ApplyGrassProfileToActiveChunks(LandscapeMigrationState.bRegenerateGrassFromImportedLayers);
		}

		RefreshPopulationForLoadedChunks();
	}

	// Debug: List all chunks
	UE_LOG(LogTerraDyne, Log, TEXT("Active chunks: %d"), ActiveChunkMap.Num());
	
	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyneManager: Initialization complete"));
}

void ATerraDyneManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority())
	{
		PopulationMaintenanceTimer -= DeltaTime;
		if (PopulationMaintenanceTimer <= 0.0f)
		{
			TickPopulationState(0.25f);
			PopulationMaintenanceTimer = 0.25f;
		}

		const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		for (auto It = ChunkCacheRetryAfterSeconds.CreateIterator(); It; ++It)
		{
			if (It.Value() <= NowSeconds)
			{
				const FIntPoint Coord = It.Key();
				It.RemoveCurrent();
				if (const FTerraDyneChunkData* PendingData = PendingChunkCacheWrites.Find(Coord))
				{
					SaveChunkToCache(Coord, *PendingData);
				}
				break;
			}
		}

		if (PendingNavigationDirtyAreas.Num() > 0)
		{
			NavigationDirtyTimer -= DeltaTime;
			if (NavigationDirtyTimer <= 0.0f)
			{
				FlushPendingNavigationDirtyAreas();
			}
		}
	}

	// Periodic update (every ~0.25s)
	LODTimer -= DeltaTime;

	if (LODTimer <= 0.0f)
	{
		LODTimer = 0.25f;

		// Gather all player positions
		TArray<FVector> PlayerPositions;
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				if (!PC) continue;

				FVector Pos = FVector::ZeroVector;
				if (APawn* Pawn = PC->GetPawn())
				{
					Pos = Pawn->GetActorLocation();
				}
				else if (PC->GetViewTarget())
				{
					Pos = PC->GetViewTarget()->GetActorLocation();
				}
				PlayerPositions.Add(Pos);
			}
		}

		if (PlayerPositions.Num() == 0)
		{
			PlayerPositions.Add(FVector::ZeroVector);
		}

		// Streaming (server only) — uses union of all player positions
		if (HasAuthority())
		{
			UpdateStreaming(PlayerPositions);
		}

		// LOD (all roles) — each chunk uses nearest player
		// TODO: Time-slice LOD updates — process a fixed budget of ~50 chunks per frame
		// instead of iterating the entire ActiveChunkMap to avoid periodic hitches.
		if (ActiveChunkMap.Num() > 0)
		{
			TArray<ATerraDyneChunk*> Chunks;
			ActiveChunkMap.GenerateValueArray(Chunks);
			int32 ProcessedCount = 0;
			const int32 Budget = 64;
			
			while (ProcessedCount < Budget)
			{
				if (LODUpdateCursor >= Chunks.Num())
				{
					LODUpdateCursor = 0;
					break;
				}
				
				ATerraDyneChunk* Chunk = Chunks[LODUpdateCursor];
				if (Chunk)
				{
					float MinDistSq = MAX_FLT;
					FVector NearestPos = PlayerPositions[0];
					for (const FVector& Pos : PlayerPositions)
					{
						float DistSq = FVector::DistSquared(Pos, Chunk->GetActorLocation());
						if (DistSq < MinDistSq)
						{
							MinDistSq = DistSq;
							NearestPos = Pos;
						}
					}
					Chunk->UpdateLOD(NearestPos);
				}
				
				LODUpdateCursor++;
				ProcessedCount++;
			}
		}
	}

#if !UE_BUILD_SHIPPING
	if (bShowDebugOverlay)
	{
		const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
		const float ChunkSize = Settings ? Settings->DefaultChunkSize : 10000.0f;

		for (const auto& Pair : ActiveChunkMap)
		{
			const FIntPoint& Coord = Pair.Key;
			ATerraDyneChunk* Chunk = Pair.Value;
			if (!Chunk) continue;

			FVector Center = Chunk->GetActorLocation();
			FVector Extent(ChunkSize * 0.5f, ChunkSize * 0.5f, 5000.0f);

			// Color by state
			FColor BoxColor = FColor::Green; // Loaded, idle
			if (PendingLoadQueue.Contains(Coord))
			{
				BoxColor = FColor::Yellow;
			}
			else if (PendingUnloadQueue.Contains(Coord))
			{
				BoxColor = FColor::Red;
			}
			else if (ImportedChunkCoords.Contains(Coord))
			{
				BoxColor = FColor::Cyan;
			}

			DrawDebugBox(GetWorld(), Center, Extent, BoxColor, false, -1.0f, 0, 3.0f);

			// Label
			FString Label = FString::Printf(TEXT("[%d,%d] Verts:%d"), Coord.X, Coord.Y, Chunk->HeightBuffer.Num());
			DrawDebugString(GetWorld(), Center + FVector(0, 0, 5000.0f), Label, nullptr, BoxColor, -1.0f, true);
		}
	}
#endif
}

void ATerraDyneManager::LoadMaterials()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TerraDyne_LoadMaterials);
	// TODO: Consider async loading (StreamableManager) to avoid blocking the game thread.

	if (bMaterialsLoaded) return;
	if (!FApp::CanEverRender() || IsRunningDedicatedServer())
	{
		bMaterialsLoaded = true;
		return;
	}

	UE_LOG(LogTerraDyne, Log, TEXT("Loading TerraDyne materials..."));

	// [N-2] Guard: CDO lookup can return nullptr during hot-reload edge cases
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	if (!Settings)
	{
		UE_LOG(LogTerraDyne, Error, TEXT("TerraDyneSettings CDO not found"));
		return;
	}

	// 1. Try explicit settings path first.
	if (!MasterMaterial && Settings->MasterMaterialPath.IsValid())
	{
		MasterMaterial = Cast<UMaterialInterface>(Settings->MasterMaterialPath.TryLoad());
	}

	// Final fallback
	if (MasterMaterial)
	{
		UE_LOG(LogTerraDyne, Log, TEXT("MasterMaterial loaded: %s"), *MasterMaterial->GetName());
	}
	else
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("Failed to load MasterMaterial, using Engine Basic Shape"));
		if (UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
		{
			Sys->ShowNotification(
				FText::FromString(TEXT("TerraDyne: Master material not found, using fallback.")),
				ETerraDyneNotifySeverity::Warning);
		}
		MasterMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	
	// Height Brush
	if (!HeightBrushMaterial && Settings->HeightBrushMaterialPath.IsValid())
	{
		HeightBrushMaterial = Cast<UMaterialInterface>(Settings->HeightBrushMaterialPath.TryLoad());
	}

	bMaterialsLoaded = true;
}

void ATerraDyneManager::SetupLighting()
{
	if (!FApp::CanEverRender() || IsRunningDedicatedServer()) return;

	// [N-4] Early-return if a DirectionalLight already exists — prevents duplicates on re-entry
	TArray<AActor*> Lights;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADirectionalLight::StaticClass(), Lights);
	if (Lights.Num() > 0) return;

	ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), 
		FVector(0, 0, 5000), 
		FRotator(-45, -45, 0));
	
	if (Sun && Sun->GetLightComponent())
	{
		Sun->GetLightComponent()->SetIntensity(10.0f);
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	}

	ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>(
		ASkyLight::StaticClass(), 
		FVector::ZeroVector, 
		FRotator::ZeroRotator);
	
	if (Sky && Sky->GetLightComponent())
	{
		Sky->GetLightComponent()->SetIntensity(1.0f);
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sky->GetLightComponent()->bRealTimeCapture = true;
	}
}

void ATerraDyneManager::SpawnDefaultSandboxChunk()
{
	UE_LOG(LogTerraDyne, Log, TEXT("Spawning 3x3 chunk grid..."));
	
	for (int32 X = -1; X <= 1; X++)
	{
		for (int32 Y = -1; Y <= 1; Y++)
		{
			FIntPoint GridCoord(X, Y);

			ATerraDyneChunk* Chunk = SpawnConfiguredChunk(GridCoord);
			if (Chunk)
			{
				// Debug box to show chunk bounds
				DrawDebugBox(GetWorld(), Chunk->GetActorLocation(), FVector(GlobalChunkSize/2, GlobalChunkSize/2, 1000), 
					FColor::Green, true, 10.0f, 0, 10.0f);
				
				UE_LOG(LogTerraDyne, Log, TEXT("Spawned chunk [%d,%d] at %s"), X, Y, *Chunk->GetActorLocation().ToString());
			}
			else
			{
				UE_LOG(LogTerraDyne, Error, TEXT("FAILED to spawn chunk [%d,%d]"), X, Y);
				if (UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
				{
					Sys->ShowNotification(
						FText::Format(NSLOCTEXT("TerraDyne", "SpawnFail", "Failed to spawn chunk [{0},{1}]"),
							FText::AsNumber(X), FText::AsNumber(Y)),
						ETerraDyneNotifySeverity::Error);
				}
			}
		}
	}
}

void ATerraDyneManager::ApplyGlobalBrush(FVector WorldLocation, float Radius, float Strength,
	ETerraDyneBrushMode BrushMode, ETerraDyneLayer TargetLayer, int32 WeightLayerIndex, float FlattenHeight)
{
	UE_LOG(LogTerraDyne, Verbose, TEXT("ApplyGlobalBrush: Loc=%s R=%.0f S=%.0f Mode=%d"),
		*WorldLocation.ToString(), Radius, Strength, (int32)BrushMode);

	TArray<FIntPoint> ChangedCoords;
	const float RadiusWorld = Radius + GlobalChunkSize * 0.5f;
	const float ProbeInset = 0.01f;
	const FIntPoint MinCoord = WorldToChunkCoord(WorldLocation - FVector(RadiusWorld, RadiusWorld, 0.0f));
	const FIntPoint MaxCoord = WorldToChunkCoord(
		WorldLocation + FVector(RadiusWorld - ProbeInset, RadiusWorld - ProbeInset, 0.0f));

	for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
	{
		for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
		{
			FIntPoint Coord(X, Y);
			if (ATerraDyneChunk** FoundChunk = ActiveChunkMap.Find(Coord))
			{
				if (ATerraDyneChunk* Chunk = *FoundChunk)
				{
					FVector RelativePos = WorldLocation - Chunk->GetActorLocation();
					Chunk->ApplyLocalIdempotentEdit(RelativePos, Radius, Strength, BrushMode, TargetLayer, WeightLayerIndex, FlattenHeight);
					MarkChunkDirty(Coord);
					ChangedCoords.AddUnique(Coord);
				}
			}
		}
	}

	if (ChangedCoords.Num() > 0)
	{
		if (BrushMode != ETerraDyneBrushMode::Paint)
		{
			BroadcastTerrainChanged(ChangedCoords);
		}
		BroadcastFoliageChanged(ChangedCoords);
		if (HasAuthority())
		{
			for (const FIntPoint& Coord : ChangedCoords)
			{
				SyncPopulationForChunk(Coord);
			}
		}
	}
}

ATerraDyneChunk* ATerraDyneManager::GetChunkAtLocation(FVector WorldLocation) const
{
	if (ATerraDyneChunk* const* Chunk = ActiveChunkMap.Find(WorldToChunkCoord(WorldLocation))) return *Chunk;
	return nullptr;
}

float ATerraDyneManager::GetWorldHeightAtLocation(FVector WorldLocation) const
{
	if (ATerraDyneChunk* Chunk = GetChunkAtLocation(WorldLocation))
	{
		FVector LocalPos = WorldLocation - Chunk->GetActorLocation();
		return Chunk->GetHeightAtLocation(LocalPos) + Chunk->GetActorLocation().Z;
	}
	return WorldLocation.Z; // Fallback
}

float ATerraDyneManager::GetWorldWeightAtLocation(FVector WorldLocation, int32 LayerIndex) const
{
	if (ATerraDyneChunk* Chunk = GetChunkAtLocation(WorldLocation))
	{
		FVector LocalPos = WorldLocation - Chunk->GetActorLocation();
		return Chunk->GetWeightAtLocation(LocalPos, LayerIndex);
	}
	return 0.f; // Fallback
}

TArray<ATerraDyneChunk*> ATerraDyneManager::GetChunksInRadius(FVector WorldLocation, float Radius)
{
	TArray<ATerraDyneChunk*> Result;
	const float RadiusWorld = Radius + GlobalChunkSize * 0.5f;
	const float ProbeInset = 0.01f;
	const FIntPoint MinCoord = WorldToChunkCoord(WorldLocation - FVector(RadiusWorld, RadiusWorld, 0.0f));
	const FIntPoint MaxCoord = WorldToChunkCoord(
		WorldLocation + FVector(RadiusWorld - ProbeInset, RadiusWorld - ProbeInset, 0.0f));
	for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
	{
		for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
		{
			if (ATerraDyneChunk** Found = ActiveChunkMap.Find(FIntPoint(X, Y)))
			{
				if (*Found) Result.Add(*Found);
			}
		}
	}
	return Result;
}

ATerraDyneChunk* ATerraDyneManager::GetChunkAtCoord(FIntPoint Coord) const
{
	ATerraDyneChunk* const* Found = ActiveChunkMap.Find(Coord);
	return Found ? *Found : nullptr;
}

void ATerraDyneManager::RegisterChunk(ATerraDyneChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	ActiveChunkMap.Add(Chunk->GridCoordinate, Chunk);
	if (HeightBrushMaterial)
	{
		Chunk->BrushMaterialBase = HeightBrushMaterial;
	}
	Chunk->SetMaterial(MasterMaterial);
	Chunk->SetGrassProfile(ActiveGrassProfile);
	Chunk->SetTransferredFoliageFollowsTerrain(LandscapeMigrationState.bTransferredFoliageFollowsTerrain);
	EnsureChunkFrameworkState(Chunk->GridCoordinate, Chunk);
	if (HasAuthority())
	{
		SyncPopulationForChunk(Chunk->GridCoordinate);
	}
}

void ATerraDyneManager::UnregisterChunk(ATerraDyneChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	if (ATerraDyneChunk** Found = ActiveChunkMap.Find(Chunk->GridCoordinate))
	{
		if (*Found == Chunk)
		{
			ActiveChunkMap.Remove(Chunk->GridCoordinate);
		}
	}
}

void ATerraDyneManager::BeginStroke(FVector WorldLocation, float Radius, APlayerController* Controller)
{
	// TODO: Consider copy-on-write or async snapshot to avoid hitch on brush click.
	TRACE_CPUPROFILER_EVENT_SCOPE(TerraDyne_BeginStroke_Snapshot);
	// PERF: Full buffer copy - consider copy-on-write for large chunks

	if (!Controller)
	{
		return;
	}

	FTerraDyneUndoEntry Entry;
	for (ATerraDyneChunk* Chunk : GetChunksInRadius(WorldLocation, Radius))
	{
		FTerraDyneChunkSnapshot Snap;
		Snap.Coordinate = Chunk->GridCoordinate;
		Snap.BaseBuffer = Chunk->BaseBuffer;
		Snap.SculptBuffer = Chunk->SculptBuffer;
		Snap.DetailBuffer = Chunk->DetailBuffer;
		Snap.WeightBuffers.SetNum(ATerraDyneChunk::NumWeightLayers);
		for (int32 L = 0; L < ATerraDyneChunk::NumWeightLayers; L++)
		{
			Snap.WeightBuffers[L] = Chunk->WeightBuffers[L];
		}
		Entry.Snapshots.Add(Chunk->GridCoordinate, MoveTemp(Snap));
	}
	PendingStrokes.Add(Controller, MoveTemp(Entry));
}

void ATerraDyneManager::CommitStroke(APlayerController* Controller)
{
	if (!Controller)
	{
		return;
	}

	FTerraDyneUndoEntry PendingEntry;
	if (!PendingStrokes.RemoveAndCopyValue(Controller, PendingEntry))
	{
		return;
	}

	TArray<FTerraDyneUndoEntry>& Stack = UndoStacks.FindOrAdd(Controller);
	Stack.Add(MoveTemp(PendingEntry));

	int32 MaxHistory = 20;
	if (const UTerraDyneSettings* S = GetDefault<UTerraDyneSettings>())
	{
		MaxHistory = S->MaxUndoHistory;
	}
	if (Stack.Num() > MaxHistory)
		Stack.RemoveAt(0, Stack.Num() - MaxHistory);

	// New stroke clears redo for this player
	RedoStacks.Remove(Controller);
}

void ATerraDyneManager::RestoreSnapshots(const FTerraDyneUndoEntry& Entry)
{
	TArray<FIntPoint> RestoredCoords;
	for (const auto& Pair : Entry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		const FTerraDyneChunkSnapshot& Snap = Pair.Value;

		Chunk->BaseBuffer = Snap.BaseBuffer;
		Chunk->SculptBuffer = Snap.SculptBuffer;
		Chunk->DetailBuffer = Snap.DetailBuffer;
		for (int32 L = 0; L < ATerraDyneChunk::NumWeightLayers; L++)
		{
			if (Snap.WeightBuffers.IsValidIndex(L))
				Chunk->WeightBuffers[L] = Snap.WeightBuffers[L];
		}
		// Recompute combined HeightBuffer
		const int32 ExpectedSize = Chunk->HeightBuffer.Num();
		if (Chunk->BaseBuffer.Num() != ExpectedSize ||
			Chunk->SculptBuffer.Num() != ExpectedSize ||
			Chunk->DetailBuffer.Num() != ExpectedSize)
		{
			UE_LOG(LogTerraDyne, Warning,
				TEXT("RestoreSnapshots: buffer size mismatch on chunk %s — skipping HeightBuffer recompute"),
				*Snap.Coordinate.ToString());
		}
		else
		{
			for (int32 i = 0; i < ExpectedSize; i++)
			{
				Chunk->HeightBuffer[i] = FMath::Clamp(
					Chunk->BaseBuffer[i] + Chunk->SculptBuffer[i] + Chunk->DetailBuffer[i], 0.f, 1.f);
			}
		}

		const int32 MeshSamples = Chunk->Resolution * Chunk->Resolution;
		if (MeshSamples > 0 && Chunk->HeightBuffer.Num() == MeshSamples)
		{
			Chunk->RebuildPhysicsMesh();
			Chunk->UploadWeightTexture();
			Chunk->RequestTransferredFoliageRefresh();
		}
		else
		{
			UE_LOG(LogTerraDyne, Warning,
				TEXT("RestoreSnapshots: chunk %s has invalid terrain sample count (%d vs %d); skipping rebuild."),
				*Snap.Coordinate.ToString(), Chunk->HeightBuffer.Num(), MeshSamples);
		}

		RestoredCoords.AddUnique(Pair.Key);
	}

	if (RestoredCoords.Num() > 0)
	{
		BroadcastTerrainChanged(RestoredCoords);
		BroadcastFoliageChanged(RestoredCoords);
		if (HasAuthority())
		{
			for (const FIntPoint& Coord : RestoredCoords)
			{
				SyncPopulationForChunk(Coord);
			}
		}
	}
}

void ATerraDyneManager::Undo(APlayerController* Controller)
{
	TArray<FTerraDyneUndoEntry>* Stack = UndoStacks.Find(Controller);
	if (!Stack || Stack->Num() == 0) return;

	FTerraDyneUndoEntry UndoEntry = Stack->Pop();

	// Snapshot current state for redo
	FTerraDyneUndoEntry RedoEntry;
	for (const auto& Pair : UndoEntry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		FTerraDyneChunkSnapshot CurrentSnap;
		CurrentSnap.Coordinate = Chunk->GridCoordinate;
		CurrentSnap.BaseBuffer = Chunk->BaseBuffer;
		CurrentSnap.SculptBuffer = Chunk->SculptBuffer;
		CurrentSnap.DetailBuffer = Chunk->DetailBuffer;
		CurrentSnap.WeightBuffers.SetNum(ATerraDyneChunk::NumWeightLayers);
		for (int32 L = 0; L < ATerraDyneChunk::NumWeightLayers; L++)
			CurrentSnap.WeightBuffers[L] = Chunk->WeightBuffers[L];
		RedoEntry.Snapshots.Add(Pair.Key, MoveTemp(CurrentSnap));
	}
	RedoStacks.FindOrAdd(Controller).Add(MoveTemp(RedoEntry));

	RestoreSnapshots(UndoEntry);

	// Multicast affected chunks to remote clients
	for (const auto& Pair : UndoEntry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		FTerraDyneChunkData ChunkData = Chunk->GetSerializedData();
		Multicast_SyncChunkState(ChunkData);
	}
}

void ATerraDyneManager::Redo(APlayerController* Controller)
{
	TArray<FTerraDyneUndoEntry>* Stack = RedoStacks.Find(Controller);
	if (!Stack || Stack->Num() == 0) return;

	FTerraDyneUndoEntry RedoEntry = Stack->Pop();

	// Snapshot current state back to undo
	FTerraDyneUndoEntry UndoEntry;
	for (const auto& Pair : RedoEntry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		FTerraDyneChunkSnapshot CurrentSnap;
		CurrentSnap.Coordinate = Chunk->GridCoordinate;
		CurrentSnap.BaseBuffer = Chunk->BaseBuffer;
		CurrentSnap.SculptBuffer = Chunk->SculptBuffer;
		CurrentSnap.DetailBuffer = Chunk->DetailBuffer;
		CurrentSnap.WeightBuffers.SetNum(ATerraDyneChunk::NumWeightLayers);
		for (int32 L = 0; L < ATerraDyneChunk::NumWeightLayers; L++)
			CurrentSnap.WeightBuffers[L] = Chunk->WeightBuffers[L];
		UndoEntry.Snapshots.Add(Pair.Key, MoveTemp(CurrentSnap));
	}
	UndoStacks.FindOrAdd(Controller).Add(MoveTemp(UndoEntry));

	RestoreSnapshots(RedoEntry);

	// Multicast affected chunks to remote clients
	for (const auto& Pair : RedoEntry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		FTerraDyneChunkData ChunkData = Chunk->GetSerializedData();
		Multicast_SyncChunkState(ChunkData);
	}
}

void ATerraDyneManager::ApplyGlobalNoise(float Strength, float Frequency, float Seed)
{
	if (ActiveChunkMap.Num() == 0)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("ApplyGlobalNoise: no active chunks to modify."));
		return;
	}

	if (FMath::IsNearlyZero(Strength) || FMath::IsNearlyZero(Frequency))
	{
		UE_LOG(LogTerraDyne, Verbose,
			TEXT("ApplyGlobalNoise skipped: Strength=%.3f Frequency=%.6f"), Strength, Frequency);
		return;
	}

	TArray<FIntPoint> ChangedCoords;
	const float NoiseFrequency = FMath::Abs(Frequency);
	const FVector2D SeedOffset(Seed * 157.31f + 11.7f, Seed * 263.77f - 19.3f);

	for (const auto& Pair : ActiveChunkMap)
	{
		ATerraDyneChunk* Chunk = Pair.Value;
		if (!Chunk || Chunk->Resolution < 2 || Chunk->HeightBuffer.Num() != Chunk->Resolution * Chunk->Resolution)
		{
			continue;
		}

		const int32 NumSamples = Chunk->Resolution * Chunk->Resolution;
		if (Chunk->BaseBuffer.Num() != NumSamples ||
			Chunk->SculptBuffer.Num() != NumSamples ||
			Chunk->DetailBuffer.Num() != NumSamples)
		{
			UE_LOG(LogTerraDyne, Warning,
				TEXT("ApplyGlobalNoise: buffer size mismatch on chunk [%d,%d], skipping."),
				Pair.Key.X, Pair.Key.Y);
			continue;
		}

		TArray<float>* TargetBuffer = &Chunk->SculptBuffer;
		if (ActiveLayer == ETerraDyneLayer::Base)
		{
			TargetBuffer = &Chunk->BaseBuffer;
		}
		else if (ActiveLayer == ETerraDyneLayer::Detail)
		{
			TargetBuffer = &Chunk->DetailBuffer;
		}

		const float StrengthNorm = Strength / FMath::Max(Chunk->ZScale, KINDA_SMALL_NUMBER);
		const float Step = Chunk->WorldSize / static_cast<float>(Chunk->Resolution - 1);
		const float HalfSize = Chunk->WorldSize * 0.5f;
		const FVector ChunkOrigin = Chunk->GetActorLocation();

		for (int32 Y = 0; Y < Chunk->Resolution; Y++)
		{
			for (int32 X = 0; X < Chunk->Resolution; X++)
			{
				const int32 Index = Y * Chunk->Resolution + X;
				const float WorldX = ChunkOrigin.X - HalfSize + X * Step;
				const float WorldY = ChunkOrigin.Y - HalfSize + Y * Step;

				float Amplitude = 1.0f;
				float OctaveFrequency = NoiseFrequency;
				float NoiseSum = 0.0f;
				float AmplitudeSum = 0.0f;
				for (int32 Octave = 0; Octave < 3; Octave++)
				{
					const FVector2D SamplePoint(
						WorldX * OctaveFrequency + SeedOffset.X,
						WorldY * OctaveFrequency + SeedOffset.Y);
					NoiseSum += FMath::PerlinNoise2D(SamplePoint) * Amplitude;
					AmplitudeSum += Amplitude;
					Amplitude *= 0.5f;
					OctaveFrequency *= 2.0f;
				}

				const float SignedNoise = AmplitudeSum > SMALL_NUMBER ? NoiseSum / AmplitudeSum : 0.0f;
				(*TargetBuffer)[Index] = FMath::Clamp((*TargetBuffer)[Index] + SignedNoise * StrengthNorm, -1.0f, 2.0f);
				Chunk->HeightBuffer[Index] = FMath::Clamp(
					Chunk->BaseBuffer[Index] + Chunk->SculptBuffer[Index] + Chunk->DetailBuffer[Index],
					0.0f,
					1.0f);
			}
		}

		Chunk->RebuildPhysicsMesh();
		Chunk->RequestGrassRegen();
		Chunk->RequestTransferredFoliageRefresh();
		MarkChunkDirty(Pair.Key);
		ChangedCoords.AddUnique(Pair.Key);
	}

	if (ChangedCoords.Num() == 0)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("ApplyGlobalNoise: no valid chunks were modified."));
		return;
	}

	BroadcastTerrainChanged(ChangedCoords);
	BroadcastFoliageChanged(ChangedCoords);
	if (HasAuthority())
	{
		for (const FIntPoint& Coord : ChangedCoords)
		{
			SyncPopulationForChunk(Coord);
		}
	}

	UE_LOG(LogTerraDyne, Log,
		TEXT("ApplyGlobalNoise: modified %d chunks on layer %d (Strength=%.2f, Frequency=%.6f, Seed=%.2f)."),
		ChangedCoords.Num(), static_cast<int32>(ActiveLayer), Strength, Frequency, Seed);
}

bool ATerraDyneManager::ApplyAuthorizedBrush(
	const FTerraDyneBrushParams& Params,
	bool bReplicateToRelevantClients)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("ApplyAuthorizedBrush rejected on a non-authority manager."));
		return false;
	}

	const FVector Location(Params.WorldLocation);
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const float MaxRadius = Settings ? Settings->MaxBrushRadius : 10000.0f;
	const float MaxStrength = Settings ? Settings->MaxBrushStrength : 5000.0f;
	const int32 MaxAffectedChunks = Settings ? Settings->MaxAffectedChunksPerBrush : 16;
	const TArray<ATerraDyneChunk*> AffectedChunks = GetChunksInRadius(Location, Params.Radius);
	if (Location.ContainsNaN() || !FMath::IsFinite(Params.Radius) || !FMath::IsFinite(Params.Strength) ||
		!FMath::IsFinite(Params.FlattenHeight) || Params.Radius <= 0.0f || Params.Radius > MaxRadius ||
		FMath::Abs(Params.Strength) > MaxStrength || Params.WeightLayerIndex < 0 ||
		Params.WeightLayerIndex >= ATerraDyneChunk::NumWeightLayers || AffectedChunks.Num() == 0 ||
		AffectedChunks.Num() > MaxAffectedChunks)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("ApplyAuthorizedBrush rejected invalid or over-budget parameters."));
		return false;
	}

	ApplyGlobalBrush(
		Location,
		Params.Radius,
		Params.Strength,
		Params.BrushMode,
		Params.TargetLayer,
		Params.WeightLayerIndex,
		Params.FlattenHeight);
	if (bReplicateToRelevantClients)
	{
		Multicast_ApplyBrush(Params);
	}
	return true;
}

TArray<FIntPoint> ATerraDyneManager::GetActiveChunkCoordinates() const
{
	TArray<FIntPoint> Coordinates;
	ActiveChunkMap.GetKeys(Coordinates);
	Coordinates.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X == B.X ? A.Y < B.Y : A.X < B.X;
	});
	return Coordinates;
}

bool ATerraDyneManager::ExportChunkStatePacket(
	FIntPoint Coordinate,
	TArray<uint8>& OutPacket,
	FString& OutError) const
{
	const ATerraDyneChunk* Chunk = GetChunkAtCoord(Coordinate);
	if (!Chunk)
	{
		OutError = FString::Printf(TEXT("Chunk [%d,%d] is not active."), Coordinate.X, Coordinate.Y);
		OutPacket.Reset();
		return false;
	}
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxUncompressed = Settings
		? Settings->MaxUncompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxUncompressedBytes;
	const int32 MaxPacket = Settings
		? Settings->MaxCompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxPacketBytes;
	if (!FTerraDyneStateCodec::EncodeChunk(Chunk->GetSerializedData(), OutPacket, &OutError, MaxUncompressed))
	{
		return false;
	}
	if (OutPacket.Num() > MaxPacket)
	{
		OutError = FString::Printf(TEXT("Encoded chunk packet size %d exceeds the %d-byte export limit."),
			OutPacket.Num(), MaxPacket);
		OutPacket.Reset();
		return false;
	}
	return true;
}

bool ATerraDyneManager::ImportChunkStatePacket(
	const TArray<uint8>& Packet,
	FString& OutError,
	bool bReplicateToRelevantClients)
{
	if (!HasAuthority())
	{
		OutError = TEXT("Chunk state can only be imported by the authoritative manager.");
		return false;
	}
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxPacket = Settings
		? Settings->MaxCompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxPacketBytes;
	const int32 MaxUncompressed = Settings
		? Settings->MaxUncompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxUncompressedBytes;

	FTerraDyneChunkData Data;
	if (!FTerraDyneStateCodec::DecodeChunk(Packet, Data, &OutError, MaxPacket, MaxUncompressed))
	{
		return false;
	}
	if (!ShouldAllowChunkCoord(Data.Coordinate))
	{
		OutError = FString::Printf(TEXT("Chunk coordinate [%d,%d] is outside the configured world domain."),
			Data.Coordinate.X, Data.Coordinate.Y);
		return false;
	}

	ATerraDyneChunk* Chunk = GetChunkAtCoord(Data.Coordinate);
	if (!Chunk)
	{
		Chunk = SpawnConfiguredChunk(Data.Coordinate, Data.Resolution);
	}
	if (!Chunk)
	{
		OutError = TEXT("Failed to create the destination chunk.");
		return false;
	}
	Chunk->LoadFromData(Data);
	MarkChunkDirty(Data.Coordinate);
	TArray<FIntPoint> ChangedCoords;
	ChangedCoords.Add(Data.Coordinate);
	BroadcastTerrainChanged(ChangedCoords);
	BroadcastFoliageChanged(ChangedCoords);
	SyncPopulationForChunk(Data.Coordinate);
	if (bReplicateToRelevantClients)
	{
		BroadcastChunkStateToRelevantClients(Data);
	}
	return true;
}

void ATerraDyneManager::SaveWorld(FString SlotName)
{
	if (SlotName.IsEmpty()) SlotName = TEXT("TerraDyneSave");
	if (bWorldSaveInProgress)
	{
		QueuedWorldSaveSlot = SlotName;
		UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: save already running; queued latest request for slot '%s'."), *SlotName);
		return;
	}

	UTerraDyneSaveGame* SaveInst = Cast<UTerraDyneSaveGame>(UGameplayStatics::CreateSaveGameObject(UTerraDyneSaveGame::StaticClass()));
	if (!SaveInst) return;

	SaveInst->SaveFormatVersion = 2;
	SaveInst->Timestamp = FDateTime::Now();
	SaveInst->ManagerLocation = GetActorLocation();
	SaveInst->SavedGlobalChunkSize = GlobalChunkSize;
	SaveInst->LandscapeMigration = LandscapeMigrationState;
	SaveInst->ProceduralWorldSettings = ProceduralWorldSettings;
	SaveInst->BiomeOverlays = BiomeOverlays;
	SaveInst->PopulationSpawnRules = PopulationSpawnRules;
	SaveInst->AISpawnZones = AISpawnZones;
	SaveInst->BuildPermissionZones = BuildPermissionZones;
	SaveInst->ProceduralChunks = ProceduralChunkStates;
	SaveInst->PersistentPopulationEntries = PersistentPopulationEntries;
	SaveInst->AuthoredChunkCoords.Reset(ImportedChunkCoords.Num());
	for (const FIntPoint& Coord : ImportedChunkCoords)
	{
		SaveInst->AuthoredChunkCoords.Add(Coord);
	}

	if (MasterMaterial)
	{
		const FString MaterialPath = MasterMaterial->GetPathName();
		if (!MaterialPath.StartsWith(TEXT("/Engine/Transient")))
		{
			SaveInst->MasterMaterialPath = MaterialPath;
		}
	}

	if (ActiveGrassProfile)
	{
		const FString ProfilePath = ActiveGrassProfile->GetPathName();
		if (!ProfilePath.StartsWith(TEXT("/Engine/Transient")))
		{
			SaveInst->ActiveGrassProfilePath = ProfilePath;
		}
	}

	for (auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			SaveInst->Chunks.Add(Pair.Key, Pair.Value->GetSerializedData());
		}
	}

	bWorldSaveInProgress = true;
	const int32 ChunkCount = SaveInst->Chunks.Num();
	TWeakObjectPtr<ATerraDyneManager> WeakThis(this);
	FAsyncSaveGameToSlotDelegate SavedDelegate;
	SavedDelegate.BindLambda([WeakThis, ChunkCount](const FString& SavedSlot, const int32 UserIndex, bool bSuccess)
	{
		if (!WeakThis.IsValid())
		{
			return;
		}
		WeakThis->bWorldSaveInProgress = false;
		if (bSuccess)
		{
			UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: asynchronously saved slot '%s' with %d chunks."),
				*SavedSlot, ChunkCount);
		}
		else
		{
			UE_LOG(LogTerraDyne, Error, TEXT("TerraDyne: asynchronous save failed for slot '%s'."), *SavedSlot);
			if (UTerraDyneSubsystem* Sys = WeakThis->GetWorld()
				? WeakThis->GetWorld()->GetSubsystem<UTerraDyneSubsystem>()
				: nullptr)
			{
				Sys->ShowNotification(
					FText::Format(NSLOCTEXT("TerraDyne", "SaveFail", "Failed to save terrain to slot '{0}'"),
						FText::FromString(SavedSlot)),
					ETerraDyneNotifySeverity::Error);
			}
		}
		WeakThis->OnWorldSaveCompleted.Broadcast(SavedSlot, bSuccess);

		if (!WeakThis->QueuedWorldSaveSlot.IsEmpty())
		{
			const FString QueuedSlot = MoveTemp(WeakThis->QueuedWorldSaveSlot);
			WeakThis->QueuedWorldSaveSlot.Reset();
			WeakThis->SaveWorld(QueuedSlot);
		}
	});
	UGameplayStatics::AsyncSaveGameToSlot(SaveInst, SlotName, 0, MoveTemp(SavedDelegate));
}

void ATerraDyneManager::LoadWorld(FString SlotName)
{
	if (SlotName.IsEmpty()) SlotName = TEXT("TerraDyneSave");

	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne: No save game found at slot '%s'"), *SlotName);
		if (UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
		{
			Sys->ShowNotification(
				FText::Format(NSLOCTEXT("TerraDyne", "LoadNoSlot", "No save found at slot '{0}'"),
					FText::FromString(SlotName)),
				ETerraDyneNotifySeverity::Warning);
		}
		return;
	}

	UTerraDyneSaveGame* LoadInst = Cast<UTerraDyneSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!LoadInst) return;
	if (LoadInst->SaveFormatVersion > 2)
	{
		UE_LOG(LogTerraDyne, Error, TEXT("TerraDyne: save slot '%s' uses unsupported future format %d."),
			*SlotName, LoadInst->SaveFormatVersion);
		return;
	}

	// Clear streaming state — full world load resets everything
	PendingLoadQueue.Empty();
	PendingUnloadQueue.Empty();
	DirtyChunkSet.Empty();
	ImportedChunkCoords.Empty();
	LastStreamingHash = 0;
	ClearUndoRedoState();
	DestroyTrackedPopulationActors();
	PersistentPopulationEntries.Reset();
	ProceduralChunkStates.Reset();
	BiomeOverlays.Reset();
	PopulationSpawnRules.Reset();
	AISpawnZones.Reset();
	BuildPermissionZones.Reset();
	RebuildWorldStateIndices();

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: Loading world from slot '%s'..."), *SlotName);

	SetActorLocation(LoadInst->ManagerLocation);
	if (LoadInst->SavedGlobalChunkSize > KINDA_SMALL_NUMBER)
	{
		GlobalChunkSize = LoadInst->SavedGlobalChunkSize;
	}

	LandscapeMigrationState = LoadInst->LandscapeMigration;
	ProceduralWorldSettings = LoadInst->ProceduralWorldSettings;
	BiomeOverlays = LoadInst->BiomeOverlays;
	PopulationSpawnRules = LoadInst->PopulationSpawnRules;
	AISpawnZones = LoadInst->AISpawnZones;
	BuildPermissionZones = LoadInst->BuildPermissionZones;
	ProceduralChunkStates = LoadInst->ProceduralChunks;
	PersistentPopulationEntries = LoadInst->PersistentPopulationEntries;
	for (const FIntPoint& Coord : LoadInst->AuthoredChunkCoords)
	{
		ImportedChunkCoords.Add(Coord);
	}
	RebuildWorldStateIndices();
	if (!LoadInst->MasterMaterialPath.IsEmpty())
	{
		if (UMaterialInterface* LoadedMaterial = LoadObject<UMaterialInterface>(nullptr, *LoadInst->MasterMaterialPath))
		{
			MasterMaterial = LoadedMaterial;
		}
	}

	if (!LoadInst->ActiveGrassProfilePath.IsEmpty())
	{
		if (UTerraDyneGrassProfile* LoadedProfile = LoadObject<UTerraDyneGrassProfile>(nullptr, *LoadInst->ActiveGrassProfilePath))
		{
			ActiveGrassProfile = LoadedProfile;
		}
	}

	TSet<FIntPoint> SavedCoords;
	for (const auto& Pair : LoadInst->Chunks)
	{
		SavedCoords.Add(Pair.Key);
	}

	TArray<FIntPoint> ExistingCoords;
	ActiveChunkMap.GetKeys(ExistingCoords);
	for (const FIntPoint& Coord : ExistingCoords)
	{
		if (!SavedCoords.Contains(Coord))
		{
			if (ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord))
			{
				ActiveChunkMap.Remove(Coord);
				Chunk->Destroy();
			}
		}
	}

	for (const auto& Pair : LoadInst->Chunks)
	{
		FIntPoint Coord = Pair.Key;
		const FTerraDyneChunkData& Data = Pair.Value;
		if (LoadInst->AuthoredChunkCoords.Num() == 0 && LandscapeMigrationState.bWasImportedFromLandscape)
		{
			ImportedChunkCoords.Add(Coord);
		}

		ATerraDyneChunk* Chunk = nullptr;

		if (ActiveChunkMap.Contains(Coord))
		{
			Chunk = ActiveChunkMap[Coord];
		}
		else
		{
			Chunk = SpawnConfiguredChunk(Coord, Data.Resolution);
		}

		if (Chunk)
		{
			Chunk->LoadFromData(Data);
		}
	}

	RebuildChunkMap();
	ApplyMaterialToActiveChunks();
	ApplyGrassProfileToActiveChunks(LandscapeMigrationState.bRegenerateGrassFromImportedLayers);
	OnRep_FrameworkConfig();
	RefreshPopulationForLoadedChunks();
}

void ATerraDyneManager::SetAuthoredChunkCoordinates(
	const TArray<FIntPoint>& ChunkCoords,
	bool bWasImportedFromLandscape)
{
	ImportedChunkCoords.Empty();
	for (const FIntPoint& Coord : ChunkCoords)
	{
		ImportedChunkCoords.Add(Coord);
	}

	LandscapeMigrationState.bWasImportedFromLandscape =
		bWasImportedFromLandscape && ImportedChunkCoords.Num() > 0;
	LandscapeMigrationState.ImportedComponentCount = ImportedChunkCoords.Num();
	LandscapeMigrationState.ImportedChunkSize = GlobalChunkSize;
	LandscapeMigrationState.RuntimeManagerLocation = GetActorLocation();

	OnRep_LandscapeMigrationState();
}

bool ATerraDyneManager::InitializeFromBakedLandscapeAssetSet(
	UTerraDyneLandscapeAssetSet* AssetSet,
	bool bClearExistingChunks)
{
	if (!AssetSet || !GetWorld())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne Import: Invalid baked Landscape asset set."));
		if (UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
		{
			Sys->ShowNotification(
				NSLOCTEXT("TerraDyne", "ImportBakedNull", "Initialize failed: no baked Landscape asset set was provided."),
				ETerraDyneNotifySeverity::Error);
		}
		return false;
	}

	if (AssetSet->Tiles.Num() == 0)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne Import: Baked Landscape asset set %s contains no tiles."), *AssetSet->GetName());
		if (UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
		{
			Sys->ShowNotification(
				NSLOCTEXT("TerraDyne", "ImportBakedEmpty", "Initialize failed: the baked Landscape asset set contains no chunk tiles."),
				ETerraDyneNotifySeverity::Error);
		}
		return false;
	}

	PendingLoadQueue.Empty();
	PendingUnloadQueue.Empty();
	DirtyChunkSet.Empty();
	ImportedChunkCoords.Empty();
	LastStreamingHash = 0;
	ClearUndoRedoState();

	if (bClearExistingChunks)
	{
		TArray<AActor*> ExistingChunks;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), ExistingChunks);
		for (AActor* Actor : ExistingChunks)
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		ActiveChunkMap.Reset();
	}

	LandscapeMigrationState = AssetSet->MigrationState;
	if (LandscapeMigrationState.ImportedChunkSize > KINDA_SMALL_NUMBER)
	{
		GlobalChunkSize = LandscapeMigrationState.ImportedChunkSize;
	}
	else if (const UTerraDyneTileData* FirstTile = AssetSet->Tiles[0])
	{
		GlobalChunkSize = FirstTile->RealWorldSize;
		LandscapeMigrationState.ImportedChunkSize = GlobalChunkSize;
	}

	if (LandscapeMigrationState.bWasImportedFromLandscape)
	{
		SetActorLocation(LandscapeMigrationState.RuntimeManagerLocation);
	}

	if (LandscapeMigrationState.bAdoptedLandscapeMaterial && AssetSet->AdoptedMasterMaterial)
	{
		MasterMaterial = AssetSet->AdoptedMasterMaterial;
	}

	int32 ImportedChunks = 0;
	int32 ImportedResolution = LandscapeMigrationState.ImportedResolution;

	for (UTerraDyneTileData* Tile : AssetSet->Tiles)
	{
		if (!Tile)
		{
			continue;
		}

		const FIntPoint Coord = Tile->GridCoordinate;
		ImportedChunkCoords.Add(Coord);

		ATerraDyneChunk* Chunk = SpawnConfiguredChunk(Coord, Tile->Resolution);
		if (!Chunk)
		{
			continue;
		}

		Chunk->LoadFromData(Tile->BuildChunkData());
		ImportedResolution = FMath::Max(ImportedResolution, Tile->Resolution);
		ImportedChunks++;
	}

	if (ImportedChunks == 0)
	{
		if (UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
		{
			Sys->ShowNotification(
				NSLOCTEXT("TerraDyne", "ImportBakedNoChunks",
					"Initialize failed: TerraDyne could not spawn chunks from the baked Landscape asset set."),
				ETerraDyneNotifySeverity::Error);
		}
		return false;
	}

	LandscapeMigrationState.ImportedComponentCount = ImportedChunks;
	LandscapeMigrationState.ImportedResolution = ImportedResolution;
	LandscapeMigrationState.RuntimeManagerLocation = GetActorLocation();

	RebuildChunkMap();
	ApplyMaterialToActiveChunks();
	ApplyGrassProfileToActiveChunks(LandscapeMigrationState.bRegenerateGrassFromImportedLayers);
	RebuildWorldStateIndices();
	OnRep_FrameworkConfig();
	OnRep_LandscapeMigrationState();
	RefreshPopulationForLoadedChunks();

	UE_LOG(
		LogTerraDyne,
		Log,
		TEXT("TerraDyne Import: Initialized %d authored chunks from baked asset set %s."),
		ImportedChunks,
		*AssetSet->GetName());
	return true;
}

void ATerraDyneManager::RebuildChunkMap()
{
	ActiveChunkMap.Reset();
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), FoundActors);
	for (AActor* Actor : FoundActors)
	{
		if (ATerraDyneChunk* Chunk = Cast<ATerraDyneChunk>(Actor))
		{
			if (Chunk->IsActorBeingDestroyed())
			{
				continue;
			}
			ActiveChunkMap.Add(Chunk->GridCoordinate, Chunk);
		}
	}

	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			EnsureChunkFrameworkState(Pair.Key, Pair.Value);
		}
	}
}

void ATerraDyneManager::ApplyMaterialToActiveChunks()
{
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			if (HeightBrushMaterial)
			{
				Pair.Value->BrushMaterialBase = HeightBrushMaterial;
			}
			Pair.Value->SetMaterial(MasterMaterial);
		}
	}
}

void ATerraDyneManager::ApplyGrassProfileToActiveChunks(bool bTriggerRegen)
{
	for (const auto& Pair : ActiveChunkMap)
	{
		if (!Pair.Value)
		{
			continue;
		}

		Pair.Value->SetGrassProfile(ActiveGrassProfile);
		if (bTriggerRegen && ActiveGrassProfile)
		{
			Pair.Value->RequestGrassRegen();
		}
	}
}

void ATerraDyneManager::DestroyTrackedPopulationActors()
{
	TArray<TObjectPtr<AActor>> ActorsToDestroy;
	ActorsToDestroy.Reserve(PopulationActorById.Num());
	for (const auto& Pair : PopulationActorById)
	{
		if (AActor* Actor = Pair.Value.Get())
		{
			ActorsToDestroy.Add(Actor);
		}
	}

	PopulationIdByActor.Reset();
	PopulationActorById.Reset();
	PopulationDestroyInProgress.Reset();

	for (AActor* Actor : ActorsToDestroy)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
}

void ATerraDyneManager::ClearUndoRedoState()
{
	UndoStacks.Reset();
	RedoStacks.Reset();
	PendingStrokes.Reset();
}

bool ATerraDyneManager::IsImportedChunkCoord(FIntPoint Coord) const
{
	return ImportedChunkCoords.Contains(Coord);
}

bool ATerraDyneManager::ShouldAllowChunkCoord(FIntPoint Coord) const
{
	if (ImportedChunkCoords.Contains(Coord))
	{
		return true;
	}

	if (ImportedChunkCoords.Num() == 0)
	{
		if (ProceduralWorldSettings.bEnableInfiniteEdgeGrowth)
		{
			return true;
		}

		if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
		{
			return FMath::Abs(Coord.X) <= Settings->GridExtent && FMath::Abs(Coord.Y) <= Settings->GridExtent;
		}

		return true;
	}

	if (!ProceduralWorldSettings.bEnableSeededOutskirts)
	{
		return false;
	}

	if (ProceduralWorldSettings.bEnableInfiniteEdgeGrowth)
	{
		return true;
	}

	return GetDistanceFromAuthoredChunks(Coord) <= FMath::Max(0, ProceduralWorldSettings.ProceduralOutskirtsExtent);
}

int32 ATerraDyneManager::GetDistanceFromAuthoredChunks(FIntPoint Coord) const
{
	if (ImportedChunkCoords.Num() == 0)
	{
		return FMath::Max(FMath::Abs(Coord.X), FMath::Abs(Coord.Y));
	}

	int32 MinDistance = TNumericLimits<int32>::Max();
	for (const FIntPoint& AuthoredCoord : ImportedChunkCoords)
	{
		MinDistance = FMath::Min(
			MinDistance,
			FMath::Abs(Coord.X - AuthoredCoord.X) + FMath::Abs(Coord.Y - AuthoredCoord.Y));
	}

	return MinDistance == TNumericLimits<int32>::Max() ? 0 : MinDistance;
}

int32 ATerraDyneManager::MakeChunkSeed(FIntPoint Coord) const
{
	return static_cast<int32>(HashCombine(GetTypeHash(Coord), GetTypeHash(ProceduralWorldSettings.WorldSeed)));
}

void ATerraDyneManager::RebuildWorldStateIndices()
{
	PopulationEntryIndexById.Reset();
	for (int32 Index = 0; Index < PersistentPopulationEntries.Num(); ++Index)
	{
		if (!PersistentPopulationEntries[Index].PopulationId.IsValid())
		{
			PersistentPopulationEntries[Index].PopulationId = FGuid::NewGuid();
		}

		PopulationEntryIndexById.Add(PersistentPopulationEntries[Index].PopulationId, Index);
	}

	ProceduralChunkStateIndexByCoord.Reset();
	for (int32 Index = 0; Index < ProceduralChunkStates.Num(); ++Index)
	{
		ProceduralChunkStateIndexByCoord.Add(ProceduralChunkStates[Index].Coordinate, Index);
	}
}

FTerraDyneProceduralChunkState* ATerraDyneManager::FindProceduralChunkState(FIntPoint Coord)
{
	if (const int32* Index = ProceduralChunkStateIndexByCoord.Find(Coord))
	{
		return ProceduralChunkStates.IsValidIndex(*Index) ? &ProceduralChunkStates[*Index] : nullptr;
	}

	return nullptr;
}

const FTerraDyneProceduralChunkState* ATerraDyneManager::FindProceduralChunkState(FIntPoint Coord) const
{
	if (const int32* Index = ProceduralChunkStateIndexByCoord.Find(Coord))
	{
		return ProceduralChunkStates.IsValidIndex(*Index) ? &ProceduralChunkStates[*Index] : nullptr;
	}

	return nullptr;
}

FName ATerraDyneManager::ResolveBiomeForChunk(FIntPoint Coord, const ATerraDyneChunk* Chunk, TArray<FName>* OutOverlays) const
{
	FName SelectedBiome = ImportedChunkCoords.Contains(Coord) ? FName(TEXT("Authored")) : FName(TEXT("Outskirts"));
	int32 SelectedPriority = TNumericLimits<int32>::Lowest();
	const bool bAuthoredChunk = ImportedChunkCoords.Contains(Coord);
	const float ProceduralNoise = FRandomStream(MakeChunkSeed(Coord)).FRand();

	if (OutOverlays)
	{
		OutOverlays->Reset();
	}

	for (const FTerraDyneBiomeOverlay& Overlay : BiomeOverlays)
	{
		bool bMatches = false;

		if (bAuthoredChunk && Overlay.bApplyToAuthoredChunks && Chunk && Overlay.WeightLayerIndex >= 0 &&
			Overlay.WeightLayerIndex < ATerraDyneChunk::NumWeightLayers)
		{
			const TArray<float>& WeightBuffer = Chunk->WeightBuffers[Overlay.WeightLayerIndex];
			if (WeightBuffer.Num() > 0)
			{
				double WeightSum = 0.0;
				for (const float Sample : WeightBuffer)
				{
					WeightSum += Sample;
				}

				const float AverageWeight = static_cast<float>(WeightSum / WeightBuffer.Num());
				bMatches = AverageWeight >= Overlay.MinimumPaintWeight;
			}
		}
		else if (!bAuthoredChunk && Overlay.bApplyToProceduralChunks)
		{
			bMatches = ProceduralNoise >= Overlay.ProceduralNoiseMin && ProceduralNoise <= Overlay.ProceduralNoiseMax;
		}

		if (!bMatches)
		{
			continue;
		}

		if (OutOverlays)
		{
			OutOverlays->AddUnique(Overlay.BiomeTag);
		}

		if (Overlay.Priority >= SelectedPriority)
		{
			SelectedPriority = Overlay.Priority;
			SelectedBiome = Overlay.BiomeTag;
		}
	}

	return SelectedBiome;
}

void ATerraDyneManager::EnsureChunkFrameworkState(FIntPoint Coord, ATerraDyneChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	FTerraDyneProceduralChunkState* State = FindProceduralChunkState(Coord);
	if (!State)
	{
		FTerraDyneProceduralChunkState NewState;
		NewState.Coordinate = Coord;
		NewState.ChunkSeed = MakeChunkSeed(Coord);
		NewState.bIsAuthoredChunk = ImportedChunkCoords.Contains(Coord);
		NewState.bGeneratedOutskirtsChunk = !NewState.bIsAuthoredChunk;
		NewState.DistanceFromAuthored = GetDistanceFromAuthoredChunks(Coord);
		NewState.PrimaryBiomeTag = ResolveBiomeForChunk(Coord, Chunk, &NewState.OverlayBiomeTags);

		const int32 NewIndex = ProceduralChunkStates.Add(MoveTemp(NewState));
		ProceduralChunkStateIndexByCoord.Add(Coord, NewIndex);
		State = &ProceduralChunkStates[NewIndex];
	}
	else
	{
		State->ChunkSeed = MakeChunkSeed(Coord);
		State->bIsAuthoredChunk = ImportedChunkCoords.Contains(Coord);
		State->bGeneratedOutskirtsChunk = !State->bIsAuthoredChunk;
		State->DistanceFromAuthored = GetDistanceFromAuthoredChunks(Coord);
		State->PrimaryBiomeTag = ResolveBiomeForChunk(Coord, Chunk, &State->OverlayBiomeTags);
	}

	Chunk->ProceduralSeed = State->ChunkSeed;
	Chunk->bIsAuthoredChunk = State->bIsAuthoredChunk;
	Chunk->PrimaryBiomeTag = State->PrimaryBiomeTag;
}

void ATerraDyneManager::RegenerateImportedGrass()
{
	if (!LandscapeMigrationState.bWasImportedFromLandscape)
	{
		return;
	}

	ApplyGrassProfileToActiveChunks(LandscapeMigrationState.bRegenerateGrassFromImportedLayers);
}

void ATerraDyneManager::OnRep_MasterMaterial()
{
	ApplyMaterialToActiveChunks();
}

void ATerraDyneManager::OnRep_ActiveGrassProfile()
{
	ApplyGrassProfileToActiveChunks(LandscapeMigrationState.bRegenerateGrassFromImportedLayers);
}

void ATerraDyneManager::OnRep_LandscapeMigrationState()
{
	if (LandscapeMigrationState.bRegenerateGrassFromImportedLayers)
	{
		ApplyGrassProfileToActiveChunks(ActiveGrassProfile != nullptr);
	}

	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			Pair.Value->SetTransferredFoliageFollowsTerrain(LandscapeMigrationState.bTransferredFoliageFollowsTerrain);
		}
	}

	OnRep_FrameworkConfig();
}

void ATerraDyneManager::OnRep_FrameworkConfig()
{
	RebuildWorldStateIndices();
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			EnsureChunkFrameworkState(Pair.Key, Pair.Value);
		}
	}

	if (HasAuthority())
	{
		RefreshPopulationForLoadedChunks();
	}
}

FTerraDyneGPUStats ATerraDyneManager::GetGPUStats() const
{
	FTerraDyneGPUStats Stats;
	
	int32 GPUChunks = 0;
	int32 CPUChunks = 0;
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value && Pair.Value->IsUsingGPU())
		{
			GPUChunks++;
		}
		else
		{
			CPUChunks++;
		}
	}
	
	if (GPUChunks > 0)
	{
		Stats.AdapterName = TEXT("Hybrid Render Target Support");
		Stats.bIsCudaAvailable = true;
		Stats.ComputeBackend = FString::Printf(TEXT("Hybrid CPU Mesh (%d RT-enabled chunks, %d CPU-only)"), GPUChunks, CPUChunks);
	}
	else
	{
		Stats.AdapterName = TEXT("CPU Dynamic Mesh");
		Stats.bIsCudaAvailable = false;
		Stats.ComputeBackend = TEXT("CPU Sculpt Pipeline");
	}
	
	Stats.DedicatedVideoMemory = 0;
	return Stats;
}

int32 ATerraDyneManager::GetTotalVertexCount() const
{
	int32 Total = 0;
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			int32 Res = Pair.Value->Resolution;
			Total += Res * Res;
		}
	}
	return Total;
}

void ATerraDyneManager::ApplyWorldPreset(UTerraDyneWorldPreset* Preset)
{
	UTerraDyneWorldPreset* PresetToApply = Preset ? Preset : WorldPreset.Get();
	if (!PresetToApply)
	{
		return;
	}

	WorldPreset = PresetToApply;
	ProceduralWorldSettings = PresetToApply->ProceduralSettings;
	BiomeOverlays = PresetToApply->BiomeOverlays;
	PopulationSpawnRules = PresetToApply->PopulationSpawnRules;
	AISpawnZones = PresetToApply->AISpawnZones;
	BuildPermissionZones = PresetToApply->BuildPermissionZones;
	bSpawnDefaultChunksOnBeginPlay = PresetToApply->bSpawnDefaultChunksOnBeginPlay;
	bSetupDefaultLightingOnBeginPlay = PresetToApply->bSetupDefaultLightingOnBeginPlay;
	bRefreshNavMeshOnTerrainChanges = PresetToApply->bRefreshNavMeshOnTerrainChanges;
	bRefreshNavMeshOnPopulationChanges = PresetToApply->bRefreshNavMeshOnPopulationChanges;
	NavigationDirtyBoundsPadding = PresetToApply->NavigationDirtyBoundsPadding;
	OnRep_FrameworkConfig();
}

FTerraDynePopulationDescriptor ATerraDyneManager::MakeDescriptorFromActor(
	AActor* Actor,
	ETerraDynePopulationKind Kind,
	bool bAllowRegrowth,
	float RegrowthDelaySeconds,
	bool bSnapToTerrain) const
{
	FTerraDynePopulationDescriptor Descriptor;
	Descriptor.Kind = Kind;
	Descriptor.TypeId = Actor ? Actor->GetClass()->GetFName() : NAME_None;
	Descriptor.ActorClass = Actor ? Actor->GetClass() : nullptr;
	Descriptor.bReplicates = true;
	Descriptor.bAttachToChunk = false;
	Descriptor.bSnapToTerrain = bSnapToTerrain;
	Descriptor.bAllowRegrowth = bAllowRegrowth;
	Descriptor.RegrowthDelaySeconds = RegrowthDelaySeconds;

	if (const AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor))
	{
		if (const UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent())
		{
			Descriptor.StaticMesh = StaticMeshComponent->GetStaticMesh();
			Descriptor.MaterialOverride = StaticMeshComponent->GetMaterial(0);
		}
	}

	return Descriptor;
}

FGuid ATerraDyneManager::RegisterPersistentActor(
	AActor* Actor,
	ETerraDynePopulationKind Kind,
	bool bAllowRegrowth,
	float RegrowthDelaySeconds,
	bool bSnapToTerrain)
{
	if (!HasAuthority() || !Actor)
	{
		return FGuid();
	}

	if (const FGuid* ExistingId = PopulationIdByActor.Find(Actor))
	{
		return *ExistingId;
	}

	FTerraDynePopulationDescriptor Descriptor =
		MakeDescriptorFromActor(Actor, Kind, bAllowRegrowth, RegrowthDelaySeconds, bSnapToTerrain);
	const FTransform WorldTransform = Actor->GetActorTransform();
	FGuid PopulationId = FGuid::NewGuid();

	FTerraDynePersistentPopulationEntry Entry;
	Entry.PopulationId = PopulationId;
	Entry.Descriptor = Descriptor;
	Entry.ChunkCoord = WorldToChunkCoord(WorldTransform.GetLocation());
	Entry.LocalTransform = WorldTransform.GetRelativeTransform(GetActorTransform());
	Entry.State = ETerraDynePopulationState::Active;
	Entry.CachedBiomeTag = GetBiomeTagAtLocation(WorldTransform.GetLocation());
	Entry.Descriptor.BiomeTag = Entry.CachedBiomeTag;

	if (Descriptor.bSnapToTerrain)
	{
		if (ATerraDyneChunk* Chunk = GetChunkAtCoord(Entry.ChunkCoord))
		{
			const FVector ChunkLocal = WorldTransform.GetLocation() - Chunk->GetActorLocation();
			const float TerrainZ = Chunk->GetActorLocation().Z +
				Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y, 0.0f));
			Entry.Descriptor.TerrainOffset = WorldTransform.GetLocation().Z - TerrainZ;
		}
	}

	const int32 NewIndex = PersistentPopulationEntries.Add(MoveTemp(Entry));
	PopulationEntryIndexById.Add(PersistentPopulationEntries[NewIndex].PopulationId, NewIndex);
	BindPopulationActor(PersistentPopulationEntries[NewIndex].PopulationId, Actor);
	if (HasAuthority() && Actor->IsActorInitialized())
	{
		Actor->SetReplicates(PersistentPopulationEntries[NewIndex].Descriptor.bReplicates);
	}
	BroadcastPopulationChanged(PersistentPopulationEntries[NewIndex], FName(TEXT("Registered")));
	RefreshNavigationForBounds(Actor->GetComponentsBoundingBox(true), true);
	return PopulationId;
}

FGuid ATerraDyneManager::PlacePersistentActorFromDescriptor(
	const FTerraDynePopulationDescriptor& Descriptor,
	const FTransform& WorldTransform,
	FName SourceRuleId)
{
	if (!HasAuthority())
	{
		return FGuid();
	}

	if (Descriptor.ActorClass.IsNull() && Descriptor.StaticMesh.IsNull())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne: PlacePersistentActorFromDescriptor called without a class or mesh."));
		return FGuid();
	}

	FTerraDynePersistentPopulationEntry Entry;
	Entry.PopulationId = FGuid::NewGuid();
	Entry.Descriptor = Descriptor;
	Entry.ChunkCoord = WorldToChunkCoord(WorldTransform.GetLocation());
	Entry.LocalTransform = WorldTransform.GetRelativeTransform(GetActorTransform());
	Entry.State = ETerraDynePopulationState::Active;
	Entry.SourceRuleId = SourceRuleId;
	Entry.bSpawnedFromRule = SourceRuleId != NAME_None;
	Entry.CachedBiomeTag = Descriptor.BiomeTag != NAME_None ? Descriptor.BiomeTag : GetBiomeTagAtLocation(WorldTransform.GetLocation());
	Entry.Descriptor.BiomeTag = Entry.CachedBiomeTag;

	if (Entry.Descriptor.TypeId == NAME_None)
	{
		if (!Descriptor.ActorClass.IsNull())
		{
			if (UClass* LoadedClass = Descriptor.ActorClass.LoadSynchronous())
			{
				Entry.Descriptor.TypeId = LoadedClass->GetFName();
			}
		}
		else if (UStaticMesh* LoadedMesh = Descriptor.StaticMesh.LoadSynchronous())
		{
			Entry.Descriptor.TypeId = LoadedMesh->GetFName();
		}
	}

	const int32 NewIndex = PersistentPopulationEntries.Add(MoveTemp(Entry));
	PopulationEntryIndexById.Add(PersistentPopulationEntries[NewIndex].PopulationId, NewIndex);
	SpawnPopulationActor(PersistentPopulationEntries[NewIndex]);
	BroadcastPopulationChanged(PersistentPopulationEntries[NewIndex], FName(TEXT("Placed")));
	RefreshNavigationForBounds(
		TerraDyneWorldFramework::MakeChunkBounds(*this, PersistentPopulationEntries[NewIndex].ChunkCoord, GlobalChunkSize),
		true);
	return PersistentPopulationEntries[NewIndex].PopulationId;
}

FGuid ATerraDyneManager::PlacePersistentActor(
	TSubclassOf<AActor> ActorClass,
	const FTransform& WorldTransform,
	ETerraDynePopulationKind Kind,
	bool bSnapToTerrain,
	float TerrainOffset,
	bool bAllowRegrowth,
	float RegrowthDelaySeconds)
{
	FTerraDynePopulationDescriptor Descriptor;
	Descriptor.Kind = Kind;
	Descriptor.ActorClass = ActorClass;
	Descriptor.TypeId = ActorClass ? ActorClass->GetFName() : NAME_None;
	Descriptor.bReplicates = true;
	Descriptor.bSnapToTerrain = bSnapToTerrain;
	Descriptor.TerrainOffset = TerrainOffset;
	Descriptor.bAllowRegrowth = bAllowRegrowth;
	Descriptor.RegrowthDelaySeconds = RegrowthDelaySeconds;
	return PlacePersistentActorFromDescriptor(Descriptor, WorldTransform);
}

void ATerraDyneManager::BindPopulationActor(const FGuid& PopulationId, AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	PopulationActorById.Add(PopulationId, Actor);
	PopulationIdByActor.Add(Actor, PopulationId);
	Actor->OnDestroyed.RemoveDynamic(this, &ATerraDyneManager::HandlePopulationActorDestroyed);
	Actor->OnDestroyed.AddUniqueDynamic(this, &ATerraDyneManager::HandlePopulationActorDestroyed);
}

AActor* ATerraDyneManager::SpawnPopulationActor(const FTerraDynePersistentPopulationEntry& Entry)
{
	if (!HasAuthority())
	{
		return nullptr;
	}

	if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(Entry.PopulationId))
	{
		if (ExistingActor->IsValid())
		{
			return ExistingActor->Get();
		}
	}

	ATerraDyneChunk* Chunk = GetChunkAtCoord(Entry.ChunkCoord);
	if (!Chunk || !GetWorld())
	{
		return nullptr;
	}

	FTransform WorldTransform = TerraDyneWorldFramework::MakeWorldTransform(Entry.LocalTransform, GetActorTransform());
	if (Entry.Descriptor.bSnapToTerrain)
	{
		FVector WorldLocation = WorldTransform.GetLocation();
		const FVector ChunkLocal = WorldLocation - Chunk->GetActorLocation();
		WorldLocation.Z = Chunk->GetActorLocation().Z +
			Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y, 0.0f)) +
			Entry.Descriptor.TerrainOffset;
		WorldTransform.SetLocation(WorldLocation);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* SpawnedActor = nullptr;
	if (UClass* DesiredClass = Entry.Descriptor.ActorClass.LoadSynchronous())
	{
		SpawnedActor = GetWorld()->SpawnActor<AActor>(DesiredClass, WorldTransform, SpawnParams);
	}
	else if (UStaticMesh* StaticMesh = Entry.Descriptor.StaticMesh.LoadSynchronous())
	{
		if (AStaticMeshActor* StaticMeshActor = GetWorld()->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			WorldTransform,
			SpawnParams))
		{
			SpawnedActor = StaticMeshActor;
		}
	}

	if (!SpawnedActor)
	{
		return nullptr;
	}

	PreparePopulationActorForRuntimePlacement(SpawnedActor);

	if (UStaticMesh* StaticMesh = Entry.Descriptor.StaticMesh.LoadSynchronous())
	{
		if (UStaticMeshComponent* StaticMeshComponent = SpawnedActor->FindComponentByClass<UStaticMeshComponent>())
		{
			StaticMeshComponent->SetStaticMesh(StaticMesh);
			if (UMaterialInterface* MaterialOverride = Entry.Descriptor.MaterialOverride.LoadSynchronous())
			{
				StaticMeshComponent->SetMaterial(0, MaterialOverride);
			}
		}
	}

	if (SpawnedActor->IsActorInitialized())
	{
		SpawnedActor->SetReplicates(Entry.Descriptor.bReplicates);
	}
	SpawnedActor->Tags.AddUnique(FName(TEXT("TerraDynePersistent")));
	if (Entry.Descriptor.TypeId != NAME_None)
	{
		SpawnedActor->Tags.AddUnique(Entry.Descriptor.TypeId);
	}
	for (const FName& Tag : Entry.Descriptor.Tags)
	{
		SpawnedActor->Tags.AddUnique(Tag);
	}

	if (Entry.Descriptor.bAttachToChunk && SpawnedActor->GetRootComponent() && Chunk->GetRootComponent())
	{
		SpawnedActor->AttachToComponent(Chunk->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}

	BindPopulationActor(Entry.PopulationId, SpawnedActor);
	return SpawnedActor;
}

void ATerraDyneManager::SetPopulationStateInternal(
	const FGuid& PopulationId,
	ETerraDynePopulationState NewState,
	FName Reason)
{
	const int32* EntryIndex = PopulationEntryIndexById.Find(PopulationId);
	if (!EntryIndex || !PersistentPopulationEntries.IsValidIndex(*EntryIndex))
	{
		return;
	}

	FTerraDynePersistentPopulationEntry& Entry = PersistentPopulationEntries[*EntryIndex];
	if (Entry.State == NewState && NewState != ETerraDynePopulationState::Active)
	{
		return;
	}

	FBox DirtyBounds = TerraDyneWorldFramework::MakeChunkBounds(*this, Entry.ChunkCoord, GlobalChunkSize);
	if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(PopulationId))
	{
		if (ExistingActor->IsValid())
		{
			DirtyBounds = ExistingActor->Get()->GetComponentsBoundingBox(true);
		}
	}

	Entry.State = NewState;
	Entry.RemainingRegrowthTimeSeconds =
		NewState == ETerraDynePopulationState::Regrowing
			? FMath::Max(0.05f, Entry.Descriptor.RegrowthDelaySeconds)
			: 0.0f;

	if (NewState == ETerraDynePopulationState::Active)
	{
		SyncPopulationForChunk(Entry.ChunkCoord);
	}
	else if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(PopulationId))
	{
		if (ExistingActor->IsValid())
		{
			PopulationDestroyInProgress.Add(PopulationId);
			ExistingActor->Get()->Destroy();
		}
	}

	BroadcastPopulationChanged(Entry, Reason);
	RefreshNavigationForBounds(DirtyBounds, true);
}

bool ATerraDyneManager::HarvestPersistentPopulation(const FGuid& PopulationId)
{
	const int32* EntryIndex = PopulationEntryIndexById.Find(PopulationId);
	if (!EntryIndex || !PersistentPopulationEntries.IsValidIndex(*EntryIndex))
	{
		return false;
	}

	const FTerraDynePersistentPopulationEntry& Entry = PersistentPopulationEntries[*EntryIndex];
	const ETerraDynePopulationState NewState =
		Entry.Descriptor.bAllowRegrowth && Entry.Descriptor.RegrowthDelaySeconds > 0.0f
			? ETerraDynePopulationState::Regrowing
			: ETerraDynePopulationState::Harvested;
	SetPopulationStateInternal(PopulationId, NewState, FName(TEXT("Harvested")));
	return true;
}

bool ATerraDyneManager::SetPersistentPopulationDestroyed(const FGuid& PopulationId, bool bDestroyed)
{
	SetPopulationStateInternal(
		PopulationId,
		bDestroyed ? ETerraDynePopulationState::Destroyed : ETerraDynePopulationState::Active,
		bDestroyed ? FName(TEXT("Destroyed")) : FName(TEXT("Restored")));
	return PopulationEntryIndexById.Contains(PopulationId);
}

void ATerraDyneManager::GeneratePopulationForChunk(FIntPoint Coord, ATerraDyneChunk* Chunk)
{
	if (!HasAuthority() || !Chunk || !ProceduralWorldSettings.bGeneratePopulationFromRules)
	{
		return;
	}

	FTerraDyneProceduralChunkState* ChunkState = FindProceduralChunkState(Coord);
	if (!ChunkState)
	{
		EnsureChunkFrameworkState(Coord, Chunk);
		ChunkState = FindProceduralChunkState(Coord);
	}

	if (!ChunkState || ChunkState->bPopulationGenerated)
	{
		return;
	}

	const bool bAuthoredChunk = ChunkState->bIsAuthoredChunk;
	const int32 DistanceFromAuthored = ChunkState->DistanceFromAuthored;
	const float HalfSize = Chunk->WorldSize * 0.5f;

	for (const FTerraDynePopulationSpawnRule& Rule : PopulationSpawnRules)
	{
		if (Rule.RuleId == NAME_None)
		{
			continue;
		}

		if ((bAuthoredChunk && !Rule.bAllowOnAuthoredChunks) || (!bAuthoredChunk && !Rule.bAllowOnProceduralChunks))
		{
			continue;
		}

		if (DistanceFromAuthored < Rule.MinDistanceFromAuthored || DistanceFromAuthored > Rule.MaxDistanceFromAuthored)
		{
			continue;
		}

		if (Rule.RequiredBiomeTag != NAME_None &&
			Rule.RequiredBiomeTag != ChunkState->PrimaryBiomeTag &&
			!ChunkState->OverlayBiomeTags.Contains(Rule.RequiredBiomeTag))
		{
			continue;
		}

		if (Rule.Descriptor.ActorClass.IsNull() && Rule.Descriptor.StaticMesh.IsNull())
		{
			continue;
		}

		FRandomStream Stream(MakeChunkSeed(Coord) ^ GetTypeHash(Rule.RuleId));
		const int32 MinCount = FMath::Max(0, Rule.MinInstancesPerChunk);
		const int32 MaxCount = FMath::Max(MinCount, Rule.MaxInstancesPerChunk);
		const int32 SpawnCount = Stream.RandRange(MinCount, MaxCount);
		TArray<FVector2D> SpawnedLocalPoints;

		for (int32 InstanceIndex = 0; InstanceIndex < SpawnCount; ++InstanceIndex)
		{
			bool bPlaced = false;
			for (int32 Attempt = 0; Attempt < 12 && !bPlaced; ++Attempt)
			{
				const float LocalX = Stream.FRandRange(-HalfSize + Rule.ChunkPadding, HalfSize - Rule.ChunkPadding);
				const float LocalY = Stream.FRandRange(-HalfSize + Rule.ChunkPadding, HalfSize - Rule.ChunkPadding);
				const FVector2D Candidate(LocalX, LocalY);

				bool bOverlapsExisting = false;
				for (const FVector2D& ExistingPoint : SpawnedLocalPoints)
				{
					if (FVector2D::Distance(ExistingPoint, Candidate) < Rule.MinSpacing)
					{
						bOverlapsExisting = true;
						break;
					}
				}

				if (bOverlapsExisting)
				{
					continue;
				}

				const float LocalZ = Rule.Descriptor.bSnapToTerrain
					? Chunk->GetHeightAtLocation(FVector(LocalX, LocalY, 0.0f)) + Rule.Descriptor.TerrainOffset
					: Rule.Descriptor.TerrainOffset;

				FTransform WorldTransform(
					FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f),
					Chunk->GetActorTransform().TransformPosition(FVector(LocalX, LocalY, LocalZ)),
					FVector(1.0f));

				FTerraDynePopulationDescriptor Descriptor = Rule.Descriptor;
				Descriptor.Kind = Rule.Descriptor.Kind == ETerraDynePopulationKind::Prop
					? ETerraDynePopulationKind::ProceduralSpawn
					: Rule.Descriptor.Kind;
				Descriptor.BiomeTag = ChunkState->PrimaryBiomeTag;
				PlacePersistentActorFromDescriptor(Descriptor, WorldTransform, Rule.RuleId);
				SpawnedLocalPoints.Add(Candidate);
				bPlaced = true;
			}
		}
	}

	ChunkState->bPopulationGenerated = true;
}

void ATerraDyneManager::SyncPopulationForChunk(FIntPoint Coord)
{
	if (!HasAuthority())
	{
		return;
	}

	ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord);
	if (!Chunk)
	{
		return;
	}

	EnsureChunkFrameworkState(Coord, Chunk);
	GeneratePopulationForChunk(Coord, Chunk);

	for (FTerraDynePersistentPopulationEntry& Entry : PersistentPopulationEntries)
	{
		if (Entry.ChunkCoord != Coord)
		{
			continue;
		}

		if (Entry.State == ETerraDynePopulationState::Active)
		{
			if (AActor* Actor = SpawnPopulationActor(Entry))
			{
				FTransform WorldTransform = TerraDyneWorldFramework::MakeWorldTransform(Entry.LocalTransform, GetActorTransform());
				if (Entry.Descriptor.bSnapToTerrain)
				{
					FVector WorldLocation = WorldTransform.GetLocation();
					const FVector ChunkLocal = WorldLocation - Chunk->GetActorLocation();
					WorldLocation.Z = Chunk->GetActorLocation().Z +
						Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y, 0.0f)) +
						Entry.Descriptor.TerrainOffset;
					WorldTransform.SetLocation(WorldLocation);
				}

				PreparePopulationActorForRuntimePlacement(Actor);
				Actor->SetActorTransform(WorldTransform);
			}
		}
		else if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(Entry.PopulationId))
		{
			if (ExistingActor->IsValid())
			{
				PopulationDestroyInProgress.Add(Entry.PopulationId);
				ExistingActor->Get()->Destroy();
			}
		}
	}
}

void ATerraDyneManager::DespawnPopulationForChunk(FIntPoint Coord)
{
	if (!HasAuthority())
	{
		return;
	}

	for (const FTerraDynePersistentPopulationEntry& Entry : PersistentPopulationEntries)
	{
		if (Entry.ChunkCoord != Coord)
		{
			continue;
		}

		if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(Entry.PopulationId))
		{
			if (ExistingActor->IsValid())
			{
				PopulationDestroyInProgress.Add(Entry.PopulationId);
				ExistingActor->Get()->Destroy();
			}
		}
	}
}

void ATerraDyneManager::HandlePopulationActorDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor)
	{
		return;
	}

	const FGuid* PopulationId = PopulationIdByActor.Find(DestroyedActor);
	if (!PopulationId)
	{
		return;
	}

	const FGuid PopulationGuid = *PopulationId;
	const FBox DirtyBounds = DestroyedActor->GetComponentsBoundingBox(true);
	PopulationIdByActor.Remove(DestroyedActor);
	PopulationActorById.Remove(PopulationGuid);

	if (PopulationDestroyInProgress.Remove(PopulationGuid) > 0)
	{
		return;
	}

	const int32* EntryIndex = PopulationEntryIndexById.Find(PopulationGuid);
	if (!EntryIndex || !PersistentPopulationEntries.IsValidIndex(*EntryIndex))
	{
		return;
	}

	FTerraDynePersistentPopulationEntry& Entry = PersistentPopulationEntries[*EntryIndex];
	if (Entry.Descriptor.bAllowRegrowth && Entry.Descriptor.RegrowthDelaySeconds > 0.0f)
	{
		Entry.State = ETerraDynePopulationState::Regrowing;
		Entry.RemainingRegrowthTimeSeconds = Entry.Descriptor.RegrowthDelaySeconds;
	}
	else
	{
		Entry.State = Entry.Descriptor.Kind == ETerraDynePopulationKind::Harvestable
			? ETerraDynePopulationState::Harvested
			: ETerraDynePopulationState::Destroyed;
		Entry.RemainingRegrowthTimeSeconds = 0.0f;
	}

	BroadcastPopulationChanged(Entry, FName(TEXT("Destroyed")));
	RefreshNavigationForBounds(DirtyBounds, true);
}

void ATerraDyneManager::TickPopulationState(float DeltaTime)
{
	TArray<FIntPoint> ChunksToSync;

	for (FTerraDynePersistentPopulationEntry& Entry : PersistentPopulationEntries)
	{
		if (Entry.State != ETerraDynePopulationState::Regrowing)
		{
			continue;
		}

		Entry.RemainingRegrowthTimeSeconds = FMath::Max(0.0f, Entry.RemainingRegrowthTimeSeconds - DeltaTime);
		if (Entry.RemainingRegrowthTimeSeconds > 0.0f)
		{
			continue;
		}

		Entry.State = ETerraDynePopulationState::Active;
		Entry.RemainingRegrowthTimeSeconds = 0.0f;
		ChunksToSync.AddUnique(Entry.ChunkCoord);
		BroadcastPopulationChanged(Entry, FName(TEXT("Regrown")));
	}

	for (const FIntPoint& Coord : ChunksToSync)
	{
		SyncPopulationForChunk(Coord);
	}
}

void ATerraDyneManager::RefreshPopulationForLoadedChunks()
{
	if (!HasAuthority())
	{
		return;
	}

	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			SyncPopulationForChunk(Pair.Key);
		}
	}
}

FName ATerraDyneManager::GetBiomeTagAtLocation(FVector WorldLocation) const
{
	const FIntPoint Coord = WorldToChunkCoord(WorldLocation);
	if (const FTerraDyneProceduralChunkState* ChunkState = FindProceduralChunkState(Coord))
	{
		return ChunkState->PrimaryBiomeTag;
	}

	return ResolveBiomeForChunk(Coord, GetChunkAtCoord(Coord));
}

TArray<FTerraDyneAISpawnZone> ATerraDyneManager::GetAISpawnZonesAtLocation(FVector WorldLocation) const
{
	TArray<FTerraDyneAISpawnZone> MatchingZones;
	const FVector LocalLocation = WorldLocation - GetActorLocation();
	const FIntPoint Coord = WorldToChunkCoord(WorldLocation);
	const bool bProceduralChunk = !ImportedChunkCoords.Contains(Coord);
	const FName BiomeTag = GetBiomeTagAtLocation(WorldLocation);

	for (const FTerraDyneAISpawnZone& Zone : AISpawnZones)
	{
		if (!Zone.bEnabled || !Zone.LocalBounds.IsInsideOrOn(LocalLocation))
		{
			continue;
		}

		if (Zone.bProceduralOnly && !bProceduralChunk)
		{
			continue;
		}

		if (Zone.RequiredBiomeTag != NAME_None && Zone.RequiredBiomeTag != BiomeTag)
		{
			continue;
		}

		MatchingZones.Add(Zone);
	}

	return MatchingZones;
}

float ATerraDyneManager::GetSlopeDegreesAtLocation(FVector WorldLocation) const
{
	const ATerraDyneChunk* Chunk = GetChunkAtLocation(WorldLocation);
	if (!Chunk)
	{
		return 0.0f;
	}

	const float SampleOffset = FMath::Max(100.0f, Chunk->WorldSize / FMath::Max(2, Chunk->Resolution - 1));
	const FVector ChunkLocal = WorldLocation - Chunk->GetActorLocation();
	const float HX0 = Chunk->GetHeightAtLocation(FVector(ChunkLocal.X - SampleOffset, ChunkLocal.Y, 0.0f));
	const float HX1 = Chunk->GetHeightAtLocation(FVector(ChunkLocal.X + SampleOffset, ChunkLocal.Y, 0.0f));
	const float HY0 = Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y - SampleOffset, 0.0f));
	const float HY1 = Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y + SampleOffset, 0.0f));
	const float DX = (HX1 - HX0) / (2.0f * SampleOffset);
	const float DY = (HY1 - HY0) / (2.0f * SampleOffset);
	return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY)));
}

bool ATerraDyneManager::CanBuildAtLocation(FVector WorldLocation, FString& OutReason) const
{
	OutReason.Reset();
	const FVector LocalLocation = WorldLocation - GetActorLocation();
	const float SlopeDegrees = GetSlopeDegreesAtLocation(WorldLocation);

	for (const FTerraDyneBuildPermissionZone& Zone : BuildPermissionZones)
	{
		if (!Zone.LocalBounds.IsInsideOrOn(LocalLocation))
		{
			continue;
		}

		if (SlopeDegrees > Zone.MaxSlopeDegrees)
		{
			OutReason = Zone.Reason.IsEmpty()
				? FString::Printf(TEXT("Slope %.1f exceeds %.1f degrees in zone %s."), SlopeDegrees, Zone.MaxSlopeDegrees, *Zone.ZoneId.ToString())
				: Zone.Reason;
			return false;
		}

		if (Zone.Permission == ETerraDyneBuildPermission::Blocked)
		{
			OutReason = Zone.Reason.IsEmpty()
				? FString::Printf(TEXT("Blocked by build zone %s."), *Zone.ZoneId.ToString())
				: Zone.Reason;
			return false;
		}

		if (Zone.Permission == ETerraDyneBuildPermission::Allowed)
		{
			continue;
		}
	}

	return true;
}

TArray<FTerraDynePCGPoint> ATerraDyneManager::GetPCGSeedPointsForChunk(
	FIntPoint Coord,
	bool bIncludePopulation,
	bool bIncludeAISpawnZones) const
{
	TArray<FTerraDynePCGPoint> Points;
	const FName BiomeTag = GetBiomeTagAtLocation(
		GetActorLocation() + FVector(Coord.X * GlobalChunkSize, Coord.Y * GlobalChunkSize, 0.0f));
	const ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord);
	const FBox ChunkBounds = Chunk
		? Chunk->GetWorldBounds()
		: TerraDyneWorldFramework::MakeChunkBounds(*this, Coord, GlobalChunkSize);

	if (bIncludePopulation)
	{
		for (const FTerraDynePersistentPopulationEntry& Entry : PersistentPopulationEntries)
		{
			if (Entry.ChunkCoord != Coord || Entry.State != ETerraDynePopulationState::Active)
			{
				continue;
			}

			FTerraDynePCGPoint Point;
			Point.Transform = TerraDyneWorldFramework::MakeWorldTransform(Entry.LocalTransform, GetActorTransform());
			Point.BiomeTag = Entry.CachedBiomeTag != NAME_None ? Entry.CachedBiomeTag : BiomeTag;
			Point.SourceId = Entry.Descriptor.TypeId;
			Point.PopulationKind = Entry.Descriptor.Kind;
			Points.Add(Point);
		}
	}

	if (bIncludeAISpawnZones)
	{
		for (const FTerraDyneAISpawnZone& Zone : AISpawnZones)
		{
			const FBox WorldBounds = Zone.LocalBounds.ShiftBy(GetActorLocation());
			if (!WorldBounds.Intersect(ChunkBounds))
			{
				continue;
			}

			FTerraDynePCGPoint Point;
			Point.Transform = FTransform(WorldBounds.GetCenter());
			Point.BiomeTag = Zone.RequiredBiomeTag != NAME_None ? Zone.RequiredBiomeTag : BiomeTag;
			Point.SourceId = Zone.ZoneId;
			Point.PopulationKind = ETerraDynePopulationKind::ProceduralSpawn;
			Point.Density = Zone.MaxConcurrentSpawns;
			Points.Add(Point);
		}
	}

	return Points;
}

void ATerraDyneManager::BroadcastTerrainChanged(const TArray<FIntPoint>& ChangedCoords)
{
	for (const FIntPoint& Coord : ChangedCoords)
	{
		FTerraDyneTerrainChangeEvent Event;
		Event.ChunkCoord = Coord;
		if (const ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord))
		{
			Event.WorldBounds = Chunk->GetWorldBounds();
		}
		else
		{
			Event.WorldBounds = TerraDyneWorldFramework::MakeChunkBounds(*this, Coord, GlobalChunkSize);
		}
		Event.bAffectsNavigation = bRefreshNavMeshOnTerrainChanges;
		OnTerrainChanged.Broadcast(Event);
		RefreshNavigationForBounds(Event.WorldBounds, false);
	}
}

void ATerraDyneManager::BroadcastFoliageChanged(const TArray<FIntPoint>& ChangedCoords)
{
	for (const FIntPoint& Coord : ChangedCoords)
	{
		FTerraDyneFoliageChangeEvent Event;
		Event.ChunkCoord = Coord;
		if (const ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord))
		{
			Event.ImportedFoliageInstanceCount =
				Chunk->GetTransferredFoliageInstanceCount() +
				Chunk->GetTransferredActorFoliageInstanceCount();
		}
		Event.bGrassRegenerationQueued = true;
		OnFoliageChanged.Broadcast(Event);
	}
}

void ATerraDyneManager::BroadcastPopulationChanged(
	const FTerraDynePersistentPopulationEntry& Entry,
	FName Reason)
{
	FTerraDynePopulationChangeEvent Event;
	Event.PopulationId = Entry.PopulationId;
	Event.TypeId = Entry.Descriptor.TypeId;
	Event.Kind = Entry.Descriptor.Kind;
	Event.State = Entry.State;
	Event.Reason = Reason;
	OnPopulationChanged.Broadcast(Event);
}

void ATerraDyneManager::RefreshNavigationForBounds(const FBox& Bounds, bool bPopulationChange)
{
	if (!HasAuthority() || !GetWorld() || !Bounds.IsValid)
	{
		return;
	}

	if ((bPopulationChange && !bRefreshNavMeshOnPopulationChanges) ||
		(!bPopulationChange && !bRefreshNavMeshOnTerrainChanges))
	{
		return;
	}

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const FBox ExpandedBounds = Bounds.ExpandBy(FMath::Max(0.0f, NavigationDirtyBoundsPadding));
	bool bMerged = false;
	for (FBox& Existing : PendingNavigationDirtyAreas)
	{
		if (Existing.ExpandBy(NavigationDirtyBoundsPadding).Intersect(ExpandedBounds))
		{
			Existing += ExpandedBounds;
			bMerged = true;
			break;
		}
	}
	if (!bMerged)
	{
		PendingNavigationDirtyAreas.Add(ExpandedBounds);
	}

	const int32 MaxAreas = Settings ? FMath::Max(1, Settings->MaxPendingNavigationDirtyAreas) : 32;
	const float Debounce = Settings ? FMath::Max(0.0f, Settings->NavigationDirtyDebounceTime) : 0.5f;
	if (PendingNavigationDirtyAreas.Num() >= MaxAreas || Debounce <= 0.0f)
	{
		FlushPendingNavigationDirtyAreas();
	}
	else if (NavigationDirtyTimer <= 0.0f)
	{
		NavigationDirtyTimer = Debounce;
	}
}

void ATerraDyneManager::FlushPendingNavigationDirtyAreas()
{
	if (!HasAuthority() || !GetWorld() || PendingNavigationDirtyAreas.Num() == 0)
	{
		return;
	}
	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		for (const FBox& DirtyBounds : PendingNavigationDirtyAreas)
		{
			if (DirtyBounds.IsValid)
			{
				NavSystem->AddDirtyArea(DirtyBounds, ENavigationDirtyFlag::All, FName(TEXT("TerraDyne")));
			}
	}
	}
	PendingNavigationDirtyAreas.Reset();
	NavigationDirtyTimer = 0.0f;
}

void ATerraDyneManager::Multicast_SyncChunkState(const FTerraDyneChunkData& Data)
{
	if (HasAuthority())
	{
		BroadcastChunkStateToRelevantClients(Data);
		return;
	}

	ATerraDyneChunk* Chunk = GetChunkAtCoord(Data.Coordinate);
	if (!Chunk) return;

	Chunk->LoadFromData(Data);
	TArray<FIntPoint> ChangedCoords;
	ChangedCoords.Add(Data.Coordinate);
	BroadcastTerrainChanged(ChangedCoords);
	BroadcastFoliageChanged(ChangedCoords);
}

void ATerraDyneManager::CleanupPlayerStacks(APlayerController* Controller)
{
	UndoStacks.Remove(Controller);
	RedoStacks.Remove(Controller);
	PendingStrokes.Remove(Controller);
}

void ATerraDyneManager::SendFullSyncToController(ATerraDyneEditController* Controller)
{
	if (!HasAuthority() || !Controller)
	{
		return;
	}

	UTerraDyneReplicationComponent* ReplicationComponent = Controller->TerraDyneReplication;
	if (!ReplicationComponent)
	{
		ReplicationComponent = Controller->FindComponentByClass<UTerraDyneReplicationComponent>();
	}
	SendFullSyncToReplicationComponent(ReplicationComponent);
}

bool ATerraDyneManager::SendChunkStateToReplicationComponent(
	UTerraDyneReplicationComponent* ReplicationComponent,
	const FTerraDyneChunkData& Data)
{
	return HasAuthority() && ReplicationComponent && ReplicationComponent->SendChunkState(Data);
}

bool ATerraDyneManager::IsControllerRelevantToLocation(
	const APlayerController* Controller,
	FVector Location) const
{
	if (!Controller)
	{
		return false;
	}
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const float Radius = Settings ? Settings->TerrainReplicationRadius : 150000.0f;
	if (Radius <= 0.0f)
	{
		return true;
	}

	const AActor* ReferenceActor = Controller->GetPawn();
	if (!ReferenceActor)
	{
		ReferenceActor = Controller->GetViewTarget();
	}
	return ReferenceActor &&
		FVector::DistSquared(ReferenceActor->GetActorLocation(), Location) <= FMath::Square(Radius);
}

void ATerraDyneManager::SendFullSyncToReplicationComponent(
	UTerraDyneReplicationComponent* ReplicationComponent)
{
	if (!HasAuthority() || !ReplicationComponent)
	{
		return;
	}

	APlayerController* Controller = ReplicationComponent->GetOwningPlayerController();
	if (!Controller)
	{
		return;
	}

	const AActor* ReferenceActor = Controller->GetPawn();
	if (!ReferenceActor)
	{
		ReferenceActor = Controller->GetViewTarget();
	}
	const FVector ReferenceLocation = ReferenceActor ? ReferenceActor->GetActorLocation() : GetActorLocation();

	TArray<ATerraDyneChunk*> RelevantChunks;
	for (const auto& Pair : ActiveChunkMap)
	{
		ATerraDyneChunk* Chunk = Pair.Value;
		if (Chunk && IsControllerRelevantToLocation(Controller, Chunk->GetActorLocation()))
		{
			RelevantChunks.Add(Chunk);
		}
	}
	RelevantChunks.Sort([ReferenceLocation](const ATerraDyneChunk& A, const ATerraDyneChunk& B)
	{
		return FVector::DistSquared(ReferenceLocation, A.GetActorLocation()) <
			FVector::DistSquared(ReferenceLocation, B.GetActorLocation());
	});

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxChunks = Settings ? FMath::Max(1, Settings->MaxFullSyncChunksPerConnection) : 128;
	const int32 ChunksToSend = FMath::Min(MaxChunks, RelevantChunks.Num());
	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyneManager: queueing %d/%d relevant chunks for %s."),
		ChunksToSend, RelevantChunks.Num(), *GetNameSafe(Controller));

	for (int32 Index = 0; Index < ChunksToSend; ++Index)
	{
		ReplicationComponent->SendChunkState(RelevantChunks[Index]->GetSerializedData());
	}
}

void ATerraDyneManager::BroadcastChunkStateToRelevantClients(const FTerraDyneChunkData& Data)
{
	ATerraDyneChunk* Chunk = GetChunkAtCoord(Data.Coordinate);
	const FVector Location = Chunk ? Chunk->GetActorLocation() : GetActorLocation();
	if (!GetWorld())
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		if (!Controller || Controller->IsLocalController() || !IsControllerRelevantToLocation(Controller, Location))
		{
			continue;
		}
		if (UTerraDyneReplicationComponent* Component =
			Controller->FindComponentByClass<UTerraDyneReplicationComponent>())
		{
			Component->SendChunkState(Data);
		}
	}
}

void ATerraDyneManager::Multicast_ApplyBrush(const FTerraDyneBrushParams& Params)
{
	if (!HasAuthority())
	{
		ApplyGlobalBrush(
			Params.WorldLocation,
			Params.Radius,
			Params.Strength,
			Params.BrushMode,
			Params.TargetLayer,
			Params.WeightLayerIndex,
			Params.FlattenHeight);
		return;
	}

	if (!GetWorld())
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		if (!Controller || Controller->IsLocalController() ||
			!IsControllerRelevantToLocation(Controller, Params.WorldLocation))
		{
			continue;
		}
		if (UTerraDyneReplicationComponent* Component =
			Controller->FindComponentByClass<UTerraDyneReplicationComponent>())
		{
			Component->Client_ApplyBrush(Params);
		}
	}
}

void ATerraDyneManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyTrackedPopulationActors();

	// [C-5] Unregister so the subsystem does not hold a stale pointer after destruction
	if (UWorld* World = GetWorld())
	{
		if (UTerraDyneSubsystem* Sys = World->GetSubsystem<UTerraDyneSubsystem>())
		{
			Sys->UnregisterManager(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ---- Streaming: Helpers ----

FIntPoint ATerraDyneManager::WorldToChunkCoord(const FVector& WorldPos) const
{
	const FVector LocalPos = WorldPos - GetActorLocation();
	return FIntPoint(
		TerraDyneWorldFramework::ChunkIndexFromLocalAxis(LocalPos.X, GlobalChunkSize),
		TerraDyneWorldFramework::ChunkIndexFromLocalAxis(LocalPos.Y, GlobalChunkSize)
	);
}

FString ATerraDyneManager::GetChunkCachePath(FIntPoint Coord) const
{
	FString Dir = TEXT("TerraDyne/ChunkCache");
	if (const UTerraDyneSettings* S = GetDefault<UTerraDyneSettings>())
	{
		Dir = S->ChunkSaveDir;
	}
	FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Dir);
	return FPaths::Combine(SaveDir, FString::Printf(TEXT("%d_%d.chunk"), Coord.X, Coord.Y));
}

void ATerraDyneManager::SaveChunkToCache(FIntPoint Coord, const FTerraDyneChunkData& Data)
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxUncompressed = Settings
		? Settings->MaxUncompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxUncompressedBytes;
	const int32 MaxPacket = Settings
		? Settings->MaxCompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxPacketBytes;
	const bool bAsyncWrite = !Settings || Settings->bUseAsyncChunkCacheWrites;
	const FString Path = GetChunkCachePath(Coord);

	PendingChunkCacheWrites.Add(Coord, Data);
	const uint64 Generation = NextChunkCacheWriteGeneration++;
	ChunkCacheWriteGenerations.Add(Coord, Generation);
	TWeakObjectPtr<ATerraDyneManager> WeakThis(this);
	auto Completion = [WeakThis, Coord, Generation](bool bSuccess, const FString& Error)
	{
		if (!WeakThis.IsValid() || WeakThis->ChunkCacheWriteGenerations.FindRef(Coord) != Generation)
		{
			return;
		}

		if (bSuccess)
		{
			WeakThis->PendingChunkCacheWrites.Remove(Coord);
			WeakThis->ChunkCacheRetryAfterSeconds.Remove(Coord);
			UE_LOG(LogTerraDyne, Verbose, TEXT("Streaming: atomically cached chunk [%d,%d]."), Coord.X, Coord.Y);
			return;
		}

		const double Now = WeakThis->GetWorld() ? WeakThis->GetWorld()->GetTimeSeconds() : 0.0;
		WeakThis->ChunkCacheRetryAfterSeconds.Add(Coord, Now + 5.0);
		UE_LOG(LogTerraDyne, Warning, TEXT("Streaming: cache write failed for chunk [%d,%d]: %s"),
			Coord.X, Coord.Y, *Error);
		if (UTerraDyneSubsystem* Sys = WeakThis->GetWorld()
			? WeakThis->GetWorld()->GetSubsystem<UTerraDyneSubsystem>()
			: nullptr)
		{
			Sys->ShowNotification(
				FText::Format(NSLOCTEXT("TerraDyne", "CacheFail", "Failed to cache chunk [{0},{1}]; retry scheduled"),
					FText::AsNumber(Coord.X), FText::AsNumber(Coord.Y)),
				ETerraDyneNotifySeverity::Warning);
		}
	};

	if (bAsyncWrite)
	{
		FOnTerraDyneAsyncSaveComplete Delegate;
		Delegate.BindLambda(Completion);
		FTerraDyneAsyncSaver::SaveChunkStateAtomically(
			Path, Data, MaxUncompressed, MaxPacket, MoveTemp(Delegate));
		return;
	}

	TArray<uint8> Packet;
	FString Error;
	bool bSuccess = FTerraDyneStateCodec::EncodeChunk(Data, Packet, &Error, MaxUncompressed);
	if (bSuccess && Packet.Num() > MaxPacket)
	{
		Error = FString::Printf(TEXT("Encoded chunk packet size %d exceeds the %d-byte cache limit."),
			Packet.Num(), MaxPacket);
		bSuccess = false;
	}
	if (bSuccess)
	{
		bSuccess = FTerraDyneAsyncSaver::SaveBytesAtomicallySync(Path, Packet, Error);
	}
	Completion(bSuccess, Error);
}

bool ATerraDyneManager::LoadChunkFromCache(FIntPoint Coord, FTerraDyneChunkData& OutData)
{
	if (const FTerraDyneChunkData* PendingData = PendingChunkCacheWrites.Find(Coord))
	{
		OutData = *PendingData;
		return true;
	}

	FString Path = GetChunkCachePath(Coord);
	if (!IFileManager::Get().FileExists(*Path))
	{
		return false;
	}
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxPacket = Settings
		? Settings->MaxCompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxPacketBytes;
	const int32 MaxUncompressed = Settings
		? Settings->MaxUncompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxUncompressedBytes;
	const int64 FileSize = IFileManager::Get().FileSize(*Path);
	if (FileSize <= 0 || FileSize > MaxPacket)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("Streaming: rejected chunk cache [%d,%d] with size %lld."),
			Coord.X, Coord.Y, FileSize);
		return false;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Path))
	{
		return false;
	}

	FString Error;
	const bool bVersioned = FTerraDyneStateCodec::IsVersionedPacket(FileData);
	const bool bDecoded = bVersioned
		? FTerraDyneStateCodec::DecodeChunk(FileData, OutData, &Error, MaxPacket, MaxUncompressed)
		: FTerraDyneStateCodec::DecodeLegacyCache(FileData, OutData, &Error, MaxUncompressed);
	if (!bDecoded || OutData.Coordinate != Coord)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("Streaming: rejected cache for chunk [%d,%d]: %s"),
			Coord.X, Coord.Y, bDecoded ? TEXT("coordinate mismatch") : *Error);
		return false;
	}

	if (!bVersioned)
	{
		UE_LOG(LogTerraDyne, Log, TEXT("Streaming: migrating legacy cache for chunk [%d,%d] to the versioned format."),
			Coord.X, Coord.Y);
		SaveChunkToCache(Coord, OutData);
	}
	return true;
}

void ATerraDyneManager::MarkChunkDirty(FIntPoint Coord)
{
	DirtyChunkSet.Add(Coord);
}

ATerraDyneChunk* ATerraDyneManager::SpawnConfiguredChunk(FIntPoint Coord, int32 OverrideResolution)
{
	if (!GetWorld())
	{
		return nullptr;
	}

	TSubclassOf<ATerraDyneChunk> ClassToSpawn = ChunkClass;
	if (!ClassToSpawn)
	{
		ClassToSpawn = ATerraDyneChunk::StaticClass();
	}
	const FVector Location = GetActorLocation() + FVector(Coord.X * GlobalChunkSize, Coord.Y * GlobalChunkSize, 0.0f);

	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;

	ATerraDyneChunk* Chunk = GetWorld()->SpawnActor<ATerraDyneChunk>(ClassToSpawn, FTransform(Location), Params);
	if (!Chunk)
	{
		return nullptr;
	}

	Chunk->GridCoordinate = Coord;
	Chunk->ChunkSizeWorldUnits = GlobalChunkSize;
	Chunk->BrushMaterialBase = HeightBrushMaterial;
	Chunk->WorldSize = GlobalChunkSize;
	Chunk->ProceduralSeed = MakeChunkSeed(Coord);
	Chunk->bIsAuthoredChunk = ImportedChunkCoords.Contains(Coord);

	if (OverrideResolution != INDEX_NONE)
	{
		Chunk->Resolution = FMath::Clamp(OverrideResolution, 32, 256);
	}
	else if (const UTerraDyneSettings* S = GetDefault<UTerraDyneSettings>())
	{
		Chunk->Resolution = FMath::Clamp(S->DefaultResolution, 32, 256);
	}
	else
	{
		Chunk->Resolution = 64;
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyneSettings CDO not found; using default Resolution=64"));
	}

	Chunk->FinishSpawning(FTransform(Location));
	Chunk->SetMaterial(MasterMaterial);
	if (ActiveGrassProfile)
	{
		Chunk->SetGrassProfile(ActiveGrassProfile);
	}

	ActiveChunkMap.Add(Coord, Chunk);
	EnsureChunkFrameworkState(Coord, Chunk);
	return Chunk;
}

// ---- Streaming: Load/Unload ----

void ATerraDyneManager::LoadOrSpawnChunk(FIntPoint Coord)
{
	if (ActiveChunkMap.Contains(Coord)) return;
	if (!ShouldAllowChunkCoord(Coord)) return;
	ATerraDyneChunk* Chunk = SpawnConfiguredChunk(Coord);
	if (!Chunk) return;

	// Try loading cached data (previously sculpted/painted chunk)
	FTerraDyneChunkData CachedData;
	if (LoadChunkFromCache(Coord, CachedData))
	{
		Chunk->LoadFromData(CachedData);
		UE_LOG(LogTerraDyne, Verbose, TEXT("Streaming: Loaded chunk [%d,%d] from cache"), Coord.X, Coord.Y);
	}
	else
	{
		UE_LOG(LogTerraDyne, Verbose, TEXT("Streaming: Spawned fresh chunk [%d,%d]"), Coord.X, Coord.Y);
	}

	EnsureChunkFrameworkState(Coord, Chunk);
	SyncPopulationForChunk(Coord);
	PendingLoadQueue.Remove(Coord);
}

void ATerraDyneManager::UnloadChunk(FIntPoint Coord)
{
	ATerraDyneChunk** Found = ActiveChunkMap.Find(Coord);
	if (!Found || !(*Found)) return;

	ATerraDyneChunk* Chunk = *Found;
	DespawnPopulationForChunk(Coord);

	// Persist dirty chunks
	if (DirtyChunkSet.Contains(Coord))
	{
		FTerraDyneChunkData Data = Chunk->GetSerializedData();
		SaveChunkToCache(Coord, Data);
		DirtyChunkSet.Remove(Coord);
		UE_LOG(LogTerraDyne, Verbose, TEXT("Streaming: Saved dirty chunk [%d,%d] to cache"), Coord.X, Coord.Y);
	}

	ActiveChunkMap.Remove(Coord);
	Chunk->Destroy();
	PendingUnloadQueue.Remove(Coord);

	UE_LOG(LogTerraDyne, Verbose, TEXT("Streaming: Unloaded chunk [%d,%d]"), Coord.X, Coord.Y);
}

// ---- Streaming: Ring Update ----

void ATerraDyneManager::UpdateStreaming(const TArray<FVector>& PlayerPositions)
{
	if (bStreamingPaused || PlayerPositions.Num() == 0) return;

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	if (!Settings) return;

	const int32 LoadRadius = Settings->ChunkLoadRadius;
	const int32 UnloadRadius = Settings->ChunkUnloadRadius;

	// Compute a hash of all player chunk coordinates to detect movement
	uint32 CurrentHash = 0;
	TArray<FIntPoint> PlayerChunks;
	for (const FVector& Pos : PlayerPositions)
	{
		FIntPoint PC = WorldToChunkCoord(Pos);
		PlayerChunks.Add(PC);
		CurrentHash = HashCombine(CurrentHash, GetTypeHash(PC));
	}

	// Only rescan when any player moves cells
	if (CurrentHash != LastStreamingHash)
	{
		LastStreamingHash = CurrentHash;

		// Build union of load regions across all players
		for (const FIntPoint& PlayerChunk : PlayerChunks)
		{
			for (int32 DX = -LoadRadius; DX <= LoadRadius; DX++)
			{
				for (int32 DY = -LoadRadius; DY <= LoadRadius; DY++)
				{
					// Diamond shape: Manhattan distance
					if (FMath::Abs(DX) + FMath::Abs(DY) > LoadRadius) continue;

					FIntPoint Coord(PlayerChunk.X + DX, PlayerChunk.Y + DY);
					if (!ShouldAllowChunkCoord(Coord))
					{
						continue;
					}

					if (!ActiveChunkMap.Contains(Coord) && !PendingLoadQueue.Contains(Coord))
					{
						PendingLoadQueue.Add(Coord);
					}
				}
			}
		}

		// Queue chunks outside ALL players' unload radius
		TArray<FIntPoint> ToUnload;
		for (const auto& Pair : ActiveChunkMap)
		{
			bool bInAnyPlayerRange = false;
			for (const FIntPoint& PlayerChunk : PlayerChunks)
			{
				int32 Dist = FMath::Abs(Pair.Key.X - PlayerChunk.X) + FMath::Abs(Pair.Key.Y - PlayerChunk.Y);
				if (Dist <= UnloadRadius)
				{
					bInAnyPlayerRange = true;
					break;
				}
			}
			if (!bInAnyPlayerRange && !PendingUnloadQueue.Contains(Pair.Key))
			{
				ToUnload.Add(Pair.Key);
			}
		}
		for (const FIntPoint& Coord : ToUnload)
		{
			PendingUnloadQueue.Add(Coord);
		}
	}

	ProcessStreamingQueues(PlayerPositions);
}

void ATerraDyneManager::ProcessStreamingQueues(const TArray<FVector>& PlayerPositions)
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxOps = Settings ? Settings->MaxChunkOpsPerTick : 2;
	int32 Ops = 0;

	// Pre-compute player chunk coords for sorting
	TArray<FIntPoint> PlayerChunks;
	for (const FVector& Pos : PlayerPositions)
	{
		PlayerChunks.Add(WorldToChunkCoord(Pos));
	}

	// Helper: minimum Manhattan distance from a chunk to any player
	auto MinDistToAnyPlayer = [&PlayerChunks](const FIntPoint& Coord) -> int32
	{
		int32 MinDist = TNumericLimits<int32>::Max();
		for (const FIntPoint& PC : PlayerChunks)
		{
			int32 D = FMath::Abs(Coord.X - PC.X) + FMath::Abs(Coord.Y - PC.Y);
			MinDist = FMath::Min(MinDist, D);
		}
		return MinDist;
	};

	// Unloads first (free memory before allocating) — farthest from any player first
	if (PendingUnloadQueue.Num() > 0 && Ops < MaxOps)
	{
		TArray<FIntPoint> UnloadList = PendingUnloadQueue.Array();
		UnloadList.Sort([&MinDistToAnyPlayer](const FIntPoint& A, const FIntPoint& B)
		{
			return MinDistToAnyPlayer(A) > MinDistToAnyPlayer(B);
		});

		for (const FIntPoint& Coord : UnloadList)
		{
			if (Ops >= MaxOps) break;
			UnloadChunk(Coord);
			Ops++;
		}
	}

	// Loads next — nearest to any player first
	if (PendingLoadQueue.Num() > 0 && Ops < MaxOps)
	{
		TArray<FIntPoint> LoadList = PendingLoadQueue.Array();
		LoadList.Sort([&MinDistToAnyPlayer](const FIntPoint& A, const FIntPoint& B)
		{
			return MinDistToAnyPlayer(A) < MinDistToAnyPlayer(B);
		});

		for (const FIntPoint& Coord : LoadList)
		{
			if (Ops >= MaxOps) break;
			LoadOrSpawnChunk(Coord);
			Ops++;
		}
	}
}

#if WITH_EDITOR
void ATerraDyneManager::MigrateLandscapeProject()
{
	ImportFromLandscapeWithOptions(TargetLandscapeSource, LandscapeMigrationOptions);
}

void ATerraDyneManager::ResampleLandscapeData(ATerraDyneChunk* Chunk, ULandscapeComponent* SourceComponent, bool bImportWeightLayers)
{
	if (!Chunk || !SourceComponent)
	{
		return;
	}

	ALandscapeProxy* Proxy = SourceComponent->GetLandscapeProxy();
	ULandscapeInfo* Info = Proxy ? Proxy->GetLandscapeInfo() : nullptr;
	if (!Info)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne Import: LandscapeInfo unavailable for component %s."), *SourceComponent->GetName());
		return;
	}

	const int32 Quads = SourceComponent->ComponentSizeQuads;
	const int32 Resolution = Quads + 1;
	const int32 NumSamples = Resolution * Resolution;
	if (Resolution < 2 || NumSamples <= 0)
	{
		return;
	}

	const int32 X1 = SourceComponent->SectionBaseX;
	const int32 Y1 = SourceComponent->SectionBaseY;
	const int32 X2 = X1 + Quads;
	const int32 Y2 = Y1 + Quads;

	FLandscapeEditDataInterface EditData(Info);

	TArray<uint16> HeightSamples;
	HeightSamples.SetNumZeroed(NumSamples);
	EditData.GetHeightDataFast(X1, Y1, X2, Y2, HeightSamples.GetData(), 0);

	Chunk->Resolution = Resolution;
	Chunk->BaseBuffer.SetNumZeroed(NumSamples);
	Chunk->SculptBuffer.SetNumZeroed(NumSamples);
	Chunk->DetailBuffer.SetNumZeroed(NumSamples);
	Chunk->HeightBuffer.SetNumZeroed(NumSamples);
	for (int32 Layer = 0; Layer < ATerraDyneChunk::NumWeightLayers; Layer++)
	{
		Chunk->WeightBuffers[Layer].SetNumZeroed(NumSamples);
	}

	for (int32 Index = 0; Index < NumSamples; Index++)
	{
		const float NormalizedHeight = static_cast<float>(HeightSamples[Index]) / 65535.0f;
		Chunk->BaseBuffer[Index] = NormalizedHeight;
		Chunk->HeightBuffer[Index] = NormalizedHeight;
	}

	if (bImportWeightLayers && LandscapeMigrationState.LayerMappings.Num() > 0)
	{
		TMap<FName, ULandscapeLayerInfoObject*> LayerInfoMap;
		for (const FWeightmapLayerAllocationInfo& Allocation : SourceComponent->GetWeightmapLayerAllocations())
		{
			if (Allocation.LayerInfo)
			{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				const FName LayerName = Allocation.LayerInfo->GetLayerName() != NAME_None
					? Allocation.LayerInfo->GetLayerName()
					: Allocation.LayerInfo->GetFName();
#else
				const FName LayerName = Allocation.LayerInfo->LayerName != NAME_None
					? Allocation.LayerInfo->LayerName
					: Allocation.LayerInfo->GetFName();
#endif
				LayerInfoMap.Add(LayerName, Allocation.LayerInfo);
			}
		}

		for (const FTerraDyneLandscapeLayerMapping& Mapping : LandscapeMigrationState.LayerMappings)
		{
			if (Mapping.TerraDyneWeightLayerIndex < 0 ||
				Mapping.TerraDyneWeightLayerIndex >= ATerraDyneChunk::NumWeightLayers)
			{
				continue;
			}

			if (ULandscapeLayerInfoObject** LayerInfo = LayerInfoMap.Find(Mapping.SourceLayerName))
			{
				TArray<uint8> WeightSamples;
				WeightSamples.SetNumZeroed(NumSamples);
				EditData.GetWeightDataFast(*LayerInfo, X1, Y1, X2, Y2, WeightSamples.GetData(), 0);

				for (int32 Index = 0; Index < NumSamples; Index++)
				{
					Chunk->WeightBuffers[Mapping.TerraDyneWeightLayerIndex][Index] = WeightSamples[Index] / 255.0f;
				}
			}
		}
	}

	Chunk->UploadWeightTexture();
	Chunk->RebuildPhysicsMesh();
}

void ATerraDyneManager::ImportFromLandscapeWithOptions(ALandscapeProxy* TargetLandscape, const FTerraDyneLandscapeMigrationOptions& Options)
{
	if (!TargetLandscape || !GetWorld())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne Import: Invalid landscape source."));
		if (UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
		{
			Sys->ShowNotification(
				NSLOCTEXT("TerraDyne", "ImportNull", "Import failed: no landscape source selected."),
				ETerraDyneNotifySeverity::Error);
		}
		return;
	}

	ALandscapeProxy* MetadataLandscape = TargetLandscape;
	if (ALandscape* RootLandscape = TargetLandscape->GetLandscapeActor())
	{
		MetadataLandscape = RootLandscape;
	}

	TArray<ULandscapeComponent*> SourceComponents;
	if (ULandscapeInfo* LandscapeInfo = TargetLandscape->GetLandscapeInfo())
	{
		LandscapeInfo->ForAllLandscapeComponents([&SourceComponents](ULandscapeComponent* Component)
		{
			if (Component)
			{
				SourceComponents.Add(Component);
			}
		});
	}

	if (SourceComponents.Num() == 0)
	{
		for (ULandscapeComponent* Component : TargetLandscape->LandscapeComponents)
		{
			if (Component)
			{
				SourceComponents.Add(Component);
			}
		}
	}

	SourceComponents.Sort([](const ULandscapeComponent& A, const ULandscapeComponent& B)
	{
		if (A.SectionBaseX == B.SectionBaseX)
		{
			return A.SectionBaseY < B.SectionBaseY;
		}

		return A.SectionBaseX < B.SectionBaseX;
	});

	if (SourceComponents.Num() == 0)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne Import: Landscape %s has no loaded components to import."),
			*TargetLandscape->GetName());
		if (UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
		{
			Sys->ShowNotification(
				NSLOCTEXT("TerraDyne", "ImportNoComponents",
					"Import failed: the selected Landscape has no loaded components. Load the Landscape cells/proxies and try again."),
				ETerraDyneNotifySeverity::Error);
		}
		return;
	}

	if (Options.bRejectLandscapeVisibilityHoles)
	{
		for (ULandscapeComponent* Component : SourceComponents)
		{
			ULandscapeLayerInfoObject* VisibilityLayer = Component ? Component->GetVisibilityLayer() : nullptr;
			ALandscapeProxy* Proxy = Component ? Component->GetLandscapeProxy() : nullptr;
			ULandscapeInfo* Info = Proxy ? Proxy->GetLandscapeInfo() : nullptr;
			if (!VisibilityLayer || !Info || Component->ComponentSizeQuads <= 0)
			{
				continue;
			}

			const int32 Size = Component->ComponentSizeQuads + 1;
			TArray<uint8> VisibilityData;
			VisibilityData.SetNumZeroed(Size * Size);
			FLandscapeEditDataInterface EditData(Info);
			EditData.GetWeightDataFast(
				VisibilityLayer,
				Component->SectionBaseX,
				Component->SectionBaseY,
				Component->SectionBaseX + Component->ComponentSizeQuads,
				Component->SectionBaseY + Component->ComponentSizeQuads,
				VisibilityData.GetData(),
				Size);
			if (VisibilityData.ContainsByPredicate([](uint8 Value) { return Value > 0; }))
			{
				UE_LOG(LogTerraDyne, Error,
					TEXT("TerraDyne Import: refusing to convert %s because component %s contains Landscape visibility holes."),
					*TargetLandscape->GetName(), *GetNameSafe(Component));
				if (UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
				{
					Sys->ShowNotification(
						NSLOCTEXT("TerraDyne", "ImportVisibilityRejected",
							"Import stopped: Landscape visibility holes are not supported by the current TerraDyne mesh format."),
						ETerraDyneNotifySeverity::Error);
				}
				return;
			}
		}
	}

	TargetLandscapeSource = TargetLandscape;

	const bool bPreviousStreamingPaused = bStreamingPaused;
	if (Options.bPauseStreamingDuringImport)
	{
		bStreamingPaused = true;
	}

	PendingLoadQueue.Empty();
	PendingUnloadQueue.Empty();
	DirtyChunkSet.Empty();
	ImportedChunkCoords.Empty();
	LastStreamingHash = 0;
	ClearUndoRedoState();

	if (Options.bClearExistingChunks)
	{
		TArray<AActor*> ExistingChunks;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), ExistingChunks);
		for (AActor* Actor : ExistingChunks)
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}
		ActiveChunkMap.Reset();
	}

	LandscapeMigrationState = FTerraDyneLandscapeMigrationState();
	LandscapeMigrationState.bWasImportedFromLandscape = true;
	LandscapeMigrationState.SourceLandscapeName = MetadataLandscape->GetName();
	LandscapeMigrationState.SourceLandscapePath = MetadataLandscape->GetPathName();
	LandscapeMigrationState.SourceLandscapeLocation = MetadataLandscape->GetActorLocation();
	LandscapeMigrationState.SourceLandscapeScale = MetadataLandscape->GetActorScale3D();
	LandscapeMigrationState.SourceLandscapeMaterialPath = MetadataLandscape->LandscapeMaterial ? MetadataLandscape->LandscapeMaterial->GetPathName() : FString();
	LandscapeMigrationState.bImportedPaintLayers = Options.bImportWeightLayers;
	LandscapeMigrationState.bRegenerateGrassFromImportedLayers = Options.bRegenerateGrassFromImportedLayers;
	LandscapeMigrationState.bSourceLandscapeHidden = Options.bHideSourceLandscape;
	LandscapeMigrationState.ImportedAtIso8601 = FDateTime::UtcNow().ToIso8601();

	const FVector LandscapeScale = MetadataLandscape->GetActorScale3D();
	if (LandscapeScale.X > KINDA_SMALL_NUMBER)
	{
		GlobalChunkSize = SourceComponents[0]->ComponentSizeQuads * LandscapeScale.X;
	}

	const FVector SourceLocation = MetadataLandscape->GetActorLocation();
	SetActorLocation(FVector(
		SourceLocation.X + (GlobalChunkSize * 0.5f),
		SourceLocation.Y + (GlobalChunkSize * 0.5f),
		SourceLocation.Z - (256.0f * LandscapeScale.Z)));
	LandscapeMigrationState.RuntimeManagerLocation = GetActorLocation();
	LandscapeMigrationState.ImportedChunkSize = GlobalChunkSize;

	if (Options.bAdoptLandscapeMaterialAsMasterMaterial && MetadataLandscape->LandscapeMaterial)
	{
		if (TerraDyneWorldFramework::UsesLandscapeOnlyMaterialExpressions(MetadataLandscape->LandscapeMaterial))
		{
			UE_LOG(LogTerraDyne, Warning,
				TEXT("TerraDyne Import: Skipping adoption of landscape material %s because it uses Landscape-only material expressions."),
				*MetadataLandscape->LandscapeMaterial->GetName());

			if (UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
			{
				Sys->ShowNotification(
					NSLOCTEXT("TerraDyne", "ImportLandscapeMaterialRejected",
						"The source Landscape material uses Landscape-only nodes, so TerraDyne kept the current chunk master material."),
					ETerraDyneNotifySeverity::Warning);
			}
		}
		else
		{
			MasterMaterial = MetadataLandscape->LandscapeMaterial;
			LandscapeMigrationState.bAdoptedLandscapeMaterial = true;
		}
	}

	if (Options.bImportWeightLayers || Options.bCaptureLayerMappings)
	{
		TSet<FName> SeenMappedLayers;
		TSet<FName> SeenUnmappedLayers;
		for (ULandscapeComponent* Component : SourceComponents)
		{
			if (!Component)
			{
				continue;
			}

			for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations())
			{
				if (!Allocation.LayerInfo || Allocation.LayerInfo == Component->GetVisibilityLayer())
				{
					continue;
				}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				const FName LayerName = Allocation.LayerInfo->GetLayerName() != NAME_None
					? Allocation.LayerInfo->GetLayerName()
					: Allocation.LayerInfo->GetFName();
#else
				const FName LayerName = Allocation.LayerInfo->LayerName != NAME_None
					? Allocation.LayerInfo->LayerName
					: Allocation.LayerInfo->GetFName();
#endif

				if (SeenMappedLayers.Contains(LayerName) || SeenUnmappedLayers.Contains(LayerName))
				{
					continue;
				}

				if ((Options.bImportWeightLayers || Options.bCaptureLayerMappings) &&
					LandscapeMigrationState.LayerMappings.Num() < ATerraDyneChunk::NumWeightLayers)
				{
					FTerraDyneLandscapeLayerMapping Mapping;
					Mapping.SourceLayerName = LayerName;
					Mapping.TerraDyneWeightLayerIndex = LandscapeMigrationState.LayerMappings.Num();
					LandscapeMigrationState.LayerMappings.Add(Mapping);
					SeenMappedLayers.Add(LayerName);
				}
				else
				{
					LandscapeMigrationState.UnmappedLayerNames.Add(LayerName.ToString());
					SeenUnmappedLayers.Add(LayerName);
				}
			}
		}
	}

	LandscapeMigrationState.ImportedWeightLayerCount = LandscapeMigrationState.LayerMappings.Num();
	LandscapeMigrationState.bImportedPaintLayers =
		Options.bImportWeightLayers && LandscapeMigrationState.ImportedWeightLayerCount > 0;
	LandscapeMigrationState.bTransferredPlacedFoliage = false;
	LandscapeMigrationState.bTransferredFoliageFollowsTerrain = false;
	LandscapeMigrationState.ImportedFoliageDefinitionCount = 0;
	LandscapeMigrationState.ImportedFoliageInstanceCount = 0;

	TSet<FString> ImportedFoliageDefinitions;
	int32 ImportedFoliageInstances = 0;

	auto CapturePlacedFoliageForComponent =
		[this, &ImportedFoliageDefinitions, &ImportedFoliageInstances, &Options](ATerraDyneChunk* Chunk, ULandscapeComponent* Component)
	{
		if (!Chunk || !Component)
		{
			return;
		}

		AInstancedFoliageActor* FoliageActor =
			AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Component->GetComponentLevel(), false);
		if (!FoliageActor)
		{
			return;
		}

		const TMap<UFoliageType*, TArray<const FFoliageInstancePlacementInfo*>> FoliageByType =
			FoliageActor->GetInstancesForComponent(Component);
		if (FoliageByType.Num() == 0)
		{
			return;
		}

		TArray<FString> StaticMeshPaths;
		TArray<int32> MaterialCounts;
		TArray<FString> OverrideMaterialPaths;
		TArray<int32> DefinitionIndices;
		TArray<FTransform> LocalTransforms;
		TArray<float> TerrainOffsets;
		TMap<FString, int32> DefinitionLookup;
		TArray<FString> ActorClassPaths;
		TArray<uint8> ActorAttachFlags;
		TArray<int32> ActorDefinitionIndices;
		TArray<FTransform> ActorLocalTransforms;
		TArray<float> ActorTerrainOffsets;
		TMap<FString, int32> ActorDefinitionLookup;

		for (const auto& FoliagePair : FoliageByType)
		{
			const UFoliageType_InstancedStaticMesh* StaticMeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliagePair.Key);
			const UFoliageType_Actor* ActorType = Cast<UFoliageType_Actor>(FoliagePair.Key);
			if (StaticMeshType && StaticMeshType->Mesh)
			{
				const FString MeshPath = StaticMeshType->Mesh->GetPathName();
				TArray<FString> MaterialPaths;
				MaterialPaths.Reserve(StaticMeshType->OverrideMaterials.Num());
				for (UMaterialInterface* OverrideMaterial : StaticMeshType->OverrideMaterials)
				{
					if (!OverrideMaterial)
					{
						MaterialPaths.Add(FString());
						continue;
					}

					const FString MaterialPath = OverrideMaterial->GetPathName();
					MaterialPaths.Add(MaterialPath.StartsWith(TEXT("/Engine/Transient")) ? FString() : MaterialPath);
				}

				FString DefinitionKey = MeshPath;
				for (const FString& MaterialPath : MaterialPaths)
				{
					DefinitionKey += TEXT("|");
					DefinitionKey += MaterialPath;
				}

				int32 DefinitionIndex = INDEX_NONE;
				if (const int32* ExistingIndex = DefinitionLookup.Find(DefinitionKey))
				{
					DefinitionIndex = *ExistingIndex;
				}
				else
				{
					DefinitionIndex = StaticMeshPaths.Add(MeshPath);
					DefinitionLookup.Add(DefinitionKey, DefinitionIndex);
					MaterialCounts.Add(MaterialPaths.Num());
					OverrideMaterialPaths.Append(MaterialPaths);
					ImportedFoliageDefinitions.Add(DefinitionKey);
				}

				for (const FFoliageInstancePlacementInfo* PlacementInfo : FoliagePair.Value)
				{
					if (!PlacementInfo)
					{
						continue;
					}

					const FTransform WorldTransform(
						FQuat(PlacementInfo->Rotation),
						PlacementInfo->Location,
						FVector(PlacementInfo->DrawScale3D.X, PlacementInfo->DrawScale3D.Y, PlacementInfo->DrawScale3D.Z));
					const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Chunk->GetActorTransform());

					DefinitionIndices.Add(DefinitionIndex);
					LocalTransforms.Add(LocalTransform);
					TerrainOffsets.Add(
						LocalTransform.GetLocation().Z -
						Chunk->GetHeightAtLocation(FVector(LocalTransform.GetLocation().X, LocalTransform.GetLocation().Y, 0.0f)));
				}
			}
			else if (ActorType && ActorType->ActorClass)
			{
				const FString ActorClassPath = ActorType->ActorClass->GetPathName();
				const FString DefinitionKey = FString::Printf(
					TEXT("Actor:%s|%d"),
					*ActorClassPath,
					ActorType->bShouldAttachToBaseComponent ? 1 : 0);

				int32 DefinitionIndex = INDEX_NONE;
				if (const int32* ExistingIndex = ActorDefinitionLookup.Find(DefinitionKey))
				{
					DefinitionIndex = *ExistingIndex;
				}
				else
				{
					DefinitionIndex = ActorClassPaths.Add(ActorClassPath);
					ActorDefinitionLookup.Add(DefinitionKey, DefinitionIndex);
					ActorAttachFlags.Add(ActorType->bShouldAttachToBaseComponent ? 1 : 0);
					ImportedFoliageDefinitions.Add(DefinitionKey);
				}

				for (const FFoliageInstancePlacementInfo* PlacementInfo : FoliagePair.Value)
				{
					if (!PlacementInfo)
					{
						continue;
					}

					const FTransform WorldTransform(
						FQuat(PlacementInfo->Rotation),
						PlacementInfo->Location,
						FVector(PlacementInfo->DrawScale3D.X, PlacementInfo->DrawScale3D.Y, PlacementInfo->DrawScale3D.Z));
					const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Chunk->GetActorTransform());

					ActorDefinitionIndices.Add(DefinitionIndex);
					ActorLocalTransforms.Add(LocalTransform);
					ActorTerrainOffsets.Add(
						LocalTransform.GetLocation().Z -
						Chunk->GetHeightAtLocation(FVector(LocalTransform.GetLocation().X, LocalTransform.GetLocation().Y, 0.0f)));
				}
			}
		}

		if (LocalTransforms.Num() == 0 && ActorLocalTransforms.Num() == 0)
		{
			return;
		}

		ImportedFoliageInstances += LocalTransforms.Num() + ActorLocalTransforms.Num();
		Chunk->SetTransferredFoliageData(
			StaticMeshPaths,
			MaterialCounts,
			OverrideMaterialPaths,
			DefinitionIndices,
			LocalTransforms,
			TerrainOffsets,
			Options.bTransferredFoliageFollowsTerrain);
		Chunk->SetTransferredActorFoliageData(
			ActorClassPaths,
			ActorAttachFlags,
			ActorDefinitionIndices,
			ActorLocalTransforms,
			ActorTerrainOffsets);
	};

	int32 ImportedChunks = 0;
	int32 ImportedResolution = 0;
	for (ULandscapeComponent* Component : SourceComponents)
	{
		if (!Component)
		{
			continue;
		}

		const FIntPoint Coord = Component->GetSectionBase() / Component->ComponentSizeQuads;
		ImportedChunkCoords.Add(Coord);
		ATerraDyneChunk* Chunk = SpawnConfiguredChunk(Coord, Component->ComponentSizeQuads + 1);
		if (!Chunk)
		{
			continue;
		}

		Chunk->ZScale = 512.0f * LandscapeScale.Z;
		ResampleLandscapeData(Chunk, Component, Options.bImportWeightLayers);
		if (Options.bTransferPlacedFoliage)
		{
			CapturePlacedFoliageForComponent(Chunk, Component);
		}
		ImportedResolution = FMath::Max(ImportedResolution, Component->ComponentSizeQuads + 1);
		ImportedChunks++;
	}

	LandscapeMigrationState.ImportedComponentCount = ImportedChunks;
	LandscapeMigrationState.ImportedResolution = ImportedResolution;
	LandscapeMigrationState.bTransferredPlacedFoliage =
		Options.bTransferPlacedFoliage && ImportedFoliageInstances > 0;
	LandscapeMigrationState.bTransferredFoliageFollowsTerrain =
		LandscapeMigrationState.bTransferredPlacedFoliage && Options.bTransferredFoliageFollowsTerrain;
	LandscapeMigrationState.ImportedFoliageDefinitionCount = ImportedFoliageDefinitions.Num();
	LandscapeMigrationState.ImportedFoliageInstanceCount = ImportedFoliageInstances;

	if (ImportedChunks == 0)
	{
		bStreamingPaused = bPreviousStreamingPaused;
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne Import: Unable to create chunks for landscape %s."),
			*MetadataLandscape->GetName());
		if (UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
		{
			Sys->ShowNotification(
				NSLOCTEXT("TerraDyne", "ImportNoChunks",
					"Import failed: no TerraDyne chunks were created from the selected Landscape."),
				ETerraDyneNotifySeverity::Error);
		}
		return;
	}

	RebuildChunkMap();
	ApplyMaterialToActiveChunks();
	ApplyGrassProfileToActiveChunks(LandscapeMigrationState.bImportedPaintLayers && Options.bRegenerateGrassFromImportedLayers);

	if (Options.bHideSourceLandscape)
	{
		TerraDyneWorldFramework::HideLandscapeHierarchy(MetadataLandscape ? MetadataLandscape : TargetLandscape);
	}

	bStreamingPaused = bPreviousStreamingPaused;

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne Import: Imported %d landscape components from %s."),
		ImportedChunks, *MetadataLandscape->GetName());
}
#endif
