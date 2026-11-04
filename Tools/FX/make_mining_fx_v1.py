# Builds the mining FX v1 Niagara systems under /Game/FX/Mining from engine lightweight templates.
# Run headless: UnrealEditor-Cmd GFGame.uproject -run=pythonscript -script=<this file>
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
    unreal.log("MiningFX: " + line)


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


def build(spec):
    path = "%s/%s" % (DEST, spec["name"])
    if EAL.does_asset_exist(path):
        log("skip, exists:", path)
        return
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
    loop = spec.get("loop")
    if loop:
        state = re.sub(r"LoopDuration=\(Min=[^,]+,Max=[^,]+,", "LoopDuration=(Min=%s,Max=%s," % (f(loop), f(loop)), state)
        state = re.sub(r"(LoopDuration=\([^)]*ChannelConstantsAndRanges=)\([^)]*\)", r"\g<1>(%s)" % f(loop), state)
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
    log("saved", path)


X = (1.0, 0.0, 0.0)
Z = (0.0, 0.0, 1.0)
SPECS = [
    # One-shot bursts fire along local X, which faces out of the mined surface.
    dict(name="NS_Mine_Hit", kind="burst", loop=0.6, count=20, life=(0.2, 0.45), color=(4.0, 2.6, 0.9), size=((1.0, 3.0), (2.0, 7.0)), dir=X, cone=60.0, speed=(300.0, 750.0), gravity=-980.0, drag=1.5, radius=2.0, bounds=box(250.0)),
    dict(name="NS_Mine_Break_Wood", kind="burst", loop=1.2, count=26, life=(0.6, 1.0), color=(0.36, 0.2, 0.08), size=((3.0, 3.0), (7.0, 7.0)), dir=X, cone=75.0, speed=(200.0, 520.0), gravity=-980.0, drag=0.8, radius=10.0, stretch=False, spin=360.0, bounds=box(300.0)),
    dict(name="NS_Mine_Break_Crystal", kind="burst", loop=1.0, count=32, life=(0.5, 0.9), color=(0.3, 2.4, 3.0), size=((2.0, 5.0), (4.0, 12.0)), dir=X, cone=80.0, speed=(300.0, 800.0), gravity=-700.0, drag=0.8, radius=10.0, bounds=box(300.0)),
    dict(name="NS_Rootmaul_Hit", kind="burst", loop=1.0, count=30, life=(0.4, 0.7), color=(0.35, 2.0, 0.3), size=((2.0, 2.0), (6.0, 6.0)), dir=X, cone=70.0, speed=(200.0, 500.0), gravity=-600.0, drag=1.0, radius=12.0, stretch=False, spin=180.0, bounds=box(250.0)),
    dict(name="NS_Frost_Shatter", kind="burst", loop=1.0, count=40, life=(0.4, 0.8), color=(2.4, 3.2, 4.5), size=((1.5, 4.0), (3.0, 10.0)), dir=X, cone=85.0, speed=(250.0, 650.0), gravity=-500.0, drag=1.2, radius=8.0, bounds=box(250.0)),
    # Looping streams for the held field marker. Local Z is the beam axis; the marker scales Z to the beam length.
    dict(name="NS_Sparkcone", kind="stream", rate=220.0, life=(0.2, 0.25), color=(1.0, 2.2, 6.0), size=((1.0, 3.0), (2.0, 6.0)), dir=Z, cone=27.0, speed=(380.0, 440.0), gravity=0.0, radius=2.0, bounds=box(70.0, -10.0, 120.0)),
    dict(name="NS_MagmaSpray", kind="stream", rate=150.0, life=(0.22, 0.3), color=(6.0, 1.6, 0.15), size=((3.0, 3.0), (7.0, 7.0)), dir=Z, cone=20.0, speed=(340.0, 420.0), gravity=-250.0, radius=3.0, bounds=box(70.0, -30.0, 120.0)),
    dict(name="NS_Frostbind_Beam", kind="stream", rate=260.0, life=(0.22, 0.25), color=(1.8, 2.6, 4.0), size=((2.0, 2.0), (4.0, 4.0)), dir=Z, cone=6.0, speed=(400.0, 430.0), gravity=0.0, radius=1.5, bounds=box(40.0, -10.0, 120.0)),
]

unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/FX"], True)
errors = []
for spec in SPECS:
    try:
        build(spec)
    except Exception as e:
        errors.append(str(e))
        log("ERROR", e)
log("DONE errors=%d" % len(errors))
open(unreal.Paths.project_saved_dir() + "MiningFX_build.txt", "w").write("\n".join(LOG))
