/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Body spin and axial tilt in the J2000 ecliptic frame (SDD 12); angles in radians
namespace SOLBodyRotation
{
    // Returns the fixed tilt rotation: ecliptic-north tipped toward ecliptic +X by tiltRad, about ecliptic +Y
    SOLTEST_API FQuat4d TiltRotation(double tiltRad);

    // Returns the tilt composed with a spin of (w0Rad + 2*PI*t/period) about the tilted pole; periodH <= 0 means no spin
    SOLTEST_API FQuat4d ComputeOrientation(double tiltRad, double w0Rad, double periodH, double secondsSinceJ2000);
}
