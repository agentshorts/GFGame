# VoxelFinish milestone 1

Paint is baked. Effect graphs are size-checked and rejected when they break the limits. They are not evaluated per block.

## Done

- Plugin `Plugins/VoxelFinish`, mounted as `/VoxelFinish/`. It links only engine modules.
- `FFinish`: tint, pattern id, two colours, effect id, intensity, and a baked glow flag. Canonical hash is FNV-1a over 8-bit colours, pattern id, effect id, intensity in thousandths (0..8), and the glow flag. The same fields always hash to the same palette id.
- Per-world palette. FinishId 0 is unpainted. Cap is 4095 (ids 1..4095), so every id has a column in the 4096-wide lookup. Garbage collection drops a finish only when no painted block and no blueprint reference still uses it. Blueprints store the full `FFinish` and re-resolve the id on restore. Saves (version 2) store only occupied palette slots; older dense saves still load. On load, a saved slot whose id does not fit the palette (slot 4096 of a version 0 save) is re-added under a free id and its cells are remapped to it; if the palette is full those cells load unpainted, as does any cell whose id is not in the restored palette.
- Server paint goes through `UVoxelFinishPainterComponent` (add it to the PlayerController, PlayerState, or possessed Pawn). Its `ServerRequestPaint(Cell, Finish)` RPC runs on an actor the client owns, and the server derives the payer from that owner (`ResolvePayer`). `AVoxelFinishVolume::AuthorityPaint` is the non-RPC server call; the volume has no RPC. Non-authority sessions are rejected. The dye hook is `TFunction<bool(const FFinish&, APlayerState*)> CanAffordAndSpend`. The game spends materials inside that hook. This plugin has no currency of its own. An unbound hook refuses the paint (`RejectedCannotAfford`), so nothing is painted for free until the game binds one. Painting a cell with the finish it already holds returns `AlreadyPainted`, is not charged, and replicates nothing.
- Client finishes are validated on the server (`FVoxelFinishSession::SanitizeFinish`): non-finite colours or intensity, a pattern id that is not 0-15 or 255, and an effect id that is not registered are rejected (`RejectedInvalidFinish`). Colours are clamped to 0..1 and intensity to 0..8. The volume registers the built-in sparkle id and the `effect_index` of every accepted starter graph; games add more with `AllowEffectId`.
- Replication is a palette fast array plus a per-chunk delta fast array. A repaint marks only that chunk dirty. When the server garbage-collects a slot, `FVoxelFinishPaletteItem::PreReplicatedRemove` clears it from the client palette and lookup (unless a live item already reuses that id).
- Server guards on `UVoxelFinishPainterComponent`, checked in this order before anything is spent: rate limit (`MaxPaintsPerSecond`, default 10; token bucket per painter that holds one second of paints; every request counts, including rejected ones; 0 turns it off) -> `RejectedRateLimited`, dropped without a rejection RPC and counted in `GetNumRateLimited`; no volume; no payer; the target cell (`AVoxelFinishVolume::SetPaintTargetResolver`; the game reports whether the cell is a real block and its centre; unbound refuses) -> `RejectedNoBlock`; reach from the owning pawn to that centre (`MaxReachCm`, default 600, or the value of `SetMaxReachProvider` read at paint time; 0 or less means no limit) -> `RejectedOutOfReach`. The resolver only reads, so a rejected or accepted paint never creates a chunk.
- GFGame binds the hooks in `AGFPaintVolume` (spawned by `AGFGameMode::StartupDirectorClasses`). `CanAffordAndSpend` burns dye items through `AGFGameState::ServerBurn` (reason `Paint.Dye`) at `FGFDyes::CostForBlocks` rates: one dye per colour family per 8 blocks, a pattern pays for both visible colours in one burn, all or nothing. `bFreeBuild` makes paint free. The resolver accepts only solid cells of the authoritative `AGFVoxelWorld` grid. `AGFPlayerController` carries the painter.
- GFGame hookup, follow-ups (`feature/gf-paint-followups`):
  - Reach: `AGFPlayerController` feeds the painter the game state's `MaxEditReach` at paint time (`SetMaxReachProvider`); 0 or less means no limit.
  - Rate-limited paints are dropped on the server without a rejection RPC (`UVoxelFinishPainterComponent::GetNumRateLimited` counts them).
  - Drawn: `AGFVoxelWorld` has a display-only paint overlay. Painted blocks get their own instanced mesh per (chunk, block, colour), drawn with a dynamic instance of the block's material whose `Color` vector parameter (`AGFVoxelWorld::PaintColorParameter`) is set. Only the base colour is shown (`FGFPaintCost::DisplayColor` = ColorA x Tint). `AGFPaintVolume` gets changes through the volume's `OnPaintChunksChanged` / `OnPaletteSlotChanged` hooks (every machine) and pushes them to the overlay each tick.
  - Cleared: on the server, removing a block (`OnBlockChanged` with no block) or replacing it with `FillLayers` (fires `OnBlockChanged` for each replaced block) unpaints that cell through `AVoxelFinishVolume::AuthorityClearCells`. `ClearAll` and `LoadData` fire `OnGridReset`, after which `AuthorityClearUnresolvable` unpaints every cell that is no longer solid. A finish whose last block is cleared is freed from the palette (`FVoxelFinishSession::ServerClearCells`). Nothing is refunded.
  - Saved: world save version 6 (`EGFSaveVersion::BlockPaint`) stores `UGFSaveGame::Paint` (`FVoxelFinishSaveData`). `ApplyToWorld` restores it after the blocks with `AVoxelFinishVolume::AuthorityRestore`, which replicates it and unpaints cells that are not solid. Saves up to version 5 load with no paint. `FVoxelFinishSaveData` and `FFinish` fields are `SaveGame`, because `SaveGameToMemory` skips properties without that flag.
