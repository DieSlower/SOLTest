/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// A lane's orthonormal frame (SDD 49 section 4.3, moved here by SDD 50 section 4.10), universe ecliptic directions:
// the outbound axis A -> B, up (12 o'clock) from the ecliptic north pole projected off the axis, right (3 o'clock, the
// outbound side) = axis x up
struct FSOLLaneBasis
{
    FVector3d Axis = FVector3d::ZeroVector;     // Outbound direction A -> B
    FVector3d Up = FVector3d::ZeroVector;       // 12 o'clock
    FVector3d Right = FVector3d::ZeroVector;    // 3 o'clock (outbound half of the tube)
    bool bValid = false;                        // False (and zero vectors) for A == B or a non-finite input
};

// One ship's flip-and-burn leg over a nominal leg l0 (SDD 50 sections 4.3 and 4.4): accelerate at Alpha from the wall
// speed w to PeakMps, coast, brake back to w
struct FSOLLaneLeg
{
    double Alpha = 0.0;         // Acceleration kappa x effort x a (m/s^2)
    double PeakMps = 0.0;       // Peak speed v_p = min(sqrt(w^2 + Alpha l0), c_k)
    double DvMps = 0.0;         // v_p - w, computed without cancellation
    double AccelS = 0.0;        // Accelerate (and brake) time t_a
    double AccelM = 0.0;        // Accelerate (and brake) distance d_a
    double CoastS = 0.0;        // Coast time t_c (>= 0)
    double LegS = 0.0;          // Leg time 2 t_a + t_c
    bool bValid = false;        // False (and zeros) for a non-positive or non-finite leg
};

// The four segments of a lane ship's cycle (SDD 50 section 4.4)
enum class ESOLLaneSegment : uint8
{
    Outbound,                   // Leg A -> B on the right of the axis
    TurnAtB,                    // U-turn beyond end ring B
    Inbound,                    // Leg B -> A on the left of the axis
    TurnAtA                     // U-turn beyond end ring A
};

// A cycle's times: leg, U-turn, whole cycle (2 legs + 2 turns)
struct FSOLLaneCycle
{
    double LegS = 0.0;          // T_leg
    double TurnS = 0.0;         // T_U = pi D_U / w
    double CycleS = 0.0;        // T_cyc = 2 T_leg + 2 T_U
};

// Where a ship is in its cycle at a time
struct FSOLLaneCyclePoint
{
    ESOLLaneSegment Segment = ESOLLaneSegment::Outbound;
    double Phase = 0.0;         // frac(phase + t / T_cyc) in [0, 1)
    double CycleTimeS = 0.0;    // tau in [0, T_cyc)
    double SegmentTimeS = 0.0;  // Time into the segment
};

// A lane's profile, frozen at spawn (SDD 50 section 4.2)
struct FSOLLaneProfile
{
    double WallRadiusM = 0.0;       // Rw = the #49 tunnel radius of the in-wall ship count (D13)
    double HaloOuterRadiusM = 0.0;  // H x Rw
    double NominalLengthM = 0.0;    // L0, the lane's maximum length (D16)
    double NominalLegM = 0.0;       // l0 = L0 - EndArcAM - EndArcBM
    double EndArcAM = 0.0;          // End ring A's distance from A, R_A + m Rw
    double EndArcBM = 0.0;          // End ring B's distance before B, R_B + m Rw
    double UTurnDepthM = 0.0;       // D_U beyond an end ring
    double UTurnS = 0.0;            // T_U = pi D_U / w
    double MetricZoneM = 0.0;       // Z of the metric end-zone map (section 4.5)
    bool bDrivable = false;         // False for a zero wall radius, a bad length or a nominal leg <= 0
};

// A lane's current geometry: its ends, basis and current leg l(t)
struct FSOLLaneFrame
{
    FVector3d AM = FVector3d::ZeroVector;       // Body A (universe ecliptic m)
    FVector3d BM = FVector3d::ZeroVector;       // Body B
    FVector3d Axis = FVector3d::ZeroVector;     // LaneBasis(A, B) outbound axis
    FVector3d Up = FVector3d::ZeroVector;       // LaneBasis(A, B) 12 o'clock
    FVector3d Right = FVector3d::ZeroVector;    // LaneBasis(A, B) 3 o'clock (the outbound half)
    double CurrentLegM = 0.0;                   // l = |B - A| - EndArcAM - EndArcBM, at least 1.001 Z (so >= 0)
    double EndShiftM = 0.0;                     // How far that floor lengthened l (0 on every real lane): B's end
                                                // ring and U-turn move out past B by it, so both ends agree on l
    bool bValid = false;                        // False for A == B or a non-finite input
};

