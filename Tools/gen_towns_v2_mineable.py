"""Builds Data/Town/towns_v2_mineable.json (mining whitelist for the towns_v2 kit: Rime, Cinderwright, homesteads,
lava and ground tiles) from the handoff's MINEABLE_towns_v2.json and each mesh folder's manifest, and points the
town_v1 fountain row at the v7 fix (sittable 43.75 cm rim) delivered in towns_v2/town_v1_fixes.

Rule (Captain): only crystal and trees can be mined. Trees (ash trees, snow pines) are mineable as whole meshes;
crystal-bearing meshes only on material slot 1 (Crystal); everything else is refused, including all lava fields,
ground patches, yard plates, smoke plumes, pipes, chimneys, the crane, roads and fences.

Run: python Tools/gen_towns_v2_mineable.py [handoff/inbox/towns_v2]
Tools/import_towns_v2.py reads the output to stamp UGFMineableAssetUserData; GFGame.TownMineable.V2* tests check it.
"""
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Data", "Town", "towns_v2_mineable.json")
TOWN_V1 = os.path.join(ROOT, "Data", "Town", "town_v1_mineable.json")
SOURCE = "MINEABLE_towns_v2.json"
FOUNTAIN_FIX = "town_v1_fixes/SM_Town_Fountain_v7"


def manifest_of(folder):
    names = sorted(n for n in os.listdir(folder) if re.match(r"manifest(_v\d+)?\.json$", n))
    if not names:
        raise RuntimeError("no manifest in " + folder)
    names.sort(key=lambda n: int(re.search(r"_v(\d+)", n).group(1)) if "_v" in n else 0)
    return names[-1], json.load(io.open(os.path.join(folder, names[-1]), encoding="utf-8"))


def row(kit_dir, m):
    man_name, man = manifest_of(os.path.join(kit_dir, m["folder"]))
    kind = m["kind"]
    if kind == "whole_tree":
        mode, slots = "whole_mesh", []
    elif kind == "crystal_slot":
        mode, slots = "slots", [m["material_slots"].index("Crystal")]
        assert slots == [1], m["asset"]
    elif kind == "none":
        mode, slots = "none", []
        assert "Crystal" not in m["material_slots"], m["asset"]
    else:
        raise RuntimeError("unknown kind {!r} on {}".format(kind, m["asset"]))
    assert bool(m["mineable"]) == (mode != "none"), m["asset"]
    assert man["material_slots"] == m["material_slots"], m["asset"]
    assert man["unreal_target"].rsplit("/", 1)[1] == m["asset"], m["asset"]
    return {
        "asset": m["asset"],
        "path": man["unreal_target"],
        "folder": m["folder"],
        "manifest": man_name,
        "mode": mode,
        "slots": slots,
        "material_slots": m["material_slots"],
        "tri_count": man["tri_count"],
        "reason": kind,
    }


def build(kit_dir):
    src = json.load(io.open(os.path.join(kit_dir, SOURCE), encoding="utf-8"))
    rows = [m for m in src["mineable"] + src["not_mineable"] if not m["folder"].startswith("town_v1_fixes/")]
    meshes = sorted((row(kit_dir, m) for m in rows), key=lambda e: e["asset"])
    assert len({m["asset"] for m in meshes}) == len(meshes), "duplicate asset"
    return {
        "schema": "gf.mineable/v1",
        "kit": "towns_v2",
        "source": "handoff/inbox/towns_v2/" + SOURCE,
        "rule": "Only crystal and trees can be mined. mode whole_mesh: any face; mode slots: only faces of the listed "
                "material slots; mode none (and any mesh not listed): never.",
        "meshes": meshes,
    }


def patch_fountain(kit_dir):
    """town_v1's fountain becomes the v7 build (still one Main slot, still refused)."""
    v1 = json.load(io.open(TOWN_V1, encoding="utf-8"))
    man_name, man = manifest_of(os.path.join(kit_dir, FOUNTAIN_FIX))
    for m in v1["meshes"]:
        if m["asset"] == "SM_Town_Fountain":
            assert man["material_slots"] == m["material_slots"] == ["Main"]
            assert man["unreal_target"] == m["path"]
            m.update({"folder": "../towns_v2/" + FOUNTAIN_FIX, "manifest": man_name, "tri_count": man["tri_count"]})
            assert m["mode"] == "none"
    with io.open(TOWN_V1, "w", encoding="utf-8", newline="\n") as f:
        json.dump(v1, f, indent=1)
        f.write("\n")


def main():
    kit = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "handoff", "inbox", "towns_v2")
    data = build(kit)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1)
        f.write("\n")
    patch_fountain(kit)
    counts = {}
    for m in data["meshes"]:
        counts[m["mode"]] = counts.get(m["mode"], 0) + 1
    print("wrote {} ({} meshes: {}); town_v1 fountain -> v7".format(OUT, len(data["meshes"]), counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
