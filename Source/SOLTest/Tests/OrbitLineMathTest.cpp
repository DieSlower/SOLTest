/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "OrbitLines/SOLOrbitLineMath.h"

#include "SOLConstants.h"
#include "Tests/SOLTestHelpers.h"
#include "Universe/SOLKepler.h"
#include "Universe/SOLRenderPlacement.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Written against the "17a contract" in Docs/Plans/17-orbit-lines-plan.md, not against the implementation.
// Helpers here carry names unique across the test files so unity builds do not collide.
namespace
{
    using FOrbitLineKey = SOLOrbitLineMath::FLineKey;
    using EOrbitLineSource = SOLOrbitLineMath::ELineSource;

    // Reference values typed independently of production code
    constexpr double ORBITLINEMATH_AU_M = 149597870700.0;
    constexpr double ORBITLINEMATH_EARTH_RADIUS_M = 6.371e6;
    constexpr double ORBITLINEMATH_MOON_A_M = 3.844e8;
    constexpr double ORBITLINEMATH_MOON_RADIUS_M = 1.737e6;
    constexpr double ORBITLINEMATH_PLUTO_A_M = 5.9064e12;
    constexpr double ORBITLINEMATH_PLUTO_RADIUS_M = 1.188e6;
    constexpr double ORBITLINEMATH_MAX_RENDER_CM = 1.0e11;
    constexpr double ORBITLINEMATH_LOG_SCALE = 0.1;

