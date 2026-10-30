"""Headless import + verification of the artist's art handoff batch (handoff/inbox -> /Game/Art).

Run:
  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_art_batch.py" -unattended -nullrhi -nosplash
Env: GFART_BATCH=3b,7 (comma list) and/or GFART_ONLY=SM_Plot_ClaimFlag,... select the union of both; skeleton dependents follow.
     GFART_PROBE=<json from Tools/art_foot_probe.py> is required for skeletal assets with clips: every clip's posed-mesh min Z
     must stay >= -5 cm and the lowest clip must match the manifest's min_ground_z_cm (UE also checks foot/toe bone Z >= -5).
     Frames the manifest lists under "Intentionally below ground" (burrow, swim, perch clips) are exempt from both checks.
Skeletal manifests with skeleton.sockets get USkeletalMeshSocket entries (bone + offset_cm, Blender bone space -> UE bone space).
Batches 3b and 7 get materials: M_GF_Prop (Diffuse, DyeMask R, Emissive mask _E) instances per mesh, M_GF_TeamTint for TeamTint slots. Translucent blocks (manifest unreal_material_hint, e.g. SM_Block_Water) get M_GF_Water (Opacity, PanSpeedU/V).
Writes Saved/ArtImport/ue_report.json. Manifest types: static_mesh, skeletal_mesh, texture_set (UI PNGs, batch 8+). Settings: Force Front X Axis, convert scene + scene units,
uniform scale 1, no redundant-key removal (scale tracks kept), no materials (slots keep names).
"""
import json, math, os, re, traceback
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
# GFART_INBOX overrides the inbox (e.g. a worktree importing from the main checkout's handoff folder)
INBOX = os.environ.get("GFART_INBOX", "").strip() or os.path.join(PROJ, "handoff", "inbox")
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ORDER = [
    # batch 1
    "SM_Block_Grass", "SM_Block_WoodPlank", "SM_Block_Stone", "SK_Player_Base", "SK_Outfit_Top_GardenTee", "SK_Dino_Raptor",
    # batch 2 (M1 hub kit). Assets that use textures_shared_from come after the asset that ships the texture.
    "SM_Block_ClayBrick", "SM_Block_Cobblestone", "SM_Block_Dirt", "SM_Block_Fabric", "SM_Block_Farmland", "SM_Block_Glass",
    "SM_Block_Leaves", "SM_Block_Log", "SM_Block_PathDirt", "SM_Block_Sand", "SM_Block_StoneBrick", "SM_Block_Thatch",
    "SM_Furn_Bed_Wood", "SM_Furn_Chair_Wood", "SM_Furn_CraftingBench", "SM_Furn_Door_Wood", "SM_Furn_FlowerPot",
    "SM_Furn_Lamp_Lantern", "SM_Furn_Table_Wood", "SM_Furn_Wardrobe",
    "SM_Crop_Berry_S0", "SM_Crop_Berry_S1", "SM_Crop_Berry_S2", "SM_Crop_Berry_S3",
    "SM_Crop_Fiber_S0", "SM_Crop_Fiber_S1", "SM_Crop_Fiber_S2", "SM_Crop_Fiber_S3",
    "SM_Crop_Flower_S0", "SM_Crop_Flower_S1", "SM_Crop_Flower_S2", "SM_Crop_Flower_S3", "SM_Crop_Flower_S3_Rare",
    "SM_Node_ClayPit", "SM_Node_ClayPit_Empty", "SM_Node_Rock", "SM_Node_Rock_Pebbles", "SM_Node_Tree", "SM_Node_Tree_Stump",
    "SM_Gate_Arena", "SM_Gate_Runway", "SM_Gate_Valley",
    # batch 8 (landlord kit + co-op UI). SK_Player_Base v2 is also tagged batch 8 (it keeps its batch-1 slot above).
    "SM_Land_Board", "SM_Rent_Board", "SM_Mailbox", "SM_Mailbox_Flag",
    "SM_Plot_ClaimFlag", "SM_Plot_CornerPost", "SM_Plot_BorderSegment", "SM_Plot_Signpost",
    "SM_Sign_ForRent", "SM_Sign_RentDue", "SM_Sign_RentDue_Sleepy",
    "SM_Overgrowth_Vine_A", "SM_Overgrowth_Vine_B", "SM_Overgrowth_Vine_C",
    "SM_Item_Letter", "SM_Item_Parcel",
    "UI_Batch8_Coop",
    # batch 3b (taming / cooking / fishing). SK_Player_Base v3 and SK_Dino_Raptor v3 keep their batch-1 slots above.
    "SM_Crop_Steakroot_S0", "SM_Crop_Steakroot_S1", "SM_Crop_Steakroot_S2", "SM_Crop_Steakroot_S3",
    "SM_Crop_Emberpepper_S0", "SM_Crop_Emberpepper_S1", "SM_Crop_Emberpepper_S2", "SM_Crop_Emberpepper_S3",
    "SM_Crop_Thornmelon_S0", "SM_Crop_Thornmelon_S1", "SM_Crop_Thornmelon_S2", "SM_Crop_Thornmelon_S3",
    "SM_Item_Steakroot", "SM_Item_Emberpepper", "SM_Item_Thornmelon", "SM_Item_Fizzberry", "SM_Item_RiverFish",
    "SM_Item_GoldenCarp", "SM_Item_Glowgrub", "SM_Item_WildEgg", "SM_Item_SmokedHaunch", "SM_Item_PetitCake",
    "SM_Item_JerkyBites", "SM_Item_EmberRoast", "SM_Item_FishSkewer", "SM_Item_GrubJelly",
    "SM_Furn_Cookpot", "SM_Node_FishingSpot", "SM_Prop_FishingRod", "SM_Prop_Bobber",
    "SM_Node_RottenLog", "SM_Node_RottenLog_Dug", "SM_Node_WildNest", "SM_Node_WildNest_Empty",
    "SM_Block_Water",
    "UI_Batch3b_Taming",
    # batch 7 (arena / colosseum). SK_Player_Base v4 and SK_Dino_Raptor v4 keep their batch-1 slots above.
    "SM_Colo_Wall_Straight", "SM_Colo_Wall_Curve", "SM_Colo_Stands", "SM_Colo_Gate", "SM_Colo_GateBars", "SM_Colo_Pillar",
    "SM_Colo_Torch", "SM_Colo_Banner", "SM_Colo_Balcony", "SM_Colo_RivalBalcony", "SM_Colo_StartSpot",
    "SM_Colo_StartSpot_Front", "SM_Colo_Hourglass",
    "SM_Deco_CupTrophy_Bronze", "SM_Deco_CupTrophy_Silver", "SM_Deco_CupTrophy_Gold", "SM_Deco_CupTrophy_Champion",
    "SM_Deco_TrophyCase",
    "SM_VFX_RainCloud", "SM_VFX_RainDrop", "SM_VFX_Vine_A", "SM_VFX_Vine_B", "SM_VFX_Vine_C", "SM_VFX_StoneWall",
    "SM_VFX_SparkBolt", "SM_VFX_BloomPetal", "SM_VFX_FizzBomb", "SM_VFX_EmberFleck", "SM_VFX_WhistleNote",
    "SM_VFX_RoarRing", "SM_VFX_SwipeArc", "SM_VFX_SprayCone", "SM_VFX_ChargeDust",
    "SM_Block_ArenaSand",
    "UI_Batch7_Arena_v2",  # v2 is the complete set (v1 folder left in the inbox, not imported)
    # new dinos v1 (batch newdinos_v1): each on its own skeleton
    "SK_Dino_Tusksaw", "SK_Dino_Ripwing", "SK_Dino_Scythepaw", "SK_Dino_Lanternmaw",
    # Big Grumble (batch big_grumble_v1): on the shared SKEL_Dino_Rex (see SHARED_SKELETONS)
    "SK_Dino_BigGrumble",
    # hazards (batch hazards1): the two set pieces ship the textures, the modular pieces reuse them (textures_shared_from)
    "SM_Hazard_LavaBridge", "SM_Hazard_DeadlyForest",
    "SM_Hazard_LavaBridge_Seg4m", "SM_Hazard_ForestTree_A", "SM_Hazard_ForestTree_B", "SM_Hazard_ForestSnag",
    "SM_Hazard_ForestThornBush", "SM_Hazard_ForestUndergrowth",
]
# Skeletons shared across species live in their own folder, not in the folder of whichever mesh created them.
# The first import creates the skeleton there; later imports reuse it. Pre-clean never deletes a shared
# skeleton, because other meshes (e.g. a future SK_Dino_Rex) depend on it.
SHARED_SKELETONS = {"SKEL_Dino_Rex": "/Game/Art/Dinos/Rex"}
_ALL = list(ORDER)
_only = [a.strip() for a in os.environ.get("GFART_ONLY", "").split(",") if a.strip()]
_batches = [b.strip() for b in os.environ.get("GFART_BATCH", "").split(",") if b.strip()]
def _batch_of(a): return str(json.load(open(os.path.join(INBOX, a, "manifest.json"), encoding="utf-8")).get("batch", "1"))
if _only or _batches:
    # GFART_ONLY=SK_Dino_Raptor re-imports just that asset; GFART_BATCH=2 or 3b,7 imports manifests with that "batch"
    # (no field = batch 1). Both together select the union (e.g. batch 3b + 7 plus the batch-8 fixes by name).
    ORDER = [a for a in ORDER if a in _only or _batch_of(a) in _batches]
