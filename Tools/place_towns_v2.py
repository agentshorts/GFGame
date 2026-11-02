"""Builds the towns_v2 maps from Carina's layout placements (run after Tools/import_towns_v2.py):

  L_Town_Cinderwright_A   layouts_v2/Town_Cinderwright_Layout_v2 (current Cinderwright layout), charcoal ground
  L_Town_Rime_A           layouts_v1/Town_Rime_Layout_v1, snow/ice ground
  L_Homestead_Verdant_A   layouts_v1/Town_Verdant_Homestead_Layout_v1, grass ground
  L_World_Factions_A      layouts_v2/World_Factions_Layout_v2 with its layout_refs flattened (Verdant A v2, Verdant
                          homestead v1, Rime v1, Cinderwright v2), grass / snow / charcoal ground split between towns

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/place_towns_v2.py" -unattended -nullrhi -nosplash

Env: GFTOWN_KIT (default <project>/handoff/inbox/towns_v2), GFTOWN_V1_KIT (default <kit>/../town_v1),
GFTOWN_MAPS (comma list of map names, default all). Each map is rebuilt from scratch: flat ground slab(s) with the top
at Z = 0 (the kits have no landscape), sun, sky light, atmosphere, fog, a player start at the +X entry, and one
StaticMeshActor per placement in outliner folders by group. Writes Saved/ArtImport/towns_v2_layout_report.json.
"""
import collections, json, math, os
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
KIT = os.environ.get("GFTOWN_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "towns_v2")
V1_KIT = os.environ.get("GFTOWN_V1_KIT", "").strip() or os.path.join(os.path.dirname(KIT), "town_v1")
OUT = os.path.join(PROJ, "Saved", "ArtImport")
EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MATS = {"grass": "/Game/Art/Blocks/Materials/MI_Block_Grass_Top", "snow": "/Game/Art/Blocks/Materials/MI_Block_Ice_Top",
        "charcoal": "/Game/Art/Blocks/Materials/MI_Block_Basalt_Top"}
LAYOUTS = {
    "Town_Verdant_A_Layout_v2": os.path.join(V1_KIT, "Town_Verdant_A_Layout_v2", "Town_Verdant_A_Layout_v2_placements.json"),
    "Town_Verdant_Homestead_Layout_v1": os.path.join(KIT, "layouts_v1", "Town_Verdant_Homestead_Layout_v1_placements.json"),
    "Town_Rime_Layout_v1": os.path.join(KIT, "layouts_v1", "Town_Rime_Layout_v1_placements.json"),
    "Town_Cinderwright_Layout_v2": os.path.join(KIT, "layouts_v2", "Town_Cinderwright_Layout_v2_placements.json"),
    "World_Factions_Layout_v2": os.path.join(KIT, "layouts_v2", "World_Factions_Layout_v2_placements.json"),
}
MAPS = [("L_Town_Cinderwright_A", "Town_Cinderwright_Layout_v2", "charcoal"),
        ("L_Town_Rime_A", "Town_Rime_Layout_v1", "snow"),
        ("L_Homestead_Verdant_A", "Town_Verdant_Homestead_Layout_v1", "grass"),
        ("L_World_Factions_A", "World_Factions_Layout_v2", None)]
report = {"maps": {}}


def log(m):
    unreal.log("[gftown] " + str(m))


def load(name):
    return json.load(open(LAYOUTS[name], encoding="utf-8"))


def flatten(layout):
    """Placements as (asset, group, loc, yaw, pitch, roll, scale, source); layout_refs are placed by their own transform."""
    out = [(p["asset"], p["group"], p["location_ue_cm"], p["rotation_ue_deg"]["yaw"], p["rotation_ue_deg"]["pitch"],
            p["rotation_ue_deg"]["roll"], p["scale_ue"], "world") for p in layout["placements"]]
    for ref in layout.get("layout_refs") or []:
        r = ref["rotation_ue_deg"]
        assert abs(r["pitch"]) < 1e-6 and abs(r["roll"]) < 1e-6, "layout ref with pitch/roll"
        yaw = r["yaw"]; c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw)); o = ref["location_ue_cm"]
        for (a, g, l, py, pp, pr, sc, _) in flatten(load(ref["layout_ref"])):
            loc = [o[0] + c * l[0] - s * l[1], o[1] + s * l[0] + c * l[1], o[2] + l[2]]
            out.append((a, ref["layout_ref"] + "/" + g, loc, py + yaw, pp, pr, sc, ref["layout_ref"]))
    return out


def spawn(cls, loc, rot=unreal.Rotator(0, 0, 0)):
    return eas.spawn_actor_from_class(cls, loc, rot)


def slab(label, x0, x1, y0, y1, mat):
    g = spawn(unreal.StaticMeshActor, unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, -10))
    c = g.static_mesh_component
    c.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
    g.set_actor_scale3d(unreal.Vector((x1 - x0) / 100.0, (y1 - y0) / 100.0, 0.2))
    if EAL.does_asset_exist(MATS[mat]):
        c.set_material(0, unreal.load_asset(MATS[mat]))
    g.set_actor_label(label); g.set_folder_path("Town/Ground")
    return [x0, x1, y0, y1, mat]


