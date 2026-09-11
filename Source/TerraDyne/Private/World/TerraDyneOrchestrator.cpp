// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneOrchestrator.h"
#include "TerraDyneModule.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneWorldPreset.h"
#include "UI/TerraDyneToolWidget.h"
#include "Settings/TerraDyneSettings.h"
#include "Grass/TerraDyneGrassTypes.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InputComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StaticMeshDescription.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneDemoMarkerActor.h"

namespace
{
	static constexpr int32 OVERLAY_KEY_TITLE = 100;
	static constexpr int32 OVERLAY_KEY_DESC = 101;
	static constexpr int32 OVERLAY_KEY_DETAIL = 102;
	static constexpr int32 OVERLAY_KEY_AUX = 103;
	static constexpr TCHAR SHOWCASE_BASELINE_SLOT[] = TEXT("TerraDyne_ShowcaseBaseline");
	static constexpr TCHAR SHOWCASE_FALLBACK_TERRAIN_MATERIAL_PATH[] =
		TEXT("/TerraDyne/Materials/VHFM/M_TerraDyne_Master.M_TerraDyne_Master");
	static constexpr TCHAR SHOWCASE_SECONDARY_TERRAIN_MATERIAL_PATH[] =
		TEXT("/Game/M_Metal_Terrain.M_Metal_Terrain");
	static constexpr TCHAR SHOWCASE_WORLD_PRESET_PATH[] =
		TEXT("/TerraDyne/Samples/Presets/DA_TerraDyne_ShowcaseWorld.DA_TerraDyne_ShowcaseWorld");
	static constexpr TCHAR SHOWCASE_GRASS_PROFILE_PATH[] =
		TEXT("/TerraDyne/Samples/Profiles/DA_TerraDyne_ShowcaseGrass.DA_TerraDyne_ShowcaseGrass");
	static constexpr TCHAR TERRADYNE_MASTER_MATERIAL_PATH[] =
		TEXT("/TerraDyne/Materials/VHFM/M_TerraDyne_Master.M_TerraDyne_Master");
	static constexpr TCHAR BASIC_SHAPE_MATERIAL_PATH[] =
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	static const FName ShowcasePropTypeId(TEXT("ShowcaseCrate"));
	static const FName ShowcaseHarvestableTypeId(TEXT("ShowcaseBerryBush"));
	static const FName ShowcaseDestroyableTypeId(TEXT("ShowcaseRuins"));
	static const FName ShowcaseRuntimeTypeId(TEXT("ShowcaseRuntimeMarker"));

	static int32 ResolveShowcaseFallbackResolution(const ATerraDyneManager& Manager)
	{
		if (Manager.LandscapeMigrationState.ImportedResolution > 1)
		{
			return FMath::Clamp(Manager.LandscapeMigrationState.ImportedResolution, 32, 256);
		}

		if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
		{
			return FMath::Clamp(Settings->DefaultResolution, 32, 256);
		}

		return 128;
	}

	static FString PopulationStateToString(ETerraDynePopulationState State)
	{
		switch (State)
		{
		case ETerraDynePopulationState::Active:
			return TEXT("Active");
		case ETerraDynePopulationState::Harvested:
			return TEXT("Harvested");
		case ETerraDynePopulationState::Destroyed:
			return TEXT("Destroyed");
		case ETerraDynePopulationState::Regrowing:
			return TEXT("Regrowing");
		default:
			return TEXT("Unknown");
		}
	}

	static FIntPoint ComputeChunkCoord(const ATerraDyneManager& Manager, const FVector& WorldLocation)
	{
		const float ChunkSize = FMath::Max(1.0f, Manager.GlobalChunkSize);
		const FVector LocalLocation = WorldLocation - Manager.GetActorLocation();
		return FIntPoint(
			FMath::FloorToInt((LocalLocation.X + (ChunkSize * 0.5f)) / ChunkSize),
			FMath::FloorToInt((LocalLocation.Y + (ChunkSize * 0.5f)) / ChunkSize));
	}

	static float ScoreLandscapeLayerName(const FName LayerName)
	{
		const FString NormalizedName = LayerName.ToString().ToLower();
		float Score = 0.0f;

		if (NormalizedName.Contains(TEXT("grass")))      Score += 8.0f;
		if (NormalizedName.Contains(TEXT("meadow")))     Score += 6.0f;
		if (NormalizedName.Contains(TEXT("field")))      Score += 4.0f;
		if (NormalizedName.Contains(TEXT("lawn")))       Score += 4.0f;
		if (NormalizedName.Contains(TEXT("foliage")))    Score += 3.5f;
		if (NormalizedName.Contains(TEXT("vegetation"))) Score += 3.5f;
		if (NormalizedName.Contains(TEXT("forest")))     Score += 2.0f;
		if (NormalizedName.Contains(TEXT("rock")))       Score -= 5.0f;
		if (NormalizedName.Contains(TEXT("cliff")))      Score -= 5.0f;
		if (NormalizedName.Contains(TEXT("sand")))       Score -= 4.0f;
		if (NormalizedName.Contains(TEXT("snow")))       Score -= 4.0f;
		if (NormalizedName.Contains(TEXT("mud")))        Score -= 3.0f;
		if (NormalizedName.Contains(TEXT("dirt")))       Score -= 2.0f;

		return Score;
	}

	static bool IsLandscapeOnlyMaterialPath(const FString& MaterialPath)
	{
		if (MaterialPath.IsEmpty())
		{
			return false;
		}

		const FString NormalizedPath = MaterialPath.ToLower();
		return NormalizedPath.Contains(TEXT("/materials/landscape/")) ||
			NormalizedPath.Contains(TEXT("mwlandscapeautomaterial")) ||
			NormalizedPath.Contains(TEXT("landscapeautomaterial")) ||
			NormalizedPath.Contains(TEXT("m_landscape")) ||
			NormalizedPath.Contains(TEXT("m_landscape")) ||
			NormalizedPath.Contains(TEXT("landscape_"));
	}

	static bool IsMeshCompatibleShowcaseMaterialPath(const FString& MaterialPath)
	{
		if (MaterialPath.IsEmpty() || MaterialPath == BASIC_SHAPE_MATERIAL_PATH)
		{
			return false;
		}

		return !IsLandscapeOnlyMaterialPath(MaterialPath);
	}

	static FTransform MakeLocalTransform(
		float X,
		float Y,
		float Z,
		float YawDegrees = 0.0f,
		float UniformScale = 1.0f)
	{
		return FTransform(
			FRotator(0.0f, YawDegrees, 0.0f),
			FVector(X, Y, Z),
			FVector(UniformScale));
	}
}

// ---------------------------------------------------------------------------
// Runtime grass blade mesh builder
// Creates a simple cross-billboard (two perpendicular quads) that reads as
// grass when instanced at small scale with a green material.
// ---------------------------------------------------------------------------
static UStaticMesh* CreateRuntimeGrassBlade(UObject* Outer)
{
#if WITH_EDITOR
	// Runtime UStaticMesh generation is only reliable in Editor builds
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer);

	UStaticMeshDescription* Desc = Mesh->CreateStaticMeshDescription();

	// Attribute accessors
	auto Normals = Desc->GetVertexInstanceNormals();
	auto UVs     = Desc->GetVertexInstanceUVs();

	// Single polygon group for one material slot
	FPolygonGroupID Group = Desc->CreatePolygonGroup();
	Desc->SetPolygonGroupMaterialSlotName(Group, FName(TEXT("GrassMat")));

	// Two quads arranged as a cross (blade A along X, blade B along Y).
	const float W = 25.f;   // half-width of blade (cm)
	const float H = 120.f;  // height of blade (cm)

	auto AddQuad = [&](FVector BL, FVector BR, FVector TR, FVector TL)
	{
		FVector Normal = FVector::CrossProduct(BR - BL, TL - BL).GetSafeNormal();
		FVector3f N3f(Normal);

		FVertexID V0 = Desc->CreateVertex(); Desc->SetVertexPosition(V0, BL);
		FVertexID V1 = Desc->CreateVertex(); Desc->SetVertexPosition(V1, BR);
		FVertexID V2 = Desc->CreateVertex(); Desc->SetVertexPosition(V2, TR);
		FVertexID V3 = Desc->CreateVertex(); Desc->SetVertexPosition(V3, TL);

		TArray<FEdgeID> Edges;

		// Tri 1: V0-V1-V2
		{
			FVertexInstanceID I0 = Desc->CreateVertexInstance(V0);
			FVertexInstanceID I1 = Desc->CreateVertexInstance(V1);
			FVertexInstanceID I2 = Desc->CreateVertexInstance(V2);
			Normals.Set(I0, 0, N3f); Normals.Set(I1, 0, N3f); Normals.Set(I2, 0, N3f);
			UVs.Set(I0, 0, FVector2f(0, 1)); UVs.Set(I1, 0, FVector2f(1, 1)); UVs.Set(I2, 0, FVector2f(1, 0));
			Desc->CreateTriangle(Group, {I0, I1, I2}, Edges);
		}
		// Tri 2: V0-V2-V3
		{
			FVertexInstanceID I0 = Desc->CreateVertexInstance(V0);
			FVertexInstanceID I2 = Desc->CreateVertexInstance(V2);
			FVertexInstanceID I3 = Desc->CreateVertexInstance(V3);
			Normals.Set(I0, 0, N3f); Normals.Set(I2, 0, N3f); Normals.Set(I3, 0, N3f);
			UVs.Set(I0, 0, FVector2f(0, 1)); UVs.Set(I2, 0, FVector2f(1, 0)); UVs.Set(I3, 0, FVector2f(0, 0));
			Desc->CreateTriangle(Group, {I0, I2, I3}, Edges);
		}
	};

	// Blade A: along X axis
	AddQuad(FVector(-W, 0, 0), FVector(W, 0, 0), FVector(W*0.3f, 0, H), FVector(-W*0.3f, 0, H));
	// Blade B: along Y axis (90 degrees)
	AddQuad(FVector(0, -W, 0), FVector(0, W, 0), FVector(0, W*0.3f, H), FVector(0, -W*0.3f, H));

	// Register material slot BEFORE Build so the polygon group name "GrassMat"
	// resolves to a real StaticMaterial entry and Build can initialize its UVChannelData.
	// Adding the slot afterwards leaves UVChannelData.bInitialized = false, which trips an
	// engine ensure() when foliage/instancing systems read UV streams from the mesh.
	Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, FName(TEXT("GrassMat"))));

	Mesh->BuildFromStaticMeshDescriptions({Desc});

#if WITH_EDITORONLY_DATA
	// Defensive: rebuild UV-stream-density data even if Build did not.
	Mesh->UpdateUVChannelData(false);
#endif

	return Mesh;
#else
	// In packaged builds, skip runtime mesh generation - use pre-authored assets instead
	UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne: Runtime grass blade generation skipped in packaged build. Use pre-authored mesh assets."));
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
#endif
}

ATerraDyneOrchestrator::ATerraDyneOrchestrator()
{
	PrimaryActorTick.bCanEverTick = true;

	CurrentPhase = EShowcasePhase::Warmup;
	PhaseTimer = 0.0f;
	ActionTimer = 0.0f;
	OriginalPlayerPos = FVector::ZeroVector;

	bIsClicking = false;
	bFlattenHeightLocked = false;
	LockedFlattenHeight = 0.0f;

	CameraWaypointIndex = 0;
	CameraWaypointTimer = 0.0f;
	bCameraInTransit = false;
	bCameraEnabled = false;
	CameraTransitStartLocation = FVector::ZeroVector;
	CameraTransitStartRotation = FRotator::ZeroRotator;

	UndoRedoDemoStep = 0;
	UndoRedoSubTimer = 0.0f;
	bPopulationRuntimePlacementTriggered = false;
	bPopulationHarvestTriggered = false;
	bPopulationDestroyTriggered = false;
	bPopulationRestoreTriggered = false;
	bGameplayPulseTriggered = false;
	bShowcaseBaselineCaptured = false;

	TerrainEventCount = 0;
	FoliageEventCount = 0;
	PopulationEventCount = 0;
	LastTerrainEventCoord = FIntPoint::ZeroValue;
	LastFoliageEventInstances = 0;
	LastPopulationReason = NAME_None;
	LastPopulationTypeId = NAME_None;
}

