"""Checks for the Acceleration Hop placement plan (Tools/accel_hop_layout.py). No Unreal editor needed.

Run from the repo root:

  python3 -m unittest Tools/test_accel_hop_layout.py

The synthetic-layout tests always run. The real-road test needs the towns_v2 kit at handoff/inbox/towns_v2
(or GFTOWN_KIT) and is skipped without it.
"""
import math
import os
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Tools"))
import accel_hop_layout as hop  # noqa: E402


def _road(asset, x, y, yaw, group="link_roads"):
    return {"asset": asset, "group": group, "location_ue_cm": [x, y, 0],
            "rotation_ue_deg": {"pitch": 0.0, "yaw": yaw, "roll": 0.0}, "scale_ue": [1, 1, 1]}


def _layout(placements, cinder=(48000.0, 0.0), yaw=180.0):
    return {"layout": "World_Test", "placements": placements,
            "layout_refs": [{"layout_ref": "Town_Verdant_A_Layout_v2", "location_ue_cm": [0, 0, 0],
                             "rotation_ue_deg": {"pitch": 0, "yaw": 0, "roll": 0}},
                            {"layout_ref": "Town_Cinderwright_Layout_v3", "location_ue_cm": [cinder[0], cinder[1], 0],
                             "rotation_ue_deg": {"pitch": 0, "yaw": yaw, "roll": 0}}]}


class AccelHopLayoutSynthetic(unittest.TestCase):
    def test_two_tiles_per_straight_on_the_road_surface(self):
        lay = _layout([_road("SM_Town_Road_Straight", 5200 + 400 * i, 0, 0.0) for i in range(5)])
        p = hop.plan(lay)
        self.assertEqual(p["straights"], 5)
        self.assertEqual(len(p["tiles"]), 10)
        xs = [t["location"][0] for t in p["tiles"]]
        self.assertEqual(xs, [5100, 5300, 5500, 5700, 5900, 6100, 6300, 6500, 6700, 6900])
        for t in p["tiles"]:
            self.assertAlmostEqual(t["location"][2], 6.25)
            self.assertAlmostEqual(t["location"][1], 0.0)
            self.assertEqual(t["yaw"], 0.0)

    def test_straights_only(self):
        lay = _layout([_road("SM_Town_Road_Straight", 5200, 0, 0.0), _road("SM_Town_Road_Corner", 5600, 0, 0.0),
                       _road("SM_Town_Road_T", 6000, 0, 0.0), _road("SM_Town_Road_Straight", 6400, 0, 0.0)])
        p = hop.plan(lay)
        self.assertEqual(p["straights_on_line"], 2)
        self.assertNotIn(1, [t["road_index"] for t in p["tiles"]])
        self.assertNotIn(2, [t["road_index"] for t in p["tiles"]])

    def test_other_roads_are_left_out(self):
        rime = [_road("SM_Town_Road_Straight", 0, 4400 + 400 * i, 90.0) for i in range(4)]
        off_line = [_road("SM_Town_Road_Straight", 8000, 1200, 0.0)]
        town = [_road("SM_Town_Road_Straight", 9000, 0, 0.0, group="streets")]
        behind = [_road("SM_Town_Road_Straight", -800, 0, 0.0)]
        main = [_road("SM_Town_Road_Straight", 5200 + 400 * i, 0, 0.0) for i in range(3)]
        p = hop.plan(_layout(rime + off_line + town + behind + main))
        self.assertEqual(p["straights"], 3)
        self.assertEqual(sorted({t["road_index"] for t in p["tiles"]}), [7, 8, 9])

    def test_reversed_straights_keep_chevrons_toward_cinder(self):
        lay = _layout([_road("SM_Town_Road_Straight", 5200, 0, 180.0), _road("SM_Town_Road_Straight", 5600, 0, 0.0)])
        p = hop.plan(lay)
        self.assertEqual([t["yaw"] for t in p["tiles"]], [0.0, 0.0, 0.0, 0.0])
        self.assertEqual([t["location"][0] for t in p["tiles"]], [5100, 5300, 5500, 5700])

    def test_diagonal_road_follows_its_own_axis(self):
        d = 400.0 / math.sqrt(2.0)
        lay = _layout([_road("SM_Town_Road_Straight", 3000 + d * i, 3000 + d * i, 45.0) for i in range(3)],
                      cinder=(30000.0, 30000.0))
        p = hop.plan(lay)
        self.assertEqual(p["straights"], 3)
        first = p["tiles"][0]["location"]
        self.assertAlmostEqual(first[0], 3000 - 100 / math.sqrt(2.0), places=2)
        self.assertAlmostEqual(first[1], 3000 - 100 / math.sqrt(2.0), places=2)
        self.assertAlmostEqual(p["tiles"][0]["yaw"], 45.0)

    def test_kinds_open_and_close_on_landing(self):
        n = 30
        kinds = [hop.kind_for(i, n) for i in range(n)]
        self.assertEqual(kinds[:2], ["Landing", "Landing"])
        self.assertEqual(kinds[-2:], ["Landing", "Landing"])
        self.assertNotIn("Landing", kinds[2:-2])
        self.assertEqual(kinds[2], "Downbeat")
        self.assertEqual(kinds[2:10], list(hop.PATTERN))
        with self.assertRaises(ValueError):
            hop.kind_for(n, n)

    def test_longest_contiguous_run_wins(self):
        a = [_road("SM_Town_Road_Straight", 5200 + 400 * i, 0, 0.0) for i in range(2)]
        b = [_road("SM_Town_Road_Straight", 9200 + 400 * i, 0, 0.0) for i in range(6)]
        p = hop.plan(_layout(a + b))
        self.assertEqual(p["runs"], [2, 6])
        self.assertEqual(p["straights"], 6)

    def test_mesh_paths(self):
        self.assertEqual(hop.mesh_path("Boost"), "/Game/Art/AccelerationHop/Tiles/SM_Hop_Tile_Boost.SM_Hop_Tile_Boost")
        with self.assertRaises(ValueError):
            hop.mesh_path("Stilts")

    def test_no_cinder_ref_is_an_error(self):
        with self.assertRaises(ValueError):
            hop.plan({"layout": "x", "placements": [], "layout_refs": []})


KIT = os.environ.get("GFTOWN_KIT", "").strip() or str(ROOT / "handoff" / "inbox" / "towns_v2")


@unittest.skipUnless(os.path.isfile(hop.default_layouts(KIT)[hop.WORLD_LAYOUT]), "towns_v2 kit not present")
class AccelHopLayoutRealRoad(unittest.TestCase):
    def test_verdant_to_cinder_road(self):
        p = hop.plan_from_kit(KIT)
        self.assertEqual(p["to"], [48000.0, 0.0])
        self.assertEqual(p["straights"], 71)
        self.assertEqual(p["runs"], [71])
        self.assertEqual(len(p["tiles"]), 142)
        self.assertEqual(p["tiles"][0]["location"], [5100.0, 0.0, 6.25])
        self.assertEqual(p["tiles"][-1]["location"], [33300.0, 0.0, 6.25])
        self.assertEqual(sum(p["counts"].values()), 142)
        self.assertEqual(p["counts"]["Landing"], 4)
        lay = hop.load(hop.default_layouts(KIT)[hop.WORLD_LAYOUT])
        for t in p["tiles"]:
            self.assertEqual(lay["placements"][t["road_index"]]["asset"], "SM_Town_Road_Straight")
        locs = {tuple(t["location"]) for t in p["tiles"]}
        self.assertEqual(len(locs), 142, "no two tiles share a spot")


if __name__ == "__main__":
    unittest.main()
