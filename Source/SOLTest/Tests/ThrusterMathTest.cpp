/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLThrusterMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #51 thruster plume math (SOLThruster, SDD 51 sections 4.2-4.9 and 7), written against the test
// contract Docs/Plans/51-thruster-math-contract.md before the production code existed (part 51a, test first). One
// automation test per contract case; case 12 (MaterialMirror, part 51b) is in Tests/ThrusterMirrorTest.cpp. Tolerances follow the contract: lengths 1e-3 m, scalars 1e-4, unless a case states otherwise. Hull frame:
// X nose, Y right, Z top (UE axes). Values the contract marks as parameters (the LOD bands, the ribbon radius, the
// burst threshold) are passed as arguments here, so tuning them later does not touch this file.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double THRUSTER_LEN_TOL = 1.0e-3;                     // Lengths, metres
    constexpr double THRUSTER_TOL = 1.0e-4;                         // Scalars
    constexpr double THRUSTER_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double THRUSTER_INF = std::numeric_limits<double>::infinity();
    constexpr float THRUSTER_NAN_F = std::numeric_limits<float>::quiet_NaN();
    constexpr float THRUSTER_INF_F = std::numeric_limits<float>::infinity();

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a float vector agrees with (x, y, z) component-wise within a tolerance
    bool ThrusterVecNear(const FVector3f& value, const double x, const double y, const double z,
        const double tolerance)
    {
        return FMath::Abs(static_cast<double>(value.X) - x) <= tolerance
            && FMath::Abs(static_cast<double>(value.Y) - y) <= tolerance
            && FMath::Abs(static_cast<double>(value.Z) - z) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a double vector agrees with (x, y, z) component-wise within a tolerance
    bool ThrusterVecNearD(const FVector3d& value, const double x, const double y, const double z,
        const double tolerance)
    {
        return FMath::Abs(value.X - x) <= tolerance && FMath::Abs(value.Y - y) <= tolerance
            && FMath::Abs(value.Z - z) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a colour's RGB agrees with (r, g, b) within a tolerance and its alpha is 1
    bool ThrusterColourNear(const FLinearColor& colour, const double r, const double g, const double b,
        const double tolerance)
    {
        return FMath::Abs(static_cast<double>(colour.R) - r) <= tolerance
            && FMath::Abs(static_cast<double>(colour.G) - g) <= tolerance
            && FMath::Abs(static_cast<double>(colour.B) - b) <= tolerance
            && FMath::Abs(static_cast<double>(colour.A) - 1.0) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two colours agree in all four channels within a tolerance
    bool ThrusterColourEqual(const FLinearColor& a, const FLinearColor& b, const double tolerance)
    {
        return FMath::Abs(static_cast<double>(a.R) - static_cast<double>(b.R)) <= tolerance
            && FMath::Abs(static_cast<double>(a.G) - static_cast<double>(b.G)) <= tolerance
            && FMath::Abs(static_cast<double>(a.B) - static_cast<double>(b.B)) <= tolerance
            && FMath::Abs(static_cast<double>(a.A) - static_cast<double>(b.A)) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when an index array equals the expected list exactly, in order
    bool ThrusterSameIndices(const TArray<int32>& actual, const TArray<int32>& expected)
    {
        if (actual.Num() != expected.Num())
        {
            return false;
        }
        for (int32 index = 0; index < actual.Num(); ++index)
        {
            if (actual[index] != expected[index])
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane thrust state with every field set (the hull facing matches the basis passed to LaneThrustLocalG)
    FSOLLaneThrustState ThrusterLaneState(const ESOLLaneThrust thrust, const double accelMps2,
        const FVector3d& direction, const FVector3d& hullForward, const double flipFraction = 0.0)
    {
        FSOLLaneThrustState state;
        state.Segment = ESOLLaneSegment::Outbound;
        state.Thrust = thrust;
        state.AccelMps2 = accelMps2;
        state.Direction = direction;
        state.FlipFraction = flipFraction;
        state.HullForward = hullForward;
        return state;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterPlumeLengthTest, "SOLTest.Thruster.PlumeLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1: x = clamp(G / G_REF, 0, 1) and L_p = L_hull x^0.8 at the section 4.3 values, proportional to the hull length,
// clamped at and above G_REF, off under the 0.01 G cut-off, and 0 for bad input
bool FSOLThrusterPlumeLengthTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const double gRef = static_cast<double>(SOL::Thruster::G_REF);

    // 30 m hull (NPC) at the table's G values
    TestEqual(TEXT("30 m, 0.825 G: 5.548 m"), PlumeLengthM(30.0, 0.825), 5.548, THRUSTER_LEN_TOL);
    TestEqual(TEXT("30 m, 1 G: 6.471 m"), PlumeLengthM(30.0, 1.0), 6.471, THRUSTER_LEN_TOL);
    TestEqual(TEXT("30 m, 1.65 G: 9.659 m"), PlumeLengthM(30.0, 1.65), 9.659, THRUSTER_LEN_TOL);
    TestEqual(TEXT("30 m, 3 G: 15.583 m"), PlumeLengthM(30.0, 3.0), 15.583, THRUSTER_LEN_TOL);
    TestEqual(TEXT("30 m, G_REF: 30 m"), PlumeLengthM(30.0, gRef), 30.0, THRUSTER_LEN_TOL);

    // Clamped above G_REF (boost up to 9 G)
    TestEqual(TEXT("30 m, 7 G: clamped to 30 m"), PlumeLengthM(30.0, 7.0), 30.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("30 m, 9 G: clamped to 30 m"), PlumeLengthM(30.0, 9.0), 30.0, THRUSTER_LEN_TOL);

    // 20 m hull (player) and proportionality
    TestEqual(TEXT("20 m, 3 G: 10.388 m"), PlumeLengthM(20.0, 3.0), 10.388, THRUSTER_LEN_TOL);
    TestEqual(TEXT("20 m, G_REF: 20 m"), PlumeLengthM(20.0, gRef), 20.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("20 m, 9 G: clamped to 20 m"), PlumeLengthM(20.0, 9.0), 20.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("60 m, 3 G: 31.165 m"), PlumeLengthM(60.0, 3.0), 31.165, THRUSTER_LEN_TOL);
    TestEqual(TEXT("60 m at 3 G is twice 30 m"), PlumeLengthM(60.0, 3.0), 2.0 * PlumeLengthM(30.0, 3.0),
        THRUSTER_LEN_TOL);

    // The fraction x
    TestEqual(TEXT("fraction 1 G: 0.146987"), PlumeFraction(1.0), 0.146987, THRUSTER_TOL);
    TestEqual(TEXT("fraction 3 G: 0.440962"), PlumeFraction(3.0), 0.440962, THRUSTER_TOL);
    TestEqual(TEXT("fraction G_REF: 1"), PlumeFraction(gRef), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("fraction 9 G: 1"), PlumeFraction(9.0), 1.0, THRUSTER_TOL);

    // Off under the cut-off, on just above it
    TestEqual(TEXT("0.009 G: off"), PlumeLengthM(30.0, 0.009), 0.0, THRUSTER_LEN_TOL);
    TestTrue(TEXT("0.02 G: on"), PlumeLengthM(30.0, 0.02) > 0.0);

    // Bad G
    TestEqual(TEXT("negative G: 0"), PlumeLengthM(30.0, -1.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("NaN G: 0"), PlumeLengthM(30.0, THRUSTER_NAN), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("+inf G: 0"), PlumeLengthM(30.0, THRUSTER_INF), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("-inf G: 0"), PlumeLengthM(30.0, -THRUSTER_INF), 0.0, THRUSTER_LEN_TOL);

    // Bad hull length
    TestEqual(TEXT("hull 0: 0"), PlumeLengthM(0.0, 3.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("hull -30: 0"), PlumeLengthM(-30.0, 3.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("hull NaN: 0"), PlumeLengthM(THRUSTER_NAN, 3.0), 0.0, THRUSTER_LEN_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterPlumeBrightnessTest, "SOLTest.Thruster.PlumeBrightness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 2: b = x^0.5 at the section 4.3 values, clamped at and above G_REF, 0 when off or for bad input
bool FSOLThrusterPlumeBrightnessTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const double gRef = static_cast<double>(SOL::Thruster::G_REF);

    TestEqual(TEXT("0.825 G: 0.34823"), PlumeBrightness(0.825), 0.34823, THRUSTER_TOL);
    TestEqual(TEXT("1 G: 0.38339"), PlumeBrightness(1.0), 0.38339, THRUSTER_TOL);
    TestEqual(TEXT("1.65 G: 0.49247"), PlumeBrightness(1.65), 0.49247, THRUSTER_TOL);
    TestEqual(TEXT("3 G: 0.66405"), PlumeBrightness(3.0), 0.66405, THRUSTER_TOL);
    TestEqual(TEXT("G_REF: 1"), PlumeBrightness(gRef), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("7 G: clamped to 1"), PlumeBrightness(7.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("9 G: clamped to 1"), PlumeBrightness(9.0), 1.0, THRUSTER_TOL);

    // Off and bad input
    TestEqual(TEXT("0.009 G: 0"), PlumeBrightness(0.009), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("negative G: 0"), PlumeBrightness(-2.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("NaN G: 0"), PlumeBrightness(THRUSTER_NAN), 0.0, THRUSTER_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterColourRampTest, "SOLTest.Thruster.ColourRamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 3: the colour scalar c(G) (0 to 2.5 G, smoothstep to 0.35 at 3 G, linear to 1 at G_REF, clamped above), War
// power forcing c = 1, continuity over a fine sweep, and the three-key rim / core ramp PlumeColour(c)
bool FSOLThrusterColourRampTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const double gRef = static_cast<double>(SOL::Thruster::G_REF);

    // c at the key points and examples
    TestEqual(TEXT("c(0 G) = 0"), PlumeColourScalar(0.0, false), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("c(2.5 G) = 0"), PlumeColourScalar(2.5, false), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("c(2.75 G) = 0.175"), PlumeColourScalar(2.75, false), 0.175, THRUSTER_TOL);
    TestEqual(TEXT("c(3 G) = 0.35"), PlumeColourScalar(3.0, false), 0.35, THRUSTER_TOL);
    TestEqual(TEXT("c(4 G) = 0.52090"), PlumeColourScalar(4.0, false), 0.52090, THRUSTER_TOL);
    TestEqual(TEXT("c(5 G) = 0.69181"), PlumeColourScalar(5.0, false), 0.69181, THRUSTER_TOL);
    TestEqual(TEXT("c(G_REF) = 1"), PlumeColourScalar(gRef, false), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("c(7 G) clamped to 1"), PlumeColourScalar(7.0, false), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("c(9 G) clamped to 1"), PlumeColourScalar(9.0, false), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("c(NaN G) = 0"), PlumeColourScalar(THRUSTER_NAN, false), 0.0, THRUSTER_TOL);

    // War power: always red-orange
    TestEqual(TEXT("War power at 1.5 G: 1"), PlumeColourScalar(1.5, true), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("War power at 0 G: 1"), PlumeColourScalar(0.0, true), 1.0, THRUSTER_TOL);

    // Continuity and range over 0 to 9 G in 0.001 G steps (G computed per step, not accumulated)
    double previous = PlumeColourScalar(0.0, false);
    double maxStep = 0.0;
    int32 jumps = 0;
    int32 outOfRange = 0;
    for (int32 step = 1; step <= 9000; ++step)
    {
        const double c = PlumeColourScalar(static_cast<double>(step) * 0.001, false);
        const double delta = FMath::Abs(c - previous);
        maxStep = FMath::Max(maxStep, delta);
        jumps += delta > 0.002 ? 1 : 0;
        outOfRange += (c < 0.0 || c > 1.0) ? 1 : 0;
        previous = c;
    }
    TestEqual(TEXT("sweep: no |dc| above 0.002"), jumps, 0);
    TestTrue(FString::Printf(TEXT("sweep: largest step %.6f <= 0.002"), maxStep), maxStep <= 0.002);
    TestEqual(TEXT("sweep: c within [0, 1]"), outOfRange, 0);

    // The three keys
    const FSOLPlumeColour blue = PlumeColour(0.0);
    TestTrue(TEXT("c 0: rim blue"), ThrusterColourNear(blue.Rim, 0.35, 0.60, 1.00, THRUSTER_TOL));
    TestTrue(TEXT("c 0: core blue"), ThrusterColourNear(blue.Core, 0.80, 0.90, 1.00, THRUSTER_TOL));
    const FSOLPlumeColour yellow = PlumeColour(0.35);
    TestTrue(TEXT("c 0.35: rim yellow"), ThrusterColourNear(yellow.Rim, 1.00, 0.80, 0.35, THRUSTER_TOL));
    TestTrue(TEXT("c 0.35: core yellow"), ThrusterColourNear(yellow.Core, 1.00, 0.96, 0.82, THRUSTER_TOL));
    const FSOLPlumeColour red = PlumeColour(1.0);
    TestTrue(TEXT("c 1: rim red"), ThrusterColourNear(red.Rim, 1.00, 0.38, 0.10, THRUSTER_TOL));
    TestTrue(TEXT("c 1: core red"), ThrusterColourNear(red.Core, 1.00, 0.82, 0.55, THRUSTER_TOL));

    // Linear between the keys
    const FSOLPlumeColour blueYellow = PlumeColour(0.175);
    TestTrue(TEXT("c 0.175: rim halfway blue-yellow"),
        ThrusterColourNear(blueYellow.Rim, 0.675, 0.70, 0.675, THRUSTER_TOL));
    TestTrue(TEXT("c 0.175: core halfway blue-yellow"),
        ThrusterColourNear(blueYellow.Core, 0.90, 0.93, 0.91, THRUSTER_TOL));
    const FSOLPlumeColour yellowRed = PlumeColour(0.675);
    TestTrue(TEXT("c 0.675: rim halfway yellow-red"),
        ThrusterColourNear(yellowRed.Rim, 1.0, 0.59, 0.225, THRUSTER_TOL));
    TestTrue(TEXT("c 0.675: core halfway yellow-red"),
        ThrusterColourNear(yellowRed.Core, 1.0, 0.89, 0.685, THRUSTER_TOL));

    // Out-of-range c is clamped
    const FSOLPlumeColour below = PlumeColour(-0.5);
    TestTrue(TEXT("c -0.5: rim as c 0"), ThrusterColourNear(below.Rim, 0.35, 0.60, 1.00, THRUSTER_TOL));
    TestTrue(TEXT("c -0.5: core as c 0"), ThrusterColourNear(below.Core, 0.80, 0.90, 1.00, THRUSTER_TOL));
    const FSOLPlumeColour above = PlumeColour(1.5);
    TestTrue(TEXT("c 1.5: rim as c 1"), ThrusterColourNear(above.Rim, 1.00, 0.38, 0.10, THRUSTER_TOL));
    TestTrue(TEXT("c 1.5: core as c 1"), ThrusterColourNear(above.Core, 1.00, 0.82, 0.55, THRUSTER_TOL));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterShockKnotsTest, "SOLTest.Thruster.ShockKnots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 4: knot spacing S = 2 r_n (1 + 0.6 b), knots at k S while k S <= 0.8 L_p with strength 0.82^(k-1), the three
// section 4.4 examples, none when off, and a short output view filled and no further
bool FSOLThrusterShockKnotsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const int32 capacity = static_cast<int32>(SOL::Thruster::SHOCK_MAX_KNOTS);

    // Spacing
    TestEqual(TEXT("spacing (1.5, 0.49247) = 3.88645"), ShockSpacingM(1.5, 0.49247), 3.88645, THRUSTER_LEN_TOL);
    TestEqual(TEXT("spacing (0.8, 0.66405) = 2.23749"), ShockSpacingM(0.8, 0.66405), 2.23749, THRUSTER_LEN_TOL);
    TestEqual(TEXT("spacing (0.8, 1.0) = 2.56"), ShockSpacingM(0.8, 1.0), 2.56, THRUSTER_LEN_TOL);

    // NPC at 1.65 G: the second knot (7.773 m) lies past 0.8 L_p (7.727 m)
    TArray<FSOLShockKnot> knots;
    knots.SetNum(capacity);
    const int32 npcCount = ShockKnots(9.659011, 1.5, 0.492473, MakeArrayView(knots));
    TestEqual(TEXT("NPC: 1 knot"), npcCount, 1);
    if (npcCount >= 1)
    {
        TestEqual(TEXT("NPC: knot 1 at 3.88645 m"), knots[0].PositionM, 3.88645, THRUSTER_LEN_TOL);
        TestEqual(TEXT("NPC: knot 1 strength 1"), knots[0].Strength, 1.0, THRUSTER_TOL);
    }

    // Player at 3 G
    knots.SetNum(capacity);
    const int32 playerCount = ShockKnots(10.388462, 0.8, 0.664050, MakeArrayView(knots));
    TestEqual(TEXT("player 3 G: 3 knots"), playerCount, 3);
    const double playerPositions[] = { 2.23749, 4.47498, 6.71246 };
    const double playerStrengths[] = { 1.0, 0.82, 0.6724 };
    for (int32 index = 0; index < FMath::Min(playerCount, 3); ++index)
    {
        TestEqual(FString::Printf(TEXT("player 3 G: knot %d position"), index + 1), knots[index].PositionM,
            playerPositions[index], THRUSTER_LEN_TOL);
        TestEqual(FString::Printf(TEXT("player 3 G: knot %d strength"), index + 1), knots[index].Strength,
            playerStrengths[index], THRUSTER_TOL);
    }

    // Player at War power (G_REF): 6 knots, the 7th (17.92 m) past 0.8 x 20 m
    knots.SetNum(capacity);
    const int32 warCount = ShockKnots(20.0, 0.8, 1.0, MakeArrayView(knots));
    TestEqual(TEXT("War power: 6 knots"), warCount, 6);
    for (int32 index = 0; index < FMath::Min(warCount, 6); ++index)
    {
        TestEqual(FString::Printf(TEXT("War power: knot %d position"), index + 1), knots[index].PositionM,
            2.56 * static_cast<double>(index + 1), THRUSTER_LEN_TOL);
        TestEqual(FString::Printf(TEXT("War power: knot %d strength"), index + 1), knots[index].Strength,
            FMath::Pow(0.82, static_cast<double>(index)), THRUSTER_TOL);
    }
    if (warCount >= 6)
    {
        TestEqual(TEXT("War power: last strength 0.37074"), knots[5].Strength, 0.37074, THRUSTER_TOL);
    }

    // Off: no knots
    knots.SetNum(capacity);
    TestEqual(TEXT("plume length 0: 0 knots"), ShockKnots(0.0, 0.8, 1.0, MakeArrayView(knots)), 0);

    // A view shorter than the knot count: fills the view, returns its size, writes nothing past it
    TArray<FSOLShockKnot> sentinels;
    sentinels.SetNum(capacity);
    for (FSOLShockKnot& knot : sentinels)
    {
        knot.PositionM = -1.0;
        knot.Strength = -1.0;
    }
    const int32 shortCount = ShockKnots(20.0, 0.8, 1.0, MakeArrayView(sentinels.GetData(), 3));
    TestEqual(TEXT("short view: returns the view size 3"), shortCount, 3);
    for (int32 index = 0; index < 3; ++index)
    {
        TestEqual(FString::Printf(TEXT("short view: knot %d position"), index + 1), sentinels[index].PositionM,
            2.56 * static_cast<double>(index + 1), THRUSTER_LEN_TOL);
    }
    TestEqual(TEXT("short view: element past the view untouched (position)"), sentinels[3].PositionM, -1.0,
        THRUSTER_TOL);
    TestEqual(TEXT("short view: element past the view untouched (strength)"), sentinels[3].Strength, -1.0,
        THRUSTER_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterLaneThrustTest, "SOLTest.Thruster.LaneThrust",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 5: a lane ship's thrust in the hull frame in G (AccelMps2 x Direction / g0 projected on forward, right = top x
// forward, top): Accelerate and Brake along the hull, Coast and Flip zero, a Turn split into main and side, bad input 0
bool FSOLThrusterLaneThrustTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const double g0 = SOL::STANDARD_GRAVITY_MPS2;
    const FVector3d forward(1.0, 0.0, 0.0);
    const FVector3d top(0.0, 0.0, 1.0);

    // Accelerate along the hull
    const FSOLLaneThrustState accelerate = ThrusterLaneState(ESOLLaneThrust::Accelerate, 1.65 * g0,
        FVector3d(1.0, 0.0, 0.0), forward);
    TestTrue(TEXT("Accelerate: (1.65, 0, 0)"),
        ThrusterVecNear(LaneThrustLocalG(accelerate, forward, top), 1.65, 0.0, 0.0, THRUSTER_TOL));

    // Brake: thrust against travel with the hull flipped to face it
    const FVector3d flipped(-1.0, 0.0, 0.0);
    const FSOLLaneThrustState brake = ThrusterLaneState(ESOLLaneThrust::Brake, 1.65 * g0, FVector3d(-1.0, 0.0, 0.0),
        flipped, 1.0);
    TestTrue(TEXT("Brake: (1.65, 0, 0)"),
        ThrusterVecNear(LaneThrustLocalG(brake, flipped, top), 1.65, 0.0, 0.0, THRUSTER_TOL));

    // Coast and Flip: no thrust, whatever the direction
    const FVector3d anyDirections[] = { FVector3d(1.0, 0.0, 0.0), FVector3d(0.6, 0.8, 0.0), FVector3d(0.0, 0.0, -1.0) };
    for (const FVector3d& direction : anyDirections)
    {
        const FString label = FString::Printf(TEXT("direction (%.1f, %.1f, %.1f)"), direction.X, direction.Y,
            direction.Z);
        const FSOLLaneThrustState coast = ThrusterLaneState(ESOLLaneThrust::Coast, 1.65 * g0, direction, forward);
        TestTrue(TEXT("Coast: 0, ") + label,
            ThrusterVecNear(LaneThrustLocalG(coast, forward, top), 0.0, 0.0, 0.0, THRUSTER_TOL));
        const FSOLLaneThrustState flip = ThrusterLaneState(ESOLLaneThrust::Flip, 1.65 * g0, direction, forward, 0.5);
        TestTrue(TEXT("Flip: 0, ") + label,
            ThrusterVecNear(LaneThrustLocalG(flip, forward, top), 0.0, 0.0, 0.0, THRUSTER_TOL));
    }

    // Turn: the along-hull part is main thrust, the rest goes to the side (right = top x forward = +Y)
    const FSOLLaneThrustState turnRight = ThrusterLaneState(ESOLLaneThrust::Turn, g0, FVector3d(0.6, 0.8, 0.0),
        forward);
    TestTrue(TEXT("Turn (0.6, 0.8, 0): (0.6, 0.8, 0)"),
        ThrusterVecNear(LaneThrustLocalG(turnRight, forward, top), 0.6, 0.8, 0.0, THRUSTER_TOL));
    const FSOLLaneThrustState turnLeft = ThrusterLaneState(ESOLLaneThrust::Turn, g0, FVector3d(-0.6, -0.8, 0.0),
        forward);
    TestTrue(TEXT("Turn (-0.6, -0.8, 0): (-0.6, -0.8, 0)"),
        ThrusterVecNear(LaneThrustLocalG(turnLeft, forward, top), -0.6, -0.8, 0.0, THRUSTER_TOL));

    // Non-finite acceleration or direction: no thrust
    const ESOLLaneThrust thrustingKinds[] = { ESOLLaneThrust::Accelerate, ESOLLaneThrust::Brake, ESOLLaneThrust::Turn };
    for (const ESOLLaneThrust kind : thrustingKinds)
    {
        const FString label = FString::Printf(TEXT("kind %d"), static_cast<int32>(kind));
        TestTrue(label + TEXT(": NaN accel -> 0"), ThrusterVecNear(LaneThrustLocalG(
            ThrusterLaneState(kind, THRUSTER_NAN, FVector3d(1.0, 0.0, 0.0), forward), forward, top),
            0.0, 0.0, 0.0, THRUSTER_TOL));
        TestTrue(label + TEXT(": +inf accel -> 0"), ThrusterVecNear(LaneThrustLocalG(
            ThrusterLaneState(kind, THRUSTER_INF, FVector3d(1.0, 0.0, 0.0), forward), forward, top),
            0.0, 0.0, 0.0, THRUSTER_TOL));
        TestTrue(label + TEXT(": NaN direction -> 0"), ThrusterVecNear(LaneThrustLocalG(
            ThrusterLaneState(kind, g0, FVector3d(THRUSTER_NAN, 0.0, 0.0), forward), forward, top),
            0.0, 0.0, 0.0, THRUSTER_TOL));
        TestTrue(label + TEXT(": inf direction -> 0"), ThrusterVecNear(LaneThrustLocalG(
            ThrusterLaneState(kind, g0, FVector3d(0.6, THRUSTER_INF, 0.0), forward), forward, top),
            0.0, 0.0, 0.0, THRUSTER_TOL));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterPlayerThrustTest, "SOLTest.Thruster.PlayerThrust",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 6: the player's thrust is the time-weighted mean of the step's substeps (m/s^2 in, G out), bad samples ignored,
// zero when empty or reset; ThrustAxes splits a hull-frame vector into main (forward only) and the five burst axes
bool FSOLThrusterPlayerThrustTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const double g0 = SOL::STANDARD_GRAVITY_MPS2;
    const double gRef = static_cast<double>(SOL::Thruster::G_REF);

    // Time-weighted mean
    {
        FSOLThrustMean mean;
        mean.Add(FVector3d(3.0 * g0, 0.0, 0.0), 0.004);
        mean.Add(FVector3d(0.0, 0.0, 0.0), 0.012);
        TestTrue(TEXT("mean of 3 G for 4 ms and 0 for 12 ms: (0.75, 0, 0)"),
            ThrusterVecNear(mean.MeanG(), 0.75, 0.0, 0.0, THRUSTER_TOL));
    }
    {
        FSOLThrustMean mean;
        mean.Add(FVector3d(gRef * g0, 0.0, 0.0), 0.01);
        mean.Add(FVector3d(1.5 * g0, 0.0, 0.0), 0.01);
        TestTrue(TEXT("mean of G_REF and 1.5 G: (4.151651, 0, 0)"),
            ThrusterVecNear(mean.MeanG(), 4.151651, 0.0, 0.0, THRUSTER_TOL));
    }

    // Empty, bad samples ignored, reset
    {
        FSOLThrustMean mean;
        TestTrue(TEXT("no samples: (0, 0, 0)"), ThrusterVecNear(mean.MeanG(), 0.0, 0.0, 0.0, THRUSTER_TOL));
        mean.Add(FVector3d(3.0 * g0, 0.0, 0.0), 0.01);
        mean.Add(FVector3d(9.0 * g0, 0.0, 0.0), 0.0);
        mean.Add(FVector3d(9.0 * g0, 0.0, 0.0), -0.01);
        mean.Add(FVector3d(9.0 * g0, 0.0, 0.0), THRUSTER_NAN);
        mean.Add(FVector3d(9.0 * g0, 0.0, 0.0), THRUSTER_INF);
        mean.Add(FVector3d(THRUSTER_NAN, 0.0, 0.0), 0.01);
        mean.Add(FVector3d(0.0, THRUSTER_INF, 0.0), 0.01);
        mean.Add(FVector3d(0.0, 0.0, -THRUSTER_INF), 0.01);
        TestTrue(TEXT("bad dt or vector ignored: mean stays (3, 0, 0)"),
            ThrusterVecNear(mean.MeanG(), 3.0, 0.0, 0.0, THRUSTER_TOL));
        mean.Reset();
        TestTrue(TEXT("after Reset: (0, 0, 0)"), ThrusterVecNear(mean.MeanG(), 0.0, 0.0, 0.0, THRUSTER_TOL));
        mean.Add(FVector3d(g0, 0.0, 0.0), 0.02);
        TestTrue(TEXT("after Reset, new samples only: (1, 0, 0)"),
            ThrusterVecNear(mean.MeanG(), 1.0, 0.0, 0.0, THRUSTER_TOL));
    }
    {
        FSOLThrustMean mean;
        mean.Add(FVector3d(9.0 * g0, 0.0, 0.0), 0.0);
        mean.Add(FVector3d(THRUSTER_NAN, 0.0, 0.0), 0.01);
        TestTrue(TEXT("only bad samples: (0, 0, 0)"), ThrusterVecNear(mean.MeanG(), 0.0, 0.0, 0.0, THRUSTER_TOL));
    }

    // Axes split
    const FSOLThrustAxes mixed = ThrustAxes(FVector3f(0.5f, -0.3f, 0.2f));
    TestEqual(TEXT("(0.5, -0.3, 0.2): Main 0.5"), static_cast<double>(mixed.Main), 0.5, THRUSTER_TOL);
    TestEqual(TEXT("(0.5, -0.3, 0.2): Reverse 0"), static_cast<double>(mixed.Reverse), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(0.5, -0.3, 0.2): Right 0"), static_cast<double>(mixed.Right), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(0.5, -0.3, 0.2): Left 0.3"), static_cast<double>(mixed.Left), 0.3, THRUSTER_TOL);
    TestEqual(TEXT("(0.5, -0.3, 0.2): Up 0.2"), static_cast<double>(mixed.Up), 0.2, THRUSTER_TOL);
    TestEqual(TEXT("(0.5, -0.3, 0.2): Down 0"), static_cast<double>(mixed.Down), 0.0, THRUSTER_TOL);

    const FSOLThrustAxes backDown = ThrustAxes(FVector3f(-1.5f, 0.0f, -2.0f));
    TestEqual(TEXT("(-1.5, 0, -2): Main 0"), static_cast<double>(backDown.Main), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(-1.5, 0, -2): Reverse 1.5"), static_cast<double>(backDown.Reverse), 1.5, THRUSTER_TOL);
    TestEqual(TEXT("(-1.5, 0, -2): Right 0"), static_cast<double>(backDown.Right), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(-1.5, 0, -2): Left 0"), static_cast<double>(backDown.Left), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(-1.5, 0, -2): Up 0"), static_cast<double>(backDown.Up), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(-1.5, 0, -2): Down 2"), static_cast<double>(backDown.Down), 2.0, THRUSTER_TOL);

    const FSOLThrustAxes pureReverse = ThrustAxes(FVector3f(-3.0f, 0.0f, 0.0f));
    TestEqual(TEXT("pure reverse: Main 0"), static_cast<double>(pureReverse.Main), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("pure reverse: Reverse 3"), static_cast<double>(pureReverse.Reverse), 3.0, THRUSTER_TOL);

    // A non-finite component zeroes that component's axes only
    const FSOLThrustAxes nanX = ThrustAxes(FVector3f(THRUSTER_NAN_F, 0.5f, -THRUSTER_INF_F));
    TestEqual(TEXT("(NaN, 0.5, -inf): Main 0"), static_cast<double>(nanX.Main), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(NaN, 0.5, -inf): Reverse 0"), static_cast<double>(nanX.Reverse), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(NaN, 0.5, -inf): Right 0.5"), static_cast<double>(nanX.Right), 0.5, THRUSTER_TOL);
    TestEqual(TEXT("(NaN, 0.5, -inf): Left 0"), static_cast<double>(nanX.Left), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(NaN, 0.5, -inf): Up 0"), static_cast<double>(nanX.Up), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(NaN, 0.5, -inf): Down 0"), static_cast<double>(nanX.Down), 0.0, THRUSTER_TOL);
    const FSOLThrustAxes nanY = ThrustAxes(FVector3f(0.2f, THRUSTER_NAN_F, -0.4f));
    TestEqual(TEXT("(0.2, NaN, -0.4): Main 0.2"), static_cast<double>(nanY.Main), 0.2, THRUSTER_TOL);
    TestEqual(TEXT("(0.2, NaN, -0.4): Right 0"), static_cast<double>(nanY.Right), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(0.2, NaN, -0.4): Left 0"), static_cast<double>(nanY.Left), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("(0.2, NaN, -0.4): Down 0.4"), static_cast<double>(nanY.Down), 0.4, THRUSTER_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterRibbonHistoryTest, "SOLTest.Thruster.RibbonHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 7: the fixed 24-sample ribbon history (sampling interval, wrap, age order, reset, non-finite pushes refused),
// the drift speed 3 L_p / A, the drawn point (ship now + offset + exhaust x drift x age), linear alpha and the width
bool FSOLThrusterRibbonHistoryTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;

    // Returns a sample with every field set: offset (-15, 0, 0) m, exhaust -X, drift 90 m/s, brightness 0.8 by default
    const auto makeSample = [](const double emitTimeS)
    {
        FSOLPlumeSample sample;
        sample.EmitTimeS = emitTimeS;
        sample.OffsetM = FVector3f(-15.0f, 0.0f, 0.0f);
        sample.ExhaustDir = FVector3f(-1.0f, 0.0f, 0.0f);
        sample.DriftMps = 90.0f;
        sample.Brightness = 0.8f;
        return sample;
    };

    // Empty history and the sampling interval A / 24
    FSOLPlumeHistory history;
    history.Reset();
    TestEqual(TEXT("empty: Num 0"), history.Num(), 0);
    TestTrue(TEXT("empty: ShouldSample(0)"), history.ShouldSample(0.0));
    TestTrue(TEXT("empty: ShouldSample(-5)"), history.ShouldSample(-5.0));
    TestTrue(TEXT("empty: ShouldSample(1e6)"), history.ShouldSample(1.0e6));
    TestEqual(TEXT("interval A / 24 = 0.041667 s"),
        static_cast<double>(SOL::Thruster::RIBBON_MAX_AGE_S) / static_cast<double>(SOL::Thruster::RIBBON_SAMPLES),
        0.041667, 1.0e-6);
    TestTrue(TEXT("push at t 0 accepted"), history.Push(makeSample(0.0)));
    TestEqual(TEXT("after one push: Num 1"), history.Num(), 1);
    TestFalse(TEXT("ShouldSample(0.03) false"), history.ShouldSample(0.03));
    TestTrue(TEXT("ShouldSample(0.0417) true"), history.ShouldSample(0.0417));

    // Wrap: 30 pushes at t = k / 24 keep the newest 24, newest first
    history.Reset();
    int32 refused = 0;
    for (int32 k = 1; k <= 30; ++k)
    {
        refused += history.Push(makeSample(static_cast<double>(k) / 24.0)) ? 0 : 1;
    }
    TestEqual(TEXT("30 pushes: all accepted"), refused, 0);
    TestEqual(TEXT("30 pushes: Num 24"), history.Num(), 24);
    if (history.Num() == 24)
    {
        TestEqual(TEXT("ByAge(0) is t 30 / 24"), history.ByAge(0).EmitTimeS, 30.0 / 24.0, 1.0e-9);
        TestEqual(TEXT("ByAge(23) is t 7 / 24"), history.ByAge(23).EmitTimeS, 7.0 / 24.0, 1.0e-9);
        int32 misordered = 0;
        for (int32 index = 0; index < 24; ++index)
        {
            const double expected = static_cast<double>(30 - index) / 24.0;
            misordered += FMath::Abs(history.ByAge(index).EmitTimeS - expected) <= 1.0e-9 ? 0 : 1;
        }
        TestEqual(TEXT("ages ordered newest to oldest"), misordered, 0);
    }
    TestFalse(TEXT("after the wrap: ShouldSample 0.03 s after the newest is false"),
        history.ShouldSample(30.0 / 24.0 + 0.03));

    // Reset and refused pushes
    history.Reset();
    TestEqual(TEXT("Reset: Num 0"), history.Num(), 0);
    FSOLPlumeSample nanOffset = makeSample(1.0);
    nanOffset.OffsetM = FVector3f(THRUSTER_NAN_F, 0.0f, 0.0f);
    TestFalse(TEXT("NaN offset on empty: refused"), history.Push(nanOffset));
    TestEqual(TEXT("NaN offset on empty: Num unchanged"), history.Num(), 0);
    for (int32 k = 0; k < 5; ++k)
    {
        history.Push(makeSample(static_cast<double>(k) / 24.0));
    }
    TestEqual(TEXT("5 valid pushes: Num 5"), history.Num(), 5);
    TestFalse(TEXT("NaN offset: refused"), history.Push(nanOffset));
    FSOLPlumeSample nanTime = makeSample(THRUSTER_NAN);
    TestFalse(TEXT("NaN emit time: refused"), history.Push(nanTime));
    FSOLPlumeSample infExhaust = makeSample(1.0);
    infExhaust.ExhaustDir = FVector3f(0.0f, THRUSTER_INF_F, 0.0f);
    TestFalse(TEXT("inf exhaust: refused"), history.Push(infExhaust));
    FSOLPlumeSample nanDrift = makeSample(1.0);
    nanDrift.DriftMps = THRUSTER_NAN_F;
    TestFalse(TEXT("NaN drift: refused"), history.Push(nanDrift));
    FSOLPlumeSample nanBrightness = makeSample(1.0);
    nanBrightness.Brightness = THRUSTER_NAN_F;
    TestFalse(TEXT("NaN brightness: refused"), history.Push(nanBrightness));
    TestEqual(TEXT("refused pushes: Num still 5"), history.Num(), 5);

    // Drift speed 3 L_p / A
    TestEqual(TEXT("drift for a 30 m plume: 90 m/s"), static_cast<double>(RibbonDriftMps(30.0)), 90.0,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("drift for a 9.659011 m plume: 28.977 m/s"), static_cast<double>(RibbonDriftMps(9.659011)),
        28.977, THRUSTER_LEN_TOL);

    // The drawn point
    const FVector3d ship(1000.0, 2000.0, 3000.0);
    const FSOLPlumeSample sample = makeSample(10.0);
    TestTrue(TEXT("point at age 0.5 s: (940, 2000, 3000)"),
        ThrusterVecNearD(RibbonPoint(ship, sample, 10.5), 940.0, 2000.0, 3000.0, THRUSTER_LEN_TOL));
    const FVector3d fullAge = RibbonPoint(ship, sample, 11.0);
    TestTrue(TEXT("point at age 1 s: (895, 2000, 3000)"),
        ThrusterVecNearD(fullAge, 895.0, 2000.0, 3000.0, THRUSTER_LEN_TOL));
    TestEqual(TEXT("full age: 90 m from the offset, 3x a 30 m plume"),
        (fullAge - (ship + FVector3d(-15.0, 0.0, 0.0))).Size(), 3.0 * 30.0, THRUSTER_LEN_TOL);
    TestTrue(TEXT("before emission (age read as 0): (985, 2000, 3000)"),
        ThrusterVecNearD(RibbonPoint(ship, sample, 9.0), 985.0, 2000.0, 3000.0, THRUSTER_LEN_TOL));

    // Linear alpha (1 - age / A) x b
    TestEqual(TEXT("alpha (0, 0.8) = 0.8"), static_cast<double>(RibbonAlpha(0.0, 0.8f)), 0.8, THRUSTER_TOL);
    TestEqual(TEXT("alpha (0.5, 0.8) = 0.4"), static_cast<double>(RibbonAlpha(0.5, 0.8f)), 0.4, THRUSTER_TOL);
    TestEqual(TEXT("alpha (1.0, 0.8) = 0"), static_cast<double>(RibbonAlpha(1.0, 0.8f)), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("alpha (1.5, 0.8) = 0"), static_cast<double>(RibbonAlpha(1.5, 0.8f)), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("alpha (-0.1, 0.8) = 0.8"), static_cast<double>(RibbonAlpha(-0.1, 0.8f)), 0.8, THRUSTER_TOL);

    // Width r_n at the nozzle to 2 r_n at full age
    TestEqual(TEXT("width (0, 1.5) = 1.5"), static_cast<double>(RibbonWidthM(0.0, 1.5)), 1.5, THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (0.5, 1.5) = 2.25"), static_cast<double>(RibbonWidthM(0.5, 1.5)), 2.25, THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (1.0, 1.5) = 3.0"), static_cast<double>(RibbonWidthM(1.0, 1.5)), 3.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (2.0, 1.5) = 3.0"), static_cast<double>(RibbonWidthM(2.0, 1.5)), 3.0, THRUSTER_LEN_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterPixelLodTest, "SOLTest.Thruster.PixelLod",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8: the reference focal length, the pixel length f L / d, the linear fade band (a test parameter), the evaluation
// gate distance and the 1 px glow floor
bool FSOLThrusterPixelLodTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;

    // Reference focal length
    TestEqual(TEXT("focal (90, 16/9) = 960"), FocalRefPx(90.0, 16.0 / 9.0), 960.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("focal (90, 4/3) = 720"), FocalRefPx(90.0, 4.0 / 3.0), 720.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("focal (60, 16/9) = 1662.7688"), FocalRefPx(60.0, 16.0 / 9.0), 1662.7688, 1.0e-3);

    // Pixel length: the section 4.5 one-pixel distances
    TestEqual(TEXT("1.65 G NPC cone at 9272.65 m: 1 px"), PixelLength(9.659011, 9272.65, 960.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("30 m cone at 28.8 km: 1 px"), PixelLength(30.0, 28800.0, 960.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("20 m cone at 19.2 km: 1 px"), PixelLength(20.0, 19200.0, 960.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("distance 0: 0"), PixelLength(30.0, 0.0, 960.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("negative distance: 0"), PixelLength(30.0, -100.0, 960.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("NaN distance: 0"), PixelLength(30.0, THRUSTER_NAN, 960.0), 0.0, THRUSTER_TOL);

    // Fade band (0.5, 1.0) and (6, 8), test parameters
    TestEqual(TEXT("band (0.5, 1): 0.4 px -> 0"), LodAlpha(0.4, 0.5, 1.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("band (0.5, 1): 0.5 px -> 0"), LodAlpha(0.5, 0.5, 1.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("band (0.5, 1): 0.75 px -> 0.5"), LodAlpha(0.75, 0.5, 1.0), 0.5, THRUSTER_TOL);
    TestEqual(TEXT("band (0.5, 1): 1 px -> 1"), LodAlpha(1.0, 0.5, 1.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("band (0.5, 1): 3 px -> 1"), LodAlpha(3.0, 0.5, 1.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("band (6, 8): 7 px -> 0.5"), LodAlpha(7.0, 6.0, 8.0), 0.5, THRUSTER_TOL);

    // Degenerate band: a step at high
    TestEqual(TEXT("band (1, 1): 1 px -> 1"), LodAlpha(1.0, 1.0, 1.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("band (1, 1): 0.99 px -> 0"), LodAlpha(0.99, 1.0, 1.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("band (2, 1): 1.5 px -> 1"), LodAlpha(1.5, 2.0, 1.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("band (2, 1): 0.5 px -> 0"), LodAlpha(0.5, 2.0, 1.0), 0.0, THRUSTER_TOL);

    // Evaluation gate: the distance where the longest plume (the hull length) falls to lowPx
    TestEqual(TEXT("gate (30, 960, 0.5) = 57600 m"), PlumeMaxDistanceM(30.0, 960.0, 0.5), 57600.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("gate (30, 960, 1) = 28800 m"), PlumeMaxDistanceM(30.0, 960.0, 1.0), 28800.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("gate lowPx 0: 0"), PlumeMaxDistanceM(30.0, 960.0, 0.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("gate lowPx negative: 0"), PlumeMaxDistanceM(30.0, 960.0, -1.0), 0.0, THRUSTER_LEN_TOL);

    // Glow floor
    TestEqual(TEXT("glow 0.9 px drawn at the 1 px floor"), GlowDrawnPx(0.9, 1.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("glow 3 px drawn at 3 px"), GlowDrawnPx(3.0, 1.0), 3.0, THRUSTER_TOL);
    TestEqual(TEXT("glow 0.9 px at the GLOW_MIN_DRAWN_PX floor"),
        GlowDrawnPx(0.9, static_cast<double>(SOL::Thruster::GLOW_MIN_DRAWN_PX)), 1.0, THRUSTER_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterNearestRibbonsTest, "SOLTest.Thruster.NearestRibbons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 9: ribbon ships are those within R (inclusive) with a plume or a fading ribbon, ranked by ribbon pixel length,
// ties by lower NPC index, up to maxCount (unlimited at RIBBON_COUNT_UNLIMITED, none at 0 or below); the output is
// cleared first
bool FSOLThrusterNearestRibbonsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    constexpr double radiusM = 5000.0;                              // Test parameter (start value 5 km)

    // Returns a candidate with every field set
    const auto makeCandidate = [](const int32 npcIndex, const double distanceM, const double ribbonPx,
        const bool bHasPlume, const bool bRibbonFading)
    {
        FSOLRibbonCandidate candidate;
        candidate.NpcIndex = npcIndex;
        candidate.DistanceM = distanceM;
        candidate.RibbonPx = ribbonPx;
        candidate.bHasPlume = bHasPlume;
        candidate.bRibbonFading = bRibbonFading;
        return candidate;
    };

    const TArray<FSOLRibbonCandidate> candidates = {
        makeCandidate(5, 1000.0, 40.0, true, false),
        makeCandidate(2, 2000.0, 40.0, true, false),
        makeCandidate(9, 4000.0, 10.0, false, true),
        makeCandidate(7, 3000.0, 20.0, false, false),
        makeCandidate(1, 6000.0, 50.0, true, false),
        makeCandidate(3, 5000.0, 5.0, true, false),
    };
    const TConstArrayView<FSOLRibbonCandidate> view(candidates);

    // Unlimited: 7 has no plume and no fading ribbon, 1 is beyond R, 3 at exactly R is in
    TArray<int32> selected = { 99, 98, 97, 96, 95, 94, 93 };
    SelectRibbonShips(view, radiusM, static_cast<int32>(SOL::Thruster::RIBBON_COUNT_UNLIMITED), selected);
    TestTrue(TEXT("unlimited: [2, 5, 9, 3]"), ThrusterSameIndices(selected, { 2, 5, 9, 3 }));

    // A count limit keeps the top of the ranking
    selected = { 99, 98, 97, 96, 95, 94, 93 };
    SelectRibbonShips(view, radiusM, 2, selected);
    TestTrue(TEXT("maxCount 2: [2, 5]"), ThrusterSameIndices(selected, { 2, 5 }));

    // No ribbons at 0 or a negative count
    selected = { 99, 98, 97 };
    SelectRibbonShips(view, radiusM, 0, selected);
    TestEqual(TEXT("maxCount 0: empty"), selected.Num(), 0);
    selected = { 99, 98, 97 };
    SelectRibbonShips(view, radiusM, -1, selected);
    TestEqual(TEXT("maxCount -1: empty"), selected.Num(), 0);
    selected = { 99, 98, 97 };
    SelectRibbonShips(view, radiusM, MIN_int32, selected);
    TestEqual(TEXT("maxCount INT32_MIN: empty"), selected.Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterQualityScalingTest, "SOLTest.Thruster.QualityScaling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 10: ribbon count per quality preset (Low 0, Medium 8, High 16, Epic 32, Cinematic unlimited with no cap at all,
// out-of-range presets clamped) and the player's ribbon off only at Low
bool FSOLThrusterQualityScalingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const int32 unlimited = static_cast<int32>(SOL::Thruster::RIBBON_COUNT_UNLIMITED);

    TestEqual(TEXT("Low (0): 0"), RibbonCountForQuality(0), 0);
    TestEqual(TEXT("Medium (1): 8"), RibbonCountForQuality(1), 8);
    TestEqual(TEXT("High (2): 16"), RibbonCountForQuality(2), 16);
    TestEqual(TEXT("Epic (3): 32"), RibbonCountForQuality(3), 32);
    TestEqual(TEXT("Cinematic (4): unlimited"), RibbonCountForQuality(4), unlimited);
    TestEqual(TEXT("Cinematic is MAX_int32, no cap"), RibbonCountForQuality(4), MAX_int32);
    TestEqual(TEXT("-1 read as Low"), RibbonCountForQuality(-1), 0);
    TestEqual(TEXT("7 read as Cinematic"), RibbonCountForQuality(7), unlimited);

    TestFalse(TEXT("player ribbon: off at Low"), PlayerHasRibbon(0));
    for (int32 quality = 1; quality <= 4; ++quality)
    {
        TestTrue(FString::Printf(TEXT("player ribbon: on at quality %d"), quality), PlayerHasRibbon(quality));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterConstantsTest, "SOLTest.Thruster.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 11: the contract's constants (asserted so a tuning change is a visible test edit), their ordering invariants
// and the per-hull nozzle table
bool FSOLThrusterConstantsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const double gRef = static_cast<double>(SOL::Thruster::G_REF);

    // Reference G and the colour keys
    TestTrue(TEXT("G_REF == WAR_POWER_PULSE_G exactly"), gRef == static_cast<double>(SOL::WAR_POWER_PULSE_G));
    TestEqual(TEXT("G_REF about 6.803301 G"), gRef, 6.803301, 1.0e-6);
    TestEqual(TEXT("PLUME_MIN_G 0.01"), static_cast<double>(SOL::Thruster::PLUME_MIN_G), 0.01, THRUSTER_TOL);
    const double blueEnd = static_cast<double>(SOL::Thruster::COLOUR_BLUE_END_G);
    const double yellowG = static_cast<double>(SOL::Thruster::COLOUR_YELLOW_G);
    const double yellowC = static_cast<double>(SOL::Thruster::COLOUR_YELLOW_C);
    TestEqual(TEXT("COLOUR_BLUE_END_G 2.5"), blueEnd, 2.5, THRUSTER_TOL);
    TestEqual(TEXT("COLOUR_YELLOW_G 3.0"), yellowG, 3.0, THRUSTER_TOL);
    TestEqual(TEXT("COLOUR_YELLOW_C 0.35"), yellowC, 0.35, THRUSTER_TOL);
    TestTrue(TEXT("BLUE_END < YELLOW_G < G_REF"), blueEnd < yellowG && yellowG < gRef);
    TestTrue(TEXT("0 < COLOUR_YELLOW_C < 1"), yellowC > 0.0 && yellowC < 1.0);

    // Exponents and shock diamonds
    TestEqual(TEXT("LENGTH_EXPONENT 0.8"), static_cast<double>(SOL::Thruster::LENGTH_EXPONENT), 0.8, THRUSTER_TOL);
    TestEqual(TEXT("BRIGHTNESS_EXPONENT 0.5"), static_cast<double>(SOL::Thruster::BRIGHTNESS_EXPONENT), 0.5,
        THRUSTER_TOL);
    TestEqual(TEXT("SHOCK_SPACING_FACTOR 2.0"), static_cast<double>(SOL::Thruster::SHOCK_SPACING_FACTOR), 2.0,
        THRUSTER_TOL);
    TestEqual(TEXT("SHOCK_BRIGHTNESS_GAIN 0.6"), static_cast<double>(SOL::Thruster::SHOCK_BRIGHTNESS_GAIN), 0.6,
        THRUSTER_TOL);
    TestEqual(TEXT("SHOCK_DECAY 0.82"), static_cast<double>(SOL::Thruster::SHOCK_DECAY), 0.82, THRUSTER_TOL);
    TestEqual(TEXT("SHOCK_ZONE 0.8"), static_cast<double>(SOL::Thruster::SHOCK_ZONE), 0.8, THRUSTER_TOL);
    TestEqual(TEXT("SHOCK_MAX_KNOTS 8"), static_cast<int32>(SOL::Thruster::SHOCK_MAX_KNOTS), 8);
    TestEqual(TEXT("TAIL_RADIUS_RATIO 0.35"), static_cast<double>(SOL::Thruster::TAIL_RADIUS_RATIO), 0.35,
        THRUSTER_TOL);

    // Glow, bursts and ribbons
    TestEqual(TEXT("GLOW_MIN_DRAWN_PX 1"), static_cast<double>(SOL::Thruster::GLOW_MIN_DRAWN_PX), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("BURST_LENGTH_HULL_DIVISOR 6"), static_cast<double>(SOL::Thruster::BURST_LENGTH_HULL_DIVISOR), 6.0,
        THRUSTER_TOL);
    TestEqual(TEXT("BURST_REF_G 1.5"), static_cast<double>(SOL::Thruster::BURST_REF_G), 1.5, THRUSTER_TOL);
    const int32 ribbonSamples = static_cast<int32>(SOL::Thruster::RIBBON_SAMPLES);
    TestEqual(TEXT("RIBBON_SAMPLES 24"), ribbonSamples, 24);
    TestTrue(TEXT("RIBBON_SAMPLES within 16-32"), ribbonSamples >= 16 && ribbonSamples <= 32);
    TestEqual(TEXT("RIBBON_MAX_AGE_S 1"), static_cast<double>(SOL::Thruster::RIBBON_MAX_AGE_S), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("RIBBON_LENGTH_FACTOR 3"), static_cast<double>(SOL::Thruster::RIBBON_LENGTH_FACTOR), 3.0,
        THRUSTER_TOL);
    TestEqual(TEXT("RIBBON_COUNT_UNLIMITED is MAX_int32"),
        static_cast<int32>(SOL::Thruster::RIBBON_COUNT_UNLIMITED), MAX_int32);

    // Hull sizes
    TestEqual(TEXT("NPC_HULL_LENGTH_M 30"), static_cast<double>(SOL::Thruster::NPC_HULL_LENGTH_M), 30.0,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("NPC_NOZZLE_RADIUS_M 1.5"), static_cast<double>(SOL::Thruster::NPC_NOZZLE_RADIUS_M), 1.5,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("PLAYER_HULL_LENGTH_M 20"), static_cast<double>(SOL::Thruster::PLAYER_HULL_LENGTH_M), 20.0,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("PLAYER_NOZZLE_RADIUS_M 0.8"), static_cast<double>(SOL::Thruster::PLAYER_NOZZLE_RADIUS_M), 0.8,
        THRUSTER_LEN_TOL);

    // NPC hulls: 30 m, one nozzle of radius 1.5 m exhausting -X
    const ESOLHullKind npcKinds[] = { ESOLHullKind::NpcFaction0, ESOLHullKind::NpcFaction1 };
    for (const ESOLHullKind kind : npcKinds)
    {
        const FString label = FString::Printf(TEXT("NPC faction %d"), kind == ESOLHullKind::NpcFaction0 ? 0 : 1);
        const FSOLHullThrusters& hull = HullThrusters(kind);
        TestEqual(label + TEXT(": hull 30 m"), static_cast<double>(hull.HullLengthM), 30.0, THRUSTER_LEN_TOL);
        TestEqual(label + TEXT(": 1 nozzle"), hull.NozzleCount, 1);
        TestEqual(label + TEXT(": radius 1.5 m"), static_cast<double>(hull.Nozzles[0].RadiusM), 1.5,
            THRUSTER_LEN_TOL);
        TestTrue(label + TEXT(": exhaust (-1, 0, 0)"),
            ThrusterVecNear(hull.Nozzles[0].ExhaustDir, -1.0, 0.0, 0.0, THRUSTER_TOL));
    }

    // Player hull: 20 m, two nozzles at (-8.0, +-2.2, -0.2) m (either order), radius 0.8 m, exhausting -X
    const FSOLHullThrusters& player = HullThrusters(ESOLHullKind::Player);
    TestEqual(TEXT("player: hull 20 m"), static_cast<double>(player.HullLengthM), 20.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("player: 2 nozzles"), player.NozzleCount, 2);
    if (player.NozzleCount == 2)
    {
        for (int32 index = 0; index < 2; ++index)
        {
            const FSOLNozzle& nozzle = player.Nozzles[index];
            const FString label = FString::Printf(TEXT("player nozzle %d"), index);
            TestEqual(label + TEXT(": x -8.0 m"), static_cast<double>(nozzle.PositionM.X), -8.0, 0.05);
            TestEqual(label + TEXT(": |y| 2.2 m"), FMath::Abs(static_cast<double>(nozzle.PositionM.Y)), 2.2, 0.05);
            TestEqual(label + TEXT(": z -0.2 m"), static_cast<double>(nozzle.PositionM.Z), -0.2, 0.05);
            TestEqual(label + TEXT(": radius 0.8 m"), static_cast<double>(nozzle.RadiusM), 0.8, THRUSTER_LEN_TOL);
            TestTrue(label + TEXT(": exhaust (-1, 0, 0)"),
                ThrusterVecNear(nozzle.ExhaustDir, -1.0, 0.0, 0.0, THRUSTER_TOL));
        }
        TestTrue(TEXT("player: one nozzle each side"),
            player.Nozzles[0].PositionM.Y * player.Nozzles[1].PositionM.Y < 0.0f);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterGasBurstsTest, "SOLTest.Thruster.GasBursts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13: the gas leaves opposite to the thrust, the burst strength ramp (threshold and reference as test parameters),
// the main plume and bursts independent, the burst length L_hull / 6 x s, and no plume or burst on dot-drawn ships
bool FSOLThrusterGasBurstsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;

    // Exhaust directions
    TestTrue(TEXT("Reverse exhausts +X (bow)"), ThrusterVecNear(BurstExhaustDir(ESOLBurst::Reverse), 1.0, 0.0, 0.0,
        THRUSTER_TOL));
    TestTrue(TEXT("Right exhausts -Y"), ThrusterVecNear(BurstExhaustDir(ESOLBurst::Right), 0.0, -1.0, 0.0,
        THRUSTER_TOL));
    TestTrue(TEXT("Left exhausts +Y"), ThrusterVecNear(BurstExhaustDir(ESOLBurst::Left), 0.0, 1.0, 0.0,
        THRUSTER_TOL));
    TestTrue(TEXT("Up exhausts -Z"), ThrusterVecNear(BurstExhaustDir(ESOLBurst::Up), 0.0, 0.0, -1.0, THRUSTER_TOL));
    TestTrue(TEXT("Down exhausts +Z"), ThrusterVecNear(BurstExhaustDir(ESOLBurst::Down), 0.0, 0.0, 1.0,
        THRUSTER_TOL));

    // Strength ramp with parameters (0.05, 2.0)
    constexpr float minG = 0.05f;
    constexpr float refG = 2.0f;
    TestEqual(TEXT("0.04 G: 0"), static_cast<double>(BurstStrength(0.04f, minG, refG)), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("0.05 G: 0"), static_cast<double>(BurstStrength(0.05f, minG, refG)), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("1.025 G: 0.5"), static_cast<double>(BurstStrength(1.025f, minG, refG)), 0.5, THRUSTER_TOL);
    TestEqual(TEXT("2.0 G: 1"), static_cast<double>(BurstStrength(2.0f, minG, refG)), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("6.0 G: 1"), static_cast<double>(BurstStrength(6.0f, minG, refG)), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("negative G: 0"), static_cast<double>(BurstStrength(-1.0f, minG, refG)), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("NaN G: 0"), static_cast<double>(BurstStrength(THRUSTER_NAN_F, minG, refG)), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("refG == minG: 0"), static_cast<double>(BurstStrength(1.0f, 2.0f, 2.0f)), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("refG < minG: 0"), static_cast<double>(BurstStrength(1.0f, 2.0f, 1.0f)), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("in game: full strength at BURST_REF_G"), static_cast<double>(BurstStrength(
        static_cast<float>(SOL::Thruster::BURST_REF_G), minG, static_cast<float>(SOL::Thruster::BURST_REF_G))),
        1.0, THRUSTER_TOL);

    // Independence of the main plume and the bursts
    const FSOLThrustAxes forwardOnly = ThrustAxes(FVector3f(2.0f, 0.0f, 0.0f));
    TestEqual(TEXT("pure forward: Main 2"), static_cast<double>(forwardOnly.Main), 2.0, THRUSTER_TOL);
    TestTrue(TEXT("pure forward: no burst axis"), forwardOnly.Reverse == 0.0f && forwardOnly.Right == 0.0f
        && forwardOnly.Left == 0.0f && forwardOnly.Up == 0.0f && forwardOnly.Down == 0.0f);
    const FSOLThrustAxes reverseOnly = ThrustAxes(FVector3f(-1.0f, 0.0f, 0.0f));
    TestEqual(TEXT("pure reverse: Main 0"), static_cast<double>(reverseOnly.Main), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("pure reverse: Reverse 1"), static_cast<double>(reverseOnly.Reverse), 1.0, THRUSTER_TOL);

    // Burst length: hull length / 6 x strength, strength clamped to [0, 1]
    TestEqual(TEXT("length (30, 1) = 5"), BurstLengthM(30.0, 1.0f), 5.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("length (30, 0.5) = 2.5"), BurstLengthM(30.0, 0.5f), 2.5, THRUSTER_LEN_TOL);
    TestEqual(TEXT("length (20, 1) = 3.3333"), BurstLengthM(20.0, 1.0f), 3.3333, THRUSTER_LEN_TOL);
    TestEqual(TEXT("length (30, 2) clamped to 5"), BurstLengthM(30.0, 2.0f), 5.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("length (30, -0.5) = 0"), BurstLengthM(30.0, -0.5f), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("length (30, NaN) = 0"), BurstLengthM(30.0, THRUSTER_NAN_F), 0.0, THRUSTER_LEN_TOL);

    // Dot-drawn ships get no plume and no burst (Q27)
    TestTrue(TEXT("hull drawn: plume allowed"), PlumeAllowed(true));
    TestFalse(TEXT("dot drawn: no plume"), PlumeAllowed(false));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterPlayerLightTest, "SOLTest.Thruster.PlayerLight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14: the player hull light's intensity b x max (b clamped to [0, 1]), its colour the ramp's rim colour, and its
// gate: on in VR at every quality, otherwise off only at Low
bool FSOLThrusterPlayerLightTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;

    // Intensity
    TestEqual(TEXT("b 0.5, max 1000: 500"), static_cast<double>(PlumeLightIntensity(0.5f, 1000.0f)), 500.0, 1.0e-3);
    TestEqual(TEXT("b 0: 0"), static_cast<double>(PlumeLightIntensity(0.0f, 1000.0f)), 0.0, 1.0e-3);
    TestEqual(TEXT("b 1.5 read as 1: 1000"), static_cast<double>(PlumeLightIntensity(1.5f, 1000.0f)), 1000.0, 1.0e-3);
    TestEqual(TEXT("b -0.5 read as 0: 0"), static_cast<double>(PlumeLightIntensity(-0.5f, 1000.0f)), 0.0, 1.0e-3);

    // Colour equals the ramp's rim colour
    for (const double c : { 0.0, 0.35, 0.6, 1.0 })
    {
        TestTrue(FString::Printf(TEXT("light colour at c %.2f equals the rim colour"), c),
            ThrusterColourEqual(PlumeLightColour(c), PlumeColour(c).Rim, THRUSTER_TOL));
    }

    // Gate
    TestFalse(TEXT("Low, not VR: off"), PlayerLightEnabled(0, false));
    for (int32 quality = 1; quality <= 4; ++quality)
    {
        TestTrue(FString::Printf(TEXT("quality %d, not VR: on"), quality), PlayerLightEnabled(quality, false));
    }
    for (int32 quality = 0; quality <= 4; ++quality)
    {
        TestTrue(FString::Printf(TEXT("quality %d, VR: on"), quality), PlayerLightEnabled(quality, true));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
