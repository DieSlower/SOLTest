/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Npc/SOLNpcScaleMath.h"

// Pure-logic menu state machine (SDD 8): main menu, playing and the Esc pause menu, with the input-suspension and spawn
// rules the runtime menu subsystem applies; plus the Demo page's model (SDD 11, Part 10e)
namespace SOLMenuState
{
    enum class EMenuMode : uint8
    {
        MainMenu,
        Playing,
        Paused
    };

    enum class EMenuTab : uint8
    {
        Resume,
        Controls,
        Info,
        Settings,
        Demo,
        About,
        Quit
    };

    enum class EMenuEvent : uint8
    {
        PlayPressed,
        EscapePressed,
        ResumePressed,
        QuitToMenuPressed,
        MapOpened,
        MapClosed
    };

    struct FMenuState
    {
        EMenuMode Mode = EMenuMode::MainMenu;
        EMenuTab Tab = EMenuTab::Resume;
        bool bMapOpen = false;
    };

    // Returns the state the game starts in: the main menu
    SOLTEST_API FMenuState Initial();

    // Returns the state after an event (pure; unlisted combinations leave the state unchanged)
    SOLTEST_API FMenuState Apply(const FMenuState& state, EMenuEvent event);

    // Returns the state with the tab selected (only while a menu is showing)
    SOLTEST_API FMenuState SelectTab(const FMenuState& state, EMenuTab tab);

    // Returns whether ship flight input must be suspended
    SOLTEST_API bool SuspendsFlightInput(const FMenuState& state);

    // Returns whether the mouse cursor must be visible
    SOLTEST_API bool ShouldShowCursor(const FMenuState& state);

    // Returns whether the ship must be spawned on this transition (exactly main menu to playing)
    SOLTEST_API bool ShouldSpawnShip(const FMenuState& previous, const FMenuState& next);

    // ---- Demo page (SDD 11 section 3.8, Part 10e) ----

    // Demo page rows after the SOLNpcScale::EDemoOption rows
    enum class EDemoExtraRow : uint8
    {
        CombatModel,
        Seed,
        Ramp,
        Count
    };

    // Demo page presets
    enum class EDemoPreset : uint8
    {
        Showcase,
        Benchmark,
        Ramp,
        Count
    };

    // Everything the Demo page edits (session-only)
    struct FDemoPage
    {
        SOLNpcScale::FDemoOptions Options;                    // Default-constructed: the descriptor defaults
        int32 CombatModel = SOL::NPC_COMBAT_DEFAULT_MODEL;    // SOLNpcAi::ECombatModel as an int
        bool bRamp = false;                                   // Start runs the stress ramp up to Options.NpcCount
    };

    // Returns the Demo page's row count (every option, then the extra rows)
    SOLTEST_API int32 DemoRowCount();

    // Returns an extra row's index
    SOLTEST_API int32 DemoExtraRow(EDemoExtraRow extra);

    // Returns the Demo page at its defaults
    SOLTEST_API FDemoPage DefaultDemoPage();

    // Steps one row up or down (options by their step or preset, the combat model clamped, the seed wrapping, the
    // ramp toggling)
    SOLTEST_API FDemoPage StepDemoRow(const FDemoPage& page, int32 row, int32 direction);

    // Applies a preset (count, stats and ramp; everything else kept)
    SOLTEST_API FDemoPage ApplyDemoPreset(const FDemoPage& page, EDemoPreset preset);

    // Returns a row's value text
    SOLTEST_API FString FormatDemoValue(const FDemoPage& page, int32 row);
}
