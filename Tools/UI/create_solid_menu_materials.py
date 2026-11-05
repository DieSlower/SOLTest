# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates the solid menu hull material and its style instances (SDD 46 sections 4.3 and 10, part 46c), headless and
# idempotent:
#   SOL::Paths SOLID_HULL_MATERIAL  M_SOLSolidHull: User Interface domain, translucent. One Custom node draws a signed-
#       distance hull in the element's own pixel space: the position is the normalized UV (TexCoord 4, Slate's
#       MaterialTexCoords) times the element's size in physical pixels (TexCoord 3, the "Pixel Size" output of
#       GetUserInterfaceUV: LocalSize x DrawScale, SlateVertexShader.usf), and the hull lengths (UMG units) become pixels
#       with the DpiScale parameter that USOLSolidMenuSubsystem pushes. The HLSL mirrors SOLSolidMenu::ClampHull and
#       HullDistance (Menu/SOLSolidMenuMath.cpp) and decodes the brush tint as SOLSolidStyle::UnpackVertexTint then
#       SOLSolidMenu::DecodeButtonTint: R hover / edge amount, G three-level code (normal, disabled, pressed), B selected
#       mark, A a continuous dim that Slate applies itself. Slate multiplies a box brush's material colour by its tint
#       (SlateElementPixelShader.usf GetColor), so every tint channel sits at or above TINT_VERTEX_FLOOR and the colour
#       is divided by the tint at the end. Layers: drop shadow (outside the hull, inside the brush rect), brushed grain
#       (raised hulls only), dither, bevel (inverted on a recess), edge highlight, amber accent edge and glow, LED,
#       underline (cyan when selected), the disabled look (desaturated hull, no grain or edge highlight, LED off, no
#       hover) and its cue: (a) a hatched stripe with T_SOLSolidHazard as the mask or (b) a dark slotted recess,
#       chosen by DisabledCueStyle. Two texture samples at most (grain, and the hatch only on a disabled hull).
#   SOL::Paths SOLID_MI_*  one material instance per ESOLSolidStyle (panel, recess, stack button, tab, small button,
#       slider groove, knob, toggle slot, puck) with its shape, look and palette.
# Every default, colour, parameter name and asset path is read from Source/SOLTest/SOLConstants.h (SOL::SolidMenu,
# SOL::SolidMenuMaterialParams, SOL::Paths), so the script cannot drift from the C++. Each asset keeps the MD5 of its
# full spec (HLSL, parameters, values) in a metadata tag: an unchanged run writes no .uasset; a changed spec rebuilds the
# asset in place; -SOLRebuild forces a rebuild.
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
#       -script="<abs path>/Tools/UI/create_solid_menu_materials.py" -unattended -nosplash -AllowCommandletRendering
#       [-SOLRebuild]
#   (-AllowCommandletRendering gives the commandlet the D3D12 RHI, so the HLSL is really compiled and its errors fail
#   the run; with the null RHI no shader map is built and the script only warns. Add -stdout -FullStdOutLogOutput to
#   see the SOLSolidMaterials lines)
import hashlib
import os
import re

import unreal

HASH_TAG = "SOLSpecMD5"
DONE_MARKER = "DONE"
SCRIPT_VERSION = "v1"  # bump to force a rebuild when the build code (not the spec) changes

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
CONSTANTS_FILE = os.path.join(PROJECT_DIR, "Source", "SOLTest", "SOLConstants.h")
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))

with open(CONSTANTS_FILE, "r", encoding="utf-8") as _handle:
    CONSTANTS_TEXT = _handle.read()


# Logs one line with the script's prefix
def log(message):
    unreal.log("SOLSolidMaterials: " + message)


# Returns the text of a namespace block of SOLConstants.h (from its opening line to its closing brace at that indent)
def namespace_block(name):
    match = re.search(r"\n(\s*)namespace " + name + r"\s*\n", CONSTANTS_TEXT)
    if match is None:
        raise RuntimeError("SOLSolidMaterials: namespace %s not found in SOLConstants.h" % name)
    indent = match.group(1).lstrip("\n")
    end = CONSTANTS_TEXT.find("\n" + indent + "}", match.end())
    return CONSTANTS_TEXT[match.end():end]


