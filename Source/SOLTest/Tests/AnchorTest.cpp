/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLAnchor.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS


// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns two bodies on the X axis: index 0 at the origin, index 1 at x = 1000 m
    TArray<FVector3d> MakeAnchorPair()
    {
        return { FVector3d(0.0, 0.0, 0.0), FVector3d(1000.0, 0.0, 0.0) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a selector already anchored to body 0 of the pair (observer at x = 100)
    FSOLAnchorSelector MakeAnchoredToFirst(FAutomationTestBase& test, const TArray<FVector3d>& bodies)
    {
        FSOLAnchorSelector selector;
        test.TestEqual(TEXT("Setup: first update anchors body 0"), selector.Update(bodies, FVector3d(100.0, 0.0, 0.0)), 0);
        return selector;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAnchorBeforeUpdateTest, "SOLTest.Anchor.IndexNoneBeforeFirstUpdate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// GetAnchorIndex is INDEX_NONE before the first Update and a valid index after it
bool FSOLAnchorBeforeUpdateTest::RunTest(const FString& /*parameters*/)
{
    FSOLAnchorSelector selector;
    TestEqual(TEXT("INDEX_NONE before first update"), selector.GetAnchorIndex(), INDEX_NONE);
    TestEqual(TEXT("SwitchRatio is 0.25"), FSOLAnchorSelector::SwitchRatio, 0.25, 0.0);

    const TArray<FVector3d> bodies = MakeAnchorPair();
    selector.Update(bodies, FVector3d(900.0, 0.0, 0.0));
    TestEqual(TEXT("Valid anchor after first update"), selector.GetAnchorIndex(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAnchorFirstNearestTest, "SOLTest.Anchor.FirstUpdatePicksNearest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The first Update picks the nearest body regardless of hysteresis, in 3D
bool FSOLAnchorFirstNearestTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> bodies =
    {
        FVector3d(0.0, 0.0, 0.0),
        FVector3d(1.0e11, 0.0, 0.0),
        FVector3d(0.0, 2.0e11, 5.0e9),
        FVector3d(-3.0e11, -1.0e11, 0.0),
    };
    const FVector3d observers[] =
    {
        FVector3d(1.0e9, 0.0, 0.0),        // near body 0
        FVector3d(0.9e11, 1.0e9, 0.0),     // near body 1
        FVector3d(0.0, 1.9e11, 5.0e9),     // near body 2
        FVector3d(-2.9e11, -1.0e11, 1e8),  // near body 3
        FVector3d(0.6e11, 0.0, 0.0),       // closer to body 1 than body 0, but not by 4x
    };
    const int32 expected[] = { 0, 1, 2, 3, 1 };
    for (int32 caseIndex = 0; caseIndex < static_cast<int32>(UE_ARRAY_COUNT(observers)); ++caseIndex)
    {
        FSOLAnchorSelector selector;
        const int32 result = selector.Update(bodies, observers[caseIndex]);
        TestEqual(*FString::Printf(TEXT("Case %d: first update picks nearest"), caseIndex), result, expected[caseIndex]);
        TestEqual(*FString::Printf(TEXT("Case %d: GetAnchorIndex matches"), caseIndex), selector.GetAnchorIndex(),
            expected[caseIndex]);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAnchorMidpointTest, "SOLTest.Anchor.NoSwitchAtMidpoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Crossing the midpoint between two bodies does not switch the anchor
bool FSOLAnchorMidpointTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> bodies = MakeAnchorPair();
    FSOLAnchorSelector selector = MakeAnchoredToFirst(*this, bodies);
    TestEqual(TEXT("At the midpoint the anchor stays 0"), selector.Update(bodies, FVector3d(500.0, 0.0, 0.0)), 0);
    TestEqual(TEXT("Past the midpoint (600) the anchor stays 0"), selector.Update(bodies, FVector3d(600.0, 0.0, 0.0)), 0);
    TestEqual(TEXT("Far past the midpoint (790) the anchor stays 0"),
        selector.Update(bodies, FVector3d(790.0, 0.0, 0.0)), 0);
    TestEqual(TEXT("GetAnchorIndex stays 0"), selector.GetAnchorIndex(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAnchorOscillationTest, "SOLTest.Anchor.HysteresisOscillation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An observer oscillating around the midpoint never toggles the anchor, from either side
bool FSOLAnchorOscillationTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> bodies = MakeAnchorPair();

    // Anchored to body 0, oscillate 350..650
    FSOLAnchorSelector selector = MakeAnchoredToFirst(*this, bodies);
    for (int32 step = 0; step < 200; ++step)
    {
        const double x = 500.0 + 150.0 * FMath::Sin(step * 0.37);
        if (!TestEqual(*FString::Printf(TEXT("Anchored to 0, x=%.3f"), x), selector.Update(bodies, FVector3d(x, 0.0, 0.0)), 0))
        {
            break;
        }
    }

    // Anchored to body 1, oscillate the same way
    FSOLAnchorSelector other;
    TestEqual(TEXT("Setup: first update anchors body 1"), other.Update(bodies, FVector3d(950.0, 0.0, 0.0)), 1);
    for (int32 step = 0; step < 200; ++step)
    {
        const double x = 500.0 + 150.0 * FMath::Sin(step * 0.37);
        if (!TestEqual(*FString::Printf(TEXT("Anchored to 1, x=%.3f"), x), other.Update(bodies, FVector3d(x, 0.0, 0.0)), 1))
        {
            break;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAnchorThresholdTest, "SOLTest.Anchor.SwitchThresholdExact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Switch only when Dist(candidate) < 0.25 * Dist(anchor): not at exactly 0.25 or above, yes just below
bool FSOLAnchorThresholdTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> bodies = MakeAnchorPair();

    // x = 800: distances 800 and 200, exactly 0.25 -> no switch
    FSOLAnchorSelector atRatio = MakeAnchoredToFirst(*this, bodies);
    TestEqual(TEXT("Exactly 0.25 does not switch"), atRatio.Update(bodies, FVector3d(800.0, 0.0, 0.0)), 0);

    // x = 799: ratio 201/799 > 0.25 -> no switch; x = 801: ratio 199/801 < 0.25 -> switch
    FSOLAnchorSelector crossing = MakeAnchoredToFirst(*this, bodies);
    TestEqual(TEXT("Ratio above 0.25 does not switch"), crossing.Update(bodies, FVector3d(799.0, 0.0, 0.0)), 0);
    TestEqual(TEXT("Ratio below 0.25 switches"), crossing.Update(bodies, FVector3d(801.0, 0.0, 0.0)), 1);
    TestEqual(TEXT("GetAnchorIndex reflects the switch"), crossing.GetAnchorIndex(), 1);

    // Coming back: the anchor is now body 1 and needs the mirrored condition to switch back
    TestEqual(TEXT("Back at the midpoint the anchor stays 1"), crossing.Update(bodies, FVector3d(500.0, 0.0, 0.0)), 1);
    TestEqual(TEXT("At x=200 (exactly 0.25 mirrored) stays 1"), crossing.Update(bodies, FVector3d(200.0, 0.0, 0.0)), 1);
    TestEqual(TEXT("At x=199 switches back to 0"), crossing.Update(bodies, FVector3d(199.0, 0.0, 0.0)), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAnchorCandidateTest, "SOLTest.Anchor.CandidateIsNearestOther",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With three bodies the switch goes to the nearest non-anchor body, and Update returns the anchor
bool FSOLAnchorCandidateTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> bodies =
    {
        FVector3d(0.0, 0.0, 0.0),
        FVector3d(0.0, 1000.0, 0.0),
        FVector3d(2000.0, 0.0, 0.0),
    };
    FSOLAnchorSelector selector;
    TestEqual(TEXT("First update anchors body 0"), selector.Update(bodies, FVector3d(10.0, 10.0, 0.0)), 0);

    // Observer beside body 2: distance to 0 is 1990, to 2 is 10, to 1 is ~2200
    const int32 result = selector.Update(bodies, FVector3d(1990.0, 0.0, 0.0));
    TestEqual(TEXT("Switches to body 2"), result, 2);
    TestEqual(TEXT("Update returns GetAnchorIndex"), result, selector.GetAnchorIndex());

    // Observer near body 1 while anchored to 2
    TestEqual(TEXT("Switches from 2 to 1"), selector.Update(bodies, FVector3d(0.0, 990.0, 0.0)), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAnchorSolarScaleTest, "SOLTest.Anchor.SolarScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At solar-system scale a ship leaving Earth keeps Earth as anchor until it is 4x closer to the Sun
bool FSOLAnchorSolarScaleTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> bodies = { FVector3d::ZeroVector, FVector3d(SOLTestHelpers::AU_M, 0.0, 0.0) };
    FSOLAnchorSelector selector;
    TestEqual(TEXT("Near Earth: anchor Earth"), selector.Update(bodies, FVector3d(SOLTestHelpers::AU_M + 1.0e7, 0.0, 0.0)), 1);
    TestEqual(TEXT("Halfway to the Sun: still Earth"), selector.Update(bodies, FVector3d(0.5 * SOLTestHelpers::AU_M, 0.0, 0.0)), 1);
    TestEqual(TEXT("0.21 AU from the Sun: still Earth"), selector.Update(bodies, FVector3d(0.21 * SOLTestHelpers::AU_M, 0.0, 0.0)), 1);
    TestEqual(TEXT("0.19 AU from the Sun: switches to the Sun"),
        selector.Update(bodies, FVector3d(0.19 * SOLTestHelpers::AU_M, 0.0, 0.0)), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