if _only or _batches:
    # A re-imported skeleton is deleted and re-created, so every asset that reuses it must come along
    # (e.g. SK_Player_Base v2 in batch 8 drags SK_Outfit_Top_GardenTee, which reuses SKEL_GF_Humanoid).
    def _man(a): return json.load(open(os.path.join(INBOX, a, "manifest.json"), encoding="utf-8"))
    _defs = {(_man(a).get("skeleton") or {}).get("skeleton_name") for a in ORDER if not (_man(a).get("skeleton") or {}).get("reuse_existing")}
    _defs.discard(None)
    ORDER = [a for a in _ALL if a in ORDER or (_man(a).get("skeleton") or {}).get("reuse_existing") in _defs]
CLIP_LIMIT_Z = -5.0  # cm: no clip may sink the posed mesh below this (manifest "limit -5")
_probe_path = os.environ.get("GFART_PROBE", "").strip()
PROBE = json.load(open(_probe_path, encoding="utf-8")) if _probe_path else {}
PROP_MAT_BATCHES = ("3b", "7")
HAZARD_MAT_BATCHES = ("hazards1",)  # per-slot materials from unreal_material_hint (M_GF_Prop, M_GF_Lava)
MAT_DIR = "/Game/Art/Materials"
BAD_PATH = re.compile(r"[a-z]:[\\/]users[\\/]", re.I)  # source paths must not leak a username
DIM_TOL = 0.05  # cm
report = {"assets": {}, "log": []}

def log(msg):
    unreal.log("GFArt " + str(msg))
    report["log"].append(str(msg))

def try_set(obj, prop, val):
    try:
        obj.set_editor_property(prop, val)
    except Exception as e:
        log("warn: cannot set {}.{}: {}".format(type(obj).__name__, prop, e))

try:
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")
    log("legacy FBX importer requested (Interchange.FeatureFlags.Import.FBX false)")
except Exception as e:
    log("warn: cvar " + str(e))

def common(d):
    try_set(d, "force_front_x_axis", True)
    try_set(d, "convert_scene", True)
    try_set(d, "convert_scene_unit", True)
    try_set(d, "import_uniform_scale", 1.0)

