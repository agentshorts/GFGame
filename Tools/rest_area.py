"""Rest area on the Verdant to Cinderwright road (issue 335): the world map's player start and the caravan's new home.

Pure Python (no unreal), so Tools/place_towns_v2.py, Tools/accel_hop_layout.py and the unit tests read one plan.

Input: the art handoff's world placements file (Rest_Area_Layout_v1_world_placements.json, UE cm, world space). Its
transforms are authoritative: the player start, NPC spots, gather zone, caravan pieces and structure placeholders are
all read from it and nothing here invents a position. Default path:
  <project>/handoff/inbox/rest_area_v1/Rest_Area_Layout_v1_world_placements.json   (env GFTOWN_REST_AREA overrides)

What the world build does with it (see apply()):
  - world_ops: the link road Straight at the city midpoint is removed; the layout's Road_T takes its place and a
    three-tile spur runs south into the clearing.
  - every kit mesh in the layout is placed (paving, caravan pen and carts, stalls, props, lamps, trees, road pieces).
  - markers: one PlayerStart tagged START_TAG (layout location + START_HEIGHT_CM, layout yaw), one NPC spot TargetPoint
    per npc_spots entry, and the gather zone box.
  - structures: an asset in STRUCTURE_MESHES is placed as its mesh once imported; any other placeholder (or a
    structure whose mesh is not imported yet) becomes a TargetPoint marker with the layout transform.
  - the Verdant caravan yard is left out (CARAVAN_GROUP pieces of the Verdant town layout, plus the props dressing them),
    so the caravan stands only at the rest area.
  - world-level scenery (barren trees, rocks, patches) inside the rest area's box is left out; roads are kept.

Run: python Tools/rest_area.py [<placements json>]   prints the plan summary.
"""
import copy
import io
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT = "Rest_Area_Layout_v1"
FILE_NAME = LAYOUT + "_world_placements.json"
SCHEMA = "placement/v1"
GROUP_PREFIX = LAYOUT + "/"        # outliner folder / group prefix for everything placed from the layout
START_TAG = "RestArea"             # PlayerStartTag; AGFGameMode prefers it until the player has a home (FGFRestAreaSpawn)
START_HEIGHT_CM = 120.0            # same stand height as every other placed start (FGFPlayerStartRules::FallbackZ)
NPC_SPOT_TAG = "GFRestAreaNpcSpot"
PLACEHOLDER_TAG = "GFRestAreaPlaceholder"
GATHER_ZONE_TAG = "GatherZone_RestArea"
CAMPFIRE_LIGHT_TAG = "GFRestAreaCampfireLight"
CARAVAN_GROUP = "caravan"
CARAVAN_ASSETS = ("SM_Town_CaravanPen", "SM_Town_CaravanCart")
VERDANT_LAYOUT = "Town_Verdant_A_Layout_v3"
YARD_DRESSING_CM = 600.0           # Verdant props this close to a caravan piece belong to the yard and leave with it
YARD_DRESSING_GROUPS = ("props",)
CLEAR_PAD_CM = 300.0               # world scenery this close to the rest area's box is left out
KEEP_GROUPS = ("link_roads",)      # world groups never cut by the clear box (the road itself)
# Structure meshes delivered after the layout (Data/Town/rest_area_structures.json). Placed as meshes once imported.
STRUCTURE_MESHES = ("SM_Town_Caravan_Campfire", "SM_Town_Caravan_NoticeBoard", "SM_Town_Caravan_CoveredWagon",
                    "SM_Town_Caravan_Tent")   # the one Tent mesh serves both tent placements
CAMPFIRE = "SM_Town_Caravan_Campfire"
CAMPFIRE_LIGHT = {"offset_cm": [0.0, 0.0, 60.0], "intensity_cd": 800.0, "radius_cm": 900.0,
                  "colour": [255, 147, 41]}   # warm point light over the fire ring; flame FX come later


def default_path(project_dir=None):
    env = os.environ.get("GFTOWN_REST_AREA", "").strip()
    if env:
        return env
    return os.path.join(project_dir or ROOT, "handoff", "inbox", "rest_area_v1", FILE_NAME)


