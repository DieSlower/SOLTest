# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates the star-field runtime materials (sub-part 4c of Part 4 / issue #5, SDD 5 section 3.2), headless and
# idempotent:
#   /Game/SOL/StarField/M_SOLStarSprite  bright-star sprite: unlit, additive, two-sided, used with instanced static
#                                        meshes. Emissive = per-instance (Flux * Color) * CubeTexelSolidAngleSr *
#                                        BrightnessScale / SpriteSolidAngleSr, shaped by a round falloff normalized to
#                                        mean 1 over the quad, so a sprite's total light equals a cube star of the same
#                                        flux (the contract in Source/SOLTest/StarField/SOLStarFieldData.h)
#   /Game/SOL/StarField/M_SOLStarSky     faint-star background: unlit, opaque, two-sided, samples T_SOLStarFieldCube
#                                        along the view direction, times BrightnessScale
# Parameter names must match SOL::StarFieldMaterialParams in Source/SOLTest/SOLConstants.h, and the asset paths
# SOL::Paths. T_SOLStarFieldCube must already exist (run import_star_field.py first).
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
#       -script="<abs path>/Tools/StarField/create_star_field_materials.py" -unattended [-stdout -FullStdOutLogOutput]
#   Pass -SOLRebuild on the command line to recreate existing materials.
import math

import unreal

MATERIAL_DIR = "/Game/SOL/StarField"
SPRITE_NAME = "M_SOLStarSprite"
SKY_NAME = "M_SOLStarSky"
CUBE_PATH = "/Game/SOL/StarField/T_SOLStarFieldCube"
QUAD_MESH_PATH = "/Game/SOL/StarField/CelestialVault/SM_Plane_FacingX"
REBUILD = "-solrebuild" in unreal.SystemLibrary.get_command_line().lower()

# Half the edge of the copied star quad (100 cm, in its local YZ plane, facing +X), checked against its bounds below
QUAD_HALF_EXTENT_CM = 50.0

# Mean over the unit square [-1,1]^2 of the falloff saturate(1 - r^2)^2: its disk integral is pi/3, over an area of 4
FALLOFF_MEAN = math.pi / 12.0

# Defaults only; ASOLStarField sets every parameter at runtime from the data asset and SOLConstants.h
DEFAULT_CUBE_TEXEL_SOLID_ANGLE_SR = (2.0 / 2048.0) ** 2
DEFAULT_SPRITE_SOLID_ANGLE_SR = (6.0 * 2.0 / 2048.0) ** 2

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


# Connects two expressions and fails loudly if the named pin does not exist
def connect(src, src_output, dst, dst_input):
    if not MEL.connect_material_expressions(src, src_output, dst, dst_input):
        raise RuntimeError("SOLStarMaterials: failed to connect %s -> %s.%r" % (src.get_name(), dst.get_name(),
                                                                               dst_input))