SOLID_BLOCK = namespace_block("SolidMenu")
PARAMS_BLOCK = namespace_block("SolidMenuMaterialParams")
PATHS_BLOCK = namespace_block("Paths")


# Returns a numeric constant of a block (default: SOL::SolidMenu)
def constant(name, block=None):
    match = re.search(r"\b" + name + r"\s*=\s*([-0-9.eE+]+)", block if block is not None else SOLID_BLOCK)
    if match is None:
        raise RuntimeError("SOLSolidMaterials: %s not found in SOLConstants.h" % name)
    return float(match.group(1))


# Returns an sRGB FColor constant of SOL::SolidMenu as a linear colour (the exact sRGB curve)
def color(name):
    match = re.search(r"\b" + name + r"\(0x([0-9A-Fa-f]{2}),\s*0x([0-9A-Fa-f]{2}),\s*0x([0-9A-Fa-f]{2})\)", SOLID_BLOCK)
    if match is None:
        raise RuntimeError("SOLSolidMaterials: colour %s not found in SOL::SolidMenu" % name)

    # sRGB byte to linear
    def linear(byte):
        c = int(byte, 16) / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return tuple(round(linear(match.group(index)), 6) for index in (1, 2, 3))


# Returns a TEXT("...") string constant of a block
def text_constant(name, block):
    match = re.search(r"\b" + name + r'\s*=\s*TEXT\("([^"]+)"\)', block)
    if match is None:
        raise RuntimeError("SOLSolidMaterials: %s not found in SOLConstants.h" % name)
    return match.group(1)


# Returns a parameter name of SOL::SolidMenuMaterialParams
def param(name):
    return text_constant(name, PARAMS_BLOCK)


