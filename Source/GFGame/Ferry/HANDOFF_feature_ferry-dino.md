# HANDOFF feature/ferry-dino

Branch base is staging `45fc8d09c8f6311bd33c973e92259ab0d19f63e2` (voxel overworld). No Unreal build was run here. Compile and the automation tests on the dev PC.

`Source/GFGame/GFGame.Build.cs` keeps `Ferry` on `PrivateIncludePaths`, next to `Territory`, `AI/Roaming`, `Dinos/Wild`, `World`, and `UI`. Headers in the Ferry folder are included by name (`GFFerryRules.h`), so that path is required. `GFCharacter.h` only forward-declares `UGFFerryRiderComponent`. The component header itself lives in `Source/GFGame/Public/` so `GFTests` can call `BeginFlightProtection`. Ferry rules headers stay private.

## What changed

New code is `Source/GFGame/Ferry/` plus `Source/GFGame/Ferry/Tests/GFFerryTests.cpp`. Pure fare, pass, flight, drop, and land-route spawn rules are plain structs. They do not name factions or species. Those strings come from `Config/DefaultGame.ini`.

`AGFFerryDino` is an `AActor`, not an `AGFDino`. It has no taming component, no leader, and no owner. `CanBeDamaged` is false, `TakeDamage` returns 0 and changes nothing, and the actor carries `GF.FactionProtected`. The parked body uses asset key `Dino.Ripwing` and anim `Ripwing.Perch_Idle`. It stands on the perch point, yaw -90. A trip spawns a transient copy at that perch. The copy plays `Ripwing.Ferry_Takeoff`, then `Ripwing.Ferry_Carry_Fly`, then `Ripwing.Ferry_Land`. It does not use `Ripwing.Glide_Loop`. The parked ferry stays at the perch. `DT_AssetMap` now has skeletal columns on `Dino.Ripwing` (mesh, `Ripwing.Idle` / `Ripwing.Run`, capsule offset Z=-96). The ferry does not call `ApplySkeletal`. Those columns are for a tameable `AGFDino`. The ferry loads `SK_Dino_Ripwing` on its own mesh, at the actor origin, which is the perch point.

`AGFFerryDirector` (`Config=Game`) reads `Stops`, `Routes`, `Tuning`, and `MobSpawn`. On authority it spawns one parked ferry per stop, flies the transient copy, and runs the lava-bridge spawner. It is registered with `+StartupDirectorClasses`. An ini entry replaces the constructor list, so every director is listed once in `DefaultGame.ini` and the ferry is not also added in `AGFGameMode`'s constructor. The constructor still names the social director and the paint volume; the ini restates them, and that is the same registration, not a second actor. `InitGameState` still skips a class that is already in the world.

`UGFFerryRiderComponent` on `AGFCharacter` exposes `RequestTrip` and `BuyPass`. Each call makes one `FGuid` and sends it with the RPC. While the rider is in flight the pawn gets `GF.FactionProtected` and `SetCanBeDamaged(false)`, and sits on the basket seat playing `Player.Emote_Sit`. `UGFSimpleAnimComponent` would replace that looping clip with idle or run, so the seat holds locomotion off with `PauseFor` until `EndSeat`. `SeatAnim` replicates, and `BeginPlay` plays it again if the mesh arrives late.

`FGFProtectedActor` in GFCore is the shared tag helper. `AGFDino::SetAttackTarget` clears a target that has the tag. `UGFGiantEventComponent::GatherPlayers` and `ApplyShove` skip the same tag, so a rider in flight is not shoved. Ferry file-local helpers live in `GFFerryRulesPrivate`, `GFFerryDirectorPrivate`, `GFFerryDinoPrivate`, and `GFFerryRiderPrivate` so a unity build does not merge them with `GFUnbinding.cpp`. The wild-director lava check uses `GFWildDinoPrivate` for the same reason.

`AGFGameMode::ChoosePlayerStart` uses `DefaultSpawnAnchor`, kept equal to `StarterFaction` and set to `Rime`. A `PlayerStart` tagged with that anchor wins, otherwise `Super` (so a mini-game map keeps its own starts). With no `PlayerStart` the player stands on `FGFPlayerStartRules::FallbackLocation`, the same point as `FGFSpawnDefaults::StarterFallback`: the anchor centre plus 120 cm. Shipped Rime is the origin, so that point is `(0, 0, 120)`. An unknown anchor is `(0, 0, 120)`. The ferry perch `EmptyMapStart` at `(0, -300, 100)`, yaw 36, stays separate and is not this spawn.

