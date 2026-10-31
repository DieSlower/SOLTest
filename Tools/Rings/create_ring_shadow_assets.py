# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates (or, with SOL_REBUILD, rebuilds in place) the ring rock material and its shadow parameter collection (issue
# #16, SDD 16 Amendments 2 and 4), headless and idempotent:
#   /Game/SOL/Rings/MPC_SOLRingShadow  vector parameters written once per frame by ASOLRingVisuals::
#                                      UpdateShadowParameters (SOL::RingShadowMpcParams): RingShadowState =
#                                      (planet shadow at the viewpoint, tau/|mu| there, Sun side +-1, layer sigma cm),
#                                      RingShadowPlane = (ring normal in Unreal axes, viewpoint height cm),
#                                      RingShadowOrigin = (viewpoint in render space, cm). Defaults = no shadow.
#   /Game/SOL/Rings/M_SOLRingRock      opaque, default-lit rock material shared by the dense Niagara rock layer and the
#                                      near-field Mass ISM: base colour = ParticleColor x RockTint x lerp(0.55, 1.1,
#                                      ParticleRandom) x the ring shadow (planet shadow x the Gaussian layer's statistical
#                                      self-shadow, SOLRS_LayerFractionTowardSun); emissive = 5% of the unshadowed colour
#                                      as fake ambient. RockTint (default white) is set by the ISM's material instance to
#                                      the ring colour, since ParticleColor is white outside Niagara.
# The parameter names must match SOL::RingShadowMpcParams and SOL::RingRockMaterialParams in
# Source/SOLTest/SOLConstants.h, and the asset paths SOL::Paths::RING_SHADOW_PARAMETER_COLLECTION /
# RING_ROCK_MATERIAL. M_SOLRingFar is not built by a script (see the project docs, Known tech debt).
# Run (editor closed):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
#       -script="<abs path>/Tools/Rings/create_ring_shadow_assets.py" -unattended [-SOLRebuild]
# or, in an open editor, exec() this file with SOL_REBUILD = True defined in the calling scope's globals.
import unreal

RING_DIR = "/Game/SOL/Rings"
MPC_NAME = "MPC_SOLRingShadow"
ROCK_NAME = "M_SOLRingRock"
REBUILD = ("-solrebuild" in unreal.SystemLibrary.get_command_line().lower()) or bool(globals().get("SOL_REBUILD"))

RING_SHADOW_INCLUDE = "/Project/SOLTest/Private/SOLRingShadow.ush"   # SOL::SHADER_VIRTUAL_DIRECTORY + file
MPC_DEFAULTS = [
    ("RingShadowState", unreal.LinearColor(1.0, 0.0, 1.0, 0.0)),
    ("RingShadowPlane", unreal.LinearColor(0.0, 0.0, 1.0, 0.0)),
    ("RingShadowOrigin", unreal.LinearColor(0.0, 0.0, 0.0, 0.0)),
]
ROCK_TINT_PARAM = "RockTint"
ROCK_BRIGHTNESS_RANGE = (0.55, 1.1)   # per-rock brightness spread from ParticleRandom
ROCK_ROUGHNESS = 0.88
ROCK_SPECULAR = 0.3
ROCK_AMBIENT = 0.05                    # emissive fraction of the unshadowed colour (fake ambient)

ROCK_SHADOW_HLSL = r"""// Ring shadow on a rock near the viewpoint (SDD 16): planet shadow at the viewpoint (State.x) times the ring
// layer's statistical self-shadow, exp(-tau/|mu| * F), F = the fraction of the Gaussian layer (1-sigma State.w cm)
// between this pixel and the Sun, from its height above the ring plane (Plane.w at the viewpoint + offset along the
// normal)
float zs = (Plane.w + dot(Rel, Plane.xyz)) * State.z;
return State.x * exp(-State.y * SOLRS_LayerFractionTowardSun(zs, State.w));"""

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


# Returns the parameter collection, creating it, and adding any missing parameter with its default (existing
# parameters keep their GUIDs, so materials that reference them stay valid)
def ensure_collection():
    path = RING_DIR + "/" + MPC_NAME
    mpc = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if mpc is None:
        mpc = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MPC_NAME, RING_DIR,
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