HULL_HLSL = r"""// SOL solid hull (SDD 46 sections 4.3, 5 and 8): SOLSolidMenu::ClampHull, HullDistance and DecodeButtonTint mirrored
const float kInvSqrt2 = 0.70710678;
const float k = max(Dpi, 0.01);

// The brush tint, unpacked from the floor (SOLSolidStyle::UnpackVertexTint) and decoded (SOLSolidMenu::DecodeButtonTint).
// UE 5.8 Slate shades in linear space: SlateVertexShader.usf turns the sRGB vertex colour linear and the pixel shader
// gamma-corrects the output, so the tint arrives linear and the colour leaves linear. (SOURCE_IN_LINEAR_SPACE is
// defined in SlateShaderCommon.ush after Material.ush is included, so this code cannot test it: no #if here.)
const float3 lin = max(VColor.rgb, 0.0);
const float3 code = saturate((lin - TintFloor) / max(1.0 - TintFloor, 1e-3));
const bool pressed = code.g >= PressedGMin;
const bool disabled = !pressed && code.g >= DisabledGMin;
const bool mark = code.b >= MarkBMin;
const float hover = disabled ? 0.0 : code.r;

// The hull rect in pixels: inset in the brush rect (room for the shadow); a puck is narrower and sits right when marked
const float2 full = max(PxSize, 1.0);
const float inset = max(Inset, 0.0) * k;
const float shift = saturate(MarkShift);
const float spanW = full.x - 2.0 * inset;
const float w = spanW * (1.0 - shift);
const float h = full.y - 2.0 * inset;
if (w <= 0.0 || h <= 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
const float2 pAll = UV * full;
const float2 p = pAll - float2(inset + (mark ? spanW * shift : 0.0), inset);

// ClampHull: the chamfers scaled by one factor so each edge's pair sums to at most MaxChamfer of it
float tl = max(ChamferTL, 0.0) * k;
float tr = max(ChamferTR, 0.0) * k;
float br = max(ChamferBR, 0.0) * k;
float bl = max(ChamferBL, 0.0) * k;
const float f = min(min(min(1.0, MaxChamfer * w / max(tl + tr, 1e-6)), MaxChamfer * h / max(tr + br, 1e-6)),
    min(MaxChamfer * w / max(br + bl, 1e-6), MaxChamfer * h / max(bl + tl, 1e-6)));
tl *= f;
tr *= f;
br *= f;
bl *= f;

// ClampHull: the title step and the chin clear of the chamfers, removed whole when left with no width or height
float sw = max(StepW, 0.0) * k;
float sh = max(StepH, 0.0) * k;
if (sw > 0.0 && sh > 0.0)
{
    sh = min(min(sh, MaxFeature * h), w - tl - tr);
    sw = sw < tl ? tl : min(sw, w - tr - sh);
}
if (!(sw > 0.0 && sh > 0.0))
{
    sw = 0.0;
    sh = 0.0;
}
float cw = max(ChinW, 0.0) * k;
float ch = max(ChinH, 0.0) * k;
if (cw > 0.0 && ch > 0.0)
{
    ch = min(min(ch, MaxFeature * h), w - bl - br);
    cw = cw < bl ? bl : min(cw, w - br - ch);
}
if (!(cw > 0.0 && ch > 0.0))
{
    cw = 0.0;
    ch = 0.0;
}

// ClampHull: the notches inside the straight part both sides share, at most half as deep as tall
float nd = max(NotchD, 0.0) * k;
float nh = max(NotchH, 0.0) * k;
float ny = NotchY * k;
bool notch = false;
if (NotchY >= 0.0 && nd > 0.0 && nh > 0.0)
{
    const float lo = max(tl, sh + tr);
    const float hi = min(h - bl, h - ch - br);
    nh = min(nh, hi - lo);
    if (nh > 0.0)
    {
        ny = ny < lo + 0.5 * nh ? lo + 0.5 * nh : min(ny, hi - 0.5 * nh);
        nd = min(min(nd, 0.5 * nh), MaxFeature * w);
        notch = true;
    }
}

// HullDistance: the chamfered rect's half-planes, minus the step, chin and notch cuts (px, negative inside)
float d = max(max(-p.x, p.x - w), max(-p.y, p.y - h));
d = max(d, (tl - p.x - p.y) * kInvSqrt2);
d = max(d, (tr - w + p.x - p.y + sh) * kInvSqrt2);
d = max(d, (br - w + p.x - h + ch + p.y) * kInvSqrt2);
d = max(d, (bl - p.x - h + p.y) * kInvSqrt2);
if (sh > 0.0)
{
    d = max(d, -max(p.y - sh, (sw - p.x + p.y) * kInvSqrt2));
}
if (ch > 0.0)
{
    d = max(d, -max(h - ch - p.y, (cw + h - p.x - p.y) * kInvSqrt2));
}
if (notch)
{
    const float top = ny - 0.5 * nh;
    const float bottom = ny + 0.5 * nh;
    const float q = w - p.x;
    d = max(d, -max(max(p.x - nd, (p.x - p.y + top) * kInvSqrt2), (p.x + p.y - bottom) * kInvSqrt2));
    d = max(d, -max(max(q - nd, (q - p.y + top) * kInvSqrt2), (q + p.y - bottom) * kInvSqrt2));
}

// Outward normal from the screen derivatives (bevel light); coverage with a one pixel edge
float2 n = float2(ddx(d), ddy(d));
n *= rsqrt(max(dot(n, n), 1e-8));
const float lit = dot(n, float2(LightX, LightY));
const float cov = saturate(0.5 - d);
const float inside = -d;
const bool recess = Recess >= 0.5;

// Hull colour by state: rest, lifted with the hover amount, pressed, disabled
float3 c = lerp(BaseColor, LiftedColor, hover);
c = pressed ? PressedColor : (disabled ? DisabledColor : c);

// Brushed grain on raised hulls only (text sits on flat recess fills), none when disabled; dither against banding
if (!recess && !disabled && GrainAmount > 0.0)
{
    const float g = Texture2DSampleLevel(GrainTex, GrainTexSampler, pAll / max(GrainTile * k, 1.0), 0.0).r;
    c *= 1.0 + GrainAmount * (2.0 * g - 1.0);
}
// The dither is +-0.5 / 255 of the sRGB output: in linear it scales by dLinear / dsRGB ~ 2.2 x c^0.545 (a flat
// +-0.5 / 255 in linear would be about +-2 output levels on the dark hull colours)
const float3 ditherScale = 2.2 * pow(max(c, 1e-4), 0.545);
c += (frac(sin(dot(floor(pAll), float2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0 * ditherScale;

// Bevel band inside the edge: lit top-left on a raised hull, inverted (inner shadow, lit lower lip) on a recess
const float band = 1.0 - saturate(inside / max(BevelPx * k, 1e-3));
c *= 1.0 + BevelAmount * band * lit * (recess ? -1.0 : 1.0) * (disabled ? 0.5 : 1.0);

// The edge line just inside the hull: the lit edge highlight (not when disabled), then the amber accent edge
const float ew = max(EdgePx * k, 0.5);
const float edgeLine = saturate(min(inside + 0.5, ew + 0.5 - inside));
if (!disabled)
{
    c = lerp(c, EdgeColor, EdgeHighlight * edgeLine * saturate(0.5 + 0.5 * lit));
}
c = lerp(c, AccentColor, hover * edgeLine);

// Hover glow inside the edge: s x Edge (SOLSolidMenu::EvaluateButton's Glow), so none at strength 0, doubled at 2
c += AccentColor * (Strength * hover * GlowGain * exp(-max(inside, 0.0) / max(GlowPx * k, 1e-3)));

// LED (LedX < 0: none; centred past the middle): cyan when selected, amber with the hover amount, off when disabled
if (LedX >= 0.0)
{
    const float r = max(LedRadius * k, 0.5);
    const float ld = length(p - float2(min(LedX * k, 0.5 * w), 0.5 * h)) - r;
    const float3 led = disabled ? LedOffColor : (mark ? MarkColor : lerp(LedOffColor, AccentColor, hover));
    c = lerp(c, led, saturate(0.5 - ld));
    const float ledLit = disabled ? 0.0 : (mark ? 1.0 : hover);
    c += led * (ledLit * Strength * GlowGain * exp(-max(ld, 0.0) / r));
}

// Underline (UnderlineBottom < 0: none): cyan when selected (over the hover), amber with the hover amount
if (UnderBottom >= 0.0 && !disabled)
{
    const float y1 = h - UnderBottom * k;
    const float y0 = y1 - max(UnderPx * k, 1.0);
    const float ux = min(p.x - UnderStart * k, w - UnderEnd * k - p.x);
    const float uy = min(p.y - y0, y1 - p.y);
    const float ucov = saturate(min(ux, uy) + 0.5);
    c = lerp(c, mark ? MarkColor : AccentColor, ucov * (mark ? 1.0 : hover));
}

// Disabled cue in a band above the bottom edge: (a) hatched stripe (the hazard stripe mask), (b) dark slotted recess
if (disabled)
{
    const float y1 = h - 2.0 * ew;
    const float y0 = y1 - max(CueBand * k, 1.0);
    const float clear = saturate(inside - ew + 0.5);
    const float bandCov = saturate(min(p.y - y0, y1 - p.y) + 0.5) * clear;
    if (CueStyle < 0.5)
    {
        const float period = max(CuePeriod * k, 1.0);
        const float m = Texture2DSampleLevel(CueTex, CueTexSampler, float2(p.x / (CuePeriodsX * period), p.y / period),
            0.0).r;
        c = lerp(c, CueColor, bandCov * m);
    }
    else
    {
        c = lerp(c, ShadowColor, bandCov);
        const float lipCov = saturate(min(p.y - y1 + 0.5, y1 + max(k, 1.0) + 0.5 - p.y)) * clear;
        c = lerp(c, EdgeColor, CueLip * lipCov);
    }
}

// Composite with the soft drop shadow outside the hull (inside the brush rect)
const float shadowA = d > 0.0 ? ShadowOpacity * (1.0 - saturate(d / max(ShadowPx * k, 1e-3))) : 0.0;
const float a = cov + (1.0 - cov) * shadowA;
float3 o = (c * cov + ShadowColor * ((1.0 - cov) * shadowA)) / max(a, 1e-4);

// Slate multiplies this colour by the tint (the state code): divide it out; the tint's alpha stays the dim / fade
o /= max(VColor.rgb, 1e-3);
return float4(o, a);"""

