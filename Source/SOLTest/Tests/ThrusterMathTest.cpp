/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLThrusterMath.h"
#include "Visuals/SOLThrusterMirror.h"
#include "Visuals/SOLThrusterRibbons.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "ConvexVolume.h"
#include "Math/InverseRotationMatrix.h"
#include "Math/PerspectiveMatrix.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #51 thruster plume math (SOLThruster, SDD 51 sections 4.2-4.9 and 7), written against the test
// contract Docs/Plans/51-thruster-math-contract.md before the production code existed (part 51a, test first; the R10
// one-piece flame cases 7, 11, 18-24 rewritten or added in part 51e2, and the end caps (case 25, case 18's 34-texel
// row, case 24's anchor hold) in part 51e3, each time before the implementation). One automation
// test per contract case; case 12 (MaterialMirror, part 51b) is in Tests/ThrusterMirrorTest.cpp. Tolerances follow
// the contract: lengths 1e-3 m, scalars 1e-4, unless a case states otherwise (the R10 cases 20-23: 1e-4 m, 1e-5).
// Hull frame: X nose, Y right, Z top (UE axes). Values the contract marks as parameters (the LOD bands, the burst
// threshold, the exhaust speed v_e, the streak depths) are passed as arguments here, so tuning them later does not
// touch this file.

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
    // Returns true when a texel agrees with (x, y, z, w) component-wise within a tolerance
    bool ThrusterTexelNear(const FVector4f& texel, const double x, const double y, const double z, const double w,
        const double tolerance)
    {
        return FMath::Abs(static_cast<double>(texel.X) - x) <= tolerance
            && FMath::Abs(static_cast<double>(texel.Y) - y) <= tolerance
            && FMath::Abs(static_cast<double>(texel.Z) - z) <= tolerance
            && FMath::Abs(static_cast<double>(texel.W) - w) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of texels that differ from the expected row (both arrays read only within their own sizes; a
    // size mismatch counts every missing texel) and writes the first differing index into outFirstBad (INDEX_NONE)
    int32 ThrusterTexelMismatches(const TArray<FVector4f>& actual, const TArray<FVector4f>& expected,
        const double tolerance, int32& outFirstBad)
    {
        outFirstBad = INDEX_NONE;
        int32 mismatches = FMath::Abs(actual.Num() - expected.Num());
        const int32 common = FMath::Min(actual.Num(), expected.Num());
        for (int32 index = 0; index < common; ++index)
        {
            const FVector4f& want = expected[index];
            if (!ThrusterTexelNear(actual[index], want.X, want.Y, want.Z, want.W, tolerance))
            {
                ++mismatches;
                outFirstBad = outFirstBad == INDEX_NONE ? index : outFirstBad;
            }
        }
        return mismatches;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a label suffix naming the first differing texel and its actual value, for a failed texel comparison
    FString ThrusterTexelFirstBad(const TArray<FVector4f>& actual, const int32 firstBad)
    {
        if (!actual.IsValidIndex(firstBad))
        {
            return FString();
        }
        const FVector4f& texel = actual[firstBad];
        return FString::Printf(TEXT(" (first bad texel %d = (%.4f, %.4f, %.4f, %.4f))"), firstBad, texel.X, texel.Y,
            texel.Z, texel.W);
    }

    constexpr double THRUSTER_FLAME_LEN_TOL = 1.0e-4;               // R10 flame cases: lengths, metres
    constexpr double THRUSTER_FLAME_TOL = 1.0e-5;                   // R10 flame cases: scalars

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a flame point agrees with the expected position (positionTol), alpha (1e-5) and width (1e-4 m)
    bool ThrusterFlamePointNear(const FSOLRibbonPoint& point, const double x, const double y, const double z,
        const double alpha, const double widthM, const double positionTol)
    {
        return ThrusterVecNear(point.PositionM, x, y, z, positionTol)
            && FMath::Abs(static_cast<double>(point.Alpha) - alpha) <= THRUSTER_FLAME_TOL
            && FMath::Abs(static_cast<double>(point.WidthM) - widthM) <= THRUSTER_FLAME_LEN_TOL;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's smoothstep S(u) = 3u^2 - 2u^3 on u clamped to [0, 1] (the test's own reference)
    double ThrusterFlameSmoothstep(const double u)
    {
        const double clamped = FMath::Clamp(u, 0.0, 1.0);
        return clamped * clamped * (3.0 - 2.0 * clamped);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's flame width 2 r_n [1 - (1 - 0.1) S(s / L_p)] for valid input (the test's own reference)
    double ThrusterFlameWidthRef(const double arcM, const double plumeLengthM, const double nozzleRadiusM)
    {
        return 2.0 * nozzleRadiusM * (1.0 - 0.9 * ThrusterFlameSmoothstep(FMath::Max(arcM, 0.0) / plumeLengthM));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a plume pose with every field set
    FSOLPlumeSample ThrusterFlamePose(const double emitTimeS, const FVector3f& offsetM, const FVector3f& exhaustDir,
        const float brightness)
    {
        FSOLPlumeSample sample;
        sample.EmitTimeS = emitTimeS;
        sample.OffsetM = offsetM;
        sample.ExhaustDir = exhaustDir;
        sample.Brightness = brightness;
        return sample;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the case 21 straight-example pose at a time: offset (-15, 0, 0) m, exhaust -X
    FSOLPlumeSample ThrusterFlameStraightPose(const double emitTimeS, const float brightness)
    {
        return ThrusterFlamePose(emitTimeS, FVector3f(-15.0f, 0.0f, 0.0f), FVector3f(-1.0f, 0.0f, 0.0f), brightness);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the case 21 steady-turn pose at a time: theta = 2 (t - 10), offset 15 m behind along -heading, b 1
    FSOLPlumeSample ThrusterFlameTurnPose(const double emitTimeS)
    {
        const double theta = 2.0 * (emitTimeS - 10.0);
        const double cosTheta = FMath::Cos(theta);
        const double sinTheta = FMath::Sin(theta);
        return ThrusterFlamePose(emitTimeS,
            FVector3f(static_cast<float>(-15.0 * cosTheta), static_cast<float>(-15.0 * sinTheta), 0.0f),
            FVector3f(static_cast<float>(-cosTheta), static_cast<float>(-sinTheta), 0.0f), 1.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills a history with the case 21 straight example's two poses, oldest first (t 9.98 then 9.99)
    void ThrusterFlameStraightHistory(FSOLPlumeHistory& history, const float olderBrightness,
        const float newerBrightness)
    {
        history.Reset();
        history.Push(ThrusterFlameStraightPose(9.98, olderBrightness));
        history.Push(ThrusterFlameStraightPose(9.99, newerBrightness));
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills a history with the case 21 steady-turn poses at t = 10 - k / 120, k = 7..1, oldest first
    void ThrusterFlameTurnHistory(FSOLPlumeHistory& history)
    {
        history.Reset();
        for (int32 k = 7; k >= 1; --k)
        {
            history.Push(ThrusterFlameTurnPose(10.0 - static_cast<double>(k) / 120.0));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ribbon point no builder writes (a sentinel for "untouched" checks)
    FSOLRibbonPoint ThrusterRibbonPointSentinel()
    {
        FSOLRibbonPoint point;
        point.PositionM = FVector3f(-999.0f, -999.0f, -999.0f);
        point.Alpha = -9.0f;
        point.WidthM = -9.0f;
        return point;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a ribbon point still holds the sentinel
    bool ThrusterRibbonPointIsSentinel(const FSOLRibbonPoint& point)
    {
        return point.PositionM == FVector3f(-999.0f, -999.0f, -999.0f) && point.Alpha == -9.0f
            && point.WidthM == -9.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a flame point with every field set
    FSOLRibbonPoint ThrusterMakeRibbonPoint(const float x, const float y, const float z, const float alpha,
        const float widthM)
    {
        FSOLRibbonPoint point;
        point.PositionM = FVector3f(x, y, z);
        point.Alpha = alpha;
        point.WidthM = widthM;
        return point;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the case 21 straight-example spine as built points (point j at (-15 - 1.25 j, 0, 0) + shift, width
    // W(1.25 j, 10, 1.5)); alpha b (1 - j / 24) with b the case 21 table's emission brightness (0.8 to point 16, then
    // 1 - 0.0125 j) when bTableBrightness, else b 0.8 on every point (the case 25 wording)
    TArray<FSOLRibbonPoint> ThrusterFlameStraightSpine(const FVector3d& shiftM, const bool bTableBrightness)
    {
        TArray<FSOLRibbonPoint> spine;
        for (int32 j = 0; j < 25; ++j)
        {
            const double arcM = 1.25 * static_cast<double>(j);
            const double brightness = bTableBrightness && j > 16 ? 1.0 - 0.0125 * static_cast<double>(j) : 0.8;
            spine.Add(ThrusterMakeRibbonPoint(static_cast<float>(-15.0 - arcM + shiftM.X),
                static_cast<float>(shiftM.Y), static_cast<float>(shiftM.Z),
                static_cast<float>(brightness * (1.0 - static_cast<double>(j) / 24.0)),
                static_cast<float>(ThrusterFlameWidthRef(arcM, 10.0, 1.5))));
        }
        return spine;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two flame points hold exactly the same position, alpha and width
    bool ThrusterRibbonPointSame(const FSOLRibbonPoint& a, const FSOLRibbonPoint& b)
    {
        return a.PositionM == b.PositionM && a.Alpha == b.Alpha && a.WidthM == b.WidthM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a shown NPC ribbon with every field set
    FSOLRibbonShown ThrusterMakeShown(const int32 npcIndex, const double ribbonPx)
    {
        FSOLRibbonShown shown;
        shown.NpcIndex = npcIndex;
        shown.RibbonPx = ribbonPx;
        return shown;
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
// Case 7 (rewritten for R10): the fixed 8-pose ring (sampling interval 1/120 s, wrap, age order, reset, non-finite
// pushes refused); a pose carries no drift any more
bool FSOLThrusterRibbonHistoryTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const int32 capacity = static_cast<int32>(SOL::Thruster::FLAME_HISTORY_SAMPLES);

    // Empty history and the sampling interval 1/120 s
    FSOLPlumeHistory history;
    history.Reset();
    TestEqual(TEXT("empty: Num 0"), history.Num(), 0);
    TestTrue(TEXT("empty: ShouldSample(0)"), history.ShouldSample(0.0));
    TestTrue(TEXT("empty: ShouldSample(-5)"), history.ShouldSample(-5.0));
    TestTrue(TEXT("empty: ShouldSample(1e6)"), history.ShouldSample(1.0e6));
    TestEqual(TEXT("FLAME_SAMPLE_INTERVAL_S = 1/120 = 0.0083333 s"),
        static_cast<double>(SOL::Thruster::FLAME_SAMPLE_INTERVAL_S), 1.0 / 120.0, 1.0e-9);
    TestTrue(TEXT("push at t 0 accepted"), history.Push(ThrusterFlameStraightPose(0.0, 0.8f)));
    TestEqual(TEXT("after one push: Num 1"), history.Num(), 1);
    TestFalse(TEXT("ShouldSample(0.008) false"), history.ShouldSample(0.008));
    TestTrue(TEXT("ShouldSample(0.0084) true"), history.ShouldSample(0.0084));

    // Wrap: 12 pushes at t = k / 120 keep the newest 8, newest first
    TestEqual(TEXT("FLAME_HISTORY_SAMPLES 8"), capacity, 8);
    history.Reset();
    int32 refused = 0;
    for (int32 k = 1; k <= 12; ++k)
    {
        refused += history.Push(ThrusterFlameStraightPose(static_cast<double>(k) / 120.0, 0.8f)) ? 0 : 1;
    }
    TestEqual(TEXT("12 pushes: all accepted"), refused, 0);
    TestEqual(TEXT("12 pushes: Num 8"), history.Num(), 8);
    if (history.Num() == 8)
    {
        TestEqual(TEXT("ByAge(0) is t 12 / 120"), history.ByAge(0).EmitTimeS, 12.0 / 120.0, 1.0e-9);
        TestEqual(TEXT("ByAge(7) is t 5 / 120"), history.ByAge(7).EmitTimeS, 5.0 / 120.0, 1.0e-9);
        int32 misordered = 0;
        for (int32 index = 0; index < 8; ++index)
        {
            const double expected = static_cast<double>(12 - index) / 120.0;
            misordered += FMath::Abs(history.ByAge(index).EmitTimeS - expected) <= 1.0e-9 ? 0 : 1;
        }
        TestEqual(TEXT("ages ordered newest to oldest"), misordered, 0);
    }
    TestFalse(TEXT("after the wrap: ShouldSample 0.008 s after the newest is false"),
        history.ShouldSample(12.0 / 120.0 + 0.008));
    TestTrue(TEXT("after the wrap: ShouldSample 0.0084 s after the newest is true"),
        history.ShouldSample(12.0 / 120.0 + 0.0084));

    // Reset and refused pushes (NaN offset, exhaust, brightness or time)
    history.Reset();
    TestEqual(TEXT("Reset: Num 0"), history.Num(), 0);
    FSOLPlumeSample nanOffset = ThrusterFlameStraightPose(1.0, 0.8f);
    nanOffset.OffsetM = FVector3f(THRUSTER_NAN_F, 0.0f, 0.0f);
    TestFalse(TEXT("NaN offset on empty: refused"), history.Push(nanOffset));
    TestEqual(TEXT("NaN offset on empty: Num unchanged"), history.Num(), 0);
    for (int32 k = 0; k < 5; ++k)
    {
        history.Push(ThrusterFlameStraightPose(static_cast<double>(k) / 120.0, 0.8f));
    }
    TestEqual(TEXT("5 valid pushes: Num 5"), history.Num(), 5);
    TestFalse(TEXT("NaN offset: refused"), history.Push(nanOffset));
    TestFalse(TEXT("NaN emit time: refused"), history.Push(ThrusterFlameStraightPose(THRUSTER_NAN, 0.8f)));
    FSOLPlumeSample nanExhaust = ThrusterFlameStraightPose(1.0, 0.8f);
    nanExhaust.ExhaustDir = FVector3f(0.0f, THRUSTER_NAN_F, 0.0f);
    TestFalse(TEXT("NaN exhaust: refused"), history.Push(nanExhaust));
    FSOLPlumeSample infExhaust = ThrusterFlameStraightPose(1.0, 0.8f);
    infExhaust.ExhaustDir = FVector3f(0.0f, THRUSTER_INF_F, 0.0f);
    TestFalse(TEXT("inf exhaust: refused"), history.Push(infExhaust));
    TestFalse(TEXT("NaN brightness: refused"), history.Push(ThrusterFlameStraightPose(1.0, THRUSTER_NAN_F)));
    TestEqual(TEXT("refused pushes: Num still 5"), history.Num(), 5);
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
    TestEqual(TEXT("1.65 G NPC plume length at 9272.65 m: 1 px"), PixelLength(9.659011, 9272.65, 960.0), 1.0,
        THRUSTER_TOL);
    TestEqual(TEXT("30 m length at 28.8 km: 1 px"), PixelLength(30.0, 28800.0, 960.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("20 m length at 19.2 km: 1 px"), PixelLength(20.0, 19200.0, 960.0), 1.0, THRUSTER_TOL);
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
// Case 9 (rewritten in 51e, SelectRibbonShips removed in the 51e review: the store pass applies RibbonShown per
// trail with no radius, count cap or ranking): a ribbon is shown when the ship burns or its ribbon is still fading
// and the ribbon is at least V px long
bool FSOLThrusterNearestRibbonsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;

    // RibbonShown: (plume or fading) and px >= V, exactly V included
    TestTrue(TEXT("shown (1.0, V 1, plume): exactly V is in"), RibbonShown(1.0, 1.0, true, false));
    TestFalse(TEXT("shown (0.99, V 1, plume): below V is out"), RibbonShown(0.99, 1.0, true, false));
    TestTrue(TEXT("shown (5, V 1, fading only): in"), RibbonShown(5.0, 1.0, false, true));
    TestTrue(TEXT("shown (5, V 1, plume and fading): in"), RibbonShown(5.0, 1.0, true, true));
    TestFalse(TEXT("shown (5, V 1, neither burning nor fading): out"), RibbonShown(5.0, 1.0, false, false));

    // RibbonShown: NaN px or NaN V never shown
    TestFalse(TEXT("shown (NaN, V 1, plume): out"), RibbonShown(THRUSTER_NAN, 1.0, true, false));
    TestFalse(TEXT("shown (5, V NaN, plume): out"), RibbonShown(5.0, THRUSTER_NAN, true, false));
    TestFalse(TEXT("shown (NaN, V NaN, plume): out"), RibbonShown(THRUSTER_NAN, THRUSTER_NAN, true, false));

    // RibbonShown: V <= 0 shows any finite px >= 0
    TestTrue(TEXT("shown (0, V 0, plume): in"), RibbonShown(0.0, 0.0, true, false));
    TestTrue(TEXT("shown (5, V 0, plume): in"), RibbonShown(5.0, 0.0, true, false));
    TestTrue(TEXT("shown (0, V -1, plume): in"), RibbonShown(0.0, -1.0, true, false));
    TestTrue(TEXT("shown (0.5, V -1, fading): in"), RibbonShown(0.5, -1.0, false, true));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterQualityScalingTest, "SOLTest.Thruster.QualityScaling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 10 (rewritten in 51e): the ribbon vanish length V per quality preset (Low 10, Medium 4.6, High 2.2, Epic 1,
// Cinematic 1 px, out-of-range presets clamped) and V never rising with quality (the player's flame at every preset,
// R5, is SOLTest.Thruster.Store.PlayerExemption's)
bool FSOLThrusterQualityScalingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;

    // V per preset
    TestEqual(TEXT("Low (0): V 10"), RibbonVanishPx(0), 10.0, THRUSTER_TOL);
    TestEqual(TEXT("Medium (1): V 4.6"), RibbonVanishPx(1), 4.6, THRUSTER_TOL);
    TestEqual(TEXT("High (2): V 2.2"), RibbonVanishPx(2), 2.2, THRUSTER_TOL);
    TestEqual(TEXT("Epic (3): V 1"), RibbonVanishPx(3), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("Cinematic (4): V 1"), RibbonVanishPx(4), 1.0, THRUSTER_TOL);

    // Out-of-range presets clamped
    TestEqual(TEXT("-1 read as Low: V 10"), RibbonVanishPx(-1), 10.0, THRUSTER_TOL);
    TestEqual(TEXT("7 read as Cinematic: V 1"), RibbonVanishPx(7), 1.0, THRUSTER_TOL);

    // Monotonic: V never rises with quality
    for (int32 quality = 1; quality <= 4; ++quality)
    {
        TestTrue(FString::Printf(TEXT("V at quality %d <= V at quality %d"), quality, quality - 1),
            RibbonVanishPx(quality) <= RibbonVanishPx(quality - 1));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterConstantsTest, "SOLTest.Thruster.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 11 (rewritten for R10): the contract's constants (asserted so a tuning change is a visible test edit), their
// ordering invariants, the flame ring's coverage of the longest NPC flame's life and the per-hull nozzle table
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

    // Glow and bursts
    TestEqual(TEXT("GLOW_MIN_DRAWN_PX 1"), static_cast<double>(SOL::Thruster::GLOW_MIN_DRAWN_PX), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("BURST_LENGTH_HULL_DIVISOR 6"), static_cast<double>(SOL::Thruster::BURST_LENGTH_HULL_DIVISOR), 6.0,
        THRUSTER_TOL);
    TestEqual(TEXT("BURST_REF_G 1.5"), static_cast<double>(SOL::Thruster::BURST_REF_G), 1.5, THRUSTER_TOL);

    // The one-piece flame (R10): taper, length factor, spine points and the pose ring
    TestEqual(TEXT("FLAME_TAIL_WIDTH_RATIO 0.1"), static_cast<double>(SOL::Thruster::FLAME_TAIL_WIDTH_RATIO), 0.1,
        THRUSTER_TOL);
    TestEqual(TEXT("FLAME_LENGTH_FACTOR 3"), static_cast<double>(SOL::Thruster::FLAME_LENGTH_FACTOR), 3.0,
        THRUSTER_TOL);
    TestEqual(TEXT("FLAME_POINTS 25"), static_cast<int32>(SOL::Thruster::FLAME_POINTS), 25);
    TestEqual(TEXT("FLAME_HISTORY_SAMPLES 8"), static_cast<int32>(SOL::Thruster::FLAME_HISTORY_SAMPLES), 8);
    TestEqual(TEXT("FLAME_SAMPLE_INTERVAL_S 1/120"), static_cast<double>(SOL::Thruster::FLAME_SAMPLE_INTERVAL_S),
        1.0 / 120.0, 1.0e-9);

    // The end caps and the strip pool row (51e3, case 25): drawn points, cap depth, texels per trail, anchor hold
    TestEqual(TEXT("FLAME_DRAWN_POINTS 27"), static_cast<int32>(SOL::Thruster::FLAME_DRAWN_POINTS), 27);
    TestEqual(TEXT("FLAME_CAP_DEPTH_NOZZLE_RADII 0.05"),
        static_cast<double>(SOL::Thruster::FLAME_CAP_DEPTH_NOZZLE_RADII), 0.05, THRUSTER_TOL);
    TestEqual(TEXT("FLAME_CAP_MIN_DEPTH_M 0.02"), static_cast<double>(SOL::Thruster::FLAME_CAP_MIN_DEPTH_M), 0.02,
        THRUSTER_TOL);
    TestEqual(TEXT("RIBBON_TEXELS_PER_TRAIL 34"), static_cast<int32>(SOL::Thruster::RIBBON_TEXELS_PER_TRAIL), 34);
    TestEqual(TEXT("RIBBON_ANCHOR_HOLD_M 1000"), static_cast<double>(SOL::Thruster::RIBBON_ANCHOR_HOLD_M), 1000.0,
        THRUSTER_TOL);

    // Exhaust speed and shutdown time: start values, not pinned, only positive
    const double exhaustSpeedMps = static_cast<double>(SOL::Thruster::FLAME_EXHAUST_SPEED_MPS);
    TestTrue(TEXT("FLAME_EXHAUST_SPEED_MPS > 0"), exhaustSpeedMps > 0.0);
    TestTrue(TEXT("FLAME_SHUTDOWN_S > 0"), static_cast<double>(SOL::Thruster::FLAME_SHUTDOWN_S) > 0.0);

    // The ring covers the longest NPC flame's life: (8 - 1) / 120 s >= 3 x 30 m / v_e
    const double ringSpanS = static_cast<double>(SOL::Thruster::FLAME_HISTORY_SAMPLES - 1)
        * static_cast<double>(SOL::Thruster::FLAME_SAMPLE_INTERVAL_S);
    const double longestLifeS = static_cast<double>(SOL::Thruster::FLAME_LENGTH_FACTOR)
        * static_cast<double>(SOL::Thruster::NPC_HULL_LENGTH_M) / exhaustSpeedMps;
    TestTrue(FString::Printf(TEXT("ring span %.4f s covers the longest NPC flame's life %.4f s"), ringSpanS,
        longestLifeS), exhaustSpeedMps > 0.0 && ringSpanS >= longestLifeS);

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
// the main plume and bursts independent and the burst length L_hull / 6 x s (no plume or burst on dot-drawn ships,
// Q27, holds by construction: only the hull-drawn pass feeds them; -SOLSmokeThruster's P3-dots-no-plume checks it)
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

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterRibbonPixelRuleTest, "SOLTest.Thruster.RibbonPixelRule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 15 (51e, SDD 51 section 11.1; R10: the flame's on-screen length): the pixel length f x 3 L_p / d, its linear
// fade from V to 4 V, the 1 px drawn-width floor, the reach f x 3 L_hull / V, and the pixel-rule constants
bool FSOLThrusterRibbonPixelRuleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    constexpr double focalRefPx = 960.0;                            // FocalRefPx(90, 16/9)
    constexpr double device720pFocalPx = 960.0 * 720.0 / 1080.0;    // 720p device focal, 640 px

    // Pixel length of the 3 L_p ribbon: War power (30 m plume) dims from 21.6 km and is gone at 86.4 km with V 1
    TestEqual(TEXT("ribbon px (30, 86400, 960) = 1"), RibbonPixelLength(30.0, 86400.0, focalRefPx), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("ribbon px (30, 21600, 960) = 4"), RibbonPixelLength(30.0, 21600.0, focalRefPx), 4.0, THRUSTER_TOL);

    // The 1.65 G NPC plume (1e-4) and a close player-size plume
    TestEqual(TEXT("ribbon px (9.659011, 27817.95, 960) = 1"), RibbonPixelLength(9.659011, 27817.95, focalRefPx), 1.0,
        1.0e-4);
    TestEqual(TEXT("ribbon px (9.659011, 6954.49, 960) = 4"), RibbonPixelLength(9.659011, 6954.49, focalRefPx), 4.0,
        1.0e-4);
    TestEqual(TEXT("ribbon px (20, 1000, 960) = 57.6"), RibbonPixelLength(20.0, 1000.0, focalRefPx), 57.6,
        THRUSTER_TOL);

    // Pixel length: bad distance, plume length or focal give 0
    TestEqual(TEXT("ribbon px distance 0: 0"), RibbonPixelLength(30.0, 0.0, focalRefPx), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("ribbon px negative distance: 0"), RibbonPixelLength(30.0, -100.0, focalRefPx), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("ribbon px NaN distance: 0"), RibbonPixelLength(30.0, THRUSTER_NAN, focalRefPx), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("ribbon px plume 0: 0"), RibbonPixelLength(0.0, 1000.0, focalRefPx), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("ribbon px negative plume: 0"), RibbonPixelLength(-30.0, 1000.0, focalRefPx), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("ribbon px focal 0: 0"), RibbonPixelLength(30.0, 1000.0, 0.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("ribbon px negative focal: 0"), RibbonPixelLength(30.0, 1000.0, -960.0), 0.0, THRUSTER_TOL);

    // Fade linear between V and 4 V, at V 1
    TestEqual(TEXT("fade V 1: 0.5 px -> 0"), RibbonPixelFade(0.5, 1.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("fade V 1: 1.0 px -> 0"), RibbonPixelFade(1.0, 1.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("fade V 1: 1.75 px -> 0.25"), RibbonPixelFade(1.75, 1.0), 0.25, THRUSTER_TOL);
    TestEqual(TEXT("fade V 1: 2.5 px -> 0.5"), RibbonPixelFade(2.5, 1.0), 0.5, THRUSTER_TOL);
    TestEqual(TEXT("fade V 1: 4.0 px -> 1"), RibbonPixelFade(4.0, 1.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("fade V 1: 10 px -> 1"), RibbonPixelFade(10.0, 1.0), 1.0, THRUSTER_TOL);

    // Fade at V 10 and V 4.6
    TestEqual(TEXT("fade V 10: 9.9 px -> 0"), RibbonPixelFade(9.9, 10.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("fade V 10: 25 px -> 0.5"), RibbonPixelFade(25.0, 10.0), 0.5, THRUSTER_TOL);
    TestEqual(TEXT("fade V 10: 40 px -> 1"), RibbonPixelFade(40.0, 10.0), 1.0, THRUSTER_TOL);
    TestEqual(TEXT("fade V 4.6: 11.5 px -> 0.5"), RibbonPixelFade(11.5, 4.6), 0.5, THRUSTER_TOL);

    // Fade: NaN px, NaN V or V <= 0 give 0
    TestEqual(TEXT("fade NaN px: 0"), RibbonPixelFade(THRUSTER_NAN, 1.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("fade NaN V: 0"), RibbonPixelFade(5.0, THRUSTER_NAN), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("fade V 0: 0"), RibbonPixelFade(5.0, 0.0), 0.0, THRUSTER_TOL);
    TestEqual(TEXT("fade V -1: 0"), RibbonPixelFade(5.0, -1.0), 0.0, THRUSTER_TOL);

    // Drawn width max(w, minPx d / f): the true width when wider, the 1 px floor when the ribbon is thinner than a pixel
    TestEqual(TEXT("width (1.5, 1000, 960, 1) = 1.5"), RibbonDrawnWidthM(1.5, 1000.0, focalRefPx, 1.0), 1.5,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (1.5, 86400, 960, 1) = 90"), RibbonDrawnWidthM(1.5, 86400.0, focalRefPx, 1.0), 90.0,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (3.0, 2880, 960, 1) = 3.0"), RibbonDrawnWidthM(3.0, 2880.0, focalRefPx, 1.0), 3.0,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (1.5, 86400, 720p focal 640, 1) = 135"),
        RibbonDrawnWidthM(1.5, 86400.0, device720pFocalPx, 1.0), 135.0, THRUSTER_LEN_TOL);

    // Drawn width: NaN or negative w read as 0
    TestEqual(TEXT("width (NaN, 960, 960, 1) = 1"), RibbonDrawnWidthM(THRUSTER_NAN, 960.0, focalRefPx, 1.0), 1.0,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (-2, 960, 960, 1) = 1"), RibbonDrawnWidthM(-2.0, 960.0, focalRefPx, 1.0), 1.0,
        THRUSTER_LEN_TOL);

    // Drawn width: the floor term is 0 for a non-finite or non-positive d, f or minPx
    TestEqual(TEXT("width (1.5, 0, 960, 1) = 1.5"), RibbonDrawnWidthM(1.5, 0.0, focalRefPx, 1.0), 1.5,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (1.5, NaN, 960, 1) = 1.5"), RibbonDrawnWidthM(1.5, THRUSTER_NAN, focalRefPx, 1.0), 1.5,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (1.5, 1000, 0, 1) = 1.5"), RibbonDrawnWidthM(1.5, 1000.0, 0.0, 1.0), 1.5,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (1.5, 86400, NaN, 1) = 1.5"), RibbonDrawnWidthM(1.5, 86400.0, THRUSTER_NAN, 1.0), 1.5,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (1.5, 86400, 960, 0) = 1.5"), RibbonDrawnWidthM(1.5, 86400.0, focalRefPx, 0.0), 1.5,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("width (1.5, 86400, 960, NaN) = 1.5"), RibbonDrawnWidthM(1.5, 86400.0, focalRefPx, THRUSTER_NAN),
        1.5, THRUSTER_LEN_TOL);

    // Reach f x 3 L_hull / V
    TestEqual(TEXT("reach (30, 960, 1) = 86400 m"), RibbonReachM(30.0, focalRefPx, 1.0), 86400.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("reach (30, 960, 10) = 8640 m"), RibbonReachM(30.0, focalRefPx, 10.0), 8640.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("reach (30, 960, 4.6) = 18782.6 m"), RibbonReachM(30.0, focalRefPx, 4.6), 18782.6, 0.1);

    // Reach: bad V, hull or focal give 0
    TestEqual(TEXT("reach V 0: 0"), RibbonReachM(30.0, focalRefPx, 0.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("reach V -1: 0"), RibbonReachM(30.0, focalRefPx, -1.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("reach V NaN: 0"), RibbonReachM(30.0, focalRefPx, THRUSTER_NAN), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("reach hull 0: 0"), RibbonReachM(0.0, focalRefPx, 1.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("reach hull -30: 0"), RibbonReachM(-30.0, focalRefPx, 1.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("reach focal 0: 0"), RibbonReachM(30.0, 0.0, 1.0), 0.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("reach focal -960: 0"), RibbonReachM(30.0, -960.0, 1.0), 0.0, THRUSTER_LEN_TOL);

    // Constants: one vanish length per preset, the fade start factor, the width floor and the ribbon length factor
    const double expectedVanish[] = { 10.0, 4.6, 2.2, 1.0, 1.0 };
    TestEqual(TEXT("RIBBON_VANISH_PX_BY_QUALITY has 5 entries"),
        static_cast<int32>(UE_ARRAY_COUNT(SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY)), 5);
    for (int32 index = 0; index < 5; ++index)
    {
        TestEqual(FString::Printf(TEXT("RIBBON_VANISH_PX_BY_QUALITY[%d] = %.1f"), index, expectedVanish[index]),
            static_cast<double>(SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY[index]), expectedVanish[index], 1.0e-9);
    }
    TestEqual(TEXT("RIBBON_FADE_START_FACTOR 4"), static_cast<double>(SOL::Thruster::RIBBON_FADE_START_FACTOR), 4.0,
        1.0e-9);
    TestEqual(TEXT("RIBBON_MIN_WIDTH_PX 1"), static_cast<double>(SOL::Thruster::RIBBON_MIN_WIDTH_PX), 1.0, 1.0e-9);
    TestEqual(TEXT("FLAME_LENGTH_FACTOR 3"), static_cast<double>(SOL::Thruster::FLAME_LENGTH_FACTOR), 3.0, 1.0e-9);

    // R10: the ribbon's pixel length is the flame's on-screen length PixelLength(FLAME_LENGTH_FACTOR L_p, d, f)
    TestEqual(TEXT("ribbon px (9.659011, 5000, 960) = PixelLength(3 L_p, 5000, 960)"),
        RibbonPixelLength(9.659011, 5000.0, focalRefPx),
        PixelLength(static_cast<double>(SOL::Thruster::FLAME_LENGTH_FACTOR) * 9.659011, 5000.0, focalRefPx),
        THRUSTER_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterRibbonRendererSplitTest, "SOLTest.Thruster.RibbonRendererSplit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 16 (51e hybrid, SDD 51 R7): the Niagara threshold 8 V per preset, and the split of the shown NPC ribbons: the
// qualifiers (px >= min, NaN never) up to the free trails by highest px (ties lower index) to Niagara px descending,
// every other entry to the pool in input order; both outputs cleared first and not reallocated on a repeat call
bool FSOLThrusterRibbonRendererSplitTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const double factor = static_cast<double>(SOL::Thruster::RIBBON_NIAGARA_PX_FACTOR);
    const int32 poolTrails = static_cast<int32>(SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS);
    const int32 inGameFree = poolTrails - 2;

    // Constants: the factor, the physical Niagara trail pool and the in-game free trails (the player's 2 taken out)
    TestEqual(TEXT("RIBBON_NIAGARA_PX_FACTOR 8"), factor, 8.0, 1.0e-9);
    TestEqual(TEXT("RIBBON_NIAGARA_POOL_TRAILS 16"), poolTrails, 16);
    TestEqual(TEXT("in-game free trails 16 - 2 = 14"), inGameFree, 14);

    // RibbonNiagaraMinPx per preset, out-of-range presets clamped
    TestEqual(TEXT("Niagara min Low (0): 80"), RibbonNiagaraMinPx(0), 80.0, THRUSTER_TOL);
    TestEqual(TEXT("Niagara min Medium (1): 36.8"), RibbonNiagaraMinPx(1), 36.8, THRUSTER_TOL);
    TestEqual(TEXT("Niagara min High (2): 17.6"), RibbonNiagaraMinPx(2), 17.6, THRUSTER_TOL);
    TestEqual(TEXT("Niagara min Epic (3): 8"), RibbonNiagaraMinPx(3), 8.0, THRUSTER_TOL);
    TestEqual(TEXT("Niagara min Cinematic (4): 8"), RibbonNiagaraMinPx(4), 8.0, THRUSTER_TOL);
    TestEqual(TEXT("Niagara min -1 read as Low: 80"), RibbonNiagaraMinPx(-1), 80.0, THRUSTER_TOL);
    TestEqual(TEXT("Niagara min 7 read as Cinematic: 8"), RibbonNiagaraMinPx(7), 8.0, THRUSTER_TOL);

    // Equals RIBBON_NIAGARA_PX_FACTOR x RibbonVanishPx at every preset
    for (const int32 quality : { 0, 1, 2, 3, 4, -1, 7 })
    {
        TestEqual(FString::Printf(TEXT("Niagara min at quality %d = factor x V"), quality),
            RibbonNiagaraMinPx(quality), factor * RibbonVanishPx(quality), THRUSTER_TOL);
    }

    TArray<int32> niagara;
    TArray<int32> pool;

    // Fills both outputs with junk (to prove they are cleared first) and runs the split
    const auto split = [&niagara, &pool](const TConstArrayView<FSOLRibbonShown> shown, const double niagaraMinPx,
        const int32 niagaraFreeTrails)
    {
        niagara = { -1, 12345, -99, 7, 7, 7 };
        pool = { -2, 54321, -98 };
        SplitRibbonRenderers(shown, niagaraMinPx, niagaraFreeTrails, niagara, pool);
    };

    // 20 entries: entry i has NpcIndex 10 i and px ((7 i) mod 20) + 1, a permutation of 1..20
    TArray<FSOLRibbonShown> twenty;
    for (int32 i = 0; i < 20; ++i)
    {
        twenty.Add(ThrusterMakeShown(10 * i, static_cast<double>(((7 * i) % 20) + 1)));
    }
    const TConstArrayView<FSOLRibbonShown> twentyView(twenty);
    const TArray<int32> allInOrder = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170,
        180, 190 };

    // Min 8, free 14 (in game): the 13 qualifiers px 8..20 all fit, px 8 (index 10) at the boundary included
    split(twentyView, 8.0, inGameFree);
    TestTrue(TEXT("20, min 8, free 14: Niagara px 20 down to 8"),
        ThrusterSameIndices(niagara, { 170, 140, 110, 80, 50, 20, 190, 160, 130, 100, 70, 40, 10 }));
    TestTrue(TEXT("20, min 8, free 14: pool [0, 30, 60, 90, 120, 150, 180]"),
        ThrusterSameIndices(pool, { 0, 30, 60, 90, 120, 150, 180 }));

    // Min 8.0001, free 14: index 10 (px 8) drops to the pool, in its input position
    split(twentyView, 8.0001, 14);
    TestTrue(TEXT("20, min 8.0001, free 14: Niagara without 10"),
        ThrusterSameIndices(niagara, { 170, 140, 110, 80, 50, 20, 190, 160, 130, 100, 70, 40 }));
    TestTrue(TEXT("20, min 8.0001, free 14: pool [0, 10, 30, 60, 90, 120, 150, 180]"),
        ThrusterSameIndices(pool, { 0, 10, 30, 60, 90, 120, 150, 180 }));

    // Overflow: min 8, free 5; the 8 overflow qualifiers keep their input positions among the rest
    split(twentyView, 8.0, 5);
    TestTrue(TEXT("20, min 8, free 5: Niagara px 20..16"), ThrusterSameIndices(niagara, { 170, 140, 110, 80, 50 }));
    TestTrue(TEXT("20, min 8, free 5: pool in input order"),
        ThrusterSameIndices(pool, { 0, 10, 20, 30, 40, 60, 70, 90, 100, 120, 130, 150, 160, 180, 190 }));

    // Min 0, free 100: every entry to Niagara in px order, the pool empty
    split(twentyView, 0.0, 100);
    TestTrue(TEXT("20, min 0, free 100: Niagara all 20 px descending"),
        ThrusterSameIndices(niagara, { 170, 140, 110, 80, 50, 20, 190, 160, 130, 100, 70, 40, 10, 180, 150, 120, 90,
            60, 30, 0 }));
    TestEqual(TEXT("20, min 0, free 100: pool empty"), pool.Num(), 0);

    // No free trails (0 or negative), an "everything to the pool" threshold or a NaN threshold: all to the pool
    split(twentyView, 8.0, 0);
    TestEqual(TEXT("20, free 0: Niagara empty"), niagara.Num(), 0);
    TestTrue(TEXT("20, free 0: pool all 20 in input order"), ThrusterSameIndices(pool, allInOrder));
    split(twentyView, 8.0, -3);
    TestEqual(TEXT("20, free -3: Niagara empty"), niagara.Num(), 0);
    TestTrue(TEXT("20, free -3: pool all 20 in input order"), ThrusterSameIndices(pool, allInOrder));
    split(twentyView, 1.0e30, 14);
    TestEqual(TEXT("20, min 1e30: Niagara empty"), niagara.Num(), 0);
    TestTrue(TEXT("20, min 1e30: pool all 20 in input order"), ThrusterSameIndices(pool, allInOrder));
    split(twentyView, THRUSTER_NAN, 14);
    TestEqual(TEXT("20, min NaN: Niagara empty"), niagara.Num(), 0);
    TestTrue(TEXT("20, min NaN: pool all 20 in input order"), ThrusterSameIndices(pool, allInOrder));

    // Ties broken by lower NpcIndex
    const TArray<FSOLRibbonShown> ties = { ThrusterMakeShown(7, 50.0), ThrusterMakeShown(3, 50.0),
        ThrusterMakeShown(9, 80.0), ThrusterMakeShown(5, 50.0), ThrusterMakeShown(1, 20.0) };
    split(TConstArrayView<FSOLRibbonShown>(ties), 30.0, 3);
    TestTrue(TEXT("ties, min 30, free 3: Niagara [9, 3, 5]"), ThrusterSameIndices(niagara, { 9, 3, 5 }));
    TestTrue(TEXT("ties, min 30, free 3: pool [7, 1]"), ThrusterSameIndices(pool, { 7, 1 }));
    split(TConstArrayView<FSOLRibbonShown>(ties), 30.0, 10);
    TestTrue(TEXT("ties, min 30, free 10: Niagara [9, 3, 5, 7]"), ThrusterSameIndices(niagara, { 9, 3, 5, 7 }));
    TestTrue(TEXT("ties, min 30, free 10: pool [1]"), ThrusterSameIndices(pool, { 1 }));

    // NaN px never qualifies, not even against a 0 threshold
    const TArray<FSOLRibbonShown> nans = { ThrusterMakeShown(4, THRUSTER_NAN), ThrusterMakeShown(8, 2.0),
        ThrusterMakeShown(6, THRUSTER_NAN), ThrusterMakeShown(2, 0.5) };
    split(TConstArrayView<FSOLRibbonShown>(nans), 0.5, 10);
    TestTrue(TEXT("NaN, min 0.5: Niagara [8, 2]"), ThrusterSameIndices(niagara, { 8, 2 }));
    TestTrue(TEXT("NaN, min 0.5: pool [4, 6]"), ThrusterSameIndices(pool, { 4, 6 }));
    split(TConstArrayView<FSOLRibbonShown>(nans), 0.0, 10);
    TestTrue(TEXT("NaN, min 0: Niagara [8, 2]"), ThrusterSameIndices(niagara, { 8, 2 }));
    TestTrue(TEXT("NaN, min 0: pool [4, 6]"), ThrusterSameIndices(pool, { 4, 6 }));

    // Empty input: both junk-filled outputs come back empty
    split(TConstArrayView<FSOLRibbonShown>(), 8.0, 14);
    TestEqual(TEXT("empty input: Niagara empty"), niagara.Num(), 0);
    TestEqual(TEXT("empty input: pool empty"), pool.Num(), 0);

    // 5,000 entries: NpcIndex k, px (37 k) mod 5000 (a permutation of 0..4999); px 4999 - j sits at k 2027 (j + 1)
    constexpr int32 manyCount = 5000;
    TArray<FSOLRibbonShown> many;
    many.Reserve(manyCount);
    for (int32 k = 0; k < manyCount; ++k)
    {
        many.Add(ThrusterMakeShown(k, static_cast<double>((37 * k) % manyCount)));
    }
    const TConstArrayView<FSOLRibbonShown> manyView(many);
    const TArray<int32> manyNiagara = { 2027, 4054, 1081, 3108, 135, 2162, 4189, 1216, 3243, 270, 2297, 4324, 1351,
        3378 };

    // Checks the 5,000-entry result: the 14 largest to Niagara, the pool 4,986 entries strictly increasing with the
    // overflow 405, and the union exactly 0..4999 (every value bounds-checked before use)
    const auto checkMany = [this, &niagara, &pool, &manyNiagara, manyCount](const FString& label)
    {
        TestTrue(label + TEXT(": Niagara px 4999 down to 4986"), ThrusterSameIndices(niagara, manyNiagara));
        TestEqual(label + TEXT(": pool 4,986 entries"), pool.Num(), 4986);
        int32 notIncreasing = 0;
        for (int32 index = 1; index < pool.Num(); ++index)
        {
            notIncreasing += pool[index] > pool[index - 1] ? 0 : 1;
        }
        TestEqual(label + TEXT(": pool strictly increasing k"), notIncreasing, 0);
        TestTrue(label + TEXT(": pool holds 405 (px 4985)"), pool.Contains(405));

        // Union of both outputs: every k once, nothing out of range
        TArray<int32> seen;
        seen.Init(0, manyCount);
        int32 outOfRange = 0;
        for (const TArray<int32>* output : { &niagara, &pool })
        {
            for (const int32 value : *output)
            {
                if (value >= 0 && value < manyCount)
                {
                    ++seen[value];
                }
                else
                {
                    ++outOfRange;
                }
            }
        }
        int32 missing = 0;
        int32 duplicated = 0;
        for (const int32 count : seen)
        {
            missing += count == 0 ? 1 : 0;
            duplicated += count > 1 ? 1 : 0;
        }
        TestEqual(label + TEXT(": union has no value outside 0..4999"), outOfRange, 0);
        TestEqual(label + TEXT(": union misses no k"), missing, 0);
        TestEqual(label + TEXT(": union has no duplicate"), duplicated, 0);
    };

    // Min 4985, free 14: 15 qualifiers (px 4985..4999), 14 fit, 405 overflows to the pool
    split(manyView, 4985.0, 14);
    checkMany(TEXT("5000, min 4985, free 14"));

    // The same call again on the same arrays: no reallocation
    const int32 niagaraMax = niagara.Max();
    const int32 poolMax = pool.Max();
    SplitRibbonRenderers(manyView, 4985.0, 14, niagara, pool);
    TestEqual(TEXT("5000 repeat: Niagara Max() unchanged"), niagara.Max(), niagaraMax);
    TestEqual(TEXT("5000 repeat: pool Max() unchanged"), pool.Max(), poolMax);
    checkMany(TEXT("5000 repeat"));

    // Min 0, free 14: the same Niagara list, the other 4,986 to the pool
    split(manyView, 0.0, 14);
    checkMany(TEXT("5000, min 0, free 14"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterRibbonTexelsTest, "SOLTest.Thruster.RibbonTexels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 18 (rewritten for R10, again in 51e3 for the end caps): a flame's strip-pool row: 27 (x, y, z, alpha) texels
// of the drawn points (case 25) padded with the last position at alpha 0, the flame header (c, b, L_p, r_n) at texel
// 27, then the unfloored widths of drawn points 2..25 four per texel in texels 28..33; extra points ignored, and
// nothing written into a view that is not exactly RIBBON_TEXELS_PER_TRAIL texels
bool FSOLThrusterRibbonTexelsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const int32 drawnPoints = static_cast<int32>(SOL::Thruster::FLAME_DRAWN_POINTS);
    const int32 texelsPerTrail = static_cast<int32>(SOL::Thruster::RIBBON_TEXELS_PER_TRAIL);
    const FVector4f junk(9.0f, 9.0f, 9.0f, 9.0f);

    // Constants: the strip pool layout (27 drawn points, 1 header, 6 width texels for drawn points 2..25)
    TestEqual(TEXT("FLAME_DRAWN_POINTS 27"), drawnPoints, 27);
    TestEqual(TEXT("RIBBON_TEXELS_PER_TRAIL 34"), texelsPerTrail, 34);
    TestEqual(TEXT("RIBBON_TRAILS_PER_BLOCK 32"), static_cast<int32>(SOL::Thruster::RIBBON_TRAILS_PER_BLOCK), 32);
    TestTrue(TEXT("texels >= drawn points + 1 header + 6 width texels"), texelsPerTrail >= drawnPoints + 1 + 6);

    TArray<FVector4f> texels;
    TArray<FVector4f> expected;

    // Packs into a 34-texel row pre-filled with junk (so every texel must be written) and compares it to expected
    const auto packAndCheck = [this, &texels, &expected, &junk](const FString& label,
        const TArray<FSOLRibbonPoint>& points, const float colourScalar, const float brightness,
        const float plumeLengthM, const float nozzleRadiusM)
    {
        texels.Init(junk, 34);
        PackRibbonTexels(TConstArrayView<FSOLRibbonPoint>(points), colourScalar, brightness, plumeLengthM,
            nozzleRadiusM, MakeArrayView(texels));
        int32 firstBad = INDEX_NONE;
        const int32 mismatches = ThrusterTexelMismatches(texels, expected, THRUSTER_TOL, firstBad);
        TestEqual(label + TEXT(": every texel as expected") + ThrusterTexelFirstBad(texels, firstBad), mismatches, 0);
    };

    // Resets the expected row to 34 zero texels with the header (c, b, L_p, r_n) at texel 27
    const auto zeroRowWithHeader = [&expected](const float colourScalar, const float brightness,
        const float plumeLengthM, const float nozzleRadiusM)
    {
        expected.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), 34);
        expected[27] = FVector4f(colourScalar, brightness, plumeLengthM, nozzleRadiusM);
    };

    // 3 points: padding repeats point 2's position at alpha 0 to texel 26, drawn points 0 and 1's widths are not
    // packed, point 2's width is texel 28.x, missing widths 0
    const TArray<FSOLRibbonPoint> three = { ThrusterMakeRibbonPoint(1.0f, 2.0f, 3.0f, 0.5f, 3.0f),
        ThrusterMakeRibbonPoint(4.0f, 5.0f, 6.0f, 0.25f, 2.0f),
        ThrusterMakeRibbonPoint(7.0f, 8.0f, 9.0f, 0.125f, 1.0f) };
    zeroRowWithHeader(0.6f, 0.8f, 10.0f, 1.5f);
    expected[0] = FVector4f(1.0f, 2.0f, 3.0f, 0.5f);
    expected[1] = FVector4f(4.0f, 5.0f, 6.0f, 0.25f);
    expected[2] = FVector4f(7.0f, 8.0f, 9.0f, 0.125f);
    for (int32 index = 3; index <= 26; ++index)
    {
        expected[index] = FVector4f(7.0f, 8.0f, 9.0f, 0.0f);
    }
    expected[28] = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
    packAndCheck(TEXT("3 points"), three, 0.6f, 0.8f, 10.0f, 1.5f);

    // The 3-point row's key texels, asserted on their own for a readable failure
    TestTrue(TEXT("3 points: texel 3 pads (7, 8, 9, 0)"), ThrusterTexelNear(texels[3], 7.0, 8.0, 9.0, 0.0,
        THRUSTER_TOL));
    TestTrue(TEXT("3 points: texel 26 pads (7, 8, 9, 0)"), ThrusterTexelNear(texels[26], 7.0, 8.0, 9.0, 0.0,
        THRUSTER_TOL));
    TestTrue(TEXT("3 points: header at texel 27 (c 0.6, b 0.8, L_p 10, r_n 1.5)"),
        ThrusterTexelNear(texels[27], 0.6, 0.8, 10.0, 1.5, THRUSTER_TOL));
    TestTrue(TEXT("3 points: texel 28 (1, 0, 0, 0), drawn point 2's width only"),
        ThrusterTexelNear(texels[28], 1.0, 0.0, 0.0, 0.0, THRUSTER_TOL));
    TestTrue(TEXT("3 points: texel 33 zero"), ThrusterTexelNear(texels[33], 0.0, 0.0, 0.0, 0.0, THRUSTER_TOL));

    // No points: texels 0..26 zero, the header at 27, 28..33 zero
    zeroRowWithHeader(0.6f, 0.8f, 10.0f, 1.5f);
    packAndCheck(TEXT("no points"), TArray<FSOLRibbonPoint>(), 0.6f, 0.8f, 10.0f, 1.5f);

    // 27 and 30 points, point p at (p, -p, p / 2), alpha p / 32, width p: no padding, widths 2..25 in texels 28..33,
    // points past the 27th ignored
    for (const int32 count : { 27, 30 })
    {
        TArray<FSOLRibbonPoint> full;
        for (int32 p = 0; p < count; ++p)
        {
            const float value = static_cast<float>(p);
            full.Add(ThrusterMakeRibbonPoint(value, -value, 0.5f * value, value / 32.0f, value));
        }
        zeroRowWithHeader(0.6f, 0.8f, 10.0f, 1.5f);
        for (int32 p = 0; p < 27; ++p)
        {
            const float value = static_cast<float>(p);
            expected[p] = FVector4f(value, -value, 0.5f * value, value / 32.0f);
        }
        for (int32 texel = 0; texel < 6; ++texel)
        {
            const float first = static_cast<float>(4 * texel + 2);
            expected[28 + texel] = FVector4f(first, first + 1.0f, first + 2.0f, first + 3.0f);
        }
        packAndCheck(FString::Printf(TEXT("%d points"), count), full, 0.6f, 0.8f, 10.0f, 1.5f);
        TestTrue(FString::Printf(TEXT("%d points: texel 26 is point 26"), count),
            ThrusterTexelNear(texels[26], 26.0, -26.0, 13.0, 26.0 / 32.0, THRUSTER_TOL));
        TestTrue(FString::Printf(TEXT("%d points: header at texel 27"), count),
            ThrusterTexelNear(texels[27], 0.6, 0.8, 10.0, 1.5, THRUSTER_TOL));
        TestTrue(FString::Printf(TEXT("%d points: texel 28 (2, 3, 4, 5)"), count),
            ThrusterTexelNear(texels[28], 2.0, 3.0, 4.0, 5.0, THRUSTER_TOL));
        TestTrue(FString::Printf(TEXT("%d points: texel 29 (6, 7, 8, 9)"), count),
            ThrusterTexelNear(texels[29], 6.0, 7.0, 8.0, 9.0, THRUSTER_TOL));
        TestTrue(FString::Printf(TEXT("%d points: texel 33 (22, 23, 24, 25)"), count),
            ThrusterTexelNear(texels[33], 22.0, 23.0, 24.0, 25.0, THRUSTER_TOL));
    }

    // A view of 33 or 35 texels: nothing written
    for (const int32 size : { 33, 35 })
    {
        TArray<FVector4f> wrong;
        wrong.Init(junk, size);
        PackRibbonTexels(TConstArrayView<FSOLRibbonPoint>(three), 0.6f, 0.8f, 10.0f, 1.5f, MakeArrayView(wrong));
        int32 changed = 0;
        for (const FVector4f& texel : wrong)
        {
            changed += texel == junk ? 0 : 1;
        }
        TestEqual(FString::Printf(TEXT("%d-texel view: unchanged"), size), changed, 0);
    }

    // The case 21 straight table (spine j at (-15 - 1.25 j, 0, 0), b 0.8 to point 16 then 1 - 0.0125 j, alpha
    // b (1 - j / 24), width W(1.25 j)) with its caps built by hand from the case 25 rule (r_n 1.5, depth 0.075: the
    // nozzle cap at (-14.925, 0, 0) with spine 0's alpha, the tail cap at (-45.075, 0, 0) with spine 24's, widths 0),
    // packed with c 0, b 0.8, L_p 10, r_n 1.5; independent of AddFlameCaps
    const TArray<FSOLRibbonPoint> spine = ThrusterFlameStraightSpine(FVector3d::ZeroVector, true);
    TArray<FSOLRibbonPoint> drawn;
    drawn.Add(ThrusterMakeRibbonPoint(-14.925f, 0.0f, 0.0f, spine[0].Alpha, 0.0f));
    drawn.Append(spine);
    drawn.Add(ThrusterMakeRibbonPoint(-45.075f, 0.0f, 0.0f, spine[24].Alpha, 0.0f));
    zeroRowWithHeader(0.0f, 0.8f, 10.0f, 1.5f);
    for (int32 p = 0; p < drawn.Num(); ++p)
    {
        const FSOLRibbonPoint& point = drawn[p];
        expected[p] = FVector4f(point.PositionM.X, point.PositionM.Y, point.PositionM.Z, point.Alpha);
        if (p >= 2 && p <= 25)
        {
            expected[28 + (p - 2) / 4][(p - 2) % 4] = point.WidthM;
        }
    }
    packAndCheck(TEXT("case 21 straight table with caps"), drawn, 0.0f, 0.8f, 10.0f, 1.5f);
    TestTrue(TEXT("straight table: texel 0 (-14.925, 0, 0, 0.8), the nozzle cap"),
        ThrusterTexelNear(texels[0], -14.925, 0.0, 0.0, 0.8, THRUSTER_TOL));
    TestTrue(TEXT("straight table: texel 1 (-15, 0, 0, 0.8)"), ThrusterTexelNear(texels[1], -15.0, 0.0, 0.0, 0.8,
        THRUSTER_TOL));
    TestTrue(TEXT("straight table: texel 9 (-25, 0, 0, 0.533333)"), ThrusterTexelNear(texels[9], -25.0, 0.0, 0.0,
        0.533333, THRUSTER_TOL));
    TestTrue(TEXT("straight table: texel 25 (-45, 0, 0, 0)"), ThrusterTexelNear(texels[25], -45.0, 0.0, 0.0, 0.0,
        THRUSTER_TOL));
    TestTrue(TEXT("straight table: texel 26 (-45.075, 0, 0, 0), the tail cap"),
        ThrusterTexelNear(texels[26], -45.075, 0.0, 0.0, 0.0, THRUSTER_TOL));
    TestTrue(TEXT("straight table: header at texel 27 (0, 0.8, 10, 1.5)"),
        ThrusterTexelNear(texels[27], 0.0, 0.8, 10.0, 1.5, THRUSTER_TOL));
    TestEqual(TEXT("straight table: texel 28.x = spine point 1's width W(1.25, 10, 1.5) = 2.883984"),
        static_cast<double>(texels[28].X), 2.883984, THRUSTER_TOL);
    TestEqual(TEXT("straight table: texel 33.w = spine point 24's width 0.3"), static_cast<double>(texels[33].W),
        0.3, THRUSTER_TOL);

    // The same spine through AddFlameCaps (case 25) then PackRibbonTexels: the same row (a failure here with the
    // hand-built row passing points at AddFlameCaps)
    TArray<FSOLRibbonPoint> capped;
    capped.Init(FSOLRibbonPoint(), drawnPoints);
    const int32 cappedCount = AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(spine), 1.5, MakeArrayView(capped));
    TestEqual(TEXT("straight table via AddFlameCaps: AddFlameCaps returns 27"), cappedCount, 27);
    packAndCheck(TEXT("straight table via AddFlameCaps"), capped, 0.0f, 0.8f, 10.0f, 1.5f);
    return true;
}

// The ribbon renderer helpers (part 51e steps 4-6, written before the implementation; the release rule rewritten
// for R10, contract case 24; the 34-texel row and the anchor hold added in 51e3): the strip pool's texel indexing (the
// CPU twin of SOLTH_RibbonTexel), the block count, the frame anchor and its hold, the device focal length of the 1 px
// floor and the shared store's release rule
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterRibbonRendererMathTest, "SOLTest.Thruster.RibbonRendererMath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Checks the strip pool indexing, the anchor and its hold, the device focal and the R10 release rule
bool FSOLThrusterRibbonRendererMathTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    using namespace SOL::Thruster;

    // Texel origin (51e3): 32 trails of 34 texels per 1088-texel row, trail t at column (t mod 32) x 34
    TestEqual(TEXT("trail 0"), RibbonTexelOrigin(0), FIntPoint(0, 0));
    TestEqual(TEXT("trail 1"), RibbonTexelOrigin(1), FIntPoint(34, 0));
    TestEqual(TEXT("trail 31"), RibbonTexelOrigin(31), FIntPoint(1054, 0));
    TestEqual(TEXT("trail 32"), RibbonTexelOrigin(32), FIntPoint(0, 1));
    TestEqual(TEXT("trail 70"), RibbonTexelOrigin(70), FIntPoint(6 * 34, 2));
    TestEqual(TEXT("trail -1"), RibbonTexelOrigin(-1), FIntPoint(-1, -1));
    TestEqual(TEXT("row width 34 x 32"), RIBBON_TEXELS_PER_TRAIL * RIBBON_TRAILS_PER_BLOCK, 1088);

    // Block count
    TestEqual(TEXT("blocks of 0"), RibbonBlockCount(0), 0);
    TestEqual(TEXT("blocks of -5"), RibbonBlockCount(-5), 0);
    TestEqual(TEXT("blocks of 1"), RibbonBlockCount(1), 1);
    TestEqual(TEXT("blocks of 32"), RibbonBlockCount(32), 1);
    TestEqual(TEXT("blocks of 33"), RibbonBlockCount(33), 2);
    TestEqual(TEXT("blocks of 700"), RibbonBlockCount(700), 22);

    // Anchor: whole metres, exact in float, zero for a non-finite camera
    const FVector3d anchor = RibbonAnchorCm(FVector3d(12345.6, -51.0, 149.9));
    TestTrue(TEXT("anchor rounded to whole metres"), anchor.Equals(FVector3d(12300.0, -100.0, 100.0), 1.0e-9));
    const FVector3d farAnchor = RibbonAnchorCm(FVector3d(9.876543e6, -1.2345e6, 3.0e5 + 49.0));
    TestTrue(TEXT("far anchor"), farAnchor.Equals(FVector3d(9.8765e6, -1.2345e6, 3.0e5), 1.0e-9));
    TestEqual(TEXT("far anchor exact in float"), static_cast<double>(static_cast<float>(farAnchor.X)), farAnchor.X);
    TestTrue(TEXT("NaN camera"), RibbonAnchorCm(FVector3d(THRUSTER_NAN, 1.0e4, 0.0)).IsZero());
    TestTrue(TEXT("infinite camera"), RibbonAnchorCm(FVector3d(0.0, THRUSTER_INF, 0.0)).IsZero());

    // Checks one RibbonAnchorHoldCm call against its expected anchor (exact: a held or rounded anchor is not computed)
    const auto checkHold = [this](const FString& label, const FVector3d& held, const bool bHasHeld,
        const FVector3d& camera, const FVector3d& want)
    {
        const FVector3d got = RibbonAnchorHoldCm(held, bHasHeld, camera);
        TestTrue(FString::Printf(TEXT("hold: %s -> (%.1f, %.1f, %.1f), got (%.1f, %.1f, %.1f)"), *label, want.X,
            want.Y, want.Z, got.X, got.Y, got.Z), got.Equals(want, 1.0e-9));
    };

    // Anchor hold (51e3, case 24): RIBBON_ANCHOR_HOLD_M 1000 m = 100,000 cm, straight-line distance, bound included
    const FVector3d held(100.0, 200.0, 300.0);
    const FVector3d nearCamera(150.4, 250.6, 349.5);
    checkHold(TEXT("camera near the held anchor"), held, true, nearCamera, held);
    checkHold(TEXT("camera exactly 100,000 cm away"), held, true, held + FVector3d(100000.0, 0.0, 0.0), held);
    checkHold(TEXT("camera 103,923 cm away, every component inside the bound"), held, true,
        held + FVector3d(60000.0, 60000.0, 60000.0), FVector3d(60100.0, 60200.0, 60300.0));
    TestTrue(TEXT("hold: the 103,923 cm case equals RibbonAnchorCm(camera)"),
        RibbonAnchorHoldCm(held, true, held + FVector3d(60000.0, 60000.0, 60000.0))
            .Equals(RibbonAnchorCm(held + FVector3d(60000.0, 60000.0, 60000.0)), 1.0e-9));
    checkHold(TEXT("camera 100,001 cm away"), held, true, held + FVector3d(100001.0, 0.0, 0.0),
        FVector3d(100100.0, 200.0, 300.0));
    checkHold(TEXT("nothing held"), held, false, nearCamera, FVector3d(200.0, 300.0, 300.0));
    checkHold(TEXT("held NaN"), FVector3d(100.0, THRUSTER_NAN, 300.0), true, nearCamera,
        FVector3d(200.0, 300.0, 300.0));
    checkHold(TEXT("held infinite"), FVector3d(100.0, 200.0, THRUSTER_INF), true, nearCamera,
        FVector3d(200.0, 300.0, 300.0));
    checkHold(TEXT("camera NaN, valid anchor held"), held, true, FVector3d(THRUSTER_NAN, 250.6, 349.5),
        FVector3d::ZeroVector);
    checkHold(TEXT("camera NaN, nothing held"), held, false, FVector3d(150.4, THRUSTER_NAN, 349.5),
        FVector3d::ZeroVector);
    const FVector3d farHeld(1.0e9, -2.0e9, 3.0e8);
    checkHold(TEXT("far from the origin"), farHeld, true, farHeld + FVector3d(12345.6, -7000.2, 0.4), farHeld);

    // Device focal: ViewToClip[1][1] x height / 2 (90 degrees horizontal at 16:9: P11 = 16/9, 1080 rows -> 960 px)
    TestNearlyEqual(TEXT("focal 1080p"), RibbonDeviceFocalPx(16.0 / 9.0, 1080.0), 960.0, THRUSTER_TOL);
    TestNearlyEqual(TEXT("focal 720p"), RibbonDeviceFocalPx(16.0 / 9.0, 720.0), 640.0, THRUSTER_TOL);
    TestEqual(TEXT("focal zero height"), RibbonDeviceFocalPx(1.0, 0.0), 0.0);
    TestEqual(TEXT("focal negative scale"), RibbonDeviceFocalPx(-1.0, 1080.0), 0.0);
    TestEqual(TEXT("focal NaN"), RibbonDeviceFocalPx(THRUSTER_NAN, 1080.0), 0.0);
    TestEqual(TEXT("focal infinite"), RibbonDeviceFocalPx(1.0, THRUSTER_INF), 0.0);

    // Release rule (R10): empty; the newest burning pose at least the flame's life old (NaN age too); or not fed this
    // frame with the fade at 0 (NaN fade as 0)
    TestTrue(TEXT("(fed, 0, 0, 0.045, 1): empty"), RibbonTrailReleasable(true, 0, 0.0, 0.045, 1.0));
    TestTrue(TEXT("(fed, 8, 0.05, 0.045, 1): all gas has left the flame"),
        RibbonTrailReleasable(true, 8, 0.05, 0.045, 1.0));
    TestFalse(TEXT("(fed, 8, 0.04, 0.045, 1): gas still in the flame"),
        RibbonTrailReleasable(true, 8, 0.04, 0.045, 1.0));
    TestFalse(TEXT("(fed, 8, 0, 0.045, 1): burning now (also during a spool-down)"),
        RibbonTrailReleasable(true, 8, 0.0, 0.045, 1.0));
    TestTrue(TEXT("(not fed, 8, 0, 0.045, 0): faded out"), RibbonTrailReleasable(false, 8, 0.0, 0.045, 0.0));
    TestFalse(TEXT("(not fed, 8, 0, 0.045, 0.5): a one-frame gap, still lit"),
        RibbonTrailReleasable(false, 8, 0.0, 0.045, 0.5));
    TestTrue(TEXT("(not fed, 8, 0, 0.045, NaN): NaN fade read as 0"),
        RibbonTrailReleasable(false, 8, 0.0, 0.045, THRUSTER_NAN));
    TestTrue(TEXT("(fed, 8, NaN, 0.045, 1): NaN age"), RibbonTrailReleasable(true, 8, THRUSTER_NAN, 0.045, 1.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameCapsTest, "SOLTest.Thruster.FlameCaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 25 (51e3, the end caps): FlameCapDepthM = max(0.05 r_n, 0.02); AddFlameCaps writes a nozzle cap, the spine (at
// most FLAME_POINTS points) unchanged and a tail cap, each cap FlameCapDepthM out along its end segment (on its end
// point for a one-point spine or a segment under 1e-6 m) with its end point's alpha and width 0; it returns n + 2, or
// 0 with nothing written for an empty spine or an output shorter than n + 2, and leaves entries past n + 1 untouched
bool FSOLThrusterFlameCapsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const int32 drawnPoints = static_cast<int32>(SOL::Thruster::FLAME_DRAWN_POINTS);

    // Constants: the drawn points, the cap depth, the strip pool row and the anchor hold
    TestEqual(TEXT("FLAME_DRAWN_POINTS 27"), drawnPoints, 27);
    TestEqual(TEXT("FLAME_DRAWN_POINTS == FLAME_POINTS + 2"), drawnPoints,
        static_cast<int32>(SOL::Thruster::FLAME_POINTS) + 2);
    TestEqual(TEXT("FLAME_CAP_DEPTH_NOZZLE_RADII 0.05"),
        static_cast<double>(SOL::Thruster::FLAME_CAP_DEPTH_NOZZLE_RADII), 0.05, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("FLAME_CAP_MIN_DEPTH_M 0.02"), static_cast<double>(SOL::Thruster::FLAME_CAP_MIN_DEPTH_M), 0.02,
        THRUSTER_FLAME_TOL);
    TestEqual(TEXT("RIBBON_TEXELS_PER_TRAIL 34"), static_cast<int32>(SOL::Thruster::RIBBON_TEXELS_PER_TRAIL), 34);
    TestEqual(TEXT("RIBBON_ANCHOR_HOLD_M 1000"), static_cast<double>(SOL::Thruster::RIBBON_ANCHOR_HOLD_M), 1000.0,
        THRUSTER_FLAME_TOL);

    // FlameCapDepthM: max(0.05 r_n, 0.02), the minimum for a non-positive, NaN or infinite radius
    TestEqual(TEXT("cap depth r_n 1.5"), FlameCapDepthM(1.5), 0.075, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("cap depth r_n 0.8"), FlameCapDepthM(0.8), 0.04, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("cap depth r_n 0.4"), FlameCapDepthM(0.4), 0.02, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("cap depth r_n 0.2"), FlameCapDepthM(0.2), 0.02, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("cap depth r_n 0"), FlameCapDepthM(0.0), 0.02, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("cap depth r_n -1"), FlameCapDepthM(-1.0), 0.02, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("cap depth r_n NaN"), FlameCapDepthM(THRUSTER_NAN), 0.02, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("cap depth r_n +infinity"), FlameCapDepthM(THRUSTER_INF), 0.02, THRUSTER_FLAME_TOL);

    // Checks a drawn point's position (within positionTol), alpha and width (within 1e-5)
    const auto checkPoint = [this](const FString& label, const FSOLRibbonPoint& point, const double x, const double y,
        const double z, const double alpha, const double widthM, const double positionTol)
    {
        const bool bNear = ThrusterVecNear(point.PositionM, x, y, z, positionTol)
            && FMath::Abs(static_cast<double>(point.Alpha) - alpha) <= THRUSTER_FLAME_TOL
            && FMath::Abs(static_cast<double>(point.WidthM) - widthM) <= THRUSTER_FLAME_TOL;
        TestTrue(FString::Printf(
            TEXT("%s: (%.6f, %.6f, %.6f), alpha %.6f, width %.6f; got (%.6f, %.6f, %.6f), %.6f, %.6f"), *label, x, y,
            z, alpha, widthM, point.PositionM.X, point.PositionM.Y, point.PositionM.Z, point.Alpha, point.WidthM),
            bNear);
    };

    // Returns how many entries from first on no longer hold the sentinel
    const auto touchedFrom = [](const TArray<FSOLRibbonPoint>& out, const int32 first)
    {
        int32 touched = 0;
        for (int32 index = first; index < out.Num(); ++index)
        {
            touched += ThrusterRibbonPointIsSentinel(out[index]) ? 0 : 1;
        }
        return touched;
    };

    // Returns how many of out[1..spine.Num()] differ from the spine (exactly: the spine is copied unchanged)
    const auto spineChanges = [](const TArray<FSOLRibbonPoint>& out, const TArray<FSOLRibbonPoint>& spine,
        const int32 count)
    {
        int32 changes = 0;
        for (int32 j = 0; j < count; ++j)
        {
            changes += out.IsValidIndex(j + 1) && ThrusterRibbonPointSame(out[j + 1], spine[j]) ? 0 : 1;
        }
        return changes;
    };

    TArray<FSOLRibbonPoint> out;

    // Case 21 straight example (spine j at (-15 - 1.25 j, 0, 0), alpha 0.8 (1 - j / 24), widths W(1.25 j)), r_n 1.5
    // (depth 0.075), an output of 27: the caps 0.075 m beyond each end along the flame's axis
    const TArray<FSOLRibbonPoint> straight = ThrusterFlameStraightSpine(FVector3d::ZeroVector, false);
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    TestEqual(TEXT("straight: returns 27"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(straight), 1.5, MakeArrayView(out)), 27);
    checkPoint(TEXT("straight: out[0], the nozzle cap"), out[0], -14.925, 0.0, 0.0, 0.8, 0.0, THRUSTER_FLAME_TOL);
    checkPoint(TEXT("straight: out[1] = spine 0"), out[1], -15.0, 0.0, 0.0, 0.8, 3.0, THRUSTER_FLAME_TOL);
    checkPoint(TEXT("straight: out[9] = spine 8"), out[9], -25.0, 0.0, 0.0, 0.533333, 0.3, THRUSTER_FLAME_TOL);
    checkPoint(TEXT("straight: out[25] = spine 24"), out[25], -45.0, 0.0, 0.0, 0.0, 0.3, THRUSTER_FLAME_TOL);
    checkPoint(TEXT("straight: out[26], the tail cap"), out[26], -45.075, 0.0, 0.0, 0.0, 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("straight: out[1..25] are the spine unchanged"), spineChanges(out, straight, 25), 0);

    // Bent ends: spine (0, 0, 0), (-3, 4, 0), (-6, 8, 0), alphas 1, 0.5, 0.25, widths 2, 1, 0.5; end segments of 5 m
    // along (-0.6, 0.8, 0)
    const TArray<FSOLRibbonPoint> bent = { ThrusterMakeRibbonPoint(0.0f, 0.0f, 0.0f, 1.0f, 2.0f),
        ThrusterMakeRibbonPoint(-3.0f, 4.0f, 0.0f, 0.5f, 1.0f),
        ThrusterMakeRibbonPoint(-6.0f, 8.0f, 0.0f, 0.25f, 0.5f) };
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    TestEqual(TEXT("bent, r_n 0.8: returns 5"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(bent), 0.8, MakeArrayView(out)), 5);
    checkPoint(TEXT("bent, r_n 0.8 (d 0.04): out[0]"), out[0], 0.024, -0.032, 0.0, 1.0, 0.0, THRUSTER_FLAME_TOL);
    checkPoint(TEXT("bent, r_n 0.8: out[4]"), out[4], -6.024, 8.032, 0.0, 0.25, 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("bent, r_n 0.8: out[1..3] are the spine unchanged"), spineChanges(out, bent, 3), 0);
    TestEqual(TEXT("bent, r_n 0.8: out[5..26] untouched"), touchedFrom(out, 5), 0);
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    TestEqual(TEXT("bent, r_n 10: returns 5"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(bent), 10.0, MakeArrayView(out)), 5);
    checkPoint(TEXT("bent, r_n 10 (d 0.5): out[0]"), out[0], 0.3, -0.4, 0.0, 1.0, 0.0, THRUSTER_FLAME_TOL);
    checkPoint(TEXT("bent, r_n 10 (d 0.5): out[4]"), out[4], -6.3, 8.4, 0.0, 0.25, 0.0, THRUSTER_FLAME_TOL);

    // One point: both caps on the point itself, with its alpha and width 0
    const TArray<FSOLRibbonPoint> single = { ThrusterMakeRibbonPoint(1.0f, 2.0f, 3.0f, 0.7f, 2.0f) };
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    TestEqual(TEXT("one point: returns 3"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(single), 1.5, MakeArrayView(out)), 3);
    checkPoint(TEXT("one point: out[0] on the point"), out[0], 1.0, 2.0, 3.0, 0.7, 0.0, 1.0e-9);
    checkPoint(TEXT("one point: out[2] on the point"), out[2], 1.0, 2.0, 3.0, 0.7, 0.0, 1.0e-9);
    TestTrue(TEXT("one point: out[1] is the point unchanged"), ThrusterRibbonPointSame(out[1], single[0]));
    TestEqual(TEXT("one point: out[3..26] untouched"), touchedFrom(out, 3), 0);

    // Two points 5e-7 m apart (near the origin, so the gap survives float storage): each cap on its own end point
    const TArray<FSOLRibbonPoint> nearPair = { ThrusterMakeRibbonPoint(0.0f, 0.0f, 0.0f, 0.9f, 1.0f),
        ThrusterMakeRibbonPoint(5.0e-7f, 0.0f, 0.0f, 0.3f, 0.5f) };
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    TestEqual(TEXT("two points 5e-7 m apart: returns 4"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(nearPair), 1.5, MakeArrayView(out)), 4);
    checkPoint(TEXT("two points 5e-7 m apart: out[0] on spine 0"), out[0], 0.0, 0.0, 0.0, 0.9, 0.0, 1.0e-9);
    checkPoint(TEXT("two points 5e-7 m apart: out[3] on spine 1"), out[3],
        static_cast<double>(nearPair[1].PositionM.X), 0.0, 0.0, 0.3, 0.0, 1.0e-9);
    TestEqual(TEXT("two points 5e-7 m apart: out[1..2] are the spine unchanged"), spineChanges(out, nearPair, 2), 0);

    // 30 spine points (points 25..29 off the line, so any use of them shows): n = 25, returns 27
    TArray<FSOLRibbonPoint> longSpine = straight;
    for (int32 k = 0; k < 5; ++k)
    {
        longSpine.Add(ThrusterMakeRibbonPoint(100.0f, 100.0f, 100.0f + static_cast<float>(k), 0.5f, 7.0f));
    }
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    TestEqual(TEXT("30 spine points: returns 27"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(longSpine), 1.5, MakeArrayView(out)), 27);
    checkPoint(TEXT("30 spine points: out[0], the nozzle cap"), out[0], -14.925, 0.0, 0.0, 0.8, 0.0,
        THRUSTER_FLAME_TOL);
    checkPoint(TEXT("30 spine points: out[26], the tail cap past spine 24"), out[26], -45.075, 0.0, 0.0, 0.0, 0.0,
        THRUSTER_FLAME_TOL);
    TestEqual(TEXT("30 spine points: out[1..25] are spine 0..24"), spineChanges(out, longSpine, 25), 0);

    // An output of 26 for a 25-point spine: too short, returns 0 and writes nothing
    out.Init(ThrusterRibbonPointSentinel(), 26);
    TestEqual(TEXT("output of 26: returns 0"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(straight), 1.5, MakeArrayView(out)), 0);
    TestEqual(TEXT("output of 26: nothing written"), touchedFrom(out, 0), 0);

    // An output of 30: returns 27, entries 27..29 untouched
    out.Init(ThrusterRibbonPointSentinel(), 30);
    TestEqual(TEXT("output of 30: returns 27"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(straight), 1.5, MakeArrayView(out)), 27);
    checkPoint(TEXT("output of 30: out[26], the tail cap"), out[26], -45.075, 0.0, 0.0, 0.0, 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("output of 30: entries 27..29 untouched"), touchedFrom(out, 27), 0);

    // An empty spine: returns 0, the sentinel-filled output unchanged
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    TestEqual(TEXT("empty spine: returns 0"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(), 1.5, MakeArrayView(out)), 0);
    TestEqual(TEXT("empty spine: nothing written"), touchedFrom(out, 0), 0);

    // Away from the anchor: the straight example shifted by (1000, -2000, 300) m; each cap 0.075 m beyond its end,
    // +x at the nozzle and -x at the tail, measured in double from the stored floats (1e-3 m)
    const TArray<FSOLRibbonPoint> shifted = ThrusterFlameStraightSpine(FVector3d(1000.0, -2000.0, 300.0), false);
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    TestEqual(TEXT("shifted: returns 27"),
        AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(shifted), 1.5, MakeArrayView(out)), 27);
    const FVector3d nozzleStep = FVector3d(out[0].PositionM) - FVector3d(out[1].PositionM);
    const FVector3d tailStep = FVector3d(out[26].PositionM) - FVector3d(out[25].PositionM);
    TestTrue(FString::Printf(TEXT("shifted: nozzle cap (0.075, 0, 0) from spine 0, got (%.6f, %.6f, %.6f)"),
        nozzleStep.X, nozzleStep.Y, nozzleStep.Z), nozzleStep.Equals(FVector3d(0.075, 0.0, 0.0), 1.0e-3));
    TestTrue(FString::Printf(TEXT("shifted: tail cap (-0.075, 0, 0) from spine 24, got (%.6f, %.6f, %.6f)"),
        tailStep.X, tailStep.Y, tailStep.Z), tailStep.Equals(FVector3d(-0.075, 0.0, 0.0), 1.0e-3));

    // Built in place (the strip pool's fill): the straight spine already at out[1..25]; the same 27 points as the
    // separate-buffer call above
    TArray<FSOLRibbonPoint> separate;
    separate.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(straight), 1.5, MakeArrayView(separate));
    out.Init(ThrusterRibbonPointSentinel(), drawnPoints);
    for (int32 j = 0; j < straight.Num(); ++j)
    {
        out[j + 1] = straight[j];
    }
    TestEqual(TEXT("in place: returns 27"), AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(out.GetData() + 1,
        straight.Num()), 1.5, MakeArrayView(out)), 27);
    int32 inPlaceDiffs = 0;
    for (int32 p = 0; p < drawnPoints; ++p)
    {
        inPlaceDiffs += ThrusterRibbonPointSame(out[p], separate[p]) ? 0 : 1;
    }
    TestEqual(TEXT("in place: the same 27 points as a separate output"), inPlaceDiffs, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterThrustGateTest, "SOLTest.Thruster.ThrustGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 19 (rewritten for R10, SDD 51 R4, R9, R10-7): the Collect pass's NPC thrust gate max(glow reach, burst reach,
// flame reach): glow f 2.5 r_n / 0.5, burst f BurstLengthM(L_hull, 1) / 0.5, flame RibbonReachM while the flames are
// on and 0 when they are off; the cone term is gone
bool FSOLThrusterThrustGateTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    constexpr double focalRefPx = 960.0;                            // FocalRefPx(90, 16/9)

    // The constants the glow and burst terms are built from
    const double glowRadii = static_cast<double>(SOL::Thruster::GLOW_DIAMETER_NOZZLE_RADII);
    const double glowLowPx = static_cast<double>(SOL::Thruster::GLOW_FADE_LOW_PX);
    const double burstLowPx = static_cast<double>(SOL::Thruster::CONE_FADE_LOW_PX);
    TestEqual(TEXT("GLOW_DIAMETER_NOZZLE_RADII 2.5"), glowRadii, 2.5, 1.0e-9);
    TestEqual(TEXT("GLOW_FADE_LOW_PX 0.5"), glowLowPx, 0.5, 1.0e-9);
    TestEqual(TEXT("CONE_FADE_LOW_PX 0.5 (the bursts' low edge)"), burstLowPx, 0.5, 1.0e-9);

    // Flames off: glow 7200 m, burst 9600 m for a 30 m hull; V ignored
    TestEqual(TEXT("gate (30, 1.5, 960, off, 1) = 9600 m"), NpcThrustGateM(30.0, 1.5, focalRefPx, false, 1.0), 9600.0,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("gate (30, 1.5, 960, off, 10) = 9600 m (V ignored)"),
        NpcThrustGateM(30.0, 1.5, focalRefPx, false, 10.0), 9600.0, THRUSTER_LEN_TOL);

    // Flames on: the flame reach 960 x 90 / V wins while it is beyond the burst term
    TestEqual(TEXT("gate (30, 1.5, 960, on, 1) = 86400 m"), NpcThrustGateM(30.0, 1.5, focalRefPx, true, 1.0), 86400.0,
        THRUSTER_LEN_TOL);
    TestEqual(TEXT("gate (30, 1.5, 960, on, 2.2) = 39272.7 m"), NpcThrustGateM(30.0, 1.5, focalRefPx, true, 2.2),
        39272.7, 0.1);
    TestEqual(TEXT("gate (30, 1.5, 960, on, 4.6) = 18782.6 m"), NpcThrustGateM(30.0, 1.5, focalRefPx, true, 4.6),
        18782.6, 0.1);
    TestEqual(TEXT("gate (30, 1.5, 960, on, 10) = 9600 m (the burst beats the flame's 8640)"),
        NpcThrustGateM(30.0, 1.5, focalRefPx, true, 10.0), 9600.0, THRUSTER_LEN_TOL);
    TestEqual(TEXT("gate (30, 1.5, 960, on, 100) = 9600 m"), NpcThrustGateM(30.0, 1.5, focalRefPx, true, 100.0),
        9600.0, THRUSTER_LEN_TOL);

    // Bad V with the flames on: the flame term is 0, the burst term remains
    for (const double vanishPx : { 0.0, -1.0, THRUSTER_NAN })
    {
        TestEqual(FString::Printf(TEXT("gate on, V %f: 9600 m"), vanishPx),
            NpcThrustGateM(30.0, 1.5, focalRefPx, true, vanishPx), 9600.0, THRUSTER_LEN_TOL);
    }

    // Bad hull length: the burst and flame terms 0, the glow's 7200 m remains, flames on and off
    for (const double hullM : { 0.0, -30.0, THRUSTER_NAN })
    {
        TestEqual(FString::Printf(TEXT("gate on, hull %f: 7200 m"), hullM),
            NpcThrustGateM(hullM, 1.5, focalRefPx, true, 1.0), 7200.0, THRUSTER_LEN_TOL);
        TestEqual(FString::Printf(TEXT("gate off, hull %f: 7200 m"), hullM),
            NpcThrustGateM(hullM, 1.5, focalRefPx, false, 1.0), 7200.0, THRUSTER_LEN_TOL);
    }

    // Bad nozzle radius: the glow term 0
    for (const double radiusM : { 0.0, -1.5, THRUSTER_NAN })
    {
        TestEqual(FString::Printf(TEXT("gate on, V 1, r_n %f: 86400 m"), radiusM),
            NpcThrustGateM(30.0, radiusM, focalRefPx, true, 1.0), 86400.0, THRUSTER_LEN_TOL);
        TestEqual(FString::Printf(TEXT("gate off, r_n %f: 9600 m"), radiusM),
            NpcThrustGateM(30.0, radiusM, focalRefPx, false, 1.0), 9600.0, THRUSTER_LEN_TOL);
    }

    // Bad focal: every term 0, flames on and off
    for (const double focalPx : { 0.0, -960.0, THRUSTER_NAN })
    {
        TestEqual(FString::Printf(TEXT("gate on, focal %f: 0"), focalPx),
            NpcThrustGateM(30.0, 1.5, focalPx, true, 1.0), 0.0, THRUSTER_LEN_TOL);
        TestEqual(FString::Printf(TEXT("gate off, focal %f: 0"), focalPx),
            NpcThrustGateM(30.0, 1.5, focalPx, false, 1.0), 0.0, THRUSTER_LEN_TOL);
    }

    // Flames off: exactly max(glow reach, burst reach) over a grid of hulls, nozzle radii and focal lengths
    for (const double hullM : { 20.0, 30.0, 45.0 })
    {
        for (const double radiusM : { 0.8, 1.5 })
        {
            for (const double focalPx : { 640.0, 960.0, 1440.0 })
            {
                const double glowReachM = focalPx * glowRadii * radiusM / glowLowPx;
                const double burstReachM = focalPx * BurstLengthM(hullM, 1.0f) / burstLowPx;
                TestEqual(FString::Printf(TEXT("gate off (%.0f, %.1f, %.0f) = max(glow, burst)"), hullM, radiusM,
                    focalPx), NpcThrustGateM(hullM, radiusM, focalPx, false, 1.0),
                    FMath::Max(glowReachM, burstReachM), THRUSTER_LEN_TOL);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameProfileTest, "SOLTest.Thruster.FlameProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 20 (SDD 51 R10-1 to R10-3): the flame length 3 L_p, the full width W(s) tapering by a smoothstep from 2 r_n to
// 0.2 r_n at L_p and constant beyond, widest at the nozzle, no kink at L_p, its slope bound, and the alpha 1 - s / L_f
bool FSOLThrusterFlameProfileTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const double tailRatio = static_cast<double>(SOL::Thruster::FLAME_TAIL_WIDTH_RATIO);

    // Flame length
    TestEqual(TEXT("L_f(9.659011) = 28.977033"), FlameLengthM(9.659011), 28.977033, THRUSTER_FLAME_LEN_TOL);
    TestEqual(TEXT("L_f(30) = 90"), FlameLengthM(30.0), 90.0, THRUSTER_FLAME_LEN_TOL);
    for (const double plumeLengthM : { 0.0, -10.0, THRUSTER_NAN, THRUSTER_INF })
    {
        TestEqual(FString::Printf(TEXT("L_f(%f) = 0"), plumeLengthM), FlameLengthM(plumeLengthM), 0.0,
            THRUSTER_FLAME_LEN_TOL);
    }

    // Width table with L_p 10, r_n 1.5: the taper to L_p, the constant tail past it (the width does not end at L_f)
    const double widthArcs[] = { 0.0, 1.25, 2.5, 5.0, 7.5, 10.0, 20.0, 30.0, 40.0, -1.0 };
    const double widthValues[] = { 3.0, 2.883984, 2.578125, 1.65, 0.721875, 0.3, 0.3, 0.3, 0.3, 3.0 };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(widthArcs)); ++index)
    {
        TestEqual(FString::Printf(TEXT("W(%.2f, 10, 1.5) = %.6f"), widthArcs[index], widthValues[index]),
            FlameWidthM(widthArcs[index], 10.0, 1.5), widthValues[index], THRUSTER_FLAME_LEN_TOL);
    }

    // Width: bad L_p, r_n or s give 0
    for (const double bad : { 0.0, -10.0, THRUSTER_NAN, THRUSTER_INF })
    {
        TestEqual(FString::Printf(TEXT("W with L_p %f = 0"), bad), FlameWidthM(5.0, bad, 1.5), 0.0,
            THRUSTER_FLAME_LEN_TOL);
        TestEqual(FString::Printf(TEXT("W with r_n %f = 0"), bad), FlameWidthM(5.0, 10.0, bad), 0.0,
            THRUSTER_FLAME_LEN_TOL);
    }
    TestEqual(TEXT("W with s NaN = 0"), FlameWidthM(THRUSTER_NAN, 10.0, 1.5), 0.0, THRUSTER_FLAME_LEN_TOL);

    // Sweeps over s from 0 to 3 L_p in steps of L_p / 1000: widest at the nozzle (never rising) and the slope bound
    for (const double plumeLengthM : { 9.659011, 30.0 })
    {
        for (const double nozzleRadiusM : { 0.8, 1.5 })
        {
            const double stepM = plumeLengthM / 1000.0;
            const double slopeBound = 1.5 * 2.0 * nozzleRadiusM * (1.0 - tailRatio) * stepM / plumeLengthM + 1.0e-9;
            double previous = FlameWidthM(0.0, plumeLengthM, nozzleRadiusM);
            int32 rises = 0;
            int32 steep = 0;
            for (int32 k = 1; k <= 3000; ++k)
            {
                const double width = FlameWidthM(static_cast<double>(k) * stepM, plumeLengthM, nozzleRadiusM);
                rises += width > previous + 1.0e-9 ? 1 : 0;
                steep += FMath::Abs(width - previous) > slopeBound ? 1 : 0;
                previous = width;
            }
            const FString label = FString::Printf(TEXT("sweep L_p %.6f, r_n %.1f"), plumeLengthM, nozzleRadiusM);
            TestEqual(label + TEXT(": the width never rises"), rises, 0);
            TestEqual(label + TEXT(": every step within the slope bound"), steep, 0);

            // No kink at L_p (C1): the slope just before L_p is near 0, the slope just after is 0
            const double h = 1.0e-4 * plumeLengthM;
            const double atLp = FlameWidthM(plumeLengthM, plumeLengthM, nozzleRadiusM);
            const double before = FlameWidthM(plumeLengthM - h, plumeLengthM, nozzleRadiusM);
            const double after = FlameWidthM(plumeLengthM + h, plumeLengthM, nozzleRadiusM);
            TestTrue(label + TEXT(": slope just before L_p <= 1e-3 x 2 r_n / L_p"),
                FMath::Abs(atLp - before) / h <= 1.0e-3 * 2.0 * nozzleRadiusM / plumeLengthM);
            TestTrue(label + TEXT(": slope just after L_p <= 1e-9"), FMath::Abs(after - atLp) / h <= 1.0e-9);
        }
    }

    // Along-arc alpha clamp(1 - s / L_f, 0, 1)
    TestEqual(TEXT("alpha (0, 30) = 1"), FlameAlongAlpha(0.0, 30.0), 1.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("alpha (10, 30) = 0.666667"), FlameAlongAlpha(10.0, 30.0), 0.666667, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("alpha (30, 30) = 0"), FlameAlongAlpha(30.0, 30.0), 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("alpha (45, 30) = 0"), FlameAlongAlpha(45.0, 30.0), 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("alpha (-5, 30) = 1"), FlameAlongAlpha(-5.0, 30.0), 1.0, THRUSTER_FLAME_TOL);
    for (const double flameLengthM : { 0.0, -30.0, THRUSTER_NAN })
    {
        TestEqual(FString::Printf(TEXT("alpha with L_f %f = 0"), flameLengthM), FlameAlongAlpha(10.0, flameLengthM),
            0.0, THRUSTER_FLAME_TOL);
    }
    TestEqual(TEXT("alpha with s NaN = 0"), FlameAlongAlpha(THRUSTER_NAN, 30.0), 0.0, THRUSTER_FLAME_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameParcelsTest, "SOLTest.Thruster.FlameParcels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 21 (SDD 51 R10-4, R10-8): the gas-parcel spine: point j at s_j = j L_f / 24 is the parcel of age s_j / v_e,
// its pose interpolated between the bracketing poses (head first), position ship + offset + (v_e exhaust + nozzle
// velocity) tau - anchor, alpha b x (1 - s / L_f) x fade, unfloored width W(s); not-yet-emitted points alpha 0
bool FSOLThrusterFlameParcelsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    const FSOLRibbonPoint sentinel = ThrusterRibbonPointSentinel();
    constexpr double exhaustSpeedMps = 2000.0;                      // v_e, a contract parameter
    constexpr double plumeLengthM = 10.0;                           // L_f 30, s_j = 1.25 j
    constexpr double nozzleRadiusM = 1.5;
    const FVector3d ship(1000.0, 2000.0, 3000.0);
    const FSOLPlumeSample head = ThrusterFlameStraightPose(10.0, 0.8f);
    FSOLPlumeHistory history;
    ThrusterFlameStraightHistory(history, 0.6f, 0.8f);
    TestEqual(TEXT("straight example history: 2 poses"), history.Num(), 2);
    TArray<FSOLRibbonPoint> points;

    // The straight example's emission brightness at point j: 0.8 back to 9.99 s (point 16), then the lerp to 0.6
    const auto straightBrightness = [](const int32 j)
    {
        return j <= 16 ? 0.8 : 1.0 - 0.0125 * static_cast<double>(j);
    };

    // Checks every point of a 25-point build against the straight line (-15 - s_j, 0, 0) + base, the given alpha per
    // point and W(s_j) (or 0), and returns nothing; one assertion per build
    const auto checkStraight = [this, &points](const FString& label, const FVector3d& base, const double positionTol,
        const TFunctionRef<double(int32)> alphaOf, const bool bZeroWidth)
    {
        int32 bad = 0;
        int32 firstBad = INDEX_NONE;
        for (int32 j = 0; j < 25 && j < points.Num(); ++j)
        {
            const double arcM = 1.25 * static_cast<double>(j);
            const double widthM = bZeroWidth ? 0.0 : ThrusterFlameWidthRef(arcM, 10.0, 1.5);
            const bool bNear = ThrusterFlamePointNear(points[j], base.X - 15.0 - arcM, base.Y, base.Z, alphaOf(j),
                widthM, positionTol);
            bad += bNear ? 0 : 1;
            firstBad = (!bNear && firstBad == INDEX_NONE) ? j : firstBad;
        }
        FString detail;
        if (points.IsValidIndex(firstBad))
        {
            const FSOLRibbonPoint& point = points[firstBad];
            detail = FString::Printf(TEXT(" (first bad point %d = (%.5f, %.5f, %.5f), alpha %.6f, width %.6f)"),
                firstBad, point.PositionM.X, point.PositionM.Y, point.PositionM.Z, point.Alpha, point.WidthM);
        }
        TestEqual(label + TEXT(": every point on the straight line with its alpha and width") + detail, bad, 0);
    };

    // Straight example: returns 25, nothing written past point 24 of a 30-element view
    points.Init(sentinel, 30);
    TestEqual(TEXT("straight: returns 25"), BuildFlamePoints(history, head, ship, plumeLengthM, nozzleRadiusM,
        exhaustSpeedMps, 1.0, ship, MakeArrayView(points)), 25);
    int32 touchedPast = 0;
    for (int32 index = 25; index < points.Num(); ++index)
    {
        touchedPast += ThrusterRibbonPointIsSentinel(points[index]) ? 0 : 1;
    }
    TestEqual(TEXT("straight: elements 25..29 untouched"), touchedPast, 0);

    // The contract table rows (j, alpha, width), positions (-15 - s_j, 0, 0)
    struct FRow { int32 J; double Alpha; double WidthM; };
    const FRow rows[] = { { 0, 0.8, 3.0 }, { 4, 0.666667, 1.65 }, { 8, 0.533333, 0.3 }, { 16, 0.266667, 0.3 },
        { 20, 0.125, 0.3 }, { 24, 0.0, 0.3 } };
    for (const FRow& row : rows)
    {
        const FSOLRibbonPoint& point = points[row.J];
        TestTrue(FString::Printf(TEXT("straight: point %d at (%.2f, 0, 0), alpha %.6f, width %.4f (got (%.5f, %.5f, "
            "%.5f), %.6f, %.6f)"), row.J, -15.0 - 1.25 * row.J, row.Alpha, row.WidthM, point.PositionM.X,
            point.PositionM.Y, point.PositionM.Z, point.Alpha, point.WidthM), ThrusterFlamePointNear(point,
            -15.0 - 1.25 * static_cast<double>(row.J), 0.0, 0.0, row.Alpha, row.WidthM, THRUSTER_FLAME_LEN_TOL));
    }

    // Every point of the straight example: alpha b_j (1 - j / 24)
    checkStraight(TEXT("straight"), FVector3d::ZeroVector, THRUSTER_FLAME_LEN_TOL, [&straightBrightness](const int32 j)
    {
        return straightBrightness(j) * (1.0 - static_cast<double>(j) / 24.0);
    }, false);

    // One pose only, at t 9.9897 (b 0.8, off the point grid): points 17..24 predate it, alpha 0, still on the line
    FSOLPlumeHistory single;
    single.Reset();
    single.Push(ThrusterFlameStraightPose(9.9897, 0.8f));
    points.Init(sentinel, 25);
    TestEqual(TEXT("one pose: returns 25"), BuildFlamePoints(single, head, ship, plumeLengthM, nozzleRadiusM,
        exhaustSpeedMps, 1.0, ship, MakeArrayView(points)), 25);
    checkStraight(TEXT("one pose at 9.9897"), FVector3d::ZeroVector, THRUSTER_FLAME_LEN_TOL, [](const int32 j)
    {
        return j <= 16 ? 0.8 * (1.0 - static_cast<double>(j) / 24.0) : 0.0;
    }, false);

    // Empty history (ignition): point 0 alpha 0.8, the rest not yet emitted, all on the head's straight line
    FSOLPlumeHistory empty;
    empty.Reset();
    points.Init(sentinel, 25);
    TestEqual(TEXT("ignition: returns 25"), BuildFlamePoints(empty, head, ship, plumeLengthM, nozzleRadiusM,
        exhaustSpeedMps, 1.0, ship, MakeArrayView(points)), 25);
    checkStraight(TEXT("ignition"), FVector3d::ZeroVector, THRUSTER_FLAME_LEN_TOL, [](const int32 j)
    {
        return j == 0 ? 0.8 : 0.0;
    }, false);

    // Fade 0.5 halves every alpha; fade NaN or -1 zeroes every alpha
    points.Init(sentinel, 25);
    BuildFlamePoints(history, head, ship, plumeLengthM, nozzleRadiusM, exhaustSpeedMps, 0.5, ship,
        MakeArrayView(points));
    checkStraight(TEXT("fade 0.5"), FVector3d::ZeroVector, THRUSTER_FLAME_LEN_TOL, [&straightBrightness](const int32 j)
    {
        return 0.5 * straightBrightness(j) * (1.0 - static_cast<double>(j) / 24.0);
    }, false);
    for (const double fade : { THRUSTER_NAN, -1.0 })
    {
        points.Init(sentinel, 25);
        BuildFlamePoints(history, head, ship, plumeLengthM, nozzleRadiusM, exhaustSpeedMps, fade, ship,
            MakeArrayView(points));
        checkStraight(FString::Printf(TEXT("fade %f"), fade), FVector3d::ZeroVector, THRUSTER_FLAME_LEN_TOL,
            [](const int32 /*j*/)
        {
            return 0.0;
        }, false);
    }

    // Nozzle radius NaN: every width 0, positions and alphas unchanged
    points.Init(sentinel, 25);
    TestEqual(TEXT("r_n NaN: returns 25"), BuildFlamePoints(history, head, ship, plumeLengthM, THRUSTER_NAN,
        exhaustSpeedMps, 1.0, ship, MakeArrayView(points)), 25);
    checkStraight(TEXT("r_n NaN, every width 0"), FVector3d::ZeroVector, THRUSTER_FLAME_LEN_TOL,
        [&straightBrightness](const int32 j)
    {
        return straightBrightness(j) * (1.0 - static_cast<double>(j) / 24.0);
    }, true);

    // A 3-point view: returns 3, points 0..2 as the straight example, element 3 of the backing array untouched
    points.Init(sentinel, 25);
    TestEqual(TEXT("3-point view: returns 3"), BuildFlamePoints(history, head, ship, plumeLengthM, nozzleRadiusM,
        exhaustSpeedMps, 1.0, ship, MakeArrayView(points.GetData(), 3)), 3);
    for (int32 j = 0; j < 3; ++j)
    {
        const double arcM = 1.25 * static_cast<double>(j);
        TestTrue(FString::Printf(TEXT("3-point view: point %d as the straight example"), j), ThrusterFlamePointNear(
            points[j], -15.0 - arcM, 0.0, 0.0, 0.8 * (1.0 - static_cast<double>(j) / 24.0),
            ThrusterFlameWidthRef(arcM, 10.0, 1.5), THRUSTER_FLAME_LEN_TOL));
    }
    TestTrue(TEXT("3-point view: element 3 untouched"), ThrusterRibbonPointIsSentinel(points[3]));

    // Returns 0 and writes nothing: an empty view, L_f 0, or v_e not finite and positive
    TestEqual(TEXT("empty view: returns 0"), BuildFlamePoints(history, head, ship, plumeLengthM, nozzleRadiusM,
        exhaustSpeedMps, 1.0, ship, TArrayView<FSOLRibbonPoint>()), 0);
    struct FRefused { const TCHAR* Label; double PlumeLengthM; double ExhaustSpeedMps; };
    const FRefused refusals[] = { { TEXT("L_p 0"), 0.0, exhaustSpeedMps }, { TEXT("v_e 0"), plumeLengthM, 0.0 },
        { TEXT("v_e -2000"), plumeLengthM, -2000.0 }, { TEXT("v_e NaN"), plumeLengthM, THRUSTER_NAN },
        { TEXT("v_e inf"), plumeLengthM, THRUSTER_INF } };
    for (const FRefused& refusal : refusals)
    {
        points.Init(sentinel, 25);
        TestEqual(FString(refusal.Label) + TEXT(": returns 0"), BuildFlamePoints(history, head, ship,
            refusal.PlumeLengthM, nozzleRadiusM, refusal.ExhaustSpeedMps, 1.0, ship, MakeArrayView(points)), 0);
        int32 touched = 0;
        for (const FSOLRibbonPoint& point : points)
        {
            touched += ThrusterRibbonPointIsSentinel(point) ? 0 : 1;
        }
        TestEqual(FString(refusal.Label) + TEXT(": nothing written"), touched, 0);
    }

    // Steady turn at 2 rad/s, nozzle 15 m behind the centre, L_p 30 (L_f 90, s_j = 3.75 j): the spine follows the
    // parcel model's lateral offset and along-axis distance within 0.02 m at every point
    {
        constexpr double omegaRadS = 2.0;
        constexpr double nozzleOffsetM = 15.0;
        FSOLPlumeHistory turnHistory;
        ThrusterFlameTurnHistory(turnHistory);
        TestEqual(TEXT("turn: 7 poses"), turnHistory.Num(), 7);
        points.Init(sentinel, 25);
        TestEqual(TEXT("turn: returns 25"), BuildFlamePoints(turnHistory, ThrusterFlameTurnPose(10.0),
            FVector3d::ZeroVector, 30.0, nozzleRadiusM, exhaustSpeedMps, 1.0, FVector3d::ZeroVector,
            MakeArrayView(points)), 25);
        int32 offLateral = 0;
        int32 offAxial = 0;
        for (int32 j = 0; j < 25; ++j)
        {
            const double arcM = 3.75 * static_cast<double>(j);
            const double phi = omegaRadS * arcM / exhaustSpeedMps;
            const double axialM = (nozzleOffsetM + arcM) * FMath::Cos(phi) + nozzleOffsetM * phi * FMath::Sin(phi)
                - nozzleOffsetM;
            const double y = static_cast<double>(points[j].PositionM.Y);
            const double x = static_cast<double>(points[j].PositionM.X);
            offLateral += FMath::Abs(y - FlameTurnOffsetM(arcM, omegaRadS, exhaustSpeedMps, nozzleOffsetM)) <= 0.02
                ? 0 : 1;
            offAxial += FMath::Abs((-x - nozzleOffsetM) - axialM) <= 0.02 ? 0 : 1;
        }
        TestEqual(TEXT("turn: every point's lateral offset within 0.02 m of FlameTurnOffsetM"), offLateral, 0);
        TestEqual(TEXT("turn: every point's axial distance within 0.02 m of X(s)"), offAxial, 0);
        TestTrue(FString::Printf(TEXT("turn: point 8 at (-44.993249, 0.900000) within 0.02 m (got (%.6f, %.6f))"),
            points[8].PositionM.X, points[8].PositionM.Y), FMath::Abs(points[8].PositionM.X + 44.993249) <= 0.02
            && FMath::Abs(points[8].PositionM.Y - 0.900000) <= 0.02);
        TestTrue(FString::Printf(TEXT("turn: point 24 at (-104.696373, 8.092711) within 0.02 m (got (%.6f, %.6f))"),
            points[24].PositionM.X, points[24].PositionM.Y), FMath::Abs(points[24].PositionM.X + 104.696373) <= 0.02
            && FMath::Abs(points[24].PositionM.Y - 8.092711) <= 0.02);
    }

    // Far from the origin: the anchor is subtracted in double before the float store
    const FVector3d farShip(1.5e11, 2.0e10, -3.0e9);
    points.Init(sentinel, 25);
    TestEqual(TEXT("far from the origin: returns 25"), BuildFlamePoints(history, head, farShip, plumeLengthM,
        nozzleRadiusM, exhaustSpeedMps, 1.0, farShip, MakeArrayView(points)), 25);
    checkStraight(TEXT("far from the origin"), FVector3d::ZeroVector, THRUSTER_LEN_TOL,
        [&straightBrightness](const int32 j)
    {
        return straightBrightness(j) * (1.0 - static_cast<double>(j) / 24.0);
    }, false);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameRigidityTest, "SOLTest.Thruster.FlameRigidity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 22 (SDD 51 R10-5, R10-6): the steady-turn lateral offset (a + s) sin(phi) - a phi cos(phi), phi = omega s / v_e:
// the table values, the sign with the turn, the square law omega s^2 / v_e for small phi, and 0 for bad input
bool FSOLThrusterFlameRigidityTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    constexpr double exhaustSpeedMps = 2000.0;                      // v_e, a contract parameter

    // Values at 2 rad/s, nozzle 15 m behind the centre
    const double arcs[] = { 0.0, 10.0, 30.0, 60.0, 90.0 };
    const double offsets[] = { 0.0, 0.100003, 0.900000, 3.598920, 8.092711 };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(arcs)); ++index)
    {
        TestEqual(FString::Printf(TEXT("delta(%.0f, 2, 2000, 15) = %.6f"), arcs[index], offsets[index]),
            FlameTurnOffsetM(arcs[index], 2.0, exhaustSpeedMps, 15.0), offsets[index], THRUSTER_FLAME_TOL);
    }

    // Nozzle at the centre, the turn reversed, no turn
    TestEqual(TEXT("delta(90, 2, 2000, 0) = 8.089069"), FlameTurnOffsetM(90.0, 2.0, exhaustSpeedMps, 0.0), 8.089069,
        THRUSTER_FLAME_TOL);
    TestEqual(TEXT("delta(90, -2, 2000, 15) = -8.092711"), FlameTurnOffsetM(90.0, -2.0, exhaustSpeedMps, 15.0),
        -8.092711, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("delta(90, 0, 2000, 15) = 0"), FlameTurnOffsetM(90.0, 0.0, exhaustSpeedMps, 15.0), 0.0,
        THRUSTER_FLAME_TOL);

    // Square law: within 1 % of omega s^2 / v_e wherever phi <= 0.1
    int32 offLaw = 0;
    int32 checked = 0;
    for (const double arcM : { 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0 })
    {
        for (const double omegaRadS : { 0.5, 1.0, 2.0 })
        {
            for (const double nozzleOffsetM : { 0.0, 5.0, 15.0 })
            {
                if (omegaRadS * arcM / exhaustSpeedMps > 0.1)
                {
                    continue;
                }
                const double law = omegaRadS * arcM * arcM / exhaustSpeedMps;
                const double delta = FlameTurnOffsetM(arcM, omegaRadS, exhaustSpeedMps, nozzleOffsetM);
                offLaw += FMath::Abs(delta - law) <= 0.01 * law ? 0 : 1;
                ++checked;
            }
        }
    }
    TestTrue(TEXT("square law: cases checked"), checked > 0);
    TestEqual(TEXT("square law: every case within 1 % of omega s^2 / v_e"), offLaw, 0);

    // At 127 degree/s (2.2165682 rad/s), nozzle 15 m behind: the SDD R10-6 table
    constexpr double tableOmegaRadS = 2.2165682;
    const double tableArcs[] = { 9.659011, 28.977033, 30.0, 90.0 };
    const double tableOffsets[] = { 0.1034, 0.9306, 0.9975, 8.9672 };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(tableArcs)); ++index)
    {
        TestEqual(FString::Printf(TEXT("127 deg/s: delta(%.6f) = %.4f"), tableArcs[index], tableOffsets[index]),
            FlameTurnOffsetM(tableArcs[index], tableOmegaRadS, exhaustSpeedMps, 15.0), tableOffsets[index], 1.0e-3);
    }

    // Bad input: s negative or NaN, v_e zero, negative or NaN, omega or a non-finite
    TestEqual(TEXT("s -10: 0"), FlameTurnOffsetM(-10.0, 2.0, exhaustSpeedMps, 15.0), 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("s NaN: 0"), FlameTurnOffsetM(THRUSTER_NAN, 2.0, exhaustSpeedMps, 15.0), 0.0, THRUSTER_FLAME_TOL);
    for (const double badSpeed : { 0.0, -2000.0, THRUSTER_NAN })
    {
        TestEqual(FString::Printf(TEXT("v_e %f: 0"), badSpeed), FlameTurnOffsetM(90.0, 2.0, badSpeed, 15.0), 0.0,
            THRUSTER_FLAME_TOL);
    }
    for (const double nonFinite : { THRUSTER_NAN, THRUSTER_INF, -THRUSTER_INF })
    {
        TestEqual(FString::Printf(TEXT("omega %f: 0"), nonFinite),
            FlameTurnOffsetM(90.0, nonFinite, exhaustSpeedMps, 15.0), 0.0, THRUSTER_FLAME_TOL);
        TestEqual(FString::Printf(TEXT("a %f: 0"), nonFinite),
            FlameTurnOffsetM(90.0, 2.0, exhaustSpeedMps, nonFinite), 0.0, THRUSTER_FLAME_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameSeamTest, "SOLTest.Thruster.FlameSeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 23 (SDD 51 R10-3, the user's "no visible seam"): nothing along the arc steps: built points' alphas fall by an
// equal amount per point and their widths never rise, fine sweeps of the width and alpha stay within their slope bounds
// with nothing special at L_p, and the material envelope terms (core, knots, streak depth) are continuous in s
bool FSOLThrusterFlameSeamTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLThruster;
    constexpr double exhaustSpeedMps = 2000.0;                      // v_e, a contract parameter
    constexpr double bodyDepth = 0.45;                              // Streak depths, contract parameters
    constexpr double tailDepth = 0.8;
    const double tailRatio = static_cast<double>(SOL::Thruster::FLAME_TAIL_WIDTH_RATIO);
    const FSOLRibbonPoint sentinel = ThrusterRibbonPointSentinel();
    TArray<FSOLRibbonPoint> points;

    // Checks a 25-point build: neighbouring alphas differ by alphaStep (1e-5) and the widths never rise
    const auto checkBuilt = [this, &points](const FString& label, const double alphaStep)
    {
        int32 unequalSteps = 0;
        int32 rises = 0;
        for (int32 j = 1; j < 25 && j < points.Num(); ++j)
        {
            const double step = static_cast<double>(points[j - 1].Alpha) - static_cast<double>(points[j].Alpha);
            unequalSteps += FMath::Abs(step - alphaStep) <= THRUSTER_FLAME_TOL ? 0 : 1;
            rises += points[j].WidthM <= points[j - 1].WidthM ? 0 : 1;
        }
        TestEqual(label + TEXT(": every alpha step equal"), unequalSteps, 0);
        TestEqual(label + TEXT(": the width never rises"), rises, 0);
    };

    // Built points, straight example with b 0.8 on every pose (L_p 10 at point 8)
    {
        FSOLPlumeHistory history;
        ThrusterFlameStraightHistory(history, 0.8f, 0.8f);
        points.Init(sentinel, 25);
        TestEqual(TEXT("straight b 0.8: returns 25"), BuildFlamePoints(history, ThrusterFlameStraightPose(10.0, 0.8f),
            FVector3d(1000.0, 2000.0, 3000.0), 10.0, 1.5, exhaustSpeedMps, 1.0, FVector3d(1000.0, 2000.0, 3000.0),
            MakeArrayView(points)), 25);
        checkBuilt(TEXT("straight b 0.8"), 0.8 / 24.0);

        // Every point, the three around L_p (7, 8, 9 at s 8.75, 10, 11.25) included, has exactly its s's W and alpha
        for (int32 j = 0; j < 25; ++j)
        {
            const double arcM = 1.25 * static_cast<double>(j);
            const bool bAroundLp = j >= 7 && j <= 9;
            const double width = static_cast<double>(points[j].WidthM);
            const double alpha = static_cast<double>(points[j].Alpha);
            const bool bExact = FMath::Abs(width - FlameWidthM(arcM, 10.0, 1.5)) <= THRUSTER_FLAME_TOL
                && FMath::Abs(alpha - 0.8 * FlameAlongAlpha(arcM, 30.0)) <= THRUSTER_FLAME_TOL;
            if (bAroundLp || !bExact)
            {
                TestTrue(FString::Printf(TEXT("straight b 0.8: point %d (s %.2f) has W(s) and 0.8 alpha(s) (got %.6f, "
                    "%.6f)"), j, arcM, width, alpha), bExact);
            }
        }
    }

    // Built points, the case 21 steady turn (b 1 on every pose)
    {
        FSOLPlumeHistory history;
        ThrusterFlameTurnHistory(history);
        points.Init(sentinel, 25);
        TestEqual(TEXT("turn: returns 25"), BuildFlamePoints(history, ThrusterFlameTurnPose(10.0),
            FVector3d::ZeroVector, 30.0, 1.5, exhaustSpeedMps, 1.0, FVector3d::ZeroVector, MakeArrayView(points)), 25);
        checkBuilt(TEXT("turn"), 1.0 / 24.0);
    }

    // Fine sweeps of W and alpha over s from 0 to L_f in steps of L_f / 10000: within the slope bounds, and the pair
    // straddling L_p no larger than the largest pair inside [0.5 L_p, 0.9 L_p] or [1.1 L_p, 2 L_p]
    struct FFlameCase { double PlumeLengthM; double NozzleRadiusM; };
    const FFlameCase flameCases[] = { { 9.659011, 1.5 }, { 30.0, 1.5 }, { 10.388462, 0.8 }, { 20.0, 0.8 } };
    constexpr int32 sweepSteps = 10000;
    for (const FFlameCase& flameCase : flameCases)
    {
        const double lp = flameCase.PlumeLengthM;
        const double rn = flameCase.NozzleRadiusM;
        const double flameLengthM = 3.0 * lp;
        const double stepM = flameLengthM / static_cast<double>(sweepSteps);
        const double widthBound = 1.5 * 2.0 * rn * (1.0 - tailRatio) * stepM / lp + 1.0e-9;
        const double alphaBound = stepM / flameLengthM + 1.0e-9;
        int32 widthSteep = 0;
        int32 alphaSteep = 0;
        double widthInteriorMax = 0.0;
        double alphaInteriorMax = 0.0;
        double widthStraddle = -1.0;
        double alphaStraddle = -1.0;
        double previousWidth = FlameWidthM(0.0, lp, rn);
        double previousAlpha = FlameAlongAlpha(0.0, flameLengthM);
        for (int32 k = 1; k <= sweepSteps; ++k)
        {
            const double previousArc = static_cast<double>(k - 1) * stepM;
            const double arcM = static_cast<double>(k) * stepM;
            const double width = FlameWidthM(arcM, lp, rn);
            const double alpha = FlameAlongAlpha(arcM, flameLengthM);
            const double widthPair = FMath::Abs(width - previousWidth);
            const double alphaPair = FMath::Abs(alpha - previousAlpha);
            widthSteep += widthPair > widthBound ? 1 : 0;
            alphaSteep += alphaPair > alphaBound ? 1 : 0;

            // The pair straddling L_p, and the largest pair wholly inside either comparison range
            if (previousArc <= lp && arcM > lp)
            {
                widthStraddle = widthPair;
                alphaStraddle = alphaPair;
            }
            const bool bInBody = previousArc >= 0.5 * lp && arcM <= 0.9 * lp;
            const bool bInTail = previousArc >= 1.1 * lp && arcM <= 2.0 * lp;
            if (bInBody || bInTail)
            {
                widthInteriorMax = FMath::Max(widthInteriorMax, widthPair);
                alphaInteriorMax = FMath::Max(alphaInteriorMax, alphaPair);
            }
            previousWidth = width;
            previousAlpha = alpha;
        }
        const FString label = FString::Printf(TEXT("sweep L_p %.6f, r_n %.1f"), lp, rn);
        TestEqual(label + TEXT(": every W pair within the slope bound"), widthSteep, 0);
        TestEqual(label + TEXT(": every alpha pair within the slope bound"), alphaSteep, 0);
        TestTrue(label + TEXT(": a pair straddles L_p"), widthStraddle >= 0.0 && alphaStraddle >= 0.0);
        TestTrue(FString::Printf(TEXT("%s: W pair at L_p %.3e <= largest interior pair %.3e"), *label, widthStraddle,
            widthInteriorMax), widthStraddle <= widthInteriorMax + 1.0e-9);
        TestTrue(FString::Printf(TEXT("%s: alpha pair at L_p %.3e <= largest interior pair %.3e"), *label,
            alphaStraddle, alphaInteriorMax), alphaStraddle <= alphaInteriorMax + 1.0e-9);

        // The material envelope along the same sweep, b 0.4925 and 1.0, radial fractions 0 and 0.5
        for (const double brightness : { 0.4925, 1.0 })
        {
            const double spacingM = ShockSpacingM(rn, brightness);
            const double knotBound = stepM / (static_cast<double>(SOL::Thruster::SHOCK_KNOT_HALF_LENGTH) * spacingM)
                + 1.0e-9;
            const double streakBound = 1.5 * FMath::Abs(tailDepth - bodyDepth) * stepM / lp + 1.0e-9;
            for (const double radialFraction : { 0.0, 0.5 })
            {
                const double core = FlameCoreWeight(radialFraction);
                int32 outOfRange = core >= 0.0 && core <= 1.0 ? 0 : 1;
                int32 knotSteep = 0;
                int32 streakSteep = 0;
                double previousKnot = FlameKnotIntensity(0.0, radialFraction, lp, rn, brightness);
                double previousStreak = FlameStreakDepth(0.0, lp, bodyDepth, tailDepth);
                for (int32 k = 1; k <= sweepSteps; ++k)
                {
                    const double arcM = static_cast<double>(k) * stepM;
                    const double knot = FlameKnotIntensity(arcM, radialFraction, lp, rn, brightness);
                    const double streak = FlameStreakDepth(arcM, lp, bodyDepth, tailDepth);
                    outOfRange += (knot < 0.0 || knot > 1.0 || streak < 0.0 || streak > 1.0) ? 1 : 0;
                    knotSteep += FMath::Abs(knot - previousKnot) > knotBound ? 1 : 0;
                    streakSteep += FMath::Abs(streak - previousStreak) > streakBound ? 1 : 0;
                    previousKnot = knot;
                    previousStreak = streak;
                }
                const FString envelope = label + FString::Printf(TEXT(", b %.4f, r %.1f"), brightness, radialFraction);
                TestEqual(envelope + TEXT(": core, knot and streak values within [0, 1]"), outOfRange, 0);
                TestEqual(envelope + TEXT(": every knot pair within step / (0.35 S)"), knotSteep, 0);
                TestEqual(envelope + TEXT(": every streak pair within 1.5 |tail - body| step / L_p"), streakSteep, 0);
            }
        }
    }

    // FlameCoreWeight point values: (sqrt(1 - r^2))^PLUME_CORE_POWER, r clamped to [0, 1], NaN 0
    TestEqual(TEXT("core (0) = 1"), FlameCoreWeight(0.0), 1.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("core (0.5) = 0.5625"), FlameCoreWeight(0.5), 0.5625, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("core (1) = 0"), FlameCoreWeight(1.0), 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("core (1.5) = 0 (read as 1)"), FlameCoreWeight(1.5), 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("core (-0.5) = 1 (read as 0)"), FlameCoreWeight(-0.5), 1.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("core (NaN) = 0"), FlameCoreWeight(THRUSTER_NAN), 0.0, THRUSTER_FLAME_TOL);

    // FlameKnotIntensity point values, the player at 3 G (L_p 10.388462, r_n 0.8, b 0.664050; knots at 2.23749,
    // 4.47498, 6.71246)
    constexpr double playerLp = 10.388462;
    constexpr double playerRn = 0.8;
    constexpr double playerB = 0.664050;
    TestEqual(TEXT("knot (2.23749, 0) = 1"), FlameKnotIntensity(2.23749, 0.0, playerLp, playerRn, playerB), 1.0,
        THRUSTER_FLAME_TOL);
    TestEqual(TEXT("knot (4.47498, 0) = 0.82"), FlameKnotIntensity(4.47498, 0.0, playerLp, playerRn, playerB), 0.82,
        THRUSTER_FLAME_TOL);
    TestEqual(TEXT("knot (2.23749, 0.3) = 0.5"), FlameKnotIntensity(2.23749, 0.3, playerLp, playerRn, playerB), 0.5,
        THRUSTER_FLAME_TOL);
    TestEqual(TEXT("knot (3.356235, 0) = 0 (between knots)"),
        FlameKnotIntensity(3.356235, 0.0, playerLp, playerRn, playerB), 0.0, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("knot (2.23749 + 0.5 x 0.35 x 2.23749, 0) = 0.5"),
        FlameKnotIntensity(2.23749 + 0.5 * 0.35 * 2.23749, 0.0, playerLp, playerRn, playerB), 0.5, THRUSTER_FLAME_TOL);
    TestEqual(TEXT("knot (9.0, 0) = 0 (past the knot zone)"), FlameKnotIntensity(9.0, 0.0, playerLp, playerRn,
        playerB), 0.0, THRUSTER_FLAME_TOL);

    // FlameStreakDepth point values: lerp(body, tail, S((s - 0.5 L_p) / L_p)) with L_p 10; L_p 0 or NaN gives the body
    const double streakArcs[] = { 0.0, 5.0, 10.0, 15.0, 30.0 };
    const double streakValues[] = { 0.45, 0.45, 0.625, 0.8, 0.8 };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(streakArcs)); ++index)
    {
        TestEqual(FString::Printf(TEXT("streak (%.0f, 10) = %.3f"), streakArcs[index], streakValues[index]),
            FlameStreakDepth(streakArcs[index], 10.0, bodyDepth, tailDepth), streakValues[index], THRUSTER_FLAME_TOL);
    }
    TestEqual(TEXT("streak with L_p 0 = body"), FlameStreakDepth(5.0, 0.0, bodyDepth, tailDepth), bodyDepth,
        THRUSTER_FLAME_TOL);
    TestEqual(TEXT("streak with L_p NaN = body"), FlameStreakDepth(5.0, THRUSTER_NAN, bodyDepth, tailDepth), bodyDepth,
        THRUSTER_FLAME_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameShutdownTest, "SOLTest.Thruster.FlameShutdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FlameShutdownBrightness(lastBurningBrightness, secondsSinceCutoff) (SDD 51 R10-Q4): clamp01(last b) x clamp(1 -
// seconds / FLAME_SHUTDOWN_S, 0, 1), a linear spool-down; a negative elapsed time reads as 0, NaN in either argument
// and an infinite elapsed time give 0. Every time is a multiple of FLAME_SHUTDOWN_S, a tunable start value
bool FSOLThrusterFlameShutdownTest::RunTest(const FString& /*parameters*/)
{
    const double s = SOL::Thruster::FLAME_SHUTDOWN_S;
    constexpr double exactTol = 1.0e-9;

    // The ramp's key points
    TestEqual(TEXT("(0.8, 0) = 0.8"), SOLThruster::FlameShutdownBrightness(0.8, 0.0), 0.8, exactTol);
    TestEqual(TEXT("(0.8, 0.5 S) = 0.4"), SOLThruster::FlameShutdownBrightness(0.8, 0.5 * s), 0.4, exactTol);
    TestEqual(TEXT("(0.8, 0.25 S) = 0.6"), SOLThruster::FlameShutdownBrightness(0.8, 0.25 * s), 0.6, exactTol);
    TestEqual(TEXT("(0.8, S) = 0"), SOLThruster::FlameShutdownBrightness(0.8, s), 0.0, exactTol);
    TestEqual(TEXT("(0.8, 2 S) = 0"), SOLThruster::FlameShutdownBrightness(0.8, 2.0 * s), 0.0, exactTol);

    // Out-of-range and non-finite inputs
    TestEqual(TEXT("(0.8, -1) = 0.8 (negative elapsed reads as 0)"), SOLThruster::FlameShutdownBrightness(0.8, -1.0),
        0.8, exactTol);
    TestEqual(TEXT("(1.5, 0) = 1 (brightness clamped)"), SOLThruster::FlameShutdownBrightness(1.5, 0.0), 1.0,
        exactTol);
    TestEqual(TEXT("(-0.2, 0) = 0"), SOLThruster::FlameShutdownBrightness(-0.2, 0.0), 0.0, exactTol);
    TestEqual(TEXT("(NaN, 0) = 0"), SOLThruster::FlameShutdownBrightness(THRUSTER_NAN, 0.0), 0.0, exactTol);
    TestEqual(TEXT("(0.8, NaN) = 0"), SOLThruster::FlameShutdownBrightness(0.8, THRUSTER_NAN), 0.0, exactTol);
    TestEqual(TEXT("(0.8, +inf) = 0"), SOLThruster::FlameShutdownBrightness(0.8, THRUSTER_INF), 0.0, exactTol);

    // A linear, monotone ramp: never rising, and no step larger than the slope b / S allows
    constexpr int32 steps = 300;
    const double step = 1.5 * s / static_cast<double>(steps);
    for (const double b : { 0.3, 1.0 })
    {
        double previous = SOLThruster::FlameShutdownBrightness(b, 0.0);
        bool bMonotone = true;
        bool bLinear = true;
        for (int32 index = 1; index <= steps; ++index)
        {
            const double value = SOLThruster::FlameShutdownBrightness(b, step * static_cast<double>(index));
            bMonotone = bMonotone && value <= previous + 1.0e-12;
            bLinear = bLinear && previous - value <= b * step / s + 1.0e-9;
            previous = value;
        }
        TestTrue(FString::Printf(TEXT("sweep b %.1f: never rises"), b), bMonotone);
        TestTrue(FString::Printf(TEXT("sweep b %.1f: no step steeper than b / S (no jump)"), b), bLinear);
    }
    return true;
}

// Helpers for the thin-flame weight and the on-screen tests (names unique across the test files for unity builds)
namespace
{
    constexpr double THRUSTER_SHARP_TOL = 1.0e-9;                   // FlameSharpness exact cases
    constexpr double THRUSTER_SHARP_MIRROR_TOL = 1.0e-4;            // CPU vs the float shader mirror
    constexpr double THRUSTER_SCREEN_WIDTH_PX = 1920.0;
    constexpr double THRUSTER_SCREEN_HEIGHT_PX = 1080.0;
    constexpr double THRUSTER_SCREEN_HALF_FOV_DEG = 45.0;           // 90 degree horizontal FOV
    constexpr double THRUSTER_SCREEN_NEAR_CM = 10.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance (m) at which a width of widthM spans px pixels under focal length focalPx
    double ThrusterSharpDistanceForPx(const double widthM, const double px, const double focalPx)
    {
        return widthM * focalPx / px;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the flame parameter vector the shader mirror reads (only Z = low px and W = high px are used)
    FVector4f ThrusterSharpFlameVector()
    {
        return FVector4f(0.0f, 0.0f, static_cast<float>(SOL::Thruster::FLAME_SHARP_LOW_PX),
            static_cast<float>(SOL::Thruster::FLAME_SHARP_HIGH_PX));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the frustum (cm, no near plane) of a view at the origin looking down +X with a 90 degree horizontal FOV
    // at 1920 x 1080, built the way production builds it (FViewMatrices' axis swap, reversed-Z infinite projection)
    FConvexVolume ThrusterScreenFrustum()
    {
        // UE view-space swap (X forward, Y right, Z up -> x right, y up, z depth), as FViewMatrices::UpdateViewMatrix
        const FMatrix viewPlanes(
            FPlane(0, 0, 1, 0),
            FPlane(1, 0, 0, 0),
            FPlane(0, 1, 0, 0),
            FPlane(0, 0, 0, 1));
        const FMatrix view = FInverseRotationMatrix(FRotator::ZeroRotator) * viewPlanes;
        const FMatrix projection = FReversedZPerspectiveMatrix(FMath::DegreesToRadians(THRUSTER_SCREEN_HALF_FOV_DEG),
            THRUSTER_SCREEN_WIDTH_PX, THRUSTER_SCREEN_HEIGHT_PX, THRUSTER_SCREEN_NEAR_CM);
        FConvexVolume frustum;
        GetViewFrustumBounds(frustum, view * projection, false);
        return frustum;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameSharpnessTest, "SOLTest.Thruster.FlameSharpness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FlameSharpness(trueWidthM, drawnWidthM, distanceM, focalPx) (SDD 51 R10): clamp01(max(true, 0) / drawn) x
// clamp01((drawnPx - FLAME_SHARP_LOW_PX) / (FLAME_SHARP_HIGH_PX - FLAME_SHARP_LOW_PX)), drawnPx = drawn x f / d; 1 when
// drawn, d or f is not positive or NaN; matches the strip pool shader's float mirror
bool FSOLThrusterFlameSharpnessTest::RunTest(const FString& /*parameters*/)
{
    using SOLThruster::FlameSharpness;
    const double lowPx = SOL::Thruster::FLAME_SHARP_LOW_PX;
    const double highPx = SOL::Thruster::FLAME_SHARP_HIGH_PX;
    constexpr double focal = 1000.0;

    // True width = drawn width: the weight is the pixel ramp alone
    TestEqual(TEXT("true = drawn, 10 px -> 1"), FlameSharpness(1.0, 1.0, 100.0, focal), 1.0, THRUSTER_SHARP_TOL);
    TestEqual(TEXT("true = drawn, at HIGH px -> 1"),
        FlameSharpness(1.0, 1.0, ThrusterSharpDistanceForPx(1.0, highPx, focal), focal), 1.0, THRUSTER_SHARP_TOL);
    TestEqual(TEXT("true = drawn, at LOW px -> 0"),
        FlameSharpness(1.0, 1.0, ThrusterSharpDistanceForPx(1.0, lowPx, focal), focal), 0.0, THRUSTER_SHARP_TOL);
    const double midPx = 0.5 * (lowPx + highPx);
    TestEqual(TEXT("true = drawn, halfway up the ramp -> 0.5"),
        FlameSharpness(1.0, 1.0, ThrusterSharpDistanceForPx(1.0, midPx, focal), focal), 0.5, THRUSTER_SHARP_TOL);
    TestEqual(TEXT("true = drawn, below LOW px -> 0"),
        FlameSharpness(1.0, 1.0, ThrusterSharpDistanceForPx(1.0, 0.5 * lowPx, focal), focal), 0.0,
        THRUSTER_SHARP_TOL);

    // A width floored to LOW px: a 1 cm flame drawn 1 m wide at 1 km
    TestEqual(TEXT("floored flame (0.01 m drawn at the LOW px floor) -> 0"),
        FlameSharpness(0.01, lowPx * 1000.0 / focal, 1000.0, focal), 0.0, THRUSTER_SHARP_TOL);

    // The width ratio term at a fully sharp pixel size
    TestEqual(TEXT("true half of drawn, 10 px -> 0.5"), FlameSharpness(0.5, 1.0, 100.0, focal), 0.5,
        THRUSTER_SHARP_TOL);
    TestEqual(TEXT("true > drawn clamps the ratio -> 1"), FlameSharpness(2.0, 1.0, 100.0, focal), 1.0,
        THRUSTER_SHARP_TOL);
    TestEqual(TEXT("true 0 -> 0"), FlameSharpness(0.0, 1.0, 100.0, focal), 0.0, THRUSTER_SHARP_TOL);
    TestEqual(TEXT("true negative -> 0"), FlameSharpness(-0.5, 1.0, 100.0, focal), 0.0, THRUSTER_SHARP_TOL);

    // Guards: no drawn width, distance or focal length gives exactly 1
    TestEqual(TEXT("drawn 0 -> 1"), FlameSharpness(0.5, 0.0, 100.0, focal), 1.0, 0.0);
    TestEqual(TEXT("drawn negative -> 1"), FlameSharpness(0.5, -1.0, 100.0, focal), 1.0, 0.0);
    TestEqual(TEXT("drawn NaN -> 1"), FlameSharpness(0.5, THRUSTER_NAN, 100.0, focal), 1.0, 0.0);
    TestEqual(TEXT("distance 0 -> 1"), FlameSharpness(0.5, 1.0, 0.0, focal), 1.0, 0.0);
    TestEqual(TEXT("distance negative -> 1"), FlameSharpness(0.5, 1.0, -100.0, focal), 1.0, 0.0);
    TestEqual(TEXT("distance NaN -> 1"), FlameSharpness(0.5, 1.0, THRUSTER_NAN, focal), 1.0, 0.0);
    TestEqual(TEXT("focal 0 -> 1"), FlameSharpness(0.5, 1.0, 100.0, 0.0), 1.0, 0.0);
    TestEqual(TEXT("focal negative -> 1"), FlameSharpness(0.5, 1.0, 100.0, -focal), 1.0, 0.0);
    TestEqual(TEXT("focal NaN -> 1"), FlameSharpness(0.5, 1.0, 100.0, THRUSTER_NAN), 1.0, 0.0);

    // Monotone non-decreasing in drawn px (true = drawn = 1 m) over (0, 8] px
    {
        constexpr int32 steps = 160;
        double previous = -1.0;
        bool bMonotone = true;
        for (int32 index = 1; index <= steps; ++index)
        {
            const double px = 8.0 * static_cast<double>(index) / static_cast<double>(steps);
            const double value = FlameSharpness(1.0, 1.0, ThrusterSharpDistanceForPx(1.0, px, focal), focal);
            bMonotone = bMonotone && value >= previous - 1.0e-12;
            previous = value;
        }
        TestTrue(TEXT("sweep 0..8 px: never falls"), bMonotone);
    }

    // Always within [0, 1] over widths (negative and above drawn included), distances and focal lengths
    {
        bool bInRange = true;
        for (const double trueM : { -1.0, 0.0, 0.01, 0.3, 1.0, 5.0 })
        {
            for (const double drawnM : { 0.01, 0.5, 1.0, 3.0 })
            {
                for (const double distanceM : { 1.0, 50.0, 1000.0, 1.0e5 })
                {
                    for (const double focalPx : { 500.0, 1000.0, 2000.0 })
                    {
                        const double value = FlameSharpness(trueM, drawnM, distanceM, focalPx);
                        bInRange = bInRange && value >= 0.0 && value <= 1.0;
                    }
                }
            }
        }
        TestTrue(TEXT("sweep: every weight in [0, 1]"), bInRange);
    }

    // Parity with the strip pool shader's float mirror (cm widths, pixel size in cm = 100 d / f)
    {
        const FVector4f flame = ThrusterSharpFlameVector();
        double worstError = 0.0;
        for (const double trueM : { 0.0, 0.01, 0.1, 0.5, 1.0, 2.0 })
        {
            for (const double drawnScale : { 1.0, 1.5, 3.0, 10.0 })
            {
                const double drawnM = trueM > 0.0 ? trueM * drawnScale : 0.05 * drawnScale;
                for (const double distanceM : { 10.0, 100.0, 1000.0, 10000.0 })
                {
                    for (const double focalPx : { 1000.0, 1500.0, 2000.0 })
                    {
                        const double cpu = FlameSharpness(trueM, drawnM, distanceM, focalPx);
                        const double gpu = SOLThrusterMirror::FlameSharpness(static_cast<float>(100.0 * trueM),
                            static_cast<float>(100.0 * drawnM), static_cast<float>(100.0 * distanceM / focalPx),
                            flame);
                        worstError = FMath::Max(worstError, FMath::Abs(cpu - static_cast<double>(gpu)));
                    }
                }
            }
        }
        TestTrue(FString::Printf(TEXT("CPU vs shader mirror: worst |error| %g <= 1e-4"), worstError),
            worstError <= THRUSTER_SHARP_MIRROR_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterFlameOnScreenTest, "SOLTest.Thruster.FlameOnScreen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ASOLThrusterRibbons::SphereTouchesFrustum(frustum, bHasFrustum, centreM, radiusM) (SDD 51 R10-Q8): true without a
// frustum; otherwise whether the sphere (metres, scaled to the frustum's cm; a negative or NaN radius read as 0)
// touches the frustum. IsOnScreen is true before the first Tick (no frustum yet). The frustum: a camera at the origin
// looking down +X, 90 degree horizontal FOV, 1920 x 1080, built as production builds it (no near plane)
bool FSOLThrusterFlameOnScreenTest::RunTest(const FString& /*parameters*/)
{
    const FConvexVolume frustum = ThrusterScreenFrustum();
    TestTrue(TEXT("the test frustum has planes"), frustum.Planes.Num() >= 4);

    // Sanity of the frustum itself (engine semantics, cm): ahead is in, behind is out
    TestTrue(TEXT("engine: a point 1 km ahead is inside the test frustum"),
        frustum.IntersectSphere(FVector(100000.0, 0.0, 0.0), 0.0f));
    TestFalse(TEXT("engine: a point 1 km behind is outside the test frustum"),
        frustum.IntersectSphere(FVector(-100000.0, 0.0, 0.0), 0.0f));

    // Returns SphereTouchesFrustum against the test frustum with a frustum present
    const auto touches = [&frustum](const FVector3d& centreM, const double radiusM)
    {
        return ASOLThrusterRibbons::SphereTouchesFrustum(frustum, true, centreM, radiusM);
    };
    // On and off the view axis
    TestTrue(TEXT("1 km ahead on axis -> true"), touches(FVector3d(1000.0, 0.0, 0.0), 0.0));
    TestFalse(TEXT("1 km behind on axis -> false"), touches(FVector3d(-1000.0, 0.0, 0.0), 0.0));
    TestTrue(TEXT("1 km behind, radius 2 km (reaches into view) -> true"),
        touches(FVector3d(-1000.0, 0.0, 0.0), 2000.0));
    TestFalse(TEXT("1 km ahead, 5 km right, radius 10 m -> false"), touches(FVector3d(1000.0, 5000.0, 0.0), 10.0));
    TestTrue(TEXT("1 km ahead, 5 km right, radius 5 km -> true"), touches(FVector3d(1000.0, 5000.0, 0.0), 5000.0));

    // The right edge at 1 km ahead: y = 1 km x tan(half FOV)
    const double edgeY = 1000.0 * FMath::Tan(FMath::DegreesToRadians(THRUSTER_SCREEN_HALF_FOV_DEG));
    const FVector3d justInside(1000.0, edgeY - 10.0, 0.0);
    const FVector3d justOutside(1000.0, edgeY + 10.0, 0.0);
    TestTrue(TEXT("10 m inside the right edge at 1 km -> true"), touches(justInside, 0.0));
    TestFalse(TEXT("10 m outside the right edge at 1 km -> false"), touches(justOutside, 0.0));
    TestTrue(TEXT("10 m outside the right edge, radius 20 m -> true"), touches(justOutside, 20.0));

    // A negative or NaN radius reads as 0
    TestTrue(TEXT("inside the edge, radius -50 m (reads as 0) -> true"), touches(justInside, -50.0));
    TestFalse(TEXT("outside the edge, radius -50 m (reads as 0) -> false"), touches(justOutside, -50.0));
    TestTrue(TEXT("inside the edge, radius NaN (reads as 0) -> true"), touches(justInside, THRUSTER_NAN));
    TestFalse(TEXT("outside the edge, radius NaN (reads as 0) -> false"), touches(justOutside, THRUSTER_NAN));

    // Without a frustum every sphere is on screen
    TestTrue(TEXT("no frustum, same planes, 1 km behind -> true"),
        ASOLThrusterRibbons::SphereTouchesFrustum(frustum, false, FVector3d(-1000.0, 0.0, 0.0), 0.0));
    TestTrue(TEXT("no frustum, empty volume -> true"),
        ASOLThrusterRibbons::SphereTouchesFrustum(FConvexVolume(), false, FVector3d(-1000.0, 0.0, 0.0), 0.0));

    // IsOnScreen before any Tick (the never-ticked class default object): true for any input
    const ASOLThrusterRibbons* const untickedRibbons = GetDefault<ASOLThrusterRibbons>();
    TestTrue(TEXT("IsOnScreen before the first Tick, behind -> true"),
        untickedRibbons->IsOnScreen(FVector3d(-1000.0, 0.0, 0.0), 0.0));
    TestTrue(TEXT("IsOnScreen before the first Tick, far to the side -> true"),
        untickedRibbons->IsOnScreen(FVector3d(1000.0, 50000.0, 0.0), 1.0));
    TestTrue(TEXT("IsOnScreen before the first Tick, negative radius -> true"),
        untickedRibbons->IsOnScreen(FVector3d(1000.0, 0.0, 0.0), -5.0));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
