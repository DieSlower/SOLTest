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
    // Returns a frame with a lane from the origin to (length, 0, 0) (group 0), a cluster body (group 1) and a battle
    // centre (group 2), at a rail time
    FSOLNpcRailFrame NpcRailFrame(const double laneLengthM, const double railTimeS)
    {
        FSOLNpcRailFrame frame;
        frame.RailTimeS = railTimeS;
        frame.GroupPointAM = { FVector3d::ZeroVector, FVector3d(1.0e11, 2.0e10, 0.0), FVector3d(5.0e10, 0.0, 0.0) };
        frame.GroupPointBM = { FVector3d(laneLengthM, 0.0, 0.0), frame.GroupPointAM[1], frame.GroupPointAM[2] };
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
