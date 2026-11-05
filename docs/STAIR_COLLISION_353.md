# Stair collision #353: walkable buried treads

Follow-up to #341. The six meshes already switched to complex as simple stay on that list. This pass adds every other mesh on `L_World_Factions_A` where a player can walk onto a tread the live audit found buried inside a simple hull.

Data file: `Data/Town/stair_collision_341_v2.json` (v1 kept). Script: `Tools/fix_stair_collision_341.py`. Gate: `GFGame.Movement.StairKit.TownCollision`.

Live audit source filename: `stair_collision_341_report_audit_20261105_063452.json` (taken after the #341 six were already complex as simple; 57 other meshes still had buried > 0).

## Fix (complex as simple)

Keep the original six unchanged (Blender FBX scan numbers):

- SM_Town_Cinderwright_BoardHall
- SM_Town_Cinderwright_CountingHouse
- SM_Town_Cinderwright_Theatre
- SM_Town_Cinderwright_Smelter
- SM_Town_Cinderwright_Crane
- SM_Town_Caravan_Tent

Add these walkable ones (live-audit heights only, no invented Blender hulls):

- Canal: SM_Town_Canal_Bridge, SM_Town_Canal_Corner
- Town v1 houses: SM_Town_House_Large / Med / Small
- Cinderwright houses: SM_Town_Cinderwright_House_Large / Med / Small
- Rime houses: SM_Town_Rime_House_Large / Med / Small
- SM_Town_Cinderwright_Factory
- SM_Town_Rime_GlacierWaterWorks
- SM_Town_Fountain
- Town v1 stalls: Coilback, Emberhare, Frosthorn, GlacierWater
- Cinderwright stalls: Coilback, Emberhare, Frosthorn, GlacierWater

Towers (Rime_Tower_Watch, Tower_Spire) are not on the fix list: the buried height looks like an interior footing, same class as wall footings. Recheck if a playtest cannot climb them.

## Leave on simple collision (audit_only)

Existing #341 audit_only stays (SovereignCourt, StarterHouses x3, Glasshouse, Wall_*, Rime_Wall_*, PowerPylon x2, SnowPine_A/B).

New leaves:

- Trees: Cinderwright_AshTree_A/B, ForestTree_A/B/C (root flare; same rule as snow pines)
- Homestead fence Gate / GateLeaf / Post / Straight (fence plinths)
- SM_Town_LampPost (lamp foot)
- SM_Town_Cinderwright_CharcoalRock_A (rock)
- Cinderwright Pipe_Straight / Pipe_Support (pipe bodies)
- CaravanCart, Caravan_CoveredWagon, CaravanPen, Caravan_Campfire, Caravan_NoticeBoard (props / wagons / pens)
- SM_Town_Bench (seat box)
- SM_Town_Planter (decorative rim)
- SM_Town_Rime_Tower_Watch, SM_Town_Tower_Spire (likely interior footing)

## Out of scope

Blender FBX rebuild. MaxStepHeight / riser assist. Sovereign Court UCX.

## Apply note

Keep the existing six applied uassets. `GFSTAIR_MODE=apply` only edits fix-list meshes that are not already complex as simple, so the new entries flip and the six are skipped as already set.
