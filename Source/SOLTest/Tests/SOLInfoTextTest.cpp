/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLActionTable.h"
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
    // The Info overlay paragraph's lane sentences of SDD 38 section 7 (D19), binding token unformatted; since SDD 52
    // (D1, D6) the flight view draws every lane as its tunnel at every distance and the map keeps the dashed lines
    constexpr const TCHAR* INFOTEXT_LANES_SENTENCE =
        TEXT("With NPC ships out (the Demo page), each lane between two bodies is drawn in flight as a tunnel of cyan "
            "rings 12 tunnel radii apart, at every distance: a ring too small or too crowded on screen fades out, so "
            "from far away a lane may show only a short run of rings or its end rings, and its ships far from you are "
            "not drawn. On the map each lane is a pair of dashed hologram lines instead, teal-white for traffic "
            "leaving the inner body and cyan for traffic coming back, the dashes flowing each way; a busier lane is "
            "wider and brighter. The Lanes row of the Overlay Menu ({OverlayMenu}) turns them off (the ships then "
            "show as points, as before); the rings and lines glow with the HUD glow setting.");

    // The Info Demo paragraph's far-ship clause of SDD 38 section 7 (since SDD 52 D6: far lane ships are not drawn)
    constexpr const TCHAR* INFOTEXT_DEMO_LANE_LINES =
        TEXT("farther ones follow simpler motion and are drawn as points, or not at all along a far lane, whose "
            "tunnel stands for them (see the overlays).");

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
    // The Info overlay paragraph's lane tunnel sentences of SDD 49 section 5 (issue #49 Part 49b; since SDD 52 the
    // flat lattice fixed to the lane, details only where the rings are far enough apart, labels on the old band, D7)
    constexpr const TCHAR* INFOTEXT_TUNNEL_SENTENCE =
        TEXT("Close to a lane (within about 100 lane radii) its ships show as points again and you can fly through "
            "its tunnel. The lane's rule is keep right: the green dots and chevrons on the right wall mark the way "
            "out from the inner body, the red ones on the left the way back; they show where the rings are far "
            "enough apart on screen. The rings stay fixed to the lane, and everything moves with the lane under time "
            "warp. A label on the wall of each tunnel within about 100 lane radii names the lane, the distance to "
            "each end and how many ships use it, or only the lane and the nearest end where space is short (the "
            "nearest tunnel's label is the brightest). Tunnels follow the Lanes row and the HUD glow and are not "
            "shown on the map.");
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

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextLaneReadoutTest, "SOLTest.InfoText.LaneTrafficReadout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #50 Part 50c (SDD 50 section 5, contract case 16), test-first: the overlay paragraph explains the lane traffic
// readout once, in one sentence after the tunnel sentences began and before the space dust: it names the traffic
// speed and the three colours (green matched, amber close, red far), with no placeholder left
bool FSOLInfoTextLaneReadoutTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> prose = SOLInfoText::BuildProse(SOLKeyBindings::DefaultKeys());
    int32 orbitCount = 0;
    const int32 overlay = InfoTextFindOrbitLinesParagraph(prose, orbitCount);
    if (!TestTrue(TEXT("the overlay paragraph exists"), overlay != INDEX_NONE))
    {
        return false;
    }
    const FString& paragraph = prose[overlay];
    const int32 tunnelAt = paragraph.Find(TEXT("Close to a lane (within about"), ESearchCase::CaseSensitive);
    const int32 dustAt = paragraph.Find(TEXT("Space dust"), ESearchCase::CaseSensitive);
    const int32 readoutAt = paragraph.Find(TEXT("readout"), ESearchCase::CaseSensitive, ESearchDir::FromStart,
        FMath::Max(tunnelAt, 0));
    if (!TestTrue(TEXT("a readout sentence after the tunnel sentences began"), tunnelAt != INDEX_NONE
        && readoutAt != INDEX_NONE))
    {
        return false;
    }
    TestTrue(TEXT("before the space dust"), dustAt != INDEX_NONE && readoutAt < dustAt);

    // The sentence that holds it
    const int32 previousStop = paragraph.Find(TEXT(". "), ESearchCase::CaseSensitive, ESearchDir::FromEnd, readoutAt);
    const int32 sentenceStart = previousStop == INDEX_NONE ? 0 : previousStop + 2;
    const int32 nextStop = paragraph.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, readoutAt);
    const FString sentence = paragraph.Mid(sentenceStart, (nextStop == INDEX_NONE ? paragraph.Len() : nextStop + 1)
        - sentenceStart);
    for (const TCHAR* phrase : { TEXT("traffic speed"), TEXT("green"), TEXT("amber"), TEXT(" red") })
    {
        TestTrue(FString::Printf(TEXT("'%s' says '%s'"), *sentence, phrase), sentence.Contains(phrase,
            ESearchCase::CaseSensitive));
    }
    TestEqual(TEXT("'readout' appears once between the tunnel text and the space dust"),
        paragraph.Mid(tunnelAt, dustAt - tunnelAt).Find(TEXT("readout"), ESearchCase::CaseSensitive,
            ESearchDir::FromEnd), readoutAt - tunnelAt);
    TestFalse(TEXT("no '{' left"), paragraph.Contains(TEXT("{")));
    return true;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the sentence of a paragraph that holds the text at index at (from the previous ". " to the next ".")
    FString InfoTextSentenceAt(const FString& paragraph, const int32 at)
    {
        const int32 previousStop = paragraph.Find(TEXT(". "), ESearchCase::CaseSensitive, ESearchDir::FromEnd, at);
        const int32 start = previousStop == INDEX_NONE ? 0 : previousStop + 2;
        const int32 nextStop = paragraph.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, at);
        return paragraph.Mid(start, (nextStop == INDEX_NONE ? paragraph.Len() : nextStop + 1) - start);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextAutopilotLanesTest, "SOLTest.InfoText.AutopilotLaneRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #50 Part 50d (SDD 50 D9, D19, contract case 16), test-first: the autopilot paragraph says AUTO follows the
