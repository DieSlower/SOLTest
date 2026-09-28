/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipFlightProcessor.h"

#include "Flight/SOLFlight.h"
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
        || mSubstepBodyPositionsM.Num() < (SOL::SHIP_MAX_SUBSTEPS + 1) * mBodyCache->Num())
    {
        return;
    }

    // Fixed-length substeps keep Newtonian gravity stable; the cap covers SOL::MAX_FRAME_DELTA_S, so the min is a guard
    const double budgetS = FMath::Min(deltaS, SOL::SHIP_MAX_SUBSTEP_S * SOL::SHIP_MAX_SUBSTEPS);
    mSubstepCount = FMath::Clamp(FMath::CeilToInt32(budgetS / SOL::SHIP_MAX_SUBSTEP_S), 1, SOL::SHIP_MAX_SUBSTEPS);
    mSubstepS = budgetS / mSubstepCount;
    mCarryPerSubstepM = mFrame.CarryPositionM / static_cast<double>(mSubstepCount);
    PrepareSubstepBodyPositions(budgetS);

    mQuery.ParallelForEachEntityChunk(context, mChunkFunction);
}

//////////////////////////////////////////////////////////////////////////
// Fills the scratch array with every body's position at each substep boundary of the frame
void USOLShipFlightProcessor::PrepareSubstepBodyPositions(const double frameS)
{
    const int32 bodyCount = mBodyCache->Num();
    const double warpFactor = FMath::Max(mFrame.WarpFactor, 1.0);
    const bool bHasPrevious = mFrame.PrevBodyPositionsM.Num() == bodyCount;

    // Average velocity over the frame (displacement / sim time) so boundary 0 is exactly last frame's position; the
    // instantaneous velocity is the fallback on the first frame
    for (int32 body = 0; body < bodyCount; ++body)
    {
        const FVector3d& endM = mBodyCache->PositionsM[body];
        const FVector3d velocityMps = bHasPrevious
            ? (endM - mFrame.PrevBodyPositionsM[body]) / (warpFactor * frameS) : mBodyCache->VelocitiesMps[body];
        for (int32 boundary = 0; boundary <= mSubstepCount; ++boundary)
        {
            const double remainingS = (mSubstepCount - boundary) * mSubstepS;
            mSubstepBodyPositionsM[boundary * bodyCount + body] =
                SOLFlight::BodyPositionAtSubstep(endM, velocityMps, warpFactor, remainingS);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Steps the ships of one chunk: carry, gravity, flight step and swept collision per substep
void USOLShipFlightProcessor::ProcessChunk(FMassExecutionContext& context) const
{
    const TArrayView<FSOLShipStateFragment> states = context.GetMutableFragmentView<FSOLShipStateFragment>();
    const TConstArrayView<FSOLShipControlFragment> controls = context.GetFragmentView<FSOLShipControlFragment>();
    const FSOLFlightParams& flightParams = context.GetConstSharedFragment<FSOLShipParamsFragment>().Params;

    const TConstArrayView<FVector3d> bodyVelocitiesMps = mBodyCache->VelocitiesMps;
    const TConstArrayView<double> bodyRadiiM = mBodyCache->RadiiM;
    const TConstArrayView<double> bodyGMs = mBodyCache->GMs;
    const int32 bodyCount = bodyVelocitiesMps.Num();
    const FVector3d* const substepBodies = mSubstepBodyPositionsM.GetData();

    const int32 entityCount = context.GetNumEntities();
    for (int32 entity = 0; entity < entityCount; ++entity)
    {
        const FSOLShipControl& control = controls[entity].Control;
        FSOLShipState state = states[entity].State;
        int32 contactBody = INDEX_NONE;

        // The ship inherits its frame's velocity change under warp once, at the frame start
        state.VelocityMps += mFrame.CarryVelocityMps;

        for (int32 substep = 0; substep < mSubstepCount; ++substep)
        {
            const TConstArrayView<FVector3d> bodiesBeforeM(substepBodies + substep * bodyCount, bodyCount);
            const TConstArrayView<FVector3d> bodiesAfterM(substepBodies + (substep + 1) * bodyCount, bodyCount);

            // Assist ignores gravity (the thrusters cancel it), so only Newtonian flight pays for the gravity sum
            const FVector3d gravityMps2 = control.bFlightAssist ? FVector3d::ZeroVector
                : SOLFlight::GravityAcceleration(state.PositionM, bodiesBeforeM, bodyGMs, bodyRadiiM);
            const FSOLShipState before = state;
            state = SOLFlight::Step(state, control, flightParams, gravityMps2, mSubstepS);
            state.PositionM += mCarryPerSubstepM;

            // Swept collision against every body the ship could reach this substep (the Sun included): a body is
            // skipped when the ship's start distance exceeds the contact radius plus the relative travel
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
                    contactBody = body;
                }
            }
        }

        states[entity].State = state;
        states[entity].ContactBodyIndex = contactBody;
    }
}
