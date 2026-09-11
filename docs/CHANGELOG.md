# Changelog

## [0.5.0] - 2026-07-12
### ✨ Added
- **Reusable Replication Component:** Added `UTerraDyneReplicationComponent`, which can be attached to any project PlayerController and no longer requires replacing the project's controller with `ATerraDyneEditController`.
- **Bounded State Transport:** Chunk state is now validated, Zlib-compressed, CRC-protected, split into configurable fragments capped at 48 KiB, throttled per connection, reassembled with timeouts, and bounded by compressed/uncompressed/in-flight byte limits.
- **Project-Owned Persistence API:** Added `GetActiveChunkCoordinates`, `ExportChunkStatePacket`, and `ImportChunkStatePacket` so database-backed games can store TerraDyne state without using local SaveGame/cache ownership.
- **Gameplay Authorization API:** Added `ApplyAuthorizedBrush` as the server-owned integration surface for craters, digging, construction, and other gameplay-approved deformation.
- **Production Settings:** Added configurable replication radius, transfer budgets, request rates, collision profile/object channel, GPU enablement, async cache writes, and navigation dirty-area coalescing.
- **Explicit Client/Server Demo Targets:** Added `TerraDyneClient.Target.cs` and `TerraDyneServer.Target.cs` for source-distribution validation where the installed engine includes those target types.
- **Production Automation:** Added codec round-trip, integrity/limit, and safe-default tests under `TerraDyne.Production`.

### 🛠 Changed
- **Safe Multiplayer Defaults:** The manager is no longer always relevant by default. Brush events and authoritative corrections are routed through relevant connection-owned components. Remote terrain editing is opt-in on the built-in controller and has a Blueprint authorization hook.
- **Server RPC Validation:** Remote brushes now enforce finite values, enum/layer validity, request rate, maximum radius/strength, owner distance, affected-chunk budget, stroke ordering, and flatten-height delta.
- **Dedicated-Server Runtime:** Removed the unused VirtualHeightfieldMesh dependency. Headless runtimes no longer allocate weight textures, height render targets, upload textures, lighting, or material instances; CPU height/collision remains available.
- **Persistence:** Dirty chunk cache files now use a versioned/CRC-checked format, atomic replacement, asynchronous compression/write, in-memory state until success, retry scheduling, and transparent legacy-cache migration. `SaveWorld` now uses Unreal's asynchronous save API and serializes an explicit schema version.
- **Navigation:** Overlapping dirty areas are coalesced and debounced instead of submitting one full chunk navigation update per brush event.
- **Collision:** Generated chunks use project-configurable collision profile and object channel rather than hard-coded WorldStatic/BlockAll behavior.
- **Authored Baking:** Component heights are read through `FLandscapeEditDataInterface`, respecting shared heightmap atlas subregions. Asset-set tiles now use one canonical four-channel layer mapping.
- **Integration Example:** Client RPCs in the replication example are disabled by default, ignore client-supplied controller references, enforce owner distance/cooldown, use `ApplyAuthorizedBrush`, and support any controller carrying the replication component.

### 🐛 Fixed
- **Oversized Chunk RPCs:** Removed internal use of full `FTerraDyneChunkData` RPC parameters, which exceeded Unreal's default 64 KiB constructed-bunch limit at normal resolution.
- **Global Edit Broadcast:** Removed high-frequency NetMulticast terrain edits from the always-relevant manager path.
- **Unvalidated Terrain Editing:** The built-in controller no longer accepts unrestricted client terrain writes.
- **Cache Corruption Detection:** New cache/network packets reject unsupported versions, inconsistent arrays, invalid sizes, coordinate mismatches, decompression failures, and CRC mismatches.
- **Landscape Atlas Bake:** Baking no longer treats an entire shared heightmap texture as one component.
- **Per-Component Paint Remapping:** RGBA meanings no longer change when component layer allocation order differs.
- **Visibility-Hole Data Loss:** Authored conversion now stops with a clear error when Landscape visibility holes are present instead of silently filling them.
- **NullRHI Startup:** Shader source mapping remains available to commandlets while chunk GPU resource creation stays disabled on headless runtimes.

