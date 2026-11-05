"""Accel Hop tutorial visual pass v1 (issue 348): sky, sun, atmosphere, fog, post-process, cel tile colours, floor.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/accelhop_visual_pass_v1.py" -unattended -nullrhi -nosplash

Needs accelhop_visual_palette_v1.py next to it. Run Tools/import_acceleration_hop.py and
Tools/create_accel_hop_tutorial_map.py first (the tiles, M_GF_HopCel and MI_Hop_Tile_<Kind> must exist, and so must
L_AccelHop_Tutorial_v1).

What it does, in order:
1. Copies /Game/Maps/L_AccelHop_Tutorial_v1 to /Game/Maps/L_AccelHop_Tutorial_v2 once (asset duplicate). v1 and L_Beat
   are never opened for edit or saved; their .umap files are hashed before and after and the report fails if either
   changed. The target must be L_AccelHop_Tutorial_v<N> with N >= 2.
2. Makes the tutorial materials under /Game/Art/AccelerationHop/Materials/Tutorial (new assets only, world road
   instances are only read as parents):
   - MI_Hop_Tile_<Key>_Tut_v1, children of MI_Hop_Tile_<Kind>, stronger tints (see the palette module).
   - M_GF_TutFloor_v1, a flat lit floor with a 200 cm grid (so the player's shadow still reads between tiles).
   An existing tutorial instance whose values differ from the palette fails the run instead of being changed
   (set GFVIS_REAPPLY=1 to write the palette values over it; prefer a _v2 palette instead).
3. In the v2 map only: removes the placeholder light rig and any sun, sky light, sky atmosphere, height fog or cloud
   left from the L_Beat copy, then places one rig tagged GFAccelHopTutorial.Visual (folder AccelHopTutorial/Visual):
   Tutorial_Sun, Tutorial_SkyLight, Tutorial_SkyAtmosphere, Tutorial_Fog, Tutorial_Look (unbound post-process volume,
   fixed manual exposure like the towns, bloom only above 1, slight saturation). Other post-process volumes are left
   alone; Tutorial_Look has priority 10 so its settings win.
4. Sets the floor material on Tutorial_Floor and the tutorial instance on every tile tagged GFAccelHopTutorial
   (kind from the GFAccelHop_<Kind> tag, lesson from GFAccelHopTutorial.Lesson.<Id>).
5. Saves the v2 map.

Reruns are safe: the rig is removed and placed again, overrides are set again, existing assets are kept.
If Tools/create_accel_hop_tutorial_map.py is rerun on v2 (GFTUT_MAP=/Game/Maps/L_AccelHop_Tutorial_v2), it respawns the
floor and tiles without the overrides: run this script again after it. It keeps the visual rig (different tag), and
does not add its placeholder lights because the rig's classes are present.

Env: GFVIS_SOURCE (default v1), GFVIS_MAP (default v2), GFVIS_REAPPLY (0).
Writes Saved/ArtImport/accelhop_visual_pass_v1_report.json and logs "[gfvis]" lines.
"""
import hashlib
import json
import os
import re
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import accelhop_visual_palette_v1 as pal  # noqa: E402

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.environ.get("GFVIS_SOURCE", "").strip() or pal.SOURCE_MAP
MAP = os.environ.get("GFVIS_MAP", "").strip() or pal.TARGET_MAP
REAPPLY = os.environ.get("GFVIS_REAPPLY", "0").strip() == "1"
OUT = os.path.join(PROJ, "Saved", "ArtImport")
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
TUT_TAG = "GFAccelHopTutorial"
PLACEHOLDER_LIGHT_TAG = "GFAccelHopTutorial.Light"
ENV_CLASSES = ("DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog", "VolumetricCloud")
TOL = 1e-3

report = {"map": MAP, "source": SOURCE, "palette": pal.VERSION, "log": [], "warnings": []}


def log(m):
    unreal.log("[gfvis] " + str(m))
    report["log"].append(str(m))


def warn(m):
    unreal.log_warning("[gfvis] " + str(m))
    report["warnings"].append(str(m))


def try_set(obj, prop, val):
    try:
        obj.set_editor_property(prop, val)
        return True
    except Exception as e:
        warn("cannot set {}.{}: {}".format(type(obj).__name__, prop, e))
        return False


def lc(rgb, a=1.0):
    return unreal.LinearColor(float(rgb[0]), float(rgb[1]), float(rgb[2]), float(a))


def srgb_color(hex_):
    r, g, b = (int(round(c * 255)) for c in pal.hex_to_srgb(hex_))
    return unreal.Color(r=r, g=g, b=b, a=255)


