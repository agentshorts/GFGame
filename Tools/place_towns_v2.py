"""Builds the towns_v2 maps from the kit layout placements (run after Tools/import_towns_v2.py):

  L_Town_Cinderwright_A   crystal_lining_layout_v2/Town_Cinderwright_Layout_v4, charcoal ground slabs
  L_Town_Rime_A           crystal_lining_layout_v2/Town_Rime_Layout_v2, snow/ice ground
  L_Homestead_Verdant_A   layouts_v1/Town_Verdant_Homestead_Layout_v1, grass ground
  L_World_Factions_A      crystal_lining_layout_v2/World_Factions_Layout_v4 with its layout_refs flattened (Verdant A v3, Verdant
                          homestead v1, Rime v2, Cinderwright v4), grass / snow / charcoal ground split between towns

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/place_towns_v2.py" -unattended -nullrhi -nosplash

Env: GFTOWN_KIT (default <project>/handoff/inbox/towns_v2), GFTOWN_V1_KIT (default <kit>/../town_v1),
GFTOWN_MAPS (comma list of map names, default all), GFTOWN_LIGHTS (FIXES_V1: "director" (default) places no sun, sky light,
atmosphere or fog so the game environment director lights the map exactly like the hub (fixed manual exposure, fog 0.005);
"placed" keeps the editor-placed lights and adds the same manual exposure volume and fog the director would use).
Each map is rebuilt from scratch: flat ground with the top at Z = 0 (the kits have no landscape; Cinderwright charcoal is
tiled with the 64 m GroundSlab mesh), lights only with GFTOWN_LIGHTS=placed, a player start at the +X entry, and one
StaticMeshActor per placement in outliner folders by group. Writes Saved/ArtImport/towns_v2_layout_report.json.
The world (issue 335) starts at the rest area on the Verdant to Cinderwright road (Tools/rest_area.py; the layout file is
required, env GFTOWN_REST_AREA, default <kit>/../rest_area_v1/Rest_Area_Layout_v1_world_placements.json):
PlayerStart_RestArea tagged RestArea at the layout's start, the layout's kit meshes (the midpoint road Straight becomes
a Road_T with a spur), the caravan pen and carts (the Verdant caravan yard is left out), NPC spot and structure
placeholder TargetPoints, the GatherZone_RestArea box, the campfire and notice board meshes (Tools/import_rest_area_structures.py)
and a campfire point light. It also gets the basket ferry stations just outside the Verdant and Rime walls
(TargetPoints tagged GFFerryStop + stop id that AGFFerryDirector flies between) and the issue 334 Acceleration Hop
tiles and road box (Tools/place_acceleration_hop.py; needs Tools/import_acceleration_hop.py first; GFTOWN_ACCEL_HOP=0
leaves them out).
"""
import collections, json, math, os, sys
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
KIT = os.environ.get("GFTOWN_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "towns_v2")
V1_KIT = os.environ.get("GFTOWN_V1_KIT", "").strip() or os.path.join(os.path.dirname(KIT), "town_v1")
OUT = os.path.join(PROJ, "Saved", "ArtImport")
sys.path.insert(0, os.path.join(PROJ, "Tools"))
import town_clearing  # FERRY: station spots and tree footprints (pure Python, runs without the editor)
import rest_area  # ISSUE 335: the rest area start and caravan (pure Python)
REST = rest_area.path_next_to_kit(KIT)
VERDANT = rest_area.VERDANT_LAYOUT
EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MATS = {"grass": "/Game/Art/Blocks/Materials/MI_Block_Grass_Top", "snow": "/Game/Art/Blocks/Materials/MI_Block_Ice_Top",
        "charcoal": "/Game/Art/Blocks/Materials/MI_Block_Basalt_Top"}
LAYOUTS = {
    "Town_Verdant_A_Layout_v2": os.path.join(V1_KIT, "Town_Verdant_A_Layout_v2", "Town_Verdant_A_Layout_v2_placements.json"),
    "Town_Verdant_A_Layout_v3": os.path.join(os.path.dirname(KIT), "crystal_lining_layout_v2", "Town_Verdant_A_Layout_v3_placements.json"),
    "Town_Verdant_Homestead_Layout_v1": os.path.join(KIT, "layouts_v1", "Town_Verdant_Homestead_Layout_v1_placements.json"),
    "Town_Rime_Layout_v1": os.path.join(KIT, "layouts_v1", "Town_Rime_Layout_v1_placements.json"),
    "Town_Rime_Layout_v2": os.path.join(os.path.dirname(KIT), "crystal_lining_layout_v2", "Town_Rime_Layout_v2_placements.json"),
    "Town_Cinderwright_Layout_v2": os.path.join(KIT, "layouts_v2", "Town_Cinderwright_Layout_v2_placements.json"),
    "Town_Cinderwright_Layout_v3": os.path.join(KIT, "fixes_v1", "layouts", "Town_Cinderwright_Layout_v3_placements.json"),  # FIXES_V1
    "Town_Cinderwright_Layout_v4": os.path.join(os.path.dirname(KIT), "crystal_lining_layout_v2", "Town_Cinderwright_Layout_v4_placements.json"),
    "World_Factions_Layout_v3": os.path.join(KIT, "fixes_v1", "layouts", "World_Factions_Layout_v3_placements.json"),  # FIXES_V1
    "World_Factions_Layout_v2": os.path.join(KIT, "layouts_v2", "World_Factions_Layout_v2_placements.json"),
    "World_Factions_Layout_v4": os.path.join(os.path.dirname(KIT), "crystal_lining_layout_v2", "World_Factions_Layout_v4_placements.json"),
}
CW = "Town_Cinderwright_Layout_v4"
GROUND_SLAB = "SM_Town_Cinderwright_GroundSlab"  # FIXES_V1: 64 m charcoal slab, pivot at the bottom centre, 6.25 cm thick
SLAB_CM, SLAB_T = 6400.0, 6.25
LIGHTS = (os.environ.get("GFTOWN_LIGHTS", "").strip() or "director").lower()
MAPS = [("L_Town_Cinderwright_A", CW, "charcoal"),
        ("L_Town_Rime_A", "Town_Rime_Layout_v2", "snow"),
        ("L_Homestead_Verdant_A", "Town_Verdant_Homestead_Layout_v1", "grass"),
        ("L_World_Factions_A", "World_Factions_Layout_v4", None)]
report = {"maps": {}}


def log(m):
    unreal.log("[gftown] " + str(m))


def load(name, world=False):
    """A layout by name. For the world build (world=True) the Verdant town loses its caravan yard (ISSUE 335: the
    caravan lives at the rest area now)."""
    layout = json.load(open(LAYOUTS[name], encoding="utf-8"))
    return rest_area.without_yard(layout) if world and name == VERDANT else layout


def load_rest():
    rest = rest_area.load(REST)
    if rest is None:
        raise RuntimeError("rest area layout not found: " + REST + " (copy the rest_area_v1 handoff into handoff/inbox "
                           "or set GFTOWN_REST_AREA)")
    return rest


def flatten(layout, world=False):
    """Placements as (asset, group, loc, yaw, pitch, roll, scale, source); layout_refs are placed by their own transform."""
    out = [(p["asset"], p["group"], p["location_ue_cm"], p["rotation_ue_deg"]["yaw"], p["rotation_ue_deg"]["pitch"],
            p["rotation_ue_deg"]["roll"], p["scale_ue"], "world") for p in layout["placements"]]
    for ref in layout.get("layout_refs") or []:
        r = ref["rotation_ue_deg"]
        assert abs(r["pitch"]) < 1e-6 and abs(r["roll"]) < 1e-6, "layout ref with pitch/roll"
        yaw = r["yaw"]; c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw)); o = ref["location_ue_cm"]
        for (a, g, l, py, pp, pr, sc, _) in flatten(load(ref["layout_ref"], world), world):
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


