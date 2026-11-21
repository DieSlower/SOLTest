# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates (or, with -SOLRebuild, rebuilds) the thruster plume assets of issue #51 (SDD 51 section 4.11, part 51b),
# idempotent. Everything is generated here: no imported art, no marketplace content (D10, CC0).
#   /Game/SOL/Fx/T_SOLThrusterNoise
#       A seeded, periodic (tiling) value-noise texture written by this script: R = streak noise (fine around, coarse
#       along), G and B = isotropic fBm (the gas puffs). Linear, TC_Masks.
#   /Game/SOL/Fx/M_SOLThrusterPlume
#       Unlit, additive, two-sided, ISM usage. The vertex stage re-places each plume shell vertex in true metres from
#       its nozzle anchor (true length L_p, true nozzle radius, tail 0.35 r_n) or collapses it when off or under the
#       pixel fade; the pixel stage draws the colour ramp from c, the white-hot core, the shock diamonds and the panning
#       noise streaks (Shaders/Private/SOLThruster.ush, SOLTH_PlumeVertex / SOLTH_PlumePixel).
#   /Game/SOL/Fx/M_SOLThrusterGlow
#       Unlit, additive, two-sided, ISM usage: the camera-facing nozzle glow with its 1 px floor (SOLTH_GlowVertex).
#   /Game/SOL/Fx/M_SOLThrusterBurst
#       Unlit, translucent (not hot additive), two-sided, ISM usage: the short, nearly transparent grey-white gas puffs,
#       noise-dissolved and flickering with the per-instance random as phase (SOLTH_BurstVertex / SOLTH_BurstOpacity).
#   /Game/SOL/Fx/M_SOLThrusterTrail
#       Unlit, additive, two-sided, Niagara ribbon usage: the curved ribbon (particle colour x alpha, soft profile).
#   /Game/SOL/Fx/SM_SOLNpcHullCone, SM_SOLNpcHullBox
#       The NPC placeholder hulls (a generated 100 cm cone and cube of the engine Cone / Cube's size and pivot, slot
#       Hull = M_SOLNpcHull) plus the thruster
#       sections: slot Plume (one 16-sided cone on the aft nozzle), slot Glow (one quad), slot Burst (6 puffs on the
#       SOL::Thruster port table: a bow pair, 4 side ports). Built in mesh local units for the instance scale of
#       HullTransform in Source/SOLTest/Npc/SOLNpcVisuals.cpp (read from there), at full length.
#   /Game/SOL/Fx/SM_SOLPlayerThrusters
#       The player's thruster sections in the hull frame (cm): two plumes and glows on the engine nozzles, 6 bursts.
#   /Game/SOL/Fx/NS_SOLThrusterTrails
#       One CPU-sim emitter, one particle per ribbon point fed from the SOL::ThrusterTrailParams arrays (a one-shot burst
#       of TrailMaxPoints particles that never die; point i is trail i / TrailPointsPerTrail, link order i % it), a
#       ribbon renderer with M_SOLThrusterTrail.
# Section vertex layout and the per-ship floats: see the header of Shaders/Private/SOLThruster.ush. Asset paths are
# SOL::Paths::THRUSTER_*, parameter names SOL::ThrusterMaterialParams / SOL::ThrusterTrailParams, every default value
# SOL::Thruster (all read from Source/SOLTest/SOLConstants.h, so the assets cannot drift from the C++; the test
# SOLTest.Thruster.MaterialAssets checks the defaults against SOLThrusterMirror::MaterialVectors).
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/Fx/create_thruster_assets.py"
#       -unattended -AllowCommandletRendering [-SOLRebuild] [-SOLOnly=M_SOLThrusterPlume,SM_SOLNpcHullCone]
# -AllowCommandletRendering makes the commandlet compile the materials' shaders, so an HLSL error fails the run.
# Without -SOLRebuild existing assets are left untouched. -SOLOnly=<name>[,<name>] limits the run to those assets.
# A rebuild edits materials, the texture and meshes in place; the Niagara system is deleted and created again (a
# rebuild in place leaves its emitter silently disabled, issue #43), which is safe while only C++ loads it by path.
import json
import math
import os
import random
import re
import struct
import zlib

import unreal

PROJECT_DIR = unreal.Paths.project_dir()
CONSTANTS_FILE = os.path.join(PROJECT_DIR, "Source", "SOLTest", "SOLConstants.h")
NPC_VISUALS_FILE = os.path.join(PROJECT_DIR, "Source", "SOLTest", "Npc", "SOLNpcVisuals.cpp")
THRUSTER_INCLUDE = "/Project/SOLTest/Private/SOLThruster.ush"   # SOL::SHADER_VIRTUAL_DIRECTORY + file
HULL_CONE_SEGMENTS = 24
NOISE_SIZE = 256
NOISE_SEED = 51
PLUME_SEGMENTS = 16
BURST_SEGMENTS = 6
COMMAND_LINE = unreal.SystemLibrary.get_command_line()
REBUILD = ("-solrebuild" in COMMAND_LINE.lower()) or bool(globals().get("SOL_REBUILD"))
ONLY_MATCH = re.search(r"-SOLOnly=([A-Za-z0-9_,]+)", COMMAND_LINE, re.IGNORECASE)
ONLY = {name.lower() for name in ONLY_MATCH.group(1).split(",")} if ONLY_MATCH else None

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


# Logs one line with the script's prefix
def log(text):
    unreal.log("create_thruster_assets: " + text)


# ---------------------------------------------------------------------------------------------------------------------
# Constants from SOLConstants.h
# ---------------------------------------------------------------------------------------------------------------------

_TEXT = {}


# Returns the text of a source file (cached)
def source_text(path):
    if path not in _TEXT:
        with open(path, encoding="utf-8") as handle:
            _TEXT[path] = handle.read()
    return _TEXT[path]


# Returns the body of a namespace that is a direct member of SOL (4-space indented)
def namespace_block(name):
    match = re.search(r"\n    namespace " + name + r"\s*\{(.*?)\n    \}", source_text(CONSTANTS_FILE), re.S)
    if match is None:
        raise RuntimeError("create_thruster_assets: namespace SOL::%s not found" % name)
    return match.group(1)


# Returns the initialiser text of a constant: first in the given SOL namespace, else a direct member of SOL
def initialiser(name, scope=None):
    if scope is not None:
        match = re.search(r"\binline constexpr [\w:]+ " + name + r"\s*=(.*?);", namespace_block(scope), re.S)
        if match is not None:
            return match.group(1)
    match = re.search(r"\n    inline constexpr [\w:]+ " + name + r"\s*=(.*?);", source_text(CONSTANTS_FILE), re.S)
    if match is None:
        raise RuntimeError("create_thruster_assets: constant %s not found (scope %s)" % (name, scope))
    return match.group(1)


# Returns a numeric constant, evaluating an arithmetic initialiser over other constants (for example G_REF or
# NPC_NOZZLE_X_M); fails unless the whole initialiser is numbers, operators and known constants
def number(name, scope="Thruster"):
    text = initialiser(name, scope).strip()

    # Replace each identifier by its value: a SOL:: one from the top level, a bare one from the same scope first (an
    # exponent such as 5.0e11 follows a digit and is not an identifier)
    def substitute(match):
        token = match.group(0)
        if token.startswith("SOL::"):
            return "(%r)" % number(token[5:], None)
        return "(%r)" % number(token, scope)

    expression = re.sub(r"(?<![0-9.])(?:SOL::)?[A-Za-z_][A-Za-z0-9_]*", substitute,
                        re.sub(r"(\d)f\b", r"\1", " ".join(text.split())))
    if not re.fullmatch(r"[0-9eE.+\-*/() ]+", expression):
        raise RuntimeError("create_thruster_assets: %s = %s is not a plain arithmetic constant" % (name, text))
    return float(eval(expression, {"__builtins__": {}}, {}))


# Returns an FLinearColor constant of SOL::Thruster as (r, g, b)
def colour(name):
    match = re.search(r"\b" + name + r"\s*=\s*FLinearColor\(\s*([-0-9.eE+]+)f?\s*,\s*([-0-9.eE+]+)f?\s*,"
                      r"\s*([-0-9.eE+]+)f?\s*\)\s*;", namespace_block("Thruster"))
    if match is None:
        raise RuntimeError("create_thruster_assets: colour Thruster::%s not found" % name)
    return tuple(float(match.group(i)) for i in (1, 2, 3))


# Returns a TEXT("...") string constant of a SOL namespace
def text_constant(name, scope):
    match = re.search(r"\b" + name + r'\s*=\s*TEXT\("([^"]+)"\)', namespace_block(scope))
    if match is None:
        raise RuntimeError("create_thruster_assets: %s::%s not found" % (scope, name))
    return match.group(1)


