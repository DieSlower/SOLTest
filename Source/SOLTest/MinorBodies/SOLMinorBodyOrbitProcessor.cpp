/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLMinorBodyOrbitProcessor.h"

#include "MinorBodies/SOLMinorBodyFragments.h"
#include "MinorBodies/SOLMinorBodyOrbit.h"

#include "MassExecutionContext.h"

namespace
{
    // The Sun's GM (m^3/s^2), matching REGISTRY_SOLAR_SYSTEM's Sun row; only the discarded velocity depends on it
    constexpr double MINOR_BODY_SUN_GM = 1.32712440018e20;
}

//////////////////////////////////////////////////////////////////////////
// Registers the query, opts out of the automatic processing phases and builds the chunk callback once
USOLMinorBodyOrbitProcessor::USOLMinorBodyOrbitProcessor()
    : mQuery(*this)
{
    bAutoRegisterWithProcessingPhases = false;
    ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);

    // The orbit step issues no Mass commands, so parallel jobs need no command buffers of their own
    mQuery.SetParallelCommandBufferEnabled(false);

    // Forwards each chunk to ProcessChunk
    mChunkFunction = [this](FMassExecutionContext& context)
    {
        ProcessChunk(context);
    };
}

//////////////////////////////////////////////////////////////////////////
// Declares the fragments the orbit step reads and writes
void USOLMinorBodyOrbitProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& /*entityManager*/)
{
    mQuery.AddRequirement<FSOLMinorBodyOrbitFragment>(EMassFragmentAccess::ReadOnly);
    mQuery.AddRequirement<FSOLMinorBodyStateFragment>(EMassFragmentAccess::ReadWrite);
}

//////////////////////////////////////////////////////////////////////////
// Places every matching chunk's bodies in parallel
void USOLMinorBodyOrbitProcessor::Execute(FMassEntityManager& /*entityManager*/, FMassExecutionContext& context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLMinorBodyOrbitProcessor::Execute);
    if (mParentPositionsM.IsEmpty())
    {
        return;
    }
    mQuery.ParallelForEachEntityChunk(context, mChunkFunction);
}

//////////////////////////////////////////////////////////////////////////
// Places the bodies of one chunk at the current time, relative to their parents
void USOLMinorBodyOrbitProcessor::ProcessChunk(FMassExecutionContext& context) const
{
    const TConstArrayView<FSOLMinorBodyOrbitFragment> orbits = context.GetFragmentView<FSOLMinorBodyOrbitFragment>();
    const TArrayView<FSOLMinorBodyStateFragment> states = context.GetMutableFragmentView<FSOLMinorBodyStateFragment>();
    const int32 parentCount = mParentPositionsM.Num();

    const int32 entityCount = context.GetNumEntities();
    for (int32 entity = 0; entity < entityCount; ++entity)
    {
        // A bad parent index leaves the body where it was rather than reading out of range; belt asteroids are always
        // parented to the validated Sun index today, but future parents (rings, 5d/5e) make this reachable, so a
        // mistake here must be loud rather than silently rendering the body at the universe origin
        const FSOLMinorBodyOrbitFragment& orbit = orbits[entity];
        if (!ensureMsgf(orbit.ParentBodyIndex >= 0 && orbit.ParentBodyIndex < parentCount,
            TEXT("MinorBodyOrbitProcessor: ParentBodyIndex %d is out of range for %d parent positions"),
            orbit.ParentBodyIndex, parentCount))
        {
            continue;
        }
        states[entity].PositionM = SOLMinorBodyOrbit::ComputePositionM(orbit.Elements,
            mParentPositionsM[orbit.ParentBodyIndex], MINOR_BODY_SUN_GM, mSecondsSinceJ2000);
    }
}