`Data/CSV/DT_Items.csv` keeps `FerryPass`. `FerryTame` is retired; it is no longer the pass currency.

## Config keys

`[/Script/GFGame.GFGameMode]`

- `DefaultSpawnAnchor="Rime"`
- `+StartupDirectorClasses` lists social, paint, giant, environment, overworld ground, wild dinos, then `/Script/GFGame.GFFerryDirector` once

`[/Script/GFGame.GFFerryDirector]`

- `Stops`: `RimeLanding` at `(1200, 600, 0)`, anchor `Rime`; `CinderLanding` at `(6000, 600, 0)`, anchor `Cinderwrights`
- `Routes`: `RimeCinder`, `StopA=RimeLanding` (home), `StopB=CinderLanding`, `LandRouteHazardId=LavaBridge`
- `Tuning`: `FareItems=Dirt,WoodPlank,Glass`, `FareCount=3`, `PassMaterialItem=Fizzberry`, `PassPrice=12`, `PassItem=FerryPass`, `FlightSeconds=13.4`, `CruiseAltitude=1500`, `BoardRadius=400`
- `MobSpawn`: `LevelBonus=5`, `MobDamagePerLevel=0.15`, `SpawnIntervalSeconds=6`, `MaxAlivePerBox=4`, `ActivationMargin=4000`, `LeashMargin=800`, `DespawnRange=6000`, `IdleDespawnSeconds=60`
- `MobBoxes`: `LavaWest` Raptor at x 2400, `LavaMid` Scythepaw at x 3700, `LavaEast` Lanternmaw at x 5000. Each half-extent is 500 by 300. `HazardId=LavaBridge`

Each of those lines is commented in `DefaultGame.ini`.

## Flight model

`FlightPosition` is a quadratic Bezier. Alpha 0 and 1 are the perch points, not the pad origins. The control point is the chord midpoint plus 1600 cm toward world +Y, with Z = midpoint Z + 2 * CruiseAltitude. Alpha 0.5 is CruiseAltitude above the chord and 800 cm off it. The return trip bulges the same way, toward +Y. The bird stays on the perch, yaw -90, until takeoff frame 10 (10/30 s). After that it faces the Bezier tangent.

The basket stays at the departure basket spot until takeoff frame 22, then its Grip point `(0, 0, 193.75)` is snapped to `FerryGrip` on `talon_l` (SnapToTarget location, KeepWorld rotation). On landing, frame 16 lowers the basket and frame 22 releases it at the arrival basket spot. The rider is snapped to the basket in basket space at the Seat offset `(-25, 0, 12.5)` for the whole flight. Attaching to the Seat socket would leave the relative location at zero.

Boarding uses horizontal distance (`Dist2D`) against `BoardRadius`. The drop is `BoardRadius / 2` past the arrival stop along the route. Drop Z is `To.Z + (riderZ - From.Z)`, using the rider's location before they sit. Movement is disabled for the flight and set back to walking. The rider is detached before the craft is destroyed.

A pawn that had a controller and then loses it is moved to the drop immediately and the flying copy is destroyed. A destroyed rider destroys the copy only. A spawned mob whose taming component is missing is destroyed instead of leaked. A spawn point must be wild and must still report `LandRouteHazardId` before the mob is created.

## Level formula

There is no dino level stat. The spawner writes:

`AttackDamage_new = AttackDamage_base * (1 + MobDamagePerLevel * (MobLevel - 1))`

`AttackDamage_base` is `AGFDino`'s `AttackDamage` before this scale (default 10). An empty party counts as level 1, plus `LevelBonus` 5, so level 6. At 0.15 that is `10 * 1.75 = 17.5`.