def path_next_to_kit(kit):
    """The rest area file beside a towns_v2 kit dir (handoff/inbox/towns_v2 -> handoff/inbox/rest_area_v1)."""
    env = os.environ.get("GFTOWN_REST_AREA", "").strip()
    if env:
        return env
    return os.path.join(os.path.dirname(os.path.normpath(kit)), "rest_area_v1", FILE_NAME)


def load(path):
    """The validated layout dict, or None when the file is not there."""
    if not path or not os.path.isfile(path):
        return None
    with io.open(path, encoding="utf-8") as f:
        rest = json.load(f)
    validate(rest)
    return rest


def validate(rest):
    """Raises ValueError when the file is not a usable rest area layout."""
    if rest.get("schema") != SCHEMA:
        raise ValueError("rest area: schema {} (want {})".format(rest.get("schema"), SCHEMA))
    if rest.get("space") != "world":
        raise ValueError("rest area: placements must be in world space")
    starts = [p for p in rest.get("placements", []) if p.get("kind") == "PlayerStart"]
    if len(starts) != 1:
        raise ValueError("rest area: {} PlayerStart placements (want 1)".format(len(starts)))
    if not rest.get("gather_zones"):
        raise ValueError("rest area: no gather zone")
    for p in rest["placements"]:
        r = p.get("rotation_ue_deg", {})
        if abs(r.get("pitch", 0.0)) > 1e-6 or abs(r.get("roll", 0.0)) > 1e-6:
            raise ValueError("rest area: {} has pitch or roll".format(p.get("asset")))
    return True


def _is_marker(p):
    return p.get("kind") in ("PlayerStart", "NPCSpawn")


def _is_placeholder(p):
    return p.get("status") == "placeholder"


def player_start(rest):
    """dict(location, yaw, tag): the stand point (layout location lifted by START_HEIGHT_CM) and facing."""
    p = [q for q in rest["placements"] if q.get("kind") == "PlayerStart"][0]
    l = p["location_ue_cm"]
    return {"location": [float(l[0]), float(l[1]), float(l[2]) + START_HEIGHT_CM],
            "yaw": float(p["rotation_ue_deg"]["yaw"]), "tag": START_TAG}


def npc_spots(rest):
    """[dict(id, location, yaw, note)] in file order."""
    return [{"id": p["asset"], "location": [float(v) for v in p["location_ue_cm"]],
             "yaw": float(p["rotation_ue_deg"]["yaw"]), "note": p.get("note", "")}
            for p in rest["placements"] if p.get("kind") == "NPCSpawn"]


def gather_zone(rest):
    z = rest["gather_zones"][0]
    return {"id": z["id"], "center": [float(v) for v in z["center_ue_cm"]], "extent": [float(v) for v in z["extents_ue_cm"]]}


def structures(rest):
    """Placeholder pieces: [dict(asset, location, yaw, scale, footprint, has_mesh)]. has_mesh = delivered mesh exists."""
    out = []
    for p in rest["placements"]:
        if _is_placeholder(p):
            out.append({"asset": p["asset"], "location": [float(v) for v in p["location_ue_cm"]],
                        "yaw": float(p["rotation_ue_deg"]["yaw"]), "scale": p.get("scale_ue") or [1, 1, 1],
                        "footprint": p.get("footprint_cm"), "has_mesh": p["asset"] in STRUCTURE_MESHES})
    return out


def kit_meshes(rest):
    """Kit mesh placements (no markers, no placeholders), group prefixed with GROUP_PREFIX."""
    out = []
    for p in rest["placements"]:
        if _is_marker(p) or _is_placeholder(p):
            continue
        q = copy.deepcopy(p)
        q["group"] = GROUP_PREFIX + p["group"]
        out.append(q)
    return out


def campfire_light(rest):
    """Point light over each campfire placement: dict(location, intensity_cd, radius_cm, colour)."""
    out = []
    for s in structures(rest):
        if s["asset"] == CAMPFIRE:
            o = CAMPFIRE_LIGHT["offset_cm"]
            out.append({"location": [s["location"][i] + o[i] for i in range(3)], "intensity_cd": CAMPFIRE_LIGHT["intensity_cd"],
                        "radius_cm": CAMPFIRE_LIGHT["radius_cm"], "colour": list(CAMPFIRE_LIGHT["colour"])})
    return out


