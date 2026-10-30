# New dinos: design sheets (v2)

Four new original predators for taming and the Dino Arena. Raptor, Rex and Sailback already exist and are animated, and Nipper and Hornback are planned, so none of these copy their niche. Display names are placeholders, because players name their own dinos. No real-world film or game designs were used.

**v2 changes:** the new arena mechanic tags (`Untargetable`, `Displace`, `Accuracy-`) are **approved**. Level-3 abilities are kept as **optional data**: rows exist, but a species ships with its 2 tame abilities, and slot 3 can be disabled per row without code changes.

| Species | Role (arena) | Size class | One line |
|---|---|---|---|
| **Tusksaw** | assassin | mid (fits doors) | A low, spine-backed digger that burrows and bursts up under the back row |
| **Ripwing** | skirmisher (flyer) | mid body, wide wings (outdoors) | A gliding pterosaur-like hunter that dive-strikes and snatches enemies out of formation |
| **Scythepaw** | bruiser / off-tank | large (outdoors only) | A tall, fluffy-feathered giant with huge sickle hand-claws: scary blades, secret sweet tooth |
| **Lanternmaw** | controller | mid-large (outdoors) | A squat swamp lurker with a glowing lure on its head that reels enemies in and gulps them |

## Shared rules (apply to all four)
- **Build:** chunky micro-voxel (1 mv = 6.25 cm), rigid voxel skinning, 16 px per block of texel density, nearest filtering, and the predator palette (from the asset briefs). Eyes, teeth and claws are the only high-contrast "danger" pixels. Each species gets a `_M` dyeable region (it works with the new paint/Finish system), a wild texture, a rare morph texture (about 1 in 20) and a `_E` eye mask (red when Hostile).
- **Rig:** `SKEL_Dino_<Species>` uses the **shared bone names** (`pelvis, spine_01.., neck_01.., head, jaw, tail_01.., thigh_l, shin_l, foot_l, toe_l, upperarm_l, forearm_l, hand_l`), and species extras are listed per sheet. Bone budgets are mid ≤ 35 and large ≤ 45.
- **Anim:** 30 fps, in place, cozy combat (they **faint** with little stars and never die or bleed). Contact and release frames go in `qa.notes`.
- **Taming** (diet mechanic, `DT_DinoDiet`):
  - Each species accepts several foods. The **favourite fills the bond 3× faster** (its BondGain is 3× the typical accepted food), and offering it to an angry dino calms it instantly.
  - **Approaching inside WarnRadius without an accepted food in the selected hotbar slot triggers Threat (about 2 s). If the player is still inside AggroRadius after that, it turns Hostile and attacks.**
  - Crouching shrinks the radii by 30 %. Dino Treat is accepted weakly by everyone.
  - All foods come from the garden, fishing, foraging or the Cookpot, and every favourite can be obtained without the lane-battle mode.
- **Economy:** materials are the only currency. Taming foods, Cookpot recipes, arena wagers and rewards are all priced in materials (Scales, Bone, Amber, Fossil and so on). There are no coins anywhere.
- **Arena** (team auto-battle, up to 3 dinos on 6 spots): every new dino gets **2 abilities at tame**, plus an **optional** 3rd at dino level 3 (optional data in `DT_DinoAbilities`, which allows 2–3). Cooldowns are starting numbers in real seconds, and the statuses reuse existing ones (Stun, Root, Slow, Wet, Burn, Shred, Taunt, Knockback, Knock-up).
- **Base clip set for every species:** Idle (60 f loop), Walk (loop), Run (loop), Attack_Bite (15 f, contact f7), Hit (10 f), Faint (36 f, hold), Rest loop (60 f), plus the **taming set**: Sniff (30), Eat (30, gulp f10), Happy (36), Threat (30, loop), Refuse (24), Tamed (48, hold the last frame). Also the **arena shared set**: Stunned loop (24) and Cheer (30, hold). Each sheet lists only the extra or species-specific notes on top of this.

---

