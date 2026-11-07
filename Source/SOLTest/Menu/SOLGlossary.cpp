/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLGlossary.h"

#include "Menu/SOLInfoText.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Npc/SOLNpcStatsText.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLRadarLayout.h"

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
