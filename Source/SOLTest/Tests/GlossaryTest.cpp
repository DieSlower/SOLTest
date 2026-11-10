/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlightMode.h"
#include "Menu/SOLGlossary.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLPlayerGuide.h"
#include "SOLConstants.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLHudLines.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudSymbols.h"
#include "UI/SOLOverlayIcons.h"

#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Math/IntRect.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a Help tabs and HUD glossary (Docs/SDDs/33-player-documentation.md sections 5, 8 and
// 10, D6, D7, D22, D26), written against the header contract (Menu/SOLGlossary.h) and the SOLMenuText tables before
// the production code existed: the five Help tabs, the GLOSSARY_ENTRIES rows resolved, the description formatter and
// the completeness of the coverage table (every HUD line, icon category, card, steer icon and flight mode explained).
// Part 33d-i: every symbol a row names builds a sane picture at the glossary scale, no feature below the glossary
// floor (D42); the five map rows name no symbol (D39) and Distance darkening shows IconPlanet alone (D40).
// Part 33e-ii (D39), also test-first against its contract: the five map rows show a screenshot crop instead
// (SOL::Paths::GLOSSARY_CROPS: CropCount, FindCrop, EntryCrop, CropRect, CropAssetFile), Space dust is the one
// description-only row (SOLMenuText::GLOSSARY_DESCRIPTION_ONLY, DescriptionOnlyReason), every row has art or a reason,
// and SOLTest.Glossary.CropAssetsExist fails until the crop textures are captured and imported;
// SOLTest.Glossary.CropTexturesCurrent checks each texture was imported from the PNG in Docs/Images.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr int32 GLOSSARYTEST_TAB_COUNT = 5;                 // Keyboard, then the four glossary tabs
    constexpr int32 GLOSSARYTEST_GLOSSARY_TABS = 4;
    constexpr int32 GLOSSARYTEST_MIN_ENTRIES = 40;              // The approved ~45-row list (section 8.4, D26)
    constexpr int32 GLOSSARYTEST_STEER_ICONS = static_cast<int32>(ESOLSteerIcon::Count);   // Free, Held, Locked
    constexpr int32 GLOSSARYTEST_FLIGHT_MODES = static_cast<int32>(ESOLFlightMode::Count); // Manual, Assisted, Auto
    constexpr int32 GLOSSARYTEST_HUD_LINES = static_cast<int32>(ESOLHudLine::Count);
    constexpr int32 GLOSSARYTEST_ICON_CATEGORIES = static_cast<int32>(SOLOverlayIcons::EIconCategory::Count);
    constexpr int32 GLOSSARYTEST_HUD_CARDS = static_cast<int32>(ESOLHudCard::Count);
    constexpr int32 GLOSSARYTEST_SYMBOLS = static_cast<int32>(ESOLHudSymbol::Count);
    constexpr TCHAR GLOSSARYTEST_SUPERSCRIPT_TWO = TEXT('²');   // Rajdhani has no glyph for it (TECH_DEBT)

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of GLOSSARY_ENTRIES rows
    int32 GlossaryTestRowCount()
    {
        return static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_ENTRIES));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the '|' ids of a Symbols cell ("" gives none)
    TArray<FString> GlossaryTestSymbolIds(const TCHAR* symbols)
    {
        TArray<FString> ids;
        FString(symbols != nullptr ? symbols : TEXT("")).ParseIntoArray(ids, TEXT("|"));
        return ids;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns every '{<Token>}' name (letters and digits) in a text, in order
    TArray<FString> GlossaryTestTokens(const FString& text)
    {
        TArray<FString> tokens;
        int32 open = text.Find(TEXT("{"));
        while (open != INDEX_NONE)
        {
            const int32 close = text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, open + 1);
            if (close == INDEX_NONE)
            {
                break;
            }
            const FString name = text.Mid(open + 1, close - open - 1);
            bool bWord = !name.IsEmpty();
            for (const TCHAR character : name)
            {
                bWord &= FChar::IsAlnum(character);
            }
            if (bWord)
            {
                tokens.Add(name);
            }
            // FString::Find clamps a start at or past the end to the last character, so stop there
            open = open + 1 < text.Len()
                ? text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, open + 1) : INDEX_NONE;
        }
        return tokens;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether any entry's coverage names this HUD id (a scan of EntryCoverage)
    bool GlossaryTestScanCovered(const ESOLGlossaryCoverage kind, const int32 value)
    {
        for (int32 entry = 0; entry < SOLGlossary::EntryCount(); ++entry)
        {
            for (const FSOLGlossaryCoverageItem& item : SOLGlossary::EntryCoverage(entry))
            {
                if (item.Kind == kind && static_cast<int32>(item.Value) == value)
                {
                    return true;
                }
            }
        }
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryTabsTest, "SOLTest.Glossary.Tabs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Five Help tabs with short rail labels and full headings, the KEYS and HUD GLOSSARY captions, and the glossary tabs
// found by their full names (D22)
bool FSOLGlossaryTabsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("five tabs"), SOLGlossary::TabCount(), GLOSSARYTEST_TAB_COUNT);
    TestEqual(TEXT("the enum has five tabs"), static_cast<int32>(ESOLHelpTab::Count), GLOSSARYTEST_TAB_COUNT);
    TestEqual(TEXT("a label per tab"), static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::HELP_TAB_LABELS)),
        GLOSSARYTEST_TAB_COUNT);
    TestEqual(TEXT("a heading per tab"), static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::HELP_TAB_HEADINGS)),
        GLOSSARYTEST_TAB_COUNT);
    TestEqual(TEXT("four glossary tabs"), static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_TABS)),
        GLOSSARYTEST_GLOSSARY_TABS);
    for (int32 tab = 0; tab < GLOSSARYTEST_TAB_COUNT; ++tab)
    {
        const ESOLHelpTab value = static_cast<ESOLHelpTab>(tab);
        TestEqual(FString::Printf(TEXT("tab %d label"), tab), FString(SOLGlossary::TabLabel(value)),
            FString(SOLMenuText::HELP_TAB_LABELS[tab]));
        TestEqual(FString::Printf(TEXT("tab %d heading"), tab), FString(SOLGlossary::TabHeading(value)),
            FString(SOLMenuText::HELP_TAB_HEADINGS[tab]));
    }
    TestEqual(TEXT("KEYS above Keyboard"), FString(SOLGlossary::TabCaption(ESOLHelpTab::Keyboard)),
        FString(SOLMenuText::HELP_CAPTION_KEYS));
    TestEqual(TEXT("HUD GLOSSARY above Flight HUD"), FString(SOLGlossary::TabCaption(ESOLHelpTab::FlightHud)),
        FString(SOLMenuText::HELP_CAPTION_GLOSSARY));
    const ESOLHelpTab uncaptioned[] = { ESOLHelpTab::TargetsRadar, ESOLHelpTab::OverlaysRoute, ESOLHelpTab::MenusMap,
        ESOLHelpTab::Count };
    for (const ESOLHelpTab tab : uncaptioned)
    {
        TestEqual(FString::Printf(TEXT("tab %d has no caption"), static_cast<int32>(tab)),
            FString(SOLGlossary::TabCaption(tab)), FString());
    }
    TestEqual(TEXT("invalid tab label"), FString(SOLGlossary::TabLabel(ESOLHelpTab::Count)), FString());
    TestEqual(TEXT("invalid tab heading"), FString(SOLGlossary::TabHeading(ESOLHelpTab::Count)), FString());

    // The glossary tabs by their full names (the headings of tabs 1-4)
    for (int32 tab = 0; tab < GLOSSARYTEST_GLOSSARY_TABS; ++tab)
    {
        const ESOLHelpTab expected = static_cast<ESOLHelpTab>(tab + 1);
        TestTrue(FString::Printf(TEXT("'%s' is glossary tab %d"), SOLMenuText::GLOSSARY_TABS[tab], tab + 1),
            SOLGlossary::TabFromGlossaryName(SOLMenuText::GLOSSARY_TABS[tab]) == expected);
        TestEqual(FString::Printf(TEXT("'%s' is its tab's heading"), SOLMenuText::GLOSSARY_TABS[tab]),
            FString(SOLMenuText::GLOSSARY_TABS[tab]), FString(SOLMenuText::HELP_TAB_HEADINGS[tab + 1]));
    }
    TestTrue(TEXT("Keyboard is no glossary tab"), SOLGlossary::TabFromGlossaryName(TEXT("Keyboard"))
        == ESOLHelpTab::Count);
    TestTrue(TEXT("names are case-sensitive"), SOLGlossary::TabFromGlossaryName(TEXT("flight hud"))
        == ESOLHelpTab::Count);
    TestTrue(TEXT("unknown tab"), SOLGlossary::TabFromGlossaryName(TEXT("Nowhere")) == ESOLHelpTab::Count);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryEntriesTest, "SOLTest.Glossary.EntriesResolve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The approved list's rows (at least 40) each sit on one of the four glossary tabs, with every symbol id resolved
bool FSOLGlossaryEntriesTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("EntryCount is the table's"), SOLGlossary::EntryCount(), GlossaryTestRowCount());
    TestTrue(FString::Printf(TEXT("the approved ~45-row list (%d rows)"), GlossaryTestRowCount()),
        GlossaryTestRowCount() >= GLOSSARYTEST_MIN_ENTRIES);

    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        const FString what = FString::Printf(TEXT("entry %d '%s'"), index, row.Name);
        TestFalse(what + TEXT(": has a name"), FString(row.Name).IsEmpty());
        TestFalse(what + TEXT(": has a description"), FString(row.Description).IsEmpty());

        FSOLGlossaryEntry entry;
        TestTrue(what + TEXT(": resolves"), SOLGlossary::ResolveEntry(index, entry));
        TestEqual(what + TEXT(": index"), entry.Index, index);
        TestTrue(what + TEXT(": on a glossary tab"), entry.Tab != ESOLHelpTab::Count
            && entry.Tab != ESOLHelpTab::Keyboard);
        TestTrue(what + TEXT(": its tab from the table"), entry.Tab == SOLGlossary::TabFromGlossaryName(row.Tab));
        TestTrue(what + TEXT(": every symbol resolved"), entry.bSymbolsResolved);

        // Each id resolves to the symbol in its slot
        const TArray<FString> ids = GlossaryTestSymbolIds(row.Symbols);
        TestTrue(what + TEXT(": fits the symbol slots"), ids.Num() <= SOL::Help::GLOSSARY_MAX_SYMBOLS_PER_ENTRY);
        TestEqual(what + TEXT(": one symbol per id"), entry.SymbolCount, ids.Num());
        for (int32 slot = 0; slot < FMath::Min(ids.Num(), SOL::Help::GLOSSARY_MAX_SYMBOLS_PER_ENTRY); ++slot)
        {
            const TOptional<ESOLHudSymbol> symbol = SOLGlossary::ResolveSymbol(ids[slot]);
            TestTrue(FString::Printf(TEXT("%s: symbol id '%s' resolves"), *what, *ids[slot]), symbol.IsSet());
            if (symbol.IsSet())
            {
                TestTrue(FString::Printf(TEXT("%s: slot %d holds '%s'"), *what, slot, *ids[slot]),
                    entry.Symbols[slot] == symbol.GetValue());
            }
        }
    }

    // Outside the table
    FSOLGlossaryEntry outside;
    TestFalse(TEXT("index -1"), SOLGlossary::ResolveEntry(-1, outside));
    TestFalse(TEXT("index past the end"), SOLGlossary::ResolveEntry(GlossaryTestRowCount(), outside));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossarySymbolIdsTest, "SOLTest.Glossary.SymbolIds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ResolveSymbol reads every symbol's own id and nothing else