### 💥 Breaking / Migration Notes
- `Multicast_ApplyBrush` and `Multicast_SyncChunkState` retain their names for source/Blueprint compatibility but are no longer NetMulticast RPCs; they route through `UTerraDyneReplicationComponent`.
- Projects using a custom PlayerController must add a replicated `UTerraDyneReplicationComponent` to receive TerraDyne edits/state.
- `ATerraDyneEditController::bAllowRemoteTerrainEditing` defaults to false. Explicitly enable it only on an authorized controller subclass and preferably override `IsRemoteTerrainEditAuthorized`.
- Cache files are migrated on read. World SaveGame objects carry format version 2 while legacy version-zero saves remain loadable.
- Landscape visibility holes remain unsupported topology in 0.6; conversion now fails safely by default.

### ✅ Verified
- UE 5.7.4 Win64 Development Editor target compiles with UHT warnings-as-errors.
- UE 5.7.4 Win64 Development Game target compiles and links.
- The full headless suite passes `30/30` tests under `UnrealEditor-Cmd -NullRHI` (`27` clean and `3` with expected diagnostic warnings).
- The full render-capable suite passes `31/31` tests under D3D12 SM6 on an NVIDIA GeForce RTX 5060 Ti, including GPU brush synchronization and material bindings.
- The clean UE 5.7 Win64 Fab source package compiles through `BuildPlugin`, passes marketplace validation before and after staging, and passes ZIP-layout validation.
- A dedicated Server compile remains unverified because Epic's installed UE 5.7 distribution rejects Server targets before plugin compilation; validate it with a source/custom engine distribution.
- The installed Epic UE 5.7 distribution reports that Server targets are not included, so the new Server target requires validation with a source/installed engine distribution that supports dedicated servers.

## [0.4.2] - 2026-06-16
### ✨ Added
- **Runtime Authored-World Asset Set:** Added `UTerraDyneLandscapeAssetSet`, a packaged-build-safe authored-world payload that stores migration metadata, an optional adopted master material, and the baked TerraDyne tile set for a converted Landscape.
- **Runtime Bootstrap API:** Added `ATerraDyneManager::InitializeFromBakedLandscapeAssetSet()` so Blueprint and C++ callers can initialize TerraDyne directly from baked authored-world data without any editor-only Landscape import path.
- **Tile Runtime Payload Reconstruction:** `UTerraDyneTileData` now exposes `BuildChunkData()` and persists the foliage/runtime payload required to reconstruct transferred static and actor foliage when booting from baked authored-world assets.
- **Async Mesh Apply Budget Controls:** Added `MaxMeshBuildAppliesPerFrame` and `MaxCollisionUpdatesPerFrame` to `UTerraDyneSettings` so teams can tune how aggressively chunk mesh and collision work is applied back onto the game thread.
- **Setup Wizard Runtime Data Controls:** The setup wizard now exposes a baked Landscape asset-set picker, baked-output folder selection, and an explicit live-import fallback toggle for authored-world conversion.

### 🛠 Changed
- **Unified Authored Conversion Workflow:** The setup wizard is now the only authored-world conversion entry point. It bakes runtime-safe authored-world data and initializes TerraDyne from that same payload instead of maintaining a separate conversion panel or tile-only bake workflow.
- **Packaged Runtime Initialization:** `ATerraDyneSceneSetup` now prefers baked authored-world data by default through `AuthoredWorldAssetSet` and `bPreferBakedLandscapeData`, allowing authored-world conversion to work in packaged builds instead of relying on `WITH_EDITOR`-only initialization.
- **Landscape Baking Pipeline:** `UTerraDyneBaker::BakeLandscapeToAssetSet()` now resolves the full Landscape group, captures migration metadata, layer mappings, adopted-material compatibility, and transferred foliage payload, and saves a runtime-safe asset set plus normalized TerraDyne tile assets.
- **Chunk Rebuild Scheduling:** Height edits and chunk loads now stage mesh-surface computation off-thread and throttle how many mesh/collision applies can land on the game thread per frame, reducing the worst terrain-edit hitch path.
- **Documentation:** `docs/QuickStart.md` and `docs/ShowcaseRunbook.md` now describe the baked authored-world workflow as the primary path and position live Landscape import as an editor-only fallback.