def tile_ground(label, x0, x1, y0, y1, mat, paths, anchor_x0=False):
    """FIXES_V1: charcoal ground is tiled with the GroundSlab mesh (top at z 0, its own charcoal material) instead of one
    engine cube stretched with the basalt block material. Other grounds, or a missing slab asset, fall back to slab()."""
    ap = paths.get(GROUND_SLAB)
    if mat != "charcoal" or not ap or not EAL.does_asset_exist(ap):
        return slab(label, x0, x1, y0, y1, mat)
    mesh = unreal.load_asset(ap)
    nx, ny = int(math.ceil((x1 - x0) / SLAB_CM)), int(math.ceil((y1 - y0) / SLAB_CM))
    # centred on the rect (town map); anchored at x0 for the world so the slabs never run under the grass / snow cubes
    sx = x0 + SLAB_CM / 2 if anchor_x0 else (x0 + x1) / 2 - (nx - 1) * SLAB_CM / 2
    sy = (y0 + y1) / 2 - (ny - 1) * SLAB_CM / 2
    sx, sy = round(sx / 6.25) * 6.25, round(sy / 6.25) * 6.25  # on the 6.25 cm grid (matches Town_Cinderwright_GroundSlab_v1_placements.json)
    for i in range(nx):
        for j in range(ny):
            g = spawn(unreal.StaticMeshActor, unreal.Vector(sx + i * SLAB_CM, sy + j * SLAB_CM, -SLAB_T))
            g.static_mesh_component.set_static_mesh(mesh)
            g.set_actor_label("{}_{:02d}_{:02d}".format(label, i, j)); g.set_folder_path("Town/Ground")
    return [x0, x1, y0, y1, mat, "slabs {}x{}".format(nx, ny)]