void ATerraDyneOrchestrator::InitShowcaseSequence()
{
	ShowcaseSequence = {
		EShowcasePhase::Warmup,
		EShowcasePhase::AuthoredWorldDemo,
		EShowcasePhase::SculptingDemo,
		EShowcasePhase::LayerDemo,
		EShowcasePhase::PaintDemo,
		EShowcasePhase::UndoRedoDemo,
		EShowcasePhase::PopulationDemo,
		EShowcasePhase::ProceduralWorldDemo,
		EShowcasePhase::GameplayHooksDemo,
		EShowcasePhase::DesignerWorkflowDemo,
		EShowcasePhase::PersistenceDemo,
		EShowcasePhase::ReplicationDemo,
		EShowcasePhase::LODDemo,
		EShowcasePhase::Interactive
	};
}

void ATerraDyneOrchestrator::InitPhaseConfigs()
{
	PhaseConfigs.Add(EShowcasePhase::Warmup, {4.0f, TEXT("TERRADYNE v0.3"), TEXT("Persistent runtime world framework for Unreal Landscapes")});
	PhaseConfigs.Add(EShowcasePhase::AuthoredWorldDemo, {8.0f, TEXT("AUTHORED WORLD CONVERSION"), TEXT("Runtime terrain, transferred foliage, actor foliage, grass, and paint metadata")});
	PhaseConfigs.Add(EShowcasePhase::SculptingDemo, {10.0f, TEXT("RUNTIME TERRAIN EDITING"), TEXT("Raise / Lower / Smooth / Flatten on the converted world")});
	PhaseConfigs.Add(EShowcasePhase::LayerDemo, {6.0f, TEXT("HEIGHT LAYER STACK"), TEXT("Base / Sculpt / Detail layers stay separate for authored plus runtime edits")});
	PhaseConfigs.Add(EShowcasePhase::PaintDemo, {6.0f, TEXT("PAINT LAYER MIGRATION"), TEXT("Weight maps continue to drive materials, biomes, and foliage logic at runtime")});
	PhaseConfigs.Add(EShowcasePhase::UndoRedoDemo, {8.0f, TEXT("UNDO / REDO"), TEXT("Per-player terrain history survives real runtime usage")});
	PhaseConfigs.Add(EShowcasePhase::PopulationDemo, {10.0f, TEXT("PERSISTENT WORLD POPULATION"), TEXT("Props, harvestables, destruction, regrowth, and runtime placement")});
	PhaseConfigs.Add(EShowcasePhase::ProceduralWorldDemo, {10.0f, TEXT("SEEDED PROCEDURAL OUTSKIRTS"), TEXT("Biome overlays, spawn rules, and optional infinite edge growth")});
	PhaseConfigs.Add(EShowcasePhase::GameplayHooksDemo, {8.0f, TEXT("GAMEPLAY HOOKS"), TEXT("Nav refresh, AI zones, build permissions, and runtime change events")});
	PhaseConfigs.Add(EShowcasePhase::DesignerWorkflowDemo, {6.0f, TEXT("DESIGNER WORKFLOW"), TEXT("Preset-driven setup, templates, docs, and PCG export points")});
	PhaseConfigs.Add(EShowcasePhase::PersistenceDemo, {8.0f, TEXT("SAVE / LOAD"), TEXT("Terrain and population state restore together from one authoritative world layer")});
	PhaseConfigs.Add(EShowcasePhase::ReplicationDemo, {5.0f, TEXT("NETWORKING"), TEXT("Replication, authoritative edits, and late-join sync")});
	PhaseConfigs.Add(EShowcasePhase::LODDemo, {6.0f, TEXT("LOD & DISTANCE CULLING"), TEXT("Far traversal keeps collision and streaming costs under control")});
	PhaseConfigs.Add(EShowcasePhase::Interactive, {0.0f, TEXT("YOUR TURN"), TEXT("Click to sculpt and inspect the runtime world systems live")});
}

int32 ATerraDyneOrchestrator::GetPhaseIndex(EShowcasePhase Phase) const
{
	return ShowcaseSequence.IndexOfByKey(Phase);
}
void ATerraDyneOrchestrator::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTerraDyne, Log, TEXT("Showcase: Sequence Started"));

	InitShowcaseSequence();
	InitPhaseConfigs();

	// Setup UI and Player
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		// 1. Check if ANY ToolWidget already exists (handles duplication robustly)
		TArray<UUserWidget*> FoundWidgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), FoundWidgets, UTerraDyneToolWidget::StaticClass(), false);

		if (FoundWidgets.Num() == 0)
		{
			// Try to load WBP from Settings, else fallback
			UClass* WidgetClass = nullptr;
			if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
			{
				if (Settings->HUDWidgetPath.IsValid())
				{
					UClass* LoadedClass = Cast<UClass>(Settings->HUDWidgetPath.TryLoad());
					if (LoadedClass && LoadedClass->IsChildOf(UUserWidget::StaticClass()))
					{
						WidgetClass = LoadedClass;
					}
				}
			}

			if (!WidgetClass)
			{
				WidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/TerraDyne/UI/WBP_TerraDyneUI.WBP_TerraDyneUI_C"));
			}

			if (!WidgetClass)
			{
				WidgetClass = UTerraDyneToolWidget::StaticClass();
			}

			if (WidgetClass)
			{
				ActiveUI = CreateWidget<UUserWidget>(PC, WidgetClass);
				if (ActiveUI)
				{
					ActiveUI->AddToViewport();
					PC->SetShowMouseCursor(true);

					FInputModeGameAndUI InputMode;
					InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
					PC->SetInputMode(InputMode);
				}
			}

			// Setup Input for Default Controller
			SetupInput();
		}
		else
		{
			// Grab the existing one so we can read values from it
			ActiveUI = FoundWidgets[0];
			UE_LOG(LogTerraDyne, Log, TEXT("Showcase: Connected to existing ToolWidget."));
		}

	}

	if (bInteractiveOnly)
	{
		ClearShowcaseTimers();
		BindManagerEvents(GetManager());
		
		PhaseTimer = 0.0f;
		ActionTimer = 0.0f;
		CurrentPhase = EShowcasePhase::Interactive;
		OnPhaseEnter(CurrentPhase);
	}
	else
	{
		RestartShowcase();
	}
}

void ATerraDyneOrchestrator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearShowcaseTimers();
	UnbindManagerEvents();
	Super::EndPlay(EndPlayReason);
}

void ATerraDyneOrchestrator::RestartShowcase()
{
	ClearShowcaseTimers();
	EnsureShowcaseWorld();
	if (bShowcaseBaselineCaptured)
	{
		RestoreShowcaseBaseline();
		EnsureShowcaseWorld();
	}
	else
	{
		CaptureShowcaseBaseline();
	}
	BindManagerEvents(GetManager());

	PhaseTimer = 0.0f;
	ActionTimer = 0.0f;
	UndoRedoDemoStep = 0;
	UndoRedoSubTimer = 0.0f;
	bPopulationRuntimePlacementTriggered = false;
	bPopulationHarvestTriggered = false;
	bPopulationDestroyTriggered = false;
	bPopulationRestoreTriggered = false;
	bGameplayPulseTriggered = false;
	TerrainEventCount = 0;
	FoliageEventCount = 0;
	PopulationEventCount = 0;
	LastTerrainEventCoord = FIntPoint::ZeroValue;
	LastFoliageEventInstances = 0;
	LastPopulationReason = NAME_None;
	LastPopulationTypeId = NAME_None;

	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Pawn->SetActorLocation(FVector(-3400.0f, -3400.0f, 3200.0f));
		Pawn->SetActorRotation(FRotator(-28.0f, 45.0f, 0.0f));
	}

	CurrentPhase = EShowcasePhase::Warmup;
	OnPhaseEnter(CurrentPhase);
}

// ---- Input ----

void ATerraDyneOrchestrator::SetupInput()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC)
	{
		EnableInput(PC);
		if (InputComponent)
		{
			// TODO: Migrate to Enhanced Input System (UEnhancedInputComponent + UInputAction assets)
			// Legacy input is retained for backward compatibility with existing projects.
			InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ATerraDyneOrchestrator::OnLeftClickStart);
			InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ATerraDyneOrchestrator::OnLeftClickStop);
		}
	}
}

void ATerraDyneOrchestrator::OnLeftClickStart() { bIsClicking = true; bFlattenHeightLocked = false; }
void ATerraDyneOrchestrator::OnLeftClickStop()  { bIsClicking = false; bFlattenHeightLocked = false; }

