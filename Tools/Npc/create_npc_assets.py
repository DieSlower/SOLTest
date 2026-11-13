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
#       Issue #38 (SDD 38 D9, D10): in rail mode a lane ship's (kind 1) brightness is multiplied by the lane crossfade
#       1 - LaneLineFade(d, R) while NpcParams4.x = 1 (lanes shown; R = the group texture's row 0 .w, skipped for a
#       lane whose row 1 .w is -1, i.e. not drawn), and a ship at alpha 0 returns the hidden offset; with
#       NpcParams4.x = 0 every point is drawn exactly as before. The fade is SOLNL_LineFade from SOLNpcLane.ush (the
#       vertex Custom node includes it), the same function the lane line uses.
#   /Game/SOL/Npc/M_SOLNpcHull
#       Default-lit placeholder hull (vector parameter Tint), usable on ISMs.
#   /Game/SOL/Npc/M_SOLNpcLanes
#       Issue #38 (SDD 38 section 4.8): the NPC lane lines. Unlit, additive, two-sided, depth test off, drawn after
#       motion blur like the orbit lines (no TSR smear of the analytic lines). Drawn on ASOLNpcVisuals's lane ribbon
#       mesh (vertices at zero; UV0 = (sample u, side -1 / +1), UV1 = (lane group index, line 0 outbound / 1 inbound)):
#       the vertex stage places each ribbon vertex from the group texture (NpcGroupTex), the pixel stage computes the
#       glow profile, the flowing dashes, the close-up fade and the occlusion (Shaders/Private/SOLNpcLane.ush, the
#       mirror of Source/SOLTest/Npc/SOLNpcLaneMath.cpp). It reads the orbit lines' parameter collection
#       (OrbitLineDash, OrbitLineStyle2, OrbitLineOccluder0..7; SDD 38 D24) and the vector parameters of
#       SOL::NpcLaneMaterialParams (set by ASOLNpcVisuals; their defaults here are read from SOLConstants.h).
#       Issue #49 (SDD 49 Q5): the pixel stage multiplies the line's close-up fade by 1 - F, F = the lane's tunnel fade
#       from the texture parameter NpcTunnelTex (row 1 .w, the column of the lane); the point material's lane branch
#       does the same, so the line and its ships' points still sum to 1.
#   /Game/SOL/Npc/M_SOLNpcTunnel
#       Issue #49 (SDD 49 section 4.7): the lane guidance tunnels. Unlit, additive, two-sided, depth test off, after
#       motion blur, used with instanced static meshes. Drawn on USOLNpcTunnelComponent's slot mesh (vertices at zero,
#       one ISM instance per lane in range, custom data 0 = the lane): the vertex stage places the ring ribbons, dot
#       quads, chevron quads and end rings from NpcTunnelTex, the pixel stage draws the glow profile or the chevron
#       shape and the shared occlusion (Shaders/Private/SOLNpcTunnel.ush, the mirror of SOLNpcTunnelMath.cpp). It reads
#       the orbit lines' collection (OrbitLineStyle2, OrbitLineOccluder0..7) and SOL::NpcTunnelMaterialParams.
# Parameter names must match SOLNpcVisuals.cpp, SOL::NpcLaneMaterialParams and SOL::NpcTunnelMaterialParams; asset
# paths SOL::Paths::NPC_POINTS_MATERIAL / NPC_HULL_MATERIAL / NPC_LANES_MATERIAL / NPC_TUNNEL_MATERIAL. The lane and
# tunnel materials and the point material's vertex node include SOLNpcLane.ush / SOLNpcTunnel.ush, so a shader change
# there needs no rebuild; a graph or parameter change does.
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/Npc/create_npc_assets.py"
#       -unattended [-SOLRebuild]
import os
import re

import unreal