### 🐛 Fixed
- **GPU Brush Simulation Gap:** `Shaders/TerraDyneSimulation.usf` is no longer a pass-through writeback. The compute path now performs raise, lower, flatten, and smooth brush operations and writes the modified height result into the swap render target.
- **Authored-World Runtime Init Gap:** TerraDyne no longer depends on the editor wizard/live Landscape import path to bootstrap authored worlds at runtime; baked asset sets can now initialize managers in packaged builds.
- **Game-Thread Rebuild Bottleneck:** Chunk mesh and collision rebuilds no longer force the full terrain-surface recompute path synchronously on the game thread during normal deformation/load flow.
- **Landscape Height Bake Scaling:** Baked Landscape Z scale is now captured using the correct full-height conversion, preventing authored-world imports from using the older under-scaled height interpretation.
- **Conversion UX Failure Cases:** Authored-world conversion now hard-fails with explicit notifications when no valid source Landscape is available for live import or when baking the runtime asset set fails.

### 🗑️ Removed
- **Obsolete Conversion UI:** Removed the legacy `STerraDyneTools` conversion panel and the redundant `TerraDyne Conversion Wizard` menu/tab path.
- **Retired Tile-Only Bake API:** Removed the deprecated `UTerraDyneBaker::BakeLandscapeToAssets()` compatibility workflow so the baked asset-set path is now the single authored-world bake surface.
- **Dead Editor Wiring:** Removed unused editor-module members, unused setup-wizard initial-template plumbing, stale editor-only includes, and the extra `WorkspaceMenuStructure` dependency that no longer served the unified workflow.

### ✅ Verified
- **Editor Build:** `Build.bat TerraDyneEditor Win64 Development -Project=TerraDyne.uproject -WaitMutex -NoHotReloadFromIDE` succeeds on UE 5.6.
- **Game Build:** `Build.bat TerraDyne Win64 Development -Project=TerraDyne.uproject -WaitMutex -NoHotReloadFromIDE` succeeds on UE 5.6.

## [0.4.1] - 2026-05-29
### ✨ Added
- **Packaged First-Run Samples:** Added self-contained TerraDyne sample content under `/TerraDyne/Samples`, including `M_TerraDyne_GrassSample`, `SM_TerraDyne_GrassA/B/C`, `DA_TerraDyne_ShowcaseGrass`, and `DA_TerraDyne_ShowcaseWorld`.
- **Survival Integration Example:** Added `ATerraDyneSurvivalIntegrationExample` plus `/TerraDyne/Examples/BP_TerraDyne_SurvivalSetup` to show a small game-facing setup path that finds or spawns a manager, applies the packaged world preset and grass profile, enables streaming/procedural defaults, and exposes a Blueprint event when the world is ready.
- **PCG Integration Example:** Added `ATerraDynePCGIntegrationExample` plus `/TerraDyne/Examples/BP_TerraDyne_PCGExporter` to demonstrate chunk seed-point export for PCG graphs and Blueprint consumers.
- **Replication Integration Example:** Added `ATerraDyneReplicationIntegrationExample` plus `/TerraDyne/Examples/BP_TerraDyne_ReplicationBridge` to demonstrate authoritative terrain edit routing, undo-stroke wrapping, multicast brush application, and full terrain sync requests for owned multiplayer actors.
- **Example Blueprint Automation:** `Content/Scripts/CreateExampleBlueprints.py` now creates or preserves the three first-run example Blueprints under `/TerraDyne/Examples` and reruns cleanly without reparenting noise.
- **Showcase Sample Automation:** Added sample-asset generation for the packaged grass meshes, grass material, showcase grass profile, and showcase world preset so the demo no longer depends on missing or project-local placeholder assets.
- **Play Mode Tool UI Switch:** Added `Enable Play Mode Tool UI` on `ATerraDyneEditController` so gameplay maps can disable the TerraDyne runtime widget safely.
- **PIE Controller Automation:** Added a dedicated PIE automation harness that boots a real `ATerraDyneEditController` with `bEnablePlayModeToolUI=false` and asserts the no-UI branch end to end.
- **External QA Landscape Import Prep:** Added repo-root `tools/ImportFreeLandscape.ps1`, `tools/import_free_landscape.py`, and `tools/ImportFreeLandscape.md` to fetch the latest public-domain USGS 3DEP 1/3-arc-second DEM for a curated profile, crop it, resample it to a UE-friendly resolution, export a 16-bit heightmap, and emit a manifest with exact Landscape import scales and source traceability.
- **Versioned Real-Map Fixture Output:** External landscape prep now writes a reproducible package under `Saved/ExternalLandscapes/...` containing the cached source GeoTIFF, a preview image, the processed heightmap, raw TNMAccess metadata, and the Unreal import manifest so authored-world conversion QA can use real terrain without shipping source maps in the plugin payload.