void ATerraDyneOrchestrator::PerformToolAction()
{
	if (CurrentPhase != EShowcasePhase::Interactive)
	{
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	FVector WorldLoc;
	FVector WorldDir;
	if (!PC->DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return;
	}

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	if (APawn* MyPawn = PC->GetPawn())
	{
		Params.AddIgnoredActor(MyPawn);
	}

	if (!GetWorld()->LineTraceSingleByChannel(Hit, WorldLoc, WorldLoc + WorldDir * 200000.0f, ECC_WorldStatic, Params))
	{
		return;
	}

	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	float Radius = 2000.0f;
	float Strength = 1000.0f;
	ETerraDyneToolMode Mode = ETerraDyneToolMode::SculptRaise;
	int32 LayerIndex = 0;

	if (UTerraDyneToolWidget* ToolUI = Cast<UTerraDyneToolWidget>(ActiveUI))
	{
		Radius = ToolUI->BrushRadius;
		Strength = ToolUI->BrushStrength * 2500.0f;
		Mode = ToolUI->CurrentTool;
		LayerIndex = ToolUI->ActiveLayerIndex;
	}

	ETerraDyneBrushMode BrushMode = ETerraDyneBrushMode::Raise;
	if (Mode == ETerraDyneToolMode::SculptLower)
	{
		BrushMode = ETerraDyneBrushMode::Lower;
		Strength = -FMath::Abs(Strength);
	}
	else if (Mode == ETerraDyneToolMode::Flatten)
	{
		BrushMode = ETerraDyneBrushMode::Flatten;
		Strength = FMath::Abs(Strength);
	}
	else if (Mode == ETerraDyneToolMode::Smooth)
	{
		BrushMode = ETerraDyneBrushMode::Smooth;
		Strength = FMath::Abs(Strength);
	}
	else if (Mode == ETerraDyneToolMode::Paint)
	{
		BrushMode = ETerraDyneBrushMode::Paint;
		Strength = FMath::Abs(Strength);
	}

	float FlattenHeight = 0.0f;
	if (BrushMode == ETerraDyneBrushMode::Flatten)
	{
		if (!bFlattenHeightLocked)
		{
			if (ATerraDyneChunk* HitChunk = Manager->GetChunkAtLocation(Hit.Location))
			{
				const FVector RelativePos = Hit.Location - HitChunk->GetActorLocation();
				LockedFlattenHeight = HitChunk->GetHeightAtLocation(RelativePos);
			}
			bFlattenHeightLocked = true;
		}
		FlattenHeight = LockedFlattenHeight;
	}

	Manager->ApplyGlobalBrush(Hit.Location, Radius, Strength, BrushMode, Manager->ActiveLayer, LayerIndex, FlattenHeight);
	DrawDebugSphere(GetWorld(), Hit.Location, Radius, 16, FColor::Green, false, -1.0f, 0, 2.0f);
}

ATerraDyneManager* ATerraDyneOrchestrator::GetManager() const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	if (UTerraDyneSubsystem* Subsystem = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
	{
		if (ATerraDyneManager* Manager = Subsystem->GetTerrainManager())
		{
			return Manager;
		}
	}

	return Cast<ATerraDyneManager>(UGameplayStatics::GetActorOfClass(GetWorld(), ATerraDyneManager::StaticClass()));
}

ATerraDyneManager* ATerraDyneOrchestrator::EnsureShowcaseManager()
{
	if (ATerraDyneManager* ExistingManager = GetManager())
	{
		return ExistingManager;
	}

	if (!GetWorld() || !UGameplayStatics::GetPlayerController(this, 0))
	{
		return nullptr;
	}

	ATerraDyneManager* Manager = GetWorld()->SpawnActorDeferred<ATerraDyneManager>(
		ATerraDyneManager::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Manager)
	{
		return nullptr;
	}

	Manager->GlobalChunkSize = 10000.0f;
	Manager->bSpawnDefaultChunksOnBeginPlay = false;
	Manager->bSetupDefaultLightingOnBeginPlay = true;
	Manager->bSpawnShowcaseOnBeginPlay = false;
	Manager->FinishSpawning(FTransform::Identity);
	return Manager;
}

UTerraDyneWorldPreset* ATerraDyneOrchestrator::BuildFallbackPreset()
{
	if (ShowcasePreset)
	{
		return ShowcasePreset;
	}

	if (UTerraDyneWorldPreset* PackagedPreset = LoadObject<UTerraDyneWorldPreset>(nullptr, SHOWCASE_WORLD_PRESET_PATH))
	{
		ShowcasePreset = PackagedPreset;
		return ShowcasePreset;
	}

	ShowcasePreset = NewObject<UTerraDyneWorldPreset>(this, TEXT("TerraDyneShowcasePreset"));
	ShowcasePreset->PresetId = TEXT("ShowcaseWorld");
	ShowcasePreset->DisplayName = FText::FromString(TEXT("Full Feature Showcase"));
	ShowcasePreset->Description = FText::FromString(TEXT("Fallback preset used by the TerraDyne demo when the map is not preconfigured."));
	ShowcasePreset->bSpawnDefaultChunksOnBeginPlay = false;
	ShowcasePreset->bSetupDefaultLightingOnBeginPlay = true;
	ShowcasePreset->bRefreshNavMeshOnTerrainChanges = true;
	ShowcasePreset->bRefreshNavMeshOnPopulationChanges = true;
	ShowcasePreset->NavigationDirtyBoundsPadding = 450.0f;
	ShowcasePreset->ProceduralSettings.WorldSeed = 20260314;
	ShowcasePreset->ProceduralSettings.bEnableSeededOutskirts = true;
	ShowcasePreset->ProceduralSettings.bEnableInfiniteEdgeGrowth = true;
	ShowcasePreset->ProceduralSettings.ProceduralOutskirtsExtent = 6;
	ShowcasePreset->ProceduralSettings.bGeneratePopulationFromRules = true;

	FTerraDyneBiomeOverlay AuthoredOverlay;
	AuthoredOverlay.BiomeTag = TEXT("Meadow");
	AuthoredOverlay.WeightLayerIndex = 1;
	AuthoredOverlay.MinimumPaintWeight = 0.15f;
	AuthoredOverlay.bApplyToAuthoredChunks = true;
	AuthoredOverlay.bApplyToProceduralChunks = false;
	AuthoredOverlay.Priority = 10;
	ShowcasePreset->BiomeOverlays.Add(AuthoredOverlay);

	FTerraDyneBiomeOverlay ForestOverlay;
	ForestOverlay.BiomeTag = TEXT("Forest");
	ForestOverlay.bApplyToAuthoredChunks = false;
	ForestOverlay.bApplyToProceduralChunks = true;
	ForestOverlay.ProceduralNoiseMin = 0.0f;
	ForestOverlay.ProceduralNoiseMax = 0.62f;
	ForestOverlay.Priority = 4;
	ShowcasePreset->BiomeOverlays.Add(ForestOverlay);

	FTerraDyneBiomeOverlay HighlandsOverlay;
	HighlandsOverlay.BiomeTag = TEXT("Highlands");
	HighlandsOverlay.bApplyToAuthoredChunks = false;
	HighlandsOverlay.bApplyToProceduralChunks = true;
	HighlandsOverlay.ProceduralNoiseMin = 0.62f;
	HighlandsOverlay.ProceduralNoiseMax = 1.0f;
	HighlandsOverlay.Priority = 5;
	ShowcasePreset->BiomeOverlays.Add(HighlandsOverlay);

	FTerraDynePopulationSpawnRule ForestRule;
	ForestRule.RuleId = TEXT("ForestMarkers");
	ForestRule.Descriptor.TypeId = TEXT("ForestMarker");
	ForestRule.Descriptor.ActorClass = ATerraDyneDemoMarkerActor::StaticClass();
	ForestRule.Descriptor.Kind = ETerraDynePopulationKind::ProceduralSpawn;
	ForestRule.Descriptor.bReplicates = true;
	ForestRule.Descriptor.bSnapToTerrain = true;
	ForestRule.Descriptor.TerrainOffset = 220.0f;
	ForestRule.Descriptor.Tags = {TEXT("Showcase"), TEXT("Procedural")};
	ForestRule.RequiredBiomeTag = TEXT("Forest");
	ForestRule.MinDistanceFromAuthored = 1;
	ForestRule.MinInstancesPerChunk = 2;
	ForestRule.MaxInstancesPerChunk = 4;
	ForestRule.MinSpacing = 1400.0f;
	ShowcasePreset->PopulationSpawnRules.Add(ForestRule);

	FTerraDynePopulationSpawnRule HighlandsRule;
	HighlandsRule.RuleId = TEXT("HighlandStones");
	HighlandsRule.Descriptor.TypeId = TEXT("HighlandStone");
	HighlandsRule.Descriptor.Kind = ETerraDynePopulationKind::ProceduralSpawn;
	HighlandsRule.Descriptor.StaticMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	HighlandsRule.Descriptor.bReplicates = true;
	HighlandsRule.Descriptor.bSnapToTerrain = true;
	HighlandsRule.Descriptor.TerrainOffset = 95.0f;
	HighlandsRule.Descriptor.Tags = {TEXT("Showcase"), TEXT("Procedural")};
	HighlandsRule.RequiredBiomeTag = TEXT("Highlands");
	HighlandsRule.MinDistanceFromAuthored = 1;
	HighlandsRule.MinInstancesPerChunk = 1;
	HighlandsRule.MaxInstancesPerChunk = 3;
	HighlandsRule.MinSpacing = 1100.0f;
	ShowcasePreset->PopulationSpawnRules.Add(HighlandsRule);

	FTerraDyneAISpawnZone TownZone;
	TownZone.ZoneId = TEXT("TownCore");
	TownZone.LocalBounds = FBox(FVector(-3200.0f, -3200.0f, -500.0f), FVector(3200.0f, 3200.0f, 3500.0f));
	TownZone.MaxConcurrentSpawns = 4;
	ShowcasePreset->AISpawnZones.Add(TownZone);

	FTerraDyneAISpawnZone OutskirtsZone;
	OutskirtsZone.ZoneId = TEXT("ForestOutskirts");
	OutskirtsZone.LocalBounds = FBox(FVector(18000.0f, -4500.0f, -500.0f), FVector(29000.0f, 4500.0f, 4500.0f));
	OutskirtsZone.bProceduralOnly = true;
	OutskirtsZone.RequiredBiomeTag = TEXT("Forest");
	OutskirtsZone.MaxConcurrentSpawns = 8;
	ShowcasePreset->AISpawnZones.Add(OutskirtsZone);

	FTerraDyneBuildPermissionZone TownFootprint;
	TownFootprint.ZoneId = TEXT("TownFootprint");
	TownFootprint.Permission = ETerraDyneBuildPermission::Blocked;
	TownFootprint.Reason = TEXT("Protected settlement footprint");
	TownFootprint.LocalBounds = FBox(FVector(-2600.0f, -2600.0f, -500.0f), FVector(2600.0f, 2600.0f, 3000.0f));
	TownFootprint.MaxSlopeDegrees = 50.0f;
	ShowcasePreset->BuildPermissionZones.Add(TownFootprint);

	return ShowcasePreset;
}

ATerraDyneChunk* ATerraDyneOrchestrator::EnsurePrimaryAuthoredChunk(ATerraDyneManager* Manager)
{
	if (!Manager || !GetWorld())
	{
		return nullptr;
	}

	ATerraDyneChunk* PrimaryChunk = Manager->GetChunkAtCoord(FIntPoint::ZeroValue);
	bool bSpawnedFallbackChunk = false;
	if (!PrimaryChunk && Manager->GetActiveChunkCount() == 0)
	{
		const int32 FallbackResolution = ResolveShowcaseFallbackResolution(*Manager);
		FActorSpawnParameters SpawnParams;
		SpawnParams.bDeferConstruction = true;
		PrimaryChunk = GetWorld()->SpawnActor<ATerraDyneChunk>(
			ATerraDyneChunk::StaticClass(),
			FTransform(Manager->GetActorLocation()),
			SpawnParams);
		if (PrimaryChunk)
		{
			PrimaryChunk->GridCoordinate = FIntPoint::ZeroValue;
			PrimaryChunk->ChunkSizeWorldUnits = Manager->GlobalChunkSize;
			PrimaryChunk->WorldSize = Manager->GlobalChunkSize;
			PrimaryChunk->Resolution = FallbackResolution;
			PrimaryChunk->BrushMaterialBase = Manager->HeightBrushMaterial;
			PrimaryChunk->bIsAuthoredChunk = true;
			PrimaryChunk->FinishSpawning(FTransform(Manager->GetActorLocation()));
			PrimaryChunk->Initialize(PrimaryChunk->Resolution, PrimaryChunk->WorldSize);
			if (Manager->MasterMaterial)
			{
				PrimaryChunk->SetMaterial(Manager->MasterMaterial);
			}
			bSpawnedFallbackChunk = true;
		}
	}

	Manager->RebuildChunkMap();
	if (!PrimaryChunk)
	{
		PrimaryChunk = Manager->GetChunkAtCoord(FIntPoint::ZeroValue);
	}

	TArray<FIntPoint> LoadedChunkCoords;
	for (ATerraDyneChunk* Chunk : Manager->GetChunksInRadius(Manager->GetActorLocation(), Manager->GlobalChunkSize * 8.0f))
	{
		if (!Chunk)
		{
			continue;
		}

		LoadedChunkCoords.AddUnique(Chunk->GridCoordinate);
		if (!PrimaryChunk)
		{
			PrimaryChunk = Chunk;
		}
	}

	if (LoadedChunkCoords.Num() > 0 && !Manager->LandscapeMigrationState.bWasImportedFromLandscape)
	{
		Manager->SetAuthoredChunkCoordinates(LoadedChunkCoords, true);
		Manager->LandscapeMigrationState.SourceLandscapeName = TEXT("Showcase_AuthoredLandscape");
		Manager->LandscapeMigrationState.SourceLandscapePath = TEXT("/TerraDyne/Demo/Showcase_AuthoredLandscape");
		Manager->LandscapeMigrationState.bImportedPaintLayers = true;
		Manager->LandscapeMigrationState.ImportedWeightLayerCount = ATerraDyneChunk::NumWeightLayers;
		Manager->LandscapeMigrationState.bRegenerateGrassFromImportedLayers = true;
		Manager->LandscapeMigrationState.bTransferredFoliageFollowsTerrain = true;
	}

	if (bSpawnedFallbackChunk)
	{
		SeedFallbackTerrain(Manager);
	}

	return PrimaryChunk;
}

void ATerraDyneOrchestrator::SeedFallbackTerrain(ATerraDyneManager* Manager)
{
	if (!Manager)
	{
		return;
	}

	const FVector Origin = Manager->GetActorLocation();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Manager->ApplyGlobalBrush(Origin + FVector(-2100.0f, -1000.0f, 0.0f), 1500.0f, 950.0f, ETerraDyneBrushMode::Raise);
		Manager->ApplyGlobalBrush(Origin + FVector(1700.0f, 1200.0f, 0.0f), 1100.0f, 800.0f, ETerraDyneBrushMode::Raise);
	}

	for (int32 Index = 0; Index < 3; ++Index)
	{
		Manager->ApplyGlobalBrush(Origin + FVector(1600.0f, -1800.0f, 0.0f), 1300.0f, 900.0f, ETerraDyneBrushMode::Lower);
	}

	Manager->ApplyGlobalBrush(Origin, 2400.0f, 1300.0f, ETerraDyneBrushMode::Smooth);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		Manager->ApplyGlobalBrush(Origin + FVector(-1200.0f, 900.0f, 0.0f), 2300.0f, 1.0f, ETerraDyneBrushMode::Paint, ETerraDyneLayer::Sculpt, 1);
		Manager->ApplyGlobalBrush(Origin + FVector(1500.0f, -600.0f, 0.0f), 1800.0f, 1.0f, ETerraDyneBrushMode::Paint, ETerraDyneLayer::Sculpt, 2);
	}
}

