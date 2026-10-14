/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic helpers of the Settings page and USOLSettingsSubsystem (SDD 8, 8d): display names and formatting of the
// values, the resolution list, stepping through choices, slider snapping, the mouse-look scaling and the save-schema
// version decision. Nothing here touches the engine's settings objects.
namespace SOLSettingsRules
{
    // What to do with a loaded settings save, by its schema version
    enum class ESaveVersionAction : uint8
    {
        Use,            // Same version: use it as is
        Migrate,        // Older known version: upgrade it, then use it (and write it back once)
        UseDefaults,    // Missing, invalid or newer version: ignore it and leave the file alone until the player
                        // changes something
    };

    // The boot decision for vsync and the quality preset (DecideBootGraphics): the values the settings model starts
    // with and which of them are pushed to the engine
    struct FBootGraphics
    {
        bool bVsync = true;         // Vsync the model starts with
        int32 QualityPreset = 0;    // Quality preset the model starts with (always a valid preset)
        bool bPushVsync = false;    // Set the engine's vsync to bVsync now
        bool bPushQuality = false;  // Set the engine's scalability level to QualityPreset now
    };

    // Window modes in UI order; the values equal EWindowMode::Type (Fullscreen, WindowedFullscreen, Windowed)
    inline constexpr int32 WINDOW_MODE_COUNT = 3;

    // Scalability presets in UI order (0 Low .. 4 Cinematic, as Scalability::SetQualityLevels takes them)
    inline constexpr int32 QUALITY_PRESET_COUNT = 5;

    // Returns the display name of a quality preset (clamped into range)
    SOLTEST_API FString QualityName(int32 preset);

    // Returns the display name of a window mode (clamped into range)
    SOLTEST_API FString WindowModeName(int32 mode);

    // Returns a resolution as "1920 x 1080"
    SOLTEST_API FString FormatResolution(const FIntPoint& resolution);

    // Returns a 0..1 fraction as a whole percentage ("75%"); NaN reads as 0%
    SOLTEST_API FString FormatPercent(float fraction);

    // Returns a multiplier with up to two decimals ("1.25x", "1x")
    SOLTEST_API FString FormatMultiplier(float value);

    // Returns the resolutions to offer: the positive ones, without duplicates, ascending by width then height, with the
    // current one included even when the display did not list it
    SOLTEST_API TArray<FIntPoint> BuildResolutionChoices(const TArray<FIntPoint>& supported, const FIntPoint& current);

    // Returns the index of a resolution in the list, or INDEX_NONE
    SOLTEST_API int32 FindResolution(const TArray<FIntPoint>& choices, const FIntPoint& resolution);

    // Returns the index one step (direction < 0 back, > 0 forward) from an index, clamped to [0, count) (no wrap);
    // INDEX_NONE for an empty list, and an out-of-range index is clamped first
    SOLTEST_API int32 StepIndex(int32 index, int32 count, int32 direction);

    // Returns the value clamped to [lo, hi] and snapped to the nearest multiple of step above lo (NaN reads as lo)
    SOLTEST_API float SnapToStep(float value, float step, float lo, float hi);

    // Returns a mouse-look delta in pixels (X right, Y up) scaled by the sensitivity, with Y flipped when inverted
    SOLTEST_API FVector2d ApplyLookSettings(const FVector2d& deltaPixels, float sensitivity, bool bInvertY);

    // Returns what to do with a save of savedVersion when the current schema is currentVersion
    SOLTEST_API ESaveVersionAction ClassifySaveVersion(int32 savedVersion, int32 currentVersion);

    // Returns whether the settings slot needs a write: an Apply, a migration or a failed write owes one, or the O key
    // left the orbit-line flag different from the value last loaded or written (SDD 17)
    SOLTEST_API bool IsSaveOwed(bool bWriteOwed, bool bAppliedShowOrbitLines, bool bWrittenShowOrbitLines);

    // Decides at boot what vsync and the quality preset start as and what is pushed to the engine. Only a value the
    // player chose on the Settings page (bSaveUsed and its "chosen" flag, and for the quality a valid preset) is the
    // model's value and is pushed, and only when it differs from the engine's; anything else starts from the engine
    // (so an engine "custom" scalability mix, engineQuality -1, or an ini edit is never overwritten at boot). When
    // the engine's level is custom and the player never chose a preset, the model shows fallbackQuality (clamped)
    SOLTEST_API FBootGraphics DecideBootGraphics(bool bSaveUsed, bool bVsyncChosen, bool bQualityChosen,
        bool savedVsync, int32 savedQuality, bool engineVsync, int32 engineQuality, int32 fallbackQuality);
}
