/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcTierProcessor.h"

#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcFragments.h"
#include "Npc/SOLNpcScaleMath.h"
#include "SOLConstants.h"

#include "Algo/Sort.h"
#include "Async/ParallelFor.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"

namespace
{
    constexpr uint8 NPC_TIER_FULL = static_cast<uint8>(SOLNpcScale::EAiTier::Full);
    constexpr uint8 NPC_TIER_MID = static_cast<uint8>(SOLNpcScale::EAiTier::Mid);
    constexpr uint8 NPC_TIER_FAR = static_cast<uint8>(SOLNpcScale::EAiTier::Far);
    constexpr uint8 NPC_RENDER_POINT = static_cast<uint8>(SOLNpcScale::ERenderTier::Point);

    //////////////////////////////////////////////////////////////////////////
    // Returns the AI tier a chunk's archetype stands for (its tag)
    uint8 ChunkAiTier(const FMassExecutionContext& context)
    {
        if (context.DoesArchetypeHaveTag<FSOLNpcFullAiTag>())
        {
            return NPC_TIER_FULL;
        }
        return context.DoesArchetypeHaveTag<FSOLNpcMidAiTag>() ? NPC_TIER_MID : NPC_TIER_FAR;
    }

    //////////////////////////////////////////////////////////////////////////
    // Defers the tag change that moves an entity from one AI tier's archetype to another's (a Full ship also gains or
    // loses its attitude fragment, SDD 18 section 3.7)
    void DeferTierMove(FMassCommandBuffer& commands, FMassEntityHandle entity, uint8 fromTier, uint8 toTier)
    {
        if (toTier == NPC_TIER_FULL)
        {
            commands.AddFragment<FSOLNpcAttitudeFragment>(entity);
        }
        else if (fromTier == NPC_TIER_FULL)
        {
            commands.RemoveFragment<FSOLNpcAttitudeFragment>(entity);
        }
        if (fromTier == NPC_TIER_FAR)
        {
            if (toTier == NPC_TIER_MID)
            {
                commands.AddTag<FSOLNpcMidAiTag>(entity);
            }
            else
            {
                commands.AddTag<FSOLNpcFullAiTag>(entity);
            }
        }
        else if (fromTier == NPC_TIER_MID)
        {
            if (toTier == NPC_TIER_FULL)
            {
                commands.SwapTags<FSOLNpcMidAiTag, FSOLNpcFullAiTag>(entity);
            }
            else
            {
                commands.RemoveTag<FSOLNpcMidAiTag>(entity);
            }
        }
        else if (toTier == NPC_TIER_MID)
        {
            commands.SwapTags<FSOLNpcFullAiTag, FSOLNpcMidAiTag>(entity);
        }
        else
        {
            commands.RemoveTag<FSOLNpcFullAiTag>(entity);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Registers the queries and opts out of the processing phases
USOLNpcTierProcessor::USOLNpcTierProcessor()
    : mEvaluateQuery(*this),
      mSelectQuery(*this),
      mCollectQuery(*this)
{
    bAutoRegisterWithProcessingPhases = false;
    ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);
}

//////////////////////////////////////////////////////////////////////////
// Declares the fragments each pass reads and writes
void USOLNpcTierProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& /*entityManager*/)
{
    // Returns whether a chunk takes part this frame
    const FMassChunkConditionFunction chunkFilter = [this](const FMassExecutionContext& context)
    {
        return IsChunkActive(context);
    };

    mEvaluateQuery.AddRequirement<FSOLNpcRailFragment>(EMassFragmentAccess::ReadOnly);
    mEvaluateQuery.AddRequirement<FSOLNpcDeviationFragment>(EMassFragmentAccess::ReadWrite);
    mEvaluateQuery.AddRequirement<FSOLNpcStateFragment>(EMassFragmentAccess::ReadWrite);
    mEvaluateQuery.AddChunkRequirement<FSOLNpcChunkWakeFragment>(EMassFragmentAccess::ReadWrite);
    mEvaluateQuery.AddConstSharedRequirement<FSOLNpcGroupFragment>();
    mEvaluateQuery.SetChunkFilter(chunkFilter);

    mSelectQuery.AddRequirement<FSOLNpcStateFragment>(EMassFragmentAccess::ReadWrite);
    mSelectQuery.AddRequirement<FSOLNpcDeviationFragment>(EMassFragmentAccess::ReadWrite);
    mSelectQuery.AddRequirement<FSOLNpcCombatFragment>(EMassFragmentAccess::ReadWrite);
    mSelectQuery.AddChunkRequirement<FSOLNpcChunkWakeFragment>(EMassFragmentAccess::ReadWrite);
    mSelectQuery.AddConstSharedRequirement<FSOLNpcGroupFragment>();
    mSelectQuery.AddConstSharedRequirement<FSOLNpcClassFragment>();
    mSelectQuery.SetChunkFilter(chunkFilter);

    mCollectQuery.AddRequirement<FSOLNpcRailFragment>(EMassFragmentAccess::ReadOnly);
    mCollectQuery.AddRequirement<FSOLNpcStateFragment>(EMassFragmentAccess::ReadWrite);
    mCollectQuery.AddRequirement<FSOLNpcDeviationFragment>(EMassFragmentAccess::ReadWrite);
    mCollectQuery.AddRequirement<FSOLNpcCombatFragment>(EMassFragmentAccess::ReadOnly);
    mCollectQuery.AddRequirement<FSOLNpcAttitudeFragment>(EMassFragmentAccess::ReadOnly,
        EMassFragmentPresence::Optional);
    mCollectQuery.AddTagRequirement<FSOLNpcMidAiTag>(EMassFragmentPresence::Any);
    mCollectQuery.AddTagRequirement<FSOLNpcFullAiTag>(EMassFragmentPresence::Any);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a chunk takes part this frame: already CPU-simulated, or a candidate group with an awake ship
bool USOLNpcTierProcessor::IsChunkActive(const FMassExecutionContext& context) const
{
    if (ChunkAiTier(context) != NPC_TIER_FAR)
    {
        return true;
    }
    // Group cull, then the per-chunk temporal cull: every ship sleeps while the chunk's minimum key exceeds the clock
    // (Evaluate also visits every chunk of a group whose chunk minimums went stale: a demotion or a spawn slice)
    const int32 group = context.GetConstSharedFragment<FSOLNpcGroupFragment>().GroupIndex;
    const bool bCandidate = mInputs.GroupCandidate.IsValidIndex(group) && mInputs.GroupCandidate[group] != 0;
    const bool bDirty = mInputs.GroupWakeDirty.IsValidIndex(group) && mInputs.GroupWakeDirty[group] != 0;
    const double wakeClockM = mInputs.GroupWakeClockM.IsValidIndex(group) ? mInputs.GroupWakeClockM[group] : 0.0;
    return SOLNpcScale::IsFarChunkActive(bCandidate, bDirty, mPass == ESOLNpcTierPass::Evaluate,
        context.GetChunkFragment<FSOLNpcChunkWakeFragment>().MinWakeKeyM, wakeClockM);
}

//////////////////////////////////////////////////////////////////////////
// Records one chunk's view (serial chunk walk; the passes then run over mChunks with ParallelFor)
void USOLNpcTierProcessor::AddChunkView(FMassExecutionContext& context, const bool bCollect)
{
    FChunkView& view = mChunks.AddDefaulted_GetRef();
    view.Count = context.GetNumEntities();
    view.Tier = ChunkAiTier(context);
    view.States = context.GetMutableFragmentView<FSOLNpcStateFragment>().GetData();
    view.Deviations = context.GetMutableFragmentView<FSOLNpcDeviationFragment>().GetData();
    view.Entities = context.GetEntities().GetData();
    if (bCollect)
    {
        // Read only in Collect (the view type is shared with Select, which writes it)
        view.Rails = context.GetFragmentView<FSOLNpcRailFragment>().GetData();
        view.Combats = const_cast<FSOLNpcCombatFragment*>(context.GetFragmentView<FSOLNpcCombatFragment>().GetData());
        view.Attitudes = context.GetFragmentView<FSOLNpcAttitudeFragment>().GetData();
        return;
    }
    view.Group = context.GetConstSharedFragment<FSOLNpcGroupFragment>().GroupIndex;
    view.Wake = &context.GetMutableChunkFragment<FSOLNpcChunkWakeFragment>();
    if (mPass == ESOLNpcTierPass::Evaluate)
    {
        view.Rails = context.GetFragmentView<FSOLNpcRailFragment>().GetData();
        return;
    }
    view.Combats = context.GetMutableFragmentView<FSOLNpcCombatFragment>().GetData();
    view.ClassIndex = context.GetConstSharedFragment<FSOLNpcClassFragment>().ClassIndex;
}

//////////////////////////////////////////////////////////////////////////
// Runs the current pass: a serial walk records the active chunks, then the work runs in parallel over them
void USOLNpcTierProcessor::Execute(FMassEntityManager& /*entityManager*/, FMassExecutionContext& context)
{
    if (mInputs.RailFrame == nullptr)
    {
        return;
    }
    mChunks.Reset();
    switch (mPass)
    {
    case ESOLNpcTierPass::Evaluate:
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcTierProcessor::Evaluate);
        mEvaluated = 0;
        mBudgetDistanceCount = 0;
        if (mDistancesM == nullptr)
        {
            break;
        }
        mEvaluateQuery.ForEachEntityChunk(context, [this](FMassExecutionContext& chunkContext)
        {
            AddChunkView(chunkContext, false);
        });
        ParallelFor(mChunks.Num(), [this](const int32 chunk)
        {
            EvaluateChunk(mChunks[chunk]);
        });
        break;
    }
    case ESOLNpcTierPass::Select:
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcTierProcessor::Select);
        mAiCounts[0] = 0;
        mAiCounts[1] = 0;
        mRenderCounts[0] = 0;
        mRenderCounts[1] = 0;
        mTierChanges = 0;
        mMoveCount = 0;
        mSelectQuery.ForEachEntityChunk(context, [this](FMassExecutionContext& chunkContext)
        {
            AddChunkView(chunkContext, false);
        });
        int32 visited = 0;
        for (const FChunkView& view : mChunks)
        {
            visited += view.Count;
        }
        if (mMoves.Num() < visited)
        {
            mMoves.SetNumUninitialized(visited);
        }
        ParallelFor(mChunks.Num(), [this](const int32 chunk)
        {
            SelectChunk(mChunks[chunk]);
        });

