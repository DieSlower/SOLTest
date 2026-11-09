/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLGlossary.h"

#include "Menu/SOLInfoText.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Npc/SOLNpcStatsText.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLRadarLayout.h"

#include "Misc/PackageName.h"
#include "String/ParseTokens.h"

static_assert(UE_ARRAY_COUNT(SOLMenuText::HELP_TAB_LABELS) == static_cast<int32>(ESOLHelpTab::Count),
    "one rail label per Help tab");
static_assert(UE_ARRAY_COUNT(SOLMenuText::HELP_TAB_HEADINGS) == static_cast<int32>(ESOLHelpTab::Count),
    "one heading per Help tab");
static_assert(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_TABS) + 1 == static_cast<int32>(ESOLHelpTab::Count),
    "the glossary tabs are the Help tabs after Keyboard");

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Builds a coverage item for an ESOLHudLine
    constexpr FSOLGlossaryCoverageItem GlossaryCoverLine(const ESOLHudLine line)
    {
        return { ESOLGlossaryCoverage::HudLine, static_cast<uint8>(line) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds a coverage item for an overlay icon category
    constexpr FSOLGlossaryCoverageItem GlossaryCoverIcon(const SOLOverlayIcons::EIconCategory category)
    {
        return { ESOLGlossaryCoverage::IconCategory, static_cast<uint8>(category) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds a coverage item for a HUD card
    constexpr FSOLGlossaryCoverageItem GlossaryCoverCard(const ESOLHudCard card)
    {
        return { ESOLGlossaryCoverage::HudCard, static_cast<uint8>(card) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds a coverage item for a mouse-steer icon
    constexpr FSOLGlossaryCoverageItem GlossaryCoverSteer(const ESOLSteerIcon icon)
    {
        return { ESOLGlossaryCoverage::SteerIcon, static_cast<uint8>(icon) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds a coverage item for a flight mode
    constexpr FSOLGlossaryCoverageItem GlossaryCoverMode(const ESOLFlightMode mode)
    {
        return { ESOLGlossaryCoverage::FlightMode, static_cast<uint8>(mode) };
    }

    // One coverage item and the GLOSSARY_ENTRIES row that explains it, named by the row's Name (SDD 33 section 8.4):
    // the row is found by its name at compile time, so adding, removing or moving a row cannot shift the coverage
    struct FGlossaryCoverageRow
    {
        const TCHAR* EntryName;             // SOLMenuText::FGlossaryEntry::Name of the row
        FSOLGlossaryCoverageItem Item;
    };

    using GlossaryLine = ESOLHudLine;
    using GlossaryIcon = SOLOverlayIcons::EIconCategory;
    using GlossaryCard = ESOLHudCard;

    // The HUD ids each glossary row explains, one item per line, in GLOSSARY_ENTRIES row order (checked below)
    constexpr FGlossaryCoverageRow GLOSSARY_COVERAGE_TABLE[] =
    {
        { TEXT("Mouse-steer icon"), GlossaryCoverSteer(ESOLSteerIcon::Free) },
        { TEXT("Mouse-steer icon"), GlossaryCoverSteer(ESOLSteerIcon::Held) },
        { TEXT("Mouse-steer icon"), GlossaryCoverSteer(ESOLSteerIcon::Locked) },
        { TEXT("Flight mode"), GlossaryCoverLine(GlossaryLine::Assist) },
        { TEXT("Flight mode"), GlossaryCoverMode(ESOLFlightMode::Manual) },
        { TEXT("Flight mode"), GlossaryCoverMode(ESOLFlightMode::Assisted) },
        { TEXT("Flight mode"), GlossaryCoverMode(ESOLFlightMode::Auto) },
        { TEXT("Speed card"), GlossaryCoverLine(GlossaryLine::Speed) },
        { TEXT("Speed card"), GlossaryCoverLine(GlossaryLine::Frame) },
        { TEXT("Speed card"), GlossaryCoverLine(GlossaryLine::AbsoluteSpeed) },
        { TEXT("Speed card"), GlossaryCoverLine(GlossaryLine::Cap) },
        { TEXT("Speed card"), GlossaryCoverCard(GlossaryCard::Speed) },
        { TEXT("G meter"), GlossaryCoverLine(GlossaryLine::GMeter) },
        { TEXT("G meter"), GlossaryCoverLine(GlossaryLine::GWarning) },
        { TEXT("Date and anchor card"), GlossaryCoverLine(GlossaryLine::DateWarp) },
        { TEXT("Date and anchor card"), GlossaryCoverLine(GlossaryLine::Anchor) },
        { TEXT("Date and anchor card"), GlossaryCoverLine(GlossaryLine::Nearest) },
        { TEXT("Date and anchor card"), GlossaryCoverLine(GlossaryLine::SurfaceLock) },
        { TEXT("Date and anchor card"), GlossaryCoverCard(GlossaryCard::DateAnchor) },
        { TEXT("Key hints and notices"), GlossaryCoverLine(GlossaryLine::Hints) },
        { TEXT("Key hints and notices"), GlossaryCoverLine(GlossaryLine::SteerHint) },
        { TEXT("Key hints and notices"), GlossaryCoverLine(GlossaryLine::ControlsNotice) },
        { TEXT("Autopilot marker"), GlossaryCoverLine(GlossaryLine::Autopilot) },
        { TEXT("Autopilot marker"), GlossaryCoverLine(GlossaryLine::AutopilotHint) },
        { TEXT("Autopilot marker"), GlossaryCoverLine(GlossaryLine::AutopilotCancel) },
        { TEXT("Chase Down line"), GlossaryCoverLine(GlossaryLine::ChaseActivity) },
        { TEXT("Target bracket and name"), GlossaryCoverLine(GlossaryLine::TargetLabel) },
        { TEXT("Target card"), GlossaryCoverLine(GlossaryLine::TargetName) },
        { TEXT("Target card"), GlossaryCoverLine(GlossaryLine::TargetDistance) },
        { TEXT("Target card"), GlossaryCoverLine(GlossaryLine::TargetRelative) },
        { TEXT("Target card"), GlossaryCoverCard(GlossaryCard::Target) },
        { TEXT("Radar scope"), GlossaryCoverLine(GlossaryLine::RadarRange) },
        { TEXT("Selected target on the radar"), GlossaryCoverLine(GlossaryLine::RadarSelected) },
        { TEXT("Star icon"), GlossaryCoverIcon(GlossaryIcon::Star) },
        { TEXT("Planet icon"), GlossaryCoverIcon(GlossaryIcon::Planet) },
        { TEXT("Moon icon"), GlossaryCoverIcon(GlossaryIcon::Moon) },
        { TEXT("Dwarf planet icon"), GlossaryCoverIcon(GlossaryIcon::Dwarf) },
        { TEXT("Asteroid icon"), GlossaryCoverIcon(GlossaryIcon::Asteroid) },
        { TEXT("Ring system icon"), GlossaryCoverIcon(GlossaryIcon::Ring) },
        { TEXT("NPC ship icon"), GlossaryCoverIcon(GlossaryIcon::Ship) },
        { TEXT("Practice target icon"), GlossaryCoverIcon(GlossaryIcon::Target) },
        { TEXT("Overlay profile tag"), GlossaryCoverLine(GlossaryLine::OverlayProfileTag) },
        { TEXT("Route progress line"), GlossaryCoverLine(GlossaryLine::Route) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::InteractHint) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialTitle) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialReason) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem0) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem1) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem2) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem3) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem4) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem5) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem6) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem7) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem8) },
        { TEXT("Icon hover and radial menu"), GlossaryCoverLine(GlossaryLine::RadialItem9) },
        { TEXT("Info card"), GlossaryCoverLine(GlossaryLine::InfoTitle) },
        { TEXT("Info card"), GlossaryCoverLine(GlossaryLine::InfoType) },
        { TEXT("Info card"), GlossaryCoverLine(GlossaryLine::InfoDistance) },
        { TEXT("Info card"), GlossaryCoverLine(GlossaryLine::InfoDetail) },
        { TEXT("Info card"), GlossaryCoverLine(GlossaryLine::InfoStatus) },
        { TEXT("Info card"), GlossaryCoverCard(GlossaryCard::Info) },
        { TEXT("Map destination pick"), GlossaryCoverLine(GlossaryLine::MapPickHints) },
    };

    constexpr int32 GLOSSARY_ENTRY_COUNT = UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_ENTRIES);
    constexpr int32 GLOSSARY_COVERAGE_ITEM_COUNT = UE_ARRAY_COUNT(GLOSSARY_COVERAGE_TABLE);

    //////////////////////////////////////////////////////////////////////////
    // Returns the GLOSSARY_ENTRIES row with a name (case-sensitive), or INDEX_NONE, at compile time
    constexpr int32 GlossaryEntryOfName(const TCHAR* name)
    {
        for (int32 entry = 0; entry < GLOSSARY_ENTRY_COUNT; ++entry)
        {
            if (SOLMenuText::TextEquals(SOLMenuText::GLOSSARY_ENTRIES[entry].Name, name))
            {
                return entry;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether every GLOSSARY_COVERAGE_TABLE row names a GLOSSARY_ENTRIES row and the rows are in table order
    constexpr bool GlossaryIsCoverageTableValid()
    {
        int32 previous = 0;
        for (const FGlossaryCoverageRow& row : GLOSSARY_COVERAGE_TABLE)
        {
            const int32 entry = GlossaryEntryOfName(row.EntryName);
            if (entry == INDEX_NONE || entry < previous)
            {
                return false;
            }
            previous = entry;
        }
        return true;
    }

    static_assert(GlossaryIsCoverageTableValid(),
        "every GLOSSARY_COVERAGE_TABLE row names a GLOSSARY_ENTRIES row (by its Name), in GLOSSARY_ENTRIES order");

    // The value counts of the coverage kinds' enums that are not ESOLHudLine, EIconCategory or ESOLHudCard
    constexpr int32 GLOSSARY_STEER_ICON_COUNT = static_cast<int32>(ESOLSteerIcon::Count);
    constexpr int32 GLOSSARY_FLIGHT_MODE_COUNT = static_cast<int32>(ESOLFlightMode::Count);

    // GLOSSARY_COVERAGE_TABLE's items as one flat list and each entry's first item in it (entry i owns First[i] ..
    // First[i + 1]), so EntryCoverage is a view into it, built at compile time
    struct FGlossaryCoverageIndex
    {
        FSOLGlossaryCoverageItem Items[GLOSSARY_COVERAGE_ITEM_COUNT];
        int32 First[GLOSSARY_ENTRY_COUNT + 1];
    };

    //////////////////////////////////////////////////////////////////////////
    // Builds the flat coverage list and each entry's range in it from GLOSSARY_COVERAGE_TABLE (rows in entry order)
    constexpr FGlossaryCoverageIndex GlossaryMakeCoverageIndex()
    {
        FGlossaryCoverageIndex index{};
        int32 row = 0;
        for (int32 entry = 0; entry <= GLOSSARY_ENTRY_COUNT; ++entry)
        {
            while (row < GLOSSARY_COVERAGE_ITEM_COUNT
                && GlossaryEntryOfName(GLOSSARY_COVERAGE_TABLE[row].EntryName) < entry)
            {
                ++row;
            }
            index.First[entry] = row;
        }
        for (int32 item = 0; item < GLOSSARY_COVERAGE_ITEM_COUNT; ++item)
        {
            index.Items[item] = GLOSSARY_COVERAGE_TABLE[item].Item;
        }
        return index;
    }

    constexpr FGlossaryCoverageIndex GLOSSARY_COVERAGE_INDEX = GlossaryMakeCoverageIndex();

    // The constant tokens of the glossary descriptions (SDD 33 section 8.5) and the HUD texts of the samples, each
    // built by the HUD's own formatter so a sample cannot drift from what the HUD shows
    enum class EGlossaryToken : uint8
    {
        InfoCardSeconds,
        RadarAutoFloor,
        RadarAutoFloorTarget,
        IconFadeStartPx,
        VelocityMarkerMinSpeed,
        ChaseSafetyMarginDefault,
        SustainedG,
        CombatG,
        OverlayProfileCount,
        FlightModeManual,
        FlightModeAssisted,
        FlightModeAuto,
        ChaseBurstSample,
        ChaseCoolSample,
        ChaseBrakeSample,
        OverlayProfileTagSample,
        Count,
    };

    // Each token's name as a description writes it between braces, in EGlossaryToken order
    constexpr const TCHAR* GLOSSARY_TOKEN_NAMES[] =
    {
        TEXT("InfoCardSeconds"), TEXT("RadarAutoFloor"), TEXT("RadarAutoFloorTarget"), TEXT("IconFadeStartPx"),
        TEXT("VelocityMarkerMinSpeed"), TEXT("ChaseSafetyMarginDefault"), TEXT("SustainedG"), TEXT("CombatG"),
        TEXT("OverlayProfileCount"), TEXT("FlightModeManual"), TEXT("FlightModeAssisted"), TEXT("FlightModeAuto"),
        TEXT("ChaseBurstSample"), TEXT("ChaseCoolSample"), TEXT("ChaseBrakeSample"), TEXT("OverlayProfileTagSample"),
    };
    static_assert(UE_ARRAY_COUNT(GLOSSARY_TOKEN_NAMES) == static_cast<int32>(EGlossaryToken::Count),
        "one name per glossary token");

    // A resume meter past a full one: the Chase Down sample's cool off shows no "resume at" (IsChaseResumeShown)
    constexpr double GLOSSARY_SAMPLE_NO_RESUME_METER = 2.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns a distance in whole kilometres with grouped digits and its unit ("10,000 km")
    FString GlossaryKilometres(const double meters)
    {
        return FString::Format(SOLMenuText::INFO_KM_FORMAT,
            { SOLNpcStatsText::FormatGroupedInteger(FMath::RoundToInt64(meters / SOL::METERS_PER_KM)) });
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the chase marker's activity line as the HUD writes it (SOLHudFormat::AppendChaseActivity), no resume
    FString GlossaryChaseLine(const ESOLChaseActivity activity, const double limitG)
    {
        FString line;
        SOLHudFormat::AppendChaseActivity(line, activity, limitG, GLOSSARY_SAMPLE_NO_RESUME_METER);
        return line;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the overlay profile tag as the HUD writes it (HUD_OVERLAY_PROFILE_TAG_FORMAT, its number token swapped
    // for the sample profile's 1-based number)
    FString GlossaryOverlayProfileTag()
    {
        FString tag(SOLMenuText::HUD_OVERLAY_PROFILE_TAG_FORMAT);
        tag.ReplaceInline(SOLMenuText::OVERLAY_PROFILE_NUMBER_TOKEN,
            *FString::FromInt(SOL::Help::GLOSSARY_SAMPLE_OVERLAY_PROFILE), ESearchCase::CaseSensitive);
        return tag;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a token's text from the same constants and formatters the HUD and the Info page use
    FString GlossaryTokenText(const EGlossaryToken token)
    {
        switch (token)
        {
        case EGlossaryToken::InfoCardSeconds:
            return SOLInfoText::FormatPercentOrValue(SOL::Interact::INFO_CARD_S);
        case EGlossaryToken::RadarAutoFloor:
            return GlossaryKilometres(SOLRadar::DefaultFloorM);
        case EGlossaryToken::RadarAutoFloorTarget:
            return GlossaryKilometres(SOLRadar::TargetedFloorM);
        case EGlossaryToken::IconFadeStartPx:
            return SOLInfoText::FormatPercentOrValue(SOL::Overlay::ICON_FADE_START_PX);
        case EGlossaryToken::VelocityMarkerMinSpeed:
            return SOLInfoText::FormatPercentOrValue(SOL::Hologram::HUD_VELOCITY_MARKER_MIN_MPS);
        case EGlossaryToken::ChaseSafetyMarginDefault:
            // A fraction of the meter, shown as whole percent (the description says "percent")
            return SOLInfoText::FormatPercentOrValue(FMath::RoundToDouble(
                100.0 * static_cast<double>(SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT)));
        case EGlossaryToken::SustainedG:
            return SOLInfoText::FormatPercentOrValue(SOL::PILOT_SUSTAINED_G);
        case EGlossaryToken::CombatG:
            return SOLInfoText::FormatPercentOrValue(SOL::PILOT_MAX_G);
        case EGlossaryToken::OverlayProfileCount:
            return FString::FromInt(SOL::Overlay::MAX_PROFILES);
        case EGlossaryToken::FlightModeManual:
            return SOLHudFormat::FlightModeLabel(ESOLFlightMode::Manual);
        case EGlossaryToken::FlightModeAssisted:
            return SOLHudFormat::FlightModeLabel(ESOLFlightMode::Assisted);
        case EGlossaryToken::FlightModeAuto:
            return SOLHudFormat::FlightModeLabel(ESOLFlightMode::Auto);
        case EGlossaryToken::ChaseBurstSample:
            return GlossaryChaseLine(ESOLChaseActivity::Burst, SOL::Help::GLOSSARY_SAMPLE_CHASE_BURST_G);
        case EGlossaryToken::ChaseCoolSample:
            return GlossaryChaseLine(ESOLChaseActivity::Cool, SOL::PILOT_SUSTAINED_G);
        case EGlossaryToken::ChaseBrakeSample:
            return GlossaryChaseLine(ESOLChaseActivity::BrakeBurst, SOL::Help::GLOSSARY_SAMPLE_CHASE_BURST_G);
        case EGlossaryToken::OverlayProfileTagSample:
            return GlossaryOverlayProfileTag();
        default:
            return FString();
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the constant token a name between braces is, or Count
    EGlossaryToken GlossaryTokenFromName(const FStringView name)
    {
        for (int32 token = 0; token < static_cast<int32>(EGlossaryToken::Count); ++token)
        {
            if (name.Equals(GLOSSARY_TOKEN_NAMES[token], ESearchCase::CaseSensitive))
            {
                return static_cast<EGlossaryToken>(token);
            }
        }
        return EGlossaryToken::Count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a Help tab is a real tab
    bool GlossaryIsValidTab(const ESOLHelpTab tab)
    {
        return static_cast<int32>(tab) < static_cast<int32>(ESOLHelpTab::Count);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of Help tabs
int32 SOLGlossary::TabCount()
{
    return static_cast<int32>(ESOLHelpTab::Count);
}

//////////////////////////////////////////////////////////////////////////
// Returns a tab's short rail label ("" for an invalid tab)
const TCHAR* SOLGlossary::TabLabel(const ESOLHelpTab tab)
{
    return GlossaryIsValidTab(tab) ? SOLMenuText::HELP_TAB_LABELS[static_cast<int32>(tab)] : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Returns a tab's full heading ("" for an invalid tab)
const TCHAR* SOLGlossary::TabHeading(const ESOLHelpTab tab)
{
    return GlossaryIsValidTab(tab) ? SOLMenuText::HELP_TAB_HEADINGS[static_cast<int32>(tab)] : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Returns the group caption above a tab: KEYS above Keyboard, HUD GLOSSARY above Flight HUD, "" for the others
const TCHAR* SOLGlossary::TabCaption(const ESOLHelpTab tab)
{
    switch (tab)
    {
    case ESOLHelpTab::Keyboard:
        return SOLMenuText::HELP_CAPTION_KEYS;
    case ESOLHelpTab::FlightHud:
        return SOLMenuText::HELP_CAPTION_GLOSSARY;
    default:
        return TEXT("");
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the glossary tab of a GLOSSARY_TABS full name (case-sensitive), or Count
ESOLHelpTab SOLGlossary::TabFromGlossaryName(const TCHAR* tabName)
{
    if (tabName == nullptr)
    {
        return ESOLHelpTab::Count;
    }
    for (int32 tab = 0; tab < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_TABS)); ++tab)
    {
        if (FCString::Strcmp(tabName, SOLMenuText::GLOSSARY_TABS[tab]) == 0)
        {
            return static_cast<ESOLHelpTab>(tab + static_cast<int32>(ESOLHelpTab::FlightHud));
        }
    }
    return ESOLHelpTab::Count;
}

//////////////////////////////////////////////////////////////////////////
// Returns the symbol of a glossary id, unset for an unknown id
TOptional<ESOLHudSymbol> SOLGlossary::ResolveSymbol(const FStringView id)
{
    ESOLHudSymbol symbol = ESOLHudSymbol::Count;
    return SOLHudSymbols::FromName(id, symbol) ? TOptional<ESOLHudSymbol>(symbol) : TOptional<ESOLHudSymbol>();
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of glossary rows
int32 SOLGlossary::EntryCount()
{
    return static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_ENTRIES));
}

//////////////////////////////////////////////////////////////////////////
// Resolves a glossary row: its tab and each '|' symbol id of its Symbols cell (empty ids skipped)
bool SOLGlossary::ResolveEntry(const int32 index, FSOLGlossaryEntry& out)
{
    out = FSOLGlossaryEntry();
    if (index < 0 || index >= EntryCount())
    {
        return false;
    }
    const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
    out.Index = index;
    out.Tab = TabFromGlossaryName(row.Tab);

    // Walk the '|' list in place (no allocation, empty ids skipped); an unknown id or one past the slots leaves the
    // symbols unresolved
    bool bSymbolsResolved = true;
    UE::String::ParseTokens(FStringView(row.Symbols != nullptr ? row.Symbols : TEXT("")),
        SOLMenuText::GLOSSARY_LIST_DELIMITER, [&](const FStringView id)
        {
            const TOptional<ESOLHudSymbol> symbol = ResolveSymbol(id);
            if (!symbol.IsSet() || out.SymbolCount >= SOL::Help::GLOSSARY_MAX_SYMBOLS_PER_ENTRY)
            {
                bSymbolsResolved = false;
                return;
            }
            out.Symbols[out.SymbolCount++] = symbol.GetValue();
        }, UE::String::EParseTokensOptions::SkipEmpty);
    out.bSymbolsResolved = bSymbolsResolved;
    return out.Tab != ESOLHelpTab::Count && bSymbolsResolved;
}

//////////////////////////////////////////////////////////////////////////
// Lists the glossary rows of a tab in table order (none for Keyboard and an invalid tab)
void SOLGlossary::EntriesOfTab(const ESOLHelpTab tab, TArray<int32>& outIndices)
{
    outIndices.Reset();
    if (tab == ESOLHelpTab::Keyboard || !GlossaryIsValidTab(tab))
    {
        return;
    }
    for (int32 index = 0; index < EntryCount(); ++index)
    {
        if (TabFromGlossaryName(SOLMenuText::GLOSSARY_ENTRIES[index].Tab) == tab)
        {
            outIndices.Add(index);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Formats a description: the live keys first, then each known constant token; an unknown token stays as written
void SOLGlossary::FormatEntryText(const TCHAR* text, const TArray<FString>& liveKeys, FString& out)
{
    out.Reset();
    const FString keyed = SOLKeyBindings::FormatKeyText(text != nullptr ? text : TEXT(""), liveKeys);

    // Copy the text, swapping each '{<Token>}' that names a constant for its value
    int32 at = 0;
    while (at < keyed.Len())
    {
        const int32 open = keyed.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, at);
        const int32 close = open != INDEX_NONE
            ? keyed.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, open + 1) : INDEX_NONE;
        if (close == INDEX_NONE)
        {
            out.Append(*keyed + at, keyed.Len() - at);
            break;
        }
        const EGlossaryToken token = GlossaryTokenFromName(FStringView(*keyed + open + 1, close - open - 1));
        if (token == EGlossaryToken::Count)
        {
            // Not a constant: keep the brace and go on after it
            out.Append(*keyed + at, open + 1 - at);
            at = open + 1;
            continue;
        }
        out.Append(*keyed + at, open - at);
        out += GlossaryTokenText(token);
        at = close + 1;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD ids a glossary row explains: its range of the flat coverage list
TConstArrayView<FSOLGlossaryCoverageItem> SOLGlossary::EntryCoverage(const int32 index)
{
    if (index < 0 || index >= GLOSSARY_ENTRY_COUNT)
    {
        return TConstArrayView<FSOLGlossaryCoverageItem>();
    }
    const int32 first = GLOSSARY_COVERAGE_INDEX.First[index];
    return TConstArrayView<FSOLGlossaryCoverageItem>(GLOSSARY_COVERAGE_INDEX.Items + first,
        GLOSSARY_COVERAGE_INDEX.First[index + 1] - first);
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of values of a coverage kind (its enum's value count; 0 for Count)
int32 SOLGlossary::CoverageValueCount(const ESOLGlossaryCoverage kind)
{
    switch (kind)
    {
    case ESOLGlossaryCoverage::HudLine:
        return static_cast<int32>(ESOLHudLine::Count);
    case ESOLGlossaryCoverage::IconCategory:
        return static_cast<int32>(SOLOverlayIcons::EIconCategory::Count);
    case ESOLGlossaryCoverage::HudCard:
        return static_cast<int32>(ESOLHudCard::Count);
    case ESOLGlossaryCoverage::SteerIcon:
        return GLOSSARY_STEER_ICON_COUNT;
    case ESOLGlossaryCoverage::FlightMode:
        return GLOSSARY_FLIGHT_MODE_COUNT;
    default:
        return 0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an entry explains a HUD id (a scan of the coverage list; false for a value out of range)
bool SOLGlossary::IsCovered(const ESOLGlossaryCoverage kind, const int32 value)
{
    if (value < 0 || value >= CoverageValueCount(kind))
    {
        return false;
    }
    for (const FSOLGlossaryCoverageItem& item : GLOSSARY_COVERAGE_INDEX.Items)
    {
        if (item.Kind == kind && static_cast<int32>(item.Value) == value)
        {
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an entry explains a HUD text line
bool SOLGlossary::IsCovered(const ESOLHudLine line)
{
    return IsCovered(ESOLGlossaryCoverage::HudLine, static_cast<int32>(line));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an entry explains an overlay icon category
bool SOLGlossary::IsCovered(const SOLOverlayIcons::EIconCategory category)
{
    return IsCovered(ESOLGlossaryCoverage::IconCategory, static_cast<int32>(category));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an entry explains a mouse-steer icon
bool SOLGlossary::IsCovered(const ESOLSteerIcon icon)
{
    return IsCovered(ESOLGlossaryCoverage::SteerIcon, static_cast<int32>(icon));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an entry explains a flight mode
bool SOLGlossary::IsCovered(const ESOLFlightMode mode)
{
    return IsCovered(ESOLGlossaryCoverage::FlightMode, static_cast<int32>(mode));
}

// ---- (33d-ii) The symbol box's layout and the symbol view's rebuild rule ----

namespace
{
    // The most '|' parts and wrapped words one sample is split into without a heap allocation (a sample is a few
    // short HUD lines; more still works, on the heap)
    constexpr int32 GLOSSARY_SAMPLE_INLINE_PARTS = 8;

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a table cell holds any text (null reads empty)
    bool GlossaryHasText(const TCHAR* text)
    {
        return text != nullptr && text[0] != TEXT('\0');
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a value is a positive finite number
    bool GlossaryIsPositiveFinite(const float value)
    {
        return FMath::IsFinite(value) && value > 0.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Splits a sample on the list delimiter into its parts, each trimmed, the empty ones dropped (out reset first)
    void GlossarySplitParts(const FStringView sample,
        TArray<FStringView, TInlineAllocator<GLOSSARY_SAMPLE_INLINE_PARTS>>& outParts)
    {
        outParts.Reset();
        int32 partStart = 0;
        for (int32 at = 0; at <= sample.Len(); ++at)
        {
            if (at == sample.Len() || sample[at] == SOLMenuText::GLOSSARY_LIST_DELIMITER)
            {
                const FStringView part = sample.Mid(partStart, at - partStart).TrimStartAndEnd();
                if (!part.IsEmpty())
                {
                    outParts.Add(part);
                }
                partStart = at + 1;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds a part's lines to out, wrapped greedily at runs of spaces so each line's measure x scale stays within
    // width where it can (a single word wider than width stays whole on its own line); the run where a line ends is
    // dropped
    void GlossaryWrapPart(const FStringView part, const float width, const float scale,
        const TFunctionRef<float(FStringView)> measureAtUnit, TArray<FStringView,
        TInlineAllocator<GLOSSARY_SAMPLE_INLINE_PARTS>>& outLines)
    {
        const int32 length = part.Len();
        int32 lineStart = 0;
        int32 lineEnd = 0;
        int32 at = 0;
        bool bLineOpen = false;
        while (at < length)
        {
            // The next word and the run of spaces before it
            int32 wordStart = at;
            while (wordStart < length && part[wordStart] == TEXT(' '))
            {
                ++wordStart;
            }
            if (wordStart >= length)
            {
                break;
            }
            int32 wordEnd = wordStart;
            while (wordEnd < length && part[wordEnd] != TEXT(' '))
            {
                ++wordEnd;
            }
            at = wordEnd;

            // A line starts with a word; the next word joins it (with its run) while the line still fits
            if (!bLineOpen)
            {
                lineStart = wordStart;
                lineEnd = wordEnd;
                bLineOpen = true;
            }
            else if (measureAtUnit(part.Mid(lineStart, wordEnd - lineStart)) * scale <= width)
            {
                lineEnd = wordEnd;
            }
            else
            {
                outLines.Add(part.Mid(lineStart, lineEnd - lineStart));
                lineStart = wordStart;
                lineEnd = wordEnd;
            }
        }
        if (bLineOpen)
        {
            outLines.Add(part.Mid(lineStart, lineEnd - lineStart));
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns what a row's box shows: its symbols, else its screenshot crop, else its sample, else nothing
ESOLGlossaryArt SOLGlossary::EntryArt(const int32 index)
{
    if (index < 0 || index >= EntryCount())
    {
        return ESOLGlossaryArt::None;
    }
    const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
    if (GlossaryHasText(row.Symbols))
    {
        return ESOLGlossaryArt::Symbols;
    }
    if (EntryCrop(index) != INDEX_NONE)
    {
        return ESOLGlossaryArt::Crop;
    }
    return GlossaryHasText(row.Sample) ? ESOLGlossaryArt::Sample : ESOLGlossaryArt::None;
}

//////////////////////////////////////////////////////////////////////////
// Returns the near-square grid count symbols share (the smallest column count whose square holds them)
FSOLGlossaryCellGrid SOLGlossary::CellGrid(const int32 count)
{
    FSOLGlossaryCellGrid grid;
    if (count <= 0)
    {
        return grid;
    }
    grid.Columns = 1;
    while (static_cast<int64>(grid.Columns) * grid.Columns < count)
    {
        ++grid.Columns;
    }
    grid.Rows = (count + grid.Columns - 1) / grid.Columns;
    return grid;
}

//////////////////////////////////////////////////////////////////////////
// Returns a cell's rect: equal cells row-major, a short last row centred under the full rows
FBox2f SOLGlossary::CellRect(const int32 cell, const int32 count, const FBox2f& area)
{
    const FVector2f size = area.GetSize();
    if (count <= 0 || cell < 0 || cell >= count || !area.bIsValid || !GlossaryIsPositiveFinite(size.X)
        || !GlossaryIsPositiveFinite(size.Y))
    {
        return FBox2f(ForceInit);
    }
    const FSOLGlossaryCellGrid grid = CellGrid(count);
    const float cellWidth = size.X / grid.Columns;
    const float cellHeight = size.Y / grid.Rows;
    const int32 row = cell / grid.Columns;
    const int32 column = cell % grid.Columns;

    // The last row's cells (k of Columns) sit centred: shifted by half the room the missing cells leave
    const int32 lastRowCells = count - (grid.Rows - 1) * grid.Columns;
    const float offset = row == grid.Rows - 1 && lastRowCells < grid.Columns
        ? (grid.Columns - lastRowCells) * cellWidth * 0.5f : 0.0f;
    const FVector2f min(area.Min.X + offset + column * cellWidth, area.Min.Y + row * cellHeight);
    return FBox2f(min, min + FVector2f(cellWidth, cellHeight));
}

//////////////////////////////////////////////////////////////////////////
// Returns the scale that shrinks a symbol to fit its cell, never enlarging (0 for an invalid input)
float SOLGlossary::SymbolFitScale(const FBox2f& boundsAtMaxScale, const FVector2f& cellSizePx, const float maxScale)
{
    if (!GlossaryIsPositiveFinite(maxScale) || !GlossaryIsPositiveFinite(cellSizePx.X)
        || !GlossaryIsPositiveFinite(cellSizePx.Y) || !boundsAtMaxScale.bIsValid)
    {
        return 0.0f;
    }
    const FVector2f size = boundsAtMaxScale.GetSize();
    float factor = 1.0f;
    if (size.X > 0.0f)
    {
        factor = FMath::Min(factor, cellSizePx.X / size.X);
    }
    if (size.Y > 0.0f)
    {
        factor = FMath::Min(factor, cellSizePx.Y / size.Y);
    }
    return maxScale * factor;
}

//////////////////////////////////////////////////////////////////////////
// Returns the point to build a symbol at so its bounds are centred in the cell
FVector2f SOLGlossary::SymbolCentre(const FBox2f& boundsAtScale, const FBox2f& cell)
{
    const FVector2f cellCentre = cell.GetCenter();
    return boundsAtScale.bIsValid ? cellCentre - boundsAtScale.GetCenter() : cellCentre;
}

//////////////////////////////////////////////////////////////////////////
// Lays a sample's '|' parts out as lines centred in the area: at maxScale unwrapped, else wrapped at spaces, else
// shrunk down to minScale, else wrapped at minScale (D44)
void SOLGlossary::LayoutSample(const FStringView sample, const FVector2f& areaPx, const float maxScale,
    const float minScale, const float lineHeightAtUnit, const TFunctionRef<float(FStringView)> measureAtUnit,
    FSOLGlossarySampleLayout& out)
{
    out.Lines.Reset();
    out.Scale = 0.0f;
    if (!GlossaryIsPositiveFinite(areaPx.X) || !GlossaryIsPositiveFinite(areaPx.Y)
        || !GlossaryIsPositiveFinite(maxScale) || !GlossaryIsPositiveFinite(lineHeightAtUnit))
    {
        return;
    }

    // The parts: split on the delimiter, each trimmed, the empty ones dropped
    TArray<FStringView, TInlineAllocator<GLOSSARY_SAMPLE_INLINE_PARTS>> parts;
    GlossarySplitParts(sample, parts);
    if (parts.IsEmpty())
    {
        return;
    }

    // Wraps each part too wide at a scale, the others one line each
    TArray<FStringView, TInlineAllocator<GLOSSARY_SAMPLE_INLINE_PARTS>> lines;
    const auto wrapAt = [&parts, &areaPx, &measureAtUnit, &lines](const float atScale)
    {
        lines.Reset();
        for (const FStringView part : parts)
        {
            if (measureAtUnit(part) * atScale > areaPx.X)
            {
                GlossaryWrapPart(part, areaPx.X, atScale, measureAtUnit, lines);
            }
            else
            {
                lines.Add(part);
            }
        }
    };
    // Whether lines all fit the area at a scale, across and down
    const auto fitsAt = [&areaPx, &measureAtUnit, lineHeightAtUnit](const auto& candidate, const float atScale)
    {
        if (candidate.Num() * lineHeightAtUnit * atScale > areaPx.Y)
        {
            return false;
        }
        for (const FStringView line : candidate)
        {
            if (measureAtUnit(line) * atScale > areaPx.X)
            {
                return false;
            }
        }
        return true;
    };

    // D44, the first that fits: unwrapped at maxScale (A); wrapped at spaces at maxScale (B); shrunk unwrapped down
    // to the floor (C); else wrapped at the floor, the lines free to overflow the height (D)
    const float floorScale = GlossaryIsPositiveFinite(minScale) && minScale <= maxScale ? minScale : maxScale;
    float scale = maxScale;
    if (fitsAt(parts, maxScale))
    {
        lines = parts;
    }
    else
    {
        wrapAt(maxScale);
        if (!fitsAt(lines, maxScale))
        {
            float widest = 0.0f;
            for (const FStringView part : parts)
            {
                widest = FMath::Max(widest, measureAtUnit(part));
            }
            scale = FMath::Min(maxScale, areaPx.Y / (parts.Num() * lineHeightAtUnit));
            if (widest > 0.0f)
            {
                scale = FMath::Min(scale, areaPx.X / widest);
            }
            if (scale >= floorScale)
            {
                lines = parts;
            }
            else
            {
                scale = floorScale;
                wrapAt(floorScale);
            }
        }
    }

    // Each line centred across, the block centred down (either may be negative: the view clips)
    out.Scale = scale;
    const float lineHeight = lineHeightAtUnit * scale;
    const float top = (areaPx.Y - lines.Num() * lineHeight) * 0.5f;
    out.Lines.Reserve(lines.Num());
    for (int32 index = 0; index < lines.Num(); ++index)
    {
        FSOLGlossarySampleLine& line = out.Lines.AddDefaulted_GetRef();
        line.Text = FString(lines[index]);
        line.Position = FVector2f((areaPx.X - measureAtUnit(lines[index]) * scale) * 0.5f, top + index * lineHeight);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of '|' parts LayoutSample draws (trimmed, the empty ones dropped)
int32 SOLGlossary::SamplePartCount(const FStringView sample)
{
    TArray<FStringView, TInlineAllocator<GLOSSARY_SAMPLE_INLINE_PARTS>> parts;
    GlossarySplitParts(sample, parts);
    return parts.Num();
}

//////////////////////////////////////////////////////////////////////////
// Returns a text-sample row's box size in menu units (D44): the symbol box when no part wraps there, else the wide box
// as tall as the sample's lines need
FVector2f SOLGlossary::SampleBoxSize(const FStringView sample, const float maxScale, const float minScale,
    const float lineHeightAtUnit, const TFunctionRef<float(FStringView)> measureAtUnit)
{
    using namespace SOL::Help;
    const FVector2f square(GLOSSARY_SYMBOL_BOX);
    const int32 parts = SamplePartCount(sample);
    if (parts == 0 || !GlossaryIsPositiveFinite(maxScale) || !GlossaryIsPositiveFinite(lineHeightAtUnit))
    {
        return square;
    }

    // Short: no part wraps at maxScale in the symbol box's width (the lines wrapped there with room for every line a
    // view holds, never shrunk: only single words too wide for the box stay, and keep the box and its look)
    constexpr float inset = 2.0f * (GLOSSARY_SYMBOL_RECESS_PADDING + GLOSSARY_SYMBOL_PADDING);
    const float tallHeight = static_cast<float>(GLOSSARY_MAX_SAMPLE_LINES) * lineHeightAtUnit * maxScale;
    FSOLGlossarySampleLayout layout;
    LayoutSample(sample, FVector2f(GLOSSARY_SYMBOL_BOX - inset, tallHeight), maxScale, maxScale, lineHeightAtUnit,
        measureAtUnit, layout);
    if (layout.Lines.Num() <= parts)
    {
        return square;
    }

    // Wide: laid out in the wide box (narrowed by the slack) with room for every line a view holds
    const FVector2f area(GLOSSARY_SAMPLE_BOX_WIDTH - inset - GLOSSARY_SAMPLE_BOX_SLACK, tallHeight);
    LayoutSample(sample, area, maxScale, minScale, lineHeightAtUnit, measureAtUnit, layout);
    const float height = FMath::CeilToFloat(static_cast<float>(layout.Lines.Num()) * lineHeightAtUnit * layout.Scale
        + inset + GLOSSARY_SAMPLE_BOX_SLACK);
    return FVector2f(GLOSSARY_SAMPLE_BOX_WIDTH, FMath::Max(GLOSSARY_SYMBOL_BOX, height));
}

//////////////////////////////////////////////////////////////////////////
// Returns a view's size in pixels ((0, 0) for an invalid input)
FIntPoint SOLGlossary::ViewPixelSize(const FVector2f& localSize, const float layoutScale)
{
    if (!GlossaryIsPositiveFinite(layoutScale) || !GlossaryIsPositiveFinite(localSize.X)
        || !GlossaryIsPositiveFinite(localSize.Y))
    {
        return FIntPoint::ZeroValue;
    }
    return FIntPoint(FMath::RoundToInt(localSize.X * layoutScale), FMath::RoundToInt(localSize.Y * layoutScale));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a view must rebuild: laid out, and not built or built for another size or strength
bool SOLGlossary::ViewNeedsRebuild(const FSOLGlossaryViewStamp& stamp, const FIntPoint& sizePx, const float strength)
{
    if (sizePx.X <= 0 || sizePx.Y <= 0)
    {
        return false;
    }
    return !stamp.bBuilt || sizePx != stamp.SizePx
        || SOLSolidMenu::SanitizeStrength(strength) != stamp.Strength;
}

//////////////////////////////////////////////////////////////////////////
// Records a build at a size and (sanitized) strength
void SOLGlossary::MarkViewBuilt(FSOLGlossaryViewStamp& stamp, const FIntPoint& sizePx, const float strength)
{
    stamp.bBuilt = true;
    stamp.SizePx = sizePx;
    stamp.Strength = SOLSolidMenu::SanitizeStrength(strength);
}

//////////////////////////////////////////////////////////////////////////
// Returns a sample line's palette role: a flight mode's HUD label in a row that explains that mode takes the mode's
// colour role (SOLHudFormat::FlightModeRole, as the HUD draws it), every other line Primary
ESOLHudPaletteRole SOLGlossary::SampleLineRole(const int32 index, const FStringView line)
{
    // The flight modes the row covers: the one whose HUD label the line is, exactly
    for (const FSOLGlossaryCoverageItem& item : EntryCoverage(index))
    {
        if (item.Kind != ESOLGlossaryCoverage::FlightMode)
        {
            continue;
        }
        const ESOLFlightMode mode = static_cast<ESOLFlightMode>(item.Value);
        if (line.Equals(FStringView(SOLHudFormat::FlightModeLabel(mode)), ESearchCase::CaseSensitive))
        {
            return SOLHudFormat::FlightModeRole(mode);
        }
    }
    return ESOLHudPaletteRole::Primary;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of map-row screenshot crops
int32 SOLGlossary::CropCount()
{
    return UE_ARRAY_COUNT(SOL::Paths::GLOSSARY_CROPS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the GLOSSARY_CROPS index of an id (case-sensitive), INDEX_NONE for an empty or unknown id
int32 SOLGlossary::FindCrop(const FStringView id)
{
    if (id.IsEmpty())
    {
        return INDEX_NONE;
    }
    for (int32 crop = 0; crop < CropCount(); ++crop)
    {
        if (id.Equals(SOL::Paths::GLOSSARY_CROPS[crop].Id, ESearchCase::CaseSensitive))
        {
            return crop;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the crop index a row's Crop names, INDEX_NONE for none, an unknown id or an index outside the table
int32 SOLGlossary::EntryCrop(const int32 index)
{
    if (index < 0 || index >= EntryCount())
    {
        return INDEX_NONE;
    }
    const TCHAR* crop = SOLMenuText::GLOSSARY_ENTRIES[index].Crop;
    return crop != nullptr ? FindCrop(crop) : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the GLOSSARY_DESCRIPTION_ONLY reason of a row (by Name), nullptr when it is not listed or out of range
const TCHAR* SOLGlossary::DescriptionOnlyReason(const int32 index)
{
    if (index < 0 || index >= EntryCount())
    {
        return nullptr;
    }
    const FStringView name = SOLMenuText::GLOSSARY_ENTRIES[index].Name;
    for (const SOLMenuText::FGlossaryDescriptionOnly& row : SOLMenuText::GLOSSARY_DESCRIPTION_ONLY)
    {
        if (name.Equals(row.Name, ESearchCase::CaseSensitive))
        {
            return row.Reason;
        }
    }
    return nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns a crop's GLOSSARY_CROP_PX square in its capture shot, an all-zero rect for a crop outside the table
FIntRect SOLGlossary::CropRect(const int32 crop)
{
    if (crop < 0 || crop >= CropCount())
    {
        return FIntRect();
    }
    const SOL::Paths::FGlossaryCrop& row = SOL::Paths::GLOSSARY_CROPS[crop];
    return FIntRect(row.Left, row.Top, row.Left + SOL::Help::GLOSSARY_CROP_PX, row.Top + SOL::Help::GLOSSARY_CROP_PX);
}

//////////////////////////////////////////////////////////////////////////
// Returns the .uasset file of a crop's texture package, empty for a crop outside the table
FString SOLGlossary::CropAssetFile(const int32 crop)
{
    if (crop < 0 || crop >= CropCount())
    {
        return FString();
    }
    const FString package = FPackageName::ObjectPathToPackageName(FString(SOL::Paths::GLOSSARY_CROPS[crop].Texture));
    return FPackageName::LongPackageNameToFilename(package, FPackageName::GetAssetPackageExtension());
}
