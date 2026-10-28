/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class APlayerController;
class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMapModeSubsystem;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;

/**
 * Verification-only map alignment run (-SOLSmokeMapAlign, issue #25): opens the jump map with Shift+M, then for a fixed list
 * of views (a focus body, a target body whose orbit line is highlighted, a camera distance) points the map camera at
 * the focus, waits for the view to settle and checks that the target body's map icon lies on its own orbit line: the
 * icon is projected as the HUD projects it (the FOV as horizontal across the viewport), the line is rebuilt on the
 * CPU with ASOLOrbitLineVisuals' segment count (from the current elements; the actor's rebuild drift of up to 5% of
 * the radius is not mirrored) and as M_SOLOrbitLine places it (SampleOrbit, parent offset from the viewpoint, PlaceLinePointCm) and projected with the renderer's own
 * view-projection matrix (ULocalPlayer::GetProjectionData), each chord clipped at the near plane as the rasterizer
 * clips it, so a HUD/renderer FOV mismatch at any aspect ratio fails it; the screen distance must be under
 * SOL::MAP_ALIGN_SMOKE_TOLERANCE_PX plus the line's designed chord error (ChordSagittaM) seen from the camera. It also
 * checks that the target's line is highlighted, takes a screenshot per view (logging the body's pixel), then orbits
 * the camera every frame for a fixed number of frames with a screenshot half way, closes the map and quits. Each
 * check logs a LogSOL PASS/FAIL line.
 */
class SOLTEST_API FSOLMapAlignSmoke
{
public:

    // Caches the controller and subsystems and opens the map
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

    // Returns the screen distance (px) from a point to a polyline given in clip space, each segment clipped to
    // W >= nearW first (as the rasterizer clips a chord that crosses the camera plane); max double if none is visible
    static double ScreenDistanceToClipPolylinePx(const FVector2D& pointPx, TConstArrayView<FVector4> clipPoints,
        const FIntRect& viewRect, double nearW);

private:

    // Selects the body as the target (cycling the targeting list until it is selected); false if it never is
    bool SelectTarget(int32 bodyIndex) const;

    // Returns the body's permanent orbit-line slot (registry bodies with a parent, in registry order); INDEX_NONE if
    // it has no parent
    int32 PermanentLineSlot(int32 bodyIndex) const;

    // Returns the screen distance (px) from the target body's map icon (projected as the HUD does) to its orbit line
    // (sampled as the line actor does, placed as the shader does, projected as the renderer does) and the line's
    // designed chord error seen from the camera (px); false if either cannot be projected
    bool MeasureBodyToLinePx(int32 bodyIndex, FVector2D& outBodyPx, double& outDistancePx,
        double& outChordErrorPx) const;

    // Fills the renderer's view-projection matrix and view rectangle for the local player; false without a viewport
    bool GetRendererProjection(FMatrix& outViewProjection, FIntRect& outViewRect) const;

    // Projects a render location (cm) as the flight HUD and the map icons do; false when behind the camera
    bool ProjectLikeHud(const FVector& renderCm, FVector2D& outScreenPx) const;

    // Takes a screenshot at the viewport's own size, so the HUD and the 3D view line up pixel for pixel in the image
    void Screenshot() const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLSimClockSubsystem> mClock;
    TWeakObjectPtr<USOLTargetingSubsystem> mTargeting;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;

    int32 mStep = 0;                // 0 = opening, 1..views = views, then the motion sweep, then closing
    int32 mStepFrames = 0;
    double mStepTimeS = 0.0;
    bool mHasPendingMapKeyRelease = false;
    bool mIsFinished = false;
    int32 mPassed = 0;
    int32 mFailed = 0;
};
