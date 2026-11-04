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
    inline constexpr const TCHAR* SETTINGS_OVERLAYS = TEXT("Overlays (what the overlay key turns on and off)");
    inline constexpr const TCHAR* SETTINGS_KEYS = TEXT("Key bindings");
    inline constexpr const TCHAR* SETTINGS_RESOLUTION = TEXT("Resolution");
    inline constexpr const TCHAR* SETTINGS_WINDOW_MODE = TEXT("Window mode");
    inline constexpr const TCHAR* SETTINGS_QUALITY = TEXT("Quality preset");
    inline constexpr const TCHAR* SETTINGS_VSYNC = TEXT("VSync");
    inline constexpr const TCHAR* SETTINGS_SPACE_DUST = TEXT("Space dust");
    inline constexpr const TCHAR* SETTINGS_MASTER_VOLUME = TEXT("Master volume");
    inline constexpr const TCHAR* SETTINGS_SFX_VOLUME = TEXT("Effects volume");
    inline constexpr const TCHAR* SETTINGS_MUSIC_VOLUME = TEXT("Music volume");
    inline constexpr const TCHAR* SETTINGS_MOUSE_SENSITIVITY = TEXT("Mouse sensitivity");
    inline constexpr const TCHAR* SETTINGS_INVERT_Y = TEXT("Invert mouse Y");
    inline constexpr const TCHAR* SETTINGS_HUD_SCALE = TEXT("HUD scale");
    inline constexpr const TCHAR* SETTINGS_HUD_EFFECTS = TEXT("HUD hologram effects");
    inline constexpr const TCHAR* SETTINGS_HUD_VISIBLE = TEXT("Show flight HUD");
    inline constexpr const TCHAR* SETTINGS_DEFAULT_ASSIST = TEXT("Flight assist on at launch");
    inline constexpr const TCHAR* SETTINGS_MOUSE_STEER_AT_START = TEXT("Mouse steering at start");
    inline constexpr const TCHAR* SETTINGS_CHASE_SAFETY_MARGIN = TEXT("Chase Down safety margin");
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
        { TEXT("Flight"), TEXT("Boost (toggle)") },
        { TEXT("Flight"), TEXT("Flight assist / manual") },
        { TEXT("Flight"), TEXT("Stop spin") },
        { TEXT("Flight"), TEXT("Steer with the mouse (hold)") },
        { TEXT("Flight"), TEXT("Mouse steering on / off") },
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
        { TEXT("Overlays"), TEXT("Overlays on / off") },
        { TEXT("Overlays"), TEXT("Orbit lines on / off") },
        { TEXT("Overlays"), TEXT("Overlay Menu") },
        { TEXT("Overlays"), TEXT("Cycle overlay profiles") },
        { TEXT("HUD"), TEXT("Radar range in") },
        { TEXT("HUD"), TEXT("Radar range out") },
        { TEXT("HUD"), TEXT("Radar automatic range") },
        { TEXT("Map"), TEXT("Open or close the map") },
        { TEXT("Map"), TEXT("Open the map in Jump mode") },
        { TEXT("Routes"), TEXT("Resume a paused route") },
        { TEXT("Routes"), TEXT("Show or hide routes") },
    };

    // Shown for a binding without a key
    inline constexpr const TCHAR* KEY_UNBOUND = TEXT("Unbound");

    // Flight HUD and speed panel texts that name a key. '{<BindingId>}' is that SOLKeyBindings action's current key
    // (SOLKeyBindings::FormatKeyText); the hint formats' '{N}' are filled by SOLHudFormat in the order it lists
    inline constexpr const TCHAR* HUD_HINT_ORBITS_ON_FORMAT = TEXT("ORBITS ON ({0})");
    inline constexpr const TCHAR* HUD_HINT_ORBITS_OFF_FORMAT = TEXT("ORBITS OFF ({0})");
    inline constexpr const TCHAR* HUD_HINT_KEYS_FORMAT = TEXT("   {0} speed panel   {1} map   {2}/{3}/{4}/{5} "
        "target   {6} match frame   {7} surface-lock   {8} {9} warp   {10} {11} radar zoom   {12} radar auto   {13} fire"
        "   {14} drop target   {15} routes   {16} resume");
    inline constexpr const TCHAR* HUD_HINT_MAP_FORMAT = TEXT("MAP   J jump   M move   P patrol   right drag orbit   "
        "middle or Shift+right drag pan   wheel zoom   {0} / Esc close");
    inline constexpr const TCHAR* HUD_SURFACE_LOCK_HINT_FORMAT = TEXT("{SurfaceLock} to surface-lock  ");
    inline constexpr const TCHAR* HUD_FRAME_LOCK_TAG_FORMAT = TEXT(" [{MatchLock} LOCK]");
    inline constexpr const TCHAR* HUD_ROUTE_RESUME_HINT_FORMAT = TEXT("   {Resume} resume");
    // HUD hint under the reticle until the player first steers in this session (issue #31, SDD 19 Amendment 5)
    inline constexpr const TCHAR* HUD_STEER_HINT_FORMAT = TEXT("Hold {SteerHold} to steer");
    inline constexpr const TCHAR* HUD_NO_TARGET_FORMAT = TEXT("none ({SelectTarget})");
    inline constexpr const TCHAR* SPEED_PANEL_TITLE_FORMAT = TEXT("SPEED CAP  ({SpeedPanel} / Enter close, Esc close)");

    // Overlay Menu (SDD 22, SDD 34 section 7): title ('{<BindingId>}' is that action's current key, '{N}' the viewed
    // profile's number), row marks, the profile tabs and rows, and the key hint
    inline constexpr const TCHAR* OVERLAY_PROFILE_NUMBER_TOKEN = TEXT("{N}");
    inline constexpr const TCHAR* OVERLAY_MENU_TITLE_FORMAT =
        TEXT("OVERLAYS - Profile {N}  ({OverlayMenu} / Esc close)");
    inline constexpr const TCHAR* OVERLAY_MENU_HINT = TEXT("Click or Space: toggle   Up / Down: move   "
        "PgUp / PgDn: profile   {CycleOverlayProfile}: next profile   {OverlayMaster}: all checked overlays on / off");
    inline constexpr const TCHAR* OVERLAY_MENU_HINT_EMBEDDED = TEXT("Click a tab to show a profile, a row to toggle it "
        "(applies at once).   {CycleOverlayProfile} in flight: next profile   {OverlayMaster} in flight: all checked "
        "overlays on / off");
    inline constexpr const TCHAR* OVERLAY_MENU_CHECKED = TEXT("[x] ");
    inline constexpr const TCHAR* OVERLAY_MENU_UNCHECKED = TEXT("[ ] ");
    inline constexpr const TCHAR* OVERLAY_MENU_CHILD_INDENT = TEXT("    ");
    inline constexpr const TCHAR* OVERLAY_MENU_TAB_FORMAT = TEXT("Profile {N}");
    inline constexpr const TCHAR* OVERLAY_MENU_TAB_ACTIVE_MARK = TEXT("> ");
    inline constexpr const TCHAR* OVERLAY_MENU_TAB_INACTIVE_MARK = TEXT("  ");
    inline constexpr const TCHAR* OVERLAY_MENU_TAB_OFF_SUFFIX = TEXT(" (off)");
    inline constexpr const TCHAR* OVERLAY_MENU_ORBIT_LINES_LABEL = TEXT("Orbit Lines");
    inline constexpr const TCHAR* OVERLAY_MENU_PROFILE_ENABLED_LABEL = TEXT("Profile enabled");
    inline constexpr const TCHAR* OVERLAY_MENU_ENABLE_NEW_LABEL = TEXT("Enable (starts as a copy of Profile 1)");
    inline constexpr const TCHAR* OVERLAY_MENU_ENABLE_RESTORE_LABEL = TEXT("Enable (restores its last settings)");

    // Flight HUD tag after the profile cycle key (SDD 34 section 8); '{N}' is the active profile's number
    inline constexpr const TCHAR* HUD_OVERLAY_PROFILE_TAG_FORMAT = TEXT("OVERLAYS: PROFILE {N}");

    // Interaction menus (SDD 22 section 2.4): action labels (SOLActionTable::ESOLAction order), the reasons an action
    // is unavailable (ESOLActionBlock order, None first), object type names (ESOLObjectType order), the HUD info card
    // and hint, and the map list menu
    inline constexpr const TCHAR* INTERACT_ACTION_LABELS[] =
    {
        TEXT("Target"), TEXT("Info"), TEXT("Move To"), TEXT("Follow / Orbit"), TEXT("Jump To"), TEXT("Match speed"),
        TEXT("Attack"), TEXT("Chase Down"), TEXT("Communications"), TEXT("Trade"),
    };
    inline constexpr const TCHAR* INTERACT_BLOCK_TEXTS[] =
    {
        TEXT(""), TEXT("Not yet available"), TEXT("Not offered for this object"), TEXT("No ship"),
        TEXT("The pilot has no control (blackout)"), TEXT("Not during a jump"), TEXT("Cannot be targeted yet"),
        TEXT("Already the target"), TEXT("Already matched"), TEXT("Already flying there"),
        TEXT("Already following it"), TEXT("Already chasing it"),
    };
    inline constexpr const TCHAR* INTERACT_TYPE_NAMES[] =
    {
        TEXT("Star"), TEXT("Planet"), TEXT("Moon"), TEXT("Dwarf planet"), TEXT("Asteroid"), TEXT("Ship"),
        TEXT("Dropped target"),
    };
    inline constexpr const TCHAR* INTERACT_INFO_OF = TEXT(" of ");
    inline constexpr const TCHAR* INTERACT_INFO_DISTANCE = TEXT("DIST  ");
    inline constexpr const TCHAR* INTERACT_INFO_RADIUS = TEXT("RADIUS  ");
    inline constexpr const TCHAR* INTERACT_INFO_SHIELD = TEXT("SHIELD ");
    inline constexpr const TCHAR* INTERACT_INFO_HULL = TEXT("   HULL ");
    inline constexpr const TCHAR* INTERACT_INFO_PERCENT = TEXT("%");
    inline constexpr const TCHAR* INTERACT_TARGET_NAME_PREFIX = TEXT("Target ");
    inline constexpr const TCHAR* INTERACT_LIST_REASON_OPEN = TEXT("   (");
    inline constexpr const TCHAR* INTERACT_LIST_REASON_CLOSE = TEXT(")");
    inline constexpr const TCHAR* INTERACT_ATTACK_HINT_PREFIX = TEXT("ATTACK  ");
    inline constexpr const TCHAR* INTERACT_ATTACK_HINT_SUFFIX = TEXT(":  aim at it and fire (hold)");
    inline constexpr const TCHAR* INTERACT_LIST_HINT = TEXT("Up / Down, Enter or click: choose   Esc: close");

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
        { TEXT("Flight"), TEXT("Mouse2D"),
            TEXT("Steer while mouse steering is on: the mouse moves a virtual joystick (yaw and pitch rate; torque in "
                "manual flight). With it off the cursor is shown and stays inside the game window") },
        { TEXT("Flight"), TEXT("@SteerHold"),
            TEXT("Steer with the mouse while held (the joystick recenters on press and release)") },
        { TEXT("Flight"), TEXT("@SteerToggle"),
            TEXT("Mouse steering on / off (off at flight start unless Settings \"Mouse steering at start\" is on; the "
                "joystick recenters when it turns on or off)") },
        { TEXT("Flight"), TEXT("@RecenterStick"), TEXT("Recenter the joystick (stop turning)") },
        { TEXT("Flight"), TEXT("@ThrustForward|@ThrustBackward"), TEXT("Thrust forward / backward") },
        { TEXT("Flight"), TEXT("@StrafeLeft|@StrafeRight"), TEXT("Strafe left / right") },
        { TEXT("Flight"), TEXT("@ThrustUp|@ThrustDown"), TEXT("Thrust up / down") },
        { TEXT("Flight"), TEXT("@RollLeft|@RollRight"), TEXT("Roll left / right") },
        { TEXT("Flight"), TEXT("@Boost"), TEXT("Boost on / off (turns off at a blackout, a jump and a respawn)") },
        { TEXT("Flight"), TEXT("@ToggleAssist"),
            TEXT("Assisted / manual flight (starts assisted unless turned off in Settings); during autopilot: take "
                "control in assisted flight") },
        { TEXT("Flight"), TEXT("@StopSpin"), TEXT("Stop spin: cancel all rotation (hold)") },
        { TEXT("Flight"), TEXT("@FreeLook"), TEXT("Free-look around the ship (hold)") },
        { TEXT("Flight"), TEXT("MouseWheelAxis"), TEXT("Step the speed cap ({SpeedCapNotches} notches per factor of ten)") },
        { TEXT("Flight"), TEXT("@SpeedPanel"), TEXT("Speed panel: set the cap digit by digit or from a body's speed") },
        { TEXT("Targeting"), TEXT("@SelectTarget"), TEXT("Select the target nearest the reticle") },
        { TEXT("Targeting"), TEXT("@NextTarget|@PreviousTarget"), TEXT("Next farther / next nearer target") },
        { TEXT("Targeting"), TEXT("@ClearTarget"), TEXT("Clear the target") },
        { TEXT("Targeting"), TEXT("@MatchLock"), TEXT("Lock the reference frame to the target (match its velocity)") },
        { TEXT("Targeting"), TEXT("@SurfaceLock"), TEXT("Surface-lock: keep the ship upright over the nearest body") },
        { TEXT("Combat"), TEXT("@Fire"),
            TEXT("Fire the pulse guns (hold). With mouse steering off, on an overlay icon: press, drag toward an "
                "action and release to choose it (release in the centre, or Esc, cancels)") },
        { TEXT("Combat"), TEXT("@DropTarget"), TEXT("Drop a practice target {DropDistance} ahead") },
        { TEXT("Time"), TEXT("@WarpDown|@WarpUp"), TEXT("Time warp down / up") },
        { TEXT("Time"), TEXT("@WarpReset"), TEXT("Time warp back to 1x") },
        { TEXT("Overlays"), TEXT("@OverlayMaster"),
            TEXT("Overlays on / off in the active profile: everything checked in the Overlay Menu together (saved "
                "with the profile)") },
        { TEXT("Overlays"), TEXT("@ToggleOrbitLines"), TEXT("Orbit lines on / off in the active profile") },
        { TEXT("Overlays"), TEXT("@OverlayMenu"),
            TEXT("Overlay Menu: the five overlay profiles and what the overlay key shows in each (icons per kind, "
                "orbits per kind, ships on radar, darkening); also in Settings") },
        { TEXT("Overlays"), TEXT("@CycleOverlayProfile"),
            TEXT("Cycle overlay profiles (the enabled ones; also in the Overlay Menu)") },
        { TEXT("HUD"), TEXT("@RadarZoomIn|@RadarZoomOut"), TEXT("Radar range in / out (manual)") },
        { TEXT("HUD"), TEXT("@RadarAuto"), TEXT("Radar back to automatic range") },
        { TEXT("Map"), TEXT("@OpenMap"), TEXT("Open the map in the last mode used / close it") },
        { TEXT("Map"), TEXT("@OpenMapJump"), TEXT("Open the map in Jump mode") },
        { TEXT("Map"), TEXT("J|M|P"), TEXT("Jump / Move / Patrol mode (P again: loop or ping-pong patrol)") },
        { TEXT("Map"), TEXT("RightMouseButton"), TEXT("Orbit the map camera (drag)") },
        { TEXT("Map"), TEXT("RightMouseButton"),
            TEXT("Click a body: its action menu (Up / Down and Enter, or click; Esc closes)") },
        { TEXT("Map"), TEXT("MiddleMouseButton|LeftShift+RightMouseButton"), TEXT("Pan the map (drag)") },
        { TEXT("Map"), TEXT("MouseWheelAxis"), TEXT("Zoom the map") },
        { TEXT("Map"), TEXT("LeftMouseButton"),
            TEXT("Pick a destination or add a waypoint: press on a body or space, drag, release") },
        { TEXT("Map"), TEXT("LeftShift|LeftShift+LeftMouseButton"),
            TEXT("Preview the height above the plane / lock it") },
        { TEXT("Map"), TEXT("LeftControl+RightMouseButton"), TEXT("Undo the last waypoint (click)") },
        { TEXT("Map"), TEXT("X"), TEXT("Clear the destination and the planned waypoints") },
        { TEXT("Map"), TEXT("Enter"), TEXT("Jump to the destination / start the route") },
        { TEXT("Map"), TEXT("Delete"), TEXT("Clear the active route") },
        { TEXT("Map"), TEXT("Escape"), TEXT("Close the map") },
        { TEXT("Routes"), TEXT("@Resume"), TEXT("Resume a paused route (in flight and on the map)") },
        { TEXT("Routes"), TEXT("@ToggleRouteOverlay"),
            TEXT("Show or hide routes in flight (the map always shows them)") },
        { TEXT("Menu"), TEXT("Escape"), TEXT("Stop a Chase Down; else pause menu (in flight) / resume") },
        { TEXT("Menu"), TEXT("LeftControl+F1"),
            TEXT("Same as Esc (temporary: Esc stops Play-In-Editor before the game sees it)") },
        { TEXT("Demo"), TEXT("LeftControl+F2"), TEXT("Show or hide the NPC stats overlay") },
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
    inline constexpr const TCHAR* INFO_THRUST = TEXT("Thrust (forward / reverse / lateral / vertical)");
    inline constexpr const TCHAR* INFO_THRUST_CAP = TEXT("Thrust limit (whole ship)");
    inline constexpr const TCHAR* INFO_BOOST = TEXT("Boost");
    inline constexpr const TCHAR* INFO_TURN_RATES = TEXT("Turn rate (pitch and yaw / roll)");
    inline constexpr const TCHAR* INFO_TURN_ACCEL = TEXT("Turn acceleration (pitch and yaw / roll)");
    inline constexpr const TCHAR* INFO_PILOT_G = TEXT("Pilot G limit (sustained / combat)");
    inline constexpr const TCHAR* INFO_G_TOLERANCE = TEXT("G meter: empty at the combat limit after");
    inline constexpr const TCHAR* INFO_BLACKOUT = TEXT("Blackout lasts at least");
    inline constexpr const TCHAR* INFO_SHIP_RADIUS = TEXT("Ship collision radius");
    inline constexpr const TCHAR* INFO_WARP_STEPS = TEXT("Time warp steps");
    inline constexpr const TCHAR* INFO_JUMP_DURATION = TEXT("Jump warp duration");
    inline constexpr const TCHAR* INFO_SURFACE_LOCK_AUTO = TEXT("Surface-lock engages automatically within");
    inline constexpr const TCHAR* INFO_SURFACE_LOCK_MANUAL = TEXT("Surface-lock with {SurfaceLock} from up to");
    inline constexpr const TCHAR* INFO_BOOST_FORMAT = TEXT("x{0} speed (assisted), x{1} thrust");
    inline constexpr const TCHAR* INFO_QUAD_FORMAT = TEXT("{0} / {1} / {2} / {3}");
    inline constexpr const TCHAR* INFO_G_FORMAT = TEXT("{0} G");
    inline constexpr const TCHAR* INFO_RAD_PER_S2_FORMAT = TEXT("{0} rad/s²");
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
            "{MatchLock}. It holds the velocity your keys ask for, up to the speed cap, but only with real thrust and "
            "never above your sustained G limit: it spends that on holding against gravity first, so large speed "
            "changes take time, and a planet heavier than the limit cannot be hovered over. Turn it off "
            "({ToggleAssist}) for manual flight: the keys fire the thrusters directly, the stick applies torque and a "
            "spin keeps going until you hold {StopSpin}. Gravity pulls in both modes. The HUD shows the mode: "
            "MANUAL, ASSISTED or AUTO."),
        TEXT("The mouse steers only while mouse steering is on: hold {SteerHold}, or press {SteerToggle} to turn it "
            "on or off. It starts off (the HUD says \"Hold {SteerHold} to steer\" until you first steer); the "
            "Settings option Mouse steering at start makes it start on. Each time it turns on or off, the joystick "
            "recenters. While it is off the cursor is shown (it stays inside the game window) and moving the mouse "
            "does not turn the ship. While you hold {FreeLook} to look around, the joystick circles turn yellow. "
            "Boost ({Boost}) is a switch: it stays on until you press it again, black out, jump or respawn."),
        TEXT("The autopilot (AUTO) flies you to a body and hands back to assisted flight when it arrives. Any thrust, "
            "roll, mouse steering or stop spin of yours takes control back in assisted flight at once, as does "
            "{ToggleAssist}; a key already held when the autopilot starts counts only after you let it go."),
        TEXT("Above the sustained G limit your body takes a G dose. The G meter on the HUD drains, faster the harder "
            "you pull, and refills below the limit. Manual flight warns when you pass the limit (assisted flight "
            "never does). As the meter runs low your vision narrows and grays out; when it empties you black out "
            "for a few seconds and the ship takes no input until you come to."),
        TEXT("Time warp speeds up the planets, not your ship: the ship is carried along with its reference frame, so "
            "an orbit or a landing spot stays put at any warp."),
        TEXT("Overlay profiles: there are five, Profile 1 to Profile 5, and each remembers its own choices in the "
            "Overlay Menu ({OverlayMenu}, also in Settings): which overlays are checked, whether the master switch "
            "({OverlayMaster}) and the orbit-line switch are on, and darkening. Profile 1 is always on; the first "
            "time you enable another profile it starts as a copy of Profile 1, and enabling it again later restores "
            "its last settings. {CycleOverlayProfile} cycles through the enabled profiles (in flight and in the "
            "Overlay Menu) and briefly shows the active one as a HUD tag; in the menu, PgUp / PgDn switch tabs (Left "
            "Shift+O does nothing). {OverlayMaster} turns on or off, together, everything checked in the active "
            "profile. "
            "The star, every planet, moon and dwarf planet, the 50 largest named asteroids, ring "
            "systems, the nearest practice targets and the nearest NPC ships get a small hologram icon that fades out "
            "as the real object grows on screen; the nearest objects off screen get an arrow at the edge pointing to "
            "them. Far icons are darker while darkening (a row of the Overlay Menu, it has no key) is on. Orbit lines "
            "({ToggleOrbitLines} or the Orbit Lines row; saved with the active profile) trace the orbits of the "
            "planets, moons, dwarf planets and the 50 largest named asteroids (each kind can be checked in the "
            "Overlay Menu; asteroid orbits start unchecked), dashed and fading with distance. Your target's orbit is "
            "drawn in gold. A line behind a planet or moon is hidden by it, and the part in front of it stays "
            "visible. Space dust (a Settings > Graphics option, the same in every profile) streams past when the ship "
            "moves relative to its reference frame; its speed is compressed and only shows motion and direction. It "
            "is hidden when stopped, under time warp, during jumps, on the map and while paused, and {OverlayMaster} "
            "does not affect it."),
        TEXT("Interaction menus: with mouse steering off, hovering an overlay icon highlights it. Press {Fire} on "
            "it, drag toward an action and release to choose it; releasing in the centre (or Esc) cancels, and "
            "{Fire} away from icons still fires. On the map, a right click on a body opens the same actions as a list. "
            "Target selects it; Info shows its card (top right) for a few seconds; Move To flies the autopilot there "
            "and stops; Follow / Orbit keeps station at the autopilot's safe distance on your side until you take "
            "control; Jump To jumps next to it; Match speed locks the reference frame to it; on a dropped target, "
            "Attack targets it and shows the fire hint. Greyed entries say why they are unavailable (Chase Down on a "
            "ship, Communications and Trade are not available yet)."),
        TEXT("Chase Down (from a body's or a dropped target's menu) chases it in bursts: the autopilot pulls above "
            "its usual gentle G limit, up to what you can survive (the full pull needs boost, which the chase uses "
            "only while your boost switch ({Boost}) is on), then cools at the sustained limit while the G meter "
            "recovers, and plans the bursts so the meter never drops below the Chase Down safety margin set in "
            "Settings (default 25%). The meter's floor and its resume mark show on the G bar. When it catches up it "
            "stays on the target's tail until you stop it. Any thrust, roll, steering or stop spin of yours, Esc, "
            "{ToggleAssist}, or another order ends the chase and returns you to assisted flight (the next Esc then "
            "opens the pause menu). It works under time warp, and it ends by itself if the target is destroyed or "
            "lost."),
        TEXT("The map ({OpenMap}; {OpenMapJump} opens it in Jump mode) shows the whole system; J, M and P switch "
            "between Jump, Move and Patrol mode. Press on a body or on empty space, drag to set where you want to be "
            "relative to it and release. In Jump mode, Enter jumps there. In Move and Patrol mode each pick adds a "
            "waypoint that moves with its body (Ctrl+right click undoes the last one, X clears them) and Enter starts "
            "the route: the autopilot flies through each waypoint at a speed its turn allows (a sharp turn stops) and "
            "stops at the last one of a Move, while a patrol of two or more waypoints repeats in a loop or back and "
            "forth (P again switches) until you clear it with Delete on the map. Thrust or steer (or C, a jump, "
            "another autopilot order) pauses a route and {Resume} resumes it. Routes are drawn as blue lines that "
            "brighten toward each waypoint, with arrows for the direction (a loop's closing leg is dashed); a "
            "waypoint above the plane through your ship has a ring with a centre dot, one below it a hollow orange "
            "ring. The plan is dim, a paused route dimmer and tagged PAUSED; the top of the screen shows the route's "
            "progress and ETA. {ToggleRouteOverlay} shows or hides routes in flight (the map always shows them). "
            "Time warp is never changed for you. The map is drawn as a hologram: bodies and rings as glowing "
            "outlines (the Sun orange), a faint grid on the ecliptic plane through the point the map camera circles, "
            "and a line from each body and waypoint to that plane (orange when it lies below it)."),
        TEXT("The flight HUD is a hologram: thin glowing blue lines and text on soft, borderless dark panels. Now "
            "and then a thin band of it shifts sideways for a moment, like a flickering hologram, and the radar's "
            "range rings pulse like sonar; the HUD hologram effects slider in Settings sets how strong that is (0 "
            "turns it off). The radar (bottom right) is drawn on your ship's own horizontal plane, ahead at the top, "
            "with the camera's view cone: contacts above the plane are blue with a solid line down to it, contacts "
            "below are orange with a dashed line up to it and a hollow mark, and the selected target is larger, "
            "ringed and named. While you hold {FreeLook} the joystick circles turn yellow."),
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
        TEXT("The Demo page (main menu, or the Demo tab here) fills the solar system with up to a million NPC ships "
            "on lanes between the planets, in clusters around bodies and the asteroid belt, and in battles a few "
            "kilometres from you. Ships near you fly and fight with full AI; farther ones follow simpler motion and "
            "are drawn as points. Ctrl+F2 shows or hides the stats overlay. Ctrl+F1 does the same as Esc, for "
            "Play-In-Editor, where Esc stops the game."),
    };

    // Demo page (SDD 11 section 3.8, Part 10e); row labels in SOLMenuState Demo row order (every option, then the
    // combat model, seed and ramp rows)
    inline constexpr const TCHAR* DEMO = TEXT("Demo");
    inline constexpr const TCHAR* DEMO_TITLE = TEXT("Scale demo");
    // HUD line under the AUTOPILOT marker (SDD 19 section 3.7): how to take control back from the autopilot
    inline constexpr const TCHAR* AUTOPILOT_CANCEL_HINT = TEXT("Thrust or steer to take control");

    // HUD chase marker (SDD 32 section 3.9): "CHASE > <name>  <distance>  closing <speed>  T <time>", the activity line
    // ("BURST 6.8 G", "COOL 1.5 G (resume at 45%)", "BRAKE 6.8 G", "BRAKE COOL 1.5 G", "HOLD"), the cancel hint and the
    // target-lost note
    inline constexpr const TCHAR* CHASE_MARKER_PREFIX = TEXT("CHASE > ");
    inline constexpr const TCHAR* CHASE_CLOSING_LABEL = TEXT("closing ");
    inline constexpr const TCHAR* CHASE_TIME_LABEL = TEXT("T ");
    inline constexpr const TCHAR* CHASE_HOLD = TEXT("HOLD");
    inline constexpr const TCHAR* CHASE_BURST = TEXT("BURST");
    inline constexpr const TCHAR* CHASE_COOL = TEXT("COOL");
    inline constexpr const TCHAR* CHASE_BRAKE = TEXT("BRAKE");
    inline constexpr const TCHAR* CHASE_BRAKE_COOL = TEXT("BRAKE COOL");
    inline constexpr const TCHAR* CHASE_RESUME_LABEL = TEXT("resume at ");
    inline constexpr const TCHAR* CHASE_CANCEL_HINT = TEXT("Thrust, steer or Esc to stop chasing");
    inline constexpr const TCHAR* CHASE_ENDED_NOTE = TEXT("CHASE ENDED: target lost");

    // HUD flight mode labels (SDD 19 section 3.7), in ESOLFlightMode order
    inline constexpr const TCHAR* HUD_FLIGHT_MODE_LABELS[] = { TEXT("MANUAL"), TEXT("ASSISTED"), TEXT("AUTO") };

    // One-time notice after a settings save from before the issue #19 key layout was migrated (SDD 19 Q11)
    inline constexpr const TCHAR* HUD_CONTROLS_CHANGED_NOTICE_FORMAT = TEXT("Default controls changed: "
        "{SteerHold} or {SteerToggle} steers, {ThrustUp} thrust up, {Boost} boost, {ToggleAssist} assist. "
        "See Controls (Esc)");

    inline constexpr const TCHAR* DEMO_HINT = TEXT("NPC ships across the solar system, up to a million. Options "
        "last for this session. Start demo spawns your ship above the start body, then the NPCs.");
    inline constexpr const TCHAR* DEMO_PAUSE_HINT = TEXT("Apply respawns the NPCs with these options around your ship "
        "and resumes; Stop NPCs removes them.");
    inline constexpr const TCHAR* DEMO_PRESETS = TEXT("Presets");
    inline constexpr const TCHAR* DEMO_PRESET_LABELS[] = { TEXT("Showcase"), TEXT("Benchmark"), TEXT("Ramp") };
    inline constexpr const TCHAR* DEMO_RESET = TEXT("Reset");
    inline constexpr const TCHAR* DEMO_START = TEXT("Start demo");
    inline constexpr const TCHAR* DEMO_APPLY = TEXT("Apply");
    inline constexpr const TCHAR* DEMO_STOP = TEXT("Stop NPCs");
    inline constexpr const TCHAR* DEMO_NO_WARNINGS = TEXT("No warnings.");
    inline constexpr const TCHAR* DEMO_VALUE_ON = TEXT("On");
    inline constexpr const TCHAR* DEMO_VALUE_OFF = TEXT("Off");
    inline constexpr const TCHAR* DEMO_UNIT_KM = TEXT(" km");
    inline constexpr const TCHAR* DEMO_UNIT_SECONDS = TEXT(" s");
    // Combat model names, in SOLNpcAi::ECombatModel order (bolts, statistical, hybrid)
    inline constexpr const TCHAR* DEMO_COMBAT_MODEL_LABELS[] = { TEXT("Bolts"), TEXT("Statistical"), TEXT("Hybrid") };
    inline constexpr const TCHAR* DEMO_WARNING_FULL_BUDGET = TEXT("More battle ships than the Full AI budget: some "
        "battle ships run Mid AI and do not fight.");
    inline constexpr const TCHAR* DEMO_WARNING_BEYOND_FULL = TEXT("Battles reach past the Full AI radius: ships there "
        "run Mid AI and do not fight.");
    inline constexpr const TCHAR* DEMO_ROW_LABELS[] =
    {
        TEXT("NPC count"),
        TEXT("Lane fraction"),
        TEXT("Battle ships near you"),
        TEXT("Battles"),
        TEXT("Battle radius"),
        TEXT("Battle distance min"),
        TEXT("Battle distance max"),
        TEXT("Full AI radius"),
        TEXT("Mid AI radius"),
        TEXT("Full AI budget"),
        TEXT("Mid AI budget"),
        TEXT("AI hysteresis"),
        TEXT("Mesh radius"),
        TEXT("Instanced radius"),
        TEXT("Mesh budget"),
        TEXT("Instanced budget"),
        TEXT("Render hysteresis"),
        TEXT("NPC combat"),
        TEXT("Stats overlay"),
        TEXT("Ramp settle time"),
        TEXT("Ramp measure time"),
        TEXT("Combat model"),
        TEXT("Seed"),
        TEXT("Run the stress ramp"),
    };

    // Demo page section headings, in page order (USOLDemoPageWidget lays out the rows under each)
    inline constexpr const TCHAR* DEMO_SECTION_TITLES[] = { TEXT("Population"), TEXT("Battles"), TEXT("AI tiers"),
        TEXT("Render tiers"), TEXT("Combat"), TEXT("Stats and stress ramp") };

    // About page
    inline constexpr const TCHAR* ABOUT_TITLE = TEXT("About");
    inline constexpr const TCHAR* ABOUT_DESCRIPTION = TEXT("A space flight simulation of the solar system at 1:1 "
        "scale, with real orbits, time warp, a jump map and ship combat.");
    inline constexpr const TCHAR* ABOUT_COPYRIGHT = TEXT("Copyright © 2026 Acid Rain Studios LLC. All rights reserved.");
    inline constexpr const TCHAR* ABOUT_ENGINE_FORMAT = TEXT("Built with Unreal Engine {0}");
    inline constexpr const TCHAR* ABOUT_CREDITS_TITLE = TEXT("Credits");
    inline constexpr const TCHAR* ABOUT_CREDITS = TEXT("Design and development: Acid Rain Studios LLC. ")
        TEXT("Orbital elements: NASA JPL. Star catalogue: AT-HYG (CC BY-SA 4.0).");
}
