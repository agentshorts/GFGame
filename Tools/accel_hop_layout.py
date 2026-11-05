"""Acceleration Hop placement plan for the Verdant to Cinderwright road (issue 334).

Pure Python (no unreal), so Tools/place_acceleration_hop.py in the editor and a plain
`python3 Tools/accel_hop_layout.py [<towns_v2 kit dir>]` agree on every tile.

The road stays as it is: no stilts and no platform. Hop tiles sit on the road surface:
  - straights only (SM_Town_Road_Straight), never corners or T junctions
  - two tiles per straight, at road local X -100 and +100
  - location = road location + (0, 0, 6.25), yaw = road yaw (+180 when that straight faces back toward Verdant,
    so the Boost chevrons, which point to local +X, always point toward Cinderwright)

Which road: the world layout's own link road straights that lie on the line from the Verdant fountain (the world
origin) to the Cinderwright layout_ref, parallel to it, between the two. The Rime road (x 0, running along Y) is
left out by the parallel test.

Tile kinds along the run (a design default, tunable here): the run opens and closes on Landing tiles, and between
them repeats PATTERN, one bar of eight tiles (four straights, 16 m): a Downbeat on the bar line, Standard hops, and a
pair of Boost chevrons before the next bar.
"""
import io
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLD_LAYOUT = "World_Factions_Layout_v3"
ROAD_STRAIGHT = "SM_Town_Road_Straight"
ROAD_GROUP = "link_roads"
CINDER_PREFIX = "Town_Cinderwright_Layout"
ROAD_TILE_CM = 400.0
TILE_OFFSETS_CM = (-100.0, 100.0)   # local X of the two hop tiles on one straight
TILE_Z_CM = 6.25                    # road surface top; the tile's pivot is its bottom centre
TILE_SIZE_CM = (200.0, 350.0, 6.25)
LINE_TOL_CM = 50.0                  # a straight's centre must be this close to the Verdant to Cinderwright line
YAW_TOL_DEG = 1.0
KINDS = ("Standard", "Downbeat", "Boost", "Landing")
PATTERN = ("Downbeat", "Standard", "Standard", "Standard", "Standard", "Standard", "Boost", "Boost")
END_LANDING_TILES = 2               # Landing tiles at each end of the run (one straight's worth)
MESH_DIR = "/Game/Art/AccelerationHop/Tiles"
BOUNDS_PAD_CM = (600.0, 400.0, 400.0)   # music box padding around the run: along, across, up


def mesh_path(kind):
    if kind not in KINDS:
        raise ValueError("unknown hop tile kind " + str(kind))
    name = "SM_Hop_Tile_" + kind
    return "{}/{}.{}".format(MESH_DIR, name, name)


def default_layouts(kit):
    v1 = os.path.join(os.path.dirname(kit), "town_v1")
    return {
        "Town_Verdant_A_Layout_v2": os.path.join(v1, "Town_Verdant_A_Layout_v2", "Town_Verdant_A_Layout_v2_placements.json"),
        "Town_Verdant_Homestead_Layout_v1": os.path.join(kit, "layouts_v1", "Town_Verdant_Homestead_Layout_v1_placements.json"),
        "Town_Rime_Layout_v1": os.path.join(kit, "layouts_v1", "Town_Rime_Layout_v1_placements.json"),
        "Town_Cinderwright_Layout_v3": os.path.join(kit, "fixes_v1", "layouts", "Town_Cinderwright_Layout_v3_placements.json"),
        WORLD_LAYOUT: os.path.join(kit, "fixes_v1", "layouts", WORLD_LAYOUT + "_placements.json"),
    }


def load(path):
    with io.open(path, encoding="utf-8") as f:
        return json.load(f)


def _angle_diff(a, b):
    """Smallest absolute difference between two angles in degrees."""
    return abs((a - b + 180.0) % 360.0 - 180.0)


def endpoints(layout):
    """(verdant_xy, cinder_xy): the world origin (the Verdant fountain) and the Cinderwright layout_ref location."""
    for ref in layout.get("layout_refs") or []:
        if ref["layout_ref"].startswith(CINDER_PREFIX):
            o = ref["location_ue_cm"]
            return (0.0, 0.0), (float(o[0]), float(o[1]))
    raise ValueError("no Cinderwright layout_ref in " + str(layout.get("layout")))


