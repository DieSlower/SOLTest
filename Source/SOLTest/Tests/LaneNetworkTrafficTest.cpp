/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <initializer_list>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #53 part 53c2a lane network traffic logic (ship blocks and the block pool, partial uploads, lane
// spawn into blocks, in space or docked, the group texel window row, traffic activation, the merge readout and lane
// search skipping closed lanes, constants), written test-first against the contract
// Docs/Plans/53-lane-network-contract.md section "53c2 additions" (cases 16-24 and 26, decisions P1-P6) before the
// production code existed. Every index into an output array is guarded by its count, so a wrong-sized output fails a
// check instead of stopping the run. Fixtures that chain several functions use hand-built inputs where they can, so
// one missing function does not hide another's result.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double LNT_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double LNT_INF = std::numeric_limits<double>::infinity();
    constexpr double LNT_TIGHT = 1.0e-12;               // Relative tolerance of exact formulas
    constexpr double LNT_DAY_S = 86400.0;
    constexpr int32 LNT_BLOCK = 100;                    // The contract's block size (NPC_LANE_SHIP_BLOCK_SIZE)

    //////////////////////////////////////////////////////////////////////////
    // Formats an int array as {a, b, c} for error messages (at most 40 entries)
    FString LntFormatInts(TConstArrayView<int32> values)
    {
        FString text = TEXT("{");
        for (int32 i = 0; i < values.Num() && i < 40; ++i)
        {
            text += FString::Printf(TEXT("%s%d"), i > 0 ? TEXT(", ") : TEXT(""), values[i]);
        }
        text += values.Num() > 40 ? TEXT(", ...}") : TEXT("}");
        return FString::Printf(TEXT("%s (%d entries)"), *text, values.Num());
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks an int array against the expected one, entry for entry
    bool LntIntsView(FAutomationTestBase& test, const FString& what, TConstArrayView<int32> actual,
        TConstArrayView<int32> expected)
    {
        bool bSame = actual.Num() == expected.Num();
        for (int32 i = 0; bSame && i < actual.Num(); ++i)
        {
            bSame = actual[i] == expected[i];
        }
        if (!bSame)
        {
            test.AddError(FString::Printf(TEXT("%s: expected %s, got %s"), *what, *LntFormatInts(expected),
                *LntFormatInts(actual)));
        }
        return bSame;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks an int array against a braced list of expected values
    bool LntInts(FAutomationTestBase& test, const FString& what, TConstArrayView<int32> actual,
        std::initializer_list<int32> expected)
    {
        const TArray<int32> expectedArray(expected);
        return LntIntsView(test, what, actual, expectedArray);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an enum's underlying value as int32 (for TestEqual)
    template <typename EnumType>
    int32 LntInt(const EnumType value)
    {
        return static_cast<int32>(value);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a bool against the expected value
    bool LntBool(FAutomationTestBase& test, const FString& what, const bool actual, const bool expected)
    {
        if (actual == expected)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected %s, got %s"), *what, expected ? TEXT("true") : TEXT("false"),
            actual ? TEXT("true") : TEXT("false")));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks actual against expected within a relative tolerance (absolute when expected is 0); NaN never matches
    bool LntNear(FAutomationTestBase& test, const FString& what, const double actual, const double expected,
        const double relTol)
    {
        const double tol = (expected == 0.0) ? relTol : relTol * FMath::Abs(expected);
        if (FMath::Abs(actual - expected) <= tol)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected %.15g, got %.15g"), *what, expected, actual));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a float exactly (infinities and float-rounded window bounds)
    bool LntFloatExact(FAutomationTestBase& test, const FString& what, const float actual, const float expected)
    {
        if (actual == expected)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected exactly %.9g, got %.9g"), *what, expected, actual));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a float4 texel exactly
    bool LntTexel(FAutomationTestBase& test, const FString& what, const FVector4f& actual, const FVector4f& expected)
    {
        if (actual == expected)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected (%.9g, %.9g, %.9g, %.9g), got (%.9g, %.9g, %.9g, %.9g)"),
            *what, expected.X, expected.Y, expected.Z, expected.W, actual.X, actual.Y, actual.Z, actual.W));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the 8 base lane weights of case 12 (2, sqrt 20, 10, sqrt 60, sqrt 12, 2, sqrt 2, 1)
    TArray<double> LntBaseWeights()
    {
        return {2.0, FMath::Sqrt(20.0), 10.0, FMath::Sqrt(60.0), FMath::Sqrt(12.0), 2.0, FMath::Sqrt(2.0), 1.0};
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the block map AssignShipBlocks must give, built by hand from LaneShipShares (contiguous, ascending slots)
    TArray<int32> LntContiguousBlockMap(const int32 blockCount, TConstArrayView<double> weights)
    {
        TArray<int32> targets;
        SOLLaneNetwork::LaneShipShares(blockCount, weights, targets);
        TArray<int32> map;
        for (int32 slot = 0; slot < targets.Num(); ++slot)
        {
            for (int32 i = 0; i < targets[slot]; ++i)
            {
                map.Add(slot);
            }
        }
        // Pad with free blocks if the shares did not cover every block (no positive weight)
        while (map.Num() < blockCount)
        {
            map.Add(INDEX_NONE);
        }
        return map;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's direct cycle {LegS 30, TurnS 5, CycleS 70}
    FSOLLaneCycle LntCycle()
    {
        FSOLLaneCycle cycle;
        cycle.LegS = 30.0;
        cycle.TurnS = 5.0;
        cycle.CycleS = 70.0;
        return cycle;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the P1 model of LaneShipInSpace for the {30, 5, 70} cycle at phase 0: the governing leg is the current
    // leg, or the leg after a U-turn; drawn iff that leg departs at or after the open and arrives by the close
    bool LntExpectedInSpace(const double t, const double openS, const double closeS)
    {
        const double k = FMath::FloorToDouble(t / 70.0);
        const double tau = t - 70.0 * k;
        double departS = 70.0 * k;                      // Outbound [0, 30)
        if (tau >= 30.0 && tau < 65.0)
        {
            departS = 70.0 * k + 35.0;                  // TurnAtB [30, 35) and Inbound [35, 65): the inbound leg
        }
        else if (tau >= 65.0)
        {
            departS = 70.0 * (k + 1.0);                 // TurnAtA [65, 70): the next outbound leg
        }
        return departS >= openS && departS + 30.0 <= closeS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's battle parameters for the spawn cases (any fixed values; battle ships are compared only
    // against the legacy MakeSpawn)
    SOLNpcScale::FBattleParams LntBattle()
    {
        SOLNpcScale::FBattleParams battle;
        battle.BattleCount = 4;
        battle.RadiusM = 3000.0;
        battle.MinDistanceM = 5000.0;
        battle.MaxDistanceM = 20000.0;
        return battle;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two spawns are equal field for field
    bool LntSpawnsEqual(const SOLNpcScale::FSpawn& a, const SOLNpcScale::FSpawn& b)
    {
        return a.Kind == b.Kind && a.GroupIndex == b.GroupIndex && a.Faction == b.Faction
            && a.LanePhase == b.LanePhase && a.LaneEffort == b.LaneEffort && a.LaneRho == b.LaneRho
            && a.LaneClockRad == b.LaneClockRad && a.bLaneHalo == b.bLaneHalo && a.OrbitRadiusM == b.OrbitRadiusM
            && a.OrbitInclinationRad == b.OrbitInclinationRad && a.OrbitNodeRad == b.OrbitNodeRad
            && a.OrbitPhaseRad == b.OrbitPhaseRad && a.BattleSlotOffsetM == b.BattleSlotOffsetM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two lane spawns agree on every lane field except the group (the hash streams)
    bool LntLaneFieldsEqual(const SOLNpcScale::FSpawn& a, const SOLNpcScale::FSpawn& b)
    {
        return a.Kind == b.Kind && a.Faction == b.Faction && a.LanePhase == b.LanePhase
            && a.LaneEffort == b.LaneEffort && a.LaneRho == b.LaneRho && a.LaneClockRad == b.LaneClockRad
            && a.bLaneHalo == b.bLaneHalo;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two lane profiles are equal field for field
    bool LntProfilesEqual(const FSOLLaneProfile& a, const FSOLLaneProfile& b)
    {
        return a.WallRadiusM == b.WallRadiusM && a.HaloOuterRadiusM == b.HaloOuterRadiusM
            && a.NominalLengthM == b.NominalLengthM && a.NominalLegM == b.NominalLegM && a.EndArcAM == b.EndArcAM
            && a.EndArcBM == b.EndArcBM && a.UTurnDepthM == b.UTurnDepthM && a.UTurnS == b.UTurnS
            && a.MetricZoneM == b.MetricZoneM && a.bDrivable == b.bDrivable;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the group table of the column cases: 8 lanes, 10 clusters, 16 battles
    FSOLNpcGroupTable LntGroups()
    {
        FSOLNpcGroupTable groups;
        groups.LaneCount = 8;
        groups.ClusterCount = 10;
        groups.BattleCount = 16;
        return groups;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns case 24's lane: MakeMergeLane(MakeLaneProfile(6e6, 4e11, 6.371e6, 3.3895e6), 0, 1, A, B, 0, 0)
    FSOLLaneMergeLane LntMergeLane()
    {
        const FSOLLaneProfile profile = SOLLaneTraffic::MakeLaneProfile(6.0e6, 4.0e11, 6.371e6, 3.3895e6);
        return SOLLaneTraffic::MakeMergeLane(profile, 0, 1, FVector3d(1.0e11, 2.0e10, 0.0),
            FVector3d(5.0e11, 2.0e10, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns case 24's two identical lanes with their open flags
    TArray<FSOLLaneMergeLane> LntMergeLanes(const bool bOpen0, const bool bOpen1)
    {
        TArray<FSOLLaneMergeLane> lanes = {LntMergeLane(), LntMergeLane()};
        lanes[0].bOpenNow = bOpen0;
        lanes[1].bOpenNow = bOpen1;
        return lanes;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a case-24 AUTO input: ship and target as given, at rest, a positive brake plan
    FSOLLaneAutoInput LntAutoInput(const FVector3d& shipM, const FVector3d& targetM, const bool bSearch)
    {
        FSOLLaneAutoInput input;
        input.ShipM = shipM;
        input.TargetM = targetM;
        input.BrakeAccelMps2 = 20.0;                    // Not pinned by the contract; any positive brake
        input.FlipS = SOL::NPC_LANE_FLIP_S;
        input.bSearch = bSearch;
        input.bIgnoreLanes = false;
        return input;
    }
}

//////////////////////////////////////////////////////////////////////////
// Case 16 (ShipBlocks, P3): block count = ceil(count / size), the last block partial; block b holds the lane-local
// ships [100 b, 100 b + 100); out of range gives 0 or INDEX_NONE
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficShipBlocksTest, "SOLTest.LaneNetworkTraffic.ShipBlocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficShipBlocksTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneNetwork;

    // ShipBlockCount
    TestEqual(TEXT("count (400000, 100)"), ShipBlockCount(400000, 100), 4000);
    TestEqual(TEXT("count (400000, 100) == default"), ShipBlockCount(400000, 100),
        SOL::NPC_LANE_SHIP_BLOCK_COUNT_DEFAULT);
    TestEqual(TEXT("count (400001, 100)"), ShipBlockCount(400001, 100), 4001);
    TestEqual(TEXT("count (400050, 100)"), ShipBlockCount(400050, 100), 4001);
    TestEqual(TEXT("count (99, 100)"), ShipBlockCount(99, 100), 1);
    TestEqual(TEXT("count (0, 100)"), ShipBlockCount(0, 100), 0);
    TestEqual(TEXT("count (-5, 100)"), ShipBlockCount(-5, 100), 0);
    TestEqual(TEXT("count (100, 0)"), ShipBlockCount(100, 0), 0);

    // ShipBlockSize
    TestEqual(TEXT("size (3999, 400000)"), ShipBlockSize(3999, 400000, 100), 100);
    TestEqual(TEXT("size (4000, 400050)"), ShipBlockSize(4000, 400050, 100), 50);
    TestEqual(TEXT("size (4000, 400001)"), ShipBlockSize(4000, 400001, 100), 1);
    TestEqual(TEXT("size (0, 99)"), ShipBlockSize(0, 99, 100), 99);
    TestEqual(TEXT("size (0, 3200)"), ShipBlockSize(0, 3200, 100), 100);
    TestEqual(TEXT("size (31, 3200)"), ShipBlockSize(31, 3200, 100), 100);
    TestEqual(TEXT("size (4000, 400000) out of range"), ShipBlockSize(4000, 400000, 100), 0);
    TestEqual(TEXT("size (4001, 400050) out of range"), ShipBlockSize(4001, 400050, 100), 0);
    TestEqual(TEXT("size (-1, 400000)"), ShipBlockSize(-1, 400000, 100), 0);

    // The block sizes sum to the lane ship count (blocks counted by the literal ceil, so a wrong count still shows)
    int64 sum400050 = 0;
    for (int32 block = 0; block < 4001; ++block)
    {
        sum400050 += ShipBlockSize(block, 400050, 100);
    }
    TestEqual(TEXT("sizes of (400050, 100) sum to 400050"), sum400050, static_cast<int64>(400050));
    TestEqual(TEXT("sizes of (99, 100) sum to 99"), ShipBlockSize(0, 99, 100), 99);

    // ShipBlockOfLaneShip
    TestEqual(TEXT("block of 12345"), ShipBlockOfLaneShip(12345, 400000, 100), 123);
    TestEqual(TEXT("block of 0"), ShipBlockOfLaneShip(0, 400000, 100), 0);
    TestEqual(TEXT("block of 99"), ShipBlockOfLaneShip(99, 400000, 100), 0);
    TestEqual(TEXT("block of 100"), ShipBlockOfLaneShip(100, 400000, 100), 1);
    TestEqual(TEXT("block of 399999"), ShipBlockOfLaneShip(399999, 400000, 100), 3999);
    TestEqual(TEXT("block of 400049 (partial block)"), ShipBlockOfLaneShip(400049, 400050, 100), 4000);
    TestEqual(TEXT("block of 400050 (past the count)"), ShipBlockOfLaneShip(400050, 400050, 100),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("block of -1"), ShipBlockOfLaneShip(-1, 400000, 100), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("block with size 0"), ShipBlockOfLaneShip(5, 400000, 0), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("block with size -100"), ShipBlockOfLaneShip(5, 400000, -100), static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 17 (BlockPool, O1, R14, P4): exact spawn and time-jump assignment, an opening lane's targets, release, take in
// the given order (ascending from EpisodeTakeOrder), the release rule and the episode-open rule
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficBlockPoolTest, "SOLTest.LaneNetworkTraffic.BlockPool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficBlockPoolTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneNetwork;
    const TArray<double> base = LntBaseWeights();

    // AssignShipBlocks (spawn, exact)
    TArray<int32> map;
    AssignShipBlocks(10, TArray<double>({1.0, 1.0, 1.0}), map);
    LntInts(*this, TEXT("assign 10 over {1, 1, 1}"), map, {0, 0, 0, 0, 1, 1, 1, 2, 2, 2});
    AssignShipBlocks(6, TArray<double>({1.0, 0.0, 2.0}), map);
    LntInts(*this, TEXT("assign 6 over {1, 0, 2}"), map, {0, 0, 2, 2, 2, 2});
    AssignShipBlocks(3, TArray<double>({0.0, 0.0}), map);
    LntInts(*this, TEXT("assign 3 over {0, 0}: all free"), map, {-1, -1, -1});
    map = {7, 7};
    AssignShipBlocks(0, TArray<double>({1.0}), map);
    LntInts(*this, TEXT("assign 0 blocks: empty"), map, {});
    map = {7, 7};
    AssignShipBlocks(-3, TArray<double>({1.0}), map);
    LntInts(*this, TEXT("assign -3 blocks: empty"), map, {});
    AssignShipBlocks(4, TArray<double>({1.0, 1.0}), map);
    LntInts(*this, TEXT("assign 4 over {1, 1}"), map, {0, 0, 1, 1});

    // 4,000 over the base weights: slot s holds [start_s, start_s+1)
    const int32 starts[9] = {0, 249, 807, 2053, 3018, 3450, 3699, 3875, 4000};
    TArray<int32> expected4000;
    for (int32 slot = 0; slot < 8; ++slot)
    {
        for (int32 block = starts[slot]; block < starts[slot + 1]; ++block)
        {
            expected4000.Add(slot);
        }
    }
    AssignShipBlocks(4000, base, map);
    LntIntsView(*this, TEXT("assign 4000 over the base weights"), map, expected4000);

    // Time jump (P4 d): a full exact re-share, whatever the old map held
    map = {2, 2, 2, -1, 0, 1, 1, 1, 1, 1, 7, 7};
    AssignShipBlocks(10, TArray<double>({1.0, 1.0, 1.0}), map);
    LntInts(*this, TEXT("time jump re-share equals a fresh call"), map, {0, 0, 0, 0, 1, 1, 1, 2, 2, 2});

    // An opening lane's targets (P4 a) are LaneShipShares over the slotted weights
    TArray<int32> targets;
    LaneShipShares(4000, base, targets);
    LntInts(*this, TEXT("targets of 4000 over the base weights"), targets, {249, 558, 1246, 965, 432, 249, 176, 125});
    LaneShipShares(10, TArray<double>({1.0, 1.0, 1.0}), targets);
    LntInts(*this, TEXT("targets of 10 over {1, 1, 1}"), targets, {4, 3, 3});

    // ReleaseShipBlocks
    map = {0, 0, 3, 1, 3, 4, 2, 4};
    TestEqual(TEXT("release returns 3"), ReleaseShipBlocks(TArray<uint8>({0, 1, 0, 0, 1}), map), 3);
    LntInts(*this, TEXT("release slots 1 and 4"), map, {0, 0, 3, -1, 3, -1, 2, -1});
    map = {5, 0};
    TestEqual(TEXT("release: a slot past the flags is kept"), ReleaseShipBlocks(TArray<uint8>({1}), map), 1);
    LntInts(*this, TEXT("release: slot 5 kept, slot 0 freed"), map, {5, -1});

    // TakeShipBlocks, ascending order {3, 4} (the pinned P4 b outcome)
    TArray<int32> moved = {99};
    map = {0, 0, -1, 1, -1, -1, 2, -1};
    TestEqual(TEXT("take {3, 4}: slot 4 short by one"),
        TakeShipBlocks(TArray<int32>({3, 4}), TArray<int32>({0, 0, 0, 2, 3}), map, moved), 1);
    LntInts(*this, TEXT("take {3, 4}: map"), map, {0, 0, 3, 1, 3, 4, 2, 4});
    LntInts(*this, TEXT("take {3, 4}: moved"), moved, {2, 4, 5, 7});

    // The function follows the order it is given
    map = {0, 0, -1, 1, -1, -1, 2, -1};
    TestEqual(TEXT("take {4, 3}: slot 3 short by one"),
        TakeShipBlocks(TArray<int32>({4, 3}), TArray<int32>({0, 0, 0, 2, 3}), map, moved), 1);
    LntInts(*this, TEXT("take {4, 3}: map"), map, {0, 0, 4, 1, 4, 4, 2, 3});
    LntInts(*this, TEXT("take {4, 3}: moved"), moved, {2, 4, 5, 7});

    // No free block: nothing moves, the shortfall is the targets' sum (the slots hold nothing)
    map = {5, 5, 5};
    moved = {99};
    TestEqual(TEXT("take with no free block returns the targets' sum"),
        TakeShipBlocks(TArray<int32>({0, 1}), TArray<int32>({2, 3}), map, moved), 5);
    LntInts(*this, TEXT("no free block: map unchanged"), map, {5, 5, 5});
    LntInts(*this, TEXT("no free block: nothing moved"), moved, {});

    // A target at or below what the slot holds takes nothing
    map = {0, 0, -1};
    TestEqual(TEXT("target equal to holding"), TakeShipBlocks(TArray<int32>({0}), TArray<int32>({2}), map, moved), 0);
    LntInts(*this, TEXT("target equal: map unchanged"), map, {0, 0, -1});
    LntInts(*this, TEXT("target equal: nothing moved"), moved, {});
    TestEqual(TEXT("target below holding"), TakeShipBlocks(TArray<int32>({0}), TArray<int32>({1}), map, moved), 0);
    LntInts(*this, TEXT("target below: map unchanged (no release)"), map, {0, 0, -1});

    // A slot out of range of the targets is skipped
    map = {-1, -1};
    TestEqual(TEXT("slot past the targets skipped"),
        TakeShipBlocks(TArray<int32>({7}), TArray<int32>({1}), map, moved), 0);
    LntInts(*this, TEXT("slot past the targets: map unchanged"), map, {-1, -1});

    // Determinism
    TArray<int32> mapA = {0, 0, -1, 1, -1, -1, 2, -1};
    TArray<int32> mapB = mapA;
    TArray<int32> movedA;
    TArray<int32> movedB;
    const int32 shortA = TakeShipBlocks(TArray<int32>({3, 4}), TArray<int32>({0, 0, 0, 2, 3}), mapA, movedA);
    const int32 shortB = TakeShipBlocks(TArray<int32>({3, 4}), TArray<int32>({0, 0, 0, 2, 3}), mapB, movedB);
    TestEqual(TEXT("determinism: shortfall"), shortA, shortB);
    LntIntsView(*this, TEXT("determinism: map"), mapA, mapB);
    LntIntsView(*this, TEXT("determinism: moved"), movedA, movedB);

    // CanReleaseSlotBlocks
    TestTrue(TEXT("release (false, 0)"), CanReleaseSlotBlocks(false, 0));
    TestFalse(TEXT("release (false, 2)"), CanReleaseSlotBlocks(false, 2));
    TestFalse(TEXT("release (true, 0)"), CanReleaseSlotBlocks(true, 0));
    TestFalse(TEXT("release (true, 2)"), CanReleaseSlotBlocks(true, 2));

    // LaneEpisodeOpens
    const FSOLLanePair lane25{2, 5};
    const FSOLLanePair lane46{4, 6};
    const FSOLLanePair noLane{INDEX_NONE, INDEX_NONE};
    TestFalse(TEXT("episode: same lane, stayed in the set"), LaneEpisodeOpens(lane25, true, lane25, true));
    TestTrue(TEXT("episode: back after leaving"), LaneEpisodeOpens(lane25, false, lane25, true));
    TestTrue(TEXT("episode: a free slot gets a lane"), LaneEpisodeOpens(noLane, false, lane25, true));
    TestTrue(TEXT("episode: another lane in the slot"), LaneEpisodeOpens(lane25, true, lane46, true));
    TestFalse(TEXT("episode: not in the set now (1)"), LaneEpisodeOpens(lane25, true, lane25, false));
    TestFalse(TEXT("episode: not in the set now (2)"), LaneEpisodeOpens(noLane, false, lane25, false));
    TestFalse(TEXT("episode: not in the set now (3)"), LaneEpisodeOpens(lane46, true, lane25, false));

    // EpisodeTakeOrder
    TArray<int32> order = {99};
    EpisodeTakeOrder(TArray<uint8>({0, 1, 0, 1, 1}), order);
    LntInts(*this, TEXT("order {0, 1, 0, 1, 1}"), order, {1, 3, 4});
    EpisodeTakeOrder(TArray<uint8>({1, 0, 0}), order);
    LntInts(*this, TEXT("order {1, 0, 0}"), order, {0});
    order = {99};
    EpisodeTakeOrder(TArray<uint8>({0, 0, 0}), order);
    LntInts(*this, TEXT("order all 0"), order, {});
    order = {99};
    EpisodeTakeOrder(TArray<uint8>(), order);
    LntInts(*this, TEXT("order of no slots"), order, {});
    EpisodeTakeOrder(TArray<uint8>({0, 2, 255}), order);
    LntInts(*this, TEXT("order: 2 and 255 count as opening"), order, {1, 2});

    // Chained: the take example's slots 3 and 4 open together
    EpisodeTakeOrder(TArray<uint8>({0, 0, 0, 1, 1}), order);
    LntInts(*this, TEXT("chained order"), order, {3, 4});
    map = {0, 0, -1, 1, -1, -1, 2, -1};
    TestEqual(TEXT("chained take shortfall"), TakeShipBlocks(order, TArray<int32>({0, 0, 0, 2, 3}), map, moved), 1);
    LntInts(*this, TEXT("chained take map"), map, {0, 0, 3, 1, 3, 4, 2, 4});

    // A short slot that does not open takes nothing (no top-up mid-episode, P4 c)
    map = {0, -1, -1};
    EpisodeTakeOrder(TArray<uint8>({0}), order);
    LntInts(*this, TEXT("short slot not opening: order"), order, {});
    TestEqual(TEXT("short slot not opening: shortfall"),
        TakeShipBlocks(order, TArray<int32>({3}), map, moved), 0);
    LntInts(*this, TEXT("short slot not opening: map"), map, {0, -1, -1});
    LntInts(*this, TEXT("short slot not opening: moved"), moved, {});
    EpisodeTakeOrder(TArray<uint8>({1}), order);
    LntInts(*this, TEXT("short slot opening: order"), order, {0});
    TestEqual(TEXT("short slot opening: shortfall"), TakeShipBlocks(order, TArray<int32>({3}), map, moved), 0);
    LntInts(*this, TEXT("short slot opening: map"), map, {0, 0, 0});
    LntInts(*this, TEXT("short slot opening: moved"), moved, {1, 2});
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 18 (BlockLists): per-slot CSR block lists and per-slot sums of a per-block value
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficBlockListsTest, "SOLTest.LaneNetworkTraffic.BlockLists",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficBlockListsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneNetwork;
    const TArray<int32> map = {0, 0, 3, 1, 3, 4, 2, 4};

    TArray<int32> slotStart = {99};
    TArray<int32> blocks = {99};
    SlotBlockLists(map, 5, slotStart, blocks);
    LntInts(*this, TEXT("CSR starts"), slotStart, {0, 2, 3, 4, 6, 8});
    LntInts(*this, TEXT("CSR blocks"), blocks, {0, 1, 3, 6, 2, 4, 5, 7});
    SlotBlockLists(map, 0, slotStart, blocks);
    LntInts(*this, TEXT("CSR slotCount 0: starts"), slotStart, {0});
    LntInts(*this, TEXT("CSR slotCount 0: blocks"), blocks, {});
    SlotBlockLists(map, 3, slotStart, blocks);
    LntInts(*this, TEXT("CSR slots >= slotCount not listed: starts"), slotStart, {0, 2, 3, 4});
    LntInts(*this, TEXT("CSR slots >= slotCount not listed: blocks"), blocks, {0, 1, 3, 6});
    SlotBlockLists(TArray<int32>({-1, 0, -1, 1}), 2, slotStart, blocks);
    LntInts(*this, TEXT("CSR free blocks not listed: starts"), slotStart, {0, 1, 2});
    LntInts(*this, TEXT("CSR free blocks not listed: blocks"), blocks, {1, 3});

    TArray<int32> perSlot = {99};
    const TArray<int32> sizes = {100, 100, 100, 100, 100, 100, 100, 50};
    SumBlockCounts(map, sizes, 5, perSlot);
    LntInts(*this, TEXT("per-slot sums"), perSlot, {200, 100, 100, 200, 150});
    int32 total = 0;
    for (const int32 value : perSlot)
    {
        total += value;
    }
    TestEqual(TEXT("per-slot sums total the lane ships"), total, 750);
    SumBlockCounts(map, TArray<int32>({100, 100, 100, 100, 100, 100, 100}), 5, perSlot);
    LntInts(*this, TEXT("a block past perBlock adds 0"), perSlot, {200, 100, 100, 200, 100});
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 19 (DirtyRanges): moved blocks to sorted, merged lane-local ship ranges (P3 contiguous membership)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficDirtyRangesTest, "SOLTest.LaneNetworkTraffic.DirtyRanges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficDirtyRangesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneNetwork;

    // Checks ranges against expected (first, count) pairs
    auto checkRanges = [this](const TCHAR* what, const TArray<FSOLShipRange>& actual,
        std::initializer_list<FIntPoint> expected)
    {
        const TArray<FIntPoint> expectedArray(expected);
        bool bSame = actual.Num() == expectedArray.Num();
        for (int32 i = 0; bSame && i < actual.Num(); ++i)
        {
            bSame = actual[i].First == expectedArray[i].X && actual[i].Count == expectedArray[i].Y;
        }
        FString got;
        for (const FSOLShipRange& range : actual)
        {
            got += FString::Printf(TEXT("(%d, %d) "), range.First, range.Count);
        }
        TestTrue(FString::Printf(TEXT("%s (got %s)"), what, *got), bSame);
    };

    TArray<FSOLShipRange> ranges = {FSOLShipRange{1, 1}};
    ShipBlockDirtyRanges(TArray<int32>({7, 2, 3, 3, 5}), 750, LNT_BLOCK, ranges);
    checkRanges(TEXT("{7, 2, 3, 3, 5} over 750"), ranges, {{200, 200}, {500, 100}, {700, 50}});
    ShipBlockDirtyRanges(TArray<int32>({7, 2, 3, 3, 5, 8, -1}), 750, LNT_BLOCK, ranges);
    checkRanges(TEXT("out-of-range blocks ignored"), ranges, {{200, 200}, {500, 100}, {700, 50}});
    ranges = {FSOLShipRange{1, 1}};
    ShipBlockDirtyRanges(TArray<int32>(), 750, LNT_BLOCK, ranges);
    checkRanges(TEXT("no moved blocks"), ranges, {});
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 19 (TexelRegions): a linear rail texel span split into head, full-row and tail rects
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficTexelRegionsTest, "SOLTest.LaneNetworkTraffic.TexelRegions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficTexelRegionsTest::RunTest(const FString& /*parameters*/)
{
    // Checks regions against expected (x, y, w, h) rects
    auto checkRegions = [this](const TCHAR* what, const TArray<FSOLTexelRegion>& actual,
        std::initializer_list<FIntRect> expected)
    {
        const TArray<FIntRect> expectedArray(expected);
        bool bSame = actual.Num() == expectedArray.Num();
        for (int32 i = 0; bSame && i < actual.Num(); ++i)
        {
            const FIntRect& e = expectedArray[i];
            bSame = actual[i].X == e.Min.X && actual[i].Y == e.Min.Y && actual[i].Width == e.Max.X
                && actual[i].Height == e.Max.Y;
        }
        FString got;
        for (const FSOLTexelRegion& region : actual)
        {
            got += FString::Printf(TEXT("(%d, %d, %d, %d) "), region.X, region.Y, region.Width, region.Height);
        }
        TestTrue(FString::Printf(TEXT("%s (got %s)"), what, *got), bSame);
    };

    // FIntRect carries (x, y) in Min and (width, height) in Max here
    const int32 width = 2048;
    TArray<FSOLTexelRegion> regions = {FSOLTexelRegion{1, 1, 1, 1}};
    SOLNpcRail::RailTexelRegions(100, 200, width, regions);
    checkRegions(TEXT("(100, 200)"), regions, {FIntRect(100, 0, 200, 1)});
    SOLNpcRail::RailTexelRegions(2000, 200, width, regions);
    checkRegions(TEXT("(2000, 200)"), regions, {FIntRect(2000, 0, 48, 1), FIntRect(0, 1, 152, 1)});
    SOLNpcRail::RailTexelRegions(2000, 4154, width, regions);
    checkRegions(TEXT("(2000, 4154)"), regions,
        {FIntRect(2000, 0, 48, 1), FIntRect(0, 1, 2048, 2), FIntRect(0, 3, 10, 1)});
    SOLNpcRail::RailTexelRegions(0, 4096, width, regions);
    checkRegions(TEXT("(0, 4096)"), regions, {FIntRect(0, 0, 2048, 2)});
    regions = {FSOLTexelRegion{1, 1, 1, 1}};
    SOLNpcRail::RailTexelRegions(0, 0, width, regions);
    checkRegions(TEXT("count 0"), regions, {});
    regions = {FSOLTexelRegion{1, 1, 1, 1}};
    SOLNpcRail::RailTexelRegions(0, -4, width, regions);
    checkRegions(TEXT("count < 0"), regions, {});
    regions = {FSOLTexelRegion{1, 1, 1, 1}};
    SOLNpcRail::RailTexelRegions(0, 100, 0, regions);
    checkRegions(TEXT("width 0"), regions, {});
    regions = {FSOLTexelRegion{1, 1, 1, 1}};
    SOLNpcRail::RailTexelRegions(-1, 100, width, regions);
    checkRegions(TEXT("first < 0"), regions, {});
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 20 (LaneSpawnBlocks): the MakeSpawn block overload puts a lane ship in its block's slot (parked for a free
// block) with every other field as the legacy MakeSpawn; per-block in-wall and halo counts; rail groups of slots and
// the parked rail
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficLaneSpawnBlocksTest,
    "SOLTest.LaneNetworkTraffic.LaneSpawnBlocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficLaneSpawnBlocksTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const uint32 seed = SOL::NPC_DEFAULT_SEED;
    const FLayout layout = DefaultLayout();
    const FBattleParams battle = LntBattle();
    const TArray<double> base = LntBaseWeights();

    const FCounts counts = SplitCounts(10000, 2000, 0.4);
    TestEqual(TEXT("split: battle"), counts.Battle, 2000);
    TestEqual(TEXT("split: lane"), counts.Lane, 3200);
    TestEqual(TEXT("split: cluster"), counts.Cluster, 4800);

    // The 32-block map, by hand and from AssignShipBlocks
    const TArray<int32> map32 = LntContiguousBlockMap(32, base);
    TArray<int32> assigned;
    SOLLaneNetwork::AssignShipBlocks(32, base, assigned);
    LntIntsView(*this, TEXT("AssignShipBlocks(32, base)"), assigned, map32);

    // Runs the overload over every NPC index against the legacy MakeSpawn and tallies per-slot in-wall and halo ships
    auto checkSpawns = [&](const TCHAR* what, const FCounts& c, const TArray<int32>& map, TArray<int32>& outSlotInWall,
        TArray<int32>& outSlotHalo)
    {
        outSlotInWall.Init(0, 8);
        outSlotHalo.Init(0, 8);
        int32 groupWrong = 0;
        int32 laneFieldsWrong = 0;
        int32 otherWrong = 0;
        for (int32 index = 0; index < c.Total(); ++index)
        {
            const FSpawn legacy = MakeSpawn(seed, index, c, layout, battle);
            const FSpawn blocked = MakeSpawn(seed, index, c, layout, battle, map, LNT_BLOCK);
            const FPlacementSlot slot = ClassifyIndex(index, c);
            if (slot.Kind == EPlacementKind::Lane)
            {
                const int32 block = slot.LocalIndex / LNT_BLOCK;
                const int32 expectedGroup = map.IsValidIndex(block) ? map[block] : INDEX_NONE;
                groupWrong += blocked.GroupIndex != expectedGroup ? 1 : 0;
                laneFieldsWrong += LntLaneFieldsEqual(blocked, legacy) ? 0 : 1;
                if (blocked.GroupIndex >= 0 && blocked.GroupIndex < 8)
                {
                    (blocked.bLaneHalo ? outSlotHalo : outSlotInWall)[blocked.GroupIndex] += 1;
                }
            }
            else
            {
                otherWrong += LntSpawnsEqual(blocked, legacy) ? 0 : 1;
            }
        }
        TestEqual(FString::Printf(TEXT("%s: lane ships with a wrong group"), what), groupWrong, 0);
        TestEqual(FString::Printf(TEXT("%s: lane ships whose lane fields differ from legacy"), what),
            laneFieldsWrong, 0);
        TestEqual(FString::Printf(TEXT("%s: battle and cluster ships that differ from legacy"), what), otherWrong, 0);
    };

    TArray<int32> slotInWall;
    TArray<int32> slotHalo;
    checkSpawns(TEXT("32 blocks"), counts, map32, slotInWall, slotHalo);

    // Parked: free blocks give INDEX_NONE with the lane fields still filled
    TArray<int32> parkedMap = map32;
    for (const int32 block : {0, 5, 6, 20, 31})
    {
        parkedMap[block] = INDEX_NONE;
    }
    TArray<int32> parkedInWall;
    TArray<int32> parkedHalo;
    checkSpawns(TEXT("parked blocks"), counts, parkedMap, parkedInWall, parkedHalo);

    // CountBlockSpawns: one entry per block, in-wall + halo = block size, halo total = legacy halo ships
    TArray<int32> inWall = {99};
    TArray<int32> halo = {99};
    CountBlockSpawns(seed, counts, LNT_BLOCK, inWall, halo);
    TestEqual(TEXT("CountBlockSpawns: in-wall entries"), inWall.Num(), 32);
    TestEqual(TEXT("CountBlockSpawns: halo entries"), halo.Num(), 32);
    int32 blockSumWrong = 0;
    int32 haloTotal = 0;
    for (int32 block = 0; block < 32; ++block)
    {
        const int32 a = inWall.IsValidIndex(block) ? inWall[block] : 0;
        const int32 h = halo.IsValidIndex(block) ? halo[block] : 0;
        blockSumWrong += (a + h == 100) ? 0 : 1;
        haloTotal += h;
    }
    TestEqual(TEXT("CountBlockSpawns: blocks whose in-wall + halo is not 100"), blockSumWrong, 0);
    int32 legacyHalo = 0;
    for (int32 index = 0; index < counts.Total(); ++index)
    {
        const FSpawn legacy = MakeSpawn(seed, index, counts, layout, battle);
        legacyHalo += (legacy.Kind == EPlacementKind::Lane && legacy.bLaneHalo) ? 1 : 0;
    }
    TestEqual(TEXT("CountBlockSpawns: halo total equals the legacy halo ships"), haloTotal, legacyHalo);

    // Summed per slot, the block counts equal a direct tally of the overload's spawns
    TArray<int32> sumInWall;
    TArray<int32> sumHalo;
    SOLLaneNetwork::SumBlockCounts(map32, inWall, 8, sumInWall);
    SOLLaneNetwork::SumBlockCounts(map32, halo, 8, sumHalo);
    LntIntsView(*this, TEXT("per-slot in-wall: block sums vs spawn tally"), sumInWall, slotInWall);
    LntIntsView(*this, TEXT("per-slot halo: block sums vs spawn tally"), sumHalo, slotHalo);

    // Partial block row (P3): lane 3,250, 33 blocks, the last of 50
    const FCounts partial = SplitCounts(10000, 2000, 0.40625);
    TestEqual(TEXT("partial split: lane"), partial.Lane, 3250);
    TestEqual(TEXT("partial split: cluster"), partial.Cluster, 4750);
    CountBlockSpawns(seed, partial, LNT_BLOCK, inWall, halo);
    TestEqual(TEXT("partial CountBlockSpawns: in-wall entries"), inWall.Num(), 33);
    TestEqual(TEXT("partial CountBlockSpawns: halo entries"), halo.Num(), 33);
    blockSumWrong = 0;
    for (int32 block = 0; block < 33; ++block)
    {
        const int32 a = inWall.IsValidIndex(block) ? inWall[block] : 0;
        const int32 h = halo.IsValidIndex(block) ? halo[block] : 0;
        blockSumWrong += (a + h == (block < 32 ? 100 : 50)) ? 0 : 1;
    }
    TestEqual(TEXT("partial CountBlockSpawns: blocks with a wrong size (100, last 50)"), blockSumWrong, 0);

    const TArray<int32> map33 = LntContiguousBlockMap(33, base);
    SOLLaneNetwork::AssignShipBlocks(33, base, assigned);
    LntIntsView(*this, TEXT("AssignShipBlocks(33, base)"), assigned, map33);
    int32 partialWrong = 0;
    for (int32 j = 3200; j < 3250; ++j)
    {
        const int32 index = partial.Battle + j;
        const FPlacementSlot slot = ClassifyIndex(index, partial);
        const FSpawn blocked = MakeSpawn(seed, index, partial, layout, battle, map33, LNT_BLOCK);
        partialWrong += (slot.Kind == EPlacementKind::Lane && slot.LocalIndex == j
            && blocked.GroupIndex == map33[32]) ? 0 : 1;
    }
    TestEqual(TEXT("partial: lane ships 3200-3249 not in block 32's slot"), partialWrong, 0);
    TArray<int32> sizes33;
    sizes33.Init(100, 33);
    sizes33[32] = 50;
    TArray<int32> perSlot;
    SOLLaneNetwork::SumBlockCounts(map33, sizes33, 8, perSlot);
    int32 perSlotTotal = 0;
    for (const int32 value : perSlot)
    {
        perSlotTotal += value;
    }
    TestEqual(TEXT("partial: per-slot ship counts sum to 3250"), perSlotTotal, 3250);

    // LaneRailGroup: the slot's group column; parked past the capacity or for no slot
    const FSOLNpcGroupTable groups = LntGroups();
    TestEqual(TEXT("rail group of slot 3"), static_cast<int32>(SOLNpcRail::LaneRailGroup(groups, 3)), 3);
    TestEqual(TEXT("rail group of slot 8"), static_cast<int32>(SOLNpcRail::LaneRailGroup(groups, 8)), 34);
    TestEqual(TEXT("rail group of slot 37"), static_cast<int32>(SOLNpcRail::LaneRailGroup(groups, 37)), 63);
    TestEqual(TEXT("rail group of slot 38 (past the capacity)"),
        static_cast<int32>(SOLNpcRail::LaneRailGroup(groups, 38)), 0xFFFF);
    TestEqual(TEXT("rail group of slot -1"), static_cast<int32>(SOLNpcRail::LaneRailGroup(groups, INDEX_NONE)),
        0xFFFF);

    // IsParked and the parked rail texels
    FSOLNpcRail rail;
    rail.Kind = static_cast<uint8>(EPlacementKind::Lane);
    rail.Group = SOL::NPC_RAIL_PARKED_GROUP;
    TestTrue(TEXT("a lane rail in the parked group is parked"), SOLNpcRail::IsParked(rail));
    rail.Group = 3;
    TestFalse(TEXT("a lane rail in group 3 is not parked"), SOLNpcRail::IsParked(rail));
    rail.Kind = static_cast<uint8>(EPlacementKind::Cluster);
    rail.Group = SOL::NPC_RAIL_PARKED_GROUP;
    TestFalse(TEXT("a cluster rail is never parked"), SOLNpcRail::IsParked(rail));
    for (const int32 faction : {0, 1})
    {
        FVector4f texelA(9.0f, 9.0f, 9.0f, 9.0f);
        FVector4f texelB(9.0f, 9.0f, 9.0f, 9.0f);
        SOLNpcRail::GpuParkedRailTexels(faction, texelA, texelB);
        const float packed = static_cast<float>(static_cast<int32>(EPlacementKind::Lane) + 4 * faction);
        LntTexel(*this, FString::Printf(TEXT("parked texel A, faction %d"), faction), texelA,
            FVector4f(packed, 0.0f, 0.0f, 0.0f));
        LntTexel(*this, FString::Printf(TEXT("parked texel B, faction %d"), faction), texelB,
            FVector4f(0.0f, 0.0f, 0.0f, static_cast<float>(SOL::NPC_LANE_SHIP_PARKED_LEG_S)));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 21 (InSpace, P1, P2): a lane ship is drawn on a leg that fits the window, through a U-turn iff the leg after it
// fits; no ship vanishes mid-leg; a docked ship's AI tier is Far at once
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficInSpaceTest, "SOLTest.LaneNetworkTraffic.InSpace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficInSpaceTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::LaneShipInSpace;
    using SOLNpcScale::EAiTier;
    const FSOLLaneCycle cycle = LntCycle();

    // The table: windows by row, times by column
    const double times[8] = {10.0, 50.0, 75.0, 99.5, 120.0, 32.0, 67.0, 102.0};
    struct FRow
    {
        const TCHAR* Name;
        double Open;
        double Close;
        bool Drawn[8];
    };
    const FRow rows[4] = {
        {TEXT("[-0.5, 100.5)"), -0.5, 100.5, {true, true, true, true, false, true, true, false}},
        {TEXT("[40.5, 100.5)"), 40.5, 100.5, {false, false, true, true, false, false, true, false}},
        {TEXT("[0, 20)"), 0.0, 20.0, {false, false, false, false, false, false, false, false}},
        {TEXT("[-inf, +inf)"), -LNT_INF, LNT_INF, {true, true, true, true, true, true, true, true}},
    };
    for (const FRow& row : rows)
    {
        for (int32 i = 0; i < 8; ++i)
        {
            LntBool(*this, FString::Printf(TEXT("window %s, t %.1f"), row.Name, times[i]),
                LaneShipInSpace(cycle, 0.0, times[i], row.Open, row.Close), row.Drawn[i]);
        }
    }
    for (const double t : times)
    {
        TestFalse(FString::Printf(TEXT("NaN window, t %.1f"), t), LaneShipInSpace(cycle, 0.0, t, LNT_NAN, LNT_NAN));
    }

    // Extra turn rows (P1)
    TestTrue(TEXT("t 33, [-0.5, 100.5)"), LaneShipInSpace(cycle, 0.0, 33.0, -0.5, 100.5));
    TestTrue(TEXT("t 34.5, [-0.5, 100.5)"), LaneShipInSpace(cycle, 0.0, 34.5, -0.5, 100.5));
    TestFalse(TEXT("t 31, [40.5, 100.5)"), LaneShipInSpace(cycle, 0.0, 31.0, 40.5, 100.5));
    TestTrue(TEXT("t 66, [40.5, 100.5)"), LaneShipInSpace(cycle, 0.0, 66.0, 40.5, 100.5));
    TestTrue(TEXT("t 69.5, [40.5, 100.5)"), LaneShipInSpace(cycle, 0.0, 69.5, 40.5, 100.5));
    TestFalse(TEXT("t 67, [70.5, 100.5) (next leg departs before the open)"),
        LaneShipInSpace(cycle, 0.0, 67.0, 70.5, 100.5));
    TestFalse(TEXT("t 67, [69.5, 99.5) (next leg arrives after the close)"),
        LaneShipInSpace(cycle, 0.0, 67.0, 69.5, 99.5));
    TestTrue(TEXT("phase 0.5, t 10, [-0.5, 100.5)"), LaneShipInSpace(cycle, 0.5, 10.0, -0.5, 100.5));

    // No ship vanishes mid-leg: the sweep follows the P1 model (constant per leg, a turn equal to the leg after it)
    for (const TPair<double, double>& window : {TPair<double, double>(-0.5, 100.5), TPair<double, double>(40.5, 100.5)})
    {
        int32 mismatches = 0;
        double firstBadT = 0.0;
        for (double t = 0.125; t < 210.0; t += 0.25)
        {
            const bool bExpected = LntExpectedInSpace(t, window.Key, window.Value);
            if (LaneShipInSpace(cycle, 0.0, t, window.Key, window.Value) != bExpected)
            {
                firstBadT = mismatches == 0 ? t : firstBadT;
                ++mismatches;
            }
        }
        TestEqual(FString::Printf(TEXT("sweep [%.1f, %.1f): samples off the leg / turn model (first at t %.3f)"),
            window.Key, window.Value, firstBadT), mismatches, 0);
    }

    // With [40.5, 100.5) the drawn set is exactly t in [65, 100) (TurnAtA and the outbound leg after it)
    int32 drawnSetWrong = 0;
    for (double t = 0.125; t < 210.0; t += 0.25)
    {
        const bool bExpected = t >= 65.0 && t < 100.0;
        drawnSetWrong += LaneShipInSpace(cycle, 0.0, t, 40.5, 100.5) == bExpected ? 0 : 1;
    }
    TestEqual(TEXT("[40.5, 100.5): samples outside the drawn set [65, 100)"), drawnSetWrong, 0);

    // Invalid cycle: drawn while the window is open
    const FSOLLaneCycle invalid{};
    TestTrue(TEXT("invalid cycle, t 5, [0, 10)"), LaneShipInSpace(invalid, 0.0, 5.0, 0.0, 10.0));
    TestFalse(TEXT("invalid cycle, t 10.5, [0, 10)"), LaneShipInSpace(invalid, 0.0, 10.5, 0.0, 10.0));

    // LaneShipAiTier (Q26, P2): Far iff docked, else the selected tier
    using SOLNpcScale::LaneShipAiTier;
    TestEqual(TEXT("tier (Far, Mid, in space)"),
        LntInt(LaneShipAiTier(EAiTier::Far, EAiTier::Mid, true)), LntInt(EAiTier::Mid));
    TestEqual(TEXT("tier (Mid, Mid, in space)"),
        LntInt(LaneShipAiTier(EAiTier::Mid, EAiTier::Mid, true)), LntInt(EAiTier::Mid));
    TestEqual(TEXT("tier (Far, Mid, docked)"),
        LntInt(LaneShipAiTier(EAiTier::Far, EAiTier::Mid, false)), LntInt(EAiTier::Far));
    TestEqual(TEXT("tier (Far, Far, docked)"),
        LntInt(LaneShipAiTier(EAiTier::Far, EAiTier::Far, false)), LntInt(EAiTier::Far));
    TestEqual(TEXT("tier (Mid, Mid, docked)"),
        LntInt(LaneShipAiTier(EAiTier::Mid, EAiTier::Mid, false)), LntInt(EAiTier::Far));
    TestEqual(TEXT("tier (Mid, Full, docked)"),
        LntInt(LaneShipAiTier(EAiTier::Mid, EAiTier::Full, false)), LntInt(EAiTier::Far));
    TestEqual(TEXT("tier (Full, Full, docked)"),
        LntInt(LaneShipAiTier(EAiTier::Full, EAiTier::Full, false)), LntInt(EAiTier::Far));
    TestEqual(TEXT("tier (Full, Mid, docked)"),
        LntInt(LaneShipAiTier(EAiTier::Full, EAiTier::Mid, false)), LntInt(EAiTier::Far));
    TestEqual(TEXT("tier (Mid, Far, docked)"),
        LntInt(LaneShipAiTier(EAiTier::Mid, EAiTier::Far, false)), LntInt(EAiTier::Far));
    TestEqual(TEXT("tier (Full, Far, in space)"),
        LntInt(LaneShipAiTier(EAiTier::Full, EAiTier::Far, true)), LntInt(EAiTier::Far));
    TestEqual(TEXT("tier (Mid, Full, in space)"),
        LntInt(LaneShipAiTier(EAiTier::Mid, EAiTier::Full, true)), LntInt(EAiTier::Full));

    // Sequence: window [40.5, 100.5), selected Mid; the ship docks at the end ring at t 100
    const double seqTimes[3] = {99.875, 100.125, 100.375};
    const bool seqInSpace[3] = {true, false, false};
    const EAiTier seqTier[3] = {EAiTier::Mid, EAiTier::Far, EAiTier::Far};
    EAiTier previous = EAiTier::Far;
    for (int32 i = 0; i < 3; ++i)
    {
        const bool bInSpace = LaneShipInSpace(cycle, 0.0, seqTimes[i], 40.5, 100.5);
        LntBool(*this, FString::Printf(TEXT("sequence t %.3f: in space"), seqTimes[i]), bInSpace, seqInSpace[i]);
        const EAiTier tier = LaneShipAiTier(previous, EAiTier::Mid, bInSpace);
        TestEqual(FString::Printf(TEXT("sequence t %.3f: tier"), seqTimes[i]), LntInt(tier), LntInt(seqTier[i]));
        previous = tier;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 22 (WindowTexel): window bounds relative to the GPU epoch as float, the slot window texel, the row-6 write at
// each slot's column and the per-episode slot profile
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficWindowTexelTest, "SOLTest.LaneNetworkTraffic.WindowTexel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficWindowTexelTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneNetwork;
    const double epoch = 810005000.0;                   // 9,375 E + 5,000
    const float far = static_cast<float>(SOL::NPC_LANE_WINDOW_FAR_S);

    // LaneWindowGpuS
    LntFloatExact(*this, TEXT("9375 E"), LaneWindowGpuS(9375.0 * LNT_DAY_S, epoch), -5000.0f);
    LntFloatExact(*this, TEXT("9380 E"), LaneWindowGpuS(9380.0 * LNT_DAY_S, epoch), 427000.0f);
    LntFloatExact(*this, TEXT("892944000"), LaneWindowGpuS(892944000.0, epoch), 82939000.0f);
    LntFloatExact(*this, TEXT("892944003 (float rounding)"), LaneWindowGpuS(892944003.0, epoch), 82939000.0f);
    LntFloatExact(*this, TEXT("+inf"), LaneWindowGpuS(LNT_INF, epoch), far);
    LntFloatExact(*this, TEXT("-inf"), LaneWindowGpuS(-LNT_INF, epoch), -far);
    LntFloatExact(*this, TEXT("beyond +FAR clamps"), LaneWindowGpuS(2.0e30, 0.0), far);
    LntFloatExact(*this, TEXT("beyond -FAR clamps"), LaneWindowGpuS(-2.0e30, 0.0), -far);
    TestTrue(TEXT("NaN stays NaN"), FMath::IsNaN(LaneWindowGpuS(LNT_NAN, epoch)));

    // LaneWindowTexel
    const double open = 9375.0 * LNT_DAY_S;
    const double close = 9380.0 * LNT_DAY_S;
    LntTexel(*this, TEXT("window, active"), LaneWindowTexel(true, open, close, epoch, true),
        FVector4f(-5000.0f, 427000.0f, 1.0f, 0.0f));
    LntTexel(*this, TEXT("window, inactive"), LaneWindowTexel(true, open, close, epoch, false),
        FVector4f(-5000.0f, 427000.0f, 0.0f, 0.0f));
    LntTexel(*this, TEXT("open over all time"), LaneWindowTexel(true, -LNT_INF, LNT_INF, epoch, true),
        FVector4f(-far, far, 1.0f, 0.0f));
    const FVector4f emptyActive(far, -far, 1.0f, 0.0f);
    const FVector4f emptyInactive(far, -far, 0.0f, 0.0f);
    LntTexel(*this, TEXT("no window, active"), LaneWindowTexel(false, open, close, epoch, true), emptyActive);
    LntTexel(*this, TEXT("no window, inactive"), LaneWindowTexel(false, open, close, epoch, false), emptyInactive);
    LntTexel(*this, TEXT("NaN open"), LaneWindowTexel(true, LNT_NAN, close, epoch, true), emptyActive);
    LntTexel(*this, TEXT("NaN close"), LaneWindowTexel(true, open, LNT_NAN, epoch, true), emptyActive);
    LntTexel(*this, TEXT("open == close"), LaneWindowTexel(true, open, open, epoch, true), emptyActive);
    LntTexel(*this, TEXT("open > close"), LaneWindowTexel(true, close, open, epoch, false), emptyInactive);

    // WriteLaneWindowRow: 8 lanes, 10 clusters, 16 battles, 9 slot texels; slot 8 lands at column 34
    const FSOLNpcGroupTable groups = LntGroups();
    const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    const int32 rowBase = SOL::NPC_GROUP_WINDOW_ROW * width;
    const FVector4f sentinel(9.0f, 9.0f, 9.0f, 9.0f);
    const FVector4f zero(0.0f, 0.0f, 0.0f, 0.0f);
    TArray<FVector4f> windows;
    for (int32 slot = 0; slot < 9; ++slot)
    {
        windows.Add(FVector4f(slot + 1.0f, slot + 2.0f, 1.0f, 0.0f));
    }
    TArray<FVector4f> texels;
    texels.Init(sentinel, width * SOL::NPC_GROUP_TEXTURE_ROWS);
    SOLNpcRail::WriteLaneWindowRow(windows, groups, texels);
    for (int32 slot = 0; slot < 8; ++slot)
    {
        LntTexel(*this, FString::Printf(TEXT("slot %d at column %d"), slot, slot), texels[rowBase + slot],
            windows[slot]);
    }
    LntTexel(*this, TEXT("slot 8 at column 34"), texels[rowBase + 34], windows[8]);
    LntTexel(*this, TEXT("column 8 (a cluster) zero"), texels[rowBase + 8], zero);
    int32 otherColumnsWrong = 0;
    for (int32 column = 8; column < width; ++column)
    {
        otherColumnsWrong += (column != 34 && texels[rowBase + column] != zero) ? 1 : 0;
    }
    TestEqual(TEXT("row 6 columns without a slot that are not zero"), otherColumnsWrong, 0);
    int32 rowsTouched = 0;
    for (int32 i = 0; i < rowBase; ++i)
    {
        rowsTouched += texels[i] != sentinel ? 1 : 0;
    }
    TestEqual(TEXT("rows 0-5 texels touched"), rowsTouched, 0);

    // Slots without a column are skipped (40 slots: 38 and 39 are past the capacity of 38)
    TArray<FVector4f> manyWindows;
    for (int32 slot = 0; slot < 40; ++slot)
    {
        manyWindows.Add(FVector4f(slot + 1.0f, slot + 2.0f, 0.0f, 0.0f));
    }
    texels.Init(sentinel, width * SOL::NPC_GROUP_TEXTURE_ROWS);
    SOLNpcRail::WriteLaneWindowRow(manyWindows, groups, texels);
    LntTexel(*this, TEXT("40 slots: slot 37 at column 63"), texels[rowBase + 63], manyWindows[37]);
    LntTexel(*this, TEXT("40 slots: slot 8 at column 34"), texels[rowBase + 34], manyWindows[8]);
    LntTexel(*this, TEXT("40 slots: column 8 zero"), texels[rowBase + 8], zero);

    // A buffer under NPC_GROUP_TEXTURE_ROWS rows is not touched
    TArray<FVector4f> shortTexels;
    shortTexels.Init(sentinel, width * (SOL::NPC_GROUP_TEXTURE_ROWS - 1));
    SOLNpcRail::WriteLaneWindowRow(windows, groups, shortTexels);
    int32 shortTouched = 0;
    for (const FVector4f& texel : shortTexels)
    {
        shortTouched += texel != sentinel ? 1 : 0;
    }
    TestEqual(TEXT("short buffer texels touched"), shortTouched, 0);

    // SlotLaneProfile: MakeLaneProfile of the tunnel radius, and BuildLaneProfiles' entry for the same inputs
    struct FProfileRow
    {
        double LaneRadiusM;
        int32 InWall;
        double NominalLengthM;
        double RadiusAM;
        double RadiusBM;
    };
    const FProfileRow profileRows[3] = {
        {4.0e6, 0, 4.0e11, 6.371e6, 3.3895e6},
        {2.0e5, 3000, 4.06e8, 6.371e6, 1.7374e6},
        {4.0e6, 50000, 1.0e12, 6.9911e7, 5.8232e7},
    };
    for (const FProfileRow& row : profileRows)
    {
        const FSOLLaneProfile slotProfile = SOLNpcRail::SlotLaneProfile(row.LaneRadiusM, row.InWall,
            row.NominalLengthM, row.RadiusAM, row.RadiusBM);
        const FSOLLaneProfile identity = SOLLaneTraffic::MakeLaneProfile(
            SOLNpcTunnel::TunnelRadiusM(row.LaneRadiusM, row.InWall), row.NominalLengthM, row.RadiusAM, row.RadiusBM);
        TestTrue(FString::Printf(TEXT("slot profile (R %.3g, in-wall %d) == MakeLaneProfile(TunnelRadiusM)"),
            row.LaneRadiusM, row.InWall), LntProfilesEqual(slotProfile, identity));

        FSOLNpcGroupTable table;
        table.LaneCount = 1;
        table.LaneRadiusM = {row.LaneRadiusM};
        SOLNpcRail::BuildLaneProfiles(table, TArray<int32>({row.InWall}), TArray<int32>({0}),
            TArray<double>({row.NominalLengthM}), TArray<double>({row.RadiusAM}), TArray<double>({row.RadiusBM}));
        const bool bBuilt = table.LaneProfile.Num() == 1;
        TestTrue(TEXT("BuildLaneProfiles gave one profile"), bBuilt);
        if (bBuilt)
        {
            TestTrue(FString::Printf(TEXT("slot profile (R %.3g, in-wall %d) == BuildLaneProfiles entry"),
                row.LaneRadiusM, row.InWall), LntProfilesEqual(slotProfile, table.LaneProfile[0]));
        }
    }
    const FSOLLaneProfile emptySlot = SOLNpcRail::SlotLaneProfile(4.0e6, 0, 4.0e11, 6.371e6, 3.3895e6);
    LntNear(*this, TEXT("empty slot: wall radius = 4e6 x NPC_TUNNEL_RADIUS_MIN_SCALE"), emptySlot.WallRadiusM,
        4.0e6 * SOL::NPC_TUNNEL_RADIUS_MIN_SCALE, LNT_TIGHT);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 23 (Activation, Q21, Q22, P5): the lane radius in reference pixels from the camera's segment distance, and the
// active flag at 2.4 px x the QualityPreset scale or with a CPU-tier ship; no overlay input
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficActivationTest, "SOLTest.LaneNetworkTraffic.Activation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficActivationTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneNetwork;
    const FVector3d a(0.0, 0.0, 0.0);
    const FVector3d b(1.0e10, 0.0, 0.0);
    const double laneR = 2.0e6;
    const double focal = 960.0;
    const double threshold = SOL::NPC_LANE_TRAFFIC_ACTIVE_PX;

    // Radius in pixels: the camera at (5e9, y, 0) gives 1.92e9 / y px
    for (const double y : {8.0e8, 7.9e8, 8.1e8, 7.9e7, 8.1e7, 1.7e8, 1.8e8, 3.6e8, 3.7e8})
    {
        LntNear(*this, FString::Printf(TEXT("radius px at y %.3g"), y),
            LaneTrafficRadiusPx(FVector3d(5.0e9, y, 0.0), a, b, laneR, focal), 1.92e9 / y, LNT_TIGHT);
    }

    // Scale 1 (Epic)
    TestTrue(TEXT("scale 1: y 7.9e8 active"), LaneTrafficActive(1.92e9 / 7.9e8, 0, 1.0));
    TestFalse(TEXT("scale 1: y 8.1e8 inactive"), LaneTrafficActive(1.92e9 / 8.1e8, 0, 1.0));
    TestTrue(TEXT("scale 1: y 8.1e8 with a CPU-tier ship active"), LaneTrafficActive(1.92e9 / 8.1e8, 1, 1.0));
    TestTrue(TEXT("scale 1: inclusive at 2.4 px"), LaneTrafficActive(threshold, 0, 1.0));
    TestFalse(TEXT("scale 1: just under 2.4 px"), LaneTrafficActive(threshold * (1.0 - 1.0e-9), 0, 1.0));
    TestFalse(TEXT("NaN px, no ship"), LaneTrafficActive(LNT_NAN, 0, 1.0));
    TestTrue(TEXT("NaN px, a CPU-tier ship"), LaneTrafficActive(LNT_NAN, 1, 1.0));

    // Scaled rows (P5): presets Low 0, Medium 1, High 2, Epic 3, Cinematic 4
    struct FScaledRow
    {
        double Y;
        bool Active[5];
    };
    const FScaledRow scaledRows[8] = {
        {7.9e7, {true, true, true, true, true}},
        {8.1e7, {false, true, true, true, true}},
        {1.7e8, {false, true, true, true, true}},
        {1.8e8, {false, false, true, true, true}},
        {3.6e8, {false, false, true, true, true}},
        {3.7e8, {false, false, false, true, true}},
        {7.9e8, {false, false, false, true, true}},
        {8.1e8, {false, false, false, false, false}},
    };
    for (const FScaledRow& row : scaledRows)
    {
        const double px = 1.92e9 / row.Y;
        for (int32 preset = 0; preset < 5; ++preset)
        {
            const double scale = LaneTunnelQualityScale(preset);
            LntBool(*this, FString::Printf(TEXT("y %.3g, preset %d"), row.Y, preset),
                LaneTrafficActive(px, 0, scale), row.Active[preset]);
            if (!row.Active[preset])
            {
                TestTrue(FString::Printf(TEXT("y %.3g, preset %d, a CPU-tier ship"), row.Y, preset),
                    LaneTrafficActive(px, 1, scale));
            }
        }
    }
    for (int32 preset = 0; preset < 5; ++preset)
    {
        const double scale = LaneTunnelQualityScale(preset);
        TestTrue(FString::Printf(TEXT("preset %d: inclusive at the scaled threshold"), preset),
            LaneTrafficActive(threshold * scale, 0, scale));
        TestFalse(FString::Printf(TEXT("preset %d: just under the scaled threshold"), preset),
            LaneTrafficActive(threshold * scale * (1.0 - 1.0e-9), 0, scale));
    }

    // A bad scale reads as 1
    for (const double scale : {LNT_NAN, LNT_INF, 0.0, -2.0})
    {
        TestTrue(FString::Printf(TEXT("bad scale %g: y 7.9e8 active"), scale),
            LaneTrafficActive(1.92e9 / 7.9e8, 0, scale));
        TestFalse(FString::Printf(TEXT("bad scale %g: y 8.1e8 inactive"), scale),
            LaneTrafficActive(1.92e9 / 8.1e8, 0, scale));
    }

    // Beyond end A: 3.2 px, active at scale 1, inactive at High (5.28 px)
    const double beyondPx = LaneTrafficRadiusPx(FVector3d(-6.0e8, 0.0, 0.0), a, b, laneR, focal);
    LntNear(*this, TEXT("beyond end A: 3.2 px"), beyondPx, 3.2, LNT_TIGHT);
    TestTrue(TEXT("beyond end A: active at scale 1"), LaneTrafficActive(3.2, 0, 1.0));
    TestFalse(TEXT("beyond end A: inactive at High"), LaneTrafficActive(3.2, 0, LaneTunnelQualityScale(2)));

    // On the segment: +inf, active at every scale
    const double onPx = LaneTrafficRadiusPx(FVector3d(5.0e9, 0.0, 0.0), a, b, laneR, focal);
    TestTrue(TEXT("camera on the segment: +inf px"), onPx == LNT_INF);
    for (int32 preset = 0; preset < 5; ++preset)
    {
        TestTrue(FString::Printf(TEXT("camera on the segment: active at preset %d"), preset),
            LaneTrafficActive(LNT_INF, 0, LaneTunnelQualityScale(preset)));
    }

    // A == B: the distance to the point
    LntNear(*this, TEXT("A == B, camera (0, 8e8, 0)"), LaneTrafficRadiusPx(FVector3d(0.0, 8.0e8, 0.0), a, a, laneR,
        focal), 2.4, LNT_TIGHT);
    LntNear(*this, TEXT("A == B, camera (3e8, 4e8, 0)"), LaneTrafficRadiusPx(FVector3d(3.0e8, 4.0e8, 0.0), a, a,
        laneR, focal), 1.92e9 / 5.0e8, LNT_TIGHT);

    // NaN camera: 0 px, inactive unless a CPU-tier ship exists; R <= 0 or f <= 0: 0 px (case 10)
    const double nanPx = LaneTrafficRadiusPx(FVector3d(LNT_NAN, 0.0, 0.0), a, b, laneR, focal);
    TestTrue(TEXT("NaN camera: 0 px"), nanPx == 0.0);
    TestFalse(TEXT("NaN camera: inactive"), LaneTrafficActive(0.0, 0, 1.0));
    TestTrue(TEXT("NaN camera: active with a CPU-tier ship"), LaneTrafficActive(0.0, 1, 1.0));
    TestTrue(TEXT("R 0: 0 px"), LaneTrafficRadiusPx(FVector3d(5.0e9, 8.0e8, 0.0), a, b, 0.0, focal) == 0.0);
    TestTrue(TEXT("f 0: 0 px"), LaneTrafficRadiusPx(FVector3d(5.0e9, 8.0e8, 0.0), a, b, laneR, 0.0) == 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 24 (MergeSkip, Q30, P6): LaneOpenAt; the merge readout and the lane search skip a closed lane; a latched
// Approach on a closed lane is Stale (searched again), a Hold is unchanged
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficMergeSkipTest, "SOLTest.LaneNetworkTraffic.MergeSkip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficMergeSkipTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::LaneOpenAt;

    // LaneOpenAt
    TestTrue(TEXT("open (5, 0, 10)"), LaneOpenAt(5.0, 0.0, 10.0));
    TestTrue(TEXT("open (0, 0, 10)"), LaneOpenAt(0.0, 0.0, 10.0));
    TestFalse(TEXT("open (10, 0, 10)"), LaneOpenAt(10.0, 0.0, 10.0));
    TestFalse(TEXT("open (-1, 0, 10)"), LaneOpenAt(-1.0, 0.0, 10.0));
    TestTrue(TEXT("open (5, -inf, +inf)"), LaneOpenAt(5.0, -LNT_INF, LNT_INF));
    TestFalse(TEXT("open (NaN, 0, 10)"), LaneOpenAt(LNT_NAN, 0.0, 10.0));
    TestFalse(TEXT("open (5, NaN, 10)"), LaneOpenAt(5.0, LNT_NAN, 10.0));
    TestFalse(TEXT("open (5, 0, NaN)"), LaneOpenAt(5.0, 0.0, LNT_NAN));

    // The merge readout: the player on the axis at the midpoint, moving along +x
    const FVector3d playerM(3.0e11, 2.0e10, 0.0);
    const FVector3d playerVelocity(1.0e4, 0.0, 0.0);
    const FVector3d forward(1.0, 0.0, 0.0);
    {
        const FSOLLaneMergeReadout bothOpen = SOLLaneTraffic::MergeReadout(LntMergeLanes(true, true), playerM,
            playerVelocity, forward, 1.0);
        TestTrue(TEXT("readout, both open: shown"), bothOpen.bShown);
        TestEqual(TEXT("readout, both open: lane 0"), bothOpen.Lane, 0);
        const FSOLLaneMergeReadout lane0Closed = SOLLaneTraffic::MergeReadout(LntMergeLanes(false, true), playerM,
            playerVelocity, forward, 1.0);
        TestTrue(TEXT("readout, lane 0 closed: shown"), lane0Closed.bShown);
        TestEqual(TEXT("readout, lane 0 closed: lane 1"), lane0Closed.Lane, 1);
        const FSOLLaneMergeReadout bothClosed = SOLLaneTraffic::MergeReadout(LntMergeLanes(false, false), playerM,
            playerVelocity, forward, 1.0);
        TestFalse(TEXT("readout, both closed: hidden"), bothClosed.bShown);
    }

    // The lane search
    const FVector3d shipM(2.0e11, 2.0e10, 0.0);
    const FVector3d targetM(4.0e11, 2.0e10, 0.0);
    const FSOLLaneAutoLeg leg0 = SOLLaneTraffic::FindLaneLeg(LntMergeLanes(true, true), shipM, targetM);
    TestTrue(TEXT("search, both open: found"), leg0.bFound);
    TestEqual(TEXT("search, both open: lane 0"), leg0.Lane, 0);
    TestTrue(TEXT("search, both open: target inside"), leg0.bTargetInside);
    const FSOLLaneAutoLeg leg1 = SOLLaneTraffic::FindLaneLeg(LntMergeLanes(false, true), shipM, targetM);
    TestTrue(TEXT("search, lane 0 closed: found"), leg1.bFound);
    TestEqual(TEXT("search, lane 0 closed: lane 1"), leg1.Lane, 1);
    const FSOLLaneAutoLeg legNone = SOLLaneTraffic::FindLaneLeg(LntMergeLanes(false, false), shipM, targetM);
    TestFalse(TEXT("search, both closed: not found"), legNone.bFound);

    // CheckLaneLatch (P6)
    using SOLLaneTraffic::CheckLaneLatch;
    const TArray<FSOLLaneMergeLane> open2 = LntMergeLanes(true, true);
    const TArray<FSOLLaneMergeLane> closed0 = LntMergeLanes(false, true);
    const TArray<FSOLLaneMergeLane> closed1 = LntMergeLanes(true, false);
    const TArray<FSOLLaneMergeLane> closedBoth = LntMergeLanes(false, false);

    // Returns the latch check of a leg in a mode for the ship above, as int32
    auto latch = [&shipM](const TArray<FSOLLaneMergeLane>& lanes, const FSOLLaneAutoLeg& leg,
        const ESOLLaneAutoMode mode, const FVector3d& target)
    {
        return LntInt(CheckLaneLatch(lanes, leg, mode, shipM, target));
    };
    TestEqual(TEXT("latch, both open: Approach keeps"),
        latch(open2, leg0, ESOLLaneAutoMode::Approach, targetM), LntInt(ESOLLaneLatchCheck::Keep));
    TestEqual(TEXT("latch, both open: Hold keeps"),
        latch(open2, leg0, ESOLLaneAutoMode::Hold, targetM), LntInt(ESOLLaneLatchCheck::Keep));
    TestEqual(TEXT("latch, lane 0 closed: Approach is Stale"),
        latch(closed0, leg0, ESOLLaneAutoMode::Approach, targetM), LntInt(ESOLLaneLatchCheck::Stale));
    TestEqual(TEXT("latch, lane 0 closed: Hold keeps"),
        latch(closed0, leg0, ESOLLaneAutoMode::Hold, targetM), LntInt(ESOLLaneLatchCheck::Keep));
    TestEqual(TEXT("latch, lane 0 closed: None keeps"),
        latch(closed0, leg0, ESOLLaneAutoMode::None, targetM), LntInt(ESOLLaneLatchCheck::Keep));
    TestEqual(TEXT("latch, lane 0 closed: Left keeps"),
        latch(closed0, leg0, ESOLLaneAutoMode::Left, targetM), LntInt(ESOLLaneLatchCheck::Keep));
    const FVector3d targetLeft(4.0e11, 2.0e10 + 1.0e9, 0.0);
    TestEqual(TEXT("latch, lane 0 closed, target left: Approach is Stale (not Release)"),
        latch(closed0, leg0, ESOLLaneAutoMode::Approach, targetLeft), LntInt(ESOLLaneLatchCheck::Stale));
    TestEqual(TEXT("latch, lane 0 closed, target left: Hold releases"),
        latch(closed0, leg0, ESOLLaneAutoMode::Hold, targetLeft), LntInt(ESOLLaneLatchCheck::Release));
    TestEqual(TEXT("latch, lane 1 closed: Approach on lane 0 keeps"),
        latch(closed1, leg0, ESOLLaneAutoMode::Approach, targetM), LntInt(ESOLLaneLatchCheck::Keep));
    TestEqual(TEXT("latch, both closed: Approach on lane 1 is Stale"),
        latch(closedBoth, leg1, ESOLLaneAutoMode::Approach, targetM), LntInt(ESOLLaneLatchCheck::Stale));

    // LaneAutoDecide after a Stale Approach: dropped and searched again (P6)
    using SOLLaneTraffic::LaneAutoDecide;
    const FSOLLaneAutoDecision searched = LaneAutoDecide(closed0, leg0, ESOLLaneAutoMode::Approach,
        LntAutoInput(shipM, targetM, true));
    TestTrue(TEXT("decide, lane 0 closed, search: found"), searched.Leg.bFound);
    TestEqual(TEXT("decide, lane 0 closed, search: lane 1"), searched.Leg.Lane, 1);
    TestTrue(TEXT("decide, lane 0 closed, search: order not ended"), searched.Mode != ESOLLaneAutoMode::Left);
    const FSOLLaneAutoDecision noSearch = LaneAutoDecide(closed0, leg0, ESOLLaneAutoMode::Approach,
        LntAutoInput(shipM, targetM, false));
    TestFalse(TEXT("decide, lane 0 closed, no search: not found"), noSearch.Leg.bFound);
    TestEqual(TEXT("decide, lane 0 closed, no search: None"), LntInt(noSearch.Mode), LntInt(ESOLLaneAutoMode::None));
    const FSOLLaneAutoDecision noLane = LaneAutoDecide(closedBoth, leg0, ESOLLaneAutoMode::Approach,
        LntAutoInput(shipM, targetM, true));
    TestFalse(TEXT("decide, both closed: not found"), noLane.Leg.bFound);
    TestEqual(TEXT("decide, both closed: None"), LntInt(noLane.Mode), LntInt(ESOLLaneAutoMode::None));
    const FSOLLaneAutoDecision control = LaneAutoDecide(open2, leg0, ESOLLaneAutoMode::Approach,
        LntAutoInput(shipM, targetM, true));
    TestEqual(TEXT("decide, control (both open): lane 0"), control.Leg.Lane, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Case 26 (Constants): the 53c2 constants' invariants (also static_asserts in SOLConstants.h)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficConstantsTest, "SOLTest.LaneNetworkTraffic.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSOLLaneNetworkTrafficConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("window row inside the group texture"), SOL::NPC_GROUP_WINDOW_ROW < SOL::NPC_GROUP_TEXTURE_ROWS);
    TestEqual(TEXT("default block count"), SOL::NPC_LANE_SHIP_BLOCK_COUNT_DEFAULT, 4000);
    TestEqual(TEXT("default block count = default lane ships / block size"), SOL::NPC_LANE_SHIP_BLOCK_COUNT_DEFAULT,
        SOL::NPC_LANE_SHIP_DEFAULT_COUNT / SOL::NPC_LANE_SHIP_BLOCK_SIZE);
    TestTrue(TEXT("active px == reference focal / fade far radii (exact)"),
        SOL::NPC_LANE_TRAFFIC_ACTIVE_PX == SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX / SOL::NPC_LANE_FADE_FAR_RADII);
    TestTrue(TEXT("active px == 2.4"), SOL::NPC_LANE_TRAFFIC_ACTIVE_PX == 2.4);
    TestTrue(TEXT("parked leg time < 0"), SOL::NPC_LANE_SHIP_PARKED_LEG_S < 0.0);
    TestTrue(TEXT("parked group is no group column"),
        static_cast<int32>(SOL::NPC_RAIL_PARKED_GROUP) >= SOL::NPC_GROUP_TEXTURE_WIDTH);
    const float farFloat = static_cast<float>(SOL::NPC_LANE_WINDOW_FAR_S);
    TestTrue(TEXT("far window bound finite in float"), FMath::IsFinite(farFloat));
    TestTrue(TEXT("far window bound far beyond the look-ahead"), SOL::NPC_LANE_WINDOW_FAR_S
        > 1.0e3 * SOL::NPC_LANE_NETWORK_HORIZON_EPOCHS * SOL::NPC_LANE_NETWORK_EPOCH_S);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
