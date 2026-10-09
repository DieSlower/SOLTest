/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic settings model (SDD 8): clamping, dirty tracking and the "write only when something changed" decision that
// the project persistence standard requires.
namespace SOLSettingsModel
{
    struct FSettingsValues
    {
        float MasterVolume = 1.0f;
        float SfxVolume = 1.0f;
        float MusicVolume = 0.7f;
        float MouseSensitivity = 1.0f;
        bool bInvertY = false;
        float HudScale = 1.0f;
        bool bHudVisible = true;
        bool bDefaultFlightAssist = true;
        bool bVsync = true;
        int32 QualityPreset = 3;
    };

    struct FSettingsModel
    {
        FSettingsValues Saved;
        FSettingsValues Pending;
    };

    // Returns the values clamped into range, with any NaN replaced by that field's default
    SOLTEST_API FSettingsValues Sanitized(const FSettingsValues& values);

    // Returns whether two value sets are equal after sanitizing (floats within 1e-4)
    SOLTEST_API bool AreEqual(const FSettingsValues& a, const FSettingsValues& b);

    // Returns a model whose saved and pending values are both the sanitized loaded values
    SOLTEST_API FSettingsModel MakeModel(const FSettingsValues& loaded);

    // Stores the sanitized values as the pending set
    SOLTEST_API void SetPending(FSettingsModel& model, const FSettingsValues& values);

    // Returns whether the pending values differ from the saved ones
    SOLTEST_API bool IsDirty(const FSettingsModel& model);

    // Discards the pending changes and returns the saved values
    SOLTEST_API FSettingsValues Revert(FSettingsModel& model);

    // Commits the pending values and returns true if a write is needed (false, and no change, when nothing differs)
    SOLTEST_API bool CommitIfDirty(FSettingsModel& model);
}
