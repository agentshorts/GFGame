# Accel Hop tutorial FX v1 (issue 348): three light Niagara systems in /Game/FX/AccelHop, built from the engine's
# Lightweight (stateless) templates with the same builder as Tools/FX/make_mining_fx_v2.py.
#   NS_HopBoost_v1    burst at the feet on a boost landing
#   NS_HopLitPop_v1   burst on the lit tile when its landing pays
#   NS_HopLitIdle_v1  slow rising sparkles on the lit tile while it is lit (attached, looping)
# Specs come from accelhop_visual_palette_v1.py (FX list), next to this file or one folder up (Tools/).
# Run headless: UnrealEditor-Cmd GFGame.uproject -run=pythonscript -script=<abs>/Tools/FX/make_accelhop_fx_v1.py
# Existing assets are left alone, so a rerun never overwrites a system. A further pass goes in a v2 script with _v2 names.
# Spawning needs the code hooks in specs/fx_hooks_v1.md; these assets do nothing until something spawns them.
import os
import re
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.dirname(HERE)]  # next to it, or Tools/ when this sits in Tools/FX
import accelhop_visual_palette_v1 as pal  # noqa: E402

L = unreal.GFFXAuthoringLibrary
EAL = unreal.EditorAssetLibrary
DEST = pal.FX_DIR
BURST = "/Niagara/DefaultAssets/Templates/Systems/DirectionalBurstLightweight"
STREAM = "/Niagara/DefaultAssets/Templates/Systems/FountainLightweight"
LOG = []


def log(*parts):
    line = " ".join(str(p) for p in parts)
    LOG.append(line)
    unreal.log("AccelHopFX v1: " + line)


def f(v):
    return "%.6f" % v


def rng(a, b):
    mode = "UniformConstant" if a == b else "UniformRange"
    chans = "(%s)" % f(a) if a == b else "(%s,%s)" % (f(a), f(b))
    return "(Min=%s,Max=%s,Mode=%s,ChannelConstantsAndRanges=%s)" % (f(a), f(b), mode, chans)


def vec3(x, y, z):
    v = "(X=%s,Y=%s,Z=%s)" % (f(x), f(y), f(z))
    return "(Min=%s,Max=%s,Mode=NonUniformConstant,ChannelConstantsAndRanges=(%s,%s,%s))" % (v, v, f(x), f(y), f(z))


def vec2_range(a, b):
    lo = "(X=%s,Y=%s)" % (f(a[0]), f(a[1]))
    hi = "(X=%s,Y=%s)" % (f(b[0]), f(b[1]))
    return "(Min=%s,Max=%s,Mode=NonUniformRange,ChannelConstantsAndRanges=(%s,%s,%s,%s))" % (lo, hi, f(a[0]), f(a[1]), f(b[0]), f(b[1]))


def color(c):
    v = "(R=%s,G=%s,B=%s,A=1.000000)" % (f(c[0]), f(c[1]), f(c[2]))
    return "(Values=(%s,%s),ValuesTimeRange=(X=0.000000,Y=1.000000),Mode=NonUniformConstant)" % (v, v)


def box(h, lo_z, hi_z):
    return "(Min=(X=%s,Y=%s,Z=%s),Max=(X=%s,Y=%s,Z=%s),IsValid=True)" % (f(-h), f(-h), f(lo_z), f(h), f(h), f(hi_z))


def setp(obj, name, text):
    ok = L.set_property_text(obj, name, text)
    log("  set", obj.get_class().get_name(), name, "ok" if ok else "FAILED", "->", L.get_property_text(obj, name)[:240])
    if not ok:
        raise RuntimeError("could not set %s on %s" % (name, obj.get_name()))


def parts(system):
    emitter = None
    modules = {}
    for o in L.inner_objects(system):
        cn = o.get_class().get_name()
        if cn == "NiagaraStatelessEmitter":
            emitter = o
        elif cn.startswith("NiagaraStatelessModule_"):
            modules[cn[len("NiagaraStatelessModule_"):]] = o
    return emitter, modules


def check(spec):
    """Keeps every system light and versioned."""
    b = pal.FX_BUDGET
    if not spec["name"].startswith("NS_Hop") or not spec["name"].endswith("_" + pal.VERSION):
        raise RuntimeError("v1 only writes NS_Hop*_v1 assets, got " + spec["name"])
    if spec["kind"] == "burst" and spec["count"] > b["max_burst_count"]:
        raise RuntimeError("%s burst %d is over the budget %d" % (spec["name"], spec["count"], b["max_burst_count"]))
    if spec["kind"] == "stream" and spec["rate"] > b["max_rate"]:
        raise RuntimeError("%s rate %s is over the budget %s" % (spec["name"], spec["rate"], b["max_rate"]))


