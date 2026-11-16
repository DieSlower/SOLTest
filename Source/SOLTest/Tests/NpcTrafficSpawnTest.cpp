/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcLaneMath.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #50 part 50b lane population (SDD 50 sections 4.2, 4.6 and 4.7, contract cases 9-12), written
// from the contract before the 50b code existed: the spawn counting pass (in-wall and halo ships per lane), the frozen
// wall radius Rw = SOLNpcTunnel::TunnelRadiusM(R, in-wall count) in the group table's lane profiles (D13: the halo does
// not widen the tunnel; D15: the label counts every lane ship), a whole population's rails (keep right by direction,
// the 75 / 25 split between the disc inside the wall and the annulus out to 2 Rw), the lane's largest peak speed and
// the wake speed bound, and the edge cases. The names of the API under test are the ones recorded in the plan's
// "Part 50b API contract (from the test author)" section.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCTRAFFICSPAWN_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double NPCTRAFFICSPAWN_INF = std::numeric_limits<double>::infinity();
    constexpr uint32 NPCTRAFFICSPAWN_SEED = 20261113u;

    // Bodies of the test population (mean radii, m) and the two lanes' nominal (maximum) lengths (D16)
    constexpr double NPCTRAFFICSPAWN_EARTH_RADIUS_M = 6.371e6;
    constexpr double NPCTRAFFICSPAWN_MARS_RADIUS_M = 3.3895e6;
    constexpr double NPCTRAFFICSPAWN_MOON_RADIUS_M = 1.7374e6;
    constexpr double NPCTRAFFICSPAWN_EARTH_MARS_L0_M = 1.5210e11 + 2.4923e11;
    constexpr double NPCTRAFFICSPAWN_EARTH_MOON_L0_M = 4.055e8;

    // A whole test population: layout, counts, group table, bodies, lane spawns and their rails, the counting pass'
    // results and the frame
    struct FNpcTrafficSpawnPopulation
    {
        SOLNpcScale::FLayout Layout;
        SOLNpcScale::FCounts Counts;
        FSOLNpcGroupTable Groups;
        TArray<FVector3d> Bodies;                   // 0 Earth, 1 Mars, 2 Moon
        TArray<SOLNpcScale::FSpawn> Spawns;         // Lane ships with a lane, in NPC index order
        TArray<FSOLNpcRail> Rails;                  // One per entry of Spawns
        TArray<int32> InWall;
        TArray<int32> Halo;
        FSOLNpcRailFrame Frame;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the test layout: Earth - Mars (radius 3,000 km, weight 3) and Earth - Moon (radius 200 km, weight 1), no
    // clusters
    SOLNpcScale::FLayout NpcTrafficSpawnLayout()
    {
        SOLNpcScale::FLayout layout;
        SOLNpcScale::FLaneDef mars;
        mars.BodyA = FName(TEXT("Earth"));
        mars.BodyB = FName(TEXT("Mars"));
        mars.RadiusM = 3.0e6;
        mars.Weight = 3.0;
        SOLNpcScale::FLaneDef moon;
        moon.BodyA = FName(TEXT("Earth"));
        moon.BodyB = FName(TEXT("Moon"));
        moon.RadiusM = 2.0e5;
        moon.Weight = 1.0;
        layout.Lanes = { mars, moon };
        return layout;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns counts with the given numbers of battle, lane and cluster ships
    SOLNpcScale::FCounts NpcTrafficSpawnCounts(const int32 battle, const int32 lane, const int32 cluster)
    {
        SOLNpcScale::FCounts counts;
        counts.Battle = battle;
        counts.Lane = lane;
        counts.Cluster = cluster;
        return counts;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the battle parameters of the test populations
    SOLNpcScale::FBattleParams NpcTrafficSpawnBattle()
    {
        SOLNpcScale::FBattleParams battle;
        battle.BattleCount = 2;
        battle.RadiusM = 3000.0;
        battle.MinDistanceM = 5000.0;
        battle.MaxDistanceM = 20000.0;
        return battle;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills a group table's lanes for the test layout (Earth = body 0, Mars = 1, Moon = 2) and builds the frozen lane
    // profiles from the given counts
    void NpcTrafficSpawnGroups(const SOLNpcScale::FLayout& layout, TConstArrayView<int32> inWall,
        TConstArrayView<int32> halo, FSOLNpcGroupTable& outGroups)
    {
        outGroups = FSOLNpcGroupTable();
        outGroups.LaneCount = layout.Lanes.Num();
        outGroups.LaneBodyA = { 0, 0 };
        outGroups.LaneBodyB = { 1, 2 };
        outGroups.LaneRadiusM = { layout.Lanes[0].RadiusM, layout.Lanes[1].RadiusM };
        const double nominal[] = { NPCTRAFFICSPAWN_EARTH_MARS_L0_M, NPCTRAFFICSPAWN_EARTH_MOON_L0_M };
        const double radiusA[] = { NPCTRAFFICSPAWN_EARTH_RADIUS_M, NPCTRAFFICSPAWN_EARTH_RADIUS_M };
        const double radiusB[] = { NPCTRAFFICSPAWN_MARS_RADIUS_M, NPCTRAFFICSPAWN_MOON_RADIUS_M };
        SOLNpcRail::BuildLaneProfiles(outGroups, inWall, halo, nominal, radiusA, radiusB);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the body positions with each lane's bodies lengthRatio x its nominal leg (plus the end arcs) apart: Earth
    // far from the origin, Mars in the ecliptic, the Moon 7 degrees above it
    TArray<FVector3d> NpcTrafficSpawnBodies(const FSOLNpcGroupTable& groups, const double lengthRatio)
    {
        const FVector3d earth(1.2e11, -8.0e10, 3.0e6);
        const double elevation = FMath::DegreesToRadians(7.0);
        const FVector3d toMoon(0.6 * FMath::Cos(elevation), 0.8 * FMath::Cos(elevation), FMath::Sin(elevation));
        const FSOLLaneProfile& mars = groups.LaneProfile[0];
        const FSOLLaneProfile& moon = groups.LaneProfile[1];
        return { earth,
            earth + FVector3d(0.6, 0.8, 0.0) * (mars.EndArcAM + mars.EndArcBM + lengthRatio * mars.NominalLegM),
            earth + toMoon * (moon.EndArcAM + moon.EndArcBM + lengthRatio * moon.NominalLegM) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a whole test population the way the NPC subsystem builds it (SDD 50 section 4.7): the counting pass, the
    // lane profiles, the rails (FromSpawn with each lane's profile), the lanes' largest peaks and the frame
    FNpcTrafficSpawnPopulation NpcTrafficSpawnBuild(const uint32 seed, const int32 laneShips, const double lengthRatio)
    {
        FNpcTrafficSpawnPopulation population;
        population.Layout = NpcTrafficSpawnLayout();
        population.Counts = NpcTrafficSpawnCounts(0, laneShips, 0);
        SOLNpcScale::CountLaneSpawns(seed, population.Counts, population.Layout, population.InWall, population.Halo);
        NpcTrafficSpawnGroups(population.Layout, population.InWall, population.Halo, population.Groups);

        // The rails of every lane ship that has a lane
        const SOLNpcScale::FBattleParams battle = NpcTrafficSpawnBattle();
        population.Spawns.Reserve(laneShips);
        population.Rails.Reserve(laneShips);
        for (int32 index = 0; index < population.Counts.Total(); ++index)
        {
            const SOLNpcScale::FSpawn spawn = SOLNpcScale::MakeSpawn(seed, index, population.Counts,
                population.Layout, battle);
            if (spawn.Kind != SOLNpcScale::EPlacementKind::Lane || spawn.GroupIndex < 0
                || spawn.GroupIndex >= population.Groups.LaneCount)
            {
                continue;
            }
            population.Spawns.Add(spawn);
            population.Rails.Add(SOLNpcRail::FromSpawn(spawn, 0, population.Groups.LaneProfile[spawn.GroupIndex],
                0.0));
        }
        SOLNpcRail::AccumulateLaneMaxPeak(population.Rails, population.Groups.LaneMaxPeakMps);
        population.Bodies = NpcTrafficSpawnBodies(population.Groups, lengthRatio);
        SOLNpcRail::BuildFrame(population.Groups, population.Bodies, 0.0, population.Frame);
        return population;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two lane profiles agree field by field within a relative tolerance
    bool NpcTrafficSpawnSameProfile(const FSOLLaneProfile& a, const FSOLLaneProfile& b, const double relTol)
    {
        const auto sameValue = [relTol](const double x, const double y)
        {
            return (FMath::IsNaN(x) && FMath::IsNaN(y)) || FMath::Abs(x - y) <= relTol * FMath::Max(FMath::Abs(x),
                FMath::Abs(y));
        };
        return sameValue(a.WallRadiusM, b.WallRadiusM) && sameValue(a.HaloOuterRadiusM, b.HaloOuterRadiusM)
            && sameValue(a.NominalLengthM, b.NominalLengthM) && sameValue(a.NominalLegM, b.NominalLegM)
            && sameValue(a.EndArcAM, b.EndArcAM) && sameValue(a.EndArcBM, b.EndArcBM)
            && sameValue(a.UTurnDepthM, b.UTurnDepthM) && sameValue(a.UTurnS, b.UTurnS)
            && sameValue(a.MetricZoneM, b.MetricZoneM) && a.bDrivable == b.bDrivable;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a rail's lateral radius sqrt(P2^2 + P3^2) in double
    double NpcTrafficSpawnLateralM(const FSOLNpcRail& rail)
    {
        return FMath::Sqrt(static_cast<double>(rail.P[2]) * rail.P[2] + static_cast<double>(rail.P[3]) * rail.P[3]);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficSpawnCountingTest, "SOLTest.NpcTrafficSpawn.CountingPass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.7 step (1): CountLaneSpawns counts, per lane of the layout, the lane ships inside the wall and in
// the halo exactly as MakeSpawn places them (bLaneHalo), skipping lane ships with no lane; AccumulateLaneSpawns adds
// one NPC index range's share (no reset; non-lane indices skipped; the range clamped to the population), so any
// partition merged equals the whole; the counts are deterministic and the halo share holds for other seeds
bool FSOLNpcTrafficSpawnCountingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FLayout layout = NpcTrafficSpawnLayout();
    const FBattleParams battle = NpcTrafficSpawnBattle();
    const FCounts counts = NpcTrafficSpawnCounts(300, 20000, 500);

    // Reference tally through MakeSpawn
    int32 expectedInWall[2] = {};
    int32 expectedHalo[2] = {};
    for (int32 index = 0; index < counts.Total(); ++index)
    {
        const FSpawn spawn = MakeSpawn(NPCTRAFFICSPAWN_SEED, index, counts, layout, battle);
        if (spawn.Kind == EPlacementKind::Lane && (spawn.GroupIndex == 0 || spawn.GroupIndex == 1))
        {
            (spawn.bLaneHalo ? expectedHalo : expectedInWall)[spawn.GroupIndex] += 1;
        }
    }

    TArray<int32> inWall;
    TArray<int32> halo;
    CountLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, inWall, halo);
    if (!TestEqual(TEXT("one in-wall count per lane"), inWall.Num(), 2)
        || !TestEqual(TEXT("one halo count per lane"), halo.Num(), 2))
    {
        return false;
    }
    for (int32 lane = 0; lane < 2; ++lane)
    {
        TestEqual(*FString::Printf(TEXT("lane %d: in-wall = MakeSpawn's"), lane), inWall[lane], expectedInWall[lane]);
        TestEqual(*FString::Printf(TEXT("lane %d: halo = MakeSpawn's"), lane), halo[lane], expectedHalo[lane]);
    }
    TestEqual(TEXT("every lane ship counted once"), inWall[0] + inWall[1] + halo[0] + halo[1], counts.Lane);
    TestTrue(TEXT("halo share about NPC_LANE_HALO_FRACTION"), FMath::Abs((halo[0] + halo[1])
        / static_cast<double>(counts.Lane) - SOL::NPC_LANE_HALO_FRACTION) <= 0.01);

    // Deterministic
    TArray<int32> inWallAgain;
    TArray<int32> haloAgain;
    CountLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, inWallAgain, haloAgain);
    TestTrue(TEXT("repeat: identical counts"), inWallAgain == inWall && haloAgain == halo);

    // Any partition of the NPC indices (uneven, including battle and cluster indices) merged equals the whole
    const int32 cuts[] = { 0, 1, 299, 300, 301, 7777, 13000, 20299, 20300, 20800 };
    TArray<int32> mergedInWall;
    TArray<int32> mergedHalo;
    mergedInWall.Init(0, 2);
    mergedHalo.Init(0, 2);
    for (int32 cut = 0; cut + 1 < UE_ARRAY_COUNT(cuts); ++cut)
    {
        AccumulateLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, cuts[cut], cuts[cut + 1], mergedInWall, mergedHalo);
    }
    TestTrue(TEXT("partition merged = whole"), mergedInWall == inWall && mergedHalo == halo);

    // The range is clamped to the population; an empty range adds nothing
    TArray<int32> clampedInWall;
    TArray<int32> clampedHalo;
    clampedInWall.Init(0, 2);
    clampedHalo.Init(0, 2);
    AccumulateLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, -50, counts.Total() + 10, clampedInWall, clampedHalo);
    TestTrue(TEXT("clamped range = whole"), clampedInWall == inWall && clampedHalo == halo);
    AccumulateLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, 5000, 5000, clampedInWall, clampedHalo);
    AccumulateLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, 9000, 4000, clampedInWall, clampedHalo);
    TestTrue(TEXT("empty and reversed ranges add nothing"), clampedInWall == inWall && clampedHalo == halo);

    // Degenerate populations and layouts
    CountLaneSpawns(NPCTRAFFICSPAWN_SEED, NpcTrafficSpawnCounts(100, 0, 100), layout, inWall, halo);
    TestTrue(TEXT("no lane ships: zeros"), inWall.Num() == 2 && halo.Num() == 2 && inWall[0] == 0 && inWall[1] == 0
        && halo[0] == 0 && halo[1] == 0);
    FLayout noWeights = layout;
    noWeights.Lanes[0].Weight = 0.0;
    noWeights.Lanes[1].Weight = -1.0;
    CountLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, noWeights, inWall, halo);
    TestTrue(TEXT("no lane with a weight: zeros (orphans not counted)"), inWall.Num() == 2 && halo.Num() == 2
        && inWall[0] + inWall[1] + halo[0] + halo[1] == 0);
    FLayout noLanes = layout;
    noLanes.Lanes.Reset();
    CountLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, noLanes, inWall, halo);
    TestTrue(TEXT("no lanes: empty counts"), inWall.Num() == 0 && halo.Num() == 0);

    // A short count array: the lanes past its end are not counted, nothing is written past it
    TArray<int32> oneInWall;
    TArray<int32> oneHalo;
    oneInWall.Init(0, 1);
    oneHalo.Init(0, 1);
    AccumulateLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, 0, counts.Total(), oneInWall, oneHalo);
    TestTrue(TEXT("short arrays: lane 0 only"), oneInWall.Num() == 1 && oneInWall[0] == expectedInWall[0]
        && oneHalo[0] == expectedHalo[0]);

    // Seed stability: the halo share holds for other seeds
    for (const uint32 seed : { NPCTRAFFICSPAWN_SEED + 1u, NPCTRAFFICSPAWN_SEED + 1000u, SOL::NPC_DEFAULT_SEED })
    {
        CountLaneSpawns(seed, counts, layout, inWall, halo);
        const double share = (halo[0] + halo[1]) / static_cast<double>(counts.Lane);
        TestTrue(*FString::Printf(TEXT("seed %u: halo share %.4f"), seed, share),
            FMath::Abs(share - SOL::NPC_LANE_HALO_FRACTION) <= 0.015);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficSpawnWallRadiusTest, "SOLTest.NpcTrafficSpawn.WallRadiusFrozen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.2 and D13: BuildLaneProfiles stores the counts and one frozen profile per lane,
// MakeLaneProfile(TunnelRadiusM(R, in-wall count), L0, R_A, R_B); the halo count never changes it (D6); a lane past the
// end of an input array reads a zero count or length; LaneMaxPeakMps is reset to zeros. End to end, the tunnel drawn
// for a lane has exactly that wall radius, and the label's ship count is every lane ship, halo included (D15)
bool FSOLNpcTrafficSpawnWallRadiusTest::RunTest(const FString& /*parameters*/)
{
    const SOLNpcScale::FLayout layout = NpcTrafficSpawnLayout();
    const int32 inWall[] = { 30000, 1200 };
    const int32 halo[] = { 10000, 400 };
    FSOLNpcGroupTable groups;
    NpcTrafficSpawnGroups(layout, inWall, halo, groups);
    if (!TestEqual(TEXT("one profile per lane"), groups.LaneProfile.Num(), 2))
    {
        return false;
    }
    const double nominal[] = { NPCTRAFFICSPAWN_EARTH_MARS_L0_M, NPCTRAFFICSPAWN_EARTH_MOON_L0_M };
    const double radiusB[] = { NPCTRAFFICSPAWN_MARS_RADIUS_M, NPCTRAFFICSPAWN_MOON_RADIUS_M };
    for (int32 lane = 0; lane < 2; ++lane)
    {
        const FString label = FString::Printf(TEXT("lane %d"), lane);
        const double rw = SOLNpcTunnel::TunnelRadiusM(layout.Lanes[lane].RadiusM, inWall[lane]);
        const FSOLLaneProfile expected = SOLLaneTraffic::MakeLaneProfile(rw, nominal[lane],
            NPCTRAFFICSPAWN_EARTH_RADIUS_M, radiusB[lane]);
        TestTrue(label + TEXT(": profile = MakeLaneProfile(TunnelRadiusM(R, in-wall), L0, R_A, R_B)"),
            NpcTrafficSpawnSameProfile(groups.LaneProfile[lane], expected, 1.0e-12));
        TestTrue(label + TEXT(": drivable"), groups.LaneProfile[lane].bDrivable);
        TestEqual(label + TEXT(": in-wall count stored"), groups.LaneInWallCount[lane], inWall[lane]);
        TestEqual(label + TEXT(": halo count stored"), groups.LaneHaloCount[lane], halo[lane]);
        TestEqual(label + TEXT(": largest peak reset to 0"), groups.LaneMaxPeakMps[lane], 0.0);
    }

    // The halo never widens the tunnel; the in-wall count does
    const int32 moreHalo[] = { 900000, 0 };
    FSOLNpcGroupTable haloGroups;
    NpcTrafficSpawnGroups(layout, inWall, moreHalo, haloGroups);
    TestTrue(TEXT("halo x 90: same profiles"), NpcTrafficSpawnSameProfile(haloGroups.LaneProfile[0],
        groups.LaneProfile[0], 0.0) && NpcTrafficSpawnSameProfile(haloGroups.LaneProfile[1], groups.LaneProfile[1],
        0.0));
    const int32 fewInside[] = { 100, 1200 };
    FSOLNpcGroupTable thinGroups;
    NpcTrafficSpawnGroups(layout, fewInside, halo, thinGroups);
    TestEqual(TEXT("100 ships inside: Rw = R (the #49 radius of 100 ships)"), thinGroups.LaneProfile[0].WallRadiusM,
        SOLNpcTunnel::TunnelRadiusM(layout.Lanes[0].RadiusM, 100), 1.0e-9 * layout.Lanes[0].RadiusM);

    // Short input arrays: the lane past their end reads count 0 and length 0 (not drivable)
    FSOLNpcGroupTable shortGroups;
    shortGroups.LaneCount = 2;
    shortGroups.LaneBodyA = { 0, 0 };
    shortGroups.LaneBodyB = { 1, 2 };
    shortGroups.LaneRadiusM = { 3.0e6, 2.0e5 };
    const int32 oneInWall[] = { 30000 };
    const int32 oneHalo[] = { 10000 };
    const double oneNominal[] = { NPCTRAFFICSPAWN_EARTH_MARS_L0_M };
    const double oneRadiusA[] = { NPCTRAFFICSPAWN_EARTH_RADIUS_M };
    const double oneRadiusB[] = { NPCTRAFFICSPAWN_MARS_RADIUS_M };
    SOLNpcRail::BuildLaneProfiles(shortGroups, oneInWall, oneHalo, oneNominal, oneRadiusA, oneRadiusB);
    TestTrue(TEXT("short inputs: sized to the lanes"), shortGroups.LaneProfile.Num() == 2
        && shortGroups.LaneInWallCount.Num() == 2 && shortGroups.LaneHaloCount.Num() == 2
        && shortGroups.LaneMaxPeakMps.Num() == 2);
    TestTrue(TEXT("short inputs: lane 1 count 0, not drivable"), shortGroups.LaneInWallCount[1] == 0
        && shortGroups.LaneHaloCount[1] == 0 && !shortGroups.LaneProfile[1].bDrivable);

    // End to end from a population: Rw from the counting pass, the drawn tunnel radius equal to it, the label count
    // every lane ship
    const FNpcTrafficSpawnPopulation population = NpcTrafficSpawnBuild(NPCTRAFFICSPAWN_SEED, 20000, 1.0);
    TArray<int32> laneShips;
    SOLNpcLane::CountLaneShips(population.Rails, population.Groups.LaneCount, laneShips);
    FSOLNpcLaneFrame laneFrame;
    SOLNpcLane::BuildLaneFrame(population.Groups, population.Frame, laneShips, laneFrame);
    const double bodyRadiusB[] = { NPCTRAFFICSPAWN_MARS_RADIUS_M, NPCTRAFFICSPAWN_MOON_RADIUS_M };
    for (int32 lane = 0; lane < 2; ++lane)
    {
        const FString label = FString::Printf(TEXT("population lane %d"), lane);
        const double rw = SOLNpcTunnel::TunnelRadiusM(population.Layout.Lanes[lane].RadiusM,
            population.InWall[lane]);
        TestEqual(label + TEXT(": Rw = TunnelRadiusM(R, in-wall count)"),
            population.Groups.LaneProfile[lane].WallRadiusM, rw, 1.0e-12 * rw);
        TestEqual(label + TEXT(": frame profile = group profile"), population.Frame.LaneProfile[lane].WallRadiusM,
            rw, 1.0e-12 * rw);
        TestEqual(label + TEXT(": label count = in-wall + halo (D15)"), laneShips[lane],
            population.InWall[lane] + population.Halo[lane]);
        const FSOLNpcLaneEntry& entry = laneFrame.Lanes[lane];
        TestEqual(label + TEXT(": entry wall radius"), entry.WallRadiusM, rw, 1.0e-12 * rw);
        FSOLNpcTunnelLaneData data;
        FSOLNpcTunnelFoot foot;
        const FVector3d viewpoint = 0.5 * (entry.A + entry.B) + population.Frame.LaneFrame[lane].Up * (5.0 * rw);
        TestTrue(label + TEXT(": has a tunnel"), SOLNpcTunnel::BuildTunnelLaneData(lane, entry, viewpoint,
            NPCTRAFFICSPAWN_EARTH_RADIUS_M, bodyRadiusB[lane], data, foot));
        TestEqual(label + TEXT(": drawn tunnel radius = the frozen Rw (D13)"), data.TunnelRadiusM, rw, 1.0e-12 * rw);
        TestTrue(label + TEXT(": the halo does not widen it"), data.TunnelRadiusM
            < SOLNpcTunnel::TunnelRadiusM(population.Layout.Lanes[lane].RadiusM, laneShips[lane]) + 1.0e-9 * rw
            || population.Halo[lane] == 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficSpawnPopulationTest, "SOLTest.NpcTrafficSpawn.Population",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A population's rails (contract cases 8 and 9 through the spawn path): ships inside the wall have a lateral radius
// below Rw, halo ships between Rw and 2 Rw, a quarter of the ships lie beyond Rw; on the outbound leg every ship is on
// the right of the axis, on the inbound leg on the left (keep right by direction, from the frame's lane basis), with
// its lateral radius constant on the legs; the build is deterministic
bool FSOLNpcTrafficSpawnPopulationTest::RunTest(const FString& /*parameters*/)
{
    const FNpcTrafficSpawnPopulation population = NpcTrafficSpawnBuild(NPCTRAFFICSPAWN_SEED, 20000, 1.0);
    const FSOLNpcRailFrame& frame = population.Frame;
    const double outer = SOL::NPC_LANE_HALO_OUTER_WALL_RADII;
    TestTrue(TEXT("rails built"), population.Rails.Num() > 19000 && population.Rails.Num() == population.Spawns.Num());

    int32 beyondWall = 0;
    int32 radiusFailures = 0;
    int32 sideFailures = 0;
    int32 lateralFailures = 0;
    int32 sideChecks = 0;
    FString firstFailure;
    for (int32 index = 0; index < population.Rails.Num(); ++index)
    {
        const FSOLNpcRail& rail = population.Rails[index];
        const SOLNpcScale::FSpawn& spawn = population.Spawns[index];
        const int32 lane = spawn.GroupIndex;
        const FSOLLaneProfile& profile = frame.LaneProfile[lane];
        const FSOLLaneFrame& laneFrame = frame.LaneFrame[lane];
        const double rw = profile.WallRadiusM;
        const double lateral = NpcTrafficSpawnLateralM(rail);

        // Inside the wall or in the annulus out to H Rw, as the spawn said
        beyondWall += lateral > rw ? 1 : 0;
        const bool bRadiusOk = spawn.bLaneHalo ? (lateral >= rw * (1.0 - 1.0e-6) && lateral <= outer * rw
            * (1.0 + 1.0e-6)) : lateral <= rw * (1.0 + 1.0e-6);
        if (!bRadiusOk)
        {
            if (radiusFailures + sideFailures + lateralFailures == 0)
            {
                firstFailure = FString::Printf(TEXT("ship %d: lateral %.6e, Rw %.6e, halo %d"), index, lateral, rw,
                    spawn.bLaneHalo ? 1 : 0);
            }
            ++radiusFailures;
        }

        // Keep right by direction on the legs (every 7th ship, three times)
        if (index % 7 != 0 || rail.P[3] < 1.0e-6 * rw)
        {
            continue;
        }
        const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);
        const FSOLLaneCycle cycle = SOLLaneTraffic::MakeCycle(SOLLaneTraffic::LaneShipLegS(profile, ship),
            profile.UTurnDepthM);
        for (const double t : { 0.0, 0.31 * cycle.CycleS, 0.77 * cycle.CycleS })
        {
            const FSOLLaneCyclePoint point = SOLLaneTraffic::CycleSegmentAt(cycle, ship.Phase, t);
            const bool bOutbound = point.Segment == ESOLLaneSegment::Outbound;
            if (!bOutbound && point.Segment != ESOLLaneSegment::Inbound)
            {
                continue;
            }
            ++sideChecks;
            const FVector3d relM = SOLNpcRail::EvaluateAtTimeM(rail, frame, t) - laneFrame.AM;
            const double right = FVector3d::DotProduct(relM, laneFrame.Right);
            const double up = FVector3d::DotProduct(relM, laneFrame.Up);
            if (bOutbound ? !(right > 0.0) : !(right < 0.0))
            {
                if (radiusFailures + sideFailures + lateralFailures == 0)
                {
                    firstFailure = FString::Printf(TEXT("ship %d t %.6e: %s leg, right component %.6e"), index, t,
                        bOutbound ? TEXT("outbound") : TEXT("inbound"), right);
                }
                ++sideFailures;
            }
            if (FMath::Abs(FMath::Sqrt(right * right + up * up) - lateral) > 1.0e-2 + 1.0e-9 * lateral)
            {
                if (radiusFailures + sideFailures + lateralFailures == 0)
                {
                    firstFailure = FString::Printf(TEXT("ship %d t %.6e: leg lateral radius %.6e, rail %.6e"), index,
                        t, FMath::Sqrt(right * right + up * up), lateral);
                }
                ++lateralFailures;
            }
        }
    }
    const double share = beyondWall / static_cast<double>(FMath::Max(1, population.Rails.Num()));
    TestTrue(*FString::Printf(TEXT("a quarter beyond the wall (%.4f)"), share),
        FMath::Abs(share - SOL::NPC_LANE_HALO_FRACTION) <= 0.01);
    TestTrue(TEXT("side checks made"), sideChecks > 1000);
    TestTrue(FString::Printf(TEXT("radii, sides, lateral radii (%d radius, %d side, %d lateral failures; first: %s)"),
        radiusFailures, sideFailures, lateralFailures, *firstFailure),
        radiusFailures == 0 && sideFailures == 0 && lateralFailures == 0);

    // Deterministic
    const FNpcTrafficSpawnPopulation again = NpcTrafficSpawnBuild(NPCTRAFFICSPAWN_SEED, 20000, 1.0);
    bool bSame = again.Rails.Num() == population.Rails.Num();
    for (int32 index = 0; bSame && index < population.Rails.Num(); ++index)
    {
        bSame = FMemory::Memcmp(&again.Rails[index], &population.Rails[index], sizeof(FSOLNpcRail)) == 0;
    }
    TestTrue(TEXT("rebuilt: bit-identical rails"), bSame);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficSpawnMaxPeakTest, "SOLTest.NpcTrafficSpawn.MaxPeak",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.7: a lane's largest peak speed is the largest w + P1 over its rails (the rail's own float dv, so
// the wake bound covers the rounded rails), at most the axis full-effort peak; AccumulateLaneMaxPeak takes the max
// (never lowers a value), ignores cluster, battle and out-of-range rails, and any partition merged equals the whole
bool FSOLNpcTrafficSpawnMaxPeakTest::RunTest(const FString& /*parameters*/)
{
    const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
    const FNpcTrafficSpawnPopulation population = NpcTrafficSpawnBuild(NPCTRAFFICSPAWN_SEED, 20000, 1.0);
    double expected[2] = {};
    for (const FSOLNpcRail& rail : population.Rails)
    {
        expected[rail.Group] = FMath::Max(expected[rail.Group], w + static_cast<double>(rail.P[1]));
    }
    for (int32 lane = 0; lane < 2; ++lane)
    {
        const FString label = FString::Printf(TEXT("lane %d"), lane);
        const double axisPeak = SOLLaneTraffic::MakeLegProfile(1.0, SOL::NPC_LANE_EFFORT_MAX,
            population.Groups.LaneProfile[lane].NominalLegM).PeakMps;
        TestEqual(label + TEXT(": largest w + P1"), population.Groups.LaneMaxPeakMps[lane], expected[lane]);
        TestTrue(label + TEXT(": at most the axis full-effort peak"),
            population.Groups.LaneMaxPeakMps[lane] <= axisPeak * (1.0 + 1.0e-6));
        TestTrue(label + TEXT(": close to it with thousands of ships"),
            population.Groups.LaneMaxPeakMps[lane] >= 0.95 * axisPeak);
    }

    // Partitions merged, max semantics, other kinds and groups ignored
    TArray<double> merged;
    merged.Init(0.0, 2);
    const int32 total = population.Rails.Num();
    const TConstArrayView<FSOLNpcRail> rails = population.Rails;
    SOLNpcRail::AccumulateLaneMaxPeak(rails.Slice(0, total / 3), merged);
    SOLNpcRail::AccumulateLaneMaxPeak(rails.Slice(total / 3, total - total / 3), merged);
    TestTrue(TEXT("partition merged = whole"), merged[0] == expected[0] && merged[1] == expected[1]);
    TArray<double> high;
    high.Init(1.0e9, 2);
    SOLNpcRail::AccumulateLaneMaxPeak(rails, high);
    TestTrue(TEXT("a larger value is kept"), high[0] == 1.0e9 && high[1] == 1.0e9);
    FSOLNpcRail cluster = population.Rails[0];
    cluster.Kind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Cluster);
    cluster.P[1] = 9.0e9f;
    FSOLNpcRail stray = population.Rails[0];
    stray.Group = 7;
    stray.P[1] = 9.0e9f;
    TArray<double> ignored;
    ignored.Init(0.0, 2);
    const FSOLNpcRail others[] = { cluster, stray };
    SOLNpcRail::AccumulateLaneMaxPeak(others, ignored);
    TestTrue(TEXT("cluster and out-of-range rails ignored"), ignored[0] == 0.0 && ignored[1] == 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficSpawnWakeBoundTest, "SOLTest.NpcTrafficSpawn.WakeBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contract case 12 through the rails: LaneWakeSpeedBoundMps(groups, frame, lane, endpoint speed) =
// SOLLaneTraffic::LaneShipSpeedBoundMps(the lane's largest peak, l0, the frame's current leg, H Rw, endpoint speed),
// and it is at or above every sampled rail's speed relative to body A: still bodies at l / l0 = 1 and 0.15, and Mars
// moving 30 km/s relative to Earth; the bound is not loose (a coasting peak ship reaches more than half of it)
bool FSOLNpcTrafficSpawnWakeBoundTest::RunTest(const FString& /*parameters*/)
{
    const double h = SOLNpcRail::RAIL_VELOCITY_STEP_S;
    for (const double ratio : { 1.0, 0.15 })
    {
        const FNpcTrafficSpawnPopulation population = NpcTrafficSpawnBuild(NPCTRAFFICSPAWN_SEED, 6000, ratio);
        const FSOLNpcRailFrame& frame = population.Frame;
        for (int32 lane = 0; lane < 2; ++lane)
        {
            const FSOLLaneProfile& profile = frame.LaneProfile[lane];
            const double bound = SOLNpcRail::LaneWakeSpeedBoundMps(population.Groups, frame, lane, 0.0);
            TestEqual(*FString::Printf(TEXT("l/l0 %.2f lane %d: the 50a bound of the lane's values"), ratio, lane),
                bound, SOLLaneTraffic::LaneShipSpeedBoundMps(population.Groups.LaneMaxPeakMps[lane],
                    profile.NominalLegM, frame.LaneFrame[lane].CurrentLegM, profile.HaloOuterRadiusM, 0.0));
            double worstRatio = 0.0;
            for (int32 index = 0; index < population.Rails.Num(); index += 9)
            {
                const FSOLNpcRail& rail = population.Rails[index];
                if (rail.Group != lane)
                {
                    continue;
                }
                const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);
                const FSOLLaneCycle cycle = SOLLaneTraffic::MakeCycle(SOLLaneTraffic::LaneShipLegS(profile, ship),
                    profile.UTurnDepthM);
                for (int32 sample = 0; sample < 40; ++sample)
                {
                    FSOLNpcRailFrame at = frame;
                    at.RailTimeS = (static_cast<double>(sample) + 0.5) / 40.0 * cycle.CycleS;
                    worstRatio = FMath::Max(worstRatio, SOLNpcRail::VelocityMps(rail, at).Size() / bound);
                }
            }
            TestTrue(*FString::Printf(TEXT("l/l0 %.2f lane %d: speeds within the bound (worst %.6f)"), ratio, lane,
                worstRatio), worstRatio <= 1.0);
            if (ratio == 1.0)
            {
                TestTrue(*FString::Printf(TEXT("lane %d: the bound is not loose (worst %.3f)"), lane, worstRatio),
                    worstRatio > 0.5);
            }
        }
    }

    // Moving bodies: Earth 30 km/s one way, Mars another; the speed of the rail relative to Earth
    const FNpcTrafficSpawnPopulation population = NpcTrafficSpawnBuild(NPCTRAFFICSPAWN_SEED, 6000, 0.6);
    const FVector3d earthVelocity(0.0, 3.0e4, 0.0);
    const FVector3d marsVelocity(2.0e4, -1.0e4, 5.0e3);
    const double endpointSpeed = (marsVelocity - earthVelocity).Size();
    double worstRatio = 0.0;
    for (int32 index = 0; index < population.Rails.Num(); index += 13)
    {
        const FSOLNpcRail& rail = population.Rails[index];
        if (rail.Group != 0)
        {
            continue;
        }
        for (const double t : { 1.0e3, 2.9e5, 1.7e6, 6.1e6 })
        {
            // Returns the rail position relative to Earth with the bodies moved to time tt
            const auto relative = [&](const double tt)
            {
                TArray<FVector3d> bodies = population.Bodies;
                bodies[0] += earthVelocity * tt;
                bodies[1] += marsVelocity * tt;
                FSOLNpcRailFrame moved;
                SOLNpcRail::BuildFrame(population.Groups, bodies, tt, moved);
                return SOLNpcRail::EvaluateM(rail, moved) - bodies[0];
            };
            TArray<FVector3d> bodiesNow = population.Bodies;
            bodiesNow[0] += earthVelocity * t;
            bodiesNow[1] += marsVelocity * t;
            FSOLNpcRailFrame now;
            SOLNpcRail::BuildFrame(population.Groups, bodiesNow, t, now);
            const double speed = (relative(t + h) - relative(t - h)).Size() / (2.0 * h);
            worstRatio = FMath::Max(worstRatio, speed / SOLNpcRail::LaneWakeSpeedBoundMps(population.Groups, now, 0,
                endpointSpeed));
        }
    }
    TestTrue(*FString::Printf(TEXT("moving bodies: speeds within the bound (worst %.6f)"), worstRatio),
        worstRatio <= 1.0);

    // An out-of-range lane gives a finite, non-negative value
    const double outside = SOLNpcRail::LaneWakeSpeedBoundMps(population.Groups, population.Frame, 5, 0.0);
    TestTrue(TEXT("out-of-range lane: finite and >= 0"), FMath::IsFinite(outside) && outside >= 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficSpawnEdgeCasesTest, "SOLTest.NpcTrafficSpawn.EdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Edge cases of the population: an empty lane keeps a drivable profile at the minimum wall radius, a zero largest peak
// and a finite wake bound; a lane with one ship counts it on the right side of the wall and moves it; bad nominal
// lengths (zero, negative, NaN, infinite) give lanes that are not drivable, with finite rails at rest
bool FSOLNpcTrafficSpawnEdgeCasesTest::RunTest(const FString& /*parameters*/)
{
    // Empty lane: the Moon lane gets no weight
    {
        SOLNpcScale::FLayout layout = NpcTrafficSpawnLayout();
        layout.Lanes[1].Weight = 0.0;
        const SOLNpcScale::FCounts counts = NpcTrafficSpawnCounts(0, 2000, 0);
        TArray<int32> inWall;
        TArray<int32> halo;
        SOLNpcScale::CountLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, inWall, halo);
        TestTrue(TEXT("empty lane: counts 0"), inWall.Num() == 2 && inWall[1] == 0 && halo[1] == 0);
        FSOLNpcGroupTable groups;
        NpcTrafficSpawnGroups(layout, inWall, halo, groups);
        const FSOLLaneProfile& empty = groups.LaneProfile[1];
        TestEqual(TEXT("empty lane: Rw = R x NPC_TUNNEL_RADIUS_MIN_SCALE"), empty.WallRadiusM,
            layout.Lanes[1].RadiusM * SOL::NPC_TUNNEL_RADIUS_MIN_SCALE, 1.0e-9 * layout.Lanes[1].RadiusM);
        TestTrue(TEXT("empty lane: drivable"), empty.bDrivable);
        TestEqual(TEXT("empty lane: largest peak 0"), groups.LaneMaxPeakMps[1], 0.0);
        FSOLNpcRailFrame frame;
        SOLNpcRail::BuildFrame(groups, NpcTrafficSpawnBodies(groups, 1.0), 0.0, frame);
        const double bound = SOLNpcRail::LaneWakeSpeedBoundMps(groups, frame, 1, 0.0);
        TestTrue(TEXT("empty lane: wake bound finite and >= 0"), FMath::IsFinite(bound) && bound >= 0.0);
    }

    // One lane ship in the whole population
    {
        const SOLNpcScale::FLayout layout = NpcTrafficSpawnLayout();
        const SOLNpcScale::FCounts counts = NpcTrafficSpawnCounts(0, 1, 0);
        TArray<int32> inWall;
        TArray<int32> halo;
        SOLNpcScale::CountLaneSpawns(NPCTRAFFICSPAWN_SEED, counts, layout, inWall, halo);
        const SOLNpcScale::FSpawn spawn = SOLNpcScale::MakeSpawn(NPCTRAFFICSPAWN_SEED, 0, counts, layout,
            NpcTrafficSpawnBattle());
        TestTrue(TEXT("one ship: on a lane"), spawn.GroupIndex == 0 || spawn.GroupIndex == 1);
        if (spawn.GroupIndex == 0 || spawn.GroupIndex == 1)
        {
            TestEqual(TEXT("one ship: counted once"), inWall[0] + inWall[1] + halo[0] + halo[1], 1);
            TestEqual(TEXT("one ship: on its side of the wall"), spawn.bLaneHalo ? halo[spawn.GroupIndex]
                : inWall[spawn.GroupIndex], 1);
            FSOLNpcGroupTable groups;
            NpcTrafficSpawnGroups(layout, inWall, halo, groups);
            TestEqual(TEXT("one ship: Rw from its in-wall count"), groups.LaneProfile[spawn.GroupIndex].WallRadiusM,
                SOLNpcTunnel::TunnelRadiusM(layout.Lanes[spawn.GroupIndex].RadiusM, inWall[spawn.GroupIndex]),
                1.0e-9 * layout.Lanes[spawn.GroupIndex].RadiusM);
            const FSOLNpcRail rail = SOLNpcRail::FromSpawn(spawn, 0, groups.LaneProfile[spawn.GroupIndex], 0.0);
            FSOLNpcRailFrame frame;
            SOLNpcRail::BuildFrame(groups, NpcTrafficSpawnBodies(groups, 1.0), 0.0, frame);
            const FVector3d first = SOLNpcRail::EvaluateAtTimeM(rail, frame, 0.0);
            const FVector3d later = SOLNpcRail::EvaluateAtTimeM(rail, frame, 100.0);
            TestTrue(TEXT("one ship: finite and moving"), FMath::IsFinite(first.X) && FMath::IsFinite(later.X)
                && (later - first).Size() > 1.0);
        }
    }

    // Bad nominal lengths: not drivable, finite rails at rest
    for (const double badLength : { 0.0, -4.0e8, NPCTRAFFICSPAWN_NAN, NPCTRAFFICSPAWN_INF })
    {
        const SOLNpcScale::FLayout layout = NpcTrafficSpawnLayout();
        FSOLNpcGroupTable groups;
        groups.LaneCount = 2;
        groups.LaneBodyA = { 0, 0 };
        groups.LaneBodyB = { 1, 2 };
        groups.LaneRadiusM = { layout.Lanes[0].RadiusM, layout.Lanes[1].RadiusM };
        const int32 inWall[] = { 3000, 300 };
        const int32 halo[] = { 1000, 100 };
        const double nominal[] = { badLength, badLength };
        const double radiusA[] = { NPCTRAFFICSPAWN_EARTH_RADIUS_M, NPCTRAFFICSPAWN_EARTH_RADIUS_M };
        const double radiusB[] = { NPCTRAFFICSPAWN_MARS_RADIUS_M, NPCTRAFFICSPAWN_MOON_RADIUS_M };
        SOLNpcRail::BuildLaneProfiles(groups, inWall, halo, nominal, radiusA, radiusB);
        const FString label = FString::Printf(TEXT("nominal length %f"), badLength);
        TestTrue(label + TEXT(": not drivable"), !groups.LaneProfile[0].bDrivable && !groups.LaneProfile[1].bDrivable);
        SOLNpcScale::FSpawn spawn;
        spawn.Kind = SOLNpcScale::EPlacementKind::Lane;
        spawn.GroupIndex = 0;
        spawn.LanePhase = 0.4;
        spawn.LaneEffort = 0.9;
        spawn.LaneRho = 0.5;
        spawn.LaneClockRad = 1.0;
        const FSOLNpcRail rail = SOLNpcRail::FromSpawn(spawn, 0, groups.LaneProfile[0], 0.0);
        bool bFinite = true;
        for (int32 index = 0; index < 7; ++index)
        {
            bFinite = bFinite && FMath::IsFinite(rail.P[index]);
        }
        TestTrue(label + TEXT(": finite rail"), bFinite);
        const TArray<FVector3d> bodies = { FVector3d(1.2e11, -8.0e10, 3.0e6), FVector3d(1.4e11, -8.0e10, 3.0e6),
            FVector3d(1.2e11, -7.9e10, 3.0e6) };
        FSOLNpcRailFrame frame;
        SOLNpcRail::BuildFrame(groups, bodies, 0.0, frame);
        const FVector3d atZero = SOLNpcRail::EvaluateAtTimeM(rail, frame, 0.0);
        TestTrue(label + TEXT(": finite and at rest"), FMath::IsFinite(atZero.X) && FMath::IsFinite(atZero.Y)
            && FMath::IsFinite(atZero.Z) && atZero == SOLNpcRail::EvaluateAtTimeM(rail, frame, 5.0e5));
        const double bound = SOLNpcRail::LaneWakeSpeedBoundMps(groups, frame, 0, 0.0);
        TestTrue(label + TEXT(": wake bound finite and >= 0"), FMath::IsFinite(bound) && bound >= 0.0);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
