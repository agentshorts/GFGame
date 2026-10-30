# GFGame

A cozy co-op dino builder made in Unreal Engine 5.8. Build a chunky voxel home and garden, invite friends to claim plots on your land (rent is paid in materials), tame predator dinos by learning what they like to eat, and earn materials in mini-games: a dino arena, a rhythm obby (Beat Hop), a side-scrolling platformer, a fashion show and more. Materials are the only currency.

- Plan: see [PLAN.md](PLAN.md) (milestones, economy, modules, art sourcing, saves, risks, open questions).
- Status: early greybox. The C++ modules build (UE 5.8.4, GFGameEditor Win64 Development) and the GFGame.* automation tests pass.
- All names, creatures and art are original to this project.

Public source. No open-source license has been chosen yet, so all rights are reserved for now.

## Layout

- `Source/`: C++ modules. GFCore (items, inventory, recipes, mini-game gates), GFVoxel (100 cm block grid, DT_Blocks, save chunks), GFBuild (crafting), GFGarden (plots, growth stages), GFCompanion (dinos, diet/taming, aggro), GFFashion, GFMoba, GFRhythm (Beat Hop beat clock, WASAPI loopback "Listen to" output device; no mic), GFSave (versioned save game), GFGame (game mode, character, controller), GFTests (editor-only automation tests).
- `Data/CSV/`: source of truth for DataTables (DT_Blocks, DT_Items, DT_Recipes, DT_Plants, DT_DinoDiet). Import with `Tools/import_datatables.py`.
- `docs/ASSET_BRIEFS.md`: art briefs; `docs/LicenseLedger.csv`: asset license log.
- `handoff/`: Blender to Unreal art handoff (inbox, imported, rejected, style). Not tracked by git.
- `*.uasset` and `*.umap` are stored in Git LFS (see `.gitattributes`).

## Build and test

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" GFGameEditor Win64 Development -Project="%CD%\GFGame.uproject" -WaitMutex
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" GFGame.uproject -ExecCmds="Automation RunTests GFGame;Quit" -unattended -nullrhi -nosplash
```