/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Scalability.h"

class UGameUserSettings;

/**
 * A copy of the engine's GameUserSettings as a verification run found them (SDD 46 section 14, part 46i), so the run
 * can give them back at shutdown: the text of every config property of the settings object, which is what
 * UGameUserSettings::SaveSettings writes to GameUserSettings.ini (the resolution and the last-confirmed one, the
 * window mode and the last-confirmed and preferred ones, the display, vsync, dynamic resolution, the frame-rate limit,
 * the desired and recommended screen sizes, audio quality, HDR, the benchmark results, the window positions and the
 * version), and the engine's scalability levels, which SaveSettings writes to the [ScalabilityGroups] section. A smoke
 * resizes the window (-ResX / -ResY, r.SetRes), which the engine stores as the confirmed resolution
 * (UGameEngine::OnViewportResized), and the engine writes the settings at exit (UGameEngine::PreExit), so without this
 * a smoke run leaves its own window size in the player's file. USOLSettingsSubsystem captures at boot (a packaged or
 * standalone game only, not the editor) and restores after the engine's exit write. Restoring the config properties
 * writes values only: nothing is applied to the window. The scalability levels are set back through
 * Scalability::SetQualityLevels, which applies them (their console variables). SaveSettings reloads WindowPositions
 * from the config cache before it writes (UGameUserSettings::ReloadWindowPositions), so that one property is written
 * as the config holds it, not from this copy.
 */
struct SOLTEST_API FSOLEngineSettingsSnapshot
{
    // One copied config property element: the property, its static array index and its text
    struct FValue
    {
        FName Property;
        int32 Index = 0;
        FString Text;
    };

    // Copies every config property of a settings object and the engine's scalability levels
    static FSOLEngineSettingsSnapshot Capture(const UGameUserSettings& settings);

    // Writes the copied config properties back into a settings object of the captured class (nothing is applied);
    // returns how many differed and were written back (0 for another class or an empty snapshot)
    int32 RestoreTo(UGameUserSettings& settings) const;

    // Sets and applies the engine's scalability levels back to the copied ones if they differ (SaveSettings writes the
    // engine's levels, not the settings object's); returns whether they differed
    bool RestoreQualityLevels() const;

    // Returns whether anything was captured
    bool IsCaptured() const { return SettingsClass.IsValid(); }

    // Returns a copied property's text (element 0), or null (verification)
    const FString* FindValue(FName property) const;

    TWeakObjectPtr<const UClass> SettingsClass;     // The settings object's class (restored only into the same class)
    TArray<FValue> Values;                          // Every config property element, in field order
    Scalability::FQualityLevels QualityLevels;      // The engine's scalability levels
};
