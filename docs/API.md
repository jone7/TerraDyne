# ⚙️ TerraDyne API Reference

Public C++ and Blueprint API organized by system.

For multiplayer security, fragmented state transfer, dedicated servers, custom collision channels, and external persistence, read `ProductionIntegration.md` before using the lower-level sculpting API.

## ⚙️ Production integration APIs

- `UTerraDyneReplicationComponent` — connection-owned relevant brush/state delivery for any PlayerController.
- `ATerraDyneManager::ApplyAuthorizedBrush` — server-owned, bounded gameplay edit entry point.
- `ATerraDyneManager::GetActiveChunkCoordinates` — stable snapshot enumeration.
- `ATerraDyneManager::ExportChunkStatePacket` — versioned/compressed/CRC-checked database payload.
- `ATerraDyneManager::ImportChunkStatePacket` — authoritative restore and optional relevant-client correction.
- `FTerraDyneStateCodec` — C++ state packet validation/encoding/decoding.

`ApplyGlobalBrush` remains available for local tools and trusted server code. Do not expose it directly as a client RPC.

---

## 🔹 Sculpting

### 👑 `ATerraDyneManager::ApplyGlobalBrush`

```
UFUNCTION(BlueprintCallable, Category = "TerraDyne|Sculpting")
void ApplyGlobalBrush(FVector WorldLocation, float Radius, float Strength, ETerraDyneToolMode Mode);
```

**Parameters:**
- `WorldLocation` — World-space center of the brush stroke
- `Radius` — Brush radius in Unreal Units (100–10000)
- `Strength` — Brush intensity (0.0–5.0, where 1.0 is default)
- `Mode` — Tool mode (SculptRaise, SculptLower, SculptFlatten, SculptSmooth, Paint)

**Returns:** void

**Usage:** Apply a single brush tick at a world location. Call per-frame while the user holds the mouse button.

**Related Events:** `OnTerrainChanged` (broadcast after mesh rebuild)

---

### 👑 `ATerraDyneManager::BeginStroke` / `CommitStroke`

```
void BeginStroke(APlayerController* Instigator);
void CommitStroke(APlayerController* Instigator);
```

**Parameters:**
- `Instigator` — The player controller that initiated the stroke

**Usage:** Call `BeginStroke` on mouse-down, `CommitStroke` on mouse-up. This creates an undo entry for the entire stroke.

---

### 👑 `ATerraDyneManager::Undo` / `Redo`

```
UFUNCTION(BlueprintCallable, Category = "TerraDyne|Editing")
void Undo(APlayerController* Instigator);
void Redo(APlayerController* Instigator);
```

**Usage:** Reverts/reapplies the last stroke for the given player. Per-player undo stacks (configurable via `MaxUndoHistory` in settings).

---

## 🔹 Painting

### 🧩 `ATerraDyneChunk::WeightBuffers`

```
TArray<float> WeightBuffers[NumWeightLayers]; // NumWeightLayers = 4
```

Weight layers are painted via `ApplyGlobalBrush` with `Mode = Paint`. The `ActiveLayerIndex` on the ToolWidget determines which layer receives paint.

---

## 🌊 Streaming

### 👑 `ATerraDyneManager` Streaming Properties

| Property | Type | Description |
|----------|------|-------------|
| `ChunkLoadRadius` | int32 | Diamond radius for loading chunks around each player |
| `ChunkUnloadRadius` | int32 | Diamond radius for unloading (must be > LoadRadius) |
| `MaxChunkOpsPerTick` | int32 | Max spawn/destroy ops per tick to limit hitches |
| `bStreamingPaused` | bool | Temporarily freeze all streaming operations |

### 🧩 `ATerraDyneManager::GetChunkAtCoord`

```
ATerraDyneChunk* GetChunkAtCoord(FIntPoint Coord);
```

**Returns:** The active chunk at the given grid coordinate, or nullptr.

---

## 🌿 Grass / Vegetation

### 🌿 `UTerraDyneGrassProfile`

Data asset that defines grass varieties, density, height ranges, and mesh overrides. Assign to the Manager's `GrassProfile` property.

### 🧩 `ATerraDyneChunk::RequestGrassRegen`

```
void RequestGrassRegen();
```