# ---------- map copy and guards ----------
def package_file(package):
    if not package.startswith("/Game/"):
        raise ValueError("not a /Game package: " + package)
    return os.path.join(PROJ, "Content", *package[len("/Game/"):].split("/")) + ".umap"


def file_hash(path):
    if not os.path.isfile(path):
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def check_target(source, target):
    m = re.match(r"^L_AccelHop_Tutorial_v(\d+)$", target.rsplit("/", 1)[-1])
    if target == source or target.rstrip("/").endswith("/L_Beat") or not m or int(m.group(1)) < 2:
        raise RuntimeError("refusing to write " + target + ": the visual pass saves a new L_AccelHop_Tutorial_v<N>, N >= 2")


def ensure_copy(source, target):
    check_target(source, target)
    if EAL.does_asset_exist(target):
        return "exists"
    if not EAL.does_asset_exist(source):
        raise RuntimeError("no source map at " + source + " (run Tools/create_accel_hop_tutorial_map.py first)")
    if EAL.duplicate_asset(source, target) is not None:
        return "created"
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(source):
        raise RuntimeError("duplicate_asset failed and load_level failed " + source)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, target):
        raise RuntimeError("could not copy " + source + " -> " + target)
    return "created (save_map)"


# ---------- materials ----------
def create_asset(path, cls, factory):
    d, n = path.rsplit("/", 1)
    a = AT.create_asset(n, d, cls, factory)
    if a is None:
        raise RuntimeError("could not create " + path)
    return a


def mi_matches(mi, parent, params):
    diffs = []
    p = mi.get_editor_property("parent")
    if p is None or p.get_path_name().split(".")[0] != parent.get_path_name().split(".")[0]:
        diffs.append("parent")
    for name, want in params["vectors"].items():
        got = MEL.get_material_instance_vector_parameter_value(mi, name)
        if any(abs(a - b) > TOL for a, b in zip((got.r, got.g, got.b), want)):
            diffs.append(name)
    for name, want in params["scalars"].items():
        if abs(MEL.get_material_instance_scalar_parameter_value(mi, name) - want) > TOL:
            diffs.append(name)
    return diffs


def make_tile_instance(key):
    path = pal.mi_path(key)
    parent_path = pal.mi_parent(key)
    if not EAL.does_asset_exist(parent_path):
        raise RuntimeError("missing parent " + parent_path + " (run Tools/import_acceleration_hop.py first)")
    parent = unreal.load_asset(parent_path)
    params = pal.mi_params(key)
    rep = {"path": path, "parent": parent_path}
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path)
        diffs = mi_matches(mi, parent, params)
        if not diffs:
            rep["state"] = "kept (matches palette)"
            return mi, rep
        if not REAPPLY:
            raise RuntimeError("{} exists with different {}; bump the palette to v2 or set GFVIS_REAPPLY=1".format(path, diffs))
        rep["state"] = "reapplied " + ",".join(diffs)
    else:
        mi = create_asset(path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        rep["state"] = "created"
    MEL.set_material_instance_parent(mi, parent)
    for name, v in params["vectors"].items():
        MEL.set_material_instance_vector_parameter_value(mi, name, lc(v))
    for name, v in params["scalars"].items():
        MEL.set_material_instance_scalar_parameter_value(mi, name, float(v))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    left = mi_matches(mi, parent, params)
    rep["ok"] = not left
    if left:
        warn("{} did not take {}".format(path, left))
    return mi, rep


FLOOR_HLSL = r"""
float2 P = W.xy / max(Grid, 1.0);
float2 F = abs(frac(P + 0.5) - 0.5) * max(Grid, 1.0);
float D = min(F.x, F.y) - 0.5 * LineWidth;
float AA = max(fwidth(D), 0.001);
float L = 1.0 - saturate(D / AA);
return lerp(BaseColor.rgb, LineColor.rgb, L);
"""


def make_floor_material():
    path = pal.TUT_MAT_DIR + "/" + pal.floor_material_name()
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path), {"path": path, "state": "kept (exists; a new look is M_GF_TutFloor_v2)"}
    mat = create_asset(path, unreal.Material, unreal.MaterialFactoryNew())
    f = pal.FLOOR
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, 0, 0)
    c.set_editor_property("code", FLOOR_HLSL)
    c.set_editor_property("description", "TutFloorGrid")
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for n in ("W", "BaseColor", "LineColor", "Grid", "LineWidth"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        pins.append(ci)
    c.set_editor_property("inputs", pins)
    wpos = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -600, -200)
    srcs = {"W": wpos}
    for i, (n, hex_) in enumerate((("BaseColor", f["base_hex"]), ("LineColor", f["line_hex"]))):
        v = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, -60 + i * 140)
        v.set_editor_property("parameter_name", n)
        v.set_editor_property("default_value", lc(pal.hex_to_linear(hex_)))
        srcs[n] = v
    for i, (n, val) in enumerate((("Grid", f["grid_cm"]), ("LineWidth", f["line_cm"]))):
        s = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 240 + i * 90)
        s.set_editor_property("parameter_name", n)
        s.set_editor_property("default_value", float(val))
        srcs[n] = s
    wired = [n for n, e in srcs.items() if MEL.connect_material_expressions(e, "", c, n)]
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for prop, val, y in ((unreal.MaterialProperty.MP_ROUGHNESS, f["roughness"], 400), (unreal.MaterialProperty.MP_SPECULAR, f["specular"], 480)):
        k = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -200, y)
        k.set_editor_property("r", float(val))
        MEL.connect_material_property(k, "", prop)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    ok = sorted(wired) == sorted(srcs)
    if not ok:
        warn("floor material wired only {}".format(wired))
    return mat, {"path": path, "state": "created", "inputs_wired": wired, "ok": ok}


