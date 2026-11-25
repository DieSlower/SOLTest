/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLAsteroidBelt.h"
#include "Npc/SOLLaneNodeTable.h"
#include "Npc/SOLNpcScaleMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"
#include "Traffic/SOLLaneNetworkSchedule.h"
#include "Traffic/SOLLaneTrafficMath.h"
#include "Universe/SOLBodyRegistry.h"

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Part 53c tests (SDD 53 4.2-4.5, 5.3, 5.4): the 24-node table, the per-epoch lane set over a simulated year (picks,
// island links, Sun state against the pure look-back, slots and windows), the slot overflow report, time jumps and
// the measured cost of a step and a rebuild. Helpers carry names unique across the test files (unity builds)
namespace
{
    constexpr double LANESCHED_DAY_S = 86400.0;
    constexpr double LANESCHED_2026_S = 9496.5 * LANESCHED_DAY_S;   // 2026-01-01 00:00, seconds since J2000
    constexpr double LANESCHED_TEST_INF = std::numeric_limits<double>::infinity();
    constexpr int32 LANESCHED_GAME_SLOTS = SOL::NPC_GROUP_TEXTURE_WIDTH - 10 - 16;   // 64 - clusters - battles

    // The registry, node table and config of a real network at a sim time (outlives the schedule using it)
    struct FLaneSchedFixture
    {
        FSOLBodyRegistry Registry;
        SOLLaneNodeTable::FTable Table;
        FSOLLaneNetworkConfig Config;
    };

    //////////////////////////////////////////////////////////////////////////
    // Fills a fixture: the populated registry, the 24-node table at t0 and the game's network config
    void LaneSchedMakeFixture(FLaneSchedFixture& fixture, const double t0S)
    {
        fixture.Registry.PopulateSolarSystem();
        fixture.Table = SOLLaneNodeTable::Build(fixture.Registry, SOLAsteroidBelt::RealAsteroids(),
            SOLNpcScale::DefaultLayout().Lanes, t0S);
        fixture.Config.Nodes = fixture.Table.Nodes;
        fixture.Config.NodeMaxSpeedMps = fixture.Table.MaxSpeedMps;
        fixture.Config.BaseLanes = fixture.Table.BaseLanes;
        fixture.Config.RootNode = SOLLaneNodeTable::EARTH_NODE;
        fixture.Config.SlotCapacity = LANESCHED_GAME_SLOTS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the fixture's node position function
    FSOLLaneNodePositionsFn LaneSchedPositionsFn(const FLaneSchedFixture& fixture)
    {
        return [&fixture](const double timeS, TArrayView<FVector3d> outPositionsM)
        {
            SOLLaneNodeTable::PositionsAt(fixture.Table, fixture.Registry, timeS, outPositionsM);
        };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the reference clearance bound of a lane over an epoch (5 samples, the pure SDD 53 5.3 rule)
    double LaneSchedReferenceBoundM(const FLaneSchedFixture& fixture, const int64 epoch, const int32 a, const int32 b)
    {
        const int32 samples = SOL::NPC_LANE_NETWORK_EPOCH_SAMPLES;
        const double spacingS = SOL::NPC_LANE_NETWORK_EPOCH_S / (samples - 1);
        TArray<FVector3d> positions;
        positions.SetNum(fixture.Table.Nodes.Num());
        TArray<double> clearances;
        for (int32 sample = 0; sample < samples; ++sample)
        {
            SOLLaneNodeTable::PositionsAt(fixture.Table, fixture.Registry,
                epoch * SOL::NPC_LANE_NETWORK_EPOCH_S + sample * spacingS, positions);
            clearances.Add(SOLLaneNetwork::SunSegmentDistanceM(positions[a], positions[b]));
        }
        const double speed = FMath::Max(fixture.Table.MaxSpeedMps[a], fixture.Table.MaxSpeedMps[b]);
        return SOLLaneNetwork::EpochClearanceBoundM(clearances, speed, spacingS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's reference Sun state at an epoch: SunStateFromLookBack over the newest-first bounds
    ESOLLaneSunState LaneSchedReferenceSunState(const FLaneSchedFixture& fixture, const int64 epoch, const int32 a,
        const int32 b, double& outCurrentBoundM, const int32 lookBackEpochs = SOL::NPC_LANE_SUN_LOOKBACK_EPOCHS)
    {
        const double closeM = SOL::NPC_LANE_SUN_CLEARANCE_RADII * SOL::SUN_RADIUS_M;
        const double reopenM = SOL::NPC_LANE_SUN_REOPEN_RADII * SOL::SUN_RADIUS_M;
        TArray<double> bounds;
        for (int32 back = 0; back < lookBackEpochs; ++back)
        {
            const double bound = LaneSchedReferenceBoundM(fixture, epoch - back, a, b);
            bounds.Add(bound);
            if (!(bound >= closeM && bound <= reopenM))
            {
                break;
            }
        }
        outCurrentBoundM = bounds[0];
        return SOLLaneNetwork::SunStateFromLookBack(bounds, SOL::SUN_RADIUS_M);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a buffered epoch's picks against ReplayPartnerPicks from the last jump's warm-up start to that epoch
    void LaneSchedCheckReplay(FAutomationTestBase& test, const FLaneSchedFixture& fixture,
        const FSOLLaneNetworkSchedule& schedule, const int64 epoch, const TCHAR* what)
    {
        const int32 nodeCount = fixture.Table.Nodes.Num();
        const int64 start = schedule.GetWarmupStartEpoch();
        const int32 count = static_cast<int32>(epoch - start + 1);
        TArray<FVector3d> series;
        series.SetNum(count * nodeCount);
        for (int32 index = 0; index < count; ++index)
        {
            SOLLaneNodeTable::PositionsAt(fixture.Table, fixture.Registry,
                (start + index) * SOL::NPC_LANE_NETWORK_EPOCH_S, MakeArrayView(series).Slice(index * nodeCount,
                nodeCount));
        }
        TArray<FSOLPartnerPick> replayed;
        SOLLaneNetwork::ReplayPartnerPicks(fixture.Table.Nodes, series, count, fixture.Table.BaseLanes,
            SOL::NPC_LANE_PARTNER_MIN_STAY_EPOCHS, SOL::NPC_LANE_PARTNER_SWITCH_RATIO, replayed);
        const TConstArrayView<FSOLPartnerPick> buffered = schedule.GetPicks(epoch);
        if (!test.TestEqual(FString::Printf(TEXT("%s: pick count"), what), buffered.Num(), replayed.Num()))
        {
            return;
        }
        for (int32 node = 0; node < nodeCount; ++node)
        {
            if (buffered[node].Partner != replayed[node].Partner
                || buffered[node].StayEpochs != replayed[node].StayEpochs)
            {
                test.AddError(FString::Printf(TEXT("%s: node %d buffered {%d, %d} vs replay {%d, %d}"), what, node,
                    buffered[node].Partner, buffered[node].StayEpochs, replayed[node].Partner,
                    replayed[node].StayEpochs));
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two sorted lane sets are equal
    bool LaneSchedSameSet(TConstArrayView<FSOLLanePair> a, TConstArrayView<FSOLLanePair> b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (int32 index = 0; index < a.Num(); ++index)
        {
            if (a[index].NodeA != b[index].NodeA || a[index].NodeB != b[index].NodeB)
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a lane set holds a lane
    bool LaneSchedHas(TConstArrayView<FSOLLanePair> lanes, const FSOLLanePair& lane)
    {
        return lanes.ContainsByPredicate([&lane](const FSOLLanePair& other)
        {
            return other.NodeA == lane.NodeA && other.NodeB == lane.NodeB;
        });
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether every node is reachable from the root through the lanes
    bool LaneSchedConnected(const int32 nodeCount, TConstArrayView<FSOLLanePair> lanes, const int32 root)
    {
        TArray<uint8> reached;
        reached.Init(0, nodeCount);
        reached[root] = 1;
        bool bGrew = true;
        while (bGrew)
        {
            bGrew = false;
            for (const FSOLLanePair& lane : lanes)
            {
                if (reached[lane.NodeA] != reached[lane.NodeB])
                {
                    reached[lane.NodeA] = 1;
                    reached[lane.NodeB] = 1;
                    bGrew = true;
                }
            }
        }
        return !reached.Contains(0);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkNodeTableTest, "SOLTest.LaneNetworkSubsystem.NodeTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 53 4.2: 24 nodes in order, kinds, tiers (O6), region radii (O5), aphelia, the 10 largest asteroids, base lanes
// and the default layout's sqrt-tier weights
bool FSOLLaneNetworkNodeTableTest::RunTest(const FString& /*parameters*/)
{
    FLaneSchedFixture fixture;
    LaneSchedMakeFixture(fixture, LANESCHED_2026_S);
    const SOLLaneNodeTable::FTable& table = fixture.Table;
    TestTrue(TEXT("table valid"), table.bValid);
    if (!TestEqual(TEXT("24 nodes"), table.Nodes.Num(), 24))
    {
        return false;
    }

    // Registry nodes: name, kind, tier, region radius, aphelion (AU, SDD 53 table 4.2, 0.01 AU)
    struct FExpected
    {
        const TCHAR* Name;
        ESOLLaneNodeKind Kind;
        double Tier;
        double RegionM;
        double AphelionAU;
    };
    const FExpected expected[] = {
        { TEXT("Mercury"), ESOLLaneNodeKind::Planet, 2, 2e6, 0.47 },
        { TEXT("Venus"), ESOLLaneNodeKind::Planet, 2, 2e6, 0.73 },
        { TEXT("Earth"), ESOLLaneNodeKind::Planet, 10, 2e6, 1.02 },
        { TEXT("Mars"), ESOLLaneNodeKind::Planet, 6, 3e6, 1.67 },
        { TEXT("Jupiter"), ESOLLaneNodeKind::Planet, 2, 5e6, 5.45 },
        { TEXT("Saturn"), ESOLLaneNodeKind::Planet, 2, 8e6, 10.05 },
        { TEXT("Uranus"), ESOLLaneNodeKind::Planet, 1, 1e7, 20.10 },
        { TEXT("Neptune"), ESOLLaneNodeKind::Planet, 1, 1e7, 30.33 },
        { TEXT("Moon"), ESOLLaneNodeKind::Moon, 10, 2e5, -1.0 },
        { TEXT("Ceres"), ESOLLaneNodeKind::DwarfPlanet, 4, 4e6, 2.98 },
        { TEXT("Pluto"), ESOLLaneNodeKind::DwarfPlanet, 1, 1e7, 49.32 },
        { TEXT("Haumea"), ESOLLaneNodeKind::DwarfPlanet, 1, 1e7, 51.51 },
        { TEXT("Makemake"), ESOLLaneNodeKind::DwarfPlanet, 1, 1e7, 52.80 },
        { TEXT("Eris"), ESOLLaneNodeKind::DwarfPlanet, 1, 1e7, 97.58 },
    };
    for (int32 node = 0; node < UE_ARRAY_COUNT(expected); ++node)
    {
        const FExpected& row = expected[node];
        const FSOLLaneNode& actual = table.Nodes[node];
        TestTrue(FString::Printf(TEXT("node %d name %s"), node, row.Name), table.Names[node] == FName(row.Name));
        TestTrue(FString::Printf(TEXT("%s kind"), row.Name), actual.Kind == row.Kind);
        TestEqual(FString::Printf(TEXT("%s tier"), row.Name), actual.PopulationTier, row.Tier);
        TestEqual(FString::Printf(TEXT("%s tier by name"), row.Name), SOLLaneNodeTable::TierOf(FName(row.Name)),
            row.Tier);
        TestEqual(FString::Printf(TEXT("%s region radius"), row.Name), actual.RegionRadiusM, row.RegionM);
        TestTrue(FString::Printf(TEXT("%s registry body"), row.Name), actual.BodyIndex != INDEX_NONE);
        TestTrue(FString::Printf(TEXT("%s speed bound positive"), row.Name), table.MaxSpeedMps[node] > 0.0);
        if (row.AphelionAU > 0.0)
        {
            TestEqual(FString::Printf(TEXT("%s aphelion (AU)"), row.Name), actual.AphelionM / SOL::AU_M,
                row.AphelionAU, 0.01);
        }
    }
    TestEqual(TEXT("Moon parent is Earth"), table.Nodes[SOLLaneNodeTable::MOON_NODE].ParentNode,
        SOLLaneNodeTable::EARTH_NODE);
    TestEqual(TEXT("Moon aphelion about Earth (m, 1 %)"), table.Nodes[SOLLaneNodeTable::MOON_NODE].AphelionM, 4.055e8,
        4.0e6);
    TestEqual(TEXT("a non-node tier"), SOLLaneNodeTable::TierOf(FName(TEXT("Io"))), 0.0);

    // The 10 largest belt asteroids: the catalogue's first rows really are the largest, in the SDD's order
    const TArray<FSOLAsteroidDef> belt = SOLAsteroidBelt::RealAsteroids();
    const TCHAR* asteroidNames[] = { TEXT("4 Vesta"), TEXT("2 Pallas"), TEXT("10 Hygiea"), TEXT("704 Interamnia"),
        TEXT("52 Europa"), TEXT("511 Davida"), TEXT("31 Euphrosyne"), TEXT("451 Patientia"), TEXT("87 Sylvia"),
        TEXT("3 Juno") };
    const double asteroidAphelionAU[] = { 2.57, 3.41, 3.49, 3.53, 3.44, 3.76, 3.84, 3.28, 3.82, 3.35 };
    double smallestPickedM = LANESCHED_TEST_INF;
    for (int32 index = 0; index < 10; ++index)
    {
        const int32 node = SOLLaneNodeTable::FIRST_ASTEROID_NODE + index;
        const FSOLLaneNode& actual = table.Nodes[node];
        TestTrue(FString::Printf(TEXT("asteroid node %d name %s"), node, asteroidNames[index]),
            table.Names[node] == FName(asteroidNames[index]));
        TestTrue(FString::Printf(TEXT("asteroid node %d kind"), node), actual.Kind == ESOLLaneNodeKind::Asteroid);
        TestEqual(FString::Printf(TEXT("asteroid node %d source row"), node), actual.SourceIndex, index);
        TestEqual(FString::Printf(TEXT("asteroid node %d tier"), node), actual.PopulationTier, 3.0);
        TestEqual(FString::Printf(TEXT("asteroid node %d region"), node), actual.RegionRadiusM, 4.0e6);
        TestEqual(FString::Printf(TEXT("asteroid node %d aphelion (AU)"), node), actual.AphelionM / SOL::AU_M,
            asteroidAphelionAU[index], 0.01);
        smallestPickedM = FMath::Min(smallestPickedM, belt[index].RadiusM);
    }
    for (int32 index = 10; index < belt.Num(); ++index)
    {
        if (belt[index].RadiusM > smallestPickedM)
        {
            AddError(FString::Printf(TEXT("%s (row %d) is larger than a picked asteroid"),
                *belt[index].Name.ToString(), index));
        }
    }

    // Base lanes: the layout's 8 rows as sorted node pairs, slot order
    const int32 basePairs[8][2] = { { 0, 1 }, { 1, 2 }, { 2, 8 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 6, 7 } };
    if (TestEqual(TEXT("8 base lanes"), table.BaseLanes.Num(), 8))
    {
        for (int32 slot = 0; slot < 8; ++slot)
        {
            TestTrue(FString::Printf(TEXT("base slot %d pair"), slot), table.BaseLanes[slot].NodeA == basePairs[slot][0]
                && table.BaseLanes[slot].NodeB == basePairs[slot][1]);
        }
    }

    // The default layout's lane weights are sqrt(tier A x tier B) (SDD 53 4.4)
    const SOLNpcScale::FLayout layout = SOLNpcScale::DefaultLayout();
    const double weights[8] = { 2.0, FMath::Sqrt(20.0), 10.0, FMath::Sqrt(60.0), FMath::Sqrt(12.0), 2.0,
        FMath::Sqrt(2.0), 1.0 };
    for (int32 lane = 0; lane < 8 && lane < layout.Lanes.Num(); ++lane)
    {
        TestEqual(FString::Printf(TEXT("layout lane %d weight"), lane), layout.Lanes[lane].Weight, weights[lane],
            1.0e-12);
    }

    // Positions: the registry chain equals the registry's own update; asteroids are in the belt
    FSOLBodyRegistry updated;
    updated.PopulateSolarSystem();
    updated.Update(LANESCHED_2026_S);
    TArray<FVector3d> positions;
    positions.SetNum(24);
    SOLLaneNodeTable::PositionsAt(table, fixture.Registry, LANESCHED_2026_S, positions);
    for (int32 node = 0; node < SOLLaneNodeTable::REGISTRY_NODE_COUNT; ++node)
    {
        TestTrue(FString::Printf(TEXT("node %d position equals the registry update"), node),
            positions[node] == updated.GetPositionM(table.Nodes[node].BodyIndex));
    }

    // The asteroid-only path (the NPC lane end points, 53c2a): the same asteroid positions, registry entries untouched
    const FVector3d sentinelM(-1.0, -2.0, -3.0);
    TArray<FVector3d> asteroidPositions;
    asteroidPositions.Init(sentinelM, 24);
    SOLLaneNodeTable::AsteroidPositionsAt(table, LANESCHED_2026_S, asteroidPositions);
    for (int32 node = 0; node < 24; ++node)
    {
        const bool bAsteroid = node >= SOLLaneNodeTable::FIRST_ASTEROID_NODE;
        TestTrue(FString::Printf(TEXT("asteroid-only node %d: %s"), node, bAsteroid ? TEXT("equals PositionsAt")
            : TEXT("left untouched")), asteroidPositions[node] == (bAsteroid ? positions[node] : sentinelM));
    }

    // The position-only Kepler path equals ElementsToState's position, for every registry orbit over two centuries
    for (int32 body = 0; body < fixture.Registry.Num(); ++body)
    {
        if (fixture.Registry.GetParent(body) == INDEX_NONE)
        {
            continue;
        }
        for (int32 step = 0; step < 8; ++step)
        {
            const FSOLKeplerElements elements = fixture.Registry.GetElements(body).AtCenturies(-1.0 + 0.25 * step);
            const double gm = fixture.Registry.GetOrbitGM(body);
            const FVector3d stateM = SOLKepler::ElementsToState(elements, gm, 0.0).PositionM;
            if (SOLKepler::ElementsToPositionM(elements, gm, 0.0) != stateM)
            {
                AddError(FString::Printf(TEXT("body %d step %d: position-only Kepler differs"), body, step));
            }
        }
    }
    for (int32 node = SOLLaneNodeTable::FIRST_ASTEROID_NODE; node < 24; ++node)
    {
        const double rAU = positions[node].Length() / SOL::AU_M;
        TestTrue(FString::Printf(TEXT("asteroid node %d in the belt (%.2f AU)"), node, rAU), rAU > 1.7 && rAU < 4.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkDailySetTest, "SOLTest.LaneNetworkSubsystem.DailySetYear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A simulated year at one-day steps from 2026-01-01: picks equal the replay from the warm-up start, the set holds the
// base lanes, every node joins Earth's group, the Sun state equals the pure look-back, slots stay put, windows equal
// LaneOpenWindow with the per-slot latch (-inf after the jump)
bool FSOLLaneNetworkDailySetTest::RunTest(const FString& /*parameters*/)
{
    FLaneSchedFixture fixture;
    LaneSchedMakeFixture(fixture, LANESCHED_2026_S);
    FSOLLaneNetworkSchedule schedule;
    schedule.Configure(fixture.Config, LaneSchedPositionsFn(fixture));
    if (!TestTrue(TEXT("configured"), schedule.IsConfigured()))
    {
        return false;
    }
    TestTrue(TEXT("first advance rebuilds"),
        schedule.Advance(LANESCHED_2026_S + 1.0) == ESOLLaneNetworkAdvance::Rebuilt);
    const int64 k0 = SOLLaneNetwork::LaneNetworkEpochIndex(LANESCHED_2026_S, SOL::NPC_LANE_NETWORK_EPOCH_S);
    TestEqual(TEXT("current epoch"), schedule.GetCurrentEpoch(), k0);
    TestEqual(TEXT("warm-up start"), schedule.GetWarmupStartEpoch(), k0 - SOL::NPC_LANE_PARTNER_WARMUP_EPOCHS);
    // 2026-01-01 00:00 is mid-epoch (epochs start at J2000 noon), so stay inside it: k0 ends 43,200 s on
    TestTrue(TEXT("same epoch: nothing"), schedule.Advance(LANESCHED_2026_S + 43000.0) == ESOLLaneNetworkAdvance::None);
    LaneSchedCheckReplay(*this, fixture, schedule, k0, TEXT("jump epoch"));
    LaneSchedCheckReplay(*this, fixture, schedule, k0 + SOL::NPC_LANE_NETWORK_HORIZON_EPOCHS - 1, TEXT("horizon end"));

    // Expected open start per pair: -inf for lanes open at the jump, else the epoch the open run began
    TMap<uint64, double> expectedStart;
    auto key = [](const FSOLLanePair& lane) { return (uint64(uint32(lane.NodeA)) << 32) | uint32(lane.NodeB); };
    TArray<FSOLLanePair> previousSlots;
    TArray<FSOLLanePair> set;
    TArray<uint8> flags;
    int32 minLanes = MAX_int32;
    int32 maxLanes = 0;
    int32 sunClosedDays = 0;
    int32 setChanges = 0;
    TArray<FSOLLanePair> previousSet;
    const double closeM = SOL::NPC_LANE_SUN_CLEARANCE_RADII * SOL::SUN_RADIUS_M;
    const double cheapMarginM = 25.0 * SOL::SUN_RADIUS_M;
    for (int32 day = 0; day <= 365; ++day)
    {
        const int64 k = k0 + day;
        if (day > 0)
        {
            TestTrue(FString::Printf(TEXT("day %d steps"), day),
                schedule.Advance(k * SOL::NPC_LANE_NETWORK_EPOCH_S + 10.0) == ESOLLaneNetworkAdvance::Stepped);
        }
        schedule.GetLaneSet(k, set);
        minLanes = FMath::Min(minLanes, set.Num());
        maxLanes = FMath::Max(maxLanes, set.Num());
        setChanges += (day > 0 && !LaneSchedSameSet(set, previousSet)) ? 1 : 0;
        previousSet = set;
        for (const FSOLLanePair& base : fixture.Table.BaseLanes)
        {
            TestTrue(FString::Printf(TEXT("day %d base lane in set"), day), schedule.IsInSet(k, base));
        }
        TestTrue(FString::Printf(TEXT("day %d every node joins Earth"), day),
            LaneSchedConnected(24, set, SOLLaneNodeTable::EARTH_NODE));
        TestTrue(FString::Printf(TEXT("day %d lanes within the bound of 38"), day), set.Num() <= 38);

        // Sun state of every lane against the pure look-back (lanes surely far from the band skip the reference)
        TArray<FVector3d> positions;
        positions.SetNum(24);
        SOLLaneNodeTable::PositionsAt(fixture.Table, fixture.Registry, k * SOL::NPC_LANE_NETWORK_EPOCH_S, positions);
        for (const FSOLLanePair& lane : set)
        {
            const double speed = FMath::Max(fixture.Table.MaxSpeedMps[lane.NodeA],
                fixture.Table.MaxSpeedMps[lane.NodeB]);
            const double d0 = SOLLaneNetwork::SunSegmentDistanceM(positions[lane.NodeA], positions[lane.NodeB]);
            bool bReferenceOpen = true;
            if (d0 - speed * 1.5 * SOL::NPC_LANE_NETWORK_EPOCH_S < cheapMarginM)
            {
                double bound = 0.0;
                bReferenceOpen = LaneSchedReferenceSunState(fixture, k, lane.NodeA, lane.NodeB, bound)
                    == ESOLLaneSunState::Open;
            }
            sunClosedDays += bReferenceOpen ? 0 : 1;
            if (schedule.IsOpen(k, lane) != bReferenceOpen)
            {
                AddError(FString::Printf(TEXT("day %d lane %d-%d: open %d, look-back says %d"), day, lane.NodeA,
                    lane.NodeB, schedule.IsOpen(k, lane) ? 1 : 0, bReferenceOpen ? 1 : 0));
            }
        }

        // Slots: base slots fixed, no overflow, every non-base lane in exactly one slot, kept while in the set
        const TConstArrayView<FSOLLanePair> slots = schedule.GetSlots();
        TestEqual(FString::Printf(TEXT("day %d no overflow"), day), schedule.GetSlotOverflow(), 0);
        for (int32 slot = 0; slot < 8; ++slot)
        {
            TestTrue(FString::Printf(TEXT("day %d base slot %d"), day, slot),
                slots[slot].NodeA == fixture.Table.BaseLanes[slot].NodeA
                && slots[slot].NodeB == fixture.Table.BaseLanes[slot].NodeB);
        }
        for (const FSOLLanePair& lane : set)
        {
            int32 holders = 0;
            for (const FSOLLanePair& slotLane : slots)
            {
                holders += (slotLane.NodeA == lane.NodeA && slotLane.NodeB == lane.NodeB) ? 1 : 0;
            }
            TestEqual(FString::Printf(TEXT("day %d lane %d-%d holds one slot"), day, lane.NodeA, lane.NodeB),
                holders, 1);
        }
        for (int32 slot = 8; slot < previousSlots.Num(); ++slot)
        {
            const FSOLLanePair& lane = previousSlots[slot];
            if (lane.NodeA != INDEX_NONE && schedule.IsInSet(k, lane))
            {
                TestTrue(FString::Printf(TEXT("day %d lane %d-%d keeps slot %d"), day, lane.NodeA, lane.NodeB, slot),
                    slots[slot].NodeA == lane.NodeA && slots[slot].NodeB == lane.NodeB);
            }
        }

        // Windows: the close from LaneOpenWindow on the lane's flags, the start the latch (-inf at the jump, else the
        // epoch the lane's open run began; a lane not open today forgets its start)
        TMap<uint64, double> todayStart;
        for (int32 slot = 0; slot < slots.Num(); ++slot)
        {
            const FSOLLanePair& lane = slots[slot];
            double startS = 0.0;
            double closeS = 0.0;
            const bool bOpen = schedule.GetSlotWindow(slot, startS, closeS);
            const bool bExpectedOpen = lane.NodeA != INDEX_NONE && schedule.IsOpen(k, lane);
            TestEqual(FString::Printf(TEXT("day %d slot %d open"), day, slot), bOpen, bExpectedOpen);
            if (!bExpectedOpen)
            {
                continue;
            }
            const double* heldStart = expectedStart.Find(key(lane));
            const double expectedStartS = heldStart != nullptr ? *heldStart
                : (day == 0 ? -LANESCHED_TEST_INF : k * SOL::NPC_LANE_NETWORK_EPOCH_S);
            todayStart.Add(key(lane), expectedStartS);
            schedule.GetOpenFlags(lane, flags);
            double referenceStartS = 0.0;
            double referenceCloseS = 0.0;
            SOLLaneNetwork::LaneOpenWindow(flags, k, SOL::NPC_LANE_NETWORK_EPOCH_S, k * SOL::NPC_LANE_NETWORK_EPOCH_S,
                expectedStartS, referenceStartS, referenceCloseS);
            if (startS != expectedStartS || startS != referenceStartS || closeS != referenceCloseS)
            {
                AddError(FString::Printf(
                    TEXT("day %d slot %d lane %d-%d: window [%.17g, %.17g], expected [%.17g, %.17g]"),
                    day, slot, lane.NodeA, lane.NodeB, startS, closeS, referenceStartS, referenceCloseS));
            }
        }
        expectedStart = MoveTemp(todayStart);
        previousSlots = TArray<FSOLLanePair>(slots);
    }
    LaneSchedCheckReplay(*this, fixture, schedule, k0 + 365, TEXT("after a year: current"));
    LaneSchedCheckReplay(*this, fixture, schedule, k0 + 365 + SOL::NPC_LANE_NETWORK_HORIZON_EPOCHS - 1,
        TEXT("after a year: horizon end"));
    TestTrue(TEXT("a Sun closure was met during the year (coverage)"), sunClosedDays > 0);
    TestTrue(TEXT("at least 8 lanes"), minLanes >= 8);
    AddInfo(FString::Printf(TEXT("Year from 2026-01-01: lanes %d-%d, %d set changes, %d lane-days Sun-closed"),
        minLanes, maxLanes, setChanges, sunClosedDays));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkOverflowTest, "SOLTest.LaneNetworkSubsystem.SlotOverflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Q28: with too few columns the lanes that find none are counted and reported once per change
bool FSOLLaneNetworkOverflowTest::RunTest(const FString& /*parameters*/)
{
    AddExpectedError(TEXT("found no group column"), EAutomationExpectedErrorFlags::Contains, 0);
    FLaneSchedFixture fixture;
    LaneSchedMakeFixture(fixture, LANESCHED_2026_S);
    fixture.Config.SlotCapacity = 9;
    FSOLLaneNetworkSchedule schedule;
    schedule.Configure(fixture.Config, LaneSchedPositionsFn(fixture));
    schedule.Advance(LANESCHED_2026_S);
    TArray<FSOLLanePair> set;
    schedule.GetLaneSet(schedule.GetCurrentEpoch(), set);
    TestEqual(TEXT("overflow = non-base lanes minus the one free column"), schedule.GetSlotOverflow(),
        set.Num() - 8 - 1);
    TestEqual(TEXT("reported once"), schedule.GetSlotOverflowReports(), 1);
    TestEqual(TEXT("9 slots"), schedule.GetSlots().Num(), 9);
    TestTrue(TEXT("the free column is taken"), schedule.GetSlots()[8].NodeA != INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkJumpTest, "SOLTest.LaneNetworkSubsystem.TimeJump",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 53 4.3, 5.3: a forward move of up to 16 epochs steps, more or backward (or a marked jump) rebuilds with the
// warm-up from k - 64; after a rebuild lanes open now latch -inf (5.4, the user's decision)
bool FSOLLaneNetworkJumpTest::RunTest(const FString& /*parameters*/)
{
    FLaneSchedFixture fixture;
    LaneSchedMakeFixture(fixture, LANESCHED_2026_S);
    FSOLLaneNetworkSchedule schedule;
    TestTrue(TEXT("unconfigured: nothing"), schedule.Advance(LANESCHED_2026_S) == ESOLLaneNetworkAdvance::None);
    schedule.Configure(fixture.Config, LaneSchedPositionsFn(fixture));
    const double e = SOL::NPC_LANE_NETWORK_EPOCH_S;
    const int64 k0 = SOLLaneNetwork::LaneNetworkEpochIndex(LANESCHED_2026_S, e);
    TestTrue(TEXT("first: rebuild"), schedule.Advance(LANESCHED_2026_S) == ESOLLaneNetworkAdvance::Rebuilt);
    TestTrue(TEXT("+16 epochs: stepped"), schedule.Advance((k0 + 16) * e) == ESOLLaneNetworkAdvance::Stepped);
    TestEqual(TEXT("+16 epochs: warm-up start unchanged"), schedule.GetWarmupStartEpoch(), k0 - 64);
    TestTrue(TEXT("+17 epochs: rebuild"), schedule.Advance((k0 + 33) * e) == ESOLLaneNetworkAdvance::Rebuilt);
    TestEqual(TEXT("+17: warm-up from k - 64"), schedule.GetWarmupStartEpoch(), k0 + 33 - 64);
    LaneSchedCheckReplay(*this, fixture, schedule, k0 + 40, TEXT("after the forward jump"));
    TestTrue(TEXT("backward: rebuild"), schedule.Advance((k0 + 32) * e) == ESOLLaneNetworkAdvance::Rebuilt);
    TestEqual(TEXT("backward: current epoch"), schedule.GetCurrentEpoch(), k0 + 32);
    schedule.MarkTimeJump();
    TestTrue(TEXT("marked jump one epoch on: rebuild"),
        schedule.Advance((k0 + 33) * e) == ESOLLaneNetworkAdvance::Rebuilt);
    const TConstArrayView<FSOLLanePair> slots = schedule.GetSlots();
    int32 open = 0;
    for (int32 slot = 0; slot < slots.Num(); ++slot)
    {
        double startS = 0.0;
        double closeS = 0.0;
        if (schedule.GetSlotWindow(slot, startS, closeS))
        {
            ++open;
            TestTrue(FString::Printf(TEXT("slot %d latch after the jump is -inf"), slot),
                startS == -LANESCHED_TEST_INF);
            TestTrue(FString::Printf(TEXT("slot %d close after now"), slot), closeS > (k0 + 33) * e);
        }
    }
    TestTrue(TEXT("lanes open after the jump"), open > 8);

    // A very large jump (a century on) still rebuilds into a valid set
    TestTrue(TEXT("century: rebuild"),
        schedule.Advance(LANESCHED_2026_S + 36525.0 * e) == ESOLLaneNetworkAdvance::Rebuilt);
    TArray<FSOLLanePair> set;
    schedule.GetLaneSet(schedule.GetCurrentEpoch(), set);
    TestTrue(TEXT("century: set connected"), LaneSchedConnected(24, set, SOLLaneNodeTable::EARTH_NODE));
    schedule.Reset();
    TestFalse(TEXT("reset: not configured"), schedule.IsConfigured());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkRouteTest, "SOLTest.LaneNetworkSubsystem.RouteQuery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 53 6.3 at the subsystem level: routes over the slot lanes open now, hops naming slots, consecutive nodes; the
// horizon rule (a window open through the horizon end plans as open); every node reachable in time even with the worst
// transfers is routed to, and none that is unreachable even with free transfers
bool FSOLLaneNetworkRouteTest::RunTest(const FString& /*parameters*/)
{
    FLaneSchedFixture fixture;
    LaneSchedMakeFixture(fixture, LANESCHED_2026_S);
    FSOLLaneNetworkSchedule schedule;
    schedule.Configure(fixture.Config, LaneSchedPositionsFn(fixture));
    schedule.Advance(LANESCHED_2026_S);
    const TConstArrayView<FSOLLanePair> slots = schedule.GetSlots();

    // Profiles per slot from the lane's nominal length and end radii (a test wall radius of 1e5 m)
    TArray<FSOLLaneProfile> profiles;
    profiles.SetNum(slots.Num());
    for (int32 slot = 0; slot < slots.Num(); ++slot)
    {
        const FSOLLanePair& lane = slots[slot];
        if (lane.NodeA != INDEX_NONE)
        {
            profiles[slot] = SOLLaneTraffic::MakeLaneProfile(1.0e5,
                SOLLaneNetwork::LaneNominalLengthM(fixture.Table.Nodes, lane.NodeA, lane.NodeB),
                fixture.Table.Nodes[lane.NodeA].ClearanceRadiusM, fixture.Table.Nodes[lane.NodeB].ClearanceRadiusM);
        }
    }
    const double startS = LANESCHED_2026_S + 3600.0;
    const FSOLLaneRoute moon = schedule.FindRoute(SOLLaneNodeTable::EARTH_NODE, SOLLaneNodeTable::MOON_NODE, startS,
        profiles, 3.0e5, SOL::NPC_LANE_TRANSFER_PENALTY_S);
    TestTrue(TEXT("Earth -> Moon found"), moon.bFound);
    if (moon.bFound && TestEqual(TEXT("Earth -> Moon one hop"), moon.HopCount, 1))
    {
        TestEqual(TEXT("Earth -> Moon over base slot 2"), moon.Hops[0].Lane, 2);
    }

    // Earliest arrivals from Earth over the open drivable slots, windows checked as the router does (a close at the
    // horizon end plans as open, the horizon rule): with no transfer cost (optimistic, a superset of what the router
    // finds) and with every transfer at its largest (the penalty plus pi + 1 end-ring distances at the wall speed,
    // pessimistic, a subset). All windows are open now, so an earlier arrival is never worse and both searches are
    // exact
    const double horizonEndS = schedule.GetHorizonEndS();
    const double wallSpeedMps = 3.0e5;
    double maxEndRingM = 0.0;
    for (const FSOLLaneProfile& profile : profiles)
    {
        maxEndRingM = FMath::Max3(maxEndRingM, profile.EndArcAM, profile.EndArcBM);
    }
    const double worstTransferS = SOL::NPC_LANE_TRANSFER_PENALTY_S
        + (UE_DOUBLE_PI + 1.0) * maxEndRingM / wallSpeedMps;
    const auto reachable = [&](const double transferS, TArray<uint8>& outReached)
    {
        TArray<double> arrivalS;
        arrivalS.Init(LANESCHED_TEST_INF, 24);
        arrivalS[SOLLaneNodeTable::EARTH_NODE] = startS;
        for (bool bImproved = true; bImproved;)
        {
            bImproved = false;
            for (int32 slot = 0; slot < slots.Num(); ++slot)
            {
                double openS = 0.0;
                double closeS = 0.0;
                if (!profiles[slot].bDrivable || !schedule.GetSlotWindow(slot, openS, closeS))
                {
                    continue;
                }
                const double legS = SOLLaneNetwork::LaneEdgeTimeS(profiles[slot]);
                const double plannedCloseS = closeS >= horizonEndS ? LANESCHED_TEST_INF : closeS;
                for (int32 end = 0; end < 2; ++end)
                {
                    const int32 from = end == 0 ? slots[slot].NodeA : slots[slot].NodeB;
                    const int32 to = end == 0 ? slots[slot].NodeB : slots[slot].NodeA;
                    const double departS = arrivalS[from]
                        + (from == SOLLaneNodeTable::EARTH_NODE ? 0.0 : transferS);
                    const bool bFits = departS >= horizonEndS
                        || SOLLaneNetwork::LegFitsWindow(departS, legS, openS, plannedCloseS);
                    if (FMath::IsFinite(departS) && bFits && departS + legS < arrivalS[to])
                    {
                        arrivalS[to] = departS + legS;
                        bImproved = true;
                    }
                }
            }
        }
        outReached.Init(0, 24);
        for (int32 node = 0; node < 24; ++node)
        {
            outReached[node] = FMath::IsFinite(arrivalS[node]) ? 1 : 0;
        }
    };
    TArray<uint8> optimistic;
    TArray<uint8> pessimistic;
    reachable(0.0, optimistic);
    reachable(worstTransferS, pessimistic);

    // Every route found walks consecutive nodes from the source to the target over open slots
    int32 found = 0;
    int32 optimisticCount = 0;
    int32 pessimisticCount = 0;
    for (int32 target = 0; target < 24; ++target)
    {
        if (target == SOLLaneNodeTable::EARTH_NODE)
        {
            continue;
        }
        optimisticCount += optimistic[target];
        pessimisticCount += pessimistic[target];
        const FSOLLaneRoute route = schedule.FindRoute(SOLLaneNodeTable::EARTH_NODE, target, startS, profiles,
            wallSpeedMps, SOL::NPC_LANE_TRANSFER_PENALTY_S);
        if (pessimistic[target] != 0)
        {
            TestTrue(FString::Printf(TEXT("target %d reachable even with the worst transfers is routed to"), target),
                route.bFound);
        }
        if (optimistic[target] == 0)
        {
            TestFalse(FString::Printf(TEXT("target %d unreachable even with free transfers has no route"), target),
                route.bFound);
        }
        if (!route.bFound)
        {
            continue;
        }
        ++found;
        int32 at = SOLLaneNodeTable::EARTH_NODE;
        for (int32 hop = 0; hop < route.HopCount; ++hop)
        {
            const FSOLLanePair& lane = slots[route.Hops[hop].Lane];
            double openS = 0.0;
            double closeS = 0.0;
            TestTrue(FString::Printf(TEXT("target %d hop %d on an open slot"), target, hop),
                schedule.GetSlotWindow(route.Hops[hop].Lane, openS, closeS));
            const int32 from = route.Hops[hop].DirectionSign > 0 ? lane.NodeA : lane.NodeB;
            TestEqual(FString::Printf(TEXT("target %d hop %d starts where the last ended"), target, hop), from, at);
            at = route.Hops[hop].DirectionSign > 0 ? lane.NodeB : lane.NodeA;
        }
        TestEqual(FString::Printf(TEXT("target %d reached"), target), at, target);
    }
    AddInfo(FString::Printf(TEXT("Routes from Earth on 2026-01-01 (horizon rule): %d of 23 nodes found; %d reachable ")
        TEXT("in time with the worst transfers, %d with free transfers"), found, pessimisticCount, optimisticCount));
    TestTrue(TEXT("found covers every node reachable with the worst transfers"), found >= pessimisticCount);
    TestTrue(TEXT("found stays inside the nodes reachable with free transfers"), found <= optimisticCount);

    // The router's plan-open time: a leg that misses its window is taken only when it departs at or after it
    TArray<FSOLLaneEdge> edges;
    FSOLLaneEdge& edge = edges.AddDefaulted_GetRef();
    edge.NodeA = 0;
    edge.NodeB = 1;
    edge.TimeS = 10.0;
    edge.OpenStartS = -LANESCHED_TEST_INF;
    edge.CloseS = 1005.0;
    edge.DirectionAB = FVector3d::UnitX();
    TestFalse(TEXT("window too short: no route"),
        SOLLaneNetwork::FindFastestRoute(2, edges, 0.0, wallSpeedMps, 1000.0, 0, 1).bFound);
    edges[0].PlanOpenFromS = 1000.0;
    TestTrue(TEXT("departing at the plan-open time: routed"),
        SOLLaneNetwork::FindFastestRoute(2, edges, 0.0, wallSpeedMps, 1000.0, 0, 1).bFound);
    edges[0].PlanOpenFromS = 1001.0;
    TestFalse(TEXT("departing before the plan-open time: no route"),
        SOLLaneNetwork::FindFastestRoute(2, edges, 0.0, wallSpeedMps, 1000.0, 0, 1).bFound);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkSteppedStretchTest, "SOLTest.LaneNetworkSubsystem.SteppedStretch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// StepOne-built entries over 1,100 days (longer than the 960-epoch horizon): each day's set equals base + pick pairs
// + IslandLinks (the plain functions), a lane entering the set inside the Sun band gets the pure look-back state, and
// every 100 days a fresh rebuild at the same epoch agrees on the Sun state of every lane both sets hold across the
// whole buffer; the picks still equal the replay from the first warm-up after the stretch. A second schedule with an
// 8-epoch look-back is checked against the reference with that look-back
bool FSOLLaneNetworkSteppedStretchTest::RunTest(const FString& /*parameters*/)
{
    FLaneSchedFixture fixture;
    LaneSchedMakeFixture(fixture, LANESCHED_2026_S);
    FSOLLaneNetworkSchedule schedule;
    schedule.Configure(fixture.Config, LaneSchedPositionsFn(fixture));
    FSOLLaneNetworkConfig shortConfig = fixture.Config;
    shortConfig.LookBackEpochs = 8;
    FSOLLaneNetworkSchedule shortSchedule;
    shortSchedule.Configure(shortConfig, LaneSchedPositionsFn(fixture));
    const double e = SOL::NPC_LANE_NETWORK_EPOCH_S;
    const int32 horizon = SOL::NPC_LANE_NETWORK_HORIZON_EPOCHS;
    const int64 k0 = SOLLaneNetwork::LaneNetworkEpochIndex(LANESCHED_2026_S, e);
    schedule.Advance(LANESCHED_2026_S);
    shortSchedule.Advance(LANESCHED_2026_S);
    const double closeM = SOL::NPC_LANE_SUN_CLEARANCE_RADII * SOL::SUN_RADIUS_M;
    const double reopenM = SOL::NPC_LANE_SUN_REOPEN_RADII * SOL::SUN_RADIUS_M;
    constexpr int32 days = 1100;
    TArray<FSOLLanePair> set;
    TArray<FSOLLanePair> farSet;
    TArray<FSOLLanePair> beforeFarSet;
    TArray<FSOLLanePair> expected;
    TArray<FSOLLanePair> pairs;
    TArray<FSOLLanePair> links;
    TArray<FSOLLanePair> rebuiltSet;
    TArray<int32> partners;
    TArray<FVector3d> positions;
    positions.SetNum(24);
    partners.SetNum(24);
    int32 newInBand = 0;
    int32 comparedLanes = 0;
    int32 shortChecked = 0;
    for (int32 day = 1; day <= days; ++day)
    {
        const int64 k = k0 + day;
        if (!TestTrue(FString::Printf(TEXT("day %d steps"), day),
            schedule.Advance(k * e + 10.0) == ESOLLaneNetworkAdvance::Stepped))
        {
            return false;
        }

        // The current set from the plain functions over the buffered picks
        SOLLaneNodeTable::PositionsAt(fixture.Table, fixture.Registry, k * e, positions);
        const TConstArrayView<FSOLPartnerPick> picks = schedule.GetPicks(k);
        for (int32 node = 0; node < 24; ++node)
        {
            partners[node] = picks[node].Partner;
        }
        SOLLaneNetwork::PairsFromPicks(partners, pairs);
        expected = fixture.Table.BaseLanes;
        expected.Append(pairs);
        SOLLaneNetwork::IslandLinks(fixture.Table.Nodes, positions, expected, SOLLaneNodeTable::EARTH_NODE, links);
        expected.Append(links);
        expected.Sort([](const FSOLLanePair& a, const FSOLLanePair& b)
        {
            return a.NodeA != b.NodeA ? a.NodeA < b.NodeA : a.NodeB < b.NodeB;
        });
        schedule.GetLaneSet(k, set);
        if (!LaneSchedSameSet(set, expected))
        {
            AddError(FString::Printf(TEXT("day %d: set of %d lanes differs from base + picks + IslandLinks (%d)"), day,
                set.Num(), expected.Num()));
        }

        // The new far-end entry (built by StepOne): a lane new to the set inside the band takes the look-back state
        const int64 far = k + horizon - 1;
        schedule.GetLaneSet(far, farSet);
        schedule.GetLaneSet(far - 1, beforeFarSet);
        for (const FSOLLanePair& lane : farSet)
        {
            if (LaneSchedHas(beforeFarSet, lane))
            {
                continue;
            }
            const double boundM = LaneSchedReferenceBoundM(fixture, far, lane.NodeA, lane.NodeB);
            if (boundM < closeM || boundM > reopenM)
            {
                continue;
            }
            ++newInBand;
            double currentBoundM = 0.0;
            const bool bReferenceOpen = LaneSchedReferenceSunState(fixture, far, lane.NodeA, lane.NodeB, currentBoundM)
                == ESOLLaneSunState::Open;
            TestEqual(FString::Printf(TEXT("day %d far end: lane %d-%d new in the band"), day, lane.NodeA, lane.NodeB),
                schedule.IsOpen(far, lane), bReferenceOpen);
        }

        // The 8-epoch look-back schedule over the first 400 days, lanes near the band against the reference
        if (day <= 400)
        {
            shortSchedule.Advance(k * e + 10.0);
            shortSchedule.GetLaneSet(k, set);
            for (const FSOLLanePair& lane : set)
            {
                const double d0 = SOLLaneNetwork::SunSegmentDistanceM(positions[lane.NodeA], positions[lane.NodeB]);
                if (d0 > 40.0 * SOL::SUN_RADIUS_M)
                {
                    continue;
                }
                ++shortChecked;
                double currentBoundM = 0.0;
                const bool bReferenceOpen = LaneSchedReferenceSunState(fixture, k, lane.NodeA, lane.NodeB,
                    currentBoundM, 8) == ESOLLaneSunState::Open;
                TestEqual(FString::Printf(TEXT("day %d look-back 8: lane %d-%d"), day, lane.NodeA, lane.NodeB),
                    shortSchedule.IsOpen(k, lane), bReferenceOpen);
            }
        }

        // Every 100 days: a fresh rebuild at the same epoch holds the same Sun state for the lanes both sets hold
        if (day % 100 == 0)
        {
            FSOLLaneNetworkSchedule rebuilt;
            rebuilt.Configure(fixture.Config, LaneSchedPositionsFn(fixture));
            rebuilt.Advance(k * e + 10.0);
            for (int64 epoch = k; epoch < k + horizon; ++epoch)
            {
                schedule.GetLaneSet(epoch, set);
                rebuilt.GetLaneSet(epoch, rebuiltSet);
                for (const FSOLLanePair& lane : set)
                {
                    if (!LaneSchedHas(rebuiltSet, lane))
                    {
                        continue;
                    }
                    ++comparedLanes;
                    if (schedule.IsOpen(epoch, lane) != rebuilt.IsOpen(epoch, lane))
                    {
                        AddError(FString::Printf(TEXT("day %d epoch +%lld lane %d-%d: stepped open %d, rebuilt %d"),
                            day, epoch - k, lane.NodeA, lane.NodeB, schedule.IsOpen(epoch, lane) ? 1 : 0,
                            rebuilt.IsOpen(epoch, lane) ? 1 : 0));
                    }
                }
            }
        }
    }
    LaneSchedCheckReplay(*this, fixture, schedule, k0 + days, TEXT("after the stretch: current"));
    LaneSchedCheckReplay(*this, fixture, schedule, k0 + days + horizon - 1, TEXT("after the stretch: horizon end"));
    AddInfo(FString::Printf(TEXT("Stretch of %d days: %d lanes new in the band at the far end, %d stepped/rebuilt ")
        TEXT("lane states compared, %d look-back-8 checks"), days, newInBand, comparedLanes, shortChecked));
    TestTrue(TEXT("stepped and rebuilt states were compared"), comparedLanes > 1000);
    TestTrue(TEXT("lanes near the band were checked with the short look-back"), shortChecked > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkSunBandTest, "SOLTest.LaneNetworkSubsystem.SunBandFailSafe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 53 5.3 on a two-node network whose one lane sits at 21 Sun radii (inside the 20-22 band) for 150 epochs and at
// 30 outside them: it stays open while the in-band run is under the look-back and closes once the run reaches it (the
// fail safe), on the rebuilt entries, the stepped ones and a rebuild deep inside the band; for the default 64-epoch and
// a 4-epoch look-back
bool FSOLLaneNetworkSunBandTest::RunTest(const FString& /*parameters*/)
{
    const double e = SOL::NPC_LANE_NETWORK_EPOCH_S;
    const int64 k0 = 10000;
    const int64 inStart = k0 + 100;
    const int64 inEnd = inStart + 150;
    for (const int32 lookBack : { SOL::NPC_LANE_SUN_LOOKBACK_EPOCHS, 4 })
    {
        FSOLLaneNetworkConfig config;
        config.Nodes.SetNum(2);
        config.Nodes[0].Kind = ESOLLaneNodeKind::Planet;
        config.Nodes[1].Kind = ESOLLaneNodeKind::Planet;
        config.NodeMaxSpeedMps = { 0.0, 0.0 };
        config.BaseLanes.Add(FSOLLanePair());
        config.BaseLanes[0].NodeA = 0;
        config.BaseLanes[0].NodeB = 1;
        config.RootNode = 0;
        config.SlotCapacity = 1;
        config.HorizonEpochs = 200;
        config.LookBackEpochs = lookBack;
        const FSOLLaneNodePositionsFn positionsFn = [e, inStart, inEnd](const double timeS,
            TArrayView<FVector3d> outPositionsM)
        {
            const bool bInBand = timeS >= inStart * e && timeS < inEnd * e;
            const double y = (bInBand ? 21.0 : 30.0) * SOL::SUN_RADIUS_M;
            outPositionsM[0] = FVector3d(-SOL::AU_M, y, 0.0);
            outPositionsM[1] = FVector3d(SOL::AU_M, y, 0.0);
        };

        // Epochs inStart - 1 .. inEnd - 1 hold an in-band sample; the lane closes once the run reaches the look-back
        const auto expectOpen = [inStart, inEnd, lookBack](const int64 epoch)
        {
            const bool bInBand = epoch >= inStart - 1 && epoch <= inEnd - 1;
            return !bInBand || epoch - (inStart - 1) + 1 < lookBack;
        };
        const FSOLLanePair lane = config.BaseLanes[0];
        FSOLLaneNetworkSchedule schedule;
        schedule.Configure(config, positionsFn);
        TestTrue(TEXT("rebuild"), schedule.Advance(k0 * e) == ESOLLaneNetworkAdvance::Rebuilt);
        for (int64 epoch = k0; epoch < k0 + config.HorizonEpochs; ++epoch)
        {
            TestEqual(FString::Printf(TEXT("look-back %d: rebuilt entry %lld"), lookBack, epoch - k0),
                schedule.IsOpen(epoch, lane), expectOpen(epoch));
        }
        for (int64 epoch = k0 + 1; epoch <= inEnd + 50; ++epoch)
        {
            schedule.Advance(epoch * e);
            const int64 far = epoch + config.HorizonEpochs - 1;
            TestEqual(FString::Printf(TEXT("look-back %d: stepped current %lld"), lookBack, epoch - k0),
                schedule.IsOpen(epoch, lane), expectOpen(epoch));
            TestEqual(FString::Printf(TEXT("look-back %d: stepped far end %lld"), lookBack, far - k0),
                schedule.IsOpen(far, lane), expectOpen(far));
        }

        // A rebuild deep inside the band takes the full look-back: closed by the fail safe when the run is long
        FSOLLaneNetworkSchedule deep;
        deep.Configure(config, positionsFn);
        deep.Advance((inStart + 80) * e);
        for (int64 epoch = inStart + 80; epoch < inStart + 80 + config.HorizonEpochs; ++epoch)
        {
            TestEqual(FString::Printf(TEXT("look-back %d: deep rebuild entry %lld"), lookBack, epoch - k0),
                deep.IsOpen(epoch, lane), expectOpen(epoch));
        }
        TestFalse(FString::Printf(TEXT("look-back %d: closed deep in the band"), lookBack),
            deep.IsOpen(inStart + 80, lane));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkEntersInBandTest, "SOLTest.LaneNetworkSubsystem.LaneEntersInBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 53 5.3: a partner lane that enters the set inside the Sun band takes the look-back state. Four nodes (base lanes
// 0-1 and 2-3, every other lane about 1 AU or more from the Sun); at epoch S node 3 moves so 1 and 3 pick each other
// and lane 1-3 joins the set at 21 Sun radii; before S that lane's clearance was 0.84 AU. So it opens at S (the walk
// back meets S - 2 above the reopen) and closes once its in-band run reaches the look-back. Checked on an entry built
// by StepOne, on rebuilt entries before and after S, for the 64-epoch and a 4-epoch look-back
bool FSOLLaneNetworkEntersInBandTest::RunTest(const FString& /*parameters*/)
{
    const double e = SOL::NPC_LANE_NETWORK_EPOCH_S;
    const double au = SOL::AU_M;
    const double inBandY = 21.0 * SOL::SUN_RADIUS_M;
    const int64 k0 = 10000;
    const int64 enter = k0 + 300;
    const FSOLLaneNodePositionsFn positionsFn = [e, au, inBandY, enter](const double timeS,
        TArrayView<FVector3d> outPositionsM)
    {
        outPositionsM[0] = FVector3d(-au, 5.0 * au, 0.0);
        outPositionsM[1] = FVector3d(-au, inBandY, 0.0);
        outPositionsM[2] = FVector3d(au, 5.0 * au, 0.0);
        outPositionsM[3] = timeS < enter * e ? FVector3d(3.0 * au, 5.0 * au, 0.0) : FVector3d(au, inBandY, 0.0);
    };
    FSOLLanePair lane;
    lane.NodeA = 1;
    lane.NodeB = 3;
    for (const int32 lookBack : { SOL::NPC_LANE_SUN_LOOKBACK_EPOCHS, 4 })
    {
        FSOLLaneNetworkConfig config;
        config.Nodes.SetNum(4);
        for (FSOLLaneNode& node : config.Nodes)
        {
            node.Kind = ESOLLaneNodeKind::Planet;
        }
        config.NodeMaxSpeedMps = { 0.0, 0.0, 0.0, 0.0 };
        config.BaseLanes.SetNum(2);
        config.BaseLanes[0].NodeA = 0;
        config.BaseLanes[0].NodeB = 1;
        config.BaseLanes[1].NodeA = 2;
        config.BaseLanes[1].NodeB = 3;
        config.RootNode = 0;
        config.SlotCapacity = 6;
        config.HorizonEpochs = 200;
        config.LookBackEpochs = lookBack;

        // In the band from S - 1 (its last sample is S's start); open while the run is under the look-back
        const auto expectOpen = [enter, lookBack](const int64 epoch)
        {
            return epoch - (enter - 1) + 1 < lookBack;
        };
        const auto checkEntered = [&](const FSOLLaneNetworkSchedule& schedule, const TCHAR* what)
        {
            TestFalse(FString::Printf(TEXT("look-back %d %s: not in the set before S"), lookBack, what),
                schedule.IsInSet(enter - 1, lane));
            for (int64 epoch = enter; epoch < enter + lookBack + 4; ++epoch)
            {
                TestTrue(FString::Printf(TEXT("look-back %d %s: in the set at S + %lld"), lookBack, what,
                    epoch - enter), schedule.IsInSet(epoch, lane));
                TestEqual(FString::Printf(TEXT("look-back %d %s: open at S + %lld"), lookBack, what, epoch - enter),
                    schedule.IsOpen(epoch, lane), expectOpen(epoch));
            }
        };

        // StepOne: stepped from k0 until S + look-back + 3 is the far end (S lies past the first horizon)
        FSOLLaneNetworkSchedule stepped;
        stepped.Configure(config, positionsFn);
        stepped.Advance(k0 * e);
        TestFalse(TEXT("S lies past the first look-ahead"), stepped.IsInSet(enter, lane));
        const int64 lastFar = enter + lookBack + 3;
        for (int64 epoch = k0 + 1; epoch + config.HorizonEpochs - 1 <= lastFar + 1; ++epoch)
        {
            stepped.Advance(epoch * e);
        }
        if (TestTrue(TEXT("S is buffered after the steps"), stepped.GetCurrentEpoch() < enter - 1))
        {
            checkEntered(stepped, TEXT("stepped"));
        }

        // Rebuilds: S inside the look-ahead, and S + 1 as the jump's first entry
        FSOLLaneNetworkSchedule before;
        before.Configure(config, positionsFn);
        before.Advance((enter - 50) * e);
        checkEntered(before, TEXT("rebuilt before S"));
        FSOLLaneNetworkSchedule after;
        after.Configure(config, positionsFn);
        after.Advance((enter + 1) * e);
        TestEqual(FString::Printf(TEXT("look-back %d: rebuilt at S + 1, first entry"), lookBack),
            after.IsOpen(enter + 1, lane), expectOpen(enter + 1));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkCostTest, "SOLTest.LaneNetworkSubsystem.MeasuredCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 53 13 gates, measured and asserted: a full rebuild (the best of 3 jumps to fresh epochs) within 5 ms, and the
// epoch step (the best mean of 3 runs of 730 steps, the warp's path) within 0.01 ms per frame at the top warp (30 d/s)
// and 60 fps
bool FSOLLaneNetworkCostTest::RunTest(const FString& /*parameters*/)
{
    FLaneSchedFixture fixture;
    LaneSchedMakeFixture(fixture, LANESCHED_2026_S);
    FSOLLaneNetworkSchedule schedule;
    schedule.Configure(fixture.Config, LaneSchedPositionsFn(fixture));
    const double e = SOL::NPC_LANE_NETWORK_EPOCH_S;
    const int64 k0 = SOLLaneNetwork::LaneNetworkEpochIndex(LANESCHED_2026_S, e);
    constexpr double rebuildGateMs = 5.0;
    constexpr double stepGateMsPerFrame = 0.01;
    constexpr double framesPerSecond = 60.0;
    const double epochsPerFrame = SOL::SIM_CLOCK_MAX_WARP / e / framesPerSecond;

    // Rebuild: the best of 3 jumps, each to fresh epochs 5,000 days apart (nothing held in the position rings)
    double rebuildMs = LANESCHED_TEST_INF;
    double worstStepMs = 0.0;
    double bestMeanStepMs = LANESCHED_TEST_INF;
    constexpr int32 steps = 730;
    for (int32 run = 0; run < 3; ++run)
    {
        const int64 jumpEpoch = k0 + run * 5000;
        const double t0 = FPlatformTime::Seconds();
        schedule.Advance(jumpEpoch * e);
        rebuildMs = FMath::Min(rebuildMs, (FPlatformTime::Seconds() - t0) * 1000.0);

        // Steps: 2 years one epoch at a time from the jump, the mean and the worst
        const double begin = FPlatformTime::Seconds();
        for (int32 step = 1; step <= steps; ++step)
        {
            const double t1 = FPlatformTime::Seconds();
            schedule.Advance((jumpEpoch + step) * e);
            worstStepMs = FMath::Max(worstStepMs, (FPlatformTime::Seconds() - t1) * 1000.0);
        }
        bestMeanStepMs = FMath::Min(bestMeanStepMs, (FPlatformTime::Seconds() - begin) * 1000.0 / steps);
    }
    const double perFrameMs = bestMeanStepMs * epochsPerFrame;
    AddInfo(FString::Printf(TEXT("Lane network cost: rebuild %.3f ms (gate %.0f ms); epoch step mean %.4f ms, worst ")
        TEXT("%.4f ms; at 30 d/s and 60 fps (%.2f epochs per frame) %.4f ms per frame (gate %.2f ms)"), rebuildMs,
        rebuildGateMs, bestMeanStepMs, worstStepMs, epochsPerFrame, perFrameMs, stepGateMsPerFrame));
    TestTrue(FString::Printf(TEXT("rebuild %.3f ms within the %.0f ms gate"), rebuildMs, rebuildGateMs),
        rebuildMs <= rebuildGateMs);
    TestTrue(FString::Printf(TEXT("epoch step %.4f ms per frame within the %.2f ms gate"), perFrameMs,
        stepGateMsPerFrame), perFrameMs <= stepGateMsPerFrame);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkStatisticsTest, "SOLTest.LaneNetworkSubsystem.Statistics250Years",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The final rules over 250 sim years (91,312 daily steps from 2026-01-01, one jump at the start), the figures of SDD
// 53 4.3, 4.4, 7.3 and R6, R7, R15 measured and pinned: lanes per day, set changes, island links, Sun-closed lanes,
// distinct pairs, partner-lane episodes, the Earth-Moon ship share under the sqrt weights, the longest lane, the
// group columns at the worst day, and (every 10 days) the most tunnels above the activation size from one viewpoint
bool FSOLLaneNetworkStatisticsTest::RunTest(const FString& /*parameters*/)
{
    FLaneSchedFixture fixture;
    LaneSchedMakeFixture(fixture, LANESCHED_2026_S);
    FSOLLaneNetworkSchedule schedule;
    schedule.Configure(fixture.Config, LaneSchedPositionsFn(fixture));
    const double e = SOL::NPC_LANE_NETWORK_EPOCH_S;
    const int64 k0 = SOLLaneNetwork::LaneNetworkEpochIndex(LANESCHED_2026_S, e);
    constexpr int32 days = 91312;
    constexpr int32 sampleEveryDays = 10;
    constexpr int32 clusterColumns = 10;
    constexpr int32 battleColumns = 16;
    constexpr double tunnelRadiusLaneRadii = 3.0;   // The tunnel radius at its largest, 3 lane radii (SDD 53 7.3)
    const TArray<FSOLLaneNode>& nodes = fixture.Table.Nodes;
    const auto key = [](const FSOLLanePair& lane) { return (uint64(uint32(lane.NodeA)) << 32) | uint32(lane.NodeB); };

    // Returns a lane's radius (O5: the larger end region radius; Earth-Moon keeps its 2e5 m row)
    const auto laneRadiusM = [&nodes](const FSOLLanePair& lane)
    {
        const bool bEarthMoon = lane.NodeA == SOLLaneNodeTable::EARTH_NODE && lane.NodeB == SOLLaneNodeTable::MOON_NODE;
        return bEarthMoon ? nodes[SOLLaneNodeTable::MOON_NODE].RegionRadiusM
            : FMath::Max(nodes[lane.NodeA].RegionRadiusM, nodes[lane.NodeB].RegionRadiusM);
    };

    int32 minLanes = MAX_int32;
    int32 maxLanes = 0;
    double sumLanes = 0.0;
    int32 minLinks = MAX_int32;
    int32 maxLinks = 0;
    double sumLinks = 0.0;
    int32 maxClosed = 0;
    double sumClosed = 0.0;
    int32 setChanges = 0;
    int32 overflowDays = 0;
    double minShare = 1.0;
    double maxShare = 0.0;
    double sumShare = 0.0;
    double longestM = 0.0;
    FSOLLanePair longest;
    int32 maxDrawn = 0;
    int32 episodes = 0;
    int32 shortEpisodes = 0;
    TSet<uint64> distinct;
    TMap<uint64, int32> episodeStart;
    TArray<FSOLLanePair> set;
    TArray<FSOLLanePair> previousSet;
    TArray<FSOLLanePair> pairs;
    TArray<int32> partners;
    TArray<FVector3d> positions;
    TArray<FVector3d> cameras;
    partners.SetNum(24);
    positions.SetNum(24);
    schedule.Advance(k0 * e);
    for (int32 day = 0; day < days; ++day)
    {
        const int64 k = k0 + day;
        if (day > 0 && schedule.Advance(k * e) != ESOLLaneNetworkAdvance::Stepped)
        {
            AddError(FString::Printf(TEXT("day %d did not step"), day));
            return false;
        }
        schedule.GetLaneSet(k, set);
        const TConstArrayView<FSOLPartnerPick> picks = schedule.GetPicks(k);
        for (int32 node = 0; node < 24; ++node)
        {
            partners[node] = picks[node].Partner;
        }
        SOLLaneNetwork::PairsFromPicks(partners, pairs);
        const int32 links = set.Num() - fixture.Table.BaseLanes.Num() - pairs.Num();
        minLanes = FMath::Min(minLanes, set.Num());
        maxLanes = FMath::Max(maxLanes, set.Num());
        sumLanes += set.Num();
        minLinks = FMath::Min(minLinks, links);
        maxLinks = FMath::Max(maxLinks, links);
        sumLinks += links;
        setChanges += (day > 0 && !LaneSchedSameSet(set, previousSet)) ? 1 : 0;
        overflowDays += schedule.GetSlotOverflow() > 0 ? 1 : 0;

        // Sun-closed lanes, the sqrt weights' Earth-Moon share, the longest lane, distinct pairs
        int32 closed = 0;
        double weightSum = 0.0;
        for (const FSOLLanePair& lane : set)
        {
            closed += schedule.IsOpen(k, lane) ? 0 : 1;
            weightSum += SOLLaneNetwork::LaneWeight(nodes[lane.NodeA].PopulationTier, nodes[lane.NodeB].PopulationTier);
            const double lengthM = SOLLaneNetwork::LaneNominalLengthM(nodes, lane.NodeA, lane.NodeB);
            if (lengthM > longestM)
            {
                longestM = lengthM;
                longest = lane;
            }
            distinct.Add(key(lane));
        }
        maxClosed = FMath::Max(maxClosed, closed);
        sumClosed += closed;
        const double share = SOLLaneNetwork::LaneWeight(10.0, 10.0) / weightSum;
        minShare = FMath::Min(minShare, share);
        maxShare = FMath::Max(maxShare, share);
        sumShare += share;

        // Partner-lane episodes (a continuous stay in the set of a non-base lane): ends and starts
        for (const FSOLLanePair& lane : previousSet)
        {
            const int32* start = episodeStart.Find(key(lane));
            if (start != nullptr && !LaneSchedHas(set, lane))
            {
                ++episodes;
                shortEpisodes += day - *start < SOL::NPC_LANE_PARTNER_MIN_STAY_EPOCHS ? 1 : 0;
                episodeStart.Remove(key(lane));
            }
        }
        for (const FSOLLanePair& lane : set)
        {
            const bool bBase = LaneSchedHas(fixture.Table.BaseLanes, lane);
            if (!bBase && !episodeStart.Contains(key(lane)))
            {
                episodeStart.Add(key(lane), day);
            }
        }

        // Every 10 days: tunnels above the activation size from each node and each lane's midpoint
        if (day % sampleEveryDays == 0)
        {
            SOLLaneNodeTable::PositionsAt(fixture.Table, fixture.Registry, k * e, positions);
            cameras = positions;
            for (const FSOLLanePair& lane : set)
            {
                cameras.Add(0.5 * (positions[lane.NodeA] + positions[lane.NodeB]));
            }
            for (const FVector3d& camera : cameras)
            {
                int32 drawn = 0;
                for (const FSOLLanePair& lane : set)
                {
                    const double distanceM = SOLLaneNetwork::SunSegmentDistanceM(positions[lane.NodeA] - camera,
                        positions[lane.NodeB] - camera);
                    const double radiusPx = SOLLaneNetwork::LaneTunnelMaxRadiusPx(distanceM,
                        tunnelRadiusLaneRadii * laneRadiusM(lane), SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX);
                    drawn += radiusPx >= SOL::NPC_TUNNEL_ACTIVATE_PX ? 1 : 0;
                }
                maxDrawn = FMath::Max(maxDrawn, drawn);
            }
        }
        previousSet = set;
    }
    const double years = days / 365.25;
    const double longestAu = longestM / SOL::AU_M;
    AddInfo(FString::Printf(TEXT("250 years: lanes %d-%d (mean %.2f); %.1f set changes per year; island links %d-%d ")
        TEXT("(mean %.2f); Sun-closed lanes at most %d (mean %.3f); %d distinct pairs; %d partner episodes, %d ")
        TEXT("shorter than %d days; Earth-Moon share %.1f-%.1f %% (mean %.1f %%); longest lane %s-%s %.2f AU; columns at ")
        TEXT("the worst day %d + %d + %d = %d of %d, %d overflow days; at most %d tunnels above %.1f px from one ")
        TEXT("viewpoint"), minLanes, maxLanes, sumLanes / days, setChanges / years, minLinks, maxLinks,
        sumLinks / days, maxClosed, sumClosed / days, distinct.Num(), episodes, shortEpisodes,
        SOL::NPC_LANE_PARTNER_MIN_STAY_EPOCHS, minShare * 100.0, maxShare * 100.0, sumShare / days * 100.0,
        *fixture.Table.Names[longest.NodeA].ToString(), *fixture.Table.Names[longest.NodeB].ToString(),
        longestAu, maxLanes, clusterColumns, battleColumns, maxLanes + clusterColumns + battleColumns,
        SOL::NPC_GROUP_TEXTURE_WIDTH, overflowDays, maxDrawn, SOL::NPC_TUNNEL_ACTIVATE_PX));

    // The pinned figures (SDD 53 4.3, 4.4, 7.3, R6, R7, R15; measured 2026-11-25)
    TestEqual(TEXT("fewest lanes in a day"), minLanes, 25);
    TestEqual(TEXT("most lanes in a day"), maxLanes, 30);
    TestEqual(TEXT("mean lanes per day"), sumLanes / days, 27.07, 0.01);
    TestEqual(TEXT("set changes per year"), setChanges / years, 15.5, 0.1);
    TestEqual(TEXT("fewest island links"), minLinks, 0);
    TestEqual(TEXT("most island links"), maxLinks, 5);
    TestEqual(TEXT("mean island links"), sumLinks / days, 2.01, 0.01);
    TestEqual(TEXT("most Sun-closed lanes at once"), maxClosed, 5);
    TestEqual(TEXT("distinct pairs"), distinct.Num(), 187);
    TestEqual(TEXT("partner episodes"), episodes, 3694);
    TestEqual(TEXT("partner episodes shorter than the minimum stay"), shortEpisodes, 393);
    TestEqual(TEXT("smallest Earth-Moon share"), minShare * 100.0, 9.9, 0.05);
    TestEqual(TEXT("largest Earth-Moon share"), maxShare * 100.0, 13.6, 0.05);
    TestTrue(TEXT("longest lane Haumea-Eris"), fixture.Table.Names[longest.NodeA] == FName(TEXT("Haumea"))
        && fixture.Table.Names[longest.NodeB] == FName(TEXT("Eris")));
    TestEqual(TEXT("longest lane (AU)"), longestAu, 149.08, 0.01);
    TestEqual(TEXT("most tunnels above the activation size from one viewpoint"), maxDrawn, 8);
    TestTrue(TEXT("lanes within the bound of 38"), maxLanes <= 38);
    TestTrue(TEXT("columns at the worst day within the 64-wide texture"),
        maxLanes + clusterColumns + battleColumns <= SOL::NPC_GROUP_TEXTURE_WIDTH);
    TestEqual(TEXT("no overflow day at 38 lane columns"), overflowDays, 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
