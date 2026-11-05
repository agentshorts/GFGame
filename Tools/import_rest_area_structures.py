"""Imports the issue 335 rest area structures: SM_Town_Caravan_Campfire, SM_Town_Caravan_NoticeBoard,
SM_Town_Caravan_CoveredWagon and SM_Town_Caravan_Tent (one tent mesh, placed twice), with textures and one
M_GF_Block material instance each. None of them is mineable.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_rest_area_structures.py" -unattended -nullrhi -nosplash

Env: GFREST_STRUCT_KIT (default <project>/handoff/inbox/rest_area_structures_v1: one folder holding the four
SM_Town_Caravan_*_v1 mesh folders; the campfire and notice board came in one art drop and the wagon and tent in
another, so copy all four folders in).

- The list comes from Data/Town/rest_area_structures.json (asset, unreal path, folder, manifest). Import, textures,
  materials and per-mesh QA (dims, tris, slots, UCX collision, min Z, clean source paths) reuse
  Tools/import_town_v1.py do_mesh, with mining stamped None (kit rest_area_structures_v1).
- Run before Tools/place_towns_v2.py, which places them at the rest area (a structure whose mesh is missing stays a
  placeholder TargetPoint and is listed in the layout report).
Writes Saved/ArtImport/rest_area_structures_report.json and logs "[gftown]" lines.
"""
import json, os, sys, traceback
import unreal

os.environ["GFTOWN_LIB"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_town_v1 as base  # noqa: E402

PROJ = base.PROJ
KIT = os.environ.get("GFREST_STRUCT_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "rest_area_structures_v1")
LIST = os.path.join(PROJ, "Data", "Town", "rest_area_structures.json")
report = {"kit": "rest_area_structures_v1", "meshes": {}, "clips": {}, "log": []}
base.report = report
log = base.log


def main():
    base.KIT, base.KIT_NAME = KIT, "rest_area_structures_v1"
    for entry in json.load(open(LIST, encoding="utf-8"))["meshes"]:
        r = {"checks": {}}
        report["meshes"][entry["asset"]] = r
        try:
            if not os.path.isdir(os.path.join(KIT, entry["folder"])):
                raise RuntimeError("folder {} not in {}".format(entry["folder"], KIT))
            base.do_mesh(entry, r)
            r["checks"]["path"] = unreal.EditorAssetLibrary.does_asset_exist(entry["path"])
        except Exception as e:
            r["error"] = str(e) + "\n" + traceback.format_exc()
        r["pass"] = "error" not in r and all(v is True for v in r["checks"].values())
        log("{}: pass={} {}".format(entry["asset"], r["pass"], r["checks"]))
    report["summary"] = {"meshes": len(report["meshes"]),
                         "meshes_pass": sum(1 for r in report["meshes"].values() if r["pass"])}
    os.makedirs(base.OUT, exist_ok=True)
    with open(os.path.join(base.OUT, "rest_area_structures_report.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    log("summary {}".format(report["summary"]))


main()
