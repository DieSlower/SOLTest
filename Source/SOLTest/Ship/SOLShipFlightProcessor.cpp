/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipFlightProcessor.h"

#include "Flight/SOLChase.h"
#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Level/SOLSurfaceLock.h"
#include "Ship/SOLShipFragments.h"
#include "SOLConstants.h"
#include "Universe/SOLBodyFrameCache.h"
#include "Universe/SOLRenderPlacement.h"
#include "Visuals/SOLThrusterMath.h"

#include "MassExecutionContext.h"

#include <limits>

namespace
{
    // The most lanes a ship's per-substep lane scratch holds (the population's lane count is at most this)
    constexpr int32 FLIGHT_MAX_LANES = SOL::NPC_GROUP_TEXTURE_WIDTH;

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane view in ecliptic axes from its table entry and the Unreal-handed bodies (invalid frame without
    // its bodies)
    FSOLLaneMergeLane FlightLaneView(const FSOLLaneTrafficEntry& entry, const TConstArrayView<FVector3d> bodiesM,
        const TConstArrayView<FVector3d> bodyVelocitiesMps)
    {
        FSOLLaneMergeLane lane;
        lane.Profile = entry.Profile;
        lane.BodyA = entry.BodyA;
        lane.BodyB = entry.BodyB;
        lane.bOpenNow = entry.bOpenNow;
        if (!bodiesM.IsValidIndex(entry.BodyA) || !bodiesM.IsValidIndex(entry.BodyB)
            || !bodyVelocitiesMps.IsValidIndex(entry.BodyA) || !bodyVelocitiesMps.IsValidIndex(entry.BodyB))
        {
            return lane;
        }
        lane = SOLLaneTraffic::MakeMergeLane(entry.Profile, entry.BodyA, entry.BodyB,
            SOLRender::EclipticToUnreal(bodiesM[entry.BodyA]), SOLRender::EclipticToUnreal(bodiesM[entry.BodyB]),
            SOLRender::EclipticToUnreal(bodyVelocitiesMps[entry.BodyA]),
            SOLRender::EclipticToUnreal(bodyVelocitiesMps[entry.BodyB]));
        lane.bOpenNow = entry.bOpenNow;
        return lane;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a guidance output with its vectors mirrored between ecliptic and Unreal axes (the mirror is its own
    // inverse)
    FSOLGuidanceOutput FlightMirrorGuidance(const FSOLGuidanceOutput& guidance)
    {
        FSOLGuidanceOutput out = guidance;
        out.DesiredNetAccelMps2 = SOLRender::EclipticToUnreal(guidance.DesiredNetAccelMps2);
        out.DesiredThrustAccelMps2 = SOLRender::EclipticToUnreal(guidance.DesiredThrustAccelMps2);
        out.DesiredForward = SOLRender::EclipticToUnreal(guidance.DesiredForward);
        return out;
    }
}

//////////////////////////////////////////////////////////////////////////
// Registers the query, opts out of the automatic processing phases and builds the chunk callback once
USOLShipFlightProcessor::USOLShipFlightProcessor()
    : mQuery(*this)
{
    bAutoRegisterWithProcessingPhases = false;
    ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);

    // The flight step issues no Mass commands, so parallel jobs need no command buffers of their own
    mQuery.SetParallelCommandBufferEnabled(false);

    // Forwards each chunk to ProcessChunk
    mChunkFunction = [this](FMassExecutionContext& context)
    {
        ProcessChunk(context);
    };
}

//////////////////////////////////////////////////////////////////////////
// Points the processor at the body data it reads each run and sizes the substep scratch once for it
void USOLShipFlightProcessor::SetBodyCache(const FSOLBodyFrameCache* bodyCache)
{
    mBodyCache = bodyCache;
    const int32 bodyCount = bodyCache != nullptr ? bodyCache->Num() : 0;
    mSubstepBodyPositionsM.SetNumUninitialized((SOL::SHIP_MAX_SUBSTEPS + 1) * bodyCount);
    mSimSubstepBodyPositionsM.SetNumUninitialized((SOL::AUTOPILOT_MAX_SUBSTEPS + 1) * bodyCount);
}

//////////////////////////////////////////////////////////////////////////
// Declares the fragments the flight step reads and writes
void USOLShipFlightProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& /*entityManager*/)
{
    mQuery.AddRequirement<FSOLShipStateFragment>(EMassFragmentAccess::ReadWrite);
    mQuery.AddRequirement<FSOLShipControlFragment>(EMassFragmentAccess::ReadOnly);
    mQuery.AddRequirement<FSOLShipChaseFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
    mQuery.AddConstSharedRequirement<FSOLShipParamsFragment>();
}

//////////////////////////////////////////////////////////////////////////
// Prepares the substep body positions, then steps every matching chunk in parallel with the context's delta time
void USOLShipFlightProcessor::Execute(FMassEntityManager& /*entityManager*/, FMassExecutionContext& context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLShipFlightProcessor::Execute);
    const double deltaS = static_cast<double>(context.GetDeltaTimeSeconds());
    if (mBodyCache == nullptr || deltaS <= 0.0
        || mSubstepBodyPositionsM.Num() < (SOL::SHIP_MAX_SUBSTEPS + 1) * mBodyCache->Num()
        || mSimSubstepBodyPositionsM.Num() < (SOL::AUTOPILOT_MAX_SUBSTEPS + 1) * mBodyCache->Num())
    {
        return;
    }