// A lane ship's spawn constants (the rail's P0-P4, SDD 50 section 4.6); the leg time is derived from them and the
// lane's nominal leg (LaneShipLegS), never stored, so float-rounded constants still give a seamless leg
struct FSOLLaneShip
{
    double Phase = 0.0;             // Cycle phase in [0, 1) at time 0
    double DvMps = 0.0;             // v_p - w
    double UpOffsetM = 0.0;         // rho Rw cos psi
    double RightOffsetM = 0.0;      // rho Rw sin psi (>= 0)
    double AccelS = 0.0;            // t_a
};

// A lane ship's thrust kind (SDD 50 section 4.12, consumer #51)
enum class ESOLLaneThrust : uint8
{
    Accelerate,                     // Alpha along travel
    Coast,                          // No thrust
    Brake,                          // Alpha against travel
    Turn                            // Centripetal, toward the U-turn's centre
};

// A lane ship's thrust at a time
struct FSOLLaneThrustState
{
    ESOLLaneSegment Segment = ESOLLaneSegment::Outbound;
    ESOLLaneThrust Thrust = ESOLLaneThrust::Coast;
    double AccelMps2 = 0.0;                         // Thrust acceleration magnitude
    FVector3d Direction = FVector3d::ZeroVector;    // Unit thrust direction (travel direction while coasting)
};

// Pure math of the issue #50 lane traffic (SDD 50 sections 4.2-4.5 and 4.12, contract section 6): the radial factor,
// the per-ship leg profile, the leg arc and speed, the mean speed field, the metric end-zone map and its inverse, the
// cycle, the lane profile and frame, the lane ship position, the wake speed bound and the thrust state. Universe
// ecliptic metres and sim seconds, double. The point material's HLSL mirror (SOLNpcTraffic.ush, part 50b) must follow
// any change to the leg, map, cycle or position formulas
namespace SOLLaneTraffic
{
    // Returns the lane basis of A -> B: up = the ecliptic north pole projected off the axis, right = axis x up; within
    // NPC_TUNNEL_UP_FALLBACK_DEG of the pole up is universe +X projected off the axis, and up to
    // NPC_TUNNEL_UP_BLEND_DEG it turns smoothly about the axis from that to north; invalid for A == B or a non-finite
    // input (SOLNpcTunnel::TunnelBasis forwards here)
    SOLTEST_API FSOLLaneBasis LaneBasis(const FVector3d& aM, const FVector3d& bM);

    // Returns the radial speed factor of a ship rho wall radii off the axis: 1 - rho^2 inside the wall,
    // NPC_LANE_HALO_KAPPA x smoothstep(1, 1 + NPC_LANE_HALO_RAMP_WALL_RADII, rho) beyond it; |rho| is used, NaN gives 0
    SOLTEST_API double RadialFactor(double rho);

    // Returns a ship's leg profile: kappa clamped to [0, 1] (NaN 0), effort clamped to [NPC_LANE_EFFORT_MIN,
    // NPC_LANE_EFFORT_MAX] (NaN the minimum); invalid with every field 0 for a non-positive or non-finite leg
    SOLTEST_API FSOLLaneLeg MakeLegProfile(double kappa, double effort, double legM);

    // Returns the arc u(tl) along a leg from its departure end ring, tl clamped to [0, LegS] (NaN 0); 0 for an invalid
    // leg
    SOLTEST_API double LegArcM(const FSOLLaneLeg& leg, double legM, double tl);

    // Returns the speed at leg time tl (clamped like LegArcM); the wall speed for an invalid leg
    SOLTEST_API double LegSpeedMps(const FSOLLaneLeg& leg, double legM, double tl);

    // Returns the speed at leg arc u, sqrt(min(w^2 + 2 Alpha min(u, l0 - u), PeakMps^2)), u clamped to [0, l0] (NaN
    // 0); the wall speed for an invalid leg
    SOLTEST_API double LegSpeedAtArcMps(const FSOLLaneLeg& leg, double legM, double u);

    // Returns the mean speed field: the speed at arc u of a mean-effort ship rho wall radii off the axis on a nominal
    // leg (u clamped to [0, l0], NaN 0); the wall speed for a bad leg
    SOLTEST_API double MeanSpeedMps(double u, double rho, double legM);

    // Returns the metric zone min(NPC_LANE_METRIC_ZONE_FRACTION x l0, l0 / 2); 0 for a non-positive or non-finite leg
    SOLTEST_API double MetricZoneM(double l0);

    // Returns the metric end-zone map X(u) = u + (l - l0) J(u) / J(l0) (u clamped to [0, l0]); symmetric, so
    // l - X(l0 - v) = X(v); an l at or below the zone Z (where X would fold over) reads as 1.001 Z, so X stays
    // strictly monotone; 0 for a non-positive or non-finite l0 or a non-finite l
    SOLTEST_API double MetricMapM(double u, double l0, double l);

