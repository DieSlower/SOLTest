/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Level/SOLSurfaceLock.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Body radii used by the state-machine tests (meters)
    constexpr double SURFACE_LOCK_ASTEROID_RADIUS_M = 1.0e5;       // below the 1,000 km manual floor
    constexpr double SURFACE_LOCK_MID_RADIUS_M = 2.0e6;            // manual range 2,000 km, release 2,500 km
    constexpr double SURFACE_LOCK_EARTH_RADIUS_M = 6.371e6;
    constexpr double SURFACE_LOCK_SUN_RADIUS_M = 6.96e8;

    // Body indices used by the state-machine tests
    constexpr int32 SURFACE_LOCK_BODY_A = 3;
    constexpr int32 SURFACE_LOCK_BODY_B = 7;
    constexpr int32 SURFACE_LOCK_BODY_DECOY = 11;                  // "nearest" body fed to engaged-branch calls

    // Sentinels for parameters a branch must ignore; a mutant that reads the wrong one fails
    constexpr double SURFACE_LOCK_SENTINEL_LOCKED_ALT_M = 1.0e12;      // unengaged calls: never within any range
    constexpr double SURFACE_LOCK_SENTINEL_LOCKED_RADIUS_M = 1.0e12;   // unengaged calls: would put everything in range
    constexpr double SURFACE_LOCK_SENTINEL_NEAREST_ALT_M = 1.0;        // engaged calls: would never release
    constexpr double SURFACE_LOCK_SENTINEL_NEAREST_RADIUS_M = 1.0e12;  // engaged calls: would never release

    // A tiny step used to probe just above or below a threshold (meters)
    constexpr double SURFACE_LOCK_EPSILON_M = 1.0e-3;

    // Tight relative tolerance for closed-form vector and angle checks
    constexpr double SURFACE_LOCK_TIGHT_TOL = 1e-9;

    //////////////////////////////////////////////////////////////////////////
    // Builds a lock state from its six fields (suppression defaults to off)
    FSOLSurfaceLockState SurfaceLockMakeState(const int32 bodyIndex, const bool bEngaged, const bool bManual,
        const bool bWarning, const bool bAutoSuppressed = false, const int32 suppressedBodyIndex = INDEX_NONE)
    {
        FSOLSurfaceLockState state;
        state.BodyIndex = bodyIndex;
        state.bEngaged = bEngaged;
        state.bManual = bManual;
        state.bWarning = bWarning;
        state.bAutoSuppressed = bAutoSuppressed;
        state.SuppressedBodyIndex = suppressedBodyIndex;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the fully released, unsuppressed (default) lock state
    FSOLSurfaceLockState SurfaceLockReleased()
    {
        return SurfaceLockMakeState(INDEX_NONE, false, false, false);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the released state with auto-engage suppressed for one body (what a manual-release press leaves)
    FSOLSurfaceLockState SurfaceLockSuppressed(const int32 bodyIndex)
    {
        return SurfaceLockMakeState(INDEX_NONE, false, false, false, true, bodyIndex);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts every field of a lock state equals the expected one
    void SurfaceLockExpectState(FAutomationTestBase& test, const FString& label, const FSOLSurfaceLockState& actual,
        const FSOLSurfaceLockState& expected)
    {
        test.TestEqual(*FString::Printf(TEXT("%s: BodyIndex"), *label), actual.BodyIndex, expected.BodyIndex);
        test.TestTrue(*FString::Printf(TEXT("%s: bEngaged %d == %d"), *label, actual.bEngaged ? 1 : 0,
            expected.bEngaged ? 1 : 0), actual.bEngaged == expected.bEngaged);
        test.TestTrue(*FString::Printf(TEXT("%s: bManual %d == %d"), *label, actual.bManual ? 1 : 0,
            expected.bManual ? 1 : 0), actual.bManual == expected.bManual);
        test.TestTrue(*FString::Printf(TEXT("%s: bWarning %d == %d"), *label, actual.bWarning ? 1 : 0,
            expected.bWarning ? 1 : 0), actual.bWarning == expected.bWarning);
        test.TestTrue(*FString::Printf(TEXT("%s: bAutoSuppressed %d == %d"), *label, actual.bAutoSuppressed ? 1 : 0,
            expected.bAutoSuppressed ? 1 : 0), actual.bAutoSuppressed == expected.bAutoSuppressed);
        test.TestEqual(*FString::Printf(TEXT("%s: SuppressedBodyIndex"), *label), actual.SuppressedBodyIndex,
            expected.SuppressedBodyIndex);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs an update from an unengaged state; the locked-body parameters get sentinels the branch must ignore
    FSOLSurfaceLockState SurfaceLockStepUnengaged(const FSOLSurfaceLockState& prev, const int32 nearestIndex,
        const double nearestAltitudeM, const double nearestRadiusM, const bool bPress,
        const FSOLSurfaceLockParams& params)
    {
        return SOLSurfaceLock::UpdateSurfaceLockState(prev, nearestIndex, nearestAltitudeM, nearestRadiusM,
            SURFACE_LOCK_SENTINEL_LOCKED_ALT_M, SURFACE_LOCK_SENTINEL_LOCKED_RADIUS_M, bPress, params);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs an update from an engaged state; a decoy nearest body gets sentinels the branch must ignore
    FSOLSurfaceLockState SurfaceLockStepEngaged(const FSOLSurfaceLockState& prev, const double lockedAltitudeM,
        const double lockedRadiusM, const bool bPress, const FSOLSurfaceLockParams& params)
    {
        return SOLSurfaceLock::UpdateSurfaceLockState(prev, SURFACE_LOCK_BODY_DECOY,
            SURFACE_LOCK_SENTINEL_NEAREST_ALT_M, SURFACE_LOCK_SENTINEL_NEAREST_RADIUS_M, lockedAltitudeM,
            lockedRadiusM, bPress, params);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unsigned angle between two vectors, in radians (atan2 form, accurate near 0 and PI)
    double SurfaceLockUnsignedAngle(const FVector3d& a, const FVector3d& b)
    {
        return FMath::Atan2(FVector3d::CrossProduct(a, b).Size(), FVector3d::DotProduct(a, b));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a general, non-axis-aligned test orientation
    FQuat4d SurfaceLockGeneralOrientation()
    {
        return FQuat4d(FVector3d(0.3, -0.4, 0.5).GetSafeNormal(), 0.77);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship's local up in world space
    FVector3d SurfaceLockUp(const FQuat4d& orientation)
    {
        return orientation.RotateVector(FVector3d(0.0, 0.0, 1.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two quaternions have bit-identical components
    bool SurfaceLockQuatsIdentical(const FQuat4d& a, const FQuat4d& b)
    {
        return a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every quaternion component is finite
    bool SurfaceLockQuatFinite(const FQuat4d& q)
    {
        return FMath::IsFinite(q.X) && FMath::IsFinite(q.Y) && FMath::IsFinite(q.Z) && FMath::IsFinite(q.W);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship's up rotated by angleRad about the unit axis (use an axis perpendicular to up for an exact angle)
    FVector3d SurfaceLockTiltedUp(const FQuat4d& orientation, const FVector3d& unitAxis, const double angleRad)
    {
        return FQuat4d(unitAxis, angleRad).RotateVector(SurfaceLockUp(orientation));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a unit axis perpendicular to the ship's up, mixing its forward and right
    FVector3d SurfaceLockMixedAxis(const FQuat4d& orientation)
    {
        return (orientation.RotateVector(FVector3d(1.0, 0.0, 0.0)) * 0.6
            + orientation.RotateVector(FVector3d(0.0, 1.0, 0.0)) * 0.8).GetSafeNormal();
    }

    //////////////////////////////////////////////////////////////////////////
    // Independently derives the expected step rotation: shortest arc from up to target, angle scaled by 1 - e^(-dt/tau)
    FQuat4d SurfaceLockExpectedStep(const FVector3d& up, const FVector3d& target, const double tauS, const double dt)
    {
        const FVector3d axis = FVector3d::CrossProduct(up, target).GetSafeNormal();
        const double angle = SurfaceLockUnsignedAngle(up, target);
        return FQuat4d(axis, angle * (1.0 - FMath::Exp(-dt / tauS)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts the rotated local axes stay unit length and mutually orthogonal
    void SurfaceLockExpectOrthonormal(FAutomationTestBase& test, const FString& label, const FQuat4d& q)
    {
        const FVector3d x = q.RotateVector(FVector3d(1.0, 0.0, 0.0));
        const FVector3d y = q.RotateVector(FVector3d(0.0, 1.0, 0.0));
        const FVector3d z = q.RotateVector(FVector3d(0.0, 0.0, 1.0));
        test.TestTrue(*FString::Printf(TEXT("%s: quaternion normalized (%.15f)"), *label, q.Size()),
            FMath::Abs(q.Size() - 1.0) < 1e-12);
        test.TestTrue(*FString::Printf(TEXT("%s: axes unit length"), *label), FMath::Abs(x.Size() - 1.0) < 1e-12
            && FMath::Abs(y.Size() - 1.0) < 1e-12 && FMath::Abs(z.Size() - 1.0) < 1e-12);
        test.TestTrue(*FString::Printf(TEXT("%s: axes orthogonal"), *label),
            FMath::Abs(FVector3d::DotProduct(x, y)) < 1e-12 && FMath::Abs(FVector3d::DotProduct(y, z)) < 1e-12
            && FMath::Abs(FVector3d::DotProduct(z, x)) < 1e-12);
    }
}

// --- Params defaults ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockParamsDefaultsTest, "SOLTest.SurfaceLock.ParamsDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The default params and state match the contract values
bool FSOLSurfaceLockParamsDefaultsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    TestEqual(TEXT("AutoRangeM is 10 km"), params.AutoRangeM, 10000.0, 0.0);
    TestEqual(TEXT("AutoReleaseFactor is 1.25"), params.AutoReleaseFactor, 1.25, 0.0);
    TestEqual(TEXT("ManualRangeMinM is 1,000 km"), params.ManualRangeMinM, 1.0e6, 0.0);
    TestEqual(TEXT("ManualReleaseFactor is 1.25"), params.ManualReleaseFactor, 1.25, 0.0);
    TestEqual(TEXT("AlignTimeConstantS is 1 s"), params.AlignTimeConstantS, 1.0, 0.0);

    const FSOLSurfaceLockState state;
    SurfaceLockExpectState(*this, TEXT("Default state is released and unsuppressed"), state, SurfaceLockReleased());
    return true;
}

// --- ComputeManualRangeM ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockManualRangeTest, "SOLTest.SurfaceLock.ManualRangeFloorOrRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Small bodies get the 1,000 km floor; bodies larger than the floor get their own radius
bool FSOLSurfaceLockManualRangeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    TestEqual(TEXT("100 km asteroid -> 1,000 km floor"),
        SOLSurfaceLock::ComputeManualRangeM(SURFACE_LOCK_ASTEROID_RADIUS_M, params), 1.0e6, 0.0);
    TestEqual(TEXT("Radius exactly at the floor -> the floor"), SOLSurfaceLock::ComputeManualRangeM(1.0e6, params),
        1.0e6, 0.0);
    TestEqual(TEXT("2,000 km body -> 2,000 km"), SOLSurfaceLock::ComputeManualRangeM(SURFACE_LOCK_MID_RADIUS_M, params),
        SURFACE_LOCK_MID_RADIUS_M, 0.0);
    TestEqual(TEXT("Earth -> its radius"), SOLSurfaceLock::ComputeManualRangeM(SURFACE_LOCK_EARTH_RADIUS_M, params),
        SURFACE_LOCK_EARTH_RADIUS_M, 0.0);
    TestEqual(TEXT("Sun -> its radius"), SOLSurfaceLock::ComputeManualRangeM(SURFACE_LOCK_SUN_RADIUS_M, params),
        SURFACE_LOCK_SUN_RADIUS_M, 0.0);

    // A custom floor is honored
    FSOLSurfaceLockParams custom;
    custom.ManualRangeMinM = 5.0e6;
    TestEqual(TEXT("Custom 5,000 km floor beats a 2,000 km radius"),
        SOLSurfaceLock::ComputeManualRangeM(SURFACE_LOCK_MID_RADIUS_M, custom), 5.0e6, 0.0);
    return true;
}

// --- UpdateSurfaceLockState: unengaged, no press ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAutoEngageTest, "SOLTest.SurfaceLock.AutoEngageUnderAutoRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Unengaged with no press: at or below 10 km of the nearest body auto-engages it in Auto mode
bool FSOLSurfaceLockAutoEngageTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState expected = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false);
    const double altitudes[] = { 9999.0, 5000.0, 100.0, 0.0, params.AutoRangeM };
    for (const double altitude : altitudes)
    {
        const FSOLSurfaceLockState next = SurfaceLockStepUnengaged(SurfaceLockReleased(), SURFACE_LOCK_BODY_A,
            altitude, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Auto-engage at %.3f m"), altitude), next, expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockStayUnengagedTest, "SOLTest.SurfaceLock.StayUnengagedWithoutPress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Unengaged with no press: above 10 km, or with no body at all, the state stays released
bool FSOLSurfaceLockStayUnengagedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState prev = SurfaceLockReleased();

    // Above the auto range, including well inside the manual range
    const double altitudes[] = { params.AutoRangeM + SURFACE_LOCK_EPSILON_M, 12000.0, 5.0e5, 1.0e9 };
    for (const double altitude : altitudes)
    {
        const FSOLSurfaceLockState next = SurfaceLockStepUnengaged(prev, SURFACE_LOCK_BODY_A, altitude,
            SURFACE_LOCK_EARTH_RADIUS_M, false, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("No press at %.3f m"), altitude), next, prev);
    }

    // No body at all, even with a tiny altitude value passed
    const FSOLSurfaceLockState none = SurfaceLockStepUnengaged(prev, INDEX_NONE, 0.0, SURFACE_LOCK_EARTH_RADIUS_M,
        false, params);
    SurfaceLockExpectState(*this, TEXT("No body, no press"), none, prev);
    return true;
}

// --- UpdateSurfaceLockState: unengaged, press ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockManualEngageTest, "SOLTest.SurfaceLock.PressEngagesManualBeyondAutoRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press beyond 10 km but within the nearest body's manual range engages it in Manual mode
bool FSOLSurfaceLockManualEngageTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState expected = SurfaceLockMakeState(SURFACE_LOCK_BODY_B, true, true, false);

    // Earth: manual range is its radius (6,371 km)
    const double earthAltitudes[] = { 5.0e5, params.AutoRangeM + SURFACE_LOCK_EPSILON_M, SURFACE_LOCK_EARTH_RADIUS_M };
    for (const double altitude : earthAltitudes)
    {
        const FSOLSurfaceLockState next = SurfaceLockStepUnengaged(SurfaceLockReleased(), SURFACE_LOCK_BODY_B,
            altitude, SURFACE_LOCK_EARTH_RADIUS_M, true, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Earth press at %.3f m"), altitude), next, expected);
    }

    // Asteroid: manual range is the 1,000 km floor; engaging exactly at the floor is allowed
    const double asteroidAltitudes[] = { 5.0e5, 1.0e6 };
    for (const double altitude : asteroidAltitudes)
    {
        const FSOLSurfaceLockState next = SurfaceLockStepUnengaged(SurfaceLockReleased(), SURFACE_LOCK_BODY_B,
            altitude, SURFACE_LOCK_ASTEROID_RADIUS_M, true, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Asteroid press at %.3f m"), altitude), next, expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockPressInsideAutoTest, "SOLTest.SurfaceLock.PressInsideAutoRangeIsAuto",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press at or below 10 km engages in Auto mode (bManual false), even though a press caused it
bool FSOLSurfaceLockPressInsideAutoTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState expected = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false);
    const double altitudes[] = { 5000.0, params.AutoRangeM };
    for (const double altitude : altitudes)
    {
        const FSOLSurfaceLockState next = SurfaceLockStepUnengaged(SurfaceLockReleased(), SURFACE_LOCK_BODY_A,
            altitude, SURFACE_LOCK_EARTH_RADIUS_M, true, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Press at %.3f m"), altitude), next, expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockPressOutOfRangeTest, "SOLTest.SurfaceLock.PressOutOfRangeNoEngage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press beyond the nearest body's manual range leaves the state unchanged
bool FSOLSurfaceLockPressOutOfRangeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState prev = SurfaceLockReleased();

    // Asteroid: floor 1,000 km
    const double asteroidAltitudes[] = { 1.0e6 + SURFACE_LOCK_EPSILON_M, 1.5e6, 1.0e9 };
    for (const double altitude : asteroidAltitudes)
    {
        const FSOLSurfaceLockState next = SurfaceLockStepUnengaged(prev, SURFACE_LOCK_BODY_A, altitude,
            SURFACE_LOCK_ASTEROID_RADIUS_M, true, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Asteroid press at %.3f m"), altitude), next, prev);
    }

    // Earth: its radius
    const FSOLSurfaceLockState earth = SurfaceLockStepUnengaged(prev, SURFACE_LOCK_BODY_A,
        SURFACE_LOCK_EARTH_RADIUS_M + 1.0, SURFACE_LOCK_EARTH_RADIUS_M, true, params);
    SurfaceLockExpectState(*this, TEXT("Earth press just beyond its radius"), earth, prev);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockPressNoBodyTest, "SOLTest.SurfaceLock.PressWithNoBodyNoEngage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press with no body at all (INDEX_NONE) leaves the state unchanged
bool FSOLSurfaceLockPressNoBodyTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState prev = SurfaceLockReleased();
    const FSOLSurfaceLockState next = SurfaceLockStepUnengaged(prev, INDEX_NONE, 0.0, SURFACE_LOCK_EARTH_RADIUS_M,
        true, params);
    SurfaceLockExpectState(*this, TEXT("Press, no body"), next, prev);
    return true;
}

// --- UpdateSurfaceLockState: engaged, press ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockPressReleasesTest, "SOLTest.SurfaceLock.PressWhileEngagedReleases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press while engaged releases instantly at any altitude and latches auto-engage suppression onto the released body
bool FSOLSurfaceLockPressReleasesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState prevs[] =
    {
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false),
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true),
        SurfaceLockMakeState(SURFACE_LOCK_BODY_B, true, true, false),
        SurfaceLockMakeState(SURFACE_LOCK_BODY_B, true, true, true),
    };
    const double altitudes[] = { 10.0, 1.0e12 };
    for (const FSOLSurfaceLockState& prev : prevs)
    {
        for (const double altitude : altitudes)
        {
            const FSOLSurfaceLockState next = SurfaceLockStepEngaged(prev, altitude, SURFACE_LOCK_EARTH_RADIUS_M,
                true, params);
            SurfaceLockExpectState(*this, FString::Printf(TEXT("Release body %d (manual %d, warning %d) at %.3e m"),
                prev.BodyIndex, prev.bManual ? 1 : 0, prev.bWarning ? 1 : 0, altitude), next,
                SurfaceLockSuppressed(prev.BodyIndex));
        }
    }

    // Also released when another body is nearest and very close (the press never re-engages in the same call)
    const FSOLSurfaceLockState other = SOLSurfaceLock::UpdateSurfaceLockState(prevs[0], SURFACE_LOCK_BODY_B, 10.0,
        SURFACE_LOCK_EARTH_RADIUS_M, 10.0, SURFACE_LOCK_EARTH_RADIUS_M, true, params);
    SurfaceLockExpectState(*this, TEXT("Release with a different nearest body"), other,
        SurfaceLockSuppressed(SURFACE_LOCK_BODY_A));
    return true;
}

// --- UpdateSurfaceLockState: engaged Auto mode, no press ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAutoThresholdsTest, "SOLTest.SurfaceLock.AutoWarnAndReleaseThresholds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Auto mode warns strictly above 10 km, stays engaged up to 12.5 km, and releases (unsuppressed) strictly above 12.5 km
bool FSOLSurfaceLockAutoThresholdsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState prev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false);
    const double warnM = params.AutoRangeM;
    const double releaseM = params.AutoRangeM * params.AutoReleaseFactor;
    TestEqual(TEXT("Auto release threshold is 12.5 km"), releaseM, 12500.0, 0.0);

    // Each case: altitude, expected state
    struct FCase
    {
        double AltitudeM;
        FSOLSurfaceLockState Expected;
    };
    const FCase cases[] =
    {
        { warnM - SURFACE_LOCK_EPSILON_M, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false) },
        { warnM, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false) },
        { warnM + SURFACE_LOCK_EPSILON_M, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true) },
        { 11000.0, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true) },
        { releaseM - SURFACE_LOCK_EPSILON_M, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true) },
        { releaseM, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true) },
        { releaseM + SURFACE_LOCK_EPSILON_M, SurfaceLockReleased() },
        { 1.0e6, SurfaceLockReleased() },
    };
    for (const FCase& testCase : cases)
    {
        const FSOLSurfaceLockState next = SurfaceLockStepEngaged(prev, testCase.AltitudeM, SURFACE_LOCK_EARTH_RADIUS_M,
            false, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Auto at %.3f m"), testCase.AltitudeM), next,
            testCase.Expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAutoRedescendTest, "SOLTest.SurfaceLock.AutoRedescendClearsWarning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Climbing into the Auto warning band then descending back under 10 km clears the warning and keeps the lock
bool FSOLSurfaceLockAutoRedescendTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    FSOLSurfaceLockState state = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false);

    // Climb into the warning band
    state = SurfaceLockStepEngaged(state, 11500.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Climb to 11.5 km"), state,
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true));

    // Descend back under 10 km
    state = SurfaceLockStepEngaged(state, 8000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Descend to 8 km"), state,
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false));
    return true;
}

// --- UpdateSurfaceLockState: engaged Manual mode, no press ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockManualThresholdsTest, "SOLTest.SurfaceLock.ManualWarnAndReleaseThresholds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Manual mode on a 2,000 km body warns strictly above 2,000 km and releases (unsuppressed) strictly above 2,500 km
bool FSOLSurfaceLockManualThresholdsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState prev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, false);
    const double warnM = SURFACE_LOCK_MID_RADIUS_M;
    const double releaseM = warnM * params.ManualReleaseFactor;
    TestEqual(TEXT("Manual release threshold is 2,500 km"), releaseM, 2.5e6, 0.0);

    // Each case: altitude, expected state
    struct FCase
    {
        double AltitudeM;
        FSOLSurfaceLockState Expected;
    };
    const FCase cases[] =
    {
        { warnM - 1.0, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, false) },
        { warnM, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, false) },
        { warnM + 1.0, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, true) },
        { releaseM - 1.0, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, true) },
        { releaseM, SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, true) },
        { releaseM + 1.0, SurfaceLockReleased() },
    };
    for (const FCase& testCase : cases)
    {
        const FSOLSurfaceLockState next = SurfaceLockStepEngaged(prev, testCase.AltitudeM, SURFACE_LOCK_MID_RADIUS_M,
            false, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Manual at %.3f m"), testCase.AltitudeM), next,
            testCase.Expected);
    }

    // Asteroid under the 1,000 km floor: warn above 1,000 km, release above 1,250 km
    const FSOLSurfaceLockState asteroidWarn = SurfaceLockStepEngaged(prev, 1.1e6, SURFACE_LOCK_ASTEROID_RADIUS_M,
        false, params);
    SurfaceLockExpectState(*this, TEXT("Asteroid manual at 1,100 km"), asteroidWarn,
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, true));
    const FSOLSurfaceLockState asteroidRelease = SurfaceLockStepEngaged(prev, 1.3e6, SURFACE_LOCK_ASTEROID_RADIUS_M,
        false, params);
    SurfaceLockExpectState(*this, TEXT("Asteroid manual at 1,300 km"), asteroidRelease, SurfaceLockReleased());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockManualIgnoresAutoTest, "SOLTest.SurfaceLock.ManualIgnoresAutoThresholds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Manual lock above the Auto 10 km / 12.5 km thresholds but inside its own range stays engaged without a warning