NPC_DIR = "/Game/SOL/Npc"
POINTS_NAME = "M_SOLNpcPoints"
HULL_NAME = "M_SOLNpcHull"
LANES_NAME = "M_SOLNpcLanes"
TUNNEL_NAME = "M_SOLNpcTunnel"
TUNNEL_INCLUDE = "/Project/SOLTest/Private/SOLNpcTunnel.ush"
ORBIT_LINE_MPC = "/Game/SOL/OrbitLines/MPC_SOLOrbitLine"
LANE_INCLUDE = "/Project/SOLTest/Private/SOLNpcLane.ush"   # SOL::SHADER_VIRTUAL_DIRECTORY + file
CONSTANTS_FILE = os.path.join(unreal.Paths.project_dir(), "Source", "SOLTest", "SOLConstants.h")
DEFAULT_TEXTURE_NAME = "T_SOLNpcFloatDefault"
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


# Returns a numeric SOL:: constant from SOLConstants.h, so the lane material defaults cannot drift from the C++
def constant(name):
    text = open(CONSTANTS_FILE, encoding="utf-8").read()
    match = re.search(r"\b" + name + r"\s*=\s*([-0-9.eE+]+)", text)
    if match is None:
        raise RuntimeError("create_npc_assets: %s not found in %s" % (name, CONSTANTS_FILE))
    return float(match.group(1))

VERTEX_HLSL = r"""// NPC point vertex (SDD 11 3.4): the block mesh sits at the origin with identity instances,
// so the offset is the target render position minus the local position
const float3 LocalPos = Parameters.PreSkinnedPosition;
Shade = float2(0.0, 0.0);
float laneAlpha = 1.0;
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
    const float4 groupA = GroupTex.Load(int3(group, 0, 0));
    const float3 pA = groupA.xyz;
    const float t = P0.x;
    if (kind == 1)
    {
        // Lane: triangle wave along A-B plus the lateral offset in the lane basis (SOLNpcScale::LaneBasis)
        const float4 groupB = GroupTex.Load(int3(group, 1, 0));
        const float3 pB = groupB.xyz;
        const float x = a.y + a.z * t;
        const float m = x - 2.0 * floor(x * 0.5);
        const float s = m <= 1.0 ? m : 2.0 - m;
        const float3 axis = pB - pA;
        const float3 ax = normalize(axis);
        const float3 refAxis = abs(ax.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        const float3 u = normalize(cross(refAxis, ax));
        const float3 v = cross(ax, u);
        relM = pA + axis * s + u * a.w + v * b.x;

        // Lane crossfade (SDD 38 D9): while the lanes are shown (P4.x) the point fades in where the lane line fades
        // out, 1 - LaneLineFade(d, R) (SOLNpcLane::LaneShipPointAlpha, the lane shader's own SOLNL_LineFade); R = row 0
        // .w (a bad radius keeps the line, so the point hides, as in C++), a lane not drawn has row 1 .w -1. Since
        // issue #49 (SDD 49 Q5) the line's fade carries the lane's tunnel fade F (NpcTunnelTex row 1 .w):
        // 1 - LaneLineFade x (1 - F), so the ships show as points again inside a tunnel
        if (P4.x > 0.5 && groupB.w >= 0.0)
        {
            const float tunnelFade = saturate(TunnelTex.Load(int3(group, 1, 0)).w);
            laneAlpha = 1.0 - SOLNL_LineFade(length(relM), groupA.w, P4.y, P4.z) * (1.0 - tunnelFade);
            if (laneAlpha <= 0.0)
            {
                return hiddenOffset;
            }
        }
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
Shade = float2(faction, clamp(pow(P2.z / dM, P3.x), P2.w, 1.0) * laneAlpha);
return target + (right * corner.x + up * corner.y) * radiusCm - LocalPos;"""

LANE_VERTEX_HLSL = r"""// NPC lane ribbon vertex (SDD 38 4.8): the mesh vertices sit at zero and the component at
// the origin, so the world position offset is the target render position itself
const int lane = (int)(UV1.x + 0.5);
const float4 groupA = GroupTex.Load(int3(lane, 0, 0));
const float4 groupB = GroupTex.Load(int3(lane, 1, 0));
float signedPx;
const float3 target = SOLNL_Vertex(UV0.xy, UV1.y, groupA, groupB, WSDemote(GetWorldCameraOrigin(Parameters)),
    Frame, Style0, Style2, Glow0, ResolvedView.ViewToTranslatedWorld[0].xyz,
    ResolvedView.ViewToTranslatedWorld[1].xyz, ResolvedView.ViewForward, ResolvedView.ViewToClip[0][0],
    ResolvedView.ViewSizeAndInvSize.xy, signedPx);
Interp = float2(signedPx, 0.0);
return target;"""

