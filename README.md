# GFGame

Shatter Flats prison planet, a cozy safe circle, dangerous outside. A co-op dino builder made in Unreal Engine 5.8. Build a chunky voxel home and garden inside the circle, invite friends to claim plots on your land (rent is paid in materials), tame predator dinos by learning what they like to eat, and earn materials in mini-games out past the circle: a dino arena, a rhythm obby (Beat Hop), a side-scrolling platformer, a fashion show and more. Materials are the only currency. Camp colours: Rime pale ice blue `#9FD8F5`, Verdant leaf green `#8ED47A`, Cinderwrights ember `#D9542B`.

- Plan: see [PLAN.md](PLAN.md) (milestones, economy, modules, art sourcing, saves, risks, open questions).
- Status: greybox phase 2 (systems, server rules and headless tests; no maps or final visuals yet). The C++ modules build (UE 5.8.4, GFGameEditor Win64 Development) and all 86 GFGame.* automation tests pass. See "Status" below.
- All names, creatures and art are original to this project.
- All brand names in this game are fictional; any resemblance to real brands is coincidental or parody.

Public source. No open-source license has been chosen yet, so all rights are reserved for now.

## Status

Phase 1 (core): items/inventory/ledger, voxel building, crafting, garden, dino taming, fashion, MOBA, Beat Hop, saves, listen-server networking.

Phase 2 (greybox mini-games, each a server-side mode director driven by `FGFModeRequest` verbs):

| Mode | Module | What works | Tests |
|---|---|---|---|
| Dino Arena | GFArena | Staked bouts between tamed dinos, seeded fight sim, payouts through the ledger | GFGame.Arena.*, GFGame.Net.Arena |
| Hallow Night | GFDepths | 2.5D platform runs by depth, shared Z/X/C platforms, run rules, tier-4 loot, leave keeps a share, faint keeps loot, water channel with rented catch / float / fly | GFGame.Depths.*, GFGame.Net.Depths |
| Pebble Mine | GFMine | Pebbling crew (unlocked by first tame), room-only digging, haul on order, upgrades incl. Auto-Haul, capped offline gathering | GFGame.Mine.*, GFGame.Net.Mine |
| Overtime lab | GFLab | Timed junk-pile shifts (one pile at a time, overtime pays 1.5x after 90 s, lockdown at 135 s keeps 75 %, extract keeps all, dropping out keeps 75 %); 1–2 seeded anomalies a shift (printer swarm jams a search, a floor eats unbanked junk, a server room hides a pile) contained for rare junk including Anomaly Shard; Lab Bench junk-to-gear recipes (Lab Coat, Safety Goggles, Burrow Lantern); Lab Treat (every dino accepts it, 1.5x bond); Calming Mist (settles a hostile dino for 10 s, blocks pack calls) | GFGame.Lab.*, GFGame.Net.Lab |

Not yet: hub map and mode maps, pawns/visuals for the modes, packaging.

## Layout

- `Source/`: C++ modules. GFCore (items, inventory, recipes, mini-game gates), GFVoxel (100 cm block grid, DT_Blocks, save chunks), GFBuild (crafting), GFGarden (plots, growth stages), GFCompanion (dinos, diet/taming, aggro, calming), GFFashion, GFMoba, GFRhythm (Beat Hop beat clock, WASAPI loopback "Listen to" output device; no mic), GFSave (versioned save game), GFNet (player/game state, ledger, mode requests), GFArena, GFDepths, GFMine, GFLab (phase 2 modes), GFGiant (giant dino event), GFSocial (safe-zone powers, jail, kick/ban, emotes), GFGame (game mode, character, controller), GFTests (editor-only automation tests).
- `Data/CSV/`: source of truth for DataTables (DT_Blocks, DT_Items, DT_Recipes, DT_Plants, DT_DinoDiet, plus mode tables such as DT_MineRooms, DT_MineUpgrades, DT_LabJunk, DT_LabAnomalies). Import with `Tools/import_datatables.py`.
- `docs/ASSET_BRIEFS.md`: art briefs; `docs/LicenseLedger.csv`: asset license log.
- `handoff/`: Blender to Unreal art handoff (inbox, imported, rejected, style). Not tracked by git.
- `*.uasset` and `*.umap` are stored in Git LFS (see `.gitattributes`).

## Build and test

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" GFGameEditor Win64 Development -Project="%CD%\GFGame.uproject" -WaitMutex
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" GFGame.uproject -ExecCmds="Automation RunTests GFGame;Quit" -unattended -nullrhi -nosplash
```
