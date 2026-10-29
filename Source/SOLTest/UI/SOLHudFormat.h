/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlightMode.h"

#include "CoreMinimal.h"

// Display unit for a speed readout or the F3 speed-cap stepper
enum class ESOLSpeedUnit : uint8
{
    MetersPerSecond,
    KilometersPerSecond,
    LightSpeed
};

namespace SOLHudFormat
{
    // Picks the most readable unit for a magnitude (m/s): < 1000 -> m/s; < 0.01c -> km/s; else -> c. Zero -> m/s
    SOLTEST_API ESOLSpeedUnit PickSpeedUnit(double speedMps);

    // Converts to the unit's display value (m/s: unchanged; km/s: /1000; c: /299792458.0)
    SOLTEST_API double ToUnitValue(double speedMps, ESOLSpeedUnit unit);

    // Inverse of ToUnitValue
    SOLTEST_API double FromUnitValue(double value, ESOLSpeedUnit unit);

    // Formats meters as "#.# m" under 1000 m, "#.# km" under 0.01 AU, else "#.#### AU" (e.g. "23402.9 km")
    SOLTEST_API FString FormatDistanceM(double distanceM);

    // Appends FormatDistanceM(distanceM)'s exact text to an existing buffer, without a temporary FString
    SOLTEST_API void AppendDistanceM(FString& outText, double distanceM);

    // Appends a duration rounded to whole seconds: "45 s", "12:05", "3:02:07" or "13 d 12 h" ("--" if invalid)
    SOLTEST_API void AppendDurationS(FString& outText, double seconds);

    // Returns the flight HUD's key hint line from the ship's keys (SOLKeyBindings order, shown through
    // SOLBindingLabel), with the orbit-lines state; with the default keys it reads "ORBITS OFF (O)   F3 speed panel
    // ...   LMB fire   G drop target"
    SOLTEST_API FString FormatFlightKeyHints(const TArray<FString>& keys, bool bOrbitLinesOn);

    // Returns the jump map's first hint line, whose close key is the (rebindable) map toggle
    SOLTEST_API FString FormatMapKeyHints(const TArray<FString>& keys);

    // The flight HUD and speed panel texts that name a rebindable key (built when the keys change, not per frame)
    struct FHudKeyTexts
    {
        FString SurfaceLockHint;    // "L to surface-lock  " (before the body name)
        FString FrameLockTag;       // " [M LOCK]" (after the frame name)
        FString NoTarget;           // "none (T)" (target panel without a target)
        FString SpeedPanelTitle;    // "SPEED CAP  (F3 / Enter close, Esc close)"
        FString ControlsChangedNotice;  // "Default controls changed: Space or Middle Mouse steers, ..." (SDD 19)
        FString RouteResumeHint;    // "   V resume" (after the paused route marker, SDD 20)
        FString SteerHint;          // "Hold Space to steer" (until the first steering of the session, issue #31)
    };

    // Returns the HUD and speed panel texts that name a key, from the ship's keys (SOLKeyBindings order); the comments
    // above show them with the default keys
    SOLTEST_API FHudKeyTexts FormatHudKeyTexts(const TArray<FString>& keys);

    // Returns the colour of the joystick circles, stick line and dot: SOL::HudPalette::FREE_LOOK (yellow) while free
    // look is held, else SOL::HudPalette::PRIMARY (issue #31)
    SOLTEST_API FLinearColor PilotCircleColor(bool bIsFreeLooking);

    // Returns the HUD's flight mode label: "MANUAL", "ASSISTED" or "AUTO" (SDD 19 section 3.7)
    SOLTEST_API const TCHAR* FlightModeLabel(ESOLFlightMode mode);

    // Returns the flight mode label's color from the original Limit Theory palette: MANUAL Secondary (orange),
    // ASSISTED Primary (blue-cyan), AUTO Good (green)
    SOLTEST_API FLinearColor FlightModeColor(ESOLFlightMode mode);
}