        // Archetype moves through the command buffer (flushed when the pass ends), in entity order: the chunks'
        // parallel appends land in any order, and the order decides the ships' slots in their new chunks
        Algo::Sort(MakeArrayView(mMoves.GetData(), mMoveCount), [](const FTierMove& a, const FTierMove& b)
        {
            return a.Entity.AsNumber() < b.Entity.AsNumber();
        });
        for (int32 move = 0; move < mMoveCount; ++move)
        {
            DeferTierMove(context.Defer(), mMoves[move].Entity, mMoves[move].FromTier, mMoves[move].ToTier);
        }
        break;
    }
    case ESOLNpcTierPass::Collect:
    default:
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcTierProcessor::Collect);
        if (mCpuShips == nullptr)
        {
            break;
        }
        mCollectQuery.ForEachEntityChunk(context, [this](FMassExecutionContext& chunkContext)
        {
            AddChunkView(chunkContext, true);
        });

        // Every Mid / Full ship gets a slot in chunk order (dead ones hidden), so the list stays deterministic
        int32 total = 0;
        for (FChunkView& view : mChunks)
        {
            view.Offset = total;
            total += view.Count;
        }
        mCpuShips->SetNumUninitialized(total, EAllowShrinking::No);
        ParallelFor(mChunks.Num(), [this](const int32 chunk)
        {
            CollectChunk(mChunks[chunk]);
        });
        break;
    }
    }
}

