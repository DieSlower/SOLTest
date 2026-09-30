/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLBodyRotation.h"

#include "SOLConstants.h"

namespace SOLBodyRotation
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the fixed tilt rotation: ecliptic-north tipped toward ecliptic +X by tiltRad, about ecliptic +Y
    FQuat4d TiltRotation(const double tiltRad)
    {
        return FQuat4d(FVector3d(0.0, 1.0, 0.0), tiltRad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the tilt composed with a spin of (w0Rad + 2*PI*t/period) about the tilted pole; periodH <= 0 means no spin
    FQuat4d ComputeOrientation(const double tiltRad, const double w0Rad, const double periodH,
        const double secondsSinceJ2000)
    {
        const FQuat4d tilt = TiltRotation(tiltRad);
        if (periodH <= 0.0)
        {
            return tilt;
        }

        // Spin about the tilted pole (world space), applied after the tilt
        const FVector3d pole = tilt.RotateVector(FVector3d(0.0, 0.0, 1.0));
        const double spinAngleRad = w0Rad + UE_DOUBLE_TWO_PI * secondsSinceJ2000 / (periodH * SOL::SECONDS_PER_HOUR);
        const FQuat4d spin(pole, spinAngleRad);
        return (spin * tilt).GetNormalized();
    }
}