**Usage:** Queues async grass regeneration after terrain edits. Called automatically by the sculpt/paint system after debounce.

---

## ⛰️ Migration (Landscape Import)

### ⛰️ `ATerraDyneManager::ImportFromLandscapeWithOptions`

```
UFUNCTION(BlueprintCallable, Category = "TerraDyne|Migration")
void ImportFromLandscapeWithOptions(ALandscapeProxy* TargetLandscape, FTerraDyneLandscapeMigrationOptions Options);
```

**Parameters:**
- `TargetLandscape` — Source UE5 Landscape actor to import from
- `Options` — Migration configuration (hide source, clear existing, import weights, etc.)

**Usage:** Converts existing Landscape heightmaps and weight layers into TerraDyne chunks. One-time operation per landscape.

### ⛰️ `ATerraDyneManager::MigrateLandscapeProject`

```
UFUNCTION(BlueprintCallable, Category = "TerraDyne|Migration")
void MigrateLandscapeProject();
```

**Usage:** Scans the world for all Landscape actors and imports them all with default options. Convenience wrapper.

---

## 💾 Save / Load

### 👑 `ATerraDyneManager::SaveWorld` / `LoadWorld`

```
UFUNCTION(BlueprintCallable, Category = "TerraDyne|Persistence")
void SaveWorld(const FString& SlotName = "TerraDyneSlot0");
void LoadWorld(const FString& SlotName = "TerraDyneSlot0");
```

**Usage:** Serializes/deserializes all active chunks to a save game slot. Chunk cache files stored in `Saved/SaveGames/TerraDyneCache/`.

---

## 🕸️ Population System

### 👑 `ATerraDyneManager::PlacePersistentActor`

```
UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
AActor* PlacePersistentActor(FTerraDynePopulationDescriptor Descriptor, FVector Location, FRotator Rotation);
```

**Usage:** Places a tracked actor (prop, harvestable, destroyable) that persists across save/load cycles.

---

## 🌴 Biomes

### 👑 `ATerraDyneManager::GetBiomeAtLocation`

```
UFUNCTION(BlueprintPure, Category = "TerraDyne|Biomes")
FName GetBiomeAtLocation(FVector WorldLocation);
```

**Returns:** The biome tag name at the given world location, or NAME_None.

---

## 📬 Delegates

| Delegate | Signature | Fires When |
|----------|-----------|------------|
| `OnTerrainChanged` | `FVector Location, float Radius` | After any sculpt/paint operation |
| `OnChunkLoaded` | `FIntPoint Coord` | A chunk finishes loading/spawning |
| `OnChunkUnloaded` | `FIntPoint Coord` | A chunk is removed from the world |

---

## 🔌 Integration Examples

### 🔌 `ATerraDyneSurvivalIntegrationExample`

Blueprintable actor that finds or spawns a manager, applies the packaged starter preset and grass profile, enables survival-oriented streaming/procedural defaults, and exposes `OnSurvivalWorldReady`.

### 🔌 `ATerraDynePCGIntegrationExample`

Blueprintable actor that calls `GetPCGSeedPointsForChunk`, stores the result in `LastSeedPoints`, and emits `OnPCGSeedPoint` / `OnPCGSeedPointsRefreshed` for Blueprint-side PCG or spawning adapters.

### 🔌 `ATerraDyneReplicationIntegrationExample`

Blueprintable replicated actor that demonstrates routing terrain edits through server authority with `ApplyBrushAuthoritatively`, then multicasting the brush through `ATerraDyneManager`.

See `Docs/ExampleBlueprints.md` and `/TerraDyne/Examples/` for the packaged Blueprint children.

---

## 🔔 Notifications

### 🕸️ `UTerraDyneSubsystem::ShowNotification`

```
UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
void ShowNotification(const FText& Message, ETerraDyneNotifySeverity Severity);
```

**Severity values:** `Info` (white), `Warning` (yellow), `Error` (red)

**Usage:** Display a user-facing notification. Editor builds show Slate toast popups; runtime builds show on-screen messages (controllable via `bShowRuntimeNotifications` in settings).

---

## ⚙️ Settings

All settings are in **Edit > Project Settings > TerraDyne Settings** (`UTerraDyneSettings`).

Hover any property in the Details panel for a tooltip explaining its purpose and valid range.