void ATerraDyneOrchestrator::EnsureFallbackFoliagePresentation(ATerraDyneManager* Manager, ATerraDyneChunk* Chunk)
{
	if (!Manager || !Chunk)
	{
		return;
	}

	if (Chunk->GetTransferredFoliageInstanceCount() == 0)
	{
		const TArray<FString> MeshPaths = {
			TEXT("/Engine/BasicShapes/Cone.Cone"),
			TEXT("/Engine/BasicShapes/Sphere.Sphere")
		};
		const TArray<int32> MaterialCounts = {0, 0};
		const TArray<FString> OverrideMaterialPaths;
		const TArray<int32> DefinitionIndices = {0, 0, 1, 1, 0, 1};
		const TArray<FTransform> LocalTransforms = {
			MakeLocalTransform(-2100.0f, -1400.0f, 0.0f, 0.0f, 0.95f),
			MakeLocalTransform(-1100.0f, 1200.0f, 0.0f, 35.0f, 0.85f),
			MakeLocalTransform(1400.0f, 1000.0f, 0.0f, 0.0f, 1.2f),
			MakeLocalTransform(2200.0f, -900.0f, 0.0f, 0.0f, 0.75f),
			MakeLocalTransform(-300.0f, 2200.0f, 0.0f, 0.0f, 0.9f),
			MakeLocalTransform(900.0f, -2300.0f, 0.0f, 0.0f, 0.7f)
		};
		const TArray<float> TerrainOffsets = {180.0f, 180.0f, 95.0f, 95.0f, 180.0f, 95.0f};
		Chunk->SetTransferredFoliageData(
			MeshPaths,
			MaterialCounts,
			OverrideMaterialPaths,
			DefinitionIndices,
			LocalTransforms,
			TerrainOffsets,
			true);
	}

	if (Chunk->GetTransferredActorFoliageInstanceCount() == 0)
	{
		const TArray<FString> ActorClassPaths = {ATerraDyneDemoMarkerActor::StaticClass()->GetPathName()};
		const TArray<uint8> AttachFlags = {1};
		const TArray<int32> DefinitionIndices = {0, 0, 0};
		const TArray<FTransform> LocalTransforms = {
			MakeLocalTransform(-900.0f, -400.0f, 0.0f, 0.0f, 1.0f),
			MakeLocalTransform(250.0f, 1600.0f, 0.0f, 20.0f, 1.1f),
			MakeLocalTransform(2100.0f, 300.0f, 0.0f, -15.0f, 0.9f)
		};
		const TArray<float> TerrainOffsets = {220.0f, 220.0f, 220.0f};
		Chunk->SetTransferredActorFoliageData(
			ActorClassPaths,
			AttachFlags,
			DefinitionIndices,
			LocalTransforms,
			TerrainOffsets);
	}

	Manager->LandscapeMigrationState.bTransferredPlacedFoliage = true;
	Manager->LandscapeMigrationState.bTransferredFoliageFollowsTerrain = true;
	Manager->LandscapeMigrationState.ImportedFoliageDefinitionCount =
		Chunk->GetTransferredFoliageDefinitionCount() + Chunk->GetTransferredActorFoliageDefinitionCount();
	Manager->LandscapeMigrationState.ImportedFoliageInstanceCount =
		Chunk->GetTransferredFoliageInstanceCount() + Chunk->GetTransferredActorFoliageInstanceCount();
}

void ATerraDyneOrchestrator::EnsureShowcaseTerrainMaterial(ATerraDyneManager* Manager)
{
	if (!Manager)
	{
		return;
	}

	auto ApplyMaterialIfLoaded = [Manager](const FString& MaterialPath) -> bool
	{
		if (MaterialPath.IsEmpty())
		{
			return false;
		}

		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
		{
			Manager->MasterMaterial = Material;
			for (ATerraDyneChunk* Chunk : Manager->GetChunksInRadius(Manager->GetActorLocation(), Manager->GlobalChunkSize * 8.0f))
			{
				if (Chunk)
				{
					Chunk->SetMaterial(Material);
				}
			}
			return true;
		}

		return false;
	};

	const FString CurrentMaterialPath = Manager->MasterMaterial ? Manager->MasterMaterial->GetPathName() : FString();
	if (IsMeshCompatibleShowcaseMaterialPath(CurrentMaterialPath))
	{
		ApplyMaterialIfLoaded(CurrentMaterialPath);
		return;
	}

	if (!Manager->LandscapeMigrationState.SourceLandscapeMaterialPath.IsEmpty())
	{
		const FString SourceMaterialPath = Manager->LandscapeMigrationState.SourceLandscapeMaterialPath;
		if (IsMeshCompatibleShowcaseMaterialPath(SourceMaterialPath) && ApplyMaterialIfLoaded(SourceMaterialPath))
		{
			return;
		}
	}

	if (ApplyMaterialIfLoaded(SHOWCASE_FALLBACK_TERRAIN_MATERIAL_PATH))
	{
		return;
	}

	ApplyMaterialIfLoaded(SHOWCASE_SECONDARY_TERRAIN_MATERIAL_PATH);
}

void ATerraDyneOrchestrator::EnsureGrassProfile(ATerraDyneManager* Manager)
{
	if (!Manager)
	{
		return;
	}

	const int32 GrassLayerIndex = ResolveShowcaseGrassLayerIndex(Manager);
	const float LayerPaintCoverage = MeasureShowcaseLayerPaintCoverage(Manager, GrassLayerIndex);
	const float MinGrassWeight = LayerPaintCoverage >= 0.05f ? 0.18f : 0.0f;
	UTerraDyneGrassProfile* Profile = Manager->ActiveGrassProfile;

	auto IsPackagedShowcaseGrassProfile = [](const UTerraDyneGrassProfile* InProfile) -> bool
	{
		return InProfile && InProfile->GetPathName().Equals(SHOWCASE_GRASS_PROFILE_PATH, ESearchCase::IgnoreCase);
	};

	if (!Profile || Profile->Varieties.Num() == 0)
	{
		if (UTerraDyneGrassProfile* PackagedProfile = LoadObject<UTerraDyneGrassProfile>(nullptr, SHOWCASE_GRASS_PROFILE_PATH))
		{
			Profile = DuplicateObject<UTerraDyneGrassProfile>(PackagedProfile, Manager);
			Manager->ActiveGrassProfile = Profile;
		}
	}
	else if (IsPackagedShowcaseGrassProfile(Profile))
	{
		Profile = DuplicateObject<UTerraDyneGrassProfile>(Profile, Manager);
		Manager->ActiveGrassProfile = Profile;
	}

	bool bShowcaseManagedProfile = Profile && Profile->GetOuter() == Manager;
	if (!Profile || Profile->Varieties.Num() == 0)
	{
		Profile = NewObject<UTerraDyneGrassProfile>(Manager);
		bShowcaseManagedProfile = true;

		struct FGrassCandidate
		{
			const TCHAR* MeshPath;
			float Density;
			FVector2D ScaleRange;
		};

		static const FGrassCandidate PackagedCandidates[] = {
			{TEXT("/TerraDyne/Samples/Meshes/SM_TerraDyne_GrassA.SM_TerraDyne_GrassA"), 0.45f, FVector2D(0.8f, 1.1f)},
			{TEXT("/TerraDyne/Samples/Meshes/SM_TerraDyne_GrassB.SM_TerraDyne_GrassB"), 0.25f, FVector2D(0.9f, 1.2f)},
			{TEXT("/TerraDyne/Samples/Meshes/SM_TerraDyne_GrassC.SM_TerraDyne_GrassC"), 0.2f, FVector2D(0.7f, 1.0f)}
		};

		static const FGrassCandidate LegacyCandidates[] = {
			{TEXT("/Game/MWLandscapeAutoMaterial/Meshes/Plants/SM_MWAM_GrassA.SM_MWAM_GrassA"), 0.45f, FVector2D(0.8f, 1.1f)},
			{TEXT("/Game/MWLandscapeAutoMaterial/Meshes/Plants/SM_MWAM_GrassB.SM_MWAM_GrassB"), 0.25f, FVector2D(0.9f, 1.2f)},
			{TEXT("/Game/MWLandscapeAutoMaterial/Meshes/Plants/SM_MWAM_GrassC.SM_MWAM_GrassC"), 0.2f, FVector2D(0.7f, 1.0f)}
		};

		auto AddCandidateVarieties = [&](const FGrassCandidate* Candidates, int32 CandidateCount)
		{
			for (int32 CandidateIndex = 0; CandidateIndex < CandidateCount; ++CandidateIndex)
			{
				const FGrassCandidate& Candidate = Candidates[CandidateIndex];
				if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Candidate.MeshPath))
				{
					FTerraDyneGrassVariety Variety;
					Variety.GrassMesh = Mesh;
					Variety.Density = Candidate.Density;
					Variety.WeightLayerIndex = GrassLayerIndex;
					Variety.MinWeight = MinGrassWeight;
					Variety.ScaleRange = Candidate.ScaleRange;
					Variety.bAlignToSurface = true;
					Variety.MaxSlopeAngle = 55.0f;
					Profile->Varieties.Add(Variety);
				}
			}
		};

		AddCandidateVarieties(PackagedCandidates, UE_ARRAY_COUNT(PackagedCandidates));
		if (Profile->Varieties.Num() == 0)
		{
			AddCandidateVarieties(LegacyCandidates, UE_ARRAY_COUNT(LegacyCandidates));
		}

		if (Profile->Varieties.Num() == 0)
		{
			UStaticMesh* GrassBlade = CreateRuntimeGrassBlade(Manager);
			UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			UMaterialInstanceDynamic* GreenMat = BaseMat ? UMaterialInstanceDynamic::Create(BaseMat, Manager) : nullptr;
			if (GreenMat)
			{
				GreenMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.20f, 0.46f, 0.18f, 1.0f));
			}

			FTerraDyneGrassVariety Variety;
			Variety.GrassMesh = GrassBlade;
			Variety.MaterialOverride = GreenMat;
			Variety.Density = 0.6f;
			Variety.WeightLayerIndex = GrassLayerIndex;
			Variety.MinWeight = MinGrassWeight;
			Variety.ScaleRange = FVector2D(0.65f, 1.2f);
			Variety.bAlignToSurface = true;
			Variety.MaxSlopeAngle = 60.0f;
			Profile->Varieties.Add(Variety);
		}

		Manager->ActiveGrassProfile = Profile;
	}

	if (bShowcaseManagedProfile)
	{
		for (FTerraDyneGrassVariety& Variety : Profile->Varieties)
		{
			Variety.WeightLayerIndex = GrassLayerIndex;
			Variety.MinWeight = MinGrassWeight;
		}
	}

	for (ATerraDyneChunk* Chunk : Manager->GetChunksInRadius(Manager->GetActorLocation(), Manager->GlobalChunkSize * 8.0f))
	{
		if (Chunk)
		{
			Chunk->SetGrassProfile(Profile);
			Chunk->RequestGrassRegen();
		}
	}
}