LANE_PIXEL_HLSL = r"""// NPC lane colour (SDD 38 4.6-4.8)
float4 occluders[SOLOL_MAX_OCCLUDERS] = { O0, O1, O2, O3, O4, O5, O6, O7 };
const int lane = (int)(UV1.x + 0.5);
const float4 groupA = GroupTex.Load(int3(lane, 0, 0));
const float4 groupB = GroupTex.Load(int3(lane, 1, 0));
return SOLNL_Pixel(Interp.x, UV1.y, groupA, groupB, -Parameters.CameraVector,
    WSDemote(GetWorldCameraOrigin(Parameters)), Frame, Params0, Style0, Style1, Style2, Glow0, Glow1, GlowScale,
    OutColor.rgb, InColor.rgb, Dash, OrbitStyle2, ResolvedView.ViewSizeAndInvSize.y, PixelDepth, SceneDepth,
    occluders, TunnelTex.Load(int3(lane, 1, 0)).w);"""

TUNNEL_VERTEX_HLSL = r"""// NPC lane tunnel vertex (SDD 49 4.7): the slot mesh sits at zero with identity instances,
// so the world position offset is the target render position itself
const int lane = (int)(Lane + 0.5);
const float4 t0 = TunnelTex.Load(int3(lane, 0, 0));
const float4 t1 = TunnelTex.Load(int3(lane, 1, 0));
const float4 t2 = TunnelTex.Load(int3(lane, 2, 0));
const float4 t3 = TunnelTex.Load(int3(lane, 3, 0));
float4 interp0;
float4 interp1;
const float3 target = SOLNT_Vertex(float3(UV0.xy, 0.0), UV1.xy, UV2.xy, t0, t1, t2, t3,
    WSDemote(GetWorldCameraOrigin(Parameters)), Frame, Style0, Style1, Shape, Style2, Style3, Layout0, Layout1,
    Layout2, Layout3, Glow0, ResolvedView.ViewToTranslatedWorld[0].xyz, ResolvedView.ViewToTranslatedWorld[1].xyz,
    ResolvedView.ViewForward, ResolvedView.ViewToClip[0][0], ResolvedView.ViewSizeAndInvSize.xy, interp0, interp1);
Interp0 = interp0;
Interp1 = interp1;
return target;"""

TUNNEL_PIXEL_HLSL = r"""// NPC lane tunnel colour (SDD 49 4.6-4.7)
float4 occluders[SOLOL_MAX_OCCLUDERS] = { O0, O1, O2, O3, O4, O5, O6, O7 };
return SOLNT_Pixel(Interp0, Interp1, WSDemote(GetWorldCameraOrigin(Parameters)), Frame, Style0, Style1, Style2,
    Glow0, Glow1, GlowScale, RingColor.rgb, DotOutColor.rgb, DotInColor.rgb, ChevronOutColor.rgb, ChevronInColor.rgb,
    OrbitStyle2, ResolvedView.ViewSizeAndInvSize.y, PixelDepth, SceneDepth, occluders);"""

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


# Adds a Custom node that includes the lane shader library
def add_lane_custom(material, x, y, description, code, output_type, input_names):
    node = add_custom(material, x, y, description, code, output_type, input_names)
    node.set_editor_property("include_file_paths", [LANE_INCLUDE])
    return node


