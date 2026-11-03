"""Finds the start clearing in a flattened town layout: open ground in a wood, away from walls, water and lava.

Pure Python (no unreal), so Tools/place_towns_v2.py and a plain `python Tools/town_clearing.py` agree on the spot.
A placement blocks a circle of its manifest footprint (XY circumradius times scale); see find() for the scoring.

Run: python Tools/town_clearing.py [<towns_v2 kit dir>]  prints the World_Factions start clearing.
"""
import io
import json
import math
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLEAR_MIN_CM = 1000.0     # open ground radius (10 m) around the start
OTHER_CLEAR_CM = 2000.0   # nothing but Verdant trees within 20 m
WOOD_RING_CM = 4000.0     # trees counted out to 40 m
MIN_QUADRANT = 3          # trees per quadrant of that ring, so the wood is all round
EDGE_PAD_CM = 2000.0      # stay this far inside the ground region
STEP_CM = 100.0
TREE = re.compile(r"ForestTree")


def footprints(kit, v1_kit):
    """asset -> XY circumradius (cm) from each mesh's manifest bounds."""
    out = {}
    for name, base in (("town_v1_mineable.json", v1_kit), ("towns_v2_mineable.json", kit)):
        for m in json.load(io.open(os.path.join(ROOT, "Data", "Town", name), encoding="utf-8"))["meshes"]:
            folder = m["folder"]
            path = os.path.normpath(os.path.join(base, folder, m["manifest"]))
            try:
                man = json.load(io.open(path, encoding="utf-8"))
            except (IOError, OSError):
                continue
            b = man.get("bounds_ue_cm")
            if b:
                lo, hi = b["lo"], b["hi"]
                out[m["asset"]] = math.hypot(max(abs(lo[0]), abs(hi[0])), max(abs(lo[1]), abs(hi[1])))
            elif man.get("dimensions_cm"):
                d = man["dimensions_cm"]
                out[m["asset"]] = math.hypot(d[0], d[1]) / 2
    return out


def flatten(layouts, name):
    """(asset, group, loc, scale, source) for every placement; layout_refs go through their own XY offset and yaw."""
    lay = json.load(io.open(layouts[name], encoding="utf-8"))
    out = [(p["asset"], p["group"], p["location_ue_cm"], p.get("scale_ue") or [1, 1, 1], name) for p in lay["placements"]]
    for ref in lay.get("layout_refs") or []:
        yaw = math.radians(ref.get("rotation_ue_deg", {}).get("yaw", 0.0))
        c, s = math.cos(yaw), math.sin(yaw)
        o = ref["location_ue_cm"]
        for a, g, l, sc, src in flatten(layouts, ref["layout_ref"]):
            out.append((a, g, [o[0] + c * l[0] - s * l[1], o[1] + s * l[0] + c * l[1], o[2] + l[2]], sc, src))
    return out


def _radius(radii, asset, scale, default_radius=300.0):
    s = max(abs(scale[0]), abs(scale[1])) if scale else 1.0
    return radii.get(asset, default_radius) * s


