/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingDense.h"

#include "Tests/SOLTestHelpers.h"
#include "Universe/SOLRenderPlacement.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Saturn-like GM (m^3/s^2) and a mid-ring plane radius (m), typed independently of production code
    constexpr double RINGDENSE_PLANET_GM = 3.7931187e16;
    constexpr double RINGDENSE_PLANE_RADIUS_M = 1.0e8;

    // Window radius (cm) used by the WindowPhaseCm example cases
    constexpr double RINGDENSE_WINDOW_W_CM = 100.0;

    // Absolute tolerance for WindowPhaseCm at huge positions, per the contract (1e-3 cm)
    constexpr double RINGDENSE_PHASE_HUGE_TOL_CM = 1.0e-3;

    // Tight tolerance for small, exactly-representable arithmetic (a few ULPs at magnitudes up to ~1e3)
    constexpr double RINGDENSE_TIGHT_TOL = 1.0e-9;

    // Tolerance for angles after normalisation into [0, 2*PI) (a handful of ULPs of 2*PI, plus omega*dt rounding)
    constexpr double RINGDENSE_ANGLE_TOL_RAD = 1.0e-9;

    // Fade band used by the ComputeDenseAlpha cases (m)
    constexpr double RINGDENSE_FADE_IN_M = 1000.0;
    constexpr double RINGDENSE_FADE_OUT_M = 5000.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns where a carpet rock with tile anchor anchorCm sits in the ring's co-rotating frame (ring-frame cm) when
    // the camera is at relativeM (ecliptic metres from the planet) and the carpet is spun by spinRad: exactly the chain
    // the host runs (frame position -> mirrored Niagara-local -> window phase -> wrapped local offset -> component
    // rotation EclipticToUnreal(orientation * Rz(spin)) -> back to ecliptic), written independently of the actor
    FVector3d RingDenseCarpetRockRingFrameCm(const FQuat4d& orientation, const double spinRad,
        const FVector3d& relativeM, const FVector3d& anchorCm, const double windowRadiusCm)
    {
        const FVector3d e1 = orientation.RotateVector(FVector3d(1.0, 0.0, 0.0));
        const FVector3d e2 = orientation.RotateVector(FVector3d(0.0, 1.0, 0.0));
        const FVector3d n = orientation.RotateVector(FVector3d(0.0, 0.0, 1.0));
        const FVector3d cameraRingFrameCm = SOLRingDense::RingFramePositionCm(relativeM, e1, e2, n, spinRad);
        const FVector3d local = SOLRingDense::RingFrameToNiagaraLocalCm(cameraRingFrameCm);

        // What the Niagara asset does: phase = tile modulo of the camera, offset = wrap(anchor - phase) into [-W, W)
        const double tileCm = 2.0 * windowRadiusCm;
        const double phaseX = SOLRingDense::WindowPhaseCm(local.X, windowRadiusCm);
        const double phaseY = SOLRingDense::WindowPhaseCm(local.Y, windowRadiusCm);
        const double dx = anchorCm.X - phaseX;
        const double dy = anchorCm.Y - phaseY;
        const FVector3d offsetLocal(dx - tileCm * std::floor((dx + windowRadiusCm) / tileCm),
            dy - tileCm * std::floor((dy + windowRadiusCm) / tileCm), anchorCm.Z - local.Z);

        // Component rotation as the host builds it, applied to the local offset, then back to ecliptic axes
        const FQuat4d spun = orientation * FQuat4d(FVector3d(0.0, 0.0, 1.0), spinRad);
        const FQuat4d unrealRotation = SOLRender::EclipticToUnreal(spun);
        const FVector3d offsetEclipticCm = SOLRender::EclipticToUnreal(unrealRotation.RotateVector(offsetLocal));

        // The rock's own ring-frame position, from its ecliptic position relative to the planet
        const FVector3d rockRelativeM = relativeM + offsetEclipticCm / 100.0;
        return SOLRingDense::RingFramePositionCm(rockRelativeM, e1, e2, n, spinRad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected orbital angular rate sqrt(GM / r^3) for the Saturn-like test plane (rad/s)
    double RingDenseExpectedOmega()
    {
        return std::sqrt(RINGDENSE_PLANET_GM / (RINGDENSE_PLANE_RADIUS_M * RINGDENSE_PLANE_RADIUS_M *
            RINGDENSE_PLANE_RADIUS_M));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the exact mathematical (non-negative) modulo of positionCm by tileCm, computed with std::fmod (exact)
    double RingDenseExactPhase(const double positionCm, const double tileCm)
    {
        double phase = std::fmod(positionCm, tileCm);
        if (phase < 0.0)
        {
            phase += tileCm;
        }
        return phase;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if angleRad lies in the normalised range [0, 2*PI)
    bool RingDenseAngleInRange(const double angleRad)
    {
        return std::isfinite(angleRad) && angleRad >= 0.0 && angleRad < 2.0 * UE_DOUBLE_PI;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the smoothstep reference 3t^2 - 2t^3, written independently of production code
    double RingDenseSmoothstep(const double t)
    {
        return t * t * (3.0 - 2.0 * t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the planar (x, y) radius of a ring-frame position
    double RingDensePlanarRadius(const FVector3d& value)
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseWindowPhaseExamplesTest, "SOLTest.RingDense.WindowPhaseExamples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// WindowPhaseCm is the mathematical modulo by 2W for the documented example values, wrapping negatives upward
bool FSOLRingDenseWindowPhaseExamplesTest::RunTest(const FString& /*parameters*/)
{
    const double w = RINGDENSE_WINDOW_W_CM;
    TestNearlyEqual(TEXT("-10 -> 190"), SOLRingDense::WindowPhaseCm(-10.0, w), 190.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("210 -> 10"), SOLRingDense::WindowPhaseCm(210.0, w), 10.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("0 -> 0"), SOLRingDense::WindowPhaseCm(0.0, w), 0.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("200 -> 0"), SOLRingDense::WindowPhaseCm(200.0, w), 0.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("-200 -> 0"), SOLRingDense::WindowPhaseCm(-200.0, w), 0.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("50 -> 50 (already in range)"), SOLRingDense::WindowPhaseCm(50.0, w), 50.0,
        RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("199.5 -> 199.5 (just below the tile)"), SOLRingDense::WindowPhaseCm(199.5, w), 199.5,
        RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("-199.5 -> 0.5"), SOLRingDense::WindowPhaseCm(-199.5, w), 0.5, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("-410 -> 190 (several tiles below)"), SOLRingDense::WindowPhaseCm(-410.0, w), 190.0,
        RINGDENSE_TIGHT_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseWindowPhaseRangeTest, "SOLTest.RingDense.WindowPhaseRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// WindowPhaseCm always lands in [0, 2W), including tiny negatives whose naive "fmod + 2W" would round up to 2W
bool FSOLRingDenseWindowPhaseRangeTest::RunTest(const FString& /*parameters*/)
{
    const double w = RINGDENSE_WINDOW_W_CM;
    const double tile = 2.0 * w;
    const double positions[] = { -1.0e12, -123456.789, -200.0, -1.0, -1.0e-12, -1.0e-300, 0.0, 1.0e-12, 1.0, 199.999,
        200.0, 123456.789, 1.0e12 };
    for (const double positionCm : positions)
    {
        const double phase = SOLRingDense::WindowPhaseCm(positionCm, w);
        TestTrue(FString::Printf(TEXT("Phase of %g is finite and in [0, 2W)"), positionCm),
            std::isfinite(phase) && phase >= 0.0 && phase < tile);
    }

    // A tiny negative maps to "just below 2W" exactly; in double that is either just under 200 or wraps to 0, both
    // equivalent positions on the tile, but never 2W itself (checked above)
    const double tinyNegPhase = SOLRingDense::WindowPhaseCm(-1.0e-12, w);
    TestTrue(TEXT("Tiny negative is near 0 or near 2W"), tinyNegPhase < 1.0e-6 || tinyNegPhase > tile - 1.0e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseWindowPhaseHugeTest, "SOLTest.RingDense.WindowPhaseHuge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// WindowPhaseCm stays within 1e-3 cm of the exact double modulo for ~1e12 cm positions, both signs
bool FSOLRingDenseWindowPhaseHugeTest::RunTest(const FString& /*parameters*/)
{
    const double windowRadii[] = { RINGDENSE_WINDOW_W_CM, 2500.0, 37.25 };
    const double positions[] = { 1.0e12, -1.0e12, 1.0e12 + 37.5, -1.0e12 - 37.5, 9.87654321e11, -4.4e12,
        SOLTestHelpers::AU_M * 100.0 };
    for (const double w : windowRadii)
    {
        for (const double positionCm : positions)
        {
            const double expected = RingDenseExactPhase(positionCm, 2.0 * w);
            const double actual = SOLRingDense::WindowPhaseCm(positionCm, w);

            // Compare on the circle so a value just below 2W and one just above 0 count as equal
            double diff = std::fabs(actual - expected);
            diff = FMath::Min(diff, 2.0 * w - diff);
            TestTrue(FString::Printf(TEXT("W=%g pos=%.17g: |%.9f - %.9f| on the tile < 1e-3 cm"), w, positionCm,
                actual, expected), diff < RINGDENSE_PHASE_HUGE_TOL_CM);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseWindowPhasePeriodicityTest, "SOLTest.RingDense.WindowPhasePeriodicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Shifting a position by whole tiles (2W * k, positive or negative k) leaves its phase unchanged
bool FSOLRingDenseWindowPhasePeriodicityTest::RunTest(const FString& /*parameters*/)
{
    const double w = RINGDENSE_WINDOW_W_CM;
    const double basePositions[] = { 13.0, 187.25, -42.5 };
    const int32 tileShifts[] = { -7, -1, 1, 3, 1000 };
    for (const double baseCm : basePositions)
    {
        const double basePhase = SOLRingDense::WindowPhaseCm(baseCm, w);
        for (const int32 k : tileShifts)
        {
            const double shiftedPhase = SOLRingDense::WindowPhaseCm(baseCm + 2.0 * w * k, w);
            TestNearlyEqual(FString::Printf(TEXT("Phase(%g + 2W*%d) == Phase(%g)"), baseCm, k, baseCm), shiftedPhase,
                basePhase, RINGDENSE_TIGHT_TOL * 1000.0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseWindowPhaseInvalidRadiusTest, "SOLTest.RingDense.WindowPhaseInvalidRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// WindowPhaseCm returns exactly 0 for a zero or negative window radius (no divide-by-zero, no NaN)
bool FSOLRingDenseWindowPhaseInvalidRadiusTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("W = 0 returns 0"), SOLRingDense::WindowPhaseCm(123.0, 0.0), 0.0);
    TestEqual(TEXT("W = 0 at position 0 returns 0"), SOLRingDense::WindowPhaseCm(0.0, 0.0), 0.0);
    TestEqual(TEXT("W < 0 returns 0"), SOLRingDense::WindowPhaseCm(123.0, -50.0), 0.0);
    TestEqual(TEXT("W < 0 with negative position returns 0"), SOLRingDense::WindowPhaseCm(-123.0, -50.0), 0.0);
    TestEqual(TEXT("W = 0 with huge position returns 0"), SOLRingDense::WindowPhaseCm(1.0e12, 0.0), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseSpinAdvanceTest, "SOLTest.RingDense.SpinAdvance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AdvanceSpinAngle adds sqrt(GM/r^3)*dt for Saturn-like numbers (omega ~1.9477e-4 rad/s)
bool FSOLRingDenseSpinAdvanceTest::RunTest(const FString& /*parameters*/)
{
    const double omega = RingDenseExpectedOmega();
    TestNearlyEqual(TEXT("Reference omega is ~1.9477e-4 rad/s"), omega, 1.9477e-4, 1.0e-7);

    // From zero, 1000 s
    TestNearlyEqual(TEXT("0 + omega*1000"), SOLRingDense::AdvanceSpinAngle(0.0, RINGDENSE_PLANET_GM,
        RINGDENSE_PLANE_RADIUS_M, 1000.0), omega * 1000.0, RINGDENSE_ANGLE_TOL_RAD);

    // From a non-zero start, 1 s
    TestNearlyEqual(TEXT("1.0 + omega*1"), SOLRingDense::AdvanceSpinAngle(1.0, RINGDENSE_PLANET_GM,
        RINGDENSE_PLANE_RADIUS_M, 1.0), 1.0 + omega, RINGDENSE_ANGLE_TOL_RAD);

    // A larger radius spins slower (Kepler), so the same dt advances less
    const double inner = SOLRingDense::AdvanceSpinAngle(0.0, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M, 100.0);
    const double outer = SOLRingDense::AdvanceSpinAngle(0.0, RINGDENSE_PLANET_GM, 2.0 * RINGDENSE_PLANE_RADIUS_M,
        100.0);
    TestTrue(TEXT("Outer plane advances less than the inner plane"), outer < inner);
    TestNearlyEqual(TEXT("Doubling r scales the advance by 2^-1.5"), outer, inner * std::pow(2.0, -1.5),
        RINGDENSE_ANGLE_TOL_RAD);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseSpinWrapTest, "SOLTest.RingDense.SpinWrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AdvanceSpinAngle wraps results past 2*PI back into [0, 2*PI), including advances of many full turns
bool FSOLRingDenseSpinWrapTest::RunTest(const FString& /*parameters*/)
{
    const double omega = RingDenseExpectedOmega();
    const double twoPi = 2.0 * UE_DOUBLE_PI;

    // Crossing 2*PI by 0.01 rad
    const double crossDt = 0.02 / omega;
    const double crossed = SOLRingDense::AdvanceSpinAngle(twoPi - 0.01, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M,
        crossDt);
    TestTrue(TEXT("Crossing result is in [0, 2PI)"), RingDenseAngleInRange(crossed));
    TestNearlyEqual(TEXT("2PI - 0.01 + 0.02 wraps to 0.01"), crossed, 0.01, RINGDENSE_ANGLE_TOL_RAD);

    // 3.25 full orbital periods lands a quarter turn on; the tolerance allows for omega*dt rounding at ~20 rad
    const double period = twoPi / omega;
    const double quarter = SOLRingDense::AdvanceSpinAngle(0.0, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M,
        3.25 * period);
    TestTrue(TEXT("Multi-turn result is in [0, 2PI)"), RingDenseAngleInRange(quarter));
    TestNearlyEqual(TEXT("3.25 periods -> PI/2"), quarter, 0.5 * UE_DOUBLE_PI, 1.0e-8);

    // A very long dt (~30 years) still returns a normalised, finite angle matching the reference wrap
    const double longDt = 1.0e9;
    const double longAngle = SOLRingDense::AdvanceSpinAngle(0.3, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M, longDt);
    const double longExpected = RingDenseExactPhase(0.3 + omega * longDt, twoPi);
    TestTrue(TEXT("Long-dt result is in [0, 2PI)"), RingDenseAngleInRange(longAngle));
    double longDiff = std::fabs(longAngle - longExpected);
    longDiff = FMath::Min(longDiff, twoPi - longDiff);
    TestTrue(TEXT("Long-dt result matches the reference on the circle (1e-6 rad at ~2e5 rad of advance)"),
        longDiff < 1.0e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseSpinZeroAndNegativeDtTest, "SOLTest.RingDense.SpinZeroAndNegativeDt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// dt = 0 only normalises the input angle; negative dt decreases the angle and still normalises into [0, 2*PI)
bool FSOLRingDenseSpinZeroAndNegativeDtTest::RunTest(const FString& /*parameters*/)
{
    const double omega = RingDenseExpectedOmega();
    const double twoPi = 2.0 * UE_DOUBLE_PI;

    // dt = 0
    TestNearlyEqual(TEXT("dt=0 keeps an in-range angle"), SOLRingDense::AdvanceSpinAngle(1.5, RINGDENSE_PLANET_GM,
        RINGDENSE_PLANE_RADIUS_M, 0.0), 1.5, RINGDENSE_ANGLE_TOL_RAD);
    TestNearlyEqual(TEXT("dt=0 normalises 7.0 to 7.0 - 2PI"), SOLRingDense::AdvanceSpinAngle(7.0, RINGDENSE_PLANET_GM,
        RINGDENSE_PLANE_RADIUS_M, 0.0), 7.0 - twoPi, RINGDENSE_ANGLE_TOL_RAD);
    TestNearlyEqual(TEXT("dt=0 normalises -0.5 to 2PI - 0.5"), SOLRingDense::AdvanceSpinAngle(-0.5,
        RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M, 0.0), twoPi - 0.5, RINGDENSE_ANGLE_TOL_RAD);
    TestNearlyEqual(TEXT("dt=0 at 0 stays 0"), SOLRingDense::AdvanceSpinAngle(0.0, RINGDENSE_PLANET_GM,
        RINGDENSE_PLANE_RADIUS_M, 0.0), 0.0, RINGDENSE_ANGLE_TOL_RAD);
    const double atTwoPi = SOLRingDense::AdvanceSpinAngle(twoPi, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M, 0.0);
    TestTrue(TEXT("dt=0 at exactly 2PI is normalised (strictly below 2PI)"), RingDenseAngleInRange(atTwoPi));
    TestTrue(TEXT("dt=0 at exactly 2PI is ~0 on the circle"), atTwoPi < 1.0e-9 || atTwoPi > twoPi - 1.0e-9);

    // Negative dt: decreases without wrapping, then wraps below zero
    TestNearlyEqual(TEXT("1.0 - omega*100"), SOLRingDense::AdvanceSpinAngle(1.0, RINGDENSE_PLANET_GM,
        RINGDENSE_PLANE_RADIUS_M, -100.0), 1.0 - omega * 100.0, RINGDENSE_ANGLE_TOL_RAD);
    const double wrappedNeg = SOLRingDense::AdvanceSpinAngle(0.1, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M,
        -0.2 / omega);
    TestTrue(TEXT("Negative-dt result is in [0, 2PI)"), RingDenseAngleInRange(wrappedNeg));
    TestNearlyEqual(TEXT("0.1 - 0.2 wraps to 2PI - 0.1"), wrappedNeg, twoPi - 0.1, RINGDENSE_ANGLE_TOL_RAD);

    // Forward then backward by the same dt returns to the start (antisymmetry in dt)
    const double forward = SOLRingDense::AdvanceSpinAngle(2.0, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M, 12345.0);
    const double back = SOLRingDense::AdvanceSpinAngle(forward, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M,
        -12345.0);
    TestNearlyEqual(TEXT("Advance +dt then -dt returns to the start"), back, 2.0, RINGDENSE_ANGLE_TOL_RAD);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseSpinInvalidInputsTest, "SOLTest.RingDense.SpinInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A non-positive plane radius or GM leaves the angle unchanged apart from normalisation (no NaN/inf)
bool FSOLRingDenseSpinInvalidInputsTest::RunTest(const FString& /*parameters*/)
{
    const double twoPi = 2.0 * UE_DOUBLE_PI;
    const double dt = 1000.0;

    TestNearlyEqual(TEXT("r = 0 keeps the angle"), SOLRingDense::AdvanceSpinAngle(1.25, RINGDENSE_PLANET_GM, 0.0, dt),
        1.25, RINGDENSE_ANGLE_TOL_RAD);
    TestNearlyEqual(TEXT("r < 0 keeps the angle"), SOLRingDense::AdvanceSpinAngle(1.25, RINGDENSE_PLANET_GM, -1.0e8,
        dt), 1.25, RINGDENSE_ANGLE_TOL_RAD);
    TestNearlyEqual(TEXT("GM = 0 keeps the angle"), SOLRingDense::AdvanceSpinAngle(1.25, 0.0, RINGDENSE_PLANE_RADIUS_M,
        dt), 1.25, RINGDENSE_ANGLE_TOL_RAD);
    TestNearlyEqual(TEXT("GM < 0 keeps the angle"), SOLRingDense::AdvanceSpinAngle(1.25, -RINGDENSE_PLANET_GM,
        RINGDENSE_PLANE_RADIUS_M, dt), 1.25, RINGDENSE_ANGLE_TOL_RAD);

    // Still normalised when the input angle is out of range
    TestNearlyEqual(TEXT("r = 0 normalises 7.0"), SOLRingDense::AdvanceSpinAngle(7.0, RINGDENSE_PLANET_GM, 0.0, dt),
        7.0 - twoPi, RINGDENSE_ANGLE_TOL_RAD);
    TestNearlyEqual(TEXT("GM = 0 normalises -1.0"), SOLRingDense::AdvanceSpinAngle(-1.0, 0.0, RINGDENSE_PLANE_RADIUS_M,
        dt), twoPi - 1.0, RINGDENSE_ANGLE_TOL_RAD);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseAlphaBoundariesTest, "SOLTest.RingDense.AlphaBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ComputeDenseAlpha is 1 at/inside fadeIn (including negative distances), 0 at/beyond fadeOut
bool FSOLRingDenseAlphaBoundariesTest::RunTest(const FString& /*parameters*/)
{
    const double in = RINGDENSE_FADE_IN_M;
    const double out = RINGDENSE_FADE_OUT_M;

    TestEqual(TEXT("Deep inside the ring volume (negative) is 1"), SOLRingDense::ComputeDenseAlpha(-1.0e6, in, out),
        1.0);
    TestEqual(TEXT("Distance 0 is 1"), SOLRingDense::ComputeDenseAlpha(0.0, in, out), 1.0);
    TestEqual(TEXT("Exactly fadeIn is 1"), SOLRingDense::ComputeDenseAlpha(in, in, out), 1.0);
    TestEqual(TEXT("Exactly fadeOut is 0"), SOLRingDense::ComputeDenseAlpha(out, in, out), 0.0);
    TestEqual(TEXT("Beyond fadeOut is 0"), SOLRingDense::ComputeDenseAlpha(out + 1.0, in, out), 0.0);
    TestEqual(TEXT("Far beyond fadeOut is 0"), SOLRingDense::ComputeDenseAlpha(1.0e15, in, out), 0.0);

    // Zero fadeIn: distance 0 is still fully opaque
    TestEqual(TEXT("fadeIn = 0, d = 0 is 1"), SOLRingDense::ComputeDenseAlpha(0.0, 0.0, out), 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseAlphaSmoothstepTest, "SOLTest.RingDense.AlphaSmoothstep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Strictly inside the band the alpha follows 1 - smoothstep(t), strictly between 0 and 1
bool FSOLRingDenseAlphaSmoothstepTest::RunTest(const FString& /*parameters*/)
{
    const double in = RINGDENSE_FADE_IN_M;
    const double out = RINGDENSE_FADE_OUT_M;
    const double band = out - in;

    // Midpoint: smoothstep(0.5) = 0.5, so alpha is 0.5
    TestNearlyEqual(TEXT("Midpoint alpha is 0.5"), SOLRingDense::ComputeDenseAlpha(in + 0.5 * band, in, out), 0.5,
        RINGDENSE_TIGHT_TOL);

    // Quarter points: smoothstep(0.25) = 0.15625, smoothstep(0.75) = 0.84375
    TestNearlyEqual(TEXT("t=0.25 alpha is 0.84375"), SOLRingDense::ComputeDenseAlpha(in + 0.25 * band, in, out),
        0.84375, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("t=0.75 alpha is 0.15625"), SOLRingDense::ComputeDenseAlpha(in + 0.75 * band, in, out),
        0.15625, RINGDENSE_TIGHT_TOL);

    // Symmetry: alpha(t) + alpha(1 - t) == 1 for smoothstep
    const double ts[] = { 0.1, 0.33, 0.47 };
    for (const double t : ts)
    {
        const double a = SOLRingDense::ComputeDenseAlpha(in + t * band, in, out);
        const double b = SOLRingDense::ComputeDenseAlpha(in + (1.0 - t) * band, in, out);
        TestNearlyEqual(FString::Printf(TEXT("alpha(%g) + alpha(1-%g) == 1"), t, t), a + b, 1.0, RINGDENSE_TIGHT_TOL);
        TestNearlyEqual(FString::Printf(TEXT("alpha(%g) matches 1 - smoothstep"), t), a, 1.0 - RingDenseSmoothstep(t),
            RINGDENSE_TIGHT_TOL);
    }

    // Just inside each edge is strictly between 0 and 1
    const double nearIn = SOLRingDense::ComputeDenseAlpha(in + 1.0e-3, in, out);
    const double nearOut = SOLRingDense::ComputeDenseAlpha(out - 1.0e-3, in, out);
    TestTrue(TEXT("Just past fadeIn is in (0, 1)"), nearIn > 0.0 && nearIn < 1.0);
    TestTrue(TEXT("Just before fadeOut is in (0, 1)"), nearOut > 0.0 && nearOut < 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseAlphaMonotonicTest, "SOLTest.RingDense.AlphaMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sweeping the distance from well inside to well beyond the band gives a finite, non-increasing alpha in [0, 1]
bool FSOLRingDenseAlphaMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const double in = RINGDENSE_FADE_IN_M;
    const double out = RINGDENSE_FADE_OUT_M;
    constexpr int32 steps = 400;
    const double startM = -2000.0;
    const double endM = 8000.0;

    double previous = 2.0;
    bool bAllOk = true;
    for (int32 i = 0; i <= steps; ++i)
    {
        const double d = startM + (endM - startM) * static_cast<double>(i) / steps;
        const double alpha = SOLRingDense::ComputeDenseAlpha(d, in, out);
        if (!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0 || alpha > previous)
        {
            AddError(FString::Printf(TEXT("Alpha %.17g at d=%g violates [0,1] / non-increasing (previous %.17g)"),
                alpha, d, previous));
            bAllOk = false;
            break;
        }
        previous = alpha;
    }
    TestTrue(TEXT("Sweep is finite, in [0, 1] and non-increasing"), bAllOk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseAlphaDegenerateBandTest, "SOLTest.RingDense.AlphaDegenerateBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// fadeOut <= fadeIn degrades to a hard step (1 for d <= fadeIn, else 0) with no NaN from a zero/negative band
bool FSOLRingDenseAlphaDegenerateBandTest::RunTest(const FString& /*parameters*/)
{
    // Zero-width band
    TestEqual(TEXT("Equal band, d below is 1"), SOLRingDense::ComputeDenseAlpha(500.0, 1000.0, 1000.0), 1.0);
    TestEqual(TEXT("Equal band, d at fadeIn is 1"), SOLRingDense::ComputeDenseAlpha(1000.0, 1000.0, 1000.0), 1.0);
    TestEqual(TEXT("Equal band, d just above is 0"), SOLRingDense::ComputeDenseAlpha(1000.001, 1000.0, 1000.0), 0.0);

    // Inverted band
    TestEqual(TEXT("Inverted band, d below fadeIn is 1"), SOLRingDense::ComputeDenseAlpha(1500.0, 2000.0, 1000.0),
        1.0);
    TestEqual(TEXT("Inverted band, d at fadeIn is 1"), SOLRingDense::ComputeDenseAlpha(2000.0, 2000.0, 1000.0), 1.0);
    TestEqual(TEXT("Inverted band, d above fadeIn is 0"), SOLRingDense::ComputeDenseAlpha(2500.0, 2000.0, 1000.0),
        0.0);
    TestEqual(TEXT("Inverted band, d between fadeOut and fadeIn is 1"),
        SOLRingDense::ComputeDenseAlpha(1200.0, 2000.0, 1000.0), 1.0);

    // Both zero
    TestEqual(TEXT("Zero band at d = 0 is 1"), SOLRingDense::ComputeDenseAlpha(0.0, 0.0, 0.0), 1.0);
    TestEqual(TEXT("Zero band at d > 0 is 0"), SOLRingDense::ComputeDenseAlpha(1.0, 0.0, 0.0), 0.0);
    TestFalse(TEXT("Zero band never returns NaN"), std::isnan(SOLRingDense::ComputeDenseAlpha(0.0, 0.0, 0.0)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseFramePositionExamplesTest, "SOLTest.RingDense.FramePositionExamples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RingFramePositionCm projects onto (e1, e2, n), rotates the plane by -angle and converts metres to centimetres
bool FSOLRingDenseFramePositionExamplesTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d e1(1.0, 0.0, 0.0);
    const FVector3d e2(0.0, 1.0, 0.0);
    const FVector3d n(0.0, 0.0, 1.0);

    // Identity basis, no spin
    const FVector3d a = SOLRingDense::RingFramePositionCm(FVector3d(1.0, 2.0, 3.0), e1, e2, n, 0.0);
    TestNearlyEqual(TEXT("Angle 0: x = 100"), a.X, 100.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("Angle 0: y = 200"), a.Y, 200.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("Angle 0: z = 300"), a.Z, 300.0, RINGDENSE_TIGHT_TOL);

    // Quarter turn: +X maps to -Y (rotation by -angle)
    const FVector3d b = SOLRingDense::RingFramePositionCm(FVector3d(1.0, 0.0, 0.0), e1, e2, n, 0.5 * UE_DOUBLE_PI);
    TestNearlyEqual(TEXT("Angle PI/2, (1,0,0): x = 0"), b.X, 0.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("Angle PI/2, (1,0,0): y = -100"), b.Y, -100.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("Angle PI/2, (1,0,0): z = 0"), b.Z, 0.0, RINGDENSE_TIGHT_TOL);

    // Quarter turn: +Y maps to +X
    const FVector3d c = SOLRingDense::RingFramePositionCm(FVector3d(0.0, 1.0, 0.0), e1, e2, n, 0.5 * UE_DOUBLE_PI);
    TestNearlyEqual(TEXT("Angle PI/2, (0,1,0): x = 100"), c.X, 100.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("Angle PI/2, (0,1,0): y = 0"), c.Y, 0.0, RINGDENSE_TIGHT_TOL);

    // Half turn negates the planar components and keeps z
    const FVector3d d = SOLRingDense::RingFramePositionCm(FVector3d(1.0, 2.0, 3.0), e1, e2, n, UE_DOUBLE_PI);
    TestNearlyEqual(TEXT("Angle PI: x = -100"), d.X, -100.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("Angle PI: y = -200"), d.Y, -200.0, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("Angle PI: z = 300"), d.Z, 300.0, RINGDENSE_TIGHT_TOL);

    // Zero relative position stays at the origin at any angle
    TestTrue(TEXT("Zero input maps to zero"), SOLTestHelpers::VectorsNear(
        SOLRingDense::RingFramePositionCm(FVector3d::ZeroVector, e1, e2, n, 1.234), FVector3d::ZeroVector, 1.0e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseFramePositionInvariantsTest, "SOLTest.RingDense.FramePositionInvariants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The planar radius and z are invariant under spin; a 2*PI turn is the identity; angle and -angle are mirror images
bool FSOLRingDenseFramePositionInvariantsTest::RunTest(const FString& /*parameters*/)
{
    // Tilted orthonormal basis (45 degrees about +X), and a realistic ring-scale offset (~1e8 m)
    const FVector3d n = FVector3d(0.0, 1.0, 1.0).GetSafeNormal();
    const FVector3d e1(1.0, 0.0, 0.0);
    const FVector3d e2 = FVector3d::CrossProduct(n, e1).GetSafeNormal();
    const FVector3d relativeM(7.0e7, -3.0e7, 2.5e7);

    // Relative tolerance at ~1e10 cm: well above double rounding of a projection and a 2x2 rotation
    const double tolCm = 1.0e-4;

    const FVector3d base = SOLRingDense::RingFramePositionCm(relativeM, e1, e2, n, 0.0);
    TestNearlyEqual(TEXT("Angle 0: x = (rel . e1) * 100"), base.X, relativeM.Dot(e1) * 100.0, tolCm);
    TestNearlyEqual(TEXT("Angle 0: y = (rel . e2) * 100"), base.Y, relativeM.Dot(e2) * 100.0, tolCm);
    TestNearlyEqual(TEXT("Angle 0: z = (rel . n) * 100"), base.Z, relativeM.Dot(n) * 100.0, tolCm);

    const double baseRadius = RingDensePlanarRadius(base);
    const double angles[] = { 0.1, 1.0, 0.5 * UE_DOUBLE_PI, 3.0, 5.9, -2.2 };
    for (const double angle : angles)
    {
        const FVector3d spun = SOLRingDense::RingFramePositionCm(relativeM, e1, e2, n, angle);
        TestNearlyEqual(FString::Printf(TEXT("Planar radius invariant at angle %g"), angle),
            RingDensePlanarRadius(spun), baseRadius, tolCm);
        TestNearlyEqual(FString::Printf(TEXT("z invariant at angle %g"), angle), spun.Z, base.Z, tolCm);

        // Explicit rotation formula x' = x cos + y sin, y' = -x sin + y cos
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        TestNearlyEqual(FString::Printf(TEXT("x' formula at angle %g"), angle), spun.X, base.X * c + base.Y * s,
            tolCm);
        TestNearlyEqual(FString::Printf(TEXT("y' formula at angle %g"), angle), spun.Y, -base.X * s + base.Y * c,
            tolCm);

        // Mirror: rotating by +a and -a gives the same x-projection component pattern (x' symmetric in sin sign)
        const FVector3d mirrored = SOLRingDense::RingFramePositionCm(relativeM, e1, e2, n, -angle);
        TestNearlyEqual(FString::Printf(TEXT("x(a) + x(-a) == 2x cos(a) at angle %g"), angle), spun.X + mirrored.X,
            2.0 * base.X * c, tolCm);
    }

    // A full turn is the identity
    const FVector3d fullTurn = SOLRingDense::RingFramePositionCm(relativeM, e1, e2, n, 2.0 * UE_DOUBLE_PI);
    TestTrue(TEXT("2PI turn equals angle 0"), SOLTestHelpers::VectorsNear(fullTurn, base, 1.0e-12));

    // Linearity in the relative position: doubling the input doubles the output
    const FVector3d doubled = SOLRingDense::RingFramePositionCm(relativeM * 2.0, e1, e2, n, 0.7);
    const FVector3d single = SOLRingDense::RingFramePositionCm(relativeM, e1, e2, n, 0.7);
    TestTrue(TEXT("Output is linear in relativeM"), SOLTestHelpers::VectorsNear(doubled, single * 2.0, 1.0e-12));
    TestTrue(TEXT("Output is finite"), SOLTestHelpers::IsFiniteVector(doubled));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseShowTierTest, "SOLTest.RingDense.ShowTier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ShouldShowDenseTier requires alpha > 0 and warp <= max warp (equal allowed); max warp <= 0 never shows
bool FSOLRingDenseShowTierTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("Alpha 1, warp 1, max 1000 shows"), SOLRingDense::ShouldShowDenseTier(1.0, 1.0, 1000.0));
    TestTrue(TEXT("Tiny positive alpha shows"), SOLRingDense::ShouldShowDenseTier(1.0e-9, 1.0, 1000.0));
    TestTrue(TEXT("Warp exactly at max shows"), SOLRingDense::ShouldShowDenseTier(0.5, 1000.0, 1000.0));
    TestFalse(TEXT("Warp just above max hides"), SOLRingDense::ShouldShowDenseTier(0.5, 1000.0001, 1000.0));
    TestFalse(TEXT("Warp far above max hides"), SOLRingDense::ShouldShowDenseTier(1.0, 1.0e6, 1000.0));
    TestFalse(TEXT("Alpha exactly 0 hides"), SOLRingDense::ShouldShowDenseTier(0.0, 1.0, 1000.0));
    TestFalse(TEXT("Negative alpha hides"), SOLRingDense::ShouldShowDenseTier(-0.1, 1.0, 1000.0));
    TestFalse(TEXT("Alpha 0 and excessive warp hides"), SOLRingDense::ShouldShowDenseTier(0.0, 1.0e6, 1000.0));
    TestFalse(TEXT("Max warp 0 never shows (warp 0)"), SOLRingDense::ShouldShowDenseTier(1.0, 0.0, 0.0));
    TestFalse(TEXT("Max warp 0 never shows (warp 1)"), SOLRingDense::ShouldShowDenseTier(1.0, 1.0, 0.0));
    TestFalse(TEXT("Negative max warp never shows"), SOLRingDense::ShouldShowDenseTier(1.0, -5.0, -1.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseFillFractionTest, "SOLTest.RingDense.FillFraction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DenseFillFraction clamps the ring density to [0, 1] and maps NaN (and +inf) to 1
bool FSOLRingDenseFillFractionTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Density 0 -> 0"), SOLRingDense::DenseFillFraction(0.0), 0.0);
    TestEqual(TEXT("Density 1 -> 1"), SOLRingDense::DenseFillFraction(1.0), 1.0);
    TestNearlyEqual(TEXT("Density 0.35 -> 0.35"), SOLRingDense::DenseFillFraction(0.35), 0.35, RINGDENSE_TIGHT_TOL);
    TestEqual(TEXT("Density 1.7 clamps to 1"), SOLRingDense::DenseFillFraction(1.7), 1.0);
    TestEqual(TEXT("Density -0.4 clamps to 0"), SOLRingDense::DenseFillFraction(-0.4), 0.0);
    TestEqual(TEXT("Quiet NaN -> 1"), SOLRingDense::DenseFillFraction(std::numeric_limits<double>::quiet_NaN()), 1.0);
    TestEqual(TEXT("+inf -> 1"), SOLRingDense::DenseFillFraction(std::numeric_limits<double>::infinity()), 1.0);

    // Output always finite and within [0, 1]
    const double inputs[] = { -1.0e300, -1.0, 0.0, 0.5, 1.0, 1.0e300 };
    for (const double density : inputs)
    {
        const double fraction = SOLRingDense::DenseFillFraction(density);
        TestTrue(FString::Printf(TEXT("Fraction for %g is finite and in [0, 1]"), density),
            std::isfinite(fraction) && fraction >= 0.0 && fraction <= 1.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseNiagaraMirrorTest, "SOLTest.RingDense.NiagaraLocalMirror",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RingFrameToNiagaraLocalCm mirrors Y only, and is its own inverse
bool FSOLRingDenseNiagaraMirrorTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d mirrored = SOLRingDense::RingFrameToNiagaraLocalCm(FVector3d(1.0, 2.0, 3.0));
    TestTrue(TEXT("(1,2,3) -> (1,-2,3)"), SOLTestHelpers::VectorsNear(mirrored, FVector3d(1.0, -2.0, 3.0), 1.0e-12));
    const FVector3d twice = SOLRingDense::RingFrameToNiagaraLocalCm(mirrored);
    TestTrue(TEXT("Applying it twice is the identity"),
        SOLTestHelpers::VectorsNear(twice, FVector3d(1.0, 2.0, 3.0), 1.0e-12));
    const FVector3d big = SOLRingDense::RingFrameToNiagaraLocalCm(FVector3d(1.0e10, -3.5e6, 5.0e3));
    TestEqual(TEXT("Length is preserved (masking radius stays right)"), big.Size(),
        FVector3d(1.0e10, -3.5e6, 5.0e3).Size(), 1.0e-3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseCoRotationTest, "SOLTest.RingDense.ProgradeOrbitIsStationary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A camera on a prograde circular orbit in the ring plane (relative = R * Rz(omega t) * (r, 0, h)) has a constant
// co-rotating-frame position when the spin angle is integrated with AdvanceSpinAngle: pins the spin sign, the -angle
// convention in RingFramePositionCm and the Keplerian rate together
bool FSOLRingDenseCoRotationTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d orientation = FQuat4d(FVector3d(0.3, -0.5, 0.8).GetSafeNormal(), 0.9);
    const FVector3d e1 = orientation.RotateVector(FVector3d(1.0, 0.0, 0.0));
    const FVector3d e2 = orientation.RotateVector(FVector3d(0.0, 1.0, 0.0));
    const FVector3d n = orientation.RotateVector(FVector3d(0.0, 0.0, 1.0));
    constexpr double radiusM = 1.0e8;
    constexpr double heightM = 25.0;
    const double omega = RingDenseExpectedOmega();

    double angle = 0.0;
    double lastTimeS = 0.0;
    const FVector3d expectedCm = FVector3d(radiusM, 0.0, heightM) * 100.0;
    for (const double timeS : { 10.0, 600.0, 3600.0, 3.0e4, 1.0e5 })
    {
        // Step the integrated angle exactly as the host does, frame by frame in uneven steps
        angle = SOLRingDense::AdvanceSpinAngle(angle, RINGDENSE_PLANET_GM, radiusM, timeS - lastTimeS);
        lastTimeS = timeS;
        const double orbitAngle = omega * timeS;
        const FQuat4d spin(FVector3d(0.0, 0.0, 1.0), orbitAngle);
        const FVector3d inPlane = spin.RotateVector(FVector3d(radiusM, 0.0, 0.0));
        const FVector3d relativeM = e1 * inPlane.X + e2 * inPlane.Y + n * heightM;
        const FVector3d positionCm = SOLRingDense::RingFramePositionCm(relativeM, e1, e2, n, angle);
        TestTrue(*FString::Printf(TEXT("t=%g s: co-rotating position stays (r, 0, h) (got %s)"), timeS,
            *positionCm.ToString()), SOLTestHelpers::VectorsNear(positionCm, expectedCm, 1.0e-2));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseCarpetFixedTest, "SOLTest.RingDense.CarpetStaysFixedInRingFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A carpet rock's position in the ring's co-rotating frame is the same whatever the camera position and spin angle
// (modulo the wrap tile): the check that catches a handedness mistake between the window parameters and the
// component rotation (a missing Y mirror makes rocks slide with the camera)
bool FSOLRingDenseCarpetFixedTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d orientation = FQuat4d(FVector3d(-0.4, 0.2, 0.9).GetSafeNormal(), 0.7);
    const FVector3d e1 = orientation.RotateVector(FVector3d(1.0, 0.0, 0.0));
    const FVector3d e2 = orientation.RotateVector(FVector3d(0.0, 1.0, 0.0));
    const FVector3d n = orientation.RotateVector(FVector3d(0.0, 0.0, 1.0));
    constexpr double windowRadiusCm = 150000.0;
    const double tileCm = 2.0 * windowRadiusCm;
    const FVector3d anchorCm(40000.0, 210000.0, -1500.0);

    // Camera positions in the co-rotating frame (cm), turned into ecliptic metres for several spin angles
    const FVector3d ringFrameCameras[] = { FVector3d(1.0e10, 0.0, 5000.0), FVector3d(1.0e10, 3.7e5, 2500.0),
        FVector3d(1.0e10 + 8.1e4, -2.2e5, 0.0), FVector3d(0.8e10, 1.1e4, 12000.0) };
    const double spins[] = { 0.0, 0.7, 2.9, 5.5 };

    // The rock's expected co-rotating position: the anchor mirrored, plus whole tiles (Niagara local = mirrored frame)
    const FVector3d expected = SOLRingDense::RingFrameToNiagaraLocalCm(anchorCm);
    for (const FVector3d& cameraRingFrame : ringFrameCameras)
    {
        for (const double spin : spins)
        {
            // Inverse of RingFramePositionCm for a camera at cameraRingFrame with this spin
            double sinSpin = 0.0;
            double cosSpin = 1.0;
            FMath::SinCos(&sinSpin, &cosSpin, spin);
            const FVector3d inPlaneM(
                (cameraRingFrame.X * cosSpin - cameraRingFrame.Y * sinSpin) / 100.0,
                (cameraRingFrame.X * sinSpin + cameraRingFrame.Y * cosSpin) / 100.0, cameraRingFrame.Z / 100.0);
            const FVector3d relativeM = e1 * inPlaneM.X + e2 * inPlaneM.Y + n * inPlaneM.Z;

            const FVector3d rock = RingDenseCarpetRockRingFrameCm(orientation, spin, relativeM, anchorCm, windowRadiusCm);
            const double tilesX = (rock.X - expected.X) / tileCm;
            const double tilesY = (rock.Y - expected.Y) / tileCm;
            const FString label = FString::Printf(TEXT("camera %s spin %.1f: rock at %s"),
                *cameraRingFrame.ToString(), spin, *rock.ToString());
            TestTrue(*(label + TEXT(" - x is the anchor plus whole tiles")),
                FMath::Abs(tilesX - FMath::RoundToDouble(tilesX)) < 1.0e-6);
            TestTrue(*(label + TEXT(" - y is the mirrored anchor plus whole tiles")),
                FMath::Abs(tilesY - FMath::RoundToDouble(tilesY)) < 1.0e-6);
            TestNearlyEqual(*(label + TEXT(" - height is the anchor's")), rock.Z, anchorCm.Z, 1.0e-3);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingDenseSpinNonFiniteTest, "SOLTest.RingDense.SpinNonFiniteKeepsAngle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A NaN or infinite time step, GM or radius never poisons the integrated angle: the previous angle is kept
bool FSOLRingDenseSpinNonFiniteTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double kept = SOLRingDense::AdvanceSpinAngle(1.25, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M, nan);
    TestNearlyEqual(TEXT("NaN dt keeps the angle"), kept, 1.25, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("Infinite dt keeps the angle"),
        SOLRingDense::AdvanceSpinAngle(1.25, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M, inf), 1.25, RINGDENSE_TIGHT_TOL);
    TestNearlyEqual(TEXT("NaN GM keeps the angle"),
        SOLRingDense::AdvanceSpinAngle(1.25, nan, RINGDENSE_PLANE_RADIUS_M, 10.0), 1.25, RINGDENSE_TIGHT_TOL);
    TestTrue(TEXT("A NaN start angle yields a finite angle in range"),
        RingDenseAngleInRange(SOLRingDense::AdvanceSpinAngle(nan, RINGDENSE_PLANET_GM, RINGDENSE_PLANE_RADIUS_M, 10.0)));
    TestEqual(TEXT("-inf density clamps to 0"), SOLRingDense::DenseFillFraction(-inf), 0.0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
