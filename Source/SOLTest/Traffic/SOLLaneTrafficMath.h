/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Flight/SOLNewtonianFlight.h"
#include "SOLConstants.h"

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

// One ship's flip-and-burn leg over a nominal leg l0 (SDD 50 sections 4.3 and 4.4, D21): accelerate at Alpha from the
// wall speed w to PeakMps, coast (at least the hull flip window T_f when the leg burns), brake back to w
struct FSOLLaneLeg
{
    double Alpha = 0.0;         // Acceleration kappa x effort x a (m/s^2)
    double PeakMps = 0.0;       // Peak speed v_p = max(w, min(v_f, c_k)), v_f the peak that leaves a T_f coast
    double DvMps = 0.0;         // v_p - w, computed without cancellation
    double AccelS = 0.0;        // Accelerate (and brake) time t_a
    double AccelM = 0.0;        // Accelerate (and brake) distance d_a
    double CoastS = 0.0;        // Coast time t_c (>= 0; >= T_f when the leg burns)
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

// A lane ship's drawn hull attitude (SOLLaneTraffic::LaneHullBasis): unit Forward (the nose) and unit Top (the body's
// up), orthogonal, universe ecliptic
struct FSOLLaneHullBasis
{
    FVector3d Forward = FVector3d::UnitX();
    FVector3d Top = FVector3d::UnitZ();
};

// A lane ship's velocity and hull attitude at one time (SOLLaneTraffic::LaneShipMotion)
struct FSOLLaneShipMotion
{
    FVector3d VelocityMps = FVector3d::ZeroVector;  // Central difference of the position, m/s
    FVector3d HullForward = FVector3d::ZeroVector;  // Unit hull facing (LaneHullForward)
    FVector3d HullTop = FVector3d::ZeroVector;      // Unit body top orthogonal to it (LaneHullBasis)
};

// A lane ship's thrust kind (SDD 50 section 4.12, consumer #51)
enum class ESOLLaneThrust : uint8
{
    Accelerate,                     // Alpha along travel
    Coast,                          // No thrust
    Brake,                          // Alpha against travel
    Turn,                           // Centripetal, toward the U-turn's centre
    Flip                            // No thrust, the hull turning over mid-leg (D21; appended, nothing persists it)
};

// A lane ship's thrust at a time
struct FSOLLaneThrustState
{
    ESOLLaneSegment Segment = ESOLLaneSegment::Outbound;
    ESOLLaneThrust Thrust = ESOLLaneThrust::Coast;
    double AccelMps2 = 0.0;                         // Thrust acceleration magnitude
    FVector3d Direction = FVector3d::ZeroVector;    // Unit thrust direction (travel direction while coasting)
    double FlipFraction = 0.0;                      // Hull flip f on a leg; in a turn 1 for a braking ship, else 0
    FVector3d HullForward = FVector3d::ZeroVector;  // Unit hull facing (LaneHullForward)
};

// The merge readout's verdict on the player's along-lane speed against the traffic speed (SDD 50 section 4.8, D8)
enum class ESOLLaneMergeVerdict : uint8
{
    Matched,                        // Within NPC_LANE_MERGE_MATCH_FRACTION of the traffic speed
    TooFast,                        // Faster than that
    TooSlow                         // Slower than that (also at rest, flying backwards or crossing the lane)
};

// The merge readout's colour band (green / amber / red, D8)
enum class ESOLLaneMergeBand : uint8
{
    Green,                          // Matched
    Amber,                          // Off by at most NPC_LANE_MERGE_AMBER_FRACTION
    Red                             // Off by more
};

// A verdict on two speeds (SOLLaneTraffic::RateMerge)
struct FSOLLaneMergeRating
{
    bool bValid = false;                                    // False for a bad traffic speed or a non-finite input
    double Delta = 0.0;                                     // (v_pl - v_t) / v_t
    ESOLLaneMergeVerdict Verdict = ESOLLaneMergeVerdict::TooSlow;
    ESOLLaneMergeBand Band = ESOLLaneMergeBand::Red;
};

// Where a point lies relative to a lane (SOLLaneTraffic::LanePointOf)
struct FSOLLanePoint
{
    double AlongM = 0.0;            // (p - A) . Axis
    double LegArcM = 0.0;           // AlongM - EndArcAM: the arc from end ring A
    double FootFraction = 0.0;      // AlongM / |B - A| clamped to [0, 1]
    double Rho = 0.0;               // Lateral distance from the axis in wall radii
    double RightOffsetM = 0.0;      // (p - A) . Right (negative on the left, inbound half)
    double UpOffsetM = 0.0;         // (p - A) . Up
    bool bInLeg = false;            // 0 <= LegArcM <= CurrentLegM (between the end rings)
    bool bInHalo = false;           // In the leg and Rho <= NPC_LANE_HALO_OUTER_WALL_RADII
    bool bValid = false;            // False (flags false) for an invalid frame, a non-drivable lane or a bad point
};

// One lane as the merge readout sees it this frame: its frozen profile, current frame and end body velocities
struct FSOLLaneMergeLane
{
    FSOLLaneProfile Profile;
    FSOLLaneFrame Frame;
    FVector3d AVelocityMps = FVector3d::ZeroVector;     // Body A's velocity (universe ecliptic, sim seconds)
    FVector3d BVelocityMps = FVector3d::ZeroVector;     // Body B's velocity
    int32 BodyA = INDEX_NONE;                           // The lane table entry's end bodies (registry indices)
    int32 BodyB = INDEX_NONE;
    FSOLLaneLeg TrackLeg;                               // The mean-effort leg on the keep-right track, precomputed by
                                                        // MakeMergeLane (invalid: computed where needed)
    bool bOpenNow = true;                               // The lane is open now (LaneOpenAt over its slot window, Q30)
};

// The merge readout of one frame (SOLLaneTraffic::MergeReadout)
struct FSOLLaneMergeReadout
{
    bool bShown = false;                                    // A lane's halo holds the player
    int32 Lane = INDEX_NONE;                                // Its index in the lanes passed
    int32 DirectionSign = 1;                                // +1 outbound (A -> B), -1 inbound
    double Rho = 0.0;                                       // The player's radius in wall radii
    double TrafficSpeedMps = 0.0;                           // v_t there, sim seconds
    double PlayerSpeedMps = 0.0;                            // The player's along-lane speed relative to the lane frame
    double Delta = 0.0;                                     // (v_pl - v_t) / v_t
    ESOLLaneMergeVerdict Verdict = ESOLLaneMergeVerdict::TooSlow;
    ESOLLaneMergeBand Band = ESOLLaneMergeBand::Red;
    bool bWarpNote = false;                                 // Time warp above 1x
    FVector3d FrameVelocityMps = FVector3d::ZeroVector;     // lerp(V_A, V_B, foot fraction)
};

// The part of an AUTO path that runs along a lane (SDD 50 section 4.9, D20): leg arcs from end ring A
struct FSOLLaneAutoLeg
{
    bool bFound = false;            // The path runs at least NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII inside a halo
    int32 Lane = INDEX_NONE;        // Its index in the lanes passed
    int32 DirectionSign = 1;        // +1 outbound (A -> B), -1 inbound
    double EntryArcM = 0.0;         // Where the path enters the halo (the ship's own arc when it starts inside)
    double ExitArcM = 0.0;          // The target's arc clamped to the leg (the end ring for a body at that end)
    double OverlapM = 0.0;          // Axis length of the path inside the halo
    bool bStartsInside = false;     // The ship is in the halo (hold without an approach)
    int32 BodyA = INDEX_NONE;       // Its lane's end bodies (the latch re-check compares them)
    int32 BodyB = INDEX_NONE;
    bool bTargetInside = false;     // The target lies in the halo between the end rings
};

// What AUTO does about a lane leg (SDD 50 section 4.9 steps 2-4)
enum class ESOLLaneAutoMode : uint8
{
    None,                           // No leg, or past its exit: the normal AUTO step
    Approach,                       // Flying to the merge point
    Hold,                           // In the lane, holding the traffic speed on the keep-right track
    Left                            // Done with the lane for this order: the normal AUTO step stops at the target
                                    // and no lane is searched again (appended, transient)
};

// The per-frame re-check of a latched Approach / Hold leg (SOLLaneTraffic::CheckLaneLatch)
enum class ESOLLaneLatchCheck : uint8
{
    Keep,                           // Still valid
    Stale,                          // Its lane is gone or another lane now (a respawned table): drop it
    Release                         // Its target left the lane, or an approach no longer runs along it: leave
};

// Where one AUTO step flies to (universe ecliptic)
struct FSOLLaneStepTarget
{
    FVector3d PositionM = FVector3d::ZeroVector;
    FVector3d VelocityMps = FVector3d::ZeroVector;
    FVector3d AccelMps2 = FVector3d::ZeroVector;
};

// One substep's inputs to the lane decision (universe ecliptic)
struct FSOLLaneAutoInput
{
    FVector3d ShipM = FVector3d::ZeroVector;
    FVector3d ShipVelocityMps = FVector3d::ZeroVector;
    FVector3d TargetM = FVector3d::ZeroVector;          // The order's own target
    FVector3d TargetVelocityMps = FVector3d::ZeroVector;
    FVector3d TargetAccelMps2 = FVector3d::ZeroVector;
    double BrakeAccelMps2 = 0.0;                        // The normal autopilot's brake plan (the leave distance)
    double FlipS = 0.0;
    bool bSearch = true;                                // A lane may be searched (the first substep of a frame)
    bool bIgnoreLanes = false;                          // A War power order
};

// One substep's lane decision (SOLLaneTraffic::LaneAutoDecide)
struct FSOLLaneAutoDecision
{
    FSOLLaneAutoLeg Leg;
    ESOLLaneAutoMode Mode = ESOLLaneAutoMode::None;
    FSOLLaneStepTarget StepTarget;                      // The merge point in Approach, else the order's target
};

// The point AUTO merges at (SOLLaneTraffic::LaneMergeTarget)
struct FSOLLaneMergeTarget
{
    bool bValid = false;                                // False for a leg not found or a bad lane
    FVector3d PositionM = FVector3d::ZeroVector;        // On the keep-right track at the entry arc
    FVector3d VelocityMps = FVector3d::ZeroVector;      // The lane frame there plus d Axis v_t
    FVector3d AccelMps2 = FVector3d::ZeroVector;        // The traffic's acceleration along the lane there
    double TrafficSpeedMps = 0.0;                       // v_t on the track at the entry arc
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

