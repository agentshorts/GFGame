"""Accel Hop tutorial visual pass v1 (issue 348): palette and look numbers. Pure Python, no unreal import.

Shared by tools/accelhop_visual_pass_v1.py (editor), tools/make_accelhop_fx_v1.py (editor) and
tools/make_palette_sheet_v1.py (box, matplotlib). Colours are written as sRGB hex and converted to linear for UE.
Change a colour here, bump every asset name to _v2, never edit a _v1 asset in place.
"""

VERSION = "v1"
SOURCE_MAP = "/Game/Maps/L_AccelHop_Tutorial_v1"
TARGET_MAP = "/Game/Maps/L_AccelHop_Tutorial_v2"
MAT_DIR = "/Game/Art/AccelerationHop/Materials"
TUT_MAT_DIR = MAT_DIR + "/Tutorial"
CEL_PARENT_PREFIX = MAT_DIR + "/MI_Hop_Tile_"      # existing per-kind instances (world road), parents of ours
FX_DIR = "/Game/FX/AccelHop"
VIS_TAG = "GFAccelHopTutorial.Visual"              # not equal to "GFAccelHopTutorial", so the layout script keeps it
VIS_FOLDER = "AccelHopTutorial/Visual"
LESSONS = ("Beat", "Steer", "Lit", "Chain")
KINDS = ("Standard", "Downbeat", "Boost", "Landing")

# Current world road tints (Tools/import_acceleration_hop.py TINTS, linear). Kept for the "stronger" check.
WORLD_TINTS_LINEAR = {
    "Standard": ((0.05, 0.12, 0.35), (0.3, 0.8, 1.0)),
    "Downbeat": ((0.35, 0.04, 0.25), (1.0, 0.45, 0.85)),
    "Boost": ((0.6, 0.18, 0.02), (1.0, 0.85, 0.2)),
    "Landing": ((0.05, 0.3, 0.1), (0.6, 1.0, 0.5)),
}
WORLD_EMISSIVE_STRENGTH = 2.0
WORLD_SHADOW_TINT_LINEAR = (0.45, 0.45, 0.55)
LIT_EMISSIVE_STRENGTH = 10.0   # FGFAccelHopLitTuning::LitEmissiveStrength, the lit tile must stay well above ours

# Tutorial tile tints, sRGB hex: (base, glyph). Kind colours keep their meaning from the world road
# (blue standard, magenta downbeat, orange boost, green landing), only stronger. Standard tiles shift hue per
# lesson inside the cool range so each lesson reads as its own zone, and never collide with magenta or orange.
TILE_HEX = {
    "Standard.Beat": ("#2457F0", "#4DF2FF"),
    "Standard.Steer": ("#10A0B4", "#5CFFD8"),
    "Standard.Lit": ("#3A2BC4", "#8FA8FF"),
    "Standard.Chain": ("#6A2BD9", "#B98CFF"),
    "Downbeat": ("#C2187A", "#FF5CC8"),
    "Boost": ("#F06A00", "#FFD23F"),
    "Landing": ("#1FA84A", "#9DFF6E"),
}
RIM_FROM_GLYPH = 0.6                 # same rule as the world road: RimTint = 0.6 * glyph
SHADOW_TINT_HEX = "#5E579C"          # cooler violet shadow band than the world road grey, for harder cel contrast
TILE_SCALARS = {"EmissiveStrength": 3.0, "CelSteps": 3.0, "ShadowThreshold": 0.12, "RimLightWidth": 0.0, "BeatPulseGain": 6.0}

# Floor (Tutorial_Floor): dark, low saturation, so the tiles carry the colour. Grid matches the strip pitch.
FLOOR = {"base_hex": "#2B2740", "line_hex": "#4A4378", "grid_cm": 200.0, "line_cm": 4.0, "roughness": 1.0, "specular": 0.0}

# Outline ink stays the world road's #2A2430 (PP_GF_HopOutline, stencil 3). Not changed here.
OUTLINE_INK_HEX = "#2A2430"

# Sun: pitch -55 so flat tile tops sit in the top cel band (NdotL = sin 55 = 0.82 >= 2/3 for CelSteps 3).
# Yaw 20 sends the light along +X (the course), so the player runs with the sun behind and never into glare.
SUN = {"pitch": -55.0, "yaw": 20.0, "intensity_lux": 10.0, "color_hex": "#FFF1DC", "cascades": 2,
       "dynamic_shadow_distance_cm": 6000.0}
SKY_LIGHT = {"intensity": 1.0, "lower_hemisphere_hex": "#1E1A30", "real_time_capture": True}
SKY_ATMOSPHERE = {"aerial_perspective_view_distance_scale": 0.5}   # less haze on far tiles, they stay saturated
FOG = {"density": 0.005, "start_distance_cm": 3000.0, "inscatter_hex": "#B9A7FF", "volumetric": False}
POST = {
    "exposure_bias": 0.0, "exposure_min": 1.0, "exposure_max": 1.0,   # same fixed manual exposure as the towns
    "bloom_intensity": 0.6, "bloom_threshold": 1.0,                   # only HDR glow blooms, flat cel faces do not
    "saturation": 1.12, "contrast": 1.05, "vignette": 0.2,
    "motion_blur": 0.0, "grain": 0.0, "chromatic_aberration": 0.0, "lens_flare": 0.0,
    "priority": 10.0,
}