# Custom node pins fed by scalar parameters: (pin, SOL::SolidMenuMaterialParams constant, default)
SCALAR_PINS = [
    ("Strength", "EFFECTS_STRENGTH", constant("HUD_EFFECTS_DEFAULT", namespace_block("Settings"))),
    ("Dpi", "DPI_SCALE", 1.0),
    ("ChamferTL", "CHAMFER_TL", constant("PANEL_CHAMFER_TL")),
    ("ChamferTR", "CHAMFER_TR", constant("PANEL_CHAMFER_TR")),
    ("ChamferBR", "CHAMFER_BR", constant("PANEL_CHAMFER_BR")),
    ("ChamferBL", "CHAMFER_BL", constant("PANEL_CHAMFER_BL")),
    ("StepW", "TITLE_STEP_WIDTH", 0.0),
    ("StepH", "TITLE_STEP_HEIGHT", 0.0),
    ("NotchY", "NOTCH_Y", -1.0),
    ("NotchD", "NOTCH_DEPTH", 0.0),
    ("NotchH", "NOTCH_HEIGHT", 0.0),
    ("ChinW", "CHIN_WIDTH", 0.0),
    ("ChinH", "CHIN_HEIGHT", 0.0),
    ("Recess", "RECESS", 0.0),
    ("Inset", "INSET", constant("PANEL_INSET_PX")),
    ("MarkShift", "MARK_SHIFT", 0.0),
    ("ShadowPx", "SHADOW_PX", constant("SHADOW_PX")),
    ("ShadowOpacity", "SHADOW_OPACITY", constant("SHADOW_OPACITY")),
    ("GrainAmount", "GRAIN_AMOUNT", constant("GRAIN_AMOUNT")),
    ("GrainTile", "GRAIN_TILE", constant("GRAIN_TILE_PX")),
    ("BevelPx", "BEVEL_PX", constant("BEVEL_PX")),
    ("BevelAmount", "BEVEL_AMOUNT", constant("BEVEL_AMOUNT")),
    ("EdgePx", "EDGE_PX", constant("EDGE_PX")),
    ("EdgeHighlight", "EDGE_HIGHLIGHT", constant("EDGE_HIGHLIGHT")),
    ("GlowPx", "GLOW_PX", constant("GLOW_PX")),
    ("LedX", "LED_X", -1.0),
    ("LedRadius", "LED_RADIUS", constant("LED_RADIUS_PX")),
    ("UnderStart", "UNDERLINE_START", 0.0),
    ("UnderEnd", "UNDERLINE_END", 0.0),
    ("UnderBottom", "UNDERLINE_BOTTOM", -1.0),
    ("UnderPx", "UNDERLINE_PX", constant("UNDERLINE_PX")),
    ("CueStyle", "DISABLED_CUE_STYLE", constant("DISABLED_CUE_STYLE")),
    ("CueBand", "CUE_BAND_PX", constant("CUE_BAND_PX")),
    ("CuePeriod", "CUE_PERIOD_PX", constant("CUE_PERIOD_PX")),
]

