# GFLab

Overtime, the after-hours lab. A shift deals eight junk piles from `DT_LabJunk`. Searching one takes its `SearchSeconds` and rolls into `Unbanked`. After 90 s, loot is 1.5x. At 135 s the doors seal. Extract keeps everything; lockdown and evacuating keep 75 %.

Anomalies (`DT_LabAnomalies`) are the hostile half of a shift. The seed deals 1 or 2. The first is not before 30 s, and none are scheduled at or after lockdown (each one can still be contained before the doors seal if you start as it wakes). Containing one is a timed order, same busy rule as a search. The drop, including Anomaly Shard, lands in `Unbanked`, so extract / lockdown / evacuate apply unchanged. Anomaly Shard stays lab-exclusive.

| Row | While it is live |
|---|---|
| RoguePrinterSwarm | The pile being searched takes 1.5x as long (+50 %). |
| ChairEatingFloor | One item leaves `Unbanked` every 15 s. |
| FlickeringServerRoom | One unsearched pile can't be searched. |

`AGFLabDirector` runs this on the server (`Contain` with the anomaly index). A live anomaly is a tinted engine cube. No art, and it does not damage players.

Rules are pure (`FGFLabRules`). Tests: `GFGame.Lab.*`, `GFGame.Net.Lab`. See `HANDOFF_feature_lab-anomalies-118.md`.
