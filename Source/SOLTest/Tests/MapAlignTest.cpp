/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapAlignSmoke.h"
#include "OrbitLines/SOLOrbitLineMath.h"
#include "SOLConstants.h"
#include "Universe/SOLBodyRegistry.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // A 1000 x 1000 px view; clip points below are written as a unit-focal pinhole: (right, up, unused, depth)
    const FIntRect MAP_ALIGN_TEST_VIEW(0, 0, 1000, 1000);
    const FVector2D MAP_ALIGN_TEST_CENTER(500.0, 500.0);
    constexpr double MAP_ALIGN_TEST_NEAR_W = 1.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns the screen distance from the view centre to the clip-space segment, as the smoke measures it
    double MapAlignTestCenterDistancePx(const FVector4& start, const FVector4& end)
    {
        const FVector4 points[] = { start, end };
        return FSOLMapAlignSmoke::ScreenDistanceToClipPolylinePx(MAP_ALIGN_TEST_CENTER, points, MAP_ALIGN_TEST_VIEW,
            MAP_ALIGN_TEST_NEAR_W);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapAlignClipTest, "SOLTest.MapAlign.ChordCrossingCameraPlaneIsClipped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #25: a long orbit-line chord seen from close by can start behind the camera; the smoke must clip it at the
// near plane as the rasterizer does, not drop it (which failed 'Earth on line' at some times of day)
bool FSOLMapAlignClipTest::RunTest(const FString& /*parameters*/)
{
    // From behind the camera (depth -5) to in front (depth 15), crossing the view axis at depth 5: on the centre
    TestEqual(TEXT("Chord through the view axis, one end behind the camera"),
        MapAlignTestCenterDistancePx(FVector4(-10.0, 0.0, 0.0, -5.0), FVector4(10.0, 0.0, 0.0, 15.0)), 0.0, 1.0e-6);
    TestEqual(TEXT("Same chord reversed"),
        MapAlignTestCenterDistancePx(FVector4(10.0, 0.0, 0.0, 15.0), FVector4(-10.0, 0.0, 0.0, -5.0)), 0.0, 1.0e-6);

    // Entirely in front at depth 10, 1 unit up: 0.5 * (1 / 10) * 1000 = 50 px above the centre
    TestEqual(TEXT("Chord in front of the camera"),
        MapAlignTestCenterDistancePx(FVector4(-10.0, 1.0, 0.0, 10.0), FVector4(10.0, 1.0, 0.0, 10.0)), 50.0, 1.0e-6);

    // Entirely behind the camera: not drawn, so no distance
    TestEqual(TEXT("Chord behind the camera"),
        MapAlignTestCenterDistancePx(FVector4(-10.0, 0.0, 0.0, -5.0), FVector4(10.0, 0.0, 0.0, -1.0)),
        TNumericLimits<double>::Max());

    // A polyline takes the nearest of its segments
    const FVector4 polyline[] = { FVector4(-10.0, 1.0, 0.0, 10.0), FVector4(10.0, 1.0, 0.0, 10.0),
        FVector4(10.0, 4.0, 0.0, 10.0) };
    TestEqual(TEXT("Polyline nearest segment"), FSOLMapAlignSmoke::ScreenDistanceToClipPolylinePx(
        MAP_ALIGN_TEST_CENTER, polyline, MAP_ALIGN_TEST_VIEW, MAP_ALIGN_TEST_NEAR_W), 50.0, 1.0e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapAlignBodyOnLineTest, "SOLTest.MapAlign.BodyOnItsLineAtAnyDate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #25: at any date, every registry body lies on its orbit line as ASOLOrbitLineVisuals samples it (its own
// segment count, uniform in E from periapsis), within that line's designed chord error (ChordSagittaM)
bool FSOLMapAlignBodyOnLineTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();

    // Every 29 minutes over two days from 2026-10-27 (a body crosses Earth's line's ~6.2e8 m chords in ~6 h, so this
    // samples every phase within a chord), then every 2.5 years across 1950-2050
    TArray<double> datesSeconds;
    const double startSeconds = (2461340.5 - 2451545.0) * 86400.0;
    for (int32 step = 0; step < 100; ++step)
    {
        datesSeconds.Add(startSeconds + step * 29.0 * 60.0);
    }
    for (int32 step = 0; step <= 40; ++step)
    {
        datesSeconds.Add((-50.0 + 2.5 * step) * 365.25 * 86400.0);
    }

    TArray<FVector3d> pointsM;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    int32 checkedCount = 0;
    for (const double seconds : datesSeconds)
    {
        registry.Update(seconds);
        const double centuries = seconds / SOL::SECONDS_PER_JULIAN_CENTURY;
        for (int32 body = 0; body < registry.Num(); ++body)
        {
            const int32 parent = registry.GetParent(body);
            if (parent == INDEX_NONE)
            {
                continue;
            }

            // The line as the actor builds it, relative to the parent like the body's own position
            const FSOLKeplerElements elements = registry.GetElements(body).AtCenturies(centuries);
            const int32 segments = SOLOrbitLineMath::SegmentCountFor(elements.SemiMajorAxisM,
                registry.GetRadiusM(body));
            pointsM.Reset();
            tangents.Reset();
            arcs.Reset();
            if (!TestTrue(TEXT("Line sampled"),
                SOLOrbitLineMath::SampleOrbit(elements, segments, pointsM, tangents, arcs) > 1))
            {
                return false;
            }
            const FVector3d relativeM = registry.GetPositionM(body) - registry.GetPositionM(parent);
            double distanceM = TNumericLimits<double>::Max();
            for (int32 index = 1; index < pointsM.Num(); ++index)
            {
                distanceM = FMath::Min(distanceM, FMath::PointDistToSegment(relativeM, pointsM[index - 1],
                    pointsM[index]));
            }

            // 1 m of slack for double rounding at up to ~1e13 m
            const double boundM = SOLOrbitLineMath::ChordSagittaM(elements.SemiMajorAxisM, segments) + 1.0;
            if (distanceM > boundM)
            {
                AddError(FString::Printf(TEXT("%s at %.0f s: %.0f m from its line (chord error %.0f m)"),
                    *registry.GetName(body).ToString(), seconds, distanceM, boundM));
            }
            ++checkedCount;
        }
    }
    int32 childCount = 0;
    for (int32 body = 0; body < registry.Num(); ++body)
    {
        childCount += registry.GetParent(body) != INDEX_NONE ? 1 : 0;
    }
    TestTrue(TEXT("Bodies with a parent exist"), childCount > 0);
    TestEqual(TEXT("Every body with a parent checked at every date"), checkedCount,
        datesSeconds.Num() * childCount);
    return true;
}

#endif
