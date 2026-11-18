/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcFullAiProcessor.h"

#include "Npc/SOLNpcAiMath.h"
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
    mQuery.AddRequirement<FSOLNpcAttitudeFragment>(EMassFragmentAccess::ReadWrite);
    mQuery.AddConstSharedRequirement<FSOLNpcClassFragment>();
    mQuery.AddTagRequirement<FSOLNpcFullAiTag>(EMassFragmentPresence::All);
}

//////////////////////////////////////////////////////////////////////////
// Runs the current pass (serial chunk walk, then parallel over the chunks)
void USOLNpcFullAiProcessor::Execute(FMassEntityManager& /*entityManager*/, FMassExecutionContext& context)
{
    if (mInputs.Combat == nullptr || mInputs.RailFrame == nullptr || !(mInputs.RealDeltaS > 0.0))
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
        view.Attitudes = chunkContext.GetMutableFragmentView<FSOLNpcAttitudeFragment>().GetData();
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
    const FSOLNpcRailFrame& frame = *mInputs.RailFrame;
    for (int32 entity = 0; entity < view.Count; ++entity)
    {
        const FSOLNpcStateFragment& state = view.States[entity];
        const FSOLNpcDeviationFragment& deviation = view.Deviations[entity];
        const FSOLNpcCombatFragment& combat = view.Combats[entity];
        const FSOLNpcRail& rail = view.Rails[entity].Rail;
        FSOLNpcAiShip ship;

        // Hull heading (one leg derivation, SOLNpcRail::Motion) and the up hint (a lane ship's lane top, else away from
        // the group's body; SDD 50 section 16); a ship just promoted to Full starts with its nose along its rail
        // heading plus deviation velocity and its up toward that hint (the attitude its Mid hull was drawn with) and
        // no spin, and its step keeps the nose on the rail heading (a lane ship faces its thrust, D21) and steers its
        // up toward the hint at its limited rate, so a lane hull does not turn or roll at promotion or at demotion
        const FSOLNpcRailMotion motion = SOLNpcRail::Motion(rail, frame, combat.NpcIndex);
        ship.RailHeadingMps = SOLNpcRail::HullHeadingVelocityMps(motion, FVector3d::ZeroVector);
        ship.UpHint = SOLNpcRail::HullUpHint(motion, state.PositionM - frame.GroupBodyM[rail.Group]);
        FSOLNpcAttitudeFragment& attitude = view.Attitudes[entity];
        if (attitude.Orientation.SizeSquared() <= 0.0)
        {
            attitude.Orientation = SOLNpcAi::HandoffOrientation(SOLNpcRail::HullHeadingVelocityMps(motion,
                deviation.DeviationVelocityMps), ship.UpHint);
            attitude.AngularVelocityRadPerS = FVector3d::ZeroVector;
        }
        ship.Orientation = attitude.Orientation;
        ship.AngularVelocityRadPerS = attitude.AngularVelocityRadPerS;
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
        ship.Faction = rail.Faction;
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
        view.Attitudes[entity].Orientation = ship->Orientation;
        view.Attitudes[entity].AngularVelocityRadPerS = ship->AngularVelocityRadPerS;
        combat.TargetNpc = ship->TargetNpc;
        combat.Health = ship->Health;
        combat.RespawnS = ship->RespawnS;
        combat.CooldownS = ship->CooldownS;
        combat.bAlive = ship->bAlive;
    }
}
