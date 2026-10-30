# HANDOFF feature/revenge-gang-roaming

Roaming lives in `Source/GFGame/AI/Roaming/`. Nothing outside this folder was edited.

## Already in the game (reused, not reimplemented)

- `AGFDino` / `UGFTamingComponent`: species, companion id, `GetTargetKey()`, follow and attack movement (`MoveToward`).
- `AGFPlayerState`: `PlayerKey`, `bLoggedOut`. Faction ids stay on the player state. Roaming does not name factions.
- `AGameSession::MaxPlayers` (world session, else the class default from `DefaultGame.ini`). `<= 0` means the gang roster has no cap. This is not `AGFGameState::MaxPerFaction`.
- Home leash geometry is `FGFSafeCircles` / `FGFSafeRegion` (`IsInsideSafe`, `ClosestSafePoint`). Home dinos stay inside. The giant's stay-out rule is a different use of the same circles.
- Map packages: `UGFTamingGateSubsystem` (`/Game/Maps/L_Valley`), `UGFMobaGateSubsystem` (`/Game/Maps/L_Moba`), `UGFBeatHopGateSubsystem` (`/Game/Maps/L_Beat`), `UGFFashionGateSubsystem` (`/Game/Maps/L_Runway`). Read them with `GetDefault`, not `NewObject` into the transient package.
- `UGFMiniGameGateSubsystem::EnterMiniGame` already broadcasts `OnBeforeEnter` and then `OpenLevel`. That is the travel call.

Not the same feature, left alone: taming `PackId`, `DT_ModeRules.bEmoteAttractsDanger` (emote noise), jail leash.

## Required outside this folder

### 1. Include path (`Source/GFGame/GFGame.Build.cs`)

Done: added when this merged to staging.

Headers sit in `AI/Roaming/`, not `Public/` or `Private/`. Quoted includes from those cpp files are enough for them. UHT's generated cpp includes `"GFRoamingPolicyComponent.h"` and `"GFRoamingSubsystem.h"` by filename, so the module needs that directory on its include path:

```csharp
PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "AI/Roaming"));
```

Do not point `GFCompanion` at `GFGame` (cycle). `GFGame` already depends on `GFCompanion`, `GFNet`, `GFMoba`, and `GFRhythm`.

### 2. Hand the record to travel

`UGFRoamingSubsystem` is a game-instance subsystem, so it survives `OpenLevel`. `BeginTravel` copies the gang only when `TravellerKey` equals the board's target (`UGFTamingComponent::GetTargetKey()`). Anyone else changing maps leaves the board in place.

- Before `OpenLevel` (the `OnBeforeEnter` lambda is the right place, in `UGFMiniGameGateSubsystem::EnterMiniGame` or a subscriber in `GFGame`): `UGFRoamingSubsystem::Get(World)->BeginTravel(Gate->GetMapName(), TravellerKey)`. `EnterMiniGame` only broadcasts when the map package exists. Empty mode maps are listed in `Tools/create_maps.py`. `BeginTravel` / `ConsumeTravel` are what the tests call.
- On the destination map, once (`AGFGameMode::InitGame` in `Source/GFGame/Private/GFGameMode.cpp`): `UGFRoamingSubsystem::Get(this)->ConsumeTravel()`. A second call is a no-op. The spent record leaves the target key on the new board.

`GFCore` cannot include `GFGame`. Keep the two calls in `GFGame`.

Following a client needs server travel. `BeginTravel` and `ConsumeTravel` run on the game instance that owns the gang. A client-only `OpenLevel` does not move the server's instance, so the gang stays on the server's map. For the gang to follow its target into MOBA or Beat Hop, the server has to travel with that player.

### 3. Ask the leash before `AGFDino` moves

`AGFDino::Tick` (`Source/GFCompanion/Private/GFDino.cpp`) calls `MoveToward` with no zone check. `GFCompanion` cannot include this component. Add a small interface on `GFCore` (both modules already depend on it), implemented by `UGFRoamingPolicyComponent`:

- `CanPathTo(Goal)` / `ClampFollowGoal(Goal)` are already on the component. They use `FGFSafeCircles`.
- In the Follow and Attack branches, if a component implements the interface and `CanPathTo` is false, move to `ClampFollowGoal` instead. If that is still false, do not step.
- Spawn path: `UGFRoamingPolicyComponent::AttachToDino(Dino, Home, HomeMap, MemberId)` from `GFGame` (game mode or a director). Do not construct the component inside `AGFDino`. Pass a unique `MemberId` (or a valid companion id). Species is not an id.

Revenge-gang dinos: `JoinGang(Subsystem->EnsureBoard(), GFRoamingLimits::MaxPlayers(GameState), OutCode)` then `SetGangTargetFromTaming(Dino->Taming)` after the taming component has a target key. After `ConsumeTravel`, respawn those member ids and `AdoptBoard`. Anyone not on the roster stays home-leashed.

## Tests (written, not run)

`GFGame.Roaming.HomeZone`, `GFGame.Roaming.RevengeGang`, `GFGame.Roaming.Handoff`, `GFGame.Roaming.SessionCap`, `GFGame.Roaming.Bind`.