# ---------- level ----------
def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _tags(actor):
    return [str(t) for t in actor.tags]


def clear_environment(eas):
    removed = []
    for a in eas.get_all_level_actors():
        tags = _tags(a)
        cls = a.get_class().get_name()
        if pal.VIS_TAG in tags or PLACEHOLDER_LIGHT_TAG in tags or cls in ENV_CLASSES:
            removed.append("{} ({})".format(a.get_actor_label(), cls))
    doomed = [a for a in eas.get_all_level_actors()
              if pal.VIS_TAG in _tags(a) or PLACEHOLDER_LIGHT_TAG in _tags(a) or a.get_class().get_name() in ENV_CLASSES]
    if doomed:
        eas.destroy_actors(doomed)
    return removed


def _place(eas, cls, label, loc=(0.0, 0.0, 0.0), rot=(0.0, 0.0, 0.0)):
    a = eas.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=rot[0], pitch=rot[1], yaw=rot[2]))
    a.tags = [unreal.Name(pal.VIS_TAG)]
    a.set_actor_label(label)
    a.set_folder_path(pal.VIS_FOLDER)
    return a


def place_rig(eas):
    s, placed = pal.SUN, []
    sun = _place(eas, unreal.DirectionalLight, "Tutorial_Sun", (0.0, 0.0, 2000.0), (0.0, s["pitch"], s["yaw"]))
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    try_set(sc, "intensity", float(s["intensity_lux"]))
    try_set(sc, "light_color", srgb_color(s["color_hex"]))
    try_set(sc, "atmosphere_sun_light", True)
    try_set(sc, "dynamic_shadow_cascades", int(s["cascades"]))
    try_set(sc, "dynamic_shadow_distance_movable_light", float(s["dynamic_shadow_distance_cm"]))
    placed.append("Tutorial_Sun")

    k = pal.SKY_LIGHT
    sky = _place(eas, unreal.SkyLight, "Tutorial_SkyLight", (0.0, 0.0, 1500.0))
    kc = sky.get_component_by_class(unreal.SkyLightComponent)
    kc.set_mobility(unreal.ComponentMobility.MOVABLE)
    try_set(kc, "real_time_capture", bool(k["real_time_capture"]))
    try_set(kc, "intensity", float(k["intensity"]))
    try_set(kc, "lower_hemisphere_is_black", False)
    try_set(kc, "lower_hemisphere_color", lc(pal.hex_to_linear(k["lower_hemisphere_hex"])))
    placed.append("Tutorial_SkyLight")

    atm = _place(eas, unreal.SkyAtmosphere, "Tutorial_SkyAtmosphere")
    ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
    for prop, val in pal.SKY_ATMOSPHERE.items():
        try_set(ac, prop, float(val))
    placed.append("Tutorial_SkyAtmosphere")

    g = pal.FOG
    fog = _place(eas, unreal.ExponentialHeightFog, "Tutorial_Fog")
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    try_set(fc, "fog_density", float(g["density"]))
    try_set(fc, "start_distance", float(g["start_distance_cm"]))
    try_set(fc, "enable_volumetric_fog", bool(g["volumetric"]))
    try_set(fc, "fog_inscattering_luminance", lc(pal.hex_to_linear(g["inscatter_hex"])))
    placed.append("Tutorial_Fog")

    p = pal.POST
    ppv = _place(eas, unreal.PostProcessVolume, "Tutorial_Look")
    try_set(ppv, "unbound", True)
    try_set(ppv, "priority", float(p["priority"]))
    try_set(ppv, "blend_weight", 1.0)
    st = ppv.get_editor_property("settings")
    sat, con = p["saturation"], p["contrast"]
    for prop, val in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                      ("auto_exposure_bias", float(p["exposure_bias"])),
                      ("auto_exposure_apply_physical_camera_exposure", False),
                      ("auto_exposure_min_brightness", float(p["exposure_min"])),
                      ("auto_exposure_max_brightness", float(p["exposure_max"])),
                      ("bloom_intensity", float(p["bloom_intensity"])),
                      ("bloom_threshold", float(p["bloom_threshold"])),
                      ("color_saturation", unreal.Vector4(sat, sat, sat, 1.0)),
                      ("color_contrast", unreal.Vector4(con, con, con, 1.0)),
                      ("vignette_intensity", float(p["vignette"])),
                      ("motion_blur_amount", float(p["motion_blur"])),
                      ("film_grain_intensity", float(p["grain"])),
                      ("scene_fringe_intensity", float(p["chromatic_aberration"])),
                      ("lens_flare_intensity", float(p["lens_flare"]))):
        if try_set(st, "override_" + prop, True):
            try_set(st, prop, val)
    ppv.set_editor_property("settings", st)
    placed.append("Tutorial_Look")
    return placed


