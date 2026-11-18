/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneTrafficMath.h"
#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"
#include "Universe/SOLRenderPlacement.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the drawn hull attitude of the issue #50 lane ships (SDD 50 section 16's open question on the hull's roll,
// answered by the user 2026-11-18), written test first against Docs/Plans/50-hull-flip-contract.md. The new API:
//   FSOLLaneHullBasis { FVector3d Forward; FVector3d Top; }
//   int32 SOLLaneTraffic::LaneFlipSide(uint32 laneShipKey)                         +1 / -1, a pure function of the key
//   FSOLLaneHullBasis SOLLaneTraffic::LaneHullBasis(profile, frame, ship, uint32 laneShipKey, double timeS)
//   FQuat SOLLaneTraffic::HullRotation(const FVector3d& forward, const FVector3d& top)   MakeFromXZ style
//   double SOLLaneTraffic::LaneFlipEndAccelMps2(double shipLengthM, double flipS = SOL::NPC_LANE_FLIP_S)
//   double SOLLaneTraffic::LaneFlipEndAccelLimitMps2(double shipLengthM)
//   double SOLLaneTraffic::LaneFlipDurationS(double shipLengthM)          the per-ship window, at least NPC_LANE_FLIP_S
// User decisions (2026-11-18): both flips turn in the same plane with the attitude held through the brake and the
// U-turn (implied here by the held attitude, the rigid half turn and periodicity together), and the flip window grows
// with ship length so the end G stays under the limit. No ship length reaches the lane math yet (every hull is the 30 m
// placeholder, whose window is exactly NPC_LANE_FLIP_S), so the lane tests here use the 60 s window. Frames: universe
// ecliptic metres, right-handed, +Z north; the drawn rotation is frame-agnostic.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCHULLFLIP_PI = 3.141592653589793238462643383279502884;
    constexpr double NPCHULLFLIP_G = 9.80665;
    constexpr double NPCHULLFLIP_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double NPCHULLFLIP_INF = std::numeric_limits<double>::infinity();
    constexpr double NPCHULLFLIP_JUMP_RAD = 1.0e-3;            // A basis change above this over...
    constexpr double NPCHULLFLIP_JUMP_S = 1.0e-4;              // ...an interval this short is a jump (snap)
    constexpr int32 NPCHULLFLIP_SCAN_BUDGET = 600000;          // Basis evaluations one ship's cycle scan may use

    // Body data (m) for the test lanes
    constexpr double NPCHULLFLIP_EARTH_RADIUS_M = 6.371e6;
    constexpr double NPCHULLFLIP_MOON_RADIUS_M = 1.7374e6;
    constexpr double NPCHULLFLIP_MARS_RADIUS_M = 3.3895e6;
    constexpr double NPCHULLFLIP_EARTH_MARS_MAX_M = 1.5210e11 + 2.4923e11;
    constexpr double NPCHULLFLIP_MOON_APOAPSIS_M = 4.055e8;

    // Failure counter of a sampled check, with the first failure's description
    struct FNpcHullFlipFailures
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

    // A test lane: its profile and a frame fixed in time
    struct FNpcHullFlipLane
    {
        FString Name;
        FSOLLaneProfile Profile;
        FSOLLaneFrame Frame;
    };

    // A test ship on a lane: its constants, key, leg time and cycle
    struct FNpcHullFlipShip
    {
        FString Name;
        FSOLLaneShip Ship;
        uint32 Key = 0;
        double LegS = 0.0;
        FSOLLaneCycle Cycle;
        bool bBurns = false;
    };

    //////////////////////////////////////////////////////////////////////////
    // Reports a sampled check: an error with the count and the first failure when anything failed
    void NpcHullFlipReport(FAutomationTestBase& test, const FString& what, const FNpcHullFlipFailures& failures)
    {
        if (failures.Count > 0)
        {
            test.AddError(FString::Printf(TEXT("%s: %d failures, first: %s"), *what, failures.Count,
                *failures.First));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the angle in radians between two vectors (atan2 of the cross and dot products, accurate near 0 and pi)
    double NpcHullFlipAngle(const FVector3d& a, const FVector3d& b)
    {
        return FMath::Atan2(FVector3d::CrossProduct(a, b).Size(), FVector3d::DotProduct(a, b));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the larger of the angles the forward and the top turn by between two bases
    double NpcHullFlipBasisGap(const FSOLLaneHullBasis& a, const FSOLLaneHullBasis& b)
    {
        return FMath::Max(NpcHullFlipAngle(a.Forward, b.Forward), NpcHullFlipAngle(a.Top, b.Top));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a basis is finite, both vectors unit and orthogonal within a tolerance
    bool NpcHullFlipOrthonormal(const FSOLLaneHullBasis& basis, const double tol)
    {
        return !basis.Forward.ContainsNaN() && !basis.Top.ContainsNaN()
            && FMath::Abs(basis.Forward.Size() - 1.0) <= tol && FMath::Abs(basis.Top.Size() - 1.0) <= tol
            && FMath::Abs(FVector3d::DotProduct(basis.Forward, basis.Top)) <= tol;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a deterministic uniform value in [0, 1) for a key (splitmix64)
    double NpcHullFlipUnit(const uint64 key)
    {
        uint64 z = key + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z = z ^ (z >> 31);
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's flip fraction f(tl) = S((tl - (legS - T_f) / 2) / T_f), S(x) = 3 x^2 - 2 x^3 with x
    // clamped to [0, 1]; 0 for a leg with no burn (SDD 50 section 4.4)
    double NpcHullFlipRefFraction(const double legS, const double accelS, const double tl)
    {
        if (!(accelS > 0.0))
        {
            return 0.0;
        }
        const double x = FMath::Clamp((FMath::Clamp(tl, 0.0, legS) - 0.5 * (legS - SOL::NPC_LANE_FLIP_S))
            / SOL::NPC_LANE_FLIP_S, 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a test lane: its profile at the nominal length (wall radius as the caller computes it, D13) and a frame
    // with A at aM and B along a direction at the nominal length
    FNpcHullFlipLane NpcHullFlipMakeLane(const TCHAR* name, const double laneRadiusM, const int32 inWallCount,
        const double nominalLengthM, const double radiusAM, const double radiusBM, const FVector3d& aM,
        const FVector3d& direction)
    {
        FNpcHullFlipLane lane;
        lane.Name = name;
        lane.Profile = SOLLaneTraffic::MakeLaneProfile(SOLNpcTunnel::TunnelRadiusM(laneRadiusM, inWallCount),
            nominalLengthM, radiusAM, radiusBM);
        lane.Frame = SOLLaneTraffic::MakeLaneFrame(lane.Profile, aM, aM + direction.GetSafeNormal() * nominalLengthM);
        return lane;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the test lanes: capped Earth - Mars along an ecliptic diagonal, uncapped Earth - Moon inclined 7 degrees,
    // and uncapped Earth - Moon along ecliptic -X (Unreal -X, today's FindBetweenNormals singularity), far from the origin
    TArray<FNpcHullFlipLane> NpcHullFlipLanes()
    {
        const FVector3d aM(1.2e11, -8.0e10, 3.0e6);
        const double elevation = FMath::DegreesToRadians(7.0);
        const double azimuth = FMath::DegreesToRadians(40.0);
        const FVector3d inclined(FMath::Cos(elevation) * FMath::Cos(azimuth),
            FMath::Cos(elevation) * FMath::Sin(azimuth), FMath::Sin(elevation));

        TArray<FNpcHullFlipLane> lanes;
        lanes.Add(NpcHullFlipMakeLane(TEXT("Earth - Mars (capped)"), 3.0e6, 30000, NPCHULLFLIP_EARTH_MARS_MAX_M,
            NPCHULLFLIP_EARTH_RADIUS_M, NPCHULLFLIP_MARS_RADIUS_M, aM, FVector3d(0.6, 0.8, 0.0)));
        lanes.Add(NpcHullFlipMakeLane(TEXT("Earth - Moon inclined (uncapped)"), 2.0e5, 20000,
            NPCHULLFLIP_MOON_APOAPSIS_M, NPCHULLFLIP_EARTH_RADIUS_M, NPCHULLFLIP_MOON_RADIUS_M, aM, inclined));
        lanes.Add(NpcHullFlipMakeLane(TEXT("Earth - Moon along -X"), 2.0e5, 20000, NPCHULLFLIP_MOON_APOAPSIS_M,
            NPCHULLFLIP_EARTH_RADIUS_M, NPCHULLFLIP_MOON_RADIUS_M, aM, FVector3d(-1.0, 0.0, 0.0)));
        return lanes;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns keys with each flip side: the first 'perSide' keys of each side found from a strided search
    TArray<uint32> NpcHullFlipKeysBothSides(const int32 perSide)
    {
        TArray<uint32> plus;
        TArray<uint32> minus;
        for (uint32 index = 0; index < 4096 && (plus.Num() < perSide || minus.Num() < perSide); ++index)
        {
            const uint32 key = 1000u + index * 7919u;
            TArray<uint32>& side = SOLLaneTraffic::LaneFlipSide(key) > 0 ? plus : minus;
            if (side.Num() < perSide)
            {
                side.Add(key);
            }
        }
        TArray<uint32> keys;
        for (int32 index = 0; index < perSide; ++index)
        {
            keys.Add(plus.IsValidIndex(index) ? plus[index] : 1000u + static_cast<uint32>(index));
            keys.Add(minus.IsValidIndex(index) ? minus[index] : 2000u + static_cast<uint32>(index));
        }
        return keys;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's test ships: six burning ones (wall and halo radii, both flip sides) and one at rho 1 (kappa 0:
    // no burn, no brake, velocity facing in the turns)
    TArray<FNpcHullFlipShip> NpcHullFlipShips(const FNpcHullFlipLane& lane, const int32 laneIndex)
    {
        const double rhos[] = { 0.1, 0.35, 0.6, 0.85, 1.5, 0.5, 1.0 };
        const TArray<uint32> keys = NpcHullFlipKeysBothSides(3);
        TArray<FNpcHullFlipShip> ships;
        for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(rhos)); ++index)
        {
            const uint64 seed = static_cast<uint64>(laneIndex) * 1000003ull + static_cast<uint64>(index) * 8ull;
            const double phase = NpcHullFlipUnit(seed);
            const double effort = SOL::NPC_LANE_EFFORT_MIN
                + (SOL::NPC_LANE_EFFORT_MAX - SOL::NPC_LANE_EFFORT_MIN) * NpcHullFlipUnit(seed + 1);
            const double clockRad = index == 6 ? 0.5 * NPCHULLFLIP_PI
                : NPCHULLFLIP_PI * (0.1 + 0.8 * NpcHullFlipUnit(seed + 2));
            FNpcHullFlipShip ship;
            ship.Ship = SOLLaneTraffic::MakeLaneShip(lane.Profile, phase, effort, rhos[index], clockRad);
            ship.Key = index < keys.Num() ? keys[index] : 777u + static_cast<uint32>(laneIndex);
            ship.LegS = SOLLaneTraffic::LaneShipLegS(lane.Profile, ship.Ship);
            ship.Cycle = SOLLaneTraffic::MakeCycle(ship.LegS, lane.Profile.UTurnDepthM);
            ship.bBurns = ship.Ship.AccelS > 0.0;
            ship.Name = lane.Name + FString::Printf(TEXT(", ship %d (rho %.2f, key %u)"), index, rhos[index], ship.Key);
            ships.Add(ship);
        }
        return ships;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rail time at which a ship is at cycle time tau
    double NpcHullFlipTimeAt(const FNpcHullFlipShip& ship, const double tau)
    {
        return (tau / ship.Cycle.CycleS - ship.Ship.Phase) * ship.Cycle.CycleS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship's drawn hull basis at cycle time tau
    FSOLLaneHullBasis NpcHullFlipBasisAt(const FNpcHullFlipLane& lane, const FNpcHullFlipShip& ship, const double tau)
    {
        return SOLLaneTraffic::LaneHullBasis(lane.Profile, lane.Frame, ship.Ship, ship.Key,
            NpcHullFlipTimeAt(ship, tau));
    }

    //////////////////////////////////////////////////////////////////////////
    // Scans cycle times [t0, t1] for a jump of the basis: an interval whose ends differ by more than
    // NPCHULLFLIP_JUMP_RAD is halved (both halves) until shorter than NPCHULLFLIP_JUMP_S, where a remaining difference
    // is a jump; returns false on a jump (its cycle time in outJumpTau) or when the evaluation budget runs out
    bool NpcHullFlipScan(const FNpcHullFlipLane& lane, const FNpcHullFlipShip& ship, const double t0,
        const FSOLLaneHullBasis& b0, const double t1, const FSOLLaneHullBasis& b1, int32& inOutBudget,
        double& outJumpTau)
    {
        if (NpcHullFlipBasisGap(b0, b1) <= NPCHULLFLIP_JUMP_RAD)
        {
            return true;
        }
        if (t1 - t0 < NPCHULLFLIP_JUMP_S || inOutBudget <= 0)
        {
            outJumpTau = t0;
            return false;
        }
        --inOutBudget;
        const double tm = 0.5 * (t0 + t1);
        const FSOLLaneHullBasis bm = NpcHullFlipBasisAt(lane, ship, tm);
        return NpcHullFlipScan(lane, ship, t0, b0, tm, bm, inOutBudget, outJumpTau)
            && NpcHullFlipScan(lane, ship, tm, bm, t1, b1, inOutBudget, outJumpTau);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a unit vector perpendicular to a unit direction, from the world axis least aligned with it
    FVector3d NpcHullFlipPerpendicular(const FVector3d& direction)
    {
        const FVector3d absDirection = direction.GetAbs();
        const FVector3d helper = absDirection.X <= absDirection.Y && absDirection.X <= absDirection.Z
            ? FVector3d::UnitX() : (absDirection.Y <= absDirection.Z ? FVector3d::UnitY() : FVector3d::UnitZ());
        return FVector3d::CrossProduct(direction, helper).GetSafeNormal();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the angle in radians of the rotation between two unit quaternions, 2 atan2(|v|, |w|) of a^-1 b (q and
    // -q agree; robust near 0, where an acos form can return NaN)
    double NpcHullFlipQuatAngle(const FQuat& a, const FQuat& b)
    {
        const FQuat delta = a.Inverse() * b;
        return 2.0 * FMath::Atan2(FMath::Sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z),
            FMath::Abs(delta.W));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a rotation is finite, unit and maps +X to the forward within a tolerance
    bool NpcHullFlipRotationMapsForward(const FQuat& rotation, const FVector3d& forward, const double tol)
    {
        return !rotation.ContainsNaN() && FMath::Abs(rotation.Size() - 1.0) <= tol
            && (rotation.RotateVector(FVector3d::UnitX()) - forward.GetSafeNormal()).GetAbsMax() <= tol;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipRotationTest, "SOLTest.Npc.HullFlip.HullRotationNoSingularity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Clause 1: HullRotation(forward, top) is a unit, finite rotation mapping +X to the forward and +Z to the top made
// orthogonal to it, for every heading (-X and its neighbours down to 1e-12 rad, +-X, +-Y, +-Z, random), independent of
// the top's skew along the forward and its length; continuous along great circles through -X and +Z with a continuous
// top; finite with X -> forward for a degenerate top; and continuous as drawn (Unreal axes) through a flip on a lane
// along ecliptic -X
bool FSOLNpcHullFlipRotationTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;

    // The headings: -X, -X tilted toward Y and Z by tiny angles, the six axes, 200 random directions
    TArray<FVector3d> headings;
    headings.Add(-FVector3d::UnitX());
    for (const double tilt : { 1.0e-3, 1.0e-6, 1.0e-9, 1.0e-12 })
    {
        headings.Add(FVector3d(-FMath::Cos(tilt), FMath::Sin(tilt), 0.0));
        headings.Add(FVector3d(-FMath::Cos(tilt), -FMath::Sin(tilt), 0.0));
        headings.Add(FVector3d(-FMath::Cos(tilt), 0.0, FMath::Sin(tilt)));
        headings.Add(FVector3d(-FMath::Cos(tilt), 0.0, -FMath::Sin(tilt)));
    }
    headings.Append({ FVector3d::UnitX(), FVector3d::UnitY(), -FVector3d::UnitY(), FVector3d::UnitZ(),
        -FVector3d::UnitZ() });
    for (int32 index = 0; index < 200; ++index)
    {
        const double z = 2.0 * NpcHullFlipUnit(9000ull + 2ull * index) - 1.0;
        const double azimuth = 2.0 * NPCHULLFLIP_PI * NpcHullFlipUnit(9001ull + 2ull * index);
        const double r = FMath::Sqrt(FMath::Max(0.0, 1.0 - z * z));
        headings.Add(FVector3d(r * FMath::Cos(azimuth), r * FMath::Sin(azimuth), z));
    }

    // Unit, X -> forward, Z -> the top, the same for a skewed and scaled top; two tops per heading
    {
        FNpcHullFlipFailures failures;
        for (const FVector3d& heading : headings)
        {
            const FVector3d forward = heading.GetSafeNormal();
            const FVector3d first = NpcHullFlipPerpendicular(forward);
            const FVector3d second = FVector3d::CrossProduct(forward, first);
            for (const FVector3d& top : { first, second, -first })
            {
                const FQuat rotation = HullRotation(forward, top);
                const FString where = FString::Printf(TEXT("heading (%.15f, %.15f, %.15f), top (%.3f, %.3f, %.3f)"),
                    forward.X, forward.Y, forward.Z, top.X, top.Y, top.Z);
                if (!NpcHullFlipRotationMapsForward(rotation, forward, 1.0e-9)
                    || (rotation.RotateVector(FVector3d::UnitZ()) - top).GetAbsMax() > 1.0e-9)
                {
                    failures.Add(where + TEXT(": not unit, or X / Z not mapped to the forward / top"));
                    continue;
                }
                const FQuat skewed = HullRotation(3.0 * forward, 2.5 * (top + 0.4 * forward));
                if (!(NpcHullFlipQuatAngle(skewed, rotation) <= 1.0e-9))
                {
                    failures.Add(where + TEXT(": a scaled forward or a skewed, scaled top changes the rotation"));
                }
            }
        }
        NpcHullFlipReport(*this, TEXT("rotation at every heading"), failures);
    }

    // Continuous along great circles through -X (top +Z, and top in the circle's plane) and through +Z
    {
        struct FCircle
        {
            const TCHAR* Name;
            double Centre;
            int32 Plane;     // 0: XY with top +Z; 1: XZ with the top in the plane (turning with the heading)
        };
        const FCircle circles[] = {
            { TEXT("XY through -X"), NPCHULLFLIP_PI, 0 },
            { TEXT("XZ through -X"), NPCHULLFLIP_PI, 1 },
            { TEXT("XZ through +Z"), 0.5 * NPCHULLFLIP_PI, 1 },
            { TEXT("XZ through -Z"), -0.5 * NPCHULLFLIP_PI, 1 },
        };
        const double step = 1.0e-4;
        for (const FCircle& circle : circles)
        {
            // Returns the heading and top at an angle along the circle
            const auto poseAt = [&circle](const double phi, FVector3d& outForward, FVector3d& outTop)
            {
                if (circle.Plane == 0)
                {
                    outForward = FVector3d(FMath::Cos(phi), FMath::Sin(phi), 0.0);
                    outTop = FVector3d::UnitZ();
                }
                else
                {
                    outForward = FVector3d(FMath::Cos(phi), 0.0, FMath::Sin(phi));
                    outTop = FVector3d(-FMath::Sin(phi), 0.0, FMath::Cos(phi));
                }
            };
            FNpcHullFlipFailures failures;
            FVector3d forward;
            FVector3d top;
            poseAt(circle.Centre - 1000.0 * step, forward, top);
            FQuat previous = HullRotation(forward, top);
            for (int32 index = -999; index <= 1000; ++index)
            {
                poseAt(circle.Centre + static_cast<double>(index) * step, forward, top);
                const FQuat rotation = HullRotation(forward, top);
                const double gap = NpcHullFlipQuatAngle(previous, rotation);
                if (!NpcHullFlipRotationMapsForward(rotation, forward, 1.0e-9)
                    || !(gap <= step * (1.0 + 1.0e-6) + 1.0e-9))
                {
                    failures.Add(FString::Printf(TEXT("step %d: rotation changed by %.9e rad for a %.1e rad step"),
                        index, gap, step));
                }
                previous = rotation;
            }
            NpcHullFlipReport(*this, FString::Printf(TEXT("great circle %s: continuous"), circle.Name), failures);
        }
    }

    // A degenerate top (along the forward, against it, zero, NaN) still gives a finite unit rotation, X -> forward
    for (const FVector3d& forward : { -FVector3d::UnitX(), FVector3d::UnitZ(), FVector3d(0.6, -0.8, 0.0) })
    {
        for (const FVector3d& badTop : { forward, -2.0 * forward, FVector3d::ZeroVector,
            FVector3d(NPCHULLFLIP_NAN, 0.0, 1.0), FVector3d(0.0, NPCHULLFLIP_INF, 0.0) })
        {
            const FQuat rotation = HullRotation(forward, badTop);
            TestTrue(FString::Printf(TEXT("forward (%.1f, %.1f, %.1f), degenerate top (%f, %f, %f): finite, unit, "
                "X -> forward, repeatable"), forward.X, forward.Y, forward.Z, badTop.X, badTop.Y, badTop.Z),
                NpcHullFlipRotationMapsForward(rotation, forward, 1.0e-9)
                && rotation.Equals(HullRotation(forward, badTop), 0.0));
        }
    }

    // As drawn (both vectors converted to Unreal axes) through the flip on the lane along ecliptic -X
    {
        const TArray<FNpcHullFlipLane> lanes = NpcHullFlipLanes();
        const FNpcHullFlipLane& lane = lanes[2];
        TestTrue(TEXT("-X lane: axis along ecliptic -X"), (lane.Frame.Axis + FVector3d::UnitX()).GetAbsMax() < 1.0e-12);
        const double tf = SOL::NPC_LANE_FLIP_S;
        const double peakRate = 1.5 * NPCHULLFLIP_PI / tf;
        const double dt = 0.25;
        FNpcHullFlipFailures failures;
        for (const FNpcHullFlipShip& ship : NpcHullFlipShips(lane, 2))
        {
            if (!ship.bBurns)
            {
                continue;
            }
            for (const double legStart : { 0.0, ship.LegS + ship.Cycle.TurnS })
            {
                const double start = legStart + 0.5 * (ship.LegS - tf);
                // Returns the drawn rotation (Unreal axes) at a cycle time
                const auto drawnAt = [&](const double tau)
                {
                    const FSOLLaneHullBasis basis = NpcHullFlipBasisAt(lane, ship, tau);
                    return HullRotation(SOLRender::EclipticToUnreal(basis.Forward),
                        SOLRender::EclipticToUnreal(basis.Top));
                };
                FQuat previous = drawnAt(start - 5.0);
                for (int32 index = 1; index <= 280; ++index)
                {
                    const double tau = start - 5.0 + dt * static_cast<double>(index);
                    const FQuat rotation = drawnAt(tau);
                    const FSOLLaneHullBasis basis = NpcHullFlipBasisAt(lane, ship, tau);
                    const double gap = NpcHullFlipQuatAngle(previous, rotation);
                    if (!NpcHullFlipRotationMapsForward(rotation, SOLRender::EclipticToUnreal(basis.Forward), 1.0e-9)
                        || !(gap <= peakRate * dt * (1.0 + 1.0e-6) + 1.0e-9))
                    {
                        failures.Add(ship.Name + FString::Printf(TEXT(" at %.2f s into the window: drawn rotation "
                            "changed by %.9e rad"), tau - start, gap));
                    }
                    previous = rotation;
                }
            }
        }
        NpcHullFlipReport(*this, TEXT("-X lane: drawn rotation continuous through the flip"), failures);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipHeldTest, "SOLTest.Npc.HullFlip.BasisHeldOutsideWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Clauses 1 and 5: on every test lane and ship the basis is orthonormal, its forward bit-identical to LaneHullForward
// at 400 times over the cycle; on a burning leg the forward is the travel direction before the window and against it
// after, the drawn top on the outbound leg before the window is the ship's own top LaneShipTopVector(key, +Axis, Up)
// (the hull carries its roll), and the top is constant from the leg's start to the window and from the window's end through the brake, the
// U-turn and the next leg's burn (the whole attitude held, clause 5); the basis is periodic over the cycle
bool FSOLNpcHullFlipHeldTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double tf = SOL::NPC_LANE_FLIP_S;
    const TArray<FNpcHullFlipLane> lanes = NpcHullFlipLanes();
    for (int32 laneIndex = 0; laneIndex < lanes.Num(); ++laneIndex)
    {
        const FNpcHullFlipLane& lane = lanes[laneIndex];
        FNpcHullFlipFailures failures;
        int32 burning = 0;
        for (const FNpcHullFlipShip& ship : NpcHullFlipShips(lane, laneIndex))
        {
            const double cycleS = ship.Cycle.CycleS;

            // Orthonormal, forward bit-identical to LaneHullForward, periodic
            for (int32 index = 0; index < 400; ++index)
            {
                const double tau = cycleS * (static_cast<double>(index) + 0.37) / 400.0;
                const double t = NpcHullFlipTimeAt(ship, tau);
                const FSOLLaneHullBasis basis = LaneHullBasis(lane.Profile, lane.Frame, ship.Ship, ship.Key, t);
                const FSOLLaneHullBasis later = LaneHullBasis(lane.Profile, lane.Frame, ship.Ship, ship.Key,
                    t + cycleS);
                if (!NpcHullFlipOrthonormal(basis, 1.0e-9))
                {
                    failures.Add(ship.Name + FString::Printf(TEXT(" at tau %.6e: basis not orthonormal"), tau));
                }
                else if (!(basis.Forward == LaneHullForward(lane.Profile, lane.Frame, ship.Ship, ship.Key, t)))
                {
                    failures.Add(ship.Name + FString::Printf(TEXT(" at tau %.6e: forward not LaneHullForward"), tau));
                }
                else if (NpcHullFlipBasisGap(basis, later) > 1.0e-7)
                {
                    failures.Add(ship.Name + FString::Printf(TEXT(" at tau %.6e: not periodic (%.3e rad)"), tau,
                        NpcHullFlipBasisGap(basis, later)));
                }
            }
            if (!ship.bBurns)
            {
                continue;
            }
            ++burning;

            // Each leg: travel and a constant top before the window; against travel and the attitude held from the
            // window's end through the brake, the turn after the leg and the next leg's burn
            for (const bool bOutbound : { true, false })
            {
                const double legStart = bOutbound ? 0.0 : ship.LegS + ship.Cycle.TurnS;
                const double legEnd = legStart + ship.LegS;
                const double turnEnd = legEnd + ship.Cycle.TurnS;
                const double start = legStart + 0.5 * (ship.LegS - tf);
                const double end = start + tf;
                const FVector3d travel = bOutbound ? lane.Frame.Axis : FVector3d(-lane.Frame.Axis);
                const FString where = ship.Name + (bOutbound ? TEXT(", outbound") : TEXT(", inbound"));

                const FSOLLaneHullBasis before = NpcHullFlipBasisAt(lane, ship, legStart + 1.0);
                const FVector3d ownTop = LaneShipTopVector(ship.Key, lane.Frame.Axis, lane.Frame.Up);
                if (bOutbound && (before.Top - ownTop).GetAbsMax() > 1.0e-9)
                {
                    failures.Add(where + FString::Printf(TEXT(": drawn top (%.9f, %.9f, %.9f) is not the ship's own top "
                        "(%.9f, %.9f, %.9f) before the flip"), before.Top.X, before.Top.Y, before.Top.Z, ownTop.X,
                        ownTop.Y, ownTop.Z));
                }
                for (const double tau : { legStart + 1.0, legStart + 0.5 * ship.Ship.AccelS, start - 1.0 })
                {
                    const FSOLLaneHullBasis basis = NpcHullFlipBasisAt(lane, ship, tau);
                    if ((basis.Forward - travel).GetAbsMax() > 1.0e-12 || (basis.Top - before.Top).GetAbsMax() > 1.0e-9)
                    {
                        failures.Add(where + FString::Printf(TEXT(" %.3f s into the leg: not travel with the leg's "
                            "constant top before the window"), tau - legStart));
                    }
                }

                const FSOLLaneHullBasis held = NpcHullFlipBasisAt(lane, ship, end + 1.0);
                const double nextBurnEnd = FMath::Fmod(turnEnd + ship.Ship.AccelS, ship.Cycle.CycleS);
                for (const double tau : { end + 1.0, legEnd - 0.5 * ship.Ship.AccelS, legEnd - 1.0,
                    legEnd + 0.25 * ship.Cycle.TurnS, legEnd + 0.5 * ship.Cycle.TurnS, legEnd + 0.75 * ship.Cycle.TurnS,
                    turnEnd - 1.0e-3, FMath::Fmod(turnEnd + 1.0, ship.Cycle.CycleS), nextBurnEnd - 1.0 })
                {
                    const FSOLLaneHullBasis basis = NpcHullFlipBasisAt(lane, ship, tau);
                    if ((basis.Forward + travel).GetAbsMax() > 1.0e-9 || (basis.Top - held.Top).GetAbsMax() > 1.0e-9)
                    {
                        failures.Add(where + FString::Printf(TEXT(" %.3f s after the window: attitude not held "
                            "(forward (%.9f, %.9f, %.9f), top moved %.3e rad)"), tau - end, basis.Forward.X,
                            basis.Forward.Y, basis.Forward.Z, NpcHullFlipAngle(basis.Top, held.Top)));
                    }
                }
            }
        }
        TestTrue(lane.Name + TEXT(": the sample has burning ships"), burning >= 5);
        NpcHullFlipReport(*this, lane.Name + TEXT(": basis orthonormal, forward LaneHullForward, attitude held, "
            "periodic"), failures);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipRigidTest, "SOLTest.Npc.HullFlip.FlipRigidHalfTurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Clause 2: in each flip window the hull turns rigidly through exactly pi about a fixed axis A perpendicular to the
// heading: A = unit(F(start) x F(middle)) is perpendicular to the travel direction, F . A = 0 and Top . A constant at
// every sample, angle(d, F) = pi f, F(end) = -F(start), Top(end) = 2 (Top0 . A) A - Top0; at 0.25 s steps from 5 s
// before to 5 s after the window the forward and the top turn by at most the smoothstep's peak rate 1.5 pi / T_f x dt
bool FSOLNpcHullFlipRigidTest::RunTest(const FString& /*parameters*/)
{
    const double tf = SOL::NPC_LANE_FLIP_S;
    const double peakRate = 1.5 * NPCHULLFLIP_PI / tf;
    const double dt = 0.25;
    const TArray<FNpcHullFlipLane> lanes = NpcHullFlipLanes();
    for (int32 laneIndex = 0; laneIndex < lanes.Num(); ++laneIndex)
    {
        const FNpcHullFlipLane& lane = lanes[laneIndex];
        FNpcHullFlipFailures failures;
        for (const FNpcHullFlipShip& ship : NpcHullFlipShips(lane, laneIndex))
        {
            if (!ship.bBurns)
            {
                continue;
            }
            for (const bool bOutbound : { true, false })
            {
                const double legStart = bOutbound ? 0.0 : ship.LegS + ship.Cycle.TurnS;
                const double start = legStart + 0.5 * (ship.LegS - tf);
                const double end = start + tf;
                const FVector3d travel = bOutbound ? lane.Frame.Axis : FVector3d(-lane.Frame.Axis);
                const FString where = ship.Name + (bOutbound ? TEXT(", outbound") : TEXT(", inbound"));

                // The pitch axis from the window's start and middle
                const FSOLLaneHullBasis first = NpcHullFlipBasisAt(lane, ship, start);
                const FSOLLaneHullBasis middle = NpcHullFlipBasisAt(lane, ship, legStart + 0.5 * ship.LegS);
                const FSOLLaneHullBasis last = NpcHullFlipBasisAt(lane, ship, end);
                if (FMath::Abs(FVector3d::DotProduct(middle.Forward, travel)) > 1.0e-6)
                {
                    failures.Add(where + TEXT(": the nose is not across the lane at the window's middle"));
                    continue;
                }
                const FVector3d axis = FVector3d::CrossProduct(first.Forward, middle.Forward).GetSafeNormal();
                const double topAlongAxis = FVector3d::DotProduct(first.Top, axis);
                if (FMath::Abs(FVector3d::DotProduct(axis, travel)) > 1.0e-9)
                {
                    failures.Add(where + TEXT(": the pitch axis is not perpendicular to the heading"));
                }

                // Rigid about the axis, the nose at pi f from travel, the step bound
                FSOLLaneHullBasis previous = NpcHullFlipBasisAt(lane, ship, start - 5.0);
                for (int32 index = 1; index <= 280; ++index)
                {
                    const double tau = start - 5.0 + dt * static_cast<double>(index);
                    const FSOLLaneHullBasis basis = NpcHullFlipBasisAt(lane, ship, tau);
                    const double f = NpcHullFlipRefFraction(ship.LegS, ship.Ship.AccelS, tau - legStart);
                    const double forwardStep = NpcHullFlipAngle(previous.Forward, basis.Forward);
                    const double topStep = NpcHullFlipAngle(previous.Top, basis.Top);
                    if (FMath::Abs(FVector3d::DotProduct(basis.Forward, axis)) > 1.0e-9
                        || FMath::Abs(FVector3d::DotProduct(basis.Top, axis) - topAlongAxis) > 1.0e-9)
                    {
                        failures.Add(where + FString::Printf(TEXT(" at %.2f s: not a rigid turn about the pitch axis "
                            "(F . A %.3e, Top . A %.9f vs %.9f)"), tau - start, FVector3d::DotProduct(basis.Forward,
                            axis), FVector3d::DotProduct(basis.Top, axis), topAlongAxis));
                    }
                    if (FMath::Abs(NpcHullFlipAngle(travel, basis.Forward) - NPCHULLFLIP_PI * f) > 1.0e-8)
                    {
                        failures.Add(where + FString::Printf(TEXT(" at %.2f s: nose %.9f rad from travel, expected "
                            "pi f %.9f"), tau - start, NpcHullFlipAngle(travel, basis.Forward), NPCHULLFLIP_PI * f));
                    }
                    if (forwardStep > peakRate * dt * (1.0 + 1.0e-6) + 1.0e-9
                        || topStep > peakRate * dt * (1.0 + 1.0e-6) + 1.0e-9)
                    {
                        failures.Add(where + FString::Printf(TEXT(" at %.2f s: step of %.9e (forward), %.9e (top) rad "
                            "over %.2f s"), tau - start, forwardStep, topStep, dt));
                    }
                    previous = basis;
                }

                // A half turn: the forward and the top at the window's end are the start's turned by pi about A
                const FVector3d expectedTop = 2.0 * topAlongAxis * axis - first.Top;
                if ((last.Forward + first.Forward).GetAbsMax() > 1.0e-9 || (last.Top - expectedTop).GetAbsMax() > 1.0e-9)
                {
                    failures.Add(where + TEXT(": the window's end is not the start turned by pi about the pitch axis"));
                }
            }
        }
        NpcHullFlipReport(*this, lane.Name + TEXT(": the flip is a smooth rigid half turn about an axis across the "
            "heading"), failures);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipSideTest, "SOLTest.Npc.HullFlip.FlipSide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Clause 3: LaneFlipSide(key) is +1 or -1, the same on every call (a pure function of the key, nothing stored), and
// both sides occur over keys 0-999 and over 1000 keys 65,536 apart; on every outbound leg the nose at the window's
// middle is side x T, T = LaneShipTopVector(key, +Axis, Up) (the ship's own top or its bottom), both through
// LaneHullBasis and LaneHullForward; the lane samples hold ships of both sides
bool FSOLNpcHullFlipSideTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;

    for (const uint32 stride : { 1u, 65536u })
    {
        int32 plus = 0;
        int32 minus = 0;
        int32 bad = 0;
        for (uint32 index = 0; index < 1000; ++index)
        {
            const uint32 key = index * stride;
            const int32 side = LaneFlipSide(key);
            plus += side == 1 ? 1 : 0;
            minus += side == -1 ? 1 : 0;
            bad += (side != 1 && side != -1) || side != LaneFlipSide(key) ? 1 : 0;
        }
        TestEqual(FString::Printf(TEXT("keys %u apart: every side +-1 and repeatable"), stride), bad, 0);
        TestTrue(FString::Printf(TEXT("keys %u apart: both sides occur (%d +1, %d -1)"), stride, plus, minus),
            plus > 0 && minus > 0);
    }
    for (const uint32 key : { 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu })
    {
        const int32 side = LaneFlipSide(key);
        TestTrue(FString::Printf(TEXT("key %u: side +-1"), key), side == 1 || side == -1);
    }

    // The outbound nose at the window's middle is side x the ship's own top
    const TArray<FNpcHullFlipLane> lanes = NpcHullFlipLanes();
    for (int32 laneIndex = 0; laneIndex < lanes.Num(); ++laneIndex)
    {
        const FNpcHullFlipLane& lane = lanes[laneIndex];
        FNpcHullFlipFailures failures;
        int32 sides[2] = {};
        for (const FNpcHullFlipShip& ship : NpcHullFlipShips(lane, laneIndex))
        {
            if (!ship.bBurns)
            {
                continue;
            }
            const int32 side = LaneFlipSide(ship.Key);
            ++sides[side > 0 ? 0 : 1];
            const FVector3d expected = static_cast<double>(side)
                * LaneShipTopVector(ship.Key, lane.Frame.Axis, lane.Frame.Up);
            const double t = NpcHullFlipTimeAt(ship, 0.5 * ship.LegS);
            const FSOLLaneHullBasis basis = LaneHullBasis(lane.Profile, lane.Frame, ship.Ship, ship.Key, t);
            const FVector3d hull = LaneHullForward(lane.Profile, lane.Frame, ship.Ship, ship.Key, t);
            if ((basis.Forward - expected).GetAbsMax() > 1.0e-6 || (hull - expected).GetAbsMax() > 1.0e-6)
            {
                failures.Add(ship.Name + FString::Printf(TEXT(": mid-flip nose (%.9f, %.9f, %.9f), expected side %d x "
                    "T (%.9f, %.9f, %.9f)"), basis.Forward.X, basis.Forward.Y, basis.Forward.Z, side, expected.X,
                    expected.Y, expected.Z));
            }
        }
        TestTrue(lane.Name + FString::Printf(TEXT(": the sample holds both sides (%d +1, %d -1)"), sides[0], sides[1]),
            sides[0] > 0 && sides[1] > 0);
        NpcHullFlipReport(*this, lane.Name + TEXT(": outbound mid-flip nose = side x T"), failures);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipContinuityTest, "SOLTest.Npc.HullFlip.ContinuousOverCycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Clauses 1-3: no snap anywhere: over a whole cycle (64 spans scanned by recursive bisection down to 1e-4 s with a
// 1e-3 rad threshold) neither the forward nor the top of the drawn basis jumps, for burning ships of both flip sides
// and for a ship with no brake (velocity facing in the turns), on every test lane
bool FSOLNpcHullFlipContinuityTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FNpcHullFlipLane> lanes = NpcHullFlipLanes();
    for (int32 laneIndex = 0; laneIndex < lanes.Num(); ++laneIndex)
    {
        const FNpcHullFlipLane& lane = lanes[laneIndex];
        FNpcHullFlipFailures failures;
        for (const FNpcHullFlipShip& ship : NpcHullFlipShips(lane, laneIndex))
        {
            constexpr int32 spanCount = 64;
            int32 budget = NPCHULLFLIP_SCAN_BUDGET;
            double previousTau = 0.0;
            FSOLLaneHullBasis previous = NpcHullFlipBasisAt(lane, ship, previousTau);
            for (int32 span = 1; span <= spanCount; ++span)
            {
                const double tau = ship.Cycle.CycleS * static_cast<double>(span) / spanCount;
                const FSOLLaneHullBasis basis = NpcHullFlipBasisAt(lane, ship, tau);
                double jumpTau = 0.0;
                if (!NpcHullFlipScan(lane, ship, previousTau, previous, tau, basis, budget, jumpTau))
                {
                    failures.Add(ship.Name + (budget <= 0 ? FString(TEXT(": scan budget ran out"))
                        : FString::Printf(TEXT(": the basis jumps at cycle time %.6f s (leg %.6f s, turn %.6f s)"),
                            jumpTau, ship.LegS, ship.Cycle.TurnS)));
                    break;
                }
                previousTau = tau;
                previous = basis;
            }
        }
        NpcHullFlipReport(*this, lane.Name + TEXT(": no snap over the cycle"), failures);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipAccelTest, "SOLTest.Npc.HullFlip.FlipEndAccel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Clause 4: LaneFlipEndAccelMps2(L, T_f) = (1.5 pi / T_f)^2 x L / 2 (default T_f = NPC_LANE_FLIP_S), 0 for a bad
// length or window; LaneFlipEndAccelLimitMps2 in (0, 1 G] for any input, non-increasing from 1 m to 20 km and lower at
// 10 km than at 10 m; a 2 km ship and the 30 m placeholder hull are under their limits; the peak turn rate measured
// from LaneHullBasis is 1.5 pi / T_f within 1 %
bool FSOLNpcHullFlipAccelTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double tf = SOL::NPC_LANE_FLIP_S;

    // The formula, with the default and an explicit window
    for (const double lengthM : { 1.0, 30.0, 500.0, 2000.0, 3000.0, 1.0e4 })
    {
        for (const double flipS : { tf, 120.0 })
        {
            const double rate = 1.5 * NPCHULLFLIP_PI / flipS;
            const double expected = rate * rate * 0.5 * lengthM;
            const double actual = LaneFlipEndAccelMps2(lengthM, flipS);
            TestTrue(FString::Printf(TEXT("%.0f m ship, %.0f s flip: %.12e m/s^2, expected %.12e"), lengthM, flipS,
                actual, expected), FMath::Abs(actual - expected) <= 1.0e-12 * expected);
        }
        TestTrue(FString::Printf(TEXT("%.0f m ship: the default window is NPC_LANE_FLIP_S"), lengthM),
            LaneFlipEndAccelMps2(lengthM) == LaneFlipEndAccelMps2(lengthM, tf));
    }
    for (const double badLength : { 0.0, -5.0, NPCHULLFLIP_NAN, NPCHULLFLIP_INF })
    {
        TestTrue(FString::Printf(TEXT("length %f: 0"), badLength), LaneFlipEndAccelMps2(badLength, tf) == 0.0);
    }
    for (const double badFlip : { 0.0, -1.0, NPCHULLFLIP_NAN, NPCHULLFLIP_INF })
    {
        TestTrue(FString::Printf(TEXT("flip window %f: 0"), badFlip), LaneFlipEndAccelMps2(100.0, badFlip) == 0.0);
    }

    // The limit: at most 1 G, positive, non-increasing with length, lower for a big ship
    {
        FNpcHullFlipFailures failures;
        double previous = LaneFlipEndAccelLimitMps2(1.0);
        for (int32 index = 0; index <= 60; ++index)
        {
            const double lengthM = FMath::Pow(10.0, 0.0 + 4.30103 * static_cast<double>(index) / 60.0);
            const double limit = LaneFlipEndAccelLimitMps2(lengthM);
            if (!FMath::IsFinite(limit) || limit <= 0.0 || limit > NPCHULLFLIP_G * (1.0 + 1.0e-12)
                || limit > previous * (1.0 + 1.0e-12))
            {
                failures.Add(FString::Printf(TEXT("%.3f m: limit %.9e (previous %.9e)"), lengthM, limit, previous));
            }
            previous = limit;
        }
        NpcHullFlipReport(*this, TEXT("limit in (0, 1 G], non-increasing with length"), failures);
        TestTrue(TEXT("a 10 km ship's limit is below a 10 m ship's"),
            LaneFlipEndAccelLimitMps2(1.0e4) < LaneFlipEndAccelLimitMps2(10.0));
        for (const double badLength : { 0.0, -5.0, NPCHULLFLIP_NAN, NPCHULLFLIP_INF })
        {
            const double limit = LaneFlipEndAccelLimitMps2(badLength);
            TestTrue(FString::Printf(TEXT("length %f: limit %.6e in (0, 1 G]"), badLength, limit),
                FMath::IsFinite(limit) && limit > 0.0 && limit <= NPCHULLFLIP_G * (1.0 + 1.0e-12));
        }
    }

    // A 2 km ship and the placeholder hull flip under their limits (the 2 km figure is about 0.63 G)
    for (const double lengthM : { 2000.0, SOL::NPC_HULL_LENGTH_M })
    {
        const double accel = LaneFlipEndAccelMps2(lengthM);
        const double limit = LaneFlipEndAccelLimitMps2(lengthM);
        TestTrue(FString::Printf(TEXT("%.0f m ship: flip end acceleration %.6f m/s^2 (%.4f G) under its limit %.6f "
            "m/s^2"), lengthM, accel, accel / NPCHULLFLIP_G, limit), accel > 0.0 && accel < limit);
    }

    // The figure is the truth: the peak turn rate of LaneHullBasis's forward over a window is 1.5 pi / T_f
    {
        const TArray<FNpcHullFlipLane> lanes = NpcHullFlipLanes();
        const FNpcHullFlipLane& lane = lanes[1];
        const double dt = 0.05;
        int32 measured = 0;
        for (const FNpcHullFlipShip& ship : NpcHullFlipShips(lane, 1))
        {
            if (!ship.bBurns)
            {
                continue;
            }
            const double start = 0.5 * (ship.LegS - tf);
            double fastest = 0.0;
            FSOLLaneHullBasis previous = NpcHullFlipBasisAt(lane, ship, start);
            for (int32 index = 1; index <= static_cast<int32>(tf / dt); ++index)
            {
                const FSOLLaneHullBasis basis = NpcHullFlipBasisAt(lane, ship, start + dt * static_cast<double>(index));
                fastest = FMath::Max(fastest, NpcHullFlipAngle(previous.Forward, basis.Forward) / dt);
                previous = basis;
            }
            const double expectedRate = 1.5 * NPCHULLFLIP_PI / tf;
            TestTrue(ship.Name + FString::Printf(TEXT(": peak turn rate %.9f rad/s within 1 %% of %.9f"), fastest,
                expectedRate), FMath::Abs(fastest - expectedRate) <= 0.01 * expectedRate);
            TestTrue(ship.Name + TEXT(": the 2 km figure matches the measured rate (2 %)"),
                FMath::Abs(fastest * fastest * 1000.0 - LaneFlipEndAccelMps2(2000.0))
                <= 0.02 * LaneFlipEndAccelMps2(2000.0));
            ++measured;
        }
        TestTrue(TEXT("rate measured on burning ships"), measured > 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipDurationTest, "SOLTest.Npc.HullFlip.FlipDuration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Decision 2 (user, 2026-11-18): LaneFlipDurationS(L) = max(NPC_LANE_FLIP_S, 1.5 pi sqrt(L / (2 limit(L)))): never
// below 60 s; exactly 60 s for 10 m, the 30 m placeholder hull, 500 m, 2 km and bad lengths; non-decreasing from 10 m to
// 20 km, strictly increasing where above 60 s, above 60 s at 20 km; the end acceleration at the returned window never
// above the limit and equal to it where the window is above 60 s (the shortest window)
bool FSOLNpcHullFlipDurationTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const double tf = SOL::NPC_LANE_FLIP_S;

    // Normal ships and bad lengths keep the 60 s window exactly
    for (const double lengthM : { 10.0, SOL::NPC_HULL_LENGTH_M, 500.0, 2000.0 })
    {
        TestTrue(FString::Printf(TEXT("%.0f m ship: window %.12f s is NPC_LANE_FLIP_S"), lengthM,
            LaneFlipDurationS(lengthM)), LaneFlipDurationS(lengthM) == tf);
    }
    for (const double badLength : { 0.0, -5.0, NPCHULLFLIP_NAN, NPCHULLFLIP_INF })
    {
        TestTrue(FString::Printf(TEXT("length %f: window NPC_LANE_FLIP_S"), badLength),
            LaneFlipDurationS(badLength) == tf);
    }

    // 10 m to 20 km: at least 60 s, the closed form, monotone, the end acceleration at or under the limit (tight above
    // 60 s)
    FNpcHullFlipFailures failures;
    double previous = 0.0;
    int32 grown = 0;
    for (int32 index = 0; index <= 120; ++index)
    {
        const double lengthM = 10.0 * FMath::Pow(2000.0, static_cast<double>(index) / 120.0);
        const double duration = LaneFlipDurationS(lengthM);
        const double limit = LaneFlipEndAccelLimitMps2(lengthM);
        const double accel = LaneFlipEndAccelMps2(lengthM, duration);
        const double closedForm = FMath::Max(tf, 1.5 * NPCHULLFLIP_PI * FMath::Sqrt(lengthM / (2.0 * limit)));
        const FString where = FString::Printf(TEXT("%.3f m: window %.9f s, end %.9e m/s^2, limit %.9e m/s^2"), lengthM,
            duration, accel, limit);
        if (!FMath::IsFinite(duration) || duration < tf)
        {
            failures.Add(where + TEXT(": below NPC_LANE_FLIP_S or not finite"));
        }
        if (FMath::Abs(duration - closedForm) > 1.0e-12 * closedForm)
        {
            failures.Add(where + FString::Printf(TEXT(": not the closed form %.12f s"), closedForm));
        }
        if (!(accel <= limit * (1.0 + 1.0e-9)))
        {
            failures.Add(where + TEXT(": the end acceleration exceeds the limit"));
        }
        if (duration > tf)
        {
            ++grown;
            if (FMath::Abs(accel - limit) > 1.0e-9 * limit)
            {
                failures.Add(where + TEXT(": grown but not the shortest window (end acceleration below the limit)"));
            }
            if (previous > tf && !(duration > previous))
            {
                failures.Add(where + FString::Printf(TEXT(": grown but not longer than the previous %.9f s"), previous));
            }
        }
        if (duration < previous)
        {
            failures.Add(where + FString::Printf(TEXT(": shorter than the previous %.9f s"), previous));
        }
        previous = duration;
    }
    NpcHullFlipReport(*this, TEXT("flip window from 10 m to 20 km"), failures);
    TestTrue(FString::Printf(TEXT("20 km ship: window %.3f s above 60 s (a 60 s flip would be %.2f G)"),
        LaneFlipDurationS(2.0e4), LaneFlipEndAccelMps2(2.0e4, tf) / NPCHULLFLIP_G), LaneFlipDurationS(2.0e4) > tf);
    TestTrue(FString::Printf(TEXT("the window grows for the longest ships (%d of 121 samples above 60 s)"), grown),
        grown > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipBadLaneTest, "SOLTest.Npc.HullFlip.BadLanes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// For an invalid frame (a default one, and A == B) and a non-drivable profile the basis is finite and orthonormal and
// its forward bit-identical to LaneHullForward, at several times and keys
bool FSOLNpcHullFlipBadLaneTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const TArray<FNpcHullFlipLane> lanes = NpcHullFlipLanes();
    const FNpcHullFlipLane& good = lanes[1];
    const FSOLLaneShip ship = MakeLaneShip(good.Profile, 0.3, 1.0, 0.4, 1.0);

    struct FBadCase
    {
        const TCHAR* Name;
        FSOLLaneProfile Profile;
        FSOLLaneFrame Frame;
    };
    const FVector3d aM(1.0e9, 2.0e9, 3.0e6);
    const FBadCase cases[] = {
        { TEXT("default frame"), good.Profile, FSOLLaneFrame() },
        { TEXT("A == B"), good.Profile, MakeLaneFrame(good.Profile, aM, aM) },
        { TEXT("non-drivable profile"), FSOLLaneProfile(), good.Frame },
    };
    for (const FBadCase& badCase : cases)
    {
        FNpcHullFlipFailures failures;
        for (const uint32 key : { 0u, 17u, 0xFFFFFFFFu })
        {
            for (const double t : { 0.0, 1.0e3, 1.0e6, -5.0e5 })
            {
                const FSOLLaneHullBasis basis = LaneHullBasis(badCase.Profile, badCase.Frame, ship, key, t);
                if (!NpcHullFlipOrthonormal(basis, 1.0e-9)
                    || !(basis.Forward == LaneHullForward(badCase.Profile, badCase.Frame, ship, key, t)))
                {
                    failures.Add(FString::Printf(TEXT("key %u, t %.1f: forward (%f, %f, %f), top (%f, %f, %f)"), key,
                        t, basis.Forward.X, basis.Forward.Y, basis.Forward.Z, basis.Top.X, basis.Top.Y, basis.Top.Z));
                }
            }
        }
        NpcHullFlipReport(*this, FString(badCase.Name) + TEXT(": finite orthonormal basis, forward LaneHullForward"),
            failures);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipDrawRotationTest, "SOLTest.Npc.HullFlip.DrawRotation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The visuals' drawn rotation (SOLNpcAi::HullDrawRotation): faction 1's box is HullRotation itself (+X to the heading,
// +Z to the orthogonalised top), and faction 0's cone is turned a quarter turn about +Y so its mesh axis (+Z) is the
// nose, its +X the bottom and its +Y the box's; unit and finite at every heading, -X included (1e-9)
bool FSOLNpcHullFlipDrawRotationTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    TArray<FVector3d> headings = { -FVector3d::UnitX(), FVector3d::UnitX(), FVector3d::UnitY(), -FVector3d::UnitY(),
        FVector3d::UnitZ(), -FVector3d::UnitZ(), FVector3d(-1.0, 1.0e-9, 0.0) };
    for (uint64 index = 0; index < 100; ++index)
    {
        const double z = 2.0 * NpcHullFlipUnit(4 * index) - 1.0;
        const double azimuth = 2.0 * NPCHULLFLIP_PI * NpcHullFlipUnit(4 * index + 1);
        const double radius = FMath::Sqrt(FMath::Max(0.0, 1.0 - z * z));
        headings.Add(FVector3d(radius * FMath::Cos(azimuth), radius * FMath::Sin(azimuth), z) * 3.0);
    }
    FNpcHullFlipFailures box;
    FNpcHullFlipFailures cone;
    for (int32 index = 0; index < headings.Num(); ++index)
    {
        // A top skewed toward the heading and scaled (the drawn top is the part orthogonal to the heading)
        const FVector3d heading = headings[index];
        const FVector3d unitHeading = heading.GetSafeNormal();
        const FVector3d top = NpcHullFlipPerpendicular(unitHeading) * 2.5 + unitHeading * 0.7;
        const FVector3d unitTop = (top - unitHeading * FVector3d::DotProduct(top, unitHeading)).GetSafeNormal();
        const FQuat boxRotation = SOLNpcAi::HullDrawRotation(heading, top, 1);
        const FQuat coneRotation = SOLNpcAi::HullDrawRotation(heading, top, 0);
        if (!(NpcHullFlipQuatAngle(boxRotation, HullRotation(heading, top)) <= 1.0e-9)
            || !boxRotation.RotateVector(FVector3d::UnitX()).Equals(unitHeading, 1.0e-9)
            || !boxRotation.RotateVector(FVector3d::UnitZ()).Equals(unitTop, 1.0e-9))
        {
            box.Add(FString::Printf(TEXT("heading %d (%f, %f, %f)"), index, heading.X, heading.Y, heading.Z));
        }
        if (!coneRotation.IsNormalized() || coneRotation.ContainsNaN()
            || !coneRotation.RotateVector(FVector3d::UnitZ()).Equals(unitHeading, 1.0e-9)
            || !coneRotation.RotateVector(FVector3d::UnitX()).Equals(-unitTop, 1.0e-9)
            || !coneRotation.RotateVector(FVector3d::UnitY()).Equals(boxRotation.RotateVector(FVector3d::UnitY()),
                1.0e-9))
        {
            cone.Add(FString::Printf(TEXT("heading %d (%f, %f, %f)"), index, heading.X, heading.Y, heading.Z));
        }
    }
    NpcHullFlipReport(*this, TEXT("faction 1: the box is HullRotation (+X heading, +Z top)"), box);
    NpcHullFlipReport(*this, TEXT("faction 0: the cone's axis (+Z) is the nose, a quarter turn about +Y"), cone);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcHullFlipTierTopTest, "SOLTest.Npc.HullFlip.TierTopAndPromotion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The top each CPU tier draws (SDD 50 section 16, user decision 2026-11-18): LaneShipMotion carries LaneHullBasis's
// attitude bit for bit; SOLNpcRail::HullUpHint gives a lane ship's lane top (Mid drawn top, Full hand-off and steering
// up) and a non-lane ship's radial hint unchanged; and a promotion keeps the drawn rotation: the Mid hull drawn from
// (HullForward of the heading velocity, HullUpHint) and the Full hull drawn from the HandoffOrientation of the same
// inputs (its forward and up, as USOLNpcTierProcessor collects them) agree within 1e-6 rad for both factions, at cycle
// times before, inside and after the flip window and in the turns, for lane ships of both flip sides and a ship that
// never brakes, with a deviation velocity, and for a non-lane rail
bool FSOLNpcHullFlipTierTopTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneTraffic;
    const TArray<FNpcHullFlipLane> lanes = NpcHullFlipLanes();
    const FVector3d deviationMps(12.0, -7.0, 4.0);
    FNpcHullFlipFailures motionFailures;
    FNpcHullFlipFailures hintFailures;
    FNpcHullFlipFailures promotionFailures;
    int32 samples = 0;

    // Returns the angle between the Mid-drawn and the promoted Full-drawn rotation of a motion and radial hint
    const auto promotionGap = [&deviationMps](const FSOLNpcRailMotion& motion, const FVector3d& radialM,
        const int32 faction)
    {
        const FVector3d upHint = SOLNpcRail::HullUpHint(motion, radialM);
        const FVector3d headingVelocity = SOLNpcRail::HullHeadingVelocityMps(motion, deviationMps);
        const FQuat mid = SOLNpcAi::HullDrawRotation(SOLNpcAi::HullForward(headingVelocity, radialM),
            upHint.GetSafeNormal(), faction);
        const FQuat4d attitude = SOLNpcAi::HandoffOrientation(headingVelocity, upHint);
        const FQuat full = SOLNpcAi::HullDrawRotation(attitude.GetForwardVector(), attitude.GetUpVector(), faction);
        return NpcHullFlipQuatAngle(mid, full);
    };
    for (int32 laneIndex = 0; laneIndex < lanes.Num(); ++laneIndex)
    {
        const FNpcHullFlipLane& lane = lanes[laneIndex];
        const FVector3d radialM = FVector3d(3.0e7, -1.0e7, 2.0e7) * (1.0 + laneIndex);
        for (const FNpcHullFlipShip& ship : NpcHullFlipShips(lane, laneIndex))
        {
            // Cycle times: each leg's start, window start, middle and end, past the window, and the turns' middles
            const double windowStart = 0.5 * (ship.LegS - SOL::NPC_LANE_FLIP_S);
            TArray<double, TInlineAllocator<16>> taus;
            for (const double legStart : { 0.0, ship.LegS + ship.Cycle.TurnS })
            {
                for (const double tl : { 1.0, windowStart + 0.5, 0.5 * ship.LegS, windowStart
                    + SOL::NPC_LANE_FLIP_S - 0.5, ship.LegS - 1.0 })
                {
                    taus.Add(legStart + tl);
                }
            }
            taus.Add(ship.LegS + 0.5 * ship.Cycle.TurnS);
            taus.Add(ship.Cycle.CycleS - 0.5 * ship.Cycle.TurnS);
            for (const double tau : taus)
            {
                ++samples;
                const double timeS = NpcHullFlipTimeAt(ship, tau);
                const FSOLLaneShipMotion laneMotion = LaneShipMotion(lane.Profile, lane.Frame, ship.Ship, ship.Key,
                    timeS, 0.5);
                const FSOLLaneHullBasis basis = LaneHullBasis(lane.Profile, lane.Frame, ship.Ship, ship.Key, timeS);
                const FString label = ship.Name + FString::Printf(TEXT(", tau %.2f"), tau);
                if (!(laneMotion.HullForward == basis.Forward) || !(laneMotion.HullTop == basis.Top))
                {
                    motionFailures.Add(label);
                }
                FSOLNpcRailMotion motion;
                motion.VelocityMps = laneMotion.VelocityMps;
                motion.LaneHullForward = laneMotion.HullForward;
                motion.LaneHullTop = laneMotion.HullTop;
                if (!(SOLNpcRail::HullUpHint(motion, radialM) == basis.Top))
                {
                    hintFailures.Add(label + TEXT(": the up hint is not the lane top"));
                }
                for (const int32 faction : { 0, 1 })
                {
                    const double gap = promotionGap(motion, radialM, faction);
                    if (!(gap <= 1.0e-6))
                    {
                        promotionFailures.Add(label + FString::Printf(TEXT(", faction %d: %.3g rad"), faction, gap));
                    }
                }
            }
        }
    }
    NpcHullFlipReport(*this, TEXT("LaneShipMotion carries LaneHullBasis's forward and top bit for bit"),
        motionFailures);
    NpcHullFlipReport(*this, TEXT("a lane ship's up hint (Mid top, Full hand-off) is its lane top"), hintFailures);
    NpcHullFlipReport(*this, TEXT("promotion keeps a lane hull's drawn rotation (1e-6 rad)"), promotionFailures);
    TestTrue(FString::Printf(TEXT("lane samples were taken (%d)"), samples), samples > 100);

    // A non-lane rail (no lane facing or top): the radial hint unchanged, and promotion keeps the drawn rotation
    FSOLNpcRailMotion cluster;
    cluster.VelocityMps = FVector3d(150.0, 40.0, -20.0);
    const FVector3d radialM(-4.0e8, 2.0e8, 5.0e7);
    TestTrue(TEXT("a non-lane rail's up hint is the radial hint unchanged"),
        SOLNpcRail::HullUpHint(cluster, radialM) == radialM);
    for (const int32 faction : { 0, 1 })
    {
        const double gap = promotionGap(cluster, radialM, faction);
        TestTrue(FString::Printf(TEXT("a non-lane promotion keeps the drawn rotation, faction %d (%.3g rad)"), faction,
            gap), gap <= 1.0e-6);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
