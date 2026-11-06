# Traversal current move (#386)

Staging `11e14d4` has no wall climb statechart. A search for `statechart` and for a `.tla` file finds nothing. `docs/WALL_CLIMB_355.md` is a design note. Its sketch covers climb and mantle only, not dash, cling, pole, or rope.

The move model is `docs/TraversalCurrentMove.tla`. Two checks:

- **OneInCharge.** `current` is one value: none, dash, grip, wall run, wall climb, mantle, swivel, swing, or yank.
- **BlockClears.** `wallRunBlocked` implies current is not wall run. `wallClimbBlocked` implies current is not wall climb.

Exit velocity is not a move. A kick, a pole launch, a rope release, or a zero stop leaves `current` at none.

## Code overlaps

`FGFStaminaState` kept the move as independent bools. `GFTraversalSim::Apply` made them exclusive only by branch order. `bBusy` included `bOverrideVelocity`, which is written earlier in the same tick, so a move that had already ended could block the next one. `EGFWallClimbPhase` is still derived from the climb and mantle flags for replication. It is unchanged.

Overlaps traced from `GFTraversalSim::Apply` and `GFStaminaSim::Step`:

| Overlap | Where | Old step | Now |
| --- | --- | --- | --- |
| Climb kick, cling kick, pole launch, or rope release, plus a dash press | Exit override is set, then Step starts a dash because busy ignores override | `bDashing` and `bOverrideVelocity` | The exit velocity owns the tick. No dash. `CurrentMove` is None. |
| Yank arrives on the anchor (override is zero) while a climb can start | `bBusy` treats that zero override as busy | Climb does not start | Zero stop is not busy. Climb starts. `CurrentMove` is WallClimb. |
| Yank arrives and a dash press has no wall | Dash starts and leaves the zero override set | Dash and override | Dash clears the stop. `CurrentMove` is Dash. |
| `bWallRunning` and `bWallRunBlocked` both set, still on the wall, fast, along | The run continues while the block is set, and it spends stamina | Still running | The block clears the run before the drain. |
| `bWallClimbing` and `bWallClimbBlocked` both set, still pushing into the wall | The climb continues and spends stamina | Still climbing | The block clears the climb before the drain. |
| `bSwinging` and `bYanking` both set | Each continue branch keeps its own flag | Both true | Yank cancels swing. `CurrentMove` is Yank. |
| `bGripping` and `bDashing` both set | Grip does not clear a dash that is already true, and `DashTimeLeft` revives it | Both true | Grip clears the dash and the timer. `CurrentMove` is Grip. |
| `bMantling` and `bWallClimbing` both set | The mantle branch does not clear the climb flag | Both true | Mantle clears the climb. `CurrentMove` is Mantle. |

`GFTraversalSim::PublishCurrentMove` runs at the end of `GFStaminaSim::Step`. It drops every move flag except the one in charge, clears a blocked wall run or wall climb, and writes `EGFTraversalMove CurrentMove`. A dash drops any override so the dash is the only velocity. Priority if more than one flag is still set: yank, swing, swivel, grip, mantle, wall climb, wall run, dash. That is the move that cancels the others when it starts, then the wall chain, then dash.

## Tests

`GFGame.Movement.CurrentMove` in `Source/GFGame/Movement/Tests/GFMovementTests.cpp`. Each overlap above has an assert that the old flags fail: a second move still set, a block that keeps spending, or a zero stop that blocks the climb. `GFGame.Movement.WallClimb.BusyBlocks` also checks that a plain wall run publishes `EGFTraversalMove::WallRun`.
