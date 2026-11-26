/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneRoutePlan.h"

#include <limits>

// The pure plan logic of part 53e (SDD 53 6.4, 6.5): no allocation, no engine state

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns an edge's end-ring distance at one of its nodes and writes the outward direction there (the router's
    // rule)
    double RoutePlanOutwardAt(const FSOLLaneEdge& edge, const bool bAtNodeA, FVector3d& outDirection)
    {
        outDirection = bAtNodeA ? edge.DirectionAB : -edge.DirectionAB;
        return bAtNodeA ? edge.EndRingDistanceAM : edge.EndRingDistanceBM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the transfer time from the lane arriving at a node into the lane leaving it (FindFastestRoute's charge)
    double RoutePlanTransferS(const FSOLLaneEdge& arriving, const int32 arrivingTo, const FSOLLaneEdge& leaving,
        const int32 node, const double wallSpeedMps, const double penaltyS)
    {
        FVector3d arrivingOutward;
        FVector3d leavingOutward;
        const double arrivingRing = RoutePlanOutwardAt(arriving, arriving.NodeA == arrivingTo, arrivingOutward);
        const double leavingRing = RoutePlanOutwardAt(leaving, leaving.NodeA == node, leavingOutward);
        return SOLLaneNetwork::NodeTransferTimeS(arrivingRing, leavingRing, arrivingOutward, leavingOutward,
            wallSpeedMps, penaltyS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes one route hop as a plan hop chained from fromNode; false for a bad edge index, direction or chain
    bool RoutePlanMakeHop(const FSOLLaneHop& hop, TConstArrayView<FSOLLaneEdge> edges, const int32 fromNode,
        FSOLLaneRoutePlanHop& outHop)
    {
        if (!edges.IsValidIndex(hop.Lane) || (hop.DirectionSign != 1 && hop.DirectionSign != -1))
        {
            return false;
        }
        const FSOLLaneEdge& edge = edges[hop.Lane];
        const int32 from = hop.DirectionSign > 0 ? edge.NodeA : edge.NodeB;
        const int32 to = hop.DirectionSign > 0 ? edge.NodeB : edge.NodeA;
        if (from != fromNode || from < 0 || to < 0 || from == to)
        {
            return false;
        }
        outHop.Lane.NodeA = FMath::Min(from, to);
        outHop.Lane.NodeB = FMath::Max(from, to);
        outHop.FromNode = from;
        outHop.ToNode = to;
        outHop.DirectionSign = from == outHop.Lane.NodeA ? 1 : -1;
        outHop.TimeS = edge.TimeS;
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the edge index of a plan hop's lane identity, INDEX_NONE if no edge joins its nodes
    int32 RoutePlanEdgeOf(const FSOLLaneRoutePlanHop& hop, TConstArrayView<FSOLLaneEdge> edges)
    {
        for (int32 index = 0; index < edges.Num(); ++index)
        {
            const FSOLLaneEdge& edge = edges[index];
            if (FMath::Min(edge.NodeA, edge.NodeB) == hop.Lane.NodeA && FMath::Max(edge.NodeA, edge.NodeB)
                == hop.Lane.NodeB)
            {
                return index;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a route's hops to a plan from fromNode (the transfer from the plan's last hop into the first appended,
    // when previousEdge is valid); false if a hop is bad or the plan would overflow
    bool RoutePlanAppend(FSOLLaneRoutePlan& plan, const FSOLLaneRoute& route, TConstArrayView<FSOLLaneEdge> edges,
        int32 fromNode, int32 previousEdge, const double penaltyS, const double wallSpeedMps)
    {
        if (route.HopCount < 0 || plan.HopCount + route.HopCount > SOL::NPC_LANE_ROUTE_MAX_HOPS)
        {
            return false;
        }
        // Each route hop chains from the previous hop's end node; the transfer into it is charged at that node as the
        // router charged it, and the next hop starts where this one ends
        for (int32 index = 0; index < route.HopCount; ++index)
        {
            FSOLLaneRoutePlanHop& hop = plan.Hops[plan.HopCount];
            if (!RoutePlanMakeHop(route.Hops[index], edges, fromNode, hop))
            {
                return false;
            }
            if (edges.IsValidIndex(previousEdge))
            {
                hop.TimeS += RoutePlanTransferS(edges[previousEdge], fromNode, edges[route.Hops[index].Lane], fromNode,
                    wallSpeedMps, penaltyS);
            }
            previousEdge = route.Hops[index].Lane;
            fromNode = hop.ToNode;
            ++plan.HopCount;
        }
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an AUTO order is lane-routed: not War power, a start node and a different target node
bool SOLLaneRoutePlan::ShouldPlanLaneRoute(const bool bWarPower, const int32 startNode, const int32 targetNode)
{
    return !bWarPower && startNode >= 0 && targetNode >= 0 && startNode != targetNode;
}

//////////////////////////////////////////////////////////////////////////
// Writes a plan from a found route (lane identities, directions, hop times); false and an empty plan otherwise
bool SOLLaneRoutePlan::MakePlan(const FSOLLaneRoute& route, TConstArrayView<FSOLLaneEdge> edges,
    const int32 sourceNode, const double penaltyS, const double wallSpeedMps, const int64 epoch, const uint32 serial,
    FSOLLaneRoutePlan& outPlan)
{
    outPlan = FSOLLaneRoutePlan();
    if (!route.bFound || route.Status != ESOLLaneRouteStatus::Found || route.HopCount < 1
        || route.HopCount > SOL::NPC_LANE_ROUTE_MAX_HOPS
        || !RoutePlanAppend(outPlan, route, edges, sourceNode, INDEX_NONE, penaltyS, wallSpeedMps))
    {
        outPlan = FSOLLaneRoutePlan();
        return false;
    }
    outPlan.CurrentHop = 0;
    outPlan.Serial = serial;
    outPlan.PlannedEpoch = epoch;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a plan has a hop left to fly
bool SOLLaneRoutePlan::HasPlan(const FSOLLaneRoutePlan& plan)
{
    return plan.CurrentHop >= 0 && plan.CurrentHop < plan.HopCount;
}

//////////////////////////////////////////////////////////////////////////
// Empties a plan
void SOLLaneRoutePlan::ClearPlan(FSOLLaneRoutePlan& plan)
{
    plan.HopCount = 0;
    plan.CurrentHop = 0;
    plan.bHopLeftEarly = false;
}

//////////////////////////////////////////////////////////////////////////
// Leaves the current hop and returns the lane mode after it: None while hops remain, Left after the last
ESOLLaneAutoMode SOLLaneRoutePlan::LeaveHop(FSOLLaneRoutePlan& plan)
{
    if (!HasPlan(plan))
    {
        return ESOLLaneAutoMode::Left;
    }
    ++plan.CurrentHop;
    plan.bHopLeftEarly = false;
    return HasPlan(plan) ? ESOLLaneAutoMode::None : ESOLLaneAutoMode::Left;
}

//////////////////////////////////////////////////////////////////////////
// Leaves a latched lane for good (a closing lane that is not Sun-safe, or the Sun aim latched): with a later hop the
// hop is left early (lane rules wait until its end node), else per-hop Left (final); the leg is dropped
void SOLLaneRoutePlan::ForceLeaveLane(FSOLLaneRoutePlan& plan, ESOLLaneAutoMode& mode, FSOLLaneAutoLeg& leg)
{
    leg = FSOLLaneAutoLeg();
    if (HasPlan(plan) && plan.CurrentHop + 1 < plan.HopCount)
    {
        plan.bHopLeftEarly = true;
        mode = ESOLLaneAutoMode::None;
        return;
    }
    if (HasPlan(plan))
    {
        LeaveHop(plan);
    }
    mode = ESOLLaneAutoMode::Left;
}

//////////////////////////////////////////////////////////////////////////
// Ends a hop left early once the ship reached its end node (the next hop is then searched); false otherwise
bool SOLLaneRoutePlan::FinishEarlyHop(FSOLLaneRoutePlan& plan, const bool bAtHopNode)
{
    if (!HasPlan(plan) || !plan.bHopLeftEarly || !bAtHopNode)
    {
        return false;
    }
    LeaveHop(plan);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Applies the lane rules' Left to a plan hop: a hop that never held its lane (modeBefore not Hold) and is short of its
// end node with a later hop is left early (ForceLeaveLane), else per-hop Left; returns whether the next hop's search
// runs at once
bool SOLLaneRoutePlan::LeaveHopOnLaneRulesLeft(FSOLLaneRoutePlan& plan, const ESOLLaneAutoMode modeBefore,
    const bool bAtHopNode, ESOLLaneAutoMode& mode, FSOLLaneAutoLeg& leg)
{
    if (!HasPlan(plan))
    {
        mode = ESOLLaneAutoMode::Left;
        return false;
    }
    if (modeBefore != ESOLLaneAutoMode::Hold && !bAtHopNode && plan.CurrentHop + 1 < plan.HopCount)
    {
        // Never held and far from the node: fly on to the hop's end node, no lane search until there
        ForceLeaveLane(plan, mode, leg);
        return false;
    }
    mode = LeaveHop(plan);
    leg = mode == ESOLLaneAutoMode::None ? FSOLLaneAutoLeg() : leg;
    return mode == ESOLLaneAutoMode::None;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an AUTO order arrived this step: an arrival flagged, not a chase, not a lane step, not latched on the
// Sun detour and not short of a later plan hop's node
bool SOLLaneRoutePlan::OrderArrived(const bool bAutopilot, const bool bChase, const bool bLaneStep,
    const bool bSunDetour, const bool bHopTarget, const bool bArrivalFlagged)
{
    return bAutopilot && !bChase && !bLaneStep && !bSunDetour && !bHopTarget && bArrivalFlagged;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the hops after the current one must be re-planned at this epoch
bool SOLLaneRoutePlan::ShouldReplan(const FSOLLaneRoutePlan& plan, const int64 epochNow)
{
    return HasPlan(plan) && plan.CurrentHop + 1 < plan.HopCount && epochNow != plan.PlannedEpoch;
}

//////////////////////////////////////////////////////////////////////////
// Returns the node a re-plan starts from (the current hop's end node), INDEX_NONE without a plan
int32 SOLLaneRoutePlan::ReplanSourceNode(const FSOLLaneRoutePlan& plan)
{
    return HasPlan(plan) ? plan.Hops[plan.CurrentHop].ToNode : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the plan's target node (the last hop's end node), INDEX_NONE without a plan
int32 SOLLaneRoutePlan::PlanTargetNode(const FSOLLaneRoutePlan& plan)
{
    return HasPlan(plan) ? plan.Hops[plan.HopCount - 1].ToNode : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Replaces the hops after the current one with a re-planned route; false (only the current hop kept) on failure
bool SOLLaneRoutePlan::ReplaceRemainingHops(FSOLLaneRoutePlan& plan, const FSOLLaneRoute& route,
    TConstArrayView<FSOLLaneEdge> edges, const double penaltyS, const double wallSpeedMps, const int64 epochNow)
{
    if (!HasPlan(plan))
    {
        return false;
    }
    const int32 source = ReplanSourceNode(plan);
    const int32 target = PlanTargetNode(plan);

    // The current hop moves to the front and stays alone until the route is appended
    plan.Hops[0] = plan.Hops[plan.CurrentHop];
    plan.HopCount = 1;
    plan.CurrentHop = 0;
    ++plan.Serial;
    plan.PlannedEpoch = epochNow;

    const bool bRouteOk = route.bFound && route.Status == ESOLLaneRouteStatus::Found && route.HopCount >= 0
        && route.HopCount < SOL::NPC_LANE_ROUTE_MAX_HOPS;
    if (!bRouteOk)
    {
        return false;
    }
    if (route.HopCount == 0)
    {
        return source == target;
    }
    const FSOLLaneRoutePlanHop currentHop = plan.Hops[0];
    if (!RoutePlanAppend(plan, route, edges, source, RoutePlanEdgeOf(currentHop, edges), penaltyS, wallSpeedMps)
        || plan.Hops[plan.HopCount - 1].ToNode != target)
    {
        plan.HopCount = 1;
        return false;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD ETA of a plan: the current hop's ETA, the later hops' times and the last mile
double SOLLaneRoutePlan::PlanEtaS(const FSOLLaneRoutePlan& plan, const double currentHopEtaS, const double lastMileS)
{
    double sumS = currentHopEtaS;
    if (HasPlan(plan))
    {
        for (int32 hop = plan.CurrentHop + 1; hop < plan.HopCount; ++hop)
        {
            sumS += plan.Hops[hop].TimeS;
        }
    }
    return sumS + lastMileS;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a held hop's lane closes before the hop ends (NaN: true, fail safe)
bool SOLLaneRoutePlan::HopClosesBeforeEnd(const double nowS, const double remainingHopS, const double closeS)
{
    return !(nowS + remainingHopS <= closeS);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether every per-epoch remaining path keeps more than the edge radius from the Sun
bool SOLLaneRoutePlan::HopFinishSafe(TConstArrayView<FVector3d> fromByEpochM, TConstArrayView<FVector3d> toByEpochM,
    const double edgeRadiusM)
{
    if (fromByEpochM.Num() == 0 || fromByEpochM.Num() != toByEpochM.Num())
    {
        return false;
    }
    for (int32 epoch = 0; epoch < fromByEpochM.Num(); ++epoch)
    {
        // Strict and NaN-false: a NaN distance or radius fails the comparison
        if (!(SOLLaneNetwork::SunSegmentDistanceM(fromByEpochM[epoch], toByEpochM[epoch]) > edgeRadiusM))
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a held hop is finished (true) or left at once with the detour (false)
bool SOLLaneRoutePlan::FinishClosingHop(const bool bClosesBeforeEnd, const bool bFinishSafe,
    const bool bPlayerOverride)
{
    return !bClosesBeforeEnd || bFinishSafe || bPlayerOverride;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the Sun safety applies to a player AUTO order of any kind (War power and Chase Down included)
bool SOLLaneRoutePlan::OrderSunSafetyActive(const bool bPlayerOverride, const bool /*bWarPower*/,
    const bool /*bChase*/)
{
    return SOLLaneNetwork::SunSafetyActive(bPlayerOverride, false);
}

//////////////////////////////////////////////////////////////////////////
// Returns the Sun detour waypoint on the edge sphere (the tangent point, at least minArcRad ahead of the ship)
FVector3d SOLLaneRoutePlan::SunDetourWaypointM(const FVector3d& shipM, const FVector3d& shipVelocityMps,
    const FVector3d& targetM, const double edgeRadiusM, const double minArcRad)
{
    const double radiusM = shipM.Size();
    const FVector3d s = radiusM > 0.0 ? shipM / radiusM : FVector3d(0.0, 0.0, 1.0);

    // The in-plane side: the target's part across s (flipped towards the velocity), else the velocity's, else any
    const FVector3d targetAcross = targetM - targetM.Dot(s) * s;
    FVector3d m;
    if (targetAcross.Size() > 1.0)
    {
        // A velocity square to m (within rounding of the unit s) keeps the target's side
        m = targetAcross.GetUnsafeNormal();
        if (shipVelocityMps.Dot(m) < -UE_KINDA_SMALL_NUMBER * shipVelocityMps.Size())
        {
            m = -m;
        }
    }
    else
    {
        const FVector3d velocityAcross = shipVelocityMps - shipVelocityMps.Dot(s) * s;
        const FVector3d sCrossZ = s.Cross(FVector3d(0.0, 0.0, 1.0));
        if (velocityAcross.SizeSquared() > UE_SMALL_NUMBER)
        {
            m = velocityAcross.GetUnsafeNormal();
        }
        else if (sCrossZ.SizeSquared() > UE_SMALL_NUMBER)
        {
            m = sCrossZ.GetUnsafeNormal();
        }
        else
        {
            m = s.Cross(FVector3d(1.0, 0.0, 0.0)).GetSafeNormal();
        }
    }

    // The tangent point's angle from s, never less than the minimum arc ahead
    const double angle = radiusM > edgeRadiusM ? FMath::Max(FMath::Acos(edgeRadiusM / radiusM), minArcRad)
        : minArcRad;
    return edgeRadiusM * (FMath::Cos(angle) * s + FMath::Sin(angle) * m);
}

//////////////////////////////////////////////////////////////////////////
// Returns the closest the ship's motion comes to the Sun: its braking path (ship -> ship + v|v| / 2a, skipped for
// a <= 0) and this substep's swept segment (ship -> ship + v dt, skipped for dt <= 0); infinity when neither runs
double SOLLaneRoutePlan::SunMotionDistanceM(const FVector3d& shipM, const FVector3d& shipVelocityMps,
    const double brakeAccelMps2, const double substepS)
{
    double closestM = std::numeric_limits<double>::infinity();
    if (brakeAccelMps2 > 0.0)
    {
        const FVector3d stopM = shipM + shipVelocityMps * (shipVelocityMps.Size() / (2.0 * brakeAccelMps2));
        closestM = FMath::Min(closestM, SOLLaneNetwork::SunSegmentDistanceM(shipM, stopM));
    }
    if (substepS > 0.0)
    {
        closestM = FMath::Min(closestM, SOLLaneNetwork::SunSegmentDistanceM(shipM, shipM + shipVelocityMps * substepS));
    }
    return closestM;
}

//////////////////////////////////////////////////////////////////////////
// Returns one AUTO step's Sun-safe aim (arrival point, zone exit, latched detour with release at the edge); the ship's
// motion (braking path, swept substep) entering the zone latches the detour too
FSOLSunSafeAim SOLLaneRoutePlan::SunSafeAim(const FVector3d& shipM, const FVector3d& shipVelocityMps,
    const FVector3d& targetM, const bool bSafetyActive, const bool bWasDetouring, const double zoneRadiusM,
    const double edgeRadiusM, const double minArcRad, const double brakeAccelMps2, const double substepS)
{
    FSOLSunSafeAim aim;
    if (!bSafetyActive)
    {
        aim.AimM = targetM;
        return aim;
    }
    const FVector3d arrivalM = SOLLaneNetwork::SunSafeArrivalM(shipM, targetM, zoneRadiusM, edgeRadiusM);
    aim.bStoppedAtZone = targetM.Size() < zoneRadiusM;

    // Inside the zone: straight out along the Sun-to-ship line to the edge sphere
    const double shipRadiusM = shipM.Size();
    if (shipRadiusM < zoneRadiusM)
    {
        aim.AimM = edgeRadiusM * (shipRadiusM > 0.0 ? shipM / shipRadiusM : FVector3d(0.0, 0.0, 1.0));
        aim.bDetour = true;
        return aim;
    }

    // Hysteresis: enter below the zone radius, release once the path clears the edge (or the arrival's or ship's own
    // radius when that is inside the edge, so every trip ends). The ship's motion sets or holds the latch only while it
    // enters the zone itself (a k_on release there could keep a ship circling the edge latched for ever)
    const double distanceM = SOLLaneNetwork::SunSegmentDistanceM(shipM, arrivalM);
    const double releaseM = FMath::Min3(edgeRadiusM, arrivalM.Size(), shipRadiusM);
    const double motionM = SunMotionDistanceM(shipM, shipVelocityMps, brakeAccelMps2, substepS);
    const bool bDetour = FMath::IsNaN(distanceM) || FMath::IsNaN(motionM) || motionM < zoneRadiusM
        || (bWasDetouring ? distanceM < releaseM : distanceM < zoneRadiusM);
    aim.bDetour = bDetour;
    aim.AimM = bDetour ? SunDetourWaypointM(shipM, shipVelocityMps, arrivalM, edgeRadiusM, minArcRad) : arrivalM;
    return aim;
}

//////////////////////////////////////////////////////////////////////////
// Returns the node the HUD's VIA line names (the current hop's end node while a later hop remains), else INDEX_NONE
int32 SOLLaneRoutePlan::ViaNode(const FSOLLaneRoutePlan& plan)
{
    return (HasPlan(plan) && plan.CurrentHop + 1 < plan.HopCount) ? plan.Hops[plan.CurrentHop].ToNode : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Writes the system map polyline (the ship, then each remaining hop's end node) and returns its point count
int32 SOLLaneRoutePlan::PlanPolylineM(const FSOLLaneRoutePlan& plan, TConstArrayView<FVector3d> nodePositionsM,
    const FVector3d& shipM, TArrayView<FVector3d> outPointsM, const FVector3d* const orderTargetM)
{
    if (!HasPlan(plan))
    {
        return 0;
    }
    const int32 count = 1 + plan.HopCount - plan.CurrentHop;
    if (outPointsM.Num() < count)
    {
        return 0;
    }
    // Every remaining hop's end node must have a position (never a partial route)
    for (int32 hop = plan.CurrentHop; hop < plan.HopCount; ++hop)
    {
        if (!nodePositionsM.IsValidIndex(plan.Hops[hop].ToNode))
        {
            return 0;
        }
    }
    // The ship, then the end nodes in hop order; the last point is the order's own target when known
    outPointsM[0] = shipM;
    for (int32 hop = plan.CurrentHop; hop < plan.HopCount; ++hop)
    {
        outPointsM[1 + hop - plan.CurrentHop] = nodePositionsM[plan.Hops[hop].ToNode];
    }
    if (orderTargetM != nullptr)
    {
        outPointsM[count - 1] = *orderTargetM;
    }
    return count;
}
