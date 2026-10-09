/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatRules.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr double COMBATRULES_MAX_STEP_M = 50000.0;     // Independent copy of a plausible teleport threshold
    constexpr double COMBATRULES_TOLERANCE = 1.0e-9;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesSweepStartTest, "SOLTest.CombatRules.SweepStart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Real motion is swept from the previous position; a teleport, missing history or NaN is tested at the end only
bool FSOLCombatRulesSweepStartTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;
    const FVector3d start(100.0, -20.0, 5.0);
    const FVector3d nearEnd(130.0, -20.0, 5.0);
    const FVector3d farEnd(100.0 + 2.0 * COMBATRULES_MAX_STEP_M, -20.0, 5.0);

    TestTrue(TEXT("Short move is sweepable"), IsSweepable(start, nearEnd, COMBATRULES_MAX_STEP_M));
    TestTrue(TEXT("Exactly the threshold is sweepable"),
        IsSweepable(FVector3d::ZeroVector, FVector3d(COMBATRULES_MAX_STEP_M, 0.0, 0.0), COMBATRULES_MAX_STEP_M));
    TestFalse(TEXT("Past the threshold is not"), IsSweepable(start, farEnd, COMBATRULES_MAX_STEP_M));
    TestFalse(TEXT("NaN is not"), IsSweepable(start, FVector3d(NAN, 0.0, 0.0), COMBATRULES_MAX_STEP_M));

    TestTrue(TEXT("Real motion starts at the previous position"),
        ResolveSweepStart(start, nearEnd, true, COMBATRULES_MAX_STEP_M).Equals(start, COMBATRULES_TOLERANCE));
    TestTrue(TEXT("No history: tested at the end"),
        ResolveSweepStart(start, nearEnd, false, COMBATRULES_MAX_STEP_M).Equals(nearEnd, COMBATRULES_TOLERANCE));
    TestTrue(TEXT("Teleport: tested at the end"),
        ResolveSweepStart(start, farEnd, true, COMBATRULES_MAX_STEP_M).Equals(farEnd, COMBATRULES_TOLERANCE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesPreviousPositionTest, "SOLTest.CombatRules.PreviousPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A snapshot entry is a previous position only when fresh, for the same entity and the same rock generation; a ring
// reassignment that moves a rock only a few km (well under the teleport threshold) is still a teleport
bool FSOLCombatRulesPreviousPositionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;
    TestTrue(TEXT("Fresh, same entity, same generation"), HasPreviousPosition(true, true, 7, 7));
    TestFalse(TEXT("Stale snapshot"), HasPreviousPosition(false, true, 7, 7));
    TestFalse(TEXT("Different entity at that index"), HasPreviousPosition(true, false, 7, 7));
    TestFalse(TEXT("Reassigned since the snapshot"), HasPreviousPosition(true, true, 7, 8));
    TestFalse(TEXT("Reassigned across the uint16 wrap"), HasPreviousPosition(true, true, 65535, 0));

    // A reassigned rock 3 km away: short enough to look like motion, but without history it is tested at its end only
    const FVector3d oldRock(0.0, 0.0, 0.0);
    const FVector3d newRock(3000.0, 0.0, 0.0);
    const bool bHasPrevious = HasPreviousPosition(true, true, 1, 2);
    const FVector3d sweepStart = ResolveSweepStart(oldRock, newRock, bHasPrevious, COMBATRULES_MAX_STEP_M);
    TestTrue(TEXT("Reassigned rock is tested where it is now"), sweepStart.Equals(newRock, COMBATRULES_TOLERANCE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesBoltRangeTest, "SOLTest.CombatRules.BoltRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Bolts near the ship stay in range; a bolt left behind (e.g. by a jump) or a NaN one does not
bool FSOLCombatRulesBoltRangeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;
    const double rangeM = 20000.0;
    TestTrue(TEXT("At the ship"), IsBoltInRange(FVector3d::ZeroVector, rangeM));
    TestTrue(TEXT("Full natural flight, 5.25 km"), IsBoltInRange(FVector3d(5250.0, 0.0, 0.0), rangeM));
    TestTrue(TEXT("Exactly at the range"), IsBoltInRange(FVector3d(0.0, -rangeM, 0.0), rangeM));
    TestFalse(TEXT("Just past the range"), IsBoltInRange(FVector3d(0.0, 0.0, rangeM + 1.0), rangeM));
    TestFalse(TEXT("Left behind by a jump (1e9 m)"), IsBoltInRange(FVector3d(1.0e9, 2.0e8, 0.0), rangeM));
    TestFalse(TEXT("NaN"), IsBoltInRange(FVector3d(NAN, 0.0, 0.0), rangeM));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesTargetHitTest, "SOLTest.CombatRules.TargetHit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A hit applies shield-first damage and reports the shield break and the kill; an inactive or dead target takes none
bool FSOLCombatRulesTargetHitTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;
    const SOLDamageMath::FDamageState fresh = SOLDamageMath::MakeState(100.0, 100.0);

    const FTargetHitOutcome first = ResolveTargetHit(true, fresh, 10.0);
    TestTrue(TEXT("Fresh: applied"), first.bApplied);
    TestEqual(TEXT("Fresh: shield takes it"), first.After.Shield, 90.0, COMBATRULES_TOLERANCE);
    TestEqual(TEXT("Fresh: health untouched"), first.After.Health, 100.0, COMBATRULES_TOLERANCE);
    TestFalse(TEXT("Fresh: no break"), first.bShieldBroken);
    TestFalse(TEXT("Fresh: no kill"), first.bDestroyed);

    // The hit that empties the shield reports the break exactly once
    const FTargetHitOutcome breaking = ResolveTargetHit(true, SOLDamageMath::MakeState(10.0, 100.0), 10.0);
    TestTrue(TEXT("Shield break reported"), breaking.bShieldBroken);
    const FTargetHitOutcome afterBreak = ResolveTargetHit(true, breaking.After, 10.0);
    TestFalse(TEXT("No second break once the shield is down"), afterBreak.bShieldBroken);
    TestEqual(TEXT("Hull takes it after the break"), afterBreak.After.Health, 90.0, COMBATRULES_TOLERANCE);

    const FTargetHitOutcome inactive = ResolveTargetHit(false, fresh, 10.0);
    TestFalse(TEXT("Inactive slot: not applied"), inactive.bApplied);
    TestEqual(TEXT("Inactive slot: state unchanged"), inactive.After.Shield, 100.0, COMBATRULES_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesDoubleKillTest, "SOLTest.CombatRules.DoubleKill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Bolts applied in order against one nearly dead target: the first kills it, and the next ones fly on (no second kill),
// whether the slot was already deactivated or is still flagged active with a dead state
bool FSOLCombatRulesDoubleKillTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;
    SOLDamageMath::FDamageState state = SOLDamageMath::MakeState(0.0, 10.0);
    bool bActive = true;
    int32 kills = 0;
    int32 applied = 0;
    for (int32 bolt = 0; bolt < 3; ++bolt)
    {
        const FTargetHitOutcome outcome = ResolveTargetHit(bActive, state, 10.0);
        if (outcome.bApplied)
        {
            ++applied;
            state = outcome.After;
        }
        if (outcome.bDestroyed)
        {
            ++kills;
            bActive = false;
        }
    }
    TestEqual(TEXT("Exactly one kill"), kills, 1);
    TestEqual(TEXT("Only the killing bolt applied"), applied, 1);

    // Same guard on the damage state alone (a dead state still flagged active)
    const FTargetHitOutcome deadButActive = ResolveTargetHit(true, state, 10.0);
    TestTrue(TEXT("Setup: state is dead"), state.bDead);
    TestFalse(TEXT("Dead state: not applied"), deadButActive.bApplied);
    TestFalse(TEXT("Dead state: no second kill"), deadButActive.bDestroyed);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesEventQueueTest, "SOLTest.CombatRules.EventQueue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An update drops only the events already broadcast (keeping ones queued since), and the cap admits up to its size
bool FSOLCombatRulesEventQueueTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;
    TestEqual(TEXT("All broadcast, none since"), EventsToTrim(5, 5), 5);
    TestEqual(TEXT("Two queued between updates are kept"), EventsToTrim(5, 7), 5);
    TestEqual(TEXT("Nothing broadcast yet"), EventsToTrim(0, 3), 0);
    TestEqual(TEXT("Never more than are queued"), EventsToTrim(9, 4), 4);
    TestEqual(TEXT("Never negative"), EventsToTrim(-3, 4), 0);
    TestEqual(TEXT("Empty queue"), EventsToTrim(5, 0), 0);

    TestTrue(TEXT("Below the cap"), MayQueueEvent(4095, 4096));
    TestFalse(TEXT("At the cap"), MayQueueEvent(4096, 4096));
    TestFalse(TEXT("Zero cap"), MayQueueEvent(0, 0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesEventPositionTest, "SOLTest.CombatRules.EventPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An event follows its frame body wherever the body is now (so a warp-speed carry keeps it on its target), plus its own
// motion in that frame for its real-time age
bool FSOLCombatRulesEventPositionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;
    const FVector3d offsetM(200.0, 0.0, 0.0);

    // Body moved 30 km/s x 1000x warp x 0.5 s = 15,000 km while the effect aged 0.5 s: the effect moved with it
    const FVector3d bodyAtEventM(1.0e11, 0.0, 0.0);
    const FVector3d bodyNowM = bodyAtEventM + FVector3d(0.0, 1.5e7, 0.0);
    const FVector3d nowM = EventPositionNowM(bodyNowM, offsetM, FVector3d::ZeroVector, 0.5);
    TestTrue(TEXT("At rest in its frame: body now plus offset"), nowM.Equals(bodyNowM + offsetM, 1.0e-3));

    // Its own relative motion integrates in real seconds
    const FVector3d relativeVelocityMps(0.0, 0.0, 4.0);
    TestTrue(TEXT("Relative motion for its age"),
        EventPositionNowM(bodyNowM, offsetM, relativeVelocityMps, 2.0).Equals(bodyNowM + offsetM
            + FVector3d(0.0, 0.0, 8.0), 1.0e-3));
    TestTrue(TEXT("Negative age is treated as zero"),
        EventPositionNowM(bodyNowM, offsetM, relativeVelocityMps, -1.0).Equals(bodyNowM + offsetM, 1.0e-3));

    // No body (INDEX_NONE frame reads as the origin): a fixed universe position
    TestTrue(TEXT("No frame body: the offset is the position"),
        EventPositionNowM(FVector3d::ZeroVector, offsetM, FVector3d::ZeroVector, 3.0).Equals(offsetM, 1.0e-9));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesHitPointTest, "SOLTest.CombatRules.HitPoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A hit is placed on the struck object's surface where it is at the END of the frame, however far the sweep origin
// (the ship, riding its reference frame) moved during the frame
bool FSOLCombatRulesHitPointTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;

    // Ship-centred frame: target at rest 200 m ahead (radius 10 m); the bolt crosses x = 190 m halfway through
    const FVector3d targetM(200.0, 0.0, 0.0);
    const FVector3d boltStartM(180.0, 0.0, 0.0);
    const FVector3d boltEndM(200.0, 0.0, 0.0);
    const double t = 0.5;

    // The ship (and with it the target) moved 120 m in universe meters this frame (Earth's 30 km/s for 4 ms)
    const FVector3d originEndM(1.0e11 + 120.0, 0.0, 0.0);
    const FVector3d hitM = HitPointAtFrameEndM(boltStartM, boltEndM, targetM, targetM, t, originEndM);
    const FVector3d targetEndM = targetM + originEndM;
    TestEqual(TEXT("On the target's surface at frame end"), FVector3d::Dist(hitM, targetEndM), 10.0, 1.0e-6);
    TestTrue(TEXT("On the near side"), hitM.X < targetEndM.X);

    // A moving object: the contact offset is taken at the hit time, then carried to where the object ends
    const FVector3d rockStartM(190.0, -40.0, 0.0);
    const FVector3d rockEndM(190.0, 40.0, 0.0);
    const FVector3d boltAcrossStartM(190.0, 5.0, -20.0);
    const FVector3d boltAcrossEndM(190.0, 5.0, 20.0);
    const FVector3d rockHitM = HitPointAtFrameEndM(boltAcrossStartM, boltAcrossEndM, rockStartM, rockEndM, 0.5,
        FVector3d::ZeroVector);
    TestTrue(TEXT("Moving object: offset (0, 5, 0) from its end position"),
        rockHitM.Equals(rockEndM + FVector3d(0.0, 5.0, 0.0), 1.0e-9));

    // Out-of-range hit times are clamped to the segment
    TestTrue(TEXT("Hit time clamped to 1"), HitPointAtFrameEndM(boltStartM, boltEndM, targetM, targetM, 3.0,
        FVector3d::ZeroVector).Equals(boltEndM, 1.0e-9));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatRulesDrawnBoltsTest, "SOLTest.CombatRules.DrawnBolts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The bolt renderer draws every live bolt up to its cap and never a negative count
bool FSOLCombatRulesDrawnBoltsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatRules;
    TestEqual(TEXT("None live"), DrawnBoltCount(0, 2048), 0);
    TestEqual(TEXT("Player's ~28"), DrawnBoltCount(28, 2048), 28);
    TestEqual(TEXT("At the cap"), DrawnBoltCount(2048, 2048), 2048);
    TestEqual(TEXT("Stress: capped"), DrawnBoltCount(10000, 2048), 2048);
    TestEqual(TEXT("Negative live count"), DrawnBoltCount(-5, 2048), 0);
    TestEqual(TEXT("Negative cap"), DrawnBoltCount(10, -1), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
