/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcScaleMath.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "CoreMinimal.h"

// Compact rail of one NPC (SDD 11 section 3.1), 32 bytes. It is the motion model's only per-ship input: the CPU
// (SOLNpcRail::EvaluateM, in double) and the GPU far-point shader evaluate the same float parameters. Cluster and
// battle ships agree to float rounding at the Mid radius; a lane ship's float evaluation (SOLNpcTraffic.ush) differs
// from the double one by up to SDD 50 case 10's bound (tens to hundreds of km), so ASOLNpcVisuals draws a lane ship's
// CPU point at the float mirror's position at the Mid radius, blended to the double one by the hull radius (SDD 50
// section 4.6). Parameter meaning by kind:
//   Lane:    (SDD 50 section 4.6) P0 cycle phase high float in [0, 1), P1 dv (m/s), P2 up offset rho Rw cos psi (m),
//            P3 right offset rho Rw sin psi >= 0 (m), P4 accelerate time t_a (s), P5 the phase's low part (phase - P0),
//            P6 reserved 0; the leg time is derived (SOLLaneTraffic::LaneShipLegS)
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
    TArray<FSOLLaneProfile> LaneProfile; // Frozen lane profile per lane (SDD 50 section 4.2; BuildLaneProfiles)
    TArray<int32> LaneInWallCount;       // Ships inside the wall per lane (the spawn counting pass)
    TArray<int32> LaneHaloCount;         // Ships in the halo per lane (stored only, D13)
    TArray<double> LaneMaxPeakMps;       // Largest peak speed w + dv of each lane's rails (AccumulateLaneMaxPeak)
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
    TArray<FVector3d> GroupBodyM;        // The group's body (lane: body A; battle: the battle body): the up hint
    TArray<FSOLLaneProfile> LaneProfile; // Per lane: a copy of the table's profile (default, not drivable, if none)
    TArray<FSOLLaneFrame> LaneFrame;     // Per lane: basis and current leg (invalid for an unresolved body)
};

// A rail's motion at the frame's rail time (SOLNpcRail::Motion)
struct FSOLNpcRailMotion
{
    FVector3d VelocityMps = FVector3d::ZeroVector;     // Relative to the group's anchor points (central difference)
    FVector3d LaneHullForward = FVector3d::ZeroVector; // Unit lane hull facing; zero for other kinds / no lane frame
};

// The NPC motion model's rail half (the swappable seam, SDD 11 section 5 answer 3): spawn parameters to a compact rail,
// and rail plus frame to a position. Tier selection, rendering and combat only consume the resulting positions.
namespace SOLNpcRail
{
    inline constexpr double RAIL_VELOCITY_STEP_S = 0.5;  // Half step of VelocityMps's central difference

    // Packs one ship's spawn parameters into its rail; laneProfile and bodyGM are its group's frozen lane profile and
    // cluster body GM (ignored for other kinds), groupBase the first global index of its kind. A lane rail holds
    // SOLLaneTraffic::MakeLaneShip's constants, each rounded once to float, the phase split into P0 and P5
    SOLTEST_API FSOLNpcRail FromSpawn(const SOLNpcScale::FSpawn& spawn, int32 groupBase,
        const FSOLLaneProfile& laneProfile, double bodyGM);

    // Returns a lane rail's constants in double (phase P0 + P5, then P1-P4)
    SOLTEST_API FSOLLaneShip LaneShipFromRail(const FSOLNpcRail& rail);

    // Sizes the table's lane profile, count and peak arrays to LaneCount and freezes each lane's profile,
    // MakeLaneProfile(TunnelRadiusM(LaneRadiusM, in-wall count), nominal length, body radii) (D13: the halo count is
    // only stored); an entry past an input's end reads 0; LaneMaxPeakMps is reset to zeros
    SOLTEST_API void BuildLaneProfiles(FSOLNpcGroupTable& groups, TConstArrayView<int32> inWallCounts,
        TConstArrayView<int32> haloCounts, TConstArrayView<double> nominalLengthsM,
        TConstArrayView<double> bodyRadiiAM, TConstArrayView<double> bodyRadiiBM);

    // Raises each lane's largest peak to w + P1 of its lane rails (never lowers a value; other kinds and lanes past the
    // array are ignored), so a parallel spawn can merge per-range results
    SOLTEST_API void AccumulateLaneMaxPeak(TConstArrayView<FSOLNpcRail> rails, TArrayView<double> inOutMaxPeakMps);

    // Fills the frame's group points from the registry body positions (universe metres), and per lane its profile copy
    // and lane frame (basis and current leg); no reallocation at the same sizes
    SOLTEST_API void BuildFrame(const FSOLNpcGroupTable& groups, TConstArrayView<FVector3d> bodyPositionsM,
        double railTimeS, FSOLNpcRailFrame& outFrame);

    // Returns the rail position (universe metres) of one ship in a frame
    SOLTEST_API FVector3d EvaluateM(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame);

    // Returns the rail position at a rail time with the frame's group points held fixed (EvaluateM at its own time)
    SOLTEST_API FVector3d EvaluateAtTimeM(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame, double railTimeS);

    // Returns the rail's velocity relative to its group's anchor points (a central difference of EvaluateAtTimeM)
    SOLTEST_API FVector3d VelocityMps(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame);

