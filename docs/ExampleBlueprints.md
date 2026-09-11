# 📝 TerraDyne Blueprint And C++ Examples

The packaged examples live in `/TerraDyne/Examples/`. Run `Plugins/TerraDyne/Content/Scripts/CreateExampleBlueprints.py` if the Blueprint assets need to be regenerated after a source build.

These examples are intentionally small. They show the integration path and expose Blueprint events/properties, but they do not try to replace your game's controller, AI, inventory, save, or PCG stack.

## ⛺ Survival Starter

Blueprint: `/TerraDyne/Examples/BP_TerraDyne_SurvivalSetup`

C++ parent: `ATerraDyneSurvivalIntegrationExample`

Use this actor when you want a first playable TerraDyne world without hand-wiring the manager. It finds or spawns an `ATerraDyneManager`, applies the packaged sample world preset, assigns the packaged grass profile, enables starter streaming/procedural settings, and adds fallback biome, AI-zone, and build-zone metadata if no preset data is present.

Typical Blueprint use:

1. Drop `BP_TerraDyne_SurvivalSetup` into a blank level.
2. Leave `Initialize On Begin Play` enabled for a quick test.
3. Optionally assign your own `Starter Preset` and `Starter Grass Profile`.
4. Implement `On Survival World Ready` if your game needs to cache the manager or spawn gameplay systems after TerraDyne is configured.

Minimal C++ equivalent:

```cpp
#include "Examples/TerraDyneIntegrationExamples.h"

ATerraDyneSurvivalIntegrationExample* Starter =
    World->SpawnActor<ATerraDyneSurvivalIntegrationExample>();
ATerraDyneManager* Manager = Starter ? Starter->EnsureSurvivalWorld() : nullptr;
```

## 🔹 PCG Seed Export

Blueprint: `/TerraDyne/Examples/BP_TerraDyne_PCGExporter`

C++ parent: `ATerraDynePCGIntegrationExample`

Use this actor as the bridge between TerraDyne runtime world metadata and your own procedural placement. It calls `GetPCGSeedPointsForChunk`, stores the results in `Last Seed Points`, and fires `On PCG Seed Point` for each exported point.

Typical Blueprint use:

1. Place the Blueprint near your PCG/spawner system.
2. Set `Sample Chunk Coord` to the chunk you want to query.
3. Toggle `Include Population` and `Include AI Spawn Zones` as needed.
4. Implement `On PCG Seed Point` to feed your own spawner, PCG graph adapter, debug marker, or gameplay system.

Minimal C++ equivalent:

```cpp
#include "Core/TerraDyneManager.h"

TArray<FTerraDynePCGPoint> Points =
    Manager->GetPCGSeedPointsForChunk(FIntPoint::ZeroValue, true, true);
```

## 🔹 Replication Bridge

Blueprint: `/TerraDyne/Examples/BP_TerraDyne_ReplicationBridge`

C++ parent: `ATerraDyneReplicationIntegrationExample`

Use this actor as a small pattern for custom multiplayer gameplay that modifies TerraDyne terrain. Client gameplay calls `Apply Brush Authoritatively`; the example routes the request to the server, applies the brush on the authoritative manager, multicasts the terrain edit, and optionally wraps the edit in an undo stroke.

For client RPCs to work, the bridge must be owned by the requesting player controller or by a pawn owned by that controller. The example can auto-assign the first player owner for simple PIE tests, but real projects should spawn or own one bridge per player.

Typical Blueprint use:

1. Place or spawn `BP_TerraDyne_ReplicationBridge`.
2. Set `Target Manager`, or let it auto-find the active manager.
3. Ensure the bridge is owned by the player who will call it.
4. Call `Apply Brush Authoritatively` from weapon, ability, tool, or interaction code.
5. Implement `On Authoritative Brush Applied` for local UI/audio/gameplay feedback.

Minimal C++ equivalent:

```cpp
#include "Examples/TerraDyneIntegrationExamples.h"

Bridge->ApplyBrushAuthoritatively(
    HitLocation,
    1200.0f,
    -350.0f,
    ETerraDyneBrushMode::Lower,
    0,
    0.0f,
    PlayerController);
```

## 💾 Save And Load

Blueprint: `/TerraDyne/Examples/BP_TerraDyne_SaveLoadDemo`

C++ parent: `ATerraDyneSaveLoadIntegrationExample`

Use this actor when you want a minimal runtime persistence shell in Blueprint. It wraps `SaveWorld` and `LoadWorld`, exposes a user-facing save slot name, and can queue autosaves from TerraDyne's terrain, foliage, or population change delegates.

This example is intentionally local-slot focused. For dedicated-server or host-authoritative persistence, treat it as a UI/gameplay wrapper around your own save ownership flow rather than a full multiplayer persistence system.

Typical Blueprint use:

1. Place `BP_TerraDyne_SaveLoadDemo` in the level alongside your manager.
2. Set `Save Slot Name` to the slot your project will use.
3. Enable `Load On Begin Play` if the level should restore its last saved state automatically.
4. Enable one or more autosave toggles if you want terrain edits or population changes to persist without hand-written delegate wiring.
5. Implement `On World Saved`, `On World Loaded`, or `On Save Slot Missing` for UI messaging.

Minimal C++ equivalent:

```cpp
#include "Examples/TerraDyneIntegrationExamples.h"

SaveLoadExample->SaveWorldToSlot(TEXT("CampaignSlot01"));
SaveLoadExample->LoadWorldFromSlot(TEXT("CampaignSlot01"));
```

## 🎮 Gameplay Hooks

Blueprint: `/TerraDyne/Examples/BP_TerraDyne_BiomeReactor`

C++ parent: `ATerraDyneBiomeReactorIntegrationExample`

Use this actor when gameplay needs a live answer to "what biome am I in?", "can the player build here?", or "which AI zones apply at this location?" It caches those answers, exposes them as Blueprint-visible state, and can refresh automatically when terrain, foliage, or runtime population changes.

Typical Blueprint use:

1. Place `BP_TerraDyne_BiomeReactor` in the level, or attach it to a gameplay actor.
2. Set `Query Actor` if the checks should follow a player, base, marker, or AI pawn.
3. Use `Query Location Offset` to sample ahead of the actor or at a build cursor point.
4. Implement `On Gameplay Context Refreshed`, `On Biome Tag Changed`, or `On Build Permission Changed` to drive UI, spawning, quests, hazards, or placement rules.
5. Enable the refresh toggles that match your gameplay needs.

Minimal C++ equivalent:

```cpp
#include "Examples/TerraDyneIntegrationExamples.h"

BiomeReactor->RefreshGameplayContext();
if (!BiomeReactor->bLastCanBuild)
{
    UE_LOG(LogTemp, Warning, TEXT("Build blocked: %s"), *BiomeReactor->LastBuildReason);
}
```

## 🔹 Legacy Example Assets

Deprecated assets such as `BP_TerraDyne_RuntimeEditor` may still exist for compatibility, but they are not part of the supported/generated example set. The regeneration script owns only the five Blueprint examples above and reparents stale versions of those assets to the current C++ parents where needed.
