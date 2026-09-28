# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates the editor assets that sub-part 1a needs, headless and idempotent:
#   /Game/SOL/Materials/M_SOLBody  parametric planet material (colors, latitude bands, noise, polar caps, emissive),
#                                  unlit and self-shaded: Lambert toward the per-body SunDirection plus an ambient floor
#   /Game/Maps/SOL_Test            blank map (the game mode spawns everything at runtime)
# Parameter names must match SOL::BodyMaterialParams in Source/SOLTest/SOLConstants.h.
# Run: UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/CreateSOLContent.py" -unattended
#      (add -stdout -FullStdOutLogOutput to see the SOLContent log lines)
#      Pass -SOLRebuild on the command line (or set REBUILD) to recreate existing assets.
import math

import unreal

MATERIAL_DIR = "/Game/SOL/Materials"
MATERIAL_NAME = "M_SOLBody"
MAP_PATH = "/Game/Maps/SOL_Test"
REBUILD = "-solrebuild" in unreal.SystemLibrary.get_command_line().lower()

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


# Connects two expressions and fails loudly if the named input does not exist
def connect(src, src_output, dst, dst_input):
    if not MEL.connect_material_expressions(src, src_output, dst, dst_input):
        raise RuntimeError("SOLContent: failed to connect %s -> %s.%r" % (src.get_name(), dst.get_name(), dst_input))


