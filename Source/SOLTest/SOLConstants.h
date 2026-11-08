/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Central home for shared physical constants, unit conversions, tunable defaults and asset paths (STYLE_GUIDE section 7)
namespace SOL
{
    // Physical constants and unit conversions
    inline constexpr double AU_M = 149597870700.0;                                          // One astronomical unit
    inline constexpr double SECONDS_PER_HOUR = 3600.0;
    inline constexpr double SECONDS_PER_DAY = 86400.0;
    inline constexpr double DAYS_PER_JULIAN_CENTURY = 36525.0;
    inline constexpr double SECONDS_PER_JULIAN_CENTURY = SECONDS_PER_DAY * DAYS_PER_JULIAN_CENTURY;
    inline constexpr double METERS_TO_CM = 100.0;
    inline constexpr double METERS_PER_KM = 1000.0;
    inline constexpr double MILLISECONDS_PER_SECOND = 1000.0;
    inline constexpr double CM_TO_METERS = 1.0 / METERS_TO_CM;
    inline constexpr double SPEED_OF_LIGHT_MPS = 299792458.0;

    // Speed-cap range shared by the debug spectator and the ship (SDD 2): log scale from 1 m/s to 0.5c
    inline constexpr double MIN_SPEED_CAP_MPS = 1.0;
    inline constexpr double MAX_SPEED_CAP_MPS = 0.5 * SPEED_OF_LIGHT_MPS;

    // Default spawn: altitude above the start body's surface, on its Sun-facing side (SDD 2)
    inline constexpr double DEFAULT_SPAWN_ALTITUDE_M = 1.0e7;

    // Hitch budget: the real delta of one frame is clamped to this before it reaches BOTH the sim clock and the ship
    // step, so a long hitch slows the whole universe uniformly instead of desynchronising the ship from the bodies
    inline constexpr double MAX_FRAME_DELTA_S = 0.5;

    // Ship flight step (Mass): a frame's real delta is split into substeps of at most this length...
    inline constexpr double SHIP_MAX_SUBSTEP_S = 1.0 / 30.0;

    // ...and at most this many substeps: 16 * 1/30 s = 0.533 s covers the whole MAX_FRAME_DELTA_S budget (15 would be
    // exactly 0.5 s, one spare absorbs rounding), so a clamped hitch never loses ship time
    inline constexpr int32 SHIP_MAX_SUBSTEPS = 16;
    static_assert(SHIP_MAX_SUBSTEP_S * SHIP_MAX_SUBSTEPS >= MAX_FRAME_DELTA_S,
        "The ship substep cap must cover the frame hitch budget");

    // Ship: speed cap at spawn, relative to the reference frame (the wheel steps it by 10^0.1)
    inline constexpr double SHIP_START_SPEED_CAP_MPS = 1000.0;

    // Autopilot under user time warp (SDD 18 section 3.5): the ship integrates SIM time in substeps of
    // max(AUTOPILOT_MIN_SUBSTEP_S, frame sim time / AUTOPILOT_MAX_SUBSTEPS), at most AUTOPILOT_MAX_SUBSTEPS per frame
    inline constexpr double AUTOPILOT_MIN_SUBSTEP_S = 10.0;
    inline constexpr int32 AUTOPILOT_MAX_SUBSTEPS = 256;

    // Autopilot HUD hint (SDD 18 decision 26): "precision reduced" above this warp (1 d/s) within this many body radii
    inline constexpr double AUTOPILOT_HINT_WARP = 86400.0;
    inline constexpr double AUTOPILOT_HINT_RADII = 10.0;

    // Autopilot to a body: the arrival point is this many body radii above the surface on the ship's side, plus the
    // fixed standoff below (so a small moon still gets a safe margin)
    inline constexpr double AUTOPILOT_BODY_STANDOFF_RADII = 0.25;
    inline constexpr double AUTOPILOT_BODY_STANDOFF_M = 10000.0;

    // ...moved out until the body's gravity there is at most this fraction of the autopilot's limit, so the arrival
    // can be held (Sun, Jupiter; SDD 18 Amendment 7, user decision H1)
    inline constexpr double AUTOPILOT_GRAVITY_STANDOFF_FRACTION = 0.9;

    // Autopilot sim-time substeps longer than this set the attitude kinematically (nose straight to the guidance's
    // forward, no spin): a rate-limited turn cannot be integrated stably in 10-169 s steps (SDD 18 Amendment 7)
    inline constexpr double AUTOPILOT_KINEMATIC_ATTITUDE_S = 1.0;

    // Rendezvous guidance arrival tolerances (SDD 18): arrived within this distance of the target, below this speed
    inline constexpr double AUTOPILOT_ARRIVAL_RADIUS_M = 10.0;
    inline constexpr double AUTOPILOT_ARRIVAL_SPEED_MPS = 1.0;

    // Player ship thrust capability (SDD 18 section 3.2, decision 20): G per axis, boost, total cap, rotation
    inline constexpr double PLAYER_THRUST_FORWARD_G = 3.0;
    inline constexpr double PLAYER_THRUST_REVERSE_G = 1.5;
    inline constexpr double PLAYER_THRUST_LATERAL_G = 2.0;
    inline constexpr double PLAYER_THRUST_VERTICAL_G = 2.0;
    inline constexpr double PLAYER_THRUST_BOOST_MULTIPLIER = 3.0;
    inline constexpr double PLAYER_THRUST_TOTAL_CAP_G = 9.0;
    inline constexpr double PLAYER_ANGULAR_ACCEL_ROLL_RADPS2 = 4.0;
    inline constexpr double PLAYER_ANGULAR_ACCEL_PITCH_YAW_RADPS2 = 3.0;
    inline constexpr double PLAYER_ANGULAR_RATE_ROLL_RADPS = 2.0943951023931953;       // 120 deg/s
    inline constexpr double PLAYER_ANGULAR_RATE_PITCH_YAW_RADPS = 1.5707963267948966;  // 90 deg/s
    inline constexpr double PLAYER_SPIN_CLAMP_RADPS = 6.283185307179586;               // 360 deg/s

    // Untrained pilot G tolerance (SDD 18 section 3.2; Amendment 7 adds the G-LOC recovery floor and the lethal-G
    // warning, user decision H2)
    inline constexpr double PILOT_SUSTAINED_G = 1.5;
    inline constexpr double PILOT_MAX_G = 9.0;
    inline constexpr double PILOT_TIME_TO_EMPTY_AT_MAX_G_S = 10.0;
    inline constexpr double PILOT_DRAIN_EXPONENT = 2.0;
    inline constexpr double PILOT_RECOVER_PER_S = 0.05;
    inline constexpr double PILOT_BLACKOUT_DURATION_S = 5.0;
    inline constexpr double PILOT_WAKE_METER = 0.2;
    inline constexpr double PILOT_CRITICAL_METER = 0.25;
    inline constexpr double PILOT_VISION_START_METER = 0.4;
    inline constexpr double PILOT_AUTOPILOT_RECOVERY_G = 1.5;
    inline constexpr double PILOT_UNCONSCIOUS_RECOVER_PER_S = 0.02;
    inline constexpr double PILOT_LETHAL_WARNING_S = 30.0;

    // Chase Down autopilot (SDD 32): G bursts planned against the pilot model, the meter floor and the tail point
    namespace Chase
    {
        // "CHASE ENDED: target lost" shows this long (real seconds from the loss)
        inline constexpr double TARGET_LOST_NOTE_S = 3.0;
        inline constexpr double MIN_METER_FLOOR = 0.01;             // Technical floor under any margin (0 is G-LOC)
        inline constexpr double COOL_HYSTERESIS_METER = 0.15;       // A cool off ends this far above floor + reserve
        // A burst turns into a cool off this close above floor + reserve: the per-step safety clamp makes the meter
        // approach that level without ever reaching it (the guidance flies 90% of the limit), so an exact test would
        // pin the meter there at about sustained G instead of cooling and bursting again (32a amendment)
        inline constexpr double COOL_SWITCH_TOLERANCE_METER = 0.01;
        inline constexpr int32 PLAN_G_LEVELS = 8;                   // Grid steps from sustained to the cap (9 levels)
        inline constexpr double PLAN_TIE_FRACTION = 0.005;          // Predicted times this close are a tie
        inline constexpr double REPLAN_INTERVAL_S = 0.25;           // Real seconds between periodic replans
        inline constexpr double REPLAN_METER_DELTA = 0.05;          // Meter off the prediction by more: replan now
        // A change of the target's frame-relative velocity above this fraction of the plan's closing speed (and at
        // least REPLAN_TARGET_DV_MIN_MPS) replans at once
        inline constexpr double REPLAN_TARGET_DV_FRACTION = 0.1;
        inline constexpr double REPLAN_TARGET_DV_MIN_MPS = 1.0;
        inline constexpr double REPLAN_IMPROVEMENT_FRACTION = 0.02; // A new plan must be this much faster to replace
        // Slack of the replan feasibility tests of the current plan (its burst against the cap, its meter vs the floor)
        inline constexpr double REPLAN_FEASIBILITY_TOLERANCE = 1.0e-9;
        inline constexpr double TAIL_MIN_SPEED_MPS = 1.0;           // Slower relative to its frame: approach side
        inline constexpr double TARGET_TRAIL_M = 200.0;             // Minimum trail distance behind a targetable
        inline constexpr double TARGET_TRAIL_RADIUS_FACTOR = 3.0;   // Tunable k: trail = max(TARGET_TRAIL_M, k x r)
        inline constexpr double REPLAN_MAX_SIM_S = 30.0;            // Sim seconds between replans under time warp
        inline constexpr double TARGET_ACCEL_FILTER_S = 0.5;        // Low-pass time constant of the target accel
        inline constexpr int32 PLAN_MAX_SEGMENTS = 4096;            // Segment cap of a prediction (then: no intercept)
    }

    // Pilot G vision loss post-process (SDD 18): the look at full loss (blacked out), blended in by the G meter
    inline constexpr float G_VISION_POST_PROCESS_PRIORITY = 10.0f;
    inline constexpr float G_VISION_VIGNETTE_INTENSITY = 3.0f;
    inline constexpr double G_VISION_SATURATION = 0.0;
    inline const FLinearColor G_VISION_SCENE_TINT(0.02f, 0.02f, 0.02f, 1.0f);

    // Ship pawn, virtual joystick: the stick's travel radius as a fraction of the smaller viewport dimension...
    inline constexpr double JOYSTICK_RADIUS_FRACTION = 0.35;

    // ...its dead zone and response exponent (fractions of the radius, SOLFlight::JoystickToRotation)
    inline constexpr double JOYSTICK_DEAD_ZONE = 0.05;
    inline constexpr double JOYSTICK_EXPONENT = 1.5;
    // Mouse steering (SDD 19 section 3.4): Space holds it, the wheel click toggles it; the toggle's state at every
    // flight start is the Settings option "Mouse steering at start", whose default this is (off; SDD 19 Amendment 5)
    inline constexpr bool SHIP_STEER_TOGGLE_START_ON = false;
    // Frames the keys held when ship control comes back (map, menu, F3 panel closed) stay latched in AUTO even if they
    // read as released, covering the key flush before the OS key repeat resumes (SDD 19 Amendment 3)
    inline constexpr int32 SHIP_RELATCH_GRACE_FRAMES = 3;

    // Ship pawn, chase camera: spring-arm length behind the ship and camera height above it (cm, 60 m and 15 m)
    inline constexpr float SHIP_CAMERA_ARM_LENGTH_CM = 6000.0f;
    inline constexpr float SHIP_CAMERA_HEIGHT_CM = 1500.0f;

    // Ship pawn, chase camera: rotation lag speed (1/s) of the camera, applied by the pawn after the ship orientation
    // update as an exponential slerp (the spring arm's own lag is off; no position lag)
    inline constexpr double SHIP_CAMERA_ROTATION_LAG_SPEED = 8.0;

    // Lighting: sunlight illuminance (lux) for the Sun light and the body shading, tuned with the fixed exposure
    inline constexpr float SUN_ILLUMINANCE_LUX = 3.0f;

    // Lighting: forward-shading priority of the Sun light, above the ship fill light's default 0, so the Sun is the one
    // directional light used for translucency and forward shading (two equal priorities make the editor warn on screen)
    inline constexpr int32 SUN_FORWARD_SHADING_PRIORITY = 1;

    // Lighting (SDD 16, 16d): the ship and dropped targets are on lighting channel 1 only. While the observer is lit,
    // the Sun light lights channels 0 and 1 and the ship's own sun light is hidden (no extra light pass); while it is
    // shadowed, the Sun light keeps channel 0 (the ring rocks, which shadow themselves per pixel) and the ship sun
    // light lights channel 1 at the shadowed intensity. Hysteresis (SOLRingShadow::NextShipShadowedState): the observer
    // becomes shadowed when the sunlight fraction drops below ENTER and lit again only at LEAVE or above, so a
    // fraction hovering at one value cannot flip the lights (and rebuild their proxies) every frame
    inline constexpr double SHIP_SUN_SHADOW_ENTER_BELOW = 0.9999;
    inline constexpr double SHIP_SUN_SHADOW_LEAVE_AT = 0.99995;

    // Ship pawn: shadowless directional "headlight" on the camera that lights the ship's shadowed side, as a fraction
    // of SUN_ILLUMINANCE_LUX (bodies use an unlit material and ignore it)
    inline constexpr float SHIP_FILL_LIGHT_FRACTION = 0.1f;

    // ...tilted down from the view direction (degrees) so it lands on the ship's upper surfaces instead of grazing them
    inline constexpr float SHIP_FILL_LIGHT_PITCH_DEG = -45.0f;

    // Ship pawn, chase camera: FOV widens on a log scale of speed relative to the frame, from BASE at or below
    // FOV_MIN_SPEED to MAX at or above FOV_MAX_SPEED, easing toward the target at FOV_INTERP_SPEED
    inline constexpr double SHIP_CAMERA_BASE_FOV_DEG = 90.0;
    inline constexpr double SHIP_CAMERA_MAX_FOV_DEG = 110.0;
    inline constexpr double SHIP_CAMERA_FOV_MIN_SPEED_MPS = 100.0;
    inline constexpr double SHIP_CAMERA_FOV_MAX_SPEED_MPS = 0.5 * SPEED_OF_LIGHT_MPS;
    inline constexpr float SHIP_CAMERA_FOV_INTERP_SPEED = 4.0f;

    // Ship pawn, Alt free-look: camera orbit per mouse pixel and the pitch limit of the orbit
    inline constexpr double SHIP_FREE_LOOK_DEG_PER_PIXEL = 0.2;
    inline constexpr double SHIP_FREE_LOOK_MAX_PITCH_DEG = 80.0;

    // Targeting: candidate slots reserved for registered (non-body) targetables so the per-frame rebuild never grows
    inline constexpr int32 TARGETING_RESERVED_TARGETABLES = 64;

    // Rendering: bodies farther than this are pulled in with angular size preserved (1e11 cm = 1,000,000 km)
    inline constexpr double DEFAULT_MAX_RENDER_DISTANCE_CM = 1.0e11;

    // Rendering: the render origin snaps back to the observer once the observer drifts this far from it (10 km).
    // ASOLStarField pins its sprites and sky to the Unreal origin and relies on this bound staying small; see SDD 5 3.2
    inline constexpr double RENDER_REBASE_DISTANCE_M = 1.0e4;

    // Minor bodies (SDD 6): the asteroid belt's generation seed, fixed so the belt is identical every session; a future
    // save-game system could vary it
    inline constexpr int32 ASTEROID_BELT_SEED = 20260601;

    // Ring patch (SDD 6 Amendments 6-8): the near-field rock generation seed, distinct from the belt's
    inline constexpr int32 RING_PATCH_SEED = 20261006;

    // Ring patch: the active cell window radius kept populated around the player (a (2*R+1)x(2*A+1) grid of
    // SOLRingPatch cells), and the activation margin/half-thickness ComputeActiveCell admits a position within,
    // expressed as a multiple of SOLRingPatch::CELL_SIZE_M for a sense of scale independent of the exact cell size
    // A radius-R window only GUARANTEES real coverage of positions within (R - 0.5) cells of the player (SDD 6
    // Amendment 9): the player can sit anywhere within their own cell, up to half a cell off the center the window is
    // built around. R=2 gives ~1.5 cells (1.5 km) of real guaranteed margin, a buffer against warp-speed movement
    // between per-frame reassignment checks, not just the bare minimum R=1 (~0.5 cell) would give.
    inline constexpr int32 RING_PATCH_WINDOW_RADIAL_RADIUS = 2;
    inline constexpr int32 RING_PATCH_WINDOW_ANGULAR_RADIUS = 2;
    inline constexpr double RING_PATCH_ACTIVATION_MARGIN_CELLS = 5.0;
    inline constexpr double RING_PATCH_HALF_THICKNESS_CELLS = 2.0;

    // Far-field Niagara ring system only has 3 gap-band User Parameter slots (SDD 6 Amendment 11); AllGapBandsM's
    // real data never exceeds this (Saturn's Cassini Division plus Pan's and Daphnis's shepherd gaps)
    inline constexpr int32 RING_FAR_FIELD_GAP_SLOT_COUNT = 3;

    // Niagara User Parameter names of Paths::RING_FAR_NIAGARA_SYSTEM (SDD 6 Amendment 11; authored live via the
    // editor's NiagaraToolsets API, so these names live only here and in that asset, not in any content-pipeline
    // script). Every gap slot's full name is spelled out rather than built from a prefix, so
    // RING_FAR_FIELD_GAP_SLOT_COUNT changing is a compile error at the array in SOLRingVisuals.cpp
    // (static_assert), not a silently-wrong constructed name.
    namespace RingFarFieldParams
    {
        inline constexpr const TCHAR* OUTER_RADIUS_CM = TEXT("RingOuterRadiusCm");
        inline constexpr const TCHAR* INNER_RADIUS_CM = TEXT("RingInnerRadiusCm");
        inline constexpr const TCHAR* COLOR = TEXT("RingColor");
        inline constexpr const TCHAR* GAP0_INNER_RADIUS_CM = TEXT("RingGap0InnerRadiusCm");
        inline constexpr const TCHAR* GAP0_OUTER_RADIUS_CM = TEXT("RingGap0OuterRadiusCm");
        inline constexpr const TCHAR* GAP1_INNER_RADIUS_CM = TEXT("RingGap1InnerRadiusCm");
        inline constexpr const TCHAR* GAP1_OUTER_RADIUS_CM = TEXT("RingGap1OuterRadiusCm");
        inline constexpr const TCHAR* GAP2_INNER_RADIUS_CM = TEXT("RingGap2InnerRadiusCm");
        inline constexpr const TCHAR* GAP2_OUTER_RADIUS_CM = TEXT("RingGap2OuterRadiusCm");
    }

    // Verification-only ring-plane spawn (-SOLRingStartKm): default height above the plane, in km
    inline constexpr double RING_START_DEFAULT_HEIGHT_KM = 0.1;

    // Ring dense layer (SDD 6 Amendments 14-15): a 1M-rock GPU Niagara carpet in a wrapped window around the camera,
    // shown only near the ring. Counts and sizes are measured values from Amendment 15's profiling table
    inline constexpr int32 RING_DENSE_PARTICLE_COUNT = 1000000;
    inline constexpr double RING_DENSE_WINDOW_RADIUS_CM = 150000.0;      // W: half the wrapped tile size
    inline constexpr double RING_DENSE_HALF_THICKNESS_CM = 2000.0;       // 1-sigma half thickness of the rock layer
    inline constexpr double RING_DENSE_ROCK_MIN_RADIUS_CM = 25.0;
    inline constexpr double RING_DENSE_ROCK_MAX_RADIUS_CM = 600.0;
    inline constexpr double RING_DENSE_CULL_ANGULAR_RADIUS = 0.001;      // rad; rocks smaller on screen are hidden
    // Fade band: the carpet only ever reaches its window radius horizontally, and its biggest rock stops being drawn
    // beyond RING_DENSE_ROCK_MAX_RADIUS_CM / RING_DENSE_CULL_ANGULAR_RADIUS = 6 km, so past 6 km the GPU system would
    // simulate and cull a million rocks to draw none (SDD 6 Amendment 15 review). The far disc's material fades
    // itself out over 1-4 km of camera distance, which this band overlaps.
    inline constexpr double RING_DENSE_FADE_IN_DISTANCE_M = 1500.0;      // fully on within this distance of the ring
    inline constexpr double RING_DENSE_FADE_OUT_DISTANCE_M = 6000.0;     // fully off beyond this distance
    static_assert(RING_DENSE_FADE_OUT_DISTANCE_M <= RING_DENSE_ROCK_MAX_RADIUS_CM / RING_DENSE_CULL_ANGULAR_RADIUS / METERS_TO_CM * 1.0e0 + 1.0,
        "Fade out beyond the distance the largest rock is still drawn would simulate a million rocks for nothing");
    inline constexpr double RING_DENSE_RELEASE_DELAY_SECONDS = 20.0;     // hidden this long: free the rocks' VRAM
    inline constexpr double RING_DENSE_MAX_TIME_WARP = 10.0;             // a co-rotating carpet is meaningless above this

    // Niagara User Parameter names of Paths::RING_DENSE_NIAGARA_SYSTEM (SDD 6 Amendment 15). The ring radii, gaps and
    // colour reuse RingFarFieldParams' names (both systems declare them)
    namespace RingDenseParams
    {
        inline constexpr const TCHAR* PARTICLE_COUNT = TEXT("RingDenseParticleCount");
        inline constexpr const TCHAR* WINDOW_RADIUS_CM = TEXT("RingWindowRadiusCm");
        inline constexpr const TCHAR* HALF_THICKNESS_CM = TEXT("RingDenseHalfThicknessCm");
        inline constexpr const TCHAR* ROCK_MIN_RADIUS_CM = TEXT("RingRockMinRadiusCm");
        inline constexpr const TCHAR* ROCK_MAX_RADIUS_CM = TEXT("RingRockMaxRadiusCm");
        inline constexpr const TCHAR* CULL_ANGULAR_RADIUS = TEXT("RingRockCullAngularRadius");
        inline constexpr const TCHAR* FILL = TEXT("RingDenseFill");
        inline constexpr const TCHAR* WINDOW_PHASE_CM = TEXT("RingWindowPhaseCm");
        inline constexpr const TCHAR* WINDOW_CENTER_KM = TEXT("RingWindowCenterKm");   // km, not cm: Niagara warns on a
                                                                                          // vec3 beyond ~10 km, and this is ~1e5 km
    }

    // Space dust speed cues (SDD 43): a camera-local Niagara particle box whose motion shows the ship's speed relative
    // to its reference frame
    namespace SpaceDust
    {
        inline constexpr double DEAD_ZONE_MPS = 0.5;             // relative speed below this: no dust at all
        inline constexpr double FULL_OPACITY_MPS = 5.0;          // relative speed at which the dust is fully faded in
        inline constexpr double APPARENT_MAX_MPS = 150.0;        // cap on the apparent (displayed) speed
        inline constexpr double STREAK_START_MPS = 10.0;         // apparent speed at which points start to streak
        inline constexpr double STREAK_TIME_S = 0.12;            // streak length = apparent speed x this
        inline constexpr double STREAK_MAX_CM = 2000.0;          // cap on the streak length
        inline constexpr double STREAK_MIN_ALPHA = 0.35;         // alpha floor of a fully stretched streak
        inline constexpr double POINT_SIZE_CM = 60.0;            // base point (streak width) size (tuned from 30)
        inline constexpr double POINT_SIZE_RANDOM_MIN = 0.6;     // per-particle size multiplier, low end
        inline constexpr double POINT_SIZE_RANDOM_MAX = 1.4;     // per-particle size multiplier, high end
        inline constexpr double SMOOTHING_TAU_S = 0.15;          // time constant of the velocity / opacity smoothing
        inline constexpr double OPACITY_EPSILON = 1.0e-3;        // opacity below this counts as off
        inline constexpr double MAX_TIME_WARP = 1.0;             // dust is hidden above this time-warp factor
        inline const FLinearColor DUST_COLOR(0.75f, 0.80f, 0.90f); // point tint, cool grey (COLOR is an engine macro)
        inline constexpr double INTENSITY = 2.0;                 // emissive intensity of the points (tuned from 0.6)
        inline constexpr int32 PARTICLE_COUNT = 4096;            // particles at High effects quality
        inline constexpr int32 PARTICLE_COUNT_LOW = 1024;        // particles at Low effects quality
        inline constexpr int32 PARTICLE_COUNT_MEDIUM = 2048;     // particles at Medium effects quality
        inline constexpr double BOX_SIZE_CM = 40000.0;           // edge of the camera-local wrapping box
        inline constexpr double RADIAL_FADE_START_FRACTION = 0.7;  // fade starts at this fraction of the box half-size
        inline constexpr double NEAR_FADE_START_CM = 300.0;      // points closer to the camera than this are invisible
        inline constexpr double NEAR_FADE_END_CM = 1500.0;       // points are fully visible beyond this camera distance
        inline constexpr const TCHAR* CVAR_ENABLED = TEXT("sol.SpaceDust");  // console switch for the whole effect
    }

    // Niagara User Parameter names of Paths::SPACE_DUST_NIAGARA_SYSTEM (SDD 43), without the "User." prefix like the
    // other parameter-name blocks
    namespace SpaceDustParams
    {
        inline constexpr const TCHAR* PARTICLE_COUNT = TEXT("SpaceDustParticleCount");
        inline constexpr const TCHAR* BOX_SIZE_CM = TEXT("SpaceDustBoxSizeCm");
        inline constexpr const TCHAR* POINT_SIZE_CM = TEXT("SpaceDustPointSizeCm");
        inline constexpr const TCHAR* RADIAL_FADE_START = TEXT("SpaceDustRadialFadeStart");
        inline constexpr const TCHAR* NEAR_FADE_START_CM = TEXT("SpaceDustNearFadeStartCm");
        inline constexpr const TCHAR* NEAR_FADE_END_CM = TEXT("SpaceDustNearFadeEndCm");
        inline constexpr const TCHAR* COLOR = TEXT("SpaceDustColor");
        inline constexpr const TCHAR* OFFSET_CM = TEXT("SpaceDustOffsetCm");
        inline constexpr const TCHAR* VELOCITY_CM_S = TEXT("SpaceDustVelocityCmS");
        inline constexpr const TCHAR* OPACITY = TEXT("SpaceDustOpacity");
        inline constexpr const TCHAR* STREAK_LENGTH_CM = TEXT("SpaceDustStreakLengthCm");
        inline constexpr const TCHAR* STREAK_ALPHA = TEXT("SpaceDustStreakAlpha");
        inline constexpr const TCHAR* CAMERA_LOCAL_CM = TEXT("SpaceDustCameraLocalCm");
    }