def lights():
    """FIXES_V1. director: place nothing; the environment director adds sun, atmosphere, sky light, fog 0.005 and the
    fixed manual exposure at runtime (it only adds the exposure when it owns the sun, which is why placed suns washed
    the ground out). placed: the original editor lights plus the director's exposure volume and fog values."""
    if LIGHTS == "director":
        return "director"
    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 2000), unreal.Rotator(0, -45, 135))
    sun.set_actor_label("Sun"); sun.set_folder_path("Town/Lighting")
    try:
        sun.light_component.set_editor_property("atmosphere_sun_light", True)
        sun.light_component.set_editor_property("intensity", 10.0)
    except Exception:
        pass
    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 1500))
    sky.set_actor_label("SkyLight"); sky.set_folder_path("Town/Lighting")
    try:
        sky.light_component.set_editor_property("real_time_capture", True)
    except Exception:
        pass
    a = spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0)); a.set_actor_label("SkyAtmosphere"); a.set_folder_path("Town/Lighting")
    fog = spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0)); fog.set_actor_label("Fog"); fog.set_folder_path("Town/Lighting")
    try:
        fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        fc.set_editor_property("fog_density", 0.005); fc.set_editor_property("enable_volumetric_fog", False)
    except Exception as e:
        log("fog settings: {}".format(e))
    ppv = spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0)); ppv.set_actor_label("Exposure_Manual"); ppv.set_folder_path("Town/Lighting")
    try:
        ppv.set_editor_property("unbound", True); ppv.set_editor_property("priority", 1.0); ppv.set_editor_property("blend_weight", 1.0)
        s = ppv.get_editor_property("settings")
        for k, v in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_bias", 0.0),
                     ("auto_exposure_apply_physical_camera_exposure", False),
                     ("auto_exposure_min_brightness", 1.0), ("auto_exposure_max_brightness", 1.0)):
            s.set_editor_property("override_" + k, True); s.set_editor_property(k, v)
        ppv.set_editor_property("settings", s)
    except Exception as e:
        log("exposure volume settings: {}".format(e))
    return "placed"


def bounds(P):
    xs = [p[2][0] for p in P]; ys = [p[2][1] for p in P]
    return min(xs), max(xs), min(ys), max(ys)


