"""Imports the mining_anims_v1 player clips from handoff/inbox/mining_anims_v1 onto SKEL_GF_Humanoid:
Mine_Punch, Mine_Dig, Mine_Claw, Mine_Overcharge, Cast_Element_Swap, Mine_Field_Start/Loop/End and FistScope.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_mining_v1.py" -unattended -nullrhi -nosplash

Env: GFMINE_KIT (default <project>/handoff/inbox/mining_anims_v1), GFMINE_PHASE=rig to run only the rig check.

- Same FBX anim settings as Tools/import_town_v1.py (fbx_anim). Clips land in /Game/Art/Characters/Player/Anims
  as A_Player_Base_<Clip> (no _v1), with root motion off (the root bone is never keyed).
- Rig check first, like Tools/import_towns_v2.py: Mine_Punch is imported alone with each axis candidate. It fails if the
  armature object (Armature_PlayerBase) adds a bone to SKEL_GF_Humanoid or a track above root; the axis setting is the one
  whose f0 best matches A_Player_Base_Idle f0 (one-shots start on Idle f1).
- Per clip: skeleton, frame count from the manifest, feet above -5 cm, upright and facing +X at f0, no armature track,
  no root motion, one-shots and Field_Start start on Idle, loops end on their first pose, clean source paths.
Writes Saved/ArtImport/mining_v1_report.json and logs "[gftown]" lines.
"""
import json, os, sys, traceback
import unreal