# Returns the ribbon ship count of a quality preset from SOL::Thruster::RIBBON_COUNTS_BY_QUALITY (literal entries)
def ribbon_count(preset):
    match = re.search(r"RIBBON_COUNTS_BY_QUALITY\[\]\s*=\s*\{([^}]*)\}", namespace_block("Thruster"))
    if match is None:
        raise RuntimeError("create_thruster_assets: Thruster::RIBBON_COUNTS_BY_QUALITY not found")
    entry = match.group(1).split(",")[preset].strip()
    if not entry.isdigit():
        raise RuntimeError("create_thruster_assets: RIBBON_COUNTS_BY_QUALITY[%d] = %s is not a literal" % (preset,
                                                                                                         entry))
    return int(entry)


# Returns (package path, folder, asset name) of a SOL::Paths constant "/Game/A/B.B"
def asset_path(name):
    package = text_constant(name, "Paths").split(".")[0]
    folder, asset = package.rsplit("/", 1)
    return package, folder, asset


# Returns an integer constant of SOL::ThrusterMaterialParams (custom data indices)
def param_index(name):
    return int(number(name, "ThrusterMaterialParams"))


# Returns the placeholder hull instance scales (faction 0 cone, faction 1 box) of HullTransform in SOLNpcVisuals.cpp
def hull_instance_scales():
    match = re.search(r"faction == 0 \? FVector\(([^)]*)\) : FVector\(([^)]*)\)", source_text(NPC_VISUALS_FILE))
    if match is None:
        raise RuntimeError("create_thruster_assets: HullTransform scales not found in %s" % NPC_VISUALS_FILE)
    return [tuple(float(v) for v in match.group(i).split(",")) for i in (1, 2)]


# Returns the material vector parameters and their defaults, packed like SOLThrusterMirror::MaterialVectors
def material_vectors():
    ref_height = number("NPC_LANE_REFERENCE_HEIGHT_PX", None)
    n = number
    p = lambda key: text_constant(key, "ThrusterMaterialParams")
    vectors = {
        p("RIM_0"): colour("RIM_BLUE") + (1.0,), p("RIM_1"): colour("RIM_YELLOW") + (1.0,),
        p("RIM_2"): colour("RIM_RED") + (1.0,), p("CORE_0"): colour("CORE_BLUE") + (1.0,),
        p("CORE_1"): colour("CORE_YELLOW") + (1.0,), p("CORE_2"): colour("CORE_RED") + (1.0,),
        p("RAMP"): (n("COLOUR_YELLOW_C"), 0.0, 0.0, 0.0),
        p("SHOCK"): (n("SHOCK_SPACING_FACTOR"), n("SHOCK_BRIGHTNESS_GAIN"), n("SHOCK_DECAY"), n("SHOCK_ZONE")),
        p("SHAPE"): (n("SHOCK_MAX_KNOTS"), n("TAIL_RADIUS_RATIO"), n("SHOCK_KNOT_HALF_LENGTH"),
                     n("SHOCK_KNOT_HALF_WIDTH")),
        p("LOOK"): (n("PLUME_EMISSIVE"), n("PLUME_CORE_POWER"), n("SHOCK_KNOT_GAIN"), n("STREAK_STRENGTH")),
        p("STREAK"): (n("STREAK_TILES_AROUND"), n("STREAK_TILE_LENGTH_RADII"), n("STREAK_PAN_TILES_PER_S"), 0.0),
        p("FADE"): (n("CONE_FADE_LOW_PX"), n("CONE_FADE_HIGH_PX"), ref_height, 0.0),
        p("GLOW"): (n("GLOW_DIAMETER_NOZZLE_RADII"), n("GLOW_MIN_DRAWN_PX"), n("GLOW_EMISSIVE"), ref_height),
        p("GLOW_FADE"): (n("GLOW_FADE_LOW_PX"), n("GLOW_FADE_HIGH_PX"), 0.0, 0.0),
        p("BURST"): (n("BURST_MAX_OPACITY"), n("BURST_FLICKER_HZ"), n("BURST_FLICKER_DEPTH"), n("BURST_EMISSIVE")),
        p("BURST_SHAPE"): (n("BURST_BASE_RADIUS_LENGTHS"), n("BURST_TAIL_RADIUS_LENGTHS"), n("CONE_FADE_LOW_PX"),
                           n("CONE_FADE_HIGH_PX")),
        p("BURST_COLOUR"): colour("BURST_COLOUR") + (1.0,),
        p("TRAIL"): (n("TRAIL_EMISSIVE"), 0.0, 0.0, 0.0),
    }
    return {name: unreal.LinearColor(*value) for name, value in vectors.items()}


# Returns whether an asset is selected by -SOLOnly
def selected(asset):
    return ONLY is None or asset.lower() in ONLY


# ---------------------------------------------------------------------------------------------------------------------
# Noise texture
# ---------------------------------------------------------------------------------------------------------------------

# Returns one octave of periodic value noise (smoothstep-interpolated lattice of period_x x period_y cells), row major
def value_noise(size, period_x, period_y, rng):
    lattice = [[rng.random() for _ in range(period_x)] for _ in range(period_y)]
    values = [0.0] * (size * size)
    for y in range(size):
        fy = y * period_y / size
        y0 = int(fy)
        ty = fy - y0
        sy = ty * ty * (3.0 - 2.0 * ty)
        row0 = lattice[y0 % period_y]
        row1 = lattice[(y0 + 1) % period_y]
        for x in range(size):
            fx = x * period_x / size
            x0 = int(fx)
            tx = fx - x0
            sx = tx * tx * (3.0 - 2.0 * tx)
            x1 = (x0 + 1) % period_x
            a = row0[x0 % period_x] + (row0[x1] - row0[x0 % period_x]) * sx
            b = row1[x0 % period_x] + (row1[x1] - row1[x0 % period_x]) * sx
            values[y * size + x] = a + (b - a) * sy
    return values


# Returns a normalised fBm channel from (period_x, period_y, weight) octaves
def fbm(size, octaves, rng):
    total = [0.0] * (size * size)
    for period_x, period_y, weight in octaves:
        octave = value_noise(size, period_x, period_y, rng)
        total = [t + weight * o for t, o in zip(total, octave)]
    low = min(total)
    high = max(total)
    return [(t - low) / (high - low) for t in total]


# Writes an 8-bit RGB PNG from three channels in [0, 1]
def write_png(path, size, channels):
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            index = y * size + x
            row.extend(int(round(255.0 * channel[index])) for channel in channels)
        rows.append(bytes(row))

    # A PNG chunk: length, tag, data, CRC of tag and data
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
                     + chunk(b"IEND", b""))


# Generates, imports and configures the noise texture; returns it
def build_noise_texture():
    package, folder, name = asset_path("THRUSTER_NOISE_TEXTURE")
    if EAL.does_asset_exist(package) and (not REBUILD or not selected(name)):
        return EAL.load_asset(package)
    rng = random.Random(NOISE_SEED)
    streak = fbm(NOISE_SIZE, [(16, 4, 0.5), (32, 8, 0.3), (64, 16, 0.2)], rng)
    puff = fbm(NOISE_SIZE, [(4, 4, 0.5), (8, 8, 0.27), (16, 16, 0.15), (32, 32, 0.08)], rng)
    detail = fbm(NOISE_SIZE, [(8, 8, 0.5), (16, 16, 0.3), (32, 32, 0.2)], rng)
    temp_dir = os.path.join(unreal.Paths.project_saved_dir(), "Temp")
    os.makedirs(temp_dir, exist_ok=True)
    source = os.path.join(temp_dir, name + ".png")
    write_png(source, NOISE_SIZE, [streak, puff, detail])
    try:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", source)
        task.set_editor_property("destination_path", folder)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        if len(task.get_editor_property("imported_object_paths")) == 0:
            raise RuntimeError("create_thruster_assets: import of %s failed" % package)
    finally:
        os.remove(source)
    texture = EAL.load_asset(package)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
    EAL.save_loaded_asset(texture)
    log("built %s (%d x %d, seed %d)" % (package, NOISE_SIZE, NOISE_SIZE, NOISE_SEED))
    return texture


# ---------------------------------------------------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------------------------------------------------

