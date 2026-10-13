# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates the star-field runtime materials (sub-part 4c of Part 4 / issue #5, SDD 5 section 3.2; rebuilt for issue #13,
# SDD 13 Amendment 1), headless and idempotent:
#   /Game/SOL/StarField/M_SOLStarSprite  bright-star sprite: unlit, additive, two-sided, used with instanced static
#                                        meshes. Every frame the vertex shader resizes each quad (world position
#                                        offset) to a kernel PointDiameterPx output pixels across, whatever the FOV,
#                                        resolution or screen percentage; the per-instance (Flux * Color) times
#                                        BrightnessScale / kernel pixel area is its peak, so its pixel sum is
#                                        Flux * BrightnessScale at any zoom (size and brightness both fixed on screen)
#   /Game/SOL/StarField/M_SOLStarSky     faint-star background: unlit, opaque, two-sided. Samples T_SOLStarFieldCube
#                                        along the view direction with its own sampler and hardware mip (as SDD 5),
#                                        times the pixel / texel solid-angle ratio (texel values are flux per texel),
#                                        while texels are minified or about 1:1; when they are magnified (narrow FOV,
#                                        high resolution) it rebuilds each star from its bilinear splat (local-maximum
#                                        2x2 texel cell: sum minus the diffuse level is the flux, the weighted
#                                        centroid the position), draws it as the same pixel-sized kernel as the
#                                        sprites and keeps the diffuse level as a faint glow; times BrightnessScale
# The view-dependent math is in Custom HLSL nodes; SOLStarBrightness (Source/SOLTest/StarField) is its tested
# reference. Parameter names must match SOL::StarFieldMaterialParams in Source/SOLTest/SOLConstants.h, and the asset
# paths SOL::Paths. T_SOLStarFieldCube must already exist (run import_star_field.py first).
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
#       -script="<abs path>/Tools/StarField/create_star_field_materials.py" -unattended [-stdout -FullStdOutLogOutput]
#   Pass -SOLRebuild on the command line to recreate existing materials (inside a running editor, exec the file with
#   SOL_FORCE_REBUILD = True in its globals instead).
import unreal

MATERIAL_DIR = "/Game/SOL/StarField"
SPRITE_NAME = "M_SOLStarSprite"
SKY_NAME = "M_SOLStarSky"
CUBE_PATH = "/Game/SOL/StarField/T_SOLStarFieldCube"
QUAD_MESH_PATH = "/Game/SOL/StarField/CelestialVault/SM_Plane_FacingX"
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()
           or globals().get("SOL_FORCE_REBUILD", False))

# Half the edge of the copied star quad (100 cm, in its local YZ plane, facing +X), checked against its bounds below
QUAD_HALF_EXTENT_CM = 50.0

# Defaults only; ASOLStarField sets every parameter at runtime from the data asset and SOLConstants.h
DEFAULT_CUBE_TEXEL_SOLID_ANGLE_SR = (2.0 / 2048.0) ** 2
DEFAULT_CUBE_FACE_SIZE = 2048.0
DEFAULT_BASE_ANGULAR_DIAMETER_RAD = 0.02
DEFAULT_POINT_DIAMETER_PX = 2.5

# HLSL expression: angular size (rad) of one output (post-upscale) pixel at the view center, 2 tan(fov_x / 2) /
# output width (ViewToClipNoAA[0][0] is 1 / tan(fov_x / 2); ViewSize is the render size and the resolution fraction
# undoes upscaling; SOLStarBrightness::PixelAngularSizeRad is the reference). Caveat: with a non-temporal (spatial)
# upscaler at a low screen percentage the 2.5 output-pixel kernel is under 2 render pixels and is not accumulated over
# frames, so it can shimmer as it moves; TSR (this project's default) resolves it
PIXEL_RAD_CENTER_HLSL = ("(2.0 / (View.ViewToClipNoAA[0][0] * View.ViewSizeAndInvSize.x"
                         " / max(View.ResolutionFractionAndInv.x, 1e-4)))")
