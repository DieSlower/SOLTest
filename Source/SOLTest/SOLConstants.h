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

    // Rendering: the render origin snaps back to the observer once the observer drifts this far from it (10 km)
    inline constexpr double RENDER_REBASE_DISTANCE_M = 1.0e4;

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

    // Registry names of bodies that code refers to directly
    namespace BodyNames
    {
        inline constexpr const TCHAR* SUN = TEXT("Sun");
        inline constexpr const TCHAR* EARTH = TEXT("Earth");
    }

    // Command-line switches and values used by verification runs (FParse::Value keys end in '=')
    namespace CommandLine
    {
        inline constexpr const TCHAR* START_BODY = TEXT("SOLStart=");           // Body the ship spawns above
        inline constexpr const TCHAR* ALTITUDE_KM = TEXT("SOLAltitudeKm=");     // Spawn altitude above its surface
        inline constexpr const TCHAR* LOOK_AT_BODY = TEXT("SOLLookAt=");        // Body the debug camera faces
        inline constexpr const TCHAR* SMOKE_SHOT = TEXT("SOLSmokeShot=");       // Screenshot after N s, then quit
        inline constexpr const TCHAR* SMOKE_FLIGHT = TEXT("SOLSmokeFlight");    // Scripted ship flight, then quit
        inline constexpr const TCHAR* SMOKE_INPUT = TEXT("SOLSmokeInput");      // Scripted player input, then quit
        inline constexpr const TCHAR* SMOKE_HUD = TEXT("SOLSmokeHud");          // Scripted HUD/F3 panel input, then quit
        inline constexpr const TCHAR* SMOKE_MAP = TEXT("SOLSmokeMap");          // Scripted jump-map input, then quit
        inline constexpr const TCHAR* SMOKE_MAP_PICK = TEXT("SOLSmokeMapPick"); // Scripted map destination pick, quit
        inline constexpr const TCHAR* SPECTATOR = TEXT("SOLSpectator");         // Debug free-fly pawn, not the ship
    }

    // Asset and content paths
    namespace Paths
    {
        inline constexpr const TCHAR* BODY_MESH = TEXT("/Engine/BasicShapes/Sphere.Sphere");
        inline constexpr const TCHAR* BODY_MATERIAL = TEXT("/Game/SOL/Materials/M_SOLBody.M_SOLBody");
        inline constexpr const TCHAR* TEST_MAP = TEXT("/Game/Maps/SOL_Test");

        // Placeholder ship: engine primitives (100 cm across, centered) and the engine's lit basic material
        inline constexpr const TCHAR* SHIP_PART_CUBE = TEXT("/Engine/BasicShapes/Cube.Cube");
        inline constexpr const TCHAR* SHIP_PART_CONE = TEXT("/Engine/BasicShapes/Cone.Cone");
        inline constexpr const TCHAR* SHIP_PART_CYLINDER = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
        inline constexpr const TCHAR* SHIP_PART_SPHERE = TEXT("/Engine/BasicShapes/Sphere.Sphere");
        inline constexpr const TCHAR* SHIP_HULL_MATERIAL =
            TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
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
}
