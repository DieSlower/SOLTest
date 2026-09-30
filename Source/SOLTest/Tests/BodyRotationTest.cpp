/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLBodyRotation.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double BODY_ROTATION_SECONDS_PER_HOUR = 3600.0;
    const FVector3d BODY_ROTATION_NORTH(0.0, 0.0, 1.0);

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected tilted pole (sin t, 0, cos t), independent of the code under test
    FVector3d BodyRotationExpectedPole(const double tiltRad)
    {
        return FVector3d(FMath::Sin(tiltRad), 0.0, FMath::Cos(tiltRad));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two quaternions match component-wise within a tolerance, allowing q and -q (same rotation)
    bool BodyRotationQuatsNear(const FQuat4d& actual, const FQuat4d& expected, const double tolerance)
    {
        const bool same = FMath::Abs(actual.X - expected.X) <= tolerance && FMath::Abs(actual.Y - expected.Y) <= tolerance
            && FMath::Abs(actual.Z - expected.Z) <= tolerance && FMath::Abs(actual.W - expected.W) <= tolerance;
        const bool negated = FMath::Abs(actual.X + expected.X) <= tolerance && FMath::Abs(actual.Y + expected.Y) <= tolerance
            && FMath::Abs(actual.Z + expected.Z) <= tolerance && FMath::Abs(actual.W + expected.W) <= tolerance;
        return same || negated;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two quaternions rotate a set of probe vectors identically within a relative tolerance
    bool BodyRotationSameRotation(const FQuat4d& actual, const FQuat4d& expected, const double tolerance)
    {
        const FVector3d probes[] =
        {
            FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 0.0, 1.0),
            FVector3d(0.3, -0.7, 0.2), FVector3d(-2.0, 1.5, 4.0),
        };
        for (const FVector3d& probe : probes)
        {
            if (!SOLTestHelpers::VectorsNear(actual.RotateVector(probe), expected.RotateVector(probe), tolerance))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected full orientation per the contract: spin about the tilted pole applied on top of the tilt
    FQuat4d BodyRotationExpectedOrientation(const double tiltRad, const double w0Rad, const double periodH,
        const double seconds)
    {
        const FQuat4d tilt(FVector3d(0.0, 1.0, 0.0), tiltRad);
        const double spinAngle = w0Rad + UE_DOUBLE_TWO_PI * seconds / (periodH * BODY_ROTATION_SECONDS_PER_HOUR);
        const FQuat4d spin(BodyRotationExpectedPole(tiltRad), spinAngle);
        return spin * tilt;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationTiltPoleTest, "SOLTest.BodyRotation.TiltRotationPoleDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// TiltRotation tips ecliptic-north toward +X: the pole lands at (sin t, 0, cos t) and the result is a unit quaternion
bool FSOLBodyRotationTiltPoleTest::RunTest(const FString& /*parameters*/)
{
    const double tiltsDeg[] = { 0.0, 7.25, 23.44, 90.0, 97.77, 177.36, 180.0 };
    for (const double tiltDeg : tiltsDeg)
    {
        const double tiltRad = FMath::DegreesToRadians(tiltDeg);
        const FQuat4d tilt = SOLBodyRotation::TiltRotation(tiltRad);
        const FVector3d pole = tilt.RotateVector(BODY_ROTATION_NORTH);
        const FVector3d expected = BodyRotationExpectedPole(tiltRad);
        TestTrue(*FString::Printf(TEXT("tilt %.2f deg: pole (%.12f, %.12f, %.12f) == (%.12f, 0, %.12f)"), tiltDeg,
            pole.X, pole.Y, pole.Z, expected.X, expected.Z), SOLTestHelpers::VectorsNear(pole, expected, 1e-9));
        TestTrue(*FString::Printf(TEXT("tilt %.2f deg: unit quaternion"), tiltDeg),
            FMath::Abs(tilt.Size() - 1.0) < 1e-12);

        // Tilt is a rotation about +Y, so +Y itself is unmoved
        TestTrue(*FString::Printf(TEXT("tilt %.2f deg: +Y unchanged"), tiltDeg),
            SOLTestHelpers::VectorsNear(tilt.RotateVector(FVector3d(0.0, 1.0, 0.0)), FVector3d(0.0, 1.0, 0.0), 1e-12));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationTiltZeroTest, "SOLTest.BodyRotation.TiltRotationZeroIsIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// TiltRotation(0) is the identity rotation
bool FSOLBodyRotationTiltZeroTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d tilt = SOLBodyRotation::TiltRotation(0.0);
    TestTrue(TEXT("TiltRotation(0) components are identity"), BodyRotationQuatsNear(tilt, FQuat4d::Identity, 1e-15));
    TestTrue(TEXT("TiltRotation(0) rotates like identity"), BodyRotationSameRotation(tilt, FQuat4d::Identity, 1e-15));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationTiltMatchesAxisAngleTest,
    "SOLTest.BodyRotation.TiltRotationIsRotationAboutY",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// TiltRotation(t) rotates every vector the same as a rotation of t about ecliptic +Y
bool FSOLBodyRotationTiltMatchesAxisAngleTest::RunTest(const FString& /*parameters*/)
{
    const double tiltsDeg[] = { 0.034, 23.44, 97.77, 177.36 };
    for (const double tiltDeg : tiltsDeg)
    {
        const double tiltRad = FMath::DegreesToRadians(tiltDeg);
        TestTrue(*FString::Printf(TEXT("tilt %.3f deg rotates like FQuat4d(+Y, t)"), tiltDeg),
            BodyRotationSameRotation(SOLBodyRotation::TiltRotation(tiltRad), FQuat4d(FVector3d(0.0, 1.0, 0.0), tiltRad),
                1e-12));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationNoPeriodTest, "SOLTest.BodyRotation.NonPositivePeriodIsTiltOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// periodH <= 0 returns TiltRotation(tilt) unchanged, ignoring w0 and time, with no NaN from a divide by zero
bool FSOLBodyRotationNoPeriodTest::RunTest(const FString& /*parameters*/)
{
    const double periods[] = { 0.0, -24.0, -1.0e-9 };
    const double tiltsDeg[] = { 0.0, 23.44, 177.36 };
    const double w0sRad[] = { 0.0, 1.3, -4.0 };
    const double times[] = { 0.0, 21600.0, -3.0e8, 1.5e9 };
    for (const double periodH : periods)
    {
        for (const double tiltDeg : tiltsDeg)
        {
            const double tiltRad = FMath::DegreesToRadians(tiltDeg);
            const FQuat4d expected = SOLBodyRotation::TiltRotation(tiltRad);
            for (const double w0Rad : w0sRad)
            {
                for (const double time : times)
                {
                    const FQuat4d actual = SOLBodyRotation::ComputeOrientation(tiltRad, w0Rad, periodH, time);
                    const FString label = FString::Printf(TEXT("period %.3e h, tilt %.2f deg, w0 %.2f, t=%.0f"),
                        periodH, tiltDeg, w0Rad, time);
                    TestTrue(*FString::Printf(TEXT("%s: finite"), *label), FMath::IsFinite(actual.X)
                        && FMath::IsFinite(actual.Y) && FMath::IsFinite(actual.Z) && FMath::IsFinite(actual.W));
                    TestTrue(*FString::Printf(TEXT("%s: equals TiltRotation"), *label),
                        BodyRotationQuatsNear(actual, expected, 1e-15));
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationUntiltedSpinTest, "SOLTest.BodyRotation.UntiltedSpinAboutZ",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With no tilt and a 24 h period, t=0 is identity, 6 h is +90 deg about +Z (+X -> +Y), and 24 h returns to identity
bool FSOLBodyRotationUntiltedSpinTest::RunTest(const FString& /*parameters*/)
{
    const double periodH = 24.0;

    const FQuat4d atEpoch = SOLBodyRotation::ComputeOrientation(0.0, 0.0, periodH, 0.0);
    TestTrue(TEXT("t=0 is identity"), BodyRotationQuatsNear(atEpoch, FQuat4d::Identity, 1e-15));

    // A positive angle about +Z is counter-clockwise seen from +Z (q v q* with q = (0, 0, sin 45, cos 45)): +X -> +Y
    const FQuat4d quarter = SOLBodyRotation::ComputeOrientation(0.0, 0.0, periodH, 21600.0);
    TestTrue(TEXT("6 h: +X -> +Y"),
        SOLTestHelpers::VectorsNear(quarter.RotateVector(FVector3d(1.0, 0.0, 0.0)), FVector3d(0.0, 1.0, 0.0), 1e-12));
    TestTrue(TEXT("6 h: +Y -> -X"),
        SOLTestHelpers::VectorsNear(quarter.RotateVector(FVector3d(0.0, 1.0, 0.0)), FVector3d(-1.0, 0.0, 0.0), 1e-12));
    TestTrue(TEXT("6 h: +Z unchanged"),
        SOLTestHelpers::VectorsNear(quarter.RotateVector(BODY_ROTATION_NORTH), BODY_ROTATION_NORTH, 1e-12));
    TestTrue(TEXT("6 h: same rotation as FQuat4d(+Z, 90 deg)"),
        BodyRotationSameRotation(quarter, FQuat4d(BODY_ROTATION_NORTH, UE_DOUBLE_HALF_PI), 1e-12));

    const FQuat4d half = SOLBodyRotation::ComputeOrientation(0.0, 0.0, periodH, 43200.0);
    TestTrue(TEXT("12 h: +X -> -X"),
        SOLTestHelpers::VectorsNear(half.RotateVector(FVector3d(1.0, 0.0, 0.0)), FVector3d(-1.0, 0.0, 0.0), 1e-12));

    // A full turn is q = -identity or identity; both are the identity rotation
    const FQuat4d full = SOLBodyRotation::ComputeOrientation(0.0, 0.0, periodH, 86400.0);
    TestTrue(TEXT("24 h: back to identity"), BodyRotationQuatsNear(full, FQuat4d::Identity, 1e-12));
    TestTrue(TEXT("24 h: rotates like identity"), BodyRotationSameRotation(full, FQuat4d::Identity, 1e-12));

    // Negative time spins the other way
    const FQuat4d before = SOLBodyRotation::ComputeOrientation(0.0, 0.0, periodH, -21600.0);
    TestTrue(TEXT("-6 h: +X -> -Y"),
        SOLTestHelpers::VectorsNear(before.RotateVector(FVector3d(1.0, 0.0, 0.0)), FVector3d(0.0, -1.0, 0.0), 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationTiltedSpinTest, "SOLTest.BodyRotation.TiltedSpinKeepsPoleFixed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With tilt and spin, t=0 equals the tilt, the tilted pole never moves, and vectors perpendicular to it do sweep
bool FSOLBodyRotationTiltedSpinTest::RunTest(const FString& /*parameters*/)
{
    const double tiltRad = FMath::DegreesToRadians(23.44);
    const double periodH = 24.0;
    const FQuat4d tilt = SOLBodyRotation::TiltRotation(tiltRad);
    const FVector3d pole = tilt.RotateVector(BODY_ROTATION_NORTH);

    TestTrue(TEXT("t=0 equals TiltRotation"),
        BodyRotationQuatsNear(SOLBodyRotation::ComputeOrientation(tiltRad, 0.0, periodH, 0.0), tilt, 1e-12));

    // The pole is fixed at every time; spinning about the untilted +Z instead would move it
    const double times[] = { 1000.0, 21600.0, 43200.0, 60000.0, 86400.0 * 3.5, -1.0e7, 1.5e9 };
    for (const double time : times)
    {
        const FQuat4d orientation = SOLBodyRotation::ComputeOrientation(tiltRad, 0.0, periodH, time);
        TestTrue(*FString::Printf(TEXT("t=%.0f: unit quaternion"), time), FMath::Abs(orientation.Size() - 1.0) < 1e-12);
        TestTrue(*FString::Printf(TEXT("t=%.0f: spin part leaves the tilted pole unchanged"), time),
            SOLTestHelpers::VectorsNear((orientation * tilt.Inverse()).RotateVector(pole), pole, 1e-9));
        TestTrue(*FString::Printf(TEXT("t=%.0f: body north maps to the tilted pole"), time),
            SOLTestHelpers::VectorsNear(orientation.RotateVector(BODY_ROTATION_NORTH), pole, 1e-9));
    }

    // At a quarter period, +Y (perpendicular to the pole) is rotated 90 deg about the pole: pole x +Y
    const FVector3d yAxis(0.0, 1.0, 0.0);
    const FQuat4d quarter = SOLBodyRotation::ComputeOrientation(tiltRad, 0.0, periodH, 21600.0);
    const FQuat4d spinOnly = quarter * tilt.Inverse();
    TestTrue(TEXT("6 h: spin part sends +Y to pole x +Y"),
        SOLTestHelpers::VectorsNear(spinOnly.RotateVector(yAxis), pole ^ yAxis, 1e-9));
    TestFalse(TEXT("6 h: a vector perpendicular to the pole has moved"),
        SOLTestHelpers::VectorsNear(spinOnly.RotateVector(yAxis), yAxis, 1e-3));

    // The composition order: result rotates v by the tilt first, then by the spin about the tilted pole
    const FVector3d probe(0.3, -0.7, 0.2);
    const FQuat4d spin(pole, UE_DOUBLE_HALF_PI);
    TestTrue(TEXT("6 h: result.RotateVector(v) == spin.RotateVector(tilt.RotateVector(v))"),
        SOLTestHelpers::VectorsNear(quarter.RotateVector(probe), spin.RotateVector(tilt.RotateVector(probe)), 1e-9));

    // A full period returns to the tilt
    TestTrue(TEXT("24 h: back to TiltRotation"),
        BodyRotationSameRotation(SOLBodyRotation::ComputeOrientation(tiltRad, 0.0, periodH, 86400.0), tilt, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationW0Test, "SOLTest.BodyRotation.W0IsSpinAngleAtEpoch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At t=0 the spin angle is exactly w0: the result matches FQuat4d(tiltedPole, w0) * TiltRotation(tilt)
bool FSOLBodyRotationW0Test::RunTest(const FString& /*parameters*/)
{
    // Tilt (deg) and w0 (rad) pairs
    struct FSOLBodyRotationW0Case
    {
        double TiltDeg;
        double W0Rad;
    };
    const FSOLBodyRotationW0Case cases[] =
    {
        { 0.0, 1.0 },
        { 23.44, 0.75 },
        { 97.77, -2.5 },
        { 177.36, 4.0 },
    };
    for (const FSOLBodyRotationW0Case& testCase : cases)
    {
        const double tiltRad = FMath::DegreesToRadians(testCase.TiltDeg);
        const FQuat4d tilt = SOLBodyRotation::TiltRotation(tiltRad);
        const FVector3d pole = tilt.RotateVector(BODY_ROTATION_NORTH);
        const FQuat4d expected = FQuat4d(pole, testCase.W0Rad) * tilt;
        const FQuat4d actual = SOLBodyRotation::ComputeOrientation(tiltRad, testCase.W0Rad, 24.0, 0.0);
        TestTrue(*FString::Printf(TEXT("tilt %.2f deg, w0 %.2f: matches spin(w0) * tilt"), testCase.TiltDeg,
            testCase.W0Rad), BodyRotationSameRotation(actual, expected, 1e-12));
        TestTrue(*FString::Printf(TEXT("tilt %.2f deg, w0 %.2f: components match (up to sign)"), testCase.TiltDeg,
            testCase.W0Rad), BodyRotationQuatsNear(actual, expected.GetNormalized(), 1e-12));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationGeneralTest, "SOLTest.BodyRotation.GeneralSpinAngleFormula",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// For arbitrary inputs the result is spin(w0 + 2*PI*t/(P*3600)) about the tilted pole times the tilt, normalized
bool FSOLBodyRotationGeneralTest::RunTest(const FString& /*parameters*/)
{
    // Tilt (deg), w0 (rad), period (h), time (s)
    struct FSOLBodyRotationGeneralCase
    {
        double TiltDeg;
        double W0Rad;
        double PeriodH;
        double Seconds;
    };
    const FSOLBodyRotationGeneralCase cases[] =
    {
        { 23.44, 0.0, 23.9345, 12345.0 },
        { 25.19, 0.4, 24.6229, 1.0e6 },
        { 97.77, -1.1, 17.24, -7.5e5 },
        { 3.13, 2.0, 9.9250, 3.3e7 },
        { 177.36, 0.0, 5832.6, 4.0e6 },
        { 7.25, 0.0, 601.2, -2.0e8 },
    };
    for (const FSOLBodyRotationGeneralCase& testCase : cases)
    {
        const double tiltRad = FMath::DegreesToRadians(testCase.TiltDeg);
        const FQuat4d actual = SOLBodyRotation::ComputeOrientation(tiltRad, testCase.W0Rad, testCase.PeriodH,
            testCase.Seconds);
        const FQuat4d expected = BodyRotationExpectedOrientation(tiltRad, testCase.W0Rad, testCase.PeriodH,
            testCase.Seconds);
        const FString label = FString::Printf(TEXT("tilt %.2f, w0 %.2f, P %.4f h, t=%.0f"), testCase.TiltDeg,
            testCase.W0Rad, testCase.PeriodH, testCase.Seconds);
        TestTrue(*FString::Printf(TEXT("%s: unit quaternion"), *label), FMath::Abs(actual.Size() - 1.0) < 1e-12);
        TestTrue(*FString::Printf(TEXT("%s: matches contract"), *label),
            BodyRotationSameRotation(actual, expected, 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRotationRetrogradeTest, "SOLTest.BodyRotation.NearHalfTurnTiltIsConsistent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Venus-like 177.36 deg tilt points the pole mostly south and spin still runs about that pole per the contract
bool FSOLBodyRotationRetrogradeTest::RunTest(const FString& /*parameters*/)
{
    const double tiltRad = FMath::DegreesToRadians(177.36);
    const double periodH = 5832.6;
    const FQuat4d tilt = SOLBodyRotation::TiltRotation(tiltRad);
    TestTrue(TEXT("Unit quaternion"), FMath::Abs(tilt.Size() - 1.0) < 1e-12);

    const FVector3d pole = tilt.RotateVector(BODY_ROTATION_NORTH);
    TestTrue(TEXT("Pole == (sin 177.36, 0, cos 177.36)"),
        SOLTestHelpers::VectorsNear(pole, BodyRotationExpectedPole(tiltRad), 1e-9));
    TestTrue(*FString::Printf(TEXT("Pole points mostly south (z = %.6f)"), pole.Z), pole.Z < -0.99);

    // Quarter period: the pole is fixed and +Y (perpendicular to it) goes to pole x +Y under the spin part
    const double quarterSeconds = 0.25 * periodH * 3600.0;
    const FQuat4d quarter = SOLBodyRotation::ComputeOrientation(tiltRad, 0.0, periodH, quarterSeconds);
    TestTrue(TEXT("Quarter period: spin part leaves the pole unchanged"),
        SOLTestHelpers::VectorsNear((quarter * tilt.Inverse()).RotateVector(pole), pole, 1e-9));
    const FVector3d yAxis(0.0, 1.0, 0.0);
    TestTrue(TEXT("Quarter period: spin part sends +Y to pole x +Y"),
        SOLTestHelpers::VectorsNear((quarter * tilt.Inverse()).RotateVector(yAxis), pole ^ yAxis, 1e-9));
    TestTrue(TEXT("Quarter period matches contract"), BodyRotationSameRotation(quarter,
        BodyRotationExpectedOrientation(tiltRad, 0.0, periodH, quarterSeconds), 1e-9));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