# HLSL expression: the same toward direction DIR (unnormalized, world axes). Off axis a rectilinear pixel subtends
# cos^2 of the center value radially and cos tangentially (solid angle cos^3); scaling by cos^1.5 keeps the kernel's
# solid angle equal to the same number of pixels, so its pixel area and pixel flux stay constant across the frame. Its
# shape is still stretched radially by 1 / sqrt(cos) on screen (about 1.5x at the 135 deg warp's edges)
PIXEL_RAD_HLSL = ("(%s * pow(clamp(dot(normalize(DIR), View.ViewForward), 0.05, 1.0), 1.5))"
                  % PIXEL_RAD_CENTER_HLSL)

# Point-mode early out (sky): a pixel whose own texel cell's light above the local background would peak below this
# pixel value (linear, before exposure) skips the star search; 1e-3 linear is ~3/255 on an sRGB display (less after
# the tonemapper's toe), so a skipped star would not have been visible
SKY_POINT_SKIP_PEAK = 1.0e-3

# Sprite vertex shader: peak pixel value per unit flux (SOLStarBrightness::PointPeakPerFlux), so a star's pixel sum is
# flux * BrightnessScale at any FOV and resolution
SPRITE_COLOR_HLSL = """
float radiusPx = 0.5 * DiameterPx;
return InstanceColor * (Brightness * 3.0 / (3.14159265 * radiusPx * radiusPx));
"""

# Sprite vertex shader: factor that resizes the quad from its built angular size to the kernel, never larger than built
# (the built size is the bounds; a kernel wider than it would be culled at the screen edge)
SPRITE_SCALE_HLSL = """
return min(DiameterPx * %s / BaseDiameter - 1.0, 0.0);
""" % PIXEL_RAD_HLSL.replace("DIR", "CameraRelative")

