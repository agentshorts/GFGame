"""Checks for the issue 335 rest area plan (Tools/rest_area.py). No Unreal editor needed.

Run from the repo root:

  python3 -m unittest Tools/test_rest_area.py

The synthetic tests always run. The real-file tests need the rest area layout (GFTOWN_REST_AREA or
<GFTOWN_KIT>/../rest_area_v1/Rest_Area_Layout_v1_world_placements.json) and, for the world and Verdant checks, the
towns_v2 and town_v1 kits; they are skipped without them.
"""
import copy
import json
import os
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Tools"))
import rest_area  # noqa: E402


def _p(asset, group, x, y, yaw=0.0, kind=None, status=None):
    d = {"asset": asset, "group": group, "location_ue_cm": [x, y, 0], "rotation_ue_deg": {"pitch": 0.0, "yaw": yaw, "roll": 0.0},
         "scale_ue": [1.0, 1.0, 1.0]}
    if kind:
        d["kind"] = kind
    if status:
        d["status"] = status
    return d


def _rest():
    return {"schema": "placement/v1", "space": "world",
            "gather_zones": [{"id": "GatherZone_RestArea", "center_ue_cm": [1000, -2000, 0], "extents_ue_cm": [700, 700, 200]}],
            "world_ops": [{"op": "remove", "match": {"asset": "SM_Town_Road_Straight", "location_ue_cm": [1000, 0, 0], "group": "link_roads"}}],
            "placements": [_p("PlayerStart", "player_start", 1000, -2000, 0.0, kind="PlayerStart"),
                           _p("NPC_A", "npc_spots", 900, -2100, 90.0, kind="NPCSpawn"),
                           _p("SM_Town_Road_T", "access", 1000, 0),
                           _p("SM_Town_CaravanPen", "caravan", 300, -2000),
                           _p("SM_Town_Caravan_Campfire", "caravan", 1000, -1900, status="placeholder"),
                           _p("SM_Town_Caravan_Tent", "caravan", 800, -2300, 45.0, status="placeholder"),
                           _p("SM_Town_Caravan_Mystery", "caravan", 1200, -2300, status="placeholder"),
                           _p("SM_Town_Paving_Worn", "ground", 1400, -1600)]}


def _world():
    return {"layout": "World_Test", "total_instances": 5, "flattened_instances": 50,
            "placements": [_p("SM_Town_Road_Straight", "link_roads", 600, 0), _p("SM_Town_Road_Straight", "link_roads", 1000, 0),
                           _p("SM_Town_Road_Straight", "link_roads", 1400, 0), _p("SM_Town_Cinderwright_AshTree_A", "barren", 1100, -1500),
                           _p("SM_Town_Cinderwright_AshTree_A", "barren", 9000, -1500)]}


class RestAreaSynthetic(unittest.TestCase):
    def test_start_is_the_layout_start_lifted_to_stand_height(self):
        st = rest_area.player_start(_rest())
        self.assertEqual(st["location"], [1000.0, -2000.0, rest_area.START_HEIGHT_CM])
        self.assertEqual(st["yaw"], 0.0)
        self.assertEqual(st["tag"], "RestArea")

    def test_validate_wants_one_start_and_a_gather_zone(self):
        r = _rest()
        r["placements"].append(copy.deepcopy(r["placements"][0]))
        with self.assertRaises(ValueError):
            rest_area.validate(r)
        r = _rest()
        r["gather_zones"] = []
        with self.assertRaises(ValueError):
            rest_area.validate(r)
        r = _rest()
        r["schema"] = "other"
        with self.assertRaises(ValueError):
            rest_area.validate(r)

    def test_missing_file_is_none_not_a_made_up_start(self):
        self.assertIsNone(rest_area.load(os.path.join(tempfile.gettempdir(), "no_such_rest_area.json")))

    def test_markers_and_placeholders_are_not_meshes(self):
        names = [p["asset"] for p in rest_area.kit_meshes(_rest())]
        self.assertEqual(names, ["SM_Town_Road_T", "SM_Town_CaravanPen", "SM_Town_Paving_Worn"])
        self.assertTrue(all(p["group"].startswith(rest_area.GROUP_PREFIX) for p in rest_area.kit_meshes(_rest())))

    def test_delivered_structures_use_meshes_and_unknown_ones_stay_markers(self):
        s = {x["asset"]: x["has_mesh"] for x in rest_area.structures(_rest())}
        self.assertEqual(s, {"SM_Town_Caravan_Campfire": True, "SM_Town_Caravan_Tent": True, "SM_Town_Caravan_Mystery": False})

    def test_campfire_gets_a_light_above_it(self):
        lights = rest_area.campfire_light(_rest())
        self.assertEqual(len(lights), 1)
        self.assertEqual(lights[0]["location"][:2], [1000.0, -1900.0])
        self.assertGreater(lights[0]["location"][2], 0.0)

    def test_apply_swaps_the_straight_and_clears_scenery(self):
        w = rest_area.apply(_world(), _rest())
        roads = [(p["asset"], p["location_ue_cm"][0]) for p in w["placements"] if "Road" in p["asset"]]
        self.assertNotIn(("SM_Town_Road_Straight", 1000), roads)
        self.assertIn(("SM_Town_Road_T", 1000), roads)
        trees = [p["location_ue_cm"][0] for p in w["placements"] if "AshTree" in p["asset"]]
        self.assertEqual(trees, [9000])
        self.assertEqual(w["rest_area"], {"removed": 1, "cleared": 1, "added": 3, "clear_box": w["rest_area"]["clear_box"]})
        self.assertEqual(w["flattened_instances"], 50 - 1 - 1 + 3)

    def test_caravan_yard_takes_its_dressing_but_not_the_town(self):
        town = {"placements": [_p("SM_Town_CaravanPen", "caravan", 4000, 1000), _p("SM_Town_Crate", "props", 4100, 1200),
                               _p("SM_Town_Crate", "props", 0, 0), _p("SM_Town_House_Small", "buildings", 4000, 1300)],
                "counts": {}, "total_instances": 4}
        self.assertEqual(rest_area.caravan_yard(town["placements"]), {0, 1})
        out = rest_area.without_yard(town)
        self.assertEqual([p["asset"] for p in out["placements"]], ["SM_Town_Crate", "SM_Town_House_Small"])
        self.assertEqual(out["total_instances"], 2)
        self.assertEqual(out["caravan_yard_removed"], 2)


