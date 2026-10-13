# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates the gas giant band-shell materials (issue #14, SDD 14, plan step 14c), headless and idempotent:
#   /Game/SOL/GasGiants/M_SOLGasGiantDeck  opaque, unlit, self-shaded cloud deck drawn on the engine sphere in place of
#                                          the body's placeholder mesh. Latitude bands warped by fBm turbulence, sampled
#                                          in band-advected longitude (lon - 2 PI * phase, the 16 phases from
#                                          SOLGasGiant::ComputeBandPhases; each sample's phase advects the noise rigidly
#                                          and neighbouring samples' fields are cross-faded mid-gap, wrap-safe,
#                                          mirroring SOLGasGiant::BandBlendAt), up to 8 storm vortices in the body frame
#                                          (centre longitudes from SOLGasGiant::StormLongitudeRad), polar tint, Lambert
#                                          toward SunDirection with a limb darkening term and an ambient floor
#                                          (AmbientLight; same radiance scale as M_SOLBody)
#   /Game/SOL/GasGiants/M_SOLGasGiantHaze  translucent, unlit upper haze shell slightly above the deck: sparse high
#                                          clouds advected by their own (faster) phases, so they visibly slide over the
#                                          bands
#   /Game/SOL/GasGiants/M_SOLGasGiantCloud volume-domain material for the near volumetric cloud layer (14d): band
#                                          coverage and 3 octaves of value noise (CloudNoise.x = 300 km features, so
#                                          ~300/150/75 km) around the planet centre, in the body frame, advected by the
#                                          deck's phases; faded by NearBlend
# Phases arrive as floats in [0, 1) computed in double on the CPU (a float cannot hold rate * t at t ~ 1e9 s).
# Parameter names must match SOL::GasGiantMaterialParams in Source/SOLTest/SOLConstants.h, and the asset paths
# SOL::Paths::GAS_GIANT_DECK_MATERIAL / GAS_GIANT_HAZE_MATERIAL / GAS_GIANT_CLOUD_MATERIAL.
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
#       -script="<abs path>/Tools/GasGiants/create_gas_giant_materials.py" -unattended [-SOLRebuild]
# or, in an open editor, exec() this file with SOL_REBUILD = True defined in the calling scope's globals.
import unreal

MATERIAL_DIR = "/Game/SOL/GasGiants"
DECK_NAME = "M_SOLGasGiantDeck"
HAZE_NAME = "M_SOLGasGiantHaze"
CLOUD_NAME = "M_SOLGasGiantCloud"
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

