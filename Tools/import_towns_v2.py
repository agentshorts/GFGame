"""Imports Carina's towns_v2 kit from handoff/inbox/towns_v2: 65 static meshes (Rime, Cinderwright, homesteads, lava
fields, ground tiles and patches, smoke plumes) with textures, per-slot materials and mining data, the town_v1 fixes
(SM_Town_Fountain v7 with the sittable 43.75 cm rim, and the 12 townsfolk clips from fbx_v8), and the 7 chunk J
townsfolk clips (fbx_v4).

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_towns_v2.py" -unattended -nullrhi -nosplash

Env: GFTOWN_KIT (default <project>/handoff/inbox/towns_v2), GFTOWN_V1_KIT (default <kit>/../town_v1),
GFTOWN_PHASE=rig to run only the rig check (one FBX, nothing kept), GFTOWN_SKIP_MESHES=1, GFTOWN_SKIP_CLIPS=1.

- Meshes come from Data/Town/towns_v2_mineable.json (Tools/gen_towns_v2_mineable.py). Import, textures, materials
  and per-mesh QA reuse Tools/import_town_v1.py (do_mesh); the mining data is stamped with kit towns_v2.
- Rig check first: TF_Idle_A from fbx_v8 is imported alone. The armature object is now Armature_PlayerBase; the
  check fails if it adds a bone to SKEL_GF_Humanoid or a track above root, and the axis setting is picked by matching
  Idle f1 against A_Player_Base_Idle f1. With the object renamed the clips come in upright (the old (0, -90, 90)
  rotation now tips them over) but facing +Y, so the chosen setting is an import yaw of -90 (0.0 cm on Idle f1).
- Every clip is checked to face +X at f0 (left foot on -Y) like the player set.
- Adjust_Mask: hand-to-head distances per frame on the old clip and on v8, written to the report.
Writes Saved/ArtImport/towns_v2_report.json and logs "[gftown]" lines.
"""
import json, os, re, sys, traceback
import unreal

