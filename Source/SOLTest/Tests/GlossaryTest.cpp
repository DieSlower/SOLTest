/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlightMode.h"
#include "Menu/SOLGlossary.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLHudLines.h"
#include "UI/SOLHudSymbols.h"
#include "UI/SOLOverlayIcons.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a Help tabs and HUD glossary (Docs/SDDs/33-player-documentation.md sections 5, 8 and
// 10, D6, D7, D22, D26), written against the header contract (Menu/SOLGlossary.h) and the SOLMenuText tables before
// the production code existed: the five Help tabs, the GLOSSARY_ENTRIES rows resolved, the description formatter and
// the completeness of the coverage table (every HUD line, icon category, card, steer icon and flight mode explained).

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

#endif
