"""Builds the Acceleration Hop tutorial map (issue 348): /Game/Maps/L_AccelHop_Tutorial_v1, a copy of L_Beat.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/create_accel_hop_tutorial_map.py" -unattended -nullrhi -nosplash

Run Tools/import_acceleration_hop.py first (the tiles need their meshes). L_Beat is never opened for edit or saved:
the script duplicates it once (asset duplicate, no byte edits), then only loads and saves the copy. It hashes
Content/Maps/L_Beat.umap before and after and fails the report if the file changed.

The course comes from Tools/accel_hop_tutorial_layout.py (pure Python, unit tested):
- one continuous floor slab (/Engine/BasicShapes/Cube, top at Z 0), a PlayerStart facing +X
- 56 AGFAccelHopTile in four lessons along +X, tagged GFAccelHopTutorial and GFAccelHopTutorial.Lesson.<Id>;
  lessons 1 and 2 have can_be_lit off
- one AGFAccelHopRoad box over every tile (one song, one beat clock)
- one AGFAccelHopTutorial director with the finish HUB gate location set (the hub director adds the gates at play)
- a directional light, sky light and sky atmosphere if the copy has none (placeholders for the visual pass)
Any GFBeatHopCourse left in the copy is removed. Reruns are safe: actors tagged GFAccelHopTutorial are removed first.

Env: GFTUT_MAP (default /Game/Maps/L_AccelHop_Tutorial_v1), GFTUT_SOURCE (default /Game/Maps/L_Beat).
Writes Saved/ArtImport/accel_hop_tutorial_map_report.json and logs "[gftut]" lines.
"""
import hashlib
import json
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import accel_hop_tutorial_layout as layout  # noqa: E402

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.environ.get("GFTUT_SOURCE", "").strip() or "/Game/Maps/L_Beat"
MAP = os.environ.get("GFTUT_MAP", "").strip() or "/Game/Maps/L_AccelHop_Tutorial_v1"
OUT = os.path.join(PROJ, "Saved", "ArtImport")
TAG = layout.TAG
FOLDER = "AccelHopTutorial"
CUBE = "/Engine/BasicShapes/Cube.Cube"
EAL = unreal.EditorAssetLibrary

report = {"map": MAP, "source": SOURCE, "log": []}


def log(m):
    unreal.log("[gftut] " + str(m))
    report["log"].append(str(m))


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _kind(name):
    return getattr(unreal.GFHopTileKind, name.upper())


def package_file(package):
    """Content/<path>.umap for a /Game/<path> package."""
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
    """Refuses to write over the source map (or any map but a versioned tutorial copy)."""
    if target == source or target.rstrip("/").endswith("/L_Beat"):
        raise RuntimeError("refusing to write " + target + ": the tutorial is a new versioned map, L_Beat stays as is")
    name = target.rsplit("/", 1)[-1]
    if not name.startswith("L_AccelHop_Tutorial_v"):
        raise RuntimeError("target " + target + " is not a versioned tutorial map (L_AccelHop_Tutorial_v<N>)")


def ensure_copy(source, target):
    """Duplicates source to target once. Returns 'created' or 'exists'."""
    check_target(source, target)
    if EAL.does_asset_exist(target):
        return "exists"
    if not EAL.does_asset_exist(source):
        raise RuntimeError("no source map at " + source + " (git lfs pull, or run Tools/create_maps.py)")
    if EAL.duplicate_asset(source, target) is not None:
        return "created"
    # Fallback: open the source and save the open world under the new name. The source package is never saved; the
    # hash check in main() proves its file did not change.
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(source):
        raise RuntimeError("duplicate_asset failed and load_level failed " + source)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, target):
        raise RuntimeError("could not copy " + source + " -> " + target)
    return "created (save_map)"


def _tagged(actor, tag):
    return unreal.Name(tag) in list(actor.tags)


def clear_tutorial():
    eas = _eas()
    old = [a for a in eas.get_all_level_actors() if _tagged(a, TAG)]
    courses = [a for a in eas.get_all_level_actors() if a not in old and "GFBeatHopCourse" in a.get_class().get_name()]
    if old or courses:
        eas.destroy_actors(old + courses)
    return len(old), len(courses)


def _tag(actor, extra=()):
    actor.tags = [unreal.Name(TAG)] + [unreal.Name(t) for t in extra]


def _ensure_light_rig(eas, rep):
    """Placeholder light so the greybox is readable; the visual pass replaces it."""
    have = {a.get_class().get_name() for a in eas.get_all_level_actors()}
    added = []
    for cls_name, rot in (("DirectionalLight", unreal.Rotator(0.0, -50.0, 30.0)), ("SkyLight", unreal.Rotator(0.0, 0.0, 0.0)),
                          ("SkyAtmosphere", unreal.Rotator(0.0, 0.0, 0.0))):
        if cls_name in have:
            continue
        cls = getattr(unreal, cls_name, None)
        if cls is None:
            continue
        a = eas.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 600.0), rot)
        _tag(a, ["GFAccelHopTutorial.Light"])
        a.set_actor_label("Tutorial_" + cls_name)
        a.set_folder_path(FOLDER + "/Light")
        added.append(cls_name)
    rep["lights_added"] = added


