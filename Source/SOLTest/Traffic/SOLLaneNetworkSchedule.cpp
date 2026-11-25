/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneNetworkSchedule.h"

#include "SOLTest.h"

#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"

#include <limits>

namespace
{
    constexpr double LANESCHED_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double LANESCHED_POS_INF = std::numeric_limits<double>::infinity();
    constexpr int64 LANESCHED_NO_EPOCH = std::numeric_limits<int64>::min();   // An empty position-ring slot
    constexpr int32 LANESCHED_MAX_NODES = 64;       // Pair masks and the epoch distances cover this many nodes
    constexpr int32 LANESCHED_MAX_RUN = 255;        // The in-band run is stored in a byte
    constexpr int32 LANESCHED_MAX_CONTEXTS = 32;    // Rebuild tasks at most (each holds one distance matrix)

    //////////////////////////////////////////////////////////////////////////
    // Returns whether bit index is set in a mask of words
    bool LaneSchedBit(const uint64* words, const int32 index)
    {
        return ((words[index >> 6] >> (index & 63)) & 1) != 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets bit index in a mask of words
    void LaneSchedSetBit(uint64* words, const int32 index)
    {
        words[index >> 6] |= uint64(1) << (index & 63);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two lane pairs are the same
    bool LaneSchedSamePair(const FSOLLanePair& a, const FSOLLanePair& b)
    {
        return a.NodeA == b.NodeA && a.NodeB == b.NodeB;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an epoch's slot in a direct-mapped ring of slotCount slots (non-negative for negative epochs too)
    int32 LaneSchedRingSlot(const int64 epoch, const int32 slotCount)
    {
        const int64 slot = epoch % slotCount;
        return static_cast<int32>(slot < 0 ? slot + slotCount : slot);
    }
}

//////////////////////////////////////////////////////////////////////////
// Takes the configuration and the position function; sizes every buffer once
void FSOLLaneNetworkSchedule::Configure(const FSOLLaneNetworkConfig& config, FSOLLaneNodePositionsFn positionsFn)
{
    Reset();
    mConfig = config;
    mPositionsFn = MoveTemp(positionsFn);
    mNodeCount = mConfig.Nodes.Num();
    const bool bValid = mPositionsFn && mNodeCount >= 2 && mNodeCount <= LANESCHED_MAX_NODES
        && mConfig.NodeMaxSpeedMps.Num() == mNodeCount && mConfig.RootNode >= 0 && mConfig.RootNode < mNodeCount
        && mConfig.HorizonEpochs >= 2 && mConfig.WarmupEpochs >= 0 && mConfig.EpochSamples >= 2
        && mConfig.LookBackEpochs >= 1 && mConfig.LookBackEpochs <= LANESCHED_MAX_RUN && mConfig.CatchUpEpochs >= 1
        && mConfig.CatchUpEpochs < mConfig.HorizonEpochs && mConfig.EpochS > 0.0 && mConfig.SunRadiusM > 0.0
        && mConfig.SlotCapacity >= mConfig.BaseLanes.Num();
    if (!bValid)
    {
        UE_LOG(LogSOL, Warning, TEXT("Lane network: invalid configuration (%d nodes), no schedule"), mNodeCount);
        Reset();
        return;
    }

    // Pair tables in A-major order, so iterating a mask visits lanes sorted by (NodeA, NodeB)
    mPairCount = mNodeCount * (mNodeCount - 1) / 2;
    mWordsPerEntry = (mPairCount + 63) / 64;
    mPairA.Reset(mPairCount);
    mPairB.Reset(mPairCount);
    for (int32 a = 0; a < mNodeCount; ++a)
    {
        for (int32 b = a + 1; b < mNodeCount; ++b)
        {
            mPairA.Add(a);
            mPairB.Add(b);
        }
    }
    mIsBasePair.Init(0, mPairCount);
    for (const FSOLLanePair& lane : mConfig.BaseLanes)
    {
        const int32 pair = PairIndex(lane);
        if (pair != INDEX_NONE)
        {
            mIsBasePair[pair] = 1;
        }
    }

    // The ring buffer, the position rings and the scratch, sized once. The start and inner rings cover every epoch a
    // rebuild or a step reads (k - max(W, L) .. k + H) without two of them sharing a slot
    mHorizon = mConfig.HorizonEpochs;
    mPicks.SetNum(mHorizon * mNodeCount);
    mSetMask.SetNumZeroed(mHorizon * mWordsPerEntry);
    mOpenMask.SetNumZeroed(mHorizon * mWordsPerEntry);
    mAboveMask.SetNumZeroed(mHorizon * mWordsPerEntry);
    mBelowMask.SetNumZeroed(mHorizon * mWordsPerEntry);
    mBandRun.SetNumZeroed(mHorizon * mPairCount);
    mStartSlots = mHorizon + FMath::Max(mConfig.WarmupEpochs, mConfig.LookBackEpochs) + 1;
    mStartPositions.SetNumZeroed(mStartSlots * mNodeCount);
    mStartNearest.Init(INDEX_NONE, mStartSlots * mNodeCount);
    mStartNearestM.Init(LANESCHED_NAN, mStartSlots * mNodeCount);
    mStartCandidates.Init(0, mStartSlots);
    mStartEpochTag.Init(LANESCHED_NO_EPOCH, mStartSlots);
    mInnerSlots = mStartSlots;
    mInnerPositions.SetNumZeroed(mInnerSlots * FMath::Max(mConfig.EpochSamples - 2, 0) * mNodeCount);
    mInnerEpochTag.Init(LANESCHED_NO_EPOCH, mInnerSlots);
    mWorkPicks.Reserve(mNodeCount);
    mPositions.SetNum(mNodeCount);
    mSampleClearances.SetNum(mConfig.EpochSamples);
    mFlagScratch.Reserve(mHorizon);

    // The rebuild's task scratch: one per worker plus the game thread, prepared once
    const int32 contextCount = FMath::Clamp(FTaskGraphInterface::Get().GetNumWorkerThreads() + 1, 1,
        LANESCHED_MAX_CONTEXTS);
    mBuildContexts.SetNum(contextCount);
    for (FBuildContext& context : mBuildContexts)
    {
        SOLLaneNetwork::PrepareEpochDistances(mConfig.Nodes, mConfig.BaseLanes, context.Distances);
        context.Lanes.Reserve(mConfig.BaseLanes.Num() + mNodeCount);
        context.Links.Reserve(mNodeCount);
        context.SampleClearances.Reserve(mConfig.EpochSamples);
    }

    // Slots: the base lanes own 0 .. base - 1, the rest start free
    const int32 capacity = mConfig.SlotCapacity;
    mSlots.Init(FSOLLanePair(), capacity);
    for (int32 slot = 0; slot < mConfig.BaseLanes.Num(); ++slot)
    {
        mSlots[slot] = mConfig.BaseLanes[slot];
    }
    mSlotHeld.Init(0, capacity);
    mSlotOpen.Init(0, capacity);
    mSlotOpenStartS.Init(LANESCHED_NAN, capacity);
    mSlotCloseS.Init(LANESCHED_NAN, capacity);
    mIsConfigured = true;
}

//////////////////////////////////////////////////////////////////////////
// Forgets the configuration, every epoch, the position rings and the slot state
void FSOLLaneNetworkSchedule::Reset()
{
    mConfig = FSOLLaneNetworkConfig();
    mPositionsFn = nullptr;
    mIsConfigured = false;
    mIsBuilt = false;
    mHasJumpPending = false;
    mNodeCount = 0;
    mPairCount = 0;
    mWordsPerEntry = 0;
    mHorizon = 0;
    mHead = 0;
    mFirstEpoch = 0;
    mWarmupStartEpoch = 0;
    mPairA.Reset();
    mPairB.Reset();
    mIsBasePair.Reset();
    mPicks.Reset();
    mSetMask.Reset();
    mOpenMask.Reset();
    mAboveMask.Reset();
    mBelowMask.Reset();
    mBandRun.Reset();
    mStartPositions.Reset();
    mStartNearest.Reset();
    mStartNearestM.Reset();
    mStartCandidates.Reset();
    mStartEpochTag.Reset();
    mStartSlots = 0;
    mInnerPositions.Reset();
    mInnerEpochTag.Reset();
    mInnerSlots = 0;
    mSlots.Reset();
    mSlotHeld.Reset();
    mSlotOpen.Reset();
    mSlotOpenStartS.Reset();
    mSlotCloseS.Reset();
    mSlotOverflow = 0;
    mSlotOverflowReports = 0;
    mHasSlotHoldChange = false;
    mSlotSetWords.Reset();
    mBuildContexts.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Brings the schedule to the epoch of simTimeS: steps forward, or rebuilds on a time jump
ESOLLaneNetworkAdvance FSOLLaneNetworkSchedule::Advance(const double simTimeS)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FSOLLaneNetworkSchedule::Advance);
    if (!mIsConfigured || !FMath::IsFinite(simTimeS))
    {
        return ESOLLaneNetworkAdvance::None;
    }
    const int64 epoch = SOLLaneNetwork::LaneNetworkEpochIndex(simTimeS, mConfig.EpochS);
    if (!mIsBuilt || mHasJumpPending || epoch < mFirstEpoch || epoch - mFirstEpoch > mConfig.CatchUpEpochs)
    {
        // A rebuild drops every hold from before it (SDD 53 4.5): the caller re-shares every block over the new slots,
        // so a stale hold would only keep a lane of the old set in a column and push a new lane out
        for (uint8& held : mSlotHeld)
        {
            held = 0;
        }
        Rebuild(epoch);
        UpdateSlots(true);
        mHasJumpPending = false;
        return ESOLLaneNetworkAdvance::Rebuilt;
    }
    if (epoch == mFirstEpoch)
    {
        return ESOLLaneNetworkAdvance::None;
    }

    // One epoch at a time, the slots stepped with each (their latch carries from epoch to epoch)
    while (mFirstEpoch < epoch)
    {
        StepOne();
        UpdateSlots(false);
    }
    return ESOLLaneNetworkAdvance::Stepped;
}

//////////////////////////////////////////////////////////////////////////
// Returns the start of epoch k + H
double FSOLLaneNetworkSchedule::GetHorizonEndS() const
{
    return mIsBuilt ? SOLLaneNetwork::LaneEpochSampleTimeS(mFirstEpoch + mHorizon, mConfig.EpochS) : LANESCHED_NAN;
}

//////////////////////////////////////////////////////////////////////////
// Returns the picks of a buffered epoch
TConstArrayView<FSOLPartnerPick> FSOLLaneNetworkSchedule::GetPicks(const int64 epoch) const
{
    const int32 entry = EntryOf(epoch);
    return entry != INDEX_NONE ? TConstArrayView<FSOLPartnerPick>(mPicks).Slice(entry * mNodeCount, mNodeCount)
        : TConstArrayView<FSOLPartnerPick>();
}

//////////////////////////////////////////////////////////////////////////
// Writes the lane set of a buffered epoch, sorted by pair
void FSOLLaneNetworkSchedule::GetLaneSet(const int64 epoch, TArray<FSOLLanePair>& outLanes) const
{
    outLanes.Reset();
    const int32 entry = EntryOf(epoch);
    if (entry == INDEX_NONE)
    {
        return;
    }
    const uint64* words = mSetMask.GetData() + entry * mWordsPerEntry;
    for (int32 pair = 0; pair < mPairCount; ++pair)
    {
        if (LaneSchedBit(words, pair))
        {
            FSOLLanePair& lane = outLanes.AddDefaulted_GetRef();
            lane.NodeA = mPairA[pair];
            lane.NodeB = mPairB[pair];
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a lane is in a buffered epoch's set
bool FSOLLaneNetworkSchedule::IsInSet(const int64 epoch, const FSOLLanePair& lane) const
{
    const int32 entry = EntryOf(epoch);
    const int32 pair = PairIndex(lane);
    return entry != INDEX_NONE && pair != INDEX_NONE && LaneSchedBit(mSetMask.GetData() + entry * mWordsPerEntry, pair);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a lane is open (in the set and Sun-open) at a buffered epoch
bool FSOLLaneNetworkSchedule::IsOpen(const int64 epoch, const FSOLLanePair& lane) const
{
    const int32 entry = EntryOf(epoch);
    const int32 pair = PairIndex(lane);
    return entry != INDEX_NONE && pair != INDEX_NONE
        && LaneSchedBit(mOpenMask.GetData() + entry * mWordsPerEntry, pair);
}

//////////////////////////////////////////////////////////////////////////
// Writes a lane's open flag per buffered epoch, current epoch first
void FSOLLaneNetworkSchedule::GetOpenFlags(const FSOLLanePair& lane, TArray<uint8>& outFlags) const
{
    outFlags.Reset();
    const int32 pair = PairIndex(lane);
    if (!mIsBuilt || pair == INDEX_NONE)
    {
        return;
    }
    outFlags.SetNumUninitialized(mHorizon);
    for (int32 offset = 0; offset < mHorizon; ++offset)
    {
        const int32 entry = (mHead + offset) % mHorizon;
        outFlags[offset] = LaneSchedBit(mOpenMask.GetData() + entry * mWordsPerEntry, pair) ? 1 : 0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a slot's current open window; false when the slot is free or its lane is closed now
bool FSOLLaneNetworkSchedule::GetSlotWindow(const int32 slot, double& outOpenStartS, double& outCloseS) const
{
    outOpenStartS = LANESCHED_NAN;
    outCloseS = LANESCHED_NAN;
    if (!mSlotOpen.IsValidIndex(slot) || mSlotOpen[slot] == 0)
    {
        return false;
    }
    outOpenStartS = mSlotOpenStartS[slot];
    outCloseS = mSlotCloseS[slot];
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Marks a slot held (fading) or not
void FSOLLaneNetworkSchedule::SetSlotHeld(const int32 slot, const bool bHeld)
{
    if (mSlotHeld.IsValidIndex(slot) && mSlotHeld[slot] != (bHeld ? 1 : 0))
    {
        mSlotHeld[slot] = bHeld ? 1 : 0;
        mHasSlotHoldChange = true;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the fastest route at startS over the slot lanes open now (hop Lane = slot), with the horizon rule
FSOLLaneRoute FSOLLaneNetworkSchedule::FindRoute(const int32 sourceNode, const int32 targetNode, const double startS,
    TConstArrayView<FSOLLaneProfile> slotProfiles, const double wallSpeedMps, const double penaltyS)
{
    FSOLLaneRoute route;
    if (!mIsBuilt)
    {
        return route;
    }

    // Edges: every open slot lane with a drivable profile, its direction from the node positions at the start; a
    // close at the horizon end is the buffer's limit, not a known close, so it plans as open, and a leg departing past
    // the horizon uses the lane as open whatever its window (the current set; re-planned at each node)
    const double horizonEndS = GetHorizonEndS();
    mPositionsFn(startS, mPositions);
    mEdges.Reset();
    mEdgeSlot.Reset();
    for (int32 slot = 0; slot < mSlots.Num() && slot < slotProfiles.Num(); ++slot)
    {
        const FSOLLaneProfile& profile = slotProfiles[slot];
        if (mSlotOpen[slot] == 0 || !profile.bDrivable)
        {
            continue;
        }
        const FSOLLanePair& lane = mSlots[slot];
        FSOLLaneEdge& edge = mEdges.AddDefaulted_GetRef();
        edge.NodeA = lane.NodeA;
        edge.NodeB = lane.NodeB;
        edge.TimeS = SOLLaneNetwork::LaneEdgeTimeS(profile);
        edge.OpenStartS = mSlotOpenStartS[slot];
        edge.CloseS = mSlotCloseS[slot] >= horizonEndS ? LANESCHED_POS_INF : mSlotCloseS[slot];
        edge.PlanOpenFromS = horizonEndS;
        edge.DirectionAB = (mPositions[lane.NodeB] - mPositions[lane.NodeA]).GetSafeNormal();
        edge.EndRingDistanceAM = profile.EndArcAM;
        edge.EndRingDistanceBM = profile.EndArcBM;
        mEdgeSlot.Add(slot);
    }
    route = SOLLaneNetwork::FindFastestRoute(mNodeCount, mEdges, penaltyS, wallSpeedMps, startS, sourceNode,
        targetNode);
    for (int32 hop = 0; hop < route.HopCount; ++hop)
    {
        route.Hops[hop].Lane = mEdgeSlot.IsValidIndex(route.Hops[hop].Lane) ? mEdgeSlot[route.Hops[hop].Lane]
            : INDEX_NONE;
    }
    return route;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's pair index in A-major order
int32 FSOLLaneNetworkSchedule::PairIndex(const FSOLLanePair& lane) const
{
    const int32 a = lane.NodeA;
    const int32 b = lane.NodeB;
    if (a < 0 || b <= a || b >= mNodeCount)
    {
        return INDEX_NONE;
    }
    return a * (2 * mNodeCount - a - 1) / 2 + (b - a - 1);
}

//////////////////////////////////////////////////////////////////////////
// Returns the ring slot of a buffered epoch
int32 FSOLLaneNetworkSchedule::EntryOf(const int64 epoch) const
{
    const int64 offset = epoch - mFirstEpoch;
    if (!mIsBuilt || offset < 0 || offset >= mHorizon)
    {
        return INDEX_NONE;
    }
    return static_cast<int32>((mHead + offset) % mHorizon);
}

//////////////////////////////////////////////////////////////////////////
// Evaluates an epoch's start positions into its slot, then its candidates and nearest partners from the distances
void FSOLLaneNetworkSchedule::FillStartSlot(const int64 epoch, const int32 slot, FBuildContext& context)
{
    FVector3d* positions = mStartPositions.GetData() + slot * mNodeCount;
    mPositionsFn(SOLLaneNetwork::LaneEpochSampleTimeS(epoch, mConfig.EpochS), TArrayView<FVector3d>(positions,
        mNodeCount));
    SOLLaneNetwork::FillEpochDistances(TConstArrayView<FVector3d>(positions, mNodeCount), context.Distances);
    context.DistancesEpoch = epoch;
    context.bHasDistances = true;
    SOLLaneNetwork::NearestFromDistances(context.Distances,
        TArrayView<int32>(mStartNearest.GetData() + slot * mNodeCount, mNodeCount),
        TArrayView<double>(mStartNearestM.GetData() + slot * mNodeCount, mNodeCount));
    mStartCandidates[slot] = context.Distances.CandidateMask;
    mStartEpochTag[slot] = epoch;
}

//////////////////////////////////////////////////////////////////////////
// Returns the node positions at an epoch's start, filling its ring slot when not held there
const FVector3d* FSOLLaneNetworkSchedule::StartPositions(const int64 epoch)
{
    const int32 slot = LaneSchedRingSlot(epoch, mStartSlots);
    if (mStartEpochTag[slot] != epoch)
    {
        FillStartSlot(epoch, slot, mBuildContexts[0]);
    }
    return mStartPositions.GetData() + slot * mNodeCount;
}

//////////////////////////////////////////////////////////////////////////
// Returns the start positions of an epoch the ring holds (the rebuild's tasks read them after its first pass)
const FVector3d* FSOLLaneNetworkSchedule::HeldStartPositions(const int64 epoch) const
{
    return mStartPositions.GetData() + LaneSchedRingSlot(epoch, mStartSlots) * mNodeCount;
}

//////////////////////////////////////////////////////////////////////////
// Fills the context's distances for an epoch whose start positions are held, unless they are that epoch's already
void FSOLLaneNetworkSchedule::FillDistances(const int64 epoch, FBuildContext& context) const
{
    if (context.bHasDistances && context.DistancesEpoch == epoch)
    {
        return;
    }
    const int32 slot = LaneSchedRingSlot(epoch, mStartSlots);
    SOLLaneNetwork::FillEpochDistances(TConstArrayView<FVector3d>(mStartPositions.GetData() + slot * mNodeCount,
        mNodeCount), context.Distances);
    context.DistancesEpoch = epoch;
    context.bHasDistances = true;
}

//////////////////////////////////////////////////////////////////////////
// Returns an epoch's inner clearance samples (samples 1 .. EpochSamples - 2), evaluated into its slot when not held
// (touches only that slot, so rebuild tasks prefetch distinct epochs in parallel)
const FVector3d* FSOLLaneNetworkSchedule::InnerSamplePositions(const int64 epoch)
{
    const int32 inner = mConfig.EpochSamples - 2;
    const int32 slot = LaneSchedRingSlot(epoch, mInnerSlots);
    FVector3d* positions = mInnerPositions.GetData() + slot * inner * mNodeCount;
    if (mInnerEpochTag[slot] != epoch)
    {
        const double startS = SOLLaneNetwork::LaneEpochSampleTimeS(epoch, mConfig.EpochS);
        const double spacingS = mConfig.EpochS / static_cast<double>(mConfig.EpochSamples - 1);
        for (int32 sample = 1; sample <= inner; ++sample)
        {
            mPositionsFn(startS + spacingS * sample, TArrayView<FVector3d>(positions + (sample - 1) * mNodeCount,
                mNodeCount));
        }
        mInnerEpochTag[slot] = epoch;
    }
    return positions;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's bound from the clearances at the epoch's two ends and whether it classifies the lane already.
// Every sample lies at least min(d0, dE) - v E / 2 from the Sun (d0, dE the clearances at t_k and t_k+1), so
// min(d0, dE) - v (E + dt) / 2 above the reopen is surely open; the sampled bound is at most min(d0, dE) - v dt / 2,
// so that below the close is surely closed; between them only the inner samples decide
double FSOLLaneNetworkSchedule::EndSampleBoundM(const FVector3d* startM, const FVector3d* endM, const int32 nodeA,
    const int32 nodeB, double& outStartClearanceM, double& outEndClearanceM, bool& bOutDecided) const
{
    const double spacingS = mConfig.EpochS / static_cast<double>(mConfig.EpochSamples - 1);
    const double speedMps = FMath::Max(mConfig.NodeMaxSpeedMps[nodeA], mConfig.NodeMaxSpeedMps[nodeB]);
    outStartClearanceM = SOLLaneNetwork::SunSegmentDistanceM(startM[nodeA], startM[nodeB]);
    outEndClearanceM = SOLLaneNetwork::SunSegmentDistanceM(endM[nodeA], endM[nodeB]);
    const double endsMinM = FMath::Min(outStartClearanceM, outEndClearanceM);
    const double surelyAboveM = endsMinM - speedMps * 0.5 * (mConfig.EpochS + spacingS);
    const double atMostM = endsMinM - speedMps * 0.5 * spacingS;
    bOutDecided = true;
    if (surelyAboveM > SOL::NPC_LANE_SUN_REOPEN_RADII * mConfig.SunRadiusM)
    {
        return surelyAboveM;
    }
    if (atMostM < SOL::NPC_LANE_SUN_CLEARANCE_RADII * mConfig.SunRadiusM)
    {
        return atMostM;
    }
    bOutDecided = false;
    return atMostM;
}

//////////////////////////////////////////////////////////////////////////
// Warms the picks up from k - W with no picks, then fills every entry from k (SDD 53 4.3, the time jump), in four
// passes: the start positions and nearest partners of every epoch it reads (k - W .. k + H) in parallel; the picks in
// order (each one step from the last); each entry's lane set in parallel; the Sun state in order
void FSOLLaneNetworkSchedule::Rebuild(const int64 epoch)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FSOLLaneNetworkSchedule::Rebuild);
    mFirstEpoch = epoch;
    mHead = 0;
    mWarmupStartEpoch = epoch - mConfig.WarmupEpochs;
    mIsBuilt = true;
    const int32 contexts = mBuildContexts.Num();
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(FSOLLaneNetworkSchedule::RebuildPositions);
        const int64 firstRead = mWarmupStartEpoch;
        const int32 readCount = static_cast<int32>(epoch + mHorizon - firstRead + 1);
        ParallelForWithExistingTaskContext(TEXT("LaneNetworkRebuildPositions"), MakeArrayView(mBuildContexts),
            readCount, FMath::DivideAndRoundUp(readCount, contexts),
            [this, firstRead](FBuildContext& context, const int32 index)
            {
                const int64 readEpoch = firstRead + index;
                const int32 slot = LaneSchedRingSlot(readEpoch, mStartSlots);
                if (mStartEpochTag[slot] != readEpoch)
                {
                    FillStartSlot(readEpoch, slot, context);
                }
            });
    }

    // The picks in order: the warm-up from no picks, then each entry one step from the one before
    mWorkPicks.Reset();
    mWorkPicks.SetNum(mNodeCount);
    for (int64 warm = mWarmupStartEpoch; warm < epoch; ++warm)
    {
        StepPicks(warm);
    }
    for (int32 offset = 0; offset < mHorizon; ++offset)
    {
        StepPicks(epoch + offset);
        FMemory::Memcpy(mPicks.GetData() + offset * mNodeCount, mWorkPicks.GetData(),
            sizeof(FSOLPartnerPick) * mNodeCount);
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(FSOLLaneNetworkSchedule::RebuildSets);
        ParallelForWithExistingTaskContext(TEXT("LaneNetworkRebuildSets"), MakeArrayView(mBuildContexts), mHorizon,
            FMath::DivideAndRoundUp(mHorizon, contexts), [this, epoch](FBuildContext& context, const int32 offset)
            {
                BuildSet(epoch + offset, offset, context, true);
            });
    }
    for (int32 offset = 0; offset < mHorizon; ++offset)
    {
        ComputeSun(epoch + offset, offset, offset > 0 ? offset - 1 : INDEX_NONE);
    }
}

//////////////////////////////////////////////////////////////////////////
// Drops the current epoch and computes the new far-end epoch, its picks one step from the previous far end's
void FSOLLaneNetworkSchedule::StepOne()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FSOLLaneNetworkSchedule::StepOne);
    const int32 lastEntry = (mHead + mHorizon - 1) % mHorizon;
    mWorkPicks.Reset();
    mWorkPicks.Append(TConstArrayView<FSOLPartnerPick>(mPicks).Slice(lastEntry * mNodeCount, mNodeCount));
    const int32 freed = mHead;
    mHead = (mHead + 1) % mHorizon;
    ++mFirstEpoch;
    ComputeEntry(mFirstEpoch + mHorizon - 1, freed, lastEntry);
}

//////////////////////////////////////////////////////////////////////////
// Computes one entry: picks, base + partner lanes + island links, and each lane's Sun state (pure look-back)
void FSOLLaneNetworkSchedule::ComputeEntry(const int64 epoch, const int32 entry, const int32 previousEntry)
{
    StartPositions(epoch);
    StepPicks(epoch);
    FMemory::Memcpy(mPicks.GetData() + entry * mNodeCount, mWorkPicks.GetData(),
        sizeof(FSOLPartnerPick) * mNodeCount);
    BuildSet(epoch, entry, mBuildContexts[0], false);
    ComputeSun(epoch, entry, previousEntry);
}

//////////////////////////////////////////////////////////////////////////
// Steps every taking-part node's pick one epoch (StepPartnerPicks' rule): its nearest from the epoch's slot, the held
// partner's distance only while both ends are candidates (the same bits as the distance matrix)
void FSOLLaneNetworkSchedule::StepPicks(const int64 epoch)
{
    const int32 slot = LaneSchedRingSlot(epoch, mStartSlots);
    const FVector3d* positions = mStartPositions.GetData() + slot * mNodeCount;
    const int32* nearest = mStartNearest.GetData() + slot * mNodeCount;
    const double* nearestM = mStartNearestM.GetData() + slot * mNodeCount;
    const uint64 candidates = mStartCandidates[slot];
    const uint64 takesPart = mBuildContexts[0].Distances.TakesPartMask;
    for (int32 node = 0; node < mNodeCount; ++node)
    {
        if (((takesPart >> node) & 1) == 0)
        {
            continue;
        }
        const int32 held = mWorkPicks[node].Partner;
        const bool bHeldMeasured = held >= 0 && held < mNodeCount && held != node && ((candidates >> node) & 1) != 0
            && ((candidates >> held) & 1) != 0;
        const double heldM = bHeldMeasured
            ? (positions[FMath::Max(node, held)] - positions[FMath::Min(node, held)]).Length() : LANESCHED_NAN;
        mWorkPicks[node] = SOLLaneNetwork::StepPartnerPick(mWorkPicks[node], nearest[node], nearestM[node], heldM,
            mConfig.MinStayEpochs, mConfig.SwitchRatio);
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes an entry's lane set: base lanes, the pairs of its picks, then the island links over both (Sun-closed lanes
// included); reads only held positions and the entry's picks, so rebuild tasks run it in parallel
void FSOLLaneNetworkSchedule::BuildSet(const int64 epoch, const int32 entry, FBuildContext& context,
    const bool bParallel)
{
    const FSOLPartnerPick* picks = mPicks.GetData() + entry * mNodeCount;
    context.Lanes.Reset();
    context.Lanes.Append(mConfig.BaseLanes);
    for (int32 node = 0; node < mNodeCount; ++node)
    {
        const int32 partner = picks[node].Partner;
        if (partner >= 0 && partner < mNodeCount && partner != node)
        {
            FSOLLanePair& lane = context.Lanes.AddDefaulted_GetRef();
            lane.NodeA = FMath::Min(node, partner);
            lane.NodeB = FMath::Max(node, partner);
        }
    }
    FillDistances(epoch, context);
    SOLLaneNetwork::IslandLinksFromDistances(context.Distances, context.Lanes, mConfig.RootNode, context.Links);
    context.Lanes.Append(context.Links);
    uint64* setWords = mSetMask.GetData() + entry * mWordsPerEntry;
    FMemory::Memzero(setWords, sizeof(uint64) * mWordsPerEntry);
    for (const FSOLLanePair& lane : context.Lanes)
    {
        const int32 pair = PairIndex(lane);
        if (pair != INDEX_NONE)
        {
            LaneSchedSetBit(setWords, pair);
        }
    }

    // Each lane's clearance class over the epoch: above the reopen, below the close, or (neither) inside the band.
    // The rebuild's tasks read the start positions held by its first pass and evaluate the inner samples of their own
    // epochs only; the serial step may fill the next epoch's start (after the island links used the distances)
    uint64* aboveWords = mAboveMask.GetData() + entry * mWordsPerEntry;
    uint64* belowWords = mBelowMask.GetData() + entry * mWordsPerEntry;
    FMemory::Memzero(aboveWords, sizeof(uint64) * mWordsPerEntry);
    FMemory::Memzero(belowWords, sizeof(uint64) * mWordsPerEntry);
    const FVector3d* start = bParallel ? HeldStartPositions(epoch) : StartPositions(epoch);
    const FVector3d* end = bParallel ? HeldStartPositions(epoch + 1) : StartPositions(epoch + 1);
    const double closeM = SOL::NPC_LANE_SUN_CLEARANCE_RADII * mConfig.SunRadiusM;
    const double reopenM = SOL::NPC_LANE_SUN_REOPEN_RADII * mConfig.SunRadiusM;
    for (int32 word = 0; word < mWordsPerEntry; ++word)
    {
        for (uint64 rest = setWords[word]; rest != 0; rest &= rest - 1)
        {
            const int32 pair = word * 64 + static_cast<int32>(FPlatformMath::CountTrailingZeros64(rest));
            double startClearanceM = 0.0;
            double endClearanceM = 0.0;
            bool bDecided = false;
            double boundM = EndSampleBoundM(start, end, mPairA[pair], mPairB[pair], startClearanceM, endClearanceM,
                bDecided);
            if (!bDecided)
            {
                boundM = SampledBoundM(epoch, mPairA[pair], mPairB[pair], startClearanceM, endClearanceM,
                    context.SampleClearances);
            }
            if (boundM > reopenM)
            {
                LaneSchedSetBit(aboveWords, pair);
            }
            else if (boundM < closeM || FMath::IsNaN(boundM))
            {
                LaneSchedSetBit(belowWords, pair);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes an entry's open lanes and in-band runs from its set, lane by lane (the pure look-back, SDD 53 5.3)
void FSOLLaneNetworkSchedule::ComputeSun(const int64 epoch, const int32 entry, const int32 previousEntry)
{
    const uint64* setWords = mSetMask.GetData() + entry * mWordsPerEntry;
    const uint64* aboveWords = mAboveMask.GetData() + entry * mWordsPerEntry;
    const uint64* belowWords = mBelowMask.GetData() + entry * mWordsPerEntry;
    uint64* openWords = mOpenMask.GetData() + entry * mWordsPerEntry;
    uint8* bandRun = mBandRun.GetData() + entry * mPairCount;
    FMemory::Memzero(bandRun, mPairCount);
    for (int32 word = 0; word < mWordsPerEntry; ++word)
    {
        // Above the reopen: open; below the close: closed; inside the band: carried or looked back, lane by lane
        openWords[word] = aboveWords[word];
        for (uint64 rest = setWords[word] & ~aboveWords[word] & ~belowWords[word]; rest != 0; rest &= rest - 1)
        {
            const int32 pair = word * 64 + static_cast<int32>(FPlatformMath::CountTrailingZeros64(rest));
            uint8 run = 0;
            if (BandLaneOpen(epoch, pair, previousEntry, run))
            {
                LaneSchedSetBit(openWords, pair);
            }
            bandRun[pair] = run;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the Sun state of a lane inside the band and writes its in-band run: carried from the previous epoch while
// the run is under the look-back (the same result as the look-back walk), else the full look-back walk when the
// previous epoch does not hold the lane
bool FSOLLaneNetworkSchedule::BandLaneOpen(const int64 epoch, const int32 pair, const int32 previousEntry,
    uint8& outBandRun)
{
    if (previousEntry != INDEX_NONE && LaneSchedBit(mSetMask.GetData() + previousEntry * mWordsPerEntry, pair))
    {
        outBandRun = static_cast<uint8>(FMath::Min<int32>(mBandRun[previousEntry * mPairCount + pair] + 1,
            mConfig.LookBackEpochs));
        return outBandRun < mConfig.LookBackEpochs
            && LaneSchedBit(mOpenMask.GetData() + previousEntry * mWordsPerEntry, pair);
    }
    return LookBackOpen(epoch, mPairA[pair], mPairB[pair], outBandRun);
}

//////////////////////////////////////////////////////////////////////////
// Returns a lower bound of the lane's Sun clearance over the epoch with the sampled bound's class: the end-sample
// bound when it decides, else EpochClearanceBoundM (min - v dt / 2) over all EpochSamples samples
double FSOLLaneNetworkSchedule::LaneClearanceBoundM(const int64 epoch, const int32 nodeA, const int32 nodeB)
{
    const FVector3d* start = StartPositions(epoch);
    const FVector3d* end = StartPositions(epoch + 1);
    double startClearanceM = 0.0;
    double endClearanceM = 0.0;
    bool bDecided = false;
    const double endsBoundM = EndSampleBoundM(start, end, nodeA, nodeB, startClearanceM, endClearanceM, bDecided);
    if (bDecided)
    {
        return endsBoundM;
    }

    return SampledBoundM(epoch, nodeA, nodeB, startClearanceM, endClearanceM, mSampleClearances);
}

//////////////////////////////////////////////////////////////////////////
// Returns EpochClearanceBoundM over all EpochSamples samples: the two end clearances given, the inner samples read
// (evaluated into the epoch's own slot when not held)
double FSOLLaneNetworkSchedule::SampledBoundM(const int64 epoch, const int32 nodeA, const int32 nodeB,
    const double startClearanceM, const double endClearanceM, TArray<double>& clearancesScratch)
{
    const int32 samples = mConfig.EpochSamples;
    const double spacingS = mConfig.EpochS / static_cast<double>(samples - 1);
    const double speedMps = FMath::Max(mConfig.NodeMaxSpeedMps[nodeA], mConfig.NodeMaxSpeedMps[nodeB]);
    const FVector3d* inner = InnerSamplePositions(epoch);
    clearancesScratch.SetNumUninitialized(samples, EAllowShrinking::No);
    clearancesScratch[0] = startClearanceM;
    for (int32 sample = 1; sample < samples - 1; ++sample)
    {
        const FVector3d* positions = inner + (sample - 1) * mNodeCount;
        clearancesScratch[sample] = SOLLaneNetwork::SunSegmentDistanceM(positions[nodeA], positions[nodeB]);
    }
    clearancesScratch[samples - 1] = endClearanceM;
    return SOLLaneNetwork::EpochClearanceBoundM(clearancesScratch, speedMps, spacingS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the lane's Sun state at an in-band epoch by walking back (SunStateFromLookBack's rule, stopping at the
// first epoch outside the band) and writes the in-band run length
bool FSOLLaneNetworkSchedule::LookBackOpen(const int64 epoch, const int32 nodeA, const int32 nodeB, uint8& outBandRun)
{
    const double closeM = SOL::NPC_LANE_SUN_CLEARANCE_RADII * mConfig.SunRadiusM;
    const double reopenM = SOL::NPC_LANE_SUN_REOPEN_RADII * mConfig.SunRadiusM;
    int32 run = 1;
    for (int32 back = 1; back < mConfig.LookBackEpochs; ++back)
    {
        const double boundM = LaneClearanceBoundM(epoch - back, nodeA, nodeB);
        if (boundM < closeM || FMath::IsNaN(boundM))
        {
            outBandRun = static_cast<uint8>(run);
            return false;
        }
        if (boundM > reopenM)
        {
            outBandRun = static_cast<uint8>(run);
            return true;
        }
        ++run;
    }
    outBandRun = static_cast<uint8>(mConfig.LookBackEpochs);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Re-assigns the current epoch's slots and their latched windows (a jump latches -inf, SDD 53 5.4)
void FSOLLaneNetworkSchedule::UpdateSlots(const bool bJump)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FSOLLaneNetworkSchedule::UpdateSlots);

    // The slots stand while the set and the held flags are those of the last assignment (no jump); otherwise the
    // current partner lanes and island links (not base), sorted, are assigned again
    const uint64* setWords = mSetMask.GetData() + mHead * mWordsPerEntry;
    const uint64* openWords = mOpenMask.GetData() + mHead * mWordsPerEntry;
    const bool bSlotsStand = !bJump && !mHasSlotHoldChange && mSlotSetWords.Num() == mWordsPerEntry
        && FMemory::Memcmp(mSlotSetWords.GetData(), setWords, sizeof(uint64) * mWordsPerEntry) == 0;
    if (bSlotsStand)
    {
        mNewSlots.Reset();
        mNewSlots.Append(mSlots);
    }
    else
    {
        AssignSlots(setWords);
    }

    // Windows: a lane open at k - 1 and still open keeps its start and extends its close by one epoch when the close
    // was the buffer's end; anything else (a new lane, a reopen, a jump) runs LaneOpenWindow on the flags
    UpdateSlotWindows(bJump, openWords);
    Swap(mSlots, mNewSlots);
}

//////////////////////////////////////////////////////////////////////////
// Assigns the slots for the current set into mNewSlots (AssignLaneSlots) and reports a change of a non-zero overflow
void FSOLLaneNetworkSchedule::AssignSlots(const uint64* setWords)
{
    mSlotSetWords.Reset();
    mSlotSetWords.Append(setWords, mWordsPerEntry);
    mHasSlotHoldChange = false;
    mPartnerSet.Reset();
    for (int32 pair = 0; pair < mPairCount; ++pair)
    {
        if (mIsBasePair[pair] == 0 && LaneSchedBit(setWords, pair))
        {
            FSOLLanePair& lane = mPartnerSet.AddDefaulted_GetRef();
            lane.NodeA = mPairA[pair];
            lane.NodeB = mPairB[pair];
        }
    }
    mPreviousSlots.Reset();
    mPreviousSlots.Append(mSlots);
    const int32 overflow = SOLLaneNetwork::AssignLaneSlots(mPreviousSlots, mSlotHeld, mPartnerSet,
        mConfig.BaseLanes.Num(), mConfig.SlotCapacity, mNewSlots);
    if (overflow > 0 && SOLLaneNetwork::StepOverflowReport(mSlotOverflow, overflow))
    {
        UE_LOG(LogSOL, Warning,
            TEXT("Lane network: %d lane(s) found no group column at epoch %lld (%d columns for lanes)"), overflow,
            mFirstEpoch, mConfig.SlotCapacity);
        ++mSlotOverflowReports;
    }
    mSlotOverflow = overflow;
}

//////////////////////////////////////////////////////////////////////////
// Updates each new slot's latched window from the current open lanes (a jump latches -inf, SDD 53 5.4)
void FSOLLaneNetworkSchedule::UpdateSlotWindows(const bool bJump, const uint64* openWords)
{
    const double nowS = SOLLaneNetwork::LaneEpochSampleTimeS(mFirstEpoch, mConfig.EpochS);
    const double previousEndS = SOLLaneNetwork::LaneEpochSampleTimeS(mFirstEpoch - 1 + mHorizon, mConfig.EpochS);
    const int32 lastEntry = (mHead + mHorizon - 1) % mHorizon;
    const uint64* lastOpenWords = mOpenMask.GetData() + lastEntry * mWordsPerEntry;
    for (int32 slot = 0; slot < mNewSlots.Num(); ++slot)
    {
        const FSOLLanePair& lane = mNewSlots[slot];
        const int32 pair = PairIndex(lane);
        const bool bWasOpen = !bJump && mSlotOpen[slot] != 0 && LaneSchedSamePair(mSlots[slot], lane);
        if (pair == INDEX_NONE || !LaneSchedBit(openWords, pair))
        {
            mSlotOpen[slot] = 0;
            mSlotOpenStartS[slot] = LANESCHED_NAN;
            mSlotCloseS[slot] = LANESCHED_NAN;
            continue;
        }
        if (bWasOpen && nowS < mSlotCloseS[slot])
        {
            if (mSlotCloseS[slot] == previousEndS && LaneSchedBit(lastOpenWords, pair))
            {
                mSlotCloseS[slot] = SOLLaneNetwork::LaneEpochSampleTimeS(mFirstEpoch + mHorizon, mConfig.EpochS);
            }
            continue;
        }
        const double latchS = bJump ? -LANESCHED_POS_INF : (bWasOpen ? mSlotOpenStartS[slot] : LANESCHED_NAN);
        GetOpenFlags(lane, mFlagScratch);
        double openStartS = LANESCHED_NAN;
        double closeS = LANESCHED_NAN;
        const bool bOpen = SOLLaneNetwork::LaneOpenWindow(mFlagScratch, mFirstEpoch, mConfig.EpochS, nowS, latchS,
            openStartS, closeS);
        mSlotOpen[slot] = bOpen ? 1 : 0;
        mSlotOpenStartS[slot] = openStartS;
        mSlotCloseS[slot] = closeS;
    }
}
