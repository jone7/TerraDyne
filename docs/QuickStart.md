# ⚡ TerraDyne Quick-Start Guide

Get terrain running in your project in under 2 minutes.

## 🔹 Disclaimer

TerraDyne is an advanced UE5 runtime terrain and world-framework plugin. It is not intended to be a beginner-friendly, drop-in asset. Productive use assumes a solid grasp of UE5 project structure, actors, materials, input, runtime systems, and at least some C++ familiarity, especially for custom gameplay integration or engine-level extension work.

If you are evaluating TerraDyne for the first time, start with the free and open-source GitHub version and its examples before relying on the Marketplace package in a production project.

## 📦 1. Install

1. Copy the `TerraDyne` folder into your project's `Plugins/` directory.
2. Open your project in UE5. Enable **TerraDyne** in Edit > Plugins if prompted.
3. Restart the editor.

## 🛠️ 2. Open the Setup Wizard

**Window > TerraDyne Setup Wizard**

This opens the unified world bootstrapper.

## 🔹 3. Pick a Template

| Template | Best For |
|----------|----------|
| **Survival Framework (Recommended First Run)** | Open-world games, survival, exploration; applies the packaged sample preset, grass profile, streaming, biome, AI-zone, and build-zone defaults |
| **Full Feature Showcase (Cinematic Tour)** | Seeing every feature in action (press Play!) |
| **Authored World Conversion** | Converting an existing UE5 Landscape (your custom map) to TerraDyne. Recommended path: select your Landscape, leave live import off, and click Initialize so TerraDyne bakes a packaged-safe Landscape asset set and boots from that runtime payload. You can also point the wizard at an existing baked asset set, or enable live import for editor-only conversion. If the dropdown shows "No Landscape detected", make sure the level containing your Landscape is open and click **Refresh**. |
| **Sandbox (Blank Canvas)** | Testing, prototyping, clean slate when you intentionally want minimal starter systems |

Select one and click **Initialize World**.

## 🔹 4. Press Play

The TerraDyne Manager spawns chunks automatically. You'll see terrain appear with materials and grass applied.

- If you chose **Full Feature Showcase**, an automated 10-phase demo runs showing sculpting, painting, biomes, population, and more.
- Other templates drop you into the editor immediately.

## 🌍 5. Sculpt Terrain

- **WASD** — Move camera
- **Left-click + drag** — Apply brush (sculpt/paint depending on tool mode)
- **Mouse wheel** — Adjust brush radius
- **Panel controls** — Change tool mode, brush strength, active layer

The brush preview decal shows exactly where your edit will land.

## 💾 6. Save & Load

- **Save World** button in the panel (or F5 in demo setups)
- **Load World** restores terrain from the last save slot

Terrain is saved per-chunk to `Saved/SaveGames/TerraDyneCache/`.

## 🔹 7. Next Steps

- **API Reference:** See `Docs/API.md` for all public functions and delegates.
- **Example Blueprints/C++:** Open `/TerraDyne/Examples/` and `Docs/ExampleBlueprints.md` for survival, PCG, and replication integration examples.
- **Demo Map:** `Content/TerraDyne/Maps/DemoMap_Showcase` — pre-configured level, just press Play.
- **Settings:** Edit > Project Settings > TerraDyne Settings for chunk size, streaming radius, and more.
- **Production Integration:** See `Docs/ProductionIntegration.md` before enabling multiplayer editing, dedicated servers, external persistence, custom collision channels, or authored-world conversion.

## 🏭 Production safety defaults

- Remote client editing is disabled until an authorized `ATerraDyneEditController` subclass opts in.
- Custom PlayerControllers can add `UTerraDyneReplicationComponent`; replacing the project's controller is not required.
- Chunk state uses compressed, CRC-checked fragments rather than one oversized RPC.
- Dedicated servers keep CPU terrain/collision and skip render textures, GPU brushes, materials, and lighting.
- Landscape conversion stops when visibility holes are present because hole topology is not yet serialized.
- Database-backed games should use `ExportChunkStatePacket` / `ImportChunkStatePacket` instead of treating local SaveGame files as service persistence.

## 🔹 Troubleshooting

| Issue | Solution |
|-------|----------|
| No terrain appears | Check Output Log for `LogTerraDyne` errors. Ensure a TerraDyne Manager actor exists. |
| Sculpting doesn't work | Add input action `TerraDyneClick` in Project Settings > Input (map to Left Mouse Button). |
| Black terrain | Your material may be VHFM/Landscape type. TerraDyne chunks need standard PBR materials. |
| Grass not showing | Check that a Grass Profile is assigned on the Manager, and that grass meshes are valid. |