    // Fixed-length substeps keep Newtonian gravity stable; the cap covers SOL::MAX_FRAME_DELTA_S, so the min is a guard
    const double budgetS = FMath::Min(deltaS, SOL::SHIP_MAX_SUBSTEP_S * SOL::SHIP_MAX_SUBSTEPS);
    mFrameRealS = budgetS;
    mSubstepCount = FMath::Clamp(FMath::CeilToInt32(budgetS / SOL::SHIP_MAX_SUBSTEP_S), 1, SOL::SHIP_MAX_SUBSTEPS);
    mSubstepS = budgetS / mSubstepCount;
    mCarryPerSubstepM = mFrame.CarryPositionM / static_cast<double>(mSubstepCount);
    PrepareSubstepBodyPositions(budgetS, mSubstepCount, mSubstepBodyPositionsM);

    // Under user warp an autopilot ship integrates the frame's sim time instead (the bodies' own interval)
    const double warpFactor = FMath::Max(mFrame.WarpFactor, 1.0);
    mSimSubstepCount = warpFactor > 1.0 ? SOLFlight::AutopilotSubstepCount(warpFactor * budgetS) : 0;
    mSimSubstepS = mSimSubstepCount > 0 ? warpFactor * budgetS / mSimSubstepCount : 0.0;
    if (mSimSubstepCount > 0)
    {
        PrepareSubstepBodyPositions(budgetS, mSimSubstepCount, mSimSubstepBodyPositionsM);
    }

    // This run's lanes for the AUTO lane rules, in ecliptic axes at the frame-START bodies (boundary 0, the same on
    // both substep grids), where the ship and its target are classified too; the subsystem passes lanes only while an
    // AUTO order flies, so a frame without one builds none
    const TConstArrayView<FVector3d> startBodiesM(mSubstepBodyPositionsM.GetData(), mBodyCache->Num());
    mLaneViews.SetNum(FMath::Min(mFrame.Lanes.Num(), FLIGHT_MAX_LANES), EAllowShrinking::No);
    for (int32 lane = 0; lane < mLaneViews.Num(); ++lane)
    {
        mLaneViews[lane] = FlightLaneView(mFrame.Lanes[lane], startBodiesM, mBodyCache->VelocitiesMps);
    }

    mQuery.ParallelForEachEntityChunk(context, mChunkFunction);
}