# Creates a material expression node at a graph position
def node(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


# Creates a named scalar parameter
def scalar(material, name, default, x, y):
    expr = node(material, unreal.MaterialExpressionScalarParameter, x, y)
    expr.set_editor_property("parameter_name", name)
    expr.set_editor_property("default_value", default)
    return expr


# Creates a named vector (color) parameter
def vector(material, name, color, x, y):
    expr = node(material, unreal.MaterialExpressionVectorParameter, x, y)
    expr.set_editor_property("parameter_name", name)
    expr.set_editor_property("default_value", unreal.LinearColor(*color))
    return expr


# Creates a two-input math node wired to a and b (b may be a float constant)
def binary(material, cls, a, b, x, y):
    expr = node(material, cls, x, y)
    connect(a, "", expr, "A")
    if isinstance(b, (int, float)):
        expr.set_editor_property("const_b", float(b))
    else:
        connect(b, "", expr, "B")
    return expr


# Creates a one-input node wired to src
def unary(material, cls, src, x, y):
    expr = node(material, cls, x, y)
    connect(src, "", expr, "")
    return expr


# Creates a lerp node wired to a, b and alpha
def lerp(material, a, b, alpha, x, y):
    expr = node(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    connect(a, "", expr, "A")
    connect(b, "", expr, "B")
    connect(alpha, "", expr, "Alpha")
    return expr


# Builds the parametric body material graph
def build_material():
    path = MATERIAL_DIR + "/" + MATERIAL_NAME
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("SOLContent: %s exists, skipping" % path)
            return
        EAL.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(MATERIAL_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())

    # Inputs
    color_a = vector(material, "ColorA", (0.5, 0.5, 0.5, 1.0), -1400, -400)
    color_b = vector(material, "ColorB", (0.3, 0.3, 0.3, 1.0), -1400, -200)
    polar = vector(material, "PolarColor", (0.9, 0.9, 0.92, 1.0), -1400, 0)
    emissive = vector(material, "EmissiveColor", (0.0, 0.0, 0.0, 1.0), -1400, 200)
    band_freq = scalar(material, "BandFrequency", 0.0, -1400, 400)
    band_strength = scalar(material, "BandStrength", 0.0, -1400, 500)
    noise_scale = scalar(material, "NoiseScale", 0.05, -1400, 600)
    noise_strength = scalar(material, "NoiseStrength", 0.0, -1400, 700)
    noise_bias = scalar(material, "NoiseBias", 0.0, -1400, 750)
    polar_start = scalar(material, "PolarStart", 2.0, -1400, 800)
    sun_dir = vector(material, "SunDirection", (1.0, 0.0, 0.0, 0.0), -1400, 1000)
    sun_illuminance = scalar(material, "SunIlluminance", 3.0, -1400, 1200)
    ambient = scalar(material, "AmbientLight", 0.02, -1400, 1300)

    # Local (mesh-space) position: the engine sphere spans -50..50 cm; z is latitude
    local_pos = node(material, unreal.MaterialExpressionLocalPosition, -1800, 300)
    local_z = node(material, unreal.MaterialExpressionComponentMask, -1600, 300)
    local_z.set_editor_property("r", False)
    local_z.set_editor_property("g", False)
    local_z.set_editor_property("b", True)
    local_z.set_editor_property("a", False)
    connect(local_pos, "", local_z, "")

    # Bands: (sin(2*pi*z*freq) * 0.5 + 0.5) * strength
    band_arg = binary(material, unreal.MaterialExpressionMultiply, local_z, band_freq, -1100, 400)
    band_sin = unary(material, unreal.MaterialExpressionSine, band_arg, -950, 400)
    band_half = binary(material, unreal.MaterialExpressionMultiply, band_sin, 0.5, -800, 400)
    band_01 = binary(material, unreal.MaterialExpressionAdd, band_half, 0.5, -650, 400)
    band_term = binary(material, unreal.MaterialExpressionMultiply, band_01, band_strength, -500, 400)

    # Noise patches: saturate((noise(pos * scale) - bias) * 3) * strength
    noise_pos = binary(material, unreal.MaterialExpressionMultiply, local_pos, noise_scale, -1100, 600)
    noise = node(material, unreal.MaterialExpressionNoise, -950, 600)
    noise.set_editor_property("scale", 1.0)
    noise.set_editor_property("levels", 4)
    noise.set_editor_property("output_min", -1.0)
    noise.set_editor_property("output_max", 1.0)
    connect(noise_pos, "", noise, "World Position")
    noise_biased = binary(material, unreal.MaterialExpressionSubtract, noise, noise_bias, -870, 600)
    noise_sharp = binary(material, unreal.MaterialExpressionMultiply, noise_biased, 3.0, -800, 600)
    noise_sat = unary(material, unreal.MaterialExpressionSaturate, noise_sharp, -650, 600)
    noise_term = binary(material, unreal.MaterialExpressionMultiply, noise_sat, noise_strength, -500, 600)

    # Surface = lerp(A, B, saturate(bands + noise))
    mix_sum = binary(material, unreal.MaterialExpressionAdd, band_term, noise_term, -350, 500)
    mix_alpha = unary(material, unreal.MaterialExpressionSaturate, mix_sum, -200, 500)
    surface = lerp(material, color_a, color_b, mix_alpha, -50, -300)

    # Polar caps: saturate((|z| / 50 - start) * 10)
    lat = binary(material, unreal.MaterialExpressionMultiply, local_z, 0.02, -1100, 850)
    lat_abs = unary(material, unreal.MaterialExpressionAbs, lat, -950, 850)
    lat_rel = binary(material, unreal.MaterialExpressionSubtract, lat_abs, polar_start, -800, 850)
    lat_sharp = binary(material, unreal.MaterialExpressionMultiply, lat_rel, 10.0, -650, 850)
    polar_alpha = unary(material, unreal.MaterialExpressionSaturate, lat_sharp, -500, 850)
    final_color = lerp(material, surface, polar, polar_alpha, 150, -100)

    # Per-body sunlight: light = saturate(N . SunDirection) + ambient; a zero direction (the Sun) leaves only ambient
    normal_ws = node(material, unreal.MaterialExpressionVertexNormalWS, -1100, 1000)
    n_dot_l = binary(material, unreal.MaterialExpressionDotProduct, normal_ws, sun_dir, -950, 1050)
    lambert = unary(material, unreal.MaterialExpressionSaturate, n_dot_l, -800, 1050)
    light = binary(material, unreal.MaterialExpressionAdd, lambert, ambient, -650, 1100)

    # Diffuse luminance of an albedo under illuminance E is albedo * E / pi (matches a Default Lit surface)
    radiance_scale = binary(material, unreal.MaterialExpressionMultiply, sun_illuminance, 1.0 / math.pi, -650, 1250)
    light_scaled = binary(material, unreal.MaterialExpressionMultiply, light, radiance_scale, -500, 1150)
    shaded = binary(material, unreal.MaterialExpressionMultiply, final_color, light_scaled, 350, 0)
    glow = binary(material, unreal.MaterialExpressionAdd, shaded, emissive, 500, 100)

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("SOLContent: created %s" % path)


# Creates and saves the blank test map
def build_map():
    if EAL.does_asset_exist(MAP_PATH) and not REBUILD:
        unreal.log("SOLContent: %s exists, skipping" % MAP_PATH)
        return
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH)
    unreal.log("SOLContent: saved %s (%s)" % (MAP_PATH, ok))


build_material()
build_map()
