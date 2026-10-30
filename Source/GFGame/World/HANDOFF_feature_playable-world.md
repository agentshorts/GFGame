# HANDOFF feature/playable-world

Empty `L_Hub` gets a sky and a floor at play time. No map or asset edits. The voxel plot is unchanged (64x64x32, ground top at Z=0).

## What changed

`AGFEnvironmentDirector` (`Source/GFGame/World/`) runs on BeginPlay and adds whatever the level is missing:

- a directional light (pitch -48, yaw 25), marked as the atmosphere sun
- a sky atmosphere component
- a movable sky light with real-time capture
- an exponential height fog (density 0.005, volumetric fog off)

`AGFOverworldGround` reads `UGFTerritorySubsystem` faction layout and hazards (the live subsystem, or the class default if the subsystem is missing) and builds:

- one slab under the whole layout, expanded by `Margin`
- one flat cylinder per faction whose largest `RadiusByMembers` entry is above zero, tinted by faction id
- one box per hazard, using that hazard's `HalfExtent` (LavaBridge orange, DeadlyForest dark green)

Meshes are engine basic shapes from `FGFAssetMap::PlaceholderMesh` / `MakeTint`, collision `BlockAll`. Disc radius is the maximum value in `RadiusByMembers`, not the current member count, so the rings show full land even at 0 members. An empty radius table gets no disc; the slab still covers the anchor. The shape list is `AGFOverworldGround::BuildShapes` (no world needed).

Both actors are registered with `+StartupDirectorClasses` in their own block at the end of `Config/DefaultGame.ini`. `AGFGameMode` is unchanged. A second actor of the same class is not spawned if one is already in the level.

## Decision

[#92](https://github.com/agentshorts/GFGame/issues/92). `InitGameState` only runs on the server, so both actors set `bReplicates` and `bAlwaysRelevant`. Each machine builds its own lights and meshes in BeginPlay. Those pieces are not replicated.

Volumetric clouds are not spawned. A default cloud layer is a tall raymarch and is not cheap next to the voxel plot.

The sun stays at 10 lux so the sky atmosphere stays in its usual range. `r.DefaultFeature.AutoExposure` is false, so when this director creates the sun it also adds an unbound post-process exposure bias (default 11). A level that already has a directional light does not get that post process.

The voxel plot stays 64x64x32. The slab is the floor outside it, not a larger buildable area. Discs and hazard boxes sit `OverlayLift` (1 cm) above the slab. Tops are clamped to at most -0.5 cm so they stay under the voxel top at Z=0.

## Config keys

`[/Script/GFGame.GFGameMode]`

- `+StartupDirectorClasses=/Script/GFGame.GFEnvironmentDirector`
- `+StartupDirectorClasses=/Script/GFGame.GFOverworldGround`

`[/Script/GFGame.GFEnvironmentDirector]`

- `SunIntensity` (default 10)
- `ExposureBias` (default 11, applied only when this director creates the sun)

`[/Script/GFGame.GFOverworldGround]`

- `Margin` (default 2000)
- `Thickness` (default 100)
- `SurfaceZ` (default -2, top of the slab)
- `OverlayLift` (default 1)
- `SlabColor`
- `FactionColours` by faction id (Rime pale ice blue, Verdant green, Cinderwrights ash grey)
- `HazardColours` by hazard id (LavaBridge orange, DeadlyForest dark green)

Unknown faction ids fall back to those three colours, then neutral grey. Hazard colour matches id, then kind.

## Tests

- `GFGame.World.OverworldCoversLayout` — every faction centre and hazard centre has ground, from `BuildShapes`, including the shipped layout. No world.
- `GFGame.World.EnvironmentSkipsPlaced` — a world that already has a directional light does not get a second one. Missing sky atmosphere, sky light, and height fog are added once.

## Dev PC steps

- Optional: open `L_Hub` and press Play. Hand-placed lighting later overrides the director automatically (a directional light, sky atmosphere, sky light, or height fog that is already in the level is left alone).
- If the picture is dark or blown out, change `ExposureBias` or `SunIntensity` under `[/Script/GFGame.GFEnvironmentDirector]`.
- Walk off the voxel plot (about 32 m from the origin) toward Verdant (X=-4400) and Cinderwrights (X=7200). There should be a floor, with a tinted disc on each camp and a coloured box on LavaBridge and DeadlyForest.
- Run `GFGame.World.OverworldCoversLayout` and `GFGame.World.EnvironmentSkipsPlaced`.

## Follow-ups

- A hand-authored sky in `L_Hub` will replace the generated one piece by piece.
- Volumetric clouds, if a cheap setting is wanted later.
- The ground actor should stay at scale 1. Shape positions are world positions parented to it.
