/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

#include "Universe/SOLKepler.h"

// Pure orbit-line math (SDD 17): the test oracle and reference implementation that Shaders/Private/SOLOrbitLine.ush
// mirrors. Units are metres, radians and pixels, except *Cm names (centimetres). SampleOrbit works in the
// SOLKepler::ElementsToState frame; every other function only needs one consistent frame for its vector inputs.
namespace SOLOrbitLineMath
{
    // Where an orbit line's elements come from
    enum class ELineSource : uint8
    {
        None,
        RegistryBody,
        MinorBody,
    };

    // Identifies one orbit line: a registry body index or a minor-body (asteroid list) index
    struct FLineKey
    {
        ELineSource Source = ELineSource::None;
        int32 Index = INDEX_NONE;

        // Keys are equal when both the source and the index match
        bool operator==(const FLineKey& other) const { return Source == other.Source && Index == other.Index; }
    };

    // Which permanent line to highlight for the current target, or that the target needs its own transient line
    struct FTargetLineResult
    {
        int32 HighlightSlot = INDEX_NONE;     // Index into the permanent line list to draw highlighted
        bool bShowTransient = false;          // The target needs its own (highlighted) line
    };

    // True for a finite a > 0, a finite 0 <= e < 1 and finite angles (the mean anomaly is ignored)
    SOLTEST_API bool IsDrawableOrbit(const FSOLKeplerElements& elements);

    // Returns a * (2*PI/N)^2 / 8, the chord-error bound of an N-segment uniform-E polyline (0 for a bad a, +inf for
    // N < 3)
    SOLTEST_API double ChordSagittaM(double semiMajorAxisM, int32 segments);

    // Returns the smallest N whose chord error is within ORBIT_LINE_SAGITTA_RADIUS_FRACTION of the body radius, clamped
    SOLTEST_API int32 SegmentCountFor(double semiMajorAxisM, double bodyRadiusM);

    // Appends N + 1 points uniform in eccentric anomaly from periapsis (entry N copies entry 0), their unit prograde
    // tangents and cumulative polyline arc lengths; returns N + 1, or 0 (arrays untouched) for bad input
    SOLTEST_API int32 SampleOrbit(const FSOLKeplerElements& elements, int32 segments, TArray<FVector3d>& outPointsM,
        TArray<FVector3d>& outTangents, TArray<double>& outArcLengthsM);

    // Returns arcLength * dashCount / perimeter in dash periods (0 for bad input)
    SOLTEST_API double DashPhase(double arcLengthM, double perimeterM, int32 dashCount);

    // Returns the box-filtered coverage of the dash square wave over a phase footprint (point sample, exact box mean,
    // or the duty cycle once a whole period fits in the footprint)
    SOLTEST_API double DashCoverage(double dashPhase, double dutyCycle, double phaseFootprint);

    // Returns the multi-scale dash coverage (SDD 17 Amendment 1): the dash period is halved in power-of-two levels
    // (0..maxLevel, crossfaded) while it is longer than minPeriodPx on screen, then the line fades to solid as the
    // remaining period grows from solidStartPx to solidEndPx. A NaN or non-positive footprint is a level-0 point sample
    SOLTEST_API double MultiScaleDashCoverage(double dashPhase, double dutyCycle, double phaseFootprint,
        double minPeriodPx, int32 maxLevel, double solidStartPx, double solidEndPx);

    // Returns ORBIT_LINE_WIDTH_PX scaled by the viewport height against ORBIT_LINE_REFERENCE_HEIGHT_PX
    SOLTEST_API double ViewportPixelWidth(double viewportHeightPx);

    // Returns the analytic anti-aliased coverage across the line; lines under 1 px keep a 1 px footprint, dimmed
    SOLTEST_API double LineCoverage(double distanceFromCenterPx, double widthPx);

