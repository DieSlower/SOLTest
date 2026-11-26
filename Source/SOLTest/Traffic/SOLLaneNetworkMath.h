/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include <limits>

// The lane network's pure math (SDD 53, contract Docs/Plans/53-lane-network-contract.md): the daily lane set (base
// lanes, hysteretic nearest partners, island links), Sun clearance and closing, open windows, edge and transfer times,
// the fastest route, the tunnel pixel classes and pool, and the traffic weights. Universe ecliptic metres, sim seconds,
// double; inputs are plain arrays (nothing reads the registry or the belt catalogue)

// A lane node's kind: planets, dwarf planets and asteroids take part in the partner picks; moons, stations and cities
// do not
enum class ESOLLaneNodeKind : uint8
{
    Planet,
    DwarfPlanet,
    Moon,
    Asteroid,
    Station,
    City
};

// One node of the lane graph (SDD 53 section 4.1)
struct FSOLLaneNode
{
    ESOLLaneNodeKind Kind = ESOLLaneNodeKind::Planet;
    int32 BodyIndex = INDEX_NONE;       // Registry body index (planets, dwarf planets, moons)
    int32 SourceIndex = INDEX_NONE;     // Index in the node's source catalogue (asteroids)
    int32 ParentNode = INDEX_NONE;      // The parent node (a moon's planet); INDEX_NONE for Sun-orbiting nodes
    double ClearanceRadiusM = 0.0;      // Body radius used for the end ring
    double AphelionM = 0.0;             // Aphelion about the parent body (the Sun, or the parent planet for a moon)
    double RegionRadiusM = 0.0;         // Region radius (SDD 53 O5)
    double PopulationTier = 0.0;        // Traffic tier (SDD 53 O6)
};

// A lane's identity: the sorted node pair (NodeA < NodeB)
struct FSOLLanePair
{
    int32 NodeA = INDEX_NONE;
    int32 NodeB = INDEX_NONE;
};

// One edge of the routing graph
struct FSOLLaneEdge
{
    int32 NodeA = INDEX_NONE;
    int32 NodeB = INDEX_NONE;
    double TimeS = 0.0;                                                 // T_edge, same both ways
    double OpenStartS = -std::numeric_limits<double>::infinity();      // The lane's current open window (SDD 53 5.4)
    double CloseS = std::numeric_limits<double>::infinity();
    double PlanOpenFromS = std::numeric_limits<double>::infinity();    // A departure from here ignores the window
                                                                        // (the horizon rule, SDD 53 6.3)
    FVector3d DirectionAB = FVector3d::ZeroVector;                      // Unit, NodeA -> NodeB
    double EndRingDistanceAM = 0.0;                                     // End-ring distance from each node's centre
    double EndRingDistanceBM = 0.0;
};

// One hop of a route: the lane (edge index) and its direction (+1 = NodeA -> NodeB)
struct FSOLLaneHop
{
    int32 Lane = INDEX_NONE;
    int32 DirectionSign = 1;
};

// Why a route search ended (SDD 53 6.3): found, or the reason it was not
enum class ESOLLaneRouteStatus : uint8
{
    Found,
    Unreachable,        // No route inside the open windows
    TooManyHops,        // The fastest route has more than NPC_LANE_ROUTE_MAX_HOPS hops
    TooManyEdges,       // More edges than the router's fixed state arrays hold (the group texture width, 64)
    InvalidEndpoint     // Source or target is not a node index
};

// The fastest route between two nodes
struct FSOLLaneRoute
{
    bool bFound = false;
    ESOLLaneRouteStatus Status = ESOLLaneRouteStatus::Unreachable;
    double TimeS = 0.0;                                 // Sum of edge times and transfer costs
    int32 HopCount = 0;
    FSOLLaneHop Hops[SOL::NPC_LANE_ROUTE_MAX_HOPS];
};

// A lane's Sun state (SDD 53 section 5.1)
enum class ESOLLaneSunState : uint8
{
    Open,
    Closed
};

// A lane tunnel's pixel class (SDD 53 section 7.1)
enum class ESOLTunnelClass : uint8
{
    Off,
    Reduced,
    Full
};

// Why a lane slot is held after its lane leaves the set (SDD 53 4.5, part 53d): Blocks (it owns ship blocks, never
// taken) or Fade (only its tunnel still fades out: pre-emptible, the first slot a new lane takes when none is free)
enum class ESOLLaneSlotHold : uint8
{
    None = 0,
    Blocks = 1,
    Fade = 2
};