    // The camera must sit inside the fully faded-in core of the box (SDD 4.4): the core half-size minus the near fade
    // end must exceed the camera's distance from the ship (arm and height are orthogonal; compared as squares)
    static_assert(
        (SpaceDust::RADIAL_FADE_START_FRACTION * SpaceDust::BOX_SIZE_CM * 0.5 - SpaceDust::NEAR_FADE_END_CM) > 0.0
        && (SpaceDust::RADIAL_FADE_START_FRACTION * SpaceDust::BOX_SIZE_CM * 0.5 - SpaceDust::NEAR_FADE_END_CM)
            * (SpaceDust::RADIAL_FADE_START_FRACTION * SpaceDust::BOX_SIZE_CM * 0.5 - SpaceDust::NEAR_FADE_END_CM)
            > static_cast<double>(SHIP_CAMERA_ARM_LENGTH_CM) * SHIP_CAMERA_ARM_LENGTH_CM
                + static_cast<double>(SHIP_CAMERA_HEIGHT_CM) * SHIP_CAMERA_HEIGHT_CM,
        "The chase camera must stay inside the fully faded-in core of the space dust box");

    // Profile indices of the far-field material's radial ring-profile table (order fixed by the asset)
    inline constexpr int32 RING_PROFILE_INDEX_JUPITER = 0;
    inline constexpr int32 RING_PROFILE_INDEX_SATURN = 1;
    inline constexpr int32 RING_PROFILE_INDEX_URANUS = 2;
    inline constexpr int32 RING_PROFILE_INDEX_NEPTUNE = 3;
    inline constexpr int32 RING_PROFILE_INDEX_INFER = -1;    // let the asset infer it from the outer radius

    // Ring shadows (SDD 16): analytic ring and planet shadows, SOLRingShadowMath and Shaders/Private/SOLRingShadow.ush
    inline constexpr double SUN_RADIUS_M = 6.957e8;                       // IAU 2015 nominal solar radius
    inline constexpr double RING_SHADOW_MIN_SUN_ELEVATION_SIN = 1.0e-3;   // below this |mu| the Sun is in the plane
    inline constexpr double RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD = 0.5; // clamp on alpha inside tan()
    inline constexpr double RING_SHADOW_LAYER_HALF_THICKNESS_M = RING_DENSE_HALF_THICKNESS_CM * CM_TO_METERS;
    inline constexpr int32 RING_SHADOW_PROFILE_COUNT = 4;                 // profiles served by RealTauProfile
    // Bands one shader box filter visits exactly (SOLRS_MAX_FILTER_BANDS); a wider window fills its rest with the last
    // band's value. The window rarely spans more than three bands
    inline constexpr int32 RING_SHADOW_MAX_FILTER_BANDS = 16;

    // Project shader folder (relative to the project directory) and the virtual path the module maps it to, so
    // material Custom nodes can include "/Project/SOLTest/Private/<file>.ush" (SDD 16)
    inline constexpr const TCHAR* SHADER_SOURCE_FOLDER = TEXT("Shaders");
    inline constexpr const TCHAR* SHADER_VIRTUAL_DIRECTORY = TEXT("/Project/SOLTest");
    // The generated profile include, relative to SHADER_SOURCE_FOLDER (SOLRingShadow::BuildShaderProfileSource)
    inline constexpr const TCHAR* RING_SHADOW_PROFILE_SHADER_FILE = TEXT("Private/SOLRingShadowProfile.ush");

    // Vector parameters of Paths::RING_SHADOW_PARAMETER_COLLECTION (SDD 16), written by the ring whose dense layer is
    // shown; the defaults (1, 0, 1, 0) and (0, 0, 1, 0) mean "no shadow"
    namespace RingShadowMpcParams
    {
        // (planet shadow at the viewpoint, slant optical depth tau/|mu|, Sun side +-1, layer 1-sigma half-thickness cm)
        inline constexpr const TCHAR* STATE = TEXT("RingShadowState");
        // (ring normal in Unreal world axes, viewpoint height above the ring plane in cm)
        inline constexpr const TCHAR* PLANE = TEXT("RingShadowPlane");
        // (the viewpoint's render-space position in cm, where PLANE's height applies; -)
        inline constexpr const TCHAR* ORIGIN = TEXT("RingShadowOrigin");
    }