def place_in_current_level(plan=None):
    """Clears earlier tutorial actors and places the plan into the open level. Returns a report dict (no save)."""
    plan = plan or layout.plan()
    missing = [k for k in layout.KINDS if not EAL.does_asset_exist(layout.mesh_path(k).rsplit(".", 1)[0])]
    if missing:
        raise RuntimeError("hop tile meshes missing ({}); run Tools/import_acceleration_hop.py first".format(", ".join(missing)))
    eas = _eas()
    rep = {"lessons": plan["lessons"], "counts": plan["counts"], "road": plan["road"], "finish_gate": plan["finish_gate"]}
    rep["removed"], rep["removed_courses"] = clear_tutorial()

    floor = plan["floor"]
    slab = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*floor["centre"]), unreal.Rotator(0.0, 0.0, 0.0))
    slab.static_mesh_component.set_static_mesh(EAL.load_asset(CUBE))
    slab.set_actor_scale3d(unreal.Vector(*floor["scale"]))
    _tag(slab, ["GFAccelHopTutorial.Floor"])
    slab.set_actor_label("Tutorial_Floor")
    slab.set_folder_path(FOLDER + "/Floor")

    starts = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == "PlayerStart"]
    if starts:
        starts[0].set_actor_location(unreal.Vector(*plan["player_start"]), False, False)
        starts[0].set_actor_rotation(unreal.Rotator(0.0, 0.0, 0.0), False)
        rep["player_start"] = "moved"
    else:
        ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(*plan["player_start"]), unreal.Rotator(0.0, 0.0, 0.0))
        # Not tagged: reruns keep and move it like a PlayerStart that came with the copy.
        ps.set_folder_path(FOLDER)
        rep["player_start"] = "added"

    placed, wrong = 0, []
    for t in plan["tiles"]:
        a = eas.spawn_actor_from_class(unreal.GFAccelHopTile, unreal.Vector(*t["location"]), unreal.Rotator(0.0, 0.0, t["yaw"]))
        a.set_kind(_kind(t["kind"]))
        a.set_can_be_lit(bool(t["can_be_lit"]))
        a.tags = [unreal.Name(n) for n in t["tags"]]
        a.set_actor_label(t["label"])
        a.set_folder_path(FOLDER + "/Tiles/" + t["lesson"])
        mesh = a.get_editor_property("mesh").get_editor_property("static_mesh")
        if mesh is None or mesh.get_path_name() != t["mesh"]:
            wrong.append(t["slot"])
        placed += 1

    b = plan["road"]
    road = eas.spawn_actor_from_class(unreal.GFAccelHopRoad, unreal.Vector(*b["centre"]), unreal.Rotator(0.0, 0.0, b["yaw"]))
    road.set_road_extent(unreal.Vector(*b["half_extent"]))
    _tag(road)
    road.set_actor_label("AccelHopTutorial_Road")
    road.set_folder_path(FOLDER)

    director = eas.spawn_actor_from_class(unreal.GFAccelHopTutorial, unreal.Vector(*plan["director"]), unreal.Rotator(0.0, 0.0, 0.0))
    director.set_editor_property("has_finish_gate", True)
    director.set_editor_property("finish_gate_location", unreal.Vector(*plan["finish_gate"]))
    _tag(director)
    director.set_actor_label("AccelHopTutorial_Director")
    director.set_folder_path(FOLDER)

    _ensure_light_rig(eas, rep)
    rep["placed"] = placed
    rep["tiles_without_mesh"] = wrong
    rep["ok"] = placed == len(plan["tiles"]) and not wrong
    log("placed {} tutorial tiles {}, one road box, the director and the floor; mesh problems at slots {}".format(
        placed, plan["counts"], wrong or "none"))
    return rep


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    check_target(SOURCE, MAP)
    beat_file = package_file(SOURCE)
    before = file_hash(beat_file)
    report["source_hash_before"] = before
    report["copy"] = ensure_copy(SOURCE, MAP)
    log("map copy " + report["copy"])
    if not les.load_level(MAP):
        raise RuntimeError("load_level failed " + MAP)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or not world.get_path_name().startswith(MAP):
        raise RuntimeError("open level is not " + MAP + "; not placing anything")
    report.update(place_in_current_level())
    if not les.save_current_level():
        raise RuntimeError("save_current_level failed")
    after = file_hash(beat_file)
    report["source_hash_after"] = after
    report["source_untouched"] = before == after
    if before != after:
        report["ok"] = False
        raise RuntimeError(SOURCE + " changed on disk; restore it from git (git checkout -- Content/Maps/L_Beat.umap)")
    log("{} untouched ({})".format(SOURCE, after))


if os.environ.get("GFTUT_LIB") != "1":
    try:
        main()
    except Exception as e:
        report["ok"] = False
        log("FAIL " + str(e) + "\n" + traceback.format_exc())
    finally:
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, "accel_hop_tutorial_map_report.json"), "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
