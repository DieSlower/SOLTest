# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates (or, with SOL_REBUILD, rebuilds in place) the flight HUD shape material (issue #28, SDD 21 phase A),
# idempotent:
#   /Game/SOL/UI/M_SOLHudShape
#       User-interface domain, additive. Drawn by SSOLHudShapeLayer as one Slate custom-vertex batch (FSOLHudShapeBatch):
#       per vertex TexCoord0 = the pixel position in the shape's local frame, TexCoord1 = (size 1, kind x stride +
#       size 2), vertex colour = sRGB(colour x intensity) with alpha = the shape's extra parameter (arc: half aperture
#       / pi, panel: fill). The Custom node mirrors SOLHudShapeMath (UI/SOLHudShapeMath.cpp): row jitter (moved into
#       the local frame through ddx), the line / arc / panel / ellipse / dashed line signed distance (the last two
#       phase C, issue #30; alpha = Y / X radius or dash period / 255), LT's glow profile, the panel fill with fresnel
#       edge, scanlines and flicker (phase B, issue #29), the optional grain and scanline composite, LT's tone
#       curve and blue grade.
# Every default is read from Source/SOLTest/SOLConstants.h (SOL::Hologram HUD_*, SOL::Settings::HUD_EFFECTS_DEFAULT);
# the parameter names must match SOL::HudShapeMaterialParams and the asset path SOL::Paths::HUD_SHAPE_MATERIAL.
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
#       -script="<abs path>/Tools/UI/create_hud_shape_material.py" -unattended [-SOLRebuild]
# or, in an open editor, exec() this file with SOL_REBUILD = True defined in the calling scope's globals.
import os
import re

import unreal

UI_DIR = "/Game/SOL/UI"
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))
CONSTANTS_FILE = os.path.join(unreal.Paths.project_dir(), "Source", "SOLTest", "SOLConstants.h")

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


# Returns a numeric SOL:: constant from SOLConstants.h, so the material defaults cannot drift from the C++
def constant(name):
    text = open(CONSTANTS_FILE, encoding="utf-8").read()
    match = re.search(r"\b" + name + r"\s*=\s*([-0-9.eE+]+)", text)
    if match is None:
        raise RuntimeError("create_hud_shape_material: %s not found in %s" % (name, CONSTANTS_FILE))
    return float(match.group(1))


