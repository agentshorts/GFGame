# HANDOFF: feature/grumble-zone-fixes

Base: 3f379ba (Giant shape zone follows all faction land). Local branch, not pushed.

## What changed

### Respawns no longer land on hazards
- `UGFGiantEventComponent` keeps a replicated `bUseDefaultRegion`. While he uses the
  default region, his own circle only counts when no provider (faction land) has a safe
  area: `FGFSafeZoneUnion::bOwnOnlyWithoutLand`. Regions set with `SetSafeRegions` always
  count. The alternative (clip his circle to owned land) is recorded in the Decision issue.
- `FGFGiantRules::RespawnPoint(Tuning, Zone, From, NoEntry)` only accepts a point that is
  safe, not a hazard (`IGFSafeZoneQuery::IsHazardAt`) and outside every no-entry box. It
  tries the plain inset point, then deeper along the inward normal, then rings around it,
  and falls back to the plain point only if nothing fits.
- Hazards are known even when no faction owns land: `IGFSafeZoneProvider::GetHazardQuery`
  (the territory subsystem returns its map) and `UGFSafeZoneSubsystem::IsHazardAt`.
  `FGFSafeZoneUnion::IsHazardAt` asks its own part and every provider.

### Narrow gaps
- `FGFSafeZoneUnion::FPartsScope` caches the part list for one sim step (`Sim.Begin`,
  `Sim.Step`, the belly respawn). Providers must not register or unregister inside such a
  scope; the scope is kept narrow (not around faint or apply hooks) for that reason.
- `KeepOut` first pushes out from the nearest edges (12 rounds), then uses the old
  costly search only if that fails.
- `StepOutside` is capped at its length: direct step, slide along the edge tangent, then a
  fan of turns (15 degree steps); if nothing is clear he stays put. It only calls `KeepOut`
  when he already stands in the safe area (safety grew under him).
- `FGFGiantSim::MoveToward` caps the final move at `StepLen`, and caches the walk-out goal
  while it stays clear.
- `FGFSafeZoneUnion::ClosestEdge` also offers the crossing point where two parts' borders
  meet (the notch), found by alternating projections.

### Nits
- Stale `GetSafeZoneQuery` comment fixed; it returns null while he has no safe area.
- `GetLandShape()` returns a raw pointer owned by the component (valid while it lives).
- `Circles.Regions` is synced in `SyncCircles` (BeginPlay, `SetSafeRegions`, refresh,
  `OnRep_SafeRegions`), no longer as a side effect of `GetSafeZone()`.

## Tests (GFGame.Social)
- `GiantNarrowGaps`: shipped layout at 3 members per faction, a visitor shuttling through
  the 600 wide gap; no jumps beyond one step, bounded zone calls per tick.
- `GiantRespawnHazard`: no land (lava at x=1850 rejected), owned land, a no-entry box, and
  a full belly event.
- `SafeZoneNotch`: union notch edge, KeepOut through the notch, capped StepOutside between
  two close circles.

## Open items
- GF.FactionProtected skip (`FGFProtectedActor::Is`) is not done: `GFProtectedActor` is not
  in this base. Add the skip once the ferry branch is in staging.
- The costly `KeepOut` fallback can still run when land grows under him and the edge push
  fails; it is no longer reached every tick in gaps.
- In a gap narrower than his clearance he waits at the mouth (stays put or steps back)
  instead of jumping through; there is no path finding around land.