void ATerraDyneOrchestrator::EnsureBasePopulation(ATerraDyneManager* Manager)
{
	if (!Manager || !Manager->HasAuthority())
	{
		return;
	}

	auto MakeMeshDescriptor =
		[](FName TypeId,
		   ETerraDynePopulationKind Kind,
		   const TCHAR* MeshPath,
		   float TerrainOffset,
		   bool bAllowRegrowth,
		   float RegrowthSeconds,
		   const TArray<FName>& DescriptorTags)
	{
			FTerraDynePopulationDescriptor Descriptor;
			Descriptor.TypeId = TypeId;
			Descriptor.Kind = Kind;
			Descriptor.StaticMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(MeshPath));
			Descriptor.bReplicates = true;
			Descriptor.bSnapToTerrain = true;
			Descriptor.TerrainOffset = TerrainOffset;
			Descriptor.bAllowRegrowth = bAllowRegrowth;
			Descriptor.RegrowthDelaySeconds = RegrowthSeconds;
			Descriptor.Tags = DescriptorTags;
			return Descriptor;
		};

	if (!FindPopulationIdByTypeId(Manager, ShowcasePropTypeId).IsValid())
	{
		Manager->PlacePersistentActorFromDescriptor(
			MakeMeshDescriptor(ShowcasePropTypeId, ETerraDynePopulationKind::Prop, TEXT("/Engine/BasicShapes/Cube.Cube"), 55.0f, false, 0.0f, {TEXT("Showcase"), TEXT("Prop")}),
			FTransform(Manager->GetActorLocation() + FVector(-2200.0f, -1800.0f, 220.0f)));
	}

	if (!FindPopulationIdByTypeId(Manager, ShowcaseHarvestableTypeId).IsValid())
	{
		Manager->PlacePersistentActorFromDescriptor(
			MakeMeshDescriptor(ShowcaseHarvestableTypeId, ETerraDynePopulationKind::Harvestable, TEXT("/Engine/BasicShapes/Sphere.Sphere"), 95.0f, true, 3.5f, {TEXT("Showcase"), TEXT("Harvestable")}),
			FTransform(Manager->GetActorLocation() + FVector(-700.0f, 1300.0f, 220.0f)));
	}

	if (!FindPopulationIdByTypeId(Manager, ShowcaseDestroyableTypeId).IsValid())
	{
		FTerraDynePopulationDescriptor Descriptor;
		Descriptor.TypeId = ShowcaseDestroyableTypeId;
		Descriptor.Kind = ETerraDynePopulationKind::Destroyable;
		Descriptor.ActorClass = ATerraDyneDemoMarkerActor::StaticClass();
		Descriptor.bReplicates = true;
		Descriptor.bSnapToTerrain = true;
		Descriptor.TerrainOffset = 220.0f;
		Descriptor.Tags = {TEXT("Showcase"), TEXT("Destroyable")};
		Manager->PlacePersistentActorFromDescriptor(
			Descriptor,
			FTransform(Manager->GetActorLocation() + FVector(1800.0f, 700.0f, 320.0f)));
	}
}

FGuid ATerraDyneOrchestrator::FindPopulationIdByTypeId(const ATerraDyneManager* Manager, FName TypeId) const
{
	if (!Manager || TypeId == NAME_None)
	{
		return FGuid();
	}

	for (const FTerraDynePersistentPopulationEntry& Entry : Manager->PersistentPopulationEntries)
	{
		if (Entry.Descriptor.TypeId == TypeId)
		{
			return Entry.PopulationId;
		}
	}

	return FGuid();
}

void ATerraDyneOrchestrator::CaptureShowcaseBaseline()
{
	if (bShowcaseBaselineCaptured)
	{
		return;
	}

	if (ATerraDyneManager* Manager = GetManager())
	{
		Manager->SaveWorld(SHOWCASE_BASELINE_SLOT);
		bShowcaseBaselineCaptured = UGameplayStatics::DoesSaveGameExist(SHOWCASE_BASELINE_SLOT, 0);
	}
}

bool ATerraDyneOrchestrator::RestoreShowcaseBaseline()
{
	ATerraDyneManager* Manager = GetManager();
	if (!Manager || !bShowcaseBaselineCaptured || !UGameplayStatics::DoesSaveGameExist(SHOWCASE_BASELINE_SLOT, 0))
	{
		return false;
	}

	Manager->LoadWorld(SHOWCASE_BASELINE_SLOT);
	BindManagerEvents(Manager);
	return true;
}

int32 ATerraDyneOrchestrator::ResolveShowcaseGrassLayerIndex(ATerraDyneManager* Manager) const
{
	if (!Manager)
	{
		return 1;
	}

	TArray<float> LayerCoverage;
	LayerCoverage.Init(0.0f, ATerraDyneChunk::NumWeightLayers);
	int32 SampledChunks = 0;

	for (ATerraDyneChunk* Chunk : Manager->GetChunksInRadius(Manager->GetActorLocation(), Manager->GlobalChunkSize * 6.0f))
	{
		if (!Chunk)
		{
			continue;
		}

		++SampledChunks;
		for (int32 LayerIndex = 0; LayerIndex < ATerraDyneChunk::NumWeightLayers; ++LayerIndex)
		{
			const TArray<float>& WeightBuffer = Chunk->WeightBuffers[LayerIndex];
			if (WeightBuffer.Num() == 0)
			{
				continue;
			}

			double Sum = 0.0;
			for (const float WeightValue : WeightBuffer)
			{
				Sum += WeightValue;
			}
			LayerCoverage[LayerIndex] += static_cast<float>(Sum / WeightBuffer.Num());
		}
	}

	if (SampledChunks > 0)
	{
		for (float& Coverage : LayerCoverage)
		{
			Coverage /= SampledChunks;
		}
	}

	int32 BestLayerIndex = 1;
	float BestScore = -BIG_NUMBER;
	for (int32 LayerIndex = 0; LayerIndex < ATerraDyneChunk::NumWeightLayers; ++LayerIndex)
	{
		float Score = LayerCoverage[LayerIndex] * 6.0f;
		if (LayerIndex == 1)
		{
			Score += 0.5f;
		}

		for (const FTerraDyneLandscapeLayerMapping& Mapping : Manager->LandscapeMigrationState.LayerMappings)
		{
			if (Mapping.TerraDyneWeightLayerIndex == LayerIndex)
			{
				Score += ScoreLandscapeLayerName(Mapping.SourceLayerName);
			}
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestLayerIndex = LayerIndex;
		}
	}

	return FMath::Clamp(BestLayerIndex, 0, ATerraDyneChunk::NumWeightLayers - 1);
}

float ATerraDyneOrchestrator::MeasureShowcaseLayerPaintCoverage(ATerraDyneManager* Manager, int32 LayerIndex) const
{
	if (!Manager || LayerIndex < 0 || LayerIndex >= ATerraDyneChunk::NumWeightLayers)
	{
		return 0.0f;
	}

	int32 TotalSamples = 0;
	int32 PaintedSamples = 0;
	for (ATerraDyneChunk* Chunk : Manager->GetChunksInRadius(Manager->GetActorLocation(), Manager->GlobalChunkSize * 6.0f))
	{
		if (!Chunk)
		{
			continue;
		}

		const TArray<float>& WeightBuffer = Chunk->WeightBuffers[LayerIndex];
		TotalSamples += WeightBuffer.Num();
		for (const float WeightValue : WeightBuffer)
		{
			if (WeightValue >= 0.1f)
			{
				++PaintedSamples;
			}
		}
	}

	return TotalSamples > 0 ? (static_cast<float>(PaintedSamples) / static_cast<float>(TotalSamples)) : 0.0f;
}

void ATerraDyneOrchestrator::EnsureShowcaseWorld()
{
	ATerraDyneManager* Manager = EnsureShowcaseManager();
	if (!Manager)
	{
		return;
	}

	if (!Manager->WorldPreset)
	{
		Manager->WorldPreset = BuildFallbackPreset();
		if (Manager->WorldPreset)
		{
			Manager->ApplyWorldPreset(Manager->WorldPreset);
		}
	}
	else
	{
		Manager->ApplyWorldPreset();
	}

	ATerraDyneChunk* PrimaryChunk = EnsurePrimaryAuthoredChunk(Manager);
	EnsureShowcaseTerrainMaterial(Manager);
	EnsureFallbackFoliagePresentation(Manager, PrimaryChunk);
	EnsureGrassProfile(Manager);
	EnsureBasePopulation(Manager);
	BindManagerEvents(Manager);
}

void ATerraDyneOrchestrator::BindManagerEvents(ATerraDyneManager* Manager)
{
	if (!Manager || BoundManager.Get() == Manager)
	{
		return;
	}

	UnbindManagerEvents();
	Manager->OnTerrainChanged.AddDynamic(this, &ATerraDyneOrchestrator::HandleTerrainChanged);
	Manager->OnFoliageChanged.AddDynamic(this, &ATerraDyneOrchestrator::HandleFoliageChanged);
	Manager->OnPopulationChanged.AddDynamic(this, &ATerraDyneOrchestrator::HandlePopulationChanged);
	BoundManager = Manager;
}

void ATerraDyneOrchestrator::UnbindManagerEvents()
{
	if (ATerraDyneManager* Manager = BoundManager.Get())
	{
		Manager->OnTerrainChanged.RemoveDynamic(this, &ATerraDyneOrchestrator::HandleTerrainChanged);
		Manager->OnFoliageChanged.RemoveDynamic(this, &ATerraDyneOrchestrator::HandleFoliageChanged);
		Manager->OnPopulationChanged.RemoveDynamic(this, &ATerraDyneOrchestrator::HandlePopulationChanged);
	}

	BoundManager.Reset();
}

void ATerraDyneOrchestrator::ClearShowcaseTimers()
{
	if (!GetWorld())
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(PersistenceMutationTimer);
	GetWorld()->GetTimerManager().ClearTimer(PersistenceRestoreTimer);
}

void ATerraDyneOrchestrator::HandleTerrainChanged(const FTerraDyneTerrainChangeEvent& Event)
{
	++TerrainEventCount;
	LastTerrainEventCoord = Event.ChunkCoord;
}

void ATerraDyneOrchestrator::HandleFoliageChanged(const FTerraDyneFoliageChangeEvent& Event)
{
	++FoliageEventCount;
	LastFoliageEventInstances = Event.ImportedFoliageInstanceCount;
}

void ATerraDyneOrchestrator::HandlePopulationChanged(const FTerraDynePopulationChangeEvent& Event)
{
	++PopulationEventCount;
	LastPopulationReason = Event.Reason;
	LastPopulationTypeId = Event.TypeId;
}

// ---- Camera Waypoint System ----

void ATerraDyneOrchestrator::SetCameraTrack(const TArray<FShowcaseCameraWaypoint>& Track)
{
	CameraTrack = Track;
	CameraWaypointIndex = 0;
	CameraWaypointTimer = 0.0f;
	bCameraInTransit = Track.Num() > 0;
	bCameraEnabled = Track.Num() > 0;

	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		CameraTransitStartLocation = Pawn->GetActorLocation();
		CameraTransitStartRotation = Pawn->GetActorRotation();
	}
}

void ATerraDyneOrchestrator::UpdateCamera(float DeltaTime)
{
	if (!bCameraEnabled || CameraTrack.Num() == 0)
	{
		return;
	}

	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn || !CameraTrack.IsValidIndex(CameraWaypointIndex))
	{
		return;
	}

	const FShowcaseCameraWaypoint& WP = CameraTrack[CameraWaypointIndex];
	CameraWaypointTimer += DeltaTime;

	if (bCameraInTransit)
	{
		const float Alpha = WP.TransitDuration > 0.0f
			? FMath::Clamp(CameraWaypointTimer / WP.TransitDuration, 0.0f, 1.0f)
			: 1.0f;
		const float Smooth = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
		Pawn->SetActorLocation(FMath::Lerp(CameraTransitStartLocation, WP.Location, Smooth));
		Pawn->SetActorRotation(FMath::Lerp(CameraTransitStartRotation, WP.Rotation, Smooth));

		if (Alpha >= 1.0f)
		{
			Pawn->SetActorLocation(WP.Location);
			Pawn->SetActorRotation(WP.Rotation);
			CameraWaypointTimer = 0.0f;
			bCameraInTransit = false;
		}
	}
	else if (CameraWaypointTimer >= WP.HoldDuration)
	{
		CameraWaypointTimer = 0.0f;
		++CameraWaypointIndex;
		if (CameraTrack.IsValidIndex(CameraWaypointIndex))
		{
			CameraTransitStartLocation = WP.Location;
			CameraTransitStartRotation = WP.Rotation;
			bCameraInTransit = true;
		}
	}
}

// ---- Overlay Helpers ----

void ATerraDyneOrchestrator::SetOverlay(const FString& Title, const FString& Description, const FString& Detail)
{
	if (!GEngine)
	{
		return;
	}

	GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_TITLE, 999.0f, FColor::Yellow, Title);
	GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DESC, 999.0f, FColor::White, Description);
	if (!Detail.IsEmpty())
	{
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 999.0f, FColor::Silver, Detail);
	}
}