// A range of lane-local ship indices [First, First + Count) (a block move's rail upload, SDD 53 4.5)
struct FSOLShipRange
{
    int32 First = 0;
    int32 Count = 0;
};

// The outcome of one ship block step (SDD 53 4.5, P4; StepShipBlocks)
struct FSOLShipBlockStep
{
    TArray<uint8> SlotRelease;        // Per slot: its blocks returned to the free pool this step
    TArray<uint8> SlotEpisodeOpens;   // Per slot: its episode opened this step (profile frozen, blocks taken)
    TArray<uint8> SlotOwnsBlocks;     // Per slot: it owns blocks after the step (the caller holds the slot)
    TArray<int32> MovedBlocks;        // Blocks to re-rail, ascending and unique: released, taken, and every block of an
                                      // opening slot (its profile changes)
    int32 Released = 0;
    int32 Taken = 0;
    int32 Shortfall = 0;              // TakeShipBlocks' shortfall (the pool was short)
};

// One node's partner-pick hysteresis state (SDD 53 O3)
struct FSOLPartnerPick
{
    int32 Partner = INDEX_NONE;
    int32 StayEpochs = 0;
};

// One epoch's pair distances for the schedule's fast path (SDD 53 5.3), up to 64 nodes: the taking-part nodes and the
// candidates (taking part, finite position) as bit masks, each node's base-linked partners, and the symmetric centre
// distance matrix (NaN where an end is no candidate). The nearest partners and the island links of an epoch share
// it, so each pair's distance is computed once per epoch; the results equal StepPartnerPicks' nearest and IslandLinks
struct FSOLLaneEpochDistances
{
    int32 NodeCount = 0;
    uint64 TakesPartMask = 0;
    uint64 CandidateMask = 0;
    TArray<uint64> BaseMask;        // Per node
    TArray<double> DistanceM;       // NodeCount x NodeCount
};

namespace SOLLaneNetwork
{
    // Sets up the distances' node kinds and base links (once per node set); false above 64 nodes
    SOLTEST_API bool PrepareEpochDistances(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FSOLLanePair> baseLanes,
        FSOLLaneEpochDistances& outDistances);

    // Fills the candidate mask and the distance matrix for one epoch's positions (they must cover the nodes)
    SOLTEST_API void FillEpochDistances(TConstArrayView<FVector3d> positionsM, FSOLLaneEpochDistances& inOutDistances);

    // Writes each node's nearest candidate not base-linked to it and that distance from filled distances (ties to the
    // lower index; INDEX_NONE and NaN for a node that is no candidate or has none): StepPartnerPicks' nearest
    SOLTEST_API void NearestFromDistances(const FSOLLaneEpochDistances& distances, TArrayView<int32> outNearest,
        TArrayView<double> outNearestDistanceM);

    // IslandLinks over filled distances
    SOLTEST_API void IslandLinksFromDistances(const FSOLLaneEpochDistances& distances,
        TConstArrayView<FSOLLanePair> lanes, int32 rootNode, TArray<FSOLLanePair>& outLinks);

    // Writes the sorted, de-duplicated pairs of each taking-part node's nearest non-base-linked partner (no hysteresis)
    SOLTEST_API void NextClosestPairs(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> baseLanes, TArray<FSOLLanePair>& outNew);

    // Writes each node's nearest taking-part partner not linked to it by a base lane (INDEX_NONE if none)
    SOLTEST_API void NearestPartners(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> baseLanes, TArray<int32>& outNearest);

    // Writes the sorted, de-duplicated pairs (node, partner) of every node with a partner
    SOLTEST_API void PairsFromPicks(TConstArrayView<int32> partnerByNode, TArray<FSOLLanePair>& outPairs);

    // Returns one node's next partner pick from its previous pick, its current nearest and the two distances (O3)
    SOLTEST_API FSOLPartnerPick StepPartnerPick(const FSOLPartnerPick& previous, int32 nearest, double nearestDistanceM,
        double heldDistanceM, int32 minStayEpochs, double switchRatio);

    // Writes the partner picks after stepping every node through epochCount epochs of positions (the warm-up)
    SOLTEST_API void ReplayPartnerPicks(TConstArrayView<FSOLLaneNode> nodes,
        TConstArrayView<FVector3d> positionsOldestFirstM, int32 epochCount, TConstArrayView<FSOLLanePair> baseLanes,
        int32 minStayEpochs, double switchRatio, TArray<FSOLPartnerPick>& outPicks);