# Niagara FX (stateless Lightweight emitters, same builder as Tools/FX/make_mining_fx_v2.py). HDR colours above 1 so
# bloom (threshold 1) picks them up; nothing else blooms.
FX = [
    dict(name="NS_HopBoost_v1", kind="burst", count=24, loop=0.5, life=(0.25, 0.4), color=(6.0, 3.6, 0.6),
         size=((3.0, 6.0), (6.0, 12.0)), dir=(0.0, 0.0, 1.0), cone=55.0, speed=(250.0, 420.0), gravity=-900.0, drag=2.0,
         radius=20.0, bounds=(150.0, -10.0, 220.0), use="boost landing (bApplied, Boost tile or lit), at the feet"),
    dict(name="NS_HopLitPop_v1", kind="burst", count=32, loop=0.6, life=(0.3, 0.45), color=(8.0, 1.2, 5.6),
         size=((3.0, 6.0), (6.0, 10.0)), dir=(0.0, 0.0, 1.0), cone=80.0, speed=(300.0, 500.0), gravity=-600.0, drag=1.5,
         radius=30.0, bounds=(220.0, -10.0, 200.0), use="lit landing paid (bLit and bApplied), at the tile top"),
    dict(name="NS_HopLitIdle_v1", kind="stream", rate=14.0, life=(0.6, 0.9), color=(3.0, 0.5, 2.2),
         size=((2.0, 4.0), (3.0, 6.0)), dir=(0.0, 0.0, 1.0), cone=10.0, speed=(60.0, 110.0), gravity=0.0, drag=0.0,
         radius=90.0, bounds=(200.0, 0.0, 160.0), spin=90.0, stretch=False,
         use="attached to the lit tile while it is lit, at the tile top (Z +6.25)"),
]
FX_BUDGET = {"max_live_particles_total": 120, "max_burst_count": 40, "max_rate": 20.0}


def hex_to_srgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def srgb_to_linear_1(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb_1(c):
    c = max(0.0, min(1.0, c))
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def hex_to_linear(h):
    return tuple(round(srgb_to_linear_1(c), 5) for c in hex_to_srgb(h))


def linear_to_hex(rgb):
    return "#" + "".join("{:02X}".format(int(round(linear_to_srgb_1(c) * 255))) for c in rgb)


def mi_key(kind, lesson):
    """Which tutorial instance a tile gets: Standard splits by lesson, the other kinds share one."""
    if kind not in KINDS:
        raise ValueError("unknown kind " + str(kind))
    if kind == "Standard":
        if lesson not in LESSONS:
            raise ValueError("unknown lesson " + str(lesson))
        return "Standard." + lesson
    return kind


def mi_name(key):
    return "MI_Hop_Tile_" + key.replace(".", "_") + "_Tut_" + VERSION


def mi_path(key):
    n = mi_name(key)
    return "{}/{}".format(TUT_MAT_DIR, n)


def mi_parent(key):
    kind = key.split(".")[0]
    return CEL_PARENT_PREFIX + kind


def mi_params(key):
    """Vector (linear) and scalar parameter values for one tutorial instance."""
    base_hex, glyph_hex = TILE_HEX[key]
    base, glyph = hex_to_linear(base_hex), hex_to_linear(glyph_hex)
    vectors = {"BaseTint": base, "GlyphTint": glyph, "RimTint": tuple(round(g * RIM_FROM_GLYPH, 5) for g in glyph),
               "EmissiveTint": glyph, "ShadowTint": hex_to_linear(SHADOW_TINT_HEX)}
    return {"vectors": vectors, "scalars": dict(TILE_SCALARS)}


def all_mi_keys():
    return list(TILE_HEX.keys())


def floor_material_name():
    return "M_GF_TutFloor_" + VERSION


def lesson_of_tags(tags):
    """GFAccelHopTutorial.Lesson.<Id> -> <Id>, or None."""
    prefix = "GFAccelHopTutorial.Lesson."
    for t in tags:
        t = str(t)
        if t.startswith(prefix):
            return t[len(prefix):]
    return None


def saturation_value(rgb):
    mx, mn = max(rgb), min(rgb)
    return ((mx - mn) / mx if mx > 0 else 0.0), mx


def as_json():
    return {
        "version": VERSION, "source_map": SOURCE_MAP, "target_map": TARGET_MAP,
        "tiles": {k: {"mi": mi_path(k), "parent": mi_parent(k), "base_hex": TILE_HEX[k][0], "glyph_hex": TILE_HEX[k][1],
                      "linear": mi_params(k)} for k in all_mi_keys()},
        "shadow_tint_hex": SHADOW_TINT_HEX, "floor": dict(FLOOR, material=TUT_MAT_DIR + "/" + floor_material_name()),
        "outline_ink_hex": OUTLINE_INK_HEX, "sun": SUN, "sky_light": SKY_LIGHT, "sky_atmosphere": SKY_ATMOSPHERE,
        "fog": FOG, "post": POST, "fx": [dict(f, path=FX_DIR + "/" + f["name"]) for f in FX], "fx_budget": FX_BUDGET,
    }