# Custom node pins fed by vector parameters (linear colours): (pin, parameter constant, default palette colour)
VECTOR_PINS = [
    ("BaseColor", "BASE_COLOR", "HULL_COLOR"),
    ("LiftedColor", "LIFTED_COLOR", "HULL_COLOR"),
    ("PressedColor", "PRESSED_COLOR", "PRESSED_COLOR"),
    ("DisabledColor", "DISABLED_COLOR", "DISABLED_HULL_COLOR"),
    ("EdgeColor", "EDGE_COLOR", "EDGE_COLOR"),
    ("ShadowColor", "SHADOW_COLOR", "SHADOW_COLOR"),
    ("LedOffColor", "LED_OFF_COLOR", "LED_OFF_COLOR"),
    ("AccentColor", "ACCENT_COLOR", "AMBER_COLOR"),
    ("MarkColor", "MARK_COLOR", "CYAN_COLOR"),
    ("CueColor", "CUE_COLOR", "DISABLED_CUE_COLOR"),
]

# Custom node pins fed by texture object parameters: (pin, parameter constant, SOL::Paths texture constant)
TEXTURE_PINS = [
    ("GrainTex", "GRAIN_TEXTURE", "SOLID_TEX_BRUSHED"),
    ("CueTex", "CUE_MASK", "SOLID_TEX_HAZARD"),
]

