/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLWarpCurve.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the jump warp-effect curves (Docs/SDDs/3-jump-map.md, Appendix I): u = clamp(elapsed / duration, 0, 1)
// (duration <= 0 means u = 1); FOV = base + (peak - base) * sin(PI * u); streak = peak * sin(PI * u), >= 0
namespace
{
    constexpr double WARP_DURATION_S = 3.0;
    constexpr double WARP_BASE_FOV_DEG = 70.0;
    constexpr double WARP_PEAK_FOV_DEG = 100.0;
    constexpr double WARP_PEAK_INTENSITY = 2.0;

    // sin(PI) is ~1.22e-16 in doubles, not 0, so u = 1 results are compared within this absolute tolerance
    constexpr double WARP_END_TOL = 1.0e-12;

    // Tolerance for intermediate points checked against hand-computed sin(PI * u) values
    constexpr double WARP_MID_TOL = 1.0e-9;

    // Hand-computed sin values: sin(PI / 6) = 0.5, sin(PI / 4) = sin(3 PI / 4) = sqrt(2) / 2
    constexpr double WARP_SIN_SIXTH = 0.5;
    constexpr double WARP_SIN_QUARTER = 0.70710678118654752;

    //////////////////////////////////////////////////////////////////////////
    // Calls ComputeFovDeg with the default duration, base and peak FOV
    double WarpFovDefault(const double elapsedS)
    {
        return SOLWarpCurve::ComputeFovDeg(elapsedS, WARP_DURATION_S, WARP_BASE_FOV_DEG, WARP_PEAK_FOV_DEG);
    }