### 🛠 Changed
- **First-Run Wizard Defaults:** The setup wizard now opens on `Survival Framework (Recommended First Run)`, lists it first, and explains that it applies packaged sample presets, grass profiles, biome overlays, AI zones, build zones, streaming defaults, lighting, manager, and orchestrator setup.
- **Scene Setup Defaults:** `ATerraDyneSceneSetup` now defaults to `SurvivalFramework` and applies `/TerraDyne/Samples/Presets/DA_TerraDyne_ShowcaseWorld` and `/TerraDyne/Samples/Profiles/DA_TerraDyne_ShowcaseGrass` when no project-specific preset or grass profile has been assigned.
- **Demo Map Self-Containment:** `CreateDemoMap.py` and `ATerraDyneOrchestrator` now prefer packaged TerraDyne sample presets, profiles, grass meshes, and materials, with legacy project assets used only as fallback.
- **Showcase Messaging:** Scene setup logging now distinguishes the cinematic `Full Feature Showcase` from general first-run world initialization, avoiding misleading "showcase ready" messaging for non-showcase templates.
- **Default Asset Paths:** TerraDyne defaults were moved away from legacy `/Game/MW/...` dependencies and now point at packaged `/TerraDyne/...` content where appropriate.
- **Documentation Positioning:** `docs/QuickStart.md` now includes a clear disclaimer that TerraDyne is not a beginner-friendly drop-in asset and assumes UE5 fluency plus some C++ familiarity for custom integration. It also recommends exposure to the FOSS GitHub version before deep production use.
- **Integration Documentation:** `docs/ExampleBlueprints.md`, `docs/API.md`, `docs/runtime-world-framework.md`, and `DEMO.md` now document the survival, PCG, and replication examples, the stronger first-run defaults, and the recommended Survival Framework entry point.
- **Edit Controller Safety:** Disabling the play mode tool UI now removes the TerraDyne widget, restores `GameOnly` input, hides the cursor and brush decal, and blocks sculpt/undo/redo and related edit RPC paths so invisible edit mode cannot remain active during play.
- **Chunk Material Initialization:** `ATerraDyneManager` now assigns `BrushMaterialBase` when registering chunks and when applying materials to active chunks, allowing chunks created before manager material initialization to participate in the GPU brush path once compatible materials are available.
- **External Import Source Selection:** The free-landscape import tool now selects only TNMAccess products that fully contain the configured crop bounds, retries transient Windows file-lock failures when finalizing downloads, and records the exact USGS tile revision used for each generated fixture.

### 🐛 Fixed
- **Missing Sample Meshes:** Added the missing packaged grass mesh assets and bound them through the sample grass profile so first-run maps and the showcase can spawn foliage without missing-asset warnings.
- **Demo Map Warning Baseline:** `/TerraDyne/Maps/DemoMap_Showcase` now loads with the packaged profile, preset, and sample meshes and stays clear of TerraDyne/map diagnostics during live validation.
- **Blueprint Example Regeneration:** Re-running the example Blueprint creation script no longer emits warnings for already-existing example assets.
- **Disabled-UI Input Leak:** `ATerraDyneEditController` no longer validates or binds TerraDyne edit input actions when play mode tool UI is disabled, so gameplay-mode PIE sessions do not emit false missing-input errors.
- **Showcase Population Mobility:** TerraDyne now normalizes persistent/runtime-placed actors to `Movable` before transform and mesh assignment, eliminating live showcase mobility warnings and `SetStaticMesh`-while-static warnings during runtime validation.
- **Population Mesh Binding Duplication:** Removed the redundant `SetStaticMesh` application in the persistent population spawn path.
- **UE 5.7 GPU Terrain Startup:** `ATerraDyneChunk` no longer seeds the height render target by writing render-target memory directly. Height data is now uploaded through a transient `PF_FloatRGBA` texture and blitted into the render target through `UCanvas`, matching UE 5.7 render-target lifecycle expectations and restoring the GPU-backed chunk path.
- **GPU Validation Readback:** Height render-target validation now uses float readback plus vertical-orientation detection, preventing false GPU fallback caused by readback ordering differences during startup validation.
- **Transient Texture Naming Noise:** Height-upload and weightmap helper textures now use unique transient object names, removing generic overwrite warnings during repeated editor/runtime validation.
- **Landscape Conversion Coverage:** Authored World Conversion now resolves the full landscape through `ULandscapeInfo` and imports all loaded landscape components instead of only the selected `ALandscapeProxy`, which fixes partial or empty imports on partitioned landscapes and streaming-proxy setups.
- **Landscape Import Diagnostics:** Conversion now surfaces explicit user-facing errors when the selected landscape has no loaded components or when no TerraDyne chunks were created from the import.
- **Landscape Material Adoption Safety:** `Adopt Landscape Material As Master Material` now refuses landscape materials that use landscape-only material expressions and keeps the existing TerraDyne chunk material instead of applying an incompatible graph.
- **Source Landscape Hiding:** `Hide Source Landscape` now hides the full landscape proxy hierarchy instead of only the selected source actor.

