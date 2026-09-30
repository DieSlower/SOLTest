/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

// Tunable surface-lock parameters (SDD 4)
struct FSOLSurfaceLockParams
{
    double AutoRangeM = SOL::SURFACE_LOCK_AUTO_RANGE_M;                         // 10 km
    double AutoReleaseFactor = SOL::SURFACE_LOCK_AUTO_RELEASE_FACTOR;           // release above 12.5 km
    double ManualRangeMinM = SOL::SURFACE_LOCK_MANUAL_RANGE_MIN_M;              // 1,000 km floor
    double ManualReleaseFactor = SOL::SURFACE_LOCK_MANUAL_RELEASE_FACTOR;
    double AlignTimeConstantS = SOL::SURFACE_LOCK_ALIGN_TIME_CONSTANT_S;
};

// Surface-lock state carried between frames; the default is fully released
struct FSOLSurfaceLockState
{
    int32 BodyIndex = INDEX_NONE;
    bool bEngaged = false;
    bool bManual = false;
    bool bWarning = false;
    bool bAutoSuppressed = false;               // a press released the lock; auto-engage is blocked for that body
    int32 SuppressedBodyIndex = INDEX_NONE;     // the body auto-engage is suppressed for
};

namespace SOLSurfaceLock
{
    // Returns the manual lock range for a body: max(ManualRangeMinM, body radius)
    SOLTEST_API double ComputeManualRangeM(double bodyRadiusM, const FSOLSurfaceLockParams& params);

    // Advances the lock state machine one frame (nearest-body inputs drive engaging, locked-body inputs drive release)
    SOLTEST_API FSOLSurfaceLockState UpdateSurfaceLockState(const FSOLSurfaceLockState& prevState,
        int32 nearestBodyIndex, double nearestAltitudeM, double nearestBodyRadiusM,
        double lockedBodyAltitudeM, double lockedBodyRadiusM,
        bool bTogglePressed, const FSOLSurfaceLockParams& params);

    // Returns the unit direction from the body's center to the ship (the local vertical)
    SOLTEST_API FVector3d TargetUpDir(const FVector3d& shipPositionM, const FVector3d& bodyPositionM);

    // Rotates the orientation along the shortest arc from its up toward targetUpDir by the fraction 1 - e^(-dt / tau)
    SOLTEST_API FQuat4d ApplyAlignmentCorrection(const FQuat4d& currentOrientation, const FVector3d& targetUpDir,
        double tauS, double dt);
}
