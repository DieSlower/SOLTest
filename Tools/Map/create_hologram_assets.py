# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates (or, with SOL_REBUILD, rebuilds in place) the map hologram materials (issue #20, SDD 20 section 2.4,
# plan step 6), idempotent:
#   /Game/SOL/Map/M_SOLHologramBody
#       Unlit, additive, after motion blur (unjittered, outside TSR's history, so the 1 px scanlines and the contour
#       lines stay crisp while the map camera moves). LT hologram fragment: Fresnel rim 1 - |N.V| plus contour lines
#       (latitude and longitude of the body-local normal, CONTOUR_GAIN exp(-CONTOUR_SHARPNESS d) with d in pixels)
#       x time noise 1 - NOISE_LOG_SCALE log(1 - noise) per 2 px row x scanlines 1 + SCANLINE_AMPLITUDE sin(PI y),
#       times HoloColor x COLOR_GAIN x HoloIntensity.
#   /Game/SOL/Map/M_SOLHologramRing
#       The same look on a two-sided plane: an annulus between RingInnerFraction and 1 (of the plane's half size) with
#       RING_FILL base glow and RING_BANDS contour bands across it.
#   /Game/SOL/Map/M_SOLHologramPost
#       Post-process after tonemapping: a tint toward Primary, 1 px scanlines, a global flicker and a small jitter of a
#       few 2 px rows, all scaled by Strength (sol.HologramPostStrength).
# Every default is read from Source/SOLTest/SOLConstants.h (SOL::Hologram); the parameter names must match
# SOL::HologramMaterialParams and the asset paths SOL::Paths::HOLOGRAM_*.
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
#       -script="<abs path>/Tools/Map/create_hologram_assets.py" -unattended [-SOLRebuild]
# or, in an open editor, exec() this file with SOL_REBUILD = True defined in the calling scope's globals.
import os
import re

import unreal

MAP_DIR = "/Game/SOL/Map"
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))
CONSTANTS_FILE = os.path.join(unreal.Paths.project_dir(), "Source", "SOLTest", "SOLConstants.h")

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


# Returns a numeric SOL:: constant from SOLConstants.h, so the material defaults cannot drift from the C++
def constant(name):
    text = open(CONSTANTS_FILE, encoding="utf-8").read()
    match = re.search(r"\b" + name + r"\s*=\s*([-0-9.eE+]+)", text)
    if match is None:
        raise RuntimeError("create_hologram_assets: %s not found in %s" % (name, CONSTANTS_FILE))
    return float(match.group(1))


# Shared HLSL: per-row time noise, scanline and contour helpers (inlined into each Custom node)
NOISE_HLSL = r"""
const float2 cell = float2(floor(Parameters.SvPosition.y * 0.5), floor(fmod(Time, 3600.0) * NoiseRate));
const float hash = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
const float noise = 1.0 - NoiseLog * log(1.0 - 0.999 * hash);
const float scan = 1.0 + ScanAmp * sin(3.14159265 * Parameters.SvPosition.y);
"""

BODY_HLSL = r"""// LT hologram body (SDD 20 2.4)
const float3 N = normalize(NormalWS);
const float3 V = normalize(CameraVec);
const float rim = 1.0 - abs(dot(N, V));
const float3 L = normalize(LocalNormal);
const float xLat = (asin(clamp(L.z, -1.0, 1.0)) / 3.14159265 + 0.5) * LatBands;
const float xLon = (atan2(L.y, L.x) / 6.2831853 + 0.5) * LonBands;
const float dLat = abs(frac(xLat + 0.5) - 0.5) / max(fwidth(xLat), 1e-5);
const float dLon = abs(frac(xLon + 0.5) - 0.5) / max(fwidth(xLon), 1e-5);
const float contour = ContourGain * exp(-ContourSharp * 0.5 * min(dLat, dLon));
""" + NOISE_HLSL + r"""
const float shape = rim + ContourAmount * contour * (0.25 + rim);
return Color.rgb * ColorGain * Intensity * shape * noise * scan;"""

RING_HLSL = r"""// LT hologram ring disc (SDD 20 2.4)
const float r = length(UV - 0.5) * 2.0;
const float aa = max(fwidth(r), 1e-5);
const float mask = saturate((r - Inner) / aa) * saturate((1.0 - r) / aa);
const float x = saturate((r - Inner) / max(1.0 - Inner, 1e-5)) * Bands;
const float d = abs(frac(x + 0.5) - 0.5) / max(fwidth(x), 1e-5);
const float contour = ContourGain * exp(-ContourSharp * 0.5 * d);
""" + NOISE_HLSL + r"""
return Color.rgb * ColorGain * Intensity * mask * (Fill + 0.5 * contour) * noise * scan;"""

