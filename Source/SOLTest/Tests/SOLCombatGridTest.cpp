/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatGrid.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Cell size (m) used by most cases
    constexpr double COMBATGRID_CELL_M = 100.0;

    // Cell-span threshold above which a query falls back to returning every id, per the contract
    constexpr int32 COMBATGRID_FALLBACK_CELLS = 4096;

    //////////////////////////////////////////////////////////////////////////
    // Returns how many times id appears in ids
    int32 CombatGridCount(const TArray<int32>& ids, const int32 id)
    {
        int32 count = 0;
        for (const int32 value : ids)
        {
            count += value == id ? 1 : 0;
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when no id appears more than once in ids
    bool CombatGridNoDuplicates(const TArray<int32>& ids)
    {
        for (const int32 value : ids)
        {
            if (CombatGridCount(ids, value) != 1)
            {
                return false;
            }
        }
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridInsertNumClearTest, "SOLTest.CombatGrid.InsertNumClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Num counts inserted ids, Clear empties the grid, and a query after Clear returns nothing
bool FSOLCombatGridInsertNumClearTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(COMBATGRID_CELL_M);
    TestEqual(TEXT("New grid is empty"), grid.Num(), 0);

    grid.Insert(1, FVector3d(0.0, 0.0, 0.0), 5.0);
    grid.Insert(2, FVector3d(1000.0, 0.0, 0.0), 5.0);
    grid.Insert(3, FVector3d(-1000.0, 50.0, 0.0), 5.0);
    TestEqual(TEXT("Three inserts give Num 3"), grid.Num(), 3);

    grid.Clear();
    TestEqual(TEXT("Clear gives Num 0"), grid.Num(), 0);

    TArray<int32> ids;
    grid.QueryCandidates(FVector3d(-50.0, 0.0, 0.0), FVector3d(50.0, 0.0, 0.0), 10.0, ids);
    TestEqual(TEXT("Query after Clear returns nothing"), ids.Num(), 0);

    // Grid is reusable after Clear
    grid.Insert(7, FVector3d(0.0, 0.0, 0.0), 5.0);
    grid.QueryCandidates(FVector3d(-50.0, 0.0, 0.0), FVector3d(50.0, 0.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Reinserted id found once"), CombatGridCount(ids, 7), 1);
    TestEqual(TEXT("Cleared id 1 not returned"), CombatGridCount(ids, 1), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridOutputClearedTest, "SOLTest.CombatGrid.OutputCleared",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// QueryCandidates clears outIds before filling it
bool FSOLCombatGridOutputClearedTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(COMBATGRID_CELL_M);
    grid.Insert(5, FVector3d(0.0, 0.0, 0.0), 5.0);

    TArray<int32> ids = { 99, 98, 97 };
    grid.QueryCandidates(FVector3d(-10.0, 0.0, 0.0), FVector3d(10.0, 0.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Stale id 99 removed"), CombatGridCount(ids, 99), 0);
    TestEqual(TEXT("Hit id 5 present"), CombatGridCount(ids, 5), 1);

    TArray<int32> empty = { 42 };
    grid.QueryCandidates(FVector3d(1.0e6, 0.0, 0.0), FVector3d(1.0e6 + 10.0, 0.0, 0.0), 0.0, empty);
    TestEqual(TEXT("Far query with pre-filled output returns empty"), empty.Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridFindsAlongSegmentTest, "SOLTest.CombatGrid.FindsAlongSegment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A segment crossing several cells finds every sphere it touches, each id exactly once
bool FSOLCombatGridFindsAlongSegmentTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(COMBATGRID_CELL_M);
    grid.Insert(10, FVector3d(50.0, 0.0, 0.0), 5.0);
    grid.Insert(11, FVector3d(250.0, 3.0, 0.0), 5.0);
    grid.Insert(12, FVector3d(450.0, 0.0, -4.0), 5.0);

    // Spheres straddling cell boundaries (centre in one cell, segment passes through the neighbour)
    grid.Insert(13, FVector3d(300.0, 104.0, 0.0), 10.0);
    grid.Insert(14, FVector3d(201.0, -199.0, 0.0), 8.0);

    TArray<int32> ids;
    grid.QueryCandidates(FVector3d(0.0, 95.0, 0.0), FVector3d(500.0, 95.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Sphere 13 (centre across a cell boundary, 9 m from segment, radius 10) found"),
        CombatGridCount(ids, 13), 1);

    grid.QueryCandidates(FVector3d(0.0, 0.0, 0.0), FVector3d(500.0, 0.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Sphere 10 found once"), CombatGridCount(ids, 10), 1);
    TestEqual(TEXT("Sphere 11 found once"), CombatGridCount(ids, 11), 1);
    TestEqual(TEXT("Sphere 12 found once"), CombatGridCount(ids, 12), 1);
    TestTrue(TEXT("No duplicates"), CombatGridNoDuplicates(ids));

    // Diagonal segment through cell corners
    grid.QueryCandidates(FVector3d(0.0, 0.0, 0.0), FVector3d(400.0, -400.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Diagonal segment finds sphere 14 near the (200,-200) cell corner"), CombatGridCount(ids, 14), 1);
    TestTrue(TEXT("Diagonal: no duplicates"), CombatGridNoDuplicates(ids));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridPaddingTest, "SOLTest.CombatGrid.Padding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Padding widens every sphere: a sphere reached only through radius + padding is still returned
bool FSOLCombatGridPaddingTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(COMBATGRID_CELL_M);

    // 150 m off the segment, radius 10: needs padding >= 140; the centre sits a cell and a half away
    grid.Insert(20, FVector3d(0.0, 150.0, 0.0), 10.0);

    TArray<int32> ids;
    grid.QueryCandidates(FVector3d(-30.0, 0.0, 0.0), FVector3d(30.0, 0.0, 0.0), 145.0, ids);
    TestEqual(TEXT("Padded sphere found"), CombatGridCount(ids, 20), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridLargeObjectTest, "SOLTest.CombatGrid.LargeObject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An object larger than a cell is found from any cell it covers, including far from its centre's cell
bool FSOLCombatGridLargeObjectTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(COMBATGRID_CELL_M);
    grid.Insert(30, FVector3d(0.0, 0.0, 0.0), 5000.0);

    TArray<int32> ids;
    // Short segment near the +Y rim, 49 cells from the centre's cell
    grid.QueryCandidates(FVector3d(-10.0, 4900.0, 0.0), FVector3d(10.0, 4900.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Found near +Y rim"), CombatGridCount(ids, 30), 1);

    // Point query near the -X,-Z rim
    grid.QueryCandidates(FVector3d(-3000.0, 0.0, -3000.0), FVector3d(-3000.0, 0.0, -3000.0), 0.0, ids);
    TestEqual(TEXT("Found from a point near the -X/-Z rim"), CombatGridCount(ids, 30), 1);

    // Segment passing entirely inside the sphere
    grid.QueryCandidates(FVector3d(1000.0, 1000.0, 1000.0), FVector3d(1020.0, 1000.0, 1000.0), 0.0, ids);
    TestEqual(TEXT("Found from a segment inside it"), CombatGridCount(ids, 30), 1);
    TestTrue(TEXT("No duplicates for a multi-cell object"), CombatGridNoDuplicates(ids));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridFarExcludedTest, "SOLTest.CombatGrid.FarExcluded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ids many cells away from a short segment are not returned (the grid actually prunes)
bool FSOLCombatGridFarExcludedTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(COMBATGRID_CELL_M);

    // 1000 targets on a line every 1000 m (10 cells apart)
    for (int32 i = 0; i < 1000; ++i)
    {
        grid.Insert(i, FVector3d(1000.0 * static_cast<double>(i), 0.0, 0.0), 5.0);
    }

    TArray<int32> ids;
    grid.QueryCandidates(FVector3d(499980.0, 0.0, 0.0), FVector3d(500005.0, 0.0, 0.0), 1.0, ids);
    TestEqual(TEXT("Target 500 found"), CombatGridCount(ids, 500), 1);

    bool bAnyFar = false;
    for (const int32 id : ids)
    {
        bAnyFar |= FMath::Abs(id - 500) >= 2;
    }
    TestFalse(TEXT("No target 20+ cells away is returned"), bAnyFar);

    // Off-axis far objects
    grid.Insert(5000, FVector3d(500000.0, 5000.0, 0.0), 5.0);
    grid.Insert(5001, FVector3d(500000.0, 0.0, -5000.0), 5.0);
    grid.QueryCandidates(FVector3d(499980.0, 0.0, 0.0), FVector3d(500005.0, 0.0, 0.0), 1.0, ids);
    TestEqual(TEXT("Object 50 cells away in +Y not returned"), CombatGridCount(ids, 5000), 0);
    TestEqual(TEXT("Object 50 cells away in -Z not returned"), CombatGridCount(ids, 5001), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridNegativeCoordsTest, "SOLTest.CombatGrid.NegativeCoords",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Negative coordinates hash with floor (not truncation toward zero), including spheres straddling the origin planes
bool FSOLCombatGridNegativeCoordsTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(COMBATGRID_CELL_M);
    grid.Insert(40, FVector3d(-150.0, -150.0, -150.0), 5.0);
    grid.Insert(41, FVector3d(-1.0, 0.0, 0.0), 0.5);
    grid.Insert(42, FVector3d(-50.0, -50.0, -50.0), 5.0);
    grid.Insert(43, FVector3d(-5000.0, -5000.0, -5000.0), 5.0);

    TArray<int32> ids;
    grid.QueryCandidates(FVector3d(-300.0, -150.0, -150.0), FVector3d(-120.0, -150.0, -150.0), 0.0, ids);
    TestEqual(TEXT("Sphere in cell (-2,-2,-2) found"), CombatGridCount(ids, 40), 1);
    TestEqual(TEXT("Sphere 50 cells away in negative space not returned"), CombatGridCount(ids, 43), 0);

    // Segment entirely on +X; the sphere's padded surface (radius 0.5 + padding 1) reaches x = 0.5
    grid.QueryCandidates(FVector3d(0.1, 0.0, 0.0), FVector3d(50.0, 0.0, 0.0), 1.0, ids);
    TestEqual(TEXT("Sphere across the x=0 plane found from +X"), CombatGridCount(ids, 41), 1);

    // Segment in the (-1,-1,-1) cell, near its corner
    grid.QueryCandidates(FVector3d(-60.0, -50.0, -50.0), FVector3d(-40.0, -50.0, -50.0), 0.0, ids);
    TestEqual(TEXT("Sphere in cell (-1,-1,-1) found"), CombatGridCount(ids, 42), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridHugeCoordsTest, "SOLTest.CombatGrid.HugeCoords",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Coordinates near 1e12 m (cell indices beyond int32) still hash correctly and prune far ids
bool FSOLCombatGridHugeCoordsTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(COMBATGRID_CELL_M);
    const FVector3d base(1.0e12, -1.0e12, 5.0e11);
    grid.Insert(50, base + FVector3d(12.5, 0.5, 0.0), 1.0);
    grid.Insert(51, base + FVector3d(1.0e6, 0.0, 0.0), 1.0);
    grid.Insert(52, -base, 1.0);

    // An object at the origin must not alias with 1e12 cells through int32 overflow/wrap
    grid.Insert(53, FVector3d(0.0, 0.0, 0.0), 1.0);

    TArray<int32> ids;
    grid.QueryCandidates(base, base + FVector3d(25.0, 0.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Target at 1e12 m found"), CombatGridCount(ids, 50), 1);
    TestEqual(TEXT("Target 1000 km further not returned"), CombatGridCount(ids, 51), 0);
    TestEqual(TEXT("Mirrored target at -1e12 m not returned"), CombatGridCount(ids, 52), 0);
    TestEqual(TEXT("Origin target not returned"), CombatGridCount(ids, 53), 0);

    grid.QueryCandidates(-base - FVector3d(10.0, 0.0, 0.0), -base + FVector3d(10.0, 0.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Target at -1e12 m found"), CombatGridCount(ids, 52), 1);
    TestEqual(TEXT("Target at +1e12 m not returned from -1e12 query"), CombatGridCount(ids, 50), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridLongSegmentTest, "SOLTest.CombatGrid.LongSegment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 100 km segment under the cap finds targets along it and prunes far ones; over the cap it returns every id
bool FSOLCombatGridLongSegmentTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d start(0.0, 0.0, 0.0);
    const FVector3d end(100000.0, 0.0, 0.0);

    // 1 km cells: 100 cells, under the cap, so the walk must still prune
    {
        SOLCombatGrid::FGrid grid(1000.0);
        grid.Insert(60, FVector3d(2000.0, 10.0, 0.0), 20.0);
        grid.Insert(61, FVector3d(99000.0, 0.0, -10.0), 20.0);
        grid.Insert(62, FVector3d(50000.0, 1.0e6, 0.0), 20.0);
        TArray<int32> ids;
        grid.QueryCandidates(start, end, 0.0, ids);
        TestEqual(TEXT("1 km cells: target near start found"), CombatGridCount(ids, 60), 1);
        TestEqual(TEXT("1 km cells: target near end found"), CombatGridCount(ids, 61), 1);
        TestEqual(TEXT("1 km cells: target 1000 cells off-axis not returned"), CombatGridCount(ids, 62), 0);
    }

    // 10 m cells: 10,000 cells exceeds the 4096-cell cap, so the query falls back to every id
    {
        const double cellM = 10.0;
        TestTrue(TEXT("Setup: segment spans more cells than the cap"),
            (end - start).Size() / cellM > static_cast<double>(COMBATGRID_FALLBACK_CELLS));
        SOLCombatGrid::FGrid grid(cellM);
        grid.Insert(70, FVector3d(2000.0, 1.0, 0.0), 2.0);
        grid.Insert(71, FVector3d(98000.0, 0.0, 1.0), 2.0);
        grid.Insert(72, FVector3d(50000.0, 1.0e6, 0.0), 2.0);
        TArray<int32> ids;
        grid.QueryCandidates(start, end, 0.0, ids);
        TestEqual(TEXT("Over cap: target near start found"), CombatGridCount(ids, 70), 1);
        TestEqual(TEXT("Over cap: target near end found"), CombatGridCount(ids, 71), 1);
        TestEqual(TEXT("Over cap: fallback returns every id, including the far one"), CombatGridCount(ids, 72), 1);
        TestEqual(TEXT("Over cap: exactly Num ids"), ids.Num(), grid.Num());
        TestTrue(TEXT("Over cap: no duplicates"), CombatGridNoDuplicates(ids));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridDeterminismTest, "SOLTest.CombatGrid.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Two grids built from the same inserts return the same candidate set for the same query
bool FSOLCombatGridDeterminismTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid first(COMBATGRID_CELL_M);
    SOLCombatGrid::FGrid second(COMBATGRID_CELL_M);
    for (int32 i = 0; i < 200; ++i)
    {
        const double fi = static_cast<double>(i);
        const FVector3d centre(37.0 * fi - 3000.0, 13.0 * fi - 1000.0, -7.0 * fi);
        first.Insert(i, centre, 3.0 + static_cast<double>(i % 5));
        second.Insert(i, centre, 3.0 + static_cast<double>(i % 5));
    }

    TArray<int32> a;
    TArray<int32> b;
    first.QueryCandidates(FVector3d(-500.0, -200.0, -300.0), FVector3d(800.0, 300.0, -600.0), 5.0, a);
    second.QueryCandidates(FVector3d(-500.0, -200.0, -300.0), FVector3d(800.0, 300.0, -600.0), 5.0, b);
    a.Sort();
    b.Sort();
    TestTrue(TEXT("Same candidate sets"), a == b);
    TestTrue(TEXT("No duplicates"), CombatGridNoDuplicates(a));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatGridFallbackKeepsBufferTest, "SOLTest.CombatGrid.FallbackKeepsBuffer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The over-cap fallback fills a caller's reserved output in place: same buffer, same capacity, stale contents gone
bool FSOLCombatGridFallbackKeepsBufferTest::RunTest(const FString& /*parameters*/)
{
    SOLCombatGrid::FGrid grid(10.0);
    grid.Insert(1, FVector3d(100.0, 0.0, 0.0), 1.0);
    grid.Insert(2, FVector3d(200.0, 0.0, 0.0), 1.0);
    grid.Insert(3, FVector3d(300.0, 0.0, 0.0), 1.0);

    // A worker's scratch: reserved well past the grid's size and holding a previous query's ids
    TArray<int32> ids;
    ids.Reserve(256);
    ids.Add(99);
    ids.Add(98);
    const int32* bufferBefore = ids.GetData();
    const int32 capacityBefore = ids.Max();

    // 10 m cells along 100 km: far over the cell cap, so the query falls back to every id
    grid.QueryCandidates(FVector3d::ZeroVector, FVector3d(100000.0, 0.0, 0.0), 0.0, ids);
    TestEqual(TEXT("Every id returned"), ids.Num(), 3);
    TestEqual(TEXT("Stale id 99 gone"), CombatGridCount(ids, 99), 0);
    TestTrue(TEXT("Same buffer (no reallocation)"), ids.GetData() == bufferBefore);
    TestEqual(TEXT("Capacity unchanged (no shrink-to-fit)"), ids.Max(), capacityBefore);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
