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

    // Ship pawn, virtual joystick: the stick's travel radius as a fraction of the smaller viewport dimension...
    inline constexpr double JOYSTICK_RADIUS_FRACTION = 0.35;

    // ...its dead zone and response exponent (fractions of the radius, SOLFlight::JoystickToRotation)
    inline constexpr double JOYSTICK_DEAD_ZONE = 0.05;
    inline constexpr double JOYSTICK_EXPONENT = 1.5;

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

    // Orbit lines: translucency sort priority of the line components, drawn after the bodies, ring discs and gas-giant
    // haze (which use small priorities); the lines draw over translucent ring discs by design (SDD 17 open question 7)
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

    // Surface lock (SDD 4): time constant of the exponential alignment rotation that aligns the ship's up with the local vertical
    inline constexpr double SURFACE_LOCK_ALIGN_TIME_CONSTANT_S = 1.0;

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

    // Menu widgets (SDD 8, 8c): viewport layer, layout sizes (UMG units at the 1080p reference DPI), fonts and colours
    namespace MenuStyle
    {
        inline constexpr int32 Z_ORDER = 20;                     // Above the HUD canvas and the F3 panel
        inline constexpr const TCHAR* FONT_REGULAR = TEXT("Regular");
        inline constexpr const TCHAR* FONT_BOLD = TEXT("Bold");
        inline constexpr int32 TITLE_FONT_SIZE = 56;
        inline constexpr int32 SUBTITLE_FONT_SIZE = 18;
        inline constexpr int32 HEADING_FONT_SIZE = 24;
        inline constexpr int32 SECTION_FONT_SIZE = 18;
        inline constexpr int32 BUTTON_FONT_SIZE = 20;
        inline constexpr int32 TAB_FONT_SIZE = 18;
        inline constexpr int32 BODY_FONT_SIZE = 16;
        inline constexpr int32 SMALL_FONT_SIZE = 14;
        inline constexpr float SCREEN_MARGIN = 80.0f;            // Main menu column inset from the screen's left edge
        inline constexpr float COLUMN_WIDTH = 340.0f;            // Main menu button column
        inline constexpr float BUTTON_HEIGHT = 52.0f;
        inline constexpr float BUTTON_GAP = 10.0f;
        inline constexpr float BUTTON_CORNER_RADIUS = 6.0f;
        inline constexpr float PANEL_PADDING = 28.0f;
        inline constexpr float PANEL_GAP = 24.0f;                // Between the main menu column and its page panel
        inline constexpr float PAGE_WIDTH = 760.0f;              // Main menu page panel
        inline constexpr float PAGE_HEIGHT = 760.0f;
        inline constexpr float PAUSE_WIDTH = 1080.0f;            // Pause menu panel
        inline constexpr float PAUSE_HEIGHT = 780.0f;
        inline constexpr float TAB_HEIGHT = 46.0f;
        inline constexpr float TAB_GAP = 8.0f;
        inline constexpr float ROW_GAP = 4.0f;
        inline constexpr float KEY_COLUMN_WIDTH = 230.0f;        // Controls page key column
        inline constexpr float LABEL_COLUMN_WIDTH = 400.0f;      // Info page label column
        inline constexpr float LIST_BUTTON_HEIGHT = 38.0f;       // Start-location rows
        inline constexpr float BUTTON_PADDING_X = 18.0f;         // Inside a menu button, around its label
        inline constexpr float BUTTON_PADDING_Y = 8.0f;
        inline const FLinearColor SCREEN_DIM(0.0f, 0.0f, 0.0f, 0.45f);        // Behind the pause menu
        inline const FLinearColor PANEL_BACKGROUND(0.01f, 0.02f, 0.04f, 0.82f);
        inline const FLinearColor BUTTON_NORMAL(0.06f, 0.09f, 0.14f, 0.85f);
        inline const FLinearColor BUTTON_PRESSED_TINT(1.4f, 1.4f, 1.4f, 1.0f);   // Brightens the tint while pressed
        inline const FLinearColor BUTTON_FOCUSED(0.16f, 0.3f, 0.5f, 1.0f);    // Keyboard focus (and mouse hover)
        inline const FLinearColor BUTTON_SELECTED(0.08f, 0.22f, 0.36f, 0.95f); // Active tab, page or start body
        inline const FLinearColor TEXT_COLOR(0.88f, 0.93f, 1.0f, 1.0f);
        inline const FLinearColor DIM_TEXT_COLOR(0.55f, 0.62f, 0.72f, 1.0f);
        inline const FLinearColor ACCENT_COLOR(0.45f, 0.78f, 1.0f, 1.0f);
        inline const FLinearColor WARNING_COLOR(1.0f, 0.62f, 0.25f, 1.0f);    // Settings: key conflicts, dirty state

        // Settings page (SDD 8, 8d): row layout
        inline constexpr float SETTINGS_LABEL_WIDTH = 250.0f;     // Left column of a settings row
        inline constexpr float SETTINGS_VALUE_WIDTH = 80.0f;      // Value readout right of a slider
        inline constexpr float SETTINGS_ROW_HEIGHT = 34.0f;       // Steppers, sliders, checkboxes and rebind rows
        inline constexpr float SETTINGS_STEP_BUTTON_WIDTH = 44.0f; // The '<' and '>' buttons of a stepper
        inline constexpr float SETTINGS_REBIND_WIDTH = 120.0f;    // The Rebind button of a key row
        inline constexpr float SETTINGS_ACTION_WIDTH = 150.0f;    // Apply / Revert buttons
        inline constexpr float SETTINGS_RESET_KEYS_WIDTH = 280.0f; // Reset keys button
    }

    // Settings (SDD 8, 8d): the USOLSettingsSave slot, its schema version and the slider steps
    namespace Settings
    {
        inline constexpr const TCHAR* SAVE_SLOT = TEXT("SOLSettings");                  // Player settings
        inline constexpr const TCHAR* SMOKE_SAVE_SLOT = TEXT("SOLSettings_MenuSmoke");  // -SOLMenuSmoke runs only
                                                                                          // (deleted at their start)
        inline constexpr int32 SAVE_USER_INDEX = 0;
        // Save schema; bump it and add a step to USOLSettingsSave::MigrateToCurrentVersion on schema changes (2: orbit
        // lines, SDD 17)
        inline constexpr int32 SAVE_VERSION = 2;
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
        inline constexpr float VALUE_EQUAL_TOLERANCE = 1.0e-4f;    // Two slider values closer than this are equal
    }

    // Registry names of bodies that code refers to directly
    namespace BodyNames
    {
        inline constexpr const TCHAR* SUN = TEXT("Sun");
        inline constexpr const TCHAR* EARTH = TEXT("Earth");
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
        inline constexpr const TCHAR* SMOKE_JUMP = TEXT("SOLSmokeJump");        // Scripted pick + jump, then quit
        inline constexpr const TCHAR* SMOKE_LEVEL = TEXT("SOLSmokeLevel");      // Scripted surface-lock (L), then quit
        inline constexpr const TCHAR* COMBAT_DEMO = TEXT("SOLCombatDemo");      // Drops a target and shoots it, repeatedly
        inline constexpr const TCHAR* COMBAT_STRESS = TEXT("SOLCombatStress="); // With -SOLCombatDemo: keeps this many
                                                                                  // extra bolts alive (load test)
        inline constexpr const TCHAR* COMBAT_DEMO_HOLD = TEXT("SOLCombatDemoHold="); // With -SOLCombatDemo: the first
                                                                                  // target is left unshot this many s,
                                                                                  // with frame screenshots (issue #15)
        inline constexpr const TCHAR* SPECTATOR = TEXT("SOLSpectator");         // Debug free-fly pawn, not the ship
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

        // Combat visuals (SDD 7, 7c), authored live through the editor's Niagara and material scripting APIs: the
        // (CPU-sim) bolt renderer, the four pooled one-shot effects, and the target mesh and its custom-data material. Each is
        // optional: a missing asset disables only its own effect, with a warning
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
}