//////////////////////////////////////////////////////////////////////////
// Fills a scratch array with every body's position at each of substepCount + 1 evenly spaced frame boundaries
void USOLShipFlightProcessor::PrepareSubstepBodyPositions(const double frameS, const int32 substepCount,
    TArray<FVector3d>& outPositionsM) const
{
    const int32 bodyCount = mBodyCache->Num();
    const double warpFactor = FMath::Max(mFrame.WarpFactor, 1.0);
    const bool bHasPrevious = mFrame.PrevBodyPositionsM.Num() == bodyCount;
    const double boundaryRealS = frameS / substepCount;

    // Average velocity over the frame (displacement / sim time) so boundary 0 is exactly last frame's position; the
    // instantaneous velocity is the fallback on the first frame
    for (int32 body = 0; body < bodyCount; ++body)
    {
        const FVector3d& endM = mBodyCache->PositionsM[body];
        const FVector3d velocityMps = bHasPrevious
            ? (endM - mFrame.PrevBodyPositionsM[body]) / (warpFactor * frameS) : mBodyCache->VelocitiesMps[body];
        for (int32 boundary = 0; boundary <= substepCount; ++boundary)
        {
            const double remainingS = (substepCount - boundary) * boundaryRealS;
            outPositionsM[boundary * bodyCount + body] =
                SOLFlight::BodyPositionAtSubstep(endM, velocityMps, warpFactor, remainingS);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Steps the ships of one chunk: carry, gravity, flight or autopilot step, swept collision and the G meter per substep
void USOLShipFlightProcessor::ProcessChunk(FMassExecutionContext& context) const
{
    const TArrayView<FSOLShipStateFragment> states = context.GetMutableFragmentView<FSOLShipStateFragment>();
    const TConstArrayView<FSOLShipControlFragment> controls = context.GetFragmentView<FSOLShipControlFragment>();
    // Only the player ship's archetype carries the chase fragment; an empty view means a chunk without it
    const TArrayView<FSOLShipChaseFragment> chases = context.GetMutableFragmentView<FSOLShipChaseFragment>();
    const FSOLFlightParams& flightParams = context.GetConstSharedFragment<FSOLShipParamsFragment>().Params;

    const TConstArrayView<FVector3d> bodyVelocitiesMps = mBodyCache->VelocitiesMps;
    const TConstArrayView<double> bodyRadiiM = mBodyCache->RadiiM;
    const TConstArrayView<double> bodyGMs = mBodyCache->GMs;
    const int32 bodyCount = bodyVelocitiesMps.Num();

    const int32 entityCount = context.GetNumEntities();
    for (int32 entity = 0; entity < entityCount; ++entity)
    {
        const FSOLShipControlFragment& controlFragment = controls[entity];
        FSOLShipControl control = controlFragment.Control;
        const int32 alignBody = controlFragment.AlignBodyIndex;
        const int32 autopilotBody = controlFragment.AutopilotBodyIndex;
        const bool bAutopilot = autopilotBody >= 0 && autopilotBody < bodyCount;
        FSOLShipStateFragment& stateFragment = states[entity];
        FSOLShipState state = stateFragment.State;
        FSOLGMeterState meter = stateFragment.GMeter;
        FSOLGuidanceOutput guidance;
        double gLoad = 0.0;
        int32 contactBody = INDEX_NONE;
        bool bPointArrived = false;
        FSOLShipChaseFragment* const chaseFragment = chases.Num() > 0 ? &chases[entity] : nullptr;
        const bool bChase = chaseFragment != nullptr && chaseFragment->bChase;
        FSOLChaseRunState chaseRun;
        FSOLChaseGLimit chaseLimit;
        FSOLWarPowerPulseState warPowerPulse = stateFragment.WarPowerPulse;
        FSOLThrustMean thrustMean;      // The plume's thrust: the substeps' ship-frame thrust, time-weighted (SDD 51)

        // Real-time flight inherits its frame's velocity change under warp once, at the frame start; an autopilot or
        // a chase under warp integrates the frame's sim time itself and needs no carry
        const bool bSimTime = (bAutopilot || bChase) && mSimSubstepCount > 0;
        const int32 substepCount = bSimTime ? mSimSubstepCount : mSubstepCount;
        const double substepS = bSimTime ? mSimSubstepS : mSubstepS;
        const FVector3d* const substepBodies = bSimTime ? mSimSubstepBodyPositionsM.GetData()
            : mSubstepBodyPositionsM.GetData();
        if (!bSimTime)
        {
            state.VelocityMps += mFrame.CarryVelocityMps;
        }

        // Chase target interpolation inputs: the offset from its frame body at the frame end (that body's last
        // boundary is its end-of-frame position) and the velocity the offset moves at; no frame body, absolute
        int32 chaseFrameBody = INDEX_NONE;
        FVector3d chaseOffsetEndM = FVector3d::ZeroVector;
        FVector3d chaseRelVelocityMps = FVector3d::ZeroVector;
        FVector3d chaseOffsetVelocityMps = FVector3d::ZeroVector;
        if (bChase)
        {
            const FSOLShipChaseFragment& chase = *chaseFragment;
            chaseRun = chase.ChaseRun;
            chaseFrameBody = chase.ChaseFrameBodyIndex >= 0 && chase.ChaseFrameBodyIndex < bodyCount
                ? chase.ChaseFrameBodyIndex : INDEX_NONE;
            chaseOffsetEndM = chaseFrameBody != INDEX_NONE
                ? chase.ChaseTargetM - substepBodies[substepCount * bodyCount + chaseFrameBody] : chase.ChaseTargetM;
            chaseRelVelocityMps = chase.ChaseTargetVelocityMps - chase.ChaseFrameVelocityMps;
            chaseOffsetVelocityMps = chaseFrameBody != INDEX_NONE ? chaseRelVelocityMps : chase.ChaseTargetVelocityMps;
        }
        const double boundaryRealS = mFrameRealS / substepCount;
        const double warpFactor = FMath::Max(mFrame.WarpFactor, 1.0);

        // The order's own target with the bodies where they are (Unreal axes): the chase target, the waypoint, or the
        // standoff point above the body
        auto orderTargetM = [&](const TConstArrayView<FVector3d> bodiesM)
        {
            if (bChase)
            {
                return chaseFragment->ChaseTargetM;
            }
            const FVector3d& bodyM = bodiesM[autopilotBody];
            if (controlFragment.bAutopilotPoint)
            {
                return bodyM + controlFragment.AutopilotPointOffsetM;
            }
            return bodyM + controlFragment.AutopilotOffsetDir * SOLFlight::AutopilotStandoffM(bodyRadiiM[autopilotBody],
                bodyGMs[autopilotBody], SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter));
        };

        // The order target's velocity (Unreal axes): its body's, or the chase target's
        const FVector3d orderTargetVelocityMps = bChase ? chaseFragment->ChaseTargetVelocityMps
            : bAutopilot ? bodyVelocitiesMps[autopilotBody] : FVector3d::ZeroVector;

        // AUTO lane rules (SDD 50 section 4.9): the latched leg and mode of this order (Left is final), the lane views
        // copied once per ship from the frame-start bodies; the latched lane's view is rebuilt where its bodies are at
        // each substep. None for War power or without lanes
        const bool bLaneRules = (bAutopilot || bChase) && !controlFragment.bAutopilotIgnoreLanes
            && !mLaneViews.IsEmpty();
        FSOLLaneAutoLeg laneLeg = bLaneRules ? stateFragment.LaneLeg : FSOLLaneAutoLeg();
        ESOLLaneAutoMode laneMode = bLaneRules ? stateFragment.LaneMode : ESOLLaneAutoMode::None;
        const int32 laneCount = bLaneRules ? FMath::Min(mLaneViews.Num(), FLIGHT_MAX_LANES) : 0;
        FSOLLaneMergeLane laneViews[FLIGHT_MAX_LANES];
        for (int32 lane = 0; lane < laneCount; ++lane)
        {
            laneViews[lane] = mLaneViews[lane];
        }
        const TConstArrayView<FSOLLaneMergeLane> lanes(laneViews, laneCount);
        bool bLaneStep = false;

        // AUTO lane route plan (SDD 53 6.4, part 53e; planned by the ship subsystem): while a plan exists, its current
        // hop's lane is the only one the search may take (the D20 straight search is off, Q15) and, before the last
        // hop, the hop's end node is the lane rules' target
        FSOLLaneRoutePlan lanePlan = stateFragment.LanePlan;
        bool bLaneLeftForSun = (bAutopilot || bChase) && stateFragment.bLaneLeftForSun;

        // Masks the lane views to the current hop's lane (every column of its two bodies, rebuilt where the bodies
        // are); a hop left early searches none until its end node. A hop whose lane is not in this frame's table drops
        // the plan (AUTO flies as today). Without a plan the views stay as copied
        auto maskLanesToHop = [&](const TConstArrayView<FVector3d> bodiesM)
        {
            if (!bLaneRules || !SOLLaneRoutePlan::HasPlan(lanePlan))
            {
                return;
            }
            const int32 hopEntry = mFrame.PlanHopEntries[lanePlan.CurrentHop];
            if (!lanes.IsValidIndex(hopEntry))
            {
                SOLLaneRoutePlan::ClearPlan(lanePlan);
                return;
            }
            const FSOLLaneTrafficEntry& hop = mFrame.Lanes[hopEntry];
            for (int32 lane = 0; lane < laneCount; ++lane)
            {
                const FSOLLaneTrafficEntry& entry = mFrame.Lanes[lane];
                const bool bHopLane = !lanePlan.bHopLeftEarly && ((entry.BodyA == hop.BodyA
                    && entry.BodyB == hop.BodyB) || (entry.BodyA == hop.BodyB && entry.BodyB == hop.BodyA));
                if (bHopLane)
                {
                    laneViews[lane] = FlightLaneView(entry, bodiesM, bodyVelocitiesMps);
                }
                else
                {
                    laneViews[lane].bOpenNow = false;
                }
            }
        };

        // A held lane that closes before the hop ends and whose rest is not Sun-safe (the subsystem's finish-if-safe
        // decision, SDD 53 6.5) is left at once and for good
        if (bLaneRules && laneMode == ESOLLaneAutoMode::Hold && mFrame.bHeldLaneLeave
            && laneLeg.Lane == mFrame.HeldLaneEntry)
        {
            SOLLaneRoutePlan::ForceLeaveLane(lanePlan, laneMode, laneLeg);
            bLaneLeftForSun = true;
        }
        maskLanesToHop(TConstArrayView<FVector3d>(substepBodies, bodyCount));
        bool bHopSearch = false;        // The hop changed (or its leg went stale) mid-frame: search at the next substep

        // The lane rules' target body: the current hop's end node while a later hop remains, else none (the order's)
        auto hopTargetBody = [&]()
        {
            const int32 body = (bLaneRules && SOLLaneRoutePlan::ViaNode(lanePlan) != INDEX_NONE)
                ? mFrame.PlanHopEndBodies[lanePlan.CurrentHop] : INDEX_NONE;
            return (body >= 0 && body < bodyCount) ? body : INDEX_NONE;
        };

        // The lane rules' and the Sun safety's target (Unreal axes): the hop's end node, else the order's own target
        auto legTargetM = [&](const TConstArrayView<FVector3d> bodiesM)
        {
            const int32 body = hopTargetBody();
            return body != INDEX_NONE ? bodiesM[body] : orderTargetM(bodiesM);
        };

        // The direct flight's aim (Unreal axes): toward a plan hop's end node (between hops, or a hop left early) the
        // standoff point above that body on the ship's side, else the order's own target
        auto directAimM = [&](const TConstArrayView<FVector3d> bodiesM, const FVector3d& shipM)
        {
            const int32 body = hopTargetBody();
            if (body == INDEX_NONE)
            {
                return orderTargetM(bodiesM);
            }
            const FVector3d awayM = (shipM - bodiesM[body]).GetSafeNormal(UE_SMALL_NUMBER, FVector3d(0.0, 0.0, 1.0));
            return bodiesM[body] + awayM * SOLFlight::AutopilotStandoffM(bodyRadiiM[body], bodyGMs[body],
                SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter));
        };

        // Sun safety (SDD 53 6.5, part 53e): every AUTO order keeps out of the danger zone unless the player overrode
        // it; the latch carries over frames, reset with each order
        bool bSunDetour = (bAutopilot || bChase) && mFrame.bSunSafety && stateFragment.bSunDetour;
        bool bSunStopped = false;
        bool bSunAimLast = false;       // The last substep flew the Sun aim (the HUD readout is then rewritten)
        double sunMinM = std::numeric_limits<double>::infinity();
        const double sunZoneM = SOL::NPC_LANE_SUN_CLEARANCE_RADII * SOL::SUN_RADIUS_M;
        const double sunEdgeM = SOL::NPC_LANE_SUN_REOPEN_RADII * SOL::SUN_RADIUS_M;
        const double sunMinArcRad = FMath::DegreesToRadians(SOL::AUTO_SUN_DETOUR_MIN_ARC_DEG);

        for (int32 substep = 0; substep < substepCount; ++substep)
        {
            const TConstArrayView<FVector3d> bodiesBeforeM(substepBodies + substep * bodyCount, bodyCount);
            const TConstArrayView<FVector3d> bodiesAfterM(substepBodies + (substep + 1) * bodyCount, bodyCount);
            const FVector3d gravityMps2 = SOLFlight::GravityAcceleration(state.PositionM, bodiesBeforeM, bodyGMs,
                bodyRadiiM);
            const FSOLShipState before = state;
            FSOLFlightStepOutput stepOutput;

            // The normal autopilot's brake plan: the lane leave distance and the Sun safety's braking path
            const FSOLGuidanceParams brakePlan = SOLNewtonian::MakeGuidanceParams(flightParams.Envelope,
                SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter), gravityMps2.Size(), true,
                false);

            // The Sun safety's braking path brakes as the order does: boosted for War power and a boosted chase
            const bool bBoostBrake = (bAutopilot && controlFragment.bAutopilotIgnoreLanes)
                || (bChase && control.bBoost);
            const double sunBrakeAccelMps2 = bBoostBrake ? SOLNewtonian::MakeGuidanceParams(flightParams.Envelope,
                SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter), gravityMps2.Size(), true,
                true).BrakeAccelMps2 : brakePlan.BrakeAccelMps2;

            // Lane rules: the decision of this substep in ecliptic axes, suspended while the Sun aim steers (latched
            // earlier) and while a hop left early flies on to its end node. A search runs on the first substep (the
            // views, ship and target all at the frame start) or after a hop change this frame (its views rebuilt here)
            FSOLLaneAutoDecision decision;
            const bool bEarlyHop = SOLLaneRoutePlan::HasPlan(lanePlan) && lanePlan.bHopLeftEarly;
            if (bLaneRules && laneMode != ESOLLaneAutoMode::Left && !bSunDetour && !bEarlyHop)
            {
                if (bHopSearch)
                {
                    maskLanesToHop(bodiesBeforeM);
                }
                else if (substep > 0 && laneLeg.bFound && lanes.IsValidIndex(laneLeg.Lane)
                    && mFrame.Lanes.IsValidIndex(laneLeg.Lane))
                {
                    laneViews[laneLeg.Lane] = FlightLaneView(mFrame.Lanes[laneLeg.Lane], bodiesBeforeM,
                        bodyVelocitiesMps);
                }
                FSOLLaneAutoInput input;
                input.ShipM = SOLRender::EclipticToUnreal(state.PositionM);
                input.ShipVelocityMps = SOLRender::EclipticToUnreal(state.VelocityMps);
                const int32 hopBody = hopTargetBody();
                input.TargetM = SOLRender::EclipticToUnreal(legTargetM(bodiesBeforeM));
                input.TargetVelocityMps = SOLRender::EclipticToUnreal(hopBody != INDEX_NONE
                    ? bodyVelocitiesMps[hopBody] : orderTargetVelocityMps);
                input.BrakeAccelMps2 = brakePlan.BrakeAccelMps2;
                input.FlipS = brakePlan.FlipTimeS;
                input.bSearch = substep == 0 || bHopSearch;
                bHopSearch = false;
                const bool bPlanSearch = input.bSearch && laneMode == ESOLLaneAutoMode::None
                    && SOLLaneRoutePlan::HasPlan(lanePlan);
                const ESOLLaneAutoMode modeBefore = laneMode;
                decision = SOLLaneTraffic::LaneAutoDecide(lanes, laneLeg, laneMode, input);
                laneLeg = decision.Leg;
                laneMode = decision.Mode;
                if (SOLLaneRoutePlan::HasPlan(lanePlan) && laneMode == ESOLLaneAutoMode::Left)
                {
                    // Per-hop Left: the next hop's search runs at the next substep from here; Left after the last. A
                    // hop that never held its lane and is short of its end node is left early instead: AUTO flies on
                    // to that node and the plan is re-made there
                    const bool bAtHopNode = hopBody != INDEX_NONE && FVector3d::Dist(state.PositionM,
                        bodiesBeforeM[hopBody]) <= SOL::AUTO_LANE_PLAN_AT_NODE_RADII * bodyRadiiM[hopBody];
                    bHopSearch = SOLLaneRoutePlan::LeaveHopOnLaneRulesLeft(lanePlan, modeBefore, bAtHopNode, laneMode,
                        laneLeg);
                }
                else if (bPlanSearch && laneMode == ESOLLaneAutoMode::None)
                {
                    // The hop's lane gave no leg from here: the plan is dropped and AUTO flies as today
                    SOLLaneRoutePlan::ClearPlan(lanePlan);
                }
                else if (SOLLaneRoutePlan::HasPlan(lanePlan) && laneMode == ESOLLaneAutoMode::None
                    && modeBefore != ESOLLaneAutoMode::None)
                {
                    // The hop's leg went stale mid-frame: searched again (on its own lane only) at the next substep
                    bHopSearch = true;
                }
                if (laneMode == ESOLLaneAutoMode::Approach || laneMode == ESOLLaneAutoMode::Hold)
                {
                    bLaneLeftForSun = false;    // A lane latched again: the lane-closed notice has done its job
                }
            }

            // The Approach step target: the decision's (the merge point co-moving with the traffic at the entry
            // radius, else the order target). A ship already past the entry arc merges abreast of itself (the point
            // moves on with it, so the rendezvous matches the traffic speed instead of turning back)
            FSOLLaneStepTarget stepTarget = decision.StepTarget;
            if (laneMode == ESOLLaneAutoMode::Approach && lanes.IsValidIndex(laneLeg.Lane))
            {
                const FSOLLaneMergeLane& view = lanes[laneLeg.Lane];
                const double shipArcM = SOLLaneTraffic::LanePointOf(view.Profile, view.Frame,
                    SOLRender::EclipticToUnreal(state.PositionM)).LegArcM;
                if ((shipArcM - laneLeg.EntryArcM) * static_cast<double>(laneLeg.DirectionSign) > 0.0)
                {
                    FSOLLaneAutoLeg mergeLeg = laneLeg;
                    mergeLeg.EntryArcM = FMath::Clamp(shipArcM, FMath::Min(laneLeg.EntryArcM, laneLeg.ExitArcM),
                        FMath::Max(laneLeg.EntryArcM, laneLeg.ExitArcM));
                    stepTarget = SOLLaneTraffic::LaneApproachStepTarget(SOLLaneTraffic::LaneMergeTarget(view,
                        mergeLeg), stepTarget.PositionM, stepTarget.VelocityMps, stepTarget.AccelMps2);
                }
            }

            // Chase Down (SDD 32): the target where it is at this substep's start, its acceleration (its frame body's
            // gravity from the others, plus its own), then the tail point the chase step flies to
            const bool bHoldStep = laneMode == ESOLLaneAutoMode::Hold && lanes.IsValidIndex(laneLeg.Lane);
            FVector3d chaseAimM = FVector3d::ZeroVector;
            FVector3d chaseTargetAccelMps2 = FVector3d::ZeroVector;
            if (bChase && !bHoldStep && laneMode != ESOLLaneAutoMode::Approach)
            {
                const double remainingS = (substepCount - substep) * boundaryRealS;
                FVector3d chaseTargetM = SOLFlight::BodyPositionAtSubstep(chaseOffsetEndM, chaseOffsetVelocityMps,
                    warpFactor, remainingS);
                const FSOLShipChaseFragment& chase = *chaseFragment;
                chaseTargetAccelMps2 = chase.ChaseTargetAccelMps2;
                if (chaseFrameBody != INDEX_NONE)
                {
                    const FVector3d& frameBodyM = bodiesBeforeM[chaseFrameBody];
                    chaseTargetM += frameBodyM;
                    chaseTargetAccelMps2 += SOLFlight::GravityAcceleration(frameBodyM, bodiesBeforeM, bodyGMs,
                        bodyRadiiM);
                }
                chaseAimM = SOLChase::ChaseAimPointM(chaseTargetM, chaseRelVelocityMps, chase.ChaseApproachDir,
                    chase.ChaseTrailM);
            }

            // Sun safety (the Sun is the universe origin) on exactly the aim the branch below flies: the lane track's
            // end in a Hold, the merge point in an Approach, the chase's tail point, else the direct aim. It gives a
            // detour waypoint on the edge sphere, the zone exit, or the arrival moved to the edge for a target inside
            // the zone; the ship's braking path and swept substep entering the zone latch it too. Without either, the
            // step below flies exactly as before (paths outside the zone stay bit-identical)
            bool bSunAim = false;
            FVector3d sunAimM = FVector3d::ZeroVector;
            if ((bAutopilot || bChase) && mFrame.bSunSafety)
            {
                const bool bApproachStep = laneMode == ESOLLaneAutoMode::Approach;
                const FVector3d flownAimM = bHoldStep ? legTargetM(bodiesBeforeM)
                    : bApproachStep ? SOLRender::EclipticToUnreal(stepTarget.PositionM)
                    : bChase ? chaseAimM : directAimM(bodiesBeforeM, state.PositionM);
                const FSOLSunSafeAim aim = SOLLaneRoutePlan::SunSafeAim(state.PositionM, state.VelocityMps, flownAimM,
                    true, bSunDetour, sunZoneM, sunEdgeM, sunMinArcRad, sunBrakeAccelMps2, substepS);
                bSunDetour = aim.bDetour;
                bSunStopped = aim.bStoppedAtZone && !bHoldStep && !bApproachStep && hopTargetBody() == INDEX_NONE;
                bSunAim = aim.bDetour || aim.bStoppedAtZone;
                sunAimM = aim.AimM;
                if (bSunAim && (bApproachStep || laneMode == ESOLLaneAutoMode::Hold))
                {
                    // The Sun aim takes over a latched lane: the lane is left for good, so its rules never resume on
                    // it (no chatter across the zone boundary); with a later hop AUTO flies on to the hop's end node
                    SOLLaneRoutePlan::ForceLeaveLane(lanePlan, laneMode, laneLeg);
                }
            }
            bSunAimLast = bSunAim;
            bLaneStep = !bSunAim && (laneMode == ESOLLaneAutoMode::Approach || laneMode == ESOLLaneAutoMode::Hold);

            if (bSunAim)
            {
                // Sun safety: rendezvous with the aim at rest relative to the Sun (a detour waypoint stays at least
                // the minimum arc ahead, so the ship never reaches it; a zone-edge arrival is held against gravity)
                state = SOLFlight::AutopilotStep(state, sunAimM, FVector3d::ZeroVector, FVector3d::ZeroVector,
                    gravityMps2, flightParams, meter, substepS, &stepOutput, &guidance, gravityMps2.Size());
                bPointArrived |= !bSunDetour && guidance.Phase == ESOLGuidancePhase::Arrived;
            }
            else if (bHoldStep)
            {
                // Hold: the traffic speed on the keep-right track at the autopilot limit (a chase too, no bursts); the
                // gain at most 1 / dt, as the rendezvous guidance's tracker, so long warp substeps stay deadbeat
                const double gainPerS = FMath::Min(FSOLGuidanceParams().VelocityGainPerS, 1.0 / substepS);
                guidance = FlightMirrorGuidance(SOLLaneTraffic::LaneHoldGuidance(lanes[laneLeg.Lane], laneLeg,
                    SOLRender::EclipticToUnreal(state.PositionM), SOLRender::EclipticToUnreal(state.VelocityMps),
                    SOLRender::EclipticToUnreal(legTargetM(bodiesBeforeM)), SOLRender::EclipticToUnreal(gravityMps2),
                    gainPerS));
                state = SOLFlight::LaneHoldStep(state, guidance, gravityMps2, flightParams, meter, substepS,
                    &stepOutput);
            }
            else if (laneMode == ESOLLaneAutoMode::Approach)
            {
                // Approach: rendezvous with the step target worked out above
                state = SOLFlight::AutopilotStep(state, SOLRender::EclipticToUnreal(stepTarget.PositionM),
                    SOLRender::EclipticToUnreal(stepTarget.VelocityMps), SOLRender::EclipticToUnreal(
                    stepTarget.AccelMps2), gravityMps2, flightParams, meter, substepS, &stepOutput, &guidance);
            }
            else if (bChase)
            {
                // Chase Down (SDD 32): the G-policy step to the tail point worked out above. The budgets subtract the
                // stronger of the gravity here and at the tail point
                const FSOLShipChaseFragment& chase = *chaseFragment;
                state = SOLFlight::ChaseStep(state, chaseAimM, chase.ChaseTargetVelocityMps, chaseTargetAccelMps2,
                    gravityMps2,
                    chase.ChasePlanGravityMps2, flightParams, meter, chase.ChasePolicy, control.bBoost, substepS,
                    chaseRun, &stepOutput, &guidance, &chaseLimit);
            }
            else if (bAutopilot && controlFragment.bAutopilotPoint && bPointArrived
                && controlFragment.AutopilotCaptureRadiusM > 0.0)
            {
                // A fly-through captured earlier this frame: coast through the rest of it (homing on the passed point
                // would turn the ship back); the route orders the next waypoint after the step
                state = SOLNewtonian::StepNewtonian(state, FSOLNewtonianCommand(), flightParams.Envelope, gravityMps2,
                    substepS);
            }
            else if (bAutopilot && controlFragment.bAutopilotPoint)
            {
                // Route waypoint (SDD 20): a fixed offset from the body, flown through at the ordered pass speed or
                // stopped at; an arrival in any substep counts for the frame
                const FVector3d& bodyM = bodiesBeforeM[autopilotBody];
                const double offsetM = FMath::Max(controlFragment.AutopilotPointOffsetM.Size(),
                    bodyRadiiM[autopilotBody]);
                const FVector3d bodyAccelMps2 = SOLFlight::GravityAcceleration(bodyM, bodiesBeforeM, bodyGMs,
                    bodyRadiiM);
                state = SOLFlight::AutopilotStep(state, bodyM + controlFragment.AutopilotPointOffsetM,
                    bodyVelocitiesMps[autopilotBody], bodyAccelMps2, gravityMps2, flightParams, meter, substepS,
                    &stepOutput, &guidance, bodyGMs[autopilotBody] / FMath::Max(offsetM * offsetM, 1.0),
                    controlFragment.AutopilotPassSpeedMps, controlFragment.AutopilotCaptureRadiusM);
                bPointArrived |= guidance.Phase == ESOLGuidancePhase::Arrived;
            }
            else if (bAutopilot)
            {
                // Toward a point above the body on the ordered side (far enough out that its gravity can be held,
                // SDD 18 Amendment 7); the body's own acceleration is the gravity of the others at its centre (a body
                // gets nothing from itself)
                const FVector3d& bodyM = bodiesBeforeM[autopilotBody];
                const double standoffM = SOLFlight::AutopilotStandoffM(bodyRadiiM[autopilotBody],
                    bodyGMs[autopilotBody], SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter));
                const FVector3d bodyAccelMps2 = SOLFlight::GravityAcceleration(bodyM, bodiesBeforeM, bodyGMs,
                    bodyRadiiM);
                if (controlFragment.bAutopilotIgnoreLanes)
                {
                    // War power move (SDD 50 D22): boosted pulses of full thrust, the G meter between its band
                    state = SOLFlight::WarPowerPulseAutopilotStep(state, bodyM + controlFragment.AutopilotOffsetDir
                        * standoffM, bodyVelocitiesMps[autopilotBody], bodyAccelMps2, gravityMps2, flightParams,
                        meter, true, substepS, warPowerPulse, &stepOutput, &guidance,
                        bodyGMs[autopilotBody] / (standoffM * standoffM));
                }
                else if (hopTargetBody() != INDEX_NONE)
                {
                    // Between plan hops (or a hop left early): the standoff above the hop's end node, the same aim the
                    // Sun safety checked
                    const int32 hopBody = hopTargetBody();
                    const double hopStandoffM = SOLFlight::AutopilotStandoffM(bodyRadiiM[hopBody], bodyGMs[hopBody],
                        SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter));
                    state = SOLFlight::AutopilotStep(state, directAimM(bodiesBeforeM, state.PositionM),
                        bodyVelocitiesMps[hopBody], SOLFlight::GravityAcceleration(bodiesBeforeM[hopBody],
                        bodiesBeforeM, bodyGMs, bodyRadiiM), gravityMps2, flightParams, meter, substepS, &stepOutput,
                        &guidance, bodyGMs[hopBody] / (hopStandoffM * hopStandoffM));
                }
                else
                {
                    state = SOLFlight::AutopilotStep(state, bodyM + controlFragment.AutopilotOffsetDir * standoffM,
                        bodyVelocitiesMps[autopilotBody], bodyAccelMps2, gravityMps2, flightParams, meter, substepS,
                        &stepOutput, &guidance, bodyGMs[autopilotBody] / (standoffM * standoffM));
                }
            }
            else if (!SOLNewtonian::PilotHasControl(meter))
            {
                // G-LOC: no input reaches the ship, it coasts
                state = SOLNewtonian::StepNewtonian(state, FSOLNewtonianCommand(), flightParams.Envelope, gravityMps2,
                    substepS);
            }
            else
            {
                // Surface-lock (SDD 4) is an attitude target in assisted mode only
                control.AttitudeUpWorld = control.bFlightAssist && alignBody >= 0 && alignBody < bodyCount
                    ? SOLSurfaceLock::TargetUpDir(state.PositionM, bodiesBeforeM[alignBody]) : FVector3d::ZeroVector;
                state = SOLFlight::Step(state, control, flightParams, gravityMps2, substepS, &stepOutput);
            }
            if (!bSimTime)
            {
                state.PositionM += mCarryPerSubstepM;
            }

            // Swept collision against every body the ship could reach this substep (the Sun included): a body is
            // skipped when the ship's start distance exceeds the contact radius plus the relative travel
            int32 substepContact = INDEX_NONE;
            for (int32 body = 0; body < bodyCount; ++body)
            {
                const FVector3d startRel = before.PositionM - bodiesBeforeM[body];
                const FVector3d endRel = state.PositionM - bodiesAfterM[body];
                const double reachM = bodyRadiiM[body] + flightParams.ShipRadiusM + FVector3d::Dist(startRel, endRel);
                if (startRel.SizeSquared() > reachM * reachM)
                {
                    continue;
                }
                if (SOLFlight::ResolveSweptSphereCollision(before, state, bodiesBeforeM[body], bodiesAfterM[body],
                    bodyVelocitiesMps[body], bodyRadiiM[body], flightParams.ShipRadiusM))
                {
                    substepContact = body;
                }
            }

            // An AUTO order's closest approach to the Sun over this substep's segment, and a hop left early ending at
            // its end node (within the plan's at-node radius, or arrived at its standoff): the next hop is searched
            if (bAutopilot || bChase)
            {
                sunMinM = FMath::Min(sunMinM, SOLLaneNetwork::SunSegmentDistanceM(before.PositionM, state.PositionM));
                const int32 earlyBody = bEarlyHop ? hopTargetBody() : INDEX_NONE;
                const bool bAtHopNode = earlyBody != INDEX_NONE && (FVector3d::Dist(state.PositionM,
                    bodiesAfterM[earlyBody]) <= SOL::AUTO_LANE_PLAN_AT_NODE_RADII * bodyRadiiM[earlyBody]
                    || (!bSunAim && guidance.Phase == ESOLGuidancePhase::Arrived));
                bHopSearch |= SOLLaneRoutePlan::FinishEarlyHop(lanePlan, bAtHopNode);
            }

            // Pilot G-load: the achieved thrust plus, while resting on a surface, its contact push (gravity itself
            // is never felt); then the G meter over this substep
            FVector3d properAccelMps2 = stepOutput.ThrustAccelWorldMps2;
            if (substepContact != INDEX_NONE)
            {
                contactBody = substepContact;
                const FVector3d towardBody = bodiesAfterM[substepContact] - state.PositionM;
                const double distanceM = FMath::Max(towardBody.Size(), bodyRadiiM[substepContact]);
                const FVector3d bodyGravityMps2 = towardBody.GetSafeNormal()
                    * (bodyGMs[substepContact] / (distanceM * distanceM));
                properAccelMps2 += SOLFlight::ContactAccelMps2(-towardBody, bodyGravityMps2,
                    stepOutput.ThrustAccelWorldMps2);
            }
            gLoad = SOLNewtonian::GLoadG(properAccelMps2);
            meter = SOLNewtonian::StepGMeter(meter, gLoad, flightParams.PilotLimits, substepS);

            // The achieved thrust alone (no contact push) in the attitude the step thrust with
            thrustMean.Add(SOLThruster::ShipThrustLocalMps2(before.Orientation, stepOutput.ThrustAccelWorldMps2),
                substepS);
        }

        // In a lane step every AUTO kind's HUD ETA counts the lane at the traffic speed (approach, lane, leave with its
        // braking), and its distance and closing speed are to the order's own target, not the merge point
        if (bLaneStep && laneLeg.bFound && mFrame.Lanes.IsValidIndex(laneLeg.Lane))
        {
            const TConstArrayView<FVector3d> endBodiesM(substepBodies + substepCount * bodyCount, bodyCount);
            const FVector3d targetM = orderTargetM(endBodiesM);
            const FSOLGuidanceParams plan = SOLNewtonian::MakeGuidanceParams(flightParams.Envelope,
                SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter), 0.0, true, false);
            const double laneEtaS = SOLLaneTraffic::LaneAutoEtaS(FlightLaneView(mFrame.Lanes[laneLeg.Lane], endBodiesM,
                bodyVelocitiesMps), laneLeg, SOLRender::EclipticToUnreal(state.PositionM),
                SOLRender::EclipticToUnreal(legTargetM(endBodiesM)), plan.PlanAccelMps2, plan.FlipTimeS);
            // With a plan the later hops' edge and transfer times follow the current hop (Q13)
            guidance.EtaS = SOLLaneRoutePlan::PlanEtaS(lanePlan, SOLLaneTraffic::LaneOrderEtaS(laneMode, laneEtaS,
                guidance.EtaS), 0.0);
            guidance.DistanceM = (targetM - state.PositionM).Size();
            guidance.ClosingSpeedMps = SOLLaneTraffic::LaneClosingSpeedMps(state.PositionM, state.VelocityMps, targetM,
                orderTargetVelocityMps);
        }

        // After a Sun step the HUD's distance and closing speed are to the order's own target, not the detour
        // waypoint, and its ETA is the direct estimate to that target (the detour time is not counted, SDD 53 6.5)
        // plus the later plan hops
        if (bSunAimLast && bAutopilot && !bChase)
        {
            const TConstArrayView<FVector3d> endBodiesM(substepBodies + substepCount * bodyCount, bodyCount);
            const FVector3d targetM = orderTargetM(endBodiesM);
            const FSOLGuidanceParams plan = SOLNewtonian::MakeGuidanceParams(flightParams.Envelope,
                SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter), 0.0, true, false);
            guidance.DistanceM = (targetM - state.PositionM).Size();
            guidance.ClosingSpeedMps = SOLLaneTraffic::LaneClosingSpeedMps(state.PositionM, state.VelocityMps, targetM,
                orderTargetVelocityMps);
            guidance.EtaS = SOLLaneRoutePlan::PlanEtaS(lanePlan, SOLNewtonian::EstimateTimeToArrivalS(
                guidance.DistanceM, guidance.ClosingSpeedMps, plan.PlanAccelMps2, plan.BrakeAccelMps2, plan.FlipTimeS,
                FMath::Max(guidance.SpeedLimitMps, 1.0)), 0.0);
        }

        stateFragment.State = state;
        stateFragment.ContactBodyIndex = contactBody;
        stateFragment.GMeter = meter;
        stateFragment.GLoadG = gLoad;
        stateFragment.Guidance = guidance;
        stateFragment.LaneLeg = laneLeg;
        stateFragment.LaneMode = laneMode;
        stateFragment.WarPowerPulse = warPowerPulse;
        stateFragment.LanePlan = lanePlan;
        stateFragment.bSunDetour = bSunDetour;
        stateFragment.bSunStoppedAtZone = bSunStopped;
        stateFragment.bLaneLeftForSun = bLaneLeftForSun;
        stateFragment.SunMinDistanceM = (bAutopilot || bChase) ? sunMinM : 0.0;
        if (substepS > 0.0)
        {
            stateFragment.ThrustLocalG = thrustMean.MeanG();    // A zero-length step keeps the last plume
        }
        if (chaseFragment != nullptr)
        {
            chaseFragment->ChaseRun = chaseRun;
            chaseFragment->ChaseLimit = chaseLimit;
        }
        // A lane approach "arriving" at its merge point, a detour step or an arrival at a plan hop's node is no arrival
        // at the order's target
        stateFragment.bAutopilotArrived = SOLLaneRoutePlan::OrderArrived(bAutopilot, bChase, bLaneStep, bSunDetour,
            hopTargetBody() != INDEX_NONE, bPointArrived || guidance.Phase == ESOLGuidancePhase::Arrived);
    }
}
