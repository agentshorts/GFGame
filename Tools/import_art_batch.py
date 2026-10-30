"""Headless import + verification of the artist's art handoff batch (handoff/inbox -> /Game/Art).

Run:
  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_art_batch.py" -unattended -nullrhi -nosplash
Writes Saved/ArtImport/ue_report.json. Settings: Force Front X Axis, convert scene + scene units,
uniform scale 1, no redundant-key removal (scale tracks kept), no materials (slots keep names).
"""
import json, math, os, re, traceback
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
INBOX = os.path.join(PROJ, "handoff", "inbox")
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ORDER = ["SM_Block_Grass", "SM_Block_WoodPlank", "SM_Block_Stone", "SK_Player_Base", "SK_Outfit_Top_GardenTee", "SK_Dino_Raptor"]
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
        if name.endswith("_M"):
            try_set(t, "srgb", False)
            try_set(t, "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        EAL.save_loaded_asset(t)
        out.append(name)
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
    bb = m.get_bounding_box()
    dims = [round(bb.max.x - bb.min.x, 3), round(bb.max.y - bb.min.y, 3), round(bb.max.z - bb.min.z, 3)]
    r["dims_cm"] = dims; r["bounds_min"] = vec(bb.min); r["bounds_max"] = vec(bb.max)
    r["tris"] = tri_count_static(m)
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    try: r["simple_collision"] = sub.get_simple_collision_count(m)
    except Exception as e: r["simple_collision"] = None; log("warn: collision count " + str(e))
    try:
        ag = m.get_editor_property("body_setup").get_editor_property("agg_geom")
        r["collision_convex_elems"] = len(ag.get_editor_property("convex_elems"))
    except Exception as e: log("info: agg_geom " + str(e))
    r["material_slots"] = [str(s.material_slot_name) for s in m.static_materials]
    chk = r["checks"]
    chk["dims"] = all(abs(a - b) <= DIM_TOL for a, b in zip(dims, man["dimensions_cm"]))
    chk["tris"] = r["tris"] == man["tri_count"] if r["tris"] is not None else "n/a"
    chk["slots"] = r["material_slots"] == man["material_slots"]
    chk["collision"] = (r["simple_collision"] or 0) >= 1 or (r.get("collision_convex_elems") or 0) >= 1

def do_skel(name, man, folder, r):
    dest = man["unreal_target"].rsplit("/", 1)[0]
    sk = man["skeleton"]
    reuse = sk.get("reuse_existing")
    skel_name = reuse or sk["skeleton_name"]
    existing = skeletons.get(skel_name)
    if existing is None:
        for cand in ["/Game/Art/Characters/" + skel_name, dest + "/" + skel_name]:
            if EAL.does_asset_exist(cand):
                existing = unreal.load_asset(cand); break
    if reuse and existing is None:
        raise RuntimeError("reuse skeleton " + reuse + " not imported yet")
    m = run_import(os.path.join(folder, man["files"]["mesh"]), dest, name, fbx_options("skel", existing))
    skel = m.skeleton
    if existing is None:
        old = skel.get_path_name().split(".")[0]
        new = dest + "/" + skel_name
        if old != new:
            if not EAL.rename_asset(old, new):
                raise RuntimeError("rename skeleton failed " + old + " -> " + new)
            EAL.save_loaded_asset(m)
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

    clips_expected = parse_clips(man["qa"]["notes"])
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
        cr["pass"] = ok
    chk["clips"] = all(cr["pass"] for cr in r["clips"].values()) if r["clips"] else "n/a"
    chk["clip_count"] = len(r["clips"]) == len(clips_expected) if man["files"]["animations"] else "n/a"

def owned_assets(man):
    dest = man["unreal_target"].rsplit("/", 1)[0]
    out = [dest + "/" + os.path.splitext(a)[0] for a in man["files"].get("animations", [])]
    out = [dest.replace(dest, dest + "/Anims") + "/" + os.path.basename(a) for a in out]
    out += [dest + "/Textures/" + os.path.splitext(t)[0] for t in man["files"].get("textures", [])]
    out.append(man["unreal_target"])
    sk = man.get("skeleton") or {}
    if sk and not sk.get("reuse_existing"):
        out.append(dest + "/" + sk["skeleton_name"])
        out.append(man["unreal_target"] + "_Skeleton")
    return out

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
        r["expected"] = {"dims_cm": man["dimensions_cm"], "tris": man["tri_count"], "slots": man["material_slots"]}
        if man["type"] == "static_mesh":
            do_static(name, man, folder, r)
        else:
            do_skel(name, man, folder, r)
    except Exception as e:
        r["error"] = str(e) + "\n" + traceback.format_exc()
    r["pass"] = "error" not in r and all(v is True or v == "n/a" for v in r["checks"].values())
    log("{}: pass={} checks={}".format(name, r["pass"], r["checks"]))

delete_redirectors()
with open(os.path.join(OUT, "ue_report.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=1)
log("report written " + os.path.join(OUT, "ue_report.json"))
