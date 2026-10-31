"""Re-imports single animation clips onto an existing skeleton without touching the mesh, the skeleton or any other clip.

Used for SK_Player_Base v7 (batch beat_hop_v2): only A_Player_Base_Fall (pelvis fix, no more float) and
A_Player_Base_Crouch_Walk (re-solved with leg IK, no foot slide) changed.
A full GFART_ONLY=SK_Player_Base import would re-create SKEL_GF_Humanoid and drag every outfit, so this does just the clips.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/reimport_player_clips.py" -unattended -nullrhi -nosplash
Env: GFART_INBOX (default <project>/handoff/inbox), GFCLIP_ASSET (default SK_Player_Base),
     GFCLIP_NAMES (default A_Player_Base_Fall,A_Player_Base_Crouch_Walk).
Same FBX anim settings as Tools/import_art_batch.py (Force Front X, convert scene + units, scale 1, exported time, no key reduction).
Writes Saved/ArtImport/reimport_clips_report.json and logs "[gfclips]" lines.
"""
import json, os, re, traceback
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
INBOX = os.environ.get("GFART_INBOX", "").strip() or os.path.join(PROJ, "handoff", "inbox")
ASSET = os.environ.get("GFCLIP_ASSET", "").strip() or "SK_Player_Base"
NAMES = [n.strip() for n in (os.environ.get("GFCLIP_NAMES", "") or "A_Player_Base_Fall,A_Player_Base_Crouch_Walk").split(",") if n.strip()]
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[gfclips] " + str(m))


def try_set(obj, prop, val):
    try:
        obj.set_editor_property(prop, val)
    except Exception as e:
        log("could not set %s: %s" % (prop, e))


def anim_options(skeleton):
    ui = unreal.FbxImportUI()
    for k, v in (("automated_import_should_detect_type", False), ("import_materials", False), ("import_textures", False),
                 ("create_physics_asset", False), ("import_mesh", False), ("import_as_skeletal", True), ("import_animations", True)):
        try_set(ui, k, v)
    try_set(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    try_set(ui, "skeleton", skeleton)
    d = ui.anim_sequence_import_data
    try_set(d, "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    try_set(d, "remove_redundant_keys", False)
    try_set(d, "import_bone_tracks", True)
    try_set(d, "import_meshes_in_bone_hierarchy", False)
    try_set(d, "delete_existing_morph_target_curves", False)
    try_set(d, "force_front_x_axis", True)
    try_set(d, "convert_scene", True)
    try_set(d, "convert_scene_unit", True)
    try_set(d, "import_uniform_scale", 1.0)
    return ui


def frames(anim):
    try:
        return int(unreal.AnimationLibrary.get_num_frames(anim))
    except Exception:
        return None


def feet_min_z(anim, n):
    """Lowest foot/toe bone Z (component space) over the clip, like import_art_batch's feet check."""
    try:
        opts = unreal.AnimPoseEvaluationOptions()
        p0 = unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim, 0, opts)
        bones = [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(p0)]
        feet = [b for b in bones if re.search(r"foot|toe|ball", b, re.I)]
        low = None
        for f in range(0, (n or 0) + 1):
            p = unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim, f, opts)
            for b in feet:
                z = unreal.AnimPoseExtensions.get_bone_pose(p, b, unreal.AnimPoseSpaces.WORLD).translation.z
                low = z if low is None else min(low, z)
        return round(low, 3) if low is not None else None
    except Exception as e:
        log("feet probe failed: %s" % e)
        return None


def main():
    folder = os.path.join(INBOX, ASSET)
    man = json.load(open(os.path.join(folder, "manifest.json"), encoding="utf-8"))
    dest = man["unreal_target"].rsplit("/", 1)[0] + "/Anims"
    report = {"asset": ASSET, "manifest_version": man.get("version"), "batch": man.get("batch"), "dest": dest, "clips": {}}
    log("%s v%s (%s): re-importing %s into %s" % (ASSET, man.get("version"), man.get("batch"), ", ".join(NAMES), dest))
    ok_all = True
    for name in NAMES:
        cr = {}
        report["clips"][name] = cr
        try:
            fbx = os.path.join(folder, name + ".fbx")
            if not os.path.isfile(fbx):
                raise RuntimeError("missing " + fbx)
            path = dest + "/" + name
            if not EAL.does_asset_exist(path):
                raise RuntimeError("no existing clip at " + path + " (this script only replaces clips)")
            old = unreal.load_asset(path)
            skel = old.get_editor_property("skeleton")
            cr["frames_before"] = frames(old)
            cr["feet_min_z_before"] = feet_min_z(old, cr["frames_before"])
            t = unreal.AssetImportTask()
            t.filename = fbx; t.destination_path = dest; t.destination_name = name
            t.automated = True; t.replace_existing = True; t.save = True
            t.options = anim_options(skel)
            AT.import_asset_tasks([t])
            anim = unreal.load_asset(path)
            cr["skeleton"] = anim.get_editor_property("skeleton").get_path_name()
            cr["skeleton_ok"] = cr["skeleton"] == skel.get_path_name()
            cr["frames"] = frames(anim)
            cr["feet_min_z"] = feet_min_z(anim, cr["frames"])
            cr["feet_above_limit"] = cr["feet_min_z"] is None or cr["feet_min_z"] >= -5.0
            cr["pass"] = bool(cr["skeleton_ok"] and cr["frames"] and cr["feet_above_limit"])
            EAL.save_loaded_asset(anim)
        except Exception as e:
            cr["error"] = str(e) + "\n" + traceback.format_exc()
            cr["pass"] = False
        ok_all = ok_all and cr["pass"]
        log("%s: %s" % (name, json.dumps({k: v for k, v in cr.items() if k != "error"})))
        if "error" in cr:
            unreal.log_warning("[gfclips] %s failed: %s" % (name, cr["error"]))
    report["pass"] = ok_all
    with open(os.path.join(OUT, "reimport_clips_report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    log("PASS" if ok_all else "FAIL")


main()