- `IVoxelFinishMeshSource` is the seam the game voxel mesh implements later. The plugin does not reference game modules. `FVoxelFinishGrid` is a standalone implementation used by the session and the tests.
- `VoxelFinish::WriteFinishToVertex` writes the finish id into UV channel 2 (exact integer in X, 16x16 face texel packed in Y).
- Lookup texture `T_FinishLookup` (4096x4, nearest, clamp, sRGB off). Opaque master `M_FinishOpaque` and emissive master `M_FinishEmissive`. No per-block parameters and no material instances. Pattern atlas and sparkle flipbook are nearest. Glow is a baked flag in the lookup. Sparkle is a global-time 8-frame flipbook at 12 fps. Past 3200 cm the material keeps the paint and drops glow and sparkle.
- Caps: 32 distinct effect finishes per chunk, 256 distinct effect finishes in the world. Plain paint does not consume those caps. The grid stores cells per chunk and keeps per-id block counts world-wide and per chunk, so the cap checks, `ClearChunk`, and `ExportChunk` never walk the whole world.
- Effect-graph limits: 32 nodes on an authored graph, flattened SubEffect graphs up to 64 nodes and 128 op cost, depth 3, combined steps 64. Cycles (a graph of nodes, or an effect that reaches itself through SubEffects) are rejected.
- Content folders: `Textures/Atlas`, `Textures/Flipbooks`, `Materials`, `Effects/Starter`. Source masks are the PNGs. The editor console command `VoxelFinish.BuildContent` imports them and saves the uassets (nearest, no mips, sRGB off). The test `VoxelFinish.Content.MasterMaterials` only loads and checks the saved assets; it never writes a .uasset.

## Left

- The game voxel world does not implement `IVoxelFinishMeshSource`, and its blocks do not use the finish masters or the lookup texture. Paint is drawn by the GFVoxel overlay instead (base colour only, see Done).
- Effect graphs are not shaded and are not baked into the dynamic 256-slot atlas. The lookup stores colours, pattern id, glow, intensity, and the sparkle flag. Arbitrary graphs stay data until a later bake step.
- Each `AVoxelFinishVolume` owns a transient lookup texture (`GetLookupTexture`, 4096x4, nearest, linear; not created on a dedicated server). Only the columns of palette ids that changed are re-uploaded (`UpdateTextureRegions`). The shared `T_FinishLookup` asset is no longer written at runtime.
- Both masters sample the lookup through a `FinishLookup` texture parameter (its default is the blank `T_FinishLookup`), and `ApplyLookupTo(UMaterialInstanceDynamic*)` binds the volume's texture (one instance per master). `M_FinishOpaque` and `M_FinishEmissive` were regenerated with `VoxelFinish.BuildContent rebuild`; `VoxelFinish.Content.MasterMaterials` fails if either master samples `T_FinishLookup` directly again. Headless regeneration: `UnrealEditor-Cmd.exe GFGame.uproject "-ExecCmds=VoxelFinish.BuildContent rebuild,QUIT_EDITOR" -nullrhi -unattended -nopause -nosplash -log` (ExecCmds separates commands with commas; `;Quit` would be read as an argument and the editor would not exit). Without `rebuild` the command only creates missing assets and leaves existing textures and materials alone.

## Follow-ups

GFGame hookup, remaining risks (`feature/gf-paint-followups`, accepted for the playtest):
- Only the base colour (ColorA x Tint) is drawn. Patterns, the second colour, glow and effects are saved and replicated but not shown.
- A block material needs a `Color` vector parameter for paint to show; without one a painted block looks unpainted.
- Each colour used in a chunk adds an instanced mesh (and a draw call). Many colours in one chunk cost draw calls.
- A changed palette slot redraws by walking every painted cell (O(painted cells)); `AuthorityClearUnresolvable` (grid resets, loads) is O(painted cells) too.
- Clearing paint (removal, `FillLayers`, resets, loads) refunds nothing.
- Unused finishes left by repaints are still freed only when the game calls `CollectGarbage`; clearing a cell frees only the finish it held.
- Loading a world replaces all paint; loading a version 5 or older save leaves the world unpainted.
- GFSave depends on the VoxelFinish plugin.
- Paint credits (one dye item covers 8 blocks) live on the server for the session only. They are not saved, so up to 7 blocks of an already burned dye are lost on a server restart.
- No plot-permission check for paint: any player can paint any solid block in reach.
- `FGFBlockRow::bDyeable` is not enforced (only Fabric is dyeable in `DT_Blocks`); every solid block can be painted.

