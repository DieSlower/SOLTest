/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLRenderOrigin.h"

#include "Universe/SOLRenderPlacement.h"
#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Integer-valued universe point (exact in double) used as a starting origin, roughly 1 AU from the Sun
    const FVector3d RENDER_ORIGIN_BASE_M(149597870700.0, -20000000000.0, 3000000000.0);

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected Unreal-space cm offset of an ecliptic meter offset: (X, -Y, Z) * 100, independent of the code
    FVector3d ExpectedRenderOffsetCm(const FVector3d& eclipticOffsetM)
    {
        return FVector3d(eclipticOffsetM.X * 100.0, -eclipticOffsetM.Y * 100.0, eclipticOffsetM.Z * 100.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a render origin already reset to the given observer position
    FSOLRenderOrigin MakeRenderOriginAt(const FVector3d& observerM)
    {
        FSOLRenderOrigin origin;
        origin.Reset(observerM);
        return origin;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginResetTest, "SOLTest.RenderOrigin.ResetSetsOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Reset places the origin exactly at the observer, every time it is called
bool FSOLRenderOriginResetTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("SnapDistanceM is 10 km"), FSOLRenderOrigin::SnapDistanceM, 10000.0, 0.0);

    FSOLRenderOrigin origin;
    origin.Reset(RENDER_ORIGIN_BASE_M);
    TestTrue(TEXT("Reset sets OriginM to the observer"), origin.OriginM == RENDER_ORIGIN_BASE_M);

    // A second reset to a nearby point moves it even though the distance is below the snap distance
    const FVector3d nearby = RENDER_ORIGIN_BASE_M + FVector3d(1.0, 2.0, 3.0);
    origin.Reset(nearby);
    TestTrue(TEXT("Reset always moves the origin"), origin.OriginM == nearby);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginBelowSnapTest, "SOLTest.RenderOrigin.UpdateBelowSnapDistanceKeepsOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Update returns false and leaves the origin when the observer is less than SnapDistanceM from it
bool FSOLRenderOriginBelowSnapTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d offsets[] =
    {
        FVector3d::ZeroVector,
        FVector3d(9999.0, 0.0, 0.0),
        FVector3d(0.0, -9999.999, 0.0),
        FVector3d(5999.0, 0.0, -7999.0),     // |offset| ~ 9998.6 m
    };
    for (const FVector3d& offset : offsets)
    {
        FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
        const bool bMoved = origin.Update(RENDER_ORIGIN_BASE_M + offset, false);
        TestFalse(*FString::Printf(TEXT("Offset (%.3f, %.3f, %.3f): Update returns false"), offset.X, offset.Y,
            offset.Z), bMoved);
        TestTrue(*FString::Printf(TEXT("Offset (%.3f, %.3f, %.3f): origin unchanged"), offset.X, offset.Y, offset.Z),
            origin.OriginM == RENDER_ORIGIN_BASE_M);
    }

    // Repeated small drifts that stay within the snap distance of the (unchanged) origin never snap
    FSOLRenderOrigin drifting = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
    for (int32 step = 1; step <= 9; ++step)
    {
        TestFalse(*FString::Printf(TEXT("Drift step %d does not snap"), step),
            drifting.Update(RENDER_ORIGIN_BASE_M + FVector3d(1000.0 * step, 0.0, 0.0), false));
    }
    TestTrue(TEXT("Drifting origin unchanged"), drifting.OriginM == RENDER_ORIGIN_BASE_M);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginSnapTest, "SOLTest.RenderOrigin.UpdateAtAndBeyondSnapDistanceMoves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Update returns true and moves the origin to the observer at exactly SnapDistanceM and beyond
bool FSOLRenderOriginSnapTest::RunTest(const FString& /*parameters*/)
{
    // Offsets whose lengths are exactly representable: 10000, 10000 (6000-8000-0 triangle), 25000, 1 AU
    const FVector3d offsets[] =
    {
        FVector3d(10000.0, 0.0, 0.0),
        FVector3d(0.0, 6000.0, -8000.0),
        FVector3d(-25000.0, 0.0, 0.0),
        FVector3d(0.0, 0.0, 149597870700.0),
    };
    for (const FVector3d& offset : offsets)
    {
        FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
        const FVector3d observer = RENDER_ORIGIN_BASE_M + offset;
        const bool bMoved = origin.Update(observer, false);
        TestTrue(*FString::Printf(TEXT("Offset (%.1f, %.1f, %.1f): Update returns true"), offset.X, offset.Y,
            offset.Z), bMoved);
        TestTrue(*FString::Printf(TEXT("Offset (%.1f, %.1f, %.1f): origin moved to the observer"), offset.X, offset.Y,
            offset.Z), origin.OriginM == observer);

        // Immediately after snapping, the same observer is no longer far from the origin
        TestFalse(*FString::Printf(TEXT("Offset (%.1f, %.1f, %.1f): second Update does not snap"), offset.X,
            offset.Y, offset.Z), origin.Update(observer, false));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginForceSnapTest, "SOLTest.RenderOrigin.ForceSnapAlwaysMoves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// bForceSnap moves the origin to the observer and returns true, even with no movement
bool FSOLRenderOriginForceSnapTest::RunTest(const FString& /*parameters*/)
{
    FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
    TestTrue(TEXT("Forced snap with zero movement returns true"), origin.Update(RENDER_ORIGIN_BASE_M, true));
    TestTrue(TEXT("Forced snap with zero movement keeps the origin at the observer"),
        origin.OriginM == RENDER_ORIGIN_BASE_M);

    const FVector3d smallMove = RENDER_ORIGIN_BASE_M + FVector3d(1.0, -2.0, 3.0);
    TestTrue(TEXT("Forced snap with a small move returns true"), origin.Update(smallMove, true));
    TestTrue(TEXT("Forced snap with a small move moves the origin"), origin.OriginM == smallMove);

    const FVector3d bigMove = smallMove + FVector3d(50000.0, 0.0, 0.0);
    TestTrue(TEXT("Forced snap with a big move returns true"), origin.Update(bigMove, true));
    TestTrue(TEXT("Forced snap with a big move moves the origin"), origin.OriginM == bigMove);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginToRenderTest, "SOLTest.RenderOrigin.UniverseToRenderCmFlipsAndScales",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// UniverseToRenderCm subtracts the origin, negates Y (ecliptic to Unreal) and converts meters to centimeters
bool FSOLRenderOriginToRenderTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
    TestTrue(TEXT("The origin maps to Unreal (0,0,0)"),
        origin.UniverseToRenderCm(RENDER_ORIGIN_BASE_M).IsNearlyZero(0.0));

    // Integer offsets keep the arithmetic exact
    const FVector3d offsets[] =
    {
        FVector3d(1.0, 2.0, 3.0),
        FVector3d(-1500.0, 2500.0, -3500.0),
        FVector3d(4000000.0, -7000000.0, 123456.0),
    };
    for (const FVector3d& offset : offsets)
    {
        const FVector3d renderCm = origin.UniverseToRenderCm(RENDER_ORIGIN_BASE_M + offset);
        const FVector3d expected = ExpectedRenderOffsetCm(offset);
        TestTrue(*FString::Printf(TEXT("Offset (%.1f, %.1f, %.1f) -> (%.3f, %.3f, %.3f), expected (%.3f, %.3f, %.3f)"),
            offset.X, offset.Y, offset.Z, renderCm.X, renderCm.Y, renderCm.Z, expected.X, expected.Y, expected.Z),
            SOLTestHelpers::VectorsNear(renderCm, expected, 1e-12));
    }

    // Explicit axis check: +Y ecliptic is -Y Unreal
    const FVector3d yOnly = origin.UniverseToRenderCm(RENDER_ORIGIN_BASE_M + FVector3d(0.0, 10.0, 0.0));
    TestTrue(TEXT("+10 m ecliptic Y is -1000 cm Unreal Y"),
        SOLTestHelpers::VectorsNear(yOnly, FVector3d(0.0, -1000.0, 0.0), 1e-12));

    // An origin at the universe zero point gives a pure flip-and-scale
    const FSOLRenderOrigin zeroOrigin = MakeRenderOriginAt(FVector3d::ZeroVector);
    const FVector3d point(3.0e12, -4.0e12, 5.0e11);
    TestTrue(TEXT("Zero origin: pure flip-and-scale"),
        SOLTestHelpers::VectorsNear(zeroOrigin.UniverseToRenderCm(point), ExpectedRenderOffsetCm(point), 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginRoundTripTest, "SOLTest.RenderOrigin.RenderCmToUniverseMIsInverse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RenderCmToUniverseM inverts UniverseToRenderCm at ~1e13 m magnitudes and near zero
bool FSOLRenderOriginRoundTripTest::RunTest(const FString& /*parameters*/)
{
    // Origin and point pairs, from Neptune-scale magnitudes down to near zero
    struct FSOLRoundTripCase
    {
        FVector3d OriginM;
        FVector3d PointM;
    };
    const FSOLRoundTripCase cases[] =
    {
        { FVector3d(4.5e12, -1.0e13, 2.0e11), FVector3d(4.5e12 + 1234.5, -1.0e13 - 678.25, 2.0e11 + 9.75) },
        { FVector3d(4.5e12, -1.0e13, 2.0e11), FVector3d(-3.0e12, 7.0e12, -1.0e12) },
        { FVector3d(-1.0e13, 1.0e13, -1.0e13), FVector3d(1.0e13, 1.0e13, 1.0e13) },
        { FVector3d::ZeroVector, FVector3d(1.0e-3, -2.0e-3, 5.0e-4) },
        { FVector3d::ZeroVector, FVector3d(12.5, -0.25, 3.0) },
        { FVector3d(1.0, -1.0, 0.5), FVector3d(1.001, -0.998, 0.4995) },
    };
    for (const FSOLRoundTripCase& testCase : cases)
    {
        const FSOLRenderOrigin origin = MakeRenderOriginAt(testCase.OriginM);
        const FVector3d renderCm = origin.UniverseToRenderCm(testCase.PointM);
        const FVector3d back = origin.RenderCmToUniverseM(renderCm);
        const FString label = FString::Printf(TEXT("origin=(%.4e, %.4e, %.4e) point=(%.6e, %.6e, %.6e)"),
            testCase.OriginM.X, testCase.OriginM.Y, testCase.OriginM.Z, testCase.PointM.X, testCase.PointM.Y,
            testCase.PointM.Z);
        TestTrue(*FString::Printf(TEXT("%s: universe -> render -> universe"), *label),
            SOLTestHelpers::VectorsNear(back, testCase.PointM, 1e-9));
        TestTrue(*FString::Printf(TEXT("%s: render -> universe -> render"), *label),
            SOLTestHelpers::VectorsNear(origin.UniverseToRenderCm(origin.RenderCmToUniverseM(renderCm)), renderCm,
                1e-9));
    }

    // The inverse maps Unreal (0,0,0) to the origin and applies the flip and cm->m on the way back
    const FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
    TestTrue(TEXT("Render zero maps to the origin"),
        SOLTestHelpers::VectorsNear(origin.RenderCmToUniverseM(FVector3d::ZeroVector), RENDER_ORIGIN_BASE_M, 1e-15));
    TestTrue(TEXT("Render (100, -200, 300) cm maps to origin + (1, 2, 3) m"),
        SOLTestHelpers::VectorsNear(origin.RenderCmToUniverseM(FVector3d(100.0, -200.0, 300.0)),
            RENDER_ORIGIN_BASE_M + FVector3d(1.0, 2.0, 3.0), 1e-15));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginShiftTest, "SOLTest.RenderOrigin.RebaseShiftsRenderPositions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After a snap by delta, a fixed universe point's render position changes by exactly -EclipticToUnreal(delta) * 100
bool FSOLRenderOriginShiftTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d fixedPoints[] =
    {
        RENDER_ORIGIN_BASE_M + FVector3d(500.0, -700.0, 20.0),
        FVector3d::ZeroVector,
        FVector3d(-4500000000000.0, 1000000000000.0, 30000000000.0),
    };
    const FVector3d deltas[] =
    {
        FVector3d(10000.0, 0.0, 0.0),
        FVector3d(-3000.0, 40000.0, 12000.0),
        FVector3d(0.0, -2000000.0, 0.0),
    };
    for (const FVector3d& delta : deltas)
    {
        for (const FVector3d& point : fixedPoints)
        {
            FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
            const FVector3d before = origin.UniverseToRenderCm(point);
            TestTrue(*FString::Printf(TEXT("delta=(%.0f, %.0f, %.0f): Update snaps"), delta.X, delta.Y, delta.Z),
                origin.Update(RENDER_ORIGIN_BASE_M + delta, false));
            const FVector3d after = origin.UniverseToRenderCm(point);
            const FVector3d expectedShift = -ExpectedRenderOffsetCm(delta);
            const FVector3d shift = after - before;
            TestTrue(*FString::Printf(TEXT("delta=(%.0f, %.0f, %.0f) point=(%.3e, %.3e, %.3e): shift (%.6f, %.6f, %.6f)"
                " == (%.6f, %.6f, %.6f)"), delta.X, delta.Y, delta.Z, point.X, point.Y, point.Z, shift.X, shift.Y,
                shift.Z, expectedShift.X, expectedShift.Y, expectedShift.Z),
                SOLTestHelpers::VectorsNear(shift, expectedShift, 1e-9));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginBodyNearTest, "SOLTest.RenderOrigin.BodyPlacementWithinRangeIsOneToOne",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A body within range is placed at the observer's render location plus the 1:1 offset, i.e. at UniverseToRenderCm(body)
bool FSOLRenderOriginBodyNearTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
    const double maxCm = 1.0e9;
    const double radiusM = 100.0;

    // Observer at the origin, and observer drifted inside the snap distance
    const FVector3d observers[] =
    {
        RENDER_ORIGIN_BASE_M,
        RENDER_ORIGIN_BASE_M + FVector3d(1234.0, -567.0, 89.0),
    };
    const FVector3d bodyOffsetM(5000.0, 2000.0, -1000.0);
    for (const FVector3d& observer : observers)
    {
        const FVector3d body = observer + bodyOffsetM;
        const FSOLRenderPlacement placement = origin.BodyPlacement(body, observer, radiusM, maxCm);
        const FVector3d expected = ExpectedRenderOffsetCm(observer - RENDER_ORIGIN_BASE_M)
            + ExpectedRenderOffsetCm(bodyOffsetM);
        const FString label = FString::Printf(TEXT("observer offset (%.1f, %.1f, %.1f)"),
            observer.X - RENDER_ORIGIN_BASE_M.X, observer.Y - RENDER_ORIGIN_BASE_M.Y,
            observer.Z - RENDER_ORIGIN_BASE_M.Z);
        TestTrue(*FString::Printf(TEXT("%s: location (%.3f, %.3f, %.3f) == (%.3f, %.3f, %.3f)"), *label,
            placement.LocationCm.X, placement.LocationCm.Y, placement.LocationCm.Z, expected.X, expected.Y, expected.Z),
            SOLTestHelpers::VectorsNear(placement.LocationCm, expected, 1e-12));
        TestTrue(*FString::Printf(TEXT("%s: location == UniverseToRenderCm(body)"), *label),
            SOLTestHelpers::VectorsNear(placement.LocationCm, origin.UniverseToRenderCm(body), 1e-12));
        TestEqual(*FString::Printf(TEXT("%s: radius 1:1"), *label), placement.RadiusCm, radiusM * 100.0, 1e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginBodyFarTest, "SOLTest.RenderOrigin.BodyPlacementFarFollowsComputePlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A far body is ComputePlacement(body - observer) in Unreal axes, offset from the observer's render location
bool FSOLRenderOriginBodyFarTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
    const FVector3d observer = RENDER_ORIGIN_BASE_M + FVector3d(-3000.0, 4000.0, 250.0);
    const FVector3d observerRenderCm = ExpectedRenderOffsetCm(observer - RENDER_ORIGIN_BASE_M);
    const double maxCm = 1.0e11;

    // Relative offsets (meters) and radii of far bodies
    struct FSOLFarBodyCase
    {
        FVector3d RelativeM;
        double RadiusM;
    };
    const FSOLFarBodyCase cases[] =
    {
        { FVector3d(1.0e12, -3.0e11, 2.0e11), 6.9911e7 },
        { FVector3d(-1.5e11, 0.0, 0.0), 6.957e8 },
        { FVector3d(0.0, 4.2e10, -1.0e9), 6.0518e6 },
    };
    for (const FSOLFarBodyCase& testCase : cases)
    {
        const FVector3d body = observer + testCase.RelativeM;
        const FSOLRenderPlacement placement = origin.BodyPlacement(body, observer, testCase.RadiusM, maxCm);
        const FSOLRenderPlacement reference = SOLRender::ComputePlacement(body - observer, testCase.RadiusM, maxCm);
        const FVector3d expectedLocation = observerRenderCm + SOLRender::EclipticToUnreal(reference.LocationCm);
        const FVector3d fromObserver = placement.LocationCm - observerRenderCm;
        const FString label = FString::Printf(TEXT("rel=(%.3e, %.3e, %.3e)"), testCase.RelativeM.X,
            testCase.RelativeM.Y, testCase.RelativeM.Z);

        TestTrue(*FString::Printf(TEXT("%s: location matches the observer offset + ComputePlacement"), *label),
            SOLTestHelpers::VectorsNear(placement.LocationCm, expectedLocation, 1e-9));
        TestTrue(*FString::Printf(TEXT("%s: radius %.6e == ComputePlacement radius %.6e"), *label, placement.RadiusCm,
            reference.RadiusCm), SOLTestHelpers::RelativeError(placement.RadiusCm, reference.RadiusCm) < 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: direction from the observer is the flipped true direction"), *label),
            SOLTestHelpers::VectorsNear(fromObserver.GetSafeNormal(),
                ExpectedRenderOffsetCm(testCase.RelativeM).GetSafeNormal(), 1e-9));
        TestTrue(*FString::Printf(TEXT("%s: angular size preserved"), *label),
            SOLTestHelpers::RelativeError(placement.RadiusCm / FMath::Max(fromObserver.Size(), UE_DOUBLE_SMALL_NUMBER),
                testCase.RadiusM / testCase.RelativeM.Size()) < 1e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderOriginBodyAtObserverTest, "SOLTest.RenderOrigin.BodyAtObserverNoNaN",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A body at the observer's position yields finite values at the observer's render location with a 1:1 radius
bool FSOLRenderOriginBodyAtObserverTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRenderOrigin origin = MakeRenderOriginAt(RENDER_ORIGIN_BASE_M);
    const FVector3d observer = RENDER_ORIGIN_BASE_M + FVector3d(700.0, -800.0, 900.0);
    const FSOLRenderPlacement placement = origin.BodyPlacement(observer, observer, 10.0, 1.0e9);
    TestTrue(TEXT("Location is finite"), SOLTestHelpers::IsFiniteVector(placement.LocationCm));
    TestTrue(TEXT("Radius is finite"), FMath::IsFinite(placement.RadiusCm));
    TestTrue(TEXT("Location is the observer's render location"), SOLTestHelpers::VectorsNear(placement.LocationCm,
        ExpectedRenderOffsetCm(observer - RENDER_ORIGIN_BASE_M), 1e-12));
    TestEqual(TEXT("Radius is 1:1"), placement.RadiusCm, 1000.0, 1e-9);

    // Zero range as well
    const FSOLRenderPlacement zeroRange = origin.BodyPlacement(observer, observer, 3.0, 0.0);
    TestTrue(TEXT("Zero range: location finite"), SOLTestHelpers::IsFiniteVector(zeroRange.LocationCm));
    TestTrue(TEXT("Zero range: radius finite"), FMath::IsFinite(zeroRange.RadiusCm));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