# Sky pixel shader: filtered texels (one sample, as in SDD 5) when minified, pixel-sized point stars rebuilt from the
# bilinear splat when the texels are magnified, blended per pixel by SOLStarBrightness::CubePointBlend of that pixel's
# own size against the local texel's (texels shrink toward face edges). A bake star's light sits in the 2x2 texels
# around it (one texel cell, the square between four texel centers). In point mode the pixel first fetches its own cell:
# if its light above the local background (the lower of the cell's minimum and a mip-2 sample) would peak below
# SKY_POINT_SKIP_PEAK it draws only the diffuse glow (most of the sky, even on the galactic plane). Otherwise it fetches
# the 4x4 texels around its cell (solid-angle weighted): their per-channel minimum is the local diffuse level
# (unresolved faint stars, drawn as a faint glow); each of the 3x3 cells whose 2x2 sum is a local maximum among its
# neighbours holds a star whose flux is that sum minus the diffuse level and whose position is the weighted centroid of
# its four texels. Cells sharing only part of a star's splat have smaller sums and are rejected, so the splat draws one
# point, not ghosts. Every star within the kernel radius (at most one texel in this mode) is in those 3x3 cells. Texels
# past a face edge are fetched along their own (unclamped) direction, which lands on the neighbouring face, so no texel
# is counted twice at a seam. Both modes give pixel flux: filtered texels times the pixel / texel solid-angle ratio,
# points peaking at flux * BrightnessScale / kernel pixel area
SKY_HLSL = """
float3 D = normalize(Dir);
float pixelRad = %(pixel)s;
float texelSr = max(TexelSr, 1e-12);
float texelRad = 2.0 / max(FaceSize, 1.0);
float kernelDiameter = DiameterPx * pixelRad;
float radiusPx = 0.5 * DiameterPx;
float peakPerFlux = 3.0 / (3.14159265 * radiusPx * radiusPx);
// Cube face and face coordinates of the view direction; a texel's angular size there shrinks as (1 + s^2)^-0.75 from
// the face center (its solid angle as (1 + s^2)^-1.5), and the blend compares it with this pixel's own size
float3 a = abs(D);
float3 M; float3 U; float3 V; float ma;
if (a.x >= a.y && a.x >= a.z) { M = float3(sign(D.x), 0, 0); U = float3(0, 1, 0); V = float3(0, 0, 1); ma = a.x; }
else if (a.y >= a.z) { M = float3(0, sign(D.y), 0); U = float3(1, 0, 0); V = float3(0, 0, 1); ma = a.y; }
else { M = float3(0, 0, sign(D.z)); U = float3(1, 0, 0); V = float3(0, 1, 0); ma = a.z; }
float2 st = float2(dot(D, U), dot(D, V)) / ma;
float stWeight = pow(1.0 + dot(st, st), -1.5);
float wPoint = saturate((texelRad * sqrt(stWeight) / pixelRad - 1.0) / 0.25);
// Filtered texels: the same single hardware-filtered sample as SDD 5 (the cube's own sampler and mip selection),
// skipped where the pixel is all point stars
float3 result = 0;
[branch] if (wPoint < 1.0)
{
    result = (1.0 - wPoint) * (pixelRad * pixelRad / texelSr) * Cube.Sample(CubeSampler, D).rgb;
}
[branch] if (wPoint > 0.0)
{
    float2 cellBase = floor((st + 1.0) * 0.5 * FaceSize - 0.5) - 1.0;
    float glowScale = pixelRad * pixelRad / (texelSr * stWeight);

    // Early out: the pixel's own cell holds part of every star within the kernel radius; if even its light above the
    // local background (the lower of its minimum and a mip-2 sample, so a star centered in the cell is not taken for
    // background) would peak below the skip level, draw only the diffuse glow (most pixels pay 5 fetches)
    float3 own = 0;
    float3 ownMin = 1e30;
    [unroll] for (int k0 = 0; k0 < 4; ++k0)
    {
        float2 tc = cellBase + 1.0 + float2(k0 %% 2, k0 / 2);
        float2 tst = (tc + 0.5) / FaceSize * 2.0 - 1.0;
        float3 v = Cube.SampleLevel(View.SharedPointClampedSampler, M + tst.x * U + tst.y * V, 0).rgb;
        v *= pow(1.0 + dot(tst, tst), -1.5);
        own += v;
        ownMin = min(ownMin, v);
    }
    float3 background = min(ownMin, Cube.SampleLevel(View.SharedPointClampedSampler, D, 2.0).rgb * stWeight);
    float3 ownExcess = max(own - 4.0 * background, 0.0);
    float3 pointResult = ownMin * glowScale;
    [branch] if ((ownExcess.r + ownExcess.g + ownExcess.b) * peakPerFlux * Brightness >= 3.0 * %(skip)g)
    {
        float3 texel[16];
        float3 diffuse = 1e30;
        [unroll] for (int t = 0; t < 16; ++t)
        {
            float2 tc = cellBase + float2(t %% 4, t / 4);
            float2 tst = (tc + 0.5) / FaceSize * 2.0 - 1.0;
            float3 v = Cube.SampleLevel(View.SharedPointClampedSampler, M + tst.x * U + tst.y * V, 0).rgb;
            texel[t] = v * pow(1.0 + dot(tst, tst), -1.5);
            diffuse = min(diffuse, texel[t]);
        }
        float cellSum[9];
        [unroll] for (int c = 0; c < 9; ++c)
        {
            int t0 = (c / 3) * 4 + c %% 3;
            float3 s = texel[t0] + texel[t0 + 1] + texel[t0 + 4] + texel[t0 + 5];
            cellSum[c] = s.r + s.g + s.b;
        }
        float r = 0.5 * kernelDiameter;
        float3 drawn = 0;
        [unroll] for (int c2 = 0; c2 < 9; ++c2)
        {
            int ci = c2 %% 3;
            int cj = c2 / 3;
            bool isPeak = true;
            [unroll] for (int o = 0; o < 9; ++o)
            {
                int oi = o %% 3;
                int oj = o / 3;
                if (o != c2 && abs(oi - ci) <= 1 && abs(oj - cj) <= 1)
                {
                    isPeak = isPeak && (o < c2 ? cellSum[c2] > cellSum[o] : cellSum[c2] >= cellSum[o]);
                }
            }
            if (isPeak)
            {
                float3 excess = 0;
                float2 starPos = 0;
                float lsum = 0;
                [unroll] for (int k = 0; k < 4; ++k)
                {
                    int ti = ci + k %% 2;
                    int tj = cj + k / 2;
                    float3 e = max(texel[tj * 4 + ti] - diffuse, 0.0);
                    float l = e.r + e.g + e.b;
                    excess += e;
                    starPos += float2(ti, tj) * l;
                    lsum += l;
                }
                if (lsum > 0.0)
                {
                    starPos /= lsum;
                    float2 cst = (cellBase + starPos + 0.5) / FaceSize * 2.0 - 1.0;
                    float3 cd = normalize(M + cst.x * U + cst.y * V);
                    float sinD = length(cross(D, cd));
                    float q = saturate(1.0 - sinD * sinD / (r * r));
                    drawn += excess * q * q;
                }
            }
        }
        pointResult = diffuse * glowScale + drawn * peakPerFlux;
    }
    result += wPoint * pointResult;
}
return result * Brightness;
""" % {"pixel": PIXEL_RAD_HLSL.replace("DIR", "Dir"), "skip": SKY_POINT_SKIP_PEAK}

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


