/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLInfoText.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"

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

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextFollowsRebindsTest, "SOLTest.InfoText.FollowsRebinds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The surface-lock label and the prose name the player's current keys, before and after a rebind
bool FSOLInfoTextFollowsRebindsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLInfoText;
    using SOLKeyBindings::EBinding;
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    TestEqual(TEXT("default surface-lock label"), FormatSurfaceLockManualLabel(keys),
        FString(TEXT("Surface-lock with L from up to")));
    TArray<FString> prose = BuildProse(keys);
    TestEqual(TEXT("one paragraph per prose entry"), prose.Num(),
        static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::INFO_PROSE)));
    const FString joined = FString::Join(prose, TEXT(" "));
    TestTrue(TEXT("default frame lock key"), joined.Contains(TEXT("locked with M.")));
    TestTrue(TEXT("default assist key"), joined.Contains(TEXT("off (C)")));
    TestTrue(TEXT("default map key"), joined.Contains(TEXT("jump map (J)")));
    TestTrue(TEXT("gas giant paragraph (issue #14)"), joined.Contains(TEXT("Great Red Spot")));
    TestTrue(TEXT("ring shadow paragraph (issue #16)"), joined.Contains(TEXT("Rings and planets cast real shadows")));
    TestTrue(TEXT("ship eclipse sentence (issue #16)"),
        joined.Contains(TEXT("Your ship and targets are shadowed too")));
    TestFalse(TEXT("no placeholder left"), joined.Contains(TEXT("{")));

    keys[static_cast<int32>(EBinding::SurfaceLock)] = TEXT("K");
    keys[static_cast<int32>(EBinding::MatchLock)] = TEXT("N");
    keys[static_cast<int32>(EBinding::ToggleAssist)] = TEXT("CapsLock");
    keys[static_cast<int32>(EBinding::ToggleMap)] = TEXT("Comma");
    TestEqual(TEXT("rebound surface-lock label"), FormatSurfaceLockManualLabel(keys),
        FString(TEXT("Surface-lock with K from up to")));
    const FString rebound = FString::Join(BuildProse(keys), TEXT(" "));
    TestTrue(TEXT("rebound frame lock key"), rebound.Contains(TEXT("locked with N.")));
    TestTrue(TEXT("rebound assist key"), rebound.Contains(TEXT("off (CapsLock)")));
    TestTrue(TEXT("rebound map key"), rebound.Contains(TEXT("jump map (Comma)")));
    TestFalse(TEXT("old map key gone"), rebound.Contains(TEXT("(J)")));
    return true;
}

namespace
{
    // Returns the index of the only paragraph that mentions orbit lines (case-insensitive), or INDEX_NONE when none or
    // several do; outCount is the number that do
    int32 InfoTextFindOrbitLinesParagraph(const TArray<FString>& prose, int32& outCount)
    {
        outCount = 0;
        int32 found = INDEX_NONE;
        for (int32 index = 0; index < prose.Num(); ++index)
        {
            if (prose[index].Contains(TEXT("orbit lines"), ESearchCase::IgnoreCase))
            {
                ++outCount;
                found = index;
            }
        }
        return outCount == 1 ? found : INDEX_NONE;
    }

    // Returns the displayed key label of the orbit-lines toggle for a key array
    FString InfoTextOrbitLinesKeyLabel(const TArray<FString>& keys)
    {
        const FString format = FString::Printf(TEXT("{%s}"),
            SOLKeyBindings::GetDef(static_cast<int32>(SOLKeyBindings::EBinding::ToggleOrbitLines)).Id);
        return SOLKeyBindings::FormatKeyText(*format, keys);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextOrbitLinesParagraphTest, "SOLTest.InfoText.OrbitLinesParagraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Exactly one paragraph explains orbit lines and names the current orbit-lines toggle key, before and after a rebind
bool FSOLInfoTextOrbitLinesParagraphTest::RunTest(const FString& /*parameters*/)
{
    using SOLKeyBindings::EBinding;
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const TArray<FString> prose = SOLInfoText::BuildProse(keys);
    int32 count = 0;
    const int32 paragraph = InfoTextFindOrbitLinesParagraph(prose, count);
    TestEqual(TEXT("exactly one orbit-lines paragraph"), count, 1);
    if (!TestTrue(TEXT("orbit-lines paragraph found"), paragraph != INDEX_NONE))
    {
        return false;
    }
    const FString defaultLabel = InfoTextOrbitLinesKeyLabel(keys);
    TestFalse(TEXT("default toggle key has a label"), defaultLabel.IsEmpty());
    TestTrue(TEXT("paragraph names the default toggle key"),
        prose[paragraph].Contains(defaultLabel, ESearchCase::CaseSensitive));
    TestFalse(TEXT("default paragraph does not name the later key"),
        prose[paragraph].Contains(TEXT("F7"), ESearchCase::CaseSensitive));

    // No paragraph keeps a placeholder brace
    for (const FString& text : prose)
    {
        TestFalse(TEXT("no '{' left"), text.Contains(TEXT("{")));
        TestFalse(TEXT("no '}' left"), text.Contains(TEXT("}")));
    }

    // Rebinding the toggle changes that paragraph to the new key
    keys[static_cast<int32>(EBinding::ToggleOrbitLines)] = TEXT("F7");
    const FString reboundLabel = InfoTextOrbitLinesKeyLabel(keys);
    TestNotEqual(TEXT("rebound label differs from the default"), reboundLabel, defaultLabel);
    const TArray<FString> reboundProse = SOLInfoText::BuildProse(keys);
    int32 reboundCount = 0;
    const int32 reboundParagraph = InfoTextFindOrbitLinesParagraph(reboundProse, reboundCount);
    TestEqual(TEXT("still exactly one orbit-lines paragraph"), reboundCount, 1);
    if (!TestTrue(TEXT("rebound orbit-lines paragraph found"), reboundParagraph != INDEX_NONE))
    {
        return false;
    }
    TestTrue(TEXT("paragraph names the rebound key"),
        reboundProse[reboundParagraph].Contains(reboundLabel, ESearchCase::CaseSensitive));
    TestNotEqual(TEXT("paragraph text changed with the rebind"), reboundProse[reboundParagraph], prose[paragraph]);
    for (const FString& text : reboundProse)
    {
        TestFalse(TEXT("rebound: no '{' left"), text.Contains(TEXT("{")));
        TestFalse(TEXT("rebound: no '}' left"), text.Contains(TEXT("}")));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextDemoParagraphTest, "SOLTest.InfoText.DemoParagraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One paragraph names the Ctrl+F1 menu hotkey and the Demo page (issue #11, Part 10e), and no placeholder is left
bool FSOLInfoTextDemoParagraphTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> prose = SOLInfoText::BuildProse(SOLKeyBindings::DefaultKeys());
    bool bFoundDemo = false;
    for (const FString& text : prose)
    {
        bFoundDemo |= text.Contains(TEXT("Ctrl+F1"), ESearchCase::CaseSensitive)
            && text.Contains(TEXT("Demo"), ESearchCase::CaseSensitive);
        TestFalse(TEXT("no '{' left"), text.Contains(TEXT("{")));
    }
    TestTrue(TEXT("a paragraph names Ctrl+F1 and the Demo page"), bFoundDemo);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
