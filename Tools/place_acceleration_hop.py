"""Places the Acceleration Hop tiles (issue 334) on the Verdant to Cinderwright road in L_World_Factions_A.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/place_acceleration_hop.py" -unattended -nullrhi -nosplash

Run Tools/import_acceleration_hop.py first (the tiles need their meshes). Tools/place_towns_v2.py rebuilds the world map
from scratch and calls place_in_current_level itself (GFTOWN_ACCEL_HOP=0 skips that), so this standalone pass is only
needed on a map built before issue 334.

Env: GFTOWN_KIT (default <project>/handoff/inbox/towns_v2), GFHOP_MAP (default L_World_Factions_A).

- The plan comes from Tools/accel_hop_layout.py (pure Python, unit tested): two tiles per straight, straights only,
  Landing tiles at both ends, Downbeat on each bar line and a Boost pair before the next bar.
- Every tile is an AGFAccelHopTile (CustomDepth stencil 3, kind sets the mesh) in the folder AccelerationHop/Tiles,
  tagged GFAccelHop. One AGFAccelHopRoad covers the run: it plays the song, pulses the glow and adds the outline pass.
- Reruns are safe: actors tagged GFAccelHop are removed before placing.
Writes Saved/ArtImport/acceleration_hop_placement_report.json and logs "[gfhop]" lines.
"""
import json, os, sys, traceback
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import accel_hop_layout as layout  # noqa: E402

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
KIT = os.environ.get("GFTOWN_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "towns_v2")
MAP = os.environ.get("GFHOP_MAP", "").strip() or "L_World_Factions_A"
OUT = os.path.join(PROJ, "Saved", "ArtImport")
TAG = "GFAccelHop"
TILE_FOLDER = "AccelerationHop/Tiles"
ROAD_FOLDER = "AccelerationHop"
EAL = unreal.EditorAssetLibrary

report = {"map": MAP, "kit": KIT, "log": []}


def log(m):
    unreal.log("[gfhop] " + str(m))
    report["log"].append(str(m))


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _kind(name):
    return getattr(unreal.GFHopTileKind, name.upper())


def clear_hops():
    eas = _eas()
    old = [a for a in eas.get_all_level_actors() if unreal.Name(TAG) in list(a.tags)]
    if old:
        eas.destroy_actors(old)
    return len(old)


def place_in_current_level(kit=None):
    """Removes earlier hop actors and places the plan into the open level. Returns a report dict (no save)."""
    kit = kit or KIT
    missing = [k for k in layout.KINDS if not unreal.EditorAssetLibrary.does_asset_exist(layout.mesh_path(k).rsplit(".", 1)[0])]
    if missing:
        raise RuntimeError("hop tile meshes missing ({}); run Tools/import_acceleration_hop.py first".format(", ".join(missing)))
    plan = layout.plan_from_kit(kit)
    if not plan["tiles"]:
        raise RuntimeError("no road straights found between Verdant and Cinderwright")
    eas = _eas()
    rep = {k: v for k, v in plan.items() if k != "tiles"}
    rep["removed"] = clear_hops()
    placed, wrong = 0, []
    for t in plan["tiles"]:
        a = eas.spawn_actor_from_class(unreal.GFAccelHopTile, unreal.Vector(*t["location"]), unreal.Rotator(0.0, 0.0, t["yaw"]))
        a.set_kind(_kind(t["kind"]))
        a.tags = [unreal.Name(TAG), unreal.Name("GFAccelHop_" + t["kind"])]
        a.set_actor_label("HopTile_{:03d}_{}".format(t["slot"], t["kind"]))
        a.set_folder_path(TILE_FOLDER)
        mesh = a.get_editor_property("mesh").get_editor_property("static_mesh")
        if mesh is None or mesh.get_path_name() != layout.mesh_path(t["kind"]):
            wrong.append(t["slot"])
        placed += 1
    b = plan["bounds"]
    road = eas.spawn_actor_from_class(unreal.GFAccelHopRoad, unreal.Vector(*b["centre"]), unreal.Rotator(0.0, 0.0, b["yaw"]))
    road.set_road_extent(unreal.Vector(*b["half_extent"]))
    road.tags = [unreal.Name(TAG)]
    road.set_actor_label("AccelerationHop_Road")
    road.set_folder_path(ROAD_FOLDER)
    rep["placed"] = placed
    rep["tiles_without_mesh"] = wrong
    rep["ok"] = placed == len(plan["tiles"]) and not wrong
    log("placed {} hop tiles {} and one road box; mesh problems at slots {}".format(placed, plan["counts"], wrong or "none"))
    return rep


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    path = "/Game/Maps/" + MAP
    if not EAL.does_asset_exist(path):
        raise RuntimeError("no map at " + path + " (run Tools/place_towns_v2.py first)")
    if not les.load_level(path):
        raise RuntimeError("load_level failed " + path)
    report.update(place_in_current_level())
    les.save_current_level()


if os.environ.get("GFHOP_LIB") != "1":
    try:
        main()
    except Exception as e:
        report["ok"] = False
        log("FAIL " + str(e) + "\n" + traceback.format_exc())
    finally:
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, "acceleration_hop_placement_report.json"), "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
