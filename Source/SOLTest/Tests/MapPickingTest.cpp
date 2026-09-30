/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapPicking.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the jump-map pure-logic contract (Docs/SDDs/3-jump-map.md, Appendix D). All positions are universe/ecliptic
// frame meters; Up = ecliptic north (0,0,1)
namespace
{
    const FVector3d MAP_PICK_X(1.0, 0.0, 0.0);
    const FVector3d MAP_PICK_Y(0.0, 1.0, 0.0);
    const FVector3d MAP_PICK_UP(0.0, 0.0, 1.0);

    // Absolute tolerance (meters) for results near solar-system-scale coordinates (double ulp at 1.5e11 m is ~3e-5 m)
    constexpr double MAP_PICK_LARGE_ABS_TOL_M = 1.0e-3;

    // Tight absolute tolerance (meters) at ~1e11 m: a few input-rounding ulps; a precision-naive implementation fails
    constexpr double MAP_PICK_TIGHT_ABS_TOL_M = 1.0e-4;

    //////////////////////////////////////////////////////////////////////////
    // Returns an in-plane orthonormal ecliptic basis rotated by the given angle about Up
    void MapPickRotatedBasis(const double angleRad, FVector3d& outX, FVector3d& outY)
    {
        outX = FVector3d(FMath::Cos(angleRad), FMath::Sin(angleRad), 0.0);
        outY = FVector3d(-FMath::Sin(angleRad), FMath::Cos(angleRad), 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the signed distance of a point from a plane, measured along the unit-normalized plane normal
    double MapPickPlaneDistance(const FVector3d& point, const FVector3d& planeOrigin, const FVector3d& planeNormal)
    {
        return FVector3d::DotProduct(point - planeOrigin, planeNormal.GetSafeNormal());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the perpendicular distance of a point from the ray's supporting line, and the scalar along the unit ray
    double MapPickRayLineDistance(const FVector3d& point, const FVector3d& rayOrigin, const FVector3d& rayDir,
        double& outAlongM)
    {
        const FVector3d unitDir = rayDir.GetSafeNormal();
        const FVector3d offset = point - rayOrigin;
        outAlongM = FVector3d::DotProduct(offset, unitDir);
        return (offset - outAlongM * unitDir).Size();
    }
}

// --- RayPlaneIntersect ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayStraightDownTest, "SOLTest.MapPicking.RayPlaneStraightDownHits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ray straight down onto the ecliptic plane from above hits directly below its origin
bool FSOLMapPickingRayStraightDownTest::RunTest(const FString& /*parameters*/)
{
    FVector3d point(-1.0, -1.0, -1.0);
    const bool bHit = SOLMapPicking::RayPlaneIntersect(FVector3d(3.0, 4.0, 100.0), FVector3d(0.0, 0.0, -1.0),
        FVector3d::ZeroVector, MAP_PICK_UP, point);
    TestTrue(TEXT("Straight-down ray hits the plane"), bHit);
    TestTrue(TEXT("Hit point is (3, 4, 0)"), point.Equals(FVector3d(3.0, 4.0, 0.0), 1e-9));

    // Same, onto a plane raised to z = 25 and offset in XY (plane origin placement must not matter within the plane)
    FVector3d raised(-1.0, -1.0, -1.0);
    const bool bRaisedHit = SOLMapPicking::RayPlaneIntersect(FVector3d(-7.0, 12.0, 100.0), FVector3d(0.0, 0.0, -1.0),
        FVector3d(500.0, -300.0, 25.0), MAP_PICK_UP, raised);
    TestTrue(TEXT("Straight-down ray hits the raised plane"), bRaisedHit);
    TestTrue(TEXT("Raised hit point is (-7, 12, 25)"), raised.Equals(FVector3d(-7.0, 12.0, 25.0), 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayObliqueTest, "SOLTest.MapPicking.RayPlaneObliqueHits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 45-degree ray from height h lands h meters away horizontally along its heading
bool FSOLMapPickingRayObliqueTest::RunTest(const FString& /*parameters*/)
{
    FVector3d point = FVector3d::ZeroVector;
    const FVector3d dir = FVector3d(1.0, 1.0, -FMath::Sqrt(2.0)).GetSafeNormal();
    const bool bHit = SOLMapPicking::RayPlaneIntersect(FVector3d(10.0, 20.0, 200.0), dir, FVector3d::ZeroVector,
        MAP_PICK_UP, point);
    TestTrue(TEXT("Oblique ray hits the plane"), bHit);
    const double horizontal = 200.0 / FMath::Sqrt(2.0);
    TestTrue(TEXT("Oblique hit point"), SOLTestHelpers::VectorsNear(point,
        FVector3d(10.0 + horizontal, 20.0 + horizontal, 0.0), 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayFromBelowTest, "SOLTest.MapPicking.RayPlaneFromBelowHits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The plane is two-sided: a ray from below travelling up (against the normal's back face) still hits
bool FSOLMapPickingRayFromBelowTest::RunTest(const FString& /*parameters*/)
{
    FVector3d point = FVector3d::ZeroVector;
    const bool bHit = SOLMapPicking::RayPlaneIntersect(FVector3d(8.0, -9.0, -50.0), FVector3d(0.0, 0.0, 1.0),
        FVector3d::ZeroVector, MAP_PICK_UP, point);
    TestTrue(TEXT("Upward ray from below hits the plane"), bHit);
    TestTrue(TEXT("Hit point is (8, -9, 0)"), point.Equals(FVector3d(8.0, -9.0, 0.0), 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayParallelTest, "SOLTest.MapPicking.RayPlaneParallelMisses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ray parallel to the plane (dot(rayDir, normal) == 0) reports no intersection, above the plane or lying in it
bool FSOLMapPickingRayParallelTest::RunTest(const FString& /*parameters*/)
{
    FVector3d point = FVector3d::ZeroVector;
    TestFalse(TEXT("Parallel ray above the plane misses"), SOLMapPicking::RayPlaneIntersect(FVector3d(0.0, 0.0, 10.0),
        FVector3d(1.0, 0.0, 0.0), FVector3d::ZeroVector, MAP_PICK_UP, point));
    TestFalse(TEXT("Parallel diagonal ray above the plane misses"), SOLMapPicking::RayPlaneIntersect(
        FVector3d(5.0, 5.0, -10.0), FVector3d(1.0, -1.0, 0.0).GetSafeNormal(), FVector3d::ZeroVector, MAP_PICK_UP,
        point));
    TestFalse(TEXT("Parallel ray lying in the plane reports no (unique) intersection"),
        SOLMapPicking::RayPlaneIntersect(FVector3d(1.0, 2.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d::ZeroVector,
            MAP_PICK_UP, point));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayBehindTest, "SOLTest.MapPicking.RayPlaneBehindOriginMisses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ray pointing away from the plane (intersection at negative t) reports no intersection, while the reversed ray hits
bool FSOLMapPickingRayBehindTest::RunTest(const FString& /*parameters*/)
{
    FVector3d point = FVector3d::ZeroVector;
    TestFalse(TEXT("Ray above the plane pointing up misses"), SOLMapPicking::RayPlaneIntersect(
        FVector3d(0.0, 0.0, 100.0), FVector3d(0.0, 0.0, 1.0), FVector3d::ZeroVector, MAP_PICK_UP, point));
    TestFalse(TEXT("Ray below the plane pointing down misses"), SOLMapPicking::RayPlaneIntersect(
        FVector3d(0.0, 0.0, -100.0), FVector3d(0.3, 0.0, -1.0).GetSafeNormal(), FVector3d::ZeroVector, MAP_PICK_UP,
        point));

    // Control: flipping the first ray makes it hit, so the misses above are about direction, not geometry
    TestTrue(TEXT("Reversed ray hits"), SOLMapPicking::RayPlaneIntersect(FVector3d(0.0, 0.0, 100.0),
        FVector3d(0.0, 0.0, -1.0), FVector3d::ZeroVector, MAP_PICK_UP, point));
    TestTrue(TEXT("Reversed ray hit point is the origin"), point.Equals(FVector3d::ZeroVector, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayOnPlaneTest, "SOLTest.MapPicking.RayPlaneOriginOnPlane",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A non-parallel ray starting exactly on the plane intersects at t = 0 (not behind), i.e. at its own origin
bool FSOLMapPickingRayOnPlaneTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d origin(5.0, 6.0, 0.0);
    FVector3d point = FVector3d::ZeroVector;
    const bool bHit = SOLMapPicking::RayPlaneIntersect(origin, FVector3d(0.6, 0.0, -0.8), FVector3d::ZeroVector,
        MAP_PICK_UP, point);
    TestTrue(TEXT("Ray starting on the plane hits"), bHit);
    TestTrue(TEXT("Hit point is the ray origin"), point.Equals(origin, 1e-9));

    // Same, leaving the plane on the other side
    FVector3d away = FVector3d::ZeroVector;
    const bool bAwayHit = SOLMapPicking::RayPlaneIntersect(origin, FVector3d(0.6, 0.0, 0.8), FVector3d::ZeroVector,
        MAP_PICK_UP, away);
    TestTrue(TEXT("Ray starting on the plane and leaving upward hits at t = 0"), bAwayHit);
    TestTrue(TEXT("Upward-leaving hit point is the ray origin"), away.Equals(origin, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayGrazingTest, "SOLTest.MapPicking.RayPlaneGrazingAngle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A shallow (1 mrad) ray still resolves: the point lies on the plane and forward along the ray, at the analytic spot
bool FSOLMapPickingRayGrazingTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin(0.0, 0.0, 10.0);
    const FVector3d rayDir = FVector3d(1.0, 0.0, -1.0e-3).GetSafeNormal();
    FVector3d point = FVector3d::ZeroVector;
    const bool bHit = SOLMapPicking::RayPlaneIntersect(rayOrigin, rayDir, FVector3d::ZeroVector, MAP_PICK_UP, point);
    TestTrue(TEXT("Grazing ray hits"), bHit);
    TestTrue(TEXT("Grazing hit point is finite"), SOLTestHelpers::IsFiniteVector(point));
    TestTrue(TEXT("Grazing hit point is (1e4, 0, 0)"), SOLTestHelpers::VectorsNear(point, FVector3d(1.0e4, 0.0, 0.0),
        1e-9));

    // Algebraic checks: on the plane, and on the forward ray (collinear with rayDir, non-negative scalar)
    TestEqual(TEXT("Grazing hit lies on the plane"), MapPickPlaneDistance(point, FVector3d::ZeroVector, MAP_PICK_UP),
        0.0, 1e-7);
    double alongM = -1.0;
    const double offLine = MapPickRayLineDistance(point, rayOrigin, rayDir, alongM);
    TestTrue(TEXT("Grazing hit lies on the ray line"), offLine <= 1e-7);
    TestTrue(TEXT("Grazing hit is forward along the ray"), alongM >= 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayTiltedPlaneTest, "SOLTest.MapPicking.RayPlaneTiltedPlane",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A general (non-axis) plane and ray: the hit is the known in-plane target, on the plane and on the forward ray
bool FSOLMapPickingRayTiltedPlaneTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d planeOrigin(100.0, -200.0, 300.0);
    const FVector3d planeNormal = FVector3d(1.0, 2.0, 3.0).GetSafeNormal();
    // (3, 0, -1) is orthogonal to (1, 2, 3), so this target lies in the plane
    const FVector3d target = planeOrigin + 50.0 * FVector3d(3.0, 0.0, -1.0);
    const FVector3d rayOrigin(1000.0, 1000.0, 1000.0);
    const FVector3d rayDir = (target - rayOrigin).GetSafeNormal();

    FVector3d point = FVector3d::ZeroVector;
    const bool bHit = SOLMapPicking::RayPlaneIntersect(rayOrigin, rayDir, planeOrigin, planeNormal, point);
    TestTrue(TEXT("Ray hits the tilted plane"), bHit);
    TestTrue(TEXT("Hit is the in-plane target"), SOLTestHelpers::VectorsNear(point, target, 1e-12));
    TestEqual(TEXT("Hit lies on the tilted plane"), MapPickPlaneDistance(point, planeOrigin, planeNormal), 0.0, 1e-9);
    double alongM = -1.0;
    TestTrue(TEXT("Hit lies on the ray line"), MapPickRayLineDistance(point, rayOrigin, rayDir, alongM) <= 1e-9);
    TestTrue(TEXT("Hit is forward along the ray"), alongM >= 0.0);

    // The opposite ray from the same origin points away from the plane
    FVector3d away = FVector3d::ZeroVector;
    TestFalse(TEXT("Opposite ray misses the tilted plane"), SOLMapPicking::RayPlaneIntersect(rayOrigin, -rayDir,
        planeOrigin, planeNormal, away));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayNonUnitNormalTest, "SOLTest.MapPicking.RayPlaneNonUnitNormal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Scaling or flipping the plane normal describes the same plane, so the intersection must not change
bool FSOLMapPickingRayNonUnitNormalTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin(10.0, -5.0, 40.0);
    const FVector3d rayDir = FVector3d(0.5, 0.25, -1.0).GetSafeNormal();
    const FVector3d expected(30.0, 5.0, 0.0);

    FVector3d scaled = FVector3d::ZeroVector;
    TestTrue(TEXT("Hit with normal (0, 0, 2.5)"), SOLMapPicking::RayPlaneIntersect(rayOrigin, rayDir,
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, 2.5), scaled));
    TestTrue(TEXT("Scaled-normal hit point"), SOLTestHelpers::VectorsNear(scaled, expected, 1e-12));

    FVector3d flipped = FVector3d::ZeroVector;
    TestTrue(TEXT("Hit with normal (0, 0, -1)"), SOLMapPicking::RayPlaneIntersect(rayOrigin, rayDir,
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, -1.0), flipped));
    TestTrue(TEXT("Flipped-normal hit point"), SOLTestHelpers::VectorsNear(flipped, expected, 1e-12));

    // Very small but nonzero normals describe the same plane (a coarse zero-length tolerance must not reject them)
    FVector3d tiny = FVector3d::ZeroVector;
    TestTrue(TEXT("Hit with normal (0, 0, 1e-10)"), SOLMapPicking::RayPlaneIntersect(rayOrigin, rayDir,
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, 1.0e-10), tiny));
    TestTrue(TEXT("Tiny-normal hit point"), SOLTestHelpers::VectorsNear(tiny, expected, 1e-12));

    FVector3d tinier = FVector3d::ZeroVector;
    TestTrue(TEXT("Hit with normal (0, 0, -1e-200)"), SOLMapPicking::RayPlaneIntersect(rayOrigin, rayDir,
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, -1.0e-200), tinier));
    TestTrue(TEXT("Tinier-normal hit point"), SOLTestHelpers::VectorsNear(tinier, expected, 1e-12));

    FVector3d huge = FVector3d::ZeroVector;
    TestTrue(TEXT("Hit with normal (0, 0, 1e200)"), SOLMapPicking::RayPlaneIntersect(rayOrigin, rayDir,
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, 1.0e200), huge));
    TestTrue(TEXT("Huge-normal hit point"), SOLTestHelpers::VectorsNear(huge, expected, 1e-12));

    // Only an exactly-zero normal is invalid
    FVector3d zero(1.0, 1.0, 1.0);
    TestFalse(TEXT("Zero normal reports no intersection"), SOLMapPicking::RayPlaneIntersect(rayOrigin, rayDir,
        FVector3d::ZeroVector, FVector3d::ZeroVector, zero));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayLargeTiltedTest, "SOLTest.MapPicking.RayPlaneLargeTiltedNormal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At ~1e11 m, a tilted non-unit plane normal and an oblique ray still land within 0.1 mm of the analytic hit
bool FSOLMapPickingRayLargeTiltedTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d planeOrigin(1.2e11, -8.0e10, 3.0e9);
    const FVector3d planeNormal(0.3, -0.5, 0.8);

    // (5, 3, 0) and (0, 8, 5) are both orthogonal to (0.3, -0.5, 0.8), so this offset lies in the plane
    const FVector3d inPlaneOffset = 400.0 * FVector3d(5.0, 3.0, 0.0) - 250.0 * FVector3d(0.0, 8.0, 5.0);
    const FVector3d rayToTarget = FVector3d(-0.4, 0.7, -1.0).GetSafeNormal();
    const FVector3d rayOrigin = planeOrigin + inPlaneOffset - 2.0e4 * rayToTarget;

    FVector3d point = FVector3d::ZeroVector;
    TestTrue(TEXT("Large-scale ray hits the tilted plane"), SOLMapPicking::RayPlaneIntersect(rayOrigin, rayToTarget,
        planeOrigin, planeNormal, point));
    const FVector3d hitOffset = point - planeOrigin;
    TestTrue(TEXT("Large-scale tilted hit within 0.1 mm of the in-plane target"),
        (hitOffset - inPlaneOffset).Size() <= MAP_PICK_TIGHT_ABS_TOL_M);
    TestTrue(TEXT("Large-scale tilted hit lies on the plane within 0.1 mm"),
        FMath::Abs(FVector3d::DotProduct(hitOffset, planeNormal.GetSafeNormal())) <= MAP_PICK_TIGHT_ABS_TOL_M);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingRayLargeCoordsTest, "SOLTest.MapPicking.RayPlaneLargeCoordinates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At solar-system scale (~1e11 m) the hit is still accurate to the millimeter
bool FSOLMapPickingRayLargeCoordsTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d planeOrigin(1.5e11, -7.0e10, 2.0e9);

    // Near camera: 500 km above the plane, slightly tilted; lands (1500, 3000) m from the plane origin
    const FVector3d nearOrigin = planeOrigin + FVector3d(1000.0, 2000.0, 5.0e5);
    const FVector3d nearDir = FVector3d(1.0e-3, 2.0e-3, -1.0).GetSafeNormal();
    FVector3d nearPoint = FVector3d::ZeroVector;
    TestTrue(TEXT("Near large-scale ray hits"), SOLMapPicking::RayPlaneIntersect(nearOrigin, nearDir, planeOrigin,
        MAP_PICK_UP, nearPoint));
    const FVector3d nearExpected = planeOrigin + FVector3d(1500.0, 3000.0, 0.0);
    TestTrue(TEXT("Near large-scale hit accurate to 1 mm"),
        (nearPoint - nearExpected).Size() <= MAP_PICK_LARGE_ABS_TOL_M);
    TestEqual(TEXT("Near large-scale hit Z is exactly the plane height"), nearPoint.Z, planeOrigin.Z,
        MAP_PICK_LARGE_ABS_TOL_M);

    // System-wide camera: 2 AU above the plane, tilted 10 mrad; lands 3e9 m away along X
    const FVector3d farOrigin = planeOrigin + FVector3d(0.0, 0.0, 3.0e11);
    const FVector3d farDir = FVector3d(1.0e-2, 0.0, -1.0).GetSafeNormal();
    FVector3d farPoint = FVector3d::ZeroVector;
    TestTrue(TEXT("Far large-scale ray hits"), SOLMapPicking::RayPlaneIntersect(farOrigin, farDir, planeOrigin,
        MAP_PICK_UP, farPoint));
    const FVector3d farExpected = planeOrigin + FVector3d(3.0e9, 0.0, 0.0);
    TestTrue(TEXT("Far large-scale hit within 1e-9 relative of the 3e9 m offset"),
        (farPoint - farExpected).Size() <= 1e-9 * 3.0e9);
    TestEqual(TEXT("Far large-scale hit lies on the plane"), farPoint.Z, planeOrigin.Z, MAP_PICK_LARGE_ABS_TOL_M);
    return true;
}

// --- DecomposePlanarOffset ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingDecomposeAxesTest, "SOLTest.MapPicking.DecomposePureAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An offset purely along EclipticX returns (d, 0); purely along EclipticY returns (0, d); signs are preserved
bool FSOLMapPickingDecomposeAxesTest::RunTest(const FString& /*parameters*/)
{
    const FVector2D alongX = SOLMapPicking::DecomposePlanarOffset(FVector3d(1234.5, 0.0, 0.0), FVector3d::ZeroVector,
        MAP_PICK_X, MAP_PICK_Y);
    TestEqual(TEXT("Pure X -> dx"), alongX.X, 1234.5, 1e-9);
    TestEqual(TEXT("Pure X -> dy = 0"), alongX.Y, 0.0, 1e-9);

    const FVector2D alongY = SOLMapPicking::DecomposePlanarOffset(FVector3d(0.0, 987.25, 0.0), FVector3d::ZeroVector,
        MAP_PICK_X, MAP_PICK_Y);
    TestEqual(TEXT("Pure Y -> dx = 0"), alongY.X, 0.0, 1e-9);
    TestEqual(TEXT("Pure Y -> dy"), alongY.Y, 987.25, 1e-9);

    const FVector2D negative = SOLMapPicking::DecomposePlanarOffset(FVector3d(-40.0, -60.0, 0.0), FVector3d::ZeroVector,
        MAP_PICK_X, MAP_PICK_Y);
    TestEqual(TEXT("Negative -> dx"), negative.X, -40.0, 1e-9);
    TestEqual(TEXT("Negative -> dy"), negative.Y, -60.0, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingDecomposeCombinedTest, "SOLTest.MapPicking.DecomposeCombinedAndOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A combined offset relative to a nonzero plane origin yields both components; any Up component is ignored
bool FSOLMapPickingDecomposeCombinedTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d planeOrigin(100.0, -50.0, 7.0);
    const FVector2D offset = SOLMapPicking::DecomposePlanarOffset(planeOrigin + FVector3d(3.0, -4.0, 99.0),
        planeOrigin, MAP_PICK_X, MAP_PICK_Y);
    TestEqual(TEXT("Combined -> dx"), offset.X, 3.0, 1e-9);
    TestEqual(TEXT("Combined -> dy"), offset.Y, -4.0, 1e-9);

    const FVector2D atOrigin = SOLMapPicking::DecomposePlanarOffset(planeOrigin, planeOrigin, MAP_PICK_X, MAP_PICK_Y);
    TestEqual(TEXT("Point at the plane origin -> dx = 0"), atOrigin.X, 0.0, 0.0);
    TestEqual(TEXT("Point at the plane origin -> dy = 0"), atOrigin.Y, 0.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingDecomposeRotatedTest, "SOLTest.MapPicking.DecomposeRotatedBasis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With a rotated (still orthonormal, in-plane) basis the components are the dot products with each axis
bool FSOLMapPickingDecomposeRotatedTest::RunTest(const FString& /*parameters*/)
{
    FVector3d axisX;
    FVector3d axisY;
    MapPickRotatedBasis(FMath::DegreesToRadians(30.0), axisX, axisY);
    const FVector3d planeOrigin(-20.0, 35.0, 0.0);
    const FVector3d point = planeOrigin + 7.0 * axisX - 11.0 * axisY + FVector3d(0.0, 0.0, 5.0);
    const FVector2D offset = SOLMapPicking::DecomposePlanarOffset(point, planeOrigin, axisX, axisY);
    TestEqual(TEXT("Rotated -> dx"), offset.X, 7.0, 1e-9);
    TestEqual(TEXT("Rotated -> dy"), offset.Y, -11.0, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingDecomposeLargeTest, "SOLTest.MapPicking.DecomposeLargeOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A small offset from a ~1e11 m plane origin is recovered accurately (no large-number precision loss)
bool FSOLMapPickingDecomposeLargeTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d planeOrigin(1.5e11, -1.2e11, 3.0e9);
    const FVector2D offset = SOLMapPicking::DecomposePlanarOffset(planeOrigin + FVector3d(1000.0, -250.0, 42.0),
        planeOrigin, MAP_PICK_X, MAP_PICK_Y);
    TestTrue(TEXT("Large origin -> dx within 1e-9 relative"), SOLTestHelpers::RelativeError(offset.X, 1000.0) <= 1e-9);
    TestTrue(TEXT("Large origin -> dy within 1e-9 relative"), SOLTestHelpers::RelativeError(offset.Y, -250.0) <= 1e-9);

    // Rotated basis at the same scale: the rounding of the input point itself (~3e-5 m) bounds achievable accuracy
    FVector3d axisX;
    FVector3d axisY;
    MapPickRotatedBasis(FMath::DegreesToRadians(-65.0), axisX, axisY);
    const FVector2D rotated = SOLMapPicking::DecomposePlanarOffset(planeOrigin + 1000.0 * axisX + 500.0 * axisY,
        planeOrigin, axisX, axisY);
    TestEqual(TEXT("Large origin, rotated -> dx"), rotated.X, 1000.0, MAP_PICK_TIGHT_ABS_TOL_M);
    TestEqual(TEXT("Large origin, rotated -> dy"), rotated.Y, 500.0, MAP_PICK_TIGHT_ABS_TOL_M);
    return true;
}

// --- ComposeDestination ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingComposeAxisTest, "SOLTest.MapPicking.ComposeAxisAligned",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Reference + dx*X + dy*Y + dz*Up for the standard ecliptic axes, per axis and all together
bool FSOLMapPickingComposeAxisTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d reference(10.0, 20.0, 30.0);
    TestTrue(TEXT("dx only"), SOLMapPicking::ComposeDestination(reference, 5.0, 0.0, 0.0, MAP_PICK_X, MAP_PICK_Y,
        MAP_PICK_UP).Equals(FVector3d(15.0, 20.0, 30.0), 1e-9));
    TestTrue(TEXT("dy only"), SOLMapPicking::ComposeDestination(reference, 0.0, -6.0, 0.0, MAP_PICK_X, MAP_PICK_Y,
        MAP_PICK_UP).Equals(FVector3d(10.0, 14.0, 30.0), 1e-9));
    TestTrue(TEXT("dz only"), SOLMapPicking::ComposeDestination(reference, 0.0, 0.0, 7.5, MAP_PICK_X, MAP_PICK_Y,
        MAP_PICK_UP).Equals(FVector3d(10.0, 20.0, 37.5), 1e-9));
    TestTrue(TEXT("dx, dy and dz together"), SOLMapPicking::ComposeDestination(reference, 1.0, 2.0, 3.0, MAP_PICK_X,
        MAP_PICK_Y, MAP_PICK_UP).Equals(FVector3d(11.0, 22.0, 33.0), 1e-9));
    TestTrue(TEXT("Negative offsets together"), SOLMapPicking::ComposeDestination(reference, -100.0, -200.0, -300.0,
        MAP_PICK_X, MAP_PICK_Y, MAP_PICK_UP).Equals(FVector3d(-90.0, -180.0, -270.0), 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingComposeZeroTest, "SOLTest.MapPicking.ComposeZeroOffsetIsReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero offsets return exactly the reference position, at small and solar-system scale
bool FSOLMapPickingComposeZeroTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d smallReference(-3.25, 8.5, 1.0);
    const FVector3d largeReference(1.496e11, -2.2e10, 4.5e8);
    TestTrue(TEXT("Zero offsets -> small reference exactly"), SOLMapPicking::ComposeDestination(smallReference, 0.0,
        0.0, 0.0, MAP_PICK_X, MAP_PICK_Y, MAP_PICK_UP) == smallReference);
    TestTrue(TEXT("Zero offsets -> large reference exactly"), SOLMapPicking::ComposeDestination(largeReference, 0.0,
        0.0, 0.0, MAP_PICK_X, MAP_PICK_Y, MAP_PICK_UP) == largeReference);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingComposeBasisTest, "SOLTest.MapPicking.ComposeUsesSuppliedBasis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The supplied axes are used (not hardcoded world axes): a basis rotated 90 degrees about Up moves dx onto +Y
bool FSOLMapPickingComposeBasisTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d reference(1.0, 2.0, 3.0);
    const FVector3d destination = SOLMapPicking::ComposeDestination(reference, 5.0, 4.0, 2.0, FVector3d(0.0, 1.0, 0.0),
        FVector3d(-1.0, 0.0, 0.0), MAP_PICK_UP);
    TestTrue(TEXT("Rotated basis destination"), destination.Equals(FVector3d(1.0 - 4.0, 2.0 + 5.0, 3.0 + 2.0), 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingComposeRoundTripTest, "SOLTest.MapPicking.ComposeDecomposeRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Decomposing a planar point then recomposing returns the point; recomposing with dz then decomposing returns (dx, dy)
bool FSOLMapPickingComposeRoundTripTest::RunTest(const FString& /*parameters*/)
{
    // Standard basis, moderate scale
    const FVector3d reference(4000.0, -2500.0, 600.0);
    const FVector3d planarPoint = reference + FVector3d(1234.25, -987.5, 0.0);
    const FVector2D offset = SOLMapPicking::DecomposePlanarOffset(planarPoint, reference, MAP_PICK_X, MAP_PICK_Y);
    const FVector3d recomposed = SOLMapPicking::ComposeDestination(reference, offset.X, offset.Y, 0.0, MAP_PICK_X,
        MAP_PICK_Y, MAP_PICK_UP);
    TestTrue(TEXT("Round trip returns the planar point"), recomposed.Equals(planarPoint, 1e-9));

    // Rotated basis
    FVector3d axisX;
    FVector3d axisY;
    MapPickRotatedBasis(FMath::DegreesToRadians(137.0), axisX, axisY);
    const FVector3d rotatedPoint = reference + 321.0 * axisX + 654.0 * axisY;
    const FVector2D rotatedOffset = SOLMapPicking::DecomposePlanarOffset(rotatedPoint, reference, axisX, axisY);
    TestTrue(TEXT("Rotated round trip returns the planar point"), SOLMapPicking::ComposeDestination(reference,
        rotatedOffset.X, rotatedOffset.Y, 0.0, axisX, axisY, MAP_PICK_UP).Equals(rotatedPoint, 1e-9));

    // Compose with a height, then decompose: the planar part comes back unchanged
    const FVector3d withHeight = SOLMapPicking::ComposeDestination(reference, 75.0, -33.0, 1.0e4, MAP_PICK_X,
        MAP_PICK_Y, MAP_PICK_UP);
    const FVector2D backOffset = SOLMapPicking::DecomposePlanarOffset(withHeight, reference, MAP_PICK_X, MAP_PICK_Y);
    TestEqual(TEXT("Compose then decompose -> dx"), backOffset.X, 75.0, 1e-9);
    TestEqual(TEXT("Compose then decompose -> dy"), backOffset.Y, -33.0, 1e-9);
    TestEqual(TEXT("Compose puts dz along Up"), withHeight.Z - reference.Z, 1.0e4, 1e-9);

    // Solar-system scale round trip
    const FVector3d largeReference(1.5e11, 2.0e10, -3.0e9);
    const FVector3d largePoint = largeReference + FVector3d(1000.0, -1500.0, 0.0);
    const FVector2D largeOffset = SOLMapPicking::DecomposePlanarOffset(largePoint, largeReference, MAP_PICK_X,
        MAP_PICK_Y);
    const FVector3d largeRecomposed = SOLMapPicking::ComposeDestination(largeReference, largeOffset.X, largeOffset.Y,
        0.0, MAP_PICK_X, MAP_PICK_Y, MAP_PICK_UP);
    TestTrue(TEXT("Large-scale round trip accurate to 1 mm"),
        (largeRecomposed - largePoint).Size() <= MAP_PICK_LARGE_ABS_TOL_M);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingComposeLargeTest, "SOLTest.MapPicking.ComposeLargeReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ~1e11 m reference plus small offsets stays accurate to the millimeter in every component
bool FSOLMapPickingComposeLargeTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d reference(1.5e11, -1.2e11, 3.0e9);
    const FVector3d destination = SOLMapPicking::ComposeDestination(reference, 1000.0, -250.0, 42.0, MAP_PICK_X,
        MAP_PICK_Y, MAP_PICK_UP);
    const FVector3d delta = destination - reference;
    TestEqual(TEXT("Large reference -> dx preserved"), delta.X, 1000.0, MAP_PICK_LARGE_ABS_TOL_M);
    TestEqual(TEXT("Large reference -> dy preserved"), delta.Y, -250.0, MAP_PICK_LARGE_ABS_TOL_M);
    TestEqual(TEXT("Large reference -> dz preserved"), delta.Z, 42.0, MAP_PICK_LARGE_ABS_TOL_M);

    // Sub-meter offsets survive too
    const FVector3d fine = SOLMapPicking::ComposeDestination(reference, 0.5, 0.25, -0.125, MAP_PICK_X, MAP_PICK_Y,
        MAP_PICK_UP);
    TestTrue(TEXT("Large reference + sub-meter offsets"),
        ((fine - reference) - FVector3d(0.5, 0.25, -0.125)).Size() <= MAP_PICK_LARGE_ABS_TOL_M);
    return true;
}

// --- PickBodyUnderRay ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickDirectTest, "SOLTest.MapPicking.PickDirectHit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ray at a body's center (or within its pick radius) returns that body's index
bool FSOLMapPickingPickDirectTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin = FVector3d::ZeroVector;
    const FVector3d rayDir(1.0, 0.0, 0.0);
    {
        const TArray<FVector3d> positions = {FVector3d(1000.0, 0.0, 0.0)};
        const TArray<double> radii = {50.0};
        TestEqual(TEXT("Single body dead ahead is picked"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), 0);
    }
    {
        const TArray<FVector3d> positions = {FVector3d(1000.0, 30.0, -20.0)};
        const TArray<double> radii = {50.0};
        TestEqual(TEXT("Body offset within its radius is picked"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), 0);
    }
    {
        // Only the third body is on the ray; the index returned is its index in the parallel arrays
        const TArray<FVector3d> positions = {FVector3d(1000.0, 500.0, 0.0), FVector3d(0.0, 1000.0, 0.0),
            FVector3d(3000.0, 0.0, 10.0), FVector3d(-1000.0, 0.0, 400.0)};
        const TArray<double> radii = {50.0, 50.0, 50.0, 50.0};
        TestEqual(TEXT("Only the on-ray body (index 2) is picked"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickDiagonalTest, "SOLTest.MapPicking.PickDiagonalRay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A general (non-axis) ray from a non-origin point picks the body it passes through
bool FSOLMapPickingPickDiagonalTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin(-500.0, 200.0, 800.0);
    const FVector3d target(2500.0, -1300.0, 100.0);
    const FVector3d rayDir = (target - rayOrigin).GetSafeNormal();
    const TArray<FVector3d> positions = {FVector3d(2500.0, 1300.0, 100.0), target + FVector3d(10.0, -5.0, 8.0)};
    const TArray<double> radii = {100.0, 40.0};
    TestEqual(TEXT("Diagonal ray picks body 1"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii),
        1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickMissTest, "SOLTest.MapPicking.PickMissReturnsNone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ray passing farther from every center than its pick radius picks nothing
bool FSOLMapPickingPickMissTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> positions = {FVector3d(1000.0, 100.0, 0.0), FVector3d(500.0, -80.0, 0.0),
        FVector3d(2000.0, 0.0, 60.0)};
    const TArray<double> radii = {50.0, 20.0, 59.0};
    TestEqual(TEXT("Ray misses every body"), SOLMapPicking::PickBodyUnderRay(FVector3d::ZeroVector,
        FVector3d(1.0, 0.0, 0.0), positions, radii), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickEmptyTest, "SOLTest.MapPicking.PickEmptyReturnsNone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No bodies means nothing to pick
bool FSOLMapPickingPickEmptyTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FVector3d> positions;
    const TArray<double> radii;
    TestEqual(TEXT("Empty arrays -> INDEX_NONE"), SOLMapPicking::PickBodyUnderRay(FVector3d::ZeroVector,
        FVector3d(1.0, 0.0, 0.0), positions, radii), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickNearestTest, "SOLTest.MapPicking.PickNearestWins",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// When several pick spheres are hit, the nearer body wins regardless of array order
bool FSOLMapPickingPickNearestTest::RunTest(const FString& /*parameters*/)
{
    // Both bodies centered on the ray with equal radii: nearer both by center distance and by sphere entry distance
    const FVector3d rayOrigin(10.0, 20.0, 30.0);
    const FVector3d rayDir(0.0, 0.0, -1.0);
    const FVector3d nearBody = rayOrigin + FVector3d(0.0, 0.0, -1000.0);
    const FVector3d farBody = rayOrigin + FVector3d(0.0, 0.0, -5000.0);
    {
        const TArray<FVector3d> positions = {farBody, nearBody};
        const TArray<double> radii = {100.0, 100.0};
        TestEqual(TEXT("Nearer body at index 1 wins"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions,
            radii), 1);
    }
    {
        const TArray<FVector3d> positions = {nearBody, farBody};
        const TArray<double> radii = {100.0, 100.0};
        TestEqual(TEXT("Nearer body at index 0 wins"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions,
            radii), 0);
    }
    {
        // Three overlapping hits, nearest in the middle of the array, with a larger far sphere that is still farther
        const TArray<FVector3d> positions = {rayOrigin + FVector3d(0.0, 0.0, -9000.0),
            rayOrigin + FVector3d(5.0, 0.0, -2000.0), rayOrigin + FVector3d(0.0, -10.0, -4000.0)};
        const TArray<double> radii = {500.0, 60.0, 200.0};
        TestEqual(TEXT("Nearest of three (index 1) wins"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir,
            positions, radii), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickBehindTest, "SOLTest.MapPicking.PickIgnoresBodiesBehindOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ray (half-line) semantics: a body behind the ray origin is not picked even though it sits on the ray's line
bool FSOLMapPickingPickBehindTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin = FVector3d::ZeroVector;
    const FVector3d rayDir(1.0, 0.0, 0.0);
    {
        const TArray<FVector3d> positions = {FVector3d(-1000.0, 0.0, 0.0)};
        const TArray<double> radii = {50.0};
        TestEqual(TEXT("Body behind the origin only -> INDEX_NONE"), SOLMapPicking::PickBodyUnderRay(rayOrigin,
            rayDir, positions, radii), INDEX_NONE);
    }
    {
        // A closer body behind the origin must not beat a farther body ahead of it
        const TArray<FVector3d> positions = {FVector3d(-500.0, 0.0, 0.0), FVector3d(2000.0, 0.0, 0.0)};
        const TArray<double> radii = {50.0, 50.0};
        TestEqual(TEXT("Body ahead (index 1) wins over the nearer body behind"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickTangentTest, "SOLTest.MapPicking.PickTangentIsStable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Just inside / just outside tangency resolve deterministically; exact tangency returns a valid, non-garbage result
bool FSOLMapPickingPickTangentTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin = FVector3d::ZeroVector;
    const FVector3d rayDir(1.0, 0.0, 0.0);
    const TArray<FVector3d> positions = {FVector3d(1000.0, 50.0, 0.0)};

    // A hair wider than tangent: the ray grazes inside the sphere and must pick it
    const TArray<double> widerRadii = {50.0 * (1.0 + 1.0e-9)};
    TestEqual(TEXT("Radius just above tangent picks the body"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir,
        positions, widerRadii), 0);

    // A hair narrower than tangent: the ray passes just outside and must miss
    const TArray<double> narrowerRadii = {50.0 * (1.0 - 1.0e-9)};
    TestEqual(TEXT("Radius just below tangent misses"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir,
        positions, narrowerRadii), INDEX_NONE);

    // Exact tangency is intentionally not pinned: the SDD leaves it to floating-point rounding, and a one-point touch
    // has no practical meaning for a mouse pick. The contract here is only a valid result (the body or none), no crash
    const TArray<double> tangentRadii = {50.0};
    const int32 picked = SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, tangentRadii);
    TestTrue(TEXT("Exact tangent returns 0 or INDEX_NONE"), picked == 0 || picked == INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickEntryNotCenterTest, "SOLTest.MapPicking.PickNearestByEntryNotCenter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// "Nearest" is the smallest sphere-entry distance: a large far-centered sphere entered first beats a small nearer one
bool FSOLMapPickingPickEntryNotCenterTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin = FVector3d::ZeroVector;
    const FVector3d rayDir(1.0, 0.0, 0.0);

    // Large: center 1000 m, radius 900 m, entry at 100 m. Small: center 500 m, radius 10 m, entry at 490 m
    const FVector3d largeCenter(1000.0, 0.0, 0.0);
    const FVector3d smallCenter(500.0, 0.0, 0.0);
    {
        const TArray<FVector3d> positions = {smallCenter, largeCenter};
        const TArray<double> radii = {10.0, 900.0};
        TestEqual(TEXT("Large sphere (index 1) with the nearer entry wins"), SOLMapPicking::PickBodyUnderRay(
            rayOrigin, rayDir, positions, radii), 1);
    }
    {
        const TArray<FVector3d> positions = {largeCenter, smallCenter};
        const TArray<double> radii = {900.0, 10.0};
        TestEqual(TEXT("Large sphere (index 0) with the nearer entry wins"), SOLMapPicking::PickBodyUnderRay(
            rayOrigin, rayDir, positions, radii), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickOriginInsideTest, "SOLTest.MapPicking.PickOriginInsideWins",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ray origin already inside a pick sphere enters it at t = 0, beating any sphere entered at a positive t
bool FSOLMapPickingPickOriginInsideTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin(10.0, -20.0, 30.0);
    const FVector3d rayDir(0.0, 1.0, 0.0);

    // Enclosing sphere centered ahead of the origin, and a small sphere entered at t = 490 m
    const TArray<FVector3d> positions = {rayOrigin + FVector3d(0.0, 500.0, 0.0), rayOrigin + FVector3d(0.0, 50.0, 0.0)};
    const TArray<double> radii = {10.0, 200.0};
    TestEqual(TEXT("Sphere containing the origin (index 1) wins"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir,
        positions, radii), 1);

    // Even a sphere entered almost immediately (t = 1 mm) loses to the enclosing one
    const TArray<FVector3d> closePositions = {rayOrigin + FVector3d(0.0, 1.001, 0.0),
        rayOrigin + FVector3d(0.0, 50.0, 0.0)};
    const TArray<double> closeRadii = {1.0, 200.0};
    TestEqual(TEXT("Enclosing sphere beats one entered at t = 1 mm"), SOLMapPicking::PickBodyUnderRay(rayOrigin,
        rayDir, closePositions, closeRadii), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickInsideBehindTest, "SOLTest.MapPicking.PickOriginInsideCenterBehind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Addendum ordering: an origin inside a sphere whose center is behind it still picks that sphere at t = 0
bool FSOLMapPickingPickInsideBehindTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin = FVector3d::ZeroVector;
    const FVector3d rayDir(1.0, 0.0, 0.0);
    {
        const TArray<FVector3d> positions = {FVector3d(-30.0, 5.0, 0.0)};
        const TArray<double> radii = {100.0};
        TestEqual(TEXT("Enclosing sphere centered behind the origin is picked"), SOLMapPicking::PickBodyUnderRay(
            rayOrigin, rayDir, positions, radii), 0);
    }
    {
        // It also beats a sphere ahead of the origin
        const TArray<FVector3d> positions = {FVector3d(500.0, 0.0, 0.0), FVector3d(-30.0, 5.0, 0.0)};
        const TArray<double> radii = {50.0, 100.0};
        TestEqual(TEXT("Enclosing behind-centered sphere (index 1) beats one ahead"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), 1);
    }
    {
        // Control: the same sphere with the origin just outside it falls back to "centered behind -> never picked"
        const TArray<FVector3d> positions = {FVector3d(-100.5, 0.0, 0.0)};
        const TArray<double> radii = {100.0};
        TestEqual(TEXT("Behind-centered sphere not containing the origin is not picked"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), INDEX_NONE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickMismatchedTest, "SOLTest.MapPicking.PickMismatchedArraysSafe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Mismatched parallel arrays are read only over their common prefix (no out-of-bounds access in either direction)
bool FSOLMapPickingPickMismatchedTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d rayOrigin = FVector3d::ZeroVector;
    const FVector3d rayDir(1.0, 0.0, 0.0);
    {
        // More positions than radii: the on-ray body at index 2 has no radius, so it is ignored
        const TArray<FVector3d> positions = {FVector3d(0.0, 900.0, 0.0), FVector3d(0.0, -900.0, 0.0),
            FVector3d(1000.0, 0.0, 0.0)};
        const TArray<double> radii = {50.0, 50.0};
        TestEqual(TEXT("Extra positions beyond the radii are ignored"), SOLMapPicking::PickBodyUnderRay(rayOrigin,
            rayDir, positions, radii), INDEX_NONE);
    }
    {
        // More radii than positions: the common prefix still picks correctly
        const TArray<FVector3d> positions = {FVector3d(1000.0, 0.0, 0.0)};
        const TArray<double> radii = {50.0, 1.0e9, 1.0e9};
        TestEqual(TEXT("Extra radii beyond the positions are ignored"), SOLMapPicking::PickBodyUnderRay(rayOrigin,
            rayDir, positions, radii), 0);
    }
    {
        // One side empty
        const TArray<FVector3d> positions = {FVector3d(1000.0, 0.0, 0.0)};
        const TArray<double> radii;
        TestEqual(TEXT("Empty radii -> INDEX_NONE"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions,
            radii), INDEX_NONE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickLargeTest, "SOLTest.MapPicking.PickLargeCoordinates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Picking works at solar-system scale: a system-wide camera ray picks a planet-sized sphere near 1 AU, not its neighbor
bool FSOLMapPickingPickLargeTest::RunTest(const FString& /*parameters*/)
{
    const double au = SOLTestHelpers::AU_M;
    const FVector3d rayOrigin(au, 0.0, 5.0e10);
    const FVector3d rayDir(0.0, 0.0, -1.0);
    const TArray<FVector3d> positions = {FVector3d(au + 3.0e7, 0.0, 0.0), FVector3d(au + 2.0e6, -1.0e6, 0.0),
        FVector3d(-au, 0.0, 0.0)};
    const TArray<double> radii = {1.0e7, 1.0e7, 1.0e7};
    TestEqual(TEXT("1 AU body under the ray (index 1) is picked"), SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir,
        positions, radii), 1);

    // A small (1 km) pick sphere at 1 AU, 10 km ahead of a nearby camera: fine picking still resolves
    const FVector3d nearOrigin(au, 1.0e3, 0.0);
    const TArray<FVector3d> finePositions = {FVector3d(au + 1.0e4, 1.2e3, 0.0), FVector3d(au + 1.0e4, 4.0e3, 0.0)};
    const TArray<double> fineRadii = {1.0e3, 1.0e3};
    TestEqual(TEXT("Fine pick at 1 AU (index 0)"), SOLMapPicking::PickBodyUnderRay(nearOrigin,
        FVector3d(1.0, 0.0, 0.0), finePositions, fineRadii), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickLargeDiagonalTest, "SOLTest.MapPicking.PickLargeDiagonalFine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At ~1 AU, a diagonal ray resolves 1 m pick spheres to within 1 mm of their rim (precision-safe differencing)
bool FSOLMapPickingPickLargeDiagonalTest::RunTest(const FString& /*parameters*/)
{
    const double au = SOLTestHelpers::AU_M;
    const FVector3d rayOrigin(0.8 * au, 0.6 * au, -2.0e9);
    const FVector3d rayDir = FVector3d(1.0, 2.0, -0.5).GetSafeNormal();
    const FVector3d perp = FVector3d::CrossProduct(rayDir, FVector3d(0.0, 0.0, 1.0)).GetSafeNormal();

    // Decoy nearer along the ray but 1 mm outside its 1 m radius; target farther and 1 mm inside its radius
    const FVector3d decoy = rayOrigin + 5.0e4 * rayDir + 1.001 * perp;
    const FVector3d target = rayOrigin + 1.0e5 * rayDir - 0.999 * perp;
    const TArray<double> radii = {1.0, 1.0};
    {
        const TArray<FVector3d> positions = {decoy, target};
        TestEqual(TEXT("1 m sphere 1 mm inside the ray (index 1) is picked over the near miss"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), 1);
    }
    {
        // Pull the decoy 2 mm inward so it is now 1 mm inside: being nearer, it must win
        const TArray<FVector3d> positions = {target, rayOrigin + 5.0e4 * rayDir + 0.999 * perp};
        TestEqual(TEXT("Nearer 1 m sphere just inside the ray (index 1) wins"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), 1);
    }
    {
        // Both 1 mm outside: nothing is picked
        const TArray<FVector3d> positions = {decoy, rayOrigin + 1.0e5 * rayDir - 1.001 * perp};
        TestEqual(TEXT("Both 1 m spheres 1 mm outside the ray -> INDEX_NONE"),
            SOLMapPicking::PickBodyUnderRay(rayOrigin, rayDir, positions, radii), INDEX_NONE);
    }
    return true;
}

// --- Non-finite input ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingNonFiniteTest, "SOLTest.MapPicking.NonFiniteInputsRejected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// NaN/Inf in any input never produces a hit, a picked index or a non-finite output from any of the four functions
bool FSOLMapPickingNonFiniteTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const FVector3d nanVec(nan, 0.0, 0.0);
    const FVector3d infVec(0.0, inf, 0.0);
    const FVector3d origin(3.0, 4.0, 100.0);
    const FVector3d down(0.0, 0.0, -1.0);

    // RayPlaneIntersect: each argument poisoned in turn
    const FVector3d badVectors[] = {nanVec, infVec, FVector3d(0.0, 0.0, -inf), FVector3d(nan, nan, nan)};
    for (const FVector3d& bad : badVectors)
    {
        FVector3d point(1.0, 1.0, 1.0);
        TestFalse(TEXT("Non-finite ray origin -> no hit"), SOLMapPicking::RayPlaneIntersect(bad, down,
            FVector3d::ZeroVector, MAP_PICK_UP, point));
        TestTrue(TEXT("Non-finite ray origin -> finite out point"), SOLTestHelpers::IsFiniteVector(point));
        TestFalse(TEXT("Non-finite ray dir -> no hit"), SOLMapPicking::RayPlaneIntersect(origin, bad,
            FVector3d::ZeroVector, MAP_PICK_UP, point));
        TestTrue(TEXT("Non-finite ray dir -> finite out point"), SOLTestHelpers::IsFiniteVector(point));
        TestFalse(TEXT("Non-finite plane origin -> no hit"), SOLMapPicking::RayPlaneIntersect(origin, down, bad,
            MAP_PICK_UP, point));
        TestTrue(TEXT("Non-finite plane origin -> finite out point"), SOLTestHelpers::IsFiniteVector(point));
        TestFalse(TEXT("Non-finite plane normal -> no hit"), SOLMapPicking::RayPlaneIntersect(origin, down,
            FVector3d::ZeroVector, bad, point));
        TestTrue(TEXT("Non-finite plane normal -> finite out point"), SOLTestHelpers::IsFiniteVector(point));
    }

    // DecomposePlanarOffset and ComposeDestination: outputs stay finite
    const FVector2D decomposed = SOLMapPicking::DecomposePlanarOffset(nanVec, FVector3d::ZeroVector, MAP_PICK_X,
        MAP_PICK_Y);
    TestTrue(TEXT("Decompose with NaN point -> finite"),
        FMath::IsFinite(decomposed.X) && FMath::IsFinite(decomposed.Y));
    const FVector2D decomposedAxis = SOLMapPicking::DecomposePlanarOffset(origin, FVector3d::ZeroVector, infVec,
        MAP_PICK_Y);
    TestTrue(TEXT("Decompose with Inf axis -> finite"),
        FMath::IsFinite(decomposedAxis.X) && FMath::IsFinite(decomposedAxis.Y));
    TestTrue(TEXT("Compose with NaN offset -> finite"), SOLTestHelpers::IsFiniteVector(
        SOLMapPicking::ComposeDestination(origin, nan, 0.0, 0.0, MAP_PICK_X, MAP_PICK_Y, MAP_PICK_UP)));
    TestTrue(TEXT("Compose with Inf reference -> finite"), SOLTestHelpers::IsFiniteVector(
        SOLMapPicking::ComposeDestination(infVec, 1.0, 2.0, 3.0, MAP_PICK_X, MAP_PICK_Y, MAP_PICK_UP)));
    TestTrue(TEXT("Compose with NaN up -> finite"), SOLTestHelpers::IsFiniteVector(
        SOLMapPicking::ComposeDestination(origin, 1.0, 2.0, 3.0, MAP_PICK_X, MAP_PICK_Y, nanVec)));

    // PickBodyUnderRay: a poisoned ray picks nothing; poisoned bodies are skipped while valid ones still pick
    const TArray<FVector3d> positions = {FVector3d(1000.0, 0.0, 0.0)};
    const TArray<double> radii = {50.0};
    TestEqual(TEXT("NaN ray origin -> INDEX_NONE"), SOLMapPicking::PickBodyUnderRay(nanVec, FVector3d(1.0, 0.0, 0.0),
        positions, radii), INDEX_NONE);
    TestEqual(TEXT("NaN ray dir -> INDEX_NONE"), SOLMapPicking::PickBodyUnderRay(FVector3d::ZeroVector, nanVec,
        positions, radii), INDEX_NONE);
    const TArray<FVector3d> mixedPositions = {nanVec, FVector3d(500.0, 0.0, 0.0), FVector3d(900.0, 0.0, 0.0),
        FVector3d(2000.0, 0.0, 0.0)};
    const TArray<double> mixedRadii = {1.0e9, nan, inf, 50.0};
    TestEqual(TEXT("Non-finite bodies skipped, finite body (index 3) picked"), SOLMapPicking::PickBodyUnderRay(
        FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), mixedPositions, mixedRadii), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickingPickManyTest, "SOLTest.MapPicking.PickAmongManyBodies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Among many bodies off the ray, the single on-ray body is found at its index
bool FSOLMapPickingPickManyTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 bodyCount = 100;
    constexpr int32 onRayIndex = 73;
    TArray<FVector3d> positions;
    TArray<double> radii;
    positions.Reserve(bodyCount);
    radii.Reserve(bodyCount);

    // Scatter bodies on a ring well off the +X ray, then place one on the ray
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const double angleRad = UE_DOUBLE_TWO_PI * index / bodyCount;
        positions.Add(FVector3d(100.0 * (index + 1), 5000.0 * FMath::Cos(angleRad), 5000.0 * FMath::Sin(angleRad)));
        radii.Add(100.0);
    }
    positions[onRayIndex] = FVector3d(7400.0, 20.0, -30.0);

    TestEqual(TEXT("On-ray body (index 73) is picked"), SOLMapPicking::PickBodyUnderRay(FVector3d::ZeroVector,
        FVector3d(1.0, 0.0, 0.0), positions, radii), onRayIndex);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
