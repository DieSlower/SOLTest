/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLThrusterMath.h"
#include "Visuals/SOLThrusterMirror.h"
#include "SOLConstants.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraSystem.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Part 51b (SDD 51 sections 4.3, 4.4 and 4.11, contract case 12): the thruster shader's float path
// (Shaders/Private/SOLThruster.ush, copied statement for statement in Visuals/SOLThrusterMirror.h) against the double
// SOLThruster functions it mirrors, on the vectors the materials hold (SOLThrusterMirror::MaterialVectors), and the
// generated assets of Tools/Fx/create_thruster_assets.py (their parameter defaults must equal the packed constants, so
// a constant changed without re-running the script fails here). The GPU output itself is checked by eye in 51c-51f.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double THRMIRROR_TOL = 1.0e-5;            // Float path vs the double reference (colours, b)
    constexpr double THRMIRROR_LEN_TOL = 1.0e-4;        // Knot positions, metres
    constexpr float THRMIRROR_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float THRMIRROR_INF = std::numeric_limits<float>::infinity();

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two colours agree in RGB within the mirror tolerance
    bool ThrMirrorColourNear(const FLinearColor& a, const FLinearColor& b)
    {
        return FMath::Abs(a.R - b.R) <= THRMIRROR_TOL && FMath::Abs(a.G - b.G) <= THRMIRROR_TOL
            && FMath::Abs(a.B - b.B) <= THRMIRROR_TOL;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a vector equals (x, y, z, w) exactly as floats of the given constants
    bool ThrMirrorPacked(const FVector4f& v, const double x, const double y, const double z, const double w)
    {
        return v.X == static_cast<float>(x) && v.Y == static_cast<float>(y) && v.Z == static_cast<float>(z)
            && v.W == static_cast<float>(w);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a material's default for a vector parameter, or NaN when the parameter is missing
    FLinearColor ThrMirrorVectorDefault(const UMaterialInterface& material, const TCHAR* name)
    {
        FLinearColor value(THRMIRROR_NAN, THRMIRROR_NAN, THRMIRROR_NAN, THRMIRROR_NAN);
        if (!material.GetVectorParameterValue(FHashedMaterialParameterInfo(FName(name)), value))
        {
            return FLinearColor(THRMIRROR_NAN, THRMIRROR_NAN, THRMIRROR_NAN, THRMIRROR_NAN);
        }
        return value;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a material's vector default equals a packed vector (float compare, as the script writes it)
    bool ThrMirrorDefaultEquals(const UMaterialInterface& material, const TCHAR* name, const FVector4f& packed)
    {
        const FLinearColor value = ThrMirrorVectorDefault(material, name);
        return FMath::IsNearlyEqual(value.R, packed.X, 1.0e-6f) && FMath::IsNearlyEqual(value.G, packed.Y, 1.0e-6f)
            && FMath::IsNearlyEqual(value.B, packed.Z, 1.0e-6f) && FMath::IsNearlyEqual(value.A, packed.W, 1.0e-6f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a material's colour default equals a packed colour in RGB
    bool ThrMirrorColourDefaultEquals(const UMaterialInterface& material, const TCHAR* name, const FLinearColor& c)
    {
        const FLinearColor value = ThrMirrorVectorDefault(material, name);
        return FMath::IsNearlyEqual(value.R, c.R, 1.0e-6f) && FMath::IsNearlyEqual(value.G, c.G, 1.0e-6f)
            && FMath::IsNearlyEqual(value.B, c.B, 1.0e-6f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a static mesh has a material slot of the given name
    bool ThrMirrorHasSlot(const UStaticMesh& mesh, const TCHAR* name)
    {
        for (const FStaticMaterial& slot : mesh.GetStaticMaterials())
        {
            if (slot.MaterialSlotName == FName(name))
            {
                return true;
            }
        }
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterMaterialMirrorTest, "SOLTest.Thruster.MaterialMirror",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contract case 12: the shader's float mirror of PlumeColour, PlumeBrightness and ShockKnots equals the double C++ at
// c = 0, 0.175, 0.35, 0.675, 1 and G = 1, 3, G_REF (on the NPC and player nozzles), on the packed parameter vectors
bool FSOLThrusterMaterialMirrorTest::RunTest(const FString& /*parameters*/)
{
    namespace T = SOL::Thruster;
    const FSOLThrusterMaterialVectors v = SOLThrusterMirror::MaterialVectors();

    // Packing: every vector carries its SOL::Thruster constants
    TestTrue(TEXT("ramp keys packed"), v.Rim[0] == T::RIM_BLUE && v.Rim[1] == T::RIM_YELLOW && v.Rim[2] == T::RIM_RED
        && v.Core[0] == T::CORE_BLUE && v.Core[1] == T::CORE_YELLOW && v.Core[2] == T::CORE_RED);
    TestTrue(TEXT("Ramp = (yellow c, 0, 0, 0)"), ThrMirrorPacked(v.Ramp, T::COLOUR_YELLOW_C, 0.0, 0.0, 0.0));
    TestTrue(TEXT("Shock = (factor, gain, decay, zone)"), ThrMirrorPacked(v.Shock, T::SHOCK_SPACING_FACTOR,
        T::SHOCK_BRIGHTNESS_GAIN, T::SHOCK_DECAY, T::SHOCK_ZONE));
    TestTrue(TEXT("Shape = (max knots, tail ratio, knot half length, half width)"), ThrMirrorPacked(v.Shape,
        T::SHOCK_MAX_KNOTS, T::TAIL_RADIUS_RATIO, T::SHOCK_KNOT_HALF_LENGTH, T::SHOCK_KNOT_HALF_WIDTH));
    TestTrue(TEXT("Fade = (low px, high px, reference height, 0)"), ThrMirrorPacked(v.Fade, T::CONE_FADE_LOW_PX,
        T::CONE_FADE_HIGH_PX, SOL::NPC_LANE_REFERENCE_HEIGHT_PX, 0.0));
    TestTrue(TEXT("Glow = (diameter radii, min drawn px, emissive, reference height)"), ThrMirrorPacked(v.Glow,
        T::GLOW_DIAMETER_NOZZLE_RADII, T::GLOW_MIN_DRAWN_PX, T::GLOW_EMISSIVE, SOL::NPC_LANE_REFERENCE_HEIGHT_PX));
    TestTrue(TEXT("Burst = (max opacity 0.25, flicker Hz, depth, emissive)"), ThrMirrorPacked(v.Burst,
        T::BURST_MAX_OPACITY, T::BURST_FLICKER_HZ, T::BURST_FLICKER_DEPTH, T::BURST_EMISSIVE));

    // PlumeColour at the contract's c points (and the clamp, NaN as 0)
    for (const float c : { 0.0f, 0.175f, 0.35f, 0.675f, 1.0f, -0.5f, 1.5f, THRMIRROR_NAN })
    {
        FLinearColor rim;
        FLinearColor core;
        SOLThrusterMirror::PlumeColour(c, v.Ramp, v.Rim, v.Core, rim, core);
        const FSOLPlumeColour reference = SOLThruster::PlumeColour(static_cast<double>(c));
        TestTrue(FString::Printf(TEXT("PlumeColour(%g) rim"), c), ThrMirrorColourNear(rim, reference.Rim));
        TestTrue(FString::Printf(TEXT("PlumeColour(%g) core"), c), ThrMirrorColourNear(core, reference.Core));
    }

    // PlumeBrightness at G = 1, 3, G_REF (and off, clamped, NaN, infinite)
    const float gRef = static_cast<float>(T::G_REF);
    for (const float g : { 1.0f, 3.0f, gRef, 0.009f, 0.02f, 9.0f, -1.0f, THRMIRROR_NAN, THRMIRROR_INF })
    {
        const float mirror = SOLThrusterMirror::PlumeBrightness(g, gRef, static_cast<float>(T::PLUME_MIN_G),
            static_cast<float>(T::BRIGHTNESS_EXPONENT));
        TestNearlyEqual(FString::Printf(TEXT("PlumeBrightness(%g)"), g), static_cast<double>(mirror),
            SOLThruster::PlumeBrightness(static_cast<double>(g)), THRMIRROR_TOL);
    }

    // ShockKnots at G = 1, 3, G_REF on the player nozzle (20 m, r 0.8) and G = 1, 1.65, 3 on the NPC one (30 m, r 1.5):
    // count, positions, strengths; the rhombus peaks at each knot centre on the axis with the knot's strength and is dark
    // between knots. The NPC at G_REF is left out on purpose: its fifth knot lies exactly on the zone edge (5 x 4.8 m =
    // 0.8 x 30 m), where the double reference rounds it out and the shader's float keeps it, a one-knot difference at a
    // brightness of 0.45 that no player can see
    struct FKnotCase { double HullM; double RadiusM; double G; };
    const FKnotCase cases[] = { { 30.0, 1.5, 1.0 }, { 30.0, 1.5, 1.65 }, { 30.0, 1.5, 3.0 },
        { 20.0, 0.8, 1.0 }, { 20.0, 0.8, 3.0 }, { 20.0, 0.8, T::G_REF }, { 30.0, 1.5, 0.0 } };
    for (const FKnotCase& knotCase : cases)
    {
        const double lengthM = SOLThruster::PlumeLengthM(knotCase.HullM, knotCase.G);
        const double b = SOLThruster::PlumeBrightness(knotCase.G);
        FSOLShockKnot knots[T::SHOCK_MAX_KNOTS];
        const int32 count = SOLThruster::ShockKnots(lengthM, knotCase.RadiusM, b, knots);
        const float spacing = SOLThrusterMirror::ShockSpacing(static_cast<float>(knotCase.RadiusM),
            static_cast<float>(b), v.Shock);
        const int32 mirrorCount = SOLThrusterMirror::ShockKnotCount(static_cast<float>(lengthM), spacing, v.Shock,
            v.Shape);
        const FString label = FString::Printf(TEXT("hull %g m, r %g, G %g"), knotCase.HullM, knotCase.RadiusM,
            knotCase.G);
        TestNearlyEqual(label + TEXT(": spacing"), static_cast<double>(spacing),
            SOLThruster::ShockSpacingM(knotCase.RadiusM, b), THRMIRROR_LEN_TOL);
        TestEqual(label + TEXT(": knot count"), mirrorCount, count);
        for (int32 k = 0; k < FMath::Min(count, mirrorCount); ++k)
        {
            float positionM;
            float strength;
            SOLThrusterMirror::ShockKnot(k + 1, spacing, v.Shock, positionM, strength);
            TestNearlyEqual(label + FString::Printf(TEXT(": knot %d position"), k + 1), static_cast<double>(positionM),
                knots[k].PositionM, THRMIRROR_LEN_TOL);
            TestNearlyEqual(label + FString::Printf(TEXT(": knot %d strength"), k + 1), static_cast<double>(strength),
                knots[k].Strength, THRMIRROR_TOL);
            TestNearlyEqual(label + FString::Printf(TEXT(": knot %d centre brightness"), k + 1),
                static_cast<double>(SOLThrusterMirror::ShockIntensity(positionM, 0.0f, spacing, mirrorCount, v.Shock,
                v.Shape)), knots[k].Strength, THRMIRROR_TOL);
            TestEqual(label + FString::Printf(TEXT(": dark half a spacing past knot %d"), k + 1),
                SOLThrusterMirror::ShockIntensity(positionM + 0.5f * spacing, 0.0f, spacing, mirrorCount, v.Shock,
                v.Shape), 0.0f);
        }
    }
    TestEqual(TEXT("no knots on a plume of length 0"), SOLThrusterMirror::ShockKnotCount(0.0f, 2.0f, v.Shock, v.Shape),
        0);
    TestEqual(TEXT("no diamond off the rhombus (radial fraction at the half width)"),
        SOLThrusterMirror::ShockIntensity(2.56f, static_cast<float>(T::SHOCK_KNOT_HALF_WIDTH), 2.56f, 6, v.Shock,
        v.Shape), 0.0f);

    // The pixel fade and the focal length match the C++ pixel rule (FocalRefPx 960 at 90 degrees, 1920 x 1080)
    const float projection = static_cast<float>(1.0 / FMath::Tan(FMath::DegreesToRadians(45.0)));
    TestNearlyEqual(TEXT("focal length 960 px"), static_cast<double>(SOLThrusterMirror::FocalRefPx(projection,
        FVector2f(1920.0f, 1080.0f), v.Fade.Z)), SOLThruster::FocalRefPx(90.0, 16.0 / 9.0), 1.0e-2);
    for (const float px : { 0.4f, 0.5f, 0.75f, 1.0f, 3.0f })
    {
        TestNearlyEqual(FString::Printf(TEXT("cone fade at %g px"), px),
            static_cast<double>(SOLThrusterMirror::LodAlpha(px, v.Fade.X, v.Fade.Y)),
            SOLThruster::LodAlpha(px, T::CONE_FADE_LOW_PX, T::CONE_FADE_HIGH_PX), THRMIRROR_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterMaterialAssetsTest, "SOLTest.Thruster.MaterialAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Part 51b (SDD 51 section 4.11): the script-built assets load with the agreed blend modes and usages, their vector
// defaults equal SOLThrusterMirror::MaterialVectors, the meshes carry the named thruster slots and bounds that cover a
// full-length plume, and the ribbon system exposes the SOL::ThrusterTrailParams user parameters
bool FSOLThrusterMaterialAssetsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::ThrusterMaterialParams;
    const FSOLThrusterMaterialVectors v = SOLThrusterMirror::MaterialVectors();

    // Plume: unlit, additive, two-sided, instanced, the ramp and look defaults
    const UMaterialInterface* plume = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::THRUSTER_PLUME_MATERIAL);
    if (TestNotNull(TEXT("M_SOLThrusterPlume loads"), plume))
    {
        TestTrue(TEXT("plume: additive"), plume->GetBlendMode() == BLEND_Additive);
        TestTrue(TEXT("plume: unlit"), plume->GetShadingModels().IsUnlit());
        TestTrue(TEXT("plume: two-sided"), plume->IsTwoSided());
        TestTrue(TEXT("plume: used with instanced static meshes"),
            plume->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
        const TCHAR* rims[] = { RIM_0, RIM_1, RIM_2 };
        const TCHAR* cores[] = { CORE_0, CORE_1, CORE_2 };
        for (int32 key = 0; key < 3; ++key)
        {
            TestTrue(FString::Printf(TEXT("plume: %s default"), rims[key]),
                ThrMirrorColourDefaultEquals(*plume, rims[key], v.Rim[key]));
            TestTrue(FString::Printf(TEXT("plume: %s default"), cores[key]),
                ThrMirrorColourDefaultEquals(*plume, cores[key], v.Core[key]));
        }
        TestTrue(TEXT("plume: ThrusterRamp default"), ThrMirrorDefaultEquals(*plume, RAMP, v.Ramp));
        TestTrue(TEXT("plume: ThrusterShock default"), ThrMirrorDefaultEquals(*plume, SHOCK, v.Shock));
        TestTrue(TEXT("plume: ThrusterShape default"), ThrMirrorDefaultEquals(*plume, SHAPE, v.Shape));
        TestTrue(TEXT("plume: ThrusterLook default"), ThrMirrorDefaultEquals(*plume, LOOK, v.Look));
        TestTrue(TEXT("plume: ThrusterStreak default"), ThrMirrorDefaultEquals(*plume, STREAK, v.Streak));
        TestTrue(TEXT("plume: ThrusterFade default"), ThrMirrorDefaultEquals(*plume, FADE, v.Fade));
    }

    // Glow: additive, the ramp and glow defaults
    const UMaterialInterface* glow = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::THRUSTER_GLOW_MATERIAL);
    if (TestNotNull(TEXT("M_SOLThrusterGlow loads"), glow))
    {
        TestTrue(TEXT("glow: additive"), glow->GetBlendMode() == BLEND_Additive);
        TestTrue(TEXT("glow: unlit"), glow->GetShadingModels().IsUnlit());
        TestTrue(TEXT("glow: ThrusterRim0 default"), ThrMirrorColourDefaultEquals(*glow, RIM_0, v.Rim[0]));
        TestTrue(TEXT("glow: ThrusterCore2 default"), ThrMirrorColourDefaultEquals(*glow, CORE_2, v.Core[2]));
        TestTrue(TEXT("glow: ThrusterGlow default"), ThrMirrorDefaultEquals(*glow, GLOW, v.Glow));
        TestTrue(TEXT("glow: ThrusterGlowFade default"), ThrMirrorDefaultEquals(*glow, GLOW_FADE, v.GlowFade));
    }

    // Burst: translucent (not hot additive), the burst defaults
    const UMaterialInterface* burst = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::THRUSTER_BURST_MATERIAL);
    if (TestNotNull(TEXT("M_SOLThrusterBurst loads"), burst))
    {
        TestTrue(TEXT("burst: translucent, not additive (Q7)"), burst->GetBlendMode() == BLEND_Translucent);
        TestTrue(TEXT("burst: unlit"), burst->GetShadingModels().IsUnlit());
        TestTrue(TEXT("burst: ThrusterBurst default"), ThrMirrorDefaultEquals(*burst, BURST, v.Burst));
        TestTrue(TEXT("burst: ThrusterBurstShape default"), ThrMirrorDefaultEquals(*burst, BURST_SHAPE,
            v.BurstShape));
        TestTrue(TEXT("burst: ThrusterBurstColour default"), ThrMirrorColourDefaultEquals(*burst, BURST_COLOUR,
            v.BurstColour));
    }

    // Trail: additive, Niagara ribbon usage
    const UMaterialInterface* trail = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::THRUSTER_TRAIL_MATERIAL);
    if (TestNotNull(TEXT("M_SOLThrusterTrail loads"), trail))
    {
        TestTrue(TEXT("trail: additive"), trail->GetBlendMode() == BLEND_Additive);
        TestTrue(TEXT("trail: used with Niagara ribbons"), trail->GetUsageByFlag(MATUSAGE_NiagaraRibbons));
        TestTrue(TEXT("trail: ThrusterTrail default"), ThrMirrorDefaultEquals(*trail, TRAIL, v.Trail));
    }
    TestNotNull(TEXT("T_SOLThrusterNoise loads"), LoadObject<UTexture2D>(nullptr, SOL::Paths::THRUSTER_NOISE_TEXTURE));

    // Meshes: the named slots, and bounds covering the full-length plume behind the nozzle (mesh local units: the cone
    // hull's length runs along local Z at 30 cm per unit, the box's along X at 30; the player mesh is in cm)
    const UStaticMesh* cone = LoadObject<UStaticMesh>(nullptr, SOL::Paths::THRUSTER_NPC_HULL_MESH_FACTION_0);
    const UStaticMesh* box = LoadObject<UStaticMesh>(nullptr, SOL::Paths::THRUSTER_NPC_HULL_MESH_FACTION_1);
    const UStaticMesh* player = LoadObject<UStaticMesh>(nullptr, SOL::Paths::THRUSTER_PLAYER_MESH);
    for (const UStaticMesh* mesh : { cone, box })
    {
        if (TestNotNull(TEXT("NPC thruster hull mesh loads"), mesh))
        {
            TestTrue(TEXT("NPC hull: slots Hull, Plume, Glow, Burst"), ThrMirrorHasSlot(*mesh, SLOT_HULL)
                && ThrMirrorHasSlot(*mesh, SLOT_PLUME) && ThrMirrorHasSlot(*mesh, SLOT_GLOW)
                && ThrMirrorHasSlot(*mesh, SLOT_BURST));
        }
    }
    const double plumeUnits = SOL::Thruster::NPC_HULL_LENGTH_M * SOL::METERS_TO_CM / 30.0;
    if (cone != nullptr)
    {
        TestTrue(TEXT("cone hull: bounds reach a full plume aft (local -Z)"),
            cone->GetBoundingBox().Min.Z <= -50.0 - plumeUnits + 1.0);
    }
    if (box != nullptr)
    {
        TestTrue(TEXT("box hull: bounds reach a full plume aft (local -X)"),
            box->GetBoundingBox().Min.X <= -50.0 - plumeUnits + 1.0);
    }
    if (TestNotNull(TEXT("SM_SOLPlayerThrusters loads"), player))
    {
        TestTrue(TEXT("player: slots Plume, Glow, Burst, no hull"), ThrMirrorHasSlot(*player, SLOT_PLUME)
            && ThrMirrorHasSlot(*player, SLOT_GLOW) && ThrMirrorHasSlot(*player, SLOT_BURST)
            && !ThrMirrorHasSlot(*player, SLOT_HULL));
        TestTrue(TEXT("player: bounds reach a full plume aft of the nozzles"), player->GetBoundingBox().Min.X
            <= (SOL::Thruster::PLAYER_NOZZLE_X_M - SOL::Thruster::PLAYER_HULL_LENGTH_M) * SOL::METERS_TO_CM + 1.0);
    }

    // Ribbon system: the array and count user parameters
    const UNiagaraSystem* trails = LoadObject<UNiagaraSystem>(nullptr, SOL::Paths::THRUSTER_TRAILS_NIAGARA_SYSTEM);
    if (TestNotNull(TEXT("NS_SOLThrusterTrails loads"), trails))
    {
        TArray<FNiagaraVariable> parameters;
        trails->GetExposedParameters().GetUserParameters(parameters);
        const TCHAR* expected[] = { SOL::ThrusterTrailParams::POSITIONS_CM, SOL::ThrusterTrailParams::COLOURS,
            SOL::ThrusterTrailParams::WIDTHS_CM, SOL::ThrusterTrailParams::POINT_COUNT,
            SOL::ThrusterTrailParams::MAX_POINTS, SOL::ThrusterTrailParams::POINTS_PER_TRAIL };
        for (const TCHAR* name : expected)
        {
            const FName userName(FString(TEXT("User.")) + name);
            const bool bFound = parameters.ContainsByPredicate([name, &userName](const FNiagaraVariable& variable)
            {
                return variable.GetName() == FName(name) || variable.GetName() == userName;
            });
            TestTrue(FString::Printf(TEXT("trails: user parameter %s"), name), bFound);
        }

        // One valid, compiled emitter with a ribbon renderer whose id is the int Particles.TrailRibbon
        TestTrue(TEXT("trails: system valid (scripts compiled)"), trails->IsValid());
        const TArray<FNiagaraEmitterHandle>& handles = trails->GetEmitterHandles();
        const FVersionedNiagaraEmitterData* emitter = handles.Num() == 1 ? handles[0].GetEmitterData() : nullptr;
        if (TestNotNull(TEXT("trails: one emitter"), emitter))
        {
            const UNiagaraRibbonRendererProperties* ribbon = emitter->GetRenderers().Num() == 1
                ? Cast<UNiagaraRibbonRendererProperties>(emitter->GetRenderers()[0]) : nullptr;
            if (TestNotNull(TEXT("trails: one ribbon renderer"), ribbon))
            {
                const FNiagaraVariableBase& ribbonId = ribbon->RibbonIdBinding.GetParamMapBindableVariable();
                TestEqual(TEXT("trails: ribbon id bound to Particles.TrailRibbon"), ribbonId.GetName(),
                    FName(TEXT("Particles.TrailRibbon")));
                TestTrue(TEXT("trails: ribbon id is an int (one ribbon per trail)"),
                    ribbonId.GetType() == FNiagaraTypeDefinition::GetIntDef());
                TestTrue(TEXT("trails: ribbon material M_SOLThrusterTrail"), ribbon->Material != nullptr
                    && ribbon->Material->GetPathName() == FString(SOL::Paths::THRUSTER_TRAIL_MATERIAL));
            }
        }
    }
    return true;
}

#endif