void ATerraDyneOrchestrator::ClearOverlay()
{
	if (!GEngine)
	{
		return;
	}

	GEngine->RemoveOnScreenDebugMessage(OVERLAY_KEY_TITLE);
	GEngine->RemoveOnScreenDebugMessage(OVERLAY_KEY_DESC);
	GEngine->RemoveOnScreenDebugMessage(OVERLAY_KEY_DETAIL);
	GEngine->RemoveOnScreenDebugMessage(OVERLAY_KEY_AUX);
}

// ---- Phase Transition ----

EShowcasePhase ATerraDyneOrchestrator::GetNextPhase(EShowcasePhase Current) const
{
	const int32 CurrentIndex = GetPhaseIndex(Current);
	if (CurrentIndex != INDEX_NONE && ShowcaseSequence.IsValidIndex(CurrentIndex + 1))
	{
		return ShowcaseSequence[CurrentIndex + 1];
	}

	return EShowcasePhase::Interactive;
}

void ATerraDyneOrchestrator::AdvancePhase()
{
	PhaseTimer = 0.0f;
	ActionTimer = 0.0f;
	UndoRedoDemoStep = 0;
	UndoRedoSubTimer = 0.0f;
	CurrentPhase = GetNextPhase(CurrentPhase);
	UE_LOG(LogTerraDyne, Log, TEXT("SHOWCASE: Phase -> %d"), static_cast<int32>(CurrentPhase));
	OnPhaseEnter(CurrentPhase);
}

void ATerraDyneOrchestrator::OnPhaseEnter(EShowcasePhase Phase)
{
	ClearOverlay();
	
	if (!bInteractiveOnly)
	{
		if (Phase == EShowcasePhase::Interactive)
		{
			RestoreShowcaseBaseline();
		}
		EnsureShowcaseWorld();

		const int32 PhaseIndex = GetPhaseIndex(Phase);
		const int32 PhaseCount = ShowcaseSequence.Num();
		if (const FShowcasePhaseConfig* Config = PhaseConfigs.Find(Phase))
		{
			SetOverlay(
				FString::Printf(TEXT("[%d/%d] %s"), PhaseIndex + 1, PhaseCount, *Config->Title),
				Config->Description
			);
		}
	}

	UndoRedoDemoStep = 0;
	UndoRedoSubTimer = 0.0f;
	bPopulationRuntimePlacementTriggered = false;
	bPopulationHarvestTriggered = false;
	bPopulationDestroyTriggered = false;
	bPopulationRestoreTriggered = false;
	bGameplayPulseTriggered = false;

	const ATerraDyneManager* Manager = GetManager();
	const float ChunkSize = Manager ? Manager->GlobalChunkSize : 10000.0f;

	switch (Phase)
	{
	case EShowcasePhase::Warmup:
		SetCameraTrack({
			{FVector(-3400.0f, -3400.0f, 3200.0f), FRotator(-28.0f, 45.0f, 0.0f), 0.4f, 3.6f}
		});
		break;

	case EShowcasePhase::AuthoredWorldDemo:
		SetCameraTrack({
			{FVector(-ChunkSize * 0.34f, -ChunkSize * 0.28f, 2400.0f), FRotator(-22.0f, 42.0f, 0.0f), 0.9f, 1.6f},
			{FVector(-900.0f, 1100.0f, 900.0f), FRotator(-10.0f, 10.0f, 0.0f), 2.0f, 1.6f},
			{FVector(1700.0f, -1300.0f, 980.0f), FRotator(-12.0f, -145.0f, 0.0f), 2.0f, 1.4f}
		});
		break;

	case EShowcasePhase::SculptingDemo:
		SetCameraTrack({
			{FVector(-2200.0f, -2200.0f, 2600.0f), FRotator(-35.0f, 45.0f, 0.0f), 0.8f, 1.5f},
			{FVector(2200.0f, -2200.0f, 2500.0f), FRotator(-34.0f, -45.0f, 0.0f), 1.8f, 1.5f},
			{FVector(2200.0f, 2200.0f, 2500.0f), FRotator(-34.0f, -135.0f, 0.0f), 1.8f, 1.4f},
			{FVector(-2200.0f, 2200.0f, 2500.0f), FRotator(-34.0f, 135.0f, 0.0f), 1.8f, 1.3f}
		});
		break;

	case EShowcasePhase::LayerDemo:
		SetCameraTrack({
			{FVector(0.0f, -3200.0f, 2400.0f), FRotator(-24.0f, 90.0f, 0.0f), 1.0f, 5.0f}
		});
		break;

	case EShowcasePhase::PaintDemo:
		SetCameraTrack({
			{FVector(0.0f, 0.0f, 4200.0f), FRotator(-82.0f, 0.0f, 0.0f), 1.2f, 4.8f}
		});
		break;

	case EShowcasePhase::UndoRedoDemo:
		SetCameraTrack({
			{FVector(-1600.0f, -1600.0f, 2200.0f), FRotator(-38.0f, 45.0f, 0.0f), 1.0f, 6.5f}
		});
		break;

	case EShowcasePhase::PopulationDemo:
		SetCameraTrack({
			{FVector(-2600.0f, 300.0f, 1400.0f), FRotator(-16.0f, 10.0f, 0.0f), 1.1f, 2.2f},
			{FVector(1500.0f, 1000.0f, 1600.0f), FRotator(-18.0f, -150.0f, 0.0f), 1.8f, 2.2f}
		});
		break;

	case EShowcasePhase::ProceduralWorldDemo:
		SetCameraTrack({
			{FVector(0.0f, -2400.0f, 1800.0f), FRotator(-16.0f, 72.0f, 0.0f), 1.0f, 1.2f},
			{FVector(ChunkSize * 2.3f, 0.0f, 2100.0f), FRotator(-18.0f, 180.0f, 0.0f), 3.2f, 2.2f},
			{FVector(ChunkSize * 2.5f, 1800.0f, 1100.0f), FRotator(-10.0f, -135.0f, 0.0f), 2.4f, 1.0f}
		});
		break;

	case EShowcasePhase::GameplayHooksDemo:
		SetCameraTrack({
			{FVector(-800.0f, -3200.0f, 1900.0f), FRotator(-18.0f, 65.0f, 0.0f), 1.0f, 2.5f},
			{FVector(ChunkSize * 2.1f, -1800.0f, 2200.0f), FRotator(-18.0f, 135.0f, 0.0f), 2.2f, 2.1f}
		});
		break;

	case EShowcasePhase::DesignerWorkflowDemo:
		SetCameraTrack({
			{FVector(0.0f, 0.0f, 3200.0f), FRotator(-70.0f, 15.0f, 0.0f), 1.1f, 4.9f}
		});
		break;

	case EShowcasePhase::PersistenceDemo:
		SetCameraTrack({
			{FVector(-3200.0f, -3200.0f, 3100.0f), FRotator(-28.0f, 45.0f, 0.0f), 1.0f, 4.0f}
		});
		StartPersistenceTest();
		break;

	case EShowcasePhase::ReplicationDemo:
		SetCameraTrack({
			{FVector(-3400.0f, -2600.0f, 2500.0f), FRotator(-22.0f, 40.0f, 0.0f), 1.0f, 1.4f},
			{FVector(-2800.0f, 2600.0f, 2500.0f), FRotator(-22.0f, -40.0f, 0.0f), 2.2f, 1.4f}
		});
		break;

	case EShowcasePhase::LODDemo:
		bCameraEnabled = false;
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			OriginalPlayerPos = Pawn->GetActorLocation();
		}
		if (ATerraDyneManager* ActiveManager = GetManager())
		{
			ActiveManager->bStreamingPaused = true;
		}
		break;

	case EShowcasePhase::Interactive:
		bCameraEnabled = false;
		if (ATerraDyneManager* ActiveManager = GetManager())
		{
			ActiveManager->bStreamingPaused = false;
			if (!bInteractiveOnly)
			{
				if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
				{
					Pawn->SetActorLocation(ActiveManager->GetActorLocation() + FVector(-2600.0f, -2600.0f, 2200.0f));
					Pawn->SetActorRotation(FRotator(-24.0f, 45.0f, 0.0f));
				}
			}
		}
		break;
	}
}

// ---- Main Tick ----

void ATerraDyneOrchestrator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	PhaseTimer += DeltaTime;
	UpdateCamera(DeltaTime);

	float PhaseDuration = 0.0f;
	if (const FShowcasePhaseConfig* Config = PhaseConfigs.Find(CurrentPhase))
	{
		PhaseDuration = Config->Duration;
	}

	switch (CurrentPhase)
	{
	case EShowcasePhase::Warmup:
		break;

	case EShowcasePhase::AuthoredWorldDemo:
		UpdateAuthoredWorldDemo(DeltaTime);
		break;

	case EShowcasePhase::SculptingDemo:
		UpdateSculptingDemo(DeltaTime);
		break;

	case EShowcasePhase::LayerDemo:
		UpdateLayerDemo(DeltaTime);
		break;

	case EShowcasePhase::PaintDemo:
		UpdatePaintDemo(DeltaTime);
		break;

	case EShowcasePhase::UndoRedoDemo:
		UpdateUndoRedoDemo(DeltaTime);
		break;

	case EShowcasePhase::PopulationDemo:
		UpdatePopulationDemo(DeltaTime);
		break;

	case EShowcasePhase::ProceduralWorldDemo:
		UpdateProceduralWorldDemo(DeltaTime);
		break;

	case EShowcasePhase::GameplayHooksDemo:
		UpdateGameplayHooksDemo(DeltaTime);
		break;

	case EShowcasePhase::DesignerWorkflowDemo:
		UpdateDesignerWorkflowDemo(DeltaTime);
		break;

	case EShowcasePhase::PersistenceDemo:
		UpdatePersistenceDemo(DeltaTime);
		break;

	case EShowcasePhase::ReplicationDemo:
		UpdateReplicationDemo(DeltaTime);
		break;

	case EShowcasePhase::LODDemo:
		UpdateLODDemo(DeltaTime);
		break;

	case EShowcasePhase::Interactive:
		if (bIsClicking)
		{
			PerformToolAction();
		}
		break;
	}

	if (PhaseDuration > 0.0f && PhaseTimer >= PhaseDuration)
	{
		AdvancePhase();
	}
}

// ---- Phase: Sculpting Demo (Reworked) ----
// 4 sub-phases at fixed quadrant positions: Raise(3s) -> Lower(3s) -> Smooth(3s) -> Flatten(3s)

void ATerraDyneOrchestrator::UpdateSculptingDemo(float DeltaTime)
{
	ActionTimer += DeltaTime;
	if (ActionTimer < 0.15f)
	{
		return;
	}
	ActionTimer = 0.0f;

	ATerraDyneManager* Man = GetManager();
	if (!Man)
	{
		return;
	}

	struct FSculptSubPhase
	{
		FVector Position;
		ETerraDyneBrushMode Mode;
		float Strength;
		const TCHAR* Label;
	};

	static const FSculptSubPhase SubPhases[] = {
		{FVector(-1600.0f, -1600.0f, 0.0f), ETerraDyneBrushMode::Raise, 1200.0f, TEXT("Raise")},
		{FVector(1600.0f, -1600.0f, 0.0f), ETerraDyneBrushMode::Lower, 1200.0f, TEXT("Lower")},
		{FVector(-1600.0f, 1600.0f, 0.0f), ETerraDyneBrushMode::Smooth, 1500.0f, TEXT("Smooth")},
		{FVector(1600.0f, 1600.0f, 0.0f), ETerraDyneBrushMode::Flatten, 1200.0f, TEXT("Flatten")},
	};

	const int32 SubIdx = FMath::Clamp(static_cast<int32>(PhaseTimer / 2.5f), 0, 3);
	const FSculptSubPhase& SP = SubPhases[SubIdx];
	const FVector Jitter(FMath::FRandRange(-160.0f, 160.0f), FMath::FRandRange(-160.0f, 160.0f), 0.0f);
	Man->ApplyGlobalBrush(Man->GetActorLocation() + SP.Position + Jitter, 800.0f, SP.Strength, SP.Mode);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.2f, FColor::Cyan, FString::Printf(TEXT("  Active brush: %s"), SP.Label));
	}
}

// ---- Phase: Layer Demo ----
// Switch Base/Sculpt/Detail layers, apply strokes on each (2s per layer)

