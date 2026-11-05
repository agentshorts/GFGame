"""Box-side checks for the Accel Hop tutorial visual pass v1 palette (no unreal). python3 -m unittest test_accelhop_visual_palette_v1.py"""
import math
import os
import unittest

import accelhop_visual_palette_v1 as pal

HERE = os.path.dirname(os.path.abspath(__file__))


class PaletteTests(unittest.TestCase):
    def test_every_tutorial_tint_is_stronger_than_the_world_road(self):
        for key in pal.all_mi_keys():
            kind = key.split(".")[0]
            old_base, old_glyph = pal.WORLD_TINTS_LINEAR[kind]
            p = pal.mi_params(key)["vectors"]
            for name, old in (("BaseTint", old_base), ("GlyphTint", old_glyph)):
                s_new, v_new = pal.saturation_value(p[name])
                s_old, v_old = pal.saturation_value(old)
                self.assertGreaterEqual(s_new + 1e-3, s_old, "{} {} saturation".format(key, name))
                self.assertGreaterEqual(v_new + 1e-3, v_old * 0.95, "{} {} value".format(key, name))

    def test_glow_is_up_but_the_lit_tile_still_stands_out(self):
        s = pal.TILE_SCALARS["EmissiveStrength"]
        self.assertGreater(s, pal.WORLD_EMISSIVE_STRENGTH)
        self.assertGreaterEqual(pal.LIT_EMISSIVE_STRENGTH / s, 3.0)
        self.assertEqual(pal.TILE_SCALARS["RimLightWidth"], 0.0)  # the lit rim stays unique to the lit tile

    def test_rim_rule_matches_the_world_road(self):
        for key in pal.all_mi_keys():
            v = pal.mi_params(key)["vectors"]
            for r, g in zip(v["RimTint"], v["GlyphTint"]):
                self.assertAlmostEqual(r, g * 0.6, places=4)

    def test_sun_keeps_flat_tile_tops_in_the_top_cel_band(self):
        steps = pal.TILE_SCALARS["CelSteps"]
        n_dot_l = math.sin(math.radians(-pal.SUN["pitch"]))
        band = min(1.0, math.floor(n_dot_l * steps) / (steps - 1.0))
        self.assertEqual(band, 1.0)
        self.assertGreater(n_dot_l, pal.TILE_SCALARS["ShadowThreshold"])

    def test_every_tile_maps_to_one_instance(self):
        for lesson in pal.LESSONS:
            for kind in pal.KINDS:
                self.assertIn(pal.mi_key(kind, lesson), pal.TILE_HEX)
        with self.assertRaises(ValueError):
            pal.mi_key("Standard", None)
        self.assertEqual(pal.lesson_of_tags(["GFAccelHopTutorial", "GFAccelHopTutorial.Lesson.Steer"]), "Steer")
        self.assertIsNone(pal.lesson_of_tags(["GFAccelHopTutorial"]))

    def test_names_are_versioned_new_assets(self):
        for key in pal.all_mi_keys():
            self.assertTrue(pal.mi_name(key).endswith("_Tut_v1"))
            self.assertTrue(pal.mi_path(key).startswith(pal.TUT_MAT_DIR + "/"))
            self.assertNotEqual(pal.mi_path(key), pal.mi_parent(key))
        self.assertTrue(pal.TARGET_MAP.endswith("_v2"))
        self.assertNotEqual(pal.TARGET_MAP, pal.SOURCE_MAP)
        self.assertNotEqual(pal.VIS_TAG, "GFAccelHopTutorial")
        for fx in pal.FX:
            self.assertTrue(fx["name"].startswith("NS_Hop") and fx["name"].endswith("_v1"))

    def test_fx_budget(self):
        live = 0.0
        for fx in pal.FX:
            if fx["kind"] == "burst":
                self.assertLessEqual(fx["count"], pal.FX_BUDGET["max_burst_count"])
                live += fx["count"]
            else:
                self.assertLessEqual(fx["rate"], pal.FX_BUDGET["max_rate"])
                live += fx["rate"] * fx["life"][1]
            self.assertGreater(min(fx["color"]), 0.0)
            self.assertGreater(max(fx["color"]), 1.0, fx["name"] + " must be HDR to bloom")
        self.assertLessEqual(live, pal.FX_BUDGET["max_live_particles_total"])

    def test_bloom_only_above_one(self):
        self.assertGreaterEqual(pal.POST["bloom_threshold"], 1.0)
        self.assertEqual(pal.POST["exposure_min"], pal.POST["exposure_max"])

    def test_hex_round_trip(self):
        for key, (b, g) in pal.TILE_HEX.items():
            self.assertEqual(pal.linear_to_hex(pal.hex_to_linear(b)), b.upper())

    def test_no_em_dashes_in_the_package(self):
        root = os.path.dirname(HERE)
        for d, _, files in os.walk(root):
            for n in files:
                if n.endswith((".py", ".md", ".json", ".txt", ".csv")):
                    with open(os.path.join(d, n), encoding="utf-8") as f:
                        # The GitHub sign-off line is the one allowed exception (drafts only).
                        text = "\n".join(l for l in f.read().splitlines() if l.strip() != "\u2014 Justin")
                        self.assertNotIn("\u2014", text, n)


if __name__ == "__main__":
    unittest.main()
