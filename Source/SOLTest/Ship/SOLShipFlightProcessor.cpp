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

        for (int32 substep = 0; substep < substepCount; ++substep)
        {
            const TConstArrayView<FVector3d> bodiesBeforeM(substepBodies + substep * bodyCount, bodyCount);
            const TConstArrayView<FVector3d> bodiesAfterM(substepBodies + (substep + 1) * bodyCount, bodyCount);
            const FVector3d gravityMps2 = SOLFlight::GravityAcceleration(state.PositionM, bodiesBeforeM, bodyGMs,
                bodyRadiiM);
            const FSOLShipState before = state;
            FSOLFlightStepOutput stepOutput;

            // Lane rules: the decision of this substep (a search only on the first, where the views, ship and target
            // are all at the frame start), in ecliptic axes, with the normal autopilot's brake plan for the leave
            FSOLLaneAutoDecision decision;
            if (bLaneRules && laneMode != ESOLLaneAutoMode::Left)
            {
                if (substep > 0 && laneLeg.bFound && lanes.IsValidIndex(laneLeg.Lane)
                    && mFrame.Lanes.IsValidIndex(laneLeg.Lane))
                {
                    laneViews[laneLeg.Lane] = FlightLaneView(mFrame.Lanes[laneLeg.Lane], bodiesBeforeM,
                        bodyVelocitiesMps);
                }
                const FSOLGuidanceParams brakePlan = SOLNewtonian::MakeGuidanceParams(flightParams.Envelope,
                    SOLNewtonian::AutopilotAccelLimitMps2(flightParams.PilotLimits, meter), gravityMps2.Size(), true,
                    false);
                FSOLLaneAutoInput input;
                input.ShipM = SOLRender::EclipticToUnreal(state.PositionM);
                input.ShipVelocityMps = SOLRender::EclipticToUnreal(state.VelocityMps);
                input.TargetM = SOLRender::EclipticToUnreal(orderTargetM(bodiesBeforeM));
                input.TargetVelocityMps = SOLRender::EclipticToUnreal(orderTargetVelocityMps);
                input.BrakeAccelMps2 = brakePlan.BrakeAccelMps2;
                input.FlipS = brakePlan.FlipTimeS;
                input.bSearch = substep == 0;
                decision = SOLLaneTraffic::LaneAutoDecide(lanes, laneLeg, laneMode, input);
                laneLeg = decision.Leg;
                laneMode = decision.Mode;
            }
            bLaneStep = laneMode == ESOLLaneAutoMode::Approach || laneMode == ESOLLaneAutoMode::Hold;

            if (laneMode == ESOLLaneAutoMode::Hold && lanes.IsValidIndex(laneLeg.Lane))
            {
                // Hold: the traffic speed on the keep-right track at the autopilot limit (a chase too, no bursts); the
                // gain at most 1 / dt, as the rendezvous guidance's tracker, so long warp substeps stay deadbeat
                const double gainPerS = FMath::Min(FSOLGuidanceParams().VelocityGainPerS, 1.0 / substepS);
                guidance = FlightMirrorGuidance(SOLLaneTraffic::LaneHoldGuidance(lanes[laneLeg.Lane], laneLeg,
                    SOLRender::EclipticToUnreal(state.PositionM), SOLRender::EclipticToUnreal(state.VelocityMps),
                    SOLRender::EclipticToUnreal(orderTargetM(bodiesBeforeM)), SOLRender::EclipticToUnreal(gravityMps2),
                    gainPerS));
                state = SOLFlight::LaneHoldStep(state, guidance, gravityMps2, flightParams, meter, substepS,
                    &stepOutput);
            }
            else if (laneMode == ESOLLaneAutoMode::Approach)
            {
                // Approach: rendezvous with the decision's step target (the merge point, co-moving with the traffic at
                // the entry radius, else the order target). A ship already past the entry arc merges abreast of itself
                // (the point moves on with it, so the rendezvous matches the traffic speed instead of turning back)
                FSOLLaneStepTarget stepTarget = decision.StepTarget;
                if (lanes.IsValidIndex(laneLeg.Lane))
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
                state = SOLFlight::AutopilotStep(state, SOLRender::EclipticToUnreal(stepTarget.PositionM),
                    SOLRender::EclipticToUnreal(stepTarget.VelocityMps), SOLRender::EclipticToUnreal(
                    stepTarget.AccelMps2), gravityMps2, flightParams, meter, substepS, &stepOutput, &guidance);
            }
            else if (bChase)
            {
                // Chase Down (SDD 32): the target where it is at this substep's start, its acceleration (its frame
                // body's gravity from the others, plus its own), the tail point, then the G-policy step. The budgets
                // subtract the stronger of the gravity here and at the tail point
                const double remainingS = (substepCount - substep) * boundaryRealS;
                FVector3d targetM = SOLFlight::BodyPositionAtSubstep(chaseOffsetEndM, chaseOffsetVelocityMps,
                    warpFactor, remainingS);
                const FSOLShipChaseFragment& chase = *chaseFragment;
                FVector3d targetAccelMps2 = chase.ChaseTargetAccelMps2;
                if (chaseFrameBody != INDEX_NONE)
                {
                    const FVector3d& frameBodyM = bodiesBeforeM[chaseFrameBody];
                    targetM += frameBodyM;
                    targetAccelMps2 += SOLFlight::GravityAcceleration(frameBodyM, bodiesBeforeM, bodyGMs, bodyRadiiM);
                }
                const FVector3d aimM = SOLChase::ChaseAimPointM(targetM, chaseRelVelocityMps, chase.ChaseApproachDir,
                    chase.ChaseTrailM);
                state = SOLFlight::ChaseStep(state, aimM, chase.ChaseTargetVelocityMps, targetAccelMps2, gravityMps2,
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
                SOLRender::EclipticToUnreal(targetM), plan.PlanAccelMps2, plan.FlipTimeS);
            guidance.EtaS = SOLLaneTraffic::LaneOrderEtaS(laneMode, laneEtaS, guidance.EtaS);
            guidance.DistanceM = (targetM - state.PositionM).Size();
            guidance.ClosingSpeedMps = SOLLaneTraffic::LaneClosingSpeedMps(state.PositionM, state.VelocityMps, targetM,
                orderTargetVelocityMps);
        }

        stateFragment.State = state;
        stateFragment.ContactBodyIndex = contactBody;
        stateFragment.GMeter = meter;
        stateFragment.GLoadG = gLoad;
        stateFragment.Guidance = guidance;
        stateFragment.LaneLeg = laneLeg;
        stateFragment.LaneMode = laneMode;
        stateFragment.WarPowerPulse = warPowerPulse;
        if (substepS > 0.0)
        {
            stateFragment.ThrustLocalG = thrustMean.MeanG();    // A zero-length step keeps the last plume
        }
        if (chaseFragment != nullptr)
        {
            chaseFragment->ChaseRun = chaseRun;
            chaseFragment->ChaseLimit = chaseLimit;
        }
        // A lane approach "arriving" at its merge point is no arrival at the order's target
        stateFragment.bAutopilotArrived = !bChase && bAutopilot && !bLaneStep
            && (bPointArrived || guidance.Phase == ESOLGuidancePhase::Arrived);
    }
}
