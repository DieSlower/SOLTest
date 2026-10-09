/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuState.h"

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
