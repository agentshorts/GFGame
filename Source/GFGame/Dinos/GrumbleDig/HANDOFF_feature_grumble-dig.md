# HANDOFF feature/grumble-dig

Dig planning lives in `Source/GFGame/Dinos/GrumbleDig/`. `FGFGrumbleDig::Plan` returns `TArray<FIntVector>` cells and `TArray<FGFItemStack>` yields. It does not call `AGFVoxelWorld`.

## No Build.cs change

`GFGame` already depends on `GFCore`, `GFVoxel`, `GFCompanion`, and `GFNet`. Those cover `GFBlockSize`, `FGFItemStack`, `AGFDino` / `UGFTamingComponent` / `EGFTameState`, and `AGFPlayerState` (`bIsHost`, `FactionId`, `PlayerKey`).

`GFGiant` is not a `GFGame` dependency. This folder does not include `AGFGiantDino`.

## Proposed driver (outside this folder)

`AGFGiantDino::MoveBody` keeps its current height and only yaws. A tamed companion driver (the comment on `AGFGiantDino` already points at one owned by the host) should, each tick, while `EGFTameState::Tamed` and the display name is `Big Grumble`:

1. Build `FGFGrumbleDigMove` from the body feet, velocity, and the requested pitch (do not read pitch from `MoveBody`; it flattens Z).
2. Build `FGFGrumbleDigContext` with `Species = FGFGrumbleDig::SpeciesId` or `DisplayName = Giant->DisplayName`, plus the owner's `AGFPlayerState` (`bIsHost`, `FactionId`, `PlayerKey`). `FromDino` is the same fill for an `AGFDino`.
3. Call `FGFGrumbleDig::Plan`.
4. Apply `Result.Cells` with the server edit path that already exists. Pass a cell-to-block map into `Plan` when yields should follow real blocks. Do not add a second voxel API here.

Session size stays `AGFGameState::MaxPerFaction` (seats per faction). Do not replace that with a literal player cap.

Faction ids stay opaque on `AGFPlayerState::FactionId`. This planner does not mint faction names.
