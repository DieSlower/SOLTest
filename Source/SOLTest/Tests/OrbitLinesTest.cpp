/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOrbitLines.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Expected number of points in one sampled ellipse
    constexpr int32 ORBITLINES_POINT_COUNT = SOLOrbitLines::EllipseSegments + 1;

    //////////////////////////////////////////////////////////////////////////
    // Returns Keplerian elements for a test orbit (mean anomaly deliberately non-zero: sampling must ignore it)
    FSOLKeplerElements OrbitLinesMakeElements(const double a, const double e, const double i, const double node,
        const double argPeri)
    {
        FSOLKeplerElements elements;
        elements.SemiMajorAxisM = a;
        elements.Eccentricity = e;
        elements.InclinationRad = i;
        elements.LongitudeOfAscendingNodeRad = node;
        elements.ArgumentOfPeriapsisRad = argPeri;
        elements.MeanAnomalyRad = 1.234;
        return elements;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a freshly sampled ellipse for the given elements
    TArray<FVector3d> OrbitLinesSample(const FSOLKeplerElements& elements)
    {
        TArray<FVector3d> points;
        points.Reserve(ORBITLINES_POINT_COUNT);
        SOLOrbitLines::SampleEllipse(elements, points);
        return points;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the prograde orbit normal for the ecliptic convention (independent of the code under test)
    FVector3d OrbitLinesExpectedNormal(const double i, const double node)
    {
        return FVector3d(FMath::Sin(i) * FMath::Sin(node), -FMath::Sin(i) * FMath::Cos(node), FMath::Cos(i));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unit periapsis direction for the ecliptic convention (independent of the code under test)
    FVector3d OrbitLinesExpectedPeriapsisDir(const double i, const double node, const double argPeri)
    {
        const double cosNode = FMath::Cos(node);
        const double sinNode = FMath::Sin(node);
        const double cosArg = FMath::Cos(argPeri);
        const double sinArg = FMath::Sin(argPeri);
        const double cosI = FMath::Cos(i);
        return FVector3d(cosNode * cosArg - sinNode * sinArg * cosI, sinNode * cosArg + cosNode * sinArg * cosI,
            sinArg * FMath::Sin(i));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinesCountTest, "SOLTest.OrbitLines.PointCountAndClosedLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Produces EllipseSegments+1 finite points whose first and last coincide
bool FSOLOrbitLinesCountTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("EllipseSegments is 90"), SOLOrbitLines::EllipseSegments, 90);

    const double a = 2.279e11;
    const TArray<FVector3d> points = OrbitLinesSample(OrbitLinesMakeElements(a, 0.0934, 0.0323, 0.865, 5.0));
    if (!TestEqual(TEXT("Point count"), points.Num(), ORBITLINES_POINT_COUNT))
    {
        return false;
    }
    TestTrue(TEXT("Closed loop: first == last"), (points[0] - points.Last()).Size() <= 1e-9 * a);

    // Every point is finite and no two consecutive points coincide
    for (int32 index = 0; index < points.Num(); ++index)
    {
        TestTrue(FString::Printf(TEXT("Point %d finite"), index), SOLTestHelpers::IsFiniteVector(points[index]));
        if (index > 0)
        {
            TestTrue(FString::Printf(TEXT("Point %d distinct from previous"), index),
                (points[index] - points[index - 1]).Size() > 1e-6 * a);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinesCircleTest, "SOLTest.OrbitLines.CircularOrbitRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A circular orbit samples every point at distance a from the parent
bool FSOLOrbitLinesCircleTest::RunTest(const FString& /*parameters*/)
{
    const double a = SOLTestHelpers::AU_M;
    const TArray<FVector3d> points = OrbitLinesSample(OrbitLinesMakeElements(a, 0.0, 0.4, 1.1, 0.3));
    if (!TestEqual(TEXT("Point count"), points.Num(), ORBITLINES_POINT_COUNT))
    {
        return false;
    }
    for (int32 index = 0; index < points.Num(); ++index)
    {
        TestTrue(FString::Printf(TEXT("Point %d at radius a"), index),
            SOLTestHelpers::RelativeError(points[index].Size(), a) < 1e-6);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinesEccentricTest, "SOLTest.OrbitLines.EccentricTrueAnomalySampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Samples start at periapsis a(1-e), reach apoapsis a(1+e) halfway, and follow r = a(1-e^2)/(1+e cos nu) uniformly in nu
bool FSOLOrbitLinesEccentricTest::RunTest(const FString& /*parameters*/)
{
    const double a = 5.0e10;
    const double e = 0.6;
    const double i = 0.5;
    const double node = 2.0;
    const double argPeri = 0.7;
    const TArray<FVector3d> points = OrbitLinesSample(OrbitLinesMakeElements(a, e, i, node, argPeri));
    if (!TestEqual(TEXT("Point count"), points.Num(), ORBITLINES_POINT_COUNT))
    {
        return false;
    }

    // Closest and farthest samples are the apsides
    double minRadius = TNumericLimits<double>::Max();
    double maxRadius = 0.0;
    for (const FVector3d& point : points)
    {
        minRadius = FMath::Min(minRadius, point.Size());
        maxRadius = FMath::Max(maxRadius, point.Size());
    }
    TestTrue(TEXT("Closest sample ~ a(1-e)"), SOLTestHelpers::RelativeError(minRadius, a * (1.0 - e)) < 1e-9);
    TestTrue(TEXT("Farthest sample ~ a(1+e)"), SOLTestHelpers::RelativeError(maxRadius, a * (1.0 + e)) < 1e-9);

    // First sample is periapsis along the expected direction; the middle sample is apoapsis opposite it
    const FVector3d periDir = OrbitLinesExpectedPeriapsisDir(i, node, argPeri);
    TestTrue(TEXT("First point is periapsis"), SOLTestHelpers::VectorsNear(points[0], periDir * a * (1.0 - e), 1e-9));
    TestTrue(TEXT("Middle point is apoapsis"),
        SOLTestHelpers::VectorsNear(points[SOLOrbitLines::EllipseSegments / 2], -periDir * a * (1.0 + e), 1e-9));

    // Each sample k lies at true anomaly 2*PI*k/EllipseSegments with the conic radius
    for (int32 index = 0; index < points.Num(); ++index)
    {
        const double nu = UE_DOUBLE_TWO_PI * index / SOLOrbitLines::EllipseSegments;
        const double expectedRadius = a * (1.0 - e * e) / (1.0 + e * FMath::Cos(nu));
        TestTrue(FString::Printf(TEXT("Point %d radius at nu = %.4f"), index, nu),
            SOLTestHelpers::RelativeError(points[index].Size(), expectedRadius) < 1e-9);
        const double cosAngle = FMath::Clamp(points[index].GetSafeNormal() | periDir, -1.0, 1.0);
        TestTrue(FString::Printf(TEXT("Point %d angle from periapsis matches nu"), index),
            FMath::Abs(cosAngle - FMath::Cos(nu)) < 1e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinesPlaneTest, "SOLTest.OrbitLines.OrbitalPlaneAndDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// All samples lie in the plane set by inclination and node, traversed prograde
bool FSOLOrbitLinesPlaneTest::RunTest(const FString& /*parameters*/)
{
    const double a = 1.0e9;
    const double i = 0.9;
    const double node = -1.3;
    const TArray<FVector3d> points = OrbitLinesSample(OrbitLinesMakeElements(a, 0.3, i, node, 2.2));
    if (!TestEqual(TEXT("Point count"), points.Num(), ORBITLINES_POINT_COUNT))
    {
        return false;
    }
    const FVector3d expectedNormal = OrbitLinesExpectedNormal(i, node);

    // Normal from three non-collinear samples, ordered by increasing true anomaly (prograde)
    const FVector3d sampledNormal = ((points[30] - points[0]) ^ (points[60] - points[0])).GetSafeNormal();
    TestTrue(TEXT("Sampled plane normal matches the orbit normal (prograde)"),
        SOLTestHelpers::VectorsNear(sampledNormal, expectedNormal, 1e-9));

    // Every point is in the plane through the parent
    for (int32 index = 0; index < points.Num(); ++index)
    {
        TestTrue(FString::Printf(TEXT("Point %d in the orbital plane"), index),
            FMath::Abs(points[index] | expectedNormal) < 1e-9 * a);
    }

    // An uninclined orbit stays in the ecliptic (Z == 0)
    const TArray<FVector3d> flat = OrbitLinesSample(OrbitLinesMakeElements(a, 0.3, 0.0, 0.4, 1.0));
    TestEqual(TEXT("Flat orbit point count"), flat.Num(), ORBITLINES_POINT_COUNT);
    for (const FVector3d& point : flat)
    {
        TestTrue(TEXT("Flat orbit point has Z == 0"), FMath::Abs(point.Z) < 1e-9 * a);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinesKeplerTest, "SOLTest.OrbitLines.MatchesKeplerPeriapsisState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The first sample equals SOLKepler's position at mean anomaly 0 (true anomaly 0) for the same shape and orientation
bool FSOLOrbitLinesKeplerTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeplerElements elements = OrbitLinesMakeElements(1.496e11, 0.2, 0.1, 0.5, 1.5);
    const TArray<FVector3d> points = OrbitLinesSample(elements);
    if (!TestEqual(TEXT("Point count"), points.Num(), ORBITLINES_POINT_COUNT))
    {
        return false;
    }
    elements.MeanAnomalyRad = 0.0;
    const FSOLOrbitState periapsis = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM, 0.0);
    TestTrue(TEXT("First sample == Kepler periapsis"), SOLTestHelpers::VectorsNear(points[0], periapsis.PositionM, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinesAppendTest, "SOLTest.OrbitLines.AppendsWithoutClearing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SampleEllipse appends to the caller's array, keeping existing elements and reserved capacity
bool FSOLOrbitLinesAppendTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d placeholder(1.0, 2.0, 3.0);
    const FSOLKeplerElements elements = OrbitLinesMakeElements(1.0e10, 0.1, 0.2, 0.3, 0.4);

    TArray<FVector3d> points;
    points.Reserve(1 + 2 * ORBITLINES_POINT_COUNT);
    const int32 reservedCapacity = points.Max();
    const FVector3d* data = points.GetData();
    points.Add(placeholder);

    SOLOrbitLines::SampleEllipse(elements, points);
    if (!TestEqual(TEXT("One placeholder + one ellipse"), points.Num(), 1 + ORBITLINES_POINT_COUNT))
    {
        return false;
    }
    TestTrue(TEXT("Placeholder preserved at index 0"), points[0] == placeholder);

    // A second ellipse appends after the first
    SOLOrbitLines::SampleEllipse(elements, points);
    TestEqual(TEXT("Placeholder + two ellipses"), points.Num(), 1 + 2 * ORBITLINES_POINT_COUNT);
    TestTrue(TEXT("Placeholder still preserved"), points[0] == placeholder);
    TestTrue(TEXT("Second ellipse repeats the first"),
        points.Num() == 1 + 2 * ORBITLINES_POINT_COUNT && points[1] == points[1 + ORBITLINES_POINT_COUNT]);
    TestEqual(TEXT("No reallocation beyond the reserved capacity"), points.Max(), reservedCapacity);
    TestTrue(TEXT("Buffer not reallocated"), points.GetData() == data);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
