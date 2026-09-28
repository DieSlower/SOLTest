/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Simulation clock in Julian dates with stepped time-warp
struct SOLTEST_API FSOLSimClock
{
public:

    static constexpr double J2000JulianDate = 2451545.0;

    // Returns sim seconds per real second for each warp step: 1, 10, 100, 1000, 3600, 86400, 864000, 2592000
    static TConstArrayView<double> WarpFactors();

    // Converts a proleptic Gregorian UTC date and time to a Julian date
    static double JulianDateFromUtc(int32 year, int32 month, int32 day, int32 hour, int32 minute, double second);

    // Sets the sim time and resets the warp to index 0
    void Init(double startJulianDate);

    // Advances sim time by the real delta times the current warp factor
    void Advance(double realDeltaSeconds);

    // Returns the current Julian date
    double GetJulianDate() const;

    // Returns the sim seconds since J2000
    double GetSecondsSinceJ2000() const;

    // Returns the Julian centuries (36525 days) since J2000
    double GetCenturiesSinceJ2000() const;

    // Returns the current warp step index
    int32 GetWarpIndex() const;

    // Returns the current warp factor
    double GetWarpFactor() const;

    // Steps the warp up one step, clamping at the last step
    void StepWarpUp();

    // Steps the warp down one step, clamping at index 0
    void StepWarpDown();

    // Resets the warp to index 0
    void ResetWarp();

private:

    double mSecondsSinceJ2000 = 0.0; // Sim time
    int32 mWarpIndex = 0;            // Index into WarpFactors()
};