def rest_markers(rest, paths, rep):
    """ISSUE 335: the rest area start, NPC spots, gather zone, structures (mesh or placeholder marker) and campfire light."""
    st = rest_area.player_start(rest)
    start = spawn(unreal.PlayerStart, unreal.Vector(*st["location"]), unreal.Rotator(0, 0, st["yaw"]))
    start.set_editor_property("player_start_tag", unreal.Name(st["tag"]))
    start.set_actor_label("PlayerStart_RestArea")
    start.set_folder_path("Town/" + rest_area.GROUP_PREFIX + "player_start")
    rep["player_start"] = st["location"]
    rep["player_start_yaw"] = st["yaw"]
    rep["player_start_tag"] = st["tag"]
    for n in rest_area.npc_spots(rest):
        m = spawn(unreal.TargetPoint, unreal.Vector(*n["location"]), unreal.Rotator(0, 0, n["yaw"]))
        m.tags = [unreal.Name(rest_area.NPC_SPOT_TAG), unreal.Name(n["id"])]
        m.set_actor_label(n["id"]); m.set_folder_path("Town/" + rest_area.GROUP_PREFIX + "npc_spots")
    z = rest_area.gather_zone(rest)
    box = spawn(unreal.TriggerBox, unreal.Vector(*z["center"]))
    try:
        box.collision_component.set_editor_property("box_extent", unreal.Vector(*z["extent"]))
    except Exception as e:
        log("gather zone extent: {}".format(e))
    box.tags = [unreal.Name(rest_area.GATHER_ZONE_TAG)]
    box.set_actor_label(z["id"]); box.set_folder_path("Town/" + rest_area.GROUP_PREFIX + "gather")
    structs = {}
    for i, s in enumerate(rest_area.structures(rest)):
        ap = paths.get(s["asset"]) if s["has_mesh"] else None
        mesh = unreal.load_asset(ap) if ap and EAL.does_asset_exist(ap) else None
        if mesh is not None:
            a = spawn(unreal.StaticMeshActor, unreal.Vector(*s["location"]), unreal.Rotator(0, 0, s["yaw"]))
            a.static_mesh_component.set_static_mesh(mesh)
            a.set_actor_scale3d(unreal.Vector(*s["scale"]))
            a.set_actor_label("{}_R{:02d}".format(s["asset"][3:], i))
            structs[s["asset"] + "#" + str(i)] = "mesh"
        else:
            a = spawn(unreal.TargetPoint, unreal.Vector(*s["location"]), unreal.Rotator(0, 0, s["yaw"]))
            a.tags = [unreal.Name(rest_area.PLACEHOLDER_TAG), unreal.Name(s["asset"])]
            a.set_actor_label("Placeholder_{}_R{:02d}".format(s["asset"][3:], i))
            structs[s["asset"] + "#" + str(i)] = "placeholder" + ("" if not s["has_mesh"] else " (mesh not imported)")
        a.set_folder_path("Town/" + rest_area.GROUP_PREFIX + "structures")
    lights_ = []
    for i, l in enumerate(rest_area.campfire_light(rest)):
        pl = spawn(unreal.PointLight, unreal.Vector(*l["location"]))
        try:
            c = pl.point_light_component
            c.set_editor_property("intensity", l["intensity_cd"])
            c.set_editor_property("attenuation_radius", l["radius_cm"])
            c.set_editor_property("light_color", unreal.Color(r=l["colour"][0], g=l["colour"][1], b=l["colour"][2], a=255))
        except Exception as e:
            log("campfire light settings: {}".format(e))
        pl.tags = [unreal.Name(rest_area.CAMPFIRE_LIGHT_TAG)]
        pl.set_actor_label("CampfireLight_{:02d}".format(i)); pl.set_folder_path("Town/" + rest_area.GROUP_PREFIX + "structures")
        lights_.append(l["location"])
    rep["rest_area"] = {"file": os.path.basename(REST), "npc_spots": len(rest_area.npc_spots(rest)), "gather_zone": z,
                        "structures": structs, "campfire_lights": lights_}