bool FSOLSurfaceLockManualIgnoresAutoTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState prev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, false);
    const double altitudes[] = { 10500.0, 12500.0 + SURFACE_LOCK_EPSILON_M, 5.0e4, 5.0e5 };
    for (const double altitude : altitudes)
    {
        const FSOLSurfaceLockState asteroid = SurfaceLockStepEngaged(prev, altitude, SURFACE_LOCK_ASTEROID_RADIUS_M,
            false, params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Asteroid manual at %.3f m"), altitude), asteroid, prev);
        const FSOLSurfaceLockState earth = SurfaceLockStepEngaged(prev, altitude, SURFACE_LOCK_EARTH_RADIUS_M, false,
            params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Earth manual at %.3f m"), altitude), earth, prev);
    }
    return true;
}

// --- UpdateSurfaceLockState: locked body is not the nearest ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockLockedNotNearestTest, "SOLTest.SurfaceLock.LockedBodyNotNearestNoSwap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// While engaged, release and warning use only the locked body's altitude/radius; a nearer body never takes over
bool FSOLSurfaceLockLockedNotNearestTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState autoPrev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false);

    // Another body is much nearer, but the locked body is inside its range: stays on the locked body, no warning
    const FSOLSurfaceLockState inRange = SOLSurfaceLock::UpdateSurfaceLockState(autoPrev, SURFACE_LOCK_BODY_B, 100.0,
        SURFACE_LOCK_ASTEROID_RADIUS_M, 5000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Auto, nearer body, locked at 5 km"), inRange, autoPrev);

    // Locked body in the warning band: warning comes from the locked altitude, not the nearer body's
    const FSOLSurfaceLockState warn = SOLSurfaceLock::UpdateSurfaceLockState(autoPrev, SURFACE_LOCK_BODY_B, 100.0,
        SURFACE_LOCK_ASTEROID_RADIUS_M, 11000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Auto, nearer body, locked at 11 km"), warn,
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true));

    // Locked body beyond release: releases even though the nearer body is within auto range (no swap in this call)
    const FSOLSurfaceLockState released = SOLSurfaceLock::UpdateSurfaceLockState(autoPrev, SURFACE_LOCK_BODY_B, 100.0,
        SURFACE_LOCK_ASTEROID_RADIUS_M, 13000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Auto, nearer body, locked at 13 km"), released, SurfaceLockReleased());

    // The nearest body is far away but the locked one is close: nearestAltitudeM must not trigger a release
    const FSOLSurfaceLockState farNearest = SOLSurfaceLock::UpdateSurfaceLockState(autoPrev, SURFACE_LOCK_BODY_B,
        1.0e12, SURFACE_LOCK_ASTEROID_RADIUS_M, 5000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Auto, nearest altitude huge, locked at 5 km"), farNearest, autoPrev);

    // Manual lock: the locked body's own radius sets the range, not the nearer body's radius
    const FSOLSurfaceLockState manualPrev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, false);
    const FSOLSurfaceLockState manualStay = SOLSurfaceLock::UpdateSurfaceLockState(manualPrev, SURFACE_LOCK_BODY_B,
        100.0, SURFACE_LOCK_ASTEROID_RADIUS_M, 1.5e6, SURFACE_LOCK_MID_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Manual, nearer small body, locked at 1,500 km"), manualStay, manualPrev);
    const FSOLSurfaceLockState manualRelease = SOLSurfaceLock::UpdateSurfaceLockState(manualPrev, SURFACE_LOCK_BODY_B,
        100.0, SURFACE_LOCK_SUN_RADIUS_M, 3.0e6, SURFACE_LOCK_MID_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Manual, nearer huge body, locked at 3,000 km"), manualRelease,
        SurfaceLockReleased());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockSteadyStateTest, "SOLTest.SurfaceLock.EngagedInRangeUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Engaged, no press, comfortably in range: the returned state is identical to the previous one
bool FSOLSurfaceLockSteadyStateTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState autoPrev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false);
    SurfaceLockExpectState(*this, TEXT("Auto at 5 km"), SurfaceLockStepEngaged(autoPrev, 5000.0,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params), autoPrev);

    const FSOLSurfaceLockState manualPrev = SurfaceLockMakeState(SURFACE_LOCK_BODY_B, true, true, false);
    SurfaceLockExpectState(*this, TEXT("Manual at 500 km"), SurfaceLockStepEngaged(manualPrev, 5.0e5,
        SURFACE_LOCK_MID_RADIUS_M, false, params), manualPrev);

    // A warning that is still warranted persists unchanged
    const FSOLSurfaceLockState warnPrev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true);
    SurfaceLockExpectState(*this, TEXT("Auto warning at 11 km"), SurfaceLockStepEngaged(warnPrev, 11000.0,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params), warnPrev);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockCustomParamsTest, "SOLTest.SurfaceLock.CustomParamsHonored",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-default params move the auto engage, warning and release thresholds, and the manual release threshold
