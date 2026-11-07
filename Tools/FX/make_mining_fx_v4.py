# Mining FX v4: looped Beam_Root (NS_Mining_BeamRoot) and a dedicated Magma hit (NS_Mining_Hit_Magma).
# Beam_Root currently points at NS_Rootmaul_Hit (one-shot leaf burst). Hit_Magma currently shares NS_Mine_Hit with Hit_Spark.
# Other Hit rows keep their existing systems on purpose (see handoff). Same builder as v3, copied rather than imported.
# Run headless: UnrealEditor-Cmd GFGame.uproject -run=pythonscript -script=Tools/FX/make_mining_fx_v4.py
# Existing assets are left alone, so a second run never overwrites a system.
import re
import unreal

L = unreal.GFFXAuthoringLibrary
EAL = unreal.EditorAssetLibrary
DEST = "/Game/FX/Mining"
BURST = "/Niagara/DefaultAssets/Templates/Systems/DirectionalBurstLightweight"
STREAM = "/Niagara/DefaultAssets/Templates/Systems/FountainLightweight"
LOG = []


def log(*parts):
    line = " ".join(str(p) for p in parts)
    LOG.append(line)
    unreal.log("MiningFX v4: " + line)


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


def box(h, lo_z=None, hi_z=None):
    lz = -h if lo_z is None else lo_z
    hz = h if hi_z is None else hi_z
    return "(Min=(X=%s,Y=%s,Z=%s),Max=(X=%s,Y=%s,Z=%s),IsValid=True)" % (f(-h), f(-h), f(lz), f(h), f(h), f(hz))


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


def loop_behaviors(system):
    out = []
    for o in L.inner_objects(system):
        if o.get_class().get_name() != "NiagaraStatelessEmitter":
            continue
        state = L.get_property_text(o, "EmitterState")
        m = re.search(r"LoopBehavior=(\w+)", state or "")
        out.append(m.group(1) if m else "?")
    return out


def build(spec):
    path = "%s/%s" % (DEST, spec["name"])
    if EAL.does_asset_exist(path):
        log("skip, exists:", path)
        return unreal.load_asset(path)
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
    behavior = spec.get("loop_behavior")
    loop = spec.get("loop")
    if behavior:
        if "LoopBehavior=" not in state:
            state = "(LoopBehavior=%s," % behavior + state[1:]
        else:
            state = re.sub(r"LoopBehavior=\w+", "LoopBehavior=%s" % behavior, state)
    if loop is not None:
        state = re.sub(r"LoopDuration=\(Min=[^,]+,Max=[^,]+,", "LoopDuration=(Min=%s,Max=%s," % (f(loop), f(loop)), state)
        state = re.sub(r"(LoopDuration=\([^)]*ChannelConstantsAndRanges=)\([^)]*\)", r"\g<1>(%s)" % f(loop), state)
        if not behavior:
            if "LoopBehavior=" not in state:
                state = "(LoopBehavior=Once," + state[1:]
            else:
                state = re.sub(r"LoopBehavior=\w+", "LoopBehavior=Once", state)
    setp(emitter, "EmitterState", state)
    if "count" in spec:
        setp(emitter, "SpawnInfos", "((Amount=(Min=%d,Max=%d)))" % (spec["count"], spec["count"]))
    else:
        r = spec["rate"]
        setp(emitter, "SpawnInfos", "((Type=Rate,Rate=(Min=%s,Max=%s,ChannelConstantsAndRanges=(%s))))" % (f(r), f(r), f(r)))
    setp(emitter, "FixedBounds", spec["bounds"])

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
    spin = m["SpriteRotationRate"]
    if spec.get("spin"):
        setp(spin, "bModuleEnabled", "True")
        setp(spin, "RotationRateDistribution", rng(-spec["spin"], spec["spin"]))

    if not L.compile_system(system):
        raise RuntimeError("compile failed for " + path)
    if not EAL.save_asset(path, only_if_is_dirty=False):
        raise RuntimeError("save failed for " + path)
    log("saved", path, "loops=", ",".join(loop_behaviors(system)))
    return system


X = (1.0, 0.0, 0.0)
Z = (0.0, 0.0, 1.0)

