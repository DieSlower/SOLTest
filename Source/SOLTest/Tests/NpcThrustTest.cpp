/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcAiMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficMath.h"
#include "Visuals/SOLThrusterMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #51 part 51c NPC thrust glue (SDD 51 sections 4.2, 4.5 and 4.10), written as contracts before the
// production code: the hull-frame projection of a world thrust (SOLThruster::HullThrustLocalG), a Full ship's achieved
// acceleration (SOLNpcAi::AchievedAccelMps2), the per-ship plume floats a hull-drawn NPC writes
// (SOLThruster::PlumeInstance, PlumeCustomData). The lane rail's thrust (SOLNpcRail::LaneThrustMps2) is tested with the
// rail helpers in Tests/NpcRailTest.cpp (SOLTest.NpcRail.LaneThrust). Hull frame: X nose, Y right = top x forward, Z
// top, in the axes the caller passes (Unreal axes in the game).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCTHRUST_TOL = 1.0e-5;
    constexpr double NPCTHRUST_LEN_TOL = 1.0e-3;
    constexpr double NPCTHRUST_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double NPCTHRUST_INF = std::numeric_limits<double>::infinity();

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a float vector agrees with (x, y, z) component-wise within a tolerance
    bool NpcThrustVecNear(const FVector3f& value, const double x, const double y, const double z,
        const double tolerance = NPCTHRUST_TOL)
    {
        return FMath::Abs(static_cast<double>(value.X) - x) <= tolerance
            && FMath::Abs(static_cast<double>(value.Y) - y) <= tolerance
            && FMath::Abs(static_cast<double>(value.Z) - z) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a double vector agrees with (x, y, z) component-wise within a tolerance
    bool NpcThrustVecNearD(const FVector3d& value, const double x, const double y, const double z,
        const double tolerance = NPCTHRUST_TOL)
    {
        return FMath::Abs(value.X - x) <= tolerance && FMath::Abs(value.Y - y) <= tolerance
            && FMath::Abs(value.Z - z) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every field of a plume instance is exactly zero
    bool NpcThrustPlumeOff(const FSOLPlumeInstance& plume)
    {
        return plume.Colour == 0.0f && plume.Brightness == 0.0f && plume.LengthM == 0.0f;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcThrustHullFrameTest, "SOLTest.NpcThrust.HullFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// HullThrustLocalG(thrustMps2, hullForward, hullTop): the thrust in G on (forward, right = top x forward, top), the
// forward normalised and the top made orthogonal to it and normalised first; zero for a non-finite input, a zero
// forward, or a top that is zero or parallel to the forward
bool FSOLNpcThrustHullFrameTest::RunTest(const FString& /*parameters*/)
{
    const double g0 = SOL::STANDARD_GRAVITY_MPS2;
    const FVector3d forward(1.0, 0.0, 0.0);
    const FVector3d top(0.0, 0.0, 1.0);

    // The identity basis: forward is X, right = Z x X = Y, top Z
    TestTrue(TEXT("(g0, 0, 0) -> (1, 0, 0)"),
        NpcThrustVecNear(SOLThruster::HullThrustLocalG(FVector3d(g0, 0.0, 0.0), forward, top), 1.0, 0.0, 0.0));
    TestTrue(TEXT("(0, 2 g0, 0) -> (0, 2, 0)"),
        NpcThrustVecNear(SOLThruster::HullThrustLocalG(FVector3d(0.0, 2.0 * g0, 0.0), forward, top), 0.0, 2.0, 0.0));
    TestTrue(TEXT("(0, 0, -g0) -> (0, 0, -1)"),
        NpcThrustVecNear(SOLThruster::HullThrustLocalG(FVector3d(0.0, 0.0, -g0), forward, top), 0.0, 0.0, -1.0));
    TestTrue(TEXT("(-1.65 g0, 0, 0) -> (-1.65, 0, 0) (reverse thrust stays signed)"),
        NpcThrustVecNear(SOLThruster::HullThrustLocalG(FVector3d(-1.65 * g0, 0.0, 0.0), forward, top), -1.65, 0.0,
            0.0));

    // A non-unit forward and a top not orthogonal to it give the same frame as the orthonormal basis
    TestTrue(TEXT("forward (2, 0, 0), top (0.5, 0, 2): (g0, g0, g0) -> (1, 1, 1)"),
        NpcThrustVecNear(SOLThruster::HullThrustLocalG(FVector3d(g0, g0, g0), FVector3d(2.0, 0.0, 0.0),
            FVector3d(0.5, 0.0, 2.0)), 1.0, 1.0, 1.0));

    // A turned basis: forward +Y, top +Z, so right = Z x Y = -X
    const FVector3d forwardY(0.0, 1.0, 0.0);
    TestTrue(TEXT("forward +Y: (g0, 0, 0) -> (0, -1, 0)"),
        NpcThrustVecNear(SOLThruster::HullThrustLocalG(FVector3d(g0, 0.0, 0.0), forwardY, top), 0.0, -1.0, 0.0));
    TestTrue(TEXT("forward +Y: (0, 3 g0, 0) -> (3, 0, 0)"),
        NpcThrustVecNear(SOLThruster::HullThrustLocalG(FVector3d(0.0, 3.0 * g0, 0.0), forwardY, top), 3.0, 0.0, 0.0));

    // An oblique basis: the length of the thrust is kept (the basis is orthonormal)
    const FVector3d oblique = FVector3d(1.0, 2.0, -0.5).GetSafeNormal();
    const FVector3d obliqueTop = FVector3d::CrossProduct(oblique, FVector3d(0.3, -0.2, 0.9)).GetSafeNormal();
    const FVector3d thrust(2.0 * g0, -0.7 * g0, 1.1 * g0);
    const FVector3f local = SOLThruster::HullThrustLocalG(thrust, oblique, obliqueTop);
    TestTrue(TEXT("oblique basis: |local| = |thrust| / g0"),
        FMath::Abs(static_cast<double>(local.Size()) - thrust.Size() / g0) <= 1.0e-4);
    TestTrue(TEXT("oblique basis: X = thrust . forward / g0"),
        FMath::Abs(static_cast<double>(local.X) - FVector3d::DotProduct(thrust, oblique) / g0) <= 1.0e-5);

    // The lane form agrees: an accelerating lane ship's state through LaneThrustLocalG on an orthonormal basis
    FSOLLaneThrustState state;
    state.Thrust = ESOLLaneThrust::Turn;
    state.AccelMps2 = 1.2 * g0;
    state.Direction = FVector3d(0.6, 0.0, 0.8);
    state.HullForward = forward;
    const FVector3f fromLane = SOLThruster::LaneThrustLocalG(state, forward, top);
    const FVector3f fromWorld = SOLThruster::HullThrustLocalG(state.Direction * state.AccelMps2, forward, top);
    TestTrue(TEXT("agrees with LaneThrustLocalG on an orthonormal basis"),
        NpcThrustVecNear(fromWorld, fromLane.X, fromLane.Y, fromLane.Z));

    // Zero cases
    TestTrue(TEXT("zero thrust -> 0"),
        NpcThrustVecNear(SOLThruster::HullThrustLocalG(FVector3d::ZeroVector, forward, top), 0.0, 0.0, 0.0, 0.0));
    TestTrue(TEXT("NaN thrust -> 0"), NpcThrustVecNear(SOLThruster::HullThrustLocalG(
        FVector3d(NPCTHRUST_NAN, 0.0, 0.0), forward, top), 0.0, 0.0, 0.0, 0.0));
    TestTrue(TEXT("infinite thrust -> 0"), NpcThrustVecNear(SOLThruster::HullThrustLocalG(
        FVector3d(0.0, NPCTHRUST_INF, 0.0), forward, top), 0.0, 0.0, 0.0, 0.0));
    TestTrue(TEXT("zero forward -> 0"), NpcThrustVecNear(SOLThruster::HullThrustLocalG(
        FVector3d(g0, 0.0, 0.0), FVector3d::ZeroVector, top), 0.0, 0.0, 0.0, 0.0));
    TestTrue(TEXT("NaN forward -> 0"), NpcThrustVecNear(SOLThruster::HullThrustLocalG(
        FVector3d(g0, 0.0, 0.0), FVector3d(NPCTHRUST_NAN, 0.0, 0.0), top), 0.0, 0.0, 0.0, 0.0));
    TestTrue(TEXT("zero top -> 0"), NpcThrustVecNear(SOLThruster::HullThrustLocalG(
        FVector3d(g0, 0.0, 0.0), forward, FVector3d::ZeroVector), 0.0, 0.0, 0.0, 0.0));
    TestTrue(TEXT("top parallel to forward -> 0"), NpcThrustVecNear(SOLThruster::HullThrustLocalG(
        FVector3d(g0, 0.0, 0.0), forward, FVector3d(-3.0, 0.0, 0.0)), 0.0, 0.0, 0.0, 0.0));
    TestTrue(TEXT("infinite top -> 0"), NpcThrustVecNear(SOLThruster::HullThrustLocalG(
        FVector3d(g0, 0.0, 0.0), forward, FVector3d(0.0, 0.0, NPCTHRUST_INF)), 0.0, 0.0, 0.0, 0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcThrustAchievedAccelTest, "SOLTest.NpcThrust.AchievedAccel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// AchievedAccelMps2(velocityBefore, velocityAfter, dt): (after - before) / dt, the Full AI step's achieved acceleration
// in the rail frame; zero for dt <= 0, a non-finite dt or a non-finite velocity
bool FSOLNpcThrustAchievedAccelTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("(0, 0, 0) -> (1, 2, 3) over 0.5 s = (2, 4, 6)"), NpcThrustVecNearD(
        SOLNpcAi::AchievedAccelMps2(FVector3d::ZeroVector, FVector3d(1.0, 2.0, 3.0), 0.5), 2.0, 4.0, 6.0));
    TestTrue(TEXT("(10, -4, 0) -> (9, -4, 0.25) over 1/60 s = (-60, 0, 15)"), NpcThrustVecNearD(
        SOLNpcAi::AchievedAccelMps2(FVector3d(10.0, -4.0, 0.0), FVector3d(9.0, -4.0, 0.25), 1.0 / 60.0), -60.0, 0.0,
        15.0, 1.0e-9));
    TestTrue(TEXT("no change -> 0"), NpcThrustVecNearD(
        SOLNpcAi::AchievedAccelMps2(FVector3d(5.0, 5.0, 5.0), FVector3d(5.0, 5.0, 5.0), 0.016), 0.0, 0.0, 0.0, 0.0));
    for (const double badDt : { 0.0, -0.016, NPCTHRUST_NAN, NPCTHRUST_INF })
    {
        TestTrue(FString::Printf(TEXT("dt %f -> 0"), badDt), NpcThrustVecNearD(
            SOLNpcAi::AchievedAccelMps2(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), badDt), 0.0, 0.0, 0.0, 0.0));
    }
    TestTrue(TEXT("NaN velocity before -> 0"), NpcThrustVecNearD(SOLNpcAi::AchievedAccelMps2(
        FVector3d(NPCTHRUST_NAN, 0.0, 0.0), FVector3d(1.0, 0.0, 0.0), 0.016), 0.0, 0.0, 0.0, 0.0));
    TestTrue(TEXT("infinite velocity after -> 0"), NpcThrustVecNearD(SOLNpcAi::AchievedAccelMps2(
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, NPCTHRUST_INF), 0.016), 0.0, 0.0, 0.0, 0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcThrustPlumeInstanceTest, "SOLTest.NpcThrust.PlumeInstance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// PlumeInstance(gMain, bWarPower, hullLengthM, nozzleRadiusM, distanceM, focalRefPx): the plume floats of one hull-
// drawn ship, colour PlumeColourScalar, brightness PlumeBrightness, true length PlumeLengthM (the true hull length,
// never the drawn size, Q14); all zero when the plume is off, when the nozzle glow's true diameter
// (GLOW_DIAMETER_NOZZLE_RADII x the nozzle radius) is under GLOW_FADE_LOW_PX on screen (R10: the zero gate is the
// glow's own fade, not the flame's length), and for a non-finite or non-positive distance, focal length or nozzle
// radius. PlumeCustomData writes them into the SOL::ThrusterMaterialParams layout with the burst floats zero (bursts
// are part 51g)
bool FSOLNpcThrustPlumeInstanceTest::RunTest(const FString& /*parameters*/)
{
    const double hullM = SOL::Thruster::NPC_HULL_LENGTH_M;
    const double radiusM = SOL::Thruster::NPC_NOZZLE_RADIUS_M;
    const double focal = 960.0;

    // A 1.65 G lane burn at 1 km (SDD 51 section 4.3 row 1.65 G)
    const FSOLPlumeInstance burn = SOLThruster::PlumeInstance(1.65, false, hullM, radiusM, 1000.0, focal);
    TestTrue(TEXT("1.65 G: c 0"), FMath::Abs(burn.Colour) <= NPCTHRUST_TOL);
    TestTrue(TEXT("1.65 G: b 0.4925"), FMath::Abs(burn.Brightness - 0.4925f) <= 1.0e-4f);
    TestTrue(TEXT("1.65 G: L_p 9.659 m"), FMath::Abs(burn.LengthM - 9.659f) <= NPCTHRUST_LEN_TOL);
    TestTrue(TEXT("1.65 G: equals the SOLThruster scalars"),
        FMath::IsNearlyEqual(burn.Brightness, static_cast<float>(SOLThruster::PlumeBrightness(1.65)), 1.0e-6f)
        && FMath::IsNearlyEqual(burn.LengthM, static_cast<float>(SOLThruster::PlumeLengthM(hullM, 1.65)), 1.0e-5f));

    // The colour ramp and the War power override (b and the length follow the real G)
    const FSOLPlumeInstance hot = SOLThruster::PlumeInstance(3.0, false, hullM, radiusM, 1000.0, focal);
    TestTrue(TEXT("3 G: c 0.35"), FMath::Abs(hot.Colour - 0.35f) <= 1.0e-4f);
    const FSOLPlumeInstance war = SOLThruster::PlumeInstance(1.5, true, hullM, radiusM, 1000.0, focal);
    TestTrue(TEXT("War power 1.5 G: c 1"), FMath::Abs(war.Colour - 1.0f) <= NPCTHRUST_TOL);
    TestTrue(TEXT("War power 1.5 G: b and L follow 1.5 G"),
        FMath::IsNearlyEqual(war.Brightness, static_cast<float>(SOLThruster::PlumeBrightness(1.5)), 1.0e-6f)
        && FMath::IsNearlyEqual(war.LengthM, static_cast<float>(SOLThruster::PlumeLengthM(hullM, 1.5)), 1.0e-5f));

    // Clamped above G_REF: the hull length, full brightness and colour
    const FSOLPlumeInstance boost = SOLThruster::PlumeInstance(9.0, false, hullM, radiusM, 1000.0, focal);
    TestTrue(TEXT("9 G: L = hull length, b 1, c 1"), FMath::Abs(boost.LengthM - 30.0f) <= NPCTHRUST_LEN_TOL
        && FMath::Abs(boost.Brightness - 1.0f) <= NPCTHRUST_TOL && FMath::Abs(boost.Colour - 1.0f) <= NPCTHRUST_TOL);

    // The true hull length sets the length: half the hull, the length scales by 0.5
    const FSOLPlumeInstance halfHull = SOLThruster::PlumeInstance(1.65, false, 0.5 * hullM, radiusM, 1000.0, focal);
    TestTrue(TEXT("proportional to the true hull length"),
        FMath::Abs(halfHull.LengthM - 0.5f * burn.LengthM) <= NPCTHRUST_LEN_TOL);

    // The pixel rule: the glow's true diameter (3.75 m for the NPC) is GLOW_FADE_LOW_PX (0.5 px) at f D / 0.5 = 7200 m,
    // whatever the G (the flame's length no longer matters)
    const double edgeM = focal * SOL::Thruster::GLOW_DIAMETER_NOZZLE_RADII * radiusM / SOL::Thruster::GLOW_FADE_LOW_PX;
    TestFalse(TEXT("just inside the glow's low pixel edge: shown"),
        NpcThrustPlumeOff(SOLThruster::PlumeInstance(1.65, false, hullM, radiusM, 0.99 * edgeM, focal)));
    TestTrue(TEXT("just past the glow's low pixel edge: off"),
        NpcThrustPlumeOff(SOLThruster::PlumeInstance(1.65, false, hullM, radiusM, 1.01 * edgeM, focal)));
    TestTrue(TEXT("a narrower view (larger f) shows it farther"),
        !NpcThrustPlumeOff(SOLThruster::PlumeInstance(1.65, false, hullM, radiusM, 1.01 * edgeM, 2.0 * focal)));
    TestFalse(TEXT("the same edge at 9 G: shown just inside"),
        NpcThrustPlumeOff(SOLThruster::PlumeInstance(9.0, false, hullM, radiusM, 0.99 * edgeM, focal)));
    TestTrue(TEXT("the same edge at 9 G: off just past it"),
        NpcThrustPlumeOff(SOLThruster::PlumeInstance(9.0, false, hullM, radiusM, 1.01 * edgeM, focal)));

    // A narrower nozzle moves the edge in: half the radius, half the distance
    TestTrue(TEXT("half the nozzle radius: off past half the edge"), NpcThrustPlumeOff(
        SOLThruster::PlumeInstance(1.65, false, hullM, 0.5 * radiusM, 1.01 * 0.5 * edgeM, focal)));
    TestFalse(TEXT("half the nozzle radius: shown inside half the edge"), NpcThrustPlumeOff(
        SOLThruster::PlumeInstance(1.65, false, hullM, 0.5 * radiusM, 0.99 * 0.5 * edgeM, focal)));

    // Off: under the cut-off, no thrust, reverse, bad G, bad distance, focal or nozzle radius
    for (const double g : { 0.0, 0.5 * SOL::Thruster::PLUME_MIN_G, -1.65, NPCTHRUST_NAN, NPCTHRUST_INF })
    {
        TestTrue(FString::Printf(TEXT("G %f: off"), g),
            NpcThrustPlumeOff(SOLThruster::PlumeInstance(g, false, hullM, radiusM, 1000.0, focal)));
    }
    TestTrue(TEXT("War power at 0 G: off (no colour without a plume)"),
        NpcThrustPlumeOff(SOLThruster::PlumeInstance(0.0, true, hullM, radiusM, 1000.0, focal)));
    for (const double distance : { 0.0, -5.0, NPCTHRUST_NAN, NPCTHRUST_INF })
    {
        TestTrue(FString::Printf(TEXT("distance %f: off"), distance),
            NpcThrustPlumeOff(SOLThruster::PlumeInstance(1.65, false, hullM, radiusM, distance, focal)));
    }
    for (const double badFocal : { 0.0, -960.0, NPCTHRUST_NAN })
    {
        TestTrue(FString::Printf(TEXT("focal %f: off"), badFocal),
            NpcThrustPlumeOff(SOLThruster::PlumeInstance(1.65, false, hullM, radiusM, 1000.0, badFocal)));
    }
    for (const double badRadius : { 0.0, -1.5, NPCTHRUST_NAN, NPCTHRUST_INF })
    {
        TestTrue(FString::Printf(TEXT("nozzle radius %f: off"), badRadius),
            NpcThrustPlumeOff(SOLThruster::PlumeInstance(1.65, false, hullM, badRadius, 1000.0, focal)));
    }
    TestTrue(TEXT("zero hull length: off"),
        NpcThrustPlumeOff(SOLThruster::PlumeInstance(1.65, false, 0.0, radiusM, 1000.0, focal)));

    // The custom-data layout: c, b, L_p at their indices, the three burst floats zero, the rest of the view untouched
    namespace P = SOL::ThrusterMaterialParams;
    float data[P::CUSTOM_DATA_FLOATS + 1];
    for (float& value : data)
    {
        value = -7.0f;
    }
    SOLThruster::PlumeCustomData(burn, MakeArrayView(data, P::CUSTOM_DATA_FLOATS));
    TestEqual(TEXT("custom data: colour"), data[P::CUSTOM_COLOUR], burn.Colour);
    TestEqual(TEXT("custom data: brightness"), data[P::CUSTOM_BRIGHTNESS], burn.Brightness);
    TestEqual(TEXT("custom data: true plume length"), data[P::CUSTOM_PLUME_LENGTH_M], burn.LengthM);
    TestEqual(TEXT("custom data: reverse burst 0"), data[P::CUSTOM_BURST_REVERSE], 0.0f);
    TestEqual(TEXT("custom data: lateral burst 0"), data[P::CUSTOM_BURST_LATERAL], 0.0f);
    TestEqual(TEXT("custom data: vertical burst 0"), data[P::CUSTOM_BURST_VERTICAL], 0.0f);
    TestEqual(TEXT("custom data: nothing written past the view"), data[P::CUSTOM_DATA_FLOATS], -7.0f);
    float shortView[2] = { -7.0f, -7.0f };
    SOLThruster::PlumeCustomData(burn, MakeArrayView(shortView, 1));
    TestTrue(TEXT("a short view: only its own floats"), shortView[0] == burn.Colour && shortView[1] == -7.0f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
