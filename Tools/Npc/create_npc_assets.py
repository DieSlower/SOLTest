# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates (or, with SOL_REBUILD / -SOLRebuild, rebuilds in place) the NPC renderer assets (issue #11, SDD 11 section
# 3.4, Part 10c), idempotent:
#   /Game/SOL/Npc/T_SOLNpcFloatDefault
#       A linear (non-sRGB, HDR) placeholder texture: the default of the material's float texture parameters, so
#       the runtime float textures match their sampler type.
#   /Game/SOL/Npc/M_SOLNpcPoints
#       Unlit, additive, two-sided. Drawn on ASOLNpcVisuals's point-block mesh (UV0 = billboard corner, UV1 = ship
#       index in its block as (index % 64, index / 64), ISM custom data 0 = the block's first ship index). The vertex
#       stage either evaluates the ship's rail (mode 0, NpcRailTex two texels per ship + NpcGroupTex group points
#       relative to the viewpoint) or reads its viewpoint-relative position (mode 1, NpcCpuTex), applies the far-depth compression of
#       SOLRender::ComputePlacement, hides rail ships inside the Mid radius, and expands a camera-facing triangle with
#       a minimum pixel size; brightness falls off with distance. The pixel stage draws a soft faction-tinted dot.
#   /Game/SOL/Npc/M_SOLNpcHull
#       Default-lit placeholder hull (vector parameter Tint), usable on ISMs.
# Parameter names must match SOLNpcVisuals.cpp; asset paths SOL::Paths::NPC_POINTS_MATERIAL / NPC_HULL_MATERIAL.
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/Npc/create_npc_assets.py"
#       -unattended [-SOLRebuild]
import unreal

NPC_DIR = "/Game/SOL/Npc"
POINTS_NAME = "M_SOLNpcPoints"
HULL_NAME = "M_SOLNpcHull"
DEFAULT_TEXTURE_NAME = "T_SOLNpcFloatDefault"
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

VERTEX_HLSL = r"""// NPC point vertex (SDD 11 3.4): the block mesh sits at the origin with identity instances,
// so the offset is the target render position minus the local position
const float3 LocalPos = Parameters.PreSkinnedPosition;
Shade = float2(0.0, 0.0);
const float3 hiddenOffset = -LocalPos;
const float shipIndex = ShipBase + floor(ShipUv.x + 0.5) + 64.0 * floor(ShipUv.y + 0.5);
if (shipIndex >= P0.z)
{
    return hiddenOffset;
}
const uint idx = (uint)(shipIndex + 0.5);
float3 relM;
float faction;
if (P0.w < 0.5)
{
    // Rail mode: two texels per ship (kind + 4 faction + 8 group, P0..P2), (P3..P6)
    const uint railWidth = (uint)(P3.z + 0.5);
    const uint t0 = idx * 2;
    const float4 a = RailTex.Load(int3(t0 % railWidth, t0 / railWidth, 0));
    const float4 b = RailTex.Load(int3((t0 + 1) % railWidth, (t0 + 1) / railWidth, 0));
    const uint packed = (uint)(a.x + 0.5);
    const uint kind = packed & 3;
    faction = (float)((packed >> 2) & 1);
    const uint group = packed >> 3;
    const float3 pA = GroupTex.Load(int3(group, 0, 0)).xyz;
    const float t = P0.x;
    if (kind == 1)
    {
        // Lane: triangle wave along A-B plus the lateral offset in the lane basis (SOLNpcScale::LaneBasis)
        const float3 pB = GroupTex.Load(int3(group, 1, 0)).xyz;
        const float x = a.y + a.z * t;
        const float m = x - 2.0 * floor(x * 0.5);
        const float s = m <= 1.0 ? m : 2.0 - m;
        const float3 axis = pB - pA;
        const float3 ax = normalize(axis);
        const float3 refAxis = abs(ax.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        const float3 u = normalize(cross(refAxis, ax));
        const float3 v = cross(ax, u);
        relM = pA + axis * s + u * a.w + v * b.x;
    }
    else if (kind == 2)
    {
        // Cluster: circular orbit r (cos theta P + sin theta Q)
        float si, ci, sn, cn, st, ct;
        sincos(a.z, si, ci);
        sincos(a.w, sn, cn);
        sincos(b.x + b.y * t, st, ct);
        relM = pA + a.y * float3(cn * ct - ci * sn * st, sn * ct + ci * cn * st, si * st);
    }
    else
    {
        // Battle: fixed slot around the battle centre
        relM = pA + a.yzw;
    }
    if (length(relM) < P0.y)
    {
        return hiddenOffset;
    }
}
else
{
    // CPU mode: viewpoint-relative position (m) and faction
    const uint cpuWidth = (uint)(P3.w + 0.5);
    const float4 c = CpuTex.Load(int3(idx % cpuWidth, idx / cpuWidth, 0));
    relM = c.xyz;
    faction = c.w;
}

// Far-depth compression (SOLRender::ComputePlacement), ecliptic to Unreal axes, viewpoint render location
const float dM = max(length(relM), 1.0);
const float maxCm = P1.w;
const float dCm = dM * 100.0;
float scale = 100.0;
if (dCm > maxCm)
{
    scale = 100.0 * maxCm * (1.0 + 0.1 * log(dCm / maxCm)) / dCm;
}
const float3 placed = relM * scale;
const float3 target = float3(placed.x, -placed.y, placed.z) + P1.xyz;

// Camera-facing triangle: the ship's radius, never below the minimum pixel size
const float placedCm = length(placed);
const float pixelCm = 2.0 * placedCm / (ResolvedView.ViewToClip[1][1] * ResolvedView.ViewSizeAndInvSize.y);
const float radiusCm = max(P2.y * scale, 0.5 * P2.x * pixelCm);
const float3 right = ResolvedView.ViewToTranslatedWorld[0].xyz;
const float3 up = ResolvedView.ViewToTranslatedWorld[1].xyz;
const float2 corner = Corner;
Shade = float2(faction, clamp(pow(P2.z / dM, P3.x), P2.w, 1.0));
return target + (right * corner.x + up * corner.y) * radiusCm - LocalPos;"""

