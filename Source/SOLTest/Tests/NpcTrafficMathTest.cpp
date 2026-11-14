/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneTrafficMath.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"
#include "Universe/SOLRenderPlacement.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #50 lane traffic math (SOLLaneTraffic, SDD 50 section 6 cases 1-8, 11, 12 and 15), written
// against that contract before the production code existed (part 50a, test first). Doubles are compared to 1e-9
// relative unless a case says otherwise; positions at universe scale are compared relative to the nearer end body,
// where double keeps sub-millimetre precision. Frames: universe ecliptic metres, right-handed, +Z north. The names of
// the API under test are the ones recorded in the plan's "Part 50a API contract (from the test author)" section, as
// amended by the 50a review (MakeLaneProfile takes the wall radius, the leg time is derived by LaneShipLegS).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCTRAFFIC_TOL = 1.0e-9;
    constexpr double NPCTRAFFIC_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double NPCTRAFFIC_INF = std::numeric_limits<double>::infinity();
    constexpr double NPCTRAFFIC_PI = 3.141592653589793238462643383279502884;
    constexpr double NPCTRAFFIC_STANDARD_GRAVITY = 9.80665;
    constexpr double NPCTRAFFIC_POSITION_TOL_M = 1.0e-3;            // Positions near a body (time rounding x speed)

    // Mean body radii (m)
    constexpr double NPCTRAFFIC_MERCURY_RADIUS_M = 2.4397e6;
    constexpr double NPCTRAFFIC_VENUS_RADIUS_M = 6.0518e6;
    constexpr double NPCTRAFFIC_EARTH_RADIUS_M = 6.371e6;
    constexpr double NPCTRAFFIC_MOON_RADIUS_M = 1.7374e6;
    constexpr double NPCTRAFFIC_MARS_RADIUS_M = 3.3895e6;
    constexpr double NPCTRAFFIC_JUPITER_RADIUS_M = 6.9911e7;
    constexpr double NPCTRAFFIC_SATURN_RADIUS_M = 5.8232e7;
    constexpr double NPCTRAFFIC_URANUS_RADIUS_M = 2.5362e7;
    constexpr double NPCTRAFFIC_NEPTUNE_RADIUS_M = 2.4622e7;

    // Aphelion distances (m); a lane's nominal length is the sum of its two bodies' (D16), the Moon's apoapsis for
    // Earth - Moon
    constexpr double NPCTRAFFIC_MERCURY_APHELION_M = 6.982e10;
    constexpr double NPCTRAFFIC_VENUS_APHELION_M = 1.0894e11;
    constexpr double NPCTRAFFIC_EARTH_APHELION_M = 1.5210e11;
    constexpr double NPCTRAFFIC_MARS_APHELION_M = 2.4923e11;
    constexpr double NPCTRAFFIC_JUPITER_APHELION_M = 8.1662e11;
    constexpr double NPCTRAFFIC_SATURN_APHELION_M = 1.5145e12;
    constexpr double NPCTRAFFIC_URANUS_APHELION_M = 3.0064e12;
    constexpr double NPCTRAFFIC_NEPTUNE_APHELION_M = 4.5372e12;
    constexpr double NPCTRAFFIC_MOON_APOAPSIS_M = 4.055e8;

    // The two test lanes (SOLNpcScale::DefaultLayout radii, in-wall counts of a large population)
    constexpr double NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M = 3.0e6;
    constexpr int32 NPCTRAFFIC_EARTH_MARS_IN_WALL = 30000;
    constexpr double NPCTRAFFIC_EARTH_MARS_MAX_M = NPCTRAFFIC_EARTH_APHELION_M + NPCTRAFFIC_MARS_APHELION_M;
    constexpr double NPCTRAFFIC_EARTH_MOON_LANE_RADIUS_M = 2.0e5;
    constexpr int32 NPCTRAFFIC_EARTH_MOON_IN_WALL = 20000;

    // SDD 50 section 4.3 example legs
    constexpr double NPCTRAFFIC_SHORT_LEG_M = 3.95e8;               // Earth - Moon, uncapped
    constexpr double NPCTRAFFIC_LONG_LEG_M = 2.25e11;               // Earth - Mars at 1.5 AU, capped

    // Failure counter of a sampled check, with the first failure's description
    struct FNpcTrafficFailures
    {
        int32 Count = 0;
        FString First;

        // Counts one failure and keeps its description if it is the first
        void Add(const FString& description)
        {
            if (Count == 0)
            {
                First = description;
            }
            ++Count;
        }
    };

    // A sample ship's dimensionless spawn values
    struct FNpcTrafficShipSpec
    {
        double Phase = 0.0;
        double Effort = 1.0;
        double Rho = 0.0;
        double ClockRad = 0.5 * NPCTRAFFIC_PI;
    };

    // A test lane: its profile, a frame (fixed in time) and its end bodies' radii
    struct FNpcTrafficLane
    {
        FString Name;
        FSOLLaneProfile Profile;
        FSOLLaneFrame Frame;
        double BodyRadiusAM = 0.0;
        double BodyRadiusBM = 0.0;
        bool bEcliptic = true;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the wall speed w
    double NpcTrafficWall()
    {
        return SOL::NPC_LANE_WALL_SPEED_MPS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the peak acceleration a in m/s^2
    double NpcTrafficAccel()
    {
        return SOL::NPC_LANE_PEAK_ACCEL_MPS2;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the speed cap c
    double NpcTrafficCap()
    {
        return SOL::NPC_LANE_SPEED_CAP_MPS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two doubles agree within a relative tolerance plus an absolute one (false for any NaN)
    bool NpcTrafficNear(const double actual, const double expected, const double relTol, const double absTol = 0.0)
    {
        return FMath::Abs(actual - expected)
            <= relTol * FMath::Max(FMath::Abs(actual), FMath::Abs(expected)) + absTol;
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds an error to a test when two doubles do not agree within a relative plus absolute tolerance
    void NpcTrafficExpectNear(FAutomationTestBase& test, const FString& what, const double actual,
        const double expected, const double relTol, const double absTol = 0.0)
    {
        if (!NpcTrafficNear(actual, expected, relTol, absTol))
        {
            test.AddError(FString::Printf(TEXT("%s: %.15e, expected %.15e"), *what, actual, expected));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Reports a sampled check: an error with the count and the first failure when anything failed
    void NpcTrafficReport(FAutomationTestBase& test, const FString& what, const FNpcTrafficFailures& failures)
    {
        if (failures.Count > 0)
        {
            test.AddError(FString::Printf(TEXT("%s: %d failures, first: %s"), *what, failures.Count,
                *failures.First));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a deterministic uniform value in [0, 1) for a key (splitmix64)
    double NpcTrafficUnit(const uint64 key)
    {
        uint64 z = key + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z = z ^ (z >> 31);
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's smoothstep(e0, e1, x) = s^2 (3 - 2 s), s clamped to [0, 1]
    double NpcTrafficSmoothstep(const double e0, const double e1, const double x)
    {
        const double s = FMath::Clamp((x - e0) / (e1 - e0), 0.0, 1.0);
        return s * s * (3.0 - 2.0 * s);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's end-zone ramp J(u) for a nominal leg and zone (SDD 50 section 4.5), P(s) = s^3 - s^4 / 2
    double NpcTrafficRefJ(const double u, const double l0, const double z)
    {
        if (u < z)
        {
            const double s = u / z;
            return z * (s * s * s - 0.5 * s * s * s * s);
        }
        if (u <= l0 - z)
        {
            return 0.5 * z + (u - z);
        }
        const double s = (l0 - u) / z;
        return (l0 - z) - z * (s * s * s - 0.5 * s * s * s * s);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's metric map X(u) = u + (l - l0) J(u) / J(l0), Z = NPC_LANE_METRIC_ZONE_FRACTION x l0
    double NpcTrafficRefX(const double u, const double l0, const double l)
    {
        const double z = SOL::NPC_LANE_METRIC_ZONE_FRACTION * l0;
        return u + (l - l0) * NpcTrafficRefJ(u, l0, z) / NpcTrafficRefJ(l0, l0, z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns sample ship 'index' of a lane: three of four inside the wall, one of four in the halo, hashed phase and
    // effort, and a clock angle strictly inside (0, pi) so every ship has a non-zero right offset
    FNpcTrafficShipSpec NpcTrafficSpec(const uint64 laneKey, const int32 index)
    {
        FNpcTrafficShipSpec spec;
        const uint64 base = laneKey * 1000003ull + static_cast<uint64>(index) * 8ull;
        spec.Phase = NpcTrafficUnit(base);
        spec.Effort = SOL::NPC_LANE_EFFORT_MIN
            + (SOL::NPC_LANE_EFFORT_MAX - SOL::NPC_LANE_EFFORT_MIN) * NpcTrafficUnit(base + 1);
        const double h = NpcTrafficUnit(base + 2);
        spec.Rho = (index % 4 == 3) ? 1.02 + 0.96 * h : 0.02 + 0.96 * h;
        spec.ClockRad = NPCTRAFFIC_PI * (0.05 + 0.9 * NpcTrafficUnit(base + 3));
        return spec;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's profile with the wall radius computed as the caller does it: the #49 tunnel radius of the lane
    // radius and the in-wall ship count (D13)
    FSOLLaneProfile NpcTrafficProfile(const double laneRadiusM, const int32 inWallCount, const double nominalLengthM,
        const double radiusAM, const double radiusBM)
    {
        return SOLLaneTraffic::MakeLaneProfile(SOLNpcTunnel::TunnelRadiusM(laneRadiusM, inWallCount), nominalLengthM,
            radiusAM, radiusBM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a test lane: its profile at the nominal (maximum) length and a frame with A at aM and B along a direction
    // such that the current leg is lengthRatio x the nominal leg
    FNpcTrafficLane NpcTrafficMakeLane(const TCHAR* name, const double laneRadiusM, const int32 inWallCount,
        const double nominalLengthM, const double radiusAM, const double radiusBM, const FVector3d& aM,
        const FVector3d& direction, const double lengthRatio)
    {
        FNpcTrafficLane lane;
        lane.Name = name;
        lane.Profile = NpcTrafficProfile(laneRadiusM, inWallCount, nominalLengthM, radiusAM, radiusBM);
        const double lengthM = lane.Profile.EndArcAM + lane.Profile.EndArcBM + lengthRatio * lane.Profile.NominalLegM;
        lane.Frame = SOLLaneTraffic::MakeLaneFrame(lane.Profile, aM, aM + direction.GetSafeNormal() * lengthM);
        lane.BodyRadiusAM = radiusAM;
        lane.BodyRadiusBM = radiusBM;
        lane.bEcliptic = FMath::Abs(direction.Z) < 1.0e-12;
        return lane;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the case 8 lanes: Earth - Mars in the ecliptic at its nominal length and at 0.15 of it, and Earth - Moon
    // inclined 7 degrees above the ecliptic at its nominal length, all far from the universe origin
    TArray<FNpcTrafficLane> NpcTrafficTestLanes()
    {
        const FVector3d aM(1.2e11, -8.0e10, 3.0e6);
        const FVector3d ecliptic(0.6, 0.8, 0.0);
        const double elevation = FMath::DegreesToRadians(7.0);
        const double azimuth = FMath::DegreesToRadians(40.0);
        const FVector3d inclined(FMath::Cos(elevation) * FMath::Cos(azimuth),
            FMath::Cos(elevation) * FMath::Sin(azimuth), FMath::Sin(elevation));

        TArray<FNpcTrafficLane> lanes;
        lanes.Add(NpcTrafficMakeLane(TEXT("Earth - Mars, l = l0"), NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
            NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
            NPCTRAFFIC_MARS_RADIUS_M, aM, ecliptic, 1.0));
        lanes.Add(NpcTrafficMakeLane(TEXT("Earth - Mars, l = 0.15 l0"), NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
            NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
            NPCTRAFFIC_MARS_RADIUS_M, aM, ecliptic, 0.15));
        lanes.Add(NpcTrafficMakeLane(TEXT("Earth - Moon inclined, l = l0"), NPCTRAFFIC_EARTH_MOON_LANE_RADIUS_M,
            NPCTRAFFIC_EARTH_MOON_IN_WALL, NPCTRAFFIC_MOON_APOAPSIS_M, NPCTRAFFIC_EARTH_RADIUS_M,
            NPCTRAFFIC_MOON_RADIUS_M, aM, inclined, 1.0));
        return lanes;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rail time at which a ship is at cycle time tau
    double NpcTrafficTimeAt(const FSOLLaneShip& ship, const FSOLLaneCycle& cycle, const double tau)
    {
        return (tau / cycle.CycleS - ship.Phase) * cycle.CycleS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship's position relative to an origin at rail time t (the lane's frame held fixed)
    FVector3d NpcTrafficPos(const FNpcTrafficLane& lane, const FSOLLaneShip& ship, const double t,
        const FVector3d& originM)
    {
        return SOLLaneTraffic::LaneShipPositionRelM(lane.Profile, lane.Frame, ship, t, originM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship's velocity by a central difference over +-h (the lane's frame held fixed)
    FVector3d NpcTrafficVel(const FNpcTrafficLane& lane, const FSOLLaneShip& ship, const double t, const double h)
    {
        return (NpcTrafficPos(lane, ship, t + h, lane.Frame.AM) - NpcTrafficPos(lane, ship, t - h, lane.Frame.AM))
            / (2.0 * h);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a relative position's (axial, up, right) coordinates in the lane frame's basis
    FVector3d NpcTrafficLocal(const FSOLLaneFrame& frame, const FVector3d& relM)
    {
        return FVector3d(FVector3d::DotProduct(relM, frame.Axis), FVector3d::DotProduct(relM, frame.Up),
            FVector3d::DotProduct(relM, frame.Right));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the horizontal screen coordinate (x right, divided by depth) of an ecliptic point seen by a camera at
    // cameraM looking along the ecliptic direction lookDirection, in the Unreal frame with the camera's up = Unreal +Z
    double NpcTrafficScreenX(const FVector3d& cameraM, const FVector3d& lookDirection, const FVector3d& pointM)
    {
        const FVector3d forward = SOLRender::EclipticToUnreal(lookDirection).GetSafeNormal();
        const FVector3d right = FVector3d::CrossProduct(FVector3d::UnitZ(), forward).GetSafeNormal();
        const FVector3d relative = SOLRender::EclipticToUnreal(pointM - cameraM);
        return FVector3d::DotProduct(relative, right) / FVector3d::DotProduct(relative, forward);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every field of a leg profile is finite and not negative
    bool NpcTrafficLegFiniteNonNegative(const FSOLLaneLeg& leg)
    {
        const double fields[] = { leg.Alpha, leg.PeakMps, leg.DvMps, leg.AccelS, leg.AccelM, leg.CoastS, leg.LegS };
        for (const double value : fields)
        {
            if (!FMath::IsFinite(value) || value < 0.0)
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two leg profiles agree field by field (1e-12 relative)
    bool NpcTrafficSameLeg(const FSOLLaneLeg& a, const FSOLLaneLeg& b)
    {
        return a.bValid == b.bValid && NpcTrafficNear(a.Alpha, b.Alpha, 1.0e-12)
            && NpcTrafficNear(a.PeakMps, b.PeakMps, 1.0e-12) && NpcTrafficNear(a.DvMps, b.DvMps, 1.0e-12)
            && NpcTrafficNear(a.AccelS, b.AccelS, 1.0e-12) && NpcTrafficNear(a.AccelM, b.AccelM, 1.0e-12)
            && NpcTrafficNear(a.CoastS, b.CoastS, 1.0e-12) && NpcTrafficNear(a.LegS, b.LegS, 1.0e-12);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathConstantsTest, "SOLTest.NpcTrafficMath.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1: the proposed traffic values (asserted so a tuning change is a visible test edit) and their relations
bool FSOLNpcTrafficMathConstantsTest::RunTest(const FString& /*parameters*/)
{
    // Speeds and acceleration (D3, D4)
    TestEqual(TEXT("wall speed 20 km/s"), static_cast<double>(SOL::NPC_LANE_WALL_SPEED_MPS), 2.0e4, NPCTRAFFIC_TOL);
    TestEqual(TEXT("peak acceleration 1.65 G"), static_cast<double>(SOL::NPC_LANE_PEAK_ACCEL_G), 1.65,
        NPCTRAFFIC_TOL);
    TestEqual(TEXT("peak acceleration = 1.1 x the sustained pilot G"), static_cast<double>(SOL::NPC_LANE_PEAK_ACCEL_G),
        1.1 * SOL::PILOT_SUSTAINED_G, 1.0e-12);
    TestEqual(TEXT("peak acceleration in m/s^2 = 1.65 x 9.80665"), static_cast<double>(SOL::NPC_LANE_PEAK_ACCEL_MPS2),
        1.65 * NPCTRAFFIC_STANDARD_GRAVITY, 1.0e-12);
    TestEqual(TEXT("speed cap 300 km/s"), static_cast<double>(SOL::NPC_LANE_SPEED_CAP_MPS), 3.0e5, NPCTRAFFIC_TOL);
    TestTrue(TEXT("cap above the wall speed"), SOL::NPC_LANE_SPEED_CAP_MPS > SOL::NPC_LANE_WALL_SPEED_MPS);

    // Effort (D4)
    TestEqual(TEXT("effort min 0.5"), static_cast<double>(SOL::NPC_LANE_EFFORT_MIN), 0.5, NPCTRAFFIC_TOL);
    TestEqual(TEXT("effort max 1.0"), static_cast<double>(SOL::NPC_LANE_EFFORT_MAX), 1.0, NPCTRAFFIC_TOL);
    TestEqual(TEXT("effort mean 0.75"), static_cast<double>(SOL::NPC_LANE_EFFORT_MEAN), 0.75, NPCTRAFFIC_TOL);
    TestTrue(TEXT("min < mean < max"), SOL::NPC_LANE_EFFORT_MIN < SOL::NPC_LANE_EFFORT_MEAN
        && SOL::NPC_LANE_EFFORT_MEAN < SOL::NPC_LANE_EFFORT_MAX);

    // Halo (D6) and radial profile (section 4.3)
    TestEqual(TEXT("halo fraction 0.25"), static_cast<double>(SOL::NPC_LANE_HALO_FRACTION), 0.25, NPCTRAFFIC_TOL);
    TestEqual(TEXT("halo outer radius 2 wall radii"), static_cast<double>(SOL::NPC_LANE_HALO_OUTER_WALL_RADII), 2.0,
        NPCTRAFFIC_TOL);
    TestEqual(TEXT("halo kappa 0.01"), static_cast<double>(SOL::NPC_LANE_HALO_KAPPA), 0.01, NPCTRAFFIC_TOL);
    TestTrue(TEXT("halo kappa in (0, 1)"), SOL::NPC_LANE_HALO_KAPPA > 0.0 && SOL::NPC_LANE_HALO_KAPPA < 1.0);
    TestEqual(TEXT("halo ramp 0.25 wall radii"), static_cast<double>(SOL::NPC_LANE_HALO_RAMP_WALL_RADII), 0.25,
        NPCTRAFFIC_TOL);
    TestTrue(TEXT("halo ramp < outer - 1"),
        SOL::NPC_LANE_HALO_RAMP_WALL_RADII < SOL::NPC_LANE_HALO_OUTER_WALL_RADII - 1.0);

    // U-turn (D1) and the metric end zones (section 4.5)
    TestEqual(TEXT("U-turn depth 2 wall radii"), static_cast<double>(SOL::NPC_LANE_UTURN_DEPTH_WALL_RADII), 2.0,
        NPCTRAFFIC_TOL);
    TestEqual(TEXT("metric zone 5 % of the nominal leg"), static_cast<double>(SOL::NPC_LANE_METRIC_ZONE_FRACTION),
        0.05, NPCTRAFFIC_TOL);
    TestTrue(TEXT("metric zone in (0, 0.5)"), SOL::NPC_LANE_METRIC_ZONE_FRACTION > 0.0
        && SOL::NPC_LANE_METRIC_ZONE_FRACTION < 0.5);

    // Merge readout (D8) and AUTO (D9, D20)
    TestEqual(TEXT("merge match 5 %"), static_cast<double>(SOL::NPC_LANE_MERGE_MATCH_FRACTION), 0.05,
        NPCTRAFFIC_TOL);
    TestEqual(TEXT("merge amber 20 %"), static_cast<double>(SOL::NPC_LANE_MERGE_AMBER_FRACTION), 0.20,
        NPCTRAFFIC_TOL);
    TestTrue(TEXT("match < amber"), SOL::NPC_LANE_MERGE_MATCH_FRACTION < SOL::NPC_LANE_MERGE_AMBER_FRACTION);
    TestEqual(TEXT("merge direction minimum 1 km/s"), static_cast<double>(SOL::NPC_LANE_MERGE_DIRECTION_MIN_MPS),
        1000.0, NPCTRAFFIC_TOL);
    TestEqual(TEXT("AUTO track 0.25 wall radii"), static_cast<double>(SOL::NPC_LANE_AUTO_TRACK_WALL_RADII), 0.25,
        NPCTRAFFIC_TOL);
    TestEqual(TEXT("AUTO minimum overlap 20 wall radii"),
        static_cast<double>(SOL::NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII), 20.0, NPCTRAFFIC_TOL);

    // Hull flip (D17)
    TestEqual(TEXT("hull flip 60 s"), static_cast<double>(SOL::NPC_LANE_FLIP_S), 60.0, NPCTRAFFIC_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathEndMarginTest, "SOLTest.NpcTrafficMath.EndMarginD14",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1 (D14): the end rings sit 3 wall radii above the surface, so the 2-radius U-turn's apex is one wall radius
// above the planet
bool FSOLNpcTrafficMathEndMarginTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("end margin 3 wall radii (D14)"), static_cast<double>(SOL::NPC_TUNNEL_END_MARGIN_RADII), 3.0,
        NPCTRAFFIC_TOL);
    TestTrue(TEXT("U-turn depth below the end margin (apex above the surface)"),
        SOL::NPC_LANE_UTURN_DEPTH_WALL_RADII < SOL::NPC_TUNNEL_END_MARGIN_RADII);
    TestEqual(TEXT("apex one wall radius above the surface"),
        static_cast<double>(SOL::NPC_TUNNEL_END_MARGIN_RADII - SOL::NPC_LANE_UTURN_DEPTH_WALL_RADII), 1.0,
        NPCTRAFFIC_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathRadialFactorTest, "SOLTest.NpcTrafficMath.RadialFactor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 2: K = 1 - rho^2 inside the wall (1 on the axis with zero slope, 0 at the wall, strictly decreasing); in the
// halo kappa_h x smoothstep(1, 1 + r_h, rho) (0 at the wall, kappa_h from 1 + r_h on, non-decreasing, continuous);
// negative rho reads as its magnitude, NaN as 0
bool FSOLNpcTrafficMathRadialFactorTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::RadialFactor;
    const double kappaHalo = SOL::NPC_LANE_HALO_KAPPA;
    const double ramp = SOL::NPC_LANE_HALO_RAMP_WALL_RADII;
    const double outer = SOL::NPC_LANE_HALO_OUTER_WALL_RADII;

    // Inside the wall: the parabola
    TestEqual(TEXT("1 on the axis"), RadialFactor(0.0), 1.0, NPCTRAFFIC_TOL);
    TestEqual(TEXT("0 at the wall"), RadialFactor(1.0), 0.0, NPCTRAFFIC_TOL);
    TestEqual(TEXT("1 - 0.3^2 at 0.3"), RadialFactor(0.3), 1.0 - 0.09, NPCTRAFFIC_TOL);
    TestEqual(TEXT("1 - 0.7^2 at 0.7"), RadialFactor(0.7), 1.0 - 0.49, NPCTRAFFIC_TOL);
    double previous = RadialFactor(0.0);
    for (int32 step = 1; step <= 100; ++step)
    {
        const double rho = static_cast<double>(step) / 100.0;
        const double factor = RadialFactor(rho);
        TestTrue(FString::Printf(TEXT("strictly decreasing at rho %.2f"), rho), factor < previous);
        previous = factor;
    }
    const double slopeAtAxis = (RadialFactor(1.0e-6) - RadialFactor(0.0)) / 1.0e-6;
    TestTrue(FString::Printf(TEXT("zero slope on the axis (%.3e)"), slopeAtAxis), FMath::Abs(slopeAtAxis) <= 1.0e-5);

    // Halo: the ramped plateau
    TestEqual(TEXT("halo: kappa_h at 1 + r_h"), RadialFactor(1.0 + ramp), kappaHalo, NPCTRAFFIC_TOL);
    TestEqual(TEXT("halo: kappa_h at 1.5"), RadialFactor(1.5), kappaHalo, NPCTRAFFIC_TOL);
    TestEqual(TEXT("halo: kappa_h at the outer radius"), RadialFactor(outer), kappaHalo, NPCTRAFFIC_TOL);
    TestEqual(TEXT("halo: kappa_h beyond the outer radius"), RadialFactor(5.0), kappaHalo, NPCTRAFFIC_TOL);
    TestEqual(TEXT("halo: smoothstep interior point (s = 0.4)"), RadialFactor(1.0 + 0.4 * ramp),
        kappaHalo * 0.16 * (3.0 - 0.8), NPCTRAFFIC_TOL);
    TestEqual(TEXT("halo: smoothstep interior point (s = 0.75)"), RadialFactor(1.0 + 0.75 * ramp),
        kappaHalo * NpcTrafficSmoothstep(1.0, 1.0 + ramp, 1.0 + 0.75 * ramp), NPCTRAFFIC_TOL);
    previous = RadialFactor(1.0);
    for (int32 step = 1; step <= 200; ++step)
    {
        const double rho = 1.0 + (outer - 1.0) * static_cast<double>(step) / 200.0;
        const double factor = RadialFactor(rho);
        TestTrue(FString::Printf(TEXT("halo: non-decreasing at rho %.3f"), rho), factor >= previous - 1.0e-15);
        TestTrue(FString::Printf(TEXT("halo: in [0, kappa_h] at rho %.3f"), rho),
            factor >= 0.0 && factor <= kappaHalo + 1.0e-15);
        previous = factor;
    }

    // Continuity at the wall and at the end of the ramp
    TestTrue(TEXT("continuous at the wall (inside)"), FMath::Abs(RadialFactor(1.0 - 1.0e-9)) <= 3.0e-9);
    TestTrue(TEXT("continuous at the wall (halo side)"), FMath::Abs(RadialFactor(1.0 + 1.0e-9)) <= 1.0e-9);
    TestTrue(TEXT("continuous at 1 + r_h"),
        FMath::Abs(RadialFactor(1.0 + ramp - 1.0e-9) - kappaHalo) <= 1.0e-9);

    // Negative and bad input
    TestEqual(TEXT("negative rho inside reads as its magnitude"), RadialFactor(-0.3), RadialFactor(0.3),
        NPCTRAFFIC_TOL);
    TestEqual(TEXT("negative rho in the halo reads as its magnitude"), RadialFactor(-1.1), RadialFactor(1.1),
        NPCTRAFFIC_TOL);
    TestEqual(TEXT("NaN -> 0"), RadialFactor(NPCTRAFFIC_NAN), 0.0, NPCTRAFFIC_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathLegProfileTest, "SOLTest.NpcTrafficMath.LegProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 3: the per-ship flip-and-burn leg: uncapped short leg (peak sqrt(w^2 + a l0) at mid-leg, no coast), capped
// long leg (accelerate / coast / brake at c), the effort scale, kappa 0 (wall speed throughout), the kappa -> 0 limit,
// the cap blend c_k^2 = w^2 + kappa (c^2 - w^2), and the bookkeeping identities for every combination
bool FSOLNpcTrafficMathLegProfileTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::MakeLegProfile;
    const double w = NpcTrafficWall();
    const double a = NpcTrafficAccel();
    const double c = NpcTrafficCap();

    // Uncapped short leg: axis ship at full effort
    {
        const double l0 = NPCTRAFFIC_SHORT_LEG_M;
        const FSOLLaneLeg leg = MakeLegProfile(1.0, 1.0, l0);
        const double peak = FMath::Sqrt(w * w + a * l0);
        TestTrue(TEXT("short: valid"), leg.bValid);
        NpcTrafficExpectNear(*this, TEXT("short: alpha = a"), leg.Alpha, a, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("short: peak sqrt(w^2 + a l0)"), leg.PeakMps, peak, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("short: peak 82.4 km/s within 0.1 %"), leg.PeakMps, 82.4e3, 1.0e-3);
        TestTrue(TEXT("short: below the cap"), leg.PeakMps < c);
        NpcTrafficExpectNear(*this, TEXT("short: dv = peak - w"), leg.DvMps, peak - w, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("short: accelerate time dv / a"), leg.AccelS, (peak - w) / a,
            NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("short: no coast"), leg.CoastS, 0.0, 0.0, 1.0e-6);
        NpcTrafficExpectNear(*this, TEXT("short: accelerate distance l0 / 2"), leg.AccelM, 0.5 * l0, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("short: leg time 2 t_a + t_c"), leg.LegS, 2.0 * leg.AccelS + leg.CoastS,
            NPCTRAFFIC_TOL);
    }

    // Capped long leg: accelerate, coast at c, brake
    {
        const double l0 = NPCTRAFFIC_LONG_LEG_M;
        const FSOLLaneLeg leg = MakeLegProfile(1.0, 1.0, l0);
        const double accelM = (c * c - w * w) / (2.0 * a);
        TestTrue(TEXT("long: valid"), leg.bValid);
        NpcTrafficExpectNear(*this, TEXT("long: peak = c"), leg.PeakMps, c, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("long: dv = c - w"), leg.DvMps, c - w, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("long: accelerate time (c - w) / a"), leg.AccelS, (c - w) / a,
            NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("long: accelerate distance (c^2 - w^2) / 2a"), leg.AccelM, accelM,
            NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("long: coast (l0 - 2 d_a) / c"), leg.CoastS, (l0 - 2.0 * accelM) / c,
            NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("long: leg time 2 t_a + t_c"), leg.LegS, 2.0 * leg.AccelS + leg.CoastS,
            NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("long: accelerate about 4.8 h"), leg.AccelS, 4.8 * 3600.0, 0.01);
    }

    // Effort scales the acceleration (kappa 1)
    {
        const FSOLLaneLeg leg = MakeLegProfile(1.0, 0.6, NPCTRAFFIC_SHORT_LEG_M);
        NpcTrafficExpectNear(*this, TEXT("effort 0.6: alpha = 0.6 a"), leg.Alpha, 0.6 * a, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("effort 0.6: peak sqrt(w^2 + 0.6 a l0)"), leg.PeakMps,
            FMath::Sqrt(w * w + 0.6 * a * NPCTRAFFIC_SHORT_LEG_M), NPCTRAFFIC_TOL);
    }

    // kappa 0: the wall speed for the whole leg
    for (const double l0 : { NPCTRAFFIC_SHORT_LEG_M, NPCTRAFFIC_LONG_LEG_M })
    {
        const FString label = FString::Printf(TEXT("kappa 0, leg %.3e m"), l0);
        const FSOLLaneLeg leg = MakeLegProfile(0.0, 1.0, l0);
        TestTrue(label + TEXT(": valid"), leg.bValid);
        NpcTrafficExpectNear(*this, label + TEXT(": peak w"), leg.PeakMps, w, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, label + TEXT(": alpha 0"), leg.Alpha, 0.0, 0.0, 1.0e-15);
        NpcTrafficExpectNear(*this, label + TEXT(": dv 0"), leg.DvMps, 0.0, 0.0, 1.0e-12);
        NpcTrafficExpectNear(*this, label + TEXT(": no accelerate time"), leg.AccelS, 0.0, 0.0, 1.0e-12);
        NpcTrafficExpectNear(*this, label + TEXT(": leg time l0 / w"), leg.LegS, l0 / w, NPCTRAFFIC_TOL);
    }

    // LegS continuous as kappa -> 0: faster with any kappa, by at most about kappa (c^2 - w^2) / w^2
    for (const double l0 : { NPCTRAFFIC_SHORT_LEG_M, NPCTRAFFIC_LONG_LEG_M })
    {
        const double legAtZero = MakeLegProfile(0.0, 1.0, l0).LegS;
        double previous = 0.0;
        for (const double kappa : { 1.0e-6, 1.0e-9, 0.0 })
        {
            const FString label = FString::Printf(TEXT("kappa %.0e, leg %.3e m"), kappa, l0);
            const FSOLLaneLeg leg = MakeLegProfile(kappa, 1.0, l0);
            const double bound = kappa * (c * c - w * w) / (w * w) + 1.0e-9;
            TestTrue(label + TEXT(": valid and finite"), leg.bValid && NpcTrafficLegFiniteNonNegative(leg));
            TestTrue(label + FString::Printf(TEXT(": LegS within %.3e of l0 / w"), bound),
                FMath::Abs(leg.LegS / legAtZero - 1.0) <= bound);
            TestTrue(label + TEXT(": LegS not above l0 / w"), leg.LegS <= legAtZero * (1.0 + 1.0e-12));
            TestTrue(label + TEXT(": LegS non-decreasing as kappa falls"), leg.LegS >= previous * (1.0 - 1.0e-12));
            previous = leg.LegS;
        }
    }

    // The cap blend: kappa 0.5 on the long leg, and the halo kappa (36 km/s, SDD section 4.3)
    {
        const FSOLLaneLeg leg = MakeLegProfile(0.5, 1.0, NPCTRAFFIC_LONG_LEG_M);
        NpcTrafficExpectNear(*this, TEXT("kappa 0.5: alpha = 0.5 a"), leg.Alpha, 0.5 * a, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("kappa 0.5: peak^2 = w^2 + 0.5 (c^2 - w^2)"), leg.PeakMps * leg.PeakMps,
            w * w + 0.5 * (c * c - w * w), NPCTRAFFIC_TOL);
        const FSOLLaneLeg halo = MakeLegProfile(SOL::NPC_LANE_HALO_KAPPA, 1.0, NPCTRAFFIC_LONG_LEG_M);
        NpcTrafficExpectNear(*this, TEXT("halo kappa: peak^2 = w^2 + kappa_h (c^2 - w^2)"),
            halo.PeakMps * halo.PeakMps, w * w + SOL::NPC_LANE_HALO_KAPPA * (c * c - w * w), NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, TEXT("halo kappa: peak 36 km/s"), halo.PeakMps, 36.0e3, 1.0e-3);
    }

    // Identities and signs for every combination
    for (const double kappa : { 0.0, 1.0e-6, 0.01, 0.3, 1.0 })
    {
        for (const double effort : { 0.5, 0.75, 1.0 })
        {
            for (const double l0 : { 1.0e3, 1.0e6, NPCTRAFFIC_SHORT_LEG_M, NPCTRAFFIC_LONG_LEG_M, 7.5e12 })
            {
                const FString label = FString::Printf(TEXT("kappa %.0e effort %.2f leg %.1e m"), kappa, effort, l0);
                const FSOLLaneLeg leg = MakeLegProfile(kappa, effort, l0);
                const double capK = FMath::Sqrt(w * w + kappa * (c * c - w * w));
                const double peak = FMath::Min(FMath::Sqrt(w * w + kappa * effort * a * l0), capK);
                TestTrue(label + TEXT(": valid"), leg.bValid);
                TestTrue(label + TEXT(": every time and distance finite and >= 0"),
                    NpcTrafficLegFiniteNonNegative(leg));
                NpcTrafficExpectNear(*this, label + TEXT(": alpha = kappa eps a"), leg.Alpha, kappa * effort * a,
                    NPCTRAFFIC_TOL, 1.0e-18);
                NpcTrafficExpectNear(*this, label + TEXT(": peak = min(sqrt(w^2 + alpha l0), c_k)"), leg.PeakMps,
                    peak, NPCTRAFFIC_TOL);
                NpcTrafficExpectNear(*this, label + TEXT(": dv = peak - w"), leg.DvMps, leg.PeakMps - w, 1.0e-6,
                    1.0e-9);
                NpcTrafficExpectNear(*this, label + TEXT(": LegS = 2 t_a + t_c"), leg.LegS,
                    2.0 * leg.AccelS + leg.CoastS, NPCTRAFFIC_TOL);
                NpcTrafficExpectNear(*this, label + TEXT(": 2 d_a + peak t_c = l0"),
                    2.0 * leg.AccelM + leg.PeakMps * leg.CoastS, l0, NPCTRAFFIC_TOL);
                NpcTrafficExpectNear(*this, label + TEXT(": d_a = w t_a + dv t_a / 2"), leg.AccelM,
                    w * leg.AccelS + 0.5 * leg.DvMps * leg.AccelS, NPCTRAFFIC_TOL, 1.0e-9);
                TestTrue(label + TEXT(": peak never above c"), leg.PeakMps <= c * (1.0 + 1.0e-12));
            }
        }
    }

    // A bad leg length is invalid
    for (const double badLeg : { 0.0, -1.0e9, NPCTRAFFIC_NAN, NPCTRAFFIC_INF, -NPCTRAFFIC_INF })
    {
        const FString label = FString::Printf(TEXT("leg %f"), badLeg);
        const FSOLLaneLeg leg = MakeLegProfile(1.0, 1.0, badLeg);
        TestFalse(label + TEXT(": invalid"), leg.bValid);
        TestTrue(label + TEXT(": fields finite and >= 0"), NpcTrafficLegFiniteNonNegative(leg));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathLegArcTest, "SOLTest.NpcTrafficMath.LegArc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 4: the leg arc u(t) runs from 0 to l0, strictly increasing and symmetric; the speed is w at both ends, the peak
// at mid-leg or over the coast, v(u)^2 = min(w^2 + 2 alpha min(u, l0 - u), peak^2); the acceleration never exceeds
// alpha; t is clamped to [0, LegS]
bool FSOLNpcTrafficMathLegArcTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    struct FLegCase
    {
        double Kappa;
        double Effort;
        double LegM;
    };
    const FLegCase cases[] = {
        { 1.0, 1.0, NPCTRAFFIC_SHORT_LEG_M },
        { 1.0, 1.0, NPCTRAFFIC_LONG_LEG_M },
        { 0.3, 0.6, NPCTRAFFIC_LONG_LEG_M },
        { 0.01, 0.75, NPCTRAFFIC_LONG_LEG_M },
        { 0.0, 0.8, NPCTRAFFIC_SHORT_LEG_M },
        { 0.5, 1.0, 1.0e6 },
    };
    for (const FLegCase& legCase : cases)
    {
        const double l0 = legCase.LegM;
        const FSOLLaneLeg leg = MakeLegProfile(legCase.Kappa, legCase.Effort, l0);
        const FString label = FString::Printf(TEXT("kappa %.2f effort %.2f leg %.3e m"), legCase.Kappa,
            legCase.Effort, l0);
        TestTrue(label + TEXT(": valid"), leg.bValid);

        // The ends and the peak
        NpcTrafficExpectNear(*this, label + TEXT(": u(0) = 0"), LegArcM(leg, l0, 0.0), 0.0, 0.0, 1.0e-6);
        NpcTrafficExpectNear(*this, label + TEXT(": u(LegS) = l0"), LegArcM(leg, l0, leg.LegS), l0, NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, label + TEXT(": speed w at the start"), LegSpeedMps(leg, l0, 0.0), w,
            NPCTRAFFIC_TOL);
        NpcTrafficExpectNear(*this, label + TEXT(": speed w at the end"), LegSpeedMps(leg, l0, leg.LegS), w,
            NPCTRAFFIC_TOL);
        const double peakTime = leg.CoastS > 0.0 ? leg.AccelS + 0.5 * leg.CoastS : 0.5 * leg.LegS;
        NpcTrafficExpectNear(*this, label + TEXT(": the peak at mid-leg or over the coast"),
            LegSpeedMps(leg, l0, peakTime), leg.PeakMps, NPCTRAFFIC_TOL);

        // 50 sampled times: increasing, symmetric, the speed law, speed by time = speed by arc
        double previousArc = 0.0;
        FNpcTrafficFailures failures;
        for (int32 sample = 1; sample < 50; ++sample)
        {
            const double t = leg.LegS * (static_cast<double>(sample) + 0.37 * NpcTrafficUnit(sample)) / 50.0;
            const double u = LegArcM(leg, l0, t);
            const double v = LegSpeedMps(leg, l0, t);
            const double expectedSquared = FMath::Min(w * w + 2.0 * leg.Alpha * FMath::Min(u, l0 - u),
                leg.PeakMps * leg.PeakMps);
            if (!(u > previousArc))
            {
                failures.Add(FString::Printf(TEXT("not increasing at t %.6e (u %.9e after %.9e)"), t, u,
                    previousArc));
            }
            if (!NpcTrafficNear(LegArcM(leg, l0, leg.LegS - t), l0 - u, NPCTRAFFIC_TOL, 1.0e-9 * l0))
            {
                failures.Add(FString::Printf(TEXT("not symmetric at t %.6e"), t));
            }
            if (!NpcTrafficNear(v * v, expectedSquared, NPCTRAFFIC_TOL))
            {
                failures.Add(FString::Printf(TEXT("v^2 %.12e at t %.6e, expected %.12e"), v * v, t,
                    expectedSquared));
            }
            if (!NpcTrafficNear(v, LegSpeedAtArcMps(leg, l0, u), NPCTRAFFIC_TOL))
            {
                failures.Add(FString::Printf(TEXT("speed by time %.9e != speed by arc %.9e at t %.6e"), v,
                    LegSpeedAtArcMps(leg, l0, u), t));
            }
            previousArc = u;
        }
        NpcTrafficReport(*this, label + TEXT(": sampled leg"), failures);

        // The acceleration (second difference) never exceeds alpha
        FNpcTrafficFailures accelFailures;
        const double h = leg.LegS / 2000.0;
        for (int32 sample = 0; sample <= 200; ++sample)
        {
            const double t = h + (leg.LegS - 2.0 * h) * static_cast<double>(sample) / 200.0;
            const double second = (LegArcM(leg, l0, t + h) - 2.0 * LegArcM(leg, l0, t) + LegArcM(leg, l0, t - h))
                / (h * h);
            if (FMath::Abs(second) > leg.Alpha * (1.0 + 1.0e-6) + 1.0e-6)
            {
                accelFailures.Add(FString::Printf(TEXT("|u''| %.9e above alpha %.9e at t %.6e"), FMath::Abs(second),
                    leg.Alpha, t));
            }
        }
        NpcTrafficReport(*this, label + TEXT(": acceleration bounded"), accelFailures);

        // t clamped to [0, LegS]
        NpcTrafficExpectNear(*this, label + TEXT(": t < 0 clamps to 0"), LegArcM(leg, l0, -10.0), 0.0, 0.0, 1.0e-6);
        NpcTrafficExpectNear(*this, label + TEXT(": t > LegS clamps to l0"), LegArcM(leg, l0, leg.LegS + 10.0), l0,
            NPCTRAFFIC_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathMeanSpeedTest, "SOLTest.NpcTrafficMath.MeanSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 5: the mean speed field is the mean-effort ship's speed at that radius: w at the wall and at both end rings,
// strictly decreasing in rho inside the wall mid-leg, symmetric in u, never above c, the halo between w and the speed
// at sqrt(1 - kappa_h)
bool FSOLNpcTrafficMathMeanSpeedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    const double c = NpcTrafficCap();
    for (const double l0 : { NPCTRAFFIC_SHORT_LEG_M, NPCTRAFFIC_LONG_LEG_M })
    {
        const FString lane = FString::Printf(TEXT("leg %.3e m"), l0);

        // Against the leg profile of a mean-effort ship, symmetric, capped
        FNpcTrafficFailures failures;
        for (const double fraction : { 0.0, 0.001, 0.1, 0.37, 0.5, 0.8, 1.0 })
        {
            const double u = fraction * l0;
            for (const double rho : { 0.0, 0.3, 0.7, 0.99, 1.0, 1.1, 1.5, 2.0 })
            {
                const double mean = MeanSpeedMps(u, rho, l0);
                const FSOLLaneLeg leg = MakeLegProfile(RadialFactor(rho), SOL::NPC_LANE_EFFORT_MEAN, l0);
                const double expected = LegSpeedAtArcMps(leg, l0, u);
                if (!NpcTrafficNear(mean, expected, NPCTRAFFIC_TOL))
                {
                    failures.Add(FString::Printf(TEXT("u %.3f l0 rho %.2f: %.9e, expected %.9e"), fraction, rho,
                        mean, expected));
                }
                if (!NpcTrafficNear(MeanSpeedMps(l0 - u, rho, l0), mean, NPCTRAFFIC_TOL))
                {
                    failures.Add(FString::Printf(TEXT("u %.3f l0 rho %.2f: not symmetric"), fraction, rho));
                }
                if (mean > c * (1.0 + 1.0e-12))
                {
                    failures.Add(FString::Printf(TEXT("u %.3f l0 rho %.2f: %.9e above c"), fraction, rho, mean));
                }
            }
        }
        NpcTrafficReport(*this, lane + TEXT(": mean field against the mean-effort leg"), failures);

        // w at the wall for every u, and at both end rings for every rho
        for (const double fraction : { 0.0, 0.01, 0.25, 0.5, 0.9, 1.0 })
        {
            NpcTrafficExpectNear(*this, lane + FString::Printf(TEXT(": w at the wall, u %.2f l0"), fraction),
                MeanSpeedMps(fraction * l0, 1.0, l0), w, NPCTRAFFIC_TOL);
        }
        for (const double rho : { 0.0, 0.5, 1.0, 1.5, 2.0 })
        {
            NpcTrafficExpectNear(*this, lane + FString::Printf(TEXT(": w at u = 0, rho %.1f"), rho),
                MeanSpeedMps(0.0, rho, l0), w, NPCTRAFFIC_TOL);
            NpcTrafficExpectNear(*this, lane + FString::Printf(TEXT(": w at u = l0, rho %.1f"), rho),
                MeanSpeedMps(l0, rho, l0), w, NPCTRAFFIC_TOL);
        }

        // Strictly decreasing in rho inside the wall mid-leg
        double previous = MeanSpeedMps(0.5 * l0, 0.0, l0);
        for (int32 step = 1; step <= 20; ++step)
        {
            const double rho = static_cast<double>(step) / 20.0;
            const double mean = MeanSpeedMps(0.5 * l0, rho, l0);
            TestTrue(lane + FString::Printf(TEXT(": strictly decreasing at rho %.2f"), rho), mean < previous);
            previous = mean;
        }

        // Halo values in [w, the speed at sqrt(1 - kappa_h)]
        const double haloEquivalentRho = FMath::Sqrt(1.0 - SOL::NPC_LANE_HALO_KAPPA);
        for (const double fraction : { 0.1, 0.5 })
        {
            const double u = fraction * l0;
            const double ceiling = MeanSpeedMps(u, haloEquivalentRho, l0);
            for (const double rho : { 1.0, 1.05, 1.1, 1.25, 1.6, 2.0 })
            {
                const double mean = MeanSpeedMps(u, rho, l0);
                TestTrue(lane + FString::Printf(TEXT(": halo rho %.2f u %.1f l0 in [w, ceiling]"), rho, fraction),
                    mean >= w * (1.0 - 1.0e-12) && mean <= ceiling * (1.0 + 1.0e-12));
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathMetricMapTest, "SOLTest.NpcTrafficMath.MetricMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 6: the metric end-zone map X(u) = u + (l - l0) J(u) / J(l0): X(0) = 0, X(l0) = l, the identity at l = l0, unit
// slope at both ends, nearly the identity deep in the zone, strictly monotone for l > Z, J continuous at the zone
// joints, the slope function against finite differences and the inverse round trip
bool FSOLNpcTrafficMathMetricMapTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double l0 = NPCTRAFFIC_LONG_LEG_M;
    const double z = MetricZoneM(l0);
    NpcTrafficExpectNear(*this, TEXT("zone = fraction x l0"), z, SOL::NPC_LANE_METRIC_ZONE_FRACTION * l0,
        NPCTRAFFIC_TOL);
    TestTrue(TEXT("zone at most l0 / 2"), z <= 0.5 * l0);

    for (const double ratio : { 0.06, 0.15, 0.5, 1.0, 2.0, 7.0 })
    {
        const double l = ratio * l0;
        const FString label = FString::Printf(TEXT("l / l0 %.2f"), ratio);

        // The ends and the closed form
        NpcTrafficExpectNear(*this, label + TEXT(": X(0) = 0"), MetricMapM(0.0, l0, l), 0.0, 0.0, 1.0e-6);
        NpcTrafficExpectNear(*this, label + TEXT(": X(l0) = l"), MetricMapM(l0, l0, l), l, NPCTRAFFIC_TOL);
        for (const double u : { 0.5 * z, 0.999 * z, z, 1.001 * z, 0.5 * l0, l0 - z, l0 - 0.3 * z })
        {
            NpcTrafficExpectNear(*this, label + FString::Printf(TEXT(": closed form at u %.6e"), u),
                MetricMapM(u, l0, l), NpcTrafficRefX(u, l0, l), NPCTRAFFIC_TOL, 1.0e-6);
        }
        if (ratio == 1.0)
        {
            for (const double fraction : { 0.01, 0.2, 0.5, 0.77, 0.99 })
            {
                NpcTrafficExpectNear(*this, label + FString::Printf(TEXT(": identity at %.2f l0"), fraction),
                    MetricMapM(fraction * l0, l0, l), fraction * l0, 1.0e-12);
            }
        }

        // Unit slope at both ends (finite difference and the slope function)
        if (ratio != 0.06 && ratio != 1.0)
        {
            const double h = 1.0e-4 * z;
            const double startSlope = (MetricMapM(h, l0, l) - MetricMapM(0.0, l0, l)) / h;
            const double endSlope = (MetricMapM(l0, l0, l) - MetricMapM(l0 - h, l0, l)) / h;
            TestTrue(label + FString::Printf(TEXT(": slope 1 at the start (%.9f)"), startSlope),
                FMath::Abs(startSlope - 1.0) <= 1.0e-6);
            TestTrue(label + FString::Printf(TEXT(": slope 1 at the end (%.9f)"), endSlope),
                FMath::Abs(endSlope - 1.0) <= 1.0e-6);
        }
        NpcTrafficExpectNear(*this, label + TEXT(": slope function 1 at u = 0"), MetricMapSlope(0.0, l0, l), 1.0,
            1.0e-12);
        NpcTrafficExpectNear(*this, label + TEXT(": slope function 1 at u = l0"), MetricMapSlope(l0, l0, l), 1.0,
            1.0e-12);

        // Deep in the zone the map is nearly the identity
        const double deep = 0.01 * z;
        TestTrue(label + TEXT(": X(u) - u below 1e-3 Z at u = 0.01 Z"),
            FMath::Abs(MetricMapM(deep, l0, l) - deep) <= 1.0e-3 * z);

        // Strictly monotone
        if (ratio == 0.06 || ratio == 0.15 || ratio == 1.0 || ratio == 7.0)
        {
            double previous = MetricMapM(0.0, l0, l);
            int32 failures = 0;
            for (int32 sample = 1; sample <= 2000; ++sample)
            {
                const double x = MetricMapM(l0 * static_cast<double>(sample) / 2000.0, l0, l);
                failures += x > previous ? 0 : 1;
                previous = x;
            }
            TestEqual(label + TEXT(": strictly monotone (failures)"), failures, 0);
        }

        // J continuous (value and slope) at Z and l0 - Z
        const double slopeMax = FMath::Max(1.0, (l - z) / (l0 - z));
        for (const double joint : { z, l0 - z })
        {
            const double epsilon = 1.0e-6 * z;
            const double gap = FMath::Abs(MetricMapM(joint + epsilon, l0, l) - MetricMapM(joint - epsilon, l0, l));
            TestTrue(label + FString::Printf(TEXT(": continuous at %.6e"), joint),
                gap <= 2.0 * epsilon * slopeMax * (1.0 + 1.0e-6) + 1.0e-3);
            TestTrue(label + FString::Printf(TEXT(": slope continuous at %.6e"), joint),
                FMath::Abs(MetricMapSlope(joint + epsilon, l0, l) - MetricMapSlope(joint - epsilon, l0, l)) <= 1.0e-6);
        }

        // The slope function against a central difference
        for (const double fraction : { 0.013, 0.03, 0.2, 0.5, 0.97 })
        {
            const double u = fraction * l0;
            const double h = 1.0e-5 * z;
            const double central = (MetricMapM(u + h, l0, l) - MetricMapM(u - h, l0, l)) / (2.0 * h);
            NpcTrafficExpectNear(*this, label + FString::Printf(TEXT(": slope at %.3f l0"), fraction),
                MetricMapSlope(u, l0, l), central, 1.0e-6);
        }

        // Inverse round trip both ways
        for (const double fraction : { 0.0, 0.001, 0.02, 0.05, 0.3, 0.5, 0.95, 0.999, 1.0 })
        {
            const double u = fraction * l0;
            NpcTrafficExpectNear(*this, label + FString::Printf(TEXT(": inverse(X(u)) at %.3f l0"), fraction),
                MetricMapInverseM(MetricMapM(u, l0, l), l0, l), u, 0.0, 1.0e-9 * l0);
            const double x = fraction * l;
            NpcTrafficExpectNear(*this, label + FString::Printf(TEXT(": X(inverse(x)) at %.3f l"), fraction),
                MetricMapM(MetricMapInverseM(x, l0, l), l0, l), x, 0.0, 1.0e-9 * l);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathCycleTest, "SOLTest.NpcTrafficMath.Cycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 7: T_U = pi D_U / w, T_cyc = 2 LegS + 2 T_U; the four segments and their boundaries; tau periodic in t (and
// for negative t), the phase always in [0, 1)
bool FSOLNpcTrafficMathCycleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    const double legS = 7.7e5;
    const double depthM = 1.6e7;
    const FSOLLaneCycle cycle = MakeCycle(legS, depthM);
    const double turnS = NPCTRAFFIC_PI * depthM / w;
    NpcTrafficExpectNear(*this, TEXT("leg time stored"), cycle.LegS, legS, NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("T_U = pi D_U / w"), cycle.TurnS, turnS, NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("T_cyc = 2 LegS + 2 T_U"), cycle.CycleS, 2.0 * legS + 2.0 * turnS,
        NPCTRAFFIC_TOL);

    // The segment boundaries (phase 0, one second either side)
    struct FBoundary
    {
        double Tau;
        ESOLLaneSegment Before;
        ESOLLaneSegment After;
        double BeforeSegmentS;
    };
    const FBoundary boundaries[] = {
        { legS, ESOLLaneSegment::Outbound, ESOLLaneSegment::TurnAtB, legS },
        { legS + turnS, ESOLLaneSegment::TurnAtB, ESOLLaneSegment::Inbound, turnS },
        { 2.0 * legS + turnS, ESOLLaneSegment::Inbound, ESOLLaneSegment::TurnAtA, legS },
        { cycle.CycleS, ESOLLaneSegment::TurnAtA, ESOLLaneSegment::Outbound, turnS },
    };
    for (const FBoundary& boundary : boundaries)
    {
        const FString label = FString::Printf(TEXT("boundary at %.3f s"), boundary.Tau);
        const FSOLLaneCyclePoint before = CycleSegmentAt(cycle, 0.0, boundary.Tau - 1.0);
        const FSOLLaneCyclePoint after = CycleSegmentAt(cycle, 0.0, boundary.Tau + 1.0);
        TestTrue(label + TEXT(": segment before"), before.Segment == boundary.Before);
        TestTrue(label + TEXT(": segment after"), after.Segment == boundary.After);
        NpcTrafficExpectNear(*this, label + TEXT(": time into the segment before"), before.SegmentTimeS,
            boundary.BeforeSegmentS - 1.0, 0.0, 1.0e-6);
        NpcTrafficExpectNear(*this, label + TEXT(": time into the segment after"), after.SegmentTimeS, 1.0, 0.0,
            1.0e-6);
    }
    TestTrue(TEXT("tau 0: outbound"), CycleSegmentAt(cycle, 0.0, 0.0).Segment == ESOLLaneSegment::Outbound);

    // tau periodic in t, consistent with the phase
    for (const double phase : { 0.0, 0.25, 0.73 })
    {
        for (const double fraction : { 0.1, 0.43, 0.9 })
        {
            const double t = fraction * cycle.CycleS + 1234.5;
            const FSOLLaneCyclePoint reference = CycleSegmentAt(cycle, phase, t);
            NpcTrafficExpectNear(*this, FString::Printf(TEXT("phase %.2f t %.3e: tau = phase x T_cyc"), phase, t),
                reference.CycleTimeS, reference.Phase * cycle.CycleS, NPCTRAFFIC_TOL, 1.0e-9 * cycle.CycleS);
            for (int32 k = -3; k <= 3; ++k)
            {
                const FSOLLaneCyclePoint shifted = CycleSegmentAt(cycle, phase,
                    t + static_cast<double>(k) * cycle.CycleS);
                const FString label = FString::Printf(TEXT("phase %.2f t %.3e k %d"), phase, t, k);
                NpcTrafficExpectNear(*this, label + TEXT(": same tau"), shifted.CycleTimeS, reference.CycleTimeS, 0.0,
                    1.0e-9 * cycle.CycleS);
                TestTrue(label + TEXT(": same segment"), shifted.Segment == reference.Segment);
            }
        }
    }

    // Negative t, and the phase input wrapped
    const FSOLLaneCyclePoint negative = CycleSegmentAt(cycle, 0.5, -0.3 * cycle.CycleS);
    NpcTrafficExpectNear(*this, TEXT("negative t: phase 0.2"), negative.Phase, 0.2, 0.0, 1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("negative t: tau 0.2 T_cyc"), negative.CycleTimeS, 0.2 * cycle.CycleS, 0.0,
        1.0e-9 * cycle.CycleS);
    NpcTrafficExpectNear(*this, TEXT("phase 1.3 reads as 0.3"), CycleSegmentAt(cycle, 1.3, 777.0).Phase,
        CycleSegmentAt(cycle, 0.3, 777.0).Phase, 0.0, 1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("phase -0.3 reads as 0.7"), CycleSegmentAt(cycle, -0.3, 777.0).Phase,
        CycleSegmentAt(cycle, 0.7, 777.0).Phase, 0.0, 1.0e-12);

    // The phase always in [0, 1) and tau in [0, T_cyc)
    for (const double phase : { 0.0, 0.999999, 1.0, 1.7, -0.3 })
    {
        for (const double t : { -1.0e12, -1.0, 0.0, 1.0, 0.5 * cycle.CycleS, cycle.CycleS, 1.0e12 })
        {
            const FSOLLaneCyclePoint point = CycleSegmentAt(cycle, phase, t);
            TestTrue(FString::Printf(TEXT("phase %.6f t %.3e: phase in [0, 1)"), phase, t),
                point.Phase >= 0.0 && point.Phase < 1.0);
            TestTrue(FString::Printf(TEXT("phase %.6f t %.3e: tau in [0, T_cyc)"), phase, t),
                point.CycleTimeS >= 0.0 && point.CycleTimeS < cycle.CycleS);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathLaneProfileTest, "SOLTest.NpcTrafficMath.LaneProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 11: the frozen lane profile: wall radius = the #49 tunnel radius of the in-wall count (D13), halo, end arcs,
// nominal leg, U-turn depth and time, metric zone (section 4.2); every default lane drivable at its nominal (maximum,
// D16) length; a lane no longer than its end rings, or with bad input, not drivable
bool FSOLNpcTrafficMathLaneProfileTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    const double margin = SOL::NPC_TUNNEL_END_MARGIN_RADII;

    // Earth - Mars against section 4.2
    const FSOLLaneProfile profile = NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
        NPCTRAFFIC_MARS_RADIUS_M);
    const double rw = SOLNpcTunnel::TunnelRadiusM(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M, NPCTRAFFIC_EARTH_MARS_IN_WALL);
    const double l0 = NPCTRAFFIC_EARTH_MARS_MAX_M - NPCTRAFFIC_EARTH_RADIUS_M - NPCTRAFFIC_MARS_RADIUS_M
        - 2.0 * margin * rw;
    NpcTrafficExpectNear(*this, TEXT("wall radius = the tunnel radius of the in-wall count"), profile.WallRadiusM, rw,
        NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("halo outer radius H Rw"), profile.HaloOuterRadiusM,
        SOL::NPC_LANE_HALO_OUTER_WALL_RADII * rw, NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("nominal length L0"), profile.NominalLengthM, NPCTRAFFIC_EARTH_MARS_MAX_M,
        NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("end arc A = R_A + m Rw"), profile.EndArcAM,
        NPCTRAFFIC_EARTH_RADIUS_M + margin * rw, NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("end arc B = R_B + m Rw"), profile.EndArcBM,
        NPCTRAFFIC_MARS_RADIUS_M + margin * rw, NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("nominal leg L0 - R_A - R_B - 2 m Rw"), profile.NominalLegM, l0,
        NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("U-turn depth 2 Rw"), profile.UTurnDepthM,
        SOL::NPC_LANE_UTURN_DEPTH_WALL_RADII * rw, NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("U-turn time pi D_U / w"), profile.UTurnS,
        NPCTRAFFIC_PI * SOL::NPC_LANE_UTURN_DEPTH_WALL_RADII * rw / w, NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("metric zone = MetricZoneM(l0)"), profile.MetricZoneM, MetricZoneM(l0),
        NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("metric zone = fraction x l0"), profile.MetricZoneM,
        SOL::NPC_LANE_METRIC_ZONE_FRACTION * l0, NPCTRAFFIC_TOL);
    TestTrue(TEXT("Earth - Mars drivable"), profile.bDrivable);

    // Every default lane drivable at its nominal length, for a quiet lane and the widest (busiest) wall
    struct FDefaultLane
    {
        const TCHAR* BodyA;
        const TCHAR* BodyB;
        double RadiusAM;
        double RadiusBM;
        double MaxLengthM;
    };
    const FDefaultLane table[] = {
        { TEXT("Mercury"), TEXT("Venus"), NPCTRAFFIC_MERCURY_RADIUS_M, NPCTRAFFIC_VENUS_RADIUS_M,
            NPCTRAFFIC_MERCURY_APHELION_M + NPCTRAFFIC_VENUS_APHELION_M },
        { TEXT("Venus"), TEXT("Earth"), NPCTRAFFIC_VENUS_RADIUS_M, NPCTRAFFIC_EARTH_RADIUS_M,
            NPCTRAFFIC_VENUS_APHELION_M + NPCTRAFFIC_EARTH_APHELION_M },
        { TEXT("Earth"), TEXT("Moon"), NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MOON_RADIUS_M,
            NPCTRAFFIC_MOON_APOAPSIS_M },
        { TEXT("Earth"), TEXT("Mars"), NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M,
            NPCTRAFFIC_EARTH_MARS_MAX_M },
        { TEXT("Mars"), TEXT("Jupiter"), NPCTRAFFIC_MARS_RADIUS_M, NPCTRAFFIC_JUPITER_RADIUS_M,
            NPCTRAFFIC_MARS_APHELION_M + NPCTRAFFIC_JUPITER_APHELION_M },
        { TEXT("Jupiter"), TEXT("Saturn"), NPCTRAFFIC_JUPITER_RADIUS_M, NPCTRAFFIC_SATURN_RADIUS_M,
            NPCTRAFFIC_JUPITER_APHELION_M + NPCTRAFFIC_SATURN_APHELION_M },
        { TEXT("Saturn"), TEXT("Uranus"), NPCTRAFFIC_SATURN_RADIUS_M, NPCTRAFFIC_URANUS_RADIUS_M,
            NPCTRAFFIC_SATURN_APHELION_M + NPCTRAFFIC_URANUS_APHELION_M },
        { TEXT("Uranus"), TEXT("Neptune"), NPCTRAFFIC_URANUS_RADIUS_M, NPCTRAFFIC_NEPTUNE_RADIUS_M,
            NPCTRAFFIC_URANUS_APHELION_M + NPCTRAFFIC_NEPTUNE_APHELION_M },
    };
    const SOLNpcScale::FLayout layout = SOLNpcScale::DefaultLayout();
    TestEqual(TEXT("the default layout has the 8 lanes of the table"), layout.Lanes.Num(),
        static_cast<int32>(UE_ARRAY_COUNT(table)));
    for (const FDefaultLane& row : table)
    {
        const FString label = FString::Printf(TEXT("%s - %s"), row.BodyA, row.BodyB);
        const SOLNpcScale::FLaneDef* def = layout.Lanes.FindByPredicate([&row](const SOLNpcScale::FLaneDef& lane)
        {
            return lane.BodyA == FName(row.BodyA) && lane.BodyB == FName(row.BodyB);
        });
        if (def == nullptr)
        {
            AddError(label + TEXT(": not in the default layout"));
            continue;
        }
        for (const int32 inWall : { 100, 1000000 })
        {
            const FSOLLaneProfile lane = NpcTrafficProfile(def->RadiusM, inWall, row.MaxLengthM, row.RadiusAM,
                row.RadiusBM);
            TestTrue(label + FString::Printf(TEXT(": drivable with %d ships in the wall"), inWall), lane.bDrivable);
            TestTrue(label + FString::Printf(TEXT(": nominal leg above twice the zone (%d ships)"), inWall),
                lane.NominalLegM > 2.0 * lane.MetricZoneM);
        }
    }

    // A lane no longer than its end rings, or with bad input, is not drivable
    const double ringsM = NPCTRAFFIC_EARTH_RADIUS_M + NPCTRAFFIC_MARS_RADIUS_M + 2.0 * margin * rw;
    TestFalse(TEXT("length = the end rings: not drivable"), NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, ringsM, NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M).bDrivable);
    TestFalse(TEXT("length below the end rings: not drivable"), NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, 0.5 * ringsM, NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M).bDrivable);
    TestFalse(TEXT("NaN length: not drivable"), NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_NAN, NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M).bDrivable);
    TestFalse(TEXT("NaN lane radius: not drivable"), NpcTrafficProfile(NPCTRAFFIC_NAN, NPCTRAFFIC_EARTH_MARS_IN_WALL,
        NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M).bDrivable);
    TestFalse(TEXT("zero lane radius: not drivable"), NpcTrafficProfile(0.0, NPCTRAFFIC_EARTH_MARS_IN_WALL,
        NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M).bDrivable);

    // The wall radius is the caller's (Traffic does not compute the #49 radius): passed through unchanged
    const FSOLLaneProfile direct = MakeLaneProfile(rw, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
        NPCTRAFFIC_MARS_RADIUS_M);
    TestTrue(TEXT("wall radius passed through"), direct.WallRadiusM == rw && direct.NominalLegM == profile.NominalLegM);
    for (const double badWall : { 0.0, -1.0e6, NPCTRAFFIC_NAN, NPCTRAFFIC_INF })
    {
        TestFalse(FString::Printf(TEXT("wall radius %f: not drivable"), badWall), MakeLaneProfile(badWall,
            NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M).bDrivable);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathLaneFrameTest, "SOLTest.NpcTrafficMath.LaneFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The lane basis moved to Traffic/ (section 4.10) against independent values: away from the pole up is north
// projected off the axis, within NPC_TUNNEL_UP_FALLBACK_DEG of it +X projected, and in the blend band frozen values
// (computed offline from the SDD 49 section 4.3 rule); the lane frame carries A, B, that basis and the current leg
// |B - A| - e_A - e_B (floored at 1.001 Z, the excess moving B's end out); A == B is invalid
bool FSOLNpcTrafficMathLaneFrameTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const FVector3d origin(5.0e10, -3.0e9, 1.0e7);

    // Returns the unit direction of an elevation and azimuth in degrees
    const auto directionOf = [](const double elevationDeg, const double azimuthDeg)
    {
        const double elevationRad = FMath::DegreesToRadians(elevationDeg);
        const double azimuthRad = FMath::DegreesToRadians(azimuthDeg);
        return FVector3d(FMath::Cos(elevationRad) * FMath::Cos(azimuthRad),
            FMath::Cos(elevationRad) * FMath::Sin(azimuthRad), FMath::Sin(elevationRad));
    };

    // Away from the pole (10 degrees or more) and at or within the fallback angle: closed-form references
    for (int32 index = 0; index < 28; ++index)
    {
        const bool bPolar = index >= 24;
        const double elevation = bPolar ? 89.25 + 0.25 * static_cast<double>(index - 24)
            : -80.0 + 160.0 * static_cast<double>(index) / 23.0;
        const double azimuth = 17.0 + 47.0 * static_cast<double>(index);
        const FVector3d axis = directionOf(elevation, azimuth);
        const FSOLLaneBasis lane = LaneBasis(origin, origin + axis * 3.0e11);
        const FVector3d reference = bPolar ? FVector3d::UnitX() : FVector3d::UnitZ();
        const FVector3d expectedUp = (reference - axis * FVector3d::DotProduct(reference, axis)).GetSafeNormal();
        const FVector3d expectedRight = FVector3d::CrossProduct(axis, expectedUp);
        const FString label = FString::Printf(TEXT("elevation %.2f azimuth %.1f"), elevation, azimuth);
        TestTrue(label + TEXT(": valid"), lane.bValid);
        TestTrue(label + TEXT(": axis"), (lane.Axis - axis).GetAbsMax() <= 1.0e-12);
        TestTrue(label + (bPolar ? TEXT(": up = +X projected") : TEXT(": up = north projected")),
            (lane.Up - expectedUp).GetAbsMax() <= 1.0e-12);
        TestTrue(label + TEXT(": right = axis x up"), (lane.Right - expectedRight).GetAbsMax() <= 1.0e-12);
    }

    // Inside the blend band (1.5 degrees from the pole, weight 0.5): frozen values
    struct FFrozenBasis
    {
        double ElevationDeg;
        double AzimuthDeg;
        FVector3d Up;
        FVector3d Right;
    };
    const FFrozenBasis frozen[] = {
        { 88.5, 1145.0, FVector3d(5.373220754666368e-01, -8.432598212029359e-01, 1.406631298593559e-02),
            FVector3d(8.433045715663384e-01, 5.369823350107708e-01, -2.207649115449912e-02) },
        { 88.5, 17.0, FVector3d(1.478104698815647e-01, -9.890081317603291e-01, 3.870439940739529e-03),
            FVector3d(9.886988453957809e-01, 1.476629296596545e-01, -2.588923168767634e-02) },
    };
    for (const FFrozenBasis& row : frozen)
    {
        const FSOLLaneBasis lane = LaneBasis(origin, origin + directionOf(row.ElevationDeg, row.AzimuthDeg) * 3.0e11);
        const FString label = FString::Printf(TEXT("blend band, azimuth %.1f"), row.AzimuthDeg);
        TestTrue(label + TEXT(": valid"), lane.bValid);
        TestTrue(label + TEXT(": up frozen"), (lane.Up - row.Up).GetAbsMax() <= 1.0e-9);
        TestTrue(label + TEXT(": right frozen"), (lane.Right - row.Right).GetAbsMax() <= 1.0e-9);
    }
    TestFalse(TEXT("A == B: invalid basis"), LaneBasis(origin, origin).bValid);

    // The frame
    const FSOLLaneProfile profile = NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
        NPCTRAFFIC_MARS_RADIUS_M);
    const FVector3d bM = origin + FVector3d(-1.1e11, 2.3e11, 4.0e9);
    const FSOLLaneFrame frame = MakeLaneFrame(profile, origin, bM);
    const FSOLLaneBasis basis = LaneBasis(origin, bM);
    TestTrue(TEXT("frame valid"), frame.bValid);
    TestTrue(TEXT("frame A"), frame.AM == origin);
    TestTrue(TEXT("frame B"), frame.BM == bM);
    TestTrue(TEXT("frame axis"), (frame.Axis - basis.Axis).GetAbsMax() <= 1.0e-15);
    TestTrue(TEXT("frame up"), (frame.Up - basis.Up).GetAbsMax() <= 1.0e-15);
    TestTrue(TEXT("frame right"), (frame.Right - basis.Right).GetAbsMax() <= 1.0e-15);
    NpcTrafficExpectNear(*this, TEXT("current leg |B - A| - e_A - e_B"), frame.CurrentLegM,
        (bM - origin).Size() - profile.EndArcAM - profile.EndArcBM, 1.0e-12);
    TestFalse(TEXT("A == B: invalid frame"), MakeLaneFrame(profile, origin, origin).bValid);

    TestEqual(TEXT("a real lane: no end shift"), frame.EndShiftM, 0.0);

    // Ends closer than the two end arcs: the current leg is floored at 1.001 Z (never negative, the map monotone)
    // and B's end moves out by the difference
    const double crowdedLengthM = 0.5 * (profile.EndArcAM + profile.EndArcBM);
    const FSOLLaneFrame crowded = MakeLaneFrame(profile, origin, origin + FVector3d(0.6, 0.8, 0.0) * crowdedLengthM);
    TestTrue(TEXT("crowded ends: frame valid"), crowded.bValid);
    NpcTrafficExpectNear(*this, TEXT("crowded ends: current leg floored at 1.001 Z"), crowded.CurrentLegM,
        1.001 * profile.MetricZoneM, 1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("crowded ends: B's end shifted by the difference"), crowded.EndShiftM,
        crowded.CurrentLegM - (crowdedLengthM - profile.EndArcAM - profile.EndArcBM), 1.0e-12);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathKeepRightTest, "SOLTest.NpcTrafficMath.KeepRight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8, the legs (D1, D2): the ship packing; at 200 sampled times per ship and 20 ships per lane, outbound legs on
// the right of the axis and inbound on the left, the lateral radius rho Rw with the up offset kept, the along position
// e_A + X(u) (outbound) or e_A + l - X(u) (inbound), the speed LegSpeedMps x X', and each direction on the right of its
// own travel as seen from behind A and from beyond B
bool FSOLNpcTrafficMathKeepRightTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const TArray<FNpcTrafficLane> lanes = NpcTrafficTestLanes();
    for (int32 laneIndex = 0; laneIndex < lanes.Num(); ++laneIndex)
    {
        const FNpcTrafficLane& lane = lanes[laneIndex];
        const FSOLLaneProfile& profile = lane.Profile;
        const FSOLLaneFrame& frame = lane.Frame;
        const double l0 = profile.NominalLegM;
        const double l = frame.CurrentLegM;
        const double rw = profile.WallRadiusM;
        const double laneLengthM = (frame.BM - frame.AM).Size();
        const FVector3d cameraBehindA = frame.AM - frame.Axis * (20.0 * rw);
        const FVector3d cameraBeyondB = frame.BM + frame.Axis * (20.0 * rw);
        TestTrue(lane.Name + TEXT(": drivable"), profile.bDrivable);
        TestTrue(lane.Name + TEXT(": frame valid"), frame.bValid);

        FNpcTrafficFailures sideFailures;
        FNpcTrafficFailures radiusFailures;
        FNpcTrafficFailures alongFailures;
        FNpcTrafficFailures speedFailures;
        FNpcTrafficFailures screenFailures;
        int32 outboundSamples = 0;
        int32 inboundSamples = 0;
        for (int32 shipIndex = 0; shipIndex < 20; ++shipIndex)
        {
            const FNpcTrafficShipSpec spec = NpcTrafficSpec(static_cast<uint64>(laneIndex), shipIndex);
            const FSOLLaneShip ship = MakeLaneShip(profile, spec.Phase, spec.Effort, spec.Rho, spec.ClockRad);
            const double shipLegS = LaneShipLegS(profile, ship);
            const FSOLLaneLeg leg = MakeLegProfile(RadialFactor(spec.Rho), spec.Effort, l0);
            const FSOLLaneCycle cycle = MakeCycle(shipLegS, profile.UTurnDepthM);
            const double upOffset = spec.Rho * rw * FMath::Cos(spec.ClockRad);
            const double rightOffset = spec.Rho * rw * FMath::Sin(spec.ClockRad);
            const FString shipLabel = lane.Name + FString::Printf(TEXT(", ship %d"), shipIndex);

            // The ship packing (the rail's P0-P4; T_leg derived from them and l0)
            if (shipIndex < 4)
            {
                NpcTrafficExpectNear(*this, shipLabel + TEXT(": phase"), ship.Phase, spec.Phase, 0.0, 1.0e-15);
                NpcTrafficExpectNear(*this, shipLabel + TEXT(": dv"), ship.DvMps, leg.DvMps, 1.0e-12, 1.0e-12);
                NpcTrafficExpectNear(*this, shipLabel + TEXT(": t_a"), ship.AccelS, leg.AccelS, 1.0e-12, 1.0e-12);
                NpcTrafficExpectNear(*this, shipLabel + TEXT(": T_leg"), shipLegS, leg.LegS, 1.0e-12);
                NpcTrafficExpectNear(*this, shipLabel + TEXT(": up offset rho Rw cos psi"), ship.UpOffsetM, upOffset,
                    1.0e-12, 1.0e-9);
                NpcTrafficExpectNear(*this, shipLabel + TEXT(": right offset rho Rw sin psi"), ship.RightOffsetM,
                    rightOffset, 1.0e-12, 1.0e-9);
                NpcTrafficExpectNear(*this, shipLabel + TEXT(": cycle turn = the profile's"), cycle.TurnS,
                    profile.UTurnS, 1.0e-12);
            }

            for (int32 sample = 0; sample < 200; ++sample)
            {
                const double t = cycle.CycleS * (static_cast<double>(sample)
                    + NpcTrafficUnit(static_cast<uint64>(shipIndex) * 977ull + static_cast<uint64>(sample))) / 200.0;
                const FSOLLaneCyclePoint point = CycleSegmentAt(cycle, ship.Phase, t);
                const bool bOutbound = point.Segment == ESOLLaneSegment::Outbound;
                if (!bOutbound && point.Segment != ESOLLaneSegment::Inbound)
                {
                    continue;
                }
                if (bOutbound)
                {
                    ++outboundSamples;
                }
                else
                {
                    ++inboundSamples;
                }
                const FVector3d local = NpcTrafficLocal(frame, NpcTrafficPos(lane, ship, t, frame.AM));
                const FString where = shipLabel + FString::Printf(TEXT(" t %.6e %s"), t,
                    bOutbound ? TEXT("outbound") : TEXT("inbound"));

                // Keep right: the side follows the current direction
                if (bOutbound ? !(local.Z > 0.0) : !(local.Z < 0.0))
                {
                    sideFailures.Add(where + FString::Printf(TEXT(": right component %.6e"), local.Z));
                }

                // Lateral radius and offsets (projected from up to 4e11 m away from A, so a millimetre of slack)
                const double lateral = FMath::Sqrt(local.Y * local.Y + local.Z * local.Z);
                if (!NpcTrafficNear(lateral, spec.Rho * rw, NPCTRAFFIC_TOL, NPCTRAFFIC_POSITION_TOL_M)
                    || !NpcTrafficNear(local.Y, upOffset, NPCTRAFFIC_TOL, NPCTRAFFIC_POSITION_TOL_M)
                    || !NpcTrafficNear(FMath::Abs(local.Z), rightOffset, NPCTRAFFIC_TOL, NPCTRAFFIC_POSITION_TOL_M))
                {
                    radiusFailures.Add(where + FString::Printf(TEXT(": lateral %.9e (up %.9e right %.9e), expected "
                        "%.9e (%.9e, %.9e)"), lateral, local.Y, local.Z, spec.Rho * rw, upOffset, rightOffset));
                }

                // Along position through the metric map
                const double u = LegArcM(leg, l0, point.SegmentTimeS);
                const double mapped = MetricMapM(u, l0, l);
                const double expectedAxial = bOutbound ? profile.EndArcAM + mapped : profile.EndArcAM + l - mapped;
                if (!NpcTrafficNear(local.X, expectedAxial, 0.0, 1.0e-3 + 1.0e-12 * laneLengthM))
                {
                    alongFailures.Add(where + FString::Printf(TEXT(": axial %.9e, expected %.9e"), local.X,
                        expectedAxial));
                }

                // Speed on the leg (away from the segment ends)
                if (point.SegmentTimeS > 2.0 && point.SegmentTimeS < shipLegS - 2.0)
                {
                    const double speed = NpcTrafficVel(lane, ship, t, 0.5).Size();
                    const double expectedSpeed = LegSpeedMps(leg, l0, point.SegmentTimeS) * MetricMapSlope(u, l0, l);
                    if (!NpcTrafficNear(speed, expectedSpeed, 1.0e-3))
                    {
                        speedFailures.Add(where + FString::Printf(TEXT(": speed %.6e, expected %.6e"), speed,
                            expectedSpeed));
                    }
                }

                // Seen from behind A (outbound) or beyond B (inbound), the ship is on the right of the screen
                if (lane.bEcliptic)
                {
                    const FVector3d absolute = LaneShipPositionM(profile, frame, ship, t);
                    const double screenX = bOutbound ? NpcTrafficScreenX(cameraBehindA, frame.Axis, absolute)
                        : NpcTrafficScreenX(cameraBeyondB, -frame.Axis, absolute);
                    if (!(screenX > 0.0))
                    {
                        screenFailures.Add(where + FString::Printf(TEXT(": screen x %.6e"), screenX));
                    }
                }
            }
        }
        TestTrue(lane.Name + TEXT(": outbound samples taken"), outboundSamples > 0);
        TestTrue(lane.Name + TEXT(": inbound samples taken"), inboundSamples > 0);
        NpcTrafficReport(*this, lane.Name + TEXT(": keep right"), sideFailures);
        NpcTrafficReport(*this, lane.Name + TEXT(": lateral radius rho Rw"), radiusFailures);
        NpcTrafficReport(*this, lane.Name + TEXT(": along position"), alongFailures);
        NpcTrafficReport(*this, lane.Name + TEXT(": leg speed"), speedFailures);
        NpcTrafficReport(*this, lane.Name + TEXT(": right of travel on screen"), screenFailures);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathJointsTest, "SOLTest.NpcTrafficMath.UTurnJoints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8, the U-turns (D1): position and velocity continuous across the four joints with speed w there; the U-turn
// reaches D_U beyond its end ring at theta = pi / 2 with no right component (and slows to rho sin(psi) Rw w / D_U);
// the legs start and end exactly at the end rings for l / l0 in {0.15, 1}
bool FSOLNpcTrafficMathJointsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    const double delta = 1.0e-3;     // Joint evaluated this far either side (s)
    const double offsetH = 0.02;     // Velocity sampled this far either side (s)...
    const double stepH = 0.01;       // ...by a central difference over +-this
    const TArray<FNpcTrafficLane> lanes = NpcTrafficTestLanes();
    for (int32 laneIndex = 0; laneIndex < lanes.Num(); ++laneIndex)
    {
        const FNpcTrafficLane& lane = lanes[laneIndex];
        const FSOLLaneProfile& profile = lane.Profile;
        const FSOLLaneFrame& frame = lane.Frame;
        const double rw = profile.WallRadiusM;
        const double depth = profile.UTurnDepthM;
        FNpcTrafficFailures positionFailures;
        FNpcTrafficFailures velocityFailures;
        FNpcTrafficFailures apexFailures;
        FNpcTrafficFailures ringFailures;
        for (int32 shipIndex = 0; shipIndex < 22; ++shipIndex)
        {
            // 20 hashed ships plus an axis ship and a wall (kappa 0) ship
            FNpcTrafficShipSpec spec = NpcTrafficSpec(100ull + static_cast<uint64>(laneIndex), shipIndex);
            spec.Rho = shipIndex == 20 ? 0.0 : (shipIndex == 21 ? 1.0 : spec.Rho);
            const FSOLLaneShip ship = MakeLaneShip(profile, spec.Phase, spec.Effort, spec.Rho, spec.ClockRad);
            const double shipLegS = LaneShipLegS(profile, ship);
            const FSOLLaneLeg leg = MakeLegProfile(RadialFactor(spec.Rho), spec.Effort, profile.NominalLegM);
            const FSOLLaneCycle cycle = MakeCycle(shipLegS, depth);
            const double accelMax = FMath::Max(leg.Alpha, w * w / depth);
            const FString shipLabel = lane.Name + FString::Printf(TEXT(", ship %d"), shipIndex);

            // The four joints: continuous position, continuous velocity, speed w
            struct FJoint
            {
                double Tau;
                bool bNearB;
            };
            const FJoint joints[] = {
                { shipLegS, true },
                { shipLegS + cycle.TurnS, true },
                { 2.0 * shipLegS + cycle.TurnS, false },
                { cycle.CycleS, false },
            };
            for (const FJoint& joint : joints)
            {
                const double tj = NpcTrafficTimeAt(ship, cycle, joint.Tau);
                const FVector3d originM = joint.bNearB ? frame.BM : frame.AM;
                const FString where = shipLabel + FString::Printf(TEXT(" joint tau %.6e"), joint.Tau);
                const double gap = (NpcTrafficPos(lane, ship, tj + delta, originM)
                    - NpcTrafficPos(lane, ship, tj - delta, originM)).Size();
                if (gap > 2.0 * delta * w * (1.0 + 1.0e-3) + NPCTRAFFIC_POSITION_TOL_M)
                {
                    positionFailures.Add(where + FString::Printf(TEXT(": jump %.6e m"), gap));
                }
                const FVector3d before = NpcTrafficVel(lane, ship, tj - offsetH, stepH);
                const FVector3d after = NpcTrafficVel(lane, ship, tj + offsetH, stepH);
                const double speedSlack = 1.0e-3 * w + accelMax * (offsetH + stepH);
                if (FMath::Abs(before.Size() - w) > speedSlack || FMath::Abs(after.Size() - w) > speedSlack
                    || (after - before).Size() > 1.0e-3 * w + 2.0 * accelMax * (offsetH + stepH))
                {
                    velocityFailures.Add(where + FString::Printf(TEXT(": speed %.6e -> %.6e, change %.6e"),
                        before.Size(), after.Size(), (after - before).Size()));
                }
            }

            // The apexes: D_U beyond the end ring, no right component, the up offset kept, the reduced speed
            struct FApex
            {
                double Tau;
                bool bAtB;
            };
            const FApex apexes[] = {
                { shipLegS + 0.5 * cycle.TurnS, true },
                { 2.0 * shipLegS + 1.5 * cycle.TurnS, false },
            };
            for (const FApex& apex : apexes)
            {
                const double t = NpcTrafficTimeAt(ship, cycle, apex.Tau);
                const FVector3d local = NpcTrafficLocal(frame,
                    NpcTrafficPos(lane, ship, t, apex.bAtB ? frame.BM : frame.AM));
                const double expectedAxial = apex.bAtB ? -profile.EndArcBM + depth : profile.EndArcAM - depth;
                const double apexSpeed = NpcTrafficVel(lane, ship, t, stepH).Size();
                const double expectedSpeed = ship.RightOffsetM * w / depth;
                if (!NpcTrafficNear(local.X, expectedAxial, 0.0, NPCTRAFFIC_POSITION_TOL_M)
                    || FMath::Abs(local.Z) > NPCTRAFFIC_POSITION_TOL_M
                    || !NpcTrafficNear(local.Y, ship.UpOffsetM, 0.0, NPCTRAFFIC_POSITION_TOL_M)
                    || !NpcTrafficNear(apexSpeed, expectedSpeed, 1.0e-3, 0.5))
                {
                    apexFailures.Add(shipLabel + FString::Printf(TEXT(" apex at %s: (%.9e, %.9e, %.9e), expected "
                        "(%.9e, %.9e, 0), speed %.6e expected %.6e"), apex.bAtB ? TEXT("B") : TEXT("A"), local.X,
                        local.Y, local.Z, expectedAxial, ship.UpOffsetM, apexSpeed, expectedSpeed));
                }
            }

            // The legs start and end exactly at the end rings, on the side of their direction
            struct FRingPoint
            {
                double Tau;
                bool bAtB;
                double RightSign;
            };
            const FRingPoint ringPoints[] = {
                { 0.0, false, 1.0 },
                { shipLegS, true, 1.0 },
                { shipLegS + cycle.TurnS, true, -1.0 },
                { 2.0 * shipLegS + cycle.TurnS, false, -1.0 },
            };
            for (const FRingPoint& ring : ringPoints)
            {
                const double t = NpcTrafficTimeAt(ship, cycle, ring.Tau);
                const FVector3d local = NpcTrafficLocal(frame,
                    NpcTrafficPos(lane, ship, t, ring.bAtB ? frame.BM : frame.AM));
                const FVector3d expected(ring.bAtB ? -profile.EndArcBM : profile.EndArcAM, ship.UpOffsetM,
                    ring.RightSign * ship.RightOffsetM);
                if ((local - expected).GetAbsMax() > NPCTRAFFIC_POSITION_TOL_M)
                {
                    ringFailures.Add(shipLabel + FString::Printf(TEXT(" tau %.6e: (%.9e, %.9e, %.9e), expected "
                        "(%.9e, %.9e, %.9e)"), ring.Tau, local.X, local.Y, local.Z, expected.X, expected.Y,
                        expected.Z));
                }
            }
        }
        NpcTrafficReport(*this, lane.Name + TEXT(": position continuous at the joints"), positionFailures);
        NpcTrafficReport(*this, lane.Name + TEXT(": velocity continuous, speed w at the joints"), velocityFailures);
        NpcTrafficReport(*this, lane.Name + TEXT(": U-turn apex"), apexFailures);
        NpcTrafficReport(*this, lane.Name + TEXT(": legs end at the end rings"), ringFailures);
        TestTrue(lane.Name + TEXT(": wall radius positive"), rw > 0.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathClearanceTest, "SOLTest.NpcTrafficMath.UTurnPlanetClearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8 (D14, R1): with the end rings 3 wall radii above the surface no ship sample is inside either body, the
// near-axis U-turn apex included (it lies (m - 2) Rw above the surface)
bool FSOLNpcTrafficMathClearanceTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const TArray<FNpcTrafficLane> lanes = NpcTrafficTestLanes();
    for (const FNpcTrafficLane& lane : lanes)
    {
        const FSOLLaneProfile& profile = lane.Profile;
        const FSOLLaneFrame& frame = lane.Frame;
        FNpcTrafficFailures failures;
        for (const double rho : { 0.0, 0.001, 0.5, 1.0, 2.0 })
        {
            for (const double clock : { 0.0, 0.5 * NPCTRAFFIC_PI, NPCTRAFFIC_PI })
            {
                const FSOLLaneShip ship = MakeLaneShip(profile, 0.37, 0.8, rho, clock);
                const double shipLegS = LaneShipLegS(profile, ship);
                const FSOLLaneCycle cycle = MakeCycle(shipLegS, profile.UTurnDepthM);

                // Both U-turns densely, and the whole cycle coarsely
                TArray<double, TInlineAllocator<400>> taus;
                for (int32 step = 0; step <= 64; ++step)
                {
                    const double fraction = static_cast<double>(step) / 64.0;
                    taus.Add(shipLegS + fraction * cycle.TurnS);
                    taus.Add(2.0 * shipLegS + cycle.TurnS + fraction * cycle.TurnS);
                }
                for (int32 step = 0; step < 200; ++step)
                {
                    taus.Add(cycle.CycleS * (static_cast<double>(step) + 0.5) / 200.0);
                }
                for (const double tau : taus)
                {
                    const double t = NpcTrafficTimeAt(ship, cycle, tau);
                    const double fromA = NpcTrafficPos(lane, ship, t, frame.AM).Size();
                    const double fromB = NpcTrafficPos(lane, ship, t, frame.BM).Size();
                    if (!(fromA >= lane.BodyRadiusAM) || !(fromB >= lane.BodyRadiusBM))
                    {
                        failures.Add(FString::Printf(TEXT("rho %.3f clock %.3f tau %.6e: %.6e m from A (R %.6e), "
                            "%.6e m from B (R %.6e)"), rho, clock, tau, fromA, lane.BodyRadiusAM, fromB,
                            lane.BodyRadiusBM));
                    }
                }

                // The axis ship's apex: (m - depth) Rw above the surface along the axis
                if (rho == 0.0 && clock == 0.0)
                {
                    const double clearanceRadii = SOL::NPC_TUNNEL_END_MARGIN_RADII
                        - SOL::NPC_LANE_UTURN_DEPTH_WALL_RADII;
                    const double tApexB = NpcTrafficTimeAt(ship, cycle, shipLegS + 0.5 * cycle.TurnS);
                    NpcTrafficExpectNear(*this, lane.Name + TEXT(": axis apex at B, distance from B"),
                        NpcTrafficPos(lane, ship, tApexB, frame.BM).Size(),
                        lane.BodyRadiusBM + clearanceRadii * profile.WallRadiusM, 0.0, NPCTRAFFIC_POSITION_TOL_M);
                    const double tApexA = NpcTrafficTimeAt(ship, cycle, 2.0 * shipLegS + 1.5 * cycle.TurnS);
                    NpcTrafficExpectNear(*this, lane.Name + TEXT(": axis apex at A, distance from A"),
                        NpcTrafficPos(lane, ship, tApexA, frame.AM).Size(),
                        lane.BodyRadiusAM + clearanceRadii * profile.WallRadiusM, 0.0, NPCTRAFFIC_POSITION_TOL_M);

                    // The closest approach over 1,025 samples of each U-turn is the apex: one wall radius (D14)
                    double closestAboveA = TNumericLimits<double>::Max();
                    double closestAboveB = TNumericLimits<double>::Max();
                    for (int32 step = 0; step <= 1024; ++step)
                    {
                        const double fraction = static_cast<double>(step) / 1024.0;
                        const double tB = NpcTrafficTimeAt(ship, cycle, shipLegS + fraction * cycle.TurnS);
                        const double tA = NpcTrafficTimeAt(ship, cycle,
                            2.0 * shipLegS + cycle.TurnS + fraction * cycle.TurnS);
                        closestAboveB = FMath::Min(closestAboveB,
                            NpcTrafficPos(lane, ship, tB, frame.BM).Size() - lane.BodyRadiusBM);
                        closestAboveA = FMath::Min(closestAboveA,
                            NpcTrafficPos(lane, ship, tA, frame.AM).Size() - lane.BodyRadiusAM);
                    }
                    const double floorM = profile.WallRadiusM - NPCTRAFFIC_POSITION_TOL_M;
                    TestTrue(lane.Name + FString::Printf(TEXT(": U-turn at A stays %.6e m >= 1 Rw above the surface"),
                        closestAboveA), closestAboveA >= floorM);
                    TestTrue(lane.Name + FString::Printf(TEXT(": U-turn at B stays %.6e m >= 1 Rw above the surface"),
                        closestAboveB), closestAboveB >= floorM);
                }
            }
        }
        NpcTrafficReport(*this, lane.Name + TEXT(": no sample inside a body"), failures);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathWakeBoundTest, "SOLTest.NpcTrafficMath.WakeBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 12: the lane ship speed bound is at or above the largest sampled ship speed relative to A (brute force over
// 1,000 ships of a lane whose B moves, l / l0 in {0.15, 1, 2}, endpoint speed 0 and 30 km/s), and not absurdly loose
bool FSOLNpcTrafficMathWakeBoundTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const FVector3d aM(1.2e11, -8.0e10, 3.0e6);
    const FVector3d direction = FVector3d(0.6, 0.8, 0.0);
    const double h = 0.5;
    const double t0 = 1000.0;
    struct FWakeLane
    {
        const TCHAR* Name;
        double LaneRadiusM;
        int32 InWall;
        double NominalLengthM;
        double RadiusAM;
        double RadiusBM;
    };
    const FWakeLane wakeLanes[] = {
        { TEXT("Earth - Mars"), NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M, NPCTRAFFIC_EARTH_MARS_IN_WALL,
            NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M },
        { TEXT("Earth - Moon"), NPCTRAFFIC_EARTH_MOON_LANE_RADIUS_M, NPCTRAFFIC_EARTH_MOON_IN_WALL,
            NPCTRAFFIC_MOON_APOAPSIS_M, NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MOON_RADIUS_M },
    };
    for (const FWakeLane& wakeLane : wakeLanes)
    {
        const FSOLLaneProfile profile = NpcTrafficProfile(wakeLane.LaneRadiusM, wakeLane.InWall,
            wakeLane.NominalLengthM, wakeLane.RadiusAM, wakeLane.RadiusBM);
        const double l0 = profile.NominalLegM;
        const double maxPeak = MakeLegProfile(1.0, SOL::NPC_LANE_EFFORT_MAX, l0).PeakMps;
        for (const double ratio : { 0.15, 1.0, 2.0 })
        {
            for (const double endpointSpeed : { 0.0, 3.0e4 })
            {
                const FString label = FString::Printf(TEXT("%s, l / l0 %.2f, endpoint %.0f m/s"), wakeLane.Name, ratio,
                    endpointSpeed);
                const FVector3d b0 = aM + direction * (profile.EndArcAM + profile.EndArcBM + ratio * l0);
                const FSOLLaneFrame frame0 = MakeLaneFrame(profile, aM, b0);
                const FVector3d velocityB = (frame0.Axis * 0.6 + frame0.Up * 0.8) * endpointSpeed;
                const FSOLLaneFrame framePlus = MakeLaneFrame(profile, aM, b0 + velocityB * h);
                const FSOLLaneFrame frameMinus = MakeLaneFrame(profile, aM, b0 - velocityB * h);

                // Brute force: the fastest of 1,000 ships relative to A (ten axis ships at full effort first)
                double fastest = 0.0;
                for (int32 shipIndex = 0; shipIndex < 1000; ++shipIndex)
                {
                    const uint64 key = 5000ull + static_cast<uint64>(shipIndex) * 4ull;
                    const bool bPeakShip = shipIndex < 10;
                    const double phase = bPeakShip ? 0.1 * static_cast<double>(shipIndex) : NpcTrafficUnit(key);
                    const double effort = bPeakShip ? SOL::NPC_LANE_EFFORT_MAX : SOL::NPC_LANE_EFFORT_MIN
                        + (SOL::NPC_LANE_EFFORT_MAX - SOL::NPC_LANE_EFFORT_MIN) * NpcTrafficUnit(key + 1);
                    const double rho = bPeakShip ? 0.0 : SOL::NPC_LANE_HALO_OUTER_WALL_RADII * NpcTrafficUnit(key + 2);
                    const double clock = NPCTRAFFIC_PI * NpcTrafficUnit(key + 3);
                    const FSOLLaneShip ship = MakeLaneShip(profile, phase, effort, rho, clock);
                    const FVector3d plus = LaneShipPositionRelM(profile, framePlus, ship, t0 + h, aM);
                    const FVector3d minus = LaneShipPositionRelM(profile, frameMinus, ship, t0 - h, aM);
                    fastest = FMath::Max(fastest, (plus - minus).Size() / (2.0 * h));
                }

                const double bound = LaneShipSpeedBoundMps(maxPeak, l0, ratio * l0, profile.HaloOuterRadiusM,
                    endpointSpeed);
                TestTrue(label + FString::Printf(TEXT(": bound %.6e >= fastest sampled %.6e"), bound, fastest),
                    bound >= fastest);
                TestTrue(label + FString::Printf(TEXT(": bound %.6e not absurdly loose"), bound),
                    FMath::IsFinite(bound)
                    && bound <= 1.25 * maxPeak * FMath::Max(1.0, ratio) + 2.0 * endpointSpeed + 1.0);
            }
        }
    }

    // A non-finite peak reads as the cap, so the bound never turns NaN (a NaN bound would never wake a chunk)
    for (const double badPeak : { NPCTRAFFIC_NAN, NPCTRAFFIC_INF })
    {
        const double bound = LaneShipSpeedBoundMps(badPeak, 1.0e11, 1.0e11, 1.0e7, 0.0);
        TestTrue(FString::Printf(TEXT("peak %f: bound %.6e finite and at least the cap"), badPeak, bound),
            FMath::IsFinite(bound) && bound >= SOL::NPC_LANE_SPEED_CAP_MPS);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathThrustTest, "SOLTest.NpcTrafficMath.ThrustState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 15 (consumer #51): Accelerate (alpha along travel), Coast (none), Brake (alpha against travel) on the legs and
// Turn (centripetal, (w / D_U)^2 x the distance from the turn's centre, w^2 / D_U at the apex) in the U-turns
bool FSOLNpcTrafficMathThrustTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    const TArray<FNpcTrafficLane> allLanes = NpcTrafficTestLanes();
    const FNpcTrafficLane* lanes[] = { &allLanes[0], &allLanes[2] };     // Capped Earth - Mars, uncapped Earth - Moon
    struct FThrustShip
    {
        double Rho;
        double Effort;
        double ClockRad;
    };
    const FThrustShip ships[] = {
        { 0.0, 1.0, 0.5 * NPCTRAFFIC_PI },
        { 0.5, 0.7, NPCTRAFFIC_PI / 3.0 },
        { 1.0, 0.9, 0.5 * NPCTRAFFIC_PI },
        { 1.6, 0.6, 0.25 * NPCTRAFFIC_PI },
    };
    for (const FNpcTrafficLane* lane : lanes)
    {
        const FSOLLaneProfile& profile = lane->Profile;
        const FSOLLaneFrame& frame = lane->Frame;
        const double depth = profile.UTurnDepthM;
        for (const FThrustShip& spec : ships)
        {
            const FString label = lane->Name + FString::Printf(TEXT(", rho %.2f"), spec.Rho);
            const FSOLLaneShip ship = MakeLaneShip(profile, 0.0, spec.Effort, spec.Rho, spec.ClockRad);
            const double shipLegS = LaneShipLegS(profile, ship);
            const FSOLLaneLeg leg = MakeLegProfile(RadialFactor(spec.Rho), spec.Effort, profile.NominalLegM);
            const FSOLLaneCycle cycle = MakeCycle(shipLegS, depth);

            // The legs: the burn phases by time into the leg, along or against travel
            FNpcTrafficFailures legFailures;
            for (const bool bOutbound : { true, false })
            {
                const double legStart = bOutbound ? 0.0 : shipLegS + cycle.TurnS;
                const FVector3d travel = bOutbound ? frame.Axis : -frame.Axis;
                for (int32 sample = 0; sample < 100; ++sample)
                {
                    const double tl = shipLegS * (static_cast<double>(sample) + 0.5) / 100.0;
                    if (FMath::Abs(tl - leg.AccelS) < 1.0e-6 * shipLegS
                        || FMath::Abs(tl - (shipLegS - leg.AccelS)) < 1.0e-6 * shipLegS)
                    {
                        continue;
                    }
                    const FSOLLaneThrustState state = LaneThrustAt(profile, frame, ship,
                        NpcTrafficTimeAt(ship, cycle, legStart + tl));
                    ESOLLaneThrust expectedKind = ESOLLaneThrust::Coast;
                    if (leg.Alpha > 0.0 && tl < leg.AccelS)
                    {
                        expectedKind = ESOLLaneThrust::Accelerate;
                    }
                    else if (leg.Alpha > 0.0 && tl > shipLegS - leg.AccelS)
                    {
                        expectedKind = ESOLLaneThrust::Brake;
                    }
                    const ESOLLaneSegment expectedSegment = bOutbound ? ESOLLaneSegment::Outbound
                        : ESOLLaneSegment::Inbound;
                    const FString where = FString::Printf(TEXT("%s tl %.6e"), bOutbound ? TEXT("outbound")
                        : TEXT("inbound"), tl);
                    if (state.Segment != expectedSegment || state.Thrust != expectedKind)
                    {
                        legFailures.Add(where + FString::Printf(TEXT(": segment %d thrust %d, expected %d / %d"),
                            static_cast<int32>(state.Segment), static_cast<int32>(state.Thrust),
                            static_cast<int32>(expectedSegment), static_cast<int32>(expectedKind)));
                        continue;
                    }
                    if (expectedKind == ESOLLaneThrust::Coast)
                    {
                        if (FMath::Abs(state.AccelMps2) > 1.0e-12)
                        {
                            legFailures.Add(where + FString::Printf(TEXT(": coasting with %.6e m/s^2"),
                                state.AccelMps2));
                        }
                        continue;
                    }
                    const FVector3d expectedDirection = expectedKind == ESOLLaneThrust::Accelerate ? travel : -travel;
                    if (!NpcTrafficNear(state.AccelMps2, leg.Alpha, NPCTRAFFIC_TOL)
                        || (state.Direction - expectedDirection).GetAbsMax() > 1.0e-9)
                    {
                        legFailures.Add(where + FString::Printf(TEXT(": %.9e m/s^2 along (%.6f, %.6f, %.6f), "
                            "expected %.9e"), state.AccelMps2, state.Direction.X, state.Direction.Y,
                            state.Direction.Z, leg.Alpha));
                    }
                }
            }
            NpcTrafficReport(*this, label + TEXT(": leg thrust"), legFailures);

            // The U-turns: centripetal toward the turn's centre
            for (const bool bAtB : { true, false })
            {
                const double turnStart = bAtB ? shipLegS : 2.0 * shipLegS + cycle.TurnS;
                const FVector3d originM = bAtB ? frame.BM : frame.AM;
                const FVector3d centreM = bAtB ? -frame.Axis * profile.EndArcBM + frame.Up * ship.UpOffsetM
                    : frame.Axis * profile.EndArcAM + frame.Up * ship.UpOffsetM;
                for (const double thetaFraction : { 0.25, 0.5, 0.75 })
                {
                    const double t = NpcTrafficTimeAt(ship, cycle, turnStart + thetaFraction * cycle.TurnS);
                    const FSOLLaneThrustState state = LaneThrustAt(profile, frame, ship, t);
                    const FVector3d fromCentre = NpcTrafficPos(*lane, ship, t, originM) - centreM;
                    const double expectedAccel = (w / depth) * (w / depth) * fromCentre.Size();
                    const FString where = label + FString::Printf(TEXT(", turn at %s, theta %.2f pi"),
                        bAtB ? TEXT("B") : TEXT("A"), thetaFraction);
                    TestTrue(where + TEXT(": segment"), state.Segment == (bAtB ? ESOLLaneSegment::TurnAtB
                        : ESOLLaneSegment::TurnAtA));
                    TestTrue(where + TEXT(": thrust kind Turn"), state.Thrust == ESOLLaneThrust::Turn);
                    NpcTrafficExpectNear(*this, where + TEXT(": magnitude (w / D_U)^2 |p - c|"), state.AccelMps2,
                        expectedAccel, 1.0e-6, 1.0e-9);
                    if (fromCentre.Size() > 1.0)
                    {
                        TestTrue(where + TEXT(": toward the centre"),
                            (state.Direction + fromCentre.GetSafeNormal()).GetAbsMax() <= 1.0e-6);
                    }
                    if (thetaFraction == 0.5)
                    {
                        NpcTrafficExpectNear(*this, where + TEXT(": w^2 / D_U at the apex"), state.AccelMps2,
                            w * w / depth, 1.0e-6);
                    }
                }
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathEdgeCasesTest, "SOLTest.NpcTrafficMath.EdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Edge cases: zero, negative, NaN and huge lengths; kappa and effort clamped; times, arcs and phases clamped or
// wrapped; a non-drivable lane's ships at rest on the midpoint disc; an invalid frame; determinism
bool FSOLNpcTrafficMathEdgeCasesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    const double c = NpcTrafficCap();
    const double l0 = NPCTRAFFIC_LONG_LEG_M;

    // Huge leg: valid, capped, finite
    const FSOLLaneLeg huge = MakeLegProfile(1.0, 1.0, 1.0e14);
    TestTrue(TEXT("huge leg: valid"), huge.bValid);
    TestTrue(TEXT("huge leg: finite and >= 0"), NpcTrafficLegFiniteNonNegative(huge));
    NpcTrafficExpectNear(*this, TEXT("huge leg: peak c"), huge.PeakMps, c, NPCTRAFFIC_TOL);
    TestTrue(TEXT("huge leg: leg time above l0 / c"), huge.LegS > 1.0e14 / c);

    // Kappa and effort clamped
    TestTrue(TEXT("kappa NaN reads as 0"), NpcTrafficSameLeg(MakeLegProfile(NPCTRAFFIC_NAN, 1.0, l0),
        MakeLegProfile(0.0, 1.0, l0)));
    TestTrue(TEXT("kappa -0.2 reads as 0"), NpcTrafficSameLeg(MakeLegProfile(-0.2, 1.0, l0),
        MakeLegProfile(0.0, 1.0, l0)));
    TestTrue(TEXT("kappa 1.5 reads as 1"), NpcTrafficSameLeg(MakeLegProfile(1.5, 1.0, l0),
        MakeLegProfile(1.0, 1.0, l0)));
    TestTrue(TEXT("effort 0.2 reads as the minimum"), NpcTrafficSameLeg(MakeLegProfile(1.0, 0.2, l0),
        MakeLegProfile(1.0, SOL::NPC_LANE_EFFORT_MIN, l0)));
    TestTrue(TEXT("effort 1.7 reads as the maximum"), NpcTrafficSameLeg(MakeLegProfile(1.0, 1.7, l0),
        MakeLegProfile(1.0, SOL::NPC_LANE_EFFORT_MAX, l0)));
    TestTrue(TEXT("effort NaN reads as the minimum"), NpcTrafficSameLeg(MakeLegProfile(1.0, NPCTRAFFIC_NAN, l0),
        MakeLegProfile(1.0, SOL::NPC_LANE_EFFORT_MIN, l0)));

    // Leg arc and speed with bad input
    const FSOLLaneLeg leg = MakeLegProfile(1.0, 1.0, l0);
    NpcTrafficExpectNear(*this, TEXT("NaN time: arc 0"), LegArcM(leg, l0, NPCTRAFFIC_NAN), 0.0, 0.0, 1.0e-6);
    const FSOLLaneLeg invalid = MakeLegProfile(1.0, 1.0, -5.0);
    NpcTrafficExpectNear(*this, TEXT("invalid leg: arc 0"), LegArcM(invalid, -5.0, 100.0), 0.0, 0.0, 1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("invalid leg: speed w"), LegSpeedMps(invalid, -5.0, 100.0), w,
        NPCTRAFFIC_TOL);

    // Mean speed with bad input reads as the wall speed
    for (const double badLeg : { 0.0, -1.0, NPCTRAFFIC_NAN })
    {
        NpcTrafficExpectNear(*this, FString::Printf(TEXT("mean speed, leg %f: w"), badLeg),
            MeanSpeedMps(1.0e6, 0.0, badLeg), w, NPCTRAFFIC_TOL);
    }
    NpcTrafficExpectNear(*this, TEXT("mean speed, NaN rho: w"), MeanSpeedMps(0.5 * l0, NPCTRAFFIC_NAN, l0), w,
        NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("mean speed, NaN u: w"), MeanSpeedMps(NPCTRAFFIC_NAN, 0.0, l0), w,
        NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("mean speed, u < 0: w"), MeanSpeedMps(-1.0e9, 0.0, l0), w, NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("mean speed, u > l0: w"), MeanSpeedMps(2.0 * l0, 0.0, l0), w, NPCTRAFFIC_TOL);

    // Metric map with bad input, and its clamps
    NpcTrafficExpectNear(*this, TEXT("map: l0 0 -> 0"), MetricMapM(1.0e6, 0.0, 1.0e9), 0.0, 0.0, 1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("map: l0 NaN -> 0"), MetricMapM(1.0e6, NPCTRAFFIC_NAN, 1.0e9), 0.0, 0.0,
        1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("map: l NaN -> 0"), MetricMapM(1.0e6, l0, NPCTRAFFIC_NAN), 0.0, 0.0, 1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("map: u < 0 clamps to 0"), MetricMapM(-5.0, l0, 0.5 * l0), 0.0, 0.0, 1.0e-6);
    NpcTrafficExpectNear(*this, TEXT("map: u > l0 clamps to l"), MetricMapM(l0 + 5.0, l0, 0.5 * l0), 0.5 * l0,
        NPCTRAFFIC_TOL);
    NpcTrafficExpectNear(*this, TEXT("inverse: x < 0 clamps to 0"), MetricMapInverseM(-5.0, l0, 0.5 * l0), 0.0, 0.0,
        1.0e-6);
    NpcTrafficExpectNear(*this, TEXT("inverse: x > l clamps to l0"), MetricMapInverseM(0.5 * l0 + 5.0, l0, 0.5 * l0),
        l0, NPCTRAFFIC_TOL);
    TestEqual(TEXT("zone of a bad leg: 0"), MetricZoneM(NPCTRAFFIC_NAN), 0.0, 1.0e-12);
    TestEqual(TEXT("zone of a negative leg: 0"), MetricZoneM(-1.0e9), 0.0, 1.0e-12);

    // Ship construction clamps: phase wrapped, clock angle to [0, pi], radius to [0, H]
    const FSOLLaneProfile profile = NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
        NPCTRAFFIC_MARS_RADIUS_M);
    const double rw = profile.WallRadiusM;
    NpcTrafficExpectNear(*this, TEXT("phase 1.25 wraps to 0.25"), MakeLaneShip(profile, 1.25, 1.0, 0.5, 1.0).Phase,
        0.25, 0.0, 1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("phase -0.25 wraps to 0.75"), MakeLaneShip(profile, -0.25, 1.0, 0.5, 1.0).Phase,
        0.75, 0.0, 1.0e-12);
    const FSOLLaneShip negativeClock = MakeLaneShip(profile, 0.0, 1.0, 0.5, -0.5);
    NpcTrafficExpectNear(*this, TEXT("clock -0.5 clamps to 0: no right offset"), negativeClock.RightOffsetM, 0.0, 0.0,
        1.0e-9);
    NpcTrafficExpectNear(*this, TEXT("clock -0.5 clamps to 0: up offset rho Rw"), negativeClock.UpOffsetM, 0.5 * rw,
        1.0e-12);
    const FSOLLaneShip wideClock = MakeLaneShip(profile, 0.0, 1.0, 0.5, 4.0);
    TestTrue(TEXT("clock 4 clamps to pi: right offset >= 0 and tiny"),
        wideClock.RightOffsetM >= 0.0 && wideClock.RightOffsetM <= 1.0e-9 * rw);
    NpcTrafficExpectNear(*this, TEXT("clock 4 clamps to pi: up offset -rho Rw"), wideClock.UpOffsetM, -0.5 * rw,
        1.0e-12);
    const FSOLLaneShip farShip = MakeLaneShip(profile, 0.0, 1.0, 3.0, 0.5 * NPCTRAFFIC_PI);
    NpcTrafficExpectNear(*this, TEXT("rho 3 clamps to the halo's outer radius"), farShip.RightOffsetM,
        profile.HaloOuterRadiusM, 1.0e-12);
    const FSOLLaneShip nanShip = MakeLaneShip(profile, NPCTRAFFIC_NAN, NPCTRAFFIC_NAN, NPCTRAFFIC_NAN, NPCTRAFFIC_NAN);
    TestTrue(TEXT("NaN spawn values: finite ship"), FMath::IsFinite(nanShip.Phase) && FMath::IsFinite(nanShip.DvMps)
        && FMath::IsFinite(nanShip.UpOffsetM) && FMath::IsFinite(nanShip.RightOffsetM)
        && FMath::IsFinite(nanShip.AccelS) && FMath::IsFinite(LaneShipLegS(profile, nanShip)));
    NpcTrafficExpectNear(*this, TEXT("NaN rho reads as 0: no offset"),
        FMath::Abs(nanShip.UpOffsetM) + FMath::Abs(nanShip.RightOffsetM), 0.0, 0.0, 1.0e-9);

    // The cycle with a NaN time
    const FSOLLaneCycle cycle = MakeCycle(7.7e5, 1.6e7);
    const FSOLLaneCyclePoint nanPoint = CycleSegmentAt(cycle, 0.4, NPCTRAFFIC_NAN);
    TestTrue(TEXT("NaN time: phase finite in [0, 1)"), FMath::IsFinite(nanPoint.Phase) && nanPoint.Phase >= 0.0
        && nanPoint.Phase < 1.0);

    // A non-drivable lane: its ships rest on the midpoint disc
    const FVector3d aM(1.2e11, -8.0e10, 3.0e6);
    const FSOLLaneProfile shortProfile = NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, 0.5 * (profile.EndArcAM + profile.EndArcBM), NPCTRAFFIC_EARTH_RADIUS_M,
        NPCTRAFFIC_MARS_RADIUS_M);
    const FVector3d shortB = aM + FVector3d(0.6, 0.8, 0.0) * (shortProfile.NominalLengthM);
    const FSOLLaneFrame shortFrame = MakeLaneFrame(shortProfile, aM, shortB);
    const FSOLLaneShip restShip = MakeLaneShip(shortProfile, 0.3, 0.8, 0.5, 1.0);
    const FVector3d restA = LaneShipPositionRelM(shortProfile, shortFrame, restShip, 0.0, aM);
    const FVector3d restB = LaneShipPositionRelM(shortProfile, shortFrame, restShip, 1.0e5, aM);
    TestFalse(TEXT("short lane: not drivable"), shortProfile.bDrivable);
    TestTrue(TEXT("short lane: finite"), FMath::IsFinite(restA.X) && FMath::IsFinite(restA.Y)
        && FMath::IsFinite(restA.Z));
    TestTrue(TEXT("short lane: at rest"), (restA - restB).GetAbsMax() <= 1.0e-6);
    NpcTrafficExpectNear(*this, TEXT("short lane: on the midpoint disc"),
        FVector3d::DotProduct(restA, shortFrame.Axis), 0.5 * (shortB - aM).Size(), NPCTRAFFIC_TOL, 1.0e-6);

    // An invalid frame (A == B): the position is A
    const FSOLLaneFrame badFrame = MakeLaneFrame(profile, aM, aM);
    const FSOLLaneShip ship = MakeLaneShip(profile, 0.3, 0.8, 0.5, 1.0);
    TestTrue(TEXT("invalid frame: the position is A"),
        LaneShipPositionRelM(profile, badFrame, ship, 1.0e4, aM).GetAbsMax() <= 1.0e-9);

    // Determinism: inputs rebuilt from scratch give bit-identical outputs at 64 sampled times, evaluated in forward
    // and in reverse order (no hidden state), and a leg's arc matches its closed form while accelerating
    const FSOLLaneLeg legOnce = MakeLegProfile(0.37, 0.81, 1.23e11);
    const FSOLLaneLeg legTwice = MakeLegProfile(0.37, 0.81, 1.23e11);
    TestTrue(TEXT("deterministic leg profile"), legOnce.PeakMps == legTwice.PeakMps && legOnce.LegS == legTwice.LegS
        && legOnce.AccelS == legTwice.AccelS && legOnce.CoastS == legTwice.CoastS && legOnce.Alpha == legTwice.Alpha
        && legOnce.DvMps == legTwice.DvMps && legOnce.AccelM == legTwice.AccelM);
    const double accelSample = 0.4 * legOnce.AccelS;
    NpcTrafficExpectNear(*this, TEXT("arc while accelerating: w t + alpha t^2 / 2"), LegArcM(legOnce, 1.23e11,
        accelSample), w * accelSample + 0.5 * legOnce.Alpha * accelSample * accelSample, 1.0e-12);
    const FVector3d bM = aM + FVector3d(0.6, 0.8, 0.0) * profile.NominalLengthM;
    const FSOLLaneProfile profileAgain = NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
        NPCTRAFFIC_MARS_RADIUS_M);
    const FSOLLaneFrame frame = MakeLaneFrame(profile, aM, bM);
    const FSOLLaneFrame frameAgain = MakeLaneFrame(profileAgain, aM, bM);
    const FSOLLaneShip shipAgain = MakeLaneShip(profileAgain, 0.3, 0.8, 0.5, 1.0);
    const FSOLLaneCycle shipCycle = MakeCycle(LaneShipLegS(profile, ship), profile.UTurnDepthM);
    FVector3d forward[64];
    for (int32 sample = 0; sample < 64; ++sample)
    {
        const double t = shipCycle.CycleS * NpcTrafficUnit(static_cast<uint64>(sample) + 700ull);
        forward[sample] = LaneShipPositionM(profile, frame, ship, t);
    }
    FNpcTrafficFailures determinismFailures;
    for (int32 sample = 63; sample >= 0; --sample)
    {
        const double t = shipCycle.CycleS * NpcTrafficUnit(static_cast<uint64>(sample) + 700ull);
        const FVector3d again = LaneShipPositionM(profileAgain, frameAgain, shipAgain, t);
        if (!(again == forward[sample]))
        {
            determinismFailures.Add(FString::Printf(TEXT("t %.9e: (%.17e, %.17e, %.17e) then (%.17e, %.17e, %.17e)"),
                t, forward[sample].X, forward[sample].Y, forward[sample].Z, again.X, again.Y, again.Z));
        }
        const FSOLLaneThrustState thrust = LaneThrustAt(profile, frame, ship, t);
        const FSOLLaneThrustState thrustAgain = LaneThrustAt(profileAgain, frameAgain, shipAgain, t);
        if (thrust.AccelMps2 != thrustAgain.AccelMps2 || !(thrust.Direction == thrustAgain.Direction))
        {
            determinismFailures.Add(FString::Printf(TEXT("thrust differs at t %.9e"), t));
        }
    }
    NpcTrafficReport(*this, TEXT("deterministic positions and thrust"), determinismFailures);
    TestTrue(TEXT("deterministic cycle"), CycleSegmentAt(cycle, 0.4, 3.1e6).CycleTimeS
        == CycleSegmentAt(MakeCycle(7.7e5, 1.6e7), 0.4, 3.1e6).CycleTimeS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathPrecisionTest, "SOLTest.NpcTrafficMath.Precision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No float truncation of a lane 4e11 m long placed 2e11 m from the origin (D10, section 4.4): evaluated from the
// nearer end, a ship near either body is exact relative to it (lateral offsets to 1e-6 m, a millisecond's travel to
// 1e-4 m), the stored ship constants keep full double precision, and the absolute position agrees with the relative one
bool FSOLNpcTrafficMathPrecisionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    const FVector3d aM(2.0e11, 1.0e11, -4.0e6);
    const FVector3d direction = FVector3d(-0.6, 0.8, 0.0);
    const FNpcTrafficLane lane = NpcTrafficMakeLane(TEXT("Earth - Mars far out"), NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
        NPCTRAFFIC_MARS_RADIUS_M, aM, direction, 1.0);
    const FSOLLaneProfile& profile = lane.Profile;
    const FSOLLaneFrame& frame = lane.Frame;
    const FSOLLaneShip ship = MakeLaneShip(profile, 0.0, 0.9, 0.6, 1.0);
    const double shipLegS = LaneShipLegS(profile, ship);
    const FSOLLaneLeg leg = MakeLegProfile(RadialFactor(0.6), 0.9, profile.NominalLegM);
    TestTrue(TEXT("B far from the origin"), frame.BM.Size() > 1.0e11);

    // Near B: one second before the leg's end ring
    const double nearB = shipLegS - 1.0;
    const FVector3d relB = NpcTrafficPos(lane, ship, nearB, frame.BM);
    const FVector3d localB = NpcTrafficLocal(frame, relB);
    NpcTrafficExpectNear(*this, TEXT("near B: up offset exact"), localB.Y, ship.UpOffsetM, 0.0, 1.0e-6);
    NpcTrafficExpectNear(*this, TEXT("near B: right offset exact"), localB.Z, ship.RightOffsetM, 0.0, 1.0e-6);
    NpcTrafficExpectNear(*this, TEXT("near B: 1 s of braking before the end ring"), localB.X,
        -(profile.EndArcBM + w + 0.5 * leg.Alpha), 0.0, NPCTRAFFIC_POSITION_TOL_M);
    const FVector3d relBLater = NpcTrafficPos(lane, ship, nearB + 1.0e-3, frame.BM);
    NpcTrafficExpectNear(*this, TEXT("near B: a millisecond's travel"), (relBLater - relB).Size(),
        LegSpeedMps(leg, profile.NominalLegM, nearB + 0.5e-3) * 1.0e-3, 0.0, 1.0e-4);

    // Near A: one second after the leg's start ring
    const FVector3d localA = NpcTrafficLocal(frame, NpcTrafficPos(lane, ship, 1.0, frame.AM));
    NpcTrafficExpectNear(*this, TEXT("near A: up offset exact"), localA.Y, ship.UpOffsetM, 0.0, 1.0e-6);
    NpcTrafficExpectNear(*this, TEXT("near A: right offset exact"), localA.Z, ship.RightOffsetM, 0.0, 1.0e-6);
    NpcTrafficExpectNear(*this, TEXT("near A: 1 s of acceleration after the end ring"), localA.X,
        profile.EndArcAM + w + 0.5 * leg.Alpha, 0.0, NPCTRAFFIC_POSITION_TOL_M);

    // The absolute position agrees with the relative one (double, not float)
    const FVector3d absolute = LaneShipPositionM(profile, frame, ship, nearB);
    TestTrue(TEXT("absolute - B = relative to B"), ((absolute - frame.BM) - relB).GetAbsMax() <= 1.0e-3);

    // A ship barely in the halo's ramp keeps its tiny dv in full precision
    const double rampRho = 1.0 + 0.04 * SOL::NPC_LANE_HALO_RAMP_WALL_RADII;
    const FSOLLaneShip rampShip = MakeLaneShip(profile, 0.0, 1.0, rampRho, 1.0);
    const FSOLLaneLeg rampLeg = MakeLegProfile(RadialFactor(rampRho), 1.0, profile.NominalLegM);
    TestTrue(TEXT("ramp ship: dv positive"), rampShip.DvMps > 0.0);
    NpcTrafficExpectNear(*this, TEXT("ramp ship: dv in full precision"), rampShip.DvMps, rampLeg.DvMps, 1.0e-12);
    NpcTrafficExpectNear(*this, TEXT("ramp ship: dv = peak - w"), rampLeg.DvMps, rampLeg.PeakMps - w, 1.0e-6);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathFloatRailTest, "SOLTest.NpcTrafficMath.FloatRail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 50a review: the rail stores a ship's constants as floats (SDD 50 section 4.6). With the phase, dv, offsets and t_a
// rounded to float, the leg time derived from them (LaneShipLegS) keeps every leg seamless: at the lane's middle at
// T_leg / 2 with the expected travel across +-1 ms (the nearer-end switch), the expected travel across every leg /
// U-turn joint, exactly at the end rings, and below the wake bound of the exact peak
bool FSOLNpcTrafficMathFloatRailTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double w = NpcTrafficWall();
    const double halfStep = 1.0e-3;
    const auto toFloat = [](const double value) { return static_cast<double>(static_cast<float>(value)); };
    const FNpcTrafficShipSpec specs[] = {
        { 0.6180339887, 1.0, 0.0, 0.5 * NPCTRAFFIC_PI },       // Axis ship at full effort (capped or uncapped)
        { 0.1234567891, 0.73, 0.4, 1.1 },
        { 0.8765432109, 0.55, 0.97, 2.0 },
        { 0.3333333333, 1.0, 1.004, 0.7 },                     // In the halo's ramp: a tiny dv
        { 0.9999999, 0.8, 1.6, 0.5 * NPCTRAFFIC_PI },          // Halo plateau, phase just below 1
    };
    const TArray<FNpcTrafficLane> lanes = NpcTrafficTestLanes();
    for (const FNpcTrafficLane& lane : lanes)
    {
        const FSOLLaneProfile& profile = lane.Profile;
        const FSOLLaneFrame& frame = lane.Frame;
        const double l0 = profile.NominalLegM;
        const double l = frame.CurrentLegM;
        const double maxPeak = MakeLegProfile(1.0, SOL::NPC_LANE_EFFORT_MAX, l0).PeakMps;
        const double bound = LaneShipSpeedBoundMps(maxPeak, l0, l, profile.HaloOuterRadiusM, 0.0);
        FNpcTrafficFailures failures;
        for (const FNpcTrafficShipSpec& spec : specs)
        {
            // The rail's float copy of the ship
            const FSOLLaneShip exact = MakeLaneShip(profile, spec.Phase, spec.Effort, spec.Rho, spec.ClockRad);
            FSOLLaneShip ship;
            ship.Phase = toFloat(exact.Phase);
            ship.DvMps = toFloat(exact.DvMps);
            ship.UpOffsetM = toFloat(exact.UpOffsetM);
            ship.RightOffsetM = toFloat(exact.RightOffsetM);
            ship.AccelS = toFloat(exact.AccelS);
            const double legS = LaneShipLegS(profile, ship);
            const FSOLLaneCycle cycle = MakeCycle(legS, profile.UTurnDepthM);
            const FSOLLaneLeg leg = MakeLegProfile(RadialFactor(spec.Rho), spec.Effort, l0);
            const FString label = lane.Name + FString::Printf(TEXT(", rho %.3f effort %.2f"), spec.Rho, spec.Effort);
            if (!(legS > 0.0) || !NpcTrafficNear(legS, leg.LegS, 1.0e-6))
            {
                failures.Add(label + FString::Printf(TEXT(": derived T_leg %.9e, exact %.9e"), legS, leg.LegS));
                continue;
            }

            // T_leg / 2 on both legs: the lane's middle, and the travel across +-1 ms at the mid-leg speed
            const double midSpeed = LegSpeedMps(leg, l0, 0.5 * leg.LegS) * MetricMapSlope(0.5 * l0, l0, l);
            for (const double legStart : { 0.0, legS + cycle.TurnS })
            {
                const double tauMid = legStart + 0.5 * legS;
                const FVector3d mid = NpcTrafficLocal(frame,
                    NpcTrafficPos(lane, ship, NpcTrafficTimeAt(ship, cycle, tauMid), frame.AM));
                const FVector3d before = NpcTrafficPos(lane, ship, NpcTrafficTimeAt(ship, cycle, tauMid - halfStep),
                    frame.AM);
                const FVector3d after = NpcTrafficPos(lane, ship, NpcTrafficTimeAt(ship, cycle, tauMid + halfStep),
                    frame.AM);
                const double travel = (after - before).Size();
                if (!NpcTrafficNear(mid.X, profile.EndArcAM + 0.5 * l, 0.0, 1.0)
                    || !NpcTrafficNear(travel, midSpeed * 2.0 * halfStep, 0.0, 1.0))
                {
                    failures.Add(label + FString::Printf(TEXT(": mid-leg at tau %.9e: axial %.9e (expected %.9e), "
                        "travel %.9e m over 2 ms (expected %.9e)"), tauMid, mid.X, profile.EndArcAM + 0.5 * l, travel,
                        midSpeed * 2.0 * halfStep));
                }
            }

            // Every joint: exactly at the end ring (relative to the nearer body), the wall speed's travel across it
            struct FFloatJoint
            {
                double Tau;
                bool bAtB;
            };
            const FFloatJoint joints[] = {
                { legS, true },
                { legS + cycle.TurnS, true },
                { 2.0 * legS + cycle.TurnS, false },
                { cycle.CycleS, false },
            };
            for (const FFloatJoint& joint : joints)
            {
                const FVector3d& bodyM = joint.bAtB ? frame.BM : frame.AM;
                const double ringAxial = joint.bAtB ? -profile.EndArcBM : profile.EndArcAM;
                const FVector3d at = NpcTrafficLocal(frame,
                    NpcTrafficPos(lane, ship, NpcTrafficTimeAt(ship, cycle, joint.Tau), bodyM));
                const FVector3d before = NpcTrafficPos(lane, ship, NpcTrafficTimeAt(ship, cycle, joint.Tau - halfStep),
                    bodyM);
                const FVector3d after = NpcTrafficPos(lane, ship, NpcTrafficTimeAt(ship, cycle, joint.Tau + halfStep),
                    bodyM);
                const double travel = (after - before).Size();
                if (!NpcTrafficNear(at.X, ringAxial, 0.0, NPCTRAFFIC_POSITION_TOL_M)
                    || !NpcTrafficNear(travel, w * 2.0 * halfStep, 0.0, 1.0))
                {
                    failures.Add(label + FString::Printf(TEXT(": joint at tau %.9e: axial %.9e (ring %.9e), travel "
                        "%.9e m over 2 ms (expected %.9e)"), joint.Tau, at.X, ringAxial, travel, w * 2.0 * halfStep));
                }
            }

            // The mid-leg speed relative to A stays below the wake bound computed from the exact peak
            const double speed = NpcTrafficVel(lane, ship, NpcTrafficTimeAt(ship, cycle, 0.5 * legS), 0.5).Size();
            if (!(speed <= bound))
            {
                failures.Add(label + FString::Printf(TEXT(": mid-leg speed %.12e above the bound %.12e"), speed,
                    bound));
            }
        }
        NpcTrafficReport(*this, lane.Name + TEXT(": float rail"), failures);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathMapCurvatureTest, "SOLTest.NpcTrafficMath.MapCurvature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.12: LaneThrustAt reports the nominal profile and leaves out the metric map's X''(u) u'^2 term. On
// every default lane, at a length below its shortest geometry (0.13 l0 for the planet lanes, the Moon's smallest
// perigee for Earth - Moon), that term stays below the peak acceleration a for the fastest ship (axis, full effort)
bool FSOLNpcTrafficMathMapCurvatureTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double a = NpcTrafficAccel();
    constexpr double planetRatio = 0.13;                    // Below Earth - Mars at its closest (about 0.137)
    constexpr double moonSmallestPerigeeM = 3.564e8;        // The Moon's smallest perigee distance
    struct FCurvatureLane
    {
        const TCHAR* BodyA;
        const TCHAR* BodyB;
        double RadiusAM;
        double RadiusBM;
        double MaxLengthM;
        double MinLengthM;                                  // 0: the planet ratio
    };
    const FCurvatureLane table[] = {
        { TEXT("Mercury"), TEXT("Venus"), NPCTRAFFIC_MERCURY_RADIUS_M, NPCTRAFFIC_VENUS_RADIUS_M,
            NPCTRAFFIC_MERCURY_APHELION_M + NPCTRAFFIC_VENUS_APHELION_M, 0.0 },
        { TEXT("Venus"), TEXT("Earth"), NPCTRAFFIC_VENUS_RADIUS_M, NPCTRAFFIC_EARTH_RADIUS_M,
            NPCTRAFFIC_VENUS_APHELION_M + NPCTRAFFIC_EARTH_APHELION_M, 0.0 },
        { TEXT("Earth"), TEXT("Moon"), NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MOON_RADIUS_M,
            NPCTRAFFIC_MOON_APOAPSIS_M, moonSmallestPerigeeM },
        { TEXT("Earth"), TEXT("Mars"), NPCTRAFFIC_EARTH_RADIUS_M, NPCTRAFFIC_MARS_RADIUS_M,
            NPCTRAFFIC_EARTH_MARS_MAX_M, 0.0 },
        { TEXT("Mars"), TEXT("Jupiter"), NPCTRAFFIC_MARS_RADIUS_M, NPCTRAFFIC_JUPITER_RADIUS_M,
            NPCTRAFFIC_MARS_APHELION_M + NPCTRAFFIC_JUPITER_APHELION_M, 0.0 },
        { TEXT("Jupiter"), TEXT("Saturn"), NPCTRAFFIC_JUPITER_RADIUS_M, NPCTRAFFIC_SATURN_RADIUS_M,
            NPCTRAFFIC_JUPITER_APHELION_M + NPCTRAFFIC_SATURN_APHELION_M, 0.0 },
        { TEXT("Saturn"), TEXT("Uranus"), NPCTRAFFIC_SATURN_RADIUS_M, NPCTRAFFIC_URANUS_RADIUS_M,
            NPCTRAFFIC_SATURN_APHELION_M + NPCTRAFFIC_URANUS_APHELION_M, 0.0 },
        { TEXT("Uranus"), TEXT("Neptune"), NPCTRAFFIC_URANUS_RADIUS_M, NPCTRAFFIC_NEPTUNE_RADIUS_M,
            NPCTRAFFIC_URANUS_APHELION_M + NPCTRAFFIC_NEPTUNE_APHELION_M, 0.0 },
    };
    const SOLNpcScale::FLayout layout = SOLNpcScale::DefaultLayout();
    for (const FCurvatureLane& row : table)
    {
        const FString label = FString::Printf(TEXT("%s - %s"), row.BodyA, row.BodyB);
        const SOLNpcScale::FLaneDef* def = layout.Lanes.FindByPredicate([&row](const SOLNpcScale::FLaneDef& lane)
        {
            return lane.BodyA == FName(row.BodyA) && lane.BodyB == FName(row.BodyB);
        });
        if (def == nullptr)
        {
            AddError(label + TEXT(": not in the default layout"));
            continue;
        }
        const FSOLLaneProfile profile = NpcTrafficProfile(def->RadiusM, 100000, row.MaxLengthM, row.RadiusAM,
            row.RadiusBM);
        const double l0 = profile.NominalLegM;
        const double l = row.MinLengthM > 0.0 ? row.MinLengthM - profile.EndArcAM - profile.EndArcBM
            : planetRatio * l0;
        const double z = profile.MetricZoneM;
        const FSOLLaneLeg fastest = MakeLegProfile(1.0, SOL::NPC_LANE_EFFORT_MAX, l0);

        // The worst |X''(u)| v(u)^2 over the zone at A (the zone at B mirrors it), X'' by a central difference
        const double h = 1.0e-4 * z;
        double worst = 0.0;
        for (int32 step = 1; step < 2000; ++step)
        {
            const double u = z * static_cast<double>(step) / 2000.0;
            const double curvature = (MetricMapSlope(u + h, l0, l) - MetricMapSlope(u - h, l0, l)) / (2.0 * h);
            const double speed = LegSpeedAtArcMps(fastest, l0, u);
            worst = FMath::Max(worst, FMath::Abs(curvature) * speed * speed);
        }
        AddInfo(label + FString::Printf(TEXT(": |X''| u'^2 at most %.3f m/s^2 (%.3f G) at l / l0 %.3f"), worst,
            worst / SOL::STANDARD_GRAVITY_MPS2, l / l0));
        TestTrue(label + FString::Printf(TEXT(": |X''| u'^2 %.6e below a %.6e"), worst, a), worst > 0.0 && worst < a);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTrafficMathShortCurrentLegTest, "SOLTest.NpcTrafficMath.ShortCurrentLeg",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 50a review: a current leg at or below the metric zone (the end rings within a zone of each other, or crossed) keeps
// the map monotone: l reads as 1.001 Z, so X stays strictly increasing with a positive slope and round-trips through
// its inverse, and a ship on such a crowded lane still moves steadily forward along its outbound leg
bool FSOLNpcTrafficMathShortCurrentLegTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double l0 = NPCTRAFFIC_LONG_LEG_M;
    const double z = MetricZoneM(l0);
    for (const double l : { z, 0.5 * z, 0.0, -1.0e9 })
    {
        const FString label = FString::Printf(TEXT("l %.6e"), l);
        NpcTrafficExpectNear(*this, label + TEXT(": X(l0) = 1.001 Z"), MetricMapM(l0, l0, l), 1.001 * z, 1.0e-12,
            1.0e-3);
        FNpcTrafficFailures failures;
        double previous = MetricMapM(0.0, l0, l);
        for (int32 sample = 1; sample <= 2000; ++sample)
        {
            const double u = l0 * static_cast<double>(sample) / 2000.0;
            const double x = MetricMapM(u, l0, l);
            const double slope = MetricMapSlope(u, l0, l);
            const double back = MetricMapInverseM(x, l0, l);
            if (!(x > previous) || !(slope > 0.0) || !NpcTrafficNear(back, u, 0.0, 1.0e-9 * l0))
            {
                failures.Add(FString::Printf(TEXT("u %.9e: X %.9e after %.9e, slope %.3e, inverse %.9e"), u, x,
                    previous, slope, back));
            }
            previous = x;
        }
        NpcTrafficReport(*this, label + TEXT(": monotone map"), failures);
    }

    // A lane whose current leg is 0.3 Z: the outbound leg moves forward at every sample, B's end shifted out
    const FVector3d aM(1.2e11, -8.0e10, 3.0e6);
    const FSOLLaneProfile profile = NpcTrafficProfile(NPCTRAFFIC_EARTH_MARS_LANE_RADIUS_M,
        NPCTRAFFIC_EARTH_MARS_IN_WALL, NPCTRAFFIC_EARTH_MARS_MAX_M, NPCTRAFFIC_EARTH_RADIUS_M,
        NPCTRAFFIC_MARS_RADIUS_M);
    const FSOLLaneFrame frame = MakeLaneFrame(profile, aM, aM + FVector3d(0.6, 0.8, 0.0)
        * (profile.EndArcAM + profile.EndArcBM + 0.3 * profile.MetricZoneM));
    const FSOLLaneShip ship = MakeLaneShip(profile, 0.0, 0.9, 0.4, 1.0);
    const double legS = LaneShipLegS(profile, ship);
    const FSOLLaneCycle cycle = MakeCycle(legS, profile.UTurnDepthM);
    TestTrue(TEXT("crowded lane: frame valid, drivable"), frame.bValid && profile.bDrivable);
    NpcTrafficExpectNear(*this, TEXT("crowded lane: end shift 0.701 Z"), frame.EndShiftM, 0.701 * profile.MetricZoneM,
        1.0e-9);
    FNpcTrafficFailures forwardFailures;
    double previousAxial = -TNumericLimits<double>::Max();
    for (int32 sample = 0; sample <= 500; ++sample)
    {
        const double tau = legS * static_cast<double>(sample) / 500.0;
        const FVector3d relM = LaneShipPositionRelM(profile, frame, ship, NpcTrafficTimeAt(ship, cycle, tau), aM);
        const double axial = FVector3d::DotProduct(relM, frame.Axis);
        if (!FMath::IsFinite(axial) || !(axial >= previousAxial - NPCTRAFFIC_POSITION_TOL_M))
        {
            forwardFailures.Add(FString::Printf(TEXT("tau %.9e: axial %.9e after %.9e"), tau, axial, previousAxial));
        }
        previousAxial = axial;
    }
    NpcTrafficReport(*this, TEXT("crowded lane: outbound leg moves forward"), forwardFailures);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
