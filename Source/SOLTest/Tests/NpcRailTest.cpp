/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcRail.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a lane rail (group 0) with a start parameter and a rate (1/s)
    FSOLNpcRail NpcRailLane(const float startParam, const float rate)
    {
        FSOLNpcRail rail;
        rail.Kind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
        rail.Group = 0;
        rail.P[0] = startParam;
        rail.P[1] = rate;
        rail.P[2] = 1.0e5f;
        rail.P[3] = -2.0e5f;
        return rail;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a cluster rail (group 1) on a circular orbit of radius r with a phase and a mean motion (rad/s)
    FSOLNpcRail NpcRailCluster(const float radiusM, const float phaseRad, const float meanMotion)
    {
        FSOLNpcRail rail;
        rail.Kind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Cluster);
        rail.Group = 1;
        rail.P[0] = radiusM;
        rail.P[1] = 0.4f;
        rail.P[2] = 1.1f;
        rail.P[3] = phaseRad;
        rail.P[4] = meanMotion;
        return rail;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a battle rail (group 2) with a fixed slot offset from the battle centre
    FSOLNpcRail NpcRailBattle()
    {
        FSOLNpcRail rail;
        rail.Kind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Battle);
        rail.Group = 2;
        rail.P[0] = 120.0f;
        rail.P[1] = -40.0f;
        rail.P[2] = 9.0f;
        return rail;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a frame with a lane from the origin to (length, 0, 0) (group 0), a cluster body (group 1) and a battle
    // centre (group 2), at a rail time; the group body points are the lane's body A, the cluster body and a battle body
    FSOLNpcRailFrame NpcRailFrame(const double laneLengthM, const double railTimeS)
    {
        FSOLNpcRailFrame frame;
        frame.RailTimeS = railTimeS;
        frame.GroupPointAM = { FVector3d::ZeroVector, FVector3d(1.0e11, 2.0e10, 0.0), FVector3d(5.0e10, 0.0, 0.0) };
        frame.GroupPointBM = { FVector3d(laneLengthM, 0.0, 0.0), frame.GroupPointAM[1], frame.GroupPointAM[2] };
        frame.GroupBodyM = { FVector3d::ZeroVector, frame.GroupPointAM[1], FVector3d(4.9e10, 0.0, 0.0) };
        return frame;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a cluster ship's in-orbit angle as the GPU computes it: phase + mean motion * time, all in float
    float NpcRailGpuTheta(const FSOLNpcRail& rail, const double railTimeS)
    {
        return rail.P[3] + rail.P[4] * static_cast<float>(railTimeS);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailEpochMatchesTest, "SOLTest.NpcRail.EpochMatches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A rail re-based by delta, evaluated at t, lands where the original lands at t + delta (up to the float rounding of
// the wrapped phase), and its phase stays inside one period
bool FSOLNpcRailEpochMatchesTest::RunTest(const FString& /*parameters*/)
{
    const double laneLengthM = 3.8e8;
    const FSOLNpcRail lane = NpcRailLane(0.37f, 4.0e-4f);
    const FSOLNpcRail cluster = NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f);
    FSOLNpcRail battle;
    battle.Kind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Battle);
    battle.Group = 2;
    battle.P[0] = 120.0f;
    battle.P[1] = -40.0f;
    battle.P[2] = 9.0f;

    for (const double deltaS : { 0.0, 1.0e4, 3.3e5, 1.0e7, -5.0e3 })
    {
        const FSOLNpcRail laneAdvanced = SOLNpcRail::AdvancedToEpoch(lane, deltaS);
        const FSOLNpcRail clusterAdvanced = SOLNpcRail::AdvancedToEpoch(cluster, deltaS);
        TestTrue(*FString::Printf(TEXT("delta %.0f: lane parameter in one period"), deltaS),
            laneAdvanced.P[0] >= 0.0f && laneAdvanced.P[0] <= 2.0f);
        TestTrue(*FString::Printf(TEXT("delta %.0f: cluster phase in one period"), deltaS),
            clusterAdvanced.P[3] >= 0.0f && clusterAdvanced.P[3] <= static_cast<float>(UE_DOUBLE_TWO_PI));
        TestEqual(TEXT("Rates are kept"), laneAdvanced.P[1], lane.P[1]);
        TestEqual(TEXT("Mean motion is kept"), clusterAdvanced.P[4], cluster.P[4]);

        for (const double t : { 0.0, 250.0, 9000.0 })
        {
            const FSOLNpcRailFrame later = NpcRailFrame(laneLengthM, t + deltaS);
            const FSOLNpcRailFrame local = NpcRailFrame(laneLengthM, t);

            // Lane: half a float ulp of a parameter in [0, 2) times the lane length
            const double laneErrorM = (SOLNpcRail::EvaluateM(laneAdvanced, local)
                - SOLNpcRail::EvaluateM(lane, later)).Size();
            TestTrue(*FString::Printf(TEXT("delta %.0f t %.0f: lane within %.1f m (%.2f m)"), deltaS, t,
                laneLengthM * 2.4e-7, laneErrorM), laneErrorM <= laneLengthM * 2.4e-7);

            // Cluster: half a float ulp of a phase in [0, 2 pi) times the radius
            const double clusterErrorM = (SOLNpcRail::EvaluateM(clusterAdvanced, local)
                - SOLNpcRail::EvaluateM(cluster, later)).Size();
            TestTrue(*FString::Printf(TEXT("delta %.0f t %.0f: cluster within 4 m (%.3f m)"), deltaS, t,
                clusterErrorM), clusterErrorM <= 4.0);

            // Battle: unchanged
            TestEqual(TEXT("Battle rail unchanged"), SOLNpcRail::EvaluateM(SOLNpcRail::AdvancedToEpoch(battle, deltaS),
                local), SOLNpcRail::EvaluateM(battle, local));
        }
    }

    // A non-finite delta leaves the rail as it was
    const FSOLNpcRail same = SOLNpcRail::AdvancedToEpoch(cluster, std::numeric_limits<double>::quiet_NaN());
    TestEqual(TEXT("NaN delta keeps the phase"), same.P[3], cluster.P[3]);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailEvaluateAtTimeTest, "SOLTest.NpcRail.EvaluateAtTimeMatches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// EvaluateAtTimeM at the frame's rail time is EvaluateM exactly, and at another time it matches a frame with that rail
// time and the same group points (issue #18 Part 18c contract item 10)
bool FSOLNpcRailEvaluateAtTimeTest::RunTest(const FString& /*parameters*/)
{
    const double laneLengthM = 3.8e8;
    const FSOLNpcRail rails[] = { NpcRailLane(0.37f, 4.0e-4f), NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f),
        NpcRailBattle() };
    const TCHAR* names[] = { TEXT("Lane"), TEXT("Cluster"), TEXT("Battle") };
    for (int32 railIndex = 0; railIndex < 3; ++railIndex)
    {
        const FSOLNpcRail& rail = rails[railIndex];
        for (const double t : { 0.0, 250.0, 3000.0, 9000.0 })
        {
            const FSOLNpcRailFrame frame = NpcRailFrame(laneLengthM, t);
            const bool bExact = SOLNpcRail::EvaluateAtTimeM(rail, frame, frame.RailTimeS)
                == SOLNpcRail::EvaluateM(rail, frame);
            TestTrue(*FString::Printf(TEXT("%s t %.0f: EvaluateAtTimeM(RailTimeS) == EvaluateM exactly"),
                names[railIndex], t), bExact);

            // Group points held, time moved: the same as a frame at that time (identical inputs, so rounding-level)
            for (const double otherS : { t + 0.5, t - 0.5, t + 1234.5 })
            {
                const FVector3d atOther = SOLNpcRail::EvaluateAtTimeM(rail, frame, otherS);
                const FVector3d viaFrame = SOLNpcRail::EvaluateM(rail, NpcRailFrame(laneLengthM, otherS));
                TestTrue(*FString::Printf(TEXT("%s t %.0f: EvaluateAtTimeM(%.1f) == EvaluateM of that frame"),
                    names[railIndex], t, otherS),
                    (atOther - viaFrame).Size() <= 1.0e-12 * FMath::Max(1.0, viaFrame.Size()));
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailVelocityTest, "SOLTest.NpcRail.RailVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rail velocity is the central difference at RAIL_VELOCITY_STEP_S: zero for a battle, tangential at r * n for a
// cluster, (B - A) * rate * (+-1) for a lane away from a turnaround (issue #18 Part 18c contract item 11)
bool FSOLNpcRailVelocityTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("RAIL_VELOCITY_STEP_S"), SOLNpcRail::RAIL_VELOCITY_STEP_S, 0.5);
    const double laneLengthM = 3.8e8;
    const double h = SOLNpcRail::RAIL_VELOCITY_STEP_S;

    // Returns an independent central difference of EvaluateAtTimeM at the frame's time with step stepS
    const auto centralDifference = [](const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame, const double stepS)
    {
        return (SOLNpcRail::EvaluateAtTimeM(rail, frame, frame.RailTimeS + stepS)
            - SOLNpcRail::EvaluateAtTimeM(rail, frame, frame.RailTimeS - stepS)) / (2.0 * stepS);
    };

    // Battle: exactly zero
    const FSOLNpcRail battle = NpcRailBattle();
    for (const double t : { 0.0, 250.0, 1.0e6 })
    {
        TestTrue(*FString::Printf(TEXT("Battle t %.0f: exactly zero"), t),
            SOLNpcRail::VelocityMps(battle, NpcRailFrame(laneLengthM, t)) == FVector3d::ZeroVector);
    }

    // Cluster: tangential, |v| = r * n within 0.1% (the central difference shortens it by sin(nh) / (nh) ~ 1 - 5e-8)
    const FSOLNpcRail cluster = NpcRailCluster(7.0e6f, 1.3f, 1.1e-3f);
    const double expectedSpeed = static_cast<double>(cluster.P[0]) * static_cast<double>(cluster.P[4]);
    for (const double t : { 0.0, 250.0, 4321.0 })
    {
        const FSOLNpcRailFrame frame = NpcRailFrame(laneLengthM, t);
        const FVector3d velocity = SOLNpcRail::VelocityMps(cluster, frame);
        const FVector3d radius = SOLNpcRail::EvaluateM(cluster, frame) - frame.GroupPointAM[1];
        const FString name = FString::Printf(TEXT("Cluster t %.0f"), t);
        TestTrue(*(name + TEXT(": |v| = r * n within 0.1%")),
            FMath::Abs(velocity.Size() - expectedSpeed) <= 1.0e-3 * expectedSpeed);

        // A symmetric chord of a circle is perpendicular to the radius; 1e-6 covers rounding at 1e11 m
        TestTrue(*(name + TEXT(": perpendicular to the radius")),
            FMath::Abs(FVector3d::DotProduct(velocity.GetSafeNormal(), radius.GetSafeNormal())) <= 1.0e-6);
        TestTrue(*(name + TEXT(": the definition (central difference at RAIL_VELOCITY_STEP_S)")),
            (velocity - centralDifference(cluster, frame, h)).Size() <= 1.0e-9 * expectedSpeed);

        // A finer independent difference differs only by the O((nh)^2) truncation (~5e-8 relative) and rounding
        TestTrue(*(name + TEXT(": matches a finer difference")),
            (velocity - centralDifference(cluster, frame, 0.05)).Size() <= 1.0e-6 * expectedSpeed);
    }

    // Lane: (B - A) * P1 * (+-1); at t = 100 the parameter is ~0.41 and at t = 3000 ~1.57, both far (> 0.4) from a
    // turnaround compared with the step's parameter change P1 * h = 2e-4
    const FSOLNpcRail lane = NpcRailLane(0.37f, 4.0e-4f);
    const FVector3d laneVelocity = FVector3d(laneLengthM, 0.0, 0.0) * static_cast<double>(lane.P[1]);
    for (const double t : { 100.0, 3000.0 })
    {
        const FSOLNpcRailFrame frame = NpcRailFrame(laneLengthM, t);
        const FVector3d velocity = SOLNpcRail::VelocityMps(lane, frame);
        const FString name = FString::Printf(TEXT("Lane t %.0f"), t);
        const double error = FMath::Min((velocity - laneVelocity).Size(), (velocity + laneVelocity).Size());
        TestTrue(*(name + TEXT(": (B - A) * rate * (+-1) within 1e-6 relative")),
            error <= 1.0e-6 * laneVelocity.Size());
        TestTrue(*(name + TEXT(": the definition (central difference at RAIL_VELOCITY_STEP_S)")),
            (velocity - centralDifference(lane, frame, h)).Size() <= 1.0e-9 * laneVelocity.Size());
        TestTrue(*(name + TEXT(": matches a finer difference")),
            (velocity - centralDifference(lane, frame, 0.05)).Size() <= 1.0e-6 * laneVelocity.Size());
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailBuildFrameGroupBodyTest, "SOLTest.NpcRail.BuildFrameGroupBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BuildFrame fills GroupBodyM: lane = body A, cluster = its body, battle = the battle body, zero for an invalid
// index (issue #18 Part 18c contract item 12)
bool FSOLNpcRailBuildFrameGroupBodyTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> bodies = { FVector3d(1.0, 2.0, 3.0), FVector3d(1.0e11, -4.0e10, 5.0e8),
        FVector3d(-2.0e11, 7.0e10, -1.0e9), FVector3d(3.0e10, 3.0e10, 3.0e10) };

    // Two lanes (the second with an unknown body A), one cluster, one battle
    FSOLNpcGroupTable groups;
    groups.LaneCount = 2;
    groups.ClusterCount = 1;
    groups.BattleCount = 1;
    groups.LaneBodyA = { 1, INDEX_NONE };
    groups.LaneBodyB = { 2, 0 };
    groups.LaneRadiusM = { 1.0e5, 1.0e5 };
    groups.ClusterBody = { 3 };
    groups.ClusterInnerM = { 7.0e6 };
    groups.ClusterOuterM = { 9.0e6 };
    groups.BattleCenterFromBodyM = { FVector3d(1.0e5, 0.0, 0.0) };
    groups.BattleBody = 2;
    groups.BattleRadiusM = 2.0e4;

    FSOLNpcRailFrame frame;
    SOLNpcRail::BuildFrame(groups, bodies, 100.0, frame);
    TestEqual(TEXT("One body point per group"), frame.GroupBodyM.Num(), groups.Num());
    if (frame.GroupBodyM.Num() == 4)
    {
        TestTrue(TEXT("Lane 0: body A"), frame.GroupBodyM[0] == bodies[1]);
        TestTrue(TEXT("Lane 1: unknown body A gives zero"), frame.GroupBodyM[1] == FVector3d::ZeroVector);
        TestTrue(TEXT("Cluster: its body"), frame.GroupBodyM[2] == bodies[3]);
        TestTrue(TEXT("Battle: the battle body"), frame.GroupBodyM[3] == bodies[2]);
    }

    // Out-of-range and missing indices give zero; rebuilding into the same frame does not append
    groups.LaneBodyA = { 7, 0 };
    groups.ClusterBody = { 99 };
    groups.BattleBody = INDEX_NONE;
    SOLNpcRail::BuildFrame(groups, bodies, 200.0, frame);
    TestEqual(TEXT("Rebuilt: one body point per group"), frame.GroupBodyM.Num(), groups.Num());
    if (frame.GroupBodyM.Num() == 4)
    {
        TestTrue(TEXT("Lane 0: out-of-range body A gives zero"), frame.GroupBodyM[0] == FVector3d::ZeroVector);
        TestTrue(TEXT("Lane 1: body A"), frame.GroupBodyM[1] == bodies[0]);
        TestTrue(TEXT("Cluster: out-of-range body gives zero"), frame.GroupBodyM[2] == FVector3d::ZeroVector);
        TestTrue(TEXT("Battle: no battle body gives zero"), frame.GroupBodyM[3] == FVector3d::ZeroVector);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcRailGpuPrecisionTest, "SOLTest.NpcRail.GpuFloatPrecision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Measurement behind NPC_GPU_RAIL_EPOCH_S: for the fastest cluster orbit (low Earth orbit), the GPU's float angle
// drifts kilometres from the double one some weeks of sim time after the epoch, and stays within metres while the
// rail time is kept within NPC_GPU_RAIL_EPOCH_S of a re-based epoch
bool FSOLNpcRailGpuPrecisionTest::RunTest(const FString& /*parameters*/)
{
    const double radiusM = 7.0e6;
    const FSOLNpcRail rail = NpcRailCluster(static_cast<float>(radiusM), 0.9f, 1.08e-3f);

    // Returns the worst position error (m) of the float angle over rail times [start, start + span]
    const auto worstErrorM = [&radiusM](const FSOLNpcRail& gpuRail, const double startS, const double spanS)
    {
        double worst = 0.0;
        for (int32 sample = 0; sample <= 200; ++sample)
        {
            const double t = startS + spanS * sample / 200.0;
            const double exact = static_cast<double>(gpuRail.P[3]) + static_cast<double>(gpuRail.P[4]) * t;
            const double gpu = static_cast<double>(NpcRailGpuTheta(gpuRail, t));
            const double wrapped = FMath::Abs(FMath::Fmod(gpu - exact + UE_DOUBLE_PI * 3.0, UE_DOUBLE_TWO_PI)
                - UE_DOUBLE_PI);
            worst = FMath::Max(worst, wrapped * radiusM);
        }
        return worst;
    };

    // Without re-basing: some 46 sim days in, the float angle is off by kilometres
    const double driftM = worstErrorM(rail, 4.0e6, SOL::NPC_GPU_RAIL_EPOCH_S);
    AddInfo(FString::Printf(TEXT("Float angle error 4e6 s after the epoch: %.0f m"), driftM));
    TestTrue(TEXT("Without re-basing the GPU drifts by more than 1 km"), driftM > 1000.0);

    // Re-based at 4e6 s: the GPU sees only the time since that epoch
    const FSOLNpcRail rebased = SOLNpcRail::AdvancedToEpoch(rail, 4.0e6);
    const double rebasedM = worstErrorM(rebased, 0.0, SOL::NPC_GPU_RAIL_EPOCH_S);
    AddInfo(FString::Printf(TEXT("Float angle error within one epoch window: %.1f m"), rebasedM));
    TestTrue(TEXT("Within one epoch window the GPU stays within 50 m"), rebasedM < 50.0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