# Custom node pins fed by constants: (pin, value)
CONSTANT_PINS = [
    ("TintFloor", constant("TINT_VERTEX_FLOOR")),
    ("PressedGMin", constant("TINT_PRESSED_G_MIN")),
    ("DisabledGMin", constant("TINT_DISABLED_G_MIN")),
    ("MarkBMin", constant("TINT_MARK_B_MIN")),
    ("MaxChamfer", constant("HULL_MAX_CHAMFER_FRACTION")),
    ("MaxFeature", constant("HULL_MAX_FEATURE_FRACTION")),
    ("LightX", constant("LIGHT_DIR_X")),
    ("LightY", constant("LIGHT_DIR_Y")),
    ("GlowGain", constant("GLOW_GAIN")),
    ("CuePeriodsX", constant("CUE_MASK_PERIODS_X")),
    ("CueLip", constant("CUE_LIP")),
]


# Returns the per-style overrides: scalar and vector parameter constant names to values (UMG units, linear colours)
def style_overrides():
    raised = {"BASE_COLOR": color("RAISED_COLOR"), "LIFTED_COLOR": color("RAISED_LIFTED_COLOR")}
    sunken = {"BASE_COLOR": color("BASE_COLOR"), "LIFTED_COLOR": color("BASE_COLOR")}
    flat = {"INSET": 0.0, "SHADOW_OPACITY": 0.0}

    # Chamfers of every corner at one size
    def corners(size):
        return {"CHAMFER_TL": size, "CHAMFER_TR": size, "CHAMFER_BR": size, "CHAMFER_BL": size}

    stack = dict(flat, **raised)
    stack.update({"CHAMFER_TL": constant("BUTTON_CUT_PX"), "CHAMFER_TR": constant("BUTTON_POINT_PX"),
                  "CHAMFER_BR": constant("BUTTON_POINT_PX"), "CHAMFER_BL": 0.0,
                  "LED_X": constant("BUTTON_LED_X_PX"), "UNDERLINE_START": constant("BUTTON_UNDERLINE_START_PX"),
                  "UNDERLINE_END": constant("BUTTON_UNDERLINE_END_PX"),
                  "UNDERLINE_BOTTOM": constant("BUTTON_UNDERLINE_BOTTOM_PX")})
    small = dict(flat, **raised)
    small.update({"CHAMFER_TL": constant("SMALL_BUTTON_CUT_PX"), "CHAMFER_TR": 0.0, "CHAMFER_BR": 0.0,
                  "CHAMFER_BL": 0.0, "LED_X": constant("SMALL_BUTTON_LED_X_PX"),
                  "UNDERLINE_START": constant("SMALL_BUTTON_UNDERLINE_START_PX"),
                  "UNDERLINE_END": constant("SMALL_BUTTON_UNDERLINE_END_PX"),
                  "UNDERLINE_BOTTOM": constant("SMALL_BUTTON_UNDERLINE_BOTTOM_PX")})
    recess = dict(flat, **sunken)
    recess.update({"RECESS": 1.0, "GRAIN_AMOUNT": 0.0, "CHAMFER_TL": constant("RECESS_CHAMFER_LARGE_PX"),
                   "CHAMFER_TR": constant("RECESS_CHAMFER_SMALL_PX"), "CHAMFER_BR": constant("RECESS_CHAMFER_LARGE_PX"),
                   "CHAMFER_BL": constant("RECESS_CHAMFER_SMALL_PX")})
    groove = dict(flat, **sunken)
    groove.update({"RECESS": 1.0, "GRAIN_AMOUNT": 0.0}, **corners(constant("GROOVE_CHAMFER_PX")))
    knob = dict(flat, **raised)
    knob.update({"LED_X": constant("LED_CENTRE_X")}, **corners(constant("KNOB_CHAMFER_PX")))
    slot = dict(flat, **sunken)
    slot.update({"RECESS": 1.0, "GRAIN_AMOUNT": 0.0}, **corners(constant("TOGGLE_CHAMFER_PX")))
    puck = dict(flat, **raised)
    puck.update({"LED_X": constant("LED_CENTRE_X"), "MARK_SHIFT": constant("PUCK_TRAVEL_FRACTION")},
                **corners(constant("PUCK_CHAMFER_PX")))
    return [
        ("SOLID_MI_PANEL", {"BASE_COLOR": color("HULL_COLOR"), "LIFTED_COLOR": color("HULL_COLOR")}),
        ("SOLID_MI_RECESS", recess),
        ("SOLID_MI_STACK_BUTTON", stack),
        ("SOLID_MI_TAB", dict(stack)),
        ("SOLID_MI_SMALL_BUTTON", small),
        ("SOLID_MI_SLIDER_GROOVE", groove),
        ("SOLID_MI_KNOB", knob),
        ("SOLID_MI_TOGGLE_SLOT", slot),
        ("SOLID_MI_PUCK", puck),
    ]


