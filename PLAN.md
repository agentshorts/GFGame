# GFGame: Build Plan (working codename **GFGame**)

GFGame is a **cozy co-op dino builder** (original IP) built in Unreal Engine 5.8.
Plan written Oct 29, 2026 for the Unreal, art and coding contributors. All C++ is reviewed before merge.

**Original IP only.** Other games are mood references for this doc only. No other game's names, creature designs, UI, sounds, logos or assets go into the game, its file names, or the prompts used to generate art ("dino", "runway", "block house" are fine; "<other game>-style X" in a generator prompt is not).

---

## 2. Core loop and economy

**Loop:** Home hub (build, garden, craft) → see what you need for the next furniture or decoration → pick a mini-game → earn materials and cosmetic unlocks → craft and place → garden grows while you play → repeat.

Design pillars: cozy, no fail-states in the hub, every mini-game session 3–8 min, the player should always have something to place.

### Materials table (all names are placeholders; players can rename them later)

| Source | Primary yield | Secondary / rare | Used for |
|---|---|---|---|
| **Garden (hub)** | Flowers, Fruit, Fiber (cotton-like plant), Seeds | Rare bloom (1 in 10 harvests) | Dyes, dino treats, fabric, decor pots |
| **Hub foraging** (M1 placeholder source) | Wood, Stone, Clay | — | Basic blocks and furniture. Stays as a trickle so players are never stuck |
| **A. Dino taming** | Scales, Bone, Amber, Dino Eggs (cosmetic) | Fossil (rare) | Sturdy furniture, lamps (amber), garden fences, dino beds |
| **B. Mini-MOBA** | Crystal, Metal, Trophy Tokens | Banner patterns | Lights, glass blocks, metal furniture, trophy display decor |
| **C. Fashion show** | Fabric, Ribbon, Glitter, Fans (score-based) | Outfit unlocks | Curtains, rugs, beds, wallpaper, wardrobe; new outfit pieces |
| **D. Beat Hop** (rhythm obby, M5) | Glow Dust, Prism Shards (by score and combo) | Gold Note (rare: full combo or all-Perfect section), song unlocks | Glowing and coloured-glass blocks, garden path lights, string lights, party and disco decor, the **Jukebox** (plays unlocked songs in the hub) |

Cross-links (so every mode feeds the others):
- **Dino treats** (crafted from garden Fruit) raise taming chance → garden feeds A.
- **Dyes** (from Flowers) recolor outfits and blocks → garden feeds C and the builder.
- **Tamed dinos** can be assigned as MOBA allies (one companion slot) and as "models" walking the runway → A feeds B and C.
- **Beat Hop** songs the player unlocks play on the hub **Jukebox**, and Prism Shards make the coloured glass and lights that make builds and gardens glow at night → D feeds the builder. Dyes (from the garden) recolour the Beat Hop outfit trail → garden feeds D.
- Furniture placed at home gives small passive bonuses (e.g. a "Wardrobe" unlocks more outfit slots; a "Dino Bed" lets one more dino follow) → builder feeds back.

### Sinks
Crafting recipes (main sink), block crafting (bulk Wood/Stone/Clay/Glass), seeds, dino treats, outfit dyes, house-plot expansions (one-time, big costs), cosmetic dino accessories. No selling, no currency inflation needed. One soft currency at most ("Hearts" from fashion Fans) only if open question Q8 asks for a shop.

### Pacing targets (tune in DataTables, not code)
- First furniture placed within **5 min** of starting.
- One mini-game run ≈ enough materials for **2–3 crafts**.
- Garden growth 5–20 min real time while in-game (not real-world clock unless Q9 says yes).
- Plot expansions roughly every 60–90 min of play.
- Total content target: **6–10 hours** of things to make, then open-ended building.

---

## 3. Milestones (smallest playable first)

Each milestone ends with a runnable build, all acceptance tests green, C++ review done, and a git tag (`m1`, `m2`…). Sizes are rough agent-days and assume daily reviews.

### M0 – Project skeleton (0.5–1 day)
- New C++ project `GFGame` (UE 5.8.4), git + LFS, `.gitignore`, Enhanced Input, Gameplay Tags, Common UI and StateTree plugins enabled.
- **Fix the toolchain first.** The Oct 29 tool check found UBT bans the installed MSVC 14.4x for game-target builds. A C++ game needs that solved: install the exact MSVC version UE 5.8.4's release notes require, and get `Build.bat GFGameEditor Win64 Development` and a packaged Development build passing.
- Unreal MCP plugin enabled (Editor-only), automation test plugin enabled.
- **Accept:** clean build of editor and game targets; `Automation RunTests GFGame.Smoke` passes (empty map loads, player spawns); packaged exe launches to the hub map.