    // The "no shadow" values of RingShadowMpcParams::STATE (planet lit, tau 0, Sun on the normal's side, no layer) and
    // PLANE (any unit normal, height 0), matching the collection's defaults (Tools/Rings/create_ring_shadow_assets.py)
    inline constexpr float RING_SHADOW_MPC_UNSHADOWED_STATE[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
    inline constexpr float RING_SHADOW_MPC_UNSHADOWED_PLANE[4] = { 0.0f, 0.0f, 1.0f, 0.0f };

    // RingFarDiscParams::PLANET_RADIUS_CM sent while sol.RingShadows is 0: a 1 cm planet casts no visible shadow on the
    // far disc (M_SOLRingFar replaces 0 or less with a built-in default radius)
    inline constexpr float RING_FAR_SHADOW_OFF_PLANET_RADIUS_CM = 1.0f;

    // Vector parameter of Paths::RING_ROCK_MATERIAL (SDD 16 Amendment 4): multiplies ParticleColor, which is white
    // outside Niagara, so the near-field Mass ISM sets it to the ring colour (the Niagara layer leaves it white)
    namespace RingRockMaterialParams
    {
        inline constexpr const TCHAR* TINT = TEXT("RockTint");
    }

    // Ring far field disc (SDD 6 Amendment 15): optional parameters of NS_SOLRingFar's analytic ring-profile material
    namespace RingFarDiscParams
    {
        inline constexpr const TCHAR* PROFILE_INDEX = TEXT("RingProfileIndex");
        inline constexpr const TCHAR* SUN_DIRECTION = TEXT("RingSunDirection");
        inline constexpr const TCHAR* PLANET_RADIUS_CM = TEXT("RingPlanetRadiusCm");
    }

    // Orbits: validity window of the JPL Standish secular elements (1800-2050), in Julian centuries since J2000
    inline constexpr double SECULAR_ELEMENTS_MIN_CENTURIES = -2.0;
    inline constexpr double SECULAR_ELEMENTS_MAX_CENTURIES = 0.5;

    // Orbits: eccentricity range the secular elements are clamped to (the linear rates run away far from J2000)
    inline constexpr double SECULAR_MIN_ECCENTRICITY = 0.0;
    inline constexpr double SECULAR_MAX_ECCENTRICITY = 0.99;

    // Kepler solver: at or above this eccentricity the Newton start switches from e*sin(M) to the far bracket end
    inline constexpr double KEPLER_HIGH_ECCENTRICITY = 0.8;

    // Jump map picking: a ray whose |dot(dir, unit plane normal)| is below this is treated as parallel (SDD 3 App. D)
    inline constexpr double MAP_PICK_PARALLEL_EPSILON = 1.0e-9;

    // Jump map camera: distance from the focus (the ship) when J opens the map. 1e13 m (~67 AU) frames Neptune's orbit
    // (~30 AU radius) with ~20% margin inside MAP_CAMERA_FOV_DEG even though the focus sits 1 AU off-center at Earth;
    // the orbit camera's own 1e10 m default would only show the inner neighborhood of the ship
    inline constexpr double MAP_DEFAULT_DISTANCE_M = 1.0e13;

    // -SOLSmokeMapAlign (issue #25): a body must project within this many pixels of its own orbit line
    inline constexpr double MAP_ALIGN_SMOKE_TOLERANCE_PX = 1.0;

    // Move / Patrol routes (SDD 20 section 2.3): corner planning and the map's route input
    namespace Route
    {
        // A corner turning by more than this (150 deg) is a full stop instead of a fly-through
        inline constexpr double STOP_TURN_ANGLE_RAD = UE_DOUBLE_PI * 150.0 / 180.0;
        // A fly-through's capture radius is at most this fraction of the shorter adjacent leg, and never below
        // MIN_CAPTURE_RADIUS_M (the pass speed follows from it, the turn angle and the G budget)
        inline constexpr double CAPTURE_LEG_FRACTION = 0.25;
        inline constexpr double MIN_CAPTURE_RADIUS_M = 1000.0;
        // Ctrl+right click (undo the last waypoint): at most this cursor travel and press time, else an orbit drag
        inline constexpr double UNDO_CLICK_MAX_TRAVEL_PX = 4.0;
        inline constexpr double UNDO_CLICK_MAX_S = 0.25;
        // In AUTO, flight input held only on Shift keys (thrust up by default) counts after this long, so the Shift+M /
        // Shift+J map chords do not cancel the autopilot
        inline constexpr double SHIFT_CHORD_GRACE_S = 0.5;

        // Route overlay (SDD 20 section 2.4): style alphas (active 1, draft ~0.4, paused and finished ~0.3)
        inline constexpr double OVERLAY_ALPHA_ACTIVE = 1.0;
        inline constexpr double OVERLAY_ALPHA_DRAFT = 0.4;
        inline constexpr double OVERLAY_ALPHA_PAUSED = 0.3;
        inline constexpr double OVERLAY_ALPHA_FINISHED = 0.3;
        // LT directional fade along a leg: exp(-OVERLAY_FADE_RATE (1 - t)), dimmest at the leg's start
        inline constexpr double OVERLAY_FADE_RATE = 2.0;
        // Per-frame bounds: legs looked at (projected), legs drawn, and triangles in the one batch (sized once)
        inline constexpr int32 OVERLAY_MAX_LEGS_CONSIDERED = 512;
        inline constexpr int32 OVERLAY_MAX_LEGS_DRAWN = 128;
        inline constexpr int32 OVERLAY_MAX_TRIANGLES = 16384;
        // Waypoint markers drawn per route (nearest the target first), and the active route's share of the triangles
        // (the rest is kept for the draft, so the plan being edited is never starved)
        inline constexpr int32 OVERLAY_MAX_MARKERS_DRAWN = 256;
        inline constexpr double OVERLAY_ROUTE_TRIANGLE_SHARE = 0.75;
        // A leg's fade is tessellated into one piece per this many pixels on screen, at most OVERLAY_MAX_SUBDIVISIONS
        inline constexpr double OVERLAY_PX_PER_SUBDIVISION = 32.0;
        inline constexpr int32 OVERLAY_MAX_SUBDIVISIONS = 16;
        // Patrol Loop closing leg: dash and gap lengths (pixels at a 720 px canvas), at most this many dashes per leg
        inline constexpr double OVERLAY_DASH_PX = 10.0;
        inline constexpr double OVERLAY_GAP_PX = 7.0;
        inline constexpr int32 OVERLAY_MAX_DASHES = 256;
    }

    // Jump map camera: horizontal field of view (degrees); narrower than the ship's 90 deg to limit edge distortion
    inline constexpr float MAP_CAMERA_FOV_DEG = 60.0f;

    // Jump map camera: right-drag orbit per mouse pixel (rad). 0.005 turns 180 deg over ~630 px (half a 1280 px
    // screen) and sweeps the whole pitch range in ~600 px: quick but still fine-grained for a single-pixel nudge
    inline constexpr double MAP_ORBIT_RAD_PER_PIXEL = 0.005;

    // Orbit lines (SDD 17): segment count range per orbit; the count is chosen so the chord error stays within
    // ORBIT_LINE_SAGITTA_RADIUS_FRACTION of the body's radius
    inline constexpr int32 ORBIT_LINE_MIN_SEGMENTS = 128;
    inline constexpr int32 ORBIT_LINE_MAX_SEGMENTS = 8192;

    // Orbit lines: largest chord error, and the drift that triggers a rebuild, as a fraction of the body's radius
    inline constexpr double ORBIT_LINE_SAGITTA_RADIUS_FRACTION = 0.05;

    // Orbit lines: rebuild tolerance as a fraction of the semi-major axis when the body's radius is unknown
    inline constexpr double ORBIT_LINE_REBUILD_FALLBACK_FRACTION = 1.0e-6;

    // Orbit lines: drift rebuilds allowed per frame (round robin)
    inline constexpr int32 ORBIT_LINE_MAX_REBUILDS_PER_FRAME = 4;

    // Orbit lines: whole dashes per orbit (an integer, so the loop closes on a whole dash) and their lit fraction
    inline constexpr int32 ORBIT_LINE_DASH_COUNT = 120;
    inline constexpr double ORBIT_LINE_DASH_DUTY = 0.6;

    // Orbit lines: multi-scale dashes (SDD 17 Amendment 1). Up close, a dash of the 120 per orbit spans millions of km,
    // so the dash period is halved in power-of-two levels (crossfaded) while it is longer than DASH_MIN_PERIOD_PX on
    // screen, up to DASH_MAX_LEVEL halvings (the float phase keeps ~1e-3 dash precision there); beyond that the line
    // fades to solid between DASH_SOLID_START_PX and DASH_SOLID_END_PX of period, so it never vanishes in a gap
    inline constexpr double ORBIT_LINE_DASH_MIN_PERIOD_PX = 24.0;
    inline constexpr int32 ORBIT_LINE_DASH_MAX_LEVEL = 8;
    inline constexpr double ORBIT_LINE_DASH_SOLID_START_PX = 200.0;
    inline constexpr double ORBIT_LINE_DASH_SOLID_END_PX = 800.0;

    // Orbit lines: width at the reference viewport height (scaled with the height), and the anti-aliasing feather
    inline constexpr double ORBIT_LINE_WIDTH_PX = 1.5;
    inline constexpr double ORBIT_LINE_REFERENCE_HEIGHT_PX = 1080.0;
    inline constexpr double ORBIT_LINE_AA_MARGIN_PX = 1.0;

    // Orbit lines: fully opaque within FADE_START semi-major axes of the camera, gone beyond FADE_END (log-linear)
    inline constexpr double ORBIT_LINE_FADE_START_SMA = 20.0;
    inline constexpr double ORBIT_LINE_FADE_END_SMA = 200.0;

    // Orbit lines: the targeted (highlighted) line never fades below this alpha
    inline constexpr double ORBIT_LINE_HIGHLIGHT_MIN_ALPHA = 0.35;

    // Orbit lines: the largest named asteroids that get permanent lines
    inline constexpr int32 ORBIT_LINE_NAMED_MINOR_COUNT = 50;

    // Orbit lines: occluder spheres passed to the shader, and the relative bias of the 1:1-zone scene-depth test
    inline constexpr int32 ORBIT_LINE_MAX_OCCLUDERS = 8;
    inline constexpr double ORBIT_LINE_DEPTH_BIAS = 1.0e-3;

    // Orbit lines: normal and targeted line colors (alpha is the line's peak opacity)
    inline const FLinearColor ORBIT_LINE_COLOR(0.25f, 0.42f, 0.75f, 0.6f);
    inline const FLinearColor ORBIT_LINE_HIGHLIGHT_COLOR(0.95f, 0.78f, 0.25f, 0.9f);

    // Orbit lines: the named asteroids' lines, dimmer so their 50 orbits do not swamp the inner system (17b tuning)
    inline const FLinearColor ORBIT_LINE_MINOR_COLOR(0.42f, 0.42f, 0.5f, 0.12f);

    // Orbit lines: smallest angular radius (rad) of an occluder sphere passed to the shader, about half a pixel at
    // 1920 px and a 90 deg horizontal field of view; smaller spheres cannot hide a visible part of a line
    inline constexpr double ORBIT_LINE_MIN_OCCLUDER_ANGULAR_RADIUS_RAD = 5.0e-4;

    // Orbit lines: custom primitive data floats per line component (M_SOLOrbitLine): 0-2 parent - viewpoint (high
    // floats, m, ecliptic), 3 semi-major axis (m), 4-6 the low floats, 7 highlight (0 or 1), 8-11 the line color
    inline constexpr int32 ORBIT_LINE_CUSTOM_DATA_FLOATS = 12;

    // Orbit lines: all custom primitive data floats per line (SDD 17 Amendment 3): the 12 above, then 12-15 the
    // rotation from the built orientation to the current one (quaternion x, y, z, w; ecliptic), 16 shared parent
    // (1 = the parent offset comes from OrbitLineMpcParams::SHARED_PARENT_*, used by the Sun-parented asteroid lines)
    inline constexpr int32 ORBIT_LINE_CUSTOM_DATA_TOTAL_FLOATS = 20;

    // Orbit lines: translucency sort priority of the line components. Since issue #25 the lines are drawn in their own
    // after-motion-blur pass, composited over all other translucency (ring discs, haze) by pass order, so this only
    // orders the lines among themselves (lines over ring discs by design, SDD 17 open question 7)
    inline constexpr int32 ORBIT_LINE_TRANSLUCENCY_SORT_PRIORITY = 100;

    // Orbit lines: render bounds radius (cm) of every line component around the Unreal origin; covers the farthest
    // log-compressed placement (Eris at ~68 AU lands near 1.9e11 cm), so a line is never frustum- or distance-culled
    inline constexpr double ORBIT_LINE_BOUNDS_RADIUS_CM = 4.0e11;

    // Vector parameters of Paths::ORBIT_LINE_PARAMETER_COLLECTION (SDD 17), written by ASOLOrbitLineVisuals; the
    // names must match Tools/OrbitLines/create_orbit_line_assets.py
    namespace OrbitLineMpcParams
    {
        // (viewpoint render location cm, max render distance cm), every frame
        inline constexpr const TCHAR* FRAME = TEXT("OrbitLineFrame");
        // (width px at 1080 p, dash duty, fade start SMA, fade end SMA), once at BeginPlay
        inline constexpr const TCHAR* STYLE = TEXT("OrbitLineStyle");
        // (highlight min alpha, feather margin px, depth bias, scene-depth trust limit cm), every frame
        inline constexpr const TCHAR* STYLE2 = TEXT("OrbitLineStyle2");
        // The Sun minus the viewpoint as a double-float pair (high xyz, low xyz; m, ecliptic), every frame, for the
        // asteroid lines (whose own custom data then changes only with the highlight)
        inline constexpr const TCHAR* SHARED_PARENT_HIGH = TEXT("OrbitLineSharedParentHigh");
        inline constexpr const TCHAR* SHARED_PARENT_LOW = TEXT("OrbitLineSharedParentLow");
        // (dash min period px, dash max level, solid start px, solid end px), once at BeginPlay
        inline constexpr const TCHAR* DASH = TEXT("OrbitLineDash");
        // OrbitLineOccluder0..7: (viewpoint-relative centre m, ecliptic; radius m), every frame (radius 0 = unused)
        inline constexpr const TCHAR* OCCLUDERS[] = {
            TEXT("OrbitLineOccluder0"), TEXT("OrbitLineOccluder1"), TEXT("OrbitLineOccluder2"),
            TEXT("OrbitLineOccluder3"), TEXT("OrbitLineOccluder4"), TEXT("OrbitLineOccluder5"),
            TEXT("OrbitLineOccluder6"), TEXT("OrbitLineOccluder7") };
    }

    // Jump map camera: middle/Shift+right-drag pan per mouse pixel as a fraction of camera distance. 0.0015 roughly
    // keeps the ground plane under the cursor in a top-down view at 720 p (visible half-height D * tan(~17.6 deg) /
    // 360 px)
    inline constexpr double MAP_PAN_DISTANCE_FRACTION_PER_PIXEL = 0.0015;

    // Jump map body icons (SDD 3 Appendix G): diameter of a body's icon disc at a 720 px canvas (scaled with the HUD's
    // UI scale); while a body cross-fades the disc grows to cover its mesh, so it never shrinks below the mesh size
    inline constexpr double MAP_ICON_DIAMETER_PX = 10.0;

    // Jump map picking: minimum on-screen pick radius of a body (pixels at a 720 px canvas), so a body stays
    // comfortably clickable when it is icon-sized (SOLMapPickRadius::ComputeMapPickRadiusM)
    inline constexpr double MAP_MIN_PICK_RADIUS_PX = 12.0;

    // Jump map destination picking (SDD 3 section 3 step 4): Shift + mouse movement previews the height offset along
    // Up at this fraction of the map camera's distance to the locked XY point per pixel of vertical mouse travel
    // (mouse up = +Up). The scale is frozen when the preview starts, so the height is a pure function of the cursor's
    // vertical travel since then: moving back retraces it exactly, it cannot produce NaN, and it scales with the zoom.
    // 0.001 is close to one screen pixel's world size at that depth (60 deg FOV at 720 p is ~0.0009 of the distance
    // per pixel), so the guide line's tip roughly follows the cursor in an edge-on view: 100 px = 10% of the distance
    inline constexpr double MAP_HEIGHT_DISTANCE_FRACTION_PER_PIXEL = 0.001;

    // Jump map destination picking works in ecliptic axes: the planar offset along X/Y, the height along Up (SDD 3
    // section 3). Shared by the map mode's pick math, the pick overlay and the pick smoke script
    inline const FVector3d MAP_PICK_ECLIPTIC_X(1.0, 0.0, 0.0);

    // Map / route canvas overlays: points closer than this in front of the camera (cm) are clipped
    inline constexpr double MAP_OVERLAY_NEAR_PLANE_CM = 10.0;
    inline const FVector3d MAP_PICK_ECLIPTIC_Y(0.0, 1.0, 0.0);
    inline const FVector3d MAP_PICK_UP(0.0, 0.0, 1.0);

    // Jump execution (SDD 3 Appendix I): the warp sequence lasts WARP_DURATION_S real seconds (SDD 1's ~2-4 s); the
    // chase camera's FOV pulses from its current value to WARP_PEAK_FOV_DEG and back, and the streak intensity (the
    // HUD's radial streaks and the warp post-process blend weight) peaks at WARP_PEAK_STREAK_INTENSITY mid-sequence
    inline constexpr double WARP_DURATION_S = 3.0;
    inline constexpr double WARP_PEAK_FOV_DEG = 135.0;
    inline constexpr double WARP_PEAK_STREAK_INTENSITY = 1.0;

    // Jump warp post-process, blended in by the streak intensity (engine features only, no material asset): strong
    // radial chromatic aberration (engine maximum 5), a full vignette and a cool blue-white tint
    inline constexpr float WARP_FRINGE_INTENSITY = 5.0f;
    inline constexpr float WARP_VIGNETTE_INTENSITY = 1.0f;
    inline const FLinearColor WARP_SCENE_TINT(0.7f, 0.85f, 1.0f);

    // Jump warp radial streaks (HUD canvas lines from the screen center), a fixed table built once
    inline constexpr int32 WARP_STREAK_COUNT = 160;

    // Surface lock (SDD 4): auto-engage range above the nearest body's surface, and the Auto release factor (warn above
    // the range, release above range * factor)
    inline constexpr double SURFACE_LOCK_AUTO_RANGE_M = 1.0e4;
    inline constexpr double SURFACE_LOCK_AUTO_RELEASE_FACTOR = 1.25;

    // Surface lock (SDD 4): manual (key-press) range is max(this floor, body radius); release above range * factor
    inline constexpr double SURFACE_LOCK_MANUAL_RANGE_MIN_M = 1.0e6;
    inline constexpr double SURFACE_LOCK_MANUAL_RELEASE_FACTOR = 1.25;

    // Combat (SDD 7), pulse guns: shots per second while the trigger is held, muzzle speed added to the ship's velocity,
    // damage per bolt, bolt lifetime, and the bolt's hit radius (added to every target and rock it is swept against)
    inline constexpr double COMBAT_FIRE_RATE_PER_S = 8.0;
    inline constexpr double COMBAT_MUZZLE_SPEED_MPS = 1500.0;
    inline constexpr double COMBAT_BOLT_DAMAGE = 10.0;
    inline constexpr double COMBAT_BOLT_LIFETIME_S = 3.5;
    inline constexpr double COMBAT_BOLT_HIT_RADIUS_M = 0.5;

    // Combat: the bolts from both guns converge on the camera ray this far ahead of the camera (the crosshair point)
    inline constexpr double COMBAT_CONVERGENCE_DISTANCE_M = 1000.0;

    // Combat: right gun muzzle in ship-local metres (X forward, Y right, Z up; the left gun mirrors Y), on the wing roots
    // of the placeholder ship; the guns fire alternately
    inline const FVector3d COMBAT_GUN_MUZZLE_OFFSET_M(0.5, 3.0, -0.3);

    // Combat: hard cap on live bolts (a shot beyond it is dropped) and on shots one gun step may fire (a long hitch must
    // not dump a burst; capped shots are not banked)
    inline constexpr int32 COMBAT_MAX_BOLTS = 20000;
    inline constexpr int32 COMBAT_MAX_SHOTS_PER_FRAME = 8;

    // Combat visuals: particles the bolt Niagara system spawns once (its CPU sim walks every one of them each frame it
    // runs), i.e. the most bolts drawn at once. Deliberately far below COMBAT_MAX_BOLTS: the player's guns keep about
    // 8 shots/s x 3.5 s = 28 bolts alive, so this leaves ~70x headroom for future shooters. Bolts past it (oldest
    // first are drawn) still fly and hit; they are just not drawn, and the visuals log that once
    inline constexpr int32 COMBAT_BOLT_RENDER_CAP = 2048;

    // Combat: a bolt farther than this from the player ship is removed (it can no longer matter to the player, and it
    // would stretch the sweep's broad-phase box, e.g. bolts left behind by a jump). A bolt fired from a ship at rest
    // reaches 1.5 km/s x 3.5 s = 5.25 km, so only bolts the ship has run away from at high speed are cut short
    inline constexpr double COMBAT_BOLT_MAX_RANGE_M = 20000.0;

    // Combat, targets: shield and health at spawn, shield regeneration delay after the last hit and rate, hit radius,
    // and how long the hit flash takes to fade
    inline constexpr double COMBAT_TARGET_SHIELD = 100.0;
    inline constexpr double COMBAT_TARGET_HEALTH = 100.0;
    inline constexpr double COMBAT_TARGET_SHIELD_REGEN_DELAY_S = 3.0;
    inline constexpr double COMBAT_TARGET_SHIELD_REGEN_PER_S = 25.0;
    inline constexpr double COMBAT_TARGET_RADIUS_M = 10.0;
    inline constexpr double COMBAT_HIT_FLASH_DURATION_S = 0.15;

    // Combat, target drops: distance ahead of the ship, minimum time between drops, and the pool size (the oldest
    // target is evicted when a drop finds the pool full)
    inline constexpr double COMBAT_TARGET_DROP_DISTANCE_M = 200.0;
    inline constexpr double COMBAT_TARGET_DROP_COOLDOWN_S = 0.25;
    inline constexpr int32 COMBAT_MAX_TARGETS = 1000;

    // Combat, broad phase: cell size of the target grid and of the minor-body (belt asteroid and ring rock) grid. Both
    // are built in the ship-centred sweep frame, where a bolt moves ~25 m per 60 fps frame, so a bolt segment touches
    // one to eight cells; ring rocks (~100 per 1 km ring cell) land a few per cell
    inline constexpr double COMBAT_TARGET_GRID_CELL_M = 250.0;
    inline constexpr double COMBAT_MINOR_BODY_GRID_CELL_M = 250.0;

    // Combat, minor bodies: a bolt is swept against a rock's mean radius times this scale, never below the minimum
    inline constexpr double COMBAT_MINOR_BODY_HIT_RADIUS_SCALE = 1.0;
    inline constexpr double COMBAT_MINOR_BODY_MIN_HIT_RADIUS_M = 1.0;

    // Combat: anything that moved farther than this in one frame in the ship-centred sweep frame (a jump teleport,
    // extreme time warp) is not swept that frame: it is tested at its new position only (a target or rock) or skips hit
    // tests for the frame (a bolt). A ring rock reassigned to another cell is detected exactly instead (its render
    // fragment's Generation changes) and also tested at its new position only, however short the move
    inline constexpr double COMBAT_MAX_SWEEP_STEP_M = 5.0e4;

    // Combat: queued one-shot events kept per update for the visuals and audio (extra events that frame are dropped)
    inline constexpr int32 COMBAT_MAX_EVENTS_PER_UPDATE = 4096;

    // -SOLCombatDemoHold (verification, issue #15): the ship strafes from this fraction of the hold to its end, with this
    // thrust input (ship axes: X forward, Y right, Z up), so the held target also crosses the screen
    inline constexpr double COMBAT_DEMO_HOLD_STRAFE_START_FRACTION = 0.5;
    inline const FVector3d COMBAT_DEMO_HOLD_STRAFE_THRUST(0.0, 1.0, 0.0);

    // Combat: bolts handed to one parallel sweep task (below this the sweep runs on the game thread alone)
    inline constexpr int32 COMBAT_SWEEP_MIN_BOLTS_PER_TASK = 256;

    // Combat visuals (SDD 7, 7c): pooled one-shot effect components per kind (a new effect reuses the oldest), and how
    // long each kind's component is kept riding with what it hit (matches each system's loop duration)
    inline constexpr int32 COMBAT_FX_IMPACT_POOL_SIZE = 32;
    inline constexpr int32 COMBAT_FX_SHIELD_BREAK_POOL_SIZE = 8;
    inline constexpr int32 COMBAT_FX_SPARK_POOL_SIZE = 32;
    inline constexpr int32 COMBAT_FX_EXPLOSION_POOL_SIZE = 8;
    inline constexpr double COMBAT_FX_IMPACT_DURATION_S = 1.0;
    inline constexpr double COMBAT_FX_SHIELD_BREAK_DURATION_S = 1.5;
    inline constexpr double COMBAT_FX_SPARK_DURATION_S = 0.8;
    inline constexpr double COMBAT_FX_EXPLOSION_DURATION_S = 6.0;

    // Combat visuals: impact flash tint while the hit target still has shield (blue) and once it is down to hull (orange)
    inline const FLinearColor COMBAT_FX_SHIELD_HIT_TINT(0.35f, 0.8f, 2.0f);
    inline const FLinearColor COMBAT_FX_HULL_HIT_TINT(1.0f, 0.55f, 0.22f);

    // Niagara User Parameter names of Paths::COMBAT_BOLT_NIAGARA_SYSTEM (one CPU-sim particle per bolt slot, fed from arrays)
    namespace CombatBoltParams
    {
        inline constexpr const TCHAR* POSITIONS_CM = TEXT("BoltPositionsCm");   // Float3 array: component-local cm
        inline constexpr const TCHAR* VELOCITIES = TEXT("BoltVelocities");      // Float3 array: streak direction (cm/s)
        inline constexpr const TCHAR* COUNT = TEXT("BoltCount");                // Live entries of both arrays
        inline constexpr const TCHAR* MAX_COUNT = TEXT("BoltMaxCount");         // Particles spawned once
                                                                                  // (COMBAT_BOLT_RENDER_CAP)
    }

    // Niagara User Parameter names of the one-shot combat effect systems (impact and spark use both, the others none)
    namespace CombatEffectParams
    {
        inline constexpr const TCHAR* DIRECTION = TEXT("Direction");            // Unit bolt direction, Unreal axes
        inline constexpr const TCHAR* TINT = TEXT("Tint");                      // Flash and spark colour
    }

    // Per-instance custom data of Paths::COMBAT_TARGET_MATERIAL: shield fraction, health fraction, hit flash
    inline constexpr int32 COMBAT_TARGET_CUSTOM_DATA_FLOATS = 3;

    // Combat audio (SDD 7, 7d): tuning of one placeholder sound kind
    struct FSOLCombatSoundTuning
    {
        const TCHAR* Path;      // USoundWave asset (optional: a missing one silences only this kind, with a warning)
        int32 Voices;           // Pooled audio components; a new sound reuses the oldest (= at most this many at once)
        double MinIntervalS;    // A sound of this kind starts at most once per this many seconds (more are dropped)
        float Volume;           // Volume multiplier
        bool bIsLongRange;      // Uses the long-range attenuation (explosions) instead of the normal one
    };

    // Combat audio: one entry per ESOLCombatSound, in its order (Shot, ShieldHit, HullHit, ShieldBreak, Explosion,
    // Spark, TargetDrop). Synthesized placeholder WAVs, mono 44.1 kHz; real audio arrives in the Part 8 pass. The gun
    // fires 8/s, so the shot interval lets every shot play once per frame at most while holding a few voices
    inline constexpr FSOLCombatSoundTuning COMBAT_SOUNDS[] =
    {
        { TEXT("/Game/SOL/Combat/Audio/S_SOLShot.S_SOLShot"),               3, 0.06, 0.55f, false },
        { TEXT("/Game/SOL/Combat/Audio/S_SOLShieldHit.S_SOLShieldHit"),     4, 0.05, 0.7f,  false },
        { TEXT("/Game/SOL/Combat/Audio/S_SOLHullHit.S_SOLHullHit"),         4, 0.05, 0.8f,  false },
        { TEXT("/Game/SOL/Combat/Audio/S_SOLShieldBreak.S_SOLShieldBreak"), 2, 0.1,  1.0f,  false },
        { TEXT("/Game/SOL/Combat/Audio/S_SOLExplosion.S_SOLExplosion"),     3, 0.1,  1.0f,  true  },
        { TEXT("/Game/SOL/Combat/Audio/S_SOLSpark.S_SOLSpark"),             3, 0.05, 0.5f,  false },
        { TEXT("/Game/SOL/Combat/Audio/S_SOLTargetDrop.S_SOLTargetDrop"),   2, 0.1,  0.6f,  false },
    };

    // Combat audio attenuation (render cm): full volume inside the inner radius, fading to silence over the falloff
    // distance beyond it. Targets drop 200 m ahead, so a hit there is still near full volume; explosions carry ~4x further
    inline constexpr float COMBAT_AUDIO_INNER_RADIUS_CM = 30000.0f;              // 300 m
    inline constexpr float COMBAT_AUDIO_FALLOFF_CM = 200000.0f;                  // 2 km
    inline constexpr float COMBAT_AUDIO_LONG_INNER_RADIUS_CM = 120000.0f;        // 1.2 km
    inline constexpr float COMBAT_AUDIO_LONG_FALLOFF_CM = 800000.0f;             // 8 km

    // Main-menu camera (SDD 8, 8b): it sits MENU_CAMERA_DISTANCE_RADII body radii from the shown body's centre, on the
    // lit side (MENU_CAMERA_BASE_AZIMUTH_DEG from the sunward direction around the ecliptic pole, swaying by
    // MENU_CAMERA_SWAY_AZIMUTH_DEG over MENU_CAMERA_SWAY_PERIOD_S real seconds) and MENU_CAMERA_ELEVATION_DEG above the
    // ecliptic, and turns MENU_CAMERA_LOOK_YAW_OFFSET_DEG away so the body sits right of the menu column
    inline constexpr double MENU_CAMERA_DISTANCE_RADII = 4.5;
    inline constexpr double MENU_CAMERA_BASE_AZIMUTH_DEG = 25.0;
    inline constexpr double MENU_CAMERA_SWAY_AZIMUTH_DEG = 20.0;
    inline constexpr double MENU_CAMERA_SWAY_PERIOD_S = 120.0;
    inline constexpr double MENU_CAMERA_ELEVATION_DEG = 12.0;
    inline constexpr float MENU_CAMERA_FOV_DEG = 60.0f;
    inline constexpr float MENU_CAMERA_LOOK_YAW_OFFSET_DEG = -16.0f;

    // Shared hologram style (SDD 20 section 2.4; #21 reuses it). Glow line, pixels at a 720 px canvas: a bright core
    // and a halo fading linearly from GLOW_HALO_INTENSITY of the core to nothing (a cheap stand-in for LT's
    // 0.8 exp(-2 max(0, d - 0.5)) + 0.2 exp(-(0.2 d)^0.75) profile)
    namespace Hologram
    {
        inline constexpr const TCHAR* CVAR_ENABLED = TEXT("sol.Hologram");             // Whole look on / off
        inline constexpr const TCHAR* CVAR_POST_STRENGTH = TEXT("sol.HologramPostStrength"); // Post-process strength
        inline constexpr double GLOW_CORE_HALF_PX = 0.9;
        inline constexpr double GLOW_HALO_HALF_PX = 4.5;
        inline constexpr double GLOW_HALO_INTENSITY = 0.15;     // Halved in issue #31 (was 0.3)

        // LT hologram fragment (proxy bodies and rings, Tools/Map/create_hologram_assets.py reads these defaults):
        // Fresnel rim 1 - |N.V| x time noise 1 - NOISE_LOG_SCALE log(1 - noise) x 1 px scanlines
        // 1 + SCANLINE_AMPLITUDE sin(PI y), additive, colour x COLOR_GAIN; contour bands
        // CONTOUR_GAIN exp(-CONTOUR_SHARPNESS |spec - k|) on latitude and longitude lines
        inline constexpr double NOISE_LOG_SCALE = 0.1;
        inline constexpr double NOISE_RATE_HZ = 30.0;               // Noise pattern changes per second
        inline constexpr double SCANLINE_AMPLITUDE = 0.2;
        inline constexpr double COLOR_GAIN = 2.0;
        inline constexpr double CONTOUR_GAIN = 2.0;
        inline constexpr double CONTOUR_SHARPNESS = 4.0;
        inline constexpr double CONTOUR_LATITUDE_BANDS = 6.0;       // Latitude lines from pole to pole
        inline constexpr double CONTOUR_LONGITUDE_BANDS = 12.0;     // Meridians around the body
        inline constexpr double CONTOUR_AMOUNT = 0.35;              // Contour lines' share against the rim
        inline constexpr double RING_FILL = 0.12;                   // Ring disc base glow
        inline constexpr double RING_BANDS = 8.0;                   // Contour bands across a ring disc
        inline constexpr double PROXY_INTENSITY_PLANET = 0.5;       // Emissive scale of a planet proxy
        inline constexpr double PROXY_INTENSITY_MOON = 0.35;
        inline constexpr double PROXY_INTENSITY_SUN = 0.5;
        inline constexpr double PROXY_INTENSITY_RING = 0.5;
        inline const FLinearColor PROXY_COLOR_BODY(0.2f, 0.7f, 1.0f, 1.0f);    // LT Primary
        inline const FLinearColor PROXY_COLOR_SUN(1.0f, 0.3f, 0.1f, 1.0f);     // LT Secondary

        // Map post-process over the scene colour (after tonemapping; the HUD canvas is drawn over it untouched):
        // tint toward Primary, 1 px scanlines, a global flicker and a small horizontal jitter of a few rows, all
        // scaled by sol.HologramPostStrength (default POST_STRENGTH_DEFAULT, clamped to [0, POST_STRENGTH_MAX])
        inline constexpr double POST_TINT = 0.25;
        inline constexpr double POST_SCANLINE = 0.06;
        inline constexpr double POST_FLICKER = 0.03;
        inline constexpr double POST_JITTER_PX = 0.75;
        inline constexpr double POST_JITTER_ROW_FRACTION = 0.03;    // Fraction of 2 px rows jittered at any time
        inline constexpr double POST_STRENGTH_DEFAULT = 1.0;
        inline constexpr double POST_STRENGTH_MAX = 2.0;
        inline constexpr float POST_PRIORITY = 10.0f;               // Over ASOLBodyVisuals' unbound post-process
        inline constexpr float POST_EXPOSURE_BIAS = 0.0f;           // Fixed exposure in the map (dark-sky exposure)

        // Ecliptic grid through the map camera focus (LT's two scales 16 and 64: ratio 4). Minor spacing is the
        // largest power of GRID_SCALE_RATIO metres not above cameraDistance / GRID_MINOR_LINES_PER_VIEW, major =
        // minor x ratio; the minor lines fade out across a level. Lines reach GRID_EXTENT_MAJOR_CELLS major spacings
        // from the focus and fade with the LT distance field exp(-2 d^2) + 0.2 exp(-(0.2 d)^0.75) (d in major
        // spacings), tapered to nothing over the outer (1 - GRID_EDGE_TAPER_START) of the extent
        inline constexpr double GRID_SCALE_RATIO = 4.0;
        inline constexpr double GRID_MINOR_LINES_PER_VIEW = 16.0;
        inline constexpr double GRID_EXTENT_MAJOR_CELLS = 4.0;
        inline constexpr double GRID_EDGE_TAPER_START = 0.75;
        inline constexpr double GRID_ALPHA = 0.1;                   // LT: Primary at 0.1 alpha (major lines)
        inline constexpr double GRID_MINOR_ALPHA_SCALE = 0.5;       // Minor lines relative to the major ones
        inline constexpr int32 GRID_PIECES_PER_LINE = 32;           // Fade tessellation along a grid line (world)

        // Stems from bodies and waypoints down (or up) to the grid plane: drawn when the height is at least
        // STEM_MIN_HEIGHT_FRACTION of the camera distance; alpha STEM_ALPHA x max(STEM_MIN_ALPHA, grid fade at the
        // foot's distance from the focus), clamped to 1
        inline constexpr double STEM_MIN_HEIGHT_FRACTION = 1.0e-3;
        inline constexpr double STEM_ALPHA = 0.5;
        inline constexpr double STEM_MIN_ALPHA = 0.15;
        inline constexpr double STEM_FOOT_RADIUS_PX = 3.0;          // Ring at a stem's foot, 720 px canvas
        inline constexpr int32 MAX_STEMS = 256;
        inline constexpr int32 OVERLAY_MAX_TRIANGLES = 24576;       // Grid plus stems, one additive batch

        // Flight HUD shape renderer (SDD 21 phase A): one batched Slate custom-vertex element drawn with
        // HUD_SHAPE_MATERIAL (Tools/UI/create_hud_shape_material.py reads these defaults). Every shape is one quad
        // padded by HUD_GLOW_PAD_PX; the material computes the pixel distance d to the shape and draws LT's glow
        // HUD_GLOW_CORE_WEIGHT exp(-HUD_GLOW_CORE_FALLOFF max(0, d - 0.5))
        // + HUD_GLOW_HALO_WEIGHT exp(-(HUD_GLOW_HALO_SCALE d)^HUD_GLOW_HALO_POWER), times colour x HUD_COLOR_GAIN, then
        // LT's tone curve 1 - exp(-2 c^1.25) and blue grade (HUD_GRADE_BLUE_1..3 as a Bezier from 0 to 1), additive
        inline constexpr const TCHAR* CVAR_HUD_RENDERER = TEXT("sol.HudRenderer");     // 1 = shape renderer, 0 = Canvas
        inline constexpr const TCHAR* CVAR_HUD_COMPOSITE = TEXT("sol.HudComposite");   // 1 = grain and scanlines on
        inline constexpr double HUD_GLOW_CORE_HALF_PX = 0.5;        // Full-strength core around the centre line
        inline constexpr double HUD_GLOW_CORE_WEIGHT = 0.9;         // LT 0.8; + halo = 1 at the core (#31)
        inline constexpr double HUD_GLOW_CORE_FALLOFF = 3.0;        // Issue #31 halved the glow: LT's 2.0 -> 3.0 and
        inline constexpr double HUD_GLOW_HALO_WEIGHT = 0.1;         // halo 0.2 -> 0.1 (SDD 21 Amendment 4)
        inline constexpr double HUD_GLOW_HALO_SCALE = 0.2;
        inline constexpr double HUD_GLOW_HALO_POWER = 0.75;
        inline constexpr float HUD_GLOW_PAD_PX = 32.0f;             // Quad padding around a shape (LT kExpansion)
        inline constexpr double HUD_PAD_FADE_START = 0.5;           // The halo fades out from this fraction of the pad
        inline constexpr double HUD_TONE_GAIN = 2.0;                // LT tone curve 1 - exp(-GAIN c^POWER)
        inline constexpr double HUD_TONE_POWER = 1.25;
        inline constexpr int32 HUD_VERTICES_PER_SHAPE = 4;          // One quad per shape
        inline constexpr int32 HUD_INDICES_PER_SHAPE = 6;
        inline constexpr uint64 HUD_PILOT_MAX_FRAME_LAG = 2;        // Older pilot snapshots are not drawn (SDD 21 D4)

        // Value noise of the jitter (CPU and material alike): hash frac(sin(i x FREQUENCY) x SCALE) of the lattice
        // index wrapped to PERIOD (keeps GPU sin accurate), and the time wrapped to TIME_WRAP_S
        inline constexpr float HUD_NOISE_HASH_FREQUENCY = 127.1f;
        inline constexpr float HUD_NOISE_HASH_SCALE = 43758.5453f;
        inline constexpr float HUD_NOISE_LATTICE_PERIOD = 289.0f;
        inline constexpr float HUD_TIME_WRAP_S = 1000.0f;
        inline constexpr double HUD_COLOR_GAIN = 2.0;               // LT doubles colours so cores clip toward white
        inline constexpr double HUD_DISPLAY_GAMMA = 2.2;            // Slate's display gamma: LT's maths is in display
                                                                    // space, Slate's material output is linear
        inline constexpr double HUD_GRADE_BLUE_1 = 0.40;            // LT gradeBlue Bezier control points
        inline constexpr double HUD_GRADE_BLUE_2 = 0.50;
        inline constexpr double HUD_GRADE_BLUE_3 = 0.75;
        inline constexpr double HUD_PANEL_CORNER_PX = 6.0;          // Panel corner radius
        inline constexpr double HUD_PANEL_FEATHER_PX = 8.0;         // Borderless panel: the fill fades in over this
                                                                    // from the edge (SDD 21 Amendment 4)
        inline constexpr double HUD_PANEL_SCANLINE = 0.2;           // Panel fill 1 px scanlines 1 + this x strength
                                                                    // x sin(PI y)
        // Panel flicker (SDD 21 phase B): every panel's brightness x (1 - HUD_PANEL_FLICKER x strength x n), n a smooth
        // noise of the time at HUD_PANEL_FLICKER_RATE_HZ lattice cells per second, offset so it is uncorrelated with
        // the row jitter
        inline constexpr double HUD_PANEL_FLICKER = 0.08;
        inline constexpr double HUD_PANEL_FLICKER_RATE_HZ = 12.0;
        inline constexpr double HUD_PANEL_FLICKER_OFFSET = 41.0;
        inline constexpr int32 HUD_MAX_TEXTS = 64;                  // Glow-text lines per frame, reserved once
        inline constexpr int32 HUD_MAX_PANELS = 32;                 // Panel backings per frame, reserved once

        // Hologram panels behind HUD text blocks (SDD 21 phase B, borderless since issue #31): a dark translucent
        // backing (alpha-blended, for contrast) drawn as HUD_PANEL_BACKING_LAYERS stacked rounded boxes that fade in
        // over HUD_PANEL_BACKING_FEATHER_PX from the edge (SOLHudShapeMath::BackingLayer), under an additive panel
        // shape with fill HUD_PANEL_FILL in the colour x HUD_PANEL_INTENSITY and no outline, grown
        // HUD_PANEL_PADDING_PX (x UI scale) around the block's text
        inline const FLinearColor HUD_PANEL_BACKING_COLOR(0.0f, 0.01f, 0.03f, 0.45f);
        inline constexpr int32 HUD_PANEL_BACKING_LAYERS = 6;
        inline constexpr float HUD_PANEL_BACKING_FEATHER_PX = 8.0f;
        inline constexpr float HUD_PANEL_FILL = 0.06f;
        inline constexpr float HUD_PANEL_INTENSITY = 0.35f;
        inline constexpr float HUD_PANEL_PADDING_PX = 8.0f;
        inline constexpr float HUD_PANEL_QUAD_PAD_PX = 2.0f;        // A borderless panel draws nothing outside itself:
                                                                    // its quad only covers the row jitter

        // HUD layout is at a 720 px high canvas: the UI scale is the canvas height over HUD_REFERENCE_HEIGHT_PX,
        // clamped to [1, HUD_MAX_UI_SCALE] (the HUD then multiplies it by the Settings HUD scale; the hologram menu
        // frames use the clamped value alone, SDD 42 section 4.6)
        inline constexpr float HUD_REFERENCE_HEIGHT_PX = 720.0f;
        inline constexpr float HUD_MAX_UI_SCALE = 2.0f;

        // Glow text (SDD 21 Amendment 2): the engine fonts drawn by Slate over the shapes; the fill is the colour
        // moved HUD_TEXT_CORE_WHITEN toward white (a hot core), the outline HUD_TEXT_GLOW_OUTLINE font units wide in
        // the colour at HUD_TEXT_GLOW_ALPHA, over a drop shadow HUD_TEXT_SHADOW_PX (x UI scale) down-right, black at
        // HUD_TEXT_SHADOW_ALPHA. sol.HudTextGlow 0 draws plain text over the shadow
        inline constexpr const TCHAR* CVAR_HUD_TEXT_GLOW = TEXT("sol.HudTextGlow");
        inline constexpr float HUD_TEXT_CORE_WHITEN = 0.3f;
        inline constexpr int32 HUD_TEXT_GLOW_OUTLINE = 1;
        inline constexpr float HUD_TEXT_GLOW_ALPHA = 0.18f;         // Halved in issue #31 (was 0.35)
        inline constexpr float HUD_TEXT_SHADOW_PX = 1.0f;
        inline constexpr float HUD_TEXT_SHADOW_ALPHA = 0.7f;
        inline constexpr float HUD_SHAPE_KIND_STRIDE = 8192.0f;     // Kind packed as kind x stride + size parameter
        inline constexpr int32 HUD_MAX_SHAPES = 4096;               // Quads per frame, reserved once
        inline constexpr float HUD_LINE_HALF_WIDTH_PX = 0.5f;       // A 1 px glow line (LT)
        inline constexpr float HUD_DASH_FRACTION = 0.5f;            // Dashed line: dash length / period (SDD 21 C)
        inline constexpr float HUD_DASH_MAX_PERIOD_PX = 255.0f;     // Dashed line period travels in the 8-bit alpha

        // Row jitter (LT Compositor_Basic): offset.x = HUD_JITTER_MAX_FRACTION x strength x screen width
        // x exp(-HUD_JITTER_SHARPNESS n) x sign, n a smooth noise of the screen row (HUD_JITTER_ROW_CELLS cells per
        // screen height) scrolling at HUD_JITTER_SCROLL_RATE cells per second; strength is the Settings slider (default
        // 1 = half LT's 0.002, the maximum 2 = LT's)
        inline constexpr double HUD_JITTER_MAX_FRACTION = 0.001;
        inline constexpr double HUD_JITTER_SHARPNESS = 48.0;
        inline constexpr double HUD_JITTER_ROW_CELLS = 64.0;
        inline constexpr double HUD_JITTER_SCROLL_RATE = 1.0;
        inline constexpr double HUD_JITTER_SIGN_SPEED = 5.0;        // The sign's noise scrolls this much faster
        inline constexpr double HUD_JITTER_SIGN_OFFSET = 17.0;      // ...and is offset so it is uncorrelated

        // Grain and scanline composite (LT noise 0.10, lines 0.05), off by default (sol.HudComposite 0); scaled by the
        // same strength slider when on
        inline constexpr double HUD_GRAIN_AMOUNT = 0.10;
        inline constexpr double HUD_SCANLINE_AMOUNT = 0.05;

        // Prograde / retrograde markers show only above this speed relative to the reference frame (the flight HUD and
        // the Help glossary's {VelocityMarkerMinSpeed})
        inline constexpr double HUD_VELOCITY_MARKER_MIN_MPS = 0.1;
    }

    // The original Limit Theory HUD palette (LimitTheory-Original resource/script/Colors.lts; SDD 19 Amendment 3). The
    // flight mode label uses it now; ticket #21 applies it, with LT's glow shading, to the whole HUD
    namespace HudPalette
    {
        inline const FLinearColor PRIMARY(0.2f, 0.7f, 1.0f, 1.0f);      // LT Primary (blue-cyan)
        inline const FLinearColor SECONDARY(1.0f, 0.3f, 0.1f, 1.0f);    // LT Secondary (orange)
        inline const FLinearColor GOOD(0.25f, 1.0f, 0.25f, 1.0f);       // LT Good / Friendly (green)
        inline const FLinearColor BAD(1.0f, 0.1f, 0.1f, 1.0f);          // LT Bad (red)
        inline const FLinearColor HOSTILE(1.0f, 0.1f, 0.01f, 1.0f);     // LT Hostile (red-orange)
        inline const FLinearColor FREE_LOOK(1.0f, 0.82f, 0.25f, 1.0f);  // Joystick circles while free-looking (#31)
        inline constexpr float DIM_INTENSITY = 0.55f;                   // The HUD's dim tone: secondary lines (axes,
                                                                        // ticks, stalks)
        inline const FLinearColor WARP_STREAK(0.8f, 0.9f, 1.0f, 1.0f);  // The jump warp's radial streaks
    }

    // Flight HUD symbol geometry (SDD 33 section 8.1, part 33d-i), shared by the HUD and the Help glossary through
    // SOLHudSymbols' builders: sizes in px at a 720 px canvas (x the UI scale), intensities are glow factors, the
    // CANVAS_LINE_* widths are the Canvas fallback's line widths (glow lines up to 2 px are LT's 1 px line)
    namespace HudGeometry
    {
        // Reticle and virtual joystick
        inline constexpr float RETICLE_GAP_PX = 6.0f;
        inline constexpr float RETICLE_ARM_PX = 12.0f;
        inline constexpr float RETICLE_DOT_RADIUS_PX = 1.0f;          // The glow dot ring (shape renderer only)
        inline constexpr float RETICLE_DOT_INTENSITY = 0.5f;          // Dimmer than the arms: the centre stays readable
        inline constexpr float RETICLE_CANVAS_DOT_PX = 2.0f;          // The Canvas fallback's dot square (not scaled)
        inline constexpr int32 CIRCLE_SEGMENTS = 48;                  // Canvas fallback circle segments
        inline constexpr float STICK_DOT_PX = 6.0f;
        inline constexpr float STICK_DOT_INTENSITY = 0.6f;            // The stick dot (a filled glow blob)
        inline constexpr float FAINT_INTENSITY = 0.18f;               // The joystick circles

        // Canvas fallback line widths
        inline constexpr float CANVAS_LINE_THIN_PX = 1.0f;
        inline constexpr float CANVAS_LINE_PX = 1.5f;
        inline constexpr float CANVAS_LINE_THICK_PX = 2.0f;

        // Target bracket and off-screen edge arrow
        inline constexpr float BRACKET_MIN_HALF_PX = 14.0f;
        inline constexpr float BRACKET_MAX_HALF_PX = 160.0f;
        inline constexpr float BRACKET_CORNER_FRACTION = 0.4f;        // A corner leg's length, of the half size
        inline constexpr float BRACKET_INTENSITY = 0.6f;              // Bracket and edge arrow (corners add up)
        inline constexpr float EDGE_ARROW_LENGTH_PX = 14.0f;
        inline constexpr float EDGE_ARROW_WIDTH_PX = 9.0f;

        // Prograde / retrograde markers: circle radius, Canvas segments, tick ends and cross arms (of the radius)
        inline constexpr float VELOCITY_MARKER_RADIUS_PX = 9.0f;
        inline constexpr int32 VELOCITY_MARKER_SEGMENTS = 16;
        inline constexpr float PROGRADE_TICK_END_FRACTION = 2.0f;
        inline constexpr float RETROGRADE_ARM_FRACTION = 0.7f;

        // Radar scope: radius, vertical squash of the tilted disc, stalk length per unit height, blips, range rings
        // (fractions of the range), the outer ring's glow factor, axis / cone intensities, ticks, the ship chevron
        // (its rise, of its half width), contact foot ticks, the selected target's highlight ring, contact glows
        inline constexpr float RADAR_RADIUS_PX = 105.0f;
        inline constexpr float RADAR_TILT = 0.5f;
        inline constexpr float RADAR_STALK_SCALE = 0.6f;
        inline constexpr float RADAR_BLIP_PX = 4.0f;
        inline constexpr float RADAR_SELECTED_BLIP_PX = 7.0f;
        inline constexpr float RADAR_RINGS[] = { 0.25f, 0.5f, 0.75f, 1.0f };
        inline constexpr float RADAR_OUTER_RING_GAIN = 1.3f;
        inline constexpr float RADAR_AXIS_INTENSITY = 0.12f;
        inline constexpr float RADAR_CONE_INTENSITY = 0.2f;
        inline constexpr float RADAR_TICK_PX = 5.0f;
        inline constexpr float RADAR_HEADING_TICK_PX = 9.0f;
        inline constexpr float RADAR_CHEVRON_PX = 5.0f;
        inline constexpr float RADAR_CHEVRON_RISE_FRACTION = 0.6f;
        inline constexpr float RADAR_CHEVRON_INTENSITY = 0.6f;
        inline constexpr float RADAR_FOOT_HALF_PX = 2.5f;
        inline constexpr float RADAR_FOOT_INTENSITY = 0.35f;
        inline constexpr float RADAR_HIGHLIGHT_GAP_PX = 3.0f;
        inline constexpr float RADAR_BLIP_INTENSITY = 0.6f;
        inline constexpr float RADAR_STEM_INTENSITY = 0.45f;
        inline constexpr int32 RADAR_CANVAS_SEGMENTS = 12;            // Canvas fallback: segments of a blip ring

        // Fill bars (speed cap, G meter) and the Chase Down ticks
        inline constexpr float SPEED_BAR_WIDTH_PX = 240.0f;
        inline constexpr float SPEED_BAR_HEIGHT_PX = 8.0f;
        inline constexpr float BAR_TRACK_INTENSITY = 0.1f;            // A glow bar's empty track
        inline constexpr float BAR_FILL_INTENSITY = 0.45f;            // A glow bar's filled part
        inline constexpr float CHASE_TICK_OVERHANG_PX = 2.0f;         // The floor tick extends past the bar

        // Mouse-steer icon (SDD 19): radius, Canvas segments, the unscaled centre dot, the Free arrow (tip offset,
        // height and side, of the radius) and the Locked tick (offset and size, of the radius)
        inline constexpr float STEER_ICON_RADIUS_PX = 6.0f;
        inline constexpr int32 STEER_ICON_SEGMENTS = 16;
        inline constexpr float STEER_DOT_PX = 2.0f;
        inline constexpr float STEER_ARROW_TIP_X_FRACTION = 0.6f;
        inline constexpr float STEER_ARROW_HEIGHT_FRACTION = 2.0f;
        inline constexpr float STEER_ARROW_SIDE_FRACTION = 1.4f;
        inline constexpr float STEER_LOCK_OFFSET_FRACTION = 0.5f;
        inline constexpr float STEER_LOCK_TICK_FRACTION = 0.7f;

        // Jump warp streaks: each travels from WARP_STREAK_START_FRACTION to past the corner (fractions of the half
        // diagonal) once per cycle, accelerating (squared phase), lengthening up to WARP_STREAK_MAX_LENGTH_FRACTION,
        // brightening from the base brightness by the phase one and thickening up to 1 + WARP_STREAK_EXTRA_THICKNESS_PX
        inline constexpr double WARP_STREAK_START_FRACTION = 0.06;
        inline constexpr double WARP_STREAK_TRAVEL_FRACTION = 1.1;
        inline constexpr double WARP_STREAK_MIN_LENGTH_FRACTION = 0.04;
        inline constexpr double WARP_STREAK_MAX_LENGTH_FRACTION = 0.45;
        inline constexpr double WARP_STREAK_BASE_BRIGHTNESS = 0.25;
        inline constexpr double WARP_STREAK_PHASE_BRIGHTNESS = 0.75;
        inline constexpr float WARP_STREAK_EXTRA_THICKNESS_PX = 2.0f;
    }

    // A fixed gap of a hologram menu frame line (SDD 42 section 6.3): its start as a fraction of the line's length and
    // its length in px at 1x
    struct FSOLHoloGap
    {
        float AtFraction;
        float LengthPx;
    };

    // Hologram in-flight menus (SDD 42 section 7; proposed, tuned from screenshots). Pixel sizes are at a 720 px canvas
    // (x UI scale); times are real application seconds
    namespace HoloMenu
    {
        // Open / close and hover curves (section 6.1)
        inline constexpr double OPEN_S = 0.2;                     // Open time (D4a)
        inline constexpr double CLOSE_S = 0.2;                    // Close time
        inline constexpr float LINE_PHASE_END = 0.6f;             // Frame lines fully drawn at this progress
        inline constexpr float FILL_PHASE_START = 0.4f;           // Fill, backdrop and text start fading up here
        inline constexpr double HOVER_RAMP_S = 0.06;              // Hover glow rise time
        inline constexpr double HOVER_FLICKER_S = 0.15;           // Length of the hover flicker
        inline constexpr float HOVER_FLICKER_DEPTH = 0.25f;       // Flicker depth per unit of strength
        inline constexpr double HOVER_FLICKER_RATE_HZ = 30.0;     // Flicker noise speed
        inline constexpr double HOVER_FLICKER_OFFSET = 17.0;      // ... and lattice offset
        inline constexpr float HOVER_GLOW_INTENSITY = 0.9f;       // Edge glow of the hovered item
        inline constexpr float FRAME_INTENSITY = 0.5f;            // Outline frame glow (not scaled by the slider)
        inline constexpr float FRAME_HALF_WIDTH_PX = 0.5f;        // Outline line half width (a 1 px core)
        inline constexpr float CARD_TICK_PX = 16.0f;              // Corner tick length of the frameless cards (D15)
        inline constexpr int32 CARD_TICK_SEGMENTS = 8;            // Capacity of CardCornerTicks (two per corner)
        inline constexpr float FLAT_PANEL_ALPHA = 0.45f;          // Backdrop alpha at strength 0, a flat panel (D18)
        inline constexpr double IDLE_SWEEP_PERIOD_S = 4.0;        // One scanline pass from top to bottom
        inline constexpr float IDLE_SWEEP_INTENSITY = 0.08f;      // Sweep line glow per unit of strength
        inline constexpr float BACKDROP_ALPHA_PER_UNIT = 0.15f;   // Dark tint alpha per unit of strength (D20 tint)
        inline constexpr double HOVER_CUE_MIN_INTERVAL_S = 0.05;  // Shortest gap between two hover cues
        // Hover glow on a highlighted DISABLED radial sector, as a factor of the enabled one's (0 = none, SDD 42 Q13 option a)
        inline constexpr float RADIAL_DISABLED_HOVER_GLOW_SCALE = 0.0f;
        inline constexpr int32 FRAME_MAX_SHAPES = 160;           // Shape capacity of one SSOLHoloFrame
        inline constexpr int32 CARD_MAX_SHAPES = 10;              // Shapes of one frameless card: fill, 8 ticks, sweep
        inline constexpr float FRAME_FLAT_PAD_PX = 1.0f;          // Glow pad of frame lines and dots at strength 0
        inline constexpr float MIN_PIECE_PX = 1.0e-3f;            // A geometry piece shorter than this is never written
        inline constexpr float UI_SCALE_MIN = 0.1f;               // Geometry clamps a valid uiScale to [MIN, MAX]
        inline constexpr float UI_SCALE_MAX = 16.0f;              // ... so no input can stall a loop or overflow
        inline constexpr int32 MENU_LAYER_MAX_SHAPES = 512;       // Shape capacity of the HUD's menu layer

        // The faint dark tint of the menu frames and cards (D3, D20): the RGB of their backing box (alpha-blended at
        // BackdropAlpha) and of their additive panel fill, matched to the approved sample overlay-c-tint.png (an
        // interior of about sRGB (3, 9, 17) over black at strength 1)
        inline const FLinearColor BACKDROP_TINT(0.007f, 0.028f, 0.07f, 1.0f);

        // Sound cues (section 7.1): tuning of one cue
        struct FSOLHoloCueTuning
        {
            const TCHAR* Path;      // USoundWave asset (optional: a missing one silences only this cue, with a warning)
            float Volume;           // Volume multiplier
            double MinIntervalS;    // A cue starts at most once per this many seconds (more are dropped)
        };

        // One entry per cue, in the order Open, Hover, Select (CUE_OPEN / CUE_HOVER / CUE_SELECT index it). Hover is
        // the quietest and the most rate limited, as it repeats under a moving cursor
        inline constexpr int32 CUE_OPEN = 0;
        inline constexpr int32 CUE_HOVER = 1;
        inline constexpr int32 CUE_SELECT = 2;
        inline constexpr FSOLHoloCueTuning CUES[] =
        {
            { TEXT("/Game/SOL/UI/Audio/S_SOLHoloOpen.S_SOLHoloOpen"),     0.8f, 0.1 },
            { TEXT("/Game/SOL/UI/Audio/S_SOLHoloHover.S_SOLHoloHover"),   0.5f, HOVER_CUE_MIN_INTERVAL_S },
            { TEXT("/Game/SOL/UI/Audio/S_SOLHoloSelect.S_SOLHoloSelect"), 0.8f, 0.1 },
        };

        // Option (c) stepped broken-line frame (D20, section 6.3)
        inline constexpr float FRAME_CUT_TL_PX = 10.0f;           // Top-left corner cut (a)
        inline constexpr float FRAME_CUT_PX = 14.0f;              // Top-right and bottom-left corner cuts (c)
        inline constexpr float FRAME_STEP_PX = 10.0f;             // Title step height, bottom-right step-in depth (s)
        inline constexpr float FRAME_NOTCH_PX = 40.0f;            // Height above the bottom where the right steps in
        inline constexpr float FRAME_MIN_W_PX = FRAME_CUT_TL_PX + FRAME_STEP_PX + FRAME_CUT_PX;   // Full-size shape
        inline constexpr float FRAME_MIN_H_PX = FRAME_STEP_PX + FRAME_CUT_PX + FRAME_NOTCH_PX;    // ... smallest rect
        inline constexpr float FRAME_TITLE_STEP_PAD_PX = 26.0f;   // Raised title section = title text width + this
        inline constexpr int32 FRAME_OUTLINE_POINTS = 11;         // Points of the stepped outline (P0-P10)
        inline constexpr FSOLHoloGap FRAME_GAPS[] = {             // The outline's fixed gaps, sorted (96 px in all)
            { 0.20f, 10.0f }, { 0.27f, 20.0f }, { 0.36f, 12.0f }, { 0.52f, 10.0f }, { 0.66f, 24.0f }, { 0.70f, 8.0f },
            { 0.90f, 12.0f } };
        inline constexpr int32 FRAME_GAP_COUNT = static_cast<int32>(UE_ARRAY_COUNT(FRAME_GAPS));
        inline constexpr int32 FRAME_MAX_SEGMENTS = 24;           // Capacity of FrameDrawSegments
        inline constexpr int32 FRAME_MAX_DOTS = 14;               // Capacity of FrameGapDots (2 per gap)
        inline constexpr float FRAME_DOT_RADIUS_PX = 1.4f;        // Radius of a gap end dot
        inline constexpr int32 FRAME_RULER_TICK_COUNT = 6;        // Ruler ticks on the left edge
        inline constexpr float FRAME_RULER_TICK_PX = 4.0f;        // Ruler tick length, pointing inward
        inline constexpr float FRAME_RULER_SPACING_PX = 9.0f;     // Distance between ruler ticks
        inline constexpr float FRAME_RULER_START = 0.5f;          // First tick at this fraction of the height
        inline constexpr int32 FRAME_ACCENT_COUNT = 4;            // Capacity of FrameAccents
        inline constexpr float FRAME_ACCENT_INSET_PX = 6.0f;      // Inset of both corner accents from their edge
        inline constexpr float FRAME_ACCENT_LEFT_TOP_PX = 18.0f;  // Top-left accent, from ...
        inline constexpr float FRAME_ACCENT_LEFT_BOTTOM_PX = 70.0f;   // ... and to (below the top)
        inline constexpr float FRAME_ACCENT_BOTTOM_PX = 74.0f;    // Length of the bottom-right accent
        inline constexpr float FRAME_TITLE_UNDERLINE_Y_PX = 34.0f;        // Title underline, below the top
        inline constexpr float FRAME_TITLE_UNDERLINE_START_PX = 12.0f;    // ... from the left
        inline constexpr float FRAME_TITLE_UNDERLINE_END_PX = 40.0f;      // ... ending this far before the title step
        inline constexpr float FRAME_TITLE_DASH_START_PX = 32.0f;         // Its short dash, from this far ...
        inline constexpr float FRAME_TITLE_DASH_END_PX = 20.0f;           // ... to this far before the title step
        inline constexpr float FRAME_DIM_ALPHA = 0.45f;           // Dim lines' alpha relative to the frame lines
        inline constexpr float FRAME_TINT_INSET_PX = 2.0f;        // Inset of the tint box from the frame rect

        // Hover marks, tab markers and dividers (section 6.3)
        inline constexpr float HOVER_PAD_X_PX = 8.0f;             // The widget inflates the hovered item rect by these
        inline constexpr float HOVER_PAD_Y_PX = 3.0f;
        inline constexpr float HOVER_BRACKET_ARM_PX = 5.0f;       // Arm of the hover brackets
        inline constexpr float HOVER_POINTER_RUN_PX = 34.0f;      // Pointer line: straight run,
        inline constexpr float HOVER_POINTER_RISE_PX = 8.0f;      // ... 45-degree rise,
        inline constexpr float HOVER_POINTER_DOT_PX = 2.0f;       // ... dot beyond its end
        inline constexpr float HOVER_CENTRE_TICK_PX = 8.0f;       // Tick above and below a hovered digit or unit field
        inline constexpr int32 HOVER_MAX_SEGMENTS = 8;            // Capacity of HoverMark
        inline constexpr float TAB_BRACKET_ARM_PX = 6.0f;         // Unselected tab bar: arm length
        inline constexpr float TAB_BRACKET_INSET_PX = 2.0f;       // ... and vertical inset
        inline constexpr float TAB_CUT_PX = 6.0f;                 // Selected tab: top-left cut
        inline constexpr float TAB_DOT_OFFSET_PX = 4.0f;          // ... and the dot's distance left of the tab
        inline constexpr int32 TAB_MAX_SEGMENTS = 4;              // Capacity of TabMarker
        inline constexpr float DIVIDER_DASH_PX = 9.0f;            // Column divider dash length
        inline constexpr float DIVIDER_PERIOD_PX = 14.0f;         // ... and repeat
        inline constexpr int32 DIVIDER_MAX_DASHES = 40;           // Capacity of DashedDivider
        inline constexpr FSOLHoloGap DIVIDER_GAPS[] = { { 0.33f, 8.0f }, { 0.75f, 16.0f } };   // BrokenDivider gaps
        inline constexpr int32 DIVIDER_GAP_COUNT = static_cast<int32>(UE_ARRAY_COUNT(DIVIDER_GAPS));
        inline constexpr int32 DIVIDER_MAX_SEGMENTS = 6;          // Capacity of BrokenDivider
        inline constexpr float SPEED_DIVIDER_STEP_AT_PX = 104.0f; // The speed panel's divider steps up this far along
        inline constexpr float SPEED_DIVIDER_STEP_PX = 6.0f;      // ... by this much

        static_assert(FRAME_MAX_SEGMENTS >= FRAME_OUTLINE_POINTS + FRAME_GAP_COUNT + 1,
            "FrameDrawSegments' worst case is one piece per outline edge and gap plus the stroke meeting point");
        static_assert(FRAME_MAX_DOTS >= 2 * FRAME_GAP_COUNT, "FrameGapDots writes up to two dots per gap");
        static_assert(DIVIDER_MAX_SEGMENTS >= 3 + DIVIDER_GAP_COUNT, "BrokenDivider: 3 legs plus one piece per gap");
    }

    // Overlay icons, the Overlay Menu and the hologram cursor (SDD 22). Pixel sizes are at a 720 px canvas (x UI scale)
    namespace Overlay
    {
        inline constexpr int32 MAX_PROFILES = 5;                  // Overlay profiles (issue #34, SDD 34)
        inline constexpr uint32 ALL_PROFILES_MASK = (1u << MAX_PROFILES) - 1u;  // Every profile's bit in a profile mask
        static_assert(MAX_PROFILES <= 8, "the per-profile bit sets of SOLOverlay::FOverlayPrefs are uint8 masks");
        inline constexpr double PROFILE_TAG_SHOW_S = 2.0;         // HUD profile tag at full brightness after a cycle
        inline constexpr double PROFILE_TAG_FADE_S = 0.5;         // ... then fading out over this time
        inline constexpr int32 LAST_PLANET_INDEX = 8;            // Registry: Sun 0, planets 1-8 (Neptune), then the
                                                                  // dwarf planets and moons (PopulateSolarSystem)
        inline constexpr float ICON_RADIUS_PX = 7.0f;             // Icon shape radius
        inline constexpr float ICON_INTENSITY = 0.7f;             // Glow intensity of a fully bright icon
        inline constexpr double ICON_FADE_START_PX = 6.0;         // Apparent radius where the icon starts to fade
        inline constexpr double ICON_FADE_END_PX = 30.0;          // Apparent radius where the icon is gone
        inline constexpr double DARKEN_NEAR_M = 1.0e7;            // Distance darkening: full brightness up to here
        inline constexpr double DARKEN_FAR_M = 1.0e12;            // ... down to the floor from here (log scale)
        inline constexpr double DARKEN_FLOOR = 0.25;              // Brightness of the farthest icons with darkening on
        inline constexpr double ICON_MIN_BRIGHTNESS = 0.02;       // Icons dimmer than this are not drawn
        inline constexpr int32 MAX_SHAPES_PER_ICON = 4;           // Vector shapes of one icon (bounded vertex budget)
        inline constexpr int32 MAX_ICONS = 192;                   // Icons per frame, all categories
        inline constexpr int32 MAX_SHIP_ICONS = 24;               // NPC ship icons per frame (nearest first)
        inline constexpr int32 MAX_TARGET_ICONS = 16;             // Dropped-target icons per frame (nearest first)
        inline constexpr float EDGE_MARGIN_PX = 18.0f;            // Edge-pinned arrows sit this far inside the edge
        inline constexpr float EDGE_ARROW_PX = 7.0f;              // Edge arrow length
        inline constexpr float EDGE_ARROW_INTENSITY = 0.45f;      // Edge arrows are dimmer than icons
        inline constexpr int32 MAX_EDGE_ARROWS = 24;              // Edge arrows per frame (nearest first)
        inline constexpr float RING_ICON_SCALE = 1.9f;            // Ring icon: ellipse this many icon radii wide
        inline constexpr int32 CURSOR_SEGMENTS = 4;               // Hologram cursor: arrow outline segments
        inline constexpr float CURSOR_LENGTH_PX = 18.0f;          // Arrow length from the tip (hot spot)
        inline constexpr float CURSOR_INTENSITY = 0.9f;           // Glow intensity of the cursor
        inline constexpr float FREE_LOOK_ARM_S = 0.2f;            // Free look arms after this hold or this much mouse
        inline constexpr double FREE_LOOK_ARM_PX = 8.0;           // travel, so a quick Alt+O does not turn the camera
        inline constexpr int32 MENU_Z_ORDER = 15;                 // Overlay Menu: above the HUD, below the pause menu
    }

    // Hologram menu frames: one profile tab marker per Overlay profile (SDD 42 section 4.6)
    namespace HoloMenu
    {
        inline constexpr int32 FRAME_MAX_TABS = Overlay::MAX_PROFILES;    // Tab markers one frame can hold
    }

    // Interaction menus (SDD 22 section 2.4): the radial menu on flight icons, the map's list menu, the action table.
    // Pixel sizes are at a 720 px canvas (x UI scale)
    namespace Interact
    {
        inline constexpr int32 MAX_ACTIONS = 10;                  // Actions one object's menu can hold (table rows)
        inline constexpr double ICON_HIT_SLACK_PX = 6.0;          // Hover / press reach beyond an icon's radius
        inline constexpr float HOVER_RING_SCALE = 1.7f;           // Hovered icon: highlight ring this many icon radii
        inline constexpr float RADIAL_INNER_PX = 24.0f;           // Radial menu: cancel zone and the sector ring's
        inline constexpr float RADIAL_OUTER_PX = 64.0f;           // inner and outer radius
        inline constexpr float RADIAL_LABEL_PX = 78.0f;           // Action labels are anchored on this radius
        inline constexpr float TITLE_GAP_PX = 4.0f;               // Radial title: gap above the top label
        inline constexpr float RADIAL_SECTOR_GAP_RAD = 0.07f;     // Gap between two sectors (radians)
        inline constexpr float RADIAL_MARGIN_PX = 90.0f;          // The menu centre stays this far inside the screen
        inline constexpr float RADIAL_INTENSITY = 0.6f;           // Sector edge glow: enabled, highlighted, disabled
        inline constexpr float RADIAL_HIGHLIGHT_INTENSITY = 1.0f;
        inline constexpr float RADIAL_DISABLED_INTENSITY = 0.22f;
        inline constexpr float RADIAL_FILL_INTENSITY = 0.12f;     // Highlighted sector's fill (a wide arc)
        inline constexpr float RADIAL_EDGE_HALF_WIDTH_PX = 1.0f;  // Sector edge arcs (inner and outer rim)
        inline constexpr double INFO_CARD_S = 10.0;               // Info: the object's card shows this long
        inline constexpr double HINT_S = 5.0;                     // The Attack hint shows this long
        inline constexpr double JUMP_STANDOFF_RADII = 1.5;        // Jump To arrives this many radii from the centre,
        inline constexpr double JUMP_STANDOFF_M = 1000.0;         // plus this (LT's "1.5 R + 1000")
        inline constexpr float LIST_MARGIN_PX = 12.0f;            // Map list menu: kept this far inside the screen
        inline constexpr float LIST_CURSOR_OFFSET_PX = 14.0f;     // ... and opened this far right / below the cursor
        inline constexpr int32 LIST_Z_ORDER = 15;                 // Map list menu: above the HUD, below the pause menu
        inline constexpr double LABEL_SIDE_COSINE = 0.3;          // Radial labels beside the ring past this |cos|
        inline constexpr float DISABLED_TEXT_SCALE = 0.4f;        // Disabled radial labels: colour scale
        inline constexpr float HINT_Y_FRACTION = 0.68f;           // Attack hint line (fraction of the screen height)
        inline constexpr float INFO_CARD_WIDTH_PX = 230.0f;       // Info card: left edge this far from the right edge
        inline constexpr float INFO_CARD_TOP_PX = 120.0f;         // ... and its top this far below the top margin
                                                                  // (under the target readout and the key hints)
    }

    // Menu widgets (SDD 8, 8c): viewport layer, layout sizes (UMG units at the 1080p reference DPI), fonts and colours
    namespace MenuStyle
    {
        inline constexpr int32 Z_ORDER = 20;                     // Above the HUD canvas and the F3 panel
        inline constexpr const TCHAR* FONT_REGULAR = TEXT("Regular");
        inline constexpr const TCHAR* FONT_BOLD = TEXT("Bold");
        // Font sizes of the menu font (Rajdhani since SDD 46 section 9, +2 on body text for its narrower face; the
        // roles' sizes equal SOL::SolidMenu FONT_*_SIZE, checked by the static_asserts after that namespace)
        inline constexpr int32 TITLE_FONT_SIZE = 56;
        inline constexpr int32 SUBTITLE_FONT_SIZE = 18;
        inline constexpr int32 HEADING_FONT_SIZE = 26;
        inline constexpr int32 SECTION_FONT_SIZE = 20;
        inline constexpr int32 BUTTON_FONT_SIZE = 20;
        inline constexpr int32 TAB_FONT_SIZE = 18;
        inline constexpr int32 BODY_FONT_SIZE = 18;
        inline constexpr int32 SMALL_FONT_SIZE = 15;
        inline constexpr float SCREEN_MARGIN = 80.0f;            // Main menu column inset from the screen's left edge
        inline constexpr float BUTTON_HEIGHT = 52.0f;
        inline constexpr float BUTTON_GAP = 10.0f;
        inline constexpr float PANEL_PADDING = 28.0f;
        inline constexpr float PANEL_GAP = 24.0f;                // Between a menu panel (main column, pause side
                                                                 // panel) and its page shell
        inline constexpr float TAB_HEIGHT = 46.0f;
        inline constexpr float ROW_GAP = 4.0f;
        inline constexpr float KEY_COLUMN_WIDTH = 230.0f;        // Controls page key column
        inline constexpr float LABEL_COLUMN_WIDTH = 400.0f;      // Info page label column
        inline constexpr float LABEL_COLUMN_NARROW_WIDTH = 200.0f;   // ... in a narrow row (46i: 1000x800), labels
                                                                     // wrapping (SOLPageTabs::LeftColumnWidth)
        inline constexpr float VALUE_COLUMN_MIN_WIDTH = 220.0f;  // ... the full one only while the Info value column
                                                                 // right of it keeps this much room
        inline constexpr float LIST_BUTTON_HEIGHT = 38.0f;       // Start-location rows
        inline constexpr float BUTTON_PADDING_X = 18.0f;         // Inside a menu button, around its label
        inline constexpr float BUTTON_PADDING_Y = 4.0f;         // (SDD 46c: 8 clipped Rajdhani in 34-unit rows)
        inline const FLinearColor TEXT_COLOR(0.88f, 0.93f, 1.0f, 1.0f);          // NPC stats overlay (the menus use the
                                                                                 // SOL::SolidMenu palette)

        // Settings page (SDD 8, 8d): row layout
        inline constexpr float SETTINGS_LABEL_WIDTH = 250.0f;     // Left column of a settings row
        inline constexpr float SETTINGS_VALUE_WIDTH = 80.0f;      // Value readout right of a slider
        inline constexpr float SETTINGS_ROW_HEIGHT = 34.0f;       // Steppers, sliders, checkboxes and rebind rows
        inline constexpr float SETTINGS_STEP_BUTTON_WIDTH = 44.0f; // The '<' and '>' buttons of a stepper
        inline constexpr float SETTINGS_REBIND_WIDTH = 120.0f;    // The Rebind button of a key row
        inline constexpr float SETTINGS_ACTION_WIDTH = 150.0f;    // Apply / Revert buttons
        inline constexpr float SETTINGS_RESET_KEYS_WIDTH = 280.0f; // Reset keys button

        // Demo page (SDD 11, Part 10e) and the NPC stats overlay
        inline constexpr float DEMO_ACTION_WIDTH = 160.0f;        // Preset, Reset, Stop and Start buttons
        inline constexpr float STATS_OVERLAY_MARGIN = 16.0f;      // Overlay inset from the screen's top-right corner
        inline constexpr float STATS_OVERLAY_WIDTH = 520.0f;        // Minimum; the panel grows with its longest line
        inline constexpr int32 STATS_FONT_SIZE = 12;
        inline const FLinearColor STATS_BACKGROUND(0.0f, 0.0f, 0.0f, 0.55f);
    }

    // Solid console-style menus (SDD 46 sections 7-9): animation timings, hull limits and layout (UMG units at the
    // 1080p reference DPI). Starting values approved in Q16, tuned from screenshots; the palette comes with the kit
    namespace SolidMenu
    {
        // Button look (SDD 46 section 8.2)
        inline constexpr float HOVER_LIFT_PX = 3.0f;              // A hovered button rises this far (render transform)
        inline constexpr float HOVER_RAMP_S = 0.12f;              // Hover look fades in, and out, over this
        inline constexpr float PRESSED_LINGER_S = 0.10f;          // A quick click still shows the pressed hull this long
        inline constexpr float DISABLED_ALPHA = 0.4f;             // Brush tint alpha of a disabled button (continuous dim)
        inline constexpr float LIFT_EPSILON_PX = 0.01f;           // Lifts closer than this look the same (no re-apply)

        // Brush tint codes (SDD 46 section 8.3, 46c amendment): G is three-level, B the selected mark
        inline constexpr float TINT_DISABLED_CODE = 0.5f;         // G written for a disabled button (pressed writes 1)
        inline constexpr float TINT_PRESSED_G_MIN = 0.75f;        // Decode: G at or above this is the pressed hull
        inline constexpr float TINT_DISABLED_G_MIN = 0.25f;       // ... G from this up to TINT_PRESSED_G_MIN is disabled
        inline constexpr float TINT_MARK_B_MIN = 0.5f;            // ... B at or above this is the selected mark
        inline constexpr float TINT_HALF_LEVEL = 0.5f / 255.0f;   // Half an 8-bit tint level: tints this close or
                                                                  // closer encode the same (one look on screen)

        // Palette (SDD 46 section 5.1), sRGB; the three DISABLED_ colours are 46c starting values
        inline const FColor BASE_COLOR(0x24, 0x28, 0x2C);
        inline const FColor HULL_COLOR(0x34, 0x3A, 0x40);
        inline const FColor RAISED_COLOR(0x4A, 0x51, 0x59);
        inline const FColor RAISED_LIFTED_COLOR(0x58, 0x62, 0x6B);
        inline const FColor PRESSED_COLOR(0x1C, 0x20, 0x24);
        inline const FColor EDGE_COLOR(0x8C, 0x96, 0x9F);
        inline const FColor SHADOW_COLOR(0x12, 0x14, 0x16);
        inline const FColor LED_OFF_COLOR(0x0B, 0x0D, 0x0F);
        inline const FColor RIVET_COLOR(0x6A, 0x73, 0x7C);
        inline const FColor TEXT_COLOR(0xA4, 0xAF, 0xB8);
        inline const FColor TEXT_BRIGHT_COLOR(0xDC, 0xE3, 0xE8);
        inline const FColor TEXT_DIM_COLOR(0x8A, 0x94, 0x9D);     // Hints and secondary lines (46c follow-up)
        inline const FColor CYAN_COLOR(0x28, 0xD2, 0xF0);
        inline const FColor AMBER_COLOR(0xFF, 0xA6, 0x28);
        inline const FColor WARNING_COLOR(0xE5, 0x53, 0x3D);
        inline const FColor DISABLED_HULL_COLOR(0x30, 0x32, 0x34);
        inline const FColor DISABLED_TEXT_COLOR(0x65, 0x69, 0x6C);
        inline const FColor DISABLED_CUE_COLOR(0x3C, 0x3C, 0x3A);

        // Panels, pause and traces (SDD 46 sections 7 and 8.4)
        inline constexpr float PANEL_OPEN_S = 0.25f;              // Panel open: fade and slide in over this
        inline constexpr float PANEL_SLIDE_PX = 24.0f;            // ... from this far off its anchor side
        inline constexpr float PAUSE_SLIDE_S = 0.25f;             // Pause side panel: slides its width in over this
        inline constexpr float PAUSE_SLIDE_MARGIN_PX = 16.0f;     // ... from this far past the left screen edge
        inline constexpr float PAUSE_DIM_ALPHA = 0.55f;           // Dim of the live flight view behind the pause panel
        inline constexpr float TRACE_POWER_DELAY_S = 0.15f;       // Circuit traces light this long after the open
        inline constexpr float TRACE_POWER_S = 0.35f;             // ... along their length over this
        inline constexpr float LED_BLINK_S = 0.1f;                // ... while each stack LED blinks on this long, in
                                                                  // stack order over TRACE_POWER_S (46d)
        inline constexpr float IDLE_PULSE_PERIOD_S = 4.0f;        // Trace brightness swing: period (a sine)
        inline constexpr float IDLE_PULSE_DEPTH = 0.15f;          // ... and depth per unit of strength (capped at 2)
        inline constexpr float PULSE_DOT_ALPHA = 0.35f;           // Idle pulse dot on the longest trace: alpha per
                                                                  // unit of strength (one pass per period, 46d)
        inline constexpr float CLOSE_S = 0.15f;                   // Pause slide-out and page shell close

        // Hull shape (SDD 46 sections 5.2 and 8.1)
        inline constexpr float HULL_MAX_CHAMFER_FRACTION = 0.45f; // An edge's two chamfers sum to at most this of it
        inline constexpr float HULL_MAX_FEATURE_FRACTION = 0.25f; // Title step and chin height (of the height) and
                                                                  // notch depth (of the width) at most this
        inline constexpr float PANEL_CHAMFER_TL = 28.0f;          // Hull panel corner chamfers
        inline constexpr float PANEL_CHAMFER_TR = 12.0f;
        inline constexpr float PANEL_CHAMFER_BR = 28.0f;
        inline constexpr float PANEL_CHAMFER_BL = 12.0f;
        inline constexpr float BUTTON_CUT_PX = 10.0f;             // Stack button: top-left cut
        inline constexpr float BUTTON_POINT_PX = 14.0f;           // ... and right point

        // Hull material look (SDD 46 sections 4.3, 5 and 10; 46c starting values). Tools/UI/
        // create_solid_menu_materials.py reads these as M_SOLSolidHull and style instance defaults (keep both in sync).
        // Lengths are UMG units; the material turns them into pixels with the DpiScale parameter
        inline constexpr float TINT_VERTEX_FLOOR = 0.5f;          // Brush tint channels sit in [this, 1] (Slate multiplies
                                                                  // the material colour by the tint, which the material
                                                                  // divides out; SOLSolidStyle::PackVertexTint)
        inline constexpr float GRAIN_TILE_PX = 256.0f;            // One brushed grain tile
        inline constexpr float GRAIN_AMOUNT = 0.16f;              // Grain contrast on raised hulls (recesses are flat)
        inline constexpr float BEVEL_PX = 3.0f;                   // Bevel band inside the hull edge
        inline constexpr float BEVEL_AMOUNT = 0.22f;              // ... its light / shade (inverted on a recess)
        inline constexpr float LIGHT_DIR_X = -0.6f;               // Bevel light: outward normals facing this way are lit
        inline constexpr float LIGHT_DIR_Y = -0.8f;               // (top-left, unit length)
        inline constexpr float EDGE_PX = 1.5f;                    // Edge highlight and accent edge width
        inline constexpr float EDGE_HIGHLIGHT = 0.55f;            // Edge highlight amount (none on a disabled hull)
        inline constexpr float GLOW_PX = 6.0f;                    // Hover glow falloff inside the edge, LED halo
        inline constexpr float GLOW_GAIN = 0.3f;                  // Glow brightness per unit of s x Edge
        inline constexpr float LED_RADIUS_PX = 3.0f;
        inline constexpr float LED_CENTRE_X = 10000.0f;           // An LedX this large centres the LED (knob, puck)
        inline constexpr float UNDERLINE_PX = 2.0f;
        inline constexpr float SHADOW_PX = 8.0f;                  // Panel drop shadow falloff outside the hull
        inline constexpr float SHADOW_OPACITY = 0.55f;
        inline constexpr float PANEL_INSET_PX = 8.0f;             // Panel hull inset in its brush (room for the shadow)
        inline constexpr float CUE_BAND_PX = 5.0f;                // Disabled cue band above the bottom edge
        inline constexpr float CUE_PERIOD_PX = 8.0f;              // ... hatch period ((a), T_SOLSolidHazard as the mask)
        inline constexpr float CUE_MASK_PERIODS_X = 8.0f;         // Stripe periods across the T_SOLSolidHazard tile
        inline constexpr float CUE_LIP = 0.35f;                   // ... slot lower lip brightness ((b))
        inline constexpr float DISABLED_CUE_STYLE = 1.0f;         // Disabled cue: 1 dark slotted recess (b, the user's
                                                                  // choice), 0 hatched stripe (a). C++ never pushes it:
                                                                  // create_solid_menu_materials.py bakes it as the
                                                                  // DisabledCueStyle default (a kit test pins the asset)

        // Style instance geometry (SDD 46 section 5.2; 46c starting values)
        inline constexpr float BUTTON_LED_X_PX = 16.0f;           // Stack / tab: LED centre from the hull's left edge
        inline constexpr float BUTTON_UNDERLINE_START_PX = 28.0f; // ... underline from here
        inline constexpr float BUTTON_UNDERLINE_END_PX = 18.0f;   // ... to this far from the right (past the point)
        inline constexpr float BUTTON_UNDERLINE_BOTTOM_PX = 5.0f; // ... this far above the bottom edge
        inline constexpr float SMALL_BUTTON_CUT_PX = 6.0f;        // Small button: top-left cut, no point
        inline constexpr float SMALL_BUTTON_LED_X_PX = 10.0f;
        inline constexpr float SMALL_BUTTON_UNDERLINE_START_PX = 18.0f;
        inline constexpr float SMALL_BUTTON_UNDERLINE_END_PX = 8.0f;
        inline constexpr float SMALL_BUTTON_UNDERLINE_BOTTOM_PX = 4.0f;
        inline constexpr float RECESS_CHAMFER_LARGE_PX = 8.0f;    // Recess: top-left and bottom-right
        inline constexpr float RECESS_CHAMFER_SMALL_PX = 4.0f;    // ... top-right and bottom-left
        inline constexpr float GROOVE_CHAMFER_PX = 2.0f;          // Slider groove
        inline constexpr float KNOB_CHAMFER_PX = 5.0f;            // Octagonal slider knob
        inline constexpr float TOGGLE_CHAMFER_PX = 4.0f;          // Toggle slot
        inline constexpr float PUCK_CHAMFER_PX = 3.0f;            // Toggle puck
        inline constexpr float PUCK_TRAVEL_FRACTION = 0.5f;       // The puck is this much narrower than its slot and
                                                                  // sits right when checked (the tint's mark)
        inline constexpr float SLIDER_BAR_THICKNESS = 8.0f;       // Style factory sizes
        inline constexpr float SLIDER_FILL_THICKNESS = 4.0f;      // Slider fill: a cyan run inside the groove from
        inline constexpr float SLIDER_FILL_INSET_PX = 2.0f;       // its start (this far in) to the knob (46e follow-up)
        inline constexpr float KNOB_WIDTH = 18.0f;
        inline constexpr float KNOB_HEIGHT = 26.0f;
        inline constexpr float TOGGLE_WIDTH = 52.0f;
        inline constexpr float TOGGLE_HEIGHT = 26.0f;
        inline constexpr float SCROLL_BAR_THICKNESS = 8.0f;
        inline constexpr float SCROLL_THUMB_HOVER_BRIGHTEN = 0.3f; // Scroll thumb: hovered blends this far to white
        inline constexpr float SCROLL_THUMB_DRAG_BRIGHTEN = 0.5f; // ... dragged this far
        inline constexpr float SCROLL_FIT_TOLERANCE_PX = 0.5f;    // Scrolling rails (D17, 46g): content this much
                                                                  // taller than its view still fits (layout rounding)
        inline constexpr int32 STACK_MAX_VISIBLE_ENTRIES = 7;     // Main-menu and pause stacks: the view holds this
                                                                  // many entries, more scroll (today's 7 fit exactly)
        inline constexpr float RIVET_SIZE_PX = 10.0f;             // USOLSolidPanel decor (46c starting values): rivets
        inline constexpr float RIVET_INSET_PX = 16.0f;            // ... this far in from both edges of each corner
        inline constexpr float VENT_WIDTH_PX = 96.0f;             // ... the top-right vent strip
        inline constexpr float VENT_HEIGHT_PX = 12.0f;
        inline constexpr float VENT_INSET_PX = 24.0f;             // ... this far in from the top and right edges

        // Circuit traces (SDD 46 sections 7 and 9; SSOLSolidTraceLayer)
        inline constexpr float TRACE_CORE_WIDTH_PX = 1.5f;
        inline constexpr float TRACE_GLOW_WIDTH_PX = 6.0f;
        inline constexpr float TRACE_GLOW_ALPHA = 0.25f;

        // Font (SDD 46 sections 4.4 and 9): F_SOLMenu typefaces (FONT_REGULAR / FONT_BOLD of SOL::MenuStyle keep their
        // names), sizes and letter spacing per text role
        inline constexpr const TCHAR* FONT_MEDIUM = TEXT("Medium");
        inline constexpr const TCHAR* FONT_SEMIBOLD = TEXT("SemiBold");
        inline constexpr int32 FONT_TITLE_SIZE = 56;
        inline constexpr int32 FONT_HEADING_SIZE = 26;
        inline constexpr int32 FONT_SECTION_SIZE = 20;
        inline constexpr int32 FONT_BUTTON_SIZE = 20;
        inline constexpr int32 FONT_BODY_SIZE = 18;
        inline constexpr int32 FONT_SMALL_SIZE = 15;
        inline constexpr int32 LETTER_SPACING_TITLE = 400;        // FSlateFontInfo::LetterSpacing (1/1000 em)
        inline constexpr int32 LETTER_SPACING_STACK = 260;        // Stack and tab labels
        inline constexpr int32 LETTER_SPACING_SMALL_CAPS = 200;   // Small headings

        // Layout (SDD 46 section 9); the existing SOL::MenuStyle sizes stay unless listed here
        inline constexpr float COLUMN_WIDTH = 380.0f;             // Main menu hull column
        inline constexpr float PAGE_WIDTH = 1040.0f;              // Page shell (SDD 46 section 6.1); narrower when the
                                                                  // view is (it takes the room right of the column)
        inline constexpr float PAGE_HEIGHT = 800.0f;              // (46d: 760 left no room for the column's traces)
        inline constexpr float TAB_COLUMN_WIDTH = 220.0f;         // The shell's vertical tab column (46e: 200 did not
                                                                  // fit "Key bindings")
        inline constexpr float PAUSE_PANEL_WIDTH = COLUMN_WIDTH;  // Pause side panel (46i: the main menu column's
                                                                  // width, so its shell has the main menu's room; 420
                                                                  // clipped the Window mode value at 1000x800)
        inline constexpr float BACK_BUTTON_WIDTH = 160.0f;        // The shell's Back button

        // Main menu column (46d, SDD 46 section 6): a fixed-height hull, so its traces sit at known places. Lengths
        // are from the panel widget's edge (the hull itself is PANEL_INSET_PX inside it) unless named for the hull
        inline constexpr float COLUMN_HEIGHT = PAGE_HEIGHT;       // Level with the page shell
        inline constexpr float COLUMN_PADDING = 28.0f;            // Hull edge to the content
        inline constexpr float COLUMN_TITLE_HEIGHT = 76.0f;       // "SOLTest" row
        inline constexpr float COLUMN_TAGLINE_HEIGHT = 28.0f;     // Tagline row under it
        inline constexpr float COLUMN_STACK_GAP = 20.0f;          // Tagline to the first entry
        inline constexpr float COLUMN_HINT_GAP = 14.0f;           // Hazard stripe to the key hint
        inline constexpr float COLUMN_TITLE_STEP_WIDTH = 290.0f;  // Hull: the stepped top edge carrying the title
        inline constexpr float COLUMN_TITLE_STEP_HEIGHT = 12.0f;
        inline constexpr float COLUMN_CHIN_WIDTH = 300.0f;        // Hull: the chin carrying the version recess
        inline constexpr float COLUMN_CHIN_HEIGHT = 14.0f;
        inline constexpr float COLUMN_TRACE_GAP = 16.0f;          // Lower-edge traces: above the version recess, and
                                                                  // their two rows this far apart (45-degree bends)
        inline constexpr float COLUMN_TRACE_DROP_BENDS = 3.0f;    // The long trace drops from row A to row B this many
                                                                  // gaps left of the middle (one gap down, one across)
        inline constexpr float COLUMN_TRACE_END_BENDS = 3.0f;     // ... and ends this many gaps short of the right end
        inline constexpr float COLUMN_TRACE_SHORT_BENDS = 5.0f;   // The row B trace ends this many gaps left of the
                                                                  // middle
        inline constexpr float COLUMN_HINT_CLEARANCE_PX = 3.0f;   // The wrapped key hint ends at least this far above
                                                                  // trace row A (a ring terminal's radius; smoke)
        inline constexpr float HAZARD_HEIGHT_PX = 10.0f;          // Hazard stripe under Quit (and Quit to desktop)
        inline constexpr float HAZARD_TILE_WIDTH_PX = 80.0f;      // ... one T_SOLSolidHazard tile (128 x 16) at it
        inline constexpr float VERSION_RECESS_HEIGHT = 58.0f;     // Chin recess holding the version line (2 lines)
        inline constexpr float VERSION_RECESS_PADDING = 8.0f;

        // Page shell (46d, SDD 46 section 6.1)
        inline constexpr float SHELL_PADDING = 28.0f;             // Hull edge to the content recess
        inline constexpr float SHELL_RECESS_PADDING = 20.0f;      // Content recess edge to the page
        inline constexpr float SHELL_BACK_GAP = 12.0f;            // Content recess to the Back recess
        inline constexpr float SHELL_BACK_RECESS_PADDING = 6.0f;  // Back recess edge to the Back button
        inline constexpr float SHELL_NOTCH_DEPTH = 6.0f;          // Hull: a notch in each side, centred
        inline constexpr float SHELL_NOTCH_HEIGHT = 72.0f;
        inline constexpr float LIST_RECESS_PADDING = 10.0f;       // Start-location list recess edge to its rows

        // Settings tab column (46e, SDD 46 section 6.3): a tab rail recess TAB_COLUMN_WIDTH wide left of the content
        // recess; the tabs are TAB_HEIGHT tall, BUTTON_GAP apart, and leave room on their right for the hover trace
        inline constexpr float SETTINGS_TAB_RAIL_PADDING = 8.0f;  // Tab rail recess edge to its tabs
        inline constexpr float SETTINGS_TAB_TRACE_PX = 14.0f;     // Room right of the tabs for the hovered tab's trace
        inline constexpr float SETTINGS_TAB_TRACE_END_PX = 4.0f;  // ... which ends this far inside the rail's edge
        inline constexpr int32 SETTINGS_TAB_LETTER_SPACING = 100; // Tab labels (the stack's 260 is too wide for them)
        // (33d-ii, SDD 33 D22 / D7) A group caption row above a tab in the rail (the Help page's "KEYS" and "HUD
        // GLOSSARY"): its height (BUTTON_GAP follows it), font size (SemiBold, DimTextColor) and letter spacing
        inline constexpr float TAB_RAIL_CAPTION_HEIGHT = 24.0f;
        inline constexpr int32 TAB_RAIL_CAPTION_FONT_SIZE = MenuStyle::SMALL_FONT_SIZE;
        inline constexpr int32 TAB_RAIL_CAPTION_LETTER_SPACING = 200;
        inline constexpr float SETTINGS_BODY_GAP = 12.0f;         // Tab rail to the content recess
        inline constexpr float SETTINGS_CONTENT_PADDING = 10.0f;  // Content recess edge to the rows
        // Per-tab label columns (46e follow-ups, SOLSettingsTabs::LabelColumnWidth; the other tabs keep
        // SOL::MenuStyle::SETTINGS_LABEL_WIDTH). Graphics: its labels are short, so the stepper values fit at 1000x800
        // ("Window mode" may run into the BUTTON_GAP after the column). Gameplay: "Chase Down safety margin" is about
        // 266 units as drawn at 1000x800, past the shared column and its gap
        inline constexpr float SETTINGS_GRAPHICS_LABEL_WIDTH = 134.0f;
        inline constexpr float SETTINGS_GAMEPLAY_LABEL_WIDTH = 264.0f;
        // Narrow views (46i, SOLSettingsTabs::IsNarrowLayout): Key bindings narrows its label column and its Rebind
        // buttons when the key text would keep less than SETTINGS_KEY_TEXT_MIN_WIDTH (a row under 500 units), Gameplay
        // narrows its label column when a slider would keep less than SETTINGS_SLIDER_MIN_WIDTH (a row under 514); the
        // narrow labels wrap. At 1000x800 a row is about 405 units: the key text keeps 123 ("Backspace" is 104), a
        // slider 165; wide views (1280x720 is about 658) keep the wide columns. The narrow Rebind holds the button's
        // desired width (its "Rebind" label and BUTTON_PADDING_X a side: 103.1 at 1000x800)
        inline constexpr float SETTINGS_KEY_TEXT_MIN_WIDTH = 120.0f;      // Key text room a key row keeps
        inline constexpr float SETTINGS_SLIDER_MIN_WIDTH = 160.0f;        // Slider room a Gameplay row keeps
        inline constexpr float SETTINGS_KEYS_NARROW_LABEL_WIDTH = 168.0f; // Key bindings' label column, narrow
        inline constexpr float SETTINGS_GAMEPLAY_NARROW_LABEL_WIDTH = 150.0f; // Gameplay's label column, narrow
        inline constexpr float SETTINGS_REBIND_NARROW_WIDTH = 104.0f;     // A Rebind button, narrow
    }
    static_assert(MenuStyle::TITLE_FONT_SIZE == SolidMenu::FONT_TITLE_SIZE
        && MenuStyle::HEADING_FONT_SIZE == SolidMenu::FONT_HEADING_SIZE
        && MenuStyle::SECTION_FONT_SIZE == SolidMenu::FONT_SECTION_SIZE
        && MenuStyle::BUTTON_FONT_SIZE == SolidMenu::FONT_BUTTON_SIZE
        && MenuStyle::BODY_FONT_SIZE == SolidMenu::FONT_BODY_SIZE
        && MenuStyle::SMALL_FONT_SIZE == SolidMenu::FONT_SMALL_SIZE, "menu font sizes are the SDD 46 section 9 roles'");

    // The Help page (SDD 33 section 11): key caps, the keyboard diagram, the HUD glossary. Starting values approved for
    // tuning (D26), sizes in menu units
    namespace Help
    {
        // Key caps (section 6)
        inline constexpr float KEYCAP_HEIGHT = 30.0f;             // One cap's height
        inline constexpr float KEYCAP_CHAMFER = 3.0f;             // Its hull corner chamfer
        inline constexpr float KEYCAP_PADDING = 8.0f;             // Legend to the cap edge, a side
        inline constexpr float KEYCAP_MIN_WIDTH = 30.0f;          // The narrowest cap
        inline constexpr int32 KEYCAP_FONT_SIZE = 15;             // Legend (Rajdhani SemiBold)
        inline constexpr int32 KEYCAP_LABEL_FONT_SIZE = 11;       // Short action label on a diagram key
        inline constexpr float KEYCAP_GAP = 4.0f;                 // Between caps (and around "+" and " / ")
        inline constexpr float KEYCAP_ROW_GAP = 4.0f;             // Between wrapped lines of a cap row
        inline constexpr float KEYCAP_SPACE_UNITS = 4.0f;         // Space bar width in key units (a cap in a row)
        inline constexpr float KEYCAP_WIDE_UNITS = 2.0f;          // Other wide caps (Backspace, Tab, Enter, Shift,
                                                                  // Ctrl, Alt, Caps) in key units, in a row
        // The mouse glyph on a Mouse cap (33b): its box, the buttons' share of its height from the top, and the wheel
        // slot (its width as a share of the glyph's, its top and bottom as shares of the height from the top)
        inline constexpr float KEYCAP_MOUSE_GLYPH_WIDTH = 12.0f;
        inline constexpr float KEYCAP_MOUSE_GLYPH_HEIGHT = 18.0f;
        inline constexpr float KEYCAP_MOUSE_BUTTON_FRACTION = 0.42f;
        inline constexpr float KEYCAP_MOUSE_WHEEL_WIDTH_FRACTION = 0.22f;
        inline constexpr float KEYCAP_MOUSE_WHEEL_TOP_FRACTION = 0.10f;
        inline constexpr float KEYCAP_MOUSE_WHEEL_BOTTOM_FRACTION = 0.34f;
        inline constexpr float KEYCAP_MOUSE_LINE_PX = 1.0f;       // The glyph's outline and button divider (pixels)
        // A bound cap's content starts this far from its left edge, past the Small button hull's unlit LED: the hull
        // material scales its LED with the DPI, so the LED always ends SolidMenu::SMALL_BUTTON_LED_X_PX +
        // LED_RADIUS_PX = 13 units from the edge, and the room leaves KEYCAP_LED_GAP clear after it at every scale;
        // the right keeps KEYCAP_PADDING. An unbound (recess) cap has no LED and keeps the padding
        // (SOLKeyCaps::ContentLeft)
        inline constexpr float KEYCAP_LED_GAP = 7.0f;
        inline constexpr float KEYCAP_LED_ROOM = SolidMenu::SMALL_BUTTON_LED_X_PX + SolidMenu::LED_RADIUS_PX
            + KEYCAP_LED_GAP;
        static_assert(KEYCAP_MOUSE_WHEEL_TOP_FRACTION > 0.0f
            && KEYCAP_MOUSE_WHEEL_TOP_FRACTION < KEYCAP_MOUSE_WHEEL_BOTTOM_FRACTION
            && KEYCAP_MOUSE_WHEEL_BOTTOM_FRACTION <= KEYCAP_MOUSE_BUTTON_FRACTION && KEYCAP_MOUSE_BUTTON_FRACTION < 1.0f
            && KEYCAP_MOUSE_GLYPH_HEIGHT < KEYCAP_HEIGHT && KEYCAP_MOUSE_GLYPH_WIDTH + 2.0f * KEYCAP_PADDING
            <= KEYCAP_MIN_WIDTH, "the mouse glyph's parts nest and the glyph fits the smallest cap");

        // Keyboard diagram (section 7; D30, D34-D38: settled by the 33c smoke at 1280x720 and 1920x1080)
        inline constexpr float DIAGRAM_MIN_UNIT = 20.0f;          // Key unit clamp
        inline constexpr float DIAGRAM_MAX_UNIT = 46.0f;
        inline constexpr float DIAGRAM_MOUSE_BELOW_UNIT = 30.0f;  // Below this one-row key unit the mouse sits under
                                                                  // the keys (D34)
        inline constexpr float DIAGRAM_STACK_UNIT = 30.0f;        // D34: a below-mouse unit under this stacks the
                                                                  // clusters and the mouse under the main block
        inline constexpr float DIAGRAM_TINT_ALPHA = 0.28f;        // Area-colour fill over an assigned key's face
        inline constexpr float DIAGRAM_FREE_ALPHA = 0.45f;        // A key with no assignment is drawn at this alpha
        // D36: the cap on the hover strip's lines (the strip is sized once per width and model for the busiest key's
        // wrapped rows, at most this many), and the height of its cap row above the lines
        inline constexpr int32 DIAGRAM_DETAIL_LINES = 24;
        inline constexpr float DIAGRAM_DETAIL_CAP_HEIGHT = 30.0f;
        inline constexpr float DIAGRAM_BADGE_GAP = 2.0f;          // D35: a key's legend to its "+N" badge
        inline constexpr int32 DIAGRAM_ABBREVIATION_MAX_CHARS = 4; // D38: a diagram legend abbreviation's length cap
        inline constexpr float DIAGRAM_NO_LED_X = -1.0f;          // D38: the keys' hull LedX (< 0: no LED)
        // The Keyboard tab's panel (33c, SOLKeyboardLayout::LayoutPanel): the area legend grid, the gap between its
        // sections, the "Other keys" strip, the "Unbound" line and one hover strip line
        inline constexpr float DIAGRAM_LEGEND_ITEM_WIDTH = 104.0f; // One legend item (swatch and area name)
        inline constexpr float DIAGRAM_LEGEND_GAP = 8.0f;         // Between legend items
        inline constexpr float DIAGRAM_LEGEND_ROW_HEIGHT = 22.0f; // One legend grid row
        inline constexpr float DIAGRAM_SECTION_GAP = 12.0f;       // Between the panel's shown sections
        inline constexpr float DIAGRAM_STRIP_HEIGHT = 40.0f;      // The "Other keys" strip
        inline constexpr float DIAGRAM_UNBOUND_HEIGHT = 22.0f;    // The "Unbound" line
        inline constexpr float DIAGRAM_DETAIL_LINE_HEIGHT = 22.0f; // One hover strip line
        // The diagram leaf's drawing (33c, SSOLKeyboardDiagram; settled from the 33c screenshots)
        inline constexpr float DIAGRAM_KEY_INSET = 1.5f;          // A key's hull inside its layout rect, a side
        inline constexpr float DIAGRAM_KEY_PAD = 2.0f;            // Key content to the hull edge
        inline constexpr int32 DIAGRAM_KEY_FONT_SIZE = 11;        // A key's legend (Rajdhani SemiBold); the short
                                                                  // label uses KEYCAP_LABEL_FONT_SIZE
        inline constexpr int32 DIAGRAM_BADGE_FONT_SIZE = 9;       // The "+N" badge
        inline constexpr float DIAGRAM_AREA_BAR = 3.0f;           // The area bar along an assigned key's lower edge
        inline constexpr float DIAGRAM_MARK_SIZE = 6.0f;          // The lock mark's and modifier mark's box
        inline constexpr float DIAGRAM_MARK_LINE = 1.5f;          // Their stroke
        inline constexpr float DIAGRAM_MARK_GAP = 1.0f;           // A mark to the legend / label beside it
        inline constexpr float DIAGRAM_BADGE_LINE_EXTRA = 2.0f;   // The "+N" badge's line over DIAGRAM_MARK_SIZE
        inline constexpr float DIAGRAM_LOCK_BODY_TOP_FRACTION = 0.45f; // The padlock body's top, of the mark size
        inline constexpr float DIAGRAM_LOCK_SHACKLE_INSET_FRACTION = 0.15f; // The shackle's sides in from the mark's
                                                                  // edges, of the mark size
        inline constexpr float DIAGRAM_CARET_HEIGHT_FRACTION = 0.6f; // The modifier caret's tip height, of the mark
                                                                  // size
        inline constexpr float DIAGRAM_LEGEND_SWATCH = 12.0f;     // A legend item's colour swatch, square
        inline constexpr float DIAGRAM_TEXT_GAP = 6.0f;           // Swatch to text, caption to caps, column gaps
        inline constexpr float DIAGRAM_DETAIL_KEYS_MAX_FRACTION = 0.35f; // The detail keys column's widest share
        inline constexpr float DIAGRAM_WIDTH_TOLERANCE = 0.5f;    // A layout width change smaller than this keeps the
                                                                  // laid-out panel (menu units)
        // (33d-ii) The Help page's Keyboard diagram's layout width at fill (menu units): the shell's content row width
        // at the D37 screens (1280x720 and 1920x1080), so the diagram paints on the page's first frame; the page's
        // FitDiagram corrects it once laid out when the rows are another width
        inline constexpr float DIAGRAM_START_LAYOUT_WIDTH = 658.0f;
        inline constexpr int32 SHORT_LABEL_MAX_CHARS = 8;        // KEY_SHORT_LABELS / FIXED_KEY_SHORT_LABELS length

        // HUD glossary (section 8)
        inline constexpr float GLOSSARY_SYMBOL_BOX = 96.0f;       // Symbol recess, square
        inline constexpr int32 GLOSSARY_MAX_SHAPES = 128;         // One symbol view's shape batch, at most
        // (33d-ii) A symbol view's batch is sized at its first build to the shapes its symbols make (a dry build) plus
        // this headroom (a size change may add a dash or two), capped at GLOSSARY_MAX_SHAPES
        inline constexpr int32 GLOSSARY_SHAPE_HEADROOM = 8;
        // (33d-ii) The symbol view (D26 starting values): the HUD's UI scale for a 1080 px canvas, in menu units (D5:
        // x pixels per unit), the recess padding around the view, the view's edge to its art area, the text sample's
        // smallest scale (D3, in menu units), the sample lines a view holds and the gap between glossary rows. The
        // glow pad is the HUD's HUD_GLOW_PAD_PX in pixels, unscaled (D6: M_SOLHudShape fades its halo over a fixed
        // pixel pad)
        inline constexpr float GLOSSARY_SYMBOL_UI_SCALE = 1.5f;
        static_assert(GLOSSARY_SYMBOL_UI_SCALE == 1080.0f / Hologram::HUD_REFERENCE_HEIGHT_PX,
            "GLOSSARY_SYMBOL_UI_SCALE is the HUD's UI scale for a 1080 px canvas");
        static_assert(GLOSSARY_SYMBOL_UI_SCALE >= 1.0f && GLOSSARY_SYMBOL_UI_SCALE <= Hologram::HUD_MAX_UI_SCALE,
            "GLOSSARY_SYMBOL_UI_SCALE lies in the HUD's UI scale range");
        inline constexpr float GLOSSARY_SYMBOL_RECESS_PADDING = 4.0f;
        // A symbol view's laid-out side (units): the box less the recess padding on both sides. The shared HUD shape
        // material's view size, and the size a view not laid out yet is built at (so a newly shown tab paints at once)
        inline constexpr float GLOSSARY_VIEW_SIZE = GLOSSARY_SYMBOL_BOX - 2.0f * GLOSSARY_SYMBOL_RECESS_PADDING;
        inline constexpr float GLOSSARY_SYMBOL_PADDING = 4.0f;
        // (33d-iii, D44) A text sample's smallest scale, 1.0 x pixels per unit: a sample is wrapped at the HUD's size
        // (GLOSSARY_SYMBOL_UI_SCALE) first; only a short sample in the square box (single words too wide for it)
        // shrinks toward this floor, never below it
        inline constexpr float GLOSSARY_SAMPLE_MIN_UI_SCALE = 1.0f;
        static_assert(GLOSSARY_SAMPLE_MIN_UI_SCALE > 0.0f && GLOSSARY_SAMPLE_MIN_UI_SCALE <= GLOSSARY_SYMBOL_UI_SCALE,
            "A sample's smallest scale lies in (0, GLOSSARY_SYMBOL_UI_SCALE]");
        // (33d-iii, D44) A text sample whose '|' parts would wrap in the symbol box gets a wider box (it reaches into
        // the description column for its own row), as tall as its lines at the HUD's size need
        // (SOLGlossary::SampleBoxSize). The slack narrows the units wrap and pads the height so the pixel layout, whose
        // fonts round to whole pixel sizes, still fits the box
        inline constexpr float GLOSSARY_SAMPLE_BOX_WIDTH = 200.0f;
        inline constexpr float GLOSSARY_SAMPLE_BOX_SLACK = 4.0f;
        static_assert(GLOSSARY_SAMPLE_BOX_WIDTH > GLOSSARY_SYMBOL_BOX, "A wide sample box is wider than a symbol box");
        // (33d-iii) A view's laid-out size matching its nominal size within this (units) ends a pending resize
        inline constexpr float GLOSSARY_NOMINAL_SIZE_TOLERANCE = 0.5f;
        inline constexpr int32 GLOSSARY_MAX_SAMPLE_LINES = 8;
        // A sample's layout passes (laid out at unit scale, then measured again at the scale found until it holds)
        inline constexpr int32 GLOSSARY_SAMPLE_MEASURE_PASSES = 3;
        inline constexpr float GLOSSARY_ROW_GAP = 12.0f;
        inline constexpr int32 GLOSSARY_MAX_SYMBOLS_PER_ENTRY = 6; // Symbol ids one GLOSSARY_ENTRIES row may list
        inline constexpr int32 MAX_PRIMS_PER_SYMBOL = 32;         // FSOLHudSymbolPrims capacity
        inline constexpr float GLOSSARY_CARD_HALF_WIDTH_PX = 24.0f; // The Card symbol's content rect, half size in px
        inline constexpr float GLOSSARY_CARD_HALF_HEIGHT_PX = 14.0f; // at UI scale 1 (the card adds its padding)
        inline constexpr double GLOSSARY_SAMPLE_CHASE_BURST_G = 6.8; // The Chase Down line sample's burst G (a plan's
                                                                     // burst varies between the sustained and max G)
        inline constexpr int32 GLOSSARY_SAMPLE_OVERLAY_PROFILE = 2; // The overlay profile tag sample's number (1-based)
        // The glossary's HUD symbol parameters (part 33d-i), at UI scale 1 (Build multiplies them by its scale): the
        // stick's offset (fractions of the joystick radius), the bracket's half size, the radar's sonar time and view
        // cone, a contact's stem, the bars' fills and Chase Down ticks, the warp streaks, the radial menu's sectors
        // (one highlighted, one disabled), the list frame's half size; no feature below GLOSSARY_MIN_FEATURE_PX (D42)
        inline constexpr float GLOSSARY_STICK_OFFSET_X = 0.5f;
        inline constexpr float GLOSSARY_STICK_OFFSET_Y = 0.25f;
        inline constexpr float GLOSSARY_BRACKET_HALF_PX = 28.0f;
        inline constexpr double GLOSSARY_RADAR_TIME_S = 0.0;
        inline constexpr double GLOSSARY_RADAR_CONE_HALF_RAD = 0.25 * UE_DOUBLE_PI;
        inline constexpr float GLOSSARY_RADAR_STEM_PX = 24.0f;
        inline constexpr double GLOSSARY_BAR_FILL = 0.7;
        inline constexpr double GLOSSARY_G_FILL = 0.6;
        inline constexpr double GLOSSARY_CHASE_FLOOR = 0.3;
        inline constexpr double GLOSSARY_CHASE_RESUME = 0.5;
        inline constexpr int32 GLOSSARY_WARP_STREAKS = 12;
        inline constexpr float GLOSSARY_WARP_HALF_DIAGONAL_PX = 32.0f;
        inline constexpr int32 GLOSSARY_RADIAL_SECTORS = 4;
        inline constexpr int32 GLOSSARY_RADIAL_DISABLED_SECTOR = 2;
        inline constexpr float GLOSSARY_FRAME_HALF_WIDTH_PX = 40.0f;
        inline constexpr float GLOSSARY_FRAME_HALF_HEIGHT_PX = 28.0f;
        inline constexpr float GLOSSARY_MIN_FEATURE_PX = 2.0f;    // Smallest radius / size / dash a glossary symbol
                                                                  // draws (lines, text and cards exempt)

        // Settings > Key bindings: the key cell's width bound (D20, tuned at 1000x800)
        inline constexpr float SETTINGS_KEYCAP_CELL_WIDTH = 200.0f;

        // Area colours (D17, D29), sRGB, in ESOLControlArea order: Flight, Targeting, Combat, Time, Overlays, HUD, Map,
        // Routes, Menu, Demo (the ten areas of SOLMenuText::CONTROL_ROWS)
        inline constexpr int32 AREA_COUNT = 10;
        inline constexpr FColor AREA_COLORS[] = {
            FColor(0x28, 0xD2, 0xF0),   // Flight (the kit cyan)
            FColor(0xF2, 0xC9, 0x4C),   // Targeting
            FColor(0xE5, 0x53, 0x3D),   // Combat (the kit warning red-orange)
            FColor(0x9B, 0x7B, 0xFF),   // Time
            FColor(0x4C, 0xD0, 0x7D),   // Overlays
            FColor(0x5B, 0x8C, 0xFF),   // HUD
            FColor(0xFF, 0x8F, 0xB1),   // Map
            FColor(0xD9, 0x8A, 0x5B),   // Routes (copper)
            FColor(0x9F, 0xD6, 0xD0),   // Menu (teal-white, D29)
            FColor(0xE8, 0xEE, 0xF2),   // Demo (near-white)
        };
        static_assert(UE_ARRAY_COUNT(AREA_COLORS) == AREA_COUNT, "one colour per Controls area");
    }

    // The build version line (SDD 46 section 11.1, SOLBuildVersion): MAJOR.MINOR.YYMM.<commit count>
    namespace BuildVersion
    {
        inline constexpr int32 MAJOR = 0;                         // Fixed parts of the version, set by hand
        inline constexpr int32 MINOR = 1;
    }

    // Settings (SDD 8, 8d): the USOLSettingsSave slot, its schema version and the slider steps
    namespace Settings
    {
        inline constexpr const TCHAR* SAVE_SLOT = TEXT("SOLSettings");                  // Player settings
        inline constexpr const TCHAR* SMOKE_SAVE_SLOT = TEXT("SOLSettings_MenuSmoke");  // -SOLMenuSmoke runs only
                                                                                          // (deleted at their start)
        inline constexpr int32 SAVE_USER_INDEX = 0;
        // Save schema; bump it and add a step to USOLSettingsSave::MigrateToCurrentVersion on schema changes (2: orbit
        // lines, SDD 17; 3: the issue #19 key layout, SDD 19; 4: the issue #20 map keys, SDD 20; 5: the HUD effects
        // strength, SDD 21; 6: mouse steering at start, SDD 19 Amendment 5; 7: overlay profiles and the O keys, SDD 22;
        // 8: Chase Down safety margin, SDD 32; 9: the Space Dust overlay row written into every profile, SDD 43;
        // 10: overlay profiles with their enabled, master and orbit-lines flags, the global space dust and the
        // RShift+O profile cycle, SDD 34; 11: the hologram menu effects strength, SDD 42)
        inline constexpr int32 SAVE_VERSION = 11;
        // The save schema that introduced the current default key layout (issue #34: O / Alt+O / Ctrl+O / RShift+O);
        // older saves get the one-time "default controls changed" notice (SOLKeyBindings::DefaultLayoutChangedSince)
        inline constexpr int32 KEY_BINDING_LAYOUT_VERSION = 10;
        inline constexpr float AUDIO_FADE_S = 0.0f;       // Volume changes are immediate (preview while dragging)
        inline constexpr float VOLUME_STEP = 0.05f;       // Slider steps
        inline constexpr float MOUSE_SENSITIVITY_STEP = 0.05f;
        inline constexpr float HUD_SCALE_STEP = 0.05f;

        // Slider ranges; they match the clamping in SOLSettingsModel::Sanitized (keep both in sync)
        inline constexpr float VOLUME_MIN = 0.0f;
        inline constexpr float VOLUME_MAX = 1.0f;
        inline constexpr float MOUSE_SENSITIVITY_MIN = 0.1f;
        inline constexpr float MOUSE_SENSITIVITY_MAX = 5.0f;
        inline constexpr float HUD_SCALE_MIN = 0.5f;
        inline constexpr float HUD_SCALE_MAX = 2.0f;
        inline constexpr float HUD_EFFECTS_MIN = 0.0f;      // HUD hologram effects strength (jitter; SDD 21)
        inline constexpr float HUD_EFFECTS_MAX = 2.0f;
        inline constexpr float HUD_EFFECTS_DEFAULT = 1.0f;
        inline constexpr float HUD_EFFECTS_STEP = 0.05f;
        inline constexpr float HOLO_MENU_EFFECTS_MIN = 0.0f;    // Hologram menu effects strength (SDD 42 section 8)
        inline constexpr float HOLO_MENU_EFFECTS_MAX = 2.0f;
        inline constexpr float HOLO_MENU_EFFECTS_DEFAULT = 1.0f; // Full strength (D8); a migrated save copies its HUD
                                                                 // effects strength instead (SDD 42 section 9, A2)
        inline constexpr float HOLO_MENU_EFFECTS_STEP = 0.05f;
        inline constexpr float VALUE_EQUAL_TOLERANCE = 1.0e-4f;    // Two slider values closer than this are equal

        // Chase Down safety margin (SDD 32 section 3.10): a floor on the G meter, as a fraction of a full meter
        inline constexpr float CHASE_SAFETY_MARGIN_DEFAULT = 0.25f;
        inline constexpr float CHASE_SAFETY_MARGIN_MIN = 0.0f;
        inline constexpr float CHASE_SAFETY_MARGIN_MAX = 0.30f;
        inline constexpr float CHASE_SAFETY_MARGIN_STEP = 0.01f;

        // Space dust speed cues (SDD 34 section 9): one global Settings option (Graphics), on by default
        inline constexpr bool SPACE_DUST_DEFAULT = true;
    }

    // Registry names of bodies that code refers to directly
    namespace BodyNames
    {
        inline constexpr const TCHAR* SUN = TEXT("Sun");
        inline constexpr const TCHAR* EARTH = TEXT("Earth");
        inline constexpr const TCHAR* MOON = TEXT("Moon");
        inline constexpr const TCHAR* MARS = TEXT("Mars");
        inline constexpr const TCHAR* JUPITER = TEXT("Jupiter");
        inline constexpr const TCHAR* SATURN = TEXT("Saturn");
        inline constexpr const TCHAR* URANUS = TEXT("Uranus");
        inline constexpr const TCHAR* NEPTUNE = TEXT("Neptune");
        inline constexpr const TCHAR* PAN = TEXT("Pan");
        inline constexpr const TCHAR* DAPHNIS = TEXT("Daphnis");
    }

    // Command-line switches and values used by verification runs (FParse::Value keys end in '=')
    namespace CommandLine
    {
        inline constexpr const TCHAR* START_BODY = TEXT("SOLStart=");           // Body the ship spawns above
        inline constexpr const TCHAR* SIM_DATE = TEXT("SOLSimDate=");           // Start sim time (UTC date), dev only
        inline constexpr const TCHAR* ALTITUDE_KM = TEXT("SOLAltitudeKm=");     // Spawn altitude above its surface
        inline constexpr const TCHAR* LOOK_AT_BODY = TEXT("SOLLookAt=");        // Body the debug camera faces
        inline constexpr const TCHAR* RING_START_KM = TEXT("SOLRingStartKm=");  // Ship spawns IN the start body's ring
                                                                                  // plane at this radius from its centre
        inline constexpr const TCHAR* RING_HEIGHT_KM = TEXT("SOLRingHeightKm="); // ...and this far above that plane
        inline constexpr const TCHAR* WRITE_PLAYER_GUIDE = TEXT("SOLWritePlayerGuide"); // Write Docs/PLAYER_GUIDE.md
                                                                                         // and its images (SDD 33,
                                                                                         // its writer lands in 33e)
        inline constexpr const TCHAR* SMOKE_SHOT = TEXT("SOLSmokeShot=");       // Screenshot after N s, then quit
        inline constexpr const TCHAR* SMOKE_CONSOLE = TEXT("SOLSmokeConsole="); // Console commands ('|'-separated) run
                                                                                  // just before the -SOLSmokeShot screenshot
        inline constexpr const TCHAR* SMOKE_WARP_STEPS = TEXT("SOLSmokeWarpSteps="); // Time-warp steps up at start
                                                                                       // (visual checks under warp)
        inline constexpr const TCHAR* SMOKE_FLIGHT = TEXT("SOLSmokeFlight");    // Scripted ship flight, then quit
        inline constexpr const TCHAR* SMOKE_INPUT = TEXT("SOLSmokeInput");      // Scripted player input, then quit
        inline constexpr const TCHAR* SMOKE_HUD = TEXT("SOLSmokeHud");          // Scripted HUD/F3 panel input, then quit
        inline constexpr const TCHAR* SMOKE_MAP = TEXT("SOLSmokeMap");          // Scripted jump-map input, then quit
        inline constexpr const TCHAR* SMOKE_MAP_PICK = TEXT("SOLSmokeMapPick"); // Scripted map destination pick, quit
        inline constexpr const TCHAR* SMOKE_MAP_ALIGN = TEXT("SOLSmokeMapAlign"); // Map icons on their lines
        inline constexpr const TCHAR* SMOKE_ROUTE = TEXT("SOLSmokeRoute");      // Map modes and a route, then quit
        inline constexpr const TCHAR* SMOKE_ROUTE_OVERLAY = TEXT("SOLSmokeRouteOverlay");   // Route overlay, then quit
        inline constexpr const TCHAR* SMOKE_HOLOGRAM = TEXT("SOLSmokeHologram");   // Map hologram look, then quit
        // Overlay keys, icons, Overlay Menu and cursor, then quit (SDD 22; smoke settings slot)
        inline constexpr const TCHAR* SMOKE_OVERLAY = TEXT("SOLSmokeOverlay");
        // Hover, radial menu (Target, Info, Move To, Follow, Match speed, Attack, Jump To), cancels and the map list
        // menu, then quit (SDD 22; smoke settings slot)
        inline constexpr const TCHAR* SMOKE_INTERACT = TEXT("SOLSmokeInteract");
        // With -SOLSmokeInteract: profile while the radial menu is open (ProfileGPU, stat dumpave), for the SDD's cost
        inline constexpr const TCHAR* SMOKE_INTERACT_PROFILE = TEXT("SOLSmokeInteractProfile");
        // Chase Down from the radial menu: bursts, the meter floor, intercept, hold, cancels, margins, standoff, warp,
        // boost and a lost target, then quit (SDD 32 section 5; smoke settings slot)
        inline constexpr const TCHAR* SMOKE_CHASE = TEXT("SOLSmokeChase");
        // With -SOLSmokeChase: cost captures (stat dumpsum / dumpmax of the SOL ship stats) during the 20 km chase, the
        // Moon chase, and a 1 d/s warp chase and autopilot, for the SDD's cost numbers
        inline constexpr const TCHAR* SMOKE_CHASE_PROFILE = TEXT("SOLSmokeChaseProfile");
        // Space dust speed cues: scripted speeds, dust visibility checks and screenshots, then quit (SDD 43)
        inline constexpr const TCHAR* SMOKE_DUST = TEXT("SOLSmokeDust");
        // With -SOLSmokeDust in -game: cost captures (stat dumpave of STAT_SOL_SpaceDustUpdate, ProfileGPU) for the SDD
        inline constexpr const TCHAR* SMOKE_DUST_PROFILE = TEXT("SOLSmokeDustProfile");
        // In -game: cost captures (ProfileGPU, stat dumpave of HUD Time and the Slate paint) of the in-flight menus and
        // cards, one pass per menu, logged against the pre-conversion baseline, then quit (SDD 42 section 11; smoke
        // settings slot)
        inline constexpr const TCHAR* SMOKE_HOLO_MENU_PROFILE = TEXT("SOLSmokeHoloMenuProfile");
        // The hologram menu kit and menus: subsystem, cues, the HUD's menu layer, a frame preview at strengths 1, 0
        // and 2, the cursor's strength, then quit (SDD 42 section 12; smoke settings slot)
        inline constexpr const TCHAR* SMOKE_HOLO_MENU = TEXT("SOLSmokeHoloMenu");
        // In -game: cost captures (ProfileGPU, stat dumpave of the Slate paint) of the main menu and the pause menu, each
        // with Settings open, at 1920x1080 and 1280x720, logged against the pre-46c baseline, then quit (SDD 46 section
        // 13; smoke settings slot; the run starts in the main menu). With -ResX / -ResY other than 1920x1080 (46h, the
        // Varjo XR-4 sizes 3840x3744 or 7680x3744, or the 4K proxy 3840x2160) it is a view run instead: nine screens at
        // that size, the menu's share judged against the VR floor of Docs/Research/VR_PERFORMANCE_TARGETS.md and the
        // whole frame's p99 logged against the XR-4 line and the 4K / 100 Hz proxy (informational)
        inline constexpr const TCHAR* SMOKE_SOLID_MENU_PROFILE = TEXT("SOLSmokeSolidMenuProfile");
        // With -SOLSmokeSolidMenuProfile: every pass shows this Settings tab (its ESOLSettingsTab position, e.g. 3 =
        // Gameplay with four sliders) instead of the opening tab (46e follow-up: the slider fill's cost)
        inline constexpr const TCHAR* SMOKE_SOLID_MENU_PROFILE_TAB = TEXT("SOLSmokeSolidMenuProfileTab=");
        // The solid menu kit: assets and fallback, the restyled buttons' hover / focus / selected / disabled looks
        // against SOLSolidMenu::EvaluateButton, an enabled / disabled comparison board of every control type, strength
        // 0 and 2, screenshots, then quit (SDD 46 section 14; smoke settings slot; the run starts in the main menu)
        inline constexpr const TCHAR* SMOKE_SOLID_MENU = TEXT("SOLSmokeSolidMenu");
        inline constexpr const TCHAR* SMOKE_JUMP = TEXT("SOLSmokeJump");        // Scripted pick + jump, then quit
        inline constexpr const TCHAR* SMOKE_LEVEL = TEXT("SOLSmokeLevel");      // Scripted surface-lock (L), then quit
        inline constexpr const TCHAR* COMBAT_DEMO = TEXT("SOLCombatDemo");      // Drops a target and shoots it, repeatedly
        inline constexpr const TCHAR* COMBAT_STRESS = TEXT("SOLCombatStress="); // With -SOLCombatDemo: keeps this many
                                                                                  // extra bolts alive (load test)
        inline constexpr const TCHAR* COMBAT_DEMO_HOLD = TEXT("SOLCombatDemoHold="); // With -SOLCombatDemo: the first
                                                                                  // target is left unshot this many s,
                                                                                  // with frame screenshots (issue #15)
        inline constexpr const TCHAR* SPECTATOR = TEXT("SOLSpectator");         // Debug free-fly pawn, not the ship
        inline constexpr const TCHAR* NPC_COUNT = TEXT("SOLNpcCount=");          // NPC ships to spawn (SDD 11; "250k",
                                                                                  // "1M" accepted, see ParseNpcCount)
        inline constexpr const TCHAR* NPC_SEED = TEXT("SOLNpcSeed=");            // NPC population seed
        inline constexpr const TCHAR* NPC_BATTLE_SHIPS = TEXT("SOLNpcBattleShips="); // NPC battle ships near the
                                                                                  // player (Demo option; 10d tests)
        inline constexpr const TCHAR* NPC_FACE_BATTLE = TEXT("SOLNpcFaceBattle="); // Turn the ship toward this
                                                                                  // NPC battle after each spawn (10d
                                                                                  // measurements and screenshots)
        inline constexpr const TCHAR* NPC_RAMP = TEXT("SOLNpcRamp");             // Ramp up to -SOLNpcCount, log each
                                                                                  // stage, then quit
        inline constexpr const TCHAR* STAR_SMOKE = TEXT("SOLStarSmoke=");       // Comma-separated FOVs (deg): one
                                                                                  // screenshot each, look input locked,
                                                                                  // then quit (SDD 13 star checks)

        // Menu flow (SDD 8, 8b): any other -SOL* switch starts straight in flight (verification runs); -SOLMenu forces
        // the main menu anyway, -SOLNoMenu skips it, and -SOLMenuSmoke=<all|screens|flow> runs the menu script
        inline constexpr const TCHAR* SWITCH_PREFIX = TEXT("SOL");              // Prefix of every project switch
        inline constexpr const TCHAR* MENU = TEXT("SOLMenu");                   // Start in the main menu regardless
        inline constexpr const TCHAR* NO_MENU = TEXT("SOLNoMenu");              // Skip the main menu
        inline constexpr const TCHAR* MENU_SMOKE = TEXT("SOLMenuSmoke=");       // Scripted menu walk, then quit
        inline constexpr const TCHAR* MENU_SMOKE_NAME = TEXT("SOLMenuSmoke");   // ...its switch name without the value
    }

    // Asset and content paths
    namespace Paths
    {
        inline constexpr const TCHAR* BODY_MESH = TEXT("/Engine/BasicShapes/Sphere.Sphere");
        inline constexpr const TCHAR* BODY_MATERIAL = TEXT("/Game/SOL/Materials/M_SOLBody.M_SOLBody");
        inline constexpr const TCHAR* TEST_MAP = TEXT("/Game/Maps/SOL_Test");

        // Ring far field (SDD 6 Amendment 11): one shared, parameterized Niagara system authored live via the
        // editor's NiagaraToolsets API, not a content-pipeline asset
        inline constexpr const TCHAR* RING_FAR_NIAGARA_SYSTEM = TEXT("/Game/SOL/Rings/NS_SOLRingFar.NS_SOLRingFar");

        // Ring dense rock layer (SDD 6 Amendment 15): GPU mesh-particle carpet around the camera near a ring
        inline constexpr const TCHAR* RING_DENSE_NIAGARA_SYSTEM = TEXT("/Game/SOL/Rings/NS_SOLRingDense.NS_SOLRingDense");
        // Ring shadow state near the viewpoint, read by M_SOLRingRock (SDD 16; SOL::RingShadowMpcParams)
        inline constexpr const TCHAR* RING_SHADOW_PARAMETER_COLLECTION =
            TEXT("/Game/SOL/Rings/MPC_SOLRingShadow.MPC_SOLRingShadow");
        // Shadowed rock material of the dense Niagara layer and the near-field Mass ISM (Tools/Rings/
        // create_ring_shadow_assets.py; SOL::RingRockMaterialParams)
        inline constexpr const TCHAR* RING_ROCK_MATERIAL = TEXT("/Game/SOL/Rings/M_SOLRingRock.M_SOLRingRock");

        // Space dust speed cues (SDD 43): the camera-local Niagara particle system and its point material
        inline constexpr const TCHAR* SPACE_DUST_NIAGARA_SYSTEM =
            TEXT("/Game/SOL/SpaceDust/NS_SOLSpaceDust.NS_SOLSpaceDust");
        inline constexpr const TCHAR* SPACE_DUST_MATERIAL = TEXT("/Game/SOL/SpaceDust/M_SOLSpaceDust.M_SOLSpaceDust");

        // Combat visuals (SDD 7, 7c), authored live through the editor's Niagara and material scripting APIs: the
        // (CPU-sim) bolt renderer, the four pooled one-shot effects, and the target mesh and its custom-data material. Each is
        // optional: a missing asset disables only its own effect, with a warning
        // NPC renderer (SDD 11 section 3.4, Part 10c): GPU point material (rail evaluation in the vertex shader) and
        // the placeholder hull material; the two placeholder hulls are engine basic shapes (open question 10)
        inline constexpr const TCHAR* NPC_POINTS_MATERIAL = TEXT("/Game/SOL/Npc/M_SOLNpcPoints.M_SOLNpcPoints");
        inline constexpr const TCHAR* NPC_HULL_MATERIAL = TEXT("/Game/SOL/Npc/M_SOLNpcHull.M_SOLNpcHull");
        inline constexpr const TCHAR* NPC_HULL_MESH_FACTION_0 = TEXT("/Engine/BasicShapes/Cone.Cone");
        inline constexpr const TCHAR* NPC_HULL_MESH_FACTION_1 = TEXT("/Engine/BasicShapes/Cube.Cube");
        inline constexpr const TCHAR* COMBAT_BOLT_NIAGARA_SYSTEM = TEXT("/Game/SOL/Combat/NS_SOLBolts.NS_SOLBolts");
        inline constexpr const TCHAR* COMBAT_IMPACT_NIAGARA_SYSTEM = TEXT("/Game/SOL/Combat/NS_SOLImpact.NS_SOLImpact");
        inline constexpr const TCHAR* COMBAT_SHIELD_BREAK_NIAGARA_SYSTEM =
            TEXT("/Game/SOL/Combat/NS_SOLShieldBreak.NS_SOLShieldBreak");
        inline constexpr const TCHAR* COMBAT_SPARK_NIAGARA_SYSTEM = TEXT("/Game/SOL/Combat/NS_SOLSpark.NS_SOLSpark");
        inline constexpr const TCHAR* COMBAT_EXPLOSION_NIAGARA_SYSTEM =
            TEXT("/Game/SOL/Combat/NS_SOLExplosion.NS_SOLExplosion");
        inline constexpr const TCHAR* COMBAT_TARGET_MESH = TEXT("/Engine/BasicShapes/Sphere.Sphere");
        inline constexpr const TCHAR* COMBAT_TARGET_MATERIAL = TEXT("/Game/SOL/Combat/M_SOLTarget.M_SOLTarget");

        // Sound classes (SDD 8, 8d), created by Tools/CreateSOLAudioClasses.py (keep both in sync): master, with the
        // effects (every S_SOL* combat wave) and music classes as its children. The settings volumes override them
        // through one runtime sound mix
        inline constexpr const TCHAR* SOUND_CLASS_MASTER = TEXT("/Game/SOL/Audio/SC_SOLMaster.SC_SOLMaster");
        inline constexpr const TCHAR* SOUND_CLASS_SFX = TEXT("/Game/SOL/Audio/SC_SOLSfx.SC_SOLSfx");
        inline constexpr const TCHAR* SOUND_CLASS_MUSIC = TEXT("/Game/SOL/Audio/SC_SOLMusic.SC_SOLMusic");

        // Placeholder ship: engine primitives (100 cm across, centered) and the engine's lit basic material
        inline constexpr const TCHAR* SHIP_PART_CUBE = TEXT("/Engine/BasicShapes/Cube.Cube");
        inline constexpr const TCHAR* SHIP_PART_CONE = TEXT("/Engine/BasicShapes/Cone.Cone");
        inline constexpr const TCHAR* SHIP_PART_CYLINDER = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
        inline constexpr const TCHAR* SHIP_PART_SPHERE = TEXT("/Engine/BasicShapes/Sphere.Sphere");
        inline constexpr const TCHAR* SHIP_HULL_MATERIAL =
            TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

        // Orbit lines (SDD 17), created by Tools/OrbitLines/create_orbit_line_assets.py (keep both in sync)
        inline constexpr const TCHAR* ORBIT_LINE_MATERIAL = TEXT("/Game/SOL/OrbitLines/M_SOLOrbitLine.M_SOLOrbitLine");
        // Map hologram look (SDD 20 section 2.4), built by Tools/Map/create_hologram_assets.py
        inline constexpr const TCHAR* HOLOGRAM_BODY_MATERIAL =
            TEXT("/Game/SOL/Map/M_SOLHologramBody.M_SOLHologramBody");
        inline constexpr const TCHAR* HOLOGRAM_RING_MATERIAL =
            TEXT("/Game/SOL/Map/M_SOLHologramRing.M_SOLHologramRing");
        inline constexpr const TCHAR* HOLOGRAM_POST_MATERIAL =
            TEXT("/Game/SOL/Map/M_SOLHologramPost.M_SOLHologramPost");
        inline constexpr const TCHAR* HOLOGRAM_RING_MESH = TEXT("/Engine/BasicShapes/Plane.Plane");
        // Flight HUD shape material (SDD 21), built by Tools/UI/create_hud_shape_material.py
        inline constexpr const TCHAR* HUD_SHAPE_MATERIAL = TEXT("/Game/SOL/UI/M_SOLHudShape.M_SOLHudShape");
        inline constexpr const TCHAR* ORBIT_LINE_PARAMETER_COLLECTION =
            TEXT("/Game/SOL/OrbitLines/MPC_SOLOrbitLine.MPC_SOLOrbitLine");

        // Star field (SDD 5), created by Tools/StarField/import_star_field.py (keep both in sync): the faint-star
        // TextureCube and the bright-star USOLStarFieldData
        inline constexpr const TCHAR* STAR_FIELD_CUBE =
            TEXT("/Game/SOL/StarField/T_SOLStarFieldCube.T_SOLStarFieldCube");
        inline constexpr const TCHAR* STAR_FIELD_DATA = TEXT("/Game/SOL/StarField/DA_SOLStarField.DA_SOLStarField");

        // Star field: assets copied from the CelestialVault plugin (SDD 5 decision 5; the plugin is never enabled).
        // The camera-facing quad (100 cm, facing +X), the round star mask, the Milky Way texture and the plugin's star
        // material, kept as a reference for the sprite material. The copied billboard material functions are only
        // referenced from material graphs, so they have no C++ path
        inline constexpr const TCHAR* STAR_QUAD_MESH =
            TEXT("/Game/SOL/StarField/CelestialVault/SM_Plane_FacingX.SM_Plane_FacingX");
        inline constexpr const TCHAR* STAR_MASK_TEXTURE =
            TEXT("/Game/SOL/StarField/CelestialVault/T_StarMask_Round.T_StarMask_Round");
        inline constexpr const TCHAR* MILKY_WAY_TEXTURE =
            TEXT("/Game/SOL/StarField/CelestialVault/T_MilkyWay.T_MilkyWay");
        // REFERENCE ONLY, do not use at runtime: it expects CelestialVault's absolute brightness units and per-instance
        // data layout, not this project's flux relative to magnitude 8 (SDD 5 section 3.1, "As built (4b)")
        inline constexpr const TCHAR* STAR_REFERENCE_MATERIAL =
            TEXT("/Game/SOL/StarField/CelestialVault/M_Stars_EnergyConservative.M_Stars_EnergyConservative");

        // Star field runtime (SDD 5, 4c), created by Tools/StarField/create_star_field_materials.py (keep both in
        // sync): the bright-star sprite material, the faint-star sky material, and the sphere the sky is drawn on
        inline constexpr const TCHAR* STAR_SPRITE_MATERIAL =
            TEXT("/Game/SOL/StarField/M_SOLStarSprite.M_SOLStarSprite");
        inline constexpr const TCHAR* STAR_SKY_MATERIAL = TEXT("/Game/SOL/StarField/M_SOLStarSky.M_SOLStarSky");
        inline constexpr const TCHAR* STAR_SKY_MESH = TEXT("/Engine/BasicShapes/Sphere.Sphere");

        // Gas giant band shells (SDD 14, 14c), created by Tools/GasGiants/create_gas_giant_materials.py (keep both in
        // sync): the opaque cloud deck and the translucent haze, both drawn on BODY_MESH
        inline constexpr const TCHAR* GAS_GIANT_DECK_MATERIAL =
            TEXT("/Game/SOL/GasGiants/M_SOLGasGiantDeck.M_SOLGasGiantDeck");
        inline constexpr const TCHAR* GAS_GIANT_HAZE_MATERIAL =
            TEXT("/Game/SOL/GasGiants/M_SOLGasGiantHaze.M_SOLGasGiantHaze");
        inline constexpr const TCHAR* GAS_GIANT_CLOUD_MATERIAL =
            TEXT("/Game/SOL/GasGiants/M_SOLGasGiantCloud.M_SOLGasGiantCloud");

        // Solid menu assets (SDD 46 sections 9 and 10), imported by Tools/UI/import_solid_menu_assets.py and
        // Tools/UI/create_solid_menu_materials.py (those scripts read these names, keep all in sync). Package paths.
        inline constexpr const TCHAR* SOLID_MENU_FONT = TEXT("/Game/SOL/UI/Solid/Fonts/F_SOLMenu");
        inline constexpr const TCHAR* SOLID_HULL_MATERIAL = TEXT("/Game/SOL/UI/Solid/M_SOLSolidHull");
        inline constexpr const TCHAR* SOLID_TEX_BRUSHED = TEXT("/Game/SOL/UI/Solid/Textures/T_SOLSolidBrushed");
        inline constexpr const TCHAR* SOLID_TEX_RIVET = TEXT("/Game/SOL/UI/Solid/Textures/T_SOLSolidRivet");
        inline constexpr const TCHAR* SOLID_TEX_VENT = TEXT("/Game/SOL/UI/Solid/Textures/T_SOLSolidVent");
        inline constexpr const TCHAR* SOLID_TEX_HAZARD = TEXT("/Game/SOL/UI/Solid/Textures/T_SOLSolidHazard");

        // Hull material style instances (SDD 46 section 10), created by Tools/UI/create_solid_menu_materials.py; in the
        // order of ESOLSolidStyle (SOLID_STYLE_MATERIALS)
        inline constexpr const TCHAR* SOLID_MI_PANEL = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidPanel");
        inline constexpr const TCHAR* SOLID_MI_RECESS = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidRecess");
        inline constexpr const TCHAR* SOLID_MI_STACK_BUTTON = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidStackButton");
        inline constexpr const TCHAR* SOLID_MI_TAB = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidTab");
        inline constexpr const TCHAR* SOLID_MI_SMALL_BUTTON = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidSmallButton");
        inline constexpr const TCHAR* SOLID_MI_SLIDER_GROOVE = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidSliderGroove");
        inline constexpr const TCHAR* SOLID_MI_KNOB = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidKnob");
        inline constexpr const TCHAR* SOLID_MI_TOGGLE_SLOT = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidToggleSlot");
        inline constexpr const TCHAR* SOLID_MI_PUCK = TEXT("/Game/SOL/UI/Solid/MI_SOLSolidPuck");
        inline constexpr const TCHAR* SOLID_STYLE_MATERIALS[] = {
            SOLID_MI_PANEL, SOLID_MI_RECESS, SOLID_MI_STACK_BUTTON, SOLID_MI_TAB, SOLID_MI_SMALL_BUTTON,
            SOLID_MI_SLIDER_GROOVE, SOLID_MI_KNOB, SOLID_MI_TOGGLE_SLOT, SOLID_MI_PUCK };

        // The generated player guide (SDD 33 section 9), relative to FPaths::ProjectDir(): written by
        // -SOLWritePlayerGuide, compared by SOLTest.PlayerGuide.UpToDate
        inline constexpr const TCHAR* PLAYER_GUIDE_FILE = TEXT("Docs/PLAYER_GUIDE.md");
        // ... and its screenshots (D24: one per Help tab, plus the Keyboard tab with a key hovered), in that folder
        inline constexpr const TCHAR* PLAYER_GUIDE_IMAGE_DIR = TEXT("Docs/Images/");
        inline constexpr const TCHAR* PLAYER_GUIDE_IMAGE_KEYBOARD = TEXT("33-help-keyboard.png");
        inline constexpr const TCHAR* PLAYER_GUIDE_IMAGE_KEYBOARD_HOVER = TEXT("33-help-keyboard-hover.png");
        // The glossary tabs' images, in SOLMenuText::GLOSSARY_TABS order
        inline constexpr const TCHAR* PLAYER_GUIDE_IMAGE_GLOSSARY[] = { TEXT("33-help-glossary-flight-hud.png"),
            TEXT("33-help-glossary-targets-radar.png"), TEXT("33-help-glossary-overlays-route.png"),
            TEXT("33-help-glossary-menus-map.png") };
    }

    // Star field (SDD 5, 4c): radius of the sky sphere around the Unreal origin (4e12 cm = 4e7 km), inside the engine's
    // HALF_WORLD_MAX (~4.4e12 cm, EngineDefines.h) so its bounds stay within the large-world envelope. Depth-compressed
    // bodies never render beyond ~2.5e11 cm (DEFAULT_MAX_RENDER_DISTANCE_CM * (1 + 0.1 * ln(d / max)) stays below that
    // even for the map camera's farthest zoom), so the sky is 16x behind the farthest body. The engine's reverse-Z far
    // plane is infinite, so there is no far clip to stay inside, and a float vertex at 4e12 cm is off by ~1e-7 of its
    // distance, far below a pixel
    inline constexpr double STAR_FIELD_SKY_RADIUS_CM = 4.0e12;

    // Star field: distance of the bright-star sprites from the Unreal origin, inside the sky sphere so they draw over
    // it and still 8x behind the farthest body
    inline constexpr double STAR_FIELD_SPRITE_RADIUS_CM = 2.0e12;

    // Star field: translucency sort priority of the additive sprites, far below the default 0, so any later world-space
    // translucency (bolts, effects) draws over the stars instead of the camera-centered sprite bounds sorting nearest
    inline constexpr int32 STAR_SPRITE_TRANSLUCENCY_SORT_PRIORITY = -1000;

    // Star field: angular diameter of every bright-star quad as built (bounds only). The sprite material rescales each
    // quad to STAR_POINT_DIAMETER_PX output pixels every frame (SDD 13), so this only has to be at least as large as
    // the widest pixel kernel ever drawn: 0.02 rad covers 3 px at the 135 deg warp peak down to a 900 px wide output
    inline constexpr double STAR_SPRITE_ANGULAR_DIAMETER_RAD = 0.02;

    // Star field (SDD 13): diameter in output (post-upscale) pixels of every point star, sprites and the faint-star
    // cube's point reconstruction alike, at every FOV and resolution. Flux-conserving: brightness, not size, carries
    // magnitude. The kernel is saturate(1 - (d / r)^2)^2, so its half-maximum width is ~0.54 of this
    inline constexpr float STAR_POINT_DIAMETER_PX = 2.5f;

    // Star field (SDD 13 Amendment 1): V magnitude whose pixel sum is 1.0 (one display-white pixel's worth before
    // tonemapping) at the dark-sky exposure (EV100 0), at every FOV and resolution. Pogson below it: a star's peak
    // pixel is 0.611 * 10^(-0.4 (V - 2.5)) linear with the 2.5 px kernel, so it reaches 1.0 at V ~2.0 (about 50
    // stars on the whole sky), the naked-eye limit V 6.5 peaks at 0.0153 (just visible after the tonemapper's toe)
    // and V 8 at 0.0039. Replaces SDD 5's mapping, which put magnitude 8 there
    inline constexpr double STAR_FIELD_WHITE_MAGNITUDE = 2.5;

    // Exposure (SDD 13 decision 2, Amendment 1): ASOLBodyVisuals keeps a manual exposure and moves it itself from the
    // bodies in view (SOLSceneExposure). The minimum EV100 is the old fixed exposure, so dark sky (stars, the lit
    // bodies tuned against it) looks exactly as before; a view whose mean radiance is above the key adapts down by
    // log2(mean / key) EV, at most MAX - MIN (4x), which fades the stars (and the bright body itself)
    inline constexpr double EXPOSURE_MIN_EV100 = 0.0;
    inline constexpr double EXPOSURE_MAX_EV100 = 2.0;

    // Exposure: view mean radiance (emissive at EV100 0) above which the exposure adapts down. 0.05 = Earth (mean
    // albedo luminance ~0.18) fully lit and filling the view adapts ~1.2 EV, Jupiter, Mars or the Sun close up hit the
    // 2 EV cap, a half-lit or distant planet and the Sun from 1 AU (6e-5 of the view) do not adapt
    inline constexpr double EXPOSURE_KEY_RADIANCE = 0.05;

    // Exposure: time constant (s) of the exponential approach to the target EV, frame-rate independent; ~1 s to
    // settle within 10%
    inline constexpr double EXPOSURE_TIME_CONSTANT_SECONDS = 0.4;

    // Parameter names of STAR_SPRITE_MATERIAL and STAR_SKY_MATERIAL (Tools/StarField/create_star_field_materials.py)
    namespace StarFieldMaterialParams
    {
        inline constexpr const TCHAR* CUBE_TEXEL_SOLID_ANGLE = TEXT("CubeTexelSolidAngleSr"); // Sky: from data asset
        inline constexpr const TCHAR* BASE_ANGULAR_DIAMETER = TEXT("BaseAngularDiameterRad"); // Sprite: built quad size
        inline constexpr const TCHAR* POINT_DIAMETER_PX = TEXT("PointDiameterPx");            // Both: kernel width
        inline constexpr const TCHAR* CUBE_FACE_SIZE = TEXT("CubeFaceSize");                  // Sky: texels per face
        inline constexpr const TCHAR* BRIGHTNESS_SCALE = TEXT("BrightnessScale");             // Both materials

        // Per-instance custom data of the sprite: Flux * linear Color (R, G, B)
        inline constexpr int32 SPRITE_CUSTOM_DATA_FLOATS = 3;
    }

    // Vector parameter of SHIP_HULL_MATERIAL that sets its base color
    inline constexpr const TCHAR* SHIP_HULL_COLOR_PARAM = TEXT("Color");

    // Radius of the BODY_MESH sphere in its own units (the engine sphere is 100 cm across)
    inline constexpr double BODY_MESH_RADIUS_CM = 50.0;

    // Parameter names of the hologram materials (Tools/Map/create_hologram_assets.py creates them with these names)
    namespace HologramMaterialParams
    {
        inline constexpr const TCHAR* COLOR = TEXT("HoloColor");                 // Proxy and ring colour
        inline constexpr const TCHAR* INTENSITY = TEXT("HoloIntensity");         // Proxy and ring emissive scale
        inline constexpr const TCHAR* RING_INNER_FRACTION = TEXT("RingInnerFraction"); // Ring inner / outer radius
        inline constexpr const TCHAR* POST_STRENGTH = TEXT("Strength");          // Post-process overall strength
        inline constexpr const TCHAR* POST_TINT = TEXT("Tint");                  // Post-process tint colour
    }

    // Parameter names of HUD_SHAPE_MATERIAL (Tools/UI/create_hud_shape_material.py creates them with these names)
    namespace HudShapeMaterialParams
    {
        inline constexpr const TCHAR* EFFECTS_STRENGTH = TEXT("EffectsStrength");   // Settings slider (jitter)
        inline constexpr const TCHAR* COMPOSITE = TEXT("Composite");                 // 1 = grain and scanlines on
        inline constexpr const TCHAR* VIEW_WIDTH = TEXT("ViewWidthPx");              // Canvas size in pixels
        inline constexpr const TCHAR* VIEW_HEIGHT = TEXT("ViewHeightPx");
    }

    // Parameter names of SOLID_HULL_MATERIAL and its style instances (SDD 46 sections 4.3 and 10; Tools/UI/
    // create_solid_menu_materials.py creates them with these names). Lengths are UMG units (x DpiScale = pixels)
    namespace SolidMenuMaterialParams
    {
        // Pushed by USOLSolidMenuSubsystem, only when they change
        inline constexpr const TCHAR* EFFECTS_STRENGTH = TEXT("EffectsStrength");   // Sanitized HudEffectsStrength (Q1)
        inline constexpr const TCHAR* DPI_SCALE = TEXT("DpiScale");                 // UMG units -> physical pixels

        // Hull shape (FSOLSolidHull; clamped in the material as SOLSolidMenu::ClampHull)
        inline constexpr const TCHAR* CHAMFER_TL = TEXT("ChamferTL");
        inline constexpr const TCHAR* CHAMFER_TR = TEXT("ChamferTR");
        inline constexpr const TCHAR* CHAMFER_BR = TEXT("ChamferBR");
        inline constexpr const TCHAR* CHAMFER_BL = TEXT("ChamferBL");
        inline constexpr const TCHAR* TITLE_STEP_WIDTH = TEXT("TitleStepWidth");
        inline constexpr const TCHAR* TITLE_STEP_HEIGHT = TEXT("TitleStepHeight");
        inline constexpr const TCHAR* NOTCH_Y = TEXT("NotchY");                     // < 0: no notches
        inline constexpr const TCHAR* NOTCH_DEPTH = TEXT("NotchDepth");
        inline constexpr const TCHAR* NOTCH_HEIGHT = TEXT("NotchHeight");
        inline constexpr const TCHAR* CHIN_WIDTH = TEXT("ChinWidth");
        inline constexpr const TCHAR* CHIN_HEIGHT = TEXT("ChinHeight");
        inline constexpr const TCHAR* RECESS = TEXT("Recess");                      // 1: sunken recess, flat fill
        inline constexpr const TCHAR* INSET = TEXT("InsetPx");                      // Hull inset in the brush rect
        inline constexpr const TCHAR* MARK_SHIFT = TEXT("MarkShift");               // Puck: narrower by this fraction,
                                                                                    // right when the mark is set

        // Look
        inline constexpr const TCHAR* SHADOW_PX = TEXT("ShadowPx");
        inline constexpr const TCHAR* SHADOW_OPACITY = TEXT("ShadowOpacity");
        inline constexpr const TCHAR* GRAIN_AMOUNT = TEXT("GrainAmount");
        inline constexpr const TCHAR* GRAIN_TILE = TEXT("GrainTilePx");
        inline constexpr const TCHAR* BEVEL_PX = TEXT("BevelPx");
        inline constexpr const TCHAR* BEVEL_AMOUNT = TEXT("BevelAmount");
        inline constexpr const TCHAR* EDGE_PX = TEXT("EdgePx");
        inline constexpr const TCHAR* EDGE_HIGHLIGHT = TEXT("EdgeHighlight");
        inline constexpr const TCHAR* GLOW_PX = TEXT("GlowPx");
        inline constexpr const TCHAR* LED_X = TEXT("LedX");                         // < 0: no LED
        inline constexpr const TCHAR* LED_RADIUS = TEXT("LedRadius");
        inline constexpr const TCHAR* UNDERLINE_START = TEXT("UnderlineStart");
        inline constexpr const TCHAR* UNDERLINE_END = TEXT("UnderlineEnd");         // From the right edge
        inline constexpr const TCHAR* UNDERLINE_BOTTOM = TEXT("UnderlineBottom");   // < 0: no underline
        inline constexpr const TCHAR* UNDERLINE_PX = TEXT("UnderlinePx");
        inline constexpr const TCHAR* DISABLED_CUE_STYLE = TEXT("DisabledCueStyle"); // 0 hatch (a), 1 slot (b)
        inline constexpr const TCHAR* CUE_BAND_PX = TEXT("CueBandPx");
        inline constexpr const TCHAR* CUE_PERIOD_PX = TEXT("CuePeriodPx");

        // Colours (linear) and textures
        inline constexpr const TCHAR* BASE_COLOR = TEXT("BaseColor");               // Rest hull / recess fill
        inline constexpr const TCHAR* LIFTED_COLOR = TEXT("LiftedColor");           // ... with the hover amount
        inline constexpr const TCHAR* PRESSED_COLOR = TEXT("PressedColor");
        inline constexpr const TCHAR* DISABLED_COLOR = TEXT("DisabledColor");
        inline constexpr const TCHAR* EDGE_COLOR = TEXT("EdgeColor");
        inline constexpr const TCHAR* SHADOW_COLOR = TEXT("ShadowColor");
        inline constexpr const TCHAR* LED_OFF_COLOR = TEXT("LedOffColor");
        inline constexpr const TCHAR* ACCENT_COLOR = TEXT("AccentColor");           // Amber hover
        inline constexpr const TCHAR* MARK_COLOR = TEXT("MarkColor");               // Cyan selected mark
        inline constexpr const TCHAR* CUE_COLOR = TEXT("CueColor");                 // Disabled hatch
        inline constexpr const TCHAR* GRAIN_TEXTURE = TEXT("GrainTexture");
        inline constexpr const TCHAR* CUE_MASK = TEXT("CueMask");
    }

    // Parameter names of BODY_MATERIAL (created by Tools/CreateSOLContent.py; keep both in sync)

    namespace BodyMaterialParams
    {
        inline constexpr const TCHAR* COLOR_A = TEXT("ColorA");                 // Base color
        inline constexpr const TCHAR* COLOR_B = TEXT("ColorB");                 // Band / noise color
        inline constexpr const TCHAR* POLAR_COLOR = TEXT("PolarColor");         // Polar cap color
        inline constexpr const TCHAR* EMISSIVE_COLOR = TEXT("EmissiveColor");   // Emissive (Sun only)
        inline constexpr const TCHAR* BAND_FREQUENCY = TEXT("BandFrequency");   // Latitude bands per cm of mesh height
        inline constexpr const TCHAR* BAND_STRENGTH = TEXT("BandStrength");     // 0 = no bands
        inline constexpr const TCHAR* NOISE_SCALE = TEXT("NoiseScale");         // Noise feature frequency
        inline constexpr const TCHAR* NOISE_STRENGTH = TEXT("NoiseStrength");   // 0 = no noise
        inline constexpr const TCHAR* NOISE_BIAS = TEXT("NoiseBias");           // Raises the noise threshold (less B)
        inline constexpr const TCHAR* POLAR_START = TEXT("PolarStart");         // |sin latitude| where caps begin; > 1 = none
        inline constexpr const TCHAR* SUN_DIRECTION = TEXT("SunDirection");     // Unit vector body -> Sun, Unreal axes
        inline constexpr const TCHAR* SUN_ILLUMINANCE = TEXT("SunIlluminance"); // Sunlight illuminance (lux) for shading
        inline constexpr const TCHAR* AMBIENT_LIGHT = TEXT("AmbientLight");     // Night-side fraction of full sunlight
    }

    // Gas giants (SDD 14, plan "14a contract"). Artistic multiplier on band and storm drift relative to the body's
    // spin, so the bands visibly drift at 1x: Jupiter's ~1.4e-6 rad/s (~100 m/s) equatorial jet moves ~0.96 deg per
    // real minute and takes ~6.2 real hours per full relative turn
    inline constexpr double GAS_GIANT_BAND_SPEED_FACTOR = 200.0;

    // Gas giants (SDD 14 Amendment 4): above this time warp the band and storm drift runs at this warp's speed
    // instead (SOLGasGiant::CappedDriftStepSeconds), so at 1000x Jupiter's equator still moves only ~0.27 deg per frame
    // at 60 fps instead of aliasing into noise; below it the drift is the exact function of sim time
    inline constexpr double GAS_GIANT_BAND_MAX_TIME_WARP = 1000.0;

    // Gas giants: latitudes sampled by SOLGasGiant::ComputeBandPhases (four float4 material parameters)
    inline constexpr int32 GAS_GIANT_BAND_SAMPLE_COUNT = 16;

    // Gas giants (SDD 14 Amendment 3): half-width, as a fraction of the gap between two band samples, of the zone in
    // which the materials cross-fade the two samples' rigidly advected noise (SOLGasGiant::BandBlendAt). The materials
    // hard-code it (Tools/GasGiants/create_gas_giant_materials.py BAND_BLEND_HALF_WIDTH; keep both in sync)
    inline constexpr double GAS_GIANT_BAND_BLEND_HALF_WIDTH = 0.25;

    // Gas giants: base storm seed (a giant's seed is this plus its body index), the upper clamp on storms per giant
    // and the rejection-sampling attempts per storm
    inline constexpr int32 GAS_GIANT_STORM_SEED = 20261014;
    inline constexpr int32 GAS_GIANT_MAX_STORMS = 8;
    inline constexpr int32 GAS_GIANT_STORM_PLACEMENT_ATTEMPTS = 64;

    // Gas giants: the near volumetric layer is fully on within FADE_IN planet radii of the centre, fully off beyond
    // FADE_OUT, and its resources are released after it has been hidden for RELEASE_DELAY real seconds
    inline constexpr double GAS_GIANT_NEAR_FADE_IN_RADII = 2.0;
    inline constexpr double GAS_GIANT_NEAR_FADE_OUT_RADII = 4.0;
    inline constexpr double GAS_GIANT_NEAR_RELEASE_DELAY_SECONDS = 20.0;

    // Gas giants (14c): radius of the translucent haze shell relative to the planet radius (the opaque deck is at
    // exactly 1, replacing the placeholder body mesh), and the haze's drift relative to the deck's, so the high clouds
    // visibly slide over the bands
    inline constexpr double GAS_GIANT_HAZE_RADIUS_SCALE = 1.003;
    inline constexpr double GAS_GIANT_HAZE_SPEED_SCALE = 1.35;

    // Gas giants: translucency sort priority of the haze shell, below the default 0 so a ring passing in front of the
    // planet (NS_SOLRingFar, priority 0) draws over the haze instead of the shared planet-centred bounds deciding
    inline constexpr int32 GAS_GIANT_HAZE_TRANSLUCENCY_SORT_PRIORITY = -10;

    // Gas giants: storms are generated within this latitude band and angular-radius range (rad); the authored primary
    // storms (Jupiter's Great Red Spot, Neptune's dark spot) are set per planet in ASOLGasGiantVisuals
    inline constexpr double GAS_GIANT_STORM_MAX_LATITUDE_RAD = 1.1;
    inline constexpr double GAS_GIANT_STORM_MIN_RADIUS_RAD = 0.015;
    inline constexpr double GAS_GIANT_STORM_MAX_RADIUS_RAD = 0.04;

    // Gas giants: storm ovals are this many times longer east-west than north-south (the deck material's
    // StormShape.x; generated ovals keep this clearance from the primary storm); storm vector .w kinds (1 primary,
    // drawn in StormColor; 0.5 light oval; 0 none) and the threshold above which the material uses StormColor
    inline constexpr double GAS_GIANT_STORM_EAST_WEST_STRETCH = 1.6;
    inline constexpr float GAS_GIANT_PRIMARY_STORM_KIND = 1.0f;
    inline constexpr float GAS_GIANT_OVAL_STORM_KIND = 0.5f;
    inline constexpr float GAS_GIANT_PRIMARY_STORM_KIND_THRESHOLD = 0.75f;

    // Gas giants: shell materials are animated only while a shell was rendered within this many real seconds (or the
    // giant is in the camera's view cone), and their night-side light floor as a fraction of full sunlight
    inline constexpr float GAS_GIANT_RECENT_RENDER_SECONDS = 0.5f;
    inline constexpr float GAS_GIANT_AMBIENT_LIGHT = 0.02f;

    // Gas giants (14d, SDD 14 Amendment 3): the near volumetric cloud layer (engine VolumetricCloud around a
    // SkyAtmosphere that only carries the planet centre). Layer bottom above the deck and thickness (km); view samples
    // (scale of the engine's 96 per ray), the view ray length through the layer and the self-shadow ray (km)
    inline constexpr float GAS_GIANT_CLOUD_LAYER_BOTTOM_KM = 2.0f;
    inline constexpr float GAS_GIANT_CLOUD_LAYER_HEIGHT_KM = 40.0f;
    inline constexpr float GAS_GIANT_CLOUD_VIEW_SAMPLE_SCALE = 0.25f;
    inline constexpr float GAS_GIANT_CLOUD_SHADOW_SAMPLE_SCALE = 0.5f;
    inline constexpr float GAS_GIANT_CLOUD_TRACING_MAX_KM = 100.0f;
    inline constexpr float GAS_GIANT_CLOUD_SHADOW_TRACING_KM = 5.0f;

    // Gas giants (14d): near cloud look (M_SOLGasGiantCloud CloudNoise / CloudAmbientLuminance). Largest noise feature
    // (km; 3 octaves, so ~300/150/75 km), cloud coverage over belts and zones, extinction (1/m) at full density, and
    // the ambient emission factor standing in for sky light on cloud sides away from the Sun
    inline constexpr float GAS_GIANT_CLOUD_FEATURE_KM = 300.0f;
    inline constexpr float GAS_GIANT_CLOUD_BELT_COVERAGE = 0.35f;
    inline constexpr float GAS_GIANT_CLOUD_ZONE_COVERAGE = 0.6f;
    inline constexpr float GAS_GIANT_CLOUD_EXTINCTION_PER_M = 0.0005f;
    inline constexpr float GAS_GIANT_CLOUD_AMBIENT_LUMINANCE = 0.2f;

    // Parameter names of Paths::GAS_GIANT_DECK_MATERIAL and GAS_GIANT_HAZE_MATERIAL
    // (Tools/GasGiants/create_gas_giant_materials.py; keep both in sync)
    namespace GasGiantMaterialParams
    {
        // Band phases (turns, [0, 1)) of the GAS_GIANT_BAND_SAMPLE_COUNT latitudes, south to north, 4 per vector
        inline constexpr const TCHAR* BAND_PHASES[] = { TEXT("BandPhases0"), TEXT("BandPhases1"), TEXT("BandPhases2"),
            TEXT("BandPhases3") };
        inline constexpr const TCHAR* HAZE_PHASES[] = { TEXT("HazePhases0"), TEXT("HazePhases1"), TEXT("HazePhases2"),
            TEXT("HazePhases3") };

        // Storm i: (latitude rad, body-frame longitude rad now, angular radius rad, kind: 1 primary, 0.5 oval, 0 none)
        inline constexpr const TCHAR* STORMS[] = { TEXT("Storm0"), TEXT("Storm1"), TEXT("Storm2"), TEXT("Storm3"),
            TEXT("Storm4"), TEXT("Storm5"), TEXT("Storm6"), TEXT("Storm7") };

        inline constexpr const TCHAR* ZONE_COLOR = TEXT("ZoneColor");     // Light bands (and the haze colour)
        inline constexpr const TCHAR* BELT_COLOR = TEXT("BeltColor");     // Dark bands
        inline constexpr const TCHAR* ACCENT_COLOR = TEXT("AccentColor"); // Turbulent detail tint
        inline constexpr const TCHAR* POLAR_COLOR = TEXT("PolarColor");   // Polar regions
        inline constexpr const TCHAR* STORM_COLOR = TEXT("StormColor");   // Primary storm
        inline constexpr const TCHAR* SHAPE_PARAMS = TEXT("ShapeParams"); // (bands per unit sin(lat), turbulence or
                                                                           // haze opacity, noise scale, polar start)
        inline constexpr const TCHAR* SUN_DIRECTION = TEXT("SunDirection");     // Unit vector body -> Sun, Unreal axes
        inline constexpr const TCHAR* SUN_ILLUMINANCE = TEXT("SunIlluminance"); // Sunlight illuminance (lux)
        inline constexpr const TCHAR* AMBIENT_LIGHT = TEXT("AmbientLight");     // Night-side fraction of sunlight
        inline constexpr const TCHAR* STORM_SHAPE = TEXT("StormShape"); // Deck: (east-west stretch, primary threshold)

        // Near cloud material only (14d): it also takes BAND_PHASES, ZONE_COLOR, BELT_COLOR and SHAPE_PARAMS
        inline constexpr const TCHAR* PLANET_CENTER = TEXT("PlanetCenter"); // Render-space centre (cm), w = radius cm
        inline constexpr const TCHAR* BODY_AXIS_X = TEXT("BodyAxisX");      // Body frame axes in Unreal space
        inline constexpr const TCHAR* BODY_AXIS_Y = TEXT("BodyAxisY");
        inline constexpr const TCHAR* BODY_AXIS_Z = TEXT("BodyAxisZ");
        inline constexpr const TCHAR* NEAR_BLEND = TEXT("NearBlend");       // NearLayerBlend; scales density
        // Ring shadow (SDD 16), deck, haze and cloud: Sun direction in the mesh's local axes, w = Sun angular radius
        inline constexpr const TCHAR* RING_SHADOW_SUN = TEXT("RingShadowSun");
        // Ring shadow: (profile index or -1 for off, planet radius km, -, -)
        inline constexpr const TCHAR* RING_SHADOW_PARAMS = TEXT("RingShadowParams");
        inline constexpr const TCHAR* CLOUD_LAYER = TEXT("CloudLayer");     // (layer bottom km, layer thickness km)
        // (feature km, belt coverage, zone coverage, extinction 1/m)
        inline constexpr const TCHAR* CLOUD_NOISE = TEXT("CloudNoise");
        inline constexpr const TCHAR* CLOUD_AMBIENT_LUMINANCE = TEXT("CloudAmbientLuminance");
    }
    static_assert(UE_ARRAY_COUNT(GasGiantMaterialParams::BAND_PHASES) * 4 == GAS_GIANT_BAND_SAMPLE_COUNT,
        "Band phase vector parameters must hold exactly GAS_GIANT_BAND_SAMPLE_COUNT phases");
    static_assert(UE_ARRAY_COUNT(GasGiantMaterialParams::STORMS) == GAS_GIANT_MAX_STORMS,
        "One storm vector parameter per possible storm");

    // NPC scale demo (SDD 11, issue #11): population cap, ramp stages, Demo-page count presets, frame target, seed
    inline constexpr int32 NPC_MAX_COUNT = 1000000;
    inline constexpr int32 NPC_RAMP_STAGE_COUNTS[] = { 10000, 100000, 250000, 500000, 1000000 };
    inline constexpr int32 NPC_COUNT_PRESETS[] = { 0, 1000, 10000, 100000, 250000, 500000, 1000000 };
    inline constexpr double NPC_TARGET_FPS = 60.0;
    inline constexpr uint32 NPC_DEFAULT_SEED = 0x50C1A11Du;

    // NPC population (SDD 11 sections 3.1-3.2): spawn slicing, Mass chunks, class hash, temporal cull
    inline constexpr int32 NPC_SPAWN_ENTITIES_PER_FRAME = 25000;       // Spawn time-slice: entities created per frame
                                                                       // (SDD 11 Amendment 6)
    inline constexpr int32 NPC_CHUNK_MEMORY_BYTES = 512 * 1024;        // Mass chunk size of the NPC archetypes (the
                                                                       // engine maximum; default is 128 KB)
    inline constexpr uint32 NPC_CLASS_HASH_STREAM = 0xC1A55u;          // HashUnit stream picking each ship's class
    inline constexpr double NPC_WAKE_SPEED_MARGIN = 1.25;              // Safety factor on each group's ship speed
                                                                       // bound (temporal cull, Amendment 1)

    // NPC renderer (SDD 11 section 3.4, Part 10c)
    inline constexpr int32 NPC_POINT_BLOCK_SHIPS = 4096;               // Ships per point-block mesh (one ISM instance)
    inline constexpr int32 NPC_RAIL_TEXTURE_WIDTH = 2048;              // Rail texture width (2 texels per ship)
    inline constexpr int32 NPC_CPU_TEXTURE_WIDTH = 1024;               // CPU point texture width (1 texel per ship)
    inline constexpr int32 NPC_GROUP_TEXTURE_WIDTH = 64;               // Group texture width (max groups)
    inline constexpr double NPC_RENDER_SPACE_BOUNDS_CM = 5.0e11;       // Half-extent of the NPC point layers' and
                                                                       // hulls' fixed bounds (all render space)
    inline constexpr double NPC_GPU_RAIL_EPOCH_S = 1.0e4;              // The GPU rail copy is re-based once the rail
                                                                       // time runs this far (sim s) from its epoch...
    inline constexpr double NPC_GPU_RAIL_EPOCH_MIN_INTERVAL_S = 2.0;   // ...at most once per this many real seconds
                                                                       // (Amendment 7)
    inline constexpr double NPC_POINT_MIN_PIXELS = 2.0;                // Point diameter floor, in pixels
    inline constexpr double NPC_HULL_LENGTH_M = 30.0;                  // Placeholder hull length
    inline constexpr double NPC_HULL_MIN_PIXELS = 3.0;                 // Instanced hulls never shrink below this
    inline constexpr double NPC_REFERENCE_VIEWPORT_WIDTH_PX = 1920.0;  // Pixel-size fallback before a viewport exists
    inline constexpr int32 NPC_HULL_MIN_INSTANCES = 1024;              // Hull ISM instance count floor once grown...
    inline constexpr int32 NPC_HULL_SHRINK_FRAMES = 300;               // ...shrunk after this many frames in a row
                                                                       // using under a quarter of the instances
    inline constexpr double NPC_POINT_BRIGHTNESS_REF_M = 2.0e6;        // Full brightness up to this distance...
    inline constexpr double NPC_POINT_BRIGHTNESS_EXPONENT = 0.5;       // ...then falls off as (ref / d)^exponent...
    inline constexpr double NPC_POINT_BRIGHTNESS_MIN = 0.04;           // ...down to this floor (open question 9)
    inline constexpr double NPC_POINT_EMISSIVE = 4.0;                  // Point emissive scale (exposure-relative)
    inline constexpr FLinearColor NPC_FACTION_COLORS[] = {
        FLinearColor(0.25f, 0.6f, 1.0f),                               // Faction 0: blue
        FLinearColor(1.0f, 0.4f, 0.2f),                                // Faction 1: orange
    };

    // NPC statistics (SDD 11 sections 3.7-3.8): the NpcStats log line and the stats overlay (Part 10e)
    inline constexpr double NPC_STATS_LOG_PERIOD_S = 2.0;              // One NpcStats line per period while NPCs exist
    inline constexpr double NPC_STATS_REFRESH_PERIOD_S = 0.25;         // Stats overlay text refresh (4 Hz)
    inline constexpr int32 NPC_STATS_FRAME_WINDOW = 240;               // Overlay frame-time window (frames, for p95)
    inline constexpr int32 NPC_STATS_OVERLAY_Z_ORDER = 5;              // Above the HUD, below the menus

    // NPC Full-tier AI and NPC-vs-NPC combat (SDD 11 section 3.6, Part 10d; class numbers in SOLNpcAiMath.cpp)
    inline constexpr double NPC_AI_TARGET_GRID_CELL_M = 2000.0;        // Target-search grid cell
    inline constexpr double NPC_AI_SHIP_GRID_CELL_M = 250.0;           // Separation and bolt-sweep grid cell
    inline constexpr double NPC_AI_NEAR_TARGET_SEARCH_M = 1000.0;      // First, cheap target-search ring
    inline constexpr uint32 NPC_AI_IDLE_SEARCH_INTERVAL_FRAMES = 8;    // A ship with no target searches every 8th
                                                                       // frame (staggered by index; Amendment 6)
    inline constexpr double NPC_AI_KEEP_RANGE_SCALE = 1.2;             // A target is kept out to 1.2x the engage range
    inline constexpr double NPC_AI_IDLE_RETURN_RATE = 0.05;            // Without a target: back to the rail at this
                                                                       // fraction of the deviation per second
    inline constexpr int32 NPC_AI_MAX_SHOTS_PER_FRAME = 2;             // Per ship and frame (SOLFireCadence cap)
    inline constexpr int32 NPC_MAX_BOLTS = 65536;                      // NPC bolt pool (real and cosmetic); shots
                                                                       // beyond it are dropped and counted
    inline constexpr int32 NPC_BOLT_RENDER_CAP = 8192;                 // NPC bolts drawn at once (nearest first)
    inline constexpr double NPC_COMBAT_VISUAL_RADIUS_M = 20000.0;      // Hybrid real-bolt radius; tracers and effects
                                                                       // are only made inside it (the player's range)
    inline constexpr int32 NPC_COMBAT_DEFAULT_MODEL = 2;               // SOLNpcAi::ECombatModel: Hybrid (SDD 11
                                                                       // open question 11, Amendment 4)
    inline constexpr int32 NPC_FX_EXPLOSION_POOL_SIZE = 16;            // Pooled NPC explosions near the camera
    inline constexpr int32 NPC_FX_IMPACT_POOL_SIZE = 24;               // Pooled NPC bolt impacts near the camera
}