## 1. Tusksaw: spined burrow ambusher

**Silhouette (chunky voxel):** low and wide, like a battering ram that digs. It has a **wedge-shaped shovel snout** with two short forward **tusks** (bone colour) jutting from the lower jaw, a short thick neck, and a body that sits close to the ground on four splayed, heavy-clawed digging limbs (front claws are 3 mv long). Its back carries a **double row of stepped voxel spines** (5 per row, tallest over the shoulders, ochre tips on a rust base), which reads as a saw-blade from the side. It has a short, flat, paddle-like tail. Its small eyes (eye gold) sit high on the head so they peek out while it's burrowed. Colours are rust and charcoal with ochre spine tips and dust-coloured belly scutes, and the wild variant is olive and moss. **Read from 50° above:** a dark lozenge with a bright zig-zag spine ridge.

| | |
|---|---|
| Size | **L 260 cm × W 70 cm × H 110 cm** (at the spine tips; 90 cm at the back), which fits through a door |
| Size class | mid. Tris 600–2,000. Texture 128² |
| Rig extras | `tusk_l/r` (rigid on `jaw`), `spine_row_l/r` (one bone each, so the spines can flare), 2 tail bones |
| Movement | Walk 150 cm/s, Run 450 cm/s (a scuttle), Burrowed travel 600 cm/s |

**Diet (`DT_DinoDiet`):**

| Food | Source | BondGain |
|---|---|---|
| Glowgrub | foraging (rotten logs, stumps) | 12 accepted |
| Wild Egg | Valley wild nests | 12 accepted |
| Steakroot | garden crop | 10 accepted |
| Dino Treat | Cookpot | 6 accepted |
| ★ **Grub Jelly** (favourite) | Cookpot: Glowgrub ×2 + Berry | **36** (3×) |

- **Aggression:** AggroRadius 10 m, WarnRadius 12.5 m, Fullness 8. A wild Tusksaw hides **burrowed**, showing only its spines and eyes. If the player enters WarnRadius without accepted food, its spines rattle (Threat), and if the player is still inside AggroRadius after about 2 s, it **bursts up under the player** (a cozy knock-down, not much damage). Holding Glowgrub or Grub Jelly makes it surface calmly and sniff instead.
- **Journal clue:** "Little holes by the rotten logs… something with spikes was digging for grubs."

**Arena (role: assassin):**

| Slot | Ability | Effect | Cooldown | Clip |
|---|---|---|---|---|
| 1 | **Burrow Ambush** | Dives underground and is untargetable for 1.5 s, then erupts under the **lowest-HP back-row enemy** for heavy damage and a 0.75 s *Knock-up* | **12 s** | Burrow_In → Burrowed_Loop → Burrow_Out |
| 2 | **Spine Sweep** | A 360° tail-and-spine sweep in a 2.5 m radius that deals damage and applies *Shred* (−20 % armour, 5 s) | **8 s** | Attack_SpineSweep |
| 3 (lv 3, optional) | **Dig In** | Half-burrows for 3 s: −50 % damage taken, and its spines deal thorns damage to melee attackers | 18 s | Brace_Burrow |

- **Combos:** a Knock-up into a Raptor's Pounce gets the existing +50 % (Stunned/Rooted family, if Knock-up is tagged as `CC.Airborne`). Shred stacks well with Rex's Ember Chomp.