def clear_box(rest):
    """(x0, x1, y0, y1): every rest area piece except the road ones (the T and spur sit on/next to the highway), padded."""
    pts = [p["location_ue_cm"] for p in rest["placements"] if p.get("group") != "access"]
    xs = [l[0] for l in pts]
    ys = [l[1] for l in pts]
    return (min(xs) - CLEAR_PAD_CM, max(xs) + CLEAR_PAD_CM, min(ys) - CLEAR_PAD_CM, max(ys) + CLEAR_PAD_CM)


def _same_xy(a, b, tol=1.0):
    return abs(a[0] - b[0]) <= tol and abs(a[1] - b[1]) <= tol


def removed_by_ops(p, rest):
    """True when a world_ops remove entry matches this world-level placement."""
    for op in rest.get("world_ops") or []:
        if op.get("op") != "remove":
            continue
        m = op["match"]
        if p.get("asset") == m.get("asset") and p.get("group") == m.get("group") and _same_xy(p["location_ue_cm"], m["location_ue_cm"]):
            return True
    return False


def in_clear_box(asset, group, loc, box):
    """World scenery inside the rest area box (roads are kept)."""
    if group in KEEP_GROUPS or "Road" in asset:
        return False
    return box[0] <= loc[0] <= box[1] and box[2] <= loc[1] <= box[3]


def caravan_yard(placements):
    """Indices of a town layout's caravan yard: CARAVAN_GROUP pieces plus YARD_DRESSING_GROUPS props within
    YARD_DRESSING_CM of one of them."""
    yard = [i for i, p in enumerate(placements) if p.get("group") == CARAVAN_GROUP]
    out = set(yard)
    for i, p in enumerate(placements):
        if p.get("group") not in YARD_DRESSING_GROUPS:
            continue
        l = p["location_ue_cm"]
        if any(math.hypot(l[0] - placements[j]["location_ue_cm"][0], l[1] - placements[j]["location_ue_cm"][1]) <= YARD_DRESSING_CM
               for j in yard):
            out.add(i)
    return out


def without_yard(layout):
    """Copy of a town layout with its caravan yard left out (counts and total_instances follow)."""
    cut = caravan_yard(layout["placements"])
    out = copy.deepcopy(layout)
    out["placements"] = [p for i, p in enumerate(layout["placements"]) if i not in cut]
    counts = {}
    for p in out["placements"]:
        counts[p["asset"]] = counts.get(p["asset"], 0) + 1
    out["counts"] = counts
    out["total_instances"] = len(out["placements"])
    out["caravan_yard_removed"] = len(cut)
    return out


def apply(world, rest):
    """Copy of the world layout with the rest area in it: world_ops applied, scenery in the clear box cut, the layout's
    kit meshes appended. flattened_instances follows (removed pieces out, rest meshes in); the Verdant yard is cut
    separately (without_yard) because it lives in the Verdant town layout_ref."""
    box = clear_box(rest)
    out = copy.deepcopy(world)
    kept, removed, cleared = [], 0, 0
    for p in world["placements"]:
        if removed_by_ops(p, rest):
            removed += 1
        elif in_clear_box(p["asset"], p.get("group", ""), p["location_ue_cm"], box):
            cleared += 1
        else:
            kept.append(copy.deepcopy(p))
    meshes = kit_meshes(rest)
    out["placements"] = kept + meshes
    out["total_instances"] = len(out["placements"])
    if world.get("flattened_instances"):
        out["flattened_instances"] = world["flattened_instances"] - removed - cleared + len(meshes)
    out["rest_area"] = {"removed": removed, "cleared": cleared, "added": len(meshes), "clear_box": list(box)}
    return out


def summary(rest):
    return {"player_start": player_start(rest), "npc_spots": len(npc_spots(rest)), "gather_zone": gather_zone(rest),
            "kit_meshes": len(kit_meshes(rest)), "structures": structures(rest), "campfire_light": campfire_light(rest),
            "clear_box": list(clear_box(rest)), "world_ops": rest.get("world_ops")}


if __name__ == "__main__":
    import sys
    r = load(sys.argv[1] if len(sys.argv) > 1 else default_path())
    print(json.dumps(summary(r), indent=1) if r else "no rest area layout")