# Returns the MD5 of a spec (any repr-able structure)
def spec_hash(spec):
    return hashlib.md5((SCRIPT_VERSION + repr(spec)).encode("utf-8")).hexdigest()


# Splits a package path into its directory and asset name
def split_path(package_path):
    directory, name = package_path.rsplit("/", 1)
    return directory, name


# Returns the asset at a package path when it must be (re)built, creating it when missing; None when it is current
def asset_to_build(package_path, asset_class, factory, tag):
    directory, name = split_path(package_path)
    if EAL.does_asset_exist(package_path):
        asset = EAL.load_asset(package_path)
        if asset is not None and not REBUILD and EAL.get_metadata_tag(asset, HASH_TAG) == tag:
            log("%s unchanged, skipped" % name)
            return None
        if asset is not None:
            return asset
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, asset_class, factory)
    if asset is None:
        raise RuntimeError("SOLSolidMaterials: could not create %s" % package_path)
    return asset


# Adds an expression of a class at an editor position
def add(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


# Builds the hull material graph: the Custom node with its UV, size, tint, parameter and constant inputs
def build_hull(material):
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    custom = add(material, unreal.MaterialExpressionCustom, -400, 0)
    custom.set_editor_property("description", "SOLSolidHull")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("code", HULL_HLSL)
    pins = (["UV", "PxSize", "VColor"] + [pin for pin, _, _ in SCALAR_PINS] + [pin for pin, _, _ in VECTOR_PINS]
            + [pin for pin, _, _ in TEXTURE_PINS] + [pin for pin, _ in CONSTANT_PINS])
    inputs = []
    for name in pins:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)

    # Slate's built-in UV sets (GetUserInterfaceUV): 4 the element's 0-1 UV, 3 its size in physical pixels
    uv = add(material, unreal.MaterialExpressionTextureCoordinate, -1400, -600)
    uv.set_editor_property("coordinate_index", 4)
    size = add(material, unreal.MaterialExpressionTextureCoordinate, -1400, -500)
    size.set_editor_property("coordinate_index", 3)
    vertex_color = add(material, unreal.MaterialExpressionVertexColor, -1400, -400)
    MEL.connect_material_expressions(uv, "", custom, "UV")
    MEL.connect_material_expressions(size, "", custom, "PxSize")
    MEL.connect_material_expressions(vertex_color, "", custom, "VColor")

    # Parameters: scalars, linear colours, textures
    y = -300
    for pin, name, value in SCALAR_PINS:
        node = add(material, unreal.MaterialExpressionScalarParameter, -1400, y)
        node.set_editor_property("parameter_name", param(name))
        node.set_editor_property("default_value", value)
        MEL.connect_material_expressions(node, "", custom, pin)
        y += 60
    for pin, name, palette in VECTOR_PINS:
        node = add(material, unreal.MaterialExpressionVectorParameter, -1400, y)
        node.set_editor_property("parameter_name", param(name))
        r, g, b = color(palette)
        node.set_editor_property("default_value", unreal.LinearColor(r, g, b, 1.0))
        MEL.connect_material_expressions(node, "RGB", custom, pin)
        y += 120
    for pin, name, path_name in TEXTURE_PINS:
        texture_path = text_constant(path_name, PATHS_BLOCK)
        texture = EAL.load_asset(texture_path)
        if texture is None:
            raise RuntimeError("SOLSolidMaterials: %s is missing (run import_solid_menu_assets.py)" % texture_path)
        node = add(material, unreal.MaterialExpressionTextureObjectParameter, -1400, y)
        node.set_editor_property("parameter_name", param(name))
        node.set_editor_property("texture", texture)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        MEL.connect_material_expressions(node, "", custom, pin)
        y += 160
    for pin, value in CONSTANT_PINS:
        node = add(material, unreal.MaterialExpressionConstant, -900, y)
        node.set_editor_property("r", value)
        MEL.connect_material_expressions(node, "", custom, pin)
        y += 60

    # Final colour and opacity from the Custom node's float4
    rgb = add(material, unreal.MaterialExpressionComponentMask, -150, -60)
    for channel, enabled in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(channel, enabled)
    alpha = add(material, unreal.MaterialExpressionComponentMask, -150, 60)
    for channel, enabled in (("r", False), ("g", False), ("b", False), ("a", True)):
        alpha.set_editor_property(channel, enabled)
    MEL.connect_material_expressions(custom, "", rgb, "")
    MEL.connect_material_expressions(custom, "", alpha, "")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)