### M1 – Hub builder + placeholder material source (4–6 days) ← first playable build
Scope: third-person character on a home plot (64×64 blocks wide, 32 high), block placement/removal with a hotbar, ~12 block types, ghost preview, undo last 20 actions; inventory; 5–8 furniture items placed on a grid with rotation; crafting bench UI; garden plots (plant → grow stages → harvest) for 3 plants; foraging nodes (trees, rocks, clay) that respawn = the placeholder source; save/load (§7).
**Accept (automated unless noted):**
- `GFGame.Voxel.SetGet`: set/get block at 1,000 random coords matches; out-of-bounds rejected.
- `GFGame.Voxel.Mesh`: a filled 16³ chunk produces only exterior faces (face count = 6×16²); removing one block updates within one frame.
- `GFGame.Craft.Recipe`: with inputs → output added, inputs removed; without inputs → fails and inventory unchanged.
- `GFGame.Garden.Grow`: planted crop reaches Harvestable after its DataTable time (simulated clock); harvest yields the table's range.
- `GFGame.Save.RoundTrip`: build 500 random blocks + 10 furniture + 3 crops + inventory → save → clear world → load → identical hash.
- `GFGame.Perf.FullPlot` (functional test): plot 50% filled, average frame ≤ 8 ms on the target mid-range GPU at 1080p in a Development build; peak RAM < 3 GB.
- Manual: a playtester builds a small house with a door, bed and garden in under 15 min without help.

### M2 – Dino taming (5–7 days)
Scope: separate "Valley" map reached from a hub portal/door. 3 dino species (one small herbivore, one mid, one large) with Blender rigs. Taming = approach calmly (stealth meter) → offer treats → a short rhythm/trust mini-game → trust fills → tamed. No capture items. Tamed dinos follow (StateTree: Idle/Follow/Fight/Rest), fight wild aggressive dinos with 1–2 abilities each, and can be stationed at home (wander in the garden). Party of up to 3 followers. Yields per §2.
**Accept:**
- `GFGame.Tame.TrustMath`: trust gain/decay matches the DataTable for 3 species; a favorite treat gives the configured multiplier.
- `GFGame.Tame.StateFlow`: Wild → Wary → Trusting → Tamed transitions only on valid events; a fled dino resets.
- `GFGame.Companion.Follow` (functional): a follower stays within 3–8 m of the player over a scripted 60 s path with no stuck time > 2 s on the test navmesh.
- `GFGame.Companion.Fight` (functional): 2 followers vs 1 hostile dummy → dummy dies within the configured time; followers never target the player.
- `GFGame.Save.Companions`: roster, names, levels and which ones are home survive save/load.
- Manual: tame all 3 species in one 10-min session.

### M3 – Fashion show (4–6 days)
Scope: dressing room with a modular character (body + slots: hair, top, bottom, shoes, accessory, plus dye colors); 30–40 original pieces; themed rounds ("Garden Party", "Dino Explorer"…) drawn from a DataTable; 60–90 s to dress, then a runway walk (Level Sequence + camera), then scoring by 3–5 AI judges using tags (theme match, color harmony, completeness, novelty vs last outfits) with a small random spread. Optional: a tamed dino walks with the player. Rewards per §2 by score tier.
**Accept:**
- `GFGame.Fashion.Score`: fixed outfits produce expected tier against each theme (golden-file test of 10 outfits × 3 themes); same input + seed → same score.
- `GFGame.Fashion.Equip`: every piece in `DT_Outfits` equips without missing-bone/material warnings (iterates all rows).
- `GFGame.Fashion.Rewards`: tier → yield matches `DT_FashionRewards`.
- Manual: one full round start → runway → judges → rewards in < 3 min, no camera clipping.

### M4 – Mini-MOBA vs bots (6–9 days, hard-capped)
Scope, deliberately small: **1 lane, 3v3** (player + up to 2 tamed dinos as allies, vs 3 bot dinos/heroes), 2 towers + 1 core per side, minion waves every 30 s, 3 abilities + 1 ultimate for the player, matches 6–8 min, simple bot AI (push lane, retreat at low HP, focus lowest-HP target, defend tower). No jungle, no items shop, no multiplayer. Difficulty: Easy/Normal via DataTable.
**Accept:**
- `GFGame.Moba.Waves`: waves spawn on the configured timer; minions path the lane end to end on the test map.
- `GFGame.Moba.TowerAggro`: tower targets minions first, switches to a hero that damages an allied hero.
- `GFGame.Moba.BotSim` (headless, 20 matches bot-vs-bot at 4× time dilation): 100% of matches end (core destroyed) within 12 sim-minutes; neither side wins > 70%.
- `GFGame.Moba.Rewards`: win/loss yields per `DT_MobaRewards`.
- Manual: a playtester wins on Easy first try and loses at least once on Normal.
- **Cut line:** if M4 passes 9 days, ship with Easy only and 2 abilities; if still blocked, replace with a "tower defense with dinos" mode reusing M2 combat.

