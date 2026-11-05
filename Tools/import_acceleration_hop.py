"""Imports the Acceleration Hop tiles (issue 334) from handoff/inbox/acceleration_hop_v1 and builds their materials.

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_acceleration_hop.py" -unattended -nullrhi -nosplash

Env: GFHOP_KIT (default <project>/handoff/inbox/acceleration_hop_v1).

- Meshes: SM_Hop_Tile_{Standard,Downbeat,Boost,Landing} to /Game/Art/AccelerationHop/Tiles with the town importer's FBX
  settings (Force Front X, convert scene + units, scale 1, UCX collision only). UE names drop the _vN suffix.
- Textures: _D sRGB, _M and _E linear masks, nearest, 2D Pixels (same helper as the town kits).
- MPC_GF_HopBeat: scalar BeatPulse (AGFAccelHopRoad writes 1 on the beat, falling to 0) and vector SunDir (towards the
  first directional light). Existing parameters are kept so their ids stay stable.
- M_GF_HopCel: the kit README's cel graph. Unlit; Albedo from the _M masks times BaseTint, RimTint and GlyphTint, nudged by
  the _D grey; light bands from SunDir with CelSteps, ShadowThreshold and ShadowTint; glow = _E * EmissiveTint *
  (EmissiveStrength + BeatPulse * BeatPulseGain), where BeatPulse = MPC BeatPulse + the BeatPulse parameter; fresnel rim
  when RimLightWidth > 0. The lighting is one Custom node so the bands stay hard and there are no If nodes.
- MI_Hop_Tile_<Kind>: one instance per tile with the README preview tints. RimTint = 0.6 * glyph, EmissiveTint = glyph.
- PP_GF_HopOutline: post-process, before tonemapping. Ink (#2A2430) where the CustomDepth stencil 3 mask changes or the
  scene depth has a crease on a tile (8-neighbour Laplacian of depth, which stays near 0 on a flat road seen at a
  grazing angle where a plain Sobel would ink the whole tile). Only visible tile pixels count (CustomDepth matches SceneDepth), so a player
  standing on a tile is not outlined through. Needs r.CustomDepth=3 (Config/DefaultEngine.ini).
- QA per mesh against its manifest: dims, tris, slot, collision count, min Z, material.
Writes Saved/ArtImport/acceleration_hop_report.json and logs "[gfhop]" lines.
"""
import json, os, sys, traceback
import unreal

