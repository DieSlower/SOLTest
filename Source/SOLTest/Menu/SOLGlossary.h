/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlightMode.h"
#include "SOLConstants.h"
#include "UI/SOLHudLines.h"
#include "UI/SOLHudSymbols.h"
#include "UI/SOLOverlayIcons.h"

#include "Containers/StringView.h"
#include "CoreMinimal.h"
#include "Misc/Optional.h"

// The Help page's tabs in rail order (SDD 33 section 5, D22): the Keys group, then the four HUD glossary tabs (the
// index into SOLMenuText::HELP_TAB_LABELS and HELP_TAB_HEADINGS)
enum class ESOLHelpTab : uint8
{
    Keyboard,
    FlightHud,
    TargetsRadar,
    OverlaysRoute,
    MenusMap,
    Count,
};

// What a glossary entry's coverage item names (the HUD ids of D6 the completeness test requires an entry for)
enum class ESOLGlossaryCoverage : uint8
{
    HudLine,        // An ESOLHudLine
    IconCategory,   // A SOLOverlayIcons::EIconCategory
    HudCard,        // An ESOLHudCard
    SteerIcon,      // An ESOLSteerIcon
    FlightMode,     // An ESOLFlightMode
    Count,
};

// One HUD id an entry explains: its kind and the enum value (as an integer)
struct FSOLGlossaryCoverageItem
{
    ESOLGlossaryCoverage Kind = ESOLGlossaryCoverage::Count;
    uint8 Value = 0;
};

// A GLOSSARY_ENTRIES row resolved: its tab and its symbols (the raw strings stay in the table)
struct FSOLGlossaryEntry
{
    int32 Index = INDEX_NONE;                                           // Row of SOLMenuText::GLOSSARY_ENTRIES
    ESOLHelpTab Tab = ESOLHelpTab::Count;                               // Count for a tab name not in GLOSSARY_TABS
    ESOLHudSymbol Symbols[SOL::Help::GLOSSARY_MAX_SYMBOLS_PER_ENTRY];
    int32 SymbolCount = 0;
    bool bSymbolsResolved = false;                                      // Every id resolved and fitted (none past
                                                                        // GLOSSARY_MAX_SYMBOLS_PER_ENTRY)

    // Starts with every symbol slot Count
    FSOLGlossaryEntry()
    {
        for (ESOLHudSymbol& symbol : Symbols)
        {
            symbol = ESOLHudSymbol::Count;
        }
    }
};

/**
 * The Help page's tabs and the HUD glossary's pure logic (SDD 33 sections 5 and 8, D6, D7), test-first: the tab list,
 * the entries of SOLMenuText::GLOSSARY_ENTRIES resolved (symbol ids to ESOLHudSymbol), the description formatter
 * (live keys and constants) and the coverage table (which HUD lines, icon categories, cards, steer icons and flight
 * modes each entry explains) the completeness tests read.
 */
namespace SOLGlossary
{
    // Returns the number of Help tabs (ESOLHelpTab::Count)
    SOLTEST_API int32 TabCount();

    // Returns a tab's short rail label (SOLMenuText::HELP_TAB_LABELS; "" for an invalid tab)
    SOLTEST_API const TCHAR* TabLabel(ESOLHelpTab tab);

    // Returns a tab's full heading (SOLMenuText::HELP_TAB_HEADINGS; "" for an invalid tab)
    SOLTEST_API const TCHAR* TabHeading(ESOLHelpTab tab);

    // Returns the group caption shown above a tab in the rail: HELP_CAPTION_KEYS above Keyboard,
    // HELP_CAPTION_GLOSSARY above FlightHud, "" for the others and an invalid tab
    SOLTEST_API const TCHAR* TabCaption(ESOLHelpTab tab);

    // Returns the glossary tab of a GLOSSARY_TABS full name (case-sensitive), or Count
    SOLTEST_API ESOLHelpTab TabFromGlossaryName(const TCHAR* tabName);

    // Returns the symbol of a GLOSSARY_ENTRIES id (SOLHudSymbols::FromName), unset for an unknown id
    SOLTEST_API TOptional<ESOLHudSymbol> ResolveSymbol(FStringView id);

    // Returns the number of GLOSSARY_ENTRIES rows
    SOLTEST_API int32 EntryCount();

    // Resolves a GLOSSARY_ENTRIES row into out; false for an index outside the table, a tab not in GLOSSARY_TABS or a
    // symbol id that does not resolve (out still holds what did)
    SOLTEST_API bool ResolveEntry(int32 index, FSOLGlossaryEntry& out);

    // Fills outIndices (Reset first, its capacity kept) with the GLOSSARY_ENTRIES rows of a tab, in table order (none
    // for Keyboard and an invalid tab)
    SOLTEST_API void EntriesOfTab(ESOLHelpTab tab, TArray<int32>& outIndices);

    // Formats a description into out (overwritten): '{<BindingId>}' becomes that action's live key
    // (SOLKeyBindings::FormatKeyText), then each '{<Token>}' of section 8.5 its constant's text (from the same sources
    // as the Info page); an unknown token is left as written
    SOLTEST_API void FormatEntryText(const TCHAR* text, const TArray<FString>& liveKeys, FString& out);

    // Returns the HUD ids a GLOSSARY_ENTRIES row explains (empty for an index outside the table)
    SOLTEST_API TConstArrayView<FSOLGlossaryCoverageItem> EntryCoverage(int32 index);

    // Returns the number of values a coverage kind has (its enum's Count; 0 for Count)
    SOLTEST_API int32 CoverageValueCount(ESOLGlossaryCoverage kind);

    // Returns whether at least one entry explains this HUD id (false for a value at or past CoverageValueCount)
    SOLTEST_API bool IsCovered(ESOLGlossaryCoverage kind, int32 value);

    // IsCovered for a HUD text line
    SOLTEST_API bool IsCovered(ESOLHudLine line);

    // IsCovered for an overlay icon category
    SOLTEST_API bool IsCovered(SOLOverlayIcons::EIconCategory category);

    // IsCovered for a mouse-steer icon
    SOLTEST_API bool IsCovered(ESOLSteerIcon icon);

    // IsCovered for a flight mode
    SOLTEST_API bool IsCovered(ESOLFlightMode mode);
}
