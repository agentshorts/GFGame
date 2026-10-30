# HANDOFF feature/territory-layout

Territory changes stay in `Source/GFGame/Territory/` plus the `[/Script/GFGame.GFTerritorySubsystem]` section of `Config/DefaultGame.ini`. `GFGame.Build.cs` is unchanged. Safety query signatures are unchanged.

## What changed

`FGFTerritoryFaction::RadiusByMembers` is a radius for each effective member count. Index 0 is 0 members. A count past the last index uses the last entry. An empty table still uses the shared `Curve`, with `BaseRadius` as the one-member curve radius.

When the table is set, `RegionFor` publishes `Members` as the effective count, `BaseRadius` and `MaxRadius` as the table radius, and `RadiusPerExtraMember` as 0. `FGFSafeRegion::Radius()` is then the table value, and the overlap tie-break still sees the member count. `CircleCacheMatches` compares the table, so a table edit drops the cached discs.

`FGFTerritoryMap::Touches` is the disc-touch query.

`FGFTerritoryHazard` is an axis-aligned box (Z ignored). `Kind` is a config name, not an enum. The subsystem `Hazards` array is copied onto the map. `GetHazardAt` returns the first box that contains the point, and only when `GetZoneAt` is wild, so faction land and the neutral disc both override a hazard. There is no damage. `gf.Territory.DrawHazards` draws the boxes (the vertical extent on screen is visual only).

`Curve` in `DefaultGame.ini` stays the empty-table fallback (1200 + 400 per member, cap 2800). The three anchors are per-faction tables instead of one shared ring.

## Touch math

`Touches(A, B)` is true only when both factions are on the map, both radii are greater than 0, and the horizontal distance between centers is less than or equal to `rA + rB`.

Centers: Rime `(0, 0)`, Verdant `(-4400, 0)`, Cinderwrights `(7200, 0)`.

- Rime to Verdant is 4400. Radii at 3 are 2000 and 1800 (sum 3800, a 600 cm gap). A 4 and a 1 are 3400+1400=4800 or 1600+3000=4600, so those pairs touch. Any pair whose larger count is below 4 stays apart. A count of 0 has radius 0 and does not touch.
- Rime to Cinderwrights is 7200. Full radii are 3400 and 3000 (sum 6400, an 800 cm gap). That gap is the lava bridge. They do not touch at any count.
- Verdant to Cinderwrights is 11600. They do not touch.

Rime's radius is larger than each other faction at every count from 1 through 4, so its area is larger at equal counts. Full Rime (radius 3400) is the largest area of any faction at any count on these tables.

## Still editor work

These anchors are layout numbers, not placed world actors. The world map still needs real anchor positions, and hazard art placed in the editor (lava bridge between Rime and Cinderwrights, deadly forest between Rime and Verdant).

## Tests

- `GFGame.Territory.Layout.RimeVerdant`
- `GFGame.Territory.Layout.CinderAlone`
- `GFGame.Territory.Layout.RimeMostLand`
- `GFGame.Territory.Layout.Table`
- `GFGame.Territory.Layout.Hazards`
