/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLAsteroidBelt.h"
#include "Universe/SOLKepler.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS


// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Documented approximate Kirkwood gap centers (3:1, 5:2, 2:1 Jupiter resonances), AU, ascending
    constexpr double BELT_GAP_CENTERS_AU[] = { 2.50, 2.82, 3.27 };
    constexpr int32 BELT_GAP_COUNT = 3;
    constexpr double BELT_GAP_CENTER_TOLERANCE_AU = 0.1;
    constexpr double BELT_EDGE_STEP_AU = 1e-9;                 // probe step just inside/outside a band edge

    // Loose bounds on the "~500 real asteroids" count (the header does not pin an exact number)
    constexpr int32 BELT_REAL_MIN_COUNT = 400;
    constexpr int32 BELT_REAL_MAX_COUNT = 600;

    // Representative family-fill parameters used by most GenerateFamilyFill cases
    constexpr int32 BELT_FILL_SEED = 42;
    constexpr int32 BELT_FILL_CLUSTERS = 20;
    constexpr int32 BELT_FILL_MIN_MEMBERS = 10;
    constexpr int32 BELT_FILL_MAX_MEMBERS = 50;
    constexpr double BELT_FILL_MIN_RADIUS_M = 20000.0;
    constexpr double BELT_FILL_MAX_RADIUS_M = 100000.0;

    // Seeds far enough apart that two generations matching by coincidence is not a realistic concern
    constexpr int32 BELT_SEED_A = 1;
    constexpr int32 BELT_SEED_B = 999999;

    // Times (Julian centuries since J2000) at which cluster cohesion is checked: J2000 and +/- 100 years
    constexpr double BELT_COHESION_CENTURIES[] = { 0.0, 1.0, -1.0 };

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two secular element sets match field for field exactly
    bool BeltElementsIdentical(const FSOLSecularElements& a, const FSOLSecularElements& b)
    {
        return a.A0AU == b.A0AU && a.ADotAUPerCy == b.ADotAUPerCy && a.E0 == b.E0 && a.EDotPerCy == b.EDotPerCy
            && a.I0Deg == b.I0Deg && a.IDotDegPerCy == b.IDotDegPerCy && a.L0Deg == b.L0Deg
            && a.LDotDegPerCy == b.LDotDegPerCy && a.LongPeri0Deg == b.LongPeri0Deg
            && a.LongPeriDotDegPerCy == b.LongPeriDotDegPerCy && a.LongNode0Deg == b.LongNode0Deg
            && a.LongNodeDotDegPerCy == b.LongNodeDotDegPerCy;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two asteroid definitions match field for field exactly
    bool BeltDefsIdentical(const FSOLAsteroidDef& a, const FSOLAsteroidDef& b)
    {
        return a.Name == b.Name && a.RadiusM == b.RadiusM && a.FamilyId == b.FamilyId
            && BeltElementsIdentical(a.Elements, b.Elements);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two asteroid lists have the same length and identical entries in the same order
    bool BeltDefListsIdentical(const TArray<FSOLAsteroidDef>& a, const TArray<FSOLAsteroidDef>& b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (int32 index = 0; index < a.Num(); ++index)
        {
            if (!BeltDefsIdentical(a[index], b[index]))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two element lists have the same length and identical entries in the same order
    bool BeltElementListsIdentical(const TArray<FSOLSecularElements>& a, const TArray<FSOLSecularElements>& b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (int32 index = 0; index < a.Num(); ++index)
        {
            if (!BeltElementsIdentical(a[index], b[index]))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every rate term of a secular element set is exactly 0
    bool BeltRatesAllZero(const FSOLSecularElements& elements)
    {
        return elements.ADotAUPerCy == 0.0 && elements.EDotPerCy == 0.0 && elements.IDotDegPerCy == 0.0
            && elements.LongPeriDotDegPerCy == 0.0 && elements.LongNodeDotDegPerCy == 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Sun-relative position (m) of a body with the given elements at a time in centuries since J2000
    FVector3d BeltPositionM(const FSOLSecularElements& elements, const double centuries)
    {
        return SOLKepler::ElementsToState(elements.AtCenturies(centuries), SOLTestHelpers::SUN_GM, 0.0).PositionM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks every member of a family fill stays within 2x maxRadiusM of its cluster center at J2000 and +/- 1 century
    void BeltCheckCohesion(FAutomationTestBase& test, const TArray<FSOLAsteroidDef>& members,
        const TArray<FSOLSecularElements>& centers, const double maxRadiusM, const TCHAR* label)
    {
        const double bound = 2.0 * maxRadiusM;
        int32 failures = 0;
        double worstDistanceM = 0.0;
        for (const FSOLAsteroidDef& member : members)
        {
            if (!centers.IsValidIndex(member.FamilyId))
            {
                // FamilyId range is reported by its own test; skip here so the lookup cannot crash
                ++failures;
                continue;
            }
            const FSOLSecularElements& center = centers[member.FamilyId];
            for (const double centuries : BELT_COHESION_CENTURIES)
            {
                const double distanceM = (BeltPositionM(member.Elements, centuries)
                    - BeltPositionM(center, centuries)).Size();
                worstDistanceM = FMath::Max(worstDistanceM, distanceM);
                if (!(distanceM <= bound))
                {
                    ++failures;
                }
            }
        }
        test.TestEqual(*FString::Printf(TEXT("%s: member/center pairs beyond 2x max cluster radius (worst %.1f m, "
            "bound %.1f m)"), label, worstDistanceM, bound), failures, 0);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltConstantsTest, "SOLTest.AsteroidBelt.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The belt range and default generation constants match the values the header documents
bool FSOLAsteroidBeltConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("BELT_MIN_SEMI_MAJOR_AXIS_AU"), SOLAsteroidBelt::BELT_MIN_SEMI_MAJOR_AXIS_AU, 2.1);
    TestEqual(TEXT("BELT_MAX_SEMI_MAJOR_AXIS_AU"), SOLAsteroidBelt::BELT_MAX_SEMI_MAJOR_AXIS_AU, 3.3);
    TestEqual(TEXT("BELT_MAX_ECCENTRICITY"), SOLAsteroidBelt::BELT_MAX_ECCENTRICITY, 0.3);
    TestEqual(TEXT("BELT_MAX_INCLINATION_DEG"), SOLAsteroidBelt::BELT_MAX_INCLINATION_DEG, 25.0);
    TestTrue(TEXT("DEFAULT_CLUSTER_COUNT within ~200-400"), SOLAsteroidBelt::DEFAULT_CLUSTER_COUNT >= 200
        && SOLAsteroidBelt::DEFAULT_CLUSTER_COUNT <= 400);
    TestEqual(TEXT("DEFAULT_MIN_MEMBERS_PER_CLUSTER"), SOLAsteroidBelt::DEFAULT_MIN_MEMBERS_PER_CLUSTER, 10);
    TestEqual(TEXT("DEFAULT_MAX_MEMBERS_PER_CLUSTER"), SOLAsteroidBelt::DEFAULT_MAX_MEMBERS_PER_CLUSTER, 50);
    TestEqual(TEXT("DEFAULT_MIN_CLUSTER_RADIUS_M"), SOLAsteroidBelt::DEFAULT_MIN_CLUSTER_RADIUS_M, 20000.0);
    TestEqual(TEXT("DEFAULT_MAX_CLUSTER_RADIUS_M"), SOLAsteroidBelt::DEFAULT_MAX_CLUSTER_RADIUS_M, 100000.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltRealAsteroidsTest, "SOLTest.AsteroidBelt.RealAsteroids",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RealAsteroids returns ~500 distinct named bodies with FamilyId INDEX_NONE, positive radius and zero rate terms
bool FSOLAsteroidBeltRealAsteroidsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAsteroidDef> real = SOLAsteroidBelt::RealAsteroids();
    TestTrue(*FString::Printf(TEXT("RealAsteroids count %d within [%d, %d]"), real.Num(), BELT_REAL_MIN_COUNT,
        BELT_REAL_MAX_COUNT), real.Num() >= BELT_REAL_MIN_COUNT && real.Num() <= BELT_REAL_MAX_COUNT);

    TSet<FName> seenNames;
    seenNames.Reserve(real.Num());
    for (int32 index = 0; index < real.Num(); ++index)
    {
        const FSOLAsteroidDef& asteroid = real[index];
        const FString label = FString::Printf(TEXT("Real asteroid %d (%s)"), index, *asteroid.Name.ToString());
        TestFalse(*(label + TEXT(": Name is non-empty")), asteroid.Name.IsNone());
        TestEqual(*(label + TEXT(": FamilyId")), asteroid.FamilyId, static_cast<int32>(INDEX_NONE));
        TestTrue(*(label + TEXT(": RadiusM > 0")), asteroid.RadiusM > 0.0);
        TestTrue(*(label + TEXT(": every rate term is 0")), BeltRatesAllZero(asteroid.Elements));

        // A bound heliocentric orbit is required for ElementsToState (0 <= e < 1, a > 0)
        TestTrue(*(label + TEXT(": a > 0")), asteroid.Elements.A0AU > 0.0);
        TestTrue(*(label + TEXT(": 0 <= e < 1")), asteroid.Elements.E0 >= 0.0 && asteroid.Elements.E0 < 1.0);

        bool bAlreadySeen = false;
        seenNames.Add(asteroid.Name, &bAlreadySeen);
        TestFalse(*(label + TEXT(": Name is unique")), bAlreadySeen);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltRealDeterministicTest, "SOLTest.AsteroidBelt.RealAsteroidsDeterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RealAsteroids returns the identical list in the identical order on every call
bool FSOLAsteroidBeltRealDeterministicTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAsteroidDef> first = SOLAsteroidBelt::RealAsteroids();
    const TArray<FSOLAsteroidDef> second = SOLAsteroidBelt::RealAsteroids();
    TestTrue(TEXT("RealAsteroids is non-empty"), first.Num() > 0);
    TestTrue(TEXT("Two RealAsteroids calls are identical"), BeltDefListsIdentical(first, second));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltGapBandsTest, "SOLTest.AsteroidBelt.KirkwoodGapBands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// KirkwoodGapBandsAU returns 3 non-empty, ascending, non-overlapping bands centered near 2.50, 2.82 and 3.27 AU
bool FSOLAsteroidBeltGapBandsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAxisBandAU> bands = SOLAsteroidBelt::KirkwoodGapBandsAU();
    if (!TestEqual(TEXT("Kirkwood band count"), bands.Num(), BELT_GAP_COUNT))
    {
        return false;
    }
    for (int32 index = 0; index < BELT_GAP_COUNT; ++index)
    {
        const FSOLAxisBandAU& band = bands[index];
        TestTrue(*FString::Printf(TEXT("Band %d: LowAU (%f) < HighAU (%f)"), index, band.LowAU, band.HighAU),
            band.LowAU < band.HighAU);
        const double centerAU = 0.5 * (band.LowAU + band.HighAU);
        TestTrue(*FString::Printf(TEXT("Band %d center %f within %.2f AU of %.2f"), index, centerAU,
            BELT_GAP_CENTER_TOLERANCE_AU, BELT_GAP_CENTERS_AU[index]),
            FMath::Abs(centerAU - BELT_GAP_CENTERS_AU[index]) <= BELT_GAP_CENTER_TOLERANCE_AU);
        if (index > 0)
        {
            TestTrue(*FString::Printf(TEXT("Band %d starts at or after band %d ends (ascending, disjoint)"), index,
                index - 1), band.LowAU >= bands[index - 1].HighAU);
        }
    }

    // Same 3 bands on a second call
    const TArray<FSOLAxisBandAU> again = SOLAsteroidBelt::KirkwoodGapBandsAU();
    bool bSame = again.Num() == bands.Num();
    for (int32 index = 0; bSame && index < bands.Num(); ++index)
    {
        bSame = again[index].LowAU == bands[index].LowAU && again[index].HighAU == bands[index].HighAU;
    }
    TestTrue(TEXT("Two KirkwoodGapBandsAU calls are identical"), bSame);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltInGapTest, "SOLTest.AsteroidBelt.IsInKirkwoodGap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// IsInKirkwoodGap is true at each gap center, false between and outside the gaps, LowAU inclusive, HighAU exclusive
bool FSOLAsteroidBeltInGapTest::RunTest(const FString& /*parameters*/)
{
    for (const double centerAU : BELT_GAP_CENTERS_AU)
    {
        TestTrue(*FString::Printf(TEXT("IsInKirkwoodGap(%.2f) at a documented gap center"), centerAU),
            SOLAsteroidBelt::IsInKirkwoodGap(centerAU));
    }

    const double outsideAU[] = { 1.5, 2.65, 3.0, 4.0 };
    for (const double valueAU : outsideAU)
    {
        TestFalse(*FString::Printf(TEXT("IsInKirkwoodGap(%.2f) outside every gap"), valueAU),
            SOLAsteroidBelt::IsInKirkwoodGap(valueAU));
    }

    // Edge behavior against the bands the implementation actually reports
    const TArray<FSOLAxisBandAU> bands = SOLAsteroidBelt::KirkwoodGapBandsAU();
    TestEqual(TEXT("Kirkwood band count"), bands.Num(), BELT_GAP_COUNT);
    for (int32 index = 0; index < bands.Num(); ++index)
    {
        const FSOLAxisBandAU& band = bands[index];
        TestTrue(*FString::Printf(TEXT("Band %d: LowAU %f is inclusive"), index, band.LowAU),
            SOLAsteroidBelt::IsInKirkwoodGap(band.LowAU));
        TestFalse(*FString::Printf(TEXT("Band %d: HighAU %f is exclusive"), index, band.HighAU),
            SOLAsteroidBelt::IsInKirkwoodGap(band.HighAU));
        TestFalse(*FString::Printf(TEXT("Band %d: just below LowAU is outside"), index),
            SOLAsteroidBelt::IsInKirkwoodGap(band.LowAU - BELT_EDGE_STEP_AU));
        TestTrue(*FString::Printf(TEXT("Band %d: just below HighAU is inside"), index),
            SOLAsteroidBelt::IsInKirkwoodGap(band.HighAU - BELT_EDGE_STEP_AU));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltCentersBoundsTest, "SOLTest.AsteroidBelt.ClusterCentersBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ClusterCenters returns exactly clusterCount centers, each in the belt range, outside every gap, with zero rates
bool FSOLAsteroidBeltCentersBoundsTest::RunTest(const FString& /*parameters*/)
{
    const int32 counts[] = { 0, 1, 10, 300 };
    for (const int32 clusterCount : counts)
    {
        const TArray<FSOLSecularElements> centers = SOLAsteroidBelt::ClusterCenters(BELT_FILL_SEED, clusterCount);
        TestEqual(*FString::Printf(TEXT("ClusterCenters(%d, %d) count"), BELT_FILL_SEED, clusterCount),
            centers.Num(), clusterCount);
        for (int32 index = 0; index < centers.Num(); ++index)
        {
            const FSOLSecularElements& center = centers[index];
            const FString label = FString::Printf(TEXT("Count %d, center %d"), clusterCount, index);
            TestTrue(*FString::Printf(TEXT("%s: a %f in [min, max)"), *label, center.A0AU),
                center.A0AU >= SOLAsteroidBelt::BELT_MIN_SEMI_MAJOR_AXIS_AU
                && center.A0AU < SOLAsteroidBelt::BELT_MAX_SEMI_MAJOR_AXIS_AU);
            TestFalse(*FString::Printf(TEXT("%s: a %f not in a Kirkwood gap"), *label, center.A0AU),
                SOLAsteroidBelt::IsInKirkwoodGap(center.A0AU));
            TestTrue(*FString::Printf(TEXT("%s: e %f in [0, max]"), *label, center.E0),
                center.E0 >= 0.0 && center.E0 <= SOLAsteroidBelt::BELT_MAX_ECCENTRICITY);
            TestTrue(*FString::Printf(TEXT("%s: i %f deg in [0, max]"), *label, center.I0Deg),
                center.I0Deg >= 0.0 && center.I0Deg <= SOLAsteroidBelt::BELT_MAX_INCLINATION_DEG);
            TestTrue(*(label + TEXT(": every rate term is 0")), BeltRatesAllZero(center));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltCentersSeedTest, "SOLTest.AsteroidBelt.ClusterCentersSeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ClusterCenters is deterministic for a given seed/count, and a very different seed gives a different list
bool FSOLAsteroidBeltCentersSeedTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 clusterCount = 50;
    const TArray<FSOLSecularElements> first = SOLAsteroidBelt::ClusterCenters(BELT_SEED_A, clusterCount);
    const TArray<FSOLSecularElements> second = SOLAsteroidBelt::ClusterCenters(BELT_SEED_A, clusterCount);
    const TArray<FSOLSecularElements> other = SOLAsteroidBelt::ClusterCenters(BELT_SEED_B, clusterCount);
    TestEqual(TEXT("ClusterCenters count"), first.Num(), clusterCount);
    TestTrue(TEXT("Same seed and count give identical centers"), BeltElementListsIdentical(first, second));
    TestEqual(TEXT("Other-seed ClusterCenters count"), other.Num(), clusterCount);
    TestFalse(TEXT("A different seed gives different centers"), BeltElementListsIdentical(first, other));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltFillShapeTest, "SOLTest.AsteroidBelt.FamilyFillShape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// GenerateFamilyFill returns unnamed members grouped by ascending FamilyId, each cluster sized within [min, max]
bool FSOLAsteroidBeltFillShapeTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAsteroidDef> fill = SOLAsteroidBelt::GenerateFamilyFill(BELT_FILL_SEED, BELT_FILL_CLUSTERS,
        BELT_FILL_MIN_MEMBERS, BELT_FILL_MAX_MEMBERS, BELT_FILL_MIN_RADIUS_M, BELT_FILL_MAX_RADIUS_M);
    TestTrue(*FString::Printf(TEXT("Fill count %d within [%d, %d]"), fill.Num(),
        BELT_FILL_CLUSTERS * BELT_FILL_MIN_MEMBERS, BELT_FILL_CLUSTERS * BELT_FILL_MAX_MEMBERS),
        fill.Num() >= BELT_FILL_CLUSTERS * BELT_FILL_MIN_MEMBERS
        && fill.Num() <= BELT_FILL_CLUSTERS * BELT_FILL_MAX_MEMBERS);

    // Per-member checks, tallying each cluster's size along the way
    TArray<int32> membersPerCluster;
    membersPerCluster.Init(0, BELT_FILL_CLUSTERS);
    int32 previousFamilyId = 0;
    for (int32 index = 0; index < fill.Num(); ++index)
    {
        const FSOLAsteroidDef& member = fill[index];
        const FString label = FString::Printf(TEXT("Fill member %d"), index);
        TestTrue(*(label + TEXT(": Name is NAME_None")), member.Name.IsNone());
        TestTrue(*FString::Printf(TEXT("%s: FamilyId %d in [0, %d)"), *label, member.FamilyId, BELT_FILL_CLUSTERS),
            member.FamilyId >= 0 && member.FamilyId < BELT_FILL_CLUSTERS);
        TestTrue(*(label + TEXT(": RadiusM > 0")), member.RadiusM > 0.0);
        TestTrue(*FString::Printf(TEXT("%s: FamilyId %d not below previous %d (grouped ascending)"), *label,
            member.FamilyId, previousFamilyId), member.FamilyId >= previousFamilyId);
        previousFamilyId = member.FamilyId;
        if (membersPerCluster.IsValidIndex(member.FamilyId))
        {
            ++membersPerCluster[member.FamilyId];
        }
    }
    for (int32 cluster = 0; cluster < BELT_FILL_CLUSTERS; ++cluster)
    {
        TestTrue(*FString::Printf(TEXT("Cluster %d member count %d within [%d, %d]"), cluster,
            membersPerCluster[cluster], BELT_FILL_MIN_MEMBERS, BELT_FILL_MAX_MEMBERS),
            membersPerCluster[cluster] >= BELT_FILL_MIN_MEMBERS && membersPerCluster[cluster] <= BELT_FILL_MAX_MEMBERS);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltFillSeedTest, "SOLTest.AsteroidBelt.FamilyFillSeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// GenerateFamilyFill is deterministic for identical inputs, and a very different seed gives a different fill
bool FSOLAsteroidBeltFillSeedTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAsteroidDef> first = SOLAsteroidBelt::GenerateFamilyFill(BELT_SEED_A, BELT_FILL_CLUSTERS,
        BELT_FILL_MIN_MEMBERS, BELT_FILL_MAX_MEMBERS, BELT_FILL_MIN_RADIUS_M, BELT_FILL_MAX_RADIUS_M);
    const TArray<FSOLAsteroidDef> second = SOLAsteroidBelt::GenerateFamilyFill(BELT_SEED_A, BELT_FILL_CLUSTERS,
        BELT_FILL_MIN_MEMBERS, BELT_FILL_MAX_MEMBERS, BELT_FILL_MIN_RADIUS_M, BELT_FILL_MAX_RADIUS_M);
    const TArray<FSOLAsteroidDef> other = SOLAsteroidBelt::GenerateFamilyFill(BELT_SEED_B, BELT_FILL_CLUSTERS,
        BELT_FILL_MIN_MEMBERS, BELT_FILL_MAX_MEMBERS, BELT_FILL_MIN_RADIUS_M, BELT_FILL_MAX_RADIUS_M);
    TestTrue(TEXT("Fill is non-empty"), first.Num() > 0);
    TestTrue(TEXT("Identical inputs give an identical fill"), BeltDefListsIdentical(first, second));
    TestTrue(TEXT("Other-seed fill is non-empty"), other.Num() > 0);
    TestFalse(TEXT("A different seed gives a different fill"), BeltDefListsIdentical(first, other));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltFillCohesionTest, "SOLTest.AsteroidBelt.FamilyFillCohesion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every fill member stays within 2x the max cluster radius of its ClusterCenters center at J2000 and +/- 100 years
bool FSOLAsteroidBeltFillCohesionTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAsteroidDef> fill = SOLAsteroidBelt::GenerateFamilyFill(BELT_FILL_SEED, BELT_FILL_CLUSTERS,
        BELT_FILL_MIN_MEMBERS, BELT_FILL_MAX_MEMBERS, BELT_FILL_MIN_RADIUS_M, BELT_FILL_MAX_RADIUS_M);
    const TArray<FSOLSecularElements> centers = SOLAsteroidBelt::ClusterCenters(BELT_FILL_SEED, BELT_FILL_CLUSTERS);
    TestEqual(TEXT("ClusterCenters count"), centers.Num(), BELT_FILL_CLUSTERS);
    TestTrue(TEXT("Fill is non-empty"), fill.Num() > 0);

    // Every cluster must contribute at least one member so cohesion is exercised across all clusters
    TSet<int32> clustersSeen;
    for (const FSOLAsteroidDef& member : fill)
    {
        clustersSeen.Add(member.FamilyId);
    }
    TestEqual(TEXT("Clusters represented in the fill"), clustersSeen.Num(), BELT_FILL_CLUSTERS);

    BeltCheckCohesion(*this, fill, centers, BELT_FILL_MAX_RADIUS_M, TEXT("Default-like fill"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltFillCentersMatchTest, "SOLTest.AsteroidBelt.FamilyFillCentersIndependentOfScatter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Members of a fill with tight scatter parameters cohere with ClusterCenters(seed, count), so centers ignore scatter
bool FSOLAsteroidBeltFillCentersMatchTest::RunTest(const FString& /*parameters*/)
{
    // Very different member counts and radii from the default-like case; centers must still be ClusterCenters'
    constexpr double tightRadiusM = 5000.0;
    const TArray<FSOLAsteroidDef> fill = SOLAsteroidBelt::GenerateFamilyFill(BELT_FILL_SEED, BELT_FILL_CLUSTERS, 3, 3,
        tightRadiusM, tightRadiusM);
    const TArray<FSOLSecularElements> centers = SOLAsteroidBelt::ClusterCenters(BELT_FILL_SEED, BELT_FILL_CLUSTERS);
    TestEqual(TEXT("Fill count"), fill.Num(), BELT_FILL_CLUSTERS * 3);
    BeltCheckCohesion(*this, fill, centers, tightRadiusM, TEXT("Tight-scatter fill"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltFillZeroClustersTest, "SOLTest.AsteroidBelt.FamilyFillZeroClusters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// GenerateFamilyFill with clusterCount 0 returns an empty list
bool FSOLAsteroidBeltFillZeroClustersTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAsteroidDef> fill = SOLAsteroidBelt::GenerateFamilyFill(BELT_FILL_SEED, 0, BELT_FILL_MIN_MEMBERS,
        BELT_FILL_MAX_MEMBERS, BELT_FILL_MIN_RADIUS_M, BELT_FILL_MAX_RADIUS_M);
    TestEqual(TEXT("Zero-cluster fill count"), fill.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltFillFixedCountTest, "SOLTest.AsteroidBelt.FamilyFillFixedMemberCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// minMembersPerCluster == maxMembersPerCluster gives every cluster exactly that many members
bool FSOLAsteroidBeltFillFixedCountTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 fixedMembers = 15;
    const TArray<FSOLAsteroidDef> fill = SOLAsteroidBelt::GenerateFamilyFill(BELT_FILL_SEED, BELT_FILL_CLUSTERS,
        fixedMembers, fixedMembers, BELT_FILL_MIN_RADIUS_M, BELT_FILL_MAX_RADIUS_M);
    TestEqual(TEXT("Fixed-count fill total"), fill.Num(), BELT_FILL_CLUSTERS * fixedMembers);

    TArray<int32> membersPerCluster;
    membersPerCluster.Init(0, BELT_FILL_CLUSTERS);
    for (const FSOLAsteroidDef& member : fill)
    {
        if (membersPerCluster.IsValidIndex(member.FamilyId))
        {
            ++membersPerCluster[member.FamilyId];
        }
    }
    for (int32 cluster = 0; cluster < BELT_FILL_CLUSTERS; ++cluster)
    {
        TestEqual(*FString::Printf(TEXT("Cluster %d member count"), cluster), membersPerCluster[cluster],
            fixedMembers);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltFillFixedRadiusTest, "SOLTest.AsteroidBelt.FamilyFillFixedRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// minClusterRadiusM == maxClusterRadiusM is a valid call that still produces a cohesive, correctly sized fill
bool FSOLAsteroidBeltFillFixedRadiusTest::RunTest(const FString& /*parameters*/)
{
    constexpr double fixedRadiusM = 50000.0;
    const TArray<FSOLAsteroidDef> fill = SOLAsteroidBelt::GenerateFamilyFill(BELT_FILL_SEED, BELT_FILL_CLUSTERS,
        BELT_FILL_MIN_MEMBERS, BELT_FILL_MAX_MEMBERS, fixedRadiusM, fixedRadiusM);
    TestTrue(*FString::Printf(TEXT("Fixed-radius fill count %d within [%d, %d]"), fill.Num(),
        BELT_FILL_CLUSTERS * BELT_FILL_MIN_MEMBERS, BELT_FILL_CLUSTERS * BELT_FILL_MAX_MEMBERS),
        fill.Num() >= BELT_FILL_CLUSTERS * BELT_FILL_MIN_MEMBERS
        && fill.Num() <= BELT_FILL_CLUSTERS * BELT_FILL_MAX_MEMBERS);
    const TArray<FSOLSecularElements> centers = SOLAsteroidBelt::ClusterCenters(BELT_FILL_SEED, BELT_FILL_CLUSTERS);
    BeltCheckCohesion(*this, fill, centers, fixedRadiusM, TEXT("Fixed-radius fill"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAsteroidBeltGenerateBeltTest, "SOLTest.AsteroidBelt.GenerateBelt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// GenerateBelt is RealAsteroids followed by the default-configured family fill, and is deterministic per seed
bool FSOLAsteroidBeltGenerateBeltTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAsteroidDef> real = SOLAsteroidBelt::RealAsteroids();
    const TArray<FSOLAsteroidDef> fill = SOLAsteroidBelt::GenerateFamilyFill(BELT_FILL_SEED,
        SOLAsteroidBelt::DEFAULT_CLUSTER_COUNT, SOLAsteroidBelt::DEFAULT_MIN_MEMBERS_PER_CLUSTER,
        SOLAsteroidBelt::DEFAULT_MAX_MEMBERS_PER_CLUSTER, SOLAsteroidBelt::DEFAULT_MIN_CLUSTER_RADIUS_M,
        SOLAsteroidBelt::DEFAULT_MAX_CLUSTER_RADIUS_M);
    const TArray<FSOLAsteroidDef> belt = SOLAsteroidBelt::GenerateBelt(BELT_FILL_SEED);

    TestTrue(TEXT("Real asteroids are non-empty"), real.Num() > 0);
    TestTrue(TEXT("Default fill is non-empty"), fill.Num() > 0);
    if (!TestEqual(TEXT("GenerateBelt count = real + default fill"), belt.Num(), real.Num() + fill.Num()))
    {
        return false;
    }

    // The leading entries are RealAsteroids() in order; the remainder is the default fill in order
    bool bRealPrefixMatches = true;
    for (int32 index = 0; index < real.Num(); ++index)
    {
        bRealPrefixMatches &= BeltDefsIdentical(belt[index], real[index]);
    }
    TestTrue(TEXT("GenerateBelt starts with RealAsteroids() in order"), bRealPrefixMatches);
    bool bFillSuffixMatches = true;
    for (int32 index = 0; index < fill.Num(); ++index)
    {
        bFillSuffixMatches &= BeltDefsIdentical(belt[real.Num() + index], fill[index]);
    }
    TestTrue(TEXT("GenerateBelt continues with the default-configured fill in order"), bFillSuffixMatches);

    const TArray<FSOLAsteroidDef> again = SOLAsteroidBelt::GenerateBelt(BELT_FILL_SEED);
    TestTrue(TEXT("Two GenerateBelt calls with the same seed are identical"), BeltDefListsIdentical(belt, again));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
