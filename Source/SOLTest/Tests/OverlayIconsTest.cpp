/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLOverlayIcons.h"

#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #22 overlay icons and hologram cursor (SDD 22 sections 2.1 and 2.5), written against the
// contract in Docs/Plans/22-overlay-icons-and-interaction-menus-plan.md before the production code existed.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    using SOLOverlayIcons::EIconCategory;

    constexpr double OVERLAYICONS_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double OVERLAYICONS_INF = std::numeric_limits<double>::infinity();
    constexpr double OVERLAYICONS_TOLERANCE = 1.0e-9;
    constexpr double OVERLAYICONS_PIN_TOLERANCE = 1.0e-6;
    constexpr int32 OVERLAYICONS_CATEGORY_COUNT = static_cast<int32>(EIconCategory::Count);

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected smoothstep fade of the contract, restated so the tests stay independent
    double OverlayIconsExpectedFade(const double radiusPx)
    {
        const double t = (radiusPx - SOL::Overlay::ICON_FADE_START_PX)
            / (SOL::Overlay::ICON_FADE_END_PX - SOL::Overlay::ICON_FADE_START_PX);
        return 1.0 - t * t * (3.0 - 2.0 * t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected log-linear darkening of the contract between the near and far distances
    double OverlayIconsExpectedDarkening(const double distanceM)
    {
        const double fraction = (FMath::LogX(10.0, distanceM) - FMath::LogX(10.0, SOL::Overlay::DARKEN_NEAR_M))
            / (FMath::LogX(10.0, SOL::Overlay::DARKEN_FAR_M) - FMath::LogX(10.0, SOL::Overlay::DARKEN_NEAR_M));
        return 1.0 + (SOL::Overlay::DARKEN_FLOOR - 1.0) * fraction;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a category's printable name for test messages
    FString OverlayIconsCategoryName(const EIconCategory category)
    {
        static const TCHAR* names[] = { TEXT("Star"), TEXT("Planet"), TEXT("Moon"), TEXT("Dwarf"), TEXT("Asteroid"),
            TEXT("Ring"), TEXT("Ship"), TEXT("Target"), TEXT("Count") };
        return names[FMath::Clamp(static_cast<int32>(category), 0, OVERLAYICONS_CATEGORY_COUNT)];
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts every vertex from firstShape on lies within reach of the centre on both axes
    void OverlayIconsCheckVertexReach(FAutomationTestBase& test, const FSOLHudShapeBatch& batch,
        const int32 firstShape, const FVector2f& center, const float reach, const FString& what)
    {
        const TArray<FSlateVertex>& vertices = batch.GetVertices();
        for (int32 index = firstShape * SOL::Hologram::HUD_VERTICES_PER_SHAPE; index < vertices.Num(); ++index)
        {
            const FVector2f position(vertices[index].Position.X, vertices[index].Position.Y);
            const bool bInside = FMath::Abs(position.X - center.X) <= reach
                && FMath::Abs(position.Y - center.Y) <= reach;
            test.TestTrue(FString::Printf(TEXT("%s: vertex %d (%.2f, %.2f) within %.2f of the centre"), *what, index,
                position.X, position.Y, reach), bInside);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a pinned position lies inside the screen shrunk by the margin and its direction is a unit vector
    void OverlayIconsCheckPinInside(FAutomationTestBase& test, const SOLOverlayIcons::FEdgePin& pin,
        const FVector2D& viewSize, const double margin, const FString& what)
    {
        test.TestTrue(what + TEXT(": x inside"), pin.PositionPx.X >= margin - OVERLAYICONS_PIN_TOLERANCE
            && pin.PositionPx.X <= viewSize.X - margin + OVERLAYICONS_PIN_TOLERANCE);
        test.TestTrue(what + TEXT(": y inside"), pin.PositionPx.Y >= margin - OVERLAYICONS_PIN_TOLERANCE
            && pin.PositionPx.Y <= viewSize.Y - margin + OVERLAYICONS_PIN_TOLERANCE);
        test.TestEqual(what + TEXT(": unit direction"), pin.Direction.Size(), 1.0, OVERLAYICONS_PIN_TOLERANCE);
    }
}

// --- Categories ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsCategoryOverlayTest, "SOLTest.OverlayIcons.CategoryOverlay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each icon category maps to its Overlay Menu row, and Count to Count
bool FSOLOverlayIconsCategoryOverlayTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    using SOLOverlay::EOverlay;
    const EOverlay expected[] = { EOverlay::IconStars, EOverlay::IconPlanets, EOverlay::IconMoons,
        EOverlay::IconDwarfs, EOverlay::IconAsteroids, EOverlay::IconRings, EOverlay::IconShips,
        EOverlay::IconTargets };
    TestEqual(TEXT("one row per category"), static_cast<int32>(UE_ARRAY_COUNT(expected)), OVERLAYICONS_CATEGORY_COUNT);
    for (int32 index = 0; index < OVERLAYICONS_CATEGORY_COUNT; ++index)
    {
        const EIconCategory category = static_cast<EIconCategory>(index);
        TestTrue(FString::Printf(TEXT("%s row"), *OverlayIconsCategoryName(category)),
            CategoryOverlay(category) == expected[index]);
    }
    TestTrue(TEXT("Count -> Count"), CategoryOverlay(EIconCategory::Count) == EOverlay::Count);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsClassifyBodyTest, "SOLTest.OverlayIcons.ClassifyBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No parent is the Star, a Sun child is a Planet up to the last planet and a Dwarf after it, any other parent a Moon
bool FSOLOverlayIconsClassifyBodyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    constexpr int32 lastPlanet = 8;
    TestTrue(TEXT("Sun is the Star"), ClassifyBody(0, INDEX_NONE, lastPlanet) == EIconCategory::Star);
    TestTrue(TEXT("Mercury is a Planet"), ClassifyBody(1, 0, lastPlanet) == EIconCategory::Planet);
    TestTrue(TEXT("Earth is a Planet"), ClassifyBody(3, 0, lastPlanet) == EIconCategory::Planet);
    TestTrue(TEXT("the last planet is a Planet"), ClassifyBody(lastPlanet, 0, lastPlanet) == EIconCategory::Planet);
    TestTrue(TEXT("the body after it is a Dwarf"), ClassifyBody(lastPlanet + 1, 0, lastPlanet)
        == EIconCategory::Dwarf);
    TestTrue(TEXT("a later Sun child is a Dwarf"), ClassifyBody(lastPlanet + 5, 0, lastPlanet)
        == EIconCategory::Dwarf);
    TestTrue(TEXT("Earth's Moon is a Moon"), ClassifyBody(lastPlanet + 10, 3, lastPlanet) == EIconCategory::Moon);
    TestTrue(TEXT("a dwarf's moon (Charon) is a Moon"), ClassifyBody(lastPlanet + 11, lastPlanet + 1, lastPlanet)
        == EIconCategory::Moon);
    TestTrue(TEXT("the parent decides, not the index"), ClassifyBody(2, 5, lastPlanet) == EIconCategory::Moon);
    return true;
}

// --- Brightness ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsSizeFadeTest, "SOLTest.OverlayIcons.SizeFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The fade is 1 up to the start radius, 0 from the end radius, a monotonic smoothstep between; bad radii give 1
bool FSOLOverlayIconsSizeFadeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const double start = SOL::Overlay::ICON_FADE_START_PX;
    const double end = SOL::Overlay::ICON_FADE_END_PX;
    TestEqual(TEXT("constants: start"), start, 6.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("constants: end"), end, 30.0, OVERLAYICONS_TOLERANCE);

    TestEqual(TEXT("zero radius"), SizeFade(0.0), 1.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("below start"), SizeFade(start * 0.5), 1.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("at start"), SizeFade(start), 1.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("at end"), SizeFade(end), 0.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("beyond end"), SizeFade(end * 10.0), 0.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("infinite radius"), SizeFade(OVERLAYICONS_INF), 0.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("midpoint is half"), SizeFade((start + end) * 0.5), 0.5, OVERLAYICONS_TOLERANCE);
    const double quarter = start + (end - start) * 0.25;
    TestEqual(TEXT("quarter is the smoothstep"), SizeFade(quarter), 0.84375, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("negative radius"), SizeFade(-5.0), 1.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("NaN radius"), SizeFade(OVERLAYICONS_NAN), 1.0, OVERLAYICONS_TOLERANCE);

    // Matches the formula and never increases across the fade band
    double previous = SizeFade(start);
    for (int32 step = 1; step <= 48; ++step)
    {
        const double radius = start + (end - start) * step / 48.0;
        const double fade = SizeFade(radius);
        TestEqual(FString::Printf(TEXT("formula at %.2f px"), radius), fade, OverlayIconsExpectedFade(radius), 1.0e-6);
        TestTrue(FString::Printf(TEXT("non-increasing at %.2f px"), radius), fade <= previous + OVERLAYICONS_TOLERANCE);
        TestTrue(FString::Printf(TEXT("in [0, 1] at %.2f px"), radius), fade >= 0.0 && fade <= 1.0);
        previous = fade;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsDarkeningTest, "SOLTest.OverlayIcons.DistanceDarkening",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Darkening off is always 1; on, it is 1 up to NEAR, the floor from FAR and linear in log10 between; bad distances 1
bool FSOLOverlayIconsDarkeningTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const double nearM = SOL::Overlay::DARKEN_NEAR_M;
    const double farM = SOL::Overlay::DARKEN_FAR_M;
    const double floor = SOL::Overlay::DARKEN_FLOOR;
    TestEqual(TEXT("constants: near"), nearM, 1.0e7, 1.0);
    TestEqual(TEXT("constants: far"), farM, 1.0e12, 1.0);
    TestEqual(TEXT("constants: floor"), floor, 0.25, OVERLAYICONS_TOLERANCE);

    // Off: 1 everywhere
    const double distances[] = { -1.0, 0.0, 1.0, nearM, 1.0e9, farM, farM * 1000.0, OVERLAYICONS_NAN };
    for (const double distance : distances)
    {
        TestEqual(FString::Printf(TEXT("off at %g m"), distance), DistanceDarkening(distance, false), 1.0,
            OVERLAYICONS_TOLERANCE);
    }

    // On: the ends and the plateaus
    TestEqual(TEXT("on: 1 m"), DistanceDarkening(1.0, true), 1.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("on: at near"), DistanceDarkening(nearM, true), 1.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("on: at far"), DistanceDarkening(farM, true), floor, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("on: beyond far"), DistanceDarkening(farM * 1000.0, true), floor, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("on: log midpoint"), DistanceDarkening(FMath::Pow(10.0, 9.5), true), (1.0 + floor) * 0.5,
        1.0e-9);
    TestEqual(TEXT("on: 1e8 m"), DistanceDarkening(1.0e8, true), OverlayIconsExpectedDarkening(1.0e8), 1.0e-9);
    TestEqual(TEXT("on: 3e10 m"), DistanceDarkening(3.0e10, true), OverlayIconsExpectedDarkening(3.0e10), 1.0e-9);

    // On: bad distances
    TestEqual(TEXT("on: zero"), DistanceDarkening(0.0, true), 1.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("on: negative"), DistanceDarkening(-1.0e9, true), 1.0, OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("on: NaN"), DistanceDarkening(OVERLAYICONS_NAN, true), 1.0, OVERLAYICONS_TOLERANCE);

    // Monotonic non-increasing over the band
    double previous = DistanceDarkening(nearM, true);
    for (int32 step = 1; step <= 50; ++step)
    {
        const double distance = FMath::Pow(10.0, 7.0 + 5.0 * step / 50.0);
        const double value = DistanceDarkening(distance, true);
        TestTrue(FString::Printf(TEXT("non-increasing at %g m"), distance), value <= previous + OVERLAYICONS_TOLERANCE);
        previous = value;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsBrightnessTest, "SOLTest.OverlayIcons.IconBrightness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Icon brightness is the size fade times the distance darkening
bool FSOLOverlayIconsBrightnessTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const double radii[] = { 0.0, 10.0, 18.0, 29.0, 40.0, -1.0 };
    const double distances[] = { 1.0e6, 1.0e8, 1.0e10, 1.0e13 };
    for (const double radius : radii)
    {
        for (const double distance : distances)
        {
            for (const bool bDarkening : { false, true })
            {
                TestEqual(FString::Printf(TEXT("%.1f px, %g m, darkening %d"), radius, distance, bDarkening ? 1 : 0),
                    IconBrightness(radius, distance, bDarkening),
                    SizeFade(radius) * DistanceDarkening(distance, bDarkening), OVERLAYICONS_TOLERANCE);
            }
        }
    }
    TestEqual(TEXT("small and far, darkened"), IconBrightness(1.0, 1.0e13, true), SOL::Overlay::DARKEN_FLOOR,
        OVERLAYICONS_TOLERANCE);
    TestEqual(TEXT("large object hides the icon"), IconBrightness(100.0, 1.0e8, false), 0.0, OVERLAYICONS_TOLERANCE);
    return true;
}

// --- Screen placement ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsPinOnScreenTest, "SOLTest.OverlayIcons.PinOnScreen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A point ahead whose projection lies inside the shrunk screen is placed at its projection with no direction
bool FSOLOverlayIconsPinOnScreenTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const FVector2D viewSize(1920.0, 1080.0);
    const FVector2D center(960.0, 540.0);
    constexpr double focal = 1000.0;
    constexpr double margin = 20.0;

    const FEdgePin ahead = PinToScreen(FVector(1000.0, 100.0, 50.0), center, focal, viewSize, margin);
    TestTrue(TEXT("ahead: on screen"), ahead.bOnScreen);
    TestEqual(TEXT("ahead: x"), ahead.PositionPx.X, 1060.0, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("ahead: y (up is screen up)"), ahead.PositionPx.Y, 490.0, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("ahead: no direction"), ahead.Direction.Size(), 0.0, OVERLAYICONS_PIN_TOLERANCE);

    const FEdgePin centred = PinToScreen(FVector(5.0e9, 0.0, 0.0), center, focal, viewSize, margin);
    TestTrue(TEXT("dead ahead: on screen"), centred.bOnScreen);
    TestEqual(TEXT("dead ahead: centre x"), centred.PositionPx.X, center.X, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("dead ahead: centre y"), centred.PositionPx.Y, center.Y, OVERLAYICONS_PIN_TOLERANCE);

    // Just inside the shrunk edge still counts as on screen
    const FEdgePin nearEdge = PinToScreen(FVector(1000.0, 939.0, 0.0), center, focal, viewSize, margin);
    TestTrue(TEXT("just inside the margin line: on screen"), nearEdge.bOnScreen);
    TestEqual(TEXT("just inside the margin line: x"), nearEdge.PositionPx.X, viewSize.X - margin - 1.0, 1.0e-6);

    // Inside the screen but within the margin is pinned
    const FEdgePin inMargin = PinToScreen(FVector(1000.0, 950.0, 0.0), center, focal, viewSize, margin);
    TestFalse(TEXT("inside the margin band: pinned"), inMargin.bOnScreen);
    TestEqual(TEXT("inside the margin band: on the right edge"), inMargin.PositionPx.X, viewSize.X - margin,
        OVERLAYICONS_PIN_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsPinEdgeTest, "SOLTest.OverlayIcons.PinToEdge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Off-screen and behind-camera points pin to the shrunk screen edge along their lateral direction
bool FSOLOverlayIconsPinEdgeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const FVector2D viewSize(1920.0, 1080.0);
    const FVector2D center(960.0, 540.0);
    constexpr double focal = 1000.0;
    constexpr double margin = 20.0;

    // Behind and to the right: right edge, centre height
    const FEdgePin behindRight = PinToScreen(FVector(-1000.0, 500.0, 0.0), center, focal, viewSize, margin);
    TestFalse(TEXT("behind right: pinned"), behindRight.bOnScreen);
    TestEqual(TEXT("behind right: direction x"), behindRight.Direction.X, 1.0, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("behind right: direction y"), behindRight.Direction.Y, 0.0, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("behind right: x on the right edge"), behindRight.PositionPx.X, viewSize.X - margin,
        OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("behind right: y centred"), behindRight.PositionPx.Y, center.Y, OVERLAYICONS_PIN_TOLERANCE);

    // Straight behind: no lateral offset pins downward to the bottom edge
    const FEdgePin behind = PinToScreen(FVector(-1000.0, 0.0, 0.0), center, focal, viewSize, margin);
    TestFalse(TEXT("straight behind: pinned"), behind.bOnScreen);
    TestEqual(TEXT("straight behind: direction down x"), behind.Direction.X, 0.0, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("straight behind: direction down y"), behind.Direction.Y, 1.0, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("straight behind: x centred"), behind.PositionPx.X, center.X, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("straight behind: bottom edge"), behind.PositionPx.Y, viewSize.Y - margin,
        OVERLAYICONS_PIN_TOLERANCE);

    // Ahead but beyond the left edge: left edge at the height of the ray
    const FEdgePin left = PinToScreen(FVector(1000.0, -2000.0, 100.0), center, focal, viewSize, margin);
    TestFalse(TEXT("beyond left: pinned"), left.bOnScreen);
    TestEqual(TEXT("beyond left: left edge"), left.PositionPx.X, margin, OVERLAYICONS_PIN_TOLERANCE);
    const double rayY = center.Y + (center.X - margin) * (-100.0 / 2000.0);
    TestEqual(TEXT("beyond left: y along the ray"), left.PositionPx.Y, rayY, 1.0e-4);
    TestTrue(TEXT("beyond left: points left"), left.Direction.X < 0.0);
    TestTrue(TEXT("beyond left: points slightly up"), left.Direction.Y < 0.0);

    // Ahead but above the top edge: top edge
    const FEdgePin above = PinToScreen(FVector(1000.0, 0.0, 1000.0), center, focal, viewSize, margin);
    TestFalse(TEXT("above: pinned"), above.bOnScreen);
    TestEqual(TEXT("above: x centred"), above.PositionPx.X, center.X, OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("above: top edge"), above.PositionPx.Y, margin, OVERLAYICONS_PIN_TOLERANCE);

    // Behind, diagonal: the nearer edge along the direction wins
    const FEdgePin diagonal = PinToScreen(FVector(-10.0, 1.0, -1.0), center, focal, viewSize, margin);
    TestFalse(TEXT("behind diagonal: pinned"), diagonal.bOnScreen);
    TestEqual(TEXT("behind diagonal: bottom edge (shorter reach)"), diagonal.PositionPx.Y, viewSize.Y - margin,
        OVERLAYICONS_PIN_TOLERANCE);
    TestEqual(TEXT("behind diagonal: x along the diagonal"), diagonal.PositionPx.X, center.X + (center.Y - margin),
        1.0e-4);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsPinInsideTest, "SOLTest.OverlayIcons.PinStaysInside",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every pinned position lies inside the shrunk screen with a unit direction, and every on-screen one too
bool FSOLOverlayIconsPinInsideTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const FVector2D viewSize(1280.0, 720.0);
    const FVector2D center(640.0, 360.0);
    constexpr double focal = 700.0;
    constexpr double margin = 24.0;
    FRandomStream random(2201);
    int32 pinnedCount = 0;
    int32 onScreenCount = 0;
    for (int32 sample = 0; sample < 400; ++sample)
    {
        const FVector local(random.FRandRange(-1.0e6, 1.0e6), random.FRandRange(-1.0e6, 1.0e6),
            random.FRandRange(-1.0e6, 1.0e6));
        const FEdgePin pin = PinToScreen(local, center, focal, viewSize, margin);
        const FString what = FString::Printf(TEXT("sample %d"), sample);
        if (pin.bOnScreen)
        {
            ++onScreenCount;
            TestTrue(what + TEXT(": on screen only ahead"), local.X > 0.0);
            TestTrue(what + TEXT(": on-screen x inside"), pin.PositionPx.X >= margin - OVERLAYICONS_PIN_TOLERANCE
                && pin.PositionPx.X <= viewSize.X - margin + OVERLAYICONS_PIN_TOLERANCE);
            TestTrue(what + TEXT(": on-screen y inside"), pin.PositionPx.Y >= margin - OVERLAYICONS_PIN_TOLERANCE
                && pin.PositionPx.Y <= viewSize.Y - margin + OVERLAYICONS_PIN_TOLERANCE);
        }
        else
        {
            ++pinnedCount;
            OverlayIconsCheckPinInside(*this, pin, viewSize, margin, what);
            const FVector2D lateral(local.Y, -local.Z);
            TestTrue(what + TEXT(": direction follows the lateral offset"),
                FVector2D::DotProduct(pin.Direction, lateral.GetSafeNormal()) > 1.0 - 1.0e-6);
        }
    }
    TestTrue(TEXT("some samples pinned"), pinnedCount > 0);
    TestTrue(TEXT("some samples on screen"), onScreenCount > 0);
    return true;
}

// --- Budget ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsSelectBudgetedTest, "SOLTest.OverlayIcons.SelectBudgeted",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The selected object comes first, then the nearest others (ties by index), capped; non-finite distances skipped
bool FSOLOverlayIconsSelectBudgetedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const TArray<double> distances = { 5.0, 1.0, 3.0, OVERLAYICONS_NAN, 1.0, OVERLAYICONS_INF, 2.0,
        -OVERLAYICONS_INF };
    TArray<int32> out;

    SelectBudgeted(distances, 6, 4, out);
    TestTrue(TEXT("selected first, then nearest, ties by index"), out == TArray<int32>({ 6, 1, 4, 2 }));

    SelectBudgeted(distances, 0, 2, out);
    TestTrue(TEXT("the far selected one still comes first"), out == TArray<int32>({ 0, 1 }));

    SelectBudgeted(distances, INDEX_NONE, 3, out);
    TestTrue(TEXT("no selection: nearest three"), out == TArray<int32>({ 1, 4, 6 }));

    SelectBudgeted(distances, 3, 3, out);
    TestTrue(TEXT("a NaN selection is not added"), out == TArray<int32>({ 1, 4, 6 }));

    SelectBudgeted(distances, 5, 3, out);
    TestTrue(TEXT("an infinite selection is not added"), out == TArray<int32>({ 1, 4, 6 }));

    SelectBudgeted(distances, 99, 3, out);
    TestTrue(TEXT("an out-of-range selection is ignored"), out == TArray<int32>({ 1, 4, 6 }));

    SelectBudgeted(distances, INDEX_NONE, 100, out);
    TestTrue(TEXT("a large cap gives every finite distance"), out == TArray<int32>({ 1, 4, 6, 2, 0 }));

    SelectBudgeted(distances, 6, 100, out);
    TestTrue(TEXT("the selection is not listed twice"), out == TArray<int32>({ 6, 1, 4, 2, 0 }));

    // A zero or negative cap gives nothing, and earlier contents are always cleared
    out = { 42, 43 };
    SelectBudgeted(distances, 6, 0, out);
    TestEqual(TEXT("cap 0: empty"), out.Num(), 0);
    out = { 42, 43 };
    SelectBudgeted(distances, 6, -3, out);
    TestEqual(TEXT("negative cap: empty"), out.Num(), 0);
    out = { 42, 43 };
    SelectBudgeted(TArray<double>(), INDEX_NONE, 5, out);
    TestEqual(TEXT("no distances: empty"), out.Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsSelectBudgetedNoAllocTest,
    "SOLTest.OverlayIcons.SelectBudgetedNoAlloc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With capacity reserved for the cap, a large selection does not reallocate and returns the nearest in order
bool FSOLOverlayIconsSelectBudgetedNoAllocTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    constexpr int32 cap = 24;
    constexpr int32 count = 1000;
    FRandomStream random(2202);
    TArray<double> distances;
    distances.Reserve(count);
    for (int32 index = 0; index < count; ++index)
    {
        distances.Add(random.FRandRange(1.0e3, 1.0e12));
    }
    constexpr int32 selected = 777;

    TArray<int32> out;
    out.Reserve(cap);
    const SIZE_T allocatedBefore = out.GetAllocatedSize();
    SelectBudgeted(distances, selected, cap, out);
    TestTrue(TEXT("no reallocation"), out.GetAllocatedSize() == allocatedBefore);
    TestEqual(TEXT("filled to the cap"), out.Num(), cap);
    if (out.Num() != cap)
    {
        return false;
    }
    TestEqual(TEXT("selected first"), out[0], selected);

    // The rest are the cap - 1 nearest others, ascending
    TArray<int32> sorted;
    for (int32 index = 0; index < count; ++index)
    {
        if (index != selected)
        {
            sorted.Add(index);
        }
    }
    sorted.Sort([&distances](const int32 a, const int32 b)
    {
        return distances[a] < distances[b] || (distances[a] == distances[b] && a < b);
    });
    for (int32 slot = 1; slot < cap; ++slot)
    {
        TestEqual(FString::Printf(TEXT("slot %d is the %d-th nearest"), slot, slot), out[slot], sorted[slot - 1]);
    }

    // A second call into the same array still does not reallocate
    SelectBudgeted(distances, INDEX_NONE, cap, out);
    TestTrue(TEXT("second call: no reallocation"), out.GetAllocatedSize() == allocatedBefore);
    TestEqual(TEXT("second call: filled to the cap"), out.Num(), cap);
    const bool bSelectedNearest = distances[selected] < distances[sorted[0]];
    TestEqual(TEXT("second call: nearest overall first"), out[0], bSelectedNearest ? selected : sorted[0]);
    return true;
}

// --- Shapes ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsCategoryColorTest, "SOLTest.OverlayIcons.CategoryColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every category has an opaque colour, and the Star and Target colours differ from the Planet colour
bool FSOLOverlayIconsCategoryColorTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    for (int32 index = 0; index < OVERLAYICONS_CATEGORY_COUNT; ++index)
    {
        const EIconCategory category = static_cast<EIconCategory>(index);
        TestEqual(FString::Printf(TEXT("%s opaque"), *OverlayIconsCategoryName(category)), CategoryColor(category).A,
            1.0f, 1.0e-6f);
    }
    TestFalse(TEXT("Star differs from Planet"), CategoryColor(EIconCategory::Star).Equals(
        CategoryColor(EIconCategory::Planet)));
    TestFalse(TEXT("Target differs from Planet"), CategoryColor(EIconCategory::Target).Equals(
        CategoryColor(EIconCategory::Planet)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsShapesTest, "SOLTest.OverlayIcons.AddIconShapes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each category adds 1 to MAX_SHAPES_PER_ICON shapes around its centre and reports exactly what it added
bool FSOLOverlayIconsShapesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const FVector2f center(400.0f, 300.0f);
    constexpr float radius = 10.0f;
    constexpr float pad = 2.0f;
    const FLinearColor color = FLinearColor::White;
    TestEqual(TEXT("constant: shapes per icon"), SOL::Overlay::MAX_SHAPES_PER_ICON, 4);

    for (int32 index = 0; index < OVERLAYICONS_CATEGORY_COUNT; ++index)
    {
        const EIconCategory category = static_cast<EIconCategory>(index);
        const FString name = OverlayIconsCategoryName(category);
        FSOLHudShapeBatch batch;
        batch.Initialize(64);
        const int32 added = AddIconShapes(batch, category, center, radius, color, 1.0f, pad);
        TestTrue(FString::Printf(TEXT("%s adds 1..MAX shapes"), *name), added >= 1
            && added <= SOL::Overlay::MAX_SHAPES_PER_ICON);
        TestEqual(FString::Printf(TEXT("%s count matches"), *name), batch.GetShapeCount(), added);
        TestEqual(FString::Printf(TEXT("%s nothing dropped"), *name), batch.GetDroppedCount(), 0);

        // The vertices stay near the icon (the ring icon is a wider ellipse)
        float reach = radius * 3.0f + pad + 2.0f;
        if (category == EIconCategory::Ring)
        {
            reach = FMath::Max(reach, SOL::Overlay::RING_ICON_SCALE * radius * 1.2f + pad + 2.0f);
        }
        OverlayIconsCheckVertexReach(*this, batch, 0, center, reach, name);

        // A second icon in the same batch grows the count by exactly what it reports
        const FVector2f second(100.0f, 50.0f);
        const int32 before = batch.GetShapeCount();
        const int32 addedAgain = AddIconShapes(batch, category, second, radius, color, 0.5f, pad);
        TestEqual(FString::Printf(TEXT("%s second icon same shape count"), *name), addedAgain, added);
        TestEqual(FString::Printf(TEXT("%s second icon count"), *name), batch.GetShapeCount(), before + addedAgain);
        OverlayIconsCheckVertexReach(*this, batch, before, second, reach, name + TEXT(" second"));
    }

    // Nothing for Count or a non-positive radius
    FSOLHudShapeBatch batch;
    batch.Initialize(64);
    TestEqual(TEXT("Count adds nothing"), AddIconShapes(batch, EIconCategory::Count, center, radius, color, 1.0f, pad),
        0);
    TestEqual(TEXT("zero radius adds nothing"), AddIconShapes(batch, EIconCategory::Planet, center, 0.0f, color, 1.0f,
        pad), 0);
    TestEqual(TEXT("negative radius adds nothing"), AddIconShapes(batch, EIconCategory::Moon, center, -4.0f, color,
        1.0f, pad), 0);
    TestEqual(TEXT("batch still empty"), batch.GetShapeCount(), 0);

    // A full batch adds nothing and counts the drops
    FSOLHudShapeBatch full;
    full.Initialize(0);
    TestEqual(TEXT("full batch adds nothing"), AddIconShapes(full, EIconCategory::Star, center, radius, color, 1.0f,
        pad), 0);
    TestEqual(TEXT("full batch stays empty"), full.GetShapeCount(), 0);
    TestTrue(TEXT("full batch counts the drop"), full.GetDroppedCount() > 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsEdgeArrowTest, "SOLTest.OverlayIcons.AddEdgeArrow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An edge arrow is two shapes for a unit direction and a positive length, and nothing otherwise
bool FSOLOverlayIconsEdgeArrowTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const FVector2f base(1900.0f, 540.0f);
    const FLinearColor color = FLinearColor::White;
    FSOLHudShapeBatch batch;
    batch.Initialize(16);

    TestEqual(TEXT("right arrow"), AddEdgeArrow(batch, base, FVector2f(1.0f, 0.0f), 12.0f, color, 1.0f, 2.0f), 2);
    TestEqual(TEXT("two shapes"), batch.GetShapeCount(), 2);
    TestEqual(TEXT("diagonal arrow"), AddEdgeArrow(batch, base, FVector2f(0.6f, 0.8f), 12.0f, color, 1.0f, 2.0f), 2);
    TestEqual(TEXT("four shapes"), batch.GetShapeCount(), 4);

    TestEqual(TEXT("zero direction"), AddEdgeArrow(batch, base, FVector2f::ZeroVector, 12.0f, color, 1.0f, 2.0f), 0);
    TestEqual(TEXT("zero length"), AddEdgeArrow(batch, base, FVector2f(1.0f, 0.0f), 0.0f, color, 1.0f, 2.0f), 0);
    TestEqual(TEXT("negative length"), AddEdgeArrow(batch, base, FVector2f(0.0f, 1.0f), -5.0f, color, 1.0f, 2.0f), 0);
    TestEqual(TEXT("still four shapes"), batch.GetShapeCount(), 4);

    FSOLHudShapeBatch full;
    full.Initialize(0);
    TestEqual(TEXT("full batch"), AddEdgeArrow(full, base, FVector2f(1.0f, 0.0f), 12.0f, color, 1.0f, 2.0f), 0);
    TestEqual(TEXT("full batch stays empty"), full.GetShapeCount(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayIconsExtentTest, "SOLTest.OverlayIcons.ShapeExtents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// IconExtent / EdgeArrowExtent are the box the drawn shapes span: inside the shapes' quads, and the quads within a line
// width of it (no glow pad); empty for invalid input
bool FSOLOverlayIconsExtentTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlayIcons;
    const FVector2f center(300.0f, 200.0f);
    const float radius = 10.0f;
    const float slack = 2.0f * SOL::Hologram::HUD_LINE_HALF_WIDTH_PX + 1.0e-3f;
    FSOLHudShapeBatch batch;
    batch.Initialize(16);

    // Compares an extent with the vertex box of the shapes just added
    const auto checkExtent = [this, &batch, slack](const FBox2f& extent, const FString& what)
    {
        FBox2f quads(ForceInit);
        for (const FSlateVertex& vertex : batch.GetVertices())
        {
            quads += FVector2f(vertex.Position.X, vertex.Position.Y);
        }
        TestTrue(what + TEXT(": valid"), extent.bIsValid != 0 && quads.bIsValid != 0);
        TestTrue(what + TEXT(": inside the quads"), quads.IsInsideOrOn(extent.Min) && quads.IsInsideOrOn(extent.Max));
        TestTrue(what + TEXT(": the quads within a line width"), extent.ExpandBy(slack).IsInsideOrOn(quads.Min)
            && extent.ExpandBy(slack).IsInsideOrOn(quads.Max));
    };
    for (int32 category = 0; category < static_cast<int32>(EIconCategory::Count); ++category)
    {
        const EIconCategory value = static_cast<EIconCategory>(category);
        batch.Reset();
        AddIconShapes(batch, value, center, radius, FLinearColor::White, 1.0f, 0.0f);
        checkExtent(IconExtent(value, center, radius), FString::Printf(TEXT("icon %d"), category));
    }
    batch.Reset();
    AddEdgeArrow(batch, center, FVector2f(0.6f, 0.8f), 12.0f, FLinearColor::White, 1.0f, 0.0f);
    checkExtent(EdgeArrowExtent(center, FVector2f(0.6f, 0.8f), 12.0f), TEXT("edge arrow"));

    TestFalse(TEXT("Count has no extent"), IconExtent(EIconCategory::Count, center, radius).bIsValid != 0);
    TestFalse(TEXT("zero radius has no extent"), IconExtent(EIconCategory::Planet, center, 0.0f).bIsValid != 0);
    TestFalse(TEXT("zero direction has no extent"),
        EdgeArrowExtent(center, FVector2f::ZeroVector, 12.0f).bIsValid != 0);
    TestFalse(TEXT("zero length has no extent"), EdgeArrowExtent(center, FVector2f(1.0f, 0.0f), 0.0f).bIsValid != 0);
    return true;
}

// --- Hologram cursor ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramCursorArrowTest, "SOLTest.HologramCursor.ArrowSegments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The arrow lies right of and below its tip at (0, 0), reaches CURSOR_LENGTH_PX x scale and scales linearly; a
// non-positive scale draws like scale 1
bool FSOLHologramCursorArrowTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 segments = SOL::Overlay::CURSOR_SEGMENTS;
    TestEqual(TEXT("constant: segments"), segments, 4);
    TestEqual(TEXT("constant: length"), SOL::Overlay::CURSOR_LENGTH_PX, 18.0f, 1.0e-6f);

    const float scales[] = { 1.0f, 2.0f, 1.5f };
    for (const float scale : scales)
    {
        FVector2f start[segments];
        FVector2f end[segments];
        SOLHologramCursor::ArrowSegments(scale, start, end);
        bool bTouchesTip = false;
        float maxCoordinate = 0.0f;
        for (int32 index = 0; index < segments; ++index)
        {
            for (const FVector2f& point : { start[index], end[index] })
            {
                TestTrue(FString::Printf(TEXT("scale %.1f segment %d: x >= 0"), scale, index), point.X >= -1.0e-4f);
                TestTrue(FString::Printf(TEXT("scale %.1f segment %d: y >= 0"), scale, index), point.Y >= -1.0e-4f);
                bTouchesTip |= point.Equals(FVector2f::ZeroVector, 1.0e-4f);
                maxCoordinate = FMath::Max3(maxCoordinate, point.X, point.Y);
            }
        }
        TestTrue(FString::Printf(TEXT("scale %.1f: a segment touches the tip"), scale), bTouchesTip);
        TestEqual(FString::Printf(TEXT("scale %.1f: length"), scale), maxCoordinate,
            SOL::Overlay::CURSOR_LENGTH_PX * scale, 1.0e-3f);
    }

    // Scale 2 doubles every point; zero and negative scales match scale 1
    FVector2f startOne[segments];
    FVector2f endOne[segments];
    FVector2f startTwo[segments];
    FVector2f endTwo[segments];
    FVector2f startZero[segments];
    FVector2f endZero[segments];
    FVector2f startNegative[segments];
    FVector2f endNegative[segments];
    SOLHologramCursor::ArrowSegments(1.0f, startOne, endOne);
    SOLHologramCursor::ArrowSegments(2.0f, startTwo, endTwo);
    SOLHologramCursor::ArrowSegments(0.0f, startZero, endZero);
    SOLHologramCursor::ArrowSegments(-3.0f, startNegative, endNegative);
    for (int32 index = 0; index < segments; ++index)
    {
        TestTrue(FString::Printf(TEXT("segment %d start doubles"), index),
            startTwo[index].Equals(startOne[index] * 2.0f, 1.0e-4f));
        TestTrue(FString::Printf(TEXT("segment %d end doubles"), index),
            endTwo[index].Equals(endOne[index] * 2.0f, 1.0e-4f));
        TestTrue(FString::Printf(TEXT("segment %d: scale 0 like 1"), index),
            startZero[index].Equals(startOne[index], 1.0e-4f) && endZero[index].Equals(endOne[index], 1.0e-4f));
        TestTrue(FString::Printf(TEXT("segment %d: negative scale like 1"), index),
            startNegative[index].Equals(startOne[index], 1.0e-4f)
            && endNegative[index].Equals(endOne[index], 1.0e-4f));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHologramCursorWidgetSideTest, "SOLTest.HologramCursor.WidgetSide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The widget side is twice the arrow length plus padding (the hot spot is its centre); non-positive scales use 1
bool FSOLHologramCursorWidgetSideTest::RunTest(const FString& /*parameters*/)
{
    const float length = SOL::Overlay::CURSOR_LENGTH_PX;
    TestEqual(TEXT("scale 1, pad 4"), SOLHologramCursor::WidgetSidePx(1.0f, 4.0f), 2.0f * (length + 4.0f), 1.0e-4f);
    TestEqual(TEXT("scale 2, pad 0"), SOLHologramCursor::WidgetSidePx(2.0f, 0.0f), 4.0f * length, 1.0e-4f);
    TestEqual(TEXT("scale 1.5, pad 3"), SOLHologramCursor::WidgetSidePx(1.5f, 3.0f),
        2.0f * (length * 1.5f + 3.0f), 1.0e-4f);
    TestEqual(TEXT("scale 0 uses 1"), SOLHologramCursor::WidgetSidePx(0.0f, 3.0f), 2.0f * (length + 3.0f), 1.0e-4f);
    TestEqual(TEXT("negative scale uses 1"), SOLHologramCursor::WidgetSidePx(-2.0f, 3.0f), 2.0f * (length + 3.0f),
        1.0e-4f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