    // Steps every taking-part node's pick one epoch at positionsM (the look-ahead buffer's step; picks of a wrong size
    // start empty); false, picks untouched, when the positions do not cover the nodes
    SOLTEST_API bool StepPartnerPicks(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> baseLanes, int32 minStayEpochs, double switchRatio,
        TArray<FSOLPartnerPick>& inOutPicks);

    // Writes the sorted island links that join every candidate component to the root group (O2; never from a Moon)
    SOLTEST_API void IslandLinks(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
        TConstArrayView<FSOLLanePair> lanes, int32 rootNode, TArray<FSOLLanePair>& outLinks);

    // Returns the epoch index floor(t / epoch); 0 for a non-positive epoch or a NaN input
    SOLTEST_API int64 LaneNetworkEpochIndex(double simTimeS, double epochS);

    // Returns an epoch's sample time, index x epoch
    SOLTEST_API double LaneEpochSampleTimeS(int64 epochIndex, double epochS);

    // Writes the lane slots for the epoch's partner set and returns the number of partner lanes that found no slot.
    // slotHeld holds an ESOLLaneSlotHold per slot: a held slot keeps its lane out of the set; a new lane takes the
    // lowest free slot, else the lowest Fade-held slot whose lane left the set (its fade is cut short), never a
    // Blocks-held one
    SOLTEST_API int32 AssignLaneSlots(TConstArrayView<FSOLLanePair> previousSlots, TConstArrayView<uint8> slotHeld,
        TConstArrayView<FSOLLanePair> partnerSet, int32 baseCount, int32 slotCapacity, TArray<FSOLLanePair>& outSlots);

    // Returns the distance from the Sun (the origin) to the segment A-B; NaN for a non-finite input
    SOLTEST_API double SunSegmentDistanceM(const FVector3d& aM, const FVector3d& bM);

    // Returns the lane's next Sun state from its clearance (close below 20, reopen above 22 Sun radii; NaN closes)
    SOLTEST_API ESOLLaneSunState StepSunState(ESOLLaneSunState previous, double clearanceM, double sunRadiusM);

    // Returns the Sun state from the newest-first epoch clearance bounds (the pure look-back form, SDD 53 5.3)
    SOLTEST_API ESOLLaneSunState SunStateFromLookBack(TConstArrayView<double> boundsNewestFirstM, double sunRadiusM);

    // Returns an epoch's clearance lower bound, min(samples) - speed x spacing / 2; -inf for a bad input
    SOLTEST_API double EpochClearanceBoundM(TConstArrayView<double> sampleClearancesM, double maxEndpointSpeedMps,
        double sampleSpacingS);

    // Returns whether the lane is open at simTimeS, with its window's start (latched, SDD 53 5.4) and close
    SOLTEST_API bool LaneOpenWindow(TConstArrayView<uint8> openByEpoch, int64 firstEpoch, double epochS,
        double simTimeS, double latchedOpenStartS, double& outOpenStartS, double& outCloseS);

    // Returns whether a leg departing at departS fits inside the open window [openStartS, closeS]
    SOLTEST_API bool LegFitsWindow(double departS, double legS, double openStartS, double closeS);

    // Returns the last departure time that still finishes by the close, close - leg
    SOLTEST_API double LaneDepartureStopS(double closeS, double legS);

    // Returns whether a lane ship's leg is drawn at simTimeS (departed inside the window, leg in progress)
    SOLTEST_API bool LaneShipLegDrawn(double simTimeS, double legDepartS, double legS, double openStartS,
        double departStopS);

    // Returns a lane's edge time T_edge, the mean-effort leg on the AUTO keep-right track; 0 for a non-drivable profile
    SOLTEST_API double LaneEdgeTimeS(const FSOLLaneProfile& profile);

    // Returns the transfer time between two lanes at a node: penalty + (0.5 (r1 + r2) theta + |r1 - r2|) / w
    SOLTEST_API double NodeTransferTimeS(double endRingDistance1M, double endRingDistance2M, const FVector3d& outward1,
        const FVector3d& outward2, double wallSpeedMps, double penaltyS);

    // Returns the fastest route over the edges with transfers and open windows (SDD 53 6.3; exact for current windows)
    SOLTEST_API FSOLLaneRoute FindFastestRoute(int32 nodeCount, TConstArrayView<FSOLLaneEdge> edges, double penaltyS,
        double wallSpeedMps, double startS, int32 sourceNode, int32 targetNode);

    // Returns the tunnel's largest on-screen radius f Rt / d in reference pixels
    SOLTEST_API double LaneTunnelMaxRadiusPx(double segmentDistanceM, double tunnelRadiusM, double focalRefPx);

