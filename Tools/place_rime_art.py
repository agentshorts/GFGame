"""Places the Rime starter landmarks (batch worldmap_v1) in L_Hub. Run headless after the worldmap_v1 import:
  UnrealEditor-Cmd.exe GFGame.uproject -ExecutePythonScript="<abs>/Tools/place_rime_art.py" -unattended -nullrhi -nosplash
- L_Hub has no PlayerStart, so players spawn at the Rime anchor (0, 0) plus 150 cm per extra player along +X
  (GFGameMode::ChoosePlayerStart and SpawnDefaultPawnAtTransform), facing +X. (0, -300) yaw 36 is the ferry perch, not a start.
  The beacon therefore stands ahead and to the right of the spawn at (700, -600) instead of on it; SM_Rime_Signpost at (250, -450), yaw 0.
- SM_Rime_Lantern every 600 cm along spawn (0, 0) -> ferry stop RimeLanding (1200, 600) and spawn -> the gate ring (+Y),
  set 150 cm beside the walk line; a lantern that would land inside the stop's board radius (400) is skipped.
- SM_Rime_FrostPine x11 (radius 1500-3000, scale 0.8-1.3) and SM_Rime_IceRock x8 (radius 800-2500), seeded scatter.
Kept clear: the spawn, the ferry stop, the lava bridge box (X 1400-6000, |Y| < 700), the DeadlyForest box, every hub
gate and its sign, the walk lines and each other. All actors go in the "Rime" folder with tag GFRime; a re-run deletes
the previous Rime_ actors first, so the layout never doubles. Writes Saved/ArtImport/place_rime_report.json.
"""
import json, math, os, random
import unreal

MAP = "/Game/Maps/L_Hub"
ART = "/Game/Art/Props/Rime/"
SPAWN = (0.0, 0.0)              # no-PlayerStart spawn: Rime anchor + 120 Z
SPAWN_BOX = (-100.0, 550.0, -100.0, 250.0)  # X min/max, Y min/max: up to 4 players 150 cm apart, plus a capsule
STOP = (1200.0, 600.0)          # Config/DefaultGame.ini RimeLanding
BOARD_R = 400.0                 # ferry Tuning BoardRadius
RING_R, GATES = 2000.0, 8       # FGFMiniGameCatalog::GateRingRadiusCm, catalog size
SIGN_RIGHT = 300.0
LAVA = (1400.0, 6000.0, 700.0)  # X min, X max, |Y| half (bridge box 1500-5900 x +-300, plus margin)
FOREST = (-2300.0, 0.0, 800.0, 1600.0)  # centre X, Y, half X, half Y (+100 margin)

def d2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])

def seg_dist(p, a, b):
    ax, ay = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((p[0] - a[0]) * ax + (p[1] - a[1]) * ay) / (ax * ax + ay * ay)))
    return d2(p, (a[0] + t * ax, a[1] + t * ay))

gates, signs = [], []
for i in range(GATES):
    a = math.radians(i * 360.0 / GATES)
    g = (RING_R * math.cos(a), RING_R * math.sin(a))
    yaw = math.atan2(-g[1], -g[0])          # GateFacing: toward the Rime centre
    gates.append(g)
    signs.append((g[0] - math.sin(yaw) * SIGN_RIGHT, g[1] + math.cos(yaw) * SIGN_RIGHT))
GATE_PATH_END = (0.0, RING_R - 400.0)      # spawn -> gates walk line ends short of the +Y gate

def blocked(p, clear):
    in_spawn = SPAWN_BOX[0] - 250 - clear <= p[0] <= SPAWN_BOX[1] + 250 + clear and SPAWN_BOX[2] - 250 - clear <= p[1] <= SPAWN_BOX[3] + 250 + clear
    if in_spawn or d2(p, STOP) < BOARD_R + 100 + clear:
        return "spawn/stop"
    if LAVA[0] - clear <= p[0] <= LAVA[1] + clear and abs(p[1]) <= LAVA[2] + clear:
        return "lava"
    if abs(p[0] - FOREST[0]) <= FOREST[2] + clear and abs(p[1] - FOREST[1]) <= FOREST[3] + clear:
        return "forest"
    if any(d2(p, g) < 450 + clear for g in gates) or any(d2(p, s) < 250 + clear for s in signs):
        return "gate"
    return None

placed = []  # (label, mesh, (x, y), yaw, scale, radius)
def add(label, mesh, p, yaw=0.0, scale=1.0, r=100.0):
    placed.append((label, mesh, p, yaw, scale, r))