os.environ["GFTOWN_LIB"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_town_v1 as base  # noqa: E402

PROJ = base.PROJ
KIT = os.environ.get("GFMINE_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "mining_anims_v1")
PHASE = os.environ.get("GFMINE_PHASE", "all")
DEST = "/Game/Art/Characters/Player/Anims"
EAL = base.EAL
MAN = json.load(open(os.path.join(KIT, "manifest.json"), encoding="utf-8"))
META = {c["clip"]: c for c in MAN["clips"]}
CLIPS = [c["clip"] for c in MAN["clips"]]
POSE_TOL_CM = 1.0
report = {"kit": "mining_anims_v1", "set": MAN.get("set"), "status": MAN.get("status"), "clips": {}, "log": []}
base.report = report
log = base.log


def bone_names(obj):
    try:
        return [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(base.pose(obj, 0))]
    except Exception:
        return []


def skel_bones(skel):
    """Bones of the skeleton's reference pose (falls back to the reference idle clip's pose)."""
    try:
        return [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(unreal.AnimPoseExtensions.get_reference_pose(skel))]
    except Exception as e:
        log("info: reference pose " + str(e))
        return bone_names(unreal.load_asset(base.REF_IDLE))


def anim_tracks(anim):
    try:
        return [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(anim)]
    except Exception as e:
        log("warn: track names " + str(e)); return None


def play_length(anim):
    try:
        return round(anim.get_play_length(), 4)
    except Exception:
        return None


def fbx_of(clip):
    return os.path.join(KIT, META[clip]["file"].replace("/", os.sep))


def rig_check():
    skel = unreal.load_asset(base.SKEL)
    ref = unreal.load_asset(base.REF_IDLE)
    before = skel_bones(skel)
    probe_dir = "/Game/Temp/MiningClipProbeV1"
    cands = [("front_x_off", False, None), ("front_x_on", True, None),
             ("front_x_off_yaw_-90", False, unreal.Rotator(0, 0, -90)), ("front_x_off_yaw_90", False, unreal.Rotator(0, 0, 90))]
    rc = {"fbx": META["Mine_Punch"]["file"], "skeleton_bones_before": len(before)}
    errs = {}
    for i, (key, fx, rot) in enumerate(cands):
        a = base.run_import(fbx_of("Mine_Punch"), probe_dir, "Probe_{}".format(i), base.fbx_anim(skel, fx, rot))
        errs[key] = base.pose_error(base.pose(a, 0), base.pose(ref, 0))
        if i == 0:
            rc["tracks"] = anim_tracks(a)
            rc["pose_bones"] = bone_names(a)
            rc["anim_skeleton"] = a.get_editor_property("skeleton").get_path_name()
    EAL.delete_directory(probe_dir)
    skel = unreal.load_asset(base.SKEL)
    after = skel_bones(skel)
    rc["skeleton_bones_after"] = len(after)
    rc["extra_bones"] = sorted(set(after) - set(before))
    tracks = rc.get("tracks") or []
    rc["armature_track"] = any("armature" in t.lower() for t in tracks + rc.get("pose_bones", []))
    rc["root_is_top"] = bool(rc.get("pose_bones")) and rc["pose_bones"][0] == "root"
    rc["idle_f0_error_cm"] = errs
    best = min(errs, key=errs.get)
    rc["chosen"] = best
    rc["ok"] = (not rc["extra_bones"]) and (not rc["armature_track"]) and rc["root_is_top"] and errs[best] <= POSE_TOL_CM \
        and rc["anim_skeleton"] == skel.get_path_name()
    report["rig_check"] = rc
    log("mining rig check: {}".format(json.dumps(rc)[:1500]))
    _, fx, rot = [c for c in cands if c[0] == best][0]
    return rc["ok"], fx, rot


def do_clips(front_x, rot):
    skel = unreal.load_asset(base.SKEL)
    idle0 = base.pose(unreal.load_asset(base.REF_IDLE), 0)
    for clip in CLIPS:
        meta = META[clip]
        name = "A_Player_Base_" + clip
        cr = {"checks": {}, "type": meta.get("type")}
        report["clips"][clip] = cr
        try:
            # A replace_existing import reuses stored settings, so start each clip fresh.
            if EAL.does_asset_exist(DEST + "/" + name):
                EAL.delete_asset(DEST + "/" + name)
            a = base.run_import(fbx_of(clip), DEST, name, base.fbx_anim(skel, front_x, rot))
            try:
                a.set_editor_property("enable_root_motion", False)
            except Exception as e:
                log("info: root motion flag " + str(e))
            cr["skeleton"] = a.get_editor_property("skeleton").get_path_name()
            cr["frames"] = base.frames(a)
            cr["frames_expected"] = meta["frames"]
            cr["length_s"] = play_length(a)
            cr["feet_min_z"] = base.feet_min_z(a, cr["frames"])
            cr["checks"]["skeleton"] = cr["skeleton"] == skel.get_path_name()
            cr["checks"]["frames"] = cr["frames"] in (meta["frames"], meta["frames"] + 1)
            cr["checks"]["feet"] = cr["feet_min_z"] is not None and cr["feet_min_z"] >= base.CLIP_LIMIT_Z
            p0 = base.pose(a, 0)
            fl = unreal.AnimPoseExtensions.get_bone_pose(p0, "foot_l", unreal.AnimPoseSpaces.WORLD).translation
            fr = unreal.AnimPoseExtensions.get_bone_pose(p0, "foot_r", unreal.AnimPoseSpaces.WORLD).translation
            hd = unreal.AnimPoseExtensions.get_bone_pose(p0, "head", unreal.AnimPoseSpaces.WORLD).translation
            cr["checks"]["upright_facing_x"] = hd.z > 90.0 and fl.y < fr.y and abs(fl.x - fr.x) < abs(fl.y - fr.y) + 1.0
            cr["checks"]["no_armature_track"] = not any("armature" in t.lower() for t in (anim_tracks(a) or []))
            cr["checks"]["root_motion_off"] = not a.get_editor_property("enable_root_motion")
            cr["checks"]["source_paths_clean"] = not any(base.BAD_PATH.search(p) for p in (base.source_files(a) or []))
            cr["f0_vs_idle_cm"] = base.pose_error(p0, idle0)
            if meta.get("starts_on", "").startswith("Idle"):
                cr["checks"]["starts_on_idle"] = cr["f0_vs_idle_cm"] <= POSE_TOL_CM
            if meta.get("type", "").startswith("loop") or clip in ("Mine_Field_Loop", "FistScope"):
                cr["loop_seam_cm"] = base.pose_error(base.pose(a, cr["frames"] or 0), p0)
                cr["checks"]["loop_seam"] = cr["loop_seam_cm"] <= POSE_TOL_CM
            EAL.save_loaded_asset(a)
        except Exception as e:
            cr["error"] = str(e) + "\n" + traceback.format_exc()
        cr["pass"] = "error" not in cr and all(cr["checks"].values())
        log("{}: pass={} {}".format(clip, cr["pass"], cr["checks"]))
    # Field chain: Start's last pose meets Loop's first, Loop's last meets End's first.
    try:
        st, lp, en = (unreal.load_asset(DEST + "/A_Player_Base_" + c) for c in ("Mine_Field_Start", "Mine_Field_Loop", "Mine_Field_End"))
        report["field_chain_cm"] = {
            "start_to_loop": base.pose_error(base.pose(st, base.frames(st)), base.pose(lp, 0)),
            "loop_to_end": base.pose_error(base.pose(lp, base.frames(lp)), base.pose(en, 0)),
        }
    except Exception as e:
        report["field_chain_error"] = str(e)


def main():
    try:
        ok, fx, rot = rig_check()
    except Exception as e:
        ok, fx, rot = False, False, None
        report["rig_check_error"] = str(e) + "\n" + traceback.format_exc()
        log("rig check failed: " + str(e))
    if PHASE != "rig":
        if ok:
            do_clips(fx, rot)
        else:
            log("rig check did not pass: clips not imported")
    report["summary"] = {"rig_ok": ok, "clips": len(report["clips"]),
                         "clips_pass": sum(1 for r in report["clips"].values() if r["pass"])}
    name = "mining_v1_rig_report.json" if PHASE == "rig" else "mining_v1_report.json"
    with open(os.path.join(base.OUT, name), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    log("summary {}".format(report["summary"]))


main()
