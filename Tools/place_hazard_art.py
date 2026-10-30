"""Places the hazard set pieces in L_Hub at their manifest placement (run headless, after import_art_batch.py):
  UnrealEditor-Cmd.exe GFGame.uproject -ExecutePythonScript="<abs>/Tools/place_hazard_art.py" -unattended -nullrhi -nosplash
Locations match the +Hazards boxes in Config/DefaultGame.ini (LavaBridge 3700,0 and DeadlyForest -2300,0).
Idempotent: actors are found by label and moved, never duplicated. Art only: the lava kill boxes in the
manifest are not placed, because the game has no hazard damage yet (GetHazardAt only reports the box).
Writes Saved/ArtImport/place_report.json.
"""
import json, os
import unreal

MAP = "/Game/Maps/L_Hub"
PIECES = [
    ("Hazard_LavaBridge", "LavaBridge", "/Game/Art/Props/Hazards/SM_Hazard_LavaBridge", unreal.Vector(3700.0, 0.0, 0.0)),
    ("Hazard_DeadlyForest", "DeadlyForest", "/Game/Art/Props/Hazards/SM_Hazard_DeadlyForest", unreal.Vector(-2300.0, 0.0, 0.0)),
]
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)
report = {"map": MAP, "actors": {}}

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report["loaded"] = bool(les.load_level(MAP))
existing = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
for label, hazard_id, mesh_path, loc in PIECES:
    r = {"mesh": mesh_path}
    mesh = unreal.load_asset(mesh_path)
    if mesh is None:
        r["error"] = "mesh missing"
        report["actors"][label] = r
        continue
    a = existing.get(label)
    if a is None:
        a = eas.spawn_actor_from_object(mesh, loc, unreal.Rotator(0.0, 0.0, 0.0))
        a.set_actor_label(label)
        r["spawned"] = True
    else:
        a.static_mesh_component.set_static_mesh(mesh)
        a.set_actor_location(loc, False, False)
        a.set_actor_rotation(unreal.Rotator(0.0, 0.0, 0.0), False)
        r["spawned"] = False
    a.set_folder_path("Hazards")
    a.tags = [unreal.Name("GFHazard"), unreal.Name(hazard_id)]
    l = a.get_actor_location()
    r["location"] = [l.x, l.y, l.z]
    r["pass"] = (l - loc).length() < 0.01 and a.static_mesh_component.static_mesh == mesh
    report["actors"][label] = r
report["saved"] = bool(les.save_current_level())
report["pass"] = report["loaded"] and report["saved"] and all(v.get("pass") for v in report["actors"].values())
with open(os.path.join(OUT, "place_report.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=1)
unreal.log("GFPlace " + json.dumps(report))
unreal.SystemLibrary.quit_editor()
