"""Editor-free checks for the Acceleration Hop editor scripts (issue 334).

A stub `unreal` module stands in for the editor. Run from the repo root:

  python3 -m unittest Tools/test_place_acceleration_hop.py
  GFTOWN_KIT=<towns_v2 kit> python3 -m unittest Tools/test_place_acceleration_hop.py   # also places the real road

Without a towns_v2 kit (GFTOWN_KIT or handoff/inbox/towns_v2) the placement tests that need the road skip.
"""
import ast
import importlib
import os
import pathlib
import re
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS = ROOT / "Tools"
sys.path.insert(0, str(TOOLS))
import accel_hop_layout as layout  # noqa: E402


def _kit():
    kit = os.environ.get("GFTOWN_KIT", "").strip() or str(ROOT / "handoff" / "inbox" / "towns_v2")
    world = layout.default_layouts(kit)[layout.WORLD_LAYOUT]
    return kit if os.path.isfile(world) else None


class _Name(str):
    pass


class _Prop:
    def __init__(self, **kw):
        self.__dict__.update(kw)

    def get_editor_property(self, name):
        return self.__dict__[name]


class _Mesh:
    def __init__(self, path):
        self.path = path

    def get_path_name(self):
        return self.path


class _Actor:
    def __init__(self, cls, loc, rot):
        self.cls, self.loc, self.rot = cls, loc, rot
        self.tags, self.label, self.folder, self.kind, self.extent = [], None, None, None, None
        self.mesh = _Prop(static_mesh=None)

    def set_kind(self, kind):
        self.kind = kind
        self.mesh.static_mesh = _Mesh(layout.mesh_path(kind.title()))

    def set_road_extent(self, v):
        self.extent = v

    def set_actor_label(self, s):
        self.label = s

    def set_folder_path(self, s):
        self.folder = s

    def get_editor_property(self, name):
        return {"mesh": self.mesh}[name]


class _ActorSubsystem:
    def __init__(self, existing=()):
        self.actors = list(existing)
        self.destroyed = []

    def get_all_level_actors(self):
        return list(self.actors)

    def destroy_actors(self, actors):
        self.destroyed.extend(actors)
        self.actors = [a for a in self.actors if a not in actors]

    def spawn_actor_from_class(self, cls, loc, rot):
        a = _Actor(cls, loc, rot)
        self.actors.append(a)
        return a


def _stub_unreal(tmp, existing_assets, subsystem):
    u = types.ModuleType("unreal")
    u.Paths = types.SimpleNamespace(convert_relative_path_to_full=lambda p: tmp, project_dir=lambda: tmp)
    u.EditorAssetLibrary = types.SimpleNamespace(does_asset_exist=lambda p: p in existing_assets)
    u.Name = _Name
    u.Vector = lambda *a: tuple(a)
    u.Rotator = lambda roll, pitch, yaw: (roll, pitch, yaw)
    u.EditorActorSubsystem = "EditorActorSubsystem"
    u.LevelEditorSubsystem = "LevelEditorSubsystem"
    u.get_editor_subsystem = lambda cls: subsystem
    u.GFAccelHopTile = "GFAccelHopTile"
    u.GFAccelHopRoad = "GFAccelHopRoad"
    u.GFHopTileKind = types.SimpleNamespace(**{k.upper(): k.upper() for k in layout.KINDS})
    u.log = lambda *_a, **_k: None
    return u


ALL_MESHES = {layout.mesh_path(k).rsplit(".", 1)[0] for k in layout.KINDS}