    // Returns the rail re-based to an epoch deltaS later, for the GPU copy (SDD 11 Amendment 7): a lane rail's phase
    // advances by deltaS / T_cyc in double (T_cyc from its lane's profile), wrapped and split into P0 / P5; the cluster
    // phase (period 2 pi) by rate * deltaS; so the GPU's float evaluation always runs on a small time and a small phase.
    // EvaluateM(result, t) matches EvaluateM(rail, t + deltaS) up to the float rounding of the wrapped phase. A
    // non-finite delta, or a lane rail whose group has no profile, leaves the rail as it is
    SOLTEST_API FSOLNpcRail AdvancedToEpoch(const FSOLNpcRail& rail, double deltaS,
        TConstArrayView<FSOLLaneProfile> laneProfiles);

    // Writes a ship's two GPU rail texels for an epoch deltaS later (SDD 50 section 4.6): the rail AdvancedToEpoch as
    // (kind + 4 faction + 8 group, P0, P1, P2), (P3, P4, P5, P6), except that a lane rail with a profile and a valid leg
    // carries its time into the cycle at the epoch tau0 = phase x T_cyc (double) as a high / low float pair in P0 / P5
    // and its leg time T_leg (double, rounded once) in P6, the inputs of SOLNpcTraffic.ush's double-float evaluation.
    // T_leg's rounding r = T_leg - float(T_leg) rides in the fraction of the packed first value: packed + r / (2 ulp),
    // within +-0.25 of the integer (the shader decodes the integer by rounding, so kind, faction and group are kept)
    SOLTEST_API void GpuRailTexels(const FSOLNpcRail& rail, double deltaS,
        TConstArrayView<FSOLLaneProfile> laneProfiles, FVector4f& outTexelA, FVector4f& outTexelB);

    // Returns the float step of a positive normal leg time as the shader computes it (exponent bits x 2^-23)
    SOLTEST_API float GpuLegUlpS(float legS);

    // Returns a lane rail's unit hull facing at a rail time (SOLLaneTraffic::LaneHullForward, D21); the zero vector for
    // cluster and battle rails and for a group without a lane frame (the caller keeps the velocity heading)
    SOLTEST_API FVector3d LaneHullForward(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame, double railTimeS);

    // Returns a rail's velocity (VelocityMps) and lane hull facing (LaneHullForward at the frame's rail time) from one
    // derivation of a lane ship's leg and cycle, for the per-frame Mid / Full paths
    SOLTEST_API FSOLNpcRailMotion Motion(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame);

    // Returns the vector a Mid / Full hull heads along from a Motion result (the SOLNpcAi::HullForward input; D17 /
    // D21): with a lane hull facing, that facing scaled by max(rail speed, wall speed), plus the deviation velocity, so
    // the hull faces its thrust on the rail and a large AI deviation still turns it; without one (cluster and battle
    // rails, a group without a lane frame) the rail velocity plus the deviation velocity (the pre-#50 heading)
    SOLTEST_API FVector3d HullHeadingVelocityMps(const FSOLNpcRailMotion& motion,
        const FVector3d& deviationVelocityMps);

    // Returns an upper bound on a lane's ship speeds relative to body A (SOLLaneTraffic::LaneShipSpeedBoundMps of the
    // lane's largest peak, nominal and current legs and halo outer radius), without NPC_WAKE_SPEED_MARGIN; finite and
    // >= 0 for an out-of-range lane
    SOLTEST_API double LaneWakeSpeedBoundMps(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame,
        int32 lane, double endpointRelativeSpeedMps);

    // Returns a lane's Mid hand-over margin (SDD 50 section 4.6): a bound on how far the point shader's float position
    // of any ship of the lane lies from its double rail position, for a viewpoint within the Mid radius plus the
    // margin: 4 ulp(|B - A| + 4 midRadiusM) + 1 m, and for a drivable lane also 2^-22 l0 x the metric map's slope (the
    // leg-time term of every ship), 4 ulp(l0) and the lane's largest peak x ulp(2 x NPC_GPU_RAIL_EPOCH_S). The tiers
    // promote and keep a lane ship as a CPU ship within the Mid radius plus this, so every ship the far layer hides is
    // drawn by the CPU point layer. 0 for an unknown lane or one without a lane frame
    SOLTEST_API double LaneHandOverMarginM(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame, int32 lane,
        double midRadiusM);

    // Writes rows 2-5 of the group texels (row r, column c at r x NPC_GROUP_TEXTURE_WIDTH + c; SDD 50 section 4.6): a
    // drivable lane gets (Up, w), (Right, D_U), (pi / T_U, e_A, l0, Z), (e_B - end shift, T_U, (l - l0) / (l0 - Z),
    // 1 / Z), w = SOL::NPC_LANE_WALL_SPEED_MPS (so the shader never bakes it) and pi / T_U 0 without a U-turn; a valid
    // but not drivable lane the two basis rows and zero rows 4-5; every other column zero rows 2-5.
    // Rows 0 and 1 are left alone; a buffer under NPC_GROUP_TEXTURE_ROWS rows is not touched
    SOLTEST_API void WriteLaneTrafficTexels(const FSOLNpcRailFrame& frame, TArrayView<FVector4f> groupTexels);

    // Returns the distance from a point to a group's volume (lane capsule of the halo's outer radius, or the lane
    // radius without a profile; cluster shell or battle ball)
    SOLTEST_API double GroupDistanceM(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame, int32 group,
        const FVector3d& pointM);
}
