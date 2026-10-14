# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates (or, with SOL_REBUILD, rebuilds in place) the orbit line material and its parameter collection (issue #17,
# SDD 17 4.2-4.3 and Amendment 3), idempotent:
#   /Game/SOL/OrbitLines/MPC_SOLOrbitLine
#       Vector parameters written by ASOLOrbitLineVisuals (SOL::OrbitLineMpcParams):
#       OrbitLineFrame = (viewpoint render location cm, max render distance cm),
#       OrbitLineStyle = (width px at 1080 p, dash duty, fade start, fade end SMA),
#       OrbitLineStyle2 = (highlight min alpha, feather px, depth bias, scene-depth trust limit cm),
#       OrbitLineDash = (dash min period px, max level, solid start/end px),
#       OrbitLineSharedParentHigh / Low = the Sun minus the viewpoint (double-float pair, m, ecliptic),
#       OrbitLineOccluder0..7 = (viewpoint-relative centre m, ecliptic; radius m).
#       The defaults are read from Source/SOLTest/SOLConstants.h (the C++ writes every value at BeginPlay anyway).
#   /Game/SOL/OrbitLines/M_SOLOrbitLine
#       Translucent, unlit, depth test off. The vertex stage (WPO) rebuilds each ribbon vertex from its double-float
#       point (UV0-UV2), dash phase and side (UV3) and orbit tangent (vertex normal), and the per-line custom
#       primitive data (0-3 parent hi + SMA, 4-7 parent lo + highlight, 8-11 color, 12-15 rotation quaternion,
#       16 shared parent); the pixel stage computes coverage x dash x fade x occlusion (SOLOrbitLine.ush).
# The parameter names must match SOL::OrbitLineMpcParams in Source/SOLTest/SOLConstants.h and the asset paths
# SOL::Paths::ORBIT_LINE_MATERIAL / ORBIT_LINE_PARAMETER_COLLECTION.
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
#       -script="<abs path>/Tools/OrbitLines/create_orbit_line_assets.py" -unattended [-SOLRebuild]
# or, in an open editor, exec() this file with SOL_REBUILD = True defined in the calling scope's globals.
import os
import re

import unreal

LINE_DIR = "/Game/SOL/OrbitLines"
MPC_NAME = "MPC_SOLOrbitLine"
MATERIAL_NAME = "M_SOLOrbitLine"
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))

INCLUDE = "/Project/SOLTest/Private/SOLOrbitLine.ush"   # SOL::SHADER_VIRTUAL_DIRECTORY + file
CONSTANTS_FILE = os.path.join(unreal.Paths.project_dir(), "Source", "SOLTest", "SOLConstants.h")


# Returns a numeric SOL:: constant from SOLConstants.h, so the collection defaults cannot drift from the C++
def constant(name):
    text = open(CONSTANTS_FILE, encoding="utf-8").read()
    match = re.search(r"\b" + name + r"\s*=\s*([-0-9.eE+]+)", text)
    if match is None:
        raise RuntimeError("create_orbit_line_assets: %s not found in %s" % (name, CONSTANTS_FILE))
    return float(match.group(1))


OCCLUDER_COUNT = int(constant("ORBIT_LINE_MAX_OCCLUDERS"))
MPC_DEFAULTS = [
    ("OrbitLineFrame", unreal.LinearColor(0.0, 0.0, 0.0, constant("DEFAULT_MAX_RENDER_DISTANCE_CM"))),
    ("OrbitLineStyle", unreal.LinearColor(constant("ORBIT_LINE_WIDTH_PX"), constant("ORBIT_LINE_DASH_DUTY"),
        constant("ORBIT_LINE_FADE_START_SMA"), constant("ORBIT_LINE_FADE_END_SMA"))),
    ("OrbitLineStyle2", unreal.LinearColor(constant("ORBIT_LINE_HIGHLIGHT_MIN_ALPHA"),
        constant("ORBIT_LINE_AA_MARGIN_PX"), constant("ORBIT_LINE_DEPTH_BIAS"),
        constant("DEFAULT_MAX_RENDER_DISTANCE_CM"))),
    ("OrbitLineDash", unreal.LinearColor(constant("ORBIT_LINE_DASH_MIN_PERIOD_PX"),
        constant("ORBIT_LINE_DASH_MAX_LEVEL"), constant("ORBIT_LINE_DASH_SOLID_START_PX"),
        constant("ORBIT_LINE_DASH_SOLID_END_PX"))),
    ("OrbitLineSharedParentHigh", unreal.LinearColor(0.0, 0.0, 0.0, 0.0)),
    ("OrbitLineSharedParentLow", unreal.LinearColor(0.0, 0.0, 0.0, 0.0)),
] + [("OrbitLineOccluder%d" % i, unreal.LinearColor(0.0, 0.0, 0.0, 0.0)) for i in range(OCCLUDER_COUNT)]