def build(map_name, layout_name, ground, paths):
    world = layout_name.startswith("World")
    layout = load(layout_name)
    rest = None
    if world:
        rest = load_rest()
        layout = rest_area.apply(layout, rest)
    P = flatten(layout, world)
    path = "/Game/Maps/" + map_name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    if EAL.does_asset_exist(path):
        # A rerun: the editor keeps the old world loaded and refuses the delete, so clear it in place instead.
        if not les.load_level(path):
            raise RuntimeError("load_level failed " + path)
        old = [a for a in eas.get_all_level_actors() if not isinstance(a, (unreal.WorldSettings, unreal.Brush))]
        eas.destroy_actors(old)
        log("{}: cleared {} actors from the existing map".format(map_name, len(old)))
    elif not les.new_level(path):
        raise RuntimeError("new_level failed " + path)
    rep = {"layout": layout_name, "missing_assets": [], "ground": []}
    x0, x1, y0, y1 = bounds(P)
    pad = 2000.0
    if ground:
        rep["ground"].append(tile_ground("Town_Ground", x0 - pad, x1 + pad, y0 - pad, y1 + pad, ground, paths))  # FIXES_V1
    else:
        # World: split the ground between the towns (grass Verdant, snow toward Rime, charcoal toward Cinderwright).
        by = collections.defaultdict(list)
        for p in P:
            by[p[7]].append(p)
        cx = bounds(by[CW])[0]  # FIXES_V1
        vx1 = max(bounds(by[k])[1] for k in ("Town_Verdant_A_Layout_v3", "Town_Verdant_Homestead_Layout_v1", "Town_Rime_Layout_v2"))
        split_x = (cx + vx1) / 2 if cx > vx1 else cx
        ry = bounds(by["Town_Rime_Layout_v2"])[2]
        vy1 = max(bounds(by[k])[3] for k in ("Town_Verdant_A_Layout_v3", "Town_Verdant_Homestead_Layout_v1"))
        split_y = (ry + vy1) / 2 if ry > vy1 else ry
        X0, X1, Y0, Y1 = x0 - pad, x1 + pad, y0 - pad, y1 + pad
        rep["ground"].append(slab("Ground_Grass", X0, split_x, Y0, split_y, "grass"))
        rep["ground"].append(slab("Ground_Snow", X0, split_x, split_y, Y1, "snow"))
        rep["ground"].append(tile_ground("Ground_Charcoal", split_x, X1, Y0, Y1, "charcoal", paths, anchor_x0=True))  # FIXES_V1
    rep["lights"] = lights()  # FIXES_V1 (was: sun, sky light, atmosphere and fog always placed)
    radii, cut, stations = {}, 0, {}
    if world:
        # ISSUE 335: the world (the game's default map) starts at the rest area on the Verdant to Cinderwright road,
        # where the caravan now lives. AGFGameMode prefers the RestArea-tagged start until the player has a home
        # (FGFRestAreaSpawn); the hub gate director puts the way home behind the start (AGFReturnGate::AreaLocation).
        radii = town_clearing.footprints(KIT, V1_KIT)
        rest_markers(rest, paths, rep)
        # FERRY: the basket ferry runs between Rime and Verdant outside both towns. AGFFerryDirector swaps its config
        # stops for these markers (FGFFerryPlacedStops) and keeps only the Rime-Verdant route.
        stations = town_clearing.ferry_stations(town_clearing.flatten(LAYOUTS, layout_name))
        for stop, loc in sorted(stations.items()):
            marker = spawn(unreal.TargetPoint, unreal.Vector(*loc))
            marker.tags = [unreal.Name("GFFerryStop"), unreal.Name(stop)]
            marker.set_actor_label("FerryStop_" + stop)
            marker.set_folder_path("Town/Ferry")
        rep["ferry_stations"] = stations
    else:
        # Player start on the +X entry road (every town's entry is on local +X).
        near = [p[2][0] for p in P if abs(p[2][1]) < 1500]
        sx = (max(near) if near else x1) + 600.0
        start = spawn(unreal.PlayerStart, unreal.Vector(sx, 0, 120), unreal.Rotator(0, 0, 180))
        start.set_actor_label("PlayerStart_Entry")
        rep["player_start"] = [sx, 0, 120]
    meshes, counts = {}, collections.Counter()
    for i, (name, group, l, yaw, pitch, roll, s, _) in enumerate(P):
        if town_clearing.ferry_cuts(name, l, s, radii, stations):
            cut += 1
            continue
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
    if world:
        yard = rest_area.caravan_yard(load(VERDANT)["placements"])
        rep["expected"] -= len(yard)
        rep["rest_area"]["world_ops"] = layout["rest_area"]
        rep["rest_area"]["verdant_caravan_yard_removed"] = len(yard)
    if not layout.get("layout_refs"):
        rep["counts_match"] = dict(counts) == layout["counts"]
    rep["cleared_trees"] = cut
    rep["placed_matches_expected"] = rep["placed"] + cut == rep["expected"]
    rep["extent_cm"] = [x0, x1, y0, y1]
    if world and os.environ.get("GFTOWN_ACCEL_HOP", "1") != "0":
        # ISSUE 334: Acceleration Hop tiles on the Verdant to Cinderwright road (Tools/place_acceleration_hop.py),
        # so a rebuilt world keeps them. A missing tile import is logged and the town build carries on.
        rep["accel_hop"] = accel_hop(map_name)
    les.save_current_level()
    report["maps"][map_name] = rep
    log("{}: placed {} of {}; missing {}".format(map_name, rep["placed"], rep["expected"], rep["missing_assets"]))


def accel_hop(map_name):
    os.environ["GFHOP_LIB"] = "1"
    try:
        import place_acceleration_hop
        return place_acceleration_hop.place_in_current_level(KIT, REST)
    except Exception as e:
        log("{}: acceleration hop tiles skipped: {}".format(map_name, e))
        return {"error": str(e)}
    finally:
        os.environ.pop("GFHOP_LIB", None)


def main():
    paths = {}
    for f in ("town_v1_mineable.json", "towns_v2_mineable.json", "rest_area_structures.json"):
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
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "towns_v2_layout_report.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)


main()
