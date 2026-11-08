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
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudSymbols.h"
#include "UI/SOLOverlayIcons.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a Help tabs and HUD glossary (Docs/SDDs/33-player-documentation.md sections 5, 8 and
// 10, D6, D7, D22, D26), written against the header contract (Menu/SOLGlossary.h) and the SOLMenuText tables before
// the production code existed: the five Help tabs, the GLOSSARY_ENTRIES rows resolved, the description formatter and
// the completeness of the coverage table (every HUD line, icon category, card, steer icon and flight mode explained).
// Part 33d-i: every symbol a row names builds a sane picture at the glossary scale, no feature below the glossary
// floor (D42); the five map rows are description-only (D39) and Distance darkening shows IconPlanet alone (D40).

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

    // The rows the user left description-only (D39): their pictures come later as screenshot crops
    const TCHAR* const GLOSSARYTEST_PICTURELESS_ROWS[] = { TEXT("Orbit lines"), TEXT("Route legs"), TEXT("Waypoints"),
        TEXT("Map destination pick"), TEXT("Map grid and stems") };

    // The symbols of those rows (ids kept in ESOLHudSymbol, no builder required)
    constexpr ESOLHudSymbol GLOSSARYTEST_PICTURELESS_SYMBOLS[] = { ESOLHudSymbol::OrbitLine, ESOLHudSymbol::RouteLeg,
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryDescriptionOnlyTest, "SOLTest.Glossary.DescriptionOnlyRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The five map rows name no symbol (D39) and no row names their symbols; Distance darkening shows IconPlanet alone,
// at one brightness (D40)
bool FSOLGlossaryDescriptionOnlyTest::RunTest(const FString& /*parameters*/)
{
    for (const TCHAR* name : GLOSSARYTEST_PICTURELESS_ROWS)
    {
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
    }

    // No row names a pictureless symbol; their ids still resolve
    for (const ESOLHudSymbol symbol : GLOSSARYTEST_PICTURELESS_SYMBOLS)
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

#endif
