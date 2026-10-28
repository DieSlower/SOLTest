/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipFlightProcessor.h"

#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Level/SOLSurfaceLock.h"
#include "Ship/SOLShipFragments.h"
#include "SOLConstants.h"
#include "Universe/SOLBodyFrameCache.h"

#include "MassExecutionContext.h"

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

        // Real-time flight inherits its frame's velocity change under warp once, at the frame start; an autopilot
        // under warp integrates the frame's sim time itself and needs no carry
        const bool bSimTime = bAutopilot && mSimSubstepCount > 0;
        const int32 substepCount = bSimTime ? mSimSubstepCount : mSubstepCount;
        const double substepS = bSimTime ? mSimSubstepS : mSubstepS;
        const FVector3d* const substepBodies = bSimTime ? mSimSubstepBodyPositionsM.GetData()
            : mSubstepBodyPositionsM.GetData();
        if (!bSimTime)
        {
            state.VelocityMps += mFrame.CarryVelocityMps;
        }

        for (int32 substep = 0; substep < substepCount; ++substep)
        {
            const TConstArrayView<FVector3d> bodiesBeforeM(substepBodies + substep * bodyCount, bodyCount);
            const TConstArrayView<FVector3d> bodiesAfterM(substepBodies + (substep + 1) * bodyCount, bodyCount);
            const FVector3d gravityMps2 = SOLFlight::GravityAcceleration(state.PositionM, bodiesBeforeM, bodyGMs,
                bodyRadiiM);
            const FSOLShipState before = state;
            FSOLFlightStepOutput stepOutput;

            if (bAutopilot && controlFragment.bAutopilotPoint && bPointArrived
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
                state = SOLFlight::AutopilotStep(state, bodyM + controlFragment.AutopilotOffsetDir * standoffM,
                    bodyVelocitiesMps[autopilotBody], bodyAccelMps2, gravityMps2, flightParams, meter, substepS,
                    &stepOutput, &guidance, bodyGMs[autopilotBody] / (standoffM * standoffM));
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
        }

        stateFragment.State = state;
        stateFragment.ContactBodyIndex = contactBody;
        stateFragment.GMeter = meter;
        stateFragment.GLoadG = gLoad;
        stateFragment.Guidance = guidance;
        stateFragment.bAutopilotArrived = bAutopilot && (bPointArrived || guidance.Phase == ESOLGuidancePhase::Arrived);
    }
}
