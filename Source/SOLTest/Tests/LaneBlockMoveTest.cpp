/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLAsteroidBelt.h"
#include "Npc/SOLLaneNodeTable.h"
#include "Npc/SOLNpcBlockMoves.h"
#include "Npc/SOLNpcFragments.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcTrafficMirror.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"
#include "Traffic/SOLLaneNetworkSchedule.h"
#include "Traffic/SOLLaneTrafficSubsystem.h"
#include "Universe/SOLBodyRegistry.h"

#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #53 part 53c2b block moves (SDD 53 4.5, contract "53c2 additions" P3 / P4): the per-step block
// decision (StepShipBlocks: release, episode open, take in ascending slot order, re-rail set, held slots), the packed
// partial rail upload layout (PackRailUploadRegions) and the Mass re-bucket of moved lane ships on a standalone entity
// manager (RebucketLaneShips), the lane table's in-place end update, the time-jump slot holds (SDD 53 4.5, P4 d) and
// the no-network window row (contract case 25). TimeJumpHolds and NoNetworkWindow were written from the contract
// ahead of the 53c2b review fixes they cover; the other cases were written alongside the 53c2b production code

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double LBM_DAY_S = 86400.0;
    constexpr double LBM_2026_S = 9496.5 * LBM_DAY_S;   // 2026-01-01 00:00, seconds since J2000

    // The registry, node table and config of the real lane network (outlives every schedule using it)
    struct FLbmNetworkFixture
    {
        FSOLBodyRegistry Registry;
        SOLLaneNodeTable::FTable Table;
        FSOLLaneNetworkConfig Config;
    };

    //////////////////////////////////////////////////////////////////////////
    // Fills a fixture: the populated registry, the 24-node table at t0 and the game's network config
    void LbmMakeNetworkFixture(FLbmNetworkFixture& fixture, const double t0S)
    {
        fixture.Registry.PopulateSolarSystem();
        fixture.Table = SOLLaneNodeTable::Build(fixture.Registry, SOLAsteroidBelt::RealAsteroids(),
            SOLNpcScale::DefaultLayout().Lanes, t0S);
        fixture.Config.Nodes = fixture.Table.Nodes;
        fixture.Config.NodeMaxSpeedMps = fixture.Table.MaxSpeedMps;
        fixture.Config.BaseLanes = fixture.Table.BaseLanes;
        fixture.Config.RootNode = SOLLaneNodeTable::EARTH_NODE;
        fixture.Config.SlotCapacity = SOL::NPC_GROUP_TEXTURE_WIDTH - 10 - 16;   // 64 - clusters - battles
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the fixture's node position function
    FSOLLaneNodePositionsFn LbmPositionsFn(const FLbmNetworkFixture& fixture)
    {
        return [&fixture](const double timeS, TArrayView<FVector3d> outPositionsM)
        {
            SOLLaneNodeTable::PositionsAt(fixture.Table, fixture.Registry, timeS, outPositionsM);
        };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two lane pairs are the same lane
    bool LbmSameLane(const FSOLLanePair& a, const FSOLLanePair& b)
    {
        return a.NodeA == b.NodeA && a.NodeB == b.NodeB;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the index of a lane in a list, INDEX_NONE if absent
    int32 LbmFindLane(TConstArrayView<FSOLLanePair> lanes, const FSOLLanePair& lane)
    {
        for (int32 index = 0; index < lanes.Num(); ++index)
        {
            if (LbmSameLane(lanes[index], lane))
            {
                return index;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many lanes of a are not in b
    int32 LbmLanesNotIn(TConstArrayView<FSOLLanePair> a, TConstArrayView<FSOLLanePair> b)
    {
        int32 count = 0;
        for (const FSOLLanePair& lane : a)
        {
            count += LbmFindLane(b, lane) == INDEX_NONE ? 1 : 0;
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes each current slot's traffic weight: LaneWeight of its end tiers when its lane is in the current set, 0 for
    // a free slot or a lane outside the set (the caller's rule, contract case 17)
    void LbmSlotWeights(const FSOLLaneNetworkSchedule& network, TConstArrayView<FSOLLaneNode> nodes,
        TArray<double>& outWeights)
    {
        const TConstArrayView<FSOLLanePair> slots = network.GetSlots();
        const int64 epoch = network.GetCurrentEpoch();
        outWeights.Init(0.0, slots.Num());
        for (int32 slot = 0; slot < slots.Num(); ++slot)
        {
            const FSOLLanePair& lane = slots[slot];
            if (nodes.IsValidIndex(lane.NodeA) && nodes.IsValidIndex(lane.NodeB) && network.IsInSet(epoch, lane))
            {
                outWeights[slot] = SOLLaneNetwork::LaneWeight(nodes[lane.NodeA].PopulationTier,
                    nodes[lane.NodeB].PopulationTier);
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane pair
    FSOLLanePair LbmPair(const int32 nodeA, const int32 nodeB)
    {
        FSOLLanePair pair;
        pair.NodeA = nodeA;
        pair.NodeB = nodeB;
        return pair;
    }

    //////////////////////////////////////////////////////////////////////////
    // Formats an int array as {a, b, c} for error messages
    FString LbmInts(TConstArrayView<int32> values)
    {
        FString text = TEXT("{");
        for (int32 index = 0; index < values.Num(); ++index)
        {
            text += index == 0 ? FString::Printf(TEXT("%d"), values[index])
                : FString::Printf(TEXT(", %d"), values[index]);
        }
        return text + TEXT("}");
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two int arrays are equal
    bool LbmSame(TConstArrayView<int32> actual, std::initializer_list<int32> expected)
    {
        if (actual.Num() != static_cast<int32>(expected.size()))
        {
            return false;
        }
        int32 index = 0;
        for (const int32 value : expected)
        {
            if (actual[index++] != value)
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a byte array as ints (for the comparisons)
    TArray<int32> LbmBytes(TConstArrayView<uint8> values)
    {
        TArray<int32> out;
        for (const uint8 value : values)
        {
            out.Add(value);
        }
        return out;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneBlockMoveStepTest, "SOLTest.LaneBlockMove.Step",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// StepShipBlocks: a steady step moves nothing; a lane that leaves the set releases its blocks only without CPU-tier
// ships; an opening lane takes its share from the free pool (ascending slot order when short, no top-up of a slot that
// does not open); the re-rail set holds released, taken and every block of an opening slot; the owning slots are held
bool FSOLLaneBlockMoveStepTest::RunTest(const FString& /*parameters*/)
{
    // Three slots: base lane (0,1) in slot 0, partner (2,3) in slot 1, slot 2 free; 10 blocks
    const FSOLLanePair base = LbmPair(0, 1);
    const FSOLLanePair partner = LbmPair(2, 3);
    const FSOLLanePair other = LbmPair(4, 5);
    const FSOLLanePair none;
    FSOLShipBlockStep step;

    // Steady: nothing changes, nothing moves, slots 0 and 1 own blocks
    {
        TArray<int32> map = { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 };
        const FSOLLanePair lanes[] = { base, partner, none };
        const uint8 inSet[] = { 1, 1, 0 };
        const int32 cpu[] = { 0, 0, 0 };
        const double weights[] = { 1.0, 1.0, 0.0 };
        SOLLaneNetwork::StepShipBlocks(lanes, inSet, lanes, inSet, cpu, weights, map, step);
        TestTrue(TEXT("steady: map unchanged"), LbmSame(map, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 }));
        TestTrue(TEXT("steady: nothing to re-rail"), step.MovedBlocks.IsEmpty());
        TestEqual(TEXT("steady: released"), step.Released, 0);
        TestEqual(TEXT("steady: taken"), step.Taken, 0);
        TestTrue(TEXT("steady: owners"), LbmSame(LbmBytes(step.SlotOwnsBlocks), { 1, 1, 0 }));
        TestTrue(TEXT("steady: no episode opens"), LbmSame(LbmBytes(step.SlotEpisodeOpens), { 0, 0, 0 }));
    }

    // The partner leaves with a promoted ship: its blocks stay and it stays owned (held)
    {
        TArray<int32> map = { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 };
        const FSOLLanePair lanes[] = { base, partner, none };
        const uint8 inSetBefore[] = { 1, 1, 0 };
        const uint8 inSetNow[] = { 1, 0, 0 };
        const int32 cpu[] = { 0, 2, 0 };
        const double weights[] = { 1.0, 0.0, 0.0 };
        SOLLaneNetwork::StepShipBlocks(lanes, inSetBefore, lanes, inSetNow, cpu, weights, map, step);
        TestTrue(TEXT("leave with a CPU ship: map unchanged"), LbmSame(map, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 }));
        TestTrue(TEXT("leave with a CPU ship: still owned"), LbmSame(LbmBytes(step.SlotOwnsBlocks), { 1, 1, 0 }));
        TestTrue(TEXT("leave with a CPU ship: no release"), LbmSame(LbmBytes(step.SlotRelease), { 0, 0, 0 }));

        // A later frame with the ship demoted: released into the pool, re-railed (parked), no longer owned
        const int32 cpuLater[] = { 0, 0, 0 };
        SOLLaneNetwork::StepShipBlocks(lanes, inSetNow, lanes, inSetNow, cpuLater, weights, map, step);
        TestTrue(*FString::Printf(TEXT("released: map %s"), *LbmInts(map)),
            LbmSame(map, { 0, 0, 0, 0, 0, -1, -1, -1, -1, -1 }));
        TestEqual(TEXT("released: count"), step.Released, 5);
        TestTrue(*FString::Printf(TEXT("released: re-rail %s"), *LbmInts(step.MovedBlocks)),
            LbmSame(step.MovedBlocks, { 5, 6, 7, 8, 9 }));
        TestTrue(TEXT("released: owners"), LbmSame(LbmBytes(step.SlotOwnsBlocks), { 1, 0, 0 }));
        TestTrue(TEXT("released: release flags"), LbmSame(LbmBytes(step.SlotRelease), { 0, 1, 0 }));
    }

    // A Sun-closed lane stays in the set (Q10): with no CPU-tier ship and a weight of 0 its slot still keeps its blocks
    // (release depends on set membership and CPU ships only); the control, the same slot out of the set, releases
    {
        TArray<int32> map = { 0, 0, 1, 1 };
        const FSOLLanePair lanes[] = { base, partner };
        const uint8 inSet[] = { 1, 1 };
        const int32 cpu[] = { 0, 0 };
        const double closedWeights[] = { 1.0, 0.0 };
        SOLLaneNetwork::StepShipBlocks(lanes, inSet, lanes, inSet, cpu, closedWeights, map, step);
        TestTrue(*FString::Printf(TEXT("Sun-closed: map kept %s"), *LbmInts(map)), LbmSame(map, { 0, 0, 1, 1 }));
        TestTrue(TEXT("Sun-closed: no release flag"), LbmSame(LbmBytes(step.SlotRelease), { 0, 0 }));
        TestEqual(TEXT("Sun-closed: released"), step.Released, 0);
        TestTrue(TEXT("Sun-closed: still owned"), LbmSame(LbmBytes(step.SlotOwnsBlocks), { 1, 1 }));
        TestTrue(TEXT("Sun-closed: no episode opens"), LbmSame(LbmBytes(step.SlotEpisodeOpens), { 0, 0 }));
        TestTrue(TEXT("Sun-closed: nothing to re-rail"), step.MovedBlocks.IsEmpty());

        TArray<int32> leftMap = { 0, 0, 1, 1 };
        const uint8 leftSet[] = { 1, 0 };
        SOLLaneNetwork::StepShipBlocks(lanes, inSet, lanes, leftSet, cpu, closedWeights, leftMap, step);
        TestTrue(*FString::Printf(TEXT("control, out of the set: released %s"), *LbmInts(leftMap)),
            LbmSame(leftMap, { 0, 0, -1, -1 }));
    }

    // Release and take in one step: slot 1's lane leaves (no CPU ship) and the other lane opens in slot 2; targets
    // LaneShipShares(10, {1, 0, 1}) = {5, 0, 5}; slot 2 takes the five freed blocks
    {
        TArray<int32> map = { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 };
        const FSOLLanePair before[] = { base, partner, none };
        const uint8 inSetBefore[] = { 1, 1, 0 };
        const FSOLLanePair now[] = { base, partner, other };
        const uint8 inSetNow[] = { 1, 0, 1 };
        const int32 cpu[] = { 0, 0, 0 };
        const double weights[] = { 1.0, 0.0, 1.0 };
        SOLLaneNetwork::StepShipBlocks(before, inSetBefore, now, inSetNow, cpu, weights, map, step);
        TestTrue(*FString::Printf(TEXT("release and take: map %s"), *LbmInts(map)),
            LbmSame(map, { 0, 0, 0, 0, 0, 2, 2, 2, 2, 2 }));
        TestEqual(TEXT("release and take: released"), step.Released, 5);
        TestEqual(TEXT("release and take: taken"), step.Taken, 5);
        TestEqual(TEXT("release and take: no shortfall"), step.Shortfall, 0);
        TestTrue(TEXT("release and take: each moved block once"), LbmSame(step.MovedBlocks, { 5, 6, 7, 8, 9 }));
        TestTrue(TEXT("release and take: opens"), LbmSame(LbmBytes(step.SlotEpisodeOpens), { 0, 0, 1 }));
        TestTrue(TEXT("release and take: owners"), LbmSame(LbmBytes(step.SlotOwnsBlocks), { 1, 0, 1 }));
    }

    // Two slots open together on a short pool (contract case 17, P4 b): ascending order serves slot 1 first; slot 0
    // (steady, short of its target) is not topped up (P4 c)
    {
        TArray<int32> map = { 0, -1, -1, -1 };
        const FSOLLanePair before[] = { base, none, none };
        const uint8 inSetBefore[] = { 1, 0, 0 };
        const FSOLLanePair now[] = { base, partner, other };
        const uint8 inSetNow[] = { 1, 1, 1 };
        const int32 cpu[] = { 0, 0, 0 };
        const double weights[] = { 2.0, 1.0, 1.0 };   // targets LaneShipShares(4, w) = {2, 1, 1}
        SOLLaneNetwork::StepShipBlocks(before, inSetBefore, now, inSetNow, cpu, weights, map, step);
        TestTrue(*FString::Printf(TEXT("two open: map %s"), *LbmInts(map)), LbmSame(map, { 0, 1, 2, -1 }));
        TestEqual(TEXT("two open: slot 0 not topped up, one block stays free"), map.IsValidIndex(3) ? map[3] : 99,
            static_cast<int32>(INDEX_NONE));
        TestTrue(TEXT("two open: re-rail"), LbmSame(step.MovedBlocks, { 1, 2 }));

        // Short pool: one free block, both open; slot 1 (lower) gets it, slot 2 is short by one
        TArray<int32> shortMap = { 0, 0, 0, -1 };
        const double heavy[] = { 1.0, 1.0, 1.0 };     // targets LaneShipShares(4, w) = {2, 1, 1}
        SOLLaneNetwork::StepShipBlocks(before, inSetBefore, now, inSetNow, cpu, heavy, shortMap, step);
        TestTrue(*FString::Printf(TEXT("short pool: map %s"), *LbmInts(shortMap)), LbmSame(shortMap, { 0, 0, 0, 1 }));
        TestEqual(TEXT("short pool: shortfall"), step.Shortfall, 1);
    }

    // A slot that opens again while it still owns blocks (its lane back after leaving): every block it owns is
    // re-railed (its profile is frozen again), plus the ones it takes
    {
        TArray<int32> map = { 0, 0, 1, -1, -1 };
        const FSOLLanePair lanes[] = { base, partner };
        const uint8 inSetBefore[] = { 1, 0 };
        const uint8 inSetNow[] = { 1, 1 };
        const int32 cpu[] = { 0, 0 };
        const double weights[] = { 1.0, 1.0 };        // targets LaneShipShares(5, w) = {3, 2}
        SOLLaneNetwork::StepShipBlocks(lanes, inSetBefore, lanes, inSetNow, cpu, weights, map, step);
        TestTrue(*FString::Printf(TEXT("reopen: map %s"), *LbmInts(map)), LbmSame(map, { 0, 0, 1, 1, -1 }));
        TestTrue(*FString::Printf(TEXT("reopen: re-rail %s"), *LbmInts(step.MovedBlocks)),
            LbmSame(step.MovedBlocks, { 2, 3 }));
    }

    // Short previous arrays read as a free slot outside the set (a slot past them opens if its lane is in the set)
    {
        TArray<int32> map = { -1, -1 };
        const FSOLLanePair now[] = { partner };
        const uint8 inSetNow[] = { 1 };
        const int32 cpu[] = { 0 };
        const double weights[] = { 1.0 };
        SOLLaneNetwork::StepShipBlocks(TConstArrayView<FSOLLanePair>(), TConstArrayView<uint8>(), now, inSetNow, cpu,
            weights, map, step);
        TestTrue(TEXT("no previous: the lane opens and takes every block"), LbmSame(map, { 0, 0 }));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneBlockMoveUploadRegionsTest, "SOLTest.LaneBlockMove.UploadRegions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PackRailUploadRegions: each lane-local ship range maps to its rail texels (2 x (battle + First), 2 x Count) split
// by RailTexelRegions, and each rect gets its own rows of the packed source (source x = the rect's x)
bool FSOLLaneBlockMoveUploadRegionsTest::RunTest(const FString& /*parameters*/)
{
    TArray<FSOLTexelRegion> regions;
    TArray<int32> rows;
    constexpr int32 WIDTH = 2048;

    // One block of 100 ships at lane-local 0 with no battle ships: one rect, one source row
    {
        const FSOLShipRange ranges[] = { { 0, 100 } };
        const int32 sourceRows = SOLNpcRail::PackRailUploadRegions(ranges, 0, WIDTH, regions, rows);
        TestEqual(TEXT("one block: source rows"), sourceRows, 1);
        if (TestEqual(TEXT("one block: one rect"), regions.Num(), 1) && rows.Num() == 1)
        {
            TestTrue(TEXT("one block: rect (0, 0, 200, 1)"), regions[0].X == 0 && regions[0].Y == 0
                && regions[0].Width == 200 && regions[0].Height == 1);
            TestEqual(TEXT("one block: source row 0"), rows[0], 0);
        }
    }

    // Battle ships shift the texels: 24 battle ships + lane-local 1,000 -> texel 2,048 (row 1)
    {
        const FSOLShipRange ranges[] = { { 1000, 100 } };
        SOLNpcRail::PackRailUploadRegions(ranges, 24, WIDTH, regions, rows);
        TestTrue(TEXT("battle shift: rect (0, 1, 200, 1)"), regions.Num() == 1 && regions[0].X == 0
            && regions[0].Y == 1 && regions[0].Width == 200 && regions[0].Height == 1);
    }

    // A range across a row end and a second range: three rects on three packed rows
    {
        const FSOLShipRange ranges[] = { { 1000, 50 }, { 5000, 100 } };
        const int32 sourceRows = SOLNpcRail::PackRailUploadRegions(ranges, 0, WIDTH, regions, rows);
        TestEqual(TEXT("two ranges: source rows"), sourceRows, 3);
        if (TestEqual(TEXT("two ranges: three rects"), regions.Num(), 3) && rows.Num() == 3)
        {
            TestTrue(TEXT("two ranges: head rect (2000, 0, 48, 1)"), regions[0].X == 2000 && regions[0].Y == 0
                && regions[0].Width == 48 && regions[0].Height == 1);
            TestTrue(TEXT("two ranges: tail rect (0, 1, 52, 1)"), regions[1].X == 0 && regions[1].Y == 1
                && regions[1].Width == 52 && regions[1].Height == 1);
            TestTrue(TEXT("two ranges: second range (1808, 4, 200, 1)"), regions[2].X == 1808 && regions[2].Y == 4
                && regions[2].Width == 200 && regions[2].Height == 1);
            TestTrue(TEXT("two ranges: rows 0, 1, 2"), rows[0] == 0 && rows[1] == 1 && rows[2] == 2);
        }
    }

    // Full rows keep their height in the packed source
    {
        const FSOLShipRange ranges[] = { { 0, 2048 } };
        const int32 sourceRows = SOLNpcRail::PackRailUploadRegions(ranges, 0, WIDTH, regions, rows);
        TestEqual(TEXT("full rows: two source rows"), sourceRows, 2);
        TestTrue(TEXT("full rows: one rect of height 2"), regions.Num() == 1 && regions[0].Height == 2);
    }

    // Nothing to upload
    TestEqual(TEXT("empty: no rows"), SOLNpcRail::PackRailUploadRegions(TConstArrayView<FSOLShipRange>(), 0, WIDTH,
        regions, rows), 0);
    TestTrue(TEXT("empty: no rects"), regions.IsEmpty() && rows.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneBlockMoveRebucketTest, "SOLTest.LaneBlockMove.Rebucket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RebucketLaneShips on a standalone entity manager: three blocks of 100 lane ships (groups 0, 1 and parked) move
// (block 0 to group 1, block 1 to parked, block 2 from parked to group 0): every moved ship carries its new rail and
// sits in its new group's chunks (parked in the trailing bucket groupCount), its wake key is reset and both groups'
// wake minimums are marked stale; a Mid-tier ship in a moved block is demoted to Far (tag, tier and deviation)
bool FSOLLaneBlockMoveRebucketTest::RunTest(const FString& /*parameters*/)
{
    const TSharedRef<FMassEntityManager> manager = MakeShareable(new FMassEntityManager());
    manager->Initialize(FMassEntityManagerStorageInitParams(TInPlaceType<FMassEntityManager_InitParams_Concurrent>()));
    const TSharedPtr<FMassCommandBuffer> commands = MakeShared<FMassCommandBuffer>();
    const UScriptStruct* const elements[] = {
        FSOLNpcRailFragment::StaticStruct(),
        FSOLNpcStateFragment::StaticStruct(),
        FSOLNpcDeviationFragment::StaticStruct(),
        FSOLNpcCombatFragment::StaticStruct(),
        FSOLNpcChunkWakeFragment::StaticStruct(),
    };
    const FMassArchetypeHandle archetype = manager->CreateArchetype(MakeArrayView(elements));
    constexpr int32 GROUP_COUNT = 3;
    constexpr int32 SHIPS = 300;
    const int32 startGroups[] = { 0, 1, GROUP_COUNT };

    // Lane ships in block order, each block created in its group's bucket with its old rail
    TArray<FMassEntityHandle> laneEntities;
    TArray<FSOLNpcRail> rails;
    rails.SetNum(SHIPS);
    for (int32 block = 0; block < 3; ++block)
    {
        FMassArchetypeSharedFragmentValues shared;
        shared.Add(manager->GetOrCreateConstSharedFragment(FSOLNpcGroupFragment::Make(startGroups[block])));
        shared.Add(manager->GetOrCreateConstSharedFragment(FSOLNpcClassFragment::Make(0)));
        shared.Sort();
        TArray<FMassEntityHandle> created;
        manager->BatchCreateEntities(archetype, shared, 100, created);
        laneEntities.Append(created);
    }
    for (int32 ship = 0; ship < SHIPS; ++ship)
    {
        FMassEntityView view(*manager, laneEntities[ship]);
        view.GetFragmentData<FSOLNpcStateFragment>().WakeKeyM = 1.0e9;
    }

    // Ship 5 is Mid with a deviation
    manager->AddTagToEntity(laneEntities[5], FSOLNpcMidAiTag::StaticStruct());
    {
        FMassEntityView view(*manager, laneEntities[5]);
        view.GetFragmentData<FSOLNpcStateFragment>().AiTier = static_cast<uint8>(SOLNpcScale::EAiTier::Mid);
        view.GetFragmentData<FSOLNpcDeviationFragment>().DeviationM = FVector3d(10.0, 0.0, 0.0);
    }

    // The new rails: block 0 on group 1, block 1 parked, block 2 on group 0
    const uint16 newGroups[] = { 1, SOL::NPC_RAIL_PARKED_GROUP, 0 };
    for (int32 ship = 0; ship < SHIPS; ++ship)
    {
        rails[ship].Kind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
        rails[ship].Group = newGroups[ship / 100];
        rails[ship].P[1] = static_cast<float>(ship);
    }
    TArray<int32> wakeDirty;
    wakeDirty.Init(0, GROUP_COUNT);
    constexpr int32 BATTLE = 7;
    TArray<FSOLNpcRail> globalRails;
    globalRails.SetNum(BATTLE);
    globalRails.Append(rails);
    const FSOLShipRange ranges[] = { { 0, SHIPS } };
    const SOLNpcBlockMoves::FRebucketStats stats = SOLNpcBlockMoves::RebucketLaneShips(*manager, commands,
        laneEntities, globalRails, BATTLE, ranges, GROUP_COUNT, wakeDirty);
    TestEqual(TEXT("re-railed"), stats.Rerailed, SHIPS);
    TestEqual(TEXT("re-bucketed"), stats.Rebucketed, SHIPS);
    TestEqual(TEXT("demoted"), stats.Demoted, 1);

    // Every ship: rail, bucket, wake key
    int32 wrongRail = 0;
    int32 wrongGroup = 0;
    int32 wrongWake = 0;
    int32 wrongClass = 0;
    for (int32 ship = 0; ship < SHIPS; ++ship)
    {
        const FMassEntityView view(*manager, laneEntities[ship]);
        const FSOLNpcRail& rail = view.GetFragmentData<FSOLNpcRailFragment>().Rail;
        wrongRail += FMemory::Memcmp(&rail, &rails[ship], sizeof(FSOLNpcRail)) != 0 ? 1 : 0;
        const int32 expectedGroup = SOLNpcBlockMoves::MassGroupOfRail(rails[ship], GROUP_COUNT);
        wrongGroup += view.GetConstSharedFragmentData<FSOLNpcGroupFragment>().GroupIndex != expectedGroup ? 1 : 0;
        wrongWake += view.GetFragmentData<FSOLNpcStateFragment>().WakeKeyM != 0.0 ? 1 : 0;
        wrongClass += view.GetConstSharedFragmentData<FSOLNpcClassFragment>().ClassIndex != 0 ? 1 : 0;
    }
    TestEqual(TEXT("the class fragment survives the bucket move"), wrongClass, 0);
    TestEqual(TEXT("every moved ship carries its new rail"), wrongRail, 0);
    TestEqual(TEXT("every moved ship sits in its new bucket"), wrongGroup, 0);
    TestEqual(TEXT("every moved ship's wake key is reset"), wrongWake, 0);
    TestEqual(TEXT("parked bucket is groupCount"), SOLNpcBlockMoves::MassGroupOfRail(rails[150], GROUP_COUNT),
        GROUP_COUNT);
    TestTrue(TEXT("both lane groups' wake minimums are stale"), wakeDirty[0] == 1 && wakeDirty[1] == 1);

    // The Mid ship is Far now
    {
        const FMassEntityView view(*manager, laneEntities[5]);
        TestFalse(TEXT("demoted: no Mid tag"), manager->DoesEntityHaveElement(laneEntities[5],
            FSOLNpcMidAiTag::StaticStruct()));
        TestEqual(TEXT("demoted: tier Far"), view.GetFragmentData<FSOLNpcStateFragment>().AiTier,
            static_cast<uint8>(SOLNpcScale::EAiTier::Far));
        TestEqual(TEXT("demoted: deviation zero"), view.GetFragmentData<FSOLNpcDeviationFragment>().DeviationM,
            FVector3d::ZeroVector);
    }

    // A second call with the same rails: re-railed again but no bucket change
    const SOLNpcBlockMoves::FRebucketStats again = SOLNpcBlockMoves::RebucketLaneShips(*manager, commands,
        laneEntities, globalRails, BATTLE, ranges, GROUP_COUNT, wakeDirty);
    TestEqual(TEXT("unchanged buckets: none moved"), again.Rebucketed, 0);
    TestEqual(TEXT("unchanged buckets: all re-railed"), again.Rerailed, SHIPS);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneBlockMoveLaneTableTest, "SOLTest.LaneBlockMove.LaneTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// UpdateLaneEnds (a column refreshed at a block move) changes the entries' end points and profiles in place and keeps
// each entry's slot and open flag, which SetLanes would reset. On the real network (2026-01-01, one entry per slot,
// stepped one epoch so each slotted entry's flag is refreshed) both flag values occur: base lanes open now and free
// slots closed, so a reset to either value is caught
bool FSOLLaneBlockMoveLaneTableTest::RunTest(const FString& /*parameters*/)
{
    FLbmNetworkFixture fixture;
    LbmMakeNetworkFixture(fixture, LBM_2026_S);
    const TStrongObjectPtr<USOLLaneTrafficSubsystem> table(NewObject<USOLLaneTrafficSubsystem>(GetTransientPackage()));
    table->ConfigureNetwork(fixture.Config, LbmPositionsFn(fixture));
    table->AdvanceNetwork(LBM_2026_S);
    const int32 slotCount = table->GetNetwork().GetSlots().Num();
    if (!TestTrue(TEXT("slots exist"), slotCount > 0))
    {
        table->ClearNetwork();
        return true;
    }

    // One entry per slot, slotted by index; a step refreshes their open flags
    const FSOLLaneProfile first = SOLLaneTraffic::MakeLaneProfile(4.0e6, 3.0e11, 6.0e6, 2.5e5);
    const FSOLLaneProfile second = SOLLaneTraffic::MakeLaneProfile(2.0e6, 1.0e11, 6.0e6, 2.5e5);
    TArray<int32> bodyA;
    TArray<int32> bodyB;
    TArray<int32> slots;
    TArray<FSOLLaneProfile> profiles;
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        bodyA.Add(slot);
        bodyB.Add(slot + 100);
        slots.Add(slot);
        profiles.Add(first);
    }
    table->SetLanes(bodyA, bodyB, profiles);
    table->SetLaneSlots(slots);
    table->AdvanceNetwork(LBM_2026_S + SOL::NPC_LANE_NETWORK_EPOCH_S);
    TArray<uint8> openBefore;
    int32 openCount = 0;
    for (const FSOLLaneTrafficEntry& entry : table->GetLanes())
    {
        openBefore.Add(entry.bOpenNow ? 1 : 0);
        openCount += entry.bOpenNow ? 1 : 0;
    }
    TestTrue(TEXT("precondition: some entry open"), openCount > 0);
    TestTrue(TEXT("precondition: some entry closed"), openCount < openBefore.Num());

    // New ends and profiles for every entry
    TArray<int32> newA;
    TArray<int32> newB;
    TArray<FSOLLaneProfile> newProfiles;
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        newA.Add(slot + 200);
        newB.Add(slot + 300);
        newProfiles.Add(second);
    }
    table->UpdateLaneEnds(newA, newB, newProfiles);
    const TConstArrayView<FSOLLaneTrafficEntry> lanes = table->GetLanes();
    if (TestEqual(TEXT("entry count"), lanes.Num(), slotCount))
    {
        int32 wrongEnds = 0;
        int32 wrongProfile = 0;
        int32 wrongSlot = 0;
        int32 wrongFlag = 0;
        for (int32 index = 0; index < lanes.Num(); ++index)
        {
            wrongEnds += (lanes[index].BodyA != index + 200 || lanes[index].BodyB != index + 300) ? 1 : 0;
            wrongProfile += lanes[index].Profile.WallRadiusM != second.WallRadiusM ? 1 : 0;
            wrongSlot += lanes[index].Slot != index ? 1 : 0;
            wrongFlag += (lanes[index].bOpenNow ? 1 : 0) != openBefore[index] ? 1 : 0;
        }
        TestEqual(TEXT("entries whose ends were not updated"), wrongEnds, 0);
        TestEqual(TEXT("entries whose profile was not updated"), wrongProfile, 0);
        TestEqual(TEXT("entries that lost their slot"), wrongSlot, 0);
        TestEqual(TEXT("entries whose open flag changed (open and closed both kept)"), wrongFlag, 0);
    }
    table->ClearNetwork();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneBlockMoveTimeJumpHoldsTest, "SOLTest.LaneBlockMove.TimeJumpHolds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A time jump (SDD 53 4.3, 4.5, P4 d) drops every slot hold from before it: on the real network every occupied slot is
// held at 2026-01-01, the clock jumps three years (MarkNetworkTimeJump), and the rebuilt slots hold only lanes of the
// new set, each set lane exactly one slot, no overflow, no window on a slot outside the set. The slot capacity is the
// larger of the two sets, so a surviving hold would push a new lane out. The exact re-share (AssignShipBlocks over the
// new slots' weights, from the stale map) then gives every slot its LaneShipShares block count, every weighted set
// lane blocks, no slot outside the set any, and the ships are conserved (a partial last block)
bool FSOLLaneBlockMoveTimeJumpHoldsTest::RunTest(const FString& /*parameters*/)
{
    FLbmNetworkFixture fixture;
    LbmMakeNetworkFixture(fixture, LBM_2026_S);
    const double t0S = LBM_2026_S;
    const double t1S = LBM_2026_S + 3.0 * 365.25 * LBM_DAY_S;

    // The two sets from a schedule that never holds a slot, so the test can check that some lanes left and some are new
    TArray<FSOLLanePair> set0;
    TArray<FSOLLanePair> set1;
    {
        FSOLLaneNetworkSchedule cold;
        cold.Configure(fixture.Config, LbmPositionsFn(fixture));
        cold.Advance(t0S);
        cold.GetLaneSet(cold.GetCurrentEpoch(), set0);
        cold.Advance(t1S);
        cold.GetLaneSet(cold.GetCurrentEpoch(), set1);
    }
    const int32 left = LbmLanesNotIn(set0, set1);
    const int32 arrived = LbmLanesNotIn(set1, set0);
    AddInfo(FString::Printf(TEXT("Sets: %d lanes before, %d after, %d left, %d new"), set0.Num(), set1.Num(), left,
        arrived));
    if (!TestTrue(TEXT("precondition: some lane leaves the set"), left > 0)
        || !TestTrue(TEXT("precondition: some lane joins the set"), arrived > 0))
    {
        return true;
    }

    // Every occupied slot held before the jump (each owns blocks then), and the block map over those slots
    FSOLLaneNetworkConfig config = fixture.Config;
    config.SlotCapacity = FMath::Max(set0.Num(), set1.Num());
    const TStrongObjectPtr<USOLLaneTrafficSubsystem> traffic(
        NewObject<USOLLaneTrafficSubsystem>(GetTransientPackage()));
    traffic->ConfigureNetwork(config, LbmPositionsFn(fixture));
    traffic->AdvanceNetwork(t0S);
    const FSOLLaneNetworkSchedule& network = traffic->GetNetwork();
    TestEqual(TEXT("before: no overflow"), network.GetSlotOverflow(), 0);
    constexpr int32 LANE_SHIPS = 400050;
    const int32 blockCount = SOLLaneNetwork::ShipBlockCount(LANE_SHIPS, SOL::NPC_LANE_SHIP_BLOCK_SIZE);
    TArray<double> weights;
    LbmSlotWeights(network, config.Nodes, weights);
    TArray<int32> blockSlot;
    SOLLaneNetwork::AssignShipBlocks(blockCount, weights, blockSlot);
    int32 held = 0;
    for (int32 slot = 0; slot < network.GetSlots().Num(); ++slot)
    {
        if (network.GetSlots()[slot].NodeA != INDEX_NONE)
        {
            traffic->SetNetworkSlotHeld(slot, true);
            ++held;
        }
    }
    TestEqual(TEXT("before: every set lane slotted and held"), held, set0.Num());

    // The jump
    traffic->MarkNetworkTimeJump();
    TestTrue(TEXT("the jump rebuilds"), traffic->AdvanceNetwork(t1S) == ESOLLaneNetworkAdvance::Rebuilt);
    const int64 epoch = network.GetCurrentEpoch();
    TArray<FSOLLanePair> setNow;
    network.GetLaneSet(epoch, setNow);
    TestTrue(TEXT("the jumped set is the set of that time"), setNow.Num() == set1.Num()
        && LbmLanesNotIn(setNow, set1) == 0);

    // Slots: only lanes of the new set, each set lane once, no window outside the set
    const TConstArrayView<FSOLLanePair> slots = network.GetSlots();
    TArray<int32> slotsOfLane;
    slotsOfLane.Init(0, setNow.Num());
    int32 staleSlots = 0;
    int32 staleWindows = 0;
    for (int32 slot = 0; slot < slots.Num(); ++slot)
    {
        const int32 index = slots[slot].NodeA == INDEX_NONE ? INDEX_NONE : LbmFindLane(setNow, slots[slot]);
        staleSlots += (slots[slot].NodeA != INDEX_NONE && index == INDEX_NONE) ? 1 : 0;
        double openStartS = 0.0;
        double closeS = 0.0;
        staleWindows += (index == INDEX_NONE && network.GetSlotWindow(slot, openStartS, closeS)) ? 1 : 0;
        if (index != INDEX_NONE)
        {
            ++slotsOfLane[index];
        }
    }
    int32 lanesNotOnce = 0;
    for (const int32 count : slotsOfLane)
    {
        lanesNotOnce += count != 1 ? 1 : 0;
    }
    TestEqual(TEXT("slots still holding a lane outside the new set (a hold survived the jump)"), staleSlots, 0);
    TestEqual(TEXT("slots outside the new set with a window (drawn)"), staleWindows, 0);
    TestEqual(TEXT("new set lanes without exactly one slot"), lanesNotOnce, 0);
    TestEqual(TEXT("after: no overflow"), network.GetSlotOverflow(), 0);

    // The exact re-share from the stale map
    LbmSlotWeights(network, config.Nodes, weights);
    SOLLaneNetwork::AssignShipBlocks(blockCount, weights, blockSlot);
    TArray<int32> shares;
    SOLLaneNetwork::LaneShipShares(blockCount, weights, shares);
    TArray<int32> ones;
    TArray<int32> blockShips;
    for (int32 block = 0; block < blockCount; ++block)
    {
        ones.Add(1);
        blockShips.Add(SOLLaneNetwork::ShipBlockSize(block, LANE_SHIPS, SOL::NPC_LANE_SHIP_BLOCK_SIZE));
    }
    TArray<int32> blocksOfSlot;
    TArray<int32> shipsOfSlot;
    SOLLaneNetwork::SumBlockCounts(blockSlot, ones, slots.Num(), blocksOfSlot);
    SOLLaneNetwork::SumBlockCounts(blockSlot, blockShips, slots.Num(), shipsOfSlot);
    TestEqual(TEXT("re-share: one entry per block"), blockSlot.Num(), blockCount);
    int32 wrongShare = 0;
    int32 emptyLanes = 0;
    int32 staleOwners = 0;
    int32 weightedSlots = 0;
    int64 slottedShips = 0;
    for (int32 slot = 0; slot < slots.Num(); ++slot)
    {
        wrongShare += (shares.IsValidIndex(slot) ? shares[slot] : -1) != blocksOfSlot[slot] ? 1 : 0;
        emptyLanes += (weights[slot] > 0.0 && blocksOfSlot[slot] == 0) ? 1 : 0;
        staleOwners += (weights[slot] <= 0.0 && blocksOfSlot[slot] != 0) ? 1 : 0;
        weightedSlots += weights[slot] > 0.0 ? 1 : 0;
        slottedShips += shipsOfSlot[slot];
    }
    int32 weightedSetLanes = 0;
    for (const FSOLLanePair& lane : setNow)
    {
        weightedSetLanes += SOLLaneNetwork::LaneWeight(config.Nodes[lane.NodeA].PopulationTier,
            config.Nodes[lane.NodeB].PopulationTier) > 0.0 ? 1 : 0;
    }
    int64 parkedShips = 0;
    for (int32 block = 0; block < blockSlot.Num(); ++block)
    {
        parkedShips += (blockSlot[block] == INDEX_NONE || blockSlot[block] >= slots.Num()) ? blockShips[block] : 0;
    }
    TestTrue(TEXT("re-share: precondition, the new set has weighted lanes"), weightedSetLanes > 0);
    TestEqual(TEXT("re-share: slots off their LaneShipShares count"), wrongShare, 0);
    TestEqual(TEXT("re-share: weighted lanes left without blocks"), emptyLanes, 0);
    TestEqual(TEXT("re-share: unweighted or stale slots owning blocks"), staleOwners, 0);
    TestEqual(TEXT("re-share: every weighted set lane has a weighted slot"), weightedSlots, weightedSetLanes);
    TestEqual(TEXT("re-share: slot ships + parked = lane ships"), slottedShips + parkedShips,
        static_cast<int64>(LANE_SHIPS));
    traffic->ClearNetwork();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneBlockMoveNoNetworkWindowTest, "SOLTest.LaneBlockMove.NoNetworkWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Without a lane network the lane traffic draws as before the network existed (contract case 25): the subsystem has no
// slot windows (GetLaneSlotWindows is empty), and WriteLaneWindowRow of that empty list gives every base lane column
// the full-open window (-FAR, +FAR) instead of zero, so LaneShipWindowDrawn is true for a non-parked lane ship (a
// midpoint disc and a ship on a drivable leg) and still false for a parked one; cluster and battle columns stay zero.
// The zero window is the control: nothing is drawn through it
bool FSOLLaneBlockMoveNoNetworkWindowTest::RunTest(const FString& /*parameters*/)
{
    FSOLNpcGroupTable groups;
    groups.LaneCount = 8;
    groups.ClusterCount = 10;
    groups.BattleCount = 16;
    const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    const int32 rowBase = SOL::NPC_GROUP_WINDOW_ROW * width;
    const float farS = static_cast<float>(SOL::NPC_LANE_WINDOW_FAR_S);
    const FVector4f sentinel(9.0f, 9.0f, 9.0f, 9.0f);
    const FVector4f zero(0.0f, 0.0f, 0.0f, 0.0f);
    TArray<FVector4f> texels;
    texels.Init(sentinel, width * SOL::NPC_GROUP_TEXTURE_ROWS);
    SOLNpcRail::WriteLaneWindowRow(TConstArrayView<FVector4f>(), groups, texels);

    // Row 6: full-open on the lane columns, zero elsewhere; rows 0-5 untouched
    int32 notFullOpen = 0;
    for (int32 column = 0; column < groups.LaneCount; ++column)
    {
        const FVector4f& texel = texels[rowBase + column];
        notFullOpen += (texel.X != -farS || texel.Y != farS) ? 1 : 0;
    }
    TestEqual(TEXT("lane columns without the full-open window (-FAR, +FAR)"), notFullOpen, 0);
    int32 otherNotZero = 0;
    for (int32 column = groups.LaneCount; column < width; ++column)
    {
        otherNotZero += texels[rowBase + column] != zero ? 1 : 0;
    }
    TestEqual(TEXT("cluster and battle columns not zero"), otherNotZero, 0);
    int32 rowsTouched = 0;
    for (int32 i = 0; i < rowBase; ++i)
    {
        rowsTouched += texels[i] != sentinel ? 1 : 0;
    }
    TestEqual(TEXT("rows 0-5 texels touched"), rowsTouched, 0);

    // The mirror on lane column 3's rows: a drivable lane (l0 > 0, a U-turn) and the row 6 just written
    FVector4f rows[SOL::NPC_GROUP_TEXTURE_ROWS];
    for (int32 row = 0; row < SOL::NPC_GROUP_TEXTURE_ROWS; ++row)
    {
        rows[row] = zero;
    }
    rows[4] = FVector4f(0.0f, 0.0f, 1.0e9f, 0.0f);
    rows[5] = FVector4f(0.0f, 100.0f, 0.0f, 0.0f);
    rows[SOL::NPC_GROUP_WINDOW_ROW] = texels[rowBase + 3];
    const FVector4f legA(0.0f, 1000.0f, 0.0f, 0.0f);
    const FVector4f legB(0.0f, 0.0f, 0.0f, 5000.0f);        // T_leg 5,000 s
    const FVector4f discA(0.0f, 0.0f, 0.0f, 0.0f);
    const FVector4f discB(0.0f, 0.0f, 0.0f, 0.0f);          // No leg: the midpoint disc
    FVector4f parkedA;
    FVector4f parkedB;
    SOLNpcRail::GpuParkedRailTexels(0, parkedA, parkedB);
    int32 legHidden = 0;
    int32 discHidden = 0;
    int32 parkedDrawn = 0;
    int32 zeroDrawn = 0;
    FVector4f zeroRows[SOL::NPC_GROUP_TEXTURE_ROWS];
    for (int32 row = 0; row < SOL::NPC_GROUP_TEXTURE_ROWS; ++row)
    {
        zeroRows[row] = rows[row];
    }
    zeroRows[SOL::NPC_GROUP_WINDOW_ROW] = zero;
    for (int32 step = 0; step <= 40; ++step)
    {
        const float timeS = 500.0f * step;
        legHidden += SOLNpcTrafficMirror::LaneShipWindowDrawn(legA, legB, rows, timeS) ? 0 : 1;
        discHidden += SOLNpcTrafficMirror::LaneShipWindowDrawn(discA, discB, rows, timeS) ? 0 : 1;
        parkedDrawn += SOLNpcTrafficMirror::LaneShipWindowDrawn(parkedA, parkedB, rows, timeS) ? 1 : 0;
        zeroDrawn += SOLNpcTrafficMirror::LaneShipWindowDrawn(legA, legB, zeroRows, timeS) ? 1 : 0;
        zeroDrawn += SOLNpcTrafficMirror::LaneShipWindowDrawn(discA, discB, zeroRows, timeS) ? 1 : 0;
    }
    TestEqual(TEXT("no network: samples where a ship on a leg is hidden"), legHidden, 0);
    TestEqual(TEXT("no network: samples where a midpoint disc is hidden"), discHidden, 0);
    TestEqual(TEXT("no network: samples where a parked ship is drawn"), parkedDrawn, 0);
    TestEqual(TEXT("control, zero window: samples where a lane ship is drawn"), zeroDrawn, 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
