/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLHologramMath.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #20 map hologram math (Docs/SDDs/20-hologram-map-and-move-orders.md section 2.4), written
// against the Visuals/SOLHologramMath.h contract before the production code existed
namespace
{
    constexpr double HOLO_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double HOLO_INF = std::numeric_limits<double>::infinity();
    constexpr double HOLO_REL_TOL = 1e-9;

    //////////////////////////////////////////////////////////////////////////
    // Returns an absolute tolerance of HOLO_REL_TOL relative to the expected value's magnitude (at least 1e-15)
    double HoloMathTol(const double expected)
    {
        return FMath::Max(FMath::Abs(expected) * HOLO_REL_TOL, 1e-15);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the LT distance field computed independently of the production code
    double HoloMathExpectedField(const double d)
    {
        const double absD = FMath::Abs(d);
        return FMath::Exp(-2.0 * absD * absD) + 0.2 * FMath::Exp(-FMath::Pow(0.2 * absD, 0.75));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the grid's edge taper computed independently of the production code
    double HoloMathExpectedTaper(const double d)
    {
        const double absD = FMath::Abs(d);
        const double extent = SOL::Hologram::GRID_EXTENT_MAJOR_CELLS;
        const double start = SOL::Hologram::GRID_EDGE_TAPER_START * extent;
        if (absD <= start)
        {
            return 1.0;
        }
        if (absD >= extent)
        {
            return 0.0;
        }
        return (extent - absD) / (extent - start);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a grid level against expected spacings and minor alpha
    void HoloMathExpectLevel(FAutomationTestBase& test, const FString& what, const double distanceM,
        const double minor, const double alpha)
    {
        FSOLHologramGridLevel level;
        test.TestTrue(what + TEXT(" returns true"), SOLHologramMath::ComputeGridLevel(distanceM, level));
        test.TestEqual(what + TEXT(" minor"), level.MinorSpacingM, minor, HoloMathTol(minor));
        const double major = minor * SOL::Hologram::GRID_SCALE_RATIO;
        test.TestEqual(what + TEXT(" major"), level.MajorSpacingM, major, HoloMathTol(major));
        test.TestEqual(what + TEXT(" alpha"), level.MinorAlpha, alpha, 1e-9);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that ComputeGridLevel rejects a distance and zeroes the output
    void HoloMathExpectNoLevel(FAutomationTestBase& test, const FString& what, const double distanceM)
    {
        FSOLHologramGridLevel level;
        level.MinorSpacingM = 7.0;
        level.MajorSpacingM = 28.0;
        level.MinorAlpha = 0.5;
        test.TestFalse(what + TEXT(" returns false"), SOLHologramMath::ComputeGridLevel(distanceM, level));
        test.TestEqual(what + TEXT(" minor zeroed"), level.MinorSpacingM, 0.0);
        test.TestEqual(what + TEXT(" major zeroed"), level.MajorSpacingM, 0.0);
        test.TestEqual(what + TEXT(" alpha zeroed"), level.MinorAlpha, 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the level invariants (minor <= s < major, major = minor x R, alpha in (0, 1]) for any valid distance
    void HoloMathExpectLevelInvariants(FAutomationTestBase& test, const FString& what, const double distanceM)
    {
        FSOLHologramGridLevel level;
        test.TestTrue(what + TEXT(" returns true"), SOLHologramMath::ComputeGridLevel(distanceM, level));
        const double s = distanceM / SOL::Hologram::GRID_MINOR_LINES_PER_VIEW;
        test.TestTrue(what + TEXT(" minor <= s"), level.MinorSpacingM <= s * (1.0 + HOLO_REL_TOL));
        test.TestTrue(what + TEXT(" s < major"), s < level.MajorSpacingM * (1.0 + HOLO_REL_TOL));
        test.TestEqual(what + TEXT(" major = minor x R"), level.MajorSpacingM,
            level.MinorSpacingM * SOL::Hologram::GRID_SCALE_RATIO, HoloMathTol(level.MajorSpacingM));
        test.TestTrue(what + TEXT(" alpha > 0"), level.MinorAlpha > 0.0);
        test.TestTrue(what + TEXT(" alpha <= 1"), level.MinorAlpha <= 1.0);

        // The minor spacing is an integer power of the ratio
        const double exponent = FMath::Loge(level.MinorSpacingM) / FMath::Loge(SOL::Hologram::GRID_SCALE_RATIO);
        test.TestEqual(what + TEXT(" minor is a power of R"), exponent, FMath::RoundToDouble(exponent), 1e-6);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathGridLevelPowersTest, "SOLTest.HologramMath.GridLevelExactPowers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At s = R^k exactly the minor spacing is R^k, the major R^(k+1) and the minor alpha 1
bool FSOLHologramMathGridLevelPowersTest::RunTest(const FString& /*parameters*/)
{
    const double ratio = SOL::Hologram::GRID_SCALE_RATIO;
    const double perView = SOL::Hologram::GRID_MINOR_LINES_PER_VIEW;
    for (int32 k = -3; k <= 8; ++k)
    {
        const double power = FMath::Pow(ratio, static_cast<double>(k));
        HoloMathExpectLevel(*this, FString::Printf(TEXT("R^%d"), k), power * perView, power, 1.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathGridLevelMidTest, "SOLTest.HologramMath.GridLevelBetweenPowers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Between two powers the level is the lower one and the minor alpha is 1 - frac(log_R(s))
bool FSOLHologramMathGridLevelMidTest::RunTest(const FString& /*parameters*/)
{
    const double ratio = SOL::Hologram::GRID_SCALE_RATIO;
    const double perView = SOL::Hologram::GRID_MINOR_LINES_PER_VIEW;
    const double logR = FMath::Loge(ratio);

    // Several s values, each with its expected level computed independently
    const double samples[] = { 2.0, 3.0, 10.0, 100.0, 1234.5, 0.3, 0.01, 7.5e6 };
    for (const double s : samples)
    {
        const double logS = FMath::Loge(s) / logR;
        const double level = FMath::FloorToDouble(logS);
        const double minor = FMath::Pow(ratio, level);
        const double alpha = 1.0 - (logS - level);
        HoloMathExpectLevel(*this, FString::Printf(TEXT("s=%g"), s), s * perView, minor, alpha);
        HoloMathExpectLevelInvariants(*this, FString::Printf(TEXT("s=%g invariants"), s), s * perView);
    }

    // Geometric midpoint of a level: s = sqrt(R) x R^k has alpha 0.5
    HoloMathExpectLevel(*this, TEXT("geometric midpoint"), FMath::Sqrt(ratio) * ratio * perView, ratio, 0.5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathGridLevelMonotoneTest, "SOLTest.HologramMath.GridLevelAlphaMonotone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Within a level the minor alpha strictly decreases with distance, reaching near 0 just below the next power
bool FSOLHologramMathGridLevelMonotoneTest::RunTest(const FString& /*parameters*/)
{
    const double ratio = SOL::Hologram::GRID_SCALE_RATIO;
    const double perView = SOL::Hologram::GRID_MINOR_LINES_PER_VIEW;
    const double baseS = ratio * ratio;

    // Sweep s across [R^2, R^3) and check the alpha keeps falling while the spacing stays put
    double previousAlpha = 2.0;
    constexpr int32 steps = 50;
    for (int32 step = 0; step < steps; ++step)
    {
        const double s = baseS * FMath::Pow(ratio, static_cast<double>(step) / steps);
        FSOLHologramGridLevel level;
        TestTrue(TEXT("sweep returns true"), SOLHologramMath::ComputeGridLevel(s * perView, level));
        TestEqual(FString::Printf(TEXT("sweep %d minor"), step), level.MinorSpacingM, baseS, HoloMathTol(baseS));
        TestTrue(FString::Printf(TEXT("sweep %d alpha decreasing"), step), level.MinorAlpha < previousAlpha);
        previousAlpha = level.MinorAlpha;
    }

    // Just below the next power: still the same level, alpha near 0
    FSOLHologramGridLevel below;
    TestTrue(TEXT("just below returns true"),
        SOLHologramMath::ComputeGridLevel(baseS * ratio * (1.0 - 1e-9) * perView, below));
    TestEqual(TEXT("just below minor"), below.MinorSpacingM, baseS, HoloMathTol(baseS));
    TestTrue(TEXT("just below alpha > 0"), below.MinorAlpha > 0.0);
    TestTrue(TEXT("just below alpha near 0"), below.MinorAlpha < 1e-6);

    // Just above the next power: the next level, alpha near 1
    FSOLHologramGridLevel above;
    TestTrue(TEXT("just above returns true"),
        SOLHologramMath::ComputeGridLevel(baseS * ratio * (1.0 + 1e-9) * perView, above));
    TestEqual(TEXT("just above minor"), above.MinorSpacingM, baseS * ratio, HoloMathTol(baseS * ratio));
    TestTrue(TEXT("just above alpha near 1"), above.MinorAlpha > 1.0 - 1e-6 && above.MinorAlpha <= 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathGridLevelExtremesTest,
    "SOLTest.HologramMath.GridLevelExtremeDistances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Very large (solar-system) and very small camera distances still give a valid level
bool FSOLHologramMathGridLevelExtremesTest::RunTest(const FString& /*parameters*/)
{
    HoloMathExpectLevelInvariants(*this, TEXT("1e13 m"), 1e13);
    HoloMathExpectLevelInvariants(*this, TEXT("1e-3 m"), 1e-3);

    // The small distance's minor spacing is below one metre (a negative level)
    FSOLHologramGridLevel tinyLevel;
    SOLHologramMath::ComputeGridLevel(1e-3, tinyLevel);
    TestTrue(TEXT("1e-3 m minor below 1"), tinyLevel.MinorSpacingM < 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathGridLevelInvalidTest, "SOLTest.HologramMath.GridLevelInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero, negative and non-finite camera distances return false with the output zeroed
bool FSOLHologramMathGridLevelInvalidTest::RunTest(const FString& /*parameters*/)
{
    HoloMathExpectNoLevel(*this, TEXT("zero"), 0.0);
    HoloMathExpectNoLevel(*this, TEXT("negative"), -100.0);
    HoloMathExpectNoLevel(*this, TEXT("NaN"), HOLO_NAN);
    HoloMathExpectNoLevel(*this, TEXT("+inf"), HOLO_INF);
    HoloMathExpectNoLevel(*this, TEXT("-inf"), -HOLO_INF);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathDistanceFieldTest, "SOLTest.HologramMath.DistanceField",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The LT distance field: 1.2 at 0, symmetric in sign, matches the formula, decreasing for d >= 0, 0 for non-finite
bool FSOLHologramMathDistanceFieldTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("d = 0"), SOLHologramMath::DistanceField(0.0), 1.2, HoloMathTol(1.2));

    // Specific values against the independently computed formula, and sign symmetry
    const double samples[] = { 0.1, 0.5, 1.0, 2.0, 3.0, 4.0, 10.0, 100.0 };
    for (const double d : samples)
    {
        const double expected = HoloMathExpectedField(d);
        TestEqual(FString::Printf(TEXT("d = %g"), d), SOLHologramMath::DistanceField(d), expected,
            HoloMathTol(expected));
        TestEqual(FString::Printf(TEXT("d = -%g symmetric"), d), SOLHologramMath::DistanceField(-d),
            SOLHologramMath::DistanceField(d), HoloMathTol(expected));
    }

    // Strictly decreasing over d >= 0
    double previous = SOLHologramMath::DistanceField(0.0);
    for (int32 step = 1; step <= 100; ++step)
    {
        const double value = SOLHologramMath::DistanceField(step * 0.1);
        TestTrue(FString::Printf(TEXT("decreasing at %d"), step), value < previous);
        previous = value;
    }

    TestEqual(TEXT("NaN"), SOLHologramMath::DistanceField(HOLO_NAN), 0.0);
    TestEqual(TEXT("+inf"), SOLHologramMath::DistanceField(HOLO_INF), 0.0);
    TestEqual(TEXT("-inf"), SOLHologramMath::DistanceField(-HOLO_INF), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathGridFadeTest, "SOLTest.HologramMath.GridFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The grid fade is the distance field inside the taper start, tapers linearly to 0 at the extent and is 0 beyond
bool FSOLHologramMathGridFadeTest::RunTest(const FString& /*parameters*/)
{
    const double extent = SOL::Hologram::GRID_EXTENT_MAJOR_CELLS;
    const double start = SOL::Hologram::GRID_EDGE_TAPER_START * extent;

    // Inside the taper start: exactly the distance field
    const double inside[] = { 0.0, 0.25 * start, 0.5 * start, start };
    for (const double d : inside)
    {
        const double expected = HoloMathExpectedField(d);
        TestEqual(FString::Printf(TEXT("inside %g"), d), SOLHologramMath::GridFade(d), expected, HoloMathTol(expected));
        TestEqual(FString::Printf(TEXT("inside %g equals DistanceField"), d), SOLHologramMath::GridFade(d),
            SOLHologramMath::DistanceField(d), HoloMathTol(expected));
    }

    // Across the taper: field x linear taper
    for (int32 step = 1; step < 10; ++step)
    {
        const double d = start + (extent - start) * step / 10.0;
        const double expected = HoloMathExpectedField(d) * HoloMathExpectedTaper(d);
        TestEqual(FString::Printf(TEXT("taper %g"), d), SOLHologramMath::GridFade(d), expected, HoloMathTol(expected));
    }
    const double mid = 0.5 * (start + extent);
    TestEqual(TEXT("taper midpoint is half the field"), SOLHologramMath::GridFade(mid),
        0.5 * HoloMathExpectedField(mid), HoloMathTol(0.5 * HoloMathExpectedField(mid)));

    // At and beyond the extent: 0
    TestEqual(TEXT("at extent"), SOLHologramMath::GridFade(extent), 0.0, 1e-12);
    TestEqual(TEXT("beyond extent"), SOLHologramMath::GridFade(extent * 1.5), 0.0);
    TestEqual(TEXT("far beyond extent"), SOLHologramMath::GridFade(1e9), 0.0);

    // Sign symmetry
    const double symmetric[] = { 0.5, start, mid, extent, extent * 2.0 };
    for (const double d : symmetric)
    {
        TestEqual(FString::Printf(TEXT("symmetric %g"), d), SOLHologramMath::GridFade(-d), SOLHologramMath::GridFade(d),
            HoloMathTol(SOLHologramMath::GridFade(d)));
    }

    TestEqual(TEXT("NaN"), SOLHologramMath::GridFade(HOLO_NAN), 0.0);
    TestEqual(TEXT("+inf"), SOLHologramMath::GridFade(HOLO_INF), 0.0);
    TestEqual(TEXT("-inf"), SOLHologramMath::GridFade(-HOLO_INF), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathSnapToGridTest, "SOLTest.HologramMath.SnapToGrid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Snapping floors each axis to the spacing (toward -inf); invalid spacings leave the point unchanged
bool FSOLHologramMathSnapToGridTest::RunTest(const FString& /*parameters*/)
{
    // Positive coordinates floor down
    const FVector2d positive = SOLHologramMath::SnapToGrid(FVector2d(5.5, 7.9), 2.0);
    TestEqual(TEXT("positive x"), positive.X, 4.0, 1e-12);
    TestEqual(TEXT("positive y"), positive.Y, 6.0, 1e-12);

    // Negative coordinates floor toward -inf, not toward 0
    const FVector2d negative = SOLHologramMath::SnapToGrid(FVector2d(-5.5, -0.1), 2.0);
    TestEqual(TEXT("negative x"), negative.X, -6.0, 1e-12);
    TestEqual(TEXT("negative y"), negative.Y, -2.0, 1e-12);

    // Exact multiples are unchanged
    const FVector2d exact = SOLHologramMath::SnapToGrid(FVector2d(-8.0, 12.0), 4.0);
    TestEqual(TEXT("exact x"), exact.X, -8.0, 1e-12);
    TestEqual(TEXT("exact y"), exact.Y, 12.0, 1e-12);
    const FVector2d origin = SOLHologramMath::SnapToGrid(FVector2d(0.0, 0.0), 4.0);
    TestEqual(TEXT("origin x"), origin.X, 0.0, 1e-12);
    TestEqual(TEXT("origin y"), origin.Y, 0.0, 1e-12);

    // Large universe-scale coordinates with a large spacing
    const double spacing = FMath::Pow(SOL::Hologram::GRID_SCALE_RATIO, 18.0);
    const FVector2d large = SOLHologramMath::SnapToGrid(FVector2d(1.5e11, -1.5e11), spacing);
    const double expectedX = FMath::FloorToDouble(1.5e11 / spacing) * spacing;
    const double expectedY = FMath::FloorToDouble(-1.5e11 / spacing) * spacing;
    TestEqual(TEXT("large x"), large.X, expectedX, HoloMathTol(expectedX));
    TestEqual(TEXT("large y"), large.Y, expectedY, HoloMathTol(expectedY));

    // Invalid spacings return the point unchanged
    const FVector2d point(3.7, -2.2);
    const double invalid[] = { 0.0, -2.0, HOLO_NAN, HOLO_INF, -HOLO_INF };
    for (const double bad : invalid)
    {
        const FVector2d result = SOLHologramMath::SnapToGrid(point, bad);
        TestEqual(FString::Printf(TEXT("spacing %g x unchanged"), bad), result.X, point.X);
        TestEqual(FString::Printf(TEXT("spacing %g y unchanged"), bad), result.Y, point.Y);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathStemVisibleTest, "SOLTest.HologramMath.IsStemVisible",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A stem shows when |height| is at least the threshold fraction of the camera distance (inclusive)
bool FSOLHologramMathStemVisibleTest::RunTest(const FString& /*parameters*/)
{
    const double distances[] = { 1000.0, 1.0e6, 3.0e11 };
    for (const double distance : distances)
    {
        const double threshold = SOL::Hologram::STEM_MIN_HEIGHT_FRACTION * distance;
        TestTrue(FString::Printf(TEXT("%g: at threshold"), distance),
            SOLHologramMath::IsStemVisible(threshold, distance));
        TestTrue(FString::Printf(TEXT("%g: above threshold"), distance),
            SOLHologramMath::IsStemVisible(threshold * 2.0, distance));
        TestFalse(FString::Printf(TEXT("%g: just below threshold"), distance),
            SOLHologramMath::IsStemVisible(threshold * (1.0 - 1e-6), distance));
        TestTrue(FString::Printf(TEXT("%g: negative at threshold"), distance),
            SOLHologramMath::IsStemVisible(-threshold, distance));
        TestTrue(FString::Printf(TEXT("%g: negative above threshold"), distance),
            SOLHologramMath::IsStemVisible(-threshold * 2.0, distance));
        TestFalse(FString::Printf(TEXT("%g: negative just below threshold"), distance),
            SOLHologramMath::IsStemVisible(-threshold * (1.0 - 1e-6), distance));
        TestFalse(FString::Printf(TEXT("%g: zero height"), distance), SOLHologramMath::IsStemVisible(0.0, distance));
    }

    // Invalid camera distances
    const double invalid[] = { 0.0, -1000.0, HOLO_NAN, HOLO_INF, -HOLO_INF };
    for (const double bad : invalid)
    {
        TestFalse(FString::Printf(TEXT("distance %g"), bad), SOLHologramMath::IsStemVisible(1.0e9, bad));
    }

    // Non-finite heights
    TestFalse(TEXT("NaN height"), SOLHologramMath::IsStemVisible(HOLO_NAN, 1000.0));
    TestFalse(TEXT("+inf height"), SOLHologramMath::IsStemVisible(HOLO_INF, 1000.0));
    TestFalse(TEXT("-inf height"), SOLHologramMath::IsStemVisible(-HOLO_INF, 1000.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathStemAlphaTest, "SOLTest.HologramMath.StemAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stem alpha is STEM_ALPHA x max(STEM_MIN_ALPHA, grid fade) capped at 1, the floor far away and for non-finite d
bool FSOLHologramMathStemAlphaTest::RunTest(const FString& /*parameters*/)
{
    const double floorAlpha = SOL::Hologram::STEM_ALPHA * SOL::Hologram::STEM_MIN_ALPHA;

    // Formula over a sweep, with the grid fade computed independently
    for (int32 step = 0; step <= 60; ++step)
    {
        const double d = step * 0.1;
        const double fade = HoloMathExpectedField(d) * HoloMathExpectedTaper(d);
        const double expected = FMath::Min(1.0,
            SOL::Hologram::STEM_ALPHA * FMath::Max(SOL::Hologram::STEM_MIN_ALPHA, fade));
        const double alpha = SOLHologramMath::StemAlpha(d);
        TestEqual(FString::Printf(TEXT("d = %g"), d), alpha, expected, HoloMathTol(expected));
        TestEqual(FString::Printf(TEXT("d = %g matches GridFade"), d), alpha,
            FMath::Min(1.0, SOL::Hologram::STEM_ALPHA
                * FMath::Max(SOL::Hologram::STEM_MIN_ALPHA, SOLHologramMath::GridFade(d))), HoloMathTol(expected));
        TestTrue(FString::Printf(TEXT("d = %g at most 1"), d), alpha <= 1.0);
        TestTrue(FString::Printf(TEXT("d = %g at least the floor"), d), alpha >= floorAlpha * (1.0 - HOLO_REL_TOL));
        TestEqual(FString::Printf(TEXT("d = -%g symmetric"), d), SOLHologramMath::StemAlpha(-d), alpha,
            HoloMathTol(expected));
    }

    // At the focus: capped to at most 1
    TestTrue(TEXT("focus at most 1"), SOLHologramMath::StemAlpha(0.0) <= 1.0);

    // Far beyond the grid: the floor
    TestEqual(TEXT("far"), SOLHologramMath::StemAlpha(SOL::Hologram::GRID_EXTENT_MAJOR_CELLS * 10.0), floorAlpha,
        HoloMathTol(floorAlpha));
    TestEqual(TEXT("very far"), SOLHologramMath::StemAlpha(1e12), floorAlpha, HoloMathTol(floorAlpha));

    // Non-finite: the floor
    TestEqual(TEXT("NaN"), SOLHologramMath::StemAlpha(HOLO_NAN), floorAlpha, HoloMathTol(floorAlpha));
    TestEqual(TEXT("+inf"), SOLHologramMath::StemAlpha(HOLO_INF), floorAlpha, HoloMathTol(floorAlpha));
    TestEqual(TEXT("-inf"), SOLHologramMath::StemAlpha(-HOLO_INF), floorAlpha, HoloMathTol(floorAlpha));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathNextTransitionTest, "SOLTest.HologramMath.NextTransition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// All eight combinations of shown, map open and look enabled
bool FSOLHologramMathNextTransitionTest::RunTest(const FString& /*parameters*/)
{
    // Not shown: Show only when the map is open and the look enabled
    TestTrue(TEXT("hidden, closed, disabled"),
        SOLHologramMath::NextTransition(false, false, false) == ESOLHologramTransition::None);
    TestTrue(TEXT("hidden, closed, enabled"),
        SOLHologramMath::NextTransition(false, false, true) == ESOLHologramTransition::None);
    TestTrue(TEXT("hidden, open, disabled"),
        SOLHologramMath::NextTransition(false, true, false) == ESOLHologramTransition::None);
    TestTrue(TEXT("hidden, open, enabled"),
        SOLHologramMath::NextTransition(false, true, true) == ESOLHologramTransition::Show);

    // Shown: Hide unless the map is open and the look enabled
    TestTrue(TEXT("shown, closed, disabled"),
        SOLHologramMath::NextTransition(true, false, false) == ESOLHologramTransition::Hide);
    TestTrue(TEXT("shown, closed, enabled"),
        SOLHologramMath::NextTransition(true, false, true) == ESOLHologramTransition::Hide);
    TestTrue(TEXT("shown, open, disabled"),
        SOLHologramMath::NextTransition(true, true, false) == ESOLHologramTransition::Hide);
    TestTrue(TEXT("shown, open, enabled"),
        SOLHologramMath::NextTransition(true, true, true) == ESOLHologramTransition::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramMathClampPostStrengthTest, "SOLTest.HologramMath.ClampPostStrength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Post strength clamps into [0, POST_STRENGTH_MAX]; non-finite values give POST_STRENGTH_DEFAULT
bool FSOLHologramMathClampPostStrengthTest::RunTest(const FString& /*parameters*/)
{
    const double maxStrength = SOL::Hologram::POST_STRENGTH_MAX;
    const double defaultStrength = SOL::Hologram::POST_STRENGTH_DEFAULT;

    // In range: unchanged
    TestEqual(TEXT("zero"), SOLHologramMath::ClampPostStrength(0.0), 0.0);
    TestEqual(TEXT("quarter of max"), SOLHologramMath::ClampPostStrength(0.25 * maxStrength), 0.25 * maxStrength);
    TestEqual(TEXT("default"), SOLHologramMath::ClampPostStrength(defaultStrength), defaultStrength);
    TestEqual(TEXT("max"), SOLHologramMath::ClampPostStrength(maxStrength), maxStrength);

    // Out of range: clamped
    TestEqual(TEXT("negative"), SOLHologramMath::ClampPostStrength(-0.5), 0.0);
    TestEqual(TEXT("very negative"), SOLHologramMath::ClampPostStrength(-1e30), 0.0);
    TestEqual(TEXT("above max"), SOLHologramMath::ClampPostStrength(maxStrength + 1.0), maxStrength);
    TestEqual(TEXT("far above max"), SOLHologramMath::ClampPostStrength(1e30), maxStrength);

    // Non-finite: the default
    TestEqual(TEXT("NaN"), SOLHologramMath::ClampPostStrength(HOLO_NAN), defaultStrength);
    TestEqual(TEXT("+inf"), SOLHologramMath::ClampPostStrength(HOLO_INF), defaultStrength);
    TestEqual(TEXT("-inf"), SOLHologramMath::ClampPostStrength(-HOLO_INF), defaultStrength);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
