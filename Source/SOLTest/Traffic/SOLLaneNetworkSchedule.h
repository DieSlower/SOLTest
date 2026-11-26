/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"

#include "CoreMinimal.h"

// Writes every lane node's Sun-frame position (universe ecliptic m) at a sim time in seconds since J2000. It must be a
// pure function of the time (const reads only): the rebuild calls it from worker threads
using FSOLLaneNodePositionsFn = TFunction<void(double /*secondsSinceJ2000*/, TArrayView<FVector3d> /*outPositionsM*/)>;

// The settings of one lane network schedule (SDD 53 4.3, 5.3); the defaults are the SOLConstants.h values
struct FSOLLaneNetworkConfig
{
    TArray<FSOLLaneNode> Nodes;
    TArray<double> NodeMaxSpeedMps;                 // Per node, the speed bound of the Sun clearance (5.3)
    TArray<FSOLLanePair> BaseLanes;                 // Sorted pairs, in slot order (slots 0 .. BaseLanes.Num() - 1)
    int32 RootNode = INDEX_NONE;                    // The island links' root (Earth, O2)
    int32 SlotCapacity = SOL::NPC_GROUP_TEXTURE_WIDTH;   // Group columns the lanes may hold (64 - clusters - battles)
    int32 HorizonEpochs = SOL::NPC_LANE_NETWORK_HORIZON_EPOCHS;
    int32 WarmupEpochs = SOL::NPC_LANE_PARTNER_WARMUP_EPOCHS;
    int32 MinStayEpochs = SOL::NPC_LANE_PARTNER_MIN_STAY_EPOCHS;
    double SwitchRatio = SOL::NPC_LANE_PARTNER_SWITCH_RATIO;
    int32 LookBackEpochs = SOL::NPC_LANE_SUN_LOOKBACK_EPOCHS;
    int32 EpochSamples = SOL::NPC_LANE_NETWORK_EPOCH_SAMPLES;
    int32 CatchUpEpochs = SOL::NPC_LANE_NETWORK_CATCH_UP_EPOCHS;
    double EpochS = SOL::NPC_LANE_NETWORK_EPOCH_S;
    double SunRadiusM = SOL::SUN_RADIUS_M;
};

// What one Advance call did
enum class ESOLLaneNetworkAdvance : uint8
{
    None,       // Same epoch (or not configured)
    Stepped,    // Moved forward one epoch at a time (at most the catch-up limit)
    Rebuilt     // A time jump: warm-up and full rebuild
};

/**
 * The lane network's per-epoch state (SDD 53 4.3-4.5, 5.3, 5.4), a plain class owned by USOLLaneTrafficSubsystem. A
 * ring buffer holds the H epochs k .. k + H - 1 (H = the horizon); each entry keeps the epoch's partner picks, its lane
 * set and its open lanes (in the set and Sun-open) as pair bitmasks. Each forward epoch steps the picks of the new
 * far-end epoch from the one before it (one shared distance matrix for the picks and the island links); a time jump
 * warms the picks up from k - W and rebuilds, the node positions of every epoch it reads evaluated in parallel first.
 * Node positions at each epoch start are kept in a ring covering k - max(W, L) .. k + H (L = the Sun look-back), so the
 * end sample of epoch e is the start of e + 1 and the look-back re-reads them; the inner clearance samples of an epoch
 * are taken only for a lane whose end-sample bound cannot classify it, and kept for the look-back. The Sun state is
 * the pure look-back (SunStateFromLookBack). The current epoch also carries the lane slots (AssignLaneSlots), each
 * slot's latched open window (LaneOpenWindow, kept incrementally) and the route query. Node positions come from the
 * caller's function, so Traffic/ reads neither the registry nor the belt. Nothing is saved.
 */
class SOLTEST_API FSOLLaneNetworkSchedule
{
public:

    // Takes the nodes, base lanes and limits and the node position function; the schedule is built on the next Advance
    void Configure(const FSOLLaneNetworkConfig& config, FSOLLaneNodePositionsFn positionsFn);

    // Forgets the configuration and every epoch (no population)
    void Reset();

    // Returns whether a configuration is held
    bool IsConfigured() const { return mIsConfigured; }

    // Makes the next Advance a time jump (a clock set within the catch-up limit still rebuilds)
    void MarkTimeJump() { mHasJumpPending = true; }

    // Brings the schedule to the epoch of simTimeS: one step per epoch for a forward move of at most the catch-up
    // limit, otherwise (backward, larger, first call or a marked jump) a warm-up and rebuild
    ESOLLaneNetworkAdvance Advance(double simTimeS);

    // Returns the current epoch k (the buffer's first entry); 0 before the first Advance
    int64 GetCurrentEpoch() const { return mFirstEpoch; }

    // Returns the epoch the last rebuild's warm-up started from (k - W at the jump)
    int64 GetWarmupStartEpoch() const { return mWarmupStartEpoch; }

    // Returns whether the schedule holds epochs
    bool IsBuilt() const { return mIsBuilt; }

    // Returns the configuration
    const FSOLLaneNetworkConfig& GetConfig() const { return mConfig; }