def tile_kind(actor):
    for t in _tags(actor):
        if t.startswith("GFAccelHop_") and t[len("GFAccelHop_"):] in pal.KINDS:
            return t[len("GFAccelHop_"):]
    try:
        name = str(actor.get_kind()).rsplit(".", 1)[-1]
        for k in pal.KINDS:
            if k.upper() == name.upper():
                return k
    except Exception:
        pass
    return None


def apply_materials(eas, instances, floor_mat):
    rep = {"tiles": {}, "tiles_failed": [], "floor": "missing"}
    for a in eas.get_all_level_actors():
        tags = _tags(a)
        if TUT_TAG not in tags:
            continue
        if "GFAccelHopTutorial.Floor" in tags:
            comp = a.static_mesh_component
            comp.set_material(0, floor_mat)
            rep["floor"] = "set" if comp.get_material(0) == floor_mat else "failed"
            continue
        if a.get_class().get_name() != "GFAccelHopTile":
            continue
        kind, lesson = tile_kind(a), pal.lesson_of_tags(tags)
        try:
            key = pal.mi_key(kind, lesson)
            comp = a.get_editor_property("mesh")
            comp.set_material(0, instances[key])
            if comp.get_material(0) != instances[key]:
                raise RuntimeError("override did not stick")
            rep["tiles"][key] = rep["tiles"].get(key, 0) + 1
        except Exception as e:
            rep["tiles_failed"].append("{}: {}".format(a.get_actor_label(), e))
    rep["tiles_total"] = sum(rep["tiles"].values())
    return rep


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    check_target(SOURCE, MAP)
    guarded = {p: package_file(p) for p in (SOURCE, "/Game/Maps/L_Beat")}
    before = {p: file_hash(f) for p, f in guarded.items()}
    report["copy"] = ensure_copy(SOURCE, MAP)
    log("map copy " + report["copy"])

    instances, mats = {}, {}
    for key in pal.all_mi_keys():
        instances[key], mats[pal.mi_name(key)] = make_tile_instance(key)
    floor_mat, mats[pal.floor_material_name()] = make_floor_material()
    report["materials"] = mats

    if not les.load_level(MAP):
        raise RuntimeError("load_level failed " + MAP)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or not world.get_path_name().startswith(MAP):
        raise RuntimeError("open level is not " + MAP + "; not changing anything")
    eas = _eas()
    report["removed"] = clear_environment(eas)
    report["rig"] = place_rig(eas)
    report.update(apply_materials(eas, instances, floor_mat))
    log("rig {}, removed {}, tiles {} ({}), floor {}".format(report["rig"], len(report["removed"]), report["tiles_total"],
                                                          report["tiles"], report["floor"]))
    if not les.save_current_level():
        raise RuntimeError("save_current_level failed")
    after = {p: file_hash(f) for p, f in guarded.items()}
    report["guarded_hashes"] = {p: {"before": before[p], "after": after[p]} for p in guarded}
    report["sources_untouched"] = before == after
    if before != after:
        raise RuntimeError("a source map changed on disk; restore it from git: " + str([p for p in guarded if before[p] != after[p]]))
    report["ok"] = (report["tiles_total"] == 56 and not report["tiles_failed"] and report["floor"] == "set"
                    and all(m.get("ok", True) for m in mats.values()))
    if report["tiles_total"] != 56:
        warn("expected 56 tutorial tiles, got {}".format(report["tiles_total"]))


if os.environ.get("GFVIS_LIB") != "1":
    try:
        main()
    except Exception as e:
        report["ok"] = False
        log("FAIL " + str(e) + "\n" + traceback.format_exc())
    finally:
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, "accelhop_visual_pass_v1_report.json"), "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
