/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuState.h"

#include "Menu/SOLMenuText.h"
#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcStatsText.h"

namespace
{
    // Highest combat model index (SOLNpcAi::ECombatModel::Hybrid)
    constexpr int32 DEMO_MAX_COMBAT_MODEL = static_cast<int32>(SOLNpcAi::ECombatModel::Hybrid);

    // Preset NPC counts (SDD 11 section 3.8: Showcase 250k with battles, Benchmark and Ramp 1M)
    constexpr double DEMO_SHOWCASE_COUNT = 250000.0;
    constexpr double DEMO_SHOWCASE_BATTLE_SHIPS = 2000.0;
    constexpr double DEMO_BENCHMARK_COUNT = 1000000.0;

    // Distances below this many km show one decimal
    constexpr double DEMO_KM_DECIMAL_LIMIT = 10.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns whether an option is a distance in metres (shown in km)
    bool DemoIsDistance(const SOLNpcScale::EDemoOption id)
    {
        using SOLNpcScale::EDemoOption;
        return id == EDemoOption::BattleRadiusM || id == EDemoOption::BattleMinDistanceM
            || id == EDemoOption::BattleMaxDistanceM || id == EDemoOption::FullAiRadiusM
            || id == EDemoOption::MidAiRadiusM || id == EDemoOption::MeshRadiusM || id == EDemoOption::InstancedRadiusM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether an option is a time in seconds
    bool DemoIsSeconds(const SOLNpcScale::EDemoOption id)
    {
        return id == SOLNpcScale::EDemoOption::RampSettleS || id == SOLNpcScale::EDemoOption::RampMeasureS;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the starting state
SOLMenuState::FMenuState SOLMenuState::Initial()
{
    return FMenuState();
}

//////////////////////////////////////////////////////////////////////////
// Applies one event to the state
SOLMenuState::FMenuState SOLMenuState::Apply(const FMenuState& state, EMenuEvent event)
{
    FMenuState result = state;
    switch (event)
    {
    case EMenuEvent::PlayPressed:
        if (state.Mode == EMenuMode::MainMenu)
        {
            result.Mode = EMenuMode::Playing;
            result.bMapOpen = false;
        }
        break;
    case EMenuEvent::EscapePressed:
        if (state.Mode == EMenuMode::Playing && !state.bMapOpen)
        {
            result.Mode = EMenuMode::Paused;
            result.Tab = EMenuTab::Resume;
        }
        else if (state.Mode == EMenuMode::Paused)
        {
            result.Mode = EMenuMode::Playing;
        }
        break;
    case EMenuEvent::ResumePressed:
        if (state.Mode == EMenuMode::Paused)
        {
            result.Mode = EMenuMode::Playing;
        }
        break;
    case EMenuEvent::QuitToMenuPressed:
        if (state.Mode == EMenuMode::Paused)
        {
            result.Mode = EMenuMode::MainMenu;
            result.bMapOpen = false;
        }
        break;
    case EMenuEvent::MapOpened:
        if (state.Mode == EMenuMode::Playing)
        {
            result.bMapOpen = true;
        }
        break;
    case EMenuEvent::MapClosed:
        if (state.Mode == EMenuMode::Playing)
        {
            result.bMapOpen = false;
        }
        break;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Selects a tab while a menu is showing
SOLMenuState::FMenuState SOLMenuState::SelectTab(const FMenuState& state, EMenuTab tab)
{
    FMenuState result = state;
    if (state.Mode != EMenuMode::Playing)
    {
        result.Tab = tab;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether flight input is suspended
bool SOLMenuState::SuspendsFlightInput(const FMenuState& state)
{
    return state.Mode != EMenuMode::Playing;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the cursor is shown
bool SOLMenuState::ShouldShowCursor(const FMenuState& state)
{
    return state.Mode != EMenuMode::Playing || state.bMapOpen;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether this transition spawns the ship
bool SOLMenuState::ShouldSpawnShip(const FMenuState& previous, const FMenuState& next)
{
    return previous.Mode == EMenuMode::MainMenu && next.Mode == EMenuMode::Playing;
}

//////////////////////////////////////////////////////////////////////////
// Returns the Demo page's row count
int32 SOLMenuState::DemoRowCount()
{
    return static_cast<int32>(SOLNpcScale::EDemoOption::Count) + static_cast<int32>(EDemoExtraRow::Count);
}

//////////////////////////////////////////////////////////////////////////
// Returns an extra row's index
int32 SOLMenuState::DemoExtraRow(const EDemoExtraRow extra)
{
    return static_cast<int32>(SOLNpcScale::EDemoOption::Count) + static_cast<int32>(extra);
}

//////////////////////////////////////////////////////////////////////////
// Returns the Demo page at its defaults
SOLMenuState::FDemoPage SOLMenuState::DefaultDemoPage()
{
    FDemoPage page;
    page.Options = SOLNpcScale::DefaultOptions();
    page.CombatModel = SOL::NPC_COMBAT_DEFAULT_MODEL;
    page.bRamp = false;
    return page;
}

//////////////////////////////////////////////////////////////////////////
// Steps one row up or down
SOLMenuState::FDemoPage SOLMenuState::StepDemoRow(const FDemoPage& page, const int32 row, const int32 direction)
{
    FDemoPage result = page;
    if (direction == 0 || row < 0 || row >= DemoRowCount())
    {
        return result;
    }
    const int32 sign = direction > 0 ? 1 : -1;
    if (row < static_cast<int32>(SOLNpcScale::EDemoOption::Count))
    {
        result.Options = SOLNpcScale::StepOption(page.Options, static_cast<SOLNpcScale::EDemoOption>(row), sign);
        return result;
    }
    switch (static_cast<EDemoExtraRow>(row - static_cast<int32>(SOLNpcScale::EDemoOption::Count)))
    {
    case EDemoExtraRow::CombatModel:
        result.CombatModel = FMath::Clamp(FMath::Clamp(page.CombatModel, 0, DEMO_MAX_COMBAT_MODEL) + sign, 0,
            DEMO_MAX_COMBAT_MODEL);
        break;
    case EDemoExtraRow::Seed:
        result.Options.Seed = page.Options.Seed + static_cast<uint32>(sign);
        break;
    case EDemoExtraRow::Ramp:
        result.bRamp = !page.bRamp;
        break;
    default:
        break;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Applies a preset (count, stats and ramp; everything else kept)
SOLMenuState::FDemoPage SOLMenuState::ApplyDemoPreset(const FDemoPage& page, const EDemoPreset preset)
{
    using SOLNpcScale::EDemoOption;
    FDemoPage result = page;
    switch (preset)
    {
    case EDemoPreset::Showcase:
        result.Options = SOLNpcScale::SetOption(result.Options, EDemoOption::NpcCount, DEMO_SHOWCASE_COUNT);
        result.Options = SOLNpcScale::SetOption(result.Options, EDemoOption::BattleShipCount,
            DEMO_SHOWCASE_BATTLE_SHIPS);
        result.bRamp = false;
        break;
    case EDemoPreset::Benchmark:
        result.Options = SOLNpcScale::SetOption(result.Options, EDemoOption::NpcCount, DEMO_BENCHMARK_COUNT);
        result.bRamp = false;
        break;
    case EDemoPreset::Ramp:
        result.Options = SOLNpcScale::SetOption(result.Options, EDemoOption::NpcCount, DEMO_BENCHMARK_COUNT);
        result.bRamp = true;
        break;
    default:
        return result;
    }
    result.Options = SOLNpcScale::SetOption(result.Options, EDemoOption::ShowStats, 1.0);
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns a row's value text
FString SOLMenuState::FormatDemoValue(const FDemoPage& page, const int32 row)
{
    if (row < 0 || row >= DemoRowCount())
    {
        return FString();
    }
    if (row >= static_cast<int32>(SOLNpcScale::EDemoOption::Count))
    {
        switch (static_cast<EDemoExtraRow>(row - static_cast<int32>(SOLNpcScale::EDemoOption::Count)))
        {
        case EDemoExtraRow::CombatModel:
            return SOLMenuText::DEMO_COMBAT_MODEL_LABELS[static_cast<int32>(
                SOLNpcAi::CombatModelFromInt(page.CombatModel))];
        case EDemoExtraRow::Seed:
            return FString::Printf(TEXT("%u"), page.Options.Seed);
        case EDemoExtraRow::Ramp:
        default:
            return page.bRamp ? SOLMenuText::DEMO_VALUE_ON : SOLMenuText::DEMO_VALUE_OFF;
        }
    }

    // Option rows: by kind (bool, distance in km, seconds, integer, fraction)
    const SOLNpcScale::EDemoOption id = static_cast<SOLNpcScale::EDemoOption>(row);
    const SOLNpcScale::FOptionDesc& desc = SOLNpcScale::OptionDescriptors()[row];
    const double value = SOLNpcScale::GetOption(page.Options, id);
    if (desc.bBool)
    {
        return value != 0.0 ? SOLMenuText::DEMO_VALUE_ON : SOLMenuText::DEMO_VALUE_OFF;
    }
    if (DemoIsDistance(id))
    {
        const double km = value / 1000.0;
        return (km < DEMO_KM_DECIMAL_LIMIT ? FString::Printf(TEXT("%.1f"), km)
            : SOLNpcStatsText::FormatGroupedInteger(FMath::RoundToInt64(km))) + SOLMenuText::DEMO_UNIT_KM;
    }
    if (DemoIsSeconds(id))
    {
        return FString::Printf(TEXT("%.0f"), value) + SOLMenuText::DEMO_UNIT_SECONDS;
    }
    if (desc.bInteger)
    {
        return SOLNpcStatsText::FormatGroupedInteger(FMath::RoundToInt64(value));
    }
    return FString::Printf(TEXT("%.2f"), value);
}
