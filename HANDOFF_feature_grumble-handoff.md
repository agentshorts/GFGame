# HANDOFF: feature/grumble-handoff

Branch off `staging`. Big Grumble thanks the player who set up an unprotected visitor, and a taunt emote calls him toward that spot. Placeholder line text is unchanged.

## What changed

- **Handoff lines.** `EGFGiantLinePhase::Handoff` and `GD_Handoff_01` through `GD_Handoff_14` in `Data/CSV/DT_GiantDinoLines.csv`. The text is the placeholder copy, including `{Dragger}` and `{Target}`. All chat, same channel as his other lines.
- **Who counts as a handoff** (`FGFGiantRules::IsHandoffCandidate`). An exposed player someone else just set up:
  - Protection was turned off within `FGFGiantTuning::HandoffWindowSeconds` (30s). The event fills `FGFGiantPlayerView::DragAgeSeconds` from `AGFPlayerState::LastDrag`.
  - Or someone else is holding them with a grab right now (`bHeld`, published from the social tethers through `FGFAttention`).
  A revoke by the player themselves does not count. A grab counts for the whole tether, including after protection is turned off, and stops when the tether drops.
- **Who he chases** (`FGFGiantRules::PickInterest`, used by the sim). Each step: a handoff anywhere, then an exposed player inside an active taunt, then the usual newest exposed player. Among handoffs, a player being held wins, then the more recent revoke, then the usual tiebreak. He leaves his current target as soon as a higher pick appears.
- **One thanks.** The visit still opens with Spotted or Return. He then posts one Handoff line aimed at that target. `{Dragger}` is the player who turned protection off, or the holder's name when there is no revoke. The same target is not thanked again that visit. A different handoff target is.
- **Taunt.** `DT_Emotes.csv` row `Taunt` (`TauntRadius` 4500). Any emote with `TauntRadius` above 0 calls him (`FGFAttention::RaiseTaunt`), including from inside safe land. It does not raise the wild-dino noise by itself. Jailed players still cannot emote. If he is not already out, the call is ignored. While the call is active (15s) and nobody exposed is inside its radius, he walks to the spot, skirts safe land, and stays out of no-entry boxes. He still only shoves and eats exposed players, and he still enters safe land only when an exposed target is standing there.
- **Jail.** No-entry is unchanged. A jailed player is not exposed, so handoff flags and a taunt inside the cell do not make him a target and do not let Big Grumble into the box.

## Decisions

Defaults are written up so they can be changed later:

- #67 — a handoff is a revoke within 30 seconds, or a live grab.
- #68 — a handoff beats a taunt, and both beat his current target.
- #69 — the taunt is an emote row and does not start his visit.
- #70 — one Handoff line per target; the dragger's name, otherwise the holder's.

#66 was opened first with an incomplete body. This token can create issues and cannot edit or close them, so #66 is still open. Close it and keep #67.

Status comments on #40 were rejected (`issues=write` is not granted to this integration). The notes that would have been posted are:

- Started: Handoff lines, the thanks when someone else makes a player available, and a taunt that draws him in. He still does not attack protected players or enter the jail.
- Finished: branch `feature/grumble-handoff`, head commit on that branch. Tests `GFGame.Giant.Handoff.*`.

## How to test

Headless, after a Win64 editor build:

`Automation RunTests GFGame.Giant.Handoff`

That is four sim tests plus the world wiring test:

- `GFGame.Giant.Handoff.Lines` — the 14 rows, token fill, and the Taunt emote row.
- `GFGame.Giant.Handoff.Priority` — fresh revoke, live grab, stale revoke, he switches off his current target, one thanks, a second target gets their own.
- `GFGame.Giant.Handoff.Taunt` — the taunt pulls him onto the exposed player there, a handoff beats it, protected players are not shoved or eaten, he does not enter their land, and he goes back to hunting when the taunt ends.
- `GFGame.Giant.Handoff.Jail` — a jailed handoff is ignored, and a taunt inside the cell brings him up to the box and not into it.
- `GFGame.Giant.Handoff.Wiring` — unprotect plus grab on the social component, he prefers that visitor, the thanks line is filled, the taunt emote calls him from safe land without dino noise, a taunt before he is out is dropped, and a jailed player cannot taunt.

`GFGame.Giant.Lines` now expects 86 lines and the three extra `{Target}` handoff rows. The older giant and protection tests are unchanged in what they assert.

In a session: on your land, turn a visitor's protection off (or grab them on the land), taunt with the Taunt emote while he is already out, and he should thank you in all chat and come for that visitor. A protected neighbour, and anyone in the jail, stay off the menu.

## Open points

- The 14 lines are placeholder copy. Wording can change in the CSV without a code change.
- 30 seconds, 15 seconds, and 4500 cm are tuning defaults (`FGFGiantTuning` and the emote row).
- A taunt while he is away does not start a visit and is not remembered.
- One thanks per target per visit. A new visit can thank them again.
- `{Dragger}` prefers the player who turned protection off when both a revoke and a grab are set.

## Build

Not compiled here. This machine has no Unreal editor. The new code uses the existing giant and social APIs. `GFCore` gained `GFAttention` (no new module dependency: giant and social already depend on `GFCore`).

Win64, same as the protection handoff: `Build.bat GFGameEditor Win64 Development -Project=<worktree>\GFGame.uproject -WaitMutex`, then `Automation RunTests GFGame.Giant.Handoff+GFGame.Giant+GFGame.Social`.
