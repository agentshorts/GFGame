# HANDOFF feature/quick-travel-ferry

Branch base is staging. No Unreal build was run. Compile and run `GFGame.Ferry.QuickTravel.*` plus the existing `GFGame.Ferry.*` and `GFGame.Spawn.DefaultAnchor` tests on a dev PC.

## What changed

The ferry director already flew a protected Ripwing between Rime and Cinderwrights, charged 3 basic blocks, and sold a pass for 12 Fizzberry. There was no stand to walk up to, and Verdant had no stop.

`AGFFerryDirector` now spawns one `AGFFerryStand` per stop. The stand is engine cubes and text. Walk onto a destination pad and the server calls `RequestTripTo` (a new guid each step-on). Walk onto the pass pad and it calls `BuyPass`. The flight, the drop, the fare ledger, and the faction-protection tag are unchanged. A second step-on while you are still in that pad does not ask again.

`RequestTrip` (no destination) still uses the first route that touches the stop. That route is still `RimeCinder`, so a destination-less trip from Rime still lands at Cinderwrights. `RequestTripTo` is what the pads use. Both stops existing with no route returns `NoRoute`.

VerdantLanding is `(-4400, 400, 0)`, 400 cm toward +Y from the Verdant anchor. That is inside the 1-member disc (1400) and inside the hole of the wild-spawn ring (the ring starts at 600 cm). `RimeVerdant` uses `DeadlyForest`. `VerdantCinder` has no hazard id, so that flight does not spawn land-route mobs.

Pads sit on Y (pass at +220, destinations stepping down from -220). Drops land about 200 cm along the route, which is mostly X, so you do not land on a return pad. Each pad is inside the 400 cm board disc.

The parked bird is still an `AGFFerryDino`, not an `AGFDino`. It has no leader. `FGFFerryFollow::WantsToFollow` is false, and the parked actor is not moved when a player walks off. `TakeDamage` still returns 0 and the body still carries `GF.FactionProtected`. Riders in flight still get that tag. Rime is still `DefaultSpawnAnchor` and `StarterFaction`. The empty-map spawn is the anchor centre plus 120 cm, not a ferry stand.

The HUD draws the stand lines (fare, destinations, pass price, last result) while the local pawn is within 500 cm of a stand.

## Tests

`EditorContext | EngineFilter`.

- `GFGame.Ferry.QuickTravel.Links` — one stand per safe zone, inside the 1-member disc, a route each way, pads inside the board disc and clear of drops, fare 3 basic blocks including Dirt, pass 12 Fizzberry, Rime spawn is not a stand, the ferry does not follow.
- `GFGame.Ferry.QuickTravel.Trip` — a pad refuses a trip with no blocks, then burns 3 Dirt, flies, and drops at Verdant; the pass pad burns Fizzberry; the return is free; a destination-less trip from Rime still reaches Cinderwrights; parked birds do not move and still ignore damage.
