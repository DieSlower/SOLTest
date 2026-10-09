/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic menu state machine (SDD 8): main menu, playing and the Esc pause menu, with the input-suspension and spawn
// rules the runtime menu subsystem applies.
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
}
