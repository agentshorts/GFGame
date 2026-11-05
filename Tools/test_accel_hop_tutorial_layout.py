"""Editor-free checks for the Acceleration Hop tutorial map (issue 348): the course plan and the map script.

A stub `unreal` module stands in for the editor. Run from the repo root:

  python3 -m unittest Tools/test_accel_hop_tutorial_layout.py
"""
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
import accel_hop_tutorial_layout as layout  # noqa: E402

RULES_H = ROOT / "Source" / "GFRhythm" / "Public" / "GFAccelHopTutorialRules.h"
RULES_CPP = ROOT / "Source" / "GFRhythm" / "Private" / "GFAccelHopTutorialRules.cpp"
CATALOG_H = ROOT / "Source" / "GFCore" / "Public" / "GFMiniGameCatalog.h"
GATE_H = ROOT / "Source" / "GFRhythm" / "Public" / "GFAccelHopTutorialGateSubsystem.h"
TILE_SIZE_Y = layout.TILE_SIZE_CM[1]


class PlanTests(unittest.TestCase):
    def setUp(self):
        self.plan = layout.plan()
        self.tiles = self.plan["tiles"]

    def test_lessons_in_teaching_order(self):
        self.assertEqual([l["lesson"] for l in self.plan["lessons"]], ["Beat", "Steer", "Lit", "Chain"])
        starts = [l["start_x"] for l in self.plan["lessons"]]
        self.assertEqual(starts, sorted(starts))
        for a, b in zip(self.plan["lessons"], self.plan["lessons"][1:]):
            self.assertGreater(b["start_x"] - a["end_x"], layout.ZIG_PITCH_CM, "a walk between lessons")

    def test_tiles_run_forward_in_lesson_order(self):
        xs = [t["location"][0] for t in self.tiles]
        self.assertEqual(xs, sorted(xs))
        self.assertEqual(len(set(xs)), len(xs), "no two tiles at one spot")
        order = [layout.LESSONS.index(t["lesson"]) for t in self.tiles]
        self.assertEqual(order, sorted(order))
        self.assertEqual([t["slot"] for t in self.tiles], list(range(len(self.tiles))))

    def test_strip_tiles_do_not_overlap(self):
        for a, b in zip(self.tiles, self.tiles[1:]):
            if a["location"][1] == b["location"][1]:
                self.assertGreaterEqual(b["location"][0] - a["location"][0], layout.TILE_SIZE_CM[0] - 1e-6)

    def test_every_tile_is_tagged_with_its_lesson(self):
        for t in self.tiles:
            self.assertIn(layout.TAG, t["tags"])
            self.assertIn(layout.lesson_tag(t["lesson"]), t["tags"])
            self.assertIn(t["kind"], layout.KINDS)
            self.assertEqual(t["mesh"], layout.mesh_path(t["kind"]))

    def test_first_two_lessons_never_light(self):
        for t in self.tiles:
            self.assertEqual(t["can_be_lit"], t["lesson"] in ("Lit", "Chain"), t["label"])

    def test_beat_lesson_has_no_boost(self):
        kinds = {t["kind"] for t in self.tiles if t["lesson"] == "Beat"}
        self.assertNotIn("Boost", kinds)
        self.assertIn("Downbeat", kinds)

    def test_steer_lesson_zigzags_outside_the_dead_zone(self):
        ys = [t["location"][1] for t in self.tiles if t["lesson"] == "Steer"]
        self.assertGreaterEqual(len(ys), 4)
        for a, b in zip(ys, ys[1:]):
            self.assertLess(a * b, 0, "alternates sides")
        for y in ys:
            self.assertGreater(abs(y), 60.0, "outside SideDeadZoneCm")
            self.assertGreater(abs(y) - TILE_SIZE_Y / 2.0, 0.0, "the tile does not cross the centre line")
        xs = [t["location"][0] for t in self.tiles if t["lesson"] == "Steer"]
        for a, b in zip(xs, xs[1:]):
            self.assertAlmostEqual(b - a, layout.ZIG_PITCH_CM)

    def test_chain_lesson_has_zigzag_and_strip(self):
        chain = [t for t in self.tiles if t["lesson"] == "Chain"]
        self.assertTrue(any(t["shape"] == "zig" for t in chain))
        self.assertTrue(any(t["shape"] == "strip" for t in chain))
        self.assertGreaterEqual(len(chain), 6, "room for a chain of 6")

    def test_lesson_goals_fit_their_tiles(self):
        goals = {"Beat": 4, "Steer": 3, "Lit": 2, "Chain": 6}
        per = {l["lesson"]: l["tiles"] for l in self.plan["lessons"]}
        self.assertGreaterEqual(per["Beat"], goals["Beat"] * 2)
        self.assertGreaterEqual(per["Steer"], goals["Steer"] + 2)
        self.assertGreaterEqual(per["Chain"], goals["Chain"] + 4)

    def test_one_road_box_covers_every_tile(self):
        road = self.plan["road"]
        hx, hy, hz = road["half_extent"]
        for t in self.tiles:
            x, y, z = t["location"]
            self.assertLessEqual(abs(layout.along(road, t["location"])), hx - layout.TILE_SIZE_CM[0] / 2.0)
            self.assertLessEqual(abs(y - road["centre"][1]) + TILE_SIZE_Y / 2.0, hy)
            self.assertLessEqual(abs(z - road["centre"][2]), hz)

    def test_floor_under_start_course_and_finish(self):
        f = self.plan["floor"]
        lo = f["centre"][0] - f["size"][0] / 2.0
        hi = f["centre"][0] + f["size"][0] / 2.0
        self.assertAlmostEqual(f["centre"][2] + f["size"][2] / 2.0, 0.0, msg="floor top at Z 0")
        for x in [self.plan["player_start"][0], -500.0, self.plan["finish_gate"][0], self.tiles[-1]["location"][0]]:
            self.assertTrue(lo < x < hi, x)
        self.assertGreaterEqual(f["size"][1] / 2.0, layout.ZIG_SIDE_CM + TILE_SIZE_Y / 2.0)
        self.assertEqual(f["scale"], [round(v / 100.0, 4) for v in f["size"]])

    def test_finish_gate_after_the_last_tile(self):
        self.assertGreater(self.plan["finish_gate"][0], self.tiles[-1]["location"][0] + layout.TILE_SIZE_CM[0])

    def test_start_area_clear_of_tiles(self):
        # Marker at (-500, 0), start HUB gate at (-500, 400), player start at the origin: no tile there.
        self.assertGreater(self.tiles[0]["location"][0] - layout.TILE_SIZE_CM[0] / 2.0, self.plan["player_start"][0] + 200.0)

    def test_kind_for_rejects_bad_slots(self):
        with self.assertRaises(ValueError):
            layout.kind_for("strip", 5, 5)
        with self.assertRaises(ValueError):
            layout.kind_for("loop", 0, 3)
        with self.assertRaises(ValueError):
            layout.lesson_tag("Dance")