Spawn points are up to 64 uniform samples inside each mob box. A point is kept only when `GetHazardAt` returns that box's `HazardId` (wild `LavaBridge`; owned land and any other hazard fail). Mob Z is the box center Z plus 100. Each box spawns one `AGFDino` per elapsed interval, up to `MaxAlivePerBox`, and `ServerForceHostile` aims it at the nearest non-flying player within 40 m. The first spawn waits one full interval. Going inactive resets that wait. Flying players do not wake a box and do not count as near. A mob despawns when it is more than 8 m outside its box, or after 60 s with nobody within 60 m. The forest route has no mob box.

## Defaults chosen

These are already issues. The numbers below are the ones in `DefaultGame.ini`.

- #25 One route, `RimeCinder`, between Rime and Cinderwrights. `StopA` is the Rime stop. Verdant has no ferry.
- #26 `RimeLanding` `(1200, 600)` and `CinderLanding` `(6000, 600)`. Each is about 1342 cm from its anchor, inside the 1-member disc (Rime r >= 1600, Cinderwrights r >= 1400). One ferry stays parked at each stop. A trip uses a separate flying copy.
- #27 Fare is 3 of any basic block (tier-1 building blocks: Dirt, WoodPlank, Glass), any mix, burned once on boarding through `ServerBurn` with reason `Ferry.Fare`. The fare TxId is the client's request id, so a duplicate RPC does not charge and does not start a second flight. If `StartFlight` fails, the fare is minted back (`Ferry.FareRefund`) and the result is `FlightFailed`. A player who cannot pay is refused and nothing is spent. A flight that has started is not refunded.
- #28 A pass costs 12 `Fizzberry`, burned, then 1 `FerryPass` is minted. If the mint fails, the burn is minted back (`Ferry.PassRollback`) and that request id cannot mint a free pass later. Holding `FerryPass` makes both directions free. A second buy returns `AlreadyHasPass`. `FerryPass` is account-bound: gifts, mail, wagers, and rent cannot move it. `FerryTame` is not the currency and the row is gone.
- #29 The trip is a 13.4 s flight with a 1500 cm arc, not a teleport. Takeoff is 40 frames and landing is 48 frames at 30 fps; the carry fills the rest. `BoardRadius` is 400 cm. The rider cannot move, take damage, or be targeted in flight. A disconnect places a surviving pawn at the drop.
- #30 The ferry is not an `AGFDino`, cannot be tamed or owned, ignores damage, and carries `GF.FactionProtected`. `AGFDino` will not target that tag. Riders in flight get the same tag. Big Grumble skips it when gathering players and when applying a shove.
- #31 Lava-bridge land-route mobs are Raptor, Scythepaw, and Lanternmaw, one species per box, at the nearby party's level plus 5. One every 6 s, up to 4 alive per box, only where that box is still wild `LavaBridge`, and only while a non-flying player is within 40 m. They despawn 8 m past the box, or after 60 s with nobody within 60 m. An empty party is level 6. Damage scale at that level is 1.75x.
- #32 `DefaultSpawnAnchor` is `Rime`, the same id as `StarterFaction`. Tagged `PlayerStart` wins, then any map start, then `StarterFallback` (anchor centre + 120 cm). Shipped Rime is `(0, 0, 120)`. The ferry perch `(0, -300, 100)`, yaw 36, stays separate.

## Rebase onto staging 45fc8d0

The voxel overworld stays. Ferry changes kept on top of it:

- `Config/DefaultGame.ini`. `DefaultSpawnAnchor="Rime"`. Every `+StartupDirectorClasses` line from staging, then `GFFerryDirector` once. An ini entry replaces the constructor list.
- `GFGame.Build.cs`. Private include paths for Ferry, wild dinos, world, and UI.
- `GFDino.cpp`. Protected-target clear, and the skeletal mesh plus `SimpleAnim`.
- `GFCharacter.h` and `GFCharacter.cpp`. `FerryRider` and `SimpleAnim`, plus the player mesh offset.
- `GFGameMode.cpp`. `AGFHUD`, the voxel plot, and the ferry start rule. A tagged start wins, then any map start, then `StarterFallback`.
- `GFGiantEventComponent.h`. Forward-declare `APawn` and `FGFGiantTaunt`.
- `GFWildDinoDirector.cpp`. Camp rings skip wild `LavaBridge`. `FMath::Max3` uses `0.0` so the double box centre is not mixed with `0.f`.

Checked:

