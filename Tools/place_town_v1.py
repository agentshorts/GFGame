"""Builds /Game/Maps/L_Town_Verdant_A from Carina's Town_Verdant_A_Layout_v2 placements (597 placements, town_v1 kit).

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/place_town_v1.py" -unattended -nullrhi -nosplash

Run after Tools/import_town_v1.py. Env: GFTOWN_KIT (default <project>/handoff/inbox/town_v1).
The map is rebuilt from scratch each run: a flat ground slab with its top at Z = 0 (the kit has no landscape),
sun, sky light, atmosphere, fog, a player start at the main gate (+X), and one StaticMeshActor per placement in
outliner folders by group. The layout names older builds for some meshes (House v1, LampPost v6...); the imported
rebuilds keep the same name, pivot and size, so each placement uses the imported asset of that name.
Writes Saved/ArtImport/town_v1_layout_report.json.
"""
import collections, json, os
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
KIT = os.environ.get("GFTOWN_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "town_v1")
LAYOUT = os.path.join(KIT, "Town_Verdant_A_Layout_v2", "Town_Verdant_A_Layout_v2_placements.json")
WHITELIST = os.path.join(PROJ, "Data", "Town", "town_v1_mineable.json")
MAP = "/Game/Maps/L_Town_Verdant_A"
OUT = os.path.join(PROJ, "Saved", "ArtImport")
EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = {"map": MAP, "missing_assets": [], "placed": 0}


def log(m):
    unreal.log("[gftown] " + str(m))


def spawn(cls, loc, rot=unreal.Rotator(0, 0, 0)):
    return eas.spawn_actor_from_class(cls, loc, rot)


def main():
    layout = json.load(open(LAYOUT, encoding="utf-8"))
    paths = {m["asset"]: m["path"] for m in json.load(open(WHITELIST, encoding="utf-8"))["meshes"]}
    if EAL.does_asset_exist(MAP):
        EAL.delete_asset(MAP)
    if not les.new_level(MAP):
        raise RuntimeError("new_level failed")

    # Ground: engine cube, 20 cm thick, top at z 0, 2 m past the outermost placement on each side.
    P = layout["placements"]
    ext = max(max(abs(p["location_ue_cm"][0]), abs(p["location_ue_cm"][1])) for p in P) + 200.0
    ground = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
    gc = ground.static_mesh_component
    gc.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
    ground.set_actor_scale3d(unreal.Vector(ext * 2 / 100.0, ext * 2 / 100.0, 0.2))
    grass = "/Game/Art/Blocks/Materials/MI_Block_Grass"
    if EAL.does_asset_exist(grass):
        gc.set_material(0, unreal.load_asset(grass))
    ground.set_actor_label("Town_Ground")
    ground.set_folder_path("Town/Ground")

    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 2000), unreal.Rotator(0, -45, 135))
    sun.set_actor_label("Sun"); sun.set_folder_path("Town/Lighting")
    try:
        sun.light_component.set_editor_property("atmosphere_sun_light", True)
    except Exception:
        pass
    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 1500))
    sky.set_actor_label("SkyLight"); sky.set_folder_path("Town/Lighting")
    try:
        sky.light_component.set_editor_property("real_time_capture", True)
    except Exception:
        pass
    for cls, label in ((unreal.SkyAtmosphere, "SkyAtmosphere"), (unreal.ExponentialHeightFog, "Fog")):
        a = spawn(cls, unreal.Vector(0, 0, 0))
        a.set_actor_label(label); a.set_folder_path("Town/Lighting")
    start = spawn(unreal.PlayerStart, unreal.Vector(3600, 0, 120), unreal.Rotator(0, 0, 180))
    start.set_actor_label("PlayerStart_MainGate")

    meshes, counts = {}, collections.Counter()
    for i, p in enumerate(P):
        name = p["asset"]
        if name not in meshes:
            path = paths.get(name)
            meshes[name] = unreal.load_asset(path) if path and EAL.does_asset_exist(path) else None
            if meshes[name] is None:
                report["missing_assets"].append(name)
        mesh = meshes[name]
        if mesh is None:
            continue
        l, r, s = p["location_ue_cm"], p["rotation_ue_deg"], p["scale_ue"]
        a = spawn(unreal.StaticMeshActor, unreal.Vector(*l), unreal.Rotator(r["roll"], r["pitch"], r["yaw"]))
        a.static_mesh_component.set_static_mesh(mesh)
        a.set_actor_scale3d(unreal.Vector(*s))
        a.set_actor_label("{}_{:03d}".format(name[3:], i))
        a.set_folder_path("Town/" + p["group"])
        counts[name] += 1
    report["placed"] = sum(counts.values())
    report["counts"] = dict(counts)
    report["expected_counts"] = layout["counts"]
    report["counts_match"] = dict(counts) == layout["counts"]
    report["ground_half_extent_cm"] = ext
    les.save_current_level()
    log("placed {} of {} (counts match {}); missing {}".format(report["placed"], layout["total_instances"], report["counts_match"], report["missing_assets"]))
    with open(os.path.join(OUT, "town_v1_layout_report.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)


main()