add("Rime_Beacon", "SM_Rime_Beacon", (700.0, -600.0), 0.0, 1.0, 200.0)
add("Rime_Signpost", "SM_Rime_Signpost", (250.0, -450.0), 0.0, 1.0, 160.0)

skipped = []
lanterns = []
for name, a, b in (("Ferry", SPAWN, STOP), ("Gates", SPAWN, GATE_PATH_END)):
    L = d2(a, b); ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
    s = 600.0
    while s <= L + 1e-3:
        p = (a[0] + ux * s + uy * 150.0, a[1] + uy * s - ux * 150.0)   # 150 cm to the walk line's right
        in_spawn = SPAWN_BOX[0] - 50 <= p[0] <= SPAWN_BOX[1] + 50 and SPAWN_BOX[2] - 50 <= p[1] <= SPAWN_BOX[3] + 50
        if d2(p, STOP) < BOARD_R or in_spawn:
            skipped.append([name, round(s), "inside the stop/spawn clearance"])
        else:
            lanterns.append(p)
        s += 600.0
for i, p in enumerate(lanterns):
    add("Rime_Lantern_%02d" % i, "SM_Rime_Lantern", p, 0.0, 1.0, 80.0)

paths = [(SPAWN, STOP), (SPAWN, GATE_PATH_END)]
def scatter(prefix, mesh, count, rmin, rmax, smin, smax, foot, rng):
    n, tries = 0, 0
    while n < count and tries < 5000:
        tries += 1
        a = rng.uniform(0, 2 * math.pi); rad = math.sqrt(rng.uniform(rmin * rmin, rmax * rmax))
        p = (rad * math.cos(a), rad * math.sin(a))
        sc = round(rng.uniform(smin, smax), 2)
        r = foot * sc
        if blocked(p, r) or any(seg_dist(p, *pp) < 200 + r for pp in paths):
            continue
        if any(d2(p, q[2]) < r + q[5] + 150 for q in placed):
            continue
        add("%s_%02d" % (prefix, n), mesh, p, round(rng.uniform(0, 360), 1), sc, r)
        n += 1
    return n

rng = random.Random(171)
n_pine = scatter("Rime_FrostPine", "SM_Rime_FrostPine", 11, 1500, 3000, 0.8, 1.3, 90.0, rng)
n_rock = scatter("Rime_IceRock", "SM_Rime_IceRock", 8, 800, 2500, 1.0, 1.0, 90.0, rng)

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)
report = {"map": MAP, "pines": n_pine, "rocks": n_rock, "lanterns": len(lanterns), "lanterns_skipped": skipped, "actors": {}}

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report["loaded"] = bool(les.load_level(MAP))
old = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith("Rime_")]
report["removed_previous"] = len(old)
for a in old:
    eas.destroy_actor(a)
starts = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
report["player_starts"] = [[round(v, 1) for v in (s.get_actor_location().x, s.get_actor_location().y, s.get_actor_location().z)] for s in starts]
others = [a for a in eas.get_all_level_actors() if not isinstance(a, (unreal.WorldSettings,))]
for label, mesh_name, p, yaw, sc, r in placed:
    rr = {"mesh": mesh_name, "loc": [round(p[0], 1), round(p[1], 1)], "yaw": yaw, "scale": sc}
    mesh = unreal.load_asset(ART + mesh_name)
    if mesh is None:
        rr["error"] = "mesh missing"; report["actors"][label] = rr; continue
    loc = unreal.Vector(p[0], p[1], 0.0)
    a = eas.spawn_actor_from_object(mesh, loc, unreal.Rotator(0.0, 0.0, yaw))
    a.set_actor_label(label)
    a.set_actor_scale3d(unreal.Vector(sc, sc, sc))
    a.set_folder_path("Rime")
    a.tags = [unreal.Name("GFRime")]
    near = []
    for o in others:
        try:
            ol = o.get_actor_location()
        except Exception:
            continue
        if math.hypot(ol.x - p[0], ol.y - p[1]) < r + 100 and not o.get_actor_label().startswith("Rime_"):
            near.append(o.get_actor_label())
    rr["near_existing"] = near
    l = a.get_actor_location()
    rr["pass"] = (l - loc).length() < 0.01 and a.static_mesh_component.static_mesh == mesh
    report["actors"][label] = rr
report["saved"] = bool(les.save_current_level())
report["pass"] = report["loaded"] and report["saved"] and all(v.get("pass") for v in report["actors"].values())
with open(os.path.join(OUT, "place_rime_report.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=1)
unreal.log("GFPlace " + json.dumps({k: report[k] for k in ("pass", "pines", "rocks", "lanterns", "lanterns_skipped", "player_starts", "removed_previous")}))
unreal.SystemLibrary.quit_editor()