POST_UV_HLSL = r"""// Row jitter: a few 2 px rows shifted sideways by up to JitterPx, re-drawn 10 times a second
const float row = floor(Parameters.SvPosition.y * 0.5);
const float hash = frac(sin(dot(float2(row, floor(fmod(Time, 3600.0) * 10.0)), float2(12.9898, 78.233))) * 43758.5453);
const float hash2 = frac(hash * 7.123 + 0.37);
const float shift = hash < JitterRows ? (hash2 * 2.0 - 1.0) * JitterPx * Strength : 0.0;
return UV + float2(shift / max(ViewSize.x, 1.0), 0.0);"""

POST_COLOR_HLSL = r"""// Tint toward the hologram colour (luminance kept), 1 px scanlines and a global flicker
const float3 lumWeights = float3(0.2126, 0.7152, 0.0722);
const float lum = dot(Scene.rgb, lumWeights);
const float3 tintN = Tint.rgb / max(dot(Tint.rgb, lumWeights), 1e-3);
const float3 tinted = lerp(Scene.rgb, lum * tintN, saturate(TintAmount * Strength));
const float scan = 1.0 - ScanAmount * Strength * (0.5 + 0.5 * sin(3.14159265 * Parameters.SvPosition.y));
const float f = frac(sin(floor(fmod(Time, 3600.0) * 15.0) * 91.345) * 47453.5453);
const float flicker = 1.0 + FlickerAmount * Strength * (2.0 * f - 1.0);
return saturate(tinted) * scan * flicker;"""


