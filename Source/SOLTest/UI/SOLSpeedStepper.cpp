/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLSpeedStepper.h"

#include "SOLConstants.h"

namespace
{
    constexpr int32 STEPPER_UNIT_COUNT = 3;     // Units in ESOLSpeedUnit, cycled in declaration order

    //////////////////////////////////////////////////////////////////////////
    // Returns the largest value the odometer digits [MinDigit, MaxDigit] can display (999999999.99)
    double StepperMaxDisplayValue()
    {
        return FMath::Pow(10.0, SOLSpeedStepper::MaxDigit + 1) - FMath::Pow(10.0, SOLSpeedStepper::MinDigit);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unit CycleUnit switches to (forward: m/s -> km/s -> c -> m/s, backward the reverse)
    ESOLSpeedUnit StepperNextUnit(const ESOLSpeedUnit unit, const bool bDown)
    {
        const int32 unitIndex = static_cast<int32>(unit);
        return static_cast<ESOLSpeedUnit>((unitIndex + (bDown ? STEPPER_UNIT_COUNT - 1 : 1)) % STEPPER_UNIT_COUNT);
    }
}

namespace SOLSpeedStepper
{
    //////////////////////////////////////////////////////////////////////////
    // Adds +/-1 at the highlighted place value (decimal carry/borrow), clamps to the display and cap range; returns m/s
    double StepDigit(const FSOLSpeedStepperState& state, const bool bDown)
    {
        const int32 digit = FMath::Clamp(state.HighlightedDigit, MinDigit, MaxDigit);
        const double placeValue = FMath::Pow(10.0, digit);
        const double stepped = state.ValueInUnit + (bDown ? -placeValue : placeValue);

        // A carry past MaxDigit clamps to the largest displayable value rather than wrapping
        const double displayed = FMath::Min(stepped, StepperMaxDisplayValue());
        const double speedMps = SOLHudFormat::FromUnitValue(displayed, state.Unit);
        return FMath::Clamp(speedMps, SOL::MIN_SPEED_CAP_MPS, SOL::MAX_SPEED_CAP_MPS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether cycling the unit keeps the converted value below 10^(MaxDigit + 1) in the target unit
    bool CanCycleUnit(const FSOLSpeedStepperState& state, const bool bDown)
    {
        const double speedMps = SOLHudFormat::FromUnitValue(state.ValueInUnit, state.Unit);
        const double targetValue = SOLHudFormat::ToUnitValue(speedMps, StepperNextUnit(state.Unit, bDown));
        return FMath::Abs(targetValue) < FMath::Pow(10.0, MaxDigit + 1);
    }

    //////////////////////////////////////////////////////////////////////////
    // Cycles the unit (forward: m/s -> km/s -> c -> m/s), keeping the underlying m/s and the highlighted digit
    FSOLSpeedStepperState CycleUnit(const FSOLSpeedStepperState& state, const bool bDown)
    {
        // A switch that would overflow the odometer is refused and leaves the state untouched
        if (!CanCycleUnit(state, bDown))
        {
            return state;
        }
        const double speedMps = SOLHudFormat::FromUnitValue(state.ValueInUnit, state.Unit);

        FSOLSpeedStepperState result;
        result.Unit = StepperNextUnit(state.Unit, bDown);
        result.ValueInUnit = SOLHudFormat::ToUnitValue(speedMps, result.Unit);
        result.HighlightedDigit = FMath::Clamp(state.HighlightedDigit, MinDigit, MaxDigit);
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds a state from a speed (magnitude clamped to the cap range): unit via PickSpeedUnit, HighlightedDigit = 0
    FSOLSpeedStepperState FromSpeedMps(const double speedMps)
    {
        const double clampedMps = FMath::Clamp(FMath::Abs(speedMps), SOL::MIN_SPEED_CAP_MPS, SOL::MAX_SPEED_CAP_MPS);

        FSOLSpeedStepperState state;
        state.Unit = SOLHudFormat::PickSpeedUnit(clampedMps);
        state.ValueInUnit = SOLHudFormat::ToUnitValue(clampedMps, state.Unit);
        state.HighlightedDigit = 0;
        return state;
    }
}
