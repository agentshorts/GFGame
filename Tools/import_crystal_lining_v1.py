"""Import crystal lining structure revisions (#383) and stamp mining data.

Imports the 15 lined revisions plus SM_Town_Rime_Wall_Straight_Unlined from
handoff/inbox/crystal_lining_structures_v1. Whitelist rows already point at those
folders (Data/Town/towns_v2_mineable.json and town_v1_mineable.json).

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_crystal_lining_v1.py" -unattended -nullrhi -nosplash
"""
import json, os, sys, traceback
import unreal

os.environ["GFTOWN_LIB"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_town_v1 as base  # noqa: E402

PROJ = base.PROJ
TOWNS_V2 = os.path.join(PROJ, "handoff", "inbox", "towns_v2")
TOWN_V1 = os.path.join(PROJ, "handoff", "inbox", "town_v1")
V2_WHITE = os.path.join(PROJ, "Data", "Town", "towns_v2_mineable.json")
V1_WHITE = os.path.join(PROJ, "Data", "Town", "town_v1_mineable.json")
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)

V2_ASSETS = {
    "SM_Town_Rime_Wall_Straight", "SM_Town_Rime_Wall_Corner", "SM_Town_Rime_Wall_Gate",
    "SM_Town_Rime_Wall_Straight_Unlined",
    "SM_Town_Cinderwright_House_Small", "SM_Town_Cinderwright_House_Med", "SM_Town_Cinderwright_House_Large",
    "SM_Town_Rime_House_Small", "SM_Town_Rime_House_Med", "SM_Town_Rime_House_Large",
    "SM_Town_Homestead_StarterHouse_Cinderwright", "SM_Town_Homestead_StarterHouse_Rime",
    "SM_Town_Homestead_StarterHouse_Verdant",
}
V1_ASSETS = {"SM_Town_House_Small", "SM_Town_House_Med", "SM_Town_House_Large"}

report = {"kit": "crystal_lining_structures_v1", "meshes": {}, "log": []}
base.report = report


def log(m):
    unreal.log("[gflining] " + str(m))
    report["log"].append(str(m))


def run(kit, kit_name, whitelist, names):
    base.KIT, base.KIT_NAME = kit, kit_name
    for entry in json.load(open(whitelist, encoding="utf-8"))["meshes"]:
        if entry["asset"] not in names:
            continue
        r = {"checks": {}}
        report["meshes"][entry["asset"]] = r
        try:
            base.do_mesh(entry, r)
        except Exception as e:
            r["error"] = str(e) + "\n" + traceback.format_exc()
        r["pass"] = "error" not in r and all(v is True for v in r["checks"].values())
        log("{}: pass={} {}".format(entry["asset"], r["pass"], r.get("checks") or r.get("error", "")[:200]))


run(TOWNS_V2, "towns_v2", V2_WHITE, V2_ASSETS)
run(TOWN_V1, "town_v1", V1_WHITE, V1_ASSETS)
report["summary"] = {
    "meshes": len(report["meshes"]),
    "meshes_pass": sum(1 for r in report["meshes"].values() if r.get("pass")),
}
with open(os.path.join(OUT, "crystal_lining_v1_report.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=1)
log("summary {}".format(report["summary"]))
