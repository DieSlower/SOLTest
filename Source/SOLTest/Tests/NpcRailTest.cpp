/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcTrafficMirror.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Rail tests. The lane cases follow issue #50 part 50b (SDD 50 sections 4.4-4.7, contract case 10), written from the
// contract before the 50b code existed; the names of the API under test are the ones recorded in the plan's "Part 50b
// API contract (from the test author)" section. Lane rail layout: P0 phase (high float, in [0, 1)), P1 dv (m/s), P2 up
// offset (m), P3 right offset (m, >= 0), P4 t_a (s), P5 phase low float (phase = P0 + P5 in double), P6 reserved (0).
// The GPU cases evaluate the shared float mirror of Shaders/Private/SOLNpcTraffic.ush (Npc/SOLNpcTrafficMirror.h) on
// the GPU rail texels (SOLNpcRail::GpuRailTexels: a lane ship's cycle time at the epoch as hi / lo floats, its leg
// time).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCRAIL_PI = 3.141592653589793238462643383279502884;
    constexpr double NPCRAIL_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double NPCRAIL_INF = std::numeric_limits<double>::infinity();
    constexpr double NPCRAIL_POSITION_TOL_M = 1.0e-3;               // CPU positions near a body (double)

    // Mean body radii and aphelion distances (m); a lane's nominal length is the sum of its bodies' aphelia (D16)
    constexpr double NPCRAIL_MERCURY_RADIUS_M = 2.4397e6;
    constexpr double NPCRAIL_VENUS_RADIUS_M = 6.0518e6;
    constexpr double NPCRAIL_EARTH_RADIUS_M = 6.371e6;
    constexpr double NPCRAIL_MOON_RADIUS_M = 1.7374e6;
    constexpr double NPCRAIL_MARS_RADIUS_M = 3.3895e6;
    constexpr double NPCRAIL_JUPITER_RADIUS_M = 6.9911e7;
    constexpr double NPCRAIL_SATURN_RADIUS_M = 5.8232e7;
    constexpr double NPCRAIL_URANUS_RADIUS_M = 2.5362e7;
    constexpr double NPCRAIL_NEPTUNE_RADIUS_M = 2.4622e7;
    constexpr double NPCRAIL_MERCURY_APHELION_M = 6.982e10;
    constexpr double NPCRAIL_VENUS_APHELION_M = 1.0894e11;
    constexpr double NPCRAIL_EARTH_APHELION_M = 1.5210e11;
    constexpr double NPCRAIL_MARS_APHELION_M = 2.4923e11;
    constexpr double NPCRAIL_JUPITER_APHELION_M = 8.1662e11;
    constexpr double NPCRAIL_SATURN_APHELION_M = 1.5145e12;
    constexpr double NPCRAIL_URANUS_APHELION_M = 3.0064e12;
    constexpr double NPCRAIL_NEPTUNE_APHELION_M = 4.5372e12;
    constexpr double NPCRAIL_MOON_APOAPSIS_M = 4.055e8;

    // The test lane (Earth - Mars): lane radius, in-wall and halo counts, nominal length, current body distance
    constexpr double NPCRAIL_LANE_RADIUS_M = 3.0e6;
    constexpr int32 NPCRAIL_IN_WALL = 30000;
    constexpr int32 NPCRAIL_HALO = 10000;
    constexpr double NPCRAIL_NOMINAL_LENGTH_M = NPCRAIL_EARTH_APHELION_M + NPCRAIL_MARS_APHELION_M;
    constexpr double NPCRAIL_BODY_DISTANCE_M = 2.3e11;

    // A lane ship's dimensionless spawn values
    struct FNpcRailSpec
    {
        double Phase;
        double Effort;
        double Rho;
        double ClockRad;
    };

    // Sample ships of the lane cases: inside the wall, on the axis, near the wall, in the halo's ramp, in the halo,
    // and a phase that rounds to 1 in float
    const FNpcRailSpec NPCRAIL_SPECS[] = {
        { 0.37, 0.83, 0.62, 1.1 },
        { 0.0, 1.0, 0.0, 0.5 * NPCRAIL_PI },
        { 0.91, 0.55, 0.97, 2.6 },
        { 0.123, 0.7, 1.004, 0.4 },
        { 0.6, 0.9, 1.6, 0.5 * NPCRAIL_PI },
        { 0.99999999, 0.5, 1.95, 3.0 },
    };

    // The rail test world: the group table (one lane, one cluster, one battle) and the registry body positions
    struct FNpcRailWorld
    {
        FSOLNpcGroupTable Groups;
        TArray<FVector3d> Bodies;           // 0 lane A, 1 lane B, 2 cluster body, 3 battle body
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the rail test world: Earth - Mars at bodyDistanceM (lane group 0, profile from BuildLaneProfiles), a
    // cluster body (group 1) and a battle body (group 2)
    FNpcRailWorld NpcRailMakeWorld(const double bodyDistanceM = NPCRAIL_BODY_DISTANCE_M)
    {
        FNpcRailWorld world;
        const FVector3d aM(1.2e11, -8.0e10, 3.0e6);
        world.Bodies = { aM, aM + FVector3d(0.6, 0.8, 0.0) * bodyDistanceM, FVector3d(1.0e11, 2.0e10, 0.0),
            FVector3d(4.9e10, 0.0, 0.0) };
        FSOLNpcGroupTable& groups = world.Groups;
        groups.LaneCount = 1;
        groups.ClusterCount = 1;
        groups.BattleCount = 1;
        groups.LaneBodyA = { 0 };
        groups.LaneBodyB = { 1 };
        groups.LaneRadiusM = { NPCRAIL_LANE_RADIUS_M };
        groups.ClusterBody = { 2 };
        groups.ClusterInnerM = { 7.0e6 };
        groups.ClusterOuterM = { 9.0e6 };
        groups.BattleCenterFromBodyM = { FVector3d(1.0e9, 0.0, 0.0) };
        groups.BattleBody = 3;
        groups.BattleRadiusM = 2.0e4;
        const int32 inWall[] = { NPCRAIL_IN_WALL };
        const int32 halo[] = { NPCRAIL_HALO };
        const double nominal[] = { NPCRAIL_NOMINAL_LENGTH_M };
        const double radiusA[] = { NPCRAIL_EARTH_RADIUS_M };
        const double radiusB[] = { NPCRAIL_MARS_RADIUS_M };
        SOLNpcRail::BuildLaneProfiles(groups, inWall, halo, nominal, radiusA, radiusB);
        return world;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the world's frame at a rail time
    FSOLNpcRailFrame NpcRailFrameAt(const FNpcRailWorld& world, const double railTimeS)
    {
        FSOLNpcRailFrame frame;
        SOLNpcRail::BuildFrame(world.Groups, world.Bodies, railTimeS, frame);
        return frame;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane spawn on lane 0 (faction 1) with the given dimensionless values
    SOLNpcScale::FSpawn NpcRailLaneSpawn(const FNpcRailSpec& spec)
    {
        SOLNpcScale::FSpawn spawn;
        spawn.Kind = SOLNpcScale::EPlacementKind::Lane;
        spawn.GroupIndex = 0;
        spawn.Faction = 1;
        spawn.LanePhase = spec.Phase;
        spawn.LaneEffort = spec.Effort;
        spawn.LaneRho = spec.Rho;
        spawn.LaneClockRad = spec.ClockRad;
        spawn.bLaneHalo = spec.Rho >= 1.0;
        return spawn;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the lane rail of a spec on the world's lane (lanes are the first groups, so group base 0)
    FSOLNpcRail NpcRailLane(const FNpcRailWorld& world, const FNpcRailSpec& spec)
    {
        return SOLNpcRail::FromSpawn(NpcRailLaneSpawn(spec), 0, world.Groups.LaneProfile[0], 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a cluster rail (group 1) on a circular orbit of radius r with a phase and a mean motion (rad/s)
    FSOLNpcRail NpcRailCluster(const float radiusM, const float phaseRad, const float meanMotion)
    {
        FSOLNpcRail rail;
        rail.Kind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Cluster);
        rail.Group = 1;
        rail.P[0] = radiusM;
        rail.P[1] = 0.4f;
        rail.P[2] = 1.1f;
        rail.P[3] = phaseRad;
        rail.P[4] = meanMotion;
        return rail;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a battle rail (group 2) with a fixed slot offset from the battle centre
    FSOLNpcRail NpcRailBattle()
    {
        FSOLNpcRail rail;
        rail.Kind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Battle);
        rail.Group = 2;
        rail.P[0] = 120.0f;
        rail.P[1] = -40.0f;
        rail.P[2] = 9.0f;
        return rail;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a cluster ship's in-orbit angle as the GPU computes it: phase + mean motion * time, all in float
    float NpcRailGpuTheta(const FSOLNpcRail& rail, const double railTimeS)
    {
        return rail.P[3] + rail.P[4] * static_cast<float>(railTimeS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane ship's cycle from its rail constants and its lane profile (the leg time derived, SDD 50 section
    // 4.4)
    FSOLLaneCycle NpcRailCycleOf(const FSOLLaneProfile& profile, const FSOLNpcRail& rail)
    {
        const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);
        return SOLLaneTraffic::MakeCycle(SOLLaneTraffic::LaneShipLegS(profile, ship), profile.UTurnDepthM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rail time at which a lane rail is at cycle time tau (the chosen phase formula: tau = wrap(phase x
    // T_cyc + t, T_cyc), phase = P0 + P5)
    double NpcRailTimeAt(const FSOLNpcRail& rail, const FSOLLaneCycle& cycle, const double tau)
    {
        return tau - SOLNpcRail::LaneShipFromRail(rail).Phase * cycle.CycleS;
    }

    // A reference point on a nominal leg: the arc from the departure end ring and the speed
    struct FNpcRailLegRef
    {
        double ArcM = 0.0;
        double SpeedMps = 0.0;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's leg arc and speed of a ship's constants at leg time tl (SDD 50 section 4.4, evaluated from
    // the nearer end in time): accelerate at dv / t_a from w, coast at w + dv, mirror for the second half
    FNpcRailLegRef NpcRailLegRefAt(const FSOLLaneShip& ship, const double l0, const double legS, const double tl)
    {
        const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
        const double alpha = ship.AccelS > 0.0 ? ship.DvMps / ship.AccelS : 0.0;
        const double peak = w + ship.DvMps;
        const double accelM = ship.AccelS * (w + 0.5 * ship.DvMps);
        const double clamped = FMath::Clamp(tl, 0.0, legS);
        const bool bFirstHalf = clamped <= 0.5 * legS;
        const double s = bFirstHalf ? clamped : legS - clamped;
        const bool bAccelerating = s <= ship.AccelS;
        const double arc = bAccelerating ? w * s + 0.5 * alpha * s * s : accelM + peak * (s - ship.AccelS);
        FNpcRailLegRef ref;
        ref.ArcM = bFirstHalf ? arc : l0 - arc;
        ref.SpeedMps = bAccelerating ? w + alpha * s : peak;
        return ref;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the float spacing (ulp) at a magnitude
    double NpcRailFloatUlp(const double magnitude)
    {
        const float value = static_cast<float>(FMath::Abs(magnitude));
        return static_cast<double>(std::nextafter(value, std::numeric_limits<float>::infinity()) - value);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane ship's position relative to the viewpoint as the point shader computes it, all in float: the
    // shared mirror of SOLNpcTraffic.ush (SOLNpcTrafficMirror::LaneShipRelM) on the rail's GPU texels at its own epoch
    // (SOLNpcRail::GpuRailTexels, delta 0); rows are the lane column's six group texels, timeS the rail time since the
    // GPU epoch
    FVector3f NpcRailGpuLaneRelM(const FSOLNpcRail& rail, const TConstArrayView<FSOLLaneProfile> laneProfiles,
        const FVector4f* rows, const float timeS)
    {
        FVector4f texelA;
        FVector4f texelB;
        SOLNpcRail::GpuRailTexels(rail, 0.0, laneProfiles, texelA, texelB);
        return SOLNpcTrafficMirror::LaneShipRelM(texelA, texelB, rows, timeS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills a lane column's six group texels as the visuals do: rows 0 and 1 the lane's bodies relative to the
    // viewpoint (double difference, then float), rows 2-5 from SOLNpcRail::WriteLaneTrafficTexels
    void NpcRailGpuRows(const FSOLNpcRailFrame& frame, const int32 lane, const FVector3d& viewpointM,
        TArray<FVector4f>& texels, FVector4f* outRows)
    {
        const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
        texels.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), width * SOL::NPC_GROUP_TEXTURE_ROWS);
        SOLNpcRail::WriteLaneTrafficTexels(frame, texels);
        for (int32 row = 0; row < SOL::NPC_GROUP_TEXTURE_ROWS; ++row)
        {
            outRows[row] = texels[row * width + lane];
        }
        const FVector3d aRel = frame.GroupPointAM[lane] - viewpointM;
        const FVector3d bRel = frame.GroupPointBM[lane] - viewpointM;
        outRows[0] = FVector4f(static_cast<float>(aRel.X), static_cast<float>(aRel.Y), static_cast<float>(aRel.Z),
            0.0f);
        outRows[1] = FVector4f(static_cast<float>(bRel.X), static_cast<float>(bRel.Y), static_cast<float>(bRel.Z),
            0.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a deterministic uniform value in [0, 1) for a key (splitmix64)
    double NpcRailUnit(const uint64 key)
    {
        uint64 z = key + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z = z ^ (z >> 31);
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns hashed sample ship 'index': three of four inside the wall, one of four in the halo
    FNpcRailSpec NpcRailHashedSpec(const uint64 laneKey, const int32 index)
    {
        const uint64 base = laneKey * 1000003ull + static_cast<uint64>(index) * 8ull;
        FNpcRailSpec spec;
        spec.Phase = NpcRailUnit(base);
        spec.Effort = SOL::NPC_LANE_EFFORT_MIN
            + (SOL::NPC_LANE_EFFORT_MAX - SOL::NPC_LANE_EFFORT_MIN) * NpcRailUnit(base + 1);
        const double h = NpcRailUnit(base + 2);
        spec.Rho = (index % 4 == 3) ? FMath::Sqrt(1.0 + 3.0 * h) : FMath::Sqrt(h);
        spec.ClockRad = NPCRAIL_PI * NpcRailUnit(base + 3);
        return spec;
    }

    // A default lane of the float mirror test: bodies, radii and nominal (maximum) length
    struct FNpcRailDefaultLane
    {
        const TCHAR* BodyA;
        const TCHAR* BodyB;
        double RadiusAM;
        double RadiusBM;
        double NominalLengthM;
    };

    // Every default lane (SOLNpcScale::DefaultLayout), with mean radii and aphelion sums
    const FNpcRailDefaultLane NPCRAIL_DEFAULT_LANES[] = {
        { TEXT("Mercury"), TEXT("Venus"), NPCRAIL_MERCURY_RADIUS_M, NPCRAIL_VENUS_RADIUS_M,
            NPCRAIL_MERCURY_APHELION_M + NPCRAIL_VENUS_APHELION_M },
        { TEXT("Venus"), TEXT("Earth"), NPCRAIL_VENUS_RADIUS_M, NPCRAIL_EARTH_RADIUS_M,
            NPCRAIL_VENUS_APHELION_M + NPCRAIL_EARTH_APHELION_M },
        { TEXT("Earth"), TEXT("Moon"), NPCRAIL_EARTH_RADIUS_M, NPCRAIL_MOON_RADIUS_M, NPCRAIL_MOON_APOAPSIS_M },
        { TEXT("Earth"), TEXT("Mars"), NPCRAIL_EARTH_RADIUS_M, NPCRAIL_MARS_RADIUS_M,
            NPCRAIL_EARTH_APHELION_M + NPCRAIL_MARS_APHELION_M },
        { TEXT("Mars"), TEXT("Jupiter"), NPCRAIL_MARS_RADIUS_M, NPCRAIL_JUPITER_RADIUS_M,
            NPCRAIL_MARS_APHELION_M + NPCRAIL_JUPITER_APHELION_M },
        { TEXT("Jupiter"), TEXT("Saturn"), NPCRAIL_JUPITER_RADIUS_M, NPCRAIL_SATURN_RADIUS_M,
            NPCRAIL_JUPITER_APHELION_M + NPCRAIL_SATURN_APHELION_M },
        { TEXT("Saturn"), TEXT("Uranus"), NPCRAIL_SATURN_RADIUS_M, NPCRAIL_URANUS_RADIUS_M,
            NPCRAIL_SATURN_APHELION_M + NPCRAIL_URANUS_APHELION_M },
        { TEXT("Uranus"), TEXT("Neptune"), NPCRAIL_URANUS_RADIUS_M, NPCRAIL_NEPTUNE_RADIUS_M,
            NPCRAIL_URANUS_APHELION_M + NPCRAIL_NEPTUNE_APHELION_M },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a one-lane world for a default lane: the layout's lane radius, 100k ships inside the wall, the bodies at
    // lengthRatio x the nominal length apart (A far from the origin; Earth - Moon inclined 7 degrees); false if the
    // lane is not in the default layout
    bool NpcRailDefaultWorld(const FNpcRailDefaultLane& row, const double lengthRatio, FNpcRailWorld& outWorld)
    {
        const SOLNpcScale::FLayout layout = SOLNpcScale::DefaultLayout();
        const SOLNpcScale::FLaneDef* def = layout.Lanes.FindByPredicate([&row](const SOLNpcScale::FLaneDef& lane)
        {
            return lane.BodyA == FName(row.BodyA) && lane.BodyB == FName(row.BodyB);
        });
        if (def == nullptr)
        {
            return false;
        }
        const FVector3d aM(1.2e11, -8.0e10, 3.0e6);
        const bool bMoon = FName(row.BodyB) == FName(TEXT("Moon"));
        const double elevation = FMath::DegreesToRadians(bMoon ? 7.0 : 0.0);
        const FVector3d direction(0.6 * FMath::Cos(elevation), 0.8 * FMath::Cos(elevation), FMath::Sin(elevation));
        outWorld = FNpcRailWorld();
        outWorld.Bodies = { aM, aM + direction * (lengthRatio * row.NominalLengthM) };
        FSOLNpcGroupTable& groups = outWorld.Groups;
        groups.LaneCount = 1;
        groups.LaneBodyA = { 0 };
        groups.LaneBodyB = { 1 };
        groups.LaneRadiusM = { def->RadiusM };
        const int32 inWall[] = { 100000 };
        const int32 halo[] = { 33000 };
        const double nominal[] = { row.NominalLengthM };
        const double radiusA[] = { row.RadiusAM };
        const double radiusB[] = { row.RadiusBM };
        SOLNpcRail::BuildLaneProfiles(groups, inWall, halo, nominal, radiusA, radiusB);
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailEpochMatchesTest, "SOLTest.NpcRail.EpochMatches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A rail re-based by delta, evaluated at t, lands where the original lands at t + delta (up to the float rounding of
// the wrapped phase), and its phase stays inside one period. Lane rails (issue #50): the phase advances by delta /
// T_cyc in double (T_cyc from the rail's own constants and its lane profile) and is split into P0 (high, in [0, 1))
// and P5 (low), so the re-based rail is exact to about 1e-14 of a cycle; P1-P4 and P6 are kept
bool FSOLNpcRailEpochMatchesTest::RunTest(const FString& /*parameters*/)
{
    const FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLNpcRailFrame frame0 = NpcRailFrameAt(world, 0.0);
    const TConstArrayView<FSOLLaneProfile> profiles = frame0.LaneProfile;
    const FSOLLaneProfile& profile = world.Groups.LaneProfile[0];
    const double l0 = profile.NominalLegM;
    const double l = frame0.LaneFrame[0].CurrentLegM;
    const double slope = FMath::Max(1.0, (l - profile.MetricZoneM) / (l0 - profile.MetricZoneM));
    const FSOLNpcRail cluster = NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f);
    const FSOLNpcRail battle = NpcRailBattle();

    for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
    {
        const FSOLNpcRail lane = NpcRailLane(world, spec);
        const FSOLLaneCycle cycle = NpcRailCycleOf(profile, lane);
        const double peak = SOL::NPC_LANE_WALL_SPEED_MPS + static_cast<double>(lane.P[1]);
        const double laneTolM = 4.0e-14 * cycle.CycleS * peak * slope + 2.0e-2;
        const FString specLabel = FString::Printf(TEXT("rho %.3f phase %.8f"), spec.Rho, spec.Phase);
        for (const double deltaS : { 0.0, 1.0e4, 3.3e5, 2.6e6, 1.0e7, -5.0e3, 0.37 * cycle.CycleS })
        {
            const FSOLNpcRail advanced = SOLNpcRail::AdvancedToEpoch(lane, deltaS, profiles);
            const FString label = specLabel + FString::Printf(TEXT(" delta %.0f"), deltaS);
            TestTrue(label + TEXT(": P0 in [0, 1)"), advanced.P[0] >= 0.0f && advanced.P[0] < 1.0f);
            TestTrue(label + TEXT(": |P5| within a float step of P0"), FMath::Abs(advanced.P[5]) <= 5.97e-8f);
            const double phase = static_cast<double>(advanced.P[0]) + static_cast<double>(advanced.P[5]);
            TestTrue(label + TEXT(": P0 + P5 in [0, 1)"), phase >= 0.0 && phase < 1.0);
            for (const int32 kept : { 1, 2, 3, 4, 6 })
            {
                TestTrue(label + FString::Printf(TEXT(": P%d kept"), kept), advanced.P[kept] == lane.P[kept]);
            }

            // Evaluated at t after the new epoch: where the original is at t + delta
            for (const double t : { 0.0, 250.0, 9000.0 })
            {
                const double errorM = (SOLNpcRail::EvaluateM(advanced, NpcRailFrameAt(world, t))
                    - SOLNpcRail::EvaluateM(lane, NpcRailFrameAt(world, t + deltaS))).Size();
                TestTrue(label + FString::Printf(TEXT(" t %.0f: lane within %.3e m (%.3e m)"), t, laneTolM, errorM),
                    errorM <= laneTolM);
            }
        }
    }

    // Cluster and battle rails: unchanged behaviour (the lane profiles are ignored)
    for (const double deltaS : { 0.0, 1.0e4, 3.3e5, 1.0e7, -5.0e3 })
    {
        const FSOLNpcRail clusterAdvanced = SOLNpcRail::AdvancedToEpoch(cluster, deltaS, profiles);
        TestTrue(*FString::Printf(TEXT("delta %.0f: cluster phase in one period"), deltaS),
            clusterAdvanced.P[3] >= 0.0f && clusterAdvanced.P[3] <= static_cast<float>(UE_DOUBLE_TWO_PI));
        TestEqual(TEXT("Mean motion is kept"), clusterAdvanced.P[4], cluster.P[4]);
        for (const double t : { 0.0, 250.0, 9000.0 })
        {
            const FSOLNpcRailFrame later = NpcRailFrameAt(world, t + deltaS);
            const FSOLNpcRailFrame local = NpcRailFrameAt(world, t);

            // Cluster: half a float ulp of a phase in [0, 2 pi) times the radius
            const double clusterErrorM = (SOLNpcRail::EvaluateM(clusterAdvanced, local)
                - SOLNpcRail::EvaluateM(cluster, later)).Size();
            TestTrue(*FString::Printf(TEXT("delta %.0f t %.0f: cluster within 4 m (%.3f m)"), deltaS, t,
                clusterErrorM), clusterErrorM <= 4.0);

            // Battle: unchanged
            TestEqual(TEXT("Battle rail unchanged"), SOLNpcRail::EvaluateM(SOLNpcRail::AdvancedToEpoch(battle, deltaS,
                profiles), local), SOLNpcRail::EvaluateM(battle, local));
        }
    }

    // A non-finite delta leaves every rail as it was
    const FSOLNpcRail lane = NpcRailLane(world, NPCRAIL_SPECS[0]);
    for (const double badDelta : { NPCRAIL_NAN, NPCRAIL_INF, -NPCRAIL_INF })
    {
        const FSOLNpcRail sameLane = SOLNpcRail::AdvancedToEpoch(lane, badDelta, profiles);
        const FSOLNpcRail sameCluster = SOLNpcRail::AdvancedToEpoch(cluster, badDelta, profiles);
        TestTrue(TEXT("Non-finite delta keeps the lane phase"),
            sameLane.P[0] == lane.P[0] && sameLane.P[5] == lane.P[5]);
        TestEqual(TEXT("Non-finite delta keeps the cluster phase"), sameCluster.P[3], cluster.P[3]);
    }

    // A lane rail whose group has no profile is left as it is
    const FSOLNpcRail orphan = SOLNpcRail::AdvancedToEpoch(lane, 1.0e5, TConstArrayView<FSOLLaneProfile>());
    TestTrue(TEXT("No lane profile: rail unchanged"), orphan.P[0] == lane.P[0] && orphan.P[5] == lane.P[5]);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailEvaluateAtTimeTest, "SOLTest.NpcRail.EvaluateAtTimeMatches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// EvaluateAtTimeM at the frame's rail time is EvaluateM exactly, and at another time it matches a frame with that rail
// time and the same group points (issue #18 Part 18c contract item 10; the lane rail is issue #50's)
bool FSOLNpcRailEvaluateAtTimeTest::RunTest(const FString& /*parameters*/)
{
    const FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLNpcRail rails[] = { NpcRailLane(world, NPCRAIL_SPECS[0]), NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f),
        NpcRailBattle() };
    const TCHAR* names[] = { TEXT("Lane"), TEXT("Cluster"), TEXT("Battle") };
    for (int32 railIndex = 0; railIndex < 3; ++railIndex)
    {
        const FSOLNpcRail& rail = rails[railIndex];
        for (const double t : { 0.0, 250.0, 3000.0, 9000.0 })
        {
            const FSOLNpcRailFrame frame = NpcRailFrameAt(world, t);
            const bool bExact = SOLNpcRail::EvaluateAtTimeM(rail, frame, frame.RailTimeS)
                == SOLNpcRail::EvaluateM(rail, frame);
            TestTrue(*FString::Printf(TEXT("%s t %.0f: EvaluateAtTimeM(RailTimeS) == EvaluateM exactly"),
                names[railIndex], t), bExact);

            // Group points held, time moved: the same as a frame at that time (identical inputs, so rounding-level)
            for (const double otherS : { t + 0.5, t - 0.5, t + 1234.5 })
            {
                const FVector3d atOther = SOLNpcRail::EvaluateAtTimeM(rail, frame, otherS);
                const FVector3d viaFrame = SOLNpcRail::EvaluateM(rail, NpcRailFrameAt(world, otherS));
                TestTrue(*FString::Printf(TEXT("%s t %.0f: EvaluateAtTimeM(%.1f) == EvaluateM of that frame"),
                    names[railIndex], t, otherS),
                    (atOther - viaFrame).Size() <= 1.0e-12 * FMath::Max(1.0, viaFrame.Size()));
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailVelocityTest, "SOLTest.NpcRail.RailVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rail velocity is the central difference at RAIL_VELOCITY_STEP_S: zero for a battle, tangential at r * n for a
// cluster (issue #18 Part 18c contract item 11). A lane ship on a leg (issue #50, contract case 10) moves along the
// lane axis (+axis outbound, -axis inbound, no lateral part) at its profile speed times the metric map's slope,
// X'(u) x v(u), within 1e-3
bool FSOLNpcRailVelocityTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("RAIL_VELOCITY_STEP_S"), SOLNpcRail::RAIL_VELOCITY_STEP_S, 0.5);
    const FNpcRailWorld world = NpcRailMakeWorld();
    const double h = SOLNpcRail::RAIL_VELOCITY_STEP_S;

    // Returns an independent central difference of EvaluateAtTimeM at the frame's time with step stepS
    const auto centralDifference = [](const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame, const double stepS)
    {
        return (SOLNpcRail::EvaluateAtTimeM(rail, frame, frame.RailTimeS + stepS)
            - SOLNpcRail::EvaluateAtTimeM(rail, frame, frame.RailTimeS - stepS)) / (2.0 * stepS);
    };

    // Battle: exactly zero
    const FSOLNpcRail battle = NpcRailBattle();
    for (const double t : { 0.0, 250.0, 1.0e6 })
    {
        TestTrue(*FString::Printf(TEXT("Battle t %.0f: exactly zero"), t),
            SOLNpcRail::VelocityMps(battle, NpcRailFrameAt(world, t)) == FVector3d::ZeroVector);
    }

    // Cluster: tangential, |v| = r * n within 0.1% (the central difference shortens it by sin(nh) / (nh) ~ 1 - 5e-8)
    const FSOLNpcRail cluster = NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f);
    const double expectedSpeed = static_cast<double>(cluster.P[0]) * static_cast<double>(cluster.P[4]);
    for (const double t : { 0.0, 250.0, 4321.0 })
    {
        const FSOLNpcRailFrame frame = NpcRailFrameAt(world, t);
        const FVector3d velocity = SOLNpcRail::VelocityMps(cluster, frame);
        const FVector3d radius = SOLNpcRail::EvaluateM(cluster, frame) - frame.GroupPointAM[1];
        const FString name = FString::Printf(TEXT("Cluster t %.0f"), t);
        TestTrue(*(name + TEXT(": |v| = r * n within 0.1%")),
            FMath::Abs(velocity.Size() - expectedSpeed) <= 1.0e-3 * expectedSpeed);

        // A symmetric chord of a circle is perpendicular to the radius; 1e-6 covers rounding at 1e11 m
        TestTrue(*(name + TEXT(": perpendicular to the radius")),
            FMath::Abs(FVector3d::DotProduct(velocity.GetSafeNormal(), radius.GetSafeNormal())) <= 1.0e-6);
        TestTrue(*(name + TEXT(": the definition (central difference at RAIL_VELOCITY_STEP_S)")),
            (velocity - centralDifference(cluster, frame, h)).Size() <= 1.0e-9 * expectedSpeed);

        // A finer independent difference differs only by the O((nh)^2) truncation (~5e-8 relative) and rounding
        TestTrue(*(name + TEXT(": matches a finer difference")),
            (velocity - centralDifference(cluster, frame, 0.05)).Size() <= 1.0e-6 * expectedSpeed);
    }

    // Lane: on both legs, away from the joints, at the accelerate, coast and brake phases
    const FSOLLaneProfile& profile = world.Groups.LaneProfile[0];
    const double l0 = profile.NominalLegM;
    for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
    {
        const FSOLNpcRail lane = NpcRailLane(world, spec);
        const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(lane);
        const FSOLLaneCycle cycle = NpcRailCycleOf(profile, lane);
        for (const double fraction : { 0.01, 0.3, 0.47, 0.71, 0.99 })
        {
            for (const bool bOutbound : { true, false })
            {
                const double legStart = bOutbound ? 0.0 : cycle.LegS + cycle.TurnS;
                const double tl = fraction * cycle.LegS;
                const FSOLNpcRailFrame frame = NpcRailFrameAt(world, NpcRailTimeAt(lane, cycle, legStart + tl));
                const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
                const FNpcRailLegRef ref = NpcRailLegRefAt(ship, l0, cycle.LegS, tl);
                const double expected = ref.SpeedMps * SOLLaneTraffic::MetricMapSlope(ref.ArcM, l0,
                    laneFrame.CurrentLegM);
                const FVector3d velocity = SOLNpcRail::VelocityMps(lane, frame);
                const FString name = FString::Printf(TEXT("Lane rho %.3f %s leg at %.2f"), spec.Rho,
                    bOutbound ? TEXT("outbound") : TEXT("inbound"), fraction);
                TestTrue(*(name + FString::Printf(TEXT(": |v| = X'(u) v(u) within 1e-3 (%.6e, expected %.6e)"),
                    velocity.Size(), expected)), FMath::Abs(velocity.Size() - expected) <= 1.0e-3 * expected);
                const double along = FVector3d::DotProduct(velocity, laneFrame.Axis) * (bOutbound ? 1.0 : -1.0);
                TestTrue(*(name + TEXT(": along the axis in the travel direction")),
                    along >= velocity.Size() * (1.0 - 1.0e-6));
                TestTrue(*(name + TEXT(": the definition (central difference at RAIL_VELOCITY_STEP_S)")),
                    (velocity - centralDifference(lane, frame, h)).Size() <= 1.0e-9 * expected);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailBuildFrameGroupBodyTest, "SOLTest.NpcRail.BuildFrameGroupBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BuildFrame fills GroupBodyM: lane = body A, cluster = its body, battle = the battle body, zero for an invalid
// index (issue #18 Part 18c contract item 12)
bool FSOLNpcRailBuildFrameGroupBodyTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> bodies = { FVector3d(1.0, 2.0, 3.0), FVector3d(1.0e11, -4.0e10, 5.0e8),
        FVector3d(-2.0e11, 7.0e10, -1.0e9), FVector3d(3.0e10, 3.0e10, 3.0e10) };

    // Two lanes (the second with an unknown body A), one cluster, one battle
    FSOLNpcGroupTable groups;
    groups.LaneCount = 2;
    groups.ClusterCount = 1;
    groups.BattleCount = 1;
    groups.LaneBodyA = { 1, INDEX_NONE };
    groups.LaneBodyB = { 2, 0 };
    groups.LaneRadiusM = { 1.0e5, 1.0e5 };
    groups.ClusterBody = { 3 };
    groups.ClusterInnerM = { 7.0e6 };
    groups.ClusterOuterM = { 9.0e6 };
    groups.BattleCenterFromBodyM = { FVector3d(1.0e5, 0.0, 0.0) };
    groups.BattleBody = 2;
    groups.BattleRadiusM = 2.0e4;

    FSOLNpcRailFrame frame;
    SOLNpcRail::BuildFrame(groups, bodies, 100.0, frame);
    TestEqual(TEXT("One body point per group"), frame.GroupBodyM.Num(), groups.Num());
    if (frame.GroupBodyM.Num() == 4)
    {
        TestTrue(TEXT("Lane 0: body A"), frame.GroupBodyM[0] == bodies[1]);
        TestTrue(TEXT("Lane 1: unknown body A gives zero"), frame.GroupBodyM[1] == FVector3d::ZeroVector);
        TestTrue(TEXT("Cluster: its body"), frame.GroupBodyM[2] == bodies[3]);
        TestTrue(TEXT("Battle: the battle body"), frame.GroupBodyM[3] == bodies[2]);
    }

    // Out-of-range and missing indices give zero; rebuilding into the same frame does not append
    groups.LaneBodyA = { 7, 0 };
    groups.ClusterBody = { 99 };
    groups.BattleBody = INDEX_NONE;
    SOLNpcRail::BuildFrame(groups, bodies, 200.0, frame);
    TestEqual(TEXT("Rebuilt: one body point per group"), frame.GroupBodyM.Num(), groups.Num());
    if (frame.GroupBodyM.Num() == 4)
    {
        TestTrue(TEXT("Lane 0: out-of-range body A gives zero"), frame.GroupBodyM[0] == FVector3d::ZeroVector);
        TestTrue(TEXT("Lane 1: body A"), frame.GroupBodyM[1] == bodies[0]);
        TestTrue(TEXT("Cluster: out-of-range body gives zero"), frame.GroupBodyM[2] == FVector3d::ZeroVector);
        TestTrue(TEXT("Battle: no battle body gives zero"), frame.GroupBodyM[3] == FVector3d::ZeroVector);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailBuildFrameLanesTest, "SOLTest.NpcRail.BuildFrameLanes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #50 (SDD 50 section 4.7): BuildFrame also fills one lane profile (a copy of the group table's frozen one) and
// one lane frame (SOLLaneTraffic::MakeLaneFrame of that profile and the lane's two bodies: basis, current leg, end
// shift) per lane; an unresolved body gives an invalid lane frame; a group table without profiles gives default
// (not drivable) profiles; a rebuild with the same sizes does not reallocate
bool FSOLNpcRailBuildFrameLanesTest::RunTest(const FString& /*parameters*/)
{
    FNpcRailWorld world = NpcRailMakeWorld();
    FSOLNpcRailFrame frame = NpcRailFrameAt(world, 50.0);
    if (!TestEqual(TEXT("one lane profile per lane"), frame.LaneProfile.Num(), world.Groups.LaneCount)
        || !TestEqual(TEXT("one lane frame per lane"), frame.LaneFrame.Num(), world.Groups.LaneCount))
    {
        return false;
    }
    const FSOLLaneProfile& expectedProfile = world.Groups.LaneProfile[0];
    const FSOLLaneProfile& profile = frame.LaneProfile[0];
    TestTrue(TEXT("profile copied from the group table"), profile.WallRadiusM == expectedProfile.WallRadiusM
        && profile.HaloOuterRadiusM == expectedProfile.HaloOuterRadiusM
        && profile.NominalLengthM == expectedProfile.NominalLengthM
        && profile.NominalLegM == expectedProfile.NominalLegM
        && profile.EndArcAM == expectedProfile.EndArcAM && profile.EndArcBM == expectedProfile.EndArcBM
        && profile.UTurnDepthM == expectedProfile.UTurnDepthM && profile.UTurnS == expectedProfile.UTurnS
        && profile.MetricZoneM == expectedProfile.MetricZoneM && profile.bDrivable == expectedProfile.bDrivable);
    const FSOLLaneFrame expected = SOLLaneTraffic::MakeLaneFrame(expectedProfile, world.Bodies[0], world.Bodies[1]);
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
    TestTrue(TEXT("lane frame valid"), laneFrame.bValid);
    TestTrue(TEXT("lane frame = MakeLaneFrame(profile, A, B)"), laneFrame.AM == expected.AM
        && laneFrame.BM == expected.BM && laneFrame.Axis == expected.Axis && laneFrame.Up == expected.Up
        && laneFrame.Right == expected.Right && laneFrame.CurrentLegM == expected.CurrentLegM
        && laneFrame.EndShiftM == expected.EndShiftM);

    // The body moves: the lane frame follows, the arrays are kept
    const FSOLLaneFrame* dataBefore = frame.LaneFrame.GetData();
    world.Bodies[1] += FVector3d(0.0, 0.0, 4.0e9);
    SOLNpcRail::BuildFrame(world.Groups, world.Bodies, 60.0, frame);
    TestTrue(TEXT("rebuild: same lane frame array"), frame.LaneFrame.GetData() == dataBefore);
    TestTrue(TEXT("rebuild: the lane frame follows B"), frame.LaneFrame[0].BM == world.Bodies[1]
        && frame.LaneFrame[0].Up == SOLLaneTraffic::MakeLaneFrame(expectedProfile, world.Bodies[0],
            world.Bodies[1]).Up);

    // Unresolved body: invalid lane frame
    FNpcRailWorld unresolved = NpcRailMakeWorld();
    unresolved.Groups.LaneBodyB = { INDEX_NONE };
    TestFalse(TEXT("unknown body B: lane frame invalid"), NpcRailFrameAt(unresolved, 0.0).LaneFrame[0].bValid);
    unresolved.Groups.LaneBodyA = { 17 };
    unresolved.Groups.LaneBodyB = { 1 };
    TestFalse(TEXT("out-of-range body A: lane frame invalid"), NpcRailFrameAt(unresolved, 0.0).LaneFrame[0].bValid);

    // No profiles in the group table: default profiles, not drivable, one per lane
    FNpcRailWorld bare = NpcRailMakeWorld();
    bare.Groups.LaneProfile.Reset();
    const FSOLNpcRailFrame bareFrame = NpcRailFrameAt(bare, 0.0);
    TestTrue(TEXT("no profiles: one default profile per lane"), bareFrame.LaneProfile.Num() == 1
        && !bareFrame.LaneProfile[0].bDrivable);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailGpuPrecisionTest, "SOLTest.NpcRail.GpuFloatPrecision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Measurement behind NPC_GPU_RAIL_EPOCH_S: for the fastest cluster orbit (low Earth orbit), the GPU's float angle
// drifts kilometres from the double one some weeks of sim time after the epoch, and stays within metres while the
// rail time is kept within NPC_GPU_RAIL_EPOCH_S of a re-based epoch
bool FSOLNpcRailGpuPrecisionTest::RunTest(const FString& /*parameters*/)
{
    const double radiusM = 7.0e6;
    const FSOLNpcRail rail = NpcRailCluster(static_cast<float>(radiusM), 0.9f, 1.08e-3f);

    // Returns the worst position error (m) of the float angle over rail times [start, start + span]
    const auto worstErrorM = [&radiusM](const FSOLNpcRail& gpuRail, const double startS, const double spanS)
    {
        double worst = 0.0;
        for (int32 sample = 0; sample <= 200; ++sample)
        {
            const double t = startS + spanS * sample / 200.0;
            const double exact = static_cast<double>(gpuRail.P[3]) + static_cast<double>(gpuRail.P[4]) * t;
            const double gpu = static_cast<double>(NpcRailGpuTheta(gpuRail, t));
            const double wrapped = FMath::Abs(FMath::Fmod(gpu - exact + UE_DOUBLE_PI * 3.0, UE_DOUBLE_TWO_PI)
                - UE_DOUBLE_PI);
            worst = FMath::Max(worst, wrapped * radiusM);
        }
        return worst;
    };

    // Without re-basing: some 46 sim days in, the float angle is off by kilometres
    const double driftM = worstErrorM(rail, 4.0e6, SOL::NPC_GPU_RAIL_EPOCH_S);
    AddInfo(FString::Printf(TEXT("Float angle error 4e6 s after the epoch: %.0f m"), driftM));
    TestTrue(TEXT("Without re-basing the GPU drifts by more than 1 km"), driftM > 1000.0);

    // Re-based at 4e6 s: the GPU sees only the time since that epoch
    const FSOLNpcRail rebased = SOLNpcRail::AdvancedToEpoch(rail, 4.0e6, TConstArrayView<FSOLLaneProfile>());
    const double rebasedM = worstErrorM(rebased, 0.0, SOL::NPC_GPU_RAIL_EPOCH_S);
    AddInfo(FString::Printf(TEXT("Float angle error within one epoch window: %.1f m"), rebasedM));
    TestTrue(TEXT("Within one epoch window the GPU stays within 50 m"), rebasedM < 50.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailLanePackingTest, "SOLTest.NpcRail.LanePacking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contract case 10 (rail packing): FromSpawn stores a lane ship's MakeLaneShip constants as floats, each rounded once
// from the double (P1 dv, P2 up offset, P3 right offset, P4 t_a), the phase split into P0 (the float nearest below 1
// at most, in [0, 1)) and P5 (the rest, so P0 + P5 is the phase to about 1e-14), P6 = 0; kind, faction and the global
// group (base + lane index) as before; LaneShipFromRail reads the constants back (phase P0 + P5 in double)
bool FSOLNpcRailLanePackingTest::RunTest(const FString& /*parameters*/)
{
    const FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLLaneProfile& profile = world.Groups.LaneProfile[0];
    const double rw = profile.WallRadiusM;
    TestTrue(TEXT("test lane drivable"), profile.bDrivable && rw > 0.0);
    for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
    {
        const FString label = FString::Printf(TEXT("rho %.3f phase %.8f"), spec.Rho, spec.Phase);
        const FSOLNpcRail rail = NpcRailLane(world, spec);
        const FSOLLaneShip exact = SOLLaneTraffic::MakeLaneShip(profile, spec.Phase, spec.Effort, spec.Rho,
            spec.ClockRad);
        TestTrue(label + TEXT(": kind Lane"), rail.Kind == static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane));
        TestEqual(label + TEXT(": faction"), static_cast<int32>(rail.Faction), 1);
        TestEqual(label + TEXT(": group"), static_cast<int32>(rail.Group), 0);
        TestTrue(label + TEXT(": P1 = float(dv)"), rail.P[1] == static_cast<float>(exact.DvMps));
        TestTrue(label + TEXT(": P2 = float(up offset)"), rail.P[2] == static_cast<float>(exact.UpOffsetM));
        TestTrue(label + TEXT(": P3 = float(right offset)"), rail.P[3] == static_cast<float>(exact.RightOffsetM));
        TestTrue(label + TEXT(": P4 = float(t_a)"), rail.P[4] == static_cast<float>(exact.AccelS));
        TestTrue(label + TEXT(": P6 reserved 0"), rail.P[6] == 0.0f);
        TestTrue(label + TEXT(": P0 in [0, 1)"), rail.P[0] >= 0.0f && rail.P[0] < 1.0f);
        TestTrue(label + TEXT(": |P5| within a float step"), FMath::Abs(rail.P[5]) <= 5.97e-8f);
        const double phase = static_cast<double>(rail.P[0]) + static_cast<double>(rail.P[5]);
        TestTrue(label + FString::Printf(TEXT(": P0 + P5 = phase (%.17g, expected %.17g)"), phase, exact.Phase),
            FMath::Abs(phase - exact.Phase) <= 1.0e-14);
        TestTrue(label + TEXT(": right offset >= 0 (keep right)"), rail.P[3] >= 0.0f);
        const double lateral = FMath::Sqrt(static_cast<double>(rail.P[2]) * rail.P[2]
            + static_cast<double>(rail.P[3]) * rail.P[3]);
        TestTrue(label + TEXT(": lateral radius rho Rw"), FMath::Abs(lateral - spec.Rho * rw) <= 1.0e-6 * rw);

        // Read back
        const FSOLLaneShip back = SOLNpcRail::LaneShipFromRail(rail);
        TestTrue(label + TEXT(": LaneShipFromRail"), back.Phase == phase && back.DvMps == rail.P[1]
            && back.UpOffsetM == rail.P[2] && back.RightOffsetM == rail.P[3] && back.AccelS == rail.P[4]);
    }

    // Phase 0 packs exactly; a phase that rounds to 1 in float keeps P0 below 1
    const FSOLNpcRail zero = NpcRailLane(world, { 0.0, 0.8, 0.5, 1.0 });
    TestTrue(TEXT("phase 0: P0 = P5 = 0"), zero.P[0] == 0.0f && zero.P[5] == 0.0f);
    const FSOLNpcRail nearOne = NpcRailLane(world, { 1.0 - 1.0e-9, 0.8, 0.5, 1.0 });
    TestTrue(TEXT("phase 1 - 1e-9: P0 < 1"), nearOne.P[0] < 1.0f);
    TestTrue(TEXT("phase 1 - 1e-9: P0 + P5 exact"), FMath::Abs(static_cast<double>(nearOne.P[0])
        + static_cast<double>(nearOne.P[5]) - (1.0 - 1.0e-9)) <= 1.0e-14);

    // Clock angle at the ends of the right half: 12 o'clock (all up) and 6 o'clock (all down)
    const FSOLNpcRail noon = NpcRailLane(world, { 0.2, 0.8, 0.5, 0.0 });
    TestTrue(TEXT("clock 0: up offset rho Rw, right 0"), FMath::Abs(noon.P[2] - 0.5 * rw) <= 1.0e-6 * rw
        && FMath::Abs(noon.P[3]) <= 1.0e-6 * rw);
    const FSOLNpcRail six = NpcRailLane(world, { 0.2, 0.8, 0.5, NPCRAIL_PI });
    TestTrue(TEXT("clock pi: up offset -rho Rw, right about 0"), FMath::Abs(six.P[2] + 0.5 * rw) <= 1.0e-6 * rw
        && FMath::Abs(six.P[3]) <= 1.0e-6 * rw);

    // The global group: base + lane index
    SOLNpcScale::FSpawn second = NpcRailLaneSpawn(NPCRAIL_SPECS[0]);
    second.GroupIndex = 1;
    TestEqual(TEXT("group base 3, lane 1: group 4"),
        static_cast<int32>(SOLNpcRail::FromSpawn(second, 3, profile, 0.0).Group), 4);

    // A cluster spawn still packs its orbit (P0 radius) with its own group base
    SOLNpcScale::FSpawn clusterSpawn;
    clusterSpawn.Kind = SOLNpcScale::EPlacementKind::Cluster;
    clusterSpawn.GroupIndex = 0;
    clusterSpawn.OrbitRadiusM = 7.0e6;
    clusterSpawn.OrbitInclinationRad = 0.3;
    clusterSpawn.OrbitNodeRad = 1.0;
    clusterSpawn.OrbitPhaseRad = 2.0;
    const FSOLNpcRail clusterRail = SOLNpcRail::FromSpawn(clusterSpawn, 1, profile, 3.986004418e14);
    TestTrue(TEXT("cluster: kind, group, radius"),
        clusterRail.Kind == static_cast<uint8>(SOLNpcScale::EPlacementKind::Cluster) && clusterRail.Group == 1
        && clusterRail.P[0] == 7.0e6f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailLaneClosedFormTest, "SOLTest.NpcRail.LaneClosedForm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contract case 10: the CPU lane rail is the 50a closed form at any time: EvaluateAtTimeM(rail, frame, t) =
// SOLLaneTraffic::LaneShipPositionM(the frame's lane profile, the frame's lane frame, LaneShipFromRail(rail), t) within
// 1e-3 m (negative, tiny and huge times included), EvaluateM is EvaluateAtTimeM at the frame's time exactly, and the
// motion repeats every T_cyc (5 cm: the double rounding of a time near 2e8 s times the speed)
bool FSOLNpcRailLaneClosedFormTest::RunTest(const FString& /*parameters*/)
{
    for (const double distanceRatio : { 1.0, 0.57 })
    {
        const FNpcRailWorld world = NpcRailMakeWorld(distanceRatio * NPCRAIL_NOMINAL_LENGTH_M);
        const FSOLLaneProfile& profile = world.Groups.LaneProfile[0];
        for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
        {
            const FSOLNpcRail rail = NpcRailLane(world, spec);
            const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);
            const FSOLLaneCycle cycle = NpcRailCycleOf(profile, rail);
            const FString label = FString::Printf(TEXT("distance %.2f L0, rho %.3f"), distanceRatio, spec.Rho);
            const double times[] = { -3.7e7, -1234.5, 0.0, 0.5, 777.25, 9.99e4, 3.3e6, 2.2e8,
                NpcRailTimeAt(rail, cycle, cycle.LegS), NpcRailTimeAt(rail, cycle, cycle.LegS + 0.5 * cycle.TurnS),
                NpcRailTimeAt(rail, cycle, 1.5 * cycle.LegS + cycle.TurnS), NpcRailTimeAt(rail, cycle, cycle.CycleS) };
            for (const double t : times)
            {
                const FSOLNpcRailFrame frame = NpcRailFrameAt(world, t);
                const FVector3d cpu = SOLNpcRail::EvaluateAtTimeM(rail, frame, t);
                const FVector3d closed = SOLLaneTraffic::LaneShipPositionM(frame.LaneProfile[0], frame.LaneFrame[0],
                    ship, t);
                TestTrue(label + FString::Printf(TEXT(" t %.4e: EvaluateAtTimeM = LaneShipPositionM (%.3e m)"), t,
                    (cpu - closed).Size()), (cpu - closed).Size() <= NPCRAIL_POSITION_TOL_M);
                TestTrue(label + FString::Printf(TEXT(" t %.4e: EvaluateM exact"), t),
                    SOLNpcRail::EvaluateM(rail, frame) == cpu);
                const FVector3d later = SOLNpcRail::EvaluateAtTimeM(rail, frame, t + 3.0 * cycle.CycleS);
                TestTrue(label + FString::Printf(TEXT(" t %.4e: periodic in T_cyc (%.3e m)"), t, (later - cpu).Size()),
                    (later - cpu).Size() <= 5.0e-2);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailLaneFloatContinuityTest, "SOLTest.NpcRail.LaneFloatContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contract cases 8, 10 and 15b through the rail: with the constants rounded to float by FromSpawn, the CPU rail stays
// seamless: exactly at the end ring at every leg / U-turn joint (relative to the nearer body, 1e-3 m) with the wall
// speed's travel across +-1 ms (1 m), and at the lane's middle at T_leg / 2 with the expected travel across +-1 ms
bool FSOLNpcRailLaneFloatContinuityTest::RunTest(const FString& /*parameters*/)
{
    const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
    const double halfStep = 1.0e-3;
    for (const double distanceRatio : { 1.0, 0.15 })
    {
        const FNpcRailWorld world = NpcRailMakeWorld(distanceRatio * NPCRAIL_NOMINAL_LENGTH_M);
        const FSOLLaneProfile& profile = world.Groups.LaneProfile[0];
        const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
        const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
        const double l0 = profile.NominalLegM;
        const double l = laneFrame.CurrentLegM;
        for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
        {
            const FSOLNpcRail rail = NpcRailLane(world, spec);
            const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);
            const FSOLLaneCycle cycle = NpcRailCycleOf(profile, rail);
            const FString label = FString::Printf(TEXT("distance %.2f L0, rho %.3f"), distanceRatio, spec.Rho);

            // Returns the rail position relative to a body at cycle time tau
            const auto at = [&](const double tau, const FVector3d& bodyM)
            {
                return SOLNpcRail::EvaluateAtTimeM(rail, frame, NpcRailTimeAt(rail, cycle, tau)) - bodyM;
            };

            // Joints: at the end ring, the wall speed's travel across them
            struct FJoint
            {
                double Tau;
                bool bAtB;
            };
            const FJoint joints[] = { { cycle.LegS, true }, { cycle.LegS + cycle.TurnS, true },
                { 2.0 * cycle.LegS + cycle.TurnS, false }, { cycle.CycleS, false } };
            for (const FJoint& joint : joints)
            {
                const FVector3d& bodyM = joint.bAtB ? laneFrame.BM : laneFrame.AM;
                const double ringAxial = joint.bAtB ? -(profile.EndArcBM - laneFrame.EndShiftM) : profile.EndArcAM;
                const double axial = FVector3d::DotProduct(at(joint.Tau, bodyM), laneFrame.Axis);
                const double travel = (at(joint.Tau + halfStep, bodyM) - at(joint.Tau - halfStep, bodyM)).Size();
                TestTrue(label + FString::Printf(TEXT(": joint at tau %.6e on the end ring (axial %.6e, ring %.6e)"),
                    joint.Tau, axial, ringAxial), FMath::Abs(axial - ringAxial) <= NPCRAIL_POSITION_TOL_M);
                TestTrue(label + FString::Printf(TEXT(": joint at tau %.6e: travel %.6e m over 2 ms"), joint.Tau,
                    travel), FMath::Abs(travel - w * 2.0 * halfStep) <= 1.0);
            }

            // T_leg / 2 on both legs: the lane's middle and the mid-leg speed's travel across +-1 ms
            const FNpcRailLegRef mid = NpcRailLegRefAt(ship, l0, cycle.LegS, 0.5 * cycle.LegS);
            const double midSpeed = mid.SpeedMps * SOLLaneTraffic::MetricMapSlope(0.5 * l0, l0, l);
            for (const double legStart : { 0.0, cycle.LegS + cycle.TurnS })
            {
                const double tauMid = legStart + 0.5 * cycle.LegS;
                const double axial = FVector3d::DotProduct(at(tauMid, laneFrame.AM), laneFrame.Axis);
                const double travel = (at(tauMid + halfStep, laneFrame.AM) - at(tauMid - halfStep, laneFrame.AM))
                    .Size();
                TestTrue(label + FString::Printf(TEXT(": mid-leg at tau %.6e (axial %.6e, expected %.6e)"), tauMid,
                    axial, profile.EndArcAM + 0.5 * l), FMath::Abs(axial - (profile.EndArcAM + 0.5 * l)) <= 1.0);
                TestTrue(label + FString::Printf(TEXT(": mid-leg travel %.6e m over 2 ms (expected %.6e)"), travel,
                    midSpeed * 2.0 * halfStep), FMath::Abs(travel - midSpeed * 2.0 * halfStep) <= 1.0);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailLaneTrafficTexelsTest, "SOLTest.NpcRail.LaneTrafficTexels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.6: the group texture has NPC_GROUP_TEXTURE_ROWS (6) rows of NPC_GROUP_TEXTURE_WIDTH texels (row r,
// column c at r x W + c). WriteLaneTrafficTexels writes rows 2-5 of every column from the frame and leaves rows 0
// and 1 alone: a drivable lane column gets row 2 (Up, w the wall speed), row 3 (Right, D_U), row 4 (pi / T_U, e_A,
// l0, Z), row 5 (e_B - end shift, T_U, (l - l0) / (l0 - Z), 1 / Z); a lane with a valid frame that is not drivable
// gets rows 2 and 3 and zero rows 4 and 5 (l0 = 0 tells the shader to hold the ship at rest); a lane with an invalid
// frame and every cluster, battle and unused column get zero rows 2-5; a buffer shorter than six rows is left alone;
// lanes past the texture width are not written
bool FSOLNpcRailLaneTrafficTexelsTest::RunTest(const FString& /*parameters*/)
{
    const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    TestEqual(TEXT("NPC_GROUP_TEXTURE_ROWS"), SOL::NPC_GROUP_TEXTURE_ROWS, 6);
    TestEqual(TEXT("NPC_GROUP_TEXTURE_WIDTH unchanged"), width, 64);

    // Lane 0 drivable, lane 1 not drivable (zero nominal length), lane 2 unresolved, then a cluster and a battle
    FNpcRailWorld world = NpcRailMakeWorld();
    FSOLNpcGroupTable& groups = world.Groups;
    groups.LaneCount = 3;
    groups.LaneBodyA = { 0, 0, INDEX_NONE };
    groups.LaneBodyB = { 1, 1, 1 };
    groups.LaneRadiusM = { NPCRAIL_LANE_RADIUS_M, NPCRAIL_LANE_RADIUS_M, NPCRAIL_LANE_RADIUS_M };
    const int32 inWall[] = { NPCRAIL_IN_WALL, 500, 500 };
    const int32 halo[] = { NPCRAIL_HALO, 100, 100 };
    const double nominal[] = { NPCRAIL_NOMINAL_LENGTH_M, 0.0, NPCRAIL_NOMINAL_LENGTH_M };
    const double radiusA[] = { NPCRAIL_EARTH_RADIUS_M, NPCRAIL_EARTH_RADIUS_M, NPCRAIL_EARTH_RADIUS_M };
    const double radiusB[] = { NPCRAIL_MARS_RADIUS_M, NPCRAIL_MARS_RADIUS_M, NPCRAIL_MARS_RADIUS_M };
    SOLNpcRail::BuildLaneProfiles(groups, inWall, halo, nominal, radiusA, radiusB);
    const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);

    const FVector4f sentinel(7.0f, 7.0f, 7.0f, 7.0f);
    TArray<FVector4f> texels;
    texels.Init(sentinel, width * SOL::NPC_GROUP_TEXTURE_ROWS);
    SOLNpcRail::WriteLaneTrafficTexels(frame, texels);

    // Returns whether a texel matches the expected values within a few float steps
    const auto texelNear = [](const FVector4f& texel, const double x, const double y, const double z, const double w4)
    {
        const auto close = [](const float actual, const double expected)
        {
            return FMath::Abs(static_cast<double>(actual) - expected) <= 1.0e-6 * FMath::Abs(expected) + 1.0e-30;
        };
        return close(texel.X, x) && close(texel.Y, y) && close(texel.Z, z) && close(texel.W, w4);
    };
    const FVector4f zero(0.0f, 0.0f, 0.0f, 0.0f);

    // Rows 0 and 1 untouched
    bool bRows01Untouched = true;
    for (int32 index = 0; index < 2 * width; ++index)
    {
        bRows01Untouched = bRows01Untouched && texels[index] == sentinel;
    }
    TestTrue(TEXT("rows 0 and 1 untouched"), bRows01Untouched);

    // Lane 0: the four traffic rows
    const FSOLLaneProfile& profile = frame.LaneProfile[0];
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
    const double l0 = profile.NominalLegM;
    const double z = profile.MetricZoneM;
    const double dl = laneFrame.CurrentLegM - l0;
    TestTrue(TEXT("lane 0 row 2: Up, w"), texelNear(texels[2 * width], laneFrame.Up.X, laneFrame.Up.Y, laneFrame.Up.Z,
        SOL::NPC_LANE_WALL_SPEED_MPS));
    TestTrue(TEXT("lane 0 row 3: Right, D_U"), texelNear(texels[3 * width], laneFrame.Right.X, laneFrame.Right.Y,
        laneFrame.Right.Z, profile.UTurnDepthM));
    TestTrue(TEXT("lane 0 row 4: pi / T_U, e_A, l0, Z"), texelNear(texels[4 * width], NPCRAIL_PI / profile.UTurnS,
        profile.EndArcAM, l0, z));
    TestTrue(TEXT("lane 0 row 5: e_B - shift, T_U, (l - l0) / (l0 - Z), 1 / Z"), texelNear(texels[5 * width],
        profile.EndArcBM - laneFrame.EndShiftM, profile.UTurnS, dl / (l0 - z), 1.0 / z));
    TestTrue(TEXT("lane 0: l - l0 is negative here (the bodies closer than the maximum)"), texels[5 * width].Z < 0.0f);

    // Lane 1: basis rows only, zero rows 4 and 5
    const FSOLLaneFrame& stillFrame = frame.LaneFrame[1];
    TestFalse(TEXT("lane 1 not drivable"), frame.LaneProfile[1].bDrivable);
    TestTrue(TEXT("lane 1 row 2: Up"), texelNear(FVector4f(texels[2 * width + 1].X, texels[2 * width + 1].Y,
        texels[2 * width + 1].Z, 0.0f), stillFrame.Up.X, stillFrame.Up.Y, stillFrame.Up.Z, 0.0));
    TestTrue(TEXT("lane 1 row 3: Right"), texelNear(FVector4f(texels[3 * width + 1].X, texels[3 * width + 1].Y,
        texels[3 * width + 1].Z, 0.0f), stillFrame.Right.X, stillFrame.Right.Y, stillFrame.Right.Z, 0.0));
    TestTrue(TEXT("lane 1 rows 4 and 5 zero"), texels[4 * width + 1] == zero && texels[5 * width + 1] == zero);

    // Lane 2 (invalid frame) and every other column: zero rows 2-5
    bool bOthersZero = true;
    for (int32 column = 2; column < width; ++column)
    {
        for (int32 row = 2; row < SOL::NPC_GROUP_TEXTURE_ROWS; ++row)
        {
            bOthersZero = bOthersZero && texels[row * width + column] == zero;
        }
    }
    TestTrue(TEXT("invalid lane, cluster, battle and unused columns: zero rows 2-5"), bOthersZero);

    // A buffer shorter than six rows is left alone
    TArray<FVector4f> shortTexels;
    shortTexels.Init(sentinel, width * (SOL::NPC_GROUP_TEXTURE_ROWS - 1));
    SOLNpcRail::WriteLaneTrafficTexels(frame, shortTexels);
    bool bShortUntouched = true;
    for (const FVector4f& texel : shortTexels)
    {
        bShortUntouched = bShortUntouched && texel == sentinel;
    }
    TestTrue(TEXT("five-row buffer untouched"), bShortUntouched);

    // More lanes than columns: the first NPC_GROUP_TEXTURE_WIDTH are written, nothing past the buffer
    FNpcRailWorld crowded = NpcRailMakeWorld();
    const int32 manyLanes = width + 6;
    crowded.Groups.LaneCount = manyLanes;
    crowded.Groups.ClusterCount = 0;
    crowded.Groups.BattleCount = 0;
    crowded.Groups.LaneBodyA.Init(0, manyLanes);
    crowded.Groups.LaneBodyB.Init(1, manyLanes);
    crowded.Groups.LaneRadiusM.Init(NPCRAIL_LANE_RADIUS_M, manyLanes);
    TArray<int32> manyInWall;
    manyInWall.Init(NPCRAIL_IN_WALL, manyLanes);
    TArray<int32> manyHalo;
    manyHalo.Init(NPCRAIL_HALO, manyLanes);
    TArray<double> manyNominal;
    manyNominal.Init(NPCRAIL_NOMINAL_LENGTH_M, manyLanes);
    TArray<double> manyRadiusA;
    manyRadiusA.Init(NPCRAIL_EARTH_RADIUS_M, manyLanes);
    TArray<double> manyRadiusB;
    manyRadiusB.Init(NPCRAIL_MARS_RADIUS_M, manyLanes);
    SOLNpcRail::BuildLaneProfiles(crowded.Groups, manyInWall, manyHalo, manyNominal, manyRadiusA, manyRadiusB);
    texels.Init(sentinel, width * SOL::NPC_GROUP_TEXTURE_ROWS);
    SOLNpcRail::WriteLaneTrafficTexels(NpcRailFrameAt(crowded, 0.0), texels);
    TestEqual(TEXT("crowded: buffer size unchanged"), texels.Num(), width * SOL::NPC_GROUP_TEXTURE_ROWS);
    TestTrue(TEXT("crowded: last column written (l0)"), FMath::Abs(static_cast<double>(texels[4 * width + width - 1].Z)
        - NpcRailFrameAt(crowded, 0.0).LaneProfile[width - 1].NominalLegM) <= 1.0e-6 * NPCRAIL_NOMINAL_LENGTH_M);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailGpuLaneMirrorTest, "SOLTest.NpcRail.GpuLaneFloatMirror",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contract case 10 (GPU float mirror): on every default lane (at its nominal length; Earth - Mars also at 0.3 of it),
// for hashed ships inside the wall and in the halo, re-based to six epochs spread over the cycle and evaluated 0, 5,000
// and 9,999.5 s into each epoch window with the viewpoint 1,000 km from the ship (the Mid hand-over), the float mirror
// of the point shader agrees with the CPU rail (double, the same float rail) within
// v_peak x max(1, (l - Z) / (l0 - Z)) x ulp(T_leg) + 4 x ulp(l0) + 4 x ulp(max(|A - V|, |B - V|)) + 1 m
// (SDD 50 section 4.6 double-float evaluation: the leg time's single float rounding in the far half of a leg, the
// rounding of the large epoch terms, and the float rounding of the lane's ends relative to the viewpoint), and Earth -
// Mars at its nominal length within 30 km anywhere. The worst error per lane is logged
bool FSOLNpcRailGpuLaneMirrorTest::RunTest(const FString& /*parameters*/)
{
    TArray<FVector4f> texels;
    struct FMirrorCase
    {
        int32 Lane;
        double Ratio;
    };
    TArray<FMirrorCase> cases;
    for (int32 lane = 0; lane < UE_ARRAY_COUNT(NPCRAIL_DEFAULT_LANES); ++lane)
    {
        cases.Add({ lane, 1.0 });
    }
    cases.Add({ 3, 0.3 });
    for (const FMirrorCase& mirrorCase : cases)
    {
        const FNpcRailDefaultLane& row = NPCRAIL_DEFAULT_LANES[mirrorCase.Lane];
        const FString laneLabel = FString::Printf(TEXT("%s - %s at %.2f L0"), row.BodyA, row.BodyB, mirrorCase.Ratio);
        FNpcRailWorld world;
        if (!NpcRailDefaultWorld(row, mirrorCase.Ratio, world))
        {
            AddError(laneLabel + TEXT(": not in the default layout"));
            continue;
        }
        const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
        const FSOLLaneProfile& profile = frame.LaneProfile[0];
        const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
        if (!profile.bDrivable || !laneFrame.bValid)
        {
            AddError(laneLabel + TEXT(": not drivable"));
            continue;
        }
        const bool bEarthMars = mirrorCase.Lane == 3 && mirrorCase.Ratio == 1.0;
        const double slope = FMath::Max(1.0, (laneFrame.CurrentLegM - profile.MetricZoneM)
            / (profile.NominalLegM - profile.MetricZoneM));
        double worstErrorM = 0.0;
        double worstRatio = 0.0;
        int32 failures = 0;
        FString firstFailure;
        for (int32 shipIndex = 0; shipIndex < 24; ++shipIndex)
        {
            const FNpcRailSpec spec = NpcRailHashedSpec(static_cast<uint64>(mirrorCase.Lane) + 11ull, shipIndex);
            const FSOLNpcRail rail = SOLNpcRail::FromSpawn(NpcRailLaneSpawn(spec), 0, profile, 0.0);
            const FSOLLaneCycle cycle = NpcRailCycleOf(profile, rail);
            const double peak = SOL::NPC_LANE_WALL_SPEED_MPS + static_cast<double>(rail.P[1]);
            for (int32 epoch = 0; epoch < 6; ++epoch)
            {
                const double deltaS = (static_cast<double>(epoch) + 0.37) / 6.0 * cycle.CycleS;
                const FSOLNpcRail gpuRail = SOLNpcRail::AdvancedToEpoch(rail, deltaS, frame.LaneProfile);
                for (const float t : { 0.0f, 5000.0f, 9999.5f })
                {
                    // CPU (double) position and a viewpoint 1,000 km from it
                    const FVector3d cpu = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(t));
                    const FVector3d viewpoint = cpu + laneFrame.Up * 9.5e5 + laneFrame.Right * 3.0e5;
                    FVector4f rows[SOL::NPC_GROUP_TEXTURE_ROWS];
                    NpcRailGpuRows(frame, 0, viewpoint, texels, rows);
                    const FVector3f gpu = NpcRailGpuLaneRelM(gpuRail, frame.LaneProfile, rows, t);
                    const FVector3d gpuRel(gpu.X, gpu.Y, gpu.Z);
                    const double errorM = (gpuRel - (cpu - viewpoint)).Size();
                    const double endsM = FMath::Max((laneFrame.AM - viewpoint).Size(),
                        (laneFrame.BM - viewpoint).Size());
                    const double boundM = FMath::Min(peak * slope * NpcRailFloatUlp(cycle.LegS)
                        + 4.0 * NpcRailFloatUlp(profile.NominalLegM) + 4.0 * NpcRailFloatUlp(endsM) + 1.0,
                        bEarthMars ? 3.0e4 : std::numeric_limits<double>::infinity());
                    worstErrorM = FMath::Max(worstErrorM, errorM);
                    worstRatio = FMath::Max(worstRatio, errorM / boundM);
                    if (!(errorM <= boundM))
                    {
                        if (failures == 0)
                        {
                            firstFailure = FString::Printf(TEXT("ship %d (rho %.3f) epoch %d t %.1f: %.6e m above "
                                "%.6e m"), shipIndex, spec.Rho, epoch, t, errorM, boundM);
                        }
                        ++failures;
                    }
                }
            }
        }
        AddInfo(laneLabel + FString::Printf(TEXT(": worst mirror error %.3f km, %.3f of the bound"),
            worstErrorM * 1.0e-3, worstRatio));
        TestTrue(laneLabel + FString::Printf(TEXT(": float mirror within the bound (%d failures; first: %s)"),
            failures, *firstFailure), failures == 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailGpuLaneSmoothTest, "SOLTest.NpcRail.GpuLaneSmoothness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Plan 50b notes (V5 in C++) and SDD 50 section 4.6: a ship's float-mirror position sampled at 60 Hz for 5 s late in an
// epoch window (9,000 s in, where the float time is coarsest) moves every frame (no repeated positions, no steps), on
// Earth - Moon, Earth - Mars, Jupiter - Saturn and the outer Saturn - Uranus lane: near either end ring on both legs
// and inside both U-turns each frame's displacement matches the CPU rail's at the same float times within 20 % plus 8
// float steps of the nearer end's distance from the viewpoint; mid-lane on both legs (coasting beyond the end zones)
// within 20 % plus 4 float steps of v_peak x 10,000 s (the moving term's spacing), and no frame repeats a position.
// Straddling T_leg / 2 on both legs (the shader's reference ring switches from the start ring to the end ring there,
// bFirstHalf) the frame across the switch may jump by the two evaluations' difference: it must stay within case 10's
// bound (as GpuLaneFloatMirror, the viewpoint's distance to the far end included); the jump is logged per lane
bool FSOLNpcRailGpuLaneSmoothTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 frameCount = 300;
    TArray<FVector4f> texels;
    const FNpcRailSpec specs[] = {
        { 0.37, 0.8, 0.5, 1.2 },                // Inside the wall
        { 0.81, 0.6, 1.5, 2.0 },                // Halo
        { 0.05, 1.0, 0.05, 0.5 * NPCRAIL_PI },  // Near the axis (nearly stops at a U-turn's apex: legs only)
    };
    int32 midLaneFrames = 0;
    int32 midLaneRepeats = 0;
    double worstSwitchRatio = 0.0;
    for (const int32 laneIndex : { 2, 3, 5, 6 })
    {
        const FNpcRailDefaultLane& row = NPCRAIL_DEFAULT_LANES[laneIndex];
        FNpcRailWorld world;
        if (!NpcRailDefaultWorld(row, 1.0, world))
        {
            AddError(FString::Printf(TEXT("%s - %s: not in the default layout"), row.BodyA, row.BodyB));
            continue;
        }
        const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
        const FSOLLaneProfile& profile = frame.LaneProfile[0];
        const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
        const double slope = FMath::Max(1.0, (laneFrame.CurrentLegM - profile.MetricZoneM)
            / (profile.NominalLegM - profile.MetricZoneM));
        double laneSwitchM = 0.0;
        double laneSwitchRatio = 0.0;
        for (const FNpcRailSpec& spec : specs)
        {
            const FSOLNpcRail rail = SOLNpcRail::FromSpawn(NpcRailLaneSpawn(spec), 0, profile, 0.0);
            const FSOLLaneCycle cycle = NpcRailCycleOf(profile, rail);
            const double peak = SOL::NPC_LANE_WALL_SPEED_MPS + static_cast<double>(rail.P[1]);
            const bool bTurns = spec.Rho * FMath::Sin(spec.ClockRad) >= 0.3;
            struct FTarget
            {
                const TCHAR* Name;
                double Tau;
                bool bNearB;
                bool bTurn;
                bool bMidLane;
                bool bSwitch;
            };
            const FTarget targets[] = {
                { TEXT("outbound near A"), 500.0, false, false, false, false },
                { TEXT("outbound mid-lane"), 0.45 * cycle.LegS, false, false, true, false },
                { TEXT("outbound across T_leg / 2"), 0.5 * cycle.LegS - 2.5, false, false, true, true },
                { TEXT("outbound near B"), cycle.LegS - 500.0, true, false, false, false },
                { TEXT("turn at B"), cycle.LegS + 0.3 * cycle.TurnS, true, true, false, false },
                { TEXT("inbound mid-lane"), cycle.LegS + cycle.TurnS + 0.55 * cycle.LegS, true, false, true, false },
                { TEXT("inbound across T_leg / 2"), cycle.LegS + cycle.TurnS + 0.5 * cycle.LegS - 2.5, true, false,
                    true, true },
                { TEXT("inbound near A"), 2.0 * cycle.LegS + cycle.TurnS - 500.0, false, false, false, false },
                { TEXT("turn at A"), 2.0 * cycle.LegS + cycle.TurnS + 0.3 * cycle.TurnS, false, true, false, false },
            };
            for (const FTarget& target : targets)
            {
                if (target.bTurn && !bTurns)
                {
                    continue;
                }

                // Re-base so that 9,000 s into the epoch window the ship is at the target cycle time
                const double deltaS = NpcRailTimeAt(rail, cycle, target.Tau) - 9000.0;
                const FSOLNpcRail gpuRail = SOLNpcRail::AdvancedToEpoch(rail, deltaS, frame.LaneProfile);
                const FVector3d viewpoint = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, 9000.0) + laneFrame.Up * 1.0e6;
                FVector4f rows[SOL::NPC_GROUP_TEXTURE_ROWS];
                NpcRailGpuRows(frame, 0, viewpoint, texels, rows);
                const double floorM = target.bMidLane ? 4.0 * NpcRailFloatUlp(peak * 1.0e4)
                    : 8.0 * NpcRailFloatUlp(((target.bNearB ? laneFrame.BM : laneFrame.AM) - viewpoint).Size());

                // Across T_leg / 2 the frame of the reference-ring switch may jump within case 10's bound
                const double switchBoundM = peak * slope * NpcRailFloatUlp(cycle.LegS)
                    + 4.0 * NpcRailFloatUlp(profile.NominalLegM) + 4.0 * NpcRailFloatUlp(FMath::Max(
                    (laneFrame.AM - viewpoint).Size(), (laneFrame.BM - viewpoint).Size())) + 1.0;
                double switchJumpM = 0.0;

                // Frame by frame: the mirror's displacement against the CPU's at the same float times
                int32 failures = 0;
                int32 repeats = 0;
                FString firstFailure;
                float previousT = static_cast<float>(9000.0);
                FVector3d previousCpu = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(previousT));
                FVector3f previousGpu = NpcRailGpuLaneRelM(gpuRail, frame.LaneProfile, rows, previousT);
                for (int32 step = 1; step <= frameCount; ++step)
                {
                    const float t = static_cast<float>(9000.0 + static_cast<double>(step) / 60.0);
                    const FVector3d cpu = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(t));
                    const FVector3f gpu = NpcRailGpuLaneRelM(gpuRail, frame.LaneProfile, rows, t);
                    const double cpuStepM = (cpu - previousCpu).Size();
                    const double gpuStepM = FVector3d(gpu.X - previousGpu.X, gpu.Y - previousGpu.Y,
                        gpu.Z - previousGpu.Z).Size();
                    const double stepDiffM = FMath::Abs(gpuStepM - cpuStepM);
                    const bool bSwitchFrame = target.bSwitch && stepDiffM > 0.2 * cpuStepM + floorM;
                    switchJumpM = target.bSwitch ? FMath::Max(switchJumpM, stepDiffM) : switchJumpM;
                    const double tolM = bSwitchFrame ? switchBoundM : 0.2 * cpuStepM + floorM;
                    repeats += gpuStepM > 0.0 ? 0 : 1;
                    if (!(gpuStepM > 0.0) || !(cpuStepM > 0.0) || !(FMath::Abs(gpuStepM - cpuStepM) <= tolM))
                    {
                        if (failures == 0)
                        {
                            firstFailure = FString::Printf(TEXT("frame %d: mirror step %.4f m, CPU step %.4f m, "
                                "tolerance %.4f m"), step, gpuStepM, cpuStepM, tolM);
                        }
                        ++failures;
                    }
                    previousCpu = cpu;
                    previousGpu = gpu;
                }
                if (target.bMidLane)
                {
                    midLaneFrames += frameCount;
                    midLaneRepeats += repeats;
                }
                if (target.bSwitch)
                {
                    laneSwitchM = FMath::Max(laneSwitchM, switchJumpM);
                    laneSwitchRatio = FMath::Max(laneSwitchRatio, switchJumpM / switchBoundM);
                }
                TestEqual(FString::Printf(TEXT("%s - %s, rho %.2f, %s: no repeated positions at 60 Hz"), row.BodyA,
                    row.BodyB, spec.Rho, target.Name), repeats, 0);
                TestTrue(FString::Printf(TEXT("%s - %s, rho %.2f, %s: smooth at 60 Hz (%d failures; first: %s)"),
                    row.BodyA, row.BodyB, spec.Rho, target.Name, failures, *firstFailure), failures == 0);
            }
        }
        AddInfo(FString::Printf(TEXT("%s - %s: worst jump across T_leg / 2 %.3f km, %.3f of the case-10 bound"),
            row.BodyA, row.BodyB, laneSwitchM * 1.0e-3, laneSwitchRatio));
        worstSwitchRatio = FMath::Max(worstSwitchRatio, laneSwitchRatio);
    }
    AddInfo(FString::Printf(TEXT("across T_leg / 2: worst jump %.3f of the case-10 bound"), worstSwitchRatio));
    AddInfo(FString::Printf(TEXT("mid-lane: %d of %d frames repeat a position"), midLaneRepeats, midLaneFrames));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailGpuLaneCycleSweepTest, "SOLTest.NpcRail.GpuLaneCycleSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.6 (the 50b V5 finding that samples spread over the whole lane repeat positions): 24 hashed ships per
// lane at 40 cycle times spread over the whole cycle, each re-based so the time falls 9,000 s into the epoch window and
// seen from 2,000 km (the Mid radius), sampled at 60 Hz for 1 s. No frame may repeat the float mirror's position while
// the CPU rail moves more than a metre, in any phase (U-turn, end zone, accelerating, coasting); Earth - Moon, Earth -
// Mars (at its nominal length and at 0.6 of it, so the end-zone map stretches), Jupiter - Saturn and Saturn - Uranus.
// Per phase the repeats and the worst frame step off the CPU's (px at 2,000 km, 1080p, 90 degrees) are logged; the
// frames across T_leg / 2 (the reference-ring switch, GpuLaneSmoothness) are left out of the step figure
bool FSOLNpcRailGpuLaneCycleSweepTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 shipCount = 24;
    constexpr int32 timesPerCycle = 40;
    constexpr int32 frameCount = 60;
    constexpr double viewDistanceM = 2.0e6;
    const double pixelM = viewDistanceM * 2.0 / 1920.0;
    constexpr int32 PHASE_TURN = 0;
    constexpr int32 PHASE_ZONE = 1;
    constexpr int32 PHASE_ACCELERATE = 2;
    constexpr int32 PHASE_COAST = 3;
    const TCHAR* phaseNames[] = { TEXT("U-turn"), TEXT("end zone"), TEXT("accelerate"), TEXT("coast") };
    struct FSweepCase
    {
        int32 Lane;
        double Ratio;
    };
    const FSweepCase sweepCases[] = { { 2, 1.0 }, { 3, 1.0 }, { 3, 0.6 }, { 5, 1.0 }, { 6, 1.0 } };
    TArray<FVector4f> texels;
    for (const FSweepCase& sweepCase : sweepCases)
    {
        const FNpcRailDefaultLane& row = NPCRAIL_DEFAULT_LANES[sweepCase.Lane];
        const FString laneLabel = FString::Printf(TEXT("%s - %s at %.1f L0"), row.BodyA, row.BodyB, sweepCase.Ratio);
        FNpcRailWorld world;
        if (!NpcRailDefaultWorld(row, sweepCase.Ratio, world))
        {
            AddError(laneLabel + TEXT(": not in the default layout"));
            continue;
        }
        const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
        const FSOLLaneProfile& profile = frame.LaneProfile[0];
        const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
        const double l0 = profile.NominalLegM;
        int32 repeats[4] = {};
        int32 phaseFrames[4] = {};
        double worstStepPx[4] = {};
        for (int32 shipIndex = 0; shipIndex < shipCount; ++shipIndex)
        {
            const FNpcRailSpec spec = NpcRailHashedSpec(static_cast<uint64>(sweepCase.Lane) + 101ull, shipIndex);
            const FSOLNpcRail rail = SOLNpcRail::FromSpawn(NpcRailLaneSpawn(spec), 0, profile, 0.0);
            const FSOLLaneCycle cycle = NpcRailCycleOf(profile, rail);
            const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);

            // Returns the phase at a cycle time and the leg time (negative in a U-turn)
            const auto phaseAt = [&](const double tau, double& outLegTimeS)
            {
                const double wrapped = FMath::Fmod(tau, cycle.CycleS);
                const bool bOutbound = wrapped < cycle.LegS;
                const bool bInbound = wrapped >= cycle.LegS + cycle.TurnS && wrapped < 2.0 * cycle.LegS + cycle.TurnS;
                if (!bOutbound && !bInbound)
                {
                    outLegTimeS = -1.0;
                    return PHASE_TURN;
                }
                outLegTimeS = bOutbound ? wrapped : wrapped - cycle.LegS - cycle.TurnS;
                const double arcM = NpcRailLegRefAt(ship, l0, cycle.LegS, outLegTimeS).ArcM;
                if (FMath::Min(arcM, l0 - arcM) < profile.MetricZoneM)
                {
                    return PHASE_ZONE;
                }
                return FMath::Min(outLegTimeS, cycle.LegS - outLegTimeS) <= ship.AccelS ? PHASE_ACCELERATE
                    : PHASE_COAST;
            };

            for (int32 sample = 0; sample < timesPerCycle; ++sample)
            {
                // Re-based so the cycle time falls 9,000 s into the epoch window, seen from the Mid radius
                const double tau = (static_cast<double>(sample) + 0.5) / timesPerCycle * cycle.CycleS;
                const FSOLNpcRail gpuRail = SOLNpcRail::AdvancedToEpoch(rail, NpcRailTimeAt(rail, cycle, tau) - 9000.0,
                    frame.LaneProfile);
                const FVector3d viewpoint = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, 9000.0)
                    + laneFrame.Up * viewDistanceM;
                FVector4f rows[SOL::NPC_GROUP_TEXTURE_ROWS];
                NpcRailGpuRows(frame, 0, viewpoint, texels, rows);
                float previousT = static_cast<float>(9000.0);
                FVector3d previousCpu = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(previousT));
                FVector3f previousGpu = NpcRailGpuLaneRelM(gpuRail, frame.LaneProfile, rows, previousT);
                double previousLegTimeS = 0.0;
                phaseAt(tau, previousLegTimeS);
                for (int32 step = 1; step <= frameCount; ++step)
                {
                    // One frame: the mirror's step against the CPU rail's at the same float times
                    const float t = static_cast<float>(9000.0 + static_cast<double>(step) / 60.0);
                    double legTimeS = 0.0;
                    const int32 phase = phaseAt(tau + static_cast<double>(step) / 60.0, legTimeS);
                    const FVector3d cpu = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(t));
                    const FVector3f gpu = NpcRailGpuLaneRelM(gpuRail, frame.LaneProfile, rows, t);
                    const double cpuStepM = (cpu - previousCpu).Size();
                    const double gpuStepM = FVector3d(gpu.X - previousGpu.X, gpu.Y - previousGpu.Y,
                        gpu.Z - previousGpu.Z).Size();
                    const bool bSwitch = legTimeS >= 0.0 && previousLegTimeS >= 0.0
                        && (legTimeS < 0.5 * cycle.LegS) != (previousLegTimeS < 0.5 * cycle.LegS);
                    ++phaseFrames[phase];
                    repeats[phase] += gpuStepM == 0.0 && cpuStepM > 1.0 ? 1 : 0;
                    if (!bSwitch)
                    {
                        worstStepPx[phase] = FMath::Max(worstStepPx[phase], FMath::Abs(gpuStepM - cpuStepM) / pixelM);
                    }
                    previousCpu = cpu;
                    previousGpu = gpu;
                    previousLegTimeS = legTimeS;
                }
            }
        }
        int32 laneRepeats = 0;
        FString detail;
        for (int32 phase = 0; phase < 4; ++phase)
        {
            laneRepeats += repeats[phase];
            detail += FString::Printf(TEXT(" [%s: %d of %d frames repeat, worst step off the CPU %.3f px]"),
                phaseNames[phase], repeats[phase], phaseFrames[phase], worstStepPx[phase]);
        }
        AddInfo(laneLabel + TEXT(":") + detail);
        TestEqual(laneLabel + TEXT(": no repeated float positions over the whole cycle at 60 Hz") + detail, laneRepeats,
            0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailLaneHandOverMarginTest, "SOLTest.NpcRail.LaneHandOverMargin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.6 (the Mid hand-over): SOLNpcRail::LaneHandOverMarginM bounds the far layer's float error, so a
// lane ship the shader hides (float length below the Mid radius) is always within the Mid radius plus the margin by its
// double position, where the tiers have made it a CPU ship. On every default lane (and Earth - Mars at 0.3 of its
// length), for hashed ships re-based to six epochs over the cycle and evaluated 0, 5,000 and 9,999.5 s into each
// epoch window, seen from 1,000 km and from the Mid radius plus the margin, the float mirror lies within the margin of
// the CPU rail; the margin is 0 for an unknown lane and positive and finite otherwise. The worst error over the margin
// is logged per lane
bool FSOLNpcRailLaneHandOverMarginTest::RunTest(const FString& /*parameters*/)
{
    constexpr double midRadiusM = 2.0e6;
    TArray<FVector4f> texels;
    struct FMarginCase
    {
        int32 Lane;
        double Ratio;
    };
    TArray<FMarginCase> cases;
    for (int32 lane = 0; lane < UE_ARRAY_COUNT(NPCRAIL_DEFAULT_LANES); ++lane)
    {
        cases.Add({ lane, 1.0 });
    }
    cases.Add({ 3, 0.3 });
    for (const FMarginCase& marginCase : cases)
    {
        const FNpcRailDefaultLane& row = NPCRAIL_DEFAULT_LANES[marginCase.Lane];
        const FString laneLabel = FString::Printf(TEXT("%s - %s at %.2f L0"), row.BodyA, row.BodyB, marginCase.Ratio);
        FNpcRailWorld world;
        if (!NpcRailDefaultWorld(row, marginCase.Ratio, world))
        {
            AddError(laneLabel + TEXT(": not in the default layout"));
            continue;
        }

        // The sample ships and their lane's largest peak, as the spawn records it
        const FSOLLaneProfile& tableProfile = world.Groups.LaneProfile[0];
        TArray<FSOLNpcRail> rails;
        for (int32 shipIndex = 0; shipIndex < 24; ++shipIndex)
        {
            const FNpcRailSpec spec = NpcRailHashedSpec(static_cast<uint64>(marginCase.Lane) + 29ull, shipIndex);
            rails.Add(SOLNpcRail::FromSpawn(NpcRailLaneSpawn(spec), 0, tableProfile, 0.0));
        }
        SOLNpcRail::AccumulateLaneMaxPeak(rails, world.Groups.LaneMaxPeakMps);
        const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
        const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
        const double marginM = SOLNpcRail::LaneHandOverMarginM(world.Groups, frame, 0, midRadiusM);
        TestTrue(laneLabel + FString::Printf(TEXT(": margin positive and finite (%.3f km)"), marginM * 1.0e-3),
            FMath::IsFinite(marginM) && marginM > 0.0);

        double worstRatio = 0.0;
        int32 failures = 0;
        FString firstFailure;
        for (int32 shipIndex = 0; shipIndex < rails.Num(); ++shipIndex)
        {
            const FSOLNpcRail& rail = rails[shipIndex];
            const FSOLLaneCycle cycle = NpcRailCycleOf(frame.LaneProfile[0], rail);
            for (int32 epoch = 0; epoch < 6; ++epoch)
            {
                const double deltaS = (static_cast<double>(epoch) + 0.61) / 6.0 * cycle.CycleS;
                const FSOLNpcRail gpuRail = SOLNpcRail::AdvancedToEpoch(rail, deltaS, frame.LaneProfile);
                for (const float t : { 0.0f, 5000.0f, 9999.5f })
                {
                    // The CPU (double) position seen from 1,000 km and from the grown Mid radius
                    const FVector3d cpu = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(t));
                    for (const double viewM : { 1.0e6, midRadiusM + marginM })
                    {
                        const FVector3d viewpoint = cpu + (laneFrame.Up * 0.8 + laneFrame.Right * 0.6) * viewM;
                        FVector4f rows[SOL::NPC_GROUP_TEXTURE_ROWS];
                        NpcRailGpuRows(frame, 0, viewpoint, texels, rows);
                        const FVector3f gpu = NpcRailGpuLaneRelM(gpuRail, frame.LaneProfile, rows, t);
                        const double errorM = (FVector3d(gpu.X, gpu.Y, gpu.Z) - (cpu - viewpoint)).Size();
                        worstRatio = FMath::Max(worstRatio, errorM / marginM);
                        if (!(errorM <= marginM))
                        {
                            if (failures == 0)
                            {
                                firstFailure = FString::Printf(TEXT("ship %d epoch %d t %.1f view %.0f km: %.6e m "
                                    "above %.6e m"), shipIndex, epoch, t, viewM * 1.0e-3, errorM, marginM);
                            }
                            ++failures;
                        }
                    }
                }
            }
        }
        AddInfo(laneLabel + FString::Printf(TEXT(": hand-over margin %.3f km, worst float error %.3f of it"),
            marginM * 1.0e-3, worstRatio));
        TestTrue(laneLabel + FString::Printf(TEXT(": float mirror within the hand-over margin (%d failures; first: %s)"),
            failures, *firstFailure), failures == 0);
    }

    // An unknown lane, and a lane without a lane frame, have no margin
    FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
    TestEqual(TEXT("unknown lane: margin 0"), SOLNpcRail::LaneHandOverMarginM(world.Groups, frame, 5, midRadiusM), 0.0);
    FSOLNpcRailFrame invalidFrame = frame;
    invalidFrame.LaneFrame[0].bValid = false;
    TestEqual(TEXT("lane without a lane frame: margin 0"),
        SOLNpcRail::LaneHandOverMarginM(world.Groups, invalidFrame, 0, midRadiusM), 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailHaloCapsuleTest, "SOLTest.NpcRail.HaloCapsule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.2: a lane's culling volume (GroupDistanceM) is the capsule A - B of radius H x Rw (the halo's
// outer radius) when the lane has a profile, else the lane radius R as before
bool FSOLNpcRailHaloCapsuleTest::RunTest(const FString& /*parameters*/)
{
    FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
    const FSOLLaneProfile& profile = world.Groups.LaneProfile[0];
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
    const double rw = profile.WallRadiusM;
    const double outer = SOL::NPC_LANE_HALO_OUTER_WALL_RADII * rw;
    TestEqual(TEXT("halo outer radius H Rw"), profile.HaloOuterRadiusM, outer, 1.0e-9 * outer);
    const FVector3d mid = 0.5 * (laneFrame.AM + laneFrame.BM);
    const double tolM = 1.0e-6 * rw + 1.0e-2;
    TestEqual(TEXT("1.5 Rw off the axis: inside the halo capsule"), SOLNpcRail::GroupDistanceM(world.Groups, frame, 0,
        mid + laneFrame.Up * (1.5 * rw)), 0.0, tolM);
    TestEqual(TEXT("2.5 Rw off the axis: 0.5 Rw outside"), SOLNpcRail::GroupDistanceM(world.Groups, frame, 0,
        mid + laneFrame.Right * (2.5 * rw)), 0.5 * rw, tolM);
    TestEqual(TEXT("3 Rw beyond B on the axis: Rw outside the end cap"), SOLNpcRail::GroupDistanceM(world.Groups,
        frame, 0, laneFrame.BM + laneFrame.Axis * (3.0 * rw)), outer > 3.0 * rw ? 0.0 : 3.0 * rw - outer, tolM);

    // Without a profile: the lane radius R
    world.Groups.LaneProfile.Reset();
    const FSOLNpcRailFrame bareFrame = NpcRailFrameAt(world, 0.0);
    TestEqual(TEXT("no profile: capsule of radius R"), SOLNpcRail::GroupDistanceM(world.Groups, bareFrame, 0,
        mid + laneFrame.Up * (2.5 * NPCRAIL_LANE_RADIUS_M)), 1.5 * NPCRAIL_LANE_RADIUS_M, 1.0e-6 * NPCRAIL_LANE_RADIUS_M
        + 1.0e-2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailLaneEdgeCasesTest, "SOLTest.NpcRail.LaneEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Edge cases of the lane rail: a lane that is not drivable (zero, negative, NaN or infinite nominal length; zero lane
// radius) gives finite constants and a ship held at rest on the midpoint disc (the 50a closed form); NaN spawn values
// still pack a finite rail with P0 in [0, 1); a lane rail whose group has no lane frame evaluates to a finite point
bool FSOLNpcRailLaneEdgeCasesTest::RunTest(const FString& /*parameters*/)
{
    for (const double badLength : { 0.0, -1.0e9, NPCRAIL_NAN, NPCRAIL_INF })
    {
        FNpcRailWorld world = NpcRailMakeWorld();
        const int32 inWall[] = { NPCRAIL_IN_WALL };
        const int32 halo[] = { NPCRAIL_HALO };
        const double nominal[] = { badLength };
        const double radiusA[] = { NPCRAIL_EARTH_RADIUS_M };
        const double radiusB[] = { NPCRAIL_MARS_RADIUS_M };
        SOLNpcRail::BuildLaneProfiles(world.Groups, inWall, halo, nominal, radiusA, radiusB);
        const FString label = FString::Printf(TEXT("nominal length %f"), badLength);
        TestFalse(label + TEXT(": not drivable"), world.Groups.LaneProfile[0].bDrivable);
        const FSOLNpcRail rail = NpcRailLane(world, NPCRAIL_SPECS[0]);
        bool bFinite = true;
        for (int32 index = 0; index < 7; ++index)
        {
            bFinite = bFinite && FMath::IsFinite(rail.P[index]);
        }
        TestTrue(label + TEXT(": finite rail"), bFinite && rail.P[6] == 0.0f);
        const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
        const FVector3d atZero = SOLNpcRail::EvaluateAtTimeM(rail, frame, 0.0);
        const FVector3d later = SOLNpcRail::EvaluateAtTimeM(rail, frame, 1.0e6);
        TestTrue(label + TEXT(": at rest"), atZero == later);
        TestTrue(label + TEXT(": the closed form's midpoint disc"), (atZero - SOLLaneTraffic::LaneShipPositionM(
            frame.LaneProfile[0], frame.LaneFrame[0], SOLNpcRail::LaneShipFromRail(rail), 0.0)).Size()
            <= NPCRAIL_POSITION_TOL_M);
    }

    // Zero lane radius: zero wall radius, not drivable
    FNpcRailWorld thin = NpcRailMakeWorld();
    thin.Groups.LaneRadiusM = { 0.0 };
    const int32 inWall[] = { NPCRAIL_IN_WALL };
    const int32 halo[] = { NPCRAIL_HALO };
    const double nominal[] = { NPCRAIL_NOMINAL_LENGTH_M };
    const double radiusA[] = { NPCRAIL_EARTH_RADIUS_M };
    const double radiusB[] = { NPCRAIL_MARS_RADIUS_M };
    SOLNpcRail::BuildLaneProfiles(thin.Groups, inWall, halo, nominal, radiusA, radiusB);
    TestFalse(TEXT("zero lane radius: not drivable"), thin.Groups.LaneProfile[0].bDrivable);

    // NaN spawn values: a finite rail, P0 in [0, 1)
    const FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLNpcRail nanRail = NpcRailLane(world, { NPCRAIL_NAN, NPCRAIL_NAN, NPCRAIL_NAN, NPCRAIL_NAN });
    bool bNanFinite = true;
    for (int32 index = 0; index < 7; ++index)
    {
        bNanFinite = bNanFinite && FMath::IsFinite(nanRail.P[index]);
    }
    TestTrue(TEXT("NaN spawn: finite rail"), bNanFinite);
    TestTrue(TEXT("NaN spawn: P0 in [0, 1)"), nanRail.P[0] >= 0.0f && nanRail.P[0] < 1.0f);

    // A lane rail on a group with no lane frame: finite
    FSOLNpcRail stray = NpcRailLane(world, NPCRAIL_SPECS[0]);
    stray.Group = 0;
    FSOLNpcRailFrame noLanes = NpcRailFrameAt(world, 0.0);
    noLanes.LaneFrame.Reset();
    noLanes.LaneProfile.Reset();
    const FVector3d strayM = SOLNpcRail::EvaluateAtTimeM(stray, noLanes, 10.0);
    TestTrue(TEXT("no lane frame: finite"), FMath::IsFinite(strayM.X) && FMath::IsFinite(strayM.Y)
        && FMath::IsFinite(strayM.Z));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailLaneHullForwardTest, "SOLTest.NpcRail.LaneHullForward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contract case 10 (D21): SOLNpcRail::LaneHullForward(rail, frame, t) is SOLLaneTraffic::LaneHullForward(the frame's
// lane profile, the frame's lane frame, LaneShipFromRail(rail), t) within 1e-9 (negative, tiny and huge times
// included), unit length, continuous at the float rail's four joints and across each leg's flip window (angle steps
// below 1.5 pi dt / T_f), facing travel before the window, Up at its middle and against travel after it; the zero
// vector for cluster and battle rails and for a lane rail whose group has no lane frame (the caller keeps the velocity
// heading)
bool FSOLNpcRailLaneHullForwardTest::RunTest(const FString& /*parameters*/)
{
    const double tf = SOL::NPC_LANE_FLIP_S;
    const double peakAngleRate = 1.5 * NPCRAIL_PI / tf;     // The smoothstep flip's largest rate (rad/s)

    // Returns the angle in radians between two vectors (accurate near 0 and pi)
    const auto angleBetween = [](const FVector3d& a, const FVector3d& b)
    {
        return FMath::Atan2(FVector3d::CrossProduct(a, b).Size(), FVector3d::DotProduct(a, b));
    };

    for (const double distanceRatio : { 1.0, 0.57 })
    {
        const FNpcRailWorld world = NpcRailMakeWorld(distanceRatio * NPCRAIL_NOMINAL_LENGTH_M);
        const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
        TestTrue(FString::Printf(TEXT("distance %.2f L0: one lane frame"), distanceRatio),
            frame.LaneProfile.Num() >= 1 && frame.LaneFrame.Num() >= 1);
        if (frame.LaneProfile.Num() < 1 || frame.LaneFrame.Num() < 1)
        {
            continue;
        }
        const FSOLLaneProfile& profile = frame.LaneProfile[0];
        const FSOLLaneFrame& laneFrame = frame.LaneFrame[0];
        for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
        {
            const FSOLNpcRail rail = NpcRailLane(world, spec);
            const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);
            const FSOLLaneCycle cycle = NpcRailCycleOf(profile, rail);
            const FString label = FString::Printf(TEXT("distance %.2f L0, rho %.3f"), distanceRatio, spec.Rho);
            TestTrue(label + TEXT(": the ship burns (so its legs flip)"), ship.AccelS > 0.0);

            // Returns the rail's hull at cycle time tau
            const auto hullAt = [&](const double tau)
            {
                return SOLNpcRail::LaneHullForward(rail, frame, NpcRailTimeAt(rail, cycle, tau));
            };

            // The pure function's value and unit length: fixed times, 200 spread over the cycle, 8 in each U-turn
            TArray<double, TInlineAllocator<232>> times = { -3.7e7, -1234.5, 0.0, 0.5, 777.25, 9.99e4, 3.3e6, 2.2e8 };
            for (int32 sample = 0; sample < 200; ++sample)
            {
                times.Add(NpcRailTimeAt(rail, cycle, cycle.CycleS * (static_cast<double>(sample) + 0.5) / 200.0));
            }
            for (const double turnStart : { cycle.LegS, 2.0 * cycle.LegS + cycle.TurnS })
            {
                for (int32 sample = 0; sample < 8; ++sample)
                {
                    times.Add(NpcRailTimeAt(rail, cycle, turnStart + cycle.TurnS * (static_cast<double>(sample) + 0.5)
                        / 8.0));
                }
            }
            int32 valueFailures = 0;
            FString firstValueFailure;
            for (const double t : times)
            {
                const FVector3d hull = SOLNpcRail::LaneHullForward(rail, frame, t);
                const FVector3d pure = SOLLaneTraffic::LaneHullForward(profile, laneFrame, ship, t);
                if (FMath::Abs(hull.Size() - 1.0) > 1.0e-12 || (hull - pure).GetAbsMax() > 1.0e-9)
                {
                    if (valueFailures == 0)
                    {
                        firstValueFailure = FString::Printf(TEXT("t %.9e: rail (%.12f, %.12f, %.12f), pure (%.12f, "
                            "%.12f, %.12f)"), t, hull.X, hull.Y, hull.Z, pure.X, pure.Y, pure.Z);
                    }
                    ++valueFailures;
                }
            }
            TestEqual(label + FString::Printf(TEXT(": unit and = SOLLaneTraffic::LaneHullForward (first failure: %s)"),
                *firstValueFailure), valueFailures, 0);

            // The four joints of the float rail: the brake attitude is held through the turns, so no jump
            const double jointDelta = 1.0e-3;
            for (const double jointTau : { cycle.LegS, cycle.LegS + cycle.TurnS, 2.0 * cycle.LegS + cycle.TurnS,
                cycle.CycleS })
            {
                const double angle = angleBetween(hullAt(jointTau - jointDelta), hullAt(jointTau + jointDelta));
                TestTrue(label + FString::Printf(TEXT(": continuous at the joint tau %.9e (%.3e rad)"), jointTau,
                    angle), angle <= peakAngleRate * 2.0 * jointDelta + 1.0e-9);
            }

            // Each leg's flip window: travel 1 s before it, Up at its middle, against travel 1 s after it, and angle
            // steps below the peak rate in 0.5 s steps from 5 s before to 5 s after it
            const double start = 0.5 * (cycle.LegS - tf);
            const double windowStep = 0.5;
            for (const bool bOutbound : { true, false })
            {
                const double legStart = bOutbound ? 0.0 : cycle.LegS + cycle.TurnS;
                const FVector3d travel = bOutbound ? laneFrame.Axis : FVector3d(-laneFrame.Axis);
                const FString legLabel = label + (bOutbound ? TEXT(", outbound") : TEXT(", inbound"));
                TestTrue(legLabel + TEXT(": travel before the window"),
                    (hullAt(legStart + start - 1.0) - travel).GetAbsMax() <= 1.0e-9);
                TestTrue(legLabel + TEXT(": Up at the window's middle"),
                    (hullAt(legStart + 0.5 * cycle.LegS) - laneFrame.Up).GetAbsMax() <= 1.0e-6);
                TestTrue(legLabel + TEXT(": against travel after the window"),
                    (hullAt(legStart + start + tf + 1.0) + travel).GetAbsMax() <= 1.0e-9);
                double largestStep = 0.0;
                FVector3d previous = hullAt(legStart + start - 5.0);
                for (int32 step = 1; step <= 140; ++step)
                {
                    const FVector3d hull = hullAt(legStart + start - 5.0 + windowStep * static_cast<double>(step));
                    largestStep = FMath::Max(largestStep, angleBetween(previous, hull));
                    previous = hull;
                }
                TestTrue(legLabel + FString::Printf(TEXT(": smooth across the window (largest step %.6e rad)"),
                    largestStep), largestStep <= peakAngleRate * windowStep + 1.0e-9 && largestStep > 0.0);
            }
        }
    }

    // Cluster and battle rails, and a lane rail whose group has no lane frame: the zero vector
    const FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
    TestTrue(TEXT("cluster rail: zero vector"),
        SOLNpcRail::LaneHullForward(NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f), frame, 10.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("battle rail: zero vector"),
        SOLNpcRail::LaneHullForward(NpcRailBattle(), frame, 10.0) == FVector3d::ZeroVector);
    FSOLNpcRailFrame noLanes = frame;
    noLanes.LaneFrame.Reset();
    noLanes.LaneProfile.Reset();
    TestTrue(TEXT("no lane frame: zero vector"),
        SOLNpcRail::LaneHullForward(NpcRailLane(world, NPCRAIL_SPECS[0]), noLanes, 10.0) == FVector3d::ZeroVector);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailGpuRailTexelsTest, "SOLTest.NpcRail.GpuRailTexels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.6 GPU upload: GpuRailTexels writes the rail re-based by AdvancedToEpoch as (kind + 4 faction + 8
// group, P0, P1, P2), (P3, P4, P5, P6), except that a lane rail with a profile carries its time into the cycle at the
// epoch as a high / low float pair (high + low = phase x T_cyc in double within one float step of the low part, low
// below half a float step of high) in P0 / P5, its leg time T_leg (rounded once) in P6 and T_leg's rounding as the
// packed value's fraction (within +-0.25, decoded within 1e-4 of a step of T_leg); cluster and battle rails are packed
// unchanged
bool FSOLNpcRailGpuRailTexelsTest::RunTest(const FString& /*parameters*/)
{
    const FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 0.0);
    for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
    {
        const FSOLNpcRail rail = NpcRailLane(world, spec);
        const FSOLLaneCycle cycle = NpcRailCycleOf(frame.LaneProfile[0], rail);
        for (const double deltaS : { 0.0, 1234.5, 0.61 * cycle.CycleS, 7.3 * cycle.CycleS })
        {
            const FString label = FString::Printf(TEXT("rho %.3f, delta %.1f s"), spec.Rho, deltaS);
            FVector4f texelA;
            FVector4f texelB;
            SOLNpcRail::GpuRailTexels(rail, deltaS, frame.LaneProfile, texelA, texelB);
            const FSOLNpcRail advanced = SOLNpcRail::AdvancedToEpoch(rail, deltaS, frame.LaneProfile);
            const double tauS = SOLNpcRail::LaneShipFromRail(advanced).Phase * cycle.CycleS;
            const float packed = static_cast<float>(advanced.Kind + 4 * advanced.Faction + 8 * advanced.Group);
            TestTrue(label + TEXT(": packed kind, faction, group (the integer, the fraction within +-0.25)"),
                FMath::RoundHalfToEven(texelA.X) == packed && FMath::Abs(texelA.X - packed) <= 0.25f);
            const double legLowS = static_cast<double>(texelA.X - packed) * 2.0
                * static_cast<double>(SOLNpcRail::GpuLegUlpS(texelB.W));
            TestTrue(label + TEXT(": T_leg's rounding carried in the fraction"),
                FMath::Abs(static_cast<double>(texelB.W) + legLowS - cycle.LegS)
                <= 1.0e-4 * static_cast<double>(SOLNpcRail::GpuLegUlpS(texelB.W)));
            TestTrue(label + TEXT(": tau0 high + low"), FMath::Abs(static_cast<double>(texelA.Y)
                + static_cast<double>(texelB.Z) - tauS) <= FMath::Max(1.0e-9, NpcRailFloatUlp(texelB.Z)));
            TestTrue(label + TEXT(": the low part is below half a float step"),
                FMath::Abs(static_cast<double>(texelB.Z)) <= 0.5 * NpcRailFloatUlp(texelA.Y));
            TestTrue(label + TEXT(": dv, offsets, t_a unchanged"), texelA.Z == advanced.P[1]
                && texelA.W == advanced.P[2] && texelB.X == advanced.P[3] && texelB.Y == advanced.P[4]);
            TestTrue(label + TEXT(": T_leg rounded once"), texelB.W == static_cast<float>(cycle.LegS));
        }
    }
    FVector4f texelA;
    FVector4f texelB;
    const FSOLNpcRail cluster = NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f);
    SOLNpcRail::GpuRailTexels(cluster, 500.0, frame.LaneProfile, texelA, texelB);
    const FSOLNpcRail advanced = SOLNpcRail::AdvancedToEpoch(cluster, 500.0, frame.LaneProfile);
    TestTrue(TEXT("cluster: the re-based rail packed unchanged"), texelA == FVector4f(static_cast<float>(
        advanced.Kind + 4 * advanced.Faction + 8 * advanced.Group), advanced.P[0], advanced.P[1], advanced.P[2])
        && texelB == FVector4f(advanced.P[3], advanced.P[4], advanced.P[5], advanced.P[6]));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailHullHeadingTest, "SOLTest.NpcRail.HullHeading",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D17 / D21 in the tiers: SOLNpcRail::HullHeadingVelocityMps (of a Motion result) turns a lane rail's hull to its
// thrust facing. With no deviation the hull (SOLNpcAi::HullForward of the heading) faces LaneHullForward, engine first
// while braking; a small deviation turns it by at most asin(|deviation| / max(rail speed, w)) and a huge one dominates;
// a stopped rail still weighs its facing like the wall speed (a perpendicular deviation of w turns it 45 degrees).
// Motion's velocity and facing equal VelocityMps and LaneHullForward, and the heading is facing x max(speed, w) +
// deviation. The Full tier's nose without a target (SOLNpcAi::FullShipDesiredForward on the rail heading,
// SOLNpcCombat) is the tiers' hull heading, so neither hand-off turns the hull. Cluster and battle rails and a lane
// rail without a lane frame keep velocity + deviation
bool FSOLNpcRailHullHeadingTest::RunTest(const FString& /*parameters*/)
{
    const double wall = SOL::NPC_LANE_WALL_SPEED_MPS;
    const FNpcRailWorld world = NpcRailMakeWorld();
    const FVector3d deviation(120.0, -45.0, 30.0);
    const FVector3d up = FVector3d::ZAxisVector;
    bool bSawBraking = false;
    for (const double timeFraction : { 0.0, 0.03, 0.5, 0.97 })
    {
        for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
        {
            // The rail time: the fraction of the ship's outbound leg (the last one in its brake)
            const FSOLNpcRail rail = NpcRailLane(world, spec);
            const FSOLLaneCycle cycle = NpcRailCycleOf(NpcRailFrameAt(world, 0.0).LaneProfile[0], rail);
            const double railTime = NpcRailTimeAt(rail, cycle, timeFraction * cycle.LegS);
            const FSOLNpcRailFrame frame = NpcRailFrameAt(world, railTime);
            const FString label = FString::Printf(TEXT("leg %.2f, rho %.3f"), timeFraction, spec.Rho);
            const FVector3d railVelocity = SOLNpcRail::VelocityMps(rail, frame);
            const FVector3d forward = SOLNpcRail::LaneHullForward(rail, frame, frame.RailTimeS);
            const FSOLNpcRailMotion motion = SOLNpcRail::Motion(rail, frame);
            TestTrue(label + TEXT(": lane hull facing exists"), !forward.IsZero());

            // No deviation: the hull faces its thrust (engine first while braking)
            const FVector3d still = SOLNpcRail::HullHeadingVelocityMps(motion, FVector3d::ZeroVector);
            const FVector3d hull = SOLNpcAi::HullForward(still, up);
            TestTrue(label + TEXT(": no deviation, the hull faces LaneHullForward"),
                (hull - forward).GetAbsMax() <= 1.0e-9);
            bSawBraking = bSawBraking || FVector3d::DotProduct(hull, railVelocity.GetSafeNormal()) < -0.99;

            // A small deviation turns the hull a little, a huge one dominates
            const double turnRad = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(SOLNpcAi::HullForward(
                SOLNpcRail::HullHeadingVelocityMps(motion, deviation), up), forward), -1.0, 1.0));
            TestTrue(label + TEXT(": a small deviation turns the hull by at most asin(|dev| / max(speed, w))"),
                turnRad <= FMath::Asin(deviation.Size() / FMath::Max(motion.VelocityMps.Size(), wall)) + 1.0e-9);
            const FVector3d huge(0.0, 0.0, 1.0e9);
            TestTrue(label + TEXT(": a huge deviation dominates the heading"),
                FVector3d::DotProduct(SOLNpcRail::HullHeadingVelocityMps(motion, huge).GetSafeNormal(),
                    FVector3d::ZAxisVector) > 0.999);

            // A stopped rail (its facing, no velocity): a perpendicular deviation of the wall speed turns it 45 degrees
            FVector3d sideAxis;
            FVector3d otherAxis;
            forward.FindBestAxisVectors(sideAxis, otherAxis);
            const FVector3d side = sideAxis.GetSafeNormal() * wall;
            FSOLNpcRailMotion stoppedMotion;
            stoppedMotion.LaneHullForward = forward;
            const FVector3d stopped = SOLNpcRail::HullHeadingVelocityMps(stoppedMotion, side);
            const double stoppedRad = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(stopped.GetSafeNormal(), forward),
                -1.0, 1.0));
            TestTrue(label + TEXT(": a stopped rail weighs its facing like the wall speed"),
                FMath::Abs(stoppedRad - 0.25 * NPCRAIL_PI) <= 1.0e-6);

            // Motion (one leg derivation) agrees with VelocityMps and LaneHullForward; the heading is built from them
            const double speedTolMps = 1.0e-6 * railVelocity.Size() + 1.0e-3;
            TestTrue(label + TEXT(": Motion velocity equals VelocityMps"),
                (motion.VelocityMps - railVelocity).GetAbsMax() <= speedTolMps);
            TestTrue(label + TEXT(": Motion hull facing equals LaneHullForward"),
                (motion.LaneHullForward - forward).GetAbsMax() <= 1.0e-12);
            TestTrue(label + TEXT(": the heading is facing x max(speed, w) + deviation"),
                (SOLNpcRail::HullHeadingVelocityMps(motion, deviation)
                    - (forward * FMath::Max(railVelocity.Size(), wall) + deviation)).GetAbsMax() <= speedTolMps);

            // The Full tier's nose without a target: the tiers' hull heading (no turn at promotion or demotion)
            const FVector3d railHeading = SOLNpcRail::HullHeadingVelocityMps(motion, FVector3d::ZeroVector);
            const FVector3d nose = SOLNpcAi::FullShipDesiredForward(FVector3d::ZeroVector, 0.0, 1.0e4,
                railHeading + FVector3d::ZeroVector, railHeading + deviation, -forward);
            TestTrue(label + TEXT(": Full nose without a target equals the tier hull heading"),
                FVector3d::DotProduct(nose, SOLNpcAi::HullForward(SOLNpcRail::HullHeadingVelocityMps(motion,
                    FVector3d::ZeroVector), up)) >= 1.0 - 1.0e-9);
        }
    }
    TestTrue(TEXT("a braking ship was covered (engine first, against its velocity)"), bSawBraking);

    // Other rails and a lane rail without a lane frame have no lane facing and keep the velocity heading
    const FSOLNpcRailFrame frame = NpcRailFrameAt(world, 4321.5);
    FSOLNpcRailMotion velocityOnly;
    velocityOnly.VelocityMps = FVector3d(3.0e4, -2.0e3, 7.0);
    TestTrue(TEXT("no lane facing: velocity + deviation"),
        SOLNpcRail::HullHeadingVelocityMps(velocityOnly, deviation) == velocityOnly.VelocityMps + deviation);
    const FSOLNpcRailMotion cluster = SOLNpcRail::Motion(NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f), frame);
    TestTrue(TEXT("cluster rail: Motion has no lane facing, heading velocity + deviation"),
        cluster.LaneHullForward.IsZero()
        && SOLNpcRail::HullHeadingVelocityMps(cluster, deviation) == cluster.VelocityMps + deviation);
    TestTrue(TEXT("battle rail: Motion has no lane facing"),
        SOLNpcRail::Motion(NpcRailBattle(), frame).LaneHullForward.IsZero());
    FSOLNpcRailFrame noLanes = frame;
    noLanes.LaneFrame.Reset();
    noLanes.LaneProfile.Reset();
    const FSOLNpcRailMotion unframed = SOLNpcRail::Motion(NpcRailLane(world, NPCRAIL_SPECS[0]), noLanes);
    TestTrue(TEXT("no lane frame: Motion has no lane facing, heading velocity + deviation"),
        unframed.LaneHullForward.IsZero()
        && SOLNpcRail::HullHeadingVelocityMps(unframed, deviation) == unframed.VelocityMps + deviation);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailFullTierHandoffTest, "SOLTest.NpcRail.FullTierHandoff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D17 / D21 through a Full-tier stay, without a combat world: a lane ship drawn as a Mid hull (SOLNpcAi::HullForward
// of the rail heading plus deviation velocity, as USOLNpcTierProcessor) is promoted (SOLNpcAi::HandoffOrientation of
// the same heading, as USOLNpcFullAiProcessor), flown by the Full step without a target (FullShipDesiredForward on the
// rail heading, StepFullShip in the class envelope, the idle return to the rail, as SOLNpcCombat) at 60 Hz from 10 s
// before its hull flip to 10 s after it, and demoted. The promotion frame does not turn the nose (0.01 degrees), no
// frame turns it faster than the class's angular rate, it follows the Mid heading through the 60 s flip (lag within 15
// degrees), and at the demotion the Mid heading it is drawn with is within 3 degrees of the nose
bool FSOLNpcRailFullTierHandoffTest::RunTest(const FString& /*parameters*/)
{
    const FNpcRailWorld world = NpcRailMakeWorld();
    const FSOLThrustEnvelope envelope = SOLNpcAi::ClassEnvelope(SOLNpcAi::GetClass(0));
    const SOLNpcAi::FClassParams& shipClass = SOLNpcAi::GetClass(0);
    constexpr double dt = 1.0 / 60.0;
    const double maxRateRadPerS = FMath::Max(envelope.MaxSpinRateRadPerS,
        FVector3d(0.0, envelope.MaxAngularRateRadPerS.Y, envelope.MaxAngularRateRadPerS.Z).Size());
    const double frameLimitRad = 1.01 * maxRateRadPerS * dt + 1.0e-9;
    int32 flips = 0;
    for (const FNpcRailSpec& spec : NPCRAIL_SPECS)
    {
        const FSOLNpcRail rail = NpcRailLane(world, spec);
        const FSOLLaneCycle cycle = NpcRailCycleOf(NpcRailFrameAt(world, 0.0).LaneProfile[0], rail);
        if (!(rail.P[4] > 0.0f) || !(cycle.LegS > 2.0 * SOL::NPC_LANE_FLIP_S + 40.0))
        {
            continue;
        }
        ++flips;
        const FString label = FString::Printf(TEXT("rho %.3f"), spec.Rho);

        // The Mid hull heading at a rail time (the tier processor's): the rail heading plus deviation velocity
        const auto midHeading = [&](const FSOLNpcRailFrame& frame, const FSOLShipState& deviation)
        {
            const FVector3d up = SOLNpcRail::EvaluateM(rail, frame) + deviation.PositionM - frame.GroupBodyM[0];
            return SOLNpcAi::HullForward(SOLNpcRail::HullHeadingVelocityMps(SOLNpcRail::Motion(rail, frame),
                deviation.VelocityMps), up);
        };

        // Promotion 10 s before the flip starts (the outbound leg's middle minus half the flip), a small deviation
        const double startS = NpcRailTimeAt(rail, cycle, 0.5 * (cycle.LegS - SOL::NPC_LANE_FLIP_S) - 10.0);
        FSOLShipState flight;
        flight.PositionM = FVector3d(120.0, -40.0, 25.0);
        flight.VelocityMps = FVector3d(15.0, 6.0, -4.0);
        const FSOLNpcRailFrame promoteFrame = NpcRailFrameAt(world, startS);
        const FVector3d midBefore = midHeading(promoteFrame, flight);
        const FVector3d upHint = SOLNpcRail::EvaluateM(rail, promoteFrame) + flight.PositionM
            - promoteFrame.GroupBodyM[0];
        flight.Orientation = SOLNpcAi::HandoffOrientation(SOLNpcRail::HullHeadingVelocityMps(
            SOLNpcRail::Motion(rail, promoteFrame), flight.VelocityMps), upHint);
        const double promoteDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            flight.Orientation.GetForwardVector() | midBefore, -1.0, 1.0)));
        TestTrue(label + FString::Printf(TEXT(": promotion keeps the nose (%.4f deg)"), promoteDeg),
            promoteDeg <= 0.01);

        // The Full stay: no target, idle return to the rail, nose on the rail heading
        const int32 frames = FMath::CeilToInt32((SOL::NPC_LANE_FLIP_S + 20.0) / dt);
        double worstStepRad = 0.0;
        double worstLagDeg = 0.0;
        FSOLNpcRailFrame frame = promoteFrame;
        for (int32 step = 1; step <= frames; ++step)
        {
            frame = NpcRailFrameAt(world, startS + step * dt);
            const FVector3d railHeading = SOLNpcRail::HullHeadingVelocityMps(SOLNpcRail::Motion(rail, frame),
                FVector3d::ZeroVector);
            const FVector3d desiredMps = (-flight.PositionM * SOL::NPC_AI_IDLE_RETURN_RATE)
                .GetClampedToMaxSize(shipClass.MaxSpeedMps);
            const FVector3d noseBefore = flight.Orientation.GetForwardVector();
            const FVector3d desiredForward = SOLNpcAi::FullShipDesiredForward(FVector3d::ZeroVector, 0.0,
                shipClass.FireRangeM, railHeading + desiredMps, railHeading + flight.VelocityMps, noseBefore);
            const FVector3d up = SOLNpcRail::EvaluateM(rail, frame) + flight.PositionM - frame.GroupBodyM[0];
            flight = SOLNpcAi::StepFullShip(flight, desiredMps, desiredForward, up, envelope, dt);
            const FVector3d nose = flight.Orientation.GetForwardVector();
            worstStepRad = FMath::Max(worstStepRad, FMath::Acos(FMath::Clamp(nose | noseBefore, -1.0, 1.0)));
            worstLagDeg = FMath::Max(worstLagDeg, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
                nose | midHeading(frame, flight), -1.0, 1.0))));
        }
        const double demoteDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            flight.Orientation.GetForwardVector() | midHeading(frame, flight), -1.0, 1.0)));
        AddInfo(label + FString::Printf(TEXT(": promotion %.4f deg, worst frame %.3f deg (limit %.3f), flip lag %.2f "
            "deg, demotion %.3f deg"), promoteDeg, FMath::RadiansToDegrees(worstStepRad),
            FMath::RadiansToDegrees(frameLimitRad), worstLagDeg, demoteDeg));
        TestTrue(label + TEXT(": no frame turns the nose faster than the class's angular rate"),
            worstStepRad <= frameLimitRad);
        TestTrue(label + TEXT(": the nose follows the Mid heading through the flip (15 deg)"), worstLagDeg <= 15.0);
        TestTrue(label + TEXT(": demotion keeps the nose (3 deg)"), demoteDeg <= 3.0);
    }
    TestTrue(TEXT("a ship with a hull flip was covered"), flips > 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailCpuPointHandOverTest, "SOLTest.NpcRail.CpuPointHandOver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.6, the Mid hand-over of a lane ship's point (SOLNpcTrafficMirror::CpuPointHandOver): the CPU draws
// it exactly where the shader hides it (the mirror's float length below the Mid radius, none at or beyond it), at the
// mirror's position at the Mid radius, at the double position at or inside the hull radius, smoothstepped between;
// a hull radius beyond the Mid radius means no blend band (the double position inside)
bool FSOLNpcRailCpuPointHandOverTest::RunTest(const FString& /*parameters*/)
{
    const float midM = 2.0e6f;
    const double hullM = 2.0e5;
    const FVector3d offsetM(3.0e4, -1.0e4, 5.0e3);
    const FVector3f direction = FVector3f(0.6f, 0.0f, 0.8f);

    // Returns the hand-over of a mirror point at a distance along the direction and the double point offset from it
    const auto handOverAt = [&](const double distanceM, const double hullStartM)
    {
        const FVector3f mirror = direction * static_cast<float>(distanceM);
        return SOLNpcTrafficMirror::CpuPointHandOver(mirror, FVector3d(mirror) + offsetM, midM, hullStartM);
    };
    TestFalse(TEXT("at the Mid radius the far layer draws it"), handOverAt(midM, hullM).bCpuDraws);
    TestFalse(TEXT("beyond the Mid radius the far layer draws it"), handOverAt(3.0e6, hullM).bCpuDraws);
    const float justInside = std::nextafter(midM, 0.0f);
    const SOLNpcTrafficMirror::FHandOver inside = handOverAt(justInside, hullM);
    TestTrue(TEXT("just inside the Mid radius the CPU draws it"), inside.bCpuDraws);
    TestTrue(TEXT("...at the mirror's position (no pop)"),
        FVector3d(inside.RelM - direction * justInside).Size() <= 1.0e-4 * offsetM.Size() + 1.0);
    TestTrue(TEXT("at the hull radius: the double position"),
        FVector3d(handOverAt(hullM, hullM).RelM).Equals(FVector3d(direction * static_cast<float>(hullM)) + offsetM,
        1.0));
    TestTrue(TEXT("inside the hull radius: the double position"),
        FVector3d(handOverAt(5.0e4, hullM).RelM).Equals(FVector3d(direction * 5.0e4f) + offsetM, 1.0));

    // Between: the offset from the mirror shrinks monotonically toward the Mid radius, half way at the band's middle
    double previousOffsetM = offsetM.Size() + 1.0;
    for (int32 step = 0; step <= 100; ++step)
    {
        const double distanceM = hullM + (static_cast<double>(midM) - hullM) * step / 100.0;
        const SOLNpcTrafficMirror::FHandOver handOver = handOverAt(distanceM, hullM);
        const double fromMirrorM = (FVector3d(handOver.RelM) - FVector3d(direction * static_cast<float>(distanceM)))
            .Size();
        if (step < 100)
        {
            TestTrue(FString::Printf(TEXT("band %d %%: the CPU draws it"), step), handOver.bCpuDraws);
        }
        TestTrue(FString::Printf(TEXT("band %d %%: the offset from the mirror never grows"), step),
            fromMirrorM <= previousOffsetM + 1.0);
        previousOffsetM = fromMirrorM;
    }
    const double middleM = 0.5 * (hullM + static_cast<double>(midM));
    TestTrue(TEXT("band middle: half way"), FMath::Abs((FVector3d(handOverAt(middleM, hullM).RelM)
        - FVector3d(direction * static_cast<float>(middleM))).Size() - 0.5 * offsetM.Size()) <= 1.0);
    TestTrue(TEXT("a hull radius beyond the Mid radius: the double position inside"),
        FVector3d(handOverAt(1.0e6, 5.0e6).RelM).Equals(FVector3d(direction * 1.0e6f) + offsetM, 1.0));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
