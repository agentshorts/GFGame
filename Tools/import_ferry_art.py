"""Headless import of the ferry kit (handoff batch ferry_v1) without re-importing the Ripwing.

Run:
  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_ferry_art.py" -unattended -nullrhi -nosplash
Reuses the helpers in Tools/import_art_batch.py (only its definitions run, not its import loop).
- SM_FerryPerch, SM_FerryBasket -> /Game/Art/Props/Ferry (UCX collision, MI_<Name> on M_GF_Prop). Mesh sockets the
  manifests list as points: basket Grip (0,0,193.75) and Seat (-25,0,12.5); perch PerchPoint (-95,0,262.5) and BasketSpot (0,0,12.5).
- UI_Ferry_v1 -> /Game/Art/UI/Ferry (UI group, nearest, no mips).
- SK_Dino_Ripwing v2: only the 3 new clips (Ferry_Takeoff, Ferry_Carry_Fly, Ferry_Land) onto the committed SKEL_Dino_Ripwing,
  plus socket FerryGrip on talon_l. Mesh, skeleton, textures and the 27 v1 clips are left alone.
  FerryGrip has a Blender bone-space X component; the UE axis for it is chosen by matching the manifest's per-key-frame
  socket heights, and the run fails if no single mapping matches.
- MI_Dino_Ripwing_Ferry (M_GF_DyeMask, Ripwing _D/_M, Dye_R sunflower #FFC93C) for ferry birds; not assigned to the mesh.
Writes Saved/ArtImport/ferry_report.json.
"""
import json, os, re, traceback
import unreal

_here = os.path.dirname(os.path.abspath(__file__))
os.environ["GFART_ONLY"] = "__none__"  # keep the batch importer's ORDER selection empty; only its helpers are used
_src = open(os.path.join(_here, "import_art_batch.py"), encoding="utf-8").read()
_cut = _src.index("# Clean re-import")
exec(compile(_src[:_cut], os.path.join(_here, "import_art_batch.py"), "exec"), globals())
report = {"assets": {}, "log": []}  # fresh report (the helpers log into this name)

STATICS = ["SM_FerryPerch", "SM_FerryBasket"]
UI = "UI_Ferry_v1"
RIP = "SK_Dino_Ripwing"
RIP_DIR = "/Game/Art/Dinos/Ripwing"
NEW_CLIPS = ["A_Dino_Ripwing_Ferry_Takeoff", "A_Dino_Ripwing_Ferry_Carry_Fly", "A_Dino_Ripwing_Ferry_Land"]
MESH_SOCKETS = {
    "SM_FerryBasket": {"Grip": (0.0, 0.0, 193.75), "Seat": (-25.0, 0.0, 12.5)},
    "SM_FerryPerch": {"PerchPoint": (-95.0, 0.0, 262.5), "BasketSpot": (0.0, 0.0, 12.5)},
}
CREST = "#FFC93C"
FERRY_MI = RIP_DIR + "/Materials/MI_Dino_Ripwing_Ferry"

def man_of(n):
    return json.load(open(os.path.join(INBOX, n, "manifest.json"), encoding="utf-8"))