# Shared HLSL helpers: hashed value noise and fBm, declared as struct methods (a Custom node body cannot declare free
# functions), plus the band phase lookup and the advected sphere point
COMMON_HLSL = r"""
struct FSOLGasGiantNoise
{
    // Integer hash of a lattice point (no large float products, so no precision loss far from the origin)
    float Hash(float3 p)
    {
        uint3 q = (uint3)(int3)p * uint3(1597334673u, 3812015801u, 2798796415u);
        uint h = (q.x ^ q.y ^ q.z) * 1597334673u;
        h ^= h >> 16;
        return float(h >> 8) * (1.0 / 16777216.0);
    }
    float Noise(float3 x)
    {
        float3 i = floor(x);
        float3 f = frac(x);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(Hash(i), Hash(i + float3(1, 0, 0)), f.x),
                         lerp(Hash(i + float3(0, 1, 0)), Hash(i + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(Hash(i + float3(0, 0, 1)), Hash(i + float3(1, 0, 1)), f.x),
                         lerp(Hash(i + float3(0, 1, 1)), Hash(i + float3(1, 1, 1)), f.x), f.y), f.z);
    }
    float Fbm(float3 p, int octaves)
    {
        float amplitude = 0.5;
        float sum = 0.0;
        for (int k = 0; k < octaves; ++k)
        {
            sum += amplitude * Noise(p);
            p = p * 2.03 + float3(1.7, 9.2, 3.1);
            amplitude *= 0.5;
        }
        return sum;
    }
    // One fBm pass returning (all octaves, octaves from 'split' on rescaled to ~[0, 1]): the deck's turbulence and
    // its fine detail share the noise evaluations instead of running two fBms
    float2 Fbm2(float3 p, int octaves, int split)
    {
        float amplitude = 0.5;
        float sum = 0.0;
        float high = 0.0;
        float highNorm = 0.0;
        for (int k = 0; k < octaves; ++k)
        {
            float v = amplitude * Noise(p);
            sum += v;
            if (k >= split)
            {
                high += v;
                highNorm += amplitude;
            }
            p = p * 2.03 + float3(1.7, 9.2, 3.1);
            amplitude *= 0.5;
        }
        return float2(sum, high / max(highNorm, 1e-4));
    }
    // Band phase sample i (0..15) of the four phase vectors, selected by components (no dynamic array indexing)
    float Phase(float4 p0, float4 p1, float4 p2, float4 p3, int i)
    {
        float4 v = i < 4 ? p0 : (i < 8 ? p1 : (i < 12 ? p2 : p3));
        int c = i & 3;
        return dot(v, float4(c == 0, c == 1, c == 2, c == 3));
    }
};
FSOLGasGiantNoise g;
const float PI_F = 3.14159265;
const float TWO_PI_F = 6.2831853;
const float BAND_BLEND_HALF_WIDTH = 0.25; // SOL::GAS_GIANT_BAND_BLEND_HALF_WIDTH (keep in sync)
float3 n = normalize(Pos);
float lat = asin(clamp(n.z, -1.0, 1.0));
float lon = atan2(n.y, n.x);

// Band phases: 16 samples from the south to the north pole. Wrap-safe (SDD 14 Amendment 3, mirrors
// SOLGasGiant::BandBlendAt): each sample's phase advects the noise rigidly, and the two neighbouring samples'
// fields are cross-faded over the middle of the gap, so nothing jumps when neighbouring phases drift more than half
// a turn apart
float sampleF = saturate(lat / PI_F + 0.5) * 15.0;
int i0 = min((int)floor(sampleF), 14);
float w = sampleF - i0;
float cw = smoothstep(0.5 - BAND_BLEND_HALF_WIDTH, 0.5 + BAND_BLEND_HALF_WIDTH, w);

// Advected points on the unit sphere (seamless in longitude), stretched so features are longer east-west
float cosLat = cos(lat);
float lonA = lon - TWO_PI_F * g.Phase(P0, P1, P2, P3, i0);
float lonB = lon - TWO_PI_F * g.Phase(P0, P1, P2, P3, i0 + 1);
float3 stretch = Params.z * float3(1.0, 1.0, 4.0);
float3 qsA = float3(cosLat * cos(lonA), cosLat * sin(lonA), n.z) * stretch;
float3 qsB = float3(cosLat * cos(lonB), cosLat * sin(lonB), n.z) * stretch;
float3 N = normalize(NormalWS);
float light = saturate(dot(N, SunDir.xyz)) * pow(saturate(dot(N, normalize(CamVec))), 0.25);
"""

