----------------------------- MODULE TraversalCurrentMove -----------------------------
\* #386 traversal: one current move.
\*
\* No wall climb statechart is in the repo at staging 11e14d4. docs/WALL_CLIMB_355.md
\* is a design note with a climb-only sketch. This module is the move model.
\* It covers dash, grip (cling), wall run, wall climb, mantle, swivel (pole),
\* swing (rope), and yank (rope).
\*
\* Two checks:
\*   OneInCharge  at most one move is in charge (current is a single value)
\*   BlockClears  a wall-run block or a wall-climb block means that move is not current
\*
\* An exit velocity (kick, pole launch, rope release, zero stop) is not a move.
\* current is "none" while only that velocity remains.
\*
\* Checked by enumerating Next from every type-correct state (8 moves, 2 blocks).

EXTENDS FiniteSets

Moves == {"dash", "grip", "wallRun", "wallClimb", "mantle", "swivel", "swing", "yank"}

VARIABLES current, wallRunBlocked, wallClimbBlocked

TypeOK ==
    /\ current \in Moves \cup {"none"}
    /\ wallRunBlocked \in BOOLEAN
    /\ wallClimbBlocked \in BOOLEAN

OneInCharge == current \in (Moves \cup {"none"})

BlockClears ==
    /\ wallRunBlocked => current # "wallRun"
    /\ wallClimbBlocked => current # "wallClimb"

Init ==
    /\ current = "none"
    /\ wallRunBlocked = FALSE
    /\ wallClimbBlocked = FALSE

\* Take replaces whatever was in charge. One step still ends on one move.
\* A block refuses the move it closed, so Take cannot enter a blocked move.
Take(m) ==
    /\ m \in Moves
    /\ (m = "wallRun" => ~wallRunBlocked)
    /\ (m = "wallClimb" => ~wallClimbBlocked)
    /\ current' = m
    /\ UNCHANGED <<wallRunBlocked, wallClimbBlocked>>

End ==
    /\ current # "none"
    /\ current' = "none"
    /\ UNCHANGED <<wallRunBlocked, wallClimbBlocked>>

BlockWallRun ==
    /\ current = "wallRun"
    /\ current' = "none"
    /\ wallRunBlocked' = TRUE
    /\ UNCHANGED wallClimbBlocked

BlockWallClimb ==
    /\ current = "wallClimb"
    /\ current' = "none"
    /\ wallClimbBlocked' = TRUE
    /\ UNCHANGED wallRunBlocked

ClearWallRunBlock ==
    /\ wallRunBlocked
    /\ current # "wallRun"
    /\ wallRunBlocked' = FALSE
    /\ UNCHANGED <<current, wallClimbBlocked>>

ClearWallClimbBlock ==
    /\ wallClimbBlocked
    /\ current # "wallClimb"
    /\ wallClimbBlocked' = FALSE
    /\ UNCHANGED <<current, wallRunBlocked>>

Next ==
    \/ \E m \in Moves : Take(m)
    \/ End
    \/ BlockWallRun
    \/ BlockWallClimb
    \/ ClearWallRunBlock
    \/ ClearWallClimbBlock

Spec == Init /\ [][Next]_<<current, wallRunBlocked, wallClimbBlocked>>

Invariant == TypeOK /\ OneInCharge /\ BlockClears

=============================================================================