    // Returns the ribbon geometry's half width: max(w, 1)/2 plus the feather margin
    SOLTEST_API double RibbonHalfExtentPx(double widthPx);

    // Returns the world size of one pixel at the given distance (0 for bad input)
    SOLTEST_API double WorldSizePerPixel(double distance, double tanHalfHorizontalFov, double viewportWidthPx);

    // Returns the unit ribbon expansion direction, perpendicular to the view ray and the tangent (deterministic
    // fallback when looking along the line; zero for a bad view ray)
    SOLTEST_API FVector3d RibbonSideDirection(const FVector3d& cameraToPoint, const FVector3d& tangent);

    // Returns the distance fade: 1 near, 0 far, linear in log distance between FADE_START and FADE_END semi-major axes;
    // a highlighted line keeps at least ORBIT_LINE_HIGHLIGHT_MIN_ALPHA
    SOLTEST_API double DistanceFade(double distanceM, double semiMajorAxisM, bool bHighlighted);

    // True when the segment from the camera to the point passes strictly inside the sphere (camera outside it)
    SOLTEST_API bool IsPointOccludedBySphere(const FVector3d& cameraM, const FVector3d& pointM,
        const FVector3d& centerM, double radiusM);

    // True when any of the spheres (common prefix of the two arrays) occludes the point
    SOLTEST_API bool IsPointOccluded(const FVector3d& cameraM, const FVector3d& pointM,
        TConstArrayView<FVector3d> centersM, TConstArrayView<double> radiiM);

    // Resets outIndices and fills it with the valid occluders, largest angular radius first (ties to the lower index),
    // truncated to maxCount; returns the count
    SOLTEST_API int32 SelectOccluders(const FVector3d& cameraM, TConstArrayView<FVector3d> centersM,
        TConstArrayView<double> radiiM, double minAngularRadiusRad, int32 maxCount, TArray<int32>& outIndices);

    // True only when the scene depth is inside the 1:1 zone and the line lies beyond it (with a relative bias)
    SOLTEST_API bool IsHiddenBySceneDepth(double lineDistanceCm, double sceneDepthCm, double maxRenderDistanceCm);

    // Returns the line point's render location: SOLRender::ComputePlacement(relativeToObserverM, 0, max).LocationCm
    SOLTEST_API FVector3d PlaceLinePointCm(const FVector3d& relativeToObserverM, double maxRenderDistanceCm);

    // Splits a double into a float pair whose sum is about 48 bits accurate (low is 0 for a non-finite value)
    SOLTEST_API void SplitDouble(double value, float& outHigh, float& outLow);

    // Splits a vector into float pairs per component
    SOLTEST_API void SplitVector(const FVector3d& value, FVector3f& outHigh, FVector3f& outLow);

    // Returns an upper bound on how far any sampled point moves between the two element sets (+inf if either is bad)
    SOLTEST_API double OrbitDriftM(const FSOLKeplerElements& built, const FSOLKeplerElements& now);

    // True when a line built from built should be rebuilt for now (drift beyond 5% of the body radius)
    SOLTEST_API bool NeedsRebuild(const FSOLKeplerElements& built, const FSOLKeplerElements& now, double bodyRadiusM);

    // Resets outLines to every registry body with a parent, then the largest named minor bodies (largest first)
    SOLTEST_API void BuildPermanentLines(TConstArrayView<int32> registryParents, TConstArrayView<double> minorRadiiM,
        TConstArrayView<bool> minorIsNamed, int32 maxMinorLines, TArray<FLineKey>& outLines);

    // Returns which permanent line to highlight for the target, or that it needs a transient line
    SOLTEST_API FTargetLineResult ResolveTargetLine(bool bLinesEnabled, const FLineKey& target, bool bTargetHasOrbit,
        TConstArrayView<FLineKey> permanentLines);

    // Returns the highlight color if highlighted, else the normal line color
    SOLTEST_API FLinearColor LineColor(bool bHighlighted);
}
