/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #53 lane network math (SOLLaneNetwork, part 53a), written test-first against the contract
// Docs/Plans/53-lane-network-contract.md (cases 1-15 with 1b PartnerHysteresis and 1c IslandLinks; case 6 is split into
// EarlyStop and FinishTrip) before the production code existed. Positions are in units of 1e11 m on the x axis unless a
// case says otherwise; all nodes are planets unless stated. Every index into an output array is guarded by its count,
// so a wrong-sized output fails a check instead of stopping the run.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double LANENET_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double LANENET_INF = std::numeric_limits<double>::infinity();
    constexpr double LANENET_UNIT_M = 1.0e11;            // The contract's position unit
    constexpr double LANENET_TIGHT = 1.0e-12;            // Relative tolerance of exact formulas
    constexpr double LANENET_LOOSE = 1.0e-6;             // Relative tolerance of the iterative or rounded values
    constexpr double LANENET_DAY_S = 86400.0;
    constexpr double LANENET_SUN_ZONE_M = 1.3914e10;     // 20 Sun radii, passed as the literal (case 15)
    constexpr double LANENET_SUN_EDGE_M = 1.53054e10;    // 22 Sun radii, passed as the literal (case 15)

    //////////////////////////////////////////////////////////////////////////
    // Checks actual against expected within a relative tolerance (absolute when expected is 0); NaN never matches
    bool LaneNetNear(FAutomationTestBase& test, const FString& what, const double actual, const double expected,
        const double relTol)
    {
        const double tol = (expected == 0.0) ? relTol : relTol * FMath::Abs(expected);
        if (FMath::Abs(actual - expected) <= tol)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected %.15g, got %.15g"), *what, expected, actual));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks actual == expected exactly (for infinities and exact sample times)
    bool LaneNetExact(FAutomationTestBase& test, const FString& what, const double actual, const double expected)
    {
        if (actual == expected)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected exactly %.17g, got %.17g"), *what, expected, actual));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that a value is NaN
    bool LaneNetIsNaN(FAutomationTestBase& test, const FString& what, const double actual)
    {
        if (FMath::IsNaN(actual))
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected NaN, got %.17g"), *what, actual));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a position on the x axis in contract units (1e11 m)
    FVector3d LaneNetX(const double x)
    {
        return FVector3d(x * LANENET_UNIT_M, 0.0, 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positions on the x axis in contract units
    TArray<FVector3d> LaneNetXs(std::initializer_list<double> xs)
    {
        TArray<FVector3d> out;
        for (const double x : xs)
        {
            out.Add(LaneNetX(x));
        }
        return out;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a node of the given kind
    FSOLLaneNode LaneNetNode(const ESOLLaneNodeKind kind)
    {
        FSOLLaneNode node;
        node.Kind = kind;
        return node;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns count planet nodes
    TArray<FSOLLaneNode> LaneNetPlanets(const int32 count)
    {
        TArray<FSOLLaneNode> out;
        for (int32 index = 0; index < count; ++index)
        {
            out.Add(LaneNetNode(ESOLLaneNodeKind::Planet));
        }
        return out;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane pair
    FSOLLanePair LaneNetPair(const int32 a, const int32 b)
    {
        FSOLLanePair pair;
        pair.NodeA = a;
        pair.NodeB = b;
        return pair;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a pair list as text for messages
    FString LaneNetPairsText(TConstArrayView<FSOLLanePair> pairs)
    {
        FString text = TEXT("{");
        for (const FSOLLanePair& pair : pairs)
        {
            text += FString::Printf(TEXT(" (%d, %d)"), pair.NodeA, pair.NodeB);
        }
        return text + TEXT(" }");
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a pair list against the expected one, element by element
    bool LaneNetPairsMatch(FAutomationTestBase& test, const FString& what, TConstArrayView<FSOLLanePair> actual,
        TConstArrayView<FSOLLanePair> expected)
    {
        bool bSame = actual.Num() == expected.Num();
        for (int32 index = 0; bSame && index < actual.Num(); ++index)
        {
            bSame = actual[index].NodeA == expected[index].NodeA && actual[index].NodeB == expected[index].NodeB;
        }
        if (!bSame)
        {
            test.AddError(FString::Printf(TEXT("%s: expected %s, got %s"), *what, *LaneNetPairsText(expected),
                *LaneNetPairsText(actual)));
        }
        return bSame;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an int list as text for messages
    FString LaneNetIntsText(TConstArrayView<int32> values)
    {
        FString text = TEXT("{");
        for (const int32 value : values)
        {
            text += FString::Printf(TEXT(" %d"), value);
        }
        return text + TEXT(" }");
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks an int list against the expected one
    bool LaneNetIntsMatch(FAutomationTestBase& test, const FString& what, TConstArrayView<int32> actual,
        TConstArrayView<int32> expected)
    {
        bool bSame = actual.Num() == expected.Num();
        for (int32 index = 0; bSame && index < actual.Num(); ++index)
        {
            bSame = actual[index] == expected[index];
        }
        if (!bSame)
        {
            test.AddError(FString::Printf(TEXT("%s: expected %s, got %s"), *what, *LaneNetIntsText(expected),
                *LaneNetIntsText(actual)));
        }
        return bSame;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a partner pick
    FSOLPartnerPick LaneNetPick(const int32 partner, const int32 stay)
    {
        FSOLPartnerPick pick;
        pick.Partner = partner;
        pick.StayEpochs = stay;
        return pick;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks one partner pick
    bool LaneNetPickIs(FAutomationTestBase& test, const FString& what, const FSOLPartnerPick& actual,
        const FSOLPartnerPick& expected)
    {
        if (actual.Partner == expected.Partner && actual.StayEpochs == expected.StayEpochs)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected {%d, %d}, got {%d, %d}"), *what, expected.Partner,
            expected.StayEpochs, actual.Partner, actual.StayEpochs));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a pick list against the expected one
    bool LaneNetPicksMatch(FAutomationTestBase& test, const FString& what, TConstArrayView<FSOLPartnerPick> actual,
        TConstArrayView<FSOLPartnerPick> expected)
    {
        if (actual.Num() != expected.Num())
        {
            test.AddError(FString::Printf(TEXT("%s: expected %d picks, got %d"), *what, expected.Num(), actual.Num()));
            return false;
        }
        bool bAll = true;
        for (int32 index = 0; index < actual.Num(); ++index)
        {
            bAll &= LaneNetPickIs(test, FString::Printf(TEXT("%s node %d"), *what, index), actual[index],
                expected[index]);
        }
        return bAll;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the partners of a pick list (for PairsFromPicks)
    TArray<int32> LaneNetPartners(TConstArrayView<FSOLPartnerPick> picks)
    {
        TArray<int32> out;
        for (const FSOLPartnerPick& pick : picks)
        {
            out.Add(pick.Partner);
        }
        return out;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks an enum value by its underlying integer
    template <typename TEnum>
    bool LaneNetEnumIs(FAutomationTestBase& test, const FString& what, const TEnum actual, const TEnum expected)
    {
        if (actual == expected)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected %d, got %d"), *what, static_cast<int32>(expected),
            static_cast<int32>(actual)));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a vector component by component within a relative tolerance of the expected length
    bool LaneNetVecNear(FAutomationTestBase& test, const FString& what, const FVector3d& actual,
        const FVector3d& expected, const double relTol)
    {
        const double tol = relTol * FMath::Max(expected.Length(), 1.0);
        if (FMath::Abs(actual.X - expected.X) <= tol && FMath::Abs(actual.Y - expected.Y) <= tol
            && FMath::Abs(actual.Z - expected.Z) <= tol)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected (%.12g, %.12g, %.12g), got (%.12g, %.12g, %.12g)"), *what,
            expected.X, expected.Y, expected.Z, actual.X, actual.Y, actual.Z));
        return false;
    }

    // One case-1 input with its expected pairs and per-node nearest partners (empty = nearest not checked)
    struct FLaneNetClosestCase
    {
        FString Name;
        TArray<FSOLLaneNode> Nodes;
        TArray<FVector3d> Positions;
        TArray<FSOLLanePair> Base;
        TArray<FSOLLanePair> Expected;
        TArray<int32> ExpectedNearest;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns every input of contract case 1 with its expected output
    TArray<FLaneNetClosestCase> LaneNetClosestCases()
    {
        TArray<FLaneNetClosestCase> cases;

        // Four in a row with three base lanes: both picks made twice
        FLaneNetClosestCase& row = cases.AddDefaulted_GetRef();
        row.Name = TEXT("row of four");
        row.Nodes = LaneNetPlanets(4);
        row.Positions = LaneNetXs({ 1.0, 2.0, 3.0, 4.0 });
        row.Base = { LaneNetPair(0, 1), LaneNetPair(1, 2), LaneNetPair(2, 3) };
        row.Expected = { LaneNetPair(0, 2), LaneNetPair(1, 3) };
        row.ExpectedNearest = { 2, 3, 0, 1 };

        // Every pair is a base lane: nothing left to pick
        FLaneNetClosestCase& full = cases.AddDefaulted_GetRef();
        full.Name = TEXT("all base");
        full.Nodes = LaneNetPlanets(3);
        full.Positions = LaneNetXs({ 1.0, 2.0, 3.0 });
        full.Base = { LaneNetPair(0, 1), LaneNetPair(1, 2), LaneNetPair(0, 2) };
        full.ExpectedNearest = { INDEX_NONE, INDEX_NONE, INDEX_NONE };

        // Tie at node 1 goes to the lower index; (1, 2) only because node 2 picks 1
        FLaneNetClosestCase& tie = cases.AddDefaulted_GetRef();
        tie.Name = TEXT("tie");
        tie.Nodes = LaneNetPlanets(3);
        tie.Positions = LaneNetXs({ 1.0, 3.0, 5.0 });
        tie.Expected = { LaneNetPair(0, 1), LaneNetPair(1, 2) };
        tie.ExpectedNearest = { 1, 0, 1 };

        // Current centre distance, not the orbit radius
        FLaneNetClosestCase& current = cases.AddDefaulted_GetRef();
        current.Name = TEXT("current distance");
        current.Nodes = LaneNetPlanets(3);
        current.Positions = LaneNetXs({ 1.0, -1.2, 3.0 });
        current.Expected = { LaneNetPair(0, 1), LaneNetPair(0, 2) };
        current.ExpectedNearest = { 2, 0, 0 };

        // Kinds: the Moon is never picked; the dwarf picks the asteroid at 3.9 over the planet at 4.0
        FLaneNetClosestCase& kinds = cases.AddDefaulted_GetRef();
        kinds.Name = TEXT("kinds");
        kinds.Nodes = { LaneNetNode(ESOLLaneNodeKind::Planet), LaneNetNode(ESOLLaneNodeKind::Asteroid),
            LaneNetNode(ESOLLaneNodeKind::Moon), LaneNetNode(ESOLLaneNodeKind::DwarfPlanet) };
        kinds.Positions = LaneNetXs({ 1.0, 1.1, 1.0001, 5.0 });
        kinds.Expected = { LaneNetPair(0, 1), LaneNetPair(1, 3) };
        kinds.ExpectedNearest = { 1, 0, INDEX_NONE, 1 };

        // A NaN node is skipped both ways
        FLaneNetClosestCase& nan = cases.AddDefaulted_GetRef();
        nan.Name = TEXT("NaN node");
        nan.Nodes = LaneNetPlanets(4);
        nan.Positions = LaneNetXs({ 1.0, LANENET_NAN, 3.0, 4.0 });
        nan.Base = { LaneNetPair(0, 1), LaneNetPair(1, 2), LaneNetPair(2, 3) };
        nan.Expected = { LaneNetPair(0, 2), LaneNetPair(0, 3) };
        nan.ExpectedNearest = { 2, INDEX_NONE, 0, 0 };

        // Positions shorter than nodes: no pairs (per-node nearest not specified by the contract here)
        FLaneNetClosestCase& shortPositions = cases.AddDefaulted_GetRef();
        shortPositions.Name = TEXT("short positions");
        shortPositions.Nodes = LaneNetPlanets(3);
        shortPositions.Positions = LaneNetXs({ 1.0, 2.0 });

        return cases;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the case 1b replay series: 3 planets, e0 {0, 1, 3}, e1-e3 {0, 1, 0.5}, oldest first
    TArray<FVector3d> LaneNetReplaySeries()
    {
        TArray<FVector3d> all = LaneNetXs({ 0.0, 1.0, 3.0 });
        for (int32 epoch = 1; epoch <= 3; ++epoch)
        {
            all.Append(LaneNetXs({ 0.0, 1.0, 0.5 }));
        }
        return all;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the node positions of one epoch of a series
    TArray<FVector3d> LaneNetEpochSlice(TConstArrayView<FVector3d> all, const int32 epoch, const int32 nodeCount)
    {
        TArray<FVector3d> out;
        for (int32 node = 0; node < nodeCount; ++node)
        {
            out.Add(all[epoch * nodeCount + node]);
        }
        return out;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a synthetic edge (windows open forever by default)
    FSOLLaneEdge LaneNetEdge(const int32 a, const int32 b, const double timeS,
        const FVector3d& direction = FVector3d(1.0, 0.0, 0.0), const double endRingAM = 0.0,
        const double endRingBM = 0.0)
    {
        FSOLLaneEdge edge;
        edge.NodeA = a;
        edge.NodeB = b;
        edge.TimeS = timeS;
        edge.DirectionAB = direction;
        edge.EndRingDistanceAM = endRingAM;
        edge.EndRingDistanceBM = endRingBM;
        return edge;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a route hop
    FSOLLaneHop LaneNetHop(const int32 lane, const int32 sign)
    {
        FSOLLaneHop hop;
        hop.Lane = lane;
        hop.DirectionSign = sign;
        return hop;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a found route's time and hop sequence (hops read only up to the checked count)
    bool LaneNetRouteIs(FAutomationTestBase& test, const FString& what, const FSOLLaneRoute& route,
        const double expectedTimeS, TConstArrayView<FSOLLaneHop> expectedHops)
    {
        if (!route.bFound)
        {
            test.AddError(FString::Printf(TEXT("%s: expected a route, none found"), *what));
            return false;
        }
        bool bOk = LaneNetNear(test, what + TEXT(" time"), route.TimeS, expectedTimeS, LANENET_TIGHT);
        if (route.HopCount != expectedHops.Num())
        {
            test.AddError(FString::Printf(TEXT("%s: expected %d hops, got %d"), *what, expectedHops.Num(),
                route.HopCount));
            return false;
        }
        for (int32 index = 0; index < expectedHops.Num() && index < SOL::NPC_LANE_ROUTE_MAX_HOPS; ++index)
        {
            const FSOLLaneHop& hop = route.Hops[index];
            if (hop.Lane != expectedHops[index].Lane || hop.DirectionSign != expectedHops[index].DirectionSign)
            {
                test.AddError(FString::Printf(TEXT("%s hop %d: expected (e%d, %+d), got (e%d, %+d)"), *what, index,
                    expectedHops[index].Lane, expectedHops[index].DirectionSign, hop.Lane, hop.DirectionSign));
                bOk = false;
            }
        }
        return bOk;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the synthetic 5-node graph of case 9: e0 0-1 10, e1 1-2 10, e2 0-2 25, e3 2-3 5, e4 1-3 30, e5 3-4 1
    TArray<FSOLLaneEdge> LaneNetSyntheticGraph()
    {
        return { LaneNetEdge(0, 1, 10.0), LaneNetEdge(1, 2, 10.0), LaneNetEdge(0, 2, 25.0), LaneNetEdge(2, 3, 5.0),
            LaneNetEdge(1, 3, 30.0), LaneNetEdge(3, 4, 1.0) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the base-lane slots 0-7 used by the slot cases: (k, k + 10)
    TArray<FSOLLanePair> LaneNetBaseSlots()
    {
        TArray<FSOLLanePair> out;
        for (int32 slot = 0; slot < SOL::NPC_LANE_BASE_COUNT; ++slot)
        {
            out.Add(LaneNetPair(slot, slot + 10));
        }
        return out;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the free slot value
    FSOLLanePair LaneNetFree()
    {
        return LaneNetPair(INDEX_NONE, INDEX_NONE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the base-lane population weights of case 12, sqrt(a * b) of the proposed tier products
    TArray<double> LaneNetBaseWeights()
    {
        const double products[] = { 4.0, 20.0, 100.0, 60.0, 12.0, 4.0, 2.0, 1.0 };
        TArray<double> out;
        for (const double product : products)
        {
            out.Add(FMath::Sqrt(product));
        }
        return out;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkNextClosestTest, "SOLTest.LaneNetwork.NextClosest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1: each taking-part node's nearest non-base-linked partner, stored once, sorted, reset first
bool FSOLLaneNetworkNextClosestTest::RunTest(const FString& /*parameters*/)
{
    // Every contract input, with the output pre-filled so a missing reset shows
    for (const FLaneNetClosestCase& item : LaneNetClosestCases())
    {
        TArray<FSOLLanePair> pairs = { LaneNetPair(7, 9) };
        SOLLaneNetwork::NextClosestPairs(item.Nodes, item.Positions, item.Base, pairs);
        LaneNetPairsMatch(*this, item.Name, pairs, item.Expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkPartnerHysteresisTest, "SOLTest.LaneNetwork.PartnerHysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1b: nearest partners, pairs from picks, the hysteresis step and the warm-up replay (O3)
bool FSOLLaneNetworkPartnerHysteresisTest::RunTest(const FString& /*parameters*/)
{
    // NearestPartners per case-1 input, and the identity NextClosestPairs == PairsFromPicks(NearestPartners)
    for (const FLaneNetClosestCase& item : LaneNetClosestCases())
    {
        TArray<int32> nearest = { 42 };
        SOLLaneNetwork::NearestPartners(item.Nodes, item.Positions, item.Base, nearest);
        if (item.ExpectedNearest.Num() > 0)
        {
            LaneNetIntsMatch(*this, TEXT("nearest, ") + item.Name, nearest, item.ExpectedNearest);
        }
        TArray<FSOLLanePair> fromPicks;
        TArray<FSOLLanePair> direct;
        SOLLaneNetwork::PairsFromPicks(nearest, fromPicks);
        SOLLaneNetwork::NextClosestPairs(item.Nodes, item.Positions, item.Base, direct);
        LaneNetPairsMatch(*this, TEXT("identity, ") + item.Name, fromPicks, direct);
        LaneNetPairsMatch(*this, TEXT("identity value, ") + item.Name, fromPicks, item.Expected);
    }

    // PairsFromPicks: union, sorted, stored once, reset first
    {
        TArray<FSOLLanePair> pairs = { LaneNetPair(5, 6) };
        SOLLaneNetwork::PairsFromPicks(TArray<int32>{ 1, 0, 1 }, pairs);
        LaneNetPairsMatch(*this, TEXT("PairsFromPicks {1, 0, 1}"), pairs,
            TArray<FSOLLanePair>{ LaneNetPair(0, 1), LaneNetPair(1, 2) });
        SOLLaneNetwork::PairsFromPicks(TArray<int32>{ 3, INDEX_NONE, 0, 1 }, pairs);
        LaneNetPairsMatch(*this, TEXT("PairsFromPicks {3, -1, 0, 1}"), pairs,
            TArray<FSOLLanePair>{ LaneNetPair(0, 2), LaneNetPair(0, 3), LaneNetPair(1, 3) });
    }

    // StepPartnerPick rows (min stay 30, ratio 0.8 passed as arguments)
    {
        const int32 stay = 30;
        const double ratio = 0.8;
        LaneNetPickIs(*this, TEXT("step none -> 5"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(INDEX_NONE, 0), 5,
            2.0, LANENET_NAN, stay, ratio), LaneNetPick(5, 1));
        LaneNetPickIs(*this, TEXT("step same partner"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 10), 3, 2.0,
            2.0, stay, ratio), LaneNetPick(3, 11));
        LaneNetPickIs(*this, TEXT("step inside min stay"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 10), 5,
            1.0, 2.0, stay, ratio), LaneNetPick(3, 11));
        LaneNetPickIs(*this, TEXT("step switch"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 30), 5, 1.0, 2.0,
            stay, ratio), LaneNetPick(5, 1));
        LaneNetPickIs(*this, TEXT("step 15 % closer"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 30), 5, 1.7,
            2.0, stay, ratio), LaneNetPick(3, 31));
        LaneNetPickIs(*this, TEXT("step exactly the ratio"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 30), 5,
            1.6, 2.0, stay, ratio), LaneNetPick(3, 31));
        LaneNetPickIs(*this, TEXT("step below the ratio"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 30), 5,
            1.59, 2.0, stay, ratio), LaneNetPick(5, 1));
        LaneNetPickIs(*this, TEXT("step held invalid"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 5), 5, 1.0,
            LANENET_NAN, stay, ratio), LaneNetPick(5, 1));
        LaneNetPickIs(*this, TEXT("step no nearest"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 5), INDEX_NONE,
            LANENET_NAN, 2.0, stay, ratio), LaneNetPick(INDEX_NONE, 0));
        LaneNetPickIs(*this, TEXT("step min stay 0"), SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 0), 5, 1.0, 2.0,
            0, ratio), LaneNetPick(5, 1));
    }

    const TArray<FSOLLaneNode> nodes = LaneNetPlanets(3);
    const TArray<FSOLLanePair> noBase;
    const TArray<FVector3d> series = LaneNetReplaySeries();
    const int32 minStay = 2;
    const double ratio = 0.8;

    // ReplayPartnerPicks over e0 .. e(n-1), with the pairs of each result
    {
        const TArray<FSOLPartnerPick> expected[] = {
            { LaneNetPick(1, 1), LaneNetPick(0, 1), LaneNetPick(1, 1) },
            { LaneNetPick(1, 2), LaneNetPick(0, 2), LaneNetPick(1, 2) },
            { LaneNetPick(2, 1), LaneNetPick(2, 1), LaneNetPick(1, 3) },
            { LaneNetPick(2, 2), LaneNetPick(2, 2), LaneNetPick(1, 4) } };
        const TArray<FSOLLanePair> expectedPairs[] = {
            { LaneNetPair(0, 1), LaneNetPair(1, 2) },
            { LaneNetPair(0, 1), LaneNetPair(1, 2) },
            { LaneNetPair(0, 2), LaneNetPair(1, 2) },
            { LaneNetPair(0, 2), LaneNetPair(1, 2) } };
        for (int32 count = 1; count <= 4; ++count)
        {
            TArray<FSOLPartnerPick> picks = { LaneNetPick(9, 9) };
            SOLLaneNetwork::ReplayPartnerPicks(nodes, series, count, noBase, minStay, ratio, picks);
            const FString what = FString::Printf(TEXT("replay epochCount %d"), count);
            LaneNetPicksMatch(*this, what, picks, expected[count - 1]);
            TArray<FSOLLanePair> pairs;
            SOLLaneNetwork::PairsFromPicks(LaneNetPartners(picks), pairs);
            LaneNetPairsMatch(*this, what + TEXT(" pairs"), pairs, expectedPairs[count - 1]);
        }
    }

    // Equivalence with a hand-written StepPartnerPick chain (nearest and distances worked out from the series)
    {
        const double unit = LANENET_UNIT_M;
        FSOLPartnerPick chain[3] = { LaneNetPick(INDEX_NONE, 0), LaneNetPick(INDEX_NONE, 0),
            LaneNetPick(INDEX_NONE, 0) };
        for (int32 epoch = 0; epoch < 4; ++epoch)
        {
            if (epoch == 0)
            {
                chain[0] = SOLLaneNetwork::StepPartnerPick(chain[0], 1, 1.0 * unit, LANENET_NAN, minStay, ratio);
                chain[1] = SOLLaneNetwork::StepPartnerPick(chain[1], 0, 1.0 * unit, LANENET_NAN, minStay, ratio);
                chain[2] = SOLLaneNetwork::StepPartnerPick(chain[2], 1, 2.0 * unit, LANENET_NAN, minStay, ratio);
            }
            else
            {
                // Held distances at e1-e3: node 0 to 1 or 2, node 1 to 0 or 2, node 2 to 1 or 0
                const double held0 = (chain[0].Partner == 1) ? 1.0 * unit : 0.5 * unit;
                const double held1 = (chain[1].Partner == 0) ? 1.0 * unit : 0.5 * unit;
                const double held2 = 0.5 * unit;
                chain[0] = SOLLaneNetwork::StepPartnerPick(chain[0], 2, 0.5 * unit, held0, minStay, ratio);
                chain[1] = SOLLaneNetwork::StepPartnerPick(chain[1], 2, 0.5 * unit, held1, minStay, ratio);
                chain[2] = SOLLaneNetwork::StepPartnerPick(chain[2], 0, 0.5 * unit, held2, minStay, ratio);
            }
            TArray<FSOLPartnerPick> picks;
            SOLLaneNetwork::ReplayPartnerPicks(nodes, series, epoch + 1, noBase, minStay, ratio, picks);
            LaneNetPicksMatch(*this, FString::Printf(TEXT("chain equivalence through e%d"), epoch), picks,
                TArray<FSOLPartnerPick>{ chain[0], chain[1], chain[2] });
        }
    }

    // Consecutive epochs one step apart: replay(e0..en) == one step per node from replay(e0..e(n-1))
    for (int32 n = 1; n <= 3; ++n)
    {
        TArray<FSOLPartnerPick> previous;
        TArray<FSOLPartnerPick> next;
        TArray<int32> nearest;
        SOLLaneNetwork::ReplayPartnerPicks(nodes, series, n, noBase, minStay, ratio, previous);
        SOLLaneNetwork::ReplayPartnerPicks(nodes, series, n + 1, noBase, minStay, ratio, next);
        const TArray<FVector3d> slice = LaneNetEpochSlice(series, n, 3);
        SOLLaneNetwork::NearestPartners(nodes, slice, noBase, nearest);
        if (previous.Num() != 3 || next.Num() != 3 || nearest.Num() != 3)
        {
            AddError(FString::Printf(TEXT("consecutive n %d: expected 3 picks and 3 nearest, got %d, %d, %d"), n,
                previous.Num(), next.Num(), nearest.Num()));
            continue;
        }
        for (int32 node = 0; node < 3; ++node)
        {
            const int32 nearNode = nearest[node];
            const int32 heldNode = previous[node].Partner;
            const double nearDistance = (nearNode >= 0 && nearNode < 3) ? (slice[nearNode] - slice[node]).Length()
                : LANENET_NAN;
            const double heldDistance = (heldNode >= 0 && heldNode < 3) ? (slice[heldNode] - slice[node]).Length()
                : LANENET_NAN;
            const FSOLPartnerPick stepped = SOLLaneNetwork::StepPartnerPick(previous[node], nearNode, nearDistance,
                heldDistance, minStay, ratio);
            LaneNetPickIs(*this, FString::Printf(TEXT("consecutive n %d node %d"), n, node), next[node], stepped);
        }

        // StepPartnerPicks (the look-ahead buffer's step, 53c) is that same step for every node at once
        TArray<FSOLPartnerPick> bufferStep = previous;
        TestTrue(FString::Printf(TEXT("StepPartnerPicks n %d: stepped"), n),
            SOLLaneNetwork::StepPartnerPicks(nodes, slice, noBase, minStay, ratio, bufferStep));
        LaneNetPicksMatch(*this, FString::Printf(TEXT("StepPartnerPicks n %d"), n), bufferStep, next);
    }

    // StepPartnerPicks from no picks (a wrong-sized array starts empty) equals a one-epoch replay; short positions
    // leave the picks untouched
    {
        TArray<FSOLPartnerPick> stepped = { LaneNetPick(9, 9) };
        TArray<FSOLPartnerPick> replayed;
        SOLLaneNetwork::StepPartnerPicks(nodes, LaneNetEpochSlice(series, 0, 3), noBase, minStay, ratio, stepped);
        SOLLaneNetwork::ReplayPartnerPicks(nodes, series, 1, noBase, minStay, ratio, replayed);
        LaneNetPicksMatch(*this, TEXT("StepPartnerPicks from empty"), stepped, replayed);
        TArray<FSOLPartnerPick> untouched = { LaneNetPick(9, 9) };
        TestFalse(TEXT("StepPartnerPicks short positions: false"), SOLLaneNetwork::StepPartnerPicks(nodes,
            TArray<FVector3d>{ FVector3d::ZeroVector }, noBase, minStay, ratio, untouched));
        TestEqual(TEXT("StepPartnerPicks short positions: untouched"), untouched.Num(), 1);
    }

    // Start dependence: the same positions replayed from e1
    {
        TArray<FVector3d> fromE1 = series;
        fromE1.RemoveAt(0, 3);
        TArray<FSOLPartnerPick> picks;
        SOLLaneNetwork::ReplayPartnerPicks(nodes, fromE1, 3, noBase, minStay, ratio, picks);
        LaneNetPicksMatch(*this, TEXT("replay from e1"), picks,
            TArray<FSOLPartnerPick>{ LaneNetPick(2, 3), LaneNetPick(2, 3), LaneNetPick(0, 3) });
    }

    // Determinism, and the empty results (epochCount < 1, positions too short), each reset first
    {
        TArray<FSOLPartnerPick> first;
        TArray<FSOLPartnerPick> second;
        SOLLaneNetwork::ReplayPartnerPicks(nodes, series, 4, noBase, minStay, ratio, first);
        SOLLaneNetwork::ReplayPartnerPicks(nodes, series, 4, noBase, minStay, ratio, second);
        LaneNetPicksMatch(*this, TEXT("determinism"), second, first);

        TArray<FSOLPartnerPick> picks = { LaneNetPick(9, 9) };
        SOLLaneNetwork::ReplayPartnerPicks(nodes, series, 0, noBase, minStay, ratio, picks);
        TestEqual(TEXT("epochCount 0: empty"), picks.Num(), 0);
        picks = { LaneNetPick(9, 9) };
        SOLLaneNetwork::ReplayPartnerPicks(nodes, series, 5, noBase, minStay, ratio, picks);
        TestEqual(TEXT("positions shorter than epochCount x nodes: empty"), picks.Num(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkIslandLinksTest, "SOLTest.LaneNetwork.IslandLinks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1c: one link per island to the root group, nearest first, any candidate in the group relays (never a Moon)
bool FSOLLaneNetworkIslandLinksTest::RunTest(const FString& /*parameters*/)
{
    // Runs IslandLinks with a pre-filled output and checks the result
    auto checkLinks = [this](const FString& what, const TArray<FSOLLaneNode>& nodes, const TArray<FVector3d>& positions,
        const TArray<FSOLLanePair>& lanes, const int32 root, const TArray<FSOLLanePair>& expected)
    {
        TArray<FSOLLanePair> links = { LaneNetPair(7, 9) };
        SOLLaneNetwork::IslandLinks(nodes, positions, lanes, root, links);
        LaneNetPairsMatch(*this, what, links, expected);
    };

    // Three components, the second link relays through asteroid 3
    {
        TArray<FSOLLaneNode> nodes = LaneNetPlanets(6);
        nodes[3].Kind = ESOLLaneNodeKind::Asteroid;
        checkLinks(TEXT("three components"), nodes, LaneNetXs({ 1.0, 2.0, 5.0, 6.0, 10.0, 11.0 }),
            { LaneNetPair(0, 1), LaneNetPair(2, 3), LaneNetPair(4, 5) }, 0, { LaneNetPair(1, 2), LaneNetPair(3, 4) });
    }

    // Nearest first, then the relay through the node joined first; ties to the lower sorted pair
    checkLinks(TEXT("nearest first"), LaneNetPlanets(3), LaneNetXs({ 0.0, 10.0, 4.0 }), {}, 0,
        { LaneNetPair(0, 2), LaneNetPair(1, 2) });
    checkLinks(TEXT("tie"), LaneNetPlanets(3), LaneNetXs({ 1.0, 3.0, -1.0 }), {}, 0,
        { LaneNetPair(0, 1), LaneNetPair(0, 2) });

    // Moons: never linked as v and never a relay (u); the island link goes through the Moon's parent (Earth)
    {
        TArray<FSOLLaneNode> nodes = { LaneNetNode(ESOLLaneNodeKind::Planet), LaneNetNode(ESOLLaneNodeKind::Moon) };
        nodes[1].ParentNode = 0;
        checkLinks(TEXT("moon outside the root group"), nodes, LaneNetXs({ 1.0, 1.0001 }), {}, 0, {});
        nodes.Add(LaneNetNode(ESOLLaneNodeKind::Planet));
        checkLinks(TEXT("moon via earth, moon nearer"), nodes, LaneNetXs({ 1.0, 5.0, 5.1 }), { LaneNetPair(0, 1) }, 0,
            { LaneNetPair(0, 2) });
        checkLinks(TEXT("earth nearer than the moon"), nodes, LaneNetXs({ 1.0, 5.0, 0.5 }), { LaneNetPair(0, 1) }, 0,
            { LaneNetPair(0, 2) });
    }

    // Corrected Moon / relay tie (contract 1c): Moon 0 (parent 1), Earth 1 the root, node 2 equidistant at 2 from
    // both; a Moon relay would win the tie as the lower pair (0, 2), the rule gives Earth's (1, 2)
    {
        TArray<FSOLLaneNode> nodes = { LaneNetNode(ESOLLaneNodeKind::Moon), LaneNetNode(ESOLLaneNodeKind::Planet),
            LaneNetNode(ESOLLaneNodeKind::Planet) };
        nodes[0].ParentNode = 1;
        checkLinks(TEXT("moon / relay tie"), nodes, LaneNetXs({ 5.0, 1.0, 3.0 }), { LaneNetPair(0, 1) }, 1,
            { LaneNetPair(1, 2) });
    }

    // NaN node neither linked nor counted; a connected set and an out-of-range root give no links
    checkLinks(TEXT("NaN node"), LaneNetPlanets(3), LaneNetXs({ 1.0, 2.0, LANENET_NAN }), {}, 0, { LaneNetPair(0, 1) });
    checkLinks(TEXT("connected"), LaneNetPlanets(3), LaneNetXs({ 1.0, 2.0, 3.0 }),
        { LaneNetPair(0, 1), LaneNetPair(1, 2) }, 0, {});
    checkLinks(TEXT("root out of range"), LaneNetPlanets(3), LaneNetXs({ 1.0, 5.0, 9.0 }), {}, 3, {});
    checkLinks(TEXT("root negative"), LaneNetPlanets(3), LaneNetXs({ 1.0, 5.0, 9.0 }), {}, INDEX_NONE, {});
    checkLinks(TEXT("positions shorter than nodes"), LaneNetPlanets(3), LaneNetXs({ 1.0, 5.0 }), {}, 0, {});

    // Purity: two calls with the same inputs give identical output
    {
        const TArray<FSOLLaneNode> nodes = LaneNetPlanets(4);
        const TArray<FVector3d> positions = LaneNetXs({ 1.0, 2.5, 7.0, -3.0 });
        const TArray<FSOLLanePair> noLanes;
        TArray<FSOLLanePair> first;
        TArray<FSOLLanePair> second;
        SOLLaneNetwork::IslandLinks(nodes, positions, noLanes, 0, first);
        SOLLaneNetwork::IslandLinks(nodes, positions, noLanes, 0, second);
        LaneNetPairsMatch(*this, TEXT("purity"), second, first);
        LaneNetPairsMatch(*this, TEXT("purity: four singletons, three links"), first,
            TArray<FSOLLanePair>{ LaneNetPair(0, 1), LaneNetPair(0, 3), LaneNetPair(1, 2) });
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkDailyRecalcTest, "SOLTest.LaneNetwork.DailyRecalc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 2: the epoch index and sample time, daily stability of the lane set, and the slot assignment
bool FSOLLaneNetworkDailyRecalcTest::RunTest(const FString& /*parameters*/)
{
    const double epoch = SOL::NPC_LANE_NETWORK_EPOCH_S;

    // Index = floor(t / epoch); bad inputs give 0
    TestEqual(TEXT("index t 0"), SOLLaneNetwork::LaneNetworkEpochIndex(0.0, epoch), int64(0));
    TestEqual(TEXT("index t 86399.9"), SOLLaneNetwork::LaneNetworkEpochIndex(86399.9, epoch), int64(0));
    TestEqual(TEXT("index t 86400"), SOLLaneNetwork::LaneNetworkEpochIndex(86400.0, epoch), int64(1));
    TestEqual(TEXT("index t -1"), SOLLaneNetwork::LaneNetworkEpochIndex(-1.0, epoch), int64(-1));
    TestEqual(TEXT("index t 3.15576e9"), SOLLaneNetwork::LaneNetworkEpochIndex(3.15576e9, epoch), int64(36525));
    TestEqual(TEXT("index epoch 0"), SOLLaneNetwork::LaneNetworkEpochIndex(1.0e6, 0.0), int64(0));
    TestEqual(TEXT("index epoch negative"), SOLLaneNetwork::LaneNetworkEpochIndex(1.0e6, -86400.0), int64(0));
    TestEqual(TEXT("index NaN time"), SOLLaneNetwork::LaneNetworkEpochIndex(LANENET_NAN, epoch), int64(0));
    TestEqual(TEXT("index NaN epoch"), SOLLaneNetwork::LaneNetworkEpochIndex(1.0e6, LANENET_NAN), int64(0));
    LaneNetExact(*this, TEXT("sample time 36525"), SOLLaneNetwork::LaneEpochSampleTimeS(36525, epoch), 3.15576e9);

    // Daily stability: 1 s and 86,399 s into epoch 36525 give the same index, sample time and lane set
    {
        const double t1 = 36525.0 * epoch + 1.0;
        const double t2 = 36525.0 * epoch + 86399.0;
        const int64 index1 = SOLLaneNetwork::LaneNetworkEpochIndex(t1, epoch);
        const int64 index2 = SOLLaneNetwork::LaneNetworkEpochIndex(t2, epoch);
        TestEqual(TEXT("stability: index at 1 s"), index1, int64(36525));
        TestEqual(TEXT("stability: same index"), index2, index1);
        const double sample1 = SOLLaneNetwork::LaneEpochSampleTimeS(index1, epoch);
        const double sample2 = SOLLaneNetwork::LaneEpochSampleTimeS(index2, epoch);
        LaneNetExact(*this, TEXT("stability: sample time"), sample1, 3.15576e9);
        LaneNetExact(*this, TEXT("stability: same sample time"), sample2, sample1);

        // Five synthetic circular orbits evaluated at each sample time
        auto positionsAt = [](const double timeS)
        {
            TArray<FVector3d> out;
            for (int32 node = 0; node < 5; ++node)
            {
                const double radius = (node + 1) * 0.7 * LANENET_UNIT_M;
                const double period = FMath::Pow(node + 1.0, 1.5) * 3.15576e7;
                const double angle = UE_DOUBLE_TWO_PI * FMath::Fmod(timeS / period, 1.0);
                out.Add(FVector3d(radius * FMath::Cos(angle), radius * FMath::Sin(angle), 0.0));
            }
            return out;
        };
        const TArray<FSOLLaneNode> nodes = LaneNetPlanets(5);
        const TArray<FSOLLanePair> base = { LaneNetPair(0, 1), LaneNetPair(1, 2) };
        TArray<FSOLLanePair> pairs1;
        TArray<FSOLLanePair> pairs2;
        SOLLaneNetwork::NextClosestPairs(nodes, positionsAt(sample1), base, pairs1);
        SOLLaneNetwork::NextClosestPairs(nodes, positionsAt(sample2), base, pairs2);
        LaneNetPairsMatch(*this, TEXT("stability: NextClosestPairs"), pairs2, pairs1);
        TestTrue(TEXT("stability: NextClosestPairs finds pairs"), pairs1.Num() > 0);
        TArray<FSOLLanePair> links1;
        TArray<FSOLLanePair> links2;
        SOLLaneNetwork::IslandLinks(nodes, positionsAt(sample1), base, 0, links1);
        SOLLaneNetwork::IslandLinks(nodes, positionsAt(sample2), base, 0, links2);
        LaneNetPairsMatch(*this, TEXT("stability: IslandLinks"), links2, links1);
        TestEqual(TEXT("stability: IslandLinks joins the 2 islands"), links1.Num(), 2);
        const FSOLPartnerPick step1 = SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 30), 5, 1.0, 2.0, 30, 0.8);
        const FSOLPartnerPick step2 = SOLLaneNetwork::StepPartnerPick(LaneNetPick(3, 30), 5, 1.0, 2.0, 30, 0.8);
        LaneNetPickIs(*this, TEXT("stability: StepPartnerPick"), step2, step1);
        LaneNetPickIs(*this, TEXT("stability: StepPartnerPick value"), step1, LaneNetPick(5, 1));
    }

    // AssignLaneSlots: capacity 12, base 8, previous 8 = (0, 2), 9 = (1, 3), set {(1, 3), (2, 5), (4, 6)}
    TArray<FSOLLanePair> previous = LaneNetBaseSlots();
    previous.Append({ LaneNetPair(0, 2), LaneNetPair(1, 3), LaneNetFree(), LaneNetFree() });
    const TArray<FSOLLanePair> set = { LaneNetPair(1, 3), LaneNetPair(2, 5), LaneNetPair(4, 6) };
    TArray<uint8> held;
    held.Init(0, 12);
    {
        TArray<FSOLLanePair> slots = { LaneNetPair(7, 9) };
        const int32 overflow = SOLLaneNetwork::AssignLaneSlots(previous, held, set, 8, 12, slots);
        TArray<FSOLLanePair> expected = LaneNetBaseSlots();
        expected.Append({ LaneNetPair(2, 5), LaneNetPair(1, 3), LaneNetPair(4, 6), LaneNetFree() });
        LaneNetPairsMatch(*this, TEXT("slots, none held"), slots, expected);
        TestEqual(TEXT("slots, none held: overflow"), overflow, 0);

        // The same inputs twice: identical output
        TArray<FSOLLanePair> again;
        const int32 overflowAgain = SOLLaneNetwork::AssignLaneSlots(previous, held, set, 8, 12, again);
        LaneNetPairsMatch(*this, TEXT("slots, repeated"), again, slots);
        TestEqual(TEXT("slots, repeated: overflow"), overflowAgain, overflow);
    }

    // Slot 8 held: its fading lane stays, the new lanes take 10 and 11
    held[8] = 1;
    {
        TArray<FSOLLanePair> slots;
        const int32 overflow = SOLLaneNetwork::AssignLaneSlots(previous, held, set, 8, 12, slots);
        TArray<FSOLLanePair> expected = LaneNetBaseSlots();
        expected.Append({ LaneNetPair(0, 2), LaneNetPair(1, 3), LaneNetPair(2, 5), LaneNetPair(4, 6) });
        LaneNetPairsMatch(*this, TEXT("slots, 8 held"), slots, expected);
        TestEqual(TEXT("slots, 8 held: overflow"), overflow, 0);
    }

    // Slot 8 held, capacity 10 (previous and held given for slots 0-9 only): 2 lanes overflow
    {
        TArray<FSOLLanePair> previous10 = previous;
        previous10.SetNum(10);
        TArray<uint8> held10 = held;
        held10.SetNum(10);
        TArray<FSOLLanePair> slots;
        const int32 overflow = SOLLaneNetwork::AssignLaneSlots(previous10, held10, set, 8, 10, slots);
        TArray<FSOLLanePair> expected = LaneNetBaseSlots();
        expected.Append({ LaneNetPair(0, 2), LaneNetPair(1, 3) });
        LaneNetPairsMatch(*this, TEXT("slots, capacity 10"), slots, expected);
        TestEqual(TEXT("slots, capacity 10: overflow"), overflow, 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkSunClearanceTest, "SOLTest.LaneNetwork.SunClearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 3: the Sun-to-segment distance (Sun at the origin)
bool FSOLLaneNetworkSunClearanceTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::SunSegmentDistanceM;
    LaneNetNear(*this, TEXT("passing segment"), SunSegmentDistanceM(FVector3d(1e11, 5e9, 0.0),
        FVector3d(-1e11, 5e9, 0.0)), 5.0e9, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("endpoint A"), SunSegmentDistanceM(FVector3d(1e11, 0.0, 0.0), FVector3d(2e11, 0.0, 0.0)),
        1.0e11, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("through the Sun"), SunSegmentDistanceM(FVector3d(1e11, 0.0, 0.0),
        FVector3d(-1e11, 0.0, 0.0)), 0.0, 1.0e-3);
    LaneNetNear(*this, TEXT("A == B"), SunSegmentDistanceM(FVector3d(3e10, 4e10, 0.0), FVector3d(3e10, 4e10, 0.0)),
        5.0e10, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("diagonal"), SunSegmentDistanceM(FVector3d(1e11, 0.0, 0.0), FVector3d(0.0, 1e11, 0.0)),
        1.0e11 / FMath::Sqrt(2.0), LANENET_TIGHT);

    // Any NaN or infinite input: NaN
    LaneNetIsNaN(*this, TEXT("NaN in A"), SunSegmentDistanceM(FVector3d(LANENET_NAN, 0.0, 0.0),
        FVector3d(1e11, 0.0, 0.0)));
    LaneNetIsNaN(*this, TEXT("NaN in B"), SunSegmentDistanceM(FVector3d(1e11, 0.0, 0.0),
        FVector3d(0.0, 0.0, LANENET_NAN)));
    LaneNetIsNaN(*this, TEXT("infinity in A"), SunSegmentDistanceM(FVector3d(0.0, LANENET_INF, 0.0),
        FVector3d(1e11, 0.0, 0.0)));
    LaneNetIsNaN(*this, TEXT("infinity in B"), SunSegmentDistanceM(FVector3d(1e11, 0.0, 0.0),
        FVector3d(-LANENET_INF, 0.0, 0.0)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkSunHysteresisTest, "SOLTest.LaneNetwork.SunHysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 4: the Sun state's hysteresis step and its pure look-back form
bool FSOLLaneNetworkSunHysteresisTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::StepSunState;
    using SOLLaneNetwork::SunStateFromLookBack;
    const double sun = SOL::SUN_RADIUS_M;
    const ESOLLaneSunState open = ESOLLaneSunState::Open;
    const ESOLLaneSunState closed = ESOLLaneSunState::Closed;

    // StepSunState rows
    LaneNetEnumIs(*this, TEXT("open, 1.39e10"), StepSunState(open, 1.39e10, sun), closed);
    LaneNetEnumIs(*this, TEXT("open, 1.3914e10"), StepSunState(open, 1.3914e10, sun), open);
    LaneNetEnumIs(*this, TEXT("closed, 1.45e10"), StepSunState(closed, 1.45e10, sun), closed);
    LaneNetEnumIs(*this, TEXT("closed, 1.54e10"), StepSunState(closed, 1.54e10, sun), open);
    LaneNetEnumIs(*this, TEXT("open, NaN"), StepSunState(open, LANENET_NAN, sun), closed);
    LaneNetEnumIs(*this, TEXT("closed, NaN"), StepSunState(closed, LANENET_NAN, sun), closed);

    // SunStateFromLookBack rows (newest first)
    LaneNetEnumIs(*this, TEXT("look-back {1.6e10}"), SunStateFromLookBack(TArray<double>{ 1.6e10 }, sun), open);
    LaneNetEnumIs(*this, TEXT("look-back {1.45e10, 1.6e10}"),
        SunStateFromLookBack(TArray<double>{ 1.45e10, 1.6e10 }, sun), open);
    LaneNetEnumIs(*this, TEXT("look-back {1.45e10, 1.3e10}"),
        SunStateFromLookBack(TArray<double>{ 1.45e10, 1.3e10 }, sun), closed);
    LaneNetEnumIs(*this, TEXT("look-back exhausted"), SunStateFromLookBack(TArray<double>{ 1.45e10, 1.45e10 }, sun),
        closed);
    LaneNetEnumIs(*this, TEXT("look-back empty"), SunStateFromLookBack(TArray<double>{}, sun), closed);
    LaneNetEnumIs(*this, TEXT("look-back NaN first"), SunStateFromLookBack(TArray<double>{ LANENET_NAN, 1.6e10 }, sun),
        closed);

    // Equivalence: stepping oldest to newest from Open equals the look-back of each newest-first prefix
    {
        const double series[] = { 1.6e10, 1.45e10, 1.3e10, 1.45e10, 1.6e10 };
        const ESOLLaneSunState expected[] = { open, open, closed, closed, open };
        ESOLLaneSunState state = open;
        for (int32 index = 0; index < 5; ++index)
        {
            state = StepSunState(state, series[index], sun);
            LaneNetEnumIs(*this, FString::Printf(TEXT("step series %d"), index), state, expected[index]);
            TArray<double> newestFirst;
            for (int32 back = index; back >= 0; --back)
            {
                newestFirst.Add(series[back]);
            }
            LaneNetEnumIs(*this, FString::Printf(TEXT("look-back prefix %d"), index),
                SunStateFromLookBack(newestFirst, sun), expected[index]);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkEpochBoundTest, "SOLTest.LaneNetwork.EpochBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 5: an epoch's clearance lower bound, -inf (closed) for a bad input
bool FSOLLaneNetworkEpochBoundTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::EpochClearanceBoundM;
    LaneNetNear(*this, TEXT("bound"), EpochClearanceBoundM(TArray<double>{ 2e10, 1.5e10, 1.8e10 }, 5.9e4, 21600.0),
        1.5e10 - 5.9e4 * 21600.0 / 2.0, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("bound value"), EpochClearanceBoundM(TArray<double>{ 2e10, 1.5e10, 1.8e10 }, 5.9e4,
        21600.0), 1.43628e10, LANENET_TIGHT);
    LaneNetExact(*this, TEXT("empty"), EpochClearanceBoundM(TArray<double>{}, 5.9e4, 21600.0), -LANENET_INF);
    LaneNetExact(*this, TEXT("NaN sample"), EpochClearanceBoundM(TArray<double>{ 2e10, LANENET_NAN }, 5.9e4, 21600.0),
        -LANENET_INF);
    LaneNetExact(*this, TEXT("NaN speed"), EpochClearanceBoundM(TArray<double>{ 2e10 }, LANENET_NAN, 21600.0),
        -LANENET_INF);
    LaneNetExact(*this, TEXT("NaN spacing"), EpochClearanceBoundM(TArray<double>{ 2e10 }, 5.9e4, LANENET_NAN),
        -LANENET_INF);
    LaneNetExact(*this, TEXT("negative speed"), EpochClearanceBoundM(TArray<double>{ 2e10 }, -1.0, 21600.0),
        -LANENET_INF);
    LaneNetExact(*this, TEXT("negative spacing"), EpochClearanceBoundM(TArray<double>{ 2e10 }, 5.9e4, -1.0),
        -LANENET_INF);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkEarlyStopTest, "SOLTest.LaneNetwork.EarlyStop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 6: the open window from the look-ahead schedule, the leg fit and the departure stop
bool FSOLLaneNetworkEarlyStopTest::RunTest(const FString& /*parameters*/)
{
    const double e = LANENET_DAY_S;
    const TArray<uint8> flags = { 1, 1, 0, 0, 1, 1, 1 };   // Epochs 100-106

    // Runs LaneOpenWindow at t with a latched open start and checks an open result
    auto checkOpen = [this, &flags, e](const FString& what, const double t, const double latched,
        const double expectedStart, const double expectedClose)
    {
        double start = 0.0;
        double close = 0.0;
        TestTrue(what + TEXT(": open"), SOLLaneNetwork::LaneOpenWindow(flags, 100, e, t, latched, start, close));
        LaneNetExact(*this, what + TEXT(": open start"), start, expectedStart);
        LaneNetExact(*this, what + TEXT(": close"), close, expectedClose);
    };
    // Runs LaneOpenWindow at t and checks a closed result (outputs NaN, whatever the latch)
    auto checkClosed = [this, &flags, e](const FString& what, const double t)
    {
        double start = 0.0;
        double close = 0.0;
        TestFalse(what + TEXT(": closed"),
            SOLLaneNetwork::LaneOpenWindow(flags, 100, e, t, -LANENET_INF, start, close));
        LaneNetIsNaN(*this, what + TEXT(": open start"), start);
        LaneNetIsNaN(*this, what + TEXT(": close"), close);
    };

    // A run reaching index 0: the carried latch (-inf or finite), a new / reopened lane (NaN) at k x E, and a latch
    // after the first entry clamped to it
    checkOpen(TEXT("t 100.5 E, latched -inf"), 100.5 * e, -LANENET_INF, -LANENET_INF, 102.0 * e);
    checkOpen(TEXT("t 100.5 E, latched 90 E"), 100.5 * e, 90.0 * e, 90.0 * e, 102.0 * e);
    checkOpen(TEXT("t 100 E, new lane at index 0"), 100.0 * e, LANENET_NAN, 100.0 * e, 102.0 * e);
    checkOpen(TEXT("t 101.9 E, reopened at index 0"), 101.9 * e, LANENET_NAN, 100.0 * e, 102.0 * e);
    checkOpen(TEXT("t 100.5 E, latch after the first entry"), 100.5 * e, 101.0 * e, 100.0 * e, 102.0 * e);

    // A run starting inside the buffer ignores the latch
    checkOpen(TEXT("t 104.2 E"), 104.2 * e, LANENET_NAN, 104.0 * e, 107.0 * e);
    checkOpen(TEXT("t 104.2 E, latched -inf"), 104.2 * e, -LANENET_INF, 104.0 * e, 107.0 * e);
    checkClosed(TEXT("t 102.5 E"), 102.5 * e);
    checkClosed(TEXT("t 99 E"), 99.0 * e);
    checkClosed(TEXT("t 107 E"), 107.0 * e);

    // The new-lane start keeps a mid-leg ship hidden: a leg departed before k x E is not drawn, one departing at it is
    {
        double start = 0.0;
        double close = 0.0;
        SOLLaneNetwork::LaneOpenWindow(flags, 100, e, 100.5 * e, LANENET_NAN, start, close);
        const double leg = 0.5 * e;
        const double stop = SOLLaneNetwork::LaneDepartureStopS(close, leg);
        TestFalse(TEXT("new lane: leg departed at 99.8 E hidden"),
            SOLLaneNetwork::LaneShipLegDrawn(100.1 * e, 99.8 * e, leg, start, stop));
        TestTrue(TEXT("new lane: leg departed at 100 E drawn"),
            SOLLaneNetwork::LaneShipLegDrawn(100.1 * e, 100.0 * e, leg, start, stop));
    }

    // LegFitsWindow rows
    using SOLLaneNetwork::LegFitsWindow;
    TestTrue(TEXT("fits (70, 30, 0, 100)"), LegFitsWindow(70.0, 30.0, 0.0, 100.0));
    TestFalse(TEXT("fits (70.1, 30, 0, 100)"), LegFitsWindow(70.1, 30.0, 0.0, 100.0));
    TestFalse(TEXT("fits (-5, 30, 0, 100)"), LegFitsWindow(-5.0, 30.0, 0.0, 100.0));
    TestTrue(TEXT("fits (10, 30, -inf, 40)"), LegFitsWindow(10.0, 30.0, -LANENET_INF, 40.0));
    TestFalse(TEXT("fits (0, 30, 0, 20)"), LegFitsWindow(0.0, 30.0, 0.0, 20.0));
    TestFalse(TEXT("fits NaN depart"), LegFitsWindow(LANENET_NAN, 30.0, 0.0, 100.0));
    TestFalse(TEXT("fits NaN leg"), LegFitsWindow(10.0, LANENET_NAN, 0.0, 100.0));
    TestFalse(TEXT("fits NaN open"), LegFitsWindow(10.0, 30.0, LANENET_NAN, 100.0));
    TestFalse(TEXT("fits NaN close"), LegFitsWindow(10.0, 30.0, 0.0, LANENET_NAN));
    TestFalse(TEXT("fits negative leg"), LegFitsWindow(10.0, -1.0, 0.0, 100.0));

    // LaneDepartureStopS rows
    LaneNetExact(*this, TEXT("stop (100, 30)"), SOLLaneNetwork::LaneDepartureStopS(100.0, 30.0), 70.0);
    LaneNetExact(*this, TEXT("stop (inf, 30)"), SOLLaneNetwork::LaneDepartureStopS(LANENET_INF, 30.0), LANENET_INF);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkFinishTripTest, "SOLTest.LaneNetwork.FinishTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 6: a lane ship's leg is drawn from a departure inside the window to its arrival, never cut mid-leg
bool FSOLLaneNetworkFinishTripTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::LaneShipLegDrawn;

    // Finish trip: open 0, departure stop 70, leg 30
    TestTrue(TEXT("depart 70, t 70"), LaneShipLegDrawn(70.0, 70.0, 30.0, 0.0, 70.0));
    TestTrue(TEXT("depart 70, t 85"), LaneShipLegDrawn(85.0, 70.0, 30.0, 0.0, 70.0));
    TestTrue(TEXT("depart 70, t 99.999"), LaneShipLegDrawn(99.999, 70.0, 30.0, 0.0, 70.0));
    TestFalse(TEXT("depart 70, t 100"), LaneShipLegDrawn(100.0, 70.0, 30.0, 0.0, 70.0));
    TestFalse(TEXT("depart 70.1, t 80"), LaneShipLegDrawn(80.0, 70.1, 30.0, 0.0, 70.0));
    TestTrue(TEXT("depart 20, t 20"), LaneShipLegDrawn(20.0, 20.0, 30.0, 0.0, 70.0));
    TestTrue(TEXT("depart 20, t 49.9"), LaneShipLegDrawn(49.9, 20.0, 30.0, 0.0, 70.0));

    // Window shorter than the leg: open 0, close 20, leg 30, so the departure stop is -10
    LaneNetExact(*this, TEXT("short window: stop"), SOLLaneNetwork::LaneDepartureStopS(20.0, 30.0), -10.0);
    TestFalse(TEXT("short window: depart 0, t 5"), LaneShipLegDrawn(5.0, 0.0, 30.0, 0.0, -10.0));

    // No ship vanishes mid-leg: drawn is constant true from departure to arrival
    int32 drawnCount = 0;
    int32 sampleCount = 0;
    for (int32 depart = 0; depart <= 70; depart += 10)
    {
        for (double t = depart; t < depart + 30.0; t += 0.5)
        {
            ++sampleCount;
            drawnCount += LaneShipLegDrawn(t, depart, 30.0, 0.0, 70.0) ? 1 : 0;
        }
    }
    TestEqual(TEXT("no ship vanishes mid-leg: every sample drawn"), drawnCount, sampleCount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkEdgeTimeTest, "SOLTest.LaneNetwork.EdgeTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 7: the edge time is the mean-effort keep-right leg (two identities and two values)
bool FSOLLaneNetworkEdgeTimeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneProfile outer = SOLLaneTraffic::MakeLaneProfile(6.0e6, 4.0e11, 6.371e6, 3.3895e6);
    const FSOLLaneProfile inner = SOLLaneTraffic::MakeLaneProfile(6.0e5, 4.0e8, 6.371e6, 1.7374e6);
    TestTrue(TEXT("precondition: outer profile drivable"), outer.bDrivable);
    TestTrue(TEXT("precondition: inner profile drivable"), inner.bDrivable);

    // Values (relative 1e-6)
    LaneNetNear(*this, TEXT("outer value"), SOLLaneNetwork::LaneEdgeTimeS(outer), 1398839.39, LANENET_LOOSE);
    LaneNetNear(*this, TEXT("inner value"), SOLLaneNetwork::LaneEdgeTimeS(inner), 8685.83, LANENET_LOOSE);

    // Identities: the mean-effort leg profile (exact) and the lane leg time over every frame, both ways
    const FSOLLaneProfile profiles[] = { outer, inner };
    const TCHAR* names[] = { TEXT("outer"), TEXT("inner") };
    const FVector3d a(1.0e11, 2.0e10, 3.0e9);
    const FVector3d direction = FVector3d(0.6, 0.8, 0.0);
    for (int32 index = 0; index < 2; ++index)
    {
        const FSOLLaneProfile& profile = profiles[index];
        const double edge = SOLLaneNetwork::LaneEdgeTimeS(profile);
        const double legS = SOLLaneTraffic::MakeLegProfile(
            SOLLaneTraffic::RadialFactor(SOL::NPC_LANE_AUTO_TRACK_WALL_RADII), SOL::NPC_LANE_EFFORT_MEAN,
            profile.NominalLegM).LegS;
        LaneNetNear(*this, FString::Printf(TEXT("%s: leg profile identity"), names[index]), edge, legS,
            LANENET_TIGHT);
        for (const double fraction : { 0.3, 0.7, 1.0 })
        {
            const FSOLLaneFrame frame = SOLLaneTraffic::MakeLaneFrame(profile, a,
                a + direction * (fraction * profile.NominalLengthM));
            for (const int32 sign : { 1, -1 })
            {
                const double laneS = SOLLaneTraffic::LaneLegTimeS(profile, frame, 0.0, frame.CurrentLegM,
                    SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, sign);
                LaneNetNear(*this, FString::Printf(TEXT("%s: frame %.1f L0, direction %+d"), names[index], fraction,
                    sign), edge, laneS, LANENET_LOOSE);
            }
        }
    }

    // A non-drivable profile: 0
    const FSOLLaneProfile bad = SOLLaneTraffic::MakeLaneProfile(0.0, 4.0e11, 6.371e6, 3.3895e6);
    TestFalse(TEXT("precondition: zero wall radius not drivable"), bad.bDrivable);
    LaneNetExact(*this, TEXT("non-drivable"), SOLLaneNetwork::LaneEdgeTimeS(bad), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTransferTest, "SOLTest.LaneNetwork.Transfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8: the node transfer time, penalty + (0.5 (r1 + r2) theta + |r1 - r2|) / w
bool FSOLLaneNetworkTransferTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::NodeTransferTimeS;
    const FVector3d x(1.0, 0.0, 0.0);
    const FVector3d y(0.0, 1.0, 0.0);
    const double w = 2.0e4;

    LaneNetNear(*this, TEXT("same direction"), NodeTransferTimeS(2.4e7, 2.4e7, x, x, w, 3600.0), 3600.0,
        LANENET_TIGHT);
    LaneNetNear(*this, TEXT("opposite"), NodeTransferTimeS(2.4e7, 2.4e7, x, -x, w, 3600.0),
        3600.0 + 2.4e7 * UE_DOUBLE_PI / 2.0e4, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("opposite value"), NodeTransferTimeS(2.4e7, 2.4e7, x, -x, w, 3600.0), 7369.91, 1.0e-6);
    LaneNetNear(*this, TEXT("perpendicular"), NodeTransferTimeS(2.0e7, 3.0e7, x, y, w, 3600.0),
        3600.0 + (2.5e7 * UE_DOUBLE_HALF_PI + 1.0e7) / 2.0e4, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("perpendicular value"), NodeTransferTimeS(2.0e7, 3.0e7, x, y, w, 3600.0), 6063.50,
        1.0e-6);
    LaneNetNear(*this, TEXT("unnormalized directions"), NodeTransferTimeS(2.0e7, 3.0e7, 2.0 * x, 3.0 * y, w, 3600.0),
        3600.0 + (2.5e7 * UE_DOUBLE_HALF_PI + 1.0e7) / 2.0e4, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("zero distances"), NodeTransferTimeS(0.0, 0.0, x, y, w, 3.0), 3.0, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("symmetric"), NodeTransferTimeS(3.0e7, 2.0e7, y, x, w, 3600.0),
        NodeTransferTimeS(2.0e7, 3.0e7, x, y, w, 3600.0), LANENET_TIGHT);
    LaneNetNear(*this, TEXT("symmetric value"), NodeTransferTimeS(3.0e7, 2.0e7, y, x, w, 3600.0),
        3600.0 + (2.5e7 * UE_DOUBLE_HALF_PI + 1.0e7) / 2.0e4, LANENET_TIGHT);

    // Bad inputs: +inf (no transfer)
    const FVector3d zero = FVector3d::ZeroVector;
    const FVector3d nanDir(LANENET_NAN, 0.0, 0.0);
    LaneNetExact(*this, TEXT("zero direction 1"), NodeTransferTimeS(1e7, 1e7, zero, x, w, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("zero direction 2"), NodeTransferTimeS(1e7, 1e7, x, zero, w, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("NaN direction"), NodeTransferTimeS(1e7, 1e7, x, nanDir, w, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("zero speed"), NodeTransferTimeS(1e7, 1e7, x, y, 0.0, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("negative speed"), NodeTransferTimeS(1e7, 1e7, x, y, -w, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("NaN speed"), NodeTransferTimeS(1e7, 1e7, x, y, LANENET_NAN, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("infinite speed"), NodeTransferTimeS(1e7, 1e7, x, y, LANENET_INF, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("negative distance"), NodeTransferTimeS(-1.0, 1e7, x, y, w, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("NaN distance"), NodeTransferTimeS(1e7, LANENET_NAN, x, y, w, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("infinite distance"), NodeTransferTimeS(LANENET_INF, 1e7, x, y, w, 0.0), LANENET_INF);
    LaneNetExact(*this, TEXT("negative penalty"), NodeTransferTimeS(1e7, 1e7, x, y, w, -1.0), LANENET_INF);
    LaneNetExact(*this, TEXT("NaN penalty"), NodeTransferTimeS(1e7, 1e7, x, y, w, LANENET_NAN), LANENET_INF);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkDijkstraTest, "SOLTest.LaneNetwork.Dijkstra",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 9: the fastest route over (node, arriving lane) states with transfers and open windows
bool FSOLLaneNetworkDijkstraTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::FindFastestRoute;
    const double w = 2.0e4;
    const TArray<FSOLLaneEdge> graph = LaneNetSyntheticGraph();

    // Penalty sweeps on the synthetic graph
    LaneNetRouteIs(*this, TEXT("penalty 0, 0 -> 3"), FindFastestRoute(5, graph, 0.0, w, 0.0, 0, 3), 25.0,
        TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(1, 1), LaneNetHop(3, 1) });
    LaneNetRouteIs(*this, TEXT("penalty 3, 0 -> 3"), FindFastestRoute(5, graph, 3.0, w, 0.0, 0, 3), 31.0,
        TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(1, 1), LaneNetHop(3, 1) });
    LaneNetRouteIs(*this, TEXT("penalty 6, 0 -> 3"), FindFastestRoute(5, graph, 6.0, w, 0.0, 0, 3), 36.0,
        TArray<FSOLLaneHop>{ LaneNetHop(2, 1), LaneNetHop(3, 1) });
    LaneNetRouteIs(*this, TEXT("penalty 0, 4 -> 0"), FindFastestRoute(5, graph, 0.0, w, 0.0, 4, 0), 26.0,
        TArray<FSOLLaneHop>{ LaneNetHop(5, -1), LaneNetHop(3, -1), LaneNetHop(1, -1), LaneNetHop(0, -1) });

    // Closed lanes, trivial and invalid requests
    {
        TArray<FSOLLaneEdge> closed = graph;
        closed[3].CloseS = -LANENET_INF;
        LaneNetRouteIs(*this, TEXT("e3 closed, 0 -> 3"), FindFastestRoute(5, closed, 0.0, w, 0.0, 0, 3), 40.0,
            TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(4, 1) });
        closed = graph;
        closed[5].CloseS = -LANENET_INF;
        TestFalse(TEXT("e5 closed, 0 -> 4: not found"), FindFastestRoute(5, closed, 0.0, w, 0.0, 0, 4).bFound);
        LaneNetRouteIs(*this, TEXT("source == target"), FindFastestRoute(5, graph, 0.0, w, 0.0, 2, 2), 0.0,
            TArray<FSOLLaneHop>{});
        TestFalse(TEXT("target out of range"), FindFastestRoute(5, graph, 0.0, w, 0.0, 0, 5).bFound);
        TestFalse(TEXT("source out of range"), FindFastestRoute(5, graph, 0.0, w, 0.0, INDEX_NONE, 3).bFound);
    }

    // Route length limit: a pure chain of 14 nodes reaches 12 hops, 13 hops is not found
    {
        TArray<FSOLLaneEdge> chain;
        for (int32 node = 0; node < 13; ++node)
        {
            chain.Add(LaneNetEdge(node, node + 1, 1.0));
        }
        const FSOLLaneRoute twelve = FindFastestRoute(14, chain, 0.0, w, 0.0, 0, 12);
        TestTrue(TEXT("chain 0 -> 12: found"), twelve.bFound);
        TestEqual(TEXT("chain 0 -> 12: hops"), twelve.HopCount, SOL::NPC_LANE_ROUTE_MAX_HOPS);
        TestFalse(TEXT("chain 0 -> 13: over the hop limit"), FindFastestRoute(14, chain, 0.0, w, 0.0, 0, 13).bFound);
    }

    // Tie at equal time: fewer hops
    LaneNetRouteIs(*this, TEXT("tie, fewer hops"), FindFastestRoute(3,
        TArray<FSOLLaneEdge>{ LaneNetEdge(0, 1, 5.0), LaneNetEdge(1, 2, 5.0), LaneNetEdge(0, 2, 10.0) }, 0.0, w, 0.0,
        0, 2), 10.0, TArray<FSOLLaneHop>{ LaneNetHop(2, 1) });

    // Tie at equal time and hops: the lower lane sequence, decided when two diamond paths reach the same (lane,
    // direction) state on the shared last lane; in the second order the winner's middle lane is the higher one, so
    // the first differing position (not the last) decides
    LaneNetRouteIs(*this, TEXT("lexicographic tie, order 1"), FindFastestRoute(5,
        TArray<FSOLLaneEdge>{ LaneNetEdge(0, 1, 5.0), LaneNetEdge(0, 2, 5.0), LaneNetEdge(1, 3, 5.0),
            LaneNetEdge(2, 3, 5.0), LaneNetEdge(3, 4, 1.0) }, 0.0, w, 0.0, 0, 4), 11.0,
        TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(2, 1), LaneNetHop(4, 1) });
    LaneNetRouteIs(*this, TEXT("lexicographic tie, order 2"), FindFastestRoute(5,
        TArray<FSOLLaneEdge>{ LaneNetEdge(0, 1, 5.0), LaneNetEdge(0, 2, 5.0), LaneNetEdge(2, 3, 5.0),
            LaneNetEdge(1, 3, 5.0), LaneNetEdge(3, 4, 1.0) }, 0.0, w, 0.0, 0, 4), 11.0,
        TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(3, 1), LaneNetHop(4, 1) });
    // Unequal legs: the higher sequence (e1, e3) settles at node 3 first, so the lower one must replace it on e4
    LaneNetRouteIs(*this, TEXT("lexicographic tie, higher sequence settles first"), FindFastestRoute(5,
        TArray<FSOLLaneEdge>{ LaneNetEdge(0, 1, 6.0), LaneNetEdge(0, 2, 4.0), LaneNetEdge(1, 3, 4.0),
            LaneNetEdge(2, 3, 6.0), LaneNetEdge(3, 4, 1.0) }, 0.0, w, 0.0, 0, 4), 11.0,
        TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(2, 1), LaneNetHop(4, 1) });

    // Zero, negative and non-finite edge times are never taken
    {
        TArray<FSOLLaneEdge> timed = { LaneNetEdge(0, 1, 0.0), LaneNetEdge(0, 1, 7.0) };
        LaneNetRouteIs(*this, TEXT("zero-time edge rejected"), FindFastestRoute(2, timed, 0.0, w, 0.0, 0, 1), 7.0,
            TArray<FSOLLaneHop>{ LaneNetHop(1, 1) });
        timed[0].TimeS = -3.0;
        LaneNetRouteIs(*this, TEXT("negative-time edge rejected"), FindFastestRoute(2, timed, 0.0, w, 0.0, 0, 1),
            7.0, TArray<FSOLLaneHop>{ LaneNetHop(1, 1) });
        timed[0].TimeS = LANENET_NAN;
        LaneNetRouteIs(*this, TEXT("NaN-time edge rejected"), FindFastestRoute(2, timed, 0.0, w, 0.0, 0, 1), 7.0,
            TArray<FSOLLaneHop>{ LaneNetHop(1, 1) });
        timed[1].TimeS = LANENET_INF;
        const FSOLLaneRoute none = FindFastestRoute(2, timed, 0.0, w, 0.0, 0, 1);
        TestFalse(TEXT("only bad edge times: not found"), none.bFound);
        LaneNetEnumIs(*this, TEXT("only bad edge times: unreachable"), none.Status, ESOLLaneRouteStatus::Unreachable);
    }

    // Status: the edge cap, unreachable, the hop limit and bad endpoints are told apart
    {
        TArray<FSOLLaneEdge> many;
        for (int32 lane = 0; lane < SOL::NPC_GROUP_TEXTURE_WIDTH + 1; ++lane)
        {
            many.Add(LaneNetEdge(0, 1, 1.0 + lane));
        }
        const FSOLLaneRoute capped = FindFastestRoute(2, many, 0.0, w, 0.0, 0, 1);
        TestFalse(TEXT("65 edges: not found"), capped.bFound);
        LaneNetEnumIs(*this, TEXT("65 edges: too many edges"), capped.Status, ESOLLaneRouteStatus::TooManyEdges);
        many.SetNum(SOL::NPC_GROUP_TEXTURE_WIDTH);
        const FSOLLaneRoute atCap = FindFastestRoute(2, many, 0.0, w, 0.0, 0, 1);
        LaneNetRouteIs(*this, TEXT("64 edges: routed"), atCap, 1.0, TArray<FSOLLaneHop>{ LaneNetHop(0, 1) });
        LaneNetEnumIs(*this, TEXT("64 edges: found"), atCap.Status, ESOLLaneRouteStatus::Found);

        TArray<FSOLLaneEdge> closed = graph;
        closed[5].CloseS = -LANENET_INF;
        LaneNetEnumIs(*this, TEXT("e5 closed: unreachable"), FindFastestRoute(5, closed, 0.0, w, 0.0, 0, 4).Status,
            ESOLLaneRouteStatus::Unreachable);
        LaneNetEnumIs(*this, TEXT("target out of range: invalid endpoint"),
            FindFastestRoute(5, graph, 0.0, w, 0.0, 0, 5).Status, ESOLLaneRouteStatus::InvalidEndpoint);
        LaneNetEnumIs(*this, TEXT("source == target: found"), FindFastestRoute(5, graph, 0.0, w, 0.0, 2, 2).Status,
            ESOLLaneRouteStatus::Found);

        TArray<FSOLLaneEdge> chain;
        for (int32 node = 0; node < 13; ++node)
        {
            chain.Add(LaneNetEdge(node, node + 1, 1.0));
        }
        LaneNetEnumIs(*this, TEXT("13 hops: too many hops"), FindFastestRoute(14, chain, 0.0, w, 0.0, 0, 13).Status,
            ESOLLaneRouteStatus::TooManyHops);
    }

    // Time windows: a hop is usable only if its window covers [t, t + T_edge]
    {
        TArray<FSOLLaneEdge> windowed = graph;
        windowed[3].CloseS = 24.0;
        LaneNetRouteIs(*this, TEXT("e3 closes at 24"), FindFastestRoute(5, windowed, 0.0, w, 0.0, 0, 3), 40.0,
            TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(4, 1) });
        windowed[3].CloseS = 25.0;
        LaneNetRouteIs(*this, TEXT("e3 closes at 25"), FindFastestRoute(5, windowed, 0.0, w, 0.0, 0, 3), 25.0,
            TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(1, 1), LaneNetHop(3, 1) });
        windowed = graph;
        windowed[3].OpenStartS = 21.0;
        LaneNetRouteIs(*this, TEXT("e3 opens at 21"), FindFastestRoute(5, windowed, 0.0, w, 0.0, 0, 3), 30.0,
            TArray<FSOLLaneHop>{ LaneNetHop(2, 1), LaneNetHop(3, 1) });
    }

    // Lane-pair transfer: the arc depends on which lane is taken at node 1
    {
        const FVector3d px(1.0, 0.0, 0.0);
        TArray<FSOLLaneEdge> pairGraph = { LaneNetEdge(0, 1, 10.0, px, 0.0, 2.0e7),
            LaneNetEdge(1, 2, 10.0, px, 2.0e7, 0.0), LaneNetEdge(1, 2, 10.0, -px, 2.0e7, 0.0) };
        LaneNetRouteIs(*this, TEXT("lane pair, same side"), FindFastestRoute(3, pairGraph, 0.0, w, 0.0, 0, 2), 20.0,
            TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(2, 1) });
        pairGraph[2].CloseS = -LANENET_INF;
        LaneNetRouteIs(*this, TEXT("lane pair, e2 closed"), FindFastestRoute(3, pairGraph, 0.0, w, 0.0, 0, 2),
            20.0 + 2.0e7 * UE_DOUBLE_PI / 2.0e4, TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(1, 1) });
    }

    // Real base network: Mercury 0, Venus 1, Earth 2, Moon 3, Mars 4, Jupiter 5, Saturn 6, Uranus 7, Neptune 8,
    // with the SDD 53 6.1 table times as inputs
    {
        const double d = LANENET_DAY_S;
        const TArray<FSOLLaneEdge> base = { LaneNetEdge(0, 1, 7.4 * d), LaneNetEdge(1, 2, 10.7 * d),
            LaneNetEdge(2, 3, 2.5 * 3600.0), LaneNetEdge(2, 4, 16.2 * d), LaneNetEdge(4, 5, 42.7 * d),
            LaneNetEdge(5, 6, 92.7 * d), LaneNetEdge(6, 7, 179.9 * d), LaneNetEdge(7, 8, 300.8 * d) };
        LaneNetRouteIs(*this, TEXT("Earth -> Saturn"), FindFastestRoute(9, base, 0.0, w, 0.0, 2, 6),
            base[3].TimeS + base[4].TimeS + base[5].TimeS,
            TArray<FSOLLaneHop>{ LaneNetHop(3, 1), LaneNetHop(4, 1), LaneNetHop(5, 1) });
        LaneNetRouteIs(*this, TEXT("Mercury -> Mars"), FindFastestRoute(9, base, 0.0, w, 0.0, 0, 4),
            base[0].TimeS + base[1].TimeS + base[3].TimeS,
            TArray<FSOLLaneHop>{ LaneNetHop(0, 1), LaneNetHop(1, 1), LaneNetHop(3, 1) });
        LaneNetRouteIs(*this, TEXT("Earth -> Moon"), FindFastestRoute(9, base, 0.0, w, 0.0, 2, 3), base[2].TimeS,
            TArray<FSOLLaneHop>{ LaneNetHop(2, 1) });
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkPixelClassesTest, "SOLTest.LaneNetwork.PixelClasses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 10: the tunnel's largest on-screen radius and its pixel class with hysteresis
bool FSOLLaneNetworkPixelClassesTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::LaneTunnelMaxRadiusPx;
    using SOLLaneNetwork::StepTunnelClass;
    LaneNetNear(*this, TEXT("radius 0.5 px"), LaneTunnelMaxRadiusPx(1.92e10, 1.0e7, 960.0), 0.5, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("radius 1 px"), LaneTunnelMaxRadiusPx(9.6e9, 1.0e7, 960.0), 1.0, LANENET_TIGHT);
    LaneNetExact(*this, TEXT("d 0"), LaneTunnelMaxRadiusPx(0.0, 1.0e7, 960.0), LANENET_INF);
    LaneNetExact(*this, TEXT("d negative"), LaneTunnelMaxRadiusPx(-5.0, 1.0e7, 960.0), LANENET_INF);
    LaneNetExact(*this, TEXT("NaN d"), LaneTunnelMaxRadiusPx(LANENET_NAN, 1.0e7, 960.0), 0.0);
    LaneNetExact(*this, TEXT("NaN radius"), LaneTunnelMaxRadiusPx(1.0e9, LANENET_NAN, 960.0), 0.0);
    LaneNetExact(*this, TEXT("radius 0"), LaneTunnelMaxRadiusPx(1.0e9, 0.0, 960.0), 0.0);
    LaneNetExact(*this, TEXT("focal 0"), LaneTunnelMaxRadiusPx(1.0e9, 1.0e7, 0.0), 0.0);
    LaneNetExact(*this, TEXT("NaN focal"), LaneTunnelMaxRadiusPx(1.0e9, 1.0e7, LANENET_NAN), 0.0);

    // StepTunnelClass rows (thresholds 0.5 / 0.4 / 10/12 / 0.75; Full on where the dots are alpha 0, 53d review)
    const ESOLTunnelClass off = ESOLTunnelClass::Off;
    const ESOLTunnelClass reduced = ESOLTunnelClass::Reduced;
    const ESOLTunnelClass full = ESOLTunnelClass::Full;
    LaneNetEnumIs(*this, TEXT("(Off, 0.49)"), StepTunnelClass(off, 0.49, 1.0), off);
    LaneNetEnumIs(*this, TEXT("(Off, 0.5)"), StepTunnelClass(off, 0.5, 1.0), reduced);
    LaneNetEnumIs(*this, TEXT("(Off, 1.0)"), StepTunnelClass(off, 1.0, 1.0), full);
    LaneNetEnumIs(*this, TEXT("(Off, 0.83)"), StepTunnelClass(off, 0.83, 1.0), reduced);
    LaneNetEnumIs(*this, TEXT("(Off, 0.834)"), StepTunnelClass(off, 0.834, 1.0), full);
    LaneNetEnumIs(*this, TEXT("(Reduced, 0.45)"), StepTunnelClass(reduced, 0.45, 1.0), reduced);
    LaneNetEnumIs(*this, TEXT("(Reduced, 0.39)"), StepTunnelClass(reduced, 0.39, 1.0), off);
    LaneNetEnumIs(*this, TEXT("(Reduced, 0.83)"), StepTunnelClass(reduced, 0.83, 1.0), reduced);
    LaneNetEnumIs(*this, TEXT("(Reduced, 0.834)"), StepTunnelClass(reduced, 0.834, 1.0), full);
    LaneNetEnumIs(*this, TEXT("(Full, 0.76)"), StepTunnelClass(full, 0.76, 1.0), full);
    LaneNetEnumIs(*this, TEXT("(Full, 0.74)"), StepTunnelClass(full, 0.74, 1.0), reduced);
    LaneNetEnumIs(*this, TEXT("(Full, 0.39)"), StepTunnelClass(full, 0.39, 1.0), off);
    LaneNetEnumIs(*this, TEXT("(Off, NaN)"), StepTunnelClass(off, LANENET_NAN, 1.0), off);
    LaneNetEnumIs(*this, TEXT("(Reduced, NaN)"), StepTunnelClass(reduced, LANENET_NAN, 1.0), off);
    LaneNetEnumIs(*this, TEXT("(Full, NaN)"), StepTunnelClass(full, LANENET_NAN, 1.0), off);

    // Quality scale: the flame vanish size V over V at Epic, presets clamped to Low .. Cinematic
    using SOLLaneNetwork::LaneTunnelQualityScale;
    const double expectedScale[] = { 10.0, 4.6, 2.2, 1.0, 1.0 };
    for (int32 preset = 0; preset < 5; ++preset)
    {
        LaneNetNear(*this, FString::Printf(TEXT("quality scale preset %d"), preset), LaneTunnelQualityScale(preset),
            expectedScale[preset], LANENET_TIGHT);
    }
    LaneNetNear(*this, TEXT("quality scale preset -1 clamps to Low"), LaneTunnelQualityScale(-1), 10.0, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("quality scale preset 7 clamps to Cinematic"), LaneTunnelQualityScale(7), 1.0,
        LANENET_TIGHT);

    // Scaled thresholds (off-boundary values): Low x10 (5 / 4 / 8.33 / 7.5), Medium x4.6 (2.3 / 1.84 / 3.83 / 3.45),
    // High x2.2 (1.1 / 0.88 / 1.83 / 1.65)
    const double low = LaneTunnelQualityScale(0);
    const double medium = LaneTunnelQualityScale(1);
    const double high = LaneTunnelQualityScale(2);
    LaneNetEnumIs(*this, TEXT("Low (Off, 4.99)"), StepTunnelClass(off, 4.99, low), off);
    LaneNetEnumIs(*this, TEXT("Low (Off, 5.0)"), StepTunnelClass(off, 5.0, low), reduced);
    LaneNetEnumIs(*this, TEXT("Low (Off, 10.0)"), StepTunnelClass(off, 10.0, low), full);
    LaneNetEnumIs(*this, TEXT("Low (Reduced, 3.99)"), StepTunnelClass(reduced, 3.99, low), off);
    LaneNetEnumIs(*this, TEXT("Low (Full, 7.51)"), StepTunnelClass(full, 7.51, low), full);
    LaneNetEnumIs(*this, TEXT("Low (Off, 8.34)"), StepTunnelClass(off, 8.34, low), full);
    LaneNetEnumIs(*this, TEXT("Low (Full, 7.49)"), StepTunnelClass(full, 7.49, low), reduced);
    LaneNetEnumIs(*this, TEXT("Low (Off, 0.5): off at Low"), StepTunnelClass(off, 0.5, low), off);
    LaneNetEnumIs(*this, TEXT("Medium (Off, 2.29)"), StepTunnelClass(off, 2.29, medium), off);
    LaneNetEnumIs(*this, TEXT("Medium (Off, 2.31)"), StepTunnelClass(off, 2.31, medium), reduced);
    LaneNetEnumIs(*this, TEXT("Medium (Off, 3.82)"), StepTunnelClass(off, 3.82, medium), reduced);
    LaneNetEnumIs(*this, TEXT("Medium (Off, 3.84)"), StepTunnelClass(off, 3.84, medium), full);
    LaneNetEnumIs(*this, TEXT("Medium (Reduced, 1.85)"), StepTunnelClass(reduced, 1.85, medium), reduced);
    LaneNetEnumIs(*this, TEXT("Medium (Reduced, 1.83)"), StepTunnelClass(reduced, 1.83, medium), off);
    LaneNetEnumIs(*this, TEXT("Medium (Full, 3.46)"), StepTunnelClass(full, 3.46, medium), full);
    LaneNetEnumIs(*this, TEXT("Medium (Full, 3.44)"), StepTunnelClass(full, 3.44, medium), reduced);
    LaneNetEnumIs(*this, TEXT("High (Off, 1.09)"), StepTunnelClass(off, 1.09, high), off);
    LaneNetEnumIs(*this, TEXT("High (Off, 1.11)"), StepTunnelClass(off, 1.11, high), reduced);
    LaneNetEnumIs(*this, TEXT("High (Reduced, 1.84)"), StepTunnelClass(reduced, 1.84, high), full);
    LaneNetEnumIs(*this, TEXT("High (Reduced, 0.87)"), StepTunnelClass(reduced, 0.87, high), off);
    LaneNetEnumIs(*this, TEXT("High (Full, 1.66)"), StepTunnelClass(full, 1.66, high), full);
    LaneNetEnumIs(*this, TEXT("High (Full, 1.64)"), StepTunnelClass(full, 1.64, high), reduced);

    // A NaN, infinite or non-positive scale reads as 1
    LaneNetEnumIs(*this, TEXT("scale NaN (Off, 0.5)"), StepTunnelClass(off, 0.5, LANENET_NAN), reduced);
    LaneNetEnumIs(*this, TEXT("scale inf (Off, 0.5)"), StepTunnelClass(off, 0.5, LANENET_INF), reduced);
    LaneNetEnumIs(*this, TEXT("scale 0 (Off, 0.5)"), StepTunnelClass(off, 0.5, 0.0), reduced);
    LaneNetEnumIs(*this, TEXT("scale -2 (Off, 0.49)"), StepTunnelClass(off, 0.49, -2.0), off);
    LaneNetEnumIs(*this, TEXT("scale NaN (Full, 0.84)"), StepTunnelClass(full, 0.84, LANENET_NAN), full);

    // Invisible switches: on where the radius fade is gone, Full on where the detail gap fade is gone
    LaneNetExact(*this, TEXT("activate == radius fade gone"), SOL::NPC_TUNNEL_ACTIVATE_PX,
        SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX);
    LaneNetNear(*this, TEXT("full on x ring spacing == detail gap fade gone"),
        SOL::NPC_TUNNEL_FULL_ON_PX * SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII, SOL::NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX,
        LANENET_TIGHT);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkPoolTest, "SOLTest.LaneNetwork.Pool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 11: the tunnel instance pool (largest first, a fixed pool reported on overflow) and the overflow report
bool FSOLLaneNetworkPoolTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::SelectTunnelPool;
    const TArray<double> px = { 3.0, 0.2, 7.0, 1.0 };
    const TArray<uint8> drawn = { 1, 0, 1, 1 };
    {
        TArray<int32> lanes = { 99 };
        TestEqual(TEXT("pool 2: overflow"), SelectTunnelPool(px, drawn, 2, lanes), 1);
        LaneNetIntsMatch(*this, TEXT("pool 2"), lanes, TArray<int32>{ 2, 0 });
        TestEqual(TEXT("pool 24: overflow"), SelectTunnelPool(px, drawn, 24, lanes), 0);
        LaneNetIntsMatch(*this, TEXT("pool 24"), lanes, TArray<int32>{ 2, 0, 3 });
        TestEqual(TEXT("pool 0: overflow = drawn count"), SelectTunnelPool(px, drawn, 0, lanes), 3);
        LaneNetIntsMatch(*this, TEXT("pool 0"), lanes, TArray<int32>{});
        TestEqual(TEXT("pool -1: overflow = drawn count"), SelectTunnelPool(px, drawn, -1, lanes), 3);
        LaneNetIntsMatch(*this, TEXT("pool -1"), lanes, TArray<int32>{});
    }

    // Ties go to the lower lane index
    {
        TArray<int32> lanes;
        TestEqual(TEXT("tie: overflow"), SelectTunnelPool(TArray<double>{ 2.0, 5.0, 2.0, 2.0 },
            TArray<uint8>{ 1, 1, 1, 1 }, 3, lanes), 1);
        LaneNetIntsMatch(*this, TEXT("tie"), lanes, TArray<int32>{ 1, 0, 2 });
    }

    // Pool 24 overflow: 30 drawn lanes with px 30 - index
    {
        TArray<double> manyPx;
        TArray<uint8> manyDrawn;
        TArray<int32> expected;
        for (int32 lane = 0; lane < 30; ++lane)
        {
            manyPx.Add(30.0 - lane);
            manyDrawn.Add(1);
            if (lane < SOL::NPC_TUNNEL_INSTANCE_POOL)
            {
                expected.Add(lane);
            }
        }
        TArray<int32> lanes;
        TestEqual(TEXT("30 drawn: overflow"), SelectTunnelPool(manyPx, manyDrawn, SOL::NPC_TUNNEL_INSTANCE_POOL,
            lanes), 6);
        LaneNetIntsMatch(*this, TEXT("30 drawn"), lanes, expected);
    }

    // StepOverflowReport: true iff the overflow changed
    using SOLLaneNetwork::StepOverflowReport;
    TestFalse(TEXT("report (0, 0)"), StepOverflowReport(0, 0));
    TestTrue(TEXT("report (0, 6)"), StepOverflowReport(0, 6));
    TestFalse(TEXT("report (6, 6)"), StepOverflowReport(6, 6));
    TestTrue(TEXT("report (6, 2)"), StepOverflowReport(6, 2));
    TestTrue(TEXT("report (2, 0)"), StepOverflowReport(2, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkTrafficWeightsTest, "SOLTest.LaneNetwork.TrafficWeights",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 12: the lane weight sqrt(tierA x tierB) and the largest-remainder ship shares
bool FSOLLaneNetworkTrafficWeightsTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::LaneWeight;
    LaneNetNear(*this, TEXT("weight (10, 10)"), LaneWeight(10.0, 10.0), 10.0, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("weight (10, 6)"), LaneWeight(10.0, 6.0), FMath::Sqrt(60.0), LANENET_TIGHT);
    LaneNetNear(*this, TEXT("weight (10, 6) value"), LaneWeight(10.0, 6.0), 7.745966692, 1.0e-9);
    LaneNetNear(*this, TEXT("weight (4, 3)"), LaneWeight(4.0, 3.0), FMath::Sqrt(12.0), LANENET_TIGHT);
    LaneNetNear(*this, TEXT("weight (3, 3)"), LaneWeight(3.0, 3.0), 3.0, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("weight (2, 1)"), LaneWeight(2.0, 1.0), FMath::Sqrt(2.0), LANENET_TIGHT);
    LaneNetNear(*this, TEXT("weight (1, 1)"), LaneWeight(1.0, 1.0), 1.0, LANENET_TIGHT);
    LaneNetExact(*this, TEXT("weight (0, 5)"), LaneWeight(0.0, 5.0), 0.0);
    LaneNetExact(*this, TEXT("weight negative tier"), LaneWeight(-4.0, -3.0), 0.0);
    LaneNetExact(*this, TEXT("weight one negative tier"), LaneWeight(-4.0, 3.0), 0.0);
    LaneNetExact(*this, TEXT("weight NaN tier"), LaneWeight(LANENET_NAN, 3.0), 0.0);
    LaneNetExact(*this, TEXT("weight symmetric"), LaneWeight(6.0, 10.0), LaneWeight(10.0, 6.0));

    // LaneShipShares rows
    using SOLLaneNetwork::LaneShipShares;
    TArray<int32> counts = { 99 };
    LaneShipShares(400000, TArray<double>{ 2.0, 1.0, 1.0 }, counts);
    LaneNetIntsMatch(*this, TEXT("400,000 over {2, 1, 1}"), counts, TArray<int32>{ 200000, 100000, 100000 });
    LaneShipShares(10, TArray<double>{ 1.0, 1.0, 1.0 }, counts);
    LaneNetIntsMatch(*this, TEXT("10 over {1, 1, 1}"), counts, TArray<int32>{ 4, 3, 3 });
    LaneShipShares(10, TArray<double>{ 1.0, -1.0, LANENET_NAN, 0.0, 1.0 }, counts);
    LaneNetIntsMatch(*this, TEXT("bad weights count 0"), counts, TArray<int32>{ 5, 0, 0, 0, 5 });
    LaneShipShares(10, TArray<double>{ 0.0, 0.0 }, counts);
    LaneNetIntsMatch(*this, TEXT("all zero"), counts, TArray<int32>{ 0, 0 });

    // Population weights of the base lanes, as ships and as blocks of 100
    const TArray<double> weights = LaneNetBaseWeights();
    LaneShipShares(400000, weights, counts);
    LaneNetIntsMatch(*this, TEXT("base lanes, 400,000"), counts,
        TArray<int32>{ 24925, 55734, 124624, 96534, 43171, 24925, 17625, 12462 });
    LaneShipShares(400000 / SOL::NPC_LANE_SHIP_BLOCK_SIZE, weights, counts);
    LaneNetIntsMatch(*this, TEXT("base lanes, 4,000 blocks"), counts,
        TArray<int32>{ 249, 558, 1246, 965, 432, 249, 176, 125 });

    // Budget unchanged by the lane count
    for (const int32 laneCount : { 8, 22, 34, 38 })
    {
        TArray<double> many;
        for (int32 lane = 0; lane < laneCount; ++lane)
        {
            many.Add(0.5 + ((lane * 37) % 11) * 0.3);
        }
        LaneShipShares(400000, many, counts);
        int64 sum = 0;
        for (const int32 count : counts)
        {
            sum += count;
        }
        TestEqual(FString::Printf(TEXT("%d lanes: one count per lane"), laneCount), counts.Num(), laneCount);
        TestEqual(FString::Printf(TEXT("%d lanes: sum"), laneCount), sum, int64(400000));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkNodesTest, "SOLTest.LaneNetwork.Nodes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13: dwarf-planet and asteroid nodes act like planets, a Moon never picks; nominal length and long lanes
bool FSOLLaneNetworkNodesTest::RunTest(const FString& /*parameters*/)
{
    // Kinds in the partner rule: dwarf 0, asteroid 1, Moon 2 (never picked), planet 3
    {
        const TArray<FSOLLaneNode> nodes = { LaneNetNode(ESOLLaneNodeKind::DwarfPlanet),
            LaneNetNode(ESOLLaneNodeKind::Asteroid), LaneNetNode(ESOLLaneNodeKind::Moon),
            LaneNetNode(ESOLLaneNodeKind::Planet) };
        const TArray<FSOLLanePair> noBase;
        TArray<FSOLLanePair> pairs;
        SOLLaneNetwork::NextClosestPairs(nodes, LaneNetXs({ 1.0, 2.0, 1.5, 4.0 }), noBase, pairs);
        LaneNetPairsMatch(*this, TEXT("dwarf and asteroid pick like planets"), pairs,
            TArray<FSOLLanePair>{ LaneNetPair(0, 1), LaneNetPair(1, 3) });
        LaneNetNear(*this, TEXT("Ceres-asteroid weight"), SOLLaneNetwork::LaneWeight(4.0, 3.0), FMath::Sqrt(12.0),
            LANENET_TIGHT);
    }

    // Nominal length: Earth 0, Moon 1 (parent Earth), Neptune 2, Eris 3, Makemake 4
    TArray<FSOLLaneNode> nodes = { LaneNetNode(ESOLLaneNodeKind::Planet), LaneNetNode(ESOLLaneNodeKind::Moon),
        LaneNetNode(ESOLLaneNodeKind::Planet), LaneNetNode(ESOLLaneNodeKind::DwarfPlanet),
        LaneNetNode(ESOLLaneNodeKind::DwarfPlanet) };
    nodes[0].AphelionM = 1.521e11;
    nodes[1].AphelionM = 4.055e8;
    nodes[1].ParentNode = 0;
    nodes[2].AphelionM = 30.33 * SOL::AU_M;
    nodes[3].AphelionM = 97.58 * SOL::AU_M;
    nodes[4].AphelionM = 52.80 * SOL::AU_M;
    using SOLLaneNetwork::LaneNominalLengthM;
    LaneNetNear(*this, TEXT("Earth-Moon"), LaneNominalLengthM(nodes, 0, 1), 4.055e8, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("Moon-Earth"), LaneNominalLengthM(nodes, 1, 0), 4.055e8, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("Neptune-Eris"), LaneNominalLengthM(nodes, 2, 3), 30.33 * SOL::AU_M + 97.58 * SOL::AU_M,
        LANENET_TIGHT);
    LaneNetNear(*this, TEXT("Eris-Makemake"), LaneNominalLengthM(nodes, 3, 4), 97.58 * SOL::AU_M + 52.80 * SOL::AU_M,
        LANENET_TIGHT);
    LaneNetExact(*this, TEXT("Moon to a non-parent"), LaneNominalLengthM(nodes, 1, 2), 0.0);
    LaneNetExact(*this, TEXT("index out of range"), LaneNominalLengthM(nodes, 0, 5), 0.0);
    LaneNetExact(*this, TEXT("index negative"), LaneNominalLengthM(nodes, INDEX_NONE, 2), 0.0);
    {
        TArray<FSOLLaneNode> bad = nodes;
        bad[2].AphelionM = -1.0;
        LaneNetExact(*this, TEXT("negative aphelion"), LaneNominalLengthM(bad, 2, 3), 0.0);
        bad[2].AphelionM = LANENET_NAN;
        LaneNetExact(*this, TEXT("NaN aphelion"), LaneNominalLengthM(bad, 2, 3), 0.0);
        bad[2].AphelionM = LANENET_INF;
        LaneNetExact(*this, TEXT("infinite aphelion"), LaneNominalLengthM(bad, 2, 3), 0.0);
    }

    // Long lanes: 150 AU is finite, positive and 880-900 days; the look-ahead covers the longest possible pair
    {
        const FSOLLaneProfile longLane = SOLLaneTraffic::MakeLaneProfile(1.0e7, 150.0 * SOL::AU_M, 2.4622e7, 1.163e6);
        TestTrue(TEXT("precondition: 150 AU profile drivable"), longLane.bDrivable);
        const double edgeS = SOLLaneNetwork::LaneEdgeTimeS(longLane);
        TestTrue(TEXT("150 AU: finite and positive"), FMath::IsFinite(edgeS) && edgeS > 0.0);
        TestTrue(FString::Printf(TEXT("150 AU: 880-900 days (got %.3f)"), edgeS / LANENET_DAY_S),
            edgeS >= 880.0 * LANENET_DAY_S && edgeS <= 900.0 * LANENET_DAY_S);
        TestTrue(TEXT("horizon covers 897 days"),
            SOL::NPC_LANE_NETWORK_HORIZON_EPOCHS * SOL::NPC_LANE_NETWORK_EPOCH_S > 897.0 * LANENET_DAY_S);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkOverflowReportTest, "SOLTest.LaneNetwork.OverflowReport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14: the group-column overflow (38 slots, 31 lanes) is reported once when it appears and once when it clears
bool FSOLLaneNetworkOverflowReportTest::RunTest(const FString& /*parameters*/)
{
    // 64 columns - 10 clusters - 16 battles = 38 slots: 8 base + 30 partner and island-link lanes
    const int32 capacity = SOL::NPC_GROUP_TEXTURE_WIDTH - 10 - 16;
    TestEqual(TEXT("capacity"), capacity, 38);
    TArray<FSOLLanePair> previous = LaneNetBaseSlots();
    while (previous.Num() < capacity)
    {
        previous.Add(LaneNetFree());
    }
    TArray<uint8> held;
    held.Init(0, capacity);
    TArray<FSOLLanePair> lanes31;
    for (int32 lane = 0; lane < 31; ++lane)
    {
        lanes31.Add(LaneNetPair(lane, 50 + lane));
    }

    // 31 lanes into 30 free slots: one overflow, reported
    TArray<FSOLLanePair> slots;
    const int32 overflow1 = SOLLaneNetwork::AssignLaneSlots(previous, held, lanes31, SOL::NPC_LANE_BASE_COUNT,
        capacity, slots);
    TestEqual(TEXT("31 lanes: overflow"), overflow1, 1);
    TestEqual(TEXT("31 lanes: slot count"), slots.Num(), capacity);
    TestTrue(TEXT("report (0, 1)"), SOLLaneNetwork::StepOverflowReport(0, overflow1));

    // The next epoch with 30 lanes (the placed ones keep their slots): cleared, reported
    TArray<FSOLLanePair> lanes30 = lanes31;
    lanes30.SetNum(30);
    TArray<FSOLLanePair> next;
    const int32 overflow2 = SOLLaneNetwork::AssignLaneSlots(slots, held, lanes30, SOL::NPC_LANE_BASE_COUNT, capacity,
        next);
    TestEqual(TEXT("30 lanes: overflow"), overflow2, 0);
    TestTrue(TEXT("report (1, 0)"), SOLLaneNetwork::StepOverflowReport(overflow1, overflow2));
    LaneNetPairsMatch(*this, TEXT("30 lanes keep their slots"), next, slots);

    // The tunnel pool as in case 11: 24 of 30
    TArray<double> px;
    TArray<uint8> drawn;
    for (int32 lane = 0; lane < 30; ++lane)
    {
        px.Add(30.0 - lane);
        drawn.Add(1);
    }
    TArray<int32> pool;
    TestEqual(TEXT("tunnel pool overflow"), SOLLaneNetwork::SelectTunnelPool(px, drawn, SOL::NPC_TUNNEL_INSTANCE_POOL,
        pool), 6);
    TestEqual(TEXT("tunnel pool size"), pool.Num(), SOL::NPC_TUNNEL_INSTANCE_POOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkSunSafetyTest, "SOLTest.LaneNetwork.SunSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 15: the Sun zone test, the player override (NPCs never override) and the safe arrival point
bool FSOLLaneNetworkSunSafetyTest::RunTest(const FString& /*parameters*/)
{
    // The literals match the constants
    LaneNetNear(*this, TEXT("zone literal"), SOL::NPC_LANE_SUN_CLEARANCE_RADII * SOL::SUN_RADIUS_M,
        LANENET_SUN_ZONE_M, LANENET_TIGHT);
    LaneNetNear(*this, TEXT("edge literal"), SOL::NPC_LANE_SUN_REOPEN_RADII * SOL::SUN_RADIUS_M, LANENET_SUN_EDGE_M,
        LANENET_TIGHT);

    // PathEntersSunZone rows
    using SOLLaneNetwork::PathEntersSunZone;
    TestTrue(TEXT("path at 5e9"), PathEntersSunZone(FVector3d(1e11, 5e9, 0.0), FVector3d(-1e11, 5e9, 0.0),
        LANENET_SUN_ZONE_M));
    TestFalse(TEXT("path at 2e10"), PathEntersSunZone(FVector3d(1e11, 2e10, 0.0), FVector3d(-1e11, 2e10, 0.0),
        LANENET_SUN_ZONE_M));
    TestFalse(TEXT("path away"), PathEntersSunZone(FVector3d(1e11, 0.0, 0.0), FVector3d(2e11, 0.0, 0.0),
        LANENET_SUN_ZONE_M));
    TestFalse(TEXT("path at the zone radius"), PathEntersSunZone(FVector3d(1e11, 1.3914e10, 0.0),
        FVector3d(-1e11, 1.3914e10, 0.0), LANENET_SUN_ZONE_M));
    TestTrue(TEXT("path NaN from"), PathEntersSunZone(FVector3d(LANENET_NAN, 0.0, 0.0), FVector3d(2e11, 0.0, 0.0),
        LANENET_SUN_ZONE_M));
    TestTrue(TEXT("path NaN to"), PathEntersSunZone(FVector3d(1e11, 0.0, 0.0), FVector3d(2e11, LANENET_NAN, 0.0),
        LANENET_SUN_ZONE_M));
    TestTrue(TEXT("path NaN zone"), PathEntersSunZone(FVector3d(1e11, 0.0, 0.0), FVector3d(2e11, 0.0, 0.0),
        LANENET_NAN));

    // SunSafetyActive = bNpc || !bPlayerOverride
    using SOLLaneNetwork::SunSafetyActive;
    TestTrue(TEXT("safety (false, false)"), SunSafetyActive(false, false));
    TestFalse(TEXT("safety (true, false)"), SunSafetyActive(true, false));
    TestTrue(TEXT("safety (false, true)"), SunSafetyActive(false, true));
    TestTrue(TEXT("safety (true, true)"), SunSafetyActive(true, true));

    // SunSafeArrivalM rows
    using SOLLaneNetwork::SunSafeArrivalM;
    LaneNetVecNear(*this, TEXT("arrival, target inside"), SunSafeArrivalM(FVector3d(1e11, 0.0, 0.0),
        FVector3d(1e9, 0.0, 0.0), LANENET_SUN_ZONE_M, LANENET_SUN_EDGE_M), FVector3d(1.53054e10, 0.0, 0.0),
        LANENET_TIGHT);
    LaneNetVecNear(*this, TEXT("arrival, target at the Sun"), SunSafeArrivalM(FVector3d(0.0, 1e11, 0.0),
        FVector3d::ZeroVector, LANENET_SUN_ZONE_M, LANENET_SUN_EDGE_M), FVector3d(0.0, 1.53054e10, 0.0),
        LANENET_TIGHT);
    LaneNetVecNear(*this, TEXT("arrival, target outside"), SunSafeArrivalM(FVector3d(1e11, 0.0, 0.0),
        FVector3d(2e10, 0.0, 0.0), LANENET_SUN_ZONE_M, LANENET_SUN_EDGE_M), FVector3d(2e10, 0.0, 0.0), 0.0);
    LaneNetVecNear(*this, TEXT("arrival, ship at the origin"), SunSafeArrivalM(FVector3d::ZeroVector,
        FVector3d(1e9, 0.0, 0.0), LANENET_SUN_ZONE_M, LANENET_SUN_EDGE_M), FVector3d(0.0, 0.0, 1.53054e10),
        LANENET_TIGHT);
    LaneNetVecNear(*this, TEXT("arrival, ship not finite"), SunSafeArrivalM(FVector3d(LANENET_NAN, 0.0, 0.0),
        FVector3d(1e9, 0.0, 0.0), LANENET_SUN_ZONE_M, LANENET_SUN_EDGE_M), FVector3d(0.0, 0.0, 1.53054e10),
        LANENET_TIGHT);
    return true;
}

// Brute-force references of the partner picks and island links (the plain scans the scratch-based code replaced)
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns whether a node takes part (planet, dwarf planet, asteroid) and has a finite position
    bool LaneNetRefCandidate(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positions,
        const int32 node)
    {
        const ESOLLaneNodeKind kind = nodes[node].Kind;
        const bool bTakesPart = kind == ESOLLaneNodeKind::Planet || kind == ESOLLaneNodeKind::DwarfPlanet
            || kind == ESOLLaneNodeKind::Asteroid;
        const FVector3d& p = positions[node];
        return bTakesPart && FMath::IsFinite(p.X) && FMath::IsFinite(p.Y) && FMath::IsFinite(p.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a base lane joins a and b (either order), by a plain scan
    bool LaneNetRefBaseLinked(TConstArrayView<FSOLLanePair> base, const int32 a, const int32 b)
    {
        for (const FSOLLanePair& lane : base)
        {
            if ((lane.NodeA == a && lane.NodeB == b) || (lane.NodeA == b && lane.NodeB == a))
            {
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one node's nearest non-base-linked candidate (ties to the lower index) by a plain scan
    int32 LaneNetRefNearest(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positions,
        TConstArrayView<FSOLLanePair> base, const int32 node, double& outDistance)
    {
        outDistance = LANENET_NAN;
        if (!LaneNetRefCandidate(nodes, positions, node))
        {
            return INDEX_NONE;
        }
        int32 best = INDEX_NONE;
        double bestDistance = LANENET_INF;
        for (int32 other = 0; other < nodes.Num(); ++other)
        {
            if (other == node || !LaneNetRefCandidate(nodes, positions, other)
                || LaneNetRefBaseLinked(base, node, other))
            {
                continue;
            }
            const double distance = (positions[other] - positions[node]).Length();
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = other;
            }
        }
        outDistance = best != INDEX_NONE ? bestDistance : LANENET_NAN;
        return best;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the island links by the full pair scan each round (both ends candidates, ties to the lower pair)
    TArray<FSOLLanePair> LaneNetRefIslandLinks(TConstArrayView<FSOLLaneNode> nodes,
        TConstArrayView<FVector3d> positions, TConstArrayView<FSOLLanePair> lanes, const int32 root)
    {
        // Components by repeated relabelling (lowest label wins), then one link per round
        const int32 count = nodes.Num();
        TArray<int32> label;
        for (int32 node = 0; node < count; ++node)
        {
            label.Add(node);
        }
        // Merges the components holding a and b
        auto merge = [&label, count](const int32 a, const int32 b)
        {
            const int32 from = FMath::Max(label[a], label[b]);
            const int32 to = FMath::Min(label[a], label[b]);
            for (int32 node = 0; node < count; ++node)
            {
                label[node] = label[node] == from ? to : label[node];
            }
        };
        for (const FSOLLanePair& lane : lanes)
        {
            if (lane.NodeA >= 0 && lane.NodeA < count && lane.NodeB >= 0 && lane.NodeB < count)
            {
                merge(lane.NodeA, lane.NodeB);
            }
        }
        TArray<FSOLLanePair> links;
        for (;;)
        {
            FSOLLanePair best;
            double bestDistance = LANENET_INF;
            for (int32 u = 0; u < count; ++u)
            {
                if (!LaneNetRefCandidate(nodes, positions, u) || label[u] != label[root])
                {
                    continue;
                }
                for (int32 v = 0; v < count; ++v)
                {
                    if (!LaneNetRefCandidate(nodes, positions, v) || label[v] == label[root])
                    {
                        continue;
                    }
                    const double distance = (positions[v] - positions[u]).Length();
                    const FSOLLanePair pair = LaneNetPair(FMath::Min(u, v), FMath::Max(u, v));
                    const bool bLower = pair.NodeA != best.NodeA ? pair.NodeA < best.NodeA : pair.NodeB < best.NodeB;
                    if (distance < bestDistance || (distance == bestDistance && bLower))
                    {
                        bestDistance = distance;
                        best = pair;
                    }
                }
            }
            if (best.NodeA == INDEX_NONE)
            {
                break;
            }
            links.Add(best);
            merge(best.NodeA, best.NodeB);
        }
        links.Sort([](const FSOLLanePair& a, const FSOLLanePair& b)
            { return a.NodeA != b.NodeA ? a.NodeA < b.NodeA : a.NodeB < b.NodeB; });
        return links;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneNetworkScratchEquivalenceTest, "SOLTest.LaneNetwork.ScratchEquivalence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The masks, distance matrix and incremental island search give the plain scans' outputs on seeded random inputs
bool FSOLLaneNetworkScratchEquivalenceTest::RunTest(const FString& /*parameters*/)
{
    // Coarse grid positions make duplicate points and distance ties common; 70 nodes exercise the mask fallback
    FRandomStream random(5353);
    const ESOLLaneNodeKind kinds[] = { ESOLLaneNodeKind::Planet, ESOLLaneNodeKind::DwarfPlanet,
        ESOLLaneNodeKind::Moon, ESOLLaneNodeKind::Asteroid, ESOLLaneNodeKind::Station, ESOLLaneNodeKind::City };
    int32 mismatches = 0;
    for (int32 trial = 0; trial < 300; ++trial)
    {
        const int32 count = trial == 0 ? 70 : random.RandRange(2, 30);
        const int32 epochs = 3;
        TArray<FSOLLaneNode> nodes;
        TArray<FVector3d> series;
        for (int32 node = 0; node < count; ++node)
        {
            nodes.Add(LaneNetNode(kinds[random.RandRange(0, 5)]));
        }
        for (int32 index = 0; index < count * epochs; ++index)
        {
            const bool bNaN = random.RandRange(0, 40) == 0;
            series.Add(bNaN ? FVector3d(LANENET_NAN, 0.0, 0.0) : FVector3d(random.RandRange(-3, 3),
                random.RandRange(-3, 3), random.RandRange(-1, 1)) * LANENET_UNIT_M);
        }
        TArray<FSOLLanePair> base;
        for (int32 lane = random.RandRange(0, 8); lane > 0; --lane)
        {
            base.Add(LaneNetPair(random.RandRange(-1, count), random.RandRange(0, count - 1)));
        }
        const TArray<FVector3d> positions = LaneNetEpochSlice(series, epochs - 1, count);

        // Nearest partners against the plain scan
        TArray<int32> nearest;
        SOLLaneNetwork::NearestPartners(nodes, positions, base, nearest);
        TArray<int32> expectedNearest;
        for (int32 node = 0; node < count; ++node)
        {
            double distance = 0.0;
            expectedNearest.Add(LaneNetRefNearest(nodes, positions, base, node, distance));
        }
        mismatches += nearest == expectedNearest ? 0 : 1;

        // Replayed picks against StepPartnerPick driven by the plain scan
        TArray<FSOLPartnerPick> picks;
        SOLLaneNetwork::ReplayPartnerPicks(nodes, series, epochs, base, 2, 0.8, picks);
        TArray<FSOLPartnerPick> expectedPicks;
        expectedPicks.SetNum(count);
        for (int32 epoch = 0; epoch < epochs; ++epoch)
        {
            const TArray<FVector3d> slice = LaneNetEpochSlice(series, epoch, count);
            for (int32 node = 0; node < count; ++node)
            {
                const ESOLLaneNodeKind kind = nodes[node].Kind;
                if (kind == ESOLLaneNodeKind::Moon || kind == ESOLLaneNodeKind::Station
                    || kind == ESOLLaneNodeKind::City)
                {
                    continue;
                }
                double nearestDistance = 0.0;
                const int32 best = LaneNetRefNearest(nodes, slice, base, node, nearestDistance);
                const int32 held = expectedPicks[node].Partner;
                const bool bHeld = held >= 0 && LaneNetRefCandidate(nodes, slice, held)
                    && LaneNetRefCandidate(nodes, slice, node);
                const double heldDistance = bHeld ? (slice[held] - slice[node]).Length() : LANENET_NAN;
                expectedPicks[node] = SOLLaneNetwork::StepPartnerPick(expectedPicks[node], best, nearestDistance,
                    heldDistance, 2, 0.8);
            }
        }
        bool bPicksMatch = picks.Num() == count;
        for (int32 node = 0; bPicksMatch && node < count; ++node)
        {
            bPicksMatch = picks[node].Partner == expectedPicks[node].Partner
                && picks[node].StayEpochs == expectedPicks[node].StayEpochs;
        }
        mismatches += bPicksMatch ? 0 : 1;

        // Island links against the full pair scan, rooted at a random node
        const int32 root = random.RandRange(0, count - 1);
        TArray<FSOLLanePair> links;
        SOLLaneNetwork::IslandLinks(nodes, positions, base, root, links);
        const TArray<FSOLLanePair> expectedLinks = LaneNetRefIslandLinks(nodes, positions, base, root);
        bool bLinksMatch = links.Num() == expectedLinks.Num();
        for (int32 index = 0; bLinksMatch && index < links.Num(); ++index)
        {
            bLinksMatch = links[index].NodeA == expectedLinks[index].NodeA
                && links[index].NodeB == expectedLinks[index].NodeB;
        }
        mismatches += bLinksMatch ? 0 : 1;
        if (!bLinksMatch)
        {
            AddError(FString::Printf(TEXT("trial %d island links: got %s, expected %s"), trial,
                *LaneNetPairsText(links), *LaneNetPairsText(expectedLinks)));
        }
    }
    TestEqual(TEXT("scratch outputs equal the plain scans over 300 random trials"), mismatches, 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
