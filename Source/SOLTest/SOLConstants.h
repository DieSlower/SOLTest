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

    // Registry names of bodies that code refers to directly
    namespace BodyNames
    {
        inline constexpr const TCHAR* SUN = TEXT("Sun");
        inline constexpr const TCHAR* EARTH = TEXT("Earth");
    }

    // Asset and content paths
    namespace Paths
    {
        inline constexpr const TCHAR* BODY_MESH = TEXT("/Engine/BasicShapes/Sphere.Sphere");
        inline constexpr const TCHAR* BODY_MATERIAL = TEXT("/Game/SOL/Materials/M_SOLBody.M_SOLBody");
        inline constexpr const TCHAR* TEST_MAP = TEXT("/Game/Maps/SOL_Test");
    }

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