**Pose and clip list (animation):**
| Clip | Frames | Loop | Key notes |
|---|---|---|---|
| Idle | 60 | yes | belly low, spines twitch, sniffs the ground |
| Walk / Run | 32 / 16 | yes | splayed sprawl gait; authored for 150 / 450 cm/s |
| Attack_Bite | 15 | no | tusk upward hook, contact f7 |
| **Burrow_In** | 18 | no | snout digs, front claws throw dirt, body sinks below Z = 0 by f14 (only the spines remain) |
| **Burrowed_Loop** | 24 | yes | only the spine tips and eyes above ground, moving with a dirt-wave wobble |
| **Burrow_Out** | 20 | no | erupts up and forward, contact f6 (the knock-up), lands f14 |
| **Attack_SpineSweep** | 20 | no | crouch, then a full body spin with the spines flared, contact f10 |
| **Brace_Burrow** | 16 | no | half-sinks and flares its spines, holding the last frame |
| Threat | 30 | yes | spines rattle up and down, a low head bob, front claws scrape |
| Sniff, Eat, Happy, Refuse, Tamed | std | | Eat dips to ground level, Happy does a little dirt-flick wiggle, and Tamed rolls onto its side showing the belly |
| Hit / Faint / Rest / Stunned / Cheer | std | | Faint flops over with its spines flat, and Cheer is a spine-flare hop |

---

## 2. Ripwing: gliding pterosaur-like hunter

**Silhouette (chunky voxel):** a lean, bird-sized body with a **long hooked beak** whose upper half has a single serrated "notch" tooth row (bone colour) and a **backswept crest that's split into two prongs** (the `_M` dyeable crest). The **wings are huge stepped voxel membranes** (charcoal leading-edge bones, ember to ochre membrane with 3 darker vein lines). Folded, the wings tuck along the body and it walks on its knuckles. Its feet are short with strong hooked talons for grabbing. Instead of a tail it has a small diamond-shaped tail vane. Colours are deep teal back, ember wings, bone belly and gold eyes, and the wild variant is moss and ochre. **Read from 50° above:** a wide "W" of bright wing and a dark spear-shaped body.

| | |
|---|---|
| Size | **L 180 cm** (beak to tail vane) × **H 120 cm** (standing on its knuckles, crest included) × **wingspan 450 cm**. Folded width is 70 cm |
| Size class | mid body. Outdoors to fly, but it can walk through a door with its wings folded. Tris 600–2,000. Texture 128² |
| Rig extras | `wing_01..03_l/r` (arm, wrist, finger), `crest`, `tail_vane`, `talon_l/r`. Total ≤ 35 |
| Movement | Walk 120 cm/s (knuckle-walk), Glide 700 cm/s, Dive 1,400 cm/s |

**Diet:**
| Food | Source | BondGain |
|---|---|---|
| River Fish | fishing | 12 accepted |
| Glowgrub | foraging | 10 accepted |
| Berry | garden | 8 accepted |
| Dino Treat | Cookpot | 6 accepted |
| ★ **Fizzberry** (favourite) | Beat Hop reward (combo milestones) | **36** (3×) |

- **Aggression:** AggroRadius 14 m (it watches from cliffs and perches), WarnRadius 17.5 m, Fullness 7. If the player approaches without accepted food, it screeches and hovers (Threat), then **dive-bombs** (a cozy knock-down) and circles back to its perch. If the player holds an accepted food up, it glides down and lands in front of the player. As a fun beat, a Fizzberry offer can be **tossed**: it catches the berry mid-air (the Happy variant).
- **Journal clue:** "Something big and bright keeps swooping over the river at Beat Hop o'clock… it seemed to like the sparkly berries."

**Arena (role: skirmisher, flyer):**
| Slot | Ability | Effect | Cooldown | Clip |
|---|---|---|---|---|
| 1 | **Dive Strike** | Takes off and dives onto any enemy within 10 m, dealing damage and *Slow* 30 % for 2 s. While airborne (0.6 s) it can't be hit by melee | **9 s** | Takeoff → Dive → Land_Strike |
| 2 | **Updraft Grab** | Snatches a small or mid enemy (≤ mid size class), carries it 4 m and drops it into the Ripwing team's front row, with a 1 s *Stun* on landing. Large dinos are immune to the carry and only take damage | **16 s** | Grab_Lift → Carry_Loop → Drop |
| 3 (lv 3, optional) | **Tailwind** | One flap that gives allies within 5 m +20 % move and attack speed for 4 s | 20 s | Flap_Buff |

- **Combos:** Updraft Grab pulls a back-row target into Rex's Roar or Sailback's Sail Flare range. Dive Strike onto a *Wet* target pairs with the player's Spark Bolt.

