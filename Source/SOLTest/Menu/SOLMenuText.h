/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Every fixed player-facing string of the main and pause menus (SDD 8, 8c), kept in one place. Values computed from
// gameplay constants are formatted by SOLInfoText and the content builders, not written here.
namespace SOLMenuText
{
    // Titles
    inline constexpr const TCHAR* GAME_TITLE = TEXT("SOLTest");
    inline constexpr const TCHAR* GAME_TAGLINE = TEXT("The solar system at true scale");
    inline constexpr const TCHAR* STUDIO_NAME = TEXT("ACID RAIN STUDIOS LLC");
    inline constexpr const TCHAR* PAUSED_TITLE = TEXT("PAUSED");

    // Main menu entries
    inline constexpr const TCHAR* PLAY = TEXT("Play");
    inline constexpr const TCHAR* START_LOCATION_FORMAT = TEXT("Start: {0}");
    inline constexpr const TCHAR* CONTROLS = TEXT("Controls");
    inline constexpr const TCHAR* SETTINGS = TEXT("Settings");
    inline constexpr const TCHAR* ABOUT = TEXT("About");
    inline constexpr const TCHAR* QUIT = TEXT("Quit");
    inline constexpr const TCHAR* MAIN_MENU_HINT = TEXT("Enter: select    Tab / arrows: move    Esc: close page");

    // Pause menu tabs and pages
    inline constexpr const TCHAR* RESUME = TEXT("Resume");
    inline constexpr const TCHAR* INFO = TEXT("Info");
    inline constexpr const TCHAR* QUIT_TAB = TEXT("Quit");
    inline constexpr const TCHAR* PAUSED_BODY = TEXT("The simulation keeps running while this menu is open: your ship "
        "coasts and the planets keep moving. Flight controls are suspended until you resume.");
    inline constexpr const TCHAR* PAUSED_HINT = TEXT("Esc: resume    Tab / arrows: move    Enter: select");
    inline constexpr const TCHAR* QUIT_PROMPT = TEXT("Leave this flight?");
    inline constexpr const TCHAR* QUIT_TO_MENU = TEXT("Quit to main menu");
    inline constexpr const TCHAR* QUIT_TO_DESKTOP = TEXT("Quit to desktop");

    // Start-location page
    inline constexpr const TCHAR* START_LOCATION_TITLE = TEXT("Start location");
    inline constexpr const TCHAR* START_LOCATION_BODY = TEXT("Choose the body to start above. Play puts the ship in "
        "orbit on its sunlit side.");

    // Settings page (8d fills it)
    inline constexpr const TCHAR* SETTINGS_TITLE = TEXT("Settings");

    // Controls page
    inline constexpr const TCHAR* CONTROLS_TITLE = TEXT("Controls");
    inline constexpr const TCHAR* BINDING_ALTERNATIVE_SEPARATOR = TEXT(" / ");

    // One Controls row: its area heading, the bindings (engine key names; '|' separates alternatives, '+' joins a
    // chord; each name is shown through SOLBindingLabel) and what it does. Keep in sync with GAME_MECHANICS.md section 1
    struct FControlRow
    {
        const TCHAR* Area;
        const TCHAR* Bindings;
        const TCHAR* Action;
    };

    inline constexpr const TCHAR* BINDING_ALTERNATIVE_DELIMITER = TEXT("|");
    inline constexpr const TCHAR* BINDING_CHORD_DELIMITER = TEXT("+");

