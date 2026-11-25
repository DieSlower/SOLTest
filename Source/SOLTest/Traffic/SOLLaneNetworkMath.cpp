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
    // Steps every taking-part node's pick one epoch in place (scratch masks built; positions cover the nodes)
    void LaneGraphStepPicks(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> baseLanes, FLaneGraphPickScratch& scratch, const int32 minStayEpochs,
        const double switchRatio, TArray<FSOLPartnerPick>& inOutPicks)
    {
        // Each node's step reads only its own previous pick and this epoch's distances (NaN held distance when either
        // end is no longer a candidate)
        const int32 nodeCount = nodes.Num();
        LaneGraphFillDistances(nodes, positionsM, scratch);
        for (int32 node = 0; node < nodeCount; ++node)
        {
            if (!LaneGraphTakesPart(nodes[node].Kind))
            {
                continue;
            }
            double nearestDistance = LANEGRAPH_NAN;
            const int32 nearest = LaneGraphNearestOf(nodes, positionsM, baseLanes, scratch, node, nearestDistance);
            const int32 held = inOutPicks[node].Partner;
            const double heldDistance = (held >= 0 && held < nodeCount)
                ? scratch.DistanceM[node * nodeCount + held] : LANEGRAPH_NAN;
            inOutPicks[node] = SOLLaneNetwork::StepPartnerPick(inOutPicks[node], nearest, nearestDistance,
                heldDistance, minStayEpochs, switchRatio);
        }
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

        // The same one-epoch step as StepPartnerPicks, so a replay equals a chain of steps bit for bit
        for (int32 epoch = 0; epoch < epochCount; ++epoch)
        {
            LaneGraphStepPicks(nodes, positionsOldestFirstM.Slice(epoch * nodeCount, nodeCount), baseLanes, scratch,
                minStayEpochs, switchRatio, outPicks);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Steps every taking-part node's pick one epoch at the given positions; false (picks untouched) on short positions
    bool StepPartnerPicks(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> baseLanes, const int32 minStayEpochs, const double switchRatio,
        TArray<FSOLPartnerPick>& inOutPicks)
    {
        const int32 nodeCount = nodes.Num();
        if (positionsM.Num() < nodeCount)
        {
            return false;
        }
        if (inOutPicks.Num() != nodeCount)
        {
            inOutPicks.Reset();
            inOutPicks.SetNum(nodeCount);
        }
        FLaneGraphPickScratch scratch;
        LaneGraphBuildBaseMasks(baseLanes, nodeCount, scratch);
        LaneGraphStepPicks(nodes, positionsM.Slice(0, nodeCount), baseLanes, scratch, minStayEpochs, switchRatio,
            inOutPicks);
        return true;
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
    // Sets up the node kinds and base links of the epoch distances; false above 64 nodes
    bool PrepareEpochDistances(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FSOLLanePair> baseLanes,
        FSOLLaneEpochDistances& outDistances)
    {
        const int32 count = nodes.Num();
        outDistances = FSOLLaneEpochDistances();
        if (count > 64)
        {
            return false;
        }
        outDistances.NodeCount = count;
        outDistances.BaseMask.Init(0, count);
        outDistances.DistanceM.Init(LANEGRAPH_NAN, count * count);
        for (int32 node = 0; node < count; ++node)
        {
            outDistances.TakesPartMask |= LaneGraphTakesPart(nodes[node].Kind) ? uint64(1) << node : 0;
        }
        for (const FSOLLanePair& lane : baseLanes)
        {
            if (lane.NodeA >= 0 && lane.NodeA < count && lane.NodeB >= 0 && lane.NodeB < count)
            {
                outDistances.BaseMask[lane.NodeA] |= uint64(1) << lane.NodeB;
                outDistances.BaseMask[lane.NodeB] |= uint64(1) << lane.NodeA;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the candidate mask and the symmetric distance matrix (NaN unless both ends are candidates)
    void FillEpochDistances(TConstArrayView<FVector3d> positionsM, FSOLLaneEpochDistances& inOutDistances)
    {
        const int32 count = inOutDistances.NodeCount;
        uint64 candidates = 0;
        for (int32 node = 0; node < count && node < positionsM.Num(); ++node)
        {
            const bool bCandidate = ((inOutDistances.TakesPartMask >> node) & 1) != 0
                && LaneGraphIsFinite(positionsM[node]);
            candidates |= bCandidate ? uint64(1) << node : 0;
        }
        inOutDistances.CandidateMask = candidates;
        double* matrix = inOutDistances.DistanceM.GetData();
        for (int32 index = 0; index < count * count; ++index)
        {
            matrix[index] = LANEGRAPH_NAN;
        }

        // Each candidate pair once, the same value both ways (|b - a| and |a - b| are the same bits)
        for (uint64 restA = candidates; restA != 0; restA &= restA - 1)
        {
            const int32 a = static_cast<int32>(FPlatformMath::CountTrailingZeros64(restA));
            const uint64 above = a < 63 ? candidates & (~uint64(0) << (a + 1)) : 0;
            for (uint64 restB = above; restB != 0; restB &= restB - 1)
            {
                const int32 b = static_cast<int32>(FPlatformMath::CountTrailingZeros64(restB));
                const double distance = (positionsM[b] - positionsM[a]).Length();
                matrix[a * count + b] = distance;
                matrix[b * count + a] = distance;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes each node's nearest non-base-linked candidate and its distance from filled distances (LaneGraphNearestOf)
    void NearestFromDistances(const FSOLLaneEpochDistances& distances, TArrayView<int32> outNearest,
        TArrayView<double> outNearestDistanceM)
    {
        const int32 count = FMath::Min3(distances.NodeCount, outNearest.Num(), outNearestDistanceM.Num());
        const double* matrix = distances.DistanceM.GetData();
        for (int32 node = 0; node < count; ++node)
        {
            // Ties go to the lower index; NaN (no candidate) never compares below
            int32 nearest = INDEX_NONE;
            double bestDistance = LANEGRAPH_INF;
            if (((distances.CandidateMask >> node) & 1) != 0)
            {
                const double* row = matrix + node * distances.NodeCount;
                const uint64 baseMask = distances.BaseMask[node];
                for (int32 other = 0; other < distances.NodeCount; ++other)
                {
                    const double distance = row[other];
                    if (other == node || !(distance < bestDistance) || ((baseMask >> other) & 1) != 0)
                    {
                        continue;
                    }
                    bestDistance = distance;
                    nearest = other;
                }
            }
            outNearest[node] = nearest;
            outNearestDistanceM[node] = nearest != INDEX_NONE ? bestDistance : LANEGRAPH_NAN;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // IslandLinks over filled distances: components as bit masks, the same scan order and tie rules
    void IslandLinksFromDistances(const FSOLLaneEpochDistances& distances, TConstArrayView<FSOLLanePair> lanes,
        const int32 rootNode, TArray<FSOLLanePair>& outLinks)
    {
        outLinks.Reset();
        const int32 count = distances.NodeCount;
        if (rootNode < 0 || rootNode >= count)
        {
            return;
        }

        // Adjacency through the epoch's lanes (lanes naming no node are ignored)
        uint64 adjacency[64] = {};
        for (const FSOLLanePair& lane : lanes)
        {
            if (lane.NodeA >= 0 && lane.NodeA < count && lane.NodeB >= 0 && lane.NodeB < count)
            {
                adjacency[lane.NodeA] |= uint64(1) << lane.NodeB;
                adjacency[lane.NodeB] |= uint64(1) << lane.NodeA;
            }
        }

        // Returns the component mask of a node through the lanes
        const auto componentOf = [&adjacency](const int32 start)
        {
            uint64 reached = uint64(1) << start;
            uint64 frontier = reached;
            while (frontier != 0)
            {
                uint64 next = 0;
                for (uint64 rest = frontier; rest != 0; rest &= rest - 1)
                {
                    next |= adjacency[FPlatformMath::CountTrailingZeros64(rest)];
                }
                frontier = next & ~reached;
                reached |= next;
            }
            return reached;
        };

        // Newly joined relays update every outside candidate's best (distance, sorted pair); then the smallest joins
        const double* matrix = distances.DistanceM.GetData();
        const uint64 candidates = distances.CandidateMask;
        double bestDistanceByNode[64];
        FSOLLanePair bestPairByNode[64];
        for (int32 node = 0; node < count; ++node)
        {
            bestDistanceByNode[node] = LANEGRAPH_INF;
        }
        uint64 rootMask = componentOf(rootNode);
        uint64 joinedMask = 0;
        for (;;)
        {
            const uint64 newlyJoined = rootMask & ~joinedMask;
            joinedMask |= newlyJoined;
            for (uint64 restU = newlyJoined & candidates; restU != 0; restU &= restU - 1)
            {
                const int32 u = static_cast<int32>(FPlatformMath::CountTrailingZeros64(restU));
                for (uint64 restV = candidates & ~rootMask; restV != 0; restV &= restV - 1)
                {
                    const int32 v = static_cast<int32>(FPlatformMath::CountTrailingZeros64(restV));
                    const double distance = matrix[u * count + v];
                    const FSOLLanePair pair = LaneGraphSortedPair(u, v);
                    if (distance < bestDistanceByNode[v]
                        || (distance == bestDistanceByNode[v] && LaneGraphPairLess(pair, bestPairByNode[v])))
                    {
                        bestDistanceByNode[v] = distance;
                        bestPairByNode[v] = pair;
                    }
                }
            }
            FSOLLanePair best;
            double bestDistance = LANEGRAPH_INF;
            for (uint64 restV = candidates & ~rootMask; restV != 0; restV &= restV - 1)
            {
                const int32 v = static_cast<int32>(FPlatformMath::CountTrailingZeros64(restV));
                const FSOLLanePair& pair = bestPairByNode[v];
                if (pair.NodeA != INDEX_NONE && (bestDistanceByNode[v] < bestDistance
                    || (bestDistanceByNode[v] == bestDistance && LaneGraphPairLess(pair, best))))
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
            rootMask |= componentOf(((rootMask >> best.NodeA) & 1) != 0 ? best.NodeB : best.NodeA);
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
                // A departure at or after the edge's plan-open time (past the look-ahead) uses the lane as open
                const double departElapsedS = router.ElapsedS[state] + transferS;
                const double departS = startS + departElapsedS;
                if (!FMath::IsFinite(departElapsedS) || (!(departS >= edge.PlanOpenFromS)
                    && !LegFitsWindow(departS, edge.TimeS, edge.OpenStartS, edge.CloseS)))
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

    // Part 53c2: ship blocks, open windows and traffic activation (contract cases 16-24)

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of ship blocks, ceil(lane ships / block size); 0 for a non-positive count or size (P3)
    int32 ShipBlockCount(const int32 laneShipCount, const int32 blockSize)
    {
        if (laneShipCount <= 0 || blockSize <= 0)
        {
            return 0;
        }
        return static_cast<int32>((static_cast<int64>(laneShipCount) + blockSize - 1) / blockSize);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship count of one block (the last block partial); 0 out of range
    int32 ShipBlockSize(const int32 block, const int32 laneShipCount, const int32 blockSize)
    {
        if (block < 0 || block >= ShipBlockCount(laneShipCount, blockSize))
        {
            return 0;
        }
        const int64 first = static_cast<int64>(block) * blockSize;
        return static_cast<int32>(FMath::Min<int64>(blockSize, laneShipCount - first));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the block of a lane-local ship index, index / size; INDEX_NONE out of range
    int32 ShipBlockOfLaneShip(const int32 laneShipIndex, const int32 laneShipCount, const int32 blockSize)
    {
        if (blockSize <= 0 || laneShipIndex < 0 || laneShipIndex >= laneShipCount)
        {
            return INDEX_NONE;
        }
        return laneShipIndex / blockSize;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes every block's slot from the slot weights (exact LaneShipShares, ascending slot order; spawn and time jump)
    void AssignShipBlocks(const int32 blockCount, TConstArrayView<double> slotWeights, TArray<int32>& outBlockSlot)
    {
        // Every block starts free; slot s then takes the next target[s] blocks in ascending slot order
        outBlockSlot.Reset();
        if (blockCount <= 0)
        {
            return;
        }
        outBlockSlot.Init(INDEX_NONE, blockCount);
        TArray<int32> shares;
        LaneShipShares(blockCount, slotWeights, shares);
        int32 next = 0;
        for (int32 slot = 0; slot < shares.Num() && next < blockCount; ++slot)
        {
            const int32 end = FMath::Min(blockCount, next + shares[slot]);
            for (; next < end; ++next)
            {
                outBlockSlot[next] = slot;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Frees every block whose slot is marked for release and returns the number freed
    int32 ReleaseShipBlocks(TConstArrayView<uint8> slotRelease, TArray<int32>& inOutBlockSlot)
    {
        int32 released = 0;
        for (int32& slot : inOutBlockSlot)
        {
            if (slot >= 0 && slot < slotRelease.Num() && slotRelease[slot] == 1)
            {
                slot = INDEX_NONE;
                ++released;
            }
        }
        return released;
    }

    //////////////////////////////////////////////////////////////////////////
    // Gives each slot in order the lowest free blocks up to its target and returns the total shortfall
    int32 TakeShipBlocks(TConstArrayView<int32> slotOrder, TConstArrayView<int32> slotTargets,
        TArray<int32>& inOutBlockSlot, TArray<int32>& outMovedBlocks)
    {
        // One cursor over the free blocks: each take is the lowest free block left, so the scan never restarts
        outMovedBlocks.Reset();
        int32 shortfall = 0;
        int32 cursor = 0;
        for (const int32 slot : slotOrder)
        {
            if (slot < 0 || slot >= slotTargets.Num())
            {
                continue;
            }
            int32 held = 0;
            for (const int32 blockSlot : inOutBlockSlot)
            {
                held += blockSlot == slot ? 1 : 0;
            }
            int32 need = slotTargets[slot] - held;
            while (need > 0)
            {
                while (cursor < inOutBlockSlot.Num() && inOutBlockSlot[cursor] != INDEX_NONE)
                {
                    ++cursor;
                }
                if (cursor >= inOutBlockSlot.Num())
                {
                    break;
                }
                inOutBlockSlot[cursor] = slot;
                outMovedBlocks.Add(cursor);
                --need;
            }
            shortfall += FMath::Max(need, 0);
        }
        return shortfall;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a slot's blocks may be released: its lane left the set and it has no CPU-tier ship
    bool CanReleaseSlotBlocks(const bool bLaneInSetNow, const int32 cpuTierShipsOfSlot)
    {
        return !bLaneInSetNow && cpuTierShipsOfSlot <= 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a slot's lane episode opens this step (in the set now, and new or back)
    bool LaneEpisodeOpens(const FSOLLanePair& previousSlotLane, const bool bPreviousInSet,
        const FSOLLanePair& slotLane, const bool bInSetNow)
    {
        const bool bSameLane = previousSlotLane.NodeA == slotLane.NodeA && previousSlotLane.NodeB == slotLane.NodeB;
        return bInSetNow && (!bSameLane || !bPreviousInSet);
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the slots whose episode opens, ascending (the block take order, P4 b and c)
    void EpisodeTakeOrder(TConstArrayView<uint8> slotEpisodeOpens, TArray<int32>& outSlotOrder)
    {
        outSlotOrder.Reset();
        for (int32 slot = 0; slot < slotEpisodeOpens.Num(); ++slot)
        {
            if (slotEpisodeOpens[slot] != 0)
            {
                outSlotOrder.Add(slot);
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs one block step: releases the blocks of slots that may release them, then lets the slots whose episode opens
    // take their share in ascending slot order, and lists the blocks to re-rail and the slots still owning blocks
    void StepShipBlocks(TConstArrayView<FSOLLanePair> previousSlotLanes, TConstArrayView<uint8> previousInSet,
        TConstArrayView<FSOLLanePair> slotLanes, TConstArrayView<uint8> inSetNow,
        TConstArrayView<int32> slotCpuShips, TConstArrayView<double> slotWeights, TArray<int32>& inOutBlockSlot,
        FSOLShipBlockStep& outStep)
    {
        const int32 slotCount = slotLanes.Num();
        const int32 blockCount = inOutBlockSlot.Num();
        outStep.MovedBlocks.Reset();
        outStep.Released = 0;
        outStep.Taken = 0;
        outStep.Shortfall = 0;
        outStep.SlotRelease.Init(0, slotCount);
        outStep.SlotEpisodeOpens.Init(0, slotCount);
        outStep.SlotOwnsBlocks.Init(0, slotCount);

        // Which slots own blocks before the step
        for (const int32 slot : inOutBlockSlot)
        {
            if (slot >= 0 && slot < slotCount)
            {
                outStep.SlotOwnsBlocks[slot] = 1;
            }
        }

        // Releases (SDD 4.5): a slot whose lane left the set and that has no CPU-tier ship frees its blocks; each freed
        // block is re-railed (parked)
        bool bAnyRelease = false;
        for (int32 slot = 0; slot < slotCount; ++slot)
        {
            const bool bInSet = inSetNow.IsValidIndex(slot) && inSetNow[slot] != 0;
            const int32 cpuShips = slotCpuShips.IsValidIndex(slot) ? slotCpuShips[slot] : 0;
            if (outStep.SlotOwnsBlocks[slot] != 0 && CanReleaseSlotBlocks(bInSet, cpuShips))
            {
                outStep.SlotRelease[slot] = 1;
                bAnyRelease = true;
            }
        }
        if (bAnyRelease)
        {
            for (int32 block = 0; block < blockCount; ++block)
            {
                const int32 slot = inOutBlockSlot[block];
                if (slot >= 0 && slot < slotCount && outStep.SlotRelease[slot] != 0)
                {
                    outStep.MovedBlocks.Add(block);
                }
            }
            outStep.Released = ReleaseShipBlocks(outStep.SlotRelease, inOutBlockSlot);
        }

        // Episode opens (P4): only these slots take blocks, in ascending order, up to their LaneShipShares target
        bool bAnyOpen = false;
        const FSOLLanePair freeSlot;
        for (int32 slot = 0; slot < slotCount; ++slot)
        {
            const FSOLLanePair& previous = previousSlotLanes.IsValidIndex(slot) ? previousSlotLanes[slot] : freeSlot;
            const bool bPreviousInSet = previousInSet.IsValidIndex(slot) && previousInSet[slot] != 0;
            const bool bInSet = inSetNow.IsValidIndex(slot) && inSetNow[slot] != 0;
            outStep.SlotEpisodeOpens[slot] = LaneEpisodeOpens(previous, bPreviousInSet, slotLanes[slot], bInSet) ? 1 : 0;
            bAnyOpen |= outStep.SlotEpisodeOpens[slot] != 0;
        }
        if (bAnyOpen)
        {
            TArray<int32> order;
            TArray<int32> targets;
            TArray<int32> taken;
            EpisodeTakeOrder(outStep.SlotEpisodeOpens, order);
            LaneShipShares(blockCount, slotWeights, targets);
            outStep.Shortfall = TakeShipBlocks(order, targets, inOutBlockSlot, taken);
            outStep.Taken = taken.Num();

            // Every block of an opening slot is re-railed: its profile is frozen again at the open (SDD 4.5)
            for (int32 block = 0; block < blockCount; ++block)
            {
                const int32 slot = inOutBlockSlot[block];
                if (slot >= 0 && slot < slotCount && outStep.SlotEpisodeOpens[slot] != 0)
                {
                    outStep.MovedBlocks.Add(block);
                }
            }
        }

        // Ascending and unique (a block released and taken in the same step is listed once)
        outStep.MovedBlocks.Sort();
        int32 unique = 0;
        for (int32 index = 0; index < outStep.MovedBlocks.Num(); ++index)
        {
            if (unique == 0 || outStep.MovedBlocks[unique - 1] != outStep.MovedBlocks[index])
            {
                outStep.MovedBlocks[unique++] = outStep.MovedBlocks[index];
            }
        }
        outStep.MovedBlocks.SetNum(unique);

        // Owners after the step (the caller holds them)
        FMemory::Memzero(outStep.SlotOwnsBlocks.GetData(), slotCount);
        for (const int32 slot : inOutBlockSlot)
        {
            if (slot >= 0 && slot < slotCount)
            {
                outStep.SlotOwnsBlocks[slot] = 1;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes each slot's blocks as a CSR list (slotCount + 1 starts, blocks ascending per slot)
    void SlotBlockLists(TConstArrayView<int32> blockSlot, const int32 slotCount, TArray<int32>& outSlotStart,
        TArray<int32>& outBlocks)
    {
        // Counting sort: count per slot, prefix sums, then place in ascending block order
        outSlotStart.Reset();
        outBlocks.Reset();
        const int32 slots = FMath::Max(slotCount, 0);
        outSlotStart.SetNumZeroed(slots + 1);
        for (const int32 slot : blockSlot)
        {
            if (slot >= 0 && slot < slots)
            {
                ++outSlotStart[slot + 1];
            }
        }
        for (int32 slot = 0; slot < slots; ++slot)
        {
            outSlotStart[slot + 1] += outSlotStart[slot];
        }
        outBlocks.SetNumUninitialized(outSlotStart[slots]);
        TArray<int32, TInlineAllocator<64>> fill;
        fill.Append(outSlotStart.GetData(), slots);
        for (int32 block = 0; block < blockSlot.Num(); ++block)
        {
            const int32 slot = blockSlot[block];
            if (slot >= 0 && slot < slots)
            {
                outBlocks[fill[slot]++] = block;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes per slot the sum of a per-block value over its blocks
    void SumBlockCounts(TConstArrayView<int32> blockSlot, TConstArrayView<int32> perBlock, const int32 slotCount,
        TArray<int32>& outPerSlot)
    {
        outPerSlot.Reset();
        const int32 slots = FMath::Max(slotCount, 0);
        outPerSlot.SetNumZeroed(slots);
        for (int32 block = 0; block < blockSlot.Num() && block < perBlock.Num(); ++block)
        {
            const int32 slot = blockSlot[block];
            if (slot >= 0 && slot < slots)
            {
                outPerSlot[slot] += perBlock[block];
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the sorted, merged lane-local ship ranges of the moved blocks
    void ShipBlockDirtyRanges(TConstArrayView<int32> movedBlocks, const int32 laneShipCount, const int32 blockSize,
        TArray<FSOLShipRange>& outRanges)
    {
        // Sort a copy of the valid blocks, then merge repeated and adjacent blocks into one range each
        outRanges.Reset();
        const int32 blockCount = ShipBlockCount(laneShipCount, blockSize);
        TArray<int32, TInlineAllocator<64>> blocks;
        for (const int32 block : movedBlocks)
        {
            if (block >= 0 && block < blockCount)
            {
                blocks.Add(block);
            }
        }
        blocks.Sort();
        int32 runFirst = INDEX_NONE;
        int32 runLast = INDEX_NONE;

        // Appends the current run of blocks as one ship range (nothing before the first run)
        const auto flush = [&]()
        {
            if (runFirst != INDEX_NONE)
            {
                const int32 first = runFirst * blockSize;
                const int32 end = static_cast<int32>(
                    FMath::Min<int64>(static_cast<int64>(runLast + 1) * blockSize, laneShipCount));
                outRanges.Add(FSOLShipRange{first, end - first});
            }
        };
        for (const int32 block : blocks)
        {
            if (runFirst != INDEX_NONE && block <= runLast + 1)
            {
                runLast = FMath::Max(runLast, block);
                continue;
            }
            flush();
            runFirst = block;
            runLast = block;
        }
        flush();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a lane ship is in space (drawn) at a rail time; a U-turn follows the leg after it (P1)
    bool LaneShipInSpace(const FSOLLaneCycle& cycle, const double phase, const double railTimeS,
        const double openStartRailS, const double closeRailS)
    {
        if (FMath::IsNaN(openStartRailS) || FMath::IsNaN(closeRailS) || FMath::IsNaN(railTimeS))
        {
            return false;
        }

        // A lane that is not drivable draws its midpoint disc while open
        const bool bValidCycle = FMath::IsFinite(cycle.LegS) && cycle.LegS > 0.0 && FMath::IsFinite(cycle.CycleS)
            && cycle.CycleS > 0.0;
        if (!bValidCycle)
        {
            return openStartRailS <= railTimeS && railTimeS < closeRailS;
        }

        // On a leg: drawn while that leg departed inside the window; in a turn: the leg after the turn decides
        const FSOLLaneCyclePoint point = SOLLaneTraffic::CycleSegmentAt(cycle, phase, railTimeS);
        const double segmentStartS = railTimeS - point.SegmentTimeS;
        if (point.Segment == ESOLLaneSegment::Outbound || point.Segment == ESOLLaneSegment::Inbound)
        {
            return LaneShipLegDrawn(railTimeS, segmentStartS, cycle.LegS, openStartRailS,
                LaneDepartureStopS(closeRailS, cycle.LegS));
        }
        return LegFitsWindow(segmentStartS + cycle.TurnS, cycle.LegS, openStartRailS, closeRailS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a window bound relative to the GPU epoch, clamped to +-NPC_LANE_WINDOW_FAR_S, rounded once to float
    float LaneWindowGpuS(const double windowS, const double gpuEpochS)
    {
        const double relative = windowS - gpuEpochS;
        if (FMath::IsNaN(relative))
        {
            return static_cast<float>(relative);
        }
        return static_cast<float>(FMath::Clamp(relative, -SOL::NPC_LANE_WINDOW_FAR_S, SOL::NPC_LANE_WINDOW_FAR_S));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a slot's group texel row 6: (open, close, active, 0), or the empty window when there is none
    FVector4f LaneWindowTexel(const bool bHasWindow, const double openStartS, const double closeS,
        const double gpuEpochS, const bool bActive)
    {
        const float active = bActive ? 1.0f : 0.0f;
        if (bHasWindow && !FMath::IsNaN(openStartS) && !FMath::IsNaN(closeS) && openStartS < closeS)
        {
            return FVector4f(LaneWindowGpuS(openStartS, gpuEpochS), LaneWindowGpuS(closeS, gpuEpochS), active, 0.0f);
        }
        const float farS = static_cast<float>(SOL::NPC_LANE_WINDOW_FAR_S);
        return FVector4f(farS, -farS, active, 0.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's largest on-screen radius from the camera's distance to the segment A-B (reference pixels)
    double LaneTrafficRadiusPx(const FVector3d& cameraM, const FVector3d& aM, const FVector3d& bM,
        const double laneRadiusM, const double focalRefPx)
    {
        // The camera-relative segment's distance to the origin is the camera's distance to the segment
        return LaneTunnelMaxRadiusPx(SunSegmentDistanceM(aM - cameraM, bM - cameraM), laneRadiusM, focalRefPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a lane's traffic is active: radius at or above the scaled threshold, or a CPU-tier ship
    bool LaneTrafficActive(const double laneRadiusPx, const int32 cpuTierShipsOfLane, const double thresholdScale)
    {
        const double scale = (FMath::IsFinite(thresholdScale) && thresholdScale > 0.0) ? thresholdScale : 1.0;
        return laneRadiusPx >= SOL::NPC_LANE_TRAFFIC_ACTIVE_PX * scale || cpuTierShipsOfLane > 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a lane is open now, openStart <= now < close (false for NaN)
    bool LaneOpenAt(const double nowS, const double openStartS, const double closeS)
    {
        return openStartS <= nowS && nowS < closeS;
    }
}
