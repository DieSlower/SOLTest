/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Destination-pick progress for the jump map (SDD 3 section 3); offsets are in ecliptic axes, meters
struct FSOLMapPickState
{
    bool bHasReference = false;              // false until the first mouse-down
    bool bReferenceIsBody = false;           // true if the reference is a clicked body, false if it's the ship
    int32 ReferenceBodyIndex = INDEX_NONE;   // valid only when bReferenceIsBody
    bool bPlanarLocked = false;              // true after mouse-up locks (dx, dy)
    double PlanarOffsetXM = 0.0;
    double PlanarOffsetYM = 0.0;
    bool bHeightLocked = false;              // true after the Shift-click locks dz
    double HeightOffsetZM = 0.0;
};

namespace SOLMapPicking
{
    // Intersects a ray with the plane {point: planeOrigin, normal: planeNormal}. Returns false (no intersection) if the
    // ray is parallel to the plane (|dot(rayDir, normalize(planeNormal))| below a small epsilon), the intersection is
    // behind the ray origin, planeNormal is zero, or any input is non-finite. Any nonzero normal magnitude is accepted.
    SOLTEST_API bool RayPlaneIntersect(const FVector3d& rayOrigin, const FVector3d& rayDir,
        const FVector3d& planeOrigin, const FVector3d& planeNormal, FVector3d& outPoint);

    // Decomposes (point - planeOrigin) into components along eclipticX/eclipticY (both unit, orthogonal to Up and to
    // each other; callers pass (1,0,0) and (0,1,0) in the ecliptic frame). Returns (dx, dy), or (0, 0) on non-finite
    // input.
    SOLTEST_API FVector2D DecomposePlanarOffset(const FVector3d& point, const FVector3d& planeOrigin,
        const FVector3d& eclipticX, const FVector3d& eclipticY);

    // Composes a locked pick back into an absolute universe-frame destination: referencePositionM + dx*eclipticX +
    // dy*eclipticY + dz*up. Pure vector math; the caller supplies the reference's CURRENT (live) position. Returns the
    // zero vector on any non-finite input.
    SOLTEST_API FVector3d ComposeDestination(const FVector3d& referencePositionM, double planarOffsetXM,
        double planarOffsetYM, double heightOffsetZM, const FVector3d& eclipticX, const FVector3d& eclipticY,
        const FVector3d& up);

    // Finds the body whose pick sphere (radius bodyPickRadiiM around its position) is hit by the ray; the smallest
    // entry distance wins (an origin inside a sphere enters at t = 0); INDEX_NONE if none. The arrays are parallel;
    // only their common prefix is read. Non-finite ray input yields INDEX_NONE and non-finite bodies are skipped.
    SOLTEST_API int32 PickBodyUnderRay(const FVector3d& rayOrigin, const FVector3d& rayDir,
        TConstArrayView<FVector3d> bodyPositionsM, TConstArrayView<double> bodyPickRadiiM);
}
