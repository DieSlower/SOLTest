/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Level/SOLSurfaceLock.h"

namespace
{
    // Below this |up x target| (unit vectors) the two are treated as parallel or antiparallel
    constexpr double SURFACE_LOCK_MIN_CROSS_SIZE = 1e-12;

    // Guard for divisions by a time constant that a caller may set to zero
    constexpr double SURFACE_LOCK_MIN_TIME_CONSTANT_S = UE_DOUBLE_SMALL_NUMBER;

    //////////////////////////////////////////////////////////////////////////
    // Returns the fully released, unsuppressed lock state
    FSOLSurfaceLockState SurfaceLockReleasedState()
    {
        return FSOLSurfaceLockState();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an engaged lock state on the given body and mode, with no warning and no suppression
    FSOLSurfaceLockState SurfaceLockEngagedState(const int32 bodyIndex, const bool bManual)
    {
        FSOLSurfaceLockState state;
        state.BodyIndex = bodyIndex;
        state.bEngaged = true;
        state.bManual = bManual;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a released state with auto-engage suppressed for the given body
    FSOLSurfaceLockState SurfaceLockSuppressedState(const int32 bodyIndex)
    {
        FSOLSurfaceLockState state;
        state.bAutoSuppressed = true;
        state.SuppressedBodyIndex = bodyIndex;
        return state;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the manual lock range for a body: max(ManualRangeMinM, body radius)
double SOLSurfaceLock::ComputeManualRangeM(const double bodyRadiusM, const FSOLSurfaceLockParams& params)
{
    return FMath::Max(params.ManualRangeMinM, bodyRadiusM);
}

//////////////////////////////////////////////////////////////////////////
// Advances the lock state machine one frame (nearest-body inputs drive engaging, locked-body inputs drive release)
FSOLSurfaceLockState SOLSurfaceLock::UpdateSurfaceLockState(const FSOLSurfaceLockState& prevState,
    const int32 nearestBodyIndex, const double nearestAltitudeM, const double nearestBodyRadiusM,
    const double lockedBodyAltitudeM, const double lockedBodyRadiusM, const bool bTogglePressed,
    const FSOLSurfaceLockParams& params)
{
    // A press toggles: release (latching suppression) if engaged, else engage the nearest body within manual range
    if (bTogglePressed)
    {
        if (prevState.bEngaged)
        {
            return SurfaceLockSuppressedState(prevState.BodyIndex);
        }
        if (nearestBodyIndex == INDEX_NONE || nearestAltitudeM > ComputeManualRangeM(nearestBodyRadiusM, params))
        {
            return prevState;
        }
        return SurfaceLockEngagedState(nearestBodyIndex, nearestAltitudeM > params.AutoRangeM);
    }

    // No press, unengaged: auto-engage the nearest body inside the auto range unless it is still suppressed
    if (!prevState.bEngaged)
    {
        const bool bStillSuppressed = prevState.bAutoSuppressed && nearestBodyIndex == prevState.SuppressedBodyIndex
            && nearestAltitudeM <= params.AutoRangeM * params.AutoReleaseFactor;
        if (nearestBodyIndex != INDEX_NONE && nearestAltitudeM <= params.AutoRangeM && !bStillSuppressed)
        {
            return SurfaceLockEngagedState(nearestBodyIndex, false);
        }
        return bStillSuppressed ? SurfaceLockSuppressedState(prevState.SuppressedBodyIndex) : SurfaceLockReleasedState();
    }

    // No press, engaged: warn and release against the locked body only, with the thresholds of the lock's mode
    double warnThresholdM = params.AutoRangeM;
    double releaseThresholdM = params.AutoRangeM * params.AutoReleaseFactor;
    if (prevState.bManual)
    {
        warnThresholdM = ComputeManualRangeM(lockedBodyRadiusM, params);
        releaseThresholdM = warnThresholdM * params.ManualReleaseFactor;
    }
    if (lockedBodyAltitudeM > releaseThresholdM)
    {
        return SurfaceLockReleasedState();
    }

    FSOLSurfaceLockState next = SurfaceLockEngagedState(prevState.BodyIndex, prevState.bManual);
    next.bWarning = lockedBodyAltitudeM > warnThresholdM;
    return next;
}

//////////////////////////////////////////////////////////////////////////
// Returns the unit direction from the body's center to the ship (the local vertical)
FVector3d SOLSurfaceLock::TargetUpDir(const FVector3d& shipPositionM, const FVector3d& bodyPositionM)
{
    return (shipPositionM - bodyPositionM).GetSafeNormal();
}

//////////////////////////////////////////////////////////////////////////
// Rotates the orientation along the shortest arc from its up toward targetUpDir by the fraction 1 - e^(-dt / tau)
FQuat4d SOLSurfaceLock::ApplyAlignmentCorrection(const FQuat4d& currentOrientation, const FVector3d& targetUpDir,
    const double tauS, const double dt)
{
    // Rotation axis and unsigned angle from the current up to the target (atan2 stays accurate near 0 and PI)
    const FVector3d currentUp = currentOrientation.RotateVector(FVector3d(0.0, 0.0, 1.0));
    const FVector3d cross = FVector3d::CrossProduct(currentUp, targetUpDir);
    const double crossSize = cross.Size();
    const double dot = FVector3d::DotProduct(currentUp, targetUpDir);

    // Degenerate axis: already aligned (nothing to do) or exactly flipped (any axis perpendicular to up is shortest)
    FVector3d axis;
    if (crossSize < SURFACE_LOCK_MIN_CROSS_SIZE)
    {
        if (dot >= 0.0)
        {
            return currentOrientation;
        }
        const FVector3d helper = FMath::Abs(currentUp.X) < 0.9 ? FVector3d(1.0, 0.0, 0.0) : FVector3d(0.0, 1.0, 0.0);
        axis = FVector3d::CrossProduct(currentUp, helper).GetSafeNormal();
    }
    else
    {
        axis = cross / crossSize;
    }

    // Close the fraction of the arc for this step and apply it on top of the current orientation
    const double angle = FMath::Atan2(crossSize, dot);
    const double fraction = 1.0 - FMath::Exp(-dt / FMath::Max(tauS, SURFACE_LOCK_MIN_TIME_CONSTANT_S));
    const FQuat4d stepRotation(axis, angle * fraction);
    return (stepRotation * currentOrientation).GetNormalized();
}