    // Returns a QualityPreset's tunnel threshold scale, the flame vanish size V over V at Epic (Low 10 .. Epic 1)
    SOLTEST_API double LaneTunnelQualityScale(int32 qualityPreset);

    // Returns the tunnel's next pixel class from its largest on-screen radius (hysteresis, thresholds x scale)
    SOLTEST_API ESOLTunnelClass StepTunnelClass(ESOLTunnelClass previous, double maxRadiusPx, double thresholdScale);

    // Writes the drawn lanes that get a tunnel instance, largest first, and returns the overflow count; a lane in
    // previousPool ranks by its radius x keepRatio (pool hysteresis: a newcomer must be keepRatio times larger to take
    // its instance; an empty previousPool or a ratio below 1 or NaN ranks by the radius alone)
    SOLTEST_API int32 SelectTunnelPool(TConstArrayView<double> maxRadiusPx, TConstArrayView<uint8> drawn,
        int32 poolSize, TArray<int32>& outLanes, TConstArrayView<uint8> previousPool = TConstArrayView<uint8>(),
        double keepRatio = 1.0);

    // Writes the pooled lanes (inPool != 0, below laneCount) per class into outFull and outReduced, index order (Off
    // lanes take no instance)
    SOLTEST_API void SplitTunnelPoolByClass(TConstArrayView<uint8> inPool, TConstArrayView<ESOLTunnelClass> classes,
        int32 laneCount, TArray<int32>& outFull, TArray<int32>& outReduced);

    // Copies next into current and returns true only when they differ (a class's instances are rewritten only then)
    SOLTEST_API bool SyncTunnelClassLanes(TArray<int32>& current, TConstArrayView<int32> next);

    // Returns a slot's hold from its block ownership and tunnel fade: Blocks while it owns blocks, else Fade while its
    // tunnel open fade is above 0, else None
    SOLTEST_API ESOLLaneSlotHold LaneSlotHold(bool bOwnsBlocks, bool bTunnelFading);

    // Returns whether a slot's block release must be retried next frame: it owns blocks while its lane is out of the
    // set (a Fade-only hold is re-checked once, when its fade ends, not every frame)
    SOLTEST_API bool IsSlotReleasePending(bool bOwnsBlocks, bool bInSetNow);

    // Returns whether an overflow count change must be logged and reported now
    SOLTEST_API bool StepOverflowReport(int32 previousOverflow, int32 overflow);

    // Returns a lane's traffic weight from its end tiers, sqrt(tierA x tierB)
    SOLTEST_API double LaneWeight(double tierA, double tierB);

    // Writes the lane ship count split over the weights by largest remainder (the counts sum to laneShipCount)
    SOLTEST_API void LaneShipShares(int32 laneShipCount, TConstArrayView<double> weights, TArray<int32>& outCounts);

    // Returns a lane's nominal length L0 from its end nodes' aphelia; 0 when not drivable
    SOLTEST_API double LaneNominalLengthM(TConstArrayView<FSOLLaneNode> nodes, int32 nodeA, int32 nodeB);

    // Returns whether the straight path from -> to enters the Sun zone; true for a non-finite input (fail safe)
    SOLTEST_API bool PathEntersSunZone(const FVector3d& fromM, const FVector3d& toM, double zoneRadiusM);

    // Returns whether the Sun safety applies: always for an NPC, for the player unless overridden
    SOLTEST_API bool SunSafetyActive(bool bPlayerOverride, bool bNpc);

    // Returns the arrival point: the target, or the zone-edge point facing the ship when the target is inside the zone
    SOLTEST_API FVector3d SunSafeArrivalM(const FVector3d& shipM, const FVector3d& targetM, double zoneRadiusM,
        double edgeRadiusM);

    // Part 53c2: ship blocks, open windows and traffic activation (contract cases 16-24)

    // Returns the number of ship blocks, ceil(lane ships / block size); 0 for a non-positive count or size (P3)
    SOLTEST_API int32 ShipBlockCount(int32 laneShipCount, int32 blockSize);

    // Returns the ship count of one block (the last block partial); 0 out of range
    SOLTEST_API int32 ShipBlockSize(int32 block, int32 laneShipCount, int32 blockSize);

    // Returns the block of a lane-local ship index, index / size; INDEX_NONE out of range
    SOLTEST_API int32 ShipBlockOfLaneShip(int32 laneShipIndex, int32 laneShipCount, int32 blockSize);

    // Writes every block's slot from the slot weights (exact LaneShipShares, ascending slot order; spawn and time jump)
    SOLTEST_API void AssignShipBlocks(int32 blockCount, TConstArrayView<double> slotWeights,
        TArray<int32>& outBlockSlot);