# Per-line custom primitive data vectors (name, first float index), SOL::ORBIT_LINE_CUSTOM_DATA_TOTAL_FLOATS
CUSTOM_DATA_VECTORS = [("LineHi", 0), ("LineLo", 4), ("LineColor", 8), ("LineRotation", 12), ("LineExtra", 16)]

VERTEX_HLSL = r"""// Ribbon vertex (SDD 17 4.2): the vertex positions are all zero and the component sits at
// the origin, so the world position offset is the target render position itself. Asteroid lines
// (LineExtra.x = 1) take their parent offset from the collection
float3 trueRelative;
float signedPx;
const bool bShared = LineExtra.x > 0.5;
const float3 parentHi = bShared ? SharedHi.xyz : LineHi.xyz;
const float3 parentLo = bShared ? SharedLo.xyz : LineLo.xyz;
const float3 cameraUnreal = WSDemote(GetWorldCameraOrigin(Parameters));
const float3 target = SOLOL_Vertex(parentHi, parentLo, float3(UV0.xy, UV1.x), float3(UV1.y, UV2.xy),
    LineRotation, normalize(Tangent), UV3.y, cameraUnreal, Frame, Style, Style2,
    ResolvedView.ViewForward, ResolvedView.ViewToClip[0][0], ResolvedView.ViewSizeAndInvSize.xy,
    trueRelative, signedPx);
Interp = float4(trueRelative, signedPx);
return target;"""

PIXEL_HLSL = r"""// Line opacity (SDD 17 4.2-4.3)
float4 occluders[SOLOL_MAX_OCCLUDERS] = { O0, O1, O2, O3, O4, O5, O6, O7 };
const float phase = UV3.x;
return SOLOL_Pixel(Interp.xyz, Interp.w, phase, fwidth(phase),
    WSDemote(GetWorldCameraOrigin(Parameters)), Frame, Style, Style2, Dash, LineHi.w, LineLo.w > 0.5,
    LineColor.a, ResolvedView.ViewSizeAndInvSize.y, PixelDepth, SceneDepth, occluders);"""

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


# Returns the parameter collection, creating it, and adding any missing parameter with its default
def ensure_collection():
    path = LINE_DIR + "/" + MPC_NAME
    mpc = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if mpc is None:
        mpc = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MPC_NAME, LINE_DIR,
            unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    params = list(mpc.get_editor_property("vector_parameters"))
    existing = {str(p.get_editor_property("parameter_name")) for p in params}
    changed = False
    for name, default in MPC_DEFAULTS:
        if name not in existing:
            param = unreal.CollectionVectorParameter()
            param.set_editor_property("parameter_name", name)
            param.set_editor_property("default_value", default)
            params.append(param)
            changed = True
    if changed:
        mpc.set_editor_property("vector_parameters", params)
        EAL.save_loaded_asset(mpc)
    return mpc


