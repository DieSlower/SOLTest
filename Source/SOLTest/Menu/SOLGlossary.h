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
#include "Math/Box2D.h"
#include "Math/IntRect.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"

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

// (33d-ii) What a glossary row's symbol box shows: its HUD symbols, its text sample, its screenshot crop (33e-ii, D39)
// or nothing (no box)
enum class ESOLGlossaryArt : uint8
{
    Symbols,
    Sample,
    None,
    Crop,
};

// (33d-ii) The near-square grid of equal cells several symbols of one row share in its box
struct FSOLGlossaryCellGrid
{
    int32 Columns = 0;
    int32 Rows = 0;
};

// (33d-ii) One line of a laid-out text sample: its text and its top-left in the area (pixels)
struct FSOLGlossarySampleLine
{
    FString Text;
    FVector2f Position = FVector2f::ZeroVector;
};

// (33d-ii) A text sample laid out in a symbol box: its lines and the scale they are drawn at (0: nothing to draw)
struct FSOLGlossarySampleLayout
{
    TArray<FSOLGlossarySampleLine> Lines;
    float Scale = 0.0f;
};

// (33d-ii) What a symbol view was last built for: its pixel size and the sanitized effects strength
struct FSOLGlossaryViewStamp
{
    FIntPoint SizePx = FIntPoint::ZeroValue;
    float Strength = -1.0f;
    bool bBuilt = false;
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

    // ---- (33d-ii) The symbol box's layout and the symbol view's rebuild rule ----

    // Returns what a GLOSSARY_ENTRIES row's box shows: Symbols when it names symbols, else Sample when it has a
    // sample, else None (also for an index outside the table)
    SOLTEST_API ESOLGlossaryArt EntryArt(int32 index);

    // Returns the grid count symbols share: Columns = ceil(sqrt(count)), Rows = ceil(count / Columns); {0, 0} for
    // count <= 0
    SOLTEST_API FSOLGlossaryCellGrid CellGrid(int32 count);

    // Returns cell's rect of count equal cells filling area row-major, a short last row centred; an invalid box for
    // a cell outside [0, count), count <= 0 or an invalid or empty area
    SOLTEST_API FBox2f CellRect(int32 cell, int32 count, const FBox2f& area);

    // Returns the scale that shrinks a symbol (bounds at maxScale given) to fit the cell, never enlarging; 0 for a
    // maxScale or cell axis that is not a positive finite number, or invalid bounds
    SOLTEST_API float SymbolFitScale(const FBox2f& boundsAtMaxScale, const FVector2f& cellSizePx, float maxScale);

    // Returns the point to build a symbol at so its bounds (relative to its centre, at the drawn scale) are centred
    // in the cell (the cell centre for invalid bounds)
    SOLTEST_API FVector2f SymbolCentre(const FBox2f& boundsAtScale, const FBox2f& cell);

    // Lays a text sample's '|' parts out as lines centred in an area (D44), the first that fits: unwrapped at
    // maxScale, wrapped at spaces at maxScale, shrunk unwrapped down to minScale, else wrapped at minScale (out reset
    // first, its capacity kept; nothing to draw for no part or an invalid area or scale)
    SOLTEST_API void LayoutSample(FStringView sample, const FVector2f& areaPx, float maxScale, float minScale,
        float lineHeightAtUnit, TFunctionRef<float(FStringView)> measureAtUnit, FSOLGlossarySampleLayout& out);

    // Returns the number of '|' parts LayoutSample draws (trimmed, the empty ones dropped)
    SOLTEST_API int32 SamplePartCount(FStringView sample);

    // Returns a text-sample row's box size in menu units (D44): the square symbol box when no part wraps there at
    // maxScale (only single words too wide for it), else
    // GLOSSARY_SAMPLE_BOX_WIDTH wide and as tall as the sample's lines at the scales given need (never below the box)
    SOLTEST_API FVector2f SampleBoxSize(FStringView sample, float maxScale, float minScale, float lineHeightAtUnit,
        TFunctionRef<float(FStringView)> measureAtUnit);

    // Returns a view's size in pixels (rounded half away from zero); (0, 0) for a non-finite input, a scale <= 0 or a
    // local axis <= 0
    SOLTEST_API FIntPoint ViewPixelSize(const FVector2f& localSize, float layoutScale);

    // Returns whether a view laid out at sizePx must rebuild its batch: never before it is laid out (an axis <= 0),
    // else when not yet built or its pixel size or sanitized strength changed
    SOLTEST_API bool ViewNeedsRebuild(const FSOLGlossaryViewStamp& stamp, const FIntPoint& sizePx, float strength);

    // Records a build at sizePx and strength (sanitized) in the stamp
    SOLTEST_API void MarkViewBuilt(FSOLGlossaryViewStamp& stamp, const FIntPoint& sizePx, float strength);

    // Returns the HUD palette role a laid-out sample line of a GLOSSARY_ENTRIES row is drawn in (D2): in a row that
    // explains flight modes, a line that is a mode's HUD label takes that mode's colour (MANUAL Secondary, ASSISTED
    // Primary, AUTO Good); every other line, and any line of another row or an index outside the table, Primary
    SOLTEST_API ESOLHudPaletteRole SampleLineRole(int32 index, FStringView line);

    // ---- (33e-ii, D39) The map rows' screenshot crops (SOL::Paths::GLOSSARY_CROPS) ----

    // Returns the number of SOL::Paths::GLOSSARY_CROPS
    SOLTEST_API int32 CropCount();

    // Returns the GLOSSARY_CROPS index of a crop id (case-sensitive); INDEX_NONE for an empty or unknown id
    SOLTEST_API int32 FindCrop(FStringView id);

    // Returns the GLOSSARY_CROPS index a GLOSSARY_ENTRIES row's Crop names; INDEX_NONE for none, an unknown id or an
    // index outside the table
    SOLTEST_API int32 EntryCrop(int32 index);

    // Returns why a GLOSSARY_ENTRIES row is description-only (its SOLMenuText::GLOSSARY_DESCRIPTION_ONLY reason, by
    // Name); nullptr for a row not listed there and an index outside the table
    SOLTEST_API const TCHAR* DescriptionOnlyReason(int32 index);

    // Returns a crop's rect in its capture shot: (Left, Top) to (Left + GLOSSARY_CROP_PX, Top + GLOSSARY_CROP_PX);
    // an empty rect (all zero) for a crop outside the table
    SOLTEST_API FIntRect CropRect(int32 crop);

    // Returns the full path of a crop texture's .uasset (its package, FPackageName::LongPackageNameToFilename with
    // FPackageName::GetAssetPackageExtension()); empty for a crop outside the table
    SOLTEST_API FString CropAssetFile(int32 crop);
}