    //////////////////////////////////////////////////////////////////////////
    // Calls ComputeStreakIntensity with the default duration and peak intensity
    double WarpStreakDefault(const double elapsedS)
    {
        return SOLWarpCurve::ComputeStreakIntensity(elapsedS, WARP_DURATION_S, WARP_PEAK_INTENSITY);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveFovEndpointsTest, "SOLTest.WarpCurve.FovEndpointsReturnToBase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FOV is the base value at the start (exactly, sin(0) = 0) and back at the base value at the end (sin(PI) ~ 0)
bool FSOLWarpCurveFovEndpointsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Elapsed 0 is exactly the base FOV"), WarpFovDefault(0.0), WARP_BASE_FOV_DEG, 0.0);
    TestEqual(TEXT("Elapsed == duration is the base FOV"), WarpFovDefault(WARP_DURATION_S), WARP_BASE_FOV_DEG,
        WARP_END_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveFovPeakTest, "SOLTest.WarpCurve.FovPeaksAtMidpoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FOV is exactly the peak value halfway through (sin(PI / 2) = 1) and below it everywhere else
bool FSOLWarpCurveFovPeakTest::RunTest(const FString& /*parameters*/)
{
    const double peak = WarpFovDefault(0.5 * WARP_DURATION_S);
    TestEqual(TEXT("Midpoint is exactly the peak FOV"), peak, WARP_PEAK_FOV_DEG, 0.0);
    TestTrue(TEXT("Quarter point is below the peak"), WarpFovDefault(0.25 * WARP_DURATION_S) < peak);
    TestTrue(TEXT("Three-quarter point is below the peak"), WarpFovDefault(0.75 * WARP_DURATION_S) < peak);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveFovIntermediateTest, "SOLTest.WarpCurve.FovIntermediateMatchesSin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Intermediate FOV values match base + (peak - base) * sin(PI * u) computed by hand, and the curve is symmetric
bool FSOLWarpCurveFovIntermediateTest::RunTest(const FString& /*parameters*/)
{
    // u = 1/6: 70 + 30 * 0.5 = 85
    TestEqual(TEXT("u = 1/6 is 85 deg"), WarpFovDefault(WARP_DURATION_S / 6.0), 85.0, WARP_MID_TOL);

    // u = 0.25 and u = 0.75: 70 + 30 * sqrt(2) / 2 = 91.21320343559643
    const double quarterDeg = WARP_BASE_FOV_DEG + (WARP_PEAK_FOV_DEG - WARP_BASE_FOV_DEG) * WARP_SIN_QUARTER;
    TestEqual(TEXT("u = 0.25 is ~91.2132 deg"), WarpFovDefault(0.25 * WARP_DURATION_S), quarterDeg, WARP_MID_TOL);
    TestEqual(TEXT("u = 0.75 is ~91.2132 deg"), WarpFovDefault(0.75 * WARP_DURATION_S), quarterDeg, WARP_MID_TOL);
    TestEqual(TEXT("Hand value check"), quarterDeg, 91.21320343559643, WARP_MID_TOL);

    // u = 5/6: symmetric with u = 1/6
    TestEqual(TEXT("u = 5/6 is 85 deg"), WarpFovDefault(5.0 * WARP_DURATION_S / 6.0), 85.0, WARP_MID_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveFovNonPositiveDurationTest,
    "SOLTest.WarpCurve.FovNonPositiveDurationIsBase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or negative duration is treated as already complete (u = 1), so the FOV is the base value
bool FSOLWarpCurveFovNonPositiveDurationTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Duration 0, elapsed 0 is the base FOV"),
        SOLWarpCurve::ComputeFovDeg(0.0, 0.0, WARP_BASE_FOV_DEG, WARP_PEAK_FOV_DEG), WARP_BASE_FOV_DEG, WARP_END_TOL);
    TestEqual(TEXT("Duration 0, elapsed 1.5 is the base FOV"),
        SOLWarpCurve::ComputeFovDeg(1.5, 0.0, WARP_BASE_FOV_DEG, WARP_PEAK_FOV_DEG), WARP_BASE_FOV_DEG, WARP_END_TOL);
    TestEqual(TEXT("Duration -3, elapsed 1.5 is the base FOV"),
        SOLWarpCurve::ComputeFovDeg(1.5, -3.0, WARP_BASE_FOV_DEG, WARP_PEAK_FOV_DEG), WARP_BASE_FOV_DEG, WARP_END_TOL);
    TestEqual(TEXT("Duration -3, elapsed -1.5 is the base FOV"),
        SOLWarpCurve::ComputeFovDeg(-1.5, -3.0, WARP_BASE_FOV_DEG, WARP_PEAK_FOV_DEG), WARP_BASE_FOV_DEG,
        WARP_END_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveFovClampTest, "SOLTest.WarpCurve.FovElapsedOutsideRangeClamps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Elapsed beyond the duration clamps to u = 1 and negative elapsed clamps to u = 0: both give the base FOV
bool FSOLWarpCurveFovClampTest::RunTest(const FString& /*parameters*/)
{
    const double endDeg = WarpFovDefault(WARP_DURATION_S);
    TestEqual(TEXT("Elapsed 1.5x duration is the base FOV"), WarpFovDefault(1.5 * WARP_DURATION_S), WARP_BASE_FOV_DEG,
        WARP_END_TOL);
    TestEqual(TEXT("Elapsed 10x duration is the base FOV"), WarpFovDefault(10.0 * WARP_DURATION_S), WARP_BASE_FOV_DEG,
        WARP_END_TOL);
    TestEqual(TEXT("Elapsed beyond duration equals the u = 1 value"), WarpFovDefault(4.0 * WARP_DURATION_S), endDeg,
        0.0);
    TestEqual(TEXT("Elapsed -1 is exactly the base FOV"), WarpFovDefault(-1.0), WARP_BASE_FOV_DEG, 0.0);
    TestEqual(TEXT("Elapsed -1e6 is exactly the base FOV"), WarpFovDefault(-1.0e6), WARP_BASE_FOV_DEG, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveFovDipTest, "SOLTest.WarpCurve.FovPeakBelowBaseDips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A peak below the base uses the same formula: the FOV dips to the peak mid-animation and returns to the base
bool FSOLWarpCurveFovDipTest::RunTest(const FString& /*parameters*/)
{
    constexpr double baseDeg = 90.0;
    constexpr double peakDeg = 60.0;
    TestEqual(TEXT("Dip: elapsed 0 is exactly the base FOV"),
        SOLWarpCurve::ComputeFovDeg(0.0, WARP_DURATION_S, baseDeg, peakDeg), baseDeg, 0.0);
    TestEqual(TEXT("Dip: midpoint is exactly the (lower) peak FOV"),
        SOLWarpCurve::ComputeFovDeg(0.5 * WARP_DURATION_S, WARP_DURATION_S, baseDeg, peakDeg), peakDeg, 0.0);
    TestEqual(TEXT("Dip: end is the base FOV"),
        SOLWarpCurve::ComputeFovDeg(WARP_DURATION_S, WARP_DURATION_S, baseDeg, peakDeg), baseDeg, WARP_END_TOL);

    // u = 0.25: 90 - 30 * sqrt(2) / 2 = 68.78679656440357; u = 1/6: 90 - 30 * 0.5 = 75
    TestEqual(TEXT("Dip: u = 0.25 is ~68.7868 deg"),
        SOLWarpCurve::ComputeFovDeg(0.25 * WARP_DURATION_S, WARP_DURATION_S, baseDeg, peakDeg), 68.78679656440357,
        WARP_MID_TOL);
    TestEqual(TEXT("Dip: u = 1/6 is 75 deg"),
        SOLWarpCurve::ComputeFovDeg(WARP_DURATION_S / 6.0, WARP_DURATION_S, baseDeg, peakDeg),
        baseDeg + (peakDeg - baseDeg) * WARP_SIN_SIXTH, WARP_MID_TOL);

    // Every interior sample lies within [peak, base]
    for (int32 step = 1; step < 20; ++step)
    {
        const double elapsedS = WARP_DURATION_S * step / 20.0;
        const double fovDeg = SOLWarpCurve::ComputeFovDeg(elapsedS, WARP_DURATION_S, baseDeg, peakDeg);
        TestTrue(*FString::Printf(TEXT("Dip: %g s is below the base"), elapsedS), fovDeg < baseDeg);
        TestTrue(*FString::Printf(TEXT("Dip: %g s is at or above the peak"), elapsedS), fovDeg >= peakDeg);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveStreakEndpointsTest, "SOLTest.WarpCurve.StreakEndpointsAreZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Streak intensity is 0 at the start (exactly) and at the end (sin(PI) ~ 0)
bool FSOLWarpCurveStreakEndpointsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Elapsed 0 is exactly 0"), WarpStreakDefault(0.0), 0.0, 0.0);
    TestEqual(TEXT("Elapsed == duration is 0"), WarpStreakDefault(WARP_DURATION_S), 0.0, WARP_END_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveStreakPeakTest, "SOLTest.WarpCurve.StreakPeaksAtMidpoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Streak intensity is exactly the peak halfway through (sin(PI / 2) = 1)
bool FSOLWarpCurveStreakPeakTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Midpoint is exactly the peak intensity"), WarpStreakDefault(0.5 * WARP_DURATION_S),
        WARP_PEAK_INTENSITY, 0.0);
    TestEqual(TEXT("Midpoint with peak 1 is exactly 1"),
        SOLWarpCurve::ComputeStreakIntensity(0.5 * WARP_DURATION_S, WARP_DURATION_S, 1.0), 1.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveStreakIntermediateTest, "SOLTest.WarpCurve.StreakIntermediateMatchesSin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Intermediate streak intensities match peak * sin(PI * u) computed by hand, and the curve is symmetric
bool FSOLWarpCurveStreakIntermediateTest::RunTest(const FString& /*parameters*/)
{
    // u = 1/6 and 5/6: 2 * 0.5 = 1
    TestEqual(TEXT("u = 1/6 is 1"), WarpStreakDefault(WARP_DURATION_S / 6.0), WARP_PEAK_INTENSITY * WARP_SIN_SIXTH,
        WARP_MID_TOL);
    TestEqual(TEXT("u = 5/6 is 1"), WarpStreakDefault(5.0 * WARP_DURATION_S / 6.0), 1.0, WARP_MID_TOL);

    // u = 0.25 and 0.75: 2 * sqrt(2) / 2 = 1.4142135623730951
    TestEqual(TEXT("u = 0.25 is ~1.41421"), WarpStreakDefault(0.25 * WARP_DURATION_S), 1.4142135623730951,
        WARP_MID_TOL);
    TestEqual(TEXT("u = 0.75 is ~1.41421"), WarpStreakDefault(0.75 * WARP_DURATION_S),
        WARP_PEAK_INTENSITY * WARP_SIN_QUARTER, WARP_MID_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveStreakNonPositiveDurationTest,
    "SOLTest.WarpCurve.StreakNonPositiveDurationIsZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or negative duration is treated as already complete (u = 1), so the streak intensity is 0
bool FSOLWarpCurveStreakNonPositiveDurationTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Duration 0, elapsed 0 is 0"),
        SOLWarpCurve::ComputeStreakIntensity(0.0, 0.0, WARP_PEAK_INTENSITY), 0.0, WARP_END_TOL);
    TestEqual(TEXT("Duration 0, elapsed 1.5 is 0"),
        SOLWarpCurve::ComputeStreakIntensity(1.5, 0.0, WARP_PEAK_INTENSITY), 0.0, WARP_END_TOL);
    TestEqual(TEXT("Duration -3, elapsed 1.5 is 0"),
        SOLWarpCurve::ComputeStreakIntensity(1.5, -3.0, WARP_PEAK_INTENSITY), 0.0, WARP_END_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarpCurveStreakRangeTest, "SOLTest.WarpCurve.StreakNonNegativeAndBounded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Across [0, duration] and beyond it the streak is never negative, never above the peak, and positive inside
bool FSOLWarpCurveStreakRangeTest::RunTest(const FString& /*parameters*/)
{
    for (int32 step = 0; step <= 30; ++step)
    {
        const double elapsedS = WARP_DURATION_S * step / 30.0;
        const double intensity = WarpStreakDefault(elapsedS);
        TestTrue(*FString::Printf(TEXT("%g s is not negative"), elapsedS), intensity >= 0.0);
        TestTrue(*FString::Printf(TEXT("%g s is not above the peak"), elapsedS), intensity <= WARP_PEAK_INTENSITY);
        if (step > 0 && step < 30)
        {
            TestTrue(*FString::Printf(TEXT("%g s is positive inside the animation"), elapsedS), intensity > 0.0);
        }
    }

    // Outside the range the clamp keeps it at the endpoint values
    TestEqual(TEXT("Negative elapsed is exactly 0"), WarpStreakDefault(-2.0), 0.0, 0.0);
    TestTrue(TEXT("Elapsed beyond duration is not negative"), WarpStreakDefault(5.0 * WARP_DURATION_S) >= 0.0);
    TestEqual(TEXT("Elapsed beyond duration is 0"), WarpStreakDefault(5.0 * WARP_DURATION_S), 0.0, WARP_END_TOL);
    return true;
}

// Ambiguity (not asserted): the contract does not say whether a negative PeakIntensity yields 0 (via the ">= 0"
// clamp) or a negative curve. No test pins that behavior until the SDD resolves it

#endif // WITH_DEV_AUTOMATION_TESTS
