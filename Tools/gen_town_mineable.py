"""Builds Data/Town/town_v1_mineable.json (the mining whitelist for the town_v1 kit) from the handoff's
MINEABLE_town_v1_v2.json and each mesh folder's manifest.

Rule (Captain): only crystal and trees can be mined. Trees are mineable as whole meshes; the 8 rebuilt meshes are
mineable only on material slot 1 (Crystal); every other town mesh is refused, including walls, canal, mosaic
paving, planter and fountain inlay and the power span.

Run: python Tools/gen_town_mineable.py [handoff/inbox/town_v1]
Tools/import_town_v1.py reads the output to stamp UGFMineableAssetUserData on each mesh; GFGame.TownMineable.*
tests check the imported assets against it.
"""
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Data", "Town", "town_v1_mineable.json")
SOURCE = "MINEABLE_town_v1_v2.json"


def manifest_of(folder):
    names = sorted(n for n in os.listdir(folder) if re.match(r"manifest(_v\d+)?\.json$", n))
    if not names:
        raise RuntimeError("no manifest in " + folder)
    # Highest version wins (manifest_v4 over manifest_v3 over manifest.json).
    names.sort(key=lambda n: int(re.search(r"_v(\d+)", n).group(1)) if "_v" in n else 0)
    return names[-1], json.load(io.open(os.path.join(folder, names[-1]), encoding="utf-8"))


def build(kit_dir):
    src = json.load(io.open(os.path.join(kit_dir, SOURCE), encoding="utf-8"))
    meshes = []
    for m in sorted(src["meshes"], key=lambda e: e["asset"]):
        folder = m.get("folder") or "{}_{}".format(m["asset"], m["build"])
        man_name, man = manifest_of(os.path.join(kit_dir, folder))
        kind = m["mineable"]
        if kind == "whole_mesh":
            mode, slots = "whole_mesh", []
        elif kind == "crystal_part_only":
            mode, slots = "slots", [m["crystal_slot"]["index"]]
            assert m["material_slots"][slots[0]] == "Crystal", m["asset"]
        elif kind == "no":
            mode, slots = "none", []
        else:
            raise RuntimeError("unknown mineable value {!r} on {}".format(kind, m["asset"]))
        assert man["material_slots"] == m["material_slots"], m["asset"]
        meshes.append({
            "asset": m["asset"],
            "path": man["unreal_target"],
            "folder": folder,
            "manifest": man_name,
            "mode": mode,
            "slots": slots,
            "material_slots": m["material_slots"],
            "tri_count": man["tri_count"],
            "reason": m.get("crystal_part") or m.get("reason", ""),
        })
    return {
        "schema": "gf.mineable/v1",
        "kit": src["kit"],
        "source": "handoff/inbox/town_v1/" + SOURCE,
        "rule": "Only crystal and trees can be mined. mode whole_mesh: any face; mode slots: only faces of the listed "
                "material slots; mode none (and any mesh not listed): never.",
        "meshes": meshes,
    }


def main():
    kit = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "handoff", "inbox", "town_v1")
    data = build(kit)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1)
        f.write("\n")
    counts = {}
    for m in data["meshes"]:
        counts[m["mode"]] = counts.get(m["mode"], 0) + 1
    print("wrote {} ({} meshes: {})".format(OUT, len(data["meshes"]), counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
