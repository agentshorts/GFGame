# Wall climb #355

Run at a wall, keep pushing into it, and you climb straight up. From the climb you can turn along the wall into the existing wall run (#193), push over a lip with a short mantle, or jump off with the same wall-kick a cling uses. Not compiled on the authoring box; the automation tests below are the gate.

Code: `Source/GFGame/Movement/GFTraversalSim.cpp` (sim), `GFStaminaSim.h/.cpp` (state, gravity, jump and dash gates), `GFCharacterMovementComponent.h/.cpp` (probes, velocity hand-off, replication, getters), `GFMovementTuning.h` (WallClimb category).

## Tunables (UGFMovementTuning, category WallClimb)

| Name | Default | What it does |
| --- | --- | --- |
| WallClimbSpeed | 350 | Climb speed straight up, cm/s |
| WallClimbStickSpeed | 60 | Push into the wall while climbing, cm/s |
| WallClimbDrainPerSecond | 12 | Stamina per second while climbing |
| WallClimbMaxSeconds | 1.5 | Longest climb per wall visit (about 5 m at 350 cm/s) |
| WallClimbCoyoteSeconds | 0.12 | A climb can still start this long after leaving the floor |
| WallClimbIntoMin | 0.5 | Minimum push into the wall (dot with the wall normal); it must also beat the along-wall part |
| WallClimbProbeDistance | 20 | Wall-ahead probe reach past the capsule, cm |
| WallClimbProbeAboveStep | 20 | Wall-ahead probe height above MaxStepHeight, cm, so stairs and one-block steps stay steps |
| WallClimbToRunSpeed | 450 | Speed a climb carries into a wall run; keep it above WallRunMinSpeed (400) |
| WallClimbMantleReach | 220 | Highest lip a climb mantles onto, cm above the feet |
| WallClimbMantleMinLip | 30 | Lowest lip that counts, cm above the feet |
| WallClimbMantleInset | 10 | How far past the wall face (beyond the capsule radius) the mantle stands, cm |
| WallClimbMantleCost | 10 | Stamina paid when the mantle starts |
| WallClimbMantleUpSpeed | 600 | Mantle rise speed, cm/s |
| WallClimbMantleForwardSpeed | 450 | Mantle step onto the lip, cm/s |
| WallClimbMantleMaxSeconds | 0.8 | Safety timeout; a mantle that never arrives ends here |
| WallClimbMantleArrive | 4 | Distance to the mantle target that counts as arrived, cm |

The Kindle Ring and cost-free runs make the climb and the mantle free, like every other traversal move.

## Contact (FGFTraversalContact)

The side wall probe used by wall run only looks left and right of the velocity, so it cannot see a wall straight ahead. The CMC adds a forward probe (`GatherClimbContact`):

- `bWallAhead` / `WallAheadNormal`: sphere sweep along the input (or into the climbed wall while climbing), from above step height, near-vertical walls facing the pawn only.
- `bLedge` / `LedgeTarget` / `LedgeHeight`: a line trace down just past the wall face finds a walkable lip between MantleMinLip and MantleReach above the feet; a capsule must fit at the target and the path straight up must be clear.

If there is no wall ahead, a side `bWallContact` that the input pushes into also works. Both sets of fields ride in the saved move, and a change in `bWallAhead`, `bLedge`, climbing or mantling stops two moves from being combined.

## State machine

```
            into wall + (grounded or coyote) + not busy + stamina
   None  --------------------------------------------------------->  Climbing
     ^                                                                 |  |  |  |
     |  peel (no input / away), wall lost, time or stamina out (blocks)|  |  |  |
     +-----------------------------------------------------------------+  |  |  |
     |  jump: wall-kick off the normal, hops and air dash back (blocks)   |  |  |
     +--------------------------------------------------------------------+  |  |
     |                                     along input: hand-off to WallRun  |  |
     |     (same WallRunTimeLeft budget, same blocks, counted as airborne)   |  |
     +-----------------------------------------------------------------------+  |
     |                 lip in reach + can pay MantleCost: Mantling (rise, then  |
     |                 step forward; ends on arrival, on landing or timeout)    |
     +--------------------------------------------------------------------------+
```

- Busy wins: cling, dash, pole and rope clear the climb and the mantle. Climbing and mantling block dash and sprint.
- "Blocks" sets `bWallClimbBlocked`. It clears once the input stops pushing into a wall, so holding into the same wall after a drop does not re-climb it.
- A grounded jump into a wall is a normal hop, not a climb.
- Jump during a mantle is ignored; the mantle finishes.
- Gravity is off while climbing or mantling; the landing marker is hidden.
- Beat Hop runs never climb (the kit list does not include it).
- Mini-games that turn stamina off clear the climb like every other traversal state.
- `UGFCharacterMovementComponent::bWallClimbAllowed` (default true) is copied onto `FGFStaminaCommand` before the Beat Hop mask. `GFGame.Movement.StairKit.WallStaysAWall` clears it so the test still measures stair refusal on a wall, not climb.
- `AGFGameMode::EnsureVoxelWorld` skips the starter voxel plot on mini-game and tutorial maps (and on the mining range), so a grass top at z=0 does not z-fight the map floor.

### Velocity hand-off in the CMC

`PhysFalling` keeps the old Z and only lets the horizontal part through `CalcVelocity`. `ApplyWallClimbMove` runs just before `Super::PerformMovement`: a climb that starts on the floor switches to falling, and the sim's override velocity (Z included) is written into `Velocity`. When the climb drops without an override, any upward Z is cut so the pawn falls from where it let go.

## Anim hooks for #356

There is no traversal owner in `EGFAnimOwner` yet, so nothing changes in the anim stack. Missing clips stay on locomotion, so there is no T-pose.

- `UGFCharacterMovementComponent::IsWallClimbing()` (BlueprintPure)
- `UGFCharacterMovementComponent::IsMantling()` (BlueprintPure)
- `UGFCharacterMovementComponent::GetWallClimbPhase()` returns `EGFWallClimbPhase` { None, Climbing, Mantling } (BlueprintPure)
- Remote players: the server writes `ReplicatedWallClimbPhase` (COND_SkipOwner) and the getters read it on simulated proxies. The owner and the server read the predicted sim.

Suggested use: a Climb clip while Climbing, and a short Mantle clip (about 0.3 to 0.6 s, root motion off) while Mantling. When the phase goes back to None, blend to fall or land as usual.

## Tests

`GFGame.Movement.WallClimb.Enter`, `.Drop`, `.ToWallRun`, `.Mantle`, `.BusyBlocks` in `Source/GFGame/Movement/Tests/GFMovementTests.cpp`. Run with `Automation RunTests GFGame.Movement.WallClimb`. `GFGame.Movement.WallRun` and `GFGame.Movement.DashGrip` should stay green.

## Not in v1

- No grab-hang or ledge shimmy (#259 stays closed).
- No sideways climbing: input along the wall hands off to a wall run instead.
- The mantle is a two-part velocity move, not a curve or root-motion clip.
