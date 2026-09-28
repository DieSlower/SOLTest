/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLSimClock.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS


// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Expected warp table from the contract, in sim seconds per real second
    constexpr double CLOCK_EXPECTED_WARPS[] = { 1.0, 10.0, 100.0, 1000.0, 3600.0, 86400.0, 864000.0, 2592000.0 };
    constexpr int32 CLOCK_WARP_COUNT = UE_ARRAY_COUNT(CLOCK_EXPECTED_WARPS);
    constexpr double CLOCK_JD_TOLERANCE = 1e-9;

    // One known calendar date and its Julian date
    struct FSOLClockDateCase
    {
        int32 Year;
        int32 Month;
        int32 Day;
        int32 Hour;
        int32 Minute;
        double Second;
        double ExpectedJd;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockJulianDateJ2000Test, "SOLTest.SimClock.JulianDateAtJ2000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 2000-01-01 12:00 is exactly JD 2451545.0 and 2000-01-01 00:00 is exactly JD 2451544.5
bool FSOLSimClockJulianDateJ2000Test::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("J2000JulianDate constant"), FSOLSimClock::J2000JulianDate, 2451545.0, 0.0);
    TestEqual(TEXT("2000-01-01 12:00:00 == 2451545.0 exactly"),
        FSOLSimClock::JulianDateFromUtc(2000, 1, 1, 12, 0, 0.0), 2451545.0, 0.0);
    TestEqual(TEXT("2000-01-01 00:00:00 == 2451544.5 exactly"),
        FSOLSimClock::JulianDateFromUtc(2000, 1, 1, 0, 0, 0.0), 2451544.5, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockJulianDateKnownTest, "SOLTest.SimClock.JulianDateKnownDates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// JulianDateFromUtc matches Meeus reference dates, including Gregorian leap-year and century rules
bool FSOLSimClockJulianDateKnownTest::RunTest(const FString& /*parameters*/)
{
    // Meeus, Astronomical Algorithms ch. 7, plus hand-counted dates
    const FSOLClockDateCase cases[] =
    {
        { 1999, 1, 1, 0, 0, 0.0, 2451179.5 },
        { 1987, 1, 27, 0, 0, 0.0, 2446822.5 },
        { 1987, 6, 19, 12, 0, 0.0, 2446966.0 },
        { 1988, 1, 27, 0, 0, 0.0, 2447187.5 },
        { 1988, 6, 19, 12, 0, 0.0, 2447332.0 },
        { 1900, 1, 1, 0, 0, 0.0, 2415020.5 },
        { 1900, 3, 1, 0, 0, 0.0, 2415079.5 },     // 1900 is not a leap year
        { 2000, 3, 1, 0, 0, 0.0, 2451604.5 },     // 2000 is a leap year
        { 1600, 1, 1, 0, 0, 0.0, 2305447.5 },
        { 1600, 12, 31, 0, 0, 0.0, 2305812.5 },
        { 2024, 2, 29, 12, 0, 0.0, 2460370.0 },
        { 2026, 9, 28, 0, 0, 0.0, 2461311.5 },
        { 1957, 10, 4, 19, 26, 24.0, 2436116.31 }, // Sputnik 1, Meeus example 7.a
    };
    for (const FSOLClockDateCase& date : cases)
    {
        const double jd = FSOLSimClock::JulianDateFromUtc(date.Year, date.Month, date.Day, date.Hour, date.Minute,
            date.Second);
        TestEqual(*FString::Printf(TEXT("%04d-%02d-%02d %02d:%02d:%06.3f"), date.Year, date.Month, date.Day, date.Hour,
            date.Minute, date.Second), jd, date.ExpectedJd, CLOCK_JD_TOLERANCE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockJulianDateFractionTest, "SOLTest.SimClock.JulianDateFractionalTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Hours, minutes and fractional seconds contribute their exact day fractions
bool FSOLSimClockJulianDateFractionTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("18:00"), FSOLSimClock::JulianDateFromUtc(2000, 1, 1, 18, 0, 0.0), 2451545.25, CLOCK_JD_TOLERANCE);
    TestEqual(TEXT("12:00:30"), FSOLSimClock::JulianDateFromUtc(2000, 1, 1, 12, 0, 30.0),
        2451545.0 + 30.0 / SOLTestHelpers::SECONDS_PER_DAY, CLOCK_JD_TOLERANCE);
    TestEqual(TEXT("12:01:00"), FSOLSimClock::JulianDateFromUtc(2000, 1, 1, 12, 1, 0.0),
        2451545.0 + 60.0 / SOLTestHelpers::SECONDS_PER_DAY, CLOCK_JD_TOLERANCE);
    TestEqual(TEXT("12:00:00.5"), FSOLSimClock::JulianDateFromUtc(2000, 1, 1, 12, 0, 0.5),
        2451545.0 + 0.5 / SOLTestHelpers::SECONDS_PER_DAY, CLOCK_JD_TOLERANCE);
    TestEqual(TEXT("23:59:59"), FSOLSimClock::JulianDateFromUtc(2000, 1, 1, 23, 59, 59.0),
        2451545.5 - 1.0 / SOLTestHelpers::SECONDS_PER_DAY, CLOCK_JD_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockInitTest, "SOLTest.SimClock.InitSetsTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Init sets the Julian date, seconds and centuries since J2000, and warp index 0
bool FSOLSimClockInitTest::RunTest(const FString& /*parameters*/)
{
    FSOLSimClock clock;
    clock.Init(FSOLSimClock::J2000JulianDate);
    TestEqual(TEXT("J2000: Julian date"), clock.GetJulianDate(), 2451545.0, 0.0);
    TestEqual(TEXT("J2000: seconds"), clock.GetSecondsSinceJ2000(), 0.0, 0.0);
    TestEqual(TEXT("J2000: centuries"), clock.GetCenturiesSinceJ2000(), 0.0, 0.0);
    TestEqual(TEXT("J2000: warp index"), clock.GetWarpIndex(), 0);
    TestEqual(TEXT("J2000: warp factor"), clock.GetWarpFactor(), 1.0, 0.0);

    // One Julian century later
    clock.Init(FSOLSimClock::J2000JulianDate + 36525.0);
    TestEqual(TEXT("J2100: Julian date"), clock.GetJulianDate(), 2451545.0 + 36525.0, CLOCK_JD_TOLERANCE);
    TestEqual(TEXT("J2100: seconds"), clock.GetSecondsSinceJ2000(), 36525.0 * SOLTestHelpers::SECONDS_PER_DAY, 1e-3);
    TestEqual(TEXT("J2100: centuries"), clock.GetCenturiesSinceJ2000(), 1.0, 1e-12);

    // Before J2000 (midnight 2000-01-01)
    clock.Init(2451544.5);
    TestEqual(TEXT("2000-01-01 00:00: seconds"), clock.GetSecondsSinceJ2000(), -43200.0, 1e-4);
    TestEqual(TEXT("2000-01-01 00:00: centuries"), clock.GetCenturiesSinceJ2000(), -0.5 / 36525.0, 1e-15);
    TestEqual(TEXT("2000-01-01 00:00: Julian date"), clock.GetJulianDate(), 2451544.5, CLOCK_JD_TOLERANCE);

    // A recent date
    clock.Init(2461311.5);
    TestEqual(TEXT("2026-09-28: seconds"), clock.GetSecondsSinceJ2000(), (2461311.5 - 2451545.0) * SOLTestHelpers::SECONDS_PER_DAY,
        1e-3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockWarpFactorsTest, "SOLTest.SimClock.WarpFactorsTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// WarpFactors returns exactly 1, 10, 100, 1000, 3600, 86400, 864000, 2592000
bool FSOLSimClockWarpFactorsTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<double> factors = FSOLSimClock::WarpFactors();
    if (!TestEqual(TEXT("Warp step count"), factors.Num(), CLOCK_WARP_COUNT))
    {
        return false;
    }
    for (int32 index = 0; index < CLOCK_WARP_COUNT; ++index)
    {
        TestEqual(*FString::Printf(TEXT("Warp factor %d"), index), factors[index], CLOCK_EXPECTED_WARPS[index], 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockAdvanceWarpTest, "SOLTest.SimClock.AdvanceAtEachWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Advance adds real delta times the current warp factor at every warp step
bool FSOLSimClockAdvanceWarpTest::RunTest(const FString& /*parameters*/)
{
    for (int32 warpIndex = 0; warpIndex < CLOCK_WARP_COUNT; ++warpIndex)
    {
        FSOLSimClock clock;
        clock.Init(FSOLSimClock::J2000JulianDate);
        for (int32 step = 0; step < warpIndex; ++step)
        {
            clock.StepWarpUp();
        }
        TestEqual(*FString::Printf(TEXT("Warp index after %d steps"), warpIndex), clock.GetWarpIndex(), warpIndex);
        TestEqual(*FString::Printf(TEXT("Warp factor at index %d"), warpIndex), clock.GetWarpFactor(),
            CLOCK_EXPECTED_WARPS[warpIndex], 0.0);

        // Two real seconds of advance
        clock.Advance(2.0);
        const double expectedSeconds = 2.0 * CLOCK_EXPECTED_WARPS[warpIndex];
        TestEqual(*FString::Printf(TEXT("Seconds after Advance(2) at index %d"), warpIndex),
            clock.GetSecondsSinceJ2000(), expectedSeconds, 1e-9 * expectedSeconds);
        TestEqual(*FString::Printf(TEXT("Julian date after Advance(2) at index %d"), warpIndex), clock.GetJulianDate(),
            FSOLSimClock::J2000JulianDate + expectedSeconds / SOLTestHelpers::SECONDS_PER_DAY, CLOCK_JD_TOLERANCE);
        TestEqual(*FString::Printf(TEXT("Centuries after Advance(2) at index %d"), warpIndex),
            clock.GetCenturiesSinceJ2000(), expectedSeconds / (36525.0 * SOLTestHelpers::SECONDS_PER_DAY), 1e-15);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockAdvanceAccumulatesTest, "SOLTest.SimClock.AdvanceAccumulates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Repeated Advance calls accumulate; Advance(0) changes nothing; a warp change applies to later advances only
bool FSOLSimClockAdvanceAccumulatesTest::RunTest(const FString& /*parameters*/)
{
    FSOLSimClock clock;
    clock.Init(FSOLSimClock::J2000JulianDate);
    for (int32 frame = 0; frame < 10; ++frame)
    {
        clock.Advance(0.1);
    }
    TestEqual(TEXT("10 x Advance(0.1) at 1x"), clock.GetSecondsSinceJ2000(), 1.0, 1e-12);

    clock.Advance(0.0);
    TestEqual(TEXT("Advance(0) leaves time unchanged"), clock.GetSecondsSinceJ2000(), 1.0, 1e-12);

    // Step to 10x, then advance
    clock.StepWarpUp();
    clock.Advance(0.5);
    TestEqual(TEXT("1 s at 1x plus 0.5 s at 10x"), clock.GetSecondsSinceJ2000(), 6.0, 1e-12);

    // Starting from a non-J2000 date
    clock.Init(2451544.5);
    clock.Advance(43200.0);
    TestEqual(TEXT("Midnight + 12 h at 1x reaches J2000"), clock.GetJulianDate(), 2451545.0, CLOCK_JD_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockWarpClampHighTest, "SOLTest.SimClock.WarpStepClampsHigh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// StepWarpUp climbs one step at a time and clamps at the last step (30 d/s)
bool FSOLSimClockWarpClampHighTest::RunTest(const FString& /*parameters*/)
{
    FSOLSimClock clock;
    clock.Init(FSOLSimClock::J2000JulianDate);
    for (int32 step = 1; step < CLOCK_WARP_COUNT; ++step)
    {
        clock.StepWarpUp();
        TestEqual(*FString::Printf(TEXT("Index after %d up-steps"), step), clock.GetWarpIndex(), step);
    }
    for (int32 extra = 0; extra < 5; ++extra)
    {
        clock.StepWarpUp();
    }
    TestEqual(TEXT("Index clamps at the last step"), clock.GetWarpIndex(), CLOCK_WARP_COUNT - 1);
    TestEqual(TEXT("Factor clamps at 2592000"), clock.GetWarpFactor(), 2592000.0, 0.0);

    // One step down from the top
    clock.StepWarpDown();
    TestEqual(TEXT("One down-step from the top"), clock.GetWarpIndex(), CLOCK_WARP_COUNT - 2);
    TestEqual(TEXT("Factor after one down-step"), clock.GetWarpFactor(), 864000.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockWarpClampLowTest, "SOLTest.SimClock.WarpStepClampsLow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// StepWarpDown clamps at index 0 (1x)
bool FSOLSimClockWarpClampLowTest::RunTest(const FString& /*parameters*/)
{
    FSOLSimClock clock;
    clock.Init(FSOLSimClock::J2000JulianDate);
    clock.StepWarpDown();
    clock.StepWarpDown();
    TestEqual(TEXT("Down from 0 stays 0"), clock.GetWarpIndex(), 0);
    TestEqual(TEXT("Factor stays 1"), clock.GetWarpFactor(), 1.0, 0.0);

    // Up twice, down five times
    clock.StepWarpUp();
    clock.StepWarpUp();
    TestEqual(TEXT("Two up-steps reach index 2"), clock.GetWarpIndex(), 2);
    for (int32 step = 0; step < 5; ++step)
    {
        clock.StepWarpDown();
    }
    TestEqual(TEXT("Clamped at 0 after extra down-steps"), clock.GetWarpIndex(), 0);
    TestEqual(TEXT("Factor 1 after extra down-steps"), clock.GetWarpFactor(), 1.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSimClockResetWarpTest, "SOLTest.SimClock.ResetWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ResetWarp returns to index 0 without touching sim time; Init also resets warp
bool FSOLSimClockResetWarpTest::RunTest(const FString& /*parameters*/)
{
    FSOLSimClock clock;
    clock.Init(FSOLSimClock::J2000JulianDate);
    for (int32 step = 0; step < 5; ++step)
    {
        clock.StepWarpUp();
    }
    TestEqual(TEXT("Five up-steps reach index 5"), clock.GetWarpIndex(), 5);
    clock.Advance(1.0);
    const double secondsBeforeReset = clock.GetSecondsSinceJ2000();
    TestEqual(TEXT("1 s at 1 d/s"), secondsBeforeReset, 86400.0, 1e-6);

    clock.ResetWarp();
    TestEqual(TEXT("ResetWarp index"), clock.GetWarpIndex(), 0);
    TestEqual(TEXT("ResetWarp factor"), clock.GetWarpFactor(), 1.0, 0.0);
    TestEqual(TEXT("ResetWarp keeps sim time"), clock.GetSecondsSinceJ2000(), secondsBeforeReset, 0.0);

    // Init resets warp as well
    clock.StepWarpUp();
    clock.StepWarpUp();
    clock.Init(FSOLSimClock::J2000JulianDate);
    TestEqual(TEXT("Init resets warp index"), clock.GetWarpIndex(), 0);
    TestEqual(TEXT("Init resets warp factor"), clock.GetWarpFactor(), 1.0, 0.0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