def find(placements, radii, region, avoid=()):
    """Best start clearing as a dict (location_cm, clear_radius_cm, trees_within_40m, trees_by_quadrant, cut_trees,
    nearest_other_cm).

    placements: (asset, loc[3], scale[3]). region: (x0, x1, y0, y1) of the ground to search, padded by EDGE_PAD_CM;
    avoid: (x0, x1, y0, y1) boxes to skip (the starter voxel plot).

    The towns_v2 woods are bands a few trees deep with no open glade inside them, so a clearing of CLEAR_MIN_CM is
    opened by leaving out the Verdant trees whose footprint reaches into it (`cuts`). A spot qualifies when every
    quadrant of the WOOD_RING_CM ring still holds MIN_QUADRANT trees and nothing but Verdant trees (no walls, roads,
    buildings, water, lava or other biomes' trees) comes within OTHER_CLEAR_CM. Best = most trees left standing
    around it minus twice the trees cut; an existing glade (no cuts) therefore wins over a carved one.
    """
    blocks = [(loc[0], loc[1], _radius(radii, asset, scale), bool(TREE.search(asset)))
              for asset, loc, scale in placements]
    cell = 2000.0
    grid = {}
    for bx, by, br, tree in blocks:
        grid.setdefault((int(math.floor(bx / cell)), int(math.floor(by / cell))), []).append((bx, by, br, tree))
    reach = int(math.ceil(WOOD_RING_CM / cell)) + 1
    x0, x1, y0, y1 = region
    x0, x1, y0, y1 = x0 + EDGE_PAD_CM, x1 - EDGE_PAD_CM, y0 + EDGE_PAD_CM, y1 - EDGE_PAD_CM
    best = None
    for ix in range(int((x1 - x0) // STEP_CM) + 1):
        x = x0 + ix * STEP_CM
        for iy in range(int((y1 - y0) // STEP_CM) + 1):
            y = y0 + iy * STEP_CM
            if any(a[0] <= x <= a[1] and a[2] <= y <= a[3] for a in avoid):
                continue
            cx, cy = int(math.floor(x / cell)), int(math.floor(y / cell))
            quad, cut, other = [0, 0, 0, 0], 0, 1e9
            for i in range(cx - reach, cx + reach + 1):
                for j in range(cy - reach, cy + reach + 1):
                    for bx, by, br, tree in grid.get((i, j), ()):
                        d = math.hypot(bx - x, by - y)
                        if not tree:
                            other = min(other, d - br)
                        elif d - br < CLEAR_MIN_CM:
                            cut += 1
                        elif d <= WOOD_RING_CM:
                            quad[(bx >= x) + 2 * (by >= y)] += 1
            if other < OTHER_CLEAR_CM or min(quad) < MIN_QUADRANT:
                continue
            key = (sum(quad) - 2 * cut, min(quad), -x, -y)
            if best is None or key > best[0]:
                best = (key, {"location_cm": [round(x, 1), round(y, 1)], "clear_radius_cm": CLEAR_MIN_CM,
                              "trees_within_40m": sum(quad), "trees_by_quadrant": quad, "cut_trees": cut,
                              "nearest_other_cm": round(other, 1)})
    return best[1] if best else None


def cuts(asset, loc, scale, radii, clearing):
    """True for a Verdant tree whose footprint reaches into the clearing (the placer leaves it out)."""
    if not clearing or not TREE.search(asset):
        return False
    c = clearing["location_cm"]
    return math.hypot(loc[0] - c[0], loc[1] - c[1]) - _radius(radii, asset, scale) < clearing["clear_radius_cm"]


def world_region(placements):
    """Grass part of the world map, split like place_towns_v2.build: x below the Cinderwright gap, y below Rime."""
    src = {}
    for a, g, l, sc, s in placements:
        src.setdefault(s, []).append(l)
    xs = [l[0] for a, g, l, sc, s in placements]
    ys = [l[1] for a, g, l, sc, s in placements]
    verdant = [l for s, ls in src.items() if "Verdant" in s or "Rime" in s for l in ls]
    cw = [l for s, ls in src.items() if "Cinderwright" in s for l in ls]
    rime = [l for s, ls in src.items() if "Rime" in s for l in ls]
    vx1, cx = max(l[0] for l in verdant), min(l[0] for l in cw)
    split_x = (cx + vx1) / 2 if cx > vx1 else cx
    vy1 = max(l[1] for s, ls in src.items() if "Verdant" in s for l in ls)
    ry = min(l[1] for l in rime)
    split_y = (ry + vy1) / 2 if ry > vy1 else ry
    return (min(xs), split_x, min(ys), split_y)


VOXEL_PLOT = (-3400.0, 3400.0, -3400.0, 3400.0)  # AGFGameMode::EnsureVoxelWorld starter plot plus 2 m


def world_clearing(layouts, name, kit, v1_kit):
    P = flatten(layouts, name)
    return find([(a, l, sc) for a, g, l, sc, s in P], footprints(kit, v1_kit), world_region(P), avoid=(VOXEL_PLOT,))


def default_layouts(kit):
    v1 = os.path.join(os.path.dirname(kit), "town_v1")
    return {
        "Town_Verdant_A_Layout_v2": os.path.join(v1, "Town_Verdant_A_Layout_v2", "Town_Verdant_A_Layout_v2_placements.json"),
        "Town_Verdant_Homestead_Layout_v1": os.path.join(kit, "layouts_v1", "Town_Verdant_Homestead_Layout_v1_placements.json"),
        "Town_Rime_Layout_v1": os.path.join(kit, "layouts_v1", "Town_Rime_Layout_v1_placements.json"),
        "Town_Cinderwright_Layout_v3": os.path.join(kit, "fixes_v1", "layouts", "Town_Cinderwright_Layout_v3_placements.json"),
        "World_Factions_Layout_v3": os.path.join(kit, "fixes_v1", "layouts", "World_Factions_Layout_v3_placements.json"),
    }


if __name__ == "__main__":
    import sys
    kit = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "handoff", "inbox", "towns_v2")
    print(json.dumps(world_clearing(default_layouts(kit), "World_Factions_Layout_v3", kit,
                                    os.path.join(os.path.dirname(kit), "town_v1")), indent=1))
