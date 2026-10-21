/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcScaleMath.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #11 (Part 10a) pure NPC scale logic, written against the 10a contract in
// Docs/Plans/11-scale-and-performance-demo-plan.md before any production code existed.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Tolerance for plain double comparisons
    constexpr double NPCSCALE_EPS = 1.0e-9;

    // Seed used by most cases
    constexpr uint32 NPCSCALE_SEED = 20261015u;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double NpcScaleNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the invalid-input cases
    double NpcScaleInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the layout most spawn cases use: two lanes (weights 1 and 3) and two clusters (weights 1 and 1)
    SOLNpcScale::FLayout NpcScaleTestLayout()
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

        layout.LaneMinSpeedMps = 2.0e4;
        layout.LaneMaxSpeedMps = 2.0e5;
        return layout;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the battle parameters most spawn cases use (4 battles, 3 km radius, 5-20 km from the player)
    SOLNpcScale::FBattleParams NpcScaleTestBattle()
    {
        SOLNpcScale::FBattleParams battle;
        battle.BattleCount = 4;
        battle.RadiusM = 3000.0;
        battle.MinDistanceM = 5000.0;
        battle.MaxDistanceM = 20000.0;
        return battle;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns counts with the given numbers of battle, lane and cluster ships
    SOLNpcScale::FCounts NpcScaleCounts(const int32 battle, const int32 lane, const int32 cluster)
    {
        SOLNpcScale::FCounts counts;
        counts.Battle = battle;
        counts.Lane = lane;
        counts.Cluster = cluster;
        return counts;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every lane-only field of a spawn holds its default
    bool NpcScaleLaneFieldsDefault(const SOLNpcScale::FSpawn& spawn)
    {
        return spawn.LaneStartParam == 0.0 && spawn.LaneDirection == 1.0 && spawn.LaneSpeedMps == 0.0
            && spawn.LaneLateralOffsetM == FVector2d::ZeroVector;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every cluster-only field of a spawn holds its default
    bool NpcScaleClusterFieldsDefault(const SOLNpcScale::FSpawn& spawn)
    {
        return spawn.OrbitRadiusM == 0.0 && spawn.OrbitInclinationRad == 0.0 && spawn.OrbitNodeRad == 0.0
            && spawn.OrbitPhaseRad == 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every battle-only field of a spawn holds its default
    bool NpcScaleBattleFieldsDefault(const SOLNpcScale::FSpawn& spawn)
    {
        return spawn.BattleSlotOffsetM == FVector3d::ZeroVector;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two spawns are bit-identical field by field
    bool NpcScaleSpawnsIdentical(const SOLNpcScale::FSpawn& a, const SOLNpcScale::FSpawn& b)
    {
        return a.Kind == b.Kind && a.GroupIndex == b.GroupIndex && a.Faction == b.Faction
            && a.LaneStartParam == b.LaneStartParam && a.LaneDirection == b.LaneDirection
            && a.LaneSpeedMps == b.LaneSpeedMps && a.LaneLateralOffsetM == b.LaneLateralOffsetM
            && a.OrbitRadiusM == b.OrbitRadiusM && a.OrbitInclinationRad == b.OrbitInclinationRad
            && a.OrbitNodeRad == b.OrbitNodeRad && a.OrbitPhaseRad == b.OrbitPhaseRad
            && a.BattleSlotOffsetM == b.BattleSlotOffsetM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the demo option id with the given integer value
    SOLNpcScale::EDemoOption NpcScaleOptionId(const int32 index)
    {
        return static_cast<SOLNpcScale::EDemoOption>(index);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of demo options (EDemoOption::Count)
    int32 NpcScaleOptionCount()
    {
        return static_cast<int32>(SOLNpcScale::EDemoOption::Count);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two option sets hold the same value for every option and the same seed
    bool NpcScaleOptionsEqual(const SOLNpcScale::FDemoOptions& a, const SOLNpcScale::FDemoOptions& b)
    {
        for (int32 index = 0; index < NpcScaleOptionCount(); ++index)
        {
            const SOLNpcScale::EDemoOption id = NpcScaleOptionId(index);
            if (SOLNpcScale::GetOption(a, id) != SOLNpcScale::GetOption(b, id))
            {
                return false;
            }
        }
        return a.Seed == b.Seed;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the descriptor of one option (the descriptor array is indexed by option id)
    const SOLNpcScale::FOptionDesc& NpcScaleDesc(const SOLNpcScale::EDemoOption id)
    {
        return SOLNpcScale::OptionDescriptors()[static_cast<int32>(id)];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the corrective (non-warning) issue flags
    SOLNpcScale::EOptionIssue NpcScaleCorrectionFlags()
    {
        using SOLNpcScale::EOptionIssue;
        return EOptionIssue::ValueClamped | EOptionIssue::MidRadiusRaised | EOptionIssue::InstancedRadiusLowered
            | EOptionIssue::MeshRadiusLowered | EOptionIssue::BattleShipsLimited | EOptionIssue::BattleDistanceRaised
            | EOptionIssue::MidBudgetRaised;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many distances lie strictly inside the radius (non-finite entries ignored, negatives count as 0)
    int32 NpcScaleCountInside(const TArray<double>& distances, const double radius)
    {
        int32 count = 0;
        for (const double distance : distances)
        {
            if (FMath::IsFinite(distance) && FMath::Max(distance, 0.0) < radius)
            {
                ++count;
            }
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns frame stats with the given fields
    SOLNpcScale::FFrameStats NpcScaleStats(const int32 count, const double meanMs, const double minMs,
        const double p50Ms, const double p95Ms, const double p99Ms, const double maxMs)
    {
        SOLNpcScale::FFrameStats stats;
        stats.Count = count;
        stats.MeanMs = meanMs;
        stats.MinMs = minMs;
        stats.P50Ms = p50Ms;
        stats.P95Ms = p95Ms;
        stats.P99Ms = p99Ms;
        stats.MaxMs = maxMs;
        return stats;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ramp state with the given fields
    SOLNpcScale::FRampState NpcScaleRampState(const SOLNpcScale::ERampPhase phase, const int32 stageIndex,
        const double elapsedS)
    {
        SOLNpcScale::FRampState state;
        state.Phase = phase;
        state.StageIndex = stageIndex;
        state.PhaseElapsedS = elapsedS;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two ramp states match (elapsed time within a tolerance)
    bool NpcScaleRampStatesEqual(const SOLNpcScale::FRampState& a, const SOLNpcScale::FRampState& b)
    {
        return a.Phase == b.Phase && a.StageIndex == b.StageIndex
            && FMath::Abs(a.PhaseElapsedS - b.PhaseElapsedS) <= NPCSCALE_EPS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Pearson correlation of two equally long sample arrays
    double NpcScaleCorrelation(const TArray<double>& x, const TArray<double>& y)
    {
        const int32 n = FMath::Min(x.Num(), y.Num());
        double meanX = 0.0;
        double meanY = 0.0;
        for (int32 index = 0; index < n; ++index)
        {
            meanX += x[index];
            meanY += y[index];
        }
        meanX /= n;
        meanY /= n;
        double covariance = 0.0;
        double varianceX = 0.0;
        double varianceY = 0.0;
        for (int32 index = 0; index < n; ++index)
        {
            covariance += (x[index] - meanX) * (y[index] - meanY);
            varianceX += (x[index] - meanX) * (x[index] - meanX);
            varianceY += (y[index] - meanY) * (y[index] - meanY);
        }
        return covariance / FMath::Sqrt(varianceX * varianceY);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Tiers
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleTierPromotionTest, "SOLTest.NpcScaleMath.TierPromotion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With no current tier, a ship takes the first tier whose radius strictly contains it
bool FSOLNpcScaleTierPromotionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const TArray<double> radii = { 100.0, 1000.0 };
    TestEqual(TEXT("Inside the first radius"), SelectTierIndex(INDEX_NONE, 50.0, radii, 0.1), 0);
    TestEqual(TEXT("At zero distance"), SelectTierIndex(INDEX_NONE, 0.0, radii, 0.1), 0);
    TestEqual(TEXT("Exactly at the first radius is outside it (strict)"),
        SelectTierIndex(INDEX_NONE, 100.0, radii, 0.1), 1);
    TestEqual(TEXT("Between the radii"), SelectTierIndex(INDEX_NONE, 500.0, radii, 0.1), 1);
    TestEqual(TEXT("Exactly at the last radius is beyond every tier"),
        SelectTierIndex(INDEX_NONE, 1000.0, radii, 0.1), 2);
    TestEqual(TEXT("Beyond every radius"), SelectTierIndex(INDEX_NONE, 1.0e12, radii, 0.1), 2);

    // A ship currently in the outermost tier is promoted by the plain radii only, not the hysteresis-widened ones
    TestEqual(TEXT("Far ship just outside the first radius is not promoted to it"),
        SelectTierIndex(2, 105.0, radii, 0.1), 1);
    TestEqual(TEXT("Far ship just outside the second radius stays far"), SelectTierIndex(2, 1050.0, radii, 0.1), 2);
    TestEqual(TEXT("Far ship inside the first radius jumps straight to it"), SelectTierIndex(2, 99.0, radii, 0.1), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleTierHysteresisTest, "SOLTest.NpcScaleMath.TierHysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ship in tier k or nearer keeps tier k out to R_k * (1 + h); a farther ship needs to come inside R_k
bool FSOLNpcScaleTierHysteresisTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const TArray<double> radii = { 100.0, 1000.0 };
    const double h = 0.25;     // exact in binary, so the widened radii 125 and 1250 are exact boundaries

    // Tier 0 keeps tier 0 inside 125, then falls to tier 1 (whose keep radius also applies, since 0 <= 1)
    TestEqual(TEXT("Tier 0 kept inside the widened radius"), SelectTierIndex(0, 124.0, radii, h), 0);
    TestEqual(TEXT("Tier 0 lost exactly at the widened radius"), SelectTierIndex(0, 125.0, radii, h), 1);
    TestEqual(TEXT("Tier 0 ship beyond R1 but inside R1 * 1.25 keeps tier 1"), SelectTierIndex(0, 1200.0, radii, h),
        1);
    TestEqual(TEXT("Tier 0 ship at R1 * 1.25 goes far"), SelectTierIndex(0, 1250.0, radii, h), 2);

    // Tier 1 keeps tier 1 out to 1250 but is promoted to tier 0 only inside 100
    TestEqual(TEXT("Tier 1 kept inside its widened radius"), SelectTierIndex(1, 1249.0, radii, h), 1);
    TestEqual(TEXT("Tier 1 lost at its widened radius"), SelectTierIndex(1, 1250.0, radii, h), 2);
    TestEqual(TEXT("Tier 1 not promoted inside tier 0's widened band"), SelectTierIndex(1, 110.0, radii, h), 1);
    TestEqual(TEXT("Tier 1 promoted inside tier 0's plain radius"), SelectTierIndex(1, 99.9, radii, h), 0);

    // A ship oscillating +/-2% around a boundary changes tier only once with hysteresis
    int32 tier = 2;
    int32 changes = 0;
    for (int32 frame = 0; frame < 200; ++frame)
    {
        const double distance = 1000.0 * (frame % 2 == 0 ? 0.98 : 1.02);
        const int32 next = SelectTierIndex(tier, distance, radii, h);
        if (next != tier)
        {
            ++changes;
        }
        tier = next;
    }
    TestEqual(TEXT("No flicker around a boundary"), changes, 1);
    TestEqual(TEXT("Settled in tier 1"), tier, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleTierEdgeCasesTest, "SOLTest.NpcScaleMath.TierEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-finite and negative inputs, invalid current tiers and odd radii follow the contract
bool FSOLNpcScaleTierEdgeCasesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const TArray<double> radii = { 100.0, 1000.0 };

    // Distances
    TestEqual(TEXT("NaN distance is beyond every tier"), SelectTierIndex(0, NpcScaleNaN(), radii, 0.1), 2);
    TestEqual(TEXT("+inf distance is beyond every tier"), SelectTierIndex(0, NpcScaleInf(), radii, 0.1), 2);
    TestEqual(TEXT("-inf distance is beyond every tier"), SelectTierIndex(0, -NpcScaleInf(), radii, 0.1), 2);
    TestEqual(TEXT("Negative distance counts as 0"), SelectTierIndex(2, -50.0, radii, 0.1), 0);

    // Hysteresis: NaN or negative counts as 0
    TestEqual(TEXT("NaN hysteresis is 0"), SelectTierIndex(0, 105.0, radii, NpcScaleNaN()), 1);
    TestEqual(TEXT("Negative hysteresis does not shrink the keep radius"), SelectTierIndex(0, 99.0, radii, -0.5), 0);
    TestEqual(TEXT("Negative hysteresis is 0 (no widening)"), SelectTierIndex(0, 101.0, radii, -0.5), 1);

    // Current tiers outside [0, N] count as "no tier"
    TestEqual(TEXT("Current -1: plain radii"), SelectTierIndex(-1, 105.0, radii, 0.1), 1);
    TestEqual(TEXT("Current 99: plain radii"), SelectTierIndex(99, 105.0, radii, 0.1), 1);
    TestEqual(TEXT("Current N (outermost) is a valid tier"), SelectTierIndex(2, 1050.0, radii, 0.1), 2);

    // Radii
    const TArray<double> nanFirst = { NpcScaleNaN(), 1000.0 };
    TestEqual(TEXT("NaN radius never contains"), SelectTierIndex(0, 1.0, nanFirst, 0.1), 1);
    const TArray<double> zeroFirst = { 0.0, 1000.0 };
    TestEqual(TEXT("Zero radius never contains, even at distance 0"), SelectTierIndex(0, 0.0, zeroFirst, 0.1), 1);
    const TArray<double> negativeFirst = { -5.0, 1000.0 };
    TestEqual(TEXT("Negative radius never contains"), SelectTierIndex(INDEX_NONE, 0.0, negativeFirst, 0.1), 1);
    const TArray<double> infLast = { 100.0, NpcScaleInf() };
    TestEqual(TEXT("+inf radius contains every finite distance"), SelectTierIndex(INDEX_NONE, 1.0e300, infLast, 0.1),
        1);
    const TArray<double> none;
    TestEqual(TEXT("No radii: tier 0 is the only tier"), SelectTierIndex(INDEX_NONE, 5.0, none, 0.1), 0);
    const TArray<double> descending = { 1000.0, 100.0 };
    TestEqual(TEXT("Radii need not ascend: first match wins"), SelectTierIndex(INDEX_NONE, 50.0, descending, 0.0), 0);
    TestEqual(TEXT("Descending radii: outside both"), SelectTierIndex(INDEX_NONE, 5000.0, descending, 0.0), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleTierWrappersTest, "SOLTest.NpcScaleMath.TierWrappers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The AI and render tier wrappers map the generic tier index onto their enums
bool FSOLNpcScaleTierWrappersTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    TestTrue(TEXT("AI: inside full radius"),
        SelectAiTier(EAiTier::Far, 10.0e3, 50.0e3, 2.0e6, 0.1) == EAiTier::Full);
    TestTrue(TEXT("AI: between full and mid"),
        SelectAiTier(EAiTier::Far, 100.0e3, 50.0e3, 2.0e6, 0.1) == EAiTier::Mid);
    TestTrue(TEXT("AI: beyond mid"), SelectAiTier(EAiTier::Mid, 3.0e6, 50.0e3, 2.0e6, 0.1) == EAiTier::Far);
    TestTrue(TEXT("AI: full kept in its hysteresis band"),
        SelectAiTier(EAiTier::Full, 52.0e3, 50.0e3, 2.0e6, 0.1) == EAiTier::Full);
    TestTrue(TEXT("AI: mid not promoted in full's hysteresis band"),
        SelectAiTier(EAiTier::Mid, 52.0e3, 50.0e3, 2.0e6, 0.1) == EAiTier::Mid);
    TestTrue(TEXT("AI: mid kept in its hysteresis band"),
        SelectAiTier(EAiTier::Mid, 2.1e6, 50.0e3, 2.0e6, 0.1) == EAiTier::Mid);
    TestTrue(TEXT("AI: far not promoted in mid's hysteresis band"),
        SelectAiTier(EAiTier::Far, 2.1e6, 50.0e3, 2.0e6, 0.1) == EAiTier::Far);
    TestTrue(TEXT("AI: NaN distance is far"),
        SelectAiTier(EAiTier::Full, NpcScaleNaN(), 50.0e3, 2.0e6, 0.1) == EAiTier::Far);

    TestTrue(TEXT("Render: inside mesh radius"),
        SelectRenderTier(ERenderTier::Point, 1.0e3, 2.0e3, 200.0e3, 0.1) == ERenderTier::Mesh);
    TestTrue(TEXT("Render: between mesh and instanced"),
        SelectRenderTier(ERenderTier::Point, 50.0e3, 2.0e3, 200.0e3, 0.1) == ERenderTier::Instanced);
    TestTrue(TEXT("Render: beyond instanced"),
        SelectRenderTier(ERenderTier::Instanced, 300.0e3, 2.0e3, 200.0e3, 0.1) == ERenderTier::Point);
    TestTrue(TEXT("Render: mesh kept in its hysteresis band"),
        SelectRenderTier(ERenderTier::Mesh, 2.1e3, 2.0e3, 200.0e3, 0.1) == ERenderTier::Mesh);
    TestTrue(TEXT("Render: instanced kept in its hysteresis band"),
        SelectRenderTier(ERenderTier::Instanced, 210.0e3, 2.0e3, 200.0e3, 0.1) == ERenderTier::Instanced);
    TestTrue(TEXT("Render: point not promoted in instanced's hysteresis band"),
        SelectRenderTier(ERenderTier::Point, 210.0e3, 2.0e3, 200.0e3, 0.1) == ERenderTier::Point);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleBudgetedRadiusTest, "SOLTest.NpcScaleMath.BudgetedRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The budgeted radius keeps at most `budget` ships inside its keep radius, and is the nominal radius when they fit
bool FSOLNpcScaleBudgetedRadiusTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    TArray<double> scratch;
    const TArray<double> distances = { 50.0, 10.0, 70.0, 30.0, 90.0, 20.0, 60.0, 40.0, 80.0 };

    // Under budget: nominal unchanged
    TestEqual(TEXT("Fits the budget: nominal"), BudgetedRadiusM(distances, 1000.0, 0.0, 9, scratch), 1000.0);
    TestEqual(TEXT("Only 3 inside the nominal radius, budget 3: nominal"),
        BudgetedRadiusM(distances, 35.0, 0.0, 3, scratch), 35.0);
    TestTrue(TEXT("Hysteresis widens the count: 4 inside 35 * 1.25, budget 3 shrinks"),
        BudgetedRadiusM(distances, 35.0, 0.25, 3, scratch) < 35.0);
    TestEqual(TEXT("...to s[3] / 1.25 = 32"), BudgetedRadiusM(distances, 35.0, 0.25, 3, scratch), 32.0);

    // Over budget, h = 0: the b-th smallest distance (0-based)
    TestEqual(TEXT("h = 0, budget 3: s[3] = 40"), BudgetedRadiusM(distances, 1000.0, 0.0, 3, scratch), 40.0);
    TestEqual(TEXT("h = 0, budget 0: s[0] = 10"), BudgetedRadiusM(distances, 1000.0, 0.0, 0, scratch), 10.0);
    TestEqual(TEXT("Negative budget counts as 0"), BudgetedRadiusM(distances, 1000.0, 0.0, -7, scratch), 10.0);

    // Over budget, h = 1 (exact in binary): s[b] / 2
    TestEqual(TEXT("h = 1, budget 3: 40 / 2"), BudgetedRadiusM(distances, 1000.0, 1.0, 3, scratch), 20.0);

    // h = 0.1: s[b] / 1.1
    TestNearlyEqual(TEXT("h = 0.1, budget 5: 60 / 1.1"), BudgetedRadiusM(distances, 1000.0, 0.1, 5, scratch),
        60.0 / 1.1, 1.0e-12);

    // Never above the nominal radius
    TestTrue(TEXT("Result <= nominal"), BudgetedRadiusM(distances, 45.0, 0.0, 2, scratch) <= 45.0);
    TestEqual(TEXT("Over budget inside nominal 45 (4 ships), budget 2: s[2] = 30"),
        BudgetedRadiusM(distances, 45.0, 0.0, 2, scratch), 30.0);

    // The budget property, for h = 0 and h = 1, for every budget
    for (const double h : { 0.0, 1.0 })
    {
        for (int32 budget = 0; budget <= 10; ++budget)
        {
            const double radius = BudgetedRadiusM(distances, 1000.0, h, budget, scratch);
            TestTrue(*FString::Printf(TEXT("At most %d inside the keep radius (h=%.0f)"), budget, h),
                NpcScaleCountInside(distances, radius * (1.0 + h)) <= budget);
        }
    }

    // Non-finite entries are ignored, negatives count as 0
    const TArray<double> messy = { NpcScaleNaN(), 30.0, NpcScaleInf(), -5.0, 10.0, -NpcScaleInf(), 20.0 };
    TestEqual(TEXT("Messy, budget 0: the negative counts as 0"), BudgetedRadiusM(messy, 1000.0, 0.0, 0, scratch), 0.0);
    TestEqual(TEXT("Messy, budget 2: s = {0, 10, 20, 30} -> 20"), BudgetedRadiusM(messy, 1000.0, 0.0, 2, scratch),
        20.0);
    TestEqual(TEXT("Messy, budget 4: fits"), BudgetedRadiusM(messy, 1000.0, 0.0, 4, scratch), 1000.0);

    // Ties
    const TArray<double> ties = { 5.0, 5.0, 5.0, 5.0 };
    const double tieRadius = BudgetedRadiusM(ties, 100.0, 0.0, 2, scratch);
    TestEqual(TEXT("Ties: radius at the tied value"), tieRadius, 5.0);
    TestEqual(TEXT("Ties: none strictly inside"), NpcScaleCountInside(ties, tieRadius), 0);

    // Infinite nominal: budget only
    TestEqual(TEXT("+inf nominal with budget 1"), BudgetedRadiusM(distances, NpcScaleInf(), 0.0, 1, scratch), 20.0);

    // Empty input
    const TArray<double> empty;
    TestEqual(TEXT("Empty: nominal"), BudgetedRadiusM(empty, 123.0, 0.1, 0, scratch), 123.0);

    // NaN hysteresis counts as 0
    TestEqual(TEXT("NaN hysteresis is 0"), BudgetedRadiusM(distances, 1000.0, NpcScaleNaN(), 3, scratch), 40.0);

    // Scratch reuse gives the same answer, and the input is not modified
    const TArray<double> copy = distances;
    const double first = BudgetedRadiusM(distances, 1000.0, 0.0, 4, scratch);
    const double second = BudgetedRadiusM(distances, 1000.0, 0.0, 4, scratch);
    TestEqual(TEXT("Same answer with a reused scratch"), first, second);
    TestTrue(TEXT("Input untouched"), copy == distances);

    // Large inputs with many ties (SDD 11 Amendment 6 selects s[b] instead of sorting): equal to a sorted reference
    FRandomStream random(1234);
    for (int32 trial = 0; trial < 20; ++trial)
    {
        TArray<double> large;
        const int32 count = 1000 + trial * 997;
        for (int32 index = 0; index < count; ++index)
        {
            large.Add(trial % 2 == 0 ? random.FRandRange(0.0, 1.0e6) : static_cast<double>(random.RandRange(0, 40)));
        }
        TArray<double> sorted = large;
        sorted.Sort();
        const int32 budget = random.RandRange(0, count - 1);
        TestEqual(*FString::Printf(TEXT("Large trial %d: s[%d]"), trial, budget),
            BudgetedRadiusM(large, 2.0e6, 0.0, budget, scratch), sorted[budget]);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Distribution
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleHashUnitTest, "SOLTest.NpcScaleMath.HashUnit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The per-index hash is deterministic, in [0, 1), uniform, and independent across indices, seeds and streams
bool FSOLNpcScaleHashUnitTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    constexpr int32 sampleCount = 200000;
    TArray<double> samples;
    TArray<double> otherStream;
    samples.Reserve(sampleCount);
    otherStream.Reserve(sampleCount);
    int32 deciles[10] = {};
    double sum = 0.0;
    bool bInRange = true;
    int32 sameAcrossSeeds = 0;
    int32 sameAcrossStreams = 0;
    for (int32 index = 0; index < sampleCount; ++index)
    {
        const double value = HashUnit(NPCSCALE_SEED, static_cast<uint32>(index), 0u);
        const double stream1 = HashUnit(NPCSCALE_SEED, static_cast<uint32>(index), 1u);
        const double seed2 = HashUnit(NPCSCALE_SEED + 1u, static_cast<uint32>(index), 0u);
        bInRange = bInRange && value >= 0.0 && value < 1.0 && stream1 >= 0.0 && stream1 < 1.0 && seed2 >= 0.0
            && seed2 < 1.0;
        samples.Add(value);
        otherStream.Add(stream1);
        sum += value;
        deciles[FMath::Clamp(static_cast<int32>(value * 10.0), 0, 9)] += 1;
        sameAcrossSeeds += (value == seed2) ? 1 : 0;
        sameAcrossStreams += (value == stream1) ? 1 : 0;
    }
    TestTrue(TEXT("Every value in [0, 1)"), bInRange);
    TestNearlyEqual(TEXT("Mean near 0.5"), sum / sampleCount, 0.5, 0.005);
    for (int32 decile = 0; decile < 10; ++decile)
    {
        TestNearlyEqual(*FString::Printf(TEXT("Decile %d holds about 10%%"), decile),
            static_cast<double>(deciles[decile]) / sampleCount, 0.1, 0.005);
    }
    TestTrue(TEXT("Different seeds rarely coincide"), sameAcrossSeeds < sampleCount / 100);
    TestTrue(TEXT("Different streams rarely coincide"), sameAcrossStreams < sampleCount / 100);

    // Neighbouring indices and different streams are uncorrelated
    TArray<double> shifted;
    shifted.Reserve(sampleCount - 1);
    TArray<double> leading;
    leading.Reserve(sampleCount - 1);
    for (int32 index = 0; index + 1 < sampleCount; ++index)
    {
        leading.Add(samples[index]);
        shifted.Add(samples[index + 1]);
    }
    TestTrue(TEXT("Lag-1 correlation is small"), FMath::Abs(NpcScaleCorrelation(leading, shifted)) < 0.01);
    TestTrue(TEXT("Cross-stream correlation is small"), FMath::Abs(NpcScaleCorrelation(samples, otherStream)) < 0.01);

    // Deterministic
    TestEqual(TEXT("Same inputs, same value"), HashUnit(7u, 12345u, 3u), HashUnit(7u, 12345u, 3u));
    TestEqual(TEXT("Same inputs, same value (index 0)"), HashUnit(0u, 0u, 0u), HashUnit(0u, 0u, 0u));
    const double zero = HashUnit(0u, 0u, 0u);
    TestTrue(TEXT("Zero inputs still in range"), zero >= 0.0 && zero < 1.0);
    const double high = HashUnit(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
    TestTrue(TEXT("Max inputs still in range"), high >= 0.0 && high < 1.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScalePickWeightedTest, "SOLTest.NpcScaleMath.PickWeighted",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The weighted pick follows the cumulative weights and skips invalid ones
bool FSOLNpcScalePickWeightedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const TArray<double> even = { 1.0, 1.0 };
    TestEqual(TEXT("u = 0 picks the first"), PickWeighted(0.0, even), 0);
    TestEqual(TEXT("u = 0.49 picks the first"), PickWeighted(0.49, even), 0);
    TestEqual(TEXT("u = 0.5 picks the second (strict <)"), PickWeighted(0.5, even), 1);
    TestEqual(TEXT("u = 0.999 picks the second"), PickWeighted(0.999, even), 1);

    // Zero, NaN, negative and infinite weights count as 0 and are never picked
    const TArray<double> mixed = { 0.0, 2.0, NpcScaleNaN(), -1.0, 3.0, NpcScaleInf() };
    TestEqual(TEXT("Mixed: u = 0 skips the zero weight"), PickWeighted(0.0, mixed), 1);
    TestEqual(TEXT("Mixed: u = 0.39"), PickWeighted(0.39, mixed), 1);
    TestEqual(TEXT("Mixed: u = 0.4 skips the invalid weights"), PickWeighted(0.4, mixed), 4);
    TestEqual(TEXT("Mixed: u = 0.9999"), PickWeighted(0.9999, mixed), 4);
    TestEqual(TEXT("Mixed: u = 1 clamps below 1"), PickWeighted(1.0, mixed), 4);
    TestEqual(TEXT("Mixed: u = 5 clamps below 1"), PickWeighted(5.0, mixed), 4);
    TestEqual(TEXT("Mixed: u = -1 clamps to 0"), PickWeighted(-1.0, mixed), 1);
    TestEqual(TEXT("Mixed: NaN u counts as 0"), PickWeighted(NpcScaleNaN(), mixed), 1);

    const TArray<double> zeros = { 0.0, 0.0, -2.0, NpcScaleNaN() };
    TestEqual(TEXT("No positive weight"), PickWeighted(0.5, zeros), static_cast<int32>(INDEX_NONE));
    const TArray<double> empty;
    TestEqual(TEXT("Empty"), PickWeighted(0.5, empty), static_cast<int32>(INDEX_NONE));
    const TArray<double> infFirst = { NpcScaleInf(), 1.0 };
    TestEqual(TEXT("Infinite weight is never picked"), PickWeighted(0.0, infFirst), 1);

    // Proportional over many hashed draws
    const TArray<double> oneThree = { 1.0, 3.0 };
    int32 second = 0;
    constexpr int32 draws = 100000;
    for (int32 index = 0; index < draws; ++index)
    {
        second += PickWeighted(HashUnit(NPCSCALE_SEED, static_cast<uint32>(index), 9u), oneThree) == 1 ? 1 : 0;
    }
    TestNearlyEqual(TEXT("Weights 1:3 give about 75% to the second"), static_cast<double>(second) / draws, 0.75, 0.01);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSplitCountsTest, "SOLTest.NpcScaleMath.SplitCounts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Battle ships come first, then the rest splits between lanes and clusters by the lane fraction
bool FSOLNpcScaleSplitCountsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FCounts basic = SplitCounts(1000, 100, 0.4);
    TestEqual(TEXT("Basic: battle"), basic.Battle, 100);
    TestEqual(TEXT("Basic: lane"), basic.Lane, 360);
    TestEqual(TEXT("Basic: cluster"), basic.Cluster, 540);
    TestEqual(TEXT("Basic: total"), basic.Total(), 1000);

    const FCounts million = SplitCounts(1000000, 2000, 0.4);
    TestEqual(TEXT("1M: battle"), million.Battle, 2000);
    TestEqual(TEXT("1M: lane"), million.Lane, 399200);
    TestEqual(TEXT("1M: cluster"), million.Cluster, 598800);

    const FCounts half = SplitCounts(10, 3, 0.5);
    TestEqual(TEXT("Rounding half up: lane = floor(3.5 + 0.5)"), half.Lane, 4);
    TestEqual(TEXT("Rounding half up: cluster"), half.Cluster, 3);

    const FCounts negative = SplitCounts(-5, 3, 0.5);
    TestEqual(TEXT("Negative total: nothing"), negative.Total(), 0);
    TestEqual(TEXT("Negative total: no battle ships"), negative.Battle, 0);

    const FCounts allBattle = SplitCounts(100, 500, 0.5);
    TestEqual(TEXT("Battle ships capped by the total"), allBattle.Battle, 100);
    TestEqual(TEXT("No lanes left"), allBattle.Lane, 0);
    TestEqual(TEXT("No clusters left"), allBattle.Cluster, 0);

    const FCounts noBattle = SplitCounts(100, -3, 0.5);
    TestEqual(TEXT("Negative battle ships count as 0"), noBattle.Battle, 0);
    TestEqual(TEXT("Even split: lanes"), noBattle.Lane, 50);
    TestEqual(TEXT("Even split: clusters"), noBattle.Cluster, 50);

    TestEqual(TEXT("NaN fraction: no lanes"), SplitCounts(100, 0, NpcScaleNaN()).Lane, 0);
    TestEqual(TEXT("Fraction 2 clamps to 1"), SplitCounts(100, 0, 2.0).Lane, 100);
    TestEqual(TEXT("Fraction -1 clamps to 0"), SplitCounts(100, 0, -1.0).Lane, 0);
    TestEqual(TEXT("Zero total"), SplitCounts(0, 0, 0.5).Total(), 0);

    // The parts always add up and stay non-negative
    bool bConsistent = true;
    for (const int32 total : { 0, 1, 7, 10000, 123457, 1000000 })
    {
        for (const int32 battle : { -1, 0, 3, 5000, 2000000 })
        {
            for (const double fraction : { 0.0, 0.123, 0.4, 0.5, 0.999, 1.0 })
            {
                const FCounts counts = SplitCounts(total, battle, fraction);
                bConsistent = bConsistent && counts.Total() == total && counts.Battle >= 0 && counts.Lane >= 0
                    && counts.Cluster >= 0;
            }
        }
    }
    TestTrue(TEXT("Parts add up to the total and are non-negative"), bConsistent);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleClassifyIndexTest, "SOLTest.NpcScaleMath.ClassifyIndex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Indices are battle ships, then lane ships, then cluster ships
bool FSOLNpcScaleClassifyIndexTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FCounts counts = NpcScaleCounts(2, 3, 4);
    TestTrue(TEXT("0 is battle"), ClassifyIndex(0, counts).Kind == EPlacementKind::Battle);
    TestEqual(TEXT("0 local"), ClassifyIndex(0, counts).LocalIndex, 0);
    TestTrue(TEXT("1 is battle"), ClassifyIndex(1, counts).Kind == EPlacementKind::Battle);
    TestEqual(TEXT("1 local"), ClassifyIndex(1, counts).LocalIndex, 1);
    TestTrue(TEXT("2 is lane"), ClassifyIndex(2, counts).Kind == EPlacementKind::Lane);
    TestEqual(TEXT("2 local"), ClassifyIndex(2, counts).LocalIndex, 0);
    TestTrue(TEXT("4 is lane"), ClassifyIndex(4, counts).Kind == EPlacementKind::Lane);
    TestEqual(TEXT("4 local"), ClassifyIndex(4, counts).LocalIndex, 2);
    TestTrue(TEXT("5 is cluster"), ClassifyIndex(5, counts).Kind == EPlacementKind::Cluster);
    TestEqual(TEXT("5 local"), ClassifyIndex(5, counts).LocalIndex, 0);
    TestTrue(TEXT("8 is cluster"), ClassifyIndex(8, counts).Kind == EPlacementKind::Cluster);
    TestEqual(TEXT("8 local"), ClassifyIndex(8, counts).LocalIndex, 3);
    TestEqual(TEXT("Total is out of range"), ClassifyIndex(9, counts).LocalIndex, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("-1 is out of range"), ClassifyIndex(-1, counts).LocalIndex, static_cast<int32>(INDEX_NONE));

    const FCounts lanesOnly = NpcScaleCounts(0, 5, 0);
    TestTrue(TEXT("No battle ships: 0 is a lane ship"), ClassifyIndex(0, lanesOnly).Kind == EPlacementKind::Lane);
    const FCounts clustersOnly = NpcScaleCounts(0, 0, 5);
    TestTrue(TEXT("Only clusters: 0 is a cluster ship"),
        ClassifyIndex(0, clustersOnly).Kind == EPlacementKind::Cluster);
    TestEqual(TEXT("Empty counts: 0 is out of range"), ClassifyIndex(0, NpcScaleCounts(0, 0, 0)).LocalIndex,
        static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSpawnDeterminismTest, "SOLTest.NpcScaleMath.SpawnDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The same arguments give a bit-identical spawn; another seed gives a different population; kinds follow the index
bool FSOLNpcScaleSpawnDeterminismTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FLayout layout = NpcScaleTestLayout();
    const FBattleParams battle = NpcScaleTestBattle();
    const FCounts counts = NpcScaleCounts(100, 300, 600);

    bool bIdentical = true;
    bool bKindsMatch = true;
    int32 laneDiffers = 0;
    int32 laneShips = 0;
    for (int32 index = 0; index < counts.Total(); ++index)
    {
        const FSpawn a = MakeSpawn(NPCSCALE_SEED, index, counts, layout, battle);
        const FSpawn b = MakeSpawn(NPCSCALE_SEED, index, counts, layout, battle);
        bIdentical = bIdentical && NpcScaleSpawnsIdentical(a, b);
        bKindsMatch = bKindsMatch && a.Kind == ClassifyIndex(index, counts).Kind;
        if (a.Kind == EPlacementKind::Lane)
        {
            const FSpawn other = MakeSpawn(NPCSCALE_SEED + 1u, index, counts, layout, battle);
            ++laneShips;
            laneDiffers += (other.LaneStartParam != a.LaneStartParam) ? 1 : 0;
        }
    }
    TestTrue(TEXT("Bit-identical on repeat"), bIdentical);
    TestTrue(TEXT("Kind is ClassifyIndex's kind"), bKindsMatch);
    TestEqual(TEXT("300 lane ships"), laneShips, 300);
    TestTrue(TEXT("Another seed changes (almost) every lane start"), laneDiffers >= 270);

    // Out-of-range indices give a default spawn
    const FSpawn defaultSpawn;
    TestTrue(TEXT("Index -1 gives a default spawn"),
        NpcScaleSpawnsIdentical(MakeSpawn(NPCSCALE_SEED, -1, counts, layout, battle), defaultSpawn));
    TestTrue(TEXT("Index == Total gives a default spawn"),
        NpcScaleSpawnsIdentical(MakeSpawn(NPCSCALE_SEED, counts.Total(), counts, layout, battle), defaultSpawn));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSpawnBattleTest, "SOLTest.NpcScaleMath.SpawnBattle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Battle ships are spread round robin over the battles with alternating factions, inside the battle radius
bool FSOLNpcScaleSpawnBattleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FLayout layout = NpcScaleTestLayout();
    const FBattleParams battle = NpcScaleTestBattle();
    const FCounts counts = NpcScaleCounts(1000, 10, 10);

    int32 perBattleFaction[4][2] = {};
    bool bGroupRule = true;
    bool bFactionRule = true;
    bool bInsideRadius = true;
    bool bOtherFieldsDefault = true;
    double meanDistance = 0.0;
    FVector3d meanOffset = FVector3d::ZeroVector;
    for (int32 index = 0; index < counts.Battle; ++index)
    {
        const FSpawn spawn = MakeSpawn(NPCSCALE_SEED, index, counts, layout, battle);
        bGroupRule = bGroupRule && spawn.Kind == EPlacementKind::Battle && spawn.GroupIndex == index % 4;
        bFactionRule = bFactionRule && spawn.Faction == (index / 4) % 2;
        const double distance = spawn.BattleSlotOffsetM.Size();
        bInsideRadius = bInsideRadius && distance <= battle.RadiusM * (1.0 + 1.0e-12);
        bOtherFieldsDefault = bOtherFieldsDefault && NpcScaleLaneFieldsDefault(spawn)
            && NpcScaleClusterFieldsDefault(spawn);
        if (spawn.GroupIndex >= 0 && spawn.GroupIndex < 4 && (spawn.Faction == 0 || spawn.Faction == 1))
        {
            perBattleFaction[spawn.GroupIndex][spawn.Faction] += 1;
        }
        meanDistance += distance;
        meanOffset += spawn.BattleSlotOffsetM;
    }
    meanDistance /= counts.Battle;
    meanOffset /= counts.Battle;
    TestTrue(TEXT("Group = local % BattleCount"), bGroupRule);
    TestTrue(TEXT("Faction = (local / BattleCount) % 2"), bFactionRule);
    TestTrue(TEXT("Slots inside the battle radius"), bInsideRadius);
    TestTrue(TEXT("Lane and cluster fields keep their defaults"), bOtherFieldsDefault);
    for (int32 group = 0; group < 4; ++group)
    {
        TestEqual(*FString::Printf(TEXT("Battle %d: 125 ships of faction 0"), group), perBattleFaction[group][0], 125);
        TestEqual(*FString::Printf(TEXT("Battle %d: 125 ships of faction 1"), group), perBattleFaction[group][1], 125);
    }
    TestTrue(TEXT("Slots spread through the ball (mean distance > 0.5 R)"), meanDistance > 0.5 * battle.RadiusM);
    TestTrue(TEXT("Slots centred (|mean offset| < 0.1 R)"), meanOffset.Size() < 0.1 * battle.RadiusM);

    // BattleCount below 1 counts as 1: one battle, factions alternate per ship
    FBattleParams single = battle;
    single.BattleCount = 0;
    bool bSingleRule = true;
    for (int32 index = 0; index < 20; ++index)
    {
        const FSpawn spawn = MakeSpawn(NPCSCALE_SEED, index, counts, layout, single);
        bSingleRule = bSingleRule && spawn.GroupIndex == 0 && spawn.Faction == index % 2;
    }
    TestTrue(TEXT("BattleCount 0 counts as 1"), bSingleRule);

    // A non-positive or non-finite radius gives a zero offset
    FBattleParams pointBattle = battle;
    pointBattle.RadiusM = 0.0;
    TestTrue(TEXT("Zero radius: zero offset"),
        MakeSpawn(NPCSCALE_SEED, 3, counts, layout, pointBattle).BattleSlotOffsetM == FVector3d::ZeroVector);
    pointBattle.RadiusM = NpcScaleNaN();
    TestTrue(TEXT("NaN radius: zero offset"),
        MakeSpawn(NPCSCALE_SEED, 3, counts, layout, pointBattle).BattleSlotOffsetM == FVector3d::ZeroVector);
    pointBattle.RadiusM = -10.0;
    TestTrue(TEXT("Negative radius: zero offset"),
        MakeSpawn(NPCSCALE_SEED, 3, counts, layout, pointBattle).BattleSlotOffsetM == FVector3d::ZeroVector);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSpawnLaneTest, "SOLTest.NpcScaleMath.SpawnLane",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Lane ships pick a lane by weight and get a start, direction, speed and lateral offset inside the contract's ranges
bool FSOLNpcScaleSpawnLaneTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FLayout layout = NpcScaleTestLayout();
    const FBattleParams battle = NpcScaleTestBattle();
    const FCounts counts = NpcScaleCounts(0, 20000, 0);

    int32 secondLane = 0;
    int32 forward = 0;
    int32 factionOne = 0;
    int32 wideOffset = 0;
    double meanStart = 0.0;
    bool bRanges = true;
    bool bOtherFieldsDefault = true;
    for (int32 index = 0; index < counts.Lane; ++index)
    {
        const FSpawn spawn = MakeSpawn(NPCSCALE_SEED, index, counts, layout, battle);
        const bool bValidGroup = spawn.GroupIndex == 0 || spawn.GroupIndex == 1;
        const double laneRadius = bValidGroup ? layout.Lanes[spawn.GroupIndex].RadiusM : 0.0;
        bRanges = bRanges && spawn.Kind == EPlacementKind::Lane && bValidGroup
            && spawn.LaneStartParam >= 0.0 && spawn.LaneStartParam < 1.0
            && (spawn.LaneDirection == 1.0 || spawn.LaneDirection == -1.0)
            && spawn.LaneSpeedMps >= layout.LaneMinSpeedMps && spawn.LaneSpeedMps <= layout.LaneMaxSpeedMps
            && spawn.LaneLateralOffsetM.Size() <= laneRadius * (1.0 + 1.0e-12)
            && (spawn.Faction == 0 || spawn.Faction == 1);
        bOtherFieldsDefault = bOtherFieldsDefault && NpcScaleClusterFieldsDefault(spawn)
            && NpcScaleBattleFieldsDefault(spawn);
        secondLane += spawn.GroupIndex == 1 ? 1 : 0;
        forward += spawn.LaneDirection > 0.0 ? 1 : 0;
        factionOne += spawn.Faction == 1 ? 1 : 0;
        wideOffset += (bValidGroup && spawn.LaneLateralOffsetM.Size() > 0.5 * laneRadius) ? 1 : 0;
        meanStart += spawn.LaneStartParam;
    }
    const double n = counts.Lane;
    TestTrue(TEXT("Every lane field in range"), bRanges);
    TestTrue(TEXT("Cluster and battle fields keep their defaults"), bOtherFieldsDefault);
    TestNearlyEqual(TEXT("Lane weights 1:3"), secondLane / n, 0.75, 0.02);
    TestNearlyEqual(TEXT("About half go A to B"), forward / n, 0.5, 0.02);
    TestNearlyEqual(TEXT("About half are faction 1"), factionOne / n, 0.5, 0.02);
    TestNearlyEqual(TEXT("Start params spread over [0, 1)"), meanStart / n, 0.5, 0.02);
    TestTrue(TEXT("Lateral offsets fill the lane disc"), wideOffset > counts.Lane / 2);

    // Swapped speed bounds still bound the speed
    FLayout swapped = layout;
    swapped.LaneMinSpeedMps = 2.0e5;
    swapped.LaneMaxSpeedMps = 2.0e4;
    bool bSwappedSpeeds = true;
    for (int32 index = 0; index < 200; ++index)
    {
        const FSpawn spawn = MakeSpawn(NPCSCALE_SEED, index, counts, swapped, battle);
        bSwappedSpeeds = bSwappedSpeeds && spawn.LaneSpeedMps >= 2.0e4 && spawn.LaneSpeedMps <= 2.0e5;
    }
    TestTrue(TEXT("Swapped speed bounds"), bSwappedSpeeds);

    // No lane with a positive weight: group INDEX_NONE and every other field at its default, kind still Lane
    FLayout noLanes = layout;
    noLanes.Lanes[0].Weight = 0.0;
    noLanes.Lanes[1].Weight = -1.0;
    const FSpawn orphan = MakeSpawn(NPCSCALE_SEED, 5, counts, noLanes, battle);
    TestTrue(TEXT("No lane: kind still Lane"), orphan.Kind == EPlacementKind::Lane);
    TestEqual(TEXT("No lane: group INDEX_NONE"), orphan.GroupIndex, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("No lane: faction default"), orphan.Faction, 0);
    TestTrue(TEXT("No lane: lane fields default"), NpcScaleLaneFieldsDefault(orphan));
    noLanes.Lanes.Reset();
    TestEqual(TEXT("Empty lane table: group INDEX_NONE"),
        MakeSpawn(NPCSCALE_SEED, 5, counts, noLanes, battle).GroupIndex, static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSpawnClusterTest, "SOLTest.NpcScaleMath.SpawnCluster",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Cluster ships pick a cluster by weight and get an orbit inside its shell and inclination cap
bool FSOLNpcScaleSpawnClusterTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FLayout layout = NpcScaleTestLayout();
    const FBattleParams battle = NpcScaleTestBattle();
    const FCounts counts = NpcScaleCounts(0, 0, 20000);

    int32 secondCluster = 0;
    int32 factionOne = 0;
    bool bRanges = true;
    bool bOtherFieldsDefault = true;
    double meanNode = 0.0;
    double meanPhase = 0.0;
    for (int32 index = 0; index < counts.Cluster; ++index)
    {
        const FSpawn spawn = MakeSpawn(NPCSCALE_SEED, index, counts, layout, battle);
        const bool bValidGroup = spawn.GroupIndex == 0 || spawn.GroupIndex == 1;
        if (!bValidGroup)
        {
            bRanges = false;
            continue;
        }
        const FClusterDef& cluster = layout.Clusters[spawn.GroupIndex];
        bRanges = bRanges && spawn.Kind == EPlacementKind::Cluster
            && spawn.OrbitRadiusM >= cluster.InnerRadiusM && spawn.OrbitRadiusM <= cluster.OuterRadiusM
            && spawn.OrbitInclinationRad >= 0.0 && spawn.OrbitInclinationRad <= cluster.MaxInclinationRad
            && spawn.OrbitNodeRad >= 0.0 && spawn.OrbitNodeRad < UE_DOUBLE_TWO_PI
            && spawn.OrbitPhaseRad >= 0.0 && spawn.OrbitPhaseRad < UE_DOUBLE_TWO_PI
            && (spawn.Faction == 0 || spawn.Faction == 1);
        bOtherFieldsDefault = bOtherFieldsDefault && NpcScaleLaneFieldsDefault(spawn)
            && NpcScaleBattleFieldsDefault(spawn);
        secondCluster += spawn.GroupIndex == 1 ? 1 : 0;
        factionOne += spawn.Faction == 1 ? 1 : 0;
        meanNode += spawn.OrbitNodeRad;
        meanPhase += spawn.OrbitPhaseRad;
    }
    const double n = counts.Cluster;
    TestTrue(TEXT("Every cluster field in range"), bRanges);
    TestTrue(TEXT("Lane and battle fields keep their defaults"), bOtherFieldsDefault);
    TestNearlyEqual(TEXT("Cluster weights 1:1"), secondCluster / n, 0.5, 0.02);
    TestNearlyEqual(TEXT("About half are faction 1"), factionOne / n, 0.5, 0.02);
    TestNearlyEqual(TEXT("Nodes spread over [0, 2 pi)"), meanNode / n, UE_DOUBLE_PI, 0.1);
    TestNearlyEqual(TEXT("Phases spread over [0, 2 pi)"), meanPhase / n, UE_DOUBLE_PI, 0.1);

    // Swapped inner / outer and a negative inclination cap
    FLayout odd = layout;
    odd.Clusters.SetNum(1);
    odd.Clusters[0].InnerRadiusM = 5.0e7;
    odd.Clusters[0].OuterRadiusM = 1.0e7;
    odd.Clusters[0].MaxInclinationRad = -0.3;
    bool bOddRanges = true;
    for (int32 index = 0; index < 200; ++index)
    {
        const FSpawn spawn = MakeSpawn(NPCSCALE_SEED, index, counts, odd, battle);
        bOddRanges = bOddRanges && spawn.GroupIndex == 0 && spawn.OrbitRadiusM >= 1.0e7 && spawn.OrbitRadiusM <= 5.0e7
            && spawn.OrbitInclinationRad == 0.0;
    }
    TestTrue(TEXT("Swapped radii still bound the orbit, negative cap gives 0 inclination"), bOddRanges);

    // No cluster with a positive weight
    FLayout noClusters = layout;
    noClusters.Clusters[0].Weight = 0.0;
    noClusters.Clusters[1].Weight = NpcScaleNaN();
    const FSpawn orphan = MakeSpawn(NPCSCALE_SEED, 7, counts, noClusters, battle);
    TestTrue(TEXT("No cluster: kind still Cluster"), orphan.Kind == EPlacementKind::Cluster);
    TestEqual(TEXT("No cluster: group INDEX_NONE"), orphan.GroupIndex, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("No cluster: faction default"), orphan.Faction, 0);
    TestTrue(TEXT("No cluster: cluster fields default"), NpcScaleClusterFieldsDefault(orphan));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleBattleCenterTest, "SOLTest.NpcScaleMath.BattleCenter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Battle centres lie between the min and max distance, in all directions, deterministically
bool FSOLNpcScaleBattleCenterTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FBattleParams battle = NpcScaleTestBattle();
    bool bInRange = true;
    bool bDeterministic = true;
    FVector3d meanDirection = FVector3d::ZeroVector;
    constexpr int32 centreCount = 1000;
    for (int32 index = 0; index < centreCount; ++index)
    {
        const FVector3d offset = BattleCenterOffsetM(NPCSCALE_SEED, index, battle);
        const double distance = offset.Size();
        bInRange = bInRange && distance >= 5000.0 * (1.0 - 1.0e-12) && distance <= 20000.0 * (1.0 + 1.0e-12);
        bDeterministic = bDeterministic && offset == BattleCenterOffsetM(NPCSCALE_SEED, index, battle);
        if (distance > 0.0)
        {
            meanDirection += offset / distance;
        }
    }
    meanDirection /= centreCount;
    TestTrue(TEXT("Distances in [min, max]"), bInRange);
    TestTrue(TEXT("Deterministic"), bDeterministic);
    TestTrue(TEXT("Directions spread over the sphere"), meanDirection.Size() < 0.15);

    // The first battles differ from each other
    bool bDistinct = true;
    for (int32 a = 0; a < 4; ++a)
    {
        for (int32 b = a + 1; b < 4; ++b)
        {
            bDistinct = bDistinct && !BattleCenterOffsetM(NPCSCALE_SEED, a, battle).Equals(
                BattleCenterOffsetM(NPCSCALE_SEED, b, battle), 1.0);
        }
    }
    TestTrue(TEXT("Battles 0-3 are at different places"), bDistinct);

    // Swapped bounds, negative and NaN distances
    FBattleParams swapped = battle;
    swapped.MinDistanceM = 20000.0;
    swapped.MaxDistanceM = 5000.0;
    const double swappedDistance = BattleCenterOffsetM(NPCSCALE_SEED, 3, swapped).Size();
    TestTrue(TEXT("Swapped bounds"), swappedDistance >= 5000.0 * (1.0 - 1.0e-12)
        && swappedDistance <= 20000.0 * (1.0 + 1.0e-12));
    FBattleParams negative = battle;
    negative.MinDistanceM = -5.0;
    const double negativeDistance = BattleCenterOffsetM(NPCSCALE_SEED, 3, negative).Size();
    TestTrue(TEXT("Negative min counts as 0"),
        negativeDistance >= 0.0 && negativeDistance <= 20000.0 * (1.0 + 1.0e-12));
    FBattleParams nanBattle = battle;
    nanBattle.MinDistanceM = NpcScaleNaN();
    nanBattle.MaxDistanceM = NpcScaleNaN();
    TestTrue(TEXT("NaN distances count as 0"),
        BattleCenterOffsetM(NPCSCALE_SEED, 3, nanBattle).Equals(FVector3d::ZeroVector, NPCSCALE_EPS));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleTriangleWaveTest, "SOLTest.NpcScaleMath.TriangleWave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The triangle wave rises 0 to 1, falls back to 0, and repeats every 2
bool FSOLNpcScaleTriangleWaveTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    TestNearlyEqual(TEXT("0"), TriangleWave01(0.0), 0.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("0.25"), TriangleWave01(0.25), 0.25, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("1"), TriangleWave01(1.0), 1.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("1.5"), TriangleWave01(1.5), 0.5, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("2"), TriangleWave01(2.0), 0.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("2.25"), TriangleWave01(2.25), 0.25, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("3.75"), TriangleWave01(3.75), 0.25, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("-0.25"), TriangleWave01(-0.25), 0.25, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("-1"), TriangleWave01(-1.0), 1.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("-1.5"), TriangleWave01(-1.5), 0.5, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Large argument"), TriangleWave01(1000000.3), 0.3, 1.0e-6);
    TestEqual(TEXT("NaN"), TriangleWave01(NpcScaleNaN()), 0.0);
    TestEqual(TEXT("+inf"), TriangleWave01(NpcScaleInf()), 0.0);
    TestEqual(TEXT("-inf"), TriangleWave01(-NpcScaleInf()), 0.0);

    // Range and continuity on a fine sweep
    bool bInRange = true;
    bool bContinuous = true;
    double previous = TriangleWave01(-5.0);
    for (int32 step = 1; step <= 10000; ++step)
    {
        const double value = TriangleWave01(-5.0 + step * 0.001);
        bInRange = bInRange && value >= 0.0 && value <= 1.0;
        bContinuous = bContinuous && FMath::Abs(value - previous) <= 0.001 + NPCSCALE_EPS;
        previous = value;
    }
    TestTrue(TEXT("Always in [0, 1]"), bInRange);
    TestTrue(TEXT("Continuous (slope 1)"), bContinuous);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleLaneParamTest, "SOLTest.NpcScaleMath.LaneParam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A lane ship moves along its lane at speed / length per second and bounces at the ends
bool FSOLNpcScaleLaneParamTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    FSpawn spawn;
    spawn.Kind = EPlacementKind::Lane;
    spawn.LaneStartParam = 0.2;
    spawn.LaneDirection = 1.0;
    spawn.LaneSpeedMps = 100.0;
    const double length = 1000.0;     // 0.1 lane per second

    TestNearlyEqual(TEXT("t = 0: the start"), LaneParamAt(spawn, length, 0.0), 0.2, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("t = 3"), LaneParamAt(spawn, length, 3.0), 0.5, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("t = 8: reaches B"), LaneParamAt(spawn, length, 8.0), 1.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("t = 10: bounced back"), LaneParamAt(spawn, length, 10.0), 0.8, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("t = 18: back at A"), LaneParamAt(spawn, length, 18.0), 0.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("t = 20: one full period"), LaneParamAt(spawn, length, 20.0), 0.2, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Negative time runs backwards"), LaneParamAt(spawn, length, -1.0), 0.1, NPCSCALE_EPS);

    spawn.LaneDirection = -1.0;
    TestNearlyEqual(TEXT("Reverse, t = 1"), LaneParamAt(spawn, length, 1.0), 0.1, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Reverse, t = 2: at A"), LaneParamAt(spawn, length, 2.0), 0.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Reverse, t = 3: bounced off A"), LaneParamAt(spawn, length, 3.0), 0.1, NPCSCALE_EPS);

    // Invalid length or time: the start
    TestNearlyEqual(TEXT("Zero length"), LaneParamAt(spawn, 0.0, 5.0), 0.2, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Negative length"), LaneParamAt(spawn, -5.0, 5.0), 0.2, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("NaN length"), LaneParamAt(spawn, NpcScaleNaN(), 5.0), 0.2, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("+inf length"), LaneParamAt(spawn, NpcScaleInf(), 5.0), 0.2, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("NaN time"), LaneParamAt(spawn, length, NpcScaleNaN()), 0.2, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("+inf time"), LaneParamAt(spawn, length, NpcScaleInf()), 0.2, NPCSCALE_EPS);

    // Solar-system scale: Earth-Mars-like lane, 100 km/s, one month
    FSpawn real;
    real.LaneStartParam = 0.0;
    real.LaneDirection = 1.0;
    real.LaneSpeedMps = 1.0e5;
    const double realLength = 1.0 * SOLTestHelpers::AU_M;
    const double monthS = 30.0 * SOLTestHelpers::SECONDS_PER_DAY;
    const double expected = 1.0e5 * monthS / realLength;   // ~1.73: past B and on the way back
    TestNearlyEqual(TEXT("Real scale"), LaneParamAt(real, realLength, monthS), 2.0 - expected, 1.0e-9);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleLaneBasisTest, "SOLTest.NpcScaleMath.LaneBasis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The lane basis is orthonormal, perpendicular to the lane, right-handed, and deterministic
bool FSOLNpcScaleLaneBasisTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FVector3d axes[] = {
        FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, -1.0),
        FVector3d(1.0, 2.0, 3.0), FVector3d(-4.0e11, 2.0e11, 1.0e9), FVector3d(0.0, 0.001, 1.0),
        FVector3d(0.3, -0.2, 0.9)
    };
    for (const FVector3d& axis : axes)
    {
        const FVector3d a = axis.GetSafeNormal();
        FVector3d u;
        FVector3d v;
        LaneBasis(axis, u, v);
        const FString label = axis.ToString();
        TestNearlyEqual(*(TEXT("|U| = 1 for ") + label), u.Size(), 1.0, 1.0e-12);
        TestNearlyEqual(*(TEXT("|V| = 1 for ") + label), v.Size(), 1.0, 1.0e-12);
        TestNearlyEqual(*(TEXT("U . axis = 0 for ") + label), u.Dot(a), 0.0, 1.0e-12);
        TestNearlyEqual(*(TEXT("V . axis = 0 for ") + label), v.Dot(a), 0.0, 1.0e-12);
        TestNearlyEqual(*(TEXT("U . V = 0 for ") + label), u.Dot(v), 0.0, 1.0e-12);
        TestTrue(*(TEXT("U x V = axis for ") + label), u.Cross(v).Equals(a, 1.0e-12));

        FVector3d u2;
        FVector3d v2;
        LaneBasis(axis, u2, v2);
        TestTrue(*(TEXT("Deterministic for ") + label), u == u2 && v == v2);

        FVector3d uScaled;
        FVector3d vScaled;
        LaneBasis(axis * 1.0e3, uScaled, vScaled);
        TestTrue(*(TEXT("Depends only on the direction for ") + label),
            uScaled.Equals(u, 1.0e-12) && vScaled.Equals(v, 1.0e-12));
    }

    // Zero and non-finite axes
    FVector3d u;
    FVector3d v;
    LaneBasis(FVector3d::ZeroVector, u, v);
    TestTrue(TEXT("Zero axis: U = +X"), u == FVector3d(1.0, 0.0, 0.0));
    TestTrue(TEXT("Zero axis: V = +Y"), v == FVector3d(0.0, 1.0, 0.0));
    LaneBasis(FVector3d(NpcScaleNaN(), 1.0, 0.0), u, v);
    TestTrue(TEXT("NaN axis: U = +X"), u == FVector3d(1.0, 0.0, 0.0));
    TestTrue(TEXT("NaN axis: V = +Y"), v == FVector3d(0.0, 1.0, 0.0));
    LaneBasis(FVector3d(0.0, NpcScaleInf(), 0.0), u, v);
    TestTrue(TEXT("Infinite axis: U = +X"), u == FVector3d(1.0, 0.0, 0.0));
    TestTrue(TEXT("Infinite axis: V = +Y"), v == FVector3d(0.0, 1.0, 0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleLanePositionTest, "SOLTest.NpcScaleMath.LanePosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A lane position interpolates the endpoints and adds the lateral offset in the lane basis, at AU scale
bool FSOLNpcScaleLanePositionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FVector3d a(1.0e11, 0.0, 0.0);
    const FVector3d b(2.0e11, 5.0e10, 0.0);
    TestTrue(TEXT("s = 0 at A"), LanePositionM(a, b, 0.0, FVector2d::ZeroVector).Equals(a, 1.0e-3));
    TestTrue(TEXT("s = 1 at B"), LanePositionM(a, b, 1.0, FVector2d::ZeroVector).Equals(b, 1.0e-3));
    const FVector3d mid = (a + b) * 0.5;
    TestTrue(TEXT("s = 0.5 at the midpoint"), LanePositionM(a, b, 0.5, FVector2d::ZeroVector).Equals(mid, 1.0e-3));
    TestTrue(TEXT("s is not clamped"),
        LanePositionM(a, b, 1.5, FVector2d::ZeroVector).Equals(a + (b - a) * 1.5, 1.0e-3));

    // Lateral offset: perpendicular to the lane, with its own length, along U and V
    const FVector3d withOffset = LanePositionM(a, b, 0.5, FVector2d(300.0, -400.0));
    const FVector3d lateral = withOffset - mid;
    TestNearlyEqual(TEXT("Offset length"), lateral.Size(), 500.0, 1.0e-3);
    TestNearlyEqual(TEXT("Offset perpendicular to the lane"), lateral.Dot((b - a).GetSafeNormal()), 0.0, 1.0e-3);
    FVector3d u;
    FVector3d v;
    LaneBasis(b - a, u, v);
    TestTrue(TEXT("Offset X along U"),
        (LanePositionM(a, b, 0.5, FVector2d(300.0, 0.0)) - mid).Equals(u * 300.0, 1.0e-3));
    TestTrue(TEXT("Offset Y along V"),
        (LanePositionM(a, b, 0.5, FVector2d(0.0, -400.0)) - mid).Equals(v * -400.0, 1.0e-3));

    // Degenerate lane: the basis falls back to X / Y
    const FVector3d same(3.0e11, 0.0, 0.0);
    TestTrue(TEXT("A == B: offset along X and Y"),
        LanePositionM(same, same, 0.7, FVector2d(10.0, 20.0)).Equals(same + FVector3d(10.0, 20.0, 0.0), 1.0e-3));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleClusterOffsetTest, "SOLTest.NpcScaleMath.ClusterOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A cluster ship flies a circular Keplerian orbit in the plane its inclination and node define
bool FSOLNpcScaleClusterOffsetTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const double gm = SOLTestHelpers::EARTH_GM;
    const double r = 1.0e7;
    const double n = FMath::Sqrt(gm / (r * r * r));
    const double period = UE_DOUBLE_TWO_PI / n;

    // Equatorial, node 0, phase 0: starts on +X, prograde toward +Y
    FSpawn flat;
    flat.Kind = EPlacementKind::Cluster;
    flat.OrbitRadiusM = r;
    TestTrue(TEXT("t = 0 on +X"), ClusterOffsetM(flat, gm, 0.0).Equals(FVector3d(r, 0.0, 0.0), 1.0e-3));
    TestTrue(TEXT("Quarter period on +Y"),
        ClusterOffsetM(flat, gm, 0.25 * period).Equals(FVector3d(0.0, r, 0.0), 1.0e-3));
    TestTrue(TEXT("Half period on -X"),
        ClusterOffsetM(flat, gm, 0.5 * period).Equals(FVector3d(-r, 0.0, 0.0), 1.0e-3));

    // Polar orbit, node 0, phase 90 deg: on +Z
    FSpawn polar = flat;
    polar.OrbitInclinationRad = UE_DOUBLE_HALF_PI;
    polar.OrbitPhaseRad = UE_DOUBLE_HALF_PI;
    TestTrue(TEXT("Polar, phase 90 deg: on +Z"), ClusterOffsetM(polar, gm, 0.0).Equals(FVector3d(0.0, 0.0, r), 1.0e-3));

    // Node rotates P: node 90 deg, phase 0 is on +Y
    FSpawn noded = flat;
    noded.OrbitNodeRad = UE_DOUBLE_HALF_PI;
    noded.OrbitInclinationRad = 0.4;
    TestTrue(TEXT("Node 90 deg, phase 0: on +Y"),
        ClusterOffsetM(noded, gm, 0.0).Equals(FVector3d(0.0, r, 0.0), 1.0e-3));

    // General orbit: radius kept, in the plane, periodic, prograde about the normal
    FSpawn general = flat;
    general.OrbitInclinationRad = 0.3;
    general.OrbitNodeRad = 1.1;
    general.OrbitPhaseRad = 0.7;
    const double i = general.OrbitInclinationRad;
    const double om = general.OrbitNodeRad;
    const FVector3d normal(FMath::Sin(i) * FMath::Sin(om), -FMath::Sin(i) * FMath::Cos(om), FMath::Cos(i));
    bool bRadius = true;
    bool bInPlane = true;
    bool bPeriodic = true;
    bool bPrograde = true;
    for (int32 sample = 0; sample < 50; ++sample)
    {
        const double t = sample * 0.137 * period;
        const FVector3d p = ClusterOffsetM(general, gm, t);
        bRadius = bRadius && FMath::Abs(p.Size() - r) <= 1.0e-6 * r;
        bInPlane = bInPlane && FMath::Abs(p.Dot(normal)) <= 1.0e-6 * r;
        bPeriodic = bPeriodic && ClusterOffsetM(general, gm, t + period).Equals(p, 1.0e-5 * r);
        bPrograde = bPrograde && p.Cross(ClusterOffsetM(general, gm, t + 0.01 * period)).Dot(normal) > 0.0;
    }
    TestTrue(TEXT("Radius kept"), bRadius);
    TestTrue(TEXT("In the orbit plane"), bInPlane);
    TestTrue(TEXT("Periodic with 2 pi / sqrt(GM / r^3)"), bPeriodic);
    TestTrue(TEXT("Prograde about P x Q"), bPrograde);

    // No usable GM: static at the phase
    const FVector3d atStart = ClusterOffsetM(general, gm, 0.0);
    TestTrue(TEXT("GM 0: static"), ClusterOffsetM(general, 0.0, 1.0e6).Equals(atStart, 1.0e-3));
    TestTrue(TEXT("GM negative: static"), ClusterOffsetM(general, -gm, 1.0e6).Equals(atStart, 1.0e-3));
    TestTrue(TEXT("GM NaN: static"), ClusterOffsetM(general, NpcScaleNaN(), 1.0e6).Equals(atStart, 1.0e-3));
    TestTrue(TEXT("GM +inf: static"), ClusterOffsetM(general, NpcScaleInf(), 1.0e6).Equals(atStart, 1.0e-3));
    TestTrue(TEXT("NaN time counts as 0"), ClusterOffsetM(general, gm, NpcScaleNaN()).Equals(atStart, 1.0e-3));

    // No usable radius: zero
    FSpawn bad = general;
    bad.OrbitRadiusM = 0.0;
    TestTrue(TEXT("Radius 0: zero"), ClusterOffsetM(bad, gm, 10.0) == FVector3d::ZeroVector);
    bad.OrbitRadiusM = -5.0;
    TestTrue(TEXT("Negative radius: zero"), ClusterOffsetM(bad, gm, 10.0) == FVector3d::ZeroVector);
    bad.OrbitRadiusM = NpcScaleNaN();
    TestTrue(TEXT("NaN radius: zero"), ClusterOffsetM(bad, gm, 10.0) == FVector3d::ZeroVector);

    // Heliocentric belt orbit: period close to Kepler's third law at 2.5 AU (~3.95 years)
    FSpawn belt = flat;
    belt.OrbitRadiusM = 2.5 * SOLTestHelpers::AU_M;
    const double beltPeriod = UE_DOUBLE_TWO_PI
        * FMath::Sqrt(FMath::Pow(belt.OrbitRadiusM, 3.0) / SOLTestHelpers::SUN_GM);
    TestNearlyEqual(TEXT("Belt period ~3.95 years"), beltPeriod / (365.25 * SOLTestHelpers::SECONDS_PER_DAY), 3.95,
        0.01);
    TestTrue(TEXT("Belt quarter period on +Y"),
        ClusterOffsetM(belt, SOLTestHelpers::SUN_GM, 0.25 * beltPeriod).Equals(FVector3d(0.0, belt.OrbitRadiusM, 0.0),
            1.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleGroupDistancesTest, "SOLTest.NpcScaleMath.GroupDistances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Distances to a segment, a lane capsule and a cluster shell, used to cull whole groups
bool FSOLNpcScaleGroupDistancesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FVector3d a(0.0, 0.0, 0.0);
    const FVector3d b(10.0, 0.0, 0.0);
    TestNearlyEqual(TEXT("Segment: beside the middle"), DistancePointToSegmentM(FVector3d(5.0, 3.0, 0.0), a, b), 3.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Segment: before A"), DistancePointToSegmentM(FVector3d(-4.0, 3.0, 0.0), a, b), 5.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Segment: past B"), DistancePointToSegmentM(FVector3d(13.0, 4.0, 0.0), a, b), 5.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Segment: on it"), DistancePointToSegmentM(FVector3d(7.0, 0.0, 0.0), a, b), 0.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Degenerate segment"), DistancePointToSegmentM(FVector3d(3.0, 4.0, 0.0), a, a), 5.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Segment at AU scale"),
        DistancePointToSegmentM(FVector3d(2.0e11, 1.0e6, 0.0), FVector3d(1.0e11, 0.0, 0.0),
            FVector3d(3.0e11, 0.0, 0.0)), 1.0e6, 1.0e-3);

    TestNearlyEqual(TEXT("Lane: outside the capsule"), DistanceToLaneM(FVector3d(5.0, 3.0, 0.0), a, b, 1.0), 2.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Lane: inside the capsule"), DistanceToLaneM(FVector3d(5.0, 3.0, 0.0), a, b, 5.0), 0.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Lane: negative radius counts as 0"), DistanceToLaneM(FVector3d(5.0, 3.0, 0.0), a, b, -1.0),
        3.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Lane: NaN radius counts as 0"),
        DistanceToLaneM(FVector3d(5.0, 3.0, 0.0), a, b, NpcScaleNaN()), 3.0, NPCSCALE_EPS);

    const FVector3d c(100.0, 0.0, 0.0);
    TestNearlyEqual(TEXT("Shell: inside the hole"), DistanceToShellM(FVector3d(105.0, 0.0, 0.0), c, 10.0, 20.0), 5.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Shell: at the centre"), DistanceToShellM(c, c, 10.0, 20.0), 10.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Shell: in the shell"), DistanceToShellM(FVector3d(115.0, 0.0, 0.0), c, 10.0, 20.0), 0.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Shell: on the inner surface"), DistanceToShellM(FVector3d(110.0, 0.0, 0.0), c, 10.0, 20.0),
        0.0, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Shell: outside"), DistanceToShellM(FVector3d(100.0, 25.0, 0.0), c, 10.0, 20.0), 5.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Shell: swapped radii"), DistanceToShellM(FVector3d(100.0, 25.0, 0.0), c, 20.0, 10.0), 5.0,
        NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Shell: swapped radii, in the hole"), DistanceToShellM(FVector3d(105.0, 0.0, 0.0), c, 20.0,
        10.0), 5.0, NPCSCALE_EPS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleDefaultLayoutTest, "SOLTest.NpcScaleMath.DefaultLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The default layout has valid lanes and clusters, including the asteroid belt on the Sun
bool FSOLNpcScaleDefaultLayoutTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FLayout layout = DefaultLayout();
    TestTrue(TEXT("At least one lane"), layout.Lanes.Num() >= 1);
    TestTrue(TEXT("At least one cluster"), layout.Clusters.Num() >= 1);
    for (const FLaneDef& lane : layout.Lanes)
    {
        const FString label = lane.BodyA.ToString() + TEXT("-") + lane.BodyB.ToString();
        TestTrue(*(TEXT("Lane endpoints named: ") + label), !lane.BodyA.IsNone() && !lane.BodyB.IsNone());
        TestTrue(*(TEXT("Lane endpoints differ: ") + label), lane.BodyA != lane.BodyB);
        TestTrue(*(TEXT("Lane radius > 0: ") + label), lane.RadiusM > 0.0 && FMath::IsFinite(lane.RadiusM));
        TestTrue(*(TEXT("Lane weight > 0: ") + label), lane.Weight > 0.0 && FMath::IsFinite(lane.Weight));
    }
    bool bHasBelt = false;
    for (const FClusterDef& cluster : layout.Clusters)
    {
        const FString label = cluster.Body.ToString();
        TestTrue(*(TEXT("Cluster body named: ") + label), !cluster.Body.IsNone());
        TestTrue(*(TEXT("Cluster 0 < inner < outer: ") + label), cluster.InnerRadiusM > 0.0
            && cluster.InnerRadiusM < cluster.OuterRadiusM && FMath::IsFinite(cluster.OuterRadiusM));
        TestTrue(*(TEXT("Cluster inclination cap in [0, pi]: ") + label), cluster.MaxInclinationRad >= 0.0
            && cluster.MaxInclinationRad <= UE_DOUBLE_PI);
        TestTrue(*(TEXT("Cluster weight > 0: ") + label), cluster.Weight > 0.0 && FMath::IsFinite(cluster.Weight));
        bHasBelt = bHasBelt || (cluster.Body == FName(TEXT("Sun")) && cluster.InnerRadiusM >= 2.0 * SOLTestHelpers::AU_M
            && cluster.OuterRadiusM <= 3.5 * SOLTestHelpers::AU_M);
    }
    TestTrue(TEXT("Includes the asteroid belt (Sun, 2.0-3.5 AU)"), bHasBelt);
    TestTrue(TEXT("Lane speeds 0 < min <= max"), layout.LaneMinSpeedMps > 0.0
        && layout.LaneMinSpeedMps <= layout.LaneMaxSpeedMps && FMath::IsFinite(layout.LaneMaxSpeedMps));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ramp and frame statistics
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleRampStagesTest, "SOLTest.NpcScaleMath.RampStages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ramp climbs the 10k / 100k / 250k / 500k / 1M ladder up to the requested count
bool FSOLNpcScaleRampStagesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    TestTrue(TEXT("1M: the full ladder"),
        BuildRampStages(1000000) == TArray<int32>({ 10000, 100000, 250000, 500000, 1000000 }));
    TestTrue(TEXT("Above 1M clamps to the full ladder"),
        BuildRampStages(2000000) == TArray<int32>({ 10000, 100000, 250000, 500000, 1000000 }));
    TestTrue(TEXT("300k: ladder below, then 300k"),
        BuildRampStages(300000) == TArray<int32>({ 10000, 100000, 250000, 300000 }));
    TestTrue(TEXT("100k: exact ladder entry"), BuildRampStages(100000) == TArray<int32>({ 10000, 100000 }));
    TestTrue(TEXT("10k: one stage"), BuildRampStages(10000) == TArray<int32>({ 10000 }));
    TestTrue(TEXT("5000: one stage"), BuildRampStages(5000) == TArray<int32>({ 5000 }));
    TestTrue(TEXT("1: one stage"), BuildRampStages(1) == TArray<int32>({ 1 }));
    TestEqual(TEXT("0: no stages"), BuildRampStages(0).Num(), 0);
    TestEqual(TEXT("Negative: no stages"), BuildRampStages(-5).Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleRampStateMachineTest, "SOLTest.NpcScaleMath.RampStateMachine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each stage settles, then records frames for its measure time, then hands over to the next stage or Done
bool FSOLNpcScaleRampStateMachineTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const int32 stageCount = 2;
    const double settle = 1.0;
    const double measure = 2.0;
    const double dt = 0.25;

    FRampState state = StartRamp(stageCount);
    TestTrue(TEXT("Starts settling stage 0"),
        NpcScaleRampStatesEqual(state, NpcScaleRampState(ERampPhase::Settling, 0, 0.0)));

    for (int32 stage = 0; stage < stageCount; ++stage)
    {
        // Settling: 4 steps of 0.25 s, no frame recorded; the 4th step enters Measuring
        for (int32 step = 1; step <= 4; ++step)
        {
            const FRampStep result = StepRamp(state, stageCount, settle, measure, dt);
            TestFalse(*FString::Printf(TEXT("Stage %d settle step %d: no record"), stage, step), result.bRecordFrame);
            TestFalse(*FString::Printf(TEXT("Stage %d settle step %d: not finished"), stage, step),
                result.bStageFinished);
            const FRampState expected = step < 4
                ? NpcScaleRampState(ERampPhase::Settling, stage, step * dt)
                : NpcScaleRampState(ERampPhase::Measuring, stage, 0.0);
            TestTrue(*FString::Printf(TEXT("Stage %d settle step %d: state"), stage, step),
                NpcScaleRampStatesEqual(result.State, expected));
            state = result.State;
        }

        // Measuring: 8 recorded steps, the 8th finishes the stage
        for (int32 step = 1; step <= 8; ++step)
        {
            const FRampStep result = StepRamp(state, stageCount, settle, measure, dt);
            TestTrue(*FString::Printf(TEXT("Stage %d measure step %d: recorded"), stage, step), result.bRecordFrame);
            const bool bLast = step == 8;
            TestTrue(*FString::Printf(TEXT("Stage %d measure step %d: finished flag"), stage, step),
                result.bStageFinished == bLast);
            if (bLast)
            {
                TestEqual(*FString::Printf(TEXT("Stage %d: finished index"), stage), result.FinishedStageIndex, stage);
                const FRampState expected = stage + 1 < stageCount
                    ? NpcScaleRampState(ERampPhase::Settling, stage + 1, 0.0)
                    : NpcScaleRampState(ERampPhase::Done, INDEX_NONE, 0.0);
                TestTrue(*FString::Printf(TEXT("Stage %d: next state"), stage),
                    NpcScaleRampStatesEqual(result.State, expected));
            }
            else
            {
                TestTrue(*FString::Printf(TEXT("Stage %d measure step %d: state"), stage, step),
                    NpcScaleRampStatesEqual(result.State, NpcScaleRampState(ERampPhase::Measuring, stage, step * dt)));
            }
            state = result.State;
        }
    }

    // Done stays Done
    const FRampStep done = StepRamp(state, stageCount, settle, measure, dt);
    TestTrue(TEXT("Done is unchanged"), NpcScaleRampStatesEqual(done.State, state));
    TestFalse(TEXT("Done: no record"), done.bRecordFrame);
    TestFalse(TEXT("Done: not finished"), done.bStageFinished);
    TestEqual(TEXT("Done: no finished index"), done.FinishedStageIndex, static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleRampEdgeCasesTest, "SOLTest.NpcScaleMath.RampEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Invalid frame times, zero durations, long frames, idle and out-of-range states follow the contract
bool FSOLNpcScaleRampEdgeCasesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    TestTrue(TEXT("StartRamp(0) is Done"),
        NpcScaleRampStatesEqual(StartRamp(0), NpcScaleRampState(ERampPhase::Done, INDEX_NONE, 0.0)));
    TestTrue(TEXT("StartRamp(-3) is Done"),
        NpcScaleRampStatesEqual(StartRamp(-3), NpcScaleRampState(ERampPhase::Done, INDEX_NONE, 0.0)));
    TestTrue(TEXT("StartRamp(5) settles stage 0"),
        NpcScaleRampStatesEqual(StartRamp(5), NpcScaleRampState(ERampPhase::Settling, 0, 0.0)));

    // Invalid dt: nothing at all happens, even with zero durations
    const FRampState measuring = NpcScaleRampState(ERampPhase::Measuring, 0, 0.5);
    for (const double badDt : { NpcScaleNaN(), NpcScaleInf(), -1.0, 0.0 })
    {
        const FRampStep result = StepRamp(measuring, 2, 0.0, 0.0, badDt);
        TestTrue(*FString::Printf(TEXT("Invalid dt %f: unchanged"), badDt), NpcScaleRampStatesEqual(result.State,
            measuring));
        TestFalse(*FString::Printf(TEXT("Invalid dt %f: not recorded"), badDt), result.bRecordFrame);
        TestFalse(*FString::Printf(TEXT("Invalid dt %f: not finished"), badDt), result.bStageFinished);
    }

    // Zero, negative or NaN durations count as 0: one valid step moves on
    const FRampState settling = NpcScaleRampState(ERampPhase::Settling, 0, 0.0);
    TestTrue(TEXT("Zero settle: first step measures"),
        StepRamp(settling, 2, 0.0, 2.0, 0.01).State.Phase == ERampPhase::Measuring);
    TestTrue(TEXT("NaN settle: first step measures"),
        StepRamp(settling, 2, NpcScaleNaN(), 2.0, 0.01).State.Phase == ERampPhase::Measuring);
    const FRampStep zeroMeasure = StepRamp(NpcScaleRampState(ERampPhase::Measuring, 0, 0.0), 2, 1.0, -3.0, 0.01);
    TestTrue(TEXT("Negative measure: the first measured frame is recorded"), zeroMeasure.bRecordFrame);
    TestTrue(TEXT("Negative measure: and finishes the stage"), zeroMeasure.bStageFinished);
    TestTrue(TEXT("Negative measure: next stage settles"),
        NpcScaleRampStatesEqual(zeroMeasure.State, NpcScaleRampState(ERampPhase::Settling, 1, 0.0)));

    // A long frame makes at most one transition, with no carry-over
    const FRampStep longSettle = StepRamp(settling, 2, 1.0, 2.0, 100.0);
    TestTrue(TEXT("Long frame while settling: measuring from 0"),
        NpcScaleRampStatesEqual(longSettle.State, NpcScaleRampState(ERampPhase::Measuring, 0, 0.0)));
    TestFalse(TEXT("Long frame while settling: not finished"), longSettle.bStageFinished);
    TestFalse(TEXT("Long frame while settling: not recorded"), longSettle.bRecordFrame);
    const FRampStep longMeasure = StepRamp(NpcScaleRampState(ERampPhase::Measuring, 0, 0.0), 3, 1.0, 2.0, 100.0);
    TestTrue(TEXT("Long frame while measuring: finishes only this stage"),
        NpcScaleRampStatesEqual(longMeasure.State, NpcScaleRampState(ERampPhase::Settling, 1, 0.0)));
    TestEqual(TEXT("Long frame while measuring: finished index"), longMeasure.FinishedStageIndex, 0);

    // Idle and out-of-range stages are left alone
    const FRampState idle;
    const FRampStep idleStep = StepRamp(idle, 2, 1.0, 2.0, 0.1);
    TestTrue(TEXT("Idle is unchanged"), NpcScaleRampStatesEqual(idleStep.State, idle));
    TestFalse(TEXT("Idle: not recorded"), idleStep.bRecordFrame);
    for (const int32 badIndex : { -1, 2, 5 })
    {
        const FRampState bad = NpcScaleRampState(ERampPhase::Measuring, badIndex, 0.5);
        const FRampStep result = StepRamp(bad, 2, 1.0, 2.0, 0.1);
        TestTrue(*FString::Printf(TEXT("Stage index %d out of range: unchanged"), badIndex),
            NpcScaleRampStatesEqual(result.State, bad));
        TestFalse(*FString::Printf(TEXT("Stage index %d out of range: not recorded"), badIndex), result.bRecordFrame);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleRampRunTest, "SOLTest.NpcScaleMath.RampRun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A simulated 60 fps ramp to 250k visits each stage once, asks for its count, and records about 2 s of frames
bool FSOLNpcScaleRampRunTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const TArray<int32> stages = BuildRampStages(250000);
    TestEqual(TEXT("Three stages"), stages.Num(), 3);
    FRampState state = StartRamp(stages.Num());
    TArray<int32> recordedPerStage;
    recordedPerStage.SetNumZeroed(stages.Num());
    TArray<int32> finishedOrder;
    bool bCountMatches = true;
    for (int32 frame = 0; frame < 60 * 30 && state.Phase != ERampPhase::Done; ++frame)
    {
        const int32 stageBefore = state.StageIndex;
        bCountMatches = bCountMatches && StageNpcCount(state, stages) == stages[stageBefore];
        const FRampStep result = StepRamp(state, stages.Num(), 1.0, 2.0, 1.0 / 60.0);
        if (result.bRecordFrame)
        {
            recordedPerStage[stageBefore] += 1;
        }
        if (result.bStageFinished)
        {
            finishedOrder.Add(result.FinishedStageIndex);
        }
        state = result.State;
    }
    TestTrue(TEXT("The ramp finished"), state.Phase == ERampPhase::Done);
    TestTrue(TEXT("Each stage asked for its own count"), bCountMatches);
    TestTrue(TEXT("Stages finished in order"), finishedOrder == TArray<int32>({ 0, 1, 2 }));
    for (int32 stage = 0; stage < stages.Num(); ++stage)
    {
        TestTrue(*FString::Printf(TEXT("Stage %d recorded about 120 frames"), stage),
            recordedPerStage[stage] >= 119 && recordedPerStage[stage] <= 121);
    }
    TestEqual(TEXT("Done asks for no count"), StageNpcCount(state, stages), static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleStageNpcCountTest, "SOLTest.NpcScaleMath.StageNpcCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ramp asks for its stage's count while settling or measuring, and for nothing otherwise
bool FSOLNpcScaleStageNpcCountTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const TArray<int32> stages = { 10000, 100000 };
    TestEqual(TEXT("Settling stage 0"), StageNpcCount(NpcScaleRampState(ERampPhase::Settling, 0, 0.0), stages), 10000);
    TestEqual(TEXT("Measuring stage 1"), StageNpcCount(NpcScaleRampState(ERampPhase::Measuring, 1, 0.3), stages),
        100000);
    TestEqual(TEXT("Idle"), StageNpcCount(NpcScaleRampState(ERampPhase::Idle, 0, 0.0), stages),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Done"), StageNpcCount(NpcScaleRampState(ERampPhase::Done, INDEX_NONE, 0.0), stages),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Index past the end"), StageNpcCount(NpcScaleRampState(ERampPhase::Settling, 2, 0.0), stages),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Negative index"), StageNpcCount(NpcScaleRampState(ERampPhase::Measuring, -1, 0.0), stages),
        static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScalePercentileTest, "SOLTest.NpcScaleMath.Percentile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Nearest-rank percentiles with the contract's clamps
bool FSOLNpcScalePercentileTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const TArray<double> four = { 10.0, 20.0, 30.0, 40.0 };
    TestEqual(TEXT("0.5 of 4"), PercentileNearestRank(four, 0.5), 20.0);
    TestEqual(TEXT("0.25 of 4 (exact rank 1)"), PercentileNearestRank(four, 0.25), 10.0);
    TestEqual(TEXT("0.26 of 4"), PercentileNearestRank(four, 0.26), 20.0);
    TestEqual(TEXT("0.95 of 4"), PercentileNearestRank(four, 0.95), 40.0);
    TestEqual(TEXT("0 gives the first"), PercentileNearestRank(four, 0.0), 10.0);
    TestEqual(TEXT("Negative gives the first"), PercentileNearestRank(four, -1.0), 10.0);
    TestEqual(TEXT("NaN gives the first"), PercentileNearestRank(four, NpcScaleNaN()), 10.0);
    TestEqual(TEXT("1 gives the last"), PercentileNearestRank(four, 1.0), 40.0);
    TestEqual(TEXT("2 gives the last"), PercentileNearestRank(four, 2.0), 40.0);
    const TArray<double> empty;
    TestEqual(TEXT("Empty gives 0"), PercentileNearestRank(empty, 0.5), 0.0);
    const TArray<double> one = { 7.0 };
    TestEqual(TEXT("Single value"), PercentileNearestRank(one, 0.99), 7.0);

    TArray<double> hundred;
    for (int32 value = 1; value <= 100; ++value)
    {
        hundred.Add(value);
    }
    TestEqual(TEXT("P50 of 1..100"), PercentileNearestRank(hundred, 0.50), 50.0);
    TestEqual(TEXT("P95 of 1..100"), PercentileNearestRank(hundred, 0.95), 95.0);
    TestEqual(TEXT("P99 of 1..100"), PercentileNearestRank(hundred, 0.99), 99.0);
    TestEqual(TEXT("P10 of 1..100"), PercentileNearestRank(hundred, 0.10), 10.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSummarizeTest, "SOLTest.NpcScaleMath.SummarizeFrameTimes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Summaries drop invalid frame times, sort in place, and report mean, min, max and percentiles
bool FSOLNpcScaleSummarizeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    TArray<double> frames;
    for (int32 index = 0; index < 100; ++index)
    {
        frames.Add(static_cast<double>((index * 37) % 100 + 1));    // 1..100 shuffled
    }
    frames.Insert(NpcScaleNaN(), 10);
    frames.Insert(-3.0, 20);
    frames.Add(NpcScaleInf());
    frames.Add(-NpcScaleInf());

    const FFrameStats stats = SummarizeFrameTimes(frames);
    TestEqual(TEXT("Count"), stats.Count, 100);
    TestNearlyEqual(TEXT("Mean"), stats.MeanMs, 50.5, NPCSCALE_EPS);
    TestEqual(TEXT("Min"), stats.MinMs, 1.0);
    TestEqual(TEXT("Max"), stats.MaxMs, 100.0);
    TestEqual(TEXT("P50"), stats.P50Ms, 50.0);
    TestEqual(TEXT("P95"), stats.P95Ms, 95.0);
    TestEqual(TEXT("P99"), stats.P99Ms, 99.0);
    TestEqual(TEXT("Invalid entries removed"), frames.Num(), 100);
    bool bSorted = true;
    for (int32 index = 0; index < frames.Num(); ++index)
    {
        bSorted = bSorted && frames[index] == static_cast<double>(index + 1);
    }
    TestTrue(TEXT("Sorted ascending in place"), bSorted);

    TArray<double> empty;
    const FFrameStats none = SummarizeFrameTimes(empty);
    TestEqual(TEXT("Empty: count 0"), none.Count, 0);
    TestTrue(TEXT("Empty: all zero"), none.MeanMs == 0.0 && none.MinMs == 0.0 && none.MaxMs == 0.0
        && none.P50Ms == 0.0 && none.P95Ms == 0.0 && none.P99Ms == 0.0);

    TArray<double> onlyInvalid = { NpcScaleNaN(), -1.0, NpcScaleInf() };
    const FFrameStats invalid = SummarizeFrameTimes(onlyInvalid);
    TestEqual(TEXT("Only invalid: count 0"), invalid.Count, 0);
    TestEqual(TEXT("Only invalid: emptied"), onlyInvalid.Num(), 0);
    TestEqual(TEXT("Only invalid: mean 0"), invalid.MeanMs, 0.0);

    TArray<double> zeros = { 0.0, 0.0 };
    const FFrameStats zeroStats = SummarizeFrameTimes(zeros);
    TestEqual(TEXT("Zero is a valid frame time"), zeroStats.Count, 2);
    TestEqual(TEXT("Zero frames: mean 0"), zeroStats.MeanMs, 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleFrameTargetTest, "SOLTest.NpcScaleMath.FrameTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A stage passes when it has frames and its 95th-percentile frame time fits the target frame rate
bool FSOLNpcScaleFrameTargetTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    TestTrue(TEXT("P95 16 ms passes 60 fps"), MeetsFrameTarget(NpcScaleStats(10, 15.0, 10.0, 15.0, 16.0, 20.0, 30.0),
        60.0));
    TestTrue(TEXT("P95 exactly 1000/60 passes"),
        MeetsFrameTarget(NpcScaleStats(10, 15.0, 10.0, 15.0, 1000.0 / 60.0, 20.0, 30.0), 60.0));
    TestFalse(TEXT("P95 17 ms fails 60 fps"), MeetsFrameTarget(NpcScaleStats(10, 12.0, 10.0, 12.0, 17.0, 17.0, 17.0),
        60.0));
    TestFalse(TEXT("A good mean does not hide a bad P95"),
        MeetsFrameTarget(NpcScaleStats(10, 10.0, 5.0, 8.0, 18.0, 25.0, 40.0), 60.0));
    TestTrue(TEXT("A bad P99 does not fail a good P95"),
        MeetsFrameTarget(NpcScaleStats(10, 10.0, 5.0, 8.0, 16.0, 25.0, 40.0), 60.0));
    TestTrue(TEXT("30 fps target"), MeetsFrameTarget(NpcScaleStats(10, 25.0, 20.0, 25.0, 30.0, 33.0, 34.0), 30.0));
    TestFalse(TEXT("No frames fails"), MeetsFrameTarget(NpcScaleStats(0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), 60.0));
    TestFalse(TEXT("Zero target fails"), MeetsFrameTarget(NpcScaleStats(10, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0), 0.0));
    TestFalse(TEXT("Negative target fails"), MeetsFrameTarget(NpcScaleStats(10, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0), -60.0));
    TestFalse(TEXT("NaN target fails"), MeetsFrameTarget(NpcScaleStats(10, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0),
        NpcScaleNaN()));
    TestFalse(TEXT("Infinite target fails"), MeetsFrameTarget(NpcScaleStats(10, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0),
        NpcScaleInf()));
    TestEqual(TEXT("NPC_TARGET_FPS is 60"), SOL::NPC_TARGET_FPS, 60.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleRampLineTest, "SOLTest.NpcScaleMath.RampStageLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The per-stage log line has the exact machine-readable format
bool FSOLNpcScaleRampLineTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FFrameStats stats = NpcScaleStats(600, 12.5, 10.0, 12.25, 15.126, 16.5, 20.0);
    TestEqual(TEXT("Passing stage"), FormatRampStageLine(1000000, stats, 20480.4, true),
        FString(TEXT("NpcRamp stage=1000000 frames=600 meanMs=12.50 p50Ms=12.25 p95Ms=15.13 p99Ms=16.50 maxMs=20.00 ")
            TEXT("fps=80.0 memMB=20480 result=PASS")));
    TestEqual(TEXT("Failing stage"), FormatRampStageLine(10000, NpcScaleStats(5, 20.0, 18.0, 20.0, 21.0, 22.0, 23.0),
        512.0, false),
        FString(TEXT("NpcRamp stage=10000 frames=5 meanMs=20.00 p50Ms=20.00 p95Ms=21.00 p99Ms=22.00 maxMs=23.00 ")
            TEXT("fps=50.0 memMB=512 result=FAIL")));
    TestEqual(TEXT("Empty stats"), FormatRampStageLine(0, FFrameStats(), 0.4, false),
        FString(TEXT("NpcRamp stage=0 frames=0 meanMs=0.00 p50Ms=0.00 p95Ms=0.00 p99Ms=0.00 maxMs=0.00 fps=0.0 ")
            TEXT("memMB=0 result=FAIL")));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Demo options
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleDescriptorsTest, "SOLTest.NpcScaleMath.OptionDescriptors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One well-formed descriptor per option, in enum order, with the SDD's ranges and defaults
bool FSOLNpcScaleDescriptorsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const TConstArrayView<FOptionDesc> descriptors = OptionDescriptors();
    TestEqual(TEXT("One descriptor per option"), descriptors.Num(), NpcScaleOptionCount());
    if (descriptors.Num() != NpcScaleOptionCount())
    {
        return false;
    }

    // Generic invariants
    for (int32 index = 0; index < descriptors.Num(); ++index)
    {
        const FOptionDesc& desc = descriptors[index];
        const FString label = FString::Printf(TEXT("Option %d"), index);
        TestTrue(*(label + TEXT(": id matches its slot")), desc.Id == NpcScaleOptionId(index));
        TestTrue(*(label + TEXT(": min <= default <= max")), desc.Min <= desc.Default && desc.Default <= desc.Max);
        TestTrue(*(label + TEXT(": finite bounds")), FMath::IsFinite(desc.Min) && FMath::IsFinite(desc.Max));
        const bool bPresetStepped = desc.Id == EDemoOption::NpcCount;
        if (desc.bBool)
        {
            TestTrue(*(label + TEXT(": bool range 0-1, step 1")), desc.Min == 0.0 && desc.Max == 1.0
                && desc.Step == 1.0);
        }
        else if (bPresetStepped)
        {
            TestEqual(*(label + TEXT(": preset-stepped count has step 0")), desc.Step, 0.0);
        }
        else
        {
            TestTrue(*(label + TEXT(": step > 0")), desc.Step > 0.0);
        }
        if (desc.bInteger)
        {
            TestTrue(*(label + TEXT(": integral bounds and default")), desc.Min == FMath::RoundToDouble(desc.Min)
                && desc.Max == FMath::RoundToDouble(desc.Max) && desc.Default == FMath::RoundToDouble(desc.Default));
        }
    }

    // Which options are bools and integers
    for (const EDemoOption id : { EDemoOption::NpcCombat, EDemoOption::ShowStats })
    {
        TestTrue(*FString::Printf(TEXT("Option %d is a bool"), static_cast<int32>(id)), NpcScaleDesc(id).bBool);
    }
    for (const EDemoOption id : { EDemoOption::NpcCount, EDemoOption::BattleShipCount, EDemoOption::BattleCount,
        EDemoOption::FullAiBudget, EDemoOption::MidAiBudget, EDemoOption::MeshBudget, EDemoOption::InstancedBudget })
    {
        TestTrue(*FString::Printf(TEXT("Option %d is an integer"), static_cast<int32>(id)), NpcScaleDesc(id).bInteger);
        TestFalse(*FString::Printf(TEXT("Option %d is not a bool"), static_cast<int32>(id)), NpcScaleDesc(id).bBool);
    }

    // Ranges from SDD 11 section 3.8
    struct FRange
    {
        EDemoOption Id;
        double Min;
        double Max;
    };
    const FRange ranges[] = {
        { EDemoOption::NpcCount, 0.0, 1000000.0 },
        { EDemoOption::LaneFraction, 0.0, 1.0 },
        { EDemoOption::BattleShipCount, 0.0, 20000.0 },
        { EDemoOption::BattleCount, 1.0, 16.0 },
        { EDemoOption::BattleRadiusM, 500.0, 20000.0 },
        { EDemoOption::BattleMinDistanceM, 1000.0, 200000.0 },
        { EDemoOption::BattleMaxDistanceM, 1000.0, 200000.0 },
        { EDemoOption::FullAiRadiusM, 5000.0, 500000.0 },
        { EDemoOption::MidAiRadiusM, 50000.0, 20000000.0 },
        { EDemoOption::FullAiBudget, 0.0, 20000.0 },
        { EDemoOption::MidAiBudget, 0.0, 500000.0 },
        { EDemoOption::AiHysteresis, 0.0, 0.5 },
        { EDemoOption::MeshRadiusM, 500.0, 20000.0 },
        { EDemoOption::InstancedRadiusM, 1000.0, 2000000.0 },
        { EDemoOption::MeshBudget, 0.0, 256.0 },
        { EDemoOption::InstancedBudget, 0.0, 200000.0 },
        { EDemoOption::RenderHysteresis, 0.0, 0.5 },
        { EDemoOption::RampSettleS, 1.0, 30.0 },
        { EDemoOption::RampMeasureS, 2.0, 60.0 },
    };
    for (const FRange& range : ranges)
    {
        const FOptionDesc& desc = NpcScaleDesc(range.Id);
        TestEqual(*FString::Printf(TEXT("Option %d min"), static_cast<int32>(range.Id)), desc.Min, range.Min);
        TestEqual(*FString::Printf(TEXT("Option %d max"), static_cast<int32>(range.Id)), desc.Max, range.Max);
    }
    TestEqual(TEXT("NPC_MAX_COUNT"), SOL::NPC_MAX_COUNT, 1000000);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleDefaultOptionsTest, "SOLTest.NpcScaleMath.DefaultOptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The defaults match the descriptors and the SDD table, equal a default-constructed struct, and are coherent
bool FSOLNpcScaleDefaultOptionsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FDemoOptions defaults = DefaultOptions();
    const FDemoOptions constructed;
    TestTrue(TEXT("DefaultOptions equals a default-constructed FDemoOptions"),
        NpcScaleOptionsEqual(defaults, constructed));
    TestTrue(TEXT("Default seed"), defaults.Seed == SOL::NPC_DEFAULT_SEED);
    for (int32 index = 0; index < NpcScaleOptionCount(); ++index)
    {
        TestEqual(*FString::Printf(TEXT("Option %d at its descriptor default"), index),
            GetOption(defaults, NpcScaleOptionId(index)), OptionDescriptors()[index].Default);
    }

    // SDD 11 section 3.8 defaults
    TestEqual(TEXT("Count 1M"), defaults.NpcCount, 1000000);
    TestEqual(TEXT("Lane fraction 0.4"), defaults.LaneFraction, 0.4);
    TestEqual(TEXT("Battle ships 2,000"), defaults.BattleShipCount, 2000);
    TestEqual(TEXT("Battles 4"), defaults.BattleCount, 4);
    TestEqual(TEXT("Battle radius 3 km"), defaults.BattleRadiusM, 3000.0);
    TestEqual(TEXT("Battle min distance 5 km"), defaults.BattleMinDistanceM, 5000.0);
    TestEqual(TEXT("Battle max distance 20 km"), defaults.BattleMaxDistanceM, 20000.0);
    TestEqual(TEXT("Full AI radius 50 km"), defaults.FullAiRadiusM, 50000.0);
    TestEqual(TEXT("Mid AI radius 2,000 km"), defaults.MidAiRadiusM, 2000000.0);
    TestEqual(TEXT("Full AI budget 4,000"), defaults.FullAiBudget, 4000);
    TestEqual(TEXT("Mid AI budget 100,000"), defaults.MidAiBudget, 100000);
    TestEqual(TEXT("AI hysteresis 0.1"), defaults.AiHysteresis, 0.1);
    TestEqual(TEXT("Mesh radius 2 km"), defaults.MeshRadiusM, 2000.0);
    TestEqual(TEXT("Instanced radius 200 km"), defaults.InstancedRadiusM, 200000.0);
    TestEqual(TEXT("Mesh budget 32"), defaults.MeshBudget, 32);
    TestEqual(TEXT("Instanced budget 20,000"), defaults.InstancedBudget, 20000);
    TestEqual(TEXT("Render hysteresis 0.1"), defaults.RenderHysteresis, 0.1);
    TestTrue(TEXT("NPC combat on"), defaults.bNpcCombat);
    TestTrue(TEXT("Stats overlay on"), defaults.bShowStats);
    TestEqual(TEXT("Ramp settle 5 s"), defaults.RampSettleS, 5.0);
    TestEqual(TEXT("Ramp measure 10 s"), defaults.RampMeasureS, 10.0);

    // Coherent: sanitizing changes nothing and reports nothing
    const FSanitizeResult result = SanitizeOptions(defaults);
    TestTrue(TEXT("Defaults report no issue"), result.Issues == EOptionIssue::None);
    TestTrue(TEXT("Defaults unchanged by sanitizing"), NpcScaleOptionsEqual(result.Options, defaults));

    // GetOption with an invalid id
    TestEqual(TEXT("GetOption(Count) is 0"), GetOption(defaults, EDemoOption::Count), 0.0);
    TestEqual(TEXT("GetOption(200) is 0"), GetOption(defaults, NpcScaleOptionId(200)), 0.0);
    TestEqual(TEXT("GetOption reads bools as 1"), GetOption(defaults, EDemoOption::NpcCombat), 1.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSetOptionTest, "SOLTest.NpcScaleMath.SetOption",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Setting an option clamps, rounds integers, maps bools, ignores NaN, then applies the cross-option rules
bool FSOLNpcScaleSetOptionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FDemoOptions defaults = DefaultOptions();

    // Doubles
    TestEqual(TEXT("Lane fraction 0.37 kept (not snapped)"),
        SetOption(defaults, EDemoOption::LaneFraction, 0.37).LaneFraction, 0.37);
    TestEqual(TEXT("Lane fraction 2 clamps to 1"), SetOption(defaults, EDemoOption::LaneFraction, 2.0).LaneFraction,
        1.0);
    TestEqual(TEXT("Lane fraction -1 clamps to 0"), SetOption(defaults, EDemoOption::LaneFraction, -1.0).LaneFraction,
        0.0);
    TestEqual(TEXT("Lane fraction +inf clamps to 1"),
        SetOption(defaults, EDemoOption::LaneFraction, NpcScaleInf()).LaneFraction, 1.0);
    TestEqual(TEXT("Lane fraction -inf clamps to 0"),
        SetOption(defaults, EDemoOption::LaneFraction, -NpcScaleInf()).LaneFraction, 0.0);

    // Integers round to nearest, then clamp
    TestEqual(TEXT("Battle count 3.5 rounds to 4"), SetOption(defaults, EDemoOption::BattleCount, 3.5).BattleCount, 4);
    TestEqual(TEXT("Battle count 3.4 rounds to 3"), SetOption(defaults, EDemoOption::BattleCount, 3.4).BattleCount, 3);
    TestEqual(TEXT("Battle count 0 clamps to 1"), SetOption(defaults, EDemoOption::BattleCount, 0.0).BattleCount, 1);
    TestEqual(TEXT("Battle count 99 clamps to 16"), SetOption(defaults, EDemoOption::BattleCount, 99.0).BattleCount,
        16);
    TestEqual(TEXT("Count 123456.4 rounds down"), SetOption(defaults, EDemoOption::NpcCount, 123456.4).NpcCount,
        123456);
    TestEqual(TEXT("Count 1e7 clamps to 1M"), SetOption(defaults, EDemoOption::NpcCount, 1.0e7).NpcCount, 1000000);
    TestEqual(TEXT("Count +inf clamps to 1M"), SetOption(defaults, EDemoOption::NpcCount, NpcScaleInf()).NpcCount,
        1000000);
    TestEqual(TEXT("Count -5 clamps to 0"), SetOption(defaults, EDemoOption::NpcCount, -5.0).NpcCount, 0);

    // Bools
    TestFalse(TEXT("Combat 0 is off"), SetOption(defaults, EDemoOption::NpcCombat, 0.0).bNpcCombat);
    TestTrue(TEXT("Combat 0.5 is on"),
        SetOption(SetOption(defaults, EDemoOption::NpcCombat, 0.0), EDemoOption::NpcCombat, 0.5).bNpcCombat);
    TestTrue(TEXT("Stats -3 is on"),
        SetOption(SetOption(defaults, EDemoOption::ShowStats, 0.0), EDemoOption::ShowStats, -3.0).bShowStats);

    // NaN and invalid ids change nothing
    for (int32 index = 0; index < NpcScaleOptionCount(); ++index)
    {
        TestTrue(*FString::Printf(TEXT("NaN leaves option %d unchanged"), index),
            NpcScaleOptionsEqual(SetOption(defaults, NpcScaleOptionId(index), NpcScaleNaN()), defaults));
        TestTrue(*FString::Printf(TEXT("Setting option %d to its default changes nothing"), index),
            NpcScaleOptionsEqual(SetOption(defaults, NpcScaleOptionId(index),
                OptionDescriptors()[index].Default), defaults));
    }
    TestTrue(TEXT("Invalid id: unchanged"), NpcScaleOptionsEqual(SetOption(defaults, EDemoOption::Count, 5.0),
        defaults));

    // Cross rules after a set: lowering Mid pulls Instanced down; raising Full pushes Mid up
    const FDemoOptions lowMid = SetOption(defaults, EDemoOption::MidAiRadiusM, 100000.0);
    TestEqual(TEXT("Mid set to 100 km"), lowMid.MidAiRadiusM, 100000.0);
    TestEqual(TEXT("Instanced pulled down to Mid"), lowMid.InstancedRadiusM, 100000.0);
    const FDemoOptions highFull = SetOption(lowMid, EDemoOption::FullAiRadiusM, 300000.0);
    TestEqual(TEXT("Full set to 300 km"), highFull.FullAiRadiusM, 300000.0);
    TestEqual(TEXT("Mid pushed up to Full"), highFull.MidAiRadiusM, 300000.0);
    TestEqual(TEXT("Instanced unchanged"), highFull.InstancedRadiusM, 100000.0);
    const FDemoOptions fewShips = SetOption(defaults, EDemoOption::NpcCount, 1000.0);
    TestEqual(TEXT("Battle ships limited to the count"), fewShips.BattleShipCount, 1000);

    // The seed is never touched
    FDemoOptions seeded = defaults;
    seeded.Seed = 4242u;
    TestTrue(TEXT("Seed kept"), SetOption(seeded, EDemoOption::LaneFraction, 0.9).Seed == 4242u);

    // Any set keeps every option inside its descriptor range
    bool bInRange = true;
    for (int32 index = 0; index < NpcScaleOptionCount(); ++index)
    {
        for (const double value : { -NpcScaleInf(), -1.0e12, -1.0, 0.0, 0.5, 1.0e12, NpcScaleInf() })
        {
            const FDemoOptions result = SetOption(defaults, NpcScaleOptionId(index), value);
            for (int32 check = 0; check < NpcScaleOptionCount(); ++check)
            {
                const double got = GetOption(result, NpcScaleOptionId(check));
                const FOptionDesc& desc = OptionDescriptors()[check];
                bInRange = bInRange && got >= desc.Min && got <= desc.Max;
            }
        }
    }
    TestTrue(TEXT("Every option stays in range after any set"), bInRange);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSanitizeTest, "SOLTest.NpcScaleMath.SanitizeOptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sanitizing resets or clamps bad fields, applies the cross rules in order, and flags what it changed or warns about
bool FSOLNpcScaleSanitizeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FDemoOptions defaults = DefaultOptions();

    // Mid raised, then Instanced lowered to the new Mid, Mesh untouched
    {
        FDemoOptions options = defaults;
        options.FullAiRadiusM = 300000.0;
        options.MidAiRadiusM = 100000.0;
        options.InstancedRadiusM = 1500000.0;
        options.MeshRadiusM = 10000.0;
        const FSanitizeResult result = SanitizeOptions(options);
        TestEqual(TEXT("Mid raised to Full"), result.Options.MidAiRadiusM, 300000.0);
        TestEqual(TEXT("Instanced lowered to Mid"), result.Options.InstancedRadiusM, 300000.0);
        TestEqual(TEXT("Mesh untouched"), result.Options.MeshRadiusM, 10000.0);
        TestTrue(TEXT("MidRadiusRaised"), EnumHasAnyFlags(result.Issues, EOptionIssue::MidRadiusRaised));
        TestTrue(TEXT("InstancedRadiusLowered"), EnumHasAnyFlags(result.Issues, EOptionIssue::InstancedRadiusLowered));
        TestFalse(TEXT("No MeshRadiusLowered"), EnumHasAnyFlags(result.Issues, EOptionIssue::MeshRadiusLowered));
        TestFalse(TEXT("No ValueClamped"), EnumHasAnyFlags(result.Issues, EOptionIssue::ValueClamped));
    }

    // Mesh lowered to Instanced
    {
        FDemoOptions options = defaults;
        options.MeshRadiusM = 10000.0;
        options.InstancedRadiusM = 5000.0;
        const FSanitizeResult result = SanitizeOptions(options);
        TestEqual(TEXT("Mesh lowered to Instanced"), result.Options.MeshRadiusM, 5000.0);
        TestTrue(TEXT("Only MeshRadiusLowered"), result.Issues == EOptionIssue::MeshRadiusLowered);
    }

    // Battle ships limited by the count
    {
        FDemoOptions options = defaults;
        options.NpcCount = 500;
        const FSanitizeResult result = SanitizeOptions(options);
        TestEqual(TEXT("Battle ships limited to 500"), result.Options.BattleShipCount, 500);
        TestTrue(TEXT("Only BattleShipsLimited"), result.Issues == EOptionIssue::BattleShipsLimited);
    }

    // Battle max distance raised to the min
    {
        FDemoOptions options = defaults;
        options.BattleMinDistanceM = 30000.0;
        options.BattleMaxDistanceM = 10000.0;
        const FSanitizeResult result = SanitizeOptions(options);
        TestEqual(TEXT("Max raised to min"), result.Options.BattleMaxDistanceM, 30000.0);
        TestEqual(TEXT("Min kept"), result.Options.BattleMinDistanceM, 30000.0);
        TestTrue(TEXT("Only BattleDistanceRaised"), result.Issues == EOptionIssue::BattleDistanceRaised);
    }

    // Mid budget raised to the Full budget
    {
        FDemoOptions options = defaults;
        options.FullAiBudget = 8000;
        options.MidAiBudget = 3000;
        const FSanitizeResult result = SanitizeOptions(options);
        TestEqual(TEXT("Mid budget raised to Full"), result.Options.MidAiBudget, 8000);
        TestEqual(TEXT("Full budget kept"), result.Options.FullAiBudget, 8000);
        TestTrue(TEXT("Only MidBudgetRaised"), result.Issues == EOptionIssue::MidBudgetRaised);
    }

    // Equal budgets are fine
    {
        FDemoOptions options = defaults;
        options.FullAiBudget = 5000;
        options.MidAiBudget = 5000;
        TestTrue(TEXT("Mid budget == Full budget: no issue"), SanitizeOptions(options).Issues == EOptionIssue::None);
    }

    // Warnings change nothing
    {
        FDemoOptions options = defaults;
        options.BattleShipCount = 5000;
        options.BattleMaxDistanceM = 60000.0;
        const FSanitizeResult result = SanitizeOptions(options);
        TestEqual(TEXT("Battle ships kept"), result.Options.BattleShipCount, 5000);
        TestEqual(TEXT("Battle distance kept"), result.Options.BattleMaxDistanceM, 60000.0);
        TestTrue(TEXT("Exactly the two warnings"),
            result.Issues == (EOptionIssue::BattleShipsOverFullBudget | EOptionIssue::BattlesBeyondFullAi));
    }

    // Battles exactly reaching the Full radius are not a warning
    {
        FDemoOptions options = defaults;
        options.BattleMaxDistanceM = 47000.0;    // + 3 km radius = 50 km = Full radius
        TestTrue(TEXT("Max + radius == Full radius: no warning"),
            SanitizeOptions(options).Issues == EOptionIssue::None);
    }

    // Non-finite doubles reset to their default; out-of-range values clamp
    {
        FDemoOptions options = defaults;
        options.LaneFraction = NpcScaleNaN();
        options.AiHysteresis = NpcScaleInf();
        options.RampSettleS = 100.0;
        const FSanitizeResult result = SanitizeOptions(options);
        TestEqual(TEXT("NaN lane fraction reset to default"), result.Options.LaneFraction, 0.4);
        TestEqual(TEXT("Infinite hysteresis reset to default (not clamped)"), result.Options.AiHysteresis, 0.1);
        TestEqual(TEXT("Settle clamped to 30 s"), result.Options.RampSettleS, 30.0);
        TestTrue(TEXT("Only ValueClamped"), result.Issues == EOptionIssue::ValueClamped);
    }
    {
        FDemoOptions options = defaults;
        options.NpcCount = -5;
        const FSanitizeResult result = SanitizeOptions(options);
        TestEqual(TEXT("Negative count clamps to 0"), result.Options.NpcCount, 0);
        TestEqual(TEXT("Then battle ships limited to 0"), result.Options.BattleShipCount, 0);
        TestTrue(TEXT("ValueClamped and BattleShipsLimited"),
            result.Issues == (EOptionIssue::ValueClamped | EOptionIssue::BattleShipsLimited));
    }
    {
        FDemoOptions options = defaults;
        options.NpcCount = 5000000;
        options.MeshBudget = 1000;
        TestEqual(TEXT("Count clamps to 1M"), SanitizeOptions(options).Options.NpcCount, 1000000);
        TestEqual(TEXT("Mesh budget clamps to 256"), SanitizeOptions(options).Options.MeshBudget, 256);
    }

    // The seed is untouched; sanitizing is idempotent for corrections
    {
        FDemoOptions messy = defaults;
        messy.Seed = 99u;
        messy.NpcCount = 300;
        messy.LaneFraction = -2.0;
        messy.BattleCount = 0;
        messy.FullAiRadiusM = 400000.0;
        messy.MidAiRadiusM = NpcScaleNaN();
        messy.InstancedRadiusM = 3000000.0;
        messy.MeshRadiusM = 50.0;
        messy.BattleMinDistanceM = 150000.0;
        messy.BattleMaxDistanceM = 2000.0;
        const FSanitizeResult first = SanitizeOptions(messy);
        const FSanitizeResult second = SanitizeOptions(first.Options);
        TestTrue(TEXT("Seed untouched"), first.Options.Seed == 99u);
        TestTrue(TEXT("Second pass changes nothing"), NpcScaleOptionsEqual(first.Options, second.Options));
        TestFalse(TEXT("Second pass reports no correction"),
            EnumHasAnyFlags(second.Issues, NpcScaleCorrectionFlags()));
        TestTrue(TEXT("First pass corrected something"), EnumHasAnyFlags(first.Issues, NpcScaleCorrectionFlags()));
        TestEqual(TEXT("NaN Mid reset to default 2,000 km, still >= Full"), first.Options.MidAiRadiusM, 2000000.0);
        TestEqual(TEXT("Instanced clamped to 2,000 km, then <= Mid"), first.Options.InstancedRadiusM, 2000000.0);
        TestEqual(TEXT("Mesh clamped to 500 m"), first.Options.MeshRadiusM, 500.0);
    }

    // Random garbage always ends inside the ranges with every cross rule holding
    bool bValid = true;
    for (int32 trial = 0; trial < 500; ++trial)
    {
        FDemoOptions options = defaults;
        for (int32 index = 0; index < NpcScaleOptionCount(); ++index)
        {
            const FOptionDesc& desc = OptionDescriptors()[index];
            const double unit = HashUnit(NPCSCALE_SEED, static_cast<uint32>(trial), static_cast<uint32>(index));
            const double value = desc.Min - 0.5 * (desc.Max - desc.Min) + unit * 2.0 * (desc.Max - desc.Min);
            options = SetOption(options, NpcScaleOptionId(index), value);
        }
        const FDemoOptions& o = options;
        for (int32 index = 0; index < NpcScaleOptionCount(); ++index)
        {
            const double got = GetOption(o, NpcScaleOptionId(index));
            bValid = bValid && got >= OptionDescriptors()[index].Min && got <= OptionDescriptors()[index].Max;
        }
        bValid = bValid && o.MidAiRadiusM >= o.FullAiRadiusM && o.InstancedRadiusM <= o.MidAiRadiusM
            && o.MeshRadiusM <= o.InstancedRadiusM && o.BattleShipCount <= o.NpcCount
            && o.BattleMaxDistanceM >= o.BattleMinDistanceM;
    }
    TestTrue(TEXT("Random options end valid"), bValid);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleStepOptionTest, "SOLTest.NpcScaleMath.StepOption",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Steppers move the count through the presets, toggle bools and move other options by one step
bool FSOLNpcScaleStepOptionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    const FDemoOptions defaults = DefaultOptions();

    // Count presets: 0, 1k, 10k, 100k, 250k, 500k, 1M
    TestEqual(TEXT("1M up stays"), StepOption(defaults, EDemoOption::NpcCount, 1).NpcCount, 1000000);
    TestEqual(TEXT("1M down to 500k"), StepOption(defaults, EDemoOption::NpcCount, -1).NpcCount, 500000);
    TestEqual(TEXT("Direction -7 is one step"), StepOption(defaults, EDemoOption::NpcCount, -7).NpcCount, 500000);
    const FDemoOptions odd = SetOption(defaults, EDemoOption::NpcCount, 123456.0);
    TestEqual(TEXT("123456 up to 250k"), StepOption(odd, EDemoOption::NpcCount, 1).NpcCount, 250000);
    TestEqual(TEXT("123456 down to 100k"), StepOption(odd, EDemoOption::NpcCount, -1).NpcCount, 100000);
    const FDemoOptions none = SetOption(defaults, EDemoOption::NpcCount, 0.0);
    TestEqual(TEXT("0 down stays"), StepOption(none, EDemoOption::NpcCount, -1).NpcCount, 0);
    TestEqual(TEXT("0 up to 1k"), StepOption(none, EDemoOption::NpcCount, 1).NpcCount, 1000);
    const FDemoOptions tenK = SetOption(defaults, EDemoOption::NpcCount, 10000.0);
    TestEqual(TEXT("Exact preset 10k up to 100k"), StepOption(tenK, EDemoOption::NpcCount, 1).NpcCount, 100000);
    TestEqual(TEXT("Exact preset 10k down to 1k"), StepOption(tenK, EDemoOption::NpcCount, -1).NpcCount, 1000);
    TestEqual(TEXT("Stepping the count down limits the battle ships"),
        StepOption(tenK, EDemoOption::NpcCount, -1).BattleShipCount, 1000);

    // Direction 0 and invalid ids change nothing
    TestTrue(TEXT("Direction 0: unchanged"), NpcScaleOptionsEqual(StepOption(defaults, EDemoOption::LaneFraction, 0),
        defaults));
    TestTrue(TEXT("Invalid id: unchanged"), NpcScaleOptionsEqual(StepOption(defaults, EDemoOption::Count, 1),
        defaults));

    // Bools toggle in either direction
    TestFalse(TEXT("Combat toggles off"), StepOption(defaults, EDemoOption::NpcCombat, 1).bNpcCombat);
    TestFalse(TEXT("Stats toggle off with -1"), StepOption(defaults, EDemoOption::ShowStats, -1).bShowStats);
    TestTrue(TEXT("Toggling twice restores"),
        StepOption(StepOption(defaults, EDemoOption::NpcCombat, 1), EDemoOption::NpcCombat, 1).bNpcCombat);

    // Other options move by one descriptor step and clamp
    const double laneStep = NpcScaleDesc(EDemoOption::LaneFraction).Step;
    TestNearlyEqual(TEXT("Lane fraction up one step"), StepOption(defaults, EDemoOption::LaneFraction, 1).LaneFraction,
        0.4 + laneStep, NPCSCALE_EPS);
    TestNearlyEqual(TEXT("Lane fraction down one step (direction -3)"),
        StepOption(defaults, EDemoOption::LaneFraction, -3).LaneFraction, 0.4 - laneStep, NPCSCALE_EPS);
    const FDemoOptions fullLanes = SetOption(defaults, EDemoOption::LaneFraction, 1.0);
    TestEqual(TEXT("Lane fraction at max stays"), StepOption(fullLanes, EDemoOption::LaneFraction, 1).LaneFraction,
        1.0);
    const FDemoOptions noMeshes = SetOption(defaults, EDemoOption::MeshBudget, 0.0);
    TestEqual(TEXT("Mesh budget at 0 stays"), StepOption(noMeshes, EDemoOption::MeshBudget, -1).MeshBudget, 0);
    const double battleStep = NpcScaleDesc(EDemoOption::BattleShipCount).Step;
    TestEqual(TEXT("Battle ships up one step"), StepOption(defaults, EDemoOption::BattleShipCount, 1).BattleShipCount,
        static_cast<int32>(2000.0 + battleStep));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleParseCountTest, "SOLTest.NpcScaleMath.ParseNpcCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The count parser accepts plain numbers and k / M suffixes for whole counts up to 1M, and rejects everything else
bool FSOLNpcScaleParseCountTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    struct FCase
    {
        const TCHAR* Text;
        int32 Expected;
    };
    const FCase cases[] = {
        { TEXT("250000"), 250000 },
        { TEXT("250k"), 250000 },
        { TEXT("250K"), 250000 },
        { TEXT("1M"), 1000000 },
        { TEXT("1m"), 1000000 },
        { TEXT("1.5k"), 1500 },
        { TEXT("2.5K"), 2500 },
        { TEXT("0.5M"), 500000 },
        { TEXT(" 10k "), 10000 },
        { TEXT("\t100000\n"), 100000 },
        { TEXT("0"), 0 },
        { TEXT("0k"), 0 },
        { TEXT("1000000"), 1000000 },
        { TEXT("1"), 1 },
        { TEXT("1000001"), INDEX_NONE },
        { TEXT(""), INDEX_NONE },
        { TEXT("   "), INDEX_NONE },
        { TEXT("abc"), INDEX_NONE },
        { TEXT("-5"), INDEX_NONE },
        { TEXT("1.5"), INDEX_NONE },
        { TEXT("1.2345k"), INDEX_NONE },
        { TEXT("2M"), INDEX_NONE },
        { TEXT("1.1M"), INDEX_NONE },
        { TEXT("10kk"), INDEX_NONE },
        { TEXT("1e6"), INDEX_NONE },
        { TEXT("1,000"), INDEX_NONE },
        { TEXT("k"), INDEX_NONE },
        { TEXT("M"), INDEX_NONE },
        { TEXT("1..5k"), INDEX_NONE },
        { TEXT("12 34"), INDEX_NONE },
        { TEXT("10x"), INDEX_NONE },
        { TEXT("99999999999999999999"), INDEX_NONE },
    };
    for (const FCase& testCase : cases)
    {
        TestEqual(*FString::Printf(TEXT("Parse \"%s\""), testCase.Text), ParseNpcCount(FString(testCase.Text)),
            testCase.Expected);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleSelectNthTest, "SOLTest.NpcScaleMath.SelectNth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SelectNth returns the nth smallest value (as a sort would), clamps nth, and returns 0 for an empty array
bool FSOLNpcScaleSelectNthTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;
    TArray<double> empty;
    TestEqual(TEXT("Empty array gives 0"), SelectNth(empty, 3), 0.0);

    // Random arrays with many duplicates, every rank against a sorted copy
    FRandomStream random(1234);
    for (int32 trial = 0; trial < 20; ++trial)
    {
        TArray<double> values;
        const int32 count = 1 + random.RandHelper(200);
        for (int32 index = 0; index < count; ++index)
        {
            values.Add(static_cast<double>(random.RandHelper(40)) * 0.5);
        }
        TArray<double> sorted = values;
        sorted.Sort();
        for (int32 nth = 0; nth < count; nth += 7)
        {
            TArray<double> scratch = values;
            TestEqual(*FString::Printf(TEXT("Trial %d rank %d"), trial, nth), SelectNth(scratch, nth), sorted[nth]);
        }
        TArray<double> low = values;
        TArray<double> high = values;
        TestEqual(TEXT("Negative nth clamps to the minimum"), SelectNth(low, -5), sorted[0]);
        TestEqual(TEXT("nth past the end clamps to the maximum"), SelectNth(high, count + 10), sorted.Last());
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleFarChunkCullTest, "SOLTest.NpcScaleMath.FarChunkCull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Far chunk runs only in a candidate group, when its minimum wake key is reached, or (Evaluate only) when its group's
// minimums are stale; a stale group stays dirty until Evaluate visits it as a candidate
bool FSOLNpcScaleFarChunkCullTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;

    // Not a candidate: never, whatever the keys and flags
    TestFalse(TEXT("Not a candidate, key reached"), IsFarChunkActive(false, false, true, 0.0, 10.0));
    TestFalse(TEXT("Not a candidate, dirty"), IsFarChunkActive(false, true, true, 100.0, 10.0));

    // Candidate: the key decides, inclusive at equality
    TestTrue(TEXT("Key below the clock"), IsFarChunkActive(true, false, false, 5.0, 10.0));
    TestTrue(TEXT("Key equal to the clock"), IsFarChunkActive(true, false, false, 10.0, 10.0));
    TestFalse(TEXT("Key above the clock sleeps"), IsFarChunkActive(true, false, true, 10.5, 10.0));

    // Dirty: Evaluate visits a sleeping chunk, Select does not (Evaluate refreshed the minimum first)
    TestTrue(TEXT("Dirty group, Evaluate"), IsFarChunkActive(true, true, true, 1.0e9, 10.0));
    TestFalse(TEXT("Dirty group, Select"), IsFarChunkActive(true, true, false, 1.0e9, 10.0));

    // A new chunk (minimum 0) is always visited once
    TestTrue(TEXT("New chunk"), IsFarChunkActive(true, false, false, 0.0, 0.0));

    // Clearing: only the candidates (the groups Evaluate visited) lose their flag
    TArray<int32> dirty = { 1, 1, 0, 1 };
    const TArray<uint8> candidate = { 1, 0, 1, 0 };
    ClearVisitedWakeDirty(dirty, candidate);
    TestEqual(TEXT("Visited dirty group cleared"), dirty[0], 0);
    TestEqual(TEXT("Unvisited dirty group kept"), dirty[1], 1);
    TestEqual(TEXT("Visited clean group stays clean"), dirty[2], 0);
    TestEqual(TEXT("Unvisited dirty group kept (2)"), dirty[3], 1);

    // Mismatched lengths: only the common prefix is touched
    TArray<int32> longer = { 1, 1, 1 };
    const TArray<uint8> shorter = { 1 };
    ClearVisitedWakeDirty(longer, shorter);
    TestEqual(TEXT("Common prefix cleared"), longer[0], 0);
    TestEqual(TEXT("Beyond the candidates untouched"), longer[2], 1);

    // A spawn slice marks its group dirty while the group is far away: it stays dirty through frames where the group
    // is not a candidate, so the chunk it joined is visited once the group becomes one
    TArray<int32> spawnDirty = { 1 };
    const TArray<uint8> notCandidate = { 0 };
    const TArray<uint8> nowCandidate = { 1 };
    ClearVisitedWakeDirty(spawnDirty, notCandidate);
    TestTrue(TEXT("Far group keeps its dirty flag"), spawnDirty[0] == 1);
    TestTrue(TEXT("Stale chunk visited once the group is a candidate"),
        IsFarChunkActive(nowCandidate[0] != 0, spawnDirty[0] != 0, true, 1.0e12, 0.0));
    ClearVisitedWakeDirty(spawnDirty, nowCandidate);
    TestTrue(TEXT("Cleared after that visit"), spawnDirty[0] == 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcScaleLaneSpeedBoundTest, "SOLTest.NpcScaleMath.LaneSpeedBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The lane speed bound holds for a lane ship relative to endpoint A while the endpoints move apart, together and
// sideways (the lane far longer than at its epoch), and its degenerate inputs are safe
bool FSOLNpcScaleLaneSpeedBoundTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcScale;

    // Formula on simple inputs
    TestEqual(TEXT("Nominal length, still endpoints"), LaneShipSpeedBoundMps(1.0e5, 1.0e9, 1.0e9, 1.0e6, 0.0), 1.0e5);
    TestEqual(TEXT("Twice as long: twice as fast"), LaneShipSpeedBoundMps(1.0e5, 1.0e9, 2.0e9, 1.0e6, 0.0), 2.0e5);
    TestEqual(TEXT("Zero nominal length: unscaled"), LaneShipSpeedBoundMps(1.0e5, 0.0, 2.0e9, 0.0, 0.0), 1.0e5);
    TestTrue(TEXT("Negative inputs use magnitudes"),
        LaneShipSpeedBoundMps(-1.0e5, 1.0e9, 1.0e9, -5.0, -3.0e4) >= 1.3e5);

    // Numerical check: endpoints on straight paths, the ship on its rail (rate fixed at spawn from the nominal length)
    const double nominalLengthM = 1.0e9;
    const double laneSpeedMps = 1.5e5;
    const double laneRadiusM = 3.0e6;
    const FVector2d lateralM(2.0e6, -1.5e6);
    const FVector3d a0(0.0, 0.0, 0.0);
    const FVector3d b0(nominalLengthM, 0.0, 0.0);
    struct FCase
    {
        FVector3d VelocityA;
        FVector3d VelocityB;
    };
    const FCase cases[] = {
        { FVector3d(0.0, 3.0e4, 0.0), FVector3d(2.0e4, -1.0e4, 5.0e3) },     // Apart and sideways
        { FVector3d(-1.0e4, 0.0, 0.0), FVector3d(4.0e4, 0.0, 0.0) },         // Straight apart (lane 5x longer)
        { FVector3d(0.0, 0.0, 0.0), FVector3d(-4.0e3, 4.0e4, 0.0) },         // B swinging around A
    };
    const double rate = laneSpeedMps / nominalLengthM;
    const double stepS = 1.0;
    int32 checked = 0;
    for (int32 caseIndex = 0; caseIndex < UE_ARRAY_COUNT(cases); ++caseIndex)
    {
        const FCase& lane = cases[caseIndex];
        double worstRatio = 0.0;
        for (double t = 0.0; t <= 1.0e5; t += 997.0)
        {
            // Position relative to A at t and t + step
            const auto relative = [&](const double time)
            {
                const FVector3d pointA = a0 + lane.VelocityA * time;
                const FVector3d pointB = b0 + lane.VelocityB * time;
                return LanePositionM(pointA, pointB, TriangleWave01(0.3 + rate * time), lateralM) - pointA;
            };
            const double speedMps = (relative(t + stepS) - relative(t)).Size() / stepS;
            const double lengthM = ((b0 + lane.VelocityB * t) - (a0 + lane.VelocityA * t)).Size();
            const double boundMps = LaneShipSpeedBoundMps(laneSpeedMps, nominalLengthM, lengthM, laneRadiusM,
                (lane.VelocityB - lane.VelocityA).Size());
            worstRatio = FMath::Max(worstRatio, speedMps / boundMps);
            ++checked;
        }
        TestTrue(*FString::Printf(TEXT("Case %d: speed within the bound (worst %.3f)"), caseIndex, worstRatio),
            worstRatio <= 1.0 + 1.0e-6);
        TestTrue(*FString::Printf(TEXT("Case %d: the bound is not loose (worst %.3f)"), caseIndex, worstRatio),
            worstRatio > 0.5);
    }
    TestTrue(TEXT("Samples were checked"), checked > 100);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