//////////////////////////////////////////////////////////////////////////
// Evaluates rails and distances of one chunk's awake ships, appends the budget distances, refreshes its minimum
void USOLNpcTierProcessor::EvaluateChunk(const FChunkView& view)
{
    const FSOLNpcRailFrame& frame = *mInputs.RailFrame;
    const FVector3d viewpointM = mInputs.ViewpointM;
    const double keepRadiusM = mInputs.MidKeepRadiusM;
    const double wakeClockM = mInputs.GroupWakeClockM.IsValidIndex(view.Group) ? mInputs.GroupWakeClockM[view.Group]
        : 0.0;
    const bool bFarChunk = view.Tier == NPC_TIER_FAR;

    // Budget distances are staged on the stack and appended in blocks with one atomic reservation each
    constexpr int32 STAGE_SIZE = 256;
    double staged[STAGE_SIZE];
    int32 stagedCount = 0;
    double* const budgetOut = mDistancesM->GetData();
    const int32 budgetCapacity = mDistancesM->Num();

    // Appends the staged distances to the shared budget array
    const auto flushStaged = [&]()
    {
        const int32 first = FPlatformAtomics::InterlockedAdd(&mBudgetDistanceCount, stagedCount);
        const int32 writable = FMath::Clamp(budgetCapacity - first, 0, stagedCount);
        FMemory::Memcpy(budgetOut + first, staged, writable * sizeof(double));
        stagedCount = 0;
    };

    int32 evaluated = 0;
    double minWakeKeyM = TNumericLimits<double>::Max();
    for (int32 entity = 0; entity < view.Count; ++entity)
    {
        // A sleeping Far ship cannot have reached the keep radius yet
        FSOLNpcStateFragment& state = view.States[entity];
        if (bFarChunk && state.WakeKeyM > wakeClockM)
        {
            minWakeKeyM = FMath::Min(minWakeKeyM, state.WakeKeyM);
            continue;
        }
        ++evaluated;
        state.PrevPositionM = state.PositionM;
        state.bHasPrevPosition = state.AiTier != NPC_TIER_FAR ? 1 : 0;
        state.PositionM = SOLNpcRail::EvaluateM(view.Rails[entity].Rail, frame) + view.Deviations[entity].DeviationM;
        const double distanceM = (state.PositionM - viewpointM).Size();
        state.DistanceM = static_cast<float>(distanceM);

        // Beyond the keep radius a Far ship sleeps until the wake clock covers the gap; CPU ships stay awake
        state.WakeKeyM = (bFarChunk && distanceM > keepRadiusM) ? wakeClockM + (distanceM - keepRadiusM) : wakeClockM;
        minWakeKeyM = FMath::Min(minWakeKeyM, state.WakeKeyM);
        if (distanceM < keepRadiusM)
        {
            staged[stagedCount++] = distanceM;
            if (stagedCount == STAGE_SIZE)
            {
                flushStaged();
            }
        }
    }
    if (stagedCount > 0)
    {
        flushStaged();
    }
    view.Wake->MinWakeKeyM = bFarChunk ? minWakeKeyM : 0.0;
    FPlatformAtomics::InterlockedAdd(&mEvaluated, evaluated);
}

