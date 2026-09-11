# ✨ TerraDyne Demo Showcase

`ATerraDyneOrchestrator` now runs a full product demo instead of the older terrain-only tour. The sequence bootstraps a fallback runtime world when the level is not preconfigured, then walks through authored conversion, persistent population, procedural outskirts, gameplay hooks, designer workflow, save/load, replication messaging, and LOD before handing control to the player.

## ⚡ Quick Start

1. Open `DEMO.umap` and press **Play**.
2. Or place `ATerraDyneSceneSetup`, choose `DemoTemplate = Full Feature Showcase`, and call `InitializeWorld()`.
3. You can relaunch the sequence at runtime from the control panel with **Run Full Showcase**.

## 🔹 What The Demo Bootstraps

If the map does not already contain a configured runtime world, the orchestrator will:

- spawn a fallback `ATerraDyneManager`
- apply a transient showcase preset with biome overlays, spawn rules, AI zones, and build permissions
- create an authored core chunk if none exists
- seed transferred foliage, actor foliage, grass, and a small persistent population set

If the map already has a manager, preset, or imported content, the demo uses that state instead of replacing it.

## 🔹 Phase Sequence

| # | Phase | Duration | What It Shows |
|---|-------|----------|---------------|
| 1 | `Warmup` | 4s | TerraDyne positioned as a persistent runtime world framework |
| 2 | `Authored World Conversion` | 8s | Authored core terrain, transferred foliage, actor foliage, grass, and migrated paint metadata |
| 3 | `Runtime Terrain Editing` | 10s | Raise / Lower / Smooth / Flatten on the converted landscape |
| 4 | `Height Layer Stack` | 6s | Base / Sculpt / Detail layer separation |
| 5 | `Paint Layer Migration` | 6s | Runtime paint layers driving material and biome logic |
| 6 | `Undo / Redo` | 8s | Per-player stroke history |
| 7 | `Persistent World Population` | 10s | Props, harvestables, destruction, regrowth, and runtime placement |
| 8 | `Seeded Procedural Outskirts` | 10s | Outskirts generation, biome overlays, spawn rules, and edge growth |
| 9 | `Gameplay Hooks` | 8s | Build permissions, AI zones, PCG points, nav refresh, and change events |
| 10 | `Designer Workflow` | 6s | Presets, scene templates, docs, and PCG-ready export points |
| 11 | `Save / Load` | 8s | Terrain plus population state restored from one save |
| 12 | `Networking` | 5s | Replication and late-join sync messaging |
| 13 | `LOD & Distance Culling` | 6s | Far-travel behavior and collision/streaming cost reduction |
| 14 | `Interactive` | ∞ | Full player control with the existing tool panel |

Total scripted runtime is about 95 seconds.

## ✨ Showcase Controls

The panel now exposes two demo buttons:

- `Run Full Showcase`: restarts the orchestrated demo and bootstraps the fallback world if needed.
- `Test Persistence (Save/Load)`: runs the save/load beat directly.

## 🔹 Demo Template Support

`ATerraDyneSceneSetup` supports:

- `Full Feature Showcase`
- `Sandbox`
- `Survival Framework`
- `Authored World Conversion`

`Survival Framework` is the recommended first-run template because it applies the packaged sample preset, grass profile, streaming defaults, biome overlays, AI zones, and build zones. `Full Feature Showcase` remains the recommended entry point for recorded feature demos.

## 🔹 Automation Coverage

`TerraDyne.Demo.Simulation` now validates the expanded phase order:

`Warmup -> AuthoredWorldDemo -> SculptingDemo -> LayerDemo -> PaintDemo -> UndoRedoDemo -> PopulationDemo -> ProceduralWorldDemo -> GameplayHooksDemo -> DesignerWorkflowDemo -> PersistenceDemo -> ReplicationDemo -> LODDemo -> Interactive`

## 🔹 Related Docs

- `Plugins/TerraDyne/docs/runtime-world-framework.md`
- `Plugins/TerraDyne/DEMO.md`