    // Returns a ship's leg profile with a coast of at least flipS (T_f) on a burning leg (D21): kappa clamped to [0, 1]
    // (NaN 0), effort clamped to [NPC_LANE_EFFORT_MIN, NPC_LANE_EFFORT_MAX] (NaN the minimum), flipS negative or NaN
    // read as 0 (the pre-D21 profile); a leg of at most w T_f has no burn; invalid with every field 0 for a
    // non-positive or non-finite leg
    SOLTEST_API FSOLLaneLeg MakeLegProfile(double kappa, double effort, double legM,
        double flipS = SOL::NPC_LANE_FLIP_S);

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

    // Returns a ship's velocity (the central difference of LaneShipPositionM over +- stepS), its LaneHullForward and its
    // LaneHullBasis top (of the same key) at time t, the leg and cycle derived once (the per-frame Mid / Full hull
    // paths)
    SOLTEST_API FSOLLaneShipMotion LaneShipMotion(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FSOLLaneShip& ship, uint32 laneShipKey, double timeS, double stepS);

    // Returns an upper bound on a lane ship's speed relative to body A: the lane's largest peak times the map's
    // largest slope max(1, (l - Z) / (l0 - Z)), plus the endpoint speed times (1 + halo outer radius / current leg),
    // with a 1e-6 relative allowance for rounding (the bound is reached by a coasting peak ship mid-lane, and the
    // float rail rounds dv by about 6e-8); a non-finite peak reads as NPC_LANE_SPEED_CAP_MPS
    SOLTEST_API double LaneShipSpeedBoundMps(double maxPeakMps, double nominalLegM, double currentLegM,
        double haloOuterRadiusM, double endpointRelativeSpeedMps);

