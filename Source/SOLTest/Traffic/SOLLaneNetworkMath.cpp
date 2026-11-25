/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneNetworkMath.h"

#include <cmath>

// The lane network's pure math (SDD 53, contract Docs/Plans/53-lane-network-contract.md). Every function is a pure
// function of its arguments: no hidden state, no heap allocation beyond the caller's output arrays up to the v1 node
// count (the per-call scratch uses inline allocators, the router fixed arrays), double precision throughout and
// deterministic order (ties always go to the lower index or the lower sorted pair)

namespace
{
    constexpr double LANEGRAPH_INF = std::numeric_limits<double>::infinity();
    constexpr double LANEGRAPH_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double LANEGRAPH_EPOCH_INDEX_LIMIT = 4.0e18;                 // |epoch index| clamp (inside int64)
    constexpr int32 LANEGRAPH_INLINE_NODES = 64;                           // Per-node scratch held inline (no heap
                                                                           // allocation up to this many nodes)
    constexpr int32 LANEGRAPH_MASK_NODES = 64;                             // Base-link bit masks cover this many nodes
    constexpr int32 LANEGRAPH_INLINE_MATRIX_NODES = 24;                    // Distance matrix inline up to the v1 nodes
    constexpr int32 LANEGRAPH_MAX_EDGES = SOL::NPC_GROUP_TEXTURE_WIDTH;    // Router edge cap: one lane per group
                                                                           // column at most (SDD 53 6.3, 64 slots)
    constexpr int32 LANEGRAPH_MAX_STATES = 2 * LANEGRAPH_MAX_EDGES + 1;    // Source + (lane, direction) arrivals
    constexpr int32 LANEGRAPH_SOURCE_STATE = 0;                            // The router's start state (no lane)

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of a vector is finite
    bool LaneGraphIsFinite(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a node kind takes part in the partner picks and island links (planets, dwarfs, asteroids)
    bool LaneGraphTakesPart(const ESOLLaneNodeKind kind)
    {
        return kind == ESOLLaneNodeKind::Planet || kind == ESOLLaneNodeKind::DwarfPlanet
            || kind == ESOLLaneNodeKind::Asteroid;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a node takes part and has a finite position (positions must cover the node)
    bool LaneGraphCandidate(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        const int32 node)
    {
        return LaneGraphTakesPart(nodes[node].Kind) && LaneGraphIsFinite(positionsM[node]);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a base lane joins a and b (either stored order)
    bool LaneGraphBaseLinked(TConstArrayView<FSOLLanePair> baseLanes, const int32 a, const int32 b)
    {
        for (const FSOLLanePair& lane : baseLanes)
        {
            if ((lane.NodeA == a && lane.NodeB == b) || (lane.NodeA == b && lane.NodeB == a))
            {
                return true;
            }
        }
        return false;
    }

    // One call's partner-pick scratch: each node's base-linked partners as a bit mask (built once per call when the
    // nodes fit the mask) and the symmetric candidate distance matrix of one epoch (NaN where an end is no candidate)
    struct FLaneGraphPickScratch
    {
        TArray<uint64, TInlineAllocator<LANEGRAPH_INLINE_NODES>> BaseMask;
        TArray<double, TInlineAllocator<LANEGRAPH_INLINE_MATRIX_NODES * LANEGRAPH_INLINE_MATRIX_NODES>> DistanceM;
        int32 NodeCount = 0;
        bool bHasMask = false;
    };

    //////////////////////////////////////////////////////////////////////////
    // Builds the scratch's base-link masks (left empty, scanned instead, above LANEGRAPH_MASK_NODES nodes)
    void LaneGraphBuildBaseMasks(TConstArrayView<FSOLLanePair> baseLanes, const int32 nodeCount,
        FLaneGraphPickScratch& scratch)
    {
        scratch.NodeCount = nodeCount;
        scratch.bHasMask = nodeCount <= LANEGRAPH_MASK_NODES;
        if (!scratch.bHasMask)
        {
            return;
        }
        scratch.BaseMask.Init(0, nodeCount);
        for (const FSOLLanePair& lane : baseLanes)
        {
            // A lane naming no node matches nothing, as in the scan
            if (lane.NodeA >= 0 && lane.NodeA < nodeCount && lane.NodeB >= 0 && lane.NodeB < nodeCount)
            {
                scratch.BaseMask[lane.NodeA] |= uint64(1) << lane.NodeB;
                scratch.BaseMask[lane.NodeB] |= uint64(1) << lane.NodeA;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the scratch's distance matrix for one epoch's positions (each pair once, the same value both ways)
    void LaneGraphFillDistances(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        FLaneGraphPickScratch& scratch)
    {
        const int32 count = scratch.NodeCount;
        scratch.DistanceM.Init(LANEGRAPH_NAN, count * count);
        for (int32 a = 0; a < count; ++a)
        {
            if (!LaneGraphCandidate(nodes, positionsM, a))
            {
                continue;
            }
            for (int32 b = a + 1; b < count; ++b)
            {
                if (LaneGraphCandidate(nodes, positionsM, b))
                {
                    const double distance = (positionsM[b] - positionsM[a]).Length();
                    scratch.DistanceM[a * count + b] = distance;
                    scratch.DistanceM[b * count + a] = distance;
                }
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a base lane joins a and b, from the masks when built, else by scanning the base lanes
    bool LaneGraphScratchBaseLinked(const FLaneGraphPickScratch& scratch, TConstArrayView<FSOLLanePair> baseLanes,
        const int32 a, const int32 b)
    {
        return scratch.bHasMask ? ((scratch.BaseMask[a] >> b) & 1) != 0 : LaneGraphBaseLinked(baseLanes, a, b);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one node's nearest taking-part partner not base-linked to it from the filled scratch, or INDEX_NONE
    int32 LaneGraphNearestOf(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> baseLanes, const FLaneGraphPickScratch& scratch, const int32 node,
        double& outDistanceM)
    {
        // Ties go to the lower index; a non-candidate other has a NaN distance and never compares below
        outDistanceM = LANEGRAPH_NAN;
        if (!LaneGraphCandidate(nodes, positionsM, node))
        {
            return INDEX_NONE;
        }
        int32 best = INDEX_NONE;
        double bestDistance = LANEGRAPH_INF;
        const double* row = scratch.DistanceM.GetData() + node * scratch.NodeCount;
        for (int32 other = 0; other < scratch.NodeCount; ++other)
        {
            const double distance = row[other];
            if (other == node || !(distance < bestDistance)
                || LaneGraphScratchBaseLinked(scratch, baseLanes, node, other))
            {
                continue;
            }
            bestDistance = distance;
            best = other;
        }
        if (best != INDEX_NONE)
        {
            outDistanceM = bestDistance;
        }
        return best;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the sorted pair of two node indices
    FSOLLanePair LaneGraphSortedPair(const int32 a, const int32 b)
    {
        FSOLLanePair pair;
        pair.NodeA = FMath::Min(a, b);
        pair.NodeB = FMath::Max(a, b);
        return pair;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether pair a sorts before pair b by (NodeA, NodeB)
    bool LaneGraphPairLess(const FSOLLanePair& a, const FSOLLanePair& b)
    {
        return a.NodeA != b.NodeA ? a.NodeA < b.NodeA : a.NodeB < b.NodeB;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two pairs are the same lane
    bool LaneGraphPairEqual(const FSOLLanePair& a, const FSOLLanePair& b)
    {
        return a.NodeA == b.NodeA && a.NodeB == b.NodeB;
    }

    //////////////////////////////////////////////////////////////////////////
    // Sorts a pair list in place by (NodeA, NodeB) and drops duplicates (no reallocation)
    void LaneGraphSortUnique(TArray<FSOLLanePair>& pairs)
    {
        pairs.Sort([](const FSOLLanePair& a, const FSOLLanePair& b) { return LaneGraphPairLess(a, b); });
        int32 write = 0;
        for (int32 read = 0; read < pairs.Num(); ++read)
        {
            if (write == 0 || !LaneGraphPairEqual(pairs[write - 1], pairs[read]))
            {
                pairs[write++] = pairs[read];
            }
        }
        pairs.SetNum(write, EAllowShrinking::No);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the union-find root of a node, halving the path on the way
    int32 LaneGraphFind(TArray<int32, TInlineAllocator<LANEGRAPH_INLINE_NODES>>& parent, int32 node)
    {
        while (parent[node] != node)
        {
            parent[node] = parent[parent[node]];
            node = parent[node];
        }
        return node;
    }

    //////////////////////////////////////////////////////////////////////////
    // Joins the union-find sets of a and b (the lower root becomes the root, so the result is order-independent)
    void LaneGraphUnion(TArray<int32, TInlineAllocator<LANEGRAPH_INLINE_NODES>>& parent, const int32 a, const int32 b)
    {
        const int32 rootA = LaneGraphFind(parent, a);
        const int32 rootB = LaneGraphFind(parent, b);
        if (rootA != rootB)
        {
            parent[FMath::Max(rootA, rootB)] = FMath::Min(rootA, rootB);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a share weight: the weight when finite and positive, else 0
    double LaneGraphShareWeight(const double weight)
    {
        return (FMath::IsFinite(weight) && weight > 0.0) ? weight : 0.0;
    }

    // The router's per-call scratch: fixed arrays over the states (source, then each lane's two arrival directions),
    // an indexed binary heap keyed on (elapsed, hops) with decrease-key, so the heap never holds a state twice
    struct FLaneGraphRouter
    {
        double ElapsedS[LANEGRAPH_MAX_STATES];      // Best elapsed time from the start (sum of edge and transfer times)
        int32 Hops[LANEGRAPH_MAX_STATES];
        int32 Parent[LANEGRAPH_MAX_STATES];         // Previous state on the best path (INDEX_NONE at the source)
        int32 HeapPos[LANEGRAPH_MAX_STATES];        // Heap slot, LANEGRAPH_NOT_QUEUED or LANEGRAPH_SETTLED
        int32 Heap[LANEGRAPH_MAX_STATES];
        int32 HeapCount = 0;

        static constexpr int32 LANEGRAPH_NOT_QUEUED = -1;
        static constexpr int32 LANEGRAPH_SETTLED = -2;

        //////////////////////////////////////////////////////////////////////////
        // Returns whether state a's key (elapsed, hops) is below state b's
        bool KeyLess(const int32 a, const int32 b) const
        {
            return ElapsedS[a] != ElapsedS[b] ? ElapsedS[a] < ElapsedS[b] : Hops[a] < Hops[b];
        }

        //////////////////////////////////////////////////////////////////////////
        // Moves the heap entry at slot up until its parent's key is not above it
        void SiftUp(int32 slot)
        {
            while (slot > 0)
            {
                const int32 up = (slot - 1) / 2;
                if (!KeyLess(Heap[slot], Heap[up]))
                {
                    break;
                }
                Swap(slot, up);
                slot = up;
            }
        }

        //////////////////////////////////////////////////////////////////////////
        // Moves the heap entry at slot down until no child's key is below it
        void SiftDown(int32 slot)
        {
            for (;;)
            {
                const int32 left = 2 * slot + 1;
                const int32 right = left + 1;
                int32 smallest = slot;
                if (left < HeapCount && KeyLess(Heap[left], Heap[smallest]))
                {
                    smallest = left;
                }
                if (right < HeapCount && KeyLess(Heap[right], Heap[smallest]))
                {
                    smallest = right;
                }
                if (smallest == slot)
                {
                    break;
                }
                Swap(slot, smallest);
                slot = smallest;
            }
        }

        //////////////////////////////////////////////////////////////////////////
        // Swaps two heap slots and updates both states' slot indices
        void Swap(const int32 a, const int32 b)
        {
            const int32 stateA = Heap[a];
            Heap[a] = Heap[b];
            Heap[b] = stateA;
            HeapPos[Heap[a]] = a;
            HeapPos[Heap[b]] = b;
        }

        //////////////////////////////////////////////////////////////////////////
        // Queues a state, or moves it up after its key dropped
        void PushOrDecrease(const int32 state)
        {
            if (HeapPos[state] == LANEGRAPH_NOT_QUEUED)
            {
                Heap[HeapCount] = state;
                HeapPos[state] = HeapCount;
                ++HeapCount;
            }
            SiftUp(HeapPos[state]);
        }

        //////////////////////////////////////////////////////////////////////////
        // Removes and returns the state with the lowest key, marking it settled
        int32 PopMin()
        {
            const int32 top = Heap[0];
            --HeapCount;
            if (HeapCount > 0)
            {
                Heap[0] = Heap[HeapCount];
                HeapPos[Heap[0]] = 0;
                SiftDown(0);
            }
            HeapPos[top] = LANEGRAPH_SETTLED;
            return top;
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns whether state a's lane sequence is lexicographically below state b's (equal hop counts)
        bool SequenceLess(int32 a, int32 b) const
        {
            // Walk both parent chains back in lockstep: the last difference seen is the first position that differs
            bool bLess = false;
            while (a != b && a != INDEX_NONE && b != INDEX_NONE)
            {
                const int32 laneA = (a - 1) / 2;
                const int32 laneB = (b - 1) / 2;
                if (laneA != laneB)
                {
                    bLess = laneA < laneB;
                }
                a = Parent[a];
                b = Parent[b];
            }
            return bLess;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the node a router state stands at: the source, or the far end of its arriving lane
    int32 LaneGraphStateNode(TConstArrayView<FSOLLaneEdge> edges, const int32 state, const int32 sourceNode)
    {
        if (state == LANEGRAPH_SOURCE_STATE)
        {
            return sourceNode;
        }
        const FSOLLaneEdge& edge = edges[(state - 1) / 2];
        return ((state - 1) % 2 == 0) ? edge.NodeB : edge.NodeA;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes a lane's outward direction (away from the node) at one end and returns its end-ring distance there
    double LaneGraphOutwardAt(const FSOLLaneEdge& edge, const bool bAtNodeA, FVector3d& outDirection)
    {
        outDirection = bAtNodeA ? edge.DirectionAB : -edge.DirectionAB;
        return bAtNodeA ? edge.EndRingDistanceAM : edge.EndRingDistanceBM;
    }
}

namespace SOLLaneNetwork
{
    //////////////////////////////////////////////////////////////////////////
    // Writes the sorted, de-duplicated pairs of each taking-part node's nearest non-base-linked partner (no hysteresis)
    void NextClosestPairs(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> baseLanes, TArray<FSOLLanePair>& outNew)
    {
        outNew.Reset();
        if (positionsM.Num() < nodes.Num())
        {
            return;
        }
        FLaneGraphPickScratch scratch;
        LaneGraphBuildBaseMasks(baseLanes, nodes.Num(), scratch);
        LaneGraphFillDistances(nodes, positionsM, scratch);
        for (int32 node = 0; node < nodes.Num(); ++node)
        {
            double distance = 0.0;
            const int32 nearest = LaneGraphNearestOf(nodes, positionsM, baseLanes, scratch, node, distance);
            if (nearest != INDEX_NONE)
            {
                outNew.Add(LaneGraphSortedPair(node, nearest));
            }
        }
        LaneGraphSortUnique(outNew);
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes each node's nearest taking-part partner not linked to it by a base lane (INDEX_NONE if none)
    void NearestPartners(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> baseLanes, TArray<int32>& outNearest)
    {
        // One entry per node always, all INDEX_NONE when the positions do not cover the nodes
        outNearest.Reset();
        outNearest.Init(INDEX_NONE, nodes.Num());
        if (positionsM.Num() < nodes.Num())
        {
            return;
        }
        FLaneGraphPickScratch scratch;
        LaneGraphBuildBaseMasks(baseLanes, nodes.Num(), scratch);
        LaneGraphFillDistances(nodes, positionsM, scratch);
        for (int32 node = 0; node < nodes.Num(); ++node)
        {
            double distance = 0.0;
            outNearest[node] = LaneGraphNearestOf(nodes, positionsM, baseLanes, scratch, node, distance);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the sorted, de-duplicated pairs (node, partner) of every node with a partner
    void PairsFromPicks(TConstArrayView<int32> partnerByNode, TArray<FSOLLanePair>& outPairs)
    {
        // A partner that is negative, the node itself or not a node index (>= the array length) is ignored
        outPairs.Reset();
        for (int32 node = 0; node < partnerByNode.Num(); ++node)
        {
            const int32 partner = partnerByNode[node];
            if (partner >= 0 && partner < partnerByNode.Num() && partner != node)
            {
                outPairs.Add(LaneGraphSortedPair(node, partner));
            }
        }
        LaneGraphSortUnique(outPairs);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one node's next partner pick from its previous pick, its current nearest and the two distances (O3)
    FSOLPartnerPick StepPartnerPick(const FSOLPartnerPick& previous, const int32 nearest,
        const double nearestDistanceM, const double heldDistanceM, const int32 minStayEpochs, const double switchRatio)
    {
        FSOLPartnerPick next;
        if (nearest == INDEX_NONE)
        {
            return next;
        }

        // No held partner, or it is no longer a valid candidate: take the nearest at once
        if (previous.Partner == INDEX_NONE || !FMath::IsFinite(heldDistanceM))
        {
            next.Partner = nearest;
            next.StayEpochs = 1;
            return next;
        }

        // Switch only after the minimum stay and when the nearest is clearly closer (strictly below the ratio)
        const bool bSwitch = nearest != previous.Partner && previous.StayEpochs >= minStayEpochs
            && nearestDistanceM < switchRatio * heldDistanceM;
        if (bSwitch)
        {
            next.Partner = nearest;
            next.StayEpochs = 1;
            return next;
        }
        next.Partner = previous.Partner;
        next.StayEpochs = previous.StayEpochs < MAX_int32 ? previous.StayEpochs + 1 : MAX_int32;
        return next;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the partner picks after stepping every node through epochCount epochs of positions (the warm-up)
    void ReplayPartnerPicks(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsOldestFirstM,
        const int32 epochCount, TConstArrayView<FSOLLanePair> baseLanes, const int32 minStayEpochs,
        const double switchRatio, TArray<FSOLPartnerPick>& outPicks)
    {
        // Each node's step reads only its own previous pick, so outPicks is stepped in place
        outPicks.Reset();
        const int32 nodeCount = nodes.Num();
        if (epochCount < 1 || static_cast<int64>(positionsOldestFirstM.Num())
            < static_cast<int64>(epochCount) * static_cast<int64>(nodeCount))
        {
            return;
        }
        outPicks.SetNum(nodeCount);
        FLaneGraphPickScratch scratch;
        LaneGraphBuildBaseMasks(baseLanes, nodeCount, scratch);

        // One StepPartnerPick per taking-part node per epoch, with the nearest and held distances at that epoch (the
        // held distance is NaN when either end is no longer a candidate)
        for (int32 epoch = 0; epoch < epochCount; ++epoch)
        {
            const TConstArrayView<FVector3d> positions = positionsOldestFirstM.Slice(epoch * nodeCount, nodeCount);
            LaneGraphFillDistances(nodes, positions, scratch);
            for (int32 node = 0; node < nodeCount; ++node)
            {
                if (!LaneGraphTakesPart(nodes[node].Kind))
                {
                    continue;
                }
                double nearestDistance = LANEGRAPH_NAN;
                const int32 nearest = LaneGraphNearestOf(nodes, positions, baseLanes, scratch, node, nearestDistance);
                const int32 held = outPicks[node].Partner;
                const double heldDistance = (held >= 0 && held < nodeCount)
                    ? scratch.DistanceM[node * nodeCount + held] : LANEGRAPH_NAN;
                outPicks[node] = StepPartnerPick(outPicks[node], nearest, nearestDistance, heldDistance,
                    minStayEpochs, switchRatio);
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the sorted island links that join every candidate component to the root node's group (O2)
    void IslandLinks(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> lanes, const int32 rootNode, TArray<FSOLLanePair>& outLinks)
    {
        outLinks.Reset();
        const int32 nodeCount = nodes.Num();
        if (rootNode < 0 || rootNode >= nodeCount || positionsM.Num() < nodeCount)
        {
            return;
        }

        // Components through the epoch's lanes (lanes naming no node are ignored)
        TArray<int32, TInlineAllocator<LANEGRAPH_INLINE_NODES>> parent;
        parent.SetNumUninitialized(nodeCount);
        for (int32 node = 0; node < nodeCount; ++node)
        {
            parent[node] = node;
        }
        for (const FSOLLanePair& lane : lanes)
        {
            if (lane.NodeA >= 0 && lane.NodeA < nodeCount && lane.NodeB >= 0 && lane.NodeB < nodeCount)
            {
                LaneGraphUnion(parent, lane.NodeA, lane.NodeB);
            }
        }

        // Repeatedly link the nearest (relay in the root group, candidate outside it) pair and merge v's component.
        // Both ends are candidates, so a Moon never relays: its island link starts at its parent (Earth). Each
        // candidate v outside keeps its best (distance, sorted pair) to the relays joined so far, updated only by
        // newly joined relays, which equals the full minimum over every (u, v) pair since the root group only grows
        TArray<uint8, TInlineAllocator<LANEGRAPH_INLINE_NODES>> joined;
        TArray<double, TInlineAllocator<LANEGRAPH_INLINE_NODES>> bestDistanceByNode;
        TArray<FSOLLanePair, TInlineAllocator<LANEGRAPH_INLINE_NODES>> bestPairByNode;
        joined.Init(0, nodeCount);
        bestDistanceByNode.Init(LANEGRAPH_INF, nodeCount);
        bestPairByNode.Init(FSOLLanePair(), nodeCount);
        for (;;)
        {
            // Newly joined relays update every outside candidate's best
            const int32 rootSet = LaneGraphFind(parent, rootNode);
            for (int32 u = 0; u < nodeCount; ++u)
            {
                if (joined[u] != 0 || LaneGraphFind(parent, u) != rootSet)
                {
                    continue;
                }
                joined[u] = 1;
                if (!LaneGraphCandidate(nodes, positionsM, u))
                {
                    continue;
                }
                for (int32 v = 0; v < nodeCount; ++v)
                {
                    if (!LaneGraphCandidate(nodes, positionsM, v) || LaneGraphFind(parent, v) == rootSet)
                    {
                        continue;
                    }
                    const double distance = (positionsM[v] - positionsM[u]).Length();
                    const FSOLLanePair pair = LaneGraphSortedPair(u, v);
                    if (distance < bestDistanceByNode[v]
                        || (distance == bestDistanceByNode[v] && LaneGraphPairLess(pair, bestPairByNode[v])))
                    {
                        bestDistanceByNode[v] = distance;
                        bestPairByNode[v] = pair;
                    }
                }
            }

            // The smallest (distance, sorted pair) over the outside candidates
            FSOLLanePair best;
            double bestDistance = LANEGRAPH_INF;
            for (int32 v = 0; v < nodeCount; ++v)
            {
                const FSOLLanePair& pair = bestPairByNode[v];
                if (pair.NodeA == INDEX_NONE || LaneGraphFind(parent, v) == rootSet)
                {
                    continue;
                }
                if (bestDistanceByNode[v] < bestDistance
                    || (bestDistanceByNode[v] == bestDistance && LaneGraphPairLess(pair, best)))
                {
                    bestDistance = bestDistanceByNode[v];
                    best = pair;
                }
            }
            if (best.NodeA == INDEX_NONE)
            {
                break;
            }
            outLinks.Add(best);
            LaneGraphUnion(parent, best.NodeA, best.NodeB);
        }
        outLinks.Sort([](const FSOLLanePair& a, const FSOLLanePair& b) { return LaneGraphPairLess(a, b); });
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the epoch index floor(t / epoch); 0 for a non-positive epoch or a non-finite input
    int64 LaneNetworkEpochIndex(const double simTimeS, const double epochS)
    {
        if (!FMath::IsFinite(simTimeS) || !FMath::IsFinite(epochS) || !(epochS > 0.0))
        {
            return 0;
        }
        const double index = std::floor(simTimeS / epochS);
        if (!FMath::IsFinite(index))
        {
            return 0;
        }
        return static_cast<int64>(FMath::Clamp(index, -LANEGRAPH_EPOCH_INDEX_LIMIT, LANEGRAPH_EPOCH_INDEX_LIMIT));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an epoch's sample time, index x epoch
    double LaneEpochSampleTimeS(const int64 epochIndex, const double epochS)
    {
        return static_cast<double>(epochIndex) * epochS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the lane slots for the epoch's partner set and returns the number of partner lanes that found no slot
    int32 AssignLaneSlots(TConstArrayView<FSOLLanePair> previousSlots, TConstArrayView<uint8> slotHeld,
        TConstArrayView<FSOLLanePair> partnerSet, const int32 baseCount, const int32 slotCapacity,
        TArray<FSOLLanePair>& outSlots)
    {
        // previousSlots or slotHeld shorter than the capacity read as free / not held there
        const int32 capacity = FMath::Max(slotCapacity, 0);
        const int32 base = FMath::Clamp(baseCount, 0, capacity);
        outSlots.Reset();
        outSlots.SetNum(capacity);

        // Base slots copy through; a partner slot keeps its lane if the lane stays in the set or its tunnel still fades
        for (int32 slot = 0; slot < capacity; ++slot)
        {
            const FSOLLanePair previous = slot < previousSlots.Num() ? previousSlots[slot] : FSOLLanePair();
            if (slot < base)
            {
                outSlots[slot] = previous;
                continue;
            }
            if (previous.NodeA == INDEX_NONE && previous.NodeB == INDEX_NONE)
            {
                continue;
            }
            bool bKeep = slot < slotHeld.Num() && slotHeld[slot] == 1;
            for (int32 index = 0; !bKeep && index < partnerSet.Num(); ++index)
            {
                bKeep = LaneGraphPairEqual(partnerSet[index], previous);
            }
            if (bKeep)
            {
                outSlots[slot] = previous;
            }
        }

        // The remaining new lanes, in set order, take the lowest free partner slot
        int32 overflow = 0;
        for (const FSOLLanePair& lane : partnerSet)
        {
            int32 freeSlot = INDEX_NONE;
            bool bPlaced = false;
            for (int32 slot = base; slot < capacity && !bPlaced; ++slot)
            {
                const FSOLLanePair& current = outSlots[slot];
                bPlaced = LaneGraphPairEqual(current, lane);
                if (freeSlot == INDEX_NONE && current.NodeA == INDEX_NONE && current.NodeB == INDEX_NONE)
                {
                    freeSlot = slot;
                }
            }
            if (bPlaced)
            {
                continue;
            }
            if (freeSlot == INDEX_NONE)
            {
                ++overflow;
                continue;
            }
            outSlots[freeSlot] = lane;
        }
        return overflow;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance from the Sun (the origin) to the segment A-B (clamped projection); NaN for a bad input
    double SunSegmentDistanceM(const FVector3d& aM, const FVector3d& bM)
    {
        if (!LaneGraphIsFinite(aM) || !LaneGraphIsFinite(bM))
        {
            return LANEGRAPH_NAN;
        }
        const FVector3d segment = bM - aM;
        const double lengthSq = segment.SizeSquared();
        if (!(lengthSq > 0.0))
        {
            return aM.Length();
        }
        const double t = FMath::Clamp(-FVector3d::DotProduct(aM, segment) / lengthSq, 0.0, 1.0);
        return (aM + segment * t).Length();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the lane's next Sun state from its clearance (close below 20, reopen above 22 Sun radii; NaN closes)
    ESOLLaneSunState StepSunState(const ESOLLaneSunState previous, const double clearanceM, const double sunRadiusM)
    {
        if (FMath::IsNaN(clearanceM) || !FMath::IsFinite(sunRadiusM))
        {
            return ESOLLaneSunState::Closed;
        }
        if (previous == ESOLLaneSunState::Open)
        {
            return clearanceM < SOL::NPC_LANE_SUN_CLEARANCE_RADII * sunRadiusM ? ESOLLaneSunState::Closed
                : ESOLLaneSunState::Open;
        }
        return clearanceM > SOL::NPC_LANE_SUN_REOPEN_RADII * sunRadiusM ? ESOLLaneSunState::Open
            : ESOLLaneSunState::Closed;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Sun state from the newest-first epoch clearance bounds (the pure look-back form, SDD 53 5.3)
    ESOLLaneSunState SunStateFromLookBack(TConstArrayView<double> boundsNewestFirstM, const double sunRadiusM)
    {
        if (!FMath::IsFinite(sunRadiusM))
        {
            return ESOLLaneSunState::Closed;
        }
        const double closeBelow = SOL::NPC_LANE_SUN_CLEARANCE_RADII * sunRadiusM;
        const double openAbove = SOL::NPC_LANE_SUN_REOPEN_RADII * sunRadiusM;
        for (const double bound : boundsNewestFirstM)
        {
            if (FMath::IsNaN(bound) || bound < closeBelow)
            {
                return ESOLLaneSunState::Closed;
            }
            if (bound > openAbove)
            {
                return ESOLLaneSunState::Open;
            }
        }
        return ESOLLaneSunState::Closed;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an epoch's clearance lower bound, min(samples) - speed x spacing / 2; -inf for a bad input
    double EpochClearanceBoundM(TConstArrayView<double> sampleClearancesM, const double maxEndpointSpeedMps,
        const double sampleSpacingS)
    {
        if (sampleClearancesM.Num() == 0 || !(maxEndpointSpeedMps >= 0.0) || !(sampleSpacingS >= 0.0))
        {
            return -LANEGRAPH_INF;
        }
        double minimum = LANEGRAPH_INF;
        for (const double sample : sampleClearancesM)
        {
            if (FMath::IsNaN(sample))
            {
                return -LANEGRAPH_INF;
            }
            minimum = FMath::Min(minimum, sample);
        }
        const double bound = minimum - maxEndpointSpeedMps * sampleSpacingS / 2.0;
        return FMath::IsNaN(bound) ? -LANEGRAPH_INF : bound;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the lane is open at simTimeS, with its window's start (latched, SDD 53 5.4) and close
    bool LaneOpenWindow(TConstArrayView<uint8> openByEpoch, const int64 firstEpoch, const double epochS,
        const double simTimeS, const double latchedOpenStartS, double& outOpenStartS, double& outCloseS)
    {
        // NaN outputs when closed or outside the buffer; the close is the buffer's end when no closed epoch follows
        outOpenStartS = LANEGRAPH_NAN;
        outCloseS = LANEGRAPH_NAN;
        if (!FMath::IsFinite(simTimeS) || !FMath::IsFinite(epochS) || !(epochS > 0.0))
        {
            return false;
        }
        const int64 offset = LaneNetworkEpochIndex(simTimeS, epochS) - firstEpoch;
        if (offset < 0 || offset >= openByEpoch.Num() || openByEpoch[static_cast<int32>(offset)] == 0)
        {
            return false;
        }
        const int32 current = static_cast<int32>(offset);

        // Walk back to the run's first open epoch and forward to the first closed one (or the buffer's end)
        int32 first = current;
        while (first > 0 && openByEpoch[first - 1] != 0)
        {
            --first;
        }
        int32 closed = current + 1;
        while (closed < openByEpoch.Num() && openByEpoch[closed] != 0)
        {
            ++closed;
        }
        // A run reaching the buffer's first entry keeps the start latched at the previous epoch (-inf allowed: open
        // since before anything known); with none (NaN: a new or reopened lane) it opens at the first entry, k x E,
        // so no ship appears mid-leg. A latched start after the first entry is not a valid latch and is clamped to it
        const double firstEntryS = LaneEpochSampleTimeS(firstEpoch, epochS);
        if (first > 0)
        {
            outOpenStartS = LaneEpochSampleTimeS(firstEpoch + first, epochS);
        }
        else
        {
            outOpenStartS = FMath::IsNaN(latchedOpenStartS) ? firstEntryS : FMath::Min(latchedOpenStartS, firstEntryS);
        }
        outCloseS = LaneEpochSampleTimeS(firstEpoch + closed, epochS);
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a leg departing at departS fits inside the open window [openStartS, closeS]
    bool LegFitsWindow(const double departS, const double legS, const double openStartS, const double closeS)
    {
        if (FMath::IsNaN(departS) || FMath::IsNaN(legS) || FMath::IsNaN(openStartS) || FMath::IsNaN(closeS)
            || legS < 0.0)
        {
            return false;
        }
        return departS >= openStartS && departS + legS <= closeS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the last departure time that still finishes by the close, close - leg
    double LaneDepartureStopS(const double closeS, const double legS)
    {
        return closeS - legS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a lane ship's leg is drawn at simTimeS (departed inside the window, leg in progress)
    bool LaneShipLegDrawn(const double simTimeS, const double legDepartS, const double legS, const double openStartS,
        const double departStopS)
    {
        return openStartS <= legDepartS && legDepartS <= departStopS && legDepartS <= simTimeS
            && simTimeS < legDepartS + legS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's edge time T_edge, the mean-effort leg on the AUTO keep-right track; 0 for a non-drivable profile
    double LaneEdgeTimeS(const FSOLLaneProfile& profile)
    {
        if (!profile.bDrivable)
        {
            return 0.0;
        }
        const FSOLLaneLeg leg = SOLLaneTraffic::MakeLegProfile(
            SOLLaneTraffic::RadialFactor(SOL::NPC_LANE_AUTO_TRACK_WALL_RADII), SOL::NPC_LANE_EFFORT_MEAN,
            profile.NominalLegM);
        return (leg.bValid && FMath::IsFinite(leg.LegS)) ? leg.LegS : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the transfer time between two lanes at a node: penalty + (0.5 (r1 + r2) theta + |r1 - r2|) / w
    double NodeTransferTimeS(const double endRingDistance1M, const double endRingDistance2M,
        const FVector3d& outward1, const FVector3d& outward2, const double wallSpeedMps, const double penaltyS)
    {
        // theta is the angle between the outward directions (atan2 form, exact at 0 and pi); +inf for any bad input
        const bool bDistancesOk = FMath::IsFinite(endRingDistance1M) && endRingDistance1M >= 0.0
            && FMath::IsFinite(endRingDistance2M) && endRingDistance2M >= 0.0;
        const bool bDirectionsOk = LaneGraphIsFinite(outward1) && LaneGraphIsFinite(outward2)
            && outward1.SizeSquared() > 0.0 && outward2.SizeSquared() > 0.0;
        const bool bSpeedOk = FMath::IsFinite(wallSpeedMps) && wallSpeedMps > 0.0;
        const bool bPenaltyOk = FMath::IsFinite(penaltyS) && penaltyS >= 0.0;
        if (!bDistancesOk || !bDirectionsOk || !bSpeedOk || !bPenaltyOk)
        {
            return LANEGRAPH_INF;
        }
        const double theta = std::atan2(FVector3d::CrossProduct(outward1, outward2).Length(),
            FVector3d::DotProduct(outward1, outward2));
        return penaltyS + (0.5 * (endRingDistance1M + endRingDistance2M) * theta
            + FMath::Abs(endRingDistance1M - endRingDistance2M)) / wallSpeedMps;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the fastest route over the edges with transfers and open windows (SDD 53 6.3; exact for current windows)
    FSOLLaneRoute FindFastestRoute(const int32 nodeCount, TConstArrayView<FSOLLaneEdge> edges, const double penaltyS,
        const double wallSpeedMps, const double startS, const int32 sourceNode, const int32 targetNode)
    {
        // Dijkstra over (node, arriving lane) states on fixed arrays. The fastest route is found first and is not found
        // (TooManyHops) if it has more than NPC_LANE_ROUTE_MAX_HOPS hops (no slower, shorter fallback); more edges than
        // LANEGRAPH_MAX_EDGES (the group texture width) is TooManyEdges. Each state keeps only its earliest arrival and
        // no waiting is modelled, so the route is exact when every window is already open at startS; a window opening
        // later can be missed by a faster arrival when a slower one would meet it (the pinned limit, SDD 53 6.3). An
        // edge with a non-positive or non-finite time is never taken; nor is a U-turn back down the arriving lane
        FSOLLaneRoute route;
        if (sourceNode < 0 || sourceNode >= nodeCount || targetNode < 0 || targetNode >= nodeCount)
        {
            route.Status = ESOLLaneRouteStatus::InvalidEndpoint;
            return route;
        }
        if (edges.Num() > LANEGRAPH_MAX_EDGES)
        {
            route.Status = ESOLLaneRouteStatus::TooManyEdges;
            return route;
        }
        if (sourceNode == targetNode)
        {
            route.bFound = true;
            route.Status = ESOLLaneRouteStatus::Found;
            return route;
        }

        // Every state starts unreached; the source state is queued at elapsed 0
        FLaneGraphRouter router;
        const int32 stateCount = 2 * edges.Num() + 1;
        for (int32 state = 0; state < stateCount; ++state)
        {
            router.ElapsedS[state] = LANEGRAPH_INF;
            router.Hops[state] = 0;
            router.Parent[state] = INDEX_NONE;
            router.HeapPos[state] = FLaneGraphRouter::LANEGRAPH_NOT_QUEUED;
        }
        router.ElapsedS[LANEGRAPH_SOURCE_STATE] = 0.0;
        router.PushOrDecrease(LANEGRAPH_SOURCE_STATE);

        // Settle states in (elapsed, hops) order and relax every lane leaving the state's node
        while (router.HeapCount > 0)
        {
            const int32 state = router.PopMin();
            const int32 node = LaneGraphStateNode(edges, state, sourceNode);
            const int32 arrivingLane = state == LANEGRAPH_SOURCE_STATE ? INDEX_NONE : (state - 1) / 2;
            FVector3d arrivingOutward = FVector3d::ZeroVector;
            double arrivingRing = 0.0;
            if (arrivingLane != INDEX_NONE)
            {
                // Arrived via +1 (A -> B) stands at B, via -1 at A
                arrivingRing = LaneGraphOutwardAt(edges[arrivingLane], (state - 1) % 2 != 0, arrivingOutward);
            }
            for (int32 lane = 0; lane < edges.Num(); ++lane)
            {
                const FSOLLaneEdge& edge = edges[lane];
                const bool bValidEdge = edge.NodeA >= 0 && edge.NodeA < nodeCount && edge.NodeB >= 0
                    && edge.NodeB < nodeCount && edge.NodeA != edge.NodeB && FMath::IsFinite(edge.TimeS)
                    && edge.TimeS > 0.0;
                if (!bValidEdge || lane == arrivingLane || (edge.NodeA != node && edge.NodeB != node))
                {
                    continue;
                }
                const bool bLeaveFromA = edge.NodeA == node;
                const int32 nextState = 1 + 2 * lane + (bLeaveFromA ? 0 : 1);
                if (router.HeapPos[nextState] == FLaneGraphRouter::LANEGRAPH_SETTLED)
                {
                    continue;
                }

                // The transfer at an intermediate node (none at the source), then the window check at departure
                double transferS = 0.0;
                if (arrivingLane != INDEX_NONE)
                {
                    FVector3d leavingOutward;
                    const double leavingRing = LaneGraphOutwardAt(edge, bLeaveFromA, leavingOutward);
                    transferS = NodeTransferTimeS(arrivingRing, leavingRing, arrivingOutward, leavingOutward,
                        wallSpeedMps, penaltyS);
                }
                const double departElapsedS = router.ElapsedS[state] + transferS;
                if (!FMath::IsFinite(departElapsedS)
                    || !LegFitsWindow(startS + departElapsedS, edge.TimeS, edge.OpenStartS, edge.CloseS))
                {
                    continue;
                }
                const double nextElapsedS = departElapsedS + edge.TimeS;
                if (!FMath::IsFinite(nextElapsedS))
                {
                    continue;
                }

                // Better by time, then fewer hops, then the lower lane sequence (same last lane, so compare parents)
                const int32 nextHops = router.Hops[state] + 1;
                const double currentS = router.ElapsedS[nextState];
                const bool bBetter = nextElapsedS < currentS
                    || (nextElapsedS == currentS && (nextHops < router.Hops[nextState]
                        || (nextHops == router.Hops[nextState]
                            && router.SequenceLess(state, router.Parent[nextState]))));
                if (bBetter)
                {
                    router.ElapsedS[nextState] = nextElapsedS;
                    router.Hops[nextState] = nextHops;
                    router.Parent[nextState] = state;
                    router.PushOrDecrease(nextState);
                }
            }
        }

        // The best state standing at the target, by (time, hops, lane sequence)
        int32 best = INDEX_NONE;
        for (int32 state = 1; state < stateCount; ++state)
        {
            if (!FMath::IsFinite(router.ElapsedS[state]) || LaneGraphStateNode(edges, state, sourceNode) != targetNode)
            {
                continue;
            }
            if (best == INDEX_NONE || router.KeyLess(state, best)
                || (!router.KeyLess(best, state) && router.SequenceLess(state, best)))
            {
                best = state;
            }
        }
        if (best == INDEX_NONE)
        {
            route.Status = ESOLLaneRouteStatus::Unreachable;
            return route;
        }
        if (router.Hops[best] > SOL::NPC_LANE_ROUTE_MAX_HOPS)
        {
            route.Status = ESOLLaneRouteStatus::TooManyHops;
            return route;
        }

        // Unwind the parent chain into the hop list
        route.bFound = true;
        route.Status = ESOLLaneRouteStatus::Found;
        route.TimeS = router.ElapsedS[best];
        route.HopCount = router.Hops[best];
        int32 state = best;
        for (int32 hop = route.HopCount - 1; hop >= 0; --hop)
        {
            route.Hops[hop].Lane = (state - 1) / 2;
            route.Hops[hop].DirectionSign = ((state - 1) % 2 == 0) ? 1 : -1;
            state = router.Parent[state];
        }
        return route;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the tunnel's largest on-screen radius f Rt / d in reference pixels
    double LaneTunnelMaxRadiusPx(const double segmentDistanceM, const double tunnelRadiusM, const double focalRefPx)
    {
        // 0 for a bad radius or focal length or a NaN distance, +inf for a non-positive distance
        if (FMath::IsNaN(segmentDistanceM) || !FMath::IsFinite(tunnelRadiusM) || !(tunnelRadiusM > 0.0)
            || !FMath::IsFinite(focalRefPx) || !(focalRefPx > 0.0))
        {
            return 0.0;
        }
        if (segmentDistanceM <= 0.0)
        {
            return LANEGRAPH_INF;
        }
        return focalRefPx * tunnelRadiusM / segmentDistanceM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a QualityPreset's tunnel threshold scale, the flame vanish size V over V at Epic (Low 10 .. Epic 1)
    double LaneTunnelQualityScale(const int32 qualityPreset)
    {
        // The preset is clamped to the presets the table covers (Low .. Cinematic)
        const int32 last = static_cast<int32>(UE_ARRAY_COUNT(SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY)) - 1;
        const int32 preset = FMath::Clamp(qualityPreset, 0, last);
        return SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY[preset]
            / SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY[SOL::NPC_TUNNEL_QUALITY_REFERENCE_PRESET];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the tunnel's next pixel class from its largest on-screen radius (hysteresis, thresholds x scale)
    ESOLTunnelClass StepTunnelClass(const ESOLTunnelClass previous, const double maxRadiusPx,
        const double thresholdScale)
    {
        // NaN radius gives Off; a NaN, infinite or non-positive scale reads as 1 (the Epic reference thresholds)
        if (FMath::IsNaN(maxRadiusPx))
        {
            return ESOLTunnelClass::Off;
        }
        const double scale = (FMath::IsFinite(thresholdScale) && thresholdScale > 0.0) ? thresholdScale : 1.0;
        const double activatePx = SOL::NPC_TUNNEL_ACTIVATE_PX * scale;
        const double deactivatePx = SOL::NPC_TUNNEL_DEACTIVATE_PX * scale;
        const double fullOnPx = SOL::NPC_TUNNEL_FULL_ON_PX * scale;
        const double fullOffPx = SOL::NPC_TUNNEL_FULL_OFF_PX * scale;
        switch (previous)
        {
        case ESOLTunnelClass::Full:
            if (maxRadiusPx < deactivatePx)
            {
                return ESOLTunnelClass::Off;
            }
            return maxRadiusPx < fullOffPx ? ESOLTunnelClass::Reduced : ESOLTunnelClass::Full;
        case ESOLTunnelClass::Reduced:
            if (maxRadiusPx >= fullOnPx)
            {
                return ESOLTunnelClass::Full;
            }
            return maxRadiusPx < deactivatePx ? ESOLTunnelClass::Off : ESOLTunnelClass::Reduced;
        default:
            if (maxRadiusPx >= fullOnPx)
            {
                return ESOLTunnelClass::Full;
            }
            return maxRadiusPx >= activatePx ? ESOLTunnelClass::Reduced : ESOLTunnelClass::Off;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the drawn lanes that get a tunnel instance, largest first, and returns the overflow count
    int32 SelectTunnelPool(TConstArrayView<double> maxRadiusPx, TConstArrayView<uint8> drawn, const int32 poolSize,
        TArray<int32>& outLanes)
    {
        // Ties to the lower index, a NaN radius last; a fixed pool, so each pick is the next in (radius desc, index
        // asc) order after the previous one and no scratch is needed. The quality scale is uniform, so it does not
        // change the ranking and is not an input
        outLanes.Reset();
        const int32 laneCount = FMath::Min(maxRadiusPx.Num(), drawn.Num());
        int32 drawnCount = 0;
        for (int32 lane = 0; lane < laneCount; ++lane)
        {
            drawnCount += drawn[lane] != 0 ? 1 : 0;
        }
        const int32 picks = FMath::Clamp(poolSize, 0, drawnCount);

        // Each pick: the largest (radius, -index) strictly after the previous pick in that order
        double previousPx = LANEGRAPH_INF;
        int32 previousLane = INDEX_NONE;
        for (int32 pick = 0; pick < picks; ++pick)
        {
            int32 bestLane = INDEX_NONE;
            double bestPx = 0.0;
            for (int32 lane = 0; lane < laneCount; ++lane)
            {
                if (drawn[lane] == 0)
                {
                    continue;
                }
                const double px = FMath::IsNaN(maxRadiusPx[lane]) ? -LANEGRAPH_INF : maxRadiusPx[lane];
                const bool bAfterPrevious = pick == 0 || px < previousPx || (px == previousPx && lane > previousLane);
                if (bAfterPrevious && (bestLane == INDEX_NONE || px > bestPx))
                {
                    bestLane = lane;
                    bestPx = px;
                }
            }
            if (bestLane == INDEX_NONE)
            {
                break;
            }
            outLanes.Add(bestLane);
            previousPx = bestPx;
            previousLane = bestLane;
        }
        return drawnCount - outLanes.Num();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether an overflow count change must be logged and reported now
    bool StepOverflowReport(const int32 previousOverflow, const int32 overflow)
    {
        return overflow != previousOverflow;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's traffic weight from its end tiers, sqrt(tierA x tierB); 0 for a negative or non-finite tier
    double LaneWeight(const double tierA, const double tierB)
    {
        if (!FMath::IsFinite(tierA) || !FMath::IsFinite(tierB) || tierA < 0.0 || tierB < 0.0)
        {
            return 0.0;
        }
        return FMath::Sqrt(tierA * tierB);
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the lane ship count split over the weights by largest remainder (the counts sum to laneShipCount)
    void LaneShipShares(const int32 laneShipCount, TConstArrayView<double> weights, TArray<int32>& outCounts)
    {
        // Remainder ties to the lower index; non-positive or NaN weights count 0; the remainders are recomputed by the
        // same expression on each pass, so no scratch array is needed
        outCounts.Reset();
        outCounts.SetNumZeroed(weights.Num());
        double total = 0.0;
        for (const double weight : weights)
        {
            total += LaneGraphShareWeight(weight);
        }
        if (laneShipCount <= 0 || !(total > 0.0) || !FMath::IsFinite(total))
        {
            return;
        }

        // The whole parts of each quota
        const double count = static_cast<double>(laneShipCount);
        int64 assigned = 0;
        for (int32 lane = 0; lane < weights.Num(); ++lane)
        {
            outCounts[lane] = static_cast<int32>(std::floor(count * LaneGraphShareWeight(weights[lane]) / total));
            assigned += outCounts[lane];
        }

        // The leftover ships go one each to the largest remainders, each pick the next in (remainder desc, index asc)
        const int64 leftover = FMath::Max<int64>(laneShipCount - assigned, 0);
        double previousRemainder = LANEGRAPH_INF;
        int32 previousLane = INDEX_NONE;
        for (int64 pick = 0; pick < leftover; ++pick)
        {
            int32 bestLane = INDEX_NONE;
            double bestRemainder = 0.0;
            for (int32 lane = 0; lane < weights.Num(); ++lane)
            {
                const double weight = LaneGraphShareWeight(weights[lane]);
                if (!(weight > 0.0))
                {
                    continue;
                }
                const double quota = count * weight / total;
                const double remainder = quota - std::floor(quota);
                const bool bAfterPrevious = remainder < previousRemainder
                    || (remainder == previousRemainder && lane > previousLane);
                if (bAfterPrevious && (bestLane == INDEX_NONE || remainder > bestRemainder))
                {
                    bestLane = lane;
                    bestRemainder = remainder;
                }
            }
            if (bestLane == INDEX_NONE)
            {
                break;
            }
            ++outCounts[bestLane];
            previousRemainder = bestRemainder;
            previousLane = bestLane;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's nominal length L0 from its end nodes' aphelia; 0 when not drivable
    double LaneNominalLengthM(TConstArrayView<FSOLLaneNode> nodes, const int32 nodeA, const int32 nodeB)
    {
        // The child's aphelion for a parent-child pair, the sum for two Sun-orbiting nodes, 0 otherwise
        if (nodeA < 0 || nodeA >= nodes.Num() || nodeB < 0 || nodeB >= nodes.Num() || nodeA == nodeB)
        {
            return 0.0;
        }
        const FSOLLaneNode& a = nodes[nodeA];
        const FSOLLaneNode& b = nodes[nodeB];
        double lengthM = 0.0;
        if (b.ParentNode == nodeA)
        {
            lengthM = b.AphelionM;
        }
        else if (a.ParentNode == nodeB)
        {
            lengthM = a.AphelionM;
        }
        else if (a.ParentNode == INDEX_NONE && b.ParentNode == INDEX_NONE)
        {
            const bool bAphelionsOk = FMath::IsFinite(a.AphelionM) && a.AphelionM >= 0.0
                && FMath::IsFinite(b.AphelionM) && b.AphelionM >= 0.0;
            lengthM = bAphelionsOk ? a.AphelionM + b.AphelionM : 0.0;
        }
        return (FMath::IsFinite(lengthM) && lengthM >= 0.0) ? lengthM : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the straight path from -> to enters the Sun zone; true for a NaN input (fail safe)
    bool PathEntersSunZone(const FVector3d& fromM, const FVector3d& toM, const double zoneRadiusM)
    {
        return !(SunSegmentDistanceM(fromM, toM) >= zoneRadiusM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the Sun safety applies: always for an NPC, for the player unless overridden
    bool SunSafetyActive(const bool bPlayerOverride, const bool bNpc)
    {
        return bNpc || !bPlayerOverride;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the arrival point: the target, or the zone-edge point facing the ship when the target is inside the zone
    FVector3d SunSafeArrivalM(const FVector3d& shipM, const FVector3d& targetM, const double zoneRadiusM,
        const double edgeRadiusM)
    {
        // A non-finite target counts as inside; +Z on the edge when the ship is at the origin or not finite
        if (LaneGraphIsFinite(targetM) && targetM.Length() >= zoneRadiusM)
        {
            return targetM;
        }
        const double shipDistance = LaneGraphIsFinite(shipM) ? shipM.Length() : 0.0;
        if (!(shipDistance > 0.0) || !FMath::IsFinite(shipDistance))
        {
            return FVector3d(0.0, 0.0, edgeRadiusM);
        }
        return (shipM / shipDistance) * edgeRadiusM;
    }
}