    // Returns the end of the look-ahead, the start of epoch k + H (a lane open through the buffer closes there)
    double GetHorizonEndS() const;

    // Returns the picks of a buffered epoch (empty view outside the buffer)
    TConstArrayView<FSOLPartnerPick> GetPicks(int64 epoch) const;

    // Writes the lane set of a buffered epoch, sorted by pair (empty outside the buffer)
    void GetLaneSet(int64 epoch, TArray<FSOLLanePair>& outLanes) const;

    // Returns whether a lane is in a buffered epoch's set
    bool IsInSet(int64 epoch, const FSOLLanePair& lane) const;

    // Returns whether a lane is open (in the set and Sun-open) at a buffered epoch
    bool IsOpen(int64 epoch, const FSOLLanePair& lane) const;

    // Writes a lane's open flag per buffered epoch, current epoch first (LaneOpenWindow's input)
    void GetOpenFlags(const FSOLLanePair& lane, TArray<uint8>& outFlags) const;

    // Returns the current lane slots (base lanes first; a free slot is {INDEX_NONE, INDEX_NONE})
    TConstArrayView<FSOLLanePair> GetSlots() const { return mSlots; }

    // Returns a slot's current open window; false (NaN outputs) when the slot is free or its lane is closed now
    bool GetSlotWindow(int32 slot, double& outOpenStartS, double& outCloseS) const;

    // Sets a slot's hold (it owns blocks, or only its tunnel still fades, 53d) so AssignLaneSlots keeps it after its
    // lane leaves the set; a Fade hold is pre-emptible by a new lane when no slot is free
    void SetSlotHold(int32 slot, ESOLLaneSlotHold hold);

    // Returns the partner lanes and island links of the current epoch that found no slot (0 = none)
    int32 GetSlotOverflow() const { return mSlotOverflow; }

    // Returns how many times a slot overflow change was reported (the stat counter)
    int32 GetSlotOverflowReports() const { return mSlotOverflowReports; }

    // Returns the fastest route from source to target at startS (current epoch) over the slot lanes open now; a hop's
    // Lane is the slot index. slotProfiles is indexed by slot (a lane without a drivable profile is not an edge). The
    // horizon rule (SDD 53 6.3, R11): a window open through the horizon end counts as having no close, so legs past
    // the horizon use the current lane set; the route is re-planned at each node, when the real windows are known
    // Note: FindRoute uses member scratch buffers, so it is game-thread only and not reentrant
    FSOLLaneRoute FindRoute(int32 sourceNode, int32 targetNode, double startS,
        TConstArrayView<FSOLLaneProfile> slotProfiles, double wallSpeedMps, double penaltyS);

private:

    // Returns a lane's pair index (A-major order), or INDEX_NONE for an invalid pair
    int32 PairIndex(const FSOLLanePair& lane) const;

    // Returns the ring slot of a buffered epoch, or INDEX_NONE
    int32 EntryOf(int64 epoch) const;

    // One parallel task's scratch in a rebuild (the serial path uses the first): an epoch's distances and lane lists
    struct FBuildContext
    {
        FSOLLaneEpochDistances Distances;
        int64 DistancesEpoch = 0;
        bool bHasDistances = false;
        TArray<FSOLLanePair> Lanes;
        TArray<FSOLLanePair> Links;
        TArray<double> SampleClearances;
    };

    // Evaluates an epoch's start positions into its ring slot with the candidates and each node's nearest partner
    // (touches only that slot and the context, so rebuild tasks run it in parallel)
    void FillStartSlot(int64 epoch, int32 slot, FBuildContext& context);

    // Returns the node positions at an epoch's start from the position ring, filling the slot when not held
    const FVector3d* StartPositions(int64 epoch);

    // Returns the start positions of an epoch the ring holds (no fill; the rebuild's tasks)
    const FVector3d* HeldStartPositions(int64 epoch) const;

    // Fills the context's distances for an epoch whose start positions are held (kept while the epoch is the same)
    void FillDistances(int64 epoch, FBuildContext& context) const;

    // Returns an epoch's inner clearance samples (EpochSamples - 2 rows of nodes), evaluating them when not held
    const FVector3d* InnerSamplePositions(int64 epoch);

    // Warms the picks up from k - W and fills every entry from k
    void Rebuild(int64 epoch);

    // Drops the current epoch and computes the new far-end one
    void StepOne();

    // Computes one entry from the picks in mWorkPicks (already the previous epoch's) and the previous entry, if any
    void ComputeEntry(int64 epoch, int32 entry, int32 previousEntry);

    // Steps mWorkPicks one epoch from the epoch's held nearest partners (the hysteresis rule, StepPartnerPick)
    void StepPicks(int64 epoch);

    // Writes an entry's lane set (base lanes, its picks' pairs and the island links) from its stored picks and each
    // lane's clearance class (above the reopen, below the close, or in the band); bParallel: a rebuild task
    void BuildSet(int64 epoch, int32 entry, FBuildContext& context, bool bParallel);