def bounds(P):
    xs = [p[2][0] for p in P]; ys = [p[2][1] for p in P]
    return min(xs), max(xs), min(ys), max(ys)


def build(map_name, layout_name, ground, paths):
    layout = load(layout_name)
    P = flatten(layout)
    path = "/Game/Maps/" + map_name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    if not les.new_level(path):
        raise RuntimeError("new_level failed " + path)
    rep = {"layout": layout_name, "missing_assets": [], "ground": []}
    x0, x1, y0, y1 = bounds(P)
    pad = 2000.0
    if ground:
        rep["ground"].append(slab("Town_Ground", x0 - pad, x1 + pad, y0 - pad, y1 + pad, ground))
    else:
        # World: split the ground between the towns (grass Verdant, snow toward Rime, charcoal toward Cinderwright).
        by = collections.defaultdict(list)
        for p in P:
            by[p[7]].append(p)
        cx = bounds(by["Town_Cinderwright_Layout_v2"])[0]
        vx1 = max(bounds(by[k])[1] for k in ("Town_Verdant_A_Layout_v2", "Town_Verdant_Homestead_Layout_v1", "Town_Rime_Layout_v1"))
        split_x = (cx + vx1) / 2 if cx > vx1 else cx
        ry = bounds(by["Town_Rime_Layout_v1"])[2]
        vy1 = max(bounds(by[k])[3] for k in ("Town_Verdant_A_Layout_v2", "Town_Verdant_Homestead_Layout_v1"))
        split_y = (ry + vy1) / 2 if ry > vy1 else ry
        X0, X1, Y0, Y1 = x0 - pad, x1 + pad, y0 - pad, y1 + pad
        rep["ground"].append(slab("Ground_Grass", X0, split_x, Y0, split_y, "grass"))
        rep["ground"].append(slab("Ground_Snow", X0, split_x, split_y, Y1, "snow"))
        rep["ground"].append(slab("Ground_Charcoal", split_x, X1, Y0, Y1, "charcoal"))
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
    # Player start on the +X entry road (every town's entry is on local +X; the world starts at Verdant's gate).
    if layout_name.startswith("World"):
        sx = 3600.0
    else:
        near = [p[2][0] for p in P if abs(p[2][1]) < 1500]
        sx = (max(near) if near else x1) + 600.0
    start = spawn(unreal.PlayerStart, unreal.Vector(sx, 0, 120), unreal.Rotator(0, 0, 180))
    start.set_actor_label("PlayerStart_Entry")
    rep["player_start"] = [sx, 0, 120]
    meshes, counts = {}, collections.Counter()
    for i, (name, group, l, yaw, pitch, roll, s, _) in enumerate(P):
        if name not in meshes:
            ap = paths.get(name)
            meshes[name] = unreal.load_asset(ap) if ap and EAL.does_asset_exist(ap) else None
            if meshes[name] is None:
                rep["missing_assets"].append(name)
        if meshes[name] is None:
            continue
        a = spawn(unreal.StaticMeshActor, unreal.Vector(*l), unreal.Rotator(roll, pitch, yaw))
        a.static_mesh_component.set_static_mesh(meshes[name])
        a.set_actor_scale3d(unreal.Vector(*s))
        a.set_actor_label("{}_{:04d}".format(name[3:], i))
        a.set_folder_path("Town/" + group)
        counts[name] += 1
    rep["placed"] = sum(counts.values())
    rep["expected"] = layout.get("flattened_instances") or layout["total_instances"]
    if not layout.get("layout_refs"):
        rep["counts_match"] = dict(counts) == layout["counts"]
    rep["placed_matches_expected"] = rep["placed"] == rep["expected"]
    rep["extent_cm"] = [x0, x1, y0, y1]
    les.save_current_level()
    report["maps"][map_name] = rep
    log("{}: placed {} of {}; missing {}".format(map_name, rep["placed"], rep["expected"], rep["missing_assets"]))


def main():
    paths = {}
    for f in ("town_v1_mineable.json", "towns_v2_mineable.json"):
        for m in json.load(open(os.path.join(PROJ, "Data", "Town", f), encoding="utf-8"))["meshes"]:
            paths[m["asset"]] = m["path"]
    want = [m.strip() for m in os.environ.get("GFTOWN_MAPS", "").split(",") if m.strip()]
    for map_name, layout_name, ground in MAPS:
        if want and map_name not in want:
            continue
        try:
            build(map_name, layout_name, ground, paths)
        except Exception as e:
            report["maps"][map_name] = {"error": str(e)}
            log("{} failed: {}".format(map_name, e))
    with open(os.path.join(OUT, "towns_v2_layout_report.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)


main()
