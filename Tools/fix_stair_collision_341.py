"""Issue 341 / 353: stairs and porches on L_World_Factions_A feel like an invisible wall.

Cause: several town meshes import one UCX hull from the ground to the roof. The render mesh shows a step, porch
or plinth in front of the walls, but the hull starts at (or past) the step's outer lip, so the capsule meets a
flat wall where the eye sees a tread. The engine step and the riser assist both see a face many metres tall and
refuse it, and the old StairRefused log only named stair-like faces, so the playtest log stayed empty.

Fix: the meshes in Data/Town/stair_collision_341_v2.json ("fix") use their render triangles as collision
(CTF_UseComplexAsSimple), so the capsule walks on the steps it sees. v2 keeps the original #341 six and adds the
walkable buried-tread meshes from issue #353 (canal bridges, house porches, factory steps, fountain rim, stalls).
Towers, fences, trees, walls, pipes, wagons and props stay on simple collision (audit_only). The UCX elements stay
in the asset; revert mode puts the hull back. Only mesh assets are edited and saved; the map is loaded, read and
never saved, so a map rebuild with Tools/place_towns_v2.py keeps the fix. Rerun this after reimporting any of those
meshes. The existing six uassets already applied for #341 stay; apply only flips new fix-list meshes that are still
on their hull.

Every unique static mesh placed on the map is audited with UGFStairCollisionLibrary.find_buried_treads (render
treads within one step of the ground that the blocking collision covers while the render mesh shows air, and that
a player can reach from outside). Apply mode only edits meshes on the fix list; the rest are reported.

Buried counts: N > 0 buried samples, 0 clean, -1 unmeasurable (the line says why, e.g. no physics state). -1 is never
read as clean: a fix-list mesh at -1 is still applied from the json. A mesh already on complex as simple reads 0 (its
collision is its render triangles, so no hull can bury a tread). The library reads triangles from the LOD0 render
buffers, or from the LOD0 mesh description when the render copy is not CPU readable (town meshes do not set
bAllowCPUAccess), so the audit no longer goes blind on those.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/fix_stair_collision_341.py" -unattended -nullrhi -nosplash

Env:
  GFSTAIR_MODE    audit (default, changes nothing), apply, or revert
  GFSTAIR_MAP     default /Game/Maps/L_World_Factions_A
  GFSTAIR_MESHES  comma list of mesh names to use instead of the json fix list (apply and revert only)
  GFSTAIR_FORCE   1 applies to listed meshes even when the live audit measures 0 buried treads

Before saving, each edited .uasset is copied to Saved/StairCollision/backup_<time>/. Writes
Saved/StairCollision/stair_collision_341_report_<mode>_<time>.json and logs "[gfstair]" lines.
Then run: -ExecCmds="Automation RunTests GFGame.Movement.StairKit;Quit"
"""
import json, os, shutil, time
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
DATA = os.path.join(PROJ, "Data", "Town", "stair_collision_341_v2.json")
OUT = os.path.join(PROJ, "Saved", "StairCollision")
STAMP = time.strftime("%Y%m%d_%H%M%S")
MODE = (os.environ.get("GFSTAIR_MODE", "").strip() or "audit").lower()
MAP = os.environ.get("GFSTAIR_MAP", "").strip() or "/Game/Maps/L_World_Factions_A"
FORCE = os.environ.get("GFSTAIR_FORCE", "").strip() == "1"
EAL = unreal.EditorAssetLibrary
LIB = unreal.GFStairCollisionLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(m):
    unreal.log("[gfstair] " + str(m))


def warn(m):
    unreal.log_warning("[gfstair] " + str(m))


def package_file(object_path):
    """/Game/A/B (or /Game/A/B.B) to <project>/Content/A/B.uasset."""
    pkg = object_path.split(".")[0]
    if not pkg.startswith("/Game/"):
        return None
    return os.path.join(PROJ, "Content", *pkg[len("/Game/"):].split("/")) + ".uasset"


def backup(object_path, folder):
    src = package_file(object_path)
    if not src or not os.path.isfile(src):
        warn("no package file to back up for %s (%s)" % (object_path, src))
        return None
    rel = os.path.relpath(src, os.path.join(PROJ, "Content"))
    dst = os.path.join(folder, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst):
        raise RuntimeError("backup already exists, not overwriting: " + dst)
    shutil.copy2(src, dst)
    return dst


def placed_meshes():
    """mesh path -> (mesh, [components]) for every static mesh component on the loaded map."""
    out = {}
    for actor in eas.get_all_level_actors():
        for comp in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = comp.static_mesh
            if not mesh:
                continue
            path = mesh.get_path_name()
            out.setdefault(path, (mesh, []))[1].append(comp)
    return out


def audit_one(mesh, comps, step):
    """Buried treads on up to four placements. buried is the worst measured count, or -1 when none could be measured."""
    best = None
    blockers = []
    for comp in comps[:4]:
        count, points, heights = LIB.find_buried_treads(comp, step)
        row = {
            "actor": comp.get_owner().get_actor_label() if comp.get_owner() else "",
            "buried": count,
            "heights_cm": sorted(set(round(h, 1) for h in heights))[:12],
            "points": [[round(p.x, 1), round(p.y, 1), round(p.z, 1)] for p in points[:8]],
            "triangles_from": LIB.describe_tread_source(comp),
            "unmeasurable_why": "",
        }
        if count < 0:
            row["unmeasurable_why"] = LIB.describe_tread_audit_blocker(comp) or "unknown"
            blockers.append(row["unmeasurable_why"])
        if best is None or count > best["buried"]:
            best = row
    if best is None:
        return {"actor": "", "buried": -1, "heights_cm": [], "points": [], "triangles_from": "none", "unmeasurable_why": "no placed component"}
    if best["buried"] < 0 and not best["unmeasurable_why"]:
        best["unmeasurable_why"] = "; ".join(sorted(set(blockers))) or "unknown"
    return best


