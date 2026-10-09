/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLInfoText.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the display text for a value that cannot be shown
    FString NotAvailable()
    {
        return TEXT("n/a");
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the value is a whole number (to the precision a one-decimal print can show)
    bool IsWhole(double value)
    {
        return FMath::Abs(value - FMath::RoundToDouble(value)) < 0.05;
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
    if (magnitude < 1000.0)
    {
        return FString::Printf(TEXT("%.1f m/s"), metersPerSecond);
    }
    if (magnitude < 1.0e6)
    {
        return FString::Printf(TEXT("%.2f km/s"), metersPerSecond / 1000.0);
    }
    return FString::Printf(TEXT("%.0f km/s"), metersPerSecond / 1000.0);
}

//////////////////////////////////////////////////////////////////////////
// Formats a per-second rate
FString SOLInfoText::FormatRate(double perSecond)
{
    return FMath::IsFinite(perSecond) ? FormatPercentOrValue(perSecond) + TEXT(" /s") : NotAvailable();
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
    TArray<FInfoLine> lines;
    lines.Reserve(6);
    lines.Add({TEXT("Bolt speed"), FormatSpeed(boltSpeedMps)});
    lines.Add({TEXT("Damage per hit"), FormatPercentOrValue(damage)});
    lines.Add({TEXT("Fire rate"), FormatRate(shotsPerSecond)});
    lines.Add({TEXT("Target shield"), FormatPercentOrValue(targetShield)});
    lines.Add({TEXT("Target health"), FormatPercentOrValue(targetHealth)});
    lines.Add({TEXT("Shield regen"), FormatRate(shieldRegenPerSecond) + TEXT(" after ")
        + FormatPercentOrValue(shieldRegenDelaySeconds) + TEXT(" s")});
    return lines;
}
