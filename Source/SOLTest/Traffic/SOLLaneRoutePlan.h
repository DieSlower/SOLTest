/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"
#include "Traffic/SOLLaneTrafficMath.h"

// Part 53e (SDD 53 6.4, 6.5): the player's AUTO lane route plan and the Sun safety of every AUTO flight. Pure logic
// in universe ecliptic metres and sim seconds, no allocation (the spec is in Tests/LaneRouteAutoTest.cpp)

// One hop of a plan: its lane identity (the sorted node pair, so a hop survives a slot change), its direction and time
struct FSOLLaneRoutePlanHop
{
    FSOLLanePair Lane;              // NodeA < NodeB
    int32 FromNode = INDEX_NONE;
    int32 ToNode = INDEX_NONE;
    int32 DirectionSign = 1;        // +1 = Lane.NodeA -> Lane.NodeB
    double TimeS = 0.0;             // The edge time plus the router's transfer into this hop (0 for the first hop)
};

// The player's transient lane route plan (never a UPROPERTY, never saved)
struct FSOLLaneRoutePlan
{
    int32 HopCount = 0;
    int32 CurrentHop = 0;
    uint32 Serial = 0;              // The order serial at planning, + 1 at every re-plan
    int64 PlannedEpoch = 0;         // The lane epoch the hops after the current one were planned at
    bool bHopLeftEarly = false;     // The current hop's lane was left before its end (a closing lane, the Sun aim):
                                    // AUTO flies on to the hop's end node, where the hop ends and the plan is re-made
    FSOLLaneRoutePlanHop Hops[SOL::NPC_LANE_ROUTE_MAX_HOPS];
};

// One AUTO step's Sun-safe aim
struct FSOLSunSafeAim
{
    FVector3d AimM = FVector3d::ZeroVector;     // Where this step flies to
    bool bDetour = false;                       // A detour (or zone exit) aim: latch it for the next step
    bool bStoppedAtZone = false;                // The order's target lies inside the zone (the HUD notice)
};

namespace SOLLaneRoutePlan
{
    // Returns whether an AUTO order is lane-routed: not War power, a start node and a different target node
    SOLTEST_API bool ShouldPlanLaneRoute(bool bWarPower, int32 startNode, int32 targetNode);

    // Writes a plan from a found route (lane identities, directions, hop times); false and an empty plan otherwise
    SOLTEST_API bool MakePlan(const FSOLLaneRoute& route, TConstArrayView<FSOLLaneEdge> edges, int32 sourceNode,
        double penaltyS, double wallSpeedMps, int64 epoch, uint32 serial, FSOLLaneRoutePlan& outPlan);

    // Returns whether a plan has a hop left to fly
    SOLTEST_API bool HasPlan(const FSOLLaneRoutePlan& plan);

    // Empties a plan
    SOLTEST_API void ClearPlan(FSOLLaneRoutePlan& plan);

    // Leaves the current hop and returns the lane mode after it: None while hops remain, Left after the last
    SOLTEST_API ESOLLaneAutoMode LeaveHop(FSOLLaneRoutePlan& plan);

    // Leaves a latched lane for good (a closing lane that is not Sun-safe, or the Sun aim latched): with a later hop
    // the hop is left early (lane rules wait until its end node), else per-hop Left (final); the leg is dropped
    SOLTEST_API void ForceLeaveLane(FSOLLaneRoutePlan& plan, ESOLLaneAutoMode& mode, FSOLLaneAutoLeg& leg);

    // Ends a hop left early once the ship reached its end node (the next hop is then searched); false otherwise
    SOLTEST_API bool FinishEarlyHop(FSOLLaneRoutePlan& plan, bool bAtHopNode);

    // Applies the lane rules' Left to a plan hop: a hop that never held its lane (modeBefore not Hold) and is short of
    // its end node with a later hop is left early (ForceLeaveLane), else per-hop Left; returns whether the next hop's
    // search runs at once
    SOLTEST_API bool LeaveHopOnLaneRulesLeft(FSOLLaneRoutePlan& plan, ESOLLaneAutoMode modeBefore, bool bAtHopNode,
        ESOLLaneAutoMode& mode, FSOLLaneAutoLeg& leg);