    inline constexpr FControlRow CONTROL_ROWS[] =
    {
        { TEXT("Flight"), TEXT("Mouse2D"), TEXT("Steer: the mouse moves a virtual joystick (yaw and pitch)") },
        { TEXT("Flight"), TEXT("MiddleMouseButton"), TEXT("Recenter the joystick (stop turning)") },
        { TEXT("Flight"), TEXT("W|S"), TEXT("Thrust forward / backward") },
        { TEXT("Flight"), TEXT("A|D"), TEXT("Strafe left / right") },
        { TEXT("Flight"), TEXT("SpaceBar|LeftControl"), TEXT("Thrust up / down") },
        { TEXT("Flight"), TEXT("Q|E"), TEXT("Roll left / right") },
        { TEXT("Flight"), TEXT("LeftShift"), TEXT("Boost (hold)") },
        { TEXT("Flight"), TEXT("Tab"), TEXT("Toggle flight assist (starts on)") },
        { TEXT("Flight"), TEXT("LeftAlt"), TEXT("Free-look around the ship (hold)") },
        { TEXT("Flight"), TEXT("MouseWheelAxis"), TEXT("Step the speed cap (10 notches per factor of ten)") },
        { TEXT("Flight"), TEXT("F3"), TEXT("Speed panel: set the cap digit by digit or from a body's speed") },
        { TEXT("Targeting"), TEXT("T"), TEXT("Select the target nearest the reticle") },
        { TEXT("Targeting"), TEXT("R|F"), TEXT("Next farther / next nearer target") },
        { TEXT("Targeting"), TEXT("X"), TEXT("Clear the target") },
        { TEXT("Targeting"), TEXT("M"), TEXT("Lock the reference frame to the target (match its velocity)") },
        { TEXT("Targeting"), TEXT("L"), TEXT("Surface-lock: keep the ship upright over the nearest body") },
        { TEXT("Combat"), TEXT("LeftMouseButton"), TEXT("Fire the pulse guns (hold)") },
        { TEXT("Combat"), TEXT("G"), TEXT("Drop a practice target 200 m ahead") },
        { TEXT("Time"), TEXT("LeftBracket|RightBracket"), TEXT("Time warp down / up") },
        { TEXT("Time"), TEXT("BackSpace"), TEXT("Time warp back to 1x") },
        { TEXT("HUD"), TEXT("O"), TEXT("Show or hide orbit lines") },
        { TEXT("HUD"), TEXT("Hyphen|Equals"), TEXT("Radar range in / out (manual)") },
        { TEXT("HUD"), TEXT("Home"), TEXT("Radar back to automatic range") },
        { TEXT("Jump map"), TEXT("J"), TEXT("Open or close the jump map") },
        { TEXT("Jump map"), TEXT("RightMouseButton"), TEXT("Orbit the map camera (drag)") },
        { TEXT("Jump map"), TEXT("MiddleMouseButton|LeftShift+RightMouseButton"), TEXT("Pan the map (drag)") },
        { TEXT("Jump map"), TEXT("MouseWheelAxis"), TEXT("Zoom the map") },
        { TEXT("Jump map"), TEXT("LeftMouseButton"), TEXT("Pick a destination: press on a body or space, drag, release") },
        { TEXT("Jump map"), TEXT("LeftShift|LeftShift+LeftMouseButton"),
            TEXT("Preview the height above the plane / lock it") },
        { TEXT("Jump map"), TEXT("X"), TEXT("Clear the destination") },
        { TEXT("Jump map"), TEXT("Enter"), TEXT("Jump to the destination") },
        { TEXT("Jump map"), TEXT("Escape"), TEXT("Close the map") },
        { TEXT("Menu"), TEXT("Escape"), TEXT("Pause menu (in flight) / resume") },
    };

    // Info page headings
    inline constexpr const TCHAR* INFO_TITLE = TEXT("Info");
    inline constexpr const TCHAR* INFO_FLIGHT = TEXT("Flight");
    inline constexpr const TCHAR* INFO_TIME = TEXT("Time and travel");
    inline constexpr const TCHAR* INFO_COMBAT = TEXT("Combat");
    inline constexpr const TCHAR* INFO_HOW_IT_WORKS = TEXT("How it works");

