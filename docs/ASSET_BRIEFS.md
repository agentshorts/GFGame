# GFGame asset briefs (Blender art)

**Project:** GFGame, a **cozy co-op dino builder**: a voxel home and garden builder, with materials earned from four mini-games (dino taming, a fashion show, a small 3v3 one-lane battle arena, and Beat Hop, a rhythm obby). Original IP, and kept fully separate from every other project.
**Written:** Oct 29, 2026 (Chicago time) by the Unreal side. **Engine:** Unreal 5.8.4. **Blender:** 5.2.2 (portable build).
**Built from:** `PLAN.md` (the plan: M1 hub builder, M2 taming, M3 fashion, M4 arena, M5 Beat Hop), the style answers, and lessons from an earlier import test (cm-scene export, root scale 1, loop keys, Force Front X Axis).
**Deadline:** none. Quality and consistency matter more than speed.

> **Design answers (Oct 29):**
> - **Art style:** chunky voxel (classic block-builder look).
> - **Favourite dinos:** the murderous ones (predators like T. rex, raptors and spinosaurus), done in voxel style.
> - **Deadline:** none.
>
> **Originality rules (from PLAN.md):** Other games are mood references only. Don't copy any other game's block textures, skin templates or UV layouts, creature designs, UI, logos or sounds. Never put other games' names in file names, material names, or any generator prompt. Real dinosaur species are fine ("raptor", "T. rex", "spinosaurus"). Don't copy any film's or game's specific dino design (signature stripe patterns, neck frills, spitting, and so on). **Every design below is original to GFGame.** Display names are placeholders, because players get to name things.
>
> **Separation:** don't reuse any mesh, rig, texture, material or `.blend` from any other project. Your own export *script and settings* may be reused; that's tooling, not content.

---

## 0. World facts every asset must respect

| Fact | Value | Notes |
|---|---|---|
| **Block (grid unit)** | **100 cm** cube | One voxel cell. The hub plot is 64 × 64 blocks wide and 32 tall. |
| **Micro-voxel (mv)** | **6.25 cm** = 1/16 block | Every character, dino, furniture piece, crop and item is built from axis-aligned cuboids whose sizes are **whole mv**. |
| **Texel density** | **16 px per block face = 1 px per mv**, on everything | Blocks, furniture, characters and dinos all share the same pixel size, so nothing looks higher-res than anything else. |
| Doors and openings | A door is 1 × 2 blocks (100 × 200 cm) | The player (175 cm) and the raptor (150 cm, ≤ 75 cm wide) must fit through. |
| Player | 175 cm tall (28 mv) | UE capsule is about radius 40 and half-height 88. |
| Hub camera | Third person: spring arm about 450 cm, pitch about −20°, FOV 90 | Make backs readable too, because players mostly see their character's back. |
| Team colours (arena, set by UE at runtime) | ally `#4FC3F7`, enemy `#FF6B6B` | Only through a white `TeamTint` slot. |
| Lighting | Dynamic and Lumen in Unreal | **Don't paint directional light or AO into textures.** |

### Size classes (cm, UE; measured from the ground)
| Class | Footprint | Height | Used for |
|---|---|---|---|
| Item / pickup | ≤ 50 × 50 | ≤ 50 | crafting materials, seeds, treats |
| Crop | inside 1 × 1 block | ≤ 100 at the final stage | garden plants |
| Humanoid | 87.5 wide (with arms) × 50 deep | 175 (+ hair or hat) | player, rival, outfits |
| Small dino | ≤ 50 wide × 125 long | 50–75 | Nipper |
| Mid dino | ≤ 75 wide × 300 long | 140–160 | Raptor (fits through a door) |
| Large dino | ≤ 200 wide × 900 long | 350–450 | Rex, Sailback (outdoors only) |
| Furniture | whole-block footprint (1 × 1, 2 × 1, …) | ≤ footprint height in blocks | everything placed on the build grid |

---

## 1. Shared specs (apply to every asset unless the brief says otherwise)

### 1.1 Blender scene: **centimetre scene** (lesson from earlier import tests)
- `scene.unit_settings.system = 'METRIC'`, **`scale_length = 0.01`**, `length_unit = 'CENTIMETERS'`. **1 Blender unit = 1 cm** (a block is 100 BU, a mv is 6.25 BU).
- Model at real size, then apply rotation and scale (Ctrl+A) so every object is 1,1,1 / 0,0,0.
- **Everything faces −Y in Blender** (numpad 1 = front view). Unreal imports with **Force Front X Axis**, so the expected Unreal facing is **+X**.
- Armature object named **`root`**. Export **deform bones only**, with no `_end`/leaf bones.
- Scene frame rate **30 fps** (`fps_base 1`).

### 1.2 FBX export
Use **the export path that passed earlier re-import tests** (cm scene, FBX written in cm with unit factor 1.0, root node scale 1). The reference settings are below. If your proven script differs, your script wins; record what you used in `manifest.units`.
```
bpy.ops.export_scene.fbx(
  use_selection=True,
  object_types={'MESH'}            # static
             # {'ARMATURE','MESH'}  # skeletal
  global_scale=1.0, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
  axis_forward='-Z', axis_up='Y', bake_space_transform=False,
  use_mesh_modifiers=True, mesh_smooth_type='FACE', use_tspace=False,
  colors_type='NONE',              # no vertex colours (see 1.3)
  add_leaf_bones=False, primary_bone_axis='Y', secondary_bone_axis='X',
  use_armature_deform_only=True, armature_nodetype='NULL',
  bake_anim=False)                 # mesh file
```
Animation files use the same settings plus `bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False, bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0`, with **one Action per file**.
**Hard edges everywhere** (voxel look): flat shading, with no smoothing groups across cuboid faces.