# Adds an expression of the given class at an editor position
def add(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


# Adds a Custom node with the given float inputs, code and output type
def add_custom(material, x, y, description, code, output_type, input_names):
    node = add(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("description", description)
    node.set_editor_property("output_type", output_type)
    node.set_editor_property("code", code)
    node.set_editor_property("include_file_paths", [INCLUDE])
    inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    node.set_editor_property("inputs", inputs)
    return node


# Builds the line material graph into an empty material
def build_material(material, mpc):
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", True)
    material.set_editor_property("use_translucency_vertex_fog", False)
    material.set_editor_property("allow_negative_emissive_color", False)

    # Shared inputs: texture coordinates, the orbit tangent, per-line custom primitive data and the collection
    uvs = []
    for index in range(4):
        uv = add(material, unreal.MaterialExpressionTextureCoordinate, -1600, -400 + 90 * index)
        uv.set_editor_property("coordinate_index", index)
        uvs.append(uv)
    tangent = add(material, unreal.MaterialExpressionVertexNormalWS, -1600, 0)
    cpd = {}
    for index, (name, data_index) in enumerate(CUSTOM_DATA_VECTORS):
        node = add(material, unreal.MaterialExpressionVectorParameter, -1600, 100 + 120 * index)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("use_custom_primitive_data", True)
        node.set_editor_property("primitive_data_index", data_index)
        cpd[name] = node
    params = {}
    for index, (name, _) in enumerate(MPC_DEFAULTS):
        node = add(material, unreal.MaterialExpressionCollectionParameter, -1600, 800 + 90 * index)
        node.set_editor_property("collection", mpc)
        node.set_editor_property("parameter_name", name)
        params[name] = node

    # Vertex stage: world position offset plus the interpolated true position and signed pixel distance
    vertex = add_custom(material, -1100, -300, "SOLOrbitLineVertex", VERTEX_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        ["UV0", "UV1", "UV2", "UV3", "Tangent", "LineHi", "LineLo", "LineRotation", "LineExtra", "SharedHi",
         "SharedLo", "Frame", "Style", "Style2"])
    extra = unreal.CustomOutput()
    extra.set_editor_property("output_name", "Interp")
    extra.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    vertex.set_editor_property("additional_outputs", [extra])
    for index in range(4):
        MEL.connect_material_expressions(uvs[index], "", vertex, "UV%d" % index)
    MEL.connect_material_expressions(tangent, "", vertex, "Tangent")
    MEL.connect_material_expressions(cpd["LineHi"], "RGBA", vertex, "LineHi")
    MEL.connect_material_expressions(cpd["LineLo"], "RGBA", vertex, "LineLo")
    MEL.connect_material_expressions(cpd["LineRotation"], "RGBA", vertex, "LineRotation")
    MEL.connect_material_expressions(cpd["LineExtra"], "RGBA", vertex, "LineExtra")
    MEL.connect_material_expressions(params["OrbitLineSharedParentHigh"], "", vertex, "SharedHi")
    MEL.connect_material_expressions(params["OrbitLineSharedParentLow"], "", vertex, "SharedLo")
    MEL.connect_material_expressions(params["OrbitLineFrame"], "", vertex, "Frame")
    MEL.connect_material_expressions(params["OrbitLineStyle"], "", vertex, "Style")
    MEL.connect_material_expressions(params["OrbitLineStyle2"], "", vertex, "Style2")
    MEL.connect_material_property(vertex, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    interpolator = add(material, unreal.MaterialExpressionVertexInterpolator, -800, -200)
    MEL.connect_material_expressions(vertex, "Interp", interpolator, "VS")

    # Pixel stage: opacity from SOLOL_Pixel, the line color as emissive
    pixel_depth = add(material, unreal.MaterialExpressionPixelDepth, -800, 0)
    scene_depth = add(material, unreal.MaterialExpressionSceneDepth, -800, 80)
    occluder_names = ["O%d" % i for i in range(OCCLUDER_COUNT)]
    pixel = add_custom(material, -500, 100, "SOLOrbitLinePixel", PIXEL_HLSL,
        unreal.CustomMaterialOutputType.CMOT_FLOAT1,
        ["Interp", "UV3", "Frame", "Style", "Style2", "Dash", "LineHi", "LineLo", "LineColor", "PixelDepth",
         "SceneDepth"]
        + occluder_names)
    MEL.connect_material_expressions(interpolator, "PS", pixel, "Interp")
    MEL.connect_material_expressions(uvs[3], "", pixel, "UV3")
    MEL.connect_material_expressions(params["OrbitLineFrame"], "", pixel, "Frame")
    MEL.connect_material_expressions(params["OrbitLineStyle"], "", pixel, "Style")
    MEL.connect_material_expressions(params["OrbitLineStyle2"], "", pixel, "Style2")
    MEL.connect_material_expressions(params["OrbitLineDash"], "", pixel, "Dash")
    MEL.connect_material_expressions(cpd["LineHi"], "RGBA", pixel, "LineHi")
    MEL.connect_material_expressions(cpd["LineLo"], "RGBA", pixel, "LineLo")
    MEL.connect_material_expressions(cpd["LineColor"], "RGBA", pixel, "LineColor")
    MEL.connect_material_expressions(pixel_depth, "", pixel, "PixelDepth")
    MEL.connect_material_expressions(scene_depth, "", pixel, "SceneDepth")
    for index, name in enumerate(occluder_names):
        MEL.connect_material_expressions(params["OrbitLineOccluder%d" % index], "", pixel, name)
    MEL.connect_material_property(pixel, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.connect_material_property(cpd["LineColor"], "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


# Creates the material, or rebuilds its graph in place when SOL_REBUILD is set
def ensure_material(mpc):
    path = LINE_DIR + "/" + MATERIAL_NAME
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("create_orbit_line_assets: %s exists (pass -SOLRebuild to rebuild)" % path)
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
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MATERIAL_NAME, LINE_DIR, unreal.Material,
            unreal.MaterialFactoryNew())
    build_material(material, mpc)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("create_orbit_line_assets: built %s" % path)


ensure_material(ensure_collection())
