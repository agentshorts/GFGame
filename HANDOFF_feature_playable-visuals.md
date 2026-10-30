# HANDOFF feature/playable-visuals

Skeletal meshes for the player, the dinos that have art, and a Big Grumble row. Branched from staging `fa4b4c3`. No `.uasset` or `.umap` edits. Not built here.

Work issue: #79. Defaults: #109.

## What changed

`DT_AssetMap` keeps the old columns and adds six trailing ones: `SkeletalMesh`, `IdleAnim`, `MoveAnim`, `ArtScale`, `ArtOffset`, `ArtRotation`. `Mesh` stays `None` on every existing row, so the static-mesh path is unchanged.

`FGFAssetMap::ApplySkeletal` loads that skeletal mesh onto a `USkeletalMeshComponent` and hides the static-mesh fallback. If the path is empty or fails to load, it logs once (`LogGF`) and keeps today's tinted placeholder. It does not crash. `ApplySkeletalFromRow` is the same call with a row already in hand (the missing-asset test uses it).

`UGFSimpleAnimComponent` (GFCore) ticks after gameplay updates. It plays `MoveAnim` looped when horizontal speed is above 20 cm/s, otherwise `IdleAnim` looped, with `SetAnimationMode(AnimationSingleNode)` and `PlayAnimation`. The clip changes only when that choice changes. `PauseFor(Seconds)` holds locomotion off so a one-shot can play; a non-looping clip is also left alone until it ends. `UGFSocialComponent::ShowEmote` uses that path when the pawn has the component, and still uses a montage when it does not.

Wired:

- `AGFCharacter` uses `GetMesh()`. The constructor sets Z -88 and yaw -90, then the `Player.Base` row overwrites that. `BeginPlay` calls `ApplySkeletal`. First person is still owner-no-see. The mesh does not collide, so the spring arm does not push in on the body.
- `AGFDino` has `Art` next to `Body`. `ApplyPlaceholder` calls `ApplySkeletal(Art, Body, AssetKey(Species))`. `Body` is still there. The character's own mesh stays hidden. `Tick` and `SetAttackTarget` are untouched.
- `AGFGiantDino` has `Art` under `ScaleRoot`. `ApplyLook` calls `ApplySkeletal` and still scales `ScaleRoot` by `MeshScale`.

Rows with a mesh: `Player.Base`, `Dino.Raptor`, `Dino.Tusksaw`, `Dino.Lanternmaw`, `Dino.Scythepaw`, `Dino.Ripwing`, `Dino.BigGrumble`. `Dino.Nipper`, `Dino.Sailback`, `Dino.Rex`, and `Dino.Hornback` stay spheres. Raptor had no `DT_AnimMap` rows; `Raptor.Idle`, `Raptor.Walk`, and `Raptor.Run` were added. Placeholder colours on the new dino rows are only for the sphere fallback.

## Config keys

None. Facing, scale, offset, and which clip plays are cells in `Data/CSV/DT_AssetMap.csv`.

## Tests

`GFGame.Visuals.*` (`EditorContext | EngineFilter`), in `Source/GFTests/Private/GFVisualsTests.cpp`:

- `AssetMapColumns` — 13 columns, old ones first, every row has them all.
- `Rows` — `Player.Base` parses; art rows point at skeletal meshes and their idle/move keys exist; Raptor's old colour and placeholder scale still parse; Nipper, Sailback, Rex, and Hornback stay empty.
- `MissingSkeletalFallsBack` — a CSV whose skeletal path does not exist parses, does not load, and the static mesh becomes the row's sphere.
- `ChooseAnim` — idle at 20 cm/s and below, move above that, idle when there is no move clip.
- `Wiring` — player, dino, and giant components; `Player.Base` shows the mesh and hides the sphere.

```
UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Visuals;Quit" -unattended -nullrhi
```

Not run here.

## Dev PC steps

- Press Play on `L_Hub`. The player should be the skeletal mesh in third person (V still hides it for the owner). Raptors and any dino whose species is Tusksaw, Lanternmaw, Scythepaw, or Ripwing should be the art mesh, idling and running. Nipper, Sailback, Hornback, and Rex stay spheres.
- Check each skeletal mesh's forward axis and size in PIE. If one faces sideways or floats, edit `ArtRotation`, `ArtOffset`, or `ArtScale` on that row. That CSV edit is the only change needed. The meshes were imported with Force Front X, so yaw 0 is the starting guess (#109).
- If `/Game/Data/DT_AssetMap` exists as an asset, delete or reimport it so this CSV wins. Today the game reads the CSV when that asset is missing.
- Big Grumble still looks up `Dino.Rex` (`DT_GiantDino.AssetKey`). To show `SK_Dino_BigGrumble`, point that cell at `Dino.BigGrumble` and update the test that expects `Dino.Rex`. `Art` is already on the giant, under `ScaleRoot`, at `MeshScale` (3.5). The intended art size is `ArtMeshScale` (4); using that means the event component calls `BodyScale(true)`, which this branch does not touch.

## Follow-ups

- Giant asset key, above. Left alone so the "T. rex placeholder" test and #61 stay true, and so this branch does not edit `GFGiantEventComponent` / `GFGiantTypes`.
- Dino capsules are still the default character capsule. Large meshes stick out of it. Collision size is separate from `ArtScale`.
- Above 20 cm/s everything plays its Run clip (Prowl for Big Grumble), including a slow step. There is no walk/run blend and the play rate is 1, not the speed the clip was authored for (Raptor walk is about 300 cm/s, run about 700).
- Raptor attacks and the other raptor clips are not in `DT_AnimMap`. Only Idle, Walk, and Run were added.
- Actors moved with `SetActorLocation` (the giant) have no velocity. The anim component treats a step of more than 1 cm as movement, so a prowl still counts once the mesh is shown.

## Issue comments

Creating #109 worked. Commenting on #79 returned HTTP 403 (`Resource not accessible by integration`), so the progress notes could not be posted on the work issue. Paste these there:

Start:

> Started on `feature/playable-visuals` from staging `fa4b4c3`. Trailing `DT_AssetMap` columns and `FGFAssetMap::ApplySkeletal`. `UGFSimpleAnimComponent` plays idle or move from speed, and `PauseFor` lets an emote hold the mesh. Wired into the player, dinos, and the giant. `Dino.Rex` stays a sphere (#61). Defaults: #109.

Finish (use the branch tip as the hash, and sign it the same way as the other playable issues):

> Done on `feature/playable-visuals` at `<hash>`. Player, Raptor, Tusksaw, Lanternmaw, Scythepaw, and Ripwing use skeletal meshes. Big Grumble's row is `Dino.BigGrumble`; the giant still asks for `Dino.Rex`, so it stays a sphere until that cell changes. Tests: `GFGame.Visuals.*`. HANDOFF: `HANDOFF_feature_playable-visuals.md`. Defaults: #109.
