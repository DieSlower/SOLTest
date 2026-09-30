/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapScreenPick.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the jump map's screen-space body pick (Docs/SDDs/3-jump-map.md, Appendix H): among candidates whose
// screen position is within their pick radius of the cursor, the one nearest the cursor on screen wins. All values
// are pixels. Distances use 3-4-5 style offsets so they are exact in double precision
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Builds a pick candidate from a body index, a screen position and a pick radius
    FSOLScreenPickCandidate MapScreenPickMake(const int32 bodyIndex, const double xPx, const double yPx,
        const double radiusPx)
    {
        FSOLScreenPickCandidate candidate;
        candidate.BodyIndex = bodyIndex;
        candidate.ScreenPositionPx = FVector2D(xPx, yPx);
        candidate.PickRadiusPx = radiusPx;
        return candidate;
    }

    //////////////////////////////////////////////////////////////////////////
    // Calls the code under test with an array of candidates
    int32 MapScreenPickRun(const TArray<FSOLScreenPickCandidate>& candidates, const FVector2D& cursorPx)
    {
        return SOLMapScreenPick::PickNearestOnScreen(candidates, cursorPx);
    }

    // A far-away candidate that never covers the cursors used below; placed first to catch "first element" bugs
    const FSOLScreenPickCandidate MAP_SCREEN_PICK_FAR_DECOY = { 99, FVector2D(5000.0, 5000.0), 10.0 };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapScreenPickInsideTest, "SOLTest.MapScreenPick.SingleInsidePicks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A cursor 5 px from a candidate with a 10 px radius picks it, even when a non-covering candidate precedes it
