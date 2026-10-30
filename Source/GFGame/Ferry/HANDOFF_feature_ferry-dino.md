# HANDOFF feature/ferry-dino

Branch base is staging `46322d15f7b84fa51d91801430eef0e82e3f5ee7`. No Unreal build was run here. Compile and the automation tests on the dev PC.

`Source/GFGame/GFGame.Build.cs` adds `Ferry` to `PrivateIncludePaths`, next to `Territory` and `AI/Roaming`. Headers in this folder are included by name (`GFFerryRules.h`), so that path is required. `GFCharacter.h` only forward-declares `UGFFerryRiderComponent`, because `GFTests` includes the character header and must not pull a private Ferry include.

## What changed

New code is `Source/GFGame/Ferry/` plus `Source/GFGame/Ferry/Tests/GFFerryTests.cpp`. Pure fare, pass, flight, drop, and land-route spawn rules are plain structs. They do not name factions or species. Those strings come from `Config/DefaultGame.ini`.

`AGFFerryDino` is an `AActor`, not an `AGFDino`. It has no taming component, no leader, and no owner. `CanBeDamaged` is false, `TakeDamage` returns 0 and changes nothing, and the actor carries `GF.FactionProtected`. The parked body uses asset key `Dino.Ripwing` and anim `Ripwing.Perch_Idle`. A trip spawns a transient copy that switches to `Ripwing.Glide_Loop` and is destroyed on landing. The parked ferry stays at the stop.

`AGFFerryDirector` (`Config=Game`) reads `Stops`, `Routes`, `Tuning`, and `MobSpawn`. On authority it spawns one parked ferry per stop, flies the transient copy, and runs the lava-bridge spawner. It is registered with `+StartupDirectorClasses`. A bare key would replace the constructor list (social director and paint volume). The `+` line appends, so the class is not also added in `AGFGameMode`'s constructor. `InitGameState` still skips a class that is already in the world.

`UGFFerryRiderComponent` on `AGFCharacter` exposes `RequestTrip` and `BuyPass`. The server calls `AGFGameState::ServerBurn` / `ServerMint` with the same plans the pure rules use. While the rider is in flight the pawn gets `GF.FactionProtected` and `SetCanBeDamaged(false)`.

`FGFProtectedActor` in GFCore is the only new shared helper. `AGFDino::SetAttackTarget` and the attack tick clear a target that has the tag. That is the only `GFCompanion` change. `GFGiant`, `GFSocial`, `IsSafeFor` / `IsSafeForDino` / `SubjectFor`, `GFSafeZone.h`, Unbinding, and `AI/Roaming` are untouched. No `.uasset` or `.umap` edits.

`AGFGameMode::ChoosePlayerStart` uses `DefaultSpawnAnchor`: a `PlayerStart` tagged with that anchor wins, otherwise `Super` (so a mini-game map keeps its own starts), otherwise the anchor center plus 120 cm Z. An unknown anchor is `(0, 0, 120)`.

`Data/CSV/DT_Items.csv` appends `FerryTame` and `FerryPass`.

## Config keys

`[/Script/GFGame.GFGameMode]`

- `DefaultSpawnAnchor="Rime"`
- `+StartupDirectorClasses=/Script/GFGame.GFFerryDirector`

`[/Script/GFGame.GFFerryDirector]`

- `Stops`: `RimeLanding` at `(1200, 600, 0)`, anchor `Rime`; `CinderLanding` at `(6000, 600, 0)`, anchor `Cinderwrights`
- `Routes`: `RimeCinder`, `StopA=RimeLanding` (home), `StopB=CinderLanding`, `LandRouteHazardId=LavaBridge`
- `Tuning`: `FareItem=Dirt`, `FareCount=3`, `PassMaterialItem=FerryTame`, `PassPrice=5`, `PassItem=FerryPass`, `FlightSeconds=12`, `CruiseAltitude=1500`, `BoardRadius=400`
- `MobSpawn`: species `Raptor` and `Hornback`, `MobLevel=10`, `MobDamagePerLevel=0.15`, `SpawnIntervalSeconds=8`, `MaxAlive=6`, `ActivationMargin=1000`, `IdleDespawnSeconds=60`

Each of those lines is commented in `DefaultGame.ini`.

## Flight model

`FlightPosition` lerps from the departure stop to the arrival stop and adds `sin(pi * alpha) * CruiseAltitude` on Z. Alpha 0 and 1 return the endpoints exactly, because `sin(pi)` is not a clean 0. Alpha 0.5 adds the full cruise altitude to the chord.

Boarding uses horizontal distance (`Dist2D`) against `BoardRadius`. The drop is `BoardRadius / 2` past the arrival stop along the route. Drop Z is `To.Z + (riderZ - From.Z)`, so the rider keeps the height they had above the departure stop. The body is moved to the flight point; the rider is placed at that point plus the same lift, then detached onto the drop. Movement is disabled for the flight and set back to walking.

A pawn that had a controller and then loses it is moved to the drop immediately and the flying copy is destroyed. A destroyed rider destroys the copy only. Parked ferries face the other stop. The flying copy faces along the route.

