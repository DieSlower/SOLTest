/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic helpers for the ring dense rock layer (SDD 6 Amendments 14-15): a GPU Niagara carpet of rocks in a wrapped
// window around the camera, shown only near a ring. The carpet is an infinite periodic field in the ring's own
// co-rotating frame; these functions compute what the host (ASOLRingVisuals) must feed the Niagara system each frame.
namespace SOLRingDense
{
    // Returns positionCm modulo the tile size 2 * windowRadiusCm, always in [0, 2W); 0 for a non-positive window
    SOLTEST_API double WindowPhaseCm(double positionCm, double windowRadiusCm);

    // Advances the ring's co-rotation angle by the Keplerian rate sqrt(GM / r^3) over deltaSeconds and normalizes the
    // result into [0, 2*PI); with a non-positive radius or GM the angle is only normalized
    SOLTEST_API double AdvanceSpinAngle(double angleRad, double planetGM, double planeRadiusM, double deltaSeconds);

    // Returns the dense layer's weight: 1 within fadeInDistanceM of the ring volume, 0 beyond fadeOutDistanceM,
    // smoothstep between (a hard step if fadeOut <= fadeIn)
    SOLTEST_API double ComputeDenseAlpha(double distanceOutsideRingVolumeM, double fadeInDistanceM, double fadeOutDistanceM);

    // Returns the camera's position in the ring's co-rotating frame, in centimeters: the in-plane components of
    // relativeM along the ring axes rotated by -spinAngleRad, and the height along the ring normal
    SOLTEST_API FVector3d RingFramePositionCm(const FVector3d& relativeM, const FVector3d& ringAxisE1,
        const FVector3d& ringAxisE2, const FVector3d& ringNormal, double spinAngleRad);

    // Converts a ring-frame position (right-handed, ecliptic-aligned axes, as RingFramePositionCm returns) to the
    // Niagara component's local axes, which are the same frame mirrored in Y because the component's rotation is
    // converted to Unreal's left-handed axes (SOLRender::EclipticToUnreal). An involution: applying it twice is the
    // identity. Every vector sent to the dense system (window phase, window centre) must go through this.
    SOLTEST_API FVector3d RingFrameToNiagaraLocalCm(const FVector3d& ringFrameCm);

    // Returns whether the dense layer should draw: it has weight and the time warp is not too high for a
    // co-rotating carpet to mean anything
    SOLTEST_API bool ShouldShowDenseTier(double denseAlpha, double timeWarpFactor, double maxTimeWarpFactor);

    // Returns the fraction of dense rocks to show for a ring of the given relative density: clamped to [0, 1], and 1
    // for a non-finite value
    SOLTEST_API double DenseFillFraction(double ringDensity);
}
