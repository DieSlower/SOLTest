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
    // R10 deletes the cone's TAIL_RADIUS_RATIO, so Shape.Y is not asserted (its R10 content is not in the contract)
    TestTrue(TEXT("Shape = (max knots, -, knot half length, half width)"),
        v.Shape.X == static_cast<float>(T::SHOCK_MAX_KNOTS)
        && v.Shape.Z == static_cast<float>(T::SHOCK_KNOT_HALF_LENGTH)
        && v.Shape.W == static_cast<float>(T::SHOCK_KNOT_HALF_WIDTH));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameMirrorTest, "SOLTest.Thruster.FlameMirror",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contract case 12, extended for R10: the float mirror of SOLTH_FlamePixel's envelope terms (FlameCoreWeight, FlameKnot,
// FlameStreakDepth in SOLThrusterMirror.h) equals the double FlameCoreWeight, FlameKnotIntensity and FlameStreakDepth
// at the case 23 point values and over its sweep (s from 0 to L_f in L_f / 10000 steps, radial fraction 0 and 0.5, b
// 0.4925 and 1, depths 0.45 / 0.8), within 1e-5. One point is left out: at L_p 30, r_n 1.5, b 1 the fifth knot sits
// exactly on the zone edge (5 x 4.8 m = 0.8 x 30 m), where the double reference rounds it out and the float keeps it
// (as in MaterialMirror), so the sweep skips that knot's rhombus for that configuration only
bool FSOLThrusterFlameMirrorTest::RunTest(const FString& /*parameters*/)
{
    namespace T = SOL::Thruster;
    const FSOLThrusterMaterialVectors v = SOLThrusterMirror::MaterialVectors();
    TestTrue(TEXT("Shape.y = FLAME_STREAK_TAIL_STRENGTH"), v.Shape.Y == static_cast<float>(T::FLAME_STREAK_TAIL_STRENGTH));
    TestTrue(TEXT("Look = (emissive, core power, knot gain, streak strength)"), ThrMirrorPacked(v.Look,
        T::PLUME_EMISSIVE, T::PLUME_CORE_POWER, T::SHOCK_KNOT_GAIN, T::STREAK_STRENGTH));

    // Core weight point values and a sweep over r
    for (const float r : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, -0.5f, THRMIRROR_NAN })
    {
        TestNearlyEqual(FString::Printf(TEXT("FlameCoreWeight(%g)"), r),
            static_cast<double>(SOLThrusterMirror::FlameCoreWeight(r, v.Look)),
            SOLThruster::FlameCoreWeight(static_cast<double>(r)), THRMIRROR_TOL);
    }

    // The player at 3 G (case 23 point values)
    TestNearlyEqual(TEXT("knot (2.23749, 0) player 3 G"), static_cast<double>(SOLThrusterMirror::FlameKnot(2.23749f,
        0.0f, 10.388462f, 0.8f, 0.664050f, v.Shock, v.Shape)), 1.0, THRMIRROR_TOL);
    TestNearlyEqual(TEXT("knot (2.23749, 0.3) player 3 G"), static_cast<double>(SOLThrusterMirror::FlameKnot(2.23749f,
        0.3f, 10.388462f, 0.8f, 0.664050f, v.Shock, v.Shape)), 0.5, THRMIRROR_TOL);
    TestNearlyEqual(TEXT("streak depth (10, L_p 10)"), static_cast<double>(SOLThrusterMirror::FlameStreakDepth(10.0f,
        10.0f, 0.45f, 0.8f)), 0.625, THRMIRROR_TOL);
    TestNearlyEqual(TEXT("streak depth L_p 0 = body"), static_cast<double>(SOLThrusterMirror::FlameStreakDepth(5.0f,
        0.0f, 0.45f, 0.8f)), 0.45, THRMIRROR_TOL);

    // The case 23 sweep
    struct FFlame { double PlumeLengthM; double RadiusM; };
    const FFlame flames[] = { { 9.659011, 1.5 }, { 30.0, 1.5 }, { 10.388462, 0.8 }, { 20.0, 0.8 } };
    constexpr int32 steps = 10000;
    for (const FFlame& flame : flames)
    {
        const double flameLengthM = SOLThruster::FlameLengthM(flame.PlumeLengthM);
        for (const double b : { 0.4925, 1.0 })
        {
            // The zone-edge knot left out (see the header comment)
            const double spacingM = SOLThruster::ShockSpacingM(flame.RadiusM, b);
            const bool bEdgeKnot = flame.PlumeLengthM == 30.0 && flame.RadiusM == 1.5 && b == 1.0;
            const double edgeKnotM = 5.0 * spacingM;
            for (const double r : { 0.0, 0.5 })
            {
                int32 knotBad = 0;
                int32 streakBad = 0;
                for (int32 k = 0; k <= steps; ++k)
                {
                    const double arcM = flameLengthM * static_cast<double>(k) / steps;
                    if (!(bEdgeKnot && FMath::Abs(arcM - edgeKnotM) <= T::SHOCK_KNOT_HALF_LENGTH * spacingM + 1.0e-3))
                    {
                        const double knot = static_cast<double>(SOLThrusterMirror::FlameKnot(static_cast<float>(arcM),
                            static_cast<float>(r), static_cast<float>(flame.PlumeLengthM),
                            static_cast<float>(flame.RadiusM), static_cast<float>(b), v.Shock, v.Shape));
                        knotBad += FMath::Abs(knot - SOLThruster::FlameKnotIntensity(arcM, r, flame.PlumeLengthM,
                            flame.RadiusM, b)) <= THRMIRROR_TOL ? 0 : 1;
                    }
                    const double streak = static_cast<double>(SOLThrusterMirror::FlameStreakDepth(
                        static_cast<float>(arcM), static_cast<float>(flame.PlumeLengthM), 0.45f, 0.8f));
                    streakBad += FMath::Abs(streak - SOLThruster::FlameStreakDepth(arcM, flame.PlumeLengthM, 0.45, 0.8))
                        <= THRMIRROR_TOL ? 0 : 1;
                }
                const FString label = FString::Printf(TEXT("L_p %g, r_n %g, b %g, r %g"), flame.PlumeLengthM,
                    flame.RadiusM, b, r);
                TestEqual(label + TEXT(": knot mirror within 1e-5 over the sweep"), knotBad, 0);
                TestEqual(label + TEXT(": streak depth mirror within 1e-5 over the sweep"), streakBad, 0);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameNeckTest, "SOLTest.Thruster.FlameNeck",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 51e3, the idle axis dot: SOLThrusterMirror::FlameNeckAlpha (SOLTH_FlameNeckAlpha). A flame with L_p 2, r_n 0.8 (L_f 6,
// tail width 0.16 m), point alpha 0.8 x the along fade, spine along +x. Seen 10 degrees off the spine, the tail wall
// crossed farther along s (from the nozzle side the far wall, around 0.75; from the tail side the near wall, around
// 0.25) takes the along fade at the other crossing s - 0.16 cot(10 deg), never before L_p; the alpha is unchanged on
// the other half, in the taper and side-on, continuous at L_p and at the side view, and never lowered
bool FSOLThrusterFlameNeckTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrusterMaterialVectors v = SOLThrusterMirror::MaterialVectors();
    TestTrue(TEXT("Flame.xy = (FLAME_LENGTH_FACTOR 3, FLAME_TAIL_WIDTH_RATIO 0.1)"),
        v.Flame.X == 3.0f && FMath::IsNearlyEqual(v.Flame.Y, 0.1f));
    const FVector4f header(0.0f, 1.0f, 2.0f, 0.8f);
    const FVector3f axis(1.0f, 0.0f, 0.0f);
    const float rad10 = FMath::DegreesToRadians(10.0f);
    const FVector3f nozzleSide(-FMath::Cos(rad10), FMath::Sin(rad10), 0.0f);
    const FVector3f tailSide(FMath::Cos(rad10), FMath::Sin(rad10), 0.0f);
    const FVector3f sideOn(0.0f, 1.0f, 0.0f);
    constexpr float rest = 0.8f;
    constexpr float tol = 1.0e-4f;

    // Returns the mirror's alpha at an arc fraction and around coordinate, the point alpha rest x (1 - arc fraction)
    const auto neck = [&](const float arcFraction, const float around, const FVector3f& toCamera)
    {
        return SOLThrusterMirror::FlameNeckAlpha(rest * (1.0f - arcFraction), arcFraction, around, axis, toCamera,
            header, v.Flame);
    };

    // Deep in the tail (s 3): entry 3 - 0.16 cot(10 deg) = 2.0926, along 0.65123
    const float runM = 0.16f * FMath::Cos(rad10) / FMath::Sin(rad10);
    TestNearlyEqual(TEXT("far wall s 3: the entry's along fade"), neck(0.5f, 0.75f, nozzleSide),
        rest * (1.0f - (3.0f - runM) / 6.0f), tol);
    // Through the neck (s 2.4, entry clamped at L_p): the neck's along fade 2 / 3
    TestNearlyEqual(TEXT("far wall s 2.4: the neck's along fade"), neck(0.4f, 0.75f, nozzleSide), rest * 2.0f / 3.0f,
        tol);
    TestNearlyEqual(TEXT("looking straight down the spine: the neck's along fade"),
        neck(0.9f, 0.75f, FVector3f(-1.0f, 0.0f, 0.0f)), rest * 2.0f / 3.0f, tol);

    // From the tail side the near wall (around 0.25) is the crossing farther along s: the same fades
    TestNearlyEqual(TEXT("tail side, near wall s 3: the far crossing's along fade"), neck(0.5f, 0.25f, tailSide),
        rest * (1.0f - (3.0f - runM) / 6.0f), tol);
    TestNearlyEqual(TEXT("tail side, near wall s 2.4: the neck's along fade"), neck(0.4f, 0.25f, tailSide),
        rest * 2.0f / 3.0f, tol);

    // Unchanged: the other half from each end, the ring's side points, the taper, side-on
    TestNearlyEqual(TEXT("nozzle side, near wall (around 0.25) unchanged"), neck(0.5f, 0.25f, nozzleSide), rest * 0.5f,
        tol);
    TestNearlyEqual(TEXT("tail side, far wall (around 0.75) unchanged"), neck(0.5f, 0.75f, tailSide), rest * 0.5f, tol);
    TestNearlyEqual(TEXT("ring side point (around 0) unchanged"), neck(0.5f, 0.0f, nozzleSide), rest * 0.5f, tol);
    TestNearlyEqual(TEXT("taper (s 1.5) unchanged"), neck(0.25f, 0.75f, nozzleSide), rest * 0.75f, tol);
    TestNearlyEqual(TEXT("side-on unchanged"), neck(0.5f, 0.75f, sideOn), rest * 0.5f, tol);

    // Continuous at L_p and toward the side view
    TestNearlyEqual(TEXT("continuous just past L_p"), neck(2.0002f / 6.0f, 0.75f, nozzleSide),
        rest * (1.0f - 2.0002f / 6.0f), tol);
    const float rad8999 = FMath::DegreesToRadians(89.99f);
    TestNearlyEqual(TEXT("continuous toward the side view"), neck(0.5f, 0.75f,
        FVector3f(-FMath::Cos(rad8999), FMath::Sin(rad8999), 0.0f)), rest * 0.5f, tol);

    // Never lowered, never above the neck's fade where it changes anything, over a sweep
    int32 bad = 0;
    for (int32 i = 0; i <= 40; ++i)
    {
        for (int32 j = 0; j < 16; ++j)
        {
            for (const float deg : { 1.0f, 10.0f, 45.0f, 90.0f, 135.0f, 179.0f })
            {
                const float arcFraction = static_cast<float>(i) / 40.0f;
                const float rad = FMath::DegreesToRadians(deg);
                const float alpha = rest * (1.0f - arcFraction);
                const float out = neck(arcFraction, static_cast<float>(j) / 16.0f,
                    FVector3f(-FMath::Cos(rad), FMath::Sin(rad), 0.0f));
                bad += out >= alpha - tol && out <= FMath::Max(alpha, rest * 2.0f / 3.0f) + tol ? 0 : 1;
            }
        }
    }
    TestEqual(TEXT("sweep: never lowered, at most the neck's fade"), bad, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterMaterialAssetsTest, "SOLTest.Thruster.MaterialAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Part 51b, rewritten for R10 (SDD 51 sections 4.11 and R10): the script-built assets load with the agreed blend modes
// and usages, their vector defaults equal SOLThrusterMirror::MaterialVectors, the meshes carry the named thruster slots
// (Hull, Glow, Burst, no Plume: the cone is gone), and the one-flame ribbon system exposes the
// SOL::ThrusterTrailParams user parameters (incl. the flame header) and a tube-shaped, untessellated, screen-facing
// ribbon with camera-facing normals (not geometry normals). The Niagara flame material (M_SOLThrusterTrail) and the strip pool flame material
// (M_SOLThrusterRibbon) carry the same flame look defaults and the noise texture
bool FSOLThrusterMaterialAssetsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::ThrusterMaterialParams;
    const FSOLThrusterMaterialVectors v = SOLThrusterMirror::MaterialVectors();

    // Checks a flame material's look defaults (rims, cores, ramp, shock, shape, look, streak) and its noise texture
    const auto checkFlameLook = [this, &v](const UMaterialInterface& material, const TCHAR* label)
    {
        const TCHAR* rims[] = { RIM_0, RIM_1, RIM_2 };
        const TCHAR* cores[] = { CORE_0, CORE_1, CORE_2 };
        for (int32 key = 0; key < 3; ++key)
        {
            TestTrue(FString::Printf(TEXT("%s: %s default"), label, rims[key]),
                ThrMirrorColourDefaultEquals(material, rims[key], v.Rim[key]));
            TestTrue(FString::Printf(TEXT("%s: %s default"), label, cores[key]),
                ThrMirrorColourDefaultEquals(material, cores[key], v.Core[key]));
        }
        TestTrue(FString::Printf(TEXT("%s: ThrusterRamp default"), label),
            ThrMirrorDefaultEquals(material, RAMP, v.Ramp));
        TestTrue(FString::Printf(TEXT("%s: ThrusterShock default"), label),
            ThrMirrorDefaultEquals(material, SHOCK, v.Shock));
        TestTrue(FString::Printf(TEXT("%s: ThrusterShape default"), label),
            ThrMirrorDefaultEquals(material, SHAPE, v.Shape));
        TestTrue(FString::Printf(TEXT("%s: ThrusterLook default"), label),
            ThrMirrorDefaultEquals(material, LOOK, v.Look));
        TestTrue(FString::Printf(TEXT("%s: ThrusterStreak default"), label),
            ThrMirrorDefaultEquals(material, STREAK, v.Streak));
        TestTrue(FString::Printf(TEXT("%s: ThrusterFlame default"), label),
            ThrMirrorDefaultEquals(material, FLAME, v.Flame));
        UTexture* noise = nullptr;
        TestTrue(FString::Printf(TEXT("%s: noise texture parameter is set"), label), material.GetTextureParameterValue(
            FHashedMaterialParameterInfo(FName(NOISE_TEXTURE)), noise) && noise != nullptr);
    };

    // The plume material is retired (R10 replaced the cone with the flame tube), so none is checked

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

    // Trail (R10): the Niagara flame material, additive, unlit, two-sided, Niagara ribbon usage, the flame look
    const UMaterialInterface* trail = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::THRUSTER_TRAIL_MATERIAL);
    if (TestNotNull(TEXT("M_SOLThrusterTrail loads"), trail))
    {
        TestTrue(TEXT("trail: additive"), trail->GetBlendMode() == BLEND_Additive);
        TestTrue(TEXT("trail: unlit"), trail->GetShadingModels().IsUnlit());
        TestTrue(TEXT("trail: two-sided"), trail->IsTwoSided());
        TestTrue(TEXT("trail: used with Niagara ribbons"), trail->GetUsageByFlag(MATUSAGE_NiagaraRibbons));
        checkFlameLook(*trail, TEXT("trail"));
    }

    // Strip pool flame (R10): additive, unlit, two-sided, instanced, the trail's flame look, the pool layout vector,
    // the per-frame vector and the float texture parameter
    const UMaterialInterface* pool = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::THRUSTER_RIBBON_MATERIAL);
    if (TestNotNull(TEXT("M_SOLThrusterRibbon loads"), pool))
    {
        TestTrue(TEXT("ribbon: additive"), pool->GetBlendMode() == BLEND_Additive);
        TestTrue(TEXT("ribbon: unlit"), pool->GetShadingModels().IsUnlit());
        TestTrue(TEXT("ribbon: two-sided"), pool->IsTwoSided());
        TestTrue(TEXT("ribbon: used with instanced static meshes"),
            pool->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
        checkFlameLook(*pool, TEXT("ribbon"));
        TestTrue(TEXT("ribbon: ThrusterRibbon default"), ThrMirrorDefaultEquals(*pool, RIBBON, v.Ribbon));
        TestTrue(TEXT("ribbon: ThrusterRibbonFrame default (no live trail)"),
            ThrMirrorDefaultEquals(*pool, RIBBON_FRAME, FVector4f(0.0f, 0.0f, 0.0f, 0.0f)));
        UTexture* ribbonTexture = nullptr;
        TestTrue(TEXT("ribbon: ThrusterRibbonTex texture parameter"), pool->GetTextureParameterValue(
            FHashedMaterialParameterInfo(FName(RIBBON_TEXTURE)), ribbonTexture) && ribbonTexture != nullptr);
    }
    TestNotNull(TEXT("T_SOLThrusterNoise loads"), LoadObject<UTexture2D>(nullptr, SOL::Paths::THRUSTER_NOISE_TEXTURE));

    // Meshes: the named slots. R10 removed the plume slot (the flame is the Niagara tube and the strip pool)
    const UStaticMesh* cone = LoadObject<UStaticMesh>(nullptr, SOL::Paths::THRUSTER_NPC_HULL_MESH_FACTION_0);
    const UStaticMesh* box = LoadObject<UStaticMesh>(nullptr, SOL::Paths::THRUSTER_NPC_HULL_MESH_FACTION_1);
    const UStaticMesh* player = LoadObject<UStaticMesh>(nullptr, SOL::Paths::THRUSTER_PLAYER_MESH);
    for (const UStaticMesh* mesh : { cone, box })
    {
        if (TestNotNull(TEXT("NPC thruster hull mesh loads"), mesh))
        {
            TestTrue(TEXT("NPC hull: slots Hull, Glow, Burst"), ThrMirrorHasSlot(*mesh, SLOT_HULL)
                && ThrMirrorHasSlot(*mesh, SLOT_GLOW) && ThrMirrorHasSlot(*mesh, SLOT_BURST));
            TestFalse(TEXT("NPC hull: no Plume slot (removed by R10)"), ThrMirrorHasSlot(*mesh, TEXT("Plume")));
        }
    }
    if (TestNotNull(TEXT("SM_SOLPlayerThrusters loads"), player))
    {
        TestTrue(TEXT("player: slots Glow, Burst, no hull"), ThrMirrorHasSlot(*player, SLOT_GLOW)
            && ThrMirrorHasSlot(*player, SLOT_BURST) && !ThrMirrorHasSlot(*player, SLOT_HULL));
        TestFalse(TEXT("player: no Plume slot (removed by R10)"), ThrMirrorHasSlot(*player, TEXT("Plume")));
    }

    // Ribbon system: the array, count and flame header user parameters
    const UNiagaraSystem* trails = LoadObject<UNiagaraSystem>(nullptr, SOL::Paths::THRUSTER_TRAILS_NIAGARA_SYSTEM);
    if (TestNotNull(TEXT("NS_SOLThrusterTrails loads"), trails))
    {
        TArray<FNiagaraVariable> parameters;
        trails->GetExposedParameters().GetUserParameters(parameters);
        const TCHAR* expected[] = { SOL::ThrusterTrailParams::POSITIONS_CM, SOL::ThrusterTrailParams::COLOURS,
            SOL::ThrusterTrailParams::WIDTHS_CM, SOL::ThrusterTrailParams::POINT_COUNT,
            SOL::ThrusterTrailParams::MAX_POINTS, SOL::ThrusterTrailParams::POINTS_PER_TRAIL,
            SOL::ThrusterTrailParams::FLAME_HEADER };
        for (const TCHAR* name : expected)
        {
            const FName userName(FString(TEXT("User.")) + name);
            const bool bFound = parameters.ContainsByPredicate([name, &userName](const FNiagaraVariable& variable)
            {
                return variable.GetName() == FName(name) || variable.GetName() == userName;
            });
            TestTrue(FString::Printf(TEXT("trails: user parameter %s"), name), bFound);
        }

        // The flame header (c, b, L_p, r_n) is a Vector4
        const FName headerName(FString(TEXT("User.")) + SOL::ThrusterTrailParams::FLAME_HEADER);
        const FNiagaraVariable* header = parameters.FindByPredicate([&headerName](const FNiagaraVariable& variable)
        {
            return variable.GetName() == headerName
                || variable.GetName() == FName(SOL::ThrusterTrailParams::FLAME_HEADER);
        });
        TestTrue(TEXT("trails: flame header is a Vector4"),
            header != nullptr && header->GetType() == FNiagaraTypeDefinition::GetVec4Def());

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

                // Parity with the strip pool (R7): straight camera-facing segments
                TestTrue(TEXT("trails: ribbon tessellation off"),
                    ribbon->TessellationMode == ENiagaraRibbonTessellationMode::Disabled);
                TestTrue(TEXT("trails: ribbon faces the screen"),
                    ribbon->FacingMode == ENiagaraRibbonFacingMode::Screen);

                // R10: the flame is a FLAME_TUBE_SIDES-sided tube, like the strip pool's (the pixel's radial fraction
                // comes from the around coordinate V, the same in both renderers)
                TestTrue(TEXT("trails: ribbon shape is a tube"), ribbon->Shape == ENiagaraRibbonShapeMode::Tube);
                TestEqual(TEXT("trails: tube subdivisions are FLAME_TUBE_SIDES"), ribbon->TubeSubdivisions,
                    static_cast<int32>(SOL::Thruster::FLAME_TUBE_SIDES));
                // Camera-facing normals (a tube with geometry normals draws nothing in 5.8)
                TestFalse(TEXT("trails: ribbon keeps the camera-facing normals"), ribbon->bUseGeometryNormals);
            }
        }

        // 51e: one trail per system instance (trails sharing a system merged into a zigzag, 51e-0); 51e3: a trail is
        // the spine and its two end caps
        const FNiagaraVariable maxPoints(FNiagaraTypeDefinition::GetIntDef(),
            FName(FString(TEXT("User.")) + SOL::ThrusterTrailParams::MAX_POINTS));
        TestEqual(TEXT("trails: TrailMaxPoints default is one drawn trail"),
            trails->GetExposedParameters().GetParameterValueOrDefault<int32>(maxPoints, INDEX_NONE),
            static_cast<int32>(SOL::Thruster::FLAME_DRAWN_POINTS));
        const FNiagaraVariable pointsPerTrail(FNiagaraTypeDefinition::GetIntDef(),
            FName(FString(TEXT("User.")) + SOL::ThrusterTrailParams::POINTS_PER_TRAIL));
        TestEqual(TEXT("trails: TrailPointsPerTrail default is FLAME_DRAWN_POINTS"),
            trails->GetExposedParameters().GetParameterValueOrDefault<int32>(pointsPerTrail, INDEX_NONE),
            static_cast<int32>(SOL::Thruster::FLAME_DRAWN_POINTS));
    }
    return true;
}

#endif