bool FSOLSurfaceLockCustomParamsTest::RunTest(const FString& /*parameters*/)
{
    FSOLSurfaceLockParams params;
    params.AutoRangeM = 500.0;
    params.AutoReleaseFactor = 2.0;

    // Engages at 400 m, not at 600 m
    SurfaceLockExpectState(*this, TEXT("Custom auto-engage at 400 m"), SurfaceLockStepUnengaged(
        SurfaceLockReleased(), SURFACE_LOCK_BODY_A, 400.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params),
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false));
    SurfaceLockExpectState(*this, TEXT("Custom no engage at 600 m"), SurfaceLockStepUnengaged(
        SurfaceLockReleased(), SURFACE_LOCK_BODY_A, 600.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params),
        SurfaceLockReleased());

    // Warns at 600 m, stays to 1,000 m (Auto uses AutoReleaseFactor 2, not ManualReleaseFactor 1.25), releases above
    const FSOLSurfaceLockState prev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false);
    SurfaceLockExpectState(*this, TEXT("Custom warn at 600 m"), SurfaceLockStepEngaged(prev, 600.0,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params), SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true));
    SurfaceLockExpectState(*this, TEXT("Custom engaged at 1,000 m"), SurfaceLockStepEngaged(prev, 1000.0,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params), SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, true));
    SurfaceLockExpectState(*this, TEXT("Custom release at 1,001 m"), SurfaceLockStepEngaged(prev, 1001.0,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params), SurfaceLockReleased());

    // Manual release uses ManualReleaseFactor (1.5 -> 3,000 km on a 2,000 km body), never AutoReleaseFactor (3.0)
    FSOLSurfaceLockParams manualParams;
    manualParams.ManualReleaseFactor = 1.5;
    manualParams.AutoReleaseFactor = 3.0;
    const FSOLSurfaceLockState manualPrev = SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, false);
    SurfaceLockExpectState(*this, TEXT("Custom manual engaged at 2,900 km"), SurfaceLockStepEngaged(manualPrev, 2.9e6,
        SURFACE_LOCK_MID_RADIUS_M, false, manualParams), SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true, true));
    SurfaceLockExpectState(*this, TEXT("Custom manual release at 3,100 km"), SurfaceLockStepEngaged(manualPrev, 3.1e6,
        SURFACE_LOCK_MID_RADIUS_M, false, manualParams), SurfaceLockReleased());
    return true;
}