### ✅ Verified
- **C++ Build:** `Build.bat TerraDyneEditor Win64 Development -Project=TerraDyne.uproject -WaitMutex -NoHotReload` succeeds on UE 5.7.
- **Automation Coverage:** `Automation RunTests TerraDyne` passes with `23/23` TerraDyne tests on UE 5.7 after the runtime warning cleanup, GPU terrain-path fixes, sample asset checks, first-run default checks, and integration example class checks.
- **Blueprint Asset Generation:** `CreateExampleBlueprints.py` completes with `0 error(s), 0 warning(s)` after the survival, PCG, and replication examples are present.
- **Marketplace Validation:** `tools/ValidateTerraDyneMarketplacePackage.ps1` passes for the plugin package.
- **Live Showcase Smoke:** `/TerraDyne/Maps/DemoMap_Showcase` live-loads successfully with no TerraDyne/map diagnostics; only unrelated engine VisionOS launcher icon warnings were observed in the commandlet log.
- **Crash Check:** No new crash folders were created during the final build, Blueprint generation, automation, marketplace validation, or showcase live-load pass.
- **Render Path Validation:** `TerraDyne.Rendering.MaterialBindings` now reaches `Chunk [0,0]: Ready (GPU: YES)` instead of disabling GPU after render-target validation.
- **Real-Map Fixture Generation:** The external import tool successfully generated a Crater Lake package from `USGS 1/3 Arc Second n43w123 20260202`, including a `2017x2017` Unreal-ready heightmap and exact recommended import scales of `X=886.9353`, `Y=881.6758`, `Z=229.7515` centimeters.

## [0.3.1] - 2026-05-08
### 🐛 Fixed
- **Fab Startup Crash:** TerraDyne now verifies the plugin shader directory before registering `/Plugin/TerraDyne`, preventing the RenderCore `DirectoryExists` assertion when a packaged install is missing `Shaders/`.
- **Marketplace Packaging Gate:** Added a release validator and automation coverage for `Shaders/TerraDyneSimulation.usf` so the required RDG shader payload cannot be omitted silently.

## [0.3.0] - 2026-05-05
### ✨ Added
- **Notification System:** New `UTerraDyneSubsystem::ShowNotification()` with `Info`/`Warning`/`Error` severities, Slate toasts in editor, on-screen messages at runtime, and a `bShowRuntimeNotifications` setting for users to opt out at runtime.
- **Brush Preview Decal:** `ATerraDyneEditController` now projects a live decal at the cursor showing the brush radius and tool mode (blue for sculpt, orange for paint).
- **Chunk Debug Overlay:** New `bShowDebugOverlay` on the manager, color-coded by chunk state (green=loaded, yellow=loading, red=unloading, cyan=imported), wired to a toggle button on `STerraDynePanel`.
- **Marketplace Documentation:** `Docs/QuickStart.md`, `Docs/API.md`, and `Docs/ExampleBlueprints.md` now ship with the plugin.
- **Asset Automation Scripts:** `Content/Scripts/CreateBrushPreviewMaterial.py`, `CreateDemoMap.py`, and `CreateExampleBlueprints.py` create the supporting assets without manual editor work.

### 🛠 Changed
- **Error Reporting:** Critical failure points (material load, chunk spawn, save/load, landscape import, cache write, buffer mismatch) now surface to the user via `ShowNotification` instead of log-only.
- **Input Validation:** `ATerraDyneEditController` validates the `TerraDyneClick` input action at startup and shows an actionable error toast if missing.
- **Logging Hygiene:** All `LogTemp` calls replaced with `LogTerraDyne` (runtime) or `LogTerraDyneEditor` (editor) — zero `LogTemp` references remain in plugin source.
- **Settings Tooltips:** Every `UTerraDyneSettings` property in the `Asset Paths` and `Defaults` categories now exposes a tooltip in the project settings details panel.
- **Setup Wizard:** `Initialize World` now dismisses its tab on success so it can't be re-clicked indefinitely.
- **Platform Support:** `.uplugin` runtime module now lists `Linux` and `Mac` alongside `Win64` (editor module remains Win64-only).