**Pose and clip list (animation):**
| Clip | Frames | Loop | Key notes |
|---|---|---|---|
| Idle (ground) | 60 | yes | wings folded, head tilts, crest bob |
| **Perch_Idle** | 60 | yes | wings half-draped, gripping an edge with its talons (wild cliff spawn) |
| Walk / Run | 32 / 20 | yes | knuckle-walk; the Run is a half-flap hop. Authored for 120 / 300 cm/s |
| **Takeoff** | 20 | no | crouch f6, wing down-beat f10, airborne f14 |
| **Glide_Loop** | 40 | yes | wings flat, small wing-tip flex, head scanning |
| **Flap_Loop** | 16 | yes | powered flight |
| **Dive** | 12 | no | wings swept back into a spear pose |
| **Land_Strike** | 18 | no | talons forward, contact f5, settles by f15 |
| **Grab_Lift** | 18 | no | talons close f6, big down-beat f10 |
| **Carry_Loop** | 16 | yes | heavy flap, legs hanging with the `talon_` socket `Grab` |
| **Drop** | 12 | no | release f4 |
| **Flap_Buff** | 24 | no | a big wing-spread pose with a peak at f12 |
| Attack_Bite | 15 | no | beak jab, contact f7 |
| Threat | 30 | yes | wings half-spread and mantling, beak open, screech pose |
| Sniff / Eat / Happy / Refuse / Tamed | std | | **Happy_Catch** variant (24 f, snaps a tossed berry at f8). Tamed folds its wings and lowers its crest to the player |
| Hit / Faint / Rest / Stunned / Cheer | std | | Faint is a wings-sprawled flop, and Cheer is a wing flap plus a crest flare |

---

## 3. Scythepaw: towering sickle-clawed giant

**Silhouette (chunky voxel):** a **tall, pot-bellied biped** that leans forward. It has a **small head on a long S-neck** with a short beak-like snout (a row of tiny pixel teeth behind it), and a **fluffy voxel feather mane** around the neck and shoulders (stepped 1–2 mv "fluff" blocks; this is the `_M` dyeable region). Its signature is a pair of **long arms ending in three giant sickle claws** each (about 10 mv long, bone and charcoal, the danger pixels) that it holds folded like a praying pose when calm. It has thick feathered legs and a short feathery tail fan. Colours are moss and olive with a bone-cream mane and charcoal claws, and the rare morph is a pale lavender mane. **Read from 50° above:** a round fluff-ball body with two huge pale crescents (the claws) in front. It's scary-looking but goofy: the joke is that it has a sweet tooth.

| | |
|---|---|
| Size | **L 500 cm × W 150 cm × H 380 cm** (head up), with **claw reach 180 cm** |
| Size class | large, outdoors only. Tris 800–3,000. Texture 256² |
| Rig extras | `claw_01..03_l/r` (3 per hand, for fanning and folding), `mane` (a puff scale bone, if non-uniform scale is allowed; otherwise a rigid flare), `tailfan`. Total ≤ 45 |
| Movement | Walk 180 cm/s, Run 520 cm/s (a lumbering stride) |

**Diet:**
| Food | Source | BondGain |
|---|---|---|
| Thornmelon | garden crop | 12 accepted |
| Steakroot | garden crop | 12 accepted |
| Smoked Haunch | Cookpot (also an arena reward) | 12 accepted |
| Berry | garden | 8 accepted |
| Dino Treat | Cookpot | 6 accepted |
| ★ **Petit Cake** (favourite) | fashion show reward (A tier+) or Cookpot: Berry ×2 + Flower + Glitter | **36** (3×) |

- **Aggression:** AggroRadius 16 m, WarnRadius 20 m, Fullness 9, slower bond decay (a big late-game tame). If the player approaches without accepted food, it **unfolds its claws and rears up** (Threat, which reads from 20 m). If the player stays, it charges and does a claw swipe (a cozy knock-down). Offering Petit Cake makes it instantly sit down like a happy giant chicken.
- **Journal clue:** "Huge claw marks on the melon patch… but the melons were eaten very neatly."

