/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

#include "OrbitLines/SOLOrbitLineMath.h"

// Pure host-side decisions of ASOLOrbitLineVisuals (SDD 17): which line the targeting selection maps to, and which
// drifted lines are rebuilt this frame (round robin, capped)
namespace SOLOrbitLineLogic
{
    // Maps a targeting candidate (registry bodies first, candidate index == body index, then other targetables) to a
    // line key; outHasOrbit is true for a registry body with a parent. Anything else is {None, INDEX_NONE}
    SOLTEST_API SOLOrbitLineMath::FLineKey TargetKeyForCandidate(int32 selectedCandidate,
        TConstArrayView<int32> registryParents, bool& outHasOrbit);

    // Resets outSlots and fills it with up to maxRebuilds slots whose flag is set, scanning all N slots once from
    // startSlot (wrapped into [0, N)); returns the count
    SOLTEST_API int32 SelectDriftRebuilds(TConstArrayView<bool> needsRebuild, int32 startSlot, int32 maxRebuilds,
        TArray<int32>& outSlots);

    // Returns the next round-robin start: (cursor + 1) wrapped into [0, lineCount), 0 without lines
    SOLTEST_API int32 NextRebuildCursor(int32 cursor, int32 lineCount);

    // Returns the unit quaternion Rz(node) Rx(inclination) Rz(argument) (J2000 ecliptic): it turns +X into the
    // periapsis direction P and +Y into Q of SOLOrbitLineMath::SampleOrbit
    SOLTEST_API FQuat4d OrbitOrientation(const FSOLKeplerElements& elements);

    // Returns the rotation from built's orbit plane and periapsis to now's: OrbitOrientation(now) * built's inverse.
    // Applied to a line sampled from built, it gives the line of built's shape in now's orientation
    SOLTEST_API FQuat4d OrbitRotationBetween(const FSOLKeplerElements& built, const FSOLKeplerElements& now);

    // Returns q.RotateVector(v) - v as 2 cross(q.xyz, cross(q.xyz, v) + q.w v), which keeps relative precision for a
    // small rotation (the vertex shader mirrors it on the float high part of each point)
    SOLTEST_API FVector3d RotationOffset(const FQuat4d& q, const FVector3d& v);

    // Returns the shape part of SOLOrbitLineMath::OrbitDriftM (semi-major axis and eccentricity terms; orientation is
    // handled by OrbitRotationBetween); +inf if either set is not drawable
    SOLTEST_API double OrbitShapeDriftM(const FSOLKeplerElements& built, const FSOLKeplerElements& now);

    // NeedsRebuild for the shape only: a pure orientation change never needs a rebuild
    SOLTEST_API bool NeedsShapeRebuild(const FSOLKeplerElements& built, const FSOLKeplerElements& now,
        double bodyRadiusM);

    // Returns the scene depth (cm) below which no log-compressed body can lie: max * (1 - the largest R/d of the
    // bodies beyond the 1:1 zone). Scene depth may hide a line only below it (a compressed planet's near surface can
    // sit just inside max, in front of a line point that is truly in front of the planet)
    SOLTEST_API double SceneDepthTrustLimitCm(const FVector3d& viewpointM, TConstArrayView<FVector3d> centersM,
        TConstArrayView<double> radiiM, double maxRenderDistanceCm);

    // True when no point of the orbit can be within FADE_END semi-major axes (lower bound: distance - a(1 + e)), so
    // the whole line is faded out and can be hidden; never for the highlighted line or non-finite input
    SOLTEST_API bool IsLineBeyondFade(double cameraToParentDistanceM, double semiMajorAxisM, double eccentricity,
        bool bHighlighted);

    // Returns whether the lines are drawn: the setting is on and the main menu is not showing
    SOLTEST_API bool AreLinesVisible(bool bSettingShown, bool bInMainMenu);
}
