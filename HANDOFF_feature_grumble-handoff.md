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
- **Taunt.** `DT_Emotes.csv` row `Taunt` (`TauntRadius` 4500, `TauntCooldown` 30). Any emote with `TauntRadius` above 0 calls him (`FGFAttention::RaiseTaunt`), including from inside safe land. It does not raise the wild-dino noise by itself. Jailed players still cannot emote. If he is not already out, the call is ignored. While the call is active (15s) and nobody exposed is inside its radius, he walks to the spot, skirts safe land, and stays out of no-entry boxes. He still only shoves and eats players he can touch, and he still enters safe land only when an exposed target is standing there. Re-taunting does not refresh that look: the same player cannot taunt again until `TauntCooldown` (30s, and never under 15s). Another player has their own clock. A Heart still uses the short shared emote cooldown.
- **Jail.** A jailed player is not protected, so they are exposed and can be the target, including a handoff. He paths to the near edge of the no-entry box (`NoEntryApproach`), the box still stops his body, and he cannot shove or eat through it (including a player who hits him from inside). Once he is at the edge he waits `JailDwellSeconds` (8s), then leaves that player for someone else for the rest of the visit while they stay in the box. If they leave the box, he can target them again. If nobody else is out, he circles.
- **Herded.** The Herded line is posted only when someone who was exposed is now safe. Already being home, a taunt look, or giving up at the jail does not say it.

## Decisions

Defaults are written up so they can be changed later:

- #67 — a handoff is a revoke within 30 seconds, or a live grab.
- #68 — a handoff beats a taunt, and both beat his current target.
- #69 — the taunt is an emote row and does not start his visit.
- #70 — one Handoff line per target; the dragger's name, otherwise the holder's.
- #83 — at a jail he waits 8 seconds on the box edge, then picks someone else. A jailed player can be the target. #67's line that a player inside a no-entry box is never a handoff is superseded. The box still blocks the bite.
- #84 — a taunt's per-player cooldown is 30 seconds, from `DT_Emotes`, and never under the 15 second look.
- #112 — wild-dino emote noise follows safe land (inside a safe area, or on owned land), not giant protection. An unprotected visitor on a plot stays quiet for wild dinos and can still be his target.

## How to test

Headless, after a Win64 editor build:

`Automation RunTests GFGame.Giant.Handoff`

That is four sim tests plus the world wiring test:

- `GFGame.Giant.Handoff.Lines` — the 14 rows, token fill, and the Taunt emote row including `TauntCooldown` (30s, floored at 15s).
- `GFGame.Giant.Handoff.Priority` — fresh revoke, live grab, stale revoke, he switches off his current target, one thanks, a second target gets their own.
- `GFGame.Giant.Handoff.Taunt` — the taunt pulls him onto the exposed player there, a handoff beats it, he does not enter land when only protected players are in the taunt, and he goes back to hunting when the taunt ends. With shoves and eating on, an exposed player and a protected player both inside the taunt: only the exposed one is shoved or eaten.
- `GFGame.Giant.Handoff.Jail` — a jailed handoff is the target. He stops at the box edge, does not enter, does not shove or eat them, waits out `JailDwellSeconds`, then takes the free player. The event is still running when they hit him. Alone at the jail he circles and does not say Herded.
- `GFGame.Giant.Handoff.Wiring` — unprotect plus grab on the social component, he prefers that visitor, the thanks line is filled, dropping the grab clears the held flag he sees, the taunt emote calls him from safe land without dino noise, a second taunt from the same player is on cooldown until `TauntCooldown`, a taunt before he is out is dropped, and a jailed player cannot taunt. The wild player stands on the unowned strip past his hub circle. The cooldown check advances `GetTimeSeconds` in steps that fit the frame-time cap.

`GFGame.Giant.Lines` now expects 86 lines and the three extra `{Target}` handoff rows. `GFGame.Giant.SafeZone` expects no Herded line when everyone was already home. `GFGame.Giant.Protection.Rules` and `Protection.NoEntry` expect a player in a no-entry box to be targetable and still untouchable. `GFGame.Social.GiantProtection` expects the jailed member to be the target when they win the tie, and still not eaten. The cell is on the unowned strip past his hub circle, because the default cell under the origin sits inside that circle and would count as safe.

In a session: on your land, turn a visitor's protection off (or grab them on the land), taunt with the Taunt emote while he is already out, and he should thank you in all chat and come for that visitor. A protected neighbour stays off the menu. A player in the jail can be the target: he runs up to the cell and cannot get in. The same player cannot taunt again for 30 seconds.

## Open points

- The 14 lines are placeholder copy. Wording can change in the CSV without a code change.
- 30 seconds (handoff window), 15 seconds (taunt look), 30 seconds (per-player taunt cooldown), 8 seconds (jail dwell), and 4500 cm are tuning defaults (`FGFGiantTuning` and the emote row).
- A taunt while he is away does not start a visit and is not remembered.
- One thanks per target per visit. A new visit can thank them again.
- `{Dragger}` prefers the player who turned protection off when both a revoke and a grab are set.

## Build

Not compiled here. This machine has no Unreal editor. The new code uses the existing giant and social APIs. `GFCore` gained `GFAttention` (no new module dependency: giant and social already depend on `GFCore`).

Win64, same as the protection handoff: `Build.bat GFGameEditor Win64 Development -Project=<worktree>\GFGame.uproject -WaitMutex`, then `Automation RunTests GFGame.Giant.Handoff+GFGame.Giant+GFGame.Social`.