### M5 – Beat Hop: rhythm obby (10–13 days: 5–7 built-in songs mode + 5–6 live mode)
A rhythm game you play with your feet: an obby-style voxel course where you run and bunny-hop, and coloured bricks light up on the beat like notes on a note highway. Original design (mood refs: rhythm games with lanes, obby/bhop courses); no names, UI, songs or charts from any existing game.

**Two modes:**
- **Live mode ("Play to my music")** — the headline mode. Beat Hop listens to whatever is already playing on the player's PC (any app) through **WASAPI loopback** of an output device, tracks the beat live, and builds the course just ahead of the player. Details below.
- **Songs mode** (kept) — a few built-in songs with hand-tuned charts, tighter timing and best-score tables. It doubles as the no-setup option, the silence fallback, and the test harness for the shared movement/judge/track code. Built first.

**Core loop:** pick a song + difficulty at the hub Beat gate → the course loads with the song → run and **bunny-hop** along a voxel track 4–5 lanes wide → ahead of you, bricks **glow up in their lane colour on the beat** (they start dim about 2 beats early, flash fully on their beat) → **land on the lit brick in time** → judged **Perfect / Good / Miss** → hits build a **combo** (multiplier ×1 → ×4) → **misses**: the brick crumbles and you **drop** to a catch-floor/checkpoint (Normal/Hard) or just **lose speed** (Easy and "Cozy" no-fail mode) → song ends → score, grade (S/A/B/C), best combo → rewards. Runs are one song, **2–4 min**.
- **Lanes:** 4 lanes on Easy/Normal, 5 on Hard. Each lane has a colour *and* a glyph (dot, bar, ring, diamond, star) so it reads for colour-blind players.
- **Brick types (chart note types):** Tap (land on it), Hold (a 2–4 brick strip you must stay on until it ends), Gap (no brick: you must be airborne across it), Pad (a launch brick for a big hop over 2+ empty beats), Switch (lane change marked by an arrow glow).
- **Timing windows** (judged on landing time vs the brick's beat, after the player's calibration offset): Songs mode Perfect ±50 ms, Good ±110 ms, Miss beyond (Easy widens to ±70/±150). **Live mode is looser** because live beat tracking jitters: Perfect ±80 ms, Good ±160 ms (Easy ±100/±200). Landing on the wrong lane = Miss. All windows live in a DataAsset.
- **Movement:** ground run 600 cm/s target; bhop keeps speed (jump within 100 ms of landing = no friction applied); air strafing lets you drift lanes mid-air. The track is laid out at the chart's reference speed, so a clean bhop chain keeps you on the beat; a miss or stumble slows you so later bricks pass early → misses compound gently, and a **pace ghost** (soft glow line) shows where "on time" is.

**How a track works (Songs mode; live mode builds the same note types on the fly):**
- **Music (Songs mode only):** the few songs that ship *inside* the game are original or royalty-free/CC0 with a licence that allows use in a game (generated music only on a paid plan with commercial rights), each with a ledger row (§6). **Live mode needs no licensing**: the game ships no music and only listens to what the player plays on their own PC (Q13, resolved).
- **Song row** `DT_RhythmSongs`: Id, Title/Artist, SoundWave, BPM (or tempo map), first-beat offset (ms), time signature, preview start, unlock tag, licence row id.
- **Charts** are JSON in git, one file per song × difficulty (`Content/Charts/<SongId>_<Easy|Normal|Hard>.json`): `{ "song": "...", "difficulty": "Normal", "bpm": 120, "offset_ms": 85, "lanes": 4, "ref_speed_cms": 600, "notes": [ { "beat": 8.0, "lane": 2, "type": "Tap" }, { "beat": 9.0, "lane": 3, "type": "Hold", "len_beats": 2 }, ... ] }`. An editor utility imports them into `URhythmChart` data assets; contributors edit/diff the JSON.
- **Charting workflow:** a small Python/editor tool proposes a chart from BPM + onset detection, then it's hand-tuned (playtested at 0.5× speed in a debug mode). The course geometry is **generated from the chart** (beat × ref speed = distance along the track, lane = sideways offset), so a new song needs no level art.
- **Difficulty:** Easy (4 lanes, quarter notes, no Gaps, wide windows, no drops), Normal (4 lanes, eighths, Gaps and Pads, drops to checkpoint), Hard (5 lanes, syncopation, Holds + Switches, tighter windows). Plus "Cozy" (any chart, no fail, half rewards). MVP: **3 songs × 3 difficulties**; more songs are just audio + JSON.