# Creates a material expression node at a graph position
def node(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


# Creates a named scalar parameter
def scalar(material, name, default, x, y):
    expr = node(material, unreal.MaterialExpressionScalarParameter, x, y)
    expr.set_editor_property("parameter_name", name)
    expr.set_editor_property("default_value", default)
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


# Creates a component mask node keeping the given channels of src
def mask(material, src, channels, x, y):
    expr = node(material, unreal.MaterialExpressionComponentMask, x, y)
    for channel in "rgba":
        expr.set_editor_property(channel, channel in channels)
    connect(src, "", expr, "")
    return expr


# Routes a vertex-shader value to the pixel shader through a vertex interpolator
def interpolate(material, src, x, y):
    expr = node(material, unreal.MaterialExpressionVertexInterpolator, x, y)
    connect(src, "", expr, "VS")
    return expr


# Deletes an existing material when rebuilding; returns False if it exists and should be kept
def prepare(path):
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("SOLStarMaterials: %s exists, skipping" % path)
            return False
        EAL.delete_asset(path)
    return True


# Fails unless the star quad is the 100 cm YZ-plane quad the sprite falloff assumes
def check_quad_mesh():
    mesh = EAL.load_asset(QUAD_MESH_PATH)
    if mesh is None:
        raise RuntimeError("SOLStarMaterials: %s missing (run import_star_field.py first)" % QUAD_MESH_PATH)
    bounds = mesh.get_bounds()
    extent = bounds.box_extent
    origin = bounds.origin
    unreal.log("SOLStarMaterials: %s bounds origin %s extent %s" % (QUAD_MESH_PATH, bounds.origin, extent))
    if max(abs(origin.x), abs(origin.y), abs(origin.z)) > 0.5:
        raise RuntimeError("SOLStarMaterials: %s pivot is not at its center" % QUAD_MESH_PATH)
    if abs(extent.x) > 1.0 or abs(extent.y - QUAD_HALF_EXTENT_CM) > 0.5 or abs(extent.z - QUAD_HALF_EXTENT_CM) > 0.5:
        raise RuntimeError("SOLStarMaterials: %s is not a %g cm YZ quad" % (QUAD_MESH_PATH, 2 * QUAD_HALF_EXTENT_CM))


# Builds the bright-star sprite material
def build_sprite():
    path = MATERIAL_DIR + "/" + SPRITE_NAME
    if not prepare(path):
        return
    check_quad_mesh()
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(SPRITE_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())

    # Brightness per unit flux, a uniform expression: CubeTexelSolidAngleSr * BrightnessScale / (SpriteSolidAngleSr *
    # FALLOFF_MEAN). Dividing by the falloff's mean makes the shaped sprite emit exactly flux * texel / sprite angle
    texel = scalar(material, "CubeTexelSolidAngleSr", DEFAULT_CUBE_TEXEL_SOLID_ANGLE_SR, -1400, -300)
    sprite_sa = scalar(material, "SpriteSolidAngleSr", DEFAULT_SPRITE_SOLID_ANGLE_SR, -1400, -200)
    brightness = scalar(material, "BrightnessScale", 1.0, -1400, -100)
    texel_scaled = binary(material, unreal.MaterialExpressionMultiply, texel, brightness, -1200, -250)
    sprite_norm = binary(material, unreal.MaterialExpressionMultiply, sprite_sa, FALLOFF_MEAN, -1200, -150)
    per_flux = binary(material, unreal.MaterialExpressionDivide, texel_scaled, sprite_norm, -1000, -200)

    # Per-instance custom data 0..2 = Flux * linear Color (ASOLStarField), scaled in the vertex shader
    instance_color = node(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, -1200, 0)
    instance_color.set_editor_property("data_index", 0)
    color_vs = binary(material, unreal.MaterialExpressionMultiply, instance_color, per_flux, -800, -100)
    color_ps = interpolate(material, color_vs, -600, -100)

    # Quad coordinates in [-1, 1]: the mesh-local YZ position over the half extent, interpolated linearly
    local_pos = node(material, unreal.MaterialExpressionLocalPosition, -1400, 200)
    local_yz = mask(material, local_pos, "gb", -1200, 200)
    quad_uv_vs = binary(material, unreal.MaterialExpressionMultiply, local_yz, 1.0 / QUAD_HALF_EXTENT_CM, -1000, 200)
    quad_uv = interpolate(material, quad_uv_vs, -800, 200)

    # Round soft falloff saturate(1 - r^2)^2: 1 at the center, 0 at the inscribed circle and outside it
    r2 = binary(material, unreal.MaterialExpressionDotProduct, quad_uv, quad_uv, -600, 200)
    one_minus = node(material, unreal.MaterialExpressionOneMinus, -450, 200)
    connect(r2, "", one_minus, "")
    falloff_lin = node(material, unreal.MaterialExpressionSaturate, -300, 200)
    connect(one_minus, "", falloff_lin, "")
    falloff = binary(material, unreal.MaterialExpressionMultiply, falloff_lin, falloff_lin, -150, 200)

    emissive = binary(material, unreal.MaterialExpressionMultiply, color_ps, falloff, 50, 0)

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("SOLStarMaterials: created %s" % path)


# Builds the faint-star background material
def build_sky():
    path = MATERIAL_DIR + "/" + SKY_NAME
    if not prepare(path):
        return
    cube = EAL.load_asset(CUBE_PATH)
    if cube is None:
        raise RuntimeError("SOLStarMaterials: %s missing (run import_star_field.py first)" % CUBE_PATH)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(SKY_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())

    # View direction from the camera through the pixel, in Unreal world axes: the bake's cube convention (SDD 5 4b)
    camera_vector = node(material, unreal.MaterialExpressionCameraVectorWS, -900, 0)
    view_dir = binary(material, unreal.MaterialExpressionMultiply, camera_vector, -1.0, -700, 0)

    sample = node(material, unreal.MaterialExpressionTextureSample, -500, 0)
    sample.set_editor_property("texture", cube)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    connect(view_dir, "", sample, "UVs")
    rgb = mask(material, sample, "rgb", -300, 0)

    brightness = scalar(material, "BrightnessScale", 1.0, -300, 200)
    emissive = binary(material, unreal.MaterialExpressionMultiply, rgb, brightness, -100, 0)

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("SOLStarMaterials: created %s" % path)


build_sprite()
build_sky()
