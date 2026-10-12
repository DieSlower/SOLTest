/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLInfoText.h"

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"

namespace
{
    constexpr double INFOTEXT_WHOLE_TOLERANCE = 0.05;      // Closer than this to an integer prints without decimals
    constexpr double INFOTEXT_KM_SPEED_MIN_MPS = 1000.0;   // Speeds at or above this read in km/s
    constexpr double INFOTEXT_KM_SPEED_WHOLE_MPS = 1.0e6;  // ...and at or above this in whole km/s
    constexpr double INFOTEXT_METERS_PER_KM = 1000.0;
    constexpr int32 INFOTEXT_COMBAT_LINE_COUNT = 6;

    //////////////////////////////////////////////////////////////////////////
    // Returns the display text for a value that cannot be shown
    FString NotAvailable()
    {
        return SOLMenuText::INFO_NOT_AVAILABLE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the value is a whole number (to the precision a one-decimal print can show)
    bool IsWhole(double value)
    {
        return FMath::Abs(value - FMath::RoundToDouble(value)) < INFOTEXT_WHOLE_TOLERANCE;
    }
}

//////////////////////////////////////////////////////////////////////////
// Formats a speed in m/s or km/s
FString SOLInfoText::FormatSpeed(double metersPerSecond)
{
    if (!FMath::IsFinite(metersPerSecond))
    {
        return NotAvailable();
    }
    const double magnitude = FMath::Abs(metersPerSecond);
    if (magnitude < INFOTEXT_KM_SPEED_MIN_MPS)
    {
        return FString::Printf(TEXT("%.1f m/s"), metersPerSecond);
    }
    if (magnitude < INFOTEXT_KM_SPEED_WHOLE_MPS)
    {
        return FString::Printf(TEXT("%.2f km/s"), metersPerSecond / INFOTEXT_METERS_PER_KM);
    }
    return FString::Printf(TEXT("%.0f km/s"), metersPerSecond / INFOTEXT_METERS_PER_KM);
}

//////////////////////////////////////////////////////////////////////////
// Formats a per-second rate
FString SOLInfoText::FormatRate(double perSecond)
{
    return FMath::IsFinite(perSecond) ? FString::Format(SOLMenuText::INFO_RATE_FORMAT,
        { FormatPercentOrValue(perSecond) }) : NotAvailable();
}

//////////////////////////////////////////////////////////////////////////
// Formats a value with no decimals when whole, else one
FString SOLInfoText::FormatPercentOrValue(double value)
{
    if (!FMath::IsFinite(value))
    {
        return NotAvailable();
    }
    return IsWhole(value) ? FString::Printf(TEXT("%.0f"), value) : FString::Printf(TEXT("%.1f"), value);
}

//////////////////////////////////////////////////////////////////////////
// Builds the combat lines
TArray<SOLInfoText::FInfoLine> SOLInfoText::BuildCombatLines(double boltSpeedMps, double damage, double shotsPerSecond,
    double targetShield, double targetHealth, double shieldRegenPerSecond, double shieldRegenDelaySeconds)
{
    using namespace SOLMenuText;
    TArray<FInfoLine> lines;
    lines.Reserve(INFOTEXT_COMBAT_LINE_COUNT);
    lines.Add({INFO_BOLT_SPEED, FormatSpeed(boltSpeedMps)});
    lines.Add({INFO_DAMAGE, FormatPercentOrValue(damage)});
    lines.Add({INFO_FIRE_RATE, FormatRate(shotsPerSecond)});
    lines.Add({INFO_TARGET_SHIELD, FormatPercentOrValue(targetShield)});
    lines.Add({INFO_TARGET_HEALTH, FormatPercentOrValue(targetHealth)});
    lines.Add({INFO_SHIELD_REGEN, FString::Format(INFO_REGEN_FORMAT, { FormatRate(shieldRegenPerSecond),
        FormatPercentOrValue(shieldRegenDelaySeconds) })});
    return lines;
}

//////////////////////////////////////////////////////////////////////////
// Returns the manual surface-lock label with the surface-lock key
FString SOLInfoText::FormatSurfaceLockManualLabel(const TArray<FString>& keys)
{
    return SOLKeyBindings::FormatKeyText(SOLMenuText::INFO_SURFACE_LOCK_MANUAL, keys);
}

//////////////////////////////////////////////////////////////////////////
// Returns the Info page's prose paragraphs with the keys they name
TArray<FString> SOLInfoText::BuildProse(const TArray<FString>& keys)
{
    TArray<FString> paragraphs;
    paragraphs.Reserve(UE_ARRAY_COUNT(SOLMenuText::INFO_PROSE));
    for (const TCHAR* paragraph : SOLMenuText::INFO_PROSE)
    {
        paragraphs.Add(SOLKeyBindings::FormatKeyText(paragraph, keys));
    }
    return paragraphs;
}