KIT = os.environ.get("GFTOWN_KIT", "").strip() or str(ROOT / "handoff" / "inbox" / "towns_v2")
REST = rest_area.path_next_to_kit(KIT)
WORLD = os.path.join(KIT, "fixes_v1", "layouts", "World_Factions_Layout_v3_placements.json")
VERDANT = os.path.join(os.path.dirname(KIT), "town_v1", "Town_Verdant_A_Layout_v2", "Town_Verdant_A_Layout_v2_placements.json")


@unittest.skipUnless(os.path.isfile(REST), "rest area layout not present")
class RestAreaRealFile(unittest.TestCase):
    def setUp(self):
        self.rest = rest_area.load(REST)

    def test_start_at_the_city_midpoint_clearing_facing_cinderwright(self):
        st = rest_area.player_start(self.rest)
        self.assertEqual(st["location"], [24000.0, -2400.0, 120.0])
        self.assertEqual(st["yaw"], 0.0)

    def test_markers(self):
        self.assertEqual(len(rest_area.npc_spots(self.rest)), 10)
        z = rest_area.gather_zone(self.rest)
        self.assertEqual(z["id"], "GatherZone_RestArea")
        self.assertEqual(z["extent"], [700.0, 700.0, 200.0])

    def test_all_four_structures_have_meshes_and_one_campfire_light(self):
        s = rest_area.structures(self.rest)
        self.assertEqual(sorted(x["asset"] for x in s), ["SM_Town_Caravan_Campfire", "SM_Town_Caravan_CoveredWagon",
                                                         "SM_Town_Caravan_NoticeBoard", "SM_Town_Caravan_Tent", "SM_Town_Caravan_Tent"])
        self.assertTrue(all(x["has_mesh"] for x in s), "no placeholder left")
        self.assertEqual(len(rest_area.campfire_light(self.rest)), 1)

    def test_caravan_and_road_pieces(self):
        m = rest_area.kit_meshes(self.rest)
        caravan = [p for p in m if p["asset"] in rest_area.CARAVAN_ASSETS]
        self.assertEqual(sorted(p["asset"] for p in caravan), ["SM_Town_CaravanCart", "SM_Town_CaravanCart", "SM_Town_CaravanPen"])
        t = [p for p in m if p["asset"] == "SM_Town_Road_T"]
        self.assertEqual(len(t), 1)
        self.assertEqual(t[0]["location_ue_cm"][:2], [24000, 0])
        spur = [p for p in m if p["asset"] == "SM_Town_Road_Straight"]
        self.assertEqual(sorted(p["location_ue_cm"][1] for p in spur), [-1200, -800, -400])

    @unittest.skipUnless(os.path.isfile(WORLD), "towns_v2 kit not present")
    def test_world_gets_the_t_and_loses_the_midpoint_straight(self):
        with open(WORLD, encoding="utf-8") as f:
            w = rest_area.apply(json.load(f), self.rest)
        at = [p["asset"] for p in w["placements"] if p["location_ue_cm"][:2] == [24000, 0]]
        self.assertEqual(at, ["SM_Town_Road_T"])
        self.assertEqual(w["rest_area"]["removed"], 1)
        box = rest_area.clear_box(self.rest)
        for p in w["placements"]:
            if not p["group"].startswith(rest_area.GROUP_PREFIX) and "Road" not in p["asset"]:
                self.assertFalse(rest_area.in_clear_box(p["asset"], p["group"], p["location_ue_cm"], box), p["asset"])

    @unittest.skipUnless(os.path.isfile(VERDANT), "town_v1 kit not present")
    def test_verdant_yard_is_gone(self):
        with open(VERDANT, encoding="utf-8") as f:
            v = json.load(f)
        out = rest_area.without_yard(v)
        self.assertEqual(out["caravan_yard_removed"], 7)
        self.assertFalse([p for p in out["placements"] if p["asset"] in rest_area.CARAVAN_ASSETS])
        self.assertFalse([p for p in out["placements"] if p["group"] == rest_area.CARAVAN_GROUP])


if __name__ == "__main__":
    unittest.main()