    // Returns whether an AUTO order arrived this step: an arrival flagged, not a chase, not a lane step, not latched
    // on the Sun detour and not short of a later plan hop's node
    SOLTEST_API bool OrderArrived(bool bAutopilot, bool bChase, bool bLaneStep, bool bSunDetour, bool bHopTarget,
        bool bArrivalFlagged);

    // Returns whether the hops after the current one must be re-planned at this epoch
    SOLTEST_API bool ShouldReplan(const FSOLLaneRoutePlan& plan, int64 epochNow);

    // Returns the node a re-plan starts from (the current hop's end node), INDEX_NONE without a plan
    SOLTEST_API int32 ReplanSourceNode(const FSOLLaneRoutePlan& plan);

    // Returns the plan's target node (the last hop's end node), INDEX_NONE without a plan
    SOLTEST_API int32 PlanTargetNode(const FSOLLaneRoutePlan& plan);

    // Replaces the hops after the current one with a re-planned route; false (only the current hop kept) on failure
    SOLTEST_API bool ReplaceRemainingHops(FSOLLaneRoutePlan& plan, const FSOLLaneRoute& route,
        TConstArrayView<FSOLLaneEdge> edges, double penaltyS, double wallSpeedMps, int64 epochNow);

    // Returns the HUD ETA of a plan: the current hop's ETA, the later hops' times and the last mile
    SOLTEST_API double PlanEtaS(const FSOLLaneRoutePlan& plan, double currentHopEtaS, double lastMileS);

    // Returns whether a held hop's lane closes before the hop ends (NaN: true, fail safe)
    SOLTEST_API bool HopClosesBeforeEnd(double nowS, double remainingHopS, double closeS);

    // Returns whether every per-epoch remaining path keeps more than the edge radius from the Sun
    SOLTEST_API bool HopFinishSafe(TConstArrayView<FVector3d> fromByEpochM, TConstArrayView<FVector3d> toByEpochM,
        double edgeRadiusM);

    // Returns whether a held hop is finished (true) or left at once with the detour (false)
    SOLTEST_API bool FinishClosingHop(bool bClosesBeforeEnd, bool bFinishSafe, bool bPlayerOverride);

    // Returns whether the Sun safety applies to a player AUTO order of any kind (War power and Chase Down included)
    SOLTEST_API bool OrderSunSafetyActive(bool bPlayerOverride, bool bWarPower, bool bChase);

    // Returns the Sun detour waypoint on the edge sphere (the tangent point, at least minArcRad ahead of the ship)
    SOLTEST_API FVector3d SunDetourWaypointM(const FVector3d& shipM, const FVector3d& shipVelocityMps,
        const FVector3d& targetM, double edgeRadiusM, double minArcRad);

    // Returns the closest the ship's motion comes to the Sun: its braking path (ship -> ship + v|v| / 2a, skipped for
    // a <= 0) and this substep's swept segment (ship -> ship + v dt, skipped for dt <= 0); infinity when neither runs
    SOLTEST_API double SunMotionDistanceM(const FVector3d& shipM, const FVector3d& shipVelocityMps,
        double brakeAccelMps2, double substepS);

    // Returns one AUTO step's Sun-safe aim (arrival point, zone exit, latched detour with release at the edge); the
    // ship's motion (braking path, swept substep) entering the zone latches the detour too
    SOLTEST_API FSOLSunSafeAim SunSafeAim(const FVector3d& shipM, const FVector3d& shipVelocityMps,
        const FVector3d& targetM, bool bSafetyActive, bool bWasDetouring, double zoneRadiusM, double edgeRadiusM,
        double minArcRad, double brakeAccelMps2, double substepS);

    // Returns the node the HUD's VIA line names (the current hop's end node while a later hop remains), else INDEX_NONE
    SOLTEST_API int32 ViaNode(const FSOLLaneRoutePlan& plan);

    // Writes the system map polyline (the ship, then each remaining hop's end node, the last one replaced by the
    // order's own target when given, e.g. a moon of the last node) and returns its point count
    SOLTEST_API int32 PlanPolylineM(const FSOLLaneRoutePlan& plan, TConstArrayView<FVector3d> nodePositionsM,
        const FVector3d& shipM, TArrayView<FVector3d> outPointsM, const FVector3d* orderTargetM = nullptr);
}
