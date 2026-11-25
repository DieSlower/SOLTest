/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcRail.h"

#include "Npc/SOLNpcTunnelMath.h"
#include "Traffic/SOLLaneNetworkMath.h"

namespace
{
    constexpr float RAIL_FLOAT_BELOW_ONE = 0.99999994f;     // Largest float below 1 (the phase's high part limit)
    constexpr double RAIL_FLOAT_EPSILON = 1.1920928955078125e-7;    // 2^-23, one float step relative to its binade

    //////////////////////////////////////////////////////////////////////////
    // Returns the float step (ulp) at a magnitude: the step of the binade its float rounding falls in
    double RailFloatUlp(const double magnitude)
    {
        const float value = static_cast<float>(FMath::Min(FMath::Abs(magnitude), 3.0e38));
        return static_cast<double>(FMath::AsFloat(FMath::AsUInt(value) & 0x7F800000u)) * RAIL_FLOAT_EPSILON;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a phase wrapped into [0, 1); a non-finite phase reads as 0
    double RailWrapPhase(const double phase)
    {
        if (!FMath::IsFinite(phase))
        {
            return 0.0;
        }
        const double fraction = phase - FMath::FloorToDouble(phase);
        return fraction >= 1.0 ? 0.0 : fraction;
    }

    //////////////////////////////////////////////////////////////////////////
    // Stores a phase in [0, 1) as a high float P0 in [0, 1) and its low part P5 = phase - P0, so P0 + P5 keeps the
    // double phase to about 1e-14 and the shader's time into the cycle moves in sub-millisecond steps
    void RailStorePhase(const double phase, FSOLNpcRail& rail)
    {
        const double wrapped = RailWrapPhase(phase);
        const float high = FMath::Min(static_cast<float>(wrapped), RAIL_FLOAT_BELOW_ONE);
        rail.P[0] = high;
        rail.P[5] = static_cast<float>(wrapped - static_cast<double>(high));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane rail's cycle time in double from its group's profile and writes its leg time (both 0 when the
    // group has no profile; the cycle is not finite or positive for a ship without a valid leg)
    double RailLaneCycleS(const FSOLNpcRail& rail, const TConstArrayView<FSOLLaneProfile> laneProfiles,
        double& outLegS)
    {
        outLegS = 0.0;
        if (!laneProfiles.IsValidIndex(rail.Group))
        {
            return 0.0;
        }
        const FSOLLaneProfile& profile = laneProfiles[rail.Group];
        outLegS = SOLLaneTraffic::LaneShipLegS(profile, SOLNpcRail::LaneShipFromRail(rail));
        return SOLLaneTraffic::MakeCycle(outLegS, profile.UTurnDepthM).CycleS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a cycle time can re-base a lane phase (finite and positive)
    bool RailIsCycleUsable(const double cycleS)
    {
        return FMath::IsFinite(cycleS) && cycleS > 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Advances a lane rail's phase by deltaS / T_cyc in double (the delta's remainder over the cycle) and re-splits it
    // into P0 / P5
    void RailAdvanceLanePhase(const FSOLNpcRail& rail, const double deltaS, const double cycleS,
        FSOLNpcRail& outAdvanced)
    {
        RailStorePhase(SOLNpcRail::LaneShipFromRail(rail).Phase + FMath::Fmod(deltaS, cycleS) / cycleS, outAdvanced);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a rail is a lane rail whose group has a lane profile and frame in the frame
    bool RailHasLaneFrame(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame)
    {
        return static_cast<SOLNpcScale::EPlacementKind>(rail.Kind) == SOLNpcScale::EPlacementKind::Lane
            && frame.LaneFrame.IsValidIndex(rail.Group) && frame.LaneProfile.IsValidIndex(rail.Group);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns how many lane network slots the group texture leaves after the clusters and battles
int32 FSOLNpcGroupTable::LaneSlotCapacity() const
{
    return FMath::Max(SOL::NPC_GROUP_TEXTURE_WIDTH - FMath::Max(ClusterCount, 0) - FMath::Max(BattleCount, 0), 0);
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane network slot's group column: a base slot is its lane column, a later slot follows the battles
int32 FSOLNpcGroupTable::LaneSlotColumn(const int32 slot) const
{
    if (slot < 0 || slot >= LaneSlotCapacity())
    {
        return INDEX_NONE;
    }
    return slot < LaneCount ? slot : slot + FMath::Max(ClusterCount, 0) + FMath::Max(BattleCount, 0);
}

//////////////////////////////////////////////////////////////////////////
// Returns a column's lane network slot: base lane columns first, then the slot columns after the battles
int32 FSOLNpcGroupTable::ColumnSlot(const int32 column) const
{
    if (column < 0 || column >= Num())
    {
        return INDEX_NONE;
    }
    if (column < LaneCount)
    {
        return column;
    }
    const int32 firstExtra = LaneCount + FMath::Max(ClusterCount, 0) + FMath::Max(BattleCount, 0);
    return column >= firstExtra ? column - firstExtra + LaneCount : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Packs the kind-specific spawn fields into the rail's float parameters
FSOLNpcRail SOLNpcRail::FromSpawn(const SOLNpcScale::FSpawn& spawn, int32 groupBase,
    const FSOLLaneProfile& laneProfile, double bodyGM)
{
    FSOLNpcRail rail;
    rail.Kind = static_cast<uint8>(spawn.Kind);
    rail.Faction = static_cast<uint8>(spawn.Faction);
    rail.Group = static_cast<uint16>(FMath::Max(groupBase + spawn.GroupIndex, 0));
    switch (spawn.Kind)
    {
    case SOLNpcScale::EPlacementKind::Lane:
    {
        // The 50a closed form's constants, each rounded once (P6 stays 0, reserved)
        const FSOLLaneShip ship = SOLLaneTraffic::MakeLaneShip(laneProfile, spawn.LanePhase, spawn.LaneEffort,
            spawn.LaneRho, spawn.LaneClockRad);
        RailStorePhase(ship.Phase, rail);
        rail.P[1] = static_cast<float>(ship.DvMps);
        rail.P[2] = static_cast<float>(ship.UpOffsetM);
        rail.P[3] = static_cast<float>(ship.RightOffsetM);
        rail.P[4] = static_cast<float>(ship.AccelS);
        break;
    }
    case SOLNpcScale::EPlacementKind::Cluster:
    {
        const double r = spawn.OrbitRadiusM;
        rail.P[0] = static_cast<float>(r);
        rail.P[1] = static_cast<float>(spawn.OrbitInclinationRad);
        rail.P[2] = static_cast<float>(spawn.OrbitNodeRad);
        rail.P[3] = static_cast<float>(spawn.OrbitPhaseRad);
        rail.P[4] = (r > 0.0 && bodyGM > 0.0) ? static_cast<float>(FMath::Sqrt(bodyGM / (r * r * r))) : 0.0f;
        break;
    }
    case SOLNpcScale::EPlacementKind::Battle:
    default:
        rail.P[0] = static_cast<float>(spawn.BattleSlotOffsetM.X);
        rail.P[1] = static_cast<float>(spawn.BattleSlotOffsetM.Y);
        rail.P[2] = static_cast<float>(spawn.BattleSlotOffsetM.Z);
        break;
    }
    return rail;
}

//////////////////////////////////////////////////////////////////////////
// Fills each group's anchor points from the current body positions
void SOLNpcRail::BuildFrame(const FSOLNpcGroupTable& groups, TConstArrayView<FVector3d> bodyPositionsM,
    double railTimeS, FSOLNpcRailFrame& outFrame, TConstArrayView<FVector3d> nodePositionsM)
{
    outFrame.RailTimeS = railTimeS;
    outFrame.GroupPointAM.SetNumUninitialized(groups.Num(), EAllowShrinking::No);
    outFrame.GroupPointBM.SetNumUninitialized(groups.Num(), EAllowShrinking::No);
    outFrame.GroupBodyM.SetNumUninitialized(groups.Num(), EAllowShrinking::No);

    // Returns whether an end point index resolves: a registry body, else a node position past the registry
    const int32 registryCount = bodyPositionsM.Num();
    const auto hasEndpoint = [&nodePositionsM, registryCount](int32 body)
    {
        return body >= 0 && (body < registryCount || nodePositionsM.IsValidIndex(body - registryCount));
    };

    // Returns an end point's position, or the universe origin for an unknown index
    const auto bodyAt = [&bodyPositionsM, &nodePositionsM, registryCount](int32 body)
    {
        if (bodyPositionsM.IsValidIndex(body))
        {
            return bodyPositionsM[body];
        }
        return body >= registryCount && nodePositionsM.IsValidIndex(body - registryCount)
            ? nodePositionsM[body - registryCount] : FVector3d::ZeroVector;
    };

    // Lane columns (base lanes, then the slot columns after the battles): the end points, the frozen profile copy and
    // the current lane frame (invalid for an unresolved end); cluster and battle columns in between get a default
    // profile and an invalid frame, their points are written below
    const int32 laneColumnEnd = FMath::Max(groups.LaneColumnEnd(), 0);
    outFrame.LaneProfile.SetNum(laneColumnEnd, EAllowShrinking::No);
    outFrame.LaneFrame.SetNum(laneColumnEnd, EAllowShrinking::No);
    for (int32 column = 0; column < laneColumnEnd; ++column)
    {
        if (!groups.IsLaneColumn(column))
        {
            outFrame.LaneProfile[column] = FSOLLaneProfile();
            outFrame.LaneFrame[column] = FSOLLaneFrame();
            continue;
        }
        const int32 bodyA = groups.LaneBodyA.IsValidIndex(column) ? groups.LaneBodyA[column] : INDEX_NONE;
        const int32 bodyB = groups.LaneBodyB.IsValidIndex(column) ? groups.LaneBodyB[column] : INDEX_NONE;
        outFrame.GroupPointAM[column] = bodyAt(bodyA);
        outFrame.GroupPointBM[column] = bodyAt(bodyB);
        outFrame.GroupBodyM[column] = outFrame.GroupPointAM[column];
        const FSOLLaneProfile& profile = groups.LaneProfile.IsValidIndex(column) ? groups.LaneProfile[column]
            : FSOLLaneProfile();
        outFrame.LaneProfile[column] = profile;
        outFrame.LaneFrame[column] = hasEndpoint(bodyA) && hasEndpoint(bodyB)
            ? SOLLaneTraffic::MakeLaneFrame(profile, outFrame.GroupPointAM[column], outFrame.GroupPointBM[column])
            : FSOLLaneFrame();
    }
    int32 group = FMath::Max(groups.LaneCount, 0);
    for (int32 cluster = 0; cluster < groups.ClusterCount; ++cluster, ++group)
    {
        outFrame.GroupPointAM[group] = bodyAt(groups.ClusterBody[cluster]);
        outFrame.GroupPointBM[group] = outFrame.GroupPointAM[group];
        outFrame.GroupBodyM[group] = outFrame.GroupPointAM[group];
    }
    const FVector3d battleBody = bodyAt(groups.BattleBody);
    for (int32 battle = 0; battle < groups.BattleCount; ++battle, ++group)
    {
        outFrame.GroupPointAM[group] = battleBody + groups.BattleCenterFromBodyM[battle];
        outFrame.GroupPointBM[group] = outFrame.GroupPointAM[group];
        outFrame.GroupBodyM[group] = battleBody;
    }
}

//////////////////////////////////////////////////////////////////////////
// Evaluates a lane, cluster or battle rail in double precision from its float parameters
FVector3d SOLNpcRail::EvaluateM(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame)
{
    return EvaluateAtTimeM(rail, frame, frame.RailTimeS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the rail's velocity relative to its group's anchor points (central difference, points held fixed)
FVector3d SOLNpcRail::VelocityMps(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame)
{
    if (static_cast<SOLNpcScale::EPlacementKind>(rail.Kind) == SOLNpcScale::EPlacementKind::Battle)
    {
        return FVector3d::ZeroVector;
    }
    return (EvaluateAtTimeM(rail, frame, frame.RailTimeS + RAIL_VELOCITY_STEP_S)
        - EvaluateAtTimeM(rail, frame, frame.RailTimeS - RAIL_VELOCITY_STEP_S)) / (2.0 * RAIL_VELOCITY_STEP_S);
}

//////////////////////////////////////////////////////////////////////////
// Evaluates a rail at a given rail time with the frame's group points held fixed
FVector3d SOLNpcRail::EvaluateAtTimeM(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame, const double railTimeS)
{
    // A parked lane ship (NPC_RAIL_PARKED_GROUP) has no group points: the universe origin, never drawn or promoted
    if (!frame.GroupPointAM.IsValidIndex(rail.Group))
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d& pointA = frame.GroupPointAM[rail.Group];
    switch (static_cast<SOLNpcScale::EPlacementKind>(rail.Kind))
    {
    case SOLNpcScale::EPlacementKind::Lane:
    {
        // The 50a closed form (phase formula tau = wrap(phase x T_cyc + t, T_cyc), as the shader); a group without a
        // lane frame holds the ship at endpoint A
        if (!RailHasLaneFrame(rail, frame))
        {
            return pointA;
        }
        return SOLLaneTraffic::LaneShipPositionM(frame.LaneProfile[rail.Group], frame.LaneFrame[rail.Group],
            LaneShipFromRail(rail), railTimeS);
    }
    case SOLNpcScale::EPlacementKind::Cluster:
    {
        // Circular orbit: r (cos theta P + sin theta Q), as SOLNpcScale::ClusterOffsetM
        const double theta = static_cast<double>(rail.P[3]) + static_cast<double>(rail.P[4]) * railTimeS;
        double sinI = 0.0;
        double cosI = 0.0;
        double sinNode = 0.0;
        double cosNode = 0.0;
        double sinTheta = 0.0;
        double cosTheta = 0.0;
        FMath::SinCos(&sinI, &cosI, static_cast<double>(rail.P[1]));
        FMath::SinCos(&sinNode, &cosNode, static_cast<double>(rail.P[2]));
        FMath::SinCos(&sinTheta, &cosTheta, theta);
        const double r = rail.P[0];
        return pointA + FVector3d(cosNode * cosTheta - cosI * sinNode * sinTheta,
            sinNode * cosTheta + cosI * cosNode * sinTheta, sinI * sinTheta) * r;
    }
    case SOLNpcScale::EPlacementKind::Battle:
    default:
        return pointA + FVector3d(rail.P[0], rail.P[1], rail.P[2]);
    }
}

//////////////////////////////////////////////////////////////////////////
// Advances the lane phase by deltaS / T_cyc or the cluster phase by rate * deltaS and wraps it into one period
FSOLNpcRail SOLNpcRail::AdvancedToEpoch(const FSOLNpcRail& rail, const double deltaS,
    TConstArrayView<FSOLLaneProfile> laneProfiles)
{
    FSOLNpcRail advanced = rail;
    if (!FMath::IsFinite(deltaS))
    {
        return advanced;
    }
    switch (static_cast<SOLNpcScale::EPlacementKind>(rail.Kind))
    {
    case SOLNpcScale::EPlacementKind::Lane:
    {
        // In double: the delta's remainder over the ship's cycle (lanes are the first groups, so the global group is
        // the lane index), then re-split into P0 / P5
        double legS = 0.0;
        const double cycleS = RailLaneCycleS(rail, laneProfiles, legS);
        if (RailIsCycleUsable(cycleS))
        {
            RailAdvanceLanePhase(rail, deltaS, cycleS, advanced);
        }
        break;
    }
    case SOLNpcScale::EPlacementKind::Cluster:
    {
        const double phase = static_cast<double>(rail.P[3]) + static_cast<double>(rail.P[4]) * deltaS;
        advanced.P[3] = static_cast<float>(phase - UE_DOUBLE_TWO_PI * FMath::FloorToDouble(phase / UE_DOUBLE_TWO_PI));
        break;
    }
    case SOLNpcScale::EPlacementKind::Battle:
    default:
        break;
    }
    return advanced;
}

//////////////////////////////////////////////////////////////////////////
// Distance to a lane capsule, a cluster shell or a battle ball
double SOLNpcRail::GroupDistanceM(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame, int32 group,
    const FVector3d& pointM)
{
    if (groups.IsLaneColumn(group))
    {
        // The halo's outer radius H Rw bounds every lane ship laterally (the U-turn apexes stay above the bodies'
        // surfaces, so between A and B along the axis)
        const double radiusM = groups.LaneProfile.IsValidIndex(group) ? groups.LaneProfile[group].HaloOuterRadiusM
            : (groups.LaneRadiusM.IsValidIndex(group) ? groups.LaneRadiusM[group] : 0.0);
        return SOLNpcScale::DistanceToLaneM(pointM, frame.GroupPointAM[group], frame.GroupPointBM[group], radiusM);
    }
    const int32 cluster = group - groups.LaneCount;
    if (cluster < groups.ClusterCount)
    {
        return SOLNpcScale::DistanceToShellM(pointM, frame.GroupPointAM[group], groups.ClusterInnerM[cluster],
            groups.ClusterOuterM[cluster]);
    }
    return SOLNpcScale::DistanceToShellM(pointM, frame.GroupPointAM[group], 0.0, groups.BattleRadiusM);
}

//////////////////////////////////////////////////////////////////////////
// Reads a lane rail's constants back in double
FSOLLaneShip SOLNpcRail::LaneShipFromRail(const FSOLNpcRail& rail)
{
    FSOLLaneShip ship;
    ship.Phase = static_cast<double>(rail.P[0]) + static_cast<double>(rail.P[5]);
    ship.DvMps = rail.P[1];
    ship.UpOffsetM = rail.P[2];
    ship.RightOffsetM = rail.P[3];
    ship.AccelS = rail.P[4];
    return ship;
}

//////////////////////////////////////////////////////////////////////////
// Freezes each lane's profile from the counting pass' in-wall count (D13), its nominal length and end body radii
void SOLNpcRail::BuildLaneProfiles(FSOLNpcGroupTable& groups, TConstArrayView<int32> inWallCounts,
    TConstArrayView<int32> haloCounts, TConstArrayView<double> nominalLengthsM, TConstArrayView<double> bodyRadiiAM,
    TConstArrayView<double> bodyRadiiBM)
{
    const int32 laneCount = FMath::Max(groups.LaneColumnEnd(), 0);
    groups.LaneProfile.SetNum(laneCount);
    groups.LaneInWallCount.SetNum(laneCount);
    groups.LaneHaloCount.SetNum(laneCount);
    groups.LaneMaxPeakMps.SetNum(laneCount);

    // Returns an input's entry, or 0 past its end
    const auto entryOf = [](const auto values, const int32 index)
    {
        return values.IsValidIndex(index) ? values[index] : 0;
    };
    for (int32 lane = 0; lane < laneCount; ++lane)
    {
        // Inputs are indexed by column; a cluster or battle column between the lane columns holds a default profile
        if (!groups.IsLaneColumn(lane))
        {
            groups.LaneInWallCount[lane] = 0;
            groups.LaneHaloCount[lane] = 0;
            groups.LaneMaxPeakMps[lane] = 0.0;
            groups.LaneProfile[lane] = FSOLLaneProfile();
            continue;
        }
        const int32 inWall = entryOf(inWallCounts, lane);
        const double laneRadiusM = groups.LaneRadiusM.IsValidIndex(lane) ? groups.LaneRadiusM[lane] : 0.0;
        groups.LaneInWallCount[lane] = inWall;
        groups.LaneHaloCount[lane] = entryOf(haloCounts, lane);
        groups.LaneMaxPeakMps[lane] = 0.0;
        groups.LaneProfile[lane] = SOLLaneTraffic::MakeLaneProfile(SOLNpcTunnel::TunnelRadiusM(laneRadiusM, inWall),
            entryOf(nominalLengthsM, lane), entryOf(bodyRadiiAM, lane), entryOf(bodyRadiiBM, lane));
    }
}

//////////////////////////////////////////////////////////////////////////
// Raises each lane's largest peak speed to its lane rails' w + dv
void SOLNpcRail::AccumulateLaneMaxPeak(TConstArrayView<FSOLNpcRail> rails, TArrayView<double> inOutMaxPeakMps)
{
    for (const FSOLNpcRail& rail : rails)
    {
        if (static_cast<SOLNpcScale::EPlacementKind>(rail.Kind) == SOLNpcScale::EPlacementKind::Lane
            && inOutMaxPeakMps.IsValidIndex(rail.Group))
        {
            inOutMaxPeakMps[rail.Group] = FMath::Max(inOutMaxPeakMps[rail.Group],
                SOL::NPC_LANE_WALL_SPEED_MPS + static_cast<double>(rail.P[1]));
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane rail's hull facing (zero for other kinds or without a lane frame)
FVector3d SOLNpcRail::LaneHullForward(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame, const int32 npcIndex,
    const double railTimeS)
{
    if (!RailHasLaneFrame(rail, frame))
    {
        return FVector3d::ZeroVector;
    }
    return SOLLaneTraffic::LaneHullForward(frame.LaneProfile[rail.Group], frame.LaneFrame[rail.Group],
        LaneShipFromRail(rail), static_cast<uint32>(npcIndex), railTimeS);
}

//////////////////////////////////////////////////////////////////////////
// Returns a rail's velocity and lane hull facing, a lane ship's leg and cycle derived once
FSOLNpcRailMotion SOLNpcRail::Motion(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame, const int32 npcIndex)
{
    FSOLNpcRailMotion motion;
    if (!RailHasLaneFrame(rail, frame))
    {
        motion.VelocityMps = VelocityMps(rail, frame);
        return motion;
    }
    const FSOLLaneShipMotion lane = SOLLaneTraffic::LaneShipMotion(frame.LaneProfile[rail.Group],
        frame.LaneFrame[rail.Group], LaneShipFromRail(rail), static_cast<uint32>(npcIndex), frame.RailTimeS,
        RAIL_VELOCITY_STEP_S);
    motion.VelocityMps = lane.VelocityMps;
    motion.LaneHullForward = lane.HullForward;
    motion.LaneHullTop = lane.HullTop;
    return motion;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane rail's thrust acceleration at the frame's rail time (zero while coasting or flipping, and off-lane)
FVector3d SOLNpcRail::LaneThrustMps2(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame, const int32 npcIndex)
{
    if (!RailHasLaneFrame(rail, frame))
    {
        return FVector3d::ZeroVector;
    }
    const FSOLLaneThrustState state = SOLLaneTraffic::LaneThrustAt(frame.LaneProfile[rail.Group],
        frame.LaneFrame[rail.Group], LaneShipFromRail(rail), static_cast<uint32>(npcIndex), frame.RailTimeS);
    const bool bThrusts = state.Thrust == ESOLLaneThrust::Accelerate || state.Thrust == ESOLLaneThrust::Brake
        || state.Thrust == ESOLLaneThrust::Turn;
    const FVector3d thrustMps2 = state.Direction * state.AccelMps2;
    return bThrusts && !thrustMps2.ContainsNaN() && FMath::IsFinite(thrustMps2.SizeSquared()) ? thrustMps2
        : FVector3d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Returns the hull heading vector from a Motion result: the lane hull facing at rail speed (never below the wall
// speed), else the rail velocity, plus the deviation velocity
FVector3d SOLNpcRail::HullHeadingVelocityMps(const FSOLNpcRailMotion& motion, const FVector3d& deviationVelocityMps)
{
    if (motion.LaneHullForward.IsZero())
    {
        return motion.VelocityMps + deviationVelocityMps;
    }
    const double railSpeedMps = FMath::IsFinite(motion.VelocityMps.SizeSquared()) ? motion.VelocityMps.Size() : 0.0;
    return motion.LaneHullForward * FMath::Max(railSpeedMps, SOL::NPC_LANE_WALL_SPEED_MPS) + deviationVelocityMps;
}

//////////////////////////////////////////////////////////////////////////
// Returns the up a hull rolls to: a lane ship's lane top, else the radial up hint
FVector3d SOLNpcRail::HullUpHint(const FSOLNpcRailMotion& motion, const FVector3d& radialUpHint)
{
    return motion.LaneHullTop.IsZero() ? radialUpHint : motion.LaneHullTop;
}

//////////////////////////////////////////////////////////////////////////
// Writes a ship's GPU rail texels for an epoch deltaS later; a lane ship carries tau0 as hi / lo floats and T_leg
void SOLNpcRail::GpuRailTexels(const FSOLNpcRail& rail, const double deltaS,
    const TConstArrayView<FSOLLaneProfile> laneProfiles, FVector4f& outTexelA, FVector4f& outTexelB)
{
    // A lane rail with a profile derives its leg and cycle once (the re-base and the texels share them); every other
    // rail is re-based by AdvancedToEpoch
    const bool bLane = static_cast<SOLNpcScale::EPlacementKind>(rail.Kind) == SOLNpcScale::EPlacementKind::Lane
        && laneProfiles.IsValidIndex(rail.Group);
    double legS = 0.0;
    const double cycleS = bLane ? RailLaneCycleS(rail, laneProfiles, legS) : 0.0;
    FSOLNpcRail advanced = rail;
    if (!bLane)
    {
        advanced = AdvancedToEpoch(rail, deltaS, laneProfiles);
    }
    else if (FMath::IsFinite(deltaS) && RailIsCycleUsable(cycleS))
    {
        RailAdvanceLanePhase(rail, deltaS, cycleS, advanced);
    }
    const float packed = static_cast<float>(advanced.Kind + 4 * advanced.Faction + 8 * advanced.Group);
    outTexelA = FVector4f(packed, advanced.P[0], advanced.P[1], advanced.P[2]);
    outTexelB = FVector4f(advanced.P[3], advanced.P[4], advanced.P[5], advanced.P[6]);
    if (!bLane)
    {
        return;
    }

    // In double: the cycle time at the epoch (the CPU's tau = phase x T_cyc + t, from the re-based float phase) split
    // into hi / lo, and T_leg (the leg depends on dv and t_a only, so the re-base does not change it)
    const double tauS = RailIsCycleUsable(cycleS) ? LaneShipFromRail(advanced).Phase * cycleS : 0.0;
    const float tauHighS = static_cast<float>(tauS);
    outTexelA.Y = tauHighS;
    outTexelB.Z = static_cast<float>(tauS - static_cast<double>(tauHighS));
    const float legHighS = FMath::IsFinite(legS) ? static_cast<float>(legS) : 0.0f;
    outTexelB.W = legHighS;

    // T_leg's rounding as a fraction of the packed value: r / (2 ulp) lies within +-0.25 (r is at most half a step).
    // The largest packed integer must leave the fraction room in float (a step of 0.25 or finer needs it below 2^21)
    static_assert(8 * SOL::NPC_GROUP_TEXTURE_WIDTH < (1 << 21),
        "the packed kind / faction / group value must stay below 2^21 to carry T_leg's rounding as its fraction");
    if (legHighS > 0.0f)
    {
        const double fraction = (legS - static_cast<double>(legHighS))
            / (2.0 * static_cast<double>(GpuLegUlpS(legHighS)));
        outTexelA.X = static_cast<float>(static_cast<double>(packed) + FMath::Clamp(fraction, -0.25, 0.25));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the float step of a positive normal leg time as the shader computes it (exponent bits x 2^-23)
float SOLNpcRail::GpuLegUlpS(const float legS)
{
    return FMath::AsFloat(FMath::AsUInt(legS) & 0x7F800000u) * 1.1920928955078125e-7f;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's ship speed bound relative to body A (the wake check, before the subsystem's margin)
double SOLNpcRail::LaneWakeSpeedBoundMps(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame,
    const int32 lane, const double endpointRelativeSpeedMps)
{
    if (!groups.LaneProfile.IsValidIndex(lane) || !groups.LaneMaxPeakMps.IsValidIndex(lane)
        || !frame.LaneFrame.IsValidIndex(lane))
    {
        // Unknown lane: the fastest any lane ship can be
        return SOLLaneTraffic::LaneShipSpeedBoundMps(SOL::NPC_LANE_SPEED_CAP_MPS, 0.0, 0.0, 0.0,
            endpointRelativeSpeedMps);
    }
    const FSOLLaneProfile& profile = groups.LaneProfile[lane];
    return SOLLaneTraffic::LaneShipSpeedBoundMps(groups.LaneMaxPeakMps[lane], profile.NominalLegM,
        frame.LaneFrame[lane].CurrentLegM, profile.HaloOuterRadiusM, endpointRelativeSpeedMps);
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's Mid hand-over margin: a bound on how far the far layer's float position of any of its ships lies
// from the double rail position, seen from within the grown Mid radius (SDD 50 case 10's bound, per lane)
double SOLNpcRail::LaneHandOverMarginM(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame,
    const int32 lane, const double midRadiusM)
{
    if (!groups.LaneProfile.IsValidIndex(lane) || !frame.LaneFrame.IsValidIndex(lane) || !frame.LaneFrame[lane].bValid)
    {
        return 0.0;
    }

    // The float rounding of the lane's ends relative to a viewpoint near any of its ships (rows 0 and 1)
    const FSOLLaneProfile& profile = groups.LaneProfile[lane];
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[lane];
    const double endsM = (laneFrame.BM - laneFrame.AM).Size() + 4.0 * FMath::Max(midRadiusM, 0.0);
    double marginM = 4.0 * RailFloatUlp(endsM) + 1.0;

    // A drivable lane adds case 10's leg terms for its fastest ship: v_p ulp(T_leg) <= v_p T_leg 2^-23 = (l0 + dv t_a)
    // 2^-23 <= 2^-22 l0 (dv t_a <= 2 d_a <= l0) times the metric map's slope, the epoch constants' 4 ulp(l0), and the
    // shader time's rounding up to two epoch spans (time warp may run past one before the re-base)
    if (profile.bDrivable)
    {
        const double l0 = profile.NominalLegM;
        const double slope = FMath::Max(1.0, (laneFrame.CurrentLegM - profile.MetricZoneM)
            / (l0 - profile.MetricZoneM));
        const double maxPeakMps = groups.LaneMaxPeakMps.IsValidIndex(lane) ? groups.LaneMaxPeakMps[lane]
            : SOL::NPC_LANE_SPEED_CAP_MPS;
        marginM += slope * 2.0 * RAIL_FLOAT_EPSILON * l0 + 4.0 * RailFloatUlp(l0)
            + maxPeakMps * RailFloatUlp(2.0 * SOL::NPC_GPU_RAIL_EPOCH_S);
    }
    return FMath::IsFinite(marginM) ? marginM : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Writes the lane traffic rows 2-5 of the group texels
void SOLNpcRail::WriteLaneTrafficTexels(const FSOLNpcRailFrame& frame, TArrayView<FVector4f> groupTexels)
{
    constexpr int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    if (groupTexels.Num() < width * SOL::NPC_GROUP_TEXTURE_ROWS)
    {
        return;
    }
    const FVector4f zero(0.0f, 0.0f, 0.0f, 0.0f);
    const int32 laneCount = FMath::Min(frame.LaneFrame.Num(), frame.LaneProfile.Num());
    for (int32 column = 0; column < width; ++column)
    {
        FVector4f rows[4] = { zero, zero, zero, zero };
        const FSOLLaneFrame* laneFrame = column < laneCount ? &frame.LaneFrame[column] : nullptr;
        if (laneFrame != nullptr && laneFrame->bValid)
        {
            // The basis rows; the leg rows only for a drivable lane (l0 = 0 holds the ship at rest)
            const FSOLLaneProfile& profile = frame.LaneProfile[column];
            rows[0] = FVector4f(FVector3f(laneFrame->Up), static_cast<float>(SOL::NPC_LANE_WALL_SPEED_MPS));
            rows[1] = FVector4f(FVector3f(laneFrame->Right), static_cast<float>(profile.UTurnDepthM));
            if (profile.bDrivable)
            {
                // l - l0 in double (the shader never subtracts two large floats) and the per-lane reciprocals
                const double l0 = profile.NominalLegM;
                const double z = profile.MetricZoneM;
                const double stretchM = laneFrame->CurrentLegM - l0;
                rows[2] = FVector4f(static_cast<float>(profile.UTurnS > 0.0 ? UE_DOUBLE_PI / profile.UTurnS : 0.0),
                    static_cast<float>(profile.EndArcAM), static_cast<float>(l0), static_cast<float>(z));
                rows[3] = FVector4f(static_cast<float>(profile.EndArcBM - laneFrame->EndShiftM),
                    static_cast<float>(profile.UTurnS), static_cast<float>(stretchM / (l0 - z)),
                    static_cast<float>(z > 0.0 ? 1.0 / z : 0.0));
            }
        }
        for (int32 row = 0; row < 4; ++row)
        {
            groupTexels[(row + 2) * width + column] = rows[row];
        }
    }
}

// Part 53c2: lane network slots in the rail and group textures (contract cases 19, 20, 22)

//////////////////////////////////////////////////////////////////////////
// Writes the at most three rects (head row part, full rows, tail row part) of a linear texel span
void SOLNpcRail::RailTexelRegions(const int32 firstTexel, const int32 texelCount, const int32 textureWidth,
    TArray<FSOLTexelRegion>& outRegions)
{
    outRegions.Reset();
    if (texelCount <= 0 || textureWidth <= 0 || firstTexel < 0)
    {
        return;
    }

    // The head: the part of the first row from the first texel, unless the span starts a row
    int64 next = firstTexel;
    const int64 end = static_cast<int64>(firstTexel) + texelCount;
    const int32 headX = static_cast<int32>(next % textureWidth);
    if (headX != 0)
    {
        const int32 width = static_cast<int32>(FMath::Min<int64>(textureWidth - headX, end - next));
        outRegions.Add(FSOLTexelRegion{headX, static_cast<int32>(next / textureWidth), width, 1});
        next += width;
    }

    // The full rows in one rect, then the tail part row
    const int64 fullRows = (end - next) / textureWidth;
    if (fullRows > 0)
    {
        outRegions.Add(FSOLTexelRegion{0, static_cast<int32>(next / textureWidth), textureWidth,
            static_cast<int32>(fullRows)});
        next += fullRows * textureWidth;
    }
    if (next < end)
    {
        outRegions.Add(FSOLTexelRegion{0, static_cast<int32>(next / textureWidth), static_cast<int32>(end - next), 1});
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane slot's rail group, its group column; NPC_RAIL_PARKED_GROUP for no slot or one past the capacity
uint16 SOLNpcRail::LaneRailGroup(const FSOLNpcGroupTable& groups, const int32 slot)
{
    const int32 column = groups.LaneSlotColumn(slot);
    return column == INDEX_NONE ? SOL::NPC_RAIL_PARKED_GROUP : static_cast<uint16>(column);
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane spawn's rail on its slot's column with that column's profile, parked for no slot
FSOLNpcRail SOLNpcRail::LaneRailFromSpawn(const SOLNpcScale::FSpawn& spawn, const FSOLNpcGroupTable& groups)
{
    const uint16 group = LaneRailGroup(groups, spawn.GroupIndex);
    const bool bParked = group == SOL::NPC_RAIL_PARKED_GROUP;
    FSOLNpcRail rail = FromSpawn(spawn, 0, !bParked && groups.LaneProfile.IsValidIndex(group)
        ? groups.LaneProfile[group] : FSOLLaneProfile(), 0.0);
    rail.Group = group;
    return rail;
}

//////////////////////////////////////////////////////////////////////////
// Returns the rail time of a sim time: seconds since J2000 less the population's spawn epoch
double SOLNpcRail::RailTimeS(const double secondsSinceJ2000, const double epochSecondsSinceJ2000)
{
    return secondsSinceJ2000 - epochSecondsSinceJ2000;
}

//////////////////////////////////////////////////////////////////////////
// Writes a slot's J2000 window in rail time at its column of the two arrays
void SOLNpcRail::SetSlotWindowRailS(const FSOLNpcGroupTable& groups, const int32 slot, const double openStartS,
    const double closeS, const double epochSecondsSinceJ2000, TArrayView<double> outOpenStartRailS,
    TArrayView<double> outCloseRailS)
{
    const int32 column = groups.LaneSlotColumn(slot);
    if (!outOpenStartRailS.IsValidIndex(column) || !outCloseRailS.IsValidIndex(column))
    {
        return;
    }
    outOpenStartRailS[column] = RailTimeS(openStartS, epochSecondsSinceJ2000);
    outCloseRailS[column] = RailTimeS(closeS, epochSecondsSinceJ2000);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a rail is in space: a lane rail flying a leg inside its window, any other kind always
bool SOLNpcRail::LaneRailInSpace(const FSOLNpcRail& rail, const FSOLLaneProfile& profile, const double railTimeS,
    const double openStartRailS, const double closeRailS)
{
    if (static_cast<SOLNpcScale::EPlacementKind>(rail.Kind) != SOLNpcScale::EPlacementKind::Lane)
    {
        return true;
    }
    const FSOLLaneShip ship = LaneShipFromRail(rail);
    const FSOLLaneCycle cycle = SOLLaneTraffic::MakeCycle(SOLLaneTraffic::LaneShipLegS(profile, ship),
        profile.UTurnDepthM);
    return SOLLaneNetwork::LaneShipInSpace(cycle, ship.Phase, railTimeS, openStartRailS, closeRailS);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a rail is a parked lane ship (its block is in the free pool)
bool SOLNpcRail::IsParked(const FSOLNpcRail& rail)
{
    return rail.Kind == static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane)
        && rail.Group == SOL::NPC_RAIL_PARKED_GROUP;
}

//////////////////////////////////////////////////////////////////////////
// Writes the two GPU rail texels of a parked lane ship (the shader hides a negative leg time)
void SOLNpcRail::GpuParkedRailTexels(const int32 faction, FVector4f& outTexelA, FVector4f& outTexelB)
{
    // Packed as GpuRailTexels packs it, with group 0 and no leg-time fraction
    const float packed = static_cast<float>(static_cast<int32>(SOLNpcScale::EPlacementKind::Lane) + 4 * faction);
    outTexelA = FVector4f(packed, 0.0f, 0.0f, 0.0f);
    outTexelB = FVector4f(0.0f, 0.0f, 0.0f, static_cast<float>(SOL::NPC_LANE_SHIP_PARKED_LEG_S));
}

//////////////////////////////////////////////////////////////////////////
// Writes row NPC_GROUP_WINDOW_ROW of the group texels: each slot's window texel at its column, other columns zero
void SOLNpcRail::WriteLaneWindowRow(TConstArrayView<FVector4f> windowBySlot, const FSOLNpcGroupTable& groups,
    TArrayView<FVector4f> groupTexels)
{
    const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    if (groupTexels.Num() < width * SOL::NPC_GROUP_TEXTURE_ROWS)
    {
        return;
    }
    FVector4f* const row = groupTexels.GetData() + SOL::NPC_GROUP_WINDOW_ROW * width;
    for (int32 column = 0; column < width; ++column)
    {
        row[column] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
    }
    for (int32 slot = 0; slot < windowBySlot.Num(); ++slot)
    {
        const int32 column = groups.LaneSlotColumn(slot);
        if (column >= 0 && column < width)
        {
            row[column] = windowBySlot[slot];
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a slot's lane profile frozen at its episode open (MakeLaneProfile of the tunnel radius of its ships)
FSOLLaneProfile SOLNpcRail::SlotLaneProfile(const double laneRadiusM, const int32 inWallCount,
    const double nominalLengthM, const double bodyRadiusAM, const double bodyRadiusBM)
{
    return SOLLaneTraffic::MakeLaneProfile(SOLNpcTunnel::TunnelRadiusM(laneRadiusM, inWallCount), nominalLengthM,
        bodyRadiusAM, bodyRadiusBM);
}