PIXEL_HLSL = r"""// NPC point pixel: a soft dot inside the billboard triangle, faction tint times distance brightness
const float r = length(Corner);
float mask = saturate(1.0 - r);
mask = mask * mask;
const float3 color = lerp(C0.rgb, C1.rgb, saturate(Shade.x));
return color * (Shade.y * P3.y * mask);"""


# Adds an expression of the given class at an editor position
def add(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


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


# Adds a vector parameter node
def add_vector(material, name, x, y, default=unreal.LinearColor(0.0, 0.0, 0.0, 0.0)):
    node = add(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


# Returns the linear placeholder texture, creating it from the engine default texture
def ensure_default_texture():
    path = NPC_DIR + "/" + DEFAULT_TEXTURE_NAME
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path)
    texture = EAL.duplicate_asset("/Engine/EngineResources/DefaultTexture", path)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_HDR)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    EAL.save_loaded_asset(texture)
    return texture


# Removes every expression of a material (repeat: one pass can leave some behind)
def clear_material(material):
    for _ in range(8):
        expressions = list(MEL.get_material_expressions(material))
        if not expressions:
            break
        for expression in expressions:
            MEL.delete_material_expression(material, expression)


# Returns the material at a path for (re)building, or None when it exists and no rebuild is requested
def material_for_build(name):
    path = NPC_DIR + "/" + name
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("create_npc_assets: %s exists (pass -SOLRebuild to rebuild)" % path)
            return None
        material = EAL.load_asset(path)
        clear_material(material)
        return material
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, NPC_DIR, unreal.Material,
        unreal.MaterialFactoryNew())


