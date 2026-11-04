# Mining FX v2: a brighter, bigger Sparkcone beam, written as a new asset (NS_Sparkcone_v2) next to v1.
# v1 (make_mining_fx_v1.py) and NS_Sparkcone are left as they are. Same builder as v1, copied rather than imported
# because importing v1 would run its build loop.
# Run headless: UnrealEditor-Cmd GFGame.uproject -run=pythonscript -script=Tools/FX/make_mining_fx_v2.py
# Existing assets are left alone, so a second run never overwrites a system. A further pass goes in a v3 script
# that writes NS_Sparkcone_v3.
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
    unreal.log("MiningFX v2: " + line)


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

# NS_Sparkcone in v1, for comparison:
#   rate=220, life=(0.2, 0.25), color=(1.0, 2.2, 6.0), size=((1.0, 3.0), (2.0, 6.0)), radius=2, bounds=box(70, -10, 120)
# v2 is about three times the colour (every channel well above 1, so bloom picks it up against a lit wall), twice
# the sprite size, a denser and slightly longer stream, and a wider spawn sphere so the cone reads as a cone and not
# a needle. Direction, cone angle and speed are unchanged, so the beam still reaches the same 8 m mark.
V1_SPARKCONE = dict(rate=220.0, life=(0.2, 0.25), color=(1.0, 2.2, 6.0), size=((1.0, 3.0), (2.0, 6.0)), radius=2.0)
SPECS = [
    dict(name="NS_Sparkcone_v2", kind="stream", rate=320.0, life=(0.22, 0.3), color=(3.0, 6.6, 18.0), size=((2.0, 6.0), (4.0, 12.0)), dir=Z, cone=27.0, speed=(380.0, 440.0), gravity=0.0, radius=3.0, bounds=box(90.0, -15.0, 150.0)),
]


def check(spec):
    """Refuses a spec that would not be brighter and bigger than v1, or would replace a v1 asset."""
    if not spec["name"].endswith("_v2"):
        raise RuntimeError("v2 only writes _v2 assets, got " + spec["name"])
    if min(spec["color"]) <= 1.0:
        raise RuntimeError("every colour channel must be above 1 (HDR), got %s" % (spec["color"],))
    for got, old in zip(spec["color"], V1_SPARKCONE["color"]):
        if got <= old:
            raise RuntimeError("colour %s is not brighter than v1 %s" % (spec["color"], V1_SPARKCONE["color"]))
    (lo, hi) = spec["size"]
    (old_lo, old_hi) = V1_SPARKCONE["size"]
    if lo[0] < old_lo[0] or lo[1] < old_lo[1] or hi[0] < old_hi[0] or hi[1] < old_hi[1]:
        raise RuntimeError("sprite size %s is smaller than v1 %s" % (spec["size"], V1_SPARKCONE["size"]))


unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/FX"], True)
errors = []
for spec in SPECS:
    try:
        check(spec)
        build(spec)
    except Exception as e:
        errors.append(str(e))
        log("ERROR", e)
log("DONE errors=%d" % len(errors))
open(unreal.Paths.project_saved_dir() + "MiningFX_v2_build.txt", "w").write("\n".join(LOG))
