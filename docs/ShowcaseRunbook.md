# ✨ TerraDyne Showcase Runbook

Use this runbook when you need to show TerraDyne's major shipped capabilities in one continuous product tour.

The goal is not to present a disconnected feature list. The goal is to prove one clear product story:

**Start from a real authored Unreal Landscape, convert it into TerraDyne chunks, edit it at runtime, persist the world state, and then build gameplay systems on top of that runtime-owned terrain.**

## 📖 What This Runbook Should Prove

By the end of the walkthrough, the viewer should have seen:

1. A real authored landscape as the starting point.
2. Landscape-to-chunk conversion.
3. Weight-layer capture.
4. Placed foliage transfer.
5. Actor foliage transfer.
6. Runtime sculpting and paint.
7. Save and load persistence.
8. Manager-owned gameplay hooks and world metadata.
9. Chunked runtime behavior on a larger map.

## 🛠️ Recommended Setup Before Recording

1. Open a level that contains a real Unreal Landscape, not a synthetic test patch.
2. Make sure the landscape components you want to import are loaded.
3. Make sure the source level contains visible paint variation, placed foliage, and actor foliage so the conversion result is obvious.
4. Decide whether the runtime edit portion should use the TerraDyne edit controller UI or a gameplay controller.
5. If you want replication proof, prepare a second client before you record.
6. If you want a clean reveal, enable source-landscape hiding during conversion.
7. If the source material is a typical Landscape material graph, do not rely on `Adopt Landscape Material As Master Material` for the demo.

## ✨ Step-by-Step Showcase

### 🎨 1. Start On The Authored Landscape

Open on the untouched source world.

Show:
- the authored landscape shape
- visible paint variation
- placed foliage instances
- actor foliage or foliage actors

Say:
- this is a normal authored Unreal Landscape workflow
- TerraDyne starts from the landscape the team already built

Success condition:
- the viewer can clearly see that the starting point is a real authored landscape, not a TerraDyne-native blank world

### 🛠️ 2. Open The Setup Wizard

Open:

`Window > TerraDyne Setup Wizard`

Choose:

`Authored World Conversion`

Show:
- the selected landscape
- the baked Landscape asset set picker
- the conversion options

Call out:
- if the landscape does not appear, use `Refresh`
- the conversion path resolves the full landscape group, not only one selected proxy
- the recommended flow now bakes runtime-safe authored-world data and initializes from that same payload

Success condition:
- the audience understands that conversion is a guided workflow, not a manual chunk-by-chunk rebuild

### 🔹 3. Configure The Import Safely

Before starting the conversion:

1. Confirm the correct landscape is selected.
2. Keep the desired landscape components loaded.
3. Leave live import off unless you explicitly need the editor-only fallback.
4. Enable source hiding if you want the post-conversion reveal to be cleaner.
5. Leave `Adopt Landscape Material As Master Material` off unless the source material is already known to be compatible with TerraDyne's runtime material path.

Show:
- the options panel
- the selected landscape entry

Call out:
- TerraDyne now refuses obviously incompatible landscape-only material adoption
- the point of this step is stable conversion, not risky one-click material reuse

Success condition:
- the audience sees that the conversion path is defensive rather than fragile

### 🔹 4. Run The Conversion

Start the conversion from the wizard.

Show:
- the initialization or migration action
- chunk creation in the level or Outliner

Call out:
- TerraDyne imports loaded landscape components into runtime chunks
- migration metadata is preserved on the manager for later use

Success condition:
- TerraDyne chunk actors are created and the authored terrain shape is represented as runtime-owned chunk data

### 🔹 5. Validate The Converted Result In Editor

Before pressing Play, inspect the converted result.

Show:
- the TerraDyne manager
- TerraDyne chunk actors
- the migrated terrain footprint
- the source landscape hidden state if that option was enabled

Call out:
- this is no longer just a source Landscape actor
- runtime ownership has moved to TerraDyne chunks and manager state

Success condition:
- the viewer can see that conversion produced a real runtime structure, not a visual trick

### 🔹 6. Prove Weight-Layer Capture

Inspect the converted terrain with paint-aware material or tool context.

Show:
- areas where source paint variation is preserved
- the active TerraDyne layer behavior if the edit UI is enabled

