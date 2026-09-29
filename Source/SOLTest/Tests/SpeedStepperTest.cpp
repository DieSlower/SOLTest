/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLSpeedStepper.h"

#include "SOLConstants.h"
#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Light speed as the contract states it (independent of the code under test)
    constexpr double STEPPER_C_MPS = 299792458.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns a stepper state
    FSOLSpeedStepperState StepperMake(const double valueInUnit, const ESOLSpeedUnit unit, const int32 digit)
    {
        FSOLSpeedStepperState state;
        state.ValueInUnit = valueInUnit;
        state.Unit = unit;
        state.HighlightedDigit = digit;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the m/s multiplier of a unit (independent of the code under test)
    double StepperUnitScale(const ESOLSpeedUnit unit)
    {
        switch (unit)
        {
        case ESOLSpeedUnit::MetersPerSecond:
            return 1.0;
        case ESOLSpeedUnit::KilometersPerSecond:
            return 1000.0;
        case ESOLSpeedUnit::LightSpeed:
            return STEPPER_C_MPS;
        }
        return 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the state's speed in m/s (independent of the code under test)
    double StepperStateMps(const FSOLSpeedStepperState& state)
    {
        return state.ValueInUnit * StepperUnitScale(state.Unit);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts StepDigit returns an expected m/s value within a relative tolerance
    void StepperExpectStep(FAutomationTestBase& test, const TCHAR* what, const FSOLSpeedStepperState& state,
        const bool bDown, const double expectedMps)
    {
        const double actual = SOLSpeedStepper::StepDigit(state, bDown);
        test.TestTrue(FString::Printf(TEXT("%s: expected %.9f m/s, got %.9f"), what, expectedMps, actual),
            SOLTestHelpers::RelativeError(actual, expectedMps) < 1e-12);
    }
}

// --- StepDigit ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperPlaceValueTest, "SOLTest.SpeedStepper.StepPlaceValue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stepping changes the value by exactly one unit at the highlighted place value, converted to m/s
bool FSOLStepperPlaceValueTest::RunTest(const FString& /*parameters*/)
{
    StepperExpectStep(*this, TEXT("5 km/s ones up"), StepperMake(5.0, ESOLSpeedUnit::KilometersPerSecond, 0), false,
        6000.0);
    StepperExpectStep(*this, TEXT("5 km/s ones down"), StepperMake(5.0, ESOLSpeedUnit::KilometersPerSecond, 0), true,
        4000.0);
    StepperExpectStep(*this, TEXT("25 km/s tens up"), StepperMake(25.0, ESOLSpeedUnit::KilometersPerSecond, 1), false,
        35000.0);
    StepperExpectStep(*this, TEXT("2 km/s thousands up"), StepperMake(2.0, ESOLSpeedUnit::KilometersPerSecond, 3), false,
        1002000.0);
    StepperExpectStep(*this, TEXT("123.45 m/s tenths up"), StepperMake(123.45, ESOLSpeedUnit::MetersPerSecond, -1), false,
        123.55);
    StepperExpectStep(*this, TEXT("123.45 m/s hundredths down"), StepperMake(123.45, ESOLSpeedUnit::MetersPerSecond, -2),
        true, 123.44);
    StepperExpectStep(*this, TEXT("500 m/s hundreds down"), StepperMake(500.0, ESOLSpeedUnit::MetersPerSecond, 2), true,
        400.0);
    StepperExpectStep(*this, TEXT("0.1 c hundredths up"), StepperMake(0.1, ESOLSpeedUnit::LightSpeed, -2), false,
        0.11 * STEPPER_C_MPS);
    StepperExpectStep(*this, TEXT("0.3 c tenths down"), StepperMake(0.3, ESOLSpeedUnit::LightSpeed, -1), true,
        0.2 * STEPPER_C_MPS);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperCarryTest, "SOLTest.SpeedStepper.StepCarriesAndBorrows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stepping a 9 up carries into the next digit; stepping a 0 down borrows from a higher non-zero digit
bool FSOLStepperCarryTest::RunTest(const FString& /*parameters*/)
{
    StepperExpectStep(*this, TEXT("19 km/s ones up -> 20"), StepperMake(19.0, ESOLSpeedUnit::KilometersPerSecond, 0), false,
        20000.0);
    StepperExpectStep(*this, TEXT("999 m/s ones up -> 1000"), StepperMake(999.0, ESOLSpeedUnit::MetersPerSecond, 0), false,
        1000.0);
    StepperExpectStep(*this, TEXT("9.95 m/s tenths up -> 10.05"), StepperMake(9.95, ESOLSpeedUnit::MetersPerSecond, -1),
        false, 10.05);
    StepperExpectStep(*this, TEXT("20 km/s ones down -> 19"), StepperMake(20.0, ESOLSpeedUnit::KilometersPerSecond, 0), true,
        19000.0);
    StepperExpectStep(*this, TEXT("100 m/s ones down -> 99"), StepperMake(100.0, ESOLSpeedUnit::MetersPerSecond, 0), true,
        99.0);
    StepperExpectStep(*this, TEXT("3.00 m/s hundredths down -> 2.99"), StepperMake(3.0, ESOLSpeedUnit::MetersPerSecond, -2),
        true, 2.99);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperClampLowTest, "SOLTest.SpeedStepper.StepClampsAtMinimum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stepping below zero or below the minimum cap clamps at SOL::MIN_SPEED_CAP_MPS rather than wrapping
bool FSOLStepperClampLowTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Stepper digit range"), SOLSpeedStepper::MinDigit, -2);
    TestEqual(TEXT("Stepper digit range"), SOLSpeedStepper::MaxDigit, 8);

    StepperExpectStep(*this, TEXT("1 m/s hundredths down clamps at the minimum"),
        StepperMake(1.0, ESOLSpeedUnit::MetersPerSecond, -2), true, SOL::MIN_SPEED_CAP_MPS);
    StepperExpectStep(*this, TEXT("1 m/s ones down (to 0) clamps at the minimum"),
        StepperMake(1.0, ESOLSpeedUnit::MetersPerSecond, 0), true, SOL::MIN_SPEED_CAP_MPS);
    StepperExpectStep(*this, TEXT("10 km/s tens down (to 0) clamps at the minimum"),
        StepperMake(10.0, ESOLSpeedUnit::KilometersPerSecond, 1), true, SOL::MIN_SPEED_CAP_MPS);
    StepperExpectStep(*this, TEXT("5 km/s tens down (below 0) clamps at the minimum"),
        StepperMake(5.0, ESOLSpeedUnit::KilometersPerSecond, 1), true, SOL::MIN_SPEED_CAP_MPS);
    StepperExpectStep(*this, TEXT("0.5 m/s ones up -> 1.5 (above the minimum, unchanged by the clamp)"),
        StepperMake(0.5, ESOLSpeedUnit::MetersPerSecond, 0), false, 1.5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperClampHighTest, "SOLTest.SpeedStepper.StepClampsAtMaximum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stepping past the max cap or past MaxDigit clamps rather than wrapping
bool FSOLStepperClampHighTest::RunTest(const FString& /*parameters*/)
{
    StepperExpectStep(*this, TEXT("0.5 c ones up clamps at the max cap"), StepperMake(0.5, ESOLSpeedUnit::LightSpeed, 0),
        false, SOL::MAX_SPEED_CAP_MPS);
    StepperExpectStep(*this, TEXT("0.49 c tenths up clamps at the max cap"),
        StepperMake(0.49, ESOLSpeedUnit::LightSpeed, -1), false, SOL::MAX_SPEED_CAP_MPS);
    StepperExpectStep(*this, TEXT("140000 km/s hundred-thousands up clamps at the max cap"),
        StepperMake(140000.0, ESOLSpeedUnit::KilometersPerSecond, 5), false, SOL::MAX_SPEED_CAP_MPS);

    // A carry below MaxDigit must not wrap to a small value
    const double carried = SOLSpeedStepper::StepDigit(StepperMake(9000000.0, ESOLSpeedUnit::MetersPerSecond, 6), false);
    TestTrue(FString::Printf(TEXT("9e6 m/s millions up does not wrap (got %.3f)"), carried), carried >= 9000000.0);
    TestTrue(FString::Printf(TEXT("9e6 m/s millions up stays within the max cap (got %.3f)"), carried),
        carried <= SOL::MAX_SPEED_CAP_MPS);

    // A carry past MaxDigit (10^8 place) must not wrap either; it clamps to the max cap
    const double carriedTop = SOLSpeedStepper::StepDigit(StepperMake(900000000.0, ESOLSpeedUnit::MetersPerSecond, 8),
        false);
    TestTrue(FString::Printf(TEXT("9e8 m/s hundred-millions up clamps at the max cap (got %.3f)"), carriedTop),
        SOLTestHelpers::RelativeError(carriedTop, SOL::MAX_SPEED_CAP_MPS) < 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperRangeInvariantTest, "SOLTest.SpeedStepper.StepAlwaysWithinCapRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every step, up or down, at every digit and unit, returns a finite speed within [MIN_SPEED_CAP, MAX_SPEED_CAP]
bool FSOLStepperRangeInvariantTest::RunTest(const FString& /*parameters*/)
{
    const ESOLSpeedUnit units[] = { ESOLSpeedUnit::MetersPerSecond, ESOLSpeedUnit::KilometersPerSecond,
        ESOLSpeedUnit::LightSpeed };
    const double values[] = { 0.01, 0.5, 1.0, 9.99, 10.0, 999.0, 123456.78, 9999999.0 };

    // Sweep every combination of unit, value, digit and direction
    for (const ESOLSpeedUnit unit : units)
    {
        for (const double value : values)
        {
            for (int32 digit = SOLSpeedStepper::MinDigit; digit <= SOLSpeedStepper::MaxDigit; ++digit)
            {
                for (const bool bDown : { false, true })
                {
                    const double mps = SOLSpeedStepper::StepDigit(StepperMake(value, unit, digit), bDown);
                    TestTrue(FString::Printf(TEXT("value %.2f unit %d digit %d %s -> %.6f in cap range"), value,
                        static_cast<int32>(unit), digit, bDown ? TEXT("down") : TEXT("up"), mps),
                        FMath::IsFinite(mps) && mps >= SOL::MIN_SPEED_CAP_MPS && mps <= SOL::MAX_SPEED_CAP_MPS);
                }
            }
        }
    }
    return true;
}

// --- CycleUnit ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperCycleForwardTest, "SOLTest.SpeedStepper.CycleUnitForward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Forward cycles m/s -> km/s -> c -> m/s, preserving the underlying m/s at every step
bool FSOLStepperCycleForwardTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpeedStepperState start = StepperMake(5000.0, ESOLSpeedUnit::MetersPerSecond, 2);
    const double startMps = StepperStateMps(start);

    const FSOLSpeedStepperState toKm = SOLSpeedStepper::CycleUnit(start, false);
    TestTrue(TEXT("m/s -> km/s"), toKm.Unit == ESOLSpeedUnit::KilometersPerSecond);
    TestEqual(TEXT("5000 m/s shows 5 km/s"), toKm.ValueInUnit, 5.0, 1e-12);

    const FSOLSpeedStepperState toC = SOLSpeedStepper::CycleUnit(toKm, false);
    TestTrue(TEXT("km/s -> c"), toC.Unit == ESOLSpeedUnit::LightSpeed);
    TestTrue(TEXT("c value preserves m/s"), SOLTestHelpers::RelativeError(StepperStateMps(toC), startMps) < 1e-12);

    const FSOLSpeedStepperState back = SOLSpeedStepper::CycleUnit(toC, false);
    TestTrue(TEXT("c -> m/s"), back.Unit == ESOLSpeedUnit::MetersPerSecond);
    TestTrue(TEXT("Full cycle preserves m/s"), SOLTestHelpers::RelativeError(back.ValueInUnit, startMps) < 1e-12);

    // High speed: 0.5c (149,896,229 m/s, 9 integer digits) fits MaxDigit 8, so c -> m/s is allowed without corruption
    const FSOLSpeedStepperState halfC = StepperMake(0.5, ESOLSpeedUnit::LightSpeed, 0);
    const double halfCMps = 0.5 * STEPPER_C_MPS;
    TestTrue(TEXT("0.5c: CanCycleUnit forward (c -> m/s)"), SOLSpeedStepper::CanCycleUnit(halfC, false));
    const FSOLSpeedStepperState halfCInM = SOLSpeedStepper::CycleUnit(halfC, false);
    TestTrue(TEXT("0.5c: c -> m/s"), halfCInM.Unit == ESOLSpeedUnit::MetersPerSecond);
    TestTrue(FString::Printf(TEXT("0.5c: m/s value %.3f preserves the speed"), halfCInM.ValueInUnit),
        SOLTestHelpers::RelativeError(halfCInM.ValueInUnit, halfCMps) < 1e-12);
    TestEqual(TEXT("0.5c: digit kept"), halfCInM.HighlightedDigit, 0);

    // Stepping the top digits of the converted value is not corrupted (10^8 place is now a valid digit)
    StepperExpectStep(*this, TEXT("0.5c in m/s, hundred-millions down"),
        StepperMake(halfCInM.ValueInUnit, ESOLSpeedUnit::MetersPerSecond, 8), true, halfCMps - 1.0e8);
    StepperExpectStep(*this, TEXT("0.5c in m/s, ten-millions down"),
        StepperMake(halfCInM.ValueInUnit, ESOLSpeedUnit::MetersPerSecond, 7), true, halfCMps - 1.0e7);
    StepperExpectStep(*this, TEXT("0.5c in m/s, ones down"),
        StepperMake(halfCInM.ValueInUnit, ESOLSpeedUnit::MetersPerSecond, 0), true, halfCMps - 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperCycleBackwardTest, "SOLTest.SpeedStepper.CycleUnitBackward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Backward is the exact reverse (m/s -> c -> km/s -> m/s) and forward-then-backward restores the state's speed
bool FSOLStepperCycleBackwardTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpeedStepperState start = StepperMake(0.25, ESOLSpeedUnit::LightSpeed, -1);
    const double startMps = StepperStateMps(start);

    const FSOLSpeedStepperState toKm = SOLSpeedStepper::CycleUnit(start, true);
    TestTrue(TEXT("c -> km/s"), toKm.Unit == ESOLSpeedUnit::KilometersPerSecond);
    TestTrue(TEXT("km/s value preserves m/s"), SOLTestHelpers::RelativeError(StepperStateMps(toKm), startMps) < 1e-12);

    const FSOLSpeedStepperState toM = SOLSpeedStepper::CycleUnit(toKm, true);
    TestTrue(TEXT("km/s -> m/s"), toM.Unit == ESOLSpeedUnit::MetersPerSecond);
    TestTrue(TEXT("m/s value preserves m/s"), SOLTestHelpers::RelativeError(toM.ValueInUnit, startMps) < 1e-12);

    const FSOLSpeedStepperState toC = SOLSpeedStepper::CycleUnit(toM, true);
    TestTrue(TEXT("m/s -> c"), toC.Unit == ESOLSpeedUnit::LightSpeed);
    TestTrue(TEXT("Full backward cycle preserves the value"), SOLTestHelpers::RelativeError(toC.ValueInUnit, 0.25) < 1e-12);

    // Forward then backward returns to the original unit and value
    const FSOLSpeedStepperState kmStart = StepperMake(29.78, ESOLSpeedUnit::KilometersPerSecond, 0);
    const FSOLSpeedStepperState roundTrip = SOLSpeedStepper::CycleUnit(SOLSpeedStepper::CycleUnit(kmStart, false), true);
    TestTrue(TEXT("Forward then backward restores the unit"), roundTrip.Unit == ESOLSpeedUnit::KilometersPerSecond);
    TestTrue(TEXT("Forward then backward restores the value"),
        SOLTestHelpers::RelativeError(roundTrip.ValueInUnit, 29.78) < 1e-12);

    // High speed: 0.5c shown in km/s (149,896.229) cycles backward to m/s (149,896,229, fits 9 integer digits)
    const double halfCMps = 0.5 * STEPPER_C_MPS;
    const FSOLSpeedStepperState halfCKm = StepperMake(halfCMps / 1000.0, ESOLSpeedUnit::KilometersPerSecond, 5);
    TestTrue(TEXT("0.5c: CanCycleUnit backward (km/s -> m/s)"), SOLSpeedStepper::CanCycleUnit(halfCKm, true));
    const FSOLSpeedStepperState halfCInM = SOLSpeedStepper::CycleUnit(halfCKm, true);
    TestTrue(TEXT("0.5c: km/s -> m/s"), halfCInM.Unit == ESOLSpeedUnit::MetersPerSecond);
    TestTrue(FString::Printf(TEXT("0.5c: m/s value %.3f preserves the speed"), halfCInM.ValueInUnit),
        SOLTestHelpers::RelativeError(halfCInM.ValueInUnit, halfCMps) < 1e-12);
    TestEqual(TEXT("0.5c: digit kept"), halfCInM.HighlightedDigit, 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperCanCycleTest, "SOLTest.SpeedStepper.CanCycleUnitOverflowBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CanCycleUnit is false exactly when the target unit's value would need more than MaxDigit + 1 (9) integer digits
bool FSOLStepperCanCycleTest::RunTest(const FString& /*parameters*/)
{
    struct FCase
    {
        const TCHAR* What;
        double Value;
        ESOLSpeedUnit Unit;
        bool bDown;
        bool bExpected;
    };
    const FCase cases[] =
    {
        // km/s -> m/s (backward): 999,999 km/s = 999,999,000 m/s fits; 999,999.999 km/s = 999,999,999 m/s fits
        { TEXT("999999 km/s -> m/s (999,999,000)"), 999999.0, ESOLSpeedUnit::KilometersPerSecond, true, true },
        { TEXT("999999.999 km/s -> m/s (999,999,999)"), 999999.999, ESOLSpeedUnit::KilometersPerSecond, true, true },
        // 1,000,000 km/s = 1,000,000,000 m/s needs 10 integer digits
        { TEXT("1000000 km/s -> m/s (1e9)"), 1000000.0, ESOLSpeedUnit::KilometersPerSecond, true, false },
        { TEXT("5000000 km/s -> m/s (5e9)"), 5000000.0, ESOLSpeedUnit::KilometersPerSecond, true, false },
        // c -> m/s (forward): 3 c = 899,377,374 m/s fits; 4 c = 1,199,169,832 m/s does not
        { TEXT("3 c -> m/s (899,377,374)"), 3.0, ESOLSpeedUnit::LightSpeed, false, true },
        { TEXT("4 c -> m/s (1,199,169,832)"), 4.0, ESOLSpeedUnit::LightSpeed, false, false },
        // c -> km/s (backward): 4 c = 1,199,169.832 km/s fits
        { TEXT("4 c -> km/s (1,199,169.832)"), 4.0, ESOLSpeedUnit::LightSpeed, true, true },
        // Conversions into a larger unit only shrink the value
        { TEXT("999999999 m/s -> km/s"), 999999999.0, ESOLSpeedUnit::MetersPerSecond, false, true },
        { TEXT("999999999 m/s -> c"), 999999999.0, ESOLSpeedUnit::MetersPerSecond, true, true },
        { TEXT("999999 km/s -> c"), 999999.0, ESOLSpeedUnit::KilometersPerSecond, false, true },
        // Ordinary speeds are always allowed in both directions
        { TEXT("5 km/s -> c"), 5.0, ESOLSpeedUnit::KilometersPerSecond, false, true },
        { TEXT("5 km/s -> m/s"), 5.0, ESOLSpeedUnit::KilometersPerSecond, true, true },
        { TEXT("0.25 c -> m/s"), 0.25, ESOLSpeedUnit::LightSpeed, false, true },
    };

    // Each case's answer, and CycleUnit agrees with it (unit changes only when allowed)
    for (const FCase& testCase : cases)
    {
        const FSOLSpeedStepperState state = StepperMake(testCase.Value, testCase.Unit, 0);
        const bool bCan = SOLSpeedStepper::CanCycleUnit(state, testCase.bDown);
        TestTrue(FString::Printf(TEXT("%s: CanCycleUnit expected %d, got %d"), testCase.What,
            testCase.bExpected ? 1 : 0, bCan ? 1 : 0), bCan == testCase.bExpected);
        const FSOLSpeedStepperState cycled = SOLSpeedStepper::CycleUnit(state, testCase.bDown);
        TestTrue(FString::Printf(TEXT("%s: CycleUnit changes the unit iff allowed"), testCase.What),
            (cycled.Unit != testCase.Unit) == testCase.bExpected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperCycleRefusedTest, "SOLTest.SpeedStepper.CycleUnitRefusedLeavesStateUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A refused unit change returns the input state exactly: same unit, value and highlighted digit
bool FSOLStepperCycleRefusedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSpeedStepperState overKm = StepperMake(1000000.0, ESOLSpeedUnit::KilometersPerSecond, 3);
    const FSOLSpeedStepperState overC = StepperMake(4.0, ESOLSpeedUnit::LightSpeed, -2);
    const struct { const TCHAR* What; FSOLSpeedStepperState State; bool bDown; } cases[] =
    {
        { TEXT("1e6 km/s backward to m/s"), overKm, true },
        { TEXT("4 c forward to m/s"), overC, false },
    };

    // Every field of the returned state matches the input
    for (const auto& testCase : cases)
    {
        TestFalse(FString::Printf(TEXT("%s: CanCycleUnit is false"), testCase.What),
            SOLSpeedStepper::CanCycleUnit(testCase.State, testCase.bDown));
        const FSOLSpeedStepperState result = SOLSpeedStepper::CycleUnit(testCase.State, testCase.bDown);
        TestTrue(FString::Printf(TEXT("%s: unit unchanged"), testCase.What), result.Unit == testCase.State.Unit);
        TestEqual(FString::Printf(TEXT("%s: value unchanged"), testCase.What), result.ValueInUnit,
            testCase.State.ValueInUnit, 0.0);
        TestEqual(FString::Printf(TEXT("%s: digit unchanged"), testCase.What), result.HighlightedDigit,
            testCase.State.HighlightedDigit);
    }

    // The allowed direction from the same states still cycles normally
    const FSOLSpeedStepperState overCToKm = SOLSpeedStepper::CycleUnit(overC, true);
    TestTrue(TEXT("4 c backward to km/s still cycles"), overCToKm.Unit == ESOLSpeedUnit::KilometersPerSecond);
    TestTrue(TEXT("4 c backward to km/s preserves m/s"),
        SOLTestHelpers::RelativeError(StepperStateMps(overCToKm), 4.0 * STEPPER_C_MPS) < 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperCycleDigitTest, "SOLTest.SpeedStepper.CycleUnitKeepsDigitInRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After a unit change the highlighted digit is always within [MinDigit, MaxDigit], and the unit did change
bool FSOLStepperCycleDigitTest::RunTest(const FString& /*parameters*/)
{
    const ESOLSpeedUnit units[] = { ESOLSpeedUnit::MetersPerSecond, ESOLSpeedUnit::KilometersPerSecond,
        ESOLSpeedUnit::LightSpeed };

    // Every unit, extreme digit and direction
    for (const ESOLSpeedUnit unit : units)
    {
        for (const int32 digit : { SOLSpeedStepper::MinDigit, 0, 3, SOLSpeedStepper::MaxDigit })
        {
            for (const bool bDown : { false, true })
            {
                const FSOLSpeedStepperState state = SOLSpeedStepper::CycleUnit(StepperMake(0.3, unit, digit), bDown);
                TestTrue(FString::Printf(TEXT("unit %d digit %d: unit changed"), static_cast<int32>(unit), digit),
                    state.Unit != unit);
                TestTrue(FString::Printf(TEXT("unit %d digit %d: digit %d in range"), static_cast<int32>(unit), digit,
                    state.HighlightedDigit), state.HighlightedDigit >= SOLSpeedStepper::MinDigit
                    && state.HighlightedDigit <= SOLSpeedStepper::MaxDigit);
                TestTrue(TEXT("Value stays > 0"), state.ValueInUnit > 0.0);
            }
        }
    }
    return true;
}

// --- FromSpeedMps ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStepperFromSpeedTest, "SOLTest.SpeedStepper.FromSpeedPicksUnit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Builds a state in the readable unit (as PickSpeedUnit), digit 0, whose value converts back to the input speed
bool FSOLStepperFromSpeedTest::RunTest(const FString& /*parameters*/)
{
    struct FCase
    {
        double SpeedMps;
        ESOLSpeedUnit Unit;
        double Value;
    };
    const FCase cases[] =
    {
        { 500.0, ESOLSpeedUnit::MetersPerSecond, 500.0 },
        { 1.0, ESOLSpeedUnit::MetersPerSecond, 1.0 },
        { 1000.0, ESOLSpeedUnit::KilometersPerSecond, 1.0 },
        { 29780.0, ESOLSpeedUnit::KilometersPerSecond, 29.78 },
        { 150000.0, ESOLSpeedUnit::KilometersPerSecond, 150.0 },
        { 0.1 * STEPPER_C_MPS, ESOLSpeedUnit::LightSpeed, 0.1 },
        { SOL::MAX_SPEED_CAP_MPS, ESOLSpeedUnit::LightSpeed, 0.5 },
    };

    // Each speed lands in the expected unit and value, agreeing with PickSpeedUnit and round-tripping through the unit
    for (const FCase& testCase : cases)
    {
        const FSOLSpeedStepperState state = SOLSpeedStepper::FromSpeedMps(testCase.SpeedMps);
        TestTrue(FString::Printf(TEXT("%.3f m/s: expected unit %d, got %d"), testCase.SpeedMps,
            static_cast<int32>(testCase.Unit), static_cast<int32>(state.Unit)), state.Unit == testCase.Unit);
        TestTrue(FString::Printf(TEXT("%.3f m/s: unit agrees with PickSpeedUnit"), testCase.SpeedMps),
            state.Unit == SOLHudFormat::PickSpeedUnit(testCase.SpeedMps));
        TestTrue(FString::Printf(TEXT("%.3f m/s: value %.9f (expected %.9f)"), testCase.SpeedMps, state.ValueInUnit,
            testCase.Value), SOLTestHelpers::RelativeError(state.ValueInUnit, testCase.Value) < 1e-12);
        TestEqual(FString::Printf(TEXT("%.3f m/s: digit 0"), testCase.SpeedMps), state.HighlightedDigit, 0);
        TestTrue(FString::Printf(TEXT("%.3f m/s: round-trips via FromUnitValue"), testCase.SpeedMps),
            SOLTestHelpers::RelativeError(SOLHudFormat::FromUnitValue(state.ValueInUnit, state.Unit), testCase.SpeedMps)
            < 1e-12);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
