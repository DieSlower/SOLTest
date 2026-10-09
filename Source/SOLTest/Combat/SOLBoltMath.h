/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic helpers for pulse-gun bolts (SDD 7): muzzle velocity, aim convergence, integration and swept hit tests.
namespace SOLBoltMath
{
    // Result of a swept hit test: whether it hit and the earliest time in [0,1] along the segment
    struct FSweepHit
    {
        bool bHit = false;
        double Time = 0.0;
    };

    // Returns shipVelocity plus the normalized aim direction scaled by muzzleSpeed (just the ship velocity if the aim is degenerate)
    SOLTEST_API FVector3d MuzzleVelocity(const FVector3d& shipVelocity, const FVector3d& aimDirection, double muzzleSpeed);

    // Returns the unit direction from the muzzle to the crosshair point convergenceDistance ahead of the camera
    SOLTEST_API FVector3d ConvergedAimDirection(const FVector3d& muzzlePosition, const FVector3d& cameraPosition,
        const FVector3d& cameraForward, double convergenceDistance);

    // Returns position + velocity * dt (unchanged for a negative or NaN dt)
    SOLTEST_API FVector3d AdvancePosition(const FVector3d& position, const FVector3d& velocity, double dt);

    // Returns whether a bolt's remaining lifetime has run out (<= 0 or NaN)
    SOLTEST_API bool IsExpired(double remainingLifetime);

    // Returns remaining - dt, never NaN (a NaN input gives 0)
    SOLTEST_API double AdvanceLifetime(double remaining, double dt);

    // Returns the earliest time the segment comes within sphereRadius of a stationary centre
    SOLTEST_API FSweepHit SweptSphereHit(const FVector3d& segmentStart, const FVector3d& segmentEnd,
        const FVector3d& sphereCenter, double sphereRadius);

    // Returns the earliest time the segment comes within sphereRadius of a centre moving linearly over the same interval
    SOLTEST_API FSweepHit SweptSphereHitMoving(const FVector3d& segmentStart, const FVector3d& segmentEnd,
        const FVector3d& sphereCenterStart, const FVector3d& sphereCenterEnd, double sphereRadius);
}