// --- UpdateSurfaceLockState: auto-engage suppression after a manual release ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockSuppressReengageTest, "SOLTest.SurfaceLock.ManualReleaseSuppressesAutoReengage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After a press releases an Auto lock, staying near that body (up to 12.5 km) does not auto-engage it again
bool FSOLSurfaceLockSuppressReengageTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;

    // Auto-engage at 5 km
    FSOLSurfaceLockState state = SurfaceLockStepUnengaged(SurfaceLockReleased(), SURFACE_LOCK_BODY_A, 5000.0,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Auto-engage at 5 km"), state,
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false));

    // Press releases and latches suppression on body A
    state = SurfaceLockStepEngaged(state, 5000.0, SURFACE_LOCK_EARTH_RADIUS_M, true, params);
    SurfaceLockExpectState(*this, TEXT("Press releases"), state, SurfaceLockSuppressed(SURFACE_LOCK_BODY_A));

    // No press, same body, still inside the auto range and up to exactly 12.5 km: stays released and suppressed
    const double altitudes[] = { 5000.0, 100.0, 9000.0, 12000.0, params.AutoRangeM * params.AutoReleaseFactor, 5000.0 };
    for (const double altitude : altitudes)
    {
        state = SurfaceLockStepUnengaged(state, SURFACE_LOCK_BODY_A, altitude, SURFACE_LOCK_EARTH_RADIUS_M, false,
            params);
        SurfaceLockExpectState(*this, FString::Printf(TEXT("Suppressed, no press at %.3f m"), altitude), state,
            SurfaceLockSuppressed(SURFACE_LOCK_BODY_A));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockSuppressBodyChangeTest, "SOLTest.SurfaceLock.SuppressionClearsOnBodyChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Suppression only applies to the released body; a different (or no) nearest body clears it
bool FSOLSurfaceLockSuppressBodyChangeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState suppressed = SurfaceLockSuppressed(SURFACE_LOCK_BODY_A);

    // A different body in auto range auto-engages normally, with suppression cleared
    SurfaceLockExpectState(*this, TEXT("Other body at 5 km auto-engages"), SurfaceLockStepUnengaged(suppressed,
        SURFACE_LOCK_BODY_B, 5000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params),
        SurfaceLockMakeState(SURFACE_LOCK_BODY_B, true, false, false));

    // A different body outside auto range: stays released, and the latch decays
    FSOLSurfaceLockState state = SurfaceLockStepUnengaged(suppressed, SURFACE_LOCK_BODY_B, 5.0e4,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Other body at 50 km clears the latch"), state, SurfaceLockReleased());

    // Back to body A inside auto range: the latch is gone, so it auto-engages
    state = SurfaceLockStepUnengaged(state, SURFACE_LOCK_BODY_A, 5000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Back to body A at 5 km auto-engages"), state,
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false));

    // No body at all also clears the latch
    SurfaceLockExpectState(*this, TEXT("No body clears the latch"), SurfaceLockStepUnengaged(suppressed, INDEX_NONE,
        0.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params), SurfaceLockReleased());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockSuppressClimbTest, "SOLTest.SurfaceLock.SuppressionClearsOnClimbAway",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Climbing strictly above 12.5 km from the suppressed body clears the latch, so a later descent auto-engages again
bool FSOLSurfaceLockSuppressClimbTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const double clearM = params.AutoRangeM * params.AutoReleaseFactor;

    // Climb just above 12.5 km: released and unsuppressed
    FSOLSurfaceLockState state = SurfaceLockStepUnengaged(SurfaceLockSuppressed(SURFACE_LOCK_BODY_A),
        SURFACE_LOCK_BODY_A, clearM + SURFACE_LOCK_EPSILON_M, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Climb above 12.5 km clears the latch"), state, SurfaceLockReleased());

    // Descend to 8 km: auto-engages, proving the latch is gone
    state = SurfaceLockStepUnengaged(state, SURFACE_LOCK_BODY_A, 8000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Descend to 8 km auto-engages"), state,
        SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false, false));

    // A much higher climb clears it too
    SurfaceLockExpectState(*this, TEXT("Climb to 500 km clears the latch"), SurfaceLockStepUnengaged(
        SurfaceLockSuppressed(SURFACE_LOCK_BODY_A), SURFACE_LOCK_BODY_A, 5.0e5, SURFACE_LOCK_EARTH_RADIUS_M, false,
        params), SurfaceLockReleased());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockSuppressManualTest, "SOLTest.SurfaceLock.SuppressionDoesNotBlockManualReengage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press while suppressed engages the suppressed body immediately and clears the latch
bool FSOLSurfaceLockSuppressManualTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState suppressed = SurfaceLockSuppressed(SURFACE_LOCK_BODY_A);

    // At 5 km: Auto mode
    SurfaceLockExpectState(*this, TEXT("Press at 5 km"), SurfaceLockStepUnengaged(suppressed, SURFACE_LOCK_BODY_A,
        5000.0, SURFACE_LOCK_EARTH_RADIUS_M, true, params), SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, false,
        false));

    // At 11 km (inside the suppression band, beyond auto range): Manual mode
    SurfaceLockExpectState(*this, TEXT("Press at 11 km"), SurfaceLockStepUnengaged(suppressed, SURFACE_LOCK_BODY_A,
        11000.0, SURFACE_LOCK_EARTH_RADIUS_M, true, params), SurfaceLockMakeState(SURFACE_LOCK_BODY_A, true, true,
        false));

    // A press that cannot engage (out of range, or no body) leaves the suppressed state unchanged
    SurfaceLockExpectState(*this, TEXT("Press out of range keeps the latch"), SurfaceLockStepUnengaged(suppressed,
        SURFACE_LOCK_BODY_A, 1.0e9, SURFACE_LOCK_ASTEROID_RADIUS_M, true, params), suppressed);
    SurfaceLockExpectState(*this, TEXT("Press with no body keeps the latch"), SurfaceLockStepUnengaged(suppressed,
        INDEX_NONE, 0.0, SURFACE_LOCK_EARTH_RADIUS_M, true, params), suppressed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockPressClearsSuppressTest, "SOLTest.SurfaceLock.PressEngageAlwaysClearsSuppression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press that engages a different body clears suppression held for the old body
bool FSOLSurfaceLockPressClearsSuppressTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSurfaceLockParams params;
    const FSOLSurfaceLockState suppressed = SurfaceLockSuppressed(SURFACE_LOCK_BODY_A);

    // Engage body B from 500 km (Manual)
    SurfaceLockExpectState(*this, TEXT("Press engages B at 500 km"), SurfaceLockStepUnengaged(suppressed,
        SURFACE_LOCK_BODY_B, 5.0e5, SURFACE_LOCK_EARTH_RADIUS_M, true, params),
        SurfaceLockMakeState(SURFACE_LOCK_BODY_B, true, true, false));

    // Engage body B from 5 km (Auto)
    SurfaceLockExpectState(*this, TEXT("Press engages B at 5 km"), SurfaceLockStepUnengaged(suppressed,
        SURFACE_LOCK_BODY_B, 5000.0, SURFACE_LOCK_EARTH_RADIUS_M, true, params),
        SurfaceLockMakeState(SURFACE_LOCK_BODY_B, true, false, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockSuppressBandFactorTest, "SOLTest.SurfaceLock.SuppressionBandUsesAutoReleaseFactor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The suppression band is AutoRangeM * AutoReleaseFactor, never ManualReleaseFactor
bool FSOLSurfaceLockSuppressBandFactorTest::RunTest(const FString& /*parameters*/)
{
    // AutoReleaseFactor 2.0, ManualReleaseFactor left at 1.25: band is 20 km
    FSOLSurfaceLockParams params;
    params.AutoReleaseFactor = 2.0;
    TestEqual(TEXT("ManualReleaseFactor left at its 1.25 default"), params.ManualReleaseFactor, 1.25, 0.0);

    // Auto-engage at 5 km, then a press releases and suppresses body A
    FSOLSurfaceLockState state = SurfaceLockStepUnengaged(SurfaceLockReleased(), SURFACE_LOCK_BODY_A, 5000.0,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    state = SurfaceLockStepEngaged(state, 5000.0, SURFACE_LOCK_EARTH_RADIUS_M, true, params);
    SurfaceLockExpectState(*this, TEXT("Press releases"), state, SurfaceLockSuppressed(SURFACE_LOCK_BODY_A));

    // 1.5 x AutoRangeM (15 km): inside the 2.0x band, beyond a wrongly used 1.25x band -> still suppressed
    state = SurfaceLockStepUnengaged(state, SURFACE_LOCK_BODY_A, 1.5 * params.AutoRangeM,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Still suppressed at 15 km"), state, SurfaceLockSuppressed(SURFACE_LOCK_BODY_A));

    // Back down to 5 km: still suppressed, so no auto-engage
    state = SurfaceLockStepUnengaged(state, SURFACE_LOCK_BODY_A, 5000.0, SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Still suppressed back at 5 km"), state,
        SurfaceLockSuppressed(SURFACE_LOCK_BODY_A));

    // Just above 20 km: the latch clears
    state = SurfaceLockStepUnengaged(state, SURFACE_LOCK_BODY_A, 2.0 * params.AutoRangeM + SURFACE_LOCK_EPSILON_M,
        SURFACE_LOCK_EARTH_RADIUS_M, false, params);
    SurfaceLockExpectState(*this, TEXT("Cleared just above 20 km"), state, SurfaceLockReleased());

    // Converse: ManualReleaseFactor 2.0, AutoReleaseFactor 1.25 -> at 15 km the latch has already cleared
    FSOLSurfaceLockParams converse;
    converse.ManualReleaseFactor = 2.0;
    SurfaceLockExpectState(*this, TEXT("Converse: cleared at 15 km"), SurfaceLockStepUnengaged(
        SurfaceLockSuppressed(SURFACE_LOCK_BODY_A), SURFACE_LOCK_BODY_A, 1.5 * converse.AutoRangeM,
        SURFACE_LOCK_EARTH_RADIUS_M, false, converse), SurfaceLockReleased());
    return true;
}

// --- TargetUpDir ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockTargetUpTest, "SOLTest.SurfaceLock.TargetUpPointsAwayFromBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The target up is the unit direction from the body's center to the ship, at any distance
bool FSOLSurfaceLockTargetUpTest::RunTest(const FString& /*parameters*/)
{
    // Each case: body position, offset from body to ship
    struct FCase
    {
        FVector3d BodyM;
        FVector3d OffsetM;
    };
    const FCase cases[] =
    {
        { FVector3d::ZeroVector, FVector3d(0.0, 0.0, 7.0e6) },
        { FVector3d(1.0e7, -2.0e7, 3.0e6), FVector3d(0.0, 0.0, 6.4e6) },
        { FVector3d(1.0e7, -2.0e7, 3.0e6), FVector3d(0.0, 0.0, -6.4e6) },
        { FVector3d(-5.0e8, 2.0e8, 0.0), FVector3d(3.0e6, 4.0e6, 0.0) },
        { FVector3d(SOLTestHelpers::AU_M, 0.0, 0.0), FVector3d(-1.0e6, 2.0e6, -2.0e6) },
        { FVector3d(0.0, 0.0, 0.0), FVector3d(1.0, 1.0, 1.0) },
    };
    for (const FCase& testCase : cases)
    {
        const FVector3d shipM = testCase.BodyM + testCase.OffsetM;
        const FVector3d up = SOLSurfaceLock::TargetUpDir(shipM, testCase.BodyM);
        const FVector3d expected = (shipM - testCase.BodyM) / (shipM - testCase.BodyM).Size();
        const FString label = FString::Printf(TEXT("Offset (%.3e, %.3e, %.3e)"), testCase.OffsetM.X, testCase.OffsetM.Y,
            testCase.OffsetM.Z);
        TestTrue(*FString::Printf(TEXT("%s: unit length (%.15f)"), *label, up.Size()),
            FMath::Abs(up.Size() - 1.0) < SURFACE_LOCK_TIGHT_TOL);
        TestTrue(*FString::Printf(TEXT("%s: (%.9f, %.9f, %.9f) points away from the body"), *label, up.X, up.Y, up.Z),
            SOLTestHelpers::VectorsNear(up, expected, SURFACE_LOCK_TIGHT_TOL));
    }

    // Explicit expectations
    TestTrue(TEXT("Ship straight above along +Z -> (0, 0, 1)"), SOLTestHelpers::VectorsNear(
        SOLSurfaceLock::TargetUpDir(FVector3d(5.0, 6.0, 1.0e7), FVector3d(5.0, 6.0, 0.0)), FVector3d(0.0, 0.0, 1.0),
        SURFACE_LOCK_TIGHT_TOL));
    TestTrue(TEXT("Ship below along -Z -> (0, 0, -1)"), SOLTestHelpers::VectorsNear(
        SOLSurfaceLock::TargetUpDir(FVector3d(0.0, 0.0, -2.0e6), FVector3d::ZeroVector), FVector3d(0.0, 0.0, -1.0),
        SURFACE_LOCK_TIGHT_TOL));
    TestTrue(TEXT("Diagonal (3, 4, 0) -> (0.6, 0.8, 0)"), SOLTestHelpers::VectorsNear(
        SOLSurfaceLock::TargetUpDir(FVector3d(3.0e6, 4.0e6, 0.0), FVector3d::ZeroVector), FVector3d(0.6, 0.8, 0.0),
        SURFACE_LOCK_TIGHT_TOL));
    return true;
}

// --- ApplyAlignmentCorrection ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAlignedTest, "SOLTest.SurfaceLock.AlignmentAlreadyAlignedUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// When the ship's up already equals the target up, the orientation is returned unchanged
bool FSOLSurfaceLockAlignedTest::RunTest(const FString& /*parameters*/)
{
    // Identity orientation, target straight up: exact early-out
    const FQuat4d identity = FQuat4d::Identity;
    const FQuat4d identityResult = SOLSurfaceLock::ApplyAlignmentCorrection(identity, FVector3d(0.0, 0.0, 1.0), 1.0,
        1.0);
    TestTrue(TEXT("Identity, target +Z: bit-identical"), SurfaceLockQuatsIdentical(identityResult, identity));

    // General orientations whose own up is the target
    const FQuat4d starts[] = { SurfaceLockGeneralOrientation(), FQuat4d(FVector3d(-0.7, 0.1, 0.2).GetSafeNormal(),
        2.3) };
    for (const FQuat4d& start : starts)
    {
        const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(start, SurfaceLockUp(start), 1.0, 0.5);
        TestTrue(*FString::Printf(TEXT("Start (%.6f, %.6f, %.6f, %.6f), own up: unchanged"), start.X, start.Y,
            start.Z, start.W), result.Equals(start, 1e-12));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAlign90Test, "SOLTest.SurfaceLock.Align90DegreesOneTimeConstant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 90-degree up error with dt = tau closes by exactly (1 - e^-1) along the shortest arc, leaving 90 * e^-1 degrees
bool FSOLSurfaceLockAlign90Test::RunTest(const FString& /*parameters*/)
{
    const double startAngle = UE_DOUBLE_HALF_PI;
    const double expectedRemaining = startAngle * FMath::Exp(-1.0);

    // Identity orientation, target +Y: the up must swing from +Z toward +Y in the Y-Z plane
    {
        const FVector3d target(0.0, 1.0, 0.0);
        const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(FQuat4d::Identity, target, 1.0, 1.0);
        const FVector3d newUp = SurfaceLockUp(result);
        const double remaining = SurfaceLockUnsignedAngle(newUp, target);
        TestTrue(*FString::Printf(TEXT("Identity: remaining %.12f rad == %.12f rad"), remaining, expectedRemaining),
            FMath::Abs(remaining - expectedRemaining) < SURFACE_LOCK_TIGHT_TOL);
        TestTrue(TEXT("Identity: new up lies between +Z and +Y"), newUp.Y > 0.0 && newUp.Z > 0.0
            && FMath::Abs(newUp.X) < 1e-12);
        SurfaceLockExpectOrthonormal(*this, TEXT("Identity"), result);
    }

    // General orientations, target 90 degrees away about axes mixing forward and right
    const FQuat4d starts[] = { SurfaceLockGeneralOrientation(), FQuat4d(FVector3d(-0.7, 0.1, 0.2).GetSafeNormal(),
        2.3) };
    for (const FQuat4d& start : starts)
    {
        for (const double sign : { 1.0, -1.0 })
        {
            const FVector3d up = SurfaceLockUp(start);
            const FVector3d target = SurfaceLockTiltedUp(start, SurfaceLockMixedAxis(start), sign * startAngle);
            const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(start, target, 1.0, 1.0);
            const FVector3d newUp = SurfaceLockUp(result);
            const FString label = FString::Printf(TEXT("Start W %.3f, sign %+.0f"), start.W, sign);
            const double remaining = SurfaceLockUnsignedAngle(newUp, target);
            TestTrue(*FString::Printf(TEXT("%s: remaining %.12f rad == %.12f rad"), *label, remaining,
                expectedRemaining), FMath::Abs(remaining - expectedRemaining) < SURFACE_LOCK_TIGHT_TOL);

            // Shortest arc: the new up stays in the plane spanned by the old up and the target
            const FVector3d planeNormal = FVector3d::CrossProduct(up, target).GetSafeNormal();
            TestTrue(*FString::Printf(TEXT("%s: new up in the up/target plane"), *label),
                FMath::Abs(FVector3d::DotProduct(newUp, planeNormal)) < SURFACE_LOCK_TIGHT_TOL);
            SurfaceLockExpectOrthonormal(*this, label, result);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAlignSlerpTest, "SOLTest.SurfaceLock.AlignmentIsShortestArcSlerp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The result is the shortest-arc rotation from up to target, scaled by 1 - e^(-dt/tau), applied on top; forward is not pinned
bool FSOLSurfaceLockAlignSlerpTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d starts[] = { FQuat4d::Identity, SurfaceLockGeneralOrientation(),
        FQuat4d(FVector3d(-0.7, 0.1, 0.2).GetSafeNormal(), 2.3) };
    const double angles[] = { 0.3, -1.2, 2.5 };
    const double dt = 0.2;
    const double tau = 0.8;
    const FVector3d probes[] = { FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d(0.2, -0.9, 0.4) };
    for (const FQuat4d& start : starts)
    {
        const FVector3d up = SurfaceLockUp(start);
        for (const double angle : angles)
        {
            const FVector3d target = SurfaceLockTiltedUp(start, SurfaceLockMixedAxis(start), angle);
            const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(start, target, tau, dt);
            const FQuat4d expectedStep = SurfaceLockExpectedStep(up, target, tau, dt);
            const FString label = FString::Printf(TEXT("Start W %.3f, angle %.2f rad"), start.W, angle);

            // result applied to any vector == step rotation applied on top of the start orientation
            for (const FVector3d& probe : probes)
            {
                TestTrue(*FString::Printf(TEXT("%s: probe (%.1f, %.1f, %.1f) matches step * start"), *label, probe.X,
                    probe.Y, probe.Z), SOLTestHelpers::VectorsNear(result.RotateVector(probe),
                    expectedStep.RotateVector(start.RotateVector(probe)), SURFACE_LOCK_TIGHT_TOL));
            }
            SurfaceLockExpectOrthonormal(*this, label, result);
        }
    }

    // Nose pointing straight at the target (the old "pole" case): no special-casing, the whole orientation tips over
    const FVector3d target(1.0, 0.0, 0.0);
    const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(FQuat4d::Identity, target, 1.0, 1.0);
    const FQuat4d expectedStep = SurfaceLockExpectedStep(FVector3d(0.0, 0.0, 1.0), target, 1.0, 1.0);
    for (const FVector3d& probe : probes)
    {
        TestTrue(*FString::Printf(TEXT("Nose at target: probe (%.1f, %.1f, %.1f) matches"), probe.X, probe.Y, probe.Z),
            SOLTestHelpers::VectorsNear(result.RotateVector(probe), expectedStep.RotateVector(probe),
            SURFACE_LOCK_TIGHT_TOL));
    }
    TestFalse(TEXT("Nose at target: forward is carried along, not pinned"), SOLTestHelpers::VectorsNear(
        result.RotateVector(FVector3d(1.0, 0.0, 0.0)), FVector3d(1.0, 0.0, 0.0), 1e-3));
    SurfaceLockExpectOrthonormal(*this, TEXT("Nose at target"), result);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAlignConvergeTest, "SOLTest.SurfaceLock.AlignmentConvergesFrom180In5s",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From a 180-degree worst case, five one-second steps at tau = 1 s leave at most 180 * e^-5 (~1.2) degrees, with no NaN
bool FSOLSurfaceLockAlignConvergeTest::RunTest(const FString& /*parameters*/)
{
    const double tau = 1.0;
    const double dt = 1.0;
    const int32 steps = 5;
    const double boundRad = UE_DOUBLE_PI * FMath::Exp(-steps * dt / tau);

    const FQuat4d starts[] = { FQuat4d::Identity, SurfaceLockGeneralOrientation() };
    for (const FQuat4d& start : starts)
    {
        // Target is exactly the ship's down (antiparallel on the first step)
        const FVector3d target = -SurfaceLockUp(start);
        FQuat4d orientation = start;
        bool bFinite = true;
        for (int32 step = 0; step < steps; ++step)
        {
            orientation = SOLSurfaceLock::ApplyAlignmentCorrection(orientation, target, tau, dt);
            bFinite = bFinite && SurfaceLockQuatFinite(orientation);
        }
        const FString label = FString::Printf(TEXT("Start W %.3f"), start.W);
        TestTrue(*FString::Printf(TEXT("%s: stays finite"), *label), bFinite);
        const double remaining = FMath::Abs(SOLTestHelpers::AngleDiffRad(SurfaceLockUnsignedAngle(
            SurfaceLockUp(orientation), target)));
        TestTrue(*FString::Printf(TEXT("%s: remaining %.6f deg <= 180 * e^-5 = %.6f deg"), *label,
            FMath::RadiansToDegrees(remaining), FMath::RadiansToDegrees(boundRad)), remaining <= boundRad * (1.0 + 1e-6));
        SurfaceLockExpectOrthonormal(*this, label, orientation);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAlignAntiparallelTest, "SOLTest.SurfaceLock.AlignmentHandlesAntiparallelUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With up exactly opposite the target, one step is finite, a valid rotation, and turns up by pi * (1 - e^(-dt/tau))
bool FSOLSurfaceLockAlignAntiparallelTest::RunTest(const FString& /*parameters*/)
{
    const double dt = 0.1;
    const double tau = 1.0;
    const double expectedRemaining = UE_DOUBLE_PI * FMath::Exp(-dt / tau);

    const FQuat4d starts[] = { FQuat4d::Identity, SurfaceLockGeneralOrientation() };
    for (const FQuat4d& start : starts)
    {
        const FVector3d target = -SurfaceLockUp(start);
        const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(start, target, tau, dt);
        const FString label = FString::Printf(TEXT("Start W %.3f"), start.W);
        TestTrue(*FString::Printf(TEXT("%s: finite"), *label), SurfaceLockQuatFinite(result));
        SurfaceLockExpectOrthonormal(*this, label, result);

        // Measurably turned toward the target, by exactly the scaled angle
        const double remaining = SurfaceLockUnsignedAngle(SurfaceLockUp(result), target);
        TestTrue(*FString::Printf(TEXT("%s: moved (remaining %.6f rad < pi - 0.29)"), *label, remaining),
            remaining < UE_DOUBLE_PI - 0.29);
        TestTrue(*FString::Printf(TEXT("%s: remaining %.12f rad == %.12f rad"), *label, remaining, expectedRemaining),
            FMath::Abs(remaining - expectedRemaining) < SURFACE_LOCK_TIGHT_TOL);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAlignAntiparallelXTest, "SOLTest.SurfaceLock.AlignmentAntiparallelUpAlongWorldX",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Antiparallel with the ship's up along (or near) world +/-X still yields a finite rotation of the expected size
bool FSOLSurfaceLockAlignAntiparallelXTest::RunTest(const FString& /*parameters*/)
{
    const double dt = 0.1;
    const double tau = 1.0;
    const double expectedRemaining = UE_DOUBLE_PI * FMath::Exp(-dt / tau);

    // Each case: label, start orientation (Z -> +X, Z -> -X, Z -> (0.95, sqrt(1 - 0.95^2), 0))
    const double nearY = FMath::Sqrt(1.0 - 0.95 * 0.95);
    struct FCase
    {
        const TCHAR* Label;
        FQuat4d Start;
    };
    const FCase cases[] =
    {
        { TEXT("Up +X"), FQuat4d(FVector3d(0.0, 1.0, 0.0), UE_DOUBLE_HALF_PI) },
        { TEXT("Up -X"), FQuat4d(FVector3d(0.0, 1.0, 0.0), -UE_DOUBLE_HALF_PI) },
        { TEXT("Up (0.95, 0.312, 0)"), FQuat4d(FVector3d(-nearY, 0.95, 0.0), UE_DOUBLE_HALF_PI) },
    };
    for (const FCase& testCase : cases)
    {
        const FVector3d up = SurfaceLockUp(testCase.Start);
        TestTrue(*FString::Printf(TEXT("%s: precondition |up.X| %.6f >= 0.9"), testCase.Label, up.X),
            FMath::Abs(up.X) >= 0.9);

        const FVector3d target = -up;
        const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(testCase.Start, target, tau, dt);
        TestTrue(*FString::Printf(TEXT("%s: finite"), testCase.Label), SurfaceLockQuatFinite(result));
        SurfaceLockExpectOrthonormal(*this, testCase.Label, result);

        const double remaining = SurfaceLockUnsignedAngle(SurfaceLockUp(result), target);
        TestTrue(*FString::Printf(TEXT("%s: remaining %.12f rad == %.12f rad"), testCase.Label, remaining,
            expectedRemaining), FMath::Abs(remaining - expectedRemaining) < SURFACE_LOCK_TIGHT_TOL);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAlignTinyErrorTest, "SOLTest.SurfaceLock.AlignmentTinyErrorNoDeadZone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Tiny up errors (1e-3 .. 1e-9 rad) still shrink by exactly e^(-dt/tau): no dead zone or cutoff near zero error
bool FSOLSurfaceLockAlignTinyErrorTest::RunTest(const FString& /*parameters*/)
{
    const double dt = 0.1;
    const double tau = 1.0;
    const double decay = FMath::Exp(-dt / tau);
    const FQuat4d starts[] = { FQuat4d::Identity, SurfaceLockGeneralOrientation() };
    const double errors[] = { 1e-3, 1e-6, 1e-9 };
    for (const FQuat4d& start : starts)
    {
        for (const double error : errors)
        {
            // Measure the actual starting error from the built vectors so construction rounding does not count
            const FVector3d target = SurfaceLockTiltedUp(start, SurfaceLockMixedAxis(start), error);
            const double originalError = SurfaceLockUnsignedAngle(SurfaceLockUp(start), target);
            const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(start, target, tau, dt);
            const double remaining = SurfaceLockUnsignedAngle(SurfaceLockUp(result), target);
            const double expected = originalError * decay;
            TestTrue(*FString::Printf(TEXT("Start W %.3f, error %.0e rad: remaining %.9e == %.9e (rel err %.3e)"),
                start.W, error, remaining, expected, SOLTestHelpers::RelativeError(remaining, expected)),
                SOLTestHelpers::RelativeError(remaining, expected) < 1e-6);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSurfaceLockAlignZeroDtTest, "SOLTest.SurfaceLock.AlignmentZeroDtUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With dt = 0 and a misaligned target, the step fraction 1 - e^0 = 0 leaves the orientation unchanged
bool FSOLSurfaceLockAlignZeroDtTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d starts[] = { FQuat4d::Identity, SurfaceLockGeneralOrientation() };
    for (const FQuat4d& start : starts)
    {
        const FVector3d target = SurfaceLockTiltedUp(start, SurfaceLockMixedAxis(start), 1.4);
        const FQuat4d result = SOLSurfaceLock::ApplyAlignmentCorrection(start, target, 1.0, 0.0);
        TestTrue(*FString::Printf(TEXT("Start (%.6f, %.6f, %.6f, %.6f): unchanged"), start.X, start.Y, start.Z,
            start.W), result.Equals(start, 1e-14));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
