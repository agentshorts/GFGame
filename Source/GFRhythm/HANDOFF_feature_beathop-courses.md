# HANDOFF feature/beathop-courses-114

Beat Hop courses, rescue dinos and revenge dinos (#114). The course the hub gate already places on `L_Beat` grows the layouts. No new map.

## Win

A win is crossing the finish (`ReachFinish`). The grade and combo bundle (`BeatHop.SongEnd`) pays only then. A song that runs out, or a run that ends early, still scores and still shows a grade. It does not pay that bundle, and `bPassed` stays false. Landings still pay as they did.

## Courses

`FGFBeatHopCourses` (Fern Lane, Rime Skate, Cinder Drop), easiest first.

- Fern is the chart, unchanged.
- Rime weaves the lanes, drifts the runner sideways, and a Raptor chases.
- Cinder stretches the gaps, and a Scythepaw knocks once as you pass its post.

The lobby on the existing course has a course row and a Hire Ripwing tile. `AGFHubGateDirector` is unchanged: it still calls `BeginSession` on `L_Beat`.

## Dinos

Rescue is a hired Ripwing: 8 GlowDust, burned once through the ledger (`BeatHop.RescueHire`), 3 catches. Dropping below the bricks calls `NotifyFall`, which spends a catch and puts the runner back on the next brick. With no hire, the catch floor is unchanged.

Revenge is a greybox sphere on the course (engine sphere, lane tint). Ice chases. Lava swats once per runner. Both add velocity. Neither writes `MaxWalkSpeed`, `GravityScale` or `AirControl`. Those stay on `ApplyRunMovement`.

## Tests

`GFGame.Rhythm.CourseCatalog`, `GFGame.Rhythm.RescueBook`, `GFGame.Rhythm.RevengeSim`, `GFGame.BeatHop.Courses`, `GFGame.BeatHop.WinIsFinish`, `GFGame.BeatHop.RescueDino`, `GFGame.BeatHop.RevengeDinos`.

`GFGame.Rhythm.Course` now expects a timeout to pay nothing, and the clean run to pass by `ReachFinish`.

Not run here: this environment has no Unreal editor, so the automation tests were not executed.