**Arena (role: bruiser / off-tank):**
| Slot | Ability | Effect | Cooldown | Clip |
|---|---|---|---|---|
| 1 | **Scythe Sweep** | A 120° cone, 3.5 m: two claw passes for damage, the second pass adds *Knockback* 2 m | **8 s** | Attack_ScytheSweep |
| 2 | **Fluff Up** | Puffs its mane: −35 % damage taken and *Taunt* on enemies within 4 m for 2.5 s | **15 s** | FluffUp |
| 3 (lv 3, optional) | **Harvest Slash** | A leaping overhead double-claw slam on one target that deals bonus damage vs *Shred* or *Knock-up* targets | 12 s | Attack_LeapSlam |

- **Combos:** Fluff Up's Taunt plus the player's Stone Wall piles enemies in front of Scythe Sweep. Knockback into a Lanternmaw's lure zone.

**Pose and clip list (animation):**
| Clip | Frames | Loop | Key notes |
|---|---|---|---|
| Idle | 60 | yes | claws folded in a "praying" pose, neck sway, mane breathes |
| Walk / Run | 36 / 20 | yes | a heavy bobbing stride, authored for 180 / 520 cm/s |
| Attack_Bite | 15 | no | a quick neck dart, contact f7 |
| **Attack_ScytheSweep** | 24 | no | right claw passes at f9 and the left at f15, with claws fanned |
| **FluffUp** | 20 | no | chest out and mane puffed, peaking at f10 and holding to the end |
| **Attack_LeapSlam** | 30 | no | crouch f8, a small leap, slam contact f18 |
| Threat | 30 | yes | rears tall, claws fully fanned overhead, beak open |
| **Sit_Happy** | 36 | no | a plop-sit with a wiggle (used after a favourite and by Tamed) |
| Sniff / Eat / Happy / Refuse / Tamed | std | | Eat is a delicate beak-nibble from the player's hand height (a long neck dip), and Tamed nuzzles and then plays Sit_Happy, holding it |
| Hit / Faint / Rest / Stunned / Cheer | std | | Faint is a fluffy topple onto its side, and Cheer is a claw clap |

---

## 4. Lanternmaw: lure-light swamp lurker

**Silhouette (chunky voxel):** a **squat, toad-flat predator** that's almost all head. It has an **enormous wide jaw** (about 40 % of its length) with a crooked row of pixel fangs, short bowed legs and a thick tapering tail. A **flexible stalk** rises from its brow (3 segments) and ends in a **glowing lantern bulb** (the `_E` emissive; this is the dyeable `_M`, so it can take the paint system's glow and sparkle Finishes). Its back has bumpy mossy "lily-pad" plates, and its small eyes sit on top of the head like a frog's. Colours are deep teal and moss with a bone-pale throat and a warm eye-gold lure, and the rare morph has a sky-blue lure. **Read from 50° above:** a dark flat oval with one bright dot in front of it. That bright dot is the whole joke.

| | |
|---|---|
| Size | **L 320 cm × W 120 cm × H 130 cm** (to the top of the lure; the body is 80 cm high) |
| Size class | mid-large, outdoors (too wide for doors). Tris 600–2,500. Texture 128² |
| Rig extras | `lure_01..03` (stalk), `lure_bulb` (socket `Lure` for VFX and light), `throat` (pouch scale or flare). Total ≤ 35 |
| Movement | Walk 120 cm/s, Run 380 cm/s (a belly-slide waddle), Swim 300 cm/s |

**Diet:**
| Food | Source | BondGain |
|---|---|---|
| River Fish | fishing | 12 accepted |
| Glowgrub | foraging | 12 accepted |
| Wild Egg | wild nests | 10 accepted |
| Fish Skewer | Cookpot | 14 accepted |
| Dino Treat | Cookpot | 6 accepted |
| ★ **Golden Carp** (favourite) | fishing, rare (1 in 15) | **36** (3×) |

