# HANDOFF: feature/playable-grumble

Branch off `staging` at `fa4b4c3`. Big Grumble's director now spawns with the game mode, and a playtest can bring him in from the console without waiting 2 days and 3 hours.

## What changed

- `DefaultGame.ini` appends `+StartupDirectorClasses=/Script/GFGiant.GFGiantDirector` in the existing `[/Script/GFGame.GFGameMode]` section. `AGFGameMode::InitGameState` already skips a class that is in the world, and `AGFGiantDirector::Get` returns the one it finds before it spawns. Together those stay one director. `GFGameMode.cpp` is unchanged.
- The director constructor already creates `UGFGiantEventComponent` (`CreateDefaultSubobject` in `GFGiantDirector.cpp`). Nothing to add there.
- New `Source/GFGiant/Private/GFGiantCheats.cpp` (declared in `Public/GFGiantCheats.h`):
  - `gf.Giant.Start` calls `ServerStartEvent`. On failure it logs why: no world, no director, an event is already running, or nobody is in. A client is ignored and not logged.
  - `gf.Giant.End` calls new `ServerEndEvent`, which calls the existing private `EndEvent` (destroy the body, clear `bEventActive`). Same teardown as a natural end. It does not rewind the saved trigger clock. Failure logs no director or no event running. A client is ignored.
  - `gf.Giant.Status` logs `active`, sim state (or `Idle`), target, players counted by `GatherPlayers`, real seconds and play seconds still to go, and `next` (the longer of those two clocks, or `needs a player` / `needs someone outside safety`). It also logs the playtest override (`off`, armed, seconds left, or `done`).
- `PlaytestFirstEventSeconds` on `UGFGiantEventComponent` (config, default 0). Above 0, the first event of this session starts that many seconds after the first player with a pawn is in. The clock is wall time from that moment (it does not pause if they leave) and the event only starts while someone is in. It does not require anyone to be outside safety, so a playtest in the hub still sees him. While that first event is still owed, the 2 day / 3 h clock does not also fire. After any event this session (playtest or `gf.Giant.Start`), the normal clock applies again, including the exposed-player check. `bAutoTrigger` does not gate this override. Left at 0, so existing tests are unchanged. The playtest path is `gf.Giant.Start`.

## Lines touched in files PR #72 also edits

`GFGiantTypes.*` is not touched. `GFGiantEventComponent.h`:

- Line 35: `UCLASS` gains `Config = Game`.
- Lines 118–130: `PlaytestFirstEventSeconds`, `PlaytestSecondsSinceFirstPlayer`, and `ServerEndEvent` next to `ServerStartEvent`.

`GFGiantEventComponent.cpp`:

- Line 15: `#include "GFGiantCheats.h"`.
- Lines 382–391: `ServerEndEvent` immediately after private `EndEvent`.
- Lines 552–570: the playtest clock hook in `TickComponent`, after the active-event early return and before `TriggerCheckIn`. Away from the handoff / taunt edits (BeginPlay, EndPlay, GatherPlayers, OnTaunt).

## Config keys

`[/Script/GFGame.GFGameMode]`

- `+StartupDirectorClasses=/Script/GFGiant.GFGiantDirector`

`[/Script/GFGiant.GFGiantEventComponent]`

- `PlaytestFirstEventSeconds=0` (0 = off)

## Tests

`Source/GFTests/Private/GFGiantPlaytestTests.cpp` (`EditorContext | EngineFilter`):

- `GFGame.Giant.Playtest.DirectorStartup` — the startup list keeps social, paint, and the giant; after `AGFGameMode::InitGame` exactly one director exists and its event component is set; `Get` / `GetEvent` do not spawn another; a director already in the world is not spawned again.
- `GFGame.Giant.Playtest.Start` — `GFGiantCheats::Start` (what `gf.Giant.Start` calls) refuses with nobody in, starts once one player has a pawn, spawns the body at `Sim.Location + (0, 0, 600)`, and `End` destroys that body.
- `GFGame.Giant.Playtest.Maths` — `FGFGiantPlaytest::Advance`, `IsFirstEventDue`, and `HoldsOffNormalClock` with no world. 0 and negative stay off. The clock starts when the first player is in, is due at the threshold, keeps counting if they leave, and stops once a session event has started. `FGFGiantRules::ShouldTrigger` is still false on a fresh save while the playtest clock is due.

## Decision

`PlaytestFirstEventSeconds` above 0 replaces the first event of the session: wall time from the first pawn, no exposure check, and the 2 day / 3 h clock waits until that event has happened. `gf.Giant.End` uses `EndEvent` and leaves the saved clock as `ServerStartEvent` set it. `bAutoTrigger` does not suppress the override. Default stays 0; playtests use `gf.Giant.Start`.

Recorded as issue #94 (`Decision: playtest giant starts on a wall clock, and End uses EndEvent`, body starts with `Label: design-decision`). The token on this run can open issues but cannot comment (`Resource not accessible by integration`), so #81 has no progress comments. The commit hash is in the PR.

## Dev PC steps

1. Build and run the three tests above (`GFGame.Giant.Playtest`). Win64: `Build.bat GFGameEditor Win64 Development -Project=<worktree>\GFGame.uproject -WaitMutex`, then `Automation RunTests GFGame.Giant.Playtest`.
2. In PIE, open the console and type `gf.Giant.Start`. He spawns at `Sim.Location + 600 Z` and should be visible (as a sphere until the visuals branch lands).
3. `gf.Giant.Status` prints the line. `gf.Giant.End` removes him.
4. Run the command on the server. Standalone and listen-server PIE are fine. A client ignores it.
5. Leave `PlaytestFirstEventSeconds` at 0 unless you want the first event of a session to start on its own. Any value above 0 arms that timer for the session.

## Follow-ups

- The skeletal body is the visuals brief. This branch does not touch `AGFGiantDino`.
- PR #72 (`feature/grumble-handoff`) should merge cleanly if it stays out of the lines listed above. `GFGiantTypes.*` was left alone.
- Belly cams already outlive a natural end. `gf.Giant.End` does the same.