# Adds a float texture object parameter (the linear placeholder as its default)
def add_texture_object(material, name, default_texture, x, y):
    node = add(material, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", default_texture)
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
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
    for index, name in enumerate(["NpcRailTex", "NpcGroupTex", "NpcCpuTex", "NpcTunnelTex"]):
        node = add(material, unreal.MaterialExpressionTextureObjectParameter, -1600, -100 + 120 * index)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("texture", default_texture)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        textures[name] = node
    params = {}
    for index, name in enumerate(["NpcParams0", "NpcParams1", "NpcParams2", "NpcParams3", "NpcParams4"]):
        params[name] = add_vector(material, name, -1600, 300 + 100 * index)
    colors = [add_vector(material, "NpcFaction0Color", -1600, 850, unreal.LinearColor(0.25, 0.6, 1.0, 1.0)),
              add_vector(material, "NpcFaction1Color", -1600, 950, unreal.LinearColor(1.0, 0.4, 0.2, 1.0))]

    # Vertex stage: world position offset plus the interpolated (faction, brightness)
    vertex = add_lane_custom(material, -1100, -300, "SOLNpcPointVertex", VERTEX_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["ShipBase", "ShipUv", "Corner", "RailTex", "GroupTex", "CpuTex", "TunnelTex", "P0", "P1", "P2", "P3",
         "P4"])
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
    MEL.connect_material_expressions(textures["NpcTunnelTex"], "", vertex, "TunnelTex")
    for index in range(5):
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


# Returns the lane material's vector parameters and their defaults (SOL::NpcLaneMaterialParams; ASOLNpcVisuals
# writes every one at BeginPlay or per frame, the colours included)
def lane_vector_defaults():
    return [
        ("NpcLaneParams0", unreal.LinearColor(0.0, 0.0, constant("NPC_LANE_FADE_NEAR_RADII"),
            constant("NPC_LANE_FADE_FAR_RADII"))),
        ("NpcLaneFrame", unreal.LinearColor(0.0, 0.0, 0.0, constant("DEFAULT_MAX_RENDER_DISTANCE_CM"))),
        ("NpcLaneStyle0", unreal.LinearColor(constant("NPC_LANE_WIDTH_MIN_PX"), constant("NPC_LANE_WIDTH_MAX_PX"),
            constant("NPC_LANE_GAP_PX"), constant("NPC_LANE_GLOW_PAD_PX"))),
        ("NpcLaneStyle1", unreal.LinearColor(constant("NPC_LANE_BRIGHTNESS_MIN"), constant("NPC_LANE_BRIGHTNESS_MAX"),
            constant("NPC_LANE_DASH_BASE_COUNT"), constant("NPC_LANE_DASH_DUTY"))),
        ("NpcLaneStyle2", unreal.LinearColor(constant("NPC_LANE_REFERENCE_HEIGHT_PX"), constant("NPC_LANE_EMISSIVE"),
            0.0, 0.0)),
        ("NpcLaneGlow0", unreal.LinearColor(constant("HUD_GLOW_CORE_HALF_PX"), constant("HUD_GLOW_CORE_FALLOFF"),
            constant("HUD_GLOW_CORE_FALLOFF_MAX"), constant("HUD_GLOW_HALO_WEIGHT"))),
        ("NpcLaneGlow1", unreal.LinearColor(constant("HUD_GLOW_HALO_SCALE"), constant("HUD_GLOW_HALO_POWER"),
            constant("HUD_GLOW_SCALE_MAX"), 0.0)),
        ("NpcLaneOutboundColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)),
        ("NpcLaneInboundColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)),
    ]


# Builds the lane ribbon material graph
def build_lanes(material, default_texture):
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", True)
    material.set_editor_property("use_translucency_vertex_fog", False)
    # Like the orbit lines (issue #25): after motion blur, out of TSR's history, so the analytic lines stay sharp
    material.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
    material.set_editor_property("allow_negative_emissive_color", False)

    # Inputs: UV0 (u, side), UV1 (lane, line), the group texture, the lane parameters and the orbit-line collection
    uv0 = add(material, unreal.MaterialExpressionTextureCoordinate, -1600, -400)
    uv1 = add(material, unreal.MaterialExpressionTextureCoordinate, -1600, -300)
    uv1.set_editor_property("coordinate_index", 1)
    group_tex = add(material, unreal.MaterialExpressionTextureObjectParameter, -1600, -200)
    group_tex.set_editor_property("parameter_name", "NpcGroupTex")
    group_tex.set_editor_property("texture", default_texture)
    group_tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    tunnel_tex = add_texture_object(material, "NpcTunnelTex", default_texture, -1900, -200)
    vectors = {}
    for index, (name, default) in enumerate(lane_vector_defaults()):
        vectors[name] = add_vector(material, name, -1600, -60 + 100 * index, default)
    glow_scale = add(material, unreal.MaterialExpressionScalarParameter, -1600, 900)
    glow_scale.set_editor_property("parameter_name", "GlowScale")
    glow_scale.set_editor_property("default_value", constant("HUD_GLOW_DEFAULT_LEVEL"))
    mpc = EAL.load_asset(ORBIT_LINE_MPC)
    if mpc is None:
        raise RuntimeError("create_npc_assets: %s missing (run Tools/OrbitLines/create_orbit_line_assets.py first)"
            % ORBIT_LINE_MPC)
    collection = {}
    occluder_names = ["OrbitLineOccluder%d" % i for i in range(int(constant("ORBIT_LINE_MAX_OCCLUDERS")))]
    for index, name in enumerate(["OrbitLineDash", "OrbitLineStyle2"] + occluder_names):
        node = add(material, unreal.MaterialExpressionCollectionParameter, -1600, 1000 + 90 * index)
        node.set_editor_property("collection", mpc)
        node.set_editor_property("parameter_name", name)
        collection[name] = node

    # Vertex stage: world position offset plus the interpolated signed pixel distance from the line's centre
    vertex = add_lane_custom(material, -1100, -300, "SOLNpcLaneVertex", LANE_VERTEX_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["UV0", "UV1", "GroupTex", "Frame", "Style0", "Style2", "Glow0"])
    extra = unreal.CustomOutput()
    extra.set_editor_property("output_name", "Interp")
    extra.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    vertex.set_editor_property("additional_outputs", [extra])
    MEL.connect_material_expressions(uv0, "", vertex, "UV0")
    MEL.connect_material_expressions(uv1, "", vertex, "UV1")
    MEL.connect_material_expressions(group_tex, "", vertex, "GroupTex")
    MEL.connect_material_expressions(vectors["NpcLaneFrame"], "RGBA", vertex, "Frame")
    MEL.connect_material_expressions(vectors["NpcLaneStyle0"], "RGBA", vertex, "Style0")
    MEL.connect_material_expressions(vectors["NpcLaneStyle2"], "RGBA", vertex, "Style2")
    MEL.connect_material_expressions(vectors["NpcLaneGlow0"], "RGBA", vertex, "Glow0")
    MEL.connect_material_property(vertex, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    interpolator = add(material, unreal.MaterialExpressionVertexInterpolator, -800, -200)
    MEL.connect_material_expressions(vertex, "Interp", interpolator, "VS")

    # Pixel stage: the additive lane colour (profile, dashes, fade, occlusion)
    pixel_depth = add(material, unreal.MaterialExpressionPixelDepth, -800, 0)
    scene_depth = add(material, unreal.MaterialExpressionSceneDepth, -800, 80)
    pixel_occluders = ["O%d" % i for i in range(len(occluder_names))]
    pixel = add_lane_custom(material, -500, 100, "SOLNpcLanePixel", LANE_PIXEL_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["Interp", "UV1", "GroupTex", "TunnelTex", "Frame", "Params0", "Style0", "Style1", "Style2", "Glow0", "Glow1",
         "GlowScale", "OutColor", "InColor", "Dash", "OrbitStyle2", "PixelDepth", "SceneDepth"] + pixel_occluders)
    MEL.connect_material_expressions(interpolator, "PS", pixel, "Interp")
    MEL.connect_material_expressions(uv1, "", pixel, "UV1")
    MEL.connect_material_expressions(group_tex, "", pixel, "GroupTex")
    MEL.connect_material_expressions(tunnel_tex, "", pixel, "TunnelTex")
    for pin, name in [("Frame", "NpcLaneFrame"), ("Params0", "NpcLaneParams0"), ("Style0", "NpcLaneStyle0"),
                      ("Style1", "NpcLaneStyle1"), ("Style2", "NpcLaneStyle2"), ("Glow0", "NpcLaneGlow0"),
                      ("Glow1", "NpcLaneGlow1"), ("OutColor", "NpcLaneOutboundColor"),
                      ("InColor", "NpcLaneInboundColor")]:
        MEL.connect_material_expressions(vectors[name], "RGBA", pixel, pin)
    MEL.connect_material_expressions(glow_scale, "", pixel, "GlowScale")
    MEL.connect_material_expressions(collection["OrbitLineDash"], "", pixel, "Dash")
    MEL.connect_material_expressions(collection["OrbitLineStyle2"], "", pixel, "OrbitStyle2")
    MEL.connect_material_expressions(pixel_depth, "", pixel, "PixelDepth")
    MEL.connect_material_expressions(scene_depth, "", pixel, "SceneDepth")
    for pin, name in zip(pixel_occluders, occluder_names):
        MEL.connect_material_expressions(collection[name], "", pixel, pin)
    MEL.connect_material_property(pixel, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Returns the tunnel material's vector parameters and their defaults (SOL::NpcTunnelMaterialParams; the component
# writes every one at BeginPlay or per frame, the colours included)
def tunnel_vector_defaults():
    return [
        ("NpcTunnelFrame", unreal.LinearColor(0.0, 0.0, 0.0, constant("DEFAULT_MAX_RENDER_DISTANCE_CM"))),
        ("NpcTunnelStyle0", unreal.LinearColor(constant("NPC_TUNNEL_RING_WIDTH_PX"),
            constant("NPC_TUNNEL_END_RING_WIDTH_PX"), constant("NPC_TUNNEL_DOT_SIZE_PX"),
            constant("NPC_LANE_REFERENCE_HEIGHT_PX"))),
        ("NpcTunnelStyle1", unreal.LinearColor(constant("NPC_TUNNEL_EMISSIVE"), constant("NPC_TUNNEL_CHEVRON_ALPHA"),
            constant("NPC_LANE_GLOW_PAD_PX"), constant("NPC_TUNNEL_DOT_RADIUS_SCALE"))),
        ("NpcTunnelShape", unreal.LinearColor(constant("NPC_TUNNEL_CHEVRON_RADIUS_SCALE"),
            constant("NPC_TUNNEL_CHEVRON_HALF_WIDTH_RADII"), constant("NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII"),
            constant("NPC_TUNNEL_END_RING_MIN_ALPHA"))),
        ("NpcTunnelStyle2", unreal.LinearColor(constant("NPC_TUNNEL_CHEVRON_NOTCH_FRACTION"),
            constant("NPC_TUNNEL_DETAIL_NEAR_FADE_START_RADII"), constant("NPC_TUNNEL_DETAIL_NEAR_FADE_FULL_RADII"),
            constant("NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII"))),
        ("NpcTunnelStyle3", unreal.LinearColor(constant("NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS"),
            constant("NPC_TUNNEL_CHEVRON_FADE_START_RADII"), constant("NPC_TUNNEL_CHEVRON_FADE_END_RADII"), 0.0)),
        ("NpcTunnelLayout0", unreal.LinearColor(constant("NPC_TUNNEL_RING_BASE_SPACING_RADII"),
            constant("NPC_TUNNEL_RING_SPACING_PER_DISTANCE"), constant("NPC_TUNNEL_RING_SLOTS_PER_SIDE"),
            constant("NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE"))),
        ("NpcTunnelLayout1", unreal.LinearColor(constant("NPC_TUNNEL_END_FADE_SPACINGS"),
            constant("NPC_TUNNEL_DEPTH_FADE_START_RADII"), constant("NPC_TUNNEL_DEPTH_FADE_END_RADII"),
            constant("NPC_TUNNEL_DEPTH_FADE_FLOOR"))),
        ("NpcTunnelLayout2", unreal.LinearColor(constant("NPC_TUNNEL_NEAR_FADE_START_RADII"),
            constant("NPC_TUNNEL_NEAR_FADE_FULL_RADII"), constant("NPC_TUNNEL_NEAR_FADE_RADIAL_FULL_RADII"),
            constant("NPC_TUNNEL_NEAR_FADE_RADIAL_NONE_RADII"))),
        ("NpcTunnelLayout3", unreal.LinearColor(constant("NPC_TUNNEL_DETAIL_FADE_START_RADII"),
            constant("NPC_TUNNEL_DETAIL_FADE_END_RADII"), constant("NPC_TUNNEL_DOTS_PER_GAP"),
            constant("NPC_TUNNEL_DOT_ROWS"))),
        ("NpcTunnelGlow0", unreal.LinearColor(constant("HUD_GLOW_CORE_HALF_PX"), constant("HUD_GLOW_CORE_FALLOFF"),
            constant("HUD_GLOW_CORE_FALLOFF_MAX"), constant("HUD_GLOW_HALO_WEIGHT"))),
        ("NpcTunnelGlow1", unreal.LinearColor(constant("HUD_GLOW_HALO_SCALE"), constant("HUD_GLOW_HALO_POWER"),
            constant("HUD_GLOW_SCALE_MAX"), 0.0)),
        ("NpcTunnelRingColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)),
        ("NpcTunnelDotOutboundColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)),
        ("NpcTunnelDotInboundColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)),
        ("NpcTunnelChevronOutboundColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)),
        ("NpcTunnelChevronInboundColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)),
    ]


# Builds the lane tunnel material graph
def build_tunnel(material, default_texture):
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", True)
    material.set_editor_property("use_translucency_vertex_fog", False)
    material.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
    material.set_editor_property("allow_negative_emissive_color", False)
    material.set_editor_property("used_with_instanced_static_meshes", True)

    # Inputs: the lane (instance custom data), UV0-UV2, the tunnel texture, the parameters and the orbit-line collection
    lane = add(material, unreal.MaterialExpressionPerInstanceCustomData, -1600, -600)
    lane.set_editor_property("data_index", 0)
    uvs = []
    for index in range(3):
        node = add(material, unreal.MaterialExpressionTextureCoordinate, -1600, -500 + 100 * index)
        node.set_editor_property("coordinate_index", index)
        uvs.append(node)
    tunnel_tex = add_texture_object(material, "NpcTunnelTex", default_texture, -1600, -200)
    vectors = {}
    for index, (name, default) in enumerate(tunnel_vector_defaults()):
        vectors[name] = add_vector(material, name, -1600, -60 + 100 * index, default)
    glow_scale = add(material, unreal.MaterialExpressionScalarParameter, -1600, 1500)
    glow_scale.set_editor_property("parameter_name", "GlowScale")
    glow_scale.set_editor_property("default_value", constant("HUD_GLOW_DEFAULT_LEVEL"))
    mpc = EAL.load_asset(ORBIT_LINE_MPC)
    if mpc is None:
        raise RuntimeError("create_npc_assets: %s missing (run Tools/OrbitLines/create_orbit_line_assets.py first)"
            % ORBIT_LINE_MPC)
    collection = {}
    occluder_names = ["OrbitLineOccluder%d" % i for i in range(int(constant("ORBIT_LINE_MAX_OCCLUDERS")))]
    for index, name in enumerate(["OrbitLineStyle2"] + occluder_names):
        node = add(material, unreal.MaterialExpressionCollectionParameter, -1600, 1600 + 90 * index)
        node.set_editor_property("collection", mpc)
        node.set_editor_property("parameter_name", name)
        collection[name] = node

    # Vertex stage: world position offset plus two interpolated vectors for the pixel stage
    vertex_pins = [("Frame", "NpcTunnelFrame"), ("Style0", "NpcTunnelStyle0"), ("Style1", "NpcTunnelStyle1"),
                   ("Shape", "NpcTunnelShape"), ("Style2", "NpcTunnelStyle2"), ("Style3", "NpcTunnelStyle3"),
                   ("Layout0", "NpcTunnelLayout0"), ("Layout1", "NpcTunnelLayout1"), ("Layout2", "NpcTunnelLayout2"),
                   ("Layout3", "NpcTunnelLayout3"), ("Glow0", "NpcTunnelGlow0")]
    vertex = add_custom(material, -1100, -300, "SOLNpcTunnelVertex", TUNNEL_VERTEX_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["Lane", "UV0", "UV1", "UV2", "TunnelTex"] + [pin for pin, _ in vertex_pins])
    vertex.set_editor_property("include_file_paths", [TUNNEL_INCLUDE])
    outputs = []
    for name in ["Interp0", "Interp1"]:
        extra = unreal.CustomOutput()
        extra.set_editor_property("output_name", name)
        extra.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
        outputs.append(extra)
    vertex.set_editor_property("additional_outputs", outputs)
    MEL.connect_material_expressions(lane, "", vertex, "Lane")
    for index in range(3):
        MEL.connect_material_expressions(uvs[index], "", vertex, "UV%d" % index)
    MEL.connect_material_expressions(tunnel_tex, "", vertex, "TunnelTex")
    for pin, name in vertex_pins:
        MEL.connect_material_expressions(vectors[name], "RGBA", vertex, pin)
    MEL.connect_material_property(vertex, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    interpolators = []
    for index, name in enumerate(["Interp0", "Interp1"]):
        node = add(material, unreal.MaterialExpressionVertexInterpolator, -800, -200 + 150 * index)
        MEL.connect_material_expressions(vertex, name, node, "VS")
        interpolators.append(node)

    # Pixel stage: the additive tunnel colour (profile or chevron, occlusion)
    pixel_depth = add(material, unreal.MaterialExpressionPixelDepth, -800, 200)
    scene_depth = add(material, unreal.MaterialExpressionSceneDepth, -800, 280)
    pixel_occluders = ["O%d" % i for i in range(len(occluder_names))]
    pixel_pins = [("Frame", "NpcTunnelFrame"), ("Style0", "NpcTunnelStyle0"), ("Style1", "NpcTunnelStyle1"),
                  ("Style2", "NpcTunnelStyle2"), ("Glow0", "NpcTunnelGlow0"), ("Glow1", "NpcTunnelGlow1"),
                  ("RingColor", "NpcTunnelRingColor"),
                  ("DotOutColor", "NpcTunnelDotOutboundColor"), ("DotInColor", "NpcTunnelDotInboundColor"),
                  ("ChevronOutColor", "NpcTunnelChevronOutboundColor"),
                  ("ChevronInColor", "NpcTunnelChevronInboundColor")]
    pixel = add_custom(material, -500, 100, "SOLNpcTunnelPixel", TUNNEL_PIXEL_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["Interp0", "Interp1"] + [pin for pin, _ in pixel_pins]
        + ["GlowScale", "OrbitStyle2", "PixelDepth", "SceneDepth"] + pixel_occluders)
    pixel.set_editor_property("include_file_paths", [TUNNEL_INCLUDE])
    MEL.connect_material_expressions(interpolators[0], "PS", pixel, "Interp0")
    MEL.connect_material_expressions(interpolators[1], "PS", pixel, "Interp1")
    for pin, name in pixel_pins:
        MEL.connect_material_expressions(vectors[name], "RGBA", pixel, pin)
    MEL.connect_material_expressions(glow_scale, "", pixel, "GlowScale")
    MEL.connect_material_expressions(collection["OrbitLineStyle2"], "", pixel, "OrbitStyle2")
    MEL.connect_material_expressions(pixel_depth, "", pixel, "PixelDepth")
    MEL.connect_material_expressions(scene_depth, "", pixel, "SceneDepth")
    for pin, name in zip(pixel_occluders, occluder_names):
        MEL.connect_material_expressions(collection[name], "", pixel, pin)
    MEL.connect_material_property(pixel, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


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
lanes = material_for_build(LANES_NAME)
if lanes is not None:
    build_lanes(lanes, default_texture)
    finish(lanes, LANES_NAME)
tunnel = material_for_build(TUNNEL_NAME)
if tunnel is not None:
    build_tunnel(tunnel, default_texture)
    finish(tunnel, TUNNEL_NAME)
