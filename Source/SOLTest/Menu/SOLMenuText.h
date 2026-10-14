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

    // Settings page (8d)
    inline constexpr const TCHAR* SETTINGS_TITLE = TEXT("Settings");
    inline constexpr const TCHAR* SETTINGS_GRAPHICS = TEXT("Graphics");
    inline constexpr const TCHAR* SETTINGS_AUDIO = TEXT("Audio");
    inline constexpr const TCHAR* SETTINGS_MOUSE = TEXT("Mouse");
    inline constexpr const TCHAR* SETTINGS_GAMEPLAY = TEXT("Gameplay");
    inline constexpr const TCHAR* SETTINGS_KEYS = TEXT("Key bindings");
    inline constexpr const TCHAR* SETTINGS_RESOLUTION = TEXT("Resolution");
    inline constexpr const TCHAR* SETTINGS_WINDOW_MODE = TEXT("Window mode");
    inline constexpr const TCHAR* SETTINGS_QUALITY = TEXT("Quality preset");
    inline constexpr const TCHAR* SETTINGS_VSYNC = TEXT("VSync");
    inline constexpr const TCHAR* SETTINGS_MASTER_VOLUME = TEXT("Master volume");
    inline constexpr const TCHAR* SETTINGS_SFX_VOLUME = TEXT("Effects volume");
    inline constexpr const TCHAR* SETTINGS_MUSIC_VOLUME = TEXT("Music volume");
    inline constexpr const TCHAR* SETTINGS_MOUSE_SENSITIVITY = TEXT("Mouse sensitivity");
    inline constexpr const TCHAR* SETTINGS_INVERT_Y = TEXT("Invert mouse Y");
    inline constexpr const TCHAR* SETTINGS_HUD_SCALE = TEXT("HUD scale");
    inline constexpr const TCHAR* SETTINGS_HUD_VISIBLE = TEXT("Show flight HUD");
    inline constexpr const TCHAR* SETTINGS_DEFAULT_ASSIST = TEXT("Flight assist on at launch");
    inline constexpr const TCHAR* SETTINGS_APPLY = TEXT("Apply");
    inline constexpr const TCHAR* SETTINGS_REVERT = TEXT("Revert");
    inline constexpr const TCHAR* SETTINGS_RESET_KEYS = TEXT("Reset keys to defaults");
    inline constexpr const TCHAR* SETTINGS_REBIND = TEXT("Rebind");
    inline constexpr const TCHAR* SETTINGS_STEP_BACK = TEXT("<");
    inline constexpr const TCHAR* SETTINGS_STEP_FORWARD = TEXT(">");
    inline constexpr const TCHAR* SETTINGS_KEY_WAITING = TEXT("Press a key...");
    inline constexpr const TCHAR* SETTINGS_HINT = TEXT("Audio, mouse and HUD changes preview at once; graphics and key "
        "bindings take effect on Apply. Leaving this page discards changes that were not applied.");
    inline constexpr const TCHAR* SETTINGS_STATUS_CLEAN = TEXT("All changes applied.");
    inline constexpr const TCHAR* SETTINGS_STATUS_DIRTY = TEXT("Unapplied changes: Apply keeps them, Revert discards "
        "them.");
    inline constexpr const TCHAR* SETTINGS_REBIND_PROMPT_FORMAT = TEXT("Press the new key for \"{0}\" (Esc cancels).");
    inline constexpr const TCHAR* SETTINGS_REBIND_CANCELLED = TEXT("Rebind cancelled.");
    inline constexpr const TCHAR* SETTINGS_REBIND_REFUSED_FORMAT = TEXT("{0} cannot be bound: it is fixed to another "
        "control.");
    inline constexpr const TCHAR* SETTINGS_REBIND_DONE_FORMAT = TEXT("\"{0}\" set to {1}. Apply to use it.");
    inline constexpr const TCHAR* SETTINGS_REBIND_CONFLICT_FORMAT = TEXT("\"{0}\" set to {1}, which is also bound to "
        "\"{2}\": both will trigger until one is changed.");
    inline constexpr const TCHAR* SETTINGS_KEYS_RESET = TEXT("Keys reset to the defaults. Apply to use them.");
    inline constexpr const TCHAR* RESOLUTION_FORMAT = TEXT("{0} x {1}");
    inline constexpr const TCHAR* PERCENT_FORMAT = TEXT("{0}%");
    inline constexpr const TCHAR* MULTIPLIER_FORMAT = TEXT("{0}x");

    // Quality presets (index = scalability level) and window modes (index = EWindowMode::Type)
    inline constexpr const TCHAR* QUALITY_NAMES[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"),
        TEXT("Cinematic") };
    inline constexpr const TCHAR* WINDOW_MODE_NAMES[] = { TEXT("Fullscreen"), TEXT("Borderless window"),
        TEXT("Windowed") };

    // Rebindable ship actions (SOLKeyBindings::EBinding order): the area and label shown on the Settings page
    struct FKeyBindingText
    {
        const TCHAR* Area;
        const TCHAR* Label;
    };

    inline constexpr FKeyBindingText KEY_BINDING_TEXTS[] =
    {
        { TEXT("Flight"), TEXT("Thrust forward") },
        { TEXT("Flight"), TEXT("Thrust backward") },
        { TEXT("Flight"), TEXT("Strafe left") },
        { TEXT("Flight"), TEXT("Strafe right") },
        { TEXT("Flight"), TEXT("Thrust up") },
        { TEXT("Flight"), TEXT("Thrust down") },
        { TEXT("Flight"), TEXT("Roll left") },
        { TEXT("Flight"), TEXT("Roll right") },
        { TEXT("Flight"), TEXT("Boost (hold)") },
        { TEXT("Flight"), TEXT("Toggle flight assist") },
        { TEXT("Flight"), TEXT("Free-look (hold)") },
        { TEXT("Flight"), TEXT("Recenter the joystick") },
        { TEXT("Flight"), TEXT("Speed panel") },
        { TEXT("Targeting"), TEXT("Select target at reticle") },
        { TEXT("Targeting"), TEXT("Next farther target") },
        { TEXT("Targeting"), TEXT("Next nearer target") },
        { TEXT("Targeting"), TEXT("Clear the target") },
        { TEXT("Targeting"), TEXT("Lock frame to target") },
        { TEXT("Targeting"), TEXT("Surface-lock") },
        { TEXT("Combat"), TEXT("Fire (hold)") },
        { TEXT("Combat"), TEXT("Drop a practice target") },
        { TEXT("Time"), TEXT("Time warp down") },
        { TEXT("Time"), TEXT("Time warp up") },
        { TEXT("Time"), TEXT("Time warp to 1x") },
        { TEXT("HUD"), TEXT("Orbit lines") },
        { TEXT("HUD"), TEXT("Radar range in") },
        { TEXT("HUD"), TEXT("Radar range out") },
        { TEXT("HUD"), TEXT("Radar automatic range") },
        { TEXT("Jump map"), TEXT("Open or close the jump map") },
    };

    // Shown for a binding without a key
    inline constexpr const TCHAR* KEY_UNBOUND = TEXT("Unbound");

    // Flight HUD and speed panel texts that name a key. '{<BindingId>}' is that SOLKeyBindings action's current key
    // (SOLKeyBindings::FormatKeyText); the hint formats' '{N}' are filled by SOLHudFormat in the order it lists
    inline constexpr const TCHAR* HUD_HINT_ORBITS_ON_FORMAT = TEXT("ORBITS ON ({0})");
    inline constexpr const TCHAR* HUD_HINT_ORBITS_OFF_FORMAT = TEXT("ORBITS OFF ({0})");
    inline constexpr const TCHAR* HUD_HINT_KEYS_FORMAT = TEXT("   {0} speed panel   {1} jump map   {2}/{3}/{4}/{5} "
        "target   {6} match frame   {7} surface-lock   {8} {9} warp   {10} {11} radar zoom   {12} radar auto   {13} fire"
        "   {14} drop target");
    inline constexpr const TCHAR* HUD_HINT_MAP_FORMAT = TEXT("JUMP MAP   right drag orbit   middle or Shift+right drag "
        "pan   wheel zoom   {0} / Esc close");
    inline constexpr const TCHAR* HUD_SURFACE_LOCK_HINT_FORMAT = TEXT("{SurfaceLock} to surface-lock  ");
    inline constexpr const TCHAR* HUD_FRAME_LOCK_TAG_FORMAT = TEXT(" [{MatchLock} LOCK]");
    inline constexpr const TCHAR* HUD_NO_TARGET_FORMAT = TEXT("none ({SelectTarget})");
    inline constexpr const TCHAR* SPEED_PANEL_TITLE_FORMAT = TEXT("SPEED CAP  ({SpeedPanel} / Enter close, Esc close)");

    // Controls page
    inline constexpr const TCHAR* CONTROLS_TITLE = TEXT("Controls");
    inline constexpr const TCHAR* BINDING_ALTERNATIVE_SEPARATOR = TEXT(" / ");

    // One Controls row: its area heading, the bindings and what it does. '|' separates alternatives; an alternative
    // starting with '@' is a rebindable action's id (SOLKeyBindings) and shows that action's current key, any other
    // is a '+' chord of fixed engine key names. Every key is shown through SOLBindingLabel. An action's
    // '{DropDistance}' and '{SpeedCapNotches}' are filled from the gameplay constants (SOLMenuContent::
    // FormatControlAction). Keep in sync with GAME_MECHANICS.md section 1
    struct FControlRow
    {
        const TCHAR* Area;
        const TCHAR* Bindings;
        const TCHAR* Action;
    };

    inline constexpr const TCHAR* BINDING_ALTERNATIVE_DELIMITER = TEXT("|");
    inline constexpr const TCHAR* BINDING_CHORD_DELIMITER = TEXT("+");
    inline constexpr TCHAR BINDING_ID_PREFIX = TEXT('@');

    inline constexpr FControlRow CONTROL_ROWS[] =
    {
        { TEXT("Flight"), TEXT("Mouse2D"), TEXT("Steer: the mouse moves a virtual joystick (yaw and pitch)") },
        { TEXT("Flight"), TEXT("@RecenterStick"), TEXT("Recenter the joystick (stop turning)") },
        { TEXT("Flight"), TEXT("@ThrustForward|@ThrustBackward"), TEXT("Thrust forward / backward") },
        { TEXT("Flight"), TEXT("@StrafeLeft|@StrafeRight"), TEXT("Strafe left / right") },
        { TEXT("Flight"), TEXT("@ThrustUp|@ThrustDown"), TEXT("Thrust up / down") },
        { TEXT("Flight"), TEXT("@RollLeft|@RollRight"), TEXT("Roll left / right") },
        { TEXT("Flight"), TEXT("@Boost"), TEXT("Boost (hold)") },
        { TEXT("Flight"), TEXT("@ToggleAssist"),
            TEXT("Toggle flight assist (starts on unless turned off in Settings)") },
        { TEXT("Flight"), TEXT("@FreeLook"), TEXT("Free-look around the ship (hold)") },
        { TEXT("Flight"), TEXT("MouseWheelAxis"), TEXT("Step the speed cap ({SpeedCapNotches} notches per factor of ten)") },
        { TEXT("Flight"), TEXT("@SpeedPanel"), TEXT("Speed panel: set the cap digit by digit or from a body's speed") },
        { TEXT("Targeting"), TEXT("@SelectTarget"), TEXT("Select the target nearest the reticle") },
        { TEXT("Targeting"), TEXT("@NextTarget|@PreviousTarget"), TEXT("Next farther / next nearer target") },
        { TEXT("Targeting"), TEXT("@ClearTarget"), TEXT("Clear the target") },
        { TEXT("Targeting"), TEXT("@MatchLock"), TEXT("Lock the reference frame to the target (match its velocity)") },
        { TEXT("Targeting"), TEXT("@SurfaceLock"), TEXT("Surface-lock: keep the ship upright over the nearest body") },
        { TEXT("Combat"), TEXT("@Fire"), TEXT("Fire the pulse guns (hold)") },
        { TEXT("Combat"), TEXT("@DropTarget"), TEXT("Drop a practice target {DropDistance} ahead") },
        { TEXT("Time"), TEXT("@WarpDown|@WarpUp"), TEXT("Time warp down / up") },
        { TEXT("Time"), TEXT("@WarpReset"), TEXT("Time warp back to 1x") },
        { TEXT("HUD"), TEXT("@ToggleOrbitLines"), TEXT("Show or hide orbit lines") },
        { TEXT("HUD"), TEXT("@RadarZoomIn|@RadarZoomOut"), TEXT("Radar range in / out (manual)") },
        { TEXT("HUD"), TEXT("@RadarAuto"), TEXT("Radar back to automatic range") },
        { TEXT("Jump map"), TEXT("@ToggleMap"), TEXT("Open or close the jump map") },
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
    inline constexpr const TCHAR* INFO_SURFACE_LOCK_MANUAL = TEXT("Surface-lock with {SurfaceLock} from up to");
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

    // Info page combat lines (SOLInfoText::BuildCombatLines) and the text for a value that cannot be shown
    inline constexpr const TCHAR* INFO_BOLT_SPEED = TEXT("Bolt speed");
    inline constexpr const TCHAR* INFO_DAMAGE = TEXT("Damage per hit");
    inline constexpr const TCHAR* INFO_FIRE_RATE = TEXT("Fire rate");
    inline constexpr const TCHAR* INFO_TARGET_SHIELD = TEXT("Target shield");
    inline constexpr const TCHAR* INFO_TARGET_HEALTH = TEXT("Target health");
    inline constexpr const TCHAR* INFO_SHIELD_REGEN = TEXT("Shield regen");
    inline constexpr const TCHAR* INFO_RATE_FORMAT = TEXT("{0} /s");
    inline constexpr const TCHAR* INFO_REGEN_FORMAT = TEXT("{0} after {1} s");
    inline constexpr const TCHAR* INFO_NOT_AVAILABLE = TEXT("n/a");

    // Info page prose: how the mechanics behave (the numbers above stay generated; '{<BindingId>}' is that action's
    // current key, SOLKeyBindings::FormatKeyText)
    inline constexpr const TCHAR* INFO_PROSE[] =
    {
        TEXT("Everything is at true scale and on real orbits. The planets, dwarf planets and major moons move along "
            "their Keplerian orbits from today's date; the Sun lights the system, and a faint fill light along your "
            "view keeps the ship's shadowed side readable."),
        TEXT("Flight assist flies you relative to a reference frame: the nearest body, or the target you locked with "
            "{MatchLock}. It holds the velocity your keys ask for, up to the speed cap, and cancels drift and gravity. "
            "Turn it off ({ToggleAssist}) for pure Newtonian flight, where gravity pulls and the ship coasts."),
        TEXT("Time warp speeds up the planets, not your ship: the ship is carried along with its reference frame, so "
            "an orbit or a landing spot stays put at any warp."),
        TEXT("The jump map ({ToggleMap}) shows the whole system. Press on a body or on empty space, drag to set where "
            "you want to be relative to it, release, then press Enter to jump there."),
        TEXT("Bolts fly as real projectiles with your ship's velocity added. Targets have a shield that absorbs hits "
            "first and regenerates once they go a while without being hit."),
        TEXT("Jupiter, Saturn, Uranus and Neptune have banded cloud decks that turn with the planet, while each band "
            "also drifts at its own speed by latitude; storms such as Jupiter's Great Red Spot ride their bands. The "
            "drift follows time warp up to a cap, so the bands stay smooth at the highest warps. Fly close to a giant "
            "and a layer of volumetric clouds fades in just above the deck, replacing the thin haze seen from afar."),
        TEXT("Rings and planets cast real shadows. A ring darkens its planet's cloud deck by how dense it is, the "
            "planet's shadow falls across its rings, and rocks inside a ring are darker on its far side from the Sun. "
            "Your ship and targets are shadowed too: fly under a dense ring or into a planet's night-side shadow and "
            "the sunlight on them dims or goes out, leaving only the fill light. The edges are soft, as wide as the "
            "Sun's disc seen from there."),
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