os.environ["GFTOWN_LIB"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_town_v1 as base  # noqa: E402

PROJ = base.PROJ
KIT = os.environ.get("GFTOWN_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "towns_v2")
V1_KIT = os.environ.get("GFTOWN_V1_KIT", "").strip() or os.path.join(os.path.dirname(KIT), "town_v1")
WHITELIST = os.path.join(PROJ, "Data", "Town", "towns_v2_mineable.json")
WHITELIST_V1 = os.path.join(PROJ, "Data", "Town", "town_v1_mineable.json")
V8 = os.path.join(KIT, "town_v1_fixes", "townsfolk_poses_v8")
CJ = os.path.join(KIT, "chunk_J")
PHASE = os.environ.get("GFTOWN_PHASE", "all")
EAL = base.EAL
report = {"kit": "towns_v2", "meshes": {}, "clips": {}, "log": []}
base.report = report
log = base.log


def clip_list(poses_json):
    return [c["clip"] for c in json.load(open(poses_json, encoding="utf-8"))["clips"]], \
        {c["clip"]: c for c in json.load(open(poses_json, encoding="utf-8"))["clips"]}


V8_CLIPS, V8_META = clip_list(os.path.join(V8, "poses_townsfolk_v8.json"))
J_CLIPS, J_META = clip_list(os.path.join(CJ, "poses_townsfolk_J_v4.json"))


def fbx_of(clip):
    if clip in V8_META:
        return os.path.join(V8, "fbx_v8", "A_Player_Base_{}_v1.fbx".format(clip))
    return os.path.join(CJ, "fbx_v4", "A_Player_Base_{}_v1.fbx".format(clip))


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


def rig_check():
    """Imports TF_Idle_A (fbx_v8) alone with each axis candidate; returns (ok, front_x, rot)."""
    skel = unreal.load_asset(base.SKEL)
    ref = unreal.load_asset(base.REF_IDLE)
    before = skel_bones(skel)
    probe_dir = "/Game/Temp/TownClipProbeV2"
    fixrot = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(0, -1, 0), unreal.Vector(1, 0, 0))
    # fbx_v8 comes in upright (no extra bone) but faces UE +Y; a yaw of -90 matches the player set (front +X).
    cands = [("front_x_off", False, None), ("front_x_on", True, None), ("old_root_fix_rotation", False, fixrot),
             ("front_x_off_yaw_-90", False, unreal.Rotator(0, 0, -90))]
    rc = {"fbx": "town_v1_fixes/townsfolk_poses_v8/fbx_v8/A_Player_Base_TF_Idle_A_v1.fbx", "skeleton_bones_before": len(before)}
    errs = {}
    for i, (key, fx, rot) in enumerate(cands):
        a = base.run_import(fbx_of("TF_Idle_A"), probe_dir, "Probe_{}".format(i), base.fbx_anim(skel, fx, rot))
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
    rc["idle_f1_error_cm"] = errs
    best = min(errs, key=errs.get)
    rc["chosen"] = best
    rc["ok"] = (not rc["extra_bones"]) and (not rc["armature_track"]) and rc["root_is_top"] and errs[best] <= 1.0 \
        and rc["anim_skeleton"] == skel.get_path_name()
    report["rig_check"] = rc
    log("rig check: {}".format(json.dumps(rc)[:1500]))
    _, fx, rot = [c for c in cands if c[0] == best][0]
    return rc["ok"], fx, rot


def hand_head(anim):
    """Per-frame distance (cm) from each hand bone to the head bone, world space."""
    if anim is None:
        return None
    n = base.frames(anim) or 0
    names = bone_names(anim)
    head = next((b for b in names if re.match(r"^head$", b, re.I)), None) or next((b for b in names if "head" in b.lower()), None)
    hands = [b for b in names if re.match(r"^hand_[lr]$", b, re.I)] or [b for b in names if "hand" in b.lower()][:2]
    rows = []
    for f in range(0, n + 1):
        p = base.pose(anim, f)
        hp = unreal.AnimPoseExtensions.get_bone_pose(p, head, unreal.AnimPoseSpaces.WORLD).translation
        d = {}
        for h in hands:
            t = unreal.AnimPoseExtensions.get_bone_pose(p, h, unreal.AnimPoseSpaces.WORLD).translation
            d[h] = round((t - hp).length(), 2)
        rows.append(d)
    out = {"head_bone": head, "hand_bones": hands, "frames": n}
    for h in hands:
        v = [r[h] for r in rows]
        out[h] = {"rest_f0": v[0], "min": min(v), "min_frame": v.index(min(v)), "max": max(v),
                  "frames_within_25cm": sum(1 for x in v if x <= 25.0)}
    out["both_within_25cm_frames"] = sum(1 for r in rows if all(x <= 25.0 for x in r.values()))
    out["per_frame"] = rows
    return out


def foot_slide(anim, f0, f1):
    names = bone_names(anim)
    feet = [b for b in names if re.match(r"^(foot|ankle)_[lr]$", b, re.I)] or [b for b in names if "foot" in b.lower()]
    worst = 0.0
    for b in feet:
        p0 = unreal.AnimPoseExtensions.get_bone_pose(base.pose(anim, f0), b, unreal.AnimPoseSpaces.WORLD).translation
        for f in range(f0, f1 + 1):
            t = unreal.AnimPoseExtensions.get_bone_pose(base.pose(anim, f), b, unreal.AnimPoseSpaces.WORLD).translation
            worst = max(worst, ((t.x - p0.x) ** 2 + (t.y - p0.y) ** 2) ** 0.5)
    return round(worst, 3)


def do_clips(front_x, rot):
    skel = unreal.load_asset(base.SKEL)
    old_mask = base.ANIM_DEST + "/A_Player_Base_TF_Adjust_Mask"
    report["adjust_mask_old"] = hand_head(unreal.load_asset(old_mask) if EAL.does_asset_exist(old_mask) else None)
    old_bow = base.ANIM_DEST + "/A_Player_Base_TF_Bow_Court"
    if EAL.does_asset_exist(old_bow):
        a = unreal.load_asset(old_bow)
        report["bow_court_old_foot_slide_cm"] = foot_slide(a, 0, base.frames(a) or 0)
    # A replace_existing import reuses the stored import settings, so start the folder fresh.
    if EAL.does_directory_exist(base.ANIM_DEST):
        EAL.delete_directory(base.ANIM_DEST)
    for clip in V8_CLIPS + J_CLIPS:
        meta = (V8_META if clip in V8_META else J_META)[clip]
        cr = {"checks": {}, "set": "town_v1 fbx_v8" if clip in V8_META else "chunk_J fbx_v4"}
        report["clips"][clip] = cr
        try:
            a = base.run_import(fbx_of(clip), base.ANIM_DEST, "A_Player_Base_" + clip, base.fbx_anim(skel, front_x, rot))
            cr["fbx"] = os.path.relpath(fbx_of(clip), KIT).replace("\\", "/")
            cr["skeleton"] = a.get_editor_property("skeleton").get_path_name()
            cr["frames"] = base.frames(a)
            cr["frames_expected"] = meta["frames"]
            cr["feet_min_z"] = base.feet_min_z(a, cr["frames"])
            cr["checks"]["skeleton"] = cr["skeleton"] == skel.get_path_name()
            cr["checks"]["frames"] = cr["frames"] in (meta["frames"], meta["frames"] + 1)
            cr["checks"]["feet"] = cr["feet_min_z"] is not None and cr["feet_min_z"] >= base.CLIP_LIMIT_Z
            p0 = base.pose(a, 0)
            fl = unreal.AnimPoseExtensions.get_bone_pose(p0, "foot_l", unreal.AnimPoseSpaces.WORLD).translation
            fr = unreal.AnimPoseExtensions.get_bone_pose(p0, "foot_r", unreal.AnimPoseSpaces.WORLD).translation
            hd = unreal.AnimPoseExtensions.get_bone_pose(p0, "head", unreal.AnimPoseSpaces.WORLD).translation
            cr["f0_vs_idle_cm"] = base.pose_error(p0, base.pose(unreal.load_asset(base.REF_IDLE), 0))
            cr["checks"]["upright_facing_x"] = hd.z > 90.0 and fl.y < fr.y and abs(fl.x - fr.x) < abs(fl.y - fr.y) + 1.0
            cr["checks"]["no_armature_track"] = not any("armature" in t.lower() for t in (anim_tracks(a) or []))
            cr["checks"]["source_paths_clean"] = not any(base.BAD_PATH.search(p) for p in (base.source_files(a) or []))
            if clip == "TF_Adjust_Mask":
                report["adjust_mask_v8"] = hand_head(a)
            if clip == "TF_Bow_Court":
                cr["foot_slide_cm"] = foot_slide(a, 0, cr["frames"] or 0)
        except Exception as e:
            cr["error"] = str(e) + "\n" + traceback.format_exc()
        cr["pass"] = "error" not in cr and all(cr["checks"].values())
        log("{}: pass={} {}".format(clip, cr["pass"], cr["checks"]))


def do_meshes():
    base.KIT, base.KIT_NAME = KIT, "towns_v2"
    for entry in json.load(open(WHITELIST, encoding="utf-8"))["meshes"]:
        r = {"checks": {}}
        report["meshes"][entry["asset"]] = r
        try:
            base.do_mesh(entry, r)
        except Exception as e:
            r["error"] = str(e) + "\n" + traceback.format_exc()
        r["pass"] = "error" not in r and all(v is True for v in r["checks"].values())
        log("{}: pass={} {}".format(entry["asset"], r["pass"], r["checks"]))
    # town_v1 fix: the fountain v7 (same asset path, still refused, kit town_v1).
    base.KIT, base.KIT_NAME = V1_KIT, "town_v1"
    for entry in json.load(open(WHITELIST_V1, encoding="utf-8"))["meshes"]:
        if entry["asset"] == "SM_Town_Fountain":
            r = {"checks": {}, "fix": "v7 sittable rim"}
            report["meshes"]["SM_Town_Fountain (town_v1 fix)"] = r
            try:
                base.do_mesh(entry, r)
            except Exception as e:
                r["error"] = str(e) + "\n" + traceback.format_exc()
            r["pass"] = "error" not in r and all(v is True for v in r["checks"].values())
            log("SM_Town_Fountain v7: pass={} {}".format(r["pass"], r["checks"]))


def main():
    try:
        ok, fx, rot = rig_check()
    except Exception as e:
        ok, fx, rot = False, False, None
        report["rig_check_error"] = str(e) + "\n" + traceback.format_exc()
        log("rig check failed: " + str(e))
    if PHASE != "rig":
        if os.environ.get("GFTOWN_SKIP_MESHES") != "1":
            do_meshes()
        if os.environ.get("GFTOWN_SKIP_CLIPS") != "1":
            if ok:
                try:
                    do_clips(fx, rot)
                except Exception as e:
                    report["clips_error"] = str(e) + "\n" + traceback.format_exc()
                    log("clips failed: " + str(e))
            else:
                log("rig check did not pass: clips not imported")
    report["summary"] = {
        "rig_ok": ok,
        "meshes": len(report["meshes"]), "meshes_pass": sum(1 for r in report["meshes"].values() if r["pass"]),
        "clips": len(report["clips"]), "clips_pass": sum(1 for r in report["clips"].values() if r["pass"]),
    }
    name = "towns_v2_rig_report.json" if PHASE == "rig" else "towns_v2_report.json"
    with open(os.path.join(base.OUT, name), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    log("summary {}".format(report["summary"]))


main()