# Builds the point material graph
def build_points(material, default_texture):
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("use_translucency_vertex_fog", False)
    material.set_editor_property("used_with_instanced_static_meshes", True)

    # Inputs: instance custom data, UV0 (corner), the textures and parameter vectors (the vertex code reads the local
    # position from Parameters.PreSkinnedPosition)
    ship_base = add(material, unreal.MaterialExpressionPerInstanceCustomData, -1600, -400)
    ship_base.set_editor_property("data_index", 0)
    uv0 = add(material, unreal.MaterialExpressionTextureCoordinate, -1600, -200)
    uv1 = add(material, unreal.MaterialExpressionTextureCoordinate, -1600, -300)
    uv1.set_editor_property("coordinate_index", 1)
    textures = {}
    for index, name in enumerate(["NpcRailTex", "NpcGroupTex", "NpcCpuTex"]):
        node = add(material, unreal.MaterialExpressionTextureObjectParameter, -1600, -100 + 120 * index)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("texture", default_texture)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        textures[name] = node
    params = {}
    for index, name in enumerate(["NpcParams0", "NpcParams1", "NpcParams2", "NpcParams3"]):
        params[name] = add_vector(material, name, -1600, 300 + 100 * index)
    colors = [add_vector(material, "NpcFaction0Color", -1600, 750, unreal.LinearColor(0.25, 0.6, 1.0, 1.0)),
              add_vector(material, "NpcFaction1Color", -1600, 850, unreal.LinearColor(1.0, 0.4, 0.2, 1.0))]

    # Vertex stage: world position offset plus the interpolated (faction, brightness)
    vertex = add_custom(material, -1100, -300, "SOLNpcPointVertex", VERTEX_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["ShipBase", "ShipUv", "Corner", "RailTex", "GroupTex", "CpuTex", "P0", "P1", "P2", "P3"])
    extra = unreal.CustomOutput()
    extra.set_editor_property("output_name", "Shade")
    extra.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    vertex.set_editor_property("additional_outputs", [extra])
    MEL.connect_material_expressions(ship_base, "", vertex, "ShipBase")
    MEL.connect_material_expressions(uv1, "", vertex, "ShipUv")
    MEL.connect_material_expressions(uv0, "", vertex, "Corner")
    MEL.connect_material_expressions(textures["NpcRailTex"], "", vertex, "RailTex")
    MEL.connect_material_expressions(textures["NpcGroupTex"], "", vertex, "GroupTex")
    MEL.connect_material_expressions(textures["NpcCpuTex"], "", vertex, "CpuTex")
    for index in range(4):
        MEL.connect_material_expressions(params["NpcParams%d" % index], "RGBA", vertex, "P%d" % index)
    MEL.connect_material_property(vertex, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    interpolator = add(material, unreal.MaterialExpressionVertexInterpolator, -800, -200)
    MEL.connect_material_expressions(vertex, "Shade", interpolator, "VS")

    # Pixel stage: soft dot
    pixel = add_custom(material, -500, 100, "SOLNpcPointPixel", PIXEL_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Corner", "Shade", "C0", "C1", "P3"])
    MEL.connect_material_expressions(uv0, "", pixel, "Corner")
    MEL.connect_material_expressions(interpolator, "PS", pixel, "Shade")
    MEL.connect_material_expressions(colors[0], "", pixel, "C0")
    MEL.connect_material_expressions(colors[1], "", pixel, "C1")
    MEL.connect_material_expressions(params["NpcParams3"], "RGBA", pixel, "P3")
    MEL.connect_material_property(pixel, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Builds the placeholder hull material graph
def build_hull(material):
    material.set_editor_property("used_with_instanced_static_meshes", True)
    tint = add_vector(material, "Tint", -600, 0, unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    roughness = add(material, unreal.MaterialExpressionConstant, -600, 150)
    roughness.set_editor_property("r", 0.45)
    metallic = add(material, unreal.MaterialExpressionConstant, -600, 230)
    metallic.set_editor_property("r", 0.6)
    glow = add(material, unreal.MaterialExpressionMultiply, -300, 300)
    glow.set_editor_property("const_b", 0.6)
    MEL.connect_material_expressions(tint, "", glow, "A")
    MEL.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Builds, compiles and saves one material
def finish(material, name):
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("create_npc_assets: built %s/%s" % (NPC_DIR, name))


default_texture = ensure_default_texture()
points = material_for_build(POINTS_NAME)
if points is not None:
    build_points(points, default_texture)
    finish(points, POINTS_NAME)
hull = material_for_build(HULL_NAME)
if hull is not None:
    build_hull(hull)
    finish(hull, HULL_NAME)