class CppAgreementTests(unittest.TestCase):
    """The plan and the C++ name the same lessons, tags and map."""

    def test_lesson_ids_and_order_match_the_rules(self):
        src = RULES_CPP.read_text(encoding="utf-8")
        ids = re.findall(r'MakeLesson\(EGFAccelHopLesson::(\w+), TEXT\("(\w+)"\)', src)
        self.assertEqual([i for _, i in ids], list(layout.LESSONS))
        self.assertEqual([e for e, _ in ids], list(layout.LESSONS))

    def test_tag_prefix_matches(self):
        src = RULES_CPP.read_text(encoding="utf-8")
        self.assertIn('TEXT("{}.Lesson.")'.format(layout.TAG), src)


class MapNameTests(unittest.TestCase):
    MAP = "/Game/Maps/L_AccelHop_Tutorial_v1"
    # The live gate and the cook list point at the visual pass copy (#348); the script still builds v1 by default.
    LIVE_MAP = "/Game/Maps/L_AccelHop_Tutorial_v2"

    def test_catalog_gate_and_script_agree(self):
        self.assertIn('"{}"'.format(self.LIVE_MAP), CATALOG_H.read_text(encoding="utf-8"))
        self.assertIn('"{}"'.format(self.LIVE_MAP), GATE_H.read_text(encoding="utf-8"))
        self.assertIn('"{}"'.format(self.MAP), (TOOLS / "create_accel_hop_tutorial_map.py").read_text(encoding="utf-8"))
        ini = (ROOT / "Config" / "DefaultGame.ini").read_text(encoding="utf-8")
        self.assertIn('+MapsToCook=(FilePath="{}")'.format(self.LIVE_MAP), ini)
        self.assertIn('+MapsToCook=(FilePath="/Game/Maps/L_Beat")', ini, "L_Beat still cooks")

    def test_no_em_dashes_in_copy(self):
        for p in (RULES_H, RULES_CPP, TOOLS / "accel_hop_tutorial_layout.py", TOOLS / "create_accel_hop_tutorial_map.py"):
            self.assertNotIn("\u2014", p.read_text(encoding="utf-8"), str(p))


