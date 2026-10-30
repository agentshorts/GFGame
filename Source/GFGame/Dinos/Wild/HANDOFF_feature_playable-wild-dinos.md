# HANDOFF feature/playable-wild-dinos

Wild dinos spawn around the faction camps and walk a short loop while they are wild, idle, and not chasing anyone. Press Play on `L_Hub` and look around Rime: Nippers and Raptors should be on a ring 6–15 m out. Verdant and Cinderwrights get their own rings. Decision: https://github.com/agentshorts/GFGame/issues/90

Work issue: https://github.com/agentshorts/GFGame/issues/80

## What changed

- `AGFWildDinoDirector` (`Source/GFGame/Dinos/Wild/`). Authority only. On begin play it fills one slot per spawn-table entry, up to `MaxAlive`. A destroyed dino's slot waits `RespawnSeconds`, then spawns again. Clients do not spawn a second set (`bReplicates` is false; the game mode only starts it on the server).
- Ring points are uniform by area between `MinRadius` and `Radius`. The centre is that id in `UGFTerritorySubsystem::FactionLayout` (the live subsystem, or the class default if the subsystem has no rows). An unknown id uses the entry's `Location`.
- Ground Z is a downward `ECC_WorldStatic` trace from the anchor Z + 2000 cm, 50000 cm long. A hit uses the impact Z plus the dino capsule half height so the feet sit on the ground. A miss uses anchor Z + 100.
- `UGFWanderComponent` (`Source/GFCompanion`). `AGFDino` creates it and calls `TickWander` from the `default:` branch of `Tick` only. `SetAttackTarget` and the top of `Tick` are unchanged.
- Wander runs when the tame state is `Wild`, the dino state is `Idle`, there is no attack target, and the dino is not `Rest`. It picks a point in an 800 cm disc around home every 4–9 seconds and walks with `AddMovementInput` until it is within 100 cm (horizontal).
- Species with no `DT_DinoDiet` row already come back as a null `GetDietRow()`. `TickServer` returns before the tame machine, and `ServerOffer` returns `NotTameable`. No taming edit. They still spawn and wander. Proximity aggro needs a diet row, so Tusksaw, Lanternmaw, and Scythepaw do not aggro by distance. Noise can still turn them hostile.
- `bSpawnHostile` false leaves the tame state `Wild`. True calls `ServerForceHostile` with an empty key (hostile, but no player to chase until taming or noise sets a real target).

`AGFDino.cpp` edits are the constructor subobject and the `default:` call. `GFGameMode.cpp`, `ChoosePlayerStart`, `GFAssetMap`, `GFCharacter`, `GFGiant`, the Ferry folder, and HUD files were not edited. `StartupDirectorClasses` is one appended ini line, so the social director and the paint volume stay registered.

## Config keys

Appended at the bottom of `Config/DefaultGame.ini`.

`[/Script/GFGame.GFGameMode]`

- `+StartupDirectorClasses=/Script/GFGame.GFWildDinoDirector`

`[/Script/GFGame.GFWildDinoDirector]`

- `MaxAlive=24`
- `RespawnSeconds=60`
- `bSpawnHostile=False`
- `Spawns`: Rime 3 Nipper and 2 Raptor; Verdant 2 Tusksaw and 2 Lanternmaw; Cinderwrights 2 Scythepaw and 1 Hornback. Every row is `MinRadius` 600 and `Radius` 1500.

`[/Script/GFCompanion.GFWanderComponent]`

- `DefaultWalkSpeed=250`
- `WanderRadius=800`
- `WanderEveryMin=4`
- `WanderEveryMax=9`
- `StopDistance=100`
- `WalkSpeedBySpecies`: Nipper 320, Raptor 480, Tusksaw 180, Lanternmaw 200, Scythepaw 400, Hornback 160 (cm/s). A missing species uses 250.

The speed map is one assignment line. `+Key` lines do not parse as a `TMap`.

## Tests

Written, not run here (no Unreal build on this machine).

- `GFGame.WildDino.Ring` — picker stays in `[MinRadius, Radius]`, including swapped and equal radii.
- `GFGame.WildDino.SpawnTable` — the six rows, radii, cap, respawn, passive flag, and walk speeds parse from config.
- `GFGame.WildDino.NearRime` — a game world spawns the director; at least one `AGFDino` is on the Rime ring; each spawn is on its anchor ring; Tusksaw has no diet row, stays Wild, and still picks a wander point. A second director with an unknown anchor uses `Location`.
- `GFGame.WildDino.Wander` — disc points and the component's wander target stay inside `WanderRadius`. An attack target blocks a new pick.
- `GFGame.WildDino.MissingDiet` — Raptor has a diet row; Tusksaw, Lanternmaw, and Scythepaw do not. Ticking them does not leave `Wild`, and Tusksaw still wanders.

Flags: `EditorContext | EngineFilter`.

## Dev PC steps

None required. Optionally place a `NavMeshBoundsVolume` in `L_Hub` later if movement moves to AI MoveTo. This uses plain movement input, so it does not need nav.

## Follow-ups

- Dinos keep the yaw they spawned with. They do not turn to face the wander point.
- Wander input is applied on `AGFDino`'s 0.1 s tick, the same cadence as follow and attack.
- Issue comments on #80 return 403 for this token. The decision is #90. The start note is #93. Later progress notes mention #80.
