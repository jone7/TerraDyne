# 🏭 TerraDyne Production Integration

TerraDyne 0.5 separates terrain technology from project ownership. The plugin can provide bounded heightfield deformation, but the host game should remain authoritative for permissions, connection relevance, persistence, gameplay actors, navigation policy, and platform budgets.

## 💡 Recommended scope

The safest production uses are explicit terrain patches:

- craters and event damage;
- shallow digging and excavation;
- nest or construction-site shaping;
- creator-mode plots;
- small deformable arenas.

TerraDyne is a heightfield. It cannot represent tunnels, arches, overhangs, or enclosed caves. It also supports four packed paint layers. Keep native Landscape/World Partition for the main world when the project depends on richer Landscape materials, visibility holes, water, splines, Landscape HLOD, or more than four paint channels.

## 🔹 Dedicated servers

The runtime no longer depends on VirtualHeightfieldMesh. On a dedicated server or non-rendering process, chunks retain CPU height arrays, DynamicMesh collision, persistence, and authoritative edit logic but do not create:

- weight textures;
- height render targets;
- upload textures;
- GPU brush resources;
- terrain material instances;
- demo lighting.

Always build the project's Editor, Game, Client, and Server targets separately. The Epic Launcher engine may not include Server target support; use a source build or installed build produced with server support.

## 🕹️ Add replication to an existing PlayerController

Projects do not need to inherit from `ATerraDyneEditController`. Add a replicated `UTerraDyneReplicationComponent` to the project's existing PlayerController.

```cpp
// MyPlayerController.h
UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
TObjectPtr<UTerraDyneReplicationComponent> TerraDyneReplication;

// MyPlayerController.cpp constructor
TerraDyneReplication =
    CreateDefaultSubobject<UTerraDyneReplicationComponent>(TEXT("TerraDyneReplication"));
```

The component:

- requests relevant initial state;
- queues state for chunks that are not ready yet;
- requests state when client chunks stream in;
- compresses and fragments server state below the network bunch limit;
- throttles outbound fragments;
- validates transfer metadata, byte limits, timeout, packet version, arrays, and CRC;
- applies server-routed brush events only to the owning connection.

The component must be a replicated default subobject of a connection-owned PlayerController. Do not place one global component on the manager.

## 🎮 Gameplay-authorized edits

Do not expose generic brush parameters directly to ordinary clients. Validate the gameplay action first, then call the manager on the server:

```cpp
FTerraDyneBrushParams Params;
Params.WorldLocation = AuthorizedImpactLocation;
Params.Radius = 900.0f;
Params.Strength = 300.0f;
Params.BrushMode = ETerraDyneBrushMode::Lower;
Params.TargetLayer = ETerraDyneLayer::Sculpt;

const bool bApplied = Manager->ApplyAuthorizedBrush(Params, true);
```

`ApplyAuthorizedBrush` enforces the global numeric and affected-chunk budgets. The game remains responsible for deciding whether the crater, nest, construction, or digging action is legal.

### 🕹️ Built-in editor controller

`ATerraDyneEditController` remains useful for a creator tool or controlled sandbox. Remote editing is disabled by default. To enable it:

1. Create a project subclass.
2. Set `bAllowRemoteTerrainEditing=true` on that server-side class default.
3. Override `IsRemoteTerrainEditAuthorized` to consult account role, game mode, edit session, patch ownership, or other project permissions.
4. Keep the built-in server limits enabled.

The controller validates rate, finite numbers, enums, layer index, radius, strength, distance from the owner, affected chunks, stroke ordering, and flatten target before applying an edit.

## 🔹 State-transfer budgets

Default settings:

| Setting | Default |
|---|---:|
| State fragment | 48 KiB |
| Maximum encoded chunk packet | 8 MiB |
| Maximum uncompressed chunk state | 64 MiB |
| Concurrent incoming transfers | 8 |
| Transfer timeout | 15 seconds |
| Outbound fragments per component tick | 4 |
| Queued state per connection | 32 MiB |
| Automatic late-join chunks | 128 |
| Terrain replication radius | 150,000 uu |

Do not raise Unreal's constructed-bunch limit to send a complete chunk USTRUCT. Tune fragment and queue budgets downward for constrained platforms. Large foliage payloads should normally stay in the host game's existing foliage system rather than terrain state.

## 🔹 Replication Graph projects

`bManagerAlwaysRelevant` defaults to false. The manager no longer carries high-frequency terrain multicast RPCs. Connection-owned components route edits and state.