def build(spec):
    path = "%s/%s" % (DEST, spec["name"])
    if EAL.does_asset_exist(path):
        log("skip, exists:", path)
        return "kept"
    template = BURST if spec["kind"] == "burst" else STREAM
    source = unreal.load_asset(template)
    system = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(spec["name"], DEST, source) if source else None
    if not system:
        raise RuntimeError("duplicate failed for " + path)
    log("made", path, "from", template)
    L.set_property_text(system, "bIsTemplateAsset", "False")
    emitter, m = parts(system)
    if emitter is None:
        raise RuntimeError("no stateless emitter in " + path)

    state = L.get_property_text(emitter, "EmitterState")
    loop = spec.get("loop") if spec["kind"] == "burst" else None
    if loop:
        state = re.sub(r"LoopDuration=\(Min=[^,]+,Max=[^,]+,", "LoopDuration=(Min=%s,Max=%s," % (f(loop), f(loop)), state)
        state = re.sub(r"(LoopDuration=\([^)]*ChannelConstantsAndRanges=)\([^)]*\)", r"\g<1>(%s)" % f(loop), state)
        if "LoopBehavior=" not in state:
            state = "(LoopBehavior=Once," + state[1:]
        else:
            state = re.sub(r"LoopBehavior=\w+", "LoopBehavior=Once", state)
    setp(emitter, "EmitterState", state)
    if spec["kind"] == "burst":
        setp(emitter, "SpawnInfos", "((Amount=(Min=%d,Max=%d)))" % (spec["count"], spec["count"]))
    else:
        r = spec["rate"]
        setp(emitter, "SpawnInfos", "((Type=Rate,Rate=(Min=%s,Max=%s,ChannelConstantsAndRanges=(%s))))" % (f(r), f(r), f(r)))
    setp(emitter, "FixedBounds", box(*spec["bounds"]))

    init = m["InitializeParticle"]
    setp(init, "LifetimeDistribution", rng(*spec["life"]))
    setp(init, "ColorDistribution", color(spec["color"]))
    setp(init, "SpriteSizeDistribution", vec2_range(*spec["size"]))

    vel = m["AddVelocity"]
    setp(vel, "VelocityType", "InCone")
    d = spec["dir"]
    setp(vel, "ConeDirection", vec3(d[0], d[1], d[2]))
    setp(vel, "ConeAngle", f(spec["cone"]))
    setp(vel, "ConeVelocityDistribution", rng(*spec["speed"]))

    grav = m["GravityForce"]
    setp(grav, "bModuleEnabled", "True" if spec["gravity"] else "False")
    if spec["gravity"]:
        setp(grav, "GravityDistribution", vec3(0, 0, spec["gravity"]))
    drag = m["Drag"]
    setp(drag, "bModuleEnabled", "True" if spec.get("drag") else "False")
    if spec.get("drag"):
        setp(drag, "DragDistribution", rng(spec["drag"], spec["drag"]))
    shape = m["ShapeLocation"]
    setp(shape, "bModuleEnabled", "True")
    setp(shape, "ShapePrimitive", "Sphere")
    setp(shape, "SphereRadius", rng(0.0, spec.get("radius", 4.0)))
    stretch = m["ScaleSpriteSizeBySpeed"]
    setp(stretch, "bModuleEnabled", "True" if spec.get("stretch", True) else "False")
    spin = m.get("SpriteRotationRate")
    if spin is not None and spec.get("spin"):
        setp(spin, "bModuleEnabled", "True")
        setp(spin, "RotationRateDistribution", rng(-spec["spin"], spec["spin"]))

    if not L.compile_system(system):
        raise RuntimeError("compile failed for " + path)
    if not EAL.save_asset(path, only_if_is_dirty=False):
        raise RuntimeError("save failed for " + path)
    log("saved", path)
    return "created"


unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/FX"], True)
errors = []
for spec in pal.FX:
    try:
        check(spec)
        build(spec)
    except Exception as e:
        errors.append(str(e))
        log("ERROR", e)
log("DONE errors=%d" % len(errors))
open(unreal.Paths.project_saved_dir() + "AccelHopFX_v1_build.txt", "w").write("\n".join(LOG))