# Adds an expression of the given class at an editor position
def add(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


# Adds a scalar parameter with a default
def scalar(material, name, value, x, y):
    node = add(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


# Adds a vector parameter with a default
def vector(material, name, value, x, y):
    node = add(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


# Adds a Custom node with the given inputs, code and output type
def add_custom(material, x, y, description, code, output_type, input_names):
    node = add(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("description", description)
    node.set_editor_property("output_type", output_type)
    node.set_editor_property("code", code)
    inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    node.set_editor_property("inputs", inputs)
    return node


# Connects the shared noise and scanline inputs (Time, NoiseRate, NoiseLog, ScanAmp) and the style constants
def connect_style(material, custom, color, intensity):
    time = add(material, unreal.MaterialExpressionTime, -1200, 400)
    MEL.connect_material_expressions(time, "", custom, "Time")
    MEL.connect_material_expressions(color, "", custom, "Color")
    MEL.connect_material_expressions(intensity, "", custom, "Intensity")
    style = [("NoiseRate", "NOISE_RATE_HZ"), ("NoiseLog", "NOISE_LOG_SCALE"), ("ScanAmp", "SCANLINE_AMPLITUDE"),
             ("ColorGain", "COLOR_GAIN"), ("ContourGain", "CONTOUR_GAIN"), ("ContourSharp", "CONTOUR_SHARPNESS")]
    for index, (pin, name) in enumerate(style):
        node = add(material, unreal.MaterialExpressionConstant, -1200, 500 + 70 * index)
        node.set_editor_property("r", constant(name))
        MEL.connect_material_expressions(node, "", custom, pin)


# Sets an unlit additive translucent material drawn after motion blur
def set_hologram_surface(material, two_sided):
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", two_sided)
    material.set_editor_property("use_translucency_vertex_fog", False)
    material.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)


# Builds the body proxy material graph
def build_body(material):
    set_hologram_surface(material, False)
    color = vector(material, "HoloColor", unreal.LinearColor(0.2, 0.7, 1.0, 1.0), -1600, -300)
    intensity = scalar(material, "HoloIntensity", 1.0, -1600, -150)
    normal = add(material, unreal.MaterialExpressionVertexNormalWS, -1600, 0)
    camera = add(material, unreal.MaterialExpressionCameraVectorWS, -1600, 100)
    local = add(material, unreal.MaterialExpressionTransform, -1300, 0)
    local.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    MEL.connect_material_expressions(normal, "", local, "")
    custom = add_custom(material, -700, 0, "SOLHologramBody", BODY_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["NormalWS", "CameraVec", "LocalNormal", "Color", "Intensity", "Time", "NoiseRate", "NoiseLog", "ScanAmp",
         "ColorGain", "ContourGain", "ContourSharp", "LatBands", "LonBands", "ContourAmount"])
    MEL.connect_material_expressions(normal, "", custom, "NormalWS")
    MEL.connect_material_expressions(camera, "", custom, "CameraVec")
    MEL.connect_material_expressions(local, "", custom, "LocalNormal")
    connect_style(material, custom, color, intensity)
    for index, (pin, name) in enumerate([("LatBands", "CONTOUR_LATITUDE_BANDS"),
                                         ("LonBands", "CONTOUR_LONGITUDE_BANDS"),
                                         ("ContourAmount", "CONTOUR_AMOUNT")]):
        node = add(material, unreal.MaterialExpressionConstant, -1000, 500 + 70 * index)
        node.set_editor_property("r", constant(name))
        MEL.connect_material_expressions(node, "", custom, pin)
    MEL.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Builds the ring disc material graph
def build_ring(material):
    set_hologram_surface(material, True)
    color = vector(material, "HoloColor", unreal.LinearColor(0.2, 0.7, 1.0, 1.0), -1600, -300)
    intensity = scalar(material, "HoloIntensity", 1.0, -1600, -150)
    inner = scalar(material, "RingInnerFraction", 0.5, -1600, 0)
    uv = add(material, unreal.MaterialExpressionTextureCoordinate, -1600, 100)
    custom = add_custom(material, -700, 0, "SOLHologramRing", RING_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["UV", "Inner", "Color", "Intensity", "Time", "NoiseRate", "NoiseLog", "ScanAmp", "ColorGain", "ContourGain",
         "ContourSharp", "Bands", "Fill"])
    MEL.connect_material_expressions(uv, "", custom, "UV")
    MEL.connect_material_expressions(inner, "", custom, "Inner")
    connect_style(material, custom, color, intensity)
    for index, (pin, name) in enumerate([("Bands", "RING_BANDS"), ("Fill", "RING_FILL")]):
        node = add(material, unreal.MaterialExpressionConstant, -1000, 500 + 70 * index)
        node.set_editor_property("r", constant(name))
        MEL.connect_material_expressions(node, "", custom, pin)
    MEL.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Builds the map post-process material graph
def build_post(material):
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    location = getattr(unreal.BlendableLocation, "BL_SCENE_COLOR_AFTER_TONEMAPPING", None)
    if location is None:
        location = unreal.BlendableLocation.BL_AFTER_TONEMAPPING
    material.set_editor_property("blendable_location", location)
    strength = scalar(material, "Strength", constant("POST_STRENGTH_DEFAULT"), -1600, -300)
    # Default only: ASOLHologramVisuals sets Tint from SOL::Hologram::PROXY_COLOR_BODY
    tint = vector(material, "Tint", unreal.LinearColor(0.2, 0.7, 1.0, 1.0), -1600, -150)
    tint_amount = scalar(material, "TintAmount", constant("POST_TINT"), -1600, 0)
    scan_amount = scalar(material, "ScanAmount", constant("POST_SCANLINE"), -1600, 100)
    flicker_amount = scalar(material, "FlickerAmount", constant("POST_FLICKER"), -1600, 200)
    jitter_px = scalar(material, "JitterPx", constant("POST_JITTER_PX"), -1600, 300)
    jitter_rows = scalar(material, "JitterRows", constant("POST_JITTER_ROW_FRACTION"), -1600, 400)
    time = add(material, unreal.MaterialExpressionTime, -1600, 500)
    uv = add(material, unreal.MaterialExpressionTextureCoordinate, -1600, 600)
    view_size = add(material, unreal.MaterialExpressionViewSize, -1300, 600)
    uv_custom = add_custom(material, -1000, 400, "SOLHologramPostUV", POST_UV_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["UV", "ViewSize", "Time", "JitterPx", "JitterRows", "Strength"])
    MEL.connect_material_expressions(uv, "", uv_custom, "UV")
    MEL.connect_material_expressions(view_size, "", uv_custom, "ViewSize")
    MEL.connect_material_expressions(time, "", uv_custom, "Time")
    MEL.connect_material_expressions(jitter_px, "", uv_custom, "JitterPx")
    MEL.connect_material_expressions(jitter_rows, "", uv_custom, "JitterRows")
    MEL.connect_material_expressions(strength, "", uv_custom, "Strength")
    scene = add(material, unreal.MaterialExpressionSceneTexture, -700, 400)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    MEL.connect_material_expressions(uv_custom, "", scene, "UVs")
    color_custom = add_custom(material, -400, 0, "SOLHologramPostColor", POST_COLOR_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["Scene", "Tint", "TintAmount", "ScanAmount", "FlickerAmount", "Strength", "Time"])
    MEL.connect_material_expressions(scene, "Color", color_custom, "Scene")
    MEL.connect_material_expressions(tint, "", color_custom, "Tint")
    MEL.connect_material_expressions(tint_amount, "", color_custom, "TintAmount")
    MEL.connect_material_expressions(scan_amount, "", color_custom, "ScanAmount")
    MEL.connect_material_expressions(flicker_amount, "", color_custom, "FlickerAmount")
    MEL.connect_material_expressions(strength, "", color_custom, "Strength")
    MEL.connect_material_expressions(time, "", color_custom, "Time")
    MEL.connect_material_property(color_custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Creates a material, or rebuilds its graph in place when SOL_REBUILD is set
def ensure_material(name, builder):
    path = MAP_DIR + "/" + name
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("create_hologram_assets: %s exists (pass -SOLRebuild to rebuild)" % path)
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
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MAP_DIR, unreal.Material,
            unreal.MaterialFactoryNew())
    builder(material)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("create_hologram_assets: built %s" % path)


ensure_material("M_SOLHologramBody", build_body)
ensure_material("M_SOLHologramRing", build_ring)
ensure_material("M_SOLHologramPost", build_post)