DECK_HLSL = COMMON_HLSL + r"""
// Bands: latitude profile (two harmonics) warped by turbulence
float2 fbm;
[branch] if (cw <= 0.0)
{
    fbm = g.Fbm2(qsA, 7, 3);
}
else if (cw >= 1.0)
{
    fbm = g.Fbm2(qsB, 7, 3);
}
else
{
    fbm = lerp(g.Fbm2(qsA, 7, 3), g.Fbm2(qsB, 7, 3), cw);
}
float turb = fbm.x - 0.5;
float s = n.z + turb * Params.y;
// cos keeps a light equatorial zone; the inner sine makes the band widths uneven, as on the real giants
float b1 = 0.5 + 0.5 * cos(TWO_PI_F * (s * Params.x + 0.3 * sin(TWO_PI_F * s * 1.3)));
float b2 = 0.5 + 0.5 * cos(TWO_PI_F * s * Params.x * 2.37 + 1.3);
float zone = smoothstep(0.3, 0.7, b1 * 0.75 + b2 * 0.25);
float detail = fbm.y;
float3 col = lerp(Belt, Zone, zone);
col = lerp(col, Accent, saturate((detail - 0.55) * 3.0) * 0.5);
col *= 0.85 + 0.3 * detail;

// Storms: oval vortices in the body frame with a spiral interior and a light collar, StormShape.x times longer
// east-west; kind w > StormShape.y = primary storm colour
float4 S[8] = { S0, S1, S2, S3, S4, S5, S6, S7 };
[unroll] for (int k = 0; k < 8; ++k)
{
    float4 st = S[k];
    if (st.w <= 0.0)
    {
        continue;
    }
    float cl = cos(st.x);
    float3 c = float3(cl * cos(st.y), cl * sin(st.y), sin(st.x));
    if (dot(n, c) <= 0.0)
    {
        continue;
    }
    float3 e = float3(-sin(st.y), cos(st.y), 0.0);
    float3 nt = cross(c, e);
    float3 dv = n - c;
    float u = dot(dv, e) / (st.z * StormShape.x);
    float v = dot(dv, nt) / st.z;
    float r = length(float2(u, v));
    if (r > 1.3)
    {
        continue;
    }
    float ang = atan2(v, u) + (1.0 - r) * 4.0;
    float swirl = g.Fbm(float3(cos(ang) * r * 5.0, sin(ang) * r * 5.0, k * 3.7), 3);
    float inside = 1.0 - smoothstep(0.75, 1.0, r);
    float collar = smoothstep(0.7, 0.95, r) * (1.0 - smoothstep(1.0, 1.3, r));
    float3 stormCol = st.w > StormShape.y ? StormCol : Zone * 1.1;
    col = lerp(col, stormCol * (0.7 + 0.6 * swirl), inside);
    col = lerp(col, Zone * 1.05, collar * 0.6);
}

// Polar tint, then self-shading (albedo * (Lambert * limb + ambient) * E / pi)
col = lerp(col, Polar, smoothstep(Params.w, 1.0, abs(n.z)));
return col * (light + Light.y) * Light.x / PI_F;
"""

HAZE_HLSL = COMMON_HLSL + r"""
// Sparse high clouds: thresholded fBm, thinner toward the poles; Params.y here is the haze opacity
float hazeNoise;
[branch] if (cw <= 0.0)
{
    hazeNoise = g.Fbm(qsA * 1.7 + 3.0, 3);
}
else if (cw >= 1.0)
{
    hazeNoise = g.Fbm(qsB * 1.7 + 3.0, 3);
}
else
{
    hazeNoise = lerp(g.Fbm(qsA * 1.7 + 3.0, 3), g.Fbm(qsB * 1.7 + 3.0, 3), cw);
}
float cloud = smoothstep(0.52, 0.78, hazeNoise);
float alpha = cloud * Params.y * (1.0 - 0.5 * smoothstep(Params.w, 1.0, abs(n.z)));
return float4(Zone * (light + Light.y) * Light.x / PI_F, alpha);
"""