    // Returns a ship's thrust at time t: on a burning leg Accelerate while tl < t_a, Brake while tl > T_leg - t_a, Flip
    // (no thrust, travel direction) while |tl - T_leg / 2| < T_f / 2, else Coast; Turn in the U-turns ((w / D_U)^2 x
    // the distance from the turn's centre, toward it); Coast for an invalid frame or a non-drivable lane; with the
    // hull's FlipFraction and HullForward (LaneHullForward). The legs report the nominal profile only: the metric map
    // adds X''(u) u'^2 along the lane in the end zones when l != l0 (a ship marked Coast slows or speeds up there, at
    // most about 0.6-1.4 G, below a on every default lane; SDD 50 section 4.12, for #51)
    SOLTEST_API FSOLLaneThrustState LaneThrustAt(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FSOLLaneShip& ship, uint32 laneShipKey, double timeS);

    // Returns the hull flip fraction f at leg time tl (D21): the smoothstep 3 x^2 - 2 x^3 of x = (tl - (legS - T_f) /
    // 2) / T_f clamped to [0, 1], tl clamped to [0, legS]; 0 when accelS <= 0 (no brake) or any input is not finite
    SOLTEST_API double LaneHullFlipFraction(double legS, double accelS, double legTimeS);

    // Returns a lane ship's own top vector about a travel direction (Q11a): cos(theta) U + sin(theta) (D x U), D the
    // unit travel direction, U the up projected off D and normalised, theta the ship's roll, a deterministic hash of
    // the key spread over [0, 2 pi) (one roll per ship on both legs and every lane, nothing stored). An up that is
    // zero, non-finite or parallel to D falls back to a perpendicular of D chosen from D alone; a zero or non-finite D
    // gives the zero vector
    SOLTEST_API FVector3d LaneShipTopVector(uint32 laneShipKey, const FVector3d& travelDirection, const FVector3d& up);