PLUME_VERTEX_HLSL = r"""// Thruster plume vertex (SDD 51 4.4): the shell re-placed in true metres from its nozzle anchor
const float3 anchor = float3(Uv2.x, Uv2.y, Uv3.x);
const float3 offsetW = TransformLocalVectorToWorld(Parameters, Parameters.PreSkinnedPosition - anchor);
const float3 axisW = TransformLocalVectorToWorld(Parameters, float3(Uv4.x, Uv4.y, Uv5.x));
const float3 cameraToVertexW = GetTranslatedWorldPosition(Parameters) - ResolvedView.TranslatedWorldCameraOrigin;
float4 interp0;
float4 interp1;
float2 interp2;
const float3 offset = SOLTH_PlumeVertex(offsetW, axisW, Uv0, Uv3.y, ShipC, ShipB, ShipLength, cameraToVertexW,
    ResolvedView.ViewToClip[0][0], ResolvedView.ViewSizeAndInvSize.xy, ResolvedView.RealTime, Shock, Shape, Streak,
    Fade, interp0, interp1, interp2);
Interp0 = interp0;
Interp1 = interp1;
Interp2 = interp2;
return offset;"""

PLUME_PIXEL_HLSL = r"""// Thruster plume colour (SDD 51 4.3, 4.4)
return SOLTH_PlumePixel(Interp0, Interp1, Noise, Ramp, Rim0.rgb, Rim1.rgb, Rim2.rgb, Core0.rgb, Core1.rgb, Core2.rgb,
    Shock, Shape, Look);"""

GLOW_VERTEX_HLSL = r"""// Thruster nozzle glow vertex (SDD 51 4.4, Q13, Q33): a camera-facing disc at the nozzle anchor
const float3 anchor = float3(Uv2.x, Uv2.y, Uv3.x);
const float3 offsetW = TransformLocalVectorToWorld(Parameters, Parameters.PreSkinnedPosition - anchor);
const float3 cameraToVertexW = GetTranslatedWorldPosition(Parameters) - ResolvedView.TranslatedWorldCameraOrigin;
float4 interp0;
const float3 offset = SOLTH_GlowVertex(offsetW, Uv0, Uv3.y, ShipC, ShipB, cameraToVertexW,
    ResolvedView.ViewToTranslatedWorld[0].xyz, ResolvedView.ViewToTranslatedWorld[1].xyz,
    ResolvedView.ViewToClip[0][0], ResolvedView.ViewSizeAndInvSize.xy, Glow, GlowFade, interp0);
Interp0 = interp0;
return offset;"""

GLOW_PIXEL_HLSL = r"""// Thruster nozzle glow colour
return SOLTH_GlowPixel(Interp0, Uv0, Ramp, Rim0.rgb, Rim1.rgb, Rim2.rgb, Core0.rgb, Core1.rgb, Core2.rgb, Glow);"""

BURST_VERTEX_HLSL = r"""// Thruster gas burst vertex (SDD 51 4.6): a puff out of its port, length from the signed burst floats
const float3 anchor = float3(Uv2.x, Uv2.y, Uv3.x);
const float3 offsetW = TransformLocalVectorToWorld(Parameters, Parameters.PreSkinnedPosition - anchor);
const float3 axisW = TransformLocalVectorToWorld(Parameters, float3(Uv4.x, Uv4.y, Uv5.x));
const float3 cameraToVertexW = GetTranslatedWorldPosition(Parameters) - ResolvedView.TranslatedWorldCameraOrigin;
float4 interp0;
const float3 offset = SOLTH_BurstVertex(offsetW, axisW, Uv0, Uv3.y, Uv5.y, float3(Reverse, Lateral, Vertical),
    cameraToVertexW, ResolvedView.ViewToClip[0][0], ResolvedView.ViewSizeAndInvSize.xy, BurstShape, Fade.z, interp0);
Interp0 = interp0;
return offset;"""

BURST_PIXEL_HLSL = r"""// Thruster gas burst: grey-white, nearly transparent, noise-dissolved, flickering per ship
const float noise = Texture2DSample(Noise, NoiseSampler, SOLTH_BurstNoiseUv(Uv0, Phase, ResolvedView.RealTime)).g;
Opacity = SOLTH_BurstOpacity(Interp0, noise, Phase, ResolvedView.RealTime, Burst);
return BurstColour.rgb * Burst.w;"""

TRAIL_PIXEL_HLSL = r"""// Thruster ribbon colour (SDD 51 4.8)
return SOLTH_TrailPixel(Uv, Colour, Alpha, Trail);"""