# NS_Mining_BeamRoot: continuous earth and root tendril along local Z (marker scales Z to beam length).
# Soft green-brown HDR so it reads against lit ground; mild gravity for a drooping vine feel; mid cone.
# NS_Mining_Hit_Magma: one-shot magma chip burst so Hit_Magma stops sharing the warm spark hit.
SPECS = [
    dict(
        name="NS_Mining_BeamRoot",
        kind="stream",
        loop_behavior="Infinite",
        rate=180.0,
        life=(0.24, 0.32),
        color=(1.8, 2.6, 0.55),
        size=((2.5, 2.5), (6.0, 6.0)),
        dir=Z,
        cone=14.0,
        speed=(320.0, 400.0),
        gravity=-180.0,
        radius=2.5,
        stretch=True,
        bounds=box(80.0, -25.0, 130.0),
    ),
    dict(
        name="NS_Mining_Hit_Magma",
        kind="burst",
        loop=0.7,
        loop_behavior="Once",
        count=22,
        life=(0.25, 0.5),
        color=(6.0, 1.8, 0.2),
        size=((1.5, 3.0), (3.0, 8.0)),
        dir=X,
        cone=55.0,
        speed=(280.0, 700.0),
        gravity=-900.0,
        drag=1.2,
        radius=3.0,
        stretch=True,
        spin=120.0,
        bounds=box(250.0),
    ),
]

ROWS = [
    ("Hit_Spark", "/Game/FX/Mining/NS_Mine_Hit.NS_Mine_Hit"),
    ("Hit_Magma", "/Game/FX/Mining/NS_Mining_Hit_Magma.NS_Mining_Hit_Magma"),
    ("Hit_Root", "/Game/FX/Mining/NS_Rootmaul_Hit.NS_Rootmaul_Hit"),
    ("Hit_Frost", "/Game/FX/Mining/NS_Frost_Shatter.NS_Frost_Shatter"),
    ("Hit_Tree", "/Game/FX/Mining/NS_Mine_Break_Wood.NS_Mine_Break_Wood"),
    ("Hit_Crystal", "/Game/FX/Mining/NS_Mine_Break_Crystal.NS_Mine_Break_Crystal"),
    ("Crack_Any", "/Game/FX/Mining/NS_Mining_Crack.NS_Mining_Crack"),
    ("Break_Any", "/Game/FX/Mining/NS_Mining_Break.NS_Mining_Break"),
    ("Break_Tree", "/Game/FX/Mining/NS_Mine_Break_Wood.NS_Mine_Break_Wood"),
    ("Break_Crystal", "/Game/FX/Mining/NS_Mine_Break_Crystal.NS_Mine_Break_Crystal"),
    ("Beam_Spark", "/Game/FX/Mining/NS_Sparkcone_v2.NS_Sparkcone_v2"),
    ("Beam_Magma", "/Game/FX/Mining/NS_MagmaSpray.NS_MagmaSpray"),
    ("Beam_Frost", "/Game/FX/Mining/NS_Frostbind_Beam.NS_Frostbind_Beam"),
    ("Beam_Root", "/Game/FX/Mining/NS_Mining_BeamRoot.NS_Mining_BeamRoot"),
]

unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/FX"], True)
errors = []
for spec in SPECS:
    try:
        build(spec)
    except Exception as e:
        errors.append(str(e))
        log("ERROR", e)

log("--- verify loads ---")
for name, path in ROWS:
    pkg = path.split(".")[0]
    asset = unreal.load_asset(pkg)
    if not asset:
        errors.append("missing " + path)
        log("LOAD FAIL", name, path)
        continue
    loops = loop_behaviors(asset)
    log("LOAD OK", name, path, "class=", asset.get_class().get_name(), "loops=", ",".join(loops))
    if name == "Beam_Root" and "Infinite" not in loops:
        errors.append("Beam_Root not Infinite: " + ",".join(loops))
        log("ERROR Beam_Root must LoopBehavior=Infinite, got", loops)
    if name.startswith("Hit_") or name.startswith("Crack_") or name.startswith("Break_"):
        if "Infinite" in loops:
            errors.append(name + " must be one-shot, got " + ",".join(loops))
            log("ERROR", name, "must be one-shot")

log("DONE errors=%d" % len(errors))
open(unreal.Paths.project_saved_dir() + "MiningFX_v4_build.txt", "w").write("\n".join(LOG))
