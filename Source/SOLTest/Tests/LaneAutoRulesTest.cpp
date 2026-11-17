/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLChase.h"
#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Flight/SOLShipState.h"
#include "Menu/SOLMenuText.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficMath.h"
#include "Universe/SOLRenderPlacement.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #50 Part 50d AUTO lane rules (SDD 50 section 4.9, decisions D9, D19, D20, D22, contract case 14),
// written against that contract before the production code existed (part 50d, test first), and extended test-first by
// the 50d review-fix round: the mid-lane exit (leave at the braking distance, never re-enter), lane views from the
// frame-start bodies, the latch re-check, the lane ETA choice and closing speed, the chase's lane activity and the
// approach target. Universe ecliptic metres and sim seconds, double. The bit-identical no-lane regression lives in
// AutopilotRegressionTest.cpp; the War power menu row, its per-command flag and its pulse in WarPowerMoveTest.cpp.
//
// API contract (from the test author; names the implementer follows). Leg arcs are measured from end ring A as in
// FSOLLanePoint::LegArcM; directionSign > 0 is outbound (A -> B), anything else inbound; the lanes are the per-frame
// views (FSOLLaneMergeLane: profile, frame, end body velocities and end bodies).
//
// Traffic/SOLLaneTrafficMath.h (pure, no world):
//   FSOLLaneMergeLane gains int32 BodyA = INDEX_NONE, BodyB = INDEX_NONE (the lane table entry's end bodies)
//   struct FSOLLaneAutoLeg { bool bFound = false; int32 Lane = INDEX_NONE; int32 DirectionSign = 1;
//       double EntryArcM = 0; double ExitArcM = 0; double OverlapM = 0; bool bStartsInside = false;
//       int32 BodyA = INDEX_NONE; int32 BodyB = INDEX_NONE; bool bTargetInside = false; } (the last three new)
//   enum class ESOLLaneAutoMode : uint8 { None, Approach, Hold, Left } (Left appended, transient: this order is done
//       with the lane, its normal AUTO step flies to the target and no lane is searched again until the next order)
//   enum class ESOLLaneLatchCheck : uint8 { Keep, Stale, Release }
//   struct FSOLLaneMergeTarget { bool bValid = false; FVector3d PositionM; FVector3d VelocityMps; FVector3d AccelMps2;
//       double TrafficSpeedMps = 0; }
//   struct FSOLLaneStepTarget { FVector3d PositionM, VelocityMps, AccelMps2 (zero by default) }
//   struct FSOLLaneAutoInput { FVector3d ShipM, ShipVelocityMps, TargetM, TargetVelocityMps, TargetAccelMps2 (zero by
//       default); double BrakeAccelMps2 = 0; double FlipS = 0; bool bSearch = true; bool bIgnoreLanes = false; }
//   struct FSOLLaneAutoDecision { FSOLLaneAutoLeg Leg; ESOLLaneAutoMode Mode = ESOLLaneAutoMode::None;
//       FSOLLaneStepTarget StepTarget; }
//   SOLLaneTraffic::MakeMergeLane(profile, bodyA, bodyB, aM, bM, aVelocityMps, bVelocityMps) -> FSOLLaneMergeLane: the
//       profile, MakeLaneFrame(profile, aM, bM), the velocities and the end bodies. The flight processor builds its
//       views from the frame-START (substep boundary 0) body positions and classifies the frame-start ship and target
//       positions against them, never frame-end views (SOLTest.LaneAutoRules.FrameStartViews)
//   SOLLaneTraffic::FindLaneLeg(lanes, shipM, targetM) -> FSOLLaneAutoLeg: the first lane (lowest index; invalid or
//       non-drivable lanes skipped) whose halo capsule (radius HaloOuterRadiusM around the axis, leg arcs [0,
//       CurrentLegM]) holds the straight segment ship -> target for an axis length OverlapM >=
//       NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII x WallRadiusM (D20: a crossing is left unchanged); DirectionSign = sign of
//       (target - ship) . Axis; EntryArcM = the leg arc where the segment first enters the halo (the ship's own arc
//       when bStartsInside, i.e. the ship is in the halo); ExitArcM = the target's leg arc clamped to [0, CurrentLegM]
//       (the target end's ring for a body at that end); BodyA / BodyB copied from the lane; bTargetInside = the
//       target point is in the halo between the end rings (LanePointOf(...).bInHalo); not found for no lanes, a
//       zero-length path or a non-finite point
//   SOLLaneTraffic::FindAutoLaneLeg: REMOVED (the processor never called it; IsLaneSearchAllowed gates War power)
//   SOLLaneTraffic::IsLaneSearchAllowed(latched, bIgnoreLanes) -> !bIgnoreLanes && latched == None (a War power order
//       never searches; a latched Approach / Hold keeps its leg; Left never searches again)
//   SOLLaneTraffic::LaneTrackOffsetM(profile, frame, directionSign) -> the keep-right track's lateral offset: d x Right
//       x NPC_LANE_AUTO_TRACK_WALL_RADII x WallRadiusM (clock 90 degrees for the travel direction, 12 o'clock = the
//       frame's Up, the ecliptic north projected); zero for an invalid frame or a non-drivable profile
//   SOLLaneTraffic::LaneMergeTarget(lane, leg) -> FSOLLaneMergeTarget: PositionM = A + Axis (EndArcAM + EntryArcM) +
//       LaneTrackOffsetM; TrafficSpeedMps = TrafficSpeedMps(profile, frame, EntryArcM, NPC_LANE_AUTO_TRACK_WALL_RADII,
//       d); VelocityMps = lerp(V_A, V_B, the point's foot fraction) + d Axis TrafficSpeedMps; AccelMps2 the profile's
//       (finite); invalid for a leg not found or a bad lane
//   SOLLaneTraffic::LaneApproachStepTarget(merge, orderTargetM, orderTargetVelocityMps, orderTargetAccelMps2) ->
//       FSOLLaneStepTarget: the merge point's position, velocity and acceleration when merge.bValid, else the order's
//       own target (an invalid merge never steers the ship to the origin)
//   SOLLaneTraffic::IsLaneMerged(lane, leg, shipM, shipVelocityMps) -> the approach ends: the ship is in the halo,
//       within NPC_LANE_AUTO_HOLD_RADIUS_WALL_RADII (0.25) wall radii of the track's radius, and its along-lane speed
//       relative to the frame (d Axis) is within NPC_LANE_AUTO_HOLD_SPEED_FRACTION (0.05) of the traffic speed on the
//       track at its arc; false for a leg not found
//   SOLLaneTraffic::LaneLeaveDistanceM(relativeSpeedMps, brakeAccelMps2, flipS) -> how far before the target AUTO
//       leaves the lane: SOLNewtonian::StoppingDistanceM(|v|, brakeAccelMps2, flipS) plus a margin (implementer's, >= 0
//       and at most 0.5 x that distance + 1e8 m); DBL_MAX for a brake <= 0 or a non-finite input (leave at once)
//   SOLLaneTraffic::LaneAutoModeAt(lane, leg, shipM, shipVelocityMps, targetM, targetVelocityMps, brakeAccelMps2,
//       flipS, previous) (replaces the five-argument form) -> Left when previous is Left; None for a leg not found;
//       Left when the ship is at or past its leave arc in its travel direction: the nearer of ExitArcM (an end ring
//       for a target outside the lane) and the target's own (unclamped) leg arc - d x LaneLeaveDistanceM(|ship
//       velocity - target velocity|, brakeAccelMps2, flipS) (a fresh leg already inside that range never engages);
//       else Hold when previous is None and leg.bStartsInside, when previous is Hold and the ship is still in the
//       halo, or when IsLaneMerged; else Approach. The caller passes the normal autopilot's brake plan
//       (SOLNewtonian::MakeGuidanceParams(env, AutopilotAccelLimitMps2(limits, meter), gravity, true, false):
//       BrakeAccelMps2, about 1.35 G, and FlipTimeS), so the stop that follows never overshoots the target
//   SOLLaneTraffic::CheckLaneLatch(lanes, leg, mode, shipM, targetM) -> Keep for a mode other than Approach / Hold;
//       Stale when leg.Lane is out of range, the lane there has another end-body pair than the leg (a respawned
//       table), or it is invalid or not drivable; Release when leg.bTargetInside and the target is no longer in that
//       lane's halo between its rings (a chase target that left the lane), or in Approach when the path ship ->
//       target no longer runs NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII inside that lane's halo (D20 re-tested; not in
//       Hold); else Keep
//   SOLLaneTraffic::LaneAutoDecide(lanes, latchedLeg, latchedMode, input) -> FSOLLaneAutoDecision, the processor's
//       whole per-substep lane decision: input.bIgnoreLanes -> no leg, None, the order target; else a latched Approach
//       / Hold is checked by CheckLaneLatch (Stale drops it to no leg and None, Release sets Left); then, when
//       input.bSearch (the first substep of a frame) and IsLaneSearchAllowed, FindLaneLeg; the mode by LaneAutoModeAt
//       with input.BrakeAccelMps2 / FlipS (Left stays Left, None without a leg); StepTarget =
//       LaneApproachStepTarget(LaneMergeTarget(lanes[Leg.Lane], Leg), the order target) in Approach, else the order
//       target. The processor then flies Approach: AutopilotStep to StepTarget; Hold: LaneHoldGuidance(lanes[Leg.Lane],
//       Leg, ...) + SOLFlight::LaneHoldStep; None / Left: its normal step (AutopilotStep, ChaseStep) to the order
//       target
//   SOLLaneTraffic::IsLaneMarkerShown(mode) -> true only for Hold (the AUTOPILOT / CHASE marker's LANE tag)
//   SOLLaneTraffic::LaneHoldGuidance(lane, leg, shipM, shipVelocityMps, targetM, gravityAccelMps2, velocityGainPerS) ->
//       FSOLGuidanceOutput: desired velocity v_frame + d Axis v_t (v_t on the track at the ship's arc) plus a lateral
//       term toward the track (gain NPC_LANE_AUTO_LATERAL_GAIN_PER_S, magnitude at most NPC_LANE_AUTO_LATERAL_MAX_MPS);
//       DesiredNetAccelMps2 = (v_des - v) x velocityGainPerS + the mean profile's acceleration;
//       DesiredThrustAccelMps2 = net - gravity; DesiredForward = d Axis before the leg's midpoint (nominal arc u <
//       l0 / 2 in the travel direction), -d Axis from it (D21); Phase from the mean profile
//       MakeLegProfile(RadialFactor(track rho), NPC_LANE_EFFORT_MEAN, l0): Accelerate for u < AccelM, Brake for u >
//       l0 - AccelM, else Cruise; SpeedLimitMps = v_t; DistanceM = |target - ship|
//   SOLLaneTraffic::LaneLegTimeS(profile, frame, fromArcM, toArcM, rho, directionSign) -> the time to fly between the
//       two leg arcs (clamped to [0, CurrentLegM]) at the traffic speed TrafficSpeedMps(..., rho, d) (an integral of
//       dx / v_t); 0 for equal arcs or a bad lane
//   SOLLaneTraffic::LaneAutoEtaS(lane, leg, shipM, targetM, accelMps2, flipS) -> the AUTO ETA with the lane slowdown:
//       the approach to the merge point (0 when bStartsInside) + LaneLegTimeS(start arc, the leave arc, track rho, d) +
//       the leave to the target, approach and leave by SOLNewtonian::EstimateTimeToArrivalS at accelMps2; the leave arc
//       as LaneAutoModeAt's for the traffic speed there (LaneLeaveDistanceM at accelMps2, flipS) and the leave term
//       starts at that speed (closing), so it counts the braking; 0 for a leg not found (the caller keeps the
//       guidance's own ETA)
//   SOLLaneTraffic::LaneOrderEtaS(mode, laneEtaS, guidanceEtaS) -> the HUD ETA of every AUTO kind (body, route, chase):
//       laneEtaS in Approach and Hold when it is finite and > 0, else guidanceEtaS
//   SOLLaneTraffic::LaneClosingSpeedMps(shipM, shipVelocityMps, targetM, targetVelocityMps) -> (v_ship - v_target) .
//       unit(target - ship); 0 for coincident points or a non-finite input (the chase marker's closing speed in lane
//       steps: to the target, not the along-lane speed)
//
// Flight/SOLFlight.h:
//   SOLFlight::LaneHoldStep(state, guidance, gravityAccelMps2, params, meter, dt, outStep = nullptr) -> FSOLShipState:
//       flies a LaneHoldGuidance output at the autopilot limit (SOLNewtonian::AutopilotAccelLimitMps2; Chase Down
//       holds at it too, no bursts), the same attitude and thrust allocation as AutopilotStep
//
// Flight/SOLChase.h: ESOLChaseActivity::Lane appended after Hold (transient, nothing persists it);
//   SOLChase::DisplayedChaseActivity(activity, bLaneStep) -> Lane in a lane step (Approach or Hold), else activity.
// Menu/SOLMenuText.h: CHASE_LANE, the chase activity line in lane steps (not COOL; wording the implementer's).
// Trip time wording (SOLInfoTextTest): the autopilot paragraph says the lane trip takes "over two weeks" and
//   "16 days" for Earth - Mars (the real lane, SOLTest.LaneAutoRules.LaneLegTime).
//
// Handedness (contract note): every SOLLaneTraffic function takes and returns right-handed universe ecliptic
// coordinates (the lane frames, the readout and the rails). The flight processor works in Unreal's left-handed axes
// (Y mirrored), so it converts the ship, target and velocity inputs with SOLRender::EclipticToUnreal (its own inverse:
// (X, -Y, Z)) before calling the lane math, and converts every returned vector (merge point and velocity, the hold
// guidance's accelerations and forward, the step target) back the same way; raw Unreal vectors fed in put keep right
// on the wrong side and paths along a lane off its axis (SOLTest.LaneAutoRules.EclipticNotUnrealAxes).
//
// SOLConstants.h: NPC_LANE_AUTO_LATERAL_GAIN_PER_S and NPC_LANE_AUTO_LATERAL_MAX_MPS (> 0; values the implementer's),
// NPC_LANE_AUTO_HOLD_RADIUS_WALL_RADII 0.25 and NPC_LANE_AUTO_HOLD_SPEED_FRACTION 0.05 (SDD 50 section 4.9 step 2),
// NPC_LANE_AUTO_TRACK_WALL_RADII 0.25, NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII 20.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double LANEAUTO_TOL = 1.0e-9;
    constexpr double LANEAUTO_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double LANEAUTO_WALL_RADIUS_M = 1.0e7;                // Rw of the test lanes
    constexpr double LANEAUTO_NOMINAL_LENGTH_M = 2.25e11;           // L0 (Earth - Mars scale)
    constexpr double LANEAUTO_RADIUS_A_M = 6.371e6;                 // Earth
    constexpr double LANEAUTO_RADIUS_B_M = 3.3895e6;                // Mars
    constexpr double LANEAUTO_DAY_S = 86400.0;
    const FVector3d LANEAUTO_VELOCITY_A(1.0e3, 2.98e4, 0.0);        // Body A's velocity (m/s)
    const FVector3d LANEAUTO_VELOCITY_B(-2.0e3, 2.41e4, 5.0e2);     // Body B's velocity (m/s)

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a is within relTol of b, relative to the larger magnitude (absolute below 1)
    bool LaneAutoNear(const double a, const double b, const double relTol = LANEAUTO_TOL)
    {
        return FMath::Abs(a - b) <= relTol * FMath::Max(1.0, FMath::Max(FMath::Abs(a), FMath::Abs(b)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two vectors agree within relTol of the larger length (absolute below 1)
    bool LaneAutoNearVec(const FVector3d& a, const FVector3d& b, const double relTol = LANEAUTO_TOL)
    {
        return (a - b).Size() <= relTol * FMath::Max(1.0, FMath::Max(a.Size(), b.Size()));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane view from A to B with the test wall radius, end radii and body velocities
    FSOLLaneMergeLane LaneAutoMakeLane(const FVector3d& aM, const FVector3d& bM,
        const double wallRadiusM = LANEAUTO_WALL_RADIUS_M)
    {
        FSOLLaneMergeLane lane;
        lane.Profile = SOLLaneTraffic::MakeLaneProfile(wallRadiusM, LANEAUTO_NOMINAL_LENGTH_M, LANEAUTO_RADIUS_A_M,
            LANEAUTO_RADIUS_B_M);
        lane.Frame = SOLLaneTraffic::MakeLaneFrame(lane.Profile, aM, bM);
        lane.AVelocityMps = LANEAUTO_VELOCITY_A;
        lane.BVelocityMps = LANEAUTO_VELOCITY_B;
        return lane;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Earth - Mars test lane at its nominal length along +X (current leg = nominal leg, identity map)
    FSOLLaneMergeLane LaneAutoNominalLane()
    {
        return LaneAutoMakeLane(FVector3d::ZeroVector, FVector3d(LANEAUTO_NOMINAL_LENGTH_M, 0.0, 0.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the point at leg arc arcM (from end ring A), rightRho and upRho wall radii off the axis on Right and Up
    FVector3d LaneAutoPoint(const FSOLLaneMergeLane& lane, const double arcM, const double rightRho,
        const double upRho = 0.0)
    {
        const double rw = lane.Profile.WallRadiusM;
        return lane.Frame.AM + lane.Frame.Axis * (lane.Profile.EndArcAM + arcM) + lane.Frame.Right * (rightRho * rw)
            + lane.Frame.Up * (upRho * rw);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the leg found for one lane
    FSOLLaneAutoLeg LaneAutoFindOne(const FSOLLaneMergeLane& lane, const FVector3d& shipM, const FVector3d& targetM)
    {
        const FSOLLaneMergeLane lanes[] = { lane };
        return SOLLaneTraffic::FindLaneLeg(lanes, shipM, targetM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the lane frame velocity at a point, lerp(V_A, V_B, its foot fraction)
    FVector3d LaneAutoFrameVelocity(const FSOLLaneMergeLane& lane, const FVector3d& pointM)
    {
        const FSOLLanePoint point = SOLLaneTraffic::LanePointOf(lane.Profile, lane.Frame, pointM);
        return lane.AVelocityMps + (lane.BVelocityMps - lane.AVelocityMps) * point.FootFraction;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the traffic speed on the keep-right track at a leg arc for a direction
    double LaneAutoTrackSpeed(const FSOLLaneMergeLane& lane, const double arcM, const int32 directionSign)
    {
        return SOLLaneTraffic::TrafficSpeedMps(lane.Profile, lane.Frame, arcM, SOL::NPC_LANE_AUTO_TRACK_WALL_RADII,
            directionSign);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the point on the keep-right track at a leg arc for a direction (right of its own travel)
    FVector3d LaneAutoTrackPoint(const FSOLLaneMergeLane& lane, const double arcM, const int32 directionSign)
    {
        const double side = directionSign > 0 ? 1.0 : -1.0;
        return LaneAutoPoint(lane, arcM, side * SOL::NPC_LANE_AUTO_TRACK_WALL_RADII);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the co-moving velocity on the track at a point: frame velocity + d Axis v_t at its arc
    FVector3d LaneAutoTrackVelocity(const FSOLLaneMergeLane& lane, const FVector3d& pointM, const double arcM,
        const int32 directionSign)
    {
        const double side = directionSign > 0 ? 1.0 : -1.0;
        return LaneAutoFrameVelocity(lane, pointM) + lane.Frame.Axis * (side * LaneAutoTrackSpeed(lane, arcM,
            directionSign));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the mean-effort profile on the track (the hold guidance's phase and flip reference)
    FSOLLaneLeg LaneAutoTrackProfile(const FSOLLaneMergeLane& lane)
    {
        return SOLLaneTraffic::MakeLegProfile(SOLLaneTraffic::RadialFactor(SOL::NPC_LANE_AUTO_TRACK_WALL_RADII),
            SOL::NPC_LANE_EFFORT_MEAN, lane.Profile.NominalLegM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an enum value as an integer for TestEqual
    template <typename TEnum>
    int32 LaneAutoInt(const TEnum value)
    {
        return static_cast<int32>(value);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the minimum overlap for a leg, NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII wall radii of axis length
    double LaneAutoMinOverlapM()
    {
        return SOL::NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII * LANEAUTO_WALL_RADIUS_M;
    }

    // Real-lane and simulation constants: aphelion sums (D16), the Moon, the lane radius's in-wall count, the
    // simulated substeps (the 1x flight step and a 10 s time-warp substep) and the overshoot allowed at a stop
    constexpr double LANEAUTO_EARTH_APHELION_M = 1.5210e11;
    constexpr double LANEAUTO_MARS_APHELION_M = 2.4923e11;
    constexpr double LANEAUTO_MOON_APOAPSIS_M = 4.055e8;
    constexpr double LANEAUTO_MOON_RADIUS_M = 1.7374e6;
    constexpr double LANEAUTO_MOON_GM = 4.9048695e12;
    constexpr int32 LANEAUTO_IN_WALL = 30000;
    constexpr double LANEAUTO_REAL_STEP_S = SOL::SHIP_MAX_SUBSTEP_S;
    constexpr double LANEAUTO_WARP_STEP_S = 10.0;
    constexpr double LANEAUTO_OVERSHOOT_BOUND_M = 1.0e5;

    //////////////////////////////////////////////////////////////////////////
    // Returns the normal autopilot's guidance budgets at a meter, no gravity (the brake plan every leave uses)
    FSOLGuidanceParams LaneAutoBrakePlan(const FSOLGMeterState& meter = FSOLGMeterState())
    {
        const FSOLFlightParams params;
        return SOLNewtonian::MakeGuidanceParams(params.Envelope,
            SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, meter), 0.0, true, false);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the lane mode toward a target with the normal autopilot's brake plan
    ESOLLaneAutoMode LaneAutoMode(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg, const FVector3d& shipM,
        const FVector3d& shipVelocityMps, const FVector3d& targetM, const FVector3d& targetVelocityMps,
        const ESOLLaneAutoMode previous)
    {
        const FSOLGuidanceParams plan = LaneAutoBrakePlan();
        return SOLLaneTraffic::LaneAutoModeAt(lane, leg, shipM, shipVelocityMps, targetM, targetVelocityMps,
            plan.BrakeAccelMps2, plan.FlipTimeS, previous);
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds a static default lane at its nominal (maximum) length along +X from an off-origin body A: the layout's
    // lane radius, LANEAUTO_IN_WALL ships in the wall, end bodies 1 and 2; false when the layout has no such lane
    bool LaneAutoRealLane(const TCHAR* bodyA, const TCHAR* bodyB, const double radiusAM, const double radiusBM,
        const double nominalLengthM, FSOLLaneMergeLane& outLane)
    {
        const SOLNpcScale::FLayout layout = SOLNpcScale::DefaultLayout();
        const SOLNpcScale::FLaneDef* def = layout.Lanes.FindByPredicate([bodyA, bodyB](const SOLNpcScale::FLaneDef& row)
        {
            return row.BodyA == FName(bodyA) && row.BodyB == FName(bodyB);
        });
        if (def == nullptr)
        {
            return false;
        }
        const FSOLLaneProfile profile = SOLLaneTraffic::MakeLaneProfile(SOLNpcTunnel::TunnelRadiusM(def->RadiusM,
            LANEAUTO_IN_WALL), nominalLengthM, radiusAM, radiusBM);
        const FVector3d aM(1.0e11, -5.0e10, 2.0e6);
        outLane = SOLLaneTraffic::MakeMergeLane(profile, 1, 2, aM, aM + FVector3d(nominalLengthM, 0.0, 0.0),
            FVector3d::ZeroVector, FVector3d::ZeroVector);
        return outLane.Profile.bDrivable && outLane.Frame.bValid;
    }

    // What one simulated AUTO order on a lane did
    struct FLaneAutoSimResult
    {
        bool bArrived = false;                  // The order's own guidance reported Arrived
        double ArrivalS = 0.0;                  // Sim time of the arrival
        double MaxOvershootM = 0.0;             // Furthest the ship got past the target along its travel (>= 0)
        double MinBodyDistanceM = TNumericLimits<double>::Max();   // Closest it came to the given body's centre
        bool bHeld = false;                     // It held the lane at some substep
        bool bLeft = false;                     // The mode became Left
        double LeaveRemainingM = 0.0;           // Ship to target along travel when the mode first became Left
        double LeaveSpeedMps = 0.0;             // The ship's speed relative to the target then
        double LeaveBrakeMps2 = 0.0;            // The brake plan then
        double LeaveFlipS = 0.0;
        int32 Reentries = 0;                    // Substeps in Approach or Hold after Left
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies one AUTO order (a route stop, or a chase with bChase) on a static lane substep by substep as the flight
    // processor does: SOLLaneTraffic::LaneAutoDecide with the normal brake plan, then the decided step (Approach:
    // AutopilotStep to the step target; Hold: LaneHoldGuidance and LaneHoldStep; None / Left: AutopilotStep or
    // ChaseStep to the target), the G meter stepped from the achieved thrust; stops at the order's arrival or maxS
    FLaneAutoSimResult LaneAutoSimulate(const FSOLLaneMergeLane& lane, const FVector3d& shipM,
        const FVector3d& shipVelocityMps, const FVector3d& targetStartM, const FVector3d& targetVelocityMps,
        const bool bChase, const double dt, const double maxS, const FVector3d& bodyM)
    {
        const FSOLFlightParams params;
        const FSOLLaneMergeLane lanes[] = { lane };
        const FVector3d zero = FVector3d::ZeroVector;
        const FVector3d travel = lane.Frame.Axis * ((targetStartM - shipM).Dot(lane.Frame.Axis) >= 0.0 ? 1.0 : -1.0);
        FLaneAutoSimResult result;
        FSOLShipState state;
        state.PositionM = shipM;
        state.VelocityMps = shipVelocityMps;
        state.Orientation = SOLNewtonian::OrientationFromForward(travel, lane.Frame.Up, FQuat4d::Identity);
        FSOLGMeterState meter;
        FSOLLaneAutoLeg leg;
        ESOLLaneAutoMode mode = ESOLLaneAutoMode::None;
        FSOLChasePolicy policy;     // The policy the Chase Down regression shows reaching the tail
        policy.BurstG = 6.0;
        policy.BrakePlanG = 4.0;
        policy.FloorMeter = SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT;
        FSOLChaseRunState chaseRun;
        const double holdGainPerS = FMath::Min(FSOLGuidanceParams().VelocityGainPerS, 1.0 / dt);
        const int32 steps = FMath::CeilToInt(maxS / dt);
        for (int32 index = 0; index < steps; ++index)
        {
            const double timeS = dt * index;
            const FVector3d targetM = targetStartM + targetVelocityMps * timeS;

            // The lane decision, with the normal autopilot's brake plan at this meter
            const FSOLGuidanceParams plan = LaneAutoBrakePlan(meter);
            FSOLLaneAutoInput input;
            input.ShipM = state.PositionM;
            input.ShipVelocityMps = state.VelocityMps;
            input.TargetM = targetM;
            input.TargetVelocityMps = targetVelocityMps;
            input.BrakeAccelMps2 = plan.BrakeAccelMps2;
            input.FlipS = plan.FlipTimeS;
            input.bSearch = true;
            input.bIgnoreLanes = false;
            const FSOLLaneAutoDecision decision = SOLLaneTraffic::LaneAutoDecide(lanes, leg, mode, input);
            const bool bLaneStep = decision.Mode == ESOLLaneAutoMode::Approach
                || decision.Mode == ESOLLaneAutoMode::Hold;
            result.Reentries += mode == ESOLLaneAutoMode::Left && bLaneStep ? 1 : 0;
            if (decision.Mode == ESOLLaneAutoMode::Left && !result.bLeft)
            {
                result.bLeft = true;
                result.LeaveRemainingM = (targetM - state.PositionM).Dot(travel);
                result.LeaveSpeedMps = (state.VelocityMps - targetVelocityMps).Size();
                result.LeaveBrakeMps2 = plan.BrakeAccelMps2;
                result.LeaveFlipS = plan.FlipTimeS;
            }
            leg = decision.Leg;
            mode = decision.Mode;

            // The decided step
            FSOLFlightStepOutput step;
            FSOLGuidanceOutput guidance;
            if (mode == ESOLLaneAutoMode::Approach)
            {
                state = SOLFlight::AutopilotStep(state, decision.StepTarget.PositionM, decision.StepTarget.VelocityMps,
                    decision.StepTarget.AccelMps2, zero, params, meter, dt, &step, &guidance);
            }
            else if (mode == ESOLLaneAutoMode::Hold && leg.Lane == 0)
            {
                guidance = SOLLaneTraffic::LaneHoldGuidance(lane, leg, state.PositionM, state.VelocityMps, targetM,
                    zero, holdGainPerS);
                state = SOLFlight::LaneHoldStep(state, guidance, zero, params, meter, dt, &step);
                result.bHeld = true;
            }
            else if (bChase)
            {
                state = SOLFlight::ChaseStep(state, targetM, targetVelocityMps, zero, zero, 0.0, params, meter, policy,
                    true, dt, chaseRun, &step, &guidance);
            }
            else
            {
                state = SOLFlight::AutopilotStep(state, targetM, targetVelocityMps, zero, zero, params, meter, dt,
                    &step, &guidance);
            }
            meter = SOLNewtonian::StepGMeter(meter, SOLNewtonian::GLoadG(step.ThrustAccelWorldMps2), params.PilotLimits,
                dt);

            // Overshoot past the moved target, the closest approach to the body, the order's arrival
            const FVector3d targetNextM = targetStartM + targetVelocityMps * (timeS + dt);
            result.MaxOvershootM = FMath::Max(result.MaxOvershootM, (state.PositionM - targetNextM).Dot(travel));
            result.MinBodyDistanceM = FMath::Min(result.MinBodyDistanceM, (state.PositionM - bodyM).Size());
            if (!bLaneStep && guidance.Phase == ESOLGuidancePhase::Arrived)
            {
                result.bArrived = true;
                result.ArrivalS = timeS + dt;
                break;
            }
        }
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts one simulated stop: it left the lane at least the braking distance before the target (one substep of
    // travel allowed), never re-entered the lane, never passed the target by more than LANEAUTO_OVERSHOOT_BOUND_M and
    // arrived
    void LaneAutoCheckStop(FAutomationTestBase& test, const FString& label, const FLaneAutoSimResult& result,
        const double dt)
    {
        test.TestTrue(label + TEXT(": left the lane"), result.bLeft);
        const double stopM = SOLNewtonian::StoppingDistanceM(result.LeaveSpeedMps, result.LeaveBrakeMps2,
            result.LeaveFlipS);
        test.TestTrue(FString::Printf(TEXT("%s: left %.6g m before the target at %.6g m/s, braking needs %.6g m"),
            *label, result.LeaveRemainingM, result.LeaveSpeedMps, stopM), result.bLeft
            && result.LeaveRemainingM >= stopM - result.LeaveSpeedMps * dt - 1.0);
        test.TestEqual(label + TEXT(": never re-entered the lane"), result.Reentries, 0);
        test.TestTrue(FString::Printf(TEXT("%s: overshoot %.6g m within %.6g m"), *label, result.MaxOvershootM,
            LANEAUTO_OVERSHOOT_BOUND_M), result.MaxOvershootM <= LANEAUTO_OVERSHOOT_BOUND_M);
        test.TestTrue(FString::Printf(TEXT("%s: arrived (%.6g s)"), *label, result.ArrivalS), result.bArrived);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a generous time bound for a stop: four times hold-then-brake from the start speed plus an hour
    double LaneAutoSimBoundS(const double distanceM, const double speedMps)
    {
        const double brakeMps2 = LaneAutoBrakePlan().BrakeAccelMps2;
        return 4.0 * (distanceM / FMath::Max(speedMps, 1.0) + speedMps / brakeMps2) + 3600.0;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoConstantsTest, "SOLTest.LaneAutoRules.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1 and 14 constants: a path uses a lane from 20 wall radii inside the halo (D20); the keep-right track at 0.25
// wall radii; the approach ends within 0.25 wall radii of the track's radius and 5 % of the traffic speed; the
// lateral pull toward the track has a positive gain and cap
bool FSOLLaneAutoConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("min overlap 20 wall radii"), SOL::NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII, 20.0);
    TestEqual(TEXT("track 0.25 wall radii"), SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, 0.25);
    TestEqual(TEXT("hold radius band 0.25 wall radii"), SOL::NPC_LANE_AUTO_HOLD_RADIUS_WALL_RADII, 0.25);
    TestEqual(TEXT("hold speed band 5 %"), SOL::NPC_LANE_AUTO_HOLD_SPEED_FRACTION, 0.05);
    TestTrue(TEXT("lateral gain > 0 and finite"), SOL::NPC_LANE_AUTO_LATERAL_GAIN_PER_S > 0.0
        && FMath::IsFinite(SOL::NPC_LANE_AUTO_LATERAL_GAIN_PER_S));
    TestTrue(TEXT("lateral cap > 0 and finite"), SOL::NPC_LANE_AUTO_LATERAL_MAX_MPS > 0.0
        && FMath::IsFinite(SOL::NPC_LANE_AUTO_LATERAL_MAX_MPS));
    TestTrue(TEXT("the track lies inside the wall"), SOL::NPC_LANE_AUTO_TRACK_WALL_RADII
        + SOL::NPC_LANE_AUTO_HOLD_RADIUS_WALL_RADII <= 1.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoFindAlongTest, "SOLTest.LaneAutoRules.FindLaneLegAlong",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14: a path along Earth - Mars inside the halo -> that lane, outbound, entering at end ring A and leaving at
// Mars's end ring, the overlap the whole leg; a parallel path at 1.5 and 1.99 wall radii also uses it, one at 2.01
// (just outside the halo, grazing it) does not
bool FSOLLaneAutoFindAlongTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double legM = lane.Frame.CurrentLegM;
    if (!TestTrue(TEXT("the test lane is drivable and valid"), lane.Profile.bDrivable && lane.Frame.bValid))
    {
        return false;
    }

    // Body centre to body centre on the axis
    const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, lane.Frame.AM, lane.Frame.BM);
    TestTrue(TEXT("A -> B: found"), leg.bFound);
    TestEqual(TEXT("A -> B: lane 0"), leg.Lane, 0);
    TestEqual(TEXT("A -> B: outbound"), leg.DirectionSign, 1);
    TestTrue(FString::Printf(TEXT("A -> B: entry at end ring A (%.6g)"), leg.EntryArcM), LaneAutoNear(leg.EntryArcM,
        0.0));
    TestTrue(FString::Printf(TEXT("A -> B: exit at Mars's end ring (%.6g vs %.6g)"), leg.ExitArcM, legM),
        LaneAutoNear(leg.ExitArcM, legM));
    TestTrue(FString::Printf(TEXT("A -> B: overlap the whole leg (%.6g vs %.6g)"), leg.OverlapM, legM),
        LaneAutoNear(leg.OverlapM, legM, 1.0e-6));
    TestFalse(TEXT("A -> B: the ship at Earth's centre is not in the halo"), leg.bStartsInside);

    // Parallel paths at a lateral offset (Up), inside and just outside the halo
    struct FParallelCase
    {
        double UpRho;
        bool bFound;
    };
    const FParallelCase cases[] = { { 1.5, true }, { 1.99, true }, { -1.99, true }, { 2.01, false },
        { -2.01, false }, { 5.0, false } };
    for (const FParallelCase& parallel : cases)
    {
        const FVector3d offset = lane.Frame.Up * (parallel.UpRho * LANEAUTO_WALL_RADIUS_M);
        const FSOLLaneAutoLeg parallelLeg = LaneAutoFindOne(lane, lane.Frame.AM + offset, lane.Frame.BM + offset);
        TestEqual(FString::Printf(TEXT("parallel at %.2f wall radii: found"), parallel.UpRho), parallelLeg.bFound,
            parallel.bFound);
        if (parallel.bFound && parallelLeg.bFound)
        {
            TestTrue(FString::Printf(TEXT("parallel at %.2f: overlap the whole leg"), parallel.UpRho),
                LaneAutoNear(parallelLeg.OverlapM, legM, 1.0e-6));
            TestEqual(FString::Printf(TEXT("parallel at %.2f: outbound"), parallel.UpRho), parallelLeg.DirectionSign,
                1);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoFindAcrossTest, "SOLTest.LaneAutoRules.FindLaneLegAcross",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14 and D20: a path that only crosses the lane is left unchanged: square across (no axis length inside), and an
// oblique crossing whose part inside the halo spans 10 wall radii of axis; a shallow one spanning 40 wall radii uses
// the lane, entering where it meets the halo and leaving at the target's foot
bool FSOLLaneAutoFindAcrossTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double rw = LANEAUTO_WALL_RADIUS_M;
    const double midArcM = 0.5 * lane.Frame.CurrentLegM;
    const FVector3d midM = LaneAutoPoint(lane, midArcM, 0.0);
    const FVector3d up = lane.Frame.Up;
    const FVector3d axis = lane.Frame.Axis;

    // Square across
    TestFalse(TEXT("square across: none"), LaneAutoFindOne(lane, midM - up * 1.0e9, midM + up * 1.0e9).bFound);
    TestFalse(TEXT("square across, the other way: none"), LaneAutoFindOne(lane, midM + up * 1.0e9,
        midM - up * 1.0e9).bFound);

    // Oblique: lateral / axial slope r keeps the path inside the halo (|lateral| <= 2 Rw) for 4 Rw / r of axis
    const double axialM = 100.0 * rw;
    const FSOLLaneAutoLeg steep = LaneAutoFindOne(lane, midM - axis * axialM - up * (0.4 * axialM),
        midM + axis * axialM + up * (0.4 * axialM));
    TestFalse(TEXT("oblique, 10 wall radii inside: none"), steep.bFound);

    const FSOLLaneAutoLeg shallow = LaneAutoFindOne(lane, midM - axis * axialM - up * (0.1 * axialM),
        midM + axis * axialM + up * (0.1 * axialM));
    TestTrue(TEXT("oblique, 40 wall radii inside: found"), shallow.bFound);
    if (shallow.bFound)
    {
        TestTrue(FString::Printf(TEXT("shallow: overlap 40 Rw (%.6g)"), shallow.OverlapM), LaneAutoNear(
            shallow.OverlapM, 40.0 * rw, 1.0e-6));
        TestTrue(FString::Printf(TEXT("shallow: enters the halo at mid - 20 Rw (%.6g)"), shallow.EntryArcM),
            LaneAutoNear(shallow.EntryArcM, midArcM - 20.0 * rw, 1.0e-6));
        TestTrue(FString::Printf(TEXT("shallow: exits at the target's foot (%.6g)"), shallow.ExitArcM),
            LaneAutoNear(shallow.ExitArcM, midArcM + axialM, 1.0e-6));
        TestEqual(TEXT("shallow: outbound"), shallow.DirectionSign, 1);
        TestFalse(TEXT("shallow: starts outside"), shallow.bStartsInside);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoFindThresholdTest, "SOLTest.LaneAutoRules.FindLaneLegThreshold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D20 boundary: from just before end ring A to a target inside the lane (the path ends inside it), 0.5 wall radii off
// the axis: 20 wall radii x (1 - 1e-3) inside the halo -> none, 20 x (1 + 1e-3) -> found, exiting at the target's arc
bool FSOLLaneAutoFindThresholdTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const FVector3d shipM = LaneAutoPoint(lane, -1.0e6, 0.5);
    for (const double factor : { 1.0 - 1.0e-3, 1.0 + 1.0e-3 })
    {
        const double targetArcM = LaneAutoMinOverlapM() * factor;
        const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, shipM, LaneAutoPoint(lane, targetArcM, 0.5));
        const bool bExpected = factor > 1.0;
        const FString label = FString::Printf(TEXT("%.4f x 20 wall radii inside"), factor);
        TestEqual(label + TEXT(": found"), leg.bFound, bExpected);
        if (bExpected && leg.bFound)
        {
            TestTrue(label + TEXT(": overlap = the target's arc"), LaneAutoNear(leg.OverlapM, targetArcM, 1.0e-6));
            TestTrue(label + TEXT(": entry at end ring A"), LaneAutoNear(leg.EntryArcM, 0.0, 1.0e-6));
            TestTrue(label + TEXT(": exit at the target inside the lane"), LaneAutoNear(leg.ExitArcM, targetArcM));
            TestFalse(label + TEXT(": the ship starts before the end ring"), leg.bStartsInside);
        }
    }

    // A longer path ending mid-lane on the axis
    const FSOLLaneAutoLeg midLeg = LaneAutoFindOne(lane, lane.Frame.AM, LaneAutoPoint(lane, 1.0e11, 0.0));
    TestTrue(TEXT("A -> mid-lane: found"), midLeg.bFound);
    TestTrue(TEXT("A -> mid-lane: exit at the target's arc"), LaneAutoNear(midLeg.ExitArcM, 1.0e11));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoFindEdgeTest, "SOLTest.LaneAutoRules.FindLaneLegEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14 edges: no lanes, a zero-length path, a non-finite point and a non-drivable lane -> none; a ship already
// inside the halo starts inside (hold without approach) at its own arc; the opposite direction is inbound, entering
// at Mars's end ring and leaving at Earth's; the first qualifying lane wins, skipping a bad lane and a crossed lane
bool FSOLLaneAutoFindEdgeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double legM = lane.Frame.CurrentLegM;

    // No lanes
    TestFalse(TEXT("no lanes: none"), SOLLaneTraffic::FindLaneLeg(TConstArrayView<FSOLLaneMergeLane>(),
        lane.Frame.AM, lane.Frame.BM).bFound);
    TestEqual(TEXT("no lanes: lane INDEX_NONE"), SOLLaneTraffic::FindLaneLeg(TConstArrayView<FSOLLaneMergeLane>(),
        lane.Frame.AM, lane.Frame.BM).Lane, static_cast<int32>(INDEX_NONE));

    // Zero-length path inside the lane
    const FVector3d insideM = LaneAutoPoint(lane, 1.0e10, 0.25);
    TestFalse(TEXT("zero-length path: none"), LaneAutoFindOne(lane, insideM, insideM).bFound);

    // Non-finite points
    const FVector3d nanM(LANEAUTO_NAN, 0.0, 0.0);
    TestFalse(TEXT("NaN ship: none"), LaneAutoFindOne(lane, nanM, lane.Frame.BM).bFound);
    TestFalse(TEXT("NaN target: none"), LaneAutoFindOne(lane, lane.Frame.AM, nanM).bFound);

    // A non-drivable lane (zero wall radius) is skipped
    const FSOLLaneMergeLane badLane = LaneAutoMakeLane(FVector3d::ZeroVector,
        FVector3d(LANEAUTO_NOMINAL_LENGTH_M, 0.0, 0.0), 0.0);
    TestFalse(TEXT("non-drivable lane: none"), LaneAutoFindOne(badLane, badLane.Frame.AM, badLane.Frame.BM).bFound);

    // Already inside the halo
    const FSOLLaneAutoLeg inside = LaneAutoFindOne(lane, insideM, lane.Frame.BM);
    TestTrue(TEXT("inside: found"), inside.bFound);
    TestTrue(TEXT("inside: starts inside (no approach)"), inside.bStartsInside);
    TestTrue(TEXT("inside: entry at the ship's own arc"), LaneAutoNear(inside.EntryArcM, 1.0e10));
    TestEqual(TEXT("inside: outbound"), inside.DirectionSign, 1);

    // Opposite direction
    const FSOLLaneAutoLeg back = LaneAutoFindOne(lane, lane.Frame.BM, lane.Frame.AM);
    TestTrue(TEXT("B -> A: found"), back.bFound);
    TestEqual(TEXT("B -> A: inbound"), back.DirectionSign, -1);
    TestTrue(TEXT("B -> A: entry at Mars's end ring"), LaneAutoNear(back.EntryArcM, legM));
    TestTrue(TEXT("B -> A: exit at Earth's end ring"), LaneAutoNear(back.ExitArcM, 0.0));
    TestTrue(TEXT("B -> A: overlap the whole leg"), LaneAutoNear(back.OverlapM, legM, 1.0e-6));

    // Lane order: a bad lane, then a lane square across the path, then the Earth - Mars lane
    const FSOLLaneMergeLane crossLane = LaneAutoMakeLane(FVector3d(1.0e11, -1.0e11, 0.0),
        FVector3d(1.0e11, 1.0e11, 0.0));
    const FSOLLaneMergeLane lanes[] = { badLane, crossLane, lane, lane };
    const FSOLLaneAutoLeg ordered = SOLLaneTraffic::FindLaneLeg(lanes, lane.Frame.AM, lane.Frame.BM);
    TestTrue(TEXT("ordered: found"), ordered.bFound);
    TestEqual(TEXT("ordered: the first lane the path runs along (index 2)"), ordered.Lane, 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoFindMovedTest, "SOLTest.LaneAutoRules.FindLaneLegMovedUnderWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Time warp moves the lane with its bodies between frames: the same path relative to the moved lane gives the same
// leg (entry, exit and overlap measured along the lane, not in absolute coordinates)
bool FSOLLaneAutoFindMovedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const FVector3d shift(3.0e11, -1.0e11, 5.0e9);
    const FSOLLaneMergeLane moved = LaneAutoMakeLane(lane.Frame.AM + shift, lane.Frame.BM + shift);
    const FVector3d shipM = LaneAutoPoint(lane, 2.0e9, 1.2);
    const FVector3d targetM = LaneAutoPoint(lane, 1.5e11, -0.4);
    const FSOLLaneAutoLeg before = LaneAutoFindOne(lane, shipM, targetM);
    const FSOLLaneAutoLeg after = LaneAutoFindOne(moved, shipM + shift, targetM + shift);
    TestTrue(TEXT("before: found"), before.bFound);
    TestTrue(TEXT("after: found"), after.bFound);
    TestEqual(TEXT("same direction"), after.DirectionSign, before.DirectionSign);
    TestEqual(TEXT("same start-inside flag"), after.bStartsInside, before.bStartsInside);
    TestTrue(FString::Printf(TEXT("same entry (%.9g vs %.9g)"), after.EntryArcM, before.EntryArcM),
        LaneAutoNear(after.EntryArcM, before.EntryArcM));
    TestTrue(FString::Printf(TEXT("same exit (%.9g vs %.9g)"), after.ExitArcM, before.ExitArcM),
        LaneAutoNear(after.ExitArcM, before.ExitArcM));
    TestTrue(FString::Printf(TEXT("same overlap (%.9g vs %.9g)"), after.OverlapM, before.OverlapM),
        LaneAutoNear(after.OverlapM, before.OverlapM));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoTrackOffsetTest, "SOLTest.LaneAutoRules.KeepRightTrackOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Keep right (outbound right, inbound left, 12 o'clock = the frame's Up, ecliptic north): the track offset lies at 0.25
// wall radii at clock 90 degrees of the travel direction: perpendicular to the axis, no Up component, on the right of
// the ship's own travel; outbound on +Right, inbound on -Right; zero for a bad lane
bool FSOLLaneAutoTrackOffsetTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const FSOLLaneMergeLane tilted = LaneAutoMakeLane(FVector3d(1.0e10, -4.0e10, 2.0e9),
        FVector3d(1.0e10 + 1.2e11, -4.0e10 + 1.8e11, 2.0e9 + 2.0e10));
    for (const FSOLLaneMergeLane* view : { &lane, &tilted })
    {
        const FString name = view == &lane ? TEXT("nominal") : TEXT("tilted");
        for (const int32 sign : { 1, -1 })
        {
            const FString label = name + (sign > 0 ? TEXT(" outbound") : TEXT(" inbound"));
            const FVector3d offset = SOLLaneTraffic::LaneTrackOffsetM(view->Profile, view->Frame, sign);
            const FVector3d travel = view->Frame.Axis * static_cast<double>(sign);
            const FVector3d rightOfTravel = FVector3d::CrossProduct(travel, view->Frame.Up);
            TestTrue(label + TEXT(": 0.25 wall radii"), LaneAutoNear(offset.Size(), SOL::NPC_LANE_AUTO_TRACK_WALL_RADII
                * view->Profile.WallRadiusM));
            TestTrue(label + TEXT(": perpendicular to the axis"), FMath::Abs(offset.Dot(view->Frame.Axis))
                <= 1.0e-9 * view->Profile.WallRadiusM);
            TestTrue(label + TEXT(": clock 90 degrees (no 12 o'clock component)"),
                FMath::Abs(offset.Dot(view->Frame.Up)) <= 1.0e-9 * view->Profile.WallRadiusM);
            TestTrue(label + TEXT(": right of its own travel"), offset.Dot(rightOfTravel) > 0.0);
            TestTrue(label + TEXT(": outbound on +Right, inbound on -Right"), offset.Dot(view->Frame.Right)
                * static_cast<double>(sign) > 0.0);
        }
        TestTrue(name + TEXT(": the two directions are opposite"), LaneAutoNearVec(SOLLaneTraffic::LaneTrackOffsetM(
            view->Profile, view->Frame, 1), -SOLLaneTraffic::LaneTrackOffsetM(view->Profile, view->Frame, -1)));
    }

    // A bad lane has no offset
    const FSOLLaneMergeLane badLane = LaneAutoMakeLane(FVector3d::ZeroVector,
        FVector3d(LANEAUTO_NOMINAL_LENGTH_M, 0.0, 0.0), 0.0);
    TestTrue(TEXT("non-drivable lane: zero"), SOLLaneTraffic::LaneTrackOffsetM(badLane.Profile, badLane.Frame, 1)
        .IsNearlyZero(0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoMergeTargetTest, "SOLTest.LaneAutoRules.MergeTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14 LaneMergeTarget: on the right half at the track radius at the entry arc, velocity = frame + d v_t; at an end
// ring entry the traffic speed is the wall speed (slow to the local traffic speed at the entry radius, D9); an
// oblique entry mid-lane merges at that arc's traffic speed; a leg not found gives no target
bool FSOLLaneAutoMergeTargetTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    struct FMergeCase
    {
        const TCHAR* Name;
        FVector3d ShipM;
        FVector3d TargetM;
    };
    const double rw = LANEAUTO_WALL_RADIUS_M;
    const FVector3d midM = LaneAutoPoint(lane, 0.5 * lane.Frame.CurrentLegM, 0.0);
    const FMergeCase cases[] = {
        { TEXT("A -> B"), lane.Frame.AM, lane.Frame.BM },
        { TEXT("B -> A"), lane.Frame.BM, lane.Frame.AM },
        { TEXT("oblique outbound"), midM - lane.Frame.Axis * (100.0 * rw) - lane.Frame.Up * (10.0 * rw),
            midM + lane.Frame.Axis * (100.0 * rw) + lane.Frame.Up * (10.0 * rw) },
    };
    for (const FMergeCase& mergeCase : cases)
    {
        const FString label = mergeCase.Name;
        const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, mergeCase.ShipM, mergeCase.TargetM);
        if (!TestTrue(label + TEXT(": leg found"), leg.bFound))
        {
            continue;
        }
        const FSOLLaneMergeTarget merge = SOLLaneTraffic::LaneMergeTarget(lane, leg);
        TestTrue(label + TEXT(": valid"), merge.bValid);
        const FSOLLanePoint point = SOLLaneTraffic::LanePointOf(lane.Profile, lane.Frame, merge.PositionM);
        TestTrue(FString::Printf(TEXT("%s: at the entry arc (%.9g vs %.9g)"), *label, point.LegArcM, leg.EntryArcM),
            FMath::Abs(point.LegArcM - leg.EntryArcM) <= 1.0e-3 + 1.0e-9 * FMath::Abs(leg.EntryArcM));
        TestTrue(FString::Printf(TEXT("%s: at the track radius (rho %.9f)"), *label, point.Rho),
            LaneAutoNear(point.Rho, SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, 1.0e-6));
        TestTrue(label + TEXT(": on the right half for its direction"), point.RightOffsetM
            * static_cast<double>(leg.DirectionSign) > 0.0);
        TestTrue(label + TEXT(": at clock 90 degrees (no Up offset)"), FMath::Abs(point.UpOffsetM) <= 1.0e-6 * rw);
        if (leg.EntryArcM > 1.0 && leg.EntryArcM < lane.Frame.CurrentLegM - 1.0)
        {
            TestTrue(label + TEXT(": a mid-lane merge point is in the halo"), point.bInHalo);
        }

        const double vt = LaneAutoTrackSpeed(lane, leg.EntryArcM, leg.DirectionSign);
        TestTrue(FString::Printf(TEXT("%s: traffic speed %.6f"), *label, merge.TrafficSpeedMps),
            LaneAutoNear(merge.TrafficSpeedMps, vt));
        const FVector3d expectedVelocity = LaneAutoFrameVelocity(lane, merge.PositionM) + lane.Frame.Axis
            * (static_cast<double>(leg.DirectionSign) * vt);
        TestTrue(label + TEXT(": velocity = frame + d v_t"), LaneAutoNearVec(merge.VelocityMps, expectedVelocity));
        TestFalse(label + TEXT(": acceleration finite"), merge.AccelMps2.ContainsNaN());
    }

    // An end ring entry merges at the wall speed (relative to the frame)
    const FSOLLaneAutoLeg endLeg = LaneAutoFindOne(lane, lane.Frame.AM, lane.Frame.BM);
    TestTrue(TEXT("end ring entry: the wall speed"), LaneAutoNear(SOLLaneTraffic::LaneMergeTarget(lane, endLeg)
        .TrafficSpeedMps, SOL::NPC_LANE_WALL_SPEED_MPS, 1.0e-6));

    // No leg, no target
    TestFalse(TEXT("leg not found: invalid"), SOLLaneTraffic::LaneMergeTarget(lane, FSOLLaneAutoLeg()).bValid);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoModeTest, "SOLTest.LaneAutoRules.ApproachHoldLeave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.9 steps 2-4: approach outside the halo; hold once in the halo near the track at the traffic speed
// (within 5 %), or at once for a ship engaged inside; a holding ship keeps holding in the halo; leave (Left) at or past
// the exit arc, and Left stays Left; the LANE marker shows only while holding; nothing without a leg. The target here
// is far ahead (Mars, or mid-lane), so the braking point never comes first except where a case says so
bool FSOLLaneAutoModeTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::IsLaneMerged;
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double legM = lane.Frame.CurrentLegM;
    const FVector3d targetM = lane.Frame.BM;
    const FVector3d targetVelocityMps = lane.BVelocityMps;
    const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, lane.Frame.AM, targetM);
    if (!TestTrue(TEXT("leg found"), leg.bFound))
    {
        return false;
    }

    // On the track at 30 % of the leg, co-moving: merged
    const double arcM = 0.3 * legM;
    const FVector3d onTrackM = LaneAutoTrackPoint(lane, arcM, 1);
    const FVector3d coMovingMps = LaneAutoTrackVelocity(lane, onTrackM, arcM, 1);
    const double vt = LaneAutoTrackSpeed(lane, arcM, 1);
    TestTrue(TEXT("on the track, co-moving: merged"), IsLaneMerged(lane, leg, onTrackM, coMovingMps));
    TestTrue(TEXT("on the track, 4 % slow: merged"), IsLaneMerged(lane, leg, onTrackM, coMovingMps
        - lane.Frame.Axis * (0.04 * vt)));
    TestFalse(TEXT("on the track, 10 % fast: not merged"), IsLaneMerged(lane, leg, onTrackM, coMovingMps
        + lane.Frame.Axis * (0.10 * vt)));
    TestFalse(TEXT("on the track, at rest in the frame: not merged"), IsLaneMerged(lane, leg, onTrackM,
        LaneAutoFrameVelocity(lane, onTrackM)));
    const FVector3d wideM = LaneAutoPoint(lane, arcM, 0.9);
    TestFalse(TEXT("0.9 wall radii (0.65 from the track's radius): not merged"), IsLaneMerged(lane, leg, wideM,
        LaneAutoTrackVelocity(lane, wideM, arcM, 1)));
    const FVector3d outsideM = LaneAutoPoint(lane, arcM, 3.0);
    TestFalse(TEXT("outside the halo: not merged"), IsLaneMerged(lane, leg, outsideM,
        LaneAutoTrackVelocity(lane, outsideM, arcM, 1)));
    TestFalse(TEXT("no leg: not merged"), IsLaneMerged(lane, FSOLLaneAutoLeg(), onTrackM, coMovingMps));

    // Modes toward Mars
    const FVector3d restMps = FVector3d::ZeroVector;
    TestEqual(TEXT("outside the halo: approach"), LaneAutoInt(LaneAutoMode(lane, leg, outsideM, restMps, targetM,
        targetVelocityMps, ESOLLaneAutoMode::None)), LaneAutoInt(ESOLLaneAutoMode::Approach));
    TestEqual(TEXT("still approaching: approach"), LaneAutoInt(LaneAutoMode(lane, leg, outsideM, restMps, targetM,
        targetVelocityMps, ESOLLaneAutoMode::Approach)), LaneAutoInt(ESOLLaneAutoMode::Approach));
    TestEqual(TEXT("merged: hold"), LaneAutoInt(LaneAutoMode(lane, leg, onTrackM, coMovingMps, targetM,
        targetVelocityMps, ESOLLaneAutoMode::Approach)), LaneAutoInt(ESOLLaneAutoMode::Hold));
    TestEqual(TEXT("in the halo, off speed, holding: keeps holding"), LaneAutoInt(LaneAutoMode(lane, leg, wideM,
        restMps, targetM, targetVelocityMps, ESOLLaneAutoMode::Hold)), LaneAutoInt(ESOLLaneAutoMode::Hold));
    TestEqual(TEXT("in the halo, off speed, approaching: approach"), LaneAutoInt(LaneAutoMode(lane, leg, wideM,
        restMps, targetM, targetVelocityMps, ESOLLaneAutoMode::Approach)), LaneAutoInt(ESOLLaneAutoMode::Approach));
    TestEqual(TEXT("held ship left the halo: approach again"), LaneAutoInt(LaneAutoMode(lane, leg, outsideM, restMps,
        targetM, targetVelocityMps, ESOLLaneAutoMode::Hold)), LaneAutoInt(ESOLLaneAutoMode::Approach));

    // Engaged inside: hold at once, whatever the speed
    const FSOLLaneAutoLeg inside = LaneAutoFindOne(lane, wideM, targetM);
    TestTrue(TEXT("engaged inside: starts inside"), inside.bStartsInside);
    TestEqual(TEXT("engaged inside: hold without approach"), LaneAutoInt(LaneAutoMode(lane, inside, wideM, restMps,
        targetM, targetVelocityMps, ESOLLaneAutoMode::None)), LaneAutoInt(ESOLLaneAutoMode::Hold));

    // Leave at or past the exit arc (outbound: at or past it; inbound: at or before it): Left, not None
    const FVector3d midTargetM = LaneAutoPoint(lane, 0.5 * legM, 0.0);
    const FVector3d midTargetMps = LaneAutoFrameVelocity(lane, midTargetM);
    const FSOLLaneAutoLeg shortLeg = LaneAutoFindOne(lane, lane.Frame.AM, midTargetM);
    const FVector3d pastM = LaneAutoTrackPoint(lane, 0.6 * legM, 1);
    TestEqual(TEXT("past the exit arc: left"), LaneAutoInt(LaneAutoMode(lane, shortLeg, pastM,
        LaneAutoTrackVelocity(lane, pastM, 0.6 * legM, 1), midTargetM, midTargetMps, ESOLLaneAutoMode::Hold)),
        LaneAutoInt(ESOLLaneAutoMode::Left));
    const FSOLLaneAutoLeg backLeg = LaneAutoFindOne(lane, lane.Frame.BM, midTargetM);
    const FVector3d backPastM = LaneAutoTrackPoint(lane, 0.4 * legM, -1);
    TestEqual(TEXT("inbound past the exit arc: left"), LaneAutoInt(LaneAutoMode(lane, backLeg, backPastM,
        LaneAutoTrackVelocity(lane, backPastM, 0.4 * legM, -1), midTargetM, midTargetMps, ESOLLaneAutoMode::Hold)),
        LaneAutoInt(ESOLLaneAutoMode::Left));
    const FVector3d backBeforeM = LaneAutoTrackPoint(lane, 0.7 * legM, -1);
    TestEqual(TEXT("inbound before the exit arc, merged: hold"), LaneAutoInt(LaneAutoMode(lane, backLeg,
        backBeforeM, LaneAutoTrackVelocity(lane, backBeforeM, 0.7 * legM, -1), midTargetM, midTargetMps,
        ESOLLaneAutoMode::Approach)), LaneAutoInt(ESOLLaneAutoMode::Hold));

    // Left stays Left, even far from the target and merged
    TestEqual(TEXT("left: stays left"), LaneAutoInt(LaneAutoMode(lane, leg, onTrackM, coMovingMps, targetM,
        targetVelocityMps, ESOLLaneAutoMode::Left)), LaneAutoInt(ESOLLaneAutoMode::Left));

    // No leg: nothing
    TestEqual(TEXT("no leg: none"), LaneAutoInt(LaneAutoMode(lane, FSOLLaneAutoLeg(), onTrackM, coMovingMps, targetM,
        targetVelocityMps, ESOLLaneAutoMode::Hold)), LaneAutoInt(ESOLLaneAutoMode::None));

    // The LANE marker
    TestFalse(TEXT("marker: none"), SOLLaneTraffic::IsLaneMarkerShown(ESOLLaneAutoMode::None));
    TestFalse(TEXT("marker: approach"), SOLLaneTraffic::IsLaneMarkerShown(ESOLLaneAutoMode::Approach));
    TestTrue(TEXT("marker: hold"), SOLLaneTraffic::IsLaneMarkerShown(ESOLLaneAutoMode::Hold));
    TestFalse(TEXT("marker: left"), SOLLaneTraffic::IsLaneMarkerShown(ESOLLaneAutoMode::Left));
    TestEqual(TEXT("Left appended after Hold"), LaneAutoInt(ESOLLaneAutoMode::Left),
        LaneAutoInt(ESOLLaneAutoMode::Hold) + 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoHoldTest, "SOLTest.LaneAutoRules.HoldGuidance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14 LaneHoldGuidance on the keep-right track: forward along travel before the leg's midpoint and against it from
// there (D21, before the brake), in both directions; the phase from the mean profile; the speed limit the traffic
// speed; the distance to the target; at the traffic speed the demand stays within the sustained limit (the profile
// alone), slower pushes forward and faster brakes; thrust = net - gravity
bool FSOLLaneAutoHoldTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double l0 = lane.Profile.NominalLegM;
    const FSOLLaneLeg mean = LaneAutoTrackProfile(lane);
    const double gain = FSOLGuidanceParams().VelocityGainPerS;
    const FVector3d gravityMps2(1.0e-3, -2.0e-3, 4.0e-4);
    const double sustainedMps2 = SOL::PILOT_SUSTAINED_G * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    if (!TestTrue(TEXT("mean profile valid with an accelerate distance"), mean.bValid && mean.AccelM > 0.0
        && 2.0 * mean.AccelM < l0))
    {
        return false;
    }

    struct FHoldCase
    {
        double NominalArcM;     // u in the travel direction
        ESOLGuidancePhase Phase;
        bool bForwardAlongTravel;
    };
    const FHoldCase cases[] = {
        { 0.5 * mean.AccelM, ESOLGuidancePhase::Accelerate, true },
        { 0.25 * l0, ESOLGuidancePhase::Cruise, true },
        { 0.75 * l0, ESOLGuidancePhase::Cruise, false },
        { l0 - 0.5 * mean.AccelM, ESOLGuidancePhase::Brake, false },
    };
    for (const int32 sign : { 1, -1 })
    {
        const FVector3d targetM = sign > 0 ? lane.Frame.BM : lane.Frame.AM;
        const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, sign > 0 ? lane.Frame.AM : lane.Frame.BM, targetM);
        if (!TestTrue(TEXT("leg found"), leg.bFound))
        {
            continue;
        }
        const FVector3d travel = lane.Frame.Axis * static_cast<double>(sign);
        for (const FHoldCase& hold : cases)
        {
            // On a nominal-length lane the map is the identity: arc = u outbound, l - u inbound
            const double arcM = sign > 0 ? hold.NominalArcM : lane.Frame.CurrentLegM - hold.NominalArcM;
            const FString label = FString::Printf(TEXT("%s u = %.4g l0"), sign > 0 ? TEXT("outbound")
                : TEXT("inbound"), hold.NominalArcM / l0);
            const FVector3d shipM = LaneAutoTrackPoint(lane, arcM, sign);
            const FVector3d vDes = LaneAutoTrackVelocity(lane, shipM, arcM, sign);
            const FSOLGuidanceOutput out = SOLLaneTraffic::LaneHoldGuidance(lane, leg, shipM, vDes, targetM,
                gravityMps2, gain);
            TestTrue(label + TEXT(": forward"), LaneAutoNearVec(out.DesiredForward, hold.bForwardAlongTravel ? travel
                : -travel, 1.0e-9));
            TestEqual(label + TEXT(": phase"), LaneAutoInt(out.Phase), LaneAutoInt(hold.Phase));
            TestTrue(FString::Printf(TEXT("%s: speed limit %.6f"), *label, out.SpeedLimitMps), LaneAutoNear(
                out.SpeedLimitMps, LaneAutoTrackSpeed(lane, arcM, sign)));
            TestTrue(label + TEXT(": distance to the target"), LaneAutoNear(out.DistanceM, (targetM - shipM).Size()));
            TestTrue(FString::Printf(TEXT("%s: at the traffic speed within the sustained limit (%.4f m/s^2)"), *label,
                out.DesiredNetAccelMps2.Size()), out.DesiredNetAccelMps2.Size() <= sustainedMps2 * (1.0 + 1.0e-9));
            TestTrue(label + TEXT(": thrust = net - gravity"), LaneAutoNearVec(out.DesiredThrustAccelMps2,
                out.DesiredNetAccelMps2 - gravityMps2));

            // Slower than the traffic pushes along travel, faster pushes back
            const FSOLGuidanceOutput slow = SOLLaneTraffic::LaneHoldGuidance(lane, leg, shipM, vDes - travel * 100.0,
                targetM, gravityMps2, gain);
            const FSOLGuidanceOutput fast = SOLLaneTraffic::LaneHoldGuidance(lane, leg, shipM, vDes + travel * 100.0,
                targetM, gravityMps2, gain);
            TestTrue(label + TEXT(": slow -> more push along travel"), (slow.DesiredNetAccelMps2
                - out.DesiredNetAccelMps2).Dot(travel) > 0.0);
            TestTrue(label + TEXT(": fast -> push against travel"), (fast.DesiredNetAccelMps2
                - out.DesiredNetAccelMps2).Dot(travel) < 0.0);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoHoldLateralTest, "SOLTest.LaneAutoRules.HoldGuidanceLateral",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hold's lateral term pulls a ship toward its keep-right track, at most NPC_LANE_AUTO_LATERAL_MAX_MPS: outbound a
// ship right of the track is pulled left, one on the axis or on the left half pulled right; inbound mirrored
bool FSOLLaneAutoHoldLateralTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double gain = FSOLGuidanceParams().VelocityGainPerS;
    const double arcM = 0.3 * lane.Frame.CurrentLegM;
    struct FLateralCase
    {
        int32 Sign;
        double RightRho;
        double ExpectedRightSign;   // Sign of the desired net acceleration on +Right
    };
    const FLateralCase cases[] = { { 1, 0.75, -1.0 }, { 1, 0.0, 1.0 }, { 1, -0.5, 1.0 }, { -1, -0.75, 1.0 },
        { -1, 0.0, -1.0 }, { -1, 0.5, -1.0 } };
    for (const FLateralCase& lateral : cases)
    {
        const FString label = FString::Printf(TEXT("%s at %.2f wall radii right"), lateral.Sign > 0
            ? TEXT("outbound") : TEXT("inbound"), lateral.RightRho);
        const FVector3d targetM = lateral.Sign > 0 ? lane.Frame.BM : lane.Frame.AM;
        const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, lateral.Sign > 0 ? lane.Frame.AM : lane.Frame.BM, targetM);
        const FVector3d shipM = LaneAutoPoint(lane, arcM, lateral.RightRho);
        const FVector3d alongMps = LaneAutoTrackVelocity(lane, shipM, arcM, lateral.Sign);
        const FSOLGuidanceOutput out = SOLLaneTraffic::LaneHoldGuidance(lane, leg, shipM, alongMps, targetM,
            FVector3d::ZeroVector, gain);
        const double rightAccel = out.DesiredNetAccelMps2.Dot(lane.Frame.Right);
        TestTrue(FString::Printf(TEXT("%s: pulled toward the track (%.6g m/s^2 on Right)"), *label, rightAccel),
            rightAccel * lateral.ExpectedRightSign > 0.0);
        TestTrue(label + TEXT(": lateral speed within the cap"), FMath::Abs(rightAccel) <= gain
            * SOL::NPC_LANE_AUTO_LATERAL_MAX_MPS * (1.0 + 1.0e-6) + 1.0e-3);
        TestFalse(label + TEXT(": finite"), out.DesiredNetAccelMps2.ContainsNaN());
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoHoldStepTest, "SOLTest.LaneAutoRules.HoldStepAtAutopilotLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14: the hold's demand, flown by LaneHoldStep, never exceeds the autopilot limit (sustained, or the recovery
// limit while critical; Chase Down holds at it too), even for a ship far off the traffic speed; it does thrust
bool FSOLLaneAutoHoldStepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const FSOLFlightParams params;
    const double arcM = 0.3 * lane.Frame.CurrentLegM;
    const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, lane.Frame.AM, lane.Frame.BM);
    FSOLShipState state;
    state.PositionM = LaneAutoTrackPoint(lane, arcM, 1);
    state.VelocityMps = LaneAutoTrackVelocity(lane, state.PositionM, arcM, 1) - lane.Frame.Axis * 5000.0;
    state.Orientation = FQuat4d::Identity;     // The nose on +X, the lane's axis
    const FSOLGuidanceOutput guidance = SOLLaneTraffic::LaneHoldGuidance(lane, leg, state.PositionM,
        state.VelocityMps, lane.Frame.BM, FVector3d::ZeroVector, FSOLGuidanceParams().VelocityGainPerS);
    TestTrue(TEXT("the demand is above the limit (2/s x 5 km/s)"), guidance.DesiredThrustAccelMps2.Size()
        > 2.0 * SOL::PILOT_SUSTAINED_G * SOLNewtonian::STANDARD_GRAVITY_MPS2);

    FSOLGMeterState full;
    FSOLGMeterState critical;
    critical.Meter = 0.5 * params.PilotLimits.CriticalMeter;
    for (const FSOLGMeterState& meter : { full, critical })
    {
        const FString label = meter.Meter >= 1.0 ? TEXT("full meter") : TEXT("critical meter");
        const double limitMps2 = SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, meter);
        FSOLFlightStepOutput step;
        SOLFlight::LaneHoldStep(state, guidance, FVector3d::ZeroVector, params, meter, 1.0, &step);
        const double achieved = step.ThrustAccelWorldMps2.Size();
        TestTrue(FString::Printf(TEXT("%s: achieved %.6f within the autopilot limit %.6f"), *label, achieved,
            limitMps2), achieved <= limitMps2 * (1.0 + 1.0e-9));
        TestTrue(label + TEXT(": it thrusts"), achieved > 0.0);
        TestTrue(label + TEXT(": along travel"), step.ThrustAccelWorldMps2.Dot(lane.Frame.Axis) > 0.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoLegTimeTest, "SOLTest.LaneAutoRules.LaneLegTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The lane slowdown in time (D19). On the REAL Earth - Mars lane (the default layout's lane radius, its nominal length
// the aphelion sum, D16) at its nominal length the whole leg at the traffic speed of the track is the closed-form
// mean-effort leg time of its frozen profile (1e-3), the same both ways: about 16 days, over two weeks, slower than the
// no-lane autopilot's flip and burn (SDD 50 section 4.9 trip time note). On the synthetic 2.25e11 m test lane: half
// of it takes half the time, equal arcs take 0, a longer stretch longer, a bad lane 0
bool FSOLLaneAutoLegTimeTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::LaneLegTimeS;
    const double rho = SOL::NPC_LANE_AUTO_TRACK_WALL_RADII;

    // The real Earth - Mars lane
    FSOLLaneMergeLane real;
    if (!TestTrue(TEXT("Earth - Mars is a default lane"), LaneAutoRealLane(TEXT("Earth"), TEXT("Mars"),
        LANEAUTO_RADIUS_A_M, LANEAUTO_RADIUS_B_M, LANEAUTO_EARTH_APHELION_M + LANEAUTO_MARS_APHELION_M, real)))
    {
        return false;
    }
    const double realLegM = real.Frame.CurrentLegM;
    const double closedS = SOLLaneTraffic::MakeLegProfile(SOLLaneTraffic::RadialFactor(rho), SOL::NPC_LANE_EFFORT_MEAN,
        real.Profile.NominalLegM).LegS;
    const double outS = LaneLegTimeS(real.Profile, real.Frame, 0.0, realLegM, rho, 1);
    const double backS = LaneLegTimeS(real.Profile, real.Frame, realLegM, 0.0, rho, -1);
    TestTrue(TEXT("at its nominal length the current leg is the nominal leg"), LaneAutoNear(realLegM,
        real.Profile.NominalLegM, 1.0e-9));
    TestTrue(FString::Printf(TEXT("real outbound %.6g s = the closed-form mean leg %.6g s"), outS, closedS),
        LaneAutoNear(outS, closedS, 1.0e-3));
    TestTrue(FString::Printf(TEXT("real inbound %.6g s"), backS), LaneAutoNear(backS, closedS, 1.0e-3));
    TestTrue(FString::Printf(TEXT("Earth - Mars %.3f days, about 16"), outS / LANEAUTO_DAY_S), outS >= 15.0
        * LANEAUTO_DAY_S && outS <= 17.0 * LANEAUTO_DAY_S);
    TestTrue(TEXT("over two weeks"), outS > 14.0 * LANEAUTO_DAY_S);
    const double sustainedMps2 = SOL::PILOT_SUSTAINED_G * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    const double noLaneS = SOLNewtonian::EstimateTimeToArrivalS(realLegM, 0.0, sustainedMps2, sustainedMps2, 0.0,
        SOL::MAX_SPEED_CAP_MPS);
    TestTrue(FString::Printf(TEXT("slower than the no-lane %.3f days"), noLaneS / LANEAUTO_DAY_S), outS > noLaneS);

    // The synthetic lane (not a real lane: 2.25e11 m nominal), relative checks only
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double legM = lane.Frame.CurrentLegM;
    const double expectedS = LaneAutoTrackProfile(lane).LegS;
    TestTrue(TEXT("synthetic whole leg = its mean leg"), LaneAutoNear(LaneLegTimeS(lane.Profile, lane.Frame, 0.0, legM,
        rho, 1), expectedS, 1.0e-3));
    TestTrue(TEXT("synthetic: half the leg, half the time"), LaneAutoNear(LaneLegTimeS(lane.Profile, lane.Frame, 0.0,
        0.5 * legM, rho, 1), 0.5 * expectedS, 1.0e-3));
    TestEqual(TEXT("equal arcs: 0"), LaneLegTimeS(lane.Profile, lane.Frame, 0.3 * legM, 0.3 * legM, rho, 1), 0.0);
    TestTrue(TEXT("a longer stretch takes longer"), LaneLegTimeS(lane.Profile, lane.Frame, 0.0, 0.3 * legM, rho, 1)
        < LaneLegTimeS(lane.Profile, lane.Frame, 0.0, 0.6 * legM, rho, 1));

    // A bad lane
    const FSOLLaneMergeLane badLane = LaneAutoMakeLane(FVector3d::ZeroVector,
        FVector3d(LANEAUTO_NOMINAL_LENGTH_M, 0.0, 0.0), 0.0);
    TestEqual(TEXT("non-drivable lane: 0"), LaneLegTimeS(badLane.Profile, badLane.Frame, 0.0, 1.0e10, rho, 1), 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoEtaTest, "SOLTest.LaneAutoRules.EtaWithSlowdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The AUTO ETA counts the lane leg (profile time plus the rest): from behind Earth on the axis (joining at end ring A)
// at least the whole lane part and longer than the no-lane ETA on Earth - Mars; a ship already on the track just past
// that ring (no approach, the same exit) is never later than that outside start; an oblique start that only meets the
// halo late (Earth + 5 wall radii Up: entry at about 0.6 of the leg) still counts the lane part it flies; ending
// mid-lane takes less than the whole lane; 0 without a leg (the guidance's own ETA stays)
bool FSOLLaneAutoEtaTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double legM = lane.Frame.CurrentLegM;
    const double accelMps2 = SOL::PILOT_SUSTAINED_G * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    const double flipS = 30.0;
    const double rho = SOL::NPC_LANE_AUTO_TRACK_WALL_RADII;

    // From 1e9 m behind Earth on the axis to Mars: joins at end ring A, the whole lane
    const FVector3d behindM = lane.Frame.AM - lane.Frame.Axis * 1.0e9;
    const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, behindM, lane.Frame.BM);
    if (!TestTrue(TEXT("behind Earth -> Mars: leg found"), leg.bFound))
    {
        return false;
    }
    TestTrue(TEXT("behind Earth: joins at end ring A"), LaneAutoNear(leg.EntryArcM, 0.0, 1.0e-6));
    TestFalse(TEXT("behind Earth: starts outside"), leg.bStartsInside);
    const double etaS = SOLLaneTraffic::LaneAutoEtaS(lane, leg, behindM, lane.Frame.BM, accelMps2, flipS);
    const double laneS = SOLLaneTraffic::LaneLegTimeS(lane.Profile, lane.Frame, leg.EntryArcM, leg.ExitArcM, rho,
        leg.DirectionSign);
    const double noLaneS = SOLNewtonian::EstimateTimeToArrivalS((lane.Frame.BM - behindM).Size(), 0.0, accelMps2,
        accelMps2, flipS, SOL::MAX_SPEED_CAP_MPS);
    TestTrue(FString::Printf(TEXT("ETA %.6g s at least the lane part %.6g s"), etaS, laneS), etaS >= laneS);
    TestTrue(FString::Printf(TEXT("ETA %.6g s above the no-lane %.6g s"), etaS, noLaneS), etaS > noLaneS);
    TestTrue(TEXT("ETA finite"), FMath::IsFinite(etaS));

    // A ship on the track just past the same ring skips the approach and exits at the same ring: never later
    const FVector3d onTrackM = LaneAutoTrackPoint(lane, 1.0e6, 1);
    const FSOLLaneAutoLeg insideLeg = LaneAutoFindOne(lane, onTrackM, lane.Frame.BM);
    TestTrue(TEXT("on the track: starts inside"), insideLeg.bStartsInside);
    TestTrue(TEXT("on the track: the same exit"), LaneAutoNear(insideLeg.ExitArcM, leg.ExitArcM));
    const double insideS = SOLLaneTraffic::LaneAutoEtaS(lane, insideLeg, onTrackM, lane.Frame.BM, accelMps2, flipS);
    TestTrue(FString::Printf(TEXT("on the track %.6g s, at least the lane part"), insideS), insideS
        >= SOLLaneTraffic::LaneLegTimeS(lane.Profile, lane.Frame, insideLeg.EntryArcM, insideLeg.ExitArcM, rho, 1)
        * (1.0 - 1.0e-9));
    TestTrue(FString::Printf(TEXT("on the track %.6g s: no later than joining at the same ring %.6g s"), insideS,
        etaS), insideS <= etaS);

    // An oblique start that meets the halo late counts only the lane part it flies, still at least that part
    const FVector3d upM = lane.Frame.AM + lane.Frame.Up * (5.0 * LANEAUTO_WALL_RADIUS_M);
    const FSOLLaneAutoLeg lateLeg = LaneAutoFindOne(lane, upM, lane.Frame.BM);
    TestTrue(TEXT("Earth + 5 Rw Up -> Mars: found"), lateLeg.bFound);
    TestTrue(FString::Printf(TEXT("Earth + 5 Rw Up: joins late (entry %.6g of the leg)"), lateLeg.EntryArcM / legM),
        lateLeg.EntryArcM > 0.5 * legM && lateLeg.EntryArcM < 0.7 * legM);
    const double lateS = SOLLaneTraffic::LaneAutoEtaS(lane, lateLeg, upM, lane.Frame.BM, accelMps2, flipS);
    TestTrue(FString::Printf(TEXT("Earth + 5 Rw Up: ETA %.6g s at least its lane part"), lateS), lateS
        >= SOLLaneTraffic::LaneLegTimeS(lane.Profile, lane.Frame, lateLeg.EntryArcM, lateLeg.ExitArcM, rho, 1));
    TestTrue(TEXT("Earth + 5 Rw Up: finite"), FMath::IsFinite(lateS));

    // Part of the lane: ending mid-lane takes less than the whole lane
    const FVector3d midTargetM = LaneAutoPoint(lane, 0.5 * legM, 0.0);
    const FSOLLaneAutoLeg halfLeg = LaneAutoFindOne(lane, behindM, midTargetM);
    TestTrue(TEXT("to mid-lane: found"), halfLeg.bFound);
    TestTrue(TEXT("to mid-lane: less than to Mars"), SOLLaneTraffic::LaneAutoEtaS(lane, halfLeg, behindM, midTargetM,
        accelMps2, flipS) < etaS);

    // The leave counts the braking: on the track to a target mid-lane (no approach), the ETA beyond the lane part to
    // the target is at least half the braking time v / a from the traffic speed there (braking over v^2 / 2a takes
    // v / a, cruising it v / 2a); a leave from rest at the target's arc would add almost nothing
    const FSOLLaneAutoLeg insideMidLeg = LaneAutoFindOne(lane, onTrackM, midTargetM);
    TestTrue(TEXT("on the track to mid-lane: starts inside"), insideMidLeg.bStartsInside);
    const double insideMidS = SOLLaneTraffic::LaneAutoEtaS(lane, insideMidLeg, onTrackM, midTargetM, accelMps2, flipS);
    const double midLaneS = SOLLaneTraffic::LaneLegTimeS(lane.Profile, lane.Frame, insideMidLeg.EntryArcM,
        insideMidLeg.ExitArcM, rho, 1);
    const double midSpeedMps = LaneAutoTrackSpeed(lane, 0.5 * legM, 1);
    TestTrue(FString::Printf(TEXT("to mid-lane %.6g s: the lane part %.6g s plus at least half the braking %.6g s"),
        insideMidS, midLaneS, 0.5 * midSpeedMps / accelMps2), insideMidS >= midLaneS + 0.45 * midSpeedMps / accelMps2);
    TestTrue(TEXT("to mid-lane: finite"), FMath::IsFinite(insideMidS));

    // Without a leg: 0
    TestEqual(TEXT("no leg: 0"), SOLLaneTraffic::LaneAutoEtaS(lane, FSOLLaneAutoLeg(), behindM, lane.Frame.BM,
        accelMps2, flipS), 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoWarPowerTest, "SOLTest.LaneAutoRules.WarPowerIgnoresLanes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D22: in the same lane on the same path a normal Move To uses the lane (and its slower ETA), a War power move does
// not: it never searches (IsLaneSearchAllowed), and the processor's decision gives no leg, no merge, no hold and the
// order's own target, so the plain straight path at maximum thrust arrives earlier; a crossing path is unaffected
// either way
bool FSOLLaneAutoWarPowerTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::IsLaneSearchAllowed;
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const FSOLLaneMergeLane lanes[] = { lane };
    const FVector3d shipM = lane.Frame.AM;
    const FVector3d targetM = lane.Frame.BM;

    // The search gate: only a normal order with nothing latched searches
    TestTrue(TEXT("normal, nothing latched: searches"), IsLaneSearchAllowed(ESOLLaneAutoMode::None, false));
    TestFalse(TEXT("War power: never searches"), IsLaneSearchAllowed(ESOLLaneAutoMode::None, true));
    TestFalse(TEXT("approach latched: keeps its leg"), IsLaneSearchAllowed(ESOLLaneAutoMode::Approach, false));
    TestFalse(TEXT("hold latched: keeps its leg"), IsLaneSearchAllowed(ESOLLaneAutoMode::Hold, false));
    TestFalse(TEXT("left: never searches again"), IsLaneSearchAllowed(ESOLLaneAutoMode::Left, false));

    // The decision for the same path, normal and War power
    const FSOLGuidanceParams plan = LaneAutoBrakePlan();
    FSOLLaneAutoInput input;
    input.ShipM = shipM;
    input.TargetM = targetM;
    input.TargetVelocityMps = lane.BVelocityMps;
    input.BrakeAccelMps2 = plan.BrakeAccelMps2;
    input.FlipS = plan.FlipTimeS;
    const FSOLLaneAutoDecision normal = SOLLaneTraffic::LaneAutoDecide(lanes, FSOLLaneAutoLeg(),
        ESOLLaneAutoMode::None, input);
    const FSOLLaneAutoLeg direct = SOLLaneTraffic::FindLaneLeg(lanes, shipM, targetM);
    TestTrue(TEXT("normal Move To: uses the lane"), normal.Leg.bFound);
    TestEqual(TEXT("normal Move To: the same leg as FindLaneLeg"), normal.Leg.Lane, direct.Lane);
    TestTrue(TEXT("normal Move To: same exit"), LaneAutoNear(normal.Leg.ExitArcM, direct.ExitArcM));
    TestEqual(TEXT("normal Move To: approaches the merge point"), LaneAutoInt(normal.Mode),
        LaneAutoInt(ESOLLaneAutoMode::Approach));
    input.bIgnoreLanes = true;
    const FSOLLaneAutoDecision warPower = SOLLaneTraffic::LaneAutoDecide(lanes, FSOLLaneAutoLeg(),
        ESOLLaneAutoMode::None, input);
    TestFalse(TEXT("War power: no lane leg"), warPower.Leg.bFound);
    TestEqual(TEXT("War power: no lane"), warPower.Leg.Lane, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("War power: no lane mode"), LaneAutoInt(warPower.Mode), LaneAutoInt(ESOLLaneAutoMode::None));
    TestTrue(TEXT("War power: flies to its own target"), LaneAutoNearVec(warPower.StepTarget.PositionM, targetM));

    // Started inside the lane on the left (wrong) half: War power still flies straight, no lane
    const FVector3d wrongSideM = LaneAutoPoint(lane, 1.0e10, -0.5);
    input.ShipM = wrongSideM;
    input.bIgnoreLanes = false;
    TestTrue(TEXT("inside, normal: uses the lane"), SOLLaneTraffic::LaneAutoDecide(lanes, FSOLLaneAutoLeg(),
        ESOLLaneAutoMode::None, input).Leg.bFound);
    input.bIgnoreLanes = true;
    TestFalse(TEXT("inside, War power: no lane leg"), SOLLaneTraffic::LaneAutoDecide(lanes, FSOLLaneAutoLeg(),
        ESOLLaneAutoMode::None, input).Leg.bFound);

    // A War power order replacing a latched hold drops the lane at once
    const FSOLLaneAutoLeg held = LaneAutoFindOne(lane, wrongSideM, targetM);
    const FSOLLaneAutoDecision replaced = SOLLaneTraffic::LaneAutoDecide(lanes, held, ESOLLaneAutoMode::Hold, input);
    TestEqual(TEXT("War power over a latched hold: no lane mode"), LaneAutoInt(replaced.Mode),
        LaneAutoInt(ESOLLaneAutoMode::None));

    // A crossing is left alone by both
    const FVector3d midM = LaneAutoPoint(lane, 0.5 * lane.Frame.CurrentLegM, 0.0);
    TestFalse(TEXT("crossing: none"), SOLLaneTraffic::FindLaneLeg(lanes, midM - lane.Frame.Up * 1.0e9,
        midM + lane.Frame.Up * 1.0e9).bFound);

    // Different results: the lane ETA against the straight path at the War power limit
    const FSOLFlightParams params;
    const double warLimitMps2 = SOLFlight::WarPowerAccelLimitMps2(params.Envelope, false);
    const double sustainedMps2 = SOL::PILOT_SUSTAINED_G * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    const double laneEtaS = SOLLaneTraffic::LaneAutoEtaS(lane, normal.Leg, shipM, targetM, sustainedMps2, 0.0);
    const double warEtaS = SOLNewtonian::EstimateTimeToArrivalS((targetM - shipM).Size(), 0.0, warLimitMps2,
        warLimitMps2, 0.0, SOL::MAX_SPEED_CAP_MPS);
    TestTrue(FString::Printf(TEXT("War power %.4f days earlier than the lane's %.4f days"), warEtaS / LANEAUTO_DAY_S,
        laneEtaS / LANEAUTO_DAY_S), warEtaS < laneEtaS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoHandednessTest, "SOLTest.LaneAutoRules.EclipticNotUnrealAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Handedness: on a lane along ecliptic +Y, a ship and target given in Unreal axes (as the flight processor holds them)
// use the lane only once converted to ecliptic with SOLRender::EclipticToUnreal (fed in raw, the mirrored path misses
// it); converted back to Unreal, the merge point lies on the Unreal right of travel (Up x Forward in Unreal's
// left-handed axes), the lane's ecliptic Right maps to that Unreal right, and the hold's forward points along the
// Unreal travel direction
bool FSOLLaneAutoHandednessTest::RunTest(const FString& /*parameters*/)
{
    using SOLRender::EclipticToUnreal;
    const FSOLLaneMergeLane lane = LaneAutoMakeLane(FVector3d(1.0e9, 2.0e9, 1.0e8),
        FVector3d(1.0e9, 2.0e9 + LANEAUTO_NOMINAL_LENGTH_M, 1.0e8));
    const FSOLLaneMergeLane lanes[] = { lane };
    if (!TestTrue(TEXT("the +Y lane is valid"), lane.Frame.bValid && lane.Profile.bDrivable))
    {
        return false;
    }

    // The processor's positions, in Unreal axes
    const FVector3d shipU = EclipticToUnreal(lane.Frame.AM);
    const FVector3d targetU = EclipticToUnreal(lane.Frame.BM);
    TestTrue(TEXT("the conversion is its own inverse"), LaneAutoNearVec(EclipticToUnreal(shipU), lane.Frame.AM));

    // Converted: the lane is used outbound; raw: the mirrored path misses it
    const FSOLLaneAutoLeg leg = SOLLaneTraffic::FindLaneLeg(lanes, EclipticToUnreal(shipU), EclipticToUnreal(targetU));
    TestTrue(TEXT("converted to ecliptic: found"), leg.bFound);
    TestEqual(TEXT("converted to ecliptic: outbound"), leg.DirectionSign, 1);
    TestFalse(TEXT("raw Unreal axes: the mirrored path misses the lane"), SOLLaneTraffic::FindLaneLeg(lanes, shipU,
        targetU).bFound);
    if (!leg.bFound)
    {
        return false;
    }

    // Keep right as the player sees it: Unreal right of travel = Up x Forward (X forward, Y right, Z up)
    const FVector3d forwardU = EclipticToUnreal(lane.Frame.Axis);
    const FVector3d upU = EclipticToUnreal(lane.Frame.Up);
    const FVector3d rightU = FVector3d::CrossProduct(upU, forwardU);
    TestTrue(TEXT("Unreal identity: Z x X = +Y (right)"), FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0),
        FVector3d(1.0, 0.0, 0.0)).Equals(FVector3d(0.0, 1.0, 0.0), 1.0e-12));
    TestTrue(TEXT("the lane's ecliptic Right is the Unreal right of travel"), EclipticToUnreal(lane.Frame.Right)
        .Dot(rightU) > 1.0 - 1.0e-9);
    const FSOLLaneMergeTarget merge = SOLLaneTraffic::LaneMergeTarget(lane, leg);
    const FVector3d axisPointU = EclipticToUnreal(lane.Frame.AM + lane.Frame.Axis * (lane.Profile.EndArcAM
        + leg.EntryArcM));
    const FVector3d mergeU = EclipticToUnreal(merge.PositionM);
    TestTrue(TEXT("the merge point is on the Unreal right of travel"), (mergeU - axisPointU).Dot(rightU)
        > 0.9 * SOL::NPC_LANE_AUTO_TRACK_WALL_RADII * LANEAUTO_WALL_RADIUS_M);

    // The hold's forward, converted back, points along the Unreal travel direction before the midpoint
    const double arcM = 0.25 * lane.Frame.CurrentLegM;
    const FVector3d holdM = LaneAutoTrackPoint(lane, arcM, 1);
    const FSOLGuidanceOutput hold = SOLLaneTraffic::LaneHoldGuidance(lane, leg, holdM, LaneAutoTrackVelocity(lane,
        holdM, arcM, 1), lane.Frame.BM, FVector3d::ZeroVector, FSOLGuidanceParams().VelocityGainPerS);
    TestTrue(TEXT("the hold's forward in Unreal axes points to the Unreal target"), EclipticToUnreal(
        hold.DesiredForward).Dot((targetU - shipU).GetSafeNormal()) > 1.0 - 1.0e-6);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoFrameStartTest, "SOLTest.LaneAutoRules.FrameStartViews",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (leg on frame-start bodies): under 86400x warp the bodies move about 4e7 m in one 60 Hz frame. The views
// MakeMergeLane builds from the frame-START bodies classify the frame-start ship and target correctly: a ship 2 wall
// radii past end ring A starts inside at its own arc, and a target 21 wall radii further on passes the 20-radius rule.
// The frame-END views read the same frame-start positions wrongly (the ship before the ring, only 19 wall radii
// inside), which is why ship, target and views must all be frame-start
bool FSOLLaneAutoFrameStartTest::RunTest(const FString& /*parameters*/)
{
    const double rw = LANEAUTO_WALL_RADIUS_M;
    const FSOLLaneProfile profile = SOLLaneTraffic::MakeLaneProfile(rw, LANEAUTO_NOMINAL_LENGTH_M, LANEAUTO_RADIUS_A_M,
        LANEAUTO_RADIUS_B_M);
    const double frameSimS = 86400.0 / 60.0;
    const FVector3d bodyVelocityMps(4.0e7 / frameSimS, 0.0, 0.0);
    const FVector3d moveM = bodyVelocityMps * frameSimS;
    const FVector3d aStartM(1.0e11, 2.0e10, 0.0);
    const FVector3d bStartM = aStartM + FVector3d(LANEAUTO_NOMINAL_LENGTH_M, 0.0, 0.0);
    const FSOLLaneMergeLane start = SOLLaneTraffic::MakeMergeLane(profile, 3, 4, aStartM, bStartM, bodyVelocityMps,
        bodyVelocityMps);
    const FSOLLaneMergeLane end = SOLLaneTraffic::MakeMergeLane(profile, 3, 4, aStartM + moveM, bStartM + moveM,
        bodyVelocityMps, bodyVelocityMps);
    TestTrue(TEXT("the bodies move 4e7 m in the frame"), LaneAutoNear(moveM.Size(), 4.0e7, 1.0e-9));

    // The builder: profile, frame, velocities and end bodies
    const FSOLLaneFrame frame = SOLLaneTraffic::MakeLaneFrame(profile, aStartM, bStartM);
    TestEqual(TEXT("builder: body A"), start.BodyA, 3);
    TestEqual(TEXT("builder: body B"), start.BodyB, 4);
    TestTrue(TEXT("builder: the frame of the given bodies"), LaneAutoNearVec(start.Frame.AM, frame.AM)
        && LaneAutoNearVec(start.Frame.BM, frame.BM) && LaneAutoNear(start.Frame.CurrentLegM, frame.CurrentLegM)
        && start.Frame.bValid);
    TestTrue(TEXT("builder: the profile"), LaneAutoNear(start.Profile.WallRadiusM, rw)
        && LaneAutoNear(start.Profile.NominalLegM, profile.NominalLegM));
    TestTrue(TEXT("builder: the velocities"), LaneAutoNearVec(start.AVelocityMps, bodyVelocityMps)
        && LaneAutoNearVec(start.BVelocityMps, bodyVelocityMps));

    // Frame-start ship and targets against the frame-start views
    const FVector3d shipM = LaneAutoPoint(start, 2.0 * rw, 0.25);
    const FVector3d nearTargetM = LaneAutoPoint(start, 23.0 * rw, 0.0);
    const FSOLLaneMergeLane startLanes[] = { start };
    const FSOLLaneMergeLane endLanes[] = { end };
    const FSOLLaneAutoLeg toB = SOLLaneTraffic::FindLaneLeg(startLanes, shipM, bStartM);
    TestTrue(TEXT("frame-start views: found"), toB.bFound);
    TestTrue(TEXT("frame-start views: the ship starts inside"), toB.bStartsInside);
    TestTrue(FString::Printf(TEXT("frame-start views: entry at the ship's arc, 2 wall radii (%.6g)"), toB.EntryArcM),
        LaneAutoNear(toB.EntryArcM, 2.0 * rw, 1.0e-6));
    const FSOLLaneAutoLeg toNear = SOLLaneTraffic::FindLaneLeg(startLanes, shipM, nearTargetM);
    TestTrue(TEXT("frame-start views: 21 wall radii inside passes the 20-radius rule"), toNear.bFound);
    TestTrue(FString::Printf(TEXT("frame-start views: overlap 21 wall radii (%.6g)"), toNear.OverlapM),
        LaneAutoNear(toNear.OverlapM, 21.0 * rw, 1.0e-6));

    // The same positions against frame-end views (the bug): misread
    TestFalse(TEXT("frame-end views: the ship seems to start before the ring"), SOLLaneTraffic::FindLaneLeg(endLanes,
        shipM, bStartM).bStartsInside);
    TestFalse(TEXT("frame-end views: only 19 wall radii seem inside"), SOLLaneTraffic::FindLaneLeg(endLanes, shipM,
        nearTargetM).bFound);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoLegFlagsTest, "SOLTest.LaneAutoRules.LegBodiesAndTargetInside",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (latched leg re-check): the leg carries its lane's end bodies, and whether the target lies in the halo
// between the end rings: Mars's centre (beyond its end ring) is not inside, a mid-lane point on the axis or in the
// halo is, a point just outside the halo is not; lanes without bodies give INDEX_NONE
bool FSOLLaneAutoLegFlagsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane base = LaneAutoNominalLane();
    const FSOLLaneMergeLane lane = SOLLaneTraffic::MakeMergeLane(base.Profile, 7, 9, base.Frame.AM, base.Frame.BM,
        base.AVelocityMps, base.BVelocityMps);
    const double legM = lane.Frame.CurrentLegM;

    const FSOLLaneAutoLeg toMars = LaneAutoFindOne(lane, lane.Frame.AM, lane.Frame.BM);
    TestTrue(TEXT("to Mars: found"), toMars.bFound);
    TestEqual(TEXT("to Mars: body A copied"), toMars.BodyA, 7);
    TestEqual(TEXT("to Mars: body B copied"), toMars.BodyB, 9);
    TestFalse(TEXT("to Mars: the body centre lies beyond its end ring, not inside"), toMars.bTargetInside);

    const FSOLLaneAutoLeg toMid = LaneAutoFindOne(lane, lane.Frame.AM, LaneAutoPoint(lane, 0.5 * legM, 0.0));
    TestTrue(TEXT("to mid-lane on the axis: target inside"), toMid.bFound && toMid.bTargetInside);
    const FVector3d fromM = LaneAutoPoint(lane, 0.2 * legM, 0.0);
    const FSOLLaneAutoLeg toHalo = LaneAutoFindOne(lane, fromM, LaneAutoPoint(lane, 0.6 * legM, 1.5));
    TestTrue(TEXT("to a point in the halo: target inside"), toHalo.bFound && toHalo.bTargetInside);
    const FSOLLaneAutoLeg toOutside = LaneAutoFindOne(lane, fromM, LaneAutoPoint(lane, 0.6 * legM, 2.5));
    TestTrue(TEXT("to a point just outside the halo: found (the path runs along)"), toOutside.bFound);
    TestFalse(TEXT("to a point just outside the halo: target not inside"), toOutside.bTargetInside);

    const FSOLLaneAutoLeg noBodies = LaneAutoFindOne(base, base.Frame.AM, base.Frame.BM);
    TestEqual(TEXT("a view without bodies: body A INDEX_NONE"), noBodies.BodyA, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("a view without bodies: body B INDEX_NONE"), noBodies.BodyB, static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoLeaveDistanceTest, "SOLTest.LaneAutoRules.LeaveDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (mid-lane exit): the leave distance is at least the braking distance of the speed relative to the target
// at the normal autopilot brake plan (about 1.35 G with its flip) and at most half again plus 1e8 m; it grows with the
// speed, reads |v|, and a bad brake or input leaves at once (DBL_MAX). At the traffic cap (300 km/s) the stop needs
// more than 3e9 m, the overshoot the old exit at the target's arc produced
bool FSOLLaneAutoLeaveDistanceTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::LaneLeaveDistanceM;
    const FSOLGuidanceParams plan = LaneAutoBrakePlan();
    const double brakeMps2 = plan.BrakeAccelMps2;
    const double flipS = plan.FlipTimeS;
    TestTrue(FString::Printf(TEXT("the brake plan is about 1.35 G (%.4f m/s^2)"), brakeMps2), FMath::Abs(brakeMps2
        - 0.9 * SOL::PILOT_SUSTAINED_G * SOLNewtonian::STANDARD_GRAVITY_MPS2) <= 0.01 * brakeMps2);

    double previousM = -1.0;
    for (const double speedMps : { 0.0, SOL::NPC_LANE_WALL_SPEED_MPS, 1.0e5, SOL::NPC_LANE_SPEED_CAP_MPS })
    {
        const double leaveM = LaneLeaveDistanceM(speedMps, brakeMps2, flipS);
        const double stopM = SOLNewtonian::StoppingDistanceM(speedMps, brakeMps2, flipS);
        const FString label = FString::Printf(TEXT("%.0f m/s: leave %.6g m, stop %.6g m"), speedMps, leaveM, stopM);
        TestTrue(label + TEXT(": finite and >= 0"), FMath::IsFinite(leaveM) && leaveM >= 0.0);
        TestTrue(label + TEXT(": at least the braking distance"), leaveM >= stopM * (1.0 - 1.0e-12));
        TestTrue(label + TEXT(": at most 1.5 x it + 1e8 m"), leaveM <= 1.5 * stopM + 1.0e8);
        TestTrue(label + TEXT(": grows with the speed"), leaveM > previousM);
        previousM = leaveM;
    }
    TestTrue(TEXT("at the cap the stop needs more than 3e9 m"), SOLNewtonian::StoppingDistanceM(
        SOL::NPC_LANE_SPEED_CAP_MPS, brakeMps2, flipS) > 3.0e9);
    TestTrue(TEXT("reads |v|"), LaneAutoNear(LaneLeaveDistanceM(-1.0e5, brakeMps2, flipS),
        LaneLeaveDistanceM(1.0e5, brakeMps2, flipS)));
    TestTrue(TEXT("zero brake: leave at once"), LaneLeaveDistanceM(1.0e5, 0.0, flipS) >= 1.0e300);
    TestTrue(TEXT("negative brake: leave at once"), LaneLeaveDistanceM(1.0e5, -1.0, flipS) >= 1.0e300);
    TestTrue(TEXT("NaN speed: leave at once"), LaneLeaveDistanceM(LANEAUTO_NAN, brakeMps2, flipS) >= 1.0e300);
    TestTrue(TEXT("NaN brake: leave at once"), LaneLeaveDistanceM(1.0e5, LANEAUTO_NAN, flipS) >= 1.0e300);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoLeaveTest, "SOLTest.LaneAutoRules.LeaveBeforeTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (mid-lane exit): a ship holding the traffic speed toward a target inside the lane leaves (Left) once the
// remaining arc is within the leave distance of its speed relative to the target, not at the target's arc: 0.9 x the
// braking distance short of the target it leaves whether it held, approached or just engaged (a fresh leg never
// engages there); 1.6 x it plus 1.1e8 m short it keeps holding; a target co-moving with the ship needs no braking
// room; both directions; Left stays Left
bool FSOLLaneAutoLeaveTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double legM = lane.Frame.CurrentLegM;
    const FSOLGuidanceParams plan = LaneAutoBrakePlan();
    for (const int32 sign : { 1, -1 })
    {
        const FString name = sign > 0 ? TEXT("outbound") : TEXT("inbound");
        const double arcM = sign > 0 ? 0.3 * legM : 0.7 * legM;
        const FVector3d shipM = LaneAutoTrackPoint(lane, arcM, sign);
        const FVector3d shipMps = LaneAutoTrackVelocity(lane, shipM, arcM, sign);

        // The braking distance of the speed relative to a target at rest in the lane frame near the ship
        const FVector3d probeM = LaneAutoPoint(lane, arcM, 0.0);
        const double relativeMps = (shipMps - LaneAutoFrameVelocity(lane, probeM)).Size();
        const double stopM = SOLNewtonian::StoppingDistanceM(relativeMps, plan.BrakeAccelMps2, plan.FlipTimeS);
        const double side = static_cast<double>(sign);

        // Far enough ahead: hold
        const FVector3d farM = LaneAutoPoint(lane, arcM + side * (1.6 * stopM + 1.1e8), 0.0);
        const FVector3d farMps = LaneAutoFrameVelocity(lane, farM);
        const FSOLLaneAutoLeg farLeg = LaneAutoFindOne(lane, shipM, farM);
        if (!TestTrue(name + TEXT(": far target, leg found inside"), farLeg.bFound && farLeg.bStartsInside
            && farLeg.bTargetInside))
        {
            continue;
        }
        TestEqual(name + TEXT(": far target, holding: hold"), LaneAutoInt(LaneAutoMode(lane, farLeg, shipM, shipMps,
            farM, farMps, ESOLLaneAutoMode::Hold)), LaneAutoInt(ESOLLaneAutoMode::Hold));
        TestEqual(name + TEXT(": far target, fresh: hold"), LaneAutoInt(LaneAutoMode(lane, farLeg, shipM, shipMps, farM,
            farMps, ESOLLaneAutoMode::None)), LaneAutoInt(ESOLLaneAutoMode::Hold));
        TestEqual(name + TEXT(": far target, left: stays left"), LaneAutoInt(LaneAutoMode(lane, farLeg, shipM, shipMps,
            farM, farMps, ESOLLaneAutoMode::Left)), LaneAutoInt(ESOLLaneAutoMode::Left));

        // Inside the braking distance: leave, from any mode
        const FVector3d nearM = LaneAutoPoint(lane, arcM + side * 0.9 * stopM, 0.0);
        const FVector3d nearMps = LaneAutoFrameVelocity(lane, nearM);
        const FSOLLaneAutoLeg nearLeg = LaneAutoFindOne(lane, shipM, nearM);
        if (!TestTrue(name + TEXT(": near target, leg found"), nearLeg.bFound && nearLeg.bTargetInside))
        {
            continue;
        }
        for (const ESOLLaneAutoMode previous : { ESOLLaneAutoMode::Hold, ESOLLaneAutoMode::Approach,
            ESOLLaneAutoMode::None })
        {
            TestEqual(FString::Printf(TEXT("%s: 0.9 x the braking distance (%.6g m) short, previous %d: left"), *name,
                stopM, LaneAutoInt(previous)), LaneAutoInt(LaneAutoMode(lane, nearLeg, shipM, shipMps, nearM, nearMps,
                previous)), LaneAutoInt(ESOLLaneAutoMode::Left));
        }

        // A target moving with the ship needs no braking room: keep holding
        TestEqual(name + TEXT(": near target co-moving with the ship: hold"), LaneAutoInt(LaneAutoMode(lane, nearLeg,
            shipM, shipMps, nearM, shipMps, ESOLLaneAutoMode::Hold)), LaneAutoInt(ESOLLaneAutoMode::Hold));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoLatchTest, "SOLTest.LaneAutoRules.LatchCheck",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (latched leg re-check): a latched Approach / Hold is kept on its own lane; after a population respawn an
// index out of range, a lane with another end-body pair at that index, or a bad lane there is Stale; a target that was
// inside and left the halo (sideways, or past an end) releases the ship; a body target beyond its ring never does;
// the 20-radius rule is re-tested in Approach only; None and Left have nothing to check
bool FSOLLaneAutoLatchTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::CheckLaneLatch;
    const FSOLLaneMergeLane base = LaneAutoNominalLane();
    const FSOLLaneMergeLane lane = SOLLaneTraffic::MakeMergeLane(base.Profile, 1, 2, base.Frame.AM, base.Frame.BM,
        base.AVelocityMps, base.BVelocityMps);
    const FSOLLaneMergeLane lanes[] = { lane };
    const double legM = lane.Frame.CurrentLegM;
    const double rw = LANEAUTO_WALL_RADIUS_M;
    const FVector3d shipM = LaneAutoTrackPoint(lane, 0.2 * legM, 1);
    const FVector3d targetM = LaneAutoPoint(lane, 0.6 * legM, 0.0);
    const FSOLLaneAutoLeg leg = SOLLaneTraffic::FindLaneLeg(lanes, shipM, targetM);
    if (!TestTrue(TEXT("leg found, target inside, bodies 1 and 2"), leg.bFound && leg.bTargetInside && leg.BodyA == 1
        && leg.BodyB == 2))
    {
        return false;
    }
    const int32 keep = LaneAutoInt(ESOLLaneLatchCheck::Keep);
    const int32 stale = LaneAutoInt(ESOLLaneLatchCheck::Stale);
    const int32 release = LaneAutoInt(ESOLLaneLatchCheck::Release);

    // Its own lane
    TestEqual(TEXT("hold on its lane: keep"), LaneAutoInt(CheckLaneLatch(lanes, leg, ESOLLaneAutoMode::Hold, shipM,
        targetM)), keep);
    TestEqual(TEXT("approach on its lane: keep"), LaneAutoInt(CheckLaneLatch(lanes, leg, ESOLLaneAutoMode::Approach,
        LaneAutoPoint(lane, 0.1 * legM, 3.0), targetM)), keep);

    // Nothing latched
    TestEqual(TEXT("none: keep"), LaneAutoInt(CheckLaneLatch(TConstArrayView<FSOLLaneMergeLane>(), leg,
        ESOLLaneAutoMode::None, shipM, targetM)), keep);
    TestEqual(TEXT("left: keep"), LaneAutoInt(CheckLaneLatch(TConstArrayView<FSOLLaneMergeLane>(), leg,
        ESOLLaneAutoMode::Left, shipM, targetM)), keep);

    // A respawned table
    TestEqual(TEXT("no lanes any more: stale"), LaneAutoInt(CheckLaneLatch(TConstArrayView<FSOLLaneMergeLane>(), leg,
        ESOLLaneAutoMode::Hold, shipM, targetM)), stale);
    const FSOLLaneMergeLane other = SOLLaneTraffic::MakeMergeLane(base.Profile, 5, 6, base.Frame.AM, base.Frame.BM,
        base.AVelocityMps, base.BVelocityMps);
    const FSOLLaneMergeLane otherLanes[] = { other };
    TestEqual(TEXT("another body pair at the index: stale"), LaneAutoInt(CheckLaneLatch(otherLanes, leg,
        ESOLLaneAutoMode::Hold, shipM, targetM)), stale);
    const FSOLLaneMergeLane moved[] = { other, lane };
    TestEqual(TEXT("its lane moved to another index: stale"), LaneAutoInt(CheckLaneLatch(moved, leg,
        ESOLLaneAutoMode::Approach, shipM, targetM)), stale);
    const FSOLLaneProfile badProfile = SOLLaneTraffic::MakeLaneProfile(0.0, LANEAUTO_NOMINAL_LENGTH_M,
        LANEAUTO_RADIUS_A_M, LANEAUTO_RADIUS_B_M);
    const FSOLLaneMergeLane badLanes[] = { SOLLaneTraffic::MakeMergeLane(badProfile, 1, 2, base.Frame.AM,
        base.Frame.BM, base.AVelocityMps, base.BVelocityMps) };
    TestEqual(TEXT("its bodies on a bad lane: stale"), LaneAutoInt(CheckLaneLatch(badLanes, leg,
        ESOLLaneAutoMode::Hold, shipM, targetM)), stale);

    // The target left the lane
    const FVector3d sidewaysM = LaneAutoPoint(lane, 0.6 * legM, 3.0);
    TestEqual(TEXT("target left sideways, hold: release"), LaneAutoInt(CheckLaneLatch(lanes, leg,
        ESOLLaneAutoMode::Hold, shipM, sidewaysM)), release);
    TestEqual(TEXT("target left sideways, approach: release"), LaneAutoInt(CheckLaneLatch(lanes, leg,
        ESOLLaneAutoMode::Approach, shipM, sidewaysM)), release);
    TestEqual(TEXT("target left past the end: release"), LaneAutoInt(CheckLaneLatch(lanes, leg,
        ESOLLaneAutoMode::Hold, shipM, lane.Frame.BM + lane.Frame.Axis * 1.0e9)), release);
    const FSOLLaneAutoLeg toMars = SOLLaneTraffic::FindLaneLeg(lanes, shipM, lane.Frame.BM);
    TestEqual(TEXT("a body target beyond its ring (never inside): keep"), LaneAutoInt(CheckLaneLatch(lanes, toMars,
        ESOLLaneAutoMode::Hold, shipM, lane.Frame.BM)), keep);

    // The 20-radius rule, re-tested in Approach only: from 10 wall radii short and 5 wall radii out, too little runs
    // inside the halo
    const FVector3d sideShipM = LaneAutoPoint(lane, 0.6 * legM - 10.0 * rw, 5.0);
    TestEqual(TEXT("approach, the path no longer runs 20 wall radii inside: release"), LaneAutoInt(CheckLaneLatch(lanes,
        leg, ESOLLaneAutoMode::Approach, sideShipM, targetM)), release);
    TestEqual(TEXT("hold, same place: keep (not re-tested in hold)"), LaneAutoInt(CheckLaneLatch(lanes, leg,
        ESOLLaneAutoMode::Hold, sideShipM, targetM)), keep);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoDecideTest, "SOLTest.LaneAutoRules.Decide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The processor's per-substep lane decision composes the rules: from behind Earth it approaches the merge point (the
// step target); on the track it holds and flies toward the order target; without a search nothing is found; a stale
// latch after a respawn is searched again on the new table (or dropped without a search); a released latch becomes
// Left and stays Left (no re-entry); a fresh leg already within the braking distance never engages
bool FSOLLaneAutoDecideTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::LaneAutoDecide;
    const FSOLLaneMergeLane base = LaneAutoNominalLane();
    const FSOLLaneMergeLane lane = SOLLaneTraffic::MakeMergeLane(base.Profile, 1, 2, base.Frame.AM, base.Frame.BM,
        base.AVelocityMps, base.BVelocityMps);
    const FSOLLaneMergeLane lanes[] = { lane };
    const double legM = lane.Frame.CurrentLegM;
    const FSOLGuidanceParams plan = LaneAutoBrakePlan();

    // Returns a decision input
    auto makeInput = [&plan](const FVector3d& shipM, const FVector3d& shipMps, const FVector3d& targetM,
        const FVector3d& targetMps, const bool bSearch)
    {
        FSOLLaneAutoInput input;
        input.ShipM = shipM;
        input.ShipVelocityMps = shipMps;
        input.TargetM = targetM;
        input.TargetVelocityMps = targetMps;
        input.BrakeAccelMps2 = plan.BrakeAccelMps2;
        input.FlipS = plan.FlipTimeS;
        input.bSearch = bSearch;
        input.bIgnoreLanes = false;
        return input;
    };

    // From behind Earth to Mars: approach, the step target the merge point
    const FVector3d behindM = lane.Frame.AM - lane.Frame.Axis * 1.0e9;
    const FSOLLaneAutoDecision approach = LaneAutoDecide(lanes, FSOLLaneAutoLeg(), ESOLLaneAutoMode::None,
        makeInput(behindM, FVector3d::ZeroVector, lane.Frame.BM, lane.BVelocityMps, true));
    TestEqual(TEXT("behind Earth: approach"), LaneAutoInt(approach.Mode), LaneAutoInt(ESOLLaneAutoMode::Approach));
    TestTrue(TEXT("behind Earth: lane 0, bodies 1 and 2"), approach.Leg.bFound && approach.Leg.Lane == 0
        && approach.Leg.BodyA == 1 && approach.Leg.BodyB == 2);
    const FSOLLaneMergeTarget merge = SOLLaneTraffic::LaneMergeTarget(lane, approach.Leg);
    TestTrue(TEXT("behind Earth: steps to the merge point"), merge.bValid && LaneAutoNearVec(
        approach.StepTarget.PositionM, merge.PositionM) && LaneAutoNearVec(approach.StepTarget.VelocityMps,
        merge.VelocityMps));

    // On the track, co-moving: hold, toward the order's own target
    const double arcM = 0.3 * legM;
    const FVector3d onTrackM = LaneAutoTrackPoint(lane, arcM, 1);
    const FVector3d coMovingMps = LaneAutoTrackVelocity(lane, onTrackM, arcM, 1);
    const FSOLLaneAutoDecision hold = LaneAutoDecide(lanes, FSOLLaneAutoLeg(), ESOLLaneAutoMode::None,
        makeInput(onTrackM, coMovingMps, lane.Frame.BM, lane.BVelocityMps, true));
    TestEqual(TEXT("on the track: hold"), LaneAutoInt(hold.Mode), LaneAutoInt(ESOLLaneAutoMode::Hold));
    TestTrue(TEXT("on the track: the step target is the order target"), LaneAutoNearVec(hold.StepTarget.PositionM,
        lane.Frame.BM));

    // No search, nothing latched: nothing
    const FSOLLaneAutoDecision quiet = LaneAutoDecide(lanes, FSOLLaneAutoLeg(), ESOLLaneAutoMode::None,
        makeInput(onTrackM, coMovingMps, lane.Frame.BM, lane.BVelocityMps, false));
    TestEqual(TEXT("no search: none"), LaneAutoInt(quiet.Mode), LaneAutoInt(ESOLLaneAutoMode::None));
    TestFalse(TEXT("no search: no leg"), quiet.Leg.bFound);

    // A respawned table: a crossing lane now at index 0, the latched lane at index 1
    const FSOLLaneMergeLane cross = SOLLaneTraffic::MakeMergeLane(base.Profile, 5, 6, FVector3d(1.0e11, -1.0e11, 0.0),
        FVector3d(1.0e11, 1.0e11, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector);
    const FSOLLaneMergeLane respawned[] = { cross, lane };
    const FSOLLaneAutoDecision researched = LaneAutoDecide(respawned, hold.Leg, ESOLLaneAutoMode::Hold,
        makeInput(onTrackM, coMovingMps, lane.Frame.BM, lane.BVelocityMps, true));
    TestTrue(TEXT("stale latch: searched again, found at index 1 with its bodies"), researched.Leg.bFound
        && researched.Leg.Lane == 1 && researched.Leg.BodyA == 1);
    TestEqual(TEXT("stale latch: holds again"), LaneAutoInt(researched.Mode), LaneAutoInt(ESOLLaneAutoMode::Hold));
    const FSOLLaneAutoDecision dropped = LaneAutoDecide(respawned, hold.Leg, ESOLLaneAutoMode::Hold,
        makeInput(onTrackM, coMovingMps, lane.Frame.BM, lane.BVelocityMps, false));
    TestEqual(TEXT("stale latch, no search: none"), LaneAutoInt(dropped.Mode), LaneAutoInt(ESOLLaneAutoMode::None));
    TestFalse(TEXT("stale latch, no search: no leg"), dropped.Leg.bFound);

    // Released (the target left sideways): Left toward the target, and it stays Left when the target comes back
    const FVector3d midM = LaneAutoPoint(lane, 0.6 * legM, 0.0);
    const FSOLLaneAutoLeg midLeg = SOLLaneTraffic::FindLaneLeg(lanes, onTrackM, midM);
    const FVector3d sidewaysM = LaneAutoPoint(lane, 0.6 * legM, 3.0);
    const FSOLLaneAutoDecision released = LaneAutoDecide(lanes, midLeg, ESOLLaneAutoMode::Hold,
        makeInput(onTrackM, coMovingMps, sidewaysM, LaneAutoFrameVelocity(lane, sidewaysM), true));
    TestEqual(TEXT("target left sideways: left"), LaneAutoInt(released.Mode), LaneAutoInt(ESOLLaneAutoMode::Left));
    TestTrue(TEXT("left: the step target is the order target"), LaneAutoNearVec(released.StepTarget.PositionM,
        sidewaysM));
    const FSOLLaneAutoDecision after = LaneAutoDecide(lanes, released.Leg, ESOLLaneAutoMode::Left,
        makeInput(onTrackM, coMovingMps, midM, LaneAutoFrameVelocity(lane, midM), true));
    TestEqual(TEXT("left, the target back in the lane: still left (no re-entry)"), LaneAutoInt(after.Mode),
        LaneAutoInt(ESOLLaneAutoMode::Left));

    // A fresh leg already inside the braking distance never engages
    const double relativeMps = (coMovingMps - LaneAutoFrameVelocity(lane, onTrackM)).Size();
    const double stopM = SOLNewtonian::StoppingDistanceM(relativeMps, plan.BrakeAccelMps2, plan.FlipTimeS);
    const FVector3d closeM = LaneAutoPoint(lane, arcM + 0.5 * stopM, 0.0);
    const FSOLLaneAutoDecision close = LaneAutoDecide(lanes, FSOLLaneAutoLeg(), ESOLLaneAutoMode::None,
        makeInput(onTrackM, coMovingMps, closeM, LaneAutoFrameVelocity(lane, closeM), true));
    TestEqual(TEXT("fresh leg within the braking distance: left"), LaneAutoInt(close.Mode),
        LaneAutoInt(ESOLLaneAutoMode::Left));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoApproachTargetTest, "SOLTest.LaneAutoRules.ApproachTargetNeverOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix: an Approach step targets the merge point only when it is valid; an invalid merge (default, or a stale
// position with bValid false) steps to the order's own target, never the origin
bool FSOLLaneAutoApproachTargetTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d orderM(1.4959787e11, -2.5e7, 3.0e6);
    const FVector3d orderMps(120.0, 340.0, -15.0);
    const FVector3d orderMps2(1.0e-3, -2.0e-3, 0.0);

    const FSOLLaneStepTarget fromInvalid = SOLLaneTraffic::LaneApproachStepTarget(FSOLLaneMergeTarget(), orderM,
        orderMps, orderMps2);
    TestTrue(TEXT("invalid merge: the order's position"), LaneAutoNearVec(fromInvalid.PositionM, orderM));
    TestTrue(TEXT("invalid merge: the order's velocity"), LaneAutoNearVec(fromInvalid.VelocityMps, orderMps));
    TestTrue(TEXT("invalid merge: the order's acceleration"), LaneAutoNearVec(fromInvalid.AccelMps2, orderMps2));
    TestTrue(TEXT("invalid merge: not the origin"), fromInvalid.PositionM.Size() > 1.0e10);

    FSOLLaneMergeTarget staleMerge;
    staleMerge.PositionM = FVector3d(5.0, 6.0, 7.0);
    staleMerge.VelocityMps = FVector3d(1.0, 0.0, 0.0);
    TestTrue(TEXT("a position without bValid is not used"), LaneAutoNearVec(SOLLaneTraffic::LaneApproachStepTarget(
        staleMerge, orderM, orderMps, orderMps2).PositionM, orderM));

    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const FSOLLaneMergeTarget merge = SOLLaneTraffic::LaneMergeTarget(lane, LaneAutoFindOne(lane, lane.Frame.AM,
        lane.Frame.BM));
    const FSOLLaneStepTarget fromValid = SOLLaneTraffic::LaneApproachStepTarget(merge, orderM, orderMps, orderMps2);
    TestTrue(TEXT("valid merge: its position"), merge.bValid && LaneAutoNearVec(fromValid.PositionM, merge.PositionM));
    TestTrue(TEXT("valid merge: its velocity"), LaneAutoNearVec(fromValid.VelocityMps, merge.VelocityMps));
    TestTrue(TEXT("valid merge: its acceleration"), LaneAutoNearVec(fromValid.AccelMps2, merge.AccelMps2));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoOrderEtaTest, "SOLTest.LaneAutoRules.OrderEtaAndClosingSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (route / chase ETA): every AUTO kind shows the lane ETA in Approach and Hold (when it is a usable time)
// and the guidance's own ETA otherwise (None, Left); the closing speed in lane steps is toward the target, not along
// the lane: a target abeam closes at 0 however fast the lane runs
bool FSOLLaneAutoOrderEtaTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::LaneClosingSpeedMps;
    using SOLLaneTraffic::LaneOrderEtaS;
    const double laneS = 1.0e6;
    const double guidanceS = 2.0e5;
    TestEqual(TEXT("approach: the lane ETA"), LaneOrderEtaS(ESOLLaneAutoMode::Approach, laneS, guidanceS), laneS);
    TestEqual(TEXT("hold: the lane ETA"), LaneOrderEtaS(ESOLLaneAutoMode::Hold, laneS, guidanceS), laneS);
    TestEqual(TEXT("none: the guidance ETA"), LaneOrderEtaS(ESOLLaneAutoMode::None, laneS, guidanceS), guidanceS);
    TestEqual(TEXT("left: the guidance ETA"), LaneOrderEtaS(ESOLLaneAutoMode::Left, laneS, guidanceS), guidanceS);
    TestEqual(TEXT("hold, lane ETA 0: the guidance ETA"), LaneOrderEtaS(ESOLLaneAutoMode::Hold, 0.0, guidanceS),
        guidanceS);
    TestEqual(TEXT("approach, lane ETA NaN: the guidance ETA"), LaneOrderEtaS(ESOLLaneAutoMode::Approach,
        LANEAUTO_NAN, guidanceS), guidanceS);
    TestEqual(TEXT("hold, lane ETA infinite: the guidance ETA"), LaneOrderEtaS(ESOLLaneAutoMode::Hold,
        std::numeric_limits<double>::infinity(), guidanceS), guidanceS);

    // Closing speed
    const FVector3d shipMps(3.0e5, 0.0, 0.0);
    TestTrue(TEXT("ahead, moving away slower: v - v_t"), LaneAutoNear(LaneClosingSpeedMps(FVector3d::ZeroVector,
        shipMps, FVector3d(1.0e9, 0.0, 0.0), FVector3d(1.0e3, 0.0, 0.0)), 2.99e5));
    TestTrue(TEXT("abeam: 0, not the along-lane speed"), FMath::Abs(LaneClosingSpeedMps(FVector3d::ZeroVector, shipMps,
        FVector3d(0.0, 1.0e9, 0.0), FVector3d::ZeroVector)) <= 1.0e-6);
    TestTrue(TEXT("behind: receding"), LaneAutoNear(LaneClosingSpeedMps(FVector3d::ZeroVector, shipMps,
        FVector3d(-1.0e9, 0.0, 0.0), FVector3d::ZeroVector), -3.0e5));
    TestEqual(TEXT("coincident: 0"), LaneClosingSpeedMps(FVector3d::ZeroVector, shipMps, FVector3d::ZeroVector,
        FVector3d::ZeroVector), 0.0);
    TestEqual(TEXT("NaN: 0"), LaneClosingSpeedMps(FVector3d(LANEAUTO_NAN, 0.0, 0.0), shipMps,
        FVector3d(1.0e9, 0.0, 0.0), FVector3d::ZeroVector), 0.0);

    // In a lane: a holding ship and a chase target ahead on the same track
    const FSOLLaneMergeLane lane = LaneAutoNominalLane();
    const double arcM = 0.3 * lane.Frame.CurrentLegM;
    const FVector3d holdM = LaneAutoTrackPoint(lane, arcM, 1);
    const FVector3d holdMps = LaneAutoTrackVelocity(lane, holdM, arcM, 1);
    const FVector3d aheadM = LaneAutoTrackPoint(lane, arcM + 1.0e9, 1);
    const FVector3d aheadMps = holdMps - lane.Frame.Axis * 1.0e5;
    TestTrue(TEXT("in a lane: closes at the speed difference"), LaneAutoNear(LaneClosingSpeedMps(holdM, holdMps, aheadM,
        aheadMps), 1.0e5, 1.0e-9));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoChaseActivityTest, "SOLTest.LaneAutoRules.ChaseLaneActivity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix: a chase in a lane step (Approach or Hold) shows the lane activity, not COOL; outside lane steps its own
// activity; the Lane value is appended after Hold and its HUD token is short, upper case and distinct
bool FSOLLaneAutoChaseActivityTest::RunTest(const FString& /*parameters*/)
{
    using SOLChase::DisplayedChaseActivity;
    TestEqual(TEXT("Lane appended after Hold"), LaneAutoInt(ESOLChaseActivity::Lane),
        LaneAutoInt(ESOLChaseActivity::Hold) + 1);
    for (const ESOLChaseActivity activity : { ESOLChaseActivity::Burst, ESOLChaseActivity::Cool,
        ESOLChaseActivity::BrakeBurst, ESOLChaseActivity::BrakeCool, ESOLChaseActivity::Hold })
    {
        TestEqual(FString::Printf(TEXT("activity %d in a lane step: lane"), LaneAutoInt(activity)),
            LaneAutoInt(DisplayedChaseActivity(activity, true)), LaneAutoInt(ESOLChaseActivity::Lane));
        TestEqual(FString::Printf(TEXT("activity %d outside: itself"), LaneAutoInt(activity)),
            LaneAutoInt(DisplayedChaseActivity(activity, false)), LaneAutoInt(activity));
    }

    const FString token = SOLMenuText::CHASE_LANE;
    TestFalse(TEXT("token: not empty"), token.TrimStartAndEnd().IsEmpty());
    TestTrue(TEXT("token: short"), token.Len() <= 16);
    TestEqual(TEXT("token: upper case like the other activities"), token, token.ToUpper());
    TestFalse(TEXT("token: no placeholder"), token.Contains(TEXT("{")));
    for (const TCHAR* other : { SOLMenuText::CHASE_BURST, SOLMenuText::CHASE_COOL, SOLMenuText::CHASE_BRAKE,
        SOLMenuText::CHASE_BRAKE_COOL, SOLMenuText::CHASE_HOLD })
    {
        TestNotEqual(FString::Printf(TEXT("token differs from %s"), other), token, FString(other));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoStopMoonRouteTest, "SOLTest.LaneAutoRules.MidLaneStopSimulatedRoute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (HIGH: mid-lane exit), simulated: a route stop waypoint inside the real Earth - Moon lane, the ship
// holding the traffic on the track from 5 % of the leg, flown through LaneAutoDecide at the 1x flight step and at 10 s
// time-warp substeps: it holds, leaves at least the braking distance short, never re-enters, stops within
// LANEAUTO_OVERSHOOT_BOUND_M of the waypoint and arrives within 20 % of the starting lane ETA
bool FSOLLaneAutoStopMoonRouteTest::RunTest(const FString& /*parameters*/)
{
    FSOLLaneMergeLane lane;
    if (!TestTrue(TEXT("Earth - Moon is a default lane"), LaneAutoRealLane(TEXT("Earth"), TEXT("Moon"),
        LANEAUTO_RADIUS_A_M, LANEAUTO_MOON_RADIUS_M, LANEAUTO_MOON_APOAPSIS_M, lane)))
    {
        return false;
    }
    const double legM = lane.Frame.CurrentLegM;
    const FSOLGuidanceParams plan = LaneAutoBrakePlan();
    const double shipArcM = 0.05 * legM;
    const FVector3d shipM = LaneAutoTrackPoint(lane, shipArcM, 1);
    const FVector3d shipMps = LaneAutoTrackVelocity(lane, shipM, shipArcM, 1);
    const FVector3d targetM = LaneAutoPoint(lane, 0.6 * legM, 0.0);
    const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, shipM, targetM);
    if (!TestTrue(TEXT("leg found, starts inside, target inside"), leg.bFound && leg.bStartsInside
        && leg.bTargetInside))
    {
        return false;
    }
    const double etaS = SOLLaneTraffic::LaneAutoEtaS(lane, leg, shipM, targetM, plan.PlanAccelMps2, plan.FlipTimeS);
    const double boundS = LaneAutoSimBoundS(0.55 * legM, LaneAutoTrackSpeed(lane, shipArcM, 1));
    for (const double dt : { LANEAUTO_REAL_STEP_S, LANEAUTO_WARP_STEP_S })
    {
        const FString label = FString::Printf(TEXT("Earth - Moon route, dt %.4g s"), dt);
        const FLaneAutoSimResult result = LaneAutoSimulate(lane, shipM, shipMps, targetM, FVector3d::ZeroVector, false,
            dt, boundS, lane.Frame.BM);
        TestTrue(label + TEXT(": held the lane first"), result.bHeld);
        LaneAutoCheckStop(*this, label, result, dt);
        TestTrue(FString::Printf(TEXT("%s: arrived at %.6g s, the lane ETA said %.6g s (20 %%)"), *label,
            result.ArrivalS, etaS), FMath::Abs(result.ArrivalS - etaS) <= 0.2 * result.ArrivalS);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoStopMoonChaseTest, "SOLTest.LaneAutoRules.MidLaneStopSimulatedChase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (HIGH: mid-lane exit), simulated: a Chase Down of a target on the track mid-lane in the real Earth - Moon
// lane, moving on at 2 km/s, at the 1x flight step and at 10 s substeps: the chase holds, leaves at least the braking
// distance of its speed relative to the target short, never re-enters, never passes it by more than the bound and
// reaches its tail while the target is still in the lane
bool FSOLLaneAutoStopMoonChaseTest::RunTest(const FString& /*parameters*/)
{
    FSOLLaneMergeLane lane;
    if (!TestTrue(TEXT("Earth - Moon is a default lane"), LaneAutoRealLane(TEXT("Earth"), TEXT("Moon"),
        LANEAUTO_RADIUS_A_M, LANEAUTO_MOON_RADIUS_M, LANEAUTO_MOON_APOAPSIS_M, lane)))
    {
        return false;
    }
    const double legM = lane.Frame.CurrentLegM;
    const double shipArcM = 0.05 * legM;
    const FVector3d shipM = LaneAutoTrackPoint(lane, shipArcM, 1);
    const FVector3d shipMps = LaneAutoTrackVelocity(lane, shipM, shipArcM, 1);
    const double targetArcM = 0.5 * legM;
    const FVector3d targetM = LaneAutoTrackPoint(lane, targetArcM, 1);
    const FVector3d targetMps = lane.Frame.Axis * 2000.0;
    const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, shipM, targetM);
    if (!TestTrue(TEXT("leg found, starts inside, target inside"), leg.bFound && leg.bStartsInside
        && leg.bTargetInside))
    {
        return false;
    }
    const double boundS = LaneAutoSimBoundS(0.6 * legM, LaneAutoTrackSpeed(lane, shipArcM, 1));
    for (const double dt : { LANEAUTO_REAL_STEP_S, LANEAUTO_WARP_STEP_S })
    {
        const FString label = FString::Printf(TEXT("Earth - Moon chase, dt %.4g s"), dt);
        const FLaneAutoSimResult result = LaneAutoSimulate(lane, shipM, shipMps, targetM, targetMps, true, dt, boundS,
            lane.Frame.BM);
        TestTrue(label + TEXT(": held the lane first"), result.bHeld);
        LaneAutoCheckStop(*this, label, result, dt);
        TestTrue(label + TEXT(": the target is still in the lane at the catch"), targetArcM + 2000.0 * result.ArrivalS
            < legM);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoStopMarsTest, "SOLTest.LaneAutoRules.MidLaneStopSimulatedFast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (HIGH: mid-lane exit), simulated at the reported speed: a route stop inside the real Earth - Mars lane
// where the traffic runs at its cap (about 300 km/s), from 2 braking distances short at 10 s substeps and 1.3 of one at
// the 1x flight step: it leaves at least the braking distance short (not at the waypoint's arc, which overshot by about
// v^2 / 2a, 3e9 m, and looped), never re-enters, stops within the bound and arrives within 20 % of the lane ETA
bool FSOLLaneAutoStopMarsTest::RunTest(const FString& /*parameters*/)
{
    FSOLLaneMergeLane lane;
    if (!TestTrue(TEXT("Earth - Mars is a default lane"), LaneAutoRealLane(TEXT("Earth"), TEXT("Mars"),
        LANEAUTO_RADIUS_A_M, LANEAUTO_RADIUS_B_M, LANEAUTO_EARTH_APHELION_M + LANEAUTO_MARS_APHELION_M, lane)))
    {
        return false;
    }
    const double legM = lane.Frame.CurrentLegM;
    const FSOLGuidanceParams plan = LaneAutoBrakePlan();
    const double shipArcM = 0.3 * legM;
    const FVector3d shipM = LaneAutoTrackPoint(lane, shipArcM, 1);
    const FVector3d shipMps = LaneAutoTrackVelocity(lane, shipM, shipArcM, 1);
    const double speedMps = LaneAutoTrackSpeed(lane, shipArcM, 1);
    const double stopM = SOLNewtonian::StoppingDistanceM(speedMps, plan.BrakeAccelMps2, plan.FlipTimeS);
    AddInfo(FString::Printf(TEXT("traffic %.6g m/s, braking distance %.6g m"), speedMps, stopM));
    struct FFastCase
    {
        double Dt;
        double Factor;
        double ExtraM;
    };
    const FFastCase cases[] = { { LANEAUTO_WARP_STEP_S, 2.0, 2.0e8 }, { LANEAUTO_REAL_STEP_S, 1.3, 0.0 } };
    for (const FFastCase& fast : cases)
    {
        const FString label = FString::Printf(TEXT("Earth - Mars route, dt %.4g s, %.1f braking distances short"),
            fast.Dt, fast.Factor);
        const double distanceM = fast.Factor * stopM + fast.ExtraM;
        const FVector3d targetM = LaneAutoPoint(lane, shipArcM + distanceM, 0.0);
        const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, shipM, targetM);
        if (!TestTrue(label + TEXT(": leg found, starts inside, target inside"), leg.bFound && leg.bStartsInside
            && leg.bTargetInside))
        {
            continue;
        }
        const double etaS = SOLLaneTraffic::LaneAutoEtaS(lane, leg, shipM, targetM, plan.PlanAccelMps2,
            plan.FlipTimeS);
        const FLaneAutoSimResult result = LaneAutoSimulate(lane, shipM, shipMps, targetM, FVector3d::ZeroVector, false,
            fast.Dt, LaneAutoSimBoundS(distanceM, speedMps), lane.Frame.BM);
        if (fast.Factor >= 2.0)
        {
            TestTrue(label + TEXT(": held the lane first"), result.bHeld);
        }
        LaneAutoCheckStop(*this, label, result, fast.Dt);
        TestTrue(FString::Printf(TEXT("%s: arrived at %.6g s, the lane ETA said %.6g s (20 %%)"), *label,
            result.ArrivalS, etaS), FMath::Abs(result.ArrivalS - etaS) <= 0.2 * result.ArrivalS);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneAutoEndRingStopTest, "SOLTest.LaneAutoRules.EndRingStopAdapts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (short-lane end-ring stop): Move To the Moon along the real Earth - Moon lane. Leaving at end ring B at
// the wall speed (20 km/s) needs the braking distance at the brake plan (about 1.5e7 m; 1.36e7 m at a full 1.5 G),
// far more than the ring leaves before the Moon's autopilot standoff; so either that room suffices or the stop plan
// adapts: a ship holding half that distance short of the ring leaves, and the simulated stop (1x step and 10 s
// substeps) leaves at least the braking distance short, never re-enters, never passes the standoff point by more than
// the bound and never touches the Moon
bool FSOLLaneAutoEndRingStopTest::RunTest(const FString& /*parameters*/)
{
    FSOLLaneMergeLane lane;
    if (!TestTrue(TEXT("Earth - Moon is a default lane"), LaneAutoRealLane(TEXT("Earth"), TEXT("Moon"),
        LANEAUTO_RADIUS_A_M, LANEAUTO_MOON_RADIUS_M, LANEAUTO_MOON_APOAPSIS_M, lane)))
    {
        return false;
    }
    const FSOLFlightParams params;
    const double legM = lane.Frame.CurrentLegM;
    const FSOLGuidanceParams plan = LaneAutoBrakePlan();
    const FVector3d moonM = lane.Frame.BM;
    const double standoffM = SOLFlight::AutopilotStandoffM(LANEAUTO_MOON_RADIUS_M, LANEAUTO_MOON_GM,
        SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, FSOLGMeterState()));
    const FVector3d targetM = moonM - lane.Frame.Axis * standoffM;
    const double roomM = lane.Profile.EndArcBM - standoffM;
    const double ringStopM = SOLNewtonian::StoppingDistanceM(SOL::NPC_LANE_WALL_SPEED_MPS, plan.BrakeAccelMps2,
        plan.FlipTimeS);
    AddInfo(FString::Printf(TEXT("end ring to standoff %.6g m, braking from the wall speed %.6g m"), roomM, ringStopM));

    // The latched leg from 40 % of the leg
    const double shipArcM = 0.4 * legM;
    const FVector3d shipM = LaneAutoTrackPoint(lane, shipArcM, 1);
    const FVector3d shipMps = LaneAutoTrackVelocity(lane, shipM, shipArcM, 1);
    const FSOLLaneAutoLeg leg = LaneAutoFindOne(lane, shipM, targetM);
    if (!TestTrue(TEXT("leg found, starts inside"), leg.bFound && leg.bStartsInside))
    {
        return false;
    }
    AddInfo(FString::Printf(TEXT("the standoff point is %s the lane"), leg.bTargetInside ? TEXT("inside")
        : TEXT("outside")));

    // Half the wall-speed braking distance short of the ring
    const double nearArcM = legM - 0.5 * ringStopM;
    const FVector3d nearM = LaneAutoTrackPoint(lane, nearArcM, 1);
    const ESOLLaneAutoMode nearMode = LaneAutoMode(lane, leg, nearM, LaneAutoTrackVelocity(lane, nearM, nearArcM, 1),
        targetM, FVector3d::ZeroVector, ESOLLaneAutoMode::Hold);
    TestTrue(FString::Printf(TEXT("the ring leaves room (%.6g m >= %.6g m) or the stop plan adapts (mode %d)"), roomM,
        ringStopM, LaneAutoInt(nearMode)), roomM >= ringStopM || nearMode == ESOLLaneAutoMode::Left);

    // The simulated stop
    const double boundS = LaneAutoSimBoundS(0.6 * legM, LaneAutoTrackSpeed(lane, shipArcM, 1));
    for (const double dt : { LANEAUTO_REAL_STEP_S, LANEAUTO_WARP_STEP_S })
    {
        const FString label = FString::Printf(TEXT("Move To the Moon, dt %.4g s"), dt);
        const FLaneAutoSimResult result = LaneAutoSimulate(lane, shipM, shipMps, targetM, FVector3d::ZeroVector, false,
            dt, boundS, moonM);
        LaneAutoCheckStop(*this, label, result, dt);
        TestTrue(FString::Printf(TEXT("%s: never touches the Moon (closest %.6g m from its centre)"), *label,
            result.MinBodyDistanceM), result.MinBodyDistanceM > LANEAUTO_MOON_RADIUS_M);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