Call out:
- the conversion records layer mappings during import
- the runtime system keeps terrain paint meaningful after migration

Success condition:
- painted areas remain differentiated after conversion

### 🔹 7. Prove Foliage Transfer

Show both foliage paths clearly.

Show:
- placed foliage that came across with the terrain
- actor foliage that was also transferred

Call out:
- TerraDyne does not only move height data
- the authored world context comes with it

Success condition:
- the viewer can identify both foliage categories in the converted result

### ⛏️ 8. Enter Play Mode And Show Runtime Editing

Press Play and move into the converted world.

If the TerraDyne edit controller is active, show:
- brush preview
- sculpt
- paint
- undo
- redo

If you are using a gameplay controller instead, explain that the converted terrain is still manager-owned and ready for gameplay systems even without the edit UI exposed.

Call out:
- runtime sculpt and paint happen on chunk-owned data
- the terrain is being modified live, not through editor-only tooling

Success condition:
- the viewer sees at least one live sculpt change and one live paint change in play mode

### 💾 9. Save The World State

Make a visible terrain edit first. Then save.

Show:
- a noticeable sculpt or paint change
- the save action

Call out:
- TerraDyne persists chunked terrain state instead of treating runtime edits as disposable

Success condition:
- the viewer knows exactly what change should survive the reload

### 🔹 10. Load The World State

Reload the saved state.

Show:
- the same edited area after load
- the terrain state restored correctly

If your demo also preserves migrated or persistent world metadata, call that out here as well.

Success condition:
- the saved terrain change is visibly restored

### 👑 11. Show The Manager As The World-State Owner

Open the manager details, a sample Blueprint, or a diagnostic view.

Show:
- that the manager owns the runtime world state
- where preset, procedural, biome, build-rule, or AI metadata lives

Useful examples to surface:
- `ApplyWorldPreset`
- `GetBiomeTagAtLocation`
- `CanBuildAtLocation`
- `GetAISpawnZonesAtLocation`
- `GetPCGSeedPointsForChunk`

Call out:
- this is where TerraDyne becomes a runtime world framework rather than only a terrain deformation tool

Success condition:
- the audience understands that gameplay systems query TerraDyne through the manager

### 🔹 12. Show Scale And Runtime Behavior

Move across a larger portion of the converted world.

Show:
- multiple chunks
- runtime interaction across the authored footprint
- any relevant streaming, collision, or LOD behavior

Call out:
- TerraDyne is chunk-based at runtime
- performance behavior is managed at the chunk level, not as one monolithic terrain object

Success condition:
- the viewer sees the system operating as a world runtime, not as a single local terrain toy

### 🔹 13. Optional: Show Replication

If multiplayer proof matters for the audience:

1. Launch a second client.
2. Make a visible terrain edit on the authority side.
3. Show the remote client receiving the update.

Call out:
- TerraDyne supports networked runtime terrain workflows

Success condition:
- the terrain edit is visible on the second client

### 🔹 14. End With The Correct Product Positioning

Close the showcase with the product story, not a feature dump.

Recommended close:

`TerraDyne converts a shipped-quality authored Unreal Landscape into a persistent, chunked, runtime-editable world layer, then exposes the hooks needed to build sandbox, survival, and open-world systems on top of it.`

## 💡 Recommended Demo Order

If you need the shortest reliable sequence, use this order:

1. Source landscape
2. Conversion in the setup wizard
3. Converted chunks in editor
4. Runtime sculpt and paint
5. Save and load
6. Manager-owned gameplay hooks
7. Larger-world chunk behavior

## 🔹 What To Avoid

Do not:

- lead with abstract framework language before showing the authored landscape
- present the product as only a narrow terrain deformation utility
- over-focus on support details like controller UI toggles
- mix speculative future ideas with shipped proof points
- rely on fragile material adoption for the main conversion demo

## 🔹 Capture Checklist

Use this checklist if you are recording a trailer, Fab page video, or documentation walkthrough.

- Source landscape visible before conversion
- Setup wizard with authored conversion selected
- Chunk creation or converted result in editor
- Paint-layer preservation
- Placed foliage transfer
- Actor foliage transfer
- Runtime sculpt
- Runtime paint
- Save action
- Load proof
- Manager gameplay-hook view
- Larger-world runtime shot
- Optional second-client replication proof