os.environ["GFTOWN_LIB"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_town_v1 as base  # noqa: E402
import accel_hop_layout as layout  # noqa: E402

PROJ = base.PROJ
KIT = os.environ.get("GFHOP_KIT", "").strip() or os.path.join(PROJ, "handoff", "inbox", "acceleration_hop_v1")
OUT = os.path.join(PROJ, "Saved", "ArtImport")
os.makedirs(OUT, exist_ok=True)
AT = base.AT
EAL = base.EAL
MEL = base.MEL
TILE_DIR = layout.MESH_DIR
MAT_DIR = "/Game/Art/AccelerationHop/Materials"
MPC_PATH = MAT_DIR + "/MPC_GF_HopBeat"
CEL_PATH = MAT_DIR + "/M_GF_HopCel"
OUTLINE_PATH = MAT_DIR + "/PP_GF_HopOutline"
DIM_TOL = 0.05
OUTLINE_STENCIL = 3
OUTLINE_INK_SRGB = (0x2A, 0x24, 0x30)
OUTLINE_WIDTH_PX = 1.5
OUTLINE_DEPTH_EDGE = 0.08
RIM_FROM_GLYPH = 0.6

# README preview tints (linear RGB). Design default: rim = 0.6 * glyph, glow = glyph.
TINTS = {
    "Standard": ((0.05, 0.12, 0.35), (0.3, 0.8, 1.0)),
    "Downbeat": ((0.35, 0.04, 0.25), (1.0, 0.45, 0.85)),
    "Boost": ((0.6, 0.18, 0.02), (1.0, 0.85, 0.2)),
    "Landing": ((0.05, 0.3, 0.1), (0.6, 1.0, 0.5)),
}
# Parent defaults from the README parameter table.
SCALARS = {"EmissiveStrength": 2.0, "BeatPulse": 0.0, "BeatPulseGain": 6.0, "CelSteps": 3.0, "ShadowThreshold": 0.12, "RimLightWidth": 0.0}
VECTORS = {"BaseTint": (0.5, 0.5, 0.5), "RimTint": (0.3, 0.3, 0.3), "GlyphTint": (1.0, 1.0, 1.0), "EmissiveTint": (1.0, 1.0, 1.0),
           "ShadowTint": (0.45, 0.45, 0.55), "RimLightTint": (1.0, 1.0, 1.0)}

report = {"kit": "acceleration_hop_v1", "kit_dir": KIT, "meshes": {}, "materials": {}, "log": []}
base.report = report


def log(m):
    unreal.log("[gfhop] " + str(m))
    report["log"].append(str(m))


base.log = log
try_set = base.try_set


def lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def color(rgb, a=1.0):
    return unreal.LinearColor(float(rgb[0]), float(rgb[1]), float(rgb[2]), float(a))


def node(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def fresh_material(path):
    """Loads the material and clears its graph, or creates it. The graph is rebuilt from this script every run."""
    if EAL.does_asset_exist(path):
        mat = unreal.load_asset(path)
        MEL.delete_all_material_expressions(mat)
        return mat
    d, n = path.rsplit("/", 1)
    return AT.create_asset(n, d, unreal.Material, unreal.MaterialFactoryNew())


def custom(mat, x, y, code, inputs, desc):
    c = node(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("description", desc)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        pins.append(ci)
    c.set_editor_property("inputs", pins)
    return c


def scalar(mat, name, x, y):
    s = node(mat, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property("parameter_name", name)
    s.set_editor_property("default_value", float(SCALARS[name]))
    return s


def vector(mat, name, x, y, value=None):
    v = node(mat, unreal.MaterialExpressionVectorParameter, x, y)
    v.set_editor_property("parameter_name", name)
    v.set_editor_property("default_value", color(value if value is not None else VECTORS[name]))
    return v


def texture(mat, name, tex, sampler, x, y):
    t = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    t.set_editor_property("parameter_name", name)
    t.set_editor_property("texture", tex)
    t.set_editor_property("sampler_type", sampler)
    return t


# ---------- MPC ----------
def make_mpc():
    if EAL.does_asset_exist(MPC_PATH):
        mpc = unreal.load_asset(MPC_PATH)
    else:
        d, n = MPC_PATH.rsplit("/", 1)
        mpc = AT.create_asset(n, d, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    scalars = list(mpc.get_editor_property("scalar_parameters"))
    vectors = list(mpc.get_editor_property("vector_parameters"))
    if not any(str(p.get_editor_property("parameter_name")) == "BeatPulse" for p in scalars):
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", "BeatPulse")
        p.set_editor_property("default_value", 0.0)
        scalars.append(p)
        mpc.set_editor_property("scalar_parameters", scalars)
    if not any(str(p.get_editor_property("parameter_name")) == "SunDir" for p in vectors):
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", "SunDir")
        p.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
        vectors.append(p)
        mpc.set_editor_property("vector_parameters", vectors)
    EAL.save_loaded_asset(mpc)
    names = sorted([str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")] +
                   [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters")])
    report["materials"]["MPC_GF_HopBeat"] = {"parameters": names}
    return mpc


def mpc_node(mat, mpc, name, x, y):
    c = node(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    c.set_editor_property("collection", mpc)
    c.set_editor_property("parameter_name", name)
    return c


# ---------- M_GF_HopCel ----------
CEL_HLSL = r"""
float3 Albedo = (M.r * BaseTint.rgb + M.g * RimTint.rgb + M.b * GlyphTint.rgb) * lerp(0.85, 1.15, dot(D.rgb, float3(0.299, 0.587, 0.114)));
float3 Sun = normalize(SunDir.xyz + float3(0.0, 0.0, 0.0001));
float3 Nn = normalize(N.xyz);
float NdotL = saturate(dot(Nn, Sun));
float Steps = max(CelSteps, 2.0);
float Band = saturate(floor(NdotL * Steps) / (Steps - 1.0));
Band = NdotL < ShadowThreshold ? 0.0 : Band;
float3 Light = lerp(ShadowTint.rgb, float3(1.0, 1.0, 1.0), Band);
float3 Glow = E * EmissiveTint.rgb * (EmissiveStrength + saturate(Pulse) * BeatPulseGain);
float Facing = 1.0 - saturate(dot(Nn, normalize(V.xyz)));
float Rim = RimLightWidth > 0.0 ? smoothstep(1.0 - RimLightWidth, 1.0, Facing) : 0.0;
return Albedo * Light + Glow + Rim * RimLightTint.rgb;
"""
CEL_INPUTS = ["M", "D", "E", "BaseTint", "RimTint", "GlyphTint", "EmissiveTint", "EmissiveStrength", "Pulse", "BeatPulseGain",
              "CelSteps", "ShadowThreshold", "ShadowTint", "RimLightTint", "RimLightWidth", "SunDir", "N", "V"]


def build_cel(mpc, tex):
    mat = fresh_material(CEL_PATH)
    try_set(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    try_set(mat, "used_with_static_lighting", False)
    c = custom(mat, 0, 0, CEL_HLSL, CEL_INPUTS, "HopCel")
    masks = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
    src = {
        "M": (texture(mat, "Mask", tex["_M"], masks, -900, -600), "RGB"),
        "D": (texture(mat, "Diffuse", tex["_D"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -900, -350), "RGB"),
        "E": (texture(mat, "Emissive", tex["_E"], masks, -900, -100), "R"),
        "SunDir": (mpc_node(mat, mpc, "SunDir", -600, 700), ""),
        "N": (node(mat, unreal.MaterialExpressionVertexNormalWS, -600, 850), ""),
        "V": (node(mat, unreal.MaterialExpressionCameraVectorWS, -600, 950), ""),
    }
    y = -600
    for name in ("BaseTint", "RimTint", "GlyphTint", "EmissiveTint", "ShadowTint", "RimLightTint"):
        src[name] = (vector(mat, name, -500, y), "")
        y += 110
    for name in ("EmissiveStrength", "BeatPulseGain", "CelSteps", "ShadowThreshold", "RimLightWidth"):
        src[name] = (scalar(mat, name, -300, y), "")
        y += 80
    pulse = node(mat, unreal.MaterialExpressionAdd, -250, 600)
    MEL.connect_material_expressions(mpc_node(mat, mpc, "BeatPulse", -500, 560), "", pulse, "A")
    MEL.connect_material_expressions(scalar(mat, "BeatPulse", -500, 640), "", pulse, "B")
    src["Pulse"] = (pulse, "")
    wired = []
    for name in CEL_INPUTS:
        expr, pin = src[name]
        if MEL.connect_material_expressions(expr, pin, c, name):
            wired.append(name)
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    report["materials"]["M_GF_HopCel"] = {"inputs_wired": wired, "inputs_expected": CEL_INPUTS}
    return mat, wired == CEL_INPUTS


# ---------- PP_GF_HopOutline ----------
# Scene texture ids: 1 SceneDepth, 13 CustomDepth, 14 PostProcessInput0, 25 CustomStencil.
OUTLINE_HLSL = r"""
float2 UV = GetDefaultSceneTextureUV(Parameters, 14);
float2 Px = View.BufferSizeAndInvSize.zw * max(WidthPx, 1.0);
const float2 O[8] = { float2(-1, -1), float2(0, -1), float2(1, -1), float2(-1, 0), float2(1, 0), float2(-1, 1), float2(0, 1), float2(1, 1) };
float D0 = SceneTextureLookup(UV, 1, false).r;
float C0 = SceneTextureLookup(UV, 13, false).r;
float T0 = (abs(SceneTextureLookup(UV, 25, false).r - Stencil) < 0.5 && abs(C0 - D0) <= 0.01 * D0 + 1.0) ? 1.0 : 0.0;
float Change = 0.0;
float AnyTile = T0;
float Lap = -8.0 * D0;
for (int i = 0; i < 8; i++)
{
    float2 S = UV + O[i] * Px;
    float D = SceneTextureLookup(S, 1, false).r;
    float C = SceneTextureLookup(S, 13, false).r;
    float T = (abs(SceneTextureLookup(S, 25, false).r - Stencil) < 0.5 && abs(C - D) <= 0.01 * D + 1.0) ? 1.0 : 0.0;
    Change = max(Change, abs(T - T0));
    AnyTile = max(AnyTile, T);
    Lap += D;
}
float DepthInk = AnyTile * step(DepthEdge, abs(Lap) / max(D0, 1.0));
float Ink = saturate(max(Change, DepthInk));
float3 SceneColor = SceneTextureLookup(UV, 14, false).rgb;
return lerp(SceneColor, InkColor.rgb, Ink);
"""
OUTLINE_INPUTS = ["Scene", "StencilTex", "InkColor", "WidthPx", "Stencil", "DepthEdge"]


def blendable_before_tonemapping():
    for name in ("BL_SCENE_COLOR_AFTER_DOF", "BL_BEFORE_TONEMAPPING"):
        v = getattr(unreal.BlendableLocation, name, None)
        if v is not None:
            return v, name
    return None, None


def build_outline():
    mat = fresh_material(OUTLINE_PATH)
    try_set(mat, "material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    loc, loc_name = blendable_before_tonemapping()
    if loc is not None:
        try_set(mat, "blendable_location", loc)
    else:
        log("warn: no BlendableLocation before tonemapping; left at the default")
    c = custom(mat, 0, 0, OUTLINE_HLSL, OUTLINE_INPUTS, "HopOutline")
    scene = node(mat, unreal.MaterialExpressionSceneTexture, -500, -300)
    try_set(scene, "scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    stencil_tex = node(mat, unreal.MaterialExpressionSceneTexture, -500, -150)
    try_set(stencil_tex, "scene_texture_id", unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
    ink = node(mat, unreal.MaterialExpressionVectorParameter, -500, 0)
    ink.set_editor_property("parameter_name", "OutlineColor")
    ink.set_editor_property("default_value", color([lin(c8) for c8 in OUTLINE_INK_SRGB]))
    consts = {}
    for i, (name, value) in enumerate((("WidthPx", OUTLINE_WIDTH_PX), ("Stencil", float(OUTLINE_STENCIL)), ("DepthEdge", OUTLINE_DEPTH_EDGE))):
        if name == "Stencil":
            k = node(mat, unreal.MaterialExpressionConstant, -500, 150 + i * 90)
            k.set_editor_property("r", value)
        else:
            k = node(mat, unreal.MaterialExpressionScalarParameter, -500, 150 + i * 90)
            k.set_editor_property("parameter_name", {"WidthPx": "OutlineWidthPx", "DepthEdge": "OutlineDepthEdge"}[name])
            k.set_editor_property("default_value", value)
        consts[name] = k
    src = {"Scene": (scene, "Color"), "StencilTex": (stencil_tex, "Color"), "InkColor": (ink, "")}
    src.update({k: (v, "") for k, v in consts.items()})
    wired = []
    for name in OUTLINE_INPUTS:
        expr, pin = src[name]
        if MEL.connect_material_expressions(expr, pin, c, name):
            wired.append(name)
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    report["materials"]["PP_GF_HopOutline"] = {"inputs_wired": wired, "inputs_expected": OUTLINE_INPUTS,
                                               "blendable_location": loc_name, "stencil": OUTLINE_STENCIL}
    return mat, wired == OUTLINE_INPUTS and loc is not None


# ---------- meshes ----------
def tile_folder(kind):
    return os.path.join(KIT, "SM_Hop_Tile_{}_v1".format(kind))


def import_tile(kind, r):
    folder = tile_folder(kind)
    man = json.load(open(os.path.join(folder, "manifest_v1.json"), encoding="utf-8"))
    name = "SM_Hop_Tile_" + kind
    if man["unreal_target"].split(".")[0] != layout.mesh_path(kind).split(".")[0]:
        raise RuntimeError("manifest target {} is not {}".format(man["unreal_target"], layout.mesh_path(kind)))
    m = base.run_import(os.path.join(folder, man["files"]["mesh"]), TILE_DIR, name, base.fbx_static())
    tex = {}
    for f in man["files"]["textures"]:
        uname, upath = base.import_texture(folder, f, TILE_DIR)
        tex[uname[-2:]] = upath
    missing = [s for s in ("_D", "_M", "_E") if s not in tex]
    if missing:
        raise RuntimeError("missing textures " + ",".join(missing))
    return m, man, tex


def make_instance(kind, cel, tex):
    mi = base.load_or_create_mi(MAT_DIR + "/MI_Hop_Tile_" + kind)
    MEL.set_material_instance_parent(mi, cel)
    for p, s in (("Mask", "_M"), ("Diffuse", "_D"), ("Emissive", "_E")):
        MEL.set_material_instance_texture_parameter_value(mi, p, unreal.load_asset(tex[s]))
    tint_base, glyph = TINTS[kind]
    for p, v in (("BaseTint", tint_base), ("GlyphTint", glyph), ("RimTint", [g * RIM_FROM_GLYPH for g in glyph]), ("EmissiveTint", glyph)):
        MEL.set_material_instance_vector_parameter_value(mi, p, color(v))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


def assign_and_check(kind, m, man, mi, r):
    mats = list(m.get_editor_property("static_materials"))
    for i, sm in enumerate(mats):
        if str(sm.get_editor_property("material_slot_name")) == "Main":
            sm.set_editor_property("material_interface", mi)
            mats[i] = sm
    m.set_editor_property("static_materials", mats)
    EAL.save_loaded_asset(m)
    m = unreal.load_asset(man["unreal_target"])
    chk = r["checks"]
    bb = m.get_bounding_box()
    dims = [round(bb.max.x - bb.min.x, 3), round(bb.max.y - bb.min.y, 3), round(bb.max.z - bb.min.z, 3)]
    r.update({"dims_cm": dims, "bounds_min": base.vec(bb.min), "bounds_max": base.vec(bb.max)})
    try:
        r["tris"] = m.get_num_triangles(0)
    except Exception:
        r["tris"] = None
    r["material_slots"] = [str(s.material_slot_name) for s in m.static_materials]
    r["materials"] = {str(s.material_slot_name): (s.material_interface.get_name() if s.material_interface else None) for s in m.static_materials}
    chk["dims"] = all(abs(a - b) <= DIM_TOL for a, b in zip(dims, man["dimensions_cm"]))
    chk["tris"] = r["tris"] == man["tri_count"]
    chk["slots"] = r["material_slots"] == man["material_slots"]
    chk["material"] = r["materials"].get("Main") == "MI_Hop_Tile_" + kind
    chk["min_z"] = abs(bb.min.z - float(man["qa"].get("min_ground_z_cm", 0.0))) <= DIM_TOL
    try:
        ag = m.get_editor_property("body_setup").get_editor_property("agg_geom")
        r["collision_elems"] = len(ag.get_editor_property("convex_elems")) + len(ag.get_editor_property("box_elems"))
    except Exception as e:
        r["collision_elems"] = None
        log("info: agg_geom " + str(e))
    chk["collision"] = r["collision_elems"] == 1


def main():
    log("kit " + KIT)
    if not os.path.isdir(KIT):
        raise RuntimeError("no kit at {} (copy acceleration_hop_v1 into handoff/inbox or set GFHOP_KIT)".format(KIT))
    mpc = make_mpc()
    imported = {}
    for kind in layout.KINDS:
        r = report["meshes"].setdefault("SM_Hop_Tile_" + kind, {"checks": {}})
        try:
            imported[kind] = import_tile(kind, r)
        except Exception as e:
            r["error"] = str(e)
            log("FAIL import {}: {}\n{}".format(kind, e, traceback.format_exc()))
    if "Standard" not in imported:
        raise RuntimeError("Standard tile did not import, cannot build the material defaults")
    std_tex = {k: unreal.load_asset(v) for k, v in imported["Standard"][2].items()}
    cel, cel_ok = build_cel(mpc, std_tex)
    report["materials"]["M_GF_HopCel"]["ok"] = cel_ok
    outline, outline_ok = build_outline()
    report["materials"]["PP_GF_HopOutline"]["ok"] = outline_ok
    for kind, (m, man, tex) in imported.items():
        r = report["meshes"]["SM_Hop_Tile_" + kind]
        try:
            mi = make_instance(kind, cel, tex)
            assign_and_check(kind, m, man, mi, r)
        except Exception as e:
            r["error"] = str(e)
            log("FAIL material {}: {}\n{}".format(kind, e, traceback.format_exc()))
    bad = [n for n, r in report["meshes"].items() if r.get("error") or not all(r["checks"].values()) or not r["checks"]]
    bad += [n for n, r in report["materials"].items() if r.get("ok") is False]
    report["ok"] = not bad
    report["failed"] = bad
    log("{} meshes, failed: {}".format(len(report["meshes"]), bad or "none"))


try:
    main()
except Exception as e:
    report["ok"] = False
    log("FAIL " + str(e) + "\n" + traceback.format_exc())
finally:
    with open(os.path.join(OUT, "acceleration_hop_report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