    // Returns a quiet NaN
    double OrbitLineMathNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Returns positive infinity
    double OrbitLineMathInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    // Returns Keplerian elements for a test orbit (mean anomaly deliberately non-zero: sampling must ignore it)
    FSOLKeplerElements OrbitLineMathElements(const double a, const double e, const double i, const double node,
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

    // Returns the unit periapsis direction P for the 3-1-3 ecliptic convention (independent of the code under test)
    FVector3d OrbitLineMathP(const FSOLKeplerElements& el)
    {
        const double cn = FMath::Cos(el.LongitudeOfAscendingNodeRad);
        const double sn = FMath::Sin(el.LongitudeOfAscendingNodeRad);
        const double cw = FMath::Cos(el.ArgumentOfPeriapsisRad);
        const double sw = FMath::Sin(el.ArgumentOfPeriapsisRad);
        const double ci = FMath::Cos(el.InclinationRad);
        const double si = FMath::Sin(el.InclinationRad);
        return FVector3d(cn * cw - sn * sw * ci, sn * cw + cn * sw * ci, sw * si);
    }

    // Returns the unit in-plane direction Q (90 deg prograde from periapsis) for the 3-1-3 convention
    FVector3d OrbitLineMathQ(const FSOLKeplerElements& el)
    {
        const double cn = FMath::Cos(el.LongitudeOfAscendingNodeRad);
        const double sn = FMath::Sin(el.LongitudeOfAscendingNodeRad);
        const double cw = FMath::Cos(el.ArgumentOfPeriapsisRad);
        const double sw = FMath::Sin(el.ArgumentOfPeriapsisRad);
        const double ci = FMath::Cos(el.InclinationRad);
        const double si = FMath::Sin(el.InclinationRad);
        return FVector3d(-cn * sw - sn * cw * ci, -sn * sw + cn * cw * ci, cw * si);
    }

    // Returns the prograde orbit normal for the ecliptic convention
    FVector3d OrbitLineMathNormal(const FSOLKeplerElements& el)
    {
        const double i = el.InclinationRad;
        const double node = el.LongitudeOfAscendingNodeRad;
        return FVector3d(FMath::Sin(i) * FMath::Sin(node), -FMath::Sin(i) * FMath::Cos(node), FMath::Cos(i));
    }

    // Returns the ellipse point at eccentric anomaly E (parent at the focus), independent of the code under test
    FVector3d OrbitLineMathPointAtE(const FSOLKeplerElements& el, const double eccentricAnomaly)
    {
        const double a = el.SemiMajorAxisM;
        const double e = el.Eccentricity;
        const double b = a * FMath::Sqrt(1.0 - e * e);
        return OrbitLineMathP(el) * (a * (FMath::Cos(eccentricAnomaly) - e))
            + OrbitLineMathQ(el) * (b * FMath::Sin(eccentricAnomaly));
    }

    // Samples one orbit into fresh arrays; returns the function's result
    int32 OrbitLineMathSample(const FSOLKeplerElements& el, const int32 segments, TArray<FVector3d>& points,
        TArray<FVector3d>& tangents, TArray<double>& arcs)
    {
        points.Reset();
        tangents.Reset();
        arcs.Reset();
        return SOLOrbitLineMath::SampleOrbit(el, segments, points, tangents, arcs);
    }

    // Returns the distance from a point to the segment [a, b]
    double OrbitLineMathPointSegmentDistance(const FVector3d& point, const FVector3d& a, const FVector3d& b)
    {
        const FVector3d ab = b - a;
        const double lengthSq = ab.SizeSquared();
        const double t = lengthSq > 0.0 ? FMath::Clamp(((point - a) | ab) / lengthSq, 0.0, 1.0) : 0.0;
        return (point - (a + ab * t)).Size();
    }

    // Returns the Ramanujan (second) approximation of an ellipse perimeter
    double OrbitLineMathRamanujanPerimeter(const double a, const double b)
    {
        const double h = FMath::Square((a - b) / (a + b));
        return UE_DOUBLE_PI * (a + b) * (1.0 + 3.0 * h / (10.0 + FMath::Sqrt(4.0 - 3.0 * h)));
    }

    // Returns the largest index-wise distance between two equally long point lists
    double OrbitLineMathMaxDisplacement(const TArray<FVector3d>& first, const TArray<FVector3d>& second)
    {
        double maxDistance = 0.0;
        for (int32 index = 0; index < FMath::Min(first.Num(), second.Num()); ++index)
        {
            maxDistance = FMath::Max(maxDistance, (first[index] - second[index]).Size());
        }
        return maxDistance;
    }

    // Returns true when the vector has unit length within the tolerance
    bool OrbitLineMathIsUnit(const FVector3d& value, const double tolerance)
    {
        return FMath::Abs(value.Size() - 1.0) <= tolerance;
    }
}

//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathConstantsTest, "SOLTest.OrbitLineMath.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The tunables are self-consistent and match the grilled decisions
bool FSOLOrbitLineMathConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("MIN_SEGMENTS >= 3"), SOL::ORBIT_LINE_MIN_SEGMENTS >= 3);
    TestTrue(TEXT("MAX_SEGMENTS >= MIN_SEGMENTS"), SOL::ORBIT_LINE_MAX_SEGMENTS >= SOL::ORBIT_LINE_MIN_SEGMENTS);
    TestTrue(TEXT("Sagitta fraction in (0, 1)"),
        SOL::ORBIT_LINE_SAGITTA_RADIUS_FRACTION > 0.0 && SOL::ORBIT_LINE_SAGITTA_RADIUS_FRACTION < 1.0);
    TestTrue(TEXT("Rebuild fallback fraction > 0"), SOL::ORBIT_LINE_REBUILD_FALLBACK_FRACTION > 0.0);
    TestTrue(TEXT("At least one rebuild per frame"), SOL::ORBIT_LINE_MAX_REBUILDS_PER_FRAME >= 1);
    TestTrue(TEXT("At least one dash per orbit"), SOL::ORBIT_LINE_DASH_COUNT >= 1);
    TestTrue(TEXT("Dash duty in (0, 1)"), SOL::ORBIT_LINE_DASH_DUTY > 0.0 && SOL::ORBIT_LINE_DASH_DUTY < 1.0);
    TestTrue(TEXT("Width > 0"), SOL::ORBIT_LINE_WIDTH_PX > 0.0);
    TestEqual(TEXT("Reference height is 1080p"), SOL::ORBIT_LINE_REFERENCE_HEIGHT_PX, 1080.0);
    TestTrue(TEXT("AA margin > 0"), SOL::ORBIT_LINE_AA_MARGIN_PX > 0.0);
    TestTrue(TEXT("0 < fade start < fade end"),
        SOL::ORBIT_LINE_FADE_START_SMA > 0.0 && SOL::ORBIT_LINE_FADE_START_SMA < SOL::ORBIT_LINE_FADE_END_SMA);
    TestTrue(TEXT("Highlight floor in (0, 1)"),
        SOL::ORBIT_LINE_HIGHLIGHT_MIN_ALPHA > 0.0 && SOL::ORBIT_LINE_HIGHLIGHT_MIN_ALPHA < 1.0);
    TestEqual(TEXT("~50 largest named asteroids (decision 2)"), SOL::ORBIT_LINE_NAMED_MINOR_COUNT, 50);
    TestTrue(TEXT("At least one occluder"), SOL::ORBIT_LINE_MAX_OCCLUDERS >= 1);
    TestTrue(TEXT("Depth bias in (0, 0.1)"), SOL::ORBIT_LINE_DEPTH_BIAS > 0.0 && SOL::ORBIT_LINE_DEPTH_BIAS < 0.1);
    TestFalse(TEXT("Highlight color differs from the normal color (decision 3)"),
        SOL::ORBIT_LINE_HIGHLIGHT_COLOR.Equals(SOL::ORBIT_LINE_COLOR));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Drawable elements
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathDrawableTest, "SOLTest.OrbitLineMath.DrawableOrbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Bound, finite elements are drawable; zero, negative, NaN, infinite or unbound ones are not; M is ignored
bool FSOLOrbitLineMathDrawableTest::RunTest(const FString& /*parameters*/)
{
    const double nan = OrbitLineMathNaN();
    const double inf = OrbitLineMathInf();
    const FSOLKeplerElements valid = OrbitLineMathElements(ORBITLINEMATH_AU_M, 0.0167, 0.1, 0.2, 0.3);
    TestTrue(TEXT("Earth-like orbit"), SOLOrbitLineMath::IsDrawableOrbit(valid));
    TestTrue(TEXT("Circular"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, 0.0, 0.0, 0.0, 0.0)));
    TestTrue(TEXT("Nearly parabolic"),
        SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, 0.999999, 0.0, 0.0, 0.0)));

    FSOLKeplerElements nanMean = valid;
    nanMean.MeanAnomalyRad = nan;
    TestTrue(TEXT("NaN mean anomaly is ignored"), SOLOrbitLineMath::IsDrawableOrbit(nanMean));

    // Each invalid field on its own
    TestFalse(TEXT("a = 0"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(0.0, 0.1, 0.1, 0.1, 0.1)));
    TestFalse(TEXT("a < 0"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(-1.0e9, 0.1, 0.1, 0.1, 0.1)));
    TestFalse(TEXT("a NaN"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(nan, 0.1, 0.1, 0.1, 0.1)));
    TestFalse(TEXT("a inf"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(inf, 0.1, 0.1, 0.1, 0.1)));
    TestFalse(TEXT("e < 0"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, -0.01, 0.1, 0.1, 0.1)));
    TestFalse(TEXT("e = 1"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, 1.0, 0.1, 0.1, 0.1)));
    TestFalse(TEXT("e > 1"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, 1.2, 0.1, 0.1, 0.1)));
    TestFalse(TEXT("e NaN"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, nan, 0.1, 0.1, 0.1)));
    TestFalse(TEXT("i NaN"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, 0.1, nan, 0.1, 0.1)));
    TestFalse(TEXT("node inf"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, 0.1, 0.1, inf, 0.1)));
    TestFalse(TEXT("arg NaN"), SOLOrbitLineMath::IsDrawableOrbit(OrbitLineMathElements(1.0e9, 0.1, 0.1, 0.1, nan)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// SampleOrbit
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathSampleCountTest, "SOLTest.OrbitLineMath.SampleCountAndClosedLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// N segments give N+1 entries per array; the last point and tangent are exact copies of the first; arc starts at 0
bool FSOLOrbitLineMathSampleCountTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeplerElements el = OrbitLineMathElements(2.279e11, 0.0934, 0.0323, 0.865, 5.0);
    for (const int32 segments : { 3, 128, 1000, 8192 })
    {
        TArray<FVector3d> points;
        TArray<FVector3d> tangents;
        TArray<double> arcs;
        const int32 count = OrbitLineMathSample(el, segments, points, tangents, arcs);
        const FString label = FString::Printf(TEXT("N=%d"), segments);
        if (!TestEqual(label + TEXT(" returns N+1"), count, segments + 1)
            || !TestEqual(label + TEXT(" points"), points.Num(), segments + 1)
            || !TestEqual(label + TEXT(" tangents"), tangents.Num(), segments + 1)
            || !TestEqual(label + TEXT(" arcs"), arcs.Num(), segments + 1))
        {
            return false;
        }
        TestTrue(label + TEXT(" last point is an exact copy of the first"), points.Last() == points[0]);
        TestTrue(label + TEXT(" last tangent is an exact copy of the first"), tangents.Last() == tangents[0]);
        TestEqual(label + TEXT(" arc starts at exactly 0"), arcs[0], 0.0);

        // Every entry is finite
        bool bAllFinite = true;
        for (int32 index = 0; index < points.Num(); ++index)
        {
            bAllFinite &= SOLTestHelpers::IsFiniteVector(points[index])
                && SOLTestHelpers::IsFiniteVector(tangents[index])
                && FMath::IsFinite(arcs[index]);
        }
        TestTrue(label + TEXT(" all finite"), bAllFinite);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathCircularTest, "SOLTest.OrbitLineMath.CircularOrbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A circular orbit samples every point at radius a, evenly spaced; the polyline perimeter is N * 2a sin(PI/N)
bool FSOLOrbitLineMathCircularTest::RunTest(const FString& /*parameters*/)
{
    const double a = ORBITLINEMATH_AU_M;
    const int32 segments = 360;
    const FSOLKeplerElements el = OrbitLineMathElements(a, 0.0, 0.4, 1.1, 0.3);
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    if (!TestEqual(TEXT("Count"), OrbitLineMathSample(el, segments, points, tangents, arcs), segments + 1))
    {
        return false;
    }
    const double chord = 2.0 * a * FMath::Sin(UE_DOUBLE_PI / segments);
    for (int32 index = 0; index <= segments; ++index)
    {
        TestTrue(FString::Printf(TEXT("Point %d at radius a"), index),
            SOLTestHelpers::RelativeError(points[index].Size(), a) < 1e-9);
        TestTrue(FString::Printf(TEXT("Arc %d = k * chord"), index),
            FMath::Abs(arcs[index] - index * chord) <= 1e-9 * a);

        // On a circle the tangent is perpendicular to the radius
        TestTrue(FString::Printf(TEXT("Tangent %d perpendicular to radius"), index),
            FMath::Abs(points[index].GetSafeNormal() | tangents[index]) < 1e-9);
    }
    TestTrue(TEXT("Polyline perimeter"), SOLTestHelpers::RelativeError(arcs.Last(), segments * chord) < 1e-12);
    TestTrue(TEXT("Point 0 is along P (periapsis convention)"),
        SOLTestHelpers::VectorsNear(points[0], OrbitLineMathP(el) * a, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathEccentricTest, "SOLTest.OrbitLineMath.HighlyEccentricOrbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Halley-like orbit (e = 0.967, retrograde inclination): points uniform in E, on the true ellipse, in the plane
bool FSOLOrbitLineMathEccentricTest::RunTest(const FString& /*parameters*/)
{
    const double a = 2.667e12;
    const double e = 0.967;
    const double b = a * FMath::Sqrt(1.0 - e * e);
    const int32 segments = 512;
    const FSOLKeplerElements el = OrbitLineMathElements(a, e, 2.83, 1.02, 1.95);
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    if (!TestEqual(TEXT("Count"), OrbitLineMathSample(el, segments, points, tangents, arcs), segments + 1))
    {
        return false;
    }
    const FVector3d pHat = OrbitLineMathP(el);
    const FVector3d qHat = OrbitLineMathQ(el);
    const FVector3d normal = OrbitLineMathNormal(el);
    const FVector3d otherFocus = pHat * (-2.0 * a * e);

    // Apsides: point 0 is periapsis, point N/2 is apoapsis
    TestTrue(TEXT("Point 0 = periapsis a(1-e) P"),
        SOLTestHelpers::VectorsNear(points[0], pHat * (a * (1.0 - e)), 1e-9));
    TestTrue(TEXT("Point N/2 = apoapsis -a(1+e) P"),
        SOLTestHelpers::VectorsNear(points[segments / 2], pHat * (-a * (1.0 + e)), 1e-9));

    // Every sample: uniform eccentric anomaly, on the ellipse (focal distance sum 2a), in the plane
    for (int32 index = 0; index < segments; ++index)
    {
        const double eccentricAnomaly = UE_DOUBLE_TWO_PI * index / segments;
        const FVector3d& point = points[index];
        TestTrue(FString::Printf(TEXT("Point %d P component = a(cos E - e)"), index),
            FMath::Abs((point | pHat) - a * (FMath::Cos(eccentricAnomaly) - e)) <= 1e-9 * a);
        TestTrue(FString::Printf(TEXT("Point %d Q component = b sin E"), index),
            FMath::Abs((point | qHat) - b * FMath::Sin(eccentricAnomaly)) <= 1e-9 * a);
        TestTrue(FString::Printf(TEXT("Point %d in the plane"), index), FMath::Abs(point | normal) <= 1e-9 * a);
        TestTrue(FString::Printf(TEXT("Point %d focal sum = 2a"), index),
            SOLTestHelpers::RelativeError(point.Size() + (point - otherFocus).Size(), 2.0 * a) < 1e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathKeplerTest, "SOLTest.OrbitLineMath.MatchesKepler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Point 0 equals SOLKepler's position at M = 0, and the body's position at any M lies on the sampled ellipse
bool FSOLOrbitLineMathKeplerTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeplerElements el = OrbitLineMathElements(ORBITLINEMATH_AU_M, 0.2, 0.1, 0.5, 1.5);
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    if (!TestEqual(TEXT("Count"), OrbitLineMathSample(el, 256, points, tangents, arcs), 257))
    {
        return false;
    }
    el.MeanAnomalyRad = 0.0;
    const FSOLOrbitState periapsis = SOLKepler::ElementsToState(el, SOLTestHelpers::SUN_GM, 0.0);
    TestTrue(TEXT("Point 0 == Kepler periapsis"), SOLTestHelpers::VectorsNear(points[0], periapsis.PositionM, 1e-9));

    // The body itself, at several mean anomalies, lies on the ellipse the line follows
    const FVector3d normal = OrbitLineMathNormal(el);
    const FVector3d otherFocus = OrbitLineMathP(el) * (-2.0 * el.SemiMajorAxisM * el.Eccentricity);
    for (const double meanAnomaly : { 0.3, 1.7, 3.1, 4.9 })
    {
        el.MeanAnomalyRad = meanAnomaly;
        const FVector3d body = SOLKepler::ElementsToState(el, SOLTestHelpers::SUN_GM, 0.0).PositionM;
        TestTrue(FString::Printf(TEXT("Body at M=%.1f in the line's plane"), meanAnomaly),
            FMath::Abs(body | normal) <= 1e-9 * el.SemiMajorAxisM);
        TestTrue(FString::Printf(TEXT("Body at M=%.1f on the line's ellipse"), meanAnomaly),
            SOLTestHelpers::RelativeError(body.Size() + (body - otherFocus).Size(), 2.0 * el.SemiMajorAxisM) < 1e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathTangentTest, "SOLTest.OrbitLineMath.Tangents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Tangents are unit, along dPoint/dE, prograde, and agree with the neighbouring points
bool FSOLOrbitLineMathTangentTest::RunTest(const FString& /*parameters*/)
{
    const int32 segments = 256;
    const FSOLKeplerElements el = OrbitLineMathElements(5.0e10, 0.6, 0.5, 2.0, 0.7);
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    if (!TestEqual(TEXT("Count"), OrbitLineMathSample(el, segments, points, tangents, arcs), segments + 1))
    {
        return false;
    }
    const double a = el.SemiMajorAxisM;
    const double b = a * FMath::Sqrt(1.0 - el.Eccentricity * el.Eccentricity);
    const FVector3d pHat = OrbitLineMathP(el);
    const FVector3d qHat = OrbitLineMathQ(el);
    const FVector3d normal = OrbitLineMathNormal(el);
    for (int32 index = 0; index < segments; ++index)
    {
        const double eccentricAnomaly = UE_DOUBLE_TWO_PI * index / segments;
        const FVector3d expected = (pHat * (-a * FMath::Sin(eccentricAnomaly))
            + qHat * (b * FMath::Cos(eccentricAnomaly))).GetSafeNormal();
        TestTrue(FString::Printf(TEXT("Tangent %d unit"), index), OrbitLineMathIsUnit(tangents[index], 1e-12));
        TestTrue(FString::Printf(TEXT("Tangent %d along dPoint/dE"), index),
            SOLTestHelpers::VectorsNear(tangents[index], expected, 1e-9));
        TestTrue(FString::Printf(TEXT("Tangent %d prograde"), index),
            ((points[index] ^ tangents[index]) | normal) > 0.0);

        // Central difference of the neighbours points the same way
        const int32 previous = index == 0 ? segments - 1 : index - 1;
        const FVector3d central = (points[index + 1] - points[previous]).GetSafeNormal();
        TestTrue(FString::Printf(TEXT("Tangent %d agrees with neighbours"), index),
            (central | tangents[index]) > 0.999);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathArcTest, "SOLTest.OrbitLineMath.ArcLengths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Arc lengths are the cumulative chord lengths, strictly increasing, and approach the true ellipse perimeter
bool FSOLOrbitLineMathArcTest::RunTest(const FString& /*parameters*/)
{
    const double a = 1.0e11;
    const double e = 0.6;
    const double b = a * FMath::Sqrt(1.0 - e * e);
    const int32 segments = 2048;
    const FSOLKeplerElements el = OrbitLineMathElements(a, e, 0.2, 0.3, 0.4);
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    if (!TestEqual(TEXT("Count"), OrbitLineMathSample(el, segments, points, tangents, arcs), segments + 1))
    {
        return false;
    }

    // Each step is the chord between consecutive points, including the closing chord
    bool bChordsMatch = true;
    bool bIncreasing = true;
    for (int32 index = 1; index <= segments; ++index)
    {
        const double step = arcs[index] - arcs[index - 1];
        bChordsMatch &= FMath::Abs(step - (points[index] - points[index - 1]).Size()) <= 1e-6 * step + 1e-3;
        bIncreasing &= step > 0.0;
    }
    TestTrue(TEXT("Arc steps equal chord lengths"), bChordsMatch);
    TestTrue(TEXT("Arc strictly increasing"), bIncreasing);

    // The inscribed polyline is slightly shorter than the true perimeter
    const double truePerimeter = OrbitLineMathRamanujanPerimeter(a, b);
    TestTrue(TEXT("Perimeter close to the ellipse's"),
        SOLTestHelpers::RelativeError(arcs.Last(), truePerimeter) < 1e-5);
    TestTrue(TEXT("Polyline not longer than the ellipse"), arcs.Last() <= truePerimeter * (1.0 + 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathSampleInvalidTest, "SOLTest.OrbitLineMath.SampleRejectsInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Undrawable elements or fewer than 3 segments return 0 and leave the arrays untouched; a NaN M still samples
bool FSOLOrbitLineMathSampleInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double nan = OrbitLineMathNaN();
    const double inf = OrbitLineMathInf();
    const FVector3d sentinel(7.0, 8.0, 9.0);
    const TArray<FSOLKeplerElements> invalid = {
        OrbitLineMathElements(0.0, 0.1, 0.1, 0.1, 0.1),
        OrbitLineMathElements(-1.0e9, 0.1, 0.1, 0.1, 0.1),
        OrbitLineMathElements(nan, 0.1, 0.1, 0.1, 0.1),
        OrbitLineMathElements(inf, 0.1, 0.1, 0.1, 0.1),
        OrbitLineMathElements(1.0e9, 1.0, 0.1, 0.1, 0.1),
        OrbitLineMathElements(1.0e9, 1.5, 0.1, 0.1, 0.1),
        OrbitLineMathElements(1.0e9, -0.1, 0.1, 0.1, 0.1),
        OrbitLineMathElements(1.0e9, nan, 0.1, 0.1, 0.1),
        OrbitLineMathElements(1.0e9, 0.1, nan, 0.1, 0.1),
        OrbitLineMathElements(1.0e9, 0.1, 0.1, inf, 0.1),
        OrbitLineMathElements(1.0e9, 0.1, 0.1, 0.1, nan),
    };
    for (int32 caseIndex = 0; caseIndex < invalid.Num(); ++caseIndex)
    {
        TArray<FVector3d> points = { sentinel };
        TArray<FVector3d> tangents = { sentinel };
        TArray<double> arcs = { 42.0 };
        const int32 count = SOLOrbitLineMath::SampleOrbit(invalid[caseIndex], 64, points, tangents, arcs);
        TestEqual(FString::Printf(TEXT("Invalid case %d returns 0"), caseIndex), count, 0);
        TestTrue(FString::Printf(TEXT("Invalid case %d leaves the arrays untouched"), caseIndex),
            points.Num() == 1 && tangents.Num() == 1 && arcs.Num() == 1 && points[0] == sentinel
            && tangents[0] == sentinel && arcs[0] == 42.0);
    }

    // Too few segments
    const FSOLKeplerElements valid = OrbitLineMathElements(1.0e9, 0.1, 0.1, 0.1, 0.1);
    for (const int32 segments : { 2, 1, 0, -5 })
    {
        TArray<FVector3d> points;
        TArray<FVector3d> tangents;
        TArray<double> arcs;
        TestEqual(FString::Printf(TEXT("%d segments returns 0"), segments),
            SOLOrbitLineMath::SampleOrbit(valid, segments, points, tangents, arcs), 0);
        TestTrue(FString::Printf(TEXT("%d segments appends nothing"), segments),
            points.Num() == 0 && tangents.Num() == 0 && arcs.Num() == 0);
    }

    // The mean anomaly is ignored, even when NaN
    FSOLKeplerElements nanMean = valid;
    nanMean.MeanAnomalyRad = nan;
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    TestEqual(TEXT("NaN mean anomaly still samples"), OrbitLineMathSample(nanMean, 64, points, tangents, arcs), 65);
    TArray<FVector3d> referencePoints;
    OrbitLineMathSample(valid, 64, referencePoints, tangents, arcs);
    TestTrue(TEXT("Same points regardless of M"), OrbitLineMathMaxDisplacement(points, referencePoints) == 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathAppendTest, "SOLTest.OrbitLineMath.SampleAppends",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SampleOrbit appends after existing entries; each call's arc lengths start at 0
bool FSOLOrbitLineMathAppendTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d sentinel(1.0, 2.0, 3.0);
    const int32 segments = 100;
    const FSOLKeplerElements first = OrbitLineMathElements(1.0e10, 0.1, 0.2, 0.3, 0.4);
    const FSOLKeplerElements second = OrbitLineMathElements(3.0e10, 0.5, 0.6, 0.7, 0.8);
    TArray<FVector3d> points = { sentinel };
    TArray<FVector3d> tangents = { sentinel };
    TArray<double> arcs = { 99.0 };

    TestEqual(TEXT("First call"), SOLOrbitLineMath::SampleOrbit(first, segments, points, tangents, arcs), segments + 1);
    TestEqual(TEXT("Second call"), SOLOrbitLineMath::SampleOrbit(second, segments, points, tangents, arcs),
        segments + 1);
    if (!TestEqual(TEXT("Sentinel + two orbits"), points.Num(), 1 + 2 * (segments + 1))
        || !TestEqual(TEXT("Tangent count"), tangents.Num(), points.Num())
        || !TestEqual(TEXT("Arc count"), arcs.Num(), points.Num()))
    {
        return false;
    }
    TestTrue(TEXT("Sentinels preserved"), points[0] == sentinel && tangents[0] == sentinel && arcs[0] == 99.0);
    TestEqual(TEXT("First orbit's arc starts at 0"), arcs[1], 0.0);
    TestEqual(TEXT("Second orbit's arc starts at 0"), arcs[1 + segments + 1], 0.0);

    // The second block equals a fresh sample of the second orbit
    TArray<FVector3d> freshPoints;
    TArray<FVector3d> freshTangents;
    TArray<double> freshArcs;
    OrbitLineMathSample(second, segments, freshPoints, freshTangents, freshArcs);
    TestTrue(TEXT("Second block matches a fresh sample"),
        points[1 + segments + 1] == freshPoints[0] && points.Last() == freshPoints.Last()
        && arcs.Last() == freshArcs.Last());
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Chord error and segment counts
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathSagittaTest, "SOLTest.OrbitLineMath.ChordSagitta",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// a (2 PI / N)^2 / 8, a true bound on every chord's gap to the ellipse, even at e = 0.9; invalid inputs
bool FSOLOrbitLineMathSagittaTest::RunTest(const FString& /*parameters*/)
{
    const double a = 1.0e9;
    const int32 segments = 64;
    const double expected = a * FMath::Square(UE_DOUBLE_TWO_PI / segments) / 8.0;
    TestTrue(TEXT("Formula"), SOLTestHelpers::RelativeError(SOLOrbitLineMath::ChordSagittaM(a, segments), expected)
        < 1e-12);
    TestEqual(TEXT("a = 0 gives 0"), SOLOrbitLineMath::ChordSagittaM(0.0, segments), 0.0);
    TestEqual(TEXT("a NaN gives 0"), SOLOrbitLineMath::ChordSagittaM(OrbitLineMathNaN(), segments), 0.0);
    TestEqual(TEXT("a < 0 and N < 3 gives 0 (a checked first)"), SOLOrbitLineMath::ChordSagittaM(-1.0, 2), 0.0);
    TestTrue(TEXT("N = 2 gives +inf"), std::isinf(SOLOrbitLineMath::ChordSagittaM(a, 2))
        && SOLOrbitLineMath::ChordSagittaM(a, 2) > 0.0);

    // Bound property: the true arc between samples never strays from its chord by more than the sagitta
    for (const double e : { 0.0, 0.5, 0.9 })
    {
        const FSOLKeplerElements el = OrbitLineMathElements(a, e, 0.3, 0.4, 0.5);
        TArray<FVector3d> points;
        TArray<FVector3d> tangents;
        TArray<double> arcs;
        if (!TestEqual(TEXT("Count"), OrbitLineMathSample(el, segments, points, tangents, arcs), segments + 1))
        {
            return false;
        }
        double worst = 0.0;
        for (int32 index = 0; index < segments; ++index)
        {
            for (int32 sub = 1; sub < 8; ++sub)
            {
                const double eccentricAnomaly = UE_DOUBLE_TWO_PI * (index + sub / 8.0) / segments;
                worst = FMath::Max(worst, OrbitLineMathPointSegmentDistance(OrbitLineMathPointAtE(el, eccentricAnomaly),
                    points[index], points[index + 1]));
            }
        }
        TestTrue(FString::Printf(TEXT("e=%.1f: worst chord gap within the sagitta bound"), e),
            worst <= SOLOrbitLineMath::ChordSagittaM(a, segments) * (1.0 + 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathSegmentCountTest, "SOLTest.OrbitLineMath.SegmentCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The smallest N meeting the radius-fraction sagitta, clamped; invalid a gives MIN, invalid R gives MAX; monotone
bool FSOLOrbitLineMathSegmentCountTest::RunTest(const FString& /*parameters*/)
{
    const double fraction = SOL::ORBIT_LINE_SAGITTA_RADIUS_FRACTION;
    const int32 minSegments = SOL::ORBIT_LINE_MIN_SEGMENTS;
    const int32 maxSegments = SOL::ORBIT_LINE_MAX_SEGMENTS;

    // Smallest-N property for real orbits (Earth, Moon, Pluto)
    struct FCase { const TCHAR* Name; double A; double R; };
    const FCase cases[] = {
        { TEXT("Earth"), ORBITLINEMATH_AU_M, ORBITLINEMATH_EARTH_RADIUS_M },
        { TEXT("Moon"), ORBITLINEMATH_MOON_A_M, ORBITLINEMATH_MOON_RADIUS_M },
        { TEXT("Pluto"), ORBITLINEMATH_PLUTO_A_M, ORBITLINEMATH_PLUTO_RADIUS_M },
    };
    for (const FCase& testCase : cases)
    {
        const int32 count = SOLOrbitLineMath::SegmentCountFor(testCase.A, testCase.R);
        const double tolerance = fraction * testCase.R;
        const double rawNeeded = UE_DOUBLE_TWO_PI / FMath::Sqrt(8.0 * tolerance / testCase.A);
        TestTrue(FString::Printf(TEXT("%s within [MIN, MAX]"), testCase.Name),
            count >= minSegments && count <= maxSegments);
        if (rawNeeded > maxSegments + 1)
        {
            TestEqual(FString::Printf(TEXT("%s clamped to MAX"), testCase.Name), count, maxSegments);
        }
        else if (count > minSegments)
        {
            const double sagitta = testCase.A * FMath::Square(UE_DOUBLE_TWO_PI / count) / 8.0;
            const double sagittaFewer = testCase.A * FMath::Square(UE_DOUBLE_TWO_PI / (count - 1)) / 8.0;
            TestTrue(FString::Printf(TEXT("%s meets the tolerance"), testCase.Name),
                sagitta <= tolerance * (1.0 + 1e-12));
            TestTrue(FString::Printf(TEXT("%s is the smallest such N"), testCase.Name), sagittaFewer > tolerance);
        }
    }

    // Pluto needs ~22,000 segments at 5% of its radius: beyond any sensible cap
    TestEqual(TEXT("Pluto capped at MAX"),
        SOLOrbitLineMath::SegmentCountFor(ORBITLINEMATH_PLUTO_A_M, ORBITLINEMATH_PLUTO_RADIUS_M), maxSegments);

    // A tiny orbit around a big body needs fewer than MIN
    TestEqual(TEXT("Tiny orbit clamped to MIN"), SOLOrbitLineMath::SegmentCountFor(1.0e6, 1.0e6), minSegments);

    // Invalid inputs
    const double nan = OrbitLineMathNaN();
    TestEqual(TEXT("a = 0 gives MIN"), SOLOrbitLineMath::SegmentCountFor(0.0, 1.0e6), minSegments);
    TestEqual(TEXT("a NaN gives MIN"), SOLOrbitLineMath::SegmentCountFor(nan, 1.0e6), minSegments);
    TestEqual(TEXT("a < 0 gives MIN"), SOLOrbitLineMath::SegmentCountFor(-1.0e9, 1.0e6), minSegments);
    TestEqual(TEXT("R = 0 gives MAX"), SOLOrbitLineMath::SegmentCountFor(ORBITLINEMATH_AU_M, 0.0), maxSegments);
    TestEqual(TEXT("R NaN gives MAX"), SOLOrbitLineMath::SegmentCountFor(ORBITLINEMATH_AU_M, nan), maxSegments);
    TestEqual(TEXT("R < 0 gives MAX"), SOLOrbitLineMath::SegmentCountFor(ORBITLINEMATH_AU_M, -5.0), maxSegments);

    // Monotone: non-decreasing in a, non-increasing in R
    int32 previous = 0;
    bool bNonDecreasingInA = true;
    for (double a = 1.0e7; a < 1.0e13; a *= 1.7)
    {
        const int32 count = SOLOrbitLineMath::SegmentCountFor(a, 1.0e6);
        bNonDecreasingInA &= count >= previous;
        previous = count;
    }
    TestTrue(TEXT("Non-decreasing in a"), bNonDecreasingInA);
    previous = TNumericLimits<int32>::Max();
    bool bNonIncreasingInR = true;
    for (double radius = 1.0e3; radius < 1.0e9; radius *= 1.7)
    {
        const int32 count = SOLOrbitLineMath::SegmentCountFor(ORBITLINEMATH_AU_M, radius);
        bNonIncreasingInR &= count <= previous;
        previous = count;
    }
    TestTrue(TEXT("Non-increasing in R"), bNonIncreasingInR);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Dashes
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathDashPhaseTest, "SOLTest.OrbitLineMath.DashPhase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Phase in dash periods: 0 at the start, dashCount after one loop (seamless), 0 for invalid input
bool FSOLOrbitLineMathDashPhaseTest::RunTest(const FString& /*parameters*/)
{
    const double perimeter = 9.4e11;
    const int32 dashes = 120;
    TestEqual(TEXT("Start"), SOLOrbitLineMath::DashPhase(0.0, perimeter, dashes), 0.0);
    TestTrue(TEXT("Half loop"), FMath::Abs(SOLOrbitLineMath::DashPhase(perimeter * 0.5, perimeter, dashes) - 60.0)
        < 1e-9);
    const double full = SOLOrbitLineMath::DashPhase(perimeter, perimeter, dashes);
    TestTrue(TEXT("Full loop = dashCount"), FMath::Abs(full - dashes) < 1e-9);

    // The loop closes on a whole dash: the filtered coverage at the end equals the coverage at the start
    const double duty = 0.6;
    TestTrue(TEXT("Seamless closure"), FMath::Abs(SOLOrbitLineMath::DashCoverage(full, duty, 0.01)
        - SOLOrbitLineMath::DashCoverage(0.0, duty, 0.01)) < 1e-6);

    // Dash length is fixed in world arc length: twice the arc, twice the phase
    const double phase1 = SOLOrbitLineMath::DashPhase(1.0e9, perimeter, dashes);
    const double phase2 = SOLOrbitLineMath::DashPhase(2.0e9, perimeter, dashes);
    TestTrue(TEXT("Linear in arc length"), FMath::Abs(phase2 - 2.0 * phase1) < 1e-12);

    // Invalid inputs
    TestEqual(TEXT("Perimeter 0"), SOLOrbitLineMath::DashPhase(1.0, 0.0, dashes), 0.0);
    TestEqual(TEXT("Perimeter < 0"), SOLOrbitLineMath::DashPhase(1.0, -5.0, dashes), 0.0);
    TestEqual(TEXT("Perimeter NaN"), SOLOrbitLineMath::DashPhase(1.0, OrbitLineMathNaN(), dashes), 0.0);
    TestEqual(TEXT("Perimeter inf"), SOLOrbitLineMath::DashPhase(1.0, OrbitLineMathInf(), dashes), 0.0);
    TestEqual(TEXT("dashCount 0"), SOLOrbitLineMath::DashPhase(1.0, perimeter, 0), 0.0);
    TestEqual(TEXT("dashCount < 0"), SOLOrbitLineMath::DashPhase(1.0, perimeter, -3), 0.0);
    TestEqual(TEXT("Arc NaN"), SOLOrbitLineMath::DashPhase(OrbitLineMathNaN(), perimeter, dashes), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathDashCoverageTest, "SOLTest.OrbitLineMath.DashCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Point sample, exact box filter at dash edges, average once a period fits in a pixel, clamps and NaN
bool FSOLOrbitLineMathDashCoverageTest::RunTest(const FString& /*parameters*/)
{
    const double duty = 0.6;

    // Point sample (footprint <= 0 or NaN), periodic, negative phases wrap
    TestEqual(TEXT("In dash"), SOLOrbitLineMath::DashCoverage(0.3, duty, 0.0), 1.0);
    TestEqual(TEXT("In gap"), SOLOrbitLineMath::DashCoverage(0.7, duty, 0.0), 0.0);
    TestEqual(TEXT("Periodic: 5.3"), SOLOrbitLineMath::DashCoverage(5.3, duty, 0.0), 1.0);
    TestEqual(TEXT("Periodic: 5.7"), SOLOrbitLineMath::DashCoverage(5.7, duty, 0.0), 0.0);
    TestEqual(TEXT("Negative phase wraps: -0.7 -> 0.3"), SOLOrbitLineMath::DashCoverage(-0.7, duty, 0.0), 1.0);
    TestEqual(TEXT("Negative phase wraps: -0.2 -> 0.8"), SOLOrbitLineMath::DashCoverage(-0.2, duty, 0.0), 0.0);
    TestEqual(TEXT("Negative footprint is a point sample"), SOLOrbitLineMath::DashCoverage(0.3, duty, -1.0), 1.0);
    TestEqual(TEXT("NaN footprint is a point sample"), SOLOrbitLineMath::DashCoverage(0.7, duty, OrbitLineMathNaN()),
        0.0);

    // Box filter: exact mean of the square wave over the window
    const double footprint = 0.1;
    TestTrue(TEXT("Window inside a dash"), FMath::Abs(SOLOrbitLineMath::DashCoverage(0.3, duty, footprint) - 1.0)
        < 1e-9);
    TestTrue(TEXT("Window inside a gap"), FMath::Abs(SOLOrbitLineMath::DashCoverage(0.8, duty, footprint)) < 1e-9);
    TestTrue(TEXT("Window centred on the dash's end"),
        FMath::Abs(SOLOrbitLineMath::DashCoverage(duty, duty, footprint) - 0.5) < 1e-9);
    TestTrue(TEXT("Window centred on the dash's start"),
        FMath::Abs(SOLOrbitLineMath::DashCoverage(3.0, duty, footprint) - 0.5) < 1e-9);
    TestTrue(TEXT("Window a quarter into the dash's end"),
        FMath::Abs(SOLOrbitLineMath::DashCoverage(duty - 0.025, duty, footprint) - 0.75) < 1e-9);

    // A whole period or more in one pixel: the average (no shimmer)
    TestEqual(TEXT("Footprint 1 gives duty"), SOLOrbitLineMath::DashCoverage(0.37, duty, 1.0), duty);
    TestEqual(TEXT("Footprint 3.3 gives duty"), SOLOrbitLineMath::DashCoverage(0.91, duty, 3.3), duty);
    TestEqual(TEXT("Footprint inf gives duty"), SOLOrbitLineMath::DashCoverage(0.1, duty, OrbitLineMathInf()), duty);

    // Continuity toward the average: just below a full period the coverage is near duty everywhere
    for (double phase = 0.0; phase < 1.0; phase += 0.0625)
    {
        TestTrue(FString::Printf(TEXT("Footprint 0.999 near duty at %.4f"), phase),
            FMath::Abs(SOLOrbitLineMath::DashCoverage(phase, duty, 0.999) - duty) < 0.01);
    }

    // Always in [0, 1]; duty clamped; NaN phase
    bool bInRange = true;
    for (double phase = -3.0; phase < 3.0; phase += 0.037)
    {
        for (const double fp : { 0.0, 0.01, 0.3, 0.77, 2.0 })
        {
            const double coverage = SOLOrbitLineMath::DashCoverage(phase, duty, fp);
            bInRange &= coverage >= 0.0 && coverage <= 1.0;
        }
    }
    TestTrue(TEXT("Coverage in [0, 1]"), bInRange);
    TestEqual(TEXT("Duty > 1 clamps to 1 (solid)"), SOLOrbitLineMath::DashCoverage(0.9, 1.5, 0.0), 1.0);
    TestEqual(TEXT("Duty < 0 clamps to 0 (hidden)"), SOLOrbitLineMath::DashCoverage(0.1, -1.0, 0.0), 0.0);
    TestEqual(TEXT("Duty NaN counts as 0"), SOLOrbitLineMath::DashCoverage(0.1, OrbitLineMathNaN(), 0.0), 0.0);
    TestEqual(TEXT("NaN phase gives 0"), SOLOrbitLineMath::DashCoverage(OrbitLineMathNaN(), duty, 0.1), 0.0);
    TestEqual(TEXT("Inf phase gives 0"), SOLOrbitLineMath::DashCoverage(OrbitLineMathInf(), duty, 0.1), 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Width, anti-aliasing and screen-space expansion
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathWidthTest, "SOLTest.OrbitLineMath.WidthAndCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Width scales with the viewport height; coverage is analytic with a 1 px minimum footprint; ribbon extent covers it
bool FSOLOrbitLineMathWidthTest::RunTest(const FString& /*parameters*/)
{
    const double baseWidth = SOL::ORBIT_LINE_WIDTH_PX;
    TestTrue(TEXT("1080p gives the base width"),
        FMath::Abs(SOLOrbitLineMath::ViewportPixelWidth(1080.0) - baseWidth) < 1e-12);
    TestTrue(TEXT("4K gives twice the base width"),
        FMath::Abs(SOLOrbitLineMath::ViewportPixelWidth(2160.0) - 2.0 * baseWidth) < 1e-12);
    TestTrue(TEXT("720p gives two thirds"),
        FMath::Abs(SOLOrbitLineMath::ViewportPixelWidth(720.0) - baseWidth * 720.0 / 1080.0) < 1e-12);
    TestEqual(TEXT("Height 0 gives the base"), SOLOrbitLineMath::ViewportPixelWidth(0.0), baseWidth);
    TestEqual(TEXT("Height < 0 gives the base"), SOLOrbitLineMath::ViewportPixelWidth(-1.0), baseWidth);
    TestEqual(TEXT("Height NaN gives the base"), SOLOrbitLineMath::ViewportPixelWidth(OrbitLineMathNaN()), baseWidth);

    // LineCoverage for a 2 px line: solid core, half at the edge, zero beyond the feather
    TestEqual(TEXT("2 px centre"), SOLOrbitLineMath::LineCoverage(0.0, 2.0), 1.0);
    TestTrue(TEXT("2 px at 1 px"), FMath::Abs(SOLOrbitLineMath::LineCoverage(1.0, 2.0) - 0.5) < 1e-12);
    TestTrue(TEXT("2 px at -1 px (even)"), FMath::Abs(SOLOrbitLineMath::LineCoverage(-1.0, 2.0) - 0.5) < 1e-12);
    TestEqual(TEXT("2 px at 1.5 px"), SOLOrbitLineMath::LineCoverage(1.5, 2.0), 0.0);
    TestEqual(TEXT("2 px far away"), SOLOrbitLineMath::LineCoverage(10.0, 2.0), 0.0);

    // Thin lines keep a 1 px footprint at reduced intensity (no flicker)
    TestTrue(TEXT("0.5 px centre = 0.5"), FMath::Abs(SOLOrbitLineMath::LineCoverage(0.0, 0.5) - 0.5) < 1e-12);
    TestTrue(TEXT("0.5 px at 0.5 px = 0.25"), FMath::Abs(SOLOrbitLineMath::LineCoverage(0.5, 0.5) - 0.25) < 1e-12);
    TestTrue(TEXT("1 px centre = 1"), FMath::Abs(SOLOrbitLineMath::LineCoverage(0.0, 1.0) - 1.0) < 1e-12);

    // Invalid inputs
    TestEqual(TEXT("Width 0"), SOLOrbitLineMath::LineCoverage(0.0, 0.0), 0.0);
    TestEqual(TEXT("Width < 0"), SOLOrbitLineMath::LineCoverage(0.0, -1.0), 0.0);
    TestEqual(TEXT("Width NaN"), SOLOrbitLineMath::LineCoverage(0.0, OrbitLineMathNaN()), 0.0);
    TestEqual(TEXT("Distance NaN"), SOLOrbitLineMath::LineCoverage(OrbitLineMathNaN(), 2.0), 0.0);

    // Ribbon half extent: core plus the feather margin, and the coverage is zero at its edge
    TestTrue(TEXT("Extent for 2 px"), FMath::Abs(SOLOrbitLineMath::RibbonHalfExtentPx(2.0)
        - (1.0 + SOL::ORBIT_LINE_AA_MARGIN_PX)) < 1e-12);
    TestTrue(TEXT("Extent for 0.5 px uses the 1 px footprint"), FMath::Abs(SOLOrbitLineMath::RibbonHalfExtentPx(0.5)
        - (0.5 + SOL::ORBIT_LINE_AA_MARGIN_PX)) < 1e-12);
    TestTrue(TEXT("Extent for invalid width counts as 1 px"),
        FMath::Abs(SOLOrbitLineMath::RibbonHalfExtentPx(OrbitLineMathNaN()) - (0.5 + SOL::ORBIT_LINE_AA_MARGIN_PX))
        < 1e-12);
    for (const double width : { 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 6.0 })
    {
        TestEqual(FString::Printf(TEXT("No coverage at the ribbon edge (%.2f px)"), width),
            SOLOrbitLineMath::LineCoverage(SOLOrbitLineMath::RibbonHalfExtentPx(width), width), 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathPixelSizeTest, "SOLTest.OrbitLineMath.WorldSizePerPixel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One pixel's world size grows linearly with distance, so a ribbon sized by it keeps a constant pixel width
bool FSOLOrbitLineMathPixelSizeTest::RunTest(const FString& /*parameters*/)
{
    const double tanHalf = 1.0;   // 90 deg horizontal FOV
    const double viewportWidth = 1920.0;
    TestTrue(TEXT("Formula"), FMath::Abs(SOLOrbitLineMath::WorldSizePerPixel(1000.0, tanHalf, viewportWidth)
        - 2000.0 / 1920.0) < 1e-12);

    // Constant screen width across 10 orders of magnitude: world width / pixel size at that distance = pixels
    const double widthPx = 1.5;
    for (const double distance : { 1.0e2, 1.0e6, 1.0e9, 1.0e12 })
    {
        const double worldWidth = widthPx * SOLOrbitLineMath::WorldSizePerPixel(distance, tanHalf, viewportWidth);
        const double projectedPx = worldWidth / distance * viewportWidth / (2.0 * tanHalf);
        TestTrue(FString::Printf(TEXT("%.0e: projects to the same pixel width"), distance),
            FMath::Abs(projectedPx - widthPx) < 1e-9);
    }

    // Invalid inputs
    TestEqual(TEXT("Distance 0"), SOLOrbitLineMath::WorldSizePerPixel(0.0, tanHalf, viewportWidth), 0.0);
    TestEqual(TEXT("Distance < 0"), SOLOrbitLineMath::WorldSizePerPixel(-1.0, tanHalf, viewportWidth), 0.0);
    TestEqual(TEXT("tanHalf 0"), SOLOrbitLineMath::WorldSizePerPixel(1.0, 0.0, viewportWidth), 0.0);
    TestEqual(TEXT("Viewport 0"), SOLOrbitLineMath::WorldSizePerPixel(1.0, tanHalf, 0.0), 0.0);
    TestEqual(TEXT("NaN distance"), SOLOrbitLineMath::WorldSizePerPixel(OrbitLineMathNaN(), tanHalf, viewportWidth),
        0.0);
    TestEqual(TEXT("Inf distance"), SOLOrbitLineMath::WorldSizePerPixel(OrbitLineMathInf(), tanHalf, viewportWidth),
        0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathSideTest, "SOLTest.OrbitLineMath.RibbonSideDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Unit and perpendicular to the view ray (and to the tangent when defined); deterministic fallback; continuous
bool FSOLOrbitLineMathSideTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d view(1.0, 0.0, 0.0);
    const FVector3d side = SOLOrbitLineMath::RibbonSideDirection(view, FVector3d(0.0, 3.0, 0.0));
    TestTrue(TEXT("Unit"), OrbitLineMathIsUnit(side, 1e-12));
    TestTrue(TEXT("Perpendicular to the view"), FMath::Abs(side | view) < 1e-12);
    TestTrue(TEXT("Perpendicular to the tangent"), FMath::Abs(side | FVector3d(0.0, 1.0, 0.0)) < 1e-12);
    TestTrue(TEXT("Equals normalize(v x t)"), SOLTestHelpers::VectorsNear(side, FVector3d(0.0, 0.0, 1.0), 1e-12));

    // Unnormalized, oblique input
    const FVector3d obliqueView(3.0e11, -2.0e11, 5.0e10);
    const FVector3d obliqueTangent(0.2, 0.9, -0.4);
    const FVector3d oblique = SOLOrbitLineMath::RibbonSideDirection(obliqueView, obliqueTangent);
    TestTrue(TEXT("Oblique unit"), OrbitLineMathIsUnit(oblique, 1e-12));
    TestTrue(TEXT("Oblique perpendicular to view"), FMath::Abs(oblique | obliqueView.GetSafeNormal()) < 1e-9);
    TestTrue(TEXT("Oblique perpendicular to tangent"), FMath::Abs(oblique | obliqueTangent.GetSafeNormal()) < 1e-9);

    // Degenerate tangents fall back to a unit vector perpendicular to the view, the same every call
    const double nan = OrbitLineMathNaN();
    const FVector3d degenerateTangents[] = { FVector3d(2.0, 0.0, 0.0), FVector3d(-1.0, 0.0, 0.0), FVector3d::ZeroVector,
        FVector3d(nan, 0.0, 0.0) };
    for (const FVector3d& tangent : degenerateTangents)
    {
        const FVector3d fallback = SOLOrbitLineMath::RibbonSideDirection(view, tangent);
        TestTrue(TEXT("Fallback unit"), OrbitLineMathIsUnit(fallback, 1e-12));
        TestTrue(TEXT("Fallback perpendicular to view"), FMath::Abs(fallback | view) < 1e-12);
        TestTrue(TEXT("Fallback deterministic"), fallback == SOLOrbitLineMath::RibbonSideDirection(view, tangent));
    }

    // Looking straight down +Z (the fallback must not use a vector parallel to the view)
    const FVector3d down(0.0, 0.0, -5.0);
    const FVector3d downFallback = SOLOrbitLineMath::RibbonSideDirection(down, FVector3d(0.0, 0.0, 1.0));
    TestTrue(TEXT("Fallback along Z view: unit"), OrbitLineMathIsUnit(downFallback, 1e-12));
    TestTrue(TEXT("Fallback along Z view: perpendicular"), FMath::Abs(downFallback | FVector3d(0.0, 0.0, 1.0)) < 1e-12);

    // Zero or non-finite view gives the zero vector
    TestTrue(TEXT("Zero view"), SOLOrbitLineMath::RibbonSideDirection(FVector3d::ZeroVector, FVector3d(0.0, 1.0, 0.0))
        == FVector3d::ZeroVector);
    TestTrue(TEXT("NaN view"), SOLOrbitLineMath::RibbonSideDirection(FVector3d(nan, 1.0, 0.0), FVector3d(0.0, 1.0, 0.0))
        == FVector3d::ZeroVector);

    // Continuous: a small change of the tangent does not flip the side
    const FVector3d sideA = SOLOrbitLineMath::RibbonSideDirection(view, FVector3d(0.0, 1.0, 0.0));
    const FVector3d sideB = SOLOrbitLineMath::RibbonSideDirection(view, FVector3d(0.0, 1.0, 0.001));
    TestTrue(TEXT("No flip for a small tangent change"), (sideA | sideB) > 0.99);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Distance fade
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathFadeTest, "SOLTest.OrbitLineMath.DistanceFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 1 near, 0 far, linear in log distance between (0.5 at the geometric mean); highlight floor; invalid gives 0
bool FSOLOrbitLineMathFadeTest::RunTest(const FString& /*parameters*/)
{
    const double a = ORBITLINEMATH_MOON_A_M;
    const double start = SOL::ORBIT_LINE_FADE_START_SMA * a;
    const double end = SOL::ORBIT_LINE_FADE_END_SMA * a;
    const double highlightFloor = SOL::ORBIT_LINE_HIGHLIGHT_MIN_ALPHA;

    TestEqual(TEXT("Camera on the line"), SOLOrbitLineMath::DistanceFade(0.0, a, false), 1.0);
    TestEqual(TEXT("Inside the orbit (d = a)"), SOLOrbitLineMath::DistanceFade(a, a, false), 1.0);
    TestEqual(TEXT("At fade start"), SOLOrbitLineMath::DistanceFade(start, a, false), 1.0);
    TestEqual(TEXT("At fade end"), SOLOrbitLineMath::DistanceFade(end, a, false), 0.0);
    TestEqual(TEXT("Far beyond"), SOLOrbitLineMath::DistanceFade(end * 1.0e3, a, false), 0.0);
    TestTrue(TEXT("Geometric mean gives 0.5"),
        FMath::Abs(SOLOrbitLineMath::DistanceFade(FMath::Sqrt(start * end), a, false) - 0.5) < 1e-9);

    // Linear in log distance: a quarter of the way in log space gives 0.75
    const double quarter = FMath::Exp(FMath::Loge(start) + 0.25 * (FMath::Loge(end) - FMath::Loge(start)));
    TestTrue(TEXT("Quarter way in log distance gives 0.75"),
        FMath::Abs(SOLOrbitLineMath::DistanceFade(quarter, a, false) - 0.75) < 1e-9);

    // Scales with the orbit: a planet orbit stays visible where a moon orbit has faded
    TestEqual(TEXT("Moon orbit gone from 1 AU"), SOLOrbitLineMath::DistanceFade(ORBITLINEMATH_AU_M * 1.0e2, a, false),
        0.0);
    TestEqual(TEXT("Earth orbit full from 1 AU"),
        SOLOrbitLineMath::DistanceFade(ORBITLINEMATH_AU_M, ORBITLINEMATH_AU_M, false), 1.0);

    // Monotone non-increasing
    double previous = 2.0;
    bool bMonotone = true;
    for (double distance = 1.0; distance < end * 10.0; distance *= 1.3)
    {
        const double alpha = SOLOrbitLineMath::DistanceFade(distance, a, false);
        bMonotone &= alpha <= previous && alpha >= 0.0 && alpha <= 1.0;
        previous = alpha;
    }
    TestTrue(TEXT("Non-increasing and in [0, 1]"), bMonotone);

    // Highlighted: never below the floor, unchanged above it
    TestEqual(TEXT("Highlighted far away keeps the floor"), SOLOrbitLineMath::DistanceFade(end * 10.0, a, true),
        highlightFloor);
    TestEqual(TEXT("Highlighted near is 1"), SOLOrbitLineMath::DistanceFade(a, a, true), 1.0);
    TestTrue(TEXT("Highlighted at the mean is max(0.5, floor)"),
        FMath::Abs(SOLOrbitLineMath::DistanceFade(FMath::Sqrt(start * end), a, true)
            - FMath::Max(0.5, highlightFloor)) < 1e-9);

    // Invalid inputs give 0, highlighted or not
    const double nan = OrbitLineMathNaN();
    for (const bool bHighlighted : { false, true })
    {
        const TCHAR* tag = bHighlighted ? TEXT("highlighted") : TEXT("normal");
        TestEqual(FString::Printf(TEXT("NaN distance (%s)"), tag), SOLOrbitLineMath::DistanceFade(nan, a, bHighlighted),
            0.0);
        TestEqual(FString::Printf(TEXT("Negative distance (%s)"), tag),
            SOLOrbitLineMath::DistanceFade(-1.0, a, bHighlighted), 0.0);
        TestEqual(FString::Printf(TEXT("Inf distance (%s)"), tag),
            SOLOrbitLineMath::DistanceFade(OrbitLineMathInf(), a, bHighlighted), 0.0);
        TestEqual(FString::Printf(TEXT("a = 0 (%s)"), tag), SOLOrbitLineMath::DistanceFade(1.0, 0.0, bHighlighted),
            0.0);
        TestEqual(FString::Printf(TEXT("a NaN (%s)"), tag), SOLOrbitLineMath::DistanceFade(1.0, nan, bHighlighted),
            0.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Occlusion
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathOcclusionTest, "SOLTest.OrbitLineMath.OcclusionBehindFrontThrough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Behind and through the sphere is hidden; in front, beside, tangent-clear or behind the camera is drawn
bool FSOLOrbitLineMathOcclusionTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d camera = FVector3d::ZeroVector;
    const FVector3d center(10.0, 0.0, 0.0);
    const double radius = 1.0;
    auto occluded = [&](const FVector3d& point)
    {
        return SOLOrbitLineMath::IsPointOccludedBySphere(camera, point, center, radius);
    };

    TestTrue(TEXT("Directly behind"), occluded(FVector3d(20.0, 0.0, 0.0)));
    TestTrue(TEXT("Behind, ray through the disc"), occluded(FVector3d(20.0, 1.5, 0.0)));
    TestTrue(TEXT("On the far surface (line passes through)"), occluded(FVector3d(11.0, 0.0, 0.0)));
    TestTrue(TEXT("Inside, at the centre"), occluded(center));
    TestTrue(TEXT("Inside, just under the near surface"), occluded(FVector3d(9.001, 0.0, 0.0)));
    TestFalse(TEXT("In front, just above the near surface"), occluded(FVector3d(8.999, 0.0, 0.0)));
    TestFalse(TEXT("In front, halfway"), occluded(FVector3d(5.0, 0.0, 0.0)));
    TestFalse(TEXT("Behind but beside the disc"), occluded(FVector3d(20.0, 5.0, 0.0)));

    // Near-tangent rays: the segment's closest approach is just outside or just inside R
    const FVector3d tangentDir(FMath::Sqrt(99.0), 1.0, 0.0);   // touches the sphere at distance R from the centre
    TestFalse(TEXT("Grazing just outside"), occluded(tangentDir * 3.0 * (1.0 + 1e-6) + FVector3d(0.0, 3.0e-5, 0.0)));
    TestTrue(TEXT("Grazing just inside"), occluded(FVector3d(30.0, 2.9, 0.0)));

    // A sphere behind the camera never hides anything in front
    TestFalse(TEXT("Sphere behind the camera"), SOLOrbitLineMath::IsPointOccludedBySphere(camera,
        FVector3d(20.0, 0.0, 0.0), FVector3d(-10.0, 0.0, 0.0), radius));

    // A point at the camera is not occluded
    TestFalse(TEXT("Point at the camera"), occluded(camera));

    // Order of the points along the same ray: in front visible, behind hidden
    for (double distance = 0.5; distance < 30.0; distance += 0.5)
    {
        const bool bExpectedHidden = distance > 9.0;
        TestEqual(FString::Printf(TEXT("Ray point at %.1f"), distance), occluded(FVector3d(distance, 0.0, 0.0)),
            bExpectedHidden);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathOcclusionInvalidTest,
    "SOLTest.OrbitLineMath.OcclusionCameraInsideAndInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A camera inside or on the sphere, a non-positive radius, or any non-finite input never occludes
bool FSOLOrbitLineMathOcclusionInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d center(10.0, 0.0, 0.0);
    const FVector3d behind(20.0, 0.0, 0.0);
    const double nan = OrbitLineMathNaN();
    TestFalse(TEXT("Camera inside"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d(10.5, 0.0, 0.0), behind,
        center, 1.0));
    TestFalse(TEXT("Camera at the centre"), SOLOrbitLineMath::IsPointOccludedBySphere(center, behind, center, 1.0));
    TestFalse(TEXT("Camera on the surface"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d(9.0, 0.0, 0.0),
        behind, center, 1.0));
    TestFalse(TEXT("Radius 0"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d::ZeroVector, behind, center, 0.0));
    TestFalse(TEXT("Radius < 0"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d::ZeroVector, behind, center,
        -1.0));
    TestFalse(TEXT("Radius NaN"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d::ZeroVector, behind, center,
        nan));
    TestFalse(TEXT("Centre NaN"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d::ZeroVector, behind,
        FVector3d(nan, 0.0, 0.0), 1.0));
    TestFalse(TEXT("Point NaN"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d::ZeroVector,
        FVector3d(nan, 0.0, 0.0), center, 1.0));
    TestFalse(TEXT("Camera NaN"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d(nan, 0.0, 0.0), behind, center,
        1.0));
    TestFalse(TEXT("Point at infinity"), SOLOrbitLineMath::IsPointOccludedBySphere(FVector3d::ZeroVector,
        FVector3d(OrbitLineMathInf(), 0.0, 0.0), center, 1.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathOcclusionScaleTest, "SOLTest.OrbitLineMath.OcclusionTrueScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At real scale (Earth at 1 AU, a camera 400,000 km sunward), the Moon's orbit is hidden only behind Earth's true disc
bool FSOLOrbitLineMathOcclusionScaleTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d earth(ORBITLINEMATH_AU_M, 0.0, 0.0);
    const double radius = ORBITLINEMATH_EARTH_RADIUS_M;
    const FVector3d camera = earth - FVector3d(4.0e8, 0.0, 0.0);

    // Behind Earth on the far side of the Moon's orbit: lateral offset at Earth ~ 1e6 * 4/7.844 = 5.1e5 m < R
    TestTrue(TEXT("Moon-orbit point behind Earth's disc"), SOLOrbitLineMath::IsPointOccludedBySphere(camera,
        earth + FVector3d(ORBITLINEMATH_MOON_A_M, 1.0e6, 0.0), earth, radius));

    // Offset at Earth ~ 1.02e7 m > R: beside the disc
    TestFalse(TEXT("Moon-orbit point beside Earth's disc"), SOLOrbitLineMath::IsPointOccludedBySphere(camera,
        earth + FVector3d(ORBITLINEMATH_MOON_A_M, 2.0e7, 0.0), earth, radius));

    // Between the camera and Earth: drawn over the planet
    TestFalse(TEXT("Moon-orbit point in front of Earth"), SOLOrbitLineMath::IsPointOccludedBySphere(camera,
        earth - FVector3d(ORBITLINEMATH_MOON_A_M * 0.5, 1.0e6, 0.0), earth, radius));

    // Earth's own orbit line passes through Earth's centre: hidden there, visible well before it
    TestTrue(TEXT("Own orbit line at the planet's centre"), SOLOrbitLineMath::IsPointOccludedBySphere(camera, earth,
        earth, radius));
    TestTrue(TEXT("Own orbit line just past the centre"), SOLOrbitLineMath::IsPointOccludedBySphere(camera,
        earth + FVector3d(0.0, radius * 0.5, 0.0), earth, radius));

    // Neptune-scale distances: the Sun hides a point on the far side of the system
    const FVector3d sunCamera(30.07 * ORBITLINEMATH_AU_M, 0.0, 0.0);
    TestTrue(TEXT("Far side of the Sun hidden from Neptune"), SOLOrbitLineMath::IsPointOccludedBySphere(sunCamera,
        FVector3d(-30.0 * ORBITLINEMATH_AU_M, 1.0e8, 0.0), FVector3d::ZeroVector, 6.957e8));
    TestFalse(TEXT("Far side of the system beside the Sun visible"), SOLOrbitLineMath::IsPointOccludedBySphere(
        sunCamera, FVector3d(-30.0 * ORBITLINEMATH_AU_M, 5.0e9, 0.0), FVector3d::ZeroVector, 6.957e8));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathOcclusionListTest, "SOLTest.OrbitLineMath.OcclusionList",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Any sphere in the list occludes; empty or mismatched lists only consider the common length
bool FSOLOrbitLineMathOcclusionListTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d camera = FVector3d::ZeroVector;
    const TArray<FVector3d> centers = { FVector3d(0.0, 10.0, 0.0), FVector3d(10.0, 0.0, 0.0) };
    const TArray<double> radii = { 1.0, 1.0 };
    TestTrue(TEXT("Second sphere hides +X"), SOLOrbitLineMath::IsPointOccluded(camera, FVector3d(20.0, 0.0, 0.0),
        centers, radii));
    TestTrue(TEXT("First sphere hides +Y"), SOLOrbitLineMath::IsPointOccluded(camera, FVector3d(0.0, 20.0, 0.0),
        centers, radii));
    TestFalse(TEXT("Nothing hides -X"), SOLOrbitLineMath::IsPointOccluded(camera, FVector3d(-20.0, 0.0, 0.0), centers,
        radii));
    TestFalse(TEXT("Empty list"), SOLOrbitLineMath::IsPointOccluded(camera, FVector3d(20.0, 0.0, 0.0),
        TConstArrayView<FVector3d>(), TConstArrayView<double>()));

    // Only one radius: the second centre is ignored
    const TArray<double> oneRadius = { 1.0 };
    TestFalse(TEXT("Mismatched lengths use the common prefix"), SOLOrbitLineMath::IsPointOccluded(camera,
        FVector3d(20.0, 0.0, 0.0), centers, oneRadius));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathSelectOccludersTest, "SOLTest.OrbitLineMath.SelectOccluders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Largest angular radius first (ties to the lower index), invalid and camera-inside spheres skipped, truncated
bool FSOLOrbitLineMathSelectOccludersTest::RunTest(const FString& /*parameters*/)
{
    const double nan = OrbitLineMathNaN();
    const FVector3d camera = FVector3d::ZeroVector;
    // 0: asin(0.1); 1: asin(0.05); 2: asin(0.1) (tie with 0); 3: NaN centre; 4: R = 0; 5: camera inside;
    // 6: asin(1e-6) (below the 1e-4 minimum); 7: asin(0.4) (largest)
    const TArray<FVector3d> centers = { FVector3d(100.0, 0.0, 0.0), FVector3d(0.0, 1000.0, 0.0),
        FVector3d(0.0, 0.0, 10.0), FVector3d(nan, 0.0, 0.0), FVector3d(5.0, 5.0, 5.0), FVector3d(0.0, 0.0, 0.5),
        FVector3d(0.0, -1.0e6, 0.0), FVector3d(-50.0, 0.0, 0.0) };
    const TArray<double> radii = { 10.0, 50.0, 1.0, 1.0, 0.0, 1.0, 1.0, 20.0 };
    const double minAngle = 1.0e-4;

    TArray<int32> selected = { 99, 98 };
    const int32 count = SOLOrbitLineMath::SelectOccluders(camera, centers, radii, minAngle, 8, selected);
    TestEqual(TEXT("Returns the count"), count, selected.Num());
    TestTrue(TEXT("Order 7, 0, 2, 1 (junk reset)"), selected == TArray<int32>({ 7, 0, 2, 1 }));

    SOLOrbitLineMath::SelectOccluders(camera, centers, radii, minAngle, 2, selected);
    TestTrue(TEXT("Truncated to 2"), selected == TArray<int32>({ 7, 0 }));

    TestEqual(TEXT("maxCount 0"), SOLOrbitLineMath::SelectOccluders(camera, centers, radii, minAngle, 0, selected), 0);
    TestEqual(TEXT("maxCount 0 leaves it empty"), selected.Num(), 0);
    TestEqual(TEXT("maxCount < 0"), SOLOrbitLineMath::SelectOccluders(camera, centers, radii, minAngle, -3, selected),
        0);

    // A NaN minimum counts as 0: the tiny sphere joins at the end
    SOLOrbitLineMath::SelectOccluders(camera, centers, radii, nan, 8, selected);
    TestTrue(TEXT("NaN minimum includes the tiny sphere"), selected == TArray<int32>({ 7, 0, 2, 1, 6 }));

    // Only the first three radii: indices beyond them are ignored
    const TArray<double> threeRadii = { 10.0, 50.0, 1.0 };
    SOLOrbitLineMath::SelectOccluders(camera, centers, threeRadii, minAngle, 8, selected);
    TestTrue(TEXT("Mismatched lengths use the common prefix"), selected == TArray<int32>({ 0, 2, 1 }));

    // Empty input
    TestEqual(TEXT("Empty input"), SOLOrbitLineMath::SelectOccluders(camera, TConstArrayView<FVector3d>(),
        TConstArrayView<double>(), minAngle, 8, selected), 0);

    // Capacity kept across calls (no reallocation once reserved)
    TArray<int32> reserved;
    reserved.Reserve(16);
    const int32* data = reserved.GetData();
    SOLOrbitLineMath::SelectOccluders(camera, centers, radii, minAngle, 8, reserved);
    SOLOrbitLineMath::SelectOccluders(camera, centers, radii, minAngle, 8, reserved);
    TestTrue(TEXT("No reallocation"), reserved.GetData() == data);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathSceneDepthTest, "SOLTest.OrbitLineMath.SceneDepthOnlyIn1to1Zone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Scene depth hides a line only inside the 1:1 zone, beyond a relative bias; compressed depth and sky never do
bool FSOLOrbitLineMathSceneDepthTest::RunTest(const FString& /*parameters*/)
{
    const double maxCm = ORBITLINEMATH_MAX_RENDER_CM;
    const double bias = SOL::ORBIT_LINE_DEPTH_BIAS;
    const double ship = 5.0e5;   // a nearby object 5 km away, rendered 1:1

    TestTrue(TEXT("Line behind a nearby object"), SOLOrbitLineMath::IsHiddenBySceneDepth(1.0e6, ship, maxCm));
    TestFalse(TEXT("Line in front of a nearby object"), SOLOrbitLineMath::IsHiddenBySceneDepth(4.0e5, ship, maxCm));
    TestFalse(TEXT("Within the bias"), SOLOrbitLineMath::IsHiddenBySceneDepth(ship * (1.0 + 0.5 * bias), ship, maxCm));
    TestTrue(TEXT("Beyond the bias"), SOLOrbitLineMath::IsHiddenBySceneDepth(ship * (1.0 + 2.0 * bias), ship, maxCm));

    // Log-compressed depth (a far planet) never hides a line: the analytic spheres handle those
    TestFalse(TEXT("Compressed scene depth"), SOLOrbitLineMath::IsHiddenBySceneDepth(3.0e13, 2.0e11, maxCm));
    TestFalse(TEXT("Scene depth exactly at max"), SOLOrbitLineMath::IsHiddenBySceneDepth(3.0e13, maxCm, maxCm));

    // Sky and invalid inputs
    TestFalse(TEXT("Sky (infinite depth)"), SOLOrbitLineMath::IsHiddenBySceneDepth(1.0e6, OrbitLineMathInf(), maxCm));
    TestFalse(TEXT("NaN scene depth"), SOLOrbitLineMath::IsHiddenBySceneDepth(1.0e6, OrbitLineMathNaN(), maxCm));
    TestFalse(TEXT("NaN line distance"), SOLOrbitLineMath::IsHiddenBySceneDepth(OrbitLineMathNaN(), ship, maxCm));
    TestFalse(TEXT("Scene depth 0"), SOLOrbitLineMath::IsHiddenBySceneDepth(1.0e6, 0.0, maxCm));
    TestFalse(TEXT("Scene depth < 0"), SOLOrbitLineMath::IsHiddenBySceneDepth(1.0e6, -1.0, maxCm));
    TestFalse(TEXT("NaN max"), SOLOrbitLineMath::IsHiddenBySceneDepth(1.0e6, ship, OrbitLineMathNaN()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathPlaceTest, "SOLTest.OrbitLineMath.PlaceLinePoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Line points use the bodies' placement: 1:1 within range, log-compressed beyond, same direction (same pixel)
bool FSOLOrbitLineMathPlaceTest::RunTest(const FString& /*parameters*/)
{
    const double maxCm = ORBITLINEMATH_MAX_RENDER_CM;

    // Within range: 1:1 in centimetres
    const FVector3d nearPoint(3.0e5, 4.0e5, 0.0);
    TestTrue(TEXT("Within range is 1:1"),
        SOLTestHelpers::VectorsNear(SOLOrbitLineMath::PlaceLinePointCm(nearPoint, maxCm), nearPoint * 100.0, 1e-12));

    // Beyond range: identical to the body placement, direction kept, depth d' = Max (1 + 0.1 ln(d / Max))
    const FVector3d farPoint(ORBITLINEMATH_AU_M, -2.0e10, 7.0e9);
    const FVector3d placed = SOLOrbitLineMath::PlaceLinePointCm(farPoint, maxCm);
    TestTrue(TEXT("Equals SOLRender::ComputePlacement"),
        placed == SOLRender::ComputePlacement(farPoint, 0.0, maxCm).LocationCm);
    TestTrue(TEXT("Same direction"),
        SOLTestHelpers::VectorsNear(placed.GetSafeNormal(), farPoint.GetSafeNormal(), 1e-12));
    const double trueCm = farPoint.Size() * 100.0;
    const double expectedCm = maxCm * (1.0 + ORBITLINEMATH_LOG_SCALE * FMath::Loge(trueCm / maxCm));
    TestTrue(TEXT("Compressed depth"), SOLTestHelpers::RelativeError(placed.Size(), expectedCm) < 1e-12);

    // Depth order along one ray is kept
    const FVector3d direction = farPoint.GetSafeNormal();
    double previous = 0.0;
    bool bOrdered = true;
    for (double distance = 1.0e6; distance < 1.0e13; distance *= 3.0)
    {
        const double placedDistance = SOLOrbitLineMath::PlaceLinePointCm(direction * distance, maxCm).Size();
        bOrdered &= placedDistance > previous;
        previous = placedDistance;
    }
    TestTrue(TEXT("Depth order preserved along a ray"), bOrdered);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Double-float precision
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathSplitTest, "SOLTest.OrbitLineMath.SplitDoublePrecision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// hi + lo reconstructs the double to ~48 bits, and a float-only camera-relative difference stays centimetre-exact
bool FSOLOrbitLineMathSplitTest::RunTest(const FString& /*parameters*/)
{
    const double values[] = { 0.0, 1.0, 1.0e-3, -7.3e12, ORBITLINEMATH_AU_M + 0.123456, 30.07 * ORBITLINEMATH_AU_M,
        67.9 * ORBITLINEMATH_AU_M };
    for (const double value : values)
    {
        float high = 0.0f;
        float low = 0.0f;
        SOLOrbitLineMath::SplitDouble(value, high, low);
        TestEqual(FString::Printf(TEXT("%g: high is float(value)"), value), high, static_cast<float>(value));
        const double error = FMath::Abs(static_cast<double>(high) + static_cast<double>(low) - value);
        TestTrue(FString::Printf(TEXT("%g: hi + lo within |v| 2^-46"), value),
            error <= FMath::Abs(value) * FMath::Pow(2.0, -46.0));
    }
    float zeroHigh = 1.0f;
    float zeroLow = 1.0f;
    SOLOrbitLineMath::SplitDouble(0.0, zeroHigh, zeroLow);
    TestTrue(TEXT("Zero splits to zeros"), zeroHigh == 0.0f && zeroLow == 0.0f);

    // Non-finite: low is 0
    float infHigh = 0.0f;
    float infLow = 1.0f;
    SOLOrbitLineMath::SplitDouble(OrbitLineMathInf(), infHigh, infLow);
    TestTrue(TEXT("Inf: high inf, low 0"), std::isinf(infHigh) && infLow == 0.0f);

    // Shader emulation: a line point 1 AU from the parent, the camera 100.25 m away from it
    const FVector3d point(ORBITLINEMATH_AU_M + 0.37, -2.1e9 + 0.11, 3.3e7);
    const FVector3d camera = point - FVector3d(100.25, -40.5, 7.75);
    FVector3f pointHigh;
    FVector3f pointLow;
    FVector3f cameraHigh;
    FVector3f cameraLow;
    SOLOrbitLineMath::SplitVector(point, pointHigh, pointLow);
    SOLOrbitLineMath::SplitVector(camera, cameraHigh, cameraLow);
    const FVector3f relative = (pointHigh - cameraHigh) + (pointLow - cameraLow);   // float-only arithmetic
    const FVector3d expected = point - camera;
    TestTrue(TEXT("Double-float camera-relative error below 1 cm"),
        (FVector3d(relative) - expected).Size() < 0.01);

    // The plain-float version would be off by kilometres at 1 AU (documents why the pair is needed)
    const FVector3f naive = FVector3f(point) - FVector3f(camera);
    TestTrue(TEXT("Plain float is far worse"), (FVector3d(naive) - expected).Size() > 1.0);

    // Vector split is per component
    FVector3f componentHigh;
    FVector3f componentLow;
    SOLOrbitLineMath::SplitVector(point, componentHigh, componentLow);
    float xHigh = 0.0f;
    float xLow = 0.0f;
    SOLOrbitLineMath::SplitDouble(point.X, xHigh, xLow);
    TestTrue(TEXT("SplitVector X = SplitDouble X"), componentHigh.X == xHigh && componentLow.X == xLow);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Rebuild on drift
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathDriftTest, "SOLTest.OrbitLineMath.OrbitDrift",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero for identical shapes (M and whole turns ignored), +inf for invalid, and a true bound on point displacement
bool FSOLOrbitLineMathDriftTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeplerElements base = OrbitLineMathElements(1.5e11, 0.2, 0.3, 1.0, 2.0);
    const double a = base.SemiMajorAxisM;
    TestTrue(TEXT("Identical gives 0"), SOLOrbitLineMath::OrbitDriftM(base, base) <= 1e-12 * a);

    FSOLKeplerElements moved = base;
    moved.MeanAnomalyRad = 3.0;
    TestTrue(TEXT("Mean anomaly ignored"), SOLOrbitLineMath::OrbitDriftM(base, moved) <= 1e-12 * a);
    moved.MeanAnomalyRad = OrbitLineMathNaN();
    TestTrue(TEXT("NaN mean anomaly ignored"), SOLOrbitLineMath::OrbitDriftM(base, moved) <= 1e-12 * a);

    FSOLKeplerElements turned = base;
    turned.LongitudeOfAscendingNodeRad += UE_DOUBLE_TWO_PI;
    turned.ArgumentOfPeriapsisRad -= UE_DOUBLE_TWO_PI;
    TestTrue(TEXT("Whole turns give 0"), SOLOrbitLineMath::OrbitDriftM(base, turned) <= 1e-9 * a);

    // Invalid on either side gives +inf
    FSOLKeplerElements invalid = base;
    invalid.Eccentricity = 1.0;
    TestTrue(TEXT("Invalid now gives +inf"), std::isinf(SOLOrbitLineMath::OrbitDriftM(base, invalid)));
    TestTrue(TEXT("Invalid built gives +inf"), std::isinf(SOLOrbitLineMath::OrbitDriftM(invalid, base)));

    // A pure change of a moves points by at least |da|
    FSOLKeplerElements grown = base;
    grown.SemiMajorAxisM += 1.0e7;
    TestTrue(TEXT("da alone >= |da|"), SOLOrbitLineMath::OrbitDriftM(base, grown) >= 1.0e7);

    // Bound property against real sampled points, for single and combined perturbations (also at high e)
    struct FPerturbation { const TCHAR* Name; double Da; double De; double Di; double DNode; double DArg; double E; };
    const FPerturbation perturbations[] = {
        { TEXT("da"), 1.0e7, 0.0, 0.0, 0.0, 0.0, 0.2 },
        { TEXT("de"), 0.0, 0.01, 0.0, 0.0, 0.0, 0.2 },
        { TEXT("di"), 0.0, 0.0, 0.001, 0.0, 0.0, 0.2 },
        { TEXT("dNode"), 0.0, 0.0, 0.0, 0.002, 0.0, 0.2 },
        { TEXT("dArg"), 0.0, 0.0, 0.0, 0.0, -0.003, 0.2 },
        { TEXT("all"), -3.0e6, -0.004, 0.0007, -0.0015, 0.002, 0.2 },
        { TEXT("high e de"), 0.0, 0.004, 0.0, 0.0, 0.0, 0.95 },
        { TEXT("high e all"), 2.0e6, 0.003, 0.001, 0.001, 0.001, 0.95 },
    };
    for (const FPerturbation& change : perturbations)
    {
        FSOLKeplerElements before = base;
        before.Eccentricity = change.E;
        FSOLKeplerElements after = before;
        after.SemiMajorAxisM += change.Da;
        after.Eccentricity += change.De;
        after.InclinationRad += change.Di;
        after.LongitudeOfAscendingNodeRad += change.DNode;
        after.ArgumentOfPeriapsisRad += change.DArg;

        TArray<FVector3d> beforePoints;
        TArray<FVector3d> afterPoints;
        TArray<FVector3d> tangents;
        TArray<double> arcs;
        OrbitLineMathSample(before, 256, beforePoints, tangents, arcs);
        OrbitLineMathSample(after, 256, afterPoints, tangents, arcs);
        const double actual = OrbitLineMathMaxDisplacement(beforePoints, afterPoints);
        const double drift = SOLOrbitLineMath::OrbitDriftM(before, after);
        TestTrue(FString::Printf(TEXT("%s: drift > 0"), change.Name), drift > 0.0);
        TestTrue(FString::Printf(TEXT("%s: drift bounds the real displacement"), change.Name),
            actual <= drift * (1.0 + 1e-9) + 1e-3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathRebuildTest, "SOLTest.OrbitLineMath.NeedsRebuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Rebuild only once the drift exceeds the radius-fraction tolerance (or the a-fraction fallback); invalid rules
bool FSOLOrbitLineMathRebuildTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeplerElements built = OrbitLineMathElements(1.5e11, 0.2, 0.3, 1.0, 2.0);
    const double radius = ORBITLINEMATH_EARTH_RADIUS_M;
    const double tolerance = SOL::ORBIT_LINE_SAGITTA_RADIUS_FRACTION * radius;

    TestFalse(TEXT("Unchanged"), SOLOrbitLineMath::NeedsRebuild(built, built, radius));

    // A pure da moves points by |da| (1 + e): half the tolerance is fine, twice it rebuilds
    FSOLKeplerElements small = built;
    small.SemiMajorAxisM += 0.5 * tolerance / (1.0 + built.Eccentricity);
    FSOLKeplerElements large = built;
    large.SemiMajorAxisM += 2.0 * tolerance / (1.0 + built.Eccentricity);
    TestFalse(TEXT("Small drift keeps the line"), SOLOrbitLineMath::NeedsRebuild(built, small, radius));
    TestTrue(TEXT("Large drift rebuilds"), SOLOrbitLineMath::NeedsRebuild(built, large, radius));

    // Consistent with OrbitDriftM for an arbitrary change
    FSOLKeplerElements precessed = built;
    precessed.ArgumentOfPeriapsisRad += 1.0e-5;
    TestEqual(TEXT("Matches drift > tolerance"), SOLOrbitLineMath::NeedsRebuild(built, precessed, radius),
        SOLOrbitLineMath::OrbitDriftM(built, precessed) > tolerance);

    // Only the mean anomaly changed: never rebuild (the body moves along a fixed line)
    FSOLKeplerElements advanced = built;
    advanced.MeanAnomalyRad += 2.0;
    TestFalse(TEXT("Mean anomaly alone never rebuilds"), SOLOrbitLineMath::NeedsRebuild(built, advanced, radius));

    // Invalid now keeps the old line; invalid built with valid now rebuilds
    FSOLKeplerElements invalid = built;
    invalid.SemiMajorAxisM = OrbitLineMathNaN();
    TestFalse(TEXT("Invalid now keeps the old line"), SOLOrbitLineMath::NeedsRebuild(built, invalid, radius));
    TestTrue(TEXT("Invalid built rebuilds"), SOLOrbitLineMath::NeedsRebuild(invalid, built, radius));

    // Unknown radius falls back to a fraction of a
    const double fallback = SOL::ORBIT_LINE_REBUILD_FALLBACK_FRACTION * built.SemiMajorAxisM;
    FSOLKeplerElements fallbackSmall = built;
    fallbackSmall.SemiMajorAxisM += 0.5 * fallback / (1.0 + built.Eccentricity);
    FSOLKeplerElements fallbackLarge = built;
    fallbackLarge.SemiMajorAxisM += 2.0 * fallback / (1.0 + built.Eccentricity);
    for (const double badRadius : { 0.0, -1.0, OrbitLineMathNaN() })
    {
        TestFalse(FString::Printf(TEXT("R=%g: small drift keeps"), badRadius),
            SOLOrbitLineMath::NeedsRebuild(built, fallbackSmall, badRadius));
        TestTrue(FString::Printf(TEXT("R=%g: large drift rebuilds"), badRadius),
            SOLOrbitLineMath::NeedsRebuild(built, fallbackLarge, badRadius));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Which bodies get lines; targeting and highlight
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathPermanentTest, "SOLTest.OrbitLineMath.BuildPermanentLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every registry body with a parent (index order), then the largest named minor bodies (ties to the lower index)
bool FSOLOrbitLineMathPermanentTest::RunTest(const FString& /*parameters*/)
{
    const double nan = OrbitLineMathNaN();
    const TArray<int32> parents = { INDEX_NONE, 0, 0, 1, 2, INDEX_NONE };
    const TArray<double> radii = { 5.0, nan, 100.0, 50.0, 100.0, 0.0, 200.0, 70.0, -3.0, OrbitLineMathInf() };
    const TArray<bool> named = { true, true, true, true, true, true, false, true, true, true };

    auto registryKey = [](const int32 index) { return FOrbitLineKey{ EOrbitLineSource::RegistryBody, index }; };
    auto minorKey = [](const int32 index) { return FOrbitLineKey{ EOrbitLineSource::MinorBody, index }; };

    TArray<FOrbitLineKey> lines = { minorKey(999) };
    SOLOrbitLineMath::BuildPermanentLines(parents, radii, named, 3, lines);
    const TArray<FOrbitLineKey> expectedThree = { registryKey(1), registryKey(2), registryKey(3), registryKey(4),
        minorKey(2), minorKey(4), minorKey(7) };
    TestTrue(TEXT("Registry children then the 3 largest named (junk reset)"), lines == expectedThree);

    SOLOrbitLineMath::BuildPermanentLines(parents, radii, named, SOL::ORBIT_LINE_NAMED_MINOR_COUNT, lines);
    const TArray<FOrbitLineKey> expectedAll = { registryKey(1), registryKey(2), registryKey(3), registryKey(4),
        minorKey(2), minorKey(4), minorKey(7), minorKey(3), minorKey(0) };
    TestTrue(TEXT("All valid named when the cap is larger"), lines == expectedAll);

    SOLOrbitLineMath::BuildPermanentLines(parents, radii, named, 0, lines);
    TestTrue(TEXT("No minor lines for a cap of 0"),
        lines == TArray<FOrbitLineKey>({ registryKey(1), registryKey(2), registryKey(3), registryKey(4) }));
    SOLOrbitLineMath::BuildPermanentLines(parents, radii, named, -1, lines);
    TestEqual(TEXT("No minor lines for a negative cap"), lines.Num(), 4);

    // The Sun alone (no parent) gets no line
    const TArray<int32> sunOnly = { INDEX_NONE };
    SOLOrbitLineMath::BuildPermanentLines(sunOnly, TConstArrayView<double>(), TConstArrayView<bool>(), 50, lines);
    TestEqual(TEXT("Sun alone: no lines"), lines.Num(), 0);

    // Mismatched minor arrays use the common prefix (named has 4 entries)
    const TArray<bool> namedShort = { true, true, true, true };
    SOLOrbitLineMath::BuildPermanentLines(TConstArrayView<int32>(), radii, namedShort, 50, lines);
    TestTrue(TEXT("Common prefix"), lines == TArray<FOrbitLineKey>({ minorKey(2), minorKey(3), minorKey(0) }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathTargetTest, "SOLTest.OrbitLineMath.ResolveTargetLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Highlight a target's permanent line, show a transient line for an orbiting target without one, else nothing
bool FSOLOrbitLineMathTargetTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FOrbitLineKey> permanent = { { EOrbitLineSource::RegistryBody, 1 },
        { EOrbitLineSource::RegistryBody, 3 }, { EOrbitLineSource::MinorBody, 7 } };
    auto expect = [this](const TCHAR* label, const SOLOrbitLineMath::FTargetLineResult& result, const int32 slot,
        const bool bTransient)
    {
        TestEqual(FString::Printf(TEXT("%s: highlight slot"), label), result.HighlightSlot, slot);
        TestEqual(FString::Printf(TEXT("%s: transient"), label), result.bShowTransient, bTransient);
    };

    // A targeted planet or moon highlights its own line
    expect(TEXT("Registry body 3"), SOLOrbitLineMath::ResolveTargetLine(true,
        { EOrbitLineSource::RegistryBody, 3 }, true, permanent), 1, false);

    // A large named asteroid with a permanent line is highlighted too
    expect(TEXT("Named asteroid 7"), SOLOrbitLineMath::ResolveTargetLine(true, { EOrbitLineSource::MinorBody, 7 }, true,
        permanent), 2, false);

    // An ordinary asteroid has no line: a transient (highlighted) one is shown
    expect(TEXT("Ordinary asteroid 4242"), SOLOrbitLineMath::ResolveTargetLine(true,
        { EOrbitLineSource::MinorBody, 4242 }, true, permanent), INDEX_NONE, true);

    // Keys compare source too: minor body 3 is not registry body 3
    expect(TEXT("Minor body 3"), SOLOrbitLineMath::ResolveTargetLine(true, { EOrbitLineSource::MinorBody, 3 }, true,
        permanent), INDEX_NONE, true);

    // A target with no orbit (the Sun, a dropped target) gets nothing
    expect(TEXT("Sun"), SOLOrbitLineMath::ResolveTargetLine(true, { EOrbitLineSource::RegistryBody, 0 }, false,
        permanent), INDEX_NONE, false);

    // A permanent line is highlighted even if the caller reports no orbit
    expect(TEXT("Permanent wins over bTargetHasOrbit"), SOLOrbitLineMath::ResolveTargetLine(true,
        { EOrbitLineSource::RegistryBody, 1 }, false, permanent), 0, false);

    // No target
    expect(TEXT("Source None"), SOLOrbitLineMath::ResolveTargetLine(true, { EOrbitLineSource::None, 3 }, true,
        permanent), INDEX_NONE, false);
    expect(TEXT("Index NONE"), SOLOrbitLineMath::ResolveTargetLine(true, { EOrbitLineSource::RegistryBody, INDEX_NONE },
        true, permanent), INDEX_NONE, false);

    // Lines toggled off: nothing, not even the target's line
    expect(TEXT("Disabled, permanent target"), SOLOrbitLineMath::ResolveTargetLine(false,
        { EOrbitLineSource::RegistryBody, 3 }, true, permanent), INDEX_NONE, false);
    expect(TEXT("Disabled, transient target"), SOLOrbitLineMath::ResolveTargetLine(false,
        { EOrbitLineSource::MinorBody, 4242 }, true, permanent), INDEX_NONE, false);

    // Duplicate keys: the first match wins
    const TArray<FOrbitLineKey> duplicated = { { EOrbitLineSource::RegistryBody, 5 },
        { EOrbitLineSource::RegistryBody, 5 } };
    expect(TEXT("First match"), SOLOrbitLineMath::ResolveTargetLine(true, { EOrbitLineSource::RegistryBody, 5 }, true,
        duplicated), 0, false);

    // Empty permanent list
    expect(TEXT("No permanent lines"), SOLOrbitLineMath::ResolveTargetLine(true, { EOrbitLineSource::RegistryBody, 3 },
        true, TConstArrayView<FOrbitLineKey>()), INDEX_NONE, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathColorTest, "SOLTest.OrbitLineMath.LineColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The highlighted line uses the highlight color, the rest the normal color; both are visible
bool FSOLOrbitLineMathColorTest::RunTest(const FString& /*parameters*/)
{
    const FLinearColor normal = SOLOrbitLineMath::LineColor(false);
    const FLinearColor highlight = SOLOrbitLineMath::LineColor(true);
    TestTrue(TEXT("Normal = ORBIT_LINE_COLOR"), normal == SOL::ORBIT_LINE_COLOR);
    TestTrue(TEXT("Highlight = ORBIT_LINE_HIGHLIGHT_COLOR"), highlight == SOL::ORBIT_LINE_HIGHLIGHT_COLOR);
    TestFalse(TEXT("Different colors"), normal.Equals(highlight));
    TestTrue(TEXT("Both visible"), normal.A > 0.0f && highlight.A > 0.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Combined scenario
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineMathInsideOrbitTest, "SOLTest.OrbitLineMath.CameraInsideOrbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Camera at 0.5 AU inside Earth's orbit: the whole line is fully faded in, expandable, hidden only behind the Sun
bool FSOLOrbitLineMathInsideOrbitTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeplerElements earth = OrbitLineMathElements(ORBITLINEMATH_AU_M, 0.0167, 0.0, 0.0, 1.796);
    const int32 segments = SOLOrbitLineMath::SegmentCountFor(earth.SemiMajorAxisM, ORBITLINEMATH_EARTH_RADIUS_M);
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    if (!TestEqual(TEXT("Count"), OrbitLineMathSample(earth, segments, points, tangents, arcs), segments + 1))
    {
        return false;
    }
    const FVector3d camera(0.5 * ORBITLINEMATH_AU_M, 0.0, 0.0);
    const FVector3d sun = FVector3d::ZeroVector;
    const double sunRadius = 6.957e8;

    int32 hiddenCount = 0;
    bool bAllFaded = true;
    bool bAllSides = true;
    for (int32 index = 0; index < segments; ++index)
    {
        const FVector3d toPoint = points[index] - camera;
        bAllFaded &= SOLOrbitLineMath::DistanceFade(toPoint.Size(), earth.SemiMajorAxisM, false) == 1.0;
        const FVector3d side = SOLOrbitLineMath::RibbonSideDirection(toPoint, tangents[index]);
        bAllSides &= OrbitLineMathIsUnit(side, 1e-9) && FMath::Abs(side | toPoint.GetSafeNormal()) < 1e-9;

        // Hidden exactly when the true segment from the camera passes inside the Sun
        const bool bHidden = SOLOrbitLineMath::IsPointOccludedBySphere(camera, points[index], sun, sunRadius);
        const bool bExpected = OrbitLineMathPointSegmentDistance(sun, camera, points[index]) < sunRadius;
        if (bHidden != bExpected)
        {
            AddError(FString::Printf(TEXT("Point %d: occlusion %d, expected %d"), index, bHidden, bExpected));
        }
        hiddenCount += bHidden ? 1 : 0;
    }
    TestTrue(TEXT("Every point fully faded in from inside the orbit"), bAllFaded);
    TestTrue(TEXT("Every ribbon side is unit and perpendicular to the view"), bAllSides);

    // Only a narrow arc behind the Sun is hidden (the Sun spans ~0.53 deg seen from 0.5 AU)
    TestTrue(TEXT("Some points behind the Sun are hidden"), hiddenCount > 0);
    TestTrue(TEXT("Only a small fraction is hidden"), hiddenCount < segments / 50);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
