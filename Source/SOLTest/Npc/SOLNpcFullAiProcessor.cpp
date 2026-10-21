/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcFullAiProcessor.h"

#include "Npc/SOLNpcCombat.h"
#include "Npc/SOLNpcFragments.h"

#include "Async/ParallelFor.h"
#include "MassExecutionContext.h"

//////////////////////////////////////////////////////////////////////////
// Registers the query and opts out of the processing phases
USOLNpcFullAiProcessor::USOLNpcFullAiProcessor()
    : mQuery(*this)
{
    bAutoRegisterWithProcessingPhases = false;
    ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);
}

//////////////////////////////////////////////////////////////////////////
// Declares the fragments both passes use
void USOLNpcFullAiProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& /*entityManager*/)
{
    mQuery.AddRequirement<FSOLNpcRailFragment>(EMassFragmentAccess::ReadOnly);
    mQuery.AddRequirement<FSOLNpcStateFragment>(EMassFragmentAccess::ReadWrite);
    mQuery.AddRequirement<FSOLNpcDeviationFragment>(EMassFragmentAccess::ReadWrite);
    mQuery.AddRequirement<FSOLNpcCombatFragment>(EMassFragmentAccess::ReadWrite);
    mQuery.AddConstSharedRequirement<FSOLNpcClassFragment>();
    mQuery.AddTagRequirement<FSOLNpcFullAiTag>(EMassFragmentPresence::All);
}

//////////////////////////////////////////////////////////////////////////
// Runs the current pass (serial chunk walk, then parallel over the chunks)
void USOLNpcFullAiProcessor::Execute(FMassEntityManager& /*entityManager*/, FMassExecutionContext& context)
{
    if (mInputs.Combat == nullptr || !(mInputs.RealDeltaS > 0.0))
    {
        return;
    }
    const bool bGather = mPass == ESOLNpcFullAiPass::Gather;
    TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(bGather ? TEXT("USOLNpcFullAiProcessor::Gather")
        : TEXT("USOLNpcFullAiProcessor::Scatter"));

    // Records every Full chunk with its first slot (chunk order is the same in both passes: no structural change
    // happens between them)
    mChunks.Reset();
    int32 total = 0;
    mQuery.ForEachEntityChunk(context, [this, &total](FMassExecutionContext& chunkContext)
    {
        FChunkView& view = mChunks.AddDefaulted_GetRef();
        view.Rails = chunkContext.GetFragmentView<FSOLNpcRailFragment>().GetData();
        view.States = chunkContext.GetMutableFragmentView<FSOLNpcStateFragment>().GetData();
        view.Deviations = chunkContext.GetMutableFragmentView<FSOLNpcDeviationFragment>().GetData();
        view.Combats = chunkContext.GetMutableFragmentView<FSOLNpcCombatFragment>().GetData();
        view.Count = chunkContext.GetNumEntities();
        view.ClassIndex = chunkContext.GetConstSharedFragment<FSOLNpcClassFragment>().ClassIndex;
        view.FirstSlot = total;
        total += view.Count;
    });
    if (bGather)
    {
        mInputs.Combat->SetShipCount(total);
    }
    ParallelFor(mChunks.Num(), [this, bGather](const int32 chunk)
    {
        if (bGather)
        {
            GatherChunk(mChunks[chunk]);
        }
        else
        {
            ScatterChunk(mChunks[chunk]);
        }
    });
}

//////////////////////////////////////////////////////////////////////////
// Writes one chunk's ships into their combat-list slots
void USOLNpcFullAiProcessor::GatherChunk(const FChunkView& view) const
{
    const double inverseDt = 1.0 / mInputs.RealDeltaS;
    for (int32 entity = 0; entity < view.Count; ++entity)
    {
        const FSOLNpcStateFragment& state = view.States[entity];
        const FSOLNpcDeviationFragment& deviation = view.Deviations[entity];
        const FSOLNpcCombatFragment& combat = view.Combats[entity];
        FSOLNpcAiShip ship;
        ship.PositionM = state.PositionM - mInputs.FrameBodyM;

        // Velocity relative to the frame body: the rail's move since last frame (this frame's position and last
        // frame's share the same deviation) plus the deviation velocity
        ship.VelocityMps = deviation.DeviationVelocityMps;
        if (state.bHasPrevPosition != 0 && mInputs.bHasFrameBodyStep)
        {
            ship.VelocityMps += (state.PositionM - state.PrevPositionM - mInputs.FrameBodyStepM) * inverseDt;
        }
        ship.DeviationM = deviation.DeviationM;
        ship.DeviationVelocityMps = deviation.DeviationVelocityMps;
        ship.NpcIndex = combat.NpcIndex;
        ship.TargetNpc = combat.TargetNpc;
        ship.ClassIndex = view.ClassIndex;
        ship.Health = combat.Health;
        ship.RespawnS = combat.RespawnS;
        ship.CooldownS = combat.CooldownS;
        ship.Faction = view.Rails[entity].Rail.Faction;
        ship.bAlive = combat.bAlive;
        mInputs.Combat->SetShip(view.FirstSlot + entity, ship);
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes one chunk's ships back from the combat list
void USOLNpcFullAiProcessor::ScatterChunk(const FChunkView& view) const
{
    for (int32 entity = 0; entity < view.Count; ++entity)
    {
        FSOLNpcCombatFragment& combat = view.Combats[entity];
        const FSOLNpcAiShip* ship = mInputs.Combat->FindShip(combat.NpcIndex);
        if (ship == nullptr)
        {
            continue;
        }

        // Position = rail plus deviation, so it moves by the deviation's change
        FSOLNpcDeviationFragment& deviation = view.Deviations[entity];
        view.States[entity].PositionM += ship->NewDeviationM - deviation.DeviationM;
        deviation.DeviationM = ship->NewDeviationM;
        deviation.DeviationVelocityMps = ship->NewDeviationVelocityMps;
        combat.TargetNpc = ship->TargetNpc;
        combat.Health = ship->Health;
        combat.RespawnS = ship->RespawnS;
        combat.CooldownS = ship->CooldownS;
        combat.bAlive = ship->bAlive;
    }
}
