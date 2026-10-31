# HANDOFF feature/lab-anomalies-118

Fighting half of Overtime (#118). Short hostile shift events. `AnomalyShard` stays lab-exclusive. No co-op split/regroup loop, no lab map, no damage to players.

## Schedule

`FGFLabRules::ScheduleAnomalies` deals 1 or 2 rows from `DT_LabAnomalies`. Start times are whole seconds in `[30, lockdown)`. The latest start still leaves the slowest `ContainSeconds` before 135 s, so sealing one as it wakes finishes in time. The stream is `seed XOR 0x414E4F4D`, not the pile dealer's stream, so junk piles for a seed did not move. Same seed, same list, chronological.

`Tick` wakes a row when the clock reaches its start and clears `bActive` at lockdown. Effects stop when it is contained.

## Effects and the order

`Contain` (`Ints = [index]`) is a timed order, like `Search`.

- Bad index, or not awake yet: `BadAnomaly`
- Already contained: `Contained`
- Searching or already containing: `Busy`
- A hidden pile: `Search` returns `Hidden`

`RoguePrinterSwarm` drains search work at `1/1.5` while it is live, including a search already going. Containing it puts the rest of that search back to normal speed. `ChairEatingFloor` removes one count from the first `Unbanked` stack every 15 s (the row's `EatInterval`); a stack that hits 0 is removed. `FlickeringServerRoom` covers the first unsearched pile that is not the one being searched.

The containment roll uses the same per-roll stream as a search (`HashCombine(seed, ++Rolls)`), 1.5x in overtime, into `Unbanked`. Extract / lockdown / evacuate are untouched, so evacuate still keeps `floor(75 %)`.

## Greybox

`AGFLabAnomalyMarker` is a replicated engine cube, tinted per kind, offset from the director. The flickering room blinks locally. No collision. Nothing here deals damage.

## Value per minute

Pile value is unchanged: about **56** a shift, about **51** a minute.

Anomaly budget, weighted by the table, one containment at 1x (overtime not included): about **11.7**. A shift averages **1.5** of them, about **18** more value, plus about **6.6 s** of containing.

`ExpectedValuePerMinute` is pile value plus that budget, over pile time plus contain time: about **61** a minute. Hand gathering is 80. The automation log prints the exact figure (`GFGame.Lab.Economy` and `GFGame.Lab.Anomalies`).

The seeded pile check still compares junk-table loot to `ExpectedShiftValue` (within 15 %). It seals anomalies as they wake so a jam or a hidden pile cannot skip a search, then puts `Unbanked` back so the anomaly drop (and a bite during the seal) is not in that mean.

## Tests

This cloud box has no Unreal editor. Build `GFGameEditor Win64 Development` and run `Automation RunTests GFGame` on Ender.

- `GFGame.Lab.Anomalies`: schedule, each effect stops when contained, shard rate against the row (600 seeds, ±0.08), overtime 1.5x, evacuate 75 %.
- `GFGame.Lab.Economy`: per-minute cap still holds with the anomaly budget.
- `GFGame.Net.Lab`: a client (and the friend) sees the anomaly wake and then contained.