# Deletes every expression of a material (delete_all_material_expressions leaves some behind, so one by one)
def clear_material(material):
    for _ in range(8):
        expressions = list(MEL.get_material_expressions(material))
        if not expressions:
            break
        for expression in expressions:
            MEL.delete_material_expression(material, expression)


# Creates or rebuilds M_SOLSolidHull when its spec changed; returns (material, spec hash)
def ensure_hull():
    path = text_constant("SOLID_HULL_MATERIAL", PATHS_BLOCK)
    spec = (HULL_HLSL, SCALAR_PINS, [(pin, name, color(palette)) for pin, name, palette in VECTOR_PINS],
            [(pin, name, text_constant(tex, PATHS_BLOCK)) for pin, name, tex in TEXTURE_PINS], CONSTANT_PINS,
            [param(name) for _, name, _ in SCALAR_PINS + VECTOR_PINS + TEXTURE_PINS])
    tag = spec_hash(spec)
    material = asset_to_build(path, unreal.Material, unreal.MaterialFactoryNew(), tag)
    if material is None:
        return EAL.load_asset(path), tag
    clear_material(material)
    build_hull(material)
    # Compile for the editor's shader platform and wait for it (get_statistics finishes the compilation); a second
    # recompile returns the finished resource's errors. The statistics have no Slate representative shader, so they
    # only show the samplers and UV scalars of a successful compile
    MEL.recompile_material(material)
    stats = MEL.get_statistics(material)
    errors = list(MEL.recompile_material(material))
    MEL.get_statistics(material)
    log("M_SOLSolidHull compiled: %d samplers, %d estimated texture samples, %d UV scalars, %d errors" % (
        stats.num_samplers, stats.num_pixel_texture_samples, stats.num_uv_scalars, len(errors)))
    if errors:
        for error in errors:
            unreal.log_error("SOLSolidMaterials: " + error)
        raise RuntimeError("SOLSolidMaterials: M_SOLSolidHull did not compile")
    if stats.num_samplers == 0:
        unreal.log_warning("SOLSolidMaterials: no shader map was compiled (null RHI): add -AllowCommandletRendering "
                           "to check the HLSL")
    EAL.set_metadata_tag(material, HASH_TAG, tag)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    log("M_SOLSolidHull built")
    return material, tag


# Creates or rebuilds one style instance when its spec (or its parent) changed
def ensure_instance(parent, parent_tag, path_name, overrides):
    path = text_constant(path_name, PATHS_BLOCK)
    tag = spec_hash((parent_tag, sorted(overrides.items())))
    instance = asset_to_build(path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew(), tag)
    if instance is None:
        return
    MEL.set_material_instance_parent(instance, parent)
    MEL.clear_all_material_instance_parameters(instance)
    for name, value in sorted(overrides.items()):
        if isinstance(value, tuple):
            MEL.set_material_instance_vector_parameter_value(instance, param(name),
                                                             unreal.LinearColor(value[0], value[1], value[2], 1.0))
        else:
            MEL.set_material_instance_scalar_parameter_value(instance, param(name), value)
    MEL.update_material_instance(instance)
    EAL.set_metadata_tag(instance, HASH_TAG, tag)
    EAL.save_loaded_asset(instance, only_if_is_dirty=False)
    log("%s built (%d overrides)" % (split_path(path)[1], len(overrides)))


# Builds the hull material, then its style instances, each only when its spec changed
def main():
    parent, parent_tag = ensure_hull()
    for path_name, overrides in style_overrides():
        ensure_instance(parent, parent_tag, path_name, overrides)
    log(DONE_MARKER)


main()
