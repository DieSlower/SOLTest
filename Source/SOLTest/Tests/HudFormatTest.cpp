/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudFormat.h"

#include "SOLConstants.h"
#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Light speed as the contract states it (independent of the code under test)
    constexpr double HUDFORMAT_C_MPS = 299792458.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns a readable name for a speed unit, for test messages
    const TCHAR* HudFormatUnitName(const ESOLSpeedUnit unit)
    {
        switch (unit)
        {
        case ESOLSpeedUnit::MetersPerSecond:
            return TEXT("m/s");
        case ESOLSpeedUnit::KilometersPerSecond:
            return TEXT("km/s");
        case ESOLSpeedUnit::LightSpeed:
            return TEXT("c");
        }
        return TEXT("?");
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts PickSpeedUnit returns the expected unit for a speed
    void HudFormatExpectUnit(FAutomationTestBase& test, const double speedMps, const ESOLSpeedUnit expected)
    {
        const ESOLSpeedUnit actual = SOLHudFormat::PickSpeedUnit(speedMps);
        test.TestTrue(FString::Printf(TEXT("PickSpeedUnit(%.6f) is %s (got %s)"), speedMps, HudFormatUnitName(expected),
            HudFormatUnitName(actual)), actual == expected);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts FormatDistanceM produces an exact string
    void HudFormatExpectDistance(FAutomationTestBase& test, const double distanceM, const TCHAR* expected)
    {
        test.TestEqual(FString::Printf(TEXT("FormatDistanceM(%.3f)"), distanceM), SOLHudFormat::FormatDistanceM(distanceM),
            FString(expected));
    }
}

// --- PickSpeedUnit ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatPickMetersKmTest, "SOLTest.HudFormat.PickUnitMetersToKmBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero and speeds under 1000 m/s pick m/s; 1000 m/s and above (below 0.01c) pick km/s
bool FSOLHudFormatPickMetersKmTest::RunTest(const FString& /*parameters*/)
{
    HudFormatExpectUnit(*this, 0.0, ESOLSpeedUnit::MetersPerSecond);
    HudFormatExpectUnit(*this, 1.0, ESOLSpeedUnit::MetersPerSecond);
    HudFormatExpectUnit(*this, 999.0, ESOLSpeedUnit::MetersPerSecond);
    HudFormatExpectUnit(*this, 999.9999, ESOLSpeedUnit::MetersPerSecond);
    HudFormatExpectUnit(*this, 1000.0, ESOLSpeedUnit::KilometersPerSecond);
    HudFormatExpectUnit(*this, 1000.0001, ESOLSpeedUnit::KilometersPerSecond);
    HudFormatExpectUnit(*this, 29780.0, ESOLSpeedUnit::KilometersPerSecond);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatPickLightTest, "SOLTest.HudFormat.PickUnitKmToLightBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Speeds just under 0.01c pick km/s; just over 0.01c and up to the max cap pick c
bool FSOLHudFormatPickLightTest::RunTest(const FString& /*parameters*/)
{
    const double onePercentC = 0.01 * HUDFORMAT_C_MPS;
    HudFormatExpectUnit(*this, 2997924.0, ESOLSpeedUnit::KilometersPerSecond);
    HudFormatExpectUnit(*this, onePercentC * (1.0 - 1e-9), ESOLSpeedUnit::KilometersPerSecond);
    HudFormatExpectUnit(*this, onePercentC * (1.0 + 1e-9), ESOLSpeedUnit::LightSpeed);
    HudFormatExpectUnit(*this, 3000000.0, ESOLSpeedUnit::LightSpeed);
    HudFormatExpectUnit(*this, 0.1 * HUDFORMAT_C_MPS, ESOLSpeedUnit::LightSpeed);
    HudFormatExpectUnit(*this, SOL::MAX_SPEED_CAP_MPS, ESOLSpeedUnit::LightSpeed);
    HudFormatExpectUnit(*this, HUDFORMAT_C_MPS, ESOLSpeedUnit::LightSpeed);
    return true;
}

// --- ToUnitValue / FromUnitValue ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatToUnitTest, "SOLTest.HudFormat.ToUnitValueFactors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// m/s is unchanged, km/s divides by 1000, c divides by exactly 299792458.0
bool FSOLHudFormatToUnitTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("m/s unchanged"), SOLHudFormat::ToUnitValue(123.45, ESOLSpeedUnit::MetersPerSecond), 123.45, 1e-12);
    TestEqual(TEXT("1500 m/s = 1.5 km/s"), SOLHudFormat::ToUnitValue(1500.0, ESOLSpeedUnit::KilometersPerSecond), 1.5,
        1e-15);
    TestEqual(TEXT("c in c units = 1"), SOLHudFormat::ToUnitValue(HUDFORMAT_C_MPS, ESOLSpeedUnit::LightSpeed), 1.0, 1e-15);
    TestEqual(TEXT("Max cap = 0.5 c"), SOLHudFormat::ToUnitValue(SOL::MAX_SPEED_CAP_MPS, ESOLSpeedUnit::LightSpeed), 0.5,
        1e-15);
    TestEqual(TEXT("1 m/s in c units"), SOLHudFormat::ToUnitValue(1.0, ESOLSpeedUnit::LightSpeed), 1.0 / HUDFORMAT_C_MPS,
        1e-24);
    TestEqual(TEXT("Zero km/s"), SOLHudFormat::ToUnitValue(0.0, ESOLSpeedUnit::KilometersPerSecond), 0.0, 0.0);
    TestEqual(TEXT("FromUnitValue km/s multiplies by 1000"),
        SOLHudFormat::FromUnitValue(2.5, ESOLSpeedUnit::KilometersPerSecond), 2500.0, 1e-12);
    TestEqual(TEXT("FromUnitValue c multiplies by light speed"),
        SOLHudFormat::FromUnitValue(0.25, ESOLSpeedUnit::LightSpeed), 0.25 * HUDFORMAT_C_MPS, 1e-6);
    TestEqual(TEXT("FromUnitValue m/s unchanged"), SOLHudFormat::FromUnitValue(42.0, ESOLSpeedUnit::MetersPerSecond), 42.0,
        1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatRoundTripTest, "SOLTest.HudFormat.UnitRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FromUnitValue(ToUnitValue(x)) returns x for every unit across magnitudes, including 0 and the max cap
bool FSOLHudFormatRoundTripTest::RunTest(const FString& /*parameters*/)
{
    const double speeds[] = { 0.0, 1.0, 0.37, 999.0, 1000.0, 29780.0, 2997924.58, 0.1 * HUDFORMAT_C_MPS,
        SOL::MAX_SPEED_CAP_MPS * (1.0 - 1e-12), SOL::MAX_SPEED_CAP_MPS };
    const ESOLSpeedUnit units[] = { ESOLSpeedUnit::MetersPerSecond, ESOLSpeedUnit::KilometersPerSecond,
        ESOLSpeedUnit::LightSpeed };

    // Every speed through every unit and back
    for (const ESOLSpeedUnit unit : units)
    {
        for (const double speed : speeds)
        {
            const double back = SOLHudFormat::FromUnitValue(SOLHudFormat::ToUnitValue(speed, unit), unit);
            const bool bNear = speed == 0.0 ? back == 0.0 : SOLTestHelpers::RelativeError(back, speed) < 1e-14;
            TestTrue(FString::Printf(TEXT("Round trip %.6f m/s via %s (got %.6f)"), speed, HudFormatUnitName(unit), back),
                bNear);
        }
    }
    return true;
}

// --- FormatDistanceM ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatDistanceMetersTest, "SOLTest.HudFormat.DistanceMetersBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Under 1000 m prints meters with one decimal
bool FSOLHudFormatDistanceMetersTest::RunTest(const FString& /*parameters*/)
{
    HudFormatExpectDistance(*this, 0.0, TEXT("0.0 m"));
    HudFormatExpectDistance(*this, 7.0, TEXT("7.0 m"));
    HudFormatExpectDistance(*this, 512.3, TEXT("512.3 m"));
    HudFormatExpectDistance(*this, 999.9, TEXT("999.9 m"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatDistanceKmTest, "SOLTest.HudFormat.DistanceKilometersBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From 1000 m up to just under 0.01 AU prints kilometers with one decimal
bool FSOLHudFormatDistanceKmTest::RunTest(const FString& /*parameters*/)
{
    HudFormatExpectDistance(*this, 1000.0, TEXT("1.0 km"));
    HudFormatExpectDistance(*this, 1500.0, TEXT("1.5 km"));
    HudFormatExpectDistance(*this, 23402900.0, TEXT("23402.9 km"));
    HudFormatExpectDistance(*this, 384400000.0, TEXT("384400.0 km"));

    // 0.01 AU = 1495978707 m; one meter below stays in km
    HudFormatExpectDistance(*this, 1495978706.0, TEXT("1495978.7 km"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatDistanceAuTest, "SOLTest.HudFormat.DistanceAstronomicalUnitBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At and above 0.01 AU prints AU with four decimals, including very large distances
bool FSOLHudFormatDistanceAuTest::RunTest(const FString& /*parameters*/)
{
    // One meter above 0.01 AU (1495978707 m); the exact boundary is left to floating-point comparison order
    HudFormatExpectDistance(*this, 1495978708.0, TEXT("0.0100 AU"));
    HudFormatExpectDistance(*this, SOLTestHelpers::AU_M, TEXT("1.0000 AU"));
    HudFormatExpectDistance(*this, 1.5 * SOLTestHelpers::AU_M, TEXT("1.5000 AU"));
    HudFormatExpectDistance(*this, 30.0 * SOLTestHelpers::AU_M, TEXT("30.0000 AU"));
    HudFormatExpectDistance(*this, 50.0 * SOLTestHelpers::AU_M, TEXT("50.0000 AU"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatAppendDistanceTest, "SOLTest.HudFormat.AppendDistanceMatchesFormat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendDistanceM appends exactly FormatDistanceM's text after any existing buffer content, in every band
bool FSOLHudFormatAppendDistanceTest::RunTest(const FString& /*parameters*/)
{
    const double distancesM[] = { 0.0, 7.0, 999.9, 1000.0, 23402900.0, 1495978706.0, 1495978708.0,
        SOLTestHelpers::AU_M, 50.0 * SOLTestHelpers::AU_M, 1.0e7 };

    // Into an empty buffer and after a prefix, the appended text equals the formatted string
    for (const double distanceM : distancesM)
    {
        const FString expected = SOLHudFormat::FormatDistanceM(distanceM);
        FString empty;
        SOLHudFormat::AppendDistanceM(empty, distanceM);
        TestEqual(FString::Printf(TEXT("AppendDistanceM(%.3f) into empty"), distanceM), empty, expected);
        FString prefixed(TEXT("RADAR "));
        SOLHudFormat::AppendDistanceM(prefixed, distanceM);
        TestEqual(FString::Printf(TEXT("AppendDistanceM(%.3f) after prefix"), distanceM), prefixed,
            FString(TEXT("RADAR ")) + expected);
    }

    // The radar's default floor reads as the in-game label shows it
    FString label;
    SOLHudFormat::AppendDistanceM(label, 1.0e7);
    TestEqual(TEXT("10,000 km floor label"), label, FString(TEXT("10000.0 km")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
