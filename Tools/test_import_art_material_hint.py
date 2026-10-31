"""Dry-run checks for the importer's unreal_material_hint step.

No Unreal editor and no binary assets. Run from the repo root:

  python3 -m unittest Tools/test_import_art_material_hint.py
  GFART_MATERIAL_DRY_RUN=1 python3 Tools/import_art_batch.py
"""
import hashlib
import os
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
IMPORTER = ROOT / "Tools" / "import_art_batch.py"
MARKER = "# --- material hint pure section ends ---"
# Bodies of the editor functions that 3b / 7 / worldmap_v1 / beat_hop_v1 still run.
UNCHANGED = {
    "prop_materials": "152423ef05326f7be48f8fd40dd1d0af1474020db172a7e2509540e8ba83157c",
    "extra_slot_materials": "ef6e6749e4a532d0db104a4425fd9a12c30f40d7398c528088b852e7a9f77bd5",
    "hazard_materials": "b24cba6983d0e84c7789d841cf2923ff7f90b7b1befb8f7a0b02e09d0beddb82",
    "prop_mi_path": "9318f4b56051f56026737befdad060f1d6294f6455cba4995201d140e0296fc3",
    "hint_slots": "cb69425350e5a66777821f29adf2e0e3652484aefdd106f786ba6007cdf395fa",
    "hazard_mi_path": "d75ab05fbae45300ea84bb5624eded016ab6003af308ca664d179a767ace08c8",
    "extra_slot_mi_paths": "21c78203cba08c91d64c49dd8d3258f0fe805248eb76312995d376ef42934d50",
}
OLD_PROP = ("3b", "7", "worldmap_v1", "beat_hop_v1")
OLD_HAZARD = ("hazards1",)


def function_source(src, name):
    lines = src.splitlines(keepends=True)
    start = None
    for i, line in enumerate(lines):
        if line.startswith("def {}(".format(name)):
            start = i
            break
    if start is None:
        raise AssertionError("missing " + name)
    end = start + 1
    while end < len(lines):
        line = lines[end]
        if line.startswith("def ") or line.startswith("class "):
            break
        if line.strip() and not line.startswith((" ", "\t", "#")):
            break
        end += 1
    return "".join(lines[start:end]).rstrip() + "\n"


def load_pure():
    src = IMPORTER.read_text(encoding="utf-8")
    ns = {"__name__": "import_art_batch_pure"}
    exec(compile(src[:src.index(MARKER)], str(IMPORTER), "exec"), ns)
    return ns


def oracle(man):
    """The behaviour the four batches had, plus a hint step for every other mesh."""
    batch = str(man.get("batch"))
    typ = man.get("type")
    if batch in OLD_PROP:
        return "prop" if typ == "static_mesh" else None
    if batch in OLD_HAZARD:
        return "hazard" if typ == "static_mesh" else None
    if man.get("unreal_material_hint") and typ in ("static_mesh", "skeletal_mesh"):
        return "hint"
    return None


class MaterialHintTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = IMPORTER.read_text(encoding="utf-8")
        cls.pure = load_pure()

    def test_dry_run_self_check_passes(self):
        self.assertEqual(self.pure["dry_run_self_check"](write=lambda msg: None), 0)

    def test_subprocess_dry_run_exits_before_unreal(self):
        env = os.environ.copy()
        env["GFART_MATERIAL_DRY_RUN"] = "1"
        proc = subprocess.run(
            [sys.executable, str(IMPORTER)],
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("material dry-run: ok", proc.stdout)
        self.assertNotIn("ModuleNotFoundError", proc.stderr)

    def test_tuple_assignment_is_gone(self):
        self.assertNotIn("PROP_MAT_BATCHES =", self.src)
        self.assertIn("def material_kind(", self.src)
        self.assertIn("def hint_materials(", self.src)

    def test_editor_functions_are_byte_identical(self):
        for name, digest in UNCHANGED.items():
            body = function_source(self.src, name)
            got = hashlib.sha256(body.encode()).hexdigest()
            self.assertEqual(got, digest, name)

    def test_matrix_matches_the_old_gate_plus_hints(self):
        kind = self.pure["material_kind"]
        batches = ("1", "2", "3b", "7", "8", "worldmap_v1", "beat_hop_v1", "beat_hop_v2",
                   "hazards1", "big_grumble_v1", "newdinos_v1", "ferry_v2", "rex_v2")
        for batch in batches:
            for typ in ("static_mesh", "skeletal_mesh", "texture_set"):
                for hinted in (False, True):
                    man = {
                        "asset_name": "SM_Sample",
                        "type": typ,
                        "batch": batch,
                        "unreal_target": "/Game/Art/Sample/SM_Sample",
                        "material_slots": ["Main", "Body"],
                        "files": {"textures": ["T_Sample_D.png"]},
                    }
                    if hinted:
                        man["unreal_material_hint"] = {"Main": {"texture": "T_Sample_D"}}
                    self.assertEqual(kind(man), oracle(man), "{} {} hinted={}".format(batch, typ, hinted))

    def test_water_plan_matches_the_prop_step(self):
        man = {
            "asset_name": "SM_Block_Water",
            "type": "static_mesh",
            "batch": "3b",
            "unreal_target": "/Game/Art/Blocks/SM_Block_Water",
            "material_slots": ["All"],
            "files": {"textures": ["T_Block_Water.png", "T_Block_Water_M.png", "T_Block_Water_E.png"]},
            "unreal_material_hint": {"blend_mode": "translucent", "opacity": 0.55, "panner_speed_uv_per_s": [0.02, 0.01]},
        }
        self.assertEqual(self.pure["material_kind"](man), "prop")
        plan = self.pure["prop_material_plan"](man)
        self.assertEqual(plan["parent"], "M_GF_Water")
        self.assertEqual(plan["textures"], {"Diffuse": "T_Block_Water"})
        self.assertEqual(plan["slots"], {"All": "MI_Block_Water"})
        self.assertEqual(plan["path"], "/Game/Art/Blocks/Materials/MI_Block_Water")
        self.assertEqual(plan["scalars"]["Opacity"], 0.55)
        self.assertEqual(plan["scalars"]["PanSpeedU"], 0.02)
        self.assertEqual(plan["scalars"]["PanSpeedV"], 0.01)
        self.assertNotIn("DyeMask", plan["textures"])
        self.assertNotIn("EmissiveStrength", plan["scalars"])

    def test_plain_prop_skips_icons_and_tints_the_team_slot(self):
        man = {
            "asset_name": "SM_Colo_Torch",
            "type": "static_mesh",
            "batch": "7",
            "unreal_target": "/Game/Art/Colo/SM_Colo_Torch",
            "material_slots": ["Main", "TeamTint"],
            "files": {"textures": ["T_Icon_Torch.png", "T_Colo_Torch_D.png", "T_Colo_Torch_E.png"]},
        }
        plan = self.pure["prop_material_plan"](man)
        self.assertEqual(plan["textures"], {"Diffuse": "T_Colo_Torch_D", "Emissive": "T_Colo_Torch_E"})
        self.assertEqual(plan["scalars"], {"EmissiveStrength": 2.0})
        self.assertEqual(plan["slots"]["TeamTint"], "MI_GF_TeamTint")
        self.assertEqual(plan["owned_mi_paths"], ["/Game/Art/Colo/Materials/MI_Colo_Torch"])

    def test_banner_and_glass_paths_stay_on_the_prop_step(self):
        banner = {
            "asset_name": "SM_Colo_Banner",
            "type": "static_mesh",
            "batch": "7",
            "unreal_target": "/Game/Art/Colo/SM_Colo_Banner",
            "material_slots": ["Main", "Pattern"],
            "files": {"textures": ["T_Colo_Banner_D.png"]},
            "unreal_material_hint": {
                "Main": {"texture": "T_Colo_Banner_D", "defaults": {"LiningColor": "#D88C68"}},
                "Pattern": {},
            },
        }
        plan = self.pure["prop_material_plan"](banner)
        root = "/Game/Art/Colo/Materials/MI_Colo_Banner"
        self.assertEqual(plan["owned_mi_paths"], [
            root,
            root + "_Pattern_Plain",
            root + "_Pattern_Claw",
            root + "_Pattern_Egg",
            root + "_Pattern_Gem",
        ])
        self.assertEqual(plan["slots"]["Pattern"], "MI_Colo_Banner_Pattern_Plain")
        self.assertEqual(plan["vectors"]["Dye_R"], "#D88C68")
        self.assertEqual(self.pure["material_kind"](banner), "prop")
        case = {
            "asset_name": "SM_Deco_TrophyCase",
            "type": "static_mesh",
            "batch": "7",
            "unreal_target": "/Game/Art/Furniture/SM_Deco_TrophyCase",
            "material_slots": ["Main", "Glass"],
            "files": {"textures": ["T_Deco_TrophyCase_D.png"]},
            "unreal_material_hint": {"Glass": {"opacity": 0.3, "blend_mode": "translucent"}},
        }
        glass = self.pure["prop_material_plan"](case)
        self.assertEqual(glass["parent"], "M_GF_Prop")
        self.assertEqual(glass["slots"]["Glass"], "MI_Deco_TrophyCase_Glass")
        self.assertEqual(glass["v2"]["Glass"]["opacity"], 0.3)

    def test_worldmap_and_beat_hop_stay_prop_even_with_a_hint(self):
        pad = {
            "asset_name": "SM_GatePad",
            "type": "static_mesh",
            "batch": "worldmap_v1",
            "unreal_target": "/Game/Art/Gates/SM_GatePad",
            "material_slots": ["Main"],
            "files": {"textures": ["T_GatePad_Note.png", "T_GatePad_D.png", "T_GatePad_M.png"]},
            "unreal_material_hint": {"Main": {"texture": "T_GatePad_D", "mask": "T_GatePad_M"}},
        }
        plan = self.pure["prop_material_plan"](pad)
        self.assertEqual(self.pure["material_kind"](pad), "prop")
        self.assertEqual(plan["textures"]["Diffuse"], "T_GatePad_D")
        self.assertEqual(plan["textures"]["DyeMask"], "T_GatePad_M")
        brick = {
            "asset_name": "SM_Block_Beat_L1",
            "type": "static_mesh",
            "batch": "beat_hop_v1",
            "unreal_target": "/Game/Art/BeatHop/Bricks/SM_Block_Beat_L1",
            "material_slots": ["Main"],
            "files": {"textures": ["T_Block_Beat_L1_D.png"]},
        }
        self.assertEqual(self.pure["material_kind"](brick), "prop")
        self.assertEqual(self.pure["prop_material_plan"](brick)["mi"], "MI_Block_Beat_L1")
        hinted_player = {
            "asset_name": "SK_Player_Base",
            "type": "skeletal_mesh",
            "batch": "beat_hop_v1",
            "unreal_target": "/Game/Art/Characters/Player/SK_Player_Base",
            "material_slots": ["Body"],
            "files": {"textures": []},
            "unreal_material_hint": {"Body": {"parent": "M_GF_DyeMask"}},
        }
        self.assertIsNone(self.pure["material_kind"](hinted_player))

    def test_generic_hint_builds_dye_mask_and_prop_instances(self):
        grumble = {
            "asset_name": "SK_Dino_BigGrumble",
            "type": "skeletal_mesh",
            "batch": "big_grumble_v4",
            "unreal_target": "/Game/Art/Dinos/BigGrumble/SK_Dino_BigGrumble",
            "material_slots": ["Body"],
            "files": {"textures": ["T_Dino_BigGrumble_D.png", "T_Dino_BigGrumble_M.png"]},
            "unreal_material_hint": {
                "slot": "Body",
                "parent": "M_GF_DyeMask",
                "instance": "/Game/Art/Dinos/BigGrumble/Materials/MI_Dino_BigGrumble",
                "Diffuse": "T_Dino_BigGrumble_D",
                "Mask": "T_Dino_BigGrumble_M",
            },
        }
        plan = self.pure["hint_material_plan"](grumble)
        inst = plan["instances"][0]
        self.assertEqual(self.pure["material_kind"](grumble), "hint")
        self.assertEqual(inst["parent"], "M_GF_DyeMask")
        self.assertEqual(inst["textures"], {"Diffuse": "T_Dino_BigGrumble_D", "Mask": "T_Dino_BigGrumble_M"})
        self.assertEqual(plan["slots"], {"Body": "MI_Dino_BigGrumble"})
        self.assertNotIn("DyeMask", inst["textures"])
        ferry = {
            "asset_name": "SM_FerryBasket",
            "type": "static_mesh",
            "batch": "ferry_v2",
            "unreal_target": "/Game/Art/Props/Ferry/SM_FerryBasket",
            "material_slots": ["Main", "TeamTint"],
            "files": {"textures": ["T_FerryBasket_D.png", "T_FerryBasket_M.png", "T_FerryBasket_E.png"]},
            "unreal_material_hint": {"Main": {"emissive_color": "D * E * 1.5", "Dye_R": "#FFC93C"}},
        }
        got = self.pure["hint_material_plan"](ferry)
        self.assertEqual(got["slots"], {"Main": "MI_FerryBasket"})
        self.assertNotIn("TeamTint", got["slots"])
        fi = got["instances"][0]
        self.assertEqual(fi["parent"], "M_GF_Prop")
        self.assertEqual(fi["textures"]["DyeMask"], "T_FerryBasket_M")
        self.assertEqual(fi["scalars"]["EmissiveStrength"], 1.5)
        self.assertEqual(fi["vectors"]["Dye_R"], "#FFC93C")
        self.assertEqual(self.pure["hint_owned_paths"](ferry), ["/Game/Art/Props/Ferry/Materials/MI_FerryBasket"])
        quiet = {
            "asset_name": "SK_Dino_Raptor",
            "type": "skeletal_mesh",
            "batch": "newdinos_v1",
            "unreal_target": "/Game/Art/Dinos/Raptor/SK_Dino_Raptor",
            "material_slots": ["Body"],
            "files": {"textures": ["T_Dino_Raptor_D.png"]},
        }
        self.assertIsNone(self.pure["material_kind"](quiet))
        pool = {
            "asset_name": "SM_Block_Pool",
            "type": "static_mesh",
            "batch": "pools_v1",
            "unreal_target": "/Game/Art/Blocks/SM_Block_Pool",
            "material_slots": ["Main", "All"],
            "files": {"textures": ["T_Block_Pool.png"]},
            "unreal_material_hint": {"blend_mode": "translucent", "opacity": 0.4, "panner_speed_uv_per_s": [0.1, 0.0]},
        }
        water = self.pure["hint_material_plan"](pool)
        self.assertEqual(self.pure["material_kind"](pool), "hint")
        self.assertEqual(water["slots"], {"Main": "MI_Block_Pool", "All": "MI_Block_Pool"})
        self.assertEqual(len(water["instances"]), 1)
        self.assertEqual(water["instances"][0]["parent"], "M_GF_Water")
        self.assertEqual(water["instances"][0]["scalars"]["Opacity"], 0.4)
        self.assertEqual(water["instances"][0]["textures"], {"Diffuse": "T_Block_Pool"})

    def test_post_import_prefix_still_defines_the_editor_helpers(self):
        # import_worldmap_v1_post.py execs this file up to "# Clean re-import".
        import types
        unreal = types.ModuleType("unreal")

        class Paths:
            @staticmethod
            def convert_relative_path_to_full(p):
                return "/tmp/gfart-prefix"

            @staticmethod
            def project_dir():
                return "/tmp/gfart-prefix/"

        class Helpers:
            @staticmethod
            def get_asset_tools():
                return object()

        class SystemLibrary:
            @staticmethod
            def execute_console_command(*_args):
                raise RuntimeError("cvar")

        unreal.Paths = Paths
        unreal.AssetToolsHelpers = Helpers
        unreal.EditorAssetLibrary = object()
        unreal.SystemLibrary = SystemLibrary
        unreal.log = lambda *_a, **_k: None
        sys.modules["unreal"] = unreal
        saved = {key: os.environ.get(key) for key in ("GFART_ONLY", "GFART_BATCH", "GFART_PROBE", "GFART_MATERIAL_DRY_RUN")}
        os.environ["GFART_ONLY"] = ""
        os.environ["GFART_BATCH"] = ""
        os.environ.pop("GFART_PROBE", None)
        os.environ.pop("GFART_MATERIAL_DRY_RUN", None)
        prefix = self.src[:self.src.index("# Clean re-import")]
        ns = {"__name__": "import_worldmap_v1_post"}
        try:
            exec(compile(prefix, str(IMPORTER), "exec"), ns)
        finally:
            for key, val in saved.items():
                if val is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = val
            sys.modules.pop("unreal", None)
        for name in ("run_import", "prop_materials", "extra_slot_materials", "hazard_materials", "hint_materials", "material_kind", "vec", "log"):
            self.assertIn(name, ns)

    def test_srgb_crest_matches_the_ferry_linear_colour(self):
        r, g, b, a = self.pure["srgb_hex_to_linear"]("#FFC93C")
        self.assertAlmostEqual(r, 1.0, places=6)
        self.assertAlmostEqual(g, 0.584078, places=4)
        self.assertAlmostEqual(b, 0.045186, places=4)
        self.assertEqual(a, 1.0)


if __name__ == "__main__":
    unittest.main()