class PlaceAccelHopTests(unittest.TestCase):
    def load(self, assets, subsystem):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        patcher = mock.patch.dict(sys.modules, {"unreal": _stub_unreal(self.tmp.name, assets, subsystem)})
        patcher.start()
        self.addCleanup(patcher.stop)
        env = mock.patch.dict(os.environ, {"GFHOP_LIB": "1"})
        env.start()
        self.addCleanup(env.stop)
        sys.modules.pop("place_acceleration_hop", None)
        self.addCleanup(sys.modules.pop, "place_acceleration_hop", None)
        return importlib.import_module("place_acceleration_hop")

    def test_library_import_does_not_touch_the_level(self):
        sub = _ActorSubsystem()
        self.load(ALL_MESHES, sub)
        self.assertEqual(sub.actors, [])
        self.assertFalse(os.path.exists(os.path.join(self.tmp.name, "Saved")))

    def test_refuses_without_imported_meshes(self):
        sub = _ActorSubsystem()
        mod = self.load(ALL_MESHES - {layout.mesh_path("Boost").rsplit(".", 1)[0]}, sub)
        with self.assertRaisesRegex(RuntimeError, "Boost"):
            mod.place_in_current_level("/nonexistent-kit")
        self.assertEqual(sub.actors, [])

    @unittest.skipUnless(_kit(), "no towns_v2 kit")
    def test_places_the_plan_and_one_road_box(self):
        old = _Actor("GFAccelHopTile", (0, 0, 0), (0, 0, 0))
        old.tags = [_Name("GFAccelHop")]
        keep = _Actor("StaticMeshActor", (0, 0, 0), (0, 0, 0))
        keep.tags = [_Name("GFFerryStop")]
        sub = _ActorSubsystem([old, keep])
        mod = self.load(ALL_MESHES, sub)
        rep = mod.place_in_current_level(_kit())
        plan = layout.plan_from_kit(_kit())
        self.assertTrue(rep["ok"])
        self.assertEqual(rep["removed"], 1)
        self.assertEqual(sub.destroyed, [old])
        self.assertIn(keep, sub.actors)
        tiles = [a for a in sub.actors if a.cls == "GFAccelHopTile"]
        roads = [a for a in sub.actors if a.cls == "GFAccelHopRoad"]
        self.assertEqual(len(tiles), len(plan["tiles"]))
        self.assertEqual(len(roads), 1)
        for a, t in zip(tiles, plan["tiles"]):
            self.assertEqual(a.kind, t["kind"].upper())
            self.assertEqual(a.loc, tuple(t["location"]))
            self.assertEqual(a.rot, (0.0, 0.0, t["yaw"]))
            self.assertEqual(a.folder, "AccelerationHop/Tiles")
            self.assertIn("GFAccelHop", a.tags)
            self.assertTrue(re.match(r"HopTile_\d{3}_" + t["kind"] + "$", a.label), a.label)
        road = roads[0]
        self.assertEqual(road.loc, tuple(plan["bounds"]["centre"]))
        self.assertEqual(road.extent, tuple(plan["bounds"]["half_extent"]))
        self.assertIn("GFAccelHop", road.tags)
        self.assertEqual(len({a.label for a in tiles}), len(tiles))

    @unittest.skipUnless(_kit(), "no towns_v2 kit")
    def test_rerun_replaces_instead_of_stacking(self):
        sub = _ActorSubsystem()
        mod = self.load(ALL_MESHES, sub)
        first = mod.place_in_current_level(_kit())
        second = mod.place_in_current_level(_kit())
        self.assertEqual(second["removed"], first["placed"] + 1)
        self.assertEqual(len(sub.actors), first["placed"] + 1)


class ImportAccelHopSourceTests(unittest.TestCase):
    """The material graph is HLSL text and parameter tables; check they agree without an editor."""

    @classmethod
    def setUpClass(cls):
        tree = ast.parse((TOOLS / "import_acceleration_hop.py").read_text(encoding="utf-8"))
        ns = {}
        for stmt in tree.body:
            if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name):
                try:
                    ns[stmt.targets[0].id] = ast.literal_eval(stmt.value)
                except ValueError:
                    pass
        cls.ns = ns
        cls.readme_params = ["BaseTint", "RimTint", "GlyphTint", "EmissiveTint", "EmissiveStrength", "BeatPulse", "BeatPulseGain",
                             "CelSteps", "ShadowThreshold", "ShadowTint", "RimLightTint", "RimLightWidth"]

    def test_every_cel_input_is_used_in_the_hlsl(self):
        for name in self.ns["CEL_INPUTS"]:
            self.assertRegex(self.ns["CEL_HLSL"], r"\b" + name + r"\b", name)

    def test_readme_parameters_all_exist(self):
        have = set(self.ns["SCALARS"]) | set(self.ns["VECTORS"])
        self.assertEqual(sorted(set(self.readme_params) - have), [])

    def test_readme_defaults(self):
        s, v = self.ns["SCALARS"], self.ns["VECTORS"]
        self.assertEqual((s["EmissiveStrength"], s["BeatPulseGain"], s["CelSteps"], s["ShadowThreshold"], s["RimLightWidth"]), (2.0, 6.0, 3.0, 0.12, 0.0))
        self.assertEqual(v["ShadowTint"], (0.45, 0.45, 0.55))

    def test_one_tint_per_tile_kind(self):
        self.assertEqual(sorted(self.ns["TINTS"]), sorted(layout.KINDS))

    def test_outline_matches_the_tile_stencil_and_ink(self):
        self.assertEqual(self.ns["OUTLINE_STENCIL"], 3)
        self.assertEqual(self.ns["OUTLINE_INK_SRGB"], (0x2A, 0x24, 0x30))
        cpp = (ROOT / "Source" / "GFRhythm" / "Public" / "GFAccelHop.h").read_text(encoding="utf-8")
        self.assertRegex(cpp, r"OutlineStencil\s*=\s*3\b")
        for name in ("InkColor", "WidthPx", "Stencil", "DepthEdge"):
            self.assertRegex(self.ns["OUTLINE_HLSL"], r"\b" + name + r"\b", name)

    def test_custom_depth_with_stencil_is_enabled(self):
        ini = (ROOT / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
        self.assertRegex(ini, r"(?m)^r\.CustomDepth=3\s*$")


if __name__ == "__main__":
    unittest.main()
