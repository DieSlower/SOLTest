/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRoutePlanner.h"

namespace
{
    // Legs shorter than this (meters) have no direction: the corner counts as a reversal
    constexpr double ROUTE_PLAN_MIN_LEG_M = 1.0e-6;

    // tan(turn / 2) at or below this is a straight line: the pass speed is the cruise cap
    constexpr double ROUTE_PLAN_STRAIGHT_TAN = 1.0e-12;

    //////////////////////////////////////////////////////////////////////////
    // Returns whether every component of a vector is finite
    bool RoutePlanIsFinite(const FVector3d& value)
    {
        return FMath::IsFinite(value.X) && FMath::IsFinite(value.Y) && FMath::IsFinite(value.Z);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the angle the path turns by at the corner, [0, PI]; PI for a degenerate leg or non-finite input
double SOLRoutePlanner::TurnAngleRad(const FVector3d& fromM, const FVector3d& cornerM, const FVector3d& toM)
{
    if (!RoutePlanIsFinite(fromM) || !RoutePlanIsFinite(cornerM) || !RoutePlanIsFinite(toM))
    {
        return UE_DOUBLE_PI;
    }
    const FVector3d inLeg = cornerM - fromM;
    const FVector3d outLeg = toM - cornerM;
    const double inLength = inLeg.Size();
    const double outLength = outLeg.Size();
    if (inLength < ROUTE_PLAN_MIN_LEG_M || outLength < ROUTE_PLAN_MIN_LEG_M)
    {
        return UE_DOUBLE_PI;
    }

    // atan2 of |cross| and dot stays accurate near 0 and PI (acos of the dot would not)
    const FVector3d inDir = inLeg / inLength;
    const FVector3d outDir = outLeg / outLength;
    return FMath::Atan2(FVector3d::CrossProduct(inDir, outDir).Size(), FVector3d::DotProduct(inDir, outDir));
}

//////////////////////////////////////////////////////////////////////////
// Returns the stop plan
FSOLCornerPlan SOLRoutePlanner::MakeStopPlan()
{
    return FSOLCornerPlan();
}

//////////////////////////////////////////////////////////////////////////
// Plans a fly-through corner from the turn angle, the legs and the lateral G budget (stop plan when too sharp)
FSOLCornerPlan SOLRoutePlanner::PlanCorner(const double turnAngleRad, const double inLegM, const double outLegM,
    const double lateralAccelMps2, const double maxSpeedMps, const FSOLRoutePlannerParams& params)
{
    if (!FMath::IsFinite(turnAngleRad) || !FMath::IsFinite(inLegM) || !FMath::IsFinite(outLegM)
        || !FMath::IsFinite(lateralAccelMps2) || !FMath::IsFinite(maxSpeedMps) || lateralAccelMps2 <= 0.0
        || maxSpeedMps <= 0.0 || inLegM <= 0.0 || outLegM <= 0.0 || turnAngleRad >= params.StopTurnAngleRad)
    {
        return MakeStopPlan();
    }

    // The turn is an arc at the lateral limit whose tangent length (distance from the corner where it starts) is at
    // most captureMax; the pass speed is the arc's speed, the capture radius its tangent length
    const double minCaptureM = FMath::Max(params.MinCaptureRadiusM, 0.0);
    const double captureMaxM = FMath::Max(minCaptureM, params.CaptureLegFraction * FMath::Min(inLegM, outLegM));
    const double tanHalf = FMath::Tan(0.5 * FMath::Max(turnAngleRad, 0.0));
    FSOLCornerPlan plan;
    plan.bStop = false;
    plan.PassSpeedMps = tanHalf <= ROUTE_PLAN_STRAIGHT_TAN ? maxSpeedMps
        : FMath::Min(maxSpeedMps, FMath::Sqrt(lateralAccelMps2 * captureMaxM / tanHalf));
    plan.CaptureRadiusM = FMath::Clamp(plan.PassSpeedMps * plan.PassSpeedMps / lateralAccelMps2 * tanHalf, minCaptureM,
        captureMaxM);
    return plan;
}

//////////////////////////////////////////////////////////////////////////
// Returns the corner G budget: a human at the sustained limit (and the hull cap), an AI at the hull cap
double SOLRoutePlanner::CornerAccelBudgetMps2(const ESOLPilotKind kind, const double pilotSustainedG,
    const double hullCapG)
{
    if (!FMath::IsFinite(hullCapG) || hullCapG <= 0.0)
    {
        return 0.0;
    }
    if (kind == ESOLPilotKind::AI)
    {
        return hullCapG * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    }
    if (!FMath::IsFinite(pilotSustainedG) || pilotSustainedG <= 0.0)
    {
        return 0.0;
    }
    return FMath::Min(pilotSustainedG, hullCapG) * SOLNewtonian::STANDARD_GRAVITY_MPS2;
}

//////////////////////////////////////////////////////////////////////////
// Returns the time to fly a leg from entry to exit speed at a constant accel / brake under a cruise cap
double SOLRoutePlanner::LegTimeS(const double distanceM, const double entrySpeedMps, const double exitSpeedMps,
    const double accelMps2, const double cruiseCapMps)
{
    if (!FMath::IsFinite(distanceM) || !FMath::IsFinite(entrySpeedMps) || !FMath::IsFinite(exitSpeedMps)
        || !FMath::IsFinite(accelMps2) || !FMath::IsFinite(cruiseCapMps) || distanceM < 0.0 || accelMps2 <= 0.0
        || cruiseCapMps <= 0.0)
    {
        return TNumericLimits<double>::Max();
    }
    if (distanceM == 0.0)
    {
        return 0.0;
    }
    const double a = accelMps2;
    const double d = distanceM;
    const double v0 = FMath::Clamp(entrySpeedMps, 0.0, cruiseCapMps);
    const double v1 = FMath::Clamp(exitSpeedMps, 0.0, cruiseCapMps);

    // One ramp: the exit speed cannot be reached, or braked down to, within the distance
    if (v1 * v1 > v0 * v0 + 2.0 * a * d)
    {
        return (FMath::Sqrt(v0 * v0 + 2.0 * a * d) - v0) / a;
    }
    if (v0 * v0 > v1 * v1 + 2.0 * a * d)
    {
        return (v0 - FMath::Sqrt(v0 * v0 - 2.0 * a * d)) / a;
    }

    // Triangle up to the peak speed, or a trapezoid that cruises at the cap
    const double peakMps = FMath::Sqrt(0.5 * (2.0 * a * d + v0 * v0 + v1 * v1));
    if (peakMps <= cruiseCapMps)
    {
        return (2.0 * peakMps - v0 - v1) / a;
    }
    const double cap = cruiseCapMps;
    const double rampsM = (cap * cap - v0 * v0) / (2.0 * a) + (cap * cap - v1 * v1) / (2.0 * a);
    return (2.0 * cap - v0 - v1) / a + (d - rampsM) / cap;
}

//////////////////////////////////////////////////////////////////////////
// Returns how far along a leg the ship is, in [0, 1]
double SOLRoutePlanner::LegFraction(const double remainingM, const double legLengthM)
{
    if (!(legLengthM > 0.0))
    {
        return 1.0;
    }
    return FMath::Clamp(1.0 - remainingM / legLengthM, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns a Move's overall progress in [0, 1]
double SOLRoutePlanner::RouteProgress(const int32 legsCompleted, const int32 totalLegs, const double legFraction)
{
    if (totalLegs <= 0)
    {
        return 0.0;
    }
    const double fraction = FMath::IsFinite(legFraction) ? legFraction : 0.0;
    return FMath::Clamp((static_cast<double>(legsCompleted) + fraction) / totalLegs, 0.0, 1.0);
}
