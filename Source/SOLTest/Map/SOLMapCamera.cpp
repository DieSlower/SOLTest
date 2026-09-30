/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapCamera.h"

#include "Universe/SOLRenderPlacement.h"

// Pole guard (SDD 3 Appendix E fix 2): the default pitch limits must stay at least 1e-3 rad inside the poles so the
// look-at up re-orthogonalization never degenerates. Callers must not widen the limits past this margin either.
static_assert(FSOLOrbitCameraParams{}.MaxPitchRad < UE_DOUBLE_HALF_PI - 1.0e-3,
    "FSOLOrbitCameraParams default MaxPitchRad is too close to the +PI/2 pole");
static_assert(FSOLOrbitCameraParams{}.MinPitchRad > -(UE_DOUBLE_HALF_PI - 1.0e-3),
    "FSOLOrbitCameraParams default MinPitchRad is too close to the -PI/2 pole");

namespace
{
    const FVector3d MAP_CAMERA_UP(0.0, 0.0, 1.0);

    //////////////////////////////////////////////////////////////////////////
    // Rotates a vector's X/Y components around Up by yawRad (Z is unaffected)
    FVector3d RotateAroundUp(const FVector3d& value, const double yawRad)
    {
        double sinYaw = 0.0;
        double cosYaw = 1.0;
        FMath::SinCos(&sinYaw, &cosYaw, yawRad);
        return FVector3d(value.X * cosYaw - value.Y * sinYaw, value.X * sinYaw + value.Y * cosYaw, value.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the camera's offset from the focus (small-magnitude, computed before any addition to the focus)
    FVector3d ComputeCameraOffsetM(const FSOLOrbitCameraState& state)
    {
        double sinPitch = 0.0;
        double cosPitch = 1.0;
        FMath::SinCos(&sinPitch, &cosPitch, state.PitchRad);
        const FVector3d offsetAtYawZero(-state.DistanceM * cosPitch, 0.0, state.DistanceM * sinPitch);
        return RotateAroundUp(offsetAtYawZero, state.YawRad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Wraps an angle into (-PI, PI], leaving values already in range bit-for-bit unchanged
    double WrapYawRad(const double yawRad)
    {
        if (yawRad > -UE_DOUBLE_PI && yawRad <= UE_DOUBLE_PI)
        {
            return yawRad;
        }
        double shifted = FMath::Fmod(yawRad + UE_DOUBLE_PI, UE_DOUBLE_TWO_PI);
        if (shifted <= 0.0)
        {
            shifted += UE_DOUBLE_TWO_PI;
        }
        return shifted - UE_DOUBLE_PI;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the focus plus the yaw/pitch/distance spherical offset
FVector3d SOLMapCamera::ComputeCameraPositionM(const FSOLOrbitCameraState& state)
{
    return state.FocusPositionM + ComputeCameraOffsetM(state);
}

//////////////////////////////////////////////////////////////////////////
// Builds the Unreal-handed look-at quaternion from the camera toward the focus with Up re-orthogonalized
FQuat4d SOLMapCamera::ComputeCameraOrientation(const FSOLOrbitCameraState& state)
{
    // focus - camera == -offset; using the offset directly avoids cancellation against a large focus
    const FVector3d forwardEcl = (-ComputeCameraOffsetM(state)).GetSafeNormal();
    const FVector3d upEcl =
        (MAP_CAMERA_UP - forwardEcl * FVector3d::DotProduct(MAP_CAMERA_UP, forwardEcl)).GetSafeNormal();

    // MakeFromXZ builds the left-handed Unreal basis (right = up x forward, the mirror of ecliptic forward x up)
    const FVector3d forwardUe = SOLRender::EclipticToUnreal(forwardEcl);
    const FVector3d upUe = SOLRender::EclipticToUnreal(upEcl);
    FQuat4d rotation = FRotationMatrix44d::MakeFromXZ(forwardUe, upUe).ToQuat();
    rotation.Normalize();
    return rotation;
}

//////////////////////////////////////////////////////////////////////////
// Adds yaw (wrapped to (-PI, PI]) and pitch (clamped to the params' range) deltas
FSOLOrbitCameraState SOLMapCamera::ApplyOrbitDelta(const FSOLOrbitCameraState& state, double yawDeltaRad,
    double pitchDeltaRad, const FSOLOrbitCameraParams& params)
{
    FSOLOrbitCameraState result = state;
    result.YawRad = WrapYawRad(state.YawRad + yawDeltaRad);
    result.PitchRad = FMath::Clamp(state.PitchRad + pitchDeltaRad, params.MinPitchRad, params.MaxPitchRad);
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Slides the focus across the ecliptic plane along the yaw-only screen right and horizontal forward (Amendment 1)
FSOLOrbitCameraState SOLMapCamera::ApplyPan(const FSOLOrbitCameraState& state, double panDeltaRightM,
    double panDeltaUpM)
{
    // Same rotation as the camera offset; RotateAroundUp passes Z through, so both axes keep an exact zero Z
    const FVector3d rightEcl = RotateAroundUp(FVector3d(0.0, -1.0, 0.0), state.YawRad);
    const FVector3d forwardHorizontalEcl = RotateAroundUp(FVector3d(1.0, 0.0, 0.0), state.YawRad);

    // Combine the small deltas fully before adding them to the (possibly huge) focus
    const FVector3d displacementM = rightEcl * panDeltaRightM + forwardHorizontalEcl * panDeltaUpM;
    FSOLOrbitCameraState result = state;
    result.FocusPositionM = state.FocusPositionM + displacementM;
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Scales the distance by ZoomStepFactor^wheelSteps, clamped to the params' distance range
FSOLOrbitCameraState SOLMapCamera::ApplyZoom(const FSOLOrbitCameraState& state, int32 wheelSteps,
    const FSOLOrbitCameraParams& params)
{
    FSOLOrbitCameraState result = state;
    const double scale = FMath::Pow(params.ZoomStepFactor, static_cast<double>(wheelSteps));
    result.DistanceM = FMath::Clamp(state.DistanceM * scale, params.MinDistanceM, params.MaxDistanceM);
    return result;
}
