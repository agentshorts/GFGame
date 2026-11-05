"""Acceleration Hop tutorial course plan (issue 348) for L_AccelHop_Tutorial_v1.

Pure Python (no unreal), so Tools/create_accel_hop_tutorial_map.py in the editor and a plain
`python3 Tools/accel_hop_tutorial_layout.py` agree on every tile. Unit tested by Tools/test_accel_hop_tutorial_layout.py.

The course is one straight line along +X from the player start, four lessons in teaching order with a short walk
between them (the floor is continuous, so a miss never drops you into the void):

  1 Beat   a strip of tiles, one per 200 cm, no lit target (learn the timing)
  2 Steer  a zigzag: tiles alternate left and right of the centre line, one hop apart (learn air steer)
  3 Lit    a strip with the road's bar pattern, lit target on (learn to chase the magenta diamond)
  4 Chain  strip, zigzag, strip, lit target on (all three at once), then the finish HUB gate

One AGFAccelHopRoad box spans every tile: one song, one beat clock. Lessons 1 and 2 opt their tiles out of the lit
pick (can_be_lit False) so the diamond first shows up in lesson 3. Every tile carries the tag
GFAccelHopTutorial.Lesson.<Id>; the C++ rules (Source/GFRhythm/Public/GFAccelHopTutorialRules.h) read it.

Design defaults (issue 348 PR): pitches, counts and gaps below. Tile pivot is its bottom centre, size 200 x 350 x 6.25.
"""
import json
import math

LESSONS = ("Beat", "Steer", "Lit", "Chain")
TAG = "GFAccelHopTutorial"
KINDS = ("Standard", "Downbeat", "Boost", "Landing")
MESH_DIR = "/Game/Art/AccelerationHop/Tiles"
TILE_SIZE_CM = (200.0, 350.0, 6.25)
TILE_Z_CM = 0.0                  # tiles sit on the floor top (Z 0)
STRIP_PITCH_CM = 200.0           # strips: tiles touch end to end, like the world road
ZIG_PITCH_CM = 450.0             # zigzag: one hop (FGFAccelHopLitTuning::HopLengthCm) apart
ZIG_SIDE_CM = 250.0              # zigzag tile centres at +-250 (inner edges 150 cm apart, outside the 60 cm dead zone)
FIRST_TILE_X_CM = 600.0
LESSON_GAP_CM = 800.0            # a short walk between lessons, where the prompt changes
FINISH_GATE_AFTER_CM = 600.0     # finish HUB gate this far past the last tile
ROAD_PAD_CM = (300.0, 400.0, 400.0)   # music box padding: along, across, up
FLOOR_THICK_CM = 50.0
FLOOR_HALF_WIDTH_CM = 800.0
FLOOR_START_X_CM = -900.0        # behind the marker and the start HUB gate at x -500
FLOOR_PAST_FINISH_CM = 600.0
PLAYER_START = (0.0, 0.0, 100.0)
DIRECTOR_LOCATION = (-200.0, -400.0, 0.0)
STRIP_PATTERN = ("Downbeat", "Standard", "Standard", "Standard", "Standard", "Standard", "Boost", "Boost")

# Each lesson: list of (shape, count). Shapes: "beat" (Landing ends, Downbeat every 4th, no Boost), "zig", "strip"
# (Landing ends, the road's bar pattern).
LESSON_SECTIONS = {
    "Beat": [("beat", 12)],
    "Steer": [("zig", 8)],
    "Lit": [("strip", 16)],
    "Chain": [("strip", 8), ("zig", 4), ("strip", 8)],
}
CAN_BE_LIT = {"Beat": False, "Steer": False, "Lit": True, "Chain": True}


def mesh_path(kind):
    if kind not in KINDS:
        raise ValueError("unknown hop tile kind " + str(kind))
    name = "SM_Hop_Tile_" + kind
    return "{}/{}.{}".format(MESH_DIR, name, name)


def lesson_tag(lesson):
    if lesson not in LESSONS:
        raise ValueError("unknown lesson " + str(lesson))
    return TAG + ".Lesson." + lesson


SHAPES = ("beat", "zig", "strip")


def kind_for(shape, slot, count):
    if shape not in SHAPES:
        raise ValueError("unknown shape " + str(shape))
    if count <= 0 or slot < 0 or slot >= count:
        raise ValueError("slot out of range")
    if shape == "zig":
        return "Landing" if slot in (0, count - 1) else "Standard"
    if slot == 0 or slot == count - 1:
        return "Landing"
    if shape == "beat":
        return "Downbeat" if (slot - 1) % 4 == 0 else "Standard"
    return STRIP_PATTERN[(slot - 1) % len(STRIP_PATTERN)]


