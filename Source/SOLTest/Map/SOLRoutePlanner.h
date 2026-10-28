/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLNewtonianFlight.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"

// Issue #20 route planner (SDD 20 section 2.3): the fly-through speed and capture radius of a corner from its turn
// angle and the G budget, leg time estimates, and route progress. Pure logic: no UObjects, no allocation.

// Corner-planning tunables (defaults from SOL::Route)
struct FSOLRoutePlannerParams
{
    double StopTurnAngleRad = SOL::Route::STOP_TURN_ANGLE_RAD;     // Sharper turns stop instead of flying through
    double CaptureLegFraction = SOL::Route::CAPTURE_LEG_FRACTION;  // Capture radius cap: this x the shorter leg
    double MinCaptureRadiusM = SOL::Route::MIN_CAPTURE_RADIUS_M;   // Capture radius floor of a fly-through
};

// How the ship passes one waypoint
struct FSOLCornerPlan
{
    bool bStop = true;              // Come to rest at the waypoint (the autopilot's normal arrival)
    double PassSpeedMps = 0.0;      // Fly-through: the speed the ship may still have at the waypoint (0 when stopping)
    double CaptureRadiusM = 0.0;    // Fly-through: the next leg starts within this distance (0 when stopping)
};

namespace SOLRoutePlanner
{
    // Returns the angle (radians, [0, PI]) the path turns by at the corner: between (corner - from) and
    // (to - corner). PI when either leg is shorter than 1e-6 m or any input is non-finite (treated as a reversal)
    SOLTEST_API double TurnAngleRad(const FVector3d& fromM, const FVector3d& cornerM, const FVector3d& toM);

    // Returns the stop plan (bStop, no pass speed, no capture radius)
    SOLTEST_API FSOLCornerPlan MakeStopPlan();

    // Plans a fly-through corner. Stop plan when the turn is at least StopTurnAngleRad, or on invalid input (non-finite
    // values, accel, max speed or a leg length <= 0). Otherwise, with captureMax = max(MinCaptureRadiusM,
    // CaptureLegFraction * min(inLegM, outLegM)) and t = tan(turn / 2): the pass speed is the lateral-acceleration
    // limited speed of a turn whose tangent length is captureMax, sqrt(accel * captureMax / t), capped at maxSpeedMps
    // (maxSpeedMps itself when t is ~0, a straight line); the capture radius is that turn's tangent length,
    // pass^2 / accel * t, clamped to [MinCaptureRadiusM, captureMax]
    SOLTEST_API FSOLCornerPlan PlanCorner(double turnAngleRad, double inLegM, double outLegM,
        double lateralAccelMps2, double maxSpeedMps, const FSOLRoutePlannerParams& params);

    // Returns the corner G budget in m/s^2: a human flies at most pilotSustainedG (and the hull's cap), an AI at the
    // hull's cap; 0 for invalid (non-finite or non-positive) limits
    SOLTEST_API double CornerAccelBudgetMps2(ESOLPilotKind kind, double pilotSustainedG, double hullCapG);

    // Returns the time to fly a leg of distanceM from entrySpeed to exitSpeed (both clamped to [0, cruiseCap]) at a
    // constant accel / brake, cruising at most at cruiseCap: a trapezoid or triangle profile; when the exit speed
    // cannot be reached (or braked to) within the distance, the whole leg is one ramp. 0 for distance 0; the largest
    // double for invalid input (non-finite, negative distance, accel or cruise cap <= 0)
    SOLTEST_API double LegTimeS(double distanceM, double entrySpeedMps, double exitSpeedMps, double accelMps2,
        double cruiseCapMps);

    // Returns how far along a leg the ship is, 1 - remaining / length clamped to [0, 1] (1 for a zero-length leg)
    SOLTEST_API double LegFraction(double remainingM, double legLengthM);

    // Returns a Move's overall progress in [0, 1]: (legsCompleted + legFraction) / totalLegs, clamped; 0 when
    // totalLegs <= 0
    SOLTEST_API double RouteProgress(int32 legsCompleted, int32 totalLegs, double legFraction);
}