//////////////////////////////////////////////////////////////////////////
// Selects tiers of one chunk and records its archetype moves
void USOLNpcTierProcessor::SelectChunk(const FChunkView& view)
{
    const uint8 chunkTier = view.Tier;
    const double wakeClockM = mInputs.GroupWakeClockM.IsValidIndex(view.Group) ? mInputs.GroupWakeClockM[view.Group]
        : 0.0;
    int32 aiCounts[2] = {};
    int32 renderCounts[2] = {};
    int32 changes = 0;
    bool bDemotedToFar = false;

    // Moves are staged on the stack and appended in blocks with one atomic reservation each
    constexpr int32 STAGE_SIZE = 64;
    FTierMove staged[STAGE_SIZE];
    int32 stagedCount = 0;

    // Appends the staged moves to the shared move array
    const auto flushStaged = [&]()
    {
        const int32 first = FPlatformAtomics::InterlockedAdd(&mMoveCount, stagedCount);
        for (int32 move = 0; move < stagedCount; ++move)
        {
            mMoves[first + move] = staged[move];
        }
        stagedCount = 0;
    };

    for (int32 entity = 0; entity < view.Count; ++entity)
    {
        // Sleeping ships (still beyond the keep radius) stay Far
        FSOLNpcStateFragment& state = view.States[entity];
        if (chunkTier == NPC_TIER_FAR && state.WakeKeyM > wakeClockM)
        {
            continue;
        }
        const double distanceM = state.DistanceM;
        uint8 aiTier = static_cast<uint8>(SOLNpcScale::SelectAiTier(
            static_cast<SOLNpcScale::EAiTier>(state.AiTier), distanceM, mInputs.FullRadiusM, mInputs.MidRadiusM,
            mInputs.AiHysteresis));

        // Dropping to Far snaps the deviation to the rail: only when that is under half a pixel, or the budget forces
        // it (SDD 18 section 3.7); otherwise the ship stays Mid while it flies back to its rail
        if (aiTier == NPC_TIER_FAR && state.AiTier != NPC_TIER_FAR && !SOLNpcAi::AllowFarDemotion(
            view.Deviations[entity].DeviationM.Size(), distanceM, mInputs.PixelAngleRad, mInputs.bMidBudgetForced))
        {
            aiTier = NPC_TIER_MID;
        }

        // Far: drawn by the GPU point layer from the bare rail, so the deviation snaps to zero (beyond the Mid radius)
        if (aiTier == NPC_TIER_FAR)
        {
            state.RenderTier = NPC_RENDER_POINT;
            if (state.AiTier != NPC_TIER_FAR)
            {
                view.Deviations[entity] = FSOLNpcDeviationFragment();
            }
        }
        else
        {
            state.RenderTier = static_cast<uint8>(SOLNpcScale::SelectRenderTier(
                static_cast<SOLNpcScale::ERenderTier>(state.RenderTier), distanceM, mInputs.MeshRadiusM,
                mInputs.InstancedRadiusM, mInputs.RenderHysteresis));
            ++aiCounts[aiTier];
            if (state.RenderTier < NPC_RENDER_POINT)
            {
                ++renderCounts[state.RenderTier];
            }
        }
        // Only the Full tier fights: a dead ship leaving it respawns at once, back on its rail slot (so no dead ship
        // is ever Mid or Far, where the GPU far layer could draw it and no respawn timer runs)
        FSOLNpcCombatFragment& combat = view.Combats[entity];
        if (chunkTier == NPC_TIER_FULL && aiTier != NPC_TIER_FULL && combat.bAlive == 0)
        {
            combat.bAlive = 1;
            combat.Health = static_cast<float>(SOLNpcAi::GetClass(view.ClassIndex).MaxHealth);
            combat.RespawnS = 0.0f;
            combat.CooldownS = 0.0f;
            combat.TargetNpc = INDEX_NONE;
            state.PositionM -= view.Deviations[entity].DeviationM;
            view.Deviations[entity] = FSOLNpcDeviationFragment();
        }
        state.AiTier = aiTier;

        // Archetype move only when the tier differs from the chunk's tag (rare thanks to hysteresis and budgets)
        if (aiTier != chunkTier)
        {
            FTierMove& move = staged[stagedCount++];
            move.Entity = view.Entities[entity];
            move.FromTier = chunkTier;
            move.ToTier = aiTier;
            bDemotedToFar |= aiTier == NPC_TIER_FAR;
            ++changes;
            if (stagedCount == STAGE_SIZE)
            {
                flushStaged();
            }
        }
    }
    if (stagedCount > 0)
    {
        flushStaged();
    }

    // A ship landing in a Far chunk makes that chunk's minimum stale: the group is evaluated in full next frame
    if (bDemotedToFar && mInputs.GroupWakeDirty.IsValidIndex(view.Group))
    {
        FPlatformAtomics::InterlockedExchange(&mInputs.GroupWakeDirty[view.Group], 1);
    }
    FPlatformAtomics::InterlockedAdd(&mAiCounts[0], aiCounts[0]);
    FPlatformAtomics::InterlockedAdd(&mAiCounts[1], aiCounts[1]);
    FPlatformAtomics::InterlockedAdd(&mRenderCounts[0], renderCounts[0]);
    FPlatformAtomics::InterlockedAdd(&mRenderCounts[1], renderCounts[1]);
    FPlatformAtomics::InterlockedAdd(&mTierChanges, changes);
}