bool FSOLGlossarySymbolIdsTest::RunTest(const FString& /*parameters*/)
{
    for (int32 symbol = 0; symbol < GLOSSARYTEST_SYMBOLS; ++symbol)
    {
        const ESOLHudSymbol value = static_cast<ESOLHudSymbol>(symbol);
        const FString id = SOLHudSymbols::ToName(value);
        const TOptional<ESOLHudSymbol> resolved = SOLGlossary::ResolveSymbol(id);
        TestTrue(FString::Printf(TEXT("symbol %d '%s' resolves"), symbol, *id),
            resolved.IsSet() && resolved.GetValue() == value);
    }
    TestFalse(TEXT("unknown id"), SOLGlossary::ResolveSymbol(TEXT("NoSuchSymbol")).IsSet());
    TestFalse(TEXT("empty id"), SOLGlossary::ResolveSymbol(FStringView()).IsSet());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryNamesTest, "SOLTest.Glossary.UniqueNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No two entries share a name
bool FSOLGlossaryNamesTest::RunTest(const FString& /*parameters*/)
{
    for (int32 a = 0; a < GlossaryTestRowCount(); ++a)
    {
        for (int32 b = a + 1; b < GlossaryTestRowCount(); ++b)
        {
            TestFalse(FString::Printf(TEXT("entries %d and %d share the name '%s'"), a, b,
                SOLMenuText::GLOSSARY_ENTRIES[a].Name), FString(SOLMenuText::GLOSSARY_ENTRIES[a].Name).Equals(
                SOLMenuText::GLOSSARY_ENTRIES[b].Name, ESearchCase::IgnoreCase));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryTabEntriesTest, "SOLTest.Glossary.EntriesOfTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each glossary tab lists its rows in table order; together they hold every row once; Keyboard and Count list none
bool FSOLGlossaryTabEntriesTest::RunTest(const FString& /*parameters*/)
{
    TArray<int32> seen;
    seen.Init(0, GlossaryTestRowCount());
    TArray<int32> indices;
    for (int32 tab = 1; tab < GLOSSARYTEST_TAB_COUNT; ++tab)
    {
        const ESOLHelpTab value = static_cast<ESOLHelpTab>(tab);
        SOLGlossary::EntriesOfTab(value, indices);
        TestTrue(FString::Printf(TEXT("tab %d has entries"), tab), indices.Num() > 0);
        for (int32 position = 0; position < indices.Num(); ++position)
        {
            const int32 index = indices[position];
            TestTrue(FString::Printf(TEXT("tab %d: row %d in the table"), tab, index), seen.IsValidIndex(index));
            if (!seen.IsValidIndex(index))
            {
                continue;
            }
            ++seen[index];
            TestTrue(FString::Printf(TEXT("tab %d: row %d is on it"), tab, index),
                SOLGlossary::TabFromGlossaryName(SOLMenuText::GLOSSARY_ENTRIES[index].Tab) == value);
            if (position > 0)
            {
                TestTrue(FString::Printf(TEXT("tab %d: table order at %d"), tab, position),
                    indices[position - 1] < index);
            }
        }
    }
    for (int32 index = 0; index < seen.Num(); ++index)
    {
        TestEqual(FString::Printf(TEXT("row %d on exactly one tab"), index), seen[index], 1);
    }

    indices.Add(7);
    SOLGlossary::EntriesOfTab(ESOLHelpTab::Keyboard, indices);
    TestEqual(TEXT("Keyboard lists no entries"), indices.Num(), 0);
    indices.Add(7);
    SOLGlossary::EntriesOfTab(ESOLHelpTab::Count, indices);
    TestEqual(TEXT("an invalid tab lists no entries"), indices.Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryFormatTest, "SOLTest.Glossary.FormatEntryText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every description formats with no '{' left at the default keys; '{<BindingId>}' is the live key, an unknown token
// stays as written
bool FSOLGlossaryFormatTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FString out;
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        SOLGlossary::FormatEntryText(row.Description, keys, out);
        TestFalse(FString::Printf(TEXT("entry %d '%s': no token left in '%s'"), index, row.Name, *out),
            out.Contains(TEXT("{")));
        TestFalse(FString::Printf(TEXT("entry %d '%s': formats to text"), index, row.Name), out.IsEmpty());
    }

    // A binding token is the live key, before and after a rebind
    const int32 fire = static_cast<int32>(SOLKeyBindings::EBinding::Fire);
    SOLGlossary::FormatEntryText(TEXT("Press {Fire} to fire."), keys, out);
    TestEqual(TEXT("{Fire} at the defaults"), out,
        FString::Printf(TEXT("Press %s to fire."), *SOLKeyBindings::BindingDisplayName(fire, keys)));
    keys[fire] = TEXT("K");
    SOLGlossary::FormatEntryText(TEXT("Press {Fire} to fire."), keys, out);
    TestEqual(TEXT("{Fire} rebound"), out, FString(TEXT("Press K to fire.")));
    SOLGlossary::FormatEntryText(TEXT("{OpenMap}"), SOLKeyBindings::DefaultKeys(), out);
    TestEqual(TEXT("{OpenMap} is the chord"), out, FString(TEXT("Shift+M")));

    // An unknown token is left as written; plain text is unchanged; out is overwritten
    SOLGlossary::FormatEntryText(TEXT("a {NoSuchToken} b"), keys, out);
    TestEqual(TEXT("unknown token left"), out, FString(TEXT("a {NoSuchToken} b")));
    SOLGlossary::FormatEntryText(TEXT("plain"), keys, out);
    TestEqual(TEXT("plain text, out overwritten"), out, FString(TEXT("plain")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryTokensTest, "SOLTest.Glossary.DescriptionTokens",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every '{<Token>}' a description names is a binding id that exists (shown as its key) or a constant token the
// formatter knows
bool FSOLGlossaryTokensTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FString out;
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        for (const FString& token : GlossaryTestTokens(row.Description))
        {
            const FString what = FString::Printf(TEXT("entry %d '%s': {%s}"), index, row.Name, *token);
            const FString wrapped = FString::Printf(TEXT("{%s}"), *token);
            SOLGlossary::FormatEntryText(*wrapped, keys, out);
            const int32 binding = SOLKeyBindings::FindById(token);
            if (binding != INDEX_NONE)
            {
                TestEqual(what + TEXT(": the binding's key"), out, SOLKeyBindings::BindingDisplayName(binding, keys));
            }
            else
            {
                TestTrue(what + TEXT(": a binding id that exists or a known constant token"), out != wrapped);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossarySuperscriptTest, "SOLTest.Glossary.NoSuperscriptTwo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No glossary text uses the superscript two, raw or formatted (the menu font has no glyph for it)
bool FSOLGlossarySuperscriptTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString superscript = FString::Chr(GLOSSARYTEST_SUPERSCRIPT_TWO);
    FString out;
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        const FString what = FString::Printf(TEXT("entry %d '%s'"), index, row.Name);
        TestFalse(what + TEXT(": name"), FString(row.Name).Contains(superscript));
        TestFalse(what + TEXT(": description"), FString(row.Description).Contains(superscript));
        TestFalse(what + TEXT(": sample"), FString(row.Sample).Contains(superscript));
        SOLGlossary::FormatEntryText(row.Description, keys, out);
        TestFalse(what + TEXT(": formatted description"), out.Contains(superscript));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryHudLinesTest, "SOLTest.Glossary.CoversHudLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every HUD text line (except Count) is explained by at least one entry (D6)
bool FSOLGlossaryHudLinesTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("value count"), SOLGlossary::CoverageValueCount(ESOLGlossaryCoverage::HudLine),
        GLOSSARYTEST_HUD_LINES);
    for (int32 line = 0; line < GLOSSARYTEST_HUD_LINES; ++line)
    {
        TestTrue(FString::Printf(TEXT("HUD line %d covered"), line), SOLGlossary::IsCovered(
            static_cast<ESOLHudLine>(line)));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryIconsTest, "SOLTest.Glossary.CoversIconCategories",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every overlay icon category is explained by at least one entry (D6)
bool FSOLGlossaryIconsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("value count"), SOLGlossary::CoverageValueCount(ESOLGlossaryCoverage::IconCategory),
        GLOSSARYTEST_ICON_CATEGORIES);
    for (int32 category = 0; category < GLOSSARYTEST_ICON_CATEGORIES; ++category)
    {
        TestTrue(FString::Printf(TEXT("icon category %d covered"), category), SOLGlossary::IsCovered(
            static_cast<SOLOverlayIcons::EIconCategory>(category)));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryCardsTest, "SOLTest.Glossary.CoversHudCards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every hologram card of the flight HUD is explained by at least one entry
bool FSOLGlossaryCardsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("value count"), SOLGlossary::CoverageValueCount(ESOLGlossaryCoverage::HudCard),
        GLOSSARYTEST_HUD_CARDS);
    for (int32 card = 0; card < GLOSSARYTEST_HUD_CARDS; ++card)
    {
        TestTrue(FString::Printf(TEXT("HUD card %d covered"), card),
            SOLGlossary::IsCovered(ESOLGlossaryCoverage::HudCard, card));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossarySteerTest, "SOLTest.Glossary.CoversSteerIcons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every mouse-steer icon is explained by at least one entry
bool FSOLGlossarySteerTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("value count"), SOLGlossary::CoverageValueCount(ESOLGlossaryCoverage::SteerIcon),
        GLOSSARYTEST_STEER_ICONS);
    for (int32 icon = 0; icon < GLOSSARYTEST_STEER_ICONS; ++icon)
    {
        TestTrue(FString::Printf(TEXT("steer icon %d covered"), icon),
            SOLGlossary::IsCovered(static_cast<ESOLSteerIcon>(icon)));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryModesTest, "SOLTest.Glossary.CoversFlightModes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every flight mode (MANUAL, ASSISTED, AUTO) is explained by at least one entry
bool FSOLGlossaryModesTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("value count"), SOLGlossary::CoverageValueCount(ESOLGlossaryCoverage::FlightMode),
        GLOSSARYTEST_FLIGHT_MODES);
    for (int32 mode = 0; mode < GLOSSARYTEST_FLIGHT_MODES; ++mode)
    {
        TestTrue(FString::Printf(TEXT("flight mode %d covered"), mode),
            SOLGlossary::IsCovered(static_cast<ESOLFlightMode>(mode)));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryCoverageTest, "SOLTest.Glossary.CoverageTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every coverage item names a real HUD id, IsCovered agrees with a scan of the entries, and out-of-range asks are
// uncovered
bool FSOLGlossaryCoverageTest::RunTest(const FString& /*parameters*/)
{
    int32 items = 0;
    for (int32 entry = 0; entry < SOLGlossary::EntryCount(); ++entry)
    {
        for (const FSOLGlossaryCoverageItem& item : SOLGlossary::EntryCoverage(entry))
        {
            ++items;
            TestTrue(FString::Printf(TEXT("entry %d: a real kind"), entry), item.Kind != ESOLGlossaryCoverage::Count);
            TestTrue(FString::Printf(TEXT("entry %d: value %d in range"), entry, item.Value),
                static_cast<int32>(item.Value) < SOLGlossary::CoverageValueCount(item.Kind));
        }
    }
    TestTrue(TEXT("the table explains HUD ids"), items > 0);
    TestEqual(TEXT("no coverage before the table"), SOLGlossary::EntryCoverage(-1).Num(), 0);
    TestEqual(TEXT("no coverage past the table"), SOLGlossary::EntryCoverage(SOLGlossary::EntryCount()).Num(), 0);
    TestEqual(TEXT("Count has no values"), SOLGlossary::CoverageValueCount(ESOLGlossaryCoverage::Count), 0);

    // IsCovered is a scan of EntryCoverage; values at or past the count are never covered
    const ESOLGlossaryCoverage kinds[] = { ESOLGlossaryCoverage::HudLine, ESOLGlossaryCoverage::IconCategory,
        ESOLGlossaryCoverage::HudCard, ESOLGlossaryCoverage::SteerIcon, ESOLGlossaryCoverage::FlightMode };
    for (const ESOLGlossaryCoverage kind : kinds)
    {
        const int32 count = SOLGlossary::CoverageValueCount(kind);
        TestTrue(FString::Printf(TEXT("kind %d has values"), static_cast<int32>(kind)), count > 0);
        for (int32 value = 0; value < count; ++value)
        {
            TestTrue(FString::Printf(TEXT("kind %d value %d agrees with the entries"), static_cast<int32>(kind),
                value), SOLGlossary::IsCovered(kind, value) == GlossaryTestScanCovered(kind, value));
        }
        TestFalse(FString::Printf(TEXT("kind %d at its count"), static_cast<int32>(kind)),
            SOLGlossary::IsCovered(kind, count));
        TestFalse(FString::Printf(TEXT("kind %d negative"), static_cast<int32>(kind)),
            SOLGlossary::IsCovered(kind, -1));
    }
    TestFalse(TEXT("Count kind"), SOLGlossary::IsCovered(ESOLGlossaryCoverage::Count, 0));
    return true;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the GLOSSARY_ENTRIES row with a name, or INDEX_NONE
    int32 GlossaryTestEntryNamed(const TCHAR* name)
    {
        for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
        {
            if (FCString::Strcmp(SOLMenuText::GLOSSARY_ENTRIES[index].Name, name) == 0)
            {
                return index;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a named row's formatted Sample split on '|' (none for an unknown row)
    TArray<FString> GlossaryTestSamplePieces(const TCHAR* name, const TArray<FString>& keys)
    {
        TArray<FString> pieces;
        const int32 index = GlossaryTestEntryNamed(name);
        if (index != INDEX_NONE)
        {
            FString out;
            SOLGlossary::FormatEntryText(SOLMenuText::GLOSSARY_ENTRIES[index].Sample, keys, out);
            out.ParseIntoArray(pieces, TEXT("|"));
        }
        return pieces;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the chase marker's activity line the HUD writes for an activity and G (no resume shown)
    FString GlossaryTestChaseLine(const ESOLChaseActivity activity, const double limitG)
    {
        FString line;
        SOLHudFormat::AppendChaseActivity(line, activity, limitG, 2.0);
        return line;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossarySamplesTest, "SOLTest.Glossary.SamplesFromHud",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every sample formats with no token left and no superscript two, and the text samples are the HUD's own texts: the
// steer hint, the flight mode labels, the Chase Down lines, the overlay profile tag and the profile count
bool FSOLGlossarySamplesTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString superscript = FString::Chr(GLOSSARYTEST_SUPERSCRIPT_TWO);
    FString out;
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        SOLGlossary::FormatEntryText(row.Sample, keys, out);
        const FString what = FString::Printf(TEXT("entry %d '%s': sample '%s'"), index, row.Name, *out);
        TestFalse(what + TEXT(": no token left"), out.Contains(TEXT("{")));
        TestFalse(what + TEXT(": no superscript two"), out.Contains(superscript));
        TestEqual(what + TEXT(": empty only when the cell is"), out.IsEmpty(), FString(row.Sample).IsEmpty());
    }

    // The steer hint is the HUD's, at the defaults and after a rebind
    const int32 steerHold = static_cast<int32>(SOLKeyBindings::EBinding::SteerHold);
    for (int32 pass = 0; pass < 2; ++pass)
    {
        if (pass == 1)
        {
            keys[steerHold] = TEXT("K");
        }
        const TArray<FString> hint = GlossaryTestSamplePieces(TEXT("Key hints and notices"), keys);
        if (TestEqual(FString::Printf(TEXT("pass %d: one steer hint sample"), pass), hint.Num(), 1))
        {
            TestEqual(FString::Printf(TEXT("pass %d: the HUD's steer hint"), pass), hint[0],
                SOLHudFormat::FormatHudKeyTexts(keys).SteerHint);
        }
    }
    keys = SOLKeyBindings::DefaultKeys();

    // One flight mode label per mode, in ESOLFlightMode order
    const TArray<FString> modes = GlossaryTestSamplePieces(TEXT("Flight mode"), keys);
    if (TestEqual(TEXT("a label per flight mode"), modes.Num(), GLOSSARYTEST_FLIGHT_MODES))
    {
        for (int32 mode = 0; mode < GLOSSARYTEST_FLIGHT_MODES; ++mode)
        {
            TestEqual(FString::Printf(TEXT("flight mode %d label"), mode), modes[mode],
                FString(SOLHudFormat::FlightModeLabel(static_cast<ESOLFlightMode>(mode))));
        }
    }

    // The Chase Down lines as the HUD writes them: the burst and brake at the sample G, the cool at the sustained G
    const TArray<FString> chase = GlossaryTestSamplePieces(TEXT("Chase Down line"), keys);
    if (TestEqual(TEXT("three Chase Down lines"), chase.Num(), 3))
    {
        TestEqual(TEXT("burst line"), chase[0],
            GlossaryTestChaseLine(ESOLChaseActivity::Burst, SOL::Help::GLOSSARY_SAMPLE_CHASE_BURST_G));
        TestEqual(TEXT("cool line at the sustained G"), chase[1],
            GlossaryTestChaseLine(ESOLChaseActivity::Cool, SOL::PILOT_SUSTAINED_G));
        TestEqual(TEXT("brake line"), chase[2],
            GlossaryTestChaseLine(ESOLChaseActivity::BrakeBurst, SOL::Help::GLOSSARY_SAMPLE_CHASE_BURST_G));
    }

    // The overlay profile tag: the HUD's format with its number token as the sample's profile number
    const TArray<FString> tag = GlossaryTestSamplePieces(TEXT("Overlay profile tag"), keys);
    const FString format(SOLMenuText::HUD_OVERLAY_PROFILE_TAG_FORMAT);
    const int32 token = format.Find(SOLMenuText::OVERLAY_PROFILE_NUMBER_TOKEN, ESearchCase::CaseSensitive);
    if (TestTrue(TEXT("the tag format has its number token"), token != INDEX_NONE)
        && TestEqual(TEXT("one profile tag sample"), tag.Num(), 1))
    {
        const FString expected = format.Left(token) + FString::FromInt(SOL::Help::GLOSSARY_SAMPLE_OVERLAY_PROFILE)
            + format.Mid(token + FCString::Strlen(SOLMenuText::OVERLAY_PROFILE_NUMBER_TOKEN));
        TestEqual(TEXT("the HUD's profile tag"), tag[0], expected);
    }
    TestTrue(TEXT("the sample profile exists"), SOL::Help::GLOSSARY_SAMPLE_OVERLAY_PROFILE >= 1
        && SOL::Help::GLOSSARY_SAMPLE_OVERLAY_PROFILE <= SOL::Overlay::MAX_PROFILES);

    // The profile count in the prose is MAX_PROFILES
    const int32 tagRow = GlossaryTestEntryNamed(TEXT("Overlay profile tag"));
    if (TestTrue(TEXT("the profile tag row exists"), tagRow != INDEX_NONE))
    {
        SOLGlossary::FormatEntryText(SOLMenuText::GLOSSARY_ENTRIES[tagRow].Description, keys, out);
        TestTrue(FString::Printf(TEXT("'%s' names MAX_PROFILES profiles"), *out),
            out.Contains(FString::Printf(TEXT("%d overlay profiles"), SOL::Overlay::MAX_PROFILES)));
    }
    return true;
}

namespace
{
    constexpr float GLOSSARYTEST_TOLERANCE = 1.0e-3f;           // Pixel comparisons
    constexpr float GLOSSARYTEST_PAD_PX = 2.0f;                 // The glow pad of the replays
    constexpr float GLOSSARYTEST_BUILD_SCALES[] = { 1.0f, 1.5f };   // UI 1 (720 px) and the 1080 px HUD scale
    constexpr float GLOSSARYTEST_FLOOR_SCALES[] = { 0.05f, 0.25f, 1.0f, 1.5f };  // Down to a tiny shrink-to-fit

    // The five map rows (D39): no symbol and no sample, a screenshot crop instead (33e-ii), and the crop id each names
    struct FGlossaryTestCropRow
    {
        const TCHAR* Name;
        const TCHAR* Crop;
    };
    const FGlossaryTestCropRow GLOSSARYTEST_CROP_ROWS[] = { { TEXT("Orbit lines"), TEXT("OrbitLines") },
        { TEXT("Route legs"), TEXT("RouteLegs") }, { TEXT("Waypoints"), TEXT("Waypoints") },
        { TEXT("Map destination pick"), TEXT("MapPick") }, { TEXT("Map grid and stems"), TEXT("MapGrid") } };

    // The symbols those rows would have shown (ids kept in ESOLHudSymbol, no builder required)
    constexpr ESOLHudSymbol GLOSSARYTEST_CROP_ROW_SYMBOLS[] = { ESOLHudSymbol::OrbitLine, ESOLHudSymbol::RouteLeg,
        ESOLHudSymbol::RouteLoopLeg, ESOLHudSymbol::WaypointAbove, ESOLHudSymbol::WaypointBelow,
        ESOLHudSymbol::MapPick, ESOLHudSymbol::MapGridStem };

    //////////////////////////////////////////////////////////////////////////
    // Returns the box a primitive's own points and sizes span (see ESOLHudPrimKind; glow pad and line width excluded)
    FBox2f GlossaryTestPrimExtent(const FSOLHudSymbolPrim& prim)
    {
        const FVector2f radius(prim.RadiusPx, prim.RadiusPx);
        switch (prim.Kind)
        {
        case ESOLHudPrimKind::Line:
        case ESOLHudPrimKind::DashedLine:
        case ESOLHudPrimKind::Card:
        case ESOLHudPrimKind::Frame:
            return FBox2f(FVector2f::Min(prim.A, prim.B), FVector2f::Max(prim.A, prim.B));
        case ESOLHudPrimKind::Ring:
        case ESOLHudPrimKind::Arc:
        case ESOLHudPrimKind::Dot:
        case ESOLHudPrimKind::Icon:
            return FBox2f(prim.A - radius, prim.A + radius);
        case ESOLHudPrimKind::Ellipse:
            return FBox2f(prim.A - FVector2f(prim.RadiusPx, prim.RadiusYPx),
                prim.A + FVector2f(prim.RadiusPx, prim.RadiusYPx));
        case ESOLHudPrimKind::Box:
            return FBox2f(FVector2f::Min(prim.A, prim.A + prim.B), FVector2f::Max(prim.A, prim.A + prim.B));
        case ESOLHudPrimKind::Panel:
            return FBox2f(prim.A - prim.B, prim.A + prim.B);
        case ESOLHudPrimKind::EdgeArrow:
        {
            const FVector2f tip = prim.A + prim.B * prim.RadiusPx;
            return FBox2f(FVector2f::Min(prim.A, tip), FVector2f::Max(prim.A, tip));
        }
        default:
            return FBox2f(prim.A, prim.A);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns why a primitive breaks the glossary floor (D42), "" when it keeps it: Dot / Ring / Arc / Icon /
    // EdgeArrow radius (length), both Ellipse radii, a Box's size, a Panel's half size (floor / 2), a dashed line's
    // dash and gap; lines (their half widths too), text, cards and frames are exempt
    FString GlossaryTestFloorBreach(const FSOLHudSymbolPrim& prim)
    {
        const float floorPx = SOL::Help::GLOSSARY_MIN_FEATURE_PX - GLOSSARYTEST_TOLERANCE;
        switch (prim.Kind)
        {
        case ESOLHudPrimKind::Dot:
        case ESOLHudPrimKind::Ring:
        case ESOLHudPrimKind::Arc:
        case ESOLHudPrimKind::Icon:
        case ESOLHudPrimKind::EdgeArrow:
            return prim.RadiusPx >= floorPx ? FString() : FString::Printf(TEXT("radius %.3f"), prim.RadiusPx);
        case ESOLHudPrimKind::Ellipse:
            return prim.RadiusPx >= floorPx && prim.RadiusYPx >= floorPx ? FString()
                : FString::Printf(TEXT("radii %.3f x %.3f"), prim.RadiusPx, prim.RadiusYPx);
        case ESOLHudPrimKind::Box:
            return prim.B.X >= floorPx && prim.B.Y >= floorPx ? FString()
                : FString::Printf(TEXT("box %.3f x %.3f"), prim.B.X, prim.B.Y);
        case ESOLHudPrimKind::Panel:
            return prim.B.X >= 0.5f * floorPx && prim.B.Y >= 0.5f * floorPx ? FString()
                : FString::Printf(TEXT("panel half size %.3f x %.3f"), prim.B.X, prim.B.Y);
        case ESOLHudPrimKind::DashedLine:
            return prim.DashPx >= floorPx && prim.GapPx >= floorPx ? FString()
                : FString::Printf(TEXT("dash %.3f gap %.3f"), prim.DashPx, prim.GapPx);
        default:
            return FString();
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two primitives are the same in every field (points and sizes within the pixel tolerance)
    bool GlossaryTestSamePrim(const FSOLHudSymbolPrim& a, const FSOLHudSymbolPrim& b)
    {
        const float tolerance = GLOSSARYTEST_TOLERANCE;
        const float fine = 1.0e-6f;
        return a.Kind == b.Kind && a.Role == b.Role && a.Path == b.Path && a.Category == b.Category
            && a.Segments == b.Segments && a.A.Equals(b.A, tolerance) && a.B.Equals(b.B, tolerance)
            && FMath::IsNearlyEqual(a.RadiusPx, b.RadiusPx, tolerance)
            && FMath::IsNearlyEqual(a.RadiusYPx, b.RadiusYPx, tolerance)
            && FMath::IsNearlyEqual(a.HalfWidthPx, b.HalfWidthPx, tolerance)
            && FMath::IsNearlyEqual(a.MidAngleRad, b.MidAngleRad, fine)
            && FMath::IsNearlyEqual(a.HalfApertureRad, b.HalfApertureRad, fine)
            && FMath::IsNearlyEqual(a.DashPx, b.DashPx, tolerance) && FMath::IsNearlyEqual(a.GapPx, b.GapPx, tolerance)
            && FMath::IsNearlyEqual(a.Fill, b.Fill, fine) && FMath::IsNearlyEqual(a.Intensity, b.Intensity, fine)
            && FMath::IsNearlyEqual(a.UiScale, b.UiScale, fine)
            && FMath::IsNearlyEqual(a.ThicknessPx, b.ThicknessPx, tolerance)
            && FMath::IsNearlyEqual(a.SquashY, b.SquashY, fine) && FMath::IsNearlyEqual(a.PadPx, b.PadPx, tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns every symbol the GLOSSARY_ENTRIES rows name, once each, in first-named order (ids that do not resolve
    // are left out; EntriesResolve reports them)
    TArray<ESOLHudSymbol> GlossaryTestNamedSymbols()
    {
        TArray<ESOLHudSymbol> symbols;
        for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
        {
            for (const FString& id : GlossaryTestSymbolIds(SOLMenuText::GLOSSARY_ENTRIES[index].Symbols))
            {
                ESOLHudSymbol symbol = ESOLHudSymbol::Count;
                if (SOLHudSymbols::FromName(id, symbol))
                {
                    symbols.AddUnique(symbol);
                }
            }
        }
        return symbols;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryEverySymbolBuildsTest, "SOLTest.Glossary.EverySymbolBuilds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every symbol a row names resolves and builds at the glossary's zero centre at UI scales 1 and 1.5: at least one
// primitive, none dropped; finite non-empty bounds that hold the origin and every primitive and stay within the
// HUD's own height at that scale; a replay into a glossary view's batch that adds shapes and drops none
bool FSOLGlossaryEverySymbolBuildsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    FSOLHudSymbolPrims prims;
    FSOLHudSymbolPrims scratch;
    int32 checked = 0;
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        for (const FString& id : GlossaryTestSymbolIds(row.Symbols))
        {
            ESOLHudSymbol symbol = ESOLHudSymbol::Count;
            if (!TestTrue(FString::Printf(TEXT("entry %d '%s': '%s' is a symbol"), index, row.Name, *id),
                SOLHudSymbols::FromName(id, symbol)))
            {
                continue;
            }
            for (const float scale : GLOSSARYTEST_BUILD_SCALES)
            {
                ++checked;
                const FString what = FString::Printf(TEXT("entry %d '%s': %s at %.2f"), index, row.Name, *id, scale);
                const int32 count = SOLHudSymbols::Build(symbol, FVector2f::ZeroVector, scale, prims);
                TestTrue(what + TEXT(": builds"), count >= 1);
                TestTrue(what + TEXT(": at most the capacity"), count <= SOL::Help::MAX_PRIMS_PER_SYMBOL);
                TestEqual(what + TEXT(": nothing dropped"), prims.DroppedCount, 0);

                // Sane bounds around the centre
                const FBox2f bounds = SOLHudSymbols::Bounds(symbol, scale, scratch);
                const bool bFinite = bounds.bIsValid != 0 && FMath::IsFinite(bounds.Min.X)
                    && FMath::IsFinite(bounds.Min.Y) && FMath::IsFinite(bounds.Max.X) && FMath::IsFinite(bounds.Max.Y);
                TestTrue(what + TEXT(": finite bounds"), bFinite);
                if (!bFinite)
                {
                    continue;
                }
                TestTrue(what + TEXT(": bounds have an area"), bounds.GetSize().X > 0.0f && bounds.GetSize().Y > 0.0f);
                TestTrue(what + TEXT(": bounds hold the centre"), bounds.Min.X <= 0.0f && bounds.Min.Y <= 0.0f
                    && bounds.Max.X >= 0.0f && bounds.Max.Y >= 0.0f);
                const float limit = SOL::Hologram::HUD_REFERENCE_HEIGHT_PX * scale;
                TestTrue(FString::Printf(TEXT("%s: within the HUD's height (%.1f .. %.1f, %.1f .. %.1f; %.1f)"), *what,
                    bounds.Min.X, bounds.Max.X, bounds.Min.Y, bounds.Max.Y, limit), -bounds.Min.X <= limit
                    && bounds.Max.X <= limit && -bounds.Min.Y <= limit && bounds.Max.Y <= limit);
                const float tolerance = GLOSSARYTEST_TOLERANCE * 10.0f * FMath::Max(1.0f, scale);
                for (int32 prim = 0; prim < count; ++prim)
                {
                    const FBox2f extent = GlossaryTestPrimExtent(prims.Prims[prim]);
                    TestTrue(FString::Printf(TEXT("%s: prim %d inside the bounds"), *what, prim),
                        extent.Min.X >= bounds.Min.X - tolerance && extent.Min.Y >= bounds.Min.Y - tolerance
                        && extent.Max.X <= bounds.Max.X + tolerance && extent.Max.Y <= bounds.Max.Y + tolerance);
                }

                // The glossary view's batch takes it whole
                batch.Reset();
                const int32 added = SOLHudSymbols::AddToBatch(prims, batch, palette, GLOSSARYTEST_PAD_PX);
                TestTrue(what + TEXT(": adds shapes"), added > 0);
                TestEqual(what + TEXT(": no shape dropped"), batch.GetDroppedCount(), 0);
            }
        }
    }
    TestTrue(TEXT("the table names symbols"), checked > 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryMapRowsNameCropsTest, "SOLTest.Glossary.MapRowsNameCrops",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The five map rows name no symbol (D39) and no row names their symbols; each names its screenshot crop instead
// (33e-ii); Distance darkening shows IconPlanet alone, at one brightness (D40)
bool FSOLGlossaryMapRowsNameCropsTest::RunTest(const FString& /*parameters*/)
{
    for (const FGlossaryTestCropRow& cropRow : GLOSSARYTEST_CROP_ROWS)
    {
        const TCHAR* name = cropRow.Name;
        const int32 index = GlossaryTestEntryNamed(name);
        if (!TestTrue(FString::Printf(TEXT("row '%s' exists"), name), index != INDEX_NONE))
        {
            continue;
        }
        TestEqual(FString::Printf(TEXT("row '%s' names no symbol"), name),
            FString(SOLMenuText::GLOSSARY_ENTRIES[index].Symbols), FString());
        FSOLGlossaryEntry entry;
        TestTrue(FString::Printf(TEXT("row '%s' resolves"), name), SOLGlossary::ResolveEntry(index, entry));
        TestEqual(FString::Printf(TEXT("row '%s' has no symbol slot"), name), entry.SymbolCount, 0);
        TestFalse(FString::Printf(TEXT("row '%s' keeps its description"), name),
            FString(SOLMenuText::GLOSSARY_ENTRIES[index].Description).IsEmpty());
        TestEqual(FString::Printf(TEXT("row '%s' names its crop"), name),
            FString(SOLMenuText::GLOSSARY_ENTRIES[index].Crop), FString(cropRow.Crop));
    }

    // No row names one of those symbols; their ids still resolve
    for (const ESOLHudSymbol symbol : GLOSSARYTEST_CROP_ROW_SYMBOLS)
    {
        const FString id = SOLHudSymbols::ToName(symbol);
        TestTrue(FString::Printf(TEXT("'%s' still resolves"), *id), SOLGlossary::ResolveSymbol(id).IsSet());
        for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
        {
            TestFalse(FString::Printf(TEXT("entry %d '%s' does not name '%s'"), index,
                SOLMenuText::GLOSSARY_ENTRIES[index].Name, *id),
                GlossaryTestSymbolIds(SOLMenuText::GLOSSARY_ENTRIES[index].Symbols).Contains(id));
        }
    }

    // Distance darkening: the planet icon, once
    const int32 darkening = GlossaryTestEntryNamed(TEXT("Distance darkening"));
    if (TestTrue(TEXT("the Distance darkening row exists"), darkening != INDEX_NONE))
    {
        TestEqual(TEXT("Distance darkening names IconPlanet only"),
            FString(SOLMenuText::GLOSSARY_ENTRIES[darkening].Symbols),
            FString(SOLHudSymbols::ToName(ESOLHudSymbol::IconPlanet)));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryMinFeatureTest, "SOLTest.Glossary.MinFeatureFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Build raises every feature of every named symbol to at least GLOSSARY_MIN_FEATURE_PX at any positive scale (D42),
// and the bounds still hold the raised primitives; where the HUD builder is already above the floor Build is that
// builder unchanged, and lines are never touched by the floor
bool FSOLGlossaryMinFeatureTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("a positive floor"), SOL::Help::GLOSSARY_MIN_FEATURE_PX > 0.0f);
    FSOLHudSymbolPrims prims;
    FSOLHudSymbolPrims scratch;
    const TArray<ESOLHudSymbol> symbols = GlossaryTestNamedSymbols();
    TestTrue(TEXT("the table names symbols"), symbols.Num() > 0);
    for (const ESOLHudSymbol symbol : symbols)
    {
        for (const float scale : GLOSSARYTEST_FLOOR_SCALES)
        {
            const FString what = FString::Printf(TEXT("%s at %.2f"), SOLHudSymbols::ToName(symbol), scale);
            const int32 count = SOLHudSymbols::Build(symbol, FVector2f::ZeroVector, scale, prims);
            TestTrue(what + TEXT(": builds"), count >= 1);
            const FBox2f bounds = SOLHudSymbols::Bounds(symbol, scale, scratch);
            TestTrue(what + TEXT(": bounds"), bounds.bIsValid != 0);
            const float tolerance = GLOSSARYTEST_TOLERANCE * 10.0f * FMath::Max(1.0f, scale);
            for (int32 prim = 0; prim < count; ++prim)
            {
                const FString breach = GlossaryTestFloorBreach(prims.Prims[prim]);
                TestTrue(FString::Printf(TEXT("%s: prim %d at the floor (%s)"), *what, prim, *breach),
                    breach.IsEmpty());
                const FBox2f extent = GlossaryTestPrimExtent(prims.Prims[prim]);
                TestTrue(FString::Printf(TEXT("%s: prim %d inside the bounds"), *what, prim), bounds.bIsValid != 0
                    && extent.Min.X >= bounds.Min.X - tolerance && extent.Min.Y >= bounds.Min.Y - tolerance
                    && extent.Max.X <= bounds.Max.X + tolerance && extent.Max.Y <= bounds.Max.Y + tolerance);
            }
        }
    }

    // Above the floor, Build is the HUD builder unchanged (prograde and the bracket at the HUD's scale 1, the
    // reticle at 2 where its dot ring reaches the floor)
    const FVector2f centre(400.25f, 300.5f);
    FSOLHudSymbolPrims reference;
    SOLHudSymbols::AppendPrograde(FVector2D(centre), 1.0f, reference);
    SOLHudSymbols::Build(ESOLHudSymbol::Prograde, centre, 1.0f, prims);
    if (TestEqual(TEXT("prograde at 1: the builder's count"), prims.Count, reference.Count))
    {
        for (int32 prim = 0; prim < prims.Count; ++prim)
        {
            TestTrue(FString::Printf(TEXT("prograde at 1: prim %d unchanged"), prim),
                GlossaryTestSamePrim(prims.Prims[prim], reference.Prims[prim]));
        }
    }
    reference.Reset();
    SOLHudSymbols::AppendTargetBracket(FVector2D(centre), static_cast<float>(SOL::Help::GLOSSARY_BRACKET_HALF_PX),
        reference);
    SOLHudSymbols::Build(ESOLHudSymbol::TargetBracket, centre, 1.0f, prims);
    if (TestEqual(TEXT("bracket at 1: the builder's count"), prims.Count, reference.Count))
    {
        for (int32 prim = 0; prim < prims.Count; ++prim)
        {
            TestTrue(FString::Printf(TEXT("bracket at 1: prim %d unchanged"), prim),
                GlossaryTestSamePrim(prims.Prims[prim], reference.Prims[prim]));
        }
    }
    const float reticleScale = static_cast<float>(SOL::Help::GLOSSARY_MIN_FEATURE_PX
        / SOL::HudGeometry::RETICLE_DOT_RADIUS_PX);
    reference.Reset();
    SOLHudSymbols::AppendReticle(FVector2D(centre), reticleScale, reference);
    SOLHudSymbols::Build(ESOLHudSymbol::Reticle, centre, reticleScale, prims);
    if (TestEqual(TEXT("reticle at its floor scale: the builder's count"), prims.Count, reference.Count))
    {
        for (int32 prim = 0; prim < prims.Count; ++prim)
        {
            TestTrue(FString::Printf(TEXT("reticle at its floor scale: prim %d unchanged"), prim),
                GlossaryTestSamePrim(prims.Prims[prim], reference.Prims[prim]));
        }
    }

    // Below it (the reticle at 1: a 1 px dot ring) only the small feature is raised; the lines stay the builder's
    reference.Reset();
    SOLHudSymbols::AppendReticle(FVector2D(centre), 1.0f, reference);
    SOLHudSymbols::Build(ESOLHudSymbol::Reticle, centre, 1.0f, prims);
    if (TestEqual(TEXT("reticle at 1: the builder's count"), prims.Count, reference.Count))
    {
        for (int32 prim = 0; prim < prims.Count; ++prim)
        {
            const FSOLHudSymbolPrim& built = prims.Prims[prim];
            const FSOLHudSymbolPrim& hud = reference.Prims[prim];
            const FString what = FString::Printf(TEXT("reticle at 1: prim %d"), prim);
            if (hud.Kind == ESOLHudPrimKind::Line)
            {
                TestTrue(what + TEXT(": a line, unchanged"), GlossaryTestSamePrim(built, hud));
            }
            else
            {
                TestTrue(what + TEXT(": same kind"), built.Kind == hud.Kind);
                TestTrue(what + TEXT(": same centre"), built.A.Equals(hud.A, GLOSSARYTEST_TOLERANCE));
                TestTrue(what + TEXT(": raised to the floor"),
                    built.RadiusPx >= SOL::Help::GLOSSARY_MIN_FEATURE_PX - GLOSSARYTEST_TOLERANCE);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Part 33e-ii (D39): the map rows' screenshot crops and the description-only rows
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int32 GLOSSARYTEST_CROPS = 5;                     // Today's GLOSSARY_CROPS (contract B cross-check)
    constexpr int32 GLOSSARYTEST_CROP_PX = 176;                 // Today's GLOSSARY_CROP_PX (contract B cross-check)

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of SOL::Paths::GLOSSARY_CROPS entries
    int32 GlossaryTestCropTableCount()
    {
        return static_cast<int32>(UE_ARRAY_COUNT(SOL::Paths::GLOSSARY_CROPS));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a file is a Git LFS pointer (its object not fetched: "git lfs pull" brings it) rather than the
    // file itself: it starts with "version https://git-lfs". Reads only the first bytes
    bool GlossaryTestIsLfsPointerFile(const FString& path)
    {
        static const ANSICHAR LFS_POINTER_START[] = "version https://git-lfs";
        constexpr int64 startLength = static_cast<int64>(UE_ARRAY_COUNT(LFS_POINTER_START)) - 1;
        const TUniquePtr<FArchive> reader(IFileManager::Get().CreateFileReader(*path, FILEREAD_Silent));
        if (!reader.IsValid() || reader->TotalSize() < startLength)
        {
            return false;
        }
        ANSICHAR head[UE_ARRAY_COUNT(LFS_POINTER_START)] = {};
        reader->Serialize(head, startLength);
        return !reader->IsError() && FMemory::Memcmp(head, LFS_POINTER_START, startLength) == 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a table cell holds any text (null reads empty)
    bool GlossaryTestHasText(const TCHAR* text)
    {
        return text != nullptr && text[0] != 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the GLOSSARY_CROPS index whose Id equals a text exactly (a scan of the table, independent of
    // production), or INDEX_NONE for no text or no match
    int32 GlossaryTestCropIndex(const TCHAR* id)
    {
        if (!GlossaryTestHasText(id))
        {
            return INDEX_NONE;
        }
        for (int32 crop = 0; crop < GlossaryTestCropTableCount(); ++crop)
        {
            if (FCString::Strcmp(SOL::Paths::GLOSSARY_CROPS[crop].Id, id) == 0)
            {
                return crop;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the GLOSSARY_DESCRIPTION_ONLY reason listed for a row name (a scan of the table), or nullptr
    const TCHAR* GlossaryTestListedReason(const TCHAR* name)
    {
        for (const SOLMenuText::FGlossaryDescriptionOnly& listed : SOLMenuText::GLOSSARY_DESCRIPTION_ONLY)
        {
            if (name != nullptr && FCString::Strcmp(listed.Name, name) == 0)
            {
                return listed.Reason;
            }
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the row indices outside GLOSSARY_ENTRIES the contract names: -1, the count, the count + 5 and the int32
    // extremes
    TArray<int32> GlossaryTestBadRows()
    {
        const int32 rows = GlossaryTestRowCount();
        return { -1, rows, rows + 5, MIN_int32, MAX_int32 };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the crop indices outside GLOSSARY_CROPS the contract names: -1, the count and the int32 extremes
    TArray<int32> GlossaryTestBadCrops()
    {
        return { -1, GlossaryTestCropTableCount(), MIN_int32, MAX_int32 };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns every other image name of the player guide: the Keyboard image and each glossary tab's image at every
    // page up to PLAYER_GUIDE_MAX_PAGES (a superset of the PLAYER_GUIDE_TAB_PAGES pages), and the Keyboard hover image
    TArray<FString> GlossaryTestGuideImageNames()
    {
        using namespace SOL::Paths;
        TArray<const TCHAR*> bases = { PLAYER_GUIDE_IMAGE_KEYBOARD };
        for (const TCHAR* name : PLAYER_GUIDE_IMAGE_GLOSSARY)
        {
            bases.Add(name);
        }
        TArray<FString> names = { FString(PLAYER_GUIDE_IMAGE_KEYBOARD_HOVER) };
        for (const TCHAR* base : bases)
        {
            for (int32 page = 0; page < PLAYER_GUIDE_MAX_PAGES; ++page)
            {
                names.Add(SOLPlayerGuide::PageFileName(base, page));
            }
        }
        return names;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryCropRowsTest, "SOLTest.Glossary.CropRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The five map rows name their crop ids and no symbol or sample (D39); no other row names a crop; Space dust is listed
// description-only with a reason; every GLOSSARY_DESCRIPTION_ONLY name is exactly one row's, listed once, with a
// reason, and that row names no symbol, crop or sample
bool FSOLGlossaryCropRowsTest::RunTest(const FString& /*parameters*/)
{
    int32 cropRows = 0;
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        const FString what = FString::Printf(TEXT("entry %d '%s'"), index, row.Name);
        const FGlossaryTestCropRow* expected = nullptr;
        for (const FGlossaryTestCropRow& cropRow : GLOSSARYTEST_CROP_ROWS)
        {
            if (FCString::Strcmp(cropRow.Name, row.Name) == 0)
            {
                expected = &cropRow;
            }
        }
        if (expected != nullptr)
        {
            TestEqual(what + TEXT(": names its crop"), FString(row.Crop), FString(expected->Crop));
            TestFalse(what + TEXT(": names no symbol"), GlossaryTestHasText(row.Symbols));
            TestFalse(what + TEXT(": names no sample"), GlossaryTestHasText(row.Sample));
        }
        else
        {
            TestFalse(FString::Printf(TEXT("%s: names no crop (has '%s')"), *what, row.Crop != nullptr ? row.Crop
                : TEXT("")), GlossaryTestHasText(row.Crop));
        }
        cropRows += GlossaryTestHasText(row.Crop) ? 1 : 0;
    }
    TestEqual(TEXT("five rows name a crop"), cropRows, static_cast<int32>(UE_ARRAY_COUNT(GLOSSARYTEST_CROP_ROWS)));
    for (const FGlossaryTestCropRow& cropRow : GLOSSARYTEST_CROP_ROWS)
    {
        TestTrue(FString::Printf(TEXT("row '%s' exists"), cropRow.Name),
            GlossaryTestEntryNamed(cropRow.Name) != INDEX_NONE);
    }

    // Space dust: description-only on purpose, with a reason
    const int32 dust = GlossaryTestEntryNamed(TEXT("Space dust"));
    if (TestTrue(TEXT("the Space dust row exists"), dust != INDEX_NONE))
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[dust];
        TestFalse(TEXT("Space dust names no symbol"), GlossaryTestHasText(row.Symbols));
        TestFalse(TEXT("Space dust names no crop"), GlossaryTestHasText(row.Crop));
        TestFalse(TEXT("Space dust names no sample"), GlossaryTestHasText(row.Sample));
        TestTrue(TEXT("Space dust is listed description-only with a reason"),
            GlossaryTestHasText(GlossaryTestListedReason(TEXT("Space dust"))));
    }

    // Every listed name is one row's, listed once, with a reason; that row shows nothing
    TSet<FString> listedNames;
    for (const SOLMenuText::FGlossaryDescriptionOnly& listed : SOLMenuText::GLOSSARY_DESCRIPTION_ONLY)
    {
        const FString name(listed.Name != nullptr ? listed.Name : TEXT(""));
        TestFalse(FString::Printf(TEXT("description-only '%s' listed once"), *name), listedNames.Contains(name));
        listedNames.Add(name);
        TestTrue(FString::Printf(TEXT("description-only '%s' has a reason"), *name),
            GlossaryTestHasText(listed.Reason));
        int32 matches = 0;
        int32 match = INDEX_NONE;
        for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
        {
            if (FCString::Strcmp(SOLMenuText::GLOSSARY_ENTRIES[index].Name, *name) == 0)
            {
                ++matches;
                match = index;
            }
        }
        TestEqual(FString::Printf(TEXT("description-only '%s' is exactly one row's name"), *name), matches, 1);
        if (match != INDEX_NONE)
        {
            const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[match];
            TestFalse(FString::Printf(TEXT("description-only '%s' names no symbol"), *name),
                GlossaryTestHasText(row.Symbols));
            TestFalse(FString::Printf(TEXT("description-only '%s' names no crop"), *name),
                GlossaryTestHasText(row.Crop));
            TestFalse(FString::Printf(TEXT("description-only '%s' names no sample"), *name),
                GlossaryTestHasText(row.Sample));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryEveryRowHasArtTest, "SOLTest.Glossary.EveryRowHasArt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every row names symbols, a crop that resolves or a sample, or is description-only with a reason; a crop row names no
// symbol and no sample; a description-only row names no symbol, crop or sample
bool FSOLGlossaryEveryRowHasArtTest::RunTest(const FString& /*parameters*/)
{
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        const FString what = FString::Printf(TEXT("entry %d '%s'"), index, row.Name);
        const bool bSymbols = GlossaryTestHasText(row.Symbols);
        const bool bSample = GlossaryTestHasText(row.Sample);
        const bool bCrop = SOLGlossary::EntryCrop(index) != INDEX_NONE;
        const TCHAR* reason = SOLGlossary::DescriptionOnlyReason(index);
        const bool bReason = GlossaryTestHasText(reason);
        TestTrue(what + TEXT(": names symbols, a crop or a sample, or is description-only with a reason"),
            bSymbols || bCrop || bSample || bReason);

        // A crop row: its crop resolves, and it shows nothing else
        if (GlossaryTestHasText(row.Crop))
        {
            TestTrue(FString::Printf(TEXT("%s: crop '%s' resolves"), *what, row.Crop), bCrop);
            TestFalse(what + TEXT(": a crop row names no symbol"), bSymbols);
            TestFalse(what + TEXT(": a crop row names no sample"), bSample);
        }

        // A description-only row: a reason, and nothing shown
        if (reason != nullptr)
        {
            TestTrue(what + TEXT(": the description-only reason is not empty"), bReason);
            TestFalse(what + TEXT(": a description-only row names no symbol"), bSymbols);
            TestFalse(what + TEXT(": a description-only row names no crop"), GlossaryTestHasText(row.Crop));
            TestFalse(what + TEXT(": a description-only row names no sample"), bSample);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryCropTableTest, "SOLTest.Glossary.CropTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// GLOSSARY_CROPS: unique non-empty ids each named by exactly one row; unique bare .png image names that are no other
// guide image's; unique texture object paths "<GLOSSARY_CROP_TEXTURE_DIR><Name>.<Name>"; each rect inside its capture
// shot; today 5 crops of 176 px, twice the glossary view
bool FSOLGlossaryCropTableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Paths;
    using namespace SOL::Help;
    TestEqual(TEXT("5 crops"), GlossaryTestCropTableCount(), GLOSSARYTEST_CROPS);
    TestEqual(TEXT("GLOSSARY_CROP_PX is 176"), GLOSSARY_CROP_PX, GLOSSARYTEST_CROP_PX);
    TestEqual(TEXT("GLOSSARY_CROP_PX is twice GLOSSARY_VIEW_SIZE"), static_cast<float>(GLOSSARY_CROP_PX),
        2.0f * GLOSSARY_VIEW_SIZE, GLOSSARYTEST_TOLERANCE);
    TestTrue(TEXT("the crop fits the capture"), GLOSSARY_CROP_PX > 0 && GLOSSARY_CROP_PX <= GLOSSARY_CROP_CAPTURE_WIDTH
        && GLOSSARY_CROP_PX <= GLOSSARY_CROP_CAPTURE_HEIGHT);

    const FString dir(GLOSSARY_CROP_TEXTURE_DIR);
    TestTrue(TEXT("the texture folder is a /Game/ folder ending in '/'"), dir.StartsWith(TEXT("/Game/"),
        ESearchCase::CaseSensitive) && dir.EndsWith(TEXT("/"), ESearchCase::CaseSensitive));
    const TArray<FString> guideImages = GlossaryTestGuideImageNames();
    TSet<FString> ids;
    TSet<FString> files;
    TSet<FString> textures;
    for (int32 crop = 0; crop < GlossaryTestCropTableCount(); ++crop)
    {
        const FGlossaryCrop& entry = GLOSSARY_CROPS[crop];
        const FString id(entry.Id != nullptr ? entry.Id : TEXT(""));
        const FString what = FString::Printf(TEXT("crop %d '%s'"), crop, *id);

        // The id: non-empty, unique, named by exactly one row
        TestFalse(what + TEXT(": has an id"), id.IsEmpty());
        TestFalse(what + TEXT(": unique id"), ids.Contains(id));
        ids.Add(id);
        int32 named = 0;
        for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
        {
            const TCHAR* rowCrop = SOLMenuText::GLOSSARY_ENTRIES[index].Crop;
            named += !id.IsEmpty() && rowCrop != nullptr && FCString::Strcmp(rowCrop, *id) == 0 ? 1 : 0;
        }
        TestEqual(what + TEXT(": named by exactly one row"), named, 1);

        // The image: a unique bare .png name, no other guide image's
        const FString file(entry.ImageFile != nullptr ? entry.ImageFile : TEXT(""));
        TestFalse(what + TEXT(": has an image file"), file.IsEmpty());
        TestFalse(FString::Printf(TEXT("%s: unique image file '%s'"), *what, *file), files.Contains(file));
        files.Add(file);
        TestTrue(FString::Printf(TEXT("%s: '%s' ends in .png"), *what, *file),
            file.EndsWith(TEXT(".png"), ESearchCase::CaseSensitive));
        TestFalse(FString::Printf(TEXT("%s: '%s' has no path separator"), *what, *file),
            file.Contains(TEXT("/")) || file.Contains(TEXT("\\")));
        TestFalse(FString::Printf(TEXT("%s: '%s' is no other guide image"), *what, *file), guideImages.Contains(file));

        // The texture: a unique object path "<Dir><Name>.<Name>"
        const FString texture(entry.Texture != nullptr ? entry.Texture : TEXT(""));
        TestTrue(FString::Printf(TEXT("%s: '%s' is in %s"), *what, *texture, *dir),
            texture.StartsWith(dir, ESearchCase::CaseSensitive));
        TestFalse(FString::Printf(TEXT("%s: unique texture '%s'"), *what, *texture), textures.Contains(texture));
        textures.Add(texture);
        const FString rest = texture.StartsWith(dir, ESearchCase::CaseSensitive) ? texture.Mid(dir.Len()) : texture;
        TestFalse(FString::Printf(TEXT("%s: '%s' has no folder below %s"), *what, *texture, *dir),
            rest.Contains(TEXT("/")));
        int32 dot = INDEX_NONE;
        int32 slash = INDEX_NONE;
        const bool bDot = texture.FindLastChar(TEXT('.'), dot);
        texture.FindLastChar(TEXT('/'), slash);
        if (TestTrue(FString::Printf(TEXT("%s: '%s' has an object name after its last '/'"), *what, *texture),
            bDot && dot > slash))
        {
            const FString packageLeaf = texture.Mid(slash + 1, dot - slash - 1);
            const FString objectName = texture.Mid(dot + 1);
            TestFalse(FString::Printf(TEXT("%s: '%s' has a package name"), *what, *texture), packageLeaf.IsEmpty());
            TestTrue(FString::Printf(TEXT("%s: '%s' object name equals its package name"), *what, *texture),
                !objectName.IsEmpty() && objectName.Equals(packageLeaf, ESearchCase::CaseSensitive));
        }

        // The rect: inside the capture shot
        TestTrue(FString::Printf(TEXT("%s: left %d >= 0"), *what, entry.Left), entry.Left >= 0);
        TestTrue(FString::Printf(TEXT("%s: top %d >= 0"), *what, entry.Top), entry.Top >= 0);
        TestTrue(FString::Printf(TEXT("%s: left %d + %d <= %d"), *what, entry.Left, GLOSSARY_CROP_PX,
            GLOSSARY_CROP_CAPTURE_WIDTH), entry.Left + GLOSSARY_CROP_PX <= GLOSSARY_CROP_CAPTURE_WIDTH);
        TestTrue(FString::Printf(TEXT("%s: top %d + %d <= %d"), *what, entry.Top, GLOSSARY_CROP_PX,
            GLOSSARY_CROP_CAPTURE_HEIGHT), entry.Top + GLOSSARY_CROP_PX <= GLOSSARY_CROP_CAPTURE_HEIGHT);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryFindCropTest, "SOLTest.Glossary.FindCrop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CropCount is the table's; FindCrop gives each id's table index (also from a view into a longer string) and
// INDEX_NONE for an empty, unknown, differently-cased, prefixed, extended or space-padded id
bool FSOLGlossaryFindCropTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("CropCount is the table's"), SOLGlossary::CropCount(), GlossaryTestCropTableCount());
    for (int32 crop = 0; crop < GlossaryTestCropTableCount(); ++crop)
    {
        const TCHAR* id = SOL::Paths::GLOSSARY_CROPS[crop].Id;
        const FString idText(id != nullptr ? id : TEXT(""));
        TestEqual(FString::Printf(TEXT("FindCrop('%s') round trip"), *idText), SOLGlossary::FindCrop(idText), crop);

        // A view of the id's length into a longer string is the id
        const FString longer = idText + TEXT("XYZ");
        TestEqual(FString::Printf(TEXT("FindCrop of a view '%s' into '%s'"), *idText, *longer),
            SOLGlossary::FindCrop(FStringView(*longer, idText.Len())), crop);

        // Its lower-case form, a prefix and its padded forms are not ids (unless the table says so)
        const FString lower = idText.ToLower();
        if (!lower.Equals(idText, ESearchCase::CaseSensitive))
        {
            TestEqual(FString::Printf(TEXT("FindCrop('%s') is case-sensitive"), *lower), SOLGlossary::FindCrop(lower),
                INDEX_NONE);
        }
        if (idText.Len() > 1)
        {
            const FString prefix = idText.LeftChop(1);
            TestEqual(FString::Printf(TEXT("FindCrop('%s') takes no prefix"), *prefix), SOLGlossary::FindCrop(prefix),
                GlossaryTestCropIndex(*prefix));
        }
        const FString padded[] = { FString(TEXT(" ")) + idText, idText + TEXT(" "), FString(TEXT(" ")) + idText
            + TEXT(" "), idText + TEXT("X") };
        for (const FString& text : padded)
        {
            TestEqual(FString::Printf(TEXT("FindCrop('%s') is no id"), *text), SOLGlossary::FindCrop(text),
                INDEX_NONE);
        }
    }

    // The contract's refusals
    const TCHAR* const refused[] = { TEXT(""), TEXT("NoSuchCrop"), TEXT("orbitlines"), TEXT("ORBITLINES"),
        TEXT("Orbit"), TEXT("OrbitLinesX"), TEXT(" OrbitLines"), TEXT("OrbitLines "), TEXT(" OrbitLines ") };
    for (const TCHAR* text : refused)
    {
        TestEqual(FString::Printf(TEXT("FindCrop('%s')"), text), SOLGlossary::FindCrop(text), INDEX_NONE);
    }
    TestEqual(TEXT("FindCrop of an empty view"), SOLGlossary::FindCrop(FStringView()), INDEX_NONE);
    TestEqual(TEXT("FindCrop('OrbitLines') is the table's"), SOLGlossary::FindCrop(TEXT("OrbitLines")),
        GlossaryTestCropIndex(TEXT("OrbitLines")));
    TestTrue(TEXT("OrbitLines is in the table"), GlossaryTestCropIndex(TEXT("OrbitLines")) != INDEX_NONE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryEntryCropTest, "SOLTest.Glossary.EntryCrop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// EntryCrop is the table index a row's Crop names (INDEX_NONE for no crop and for an index outside the table); the
// five map rows each resolve, and every crop is some row's exactly once
bool FSOLGlossaryEntryCropTest::RunTest(const FString& /*parameters*/)
{
    TArray<int32> uses;
    uses.Init(0, GlossaryTestCropTableCount());
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        const int32 expected = GlossaryTestCropIndex(row.Crop);
        const int32 crop = SOLGlossary::EntryCrop(index);
        TestEqual(FString::Printf(TEXT("entry %d '%s' crop"), index, row.Name), crop, expected);
        if (uses.IsValidIndex(crop))
        {
            ++uses[crop];
        }
    }
    for (int32 crop = 0; crop < uses.Num(); ++crop)
    {
        TestEqual(FString::Printf(TEXT("crop %d '%s' is exactly one row's"), crop, SOL::Paths::GLOSSARY_CROPS[crop].Id),
            uses[crop], 1);
    }

    // The five map rows by name
    for (const FGlossaryTestCropRow& cropRow : GLOSSARYTEST_CROP_ROWS)
    {
        const int32 index = GlossaryTestEntryNamed(cropRow.Name);
        if (TestTrue(FString::Printf(TEXT("row '%s' exists"), cropRow.Name), index != INDEX_NONE))
        {
            const int32 expected = GlossaryTestCropIndex(cropRow.Crop);
            TestTrue(FString::Printf(TEXT("crop '%s' is in the table"), cropRow.Crop), expected != INDEX_NONE);
            TestEqual(FString::Printf(TEXT("row '%s' shows crop '%s'"), cropRow.Name, cropRow.Crop),
                SOLGlossary::EntryCrop(index), expected);
        }
    }
    const int32 dust = GlossaryTestEntryNamed(TEXT("Space dust"));
    if (TestTrue(TEXT("the Space dust row exists"), dust != INDEX_NONE))
    {
        TestEqual(TEXT("Space dust has no crop"), SOLGlossary::EntryCrop(dust), INDEX_NONE);
    }

    // Outside the table
    for (const int32 index : GlossaryTestBadRows())
    {
        TestEqual(FString::Printf(TEXT("EntryCrop(%d)"), index), SOLGlossary::EntryCrop(index), INDEX_NONE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryDescriptionOnlyReasonTest, "SOLTest.Glossary.DescriptionOnlyReason",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DescriptionOnlyReason is the GLOSSARY_DESCRIPTION_ONLY reason listed for the row's Name, nullptr for a row not listed
// (a map crop row among them) and for an index outside the table
bool FSOLGlossaryDescriptionOnlyReasonTest::RunTest(const FString& /*parameters*/)
{
    for (int32 index = 0; index < GlossaryTestRowCount(); ++index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        const FString what = FString::Printf(TEXT("entry %d '%s'"), index, row.Name);
        const TCHAR* expected = GlossaryTestListedReason(row.Name);
        const TCHAR* reason = SOLGlossary::DescriptionOnlyReason(index);
        if (expected == nullptr)
        {
            TestTrue(FString::Printf(TEXT("%s: not listed, no reason (got '%s')"), *what, reason != nullptr ? reason
                : TEXT("")), reason == nullptr);
        }
        else if (TestTrue(what + TEXT(": listed, has a reason"), reason != nullptr))
        {
            TestEqual(what + TEXT(": the listed reason"), FString(reason), FString(expected));
        }
    }

    // Space dust is listed; a map crop row is not
    const int32 dust = GlossaryTestEntryNamed(TEXT("Space dust"));
    if (TestTrue(TEXT("the Space dust row exists"), dust != INDEX_NONE))
    {
        const TCHAR* reason = SOLGlossary::DescriptionOnlyReason(dust);
        TestTrue(TEXT("Space dust has a reason"), GlossaryTestHasText(reason));
        TestEqual(TEXT("Space dust's reason is the listed one"), FString(reason != nullptr ? reason : TEXT("")),
            FString(GlossaryTestListedReason(TEXT("Space dust")) != nullptr
                ? GlossaryTestListedReason(TEXT("Space dust")) : TEXT("<not listed>")));
    }
    for (const FGlossaryTestCropRow& cropRow : GLOSSARYTEST_CROP_ROWS)
    {
        const int32 index = GlossaryTestEntryNamed(cropRow.Name);
        if (index != INDEX_NONE)
        {
            TestTrue(FString::Printf(TEXT("'%s' is not description-only"), cropRow.Name),
                SOLGlossary::DescriptionOnlyReason(index) == nullptr);
        }
    }

    // Outside the table
    for (const int32 index : GlossaryTestBadRows())
    {
        TestTrue(FString::Printf(TEXT("DescriptionOnlyReason(%d) is nullptr"), index),
            SOLGlossary::DescriptionOnlyReason(index) == nullptr);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryCropRectTest, "SOLTest.Glossary.CropRect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CropRect is (Left, Top) to (Left + GLOSSARY_CROP_PX, Top + GLOSSARY_CROP_PX), a GLOSSARY_CROP_PX square inside its
// capture shot; an all-zero rect for a crop outside the table
bool FSOLGlossaryCropRectTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    for (int32 crop = 0; crop < GlossaryTestCropTableCount(); ++crop)
    {
        const SOL::Paths::FGlossaryCrop& entry = SOL::Paths::GLOSSARY_CROPS[crop];
        const FString what = FString::Printf(TEXT("crop %d '%s'"), crop, entry.Id);
        const FIntRect rect = SOLGlossary::CropRect(crop);
        TestEqual(what + TEXT(": min x"), rect.Min.X, entry.Left);
        TestEqual(what + TEXT(": min y"), rect.Min.Y, entry.Top);
        TestEqual(what + TEXT(": max x"), rect.Max.X, entry.Left + GLOSSARY_CROP_PX);
        TestEqual(what + TEXT(": max y"), rect.Max.Y, entry.Top + GLOSSARY_CROP_PX);
        TestEqual(what + TEXT(": width"), rect.Width(), GLOSSARY_CROP_PX);
        TestEqual(what + TEXT(": height"), rect.Height(), GLOSSARY_CROP_PX);
        TestTrue(what + TEXT(": inside the capture shot"), rect.Min.X >= 0 && rect.Min.Y >= 0
            && rect.Max.X <= GLOSSARY_CROP_CAPTURE_WIDTH && rect.Max.Y <= GLOSSARY_CROP_CAPTURE_HEIGHT);
    }

    // Outside the table
    for (const int32 crop : GlossaryTestBadCrops())
    {
        const FIntRect rect = SOLGlossary::CropRect(crop);
        TestTrue(FString::Printf(TEXT("CropRect(%d) is all zero, got (%d, %d)-(%d, %d)"), crop, rect.Min.X,
            rect.Min.Y, rect.Max.X, rect.Max.Y), rect == FIntRect());
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryCropAssetFileTest, "SOLTest.Glossary.CropAssetFile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CropAssetFile is the .uasset of the texture's package (the object path before its last '.') under
// Content/SOL/UI/Glossary/, one per crop; empty for a crop outside the table
bool FSOLGlossaryCropAssetFileTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("the asset extension is .uasset"), FPackageName::GetAssetPackageExtension(),
        FString(TEXT(".uasset")));
    TSet<FString> assetFiles;
    for (int32 crop = 0; crop < GlossaryTestCropTableCount(); ++crop)
    {
        const SOL::Paths::FGlossaryCrop& entry = SOL::Paths::GLOSSARY_CROPS[crop];
        const FString what = FString::Printf(TEXT("crop %d '%s'"), crop, entry.Id);
        const FString texture(entry.Texture);

        // The package: the object path before its last '.'
        int32 dot = INDEX_NONE;
        const FString package = texture.FindLastChar(TEXT('.'), dot) ? texture.Left(dot) : texture;
        TestEqual(what + TEXT(": the engine reads the same package"), FPackageName::ObjectPathToPackageName(texture),
            package);
        int32 slash = INDEX_NONE;
        package.FindLastChar(TEXT('/'), slash);
        const FString leaf = package.Mid(slash + 1);

        // The file
        const FString expected = FPackageName::LongPackageNameToFilename(package,
            FPackageName::GetAssetPackageExtension());
        const FString file = SOLGlossary::CropAssetFile(crop);
        TestEqual(what + TEXT(": the package's .uasset"), file, expected);
        TestTrue(FString::Printf(TEXT("%s: '%s' ends in .uasset"), *what, *file),
            file.EndsWith(TEXT(".uasset"), ESearchCase::CaseSensitive));
        const FString normalised = file.Replace(TEXT("\\"), TEXT("/"));
        TestTrue(FString::Printf(TEXT("%s: '%s' is in Content/SOL/UI/Glossary/"), *what, *file),
            normalised.Contains(TEXT("Content/SOL/UI/Glossary/"), ESearchCase::CaseSensitive));
        TestTrue(FString::Printf(TEXT("%s: '%s' is %s.uasset"), *what, *file, *leaf),
            !leaf.IsEmpty() && normalised.EndsWith(FString(TEXT("/")) + leaf + TEXT(".uasset"),
            ESearchCase::CaseSensitive));
        TestFalse(FString::Printf(TEXT("%s: unique asset file '%s'"), *what, *file), assetFiles.Contains(file));
        assetFiles.Add(file);
    }

    // Outside the table
    for (const int32 crop : GlossaryTestBadCrops())
    {
        TestTrue(FString::Printf(TEXT("CropAssetFile(%d) is empty"), crop), SOLGlossary::CropAssetFile(crop).IsEmpty());
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryCropAssetsExistTest, "SOLTest.Glossary.CropAssetsExist",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every crop's texture asset is committed (D27-style: it fails, naming the crop and Tools\WritePlayerGuide.bat, until
// the run has captured and imported the crops); an asset file that is only its Git LFS pointer fails asking for
// "git lfs pull"
bool FSOLGlossaryCropAssetsExistTest::RunTest(const FString& /*parameters*/)
{
    IFileManager& fileManager = IFileManager::Get();
    if (SOLGlossary::CropCount() == 0 || GlossaryTestCropTableCount() == 0)
    {
        AddError(TEXT("No glossary crops listed (SOL::Paths::GLOSSARY_CROPS / SOLGlossary::CropCount)"));
    }
    for (int32 crop = 0; crop < GlossaryTestCropTableCount(); ++crop)
    {
        const SOL::Paths::FGlossaryCrop& entry = SOL::Paths::GLOSSARY_CROPS[crop];
        const FString file = SOLGlossary::CropAssetFile(crop);
        if (file.IsEmpty() || !fileManager.FileExists(*file))
        {
            AddError(FString::Printf(TEXT("Glossary crop '%s': its texture %s is missing (%s): run "
                "Tools\\WritePlayerGuide.bat (with the editor closed), which captures and imports the glossary crops, "
                "and commit the textures and images"), entry.Id, entry.Texture,
                file.IsEmpty() ? TEXT("no asset file") : *file));
        }
        else if (GlossaryTestIsLfsPointerFile(file))
        {
            AddError(FString::Printf(TEXT("Glossary crop '%s': its texture %s (%s) is a Git LFS pointer, not the "
                "asset: run git lfs pull"), entry.Id, entry.Texture, *file));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryCropTexturesCurrentTest, "SOLTest.Glossary.CropTexturesCurrent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every crop's texture was imported from the PNG now in Docs/Images: its SOLSourceMD5 metadata tag (written by
// Tools/UI/import_glossary_crops.py as "<source MD5>-<import settings>") starts with that PNG's MD5. A PNG or texture
// asset that is only its Git LFS pointer fails asking for "git lfs pull" (regenerating would not help)
bool FSOLGlossaryCropTexturesCurrentTest::RunTest(const FString& /*parameters*/)
{
#if WITH_EDITORONLY_DATA
    for (int32 crop = 0; crop < GlossaryTestCropTableCount(); ++crop)
    {
        const SOL::Paths::FGlossaryCrop& entry = SOL::Paths::GLOSSARY_CROPS[crop];
        const FString image = SOLPlayerGuide::ImagePath(entry.ImageFile);
        const FString assetFile = SOLGlossary::CropAssetFile(crop);
        const bool bImagePointer = GlossaryTestIsLfsPointerFile(image);
        if (bImagePointer || (!assetFile.IsEmpty() && GlossaryTestIsLfsPointerFile(assetFile)))
        {
            AddError(FString::Printf(TEXT("Glossary crop '%s': %s is a Git LFS pointer, not the file: run git lfs "
                "pull"), entry.Id, bImagePointer ? *image : *assetFile));
            continue;
        }
        const FMD5Hash hash = FMD5Hash::HashFile(*image);
        const UTexture2D* texture = LoadObject<UTexture2D>(nullptr, entry.Texture);
        const FString* tag = texture != nullptr
            ? texture->GetPackage()->GetMetaData().FindValue(texture, TEXT("SOLSourceMD5")) : nullptr;
        if (!hash.IsValid() || tag == nullptr || !tag->StartsWith(LexToString(hash)))
        {
            AddError(FString::Printf(TEXT("Glossary crop '%s': its texture %s is not the import of %s (%s): run "
                "Tools\\WritePlayerGuide.bat (with the editor closed), which captures and imports the glossary crops, "
                "and commit the textures and images"), entry.Id, entry.Texture, *image,
                !hash.IsValid() ? TEXT("no image") : texture == nullptr ? TEXT("no texture")
                : tag == nullptr ? TEXT("no SOLSourceMD5 tag") : TEXT("a different image's MD5")));
        }
    }
#endif
    return true;
}

#endif