# Adds an expression of the given class at an editor position
def add(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


# Adds a Custom node including SOLThruster.ush, with the given inputs, code, output type and extra outputs
def add_custom(material, x, y, description, code, output_type, input_names, extra_outputs=()):
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
    outputs = []
    for name, kind in extra_outputs:
        extra = unreal.CustomOutput()
        extra.set_editor_property("output_name", name)
        extra.set_editor_property("output_type", kind)
        outputs.append(extra)
    node.set_editor_property("additional_outputs", outputs)
    node.set_editor_property("include_file_paths", [THRUSTER_INCLUDE])
    return node


# Adds texture coordinate nodes for the given channels; returns {channel: node}
def add_uvs(material, channels, x, y):
    nodes = {}
    for row, channel in enumerate(channels):
        node = add(material, unreal.MaterialExpressionTextureCoordinate, x, y + 80 * row)
        node.set_editor_property("coordinate_index", channel)
        nodes[channel] = node
    return nodes


# Adds one per-ship float: the hull ISM's per-instance custom data plus the same index of custom primitive data (a
# single mesh component); each source reads 0 where the other is used
def add_ship_float(material, index, x, y):
    instance = add(material, unreal.MaterialExpressionPerInstanceCustomData, x, y)
    instance.set_editor_property("data_index", index)
    primitive = add(material, unreal.MaterialExpressionScalarParameter, x, y + 70)
    primitive.set_editor_property("parameter_name", "ThrusterShipData%d" % index)
    primitive.set_editor_property("default_value", 0.0)
    primitive.set_editor_property("use_custom_primitive_data", True)
    primitive.set_editor_property("primitive_data_index", index)
    total = add(material, unreal.MaterialExpressionAdd, x + 250, y + 30)
    MEL.connect_material_expressions(instance, "", total, "A")
    MEL.connect_material_expressions(primitive, "", total, "B")
    return total


# Adds the named vector parameters with their defaults; returns {name: node}
def add_vectors(material, names, vectors, x, y):
    nodes = {}
    for row, name in enumerate(names):
        node = add(material, unreal.MaterialExpressionVectorParameter, x, y + 100 * row)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", vectors[name])
        nodes[name] = node
    return nodes


# Adds a vertex-to-pixel interpolator fed by a Custom node output; returns it
def add_interpolator(material, source, output_name, x, y):
    node = add(material, unreal.MaterialExpressionVertexInterpolator, x, y)
    MEL.connect_material_expressions(source, output_name, node, "VS")
    return node


# Returns the thruster noise texture object parameter node
def add_noise_object(material, noise, x, y):
    node = add(material, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property("parameter_name", text_constant("NOISE_TEXTURE", "ThrusterMaterialParams"))
    node.set_editor_property("texture", noise)
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    return node


# Sets the shared translucent, unlit, two-sided properties
def set_translucent(material, blend_mode):
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", blend_mode)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("use_translucency_vertex_fog", False)
    material.set_editor_property("allow_negative_emissive_color", False)


# Returns the ramp parameter names (yellow c, then the rim and core keys)
def ramp_names():
    p = lambda key: text_constant(key, "ThrusterMaterialParams")
    return [p("RAMP"), p("RIM_0"), p("RIM_1"), p("RIM_2"), p("CORE_0"), p("CORE_1"), p("CORE_2")]


# Connects vector parameter nodes to Custom node pins of the same order
def connect_vectors(nodes, names, custom, pins):
    for name, pin in zip(names, pins):
        MEL.connect_material_expressions(nodes[name], "RGBA", custom, pin)


# Builds the plume material graph
def build_plume(material, noise, vectors):
    set_translucent(material, unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    p = lambda key: text_constant(key, "ThrusterMaterialParams")
    uvs = add_uvs(material, [0, 2, 3, 4, 5], -1800, -500)
    ship_c = add_ship_float(material, param_index("CUSTOM_COLOUR"), -1800, -60)
    ship_b = add_ship_float(material, param_index("CUSTOM_BRIGHTNESS"), -1800, 100)
    ship_length = add_ship_float(material, param_index("CUSTOM_PLUME_LENGTH_M"), -1800, 260)
    vertex_names = [p("SHOCK"), p("SHAPE"), p("STREAK"), p("FADE")]
    pixel_names = ramp_names() + [p("LOOK")]
    nodes = add_vectors(material, vertex_names + pixel_names, vectors, -2200, -500)

    # Vertex stage: the world position offset and three interpolated vectors
    vertex = add_custom(material, -1300, -400, "SOLThrusterPlumeVertex", PLUME_VERTEX_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["Uv0", "Uv2", "Uv3", "Uv4", "Uv5", "ShipC", "ShipB", "ShipLength", "Shock", "Shape", "Streak", "Fade"],
        [("Interp0", unreal.CustomMaterialOutputType.CMOT_FLOAT4),
         ("Interp1", unreal.CustomMaterialOutputType.CMOT_FLOAT4),
         ("Interp2", unreal.CustomMaterialOutputType.CMOT_FLOAT2)])
    for channel in (0, 2, 3, 4, 5):
        MEL.connect_material_expressions(uvs[channel], "", vertex, "Uv%d" % channel)
    MEL.connect_material_expressions(ship_c, "", vertex, "ShipC")
    MEL.connect_material_expressions(ship_b, "", vertex, "ShipB")
    MEL.connect_material_expressions(ship_length, "", vertex, "ShipLength")
    connect_vectors(nodes, vertex_names, vertex, ["Shock", "Shape", "Streak", "Fade"])
    MEL.connect_material_property(vertex, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    interp = [add_interpolator(material, vertex, name, -900, -300 + 120 * row)
              for row, name in enumerate(["Interp0", "Interp1", "Interp2"])]

    # Pixel stage: the streak noise sampled at the interpolated coordinates, then the colour
    sample = add(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, 100)
    sample.set_editor_property("parameter_name", text_constant("NOISE_TEXTURE", "ThrusterMaterialParams"))
    sample.set_editor_property("texture", noise)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    MEL.connect_material_expressions(interp[2], "PS", sample, "UVs")
    pixel = add_custom(material, -400, 0, "SOLThrusterPlumePixel", PLUME_PIXEL_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["Interp0", "Interp1", "Noise", "Ramp", "Rim0", "Rim1", "Rim2", "Core0", "Core1", "Core2", "Shock", "Shape",
         "Look"])
    MEL.connect_material_expressions(interp[0], "PS", pixel, "Interp0")
    MEL.connect_material_expressions(interp[1], "PS", pixel, "Interp1")
    MEL.connect_material_expressions(sample, "R", pixel, "Noise")
    connect_vectors(nodes, pixel_names, pixel, ["Ramp", "Rim0", "Rim1", "Rim2", "Core0", "Core1", "Core2", "Look"])
    connect_vectors(nodes, [p("SHOCK"), p("SHAPE")], pixel, ["Shock", "Shape"])
    MEL.connect_material_property(pixel, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Builds the nozzle glow material graph
def build_glow(material, vectors):
    set_translucent(material, unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    p = lambda key: text_constant(key, "ThrusterMaterialParams")
    uvs = add_uvs(material, [0, 2, 3], -1800, -400)
    ship_c = add_ship_float(material, param_index("CUSTOM_COLOUR"), -1800, -120)
    ship_b = add_ship_float(material, param_index("CUSTOM_BRIGHTNESS"), -1800, 40)
    pixel_names = ramp_names()
    nodes = add_vectors(material, [p("GLOW"), p("GLOW_FADE")] + pixel_names, vectors, -2200, -400)

    # Vertex stage: the camera-facing corner and the interpolated (c, alpha, b)
    vertex = add_custom(material, -1300, -300, "SOLThrusterGlowVertex", GLOW_VERTEX_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Uv0", "Uv2", "Uv3", "ShipC", "ShipB", "Glow", "GlowFade"],
        [("Interp0", unreal.CustomMaterialOutputType.CMOT_FLOAT4)])
    for channel in (0, 2, 3):
        MEL.connect_material_expressions(uvs[channel], "", vertex, "Uv%d" % channel)
    MEL.connect_material_expressions(ship_c, "", vertex, "ShipC")
    MEL.connect_material_expressions(ship_b, "", vertex, "ShipB")
    connect_vectors(nodes, [p("GLOW"), p("GLOW_FADE")], vertex, ["Glow", "GlowFade"])
    MEL.connect_material_property(vertex, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    interp0 = add_interpolator(material, vertex, "Interp0", -900, -200)

    # Pixel stage: the soft disc in the ramp's colours
    pixel = add_custom(material, -400, 0, "SOLThrusterGlowPixel", GLOW_PIXEL_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["Interp0", "Uv0", "Ramp", "Rim0", "Rim1", "Rim2", "Core0", "Core1", "Core2", "Glow"])
    MEL.connect_material_expressions(interp0, "PS", pixel, "Interp0")
    MEL.connect_material_expressions(uvs[0], "", pixel, "Uv0")
    connect_vectors(nodes, pixel_names + [p("GLOW")], pixel,
                    ["Ramp", "Rim0", "Rim1", "Rim2", "Core0", "Core1", "Core2", "Glow"])
    MEL.connect_material_property(pixel, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Builds the gas burst material graph
def build_burst(material, noise, vectors):
    set_translucent(material, unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    p = lambda key: text_constant(key, "ThrusterMaterialParams")
    uvs = add_uvs(material, [0, 2, 3, 4, 5], -1800, -500)
    reverse = add_ship_float(material, param_index("CUSTOM_BURST_REVERSE"), -1800, -60)
    lateral = add_ship_float(material, param_index("CUSTOM_BURST_LATERAL"), -1800, 100)
    vertical = add_ship_float(material, param_index("CUSTOM_BURST_VERTICAL"), -1800, 260)
    nodes = add_vectors(material, [p("BURST_SHAPE"), p("FADE"), p("BURST"), p("BURST_COLOUR")], vectors, -2200, -500)

    # Vertex stage: the puff placed from its port, the interpolated (t, radial fraction, alpha, strength)
    vertex = add_custom(material, -1300, -400, "SOLThrusterBurstVertex", BURST_VERTEX_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["Uv0", "Uv2", "Uv3", "Uv4", "Uv5", "Reverse", "Lateral", "Vertical", "BurstShape", "Fade"],
        [("Interp0", unreal.CustomMaterialOutputType.CMOT_FLOAT4)])
    for channel in (0, 2, 3, 4, 5):
        MEL.connect_material_expressions(uvs[channel], "", vertex, "Uv%d" % channel)
    MEL.connect_material_expressions(reverse, "", vertex, "Reverse")
    MEL.connect_material_expressions(lateral, "", vertex, "Lateral")
    MEL.connect_material_expressions(vertical, "", vertex, "Vertical")
    connect_vectors(nodes, [p("BURST_SHAPE"), p("FADE")], vertex, ["BurstShape", "Fade"])
    MEL.connect_material_property(vertex, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    interp0 = add_interpolator(material, vertex, "Interp0", -900, -200)

    # Pixel stage: the gas colour and its opacity
    noise_object = add_noise_object(material, noise, -900, 100)
    phase = add(material, unreal.MaterialExpressionPerInstanceRandom, -900, 250)
    pixel = add_custom(material, -400, 0, "SOLThrusterBurstPixel", BURST_PIXEL_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Interp0", "Uv0", "Noise", "Phase", "Burst", "BurstColour"],
        [("Opacity", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
    MEL.connect_material_expressions(interp0, "PS", pixel, "Interp0")
    MEL.connect_material_expressions(uvs[0], "", pixel, "Uv0")
    MEL.connect_material_expressions(noise_object, "", pixel, "Noise")
    MEL.connect_material_expressions(phase, "", pixel, "Phase")
    connect_vectors(nodes, [p("BURST"), p("BURST_COLOUR")], pixel, ["Burst", "BurstColour"])
    MEL.connect_material_property(pixel, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(pixel, "Opacity", unreal.MaterialProperty.MP_OPACITY)


# Builds the ribbon material graph
def build_trail(material, vectors):
    set_translucent(material, unreal.BlendMode.BLEND_ADDITIVE)
    MEL.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_RIBBONS)
    p = lambda key: text_constant(key, "ThrusterMaterialParams")
    uv = add(material, unreal.MaterialExpressionTextureCoordinate, -900, -100)
    particle = add(material, unreal.MaterialExpressionParticleColor, -900, 0)
    nodes = add_vectors(material, [p("TRAIL")], vectors, -900, 200)
    pixel = add_custom(material, -400, 0, "SOLThrusterTrailPixel", TRAIL_PIXEL_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Uv", "Colour", "Alpha", "Trail"])
    MEL.connect_material_expressions(uv, "", pixel, "Uv")
    MEL.connect_material_expressions(particle, "RGB", pixel, "Colour")
    MEL.connect_material_expressions(particle, "A", pixel, "Alpha")
    connect_vectors(nodes, [p("TRAIL")], pixel, ["Trail"])
    MEL.connect_material_property(pixel, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Removes every expression of a material (repeat: one pass can leave some behind)
def clear_material(material):
    for _ in range(8):
        expressions = list(MEL.get_material_expressions(material))
        if not expressions:
            break
        for expression in expressions:
            MEL.delete_material_expression(material, expression)


# Returns the material of a SOL::Paths constant for (re)building, or the existing one (None to build) when kept
def material_for_build(path_name):
    package, folder, name = asset_path(path_name)
    if EAL.does_asset_exist(package):
        material = EAL.load_asset(package)
        if not REBUILD or not selected(name):
            log("%s kept (pass -SOLRebuild%s to rebuild)" % (package, "" if selected(name) else " and -SOLOnly"))
            return material, False
        clear_material(material)
        return material, True
    if not selected(name):
        raise RuntimeError("create_thruster_assets: %s is missing but excluded by -SOLOnly" % package)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material,
        unreal.MaterialFactoryNew())
    return material, True


# Compiles a material for the editor's shader platform, fails on any compile error, logs its statistics and saves it
# (get_statistics waits for the compilation; a second recompile returns the finished resource's errors). A commandlet
# compiles shaders only with -AllowCommandletRendering; without it the statistics stay empty and a warning says so
def finish_material(material):
    MEL.recompile_material(material)
    stats = MEL.get_statistics(material)
    errors = list(MEL.recompile_material(material))
    stats = MEL.get_statistics(material)
    log("built %s: vertex shader %d instructions, pixel shader %d instructions, %d samplers, %d errors" % (
        material.get_path_name(), stats.num_vertex_shader_instructions, stats.num_pixel_shader_instructions,
        stats.num_samplers, len(errors)))
    if errors:
        for error in errors:
            unreal.log_error("create_thruster_assets: " + error)
        raise RuntimeError("create_thruster_assets: %s did not compile" % material.get_path_name())
    if stats.num_pixel_shader_instructions <= 0 and stats.num_samplers <= 0:
        unreal.log_warning("create_thruster_assets: no shader map was compiled for %s (null RHI or no "
                           "-AllowCommandletRendering): the HLSL is unchecked" % material.get_path_name())
    EAL.save_loaded_asset(material)


# Builds the four materials; returns {path constant: material}
def build_materials(noise):
    vectors = material_vectors()
    builders = [("THRUSTER_PLUME_MATERIAL", lambda m: build_plume(m, noise, vectors)),
                ("THRUSTER_GLOW_MATERIAL", lambda m: build_glow(m, vectors)),
                ("THRUSTER_BURST_MATERIAL", lambda m: build_burst(m, noise, vectors)),
                ("THRUSTER_TRAIL_MATERIAL", lambda m: build_trail(m, vectors))]
    materials = {}
    for path_name, builder in builders:
        material, build = material_for_build(path_name)
        if build:
            builder(material)
            finish_material(material)
        materials[path_name] = material
    return materials


# ---------------------------------------------------------------------------------------------------------------------
# Meshes
# ---------------------------------------------------------------------------------------------------------------------

# Maps hull-frame metres (X nose, Y right, Z top) to a mesh's local units
class HullFrame(object):
    # Stores the local axis each hull axis maps to (index, sign) and the cm per local unit of each local axis
    def __init__(self, axis_map, cm_per_unit):
        self.axis_map = axis_map
        self.cm_per_unit = cm_per_unit

    # Returns a hull-frame vector (metres) in local units as a tuple
    def vector(self, hull):
        local = [0.0, 0.0, 0.0]
        for hull_axis, (local_axis, sign) in enumerate(self.axis_map):
            local[local_axis] = sign * hull[hull_axis] * 100.0 / self.cm_per_unit[local_axis]
        return tuple(local)


# Returns the hull frames: the cone (local Z is the nose, local X the hull's -Z, CONE_AXIS_TO_NOSE in
# Npc/SOLNpcAiMath.cpp), the box (local axes are the hull axes) and the player (hull frame in cm)
def hull_frames():
    hull_cm = number("NPC_HULL_LENGTH_M", None) * 100.0
    cone_scale, box_scale = hull_instance_scales()
    cone = HullFrame([(2, 1.0), (1, 1.0), (0, -1.0)], tuple(s * hull_cm / 100.0 for s in cone_scale))
    box = HullFrame([(0, 1.0), (1, 1.0), (2, 1.0)], tuple(s * hull_cm / 100.0 for s in box_scale))
    player = HullFrame([(0, 1.0), (1, 1.0), (2, 1.0)], (1.0, 1.0, 1.0))
    return cone, box, player


# Returns a + b x k for 3-tuples
def axpy(a, b, k):
    return tuple(a[i] + b[i] * k for i in range(3))


# Returns a unit vector of a 3-tuple
def unit(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


# Returns the cross product of two 3-tuples
def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


# Returns two unit vectors perpendicular to an axis and to each other
def perpendicular_basis(axis):
    reference = (0.0, 0.0, 1.0) if abs(axis[2]) < 0.9 else (0.0, 1.0, 0.0)
    first = unit(cross(axis, reference))
    return first, cross(axis, first)


# One mesh section's vertex and triangle buffers in the layout of SOLThruster.ush
class Section(object):
    # Starts empty
    def __init__(self):
        self.vertices = []
        self.normals = []
        self.uvs = [[] for _ in range(6)]
        self.triangles = []

    # Adds one vertex (local units) with its anchor, axis, size and port code; returns its index
    def vertex(self, frame, position, normal, uv0, anchor, axis, size_m, code):
        anchor_local = frame.vector(anchor)
        axis_local = frame.vector(axis)
        self.vertices.append(unreal.Vector(*frame.vector(position)))
        self.normals.append(unreal.Vector(*unit(frame.vector(normal))))
        for channel, value in enumerate([uv0, (0.0, 0.0), anchor_local[:2], (anchor_local[2], size_m),
                                         axis_local[:2], (axis_local[2], code)]):
            self.uvs[channel].append(unreal.Vector2D(value[0], value[1]))
        return len(self.vertices) - 1

    # Adds an open cone from the anchor along the axis: radius r0 at s = 0, r1 at s = 1, segments around
    def cone(self, frame, anchor, axis, length_m, r0_m, r1_m, segments, size_m, code):
        first, second = perpendicular_basis(axis)
        base = len(self.vertices)
        for s in (0.0, 1.0):
            centre = axpy(anchor, axis, s * length_m)
            radius = r0_m + (r1_m - r0_m) * s
            for column in range(segments + 1):
                theta = 2.0 * math.pi * column / segments
                radial = tuple(first[i] * math.cos(theta) + second[i] * math.sin(theta) for i in range(3))
                self.vertex(frame, axpy(centre, radial, radius), radial, (column / float(segments), s), anchor,
                            axis, size_m, code)
        ring = segments + 1
        for column in range(segments):
            a = base + column
            b = a + 1
            c = a + ring
            d = c + 1
            self.triangles.append(unreal.IntVector(a, c, b))
            self.triangles.append(unreal.IntVector(b, c, d))

    # Adds a glow quad at the anchor, corners (-1 / +1, -1 / +1) in UV0
    def quad(self, frame, anchor, axis, radius_m, size_m):
        first, second = perpendicular_basis(axis)
        base = len(self.vertices)
        for cx, cy in ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)):
            corner = tuple(first[i] * cx + second[i] * cy for i in range(3))
            self.vertex(frame, axpy(anchor, corner, radius_m), axis, (cx, cy), anchor, axis, size_m, 0.0)
        self.triangles.append(unreal.IntVector(base, base + 1, base + 2))
        self.triangles.append(unreal.IntVector(base, base + 2, base + 3))

    # Returns the section as Geometry Script buffers
    def buffers(self):
        result = unreal.GeometryScriptSimpleMeshBuffers()
        result.set_editor_property("vertices", self.vertices)
        result.set_editor_property("normals", self.normals)
        for channel in range(6):
            result.set_editor_property("uv%d" % channel, self.uvs[channel])
        result.set_editor_property("triangles", self.triangles)
        return result


# Returns the burst ports of a hull as (position, exhaust, port code): the bow pair (reverse), the left / right side
# ports (lateral > 0 / < 0) and the bottom / top ports (vertical > 0 / < 0), SOLThruster.ush SOLTH_PORT_*
def burst_ports(prefix):
    bow_x = number(prefix + "_BOW_PORT_X_M")
    bow_y = number(prefix + "_BOW_PORT_Y_M")
    side_x = number(prefix + "_SIDE_PORT_X_M")
    side_y = number(prefix + "_SIDE_PORT_Y_M")
    side_z = number(prefix + "_SIDE_PORT_Z_M")
    return [((bow_x, bow_y, 0.0), (1.0, 0.0, 0.0), 0.0), ((bow_x, -bow_y, 0.0), (1.0, 0.0, 0.0), 0.0),
            ((side_x, -side_y, 0.0), (0.0, -1.0, 0.0), 1.0), ((side_x, side_y, 0.0), (0.0, 1.0, 0.0), 2.0),
            ((side_x, 0.0, -side_z), (0.0, 0.0, -1.0), 3.0), ((side_x, 0.0, side_z), (0.0, 0.0, 1.0), 4.0)]


# Returns the plume, glow and burst sections of a hull: nozzles [(position, radius)], its port prefix and length
def thruster_sections(frame, nozzles, port_prefix, hull_length_m):
    exhaust = (-1.0, 0.0, 0.0)
    tail = number("TAIL_RADIUS_RATIO")
    glow_radius = 0.5 * number("GLOW_DIAMETER_NOZZLE_RADII")
    burst_length = hull_length_m / number("BURST_LENGTH_HULL_DIVISOR")
    plume = Section()
    glow = Section()
    burst = Section()
    for position, radius in nozzles:
        plume.cone(frame, position, exhaust, hull_length_m, radius, radius * tail, PLUME_SEGMENTS, radius, 0.0)
        glow.quad(frame, position, exhaust, radius * glow_radius, radius)
    for position, axis, code in burst_ports(port_prefix):
        burst.cone(frame, position, axis, burst_length, burst_length * number("BURST_BASE_RADIUS_LENGTHS"),
                   burst_length * number("BURST_TAIL_RADIUS_LENGTHS"), BURST_SEGMENTS, burst_length, code)
    return [plume, glow, burst]


# Writes a dynamic mesh into the static mesh asset of a SOL::Paths constant with named slots and the thruster build
# settings (no lightmap UVs, normals and degenerates kept), creating the asset when missing
def write_mesh(path_name, dynamic, slot_names, materials):
    package, folder, name = asset_path(path_name)
    if not EAL.does_asset_exist(package):
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property("enable_recompute_normals", False)
        options.set_editor_property("enable_recompute_tangents", False)
        options.set_editor_property("enable_nanite", False)
        options.set_editor_property("enable_collision", False)
        mesh, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dynamic, package,
                                                                                                options)
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS or mesh is None:
            raise RuntimeError("create_thruster_assets: could not create %s" % package)
    mesh = EAL.load_asset(package)

    # The geometry and the named slots, replacing the asset's material list; the build keeps the normals and the
    # (on purpose zero-area when idle) triangles and generates no lightmap UVs. The section values fit half-float UVs
    # (anchors at most 800 local units, about 0.5 cm steps; port codes are small integers)
    copy = unreal.GeometryScriptCopyMeshToAssetOptions()
    copy.set_editor_property("enable_recompute_normals", False)
    copy.set_editor_property("enable_recompute_tangents", False)
    copy.set_editor_property("enable_remove_degenerates", False)
    copy.set_editor_property("generate_lightmap_u_vs",
                             unreal.GeometryScriptGenerateLightmapUVOptions.DO_NOT_GENERATE_LIGHTMAP_U_VS)
    copy.set_editor_property("replace_materials", True)
    copy.set_editor_property("new_materials", materials)
    copy.set_editor_property("new_material_slot_names", [unreal.Name(slot) for slot in slot_names])
    _, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dynamic, mesh, copy,
                                                                          unreal.GeometryScriptMeshWriteLOD(), False)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError("create_thruster_assets: could not write %s" % package)
    EAL.save_loaded_asset(mesh)
    box = mesh.get_bounding_box()
    log("built %s: %d slots, bounds min (%.1f, %.1f, %.1f) max (%.1f, %.1f, %.1f)" % (
        package, len(slot_names), box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z))
    verify_mesh(mesh, package)


# Reads a written mesh back and fails unless its last triangle (a burst port) still carries the section layout: six
# UV channels and a port code in UV5.y
def verify_mesh(mesh, package):
    readback = unreal.DynamicMesh()
    readback, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(
        mesh, readback, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    channels = unreal.GeometryScript_MeshQueries.get_num_uv_sets(readback)
    triangles = unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(readback)
    corners = unreal.GeometryScript_MeshQueries.get_triangle_u_vs(readback, 5, triangles - 1)
    log("%s read back: %d UV channels, %d triangles, last triangle UV5 %s" % (package, channels, triangles,
                                                                               str(corners)))
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS or channels < 6:
        raise RuntimeError("create_thruster_assets: %s lost its section UV channels" % package)


# Returns a dynamic mesh holding a generated placeholder hull with 6 UV channels, material 0: the same 100 cm,
# centred shapes as the engine Cone (radius 50, apex +Z) and Cube that NPC_HULL_MESH_FACTION_0 / 1 draw today, so
# HullTransform's instance scales fit both; fails unless the aft face sits where the nozzle anchors expect it
def hull_dynamic_mesh(kind):
    dynamic = unreal.DynamicMesh()
    options = unreal.GeometryScriptPrimitiveOptions()
    centre = unreal.GeometryScriptPrimitiveOriginMode.CENTER
    if kind == "cone":
        unreal.GeometryScript_Primitives.append_cone(dynamic, options, unreal.Transform(), 50.0, 0.0, 100.0,
                                                     HULL_CONE_SEGMENTS, 1, True, centre)
        aft = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(dynamic).min.z
    else:
        unreal.GeometryScript_Primitives.append_box(dynamic, options, unreal.Transform(), 100.0, 100.0, 100.0, 0, 0,
                                                    0, centre)
        aft = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(dynamic).min.x
    if abs(aft + 50.0) > 0.5:
        raise RuntimeError("create_thruster_assets: the %s hull's aft face is at %.2f, expected -50" % (kind, aft))
    unreal.GeometryScript_UVs.set_num_uv_sets(dynamic, 6)
    return dynamic


# Appends sections to a dynamic mesh as materials first_id, first_id + 1, ...
def append_sections(dynamic, sections, first_id):
    for offset, section in enumerate(sections):
        unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic, section.buffers(), first_id + offset)


# Builds the two NPC hull meshes and the player's thruster mesh
def build_meshes(materials):
    p = lambda key: text_constant(key, "ThrusterMaterialParams")
    cone_frame, box_frame, player_frame = hull_frames()
    npc_length = number("NPC_HULL_LENGTH_M", None)
    npc_nozzle = [((number("NPC_NOZZLE_X_M"), 0.0, 0.0), number("NPC_NOZZLE_RADIUS_M"))]
    hull_material = EAL.load_asset(text_constant("NPC_HULL_MATERIAL", "Paths").split(".")[0])
    thruster_materials = [materials["THRUSTER_PLUME_MATERIAL"], materials["THRUSTER_GLOW_MATERIAL"],
                          materials["THRUSTER_BURST_MATERIAL"]]
    thruster_slots = [p("SLOT_PLUME"), p("SLOT_GLOW"), p("SLOT_BURST")]
    jobs = [("THRUSTER_NPC_HULL_MESH_FACTION_0", "cone", cone_frame, "NPC_CONE"),
            ("THRUSTER_NPC_HULL_MESH_FACTION_1", "box", box_frame, "NPC_BOX")]
    for path_name, kind, frame, ports in jobs:
        package, _, name = asset_path(path_name)
        if EAL.does_asset_exist(package) and (not REBUILD or not selected(name)):
            log("%s kept" % package)
            continue
        dynamic = hull_dynamic_mesh(kind)
        append_sections(dynamic, thruster_sections(frame, npc_nozzle, ports, npc_length), 1)
        write_mesh(path_name, dynamic, [p("SLOT_HULL")] + thruster_slots, [hull_material] + thruster_materials)

    # The player: two nozzles on the engine cylinders' aft faces, no hull section
    package, _, name = asset_path("THRUSTER_PLAYER_MESH")
    if EAL.does_asset_exist(package) and (not REBUILD or not selected(name)):
        log("%s kept" % package)
        return
    radius = number("PLAYER_NOZZLE_RADIUS_M")
    nozzle = (number("PLAYER_NOZZLE_X_M"), number("PLAYER_NOZZLE_Y_M"), number("PLAYER_NOZZLE_Z_M"))
    nozzles = [(nozzle, radius), ((nozzle[0], -nozzle[1], nozzle[2]), radius)]
    dynamic = unreal.DynamicMesh()
    unreal.GeometryScript_UVs.set_num_uv_sets(dynamic, 6)
    append_sections(dynamic, thruster_sections(player_frame, nozzles, "PLAYER", number("PLAYER_HULL_LENGTH_M")), 0)
    write_mesh("THRUSTER_PLAYER_MESH", dynamic, thruster_slots, thruster_materials)


# ---------------------------------------------------------------------------------------------------------------------
# Niagara ribbon system (NiagaraToolsets JSON tools, the same tools unreal-mcp exposes)
# ---------------------------------------------------------------------------------------------------------------------

TOOLSET = "NiagaraToolsets.NiagaraToolset_System"
SYSTEM_TEMPLATE = "/Niagara/DefaultAssets/Templates/Systems/MinimalLightweight.MinimalLightweight"
EMITTER_TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"
SPAWN_BURST_MODULE = "/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"
RIBBON_RENDERER = "/Script/Niagara.NiagaraRibbonRendererProperties"
EMITTER_NAME = "Trails"
SELECT_INPUTS = "/Niagara/DynamicInputs/Arrays/"
T_FLOAT = "/Script/Niagara.NiagaraFloat"
T_INT = "/Script/Niagara.NiagaraInt32"
T_BOOL = "/Script/Niagara.NiagaraBool"
T_POSITION = "/Script/Niagara.NiagaraPosition"
T_VEC3 = "/Script/CoreUObject.Vector3f"
T_COLOR = "/Script/CoreUObject.LinearColor"
T_ARRAY_FLOAT3 = "/Script/Niagara.NiagaraDataInterfaceArrayFloat3"
T_ARRAY_COLOR = "/Script/Niagara.NiagaraDataInterfaceArrayColor"
T_ARRAY_FLOAT = "/Script/Niagara.NiagaraDataInterfaceArrayFloat"
D_ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
D_LINKED = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
D_HLSL = "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"
D_DYNAMIC = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"
NIAGARA_TRUE = -1   # FNiagaraBool stores true as -1
ZERO = {
    T_FLOAT: {"struct": {"refPath": T_FLOAT}, "value": {"value": 0.0}},
    T_INT: {"struct": {"refPath": T_INT}, "value": {"value": 0}},
    T_POSITION: {"struct": {"refPath": T_POSITION}, "value": {"x": 0.0, "y": 0.0, "z": 0.0}},
    T_VEC3: {"struct": {"refPath": T_VEC3}, "value": {"x": 0.0, "y": 0.0, "z": 0.0}},
    T_COLOR: {"struct": {"refPath": T_COLOR}, "value": {"r": 0.0, "g": 0.0, "b": 0.0, "a": 0.0}},
}


# Calls one NiagaraToolset_System tool with JSON arguments and returns its decoded return value
def tool(name, args):
    result = unreal.ToolsetRegistry.execute_tool(TOOLSET, name, json.dumps(args))
    if result.error:
        raise RuntimeError("create_thruster_assets: %s failed: %s" % (name, result.error))
    text = result.get_value_as_json_string()
    if not text:
        return None
    value = json.loads(text)
    while isinstance(value, str):
        value = json.loads(value)
    if isinstance(value, dict) and "returnValue" in value:
        value = value["returnValue"]
    return value


# Returns a stack item reference into the system (empty fields select nothing at that level)
def ref(system_path, emitter="", script="", module="", renderer=-1, inputs=None):
    return {"system": {"refPath": system_path}, "emitterName": emitter, "scriptName": script, "moduleName": module,
            "rendererIndex": renderer, "inputNameStack": inputs or []}


# Returns a Niagara type wrapper for a struct or class path
def niagara_type(path):
    return {"classStructOrEnum": {"refPath": path}}


# Returns an instanced value of the given struct
def value(struct_path, payload):
    return {"struct": {"refPath": struct_path}, "value": payload}


# Returns an HLSL expression input value
def hlsl(expression):
    return value(D_HLSL, {"hlslExpression": expression})


# Returns an input value linked to a variable of the given type
def linked(name, type_path):
    return value(D_LINKED, {"linkedVariable": {"name": name, "type": niagara_type(type_path)}})


# Returns an enum input value
def enum(enum_path, enum_name, display_name):
    return value(D_ENUM, {"enum": {"refPath": enum_path}, "enumName": enum_name, "displayName": display_name})


# Sets one module input (inputs is the input name stack: the input, then nested dynamic-input inputs)
def set_input(system_path, script, module, inputs, input_value):
    tool("SetStackInputData", {"stackInputRef": ref(system_path, EMITTER_NAME, script, module, -1, inputs),
                               "inputData": input_value})


# Returns the current value payload of one module input
def input_value(system_path, emitter, script, module, input_name):
    values = tool("GetModuleInputValues", {"moduleRef": ref(system_path, emitter, script, module)})
    for entry in values["inputs"]:
        if entry["name"] == input_name:
            return entry["value"].get("value")
    raise RuntimeError("create_thruster_assets: no input '%s' on %s" % (input_name, module))


# Adds a SetParameters module with the given (name, type, value) entries at the end of a script stack
def add_set_parameters(system_path, script, entries):
    parameters = [{"variable": {"name": name, "type": niagara_type(type_path)}, "defaultValue": ZERO[type_path]}
                  for name, type_path, _ in entries]
    module = tool("AddSetParametersModule", {"moduleLocationRef": ref(system_path, EMITTER_NAME, script),
                                             "parameters": parameters})
    for name, _, entry_value in entries:
        set_input(system_path, script, module["moduleName"], [name], entry_value)
    return module["moduleName"]


# Adds a SetParameters module whose one entry selects element Particles.UniqueID of a user array (direct index,
# clamped): the bolt system's technique (an ExecIndex() index stops the system activating, SDD 7 Amendment 2)
def add_array_select(system_path, attribute, attribute_type, selector, array_input, array_name, array_type):
    module = add_set_parameters(system_path, "ParticleUpdateScript", [(attribute, attribute_type,
        value(D_DYNAMIC, {"dynamicInputAsset": {"refPath": SELECT_INPUTS + selector + "." + selector}}))])
    set_input(system_path, "ParticleUpdateScript", module, [attribute, "Array Sampling Mode"],
              enum("/Niagara/Enums/ENiagaraArraySamplingMode.ENiagaraArraySamplingMode",
                   "ENiagaraArraySamplingMode::NewEnumerator1", "Direct Set"))
    set_input(system_path, "ParticleUpdateScript", module, [attribute, "Direct Array Index"], hlsl("Particles.UniqueID"))
    set_input(system_path, "ParticleUpdateScript", module, [attribute, array_input], linked(array_name, array_type))


# Returns this session's Niagara type handle of int32, read from a temporary sprite renderer's visibility-tag binding
# (the ribbon renderer has no int binding to copy it from); the temporary renderer is removed again
def int_type_handle(system_path):
    added = tool("AddRenderer", {"newRendererLocation": ref(system_path, EMITTER_NAME),
                                 "rendererClass": {"refPath": "/Script/Niagara.NiagaraSpriteRendererProperties"}})
    index = added["rendererIndex"]
    try:
        sprite = json.loads(tool("GetRendererData", {"rendererRef": ref(system_path, EMITTER_NAME, "", "", index)})
                            ["propertyValues"])
        return sprite["RendererVisibilityTagBinding"]["paramMapVariable"]["typeDefHandle"]
    finally:
        tool("RemoveRenderer", {"rendererToRemove": ref(system_path, EMITTER_NAME, "", "", index)})


# Builds the emitter, user parameters and ribbon renderer into an empty system
def build_trails_system(system_path, material_path):
    names = lambda key: text_constant(key, "ThrusterTrailParams")
    points_per_trail = int(number("RIBBON_POINTS_PER_TRAIL"))
    max_points = points_per_trail * (ribbon_count(3) + 1)   # the Epic default: 32 NPC trails and the player's
    bounds = number("NPC_RENDER_SPACE_BOUNDS_CM", None)

    # System: loop forever (the template already does; write only on a difference, an unchanged write is refused)
    infinite = enum("/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions",
                    "ENiagara_EmitterStateOptions::NewEnumerator0", "Infinite")
    if input_value(system_path, "", "SystemUpdateScript", "SystemState", "Loop Behavior") != infinite["value"]:
        tool("SetStackInputData", {"stackInputRef": ref(system_path, "", "SystemUpdateScript", "SystemState", -1,
                                                        ["Loop Behavior"]), "inputData": infinite})
    tool("AddUserVariables", {"system": {"refPath": system_path}, "variablesToAdd": [
        {"name": "User." + names("POINT_COUNT"), "type": niagara_type(T_INT), "defaultValue": value(T_INT, {"value": 0}),
         "description": "Live ribbon points this frame: entries of the arrays to draw"},
        {"name": "User." + names("MAX_POINTS"), "type": niagara_type(T_INT),
         "defaultValue": value(T_INT, {"value": max_points}),
         "description": "Particles spawned once; must cover every trail's points (trails x points per trail)"},
        {"name": "User." + names("POINTS_PER_TRAIL"), "type": niagara_type(T_INT),
         "defaultValue": value(T_INT, {"value": points_per_trail}),
         "description": "Points per trail (SOL::Thruster::RIBBON_POINTS_PER_TRAIL)"}]})

    # Emitter: CPU sim (the bolt system's GPU variant never drew), local space, fixed render-space bounds
    tool("AddEmitter", {"system": {"refPath": system_path}, "templateEmitter": {"refPath": EMITTER_TEMPLATE},
                        "emitterName": EMITTER_NAME})
    data = {"bLocalSpace": True, "SimTarget": "CPUSim", "CalculateBoundsMode": "Fixed", "bDeterminism": False,
            "FixedBounds": {"min": {"x": -bounds, "y": -bounds, "z": -bounds},
                            "max": {"x": bounds, "y": bounds, "z": bounds}, "isValid": True}}
    tool("SetEmitterData", {"emitter": ref(system_path, EMITTER_NAME),
                            "emitterData": {"propertyValues": json.dumps(data)}})

    # Emitter update: follow the system's life cycle, burst TrailMaxPoints once, copy the ints into Emitter.* (a User.*
    # read straight from a particle expression reads zero, SDD 7 Amendment 2)
    life_cycle = input_value(system_path, EMITTER_NAME, "EmitterUpdateScript", "EmitterState", "Life Cycle Mode")
    if life_cycle.get("displayName") != "System":
        set_input(system_path, "EmitterUpdateScript", "EmitterState", ["Life Cycle Mode"],
                  enum("/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode",
                       "ENiagaraEmitterLifeCycleMode::NewEnumerator0", "System"))
    tool("AddModule", {"moduleLocationRef": ref(system_path, EMITTER_NAME, "EmitterUpdateScript"),
                       "moduleAsset": {"refPath": SPAWN_BURST_MODULE}})
    set_input(system_path, "EmitterUpdateScript", "SpawnBurst_Instantaneous", ["Spawn Count"],
              linked("User." + names("MAX_POINTS"), T_INT))
    set_input(system_path, "EmitterUpdateScript", "SpawnBurst_Instantaneous", ["UseLoopCountLimit"],
              value(T_BOOL, {"value": NIAGARA_TRUE}))
    set_input(system_path, "EmitterUpdateScript", "SpawnBurst_Instantaneous", ["Loop Count Limit"],
              value(T_INT, {"value": 1}))
    add_set_parameters(system_path, "EmitterUpdateScript", [
        ("Emitter.TrailPointCount", T_INT, linked("User." + names("POINT_COUNT"), T_INT)),
        ("Emitter.TrailPointsPerTrail", T_INT, linked("User." + names("POINTS_PER_TRAIL"), T_INT))])

    # Particle update: never kill; read the point's position, colour and width; hide points past the live count;
    # the ribbon id and link order from the point index
    set_input(system_path, "ParticleUpdateScript", "ParticleState", ["Kill Particles When Lifetime Has Elapsed"],
              value(T_BOOL, {"value": 0}))
    add_array_select(system_path, "Particles.TrailPositionCm", T_VEC3, "SelectVectorFromArray",
                     "Vector Selection Array", "User." + names("POSITIONS_CM"), T_ARRAY_FLOAT3)
    add_array_select(system_path, "Particles.TrailColour", T_COLOR, "SelectLinearColorFromArray",
                     "Color Selection Array", "User." + names("COLOURS"), T_ARRAY_COLOR)
    add_array_select(system_path, "Particles.TrailWidthCm", T_FLOAT, "SelectFloatFromArray",
                     "Float Selection Array", "User." + names("WIDTHS_CM"), T_ARRAY_FLOAT)
    live = "Particles.UniqueID < Emitter.TrailPointCount"
    add_set_parameters(system_path, "ParticleUpdateScript", [
        ("Particles.Position", T_POSITION, hlsl("Particles.TrailPositionCm")),
        ("Particles.Color", T_COLOR, hlsl("%s ? Particles.TrailColour : float4(0.0, 0.0, 0.0, 0.0)" % live)),
        ("Particles.RibbonWidth", T_FLOAT, hlsl("%s ? Particles.TrailWidthCm : 0.0" % live)),
        ("Particles.TrailRibbon", T_INT, hlsl("Particles.UniqueID / max(Emitter.TrailPointsPerTrail, 1)")),
        ("Particles.RibbonLinkOrder", T_FLOAT, hlsl(
            "(float)(Particles.UniqueID - (Particles.UniqueID / max(Emitter.TrailPointsPerTrail, 1))"
            " * max(Emitter.TrailPointsPerTrail, 1))"))])

    # Renderer: the template's sprite renderer replaced by a camera-facing ribbon, its id bound to the int
    # Particles.TrailRibbon (the default Particles.RibbonID is a NiagaraID)
    tool("RemoveRenderer", {"rendererToRemove": ref(system_path, EMITTER_NAME, "", "", 0)})
    renderer = tool("AddRenderer", {"newRendererLocation": ref(system_path, EMITTER_NAME),
                                    "rendererClass": {"refPath": RIBBON_RENDERER}})
    log("ribbon renderer %s" % json.dumps(renderer))
    index = 0
    current = json.loads(tool("GetRendererData", {"rendererRef": ref(system_path, EMITTER_NAME, "", "", index)})
                         ["propertyValues"])
    ribbon_id = dict(current["RibbonIdBinding"])
    int_type = int_type_handle(system_path)
    ribbon_id["rootName"] = "TrailRibbon"   # the binding is rebuilt from its root on load
    ribbon_id["paramMapVariable"] = {"name": "Particles.TrailRibbon", "typeDefHandle": int_type}
    ribbon_id["dataSetName"] = "TrailRibbon"
    ribbon_id["cachedDisplayName"] = "Particles.TrailRibbon"
    wanted = {"Material": {"refPath": material_path}, "FacingMode": "Screen", "MotionVectorSetting": "Disable",
              "bCastShadows": False, "bIncludeInHitProxy": False, "RibbonIdBinding": ribbon_id}
    data = {key: entry for key, entry in wanted.items() if key in current}
    log("ribbon renderer properties not found: %s" % sorted(set(wanted) - set(data)))
    tool("SetRendererData", {"renderer": ref(system_path, EMITTER_NAME, "", "", index),
                             "rendererData": {"propertyValues": json.dumps(data)}})
    bound = json.loads(tool("GetRendererData", {"rendererRef": ref(system_path, EMITTER_NAME, "", "", index)})
                       ["propertyValues"])["RibbonIdBinding"]
    log("ribbon id binding: %s" % json.dumps(bound.get("paramMapVariable")))

    # Toggling a module forces a real recompile, so no stale compiled renderer is saved (SDD 6 Amendment 15)
    for enabled in (False, True):
        tool("SetModuleEnabled", {"moduleRef": ref(system_path, EMITTER_NAME, "ParticleUpdateScript",
                                                   "ParticleState"), "bEnabled": enabled})


# Creates the ribbon system (a rebuild deletes it first)
def build_trails(material):
    package, folder, name = asset_path("THRUSTER_TRAILS_NIAGARA_SYSTEM")
    system_path = package + "." + name
    if not selected(name):
        return
    if EAL.does_asset_exist(package):
        if not REBUILD:
            log("%s kept" % package)
            return
        if not EAL.delete_asset(package):
            raise RuntimeError("create_thruster_assets: could not delete %s for the rebuild" % package)
    tool("CreateNiagaraSystem", {"assetName": name, "assetPath": folder, "templateSystem": {"refPath": SYSTEM_TEMPLATE}})
    summary = tool("GetSystemSummary", {"system": {"refPath": system_path}})
    for emitter in summary.get("emitters", []):
        tool("RemoveEmitter", {"emitterToRemove": ref(system_path, emitter["emitterName"])})
    build_trails_system(system_path, material.get_path_name())
    # Saving waits for the system's compile (SOLTest.Thruster.MaterialAssets checks it is valid). The asynchronous
    # compile-state and stack-issue tools are not called: in a commandlet they return before finishing, and the
    # unfinished requests fail an engine assertion when the toolset registry shuts down
    EAL.save_loaded_asset(EAL.load_asset(package))
    log("built %s" % package)


noise_texture = build_noise_texture()
thruster_materials = build_materials(noise_texture)
build_meshes(thruster_materials)
build_trails(thruster_materials["THRUSTER_TRAIL_MATERIAL"])
log("done")
