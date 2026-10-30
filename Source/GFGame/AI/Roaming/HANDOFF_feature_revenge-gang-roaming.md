# HANDOFF feature/revenge-gang-roaming

Roaming lives in `Source/GFGame/AI/Roaming/`. Nothing outside this folder was edited.

## Already in the game (reused, not reimplemented)

- `AGFDino` / `UGFTamingComponent`: species, companion id, follow and attack movement (`MoveToward`).
- `AGFPlayerState`: `PlayerKey`, `FactionId`, `bIsHost`, `bLoggedOut`.
- `AGFGameState::MaxPerFaction` and `FindPlayerByKey`. Session cap is `GFRoamingFactions::Count() * MaxPerFaction` (Cinderwrights, Verdant, Rime).
- Map packages: `UGFTamingGateSubsystem` (`/Game/Maps/L_Valley`), `UGFMobaGateSubsystem` (`/Game/Maps/L_Arena`), `UGFBeatHopGateSubsystem` (`/Game/Maps/L_Beat`), `UGFFashionGateSubsystem` (`/Game/Maps/L_Runway`).
- `UGFMiniGameGateSubsystem::EnterMiniGame` already broadcasts `OnBeforeEnter` and then `OpenLevel`. That is the travel call.

Not the same feature, left alone: `IGFSafeZoneQuery` (giant stays out of safety), taming `PackId`, `DT_ModeRules.bEmoteAttractsDanger` (emote noise), jail leash.

## Required outside this folder

### 1. Include path (`Source/GFGame/GFGame.Build.cs`)

Headers sit in `AI/Roaming/`, not `Public/` or `Private/`. Quoted includes from those cpp files are enough for them. UHT's generated cpp includes `"GFRoamingPolicyComponent.h"` and `"GFRoamingSubsystem.h"` by filename, so the module needs that directory on its include path:

```csharp
PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "AI/Roaming"));
```

Do not point `GFCompanion` at `GFGame` (cycle). `GFGame` already depends on `GFCompanion`, `GFNet`, `GFMoba`, and `GFRhythm`.

### 2. Hand the record to travel

`UGFRoamingSubsystem` is a game-instance subsystem, so it survives `OpenLevel`. Call it around the existing gate:

- Before `OpenLevel` (the `OnBeforeEnter` lambda is the right place, in `UGFMiniGameGateSubsystem::EnterMiniGame` or a subscriber in `GFGame`): `UGFRoamingSubsystem::Get(World)->BeginTravel(Gate->GetMapName())`. `EnterMiniGame` only broadcasts when the map package exists. `L_Arena` and `L_Beat` are not in `Content/Maps` yet, so that hook does not run until those maps land. `BeginTravel` / `ConsumeTravel` are what the tests call.
- On the destination map, once (`AGFGameMode::InitGame` in `Source/GFGame/Private/GFGameMode.cpp`): `UGFRoamingSubsystem::Get(this)->ConsumeTravel()`. A second call is a no-op. The spent record leaves the target on the new board.

`GFCore` cannot include `GFGame`. Keep the two calls in `GFGame`.

### 3. Ask the leash before `AGFDino` moves

`AGFDino::Tick` (`Source/GFCompanion/Private/GFDino.cpp`) calls `MoveToward` with no zone check. `GFCompanion` cannot include this component. Add a small interface on `GFCore` (both modules already depend on it), implemented by `UGFRoamingPolicyComponent`:

- `CanPathTo(Goal)` / `ClampFollowGoal(Goal)` are already on the component.
- In the Follow and Attack branches, if a component implements the interface and `CanPathTo` is false, move to `ClampFollowGoal` instead. If that is still false, do not step.
- Spawn path: `UGFRoamingPolicyComponent::AttachToDino(Dino, Home, HomeMap, MemberId)` from `GFGame` (game mode or a director). Do not construct the component inside `AGFDino`.

Revenge-gang dinos: `JoinGang(Subsystem->EnsureBoard(), GFRoamingLimits::SeatsPerFaction(GameState), OutCode)` then `SetGangTargetFromPlayer`. After `ConsumeTravel`, respawn those member ids and `AdoptBoard`. Anyone not on the roster stays home-leashed.

## Tests (written, not run)

`GFGame.Roaming.HomeZone`, `GFGame.Roaming.RevengeGang`, `GFGame.Roaming.Handoff`, `GFGame.Roaming.SessionCap`, `GFGame.Roaming.Bind`.