def srgb_hex(h):
    h = h.lstrip("#")
    def lin(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(lin(int(h[0:2], 16)), lin(int(h[2:4], 16)), lin(int(h[4:6], 16)), 1.0)

def add_mesh_sockets(m, socks, r):
    for sname, loc in socks.items():
        if m.find_socket(sname) is not None:
            m.remove_socket(m.find_socket(sname))
        s = unreal.StaticMeshSocket(m)
        s.set_editor_property("socket_name", sname)
        s.set_editor_property("relative_location", unreal.Vector(*loc))
        m.add_socket(s)
    EAL.save_loaded_asset(m)
    m = unreal.load_asset(m.get_path_name().split(".")[0])
    got = {}
    for sname, loc in socks.items():
        s = m.find_socket(sname)
        got[sname] = None if s is None else vec(s.get_editor_property("relative_location"))
    r["mesh_sockets"] = got
    r["checks"]["mesh_sockets"] = all(got[n] is not None and all(abs(a - b) < 1e-3 for a, b in zip(got[n], loc)) for n, loc in socks.items())

def clean(paths):
    for a in paths:
        if EAL.does_asset_exist(a):
            log("pre-clean delete {} ok={}".format(a, EAL.delete_asset(a)))

# ---- statics + UI (pre-clean only their own assets) ----
mans = {n: man_of(n) for n in STATICS + [UI, RIP]}
doomed = []
for n in STATICS:
    m = mans[n]
    dest = m["unreal_target"].rsplit("/", 1)[0]
    doomed += [m["unreal_target"], prop_mi_path(m)] + [dest + "/Textures/" + os.path.splitext(t)[0] for t in m["files"]["textures"]]
doomed += owned_assets(mans[UI])
doomed += [RIP_DIR + "/Anims/" + c for c in NEW_CLIPS] + [FERRY_MI]
clean(doomed)

for n in STATICS + [UI]:
    r = {"checks": {}}
    report["assets"][n] = r
    try:
        m = mans[n]
        r["expected"] = {"dims_cm": m.get("dimensions_cm"), "tris": m.get("tri_count"), "slots": m.get("material_slots")}
        if m["type"] == "texture_set":
            do_texture_set(n, m, os.path.join(INBOX, n), r)
        else:
            notes = m["qa"].get("notes", "")
            do_static(n, m, os.path.join(INBOX, n), r)
            prop_materials(n, m, r)
            add_mesh_sockets(unreal.load_asset(m["unreal_target"]), MESH_SOCKETS[n], r)
    except Exception as e:
        r["error"] = str(e) + "\n" + traceback.format_exc()
    r["pass"] = "error" not in r and all(v is True or v == "n/a" for v in r["checks"].values())
    log("{}: pass={} checks={}".format(n, r["pass"], r["checks"]))

# ---- Ripwing: 3 clips + FerryGrip ----
r = {"checks": {}}
report["assets"][RIP] = r
try:
    m = mans[RIP]
    folder = os.path.join(INBOX, RIP)
    skel = unreal.load_asset(RIP_DIR + "/SKEL_Dino_Ripwing")
    mesh = unreal.load_asset(RIP_DIR + "/SK_Dino_Ripwing")
    if skel is None or mesh is None:
        raise RuntimeError("committed Ripwing mesh/skeleton missing")
    if mesh.skeleton.get_path_name() != skel.get_path_name():
        raise RuntimeError("SK_Dino_Ripwing is not on SKEL_Dino_Ripwing")
    bones = pose_bones(ref_pose(skel))
    r["bone_count"] = len(bones)
    r["checks"]["bone_count"] = len(bones) == m["skeleton"]["bone_count"]
    clips_expected = parse_clips(m["qa"]["notes"])
    notes = m["qa"]["notes"]
    heights = {}
    for c in NEW_CLIPS:
        mm = re.search(re.escape(c) + r" \d+f .*?keys \[([\d, ]+)\].*?height above the actor origin by key frame \(cm\) \[([-\d., ]+)\]", notes)
        if mm:
            heights[c] = list(zip([int(x) for x in mm.group(1).split(",")], [float(x) for x in mm.group(2).split(",")]))
    r["ferrygrip_heights_expected"] = heights
    anims = {}
    r["clips"] = {}
    for c in NEW_CLIPS:
        cr = {}
        r["clips"][c] = cr
        try:
            a = run_import(os.path.join(folder, c + ".fbx"), RIP_DIR + "/Anims", c, fbx_options("anim", skel))
            anims[c] = a
            nf = anim_frames(a)
            cr["frames"] = nf; cr["keys"] = anim_keys(a)
            cr["length_s"] = round(a.get_play_length(), 4)
            cr["skeleton_ok"] = a.get_editor_property("skeleton").get_path_name() == skel.get_path_name()
            exp = clips_expected.get(c) or clips_expected.get(c.split("_", 3)[-1])
            cr["expected"] = exp
            cr["frames_ok"] = bool(exp) and nf == exp["frames"]
            root = bones[0]
            dev = 0.0
            for f in range(0, (nf or 0) + 1):
                rs = bone_t(pose_at(a, f), root, unreal.AnimPoseSpaces.LOCAL).scale3d
                dev = max(dev, abs(rs.x - 1), abs(rs.y - 1), abs(rs.z - 1))
            cr["root_scale_max_dev"] = round(dev, 6)
            p0 = pose_at(a, 0); pn = pose_at(a, nf)
            worst = 0.0
            for b in bones:
                dt, da, ds = tdiff(bone_t(p0, b, unreal.AnimPoseSpaces.LOCAL), bone_t(pn, b, unreal.AnimPoseSpaces.LOCAL))
                worst = max(worst, dt + da + ds)
            cr["first_last_diff_sum"] = round(worst, 4)
            if exp and exp["loop"]:
                cr["loop_closes"] = worst < 0.1
            cr["pass"] = cr["skeleton_ok"] and cr["frames_ok"] and cr["root_scale_max_dev"] < 1e-4 and cr.get("loop_closes", True)
        except Exception as e:
            cr["error"] = str(e) + "\n" + traceback.format_exc(); cr["pass"] = False
    r["checks"]["clips"] = all(cr["pass"] for cr in r["clips"].values())

    # FerryGrip offset: Blender bone-space (bx, by, bz); by (along the bone) -> UE +Z as for Grab.
    off = m["skeleton"]["sockets"]["FerryGrip"]["offset_cm"]
    bx, by, bz = float(off[0]), float(off[1]), float(off[2])
    if abs(bz) > 1e-6:
        raise RuntimeError("FerryGrip Z component not handled: " + str(off))
    cands = {"+X": unreal.Vector(bx, 0, by), "-X": unreal.Vector(-bx, 0, by), "+Y": unreal.Vector(0, bx, by), "-Y": unreal.Vector(0, -bx, by)}
    errs = {}
    for k, v in cands.items():
        e = []
        for c, hs in heights.items():
            if c not in anims:
                continue
            for f1, h in hs:
                t = bone_t(pose_at(anims[c], f1 - 1), "talon_l", unreal.AnimPoseSpaces.WORLD)
                e.append(abs(t.transform_location(v).z - h))
        errs[k] = round(max(e), 3) if e else None
    r["ferrygrip_candidate_max_err_cm"] = errs
    ok_c = [k for k, v in errs.items() if v is not None and v <= 0.1]
    # midpoint check against talon_r (if present), mirrored Grab-style point
    if "talon_r" in bones and "A_Dino_Ripwing_Ferry_Takeoff" in anims:
        p = pose_at(anims["A_Dino_Ripwing_Ferry_Takeoff"], 21)
        tl = bone_t(p, "talon_l", unreal.AnimPoseSpaces.WORLD); tr = bone_t(p, "talon_r", unreal.AnimPoseSpaces.WORLD)
        r["takeoff_f22_talons"] = {"talon_l": vec(tl.translation), "talon_r": vec(tr.translation),
                                   "cands": {k: vec(tl.transform_location(v)) for k, v in cands.items()}}
    r["ferrygrip_mapping"] = ok_c
    if len(ok_c) > 1 and "takeoff_f22_talons" in r:
        # Heights cannot separate a lateral sign; the manifest defines FerryGrip as the midpoint
        # between both closed talons, so keep the candidate whose world Y sits on the talon midline.
        mid_y = 0.5 * (r["takeoff_f22_talons"]["talon_l"][1] + r["takeoff_f22_talons"]["talon_r"][1])
        dy = {k: abs(r["takeoff_f22_talons"]["cands"][k][1] - mid_y) for k in ok_c}
        r["ferrygrip_midline_dy_cm"] = {k: round(v, 3) for k, v in dy.items()}
        ok_c = [k for k in ok_c if dy[k] <= 0.5]
        r["ferrygrip_mapping"] = ok_c
    if len(ok_c) != 1:
        raise RuntimeError("FerryGrip axis not unique: " + str(errs))
    chosen = cands[ok_c[0]]
    if mesh.find_socket("FerryGrip") is not None:
        mesh.remove_socket("FerryGrip")
    sock = unreal.SkeletalMeshSocket(mesh)
    sock.set_socket_parent(mesh, "talon_l")
    sock.set_editor_property("relative_location", chosen)
    mesh.add_socket(sock, False)
    if not mesh.rename_socket(sock.get_editor_property("socket_name"), "FerryGrip"):
        raise RuntimeError("socket rename failed")
    EAL.save_loaded_asset(mesh)
    mesh = unreal.load_asset(RIP_DIR + "/SK_Dino_Ripwing")
    got = {}
    for sn in ("Grab", "FerryGrip"):
        s = mesh.find_socket(sn)
        if s is None:
            got[sn] = None; continue
        pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.skeleton)
        bt = unreal.AnimPoseExtensions.get_bone_pose(pose, str(s.get_editor_property("bone_name")), unreal.AnimPoseSpaces.WORLD)
        got[sn] = {"bone": str(s.get_editor_property("bone_name")), "loc": vec(s.get_editor_property("relative_location")),
                   "ref_world": vec(bt.transform_location(s.get_editor_property("relative_location")))}
    r["sockets"] = got
    r["checks"]["sockets"] = got.get("Grab") is not None and got.get("FerryGrip") is not None and got["FerryGrip"]["bone"] == "talon_l"

    # crest-tint MI for ferry birds (M_GF_DyeMask is the skeletal-mesh dye master)
    MEL = unreal.MaterialEditingLibrary
    master_dm = unreal.load_asset(PLAYER_MAT_DIR + "/M_GF_DyeMask")
    mi = AT.create_asset(FERRY_MI.rsplit("/", 1)[1], FERRY_MI.rsplit("/", 1)[0], unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, master_dm)
    MEL.set_material_instance_texture_parameter_value(mi, "Diffuse", unreal.load_asset(RIP_DIR + "/Textures/T_Dino_Ripwing_D"))
    MEL.set_material_instance_texture_parameter_value(mi, "Mask", unreal.load_asset(RIP_DIR + "/Textures/T_Dino_Ripwing_M"))
    MEL.set_material_instance_vector_parameter_value(mi, "Dye_R", srgb_hex(CREST))
    EAL.save_loaded_asset(mi)
    c = MEL.get_material_instance_vector_parameter_value(mi, "Dye_R")
    r["ferry_mi"] = {"path": FERRY_MI, "parent": master_dm.get_name(), "dye_r": [round(c.r, 4), round(c.g, 4), round(c.b, 4)]}
    r["checks"]["ferry_mi"] = master_dm is not None and abs(c.r - srgb_hex(CREST).r) < 1e-3
    r["mesh_slot_material"] = [str(s.material_interface.get_name()) if s.material_interface else None for s in mesh.materials]
except Exception as e:
    r["error"] = str(e) + "\n" + traceback.format_exc()
r["pass"] = "error" not in r and all(v is True or v == "n/a" for v in r["checks"].values())
log("{}: pass={} checks={}".format(RIP, r["pass"], r["checks"]))

delete_redirectors()
with open(os.path.join(OUT, "ferry_report.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=1)
log("report written " + os.path.join(OUT, "ferry_report.json"))