# Near volumetric cloud layer (14d, SDD 14 Amendment 3), evaluated per ray-march sample by the engine cloud renderer.
# Returns (albedo, extinction 1/m). Position relative to the planet centre gives the altitude in the layer and, in the
# body frame (BodyAxisX/Y/Z), the latitude and band-advected longitude, using the same wrap-safe phase cross-fade as the
# shells. Coverage follows the band profile (zones carry more cloud than belts); 3 octaves of value noise in kilometres
# shape the cloud tops, eroded toward the top of the layer. Faded by NearBlend
CLOUD_HLSL = COMMON_HLSL[:COMMON_HLSL.index("const float PI_F")] + r"""
const float PI_F = 3.14159265;
const float TWO_PI_F = 6.2831853;
const float BAND_BLEND_HALF_WIDTH = 0.25; // SOL::GAS_GIANT_BAND_BLEND_HALF_WIDTH (keep in sync)
float3 d = WP - Center.xyz;
float r = length(d);
float hN = ((r - Center.w) * 1e-5 - Layer.x) / Layer.y;
if (hN <= 0.0 || hN >= 1.0 || Blend <= 0.0)
{
    return float4(0, 0, 0, 0);
}
float3 n = d / r;
float3 b = float3(dot(n, AX), dot(n, AY), dot(n, AZ));
float lat = asin(clamp(b.z, -1.0, 1.0));
float lon = atan2(b.y, b.x);

// Wrap-safe band advection (see the shells)
float sampleF = saturate(lat / PI_F + 0.5) * 15.0;
int i0 = min((int)floor(sampleF), 14);
float cw = smoothstep(0.5 - BAND_BLEND_HALF_WIDTH, 0.5 + BAND_BLEND_HALF_WIDTH, sampleF - i0);
float cosLat = cos(lat);
float lonA = lon - TWO_PI_F * g.Phase(P0, P1, P2, P3, i0);
float lonB = lon - TWO_PI_F * g.Phase(P0, P1, P2, P3, i0 + 1);

// Noise space in kilometres over FeatureKm, stretched east-west, with altitude as a slow third axis
float radiusKm = Center.w * 1e-5;
float3 k = float3(1.0, 1.0, 2.5) * (radiusKm / Noise.x);
float altK = hN * Layer.y / Noise.x * 4.0;
float3 pA = float3(cosLat * cos(lonA), cosLat * sin(lonA), b.z) * k + altK;
float3 pB = float3(cosLat * cos(lonB), cosLat * sin(lonB), b.z) * k + altK;
float shape;
[branch] if (cw <= 0.0)
{
    shape = g.Fbm(pA, 3);
}
else if (cw >= 1.0)
{
    shape = g.Fbm(pB, 3);
}
else
{
    shape = lerp(g.Fbm(pA, 3), g.Fbm(pB, 3), cw);
}

// Band coverage: zones (light) are cloudier than belts; same profile as the deck without its turbulence
float s = b.z + (shape - 0.45) * Params.y;
float zone = smoothstep(0.3, 0.7, 0.5 + 0.5 * cos(TWO_PI_F * (s * Params.x + 0.3 * sin(TWO_PI_F * s * 1.3))));
float coverage = lerp(Noise.y, Noise.z, zone);

// Vertical profile: soft base, tops eroded so higher cloud needs denser noise
float profile = smoothstep(0.0, 0.12, hN) * (1.0 - smoothstep(0.55, 1.0, hN));
float density = saturate((shape - (1.0 - coverage) - 0.3 * hN) * 3.0) * profile;
float3 albedo = saturate(lerp(Belt, Zone, zone) * 1.15);
return float4(albedo, density * Noise.w * Blend);
"""