bool FSOLMapScreenPickInsideTest::RunTest(const FString& /*parameters*/)
{
    const FVector2D cursorPx(103.0, 204.0);
    const FSOLScreenPickCandidate target = MapScreenPickMake(7, 100.0, 200.0, 10.0);
    TestEqual(TEXT("Single candidate 5 px away with a 10 px radius is picked"), MapScreenPickRun({ target }, cursorPx),
        7);
    TestEqual(TEXT("Cursor exactly at the center is picked"), MapScreenPickRun({ target }, FVector2D(100.0, 200.0)), 7);
    TestEqual(TEXT("A non-covering candidate before it does not steal the pick"),
        MapScreenPickRun({ MAP_SCREEN_PICK_FAR_DECOY, target }, cursorPx), 7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapScreenPickOutsideTest, "SOLTest.MapScreenPick.SingleOutsideMisses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A cursor 5 px from a candidate whose radius is under 5 px returns INDEX_NONE
bool FSOLMapScreenPickOutsideTest::RunTest(const FString& /*parameters*/)
{
    const FVector2D cursorPx(103.0, 204.0);
    TestEqual(TEXT("4 px radius, 5 px away misses"), MapScreenPickRun({ MapScreenPickMake(7, 100.0, 200.0, 4.0) },
        cursorPx), INDEX_NONE);
    TestEqual(TEXT("4.999 px radius, 5 px away misses"),
        MapScreenPickRun({ MapScreenPickMake(7, 100.0, 200.0, 4.999) }, cursorPx), INDEX_NONE);
    TestEqual(TEXT("Far away with a large radius still misses"),
        MapScreenPickRun({ MapScreenPickMake(7, 1000.0, 1000.0, 100.0) }, cursorPx), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapScreenPickBoundaryTest, "SOLTest.MapScreenPick.BoundaryIsInclusive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A cursor at exactly PickRadiusPx from the center is inside ("within" read as <=); a hair beyond it misses
bool FSOLMapScreenPickBoundaryTest::RunTest(const FString& /*parameters*/)
{
    // Offset (3, 4) is exactly 5 px, and 5^2 = 25 is exact in double
    const FVector2D cursorPx(103.0, 204.0);
    TestEqual(TEXT("Distance == radius (5 px) is picked"), MapScreenPickRun({ MapScreenPickMake(7, 100.0, 200.0, 5.0) },
        cursorPx), 7);
    TestEqual(TEXT("Radius a hair under the distance misses"),
        MapScreenPickRun({ MapScreenPickMake(7, 100.0, 200.0, 5.0 - 1.0e-9) }, cursorPx), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapScreenPickSunMercuryTest, "SOLTest.MapScreenPick.NearestBeatsLargerRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Regression: a small body near the cursor wins over a big body whose large pick circle also covers the cursor
bool FSOLMapScreenPickSunMercuryTest::RunTest(const FString& /*parameters*/)
{
    // Sun at (640, 360), 200 px radius; Mercury at (700, 360), 12 px radius. Cursor (705, 363):
    // to Mercury sqrt(5^2 + 3^2) = ~5.83 px (< 12), to the Sun sqrt(65^2 + 3^2) = ~65.07 px (< 200)
    const FSOLScreenPickCandidate sun = MapScreenPickMake(0, 640.0, 360.0, 200.0);
    const FSOLScreenPickCandidate mercury = MapScreenPickMake(1, 700.0, 360.0, 12.0);
    const FVector2D cursorPx(705.0, 363.0);

    TestEqual(TEXT("Sun first: Mercury (nearer on screen) wins"), MapScreenPickRun({ sun, mercury }, cursorPx), 1);
    TestEqual(TEXT("Mercury first: Mercury still wins"), MapScreenPickRun({ mercury, sun }, cursorPx), 1);

    // A third, even larger circle that also covers the cursor does not change the winner
    const FSOLScreenPickCandidate giant = MapScreenPickMake(2, 400.0, 360.0, 500.0);
    TestEqual(TEXT("Giant, Sun, Mercury: Mercury wins"), MapScreenPickRun({ giant, sun, mercury }, cursorPx), 1);

    // Sanity: away from Mercury but inside the Sun's circle, the Sun is picked
    TestEqual(TEXT("Cursor outside Mercury's radius picks the Sun"),
        MapScreenPickRun({ mercury, sun }, FVector2D(640.0, 400.0)), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapScreenPickOrderTest, "SOLTest.MapScreenPick.SmallestDistanceWinsAnyOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With several covering candidates the smallest cursor distance wins wherever it sits in the array
bool FSOLMapScreenPickOrderTest::RunTest(const FString& /*parameters*/)
{
    // Cursor at the origin. a: 5 px away, r 50. b: 10 px away, r 50. c: 1 px away, r 2 (smallest radius too, but
    // it wins on distance). d: 0.5 px away but its 0.4 px radius does not reach the cursor, so it never qualifies
    const FVector2D cursorPx(0.0, 0.0);
    const FSOLScreenPickCandidate a = MapScreenPickMake(10, 3.0, 4.0, 50.0);
    const FSOLScreenPickCandidate b = MapScreenPickMake(20, 6.0, 8.0, 50.0);
    const FSOLScreenPickCandidate c = MapScreenPickMake(30, -1.0, 0.0, 2.0);
    const FSOLScreenPickCandidate d = MapScreenPickMake(40, 0.5, 0.0, 0.4);

    TestEqual(TEXT("Winner first"), MapScreenPickRun({ c, a, b }, cursorPx), 30);
    TestEqual(TEXT("Winner last"), MapScreenPickRun({ a, b, c }, cursorPx), 30);
    TestEqual(TEXT("Winner in the middle"), MapScreenPickRun({ b, c, a }, cursorPx), 30);
    TestEqual(TEXT("Non-qualifying nearer candidate first is skipped"), MapScreenPickRun({ d, a, b, c }, cursorPx),
        30);
    TestEqual(TEXT("Non-qualifying nearer candidate last is skipped"), MapScreenPickRun({ b, a, c, d }, cursorPx), 30);

    // Without c, a (5 px) beats b (10 px) in either order
    TestEqual(TEXT("b then a: a wins"), MapScreenPickRun({ b, a }, cursorPx), 10);
    TestEqual(TEXT("a then b: a wins"), MapScreenPickRun({ a, b }, cursorPx), 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapScreenPickNonPositiveTest,
    "SOLTest.MapScreenPick.NonPositiveRadiusNeverQualifies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A candidate with PickRadiusPx 0 or negative is never picked, even with the cursor exactly on its center
bool FSOLMapScreenPickNonPositiveTest::RunTest(const FString& /*parameters*/)
{
    const FVector2D cursorPx(250.0, 125.0);
    const FSOLScreenPickCandidate zero = MapScreenPickMake(5, 250.0, 125.0, 0.0);
    const FSOLScreenPickCandidate negative = MapScreenPickMake(6, 250.0, 125.0, -10.0);
    const FSOLScreenPickCandidate valid = MapScreenPickMake(8, 253.0, 129.0, 10.0);

    TestEqual(TEXT("Radius 0 at distance 0 misses"), MapScreenPickRun({ zero }, cursorPx), INDEX_NONE);
    TestEqual(TEXT("Negative radius at distance 0 misses"), MapScreenPickRun({ negative }, cursorPx), INDEX_NONE);
    TestEqual(TEXT("Zero/negative radius at distance 0 do not beat a valid candidate 5 px away"),
        MapScreenPickRun({ zero, negative, valid }, cursorPx), 8);
    TestEqual(TEXT("Same, valid candidate first"), MapScreenPickRun({ valid, negative, zero }, cursorPx), 8);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapScreenPickEmptyTest, "SOLTest.MapScreenPick.EmptyReturnsNone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No candidates returns INDEX_NONE
bool FSOLMapScreenPickEmptyTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLScreenPickCandidate> empty;
    TestEqual(TEXT("Empty candidates returns INDEX_NONE"), MapScreenPickRun(empty, FVector2D(10.0, 10.0)), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapScreenPickTieTest, "SOLTest.MapScreenPick.TiePicksOneOfTied",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Two candidates exactly equidistant from the cursor: the contract leaves the winner unspecified, so only assert that
// one of the tied candidates is picked (never INDEX_NONE, never the non-covering decoy)
bool FSOLMapScreenPickTieTest::RunTest(const FString& /*parameters*/)
{
    // Offsets (3, 4) and (-4, 3) are both exactly 5 px
    const FVector2D cursorPx(0.0, 0.0);
    const FSOLScreenPickCandidate first = MapScreenPickMake(1, 3.0, 4.0, 10.0);
    const FSOLScreenPickCandidate second = MapScreenPickMake(2, -4.0, 3.0, 10.0);

    const int32 forward = MapScreenPickRun({ MAP_SCREEN_PICK_FAR_DECOY, first, second }, cursorPx);
    TestTrue(TEXT("Forward order picks one of the tied candidates"), forward == 1 || forward == 2);
    const int32 reverse = MapScreenPickRun({ MAP_SCREEN_PICK_FAR_DECOY, second, first }, cursorPx);
    TestTrue(TEXT("Reverse order picks one of the tied candidates"), reverse == 1 || reverse == 2);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