SHAPE_HLSL = r"""// SOL HUD shape (SDD 21): jitter, signed distance, LT glow, composite, tone curve and blue grade
const float kPi = 3.14159265;
const float kind = floor(Params.y / KindStride + 1e-4);
const float s1 = Params.x;
const float s2 = Params.y - kind * KindStride;
float2 p = Local;

// Row jitter (SOLHudShapeMath::JitterOffsetFraction): a smooth noise of the screen row picks rare rows to shift
const float y01 = Parameters.SvPosition.y / max(ViewH, 1.0);
const float age = fmod(Time, TimeWrap);
const float jx = y01 * RowCells + age * Scroll;
const float ji = floor(jx);
const float jf = jx - ji;
const float n1 = lerp(frac(sin(fmod(ji, Lattice) * HashF) * HashS), frac(sin(fmod(ji + 1.0, Lattice) * HashF) * HashS),
    jf * jf * (3.0 - 2.0 * jf));
const float sx = y01 * RowCells + age * Scroll * SignSpeed + SignOffset;
const float si = floor(sx);
const float sf = sx - si;
const float n2 = lerp(frac(sin(fmod(si, Lattice) * HashF) * HashS), frac(sin(fmod(si + 1.0, Lattice) * HashF) * HashS),
    sf * sf * (3.0 - 2.0 * sf));
const float jitterPx = Strength * JitterMax * ViewW * exp(-Sharp * n1) * (n2 >= 0.5 ? 1.0 : -1.0);
p += ddx(Local) * jitterPx;

// Signed distance in pixels (negative inside): capsule, ring arc about +Y (alpha = half aperture / pi), rounded box
float d;
if (kind < 0.5)
{
    d = length(float2(max(abs(p.x) - s1, 0.0), p.y)) - s2;
}
else if (kind < 1.5)
{
    const float ap = VAlpha * kPi;
    const float2 sc = float2(max(sin(ap), 0.0), cos(ap));   // sin(pi) may round below 0: full ring stays a ring
    const float2 q = float2(abs(p.x), p.y);
    d = ((sc.y * q.x > sc.x * q.y) ? length(q - sc * s1) : abs(length(q) - s1)) - s2;
}
else if (kind < 2.5)
{
    const float2 q = abs(p) - float2(s1, s2) + Corner;
    d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - Corner;
}
else if (kind < 3.5)
{
    // Ellipse ring (phase C, issue #30; SOLHudShapeMath::EllipseDistancePx): radii (s1, s1 x alpha), half width s2
    const float2 r = max(float2(s1, s1 * VAlpha), 1e-3);   // A zero radius would divide by zero
    const float k0 = length(p / r);
    const float k1 = length(p / (r * r));
    d = (k1 > 1e-6 ? abs(k0 * (k0 - 1.0) / k1) : min(r.x, r.y)) - s2;
}
else
{
    // Dashed line (phase C; SOLHudShapeMath::DashedLineDistancePx): half length s1, half width s2, period alpha x 255
    // px, dashes from -s1; dx is the distance along the line to the nearest existing dash
    const float period = max(VAlpha * 255.0, 1.0);
    const float dashLen = DashFraction * period;
    const float t = p.x + s1;
    const float tc = clamp(t, 0.0, 2.0 * s1);
    float tt = tc - floor(tc / period) * period;
    tt += (s1 > 0.0 && tc - tt >= 2.0 * s1) ? period : 0.0;   // No dash starts exactly at the far end
    const float nextStart = tc - tt + period;
    float gapD = max(tt - dashLen, 0.0);
    if (tt > dashLen && nextStart < 2.0 * s1)
    {
        gapD = min(gapD, nextStart - tc);
    }
    d = length(float2(abs(t - tc) + gapD, p.y)) - s2;
}
const bool isPanel = kind >= 1.5 && kind < 2.5;

// LT glow of the distance from a CoreHalf px core; panels glow along their outline and add a fill with a fresnel edge
const float dd = !isPanel ? max(d + CoreHalf, 0.0) : abs(d);
float shape = CoreW * exp(-CoreF * max(0.0, dd - CoreHalf)) + HaloW * exp(-pow(HaloS * dd, HaloP));

// The halo fades to nothing over the outer part of the quad padding, so no quad edge ever shows
shape *= 1.0 - smoothstep(PadFade * Pad, Pad, dd);
// (SOLHudShapeMath::PadFade, PanelFill and PanelFlicker; scanlines and flicker scale with the strength slider)
const float strength0 = max(Strength, 0.0);
if (isPanel && d < 0.0)
{
    shape += VAlpha * (FillBase + (1.0 - FillBase) * exp(d / EdgeFall))
        * (1.0 + PanelScan * strength0 * sin(kPi * Parameters.SvPosition.y));
}
if (isPanel)
{
    const float fx = age * FlickerRate + FlickerOffset;
    const float fi = floor(fx);
    const float ff = fx - fi;
    const float fn = lerp(frac(sin(fmod(fi, Lattice) * HashF) * HashS),
        frac(sin(fmod(fi + 1.0, Lattice) * HashF) * HashS), ff * ff * (3.0 - 2.0 * ff));
    shape *= 1.0 - Flicker * strength0 * fn;
}

// LT's maths is in display (gamma) space: when Slate shades in linear space (the vertex colour arrives linearised
// and the output is gamma-corrected afterwards), go to display space here and back to linear at the end (a 2.2
// power, not the exact sRGB curve: slightly off in the darkest tones, which the glow barely uses)
#if defined(SOURCE_IN_LINEAR_SPACE) && SOURCE_IN_LINEAR_SPACE
const float3 baseColor = pow(max(VColor, 0.0), 1.0 / DisplayGamma);
#else
const float3 baseColor = VColor;
#endif
float3 c = baseColor * ColorGain * shape;

// Optional LT composite (grain, scanlines), scaled by the strength slider
const float cs = Composite * Strength;
const float grainHash = frac(sin(dot(floor(Parameters.SvPosition.xy), float2(12.9898, 78.233))
    + floor(fmod(Time, TimeWrap) * 60.0) * 0.123) * HashS);
c *= 1.0 - Grain * cs * log(1.0 - 0.999 * grainHash * grainHash);
c *= 1.0 + Scan * cs * exp(-2.0 * (0.5 + 0.5 * sin(2.0 * Parameters.SvPosition.y)));

// LT tone curve and blue grade (quartic Bezier 0, B1, B2, B3, 1)
c = 1.0 - exp(-ToneGain * pow(max(c, 0.0), TonePower));
const float t = c.b;
const float u = 1.0 - t;
c.b = 4.0 * t * u * u * u * B1 + 6.0 * t * t * u * u * B2 + 4.0 * t * t * t * u * B3 + t * t * t * t;
#if defined(SOURCE_IN_LINEAR_SPACE) && SOURCE_IN_LINEAR_SPACE
c = pow(max(c, 0.0), DisplayGamma);
#endif
return c;"""

