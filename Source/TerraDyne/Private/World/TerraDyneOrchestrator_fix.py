import os

file_path = "F:/Epic Games/TerraDyne/Plugins/TerraDyne/Source/TerraDyne/Private/World/TerraDyneOrchestrator.cpp"

with open(file_path, "r") as f:
    lines = f.readlines()

content_to_insert = """			NormalizedPath.Contains(TEXT("m_landscape")) ||
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
}"""

new_lines = lines[:125] + [content_to_insert + "\n"] + lines[126:]
with open(file_path, "w") as f:
    f.writelines(new_lines)