**Live mode: listening to the PC's audio**
- **Capture:** WASAPI **loopback** on an output (render) endpoint, shared mode, on a dedicated capture thread (MMCSS "Pro Audio" priority). The audio is downmixed to mono float, resampled to 48 kHz, and pushed into a lock-free ring buffer with the packet's QPC timestamp (`IAudioCaptureClient::GetBuffer`), so analysis knows exactly when each sample was rendered. Audio is processed in memory only: **never recorded, saved or sent anywhere**.
- **Settings → Beat Hop → "Listen to":** a dropdown of **active output devices only** (enumerated with `eRender`, shown by friendly name), with **"System default output" as the default**. With "System default" chosen, it follows default-device changes live (`IMMNotificationClient`). A specific device is saved by endpoint ID; if it disappears, fall back to the system default and show a toast. **There is no microphone or input-device option**: capture endpoints are never enumerated or opened.
- **The game's own sound:** loopback also hears the game. In live mode the game's music is off, hit sounds default to off (a toggle turns on short, quiet clicks), and the detector ignores onsets within 30 ms of a sound the game itself triggered.
- **Live beat and onset tracking** (`FGFBeatTracker`, pure C++, no engine dependencies so it can be tested offline):
  - Onsets: multi-band spectral flux (FFT 1024, hop 256 ≈ 5 ms at 48 kHz) with adaptive thresholds, split into low (kick), mid (snare/vocals) and high (hats) bands.
  - Tempo: autocorrelation/comb-filter bank over the last 6–8 s of the onset envelope (60–200 BPM, with octave-error correction), plus a phase-locked beat predictor that outputs the next beats' times and a **confidence** value.
  - Warm-up: the run shows "Listening…" for 4–6 s until confidence locks, then bricks start.
  - Tempo or song change: when confidence drops or tempo jumps > 8 %, a short bridge of neutral bricks plays while it re-locks.
  - Written in-house, or with a permissively licensed DSP library only (no GPL code), logged in the ledger.
- **Brick generation with a short look-ahead buffer** (`UGFLiveChartGenerator`): the course is generated from **predicted** beats about **1.5–2 s ahead** (2–4 beats; tunable), so bricks appear and start their dim glow before their beat.
  - Lane and brick type come from what the bands are doing. Kick-heavy beats go to centre lanes; snare and accents go to outer lanes or Switches. Sustained loud sections become Holds, and big accents after a quiet bar become Pads.
  - Music repeats, so the onset pattern of the last bar seeds the next bar's pattern.
  - Bricks inside the last 0.5 s are **frozen**: later tempo corrections never move a brick the player is about to land on.
  - Difficulty here sets brick density: Easy is on-beat only, Normal adds off-beats, Hard adds syncopation and 5 lanes.
  - `AGFRhythmTrack` streams the course endlessly, adding HISM instances ahead and recycling them behind.
- **Latency calibration** (Settings → Beat Hop → "Live latency"):
  - The latency the game can measure is compensated automatically: capture-buffer delay from the QPC timestamps plus the analysis hop.
  - What it can't measure is set per output device (saved with the endpoint ID): speaker or Bluetooth delay (often 150–250 ms) and display lag. There's a **±400 ms slider** and a **tap-along test** (tap Jump on the beat of whatever is playing for 16 beats, and the median offset from the detected beats becomes the setting).
  - Songs mode keeps its own separate offset.
- **Silence and low-confidence fallback:**
  - If the input is below about −50 dBFS for > 1.5 s, or beat confidence stays low for > 4 s, the run **coasts on the last tempo** for one bar.
  - Then it **pauses judging**: no misses, no drops, bricks go neutral and "Play some music! 🎵" shows. Optionally, a soft built-in metronome loop keeps the bricks pulsing at the last tempo (100 BPM if there never was one).
  - It resumes automatically once ≥ 4 s of confident beats come back.
  - If nothing plays for 10 s at the start, it offers **"Play a built-in song instead"** (Songs mode).