    // Info page labels (the values come from SOLConstants.h and the flight parameters)
    inline constexpr const TCHAR* INFO_SPEED_CAP_RANGE = TEXT("Speed cap range");
    inline constexpr const TCHAR* INFO_START_SPEED_CAP = TEXT("Starting speed cap");
    inline constexpr const TCHAR* INFO_ASSIST_RESPONSE = TEXT("Assist response time");
    inline constexpr const TCHAR* INFO_NEWTONIAN_ACCEL = TEXT("Newtonian thrust (main / lateral)");
    inline constexpr const TCHAR* INFO_BOOST = TEXT("Boost");
    inline constexpr const TCHAR* INFO_TURN_RATES = TEXT("Turn rate (pitch and yaw / roll)");
    inline constexpr const TCHAR* INFO_SHIP_RADIUS = TEXT("Ship collision radius");
    inline constexpr const TCHAR* INFO_WARP_STEPS = TEXT("Time warp steps");
    inline constexpr const TCHAR* INFO_JUMP_DURATION = TEXT("Jump warp duration");
    inline constexpr const TCHAR* INFO_SURFACE_LOCK_AUTO = TEXT("Surface-lock engages automatically within");
    inline constexpr const TCHAR* INFO_SURFACE_LOCK_MANUAL = TEXT("Surface-lock with L from up to");
    inline constexpr const TCHAR* INFO_BOOST_FORMAT = TEXT("x{0} speed (assist), x{1} thrust (Newtonian)");
    inline constexpr const TCHAR* INFO_RANGE_FORMAT = TEXT("{0} to {1}");
    inline constexpr const TCHAR* INFO_PAIR_FORMAT = TEXT("{0} / {1}");
    inline constexpr const TCHAR* INFO_SECONDS_FORMAT = TEXT("{0} s");
    inline constexpr const TCHAR* INFO_ACCEL_FORMAT = TEXT("{0} m/s²");
    inline constexpr const TCHAR* INFO_DEG_PER_S_FORMAT = TEXT("{0}°/s");
    inline constexpr const TCHAR* INFO_METERS_FORMAT = TEXT("{0} m");
    inline constexpr const TCHAR* INFO_KM_FORMAT = TEXT("{0} km");
    inline constexpr const TCHAR* INFO_MANUAL_RANGE_FORMAT = TEXT("{0} km or the body's radius, whichever is larger");
    inline constexpr const TCHAR* WARP_TIMES_FORMAT = TEXT("{0}x");
    inline constexpr const TCHAR* WARP_HOURS_FORMAT = TEXT("{0} h/s");
    inline constexpr const TCHAR* WARP_DAYS_FORMAT = TEXT("{0} d/s");
    inline constexpr const TCHAR* LIST_SEPARATOR = TEXT(", ");

    // Info page prose: how the mechanics behave (the numbers above stay generated)
    inline constexpr const TCHAR* INFO_PROSE[] =
    {
        TEXT("Everything is at true scale and on real orbits. The planets, dwarf planets and major moons move along "
            "their Keplerian orbits from today's date; the Sun is the only light."),
        TEXT("Flight assist flies you relative to a reference frame: the nearest body, or the target you locked with "
            "M. It holds the velocity your keys ask for, up to the speed cap, and cancels drift and gravity. Turn it "
            "off (Tab) for pure Newtonian flight, where gravity pulls and the ship coasts."),
        TEXT("Time warp speeds up the planets, not your ship: the ship is carried along with its reference frame, so "
            "an orbit or a landing spot stays put at any warp."),
        TEXT("The jump map (J) shows the whole system. Press on a body or on empty space, drag to set where you want "
            "to be relative to it, release, then press Enter to jump there."),
        TEXT("Bolts fly as real projectiles with your ship's velocity added. Targets have a shield that absorbs hits "
            "first and regenerates once they go a while without being hit."),
    };

    // About page
    inline constexpr const TCHAR* ABOUT_TITLE = TEXT("About");
    inline constexpr const TCHAR* ABOUT_DESCRIPTION = TEXT("A space flight simulation of the solar system at 1:1 "
        "scale, with real orbits, time warp, a jump map and ship combat.");
    inline constexpr const TCHAR* ABOUT_COPYRIGHT = TEXT("Copyright © 2026 Acid Rain Studios LLC. All rights reserved.");
    inline constexpr const TCHAR* ABOUT_ENGINE_FORMAT = TEXT("Built with Unreal Engine {0}");
    inline constexpr const TCHAR* ABOUT_CREDITS_TITLE = TEXT("Credits");
    inline constexpr const TCHAR* ABOUT_CREDITS = TEXT("Design and development: Acid Rain Studios LLC. Orbital elements: "
        "NASA JPL. Star catalogue: AT-HYG (CC BY-SA 4.0).");
}