Item 7 (palette lifetime):
- No automatic garbage collection. `FVoxelFinishSession::CollectGarbage` has to be called by the game; nothing schedules it.
- Restoring a save does not re-add blueprint references, so a GC right after load can drop a finish that only a blueprint still uses.
- The blueprint finish list only grows. Nothing removes entries for deleted blueprints.

Item 9 (schema and effects):
- `effects_schema_v3` and `FFinish` do not match: the schema has a glow mask and an effect id plus revision, while `FFinish` has a single glow flag and an effect id with no revision.
- Sparkle is hard-coded (`VoxelFinish::SparkleEffectId = 4`) instead of being looked up from the starter graph.

## Interface

`IVoxelFinishMeshSource`:

- `GetFinishId(FIntVector) const`
- `SetFinishId(FIntVector, uint16)` (no-op unless the implementation is the authority)
- `MarkChunkDirty(FIntVector)`
- `ForEachPaintedBlock(TFunctionRef<void(FIntVector, uint16)>) const`

`FVoxelFinishSession::ServerPaint` is the authority command. `UVoxelFinishPainterComponent::RequestPaint` is the client entry point (server RPC on a player-owned actor); `AVoxelFinishVolume::AuthorityPaint` is the server call it makes. `FEffectGraphRegistry::Validate` enforces the graph limits. `VoxelFinish::WriteFinishToVertex` / `DecodeFinishVertex` own UV2.

## Tests

Filter `VoxelFinish`. Command:

`UnrealEditor-Cmd.exe GFGame.uproject -ExecCmds="Automation RunTests VoxelFinish;Quit" -nullrhi -unattended -nopause -nosplash -log`

Last run (Win64 Development editor, `feature/gf-paint-followups`), filter `GFGame+VoxelFinish`: 137 tests (121 GFGame, 16 VoxelFinish), 137 passed, 0 failed.

Keep `;Quit` inside the Automation command. `-ExecCmds="Automation RunTests X,QUIT_EDITOR"` quits as soon as the tests are queued, so nothing runs.

- `VoxelFinish.DedupeHash`
- `VoxelFinish.PaletteCap`
- `VoxelFinish.GCBlueprintRefs`
- `VoxelFinish.ClearCells` (unpaint cells, free only the finishes they held, blueprint refs kept, clients cannot clear)
- `VoxelFinish.VertexEncoding`
- `VoxelFinish.Caps` (per chunk, global, distance cutoff)
- `VoxelFinish.SerializationRoundTrip` (sparse version 2 and legacy dense saves, legacy id-4096 cell remap, full-palette and unknown-id cells load unpainted)
- `VoxelFinish.Paint.Authority` (client reject, unset dye hook refuses, dye hook, single-chunk dirty, replica delta, same-finish repaint not charged, client drops a garbage-collected slot)
- `VoxelFinish.Paint.PlayerOwnedPath` (painter component RPC, volume has no RPC)
- `VoxelFinish.Paint.Validation` (non-finite, pattern id, unregistered effect id, clamping)
- `VoxelFinish.Grid.Counts` (per-chunk storage, world and chunk use counts)
- `VoxelFinish.Effect.Limits` (authored size, flatten, depth, cycles, starter graphs)
- `VoxelFinish.LookupBake`
- `VoxelFinish.Content.MasterMaterials` (read-only; both masters sample the `FinishLookup` parameter and never `T_FinishLookup` directly)
- `VoxelFinish.Paint.RateLimit` (token bucket: burst, refill, clock going back, cap, slow and disabled rates)
- `VoxelFinish.Paint.ServerGuards` (defaults, unbound resolver, air and out-of-world cells, reach, rate limit through the painter; rejected paints spend nothing and create no chunk)

GFGame tests (filter `GFGame`):

- `GFGame.Paint.Cost` (colours and dye families, 8 blocks per dye, two-colour patterns all or nothing, ledger burn reason)
- `GFGame.Net.Paint` (client RPC: dye burned once, free repaint, CannotAfford, OutOfReach against the game state's reach, NoBlock for air and outside the world with no chunk created, burst of 15 rate limited to 10 with no rejection sent, reach 0 = no limit, the client draws the paint, a removed block's paint is cleared on the client)
- `GFGame.Paint.Display` (painted mesh and material colour, shared colour mesh, repaint, pattern drawn as ColorA x Tint)
- `GFGame.Paint.Removal` (removed block unpainted, finish freed with its last block, re-placed block unpainted, FillLayers clears replaced blocks only, LoadData and ClearAll)
- `GFGame.Paint.Save` (version 6 round trip through `SaveGameToMemory`, a missing block loads unpainted, a version 5 save loads with no paint)
