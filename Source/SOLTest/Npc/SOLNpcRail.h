/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcScaleMath.h"

#include "CoreMinimal.h"

// Compact rail of one NPC (SDD 11 section 3.1), 32 bytes. It is the motion model's only per-ship input: the CPU
// (SOLNpcRail::EvaluateM) and the GPU far-point shader evaluate the same float parameters, so a ship crossing the Mid
// radius does not jump. Parameter meaning by kind:
//   Lane:    P0 start parameter, P1 signed rate (direction * speed / nominal length, 1/s), P2-P3 lateral offset (m)
//   Cluster: P0 orbit radius (m), P1 inclination, P2 node, P3 phase (rad), P4 mean motion (rad/s)
//   Battle:  P0-P2 slot offset from the battle centre (m)
struct FSOLNpcRail
{
    uint8 Kind = 0;                  // SOLNpcScale::EPlacementKind
    uint8 Faction = 0;
    uint16 Group = 0;                // Global group index: lanes, then clusters, then battles (FSOLNpcGroupTable)
    float P[7] = {};
};
static_assert(sizeof(FSOLNpcRail) == 32, "FSOLNpcRail is uploaded to the GPU as two float4 texels");

// Group layout of one population: global group index = lane, then cluster, then battle
struct FSOLNpcGroupTable
{
    int32 LaneCount = 0;
    int32 ClusterCount = 0;
    int32 BattleCount = 0;
    TArray<int32> LaneBodyA;             // Registry index per lane (INDEX_NONE if unknown)
    TArray<int32> LaneBodyB;
    TArray<double> LaneRadiusM;
    TArray<int32> ClusterBody;           // Registry index per cluster
    TArray<double> ClusterInnerM;
    TArray<double> ClusterOuterM;
    TArray<FVector3d> BattleCenterFromBodyM; // Battle centre relative to the battle body, fixed at spawn
    int32 BattleBody = INDEX_NONE;       // Registry index the battles co-move with (the player's anchor at spawn)
    double BattleRadiusM = 0.0;

    // Returns the number of groups
    int32 Num() const { return LaneCount + ClusterCount + BattleCount; }
};

// Per-frame inputs of the rail evaluation: each group's anchor points (universe metres) and the rail time
struct FSOLNpcRailFrame
{
    double RailTimeS = 0.0;              // Sim seconds since the population epoch
    TArray<FVector3d> GroupPointAM;      // Lane: endpoint A; cluster: body centre; battle: battle centre
    TArray<FVector3d> GroupPointBM;      // Lane: endpoint B; otherwise equal to A
};

// The NPC motion model's rail half (the swappable seam, SDD 11 section 5 answer 3): spawn parameters to a compact rail,
// and rail plus frame to a position. Tier selection, rendering and combat only consume the resulting positions.
namespace SOLNpcRail
{
    // Packs one ship's spawn parameters into its rail; laneLengthM and bodyGM are its group's nominal lane length and
    // cluster body GM (ignored for other kinds), groupBase the first global index of its kind
    SOLTEST_API FSOLNpcRail FromSpawn(const SOLNpcScale::FSpawn& spawn, int32 groupBase, double laneLengthM,
        double bodyGM);

    // Fills the frame's group points from the registry body positions (universe metres)
    SOLTEST_API void BuildFrame(const FSOLNpcGroupTable& groups, TConstArrayView<FVector3d> bodyPositionsM,
        double railTimeS, FSOLNpcRailFrame& outFrame);

    // Returns the rail position (universe metres) of one ship in a frame
    SOLTEST_API FVector3d EvaluateM(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame);

    // Returns the rail re-based to an epoch deltaS later, for the GPU copy (SDD 11 Amendment 7): the lane parameter
    // (period 2) and the cluster phase (period 2 pi) are advanced by rate * deltaS in double and wrapped, so the GPU's
    // float evaluation always runs on a small time and a small phase. EvaluateM(result, t) matches
    // EvaluateM(rail, t + deltaS) up to the float rounding of the wrapped phase
    SOLTEST_API FSOLNpcRail AdvancedToEpoch(const FSOLNpcRail& rail, double deltaS);

    // Returns the distance from a point to a group's volume (lane capsule, cluster shell or battle ball)
    SOLTEST_API double GroupDistanceM(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame, int32 group,
        const FVector3d& pointM);
}
