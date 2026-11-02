"""Imports Carina's town_v1 kit (30 static meshes, their textures and per-slot materials, the mining whitelist data
and the 12 townsfolk clips) from handoff/inbox/town_v1.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_town_v1.py" -unattended -nullrhi -nosplash

Env: GFTOWN_KIT (default <project>/handoff/inbox/town_v1), GFTOWN_SKIP_CLIPS=1 to skip the clips.

- Meshes come from Data/Town/town_v1_mineable.json (Tools/gen_town_mineable.py builds it from MINEABLE_town_v1_v2.json
  and the folder manifests). The eight crystal-slot meshes are the v2 rebuilds: slot 0 Main, slot 1 Crystal.
- FBX settings match Tools/import_art_batch.py (Force Front X, convert scene + units, scale 1, combine meshes, UCX
  collision only, no materials or textures from the FBX). UE names drop the _vN suffix.
- Textures: nearest, 2D Pixels, wrap; _D sRGB (uncompressed), _E and _M linear masks.
- Materials: one M_GF_Block instance per slot (MI_Town_<X>, MI_Town_<X>_Crystal). The Crystal instance is a child of
  the same parent with the same textures, so it looks identical. DetailMean = mean linear luma of _D.
- Mining: UGFMineableAssetUserData on every mesh (WholeMesh for trees, Slots [1] for crystal meshes, None otherwise).
- QA per mesh against the manifest: dims, tri_count (trusted over the notes), slots, UCX count, min Z, sockets.
- Clips: A_Player_Base_TF_* onto SKEL_GF_Humanoid in /Game/Art/Characters/Player/Anims/Townsfolk (fbx_v5 for Idle_B and
  Sweep, fbx_v4 for the rest). The axis setting is chosen by probing TF_Idle_A f1 against A_Player_Base_Idle f1: these FBX name
  the armature object "root" like the root bone, so UE comes in lying on its back unless the import rotation fixes it.
Writes Saved/ArtImport/town_v1_report.json and logs "[gftown]" lines.
"""
import hashlib, json, os, re, struct, traceback, zlib
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
KIT = os.environ.get("GFTOWN_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "town_v1")
WHITELIST = os.path.join(PROJ, "Data", "Town", "town_v1_mineable.json")
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
PARENT = "/Game/Art/Blocks/Materials/M_GF_Block"
SKEL = "/Game/Art/Characters/Player/SKEL_GF_Humanoid"
REF_IDLE = "/Game/Art/Characters/Player/Anims/A_Player_Base_Idle"
ANIM_DEST = "/Game/Art/Characters/Player/Anims/Townsfolk"
POSES = os.path.join(KIT, "townsfolk_poses_v5")
CLIPS_V5 = ("TF_Idle_B", "TF_Sweep")
CLIPS = ["TF_Idle_A", "TF_Idle_B", "TF_Vendor_Call", "TF_Vendor_Offer", "TF_Carry_Basket", "TF_Sweep", "TF_Chat",
         "TF_Sit_Edge", "TF_Wave", "TF_Startle", "TF_Adjust_Mask", "TF_Bow_Court"]
ROUGHNESS = 0.8
EMISSIVE_STRENGTH = 1.5  # same as the ore crystal
DIM_TOL = 0.05
CLIP_LIMIT_Z = -5.0
BAD_PATH = re.compile(r"[a-z]:[\\/]users[\\/]", re.I)
KIT_NAME = "town_v1"
report = {"kit": "town_v1", "meshes": {}, "clips": {}, "log": []}


def log(m):
    unreal.log("[gftown] " + str(m))
    report["log"].append(str(m))


def try_set(obj, prop, val):
    try:
        obj.set_editor_property(prop, val)
    except Exception as e:
        log("warn: cannot set {}.{}: {}".format(type(obj).__name__, prop, e))


def unversioned(stem):
    return re.sub(r"_v\d+$", "", stem)


try:
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")
except Exception as e:
    log("warn: cvar " + str(e))


# ---------- PNG (stdlib) for DetailMean ----------
def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, plte = 8, b"", None
    w = h = depth = ctype = 0
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        t = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if t == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or inter != 0:
                raise RuntimeError("unsupported png {} (depth {}, interlace {})".format(path, depth, inter))
        elif t == b"PLTE":
            plte = body
        elif t == b"IDAT":
            idat += body
        elif t == b"IEND":
            break
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line

    def rgb(line, x):
        if ctype == 2 or ctype == 6:
            return line[x * bpp], line[x * bpp + 1], line[x * bpp + 2]
        if ctype == 3:
            k = line[x] * 3
            return plte[k], plte[k + 1], plte[k + 2]
        return line[x * bpp], line[x * bpp], line[x * bpp]
    return w, h, rows, rgb


_LIN = [((c / 255.0) / 12.92) if c <= 10 else (((c / 255.0) + 0.055) / 1.055) ** 2.4 for c in range(256)]


def detail_mean(path):
    """Mean linear luma (Rec. 709) of an sRGB colour texture."""
    w, h, rows, rgb = read_png(path)
    tot = 0.0
    for line in rows:
        for x in range(w):
            r, g, b = rgb(line, x)
            tot += 0.2126 * _LIN[r] + 0.7152 * _LIN[g] + 0.0722 * _LIN[b]
    return round(tot / (w * h), 4)


# ---------- import helpers ----------
def fbx_static():
    ui = unreal.FbxImportUI()
    for k, v in (("automated_import_should_detect_type", False), ("import_materials", False), ("import_textures", False),
                 ("create_physics_asset", False), ("import_mesh", True), ("import_as_skeletal", False), ("import_animations", False)):
        try_set(ui, k, v)
    try_set(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    d = ui.static_mesh_import_data
    for k, v in (("combine_meshes", True), ("auto_generate_collision", False), ("force_front_x_axis", True),
                 ("convert_scene", True), ("convert_scene_unit", True), ("import_uniform_scale", 1.0)):
        try_set(d, k, v)
    return ui


def fbx_anim(skeleton, front_x, rot=None):
    ui = unreal.FbxImportUI()
    for k, v in (("automated_import_should_detect_type", False), ("import_materials", False), ("import_textures", False),
                 ("create_physics_asset", False), ("import_mesh", False), ("import_as_skeletal", True), ("import_animations", True)):
        try_set(ui, k, v)
    try_set(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    try_set(ui, "skeleton", skeleton)
    d = ui.anim_sequence_import_data
    for k, v in (("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME), ("remove_redundant_keys", False),
                 ("import_bone_tracks", True), ("import_meshes_in_bone_hierarchy", False), ("delete_existing_morph_target_curves", False),
                 ("force_front_x_axis", front_x), ("convert_scene", True), ("convert_scene_unit", True), ("import_uniform_scale", 1.0)):
        try_set(d, k, v)
    if rot is not None:
        try_set(d, "import_rotation", rot)
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
        raise RuntimeError("import produced nothing at " + obj)
    return unreal.load_asset(obj)


def source_files(asset):
    try:
        return [str(f) for f in asset.get_editor_property("asset_import_data").extract_filenames()]
    except Exception as e:
        log("warn: source paths " + str(e)); return None


_TEX = {}


def import_texture(folder, fname, dest):
    uname = unversioned(os.path.splitext(fname)[0])
    upath = dest + "/Textures/" + uname
    src = os.path.join(folder, fname)
    digest = hashlib.sha1(open(src, "rb").read()).hexdigest()
    if upath in _TEX:
        if _TEX[upath] != digest:
            raise RuntimeError("texture {} ships twice with different bytes".format(uname))
        return uname, upath
    _TEX[upath] = digest
    t = run_import(src, dest + "/Textures", uname, None)
    lin = uname.endswith("_E") or uname.endswith("_M")
    for prop, val in (("filter", unreal.TextureFilter.TF_NEAREST), ("lod_group", unreal.TextureGroup.TEXTUREGROUP_PIXELS2D),
                      ("address_x", unreal.TextureAddress.TA_WRAP), ("address_y", unreal.TextureAddress.TA_WRAP), ("srgb", not lin),
                      ("compression_settings", unreal.TextureCompressionSettings.TC_MASKS if lin else unreal.TextureCompressionSettings.TC_EDITOR_ICON)):
        try_set(t, prop, val)
    EAL.save_loaded_asset(t)
    return uname, upath


def load_or_create_mi(path):
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    d, n = path.rsplit("/", 1)
    return AT.create_asset(n, d, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())


def slot_mi_names(name, man):
    hint = man.get("unreal_material_hint")
    inst = hint.get("slot_instances") if isinstance(hint, dict) else None
    base = "MI_" + name[3:]
    out = []
    for s in man["material_slots"]:
        hinted = (inst or {}).get(s)
        if hinted:
            # hints may carry notes after the asset name, e.g. "MI_X_Main (opaque)"
            mm = re.match(r"\s*([A-Za-z0-9_]+)", hinted)
            hinted = mm.group(1) if mm else None
        out.append(hinted or (base if s == "Main" else base + "_" + s))
    return out


def vec(v):
    return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]


def do_mesh(entry, r):
    folder = os.path.join(KIT, entry["folder"])
    man = json.load(open(os.path.join(folder, entry["manifest"]), encoding="utf-8"))
    name = entry["asset"]
    if man["unreal_target"].rsplit("/", 1)[1] != name:
        raise RuntimeError("manifest target {} does not match {}".format(man["unreal_target"], name))
    dest = man["unreal_target"].rsplit("/", 1)[0]
    m = run_import(os.path.join(folder, man["files"]["mesh"]), dest, name, fbx_static())
    tex = {}
    for f in man["files"]["textures"]:
        uname, upath = import_texture(folder, f, dest)
        tex[uname[-2:]] = (upath, os.path.join(folder, f))
    if "_D" not in tex:
        raise RuntimeError("no _D texture")
    mean = detail_mean(tex["_D"][1])
    parent = unreal.load_asset(PARENT)
    mats = list(m.get_editor_property("static_materials"))
    want = {}
    for i, mi_name in enumerate(slot_mi_names(name, man)):
        mi = load_or_create_mi(dest + "/Materials/" + mi_name)
        MEL.set_material_instance_parent(mi, parent)
        MEL.set_material_instance_texture_parameter_value(mi, "Diffuse", unreal.load_asset(tex["_D"][0]))
        if "_E" in tex:
            MEL.set_material_instance_texture_parameter_value(mi, "Emissive", unreal.load_asset(tex["_E"][0]))
        for p, v in (("Roughness", ROUGHNESS), ("DetailMean", mean), ("Brightness", 1.0), ("EmissiveStrength", EMISSIVE_STRENGTH if "_E" in tex else 0.0)):
            MEL.set_material_instance_scalar_parameter_value(mi, p, float(v))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        want[man["material_slots"][i]] = mi
    for i, sm in enumerate(mats):
        s = str(sm.get_editor_property("material_slot_name"))
        if s in want:
            sm.set_editor_property("material_interface", want[s]); mats[i] = sm
    m.set_editor_property("static_materials", mats)
    mode = {"whole_mesh": unreal.GFMineableMode.WHOLE_MESH, "slots": unreal.GFMineableMode.SLOTS}.get(entry["mode"], unreal.GFMineableMode.NONE)
    ok_mine = unreal.GFMineableLibrary.set_mesh_mineable(m, mode, entry["slots"], KIT_NAME, entry["reason"])
    EAL.save_loaded_asset(m)

    m = unreal.load_asset(man["unreal_target"])
    chk = r["checks"]
    bb = m.get_bounding_box()
    dims = [round(bb.max.x - bb.min.x, 3), round(bb.max.y - bb.min.y, 3), round(bb.max.z - bb.min.z, 3)]
    r.update({"folder": entry["folder"], "dims_cm": dims, "bounds_min": vec(bb.min), "bounds_max": vec(bb.max),
              "detail_mean": mean, "textures": sorted(t[0] for t in tex.values())})
    try:
        r["tris"] = m.get_num_triangles(0)
    except Exception:
        r["tris"] = None
    r["material_slots"] = [str(s.material_slot_name) for s in m.static_materials]
    r["materials"] = {str(s.material_slot_name): (s.material_interface.get_name() if s.material_interface else None) for s in m.static_materials}
    chk["dims"] = all(abs(a - b) <= DIM_TOL for a, b in zip(dims, man["dimensions_cm"]))
    chk["tris"] = r["tris"] == man["tri_count"]
    chk["slots"] = r["material_slots"] == man["material_slots"]
    chk["materials"] = all(r["materials"].get(s) == want[s].get_name() for s in want)
    try:
        ag = m.get_editor_property("body_setup").get_editor_property("agg_geom")
        r["collision_convex_elems"] = len(ag.get_editor_property("convex_elems")) + len(ag.get_editor_property("box_elems"))
    except Exception as e:
        r["collision_convex_elems"] = None; log("info: agg_geom " + str(e))
    named = set(re.findall(r"UCX_" + re.escape(name) + r"_\d+", json.dumps(man)))
    no_col = re.search(r"\bno collision\b", str(man.get("qa", {}).get("notes", "")), re.I) is not None
    if man.get("collision") == "none" or (no_col and not named):
        r["collision_expected"] = 0
        chk["collision"] = r["collision_convex_elems"] == 0
    elif named:
        r["collision_expected"] = len(named)
        chk["collision"] = r["collision_convex_elems"] == len(named)
    else:
        r["collision_expected"] = ">=1"
        chk["collision"] = (r["collision_convex_elems"] or 0) >= 1
    exp_min = float(man["qa"].get("min_ground_z_cm", 0.0))
    chk["min_z"] = abs(bb.min.z - exp_min) <= DIM_TOL
    socks = sorted((man.get("sockets_blender_cm") or {}).keys())
    r["sockets"] = [s for s in socks if m.find_socket(s) is not None]
    if socks:
        r["sockets_expected"] = socks
        chk["sockets"] = r["sockets"] == socks
    md, ms = unreal.GFMineableLibrary.get_mesh_mineable_mode(m)
    r["mineable"] = {"mode": str(md), "slots": list(ms)}
    chk["mineable"] = bool(ok_mine) and md == mode and list(ms) == list(entry["slots"] if entry["mode"] == "slots" else [])
    paths = (source_files(m) or []) + [p for t in tex.values() for p in (source_files(unreal.load_asset(t[0])) or [])]
    chk["source_paths_clean"] = not any(BAD_PATH.search(p) for p in paths)


# ---------- clips ----------
def clip_fbx(clip):
    sub = "fbx_v5" if clip in CLIPS_V5 else "fbx_v4"
    return os.path.join(POSES, sub, "A_Player_Base_{}_v1.fbx".format(clip))


def pose(anim, frame):
    return unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim, frame, unreal.AnimPoseEvaluationOptions())


def pose_error(a, b):
    """Worst world-space bone position difference (cm) between two poses."""
    worst = 0.0
    for bone in unreal.AnimPoseExtensions.get_bone_names(a):
        ta = unreal.AnimPoseExtensions.get_bone_pose(a, bone, unreal.AnimPoseSpaces.WORLD).translation
        tb = unreal.AnimPoseExtensions.get_bone_pose(b, bone, unreal.AnimPoseSpaces.WORLD).translation
        worst = max(worst, (ta - tb).length())
    return round(worst, 3)


def frames(anim):
    try:
        return int(unreal.AnimationLibrary.get_num_frames(anim))
    except Exception:
        return None


def feet_min_z(anim, n):
    p0 = pose(anim, 0)
    feet = [b for b in unreal.AnimPoseExtensions.get_bone_names(p0) if re.search(r"foot|toe|ball", str(b), re.I)]
    low = None
    for f in range(0, (n or 0) + 1):
        p = pose(anim, f)
        for b in feet:
            z = unreal.AnimPoseExtensions.get_bone_pose(p, b, unreal.AnimPoseSpaces.WORLD).translation.z
            low = z if low is None else min(low, z)
    return round(low, 3) if low is not None else None


def do_clips():
    skel = unreal.load_asset(SKEL)
    ref = unreal.load_asset(REF_IDLE)
    meta = {c["clip"]: c for c in json.load(open(os.path.join(POSES, "poses_townsfolk_v5.json"), encoding="utf-8"))["clips"]}
    # TF_Idle_A f1 is the player Idle f1: import it with each candidate setting and keep the one that reproduces it.
    # The v4/v5 FBX name the armature object "root" like the root bone, so UE takes the object as the root bone and the
    # clip comes in lying on its back facing +Z; Force Front X has no effect on that. An import rotation of
    # (pitch 0, yaw -90, roll 90) maps it back (up +Z, front +X like the player set) without touching Carina's files.
    probe_dir = "/Game/Temp/TownClipProbe"
    fixrot = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(0, -1, 0), unreal.Vector(1, 0, 0))
    cands = [("front_x_off", False, None), ("front_x_on", True, None), ("root_fix_rotation", False, fixrot)]
    errs = {}
    for i, (key, fx, rot) in enumerate(cands):
        a = run_import(clip_fbx("TF_Idle_A"), probe_dir, "Probe_{}".format(i), fbx_anim(skel, fx, rot))
        errs[key] = pose_error(pose(a, 0), pose(ref, 0))
    EAL.delete_directory(probe_dir)
    best = min(errs, key=errs.get)
    _, front_x, rot = [c for c in cands if c[0] == best][0]
    report["clip_front_x"] = {"chosen": best, "force_front_x": front_x,
                              "import_rotation": None if rot is None else [rot.pitch, rot.yaw, rot.roll],
                              "idle_f1_error_cm": errs, "readme": "Force Front X off"}
    report["clip_probe_ok"] = errs[best] <= 1.0
    log("clip axis probe: errors {} -> {}".format(errs, best))
    # A replace_existing import onto an existing clip reuses its stored import settings, so start the folder fresh.
    if EAL.does_directory_exist(ANIM_DEST):
        EAL.delete_directory(ANIM_DEST)
    for clip in CLIPS:
        cr = {"checks": {}}
        report["clips"][clip] = cr
        try:
            name = "A_Player_Base_" + clip
            a = run_import(clip_fbx(clip), ANIM_DEST, name, fbx_anim(skel, front_x, rot))
            cr["fbx"] = os.path.relpath(clip_fbx(clip), KIT).replace("\\", "/")
            cr["skeleton"] = a.get_editor_property("skeleton").get_path_name()
            cr["frames"] = frames(a)
            cr["frames_expected"] = meta[clip]["frames"]
            cr["feet_min_z"] = feet_min_z(a, cr["frames"])
            cr["checks"]["skeleton"] = cr["skeleton"] == skel.get_path_name()
            cr["checks"]["upright_probe"] = report["clip_probe_ok"]
            cr["checks"]["frames"] = cr["frames"] in (meta[clip]["frames"], meta[clip]["frames"] + 1)
            cr["checks"]["feet"] = cr["feet_min_z"] is not None and cr["feet_min_z"] >= CLIP_LIMIT_Z
            cr["checks"]["source_paths_clean"] = not any(BAD_PATH.search(p) for p in (source_files(a) or []))
        except Exception as e:
            cr["error"] = str(e) + "\n" + traceback.format_exc()
        cr["pass"] = "error" not in cr and all(cr["checks"].values())
        log("{}: pass={} {}".format(clip, cr["pass"], cr["checks"]))


def main():
    wl = json.load(open(WHITELIST, encoding="utf-8"))
    for entry in wl["meshes"]:
        r = {"checks": {}}
        report["meshes"][entry["asset"]] = r
        try:
            do_mesh(entry, r)
        except Exception as e:
            r["error"] = str(e) + "\n" + traceback.format_exc()
        r["pass"] = "error" not in r and all(v is True for v in r["checks"].values())
        log("{}: pass={} {}".format(entry["asset"], r["pass"], r["checks"]))
    if os.environ.get("GFTOWN_SKIP_CLIPS") != "1":
        try:
            do_clips()
        except Exception as e:
            report["clips_error"] = str(e) + "\n" + traceback.format_exc()
            log("clips failed: " + str(e))
    report["summary"] = {
        "meshes": len(report["meshes"]), "meshes_pass": sum(1 for r in report["meshes"].values() if r["pass"]),
        "clips": len(report["clips"]), "clips_pass": sum(1 for r in report["clips"].values() if r["pass"]),
    }
    with open(os.path.join(OUT, "town_v1_report.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    log("summary {}".format(report["summary"]))


# Tools/import_towns_v2.py imports this file as a library (GFTOWN_LIB=1) and reuses do_mesh and the clip helpers.
if os.environ.get("GFTOWN_LIB") != "1":
    main()