# ---- the editor script, against a stub unreal ----

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


class _Class:
    def __init__(self, name):
        self.name = name

    def get_name(self):
        return self.name


class _Actor:
    def __init__(self, cls, loc, rot):
        self.cls, self.loc, self.rot = cls, loc, rot
        self.tags, self.label, self.folder, self.kind, self.extent, self.lit = [], None, None, None, None, None
        self.props = {"mesh": _Prop(static_mesh=None)}
        self.static_mesh_component = types.SimpleNamespace(set_static_mesh=lambda m: self.props.__setitem__("cube", m))
        self.scale = None

    def get_class(self):
        return _Class(self.cls)

    def set_kind(self, kind):
        self.kind = kind
        self.props["mesh"].static_mesh = _Mesh(layout.mesh_path(kind.title()))

    def set_can_be_lit(self, on):
        self.lit = on

    def set_road_extent(self, v):
        self.extent = v

    def set_actor_label(self, s):
        self.label = s

    def set_folder_path(self, s):
        self.folder = s

    def set_actor_scale3d(self, v):
        self.scale = v

    def set_actor_location(self, v, *_):
        self.loc = v

    def set_actor_rotation(self, r, *_):
        self.rot = r

    def get_editor_property(self, name):
        return self.props[name]

    def set_editor_property(self, name, value):
        self.props[name] = value


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


def _stub_unreal(tmp, existing_assets, subsystem, duplicated):
    u = types.ModuleType("unreal")
    u.Paths = types.SimpleNamespace(convert_relative_path_to_full=lambda p: tmp, project_dir=lambda: tmp)

    def dup(src, dst):
        duplicated.append((src, dst))
        existing_assets.add(dst)
        return object()

    u.EditorAssetLibrary = types.SimpleNamespace(does_asset_exist=lambda p: p in existing_assets, duplicate_asset=dup,
                                                 load_asset=lambda p: "cube:" + p)
    u.Name = _Name
    u.Vector = lambda *a: tuple(a)
    u.Rotator = lambda roll, pitch, yaw: (roll, pitch, yaw)
    u.EditorActorSubsystem = "EditorActorSubsystem"
    u.LevelEditorSubsystem = "LevelEditorSubsystem"
    u.UnrealEditorSubsystem = "UnrealEditorSubsystem"
    u.get_editor_subsystem = lambda cls: subsystem
    for name in ("GFAccelHopTile", "GFAccelHopRoad", "GFAccelHopTutorial", "StaticMeshActor", "PlayerStart",
                 "DirectionalLight", "SkyLight", "SkyAtmosphere"):
        setattr(u, name, name)
    u.GFHopTileKind = types.SimpleNamespace(**{k.upper(): k.upper() for k in layout.KINDS})
    u.log = lambda *_a, **_k: None
    return u


ALL_MESHES = {layout.mesh_path(k).rsplit(".", 1)[0] for k in layout.KINDS}


