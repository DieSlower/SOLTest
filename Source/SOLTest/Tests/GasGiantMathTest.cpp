/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "GasGiants/SOLGasGiantMath.h"

#include "SOLConstants.h"
#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Written against the "14a contract" in Docs/Plans/14-gas-giant-fidelity-plan.md, not against the implementation.
// Helpers here carry names unique across the test files so unity builds do not collide.
namespace
{
    // Jupiter-like numbers typed independently of production code
    constexpr double GASGIANT_JUPITER_RADIUS_M = 7.1492e7;
    constexpr double GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S = 1.4e-6;   // ~100 m/s equatorial jet relative to System III

    // A sim time in 2027 (seconds since J2000)
    constexpr double GASGIANT_SIM_2027_S = 8.6e8;

    // Tolerances
    constexpr double GASGIANT_TIGHT_TOL = 1.0e-12;
    constexpr double GASGIANT_PHASE_TOL = 1.0e-9;
    constexpr double GASGIANT_ANGLE_TOL_RAD = 1.0e-9;

    // Near-layer fade band (planet radii) used by the blend cases
    constexpr double GASGIANT_FADE_IN_RADII = 2.0;
    constexpr double GASGIANT_FADE_OUT_RADII = 4.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns the signed circular difference a - b of two phases in turns, reduced to [-0.5, 0.5]
    double GasGiantTurnDiff(const double a, const double b)
    {
        const double d = a - b;
        return d - std::round(d);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the signed circular difference a - b of two angles in radians, reduced to [-PI, PI]
    double GasGiantAngleDiff(const double a, const double b)
    {
        return SOLTestHelpers::AngleDiffRad(a - b);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if a phase lies in [0, 1) and is finite
    bool GasGiantPhaseInRange(const double phase)
    {
        return std::isfinite(phase) && phase >= 0.0 && phase < 1.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the smoothstep reference 3t^2 - 2t^3, written independently of production code
    double GasGiantSmoothstep(const double t)
    {
        return t * t * (3.0 - 2.0 * t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the reference drift rate eq + (polar - eq) * sin^2(lat), written independently of production code
    double GasGiantRefDriftRate(const double latRad, const double eq, const double polar)
    {
        const double s = std::sin(latRad);
        return eq + (polar - eq) * s * s;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the great-circle angle (rad) between two points on the unit sphere (haversine form)
    double GasGiantGreatCircle(const double lat1, const double lon1, const double lat2, const double lon2)
    {
        const double sLat = std::sin(0.5 * (lat2 - lat1));
        const double sLon = std::sin(0.5 * (lon2 - lon1));
        const double h = sLat * sLat + std::cos(lat1) * std::cos(lat2) * sLon * sLon;
        return 2.0 * std::asin(std::sqrt(FMath::Clamp(h, 0.0, 1.0)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if two storm lists are bit-identical
    bool GasGiantStormsIdentical(const TArray<SOLGasGiant::FStorm>& a, const TArray<SOLGasGiant::FStorm>& b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (int32 i = 0; i < a.Num(); ++i)
        {
            if (a[i].LatitudeRad != b[i].LatitudeRad || a[i].LongitudeRad != b[i].LongitudeRad ||
                a[i].RadiusRad != b[i].RadiusRad)
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a storm list pre-filled with junk, to check GenerateStorms resets rather than appends
    TArray<SOLGasGiant::FStorm> GasGiantJunkStorms()
    {
        TArray<SOLGasGiant::FStorm> junk;
        for (int32 i = 0; i < 5; ++i)
        {
            SOLGasGiant::FStorm storm;
            storm.LatitudeRad = 99.0;
            storm.LongitudeRad = -99.0;
            storm.RadiusRad = 42.0;
            junk.Add(storm);
        }
        return junk;
    }
}

//====================================================================================================================
// Differential rotation rate
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantDriftRateExamplesTest, "SOLTest.GasGiantMath.DriftRateExamples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DriftRateRadPerS is the equator rate at 0, the polar rate at the poles and follows the sin^2 law between
bool FSOLGasGiantDriftRateExamplesTest::RunTest(const FString& /*parameters*/)
{
    const double eq = 3.0e-6;
    const double polar = -1.0e-6;
    TestNearlyEqual(TEXT("Equator -> equator rate"), SOLGasGiant::DriftRateRadPerS(0.0, eq, polar), eq,
        GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("North pole -> polar rate"), SOLGasGiant::DriftRateRadPerS(0.5 * UE_DOUBLE_PI, eq, polar),
        polar, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("South pole -> polar rate"), SOLGasGiant::DriftRateRadPerS(-0.5 * UE_DOUBLE_PI, eq, polar),
        polar, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("45 deg -> midpoint (sin^2 = 0.5)"), SOLGasGiant::DriftRateRadPerS(0.25 * UE_DOUBLE_PI, eq,
        polar), 0.5 * (eq + polar), GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("30 deg -> quarter of the way (sin^2 = 0.25)"), SOLGasGiant::DriftRateRadPerS(
        UE_DOUBLE_PI / 6.0, eq, polar), eq + 0.25 * (polar - eq), GASGIANT_TIGHT_TOL);

    // Arbitrary latitudes match the independent reference
    const double lats[] = { 0.1, 0.7, 1.2, -0.4, -1.5 };
    for (const double lat : lats)
    {
        TestNearlyEqual(FString::Printf(TEXT("Lat %g matches eq + (p - eq) sin^2"), lat),
            SOLGasGiant::DriftRateRadPerS(lat, eq, polar), GasGiantRefDriftRate(lat, eq, polar), GASGIANT_TIGHT_TOL);
    }

    // Equal equator and polar rates give a rigid (latitude-independent) drift
    TestNearlyEqual(TEXT("eq == polar is rigid"), SOLGasGiant::DriftRateRadPerS(0.9, 2.0e-6, 2.0e-6), 2.0e-6,
        GASGIANT_TIGHT_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantDriftRateSymmetryTest, "SOLTest.GasGiantMath.DriftRateSymmetryAndClamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rate is even in latitude, and latitudes beyond the poles clamp to the polar rate
bool FSOLGasGiantDriftRateSymmetryTest::RunTest(const FString& /*parameters*/)
{
    const double eq = 2.0e-6;
    const double polar = 5.0e-7;
    const double lats[] = { 0.05, 0.3, 0.8, 1.3, 1.57 };
    for (const double lat : lats)
    {
        TestNearlyEqual(FString::Printf(TEXT("rate(%g) == rate(-%g)"), lat, lat),
            SOLGasGiant::DriftRateRadPerS(lat, eq, polar), SOLGasGiant::DriftRateRadPerS(-lat, eq, polar),
            GASGIANT_TIGHT_TOL);
    }

    TestNearlyEqual(TEXT("Lat 2.0 clamps to the north pole"), SOLGasGiant::DriftRateRadPerS(2.0, eq, polar), polar,
        GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("Lat -10 clamps to the south pole"), SOLGasGiant::DriftRateRadPerS(-10.0, eq, polar), polar,
        GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("Lat PI (not wrapped) clamps to the pole"), SOLGasGiant::DriftRateRadPerS(UE_DOUBLE_PI, eq,
        polar), polar, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("Lat +inf clamps to the pole"), SOLGasGiant::DriftRateRadPerS(
        std::numeric_limits<double>::infinity(), eq, polar), polar, GASGIANT_TIGHT_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantDriftRateMonotonicTest, "SOLTest.GasGiantMath.DriftRateMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sweeping from the equator to the pole moves the rate monotonically from the equator value to the polar value
bool FSOLGasGiantDriftRateMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const double eq = 4.0e-6;
    const double polar = -2.0e-6;
    constexpr int32 steps = 180;
    double previous = std::numeric_limits<double>::infinity();
    bool bOk = true;
    for (int32 i = 0; i <= steps; ++i)
    {
        const double lat = 0.5 * UE_DOUBLE_PI * static_cast<double>(i) / steps;
        const double rate = SOLGasGiant::DriftRateRadPerS(lat, eq, polar);
        if (!std::isfinite(rate) || rate > previous + GASGIANT_TIGHT_TOL || rate > eq + GASGIANT_TIGHT_TOL ||
            rate < polar - GASGIANT_TIGHT_TOL)
        {
            AddError(FString::Printf(TEXT("Rate %.17g at lat %g breaks monotonic [polar, eq] (previous %.17g)"), rate,
                lat, previous));
            bOk = false;
            break;
        }
        previous = rate;
    }
    TestTrue(TEXT("Equator-to-pole sweep is finite, bounded and non-increasing"), bOk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantDriftRateInvalidTest, "SOLTest.GasGiantMath.DriftRateInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A NaN latitude is the equator; a non-finite drift input is treated as 0; the result is always finite
bool FSOLGasGiantDriftRateInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double eq = 3.0e-6;
    const double polar = 1.0e-6;

    TestNearlyEqual(TEXT("NaN latitude -> equator rate"), SOLGasGiant::DriftRateRadPerS(nan, eq, polar), eq,
        GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("NaN equator drift is 0 at the equator"), SOLGasGiant::DriftRateRadPerS(0.0, nan, polar), 0.0,
        GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("NaN equator drift leaves the polar rate at the pole"), SOLGasGiant::DriftRateRadPerS(
        0.5 * UE_DOUBLE_PI, nan, polar), polar, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("Inf polar drift is 0 at the pole"), SOLGasGiant::DriftRateRadPerS(0.5 * UE_DOUBLE_PI, eq,
        inf), 0.0, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("-Inf polar drift leaves the equator rate at the equator"), SOLGasGiant::DriftRateRadPerS(0.0,
        eq, -inf), eq, GASGIANT_TIGHT_TOL);

    const double lats[] = { nan, inf, -inf, 0.0, 0.6, -1.2 };
    for (const double lat : lats)
    {
        TestTrue(FString::Printf(TEXT("Result finite for lat %g with non-finite drifts"), lat),
            std::isfinite(SOLGasGiant::DriftRateRadPerS(lat, nan, inf)));
    }
    return true;
}

//====================================================================================================================
// Phase
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantPhaseExamplesTest, "SOLTest.GasGiantMath.PhaseTurnsExamples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PhaseTurns is frac(t * rate * factor / 2PI) for simple values, wrapping negatives upward
bool FSOLGasGiantPhaseExamplesTest::RunTest(const FString& /*parameters*/)
{
    // A quarter turn per second
    const double quarterRate = 0.5 * UE_DOUBLE_PI;
    TestNearlyEqual(TEXT("t = 0 -> 0"), SOLGasGiant::PhaseTurns(0.0, quarterRate, 1.0), 0.0, GASGIANT_PHASE_TOL);
    TestNearlyEqual(TEXT("t = 1 -> 0.25"), SOLGasGiant::PhaseTurns(1.0, quarterRate, 1.0), 0.25, GASGIANT_PHASE_TOL);
    TestNearlyEqual(TEXT("t = 3 -> 0.75"), SOLGasGiant::PhaseTurns(3.0, quarterRate, 1.0), 0.75, GASGIANT_PHASE_TOL);
    TestNearlyEqual(TEXT("t = 5 -> 0.25 (1.25 turns)"), SOLGasGiant::PhaseTurns(5.0, quarterRate, 1.0), 0.25,
        GASGIANT_PHASE_TOL);
    TestNearlyEqual(TEXT("t = -1 -> 0.75 (negative wraps upward)"), SOLGasGiant::PhaseTurns(-1.0, quarterRate, 1.0),
        0.75, GASGIANT_PHASE_TOL);
    TestNearlyEqual(TEXT("t = -5 -> 0.75"), SOLGasGiant::PhaseTurns(-5.0, quarterRate, 1.0), 0.75, GASGIANT_PHASE_TOL);
    TestNearlyEqual(TEXT("Factor 2, t = 1 -> 0.5"), SOLGasGiant::PhaseTurns(1.0, quarterRate, 2.0), 0.5,
        GASGIANT_PHASE_TOL);
    TestNearlyEqual(TEXT("Negative rate, t = 1 -> 0.75"), SOLGasGiant::PhaseTurns(1.0, -quarterRate, 1.0), 0.75,
        GASGIANT_PHASE_TOL);
    TestNearlyEqual(TEXT("Factor 0.5, t = 1 -> 0.125"), SOLGasGiant::PhaseTurns(1.0, quarterRate, 0.5), 0.125,
        GASGIANT_PHASE_TOL);

    // Rate 0 never moves
    TestEqual(TEXT("Rate 0 -> 0"), SOLGasGiant::PhaseTurns(GASGIANT_SIM_2027_S, 0.0, 200.0), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantPhaseRangeTest, "SOLTest.GasGiantMath.PhaseTurnsRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PhaseTurns always lands in [0, 1), never exactly 1, including whole-turn and tiny-negative products
bool FSOLGasGiantPhaseRangeTest::RunTest(const FString& /*parameters*/)
{
    const double times[] = { -1.0e10, -GASGIANT_SIM_2027_S, -1.0, -1.0e-300, 0.0, 1.0e-300, 1.0, 12345.678,
        GASGIANT_SIM_2027_S, 1.0e10 };
    const double rates[] = { -1.0e-3, -GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S, 1.0e-12,
        GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S, 2.0 * UE_DOUBLE_PI, 1.0e-3 };
    const double factors[] = { 1.0, 200.0, 1000.0 };
    for (const double t : times)
    {
        for (const double rate : rates)
        {
            for (const double factor : factors)
            {
                const double phase = SOLGasGiant::PhaseTurns(t, rate, factor);
                if (!GasGiantPhaseInRange(phase))
                {
                    AddError(FString::Printf(TEXT("Phase(t=%g, rate=%g, f=%g) = %.17g is outside [0, 1)"), t, rate,
                        factor, phase));
                }
            }
        }
    }

    // Exactly whole turns (2PI rad/s for an integer number of seconds) must give ~0 on the circle, never 1
    const double whole = SOLGasGiant::PhaseTurns(7.0, 2.0 * UE_DOUBLE_PI, 1.0);
    TestTrue(TEXT("Whole turns is in [0, 1)"), GasGiantPhaseInRange(whole));
    TestTrue(TEXT("Whole turns is ~0 on the circle"), std::fabs(GasGiantTurnDiff(whole, 0.0)) < GASGIANT_PHASE_TOL);

    // A tiny negative product wraps to just below 1 or to 0, never to 1 itself
    const double tinyNeg = SOLGasGiant::PhaseTurns(-1.0e-12, 1.0, 1.0);
    TestTrue(TEXT("Tiny negative product is in [0, 1)"), GasGiantPhaseInRange(tinyNeg));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantPhasePrecisionTest, "SOLTest.GasGiantMath.PhaseTurnsLargeTimePrecision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At realistic sim times (~1e9 s since J2000) the phase stays precise: one-second steps advance by exactly
// rate * factor / 2PI, and huge times match the exact double modulo to 1e-6 turns
bool FSOLGasGiantPhasePrecisionTest::RunTest(const FString& /*parameters*/)
{
    const double rate = GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    const double factor = 200.0;
    const double expectedStep = rate * factor / (2.0 * UE_DOUBLE_PI);

    const double startTimes[] = { GASGIANT_SIM_2027_S, -GASGIANT_SIM_2027_S, 3.0e9 };
    for (const double t0 : startTimes)
    {
        const double a = SOLGasGiant::PhaseTurns(t0, rate, factor);
        const double b = SOLGasGiant::PhaseTurns(t0 + 1.0, rate, factor);
        TestNearlyEqual(FString::Printf(TEXT("One-second step at t=%g advances rate*f/2PI"), t0),
            GasGiantTurnDiff(b, a), expectedStep, 1.0e-9);

        // A 1/60 s frame step at 1x still registers a forward, correctly sized movement
        const double c = SOLGasGiant::PhaseTurns(t0 + 1.0 / 60.0, rate, factor);
        TestNearlyEqual(FString::Printf(TEXT("Frame step at t=%g advances rate*f/(60*2PI)"), t0),
            GasGiantTurnDiff(c, a), expectedStep / 60.0, 1.0e-9);
    }

    // Against the exact modulo at huge times with |rate * factor| <= 0.1 rad/s
    const double hugeTimes[] = { 1.0e10, -1.0e10, 9.87654321e9, GASGIANT_SIM_2027_S };
    const double products[] = { 0.1, -0.1, 0.0123 };
    for (const double t : hugeTimes)
    {
        for (const double product : products)
        {
            double expected = std::fmod(t * product / (2.0 * UE_DOUBLE_PI), 1.0);
            if (expected < 0.0)
            {
                expected += 1.0;
            }
            const double actual = SOLGasGiant::PhaseTurns(t, product, 1.0);
            TestTrue(FString::Printf(TEXT("t=%g rate*f=%g: %.12f vs %.12f within 1e-6 turns"), t, product, actual,
                expected), std::fabs(GasGiantTurnDiff(actual, expected)) < 1.0e-6);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantPhaseDeterministicTest,
    "SOLTest.GasGiantMath.PhaseTurnsDeterministicAndPauseSafe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The phase is a pure function of sim time: the same time always gives the bit-identical phase, regardless of calls
// in between (no hidden accumulator), so a paused clock freezes the bands and seeking back restores them
bool FSOLGasGiantPhaseDeterministicTest::RunTest(const FString& /*parameters*/)
{
    const double rate = 2.5e-6;
    const double factor = 200.0;
    const double first = SOLGasGiant::PhaseTurns(GASGIANT_SIM_2027_S, rate, factor);

    // Interleave other times, including a large warp jump forward and a jump backward
    (void)SOLGasGiant::PhaseTurns(GASGIANT_SIM_2027_S + 2592000.0, rate, factor);
    (void)SOLGasGiant::PhaseTurns(-1.0e9, rate, factor);
    (void)SOLGasGiant::BandPhaseTurns(GASGIANT_SIM_2027_S + 5.0, 0.3, rate, 0.0, factor);

    const double second = SOLGasGiant::PhaseTurns(GASGIANT_SIM_2027_S, rate, factor);
    TestTrue(TEXT("Same sim time -> bit-identical phase"), first == second);

    // A paused clock (repeated identical time) yields identical phases every frame
    bool bFrozen = true;
    for (int32 frame = 0; frame < 10; ++frame)
    {
        bFrozen &= SOLGasGiant::BandPhaseTurns(GASGIANT_SIM_2027_S, 0.4, rate, -1.0e-6, factor) ==
            SOLGasGiant::BandPhaseTurns(GASGIANT_SIM_2027_S, 0.4, rate, -1.0e-6, factor);
    }
    TestTrue(TEXT("Paused clock keeps the band phase frozen"), bFrozen);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantPhaseWarpTest, "SOLTest.GasGiantMath.PhaseTurnsFollowsWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Under time warp the sim clock advances faster, so the phase advance over one real second scales with the warp;
// time, rate and factor are interchangeable multipliers
bool FSOLGasGiantPhaseWarpTest::RunTest(const FString& /*parameters*/)
{
    const double rate = 1.0e-6;
    const double factor = 200.0;
    const double t0 = GASGIANT_SIM_2027_S;
    const double base = SOLGasGiant::PhaseTurns(t0, rate, factor);

    // One real second at warp 1 and at warp 10 (sim advances 1 s vs 10 s)
    const double at1x = GasGiantTurnDiff(SOLGasGiant::PhaseTurns(t0 + 1.0, rate, factor), base);
    const double at10x = GasGiantTurnDiff(SOLGasGiant::PhaseTurns(t0 + 10.0, rate, factor), base);
    TestNearlyEqual(TEXT("Warp 10 moves 10x as far per real second"), at10x, 10.0 * at1x, 1.0e-10);

    // Interchangeability: (t, 2*rate, f) == (t, rate, 2*f) == (2*t, rate, f) on the circle
    const double times[] = { 1234.5, 1.0e6, 4.2e8 };
    for (const double t : times)
    {
        const double a = SOLGasGiant::PhaseTurns(t, 2.0 * rate, factor);
        const double b = SOLGasGiant::PhaseTurns(t, rate, 2.0 * factor);
        const double c = SOLGasGiant::PhaseTurns(2.0 * t, rate, factor);
        TestTrue(FString::Printf(TEXT("t=%g: 2*rate == 2*factor"), t), std::fabs(GasGiantTurnDiff(a, b)) < 1.0e-9);
        TestTrue(FString::Printf(TEXT("t=%g: 2*factor == 2*t"), t), std::fabs(GasGiantTurnDiff(b, c)) < 1.0e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantPhaseMonotonicTest, "SOLTest.GasGiantMath.PhaseTurnsMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stepping time forward in sub-half-turn steps always moves a positive-rate phase forward (and a negative-rate phase
// backward) on the circle, across the 1 -> 0 wrap
bool FSOLGasGiantPhaseMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const double rate = 0.3;     // rad/s: ~0.0477 turns per second, so the sweep wraps several times
    const double dt = 0.5;
    const double t0 = GASGIANT_SIM_2027_S;
    double previousPos = SOLGasGiant::PhaseTurns(t0, rate, 1.0);
    double previousNeg = SOLGasGiant::PhaseTurns(t0, -rate, 1.0);
    int32 wraps = 0;
    bool bOk = true;
    for (int32 i = 1; i <= 200; ++i)
    {
        const double t = t0 + dt * i;
        const double pos = SOLGasGiant::PhaseTurns(t, rate, 1.0);
        const double neg = SOLGasGiant::PhaseTurns(t, -rate, 1.0);
        if (pos < previousPos)
        {
            ++wraps;
        }
        if (GasGiantTurnDiff(pos, previousPos) <= 0.0 || GasGiantTurnDiff(neg, previousNeg) >= 0.0)
        {
            AddError(FString::Printf(TEXT("Step %d: phase did not move in the rate's direction"), i));
            bOk = false;
            break;
        }
        previousPos = pos;
        previousNeg = neg;
    }
    TestTrue(TEXT("Every step moves in the rate's direction"), bOk);
    TestTrue(TEXT("The sweep crossed the wrap at least once"), wraps >= 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantPhaseInvalidTest, "SOLTest.GasGiantMath.PhaseTurnsInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or negative speed factor, or any non-finite input, yields phase 0
bool FSOLGasGiantPhaseInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double rate = 1.0e-4;
    const double t = GASGIANT_SIM_2027_S;

    TestEqual(TEXT("Factor 0 -> 0"), SOLGasGiant::PhaseTurns(t, rate, 0.0), 0.0);
    TestEqual(TEXT("Factor -1 -> 0"), SOLGasGiant::PhaseTurns(t, rate, -1.0), 0.0);
    TestEqual(TEXT("Factor -200 -> 0"), SOLGasGiant::PhaseTurns(t, rate, -200.0), 0.0);
    TestEqual(TEXT("NaN time -> 0"), SOLGasGiant::PhaseTurns(nan, rate, 200.0), 0.0);
    TestEqual(TEXT("+inf time -> 0"), SOLGasGiant::PhaseTurns(inf, rate, 200.0), 0.0);
    TestEqual(TEXT("-inf time -> 0"), SOLGasGiant::PhaseTurns(-inf, rate, 200.0), 0.0);
    TestEqual(TEXT("NaN rate -> 0"), SOLGasGiant::PhaseTurns(t, nan, 200.0), 0.0);
    TestEqual(TEXT("inf rate -> 0"), SOLGasGiant::PhaseTurns(t, inf, 200.0), 0.0);
    TestEqual(TEXT("NaN factor -> 0"), SOLGasGiant::PhaseTurns(t, rate, nan), 0.0);
    TestEqual(TEXT("inf factor -> 0"), SOLGasGiant::PhaseTurns(t, rate, inf), 0.0);
    TestEqual(TEXT("BandPhaseTurns with factor 0 -> 0"), SOLGasGiant::BandPhaseTurns(t, 0.5, rate, -rate, 0.0), 0.0);
    TestEqual(TEXT("BandPhaseTurns with NaN time -> 0"), SOLGasGiant::BandPhaseTurns(nan, 0.5, rate, -rate, 200.0),
        0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantBandPhaseTest, "SOLTest.GasGiantMath.BandPhaseComposition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BandPhaseTurns is PhaseTurns at the latitude's drift rate: differential bands separate over time, a rigid profile
// keeps every latitude in step, and mirrored latitudes stay in step
bool FSOLGasGiantBandPhaseTest::RunTest(const FString& /*parameters*/)
{
    const double eq = GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    const double polar = -0.5 * GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    const double factor = 200.0;
    const double t = GASGIANT_SIM_2027_S + 3600.0;

    const double lats[] = { 0.0, 0.2, -0.6, 1.1, 0.5 * UE_DOUBLE_PI };
    for (const double lat : lats)
    {
        const double band = SOLGasGiant::BandPhaseTurns(t, lat, eq, polar, factor);
        const double composed = SOLGasGiant::PhaseTurns(t, SOLGasGiant::DriftRateRadPerS(lat, eq, polar), factor);
        TestTrue(FString::Printf(TEXT("Lat %g: BandPhaseTurns == PhaseTurns(DriftRate)"), lat),
            std::fabs(GasGiantTurnDiff(band, composed)) < GASGIANT_PHASE_TOL);
        TestTrue(FString::Printf(TEXT("Lat %g: in [0, 1)"), lat), GasGiantPhaseInRange(band));
        TestTrue(FString::Printf(TEXT("Lat %g: mirrored latitude in step"), lat), std::fabs(GasGiantTurnDiff(band,
            SOLGasGiant::BandPhaseTurns(t, -lat, eq, polar, factor))) < GASGIANT_PHASE_TOL);
    }

    // Differential rotation: over one hour of sim time the equator and the pole separate by (eq - polar) * f * dt
    const double dt = 3600.0;
    const double eq0 = SOLGasGiant::BandPhaseTurns(t, 0.0, eq, polar, factor);
    const double eq1 = SOLGasGiant::BandPhaseTurns(t + dt, 0.0, eq, polar, factor);
    const double pole0 = SOLGasGiant::BandPhaseTurns(t, 0.5 * UE_DOUBLE_PI, eq, polar, factor);
    const double pole1 = SOLGasGiant::BandPhaseTurns(t + dt, 0.5 * UE_DOUBLE_PI, eq, polar, factor);
    const double relative = GasGiantTurnDiff(GasGiantTurnDiff(eq1, eq0) - GasGiantTurnDiff(pole1, pole0), 0.0);
    TestNearlyEqual(TEXT("Equator gains (eq - polar) * f * dt / 2PI on the pole"), relative,
        (eq - polar) * factor * dt / (2.0 * UE_DOUBLE_PI), 1.0e-9);

    // Rigid profile: every latitude shares one phase
    const double rigid = 2.0e-6;
    const double reference = SOLGasGiant::BandPhaseTurns(t, 0.0, rigid, rigid, factor);
    for (const double lat : lats)
    {
        TestTrue(FString::Printf(TEXT("Rigid profile: lat %g matches the equator"), lat), std::fabs(GasGiantTurnDiff(
            SOLGasGiant::BandPhaseTurns(t, lat, rigid, rigid, factor), reference)) < GASGIANT_PHASE_TOL);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantVisibleAt1xTest, "SOLTest.GasGiantMath.BandsVisiblyDriftAt1x",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With SOL::GAS_GIANT_BAND_SPEED_FACTOR, Jupiter's reference equatorial drift moves >= 0.5 deg per real minute at
// 1x (visible) but takes >= 10 real minutes for a full relative turn (not a blur)
bool FSOLGasGiantVisibleAt1xTest::RunTest(const FString& /*parameters*/)
{
    const double factor = SOL::GAS_GIANT_BAND_SPEED_FACTOR;
    TestTrue(TEXT("Speed factor is positive and finite"), std::isfinite(factor) && factor > 0.0);

    // Measured through the API: one real minute at 1x is 60 sim seconds
    const double t0 = GASGIANT_SIM_2027_S;
    const double eq = GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    const double p0 = SOLGasGiant::BandPhaseTurns(t0, 0.0, eq, 0.0, factor);
    const double p1 = SOLGasGiant::BandPhaseTurns(t0 + 60.0, 0.0, eq, 0.0, factor);
    const double degPerMinute = std::fabs(GasGiantTurnDiff(p1, p0)) * 360.0;
    TestTrue(FString::Printf(TEXT("Equator drifts %.4f deg per real minute (>= 0.5)"), degPerMinute),
        degPerMinute >= 0.5);

    // Full relative turn duration from the rate itself (the circular difference above cannot see > half a turn)
    const double minutesPerTurn = 2.0 * UE_DOUBLE_PI / (eq * factor) / 60.0;
    TestTrue(FString::Printf(TEXT("A full relative turn takes %.2f real minutes (>= 10)"), minutesPerTurn),
        minutesPerTurn >= 10.0);

    // The pole (zero drift) stays put, so the bands visibly shear against each other
    const double pole0 = SOLGasGiant::BandPhaseTurns(t0, 0.5 * UE_DOUBLE_PI, eq, 0.0, factor);
    const double pole1 = SOLGasGiant::BandPhaseTurns(t0 + 60.0, 0.5 * UE_DOUBLE_PI, eq, 0.0, factor);
    TestTrue(TEXT("Zero-drift pole does not move"), std::fabs(GasGiantTurnDiff(pole1, pole0)) < GASGIANT_PHASE_TOL);
    return true;
}

//====================================================================================================================
// Shader handoff and band samples
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantPhaseToShaderTest, "SOLTest.GasGiantMath.PhaseToShader",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PhaseToShader wraps into float [0, 1); values that would round to 1.0f become 0.0f; non-finite gives 0.0f
bool FSOLGasGiantPhaseToShaderTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    TestNearlyEqual(TEXT("0.25 -> 0.25f"), static_cast<double>(SOLGasGiant::PhaseToShader(0.25)), 0.25, 1.0e-7);
    TestNearlyEqual(TEXT("0 -> 0"), static_cast<double>(SOLGasGiant::PhaseToShader(0.0)), 0.0, 1.0e-7);
    TestNearlyEqual(TEXT("1.0 -> 0"), static_cast<double>(SOLGasGiant::PhaseToShader(1.0)), 0.0, 1.0e-7);
    TestNearlyEqual(TEXT("3.5 -> 0.5"), static_cast<double>(SOLGasGiant::PhaseToShader(3.5)), 0.5, 1.0e-7);
    TestNearlyEqual(TEXT("-0.25 -> 0.75"), static_cast<double>(SOLGasGiant::PhaseToShader(-0.25)), 0.75, 1.0e-7);
    TestNearlyEqual(TEXT("-7.75 -> 0.25"), static_cast<double>(SOLGasGiant::PhaseToShader(-7.75)), 0.25, 1.0e-6);
    TestTrue(TEXT("1 - 1e-9 (rounds to 1.0f) -> exactly 0.0f"), SOLGasGiant::PhaseToShader(1.0 - 1.0e-9) == 0.0f);
    TestTrue(TEXT("-1e-12 (wraps to ~1) -> exactly 0.0f"), SOLGasGiant::PhaseToShader(-1.0e-12) == 0.0f);
    TestTrue(TEXT("NaN -> 0.0f"), SOLGasGiant::PhaseToShader(nan) == 0.0f);
    TestTrue(TEXT("+inf -> 0.0f"), SOLGasGiant::PhaseToShader(inf) == 0.0f);
    TestTrue(TEXT("-inf -> 0.0f"), SOLGasGiant::PhaseToShader(-inf) == 0.0f);

    // Sweep: always a finite float in [0, 1)
    const double inputs[] = { -1.0e6, -1.0, -0.5, -1.0e-30, 0.0, 1.0e-30, 0.5, 0.9999999, 0.99999999999, 1.0, 2.0,
        1.0e6 };
    for (const double value : inputs)
    {
        const float shader = SOLGasGiant::PhaseToShader(value);
        TestTrue(FString::Printf(TEXT("PhaseToShader(%.17g) = %.9g is in [0, 1)"), value, shader),
            FMath::IsFinite(shader) && shader >= 0.0f && shader < 1.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantBandSampleLatitudeTest, "SOLTest.GasGiantMath.BandSampleLatitude",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Band sample latitudes run evenly from -PI/2 to +PI/2 inclusive, symmetric, strictly increasing, with clamped
// indices and a single equator sample for count <= 1
bool FSOLGasGiantBandSampleLatitudeTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 count = 16;
    TestNearlyEqual(TEXT("Index 0 is the south pole"), SOLGasGiant::BandSampleLatitudeRad(0, count),
        -0.5 * UE_DOUBLE_PI, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("Last index is the north pole"), SOLGasGiant::BandSampleLatitudeRad(count - 1, count),
        0.5 * UE_DOUBLE_PI, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("Count 3: middle is the equator"), SOLGasGiant::BandSampleLatitudeRad(1, 3), 0.0,
        GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("Count 5: index 1 is -PI/4"), SOLGasGiant::BandSampleLatitudeRad(1, 5), -0.25 * UE_DOUBLE_PI,
        GASGIANT_TIGHT_TOL);

    double previous = -std::numeric_limits<double>::infinity();
    const double spacing = UE_DOUBLE_PI / (count - 1);
    for (int32 i = 0; i < count; ++i)
    {
        const double lat = SOLGasGiant::BandSampleLatitudeRad(i, count);
        TestTrue(FString::Printf(TEXT("Index %d strictly increasing"), i), lat > previous);
        TestNearlyEqual(FString::Printf(TEXT("Index %d evenly spaced"), i), lat, -0.5 * UE_DOUBLE_PI + spacing * i,
            GASGIANT_TIGHT_TOL);
        TestNearlyEqual(FString::Printf(TEXT("Index %d mirrors index %d"), i, count - 1 - i), lat,
            -SOLGasGiant::BandSampleLatitudeRad(count - 1 - i, count), GASGIANT_TIGHT_TOL);
        previous = lat;
    }

    // Clamping and degenerate counts
    TestNearlyEqual(TEXT("Index -1 clamps to the south pole"), SOLGasGiant::BandSampleLatitudeRad(-1, count),
        -0.5 * UE_DOUBLE_PI, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("Index 99 clamps to the north pole"), SOLGasGiant::BandSampleLatitudeRad(99, count),
        0.5 * UE_DOUBLE_PI, GASGIANT_TIGHT_TOL);
    TestEqual(TEXT("Count 1 -> equator"), SOLGasGiant::BandSampleLatitudeRad(0, 1), 0.0);
    TestEqual(TEXT("Count 0 -> equator"), SOLGasGiant::BandSampleLatitudeRad(0, 0), 0.0);
    TestEqual(TEXT("Count -3 -> equator"), SOLGasGiant::BandSampleLatitudeRad(2, -3), 0.0);
    TestNearlyEqual(TEXT("Count 2: index 0 is the south pole"), SOLGasGiant::BandSampleLatitudeRad(0, 2),
        -0.5 * UE_DOUBLE_PI, GASGIANT_TIGHT_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantComputeBandPhasesTest, "SOLTest.GasGiantMath.ComputeBandPhases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ComputeBandPhases fills every slot with the shader phase of its sample latitude; an empty view is a no-op
bool FSOLGasGiantComputeBandPhasesTest::RunTest(const FString& /*parameters*/)
{
    const double eq = GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    const double polar = -0.3 * GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    const double factor = 200.0;
    const double t = GASGIANT_SIM_2027_S + 12345.0;

    const int32 counts[] = { 1, 2, 5, SOL::GAS_GIANT_BAND_SAMPLE_COUNT };
    for (const int32 n : counts)
    {
        TArray<float> phases;
        phases.Init(-1.0f, n);
        SOLGasGiant::ComputeBandPhases(t, eq, polar, factor, MakeArrayView(phases));
        for (int32 i = 0; i < n; ++i)
        {
            const float expected = SOLGasGiant::PhaseToShader(SOLGasGiant::BandPhaseTurns(t,
                SOLGasGiant::BandSampleLatitudeRad(i, n), eq, polar, factor));
            TestTrue(FString::Printf(TEXT("n=%d slot %d is the sample's shader phase (%.9g vs %.9g)"), n, i,
                phases[i], expected), phases[i] == expected);
            TestTrue(FString::Printf(TEXT("n=%d slot %d is in [0, 1)"), n, i), phases[i] >= 0.0f && phases[i] < 1.0f);
        }
    }

    // Empty view: nothing to write, must not crash
    TArray<float> empty;
    SOLGasGiant::ComputeBandPhases(t, eq, polar, factor, MakeArrayView(empty));
    TestEqual(TEXT("Empty view stays empty"), empty.Num(), 0);

    // Factor 0: every slot is 0
    TArray<float> still;
    still.Init(0.5f, 4);
    SOLGasGiant::ComputeBandPhases(t, eq, polar, 0.0, MakeArrayView(still));
    for (int32 i = 0; i < still.Num(); ++i)
    {
        TestTrue(FString::Printf(TEXT("Factor 0: slot %d is 0"), i), still[i] == 0.0f);
    }
    return true;
}

//====================================================================================================================
// Storms
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormsDeterministicTest, "SOLTest.GasGiantMath.StormsDeterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The same arguments give bit-identical storms, regardless of global RNG use in between
bool FSOLGasGiantStormsDeterministicTest::RunTest(const FString& /*parameters*/)
{
    TArray<SOLGasGiant::FStorm> a;
    TArray<SOLGasGiant::FStorm> b;
    SOLGasGiant::GenerateStorms(20261014, 6, 1.2, 0.02, 0.15, a);

    // Disturb the global RNG between the calls
    FMath::RandInit(12345);
    for (int32 i = 0; i < 100; ++i)
    {
        (void)FMath::Rand();
        (void)FMath::FRand();
    }
    SOLGasGiant::GenerateStorms(20261014, 6, 1.2, 0.02, 0.15, b);

    TestTrue(TEXT("Some storms were generated"), a.Num() > 0);
    TestTrue(TEXT("Same seed and arguments -> identical storms"), GasGiantStormsIdentical(a, b));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormsSeedTest, "SOLTest.GasGiantMath.StormsSeedVaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Different seeds (e.g. base seed + body index) give different storm layouts
bool FSOLGasGiantStormsSeedTest::RunTest(const FString& /*parameters*/)
{
    TArray<SOLGasGiant::FStorm> a;
    TArray<SOLGasGiant::FStorm> b;
    TArray<SOLGasGiant::FStorm> c;
    SOLGasGiant::GenerateStorms(20261014 + 4, 5, 1.2, 0.02, 0.1, a);
    SOLGasGiant::GenerateStorms(20261014 + 5, 5, 1.2, 0.02, 0.1, b);
    SOLGasGiant::GenerateStorms(1, 5, 1.2, 0.02, 0.1, c);
    TestFalse(TEXT("Seeds +4 and +5 differ"), GasGiantStormsIdentical(a, b));
    TestFalse(TEXT("Seeds +4 and 1 differ"), GasGiantStormsIdentical(a, c));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormsPrefixTest, "SOLTest.GasGiantMath.StormsPrefixStable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Asking for more storms never reshuffles the earlier ones: count k is a prefix of count n >= k
bool FSOLGasGiantStormsPrefixTest::RunTest(const FString& /*parameters*/)
{
    TArray<SOLGasGiant::FStorm> full;
    SOLGasGiant::GenerateStorms(777, SOL::GAS_GIANT_MAX_STORMS, 1.3, 0.01, 0.05, full);
    for (int32 k = 0; k <= full.Num(); ++k)
    {
        TArray<SOLGasGiant::FStorm> part;
        SOLGasGiant::GenerateStorms(777, k, 1.3, 0.01, 0.05, part);
        TestEqual(FString::Printf(TEXT("Count %d gives %d storms"), k, k), part.Num(), k);
        bool bPrefix = part.Num() <= full.Num();
        for (int32 i = 0; bPrefix && i < part.Num(); ++i)
        {
            bPrefix = part[i].LatitudeRad == full[i].LatitudeRad && part[i].LongitudeRad == full[i].LongitudeRad &&
                part[i].RadiusRad == full[i].RadiusRad;
        }
        TestTrue(FString::Printf(TEXT("Count %d is a prefix of the full list"), k), bPrefix);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormsBoundsTest, "SOLTest.GasGiantMath.StormsWithinBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every storm lies within the latitude limit, has a longitude in [0, 2PI) and a radius in [min, max]
bool FSOLGasGiantStormsBoundsTest::RunTest(const FString& /*parameters*/)
{
    const double maxLat = 0.9;
    const double minRadius = 0.03;
    const double maxRadius = 0.12;
    for (int32 seed = 0; seed < 40; ++seed)
    {
        TArray<SOLGasGiant::FStorm> storms;
        SOLGasGiant::GenerateStorms(seed * 7919 + 3, SOL::GAS_GIANT_MAX_STORMS, maxLat, minRadius, maxRadius, storms);
        TestTrue(FString::Printf(TEXT("Seed %d: count within the clamp"), seed),
            storms.Num() <= SOL::GAS_GIANT_MAX_STORMS);
        for (const SOLGasGiant::FStorm& storm : storms)
        {
            const bool bOk = std::isfinite(storm.LatitudeRad) && std::isfinite(storm.LongitudeRad) &&
                std::isfinite(storm.RadiusRad) && std::fabs(storm.LatitudeRad) <= maxLat &&
                storm.LongitudeRad >= 0.0 && storm.LongitudeRad < 2.0 * UE_DOUBLE_PI &&
                storm.RadiusRad >= minRadius && storm.RadiusRad <= maxRadius;
            if (!bOk)
            {
                AddError(FString::Printf(TEXT("Seed %d: storm (lat %g, lon %g, r %g) out of bounds"), seed,
                    storm.LatitudeRad, storm.LongitudeRad, storm.RadiusRad));
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormsNoOverlapTest, "SOLTest.GasGiantMath.StormsNoOverlap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No two storms overlap: great-circle distance between centres >= sum of radii, including across the 0/2PI seam
bool FSOLGasGiantStormsNoOverlapTest::RunTest(const FString& /*parameters*/)
{
    // Large storms in a narrow belt force rejections and seam crossings
    struct FCase { double MaxLat; double MinR; double MaxR; };
    const FCase cases[] = { { 1.2, 0.05, 0.3 }, { 0.2, 0.1, 0.25 }, { 0.0, 0.2, 0.4 }, { 1.5, 0.01, 0.02 } };
    for (const FCase& testCase : cases)
    {
        for (int32 seed = 0; seed < 25; ++seed)
        {
            TArray<SOLGasGiant::FStorm> storms;
            SOLGasGiant::GenerateStorms(seed + 100, SOL::GAS_GIANT_MAX_STORMS, testCase.MaxLat, testCase.MinR,
                testCase.MaxR, storms);
            for (int32 i = 0; i < storms.Num(); ++i)
            {
                for (int32 j = i + 1; j < storms.Num(); ++j)
                {
                    const double separation = GasGiantGreatCircle(storms[i].LatitudeRad, storms[i].LongitudeRad,
                        storms[j].LatitudeRad, storms[j].LongitudeRad);
                    const double needed = storms[i].RadiusRad + storms[j].RadiusRad;
                    if (separation < needed - 1.0e-9)
                    {
                        AddError(FString::Printf(TEXT("Case maxLat %g seed %d: storms %d and %d overlap (%g < %g)"),
                            testCase.MaxLat, seed, i, j, separation, needed));
                    }
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormsSmallAllPlacedTest, "SOLTest.GasGiantMath.StormsSmallAllPlaced",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Storms small relative to the sphere are all placed (the requested count, up to the clamp)
bool FSOLGasGiantStormsSmallAllPlacedTest::RunTest(const FString& /*parameters*/)
{
    for (int32 seed = 0; seed < 30; ++seed)
    {
        TArray<SOLGasGiant::FStorm> storms;
        SOLGasGiant::GenerateStorms(seed, SOL::GAS_GIANT_MAX_STORMS, 1.2, 0.01, 0.02, storms);
        TestEqual(FString::Printf(TEXT("Seed %d: all %d small storms placed"), seed, SOL::GAS_GIANT_MAX_STORMS),
            storms.Num(), SOL::GAS_GIANT_MAX_STORMS);

        TArray<SOLGasGiant::FStorm> three;
        SOLGasGiant::GenerateStorms(seed, 3, 1.2, 0.01, 0.02, three);
        TestEqual(FString::Printf(TEXT("Seed %d: 3 requested, 3 placed"), seed), three.Num(), 3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormsCountTest, "SOLTest.GasGiantMath.StormsCountClampAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The count is clamped to [0, SOL::GAS_GIANT_MAX_STORMS] and the output array is reset, never appended to
bool FSOLGasGiantStormsCountTest::RunTest(const FString& /*parameters*/)
{
    TArray<SOLGasGiant::FStorm> storms = GasGiantJunkStorms();
    SOLGasGiant::GenerateStorms(5, 1000, 1.2, 0.01, 0.02, storms);
    TestEqual(TEXT("Count 1000 clamps to the max"), storms.Num(), SOL::GAS_GIANT_MAX_STORMS);
    bool bNoJunk = true;
    for (const SOLGasGiant::FStorm& storm : storms)
    {
        bNoJunk &= storm.RadiusRad != 42.0;
    }
    TestTrue(TEXT("Pre-filled junk is gone"), bNoJunk);

    storms = GasGiantJunkStorms();
    SOLGasGiant::GenerateStorms(5, 0, 1.2, 0.01, 0.02, storms);
    TestEqual(TEXT("Count 0 -> empty (reset)"), storms.Num(), 0);

    storms = GasGiantJunkStorms();
    SOLGasGiant::GenerateStorms(5, -5, 1.2, 0.01, 0.02, storms);
    TestEqual(TEXT("Count -5 -> empty (reset)"), storms.Num(), 0);

    storms = GasGiantJunkStorms();
    SOLGasGiant::GenerateStorms(5, 2, 1.2, 0.01, 0.02, storms);
    TestEqual(TEXT("Count 2 replaces 5 junk entries with 2 storms"), storms.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormsInvalidTest, "SOLTest.GasGiantMath.StormsInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Latitude limit NaN -> equator, > PI/2 clamps; swapped radii are swapped back; negative min clamps to 0;
// non-finite radii or max <= 0 give no storms (output reset)
bool FSOLGasGiantStormsInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    // NaN latitude limit -> all on the equator
    TArray<SOLGasGiant::FStorm> storms;
    SOLGasGiant::GenerateStorms(9, 4, nan, 0.01, 0.02, storms);
    for (const SOLGasGiant::FStorm& storm : storms)
    {
        TestEqual(TEXT("NaN max latitude: storm on the equator"), storm.LatitudeRad, 0.0);
    }

    // Negative latitude limit clamps to 0
    SOLGasGiant::GenerateStorms(9, 4, -0.5, 0.01, 0.02, storms);
    for (const SOLGasGiant::FStorm& storm : storms)
    {
        TestEqual(TEXT("Negative max latitude: storm on the equator"), storm.LatitudeRad, 0.0);
    }

    // Latitude limit beyond the pole clamps to PI/2
    SOLGasGiant::GenerateStorms(9, SOL::GAS_GIANT_MAX_STORMS, 5.0, 0.01, 0.02, storms);
    TestTrue(TEXT("Huge max latitude still places storms"), storms.Num() > 0);
    for (const SOLGasGiant::FStorm& storm : storms)
    {
        TestTrue(TEXT("Huge max latitude: |lat| <= PI/2"), std::fabs(storm.LatitudeRad) <= 0.5 * UE_DOUBLE_PI);
    }

    // min > max is swapped
    SOLGasGiant::GenerateStorms(9, 4, 1.0, 0.05, 0.02, storms);
    TestTrue(TEXT("Swapped radii still place storms"), storms.Num() > 0);
    for (const SOLGasGiant::FStorm& storm : storms)
    {
        TestTrue(TEXT("Swapped radii: radius in [0.02, 0.05]"), storm.RadiusRad >= 0.02 && storm.RadiusRad <= 0.05);
    }

    // Negative min clamps to 0
    SOLGasGiant::GenerateStorms(9, 4, 1.0, -0.5, 0.03, storms);
    TestTrue(TEXT("Negative min still places storms"), storms.Num() > 0);
    for (const SOLGasGiant::FStorm& storm : storms)
    {
        TestTrue(TEXT("Negative min: radius in [0, 0.03]"), storm.RadiusRad >= 0.0 && storm.RadiusRad <= 0.03);
    }

    // Empty-output cases, each starting from junk to prove the reset
    struct FEmptyCase { const TCHAR* Label; double MinR; double MaxR; };
    const FEmptyCase emptyCases[] = { { TEXT("max 0"), 0.0, 0.0 }, { TEXT("max < 0"), -0.2, -0.1 },
        { TEXT("NaN min"), nan, 0.1 }, { TEXT("NaN max"), 0.01, nan }, { TEXT("inf max"), 0.01, inf },
        { TEXT("-inf min"), -inf, 0.1 } };
    for (const FEmptyCase& emptyCase : emptyCases)
    {
        storms = GasGiantJunkStorms();
        SOLGasGiant::GenerateStorms(9, 4, 1.0, emptyCase.MinR, emptyCase.MaxR, storms);
        TestEqual(FString::Printf(TEXT("%s -> no storms"), emptyCase.Label), storms.Num(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStormLongitudeTest, "SOLTest.GasGiantMath.StormLongitude",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A storm drifts with the band at its latitude: authored longitude at t = 0 or factor 0, wrapped into [0, 2PI),
// and its offset from the band phase stays constant over time
bool FSOLGasGiantStormLongitudeTest::RunTest(const FString& /*parameters*/)
{
    const double twoPi = 2.0 * UE_DOUBLE_PI;
    const double eq = GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    const double polar = -0.5 * GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    const double factor = 200.0;

    SOLGasGiant::FStorm storm;
    storm.LatitudeRad = -0.39;     // Great-Red-Spot-like southern latitude
    storm.LongitudeRad = 1.0;
    storm.RadiusRad = 0.1;

    TestNearlyEqual(TEXT("t = 0 -> authored longitude"), SOLGasGiant::StormLongitudeRad(storm, 0.0, eq, polar, factor),
        1.0, GASGIANT_ANGLE_TOL_RAD);
    TestNearlyEqual(TEXT("Factor 0 -> authored longitude"), SOLGasGiant::StormLongitudeRad(storm, GASGIANT_SIM_2027_S,
        eq, polar, 0.0), 1.0, GASGIANT_ANGLE_TOL_RAD);

    // Out-of-range authored longitude is wrapped at t = 0
    SOLGasGiant::FStorm negative = storm;
    negative.LongitudeRad = -1.0;
    TestNearlyEqual(TEXT("Authored -1 -> 2PI - 1"), SOLGasGiant::StormLongitudeRad(negative, 0.0, eq, polar, factor),
        twoPi - 1.0, GASGIANT_ANGLE_TOL_RAD);
    SOLGasGiant::FStorm big = storm;
    big.LongitudeRad = 7.0;
    TestNearlyEqual(TEXT("Authored 7 -> 7 - 2PI"), SOLGasGiant::StormLongitudeRad(big, 0.0, eq, polar, factor),
        7.0 - twoPi, GASGIANT_ANGLE_TOL_RAD);

    // Moves with its band: longitude - 2PI * band phase is the authored longitude at every time
    const double times[] = { 60.0, 3600.0, 86400.0, GASGIANT_SIM_2027_S, -GASGIANT_SIM_2027_S };
    for (const double t : times)
    {
        const double lon = SOLGasGiant::StormLongitudeRad(storm, t, eq, polar, factor);
        TestTrue(FString::Printf(TEXT("t=%g: longitude in [0, 2PI)"), t), std::isfinite(lon) && lon >= 0.0 &&
            lon < twoPi);
        const double band = SOLGasGiant::BandPhaseTurns(t, storm.LatitudeRad, eq, polar, factor);
        TestTrue(FString::Printf(TEXT("t=%g: storm rides its band"), t),
            std::fabs(GasGiantAngleDiff(lon - twoPi * band, storm.LongitudeRad)) < 1.0e-8);
    }

    // Wraps across 2PI: start just below the seam with positive drift
    SOLGasGiant::FStorm seam = storm;
    seam.LongitudeRad = twoPi - 0.001;
    const double rate = SOLGasGiant::DriftRateRadPerS(seam.LatitudeRad, eq, polar);
    TestTrue(TEXT("Reference drift at the storm latitude is positive"), rate > 0.0);
    const double crossT = 0.002 / (rate * factor);
    const double crossed = SOLGasGiant::StormLongitudeRad(seam, crossT, eq, polar, factor);
    TestTrue(TEXT("Seam crossing stays in [0, 2PI)"), crossed >= 0.0 && crossed < twoPi);
    TestNearlyEqual(TEXT("2PI - 0.001 + 0.002 -> 0.001"), crossed, 0.001, 1.0e-8);

    // Storms at different latitudes drift apart under differential rotation
    SOLGasGiant::FStorm equatorial = storm;
    equatorial.LatitudeRad = 0.0;
    const double dt = 3600.0;
    const double driftA = GasGiantAngleDiff(SOLGasGiant::StormLongitudeRad(storm, dt, eq, polar, factor),
        SOLGasGiant::StormLongitudeRad(storm, 0.0, eq, polar, factor));
    const double driftB = GasGiantAngleDiff(SOLGasGiant::StormLongitudeRad(equatorial, dt, eq, polar, factor),
        SOLGasGiant::StormLongitudeRad(equatorial, 0.0, eq, polar, factor));
    TestTrue(TEXT("Equatorial storm outruns the southern one (eq > polar)"), driftB > driftA);

    // Pause-safe: the same time gives the bit-identical longitude
    TestTrue(TEXT("Same time -> identical longitude"),
        SOLGasGiant::StormLongitudeRad(storm, GASGIANT_SIM_2027_S, eq, polar, factor) ==
        SOLGasGiant::StormLongitudeRad(storm, GASGIANT_SIM_2027_S, eq, polar, factor));

    // Non-finite time behaves like phase 0 (authored longitude)
    TestNearlyEqual(TEXT("NaN time -> authored longitude"), SOLGasGiant::StormLongitudeRad(storm,
        std::numeric_limits<double>::quiet_NaN(), eq, polar, factor), 1.0, GASGIANT_ANGLE_TOL_RAD);
    return true;
}

//====================================================================================================================
// Near layer blend
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantNearBlendBoundariesTest, "SOLTest.GasGiantMath.NearBlendBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The blend is 1 at/inside fadeIn radii (including inside the planet), 0 at/beyond fadeOut radii, smoothstep between
bool FSOLGasGiantNearBlendBoundariesTest::RunTest(const FString& /*parameters*/)
{
    const double r = GASGIANT_JUPITER_RADIUS_M;
    const double in = GASGIANT_FADE_IN_RADII;
    const double out = GASGIANT_FADE_OUT_RADII;

    TestEqual(TEXT("Centre is 1"), SOLGasGiant::NearLayerBlend(0.0, r, in, out), 1.0);
    TestEqual(TEXT("Negative distance is 1"), SOLGasGiant::NearLayerBlend(-5.0, r, in, out), 1.0);
    TestEqual(TEXT("At the cloud tops is 1"), SOLGasGiant::NearLayerBlend(r, r, in, out), 1.0);
    TestEqual(TEXT("Exactly fadeIn radii is 1"), SOLGasGiant::NearLayerBlend(in * r, r, in, out), 1.0);
    TestEqual(TEXT("Exactly fadeOut radii is 0"), SOLGasGiant::NearLayerBlend(out * r, r, in, out), 0.0);
    TestEqual(TEXT("Beyond fadeOut is 0"), SOLGasGiant::NearLayerBlend(out * r + 1.0, r, in, out), 0.0);
    TestEqual(TEXT("Interplanetary distance is 0"), SOLGasGiant::NearLayerBlend(7.8e11, r, in, out), 0.0);

    // Midpoint (3 radii) and quarter points
    TestNearlyEqual(TEXT("3 radii -> 0.5"), SOLGasGiant::NearLayerBlend(3.0 * r, r, in, out), 0.5, GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("2.5 radii -> 0.84375"), SOLGasGiant::NearLayerBlend(2.5 * r, r, in, out), 0.84375,
        GASGIANT_TIGHT_TOL);
    TestNearlyEqual(TEXT("3.5 radii -> 0.15625"), SOLGasGiant::NearLayerBlend(3.5 * r, r, in, out), 0.15625,
        GASGIANT_TIGHT_TOL);

    const double ts[] = { 0.1, 0.37, 0.8 };
    for (const double t : ts)
    {
        TestNearlyEqual(FString::Printf(TEXT("t=%g matches 1 - smoothstep"), t), SOLGasGiant::NearLayerBlend(
            (in + t * (out - in)) * r, r, in, out), 1.0 - GasGiantSmoothstep(t), GASGIANT_TIGHT_TOL);
    }

    // Just inside each edge is strictly between 0 and 1
    const double nearIn = SOLGasGiant::NearLayerBlend(in * r + 10.0, r, in, out);
    const double nearOut = SOLGasGiant::NearLayerBlend(out * r - 10.0, r, in, out);
    TestTrue(TEXT("Just past fadeIn is in (0, 1)"), nearIn > 0.0 && nearIn < 1.0);
    TestTrue(TEXT("Just before fadeOut is in (0, 1)"), nearOut > 0.0 && nearOut < 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantNearBlendMonotonicTest, "SOLTest.GasGiantMath.NearBlendMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sweeping the distance outward gives a finite, non-increasing blend in [0, 1]
bool FSOLGasGiantNearBlendMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const double r = GASGIANT_JUPITER_RADIUS_M;
    constexpr int32 steps = 500;
    double previous = 2.0;
    bool bOk = true;
    for (int32 i = 0; i <= steps; ++i)
    {
        const double d = 6.0 * r * static_cast<double>(i) / steps;
        const double blend = SOLGasGiant::NearLayerBlend(d, r, GASGIANT_FADE_IN_RADII, GASGIANT_FADE_OUT_RADII);
        if (!std::isfinite(blend) || blend < 0.0 || blend > 1.0 || blend > previous)
        {
            AddError(FString::Printf(TEXT("Blend %.17g at d=%g violates [0,1] / non-increasing"), blend, d));
            bOk = false;
            break;
        }
        previous = blend;
    }
    TestTrue(TEXT("Sweep is finite, in [0, 1] and non-increasing"), bOk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantNearBlendScaleTest, "SOLTest.GasGiantMath.NearBlendScalesWithRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The blend depends only on distance in planet radii, so Jupiter and Neptune fade at the same relative distance
bool FSOLGasGiantNearBlendScaleTest::RunTest(const FString& /*parameters*/)
{
    const double jupiter = GASGIANT_JUPITER_RADIUS_M;
    const double neptune = 2.4764e7;
    const double radii[] = { 0.5, 1.0, 2.0, 2.3, 3.0, 3.9, 4.0, 10.0 };
    for (const double x : radii)
    {
        TestNearlyEqual(FString::Printf(TEXT("%g radii: Jupiter == Neptune"), x),
            SOLGasGiant::NearLayerBlend(x * jupiter, jupiter, GASGIANT_FADE_IN_RADII, GASGIANT_FADE_OUT_RADII),
            SOLGasGiant::NearLayerBlend(x * neptune, neptune, GASGIANT_FADE_IN_RADII, GASGIANT_FADE_OUT_RADII),
            1.0e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantNearBlendDegenerateTest, "SOLTest.GasGiantMath.NearBlendDegenerateBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// fadeOut <= fadeIn degrades to a hard step at fadeIn with no NaN from a zero or negative band width
bool FSOLGasGiantNearBlendDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const double r = 1000.0;
    TestEqual(TEXT("Equal band, below is 1"), SOLGasGiant::NearLayerBlend(2500.0, r, 3.0, 3.0), 1.0);
    TestEqual(TEXT("Equal band, at fadeIn is 1"), SOLGasGiant::NearLayerBlend(3000.0, r, 3.0, 3.0), 1.0);
    TestEqual(TEXT("Equal band, just above is 0"), SOLGasGiant::NearLayerBlend(3000.01, r, 3.0, 3.0), 0.0);
    TestEqual(TEXT("Inverted band, below fadeIn is 1"), SOLGasGiant::NearLayerBlend(3500.0, r, 4.0, 2.0), 1.0);
    TestEqual(TEXT("Inverted band, at fadeIn is 1"), SOLGasGiant::NearLayerBlend(4000.0, r, 4.0, 2.0), 1.0);
    TestEqual(TEXT("Inverted band, above fadeIn is 0"), SOLGasGiant::NearLayerBlend(4500.0, r, 4.0, 2.0), 0.0);
    TestEqual(TEXT("Both zero, at the centre is 1"), SOLGasGiant::NearLayerBlend(0.0, r, 0.0, 0.0), 1.0);
    TestEqual(TEXT("Both zero, outside is 0"), SOLGasGiant::NearLayerBlend(1.0, r, 0.0, 0.0), 0.0);
    TestFalse(TEXT("Both zero never NaN"), std::isnan(SOLGasGiant::NearLayerBlend(0.0, r, 0.0, 0.0)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantNearBlendInvalidTest, "SOLTest.GasGiantMath.NearBlendInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A non-finite distance, or a non-finite or non-positive planet radius, gives blend 0 (layer off, never NaN)
bool FSOLGasGiantNearBlendInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double r = GASGIANT_JUPITER_RADIUS_M;
    const double in = GASGIANT_FADE_IN_RADII;
    const double out = GASGIANT_FADE_OUT_RADII;

    TestEqual(TEXT("Radius 0 -> 0"), SOLGasGiant::NearLayerBlend(1.0e6, 0.0, in, out), 0.0);
    TestEqual(TEXT("Radius 0 at distance 0 -> 0"), SOLGasGiant::NearLayerBlend(0.0, 0.0, in, out), 0.0);
    TestEqual(TEXT("Negative radius -> 0"), SOLGasGiant::NearLayerBlend(1.0e6, -r, in, out), 0.0);
    TestEqual(TEXT("NaN radius -> 0"), SOLGasGiant::NearLayerBlend(1.0e6, nan, in, out), 0.0);
    TestEqual(TEXT("Inf radius -> 0"), SOLGasGiant::NearLayerBlend(1.0e6, inf, in, out), 0.0);
    TestEqual(TEXT("NaN distance -> 0"), SOLGasGiant::NearLayerBlend(nan, r, in, out), 0.0);
    TestEqual(TEXT("+inf distance -> 0"), SOLGasGiant::NearLayerBlend(inf, r, in, out), 0.0);
    TestEqual(TEXT("-inf distance -> 0"), SOLGasGiant::NearLayerBlend(-inf, r, in, out), 0.0);
    return true;
}

//====================================================================================================================
// Near layer visibility and release delay
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantNearVisibleTest, "SOLTest.GasGiantMath.NearLayerVisible",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The near layer is visible only for a finite, strictly positive blend
bool FSOLGasGiantNearVisibleTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("Blend 1 is visible"), SOLGasGiant::IsNearLayerVisible(1.0));
    TestTrue(TEXT("Tiny positive blend is visible"), SOLGasGiant::IsNearLayerVisible(1.0e-9));
    TestFalse(TEXT("Blend 0 is hidden"), SOLGasGiant::IsNearLayerVisible(0.0));
    TestFalse(TEXT("Negative blend is hidden"), SOLGasGiant::IsNearLayerVisible(-0.1));
    TestFalse(TEXT("NaN blend is hidden"), SOLGasGiant::IsNearLayerVisible(std::numeric_limits<double>::quiet_NaN()));
    TestFalse(TEXT("Inf blend is hidden"), SOLGasGiant::IsNearLayerVisible(std::numeric_limits<double>::infinity()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStepActivateTest, "SOLTest.GasGiantMath.StepNearLayerActivate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The first visible step activates the layer exactly once; further visible steps do nothing
bool FSOLGasGiantStepActivateTest::RunTest(const FString& /*parameters*/)
{
    SOLGasGiant::FNearLayerState state;
    TestFalse(TEXT("Default state is inactive"), state.bActive);
    TestEqual(TEXT("Default hidden time is 0"), state.HiddenSeconds, 0.0);

    TestTrue(TEXT("First visible step -> Activate"), SOLGasGiant::StepNearLayer(state, 0.3, 1.0 / 60.0, 20.0) ==
        SOLGasGiant::ENearLayerAction::Activate);
    TestTrue(TEXT("Now active"), state.bActive);
    for (int32 i = 0; i < 5; ++i)
    {
        TestTrue(TEXT("Further visible steps -> None"), SOLGasGiant::StepNearLayer(state, 1.0, 1.0 / 60.0, 20.0) ==
            SOLGasGiant::ENearLayerAction::None);
    }
    TestTrue(TEXT("Still active"), state.bActive);
    TestEqual(TEXT("Hidden time stays 0 while visible"), state.HiddenSeconds, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStepReleaseTest, "SOLTest.GasGiantMath.StepNearLayerReleaseAfterDelay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Hidden for the full delay releases exactly once (at >= delay), resets the state, and later hidden steps do nothing
bool FSOLGasGiantStepReleaseTest::RunTest(const FString& /*parameters*/)
{
    SOLGasGiant::FNearLayerState state;
    SOLGasGiant::StepNearLayer(state, 1.0, 0.1, 20.0);

    // 5 s steps: 5, 10, 15 keep the resources
    for (int32 i = 1; i <= 3; ++i)
    {
        TestTrue(FString::Printf(TEXT("Hidden %d s -> None"), 5 * i), SOLGasGiant::StepNearLayer(state, 0.0, 5.0,
            20.0) == SOLGasGiant::ENearLayerAction::None);
        TestTrue(TEXT("Still active (resources kept)"), state.bActive);
        TestNearlyEqual(TEXT("Hidden time accumulates"), state.HiddenSeconds, 5.0 * i, GASGIANT_TIGHT_TOL);
    }

    // 20 s reaches the delay exactly -> Release
    TestTrue(TEXT("Hidden 20 s (== delay) -> Release"), SOLGasGiant::StepNearLayer(state, 0.0, 5.0, 20.0) ==
        SOLGasGiant::ENearLayerAction::Release);
    TestFalse(TEXT("Inactive after release"), state.bActive);
    TestEqual(TEXT("Hidden time reset after release"), state.HiddenSeconds, 0.0);

    // Further hidden steps never release again
    for (int32 i = 0; i < 10; ++i)
    {
        TestTrue(TEXT("Hidden after release -> None"), SOLGasGiant::StepNearLayer(state, 0.0, 5.0, 20.0) ==
            SOLGasGiant::ENearLayerAction::None);
    }
    TestEqual(TEXT("Hidden time stays 0 while inactive"), state.HiddenSeconds, 0.0);

    // A single big step past the delay releases on that step
    SOLGasGiant::FNearLayerState jump;
    SOLGasGiant::StepNearLayer(jump, 1.0, 0.1, 20.0);
    TestTrue(TEXT("One 25 s hidden step -> Release"), SOLGasGiant::StepNearLayer(jump, 0.0, 25.0, 20.0) ==
        SOLGasGiant::ENearLayerAction::Release);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStepHysteresisTest, "SOLTest.GasGiantMath.StepNearLayerHysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Hiding for less than the delay and showing again keeps the resources (no Release, no second Activate) and
// restarts the timer, so flickering at the fade edge never thrashes allocations
bool FSOLGasGiantStepHysteresisTest::RunTest(const FString& /*parameters*/)
{
    SOLGasGiant::FNearLayerState state;
    SOLGasGiant::StepNearLayer(state, 1.0, 0.1, 20.0);

    // Hidden 15 s, visible again
    SOLGasGiant::StepNearLayer(state, 0.0, 15.0, 20.0);
    TestTrue(TEXT("Visible again within the delay -> None (no re-Activate)"), SOLGasGiant::StepNearLayer(state, 0.5,
        0.1, 20.0) == SOLGasGiant::ENearLayerAction::None);
    TestTrue(TEXT("Still active"), state.bActive);
    TestEqual(TEXT("Timer restarted"), state.HiddenSeconds, 0.0);

    // Hidden another 15 s: had the timer not restarted this would release (30 s total)
    TestTrue(TEXT("Another 15 s hidden -> None"), SOLGasGiant::StepNearLayer(state, 0.0, 15.0, 20.0) ==
        SOLGasGiant::ENearLayerAction::None);
    TestTrue(TEXT("Still active after 15 + 15 split by a visible frame"), state.bActive);

    // 5 more seconds completes a contiguous 20 s
    TestTrue(TEXT("Contiguous 20 s hidden -> Release"), SOLGasGiant::StepNearLayer(state, 0.0, 5.0, 20.0) ==
        SOLGasGiant::ENearLayerAction::Release);

    // Rapid flicker at 60 fps for a long time never releases and never re-activates
    SOLGasGiant::FNearLayerState flicker;
    SOLGasGiant::StepNearLayer(flicker, 1.0, 1.0 / 60.0, 20.0);
    bool bQuiet = true;
    for (int32 frame = 0; frame < 6000; ++frame)
    {
        const double blend = (frame % 2 == 0) ? 0.0 : 1.0e-4;
        bQuiet &= SOLGasGiant::StepNearLayer(flicker, blend, 1.0 / 60.0, 20.0) == SOLGasGiant::ENearLayerAction::None;
    }
    TestTrue(TEXT("Flickering blend -> only None actions"), bQuiet);
    TestTrue(TEXT("Flickering blend keeps the layer active"), flicker.bActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStepFrameRateTest, "SOLTest.GasGiantMath.StepNearLayerFrameSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At 60 fps the release lands after ~20 s of hidden frames (frame 1199-1201), not earlier or much later
bool FSOLGasGiantStepFrameRateTest::RunTest(const FString& /*parameters*/)
{
    SOLGasGiant::FNearLayerState state;
    SOLGasGiant::StepNearLayer(state, 1.0, 1.0 / 60.0, 20.0);
    int32 releaseFrame = -1;
    int32 releaseCount = 0;
    for (int32 frame = 1; frame <= 1500; ++frame)
    {
        if (SOLGasGiant::StepNearLayer(state, 0.0, 1.0 / 60.0, 20.0) == SOLGasGiant::ENearLayerAction::Release)
        {
            ++releaseCount;
            releaseFrame = frame;
        }
    }
    TestEqual(TEXT("Exactly one release"), releaseCount, 1);
    TestTrue(FString::Printf(TEXT("Released at frame %d (expected 1199-1201)"), releaseFrame),
        releaseFrame >= 1199 && releaseFrame <= 1201);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStepInactiveTest, "SOLTest.GasGiantMath.StepNearLayerInactiveHidden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A layer that was never shown stays inactive with no actions and no hidden-time accumulation
bool FSOLGasGiantStepInactiveTest::RunTest(const FString& /*parameters*/)
{
    SOLGasGiant::FNearLayerState state;
    bool bQuiet = true;
    for (int32 i = 0; i < 100; ++i)
    {
        bQuiet &= SOLGasGiant::StepNearLayer(state, 0.0, 1.0, 20.0) == SOLGasGiant::ENearLayerAction::None;
    }
    TestTrue(TEXT("Never-visible layer -> only None"), bQuiet);
    TestFalse(TEXT("Still inactive"), state.bActive);
    TestEqual(TEXT("Hidden time stays 0"), state.HiddenSeconds, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStepReactivateTest, "SOLTest.GasGiantMath.StepNearLayerReactivate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After a release, becoming visible again activates the layer again
bool FSOLGasGiantStepReactivateTest::RunTest(const FString& /*parameters*/)
{
    SOLGasGiant::FNearLayerState state;
    SOLGasGiant::StepNearLayer(state, 1.0, 0.1, 20.0);
    TestTrue(TEXT("Released"), SOLGasGiant::StepNearLayer(state, 0.0, 30.0, 20.0) ==
        SOLGasGiant::ENearLayerAction::Release);
    TestTrue(TEXT("Visible after release -> Activate"), SOLGasGiant::StepNearLayer(state, 0.2, 0.1, 20.0) ==
        SOLGasGiant::ENearLayerAction::Activate);
    TestTrue(TEXT("Active again"), state.bActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStepInvalidDtTest, "SOLTest.GasGiantMath.StepNearLayerInvalidDelta",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A NaN, infinite or negative delta counts as 0 (never releases early, never poisons the timer); NaN blend is hidden
bool FSOLGasGiantStepInvalidDtTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    SOLGasGiant::FNearLayerState state;
    SOLGasGiant::StepNearLayer(state, 1.0, 0.1, 20.0);
    const double badDeltas[] = { nan, inf, -inf, -5.0, -1.0e9 };
    for (const double dt : badDeltas)
    {
        TestTrue(FString::Printf(TEXT("Hidden with dt %g -> None"), dt), SOLGasGiant::StepNearLayer(state, 0.0, dt,
            20.0) == SOLGasGiant::ENearLayerAction::None);
        TestTrue(FString::Printf(TEXT("Hidden time finite and 0 after dt %g"), dt),
            std::isfinite(state.HiddenSeconds) && state.HiddenSeconds == 0.0);
        TestTrue(TEXT("Still active"), state.bActive);
    }

    // Valid steps after bad ones still count normally
    SOLGasGiant::StepNearLayer(state, 0.0, 10.0, 20.0);
    TestNearlyEqual(TEXT("10 s counted after bad deltas"), state.HiddenSeconds, 10.0, GASGIANT_TIGHT_TOL);

    // NaN blend is treated as hidden: activates nothing, counts toward release
    SOLGasGiant::FNearLayerState fresh;
    TestTrue(TEXT("NaN blend on an inactive layer -> None"), SOLGasGiant::StepNearLayer(fresh, nan, 1.0, 20.0) ==
        SOLGasGiant::ENearLayerAction::None);
    TestFalse(TEXT("NaN blend does not activate"), fresh.bActive);
    TestTrue(TEXT("NaN blend counts as hidden toward release"), SOLGasGiant::StepNearLayer(state, nan, 10.0, 20.0) ==
        SOLGasGiant::ENearLayerAction::Release);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantStepZeroDelayTest, "SOLTest.GasGiantMath.StepNearLayerZeroDelay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero, negative or NaN release delay releases on the first hidden step, even with dt 0
bool FSOLGasGiantStepZeroDelayTest::RunTest(const FString& /*parameters*/)
{
    const double delays[] = { 0.0, -1.0, std::numeric_limits<double>::quiet_NaN() };
    for (const double delay : delays)
    {
        SOLGasGiant::FNearLayerState state;
        SOLGasGiant::StepNearLayer(state, 1.0, 0.1, delay);
        TestTrue(FString::Printf(TEXT("Delay %g: first hidden step (dt 0) -> Release"), delay),
            SOLGasGiant::StepNearLayer(state, 0.0, 0.0, delay) == SOLGasGiant::ENearLayerAction::Release);
        TestFalse(FString::Printf(TEXT("Delay %g: inactive"), delay), state.bActive);
    }
    return true;
}

//====================================================================================================================
// Constants
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantConstantsTest, "SOLTest.GasGiantMath.ConstantsSanity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The gas giant constants in SOLConstants.h have sane, mutually consistent values
bool FSOLGasGiantConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("Band speed factor > 0"), SOL::GAS_GIANT_BAND_SPEED_FACTOR > 0.0);
    TestTrue(TEXT("At least 2 band samples"), SOL::GAS_GIANT_BAND_SAMPLE_COUNT >= 2);
    TestTrue(TEXT("Band samples fit float4 parameters"), SOL::GAS_GIANT_BAND_SAMPLE_COUNT % 4 == 0);
    TestTrue(TEXT("At least one storm"), SOL::GAS_GIANT_MAX_STORMS >= 1);
    TestTrue(TEXT("At least one placement attempt"), SOL::GAS_GIANT_STORM_PLACEMENT_ATTEMPTS >= 1);
    TestTrue(TEXT("Fade-in is above the cloud tops (> 1 radius)"), SOL::GAS_GIANT_NEAR_FADE_IN_RADII > 1.0);
    TestTrue(TEXT("Fade-out beyond fade-in"), SOL::GAS_GIANT_NEAR_FADE_OUT_RADII > SOL::GAS_GIANT_NEAR_FADE_IN_RADII);
    TestTrue(TEXT("Release delay > 0"), SOL::GAS_GIANT_NEAR_RELEASE_DELAY_SECONDS > 0.0);

    // The shipped constants give a visible near layer at the cloud tops and none from far away
    const double r = GASGIANT_JUPITER_RADIUS_M;
    TestEqual(TEXT("Shipped band: blend 1 at the cloud tops"), SOLGasGiant::NearLayerBlend(r, r,
        SOL::GAS_GIANT_NEAR_FADE_IN_RADII, SOL::GAS_GIANT_NEAR_FADE_OUT_RADII), 1.0);
    TestEqual(TEXT("Shipped band: blend 0 at 100 radii"), SOLGasGiant::NearLayerBlend(100.0 * r, r,
        SOL::GAS_GIANT_NEAR_FADE_IN_RADII, SOL::GAS_GIANT_NEAR_FADE_OUT_RADII), 0.0);
    return true;
}

//====================================================================================================================
// Wrap-safe band lookup and haze fade (SDD 14 Amendment 3; test-after for the 14c phase-wrap fix)
//====================================================================================================================

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the latitude a fraction w of the way from band sample lower to lower + 1 (of count)
    double GasGiantGapLatitude(const int32 lower, const double w, const int32 count)
    {
        return -0.5 * UE_DOUBLE_PI + UE_DOUBLE_PI * (static_cast<double>(lower) + w) / static_cast<double>(count - 1);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a test field cos(lon - 2 PI phase), advected by the wrap-safe lookup (as the materials do)
    double GasGiantBlendedField(const double latRad, const double lonRad, const float* phases, const int32 count)
    {
        const SOLGasGiant::FBandBlend blend = SOLGasGiant::BandBlendAt(latRad, count, 0.25);
        const double lower = std::cos(lonRad - 2.0 * UE_DOUBLE_PI * phases[blend.Lower]);
        const double upper = std::cos(lonRad - 2.0 * UE_DOUBLE_PI * phases[blend.Upper]);
        return lower + (upper - lower) * blend.UpperWeight;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the same field advected by the 14c shortest-arc phase interpolation (the jumping behaviour)
    double GasGiantShortestArcField(const double latRad, const double lonRad, const float* phases, const int32 count)
    {
        const double sampleF = (latRad / UE_DOUBLE_PI + 0.5) * (count - 1);
        const int32 i0 = FMath::Clamp(static_cast<int32>(std::floor(sampleF)), 0, count - 2);
        const double w = sampleF - i0;
        const double d = phases[i0 + 1] - phases[i0];
        const double phase = phases[i0] + (d - std::floor(d + 0.5)) * w;
        return std::cos(lonRad - 2.0 * UE_DOUBLE_PI * phase);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantBandBlendExamplesTest, "SOLTest.GasGiantMath.BandBlendExamples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BandBlendAt picks the neighbouring samples and cross-fades only over the middle of the gap
bool FSOLGasGiantBandBlendExamplesTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 count = 16;
    const SOLGasGiant::FBandBlend south = SOLGasGiant::BandBlendAt(-0.5 * UE_DOUBLE_PI, count, 0.25);
    TestEqual(TEXT("South pole: lower 0"), south.Lower, 0);
    TestEqual(TEXT("South pole: upper 1"), south.Upper, 1);
    TestEqual(TEXT("South pole: weight 0"), south.UpperWeight, 0.0);
    const SOLGasGiant::FBandBlend north = SOLGasGiant::BandBlendAt(0.5 * UE_DOUBLE_PI, count, 0.25);
    TestEqual(TEXT("North pole: lower 14"), north.Lower, 14);
    TestEqual(TEXT("North pole: upper 15"), north.Upper, 15);
    TestEqual(TEXT("North pole: weight 1"), north.UpperWeight, 1.0);

    // Inside a gap: sample alone below 0.25, cross-fade between, the other sample alone above 0.75
    for (int32 lower = 0; lower < count - 1; ++lower)
    {
        const SOLGasGiant::FBandBlend nearLower = SOLGasGiant::BandBlendAt(GasGiantGapLatitude(lower, 0.2, count),
            count, 0.25);
        TestEqual(TEXT("w 0.2: lower index"), nearLower.Lower, lower);
        TestEqual(TEXT("w 0.2: upper index"), nearLower.Upper, lower + 1);
        TestEqual(TEXT("w 0.2: weight 0"), nearLower.UpperWeight, 0.0);
        TestEqual(TEXT("w 0.5: weight 0.5"), SOLGasGiant::BandBlendAt(GasGiantGapLatitude(lower, 0.5, count), count,
            0.25).UpperWeight, 0.5, 1.0e-9);
        TestEqual(TEXT("w 0.8: weight 1"), SOLGasGiant::BandBlendAt(GasGiantGapLatitude(lower, 0.8, count), count,
            0.25).UpperWeight, 1.0);
    }

    // Weight is non-decreasing across a gap and continuous across a sample (weight 1 below = weight 0 above)
    double previous = -1.0;
    for (int32 step = 0; step <= 100; ++step)
    {
        const double weight = SOLGasGiant::BandBlendAt(GasGiantGapLatitude(6, step / 100.0 * 0.999999, count), count,
            0.25).UpperWeight;
        TestTrue(TEXT("Weight non-decreasing in the gap"), weight >= previous);
        previous = weight;
    }
    const double sampleLat = GasGiantGapLatitude(7, 0.0, count);
    const SOLGasGiant::FBandBlend below = SOLGasGiant::BandBlendAt(sampleLat - 1.0e-9, count, 0.25);
    const SOLGasGiant::FBandBlend above = SOLGasGiant::BandBlendAt(sampleLat + 1.0e-9, count, 0.25);
    TestTrue(TEXT("Just below a sample: that sample (upper, weight 1)"), below.Upper == 7 && below.UpperWeight == 1.0);
    TestTrue(TEXT("Just above a sample: that sample (lower, weight 0)"), above.Lower == 7 && above.UpperWeight == 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantBandBlendDegenerateTest, "SOLTest.GasGiantMath.BandBlendDegenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BandBlendAt handles few samples, NaN latitudes and out-of-range blend widths
bool FSOLGasGiantBandBlendDegenerateTest::RunTest(const FString& /*parameters*/)
{
    for (const int32 count : { -3, 0, 1 })
    {
        const SOLGasGiant::FBandBlend blend = SOLGasGiant::BandBlendAt(0.3, count, 0.25);
        TestTrue(TEXT("count < 2: all zero"), blend.Lower == 0 && blend.Upper == 0 && blend.UpperWeight == 0.0);
    }
    const SOLGasGiant::FBandBlend nan = SOLGasGiant::BandBlendAt(std::numeric_limits<double>::quiet_NaN(), 16, 0.25);
    const SOLGasGiant::FBandBlend equator = SOLGasGiant::BandBlendAt(0.0, 16, 0.25);
    TestTrue(TEXT("NaN latitude = equator"), nan.Lower == equator.Lower && nan.UpperWeight == equator.UpperWeight);
    const SOLGasGiant::FBandBlend beyond = SOLGasGiant::BandBlendAt(5.0, 16, 0.25);
    TestTrue(TEXT("Latitude clamped to the north pole"), beyond.Lower == 14 && beyond.UpperWeight == 1.0);

    // Width 0 (or negative / NaN) is a step at the middle of the gap; widths above 0.5 clamp to 0.5
    TestEqual(TEXT("h 0: w 0.49 -> 0"), SOLGasGiant::BandBlendAt(GasGiantGapLatitude(3, 0.49, 16), 16, 0.0)
        .UpperWeight, 0.0);
    TestEqual(TEXT("h 0: w 0.51 -> 1"), SOLGasGiant::BandBlendAt(GasGiantGapLatitude(3, 0.51, 16), 16, 0.0)
        .UpperWeight, 1.0);
    TestEqual(TEXT("h NaN = step"), SOLGasGiant::BandBlendAt(GasGiantGapLatitude(3, 0.51, 16), 16,
        std::numeric_limits<double>::quiet_NaN()).UpperWeight, 1.0);
    TestEqual(TEXT("h 2 = h 0.5: w 0.25 -> smoothstep(0.25)"), SOLGasGiant::BandBlendAt(GasGiantGapLatitude(3, 0.25,
        16), 16, 2.0).UpperWeight, 0.15625, 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantBandBlendWrapSafeTest, "SOLTest.GasGiantMath.BandBlendWrapSafeOverTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A field advected through the wrap-safe lookup changes smoothly in time at every latitude, including when
// neighbouring band phases drift more than half a turn apart (where the 14c shortest-arc interpolation jumped)
bool FSOLGasGiantBandBlendWrapSafeTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 count = 16;
    constexpr double eq = GASGIANT_JUPITER_EQ_DRIFT_RAD_PER_S;
    constexpr double polar = -0.3e-6;
    constexpr double factor = 200.0;
    constexpr double stepS = 5.0;
    constexpr int32 steps = 20000;   // 1e5 s: mid-latitude neighbour phase gaps pass half a turn (~4.4e4 s each)

    float previous[count] = {};
    float current[count] = {};
    SOLGasGiant::ComputeBandPhases(GASGIANT_SIM_2027_S, eq, polar, factor, MakeArrayView(previous));
    double maxBlendedStep = 0.0;
    double maxShortestArcStep = 0.0;
    for (int32 step = 1; step <= steps; ++step)
    {
        SOLGasGiant::ComputeBandPhases(GASGIANT_SIM_2027_S + step * stepS, eq, polar, factor, MakeArrayView(current));
        for (int32 latStep = 1; latStep < 60; ++latStep)
        {
            const double lat = -0.5 * UE_DOUBLE_PI + UE_DOUBLE_PI * latStep / 60.0;
            for (const double lon : { 0.0, 1.0, 2.5, 4.0 })
            {
                maxBlendedStep = FMath::Max(maxBlendedStep, std::abs(GasGiantBlendedField(lat, lon, current, count) -
                    GasGiantBlendedField(lat, lon, previous, count)));
                maxShortestArcStep = FMath::Max(maxShortestArcStep, std::abs(GasGiantShortestArcField(lat, lon,
                    current, count) - GasGiantShortestArcField(lat, lon, previous, count)));
            }
        }
        FMemory::Memcpy(previous, current, sizeof(current));
    }

    // Per 5 s step the fastest band moves 5 * 1.4e-6 * 200 rad = 1.4e-3 rad, so a smooth field changes < 2e-3
    TestTrue(FString::Printf(TEXT("Wrap-safe field changes smoothly (max step %.6f)"), maxBlendedStep),
        maxBlendedStep < 2.0e-3);
    TestTrue(FString::Printf(TEXT("The shortest-arc interpolation does jump in the same window (max step %.4f)"),
        maxShortestArcStep), maxShortestArcStep > 0.1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantHazeFadeTest, "SOLTest.GasGiantMath.HazeFadeForNearBlend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The haze fades out exactly as the near layer fades in
bool FSOLGasGiantHazeFadeTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Blend 0 -> haze 1"), SOLGasGiant::HazeFadeForNearBlend(0.0), 1.0);
    TestEqual(TEXT("Blend 1 -> haze 0"), SOLGasGiant::HazeFadeForNearBlend(1.0), 0.0);
    TestEqual(TEXT("Blend 0.25 -> haze 0.75"), SOLGasGiant::HazeFadeForNearBlend(0.25), 0.75);
    TestEqual(TEXT("Blend > 1 clamps"), SOLGasGiant::HazeFadeForNearBlend(3.0), 0.0);
    TestEqual(TEXT("Blend < 0 clamps"), SOLGasGiant::HazeFadeForNearBlend(-1.0), 1.0);
    TestEqual(TEXT("NaN blend -> haze 1"), SOLGasGiant::HazeFadeForNearBlend(std::numeric_limits<double>::quiet_NaN()),
        1.0);
    TestEqual(TEXT("Shipped blend width"), SOL::GAS_GIANT_BAND_BLEND_HALF_WIDTH, 0.25);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantCappedDriftTest, "SOLTest.GasGiantMath.CappedDriftStepSeconds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The drift clock follows sim time up to the warp cap and runs at the cap's speed above it (SDD 14 Amendment 4)
bool FSOLGasGiantCappedDriftTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double dt = 1.0 / 60.0;
    TestEqual(TEXT("1x passes through"), SOLGasGiant::CappedDriftStepSeconds(dt, dt, 1000.0), dt);
    TestEqual(TEXT("At the cap passes through"), SOLGasGiant::CappedDriftStepSeconds(1000.0 * dt, dt, 1000.0),
        1000.0 * dt);
    TestNearlyEqual(TEXT("10000x runs at 1000x"), SOLGasGiant::CappedDriftStepSeconds(10000.0 * dt, dt, 1000.0),
        1000.0 * dt, 1.0e-12);
    TestNearlyEqual(TEXT("Negative steps are capped symmetrically"),
        SOLGasGiant::CappedDriftStepSeconds(-10000.0 * dt, dt, 1000.0), -1000.0 * dt, 1.0e-12);
    TestEqual(TEXT("Paused (no sim step) stays frozen"), SOLGasGiant::CappedDriftStepSeconds(0.0, dt, 1000.0), 0.0);
    TestEqual(TEXT("Zero real step caps to zero"), SOLGasGiant::CappedDriftStepSeconds(5.0, 0.0, 1000.0), 0.0);
    TestEqual(TEXT("Negative real step -> 0"), SOLGasGiant::CappedDriftStepSeconds(5.0, -dt, 1000.0), 0.0);
    TestEqual(TEXT("NaN sim step -> 0"), SOLGasGiant::CappedDriftStepSeconds(nan, dt, 1000.0), 0.0);
    TestEqual(TEXT("NaN real step -> 0"), SOLGasGiant::CappedDriftStepSeconds(5.0, nan, 1000.0), 0.0);
    TestEqual(TEXT("No cap for maxTimeWarp <= 0"), SOLGasGiant::CappedDriftStepSeconds(500.0, dt, 0.0), 500.0);
    TestEqual(TEXT("No cap for NaN maxTimeWarp"), SOLGasGiant::CappedDriftStepSeconds(500.0, dt, nan), 500.0);
    TestEqual(TEXT("Shipped cap"), SOL::GAS_GIANT_BAND_MAX_TIME_WARP, 1000.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantViewConeTest, "SOLTest.GasGiantMath.IsSphereInViewCone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Spheres ahead, at the cone edge, behind and around the camera are classified conservatively
bool FSOLGasGiantViewConeTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d origin = FVector3d::ZeroVector;
    const FVector3d forward(1.0, 0.0, 0.0);
    const double halfAngle = FMath::DegreesToRadians(45.0);
    TestTrue(TEXT("Straight ahead"), SOLGasGiant::IsSphereInViewCone(origin, forward, halfAngle,
        FVector3d(100.0, 0.0, 0.0), 1.0));
    TestFalse(TEXT("Behind"), SOLGasGiant::IsSphereInViewCone(origin, forward, halfAngle,
        FVector3d(-100.0, 0.0, 0.0), 1.0));
    TestFalse(TEXT("60 deg off-axis, small"), SOLGasGiant::IsSphereInViewCone(origin, forward, halfAngle,
        FVector3d(50.0, 86.6, 0.0), 1.0));
    TestTrue(TEXT("60 deg off-axis but its angular radius reaches the cone"), SOLGasGiant::IsSphereInViewCone(origin,
        forward, halfAngle, FVector3d(50.0, 86.6, 0.0), 40.0));
    TestTrue(TEXT("Camera inside the sphere"), SOLGasGiant::IsSphereInViewCone(origin, forward, halfAngle,
        FVector3d(-5.0, 0.0, 0.0), 10.0));
    TestTrue(TEXT("Unnormalized forward works"), SOLGasGiant::IsSphereInViewCone(origin, forward * 7.0, halfAngle,
        FVector3d(100.0, 10.0, 0.0), 1.0));
    TestTrue(TEXT("Zero forward counts as visible"), SOLGasGiant::IsSphereInViewCone(origin, FVector3d::ZeroVector,
        halfAngle, FVector3d(-100.0, 0.0, 0.0), 1.0));
    TestTrue(TEXT("NaN counts as visible"), SOLGasGiant::IsSphereInViewCone(origin, forward,
        std::numeric_limits<double>::quiet_NaN(), FVector3d(-100.0, 0.0, 0.0), 1.0));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
