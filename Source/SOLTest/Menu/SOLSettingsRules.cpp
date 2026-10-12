/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsRules.h"

#include "Menu/SOLMenuText.h"

namespace
{
    static_assert(UE_ARRAY_COUNT(SOLMenuText::QUALITY_NAMES) == SOLSettingsRules::QUALITY_PRESET_COUNT,
        "one name per quality preset");
    static_assert(UE_ARRAY_COUNT(SOLMenuText::WINDOW_MODE_NAMES) == SOLSettingsRules::WINDOW_MODE_COUNT,
        "one name per window mode");

    constexpr float SETTINGSRULES_PERCENT = 100.0f;

    //////////////////////////////////////////////////////////////////////////
    // Orders resolutions by width, then height
    bool SettingsRulesResolutionLess(const FIntPoint& a, const FIntPoint& b)
    {
        return a.X != b.X ? a.X < b.X : a.Y < b.Y;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the display name of a quality preset
FString SOLSettingsRules::QualityName(const int32 preset)
{
    return SOLMenuText::QUALITY_NAMES[FMath::Clamp(preset, 0, QUALITY_PRESET_COUNT - 1)];
}

//////////////////////////////////////////////////////////////////////////
// Returns the display name of a window mode
FString SOLSettingsRules::WindowModeName(const int32 mode)
{
    return SOLMenuText::WINDOW_MODE_NAMES[FMath::Clamp(mode, 0, WINDOW_MODE_COUNT - 1)];
}

//////////////////////////////////////////////////////////////////////////
// Returns a resolution as "W x H"
FString SOLSettingsRules::FormatResolution(const FIntPoint& resolution)
{
    return FString::Format(SOLMenuText::RESOLUTION_FORMAT, { resolution.X, resolution.Y });
}

//////////////////////////////////////////////////////////////////////////
// Returns a fraction as a whole percentage
FString SOLSettingsRules::FormatPercent(const float fraction)
{
    const float safe = FMath::IsNaN(fraction) ? 0.0f : fraction;
    return FString::Format(SOLMenuText::PERCENT_FORMAT, { FMath::RoundToInt(safe * SETTINGSRULES_PERCENT) });
}

//////////////////////////////////////////////////////////////////////////
// Returns a multiplier with up to two decimals
FString SOLSettingsRules::FormatMultiplier(const float value)
{
    const float safe = FMath::IsNaN(value) ? 0.0f : value;
    FString number = FString::Printf(TEXT("%.2f"), safe);

    // Trailing zeros and a bare point go ("1.50" -> "1.5", "1.00" -> "1")
    while (number.EndsWith(TEXT("0")) && number.Contains(TEXT(".")))
    {
        number.LeftChopInline(1);
    }
    if (number.EndsWith(TEXT(".")))
    {
        number.LeftChopInline(1);
    }
    return FString::Format(SOLMenuText::MULTIPLIER_FORMAT, { number });
}

//////////////////////////////////////////////////////////////////////////
// Returns the sorted, unique, positive resolutions plus the current one
TArray<FIntPoint> SOLSettingsRules::BuildResolutionChoices(const TArray<FIntPoint>& supported, const FIntPoint& current)
{
    TArray<FIntPoint> choices;
    choices.Reserve(supported.Num() + 1);
    for (const FIntPoint& resolution : supported)
    {
        if (resolution.X > 0 && resolution.Y > 0)
        {
            choices.AddUnique(resolution);
        }
    }
    if (current.X > 0 && current.Y > 0)
    {
        choices.AddUnique(current);
    }
    choices.Sort(&SettingsRulesResolutionLess);
    return choices;
}

//////////////////////////////////////////////////////////////////////////
// Returns the index of a resolution in the list, or INDEX_NONE
int32 SOLSettingsRules::FindResolution(const TArray<FIntPoint>& choices, const FIntPoint& resolution)
{
    return choices.IndexOfByKey(resolution);
}

//////////////////////////////////////////////////////////////////////////
// Returns the index one step away, clamped (no wrap)
int32 SOLSettingsRules::StepIndex(const int32 index, const int32 count, const int32 direction)
{
    if (count <= 0)
    {
        return INDEX_NONE;
    }
    const int32 start = FMath::Clamp(index, 0, count - 1);
    return FMath::Clamp(start + FMath::Sign(direction), 0, count - 1);
}

//////////////////////////////////////////////////////////////////////////
// Clamps and snaps a slider value to its step
float SOLSettingsRules::SnapToStep(const float value, const float step, const float lo, const float hi)
{
    const float clamped = FMath::IsNaN(value) ? lo : FMath::Clamp(value, lo, hi);
    if (step <= 0.0f)
    {
        return clamped;
    }
    return FMath::Clamp(lo + FMath::RoundToFloat((clamped - lo) / step) * step, lo, hi);
}

//////////////////////////////////////////////////////////////////////////
// Scales a mouse-look delta by the sensitivity and flips Y when inverted
FVector2d SOLSettingsRules::ApplyLookSettings(const FVector2d& deltaPixels, const float sensitivity,
    const bool bInvertY)
{
    const double scale = static_cast<double>(sensitivity);
    return FVector2d(deltaPixels.X * scale, deltaPixels.Y * scale * (bInvertY ? -1.0 : 1.0));
}

//////////////////////////////////////////////////////////////////////////
// Decides what to do with a loaded save by its schema version
SOLSettingsRules::ESaveVersionAction SOLSettingsRules::ClassifySaveVersion(const int32 savedVersion,
    const int32 currentVersion)
{
    if (savedVersion <= 0 || savedVersion > currentVersion)
    {
        return ESaveVersionAction::UseDefaults;
    }
    return savedVersion == currentVersion ? ESaveVersionAction::Use : ESaveVersionAction::Migrate;
}

//////////////////////////////////////////////////////////////////////////
// Decides the boot vsync and quality preset and which of them go to the engine
SOLSettingsRules::FBootGraphics SOLSettingsRules::DecideBootGraphics(const bool bSaveUsed, const bool bVsyncChosen,
    const bool bQualityChosen, const bool savedVsync, const int32 savedQuality, const bool engineVsync,
    const int32 engineQuality, const int32 fallbackQuality)
{
    // Returns whether a scalability level is one of the presets (not custom, -1)
    const auto isPreset = [](const int32 level)
    {
        return level >= 0 && level < QUALITY_PRESET_COUNT;
    };
    FBootGraphics result;

    // Vsync: the player's choice wins, else the engine's
    const bool bUseSavedVsync = bSaveUsed && bVsyncChosen;
    result.bVsync = bUseSavedVsync ? savedVsync : engineVsync;
    result.bPushVsync = bUseSavedVsync && savedVsync != engineVsync;

    // Quality: the player's (valid) preset wins, else the engine's level; a custom engine mix shows the fallback
    const bool bUseSavedQuality = bSaveUsed && bQualityChosen && isPreset(savedQuality);
    if (bUseSavedQuality)
    {
        result.QualityPreset = savedQuality;
    }
    else
    {
        result.QualityPreset = isPreset(engineQuality) ? engineQuality
            : FMath::Clamp(fallbackQuality, 0, QUALITY_PRESET_COUNT - 1);
    }
    result.bPushQuality = bUseSavedQuality && savedQuality != engineQuality;
    return result;
}
