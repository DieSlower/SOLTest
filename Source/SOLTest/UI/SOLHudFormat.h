/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLChase.h"
#include "Flight/SOLFlightMode.h"

#include "CoreMinimal.h"

enum class ESOLHudPaletteRole : uint8;

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

    // Returns the unit FormatDistanceM picks (0 = m and 1 = km with "%.1f", 2 = AU with "%.4f") and the number in
    // that unit, so a caller can reproduce the text's digits without building it
    SOLTEST_API int32 DistanceUnitM(double distanceM, double& outValue);

    // Appends a duration rounded to whole seconds: "45 s", "12:05", "3:02:07" or "13 d 12 h" ("--" if invalid)
    SOLTEST_API void AppendDurationS(FString& outText, double seconds);

    // Appends a speed with its auto-picked unit: "12.3 m/s", "4.50 km/s" or "0.0123 c"; a negative speed keeps its
    // minus sign ("-3.20 km/s")
    SOLTEST_API void AppendSpeedMps(FString& outText, double speedMps);

    // Appends the chase marker's first line (SDD 32 section 3.9): "CHASE > Moon  384.4 km  closing 1.20 km/s  T 3:20";
    // T reads "HOLD" on the tail (bHolding) and "--" when the time is not finite or not known
    SOLTEST_API void AppendChaseMarker(FString& outText, FStringView targetName, double distanceM,
        double closingSpeedMps, double timeToInterceptS, bool bHolding);

    // Returns a chase activity's HUD and log name: "BURST", "COOL", "BRAKE", "BRAKE COOL" or "HOLD"
    SOLTEST_API const TCHAR* ChaseActivityName(ESOLChaseActivity activity);

    // Returns whether a cool off's resume meter is shown (text and G meter tick): finite and at most a full meter
    SOLTEST_API bool IsChaseResumeShown(double resumeMeter);

    // Appends the chase marker's activity line (SDD 32 section 3.9): "BURST 6.8 G", "COOL 1.5 G (resume at 45%)",
    // "BRAKE 6.8 G", "BRAKE COOL 1.5 G" or "HOLD" (no G); resumeMeter is the fraction of a full meter (a cool off ends
    // there). A resume meter above a full meter or not finite (the cool off lasts until the brake) shows no "resume at"
    SOLTEST_API void AppendChaseActivity(FString& outText, ESOLChaseActivity activity, double limitG,
        double resumeMeter);

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
    // ASSISTED Primary (blue-cyan), AUTO Good (green); the HUD palette's colour of FlightModeRole
    SOLTEST_API FLinearColor FlightModeColor(ESOLFlightMode mode);

    // (33d-ii) Returns the HUD palette role of a flight mode's label (MANUAL Secondary, ASSISTED Primary, AUTO Good):
    // the one source of the mode colours, for the HUD's label and the Help glossary's Flight mode sample
    SOLTEST_API ESOLHudPaletteRole FlightModeRole(ESOLFlightMode mode);
}