    // Returns a ship's unit hull facing at time t (SDD 50 section 4.4, D21 as amended by section 16, Q11a): on a leg
    // cos(pi f) d + side sin(pi f) B, d the travel direction, side = LaneFlipSide(key) and B the leg's starting body
    // top, T_out = LaneShipTopVector(key, +Axis, Up) outbound and -T_out inbound (the attitude the outbound flip left),
    // so both flips turn in the same plane; in a turn the brake attitude held (-Axis at B, +Axis at A) for a ship whose
    // legs brake, else the turn's velocity direction; +Axis (or +X when there is no axis) for an invalid frame, +Axis
    // for a non-drivable lane
    SOLTEST_API FVector3d LaneHullForward(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FSOLLaneShip& ship, uint32 laneShipKey, double timeS);

    // Hull attitude and flip window (SDD 50 section 16, user decisions 2026-11-18, Docs/Plans/50-hull-flip-contract.md)

    // Returns +1 when the ship's flips swing its nose toward its own body top (top-over), -1 toward its bottom; a pure
    // function of the key (a salted hash, independent of the roll), nothing stored; both values occur over the keys
    SOLTEST_API int32 LaneFlipSide(uint32 laneShipKey);

    // Returns the ship's drawn attitude at time t: Forward bit-identical to LaneHullForward; Top T_out from the leg's
    // start to the outbound flip, turned rigidly with the nose through it (a half turn about side (Axis x T_out)), then
    // held as -T_out through the brake, the turn at B and the inbound leg up to its flip, which turns it back to T_out
    // in the same plane (held through the turn at A); a ship that never brakes carries T_out about Up with its velocity
    // in the turns. Unit, orthogonal, continuous and periodic over the cycle; a bad lane gives the forward's
    // LaneShipTopVector
    SOLTEST_API FSOLLaneHullBasis LaneHullBasis(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FSOLLaneShip& ship, uint32 laneShipKey, double timeS);

