/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcMidProcessor.h"

#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcFragments.h"

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
    mQuery.AddConstSharedRequirement<FSOLNpcClassFragment>();
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
    const FSOLGuidanceParams params = SOLNpcAi::MidReturnGuidanceParams(
        SOLNpcAi::GetClass(context.GetConstSharedFragment<FSOLNpcClassFragment>().ClassIndex));

    const int32 entityCount = context.GetNumEntities();
    for (int32 entity = 0; entity < entityCount; ++entity)
    {
        // A ship at rest on its rail stays there
        FSOLNpcDeviationFragment& deviation = deviations[entity];
        if (deviation.DeviationM.IsZero() && deviation.DeviationVelocityMps.IsZero())
        {
            continue;
        }
        const SOLNpcAi::FMidReturnStep step = SOLNpcAi::StepMidReturn(deviation.DeviationM,
            deviation.DeviationVelocityMps, params, mRealDeltaSeconds);
        states[entity].PositionM += step.DeviationM - deviation.DeviationM;
        deviation.DeviationM = step.DeviationM;
        deviation.DeviationVelocityMps = step.DeviationVelocityMps;
    }
}
