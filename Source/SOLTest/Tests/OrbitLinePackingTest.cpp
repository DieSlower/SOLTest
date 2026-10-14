/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "OrbitLines/SOLOrbitLineMesh.h"
#include "OrbitLines/SOLOrbitLineMath.h"

#include "SOLConstants.h"
#include "Universe/SOLKepler.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Written against the orbit-line packing and multi-scale dash contract (SDD 17, Amendment 1), not the implementation.
// Helpers here carry names unique across the test files so unity builds do not collide.
namespace
{
    // Reference values typed independently of production code
    constexpr double ORBITLINEPACKING_AU_M = 149597870700.0;
    constexpr float ORBITLINEPACKING_SENTINEL = -12345.5f;
    constexpr double ORBITLINEPACKING_MIN_PERIOD_PX = 24.0;
    constexpr int32 ORBITLINEPACKING_MAX_LEVEL = 8;
    constexpr double ORBITLINEPACKING_SOLID_START_PX = 200.0;
    constexpr double ORBITLINEPACKING_SOLID_END_PX = 800.0;

    // Returns a quiet NaN
    double OrbitLinePackingNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Returns positive infinity
    double OrbitLinePackingInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    // Splits a double into the contract's float pair: high = float(v), low = float(v - double(high))
    void OrbitLinePackingSplit(const double value, float& outHigh, float& outLow)
    {
        outHigh = static_cast<float>(value);
        outLow = static_cast<float>(value - static_cast<double>(outHigh));
    }

    // Returns the double reconstructed from a float pair
    double OrbitLinePackingJoin(const float high, const float low)
    {
        return static_cast<double>(high) + static_cast<double>(low);
    }

    // Builds a slightly inclined circle of radius 1 AU sampled at segments + 1 points (last closes the loop), with
    // unit tangents and cumulative chord lengths computed here
    void OrbitLinePackingCircle(const int32 segments, TArray<FVector3d>& outPoints, TArray<FVector3d>& outTangents,
        TArray<double>& outArcs)
    {
        outPoints.Reset();
        outTangents.Reset();
        outArcs.Reset();
        for (int32 k = 0; k <= segments; ++k)
        {
            const double angle = 2.0 * UE_DOUBLE_PI * k / segments;
            outPoints.Add(FVector3d(FMath::Cos(angle), FMath::Sin(angle), 0.05 * FMath::Sin(angle))
                * ORBITLINEPACKING_AU_M + FVector3d(0.37, -0.81, 0.123));
            outTangents.Add(FVector3d(-FMath::Sin(angle), FMath::Cos(angle), 0.05 * FMath::Cos(angle)).GetSafeNormal());
            outArcs.Add(k == 0 ? 0.0 : outArcs[k - 1] + FVector3d::Distance(outPoints[k], outPoints[k - 1]));
        }
    }

    // Returns the multi-scale coverage with the realistic tunables
    double OrbitLinePackingCoverage(const double phase, const double duty, const double footprint)
    {
        return SOLOrbitLineMath::MultiScaleDashCoverage(phase, duty, footprint, ORBITLINEPACKING_MIN_PERIOD_PX,
            ORBITLINEPACKING_MAX_LEVEL, ORBITLINEPACKING_SOLID_START_PX, ORBITLINEPACKING_SOLID_END_PX);
    }

    // Phases used by the dash tests: dash starts, mid dash, mid gap and large phases
    const double ORBITLINEPACKING_PHASES[] = { 0.0, 0.13, 0.25, 0.37, 0.5, 0.61, 0.75, 0.9, 3.77, 12.345 };
}