    // Returns the drawn rotation mapping +X to the normalised forward and +Z to the top made orthogonal to it
    // (Gram-Schmidt), +Y = Z x X (a proper rotation); frame-agnostic (the visuals convert both vectors with
    // SOLRender::EclipticToUnreal first). A top that is zero, non-finite or parallel to the forward falls back to a
    // perpendicular chosen from the forward alone; a zero or non-finite forward reads as +X. No singular heading
    SOLTEST_API FQuat HullRotation(const FVector3d& forward, const FVector3d& top);

    // Returns the peak rotational acceleration at a ship's ends in a flip, (1.5 pi / flipS)^2 x shipLengthM / 2 (the
    // smoothstep's peak turn rate squared times half the length); 0 for a non-positive or non-finite length or flipS
    SOLTEST_API double LaneFlipEndAccelMps2(double shipLengthM, double flipS = SOL::NPC_LANE_FLIP_S);

    // Returns a ship's limit on that acceleration: NPC_LANE_FLIP_END_ACCEL_MAX_G up to
    // NPC_LANE_FLIP_END_ACCEL_REF_LENGTH_M, falling as (ref / L)^NPC_LANE_FLIP_END_ACCEL_EXPONENT beyond it, never
    // below NPC_LANE_FLIP_END_ACCEL_MIN_G (provisional numbers); the maximum for a non-positive or non-finite length
    SOLTEST_API double LaneFlipEndAccelLimitMps2(double shipLengthM);

    // Returns a ship's flip window: max(NPC_LANE_FLIP_S, 1.5 pi sqrt(L / (2 LaneFlipEndAccelLimitMps2(L)))), the
    // shortest window at or above NPC_LANE_FLIP_S whose end acceleration stays within the limit; NPC_LANE_FLIP_S for a
    // non-positive or non-finite length. No ship length reaches the lane math yet (every hull is the
    // NPC_HULL_LENGTH_M placeholder, whose window is NPC_LANE_FLIP_S), so nothing calls it in the game (SDD 50 section
    // 16, open item)
    SOLTEST_API double LaneFlipDurationS(double shipLengthM);

    // Part 50c, the HUD merge readout (SDD 50 section 4.8, D7, D8, contract case 13)

    // Returns the verdict on a player speed against a traffic speed: delta = (v_pl - v_t) / v_t; MATCHED (green) for
    // |delta| <= NPC_LANE_MERGE_MATCH_FRACTION, else TOO FAST (delta > 0) or TOO SLOW, amber for |delta| <=
    // NPC_LANE_MERGE_AMBER_FRACTION, red beyond (a boundary belongs to the better class); invalid for a traffic speed
    // <= 0 or a non-finite input
    SOLTEST_API FSOLLaneMergeRating RateMerge(double trafficSpeedMps, double playerSpeedMps);

    // Returns the traffic direction the player would merge into, +1 outbound or -1 inbound: the sign of the along-lane
    // relative velocity when |v| >= NPC_LANE_MERGE_DIRECTION_MIN_MPS (NaN below), else the sign of the unit forward's
    // along component when |f| > NPC_LANE_MERGE_FORWARD_MIN (0.1; NaN as 0), else the player's half (right offset
    // >= 0 or NaN: outbound)
    SOLTEST_API int32 MergeDirectionSign(double alongVelocityMps, double alongForward, double rightOffsetM);

    // Returns where a point lies relative to a lane: its along-axis distance from A, arc from end ring A, foot
    // fraction, lateral offsets and rho, and whether it is in the leg and in the halo; invalid for an invalid frame, a
    // non-drivable lane or a non-finite point
    SOLTEST_API FSOLLanePoint LanePointOf(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FVector3d& pointM);

    // Returns the traffic speed v_t = MeanSpeedMps(u, rho) x MetricMapSlope(u) at a leg arc (clamped to the current
    // leg) and rho for a direction, u the nominal arc of the distance from the departure ring (X inverted); 0 for an
    // invalid frame, a non-drivable lane or a NaN arc or rho
    SOLTEST_API double TrafficSpeedMps(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame, double legArcM,
        double rho, int32 directionSign);