//////////////////////////////////////////////////////////////////////////
// Copies one Mid / Full chunk's ships into the CPU ship list at its offset
void USOLNpcTierProcessor::CollectChunk(const FChunkView& view) const
{
    const FSOLNpcRailFrame& frame = *mInputs.RailFrame;
    FSOLNpcCpuShip* const out = mCpuShips->GetData() + view.Offset;
    for (int32 entity = 0; entity < view.Count; ++entity)
    {
        const FSOLNpcStateFragment& state = view.States[entity];
        FSOLNpcCpuShip& ship = out[entity];
        ship.PositionM = state.PositionM;
        ship.PrevPositionM = state.bHasPrevPosition != 0 ? state.PrevPositionM : state.PositionM;
        ship.Faction = view.Rails[entity].Rail.Faction;
        ship.RenderTier = state.RenderTier;
        ship.AiTier = state.AiTier;
        ship.NpcIndex = view.Combats[entity].NpcIndex;
        ship.RailDistanceM = static_cast<float>((state.PositionM - view.Deviations[entity].DeviationM
            - mInputs.ViewpointM).Size());

        // Hull heading (hulls only): a Full ship's nose, else the rail plus deviation velocity, which is also the nose
        // a promotion to Full starts with (SOLNpcAi::HandoffOrientation), so neither hand-off turns the hull
        ship.HeadingDirection = FVector3f::ZeroVector;
        if (state.RenderTier != NPC_RENDER_POINT)
        {
            const FQuat4d* const orientation = view.Attitudes != nullptr ? &view.Attitudes[entity].Orientation
                : nullptr;
            if (orientation != nullptr && orientation->SizeSquared() > 0.0)
            {
                ship.HeadingDirection = FVector3f(orientation->GetForwardVector());
            }
            else
            {
                const FSOLNpcRail& rail = view.Rails[entity].Rail;
                ship.HeadingDirection = FVector3f(SOLNpcAi::HullForward(SOLNpcRail::VelocityMps(rail, frame)
                    + view.Deviations[entity].DeviationVelocityMps, state.PositionM - frame.GroupBodyM[rail.Group]));
            }
        }

        // Dead ships (waiting to respawn) are not drawn
        ship.bHidden = view.Combats[entity].bAlive == 0 ? 1 : 0;
    }
}
