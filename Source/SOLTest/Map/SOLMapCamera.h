/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Orbit-camera state for the jump map (SDD 3 Appendix E); universe/ecliptic frame, meters, Up = (0,0,1)
struct FSOLOrbitCameraState
{
    FVector3d FocusPositionM = FVector3d::ZeroVector;   // the point the camera orbits
    double YawRad = 0.0;                                 // rotation around Up; wrapped to (-PI, PI]
    double PitchRad = 0.5;                               // tilt from the horizontal plane; clamped (+ = above)
    double DistanceM = 1.0e10;
};

// Limits and tuning for the jump-map orbit camera. The pitch limits must stay at least 1e-3 rad inside +/-PI/2
// (the defaults are static_asserted in SOLMapCamera.cpp); widening them past that degenerates the look-at up vector.
struct FSOLOrbitCameraParams
{
    double MinPitchRad = -1.5;      // ~-85.9 deg, stops short of the pole to avoid a flip
    double MaxPitchRad = 1.5;
    double MinDistanceM = 1.0e6;    // 1,000 km
    double MaxDistanceM = 6.0e13;   // well beyond Neptune's orbit
    double ZoomStepFactor = 1.2;    // per wheel notch, gentler than the HUD radar's 10x since this is continuous
};

namespace SOLMapCamera
{
    // Spherical position around the focus: at Yaw=0, Pitch=0 the camera sits at
    // focusPositionM + distanceM * (-1,0,0) (looking toward +EclipticX). Yaw rotates that offset around Up; Pitch
    // tilts it up/down from the horizontal plane.
    SOLTEST_API FVector3d ComputeCameraPositionM(const FSOLOrbitCameraState& state);

    // Look-at orientation: forward = normalize(focus - camera position), up = Up re-orthogonalized against forward,
    // both converted with SOLRender::EclipticToUnreal before building the FQuat4d (Unreal-handed)
    SOLTEST_API FQuat4d ComputeCameraOrientation(const FSOLOrbitCameraState& state);

    // Adds the yaw/pitch deltas (already converted from pixels by the caller); Yaw wraps to (-PI, PI], Pitch clamps to
    // [MinPitchRad, MaxPitchRad]. FocusPositionM and DistanceM are unchanged.
    SOLTEST_API FSOLOrbitCameraState ApplyOrbitDelta(const FSOLOrbitCameraState& state, double yawDeltaRad,
        double pitchDeltaRad, const FSOLOrbitCameraParams& params);

    // Slides FocusPositionM across the ecliptic plane (Homeworld-style, SDD 3 Appendix E Amendment 1): right is the
    // Yaw=0 (0,-1,0) and panDeltaUpM drives the horizontal forward (1,0,0), both rotated around Up by Yaw only
    // (Pitch ignored), so the focus never moves along Up. Deltas are meters, already zoom-scaled.
    // Yaw/Pitch/DistanceM are unchanged.
    SOLTEST_API FSOLOrbitCameraState ApplyPan(const FSOLOrbitCameraState& state, double panDeltaRightM,
        double panDeltaUpM);

    // Multiplies DistanceM by ZoomStepFactor^wheelSteps (negative zooms in), clamped to [MinDistanceM, MaxDistanceM]
    SOLTEST_API FSOLOrbitCameraState ApplyZoom(const FSOLOrbitCameraState& state, int32 wheelSteps,
        const FSOLOrbitCameraParams& params);
}
