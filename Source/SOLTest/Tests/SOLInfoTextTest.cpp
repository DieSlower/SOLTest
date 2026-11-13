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
    TestTrue(TEXT("default map keys (issue #20)"),
        joined.Contains(TEXT("The map (Shift+M; Shift+J opens it in Jump mode)")));
    TestTrue(TEXT("default resume key (issue #20)"), joined.Contains(TEXT("V resumes it")));
    TestTrue(TEXT("gas giant paragraph (issue #14)"), joined.Contains(TEXT("Great Red Spot")));
    TestTrue(TEXT("ring shadow paragraph (issue #16)"), joined.Contains(TEXT("Rings and planets cast real shadows")));
    TestTrue(TEXT("ship eclipse sentence (issue #16)"),
        joined.Contains(TEXT("Your ship and targets are shadowed too")));
    TestFalse(TEXT("no placeholder left"), joined.Contains(TEXT("{")));

    keys[static_cast<int32>(EBinding::SurfaceLock)] = TEXT("K");
    keys[static_cast<int32>(EBinding::MatchLock)] = TEXT("N");
    keys[static_cast<int32>(EBinding::ToggleAssist)] = TEXT("CapsLock");
    keys[static_cast<int32>(EBinding::OpenMap)] = TEXT("Comma");
    TestEqual(TEXT("rebound surface-lock label"), FormatSurfaceLockManualLabel(keys),
        FString(TEXT("Surface-lock with K from up to")));
    const FString rebound = FString::Join(BuildProse(keys), TEXT(" "));
    TestTrue(TEXT("rebound frame lock key"), rebound.Contains(TEXT("locked with N.")));
    TestTrue(TEXT("rebound assist key"), rebound.Contains(TEXT("off (CapsLock)")));
    TestTrue(TEXT("rebound map key"), rebound.Contains(TEXT("The map (Shift+Comma;")));
    TestFalse(TEXT("old map key gone"), rebound.Contains(TEXT("(Shift+M;")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextHudParagraphTest, "SOLTest.InfoText.HudParagraphPointsToHelp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (33d-ii, SDD 33 D25 with D1 option B, test-after) One paragraph is the HUD's: it keeps the two effects sliders and
// what they scale, the HUD glow slider (issue #37, SDD 37 D6), the HUD's look, the speed and G block's place and the
// cards' look and life, then points to Help, HUD glossary; the radar and free-look details it used to repeat are left
// to the glossary
bool FSOLInfoTextHudParagraphTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> prose = SOLInfoText::BuildProse(SOLKeyBindings::DefaultKeys());
    int32 found = INDEX_NONE;
    int32 count = 0;
    for (int32 index = 0; index < prose.Num(); ++index)
    {
        if (prose[index].Contains(TEXT("Help, HUD glossary")))
        {
            found = index;
            ++count;
        }
    }
    TestEqual(TEXT("one paragraph points to the HUD glossary"), count, 1);
    if (found == INDEX_NONE)
    {
        return false;
    }
    const FString& hud = prose[found];
    const TCHAR* const kept[] = {
        TEXT("HUD and menu effects slider"), TEXT("0 turns them off"),
        TEXT("main menu and the pause menu's side panel"),
        TEXT("Hologram menu effects slider"), TEXT("in-flight hologram menus and the mouse pointer"),
        TEXT("thin glowing blue lines and text on soft, borderless dark panels"), TEXT("speed and G (bottom left)"),
        TEXT("glowing corner marks on a faint dark tint with a slow scanline"),
        TEXT("The date and speed cards are always there"), TEXT("draws in when you select a target and fades out"),
        TEXT("only changes its text"), TEXT("info card draws in and fades out"),
        TEXT("The HUD glow slider in Settings"), TEXT("0% leaves crisp lines with no glow"),
        TEXT("and the mouse pointer with them"), TEXT("the sample under the slider shows it at once"),
        TEXT("the menus keep their own look"), TEXT("the readouts inside the hologram cards"),
        TEXT("HUD pictures in the Help glossary"), TEXT("so do the cards' corner marks"),
        TEXT("except its two route pictures, fixed pictures taken at the default"),
    };
    for (const TCHAR* const phrase : kept)
    {
        TestTrue(FString::Printf(TEXT("keeps \"%s\""), phrase), hud.Contains(phrase));
    }
    TestTrue(TEXT("the pointer ends the paragraph"), hud.EndsWith(TEXT("explained in Help, HUD glossary.")));
    TestFalse(TEXT("the radar is left to the glossary"), hud.Contains(TEXT("radar (bottom right)")));
    TestFalse(TEXT("free look is left to the glossary"), hud.Contains(TEXT("joystick circles turn yellow")));
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

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextOverlayMenuLanesTest, "SOLTest.InfoText.OverlayMenuControlsLanes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #38 (SDD 38 section 7, D19): exactly one Controls row is the Overlay Menu's, and it lists the NPC lanes among
// the row kinds, after ships on radar and before darkening, with no placeholder left. Since issue #49 (SDD 49 section
// 5, case 22) the row reads "NPC lanes and tunnels"
bool FSOLInfoTextOverlayMenuLanesTest::RunTest(const FString& /*parameters*/)
{
    int32 found = 0;
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        if (FString(row.Bindings) != TEXT("@OverlayMenu"))
        {
            continue;
        }
        ++found;
        const FString action = row.Action;
        TestTrue(TEXT("the row names the row kinds with NPC lanes and tunnels"), action.Contains(
            TEXT("(icons per kind, orbits per kind, ships on radar, NPC lanes and tunnels, darkening); also in "
                "Settings"), ESearchCase::CaseSensitive));
        const int32 radar = action.Find(TEXT("ships on radar"), ESearchCase::CaseSensitive);
        const int32 lanes = action.Find(TEXT("NPC lanes"), ESearchCase::CaseSensitive);
        const int32 darkening = action.Find(TEXT("darkening"), ESearchCase::CaseSensitive);
        TestTrue(TEXT("NPC lanes after ships on radar"), radar != INDEX_NONE && lanes > radar);
        TestTrue(TEXT("NPC lanes before darkening"), lanes != INDEX_NONE && darkening > lanes);
        TestFalse(TEXT("no '{' left"), action.Contains(TEXT("{")));
    }
    TestEqual(TEXT("exactly one Overlay Menu row"), found, 1);
    return true;
}

namespace
{
    // The Info overlay paragraph's lane sentences of SDD 38 section 7 (D19), binding token unformatted
    constexpr const TCHAR* INFOTEXT_LANES_SENTENCE =
        TEXT("With NPC ships out (the Demo page), each lane between two bodies is drawn as a pair of dashed hologram "
            "lines, teal-white for traffic leaving the inner body and cyan for traffic coming back, the dashes "
            "flowing each way; a busier lane is wider and brighter. Close to a lane the lines fade out and its ships "
            "show one by one. The Lanes row of the Overlay Menu ({OverlayMenu}) turns them off (the ships then show "
            "as points, as before); the lines glow with the HUD glow setting.");

    // The Info Demo paragraph's far-ship clause of SDD 38 section 7
    constexpr const TCHAR* INFOTEXT_DEMO_LANE_LINES =
        TEXT("farther ones follow simpler motion and are drawn as points, or as lane lines (see the overlays).");

    // Returns the indices of the paragraphs that contain a text (case-sensitive)
    TArray<int32> InfoTextParagraphsWith(const TArray<FString>& prose, const FString& text)
    {
        TArray<int32> found;
        for (int32 index = 0; index < prose.Num(); ++index)
        {
            if (prose[index].Contains(text, ESearchCase::CaseSensitive))
            {
                found.Add(index);
            }
        }
        return found;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextLanesSentenceTest, "SOLTest.InfoText.LanesSentence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #38 Part 38b (SDD 38 section 6 case 24, section 7), test-first: the overlay paragraph (the one that explains
// orbit lines) carries the lane sentences after its orbit-lines sentence, with {OverlayMenu} resolved to the current
// key before and after a rebind, and no placeholder left
bool FSOLInfoTextLanesSentenceTest::RunTest(const FString& /*parameters*/)
{
    using SOLKeyBindings::EBinding;
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const TArray<FString> prose = SOLInfoText::BuildProse(keys);
    const FString expected = SOLKeyBindings::FormatKeyText(INFOTEXT_LANES_SENTENCE, keys);
    TestFalse(TEXT("the expected sentence resolves its token"), expected.Contains(TEXT("{")));

    const TArray<int32> found = InfoTextParagraphsWith(prose, expected);
    if (!TestEqual(TEXT("exactly one paragraph has the lane sentences"), found.Num(), 1))
    {
        return false;
    }
    int32 orbitCount = 0;
    const int32 orbitParagraph = InfoTextFindOrbitLinesParagraph(prose, orbitCount);
    TestEqual(TEXT("in the overlay paragraph (the orbit-lines one)"), found[0], orbitParagraph);
    const FString& paragraph = prose[found[0]];
    const int32 orbitAt = paragraph.Find(TEXT("Orbit lines ("), ESearchCase::CaseSensitive);
    const int32 lanesAt = paragraph.Find(expected, ESearchCase::CaseSensitive);
    TestTrue(TEXT("after the orbit-lines sentence"), orbitAt != INDEX_NONE && lanesAt > orbitAt);
    TestFalse(TEXT("no '{' left"), paragraph.Contains(TEXT("{")));
    TestFalse(TEXT("no '}' left"), paragraph.Contains(TEXT("}")));

    // A rebound Overlay Menu key shows in the sentence
    keys[static_cast<int32>(EBinding::OverlayMenu)] = TEXT("F9");
    const FString rebound = SOLKeyBindings::FormatKeyText(INFOTEXT_LANES_SENTENCE, keys);
    TestNotEqual(TEXT("the rebind changes the sentence"), rebound, expected);
    TestEqual(TEXT("rebound: exactly one paragraph has the lane sentences"),
        InfoTextParagraphsWith(SOLInfoText::BuildProse(keys), rebound).Num(), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextDemoLaneLinesTest, "SOLTest.InfoText.DemoLaneLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #38 Part 38b (SDD 38 section 7), test-first: the Demo paragraph (the one naming Ctrl+F1) says far ships are
// drawn as points or as lane lines, pointing to the overlays
bool FSOLInfoTextDemoLaneLinesTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> prose = SOLInfoText::BuildProse(SOLKeyBindings::DefaultKeys());
    const TArray<int32> found = InfoTextParagraphsWith(prose, INFOTEXT_DEMO_LANE_LINES);
    if (!TestEqual(TEXT("exactly one paragraph has the lane-lines clause"), found.Num(), 1))
    {
        return false;
    }
    TestTrue(TEXT("it is the Demo paragraph"), prose[found[0]].Contains(TEXT("Ctrl+F1"), ESearchCase::CaseSensitive)
        && prose[found[0]].Contains(TEXT("Demo"), ESearchCase::CaseSensitive));
    TestFalse(TEXT("the old full stop after points is gone"),
        prose[found[0]].Contains(TEXT("are drawn as points. "), ESearchCase::CaseSensitive));
    return true;
}

namespace
{
    // The Info overlay paragraph's lane tunnel sentences of SDD 49 section 5 (issue #49 Part 49b)
    constexpr const TCHAR* INFOTEXT_TUNNEL_SENTENCE =
        TEXT("Close to a lane (the tunnel fades in between 100 and 50 lane radii away) its dashed lines fade "
            "out and become a tunnel of cyan rings that you can fly through, and its ships show as points again. The "
            "lane's rule is keep right: the green dots and chevrons on the right wall mark the way out from the inner "
            "body, the red ones on the left the way back. The rings near you stay fixed to the lane and the far ones "
            "also slide when you move toward or away from its axis, and everything moves with the lane under time "
            "warp. A label on each tunnel's wall names the lane, the distance to each end and how many ships use it, "
            "or only the lane and the nearest end where space is short (the nearest tunnel's label is the "
            "brightest). Tunnels follow the Lanes row and the HUD glow and are not shown on the map.");
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextTunnelSentenceTest, "SOLTest.InfoText.TunnelSentence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #49 Part 49b (SDD 49 section 5, case 22), test-first: the overlay paragraph carries the lane tunnel sentences
// once, after the #38 lane sentences and before the space dust, before and after a rebind of the Overlay Menu key, with
// no placeholder left; the text states keep right as the lane's rule and names no "your side"
bool FSOLInfoTextTunnelSentenceTest::RunTest(const FString& /*parameters*/)
{
    using SOLKeyBindings::EBinding;
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const TArray<FString> prose = SOLInfoText::BuildProse(keys);
    const TArray<int32> found = InfoTextParagraphsWith(prose, INFOTEXT_TUNNEL_SENTENCE);
    if (!TestEqual(TEXT("exactly one paragraph has the tunnel sentences"), found.Num(), 1))
    {
        return false;
    }
    int32 orbitCount = 0;
    TestEqual(TEXT("in the overlay paragraph (the orbit-lines one)"), found[0],
        InfoTextFindOrbitLinesParagraph(prose, orbitCount));
    const FString& paragraph = prose[found[0]];
    const FString lanes = SOLKeyBindings::FormatKeyText(INFOTEXT_LANES_SENTENCE, keys);
    const int32 lanesAt = paragraph.Find(lanes, ESearchCase::CaseSensitive);
    const int32 tunnelAt = paragraph.Find(INFOTEXT_TUNNEL_SENTENCE, ESearchCase::CaseSensitive);
    const int32 dustAt = paragraph.Find(TEXT("Space dust"), ESearchCase::CaseSensitive);
    TestTrue(TEXT("after the #38 lane sentences"), lanesAt != INDEX_NONE && tunnelAt > lanesAt);
    TestTrue(TEXT("before the space dust"), dustAt != INDEX_NONE && dustAt > tunnelAt);
    TestEqual(TEXT("the tunnel text appears once"), paragraph.Find(INFOTEXT_TUNNEL_SENTENCE,
        ESearchCase::CaseSensitive, ESearchDir::FromEnd), tunnelAt);
    TestFalse(TEXT("no '{' left"), paragraph.Contains(TEXT("{")));
    TestFalse(TEXT("no 'your side'"), FString(INFOTEXT_TUNNEL_SENTENCE).Contains(TEXT("your side")));

    // A rebound Overlay Menu key leaves the tunnel text in place
    keys[static_cast<int32>(EBinding::OverlayMenu)] = TEXT("F9");
    TestEqual(TEXT("rebound: exactly one paragraph has the tunnel sentences"),
        InfoTextParagraphsWith(SOLInfoText::BuildProse(keys), INFOTEXT_TUNNEL_SENTENCE).Num(), 1);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