def fbx_options(kind, skeleton=None):
    ui = unreal.FbxImportUI()
    try_set(ui, "automated_import_should_detect_type", False)
    try_set(ui, "import_materials", False)
    try_set(ui, "import_textures", False)
    try_set(ui, "create_physics_asset", False)
    if kind == "static":
        try_set(ui, "import_mesh", True); try_set(ui, "import_as_skeletal", False); try_set(ui, "import_animations", False)
        try_set(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        d = ui.static_mesh_import_data
        try_set(d, "combine_meshes", True); try_set(d, "auto_generate_collision", False)
        common(d)
    elif kind == "skel":
        try_set(ui, "import_mesh", True); try_set(ui, "import_as_skeletal", True); try_set(ui, "import_animations", False)
        try_set(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        if skeleton is not None:
            try_set(ui, "skeleton", skeleton)
        d = ui.skeletal_mesh_import_data
        try_set(d, "import_morph_targets", False)
        common(d)
        common(ui.anim_sequence_import_data)
    else:
        try_set(ui, "import_mesh", False); try_set(ui, "import_as_skeletal", True); try_set(ui, "import_animations", True)
        try_set(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
        try_set(ui, "skeleton", skeleton)
        d = ui.anim_sequence_import_data
        try_set(d, "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        try_set(d, "remove_redundant_keys", False)
        try_set(d, "import_bone_tracks", True)
        try_set(d, "import_meshes_in_bone_hierarchy", False)
        try_set(d, "delete_existing_morph_target_curves", False)
        common(d)
    return ui

def run_import(path, dest, name, options):
    t = unreal.AssetImportTask()
    t.filename = path; t.destination_path = dest; t.destination_name = name
    t.automated = True; t.replace_existing = True; t.save = True
    if options is not None:
        t.options = options
    AT.import_asset_tasks([t])
    obj = dest + "/" + name
    if not EAL.does_asset_exist(obj):
        raise RuntimeError("import produced nothing at " + obj + " (task paths: " + str(list(t.imported_object_paths)) + ")")
    return unreal.load_asset(obj)

def import_textures(man, folder, dest):
    out = []
    for tex in man["files"].get("textures", []):
        name = os.path.splitext(tex)[0]
        t = run_import(os.path.join(folder, tex), dest + "/Textures", name, None)
        try_set(t, "filter", unreal.TextureFilter.TF_NEAREST)
        if hint_slots(man):
            # per-slot hints (hazards, banner/trophy v2): pixel-art LOD group, tiling textures wrap
            try_set(t, "lod_group", unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
            try_set(t, "address_x", unreal.TextureAddress.TA_WRAP)
            try_set(t, "address_y", unreal.TextureAddress.TA_WRAP)
        if name.startswith("T_Icon_"):
            ui_texture(t, name)
        elif name.endswith("_M") or name.endswith("_E"):  # dye masks and greyscale emissive masks are linear
            try_set(t, "srgb", False)
            try_set(t, "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        EAL.save_loaded_asset(t)
        out.append(name)
    return out

def ui_texture(t, name):
    """UI settings: UserInterface2D (RGBA), UI LOD group, no mips (so non-power-of-two sizes are fine), nearest."""
    try_set(t, "compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    try_set(t, "lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    try_set(t, "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    try_set(t, "filter", unreal.TextureFilter.TF_NEAREST)
    try_set(t, "never_stream", True)
    try_set(t, "srgb", not name.endswith("_M"))  # _M masks are linear

def ui_settings_ok(t, name):
    g = t.get_editor_property
    return (g("compression_settings") == unreal.TextureCompressionSettings.TC_EDITOR_ICON
            and g("lod_group") == unreal.TextureGroup.TEXTUREGROUP_UI
            and g("mip_gen_settings") == unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
            and g("filter") == unreal.TextureFilter.TF_NEAREST
            and bool(g("srgb")) == (not name.endswith("_M")))

def tex_size(t):
    for a, b in (("blueprint_get_size_x", "blueprint_get_size_y"),):
        try: return [int(getattr(t, a)()), int(getattr(t, b)())]
        except Exception: pass
    try:
        s = t.get_editor_property("source")
        return [int(s.get_size_x()), int(s.get_size_y())]
    except Exception as e:
        log("info: texture size " + str(e)); return None

def do_texture_set(name, man, folder, r):
    """texture_set manifest: PNGs straight into unreal_target (no mesh), UI settings, size per manifest."""
    dest = man["unreal_target"]
    sizes = {}
    for k, v in man.get("textures", {}).items():
        for f in v.get("files", []):
            sizes[os.path.splitext(f)[0]] = v.get("size")
    r["textures"] = {}
    chk = r["checks"]
    for tex in man["files"]["textures"]:
        tname = os.path.splitext(tex)[0]
        tr = {}
        r["textures"][tname] = tr
        try:
            t = run_import(os.path.join(folder, tex), dest, tname, None)
            ui_texture(t, tname)
            EAL.save_loaded_asset(t)
            tr["size"] = tex_size(t)
            tr["size_expected"] = sizes.get(tname)
            tr["sources"] = source_files(t)
            tr["settings_ok"] = ui_settings_ok(t, tname)
            tr["pass"] = (tr["settings_ok"] and tr["size"] == tr["size_expected"] and tr["sources"] is not None
                          and not any(BAD_PATH.search(p) for p in tr["sources"]))
        except Exception as e:
            tr["error"] = str(e) + "\n" + traceback.format_exc(); tr["pass"] = False
    r["texture_count"] = len(r["textures"])
    chk["texture_count"] = r["texture_count"] == len(man["files"]["textures"]) and r["texture_count"] > 0
    chk["textures"] = all(v["pass"] for v in r["textures"].values())
    r["failed_textures"] = [k for k, v in r["textures"].items() if not v["pass"]]

def source_files(asset):
    try:
        return [str(f) for f in asset.get_editor_property("asset_import_data").extract_filenames()]
    except Exception as e:
        log("warn: source paths " + str(e)); return None

def shared_textures(man):
    """files.textures_shared_from: reuse the textures the named asset already imported (no copy)."""
    src = man["files"].get("textures_shared_from")
    out = []
    if not src:
        return out
    sman = json.load(open(os.path.join(INBOX, src, "manifest.json"), encoding="utf-8"))
    sdest = sman["unreal_target"].rsplit("/", 1)[0] + "/Textures/"
    for tex in man["files"].get("textures_shared", []):
        p = sdest + os.path.splitext(tex)[0]
        out.append({"texture": p, "exists": EAL.does_asset_exist(p), "from": src})
    return out

def vec(v): return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]

def tri_count_static(m):
    for fn in ("get_num_triangles",):
        try: return getattr(m, fn)(0)
        except Exception: pass
    try: return unreal.StaticMeshEditorSubsystem().get_number_verts(m, 0) and None
    except Exception: return None

def tri_count_skel(m):
    try:
        dm = unreal.DynamicMesh()
        opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
        lod = unreal.GeometryScriptMeshReadLOD()
        res = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(m, dm, opts, lod)
        return int(dm.get_triangle_count())
    except Exception as e:
        log("info: skeletal tri count via GeometryScript unavailable: " + str(e))
    try:
        md = m.clone_mesh_description(0) if hasattr(m, "clone_mesh_description") else m.get_mesh_description(0)
        return md.get_triangle_count()
    except Exception as e:
        log("info: skeletal tri count via mesh description unavailable: " + str(e))
    return None

def skel_dims(m):
    for fn in ("get_imported_bounds", "get_bounds"):
        try:
            b = getattr(m, fn)()
            e = b.box_extent
            return [round(2*e.x, 3), round(2*e.y, 3), round(2*e.z, 3)], vec(b.origin), fn
        except Exception:
            pass
    return None, None, None

def ref_pose(skeleton):
    return unreal.AnimPoseExtensions.get_reference_pose(skeleton)

def pose_bones(pose):
    return [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]

def bone_t(pose, b, space):
    return unreal.AnimPoseExtensions.get_bone_pose(pose, b, space)

def anim_frames(anim):
    try: return int(unreal.AnimationLibrary.get_num_frames(anim))
    except Exception: pass
    return None

def anim_keys(anim):
    try: return int(anim.get_editor_property("number_of_sampled_keys"))
    except Exception: return None

def pose_at(anim, frame):
    opts = unreal.AnimPoseEvaluationOptions()
    try_set(opts, "evaluation_type", unreal.AnimDataEvalType.RAW)
    try_set(opts, "should_retarget", False)
    try_set(opts, "extract_root_motion", False)
    return unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim, frame, opts)

def tdiff(a, b):
    dt = (a.translation - b.translation).length()
    q1, q2 = a.rotation, b.rotation
    dot = abs(q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w)
    ang = math.degrees(2*math.acos(min(1.0, dot)))
    ds = (a.scale3d - b.scale3d).length()
    return dt, ang, ds

def parse_exempt(notes):
    """Manifest "Intentionally below ground (...): Burrow_In f6-f19, Burrowed_Loop all frames; ..." ->
    {clip: None (every frame) or [(first, last)] authored frames, 1-based}. Missing section = no exemptions."""
    m = re.search(r"Intentionally below ground \([^)]*\): ([^;]*)", notes)
    out = {}
    if not m:
        return out
    for item in m.group(1).split(","):
        item = item.strip()
        mm = re.match(r"([A-Za-z_]+) (?:all frames|f(\d+)-f(\d+))$", item)
        if not mm:
            raise RuntimeError("cannot parse ground exemption: " + item)
        if mm.group(2):
            out.setdefault(mm.group(1), []).append((int(mm.group(2)), int(mm.group(3))))
        else:
            out[mm.group(1)] = None
    return out

def exempt_frame(ex, short, i):
    """i = 0-based frame index (UE frame i, probe per_frame[i] = authored frame i + 1)."""
    if short not in ex:
        return False
    rng = ex[short]
    return rng is None or any(a - 1 <= i <= b - 1 for a, b in rng)

# Blender bone space has Y along the bone; after the FBX import the same along-bone direction is the
# UE bone's local +Z (checked against the ref-pose world position of lure_bulb and talon_l). Only
# along-bone offsets are verified, so any X/Z component is refused rather than guessed.
def socket_offset_ue(off):
    if abs(float(off[0])) > 1e-6 or abs(float(off[2])) > 1e-6:
        raise ValueError("socket offset_cm with X/Z components is not supported: " + str(off))
    return unreal.Vector(0.0, 0.0, float(off[1]))

def socket_dict(sockets):
    """skeleton.sockets as {name: {bone, offset_cm}} or as a list of {name, bone, loc_cm, rot}."""
    if isinstance(sockets, dict):
        return sockets
    out = {}
    for sd in sockets:
        if any(abs(float(a)) > 1e-6 for a in sd.get("rot", [0, 0, 0])):
            raise ValueError("socket rot is not supported: " + str(sd))
        out[sd["name"]] = {"bone": sd["bone"], "offset_cm": sd.get("loc_cm", [0, 0, 0])}
    return out

def add_skel_sockets(m, sockets, r):
    sockets = socket_dict(sockets)
    got = {}
    for sname, sd in sockets.items():
        # SocketName and BoneName are read-only from Python: parent via set_socket_parent,
        # add the unnamed socket, then rename it.
        if m.find_socket(sname) is not None:
            m.remove_socket(sname)
        sock = unreal.SkeletalMeshSocket(m)
        sock.set_socket_parent(m, sd["bone"])
        sock.set_editor_property("relative_location", socket_offset_ue(sd.get("offset_cm", [0, 0, 0])))
        m.add_socket(sock, False)
        if not m.rename_socket(sock.get_editor_property("socket_name"), sname):
            log("socket rename failed for " + sname)
    EAL.save_loaded_asset(m)
    for sname, sd in sockets.items():
        s2 = m.find_socket(sname)
        got[sname] = None if s2 is None else {"bone": str(s2.get_editor_property("bone_name")), "loc": vec(s2.get_editor_property("relative_location"))}
        if s2 is not None:
            try:
                pose = unreal.AnimPoseExtensions.get_reference_pose(m.skeleton)
                bt = unreal.AnimPoseExtensions.get_bone_pose(pose, sd["bone"], unreal.AnimPoseSpaces.WORLD)
                got[sname]["ref_world"] = vec(bt.transform_location(s2.get_editor_property("relative_location")))
            except Exception as e:
                log("info: socket ref_world " + str(e))
    r["sockets"] = got
    r["checks"]["sockets"] = all(got.get(n) is not None and got[n]["bone"] == sd["bone"] for n, sd in sockets.items())

def parse_clips(notes):
    clips = {}
    for m in re.finditer(r"(?:^|; )([A-Za-z_]+) (\d+)f (loop|one-shot)", notes):
        clips[m.group(1)] = {"frames": int(m.group(2)), "loop": m.group(3) == "loop"}
    return clips

def delete_redirectors():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(package_paths=["/Game/Art"], recursive_paths=True, class_paths=[unreal.TopLevelAssetPath("/Script/CoreUObject", "ObjectRedirector")])
    for a in reg.get_assets(flt):
        p = str(a.package_name)
        log("deleting redirector " + p)
        EAL.delete_asset(p)

skeletons = {}

def do_static(name, man, folder, r):
    dest = man["unreal_target"].rsplit("/", 1)[0]
    m = run_import(os.path.join(folder, man["files"]["mesh"]), dest, name, fbx_options("static"))
    r["textures"] = import_textures(man, folder, dest)
    r["textures_shared"] = shared_textures(man)
    for tname in r["textures"]:
        t = unreal.load_asset(dest + "/Textures/" + tname)
        r.setdefault("texture_sources", {})[tname] = source_files(t)
    r["mesh_sources"] = source_files(m)
    bb = m.get_bounding_box()
    dims = [round(bb.max.x - bb.min.x, 3), round(bb.max.y - bb.min.y, 3), round(bb.max.z - bb.min.z, 3)]
    r["dims_cm"] = dims; r["bounds_min"] = vec(bb.min); r["bounds_max"] = vec(bb.max)
    r["tris"] = tri_count_static(m)
    r["simple_collision"] = None  # StaticMeshEditorSubsystem is unavailable in -run=pythonscript; count convex elems instead
    try:
        ag = m.get_editor_property("body_setup").get_editor_property("agg_geom")
        r["collision_convex_elems"] = len(ag.get_editor_property("convex_elems"))
    except Exception as e: log("info: agg_geom " + str(e))
    r["material_slots"] = [str(s.material_slot_name) for s in m.static_materials]
    chk = r["checks"]
    chk["dims"] = all(abs(a - b) <= DIM_TOL for a, b in zip(dims, man["dimensions_cm"]))
    chk["tris"] = r["tris"] == man["tri_count"] if r["tris"] is not None else "n/a"
    chk["slots"] = r["material_slots"] == man["material_slots"]
    cnote = re.search(r"Collision: ([^;]*)", man["qa"].get("notes", ""))
    ncol = re.match(r"\s*(\d+) UCX", cnote.group(1)) if cnote else None
    if ncol:
        r["collision_expected"] = int(ncol.group(1))  # "11 UCX boxes (UCX_..._00, UCX_..._01 ...)"
    else:
        r["collision_expected"] = len(re.findall(r"UCX_", cnote.group(1))) if cnote else None
    if "/Crops/" in man["unreal_target"]:
        chk["collision"] = "n/a"  # crops get no collision (brief 1.8); count still reported
    elif r["collision_expected"] is not None:
        chk["collision"] = r.get("collision_convex_elems") == r["collision_expected"]
    else:
        chk["collision"] = (r["simple_collision"] or 0) >= 1 or (r.get("collision_convex_elems") or 0) >= 1
    # min Z must match the manifest's min_ground_z_cm (0 for ground props; hinge/wall/centred pivots are negative on purpose)
    r["min_z_expected"] = float(man["qa"].get("min_ground_z_cm", 0.0))
    chk["min_z"] = abs(bb.min.z - r["min_z_expected"]) <= DIM_TOL
    if r["textures_shared"]:
        chk["shared_textures_exist"] = all(t["exists"] for t in r["textures_shared"])
    paths = (r["mesh_sources"] or []) + [p for v in r.get("texture_sources", {}).values() for p in (v or [])]
    r["bad_source_paths"] = [p for p in paths if BAD_PATH.search(p)]
    chk["source_paths_clean"] = not r["bad_source_paths"] and r["mesh_sources"] is not None
    # FBX empties named SOCKET_<Name> become static mesh sockets <Name> (manifest notes list them; "SOCKET_Trophy_1..4" = 1 to 4)
    exp_s = expected_sockets(man["qa"].get("notes", ""))
    try:
        # StaticMesh.Sockets is protected in Python; probe the expected names via find_socket
        r["sockets"] = [n for n in exp_s if m.find_socket(n) is not None]
    except Exception as e:
        r["sockets"] = None; log("info: sockets " + str(e))
    if exp_s:
        r["sockets_expected"] = exp_s
        chk["sockets"] = r["sockets"] is not None and all(s in r["sockets"] for s in exp_s)

def expected_sockets(notes):
    out = []
    for m in re.finditer(r"SOCKET_(\w+?)(?:_(\d+)\.\.(\d+)|\b)", notes):
        if m.group(2):
            out += ["{}_{}".format(m.group(1), i) for i in range(int(m.group(2)), int(m.group(3)) + 1)]
        else:
            out.append(m.group(1))
    return sorted(set(out))

def do_skel(name, man, folder, r):
    dest = man["unreal_target"].rsplit("/", 1)[0]
    sk = man["skeleton"]
    reuse = sk.get("reuse_existing")
    # reuse_existing is a skeleton name imported earlier in this run, or a full UE path to a committed skeleton.
    reuse_path = reuse if reuse and reuse.startswith("/Game/") else None
    if reuse_path:
        reuse = reuse_path.rsplit("/", 1)[1]
    skel_name = reuse or sk["skeleton_name"]
    existing = skeletons.get(skel_name)
    if existing is None and reuse_path and EAL.does_asset_exist(reuse_path):
        existing = unreal.load_asset(reuse_path)
    shared_dir = SHARED_SKELETONS.get(skel_name)
    skel_dir = shared_dir or dest
    if existing is None:
        for cand in ["/Game/Art/Characters/" + skel_name, skel_dir + "/" + skel_name]:
            if EAL.does_asset_exist(cand):
                existing = unreal.load_asset(cand); break
    if reuse and existing is None:
        raise RuntimeError("reuse skeleton " + reuse + " not imported yet")
    m = run_import(os.path.join(folder, man["files"]["mesh"]), dest, name, fbx_options("skel", existing))
    skel = m.skeleton
    if existing is None:
        old = skel.get_path_name().split(".")[0]
        new = skel_dir + "/" + skel_name
        if old != new:
            if not EAL.rename_asset(old, new):
                raise RuntimeError("rename skeleton failed " + old + " -> " + new)
            # The rename does not dirty the mesh, so force the save; otherwise the mesh keeps pointing
            # at the redirector deleted below and loads without a skeleton.
            EAL.save_loaded_asset(m, False)
            delete_redirectors()
        skel = unreal.load_asset(new)
    skeletons[skel_name] = skel
    r["skeleton"] = skel.get_path_name()
    r["textures"] = import_textures(man, folder, dest)
    pose = ref_pose(skel)
    bones = pose_bones(pose)
    r["bone_count"] = len(bones); r["bones"] = bones
    root = bones[0] if bones else None
    rt = bone_t(pose, root, unreal.AnimPoseSpaces.LOCAL)
    r["root_bone"] = root; r["root_ref_scale"] = vec(rt.scale3d); r["root_ref_loc"] = vec(rt.translation)
    dims, origin, how = skel_dims(m)
    r["dims_cm"] = dims; r["bounds_origin"] = origin; r["bounds_fn"] = how
    r["tris"] = tri_count_skel(m)
    try: r["material_slots"] = [str(s.material_slot_name) for s in m.materials]
    except Exception: r["material_slots"] = None
    chk = r["checks"]
    chk["root_name"] = root == sk["root_bone"]
    chk["root_scale_1"] = all(abs(s - 1.0) < 1e-4 for s in r["root_ref_scale"])
    chk["bone_count"] = len(bones) == sk["bone_count"]
    chk["dims"] = dims is not None and all(abs(a - b) <= DIM_TOL for a, b in zip(dims, man["dimensions_cm"]))
    chk["tris"] = r["tris"] == man["tri_count"] if r["tris"] is not None else "n/a"
    chk["slots"] = r["material_slots"] == man["material_slots"] if r["material_slots"] is not None else "n/a"
    if reuse:
        chk["shares_skeleton"] = skel.get_path_name() == skeletons[reuse].get_path_name()
    if reuse_path:
        chk["reuse_path"] = skel.get_path_name().split(".")[0] == reuse_path
    if shared_dir:
        chk["shared_skeleton_path"] = skel.get_path_name().split(".")[0] == shared_dir + "/" + skel_name

    if sk.get("sockets"):
        add_skel_sockets(m, sk["sockets"], r)
    clips_expected = parse_clips(man["qa"]["notes"])
    exempt = parse_exempt(man["qa"]["notes"])
    if exempt:
        r["ground_exempt"] = {k: ("all frames" if v is None else v) for k, v in exempt.items()}
        unknown = [k for k in exempt if k not in clips_expected]
        chk["ground_exempt_clips_known"] = not unknown
    r["clips"] = {}
    for af in man["files"]["animations"]:
        aname = os.path.splitext(af)[0]
        short = aname.split("_", 3)[-1] if aname.count("_") >= 3 else aname
        cr = {}
        r["clips"][aname] = cr
        try:
            anim = run_import(os.path.join(folder, af), dest + "/Anims", aname, fbx_options("anim", skel))
            nf = anim_frames(anim); nk = anim_keys(anim)
            cr["frames"] = nf; cr["keys"] = nk
            try: cr["length_s"] = round(anim.get_play_length(), 4)
            except Exception: pass
            cr["skeleton"] = anim.get_editor_property("skeleton").get_path_name()
            cr["skeleton_ok"] = cr["skeleton"] == skel.get_path_name()
            exp = clips_expected.get(short)
            cr["expected"] = exp
            if exp:
                cr["frames_ok"] = nf == exp["frames"]
            # per-frame: root scale, min bone Z (component space), foot/toe bone Z
            feet = [b for b in bones if re.search(r"foot|toe|ball", b, re.I)]
            per = []
            max_root_scale_dev = 0.0
            for f in range(0, (nf or 0) + 1):
                p = pose_at(anim, f)
                rs = bone_t(p, root, unreal.AnimPoseSpaces.LOCAL).scale3d
                max_root_scale_dev = max(max_root_scale_dev, abs(rs.x-1), abs(rs.y-1), abs(rs.z-1))
                ws = {b: bone_t(p, b, unreal.AnimPoseSpaces.WORLD).translation.z for b in feet}
                per.append({"f": f, "feet_z": {b: round(z, 3) for b, z in ws.items()}})
            cr["root_scale_max_dev"] = round(max_root_scale_dev, 6)
            cr["per_frame"] = per
            if short in exempt:
                cr["ground_exempt"] = "all frames" if exempt[short] is None else exempt[short]
            fz = [z for p in per for z in p["feet_z"].values()]
            cr["min_feet_bone_z"] = round(min(fz), 3) if fz else None
            fzc = [z for i, p in enumerate(per) if not exempt_frame(exempt, short, i) for z in p["feet_z"].values()]
            cr["min_feet_bone_z_checked"] = round(min(fzc), 3) if fzc else None
            cr["feet_above_limit"] = (min(fzc) >= CLIP_LIMIT_Z) if fzc else "n/a"
            pr = PROBE.get(name, {}).get(aname)
            if pr is not None:
                cr["probe_min_z"] = pr["worst_min_z_cm"]
                pc = [f["mesh_min_z"] for i, f in enumerate(pr["per_frame"]) if not exempt_frame(exempt, short, i)]
                cr["probe_min_z_checked"] = min(pc) if pc else None
                cr["probe_above_limit"] = (min(pc) >= CLIP_LIMIT_Z) if pc else True
            # loop closure
            p0 = pose_at(anim, 0); pn = pose_at(anim, nf)
            worst = (0.0, 0.0, 0.0, None)
            for b in bones:
                dt, da, ds = tdiff(bone_t(p0, b, unreal.AnimPoseSpaces.LOCAL), bone_t(pn, b, unreal.AnimPoseSpaces.LOCAL))
                if dt + da + ds > worst[0] + worst[1] + worst[2]:
                    worst = (dt, da, ds, b)
            cr["first_last_diff"] = {"cm": round(worst[0], 4), "deg": round(worst[1], 4), "scale": round(worst[2], 6), "bone": worst[3]}
            if exp and exp["loop"]:
                cr["loop_closes"] = worst[0] < 0.05 and worst[1] < 0.05 and worst[2] < 1e-3
        except Exception as e:
            cr["error"] = str(e) + "\n" + traceback.format_exc()
    for cn, cr in r["clips"].items():
        ok = "error" not in cr and cr.get("frames_ok", False) and cr.get("skeleton_ok", False) and cr.get("loop_closes", True) is True and cr.get("root_scale_max_dev", 1) < 1e-4
        ok = ok and cr.get("feet_above_limit") in (True, "n/a") and cr.get("probe_above_limit", True) is True
        cr["pass"] = ok
    chk["clips"] = all(cr["pass"] for cr in r["clips"].values()) if r["clips"] else "n/a"
    chk["clip_count"] = len(r["clips"]) == len(clips_expected) if man["files"]["animations"] else "n/a"
    # min Z per manifest: the lowest posed-mesh point over all clips (Blender foot probe) must match min_ground_z_cm
    if man["files"]["animations"] and man["qa"].get("min_ground_z_cm") is not None:
        r["min_z_expected"] = float(man["qa"]["min_ground_z_cm"])
        if any("probe_min_z" not in cr for cr in r["clips"].values()):
            pz = [None]
        else:
            # exempt frames left out; a clip that is exempt on every frame has no checked minimum
            pz = [cr["probe_min_z_checked"] for cr in r["clips"].values() if cr["probe_min_z_checked"] is not None]
        if pz and None not in pz:
            r["probe_min_z"] = min(pz)
            chk["min_z"] = abs(min(pz) - r["min_z_expected"]) <= DIM_TOL
            chk["clips_above_limit"] = min(pz) >= CLIP_LIMIT_Z
        else:
            r["min_z_note"] = "no foot-probe data for every clip: run Tools/art_foot_probe.py and pass GFART_PROBE"
            chk["min_z"] = False

PLAYER_MAT_DIR = "/Game/Art/Characters/Player/Materials"

def _mexpr(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)

def build_dye_mask_master(path, default_d, default_m):
    """M_GF_DyeMask: Base = D, then lerp to D*Dye_R by M.R, D*Dye_B by M.B, and Dye_G last by M.G
    (Basewear_M v2: R = tank dye, G = player-colour trim, B = shorts dye; trim overrides). Dye_* default white = no tint."""
    MEL = unreal.MaterialEditingLibrary
    d, n = path.rsplit("/", 1)
    mat = AT.create_asset(n, d, unreal.Material, unreal.MaterialFactoryNew())
    td = _mexpr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -100)
    td.set_editor_property("parameter_name", "Diffuse")
    td.set_editor_property("texture", default_d)
    td.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    tm = _mexpr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 250)
    tm.set_editor_property("parameter_name", "Mask")
    tm.set_editor_property("texture", default_m)
    tm.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    cur = td; cur_pin = "RGB"; y = -300
    for ch, pin in (("R", "R"), ("B", "B"), ("G", "G")):  # apply order R, B, G (trim last)
        col = _mexpr(mat, unreal.MaterialExpressionVectorParameter, -600, y)
        col.set_editor_property("parameter_name", "Dye_" + ch)
        col.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
        mul = _mexpr(mat, unreal.MaterialExpressionMultiply, -350, y)
        MEL.connect_material_expressions(td, "RGB", mul, "A")
        MEL.connect_material_expressions(col, "", mul, "B")
        lerp = _mexpr(mat, unreal.MaterialExpressionLinearInterpolate, -150, y)
        MEL.connect_material_expressions(cur, cur_pin, lerp, "A")
        MEL.connect_material_expressions(mul, "", lerp, "B")
        MEL.connect_material_expressions(tm, pin, lerp, "Alpha")
        cur = lerp; cur_pin = ""; y += 200
    MEL.connect_material_property(cur, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _mexpr(mat, unreal.MaterialExpressionConstant, -150, 400)
    rough.set_editor_property("r", 0.9)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    try_set(mat, "used_with_skeletal_mesh", True)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat

def player_materials(man, r):
    """Master + 3 instances for SK_Player_Base slots (Skin, Face, Basewear), assigned to the mesh."""
    MEL = unreal.MaterialEditingLibrary
    tdir = man["unreal_target"].rsplit("/", 1)[0] + "/Textures/"
    master = build_dye_mask_master(PLAYER_MAT_DIR + "/M_GF_DyeMask", unreal.load_asset(tdir + "T_Player_Basewear_D"), unreal.load_asset(tdir + "T_Player_Basewear_M"))
    inst = {}
    for slot in ("Skin", "Face", "Basewear"):
        mi = AT.create_asset("MI_Player_" + slot, PLAYER_MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)
        MEL.set_material_instance_texture_parameter_value(mi, "Diffuse", unreal.load_asset(tdir + "T_Player_" + slot + "_D"))
        MEL.set_material_instance_texture_parameter_value(mi, "Mask", unreal.load_asset(tdir + "T_Player_" + slot + "_M"))
        EAL.save_loaded_asset(mi)
        inst[slot] = mi
    m = unreal.load_asset(man["unreal_target"])
    mats = list(m.materials)
    for i, sm in enumerate(mats):
        s = str(sm.material_slot_name)
        if s in inst:
            sm.material_interface = inst[s]
            mats[i] = sm
    m.set_editor_property("materials", mats)
    EAL.save_loaded_asset(m)
    m = unreal.load_asset(man["unreal_target"])
    got = {str(s.material_slot_name): (s.material_interface.get_name() if s.material_interface else None) for s in m.materials}
    r["player_materials"] = got
    r["checks"]["player_materials"] = all(got.get(s) == "MI_Player_" + s for s in ("Skin", "Face", "Basewear"))
PLAYER_MATS = ["MI_Player_Skin", "MI_Player_Face", "MI_Player_Basewear", "M_GF_DyeMask"]

def owned_assets(man):
    if man["type"] == "texture_set":
        return [man["unreal_target"] + "/" + os.path.splitext(t)[0] for t in man["files"]["textures"]]
    dest = man["unreal_target"].rsplit("/", 1)[0]
    out = [dest + "/" + os.path.splitext(a)[0] for a in man["files"].get("animations", [])]
    out = [dest.replace(dest, dest + "/Anims") + "/" + os.path.basename(a) for a in out]
    out += [dest + "/Textures/" + os.path.splitext(t)[0] for t in man["files"].get("textures", [])]
    if man.get("asset_name") == "SK_Player_Base":
        # dependents first: mesh -> material instances -> master -> textures
        tex = [a for a in out if "/Textures/" in a]
        out = [a for a in out if "/Textures/" not in a] + [man["unreal_target"]] + [PLAYER_MAT_DIR + "/" + m for m in PLAYER_MATS] + tex
    elif man["type"] == "static_mesh" and str(man.get("batch")) in PROP_MAT_BATCHES:
        tex = [a for a in out if "/Textures/" in a]
        out = [a for a in out if "/Textures/" not in a] + [man["unreal_target"], prop_mi_path(man)] + extra_slot_mi_paths(man) + tex
    elif man["type"] == "static_mesh" and str(man.get("batch")) in HAZARD_MAT_BATCHES:
        tex = [a for a in out if "/Textures/" in a]
        own = set(os.path.splitext(t)[0] for t in man["files"].get("textures", []))
        mis = sorted(set(p for slot, h in hint_slots(man).items() for p in [hazard_mi_path(man, h)] if h.get("texture") in own))
        out = [a for a in out if "/Textures/" not in a] + [man["unreal_target"]] + mis + tex
    else:
        out.append(man["unreal_target"])
    sk = man.get("skeleton") or {}
    if sk and not sk.get("reuse_existing") and sk["skeleton_name"] not in SHARED_SKELETONS:
        out.append(dest + "/" + sk["skeleton_name"])
        out.append(man["unreal_target"] + "_Skeleton")
    return out

def prop_mi_path(man):
    return man["unreal_target"].rsplit("/", 1)[0] + "/Materials/MI_" + man["asset_name"][3:]

def _png(path, w, h, rgb):
    """Tiny solid-colour PNG (stdlib only) used as a material default texture."""
    import struct, zlib
    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

def default_tex(name, rgb, linear):
    p = MAT_DIR + "/" + name
    if EAL.does_asset_exist(p):
        return unreal.load_asset(p)
    src = os.path.join(OUT, name + ".png")
    _png(src, 4, 4, rgb)
    t = run_import(src, MAT_DIR, name, None)
    try_set(t, "filter", unreal.TextureFilter.TF_NEAREST)
    if linear:
        try_set(t, "srgb", False)
        try_set(t, "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    EAL.save_loaded_asset(t)
    return t

def _tparam(mat, pname, tex, sampler, x, y):
    n = _mexpr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    n.set_editor_property("parameter_name", pname)
    n.set_editor_property("texture", tex)
    n.set_editor_property("sampler_type", sampler)
    return n

def build_prop_master(path):
    """M_GF_Prop: Base = lerp(D, D*Dye_R, DyeMask.R); Emissive = Base * Emissive.R * EmissiveColor * EmissiveStrength.
    DyeMask/Emissive default to a black linear mask and EmissiveStrength to 0, so a plain prop is just its _D."""
    MEL = unreal.MaterialEditingLibrary
    white = default_tex("T_GF_DefaultWhite", (255, 255, 255), False)
    black = default_tex("T_GF_MaskBlack", (0, 0, 0), True)
    d, n = path.rsplit("/", 1)
    mat = AT.create_asset(n, d, unreal.Material, unreal.MaterialFactoryNew())
    td = _tparam(mat, "Diffuse", white, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1000, -200)
    tm = _tparam(mat, "DyeMask", black, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1000, 150)
    te = _tparam(mat, "Emissive", black, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1000, 450)
    dye = _mexpr(mat, unreal.MaterialExpressionVectorParameter, -700, -350)
    dye.set_editor_property("parameter_name", "Dye_R"); dye.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    mul = _mexpr(mat, unreal.MaterialExpressionMultiply, -450, -300)
    MEL.connect_material_expressions(td, "RGB", mul, "A"); MEL.connect_material_expressions(dye, "", mul, "B")
    lerp = _mexpr(mat, unreal.MaterialExpressionLinearInterpolate, -250, -200)
    MEL.connect_material_expressions(td, "RGB", lerp, "A"); MEL.connect_material_expressions(mul, "", lerp, "B")
    MEL.connect_material_expressions(tm, "R", lerp, "Alpha")
    MEL.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    ecol = _mexpr(mat, unreal.MaterialExpressionVectorParameter, -700, 600)
    ecol.set_editor_property("parameter_name", "EmissiveColor"); ecol.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    estr = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -700, 800)
    estr.set_editor_property("parameter_name", "EmissiveStrength"); estr.set_editor_property("default_value", 0.0)
    e1 = _mexpr(mat, unreal.MaterialExpressionMultiply, -450, 400)
    MEL.connect_material_expressions(lerp, "", e1, "A"); MEL.connect_material_expressions(te, "R", e1, "B")
    e2 = _mexpr(mat, unreal.MaterialExpressionMultiply, -300, 500)
    MEL.connect_material_expressions(e1, "", e2, "A"); MEL.connect_material_expressions(ecol, "", e2, "B")
    e3 = _mexpr(mat, unreal.MaterialExpressionMultiply, -150, 600)
    MEL.connect_material_expressions(e2, "", e3, "A"); MEL.connect_material_expressions(estr, "", e3, "B")
    MEL.connect_material_property(e3, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = _mexpr(mat, unreal.MaterialExpressionConstant, -150, 200)
    rough.set_editor_property("r", 0.9)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat

def build_team_tint_master(path):
    """M_GF_TeamTint: TeamTint slots (UV 0-1, no atlas). Base = TeamColor; Emissive = TeamColor * TeamGlow (UE pulses TeamGlow)."""
    MEL = unreal.MaterialEditingLibrary
    d, n = path.rsplit("/", 1)
    mat = AT.create_asset(n, d, unreal.Material, unreal.MaterialFactoryNew())
    col = _mexpr(mat, unreal.MaterialExpressionVectorParameter, -600, 0)
    col.set_editor_property("parameter_name", "TeamColor"); col.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    glow = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -600, 250)
    glow.set_editor_property("parameter_name", "TeamGlow"); glow.set_editor_property("default_value", 0.0)
    mul = _mexpr(mat, unreal.MaterialExpressionMultiply, -300, 150)
    MEL.connect_material_expressions(col, "", mul, "A"); MEL.connect_material_expressions(glow, "", mul, "B")
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = _mexpr(mat, unreal.MaterialExpressionConstant, -300, 350)
    rough.set_editor_property("r", 0.9)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat

def build_water_master(path):
    """M_GF_Water: translucent block (manifest unreal_material_hint). Base = Diffuse sampled at panned UVs
    (PanSpeedU/PanSpeedV UV per second); Opacity = scalar Opacity. The texture itself stays opaque."""
    MEL = unreal.MaterialEditingLibrary
    white = default_tex("T_GF_DefaultWhite", (255, 255, 255), False)
    d, n = path.rsplit("/", 1)
    mat = AT.create_asset(n, d, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    uvc = _mexpr(mat, unreal.MaterialExpressionTextureCoordinate, -1100, -200)
    su = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -1100, 0)
    su.set_editor_property("parameter_name", "PanSpeedU"); su.set_editor_property("default_value", 0.05)
    sv = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -1100, 150)
    sv.set_editor_property("parameter_name", "PanSpeedV"); sv.set_editor_property("default_value", 0.0)
    app = _mexpr(mat, unreal.MaterialExpressionAppendVector, -900, 50)
    MEL.connect_material_expressions(su, "", app, "A"); MEL.connect_material_expressions(sv, "", app, "B")
    pan = _mexpr(mat, unreal.MaterialExpressionPanner, -700, -100)
    MEL.connect_material_expressions(uvc, "", pan, "Coordinate"); MEL.connect_material_expressions(app, "", pan, "Speed")
    td = _tparam(mat, "Diffuse", white, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -450, -150)
    MEL.connect_material_expressions(pan, "", td, "UVs")
    MEL.connect_material_property(td, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    op = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -450, 200)
    op.set_editor_property("parameter_name", "Opacity"); op.set_editor_property("default_value", 0.65)
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    rough = _mexpr(mat, unreal.MaterialExpressionConstant, -450, 350)
    rough.set_editor_property("r", 0.2)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def hint_slots(man):
    """unreal_material_hint entries keyed by material slot name (hazards, banner/trophy v2). Water-style flat hints -> {}."""
    h = man.get("unreal_material_hint") or {}
    slots = man.get("material_slots") or []
    return {k: v for k, v in h.items() if isinstance(v, dict) and k in slots}

def hazard_mi_path(man, h):
    """One instance per texture set, shared by every hazard piece: T_Hazard_LavaRock_D -> MI_Hazard_LavaRock."""
    base = h["texture"][2:] if h["texture"].startswith("T_") else h["texture"]
    if base.endswith("_D"):
        base = base[:-2]
    return man["unreal_target"].rsplit("/", 1)[0] + "/Materials/MI_" + base

BANNER_PATTERNS = ("Plain", "Claw", "Egg", "Gem")

def extra_slot_mi_paths(man):
    """v2 Pattern slot (banner: one instance per default pattern) and Glass slot (trophy case)."""
    hs = hint_slots(man)
    d = man["unreal_target"].rsplit("/", 1)[0] + "/Materials/MI_" + man["asset_name"][3:]
    out = []
    if "Pattern" in hs:
        out += [d + "_Pattern_" + p for p in BANNER_PATTERNS]
    if "Glass" in hs:
        out.append(d + "_Glass")
    return out

def _strength(h, default=0.0):
    m = re.search(r"\*\s*E\s*\*\s*([0-9.]+)", str(h.get("emissive_color", "")))
    return float(m.group(1)) if m else default

def _srgb_hex(hexs):
    hexs = hexs.lstrip("#")
    def lin(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(lin(int(hexs[0:2], 16)), lin(int(hexs[2:4], 16)), lin(int(hexs[4:6], 16)), 1.0)

def _load_or_create_mi(path):
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    d, n = path.rsplit("/", 1)
    return AT.create_asset(n, d, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())

def build_lava_master(path):
    """M_GF_Lava: opaque lava (hazards1 Lava hint). D and E sampled at panned UVs (PanSpeedU/V UV per second);
    Base = D; Emissive = D * E.R * (EmissiveStrength + PulseAmount * sin(2 pi PulseHz t)). Defaults pulse 2.0-3.5 at 0.25 Hz."""
    MEL = unreal.MaterialEditingLibrary
    white = default_tex("T_GF_DefaultWhite", (255, 255, 255), False)
    black = default_tex("T_GF_MaskBlack", (0, 0, 0), True)
    d, n = path.rsplit("/", 1)
    mat = AT.create_asset(n, d, unreal.Material, unreal.MaterialFactoryNew())
    uvc = _mexpr(mat, unreal.MaterialExpressionTextureCoordinate, -1300, -200)
    su = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -1300, 0)
    su.set_editor_property("parameter_name", "PanSpeedU"); su.set_editor_property("default_value", 0.0)
    sv = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -1300, 150)
    sv.set_editor_property("parameter_name", "PanSpeedV"); sv.set_editor_property("default_value", 0.02)
    app = _mexpr(mat, unreal.MaterialExpressionAppendVector, -1100, 50)
    MEL.connect_material_expressions(su, "", app, "A"); MEL.connect_material_expressions(sv, "", app, "B")
    pan = _mexpr(mat, unreal.MaterialExpressionPanner, -900, -100)
    MEL.connect_material_expressions(uvc, "", pan, "Coordinate"); MEL.connect_material_expressions(app, "", pan, "Speed")
    td = _tparam(mat, "Diffuse", white, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -650, -250)
    te = _tparam(mat, "Emissive", black, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -650, 100)
    MEL.connect_material_expressions(pan, "", td, "UVs"); MEL.connect_material_expressions(pan, "", te, "UVs")
    MEL.connect_material_property(td, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    tm = _mexpr(mat, unreal.MaterialExpressionTime, -1100, 450)
    hz = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -1100, 600)
    hz.set_editor_property("parameter_name", "PulseHz"); hz.set_editor_property("default_value", 0.25)
    tmul = _mexpr(mat, unreal.MaterialExpressionMultiply, -900, 500)
    MEL.connect_material_expressions(tm, "", tmul, "A"); MEL.connect_material_expressions(hz, "", tmul, "B")
    sine = _mexpr(mat, unreal.MaterialExpressionSine, -750, 500)  # period 1: sin(2 pi x)
    MEL.connect_material_expressions(tmul, "", sine, "")
    amp = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -750, 650)
    amp.set_editor_property("parameter_name", "PulseAmount"); amp.set_editor_property("default_value", 0.75)
    smul = _mexpr(mat, unreal.MaterialExpressionMultiply, -600, 550)
    MEL.connect_material_expressions(sine, "", smul, "A"); MEL.connect_material_expressions(amp, "", smul, "B")
    estr = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -600, 750)
    estr.set_editor_property("parameter_name", "EmissiveStrength"); estr.set_editor_property("default_value", 2.75)
    add = _mexpr(mat, unreal.MaterialExpressionAdd, -450, 600)
    MEL.connect_material_expressions(estr, "", add, "A"); MEL.connect_material_expressions(smul, "", add, "B")
    e1 = _mexpr(mat, unreal.MaterialExpressionMultiply, -400, 200)
    MEL.connect_material_expressions(td, "RGB", e1, "A"); MEL.connect_material_expressions(te, "R", e1, "B")
    e2 = _mexpr(mat, unreal.MaterialExpressionMultiply, -250, 350)
    MEL.connect_material_expressions(e1, "", e2, "A"); MEL.connect_material_expressions(add, "", e2, "B")
    MEL.connect_material_property(e2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = _mexpr(mat, unreal.MaterialExpressionConstant, -250, 0)
    rough.set_editor_property("r", 0.9)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat

def build_glass_master(path):
    """M_GF_Glass: translucent pane (trophy case Glass hint). Base = Diffuse; Opacity = scalar Opacity (0.3);
    roughness 0.1, specular 0.5."""
    MEL = unreal.MaterialEditingLibrary
    white = default_tex("T_GF_DefaultWhite", (255, 255, 255), False)
    d, n = path.rsplit("/", 1)
    mat = AT.create_asset(n, d, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    td = _tparam(mat, "Diffuse", white, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -500, -150)
    MEL.connect_material_property(td, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    op = _mexpr(mat, unreal.MaterialExpressionScalarParameter, -500, 150)
    op.set_editor_property("parameter_name", "Opacity"); op.set_editor_property("default_value", 0.3)
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    rough = _mexpr(mat, unreal.MaterialExpressionConstant, -300, 300)
    rough.set_editor_property("r", 0.1)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = _mexpr(mat, unreal.MaterialExpressionConstant, -300, 400)
    spec.set_editor_property("r", 0.5)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat

def _tex_paths(man):
    """Texture short name -> UE path, own textures plus files.textures_shared_from."""
    dest = man["unreal_target"].rsplit("/", 1)[0]
    tex = {os.path.splitext(t)[0]: dest + "/Textures/" + os.path.splitext(t)[0] for t in man["files"].get("textures", [])}
    src = man["files"].get("textures_shared_from")
    if src:
        sman = json.load(open(os.path.join(INBOX, src, "manifest.json"), encoding="utf-8"))
        for t in man["files"].get("textures_shared", []):
            t = os.path.splitext(t)[0]
            tex[t] = sman["unreal_target"].rsplit("/", 1)[0] + "/Textures/" + t
    return tex

def _set_slots(man, by_slot):
    m = unreal.load_asset(man["unreal_target"])
    mats = list(m.get_editor_property("static_materials"))
    for i, sm in enumerate(mats):
        s = str(sm.get_editor_property("material_slot_name"))
        if s in by_slot:
            sm.set_editor_property("material_interface", by_slot[s])
            mats[i] = sm
    m.set_editor_property("static_materials", mats)
    EAL.save_loaded_asset(m)
    m = unreal.load_asset(man["unreal_target"])
    return {str(s.material_slot_name): (s.material_interface.get_name() if s.material_interface else None) for s in m.static_materials}

def hazard_materials(name, man, r):
    """hazards1: one instance per hint slot (shared across pieces by texture). Lava slots (panner hint) -> M_GF_Lava,
    the rest -> M_GF_Prop (Diffuse, Emissive mask, EmissiveStrength from 'D * E * k')."""
    MEL = unreal.MaterialEditingLibrary
    tex = _tex_paths(man)
    by_slot = {}
    params = {}
    ok = True
    for slot, h in hint_slots(man).items():
        mi = _load_or_create_mi(hazard_mi_path(man, h))
        lava = "panner_speed_uv_per_s" in h
        MEL.set_material_instance_parent(mi, master("M_GF_Lava", build_lava_master) if lava else master("M_GF_Prop", build_prop_master))
        d = tex.get(h["texture"]); e = tex.get(h.get("emissive")) if h.get("emissive") else None
        ok = ok and d is not None and EAL.does_asset_exist(d) and (not h.get("emissive") or (e is not None and EAL.does_asset_exist(e)))
        if d: MEL.set_material_instance_texture_parameter_value(mi, "Diffuse", unreal.load_asset(d))
        if e: MEL.set_material_instance_texture_parameter_value(mi, "Emissive", unreal.load_asset(e))
        p = {"mi": mi.get_name(), "parent": "M_GF_Lava" if lava else "M_GF_Prop", "diffuse": h["texture"], "emissive": h.get("emissive")}
        if lava:
            spd = list(h["panner_speed_uv_per_s"]) + [0.0, 0.0]
            MEL.set_material_instance_scalar_parameter_value(mi, "PanSpeedU", float(spd[0]))
            MEL.set_material_instance_scalar_parameter_value(mi, "PanSpeedV", float(spd[1]))
            MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", 2.75)
            MEL.set_material_instance_scalar_parameter_value(mi, "PulseAmount", 0.75)
            MEL.set_material_instance_scalar_parameter_value(mi, "PulseHz", 0.25)
            p["pan_uv"] = [MEL.get_material_instance_scalar_parameter_value(mi, "PanSpeedU"), MEL.get_material_instance_scalar_parameter_value(mi, "PanSpeedV")]
            ok = ok and abs(p["pan_uv"][1] - float(spd[1])) < 1e-5
        elif e:
            k = _strength(h, 1.0)
            MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", k)
            p["emissive_strength"] = MEL.get_material_instance_scalar_parameter_value(mi, "EmissiveStrength")
        EAL.save_loaded_asset(mi)
        by_slot[slot] = mi
        params[slot] = p
    got = _set_slots(man, by_slot)
    r["materials"] = got
    r["material_params"] = params
    ok = ok and all(got.get(s) == by_slot[s].get_name() for s in by_slot) and set(by_slot) == set(man["material_slots"])
    r["checks"]["materials"] = ok

def extra_slot_materials(name, man, r):
    """v2 banner/trophy: Main keeps MI_<Name> on M_GF_Prop (Dye_R is the player/lining colour input, i.e.
    D * lerp(1, Dye_R, M.R)); Pattern -> MI_<Name>_Pattern_<P> on M_GF_Prop (PatternD/PatternM as Diffuse/DyeMask),
    Plain assigned; Glass -> MI_<Name>_Glass on M_GF_Glass (Opacity from the hint)."""
    MEL = unreal.MaterialEditingLibrary
    hs = hint_slots(man)
    if not hs:
        return
    tex = _tex_paths(man)
    dmi = man["unreal_target"].rsplit("/", 1)[0] + "/Materials/MI_" + man["asset_name"][3:]
    by_slot = {}
    ok = r["checks"].get("materials", True) is True
    extra = {}
    main = hs.get("Main")
    if main:
        mi = unreal.load_asset(prop_mi_path(man))
        if main.get("texture") in tex: MEL.set_material_instance_texture_parameter_value(mi, "Diffuse", unreal.load_asset(tex[main["texture"]]))
        if main.get("mask") in tex: MEL.set_material_instance_texture_parameter_value(mi, "DyeMask", unreal.load_asset(tex[main["mask"]]))
        lining = (main.get("defaults") or {}).get("LiningColor")
        if lining:
            MEL.set_material_instance_vector_parameter_value(mi, "Dye_R", _srgb_hex(lining))
            extra["Main_Dye_R_default"] = lining
        EAL.save_loaded_asset(mi)
    if "Pattern" in hs:
        pm = {}
        for pname in BANNER_PATTERNS:
            mi = _load_or_create_mi(dmi + "_Pattern_" + pname)
            MEL.set_material_instance_parent(mi, master("M_GF_Prop", build_prop_master))
            pd, pk = "T_Colo_Banner_Pattern_%s_D" % pname, "T_Colo_Banner_Pattern_%s_M" % pname
            ok = ok and pd in tex and pk in tex and EAL.does_asset_exist(tex[pd]) and EAL.does_asset_exist(tex[pk])
            MEL.set_material_instance_texture_parameter_value(mi, "Diffuse", unreal.load_asset(tex[pd]))
            MEL.set_material_instance_texture_parameter_value(mi, "DyeMask", unreal.load_asset(tex[pk]))
            EAL.save_loaded_asset(mi)
            pm[pname] = mi
        by_slot["Pattern"] = pm["Plain"]
        extra["Pattern"] = sorted(m.get_name() for m in pm.values())
    if "Glass" in hs:
        h = hs["Glass"]
        mi = _load_or_create_mi(dmi + "_Glass")
        gm = master("M_GF_Glass", build_glass_master)
        MEL.set_material_instance_parent(mi, gm)
        ok = ok and h["texture"] in tex
        MEL.set_material_instance_texture_parameter_value(mi, "Diffuse", unreal.load_asset(tex[h["texture"]]))
        MEL.set_material_instance_scalar_parameter_value(mi, "Opacity", float(h.get("opacity", 0.3)))
        EAL.save_loaded_asset(mi)
        by_slot["Glass"] = mi
        extra["Glass"] = {"blend_mode": str(gm.get_editor_property("blend_mode")),
                          "opacity": MEL.get_material_instance_scalar_parameter_value(mi, "Opacity")}
        ok = ok and "TRANSLUCENT" in extra["Glass"]["blend_mode"].upper() and abs(extra["Glass"]["opacity"] - float(h.get("opacity", 0.3))) < 1e-4
    got = _set_slots(man, by_slot)
    r["materials"] = got
    r["material_params_v2"] = extra
    ok = ok and all(got.get(s) == by_slot[s].get_name() for s in by_slot) and got.get("Main") == "MI_" + man["asset_name"][3:]
    r["checks"]["materials"] = ok

_masters = {}
def master(name, builder):
    p = MAT_DIR + "/" + name
    if name not in _masters:
        _masters[name] = unreal.load_asset(p) if EAL.does_asset_exist(p) else builder(p)
    return _masters[name]

def team_tint_mi():
    p = MAT_DIR + "/MI_GF_TeamTint"
    if EAL.does_asset_exist(p):
        return unreal.load_asset(p)
    mi = AT.create_asset("MI_GF_TeamTint", MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master("M_GF_TeamTint", build_team_tint_master))
    EAL.save_loaded_asset(mi)
    return mi

def prop_materials(name, man, r):
    """Batch 3b/7 static meshes: MI_<Name> (M_GF_Prop: _D, _M dye mask, _E emissive) on the atlas slot (Main/All),
    MI_GF_TeamTint on TeamTint slots. Pattern (banner art) and Glass slots stay empty for gameplay/VFX code."""
    MEL = unreal.MaterialEditingLibrary
    dest = man["unreal_target"].rsplit("/", 1)[0]
    own = [os.path.splitext(t)[0] for t in man["files"].get("textures", [])]
    tex = {}
    for t in own:
        tex[t] = dest + "/Textures/" + t
    src = man["files"].get("textures_shared_from")
    if src:
        sman = json.load(open(os.path.join(INBOX, src, "manifest.json"), encoding="utf-8"))
        for t in man["files"].get("textures_shared", []):
            t = os.path.splitext(t)[0]
            tex[t] = sman["unreal_target"].rsplit("/", 1)[0] + "/Textures/" + t
    diff = [t for t in tex if not t.startswith("T_Icon_") and not t.endswith("_E") and not t.endswith("_M")]
    emis = [t for t in tex if t.endswith("_E")]
    mask = [t for t in tex if t.endswith("_M")]
    mi = AT.create_asset(prop_mi_path(man).rsplit("/", 1)[1], dest + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    hint = man.get("unreal_material_hint") or {}
    water = str(hint.get("blend_mode", "")).lower() == "translucent"
    if water:
        # Translucent block (SM_Block_Water): opacity + UV panner from the manifest hint
        wm = master("M_GF_Water", build_water_master)
        MEL.set_material_instance_parent(mi, wm)
        spd = list(hint.get("panner_speed_uv_per_s", [0.05, 0.0])) + [0.0, 0.0]
        MEL.set_material_instance_scalar_parameter_value(mi, "Opacity", float(hint.get("opacity", 0.65)))
        MEL.set_material_instance_scalar_parameter_value(mi, "PanSpeedU", float(spd[0]))
        MEL.set_material_instance_scalar_parameter_value(mi, "PanSpeedV", float(spd[1]))
    else:
        MEL.set_material_instance_parent(mi, master("M_GF_Prop", build_prop_master))
    if diff:
        MEL.set_material_instance_texture_parameter_value(mi, "Diffuse", unreal.load_asset(tex[diff[0]]))
    if mask and not water:
        MEL.set_material_instance_texture_parameter_value(mi, "DyeMask", unreal.load_asset(tex[mask[0]]))
    if emis and not water:
        MEL.set_material_instance_texture_parameter_value(mi, "Emissive", unreal.load_asset(tex[emis[0]]))
        MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", 2.0)
    EAL.save_loaded_asset(mi)
    m = unreal.load_asset(man["unreal_target"])
    mats = list(m.get_editor_property("static_materials"))
    for i, sm in enumerate(mats):
        s = str(sm.get_editor_property("material_slot_name"))
        if s in ("Main", "All"):
            sm.set_editor_property("material_interface", mi)
        elif s == "TeamTint":
            sm.set_editor_property("material_interface", team_tint_mi())
        mats[i] = sm
    m.set_editor_property("static_materials", mats)
    EAL.save_loaded_asset(m)
    m = unreal.load_asset(man["unreal_target"])
    got = {str(s.material_slot_name): (s.material_interface.get_name() if s.material_interface else None) for s in m.static_materials}
    r["materials"] = got
    r["material_params"] = {"diffuse": diff[:1], "dye_mask": mask[:1], "emissive": emis[:1]}
    ok = all(got.get(s) == mi.get_name() for s in got if s in ("Main", "All"))
    ok = ok and all(got.get(s) == "MI_GF_TeamTint" for s in got if s == "TeamTint")
    ok = ok and bool(diff) and all(EAL.does_asset_exist(tex[t]) for t in diff + mask + emis)
    if water:
        r["material_params"]["water"] = {"blend_mode": str(wm.get_editor_property("blend_mode")),
            "opacity": MEL.get_material_instance_scalar_parameter_value(mi, "Opacity"),
            "pan_u": MEL.get_material_instance_scalar_parameter_value(mi, "PanSpeedU"),
            "pan_v": MEL.get_material_instance_scalar_parameter_value(mi, "PanSpeedV")}
        w = r["material_params"]["water"]
        ok = ok and "TRANSLUCENT" in w["blend_mode"].upper() and abs(w["opacity"] - float(hint.get("opacity", 0.65))) < 1e-3 \
            and abs(w["pan_u"] - float(spd[0])) < 1e-4
    r["checks"]["materials"] = ok

# Clean re-import: replace-existing on skeletal meshes re-creates a default skeleton, so delete this
# batch's own assets first (dependents first, skeletons last). Nothing outside the manifests is touched.
mans = {n: json.load(open(os.path.join(INBOX, n, "manifest.json"), encoding="utf-8")) for n in ORDER}
doomed = []
for n in reversed(ORDER):
    doomed += [a for a in owned_assets(mans[n]) if "SKEL_" not in a and not a.endswith("_Skeleton")]
for n in ORDER:
    doomed += [a for a in owned_assets(mans[n]) if "SKEL_" in a or a.endswith("_Skeleton")]
for a in doomed:
    if EAL.does_asset_exist(a):
        ok = EAL.delete_asset(a)
        log("pre-clean delete {} ok={}".format(a, ok))

for name in ORDER:
    folder = os.path.join(INBOX, name)
    r = {"checks": {}}
    report["assets"][name] = r
    try:
        man = json.load(open(os.path.join(folder, "manifest.json"), encoding="utf-8"))
        r["status_in_manifest"] = man.get("status")
        r["expected"] = {"dims_cm": man.get("dimensions_cm"), "tris": man.get("tri_count"), "slots": man.get("material_slots")}
        if man["type"] == "static_mesh":
            do_static(name, man, folder, r)
        elif man["type"] == "texture_set":
            do_texture_set(name, man, folder, r)
        else:
            do_skel(name, man, folder, r)
            if name == "SK_Player_Base":
                player_materials(man, r)
    except Exception as e:
        r["error"] = str(e) + "\n" + traceback.format_exc()
    r["pass"] = "error" not in r and all(v is True or v == "n/a" for v in r["checks"].values())
    log("{}: pass={} checks={}".format(name, r["pass"], r["checks"]))

for name in ORDER:
    man = mans[name]
    r = report["assets"][name]
    if man["type"] == "static_mesh" and str(man.get("batch")) in PROP_MAT_BATCHES + HAZARD_MAT_BATCHES and "error" not in r:
        try:
            if str(man.get("batch")) in HAZARD_MAT_BATCHES:
                hazard_materials(name, man, r)
            else:
                prop_materials(name, man, r)
                extra_slot_materials(name, man, r)
        except Exception as e:
            r["error"] = "materials: " + str(e) + "\n" + traceback.format_exc()
        r["pass"] = "error" not in r and all(v is True or v == "n/a" for v in r["checks"].values())
        log("{}: materials {} pass={}".format(name, r.get("materials"), r["pass"]))

delete_redirectors()
with open(os.path.join(OUT, "ue_report.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=1)
log("report written " + os.path.join(OUT, "ue_report.json"))