### 1.3 Texturing: pixel textures, nearest filtering (decided)
**Decision:** use **pixel-art PNG textures** sampled with **nearest filtering**. **No vertex colours** (export with `colors_type='NONE'` so UE doesn't pick any up). Here's why:
- the voxel mesher needs per-face block textures anyway
- dyes and skin tones need masks
- one method everywhere keeps the look consistent
- **1 px = 1 mv** (6.25 cm). Every cuboid face maps to a whole-pixel rectangle. **Snap UVs to pixel corners** (no half pixels and no stretched pixels) so nothing smears.
- Texture sizes are powers of two: blocks are **exactly 16 × 16** (the mesher packs them into a texture array, so every block face must be the same size). Items and crops use 32² or 64². Characters use 64². Mid dinos use 128², and large dinos 256².
- Mirrored or overlapping UV islands are **allowed** (left and right limbs can share pixels). You don't need a UV1 lightmap channel; Unreal handles that if it's ever needed.
- **Map set:**
  - `T_<Name>_D` (sRGB colour, alpha only where a brief says masked)
  - optional `T_<Name>_E` (greyscale emissive mask, same size as D, linear)
  - optional `T_<Name>_M` (linear mask: **R = primary dye/tint region, G = secondary dye region**, B unused)
  - **No normal or roughness maps.**
- Dyeable or tintable regions are painted in a **neutral light-grey ramp** in `_D` (3–4 shades from `#FBF6EC` down to `#9C9690`), and Unreal multiplies the dye colour in through `_M`.
- **No anti-aliasing in pixel art.** If you bake or render anything into a texture, render at the final pixel size with a pixel filter of about 0.01 px, or paint it directly.
- Unreal side, for reference: Filter **Nearest**, texture group **2D Pixels (unfiltered)**, mips on (point-sampled) for blocks and world props, and no BC1 on 16 px textures (BC7 or uncompressed).

### 1.4 Palette rules
One master palette for the whole game. Deliver it as `handoff\style\GF_Palette.png` (one swatch per pixel, plus a labelled 32 px-swatch version) and `GF_Palette.gpl`. **Every texture uses only palette colours.** You can add up to 8 new colours if a design truly needs them; add them to the palette file and note them in `qa.notes`.

| Group | Ramp (light → dark) |
|---|---|
| Grass | `#7CC45C` `#5FAE4E` `#44893E` `#2F6633` |
| Dirt | `#A8744E` `#8A5A3C` `#6B4430` `#4E3124` |
| Wood plank | `#D9A86C` `#C08A55` `#9C6A3E` `#7A5030` |
| Bark / log | `#8A6440` `#6E4B2E` `#5A3B24` |
| Stone | `#B5B5B0` `#9A9A98` `#7A7A78` `#5E5E5C` |
| Sand / clay | `#E3CF8F` `#CBB477` / `#D88C68` `#C77B5B` `#A86249` |
| Leaves / water | `#4E9A3F` `#3B7A33` / `#7EC8F2` `#4FA6D9` `#3784B8` |
| Flowers and accents | rose `#E8577A`, blush `#F49AC1`, lavender `#B48DE0`, sunflower `#F5C842`, coral `#FF8A65`, cream `#F6EBD3` |
| Fabric pastels | mint `#A8E6CF`, peach `#FFD3B6`, lilac `#D5C6F5`, butter `#FFF1A8` |
| Predator dinos | rust `#B5532E`, ember `#D9733B`, ochre `#C9A04A`, olive `#6E7B3A`, moss `#4F6B3A`, deep teal `#2F6F73`, charcoal `#3A3533`, bone `#E8DCC2`, mouth red `#A8232E`, eye gold `#FFD23F` |
| Amber / crystal / metal | amber `#F0A030`, crystal `#7FE3F0`, metal `#A7B2BC` `#6C7780` |
| Neutrals | paper `#FBF6EC` (lightest), ink `#2A2430` (darkest) |
| Skin presets (UE tints the Skin slot) | `#F6D3B8` `#E8B48F` `#C98C63` `#A0683F` `#73472B` `#4A2E1E` |

Rules:
1. **No pure black or pure white.** Use ink and paper.
2. Each surface uses **3–5 shades of one ramp**. Shift the hue as you go: shadows are cooler and slightly more saturated, highlights warmer.
3. Add pixel noise for texture, but **≤ about 25 % of pixels** should differ from the base shade, and never use a checkerboard. Block tops stay **low-contrast** so the build ghost and selection outline read on top of them.
4. **Cozy overall, with one "danger" accent on predators:** eyes, teeth and claws are the only high-contrast pixels on a dino. That reads as "murderous but lovable."
5. Faces use 1–2 px features (eyes with a 1 px highlight, plus a blush pixel).

### 1.5 Rigs (skeletal)
- **Rigid voxel skinning:** each cuboid is **100 % weighted to one bone**, with no blended weights. Joints pivot at cuboid ends. A 1 mv overlap at elbows and knees is fine to hide gaps on bends.
- Bind pose: humanoids in an **A-pose about 30° down** (arms clear the torso); dinos in a neutral stance.
- Skeletons:
  - **`SKEL_GF_Humanoid`** is defined by `SK_Player_Base` in batch 1 and **frozen after batch 1 passes**. Every outfit, the rival and all humanoid animations use it.
  - Each dino species gets its own `SKEL_Dino_<Species>`, but **shared parts use the same bone names across species** (`pelvis, spine_01, spine_02, neck_01, neck_02, head, jaw, tail_01…, thigh_l, shin_l, foot_l, toe_l, upperarm_l, forearm_l`, and so on) so the follow/fight logic and AnimBPs can be shared.
- Bone budgets: humanoid 24, mid dino ≤ 35, large dino ≤ 45, minions ≤ 20.

### 1.6 Animation rules (30 fps, in place)
- **All clips are in place** with the root at the origin and **no root motion**. Unreal moves the actor. For locomotion loops, record the **ground speed the stride was authored for** (cm/s) in `qa.notes` so the blend space matches and feet don't slide.
- **Frame-count rule** (fixes the "one frame short" issue): a clip of **N frames** is keyed **f1 to f(N+1)**, so Unreal reads **N frames = N/30 s**.
  - **Loops:** f(N+1) is an **exact copy of f1** (the closing loop key is exported).
  - **One-shots:** f(N+1) is the end or hold pose.
- File names are `A_<AssetBase>_<Clip>.fbx`, one Action per file. Record contact, release and impact frames in `qa.notes` (for example, `Attack_Bite contact f7`).
- Combat stays cozy: dinos **faint** (flop over, little stars), they never die or bleed.

### 1.7 Origins
- **Characters and dinos:** on the ground (Z = 0) **directly under the pelvis**.
- **Blocks (preview mesh):** bottom centre of the cube.
- **Furniture:** bottom centre of the **whole-block footprint**. Footprint sizes are exact multiples of 100 cm, and Unreal rotates in 90° steps around the origin. **Exception:** doors pivot at the bottom of the hinge edge (see the brief).
- **Crops:** bottom centre of the cell, where Z = 0 is the **top of the farmland block**.
- **Items and pickups:** the geometric centre (Unreal bobs and spins them).

### 1.8 Collision
Static meshes get `UCX_<AssetName>_00…` boxes (furniture: 1–3 simple boxes; block preview: one box; items: one box). Crops and decals get no collision. Unreal generates skeletal physics assets.

### 1.9 Provenance and licence
Model everything by hand: `"ai_generated": false, "license": "own work"`. If an AI 3D generator is ever used for a base mesh, it must be a **paid plan with private output and full ownership**. Log the generator, plan tier, date and prompt, and use no franchise names in prompts. **Never use a free plan, and never use anyone else's art or game screenshots as input.**
Add one row per asset to `<repo>\handoff\license_ledger.csv` with the PLAN.md §6 columns: `asset_path, source, url_or_job_id, creator, license, plan_tier_at_generation, date_ct, prompt_or_ref_file, attribution_needed, proof_file, notes`.

### 1.10 Previews
`preview.png` (1280 × 720, Eevee or Cycles):
- **Shot A:** the hub third-person angle (camera about 450 cm behind and above, pitch −20°, FOV 90).
- **Shot B:** front three-quarter view.
- Shot C (characters and dinos): a **scale line-up** with a 1 m block, a 1 × 2 door frame, and the 175 cm player (or a 175 cm placeholder until the player exists).

Check that dinos and characters still read at about 120 px tall.

---

## 2. Priority order (batches)

| # | Asset | Type | Batch | Milestone |
|---|---|---|---|---|
| 0 | `handoff\style\` palette and style sheet (not an import asset) | doc and PNG | **1** | all |
| 1 | `SM_Block_Grass` (+ `T_Block_Dirt`) | block | **1** | M1 |
| 2 | `SM_Block_WoodPlank` | block | **1** | M1 |
| 3 | `SM_Block_Stone` | block | **1** | M1 |
| 4 | `SK_Player_Base` (defines `SKEL_GF_Humanoid`) | skeletal | **1** | M1 |
| 5 | `SK_Outfit_Top_GardenTee` (first modular outfit piece) | skeletal, on the humanoid skeleton | **1** | M1/M3 |
| 6 | `SK_Dino_Raptor` | skeletal | **1** | M2 |
| 7–25 | the other M1 blocks, 8 M1 furniture pieces, 3 crops × 4 stages, foraging nodes, hub gates | static | 2 | M1 |
| 26–50 | crafting-material items with icons, later blocks and furniture | static | 2b | M1+ |
| 51–60 | `SK_Dino_Rex`, `SK_Dino_Sailback`, `SK_Dino_Nipper`, optional `SK_Dino_Hornback`, raptor anim top-up, wild and rare colour textures, dino accessories, eggs, valley kit | skeletal and static | 3 | M2 |
| 61–100 | about 33 outfit pieces, runway and dressing-room kit, runway anims, judges | skeletal and static | 4 | M3 |
| 101+ | arena tower, core, minions, lane kit, player combat anims, dino armour | mixed | 5 | M4 |
| 130+ | Beat Hop: 5 lane-colour beat bricks + neutral/hold/pad/crumble bricks, track kit, VFX meshes and sprites, player hop/air/land/fall anims, Jukebox | mixed | 6 | M5 |

**Why this order:** batch 1 tests every pipeline type in one import pass: a voxel block (texture-array faces plus a preview mesh), the shared humanoid skeleton, a modular outfit layered on it, and a creature rig with follow and fight locomotion. Then come the first playable hub (M1), then taming (the core draw: predators), then fashion, and the arena last because it's the riskiest and least essential (PLAN.md §8).

---

## 3. Batch 1: pipeline test (6 assets plus the style sheet)

**Acceptance for batch 1** (Unreal side):
- Each folder lands in `handoff\inbox\`, and UE imports with **Force Front X Axis**.
- **Bounds = `dimensions_cm` ±1 %**, the asset faces **+X**, and the **root bone scale is 1.0**.
- Anims play in place with the right lengths (Idle reads **60 frames / 2.0 s**).
- Block faces tile seamlessly in the voxel test chunk, and textures are crisp under Nearest filtering.
- The tee layers on the player through Leader Pose with **no z-fighting and no gaps** in every player anim.
- The raptor follows the player on the test path with no visible foot sliding at its recorded speeds.
- **The project owner approves** the first import of each type (block, player plus outfit, dino).

### 3.0 Style sheet (`handoff\style\`)
- `GF_Palette.png` and `GF_Palette.gpl` (§1.4).
- `StyleSheet.png`: a single image with the three batch 1 blocks, the player (front and back) wearing the tee, the raptor (side and front), a 1 × 2 door frame and a 1 m grid, all at the same scale under neutral light. This becomes the art reference for everything after it.
- `mv_grid.blend`: an empty cm scene with a 6.25 cm snapping grid, block-sized guides, and the export script, so every later asset starts from the same file.

### 3.1 `SM_Block_Grass` (batch 1)
- **Purpose:** the default ground block, and the block players see most. Its side view also defines how all block sides look.
- **Size:** a 100 × 100 × 100 cm cube. `dimensions_cm` = `[100, 100, 100]`. **Tris:** 12.
- **Textures (exactly 16 × 16 px each):**
  - `T_Block_Grass_Top`: grass ramp with a few lighter blade pixels. It **must tile seamlessly on all 4 edges and still look right rotated 90/180/270°** (the mesher may rotate tops to break repetition).
  - `T_Block_Grass_Side`: dirt with a **3–4 px grass lip** along the top edge, plus a few 1 px drips. It tiles horizontally, and **V up = +Z**.
  - `T_Block_Dirt`: plain dirt. It's used as this block's bottom **and** as the future `SM_Block_Dirt` (batch 2 just references it).
- **Materials:** slot 0 `Top`, slot 1 `Side`, slot 2 `Bottom`. Each face is UV'd to the full 0–1 square of its texture.
- **Origin:** bottom centre. **Collision:** `UCX_SM_Block_Grass_00` (a 100 cm box, used for the dropped-item and held-item mesh).
- **Note:** in the world, the voxel mesher draws blocks from the **face textures**. The static mesh is for the held item, dropped item, ghost preview and inventory icon.
- **UE target:** `/Game/Art/Blocks/SM_Block_Grass` (textures in `/Game/Art/Blocks/Textures/`).

### 3.2 `SM_Block_WoodPlank` (batch 1)
- **Purpose:** the first building block. Walls and floors of the player's first house are made from it.
- **Size:** 100 cm cube, `[100, 100, 100]`, 12 tris.
- **Texture:** `T_Block_WoodPlank` 16 × 16: **4 horizontal planks of 4 px**, staggered seams in the darkest wood shade, sparse grain, 1–2 nail pixels. Tiles on all edges.
- **Materials:** a single slot `All` (all 6 faces). **Origin:** bottom centre. **Collision:** UCX box.
- **UE target:** `/Game/Art/Blocks/SM_Block_WoodPlank`.

### 3.3 `SM_Block_Stone` (batch 1)
- **Purpose:** natural stone. It's a basic block and a foraging yield.
- **Size:** 100 cm cube, `[100, 100, 100]`, 12 tris.
- **Texture:** `T_Block_Stone` 16 × 16: soft cool-grey stone ramp with 2–3 faint cracks and low contrast. Tiles on all edges, with no obvious repeat in a 4 × 4 wall.
- **Materials:** slot `All`. **Origin:** bottom centre. **Collision:** UCX box.
- **UE target:** `/Game/Art/Blocks/SM_Block_Stone`.

### 3.4 `SK_Player_Base` (batch 1): the player character, and the shared humanoid skeleton
- **Purpose:** the player avatar in every mode. It **defines `SKEL_GF_Humanoid`**, which every outfit piece, runway animation and the arena moves are built on. The bone list is frozen once batch 1 passes.
- **Proportions (whole mv):**
  - **Head** 8 × 8 × 8 (50 cm).
  - **Torso** 8 wide × 4 deep × 10 tall.
  - **Arms** 3 × 3 cross-section: upperarm 4, lowerarm 4, hand 2.
  - **Legs** 4 × 4 cross-section: thigh 4, calf 4, foot 2. The foot extends 1 mv forward, so it's 4 × 5 × 2.
  - Total **28 mv = 175 cm** tall and 14 mv = 87.5 cm wide. `dimensions_cm` (UE X, Y, Z) = `[50, 87.5, 175]`.
  - Slightly big-headed, so it's cute and readable from the hub camera.
- **Look (original):** a friendly neutral avatar in simple **basewear**: a plain tank top and shorts in soft neutrals, so the player is never "undressed" in the dressing room. Its own original UV layout (not any game's skin template).
  - **Face:** the front 8 × 8 of the head is a separate `Face` slot. `T_Player_Face_D` is **32 × 8 = 4 expressions** (neutral, happy, wink, surprised) side by side, and Unreal offsets the UV to swap them.
  - **Hair:** none on the base. Hair is an outfit slot, so the base has a simple short cap of hair-coloured pixels and the hair pieces sit over it.
- **Tris:** 200–1,000. **Materials:**
  - `Skin` (`T_Player_Skin_D` 64², a greyscale ramp, `_M` R = skin; UE tints it to one of the 6 skin presets)
  - `Face` (`T_Player_Face_D` 32 × 8)
  - `Basewear` (`T_Player_Basewear_D` 64², `_M` R/G dyeable)
- **Skeleton `SKEL_GF_Humanoid` (24 bones, UE-mannequin-style names so stock retargeting can map them):**
  - Body: `root, pelvis, spine_01, spine_02, neck_01, head, upperarm_l, lowerarm_l, hand_l, upperarm_r, lowerarm_r, hand_r, thigh_l, calf_l, foot_l, thigh_r, calf_r, foot_r`
  - **Spare bones for outfits:** `hair_back` (ponytail or long-hair sway), `cape_01, cape_02` (capes and coats), `skirt_f, skirt_b` (skirts and dresses), `prop_r` (a held item, child of `hand_r`).
  - The spare bones have no weights on the body, but **are flagged Deform so they export**.
- **Anims (batch 1):**

| Clip | Frames | Loop | Notes |
|---|---|---|---|
| Idle | 60 | yes | breathing bob (1 mv), blink via face swap is UE-side |
| Walk | 30 | yes | authored for **200 cm/s**; arms and legs swing ±35° (voxel-style stiff swing) |
| Run | 20 | yes | authored for **450 cm/s** |
| Jump_Start | 6 | no | crouch, then launch |
| Jump_Loop | 12 | yes | tucked air pose with a slight bob |
| Jump_Land | 8 | no | squash, then recover |
| Interact | 12 | no | right-arm chop/place swing (place, break, harvest, craft); **contact f6** |

- **Origin:** on the ground between the feet, under the pelvis. **UE target:** `/Game/Art/Characters/Player/SK_Player_Base`.

### 3.5 `SK_Outfit_Top_GardenTee` (batch 1): the first modular outfit piece
- **Purpose:** proves the modular outfit system (M3 fashion, and also what the player wears in the hub). It's the **Top** slot. The other slots (Hair, Bottom, Shoes, Accessory) follow the same rules (§4.4).
- **Rule for all outfit pieces:** a **shell 0.5 mv (3.125 cm) outside the body surface** on every covered face, so it never z-fights and the body never needs hiding. Use the **same bind pose and the same skeleton** as `SK_Player_Base`, rigidly weighted to the bone under each part.
- **Coverage:** torso (spine_01 and spine_02) plus short sleeves over the top 3 mv of each upperarm. The torso shell is 8.5 mv wide × 4.5 deep plus offsets, so about 53 × 28 cm. `dimensions_cm` ≈ `[28.1, 87.5, 65.6]` (measure and record the exact figure).
- **Look (original):** a relaxed short-sleeve tee with rolled cuffs, a chest pocket with a **pixel flower** on it, and a 1 px hem line. The body is dyeable (`_M` R), and the pocket flower is the secondary dye (`_M` G).
- **Tris:** 50–300. **Materials:** `Outfit` (`T_Outfit_Top_GardenTee_D` 64², `_M` 64²).
- **Skeleton:** export with the armature (it's a skeletal mesh), but in the manifest set `"reuse_existing": "SKEL_GF_Humanoid"`. Unreal imports it onto the batch 1 skeleton. It has **no animations of its own** (Leader Pose follows the body).
- **Data tags for `DT_Outfits`** (put them in `qa.notes`): `slot=Top; tags=garden,casual,pastel; dyeable=R,G`.
- **UE target:** `/Game/Art/Characters/Outfits/Top/SK_Outfit_Top_GardenTee`.

### 3.6 `SK_Dino_Raptor` (batch 1): the first tameable predator
- **Purpose:** the first dino the player tames (M2). Tamed, it **follows the player and fights** wild dinos. Wild ones use the same mesh with a different texture. It's also the pipeline test for creature locomotion and combat.
- **Gameplay facts:**
  - Follows the player at **3–8 m**, walking or running to catch up, so it has to **outrun the player** (run 700 cm/s vs the player's 450).
  - Fights with a bite, and later a pounce (Unreal supplies the leap movement).
  - **Fits through a 1 × 2 door.**
  - Faints instead of dying.
  - Can rest in the player's garden.
- **Size (mv):**
  - Overall: **48 long (300 cm) × 24 tall at the head (150 cm) × ≤ 12 wide (75 cm)**. `dimensions_cm` (UE X, Y, Z) = `[300, 75, 150]`.
  - Hip height about 14 mv (87.5 cm).
  - Head 9 long × 6 wide × 6 tall, deliberately chunky (about 20 % of body length).
  - Body 14 × 8 × 9; neck 4 × 4 × 6; tail 20 long, tapering from 6 to 2 mv.
  - Thigh 4 × 4 × 7, shin 3 × 3 × 7, foot 6 × 3 × 2.
  - Small forearms 2 × 2 × 4 + 2 × 2 × 4.
- **Look (original):** a stocky, big-headed voxel predator. It's menacing and cute at the same time.
  - A **row of bone-coloured pixel teeth** visible even with the jaw closed, on a mouth-red edge.
  - Golden eyes with a 1 px highlight, and a heavy brow cuboid for a "grumpy hunter" look.
  - A **voxel feather crest** of 3 stepped tufts on the head, and a 1 mv feather fringe on the forearms.
  - A **raised sickle claw** (bone, 1 × 1 × 2 mv) on each foot, the signature silhouette pixel.
  - **Colouring:** rust back fading through ember to an ochre and bone belly, with **charcoal chevrons** down the spine and tail (chevrons, not stripes).
  - From behind (the follow camera mostly sees its back and side): the chevrons and crest must read.
- **Tris:** 400–2,000. **Materials:**
  - `Body` (`T_Dino_Raptor_D` 128², `_M` 128² with R = crest and feathers, so the player can dye them as a cosmetic later)
  - also deliver **`T_Dino_Raptor_Wild_D`** on the same UVs: an olive and moss camo variant for wild raptors.
- **Rig:** hand-built, deform only, about 26 bones (≤ 35):
  - `root, pelvis, spine_01, spine_02, neck_01, neck_02, head, jaw, crest, tail_01…tail_05`
  - `thigh_l/r, shin_l/r, foot_l/r, toe_l/r` (the sickle claw sits on the toe)
  - `upperarm_l/r, forearm_l/r`
- **Anims:**

| Clip | Frames | Loop | Notes |
|---|---|---|---|
| Idle | 60 | yes | breathing, head tilts, slow tail sway, one jaw snap |
| Walk | 24 | yes | authored for **300 cm/s**; head stays level |
| Run | 16 | yes | authored for **700 cm/s**; body low, tail straight |
| Attack_Bite | 15 | no | lunge-bite in place; **contact f7** |
| Hit | 9 | no | flinch, then back to idle |
| Faint | 30 | no | wobble, then flop onto its side **inside the footprint**; hold the last frame |
| *Happy* (nice to have) | 36 | no | hop and tail wag (taming success, petting) |
| *Eat* (nice to have) | 30 | no | snaps up a treat; **gulp f10** |

- **Origin:** on the ground under the pelvis. Record the bounds-centre offset in `qa.notes` (the tail makes it back-heavy).
- **UE target:** `/Game/Art/Dinos/Raptor/SK_Dino_Raptor`.

---

## 4. Remaining briefs

### 4.1 Batch 2: hub kit (M1)

**Blocks** (16 × 16 faces, 100 cm preview cube, bottom-centre origin, UCX box; same rules as 3.1–3.3). The first 12 below are the M1 set.

| Asset | Faces | Look and notes |
|---|---|---|
| `SM_Block_Dirt` | All = `T_Block_Dirt` (from batch 1) | no new texture, just the folder and manifest |
| `SM_Block_Cobblestone` | All | rounded stones with ink-dark mortar pixels |
| `SM_Block_StoneBrick` | All | 2 courses of offset bricks |
| `SM_Block_Log` | Top/Bottom = rings, Side = bark | rings centred; bark vertical |
| `SM_Block_Leaves` | All, **opaque** | dense leaf pixels with darker "holes" painted in, no alpha |
| `SM_Block_Sand` | All | very low contrast |
| `SM_Block_ClayBrick` | All | warm clay ramp, small bricks |
| `SM_Block_Farmland` | Top = tilled rows, Side = dirt with darker top 2 px | crops sit on its top face |
| `SM_Block_Thatch` | All | straw (butter/ochre ramp), for roofs |
| `SM_Block_PathDirt` | Top = trodden dirt with pebbles, Side/Bottom = dirt | garden paths |
| `SM_Block_Fabric` | All | **dyeable**: grey ramp with a woven 1 px pattern, `_M` R full |
| `SM_Block_Glass` | All, **masked alpha** (1-bit) | a 1 px frame plus a 2 px glint; centre transparent |
| `SM_Block_AmberLamp` | All + `_E` | amber core glowing through a wooden lattice |
| `SM_Block_CrystalBlock` | All + `_E` | faceted crystal pixels |
| `SM_Block_MetalPlate` | All | riveted metal panels |
| `SM_Block_Bone` | All | stacked bone pattern (from taming yields) |

**Furniture: the first 8 are M1.** Origin is the bottom centre of the footprint and the front faces −Y. Texture is per asset (64² or 128²) at 1 px/mv. Collision is UCX boxes.

| Asset | Footprint (blocks W×D×H) | Tris | Notes |
|---|---|---|---|
| `SM_Furn_CraftingBench` | 1×1×1 | 100–500 | **The crafting station.** Thick plank top, tools hanging on the side, a pixel saw and hammer. Must read as "click me" |
| `SM_Furn_Door_Wood` | 1×1×2 (panel 2 mv thick) | 50–300 | **Origin at the bottom of the hinge edge** (Blender −X side); Unreal swings it 90°. A separate `SM_Furn_DoorFrame_Wood` is optional |
| `SM_Furn_Bed_Wood` | 1×2×1 | 100–500 | `_M` R = blanket (dyeable). Pillow, plus a tiny pixel dino on the blanket |
| `SM_Furn_Table_Wood` | 1×1×1 | 50–300 | |
| `SM_Furn_Chair_Wood` | 1×1×1 | 50–300 | the seat faces −Y |
| `SM_Furn_Lamp_Lantern` | 1×1×1 (lantern 6 mv) | 50–300 | `_E` on the flame; Unreal adds a light |
| `SM_Furn_FlowerPot` | 1×1×1 (pot 6 mv) | 50–300 | `_M` R = petals (Unreal tints to the flower colour the player grew) |
| `SM_Furn_Wardrobe` | 1×1×2 | 100–500 | Passive bonus: more outfit slots. Doors closed, with a pixel hanger icon on the front |
| `SM_Furn_DinoBed` | 2×2×1 | 100–600 | Passive bonus: +1 follower. A nest of bones and scales with a cushion (dyeable) |
| `SM_Furn_Fence_Wood` / `_Gate` | 1×1×1 | 50–200 | Unreal auto-joins neighbours, so put posts at the cell centre and rails to the ±X/±Y edges as **4 separate rail meshes** (`_Post`, `_Rail`) |
| `SM_Furn_Fence_Bone` | 1×1×1 | 50–200 | same structure as the wood fence |
| `SM_Furn_Lamp_Amber` | 1×1×1 | 100–400 | glowing amber on an iron stand |
| `SM_Furn_Rug` | 2×2×(1 mv) | 20–100 | dyeable R and G pattern |
| `SM_Furn_Curtains` | 1×(2 mv)×2 | 50–200 | dyeable; hangs inside a window cell |
| `SM_Furn_Sofa` | 2×1×1 | 100–500 | dyeable cushions |
| `SM_Furn_Bookshelf` | 1×1×2 | 100–500 | |
| `SM_Furn_Vanity` | 1×1×2 | 100–500 | mirror (paper-light pixels), for the fashion theme |
| `SM_Furn_TrophyShelf` | 2×1×2 | 100–600 | slots for arena trophies |
| `SM_Furn_Banner` | 1×(1 mv)×2 | 20–100 | `_M` plus a pattern texture param (arena rewards) |
| `SM_Furn_PhotoFrame` | 1×(1 mv)×1 | 20–100 | **a separate `Photo` slot with a 0–1 UV**, so the player can drop in a real photo (Q10) |
| `SM_Furn_FossilDisplay` | 1×1×2 | 100–500 | a fossil skull on a stand |
| `SM_Furn_EggNest` | 1×1×1 | 50–300 | displays cosmetic dino eggs |
| `SM_Furn_CrystalChandelier` | 1×1×1 (hangs from the top) | 100–500 | `_E`; **origin at the top centre** (hangs from the ceiling) |

**Garden crops** (3 for M1). Static, no alpha, micro-voxel. Each stage is its own mesh. Origin is the bottom centre on top of the farmland. One shared 64² texture per plant (`T_Crop_<Plant>_D`).

| Plant | Stages (`SM_Crop_<Plant>_S0…S3`) | Max height | Notes |
|---|---|---|---|
| `Flower` (yields Flowers, which make dyes) | S0 seed mound, S1 sprout (2 leaves), S2 bud, S3 bloom | 75 cm | `_M` R = petals, so Unreal tints one mesh into each flower variety (rose, sunflower, lavender, …). Also `SM_Crop_Flower_S3_Rare`: a bigger double bloom with sparkle pixels in `_E` (the 1-in-10 rare bloom) |
| `Berry` (Fruit, which makes dino treats) | S0 mound, S1 sprout, S2 leafy bush, S3 bush with berries | 100 cm | berries in mouth red and coral, 1–2 mv clusters |
| `Fiber` (cotton-like, makes fabric) | S0 mound, S1 sprout, S2 stalks, S3 stalks with puff bolls | 100 cm | puffs in paper and cream |

Optional later: `Pumpkin` (decor), `Sapling` (plants a tree node).

**Foraging nodes** (M1 placeholder material source; they respawn). Static; origin at the bottom centre; UCX.
- `SM_Node_Tree` (trunk 1×1 block × 4 tall, canopy 5×5×3 blocks, 300–1,500 tris) and `SM_Node_Tree_Stump` (depleted).
- `SM_Node_Rock` (2×2×1.5 blocks, chunky boulder) and `SM_Node_Rock_Pebbles` (depleted).
- `SM_Node_ClayPit` (1×1 block, wet clay mound in a puddle) and `SM_Node_ClayPit_Empty`.

**Hub gates** to the mini-games (2×1×3 blocks, static, `_E` glow):
- `SM_Gate_Valley`: a stone arch with a dino footprint and claw marks.
- `SM_Gate_Runway`: a curtain arch with a ribbon and a star.
- `SM_Gate_Arena`: a banner arch with crossed pennants.

### 4.2 Batch 2b: crafting materials and items
Each item is `SM_Item_<Name>` (≤ 50 cm, micro-voxel, **origin at the centre**, 20–300 tris, 32² texture, UCX box) **plus an icon** `T_Icon_<Name>` (128 × 128 PNG, transparent background, orthographic three-quarter view at yaw 45° / pitch −30°, rendered with no anti-aliasing so the pixels stay crisp, and the same framing for every icon).

| Group | Items |
|---|---|
| Hub and garden | `Wood` (log chunk), `Stone` (rock chunk), `Clay` (lump), `Fiber` (bundle), `Flower` (posy; petal `_M`), `Berry`, `RareBloom` (`_E`), `Seeds_Flower`, `Seeds_Berry`, `Seeds_Fiber` (seed packets with a pixel picture) |
| Taming | `Scale`, `Bone`, `Amber` (`_E`), `Fossil`, `DinoEgg` (`_M` R/G so it tints per species), `DinoTreat` (a crafted treat; must look tasty to a predator, like a meaty biscuit) |
| Fashion | `Fabric` (bolt; dyeable), `Ribbon` (dyeable), `Glitter` (jar, sparkle `_E`) |
| Arena | `Crystal` (`_E`), `Metal` (ingot), `TrophyToken` (a coin with a pixel crown) |
| Crafted | `Dye` (bottle; `_M` R = liquid, so Unreal tints it to any colour) |
| System | `MysteryCrate`: a wrapped crate with a "?" (the save-migration fallback item from PLAN.md §7) |

### 4.3 Batch 3: the other dinos (M2)
All follow §1 and the raptor's conventions: shared bone names, rigid voxel skinning, `Body` slot plus `_M` for dyeable crest, sail or feathers, a **wild texture variant**, and a **rare colour-morph texture** (`T_Dino_<Species>_Rare_D`, about 1 in 20 wild spawns). Cozy combat, so they faint and never die.

| Asset | Inspired by | Size (L × W × H cm) | Tris | Rig | Design notes (original) | Anims beyond the raptor set (Idle, Walk, Run, Attack_Bite, Hit, Faint, Happy, Eat) |
|---|---|---|---|---|---|---|
| `SK_Dino_Rex` | T. rex | 900 × 200 × 450 | 800–3,000 | ≤ 45 bones (+ `jaw`, tiny arms) | **Huge boxy head** (about 30 % of length) with a jutting underbite of pixel teeth, comically tiny two-claw arms, a heavy armoured brow, a deep-teal and charcoal hide with a bone-pale throat, and a scarred snout pixel. Outdoors only (too big for doors) | `Roar` (30 f; stun ability, peak f14), `Attack_TailSwipe` (20 f, contact f10), `Stomp` (18 f, impact f9), `Rest` loop (60 f) |
| `SK_Dino_Sailback` | spinosaurus | 800 × 175 × 380 (incl. sail) | 800–3,000 | ≤ 45 (+ 3 `sail_` bones to flare it) | A long crocodile-like snout with interlocking teeth, a **tall stepped voxel sail** (`_M` R = sail pattern, dyeable), hooked thumb claws, olive and ochre with an ember sail. It shakes water off | `SailFlare` (24 f; intimidate ability), `Attack_Claw` (15 f, contact f7), `Swim` loop (30 f, authored for 400 cm/s), `Rest` loop |
| `SK_Dino_Nipper` | small pack predator | 125 × 50 × 75 | 200–800 | ≤ 25 | A tiny, cheeky, big-eyed biter that comes in **packs of 3–5** as wild pests, and is tameable as a pocket follower. Coral and cream | `Scurry` loop (12 f, authored for 500 cm/s), `Hop` (12 f) |
| `SK_Dino_Hornback` (optional) | horned charger | 600 × 150 × 260 | 600–2,000 | ≤ 40 | A stubby-armed charger with two thick brow horns and a bumpy hide, rust and charcoal | `Charge` loop (16 f, authored for 900 cm/s), `Attack_Ram` (18 f, contact f9) |

**Raptor top-up:** `Attack_Pounce` (24 f, in place; crouch, leap pose, land; **contact f14**; Unreal supplies the arc), `Roar` (30 f), `Rest` loop (60 f, lying down in the garden), `Sit` loop (60 f), `Sniff` (30 f), plus `Happy` and `Eat` if they weren't in batch 1.

**Dino accessories** (cosmetic): rigid static meshes that Unreal attaches to sockets, **one per species size class**, named `SM_DinoAcc_<Item>_<Species>`: `Collar` (dyeable; tamed marker), `Bow`, `FlowerCrown`, `SaddleBlanket` (dyeable), `Bandana`.

**Valley kit** (M2 map): blocks `SM_Block_Mud`, `SM_Block_JungleLeaves` (opaque), `SM_Block_Mossstone`. Props: `SM_Valley_Fern` (no alpha), `SM_Valley_Cycad`, `SM_Valley_Boulder_A/B`, `SM_Valley_Nest` (wild eggs), `SM_Node_Amber` (+ depleted), `SM_Node_FossilDig` (+ depleted), `SM_Valley_BoneArch`.

### 4.4 Batch 4: fashion (M3)
**Slot rules** (all on `SKEL_GF_Humanoid`, using the same bind pose, a 0.5 mv shell over the body or over the previous layer, rigid weights, `Outfit` slot with `_D` 64² + `_M` R/G dye, and tags in `qa.notes` for `DT_Outfits`):
- **Hair:** a shell over the head, which can hang down the back (weighted to `hair_back`). It must cover the base hair cap.
- **Top:** torso and arms (sleeves can go to the wrists). Coats and capes use `cape_01/02`.
- **Bottom:** pelvis and legs. Skirts and dresses use `skirt_f/skirt_b`. A dress is a Top + Bottom pair, or one Top flagged `covers=Bottom`.
- **Shoes:** foot plus the lower 2 mv of the calf.
- **Accessory:** a hat, glasses, a necklace, a bag or wings, rigid on `head`, `spine_02` or `hand_r`. Hats sit over any hair (hair under a hat is Unreal's job: there's a `Hair_UnderHat` variant flag).

**Starter list (33 pieces):** a proposal until the open question is answered Q7 (favourite fashion themes). Placeholder themes are Garden Party, Dino Explorer, Apex Predator, Cozy Night In, Beach Day and Glam Night.

| Slot | Pieces |
|---|---|
| Hair (6) | `Hair_LongWavy`, `Hair_Bob`, `Hair_HighPonytail`, `Hair_SpaceBuns`, `Hair_Braids`, `Hair_PixieCut` |
| Top (8) | `Top_GardenTee` (batch 1), `Top_ExplorerVest`, `Top_ClawMarkJacket` (three pixel claw slashes across the back), `Top_RaptorHoodie` (the hood has a toothy pixel jaw rim and a feather crest), `Top_CozySweater`, `Top_SunBlouse`, `Top_SequinTop` (`_E` glints), `Top_FloralDress` (`covers=Bottom`) |
| Bottom (6) | `Bottom_Jeans`, `Bottom_CargoShorts`, `Bottom_PleatedSkirt`, `Bottom_LoungePants`, `Bottom_SarongWrap`, `Bottom_GalaSkirt` |
| Shoes (5) | `Shoes_Sneakers`, `Shoes_HikingBoots`, `Shoes_Sandals`, `Shoes_FuzzySlippers`, `Shoes_Heels` |
| Accessory (8) | `Acc_SunHat`, `Acc_ExplorerHat`, `Acc_FangNecklace`, `Acc_SkullHairClip` (a tiny pixel rex skull), `Acc_HeartGlasses`, `Acc_FlowerCrown`, `Acc_Satchel`, `Acc_Tiara` |

**Runway and dressing room:**
- Static: `SM_Runway_Stage` (modular 2×1 block segments plus an end platform), `SM_Runway_Backdrop` (a separate `Logo` slot for a title image), `SM_Runway_JudgeDesk`, `SM_Runway_Spotlight` (`_E`), `SM_Dressing_Mirror`, `SM_Dressing_Rack`.
- Player anims: `Runway_Walk` loop (30 f, authored for 180 cm/s, hip sway), `Pose_01…04` (60 f each, holds), `Turn180` (20 f), `Wave` (30 f), `Spin` (24 f).
- **Judges (proposal):** the player's tamed predator species as the judges, sitting at the desk. They use the dino rigs plus accessories (`SM_DinoAcc_Monocle`, `SM_DinoAcc_Bowtie`) and new clips: `Judge_Sit` loop, `Judge_Clap` (24 f), `Judge_ScoreRaise` (18 f, holding up a scorecard prop `SM_Prop_ScoreCard` with a number-texture param), and `Judge_Gasp` (18 f).

### 4.5 Batch 5: arena, 3v3 one lane (M4, last)
Everything that changes by side gets a white **`TeamTint`** slot (ally `#4FC3F7`, enemy `#FF6B6B`, set by Unreal). Keep it visible from the third-person camera.

| Asset | Size (cm) | Tris | Notes |
|---|---|---|---|
| `SM_Arena_Tower` + `_Rubble` | 200 × 200 × 400 | 300–1,500 | a stone-and-wood watchtower with a crystal top (`_E`), banners in `TeamTint`, origin at the bottom centre, UCX |
| `SM_Arena_Core` + `_Rubble` | 300 × 300 × 500 | 500–2,000 | a giant crystal heart on a pedestal, `_E` plus `TeamTint` |
| `SM_Arena_SpawnPad` | 300 × 300 × 25 | 50–300 | `TeamTint` ring |
| `SM_Arena_LaneEdge_A/B`, `SM_Arena_Bush` (opaque), `SM_Arena_Pennant` | modular 1–2 blocks | 50–500 | the lane floor reuses hub blocks |
| `SK_Arena_Minion_Melee` | 50 × 50 × 90 | 150–600 | **"Pebblet"**: a little rock critter with a leaf hat. ≤ 20 bones. Idle, Walk (authored for 300 cm/s), Attack (12 f, contact f6), Hit, Poof (18 f; cozy "defeat" that shrinks into a puff) |
| `SK_Arena_Minion_Ranged` | 50 × 50 × 80 | 150–600 | **"Sproutling"**: a seed-pod critter that spits seeds. Same clips, Attack release f6 |
| `SM_Proj_Seed`, `SM_Proj_SeedBomb`, `SM_Proj_Pebble` | ≤ 25 | ≤ 100 | origin at the centre; the trails are Unreal VFX |
| `SM_Prop_GardenWand` | 75 long | 50–300 | the player's weapon, on the `prop_r` bone, `_E` tip |
| `SM_DinoAcc_Armor_<Species>` | per species | 100–800 | armour plates plus `TeamTint`, for the player's dinos and the bot team's dinos |

**Player arena anims** (on `SKEL_GF_Humanoid`): `Attack_Throw` (15 f, release f7), `Cast_1/2/3` (18 f each, release f10), `Ultimate` (36 f, release f20), `Hit`, `KO` (30 f, sits down dizzy, hold), `Recall` loop (60 f).
The rival bot heroes reuse the player skeleton with outfit pieces, plus the dinos with armour, so **no new rigs are needed**.
If the arena gets cut (PLAN.md fallback: tower defence with dinos), the tower, core, minions and projectiles still get used.

### 4.6 Batch 6: Beat Hop rhythm obby (M5)
**What it is:** the player runs and bunny-hops along a voxel course; bricks in 4–5 coloured lanes light up on the beat and has to land on the lit one in time (PLAN.md M5). The course is **generated in Unreal from chart data** (bricks placed as instances), so the artist makes the **pieces**, not levels. All bricks are exactly **1 block (100 cm)** wide and deep; lanes are 100 cm apart.

**Beat bricks** (static, bottom-centre origin, UCX box, 16 × 16 faces like every block, **with `_E` emissive masks**). Unreal drives glow per instance: dim (about 20 %) → approaching → **lit on the beat** → hit flash or miss. So paint `_D` as the brick at rest and `_E` as everything that glows.

| Asset | Lane colour (D base + E glow tint) | Glyph in the `_E` mask (colour-blind safe) | Notes |
|---|---|---|---|
| `SM_Block_Beat_L1` | rose `#E8577A` | dot (center 4 × 4 px) | |
| `SM_Block_Beat_L2` | sunflower `#F5C842` | horizontal bar | |
| `SM_Block_Beat_L3` | sky `#7EC8F2` | ring | |
| `SM_Block_Beat_L4` | lavender `#B48DE0` | diamond | |
| `SM_Block_Beat_L5` | coral `#FF8A65` | star (5-point, pixel) | Hard only |
| `SM_Block_Beat_Neutral` | stone/ink, no glow | none | filler and run-up bricks |
| `SM_Block_Beat_Hold` | uses lane colour via `_M` R | chevrons along its length | **1 × 3 blocks** (300 cm long, along X); Unreal scales to 2–4 |
| `SM_Block_Beat_Pad` | uses lane colour via `_M` R | up-arrow | launch pad, top 4 mv lower with a spring rim |
| `SM_Block_Beat_Switch` | uses lane colour via `_M` R | side arrow (mirrored in UE for left/right) | lane-change cue |
| `SM_Block_Beat_Crumble_A/B/C` | none (rest colour) | — | **3 broken pieces** of one brick (each ≤ 50 cm), origin at the brick's bottom centre so Unreal can swap and drop them on a miss |

Rules for beat bricks: top face = a **4 px bevel frame + the glyph** in `_E`; side faces glow only on a 2 px top band (so the lit lane reads from the run camera behind and above). The `_D` lane colour at rest uses the lane's mid shade; `_E` is pure greyscale (white = full glow) and Unreal tints it with the lane colour. Keep the five lanes distinguishable with glow off **and** in greyscale.

**Track kit** (static, whole-block footprints, bottom-centre origin, UCX where a player can touch it):
- `SM_Track_StartGate` (6 × 1 × 4 blocks: an arch with a countdown panel, separate `Screen` slot for a UI texture), `SM_Track_FinishGate` (same size, checkered pixel banner), `SM_Track_Checkpoint` (a 5 × 1 pad strip with a flag; `_E`).
- `SM_Track_Rail_Side` (1 × 1 low glowing edge rail, 4 mv tall, `_E` beat-pulse strip), `SM_Track_Pillar` (1 × 1 × 4 support under floating bricks), `SM_Track_CatchFloor` (4 × 4 tile, soft cloud/foam look, where you drop on a miss).
- Scenery (off-track only): `SM_Track_Speaker` (2 × 1 × 3, cone `_E` pulses), `SM_Track_FloatingNote_A/B` (pixel music notes, 1 block, `_E`), `SM_Track_DiscoCube` (1 block, `_E` facets), `SM_Track_Skyline_A/B` (big voxel silhouettes, 8–16 blocks, cheap, no collision).
- Hub piece: `SM_Furn_Jukebox` (1 × 1 × 2, `_E` lights, the hub reward that plays unlocked songs) and `SM_Gate_Beat` (2 × 1 × 3 hub gate, a speaker arch with a pixel note).

**Hit VFX note:** the effects are **built in Unreal (Niagara)**, not in Blender. The artist supplies only the pieces:
- `SM_VFX_Cube` (a 1 mv and a 2 mv cube, 12 tris each, white, for voxel-burst particles tinted per lane),
- `T_VFX_Judgement` (a 64 × 64 pixel-text sheet: PERFECT / GOOD / MISS / COMBO and digits 0–9, white on transparent, 1-bit alpha),
- `T_VFX_Sparkle` (16 × 16, 4-frame flipbook in a 64 × 16 strip).
Perfect = a big lane-coloured cube burst + ring; Good = smaller burst; Miss = grey puff + crumble pieces.

**Player animations** (on `SKEL_GF_Humanoid`, in place, 30 fps, §1.6 rules; batch 1's Jump_Start/Loop/Land stay for the hub):

| Clip | Frames | Loop | Notes |
|---|---|---|---|
| Bhop_Takeoff | 4 | no | very short crouch-pop so a chained hop doesn't feel delayed; **leave-ground f2** |
| Bhop_Air | 12 | yes | tucked, arms out for balance |
| Air_Strafe_L / Air_Strafe_R | 12 | yes | body leans 15–20° into the strafe (layered over Bhop_Air in UE) |
| Bhop_Land | 4 | no | tiny squash that blends straight back into Takeoff (**contact f1**) |
| Land_Perfect | 10 | no | landing with a little arm flourish (additive-safe) |
| Stumble | 12 | no | miss: wobble, arms windmill, recovers to run |
| Fall | 20 | yes | dropping off the track: flailing, cartoony |
| Land_Catch | 18 | no | bounce onto the catch floor and pop back up |
| Run_Fast | 16 | yes | authored for **600 cm/s** (the course reference speed) |
| Victory | 60 | no | song-clear dance, hold last frame |

Budgets: bricks 12–60 tris each; track kit 50–1,500 each; textures 16² for bricks, 64² to 128² for kit pieces. Same palette (§1.4), no vertex colours, pixel-snapped UVs.

---

## 5. Handoff

**Folders on the dev PC** (create any that are missing). This is the GFGame project only. PLAN.md suggested `ArtHandoff\`, but the Unreal side uses `handoff\`.
```
<repo>\handoff\inbox\<AssetName>\     <- you write here
<repo>\handoff\imported\<AssetName>\  <- Unreal side moves it here after QA
<repo>\handoff\rejected\<AssetName>\  <- with qa_notes.md
<repo>\handoff\style\                 <- palette, style sheet, mv_grid.blend
<repo>\handoff\license_ledger.csv     <- one row per asset (§1.9)
<repo>\blender\<AssetName>.blend      <- source files
<repo>\renders\                       <- previews, scale line-ups, icons work files
```
Don't edit `.gitignore` or commit anything; the Unreal side handles git and LFS.

**Per-asset folder contents:** `<AssetName>.fbx` (mesh, or mesh + armature with no animation), `A_<Base>_<Clip>.fbx` (one per clip), `T_*.png`, `preview.png`, `manifest.json` and `blender\<AssetName>.blend` (a copy). For items, add `T_Icon_<Name>.png`. One asset per folder, and the file name = the asset name. Re-exports overwrite the folder and bump `"version"`.

**Manifest:** schema `handoff/v1`. Required: `schema, version, asset_name, type, source_blend, files{}, format, units{}, orientation{}, dimensions_cm[] (UE X,Y,Z), tri_count, material_slots[], skeleton{}, unreal_target, provenance{}, status, qa{}`. Leave out any field this example doesn't show. Set `status` to `ready_for_import` only after the Blender QA checklist passes.

Example (dino):
```json
{
  "schema": "handoff/v1",
  "version": 1,
  "asset_name": "SK_Dino_Raptor",
  "type": "skeletal_mesh",
  "source_blend": "blender/SK_Dino_Raptor.blend",
  "files": {
    "mesh": "SK_Dino_Raptor.fbx",
    "animations": ["A_Dino_Raptor_Idle.fbx", "A_Dino_Raptor_Walk.fbx", "A_Dino_Raptor_Run.fbx",
                   "A_Dino_Raptor_Attack_Bite.fbx", "A_Dino_Raptor_Hit.fbx", "A_Dino_Raptor_Faint.fbx"],
    "textures": ["T_Dino_Raptor_D.png", "T_Dino_Raptor_M.png", "T_Dino_Raptor_Wild_D.png"]
  },
  "format": "fbx",
  "units": { "blender_unit_scale": 0.01, "length_unit": "CENTIMETERS", "fbx_unit_factor": 1.0, "apply_scalings": "FBX_SCALE_UNITS" },
  "orientation": { "facing_in_blender": "-Y", "expected_in_unreal": "+X", "up": "Z", "unreal_import": "Force Front X Axis" },
  "dimensions_cm": [300, 75, 150],
  "tri_count": 0,
  "material_slots": ["Body"],
  "skeleton": { "reuse_existing": null, "skeleton_name": "SKEL_Dino_Raptor", "root_bone": "root", "bone_count": 26, "rig_source": "hand_built" },
  "unreal_target": "/Game/Art/Dinos/Raptor/SK_Dino_Raptor",
  "provenance": { "tools": ["Blender 5.2.2"], "ai_generated": false, "generator": null, "license": "own work", "created_utc": "", "project": "GFGame" },
  "status": "ready_for_import",
  "qa": { "blender_checks_passed": true, "notes": "Idle 60f loop (f61=f1); Walk 24f loop @300cm/s; Run 16f loop @700cm/s; Attack_Bite contact f7; Faint holds last frame; bounds centre offset X=..cm" }
}
```
- **Blocks:** `"type": "static_mesh"`, `"skeleton": null`, and list all face textures (`T_Block_Grass_Top/Side.png`, `T_Block_Dirt.png`).
- **Outfit pieces:** `"skeleton": { "reuse_existing": "SKEL_GF_Humanoid", "root_bone": "root", "bone_count": 24, "rig_source": "hand_built" }`, with no animations, and the slot and tags in `qa.notes`.

**Blender QA before you hand off:**
- It's a cm scene (`scale_length 0.01`), and every object is at scale 1,1,1 with transforms applied.
- The origin is where §1.7 or the brief says, and the asset faces −Y.
- **Every cuboid is a whole number of mv**, and UVs snap to pixel corners at 1 px/mv. The palette is the only colour source, there's no vertex colour, and edges are hard.
- Block textures are exactly 16 × 16 and tile on every edge.
- The mesh is manifold, normals face outward, and there are no interior faces between merged cuboids.
- The tri budget is met. Only deform bones are exported (including the humanoid spare bones), with no leaf or `_end` bones, and the armature is named `root`.
- Weights are rigid (one bone per cuboid).
- One Action per clip, anims are in place, N-frame clips span f1–f(N+1), and loops end on a copy of f1.
- `preview.png` is saved, the manifest is filled in, and the ledger row is added.

**Unreal-side checks** (for reference; the Unreal side runs these):
- Import with **Force Front X Axis**.
- Bounds = `dimensions_cm` ±1 %, it faces +X, and the root bone scale is 1.0.
- Frame counts and lengths match `qa.notes`, and the anims run in place on the right skeleton.
- Textures are set to Nearest / 2D Pixels and look crisp, materials are assigned, and dye and tint masks respond.
- Outfits follow the body through Leader Pose with no z-fighting.
- Collision or a physics asset exists.
- The folder moves to `imported\`, or to `rejected\` with `qa_notes.md`.
