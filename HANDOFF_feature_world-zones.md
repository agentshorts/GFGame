# World zones handoff

Branch `feature/world-zones`, cut from staging `45fc8d0` ("Voxel overworld stamped from the territory layout on L_Hub"). The PR targets `staging` only. Lighting commit `569e05d` is not in this branch, and `AGFEnvironmentDirector` was not touched. Nothing here is aimed at `main`.

The overworld is one disc. Kindle Ring in the middle, eight porches out at 900 m, Badlands between them, Deep Badlands along the Shatter Rim. Walk a porch arch and the existing mini-game gate runs. Come home and you stand at that porch's Shardstone. Hub shortcuts still do not lock, and they still do not set a return stone.

## What Jimmy runs on Ender

This machine cannot compile Win64. There is no Unreal editor and no `Build.bat` here, so the automation tests have not been executed. On Ender:

```bat
Build.bat GFGameEditor Win64 Development
```

Then, from the editor or the session frontend:

```bat
Automation RunTests GFGame+VoxelFinish
```

The new names are:

- `GFGame.World.Zones.Priority`
- `GFGame.World.Zones.Hysteresis`
- `GFGame.World.Zones.PvPAndRows`
- `GFGame.World.Zones.Layout`
- `GFGame.World.Zones.Shardhop`
- `GFGame.World.Zones.Gates`
- `GFGame.World.Zones.POI`
- `GFGame.World.Zones.Save`
- `GFGame.World.Zones.KindleStamp`

Territory, Overworld, Giant, and Social tests were not edited and should still pass. Please do not merge until that Win64 run is green.

## Where the pieces live

- `Data/CSV/DT_Zones.csv`, `DT_ZoneAmbience.csv`, `DT_Shardstones.csv`. Hash-comment lines are the on-hold outer ring. `FGFDataTables` strips those before the parser.
- `Source/GFGame/Zones/` holds tags, the catalog, the 5 Hz subsystem, Shardhop rules, the travel subsystem, props, stones, arches, the ambience director, and the layout director.
- New gates: `UGFArenaGateSubsystem` (`/Game/Maps/L_Crucible`), `UGFMineGateSubsystem` (`L_Mine`), `UGFDepthsGateSubsystem` (`L_Depths`), `UGFGrumbleDigGateSubsystem` (`L_GrumbleDig`). None of those packages exist yet, so the arches say "Under construction, check back soon" and stay put.
- `L_Hub` stamps a 340×340×16 grass ring via `MakeKindleRing`. `Make()` is still the old plot. Shatter Flats is a non-editable slab out to the rim, with a tinted disc on each porch. `ShouldDrawShape` is unchanged, so the hub still draws none of the old slab, faction disc, or hazard box.
- `F` starts a Shardhop toward the unlocked stone you are looking at, when you are within 6 m of one you already have. Villain Mode has a replicated flag and no toggle. That toggle is still #163.
- Decisions, including the ones made while building, are in `docs/world_zones_decisions.md`.

## Left on the table

World Partition, HLOD, PCG, voxel streaming, audio volumes, the Villain Mode toggle, building the match maps, and any lighting change. Spawn table ids are data only.

— Frank