- Overworld slab tops sit just under Z=0 (`SurfaceZ` -2, overlays clamped under 0). Ferry stops stay at Z=0. Boarding is horizontal. The perch point is 262.5 cm above the stop. The no-PlayerStart spawn for shipped Rime is `(0, 0, 120)`, above the voxel ground. The ferry perch is not that spawn.
- `AGFWildDinoDirector` camp rings (600–1500 cm) overlap the `LavaBridge` box, and a wander of 800 cm can walk onto it. A ring spawn on wild `LavaBridge`, or whose wander disc would enter that box, is retried (32 tries). A zero-radius fallback is refused only when the point itself is wild lava. The ferry spawner is the only one that fills that hazard. `DeadlyForest` has no second spawner; a thin Verdant overlap was left alone.
- `DT_AnimMap.csv` merged clean. The ferry clip rows kept the `PlantedPropOffsetZ` column. No duplicate keys.
- Ferry and staging automation tests are still the same cases. `GFFerryTests.cpp` includes `GFInventoryComponent.h` (the Ender unity build failed without it).

New, recorded in [#149](https://github.com/agentshorts/GFGame/issues/149):

- `FerryPass` display name "Ferry Pass", stack max 1. It is Material, tier 4, value 20, category Rare, not wagerable, not rentable, and `bAccountBound`, so `ValidateItemTable` accepts it. `FerryTame` is not in the table.
- Pass ledger reasons are `Ferry.Pass` (burn), `Ferry.PassGrant` (mint), and `Ferry.PassRollback`. Fare refund reason is `Ferry.FareRefund`. The fare TxId is the request guid. Pass burn and mint ids are that guid with `FGuid::D` XOR `0x50535331` and `0x50535332`. Fare refund uses `0x46524652`. Pass rollback uses `0x50535242`. An invalid request id fails with `NotAuthority`.
- An account the ledger cannot read returns `NotAuthority`. A stop that is not on a route returns `UnknownStop`. Authority is checked before the already-paid and already-owns-a-pass short circuits. A replayed fare `TxId` returns `Ok` and does not charge, and it does not re-check boarding.
- Boarding is horizontal. The drop is `BoardRadius / 2` past the arrival stop along the route.
- Body key `Dino.Ripwing`. Parked anim `Ripwing.Perch_Idle`, yaw -90, at the perch point. Transit anims are `Ripwing.Ferry_Takeoff`, `Ripwing.Ferry_Carry_Fly`, and `Ripwing.Ferry_Land`. Body collision is off. The skeletal mesh and ferry material load when those assets exist. Missing perch, basket, and clip assets are left empty.
- Each box tries 64 samples, lifts the mob 100 cm, waits one full interval before the first spawn, and resets that wait when the box goes inactive.

## Follow-ups

- `FerryTame` is retired in the same change as the 12 Fizzberry pass. Do not add it back as a currency.
- There is no overworld player health. Mob damage is written onto `AttackDamage` and `ApplyDamage` still runs, but a hit does not change a player until health exists.
- The perch and basket meshes, the ferry clips, and the boarding UI are placed in the editor. This branch stores the asset paths and plays a clip when it loads.
- `DT_Items` needs a CSV reimport so `bAccountBound` reaches the cooked data table. The CSV fallback already reads that column.

## Tests

Automation names, `EditorContext | EngineFilter`. Fare, pass, layout, spawn, and the spawner rules run without a world. `GFGame.Ferry.NoAttack` creates a game world.

- `GFGame.Ferry.Fare` (Bezier peak is off the chord, same side both ways)
- `GFGame.Ferry.Pass`
- `GFGame.Ferry.NoAttack` (perch, seat, takeoff, carry)
- `GFGame.Ferry.DuplicateCharge`
- `GFGame.Ferry.FareRefund`
- `GFGame.Ferry.PassRollback`
- `GFGame.Ferry.PassBound`
- `GFGame.Ferry.Receipt` (12 Fizzberry, basic-block fare, phase timing, ledger salts, FerryPass row, FerryTame gone)
- `GFGame.Ferry.SpawnerRouteOnly`
- `GFGame.Ferry.Layout`
- `GFGame.Spawn.DefaultAnchor`
- `GFGame.Giant.ProtectedSkip`
