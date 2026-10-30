# HANDOFF: feature/grumble-land-zone

Branch off `staging` at 8345bec (giant protection). Big Grumble's movement zone now covers all owned faction land, not just his default circle. Local commits only; nothing pushed.

## What changed

- **Union zone** (GFCore, game-agnostic): `FGFSafeZoneUnion` joins an optional own zone with every safe-zone provider registered in `UGFSafeZoneSubsystem` except one skipped provider (new `UGFSafeZoneSubsystem::GetProviderZones(Skip)`, newest first). The provider list is read again on every query, so the union follows land as it appears, grows or goes away. Inside means inside any part. Edges are the parts' borders that no other part covers. Owned land answers `OwnerAt` / `IsSafeFor` through its owner; elsewhere any part that says safe wins. A query that re-enters the same union (two unions that see each other) gets an empty zone instead of looping. The subsystem's test override is not a provider and is not part of the union.
- **Giant shape** (`UGFGiantEventComponent`): at `BeginPlay` (server and clients) it builds `LandShape`, a union of its own circle regions (`Own`) and every other provider's land, skipping itself, and installs it with `SetSafeZone`. `GetSafeZone`, its provider zone, the sim's shapes (`FGFGiantLandZone::Base`) and the respawn point all use it. His default circle stays part of the shape.
  - `SetSafeRegions` replaces only his own circles; the shape keeps following land.
  - `SetSafeZone(other)` replaces the shape; `SetSafeZone(nullptr)` leaves his own circles only.
  - New `GetLandShape()` and `IsFollowingLand()`.
- **Movement rules are unchanged** (`FGFGiantSim::MoveToward`): he skirts every safe area in the shape (now including faction land outside his circle), walks into land only after an exposed target standing there, and walks back out the short way afterwards.

## Tests

- New world test `GFGame.Social.GiantLandShape` (`GFSocialTests.cpp`): two factions' land registered as a provider, both outside his default circle.
  - The shape covers his circle and both lands, and it is the subsystem's current zone.
  - Edges and closest points come from the nearest land.
  - Land that is added, removed or grown shows in the shape at once.
  - `SetSafeZone(nullptr)`, `SetSafeZone(GetLandShape())` and `SetSafeRegions` behave as described above.
  - He chases a visitor round four corners around both lands for 180 simulated seconds and never sets foot on either land.
  - He follows the unprotected visitor onto faction land. Once the visitor is protected, he walks back off.
  - Nobody is eaten and the giant deals no damage.
- Existing giant and protection tests are unchanged.

## Notes

- A snacked player's respawn point (`FGFGiantRules::RespawnPoint`) and shoves (toward the nearest safe area) now use the whole shape, so both can point at another faction's land where that player isn't protected. This is unchanged in kind from the decision that shoves keep their geometric direction.

## Pending follow-up (not done here)

- Once the ferry branch merges, the giant should skip actors tagged `GF.FactionProtected` (`FGFProtectedActor::Is`): never target, shove or eat them.

## Build and test

Win64 only: `Build.bat GFGameEditor Win64 Development -Project=<worktree>\GFGame.uproject -WaitMutex`, then the headless suite `Automation RunTests GFGame+VoxelFinish`. No changes to config, data tables or other non-code assets.
