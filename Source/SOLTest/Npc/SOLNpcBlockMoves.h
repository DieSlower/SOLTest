/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"

struct FMassCommandBuffer;
struct FMassEntityManager;

// The apply half of a lane ship block move (SDD 53 4.5, part 53c2b): the moved ships' rails rebuilt from their blocks'
// slots, and their Mass entities re-railed and re-bucketed into their new group's chunks (parked ships in the trailing
// bucket). USOLNpcSubsystem decides the moves (SOLLaneNetwork::StepShipBlocks) and calls these; the tests run them on
// a standalone entity manager
namespace SOLNpcBlockMoves
{
    // The spawn inputs a lane ship's rail is rebuilt from (SOLNpcScale::MakeSpawn's block overload)
    struct FLaneSpawnInputs
    {
        uint32 Seed = 0;
        SOLNpcScale::FCounts Counts;
        SOLNpcScale::FLayout Layout;
        SOLNpcScale::FBattleParams Battle;
        int32 BlockSize = SOL::NPC_LANE_SHIP_BLOCK_SIZE;
    };

    // What one RebucketLaneShips call did
    struct FRebucketStats
    {
        int32 Rerailed = 0;       // Ships whose rail fragment was rewritten
        int32 Rebucketed = 0;     // Ships moved to another group's chunks
        int32 Demoted = 0;        // Mid / Full ships forced to Far (none outside a time jump, by the release rule)
    };

    // Returns a rail's Mass group bucket: its group column, or groupCount (the trailing parked bucket) for a parked
    // lane ship or a group past groupCount
    SOLTEST_API int32 MassGroupOfRail(const FSOLNpcRail& rail, int32 groupCount);

    // Rebuilds the rails of the lane ships in the lane-local ranges from their blocks' slots (MakeSpawn's block
    // overload and LaneRailFromSpawn, the columns' current profiles) and raises each column's LaneMaxPeakMps over them;
    // rails is indexed by NPC index (lane ships after the battle ships)
    SOLTEST_API void RebuildLaneRails(const FLaneSpawnInputs& inputs, TConstArrayView<int32> blockSlot,
        TConstArrayView<FSOLShipRange> ranges, FSOLNpcGroupTable& groups, TArrayView<FSOLNpcRail> rails);

    // Writes the rails (by NPC index, battleShips + lane-local index) into the lane ships' entities (laneEntities by
    // lane-local index) of the ranges, resets their wake keys, marks the old and new groups' wake minimums stale, moves
    // each ship whose bucket (MassGroupOfRail) changed into the new group's chunks with
    // FMassEntityManager::SwapConstSharedFragmentForEntity, and demotes any Mid / Full ship among them to Far (tags and
    // attitude removed through the command buffer, flushed here). Game thread, outside any processor run
    SOLTEST_API FRebucketStats RebucketLaneShips(FMassEntityManager& manager,
        const TSharedPtr<FMassCommandBuffer>& commands, TConstArrayView<FMassEntityHandle> laneEntities,
        TConstArrayView<FSOLNpcRail> rails, int32 battleShips, TConstArrayView<FSOLShipRange> ranges,
        int32 groupCount, TArrayView<int32> groupWakeDirty);
}
