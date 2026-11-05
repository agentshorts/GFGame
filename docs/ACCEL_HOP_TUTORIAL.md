# Acceleration Hop tutorial (issue 348)

Live map `/Game/Maps/L_AccelHop_Tutorial_v2` (the visual pass, #348; the hub gate and the cook list point at it), gate id `AccelHopTutorial`, hub label "Accel Hop 101".
L_Beat is the source of the copy and is never edited. `L_AccelHop_Tutorial_v1` is the first build and stays on disk;
a new version of the tutorial is a new map (`GFTUT_MAP=/Game/Maps/L_AccelHop_Tutorial_vN`), never a save over an old one.

FX (`/Game/FX/AccelHop`, spec in the visual pass `fx_hooks_v1.md`): the tutorial director spawns NS_HopBoost_v1 at the
player's feet on an applied lit or Boost landing, NS_HopLitPop_v1 on the tile top for a lit landing, and keeps one
NS_HopLitIdle_v1 attached to the road's lit tile. Local cosmetics only, skipped on a dedicated server, and a missing
system logs once and plays nothing.

## How it fits together

| Piece | Where |
| --- | --- |
| Side gate entry (beside the Beat Hop ring portal, not on the ring) | `FGFMiniGameCatalog::Extras()` in `Source/GFCore` |
| Gate subsystem (map name for the hub portal) | `Source/GFRhythm/Public/GFAccelHopTutorialGateSubsystem.h` |
| Lessons, goals, prompt copy, progress rules (pure, no world) | `Source/GFRhythm/Public/GFAccelHopTutorialRules.h` |
| Director: listens to judged landings, skips ahead by position, finish HUB gate | `Source/GFGame/World/GFAccelHopTutorial.h` |
| Landing event the director listens to | `UGFCharacterMovementComponent::OnAccelHopLanded` |
| Prompt panel | `AGFHUD::DrawAccelHopTutorial` |
| Lit opt-out per tile (lessons 1 and 2) | `AGFAccelHopTile::bCanBeLit`, `FGFHopTileSlot::bCanLight` |
| Course plan (pure Python) | `Tools/accel_hop_tutorial_layout.py` |
| Map build (editor Python) | `Tools/create_accel_hop_tutorial_map.py` |
| Tests | `Source/GFGame/World/Tests/GFAccelHopTutorialTests.cpp` (`GFGame.AccelHop.Tutorial`), `Tools/test_accel_hop_tutorial_layout.py` |

## Design defaults

- Lesson goals: Beat 4 on-beat landings; Steer 3 side switches (any judgement); Lit 2 lit landings;
  Chain best chain 6 on-beat plus 1 zigzag plus 1 lit.
- Course: one straight line along +X. Beat 12 tiles at 200 cm (no Boost, Downbeat every 4th); Steer 8 tiles
  at 450 cm, +-250 cm sideways; Lit 16 tiles with the world road's bar pattern; Chain 8 strip, 4 zigzag, 8 strip.
  800 cm walk between lessons. Finish HUB gate 600 cm past the last tile. Continuous floor, top at Z 0.
- One road box for the whole course, so there is one song and one beat clock.
- Changing a goal or copy: edit `FGFAccelHopTutorialRules::Lessons()` and the strings in `PromptFor`. Changing the
  course: edit the constants in `Tools/accel_hop_tutorial_layout.py` and rerun the map script (reruns replace,
  they never stack). Lesson ids must stay the same in both (a Python test checks).

## Visual pass hooks

Code and prompts are done. Everything below is a placeholder, waiting for the visual pass:

- Gate: the shared sim gate mesh dyed pink (`PlaceholderColor` on the catalog entry). No pictogram sign yet:
  set `SignObject` on the Extras entry in `GFMiniGameCatalog.cpp` when a sign mesh exists.
- Prompt panel: flat dark violet box, magenta title, gold goal line, default HUD font. All in
  `AGFHUD::DrawAccelHopTutorial` (marked VISUAL PASS). Swap for panel art or a widget there.
- "Nice!" flash: text only. A sound or a sparkle on lesson change can key off
  `AGFAccelHopTutorial::GetProgress().Serial` or `GetLastAdvanceTime()`.
- Floor: `/Engine/BasicShapes/Cube` with the default material, actor `Tutorial_Floor` in folder
  `AccelHopTutorial/Floor`. A rerun of the map script respawns it, so set the material in the script
  (`place_in_current_level`) rather than by hand in the map.
- Light: a directional light, sky light and sky atmosphere are added only if the copy has none
  (folder `AccelHopTutorial/Light`). Swap for the sky and light rig of the visual pass.
- Tiles: the same tile meshes, cel material and outline as the world road. Lesson 1 and 2 tiles never light.
  Per-lesson tinting can key off the tag `GFAccelHopTutorial.Lesson.<Id>`.
- Lesson boundaries: no arch or sign between lessons yet. Lesson starts are at the first tile of each lesson
  (see the map report `Saved/ArtImport/accel_hop_tutorial_map_report.json`, `lessons[].start_x`).
