/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLSpaceDustMath.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #43 space dust math (Docs/SDDs/43-space-dust-speed-cues.md sections 5.1, 5.2 and 7), written
// against the Visuals/SOLSpaceDustMath.h contract before the production code existed
namespace
{
    constexpr double DUST_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double DUST_INF = std::numeric_limits<double>::infinity();
    constexpr double DUST_DBL_MAX = std::numeric_limits<double>::max();
    constexpr double DUST_TOL = 1e-9;
    constexpr double DUST_FLOAT_TOL = 1e-4;
    constexpr double DUST_POSITION_TOL_CM = 0.05;
    constexpr int32 DUST_SEED = 4343;

    //////////////////////////////////////////////////////////////////////////
    // Returns the standard smoothstep 3t^2 - 2t^3 of x between e0 and e1, computed independently of the production code
    double DustSmoothStepRef(const double e0, const double e1, const double x)
    {
        const double t = FMath::Clamp((x - e0) / (e1 - e0), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the apparent speed A(v) of SDD 43 section 5.1 for a finite speed, computed independently
    double DustApparentSpeedRef(const double v, const FSOLSpaceDustParams& p)
    {
        if (v <= p.DeadZoneMps)
        {
            return 0.0;
        }
        return p.ApparentMaxMps * (1.0 - FMath::Exp(-(v - p.DeadZoneMps) / p.ApparentMaxMps));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a uniform double in [lo, hi) drawn from the stream
    double DustRand(FRandomStream& stream, const double lo, const double hi)
    {
        return lo + (hi - lo) * static_cast<double>(stream.GetFraction());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance between two values on a circle of circumference box (handles the wrap seam)
    double DustCircularDiff(const double a, const double b, const double box)
    {
        const double d = FMath::Fmod(FMath::Abs(a - b), box);
        return FMath::Min(d, box - d);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a double vector against an expected one per component
    void DustExpectVec(FAutomationTestBase& test, const FString& what, const FVector3d& actual,
        const FVector3d& expected, const double tol)
    {
        test.TestEqual(what + TEXT(" x"), actual.X, expected.X, tol);
        test.TestEqual(what + TEXT(" y"), actual.Y, expected.Y, tol);
        test.TestEqual(what + TEXT(" z"), actual.Z, expected.Z, tol);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that every component of a double vector is in [0, box)
    void DustExpectInBox(FAutomationTestBase& test, const FString& what, const FVector3d& v, const double box)
    {
        test.TestTrue(what + TEXT(" x in [0, box)"), v.X >= 0.0 && v.X < box);
        test.TestTrue(what + TEXT(" y in [0, box)"), v.Y >= 0.0 && v.Y < box);
        test.TestTrue(what + TEXT(" z in [0, box)"), v.Z >= 0.0 && v.Z < box);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a gate where every condition says "run" (ship, setting on, cvar on, warp 1, no jump, no map, unpaused)
    FSOLSpaceDustGate DustGoodGate()
    {
        FSOLSpaceDustGate gate;
        gate.bHasShip = true;
        gate.bSettingEnabled = true;
        gate.bCvarEnabled = true;
        gate.TimeWarpFactor = 1.0;
        gate.bJumpWarping = false;
        gate.bMapOpen = false;
        gate.bPaused = false;
        return gate;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathDefaultsTest, "SOLTest.SpaceDustMath.DefaultsMatchConstants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The SOL::SpaceDust constants have the approved starting values and a default FSOLSpaceDustParams uses them
bool FSOLSpaceDustMathDefaultsTest::RunTest(const FString& /*parameters*/)
{
    // Pin the approved values of SDD 43 sections 5.1 and 5.2
    TestEqual(TEXT("DEAD_ZONE_MPS"), static_cast<double>(SOL::SpaceDust::DEAD_ZONE_MPS), 0.5, DUST_TOL);
    TestEqual(TEXT("FULL_OPACITY_MPS"), static_cast<double>(SOL::SpaceDust::FULL_OPACITY_MPS), 5.0, DUST_TOL);
    TestEqual(TEXT("APPARENT_MAX_MPS"), static_cast<double>(SOL::SpaceDust::APPARENT_MAX_MPS), 150.0, DUST_TOL);
    TestEqual(TEXT("STREAK_START_MPS"), static_cast<double>(SOL::SpaceDust::STREAK_START_MPS), 10.0, DUST_TOL);
    TestEqual(TEXT("STREAK_TIME_S"), static_cast<double>(SOL::SpaceDust::STREAK_TIME_S), 0.12, 1e-6);
    TestEqual(TEXT("STREAK_MAX_CM"), static_cast<double>(SOL::SpaceDust::STREAK_MAX_CM), 2000.0, DUST_TOL);
    TestEqual(TEXT("STREAK_MIN_ALPHA"), static_cast<double>(SOL::SpaceDust::STREAK_MIN_ALPHA), 0.35, 1e-6);
    TestEqual(TEXT("POINT_SIZE_CM"), static_cast<double>(SOL::SpaceDust::POINT_SIZE_CM), 60.0, DUST_TOL);
    TestEqual(TEXT("POINT_SIZE_RANDOM_MIN"), static_cast<double>(SOL::SpaceDust::POINT_SIZE_RANDOM_MIN), 0.6, 1e-6);
    TestEqual(TEXT("POINT_SIZE_RANDOM_MAX"), static_cast<double>(SOL::SpaceDust::POINT_SIZE_RANDOM_MAX), 1.4, 1e-6);
    TestEqual(TEXT("SMOOTHING_TAU_S"), static_cast<double>(SOL::SpaceDust::SMOOTHING_TAU_S), 0.15, 1e-6);
    TestEqual(TEXT("OPACITY_EPSILON"), static_cast<double>(SOL::SpaceDust::OPACITY_EPSILON), 1.0e-3, 1e-9);
    TestEqual(TEXT("MAX_TIME_WARP"), static_cast<double>(SOL::SpaceDust::MAX_TIME_WARP), 1.0, DUST_TOL);
    TestEqual(TEXT("INTENSITY"), static_cast<double>(SOL::SpaceDust::INTENSITY), 2.0, 1e-6);
    TestEqual(TEXT("PARTICLE_COUNT"), static_cast<int32>(SOL::SpaceDust::PARTICLE_COUNT), 4096);
    TestEqual(TEXT("PARTICLE_COUNT_LOW"), static_cast<int32>(SOL::SpaceDust::PARTICLE_COUNT_LOW), 1024);
    TestEqual(TEXT("PARTICLE_COUNT_MEDIUM"), static_cast<int32>(SOL::SpaceDust::PARTICLE_COUNT_MEDIUM), 2048);
    TestEqual(TEXT("BOX_SIZE_CM"), static_cast<double>(SOL::SpaceDust::BOX_SIZE_CM), 40000.0, DUST_TOL);
    TestEqual(TEXT("RADIAL_FADE_START_FRACTION"), static_cast<double>(SOL::SpaceDust::RADIAL_FADE_START_FRACTION),
        0.7, 1e-6);
    TestEqual(TEXT("NEAR_FADE_START_CM"), static_cast<double>(SOL::SpaceDust::NEAR_FADE_START_CM), 300.0, DUST_TOL);
    TestEqual(TEXT("NEAR_FADE_END_CM"), static_cast<double>(SOL::SpaceDust::NEAR_FADE_END_CM), 1500.0, DUST_TOL);

    // A default-constructed params struct carries exactly the constants
    const FSOLSpaceDustParams p;
    TestEqual(TEXT("DeadZoneMps"), p.DeadZoneMps, static_cast<double>(SOL::SpaceDust::DEAD_ZONE_MPS));
    TestEqual(TEXT("FullOpacityMps"), p.FullOpacityMps, static_cast<double>(SOL::SpaceDust::FULL_OPACITY_MPS));
    TestEqual(TEXT("ApparentMaxMps"), p.ApparentMaxMps, static_cast<double>(SOL::SpaceDust::APPARENT_MAX_MPS));
    TestEqual(TEXT("StreakStartMps"), p.StreakStartMps, static_cast<double>(SOL::SpaceDust::STREAK_START_MPS));
    TestEqual(TEXT("StreakTimeS"), p.StreakTimeS, static_cast<double>(SOL::SpaceDust::STREAK_TIME_S));
    TestEqual(TEXT("StreakMaxCm"), p.StreakMaxCm, static_cast<double>(SOL::SpaceDust::STREAK_MAX_CM));
    TestEqual(TEXT("StreakMinAlpha"), p.StreakMinAlpha, static_cast<double>(SOL::SpaceDust::STREAK_MIN_ALPHA));
    TestEqual(TEXT("PointSizeCm"), p.PointSizeCm, static_cast<double>(SOL::SpaceDust::POINT_SIZE_CM));
    TestEqual(TEXT("BoxSizeCm"), p.BoxSizeCm, static_cast<double>(SOL::SpaceDust::BOX_SIZE_CM));
    TestEqual(TEXT("RadialFadeStartFraction"), p.RadialFadeStartFraction,
        static_cast<double>(SOL::SpaceDust::RADIAL_FADE_START_FRACTION));
    TestEqual(TEXT("NearFadeStartCm"), p.NearFadeStartCm, static_cast<double>(SOL::SpaceDust::NEAR_FADE_START_CM));
    TestEqual(TEXT("NearFadeEndCm"), p.NearFadeEndCm, static_cast<double>(SOL::SpaceDust::NEAR_FADE_END_CM));
    TestEqual(TEXT("SmoothingTauS"), p.SmoothingTauS, static_cast<double>(SOL::SpaceDust::SMOOTHING_TAU_S));
    TestEqual(TEXT("MaxTimeWarp"), p.MaxTimeWarp, static_cast<double>(SOL::SpaceDust::MAX_TIME_WARP));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathParticleCountTest, "SOLTest.SpaceDustMath.ParticleCountForQuality",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Effects quality 0/1/2/3 maps to 1024/2048/4096/4096, with negative clamped to Low and above 3 clamped to Epic
bool FSOLSpaceDustMathParticleCountTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Low (0)"), SOLSpaceDust::ParticleCountForQuality(0), 1024);
    TestEqual(TEXT("Medium (1)"), SOLSpaceDust::ParticleCountForQuality(1), 2048);
    TestEqual(TEXT("High (2)"), SOLSpaceDust::ParticleCountForQuality(2), 4096);
    TestEqual(TEXT("Epic (3)"), SOLSpaceDust::ParticleCountForQuality(3), 4096);
    TestEqual(TEXT("-1 -> Low"), SOLSpaceDust::ParticleCountForQuality(-1), 1024);
    TestEqual(TEXT("-100 -> Low"), SOLSpaceDust::ParticleCountForQuality(-100), 1024);
    TestEqual(TEXT("INT_MIN -> Low"), SOLSpaceDust::ParticleCountForQuality(TNumericLimits<int32>::Min()), 1024);
    TestEqual(TEXT("4 -> Epic"), SOLSpaceDust::ParticleCountForQuality(4), 4096);
    TestEqual(TEXT("100 -> Epic"), SOLSpaceDust::ParticleCountForQuality(100), 4096);
    TestEqual(TEXT("INT_MAX -> Epic"), SOLSpaceDust::ParticleCountForQuality(TNumericLimits<int32>::Max()), 4096);

    // The mapping reads the named constants
    TestEqual(TEXT("Low uses PARTICLE_COUNT_LOW"), SOLSpaceDust::ParticleCountForQuality(0),
        static_cast<int32>(SOL::SpaceDust::PARTICLE_COUNT_LOW));
    TestEqual(TEXT("Medium uses PARTICLE_COUNT_MEDIUM"), SOLSpaceDust::ParticleCountForQuality(1),
        static_cast<int32>(SOL::SpaceDust::PARTICLE_COUNT_MEDIUM));
    TestEqual(TEXT("High uses PARTICLE_COUNT"), SOLSpaceDust::ParticleCountForQuality(2),
        static_cast<int32>(SOL::SpaceDust::PARTICLE_COUNT));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathApparentSpeedDeadZoneTest,
    "SOLTest.SpaceDustMath.ApparentSpeedDeadZoneAndInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The apparent speed is 0 at or below the dead zone, for negative speeds and for non-finite input
bool FSOLSpaceDustMathApparentSpeedDeadZoneTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const double v0 = p.DeadZoneMps;
    TestEqual(TEXT("0 -> 0"), SOLSpaceDust::ApparentSpeedMps(0.0, p), 0.0);
    TestEqual(TEXT("v0 -> 0"), SOLSpaceDust::ApparentSpeedMps(v0, p), 0.0);
    TestEqual(TEXT("0.5 v0 -> 0"), SOLSpaceDust::ApparentSpeedMps(0.5 * v0, p), 0.0);
    TestEqual(TEXT("-5 -> 0"), SOLSpaceDust::ApparentSpeedMps(-5.0, p), 0.0);
    TestEqual(TEXT("NaN -> 0"), SOLSpaceDust::ApparentSpeedMps(DUST_NAN, p), 0.0);
    TestEqual(TEXT("+Inf -> 0"), SOLSpaceDust::ApparentSpeedMps(DUST_INF, p), 0.0);
    TestEqual(TEXT("-Inf -> 0"), SOLSpaceDust::ApparentSpeedMps(-DUST_INF, p), 0.0);

    // Custom params move the dead zone with them
    FSOLSpaceDustParams custom;
    custom.DeadZoneMps = 3.0;
    custom.ApparentMaxMps = 50.0;
    TestEqual(TEXT("custom: 3 -> 0"), SOLSpaceDust::ApparentSpeedMps(3.0, custom), 0.0);
    TestEqual(TEXT("custom: 2 -> 0"), SOLSpaceDust::ApparentSpeedMps(2.0, custom), 0.0);
    TestEqual(TEXT("custom: 13 -> formula"), SOLSpaceDust::ApparentSpeedMps(13.0, custom),
        50.0 * (1.0 - FMath::Exp(-10.0 / 50.0)), DUST_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathApparentSpeedShapeTest, "SOLTest.SpaceDustMath.ApparentSpeedShape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Above the dead zone the apparent speed follows Amax (1 - exp(-(v - v0) / Amax)): slope ~1 near v0, saturating
// below Amax
bool FSOLSpaceDustMathApparentSpeedShapeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const double v0 = p.DeadZoneMps;
    const double aMax = p.ApparentMaxMps;

    // Slope about 1 just above the dead zone, and continuous at v0
    TestEqual(TEXT("v0 + 1 within 1% of 1"), SOLSpaceDust::ApparentSpeedMps(v0 + 1.0, p), 1.0, 0.01);
    TestEqual(TEXT("v0 + 1e-6 -> ~1e-6"), SOLSpaceDust::ApparentSpeedMps(v0 + 1e-6, p), 1e-6, 1e-9);

    // The formula at a few points
    for (const double v : { 1.0, 10.0, 100.0, 1000.0 })
    {
        TestEqual(FString::Printf(TEXT("formula at %g"), v), SOLSpaceDust::ApparentSpeedMps(v, p),
            DustApparentSpeedRef(v, p), DUST_TOL);
    }

    // Relativistic speeds read as "fast" but never reach Amax
    const double nearC = SOLSpaceDust::ApparentSpeedMps(1e8, p);
    TestTrue(TEXT("1e8 >= 0.99 Amax"), nearC >= 0.99 * aMax);
    TestTrue(TEXT("1e8 < Amax"), nearC < aMax);
    const double huge = SOLSpaceDust::ApparentSpeedMps(DUST_DBL_MAX, p);
    TestTrue(TEXT("DBL_MAX >= 0.99 Amax"), huge >= 0.99 * aMax);
    TestTrue(TEXT("DBL_MAX < Amax"), huge < aMax);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathApparentSpeedSweepTest,
    "SOLTest.SpaceDustMath.ApparentSpeedMonotonicSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A sweep from 0 to 1e9 m/s never decreases and stays in [0, Amax)
bool FSOLSpaceDustMathApparentSpeedSweepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const double aMax = p.ApparentMaxMps;

    // Build the sweep: a fine linear run through the dead zone, then a geometric run up to 1e9
    TArray<double> speeds;
    for (int32 i = 0; i <= 2000; ++i)
    {
        speeds.Add(i * 0.005);
    }
    for (double v = 10.0; v <= 1e9; v *= 1.01)
    {
        speeds.Add(v);
    }
    speeds.Add(1e9);

    // Walk the sweep checking order and range
    double previous = SOLSpaceDust::ApparentSpeedMps(speeds[0], p);
    bool bMonotonic = true;
    bool bInRange = previous >= 0.0 && previous < aMax;
    for (int32 i = 1; i < speeds.Num(); ++i)
    {
        const double a = SOLSpaceDust::ApparentSpeedMps(speeds[i], p);
        if (a < previous)
        {
            bMonotonic = false;
            AddError(FString::Printf(TEXT("decreases at v = %.17g: %.17g < %.17g"), speeds[i], a, previous));
            break;
        }
        bInRange = bInRange && a >= 0.0 && a < aMax;
        previous = a;
    }
    TestTrue(TEXT("sweep never decreases"), bMonotonic);
    TestTrue(TEXT("sweep stays in [0, Amax)"), bInRange);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathOpacityValuesTest, "SOLTest.SpaceDustMath.OpacityValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Opacity is smoothstep(v0, v1, v): 0 at or below v0, 1 from v1 up (even at 3e8), 0.5 at the midpoint, 0 for NaN
bool FSOLSpaceDustMathOpacityValuesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const double v0 = p.DeadZoneMps;
    const double v1 = p.FullOpacityMps;
    TestEqual(TEXT("0 -> 0"), SOLSpaceDust::Opacity(0.0, p), 0.0);
    TestEqual(TEXT("v0 -> 0"), SOLSpaceDust::Opacity(v0, p), 0.0);
    TestEqual(TEXT("-5 -> 0"), SOLSpaceDust::Opacity(-5.0, p), 0.0);
    TestEqual(TEXT("v1 -> 1"), SOLSpaceDust::Opacity(v1, p), 1.0, DUST_TOL);
    TestEqual(TEXT("10 v1 -> 1"), SOLSpaceDust::Opacity(10.0 * v1, p), 1.0, DUST_TOL);
    TestEqual(TEXT("3e8 -> 1"), SOLSpaceDust::Opacity(3e8, p), 1.0, DUST_TOL);
    TestEqual(TEXT("midpoint -> 0.5"), SOLSpaceDust::Opacity(0.5 * (v0 + v1), p), 0.5, DUST_TOL);
    TestEqual(TEXT("quarter -> smoothstep(0.25)"), SOLSpaceDust::Opacity(v0 + 0.25 * (v1 - v0), p), 0.15625,
        DUST_TOL);
    TestEqual(TEXT("NaN -> 0"), SOLSpaceDust::Opacity(DUST_NAN, p), 0.0);
    TestEqual(TEXT("+Inf -> 0 (non-finite)"), SOLSpaceDust::Opacity(DUST_INF, p), 0.0);
    TestEqual(TEXT("-Inf -> 0"), SOLSpaceDust::Opacity(-DUST_INF, p), 0.0);

    // Custom params: v0 = 1, v1 = 3
    FSOLSpaceDustParams custom;
    custom.DeadZoneMps = 1.0;
    custom.FullOpacityMps = 3.0;
    TestEqual(TEXT("custom midpoint -> 0.5"), SOLSpaceDust::Opacity(2.0, custom), 0.5, DUST_TOL);
    TestEqual(TEXT("custom 1.5 -> smoothstep"), SOLSpaceDust::Opacity(1.5, custom),
        DustSmoothStepRef(1.0, 3.0, 1.5), DUST_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathOpacitySweepTest, "SOLTest.SpaceDustMath.OpacityMonotonicSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A sweep of speeds never decreases the opacity, keeps it in [0, 1] and matches the smoothstep reference
bool FSOLSpaceDustMathOpacitySweepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;

    // Build the sweep: linear through the ramp, then geometric up to 3e8
    TArray<double> speeds;
    for (int32 i = 0; i <= 2000; ++i)
    {
        speeds.Add(i * 0.005);
    }
    for (double v = 10.0; v <= 3e8; v *= 1.05)
    {
        speeds.Add(v);
    }

    // Walk the sweep checking order, range and the reference
    double previous = SOLSpaceDust::Opacity(speeds[0], p);
    bool bMonotonic = true;
    bool bInRange = true;
    bool bMatchesRef = true;
    for (int32 i = 0; i < speeds.Num(); ++i)
    {
        const double a = SOLSpaceDust::Opacity(speeds[i], p);
        bMonotonic = bMonotonic && a >= previous;
        bInRange = bInRange && a >= 0.0 && a <= 1.0;
        bMatchesRef = bMatchesRef
            && FMath::Abs(a - DustSmoothStepRef(p.DeadZoneMps, p.FullOpacityMps, speeds[i])) <= DUST_TOL;
        previous = a;
    }
    TestTrue(TEXT("sweep never decreases"), bMonotonic);
    TestTrue(TEXT("sweep stays in [0, 1]"), bInRange);
    TestTrue(TEXT("sweep matches smoothstep(v0, v1, v)"), bMatchesRef);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathOpacityDegenerateTest,
    "SOLTest.SpaceDustMath.OpacityDegenerateParams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With v1 <= v0 the opacity is a step at v0: 0 at or below it, 1 above it
bool FSOLSpaceDustMathOpacityDegenerateTest::RunTest(const FString& /*parameters*/)
{
    // Equal edges
    FSOLSpaceDustParams equal;
    equal.DeadZoneMps = 2.0;
    equal.FullOpacityMps = 2.0;
    TestEqual(TEXT("v1 == v0: below -> 0"), SOLSpaceDust::Opacity(1.9, equal), 0.0);
    TestEqual(TEXT("v1 == v0: at v0 -> 0"), SOLSpaceDust::Opacity(2.0, equal), 0.0);
    TestEqual(TEXT("v1 == v0: just above -> 1"), SOLSpaceDust::Opacity(2.0001, equal), 1.0);
    TestEqual(TEXT("v1 == v0: far above -> 1"), SOLSpaceDust::Opacity(1000.0, equal), 1.0);
    TestEqual(TEXT("v1 == v0: NaN -> 0"), SOLSpaceDust::Opacity(DUST_NAN, equal), 0.0);

    // Inverted edges
    FSOLSpaceDustParams inverted;
    inverted.DeadZoneMps = 3.0;
    inverted.FullOpacityMps = 1.0;
    TestEqual(TEXT("v1 < v0: between v1 and v0 -> 0"), SOLSpaceDust::Opacity(2.0, inverted), 0.0);
    TestEqual(TEXT("v1 < v0: at v0 -> 0"), SOLSpaceDust::Opacity(3.0, inverted), 0.0);
    TestEqual(TEXT("v1 < v0: above v0 -> 1"), SOLSpaceDust::Opacity(4.0, inverted), 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathApparentVelocityTest,
    "SOLTest.SpaceDustMath.ApparentVelocityDirectionAndLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The apparent velocity points along vRel with length ApparentSpeedMps(|vRel|) x 100 cm/s
bool FSOLSpaceDustMathApparentVelocityTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;

    // Along +X
    const FVector3d alongX = SOLSpaceDust::ApparentVelocityCmPerS(FVector3d(10.0, 0.0, 0.0), p);
    DustExpectVec(*this, TEXT("(+10, 0, 0)"), alongX,
        FVector3d(SOLSpaceDust::ApparentSpeedMps(10.0, p) * 100.0, 0.0, 0.0), 1e-7);
    TestEqual(TEXT("(+10, 0, 0) length"), alongX.Size(), DustApparentSpeedRef(10.0, p) * 100.0, 1e-7);

    // Along -Y
    const FVector3d alongNegY = SOLSpaceDust::ApparentVelocityCmPerS(FVector3d(0.0, -10.0, 0.0), p);
    DustExpectVec(*this, TEXT("(0, -10, 0)"), alongNegY,
        FVector3d(0.0, -DustApparentSpeedRef(10.0, p) * 100.0, 0.0), 1e-7);

    // A diagonal keeps its direction (|(3, -4, 12)| = 13)
    const FVector3d diagonalIn(3.0, -4.0, 12.0);
    const FVector3d diagonal = SOLSpaceDust::ApparentVelocityCmPerS(diagonalIn, p);
    TestEqual(TEXT("diagonal direction"), diagonal.GetSafeNormal().Dot(diagonalIn.GetSafeNormal()), 1.0, 1e-9);
    TestEqual(TEXT("diagonal length"), diagonal.Size(), DustApparentSpeedRef(13.0, p) * 100.0, 1e-7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathApparentVelocityInvalidTest,
    "SOLTest.SpaceDustMath.ApparentVelocityDeadZoneAndInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The apparent velocity is the zero vector in the dead zone and for any non-finite component
bool FSOLSpaceDustMathApparentVelocityInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    DustExpectVec(*this, TEXT("zero"), SOLSpaceDust::ApparentVelocityCmPerS(FVector3d::ZeroVector, p),
        FVector3d::ZeroVector, 0.0);
    DustExpectVec(*this, TEXT("(0.1, 0, 0)"), SOLSpaceDust::ApparentVelocityCmPerS(FVector3d(0.1, 0.0, 0.0), p),
        FVector3d::ZeroVector, 0.0);
    DustExpectVec(*this, TEXT("|v| < v0 on a diagonal"),
        SOLSpaceDust::ApparentVelocityCmPerS(FVector3d(0.0, 0.3, 0.3), p),
        FVector3d::ZeroVector, 0.0);
    DustExpectVec(*this, TEXT("(NaN, 0, 0)"), SOLSpaceDust::ApparentVelocityCmPerS(FVector3d(DUST_NAN, 0.0, 0.0), p),
        FVector3d::ZeroVector, 0.0);
    DustExpectVec(*this, TEXT("(100, NaN, 0)"),
        SOLSpaceDust::ApparentVelocityCmPerS(FVector3d(100.0, DUST_NAN, 0.0), p), FVector3d::ZeroVector, 0.0);
    DustExpectVec(*this, TEXT("(0, 0, +Inf)"), SOLSpaceDust::ApparentVelocityCmPerS(FVector3d(0.0, 0.0, DUST_INF), p),
        FVector3d::ZeroVector, 0.0);
    DustExpectVec(*this, TEXT("(-Inf, 5, 5)"),
        SOLSpaceDust::ApparentVelocityCmPerS(FVector3d(-DUST_INF, 5.0, 5.0), p), FVector3d::ZeroVector, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathApparentVelocitySweepTest,
    "SOLTest.SpaceDustMath.ApparentVelocityBoundedSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// On a random sweep of directions and magnitudes up to 1e9 m/s the length is always below Amax x 100 and the
// direction follows vRel
bool FSOLSpaceDustMathApparentVelocitySweepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const double maxLength = p.ApparentMaxMps * 100.0;
    FRandomStream stream(DUST_SEED);
    bool bBounded = true;
    bool bAligned = true;

    // Random directions at log-uniform magnitudes from 1 m/s to 1e9 m/s
    for (int32 i = 0; i < 2000; ++i)
    {
        const FVector3d dir = FVector3d(stream.VRand()).GetSafeNormal();
        const double magnitude = FMath::Pow(10.0, DustRand(stream, 0.0, 9.0));
        const FVector3d vRel = dir * magnitude;
        const FVector3d apparent = SOLSpaceDust::ApparentVelocityCmPerS(vRel, p);
        bBounded = bBounded && apparent.Size() < maxLength;
        bAligned = bAligned && apparent.GetSafeNormal().Dot(dir) > 1.0 - 1e-6;
    }

    // Extreme axis-aligned and diagonal speeds
    for (const FVector3d& vRel : { FVector3d(1e8, 1e8, 1e8), FVector3d(-1e9, 0.0, 0.0), FVector3d(0.0, 3e8, -3e8) })
    {
        bBounded = bBounded && SOLSpaceDust::ApparentVelocityCmPerS(vRel, p).Size() < maxLength;
    }
    TestTrue(TEXT("length always < Amax x 100"), bBounded);
    TestTrue(TEXT("direction follows vRel"), bAligned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathStreakLengthTest, "SOLTest.SpaceDustMath.StreakLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Streak length is clamp(P + max(0, A - As) x 100 x Ts, P, Smax), with P for non-finite or non-positive input
bool FSOLSpaceDustMathStreakLengthTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const double pointCm = p.PointSizeCm;
    TestEqual(TEXT("0 -> P"), SOLSpaceDust::StreakLengthCm(0.0, p), pointCm, DUST_TOL);
    TestEqual(TEXT("-5 -> P"), SOLSpaceDust::StreakLengthCm(-5.0, p), pointCm, DUST_TOL);
    TestEqual(TEXT("As -> P"), SOLSpaceDust::StreakLengthCm(p.StreakStartMps, p), pointCm, DUST_TOL);
    TestEqual(TEXT("0.5 As -> P"), SOLSpaceDust::StreakLengthCm(0.5 * p.StreakStartMps, p), pointCm, DUST_TOL);
    TestEqual(TEXT("As + 10 -> P + 120"), SOLSpaceDust::StreakLengthCm(p.StreakStartMps + 10.0, p), pointCm + 120.0,
        1e-6);
    TestEqual(TEXT("As + 10 -> 180 cm"), SOLSpaceDust::StreakLengthCm(p.StreakStartMps + 10.0, p), 180.0, 1e-6);
    TestEqual(TEXT("Amax -> 1740 cm"), SOLSpaceDust::StreakLengthCm(p.ApparentMaxMps, p), 1740.0, 1e-6);
    TestEqual(TEXT("huge -> Smax"), SOLSpaceDust::StreakLengthCm(1e6, p), p.StreakMaxCm, DUST_TOL);
    TestEqual(TEXT("DBL_MAX -> Smax"), SOLSpaceDust::StreakLengthCm(DUST_DBL_MAX, p), p.StreakMaxCm, DUST_TOL);
    TestEqual(TEXT("NaN -> P"), SOLSpaceDust::StreakLengthCm(DUST_NAN, p), pointCm, DUST_TOL);
    TestEqual(TEXT("+Inf -> P"), SOLSpaceDust::StreakLengthCm(DUST_INF, p), pointCm, DUST_TOL);
    TestEqual(TEXT("-Inf -> P"), SOLSpaceDust::StreakLengthCm(-DUST_INF, p), pointCm, DUST_TOL);

    // Monotonic sweep that stays in [P, Smax]
    double previous = SOLSpaceDust::StreakLengthCm(0.0, p);
    bool bMonotonic = true;
    bool bInRange = true;
    for (int32 i = 1; i <= 20000; ++i)
    {
        const double s = SOLSpaceDust::StreakLengthCm(i * 0.05, p);
        bMonotonic = bMonotonic && s >= previous;
        bInRange = bInRange && s >= pointCm && s <= p.StreakMaxCm;
        previous = s;
    }
    TestTrue(TEXT("sweep never decreases"), bMonotonic);
    TestTrue(TEXT("sweep stays in [P, Smax]"), bInRange);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathStreakAlphaTest, "SOLTest.SpaceDustMath.StreakAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Streak alpha is max(Amin, sqrt(P / S)), 1 for S <= P or non-finite, always in [Amin, 1]
bool FSOLSpaceDustMathStreakAlphaTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const double pointCm = p.PointSizeCm;
    TestEqual(TEXT("P -> 1"), SOLSpaceDust::StreakAlpha(pointCm, p), 1.0, DUST_TOL);
    TestEqual(TEXT("4P -> 0.5"), SOLSpaceDust::StreakAlpha(4.0 * pointCm, p), 0.5, DUST_TOL);
    TestEqual(TEXT("Smax -> max(Amin, sqrt(P / Smax))"), SOLSpaceDust::StreakAlpha(p.StreakMaxCm, p),
        FMath::Max(p.StreakMinAlpha, FMath::Sqrt(pointCm / p.StreakMaxCm)), DUST_TOL);
    TestEqual(TEXT("Smax -> Amin (defaults)"), SOLSpaceDust::StreakAlpha(p.StreakMaxCm, p), p.StreakMinAlpha,
        DUST_TOL);
    TestEqual(TEXT("0.5 P -> 1"), SOLSpaceDust::StreakAlpha(0.5 * pointCm, p), 1.0, DUST_TOL);
    TestEqual(TEXT("0 -> 1"), SOLSpaceDust::StreakAlpha(0.0, p), 1.0, DUST_TOL);
    TestEqual(TEXT("-10 -> 1"), SOLSpaceDust::StreakAlpha(-10.0, p), 1.0, DUST_TOL);
    TestEqual(TEXT("NaN -> 1"), SOLSpaceDust::StreakAlpha(DUST_NAN, p), 1.0, DUST_TOL);
    TestEqual(TEXT("+Inf -> 1"), SOLSpaceDust::StreakAlpha(DUST_INF, p), 1.0, DUST_TOL);

    // Custom params where the floor does not bite at the cap
    FSOLSpaceDustParams custom;
    custom.PointSizeCm = 100.0;
    custom.StreakMinAlpha = 0.1;
    TestEqual(TEXT("custom 400 -> 0.5"), SOLSpaceDust::StreakAlpha(400.0, custom), 0.5, DUST_TOL);
    TestEqual(TEXT("custom 2500 -> 0.2"), SOLSpaceDust::StreakAlpha(2500.0, custom), 0.2, DUST_TOL);

    // A sweep never leaves [Amin, 1]
    bool bInRange = true;
    for (int32 i = 0; i <= 10000; ++i)
    {
        const double a = SOLSpaceDust::StreakAlpha(i * 1.0, p);
        bInRange = bInRange && a >= p.StreakMinAlpha && a <= 1.0;
    }
    TestTrue(TEXT("sweep stays in [Amin, 1]"), bInRange);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathSmoothScalarTest, "SOLTest.SpaceDustMath.SmoothScalar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SmoothScalar moves current towards target by (1 - exp(-dt / tau)), with the invalid-input rules of SDD 43 section 7
bool FSOLSpaceDustMathSmoothScalarTest::RunTest(const FString& /*parameters*/)
{
    const double tau = SOL::SpaceDust::SMOOTHING_TAU_S;

    // One step of dt = tau goes 63.2% of the way
    const double oneTau = SOLSpaceDust::SmoothScalar(2.0, 12.0, tau, tau);
    TestEqual(TEXT("dt = tau formula"), oneTau, 2.0 + 10.0 * (1.0 - FMath::Exp(-1.0)), DUST_TOL);
    TestEqual(TEXT("dt = tau is ~63.2% of the way"), (oneTau - 2.0) / 10.0, 0.632, 1e-3);
    TestEqual(TEXT("half tau formula"), SOLSpaceDust::SmoothScalar(1.0, 0.0, 0.5 * tau, tau),
        1.0 - (1.0 - FMath::Exp(-0.5)), DUST_TOL);

    // Degenerate dt and tau
    TestEqual(TEXT("dt = 0 -> current"), SOLSpaceDust::SmoothScalar(3.0, 7.0, 0.0, tau), 3.0);
    TestEqual(TEXT("dt < 0 -> current"), SOLSpaceDust::SmoothScalar(3.0, 7.0, -0.1, tau), 3.0);
    TestEqual(TEXT("dt NaN -> current"), SOLSpaceDust::SmoothScalar(3.0, 7.0, DUST_NAN, tau), 3.0);
    TestEqual(TEXT("dt +Inf -> current"), SOLSpaceDust::SmoothScalar(3.0, 7.0, DUST_INF, tau), 3.0);
    TestEqual(TEXT("tau = 0 -> target"), SOLSpaceDust::SmoothScalar(3.0, 7.0, 0.016, 0.0), 7.0);
    TestEqual(TEXT("tau < 0 -> target"), SOLSpaceDust::SmoothScalar(3.0, 7.0, 0.016, -1.0), 7.0);
    TestEqual(TEXT("target NaN -> current"), SOLSpaceDust::SmoothScalar(3.0, DUST_NAN, 0.016, tau), 3.0);
    TestEqual(TEXT("target +Inf -> current"), SOLSpaceDust::SmoothScalar(3.0, DUST_INF, 0.016, tau), 3.0);

    // A step response from 0 to 1 converges below 1e-3 after 7 tau (70 steps of tau / 10)
    double value = 0.0;
    for (int32 i = 0; i < 70; ++i)
    {
        value = SOLSpaceDust::SmoothScalar(value, 1.0, tau / 10.0, tau);
    }
    TestTrue(TEXT("step response within 1e-3 after 7 tau"), FMath::Abs(1.0 - value) < 1e-3);
    TestTrue(TEXT("step response does not overshoot"), value <= 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathSmoothVectorTest, "SOLTest.SpaceDustMath.SmoothVector",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SmoothVector applies the scalar rule per component and leaves the vector unchanged for a NaN target
bool FSOLSpaceDustMathSmoothVectorTest::RunTest(const FString& /*parameters*/)
{
    const double tau = SOL::SpaceDust::SMOOTHING_TAU_S;
    const FVector3d current(0.0, 10.0, -4.0);
    const FVector3d target(1.0, -10.0, 4.0);
    const double k = 1.0 - FMath::Exp(-1.0);

    // Per-component smoothing at dt = tau
    DustExpectVec(*this, TEXT("dt = tau"), SOLSpaceDust::SmoothVector(current, target, tau, tau),
        current + (target - current) * k, DUST_TOL);

    // Degenerate dt and tau
    DustExpectVec(*this, TEXT("dt = 0 -> current"), SOLSpaceDust::SmoothVector(current, target, 0.0, tau), current,
        0.0);
    DustExpectVec(*this, TEXT("dt NaN -> current"), SOLSpaceDust::SmoothVector(current, target, DUST_NAN, tau),
        current, 0.0);
    DustExpectVec(*this, TEXT("tau = 0 -> target"), SOLSpaceDust::SmoothVector(current, target, 0.016, 0.0), target,
        0.0);

    // A NaN target component leaves the whole vector unchanged
    DustExpectVec(*this, TEXT("NaN target -> unchanged"),
        SOLSpaceDust::SmoothVector(current, FVector3d(1.0, DUST_NAN, 4.0), 0.016, tau), current, 0.0);
    DustExpectVec(*this, TEXT("Inf target -> unchanged"),
        SOLSpaceDust::SmoothVector(current, FVector3d(DUST_INF, 0.0, 0.0), 0.016, tau), current, 0.0);

    // A step response converges below 1e-3 per component after 7 tau
    FVector3d value = FVector3d::ZeroVector;
    const FVector3d goal(1.0, -1.0, 0.5);
    for (int32 i = 0; i < 70; ++i)
    {
        value = SOLSpaceDust::SmoothVector(value, goal, tau / 10.0, tau);
    }
    DustExpectVec(*this, TEXT("step response after 7 tau"), value, goal, 1e-3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathAdvanceOffsetBoxTest, "SOLTest.SpaceDustMath.AdvanceOffsetStaysInBox",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Advancing the offset by L/3 a frame for 1000 frames stays in [0, L), and a negative velocity wraps up to near L
bool FSOLSpaceDustMathAdvanceOffsetBoxTest::RunTest(const FString& /*parameters*/)
{
    const double box = SOL::SpaceDust::BOX_SIZE_CM;
    const double dt = 1.0 / 60.0;

    // 1000 frames moving L/3 per frame (mixed signs and a non-dividing step on Z)
    const FVector3d velocity(box / 3.0 / dt, -box / 3.0 / dt, box / 7.0 / dt);
    FVector3d offset = FVector3d::ZeroVector;
    bool bInBox = true;
    for (int32 i = 0; i < 1000; ++i)
    {
        offset = SOLSpaceDust::AdvanceOffsetCm(offset, velocity, dt, box);
        bInBox = bInBox && offset.X >= 0.0 && offset.X < box && offset.Y >= 0.0 && offset.Y < box && offset.Z >= 0.0
            && offset.Z < box;
    }
    TestTrue(TEXT("1000 frames of L/3 stay in [0, L)"), bInBox);

    // A negative velocity from 0 wraps up to just under L
    const FVector3d wrapped = SOLSpaceDust::AdvanceOffsetCm(FVector3d::ZeroVector, FVector3d(-100.0, 0.0, 0.0), 0.01,
        box);
    DustExpectVec(*this, TEXT("negative velocity wraps to L - 1"), wrapped, FVector3d(box - 1.0, 0.0, 0.0), 1e-6);
    DustExpectInBox(*this, TEXT("negative velocity"), wrapped, box);

    // A plain step inside the box is offset + velocity x dt
    DustExpectVec(*this, TEXT("plain step"),
        SOLSpaceDust::AdvanceOffsetCm(FVector3d(100.0, 200.0, 300.0), FVector3d(1000.0, -2000.0, 0.0), 0.05, box),
        FVector3d(150.0, 100.0, 300.0), 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathAdvanceOffsetReferenceTest,
    "SOLTest.SpaceDustMath.AdvanceOffsetMatchesFmodReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After 1e6 steps the accumulated offset equals an fmod reference of the total travel to 1e-3 cm
bool FSOLSpaceDustMathAdvanceOffsetReferenceTest::RunTest(const FString& /*parameters*/)
{
    const double box = SOL::SpaceDust::BOX_SIZE_CM;
    const double dt = 1.0 / 60.0;
    const FVector3d start(123.456, 39000.0, 7.0);
    const FVector3d velocity(12345.678, -9876.54321, 14999.9265);
    constexpr int32 steps = 1000000;

    // Accumulate through the production function
    FVector3d offset = start;
    for (int32 i = 0; i < steps; ++i)
    {
        offset = SOLSpaceDust::AdvanceOffsetCm(offset, velocity, dt, box);
    }
    DustExpectInBox(*this, TEXT("after 1e6 steps"), offset, box);

    // Reference: total travel wrapped once with fmod (compared on the circle so the seam does not matter)
    const FVector3d step = velocity * dt;
    for (int32 axis = 0; axis < 3; ++axis)
    {
        double reference = FMath::Fmod(start[axis] + step[axis] * steps, box);
        if (reference < 0.0)
        {
            reference += box;
        }
        const double diff = DustCircularDiff(offset[axis], reference, box);
        TestTrue(FString::Printf(TEXT("axis %d matches fmod reference to 1e-3 cm (diff %g)"), axis, diff),
            diff < 1e-3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathAdvanceOffsetInvalidTest,
    "SOLTest.SpaceDustMath.AdvanceOffsetInvalidInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A NaN velocity leaves the offset unchanged; dt <= 0 or non-finite only wraps the offset
bool FSOLSpaceDustMathAdvanceOffsetInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double box = SOL::SpaceDust::BOX_SIZE_CM;
    const FVector3d inside(100.0, 200.0, 300.0);
    const FVector3d velocity(1000.0, 1000.0, 1000.0);

    // A non-finite velocity leaves an in-range offset unchanged
    DustExpectVec(*this, TEXT("NaN velocity -> unchanged"),
        SOLSpaceDust::AdvanceOffsetCm(inside, FVector3d(DUST_NAN, 0.0, 0.0), 0.016, box), inside, 1e-9);
    DustExpectVec(*this, TEXT("Inf velocity -> unchanged"),
        SOLSpaceDust::AdvanceOffsetCm(inside, FVector3d(0.0, DUST_INF, 0.0), 0.016, box), inside, 1e-9);

    // dt <= 0 or non-finite returns the wrapped offset without moving it
    const FVector3d outside(box + 5.0, -5.0, 10.0);
    const FVector3d outsideWrapped(5.0, box - 5.0, 10.0);
    DustExpectVec(*this, TEXT("dt = 0 -> wrap(offset)"), SOLSpaceDust::AdvanceOffsetCm(outside, velocity, 0.0, box),
        outsideWrapped, 1e-6);
    DustExpectVec(*this, TEXT("dt < 0 -> wrap(offset)"), SOLSpaceDust::AdvanceOffsetCm(outside, velocity, -1.0, box),
        outsideWrapped, 1e-6);
    DustExpectVec(*this, TEXT("dt NaN -> wrap(offset)"),
        SOLSpaceDust::AdvanceOffsetCm(outside, velocity, DUST_NAN, box), outsideWrapped, 1e-6);
    DustExpectVec(*this, TEXT("dt Inf -> wrap(offset)"),
        SOLSpaceDust::AdvanceOffsetCm(outside, velocity, DUST_INF, box), outsideWrapped, 1e-6);
    DustExpectVec(*this, TEXT("dt = 0 in range -> unchanged"), SOLSpaceDust::AdvanceOffsetCm(inside, velocity, 0.0,
        box), inside, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathWrapOffsetTest, "SOLTest.SpaceDustMath.WrapOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// WrapOffsetCm puts each component in [0, box): exact box -> 0, non-finite -> 0, box <= 0 -> zero vector
bool FSOLSpaceDustMathWrapOffsetTest::RunTest(const FString& /*parameters*/)
{
    const double box = SOL::SpaceDust::BOX_SIZE_CM;
    DustExpectVec(*this, TEXT("0 -> 0"), SOLSpaceDust::WrapOffsetCm(FVector3d::ZeroVector, box),
        FVector3d::ZeroVector, 0.0);
    DustExpectVec(*this, TEXT("L -> 0"), SOLSpaceDust::WrapOffsetCm(FVector3d(box, box, box), box),
        FVector3d::ZeroVector, 1e-9);
    DustExpectVec(*this, TEXT("-1 -> L - 1"), SOLSpaceDust::WrapOffsetCm(FVector3d(-1.0, -1.0, -1.0), box),
        FVector3d(box - 1.0, box - 1.0, box - 1.0), 1e-9);
    DustExpectVec(*this, TEXT("5.5 L -> 0.5 L"), SOLSpaceDust::WrapOffsetCm(FVector3d(5.5 * box, 5.5 * box, 5.5 * box),
        box), FVector3d(0.5 * box, 0.5 * box, 0.5 * box), 1e-6);
    DustExpectVec(*this, TEXT("mixed components"), SOLSpaceDust::WrapOffsetCm(FVector3d(10.0, -2.0 * box + 3.0,
        box + 7.0), box), FVector3d(10.0, 3.0, 7.0), 1e-6);

    // Huge, tiny-negative and -L values stay in range
    DustExpectInBox(*this, TEXT("-1e15"), SOLSpaceDust::WrapOffsetCm(FVector3d(-1e15, 1e15, -1e15), box), box);
    DustExpectInBox(*this, TEXT("tiny negative"), SOLSpaceDust::WrapOffsetCm(FVector3d(-1e-12, -1e-12, -1e-12), box),
        box);
    DustExpectInBox(*this, TEXT("-L"), SOLSpaceDust::WrapOffsetCm(FVector3d(-box, -box, -box), box), box);

    // Non-finite components become 0 without touching the others
    DustExpectVec(*this, TEXT("NaN -> 0"), SOLSpaceDust::WrapOffsetCm(FVector3d(DUST_NAN, 5.0, 6.0), box),
        FVector3d(0.0, 5.0, 6.0), 1e-9);
    DustExpectVec(*this, TEXT("Inf -> 0"), SOLSpaceDust::WrapOffsetCm(FVector3d(1.0, DUST_INF, -DUST_INF), box),
        FVector3d(1.0, 0.0, 0.0), 1e-9);

    // A non-positive box gives the zero vector
    DustExpectVec(*this, TEXT("box 0 -> zero"), SOLSpaceDust::WrapOffsetCm(FVector3d(10.0, 20.0, 30.0), 0.0),
        FVector3d::ZeroVector, 0.0);
    DustExpectVec(*this, TEXT("box < 0 -> zero"), SOLSpaceDust::WrapOffsetCm(FVector3d(10.0, 20.0, 30.0), -5.0),
        FVector3d::ZeroVector, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathWrappedLocalPositionTest,
    "SOLTest.SpaceDustMath.WrappedLocalPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shader formula frac((anchor - offset) / box) x box - box / 2 centres at L/2, seams at -L/2 and stays in range
bool FSOLSpaceDustMathWrappedLocalPositionTest::RunTest(const FString& /*parameters*/)
{
    const float box = static_cast<float>(SOL::SpaceDust::BOX_SIZE_CM);
    const float half = 0.5f * box;
    const double tol = DUST_POSITION_TOL_CM;

    // anchor - offset = L/2 -> 0
    const FVector3f centre = SOLSpaceDust::WrappedLocalPositionCm(FVector3f(30000.0f), FVector3f(10000.0f), box);
    TestEqual(TEXT("anchor - offset = L/2 -> 0 (x)"), static_cast<double>(centre.X), 0.0, tol);
    TestEqual(TEXT("anchor - offset = L/2 -> 0 (y)"), static_cast<double>(centre.Y), 0.0, tol);
    TestEqual(TEXT("anchor - offset = L/2 -> 0 (z)"), static_cast<double>(centre.Z), 0.0, tol);

    // anchor == offset -> -L/2
    const FVector3f seam = SOLSpaceDust::WrappedLocalPositionCm(FVector3f(12345.0f), FVector3f(12345.0f), box);
    TestEqual(TEXT("equal -> -L/2"), static_cast<double>(seam.X), static_cast<double>(-half), tol);

    // Increasing the offset by d moves the position by -d away from the seam
    const float d = 100.0f;
    const FVector3f before = SOLSpaceDust::WrappedLocalPositionCm(FVector3f(25000.0f), FVector3f(10000.0f), box);
    const FVector3f after = SOLSpaceDust::WrappedLocalPositionCm(FVector3f(25000.0f), FVector3f(10000.0f + d), box);
    TestEqual(TEXT("before (diff 15000) -> -5000"), static_cast<double>(before.X), -5000.0, tol);
    TestEqual(TEXT("offset + d moves by -d"), static_cast<double>(after.X - before.X), static_cast<double>(-d), tol);

    // Past the seam the position wraps from -L/2 to just under +L/2
    const FVector3f wrapped = SOLSpaceDust::WrappedLocalPositionCm(FVector3f(12345.0f), FVector3f(12346.0f), box);
    TestEqual(TEXT("wraps to L/2 - 1"), static_cast<double>(wrapped.X), static_cast<double>(half - 1.0f), tol);
    TestTrue(TEXT("wrapped is below +L/2"), wrapped.X < half);

    // Seam-adjacent and multiple-of-L/2 differences stay in [-L/2, L/2)
    for (const float diff : { half, -half, box, -box, 3.0f * half, -3.0f * half, -0.001f, 0.0f, box - 0.001f })
    {
        const FVector3f r = SOLSpaceDust::WrappedLocalPositionCm(FVector3f(diff + 20000.0f), FVector3f(20000.0f), box);
        TestTrue(FString::Printf(TEXT("anchor - offset = %g in [-L/2, L/2)"), diff), r.X >= -half && r.X < half);
    }
    for (const float anchor : { half, -half, 0.0f, box })
    {
        const FVector3f r = SOLSpaceDust::WrappedLocalPositionCm(FVector3f(anchor), FVector3f(0.0f), box);
        TestTrue(FString::Printf(TEXT("anchor %g, offset 0 in [-L/2, L/2)"), anchor), r.X >= -half && r.X < half);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathWrappedLocalSweepTest,
    "SOLTest.SpaceDustMath.WrappedLocalPositionSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// On a random sweep of anchors and offsets in [0, L) the position stays in [-L/2, L/2) and matches a double reference
bool FSOLSpaceDustMathWrappedLocalSweepTest::RunTest(const FString& /*parameters*/)
{
    const double box = SOL::SpaceDust::BOX_SIZE_CM;
    const float boxF = static_cast<float>(box);
    const float half = 0.5f * boxF;
    FRandomStream stream(DUST_SEED);
    bool bInRange = true;
    bool bMatchesRef = true;

    // Each sample compares all three components with the double-precision shader formula
    for (int32 i = 0; i < 5000; ++i)
    {
        const FVector3f anchor(static_cast<float>(DustRand(stream, 0.0, box)),
            static_cast<float>(DustRand(stream, 0.0, box)), static_cast<float>(DustRand(stream, 0.0, box)));
        const FVector3f offset(static_cast<float>(DustRand(stream, 0.0, box)),
            static_cast<float>(DustRand(stream, 0.0, box)), static_cast<float>(DustRand(stream, 0.0, box)));
        const FVector3f r = SOLSpaceDust::WrappedLocalPositionCm(anchor, offset, boxF);
        for (int32 axis = 0; axis < 3; ++axis)
        {
            const double x = (static_cast<double>(anchor[axis]) - static_cast<double>(offset[axis])) / box;
            const double reference = (x - FMath::FloorToDouble(x)) * box - 0.5 * box;
            bInRange = bInRange && r[axis] >= -half && r[axis] < half;
            bMatchesRef = bMatchesRef
                && DustCircularDiff(static_cast<double>(r[axis]), reference, box) < DUST_POSITION_TOL_CM;
        }
    }
    TestTrue(TEXT("sweep stays in [-L/2, L/2)"), bInRange);
    TestTrue(TEXT("sweep matches the shader formula"), bMatchesRef);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathDustFadeTest, "SOLTest.SpaceDustMath.DustFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DustFade is (1 - smoothstep(f R, R, |local|)) x smoothstep(nearStart, nearEnd, |local - camera|) with R = box / 2
bool FSOLSpaceDustMathDustFadeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const float r = static_cast<float>(0.5 * p.BoxSizeCm);
    const float coreR = static_cast<float>(p.RadialFadeStartFraction * 0.5 * p.BoxSizeCm);
    const FVector3f farCamera(0.0f, 0.0f, 10000.0f);
    const FVector3f originCamera(0.0f, 0.0f, 0.0f);

    // Radial fade (camera far from the particle)
    TestEqual(TEXT("centre, camera far -> 1"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(0.0f), farCamera, p)), 1.0, DUST_FLOAT_TOL);
    TestEqual(TEXT("|local| = R -> 0"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(r, 0.0f, 0.0f), originCamera, p)), 0.0, DUST_FLOAT_TOL);
    TestEqual(TEXT("|local| > R -> 0"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(r, r, r), originCamera, p)), 0.0, DUST_FLOAT_TOL);
    TestEqual(TEXT("|local| = f R -> 1"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(0.0f, coreR, 0.0f), originCamera, p)), 1.0,
        DUST_FLOAT_TOL);
    TestEqual(TEXT("radial midpoint -> 0.5"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(0.5f * (coreR + r), 0.0f, 0.0f), originCamera, p)), 0.5,
        DUST_FLOAT_TOL);

    // Near fade around the camera
    TestEqual(TEXT("on the camera -> 0"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(100.0f, 200.0f, 300.0f), FVector3f(100.0f, 200.0f,
            300.0f), p)), 0.0, DUST_FLOAT_TOL);
    TestEqual(TEXT("within nearStart of the camera -> 0"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(0.0f), FVector3f(250.0f, 0.0f, 0.0f), p)), 0.0,
        DUST_FLOAT_TOL);
    TestEqual(TEXT("15 m from the camera inside the core -> 1"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(0.0f), FVector3f(1500.0f, 0.0f, 0.0f), p)), 1.0,
        DUST_FLOAT_TOL);
    TestEqual(TEXT("near midpoint -> 0.5"),
        static_cast<double>(SOLSpaceDust::DustFade(FVector3f(0.0f), FVector3f(0.0f, 900.0f, 0.0f), p)), 0.5,
        DUST_FLOAT_TOL);

    // Both fades multiply
    const FVector3f local(0.5f * (coreR + r), 0.0f, 0.0f);
    TestEqual(TEXT("radial 0.5 x near 0.5 -> 0.25"),
        static_cast<double>(SOLSpaceDust::DustFade(local, local + FVector3f(0.0f, 900.0f, 0.0f), p)), 0.25,
        DUST_FLOAT_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathDustFadeSweepTest, "SOLTest.SpaceDustMath.DustFadeSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// On a random sweep of particle and camera positions DustFade stays in [0, 1] and matches the reference formula
bool FSOLSpaceDustMathDustFadeSweepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    const double r = 0.5 * p.BoxSizeCm;
    FRandomStream stream(DUST_SEED);
    bool bInRange = true;
    bool bMatchesRef = true;

    // Particles across (and past) the box, cameras within 30 m of the centre
    for (int32 i = 0; i < 5000; ++i)
    {
        const FVector3f local(static_cast<float>(DustRand(stream, -1.2 * r, 1.2 * r)),
            static_cast<float>(DustRand(stream, -1.2 * r, 1.2 * r)), static_cast<float>(DustRand(stream, -1.2 * r,
                1.2 * r)));
        const FVector3f camera(static_cast<float>(DustRand(stream, -3000.0, 3000.0)),
            static_cast<float>(DustRand(stream, -3000.0, 3000.0)), static_cast<float>(DustRand(stream, -3000.0,
                3000.0)));
        const double fade = static_cast<double>(SOLSpaceDust::DustFade(local, camera, p));
        const double radial = 1.0 - DustSmoothStepRef(p.RadialFadeStartFraction * r, r,
            static_cast<double>(local.Size()));
        const double nearFade = DustSmoothStepRef(p.NearFadeStartCm, p.NearFadeEndCm,
            static_cast<double>((local - camera).Size()));
        bInRange = bInRange && fade >= 0.0 && fade <= 1.0;
        bMatchesRef = bMatchesRef && FMath::Abs(fade - radial * nearFade) <= 1e-3;
    }
    TestTrue(TEXT("sweep stays in [0, 1]"), bInRange);
    TestTrue(TEXT("sweep matches the reference formula"), bMatchesRef);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathShouldRunTest, "SOLTest.SpaceDustMath.ShouldRunGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ShouldRun is true only when every gate condition says run; flipping any one of them hides the dust
bool FSOLSpaceDustMathShouldRunTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;
    TestTrue(TEXT("all good -> true"), SOLSpaceDust::ShouldRun(DustGoodGate(), p));

    // Flip each condition on its own
    {
        FSOLSpaceDustGate gate = DustGoodGate();
        gate.bHasShip = false;
        TestFalse(TEXT("no ship -> false"), SOLSpaceDust::ShouldRun(gate, p));
    }
    {
        FSOLSpaceDustGate gate = DustGoodGate();
        gate.bSettingEnabled = false;
        TestFalse(TEXT("setting disabled -> false"), SOLSpaceDust::ShouldRun(gate, p));
    }
    {
        FSOLSpaceDustGate gate = DustGoodGate();
        gate.bCvarEnabled = false;
        TestFalse(TEXT("cvar off -> false"), SOLSpaceDust::ShouldRun(gate, p));
    }
    {
        FSOLSpaceDustGate gate = DustGoodGate();
        gate.TimeWarpFactor = 10.0;
        TestFalse(TEXT("warp 10 -> false"), SOLSpaceDust::ShouldRun(gate, p));
    }
    {
        FSOLSpaceDustGate gate = DustGoodGate();
        gate.bJumpWarping = true;
        TestFalse(TEXT("jump warping -> false"), SOLSpaceDust::ShouldRun(gate, p));
    }
    {
        FSOLSpaceDustGate gate = DustGoodGate();
        gate.bMapOpen = true;
        TestFalse(TEXT("map open -> false"), SOLSpaceDust::ShouldRun(gate, p));
    }
    {
        FSOLSpaceDustGate gate = DustGoodGate();
        gate.bPaused = true;
        TestFalse(TEXT("paused -> false"), SOLSpaceDust::ShouldRun(gate, p));
    }

    // The gate has no master-O input: the seven fields above fully decide the result (the setting enabled with no other
    // blocker runs, whatever the overlay master toggle is), and speed is not part of the gate
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSpaceDustMathShouldRunWarpTest, "SOLTest.SpaceDustMath.ShouldRunTimeWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The time-warp gate runs for finite warp <= MaxTimeWarp (including 0) and hides for larger or non-finite warp
bool FSOLSpaceDustMathShouldRunWarpTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpaceDustParams p;

    // Returns ShouldRun for a good gate with the given warp factor and params
    auto runAtWarp = [](const double warp, const FSOLSpaceDustParams& params)
    {
        FSOLSpaceDustGate gate = DustGoodGate();
        gate.TimeWarpFactor = warp;
        return SOLSpaceDust::ShouldRun(gate, params);
    };

    TestTrue(TEXT("warp 1 -> true"), runAtWarp(1.0, p));
    TestTrue(TEXT("warp 0 (paused clock) -> true"), runAtWarp(0.0, p));
    TestTrue(TEXT("warp 0.5 -> true"), runAtWarp(0.5, p));
    TestFalse(TEXT("warp 1.5 -> false"), runAtWarp(1.5, p));
    TestFalse(TEXT("warp 10 -> false"), runAtWarp(10.0, p));
    TestFalse(TEXT("warp NaN -> false"), runAtWarp(DUST_NAN, p));
    TestFalse(TEXT("warp +Inf -> false"), runAtWarp(DUST_INF, p));

    // The limit comes from the params
    FSOLSpaceDustParams custom;
    custom.MaxTimeWarp = 4.0;
    TestTrue(TEXT("custom max 4: warp 3 -> true"), runAtWarp(3.0, custom));
    TestTrue(TEXT("custom max 4: warp 4 -> true"), runAtWarp(4.0, custom));
    TestFalse(TEXT("custom max 4: warp 5 -> false"), runAtWarp(5.0, custom));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
