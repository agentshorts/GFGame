# HANDOFF: feature/playable-hud

Branch off `staging` at `fa4b4c3`. Canvas HUD only. No `.uasset`, no `.umap`, no Unreal build here.

## What changed

`AGFHUD` (`Source/GFGame/UI/`) draws with `DrawHUD` / `UCanvas` and `GEngine->GetSmallFont()`. No UMG and no textures.

- Centre crosshair (short white plus).
- Bottom row of `AGFVoxelWorld::GetPlaceableBlockIds()`, the same list `AGFCharacter::CycleBlock` walks. `AGFCharacter::SelectedBlock` is the highlighted cell. The row stays centred and shrinks to the side margins when it would not fit.
- Controls hint, the exact line `WASD move, Space jump, LMB dig, RMB place, wheel block, V view, Enter chat`. Shown until 30s, then hidden. H toggles it, and that choice sticks. H does nothing while chat is open.
- Top-left land and state from `UGFTerritorySubsystem::GetZoneAt`, `GetHazardAt`, and `IsSafeFor` at the pawn. See #105.
- Bottom-left, the last 8 `AGFGameState::ChatLog` lines. Each stays fully opaque for 15s from the moment this HUD first sees its `Seq`, then fades over 1.5s. The sender name uses `AGFPlayerState::ColorIndex` when `FindPlayerByKey` finds that `SenderKey`. Otherwise the name is white. A line with an empty sender name is drawn as system text.
- `AGFPlayerState::LandNotices` and `ChatResults` toast in the upper middle for 4s, including result codes such as `Ok`.

Chat entry is one `SEditableText` added with `GEngine->GameViewport->AddViewportWidgetContent` (not `SEditableTextBox`, so Escape is handled on the focused widget). Enter is deferred one tick so the key that opens the line does not also submit it. Enter sends the trimmed line through `AGFPlayerState::RequestChat`. An empty Enter or Esc closes it. Input is UI-only while the line is open, then game-only again, cursor hidden.

`AGFGameMode`'s constructor sets `HUDClass = AGFHUD::StaticClass()`. The `.cpp` also includes `GFHUD.h`. That is the whole game-mode edit. `GFCharacter.cpp` is untouched. Enter, H, and Esc are an enhanced-input mapping on `AGFPlayerController` at priority 1 (`IMC_GFHUD`), above the pawn context.

`Source/GFGame/UI` is on `GFGame` `PrivateIncludePaths`. The module now depends on `Slate` and `SlateCore`.

## Defaults

Logged as issue #105 (`Decision: canvas HUD labels, name colours, and chat timings`).

- Unsafe faction land reads `Rime · unsafe`. A faction hit with no id reads `Land`. Neutral reads `Neutral`. Clear wild reads `Wild`. A wild hazard reads `Wild · LavaBridge`, using the hazard id, or its kind if the id is none.
- Name colours are 10 fixed colours, wrapping on `ColorIndex`. There is no name-tag palette in the project yet.
- Chat fade is 1.5s after the 15s hold. Toasts last 4s.

## Config keys

None.

## Tests

`EditorContext | EngineFilter`, not run here (no Unreal build):

- `GFGame.HUD.ZoneLabel`
- `GFGame.HUD.ChatFade`
- `GFGame.HUD.HotbarLayout`
- `GFGame.HUD.HintAndColor`
- `GFGame.HUD.ControllerClass` spawns a game world, expects `AGFGameMode`, and checks the controller's HUD is an `AGFHUD`. A bare world has no net driver, so the test calls `ClientSetHUD_Implementation` (the body of the client RPC).

## Dev PC steps

None.

## Follow-ups

A UMG / Common UI pass replaces this canvas HUD later. That is a follow-up, not a Decision.

## Issue notes

#105 was opened for the defaults above and points at work issue #82. Comments on #82 were rejected by the token (`Resource not accessible by integration`), so the start note and the final hash could not be posted there.