## Level formula

There is no dino level stat. The spawner writes:

`AttackDamage_new = AttackDamage_base * (1 + MobDamagePerLevel * (MobLevel - 1))`

`AttackDamage_base` is `AGFDino`'s `AttackDamage` before this scale (default 10). At level 10 and 0.15 that is `10 * 2.35 = 23.5`.

Spawn points are up to 64 uniform samples inside the hazard box. A point is kept only when `GetHazardAt` returns `LandRouteHazardId` (wild ground; owned land and any other hazard fail). Mob Z is the box center Z plus 100. One `AGFDino` spawns per elapsed interval, up to `MaxAlive`, and `ServerForceHostile` aims it at the nearest non-flying player within the margin. The first spawn waits one full interval. Going inactive resets that wait. Flying players do not activate the route and do not count as near. A mob despawns when it is outside the box by more than the margin, or when its age reaches `IdleDespawnSeconds` and nobody is near.

## Defaults chosen

These are already issues. The numbers below are the ones in `DefaultGame.ini`.

- #25 One route, `RimeCinder`, between Rime and Cinderwrights. `StopA` is the Rime stop. Verdant has no ferry.
- #26 `RimeLanding` `(1200, 600)` and `CinderLanding` `(6000, 600)`. Each is about 1342 cm from its anchor, inside the 1-member disc (Rime r >= 1600, Cinderwrights r >= 1400). One ferry stays parked at each stop. A trip uses a separate flying copy.
- #27 Fare is 3 Dirt, burned once on boarding through `ServerBurn` with reason `Ferry.Fare`. A player who cannot pay is refused and nothing is spent.
- #28 A pass costs 5 `FerryTame`, burned, then 1 `FerryPass` is minted. Holding `FerryPass` makes both directions free. A second buy returns `AlreadyHasPass`.
- #29 The trip is a 12 s flight with a 1500 cm arc, not a teleport. `BoardRadius` is 400 cm. The rider cannot move, take damage, or be targeted in flight. A disconnect places a surviving pawn at the drop.
- #30 The ferry is not an `AGFDino`, cannot be tamed or owned, ignores damage, and carries `GF.FactionProtected`. `AGFDino` will not target that tag. Riders in flight get the same tag.
- #31 Land-route mobs are level 10 Raptor and Hornback, one every 8 s, up to 6, only where `LavaBridge` is still wild, and only while a non-flying player is within 1000 cm. They despawn past that margin, or after 60 s with nobody near. Damage scale is 2.35x.
- #32 `DefaultSpawnAnchor` is `Rime`. Tagged `PlayerStart` wins, then any map start, then the anchor center + 120 cm Z. Unknown anchor is `(0, 0, 120)`.

New, needs an issue:

- `FerryTame` display name "Ferry Tame", stack max 999 (Big Grumble's tame row is 99). `FerryPass` display name "Ferry Pass", stack max 1. Both are Material, tier 4, value 20, category Rare, not wagerable, not rentable, so `ValidateItemTable` accepts them.
- Pass ledger reasons are `Ferry.Pass` (burn) and `Ferry.PassGrant` (mint). A fare `TxId` is a new guid per request. Pass burn and mint ids are that request guid with `FGuid::D` XOR `0x50535331` and `0x50535332`.
- An account the ledger cannot read returns `NotAuthority`. A stop that is not on a route returns `UnknownStop`. Authority is checked before the already-paid and already-owns-a-pass short circuits. A replayed fare `TxId` returns `Ok` and does not charge, and it does not re-check boarding.
- Boarding is horizontal. The drop is `BoardRadius / 2` past the arrival stop along the route.
- Body key `Dino.Ripwing`. Parked anim `Ripwing.Perch_Idle`. Transit anim `Ripwing.Glide_Loop`. Body collision is off. Parked ferries face the other stop.
- The spawner tries 64 samples, lifts the mob 100 cm, waits one full interval before the first spawn, and resets that wait when the route goes inactive.

## Follow-ups

- `FerryTame` has no source. Do not invent a drop or a shop for it here.
- Big Grumble (`GFGiant`) does not read `GF.FactionProtected`. That skip belongs on the giant targeting branch.
- There is no overworld player health. Mob damage is written onto `AttackDamage` and `ApplyDamage` still runs, but a hit does not change a player until health exists.
- Stop meshes, the perch and glide anims, and the boarding UI are placed in the editor. This branch only stores the asset keys.
- `DT_Items` needs a CSV reimport so `FerryTame` and `FerryPass` exist as rows in the data table.
- A pass is an inventory item, so mail could still move it. Soulbinding is later, if it matters.

## Tests

Automation names, `EditorContext | EngineFilter`. Fare, pass, layout, spawn, and the spawner rules run without a world. `GFGame.Ferry.NoAttack` creates a game world.

- `GFGame.Ferry.Fare`
- `GFGame.Ferry.Pass`
- `GFGame.Ferry.NoAttack`
- `GFGame.Ferry.SpawnerRouteOnly`
- `GFGame.Ferry.Layout`
- `GFGame.Spawn.DefaultAnchor`
