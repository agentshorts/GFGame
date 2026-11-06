"""Live readback for crystal lining layout in L_World_Factions_A (#384).

Counts Rime lined vs unlined wall static mesh actors, checks materials load, and
probes mineable mode on one lined and one unlined wall mesh.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/readback_crystal_lining_v1.py" -unattended -nullrhi -nosplash
"""
import json, os
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)
MAP = "/Game/Maps/L_World_Factions_A"
LINED = {
    "SM_Town_Rime_Wall_Straight",
    "SM_Town_Rime_Wall_Corner",
    "SM_Town_Rime_Wall_Gate",
}
UNLINED = "SM_Town_Rime_Wall_Straight_Unlined"
report = {"map": MAP, "counts": {}, "materials": {}, "mineable": {}, "ok": False}


def mesh_name(actor):
    try:
        for c in actor.get_components_by_class(unreal.StaticMeshComponent):
            m = c.static_mesh
            if m:
                return m.get_name()
    except Exception:
        return None
    return None


def mats_ok(path):
    m = unreal.load_asset(path)
    if not m:
        return False, "missing mesh"
    slots = []
    for s in m.static_materials:
        mi = s.material_interface
        slots.append({"slot": str(s.material_slot_name), "mi": mi.get_name() if mi else None})
    return all(s["mi"] for s in slots), slots


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not les.load_level(MAP):
    report["error"] = "load_level failed"
else:
    lined = 0
    unlined = 0
    other = {}
    for a in eas.get_all_level_actors():
        n = mesh_name(a)
        if not n:
            continue
        if n in LINED:
            lined += 1
        elif n == UNLINED:
            unlined += 1
        elif "Rime_Wall" in n:
            other[n] = other.get(n, 0) + 1
    report["counts"] = {"lined": lined, "unlined": unlined, "other_rime_wall": other, "expect_lined": 10, "expect_unlined": 53}
    for path in (
        "/Game/Art/Town/Rime/SM_Town_Rime_Wall_Straight",
        "/Game/Art/Town/Rime/SM_Town_Rime_Wall_Straight_Unlined",
        "/Game/Art/Town/Rime/SM_Town_Rime_Wall_Corner",
        "/Game/Art/Town/Rime/SM_Town_Rime_Wall_Gate",
    ):
        ok, info = mats_ok(path)
        report["materials"][path] = {"ok": ok, "slots": info}
        md, ms = unreal.GFMineableLibrary.get_mesh_mineable_mode(unreal.load_asset(path))
        report["mineable"][path.rsplit("/", 1)[1]] = {"mode": str(md), "slots": list(ms)}
    report["ok"] = (
        lined == 10 and unlined == 53
        and all(v["ok"] for v in report["materials"].values())
        and report["mineable"].get(UNLINED, {}).get("mode", "").endswith("NONE")
        and report["mineable"].get("SM_Town_Rime_Wall_Straight", {}).get("mode", "").endswith("SLOTS")
    )
    unreal.log("[gflining-readback] " + json.dumps(report)[:2000])

with open(os.path.join(OUT, "crystal_lining_readback_v1.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=1)
