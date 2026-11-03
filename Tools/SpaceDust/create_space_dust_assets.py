# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates (or, with SOL_REBUILD, rebuilds: see below) the space dust speed-cue assets (issue #43, SDD 43 sections 5.4,
# 5.5, 8.1 and Amendment 1), idempotent:
#   /Game/SOL/SpaceDust/M_SOLSpaceDust
#       Unlit, additive, translucent, drawn after motion blur (outside TSR, like M_SOLOrbitLine since issue #25), depth
#       test on, no depth write, two-sided, usage Niagara sprites. Emissive = ParticleColor.rgb x Intensity x
#       lerp(BrightnessMin, 1, ParticleRandom) x a soft capsule profile (a round dot when the sprite is square, a
#       capsule with round caps when it is stretched along the velocity); opacity = ParticleColor.a.
#       Scalar parameters: Intensity (SOL::SpaceDust::INTENSITY), BrightnessMin.
#   /Game/SOL/SpaceDust/NS_SOLSpaceDust
#       One GPU-sim, local-space emitter "SpaceDust" with fixed bounds of +-L/2: a one-shot burst of
#       User.SpaceDustParticleCount particles that never die. Spawn: Particles.DustAnchor uniform in [0, L)^3,
#       Particles.DustSizeFactor in [POINT_SIZE_RANDOM_MIN, _MAX], Particles.MaterialRandom (the brightness seed).
#       Update: the wrapped position, the alignment-only velocity (never integrated: no Solve Forces and Velocity),
#       Particles.DustFade, then the colour and the sprite size (SDD 5.4). Velocity-aligned sprite renderer facing the
#       camera position, M_SOLSpaceDust, no sorting, motion vectors disabled, no shadows. Every default leaves it
#       invisible (SpaceDustOpacity = 0) until the C++ drives it.
#       User parameters (must match SOL::SpaceDustParams in Source/SOLTest/SOLConstants.h; defaults are read from
#       SOL::SpaceDust there): SpaceDustParticleCount (int), SpaceDustBoxSizeCm, SpaceDustPointSizeCm,
#       SpaceDustRadialFadeStart, SpaceDustNearFadeStartCm, SpaceDustNearFadeEndCm, SpaceDustOpacity,
#       SpaceDustStreakLengthCm, SpaceDustStreakAlpha (float), SpaceDustColor (LinearColor), SpaceDustOffsetCm,
#       SpaceDustVelocityCmS, SpaceDustCameraLocalCm (Vector3f).
# The asset paths must match SOL::Paths::SPACE_DUST_MATERIAL / SPACE_DUST_NIAGARA_SYSTEM.
# The Niagara system is edited through the engine's NiagaraToolsets plugin (unreal.ToolsetRegistry.execute_tool, the
# same JSON tools unreal-mcp exposes), so it needs a running editor; the material part also runs headless. Nothing is
# left to hand-author: the whole recipe is in this file (and in SDD 43 Amendment 1).
# Run, in an open editor with Play In Editor stopped (Python console or an editor-Python bridge):
#   SOL_REBUILD = True; exec(open(r"<abs path>/Tools/SpaceDust/create_space_dust_assets.py", encoding="utf-8").read())
# or set the environment variable SOL_REBUILD=1, or pass -SOLRebuild on the editor command line. Without a rebuild
# flag, existing assets are left untouched. A rebuild in a running editor deletes both assets and creates them again
# at the same paths: rebuilding the system in place (removing and re-adding its emitter and user parameters through the
# toolset) left a system whose emitter was silently disabled, so every activation completed on its first tick and drew
# nothing. Nothing else references the assets (the C++ loads the system by path, and only the system uses the
# material), so nothing needs fixing up. Headless (-run=pythonscript) only the material is built, in place: the
# toolset registry is not available there, and the system is skipped with a warning.
import json
import os
import re

import unreal