void ATerraDyneOrchestrator::UpdateLayerDemo(float DeltaTime)
{
	ActionTimer += DeltaTime;
	if (ActionTimer < 0.15f)
	{
		return;
	}
	ActionTimer = 0.0f;

	ATerraDyneManager* Man = GetManager();
	if (!Man)
	{
		return;
	}

	struct FLayerSubPhase
	{
		ETerraDyneLayer Layer;
		FVector Position;
		const TCHAR* Label;
	};

	static const FLayerSubPhase SubPhases[] = {
		{ETerraDyneLayer::Base, FVector(-1200.0f, 0.0f, 0.0f), TEXT("Base")},
		{ETerraDyneLayer::Sculpt, FVector(0.0f, 0.0f, 0.0f), TEXT("Sculpt")},
		{ETerraDyneLayer::Detail, FVector(1200.0f, 0.0f, 0.0f), TEXT("Detail")},
	};

	int32 SubIdx = FMath::Clamp((int32)(PhaseTimer / 2.0f), 0, 2);
	const FLayerSubPhase& LP = SubPhases[SubIdx];

	Man->ActiveLayer = LP.Layer;

	const FVector Jitter(FMath::FRandRange(-120.0f, 120.0f), FMath::FRandRange(-120.0f, 120.0f), 0.0f);
	Man->ApplyGlobalBrush(Man->GetActorLocation() + LP.Position + Jitter, 650.0f, 820.0f, ETerraDyneBrushMode::Raise, LP.Layer);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.2f, FColor::Cyan, FString::Printf(TEXT("  Active layer: %s"), LP.Label));
	}
}

// ---- Phase: Paint Demo ----
// Paint weight layers 0-3 at 4 positions (2s per layer)

void ATerraDyneOrchestrator::UpdatePaintDemo(float DeltaTime)
{
	ActionTimer += DeltaTime;
	if (ActionTimer < 0.15f)
	{
		return;
	}
	ActionTimer = 0.0f;

	ATerraDyneManager* Man = GetManager();
	if (!Man)
	{
		return;
	}

	struct FPaintSubPhase
	{
		int32 LayerIndex;
		FVector Position;
		FColor DebugColor;
		const TCHAR* Label;
	};

	static const FPaintSubPhase SubPhases[] = {
		{0, FVector(-1200.0f, -1200.0f, 0.0f), FColor::Red, TEXT("Layer 0")},
		{1, FVector(1200.0f, -1200.0f, 0.0f), FColor::Green, TEXT("Layer 1")},
		{2, FVector(-1200.0f, 1200.0f, 0.0f), FColor::Blue, TEXT("Layer 2")},
		{3, FVector(1200.0f, 1200.0f, 0.0f), FColor::Yellow, TEXT("Layer 3")},
	};

	const int32 SubIdx = FMath::Clamp(static_cast<int32>(PhaseTimer / 1.5f), 0, 3);
	const FPaintSubPhase& PP = SubPhases[SubIdx];

	const FVector Jitter(FMath::FRandRange(-90.0f, 90.0f), FMath::FRandRange(-90.0f, 90.0f), 0.0f);
	Man->ApplyGlobalBrush(Man->GetActorLocation() + PP.Position + Jitter, 780.0f, 1.0f, ETerraDyneBrushMode::Paint, ETerraDyneLayer::Sculpt, PP.LayerIndex);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.2f, PP.DebugColor, FString::Printf(TEXT("  Painting %s"), PP.Label));
	}
}

// ---- Phase: Undo/Redo Demo ----
// Sub-state: Stroke1(1s) -> Stroke2(1s) -> Undo x2(2s) -> pause(1s) -> Redo x2(2s) -> pause(1s)

void ATerraDyneOrchestrator::UpdateUndoRedoDemo(float DeltaTime)
{
	ATerraDyneManager* Man = GetManager();
	if (!Man) return;
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC) return;

	UndoRedoSubTimer += DeltaTime;

	auto ShowStep = [this](const TCHAR* StepText)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.5f, FColor::Orange, StepText);
		}
	};

	switch (UndoRedoDemoStep)
	{
	case 0:
		if (UndoRedoSubTimer < 0.1f)
		{
			Man->BeginStroke(Man->GetActorLocation() + FVector(-600.0f, -600.0f, 0.0f), 650.0f, PC);
		}
		{
			ActionTimer += DeltaTime;
			if (ActionTimer >= 0.15f)
			{
				ActionTimer = 0.0f;
				const FVector Jitter(FMath::FRandRange(-100.0f, 100.0f), FMath::FRandRange(-100.0f, 100.0f), 0.0f);
				Man->ApplyGlobalBrush(Man->GetActorLocation() + FVector(-600.0f, -600.0f, 0.0f) + Jitter, 650.0f, 980.0f, ETerraDyneBrushMode::Raise);
			}
		}
		ShowStep(TEXT("  Stroke 1: raise terrain"));
		if (UndoRedoSubTimer >= 1.0f)
		{
			Man->CommitStroke(PC);
			UndoRedoDemoStep = 1;
			UndoRedoSubTimer = 0.0f;
			ActionTimer = 0.0f;
		}
		break;

	case 1:
		if (UndoRedoSubTimer < 0.1f)
		{
			Man->BeginStroke(Man->GetActorLocation() + FVector(700.0f, 700.0f, 0.0f), 650.0f, PC);
		}
		{
			ActionTimer += DeltaTime;
			if (ActionTimer >= 0.15f)
			{
				ActionTimer = 0.0f;
				const FVector Jitter(FMath::FRandRange(-100.0f, 100.0f), FMath::FRandRange(-100.0f, 100.0f), 0.0f);
				Man->ApplyGlobalBrush(Man->GetActorLocation() + FVector(700.0f, 700.0f, 0.0f) + Jitter, 650.0f, 980.0f, ETerraDyneBrushMode::Raise);
			}
		}
		ShowStep(TEXT("  Stroke 2: raise terrain"));
		if (UndoRedoSubTimer >= 1.0f)
		{
			Man->CommitStroke(PC);
			UndoRedoDemoStep = 2;
			UndoRedoSubTimer = 0.0f;
		}
		break;

	case 2:
		ShowStep(TEXT("  Preparing to undo"));
		if (UndoRedoSubTimer >= 0.5f)
		{
			UndoRedoDemoStep = 3;
			UndoRedoSubTimer = 0.0f;
		}
		break;

	case 3:
		if (UndoRedoSubTimer < 0.1f)
		{
			Man->Undo(PC);
		}
		ShowStep(TEXT("  >> UNDO (1/2)"));
		if (UndoRedoSubTimer >= 1.0f)
		{
			UndoRedoDemoStep = 4;
			UndoRedoSubTimer = 0.0f;
		}
		break;

	case 4:
		if (UndoRedoSubTimer < 0.1f)
		{
			Man->Undo(PC);
		}
		ShowStep(TEXT("  >> UNDO (2/2)"));
		if (UndoRedoSubTimer >= 1.0f)
		{
			UndoRedoDemoStep = 5;
			UndoRedoSubTimer = 0.0f;
		}
		break;

	case 5:
		ShowStep(TEXT("  Terrain restored"));
		if (UndoRedoSubTimer >= 1.0f)
		{
			UndoRedoDemoStep = 6;
			UndoRedoSubTimer = 0.0f;
		}
		break;

	case 6:
		if (UndoRedoSubTimer < 0.1f)
		{
			Man->Redo(PC);
		}
		ShowStep(TEXT("  >> REDO (1/2)"));
		if (UndoRedoSubTimer >= 1.0f)
		{
			UndoRedoDemoStep = 7;
			UndoRedoSubTimer = 0.0f;
		}
		break;

	case 7:
		if (UndoRedoSubTimer < 0.1f)
		{
			Man->Redo(PC);
		}
		ShowStep(TEXT("  >> REDO (2/2)"));
		if (UndoRedoSubTimer >= 1.0f)
		{
			UndoRedoDemoStep = 8;
			UndoRedoSubTimer = 0.0f;
		}
		break;

	case 8:
		ShowStep(TEXT("  Strokes re-applied"));
		break;
	}
}

void ATerraDyneOrchestrator::UpdateAuthoredWorldDemo(float DeltaTime)
{
	ActionTimer += DeltaTime;
	if (ActionTimer < 0.18f)
	{
		return;
	}
	ActionTimer = 0.0f;

	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(FIntPoint::ZeroValue);
	if (!Chunk)
	{
		return;
	}

	const FVector Origin = Manager->GetActorLocation();
	if (PhaseTimer < 4.0f)
	{
		const FVector Jitter(FMath::FRandRange(-120.0f, 120.0f), FMath::FRandRange(-120.0f, 120.0f), 0.0f);
		Manager->ApplyGlobalBrush(Origin + FVector(-900.0f, 1100.0f, 0.0f) + Jitter, 700.0f, 420.0f, ETerraDyneBrushMode::Raise);
	}
	else if (PhaseTimer < 6.5f)
	{
		const FVector Jitter(FMath::FRandRange(-140.0f, 140.0f), FMath::FRandRange(-140.0f, 140.0f), 0.0f);
		Manager->ApplyGlobalBrush(Origin + FVector(1700.0f, -1300.0f, 0.0f) + Jitter, 780.0f, 360.0f, ETerraDyneBrushMode::Smooth);
	}

	GEngine->AddOnScreenDebugMessage(
		OVERLAY_KEY_DETAIL,
		0.25f,
		FColor::Cyan,
		FString::Printf(
			TEXT("  Authored core: %s | paint layers: %d | transferred foliage: %d | actor foliage: %d"),
			Chunk->bIsAuthoredChunk ? TEXT("Runtime-converted") : TEXT("Demo bootstrap"),
			ATerraDyneChunk::NumWeightLayers,
			Chunk->GetTransferredFoliageInstanceCount(),
			Chunk->GetTransferredActorFoliageInstanceCount()));
}

void ATerraDyneOrchestrator::UpdatePopulationDemo(float DeltaTime)
{
	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	EnsureBasePopulation(Manager);

	if (!bPopulationRuntimePlacementTriggered && PhaseTimer >= 0.6f && Manager->HasAuthority())
	{
		FTerraDynePopulationDescriptor Descriptor;
		Descriptor.TypeId = ShowcaseRuntimeTypeId;
		Descriptor.Kind = ETerraDynePopulationKind::RuntimePlaced;
		Descriptor.ActorClass = ATerraDyneDemoMarkerActor::StaticClass();
		Descriptor.bReplicates = true;
		Descriptor.bSnapToTerrain = true;
		Descriptor.TerrainOffset = 220.0f;
		Descriptor.Tags = {TEXT("Showcase"), TEXT("RuntimePlaced")};
		Manager->PlacePersistentActorFromDescriptor(
			Descriptor,
			FTransform(Manager->GetActorLocation() + FVector(0.0f, 2400.0f, 320.0f)));
		bPopulationRuntimePlacementTriggered = true;
	}

	const FGuid HarvestableId = FindPopulationIdByTypeId(Manager, ShowcaseHarvestableTypeId);
	const FGuid DestroyableId = FindPopulationIdByTypeId(Manager, ShowcaseDestroyableTypeId);
	if (!bPopulationHarvestTriggered && HarvestableId.IsValid() && PhaseTimer >= 2.0f)
	{
		Manager->HarvestPersistentPopulation(HarvestableId);
		bPopulationHarvestTriggered = true;
	}
	if (!bPopulationDestroyTriggered && DestroyableId.IsValid() && PhaseTimer >= 4.0f)
	{
		Manager->SetPersistentPopulationDestroyed(DestroyableId, true);
		bPopulationDestroyTriggered = true;
	}
	if (!bPopulationRestoreTriggered && DestroyableId.IsValid() && PhaseTimer >= 6.5f)
	{
		Manager->SetPersistentPopulationDestroyed(DestroyableId, false);
		bPopulationRestoreTriggered = true;
	}

	FString HarvestableState = TEXT("Missing");
	FString DestroyableState = TEXT("Missing");
	FString RuntimeState = TEXT("Missing");
	for (const FTerraDynePersistentPopulationEntry& Entry : Manager->PersistentPopulationEntries)
	{
		const FTransform WorldTransform = Entry.LocalTransform * Manager->GetActorTransform();
		if (Entry.State == ETerraDynePopulationState::Active)
		{
			DrawDebugSphere(GetWorld(), WorldTransform.GetLocation(), 90.0f, 10, FColor::Green, false, 0.2f, 0, 2.0f);
		}

		if (Entry.Descriptor.TypeId == ShowcaseHarvestableTypeId)
		{
			HarvestableState = PopulationStateToString(Entry.State);
		}
		else if (Entry.Descriptor.TypeId == ShowcaseDestroyableTypeId)
		{
			DestroyableState = PopulationStateToString(Entry.State);
		}
		else if (Entry.Descriptor.TypeId == ShowcaseRuntimeTypeId)
		{
			RuntimeState = PopulationStateToString(Entry.State);
		}
	}

	GEngine->AddOnScreenDebugMessage(
		OVERLAY_KEY_DETAIL,
		0.25f,
		FColor::Cyan,
		FString::Printf(
			TEXT("  entries: %d | harvestable: %s | destroyable: %s | runtime placed: %s"),
			Manager->GetPersistentPopulationCount(),
			*HarvestableState,
			*DestroyableState,
			*RuntimeState));
}

