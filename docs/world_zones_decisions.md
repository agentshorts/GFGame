# World zone decisions

The brief already decided the shape of the overworld. This file records those calls and the smaller ones made while building them. Dates are 2026-10-30 unless the line says otherwise.

## From the brief

1. One overworld, `L_Hub`. Region porches open the existing mini-game gate. Match maps stay separate.
2. Return from a match spawns at that region's Shardstone, not the hub. Hub gates stay shortcuts and are never locked.
3. Playable disc radius 1.2 km. Kindle Ring radius 150 m. Eight porches on a 900 m ring, 45 degrees apart, each 120 m across. Safe core 30 m. Fringe 150–170 m. Deep Badlands 1.05–1.2 km.
4. Bearings run from +X toward +Y. `X = R cos θ`, `Y = R sin θ`.
5. Walk speed stays 5 m/s. Nothing in the disc is more than a 4 minute walk from a Shardstone.
6. Splitcrystal Flats (`Region.Reserved1`) is on hold: pad and spire exist, the arch does not travel.
7. On-hold modes (Push the Cart, Tower Defense, Factory, Old West) are commented rows on an outer ring at 1.05 km. They are not live slots.
8. Highest priority wins. Order: faction land, Kindle Ring, porch safe core, region porch, hazard box, Fringe, Deep Badlands, Badlands. Z is ignored.
9. Danger 0/1/2/3 as in the brief. `IsPvPAllowedAt` is true only at tier 2 and 3. Villain Mode's toggle is task #163. PvP also requires both players to have that toggle on, which #163 will enforce.
10. Rows live in `Data/CSV`, not uassets. `FGFZoneRow` is the row struct.
11. Server resolves each pawn at 5 Hz and replicates `CurrentZoneId`. 2 m hysteresis.
12. Toasts: "Leaving the Kindle Ring: Badlands ahead" and "Home. Kettle's on."
13. Ambience blends for 1.5 s on one unbound post-process volume. No lights, no fog, no sky. `AGFEnvironmentDirector` is not edited.
14. Kindle Shardstone starts unlocked. A region stone unlocks within 12 m, or when that region's match finishes. Hop within 6 m, free, 3 s channel. Damage or Villain Mode cancels it. No hop while Villain Mode is on, or within 8 s of PvP damage. Tamed companions within 20 m come along.
15. A missing match map says "Under construction, check back soon" and does not travel.
16. No World Partition. Editable voxels cover the Kindle Ring only, about 340×340 m including the 20 m margin. The rest is Shatter Flats.
17. Nine spires, rim cliffs, seeded points of interest, and a compass strip.

## Choices made while building (2026-10-30)

- Distances in code and CSV are centimetres. 150 m is `15000`.
- `Zone.Faction` is an extra native tag. A faction hit is `Faction.<Id>`, tier 0, not PvP, Kindle ambience, priority 100. It does not change `IsSafeFor`. Neutral discs are not a zone.
- Hazard boxes are copied from `DefaultGame.ini` (Lava Bridge and Deadly Forest). They sit inside the Kindle Ring, so Kindle wins there. The HUD still prints the old hazard and faction strings when territory says so. `GFFormatZoneLabel` is unchanged.
- Safe cores are real CSV rows (priority 70), not a special case in the resolver.
- `DisplayName` and `AmbientLoop` are strings so the CSV stays editable. An empty loop means silence.
- Save version stays `BlockPaint` (6). `UnlockedShardstones` is a new array on `FGFPlayerProgress`. Old saves load it empty. A stone with `bStartsUnlocked` counts even when the array does not list it.
- `bVillainMode` is a replicated flag with `SetVillainMode`. There is no input for it.
- The porch Arena gate id is `Arena` and its map is `/Game/Maps/L_Crucible`. That is not the Moba gate (`Moba`, `/Game/Maps/L_Arena`). Mine, Depths, and Grumble Dig got new gate subsystems and maps `L_Mine`, `L_Depths`, and `L_GrumbleDig`. The in-world directors stay the hub shortcuts and do not set a return stone.
- Hysteresis: if the point is still inside the current shape, a higher priority zone wins immediately (faction land inside the ring). The 2 m hold applies only once the point is outside. 1 m past stays, 2.1 m past leaves.
- The Badlands circle is also the fallback past the rim, so a point with no containing row still reports `Zone.Badlands`.
- Faction land counts as home for the Kindle toasts. Safe cores do not.
- The channel also cancels if you walk more than 6 m from every unlocked stone.
- Arrivals spread on a 2.5 m ring, one slot every 90 degrees. Slot is the player's index in `PlayerArray`.
- Spawn table ids are data only. `AGFWildDinoDirector` is not wired to them.
- Points of interest use seed `20261030`, a 130 m grid, and ±4 m of jitter, so neighbours stay at least 120 m apart. The stream is consumed even when a candidate is rejected.
- Spires, rim, stones, gates, and the flats slab are always relevant. Points of interest cull at about 250 m. Props do not collide; the rim is a visual cliff. Porch discs do not collide. The Shatter Flats slab does.
- `ContrastTint` drives post-process Color Gain. Saturation, vignette, and a clamped exposure offset are the only other overrides.
- `FGFDataTables` strips full-line `#` comments before the engine CSV parser sees them.
- The compass covers ±100 degrees of view yaw. A marker behind the player pins to the nearer edge. Kindle is always shown. A region icon appears once that stone is unlocked.
- The under-construction line is `LastEnterMessage` on the gate and a land toast from the porch arch.
- The disabled Splitcrystal arch overlaps and does nothing: no toast, no travel, no pending stone.
- `AGFCharacter::TakeDamage` tells the player state. The serial bumps on any positive damage. The PvP clock stamps only when the instigator is another player and `IsPvPAllowedAt` is true.
- Press **F** (not while chat is open) asks for the unlocked stone you are not standing on whose bearing is closest to your view. The controls-hint string is unchanged.
- Deny lines, when a hop is refused: "Shardhop waits until Villain Mode is off.", "Shardhop waits out the fight.", "Stand by an unlocked Shardstone.", "That Shardstone is still dark.", "No such Shardstone."
- The Kindle voxel grid is 340×340×16. Height stays the existing overworld `GridZ` (16), not the old 64-plot's 32, so surface and trunk maths still match. `FGFOverworldBuild::Make()` is not resized. Only `L_Hub` uses `MakeKindleRing`. Wild cells in that stamp are grass on dirt. Faction themes still win on their own discs.
- `PendingArrivalStoneId` lives on a game-instance subsystem. Each player consumes it once. The next porch enter replaces it and arms everyone again, including late joiners.
- A finished match unlocks that region's stone for every player currently on the server.
