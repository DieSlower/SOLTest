/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcBlockMoves.h"

#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcFragments.h"

#include "Async/ParallelFor.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"

namespace
{
    // Ships per ParallelFor task when a range is large (a time jump re-rails every lane ship)
    constexpr int32 BLOCK_MOVE_SHIPS_PER_TASK = 4096;
}

//////////////////////////////////////////////////////////////////////////
// Returns a rail's Mass group bucket: its column, or groupCount for a parked ship
int32 SOLNpcBlockMoves::MassGroupOfRail(const FSOLNpcRail& rail, const int32 groupCount)
{
    return FMath::Min<int32>(rail.Group, groupCount);
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the rails of the lane ships in the ranges from their blocks' slots and raises the columns' peaks
void SOLNpcBlockMoves::RebuildLaneRails(const FLaneSpawnInputs& inputs, TConstArrayView<int32> blockSlot,
    TConstArrayView<FSOLShipRange> ranges, FSOLNpcGroupTable& groups, TArrayView<FSOLNpcRail> rails)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SOLNpcBlockMoves::RebuildLaneRails);
    const FSOLNpcGroupTable& constGroups = groups;
    for (const FSOLShipRange& range : ranges)
    {
        const int32 first = inputs.Counts.Battle + range.First;
        if (range.Count <= 0 || first < 0 || first + range.Count > rails.Num())
        {
            continue;
        }

        // Rebuilds the rails of [begin, end) (NPC indices); stateless per index, so tasks may split a range
        const auto rebuild = [&inputs, blockSlot, &constGroups, rails](const int32 begin, const int32 end)
        {
            for (int32 npcIndex = begin; npcIndex < end; ++npcIndex)
            {
                const SOLNpcScale::FSpawn spawn = SOLNpcScale::MakeSpawn(inputs.Seed, npcIndex, inputs.Counts,
                    inputs.Layout, inputs.Battle, blockSlot, inputs.BlockSize);
                rails[npcIndex] = SOLNpcRail::LaneRailFromSpawn(spawn, constGroups);
            }
        };
        if (range.Count <= BLOCK_MOVE_SHIPS_PER_TASK)
        {
            rebuild(first, first + range.Count);
        }
        else
        {
            ParallelFor(FMath::DivideAndRoundUp(range.Count, BLOCK_MOVE_SHIPS_PER_TASK), [&](const int32 task)
            {
                const int32 begin = first + task * BLOCK_MOVE_SHIPS_PER_TASK;
                rebuild(begin, FMath::Min(begin + BLOCK_MOVE_SHIPS_PER_TASK, first + range.Count));
            });
        }
        SOLNpcRail::AccumulateLaneMaxPeak(rails.Slice(first, range.Count), groups.LaneMaxPeakMps);
    }
}