# Connects two expressions and fails loudly if the named pin does not exist
def connect(src, src_output, dst, dst_input):
    if not MEL.connect_material_expressions(src, src_output, dst, dst_input):
        raise RuntimeError("SOLGasGiantMaterials: failed to connect %s -> %s.%r" % (src.get_name(), dst.get_name(),
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


# Creates a named vector parameter
def vector(material, name, value, x, y):
    expr = node(material, unreal.MaterialExpressionVectorParameter, x, y)
    expr.set_editor_property("parameter_name", name)
    expr.set_editor_property("default_value", unreal.LinearColor(*value))
    return expr


# Creates a component mask node keeping the given channels of src
def mask(material, src, channels, x, y):
    expr = node(material, unreal.MaterialExpressionComponentMask, x, y)
    for channel in "rgba":
        expr.set_editor_property(channel, channel in channels)
    connect(src, "", expr, "")
    return expr


# Creates an AppendVector node of a and b
def append(material, a, b, x, y):
    expr = node(material, unreal.MaterialExpressionAppendVector, x, y)
    connect(a, "", expr, "A")
    connect(b, "", expr, "B")
    return expr


# Creates a Custom HLSL node with named inputs wired from (name, expression[, output pin]) entries and the given output
# type (a vector parameter's float4 needs its "RGBA" pin; its default pin is float3)
def custom(material, code, output_type, inputs, x, y, description):
    expr = node(material, unreal.MaterialExpressionCustom, x, y)
    expr.set_editor_property("code", code)
    expr.set_editor_property("output_type", output_type)
    expr.set_editor_property("description", description)
    custom_inputs = []
    for entry in inputs:
        name = entry[0]
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    expr.set_editor_property("inputs", custom_inputs)
    for entry in inputs:
        connect(entry[1], entry[2] if len(entry) > 2 else "", expr, entry[0])
    return expr


# Deletes an existing material when rebuilding; returns False if it exists and should be kept
def prepare(path):
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("SOLGasGiantMaterials: %s exists, skipping" % path)
            return False
        EAL.delete_asset(path)
    return True


# Creates the parameters shared by both materials and returns the Custom node inputs
def common_inputs(material, phase_prefix, with_storms):
    inputs = [("Pos", node(material, unreal.MaterialExpressionLocalPosition, -1600, -600))]
    for i in range(4):
        inputs.append(("P%d" % i, vector(material, "%s%d" % (phase_prefix, i), (0, 0, 0, 0), -1600, -500 + 100 * i),
                       "RGBA"))
    if with_storms:
        for i in range(8):
            storm = vector(material, "Storm%d" % i, (0, 0, 0, 0), -1600, -100 + 100 * i)
            inputs.append(("S%d" % i, storm, "RGBA"))
    zone = vector(material, "ZoneColor", (0.86, 0.8, 0.7, 1), -1300, -600)
    inputs.append(("Zone", mask(material, zone, "rgb", -1100, -600)))
    if with_storms:
        for name, pin, value, y in (("BeltColor", "Belt", (0.6, 0.38, 0.2, 1), -500),
                                    ("AccentColor", "Accent", (0.7, 0.5, 0.35, 1), -400),
                                    ("PolarColor", "Polar", (0.5, 0.45, 0.4, 1), -300),
                                    ("StormColor", "StormCol", (0.7, 0.3, 0.15, 1), -200)):
            inputs.append((pin, mask(material, vector(material, name, value, -1300, y), "rgb", -1100, y)))
        # (storm east-west stretch, primary-kind threshold): SOL::GAS_GIANT_STORM_EAST_WEST_STRETCH and
        # SOL::GAS_GIANT_PRIMARY_STORM_KIND_THRESHOLD, set by ASOLGasGiantVisuals
        inputs.append(("StormShape", vector(material, "StormShape", (1.6, 0.75, 0, 0), -1300, -700), "RGBA"))
    inputs.append(("Params", vector(material, "ShapeParams", (6.0, 0.08, 6.0, 0.85), -1300, -100), "RGBA"))
    sun = vector(material, "SunDirection", (1, 0, 0, 0), -1300, 0)
    inputs.append(("SunDir", mask(material, sun, "rgb", -1100, 0)))
    illum = scalar(material, "SunIlluminance", 3.0, -1300, 100)
    ambient = scalar(material, "AmbientLight", 0.02, -1300, 200)
    inputs.append(("Light", append(material, illum, ambient, -1100, 150)))
    inputs.append(("NormalWS", node(material, unreal.MaterialExpressionVertexNormalWS, -1300, 300)))
    inputs.append(("CamVec", node(material, unreal.MaterialExpressionCameraVectorWS, -1300, 400)))
    return inputs


# Builds the opaque deck material
def build_deck():
    path = MATERIAL_DIR + "/" + DECK_NAME
    if not prepare(path):
        return
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(DECK_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    inputs = common_inputs(material, "BandPhases", True)
    deck = custom(material, DECK_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, -600, 0,
                  "SOL gas giant deck")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    MEL.connect_material_property(deck, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("SOLGasGiantMaterials: created %s" % path)


# Builds the translucent haze material
def build_haze():
    path = MATERIAL_DIR + "/" + HAZE_NAME
    if not prepare(path):
        return
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(HAZE_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    inputs = common_inputs(material, "HazePhases", False)
    haze = custom(material, HAZE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4, inputs, -600, 0,
                  "SOL gas giant haze")
    rgb = mask(material, haze, "rgb", -300, -50)
    alpha = mask(material, haze, "a", -300, 50)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("SOLGasGiantMaterials: created %s" % path)


# Builds the volume-domain near cloud material for the engine VolumetricCloud component
def build_cloud():
    path = MATERIAL_DIR + "/" + CLOUD_NAME
    if not prepare(path):
        return
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(CLOUD_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    inputs = [("WP", node(material, unreal.MaterialExpressionWorldPosition, -1600, -700))]
    inputs.append(("Center", vector(material, "PlanetCenter", (0, 0, 0, 7.1492e9), -1600, -600), "RGBA"))
    for i, axis in enumerate("XYZ"):
        value = [0, 0, 0, 0]
        value[i] = 1
        inputs.append(("A" + axis, mask(material, vector(material, "BodyAxis" + axis, value, -1600, -500 + 100 * i),
                                        "rgb", -1400, -500 + 100 * i)))
    for i in range(4):
        inputs.append(("P%d" % i, vector(material, "BandPhases%d" % i, (0, 0, 0, 0), -1600, -200 + 100 * i), "RGBA"))
    inputs.append(("Zone", mask(material, vector(material, "ZoneColor", (0.86, 0.8, 0.7, 1), -1600, 200), "rgb",
                                -1400, 200)))
    inputs.append(("Belt", mask(material, vector(material, "BeltColor", (0.6, 0.38, 0.2, 1), -1600, 300), "rgb",
                                -1400, 300)))
    inputs.append(("Params", vector(material, "ShapeParams", (4.5, 0.09, 6.0, 0.8), -1600, 400), "RGBA"))
    # (feature size km, belt coverage, zone coverage, extinction 1/m at full density)
    inputs.append(("Noise", vector(material, "CloudNoise", (300.0, 0.35, 0.6, 0.0005), -1600, 500), "RGBA"))
    # (layer bottom km above the planet radius, layer thickness km)
    inputs.append(("Layer", vector(material, "CloudLayer", (2.0, 40.0, 0, 0), -1600, 600), "RGBA"))
    inputs.append(("Blend", scalar(material, "NearBlend", 1.0, -1600, 700)))
    cloud = custom(material, CLOUD_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4, inputs, -900, 0,
                   "SOL gas giant near cloud")
    albedo = mask(material, cloud, "rgb", -500, -50)
    extinction = mask(material, cloud, "a", -500, 50)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    material.set_editor_property("used_with_volumetric_cloud", True)
    MEL.connect_material_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(extinction, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    # Ambient (sky-like) light so cloud sides away from the Sun are not black: emission = albedo * extinction * L
    ambient = scalar(material, "CloudAmbientLuminance", 0.2, -500, 200)
    emit_a = node(material, unreal.MaterialExpressionMultiply, -300, 100)
    connect(albedo, "", emit_a, "A")
    connect(extinction, "", emit_a, "B")
    emit = node(material, unreal.MaterialExpressionMultiply, -150, 150)
    connect(emit_a, "", emit, "A")
    connect(ambient, "", emit, "B")
    MEL.connect_material_property(emit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("SOLGasGiantMaterials: created %s" % path)


build_deck()
build_haze()
build_cloud()