# Creates a Custom HLSL node with named inputs wired from (name, expression) pairs and the given output type
def custom(material, code, output_type, inputs, x, y, description):
    expr = node(material, unreal.MaterialExpressionCustom, x, y)
    expr.set_editor_property("code", code)
    expr.set_editor_property("output_type", output_type)
    expr.set_editor_property("description", description)
    custom_inputs = []
    for name, _ in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    expr.set_editor_property("inputs", custom_inputs)
    for name, src in inputs:
        connect(src, "", expr, name)
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

    brightness = scalar(material, "BrightnessScale", 1.0, -1400, -200)
    diameter_px = scalar(material, "PointDiameterPx", DEFAULT_POINT_DIAMETER_PX, -1400, -100)
    base_diameter = scalar(material, "BaseAngularDiameterRad", DEFAULT_BASE_ANGULAR_DIAMETER_RAD, -1400, 600)

    # Per-instance custom data 0..2 = Flux * linear Color (ASOLStarField), scaled to emissive in the vertex shader
    instance_color = node(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, -1400, 0)
    instance_color.set_editor_property("data_index", 0)

    # Vertex position relative to the camera, before this material's own offset: the direction toward the star
    camera_relative = node(material, unreal.MaterialExpressionWorldPosition, -1400, 100)
    camera_relative.set_editor_property("world_position_shader_offset",
                                        unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE_NO_OFFSETS)
    color_vs = custom(material, SPRITE_COLOR_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                      [("InstanceColor", instance_color), ("Brightness", brightness), ("DiameterPx", diameter_px)],
                      -1000, -150, "StarKernelColor")
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

    # World position offset: the vertex's offset from the instance center (instance space to world) times
    # (kernel / built size - 1), so every quad is drawn PointDiameterPx output pixels across
    to_world = node(material, unreal.MaterialExpressionTransform, -1000, 400)
    to_world.set_editor_property("transform_source_type",
                                 unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_INSTANCE)
    to_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(local_pos, "", to_world, "")
    resize = custom(material, SPRITE_SCALE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                    [("DiameterPx", diameter_px), ("BaseDiameter", base_diameter),
                     ("CameraRelative", camera_relative)], -1000, 550, "StarKernelResize")
    offset = binary(material, unreal.MaterialExpressionMultiply, to_world, resize, -700, 450)

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

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

    cube_object = node(material, unreal.MaterialExpressionTextureObject, -900, 150)
    cube_object.set_editor_property("texture", cube)
    texel = scalar(material, "CubeTexelSolidAngleSr", DEFAULT_CUBE_TEXEL_SOLID_ANGLE_SR, -900, 300)
    face_size = scalar(material, "CubeFaceSize", DEFAULT_CUBE_FACE_SIZE, -900, 400)
    diameter_px = scalar(material, "PointDiameterPx", DEFAULT_POINT_DIAMETER_PX, -900, 500)
    brightness = scalar(material, "BrightnessScale", 1.0, -900, 600)
    emissive = custom(material, SKY_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                      [("Dir", view_dir), ("Cube", cube_object), ("TexelSr", texel), ("FaceSize", face_size),
                       ("DiameterPx", diameter_px), ("Brightness", brightness)], -400, 200, "StarSky")

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("SOLStarMaterials: created %s" % path)


build_sprite()
build_sky()