//////////////////////////////////////////////////////////////////////////
// PackCustomData
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingCustomDataLayoutTest, "SOLTest.OrbitLinePacking.CustomDataLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every slot of the 12-float layout holds the documented value and the tail of a larger buffer is untouched
bool FSOLOrbitLinePackingCustomDataLayoutTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Layout is 12 floats"), SOL::ORBIT_LINE_CUSTOM_DATA_FLOATS, 12);

    const FVector3d offset(ORBITLINEPACKING_AU_M + 0.37, -2.3e12 - 0.25, 7.7e12 + 0.123);
    const double semiMajorAxisM = 5.9064e12;
    const FLinearColor color(0.1f, 0.2f, 0.3f, 0.4f);

    for (const bool bHighlighted : { true, false })
    {
        TArray<float> data;
        data.Init(ORBITLINEPACKING_SENTINEL, 16);
        SOLOrbitLineMesh::PackCustomData(offset, semiMajorAxisM, bHighlighted, color, data);

        // Position high and low halves per component
        for (int32 axis = 0; axis < 3; ++axis)
        {
            float high;
            float low;
            OrbitLinePackingSplit(offset[axis], high, low);
            TestTrue(FString::Printf(TEXT("High %d"), axis), data[axis] == high);
            TestTrue(FString::Printf(TEXT("Low %d"), axis), data[4 + axis] == low);
        }
        TestTrue(TEXT("Semi-major axis"), data[3] == static_cast<float>(semiMajorAxisM));
        TestTrue(TEXT("Highlight flag"), data[7] == (bHighlighted ? 1.0f : 0.0f));
        TestTrue(TEXT("Color R"), data[8] == color.R);
        TestTrue(TEXT("Color G"), data[9] == color.G);
        TestTrue(TEXT("Color B"), data[10] == color.B);
        TestTrue(TEXT("Color A"), data[11] == color.A);
        for (int32 i = 12; i < data.Num(); ++i)
        {
            TestTrue(FString::Printf(TEXT("Slot %d untouched"), i), data[i] == ORBITLINEPACKING_SENTINEL);
        }
    }

    // An exactly sized buffer is accepted
    TArray<float> exact;
    exact.Init(ORBITLINEPACKING_SENTINEL, SOL::ORBIT_LINE_CUSTOM_DATA_FLOATS);
    SOLOrbitLineMesh::PackCustomData(FVector3d(1.0, 2.0, 3.0), 4.0, false, FLinearColor::White, exact);
    TestTrue(TEXT("Exact buffer: x"), exact[0] == 1.0f && exact[4] == 0.0f);
    TestTrue(TEXT("Exact buffer: alpha"), exact[11] == 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingCustomDataPrecisionTest,
    "SOLTest.OrbitLinePacking.CustomDataPrecision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The high + low pair reproduces each component to about |v| * 2^-47, far better than one float
bool FSOLOrbitLinePackingCustomDataPrecisionTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d cases[] = {
        FVector3d(ORBITLINEPACKING_AU_M + 0.37, -ORBITLINEPACKING_AU_M * 5.2 - 0.5, 1.0e12 + 0.001),
        FVector3d(9.87654321e12, -4.4e13 + 0.75, 2.5e12 - 0.3),
        FVector3d(0.5, -3.25e-3, 12345.678),
        FVector3d(0.0, -0.0, 1.0e-7),
    };
    for (const FVector3d& offset : cases)
    {
        TArray<float> data;
        data.Init(ORBITLINEPACKING_SENTINEL, SOL::ORBIT_LINE_CUSTOM_DATA_FLOATS);
        SOLOrbitLineMesh::PackCustomData(offset, 1.0e11, false, FLinearColor::Black, data);
        for (int32 axis = 0; axis < 3; ++axis)
        {
            const double value = offset[axis];
            const double error = FMath::Abs(OrbitLinePackingJoin(data[axis], data[4 + axis]) - value);
            const double bound = FMath::Abs(value) * FMath::Pow(2.0, -47.0) + 1.0e-30;
            TestTrue(FString::Printf(TEXT("%.6f reproduced within |v|*2^-47 (error %g)"), value, error),
                error <= bound);
        }
    }

    // 1 AU + 0.37 m: one float is thousands of metres off, the pair is sub-millimetre
    const double value = ORBITLINEPACKING_AU_M + 0.37;
    TArray<float> data;
    data.Init(0.0f, SOL::ORBIT_LINE_CUSTOM_DATA_FLOATS);
    SOLOrbitLineMesh::PackCustomData(FVector3d(value, 0.0, 0.0), 1.0, true, FLinearColor::Black, data);
    const double pairError = FMath::Abs(OrbitLinePackingJoin(data[0], data[4]) - value);
    const double floatError = FMath::Abs(static_cast<double>(static_cast<float>(value)) - value);
    TestTrue(FString::Printf(TEXT("Pair error %g < 1 mm"), pairError), pairError < 1.0e-3);
    TestTrue(FString::Printf(TEXT("Float alone error %g > 1 km"), floatError), floatError > 1000.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// BuildRibbon
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingRibbonRejectTest, "SOLTest.OrbitLinePacking.RibbonRejectsBadInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Too few points, mismatched arrays and too many vertices return false with both arrays emptied
bool FSOLOrbitLinePackingRibbonRejectTest::RunTest(const FString& /*parameters*/)
{
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    OrbitLinePackingCircle(8, points, tangents, arcs);

    // Runs BuildRibbon on pre-filled outputs and expects a rejection with both arrays reset
    auto expectReject = [this](const TCHAR* label, TConstArrayView<FVector3d> p, TConstArrayView<FVector3d> t,
        TConstArrayView<double> a)
    {
        TArray<FStaticMeshBuildVertex> vertices;
        vertices.SetNum(5);
        TArray<uint32> indices = { 0, 1, 2 };
        const bool bBuilt = SOLOrbitLineMesh::BuildRibbon(p, t, a, 7, vertices, indices);
        TestFalse(FString::Printf(TEXT("%s: rejected"), label), bBuilt);
        TestEqual(FString::Printf(TEXT("%s: no vertices"), label), vertices.Num(), 0);
        TestEqual(FString::Printf(TEXT("%s: no indices"), label), indices.Num(), 0);
    };

    const TConstArrayView<FVector3d> allPoints(points);
    const TConstArrayView<FVector3d> allTangents(tangents);
    const TConstArrayView<double> allArcs(arcs);
    expectReject(TEXT("No points"), allPoints.Left(0), allTangents.Left(0), allArcs.Left(0));
    expectReject(TEXT("One point"), allPoints.Left(1), allTangents.Left(1), allArcs.Left(1));
    expectReject(TEXT("Short tangents"), allPoints, allTangents.Left(8), allArcs);
    expectReject(TEXT("Short arcs"), allPoints, allTangents, allArcs.Left(8));
    expectReject(TEXT("Long arcs"), allPoints.Left(5), allTangents.Left(5), allArcs.Left(6));

    // 16-bit index limit: 32768 points (65536 vertices) fit, 32769 do not
    TArray<FVector3d> bigPoints;
    TArray<FVector3d> bigTangents;
    TArray<double> bigArcs;
    OrbitLinePackingCircle(32768, bigPoints, bigTangents, bigArcs);
    expectReject(TEXT("32769 points"), bigPoints, bigTangents, bigArcs);

    TArray<FStaticMeshBuildVertex> vertices;
    TArray<uint32> indices;
    const bool bBuilt = SOLOrbitLineMesh::BuildRibbon(TConstArrayView<FVector3d>(bigPoints).Left(32768),
        TConstArrayView<FVector3d>(bigTangents).Left(32768), TConstArrayView<double>(bigArcs).Left(32768), 7,
        vertices, indices);
    TestTrue(TEXT("32768 points accepted"), bBuilt);
    TestEqual(TEXT("32768 points: 65536 vertices"), vertices.Num(), 65536);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingRibbonVerticesTest, "SOLTest.OrbitLinePacking.RibbonVertices",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Two zero-position vertices per point carrying the split point, dash phase, side and tangent frame
bool FSOLOrbitLinePackingRibbonVerticesTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 dashCount = 7;
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    OrbitLinePackingCircle(8, points, tangents, arcs);
    const int32 count = points.Num();

    TArray<FStaticMeshBuildVertex> vertices;
    TArray<uint32> indices;
    TestTrue(TEXT("Built"), SOLOrbitLineMesh::BuildRibbon(points, tangents, arcs, dashCount, vertices, indices));
    if (!TestEqual(TEXT("Two vertices per point"), vertices.Num(), 2 * count))
    {
        return false;
    }

    for (int32 k = 0; k < count; ++k)
    {
        float hx, lx, hy, ly, hz, lz;
        OrbitLinePackingSplit(points[k].X, hx, lx);
        OrbitLinePackingSplit(points[k].Y, hy, ly);
        OrbitLinePackingSplit(points[k].Z, hz, lz);
        const double expectedPhase = arcs[k] * dashCount / arcs.Last();
        const FVector3f expectedNormal(static_cast<float>(tangents[k].X), static_cast<float>(-tangents[k].Y),
            static_cast<float>(tangents[k].Z));

        for (int32 sideIndex = 0; sideIndex < 2; ++sideIndex)
        {
            const FStaticMeshBuildVertex& vertex = vertices[2 * k + sideIndex];
            const float side = sideIndex == 0 ? -1.0f : 1.0f;
            const FString label = FString::Printf(TEXT("Point %d side %+.0f"), k, side);
            TestTrue(label + TEXT(": position zero"), vertex.Position == FVector3f::ZeroVector);
            TestTrue(label + TEXT(": UV0 = high x, y"), vertex.UVs[0].X == hx && vertex.UVs[0].Y == hy);
            TestTrue(label + TEXT(": UV1 = high z, low x"), vertex.UVs[1].X == hz && vertex.UVs[1].Y == lx);
            TestTrue(label + TEXT(": UV2 = low y, z"), vertex.UVs[2].X == ly && vertex.UVs[2].Y == lz);
            TestTrue(label + TEXT(": UV3.y = side"), vertex.UVs[3].Y == side);
            TestEqual(label + TEXT(": UV3.x = dash phase"), static_cast<double>(vertex.UVs[3].X), expectedPhase,
                1.0e-5 * dashCount);
            TestTrue(label + TEXT(": normal = tangent in Unreal axes"),
                FVector3f::Distance(vertex.TangentZ, expectedNormal) <= 1.0e-6f);
            TestTrue(label + TEXT(": TangentX unit"), FMath::Abs(vertex.TangentX.Size() - 1.0f) <= 1.0e-4f);
            TestTrue(label + TEXT(": TangentX perpendicular to normal"),
                FMath::Abs(FVector3f::DotProduct(vertex.TangentX, vertex.TangentZ)) <= 1.0e-4f);
        }
    }
    TestTrue(TEXT("First point phase is 0"), vertices[0].UVs[3].X == 0.0f && vertices[1].UVs[3].X == 0.0f);
    TestEqual(TEXT("Last point phase is the dash count"), vertices.Last().UVs[3].X, static_cast<float>(dashCount),
        1.0e-5f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingRibbonIndicesTest, "SOLTest.OrbitLinePacking.RibbonIndices",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Two triangles per segment over exactly that segment's four vertices; a second build replaces the first
bool FSOLOrbitLinePackingRibbonIndicesTest::RunTest(const FString& /*parameters*/)
{
    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    TArray<FStaticMeshBuildVertex> vertices;
    TArray<uint32> indices;

    for (const int32 segments : { 1, 2, 8, 33 })
    {
        OrbitLinePackingCircle(segments, points, tangents, arcs);
        const int32 count = points.Num();
        TestTrue(FString::Printf(TEXT("%d points: built"), count),
            SOLOrbitLineMesh::BuildRibbon(points, tangents, arcs, 3, vertices, indices));
        TestEqual(FString::Printf(TEXT("%d points: vertex count (replaced, not appended)"), count), vertices.Num(),
            2 * count);
        if (!TestEqual(FString::Printf(TEXT("%d points: 6 indices per segment"), count), indices.Num(),
            6 * (count - 1)))
        {
            continue;
        }

        for (int32 k = 0; k < count - 1; ++k)
        {
            bool bUsed[4] = { false, false, false, false };
            bool bInSegment = true;
            for (int32 i = 0; i < 6; ++i)
            {
                const uint32 index = indices[6 * k + i];
                TestTrue(FString::Printf(TEXT("%d points: index %u < 2N"), count, index),
                    index < static_cast<uint32>(2 * count));
                const int64 local = static_cast<int64>(index) - 2 * k;
                if (local < 0 || local > 3)
                {
                    bInSegment = false;
                    continue;
                }
                bUsed[local] = true;
            }
            TestTrue(FString::Printf(TEXT("%d points, segment %d: only its four vertices"), count, k), bInSegment);
            TestTrue(FString::Printf(TEXT("%d points, segment %d: all four vertices used"), count, k),
                bUsed[0] && bUsed[1] && bUsed[2] && bUsed[3]);

            // Neither triangle is degenerate
            for (int32 tri = 0; tri < 2; ++tri)
            {
                const uint32 a = indices[6 * k + 3 * tri];
                const uint32 b = indices[6 * k + 3 * tri + 1];
                const uint32 c = indices[6 * k + 3 * tri + 2];
                TestTrue(FString::Printf(TEXT("%d points, segment %d, triangle %d: distinct"), count, k, tri),
                    a != b && b != c && a != c);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingRibbonFromSampledOrbitTest,
    "SOLTest.OrbitLinePacking.RibbonFromSampledOrbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A sampled Earth-like orbit builds a ribbon whose phase runs from 0 to the dash count
bool FSOLOrbitLinePackingRibbonFromSampledOrbitTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeplerElements elements;
    elements.SemiMajorAxisM = ORBITLINEPACKING_AU_M;
    elements.Eccentricity = 0.0167;
    elements.InclinationRad = 0.01;
    elements.LongitudeOfAscendingNodeRad = 0.3;
    elements.ArgumentOfPeriapsisRad = 1.8;
    elements.MeanAnomalyRad = 0.5;

    TArray<FVector3d> points;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    const int32 count = SOLOrbitLineMath::SampleOrbit(elements, 256, points, tangents, arcs);
    if (!TestEqual(TEXT("Sampled N + 1 points"), count, 257))
    {
        return false;
    }

    TArray<FStaticMeshBuildVertex> vertices;
    TArray<uint32> indices;
    TestTrue(TEXT("Built"), SOLOrbitLineMesh::BuildRibbon(points, tangents, arcs, SOL::ORBIT_LINE_DASH_COUNT,
        vertices, indices));
    TestEqual(TEXT("Vertex count"), vertices.Num(), 2 * count);
    TestEqual(TEXT("Index count"), indices.Num(), 6 * (count - 1));
    if (vertices.Num() == 2 * count)
    {
        TestTrue(TEXT("Phase starts at 0"), vertices[0].UVs[3].X == 0.0f);
        TestEqual(TEXT("Phase ends at the dash count"), vertices.Last().UVs[3].X,
            static_cast<float>(SOL::ORBIT_LINE_DASH_COUNT), 1.0e-4f);
        float previous = -1.0f;
        bool bMonotonic = true;
        for (int32 k = 0; k < count; ++k)
        {
            bMonotonic &= vertices[2 * k].UVs[3].X >= previous;
            previous = vertices[2 * k].UVs[3].X;
        }
        TestTrue(TEXT("Phase never decreases"), bMonotonic);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// MultiScaleDashCoverage
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingDashFallbackTest, "SOLTest.OrbitLinePacking.DashFallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-finite phases give 0; bad footprints or periods fall back exactly to DashCoverage; NaN duty counts as 0
bool FSOLOrbitLinePackingDashFallbackTest::RunTest(const FString& /*parameters*/)
{
    for (const double phase : { OrbitLinePackingNaN(), OrbitLinePackingInf(), -OrbitLinePackingInf() })
    {
        TestEqual(TEXT("Non-finite phase (mid footprint) gives 0"), OrbitLinePackingCoverage(phase, 0.5, 0.01), 0.0);
        TestEqual(TEXT("Non-finite phase (level 0) gives 0"), OrbitLinePackingCoverage(phase, 0.5, 0.5), 0.0);
    }

    for (const double phase : ORBITLINEPACKING_PHASES)
    {
        for (const double footprint : { OrbitLinePackingNaN(), 0.0, -1.0, -1.0e-9, OrbitLinePackingInf() })
        {
            TestEqual(FString::Printf(TEXT("Bad footprint %g at phase %g = DashCoverage"), footprint, phase),
                OrbitLinePackingCoverage(phase, 0.6, footprint),
                SOLOrbitLineMath::DashCoverage(phase, 0.6, footprint));
        }
        for (const double minPeriod : { 0.0, -5.0, OrbitLinePackingNaN() })
        {
            TestEqual(FString::Printf(TEXT("Bad min period %g at phase %g = DashCoverage"), minPeriod, phase),
                SOLOrbitLineMath::MultiScaleDashCoverage(phase, 0.6, 1.0e-6, minPeriod, 8, 200.0, 800.0),
                SOLOrbitLineMath::DashCoverage(phase, 0.6, 1.0e-6));
        }
    }

    // NaN duty: no dash when not solid, fully solid when the period is huge
    TestEqual(TEXT("NaN duty, level 0"), OrbitLinePackingCoverage(0.25, OrbitLinePackingNaN(), 0.1), 0.0);
    TestEqual(TEXT("NaN duty, solid"), OrbitLinePackingCoverage(0.25, OrbitLinePackingNaN(), 1.0e-9), 1.0, 1.0e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingDashLevelsTest, "SOLTest.OrbitLinePacking.DashLevels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Level 0 below the minimum period, exact power-of-two levels, half-level crossfade and maxLevel 0
bool FSOLOrbitLinePackingDashLevelsTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineMath::DashCoverage;
    for (const double duty : { 0.5, 0.6 })
    {
        for (const double phase : ORBITLINEPACKING_PHASES)
        {
            // Periods at or under the minimum are plain level-0 coverage
            for (const double footprint : { 1.0 / 24.0, 0.1, 0.5, 2.0 })
            {
                TestEqual(FString::Printf(TEXT("Level 0: phase %g fp %g"), phase, footprint),
                    OrbitLinePackingCoverage(phase, duty, footprint), DashCoverage(phase, duty, footprint), 1.0e-12);
            }

            // Period minPeriod * 2^j is level j exactly (remaining period = 24 px, not solid)
            for (int32 j = 1; j <= ORBITLINEPACKING_MAX_LEVEL; ++j)
            {
                const double scale = FMath::Pow(2.0, static_cast<double>(j));
                const double footprint = 1.0 / (ORBITLINEPACKING_MIN_PERIOD_PX * scale);
                TestEqual(FString::Printf(TEXT("Level %d: phase %g duty %g"), j, phase, duty),
                    OrbitLinePackingCoverage(phase, duty, footprint),
                    DashCoverage(phase * scale, duty, footprint * scale), 1.0e-6);
            }

            // Level 3.5 is the midpoint crossfade of levels 3 and 4
            const double footprint = 1.0 / (ORBITLINEPACKING_MIN_PERIOD_PX * FMath::Pow(2.0, 3.5));
            const double expected = 0.5 * (DashCoverage(phase * 8.0, duty, footprint * 8.0)
                + DashCoverage(phase * 16.0, duty, footprint * 16.0));
            TestEqual(FString::Printf(TEXT("Level 3.5 crossfade: phase %g"), phase),
                OrbitLinePackingCoverage(phase, duty, footprint), expected, 1.0e-9);

            // maxLevel 0 (or negative) never subdivides; the remaining period is the full period
            for (const int32 maxLevel : { 0, -3 })
            {
                TestEqual(FString::Printf(TEXT("maxLevel %d: phase %g"), maxLevel, phase),
                    SOLOrbitLineMath::MultiScaleDashCoverage(phase, duty, 1.0 / 96.0, 24.0, maxLevel, 200.0, 800.0),
                    DashCoverage(phase, duty, 1.0 / 96.0), 1.0e-12);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingDashSolidTest, "SOLTest.OrbitLinePacking.DashSolid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Huge periods go solid (even in a gap), the solid ramp is linear, and an empty ramp is a step
bool FSOLOrbitLinePackingDashSolidTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineMath::DashCoverage;
    using SOLOrbitLineMath::MultiScaleDashCoverage;

    // Footprint 1e-9: period 1e9 px, remaining ~3.9e6 px after 8 levels, fully solid
    for (const double phase : ORBITLINEPACKING_PHASES)
    {
        TestEqual(FString::Printf(TEXT("Tiny footprint solid at phase %g"), phase),
            OrbitLinePackingCoverage(phase, 0.5, 1.0e-9), 1.0, 1.0e-12);
    }
    TestEqual(TEXT("Solid in a gap (phase 0.75, duty 0.5)"), OrbitLinePackingCoverage(0.75, 0.5, 1.0e-9), 1.0,
        1.0e-12);

    // maxLevel 0, period 500 px: halfway along the 200..800 ramp; mid-gap coverage 0 lifts to 0.5
    const double rampCoverage = DashCoverage(0.75, 0.5, 1.0 / 500.0);
    TestEqual(TEXT("Mid-gap box coverage is 0"), rampCoverage, 0.0, 1.0e-12);
    TestEqual(TEXT("Ramp midpoint"), MultiScaleDashCoverage(0.75, 0.5, 1.0 / 500.0, 24.0, 0, 200.0, 800.0), 0.5,
        1.0e-9);
    const double dashCoverage = DashCoverage(0.3, 0.5, 1.0 / 350.0);
    TestEqual(TEXT("Ramp quarter"), MultiScaleDashCoverage(0.3, 0.5, 1.0 / 350.0, 24.0, 0, 200.0, 800.0),
        dashCoverage + (1.0 - dashCoverage) * 0.25, 1.0e-9);

    // solidEnd <= solidStart: step at solidStart
    TestEqual(TEXT("Step: above start is solid"),
        MultiScaleDashCoverage(0.75, 0.5, 1.0 / 300.0, 24.0, 0, 200.0, 200.0), 1.0, 1.0e-12);
    TestEqual(TEXT("Step: below start is plain coverage"),
        MultiScaleDashCoverage(0.75, 0.5, 1.0 / 100.0, 24.0, 0, 200.0, 200.0), DashCoverage(0.75, 0.5, 1.0 / 100.0),
        1.0e-12);
    TestEqual(TEXT("Inverted ramp: above start is solid"),
        MultiScaleDashCoverage(0.75, 0.5, 1.0 / 300.0, 24.0, 0, 200.0, 100.0), 1.0, 1.0e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLinePackingDashContinuityTest, "SOLTest.OrbitLinePacking.DashContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sweeping the period from 10 px to 1e6 px stays in [0, 1] with no jumps at level or solid boundaries
bool FSOLOrbitLinePackingDashContinuityTest::RunTest(const FString& /*parameters*/)
{
    constexpr double step = 1.0005;
    for (const double duty : { 0.0, 0.3, 0.5, 1.0 })
    {
        for (const double phase : { 0.0, 0.25, 0.49, 0.5, 0.73, 3.1 })
        {
            double previous = OrbitLinePackingCoverage(phase, duty, 1.0 / 10.0);
            double worstJump = 0.0;
            bool bInRange = true;
            for (double period = 10.0 * step; period <= 1.0e6; period *= step)
            {
                const double value = OrbitLinePackingCoverage(phase, duty, 1.0 / period);
                bInRange &= value >= 0.0 && value <= 1.0;
                worstJump = FMath::Max(worstJump, FMath::Abs(value - previous));
                previous = value;
            }
            TestTrue(FString::Printf(TEXT("Duty %g phase %g: in [0, 1]"), duty, phase), bInRange);
            TestTrue(FString::Printf(TEXT("Duty %g phase %g: worst step jump %g < 0.02"), duty, phase, worstJump),
                worstJump < 0.02);
        }
    }

    // Straddling each integer level boundary barely changes the value
    for (int32 j = 1; j <= ORBITLINEPACKING_MAX_LEVEL; ++j)
    {
        const double boundary = ORBITLINEPACKING_MIN_PERIOD_PX * FMath::Pow(2.0, static_cast<double>(j));
        for (const double phase : ORBITLINEPACKING_PHASES)
        {
            const double below = OrbitLinePackingCoverage(phase, 0.6, 1.0 / (boundary * (1.0 - 1.0e-7)));
            const double above = OrbitLinePackingCoverage(phase, 0.6, 1.0 / (boundary * (1.0 + 1.0e-7)));
            TestTrue(FString::Printf(TEXT("Level %d boundary at phase %g: |%g - %g| small"), j, phase, below, above),
                FMath::Abs(below - above) < 1.0e-3);
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