- **Rewards in live mode:** same `DT_RhythmRewards` tiers, scaled by minutes actually judged (silence doesn't count), at a ×0.8 multiplier since there's no fixed chart. Best scores are kept per mode; live runs have no per-song leaderboard.

**Rewards** (`DT_RhythmRewards`): by grade and best combo → Glow Dust + Prism Shards; Gold Note on full combo or an all-Perfect section; first clear of each song unlocks it on the hub Jukebox. Tuned so one run ≈ 2–3 crafts (§2 pacing).

**Unreal approach (C++ first, thin Blueprints):** new module `GFRhythm`.
- `UGFBhopMovementComponent : UCharacterMovementComponent` (C++): custom air acceleration with a per-axis wish-speed cap (classic air-strafe feel, values in a DataAsset), jump buffering (100 ms) + coyote time (80 ms), no friction on the landing frame when a jump is queued (bhop), a speed cap, and a landing event that reports exact landing time and the brick under the feet. Only this map uses it; the hub keeps normal movement.
- `FGFWasapiLoopback` (Windows-only, `#if PLATFORM_WINDOWS`, `Windows/AllowWindowsPlatformTypes.h`, links `ole32`/`avrt`): output-device enumeration, loopback capture thread, ring buffer, device-change notifications. `FGFBeatTracker` and `UGFLiveChartGenerator` as above. A `UGFRhythmSettings` (`UDeveloperSettings`/GameUserSettings) holds the listen device ID, live latency per device, Songs-mode offset and hit-sound toggle.
- `UGFBeatClockSubsystem` (world subsystem): one beat-time interface with two sources. In Songs mode it starts the song through a **Quartz** clock (sample-accurate start), tracks song time from the audio render clock (not frame time), applies the player's **calibration offset** (a tap-along calibration screen in settings), and exposes `GetSongTimeSec()`, `BeatToTime()`, and beat/half-beat events. In live mode the same interface is driven by the beat tracker's predicted beats on the QPC clock, minus the live latency setting, so the judge and the track don't care which mode is running.
- `UGFRhythmJudge` (pure C++, no world): (landing time, lane, chart) → Perfect/Good/Miss, combo, score. Fully unit-tested.
- `AGFRhythmTrack`: builds the course from a `URhythmChart` as HISM instances per brick type (one draw call per type); each brick's glow is a per-instance custom data float driven from the beat clock (dim → lit → hit/miss), so there are no per-brick actors or ticks.
- Hit/miss feedback: Niagara (voxel cube burst in the lane colour, Perfect/Good/Miss pixel text), camera kick on Perfect, crumble animation done by the track (per-instance transform), not physics.
- Blueprints: UI layout (song select, results, combo counter), VFX/SFX hookup, tuning values in DataAssets. No gameplay logic in Blueprints.

**Accept:**
- `GFGame.Rhythm.Judge`: golden test of landing times against windows (Perfect/Good/Miss edges ±1 ms) and combo/score math; same input → same score.
- `GFGame.Rhythm.ChartValidate`: every chart JSON parses, notes sorted, lanes in range, Holds have length, no impossible jumps at ref speed (gap distance ≤ max hop distance).
- `GFGame.Rhythm.ClockDrift` (functional): over a 3-min song the beat clock stays within **±5 ms** of the audio playback position; works after an alt-tab hitch.
- `GFGame.Move.Bhop` (functional): scripted inputs chain 10 hops and keep horizontal speed within ±5 %; air strafe moves one lane (100 cm) within one hop; landing without a queued jump applies friction.
- `GFGame.Rhythm.Rewards`: grade/combo → yield matches `DT_RhythmRewards`.
- `GFGame.Live.BeatTrack` (offline, no audio device): licence-free test clips at known tempos (80–170 BPM, a few genres) fed through `FGFBeatTracker` lock within 6 s, with tempo within ±2 %, ≥ 90 % of beats predicted within ±40 ms, and no octave errors on the golden set.
- `GFGame.Live.Lookahead`: generated bricks always appear ≥ 1.5 s before their beat, and frozen bricks never move after a tempo correction.
- `GFGame.Live.Silence`: silence or noise input → no misses, fallback within 1.5 s, resume within 4 s of music returning; 10 s of silence at the start offers Songs mode.
- `GFGame.Live.Devices` (functional, on a dev PC): only render endpoints are listed (no capture devices), "System default" follows a default-device switch, and a removed device falls back without a crash.
- `GFGame.Live.Latency` (functional): the game plays its own click track to the default output, captures it via loopback, and the measured internal latency is compensated to within ±10 ms.
- Manual, live: with a music app playing on the PC, a playtester plays 3 different songs on Normal; bricks feel on the beat after the tap-along calibration, and pausing the music triggers the fallback cleanly.
- `GFGame.Rhythm.Perf`: course with 600 bricks ≤ 8 ms average frame on the target mid-range GPU at 1080p.
- Manual: a playtester full-combos one Easy chart and finds Normal fair; with the calibration offset set, Perfects feel "on the beat".
- **Cut line:** if it passes 13 days, ship Easy + Normal only with Tap/Gap notes (no Holds/Switches/Pads) in both modes. If live tracking still isn't solid, ship live mode with Easy only (on-beat bricks, widest windows) and Songs mode complete.

### M6 – Polish and delivery (3–5 days)
Title screen (per Q10), tutorial prompts, music/SFX (licensed per §6), settings (volume, graphics preset, key rebinding), achievements/"memory book" of firsts, crash-safe autosave, packaged Shipping build, installer or zipped folder, desktop shortcut, a printed/digital quick-start card.
**Accept:**
- Full playthrough script (manual, ~2 h): new save → each mode → craft 20 items → plot expansion → quit/reload → no blocker bugs, no crashes.
- `GFGame.Smoke.AllMaps`: every map loads and unloads with zero errors in the log.
- Shipping build runs on the target PC (Q3) at ≥ 60 fps on the hub at the chosen preset.
- License ledger complete: every asset row has source + license + proof (§6).

**Order and parallel work:** The Blender artist can model/rig M2 dinos and M3 outfit pieces while agents build M1 code. M5 needs no new rigs (it reuses the player skeleton and brick blocks), so it can be built before M4 if the MOBA slips. Rough total: **~6–7 weeks of agent work** with daily reviews.

---

## 4. Architecture sketch (C++ first, thin Blueprints)

"Sidecar" style: C++ core with thin BP/Data on top.

```
GFGame (primary game module: GameMode, PlayerController, Character, HUD glue)
├─ GFCore          shared types, GameplayTags, UGFInventoryComponent, UGFItemDefs, DataTable row structs, logging
├─ GFVoxel         AGFVoxelWorld, UGFVoxelChunk (16x16x16 uint16 ids), mesher, block raycast/placement, serialization
├─ GFBuild         furniture placement grid, ghost preview, undo stack, crafting bench (UGFCraftingSubsystem)
├─ GFGarden        AGFPlot, growth ticking via a world subsystem (one tick for all crops, not per-actor ticks)
├─ GFCompanion     AGFDino (Character), UGFTamingComponent, StateTree tasks, UGFCompanionRoster (GameInstanceSubsystem)
├─ GFFashion       modular character (Leader Pose / skeletal mesh merge), outfit equip, judge scoring (pure C++, testable)
├─ GFRhythm        UGFBhopMovementComponent, UGFBeatClockSubsystem (Quartz / live), UGFRhythmJudge (pure C++), AGFRhythmTrack (HISM bricks, streamed in live mode), URhythmChart + JSON importer, FGFWasapiLoopback (Win), FGFBeatTracker (pure C++), UGFLiveChartGenerator, UGFRhythmSettings
├─ GFMoba          lane manager, wave spawner, tower, core, abilities (simple custom component, not GAS unless needed), bot StateTree
├─ GFSave          UGFSaveGame, UGFSaveSubsystem, migrations
├─ GFUI            Common UI widgets in C++ base classes; layout/styling in UMG Blueprints
└─ GFTests         Automation specs + functional test maps (Editor-only module)
```
Rules for agents:
- Logic in C++; Blueprints only for layout, VFX/SFX hookups, and DataTable-driven tuning. No gameplay logic in Level Blueprints.
- Every system exposes pure functions (scoring, trust math, recipes, save encoding) so tests don't need a world.
- Cross-mode talk goes through `UGFInventoryComponent` and `UGFCompanionRoster` only; mini-game maps never reference hub classes directly.
- One PR per feature, < ~600 changed lines, with tests in the same PR, so reviews stay manageable.
- Mini-games load as separate maps (`L_Hub`, `L_Valley`, `L_Runway`, `L_Arena`, `L_Beat`); the hub is saved before each transition.

---

## 5. Data-driven design (DataTables, CSV/JSON in git)

| Table | Row struct (key fields) |
|---|---|
| `DT_Items` | Id, DisplayName, Icon, Category (Material/Furniture/Seed/Treat/Outfit/Block), StackMax, Rarity |
| `DT_Blocks` | Id, Material, Hardness, CraftCost, Placeable flags, Sound set |
| `DT_Furniture` | Id, Mesh, Footprint (x,y,z), Rotatable, PassiveBonus tag |
| `DT_Recipes` | Id, Inputs[(Item,Qty)], Output(Item,Qty), Station, UnlockTag |
| `DT_Plants` | Id, Seed item, Stage meshes[], StageSeconds[], Yield min/max, Rare chance |
| `DT_Dinos` | Id, Mesh/AnimBP, Temperament, Favorite treats, Trust gain/decay, Stats, Abilities[], Drop table |
| `DT_DropTables` | Source, Item, Weight, Min/Max |
| `DT_Outfits` | Id, Slot, Mesh, Tags (theme/style/color family), Dyeable, Unlock source |
| `DT_FashionThemes` | Id, Title, Wanted tags + weights, Banned tags |
| `DT_FashionRewards` / `DT_MobaRewards` / `DT_RhythmRewards` | Tier/Result (Beat Hop: grade + best combo), Items |
| `DT_RhythmSongs` | Id, Title/Artist, SoundWave, BPM/tempo map, Offset ms, Time signature, Preview start, Unlock tag, Licence row |
| Charts (JSON, not a DataTable) | `Content/Charts/<SongId>_<Difficulty>.json` → `URhythmChart` assets: bpm, offset_ms, lanes, ref_speed_cms, notes[beat, lane, type, len_beats] |
| `DT_MobaUnits` | Id, HP, Damage, Range, Speed, Abilities, Bot behavior profile |
| `DT_Expansions` | Step, Plot size, Cost |

Tables are edited as CSV in git (agents diff them easily) and reimported via an editor utility. A `GFGame.Data.Validate` test checks every reference (items in recipes exist, meshes load, no duplicate ids).

---

## 6. Art, audio and licensing

**Style:** decided by Q1. A stylized, low-poly/toon look is cheapest and fits voxels; avoid realism.

**Sources, in order of preference:**
1. **Blender (own work):** dinos (rig, Idle/Walk/Run/Attack/Happy/Eat), furniture kit, outfit pieces on one shared character skeleton. Fully owned.
2. **AI 3D generators, paid plan only,** as base meshes that the Blender artist cleans, retopos and rigs:
   - Use only generators whose paid plan grants ownership and commercial use of the output; check and save the terms on the day of first use. Free plans often keep the rights or require attribution, so **never ship free-plan output.**
   - Prompts and reference images must be original (no franchise names, no screenshots of other games). Save the prompt, reference, plan tier and date for every generation.
3. **Asset stores:** check each listing's license at purchase. Standard License = OK for use in a game. Personal License blocks public release, so prefer Standard. Listings marked for Unreal only must stay in Unreal. Check AI-generated disclosure on listings. Free content from the engine's own store is fine inside Unreal.
4. **Audio:** music/SFX from royalty-free libraries with a game license (or generated on a paid plan with commercial rights), each logged.
5. **Fonts:** OFL-licensed fonts only.

**License ledger:** `docs/LicenseLedger.csv`. Columns: `asset_path, source (own work/AI generator/asset store/other), url_or_job_id, creator, license, plan_tier_at_generation, date_ct, prompt_or_ref_file, attribution_needed, proof_file (receipt/terms snapshot PDF), notes`. Rule: no asset is merged without a ledger row; `GFGame.Data.LedgerCheck` fails if any `/Game/Art/**` asset has no row. Save a PDF of each generator's terms on the day of first use.

Keep everything clean so the project can be shown, devlogged or released.

---

## 7. Save system

- `UGFSaveGame : USaveGame` with `int32 SaveVersion`, `FDateTime SavedAt`, `FString SlotDisplayName`, playtime.
- **3 manual slots + 1 autosave** (autosave every 5 min, on map change, and on quit). Write to a temp slot, then swap, so a crash mid-write can't corrupt the save. Keep the previous autosave as `.bak`.
- **Contents:** inventory; unlocked recipes/outfits (gameplay tags); furniture list (id, transform, color); garden plots (plant id, stage, elapsed seconds); companion roster (species, name, level, accessories, home/party); fashion history (last N outfits for novelty); expansions; settings separately in `GameUserSettings`.
- **Voxel chunks:** save only modified chunks. Per save, a **palette** (`TArray<FName>` block ids → uint16 index) so DataTable reordering never breaks old saves. Each chunk = coord (`FIntVector`) + RLE-encoded uint16 array (fallback: raw + zlib via `FCompression`). A 64×64×32 plot is ≤ 32 chunks, typically < 100 KB saved.
- **Versioning:** `SaveVersion` enum; `UGFSaveSubsystem::Migrate(int From)` steps through migrations one at a time; unknown block/item ids load as a "Mystery Crate" item instead of failing. Test: `GFGame.Save.Migration` loads committed golden saves from every earlier version.
- Saves live in the standard `Saved/SaveGames/`; document where so players can back them up.

---

## 8. Biggest risks and mitigations

| Risk | Why | Mitigation |
|---|---|---|
| **MOBA scope (biggest)** | MOBAs are huge (AI, balance, abilities, map) | 1 lane, 3v3, bots only, 6–8 min matches, hard 9-day cap with a fallback mode (§3 M4). Build it last so the game is complete without it |
| **Voxel performance** | Naive per-block actors/meshes die fast | Chunked data + one mesh per chunk (greedy meshing into a dynamic/procedural mesh), or HISM per block type per chunk as the simple first version; rebuild only dirty chunks; small plot size; perf test in M1 |
| **AI-agent C++ quality** | Crashes, UPROPERTY/GC mistakes, tangled modules | Small PRs, tests required, `-WarningsAsErrors` on GFGame modules, check `UPROPERTY` on all UObject pointers, every PR reviewed, no raw `new` of UObjects, no ticks without reason |
| Toolchain | UBT rejected the installed MSVC 14.4x for game targets in the Oct 29 check | Fix in M0 before any feature work |
| Art consistency | Mixed sources look patchy | One style sheet, one shared skeleton, the Blender artist does final materials on everything |
| Taming feels like a clone | Genre is crowded | Trust/rhythm mechanic and dinos living in your garden, not capture devices |
| Scope creep from "one more feature" | Scope grows | Lock milestone scope; extras go to a `LATER.md` |
| **Beat Hop audio sync** | Frame-time clocks drift; audio/display latency differs per PC; bhop speed varies so bricks can arrive off-beat | Quartz-started song + audio-clock song time, calibration screen, judge on landing timestamp not frame, pace ghost + generous Easy windows, `ClockDrift` test |
| **Live beat tracking** (new biggest Beat Hop risk) | Real songs have tempo drift, breakdowns, rubato and quiet intros; Bluetooth adds unknown delay; loopback also hears the game | Prediction + 1.5–2 s look-ahead with frozen near bricks, confidence-gated warm-up and re-lock, looser live windows, silence fallback, per-device latency calibration, game audio off in live mode, offline golden-clip tests, Songs mode as a fallback, live-Easy-only cut line |
| Music licensing | Only Songs-mode tracks ship inside the game | Original/royalty-free only for built-in songs, with a ledger row per song (§6). Live mode ships no music, so nothing to licence |
| GPU contention | One GPU shared by Blender, generators and UE | Schedule Blender renders and UE builds apart |

---

## 9. Open questions

1. **Art style:** cute/chibi, toon low-poly, cozy pastel, or something else? Any game or show to use as a mood reference for the look (reference only)?
2. **Favorite dinos** (real species are fine: e.g. triceratops, stegosaurus, parasaurolophus) and should they be realistic, cute, or fantasy-colored?
3. **Platform:** Windows PC: minimum GPU/specs? Controller or mouse/keyboard? (Console or Mac would change the plan a lot.)
4. **Deadline:** the target date drives which milestones make it in.
5. **Single-player only?** Or co-op with friends (co-op would add a lot of work)?
6. **Names:** the game's title, the default character name, home/valley names, and can players name each dino?
7. **Fashion themes** to include (e.g. cottagecore, streetwear, goth, Y2K, formal, beach) and any to avoid.
8. Should there be a shop/currency, or only crafting?
9. Should gardens grow in **real time** while the game is closed (like a pet) or only while playing?
10. **Easter eggs:** a message on the title screen, hidden extras, a photo frame furniture item?
11. How competitive should the MOBA feel — easy and cute, or a real challenge? Is the MOBA a must-have or a nice-to-have?
12. ~~**Beat Hop music:** what music, and should we make a track?~~ **Resolved (Oct 29):** Beat Hop's live mode syncs to whatever the player is playing on their PC via output-device loopback, so licensing isn't an issue and the player's own music is the soundtrack. Built-in Songs mode keeps a few licence-free tracks. (Keyboard vs controller for hopping folds into Q3.)

---

## 10. Next steps
1. Answer Q1–Q4 (style, dinos, platform, deadline) — blocks art and the schedule.
2. M0 (new repo, toolchain fix, smoke test).
3. Art: style sheet + shared character skeleton + 1 test dino once Q1/Q2 are answered.
4. Pick an AI 3D generator (paid plan) only if the artist needs base meshes; save terms PDFs and start the ledger.