DUST_DIR = "/Game/SOL/SpaceDust"
MATERIAL_NAME = "M_SOLSpaceDust"
SYSTEM_NAME = "NS_SOLSpaceDust"
EMITTER_NAME = "SpaceDust"
REBUILD = (("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))
           or os.environ.get("SOL_REBUILD", "") not in ("", "0"))
CONSTANTS_FILE = os.path.join(unreal.Paths.project_dir(), "Source", "SOLTest", "SOLConstants.h")

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

BRIGHTNESS_MIN = 0.6   # darkest particle relative to the brightest (per-particle seed, Particles.MaterialRandom)


# Returns the text of the SOL::SpaceDust namespace block in SOLConstants.h
def space_dust_block():
    text = open(CONSTANTS_FILE, encoding="utf-8").read()
    match = re.search(r"namespace SpaceDust\s*\{(.*?)\n    \}", text, re.S)
    if match is None:
        raise RuntimeError("create_space_dust_assets: namespace SpaceDust not found in %s" % CONSTANTS_FILE)
    return match.group(1)


# Returns a numeric SOL::SpaceDust constant from SOLConstants.h, so the asset defaults cannot drift from the C++
def constant(name):
    match = re.search(r"\b" + name + r"\s*=\s*([-0-9.eE+]+)", space_dust_block())
    if match is None:
        raise RuntimeError("create_space_dust_assets: SpaceDust::%s not found in %s" % (name, CONSTANTS_FILE))
    return float(match.group(1))


# Returns SOL::SpaceDust::DUST_COLOR from SOLConstants.h as (r, g, b)
def dust_color():
    match = re.search(r"DUST_COLOR\s*\(\s*([-0-9.eE+]+)f?\s*,\s*([-0-9.eE+]+)f?\s*,\s*([-0-9.eE+]+)f?",
                      space_dust_block())
    if match is None:
        raise RuntimeError("create_space_dust_assets: SpaceDust::DUST_COLOR not found in %s" % CONSTANTS_FILE)
    return tuple(float(match.group(i)) for i in (1, 2, 3))


# ---------------------------------------------------------------------------------------------------------------------
# Material
# ---------------------------------------------------------------------------------------------------------------------

# Soft capsule across the velocity-aligned sprite: UV.x runs across the width (Size.x), UV.y along the streak
# (Size.y >= Size.x). A square sprite gives a round dot, a long one a capsule with a round cap at each end, so the
# points and streaks stay anti-aliased without TSR
PROFILE_HLSL = r"""// Space dust sprite profile (SDD 43 5.5)
float2 size = max(Size, float2(1e-3, 1e-3));
float halfWidth = 0.5 * size.x;
float along = abs(UV.y - 0.5) * size.y;
float core = max(0.0, 0.5 * size.y - halfWidth);
float a = max(0.0, along - core) / halfWidth;
float x = (UV.x - 0.5) * 2.0;
float f = saturate(1.0 - (x * x + a * a));
return f * f;"""


# Adds an expression of the given class at an editor position
def add(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


# Adds a scalar parameter node with a default value
def add_scalar(material, name, value, x, y):
    node = add(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


# Builds the dust material graph into an empty material
def build_material(material):
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", False)
    material.set_editor_property("use_translucency_vertex_fog", False)
    # Drawn after motion blur, i.e. after TSR (as M_SOLOrbitLine, issue #25): the box rides with the ship without motion
    # vectors, so TSR would reproject the thin points with the background's motion and smear them (SDD 43 4.5)
    material.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
    material.set_editor_property("allow_negative_emissive_color", False)
    MEL.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)

    # Sprite profile from the sprite UV and the particle's sprite size
    uv = add(material, unreal.MaterialExpressionTextureCoordinate, -1000, -100)
    size = add(material, unreal.MaterialExpressionParticleSize, -1000, 0)
    profile = add(material, unreal.MaterialExpressionCustom, -750, -60)
    profile.set_editor_property("description", "SOLSpaceDustProfile")
    profile.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    profile.set_editor_property("code", PROFILE_HLSL)
    inputs = []
    for name in ("UV", "Size"):
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    profile.set_editor_property("inputs", inputs)
    MEL.connect_material_expressions(uv, "", profile, "UV")
    MEL.connect_material_expressions(size, "", profile, "Size")

    # Per-particle brightness from the seed (Particles.MaterialRandom)
    random = add(material, unreal.MaterialExpressionParticleRandom, -1000, 200)
    low = add_scalar(material, "BrightnessMin", BRIGHTNESS_MIN, -1000, 280)
    one = add(material, unreal.MaterialExpressionConstant, -1000, 380)
    one.set_editor_property("r", 1.0)
    brightness = add(material, unreal.MaterialExpressionLinearInterpolate, -750, 260)
    MEL.connect_material_expressions(low, "", brightness, "A")
    MEL.connect_material_expressions(one, "", brightness, "B")
    MEL.connect_material_expressions(random, "", brightness, "Alpha")

    # Emissive = colour x intensity x brightness x profile; opacity = particle alpha (additive scales by it)
    color = add(material, unreal.MaterialExpressionParticleColor, -1000, 480)
    intensity = add_scalar(material, "Intensity", constant("INTENSITY"), -1000, 640)
    scaled = add(material, unreal.MaterialExpressionMultiply, -750, 520)
    MEL.connect_material_expressions(color, "RGB", scaled, "A")
    MEL.connect_material_expressions(intensity, "", scaled, "B")
    lit = add(material, unreal.MaterialExpressionMultiply, -550, 360)
    MEL.connect_material_expressions(scaled, "", lit, "A")
    MEL.connect_material_expressions(brightness, "", lit, "B")
    emissive = add(material, unreal.MaterialExpressionMultiply, -350, 100)
    MEL.connect_material_expressions(lit, "", emissive, "A")
    MEL.connect_material_expressions(profile, "", emissive, "B")
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(color, "A", unreal.MaterialProperty.MP_OPACITY)


# Creates the material, or rebuilds its graph in place when SOL_REBUILD is set and it still exists (a headless rebuild;
# in a running editor the rebuild has deleted it first)
def ensure_material():
    path = DUST_DIR + "/" + MATERIAL_NAME
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("create_space_dust_assets: %s exists (set SOL_REBUILD to rebuild)" % path)
            return EAL.load_asset(path)
        material = EAL.load_asset(path)
        # delete_all_material_expressions leaves some nodes behind, so delete one by one until none are left
        for _ in range(8):
            expressions = list(MEL.get_material_expressions(material))
            if not expressions:
                break
            for expression in expressions:
                MEL.delete_material_expression(material, expression)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MATERIAL_NAME, DUST_DIR, unreal.Material,
            unreal.MaterialFactoryNew())
    build_material(material)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("create_space_dust_assets: built %s" % path)
    return material


# ---------------------------------------------------------------------------------------------------------------------
# Niagara system (NiagaraToolsets JSON tools)
# ---------------------------------------------------------------------------------------------------------------------

TOOLSET = "NiagaraToolsets.NiagaraToolset_System"
SYSTEM_TEMPLATE = "/Niagara/DefaultAssets/Templates/Systems/MinimalLightweight.MinimalLightweight"
EMITTER_TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"
SPAWN_BURST_MODULE = "/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"

T_FLOAT = "/Script/Niagara.NiagaraFloat"
T_INT = "/Script/Niagara.NiagaraInt32"
T_BOOL = "/Script/Niagara.NiagaraBool"
T_POSITION = "/Script/Niagara.NiagaraPosition"
T_VEC2 = "/Script/CoreUObject.Vector2f"
T_VEC3 = "/Script/CoreUObject.Vector3f"
T_COLOR = "/Script/CoreUObject.LinearColor"
D_ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
D_LINKED = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
D_HLSL = "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"
NIAGARA_TRUE = -1   # FNiagaraBool stores true as -1
# Zero default value per attribute type (the variable-value struct that matches each Niagara type)
ZERO = {
    "/Script/Niagara.NiagaraFloat": {"struct": {"refPath": "/Script/Niagara.NiagaraFloat"}, "value": {"value": 0.0}},
    "/Script/Niagara.NiagaraPosition": {"struct": {"refPath": "/Script/Niagara.NiagaraPosition"},
                                        "value": {"x": 0.0, "y": 0.0, "z": 0.0}},
    "/Script/CoreUObject.Vector2f": {"struct": {"refPath": "/Script/CoreUObject.Vector2f"},
                                     "value": {"x": 0.0, "y": 0.0}},
    "/Script/CoreUObject.Vector3f": {"struct": {"refPath": "/Script/CoreUObject.Vector3f"},
                                     "value": {"x": 0.0, "y": 0.0, "z": 0.0}},
    "/Script/CoreUObject.LinearColor": {"struct": {"refPath": "/Script/CoreUObject.LinearColor"},
                                        "value": {"r": 0.0, "g": 0.0, "b": 0.0, "a": 0.0}},
}


# Calls one NiagaraToolset_System tool with JSON arguments and returns its decoded return value
def tool(name, args):
    result = unreal.ToolsetRegistry.execute_tool(TOOLSET, name, json.dumps(args))
    if result.error:
        raise RuntimeError("create_space_dust_assets: %s failed: %s" % (name, result.error))
    text = result.get_value_as_json_string()
    if not text:
        return None
    value = json.loads(text)
    if isinstance(value, str):
        value = json.loads(value)
    if isinstance(value, dict) and "returnValue" in value:
        value = value["returnValue"]
    return value


# Returns a stack item reference into the dust system (empty fields select nothing at that level)
def ref(system_path, emitter="", script="", module="", renderer=-1, inputs=None):
    return {"system": {"refPath": system_path}, "emitterName": emitter, "scriptName": script, "moduleName": module,
            "rendererIndex": renderer, "inputNameStack": inputs or []}


# Returns a Niagara type wrapper for a struct path
def niagara_type(struct_path):
    return {"classStructOrEnum": {"refPath": struct_path}}


# Returns an instanced value of the given struct
def value(struct_path, payload):
    return {"struct": {"refPath": struct_path}, "value": payload}


# Returns an HLSL expression input value
def hlsl(expression):
    return value(D_HLSL, {"hlslExpression": expression})


# Returns an input value linked to a variable of the given type
def linked(name, struct_path):
    return value(D_LINKED, {"linkedVariable": {"name": name, "type": niagara_type(struct_path)}})


# Returns an enum input value
def enum(enum_path, enum_name, display_name):
    return value(D_ENUM, {"enum": {"refPath": enum_path}, "enumName": enum_name, "displayName": display_name})


# Returns the user parameters (name, type, default value, description); every default leaves the dust invisible
def user_parameters():
    r, g, b = dust_color()
    zero = {"x": 0.0, "y": 0.0, "z": 0.0}
    return [
        ("SpaceDustParticleCount", T_INT, value(T_INT, {"value": int(constant("PARTICLE_COUNT"))}),
         "Particles spawned once by the burst (per effects quality)"),
        ("SpaceDustBoxSizeCm", T_FLOAT, value(T_FLOAT, {"value": constant("BOX_SIZE_CM")}),
         "Edge L of the wrapping box (cm)"),
        ("SpaceDustPointSizeCm", T_FLOAT, value(T_FLOAT, {"value": constant("POINT_SIZE_CM")}),
         "Base point size P (cm), the streak width"),
        ("SpaceDustRadialFadeStart", T_FLOAT, value(T_FLOAT, {"value": constant("RADIAL_FADE_START_FRACTION")}),
         "Radial fade starts at this fraction of L/2"),
        ("SpaceDustNearFadeStartCm", T_FLOAT, value(T_FLOAT, {"value": constant("NEAR_FADE_START_CM")}),
         "Invisible closer than this to the camera (cm)"),
        ("SpaceDustNearFadeEndCm", T_FLOAT, value(T_FLOAT, {"value": constant("NEAR_FADE_END_CM")}),
         "Fully visible beyond this distance from the camera (cm)"),
        ("SpaceDustColor", T_COLOR, value(T_COLOR, {"r": r, "g": g, "b": b, "a": 1.0}), "Point tint (emissive)"),
        ("SpaceDustOffsetCm", T_VEC3, value("/Script/CoreUObject.Vector", zero),
         "Accumulated apparent travel, wrapped into [0, L) on the CPU (cm)"),
        ("SpaceDustVelocityCmS", T_VEC3, value("/Script/CoreUObject.Vector", zero),
         "Smoothed apparent velocity (cm/s), for the streak alignment only"),
        ("SpaceDustCameraLocalCm", T_VEC3, value("/Script/CoreUObject.Vector", zero),
         "Camera position in the box's local space (cm), for the near fade"),
        ("SpaceDustOpacity", T_FLOAT, value(T_FLOAT, {"value": 0.0}), "Overall opacity (0 = invisible)"),
        ("SpaceDustStreakLengthCm", T_FLOAT, value(T_FLOAT, {"value": constant("POINT_SIZE_CM")}),
         "Streak length S (cm); a round point when S <= P"),
        ("SpaceDustStreakAlpha", T_FLOAT, value(T_FLOAT, {"value": 1.0}), "Streak dimming factor"),
    ]


# Spawn attributes: the fixed anchor in [0, L)^3 and the per-particle size factor
def spawn_entries():
    low = constant("POINT_SIZE_RANDOM_MIN")
    high = constant("POINT_SIZE_RANDOM_MAX")
    return [
        ("Particles.DustAnchor", T_VEC3, "float3(rand(1.0), rand(1.0), rand(1.0)) * User.SpaceDustBoxSizeCm"),
        ("Particles.DustSizeFactor", T_FLOAT, "%r + %r * rand(1.0)" % (low, round(high - low, 6))),
    ]


# Update step 1: the wrapped position and the alignment-only velocity
UPDATE_PLACE = [
    ("Particles.Position", T_POSITION,
     "frac((Particles.DustAnchor - User.SpaceDustOffsetCm) / User.SpaceDustBoxSizeCm) * User.SpaceDustBoxSizeCm"
     " - 0.5 * User.SpaceDustBoxSizeCm"),
    ("Particles.Velocity", T_VEC3, "-User.SpaceDustVelocityCmS"),
]
# Update step 2: DustFade = radial fade towards the box edge x near fade around the camera
UPDATE_FADE = [
    ("Particles.DustFade", T_FLOAT,
     "(1.0 - smoothstep(User.SpaceDustRadialFadeStart * 0.5 * User.SpaceDustBoxSizeCm,"
     " 0.5 * User.SpaceDustBoxSizeCm, length(Particles.Position)))"
     " * smoothstep(User.SpaceDustNearFadeStartCm, User.SpaceDustNearFadeEndCm,"
     " length(Particles.Position - User.SpaceDustCameraLocalCm))"),
]
# Update step 3: the colour and its alpha, and the sprite size; a fully faded particle gets a zero sprite size so it
# costs no raster work. The size is computed here in full: an input of a SetParameters module that reads the attribute
# the same module writes (Particles.SpriteSize = f(Particles.SpriteSize)) reads zero, not the earlier module's value
UPDATE_COLOR = [
    ("Particles.Color", T_COLOR,
     "float4(User.SpaceDustColor.r, User.SpaceDustColor.g, User.SpaceDustColor.b,"
     " User.SpaceDustOpacity * User.SpaceDustStreakAlpha * Particles.DustFade)"),
    ("Particles.SpriteSize", T_VEC2,
     "Particles.DustFade > 0.0"
     " ? float2(User.SpaceDustPointSizeCm * Particles.DustSizeFactor,"
     " max(User.SpaceDustPointSizeCm * Particles.DustSizeFactor, User.SpaceDustStreakLengthCm))"
     " : float2(0.0, 0.0)"),
]


# Adds a SetParameters module whose entries are HLSL expressions, at the end of a script stack
def add_set_parameters(system_path, script, entries):
    # The entries are declared with a zero default, then each input is switched to its HLSL expression
    parameters = [{"variable": {"name": name, "type": niagara_type(struct_path)}, "defaultValue": ZERO[struct_path]}
                  for name, struct_path, _ in entries]
    module = tool("AddSetParametersModule", {"moduleLocationRef": ref(system_path, EMITTER_NAME, script),
                                             "parameters": parameters})
    for name, _, expression in entries:
        set_input(system_path, EMITTER_NAME, script, module["moduleName"], name, hlsl(expression))
    return module


# Sets one module input
def set_input(system_path, emitter, script, module, input_name, input_value):
    tool("SetStackInputData", {"stackInputRef": ref(system_path, emitter, script, module, -1, [input_name]),
                               "inputData": input_value})


# Returns the current value payload of one module input
def input_value(system_path, emitter, script, module, input_name):
    values = tool("GetModuleInputValues", {"moduleRef": ref(system_path, emitter, script, module)})
    for entry in values["inputs"]:
        if entry["name"] == input_name:
            return entry["value"].get("value")
    raise RuntimeError("create_space_dust_assets: no input '%s' on %s" % (input_name, module))


# Merges property values into the emitter (GPU sim, local space, fixed bounds of +-L/2)
def configure_emitter(system_path):
    half = 0.5 * constant("BOX_SIZE_CM")
    data = {"bLocalSpace": True, "SimTarget": "GPUComputeSim", "CalculateBoundsMode": "Fixed",
            "FixedBounds": {"min": {"x": -half, "y": -half, "z": -half}, "max": {"x": half, "y": half, "z": half},
                            "isValid": True},
            "bDeterminism": False}
    tool("SetEmitterData",
         {"emitter": ref(system_path, EMITTER_NAME), "emitterData": {"propertyValues": json.dumps(data)}})


# Sets the sprite renderer: the dust material, velocity aligned, facing the camera position, no sorting, no motion
# vectors, no shadows
def configure_renderer(system_path, material_path):
    data = {"Material": {"refPath": material_path}, "Alignment": "VelocityAligned", "FacingMode": "FaceCameraPosition",
            "SortMode": "None", "bCastShadows": False, "MotionVectorSetting": "Disable",
            "bEnableCameraDistanceCulling": False, "bIncludeInHitProxy": False}
    tool("SetRendererData", {"renderer": ref(system_path, EMITTER_NAME, "", "", 0),
                             "rendererData": {"propertyValues": json.dumps(data)}})


# Returns the module names of one script stack of the dust emitter
def module_names(system_path, script):
    topology = tool("GetScriptStackTopology", {"scriptRef": ref(system_path, EMITTER_NAME, script)})
    return [m["moduleName"] for m in topology["modules"]]


# Empties the system for a rebuild in place: removes its emitters and user parameters
def clear_system(system_path):
    summary = tool("GetSystemSummary", {"system": {"refPath": system_path}})
    for emitter in summary.get("emitters", []):
        tool("RemoveEmitter", {"emitterToRemove": ref(system_path, emitter["emitterName"])})
    variables = [{"name": v["name"], "type": v["type"]} for v in summary.get("userVariables", [])]
    if variables:
        tool("RemoveUserVariables", {"system": {"refPath": system_path}, "variablesToRemove": variables})


# Builds the emitter and user parameters into a system that has none
def build_system(system_path, material_path):
    # System: infinite loop, or the system goes inactive after its first loop (SDD 6 Amendment 11). The template
    # already loops forever; the value is only written when it differs (an unchanged write is refused as hidden)
    infinite = enum("/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions",
                    "ENiagara_EmitterStateOptions::NewEnumerator0", "Infinite")
    if input_value(system_path, "", "SystemUpdateScript", "SystemState", "Loop Behavior") != infinite["value"]:
        set_input(system_path, "", "SystemUpdateScript", "SystemState", "Loop Behavior", infinite)
    tool("AddUserVariables", {"system": {"refPath": system_path}, "variablesToAdd": [
        {"name": "User." + name, "type": niagara_type(struct_path), "defaultValue": default, "description": text}
        for name, struct_path, default, text in user_parameters()]})

    tool("AddEmitter", {"system": {"refPath": system_path}, "templateEmitter": {"refPath": EMITTER_TEMPLATE},
                        "emitterName": EMITTER_NAME})
    configure_emitter(system_path)

    # Emitter update: the emitter follows the system's life cycle (the template's Life Cycle Mode = System, so it
    # inherits the infinite loop), plus a one-shot burst of User.SpaceDustParticleCount
    life_cycle = input_value(system_path, EMITTER_NAME, "EmitterUpdateScript", "EmitterState", "Life Cycle Mode")
    if life_cycle.get("displayName") != "System":
        set_input(system_path, EMITTER_NAME, "EmitterUpdateScript", "EmitterState", "Life Cycle Mode",
                  enum("/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode",
                       "ENiagaraEmitterLifeCycleMode::NewEnumerator0", "System"))
    tool("AddModule", {"moduleLocationRef": ref(system_path, EMITTER_NAME, "EmitterUpdateScript"),
                       "moduleAsset": {"refPath": SPAWN_BURST_MODULE}})
    set_input(system_path, EMITTER_NAME, "EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count",
              linked("User.SpaceDustParticleCount", T_INT))
    # Only the first loop bursts (the emitter loops every second, forever)
    set_input(system_path, EMITTER_NAME, "EmitterUpdateScript", "SpawnBurst_Instantaneous", "UseLoopCountLimit",
              value(T_BOOL, {"value": NIAGARA_TRUE}))
    set_input(system_path, EMITTER_NAME, "EmitterUpdateScript", "SpawnBurst_Instantaneous", "Loop Count Limit",
              value(T_INT, {"value": 1}))

    # Particle spawn: the brightness seed (Particles.MaterialRandom) from InitializeParticle, then the anchor and size
    set_input(system_path, EMITTER_NAME, "ParticleSpawnScript", "InitializeParticle", "Write Material Random",
              value(T_BOOL, {"value": NIAGARA_TRUE}))
    add_set_parameters(system_path, "ParticleSpawnScript", spawn_entries())

    # Particle update: never kill, then place, fade and colour
    set_input(system_path, EMITTER_NAME, "ParticleUpdateScript", "ParticleState",
              "Kill Particles When Lifetime Has Elapsed", value(T_BOOL, {"value": 0}))
    for entries in (UPDATE_PLACE, UPDATE_FADE, UPDATE_COLOR):
        add_set_parameters(system_path, "ParticleUpdateScript", entries)

    configure_renderer(system_path, material_path)

    # Toggling a module forces a real recompile, so no stale compiled renderer is saved (SDD 6 Amendment 15)
    tool("SetModuleEnabled", {"moduleRef": ref(system_path, EMITTER_NAME, "ParticleUpdateScript", "ParticleState"),
                              "bEnabled": False})
    tool("SetModuleEnabled", {"moduleRef": ref(system_path, EMITTER_NAME, "ParticleUpdateScript", "ParticleState"),
                              "bEnabled": True})


# Creates the Niagara system when it does not exist (a rebuild has deleted it first)
def ensure_system(material):
    asset_path = DUST_DIR + "/" + SYSTEM_NAME
    system_path = asset_path + "." + SYSTEM_NAME
    if not unreal.ToolsetRegistry.is_available():
        unreal.log_warning("create_space_dust_assets: the toolset registry needs a running editor; %s skipped"
                           % asset_path)
        return
    if EAL.does_asset_exist(asset_path):
        unreal.log("create_space_dust_assets: %s exists (set SOL_REBUILD to rebuild)" % asset_path)
        return
    tool("CreateNiagaraSystem", {"assetName": SYSTEM_NAME, "assetPath": DUST_DIR,
                                 "templateSystem": {"refPath": SYSTEM_TEMPLATE}})
    clear_system(system_path)
    build_system(system_path, material.get_path_name())
    # The compile-state tool waits for the compile; saving also waits for any compile still in flight
    result = unreal.ToolsetRegistry.execute_tool(TOOLSET, "GetSystemCompileState",
                                                 json.dumps({"system": {"refPath": system_path}}))
    if result.is_complete and not result.error:
        unreal.log("create_space_dust_assets: compile state %s" % result.get_value_as_json_string()[:2000])
    EAL.save_loaded_asset(EAL.load_asset(asset_path))
    unreal.log("create_space_dust_assets: built %s" % asset_path)


# Refuses to run during Play In Editor: the editor asset library then reports every asset as missing, so the script
# would try to create the assets again (a modal "Overwrite Existing Object" prompt) and could not save them
def check_not_playing():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor is not None and editor.get_game_world() is not None:
        raise RuntimeError("create_space_dust_assets: stop Play In Editor first")


# Rebuild in a running editor: deletes the system, then the material it references, so both are created fresh
def delete_for_rebuild():
    if not REBUILD or not unreal.ToolsetRegistry.is_available():
        return
    for name in (SYSTEM_NAME, MATERIAL_NAME):
        path = DUST_DIR + "/" + name
        if EAL.does_asset_exist(path) and not EAL.delete_asset(path):
            raise RuntimeError("create_space_dust_assets: could not delete %s for the rebuild" % path)


check_not_playing()
delete_for_rebuild()
ensure_system(ensure_material())
