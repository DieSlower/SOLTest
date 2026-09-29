/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "UI/SOLHudFormat.h"

// The F3 speed-cap panel's odometer state
struct FSOLSpeedStepperState
{
    double ValueInUnit = 1.0;                               // the displayed numeric value, always > 0
    ESOLSpeedUnit Unit = ESOLSpeedUnit::KilometersPerSecond;
    int32 HighlightedDigit = 0;                             // 0 = ones, 1 = tens, ... negative = fractional (-1 = tenths)
};

namespace SOLSpeedStepper
{
    constexpr int32 MinDigit = -2;    // hundredths
    constexpr int32 MaxDigit = 8;     // up to 10^8 place in the current unit (integer part at most 999,999,999)

    // Steps the highlighted digit by +/-1 with carry (clamps past MaxDigit or below 0); returns the clamped speed cap in m/s
    SOLTEST_API double StepDigit(const FSOLSpeedStepperState& state, bool bDown);

    // Returns whether CycleUnit in this direction keeps the converted value within MaxDigit + 1 integer digits
    SOLTEST_API bool CanCycleUnit(const FSOLSpeedStepperState& state, bool bDown);

    // Cycles the unit (forward: m/s -> km/s -> c -> m/s), keeping the underlying m/s and reclamping HighlightedDigit;
    // returns the state unchanged when CanCycleUnit is false
    SOLTEST_API FSOLSpeedStepperState CycleUnit(const FSOLSpeedStepperState& state, bool bDown);

    // Builds a state from an absolute speed: unit via SOLHudFormat::PickSpeedUnit, HighlightedDigit = 0
    SOLTEST_API FSOLSpeedStepperState FromSpeedMps(double speedMps);
}