    // Returns the merge readout: the lane whose halo holds the player with the smallest rho (the lower index on a tie),
    // its frame velocity at the player's foot, the direction, the traffic and player speeds (sim seconds whatever the
    // warp, D7) and the verdict, and the warp note above 1x; hidden with no such lane or a non-finite position or
    // velocity
    SOLTEST_API FSOLLaneMergeReadout MergeReadout(TConstArrayView<FSOLLaneMergeLane> lanes, const FVector3d& playerM,
        const FVector3d& playerVelocityMps, const FVector3d& playerForward, double timeWarpScale);

    // Part 50d, the AUTO lane rules (SDD 50 section 4.9, D9, D19, D20, D22). Every input and output is in universe
    // ecliptic axes: the flight processor (Unreal axes, Y mirrored) converts with SOLRender::EclipticToUnreal both ways

    // Returns the first lane (lowest index; invalid or non-drivable lanes skipped) whose halo between its end rings
    // holds the straight path ship -> target for at least NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII wall radii of axis:
    // direction, entry arc (exactly 0 or l at an end ring, the ship's arc when it starts inside), exit arc (the
    // target's arc clamped to the leg), overlap, the lane's end bodies and whether the target is in the halo; not found for no
    // lanes, a zero-length path or a non-finite point
    SOLTEST_API FSOLLaneAutoLeg FindLaneLeg(TConstArrayView<FSOLLaneMergeLane> lanes, const FVector3d& shipM,
        const FVector3d& targetM);

    // Returns a lane view: the profile, MakeLaneFrame of the end bodies' positions, their velocities and indices and
    // the track's mean leg (the flight processor builds its views from the frame-start bodies)
    SOLTEST_API FSOLLaneMergeLane MakeMergeLane(const FSOLLaneProfile& profile, int32 bodyA, int32 bodyB,
        const FVector3d& aM, const FVector3d& bM, const FVector3d& aVelocityMps, const FVector3d& bVelocityMps);

    // Returns whether a lane may be searched: not for a War power order, only with nothing latched (Left never again)
    SOLTEST_API bool IsLaneSearchAllowed(ESOLLaneAutoMode latched, bool bIgnoreLanes);

    // Returns the keep-right track's lateral offset for a direction: d x Right x NPC_LANE_AUTO_TRACK_WALL_RADII x Rw
    // (clock 90 degrees of travel); zero for an invalid frame or a non-drivable profile
    SOLTEST_API FVector3d LaneTrackOffsetM(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        int32 directionSign);

    // Returns the merge point: on the keep-right track at the entry arc, moving with the lane frame plus d Axis v_t,
    // the traffic's acceleration there; invalid for a leg not found or a bad lane
    SOLTEST_API FSOLLaneMergeTarget LaneMergeTarget(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg);

    // Returns whether the approach is over: the ship in the halo, within NPC_LANE_AUTO_HOLD_RADIUS_WALL_RADII of the
    // track's radius and within NPC_LANE_AUTO_HOLD_SPEED_FRACTION of the track's traffic speed along the lane
    SOLTEST_API bool IsLaneMerged(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg, const FVector3d& shipM,
        const FVector3d& shipVelocityMps);

    // Returns the step target of an approach: the merge point when it is valid, else the order's own target (never the
    // origin)
    SOLTEST_API FSOLLaneStepTarget LaneApproachStepTarget(const FSOLLaneMergeTarget& merge,
        const FVector3d& orderTargetM, const FVector3d& orderTargetVelocityMps, const FVector3d& orderTargetAccelMps2);

    // Returns how far before the target AUTO leaves the lane: the braking distance of |v| at the brake plan (with its
    // flip) plus a margin of NPC_LANE_AUTO_LEAVE_MARGIN_FRACTION of it and NPC_LANE_AUTO_LEAVE_MARGIN_M; DBL_MAX (leave
    // at once) for a brake <= 0 or a non-finite input
    SOLTEST_API double LaneLeaveDistanceM(double relativeSpeedMps, double brakeAccelMps2, double flipS);