# Custom node constant pins and the SOLConstants.h names they are read from
CONSTANT_PINS = [
    ("KindStride", "HUD_SHAPE_KIND_STRIDE"), ("RowCells", "HUD_JITTER_ROW_CELLS"),
    ("Scroll", "HUD_JITTER_SCROLL_RATE"), ("SignSpeed", "HUD_JITTER_SIGN_SPEED"),
    ("SignOffset", "HUD_JITTER_SIGN_OFFSET"), ("JitterMax", "HUD_JITTER_MAX_FRACTION"),
    ("Sharp", "HUD_JITTER_SHARPNESS"), ("Corner", "HUD_PANEL_CORNER_PX"), ("CoreW", "HUD_GLOW_CORE_WEIGHT"),
    ("CoreF", "HUD_GLOW_CORE_FALLOFF"), ("HaloW", "HUD_GLOW_HALO_WEIGHT"), ("HaloS", "HUD_GLOW_HALO_SCALE"),
    ("HaloP", "HUD_GLOW_HALO_POWER"), ("EdgeFall", "HUD_PANEL_EDGE_FALLOFF_PX"),
    ("PanelScan", "HUD_PANEL_SCANLINE"), ("ColorGain", "HUD_COLOR_GAIN"), ("Grain", "HUD_GRAIN_AMOUNT"),
    ("Scan", "HUD_SCANLINE_AMOUNT"), ("B1", "HUD_GRADE_BLUE_1"), ("B2", "HUD_GRADE_BLUE_2"),
    ("B3", "HUD_GRADE_BLUE_3"), ("Pad", "HUD_GLOW_PAD_PX"), ("DisplayGamma", "HUD_DISPLAY_GAMMA"),
    ("TimeWrap", "HUD_TIME_WRAP_S"), ("Lattice", "HUD_NOISE_LATTICE_PERIOD"), ("HashF", "HUD_NOISE_HASH_FREQUENCY"),
    ("HashS", "HUD_NOISE_HASH_SCALE"), ("CoreHalf", "HUD_GLOW_CORE_HALF_PX"), ("PadFade", "HUD_PAD_FADE_START"),
    ("FillBase", "HUD_PANEL_FILL_BASE"), ("ToneGain", "HUD_TONE_GAIN"), ("TonePower", "HUD_TONE_POWER"),
    ("Flicker", "HUD_PANEL_FLICKER"), ("FlickerRate", "HUD_PANEL_FLICKER_RATE_HZ"),
    ("FlickerOffset", "HUD_PANEL_FLICKER_OFFSET"), ("DashFraction", "HUD_DASH_FRACTION"),
]


# Adds an expression of the given class at an editor position
def add(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


# Adds a scalar parameter with a default
def scalar(material, name, value, x, y):
    node = add(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


# Builds the shape material graph
def build_shape(material):
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    variable_pins = ["Local", "Params", "VColor", "VAlpha", "Time", "Strength", "Composite", "ViewW", "ViewH"]
    custom = add(material, unreal.MaterialExpressionCustom, -500, 0)
    custom.set_editor_property("description", "SOLHudShape")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", SHAPE_HLSL)
    inputs = []
    for name in variable_pins + [pin for pin, _ in CONSTANT_PINS]:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)

    local = add(material, unreal.MaterialExpressionTextureCoordinate, -1200, -400)
    local.set_editor_property("coordinate_index", 0)
    params = add(material, unreal.MaterialExpressionTextureCoordinate, -1200, -300)
    params.set_editor_property("coordinate_index", 1)
    vertex_color = add(material, unreal.MaterialExpressionVertexColor, -1200, -200)
    time = add(material, unreal.MaterialExpressionTime, -1200, -100)
    strength = scalar(material, "EffectsStrength", constant("HUD_EFFECTS_DEFAULT"), -1200, 0)
    composite = scalar(material, "Composite", 0.0, -1200, 100)
    view_w = scalar(material, "ViewWidthPx", 1280.0, -1200, 200)
    view_h = scalar(material, "ViewHeightPx", 720.0, -1200, 300)
    MEL.connect_material_expressions(local, "", custom, "Local")
    MEL.connect_material_expressions(params, "", custom, "Params")
    MEL.connect_material_expressions(vertex_color, "", custom, "VColor")
    MEL.connect_material_expressions(vertex_color, "A", custom, "VAlpha")
    MEL.connect_material_expressions(time, "", custom, "Time")
    MEL.connect_material_expressions(strength, "", custom, "Strength")
    MEL.connect_material_expressions(composite, "", custom, "Composite")
    MEL.connect_material_expressions(view_w, "", custom, "ViewW")
    MEL.connect_material_expressions(view_h, "", custom, "ViewH")
    for index, (pin, name) in enumerate(CONSTANT_PINS):
        node = add(material, unreal.MaterialExpressionConstant, -900, 400 + 60 * index)
        node.set_editor_property("r", constant(name))
        MEL.connect_material_expressions(node, "", custom, pin)
    MEL.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Creates the material, or rebuilds its graph in place when SOL_REBUILD is set
def ensure_material(name, builder):
    path = UI_DIR + "/" + name
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("create_hud_shape_material: %s exists (pass -SOLRebuild to rebuild)" % path)
            return
        material = EAL.load_asset(path)
        # delete_all_material_expressions leaves some nodes behind, so delete one by one until none are left
        for _ in range(8):
            expressions = list(MEL.get_material_expressions(material))
            if not expressions:
                break
            for expression in expressions:
                MEL.delete_material_expression(material, expression)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, UI_DIR, unreal.Material,
            unreal.MaterialFactoryNew())
    builder(material)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("create_hud_shape_material: built %s" % path)


ensure_material("M_SOLHudShape", build_shape)