//////////////////////////////////////////////////////////////////////////
// Re-rails and re-buckets the lane ships of the ranges, demoting any CPU-tier ship among them to Far
SOLNpcBlockMoves::FRebucketStats SOLNpcBlockMoves::RebucketLaneShips(FMassEntityManager& manager,
    const TSharedPtr<FMassCommandBuffer>& commands, TConstArrayView<FMassEntityHandle> laneEntities,
    TConstArrayView<FSOLNpcRail> rails, const int32 battleShips, TConstArrayView<FSOLShipRange> ranges,
    const int32 groupCount, TArrayView<int32> groupWakeDirty)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SOLNpcBlockMoves::RebucketLaneShips);
    FRebucketStats stats;
    constexpr uint8 TIER_FAR = static_cast<uint8>(SOLNpcScale::EAiTier::Far);
    constexpr uint8 RENDER_POINT = static_cast<uint8>(SOLNpcScale::ERenderTier::Point);

    // One group fragment per bucket, created on first use
    TArray<FConstSharedStruct, TInlineAllocator<SOL::NPC_GROUP_TEXTURE_WIDTH + 1>> groupFragments;
    groupFragments.SetNum(FMath::Max(groupCount, 0) + 1);

    // Marks a group's chunk wake minimums stale (the parked bucket has no entry)
    const auto markDirty = [groupWakeDirty](const int32 group)
    {
        if (groupWakeDirty.IsValidIndex(group))
        {
            groupWakeDirty[group] = 1;
        }
    };
    for (const FSOLShipRange& range : ranges)
    {
        for (int32 local = range.First; local < range.First + range.Count; ++local)
        {
            const int32 npcIndex = battleShips + local;
            if (!laneEntities.IsValidIndex(local) || !rails.IsValidIndex(npcIndex)
                || !manager.IsEntityActive(laneEntities[local]))
            {
                continue;
            }
            const FMassEntityHandle entity = laneEntities[local];
            const FSOLNpcRail& rail = rails[npcIndex];
            {
                // The new rail; a wake key of 0 so the ship is evaluated at its group's next candidate visit
                const FMassEntityView view(manager, entity);
                view.GetFragmentData<FSOLNpcRailFragment>().Rail = rail;
                FSOLNpcStateFragment& state = view.GetFragmentData<FSOLNpcStateFragment>();
                state.WakeKeyM = 0.0;

                // A CPU-tier ship (only at a time jump) is demoted to Far at once: no deviation, alive, untargeted
                if (state.AiTier != TIER_FAR)
                {
                    state.AiTier = TIER_FAR;
                    state.RenderTier = RENDER_POINT;
                    state.bHasPrevPosition = 0;
                    view.GetFragmentData<FSOLNpcDeviationFragment>() = FSOLNpcDeviationFragment();
                    FSOLNpcCombatFragment& combat = view.GetFragmentData<FSOLNpcCombatFragment>();
                    combat.TargetNpc = INDEX_NONE;
                    if (combat.bAlive == 0)
                    {
                        combat.bAlive = 1;
                        combat.RespawnS = 0.0f;
                        combat.CooldownS = 0.0f;
                        combat.Health = static_cast<float>(SOLNpcAi::GetClass(
                            view.GetConstSharedFragmentData<FSOLNpcClassFragment>().ClassIndex).MaxHealth);
                    }
                    if (manager.DoesEntityHaveElement(entity, FSOLNpcFullAiTag::StaticStruct()))
                    {
                        commands->RemoveTag<FSOLNpcFullAiTag>(entity);
                        commands->RemoveFragment<FSOLNpcAttitudeFragment>(entity);
                    }
                    if (manager.DoesEntityHaveElement(entity, FSOLNpcMidAiTag::StaticStruct()))
                    {
                        commands->RemoveTag<FSOLNpcMidAiTag>(entity);
                    }
                    ++stats.Demoted;
                }

                // Both groups' chunk minimums are stale (a ship with key 0 joins the new group's chunks)
                const int32 oldGroup = view.GetConstSharedFragmentData<FSOLNpcGroupFragment>().GroupIndex;
                const int32 newGroup = MassGroupOfRail(rail, groupCount);
                markDirty(oldGroup);
                markDirty(newGroup);
                ++stats.Rerailed;
                if (oldGroup == newGroup || !groupFragments.IsValidIndex(newGroup))
                {
                    continue;
                }
            }

            // The bucket move: the entity's chunk changes within its archetype (the view above is stale after it)
            const int32 newGroup = MassGroupOfRail(rail, groupCount);
            if (!groupFragments[newGroup].IsValid())
            {
                groupFragments[newGroup] = manager.GetOrCreateConstSharedFragment(
                    FSOLNpcGroupFragment::Make(newGroup));
            }
            manager.SwapConstSharedFragmentForEntity(entity, groupFragments[newGroup]);
            ++stats.Rebucketed;
        }
    }
    if (stats.Demoted > 0)
    {
        manager.FlushCommands(commands);
    }
    return stats;
}