- **Aggression:** AggroRadius 9 m (short, because it's an ambusher), WarnRadius 11 m, Fullness 8. Wild ones sit half-submerged by the river and pond with only the lure showing, and the lure gently draws the player's eye. If the player gets within range without accepted food, the lure flares red (Threat). If the player stays, it **lunges with a gulp-snap** (a cozy knock-down, and the player is spat back out with a "ptoo"). Holding fish makes it waddle out and open wide.
- **Journal clue:** "A little light floating on the pond at dusk… the Golden Carp vanished every time it blinked."

**Arena (role: controller):**
| Slot | Ability | Effect | Cooldown | Clip |
|---|---|---|---|---|
| 1 | **Lure Light** | The lure pulses: enemies within 6 m are pulled 2.5 m toward the Lanternmaw and *Rooted* for 1.5 s | **14 s** | LurePulse |
| 2 | **Gulp Snap** | A huge bite on one adjacent enemy that deals ×2 damage if it's *Rooted* or *Stunned*, and heals the Lanternmaw for 15 % max HP | **10 s** | Attack_Gulp |
| 3 (lv 3, optional) | **Murk Belch** | A cone that applies *Wet* (slow 20 %, 5 s) and −15 % accuracy for 3 s | 16 s | Belch |

- **Combos:** Lure Light's Root sets up the existing Raptor Pounce (+50 %) and its own Gulp Snap. Murk Belch's Wet works with Spark Bolt and Sailback's Tidal Snap.

**Pose and clip list (animation):**
| Clip | Frames | Loop | Key notes |
|---|---|---|---|
| Idle | 60 | yes | flat on its belly, the lure bobbing in a slow figure-eight, throat pulsing |
| **Submerged_Idle** | 60 | yes | the body below the water line (Z offset), only the eyes and lure above |
| Walk / Run | 36 / 20 | yes | a waddle, then a belly-slide hop. Authored for 120 / 380 cm/s |
| Swim | 30 | yes | tail-scull with the lure trailing, authored for 300 cm/s |
| Attack_Bite | 15 | no | a quick snap, contact f7 |
| **LurePulse** | 24 | no | lure raised high f6, flare peak f12 (VFX on the `Lure` socket), lure drawn back toward the mouth f20 |
| **Attack_Gulp** | 24 | no | jaw opens wide f8, lunge and gulp contact f13, satisfied throat bulge f18 |
| **Belch** | 20 | no | throat inflates f8, release f10–f16 |
| Threat | 30 | yes | jaw half-open with the fangs showing, lure flicking fast, tail slap |
| Sniff / Eat / Happy / Refuse / Tamed | std | | Eat opens wide while the player drops the food in (gulp f10), Happy makes the lure blink with a hop, Refuse spits the food out with a "ptoo", and Tamed lowers the lure to boop the player then lies flat |
| Hit / Faint / Rest / Stunned / Cheer | std | | Faint flips belly-up with the lure dimmed, and Cheer is a lure-spin plus a croak pose |

---

## Notes for the data tables
- `DT_DinoDiet` rows are above: AggroRadius, WarnRadius = Aggro × 1.25, Fullness, BondGain. The favourites are exactly 3× the species' main accepted food (36 vs 12).
- `DT_DinoAbilities`: 2 abilities at tame. Slot 3 (level 3) is **optional data** (`bOptional = true` / it can be disabled per row), and it isn't required for a species to ship. All abilities reuse existing statuses.
- **Approved new mechanic tags:** `Mechanic.Untargetable` (Burrow Ambush, and the airborne window of Dive Strike), `Mechanic.Displace` (Updraft Grab, Lure Light pull) and `Status.AccuracyDown` (`Accuracy-`, Murk Belch).
- Foods: no new food items are needed, because everything uses the existing taming-food list.
- Bone extras keep the shared names, so the follow, fight and taming AnimBPs carry over.