    // Frees every block whose slot is marked for release and returns the number freed
    SOLTEST_API int32 ReleaseShipBlocks(TConstArrayView<uint8> slotRelease, TArray<int32>& inOutBlockSlot);

    // Gives each slot in order the lowest free blocks up to its target and returns the total shortfall
    SOLTEST_API int32 TakeShipBlocks(TConstArrayView<int32> slotOrder, TConstArrayView<int32> slotTargets,
        TArray<int32>& inOutBlockSlot, TArray<int32>& outMovedBlocks);

    // Returns whether a slot's blocks may be released: its lane left the set and it has no CPU-tier ship
    SOLTEST_API bool CanReleaseSlotBlocks(bool bLaneInSetNow, int32 cpuTierShipsOfSlot);

    // Returns whether a slot's lane episode opens this step (in the set now, and new or back)
    SOLTEST_API bool LaneEpisodeOpens(const FSOLLanePair& previousSlotLane, bool bPreviousInSet,
        const FSOLLanePair& slotLane, bool bInSetNow);

    // Writes the slots whose episode opens, ascending (the block take order, P4 b and c)
    SOLTEST_API void EpisodeTakeOrder(TConstArrayView<uint8> slotEpisodeOpens, TArray<int32>& outSlotOrder);

    // Runs one block step (53c2b): every slot owning blocks that CanReleaseSlotBlocks (from inSetNow and its CPU-tier
    // ships) releases them, then the slots whose episode opens (LaneEpisodeOpens of the previous and current lane and
    // in-set flags; previous entries past their arrays read as a free slot outside the set) take blocks in
    // EpisodeTakeOrder up to LaneShipShares(block count, slotWeights). Writes the re-rail set (released, taken and
    // every block of an opening slot, ascending and unique) and the slots that own blocks afterwards
    SOLTEST_API void StepShipBlocks(TConstArrayView<FSOLLanePair> previousSlotLanes,
        TConstArrayView<uint8> previousInSet, TConstArrayView<FSOLLanePair> slotLanes, TConstArrayView<uint8> inSetNow,
        TConstArrayView<int32> slotCpuShips, TConstArrayView<double> slotWeights, TArray<int32>& inOutBlockSlot,
        FSOLShipBlockStep& outStep);

    // Writes each slot's blocks as a CSR list (slotCount + 1 starts, blocks ascending per slot)
    SOLTEST_API void SlotBlockLists(TConstArrayView<int32> blockSlot, int32 slotCount, TArray<int32>& outSlotStart,
        TArray<int32>& outBlocks);

    // Writes per slot the sum of a per-block value over its blocks
    SOLTEST_API void SumBlockCounts(TConstArrayView<int32> blockSlot, TConstArrayView<int32> perBlock,
        int32 slotCount, TArray<int32>& outPerSlot);

    // Writes the sorted, merged lane-local ship ranges of the moved blocks
    SOLTEST_API void ShipBlockDirtyRanges(TConstArrayView<int32> movedBlocks, int32 laneShipCount, int32 blockSize,
        TArray<FSOLShipRange>& outRanges);

    // Returns whether a lane ship is in space (drawn) at a rail time; a U-turn follows the leg after it (P1)
    SOLTEST_API bool LaneShipInSpace(const FSOLLaneCycle& cycle, double phase, double railTimeS,
        double openStartRailS, double closeRailS);

    // Returns a window bound relative to the GPU epoch, clamped to +-NPC_LANE_WINDOW_FAR_S, rounded once to float
    SOLTEST_API float LaneWindowGpuS(double windowS, double gpuEpochS);

    // Returns a slot's group texel row 6: (open, close, active, 0), or the empty window when there is none
    SOLTEST_API FVector4f LaneWindowTexel(bool bHasWindow, double openStartS, double closeS, double gpuEpochS,
        bool bActive);

    // Returns a lane's largest on-screen radius from the camera's distance to the segment A-B (reference pixels)
    SOLTEST_API double LaneTrafficRadiusPx(const FVector3d& cameraM, const FVector3d& aM, const FVector3d& bM,
        double laneRadiusM, double focalRefPx);

    // Returns whether a lane's traffic is active: radius at or above the scaled threshold, or a CPU-tier ship
    SOLTEST_API bool LaneTrafficActive(double laneRadiusPx, int32 cpuTierShipsOfLane, double thresholdScale);

    // Returns whether a lane is open now, openStart <= now < close (false for NaN)
    SOLTEST_API bool LaneOpenAt(double nowS, double openStartS, double closeS);
}