### 🐛 Fixed
- **Setup Wizard:** `Initialize World` button now dismisses the wizard tab on success — previously it could be re-clicked indefinitely.
- **Runtime Grass Blade Mesh:** Material slot is now registered before `BuildFromStaticMeshDescriptions`, fixing a `UVChannelData.bInitialized` ensure() that fired during showcase startup.

### ✅ Verified
- **Automation:** `Automation RunTests TerraDyne` passes with 21/21 TerraDyne tests.

## [0.2.0] - 2026-04-05
### ✨ Added
- **Authored World Conversion:** GitHub now includes the verified landscape migration path with paint layer capture, placed foliage transfer, and actor foliage transfer.
- **Persistent Runtime World:** Terrain save/load, replicated chunk sync, persistent runtime actors, destruction state, harvest/regrowth flow, and runtime placement are part of the public release.
- **Procedural Extension:** Seeded outskirts, biome overlays, runtime spawn rules, optional edge growth, and PCG-ready point export are now included.
- **Gameplay Hooks:** Blueprint-callable biome queries, AI spawn zones, build-permission checks, navmesh dirtying, and terrain/foliage/population change events are exposed from the manager.
- **Designer Workflow:** World presets, scene templates, showcase automation, and packaged docs now ship with the plugin.

### 🛠 Changed
- **Public Positioning:** TerraDyne 0.3 is now presented as a persistent runtime world framework for survival, sandbox, and open-world games built on Unreal Landscapes.
- **Release Versioning:** The GitHub plugin manifest and shipped UI strings now report the public release as `0.3`.

### ✅ Verified
- **Automation:** `Automation RunTests TerraDyne` passes with 16 TerraDyne tests.

## [0.1.1] - 2026-03-14
### 🐛 Fixed
- **Startup Safety:** Default terrain spawning, scene lighting, and showcase playback are now opt-in instead of mutating every world on `BeginPlay`.
- **Self-Contained Defaults:** Default material and tool UI paths no longer depend on `/Game/...` project assets.
- **Undo Reliability:** Snapshot restore now validates terrain buffer sizes before rebuilding meshes, preventing undo crashes on malformed data.
- **Chunk Registration:** Chunks register and unregister with the manager automatically, which keeps the active chunk map valid for pre-placed and streamed actors.
- **Network Sync Resilience:** Client chunk sync messages are queued until the target chunk exists, improving late-join and streaming behavior.
- **Landscape Import:** Manual landscape import now creates TerraDyne chunks and resamples landscape data instead of leaving the editor commands stubbed out.

### 🛠 Changed
- **Documentation:** Public docs now describe TerraDyne as a UE 5.7 CPU-authoritative runtime terrain system with optional hybrid render-target visuals.
- **Extensibility:** `ChunkClass` is now respected by the main chunk spawn paths used by loading and streaming.

## [0.1.0] - 2026-02-10
### ✨ Added
- **Hybrid Render-Target Support:** Added optional `UTextureRenderTarget2D` workflows for brush and material-driven visual effects.
- **Persistence System:** New Save/Load architecture (`UTerraDyneSaveGame`) for serializing terrain state to disk.
- **Showcase Demo:** Added `ATerraDyneOrchestrator` which runs a guided tour of features (Sculpt, Save/Load, LOD) when `BP_TerraDyneSceneSetup` is used.
- **Smart Material:** New `M_TerraDyne_Smart` master material with slope-based blending and procedural noise.

### 🐛 Fixed
- **Collision Reliability:** Resolved physics "fall-through" issues by implementing collision throttling (`CollisionDebounceTime`) and ensuring `BlockAll` profiles.
- **LOD Performance:** Optimized distance-based culling to disable complex collision on distant chunks (>500m).
- **Import Pipeline:** Fixed `TerraDyneManager` import tools to correctly handle scale differences when importing from `ALandscapeProxy`.
- **Compilation:** Fixed missing includes (`APawn`, `Engine.h`) that caused build failures in strict compilation environments.

### 🛠 Changed
- **Architecture:** Refactored `ATerraDyneChunk` to use a unified `UDynamicMeshComponent` for both visual and physical representation.
- **UI:** Updated `STerraDynePanel` to correctly display GPU/CPU backend status.
