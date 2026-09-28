/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Shared constants and comparison helpers for the SOLTest automation tests (one definition, unity-build safe)
namespace SOLTestHelpers
{
    inline constexpr double AU_M = 149597870700.0;
    inline constexpr double SUN_GM = 1.32712440018e20;
    inline constexpr double EARTH_GM = 3.986004418e14;
    inline constexpr double SECONDS_PER_DAY = 86400.0;

    // Reduces an angle difference to the nearest equivalent in [-PI, PI] (independent of the code under test)
    inline double AngleDiffRad(const double angleRad)
    {
        return angleRad - UE_DOUBLE_TWO_PI * FMath::RoundToDouble(angleRad / UE_DOUBLE_TWO_PI);
    }

    // Returns the relative error of a value against a non-zero expected value
    inline double RelativeError(const double actual, const double expected)
    {
        return FMath::Abs(actual - expected) / FMath::Max(FMath::Abs(expected), UE_DOUBLE_SMALL_NUMBER);
    }

    // Returns true when two vectors match within a tolerance relative to the expected vector's length
    inline bool VectorsNear(const FVector3d& actual, const FVector3d& expected, const double relTolerance)
    {
        return (actual - expected).Size() <= relTolerance * FMath::Max(expected.Size(), UE_DOUBLE_SMALL_NUMBER);
    }

    // Returns true when every component of a vector is finite
    inline bool IsFiniteVector(const FVector3d& value)
    {
        return FMath::IsFinite(value.X) && FMath::IsFinite(value.Y) && FMath::IsFinite(value.Z);
    }
}