    // Returns the map's slope X'(u) (1 at both ends; l read as in MetricMapM); 1 for a bad input
    SOLTEST_API double MetricMapSlope(double u, double l0, double l);

    // Returns the u with X(u) = x (x clamped to [0, l], l read as in MetricMapM), by safeguarded Newton steps from
    // the nearer end; 0 for a bad input
    SOLTEST_API double MetricMapInverseM(double x, double l0, double l);

    // Returns the cycle of a ship with leg time legS on a lane with U-turn depth uTurnDepthM (bad inputs read as 0)
    SOLTEST_API FSOLLaneCycle MakeCycle(double legS, double uTurnDepthM);

    // Returns the segment, phase and times of a ship of the given phase at time t (NaN phase and t read as 0; the
    // segments are half-open: outbound, turn at B, inbound, turn at A)
    SOLTEST_API FSOLLaneCyclePoint CycleSegmentAt(const FSOLLaneCycle& cycle, double phase, double timeS);

    // Returns a lane's frozen profile from its wall radius Rw (the caller's SOLNpcTunnel::TunnelRadiusM of the lane
    // radius and in-wall count, D13), nominal length and end body radii
    SOLTEST_API FSOLLaneProfile MakeLaneProfile(double wallRadiusM, double nominalLengthM, double bodyRadiusAM,
        double bodyRadiusBM);

    // Returns a lane's current frame from its profile and its end bodies' positions; the current leg is floored at
    // 1.001 Z (the metric map folds over for l <= Z; also keeps it >= 0) with the excess in EndShiftM; invalid for
    // A == B
    SOLTEST_API FSOLLaneFrame MakeLaneFrame(const FSOLLaneProfile& profile, const FVector3d& aM, const FVector3d& bM);

    // Returns a ship's constants: phase wrapped into [0, 1) (NaN 0), rho clamped to [0, H] (NaN 0), clock angle
    // clamped to [0, pi] (NaN pi / 2), the leg from MakeLegProfile(RadialFactor(rho), effort, NominalLegM)
    SOLTEST_API FSOLLaneShip MakeLaneShip(const FSOLLaneProfile& profile, double phase, double effort, double rho,
        double clockRad);

    // Returns a ship's leg time T_leg = 2 t_a + t_c with the coast derived from the nominal leg, t_c = (l0 - 2 t_a (w +
    // dv / 2)) / (w + dv) (slightly negative allowed for rounded constants); 0 for a bad nominal leg or a non-positive
    // result. The position, thrust and cycle all use this leg time
    SOLTEST_API double LaneShipLegS(const FSOLLaneProfile& profile, const FSOLLaneShip& ship);

    // Returns a ship's position at time t relative to originM, evaluated from the nearer end of the lane so a ship
    // near either body is exact relative to it; A - origin for an invalid frame, the midpoint disc at rest for a
    // non-drivable lane
    SOLTEST_API FVector3d LaneShipPositionRelM(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FSOLLaneShip& ship, double timeS, const FVector3d& originM);

    // Returns a ship's absolute position at time t (LaneShipPositionRelM with the origin at zero)
    SOLTEST_API FVector3d LaneShipPositionM(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FSOLLaneShip& ship, double timeS);

    // Returns an upper bound on a lane ship's speed relative to body A: the lane's largest peak times the map's
    // largest slope max(1, (l - Z) / (l0 - Z)), plus the endpoint speed times (1 + halo outer radius / current leg),
    // with a 1e-6 relative allowance for rounding (the bound is reached by a coasting peak ship mid-lane, and the
    // float rail rounds dv by about 6e-8); a non-finite peak reads as NPC_LANE_SPEED_CAP_MPS
    SOLTEST_API double LaneShipSpeedBoundMps(double maxPeakMps, double nominalLegM, double currentLegM,
        double haloOuterRadiusM, double endpointRelativeSpeedMps);

    // Returns a ship's thrust at time t: Accelerate / Coast / Brake on the legs (Alpha along or against travel), Turn
    // in the U-turns ((w / D_U)^2 x the distance from the turn's centre, toward it); Coast for an invalid frame or a
    // non-drivable lane. The legs report the nominal profile only: the metric map adds X''(u) u'^2 along the lane in
    // the end zones when l != l0 (a ship marked Coast slows or speeds up there, at most about 0.6-1.4 G, below a on
    // every default lane; SDD 50 section 4.12, for #51)
    SOLTEST_API FSOLLaneThrustState LaneThrustAt(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FSOLLaneShip& ship, double timeS);
}
