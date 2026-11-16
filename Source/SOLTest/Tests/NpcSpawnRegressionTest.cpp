/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcScaleMath.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Issue #50 part 50b regression (SDD 50 section 4.7 and contract case 9): the lane spawn branch changes, the battle and
// cluster branches must not. Three battle, three cluster and three lane samples of SOLNpcScale::MakeSpawn are compared
// bit for bit with values captured from the pre-#50 build (commit a2fe894); for lane ships only the group and faction
// streams, which issue #50 keeps. This file uses only FSpawn fields that exist before and after part 50b, so it builds
// on the old code alone.
//
// Capture protocol (once, before MakeSpawn is changed): build the pre-#50 code with only this file added, run
// `Tools\RunTests.bat -Filter SOLTest.NpcSpawnRegression`, paste the three logged tables over the zero rows below and
// set NPCSPAWNREG_CAPTURED to true. Until then the test fails on purpose.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // True once the tables below hold the values captured from the pre-#50 build
    constexpr bool NPCSPAWNREG_CAPTURED = true;

    // Seed of the regression population
    constexpr uint32 NPCSPAWNREG_SEED = 20261113u;

    // One captured spawn: battle V = slot offset x, y, z and 0; cluster V = orbit radius, inclination, node, phase;
    // lane V unused (0)
    struct FNpcSpawnRegSample
    {
        int32 NpcIndex;
        int32 Group;
        int32 Faction;
        double V[4];
    };

    // Captured battle samples (indices 0, 417, 999)
    const FNpcSpawnRegSample NPCSPAWNREG_BATTLE[] = {
        { 0, 0, 0, { 451.38937908791621, 830.35290924062758, -343.68408330018565, 0 } },
        { 417, 1, 0, { -2201.168250611795, 947.72685324917791, 158.22784483637591, 0 } },
        { 999, 3, 1, { -1684.1238956949821, -863.78559991495399, 559.76185142664133, 0 } },
    };

    // Captured lane samples (indices 1000, 1500, 1999): group and faction only
    const FNpcSpawnRegSample NPCSPAWNREG_LANE[] = {
        { 1000, 1, 1, { 0, 0, 0, 0 } },
        { 1500, 1, 0, { 0, 0, 0, 0 } },
        { 1999, 0, 1, { 0, 0, 0, 0 } },
    };

    // Captured cluster samples (indices 2000, 2468, 2999)
    const FNpcSpawnRegSample NPCSPAWNREG_CLUSTER[] = {
        { 2000, 1, 0, { 368497755331.91858, 0.059063780214637519, 0.36995560195293109, 5.4310465681121283 } },
        { 2468, 1, 1, { 470522134285.42017, 0.10189762194640935, 4.102934802583321, 4.5188995672654135 } },
        { 2999, 1, 1, { 401909958058.9682, 0.13777048499323427, 4.7080503647382193, 5.7828160224590137 } },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the regression layout: two lanes (weights 1 and 3) and two clusters (weights 1 and 1), a frozen copy so a
    // change to the default layout does not move the samples
    SOLNpcScale::FLayout NpcSpawnRegLayout()
    {
        SOLNpcScale::FLayout layout;
        SOLNpcScale::FLaneDef laneA;
        laneA.BodyA = FName(TEXT("Earth"));
        laneA.BodyB = FName(TEXT("Mars"));
        laneA.RadiusM = 1000.0;
        laneA.Weight = 1.0;
        SOLNpcScale::FLaneDef laneB;
        laneB.BodyA = FName(TEXT("Mars"));
        laneB.BodyB = FName(TEXT("Jupiter"));
        laneB.RadiusM = 5000.0;
        laneB.Weight = 3.0;
        layout.Lanes = { laneA, laneB };

        SOLNpcScale::FClusterDef clusterA;
        clusterA.Body = FName(TEXT("Earth"));
        clusterA.InnerRadiusM = 1.0e7;
        clusterA.OuterRadiusM = 5.0e7;
        clusterA.MaxInclinationRad = 0.5;
        clusterA.Weight = 1.0;
        SOLNpcScale::FClusterDef clusterB;
        clusterB.Body = FName(TEXT("Sun"));
        clusterB.InnerRadiusM = 3.0e11;
        clusterB.OuterRadiusM = 5.0e11;
        clusterB.MaxInclinationRad = 0.2;
        clusterB.Weight = 1.0;
        layout.Clusters = { clusterA, clusterB };
        return layout;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the regression battle parameters (4 battles, 3 km radius, 5-20 km from the player)
    SOLNpcScale::FBattleParams NpcSpawnRegBattle()
    {
        SOLNpcScale::FBattleParams battle;
        battle.BattleCount = 4;
        battle.RadiusM = 3000.0;
        battle.MinDistanceM = 5000.0;
        battle.MaxDistanceM = 20000.0;
        return battle;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the regression counts: 1,000 battle, 1,000 lane and 1,000 cluster ships
    SOLNpcScale::FCounts NpcSpawnRegCounts()
    {
        SOLNpcScale::FCounts counts;
        counts.Battle = 1000;
        counts.Lane = 1000;
        counts.Cluster = 1000;
        return counts;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a spawn's sample values in the captured layout of its kind
    FNpcSpawnRegSample NpcSpawnRegSampleOf(const SOLNpcScale::FSpawn& spawn, const int32 npcIndex)
    {
        FNpcSpawnRegSample sample = { npcIndex, spawn.GroupIndex, spawn.Faction, { 0.0, 0.0, 0.0, 0.0 } };
        if (spawn.Kind == SOLNpcScale::EPlacementKind::Battle)
        {
            sample.V[0] = spawn.BattleSlotOffsetM.X;
            sample.V[1] = spawn.BattleSlotOffsetM.Y;
            sample.V[2] = spawn.BattleSlotOffsetM.Z;
        }
        else if (spawn.Kind == SOLNpcScale::EPlacementKind::Cluster)
        {
            sample.V[0] = spawn.OrbitRadiusM;
            sample.V[1] = spawn.OrbitInclinationRad;
            sample.V[2] = spawn.OrbitNodeRad;
            sample.V[3] = spawn.OrbitPhaseRad;
        }
        return sample;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a sample as a C++ initializer line (17 significant digits, so a pasted value round-trips bit for bit)
    FString NpcSpawnRegLine(const FNpcSpawnRegSample& sample)
    {
        return FString::Printf(TEXT("        { %d, %d, %d, { %.17g, %.17g, %.17g, %.17g } },"), sample.NpcIndex,
            sample.Group, sample.Faction, sample.V[0], sample.V[1], sample.V[2], sample.V[3]);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcSpawnRegressionTest, "SOLTest.NpcSpawnRegression.BattleClusterUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Battle and cluster spawns are bit-identical to the pre-#50 implementation, and lane ships keep their group and
// faction streams (contract case 9); before the capture the test logs the tables to paste and fails
bool FSOLNpcSpawnRegressionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FLayout layout = NpcSpawnRegLayout();
    const FBattleParams battle = NpcSpawnRegBattle();
    const FCounts counts = NpcSpawnRegCounts();

    struct FRegTable
    {
        const TCHAR* Name;
        const FNpcSpawnRegSample* Rows;
        int32 Count;
        EPlacementKind Kind;
        bool bCompareValues;
    };
    const FRegTable tables[] = {
        { TEXT("Battle"), NPCSPAWNREG_BATTLE, UE_ARRAY_COUNT(NPCSPAWNREG_BATTLE), EPlacementKind::Battle, true },
        { TEXT("Lane"), NPCSPAWNREG_LANE, UE_ARRAY_COUNT(NPCSPAWNREG_LANE), EPlacementKind::Lane, false },
        { TEXT("Cluster"), NPCSPAWNREG_CLUSTER, UE_ARRAY_COUNT(NPCSPAWNREG_CLUSTER), EPlacementKind::Cluster, true },
    };
    for (const FRegTable& table : tables)
    {
        // Before the capture: log the current values in paste-ready form
        if (!NPCSPAWNREG_CAPTURED)
        {
            AddInfo(FString::Printf(TEXT("%s samples to paste:"), table.Name));
            for (int32 row = 0; row < table.Count; ++row)
            {
                const int32 npcIndex = table.Rows[row].NpcIndex;
                AddInfo(NpcSpawnRegLine(NpcSpawnRegSampleOf(MakeSpawn(NPCSPAWNREG_SEED, npcIndex, counts, layout,
                    battle), npcIndex)));
            }
            continue;
        }

        // After the capture: bit-identical
        for (int32 row = 0; row < table.Count; ++row)
        {
            const FNpcSpawnRegSample& expected = table.Rows[row];
            const FSpawn spawn = MakeSpawn(NPCSPAWNREG_SEED, expected.NpcIndex, counts, layout, battle);
            const FNpcSpawnRegSample actual = NpcSpawnRegSampleOf(spawn, expected.NpcIndex);
            const FString label = FString::Printf(TEXT("%s index %d"), table.Name, expected.NpcIndex);
            TestTrue(label + TEXT(": kind"), spawn.Kind == table.Kind);
            TestEqual(label + TEXT(": group"), actual.Group, expected.Group);
            TestEqual(label + TEXT(": faction"), actual.Faction, expected.Faction);
            if (table.bCompareValues)
            {
                for (int32 value = 0; value < 4; ++value)
                {
                    TestTrue(label + FString::Printf(TEXT(": value %d bit-identical (%.17g, captured %.17g)"), value,
                        actual.V[value], expected.V[value]), actual.V[value] == expected.V[value]);
                }
            }
        }
    }
    TestTrue(TEXT("Regression values captured from the pre-#50 build (see the capture protocol at the top)"),
        NPCSPAWNREG_CAPTURED);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
