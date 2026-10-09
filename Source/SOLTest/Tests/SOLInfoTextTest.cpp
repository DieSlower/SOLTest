/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLInfoText.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr double INFOTEXT_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double INFOTEXT_INF = std::numeric_limits<double>::infinity();

    //////////////////////////////////////////////////////////////////////////
    // Builds a set of combat lines from plausible, independently chosen values
    TArray<SOLInfoText::FInfoLine> InfoTextSampleLines()
    {
        return SOLInfoText::BuildCombatLines(20000.0, 25.0, 8.0, 100.0, 100.0, 20.0, 3.0);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextSpeedMetersTest, "SOLTest.InfoText.SpeedMeters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Speeds below 1000 m/s print in m/s with one decimal
bool FSOLInfoTextSpeedMetersTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLInfoText;
    TestEqual(TEXT("Zero"), FormatSpeed(0.0), FString(TEXT("0.0 m/s")));
    TestEqual(TEXT("Integer"), FormatSpeed(250.0), FString(TEXT("250.0 m/s")));
    TestEqual(TEXT("Fraction"), FormatSpeed(12.5), FString(TEXT("12.5 m/s")));
    TestEqual(TEXT("Just below km/s"), FormatSpeed(999.0), FString(TEXT("999.0 m/s")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextSpeedKilometersTest, "SOLTest.InfoText.SpeedKilometers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 1000 m/s up to below 1,000,000 m/s prints in km/s with two decimals; above that with none and no separators
bool FSOLInfoTextSpeedKilometersTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLInfoText;
    TestEqual(TEXT("Exactly 1 km/s"), FormatSpeed(1000.0), FString(TEXT("1.00 km/s")));
    TestEqual(TEXT("Two decimals"), FormatSpeed(12340.0), FString(TEXT("12.34 km/s")));
    TestEqual(TEXT("Below the 0-decimal band"), FormatSpeed(999000.0), FString(TEXT("999.00 km/s")));
    TestEqual(TEXT("Exactly 1,000,000 m/s"), FormatSpeed(1000000.0), FString(TEXT("1000 km/s")));
    TestEqual(TEXT("Light speed, no thousands separator"), FormatSpeed(299792458.0), FString(TEXT("299792 km/s")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextSpeedEdgeTest, "SOLTest.InfoText.SpeedEdge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-finite speeds print "n/a"; negative speeds get a leading '-' and pick the band by magnitude
bool FSOLInfoTextSpeedEdgeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLInfoText;
    TestEqual(TEXT("NaN"), FormatSpeed(INFOTEXT_NAN), FString(TEXT("n/a")));
    TestEqual(TEXT("+inf"), FormatSpeed(INFOTEXT_INF), FString(TEXT("n/a")));
    TestEqual(TEXT("-inf"), FormatSpeed(-INFOTEXT_INF), FString(TEXT("n/a")));
    TestEqual(TEXT("Negative m/s"), FormatSpeed(-500.0), FString(TEXT("-500.0 m/s")));
    TestEqual(TEXT("Negative km/s"), FormatSpeed(-5000.0), FString(TEXT("-5.00 km/s")));
    TestEqual(TEXT("Negative large km/s"), FormatSpeed(-2000000.0), FString(TEXT("-2000 km/s")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextRateTest, "SOLTest.InfoText.Rate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Rates print with up to one decimal, dropping a trailing ".0"; NaN prints "n/a"
bool FSOLInfoTextRateTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLInfoText;
    TestEqual(TEXT("Integer rate"), FormatRate(8.0), FString(TEXT("8 /s")));
    TestEqual(TEXT("Half rate"), FormatRate(2.5), FString(TEXT("2.5 /s")));
    TestEqual(TEXT("Zero"), FormatRate(0.0), FString(TEXT("0 /s")));
    TestEqual(TEXT("Rounds to integer then trims"), FormatRate(1.04), FString(TEXT("1 /s")));
    TestEqual(TEXT("Large integer"), FormatRate(120.0), FString(TEXT("120 /s")));
    TestEqual(TEXT("NaN"), FormatRate(INFOTEXT_NAN), FString(TEXT("n/a")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextValueTest, "SOLTest.InfoText.Value",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Integers print with no decimals, other values with one decimal; NaN prints "n/a"
bool FSOLInfoTextValueTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLInfoText;
    TestEqual(TEXT("Integer"), FormatPercentOrValue(100.0), FString(TEXT("100")));
    TestEqual(TEXT("Zero"), FormatPercentOrValue(0.0), FString(TEXT("0")));
    TestEqual(TEXT("Fraction"), FormatPercentOrValue(2.5), FString(TEXT("2.5")));
    TestEqual(TEXT("Long fraction to one decimal"), FormatPercentOrValue(33.33), FString(TEXT("33.3")));
    TestEqual(TEXT("Negative integer"), FormatPercentOrValue(-3.0), FString(TEXT("-3")));
    TestEqual(TEXT("NaN"), FormatPercentOrValue(INFOTEXT_NAN), FString(TEXT("n/a")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextCombatLabelsTest, "SOLTest.InfoText.CombatLabels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The combat block has exactly six lines with fixed labels in a fixed order
bool FSOLInfoTextCombatLabelsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<SOLInfoText::FInfoLine> lines = InfoTextSampleLines();
    if (!TestEqual(TEXT("Six lines"), lines.Num(), 6))
    {
        return false;
    }
    TestEqual(TEXT("Line 0 label"), lines[0].Label, FString(TEXT("Bolt speed")));
    TestEqual(TEXT("Line 1 label"), lines[1].Label, FString(TEXT("Damage per hit")));
    TestEqual(TEXT("Line 2 label"), lines[2].Label, FString(TEXT("Fire rate")));
    TestEqual(TEXT("Line 3 label"), lines[3].Label, FString(TEXT("Target shield")));
    TestEqual(TEXT("Line 4 label"), lines[4].Label, FString(TEXT("Target health")));
    TestEqual(TEXT("Line 5 label"), lines[5].Label, FString(TEXT("Shield regen")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextCombatValuesTest, "SOLTest.InfoText.CombatValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each combat line's value is formatted with the matching formatter
bool FSOLInfoTextCombatValuesTest::RunTest(const FString& /*parameters*/)
{
    const TArray<SOLInfoText::FInfoLine> lines =
        SOLInfoText::BuildCombatLines(20000.0, 25.0, 8.0, 100.0, 150.5, 2.5, 3.0);
    if (!TestEqual(TEXT("Six lines"), lines.Num(), 6))
    {
        return false;
    }
    TestEqual(TEXT("Bolt speed"), lines[0].Value, FString(TEXT("20.00 km/s")));
    TestEqual(TEXT("Damage per hit"), lines[1].Value, FString(TEXT("25")));
    TestEqual(TEXT("Fire rate"), lines[2].Value, FString(TEXT("8 /s")));
    TestEqual(TEXT("Target shield"), lines[3].Value, FString(TEXT("100")));
    TestEqual(TEXT("Target health"), lines[4].Value, FString(TEXT("150.5")));
    TestEqual(TEXT("Shield regen"), lines[5].Value, FString(TEXT("2.5 /s after 3 s")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextCombatEdgeTest, "SOLTest.InfoText.CombatEdge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// NaN inputs still give six lines, with "n/a" in place of each bad value
bool FSOLInfoTextCombatEdgeTest::RunTest(const FString& /*parameters*/)
{
    const TArray<SOLInfoText::FInfoLine> lines = SOLInfoText::BuildCombatLines(INFOTEXT_NAN, INFOTEXT_NAN,
        INFOTEXT_NAN, INFOTEXT_NAN, INFOTEXT_NAN, INFOTEXT_NAN, 0.5);
    if (!TestEqual(TEXT("Still six lines"), lines.Num(), 6))
    {
        return false;
    }
    TestEqual(TEXT("Bolt speed n/a"), lines[0].Value, FString(TEXT("n/a")));
    TestEqual(TEXT("Damage n/a"), lines[1].Value, FString(TEXT("n/a")));
    TestEqual(TEXT("Fire rate n/a"), lines[2].Value, FString(TEXT("n/a")));
    TestEqual(TEXT("Shield n/a"), lines[3].Value, FString(TEXT("n/a")));
    TestEqual(TEXT("Health n/a"), lines[4].Value, FString(TEXT("n/a")));
    TestEqual(TEXT("Regen n/a with valid delay"), lines[5].Value, FString(TEXT("n/a after 0.5 s")));
    TestEqual(TEXT("Labels survive bad values"), lines[0].Label, FString(TEXT("Bolt speed")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextDeterminismTest, "SOLTest.InfoText.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The same inputs always produce identical lines and strings
bool FSOLInfoTextDeterminismTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLInfoText;
    const TArray<FInfoLine> first = InfoTextSampleLines();
    const TArray<FInfoLine> second = InfoTextSampleLines();
    if (!TestEqual(TEXT("Same line count"), first.Num(), second.Num()))
    {
        return false;
    }
    // Compare every label and value pairwise
    for (int32 index = 0; index < first.Num(); ++index)
    {
        TestEqual(TEXT("Same label"), first[index].Label, second[index].Label);
        TestEqual(TEXT("Same value"), first[index].Value, second[index].Value);
    }
    TestEqual(TEXT("FormatSpeed repeatable"), FormatSpeed(4321.0), FormatSpeed(4321.0));
    TestEqual(TEXT("FormatRate repeatable"), FormatRate(2.5), FormatRate(2.5));
    TestEqual(TEXT("FormatPercentOrValue repeatable"), FormatPercentOrValue(7.25), FormatPercentOrValue(7.25));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