    // Returns the AUTO lane mode: Left stays Left; None without a leg; Left at or past the leave arc (the nearer of the
    // exit arc and the target's own arc less the leave distance of the speed relative to the target); Hold for a fresh
    // leg that starts inside, a holding ship still in the halo, or a merged ship; else Approach
    SOLTEST_API ESOLLaneAutoMode LaneAutoModeAt(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg,
        const FVector3d& shipM, const FVector3d& shipVelocityMps, const FVector3d& targetM,
        const FVector3d& targetVelocityMps, double brakeAccelMps2, double flipS, ESOLLaneAutoMode previous);

    // Returns the re-check of a latched Approach / Hold leg: Stale for a lane gone or replaced (index, end bodies,
    // invalid), Release for a target that was inside and left the halo or an approach no longer running along the lane
    // (D20), else Keep (also for any other mode)
    SOLTEST_API ESOLLaneLatchCheck CheckLaneLatch(TConstArrayView<FSOLLaneMergeLane> lanes, const FSOLLaneAutoLeg& leg,
        ESOLLaneAutoMode mode, const FVector3d& shipM, const FVector3d& targetM);

    // Returns one substep's lane decision: none for War power; a latched leg re-checked (Stale drops, Release leaves);
    // a search when allowed; the mode; the step target (the merge point in Approach, else the order target). A latched
    // leg's exit follows the target's arc on the current leg (the lane's length drifts with its bodies)
    SOLTEST_API FSOLLaneAutoDecision LaneAutoDecide(TConstArrayView<FSOLLaneMergeLane> lanes,
        const FSOLLaneAutoLeg& latchedLeg, ESOLLaneAutoMode latchedMode, const FSOLLaneAutoInput& input);

    // Returns whether the AUTOPILOT / CHASE marker shows its LANE tag (only while holding)
    SOLTEST_API bool IsLaneMarkerShown(ESOLLaneAutoMode mode);

    // Returns the hold guidance: toward the frame velocity plus d Axis v_t (the track's speed at the ship's arc) and a
    // capped lateral pull to the track, at velocityGainPerS plus the mean profile's acceleration; forward along travel
    // before the leg's midpoint and against it from there (D21); the phase from the mean profile
    SOLTEST_API FSOLGuidanceOutput LaneHoldGuidance(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg,
        const FVector3d& shipM, const FVector3d& shipVelocityMps, const FVector3d& targetM,
        const FVector3d& gravityAccelMps2, double velocityGainPerS);

    // Returns the time to fly between two leg arcs (clamped to the leg) at the traffic speed of radius rho: the mean
    // leg's closed-form time between the two nominal arcs (the map's slope cancels); 0 for equal arcs or a bad lane
    SOLTEST_API double LaneLegTimeS(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame, double fromArcM,
        double toArcM, double rho, int32 directionSign);

    // Returns the AUTO ETA with the lane: the approach to the merge point (0 for a ship in the halo), the lane part on
    // the track to the leave arc and the leave to the target from the traffic speed there (so it counts the braking),
    // approach and leave by SOLNewtonian::EstimateTimeToArrivalS; 0 without a leg
    SOLTEST_API double LaneAutoEtaS(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg, const FVector3d& shipM,
        const FVector3d& targetM, double accelMps2, double flipS);

    // Returns the HUD ETA of any AUTO kind: the lane ETA in Approach and Hold when it is finite and > 0, else the
    // guidance's own
    SOLTEST_API double LaneOrderEtaS(ESOLLaneAutoMode mode, double laneEtaS, double guidanceEtaS);

    // Returns the closing speed toward a target, (v_ship - v_target) . unit(target - ship); 0 for coincident points or
    // a non-finite input
    SOLTEST_API double LaneClosingSpeedMps(const FVector3d& shipM, const FVector3d& shipVelocityMps,
        const FVector3d& targetM, const FVector3d& targetVelocityMps);
}
