/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcMidProcessor.h"

#include "Npc/SOLNpcFragments.h"
#include "SOLConstants.h"

#include "MassExecutionContext.h"

//////////////////////////////////////////////////////////////////////////
// Registers the query, opts out of the processing phases and builds the chunk callback once
USOLNpcMidProcessor::USOLNpcMidProcessor()
    : mQuery(*this)
{
    bAutoRegisterWithProcessingPhases = false;
    ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);
    mQuery.SetParallelCommandBufferEnabled(false);

    // Forwards each chunk to ProcessChunk
    mChunkFunction = [this](FMassExecutionContext& context)
    {
        ProcessChunk(context);
    };
}

//////////////////////////////////////////////////////////////////////////
// Declares the fragments the deviation step reads and writes
void USOLNpcMidProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& /*entityManager*/)
{
    mQuery.AddRequirement<FSOLNpcDeviationFragment>(EMassFragmentAccess::ReadWrite);
    mQuery.AddRequirement<FSOLNpcStateFragment>(EMassFragmentAccess::ReadWrite);
    mQuery.AddTagRequirement<FSOLNpcMidAiTag>(EMassFragmentPresence::All);
}

//////////////////////////////////////////////////////////////////////////
// Steps every Mid chunk in parallel (Full ships are steered by USOLNpcFullAiProcessor)
void USOLNpcMidProcessor::Execute(FMassEntityManager& /*entityManager*/, FMassExecutionContext& context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcMidProcessor::Execute);
    if (!(mRealDeltaSeconds > 0.0))
    {
        return;
    }
    mQuery.ParallelForEachEntityChunk(context, mChunkFunction);
}

//////////////////////////////////////////////////////////////////////////
// Steps the deviations of one chunk and moves the positions by the change (no Mid ship is dead: a dead Full ship
// respawns when it leaves the Full tier, USOLNpcTierProcessor)
void USOLNpcMidProcessor::ProcessChunk(FMassExecutionContext& context) const
{
    const TArrayView<FSOLNpcDeviationFragment> deviations = context.GetMutableFragmentView<FSOLNpcDeviationFragment>();
    const TArrayView<FSOLNpcStateFragment> states = context.GetMutableFragmentView<FSOLNpcStateFragment>();

    // Critically damped spring x'' = -2 w x' - w^2 x, semi-implicit Euler (stable for w * dt < 1)
    const double omega = SOL::NPC_MID_DEVIATION_OMEGA;
    const double dt = FMath::Min(mRealDeltaSeconds, 0.5 / omega);
    const double leashM = SOL::NPC_MID_DEVIATION_LEASH_M;

    const int32 entityCount = context.GetNumEntities();
    for (int32 entity = 0; entity < entityCount; ++entity)
    {
        FSOLNpcDeviationFragment& deviation = deviations[entity];
        const FVector3d oldDeviationM = deviation.DeviationM;
        const FVector3d acceleration = deviation.DeviationVelocityMps * (-2.0 * omega)
            - oldDeviationM * (omega * omega);
        deviation.DeviationVelocityMps += acceleration * dt;

        // The leash never pulls a ship in by more than the spring does (a Full ship demoted beyond it does not jump)
        deviation.DeviationM = (oldDeviationM + deviation.DeviationVelocityMps * dt).GetClampedToMaxSize(
            FMath::Max(leashM, oldDeviationM.Size()));
        states[entity].PositionM += deviation.DeviationM - oldDeviationM;
    }
}
