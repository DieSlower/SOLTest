/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcRail.h"

//////////////////////////////////////////////////////////////////////////
// Packs the kind-specific spawn fields into the rail's float parameters
FSOLNpcRail SOLNpcRail::FromSpawn(const SOLNpcScale::FSpawn& spawn, int32 groupBase, double laneLengthM,
    double bodyGM)
{
    FSOLNpcRail rail;
    rail.Kind = static_cast<uint8>(spawn.Kind);
    rail.Faction = static_cast<uint8>(spawn.Faction);
    rail.Group = static_cast<uint16>(FMath::Max(groupBase + spawn.GroupIndex, 0));
    switch (spawn.Kind)
    {
    case SOLNpcScale::EPlacementKind::Lane:
    {
        const double rate = laneLengthM > 0.0 ? spawn.LaneDirection * spawn.LaneSpeedMps / laneLengthM : 0.0;
        rail.P[0] = static_cast<float>(spawn.LaneStartParam);
        rail.P[1] = static_cast<float>(rate);
        rail.P[2] = static_cast<float>(spawn.LaneLateralOffsetM.X);
        rail.P[3] = static_cast<float>(spawn.LaneLateralOffsetM.Y);
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
    double railTimeS, FSOLNpcRailFrame& outFrame)
{
    outFrame.RailTimeS = railTimeS;
    outFrame.GroupPointAM.SetNumUninitialized(groups.Num(), EAllowShrinking::No);
    outFrame.GroupPointBM.SetNumUninitialized(groups.Num(), EAllowShrinking::No);

    // Returns a body's position, or the universe origin for an unknown index
    const auto bodyAt = [&bodyPositionsM](int32 body)
    {
        return bodyPositionsM.IsValidIndex(body) ? bodyPositionsM[body] : FVector3d::ZeroVector;
    };

    int32 group = 0;
    for (int32 lane = 0; lane < groups.LaneCount; ++lane, ++group)
    {
        outFrame.GroupPointAM[group] = bodyAt(groups.LaneBodyA[lane]);
        outFrame.GroupPointBM[group] = bodyAt(groups.LaneBodyB[lane]);
    }
    for (int32 cluster = 0; cluster < groups.ClusterCount; ++cluster, ++group)
    {
        outFrame.GroupPointAM[group] = bodyAt(groups.ClusterBody[cluster]);
        outFrame.GroupPointBM[group] = outFrame.GroupPointAM[group];
    }
    const FVector3d battleBody = bodyAt(groups.BattleBody);
    for (int32 battle = 0; battle < groups.BattleCount; ++battle, ++group)
    {
        outFrame.GroupPointAM[group] = battleBody + groups.BattleCenterFromBodyM[battle];
        outFrame.GroupPointBM[group] = outFrame.GroupPointAM[group];
    }
}

//////////////////////////////////////////////////////////////////////////
// Evaluates a lane, cluster or battle rail in double precision from its float parameters
FVector3d SOLNpcRail::EvaluateM(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame)
{
    const FVector3d& pointA = frame.GroupPointAM[rail.Group];
    switch (static_cast<SOLNpcScale::EPlacementKind>(rail.Kind))
    {
    case SOLNpcScale::EPlacementKind::Lane:
    {
        const double s = SOLNpcScale::TriangleWave01(static_cast<double>(rail.P[0])
            + static_cast<double>(rail.P[1]) * frame.RailTimeS);
        return SOLNpcScale::LanePositionM(pointA, frame.GroupPointBM[rail.Group], s,
            FVector2d(rail.P[2], rail.P[3]));
    }
    case SOLNpcScale::EPlacementKind::Cluster:
    {
        // Circular orbit: r (cos theta P + sin theta Q), as SOLNpcScale::ClusterOffsetM
        const double theta = static_cast<double>(rail.P[3]) + static_cast<double>(rail.P[4]) * frame.RailTimeS;
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
// Advances the lane parameter or the cluster phase by rate * deltaS and wraps it into one period
FSOLNpcRail SOLNpcRail::AdvancedToEpoch(const FSOLNpcRail& rail, const double deltaS)
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
        // The triangle wave has period 2
        const double param = static_cast<double>(rail.P[0]) + static_cast<double>(rail.P[1]) * deltaS;
        advanced.P[0] = static_cast<float>(param - 2.0 * FMath::FloorToDouble(param * 0.5));
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
    if (group < groups.LaneCount)
    {
        return SOLNpcScale::DistanceToLaneM(pointM, frame.GroupPointAM[group], frame.GroupPointBM[group],
            groups.LaneRadiusM[group]);
    }
    const int32 cluster = group - groups.LaneCount;
    if (cluster < groups.ClusterCount)
    {
        return SOLNpcScale::DistanceToShellM(pointM, frame.GroupPointAM[group], groups.ClusterInnerM[cluster],
            groups.ClusterOuterM[cluster]);
    }
    return SOLNpcScale::DistanceToShellM(pointM, frame.GroupPointAM[group], 0.0, groups.BattleRadiusM);
}