class MapScriptTests(unittest.TestCase):
    def load(self, assets, subsystem):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.duplicated = []
        patcher = mock.patch.dict(sys.modules, {"unreal": _stub_unreal(self.tmp.name, set(assets), subsystem, self.duplicated)})
        patcher.start()
        self.addCleanup(patcher.stop)
        env = mock.patch.dict(os.environ, {"GFTUT_LIB": "1"})
        env.start()
        self.addCleanup(env.stop)
        sys.modules.pop("create_accel_hop_tutorial_map", None)
        self.addCleanup(sys.modules.pop, "create_accel_hop_tutorial_map", None)
        return importlib.import_module("create_accel_hop_tutorial_map")

    def test_library_import_does_not_touch_anything(self):
        sub = _ActorSubsystem()
        self.load(ALL_MESHES, sub)
        self.assertEqual(sub.actors, [])
        self.assertEqual(self.duplicated, [])
        self.assertFalse(os.path.exists(os.path.join(self.tmp.name, "Saved")))

    def test_never_targets_l_beat(self):
        mod = self.load(ALL_MESHES, _ActorSubsystem())
        for bad in ("/Game/Maps/L_Beat", "/Game/Maps/L_Hub", "/Game/Maps/L_AccelHop_Tutorial"):
            with self.assertRaises(RuntimeError):
                mod.check_target("/Game/Maps/L_Beat", bad)
        mod.check_target("/Game/Maps/L_Beat", "/Game/Maps/L_AccelHop_Tutorial_v2")
        self.assertEqual(mod.MAP, "/Game/Maps/L_AccelHop_Tutorial_v1")
        self.assertEqual(mod.SOURCE, "/Game/Maps/L_Beat")

    def test_copies_once(self):
        mod = self.load(ALL_MESHES | {"/Game/Maps/L_Beat"}, _ActorSubsystem())
        self.assertEqual(mod.ensure_copy(mod.SOURCE, mod.MAP), "created")
        self.assertEqual(mod.ensure_copy(mod.SOURCE, mod.MAP), "exists")
        self.assertEqual(self.duplicated, [("/Game/Maps/L_Beat", "/Game/Maps/L_AccelHop_Tutorial_v1")])

    def test_copy_needs_the_source(self):
        mod = self.load(ALL_MESHES, _ActorSubsystem())
        with self.assertRaisesRegex(RuntimeError, "no source map"):
            mod.ensure_copy(mod.SOURCE, mod.MAP)

    def test_package_file_and_hash(self):
        mod = self.load(ALL_MESHES, _ActorSubsystem())
        path = mod.package_file("/Game/Maps/L_Beat")
        self.assertTrue(path.endswith(os.path.join("Content", "Maps", "L_Beat.umap")))
        self.assertIsNone(mod.file_hash(path))
        os.makedirs(os.path.dirname(path))
        with open(path, "wb") as f:
            f.write(b"pointer")
        self.assertEqual(mod.file_hash(path), mod.file_hash(path))

    def test_refuses_without_imported_meshes(self):
        sub = _ActorSubsystem()
        mod = self.load(ALL_MESHES - {layout.mesh_path("Landing").rsplit(".", 1)[0]}, sub)
        with self.assertRaisesRegex(RuntimeError, "Landing"):
            mod.place_in_current_level()
        self.assertEqual(sub.actors, [])

    def test_places_the_course(self):
        course = _Actor("GFBeatHopCourse", (0, 0, 0), (0, 0, 0))
        start = _Actor("PlayerStart", (5, 5, 5), (0, 0, 90))
        sub = _ActorSubsystem([course, start])
        mod = self.load(ALL_MESHES, sub)
        rep = mod.place_in_current_level()
        plan = layout.plan()
        self.assertTrue(rep["ok"], rep)
        self.assertIn(course, sub.destroyed, "the Beat Hop course does not belong in the tutorial")
        self.assertEqual(start.loc, tuple(plan["player_start"]))
        tiles = [a for a in sub.actors if a.cls == "GFAccelHopTile"]
        self.assertEqual(len(tiles), len(plan["tiles"]))
        for a, t in zip(tiles, plan["tiles"]):
            self.assertEqual(a.loc, tuple(t["location"]))
            self.assertEqual(a.lit, t["can_be_lit"])
            self.assertEqual(list(a.tags), t["tags"])
            self.assertTrue(a.folder.startswith("AccelHopTutorial/Tiles/"))
        roads = [a for a in sub.actors if a.cls == "GFAccelHopRoad"]
        self.assertEqual(len(roads), 1, "one song, one beat clock")
        self.assertEqual(roads[0].extent, tuple(plan["road"]["half_extent"]))
        directors = [a for a in sub.actors if a.cls == "GFAccelHopTutorial"]
        self.assertEqual(len(directors), 1)
        self.assertTrue(directors[0].props["has_finish_gate"])
        self.assertEqual(directors[0].props["finish_gate_location"], tuple(plan["finish_gate"]))
        floors = [a for a in sub.actors if a.cls == "StaticMeshActor"]
        self.assertEqual(len(floors), 1)
        self.assertEqual(floors[0].scale, tuple(plan["floor"]["scale"]))
        self.assertEqual(sorted(rep["lights_added"]), ["DirectionalLight", "SkyAtmosphere", "SkyLight"])

    def test_rerun_replaces_instead_of_stacking(self):
        sub = _ActorSubsystem()
        mod = self.load(ALL_MESHES, sub)
        mod.place_in_current_level()
        first = len(sub.actors)
        rep = mod.place_in_current_level()
        self.assertEqual(len(sub.actors), first)
        self.assertEqual(rep["removed"], first - 1, "everything but the added player start is tagged")
        self.assertEqual(len([a for a in sub.actors if a.cls == "GFAccelHopTutorial"]), 1)


if __name__ == "__main__":
    unittest.main()