def describe_count(audit):
    if audit["buried"] < 0:
        return "unmeasurable (%s)" % audit["unmeasurable_why"]
    return "%d" % audit["buried"]


def main():
    if MODE not in ("audit", "apply", "revert"):
        raise RuntimeError("GFSTAIR_MODE must be audit, apply or revert, not " + MODE)
    with open(DATA, "r", encoding="utf-8") as f:
        data = json.load(f)
    step = float(data.get("step_height_cm", 105))
    fix_names = [e["name"] for e in data["fix"]]
    override = [n.strip() for n in os.environ.get("GFSTAIR_MESHES", "").split(",") if n.strip()]
    targets = override or fix_names
    audit_only = {e["name"]: e["why"] for e in data.get("audit_only", [])}
    os.makedirs(OUT, exist_ok=True)

    log("mode %s, map %s, step %.1f cm, targets %s" % (MODE, MAP, step, ", ".join(targets)))
    if not les.load_level(MAP):
        raise RuntimeError("load_level failed " + MAP)
    placed = placed_meshes()
    log("%d unique static meshes placed" % len(placed))

    report = {"mode": MODE, "map": MAP, "step_height_cm": step, "time": STAMP, "meshes": [], "changed": [], "skipped": []}
    by_name = {}
    for path in sorted(placed):
        mesh, comps = placed[path]
        name = mesh.get_name()
        by_name[name] = (path, mesh, comps)
        before = audit_one(mesh, comps, step)
        row = {
            "name": name,
            "path": path,
            "placed": len(comps),
            "complex_as_simple": LIB.uses_complex_as_simple(mesh),
            "collision": LIB.describe_simple_collision(mesh),
            "before": before,
            "on_fix_list": name in fix_names,
            "audit_only_reason": audit_only.get(name, ""),
        }
        report["meshes"].append(row)
        if before["buried"] > 0:
            log("%s x%d: %d buried tread samples at %s cm (%s, triangles from %s); %s" % (
                name, len(comps), before["buried"], before["heights_cm"], before["actor"], before["triangles_from"], row["collision"][:300]))
        elif before["buried"] < 0:
            warn("%s x%d: buried treads unmeasurable, not clean: %s%s" % (
                name, len(comps), before["unmeasurable_why"], " (on the fix list, apply uses the json)" if row["on_fix_list"] else ""))

    folder = os.path.join(OUT, "backup_" + STAMP)
    if MODE in ("apply", "revert"):
        for name in targets:
            if name not in by_name:
                warn("%s is not placed on %s, skipped" % (name, MAP))
                report["skipped"].append({"name": name, "why": "not placed"})
                continue
            path, mesh, comps = by_name[name]
            want = MODE == "apply"
            row = next(r for r in report["meshes"] if r["name"] == name)
            if row["complex_as_simple"] == want:
                log("%s already %s, nothing to do" % (name, "complex as simple" if want else "on its UCX hull"))
                report["skipped"].append({"name": name, "why": "already set"})
                continue
            if want and row["before"]["buried"] == 0 and not FORCE:
                log("%s: live audit measures 0 buried treads, left alone (GFSTAIR_FORCE=1 to apply anyway)" % name)
                report["skipped"].append({"name": name, "why": "live audit measured 0 buried treads"})
                continue
            if want and row["before"]["buried"] < 0:
                warn("%s: live audit cannot measure (%s); applying from the json fix list anyway" % (name, row["before"]["unmeasurable_why"]))
            saved = backup(path, folder)
            if not LIB.set_use_complex_as_simple(mesh, want):
                warn("%s: set_use_complex_as_simple failed" % name)
                report["skipped"].append({"name": name, "why": "set failed"})
                continue
            after = audit_one(mesh, comps, step)
            ok = EAL.save_loaded_asset(mesh, False)
            log("%s: %s, buried %s -> %s, saved %s, backup %s" % (
                name, "complex as simple" if want else "back on its UCX hull", describe_count(row["before"]), describe_count(after), ok, saved))
            report["changed"].append({"name": name, "path": path, "complex_as_simple": want, "before": row["before"]["buried"],
                                      "after": after["buried"], "after_detail": after, "saved": bool(ok), "backup": saved})

    out = os.path.join(OUT, "stair_collision_341_report_%s_%s.json" % (MODE, STAMP))
    with open(out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    flagged = [r["name"] for r in report["meshes"] if r["before"]["buried"] > 0]
    blind = [r["name"] for r in report["meshes"] if r["before"]["buried"] < 0]
    log("done: %d meshes with buried treads (%s); %d unmeasurable (%s); changed %d; report %s" % (
        len(flagged), ", ".join(flagged), len(blind), ", ".join(blind), len(report["changed"]), out))
    if blind:
        warn("unmeasurable is not clean: check the unmeasurable_why lines above, then run the StairKit tests, which measure in a test world")
    if MODE == "apply":
        log("map not saved (only mesh assets changed). Rebuild navigation if the map has a nav mesh, then run the StairKit tests.")


main()