def road_straights(layout, start, end):
    """World-level straights on the start to end road, sorted from start to end.

    Each item: dict(location, yaw, along, index), index = position in layout["placements"]."""
    dx, dy = end[0] - start[0], end[1] - start[1]
    length = math.hypot(dx, dy)
    if length < 1.0:
        raise ValueError("road endpoints coincide")
    ux, uy = dx / length, dy / length
    travel_yaw = math.degrees(math.atan2(uy, ux))
    out = []
    for i, p in enumerate(layout["placements"]):
        if p["asset"] != ROAD_STRAIGHT or p.get("group") != ROAD_GROUP:
            continue
        r = p["rotation_ue_deg"]
        if abs(r.get("pitch", 0.0)) > 1e-6 or abs(r.get("roll", 0.0)) > 1e-6:
            continue
        yaw = float(r["yaw"])
        if min(_angle_diff(yaw, travel_yaw), _angle_diff(yaw, travel_yaw + 180.0)) > YAW_TOL_DEG:
            continue
        l = p["location_ue_cm"]
        rx, ry = l[0] - start[0], l[1] - start[1]
        along = rx * ux + ry * uy
        across = -rx * uy + ry * ux
        if abs(across) > LINE_TOL_CM or along < 0.0 or along > length:
            continue
        out.append({"location": [float(l[0]), float(l[1]), float(l[2])], "yaw": yaw, "along": along, "index": i})
    out.sort(key=lambda s: s["along"])
    return out, travel_yaw


def runs(straights):
    """Splits sorted straights into contiguous runs (neighbours one road tile apart)."""
    groups = []
    for s in straights:
        if groups and abs(s["along"] - groups[-1][-1]["along"] - ROAD_TILE_CM) < 1.0:
            groups[-1].append(s)
        else:
            groups.append([s])
    return groups


def kind_for(slot, count):
    """Tile kind for slot (0 = Verdant end) in a run of count tiles."""
    if count <= 0 or slot < 0 or slot >= count:
        raise ValueError("slot out of range")
    if slot < END_LANDING_TILES or slot >= count - END_LANDING_TILES:
        return "Landing"
    return PATTERN[(slot - END_LANDING_TILES) % len(PATTERN)]


def tiles_for(straights, travel_yaw):
    """Two hop tiles per straight, in travel order, each dict(kind, mesh, location, yaw, road_index, slot)."""
    out = []
    for s in straights:
        flip = _angle_diff(s["yaw"], travel_yaw) > 90.0
        yaw = (s["yaw"] + 180.0) if flip else s["yaw"]
        yaw = (yaw + 180.0) % 360.0 - 180.0
        c, sn = math.cos(math.radians(s["yaw"])), math.sin(math.radians(s["yaw"]))
        offs = TILE_OFFSETS_CM[::-1] if flip else TILE_OFFSETS_CM   # nearer Verdant first
        for ox in offs:
            x, y, z = s["location"]
            out.append({"location": [round(x + c * ox, 3), round(y + sn * ox, 3), round(z + TILE_Z_CM, 3)],
                        "yaw": round(yaw, 3), "road_index": s["index"]})
    for slot, t in enumerate(out):
        t["slot"] = slot
        t["kind"] = kind_for(slot, len(out))
        t["mesh"] = mesh_path(t["kind"])
    return out


def bounds(tiles, travel_yaw):
    """Music box for the run: centre, yaw and half extents (along, across, up), padded by BOUNDS_PAD_CM."""
    if not tiles:
        return None
    first, last = tiles[0]["location"], tiles[-1]["location"]
    centre = [(first[i] + last[i]) / 2.0 for i in range(3)]
    half_len = math.hypot(last[0] - first[0], last[1] - first[1]) / 2.0 + TILE_SIZE_CM[0] / 2.0
    return {"centre": [round(v, 3) for v in centre], "yaw": round(travel_yaw, 3),
            "half_extent": [round(half_len + BOUNDS_PAD_CM[0], 3), round(TILE_SIZE_CM[1] / 2.0 + BOUNDS_PAD_CM[1], 3),
                            round(BOUNDS_PAD_CM[2], 3)]}


def plan(layout):
    """The full placement plan for one world layout dict."""
    start, end = endpoints(layout)
    straights, travel_yaw = road_straights(layout, start, end)
    groups = runs(straights)
    run = max(groups, key=len) if groups else []
    tiles = tiles_for(run, travel_yaw)
    counts = {k: sum(1 for t in tiles if t["kind"] == k) for k in KINDS}
    return {"layout": layout.get("layout"), "from": list(start), "to": list(end), "travel_yaw": round(travel_yaw, 3),
            "straights": len(run), "straights_on_line": len(straights), "runs": [len(g) for g in groups],
            "tiles": tiles, "counts": counts, "bounds": bounds(tiles, travel_yaw)}


def plan_from_kit(kit):
    return plan(load(default_layouts(kit)[WORLD_LAYOUT]))


if __name__ == "__main__":
    import sys
    kit = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "handoff", "inbox", "towns_v2")
    p = plan_from_kit(kit)
    summary = {k: v for k, v in p.items() if k != "tiles"}
    summary["first"] = p["tiles"][:2]
    summary["last"] = p["tiles"][-2:]
    print(json.dumps(summary, indent=1))