    // Writes an entry's open lanes and in-band runs from its classes and the previous entry's (serial, in order)
    void ComputeSun(int64 epoch, int32 entry, int32 previousEntry);

    // Returns a lane's bound from an epoch's two end samples, its two clearances, and whether that bound decides
    double EndSampleBoundM(const FVector3d* startM, const FVector3d* endM, int32 nodeA, int32 nodeB,
        double& outStartClearanceM, double& outEndClearanceM, bool& bOutDecided) const;

    // Returns the Sun state of a lane inside the band (carried, or the look-back) and writes its in-band run
    bool BandLaneOpen(int64 epoch, int32 pair, int32 previousEntry, uint8& outBandRun);

    // Returns EpochClearanceBoundM over all samples of an epoch from its two end clearances and its inner samples
    double SampledBoundM(int64 epoch, int32 nodeA, int32 nodeB, double startClearanceM, double endClearanceM,
        TArray<double>& clearancesScratch);

    // Returns a lower bound of a lane's Sun clearance over an epoch that classifies it like the sampled bound (below
    // the close, inside the band, above the reopen); the inner samples are read only when the end samples cannot
    double LaneClearanceBoundM(int64 epoch, int32 nodeA, int32 nodeB);

    // Returns a lane's Sun state at an in-band epoch by the full lazy look-back, and its in-band run length
    bool LookBackOpen(int64 epoch, int32 nodeA, int32 nodeB, uint8& outBandRun);

    // Re-assigns the slots and their windows for the current epoch (bJump: latch -inf)
    void UpdateSlots(bool bJump);

    // Assigns the slots of the current set into mNewSlots and counts a non-zero overflow change
    void AssignSlots(const uint64* setWords);

    // Updates the new slots' latched windows from the current open lanes (bJump: latch -inf)
    void UpdateSlotWindows(bool bJump, const uint64* openWords);

    FSOLLaneNetworkConfig mConfig;
    FSOLLaneNodePositionsFn mPositionsFn;
    bool mIsConfigured = false;
    bool mIsBuilt = false;
    bool mHasJumpPending = false;
    int32 mNodeCount = 0;
    int32 mPairCount = 0;
    int32 mWordsPerEntry = 0;
    int32 mHorizon = 0;
    int32 mHead = 0;                        // Ring slot of the current epoch
    int64 mFirstEpoch = 0;
    int64 mWarmupStartEpoch = 0;
    TArray<int32> mPairA;                   // Per pair index, its two nodes
    TArray<int32> mPairB;
    TArray<uint8> mIsBasePair;              // Per pair index

    // The ring buffer, per entry: picks, the lane set and open lanes as bitmasks, and the in-band run per pair (the
    // number of consecutive epochs, up to the look-back, whose clearance bound lies inside the 20-22 band)
    TArray<FSOLPartnerPick> mPicks;
    TArray<uint64> mSetMask;
    TArray<uint64> mOpenMask;
    TArray<uint64> mAboveMask;              // Per entry, the set lanes whose bound is above the reopen...
    TArray<uint64> mBelowMask;              // ...and below the close (the rest of the set is inside the band)
    TArray<uint8> mBandRun;

    // Node positions per epoch start and per epoch's inner samples (direct-mapped rings tagged by epoch, each holding
    // k - max(W, L) .. k + H: about 0.6 MB and 1.8 MB for the v1 network)
    TArray<FVector3d> mStartPositions;      // mStartSlots x nodes
    TArray<int32> mStartNearest;            // mStartSlots x nodes: each node's nearest partner at the epoch start
    TArray<double> mStartNearestM;          // ...and its distance
    TArray<uint64> mStartCandidates;        // Per slot, the candidate nodes (taking part, finite position)
    TArray<int64> mStartEpochTag;
    int32 mStartSlots = 0;
    TArray<FVector3d> mInnerPositions;      // mInnerSlots x (EpochSamples - 2) x nodes
    TArray<int64> mInnerEpochTag;
    int32 mInnerSlots = 0;

    // Current-epoch slot state
    TArray<FSOLLanePair> mSlots;
    TArray<uint8> mSlotHeld;
    TArray<uint8> mSlotOpen;
    TArray<double> mSlotOpenStartS;
    TArray<double> mSlotCloseS;
    int32 mSlotOverflow = 0;
    int32 mSlotOverflowReports = 0;
    bool mHasSlotHoldChange = false;        // A held flag changed since the last assignment
    TArray<uint64> mSlotSetWords;           // The set of the last assignment

    // Scratch reused by every step (no allocation once grown)
    TArray<FBuildContext> mBuildContexts;   // One per rebuild task, the first also the serial path's
    TArray<FSOLPartnerPick> mWorkPicks;
    TArray<FVector3d> mPositions;           // The route query's positions
    TArray<double> mSampleClearances;
    TArray<FSOLLanePair> mPartnerSet;
    TArray<FSOLLanePair> mNewSlots;
    TArray<FSOLLanePair> mPreviousSlots;
    TArray<uint8> mFlagScratch;
    TArray<FSOLLaneEdge> mEdges;
    TArray<int32> mEdgeSlot;
};