void ATerraDyneOrchestrator::UpdateProceduralWorldDemo(float DeltaTime)
{
	ATerraDyneManager* Manager = GetManager();
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Manager || !Pawn)
	{
		return;
	}

	const FVector ProbeLocation = Pawn->GetActorLocation();
	const FIntPoint ChunkCoord = ComputeChunkCoord(*Manager, ProbeLocation);
	const FName BiomeTag = Manager->GetBiomeTagAtLocation(ProbeLocation);
	const TArray<FTerraDynePCGPoint> PCGPoints = Manager->GetPCGSeedPointsForChunk(ChunkCoord, true, false);

	int32 AuthoredChunks = 0;
	int32 ProceduralChunks = 0;
	for (ATerraDyneChunk* Chunk : Manager->GetChunksInRadius(Manager->GetActorLocation(), Manager->GlobalChunkSize * 8.0f))
	{
		if (!Chunk)
		{
			continue;
		}

		if (Chunk->bIsAuthoredChunk)
		{
			++AuthoredChunks;
		}
		else
		{
			++ProceduralChunks;
		}
	}

	for (const FTerraDynePCGPoint& Point : PCGPoints)
	{
		DrawDebugSphere(GetWorld(), Point.Transform.GetLocation(), 80.0f, 8, FColor::Magenta, false, 0.2f, 0, 1.5f);
	}

	GEngine->AddOnScreenDebugMessage(
		OVERLAY_KEY_DETAIL,
		0.25f,
		FColor::Cyan,
		FString::Printf(
			TEXT("  chunk [%d,%d] | biome: %s | authored loaded: %d | procedural loaded: %d | PCG points: %d"),
			ChunkCoord.X,
			ChunkCoord.Y,
			*BiomeTag.ToString(),
			AuthoredChunks,
			ProceduralChunks,
			PCGPoints.Num()));
}

void ATerraDyneOrchestrator::UpdateGameplayHooksDemo(float DeltaTime)
{
	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	const FVector TownProbe = Manager->GetActorLocation() + FVector(0.0f, 0.0f, 200.0f);
	const FVector OutskirtsProbe = Manager->GetActorLocation() + FVector(Manager->GlobalChunkSize * 2.3f, 0.0f, 200.0f);
	if (!bGameplayPulseTriggered)
	{
		Manager->ApplyGlobalBrush(TownProbe, 700.0f, 220.0f, ETerraDyneBrushMode::Raise);
		bGameplayPulseTriggered = true;
	}

	for (const FTerraDyneBuildPermissionZone& Zone : Manager->BuildPermissionZones)
	{
		const FBox WorldBounds = Zone.LocalBounds.ShiftBy(Manager->GetActorLocation());
		DrawDebugBox(GetWorld(), WorldBounds.GetCenter(), WorldBounds.GetExtent(), Zone.Permission == ETerraDyneBuildPermission::Blocked ? FColor::Red : FColor::Green, false, 0.2f, 0, 3.0f);
	}
	for (const FTerraDyneAISpawnZone& Zone : Manager->AISpawnZones)
	{
		const FBox WorldBounds = Zone.LocalBounds.ShiftBy(Manager->GetActorLocation());
		DrawDebugBox(GetWorld(), WorldBounds.GetCenter(), WorldBounds.GetExtent(), FColor::Cyan, false, 0.2f, 0, 2.0f);
	}

	FString BuildReason;
	const bool bCanBuild = Manager->CanBuildAtLocation(TownProbe, BuildReason);
	const TArray<FTerraDyneAISpawnZone> MatchingZones = Manager->GetAISpawnZonesAtLocation(OutskirtsProbe);
	const TArray<FTerraDynePCGPoint> DesignerPoints = Manager->GetPCGSeedPointsForChunk(FIntPoint::ZeroValue, true, true);
	for (const FTerraDynePCGPoint& Point : DesignerPoints)
	{
		DrawDebugPoint(GetWorld(), Point.Transform.GetLocation(), 18.0f, FColor::Yellow, false, 0.2f);
	}

	GEngine->AddOnScreenDebugMessage(
		OVERLAY_KEY_DETAIL,
		0.25f,
		FColor::Cyan,
		FString::Printf(
			TEXT("  build@town: %s | AI zones@outskirts: %d | PCG export: %d points"),
			bCanBuild ? TEXT("Allowed") : *BuildReason,
			MatchingZones.Num(),
			DesignerPoints.Num()));
	GEngine->AddOnScreenDebugMessage(
		OVERLAY_KEY_AUX,
		0.25f,
		FColor::Silver,
		FString::Printf(
			TEXT("  events -> terrain %d (%d,%d) | foliage %d (%d inst) | population %d (%s / %s)"),
			TerrainEventCount,
			LastTerrainEventCoord.X,
			LastTerrainEventCoord.Y,
			FoliageEventCount,
			LastFoliageEventInstances,
			PopulationEventCount,
			*LastPopulationReason.ToString(),
			*LastPopulationTypeId.ToString()));
}

void ATerraDyneOrchestrator::UpdateDesignerWorkflowDemo(float DeltaTime)
{
	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	const UTerraDyneWorldPreset* ActivePreset = Manager->WorldPreset ? Manager->WorldPreset.Get() : ShowcasePreset.Get();
	const TArray<FTerraDynePCGPoint> PCGPoints = Manager->GetPCGSeedPointsForChunk(FIntPoint::ZeroValue, true, true);
	for (const FTerraDynePCGPoint& Point : PCGPoints)
	{
		DrawDebugSphere(GetWorld(), Point.Transform.GetLocation(), 60.0f, 8, FColor::Yellow, false, 0.2f, 0, 1.5f);
	}

	GEngine->AddOnScreenDebugMessage(
		OVERLAY_KEY_DETAIL,
		0.25f,
		FColor::Cyan,
		FString::Printf(
			TEXT("  preset: %s | biome overlays: %d | spawn rules: %d | AI zones: %d | build zones: %d"),
			ActivePreset ? *ActivePreset->PresetId.ToString() : TEXT("Transient Showcase"),
			Manager->BiomeOverlays.Num(),
			Manager->PopulationSpawnRules.Num(),
			Manager->AISpawnZones.Num(),
			Manager->BuildPermissionZones.Num()));
	GEngine->AddOnScreenDebugMessage(
		OVERLAY_KEY_AUX,
		0.25f,
		FColor::Silver,
		TEXT("  SceneSetup templates: Full Feature Showcase / Sandbox / Survival Framework / Authored World Conversion | Docs: Plugins/TerraDyne/DEMO.md"));
}

void ATerraDyneOrchestrator::StartPersistenceTest()
{
	ATerraDyneManager* Manager = GetManager();
	if (!Manager || !GetWorld())
	{
		return;
	}

	EnsureBasePopulation(Manager);
	ClearShowcaseTimers();
	Manager->SaveWorld(TEXT("Showcase_Save"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_AUX, 1.2f, FColor::Green, TEXT(">> World saved"));
	}

	TWeakObjectPtr<ATerraDyneManager> WeakManager(Manager);
	TWeakObjectPtr<ATerraDyneOrchestrator> WeakThis(this);
	GetWorld()->GetTimerManager().SetTimer(
		PersistenceMutationTimer,
		[WeakThis, WeakManager]()
		{
			ATerraDyneManager* LocalManager = WeakManager.Get();
			ATerraDyneOrchestrator* Self = WeakThis.Get();
			if (!LocalManager || !Self)
			{
				return;
			}

			LocalManager->ApplyGlobalBrush(LocalManager->GetActorLocation(), 4200.0f, -3200.0f, ETerraDyneBrushMode::Lower);
			if (const FGuid HarvestableId = Self->FindPopulationIdByTypeId(LocalManager, ShowcaseHarvestableTypeId); HarvestableId.IsValid())
			{
				LocalManager->HarvestPersistentPopulation(HarvestableId);
			}
			if (const FGuid DestroyableId = Self->FindPopulationIdByTypeId(LocalManager, ShowcaseDestroyableTypeId); DestroyableId.IsValid())
			{
				LocalManager->SetPersistentPopulationDestroyed(DestroyableId, true);
			}

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_AUX, 1.4f, FColor::Red, TEXT(">> Runtime world mutated: crater plus population damage"));
			}
		},
		1.6f,
		false);

	GetWorld()->GetTimerManager().SetTimer(
		PersistenceRestoreTimer,
		[WeakThis, WeakManager]()
		{
			ATerraDyneManager* LocalManager = WeakManager.Get();
			ATerraDyneOrchestrator* Self = WeakThis.Get();
			if (!LocalManager || !Self)
			{
				return;
			}

			LocalManager->LoadWorld(TEXT("Showcase_Save"));
			Self->BindManagerEvents(LocalManager);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_AUX, 1.8f, FColor::Green, TEXT(">> World restored from save"));
			}
		},
		4.2f,
		false);
}

void ATerraDyneOrchestrator::UpdatePersistenceDemo(float DeltaTime)
{
	if (ATerraDyneManager* Manager = GetManager())
	{
		GEngine->AddOnScreenDebugMessage(
			OVERLAY_KEY_DETAIL,
			0.25f,
			FColor::Cyan,
			FString::Printf(TEXT("  saved world tracks %d chunks and %d population entries together"), Manager->GetActiveChunkCount(), Manager->GetPersistentPopulationCount()));
	}
}

void ATerraDyneOrchestrator::UpdateReplicationDemo(float DeltaTime)
{
	ATerraDyneManager* Manager = GetManager();
	const int32 PopulationCount = Manager ? Manager->GetPersistentPopulationCount() : 0;
	const int32 ChunkCount = Manager ? Manager->GetActiveChunkCount() : 0;

	if (PhaseTimer < 1.5f)
	{
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.2f, FColor::Cyan, TEXT("  Server-authoritative brush edits replicate through RPC plus multicast terrain sync"));
	}
	else if (PhaseTimer < 3.0f)
	{
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.2f, FColor::Cyan, TEXT("  Imported foliage, biome overlays, AI zones, and build permissions live on the replicated manager"));
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.2f, FColor::Cyan, TEXT("  Late joiners receive chunk state, persistent population state, and runtime world metadata"));
	}

	GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_AUX, 0.2f, FColor::Silver, FString::Printf(TEXT("  active chunks: %d | persistent entries: %d"), ChunkCount, PopulationCount));
}

void ATerraDyneOrchestrator::UpdateLODDemo(float DeltaTime)
{
	ATerraDyneManager* Manager = GetManager();
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}

	if (PhaseTimer < 3.0f)
	{
		Pawn->SetActorLocation(FVector(60000.0f, 0.0f, 10000.0f));
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.2f, FColor::Cyan, TEXT("  Teleported beyond 50 km: far chunks should run with collision and streaming costs reduced"));
	}
	else
	{
		Pawn->SetActorLocation(OriginalPlayerPos);
		if (Manager)
		{
			Manager->bStreamingPaused = false;
		}
		GEngine->AddOnScreenDebugMessage(OVERLAY_KEY_DETAIL, 0.2f, FColor::Cyan, TEXT("  Returned to the authored core: close-range collision and streaming resume"));
	}
}