Projects with a custom Replication Graph should spatialize chunk actors and ensure the local manager exists when a chunk becomes relevant. The replication component queues decoded state until its destination chunk is registered and requests a fresh authoritative state when a client chunk begins play.

## 💾 External/database persistence

The built-in SaveGame and chunk cache are appropriate for samples, standalone games, and small servers. Database-backed games can own persistence:

```cpp
TArray<uint8> Packet;
FString Error;
if (Manager->ExportChunkStatePacket(Coordinate, Packet, Error))
{
    Database->SaveTerrainPatchAsync(MapId, Coordinate, Packet);
}
```

Restore on the authoritative server:

```cpp
FString Error;
Manager->ImportChunkStatePacket(PacketFromDatabase, Error, true);
```

The packet is the same versioned, compressed, CRC-checked format used by network state and local chunk cache. Store a map/content version and patch identity beside it in the project database.

`GetActiveChunkCoordinates()` returns a stable sorted list for snapshot enumeration.

## 💾 Built-in persistence behavior

- `SaveWorld` uses `AsyncSaveGameToSlot` and coalesces repeated requests to the latest queued slot.
- Bind `OnWorldSaveCompleted(SlotName, bSuccess)` to observe the actual asynchronous result; `IsWorldSaveInProgress()` reports whether a save is active.
- World saves carry `SaveFormatVersion=2`.
- Dirty streaming chunks are encoded/compressed/written on a worker thread by default.
- Cache files use atomic temporary-file replacement.
- The latest snapshot remains in memory until its write succeeds.
- Failed writes are retried after five seconds.
- Pre-0.5 cache files are decoded and rewritten in the versioned format.

For a dedicated service, prefer the external packet API and project database rather than relying on local disk.

## 🔹 Collision and physical surfaces

Configure generated chunks in Project Settings > TerraDyne:

- `ChunkCollisionProfileName`;
- `ChunkCollisionObjectType`.

Projects with a custom Terrain object channel should select it here. The collision profile must match native terrain responses for player movement, IK, map queries, AI, vehicles, and project-specific traces.

TerraDyne does not automatically reproduce a complex Landscape layer-to-physical-material graph. Implement a project surface lookup or physical-material mask when footsteps and impact effects depend on painted layers.

## 🔹 Navigation

Terrain and population changes queue navigation dirty bounds. Overlapping bounds are merged and submitted after `NavigationDirtyDebounceTime`; the queue flushes early at `MaxPendingNavigationDirtyAreas`.

For large multiplayer games:

- disable navigation refresh for cosmetic deformation;
- restrict edits near critical paths;
- raise the debounce rather than rebuilding every brush tick;
- monitor tile-pool usage and server navigation time;
- consider updating navigation only after a committed gameplay action.

## 🎨 Authored Landscape conversion

TerraDyne 0.5 corrects two critical baking issues:

- component heights are read from the component rect through `FLandscapeEditDataInterface`, not from the complete shared texture atlas;
- every tile in an asset set uses the same canonical RGBA layer names.

Conversion stops if Landscape visibility holes are detected because the current chunk schema does not persist hole topology. This is intentional data-loss prevention. Do not disable that rejection unless filling the holes is explicitly acceptable for the target map.

The runtime mesh still supports only four paint channels and a standard mesh material. Whole-world Landscape replacement remains a specialized migration, not the default production recommendation.

## 🔹 Suggested release gates

Before shipping:

1. Build Editor, Game, Client, and Server with the actual production engine fork.
2. Run `Automation RunTests TerraDyne.Production` under NullRHI.
3. Test two real network processes, packet loss, join-in-progress, reconnect, and relevance re-entry.
4. Exercise the maximum authorized edit rate and malicious invalid inputs.
5. Measure server brush, mesh, collision, navigation, memory, state queue, and persistence latency.
6. Verify collision channels, physical surfaces, IK, and project terrain traces.
7. Restart from persisted state and test corrupt/future-version packets.
8. Validate each shipping platform independently. The descriptor allow-list is not evidence of platform QA.

## 🔹 Current limits

- four paint channels;
- heightfield topology only;
- Landscape visibility holes rejected, not converted;
- local SaveGame load remains synchronous;
- no built-in project database implementation;
- no claim of console/mobile validation from the 0.5 Win64 pass;
- the sample manager still uses a union-of-player chunk streamer, so large games should use bounded patches or replace streaming ownership.

These limits are explicit so projects can integrate TerraDyne where it adds value without allowing it to compete with mature world, persistence, or networking systems.