// lane rules: it slows to the traffic speed, keeps to the right and the trip is slower; the 50d review fix: the lane
// trip takes over two weeks, 16 days Earth to Mars (the real lane's leg time, SOLTest.LaneAutoRules.LaneLegTime), not
// "about two weeks"; no placeholder left
bool FSOLInfoTextAutopilotLanesTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> prose = SOLInfoText::BuildProse(SOLKeyBindings::DefaultKeys());
    const TArray<int32> found = InfoTextParagraphsWith(prose, TEXT("The autopilot (AUTO)"));
    if (!TestEqual(TEXT("exactly one autopilot paragraph"), found.Num(), 1))
    {
        return false;
    }
    const FString& paragraph = prose[found[0]];
    for (const TCHAR* phrase : { TEXT("lane"), TEXT("traffic speed"), TEXT("right"), TEXT("slower"),
        TEXT("over two weeks"), TEXT("16 days") })
    {
        TestTrue(FString::Printf(TEXT("'%s' says '%s'"), *paragraph, phrase), paragraph.Contains(phrase,
            ESearchCase::CaseSensitive));
    }
    TestFalse(TEXT("no longer 'about two weeks'"), paragraph.Contains(TEXT("about two weeks"),
        ESearchCase::CaseSensitive));
    TestFalse(TEXT("no '{' left"), paragraph.Contains(TEXT("{")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInfoTextWarPowerTest, "SOLTest.InfoText.WarPowerMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #50 Part 50d (SDD 50 D22, contract case 16), test-first: the interaction menus paragraph names the War power
// move by its menu label, once, after Move To, in a sentence that says it ignores the lanes and flies at full thrust;
// no placeholder left. The 50d review fix (no exact literal is possible before the implementer words it): its own
// clause (from the label to the next ';' or '.') carries the keywords and stays at most 300 characters
bool FSOLInfoTextWarPowerTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> prose = SOLInfoText::BuildProse(SOLKeyBindings::DefaultKeys());
    const TArray<int32> found = InfoTextParagraphsWith(prose, TEXT("Interaction menus:"));
    if (!TestEqual(TEXT("exactly one interaction menus paragraph"), found.Num(), 1))
    {
        return false;
    }
    const FString& paragraph = prose[found[0]];
    const FString label = SOLActionTable::ActionLabel(ESOLAction::MoveToWarPower);
    if (!TestFalse(TEXT("the War power row has a label"), label.IsEmpty()))
    {
        return false;
    }
    const int32 labelAt = paragraph.Find(label, ESearchCase::CaseSensitive);
    const int32 moveToAt = paragraph.Find(TEXT("Move To"), ESearchCase::CaseSensitive);
    if (!TestTrue(FString::Printf(TEXT("'%s' names '%s'"), *paragraph, *label), labelAt != INDEX_NONE))
    {
        return false;
    }
    TestTrue(TEXT("after Move To"), moveToAt != INDEX_NONE && labelAt > moveToAt);
    TestEqual(TEXT("named once"), paragraph.Find(label, ESearchCase::CaseSensitive, ESearchDir::FromEnd), labelAt);
    const FString sentence = InfoTextSentenceAt(paragraph, labelAt);
    for (const TCHAR* phrase : { TEXT("lane"), TEXT("thrust") })
    {
        TestTrue(FString::Printf(TEXT("'%s' says '%s'"), *sentence, phrase), sentence.Contains(phrase,
            ESearchCase::CaseSensitive));
    }

    // The War power clause itself: from the label to the next ';' (the menu list's separator) or '.'
    const int32 semicolonAt = paragraph.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, labelAt);
    const int32 stopAt = paragraph.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, labelAt);
    const int32 endAt = semicolonAt == INDEX_NONE ? stopAt : stopAt == INDEX_NONE ? semicolonAt
        : FMath::Min(semicolonAt, stopAt);
    const FString clause = paragraph.Mid(labelAt, (endAt == INDEX_NONE ? paragraph.Len() : endAt) - labelAt);
    for (const TCHAR* phrase : { TEXT("lane"), TEXT("thrust") })
    {
        TestTrue(FString::Printf(TEXT("clause '%s' says '%s'"), *clause, phrase), clause.Contains(phrase,
            ESearchCase::CaseSensitive));
    }
    TestTrue(FString::Printf(TEXT("clause of %d characters, at most 300"), clause.Len()), clause.Len() <= 300);
    TestFalse(TEXT("no '{' left"), paragraph.Contains(TEXT("{")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
