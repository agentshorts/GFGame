"""World map v1 follow-up, run headless after import_art_batch.py with GFART_ONLY=<the 12 worldmap_v1 statics>:
  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_worldmap_v1_post.py" -unattended -nullrhi -nosplash
- SM_FerryBasket v2: the batch importer deletes and re-creates the mesh, so the Grip (0,0,193.75) and Seat (-25,0,12.5)
  sockets are added again at the manifest points.
- SK_Dino_Ripwing v3 and SK_Dino_BigGrumble v4 are texture-only: just _D is re-imported (mesh, skeleton and clips stay).
- Then each manifest's ue/ helper runs: MI_Dino_Ripwing + MI_Dino_Ripwing_Ferry, and MI_Dino_BigGrumble on the Body slot.
  Since Ripwing v4 the helper writes the ferry Dye_R itself from sRGB #FFC93C (linear 1.0, 0.584, 0.045), so nothing is
  patched afterwards; the value is only read back and checked against FERRY_CREST.
GFART_INBOX overrides the inbox as in import_art_batch.py. GFPOST_ONLY (comma-separated: SM_FerryBasket,
SK_Dino_Ripwing, SK_Dino_BigGrumble) limits the run to those assets. Writes Saved/ArtImport/worldmap_v1_post.json.
"""
import json, os, traceback
import unreal

_here = os.path.dirname(os.path.abspath(__file__))
os.environ["GFART_ONLY"] = "__none__"  # only the batch importer's helpers are used
_src = open(os.path.join(_here, "import_art_batch.py"), encoding="utf-8").read()
exec(compile(_src[:_src.index("# Clean re-import")], os.path.join(_here, "import_art_batch.py"), "exec"), globals())
report = {"assets": {}, "log": []}
_only = [n.strip() for n in os.environ.get("GFPOST_ONLY", "").split(",") if n.strip()]


def wanted(n):
    return not _only or n in _only


FERRY_CREST = "#FFC93C"

def srgb_hex(h):
    h = h.lstrip("#")
    def lin(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(lin(int(h[0:2], 16)), lin(int(h[2:4], 16)), lin(int(h[4:6], 16)), 1.0)

BASKET_SOCKETS = {"Grip": (0.0, 0.0, 193.75), "Seat": (-25.0, 0.0, 12.5)}
DINOS = {
    "SK_Dino_Ripwing": ("/Game/Art/Dinos/Ripwing/Textures", "T_Dino_Ripwing_D", "MI_Dino_Ripwing_setup.py"),
    "SK_Dino_BigGrumble": ("/Game/Art/Dinos/BigGrumble/Textures", "T_Dino_BigGrumble_D", "MI_Dino_BigGrumble_setup.py"),
}

r = {"checks": {}}
try:
    if not wanted("SM_FerryBasket"):
        raise StopIteration
    report["assets"]["SM_FerryBasket"] = r
    m = unreal.load_asset("/Game/Art/Props/Ferry/SM_FerryBasket")
    for sname, loc in BASKET_SOCKETS.items():
        if m.find_socket(sname) is not None:
            m.remove_socket(m.find_socket(sname))
        s = unreal.StaticMeshSocket(m)
        s.set_editor_property("socket_name", sname)
        s.set_editor_property("relative_location", unreal.Vector(*loc))
        m.add_socket(s)
    EAL.save_loaded_asset(m)
    m = unreal.load_asset("/Game/Art/Props/Ferry/SM_FerryBasket")
    got = {n: (None if m.find_socket(n) is None else vec(m.find_socket(n).get_editor_property("relative_location"))) for n in BASKET_SOCKETS}
    r["sockets"] = got
    r["checks"]["sockets"] = all(got[n] is not None and all(abs(a - b) < 1e-3 for a, b in zip(got[n], l)) for n, l in BASKET_SOCKETS.items())
except StopIteration:
    pass
except Exception as e:
    r["error"] = str(e) + "\n" + traceback.format_exc()

for name, (tdir, tex, helper) in DINOS.items():
    if not wanted(name):
        continue
    r = {"checks": {}}
    report["assets"][name] = r
    try:
        folder = os.path.join(INBOX, name)
        man = json.load(open(os.path.join(folder, "manifest.json"), encoding="utf-8"))
        r["manifest_version"] = man.get("version")
        t = run_import(os.path.join(folder, tex + ".png"), tdir, tex, None)
        try_set(t, "filter", unreal.TextureFilter.TF_NEAREST)
        EAL.save_loaded_asset(t)
        r["texture_sources"] = source_files(t)
        r["checks"]["source_paths_clean"] = not [p for p in (r["texture_sources"] or []) if BAD_PATH.search(p)]
        code = open(os.path.join(folder, "ue", helper), encoding="utf-8").read()
        exec(compile(code, helper, "exec"), {"__name__": "__gfart_helper__"})
        if name == "SK_Dino_Ripwing":
            # Read back only: the helper wrote it (no post-import pin).
            fmi = unreal.load_asset("/Game/Art/Dinos/Ripwing/Materials/MI_Dino_Ripwing_Ferry")
            c = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(fmi, "Dye_R")
            r["ferry_dye_r"] = [round(c.r, 4), round(c.g, 4), round(c.b, 4)]
            r["checks"]["ferry_dye_r"] = abs(c.r - srgb_hex(FERRY_CREST).r) < 1e-3 and abs(c.g - srgb_hex(FERRY_CREST).g) < 1e-3 and abs(c.b - srgb_hex(FERRY_CREST).b) < 1e-3
        mesh = unreal.load_asset(man["unreal_target"])
        r["body_material"] = [str(s.material_interface.get_name()) if s.material_interface else None for s in mesh.materials]
        want = "MI_" + name[3:]
        r["checks"]["body_mi"] = r["body_material"] == [want]
    except Exception as e:
        r["error"] = str(e) + "\n" + traceback.format_exc()

for n, r in report["assets"].items():
    r["pass"] = "error" not in r and all(v is True for v in r["checks"].values())
    log("{}: pass={} checks={}".format(n, r["pass"], r["checks"]))
with open(os.path.join(OUT, "worldmap_v1_post.json"), "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=1)
log("report written " + os.path.join(OUT, "worldmap_v1_post.json"))
