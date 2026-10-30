# HANDOFF: feature/grumble-protection

Branch off `staging` at 9d5be90 (faction protection). Big Grumble (GFGiant) now respects per-player protection, may walk into land after an unprotected visitor, and can never enter the jail cell. Local commits only; nothing pushed.

## What changed

- **Who it may take** (`FGFGiantRules::IsExposed`): a player is exposed when `IGFSafeZoneQuery::IsSafeFor(player, location)` says unsafe (so faction protection, guests, members and the host count) and they are not inside a no-entry box. It only ever targets, shoves or eats exposed players. This replaces `IsInsideSafe` in the three places that decided it: the trigger (`AnyoneOutside`), target choice (`PickTarget`) and shoves (`FGFGiantSim::Step`).
- **Protection reaches the sim through the land owner** (`FGFGiantLandZone`, built by `UGFGiantEventComponent::GetLandZone`): shapes (inside, edges, closest points) still come from the giant's own zone (`GetSafeZone`: its circle regions or a custom zone), but per-player safety and owners are asked of `UGFSafeZoneSubsystem` first (`OwnerAt` / `IsSafeFor`), falling back to the giant's zone on unowned land. Player views now carry `GroupId` (`AGFPlayerState::FactionId`) and `bHost`, so `FGFGiantPlayerView::Subject()` is the safety subject. The sim itself stays world-free and deterministic.
- **Movement** (`FGFGiantSim::MoveToward`): it walks straight into safe territory only while chasing an exposed target who stands there (an unprotected visitor on faction land). Otherwise (spawn, circling, chasing a target in the open, leaving) it skirts safe territory as before. After chasing someone in it walks out the shortest way (`bWalkingOut`) instead of jumping; territory growing under it still pushes it out at once, as before. Note that faction land the giant's own regions don't cover was never a barrier to it; only its own regions are.
- **Attacks**: a protected attacker (or one out of reach) is never eaten; the hit is answered with `Ints = [0]`. An exposed attacker is eaten as before.
- **No-entry boxes** (GFCore, game-agnostic): `FGFNoEntry` (pure helpers) and `UGFSafeZoneSubsystem::SetNoEntry / ClearNoEntry / GetNoEntry / IsInsideNoEntry`. `AGFJailCell` registers its whole cell box (walls, floor and ceiling included) at spawn (`PostInitializeComponents`) and clears it at `EndPlay`; a destroyed cell drops out on its own (weak owner). The giant never moves into a box that overlaps its body height (`FGFGiantTuning::BodyHalfHeight`, 600) by its radius (`GiantRadius`); boxes above or below it (the cell is underground) don't block it. Anyone inside a box (a jailed player) is out of reach: not counted by the trigger, never targeted, shoved or eaten, and the giant never arrives next to them. Jailed players have no protection; the box is what keeps them safe from it.
- **{Dragger}**: unchanged mechanism (`AGFPlayerState::LastDrag`, cleared on re-protect). Every line aimed at the target (Spotted / Return, Target, Chase, CloseCall, idle warning, Snacked) fills `{Dragger}` from it, and targets are always unprotected now. `DT_GiantDinoLines.csv` still has no `{Dragger}` lines (content, not touched here); the token drops cleanly when a line has it and nobody dragged the target.
- **Herding direction**: unchanged (geometric). Shoves point toward the nearest safe region of the giant's zone, and inward when the player already stands inside one, so an unprotected visitor in the giant's regions is pushed deeper in. It still never damages.

## Tests

- New `GFTests/Private/GFGiantProtectionTests.cpp`: `GFGame.Giant.Protection.Rules`, `.Chase`, `.NoEntry`, `.Dragger`.
- New world test `GFGame.Social.GiantProtection` (in `GFSocialTests.cpp`): land zone wiring through the subsystem, player views, the jail cell as a no-entry box, event target choice, protected and jailed attackers not eaten.
- Existing giant tests unchanged and still passing (`NeverEntersSafe` holds: no exposed player stands inside safety in it).

## Open decisions (defaults chosen, to be confirmed)

1. Protection is asked through the subsystem's land owner, shapes from the giant's own regions.
2. It enters land only after an exposed target standing there; spawn, circling and leaving still skirt it.
3. The jail is a GFCore no-entry box, height-aware; jailed players are out of reach (not targeted at all).
4. A protected attacker is not eaten.
5. Herding and shoves keep the geometric direction (toward or into the nearest safe region).
6. `{Dragger}` comes from LastDrag wherever the target is; no CSV lines added.
7. The trigger counts exposed players only.

## Build and test

Win64 only: `Build.bat GFGameEditor Win64 Development -Project=<worktree>\GFGame.uproject -WaitMutex`, then the headless suite `Automation RunTests GFGame+VoxelFinish`. No changes to config, data tables or other non-code assets.
