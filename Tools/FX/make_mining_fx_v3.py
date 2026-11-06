# Mining FX v3: dedicated Crack and Break systems so those rows stop reusing NS_Mine_Hit.
# NS_Mining_Crack is a short dust and fragment puff. NS_Mining_Break is a debris burst in a tight cone along the
# spawn forward axis; GFMiningFX spawns with the hit normal as forward, so debris flies back toward the player.
# v1 and v2 assets are left as they are. Same builder as v2, copied rather than imported.
# Run headless: UnrealEditor-Cmd GFGame.uproject -run=pythonscript -script=Tools/FX/make_mining_fx_v3.py
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
    unreal.log("MiningFX v3: " + line)


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

SPECS = [
    dict(name="NS_Mining_Crack", kind="burst", loop=0.8, count=14, life=(0.35, 0.6), color=(0.55, 0.5, 0.45), size=((4.0, 4.0), (10.0, 10.0)), dir=X, cone=70.0, speed=(80.0, 220.0), gravity=-300.0, drag=3.0, radius=6.0, stretch=False, spin=90.0, bounds=box(150.0)),
    dict(name="NS_Mining_Break", kind="burst", loop=1.2, count=36, life=(0.6, 1.0), color=(0.7, 0.62, 0.5), size=((3.0, 3.0), (8.0, 8.0)), dir=X, cone=40.0, speed=(350.0, 750.0), gravity=-980.0, drag=0.8, radius=10.0, stretch=False, spin=360.0, bounds=box(350.0)),
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
open(unreal.Paths.project_saved_dir() + "MiningFX_v3_build.txt", "w").write("\n".join(LOG))
