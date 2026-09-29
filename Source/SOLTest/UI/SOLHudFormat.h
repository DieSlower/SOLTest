/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Display unit for a speed readout or the F3 speed-cap stepper
enum class ESOLSpeedUnit : uint8
{
    MetersPerSecond,
    KilometersPerSecond,
    LightSpeed
};

namespace SOLHudFormat
{
    // Picks the most readable unit for a magnitude (m/s): < 1000 -> m/s; < 0.01c -> km/s; else -> c. Zero -> m/s
    SOLTEST_API ESOLSpeedUnit PickSpeedUnit(double speedMps);

    // Converts to the unit's display value (m/s: unchanged; km/s: /1000; c: /299792458.0)
    SOLTEST_API double ToUnitValue(double speedMps, ESOLSpeedUnit unit);

    // Inverse of ToUnitValue
    SOLTEST_API double FromUnitValue(double value, ESOLSpeedUnit unit);

    // Formats meters as "#.# m" under 1000 m, "#.# km" under 0.01 AU, else "#.#### AU" (e.g. "23402.9 km")
    SOLTEST_API FString FormatDistanceM(double distanceM);

    // Appends FormatDistanceM(distanceM)'s exact text to an existing buffer, without a temporary FString
    SOLTEST_API void AppendDistanceM(FString& outText, double distanceM);
}