def section_tiles(shape, count, start_x):
    """Tiles of one section from start_x: list of (x, y, kind); returns (tiles, last_x)."""
    out = []
    pitch = ZIG_PITCH_CM if shape == "zig" else STRIP_PITCH_CM
    x = start_x
    for slot in range(count):
        y = (-ZIG_SIDE_CM if slot % 2 == 0 else ZIG_SIDE_CM) if shape == "zig" else 0.0
        out.append((x, y, kind_for(shape, slot, count)))
        x += pitch
    return out, x - pitch


def plan():
    """The full course: tiles (in travel order), lesson starts, road box, floor, finish gate, player start."""
    tiles = []
    lessons = []
    x = FIRST_TILE_X_CM
    for lesson in LESSONS:
        first = len(tiles)
        prev_shape = None
        for shape, count in LESSON_SECTIONS[lesson]:
            if prev_shape is not None:
                # Within a lesson, the next section starts one zig pitch on, so the switch is one hop.
                x += ZIG_PITCH_CM
            sec, last_x = section_tiles(shape, count, x)
            for tx, ty, kind in sec:
                tiles.append({"lesson": lesson, "shape": shape, "kind": kind, "mesh": mesh_path(kind),
                              "location": [round(tx, 3), round(ty, 3), TILE_Z_CM], "yaw": 0.0,
                              "can_be_lit": CAN_BE_LIT[lesson], "tags": [TAG, lesson_tag(lesson), "GFAccelHop_" + kind]})
            x = last_x
            prev_shape = shape
        lessons.append({"lesson": lesson, "tag": lesson_tag(lesson), "first_slot": first, "tiles": len(tiles) - first,
                        "start_x": tiles[first]["location"][0], "end_x": tiles[-1]["location"][0],
                        "can_be_lit": CAN_BE_LIT[lesson]})
        x += LESSON_GAP_CM
    for slot, t in enumerate(tiles):
        t["slot"] = slot
        t["label"] = "TutorialTile_{:03d}_{}_{}".format(slot, t["lesson"], t["kind"])
    first_x = tiles[0]["location"][0]
    last_x = tiles[-1]["location"][0]
    half_len = (last_x - first_x) / 2.0 + TILE_SIZE_CM[0] / 2.0 + ROAD_PAD_CM[0]
    half_across = ZIG_SIDE_CM + TILE_SIZE_CM[1] / 2.0 + ROAD_PAD_CM[1]
    road = {"centre": [round((first_x + last_x) / 2.0, 3), 0.0, 0.0], "yaw": 0.0,
            "half_extent": [round(half_len, 3), round(half_across, 3), ROAD_PAD_CM[2]]}
    finish = [round(last_x + FINISH_GATE_AFTER_CM, 3), 0.0, 0.0]
    floor_end = finish[0] + FLOOR_PAST_FINISH_CM
    floor = {"centre": [round((FLOOR_START_X_CM + floor_end) / 2.0, 3), 0.0, -FLOOR_THICK_CM / 2.0],
             "size": [round(floor_end - FLOOR_START_X_CM, 3), FLOOR_HALF_WIDTH_CM * 2.0, FLOOR_THICK_CM]}
    # /Engine/BasicShapes/Cube is 100 cm on a side, pivot at its centre.
    floor["scale"] = [round(v / 100.0, 4) for v in floor["size"]]
    counts = {k: sum(1 for t in tiles if t["kind"] == k) for k in KINDS}
    return {"tiles": tiles, "lessons": lessons, "road": road, "finish_gate": finish, "floor": floor,
            "player_start": list(PLAYER_START), "director": list(DIRECTOR_LOCATION), "counts": counts}


def along(road, location):
    """Road-local X of a world location, from the box centre (AGFAccelHopRoad::AlongOf measures the same axis)."""
    yaw = math.radians(road["yaw"])
    dx, dy = location[0] - road["centre"][0], location[1] - road["centre"][1]
    return dx * math.cos(yaw) + dy * math.sin(yaw)


if __name__ == "__main__":
    p = plan()
    summary = {k: v for k, v in p.items() if k != "tiles"}
    summary["tile_count"] = len(p["tiles"])
    print(json.dumps(summary, indent=1))
