/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Flight/SOLShipState.h"
#include "Interaction/SOLActionTable.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "Ship/SOLAutopilotOrder.h"
#include "Ship/SOLShipFragments.h"
#include "Ship/SOLShipSubsystem.h"
#include "Traffic/SOLLaneNetworkMath.h"
#include "Traffic/SOLLaneRoutePlan.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for issue #53 part 53e "AUTO routing and Sun safety" (SDD 53 6.4, 6.5, section 16-18 decisions; plan 53e),
// written test first against the SDD before the production code existed. They cover only 53e behaviour: the plan
// (creation, lane identities, per-hop Left, the straight search off, re-plan at an epoch change, ETA, transient state
// and its reset), the held hop that closes (finish if safe), the Sun detour of every AUTO flight (War power included)
// and the override, the "Toggle Sun Safety" row, the session flag, the HUD lines and the system map polyline. The
// router itself (FindFastestRoute) and case 15 (PathEntersSunZone, SunSafetyActive, SunSafeArrivalM) are covered in
// LaneNetworkMathTest.cpp.
//
// Review fixes (53e review, 2026-11-26): the tests ClosingLaneRelease, NoArrivalWhileLatched,
// BrakingPathAndSweptSubstep and MidFrameHopAim, the moon point of MapPolyline, the notice and flag resets of
// PlanTransientAndReset, and the Sun-aim step of DetourAutopilotAndWarPower were written AFTER the production fix by
// its implementer (test-after, not TDD), against the review's findings and SDD 53 6.5. FindHopSlot and
// IsStraightSearchAllowed were deleted (unused in production) with their cases. Added contract:
// ForceLeaveLane(plan, mode, leg) (later hop: bHopLeftEarly, None; else LeaveHop and Left; leg dropped), FinishEarlyHop(plan, bAtHopNode), OrderArrived(bAutopilot, bChase, bLaneStep,
// bSunDetour, bHopTarget, bArrivalFlagged), SunMotionDistanceM(ship, v, brakeAccel, dt), SunSafeAim(..., brakeAccel,
// dt) (the motion entering the zone latches; it never holds the latch outside the zone), PlanPolylineM(...,
// orderTargetM) (replaces the last point).
//
// Smoke fix (53e, 2026-11-26, test-after with the fix): LeaveHopOnLaneRulesLeft(plan, modeBefore, bAtHopNode, mode,
// leg): the lane rules' Left on a hop that never held (modeBefore not Hold), away from its end node, with a later hop
// -> ForceLeaveLane (left early, no search); else LeaveHop (search when a hop remains); no plan -> Left. Test
// NeverHeldHopLeftEarly.
//
// API contract (from the test author; the implementer follows the names; part 53e stubs return wrong values):
//
// Traffic/SOLLaneRoutePlan.h (new, pure, no allocation):
//   FSOLLaneRoutePlanHop { FSOLLanePair Lane (sorted, NodeA < NodeB); FromNode; ToNode; DirectionSign (+1 = Lane.NodeA
//     -> Lane.NodeB); double TimeS = the edge time plus the router's transfer cost into this hop (the same
//     NodeTransferTimeS FindFastestRoute charges at that node), 0 transfer for the first hop }
//   FSOLLaneRoutePlan { HopCount; CurrentHop; uint32 Serial; int64 PlannedEpoch; Hops[NPC_LANE_ROUTE_MAX_HOPS] }
//   ShouldPlanLaneRoute(bWarPower, startNode, targetNode): !bWarPower && startNode >= 0 && targetNode >= 0 && start !=
//     target (Q11: the ship at a node or holding a lane; the caller resolves a body under a node to that node)
//   MakePlan(route, edges, sourceNode, penaltyS, wallSpeedMps, epoch, serial, out): a found route with 1..MAX hops
//     whose hops chain from sourceNode through valid edge indices -> true, CurrentHop 0, Serial = serial,
//     PlannedEpoch = epoch; anything else -> false and an empty plan (HopCount 0). Sum of hop TimeS == route.TimeS
//   HasPlan = CurrentHop < HopCount; ClearPlan empties it
//   LeaveHop(plan): per-hop Left: CurrentHop + 1 and None while a hop remains, Left after the last (final as today);
//     Left without a plan
//   ShouldReplan(plan, epochNow): HasPlan, a hop after the current one remains, epochNow != PlannedEpoch (Q14)
//   ReplanSourceNode = Hops[CurrentHop].ToNode; PlanTargetNode = Hops[HopCount - 1].ToNode (INDEX_NONE without a plan)
//   ReplaceRemainingHops(plan, route, edges, penaltyS, wallSpeedMps, epochNow): keeps the current hop and appends the
//     route's hops (from ReplanSourceNode to PlanTargetNode, transfer into the first included), CurrentHop 0,
//     Serial + 1, PlannedEpoch = epochNow -> true; a route not found, not from the source, not to the target or too
//     long -> false, the current hop alone kept (later the order flies on as today), Serial + 1,
//     PlannedEpoch = epochNow
//   PlanEtaS(plan, currentHopEtaS, lastMileS) = currentHopEtaS + sum of TimeS over the hops after the current one +
//     lastMileS (Q13)
//   HopClosesBeforeEnd(nowS, remainingHopS, closeS) = !(now + remaining <= close) (NaN: true, fail safe)
//   HopFinishSafe(fromByEpochM, toByEpochM, edgeRadiusM): equal, non-zero counts and SunSegmentDistanceM(from[k],
//     to[k]) > edge for every epoch k (strict, NaN false; SDD 6.5 "more than k_on R_sun")
//   FinishClosingHop(bClosesBeforeEnd, bFinishSafe, bPlayerOverride) = !closes || safe || override (false: AUTO leaves
//     the hop at once, flies to its end node with the detour and re-plans there). CheckLaneLatch's Hold Keep (contract
//     case 24) stays: this is the extra 53e decision the processor takes for a held hop
//   OrderSunSafetyActive(bPlayerOverride, bWarPower, bChase) = SunSafetyActive(bPlayerOverride, false): every AUTO
//     order kind obeys the safety (O4), War power included
//   SunDetourWaypointM(ship, velocity, target, edgeRadiusM, minArcRad): s = unit(ship), r = |ship|; the in-plane side
//     m = unit(target - (target . s) s) (the target's side), flipped when velocity . m < 0 (O4: the side nearer the
//     velocity); when the target's part across s is at most 1 m (collinear): m = unit(v - (v . s) s), else unit(s x
//     +Z), else unit(s x +X). Angle a = max(acos(edge / r), minArcRad) for r > edge (the tangent point, never less than
//     minArcRad ahead), minArcRad for r <= edge. W = edge (cos a s + sin a m). |W| = edge. The production minimum arc
//     must keep edge cos(minArc) > zone (below about 24.6 degrees; these tests pass 15): a pursuit settles at that
//     radius, so add a static_assert or test on the real constant
//   SunSafeAim(ship, velocity, target, bSafetyActive, bWasDetouring, zone, edge, minArcRad) -> { AimM, bDetour,
//     bStoppedAtZone }:
//     1. safety off: the target itself, bit for bit, no flags (paths stay bit-identical, NoLaneBitIdentical);
//     2. arrival = SunSafeArrivalM(ship, target, zone, edge); bStoppedAtZone = |target| < zone;
//     3. ship inside the zone (|ship| < zone): aim edge x unit(ship) (+Z at the origin), bDetour true (zone exit);
//     4. d = SunSegmentDistanceM(ship, arrival); detour when (bWasDetouring ? d < min(edge, |arrival|, |ship|) :
//        d < zone) or d is NaN (hysteresis: enter at k_off, release at k_on, or at the arrival's or ship's own radius
//        inside k_on so every trip ends);
//     5. detour: AimM = SunDetourWaypointM(ship, velocity, arrival, edge, minArcRad), bDetour true; else the arrival
//        (bit for bit the target when it is outside the zone), bDetour false.
//   ViaNode(plan): Hops[CurrentHop].ToNode while a hop after the current one remains, else INDEX_NONE (Q16)
//   PlanPolylineM(plan, nodePositionsM, shipM, outPointsM): ship, then the current position of each remaining hop's
//     ToNode (rebuilt each frame from the moving nodes); 0 (nothing drawn) without a plan, for a node outside the
//     positions or when outPointsM is too small (never a partial route). Drawn in a new ESOLRouteDrawStyle, width in
//     pixels (not tested here: colour, dash and width are the implementer's)
// Ship/SOLShipFragments.h: FSOLShipStateFragment gains `FSOLLaneRoutePlan LanePlan;` and `bool bSunDetour = false;`
//   (plain fields, never a UPROPERTY, never saved)
// Ship/SOLAutopilotOrder.h: ResetLaneState(FSOLShipStateFragment&) resets LaneLeg, LaneMode, WarPowerPulse, LanePlan
//   and bSunDetour (USOLShipSubsystem::ResetLaneState calls it at every order write); SunSafetyLineText(bOverridden) ->
//   SOLMenuText::AUTOPILOT_SUN_SAFETY_OFF ("SUN SAFETY OFF") or ""; AppendViaLine(out, name) appends
//   SOLMenuText::AUTOPILOT_VIA_PREFIX ("VIA ") and the name
// Ship/SOLShipSubsystem.h: IsSunSafetyOverridden() (default false) and ToggleSunSafetyOverride(), a plain bool member
//   (session only: not a UPROPERTY, no settings field, no key binding; SAVE_VERSION 13, KEY_BINDING_LAYOUT_VERSION 10)
// Interaction/SOLActionTable.h: ESOLAction::SunSafetyOverride appended after MoveToWarPower (this file derives it as
//   MoveToWarPower + 1 so it builds before the enumerator exists; replace with the name once it does), offered live on
//   Star, Planet, Moon and Dwarf right after MoveToWarPower, not on Asteroid, Ship or DroppedTarget, blocked only by
//   NoShip, label "Toggle Sun Safety" (SOLMenuText::INTERACT_ACTION_LABELS); SOL::Interact::MAX_ACTIONS 11 (with
//   ESOLHudLine::RadialItem10; its static_assert ties it)
//
// Stale after 53e (re-validate, not edited here): InteractMenusTest.cpp:314 "MAX_ACTIONS is 10";
// WarPowerMoveTest.cpp:351 "the last before Count" (MoveToWarPower is no longer last).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double LANEROUTE_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double LANEROUTE_INF = std::numeric_limits<double>::infinity();
    constexpr double LANEROUTE_ZONE_M = 1.3914e10;      // 20 Sun radii (k_off), the case 15 literal
    constexpr double LANEROUTE_EDGE_M = 1.53054e10;     // 22 Sun radii (k_on), the case 15 literal
    constexpr double LANEROUTE_TIGHT = 1.0e-9;          // Relative tolerance of closed-form values
    constexpr double LANEROUTE_PENALTY_S = 3.0;         // Transfer penalty of the synthetic graph cases
    constexpr double LANEROUTE_WALL_MPS = 2.0e4;

    // The minimum detour arc ahead of the ship (15 degrees: the pursuit settles at edge cos(15) = 1.478e10, above the
    // zone; 30 degrees would settle at 1.325e10, inside it)
    const double LANEROUTE_MIN_ARC_RAD = FMath::DegreesToRadians(15.0);

    // The body types that carry the AUTO orders (and the Toggle Sun Safety row)
    const ESOLObjectType LANEROUTE_BODY_TYPES[] = { ESOLObjectType::Star, ESOLObjectType::Planet, ESOLObjectType::Moon,
        ESOLObjectType::Dwarf };

    //////////////////////////////////////////////////////////////////////////
    // Returns the Toggle Sun Safety action (ESOLAction::SunSafetyOverride, appended after MoveToWarPower)
    ESOLAction LaneRouteSunAction()
    {
        return ESOLAction::SunSafetyOverride;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks actual against expected within a relative tolerance (absolute when expected is 0); NaN never matches
    bool LaneRouteNear(FAutomationTestBase& test, const FString& what, const double actual, const double expected,
        const double relTol)
    {
        const double tol = (expected == 0.0) ? relTol : relTol * FMath::Abs(expected);
        if (FMath::Abs(actual - expected) <= tol)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected %.15g, got %.15g"), *what, expected, actual));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a vector componentwise within tol x max(|expected|, 1)
    bool LaneRouteVecNear(FAutomationTestBase& test, const FString& what, const FVector3d& actual,
        const FVector3d& expected, const double relTol)
    {
        const double tol = relTol * FMath::Max(expected.Size(), 1.0);
        if (actual.ContainsNaN() || FMath::Abs(actual.X - expected.X) > tol || FMath::Abs(actual.Y - expected.Y) > tol
            || FMath::Abs(actual.Z - expected.Z) > tol)
        {
            test.AddError(FString::Printf(TEXT("%s: expected (%.15g, %.15g, %.15g), got (%.15g, %.15g, %.15g)"), *what,
                expected.X, expected.Y, expected.Z, actual.X, actual.Y, actual.Z));
            return false;
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a vector is bit for bit the expected one
    bool LaneRouteVecExact(FAutomationTestBase& test, const FString& what, const FVector3d& actual,
        const FVector3d& expected)
    {
        if (actual.X == expected.X && actual.Y == expected.Y && actual.Z == expected.Z)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected exactly (%.17g, %.17g, %.17g), got (%.17g, %.17g, %.17g)"),
            *what, expected.X, expected.Y, expected.Z, actual.X, actual.Y, actual.Z));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an edge of the synthetic graph (direction +X, end rings at the node centres)
    FSOLLaneEdge LaneRouteEdge(const int32 a, const int32 b, const double timeS)
    {
        FSOLLaneEdge edge;
        edge.NodeA = a;
        edge.NodeB = b;
        edge.TimeS = timeS;
        edge.DirectionAB = FVector3d(1.0, 0.0, 0.0);
        return edge;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's synthetic routing graph (case 9): e0 0-1 10, e1 1-2 10, e2 0-2 25, e3 2-3 5, e4 1-3 30,
    // e5 3-4 1; with the 3 s penalty 0 -> 3 runs e0, e1, e3 in 31 s
    TArray<FSOLLaneEdge> LaneRouteGraph()
    {
        return { LaneRouteEdge(0, 1, 10.0), LaneRouteEdge(1, 2, 10.0), LaneRouteEdge(0, 2, 25.0),
            LaneRouteEdge(2, 3, 5.0), LaneRouteEdge(1, 3, 30.0), LaneRouteEdge(3, 4, 1.0) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the fastest route over a graph at the case penalty and wall speed, starting at time 0
    FSOLLaneRoute LaneRouteFind(const TArray<FSOLLaneEdge>& graph, const int32 source, const int32 target)
    {
        return SOLLaneNetwork::FindFastestRoute(5, graph, LANEROUTE_PENALTY_S, LANEROUTE_WALL_MPS, 0.0, source,
            target);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane pair
    FSOLLanePair LaneRoutePair(const int32 a, const int32 b)
    {
        FSOLLanePair pair;
        pair.NodeA = a;
        pair.NodeB = b;
        return pair;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the plan 0 -> 3 over the synthetic graph (epoch 7, serial 42); false checks report a failed MakePlan
    FSOLLaneRoutePlan LaneRoutePlan03(FAutomationTestBase& test)
    {
        FSOLLaneRoutePlan plan;
        const TArray<FSOLLaneEdge> graph = LaneRouteGraph();
        test.TestTrue(TEXT("plan 0 -> 3 made"), SOLLaneRoutePlan::MakePlan(LaneRouteFind(graph, 0, 3), graph, 0,
            LANEROUTE_PENALTY_S, LANEROUTE_WALL_MPS, 7, 42u, plan));
        return plan;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks one plan hop (guarded by the hop count)
    void LaneRouteHopIs(FAutomationTestBase& test, const FString& what, const FSOLLaneRoutePlan& plan,
        const int32 index, const FSOLLanePair& lane, const int32 from, const int32 to, const int32 sign,
        const double timeS)
    {
        if (index < 0 || index >= plan.HopCount || index >= SOL::NPC_LANE_ROUTE_MAX_HOPS)
        {
            test.AddError(FString::Printf(TEXT("%s: hop %d missing (%d hops)"), *what, index, plan.HopCount));
            return;
        }
        const FSOLLaneRoutePlanHop& hop = plan.Hops[index];
        test.TestEqual(what + TEXT(" lane A"), hop.Lane.NodeA, lane.NodeA);
        test.TestEqual(what + TEXT(" lane B"), hop.Lane.NodeB, lane.NodeB);
        test.TestEqual(what + TEXT(" from"), hop.FromNode, from);
        test.TestEqual(what + TEXT(" to"), hop.ToNode, to);
        test.TestEqual(what + TEXT(" sign"), hop.DirectionSign, sign);
        LaneRouteNear(test, what + TEXT(" time"), hop.TimeS, timeS, LANEROUTE_TIGHT);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected tangent waypoint of a ship on +X at distance r with the side unit m (a = max(acos(edge / r),
    // the minimum arc))
    FVector3d LaneRouteTangentOnX(const double r, const FVector3d& side)
    {
        const double a = r > LANEROUTE_EDGE_M ? FMath::Max(FMath::Acos(LANEROUTE_EDGE_M / r), LANEROUTE_MIN_ARC_RAD)
            : LANEROUTE_MIN_ARC_RAD;
        return LANEROUTE_EDGE_M * (FMath::Cos(a) * FVector3d(1.0, 0.0, 0.0) + FMath::Sin(a) * side);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Sun-safe aim with the case radii and minimum arc
    FSOLSunSafeAim LaneRouteAim(const FVector3d& shipM, const FVector3d& velocityMps, const FVector3d& targetM,
        const bool bSafety, const bool bWasDetouring)
    {
        return SOLLaneRoutePlan::SunSafeAim(shipM, velocityMps, targetM, bSafety, bWasDetouring, LANEROUTE_ZONE_M,
            LANEROUTE_EDGE_M, LANEROUTE_MIN_ARC_RAD, 0.0, 0.0);
    }

    // One detour trip's outcome
    struct FLaneRouteTrip
    {
        bool bReached = false;                  // Came within the reach distance of the target, not detouring
        double MinSunM = LANEROUTE_INF;         // Closest approach to the Sun over every step segment
        bool bDetoured = false;                 // A detour (or zone exit) aim was flown at some step
        int32 Steps = 0;
        FVector3d FinalM = FVector3d::ZeroVector;
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies a deterministic kinematic pursuit: each step moves stepM straight toward the Sun-safe aim (the latch
    // carried, the step direction as the velocity), until it is within stepM of a non-detour aim (the target, or its
    // arrival point at the zone edge)
    FLaneRouteTrip LaneRoutePursuit(const FVector3d& startM, const FVector3d& targetM, const double stepM,
        const int32 maxSteps)
    {
        FLaneRouteTrip trip;
        FVector3d shipM = startM;
        FVector3d velocity = FVector3d::ZeroVector;
        bool bDetour = false;
        for (int32 step = 0; step < maxSteps; ++step)
        {
            const FSOLSunSafeAim aim = LaneRouteAim(shipM, velocity, targetM, true, bDetour);
            bDetour = aim.bDetour;
            trip.bDetoured |= bDetour;
            const FVector3d toAim = aim.AimM - shipM;
            const double distance = toAim.Size();
            if (!bDetour && distance <= stepM)
            {
                trip.bReached = true;
                break;
            }
            if (!(distance > 0.0) || aim.AimM.ContainsNaN())
            {
                break;
            }
            const FVector3d move = toAim / distance * FMath::Min(stepM, distance);
            const FVector3d nextM = shipM + move;
            trip.MinSunM = FMath::Min(trip.MinSunM, SOLLaneNetwork::SunSegmentDistanceM(shipM, nextM));
            velocity = move;
            shipM = nextM;
            trip.Steps = step + 1;
        }
        trip.FinalM = shipM;
        return trip;
    }

    // How a flight picks its step target
    enum class ELaneRouteAimMode : uint8
    {
        Direct,         // The target itself (today's AUTO)
        SafetyOff,      // SunSafeAim with the safety off (the player's override)
        SafetyOn        // SunSafeAim with the safety on
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies from rest at startM to a still target as the flight processor does: the Sun-safe aim per mode each step
    // (with the autopilot's brake plan and the step for the braking-path and swept checks); a Sun aim (detour or
    // zone-edge arrival) flies the plain AutopilotStep with the aim at rest, else the normal AutopilotStep or the War
    // power pulse step (boost on); the G meter stepped from the achieved thrust; no gravity; until reached (1e-4 of the
    // trip while not detouring) or maxS
    FLaneRouteTrip LaneRouteFly(const FVector3d& startM, const FVector3d& targetM, const ELaneRouteAimMode mode,
        const bool bWarPower, const double dt, const double maxS)
    {
        const FSOLFlightParams params;
        const FVector3d zero = FVector3d::ZeroVector;
        const double reachM = 1.0e-4 * (targetM - startM).Size();
        FLaneRouteTrip trip;
        FSOLShipState state;
        state.PositionM = startM;
        state.Orientation = FQuat4d::Identity;
        FSOLGMeterState meter;
        FSOLWarPowerPulseState pulse;
        bool bDetour = false;
        const int32 steps = FMath::CeilToInt(maxS / dt);
        for (int32 index = 0; index < steps; ++index)
        {
            FVector3d aimM = targetM;
            bool bSunAim = false;
            if (mode != ELaneRouteAimMode::Direct)
            {
                const FSOLGuidanceParams brakePlan = SOLNewtonian::MakeGuidanceParams(params.Envelope,
                    SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, meter), 0.0, true, bWarPower);
                const FSOLSunSafeAim aim = SOLLaneRoutePlan::SunSafeAim(state.PositionM, state.VelocityMps, targetM,
                    mode == ELaneRouteAimMode::SafetyOn, bDetour, LANEROUTE_ZONE_M, LANEROUTE_EDGE_M,
                    LANEROUTE_MIN_ARC_RAD, brakePlan.BrakeAccelMps2, dt);
                bDetour = aim.bDetour;
                bSunAim = aim.bDetour || aim.bStoppedAtZone;
                trip.bDetoured |= bDetour;
                aimM = aim.AimM;
            }
            const FVector3d beforeM = state.PositionM;
            FSOLFlightStepOutput step;
            FSOLGuidanceOutput guidance;
            state = bSunAim
                ? SOLFlight::AutopilotStep(state, aimM, zero, zero, zero, params, meter, dt, &step, &guidance, 0.0)
                : bWarPower
                ? SOLFlight::WarPowerPulseAutopilotStep(state, aimM, zero, zero, zero, params, meter, true, dt, pulse,
                    &step, &guidance)
                : SOLFlight::AutopilotStep(state, aimM, zero, zero, zero, params, meter, dt, &step, &guidance);
            meter = SOLNewtonian::StepGMeter(meter, SOLNewtonian::GLoadG(step.ThrustAccelWorldMps2), params.PilotLimits,
                dt);
            trip.MinSunM = FMath::Min(trip.MinSunM, SOLLaneNetwork::SunSegmentDistanceM(beforeM, state.PositionM));
            trip.Steps = index + 1;
            if (!bDetour && (targetM - state.PositionM).Size() <= reachM)
            {
                trip.bReached = true;
                break;
            }
        }
        trip.FinalM = state.PositionM;
        return trip;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the row index of an action in a type's favourable menu, or INDEX_NONE
    int32 LaneRouteRowOf(const ESOLObjectType type, const ESOLAction action)
    {
        FSOLActionContext favourable;
        favourable.bHasShip = true;
        favourable.bPilotHasControl = true;
        favourable.bIsTargetable = true;
        FSOLActionList list;
        SOLActionTable::Build(type, favourable, list);
        for (int32 row = 0; row < list.Num; ++row)
        {
            if (list.Entries[row].Action == action)
            {
                return row;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the context whose flags are the bits of mask (bit 0 bHasShip ... bit 8 bIsChased)
    FSOLActionContext LaneRouteContextFromMask(const int32 mask)
    {
        FSOLActionContext context;
        context.bHasShip = (mask & (1 << 0)) != 0;
        context.bPilotHasControl = (mask & (1 << 1)) != 0;
        context.bJumpWarping = (mask & (1 << 2)) != 0;
        context.bIsTargetable = (mask & (1 << 3)) != 0;
        context.bIsSelected = (mask & (1 << 4)) != 0;
        context.bIsFrameLocked = (mask & (1 << 5)) != 0;
        context.bIsMoveTarget = (mask & (1 << 6)) != 0;
        context.bIsFollowed = (mask & (1 << 7)) != 0;
        context.bIsChased = (mask & (1 << 8)) != 0;
        return context;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoScopeTest, "SOLTest.LaneRouteAuto.RouterScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Q11 and SDD 50 D22: an AUTO order is lane-routed only from a node to a different node, never for War power
bool FSOLLaneRouteAutoScopeTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneRoutePlan::ShouldPlanLaneRoute;
    TestTrue(TEXT("node to node"), ShouldPlanLaneRoute(false, 2, 6));
    TestFalse(TEXT("War power ignores lanes"), ShouldPlanLaneRoute(true, 2, 6));
    TestFalse(TEXT("same node"), ShouldPlanLaneRoute(false, 3, 3));
    TestFalse(TEXT("ship at no node"), ShouldPlanLaneRoute(false, INDEX_NONE, 6));
    TestFalse(TEXT("target at no node"), ShouldPlanLaneRoute(false, 2, INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoMakePlanTest, "SOLTest.LaneRouteAuto.MakePlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 6.4: a plan from a found route carries each hop's lane identity, direction and time (edge + transfer), starts at
// hop 0 with the order serial and epoch; a route that is not found, empty or does not chain from the source gives none
bool FSOLLaneRouteAutoMakePlanTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    const TArray<FSOLLaneEdge> graph = LaneRouteGraph();

    // 0 -> 3: e0, e1, e3 outbound, 10 + (10 + 3) + (5 + 3) = 31
    const FSOLLaneRoute route03 = LaneRouteFind(graph, 0, 3);
    TestTrue(TEXT("router: 0 -> 3 found"), route03.bFound && route03.HopCount == 3);
    FSOLLaneRoutePlan plan;
    TestTrue(TEXT("0 -> 3: made"), MakePlan(route03, graph, 0, LANEROUTE_PENALTY_S, LANEROUTE_WALL_MPS, 7, 42u, plan));
    TestTrue(TEXT("0 -> 3: has a plan"), HasPlan(plan));
    TestEqual(TEXT("0 -> 3: 3 hops"), plan.HopCount, 3);
    TestEqual(TEXT("0 -> 3: at hop 0"), plan.CurrentHop, 0);
    TestEqual(TEXT("0 -> 3: serial"), plan.Serial, 42u);
    TestEqual(TEXT("0 -> 3: epoch"), plan.PlannedEpoch, static_cast<int64>(7));
    LaneRouteHopIs(*this, TEXT("0 -> 3 hop 0"), plan, 0, LaneRoutePair(0, 1), 0, 1, 1, 10.0);
    LaneRouteHopIs(*this, TEXT("0 -> 3 hop 1"), plan, 1, LaneRoutePair(1, 2), 1, 2, 1, 13.0);
    LaneRouteHopIs(*this, TEXT("0 -> 3 hop 2"), plan, 2, LaneRoutePair(2, 3), 2, 3, 1, 8.0);
    double sumS = 0.0;
    for (int32 hop = 0; hop < plan.HopCount && hop < SOL::NPC_LANE_ROUTE_MAX_HOPS; ++hop)
    {
        sumS += plan.Hops[hop].TimeS;
    }
    LaneRouteNear(*this, TEXT("0 -> 3: hop times sum to the route time"), sumS, route03.TimeS, LANEROUTE_TIGHT);

    // 3 -> 0: the same lanes inbound (identities stay sorted), 5 + 13 + 13 = 31
    FSOLLaneRoutePlan back;
    TestTrue(TEXT("3 -> 0: made"), MakePlan(LaneRouteFind(graph, 3, 0), graph, 3, LANEROUTE_PENALTY_S,
        LANEROUTE_WALL_MPS, 8, 43u, back));
    LaneRouteHopIs(*this, TEXT("3 -> 0 hop 0"), back, 0, LaneRoutePair(2, 3), 3, 2, -1, 5.0);
    LaneRouteHopIs(*this, TEXT("3 -> 0 hop 1"), back, 1, LaneRoutePair(1, 2), 2, 1, -1, 13.0);
    LaneRouteHopIs(*this, TEXT("3 -> 0 hop 2"), back, 2, LaneRoutePair(0, 1), 1, 0, -1, 13.0);

    // No plan: a route to the node itself (found, no hops), one not found, one from another source, bad edge indices
    FSOLLaneRoutePlan none = plan;
    TestFalse(TEXT("source == target: no plan"), MakePlan(LaneRouteFind(graph, 2, 2), graph, 2, LANEROUTE_PENALTY_S,
        LANEROUTE_WALL_MPS, 7, 1u, none));
    TestFalse(TEXT("source == target: empty"), HasPlan(none) || none.HopCount != 0);
    TArray<FSOLLaneEdge> closed = graph;
    closed[5].CloseS = 0.0;
    none = plan;
    TestFalse(TEXT("not found: no plan"), MakePlan(LaneRouteFind(closed, 0, 4), closed, 0, LANEROUTE_PENALTY_S,
        LANEROUTE_WALL_MPS, 7, 1u, none));
    TestEqual(TEXT("not found: empty"), none.HopCount, 0);
    none = plan;
    TestFalse(TEXT("route from 0 given source 1: no plan"), MakePlan(route03, graph, 1, LANEROUTE_PENALTY_S,
        LANEROUTE_WALL_MPS, 7, 1u, none));
    TestEqual(TEXT("wrong source: empty"), none.HopCount, 0);
    none = plan;
    const TArray<FSOLLaneEdge> shortEdges = { graph[0], graph[1] };
    TestFalse(TEXT("edge index outside the edges: no plan"), MakePlan(route03, shortEdges, 0, LANEROUTE_PENALTY_S,
        LANEROUTE_WALL_MPS, 7, 1u, none));
    TestEqual(TEXT("bad edges: empty"), none.HopCount, 0);

    // ClearPlan
    FSOLLaneRoutePlan cleared = plan;
    ClearPlan(cleared);
    TestFalse(TEXT("cleared: no plan"), HasPlan(cleared));
    TestEqual(TEXT("cleared: no hops"), cleared.HopCount, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoHopTest, "SOLTest.LaneRouteAuto.HopIdentityAndPerHopLeft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 6.4: leaving a hop goes on to the next with None (it latches the next lane), Left only after the last. (The hop
// finds its lane by its two bodies in the flight run; FindHopSlot and IsStraightSearchAllowed were removed as unused.)
bool FSOLLaneRouteAutoHopTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    FSOLLaneRoutePlan plan = LaneRoutePlan03(*this);

    // Per-hop Left
    TestEqual(TEXT("leave hop 0: None"), static_cast<int32>(LeaveHop(plan)),
        static_cast<int32>(ESOLLaneAutoMode::None));
    TestEqual(TEXT("leave hop 0: at hop 1"), plan.CurrentHop, 1);
    TestTrue(TEXT("leave hop 0: plan left"), HasPlan(plan));
    TestEqual(TEXT("hop 1 is (1, 2)"), plan.Hops[plan.CurrentHop].Lane.NodeB, 2);
    TestEqual(TEXT("leave hop 1: None"), static_cast<int32>(LeaveHop(plan)),
        static_cast<int32>(ESOLLaneAutoMode::None));
    TestEqual(TEXT("leave hop 1: at hop 2"), plan.CurrentHop, 2);
    TestEqual(TEXT("leave hop 2 (last): Left"), static_cast<int32>(LeaveHop(plan)),
        static_cast<int32>(ESOLLaneAutoMode::Left));
    TestFalse(TEXT("after the last hop: no plan"), HasPlan(plan));
    FSOLLaneRoutePlan empty;
    TestEqual(TEXT("no plan: Left, final as today"), static_cast<int32>(LeaveHop(empty)),
        static_cast<int32>(ESOLLaneAutoMode::Left));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoReplanTest, "SOLTest.LaneRouteAuto.ReplanAtEpochChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Q14: at an epoch change the hops after the current one are re-planned from the current hop's end node to the plan's
// target; the current hop stays; a failed re-plan keeps only the current hop
bool FSOLLaneRouteAutoReplanTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    FSOLLaneRoutePlan plan = LaneRoutePlan03(*this);
    TestFalse(TEXT("same epoch: no re-plan"), ShouldReplan(plan, 7));
    TestTrue(TEXT("next epoch: re-plan"), ShouldReplan(plan, 8));
    TestFalse(TEXT("no plan: no re-plan"), ShouldReplan(FSOLLaneRoutePlan(), 8));
    TestEqual(TEXT("re-plan source: hop 0's end node"), ReplanSourceNode(plan), 1);
    TestEqual(TEXT("plan target"), PlanTargetNode(plan), 3);
    TestEqual(TEXT("no plan: no source"), ReplanSourceNode(FSOLLaneRoutePlan()), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("no plan: no target"), PlanTargetNode(FSOLLaneRoutePlan()), static_cast<int32>(INDEX_NONE));

    // The new epoch closes e1 (1-2) before its leg ends: from node 1 the fastest is e4 (1-3, 30 s)
    TArray<FSOLLaneEdge> closed = LaneRouteGraph();
    closed[1].CloseS = 5.0;
    const FSOLLaneRoute fromOne = LaneRouteFind(closed, 1, 3);
    TestTrue(TEXT("router: 1 -> 3 via e4"), fromOne.bFound && fromOne.HopCount == 1 && fromOne.Hops[0].Lane == 4);
    FSOLLaneRoutePlan replanned = plan;
    TestTrue(TEXT("re-planned"), ReplaceRemainingHops(replanned, fromOne, closed, LANEROUTE_PENALTY_S,
        LANEROUTE_WALL_MPS, 8));
    TestEqual(TEXT("re-planned: 2 hops"), replanned.HopCount, 2);
    TestEqual(TEXT("re-planned: at hop 0"), replanned.CurrentHop, 0);
    TestEqual(TEXT("re-planned: serial + 1"), replanned.Serial, 43u);
    TestEqual(TEXT("re-planned: epoch"), replanned.PlannedEpoch, static_cast<int64>(8));
    LaneRouteHopIs(*this, TEXT("re-planned hop 0 (kept)"), replanned, 0, LaneRoutePair(0, 1), 0, 1, 1, 10.0);
    LaneRouteHopIs(*this, TEXT("re-planned hop 1"), replanned, 1, LaneRoutePair(1, 3), 1, 3, 1, 33.0);
    TestFalse(TEXT("re-planned: no further re-plan this epoch"), ShouldReplan(replanned, 8));

    // On the last hop nothing is left to re-plan
    FSOLLaneRoutePlan last = plan;
    LeaveHop(last);
    LeaveHop(last);
    TestFalse(TEXT("last hop: no re-plan"), ShouldReplan(last, 9));

    // A failed re-plan: not found, from another node, or to another target; only the current hop stays
    TArray<FSOLLaneEdge> cut = closed;
    cut[4].CloseS = 5.0;
    cut[0].CloseS = 5.0;
    FSOLLaneRoutePlan failed = plan;
    TestFalse(TEXT("not found: false"), ReplaceRemainingHops(failed, LaneRouteFind(cut, 1, 3), cut,
        LANEROUTE_PENALTY_S, LANEROUTE_WALL_MPS, 8));
    TestEqual(TEXT("not found: the current hop alone"), failed.HopCount, 1);
    LaneRouteHopIs(*this, TEXT("not found: hop 0 kept"), failed, 0, LaneRoutePair(0, 1), 0, 1, 1, 10.0);
    TestEqual(TEXT("not found: serial + 1"), failed.Serial, 43u);
    TestEqual(TEXT("not found: epoch"), failed.PlannedEpoch, static_cast<int64>(8));
    FSOLLaneRoutePlan wrongSource = plan;
    TestFalse(TEXT("route from node 2: false"), ReplaceRemainingHops(wrongSource, LaneRouteFind(closed, 2, 3), closed,
        LANEROUTE_PENALTY_S, LANEROUTE_WALL_MPS, 8));
    TestEqual(TEXT("route from node 2: the current hop alone"), wrongSource.HopCount, 1);
    FSOLLaneRoutePlan wrongTarget = plan;
    TestFalse(TEXT("route to node 4: false"), ReplaceRemainingHops(wrongTarget, LaneRouteFind(LaneRouteGraph(), 1, 4),
        LaneRouteGraph(), LANEROUTE_PENALTY_S, LANEROUTE_WALL_MPS, 8));
    TestEqual(TEXT("route to node 4: the current hop alone"), wrongTarget.HopCount, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoEtaTest, "SOLTest.LaneRouteAuto.PlanEta",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Q13: the HUD ETA is the current hop's ETA, the later hops' edge and transfer times and the last mile
bool FSOLLaneRouteAutoEtaTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    FSOLLaneRoutePlan plan = LaneRoutePlan03(*this);
    LaneRouteNear(*this, TEXT("hop 0: 4 + 13 + 8 + 2"), PlanEtaS(plan, 4.0, 2.0), 27.0, LANEROUTE_TIGHT);
    LeaveHop(plan);
    LaneRouteNear(*this, TEXT("hop 1: 4 + 8 + 2"), PlanEtaS(plan, 4.0, 2.0), 14.0, LANEROUTE_TIGHT);
    LeaveHop(plan);
    LaneRouteNear(*this, TEXT("hop 2 (last): 4 + 2"), PlanEtaS(plan, 4.0, 2.0), 6.0, LANEROUTE_TIGHT);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoFinishTest, "SOLTest.LaneRouteAuto.FinishIfSafe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 6.5: a held hop whose lane closes before the hop ends is finished only if the remaining path keeps more than
// k_on R_sun from the Sun at every epoch, or with the override; else AUTO leaves it at once
bool FSOLLaneRouteAutoFinishTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;

    // Closing before the hop ends
    TestFalse(TEXT("ends at 10, closes at 20"), HopClosesBeforeEnd(0.0, 10.0, 20.0));
    TestFalse(TEXT("ends exactly at the close"), HopClosesBeforeEnd(0.0, 20.0, 20.0));
    TestTrue(TEXT("ends at 21, closes at 20"), HopClosesBeforeEnd(0.0, 21.0, 20.0));
    TestFalse(TEXT("never closes"), HopClosesBeforeEnd(100.0, 1.0e9, LANEROUTE_INF));
    TestTrue(TEXT("NaN now"), HopClosesBeforeEnd(LANEROUTE_NAN, 10.0, 20.0));
    TestTrue(TEXT("NaN remaining"), HopClosesBeforeEnd(0.0, LANEROUTE_NAN, 20.0));
    TestTrue(TEXT("NaN close"), HopClosesBeforeEnd(0.0, 10.0, LANEROUTE_NAN));

    // The remaining path per epoch: at 2e10 from the Sun both epochs, then at 1.5e10 (inside k_on) or exactly k_on
    const TArray<FVector3d> from = { FVector3d(1e11, 2e10, 0.0), FVector3d(8e10, 2e10, 0.0) };
    const TArray<FVector3d> to = { FVector3d(-1e11, 2e10, 0.0), FVector3d(-1.2e11, 2e10, 0.0) };
    TestTrue(TEXT("2e10 at both epochs: safe"), HopFinishSafe(from, to, LANEROUTE_EDGE_M));
    const TArray<FVector3d> toClose = { FVector3d(-1e11, 2e10, 0.0), FVector3d(-1.2e11, 1.5e10, 0.0) };
    const TArray<FVector3d> fromClose = { FVector3d(1e11, 2e10, 0.0), FVector3d(8e10, 1.5e10, 0.0) };
    TestFalse(TEXT("1.5e10 at the second epoch: not safe"), HopFinishSafe(fromClose, toClose, LANEROUTE_EDGE_M));
    const TArray<FVector3d> fromEdge = { FVector3d(1e11, LANEROUTE_EDGE_M, 0.0) };
    const TArray<FVector3d> toEdge = { FVector3d(-1e11, LANEROUTE_EDGE_M, 0.0) };
    TestFalse(TEXT("exactly k_on: not more than it"), HopFinishSafe(fromEdge, toEdge, LANEROUTE_EDGE_M));
    TestFalse(TEXT("no epochs: not safe"), HopFinishSafe(TArray<FVector3d>(), TArray<FVector3d>(), LANEROUTE_EDGE_M));
    TestFalse(TEXT("mismatched epochs: not safe"), HopFinishSafe(from, toEdge, LANEROUTE_EDGE_M));
    const TArray<FVector3d> fromNaN = { FVector3d(LANEROUTE_NAN, 2e10, 0.0) };
    TestFalse(TEXT("NaN: not safe"), HopFinishSafe(fromNaN, toEdge, LANEROUTE_EDGE_M));

    // The decision
    for (int32 mask = 0; mask < 8; ++mask)
    {
        const bool bCloses = (mask & 1) != 0;
        const bool bSafe = (mask & 2) != 0;
        const bool bOverride = (mask & 4) != 0;
        TestEqual(FString::Printf(TEXT("closes %d, safe %d, override %d"), mask & 1, (mask >> 1) & 1, (mask >> 2) & 1),
            FinishClosingHop(bCloses, bSafe, bOverride), !bCloses || bSafe || bOverride);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoOrderSafetyTest, "SOLTest.LaneRouteAuto.EveryOrderObeysSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// O4: every AUTO order kind obeys the Sun safety unless the player overrode it, War power and Chase Down included
bool FSOLLaneRouteAutoOrderSafetyTest::RunTest(const FString& /*parameters*/)
{
    for (int32 mask = 0; mask < 8; ++mask)
    {
        const bool bOverride = (mask & 1) != 0;
        const bool bWarPower = (mask & 2) != 0;
        const bool bChase = (mask & 4) != 0;
        TestEqual(FString::Printf(TEXT("override %d, War power %d, chase %d"), mask & 1, (mask >> 1) & 1,
            (mask >> 2) & 1),
            SOLLaneRoutePlan::OrderSunSafetyActive(bOverride, bWarPower, bChase), !bOverride);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoWaypointTest, "SOLTest.LaneRouteAuto.DetourWaypoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 6.5 detour waypoint (math chosen by the test author, contract case 15 to be extended): on the k_on sphere, in
// the plane of the Sun, the ship and the target, the tangent point from the ship but at least the minimum arc ahead, on
// the target's side unless the velocity points to the other
bool FSOLLaneRouteAutoWaypointTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneRoutePlan::SunDetourWaypointM;
    const double e = LANEROUTE_EDGE_M;
    const double arc = LANEROUTE_MIN_ARC_RAD;
    const FVector3d zero = FVector3d::ZeroVector;
    const FVector3d ship(1e11, 0.0, 0.0);
    const FVector3d yUp(0.0, 1.0, 0.0);
    const FVector3d yDown(0.0, -1.0, 0.0);

    // Off-centre target on +Y: the target's side, unless the velocity says the other
    const FVector3d target(-1e11, 5e9, 0.0);
    LaneRouteVecNear(*this, TEXT("at rest: tangent, target side"), SunDetourWaypointM(ship, zero, target, e, arc),
        LaneRouteTangentOnX(1e11, yUp), LANEROUTE_TIGHT);
    LaneRouteVecNear(*this, TEXT("velocity -Y: the velocity's side"), SunDetourWaypointM(ship,
        FVector3d(0.0, -1.0e4, 0.0), target, e, arc), LaneRouteTangentOnX(1e11, yDown), LANEROUTE_TIGHT);
    LaneRouteVecNear(*this, TEXT("velocity +Y: target side"), SunDetourWaypointM(ship, FVector3d(0.0, 1.0e4, 0.0),
        target, e, arc), LaneRouteTangentOnX(1e11, yUp), LANEROUTE_TIGHT);
    LaneRouteVecNear(*this, TEXT("velocity along X only: target side"), SunDetourWaypointM(ship,
        FVector3d(1.0e4, 0.0, 0.0), target, e, arc), LaneRouteTangentOnX(1e11, yUp), LANEROUTE_TIGHT);

    // Target straight behind the Sun: the velocity's perpendicular part, else s x +Z, else s x +X
    const FVector3d behind(-1e11, 0.0, 0.0);
    LaneRouteVecNear(*this, TEXT("collinear, velocity +Z"), SunDetourWaypointM(ship, FVector3d(0.0, 0.0, 1.0e4),
        behind, e, arc), LaneRouteTangentOnX(1e11, FVector3d(0.0, 0.0, 1.0)), LANEROUTE_TIGHT);
    LaneRouteVecNear(*this, TEXT("collinear, velocity (3e4, 1e4, 0)"), SunDetourWaypointM(ship,
        FVector3d(3.0e4, 1.0e4, 0.0), behind, e, arc), LaneRouteTangentOnX(1e11, yUp), LANEROUTE_TIGHT);
    LaneRouteVecNear(*this, TEXT("collinear, at rest: s x +Z = -Y"), SunDetourWaypointM(ship, zero, behind, e, arc),
        LaneRouteTangentOnX(1e11, yDown), LANEROUTE_TIGHT);
    const double c = e / 1e11;
    LaneRouteVecNear(*this, TEXT("on the Z axis, at rest: s x +X = +Y"), SunDetourWaypointM(FVector3d(0.0, 0.0, 1e11),
        zero, FVector3d(0.0, 0.0, -1e11), e, arc), FVector3d(0.0, e * FMath::Sqrt(1.0 - c * c), e * c),
        LANEROUTE_TIGHT);

    // Near the zone: the tangent point at 2e10 (the leg to it touches k_on), the minimum arc at 1.55e10 (tangent
    // 9.1 degrees) and inside k_on at 1.45e10
    const FVector3d farTarget(-1e11, 1e9, 0.0);
    const FVector3d nearShip(2e10, 0.0, 0.0);
    const FVector3d nearW = SunDetourWaypointM(nearShip, zero, farTarget, e, arc);
    LaneRouteVecNear(*this, TEXT("ship at 2e10: tangent (40.1 degrees)"), nearW, LaneRouteTangentOnX(2e10, yUp),
        LANEROUTE_TIGHT);
    LaneRouteNear(*this, TEXT("ship at 2e10: the leg touches k_on"), SOLLaneNetwork::SunSegmentDistanceM(nearShip,
        nearW), e, 1.0e-6);
    LaneRouteVecNear(*this, TEXT("ship at 1.55e10: the minimum arc"), SunDetourWaypointM(FVector3d(1.55e10, 0.0, 0.0),
        zero, farTarget, e, arc), e * FVector3d(FMath::Cos(arc), FMath::Sin(arc), 0.0), LANEROUTE_TIGHT);
    LaneRouteVecNear(*this, TEXT("ship at 1.45e10 (inside k_on): the minimum arc"), SunDetourWaypointM(
        FVector3d(1.45e10, 0.0, 0.0), zero, farTarget, e, arc), e * FVector3d(FMath::Cos(arc), FMath::Sin(arc), 0.0),
        LANEROUTE_TIGHT);

    // Always on the k_on sphere and in the Sun-ship-target plane
    for (const FVector3d& start : { ship, nearShip, FVector3d(3e10, 4e10, 1e10) })
    {
        const FVector3d w = SunDetourWaypointM(start, zero, farTarget, e, arc);
        LaneRouteNear(*this, TEXT("|W| = k_on"), w.Size(), e, LANEROUTE_TIGHT);
        const FVector3d normal = FVector3d::CrossProduct(start, farTarget).GetSafeNormal();
        LaneRouteNear(*this, TEXT("W in the plane"), FVector3d::DotProduct(w, normal) / e, 0.0, LANEROUTE_TIGHT);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoAimTest, "SOLTest.LaneRouteAuto.SunSafeAim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 6.5: the safety off (override) or a clear path flies to the target bit for bit; a path into the zone gets the
// detour waypoint; a target inside the zone stops at its edge facing the ship; a ship inside the zone leaves radially
bool FSOLLaneRouteAutoAimTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d zero = FVector3d::ZeroVector;
    const FVector3d ship(1e11, 0.0, 0.0);
    const FVector3d through(-1e11, 5e9, 0.0);   // The straight path passes 2.5e9 m from the Sun
    const FVector3d inside(1e9, 0.0, 0.0);

    // Safety off: the target itself, whatever the path, the latch or the target
    for (const bool bLatched : { false, true })
    {
        const FSOLSunSafeAim off = LaneRouteAim(ship, zero, through, false, bLatched);
        LaneRouteVecExact(*this, TEXT("safety off: the target bit for bit"), off.AimM, through);
        TestFalse(TEXT("safety off: no detour"), off.bDetour);
        TestFalse(TEXT("safety off: not stopped"), off.bStoppedAtZone);
    }
    const FSOLSunSafeAim offInside = LaneRouteAim(ship, zero, inside, false, false);
    LaneRouteVecExact(*this, TEXT("safety off, target inside: flies as today"), offInside.AimM, inside);
    TestFalse(TEXT("safety off, target inside: not stopped"), offInside.bStoppedAtZone);

    // Safety on, clear path: the target bit for bit
    const FVector3d clear(0.0, 1e11, 0.0);
    const FSOLSunSafeAim onClear = LaneRouteAim(ship, zero, clear, true, false);
    LaneRouteVecExact(*this, TEXT("clear path: the target bit for bit"), onClear.AimM, clear);
    TestFalse(TEXT("clear path: no detour"), onClear.bDetour);
    TestFalse(TEXT("clear path: not stopped"), onClear.bStoppedAtZone);

    // Safety on, path into the zone: the waypoint
    const FSOLSunSafeAim onThrough = LaneRouteAim(ship, zero, through, true, false);
    TestTrue(TEXT("through the zone: detour"), onThrough.bDetour);
    TestFalse(TEXT("through the zone: not stopped"), onThrough.bStoppedAtZone);
    LaneRouteVecNear(*this, TEXT("through the zone: the waypoint"), onThrough.AimM,
        LaneRouteTangentOnX(1e11, FVector3d(0.0, 1.0, 0.0)), LANEROUTE_TIGHT);

    // Target inside the zone: the edge point facing the ship, no detour needed
    const FSOLSunSafeAim onInside = LaneRouteAim(ship, zero, inside, true, false);
    LaneRouteVecNear(*this, TEXT("target inside: the edge facing the ship"), onInside.AimM,
        FVector3d(LANEROUTE_EDGE_M, 0.0, 0.0), LANEROUTE_TIGHT);
    TestTrue(TEXT("target inside: stopped at the zone"), onInside.bStoppedAtZone);
    TestFalse(TEXT("target inside: no detour"), onInside.bDetour);
    const FSOLSunSafeAim onBehind = LaneRouteAim(ship, zero, FVector3d(-1e9, 1e8, 0.0), true, false);
    LaneRouteVecNear(*this, TEXT("target inside, behind the Sun: the edge facing the ship"), onBehind.AimM,
        FVector3d(LANEROUTE_EDGE_M, 0.0, 0.0), LANEROUTE_TIGHT);
    TestTrue(TEXT("target inside, behind the Sun: stopped at the zone"), onBehind.bStoppedAtZone);

    // Ship inside the zone (manual flight took it there): leave radially to k_on, +Z at the Sun's centre
    const FSOLSunSafeAim zoneExit = LaneRouteAim(FVector3d(5e9, 0.0, 0.0), zero, through, true, false);
    LaneRouteVecNear(*this, TEXT("ship inside: radial exit"), zoneExit.AimM, FVector3d(LANEROUTE_EDGE_M, 0.0, 0.0),
        LANEROUTE_TIGHT);
    TestTrue(TEXT("ship inside: detour latched"), zoneExit.bDetour);
    const FSOLSunSafeAim centre = LaneRouteAim(zero, zero, through, true, false);
    LaneRouteVecNear(*this, TEXT("ship at the centre: +Z exit"), centre.AimM, FVector3d(0.0, 0.0, LANEROUTE_EDGE_M),
        LANEROUTE_TIGHT);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoHysteresisTest, "SOLTest.LaneRouteAuto.DetourHysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The detour latch (test author's choice, like the lanes' 20 / 22 band): it engages when the path enters the zone
// (k_off) and releases when the path clears k_on, or the arrival's or the ship's own radius when those are inside k_on
bool FSOLLaneRouteAutoHysteresisTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d zero = FVector3d::ZeroVector;

    // A path 1.45e10 from the Sun (between k_off and k_on): straight when fresh, detour while latched
    const FVector3d shipBand(1e11, 1.45e10, 0.0);
    const FVector3d targetBand(-1e11, 1.45e10, 0.0);
    const FSOLSunSafeAim fresh = LaneRouteAim(shipBand, zero, targetBand, true, false);
    LaneRouteVecExact(*this, TEXT("band, fresh: straight"), fresh.AimM, targetBand);
    TestFalse(TEXT("band, fresh: no detour"), fresh.bDetour);
    const FSOLSunSafeAim latched = LaneRouteAim(shipBand, zero, targetBand, true, true);
    TestTrue(TEXT("band, latched: still detouring"), latched.bDetour);
    LaneRouteVecNear(*this, TEXT("band, latched: the waypoint"), latched.AimM, SOLLaneRoutePlan::SunDetourWaypointM(
        shipBand, zero, targetBand, LANEROUTE_EDGE_M, LANEROUTE_MIN_ARC_RAD), LANEROUTE_TIGHT);

    // A path 1.6e10 from the Sun (beyond k_on): released
    const FVector3d targetOut(-1e11, 1.6e10, 0.0);
    const FSOLSunSafeAim released = LaneRouteAim(FVector3d(1e11, 1.6e10, 0.0), zero, targetOut, true, true);
    LaneRouteVecExact(*this, TEXT("beyond k_on, latched: released, straight"), released.AimM, targetOut);
    TestFalse(TEXT("beyond k_on, latched: no detour"), released.bDetour);

    // A target at 1.45e10 (inside k_on), approached radially: the path's closest point is the target itself, released
    const FVector3d targetLow(0.0, 1.45e10, 0.0);
    const FSOLSunSafeAim toLow = LaneRouteAim(FVector3d(0.0, 5e10, 0.0), zero, targetLow, true, true);
    LaneRouteVecExact(*this, TEXT("target inside k_on, radial: released"), toLow.AimM, targetLow);
    TestFalse(TEXT("target inside k_on, radial: no detour"), toLow.bDetour);

    // A ship at 1.45e10 heading outward: the path's closest point is the ship itself, released
    const FVector3d targetOutward(1e11, 1e9, 0.0);
    const FSOLSunSafeAim outward = LaneRouteAim(FVector3d(1.45e10, 0.0, 0.0), zero, targetOutward, true, true);
    LaneRouteVecExact(*this, TEXT("ship inside k_on, outward: released"), outward.AimM, targetOutward);
    TestFalse(TEXT("ship inside k_on, outward: no detour"), outward.bDetour);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoPursuitTest, "SOLTest.LaneRouteAuto.DetourPursuit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// "AUTO never plans or keeps a path through it": a kinematic pursuit of the aim (2e7 m steps) from distant and near
// starts to distant, behind-the-Sun and in-band targets always arrives and never enters the zone; a start inside the
// zone never goes deeper than it started
bool FSOLLaneRouteAutoPursuitTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d starts[] = { FVector3d(1e11, 0.0, 0.0), FVector3d(2e10, 0.0, 0.0), FVector3d(1.6e10, 0.0, 0.0),
        FVector3d(1.45e10, 1e8, 0.0), FVector3d(5e9, 0.0, 0.0) };
    const FVector3d targets[] = { FVector3d(-1e11, 5e9, 0.0), FVector3d(-2e10, 0.0, 0.0), FVector3d(-1.5e10, 1e9, 0.0),
        FVector3d(-1e11, 0.0, 0.0), FVector3d(-1e9, 1e8, 0.0) };
    for (const FVector3d& start : starts)
    {
        for (const FVector3d& target : targets)
        {
            const FString label = FString::Printf(TEXT("(%.3g, %.3g) -> (%.3g, %.3g)"), start.X, start.Y, target.X,
                target.Y);
            const FLaneRouteTrip trip = LaneRoutePursuit(start, target, 2.0e7, 200000);
            TestTrue(label + TEXT(": arrives"), trip.bReached);
            const FVector3d arrivalM = SOLLaneNetwork::SunSafeArrivalM(start, target, LANEROUTE_ZONE_M,
                LANEROUTE_EDGE_M);
            const bool bNeedsDetour = start.Size() < LANEROUTE_ZONE_M
                || SOLLaneNetwork::PathEntersSunZone(start, arrivalM, LANEROUTE_ZONE_M);
            TestEqual(label + TEXT(": detoured exactly when the straight path enters the zone"), trip.bDetoured,
                bNeedsDetour);
            const double floorM = FMath::Min(LANEROUTE_ZONE_M, start.Size());
            TestTrue(FString::Printf(TEXT("%s: closest %.6g m at or beyond %.6g m"), *label, trip.MinSunM, floorM),
                trip.MinSunM >= floorM * (1.0 - 1.0e-9));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoFlightTest, "SOLTest.LaneRouteAuto.DetourAutopilotAndWarPower",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// O4: a Move To and a War power move across the Sun (straight path 2.5e9 m from it) fly the detour with the real
// guidance steps (10 s substeps, no gravity) and arrive without entering the zone
bool FSOLLaneRouteAutoFlightTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d start(1e11, 0.0, 0.0);
    const FVector3d target(-1e11, 5e9, 0.0);
    for (const bool bWarPower : { false, true })
    {
        const FString label = bWarPower ? TEXT("War power") : TEXT("Move To");
        const FLaneRouteTrip trip = LaneRouteFly(start, target, ELaneRouteAimMode::SafetyOn, bWarPower, 10.0,
            600.0 * 3600.0);
        TestTrue(label + TEXT(": arrives"), trip.bReached);
        TestTrue(label + TEXT(": detoured"), trip.bDetoured);
        TestTrue(FString::Printf(TEXT("%s: closest %.6g m beyond the zone"), *label, trip.MinSunM),
            trip.MinSunM >= LANEROUTE_ZONE_M);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoOverrideTest, "SOLTest.LaneRouteAuto.OverrideFliesStraight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// O4 override: with the safety off the same trip is bit-identical to today's direct AUTO flight and passes through the
// zone (no detour, nothing changed)
bool FSOLLaneRouteAutoOverrideTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d start(1e11, 0.0, 0.0);
    const FVector3d target(-1e11, 5e9, 0.0);
    for (const bool bWarPower : { false, true })
    {
        const FString label = bWarPower ? TEXT("War power") : TEXT("Move To");
        const FLaneRouteTrip direct = LaneRouteFly(start, target, ELaneRouteAimMode::Direct, bWarPower, 10.0,
            600.0 * 3600.0);
        const FLaneRouteTrip off = LaneRouteFly(start, target, ELaneRouteAimMode::SafetyOff, bWarPower, 10.0,
            600.0 * 3600.0);
        TestTrue(label + TEXT(": arrives"), off.bReached);
        TestFalse(label + TEXT(": no detour"), off.bDetoured);
        TestEqual(label + TEXT(": same step count as direct"), off.Steps, direct.Steps);
        LaneRouteVecExact(*this, label + TEXT(": bit-identical to direct"), off.FinalM, direct.FinalM);
        TestTrue(FString::Printf(TEXT("%s: closest %.6g m inside the zone"), *label, off.MinSunM),
            off.MinSunM < LANEROUTE_ZONE_M);

        // Review fix: a clear trip sunward to a target outside the zone (the braking path at the order's own
        // deceleration never reaches the zone) is unchanged by the safety, bit for bit
        const FVector3d nearTarget(3e10, 0.0, 0.0);
        const FLaneRouteTrip clearDirect = LaneRouteFly(start, nearTarget, ELaneRouteAimMode::Direct, bWarPower, 10.0,
            600.0 * 3600.0);
        const FLaneRouteTrip clearOn = LaneRouteFly(start, nearTarget, ELaneRouteAimMode::SafetyOn, bWarPower, 10.0,
            600.0 * 3600.0);
        TestTrue(label + TEXT(", clear sunward trip: arrives"), clearOn.bReached);
        TestFalse(label + TEXT(", clear sunward trip: no detour"), clearOn.bDetoured);
        TestEqual(label + TEXT(", clear sunward trip: same step count"), clearOn.Steps, clearDirect.Steps);
        LaneRouteVecExact(*this, label + TEXT(", clear sunward trip: bit-identical"), clearOn.FinalM,
            clearDirect.FinalM);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoTransientTest, "SOLTest.LaneRouteAuto.PlanTransientAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 6.4 / plan 53e step 2: the plan and the detour latch are plain transient fields (never reflected, never saved),
// empty by default, and every order write resets them with the rest of the lane state; the kinematic state is kept
bool FSOLLaneRouteAutoTransientTest::RunTest(const FString& /*parameters*/)
{
    FSOLShipStateFragment fresh;
    TestEqual(TEXT("new fragment: no plan"), fresh.LanePlan.HopCount, 0);
    TestFalse(TEXT("new fragment: no detour"), fresh.bSunDetour);
    TestNull(TEXT("LanePlan not reflected"), FindFProperty<FProperty>(FSOLShipStateFragment::StaticStruct(),
        TEXT("LanePlan")));
    TestNull(TEXT("bSunDetour not reflected"), FindFProperty<FProperty>(FSOLShipStateFragment::StaticStruct(),
        TEXT("bSunDetour")));

    // A stale state of every lane field
    FSOLShipStateFragment state;
    state.State.PositionM = FVector3d(1.0e11, 2.0e10, 3.0e9);
    state.State.VelocityMps = FVector3d(1.0e4, 0.0, 0.0);
    state.LaneLeg.bFound = true;
    state.LaneLeg.Lane = 3;
    state.LaneMode = ESOLLaneAutoMode::Hold;
    state.WarPowerPulse.bRecovering = true;
    state.LanePlan = LaneRoutePlan03(*this);
    state.LanePlan.CurrentHop = 1;
    state.LanePlan.bHopLeftEarly = true;
    state.bSunDetour = true;
    state.bSunStoppedAtZone = true;
    state.bLaneLeftForSun = true;
    SOLAutopilotOrder::ResetLaneState(state);
    TestFalse(TEXT("reset: not stopped at the zone"), state.bSunStoppedAtZone);
    TestFalse(TEXT("reset: no lane-closed notice"), state.bLaneLeftForSun);
    TestFalse(TEXT("reset: no hop left early"), state.LanePlan.bHopLeftEarly);
    TestFalse(TEXT("new fragment: not stopped"), fresh.bSunStoppedAtZone);
    TestFalse(TEXT("new fragment: no lane-closed notice"), fresh.bLaneLeftForSun);
    TestNull(TEXT("bLaneLeftForSun not reflected"), FindFProperty<FProperty>(FSOLShipStateFragment::StaticStruct(),
        TEXT("bLaneLeftForSun")));
    TestFalse(TEXT("reset: no leg"), state.LaneLeg.bFound);
    TestEqual(TEXT("reset: mode None"), static_cast<int32>(state.LaneMode), static_cast<int32>(ESOLLaneAutoMode::None));
    TestFalse(TEXT("reset: pulse phase"), state.WarPowerPulse.bRecovering);
    TestEqual(TEXT("reset: no plan hops"), state.LanePlan.HopCount, 0);
    TestEqual(TEXT("reset: hop 0"), state.LanePlan.CurrentHop, 0);
    TestFalse(TEXT("reset: no plan"), SOLLaneRoutePlan::HasPlan(state.LanePlan));
    TestFalse(TEXT("reset: no detour"), state.bSunDetour);
    LaneRouteVecExact(*this, TEXT("reset: position kept"), state.State.PositionM, FVector3d(1.0e11, 2.0e10, 3.0e9));
    LaneRouteVecExact(*this, TEXT("reset: velocity kept"), state.State.VelocityMps, FVector3d(1.0e4, 0.0, 0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoActionRowTest, "SOLTest.LaneRouteAuto.ToggleSunSafetyRow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// O4 UI: "Toggle Sun Safety", appended after MoveToWarPower (no ordinal moves), live right after Move To (War Power) on
// the Star, Planet, Moon and Dwarf rows (11 rows, MAX_ACTIONS 11), on no other type, blocked only by NoShip
bool FSOLLaneRouteAutoActionRowTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLActionTable;
    const ESOLAction sun = LaneRouteSunAction();
    TestEqual(TEXT("appended: the last before Count"), static_cast<int32>(ESOLAction::Count),
        static_cast<int32>(ESOLAction::MoveToWarPower) + 2);
    TestEqual(TEXT("MoveToWarPower keeps its ordinal"), static_cast<int32>(ESOLAction::MoveToWarPower),
        static_cast<int32>(ESOLAction::Trade) + 1);
    TestEqual(TEXT("MAX_ACTIONS is 11"), SOL::Interact::MAX_ACTIONS, 11);

    for (const ESOLObjectType type : LANEROUTE_BODY_TYPES)
    {
        const FString name = TypeName(type);
        TestTrue(name + TEXT(": offered"), Offers(type, sun));
        TestFalse(name + TEXT(": live"), IsPlaceholder(type, sun));
        const int32 warRow = LaneRouteRowOf(type, ESOLAction::MoveToWarPower);
        TestTrue(name + TEXT(": War power present"), warRow != INDEX_NONE);
        TestEqual(name + TEXT(": right after Move To (War Power)"), LaneRouteRowOf(type, sun), warRow + 1);
        TestEqual(name + TEXT(": 11 rows"), RowCount(type), 11);
        TestEqual(name + TEXT(": fills MAX_ACTIONS"), RowCount(type), SOL::Interact::MAX_ACTIONS);

        // Blocked only by NoShip, in every one of the 512 contexts
        int32 mismatches = 0;
        for (int32 mask = 0; mask < (1 << 9); ++mask)
        {
            const FSOLActionContext context = LaneRouteContextFromMask(mask);
            const ESOLActionBlock expected = context.bHasShip ? ESOLActionBlock::None : ESOLActionBlock::NoShip;
            mismatches += Evaluate(type, sun, context) != expected ? 1 : 0;
        }
        TestEqual(name + TEXT(": blocked only by NoShip"), mismatches, 0);
    }
    for (const ESOLObjectType type : { ESOLObjectType::Asteroid, ESOLObjectType::Ship, ESOLObjectType::DroppedTarget })
    {
        TestFalse(FString(TypeName(type)) + TEXT(": not offered"), Offers(type, sun));
    }

    // The fixed label from SOLMenuText
    TestEqual(TEXT("label"), FString(ActionLabel(sun)), FString(TEXT("Toggle Sun Safety")));
    const int32 index = static_cast<int32>(sun);
    TestEqual(TEXT("one label per action"), static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::INTERACT_ACTION_LABELS)),
        static_cast<int32>(ESOLAction::Count));
    if (index < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::INTERACT_ACTION_LABELS)))
    {
        TestEqual(TEXT("label from SOLMenuText"), FString(ActionLabel(sun)),
            FString(SOLMenuText::INTERACT_ACTION_LABELS[index]));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoSessionFlagTest, "SOLTest.LaneRouteAuto.SunSafetySessionOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// O4 persistence: the override is a session flag on the ship subsystem, safety on by default, flipped by the row, not
// reflected (nothing saves it), and no save schema or key layout version moves
bool FSOLLaneRouteAutoSessionFlagTest::RunTest(const FString& /*parameters*/)
{
    const TStrongObjectPtr<USOLShipSubsystem> ships(NewObject<USOLShipSubsystem>(GetTransientPackage()));
    TestFalse(TEXT("default: safety on"), ships->IsSunSafetyOverridden());
    ships->ToggleSunSafetyOverride();
    TestTrue(TEXT("toggled: overridden"), ships->IsSunSafetyOverridden());
    ships->ToggleSunSafetyOverride();
    TestFalse(TEXT("toggled again: safety on"), ships->IsSunSafetyOverridden());

    int32 reflected = 0;
    for (TFieldIterator<FProperty> it(USOLShipSubsystem::StaticClass()); it; ++it)
    {
        reflected += it->GetName().Contains(TEXT("SunSafety")) ? 1 : 0;
    }
    TestEqual(TEXT("no reflected Sun safety property"), reflected, 0);
    TestEqual(TEXT("SAVE_VERSION stays 13"), SOL::Settings::SAVE_VERSION, 13);
    TestEqual(TEXT("KEY_BINDING_LAYOUT_VERSION stays 10"), SOL::Settings::KEY_BINDING_LAYOUT_VERSION, 10);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoHudTest, "SOLTest.LaneRouteAuto.HudLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Q16 and O4: "VIA <node>" names the current hop's end node while a later hop remains; "SUN SAFETY OFF" shows while
// the override is on (an AUTO order or not)
bool FSOLLaneRouteAutoHudTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    FSOLLaneRoutePlan plan = LaneRoutePlan03(*this);
    TestEqual(TEXT("hop 0: VIA node 1"), ViaNode(plan), 1);
    LeaveHop(plan);
    TestEqual(TEXT("hop 1: VIA node 2"), ViaNode(plan), 2);
    LeaveHop(plan);
    TestEqual(TEXT("last hop: no VIA"), ViaNode(plan), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("no plan: no VIA"), ViaNode(FSOLLaneRoutePlan()), static_cast<int32>(INDEX_NONE));
    const TArray<FSOLLaneEdge> graph = LaneRouteGraph();
    FSOLLaneRoutePlan single;
    TestTrue(TEXT("single-hop plan made"), MakePlan(LaneRouteFind(graph, 2, 3), graph, 2, LANEROUTE_PENALTY_S,
        LANEROUTE_WALL_MPS, 7, 1u, single));
    TestEqual(TEXT("single hop: no VIA"), ViaNode(single), static_cast<int32>(INDEX_NONE));

    TStringBuilder<64> via;
    SOLAutopilotOrder::AppendViaLine(via, TEXT("Mars"));
    TestEqual(TEXT("VIA line"), FString(via.ToString()), FString(TEXT("VIA Mars")));

    TestEqual(TEXT("override on: SUN SAFETY OFF"), FString(SOLAutopilotOrder::SunSafetyLineText(true)),
        FString(TEXT("SUN SAFETY OFF")));
    TestTrue(TEXT("override off: no line"), FString(SOLAutopilotOrder::SunSafetyLineText(false)).IsEmpty());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoPolylineTest, "SOLTest.LaneRouteAuto.MapPolyline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Q16: the map route runs from the ship through the current positions of the remaining hops' end nodes, follows the
// moving nodes, and draws nothing without a plan, with a missing node or without room for every point
bool FSOLLaneRouteAutoPolylineTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneRoutePlan::PlanPolylineM;
    FSOLLaneRoutePlan plan = LaneRoutePlan03(*this);
    TArray<FVector3d> nodes = { FVector3d(1e11, 0.0, 0.0), FVector3d(2e11, 0.0, 0.0), FVector3d(3e11, 1e10, 0.0),
        FVector3d(4e11, 2e10, 0.0), FVector3d(5e11, 0.0, 0.0) };
    const FVector3d ship(1.1e11, 1e9, 0.0);
    FVector3d points[SOL::NPC_LANE_ROUTE_MAX_HOPS + 1];
    for (FVector3d& point : points)
    {
        point = FVector3d::ZeroVector;
    }

    // Hop 0: ship, node 1, node 2, node 3
    TestEqual(TEXT("hop 0: 4 points"), PlanPolylineM(plan, nodes, ship, points), 4);
    LaneRouteVecExact(*this, TEXT("hop 0: point 0 the ship"), points[0], ship);
    LaneRouteVecExact(*this, TEXT("hop 0: point 1 node 1"), points[1], nodes[1]);
    LaneRouteVecExact(*this, TEXT("hop 0: point 2 node 2"), points[2], nodes[2]);
    LaneRouteVecExact(*this, TEXT("hop 0: point 3 node 3"), points[3], nodes[3]);

    // The nodes move: the next frame's line follows
    nodes[2] = FVector3d(3.1e11, 1.2e10, 0.0);
    TestEqual(TEXT("moved: 4 points"), PlanPolylineM(plan, nodes, ship, points), 4);
    LaneRouteVecExact(*this, TEXT("moved: node 2 where it is now"), points[2], nodes[2]);

    // Hop 1: ship, node 2, node 3
    SOLLaneRoutePlan::LeaveHop(plan);
    TestEqual(TEXT("hop 1: 3 points"), PlanPolylineM(plan, nodes, ship, points), 3);
    LaneRouteVecExact(*this, TEXT("hop 1: point 1 node 2"), points[1], nodes[2]);

    // Nothing drawn
    TestEqual(TEXT("no plan"), PlanPolylineM(FSOLLaneRoutePlan(), nodes, ship, points), 0);
    const TArray<FVector3d> fewNodes = { nodes[0], nodes[1], nodes[2] };
    TestEqual(TEXT("node 3 missing"), PlanPolylineM(plan, fewNodes, ship, points), 0);
    TestEqual(TEXT("no room for every point"), PlanPolylineM(plan, nodes, ship, TArrayView<FVector3d>(points, 2)),
        0);

    // The order's own target (a moon of the last node) replaces the last point; the others stay
    const FVector3d moon(nodes[3].X + 3.8e8, nodes[3].Y, 0.0);
    TestEqual(TEXT("moon target: 3 points"), PlanPolylineM(plan, nodes, ship, points, &moon), 3);
    LaneRouteVecExact(*this, TEXT("moon target: last point the moon"), points[2], moon);
    LaneRouteVecExact(*this, TEXT("moon target: node 2 kept"), points[1], nodes[2]);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoClosingLaneTest, "SOLTest.LaneRouteAuto.ClosingLaneRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 6.5 (review fix): a held lane that closes and is not safe to finish, or the Sun aim latching over a lane, leaves
// it for good: with a later hop the hop is left early (no lane rules until its end node, then the next hop), on the
// last hop or without a plan per-hop Left (final); the leg is always dropped, so the lane's rules never resume
bool FSOLLaneRouteAutoClosingLaneTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    FSOLLaneAutoLeg heldLeg;
    heldLeg.bFound = true;
    heldLeg.Lane = 4;

    // Hop 0 of 3: left early, mode None, the plan kept on hop 0
    FSOLLaneRoutePlan plan = LaneRoutePlan03(*this);
    ESOLLaneAutoMode mode = ESOLLaneAutoMode::Hold;
    FSOLLaneAutoLeg leg = heldLeg;
    ForceLeaveLane(plan, mode, leg);
    TestFalse(TEXT("hop 0: leg dropped"), leg.bFound);
    TestEqual(TEXT("hop 0: mode None"), static_cast<int32>(mode), static_cast<int32>(ESOLLaneAutoMode::None));
    TestTrue(TEXT("hop 0: left early"), plan.bHopLeftEarly);
    TestEqual(TEXT("hop 0: still hop 0 (fly on to its end node)"), plan.CurrentHop, 0);
    TestEqual(TEXT("hop 0: VIA still its end node"), ViaNode(plan), 1);

    // Not yet at the node: nothing changes; at the node: the hop ends, the next is hop 1 with the flag cleared
    TestFalse(TEXT("away from the node: the hop goes on"), FinishEarlyHop(plan, false));
    TestEqual(TEXT("away from the node: hop 0"), plan.CurrentHop, 0);
    TestTrue(TEXT("at the node: the hop ends"), FinishEarlyHop(plan, true));
    TestEqual(TEXT("at the node: hop 1"), plan.CurrentHop, 1);
    TestFalse(TEXT("at the node: flag cleared"), plan.bHopLeftEarly);
    TestFalse(TEXT("a hop not left early never ends there"), FinishEarlyHop(plan, true));
    TestEqual(TEXT("not left early: still hop 1"), plan.CurrentHop, 1);

    // The last hop: per-hop Left, no plan left
    LeaveHop(plan);
    mode = ESOLLaneAutoMode::Approach;
    leg = heldLeg;
    ForceLeaveLane(plan, mode, leg);
    TestEqual(TEXT("last hop: Left"), static_cast<int32>(mode), static_cast<int32>(ESOLLaneAutoMode::Left));
    TestFalse(TEXT("last hop: no plan"), HasPlan(plan));
    TestFalse(TEXT("last hop: leg dropped"), leg.bFound);

    // No plan (a D20 hold): Left, final
    FSOLLaneRoutePlan empty;
    mode = ESOLLaneAutoMode::Hold;
    leg = heldLeg;
    ForceLeaveLane(empty, mode, leg);
    TestEqual(TEXT("no plan: Left"), static_cast<int32>(mode), static_cast<int32>(ESOLLaneAutoMode::Left));
    TestFalse(TEXT("no plan: leg dropped"), leg.bFound);
    TestFalse(TEXT("no plan: nothing left early"), empty.bHopLeftEarly);

    // The finish decision that feeds it: a closing lane with an unsafe rest leaves unless overridden
    TestFalse(TEXT("closing, unsafe: leave"), FinishClosingHop(true, false, false));
    TestTrue(TEXT("closing, unsafe, override: finish"), FinishClosingHop(true, false, true));
    TestTrue(TEXT("closing, safe: finish"), FinishClosingHop(true, true, false));

    // ClearPlan forgets a hop left early
    FSOLLaneRoutePlan early = LaneRoutePlan03(*this);
    early.bHopLeftEarly = true;
    ClearPlan(early);
    TestFalse(TEXT("cleared: flag gone"), early.bHopLeftEarly);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoNeverHeldLeftTest, "SOLTest.LaneRouteAuto.NeverHeldHopLeftEarly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 6.5 (53e smoke fix, user decision 2026-11-26): the lane rules' Left on a plan hop that never held its lane
// (Approach or a search straight to Left) far from its end node, with a later hop, leaves the hop early like
// ForceLeaveLane (fly on to the node, no lane search, re-plan there; the plan and VIA kept). A held hop, a hop at its
// node, the last hop and no plan keep per-hop Left (test-after, written with the fix)
bool FSOLLaneRouteAutoNeverHeldLeftTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    FSOLLaneAutoLeg approachLeg;
    approachLeg.bFound = true;
    approachLeg.Lane = 7;

    // Approach -> Left far from the node on hop 0 of 3: left early, no search, still hop 0 with its VIA node
    FSOLLaneRoutePlan plan = LaneRoutePlan03(*this);
    ESOLLaneAutoMode mode = ESOLLaneAutoMode::Left;
    FSOLLaneAutoLeg leg = approachLeg;
    TestFalse(TEXT("never held, away: no search"), LeaveHopOnLaneRulesLeft(plan, ESOLLaneAutoMode::Approach, false,
        mode, leg));
    TestEqual(TEXT("never held, away: mode None"), static_cast<int32>(mode), static_cast<int32>(ESOLLaneAutoMode::None));
    TestFalse(TEXT("never held, away: leg dropped"), leg.bFound);
    TestTrue(TEXT("never held, away: left early"), plan.bHopLeftEarly);
    TestEqual(TEXT("never held, away: still hop 0"), plan.CurrentHop, 0);
    TestEqual(TEXT("never held, away: plan kept"), plan.HopCount, 3);
    TestEqual(TEXT("never held, away: VIA kept"), ViaNode(plan), 1);

    // It then ends at the node like any hop left early, and the next hop is searched there
    TestTrue(TEXT("never held: ends at the node"), FinishEarlyHop(plan, true));
    TestEqual(TEXT("never held: hop 1 next"), plan.CurrentHop, 1);

    // A search straight to Left (mode None before) far from the node: the same
    FSOLLaneRoutePlan searched = LaneRoutePlan03(*this);
    mode = ESOLLaneAutoMode::Left;
    leg = approachLeg;
    LeaveHopOnLaneRulesLeft(searched, ESOLLaneAutoMode::None, false, mode, leg);
    TestTrue(TEXT("search to Left, away: left early"), searched.bHopLeftEarly);
    TestEqual(TEXT("search to Left, away: still hop 0"), searched.CurrentHop, 0);

    // A held hop leaving at its end ring (often beyond the at-node radius): per-hop Left, the next hop searched
    FSOLLaneRoutePlan held = LaneRoutePlan03(*this);
    mode = ESOLLaneAutoMode::Left;
    leg = approachLeg;
    TestTrue(TEXT("held: next hop searched"), LeaveHopOnLaneRulesLeft(held, ESOLLaneAutoMode::Hold, false, mode, leg));
    TestEqual(TEXT("held: hop 1"), held.CurrentHop, 1);
    TestEqual(TEXT("held: mode None"), static_cast<int32>(mode), static_cast<int32>(ESOLLaneAutoMode::None));
    TestFalse(TEXT("held: leg dropped"), leg.bFound);
    TestFalse(TEXT("held: not left early"), held.bHopLeftEarly);

    // Never held but already at the node: per-hop Left
    FSOLLaneRoutePlan atNode = LaneRoutePlan03(*this);
    mode = ESOLLaneAutoMode::Left;
    leg = approachLeg;
    TestTrue(TEXT("at the node: next hop searched"), LeaveHopOnLaneRulesLeft(atNode, ESOLLaneAutoMode::Approach, true,
        mode, leg));
    TestEqual(TEXT("at the node: hop 1"), atNode.CurrentHop, 1);
    TestFalse(TEXT("at the node: not left early"), atNode.bHopLeftEarly);

    // The last hop, never held and away: per-hop Left (final as today), no plan left
    FSOLLaneRoutePlan last = LaneRoutePlan03(*this);
    LeaveHop(last);
    LeaveHop(last);
    mode = ESOLLaneAutoMode::Left;
    leg = approachLeg;
    TestFalse(TEXT("last hop: no search"), LeaveHopOnLaneRulesLeft(last, ESOLLaneAutoMode::Approach, false, mode, leg));
    TestEqual(TEXT("last hop: Left"), static_cast<int32>(mode), static_cast<int32>(ESOLLaneAutoMode::Left));
    TestFalse(TEXT("last hop: no plan"), HasPlan(last));
    TestFalse(TEXT("last hop: not left early"), last.bHopLeftEarly);

    // No plan (a D20 lane): Left, the leg untouched
    FSOLLaneRoutePlan empty;
    mode = ESOLLaneAutoMode::Left;
    leg = approachLeg;
    TestFalse(TEXT("no plan: no search"), LeaveHopOnLaneRulesLeft(empty, ESOLLaneAutoMode::Approach, false, mode, leg));
    TestEqual(TEXT("no plan: Left"), static_cast<int32>(mode), static_cast<int32>(ESOLLaneAutoMode::Left));
    TestFalse(TEXT("no plan: nothing left early"), empty.bHopLeftEarly);

    // The epoch re-plan keeps a hop left early (the direct flight to the node spans many daily epochs)
    FSOLLaneRoutePlan replanned = LaneRoutePlan03(*this);
    mode = ESOLLaneAutoMode::Left;
    leg = approachLeg;
    LeaveHopOnLaneRulesLeft(replanned, ESOLLaneAutoMode::Approach, false, mode, leg);
    FSOLLaneRoute noRoute;
    ReplaceRemainingHops(replanned, noRoute, TConstArrayView<FSOLLaneEdge>(), 0.0, 1.0, replanned.PlannedEpoch + 1);
    TestTrue(TEXT("re-plan: still left early"), replanned.bHopLeftEarly);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoArrivalTest, "SOLTest.LaneRouteAuto.NoArrivalWhileLatched",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The flight run's arrival rule (SOLLaneRoutePlan::OrderArrived, called by USOLShipFlightProcessor): an arrival flag
// counts only for an autopilot order that is not a chase, not in a lane step, not latched on the Sun detour and not
// short of a later plan hop's node (arriving at a VIA node's standoff is no arrival)
bool FSOLLaneRouteAutoArrivalTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    TestTrue(TEXT("plain arrival"), OrderArrived(true, false, false, false, false, true));
    TestFalse(TEXT("no flag"), OrderArrived(true, false, false, false, false, false));
    TestFalse(TEXT("latched on the detour"), OrderArrived(true, false, false, true, false, true));
    TestFalse(TEXT("at a VIA node"), OrderArrived(true, false, false, false, true, true));
    TestFalse(TEXT("a lane step (merge point)"), OrderArrived(true, false, true, false, false, true));
    TestFalse(TEXT("a chase"), OrderArrived(true, true, false, false, false, true));
    TestFalse(TEXT("no autopilot"), OrderArrived(false, false, false, false, false, true));

    // Every combination: true only for the plain case
    for (int32 mask = 0; mask < 64; ++mask)
    {
        const bool bExpected = mask == 0b100001;
        TestEqual(FString::Printf(TEXT("mask %d"), mask), OrderArrived((mask & 1) != 0, (mask & 2) != 0,
            (mask & 4) != 0, (mask & 8) != 0, (mask & 16) != 0, (mask & 32) != 0), bExpected);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoMotionTest, "SOLTest.LaneRouteAuto.BrakingPathAndSweptSubstep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix: the aim segment alone misses a ship whose velocity carries it into the zone. Its braking path (ship ->
// ship + v|v| / 2a) or this substep's swept segment (ship -> ship + v dt) entering the zone latches the detour; a
// motion that stays out changes nothing (bit for bit), and it never holds the latch outside the zone
bool FSOLLaneRouteAutoMotionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    const FVector3d ship(1e11, 0.0, 0.0);
    const FVector3d clearTarget(1e11, 1e11, 0.0);           // The aim segment stays far from the Sun
    const FVector3d sunward(-1.0e5, 0.0, 0.0);              // 100 km/s straight at the Sun

    // SunMotionDistanceM: at 0.1 m/s2 the braking path is 1e5^2 / (2 x 0.1) = 5e10 m long, so it stops 5e10 m out
    TestTrue(TEXT("no checks: infinity"), SunMotionDistanceM(ship, sunward, 0.0, 0.0) == LANEROUTE_INF);
    TestNearlyEqual(TEXT("braking path at 0.1 m/s2 ends 5e10 from the Sun"), SunMotionDistanceM(ship, sunward, 0.1,
        0.0), 5.0e10, 1.0);
    TestNearlyEqual(TEXT("braking path at 0.02 m/s2 passes the Sun"), SunMotionDistanceM(ship, sunward, 0.02, 0.0),
        0.0, 1.0);
    TestNearlyEqual(TEXT("swept 1e6 s substep passes the Sun"), SunMotionDistanceM(ship, sunward, 0.0, 1.0e6), 0.0,
        1.0);
    TestNearlyEqual(TEXT("swept 1e5 s substep ends 9e10 out"), SunMotionDistanceM(ship, sunward, 0.0, 1.0e5), 9.0e10,
        1.0);

    // The aim: a clear aim segment with a safe motion flies the target bit for bit
    const FSOLSunSafeAim safe = SunSafeAim(ship, sunward, clearTarget, true, false, LANEROUTE_ZONE_M,
        LANEROUTE_EDGE_M, LANEROUTE_MIN_ARC_RAD, 0.1, 10.0);
    LaneRouteVecExact(*this, TEXT("safe motion: the target bit for bit"), safe.AimM, clearTarget);
    TestFalse(TEXT("safe motion: no detour"), safe.bDetour);

    // The braking path into the zone latches the detour
    const FSOLSunSafeAim braking = SunSafeAim(ship, sunward, clearTarget, true, false, LANEROUTE_ZONE_M,
        LANEROUTE_EDGE_M, LANEROUTE_MIN_ARC_RAD, 0.02, 10.0);
    TestTrue(TEXT("braking path into the zone: detour"), braking.bDetour);
    TestNearlyEqual(TEXT("braking path: the waypoint on k_on"), braking.AimM.Size(), LANEROUTE_EDGE_M, 1.0e3);

    // A long warp substep's swept segment into the zone latches it too
    const FSOLSunSafeAim swept = SunSafeAim(ship, sunward, clearTarget, true, false, LANEROUTE_ZONE_M,
        LANEROUTE_EDGE_M, LANEROUTE_MIN_ARC_RAD, 0.0, 1.0e6);
    TestTrue(TEXT("swept substep into the zone: detour"), swept.bDetour);

    // Latched, with the motion between k_off and k_on (a ship circling the edge): the motion does not hold the latch,
    // the aim segment's release decides (clear here: released)
    const FVector3d tangent(1.0e4, 0.0, 0.0);
    const FVector3d farTarget(1e11, 1.6e10, 0.0);
    const FSOLSunSafeAim circling = SunSafeAim(FVector3d(0.0, 1.5e10, 0.0), tangent, farTarget, true, true,
        LANEROUTE_ZONE_M, LANEROUTE_EDGE_M, LANEROUTE_MIN_ARC_RAD, 1.0, 10.0);
    TestFalse(TEXT("circling at 1.5e10, moving away: released"), circling.bDetour);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneRouteAutoMidFrameHopTest, "SOLTest.LaneRouteAuto.MidFrameHopAim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (pure part of the flight run's rule): after a per-hop Left with later hops the lane rules' target and the
// direct flight's aim are the new current hop's end node (ViaNode), not the order's own target, so the Sun check runs
// on the node the ship flies to. The processor wiring itself (that it passes the flown aim) is NOT covered by any test
// yet (the planned live smoke -SOLSmokeLaneRoute is not built)
bool FSOLLaneRouteAutoMidFrameHopTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLLaneRoutePlan;
    FSOLLaneRoutePlan plan = LaneRoutePlan03(*this);       // 0 -> 1 -> 2 -> 3
    TestEqual(TEXT("hop 0: VIA node 1"), ViaNode(plan), 1);
    TestEqual(TEXT("per-hop Left: None"), static_cast<int32>(LeaveHop(plan)),
        static_cast<int32>(ESOLLaneAutoMode::None));
    TestEqual(TEXT("hop 1: VIA node 2 (the aim from the next substep on)"), ViaNode(plan), 2);
    LeaveHop(plan);
    TestEqual(TEXT("last hop: no VIA (the order's own target)"), ViaNode(plan), static_cast<int32>(INDEX_NONE));

    // A Sun check on the hop's node: a node across the Sun from the ship gets the detour, the same aim the step flies
    const FVector3d ship(1e11, 0.0, 0.0);
    const FVector3d nodeAcross(-1e11, 5e9, 0.0);
    const FSOLSunSafeAim aim = SunSafeAim(ship, FVector3d::ZeroVector, nodeAcross, true, false, LANEROUTE_ZONE_M,
        LANEROUTE_EDGE_M, LANEROUTE_MIN_ARC_RAD, 0.0, 0.0);
    TestTrue(TEXT("node across the Sun: detour"), aim.bDetour);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