# Builds the rock material graph into an empty material
def build_rock(material, mpc):
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    MEL.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
    MEL.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)

    # Rock colour: particle colour (Niagara) x tint (ISM) x a per-rock brightness
    particle_color = add(material, unreal.MaterialExpressionParticleColor, -1000, 0)
    tint = add(material, unreal.MaterialExpressionVectorParameter, -1000, 120)
    tint.set_editor_property("parameter_name", ROCK_TINT_PARAM)
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tinted = add(material, unreal.MaterialExpressionMultiply, -800, 40)
    MEL.connect_material_expressions(particle_color, "RGB", tinted, "A")
    MEL.connect_material_expressions(tint, "RGB", tinted, "B")
    random = add(material, unreal.MaterialExpressionParticleRandom, -1000, 260)
    low = add(material, unreal.MaterialExpressionConstant, -1000, 340)
    low.set_editor_property("r", ROCK_BRIGHTNESS_RANGE[0])
    high = add(material, unreal.MaterialExpressionConstant, -1000, 420)
    high.set_editor_property("r", ROCK_BRIGHTNESS_RANGE[1])
    brightness = add(material, unreal.MaterialExpressionLinearInterpolate, -800, 300)
    MEL.connect_material_expressions(low, "", brightness, "A")
    MEL.connect_material_expressions(high, "", brightness, "B")
    MEL.connect_material_expressions(random, "", brightness, "Alpha")
    color = add(material, unreal.MaterialExpressionMultiply, -600, 100)
    MEL.connect_material_expressions(tinted, "", color, "A")
    MEL.connect_material_expressions(brightness, "", color, "B")

    # Shadow inputs from the collection, and this pixel's offset from the viewpoint
    params = {}
    for index, (name, _) in enumerate(MPC_DEFAULTS):
        node = add(material, unreal.MaterialExpressionCollectionParameter, -1100, 560 + 100 * index)
        node.set_editor_property("collection", mpc)
        node.set_editor_property("parameter_name", name)
        params[name] = node
    origin = add(material, unreal.MaterialExpressionComponentMask, -900, 760)
    for channel, on in zip(("r", "g", "b", "a"), (True, True, True, False)):
        origin.set_editor_property(channel, on)
    MEL.connect_material_expressions(params["RingShadowOrigin"], "", origin, "")
    world = add(material, unreal.MaterialExpressionWorldPosition, -1100, 880)
    rel = add(material, unreal.MaterialExpressionSubtract, -750, 820)
    MEL.connect_material_expressions(world, "", rel, "A")
    MEL.connect_material_expressions(origin, "", rel, "B")

    shadow = add(material, unreal.MaterialExpressionCustom, -500, 600)
    shadow.set_editor_property("description", "SOLRingRockShadow")
    shadow.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    shadow.set_editor_property("code", ROCK_SHADOW_HLSL)
    shadow.set_editor_property("include_file_paths", [RING_SHADOW_INCLUDE])
    inputs = []
    for name in ("State", "Plane", "Rel"):
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    shadow.set_editor_property("inputs", inputs)
    MEL.connect_material_expressions(params["RingShadowState"], "", shadow, "State")
    MEL.connect_material_expressions(params["RingShadowPlane"], "", shadow, "Plane")
    MEL.connect_material_expressions(rel, "", shadow, "Rel")

    # Outputs: shadowed base colour, unshadowed fake ambient, constant roughness and specular
    base = add(material, unreal.MaterialExpressionMultiply, -300, 200)
    MEL.connect_material_expressions(color, "", base, "A")
    MEL.connect_material_expressions(shadow, "", base, "B")
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    ambient_scale = add(material, unreal.MaterialExpressionConstant, -500, 420)
    ambient_scale.set_editor_property("r", ROCK_AMBIENT)
    ambient = add(material, unreal.MaterialExpressionMultiply, -300, 400)
    MEL.connect_material_expressions(color, "", ambient, "A")
    MEL.connect_material_expressions(ambient_scale, "", ambient, "B")
    MEL.connect_material_property(ambient, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    roughness = add(material, unreal.MaterialExpressionConstant, -300, 500)
    roughness.set_editor_property("r", ROCK_ROUGHNESS)
    MEL.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    specular = add(material, unreal.MaterialExpressionConstant, -300, 560)
    specular.set_editor_property("r", ROCK_SPECULAR)
    MEL.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)


# Creates the rock material, or rebuilds its graph in place (keeping the asset, so the Niagara renderer's reference
# stays valid) when SOL_REBUILD is set
def ensure_rock(mpc):
    path = RING_DIR + "/" + ROCK_NAME
    if EAL.does_asset_exist(path):
        if not REBUILD:
            unreal.log("create_ring_shadow_assets: %s exists (pass -SOLRebuild to rebuild)" % path)
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
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(ROCK_NAME, RING_DIR, unreal.Material,
            unreal.MaterialFactoryNew())
    build_rock(material, mpc)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    unreal.log("create_ring_shadow_assets: built %s" % path)


ensure_rock(ensure_collection())
