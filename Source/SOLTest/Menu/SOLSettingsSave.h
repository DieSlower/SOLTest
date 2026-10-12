/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSettingsModel.h"

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "SOLSettingsSave.generated.h"

/**
 * The player's settings on disk (SDD 8, 8d): one USaveGame in the SOL::Settings::SAVE_SLOT slot, written by
 * USOLSettingsSubsystem at most once per settings visit and only when something was applied. Version is the schema
 * version (SOL::Settings::SAVE_VERSION when written); SOLSettingsRules::ClassifySaveVersion decides on load whether
 * a file is used, migrated or ignored. Resolution and window mode are not here: the engine's UGameUserSettings owns
 * them (it applies them when the window is created).
 */
UCLASS()
class SOLTEST_API USOLSettingsSave : public USaveGame
{
    GENERATED_BODY()

public:

    // Starts with the default settings values
    USOLSettingsSave();

    // Copies model values into the saved fields
    void SetValues(const SOLSettingsModel::FSettingsValues& values);

    // Returns the saved fields as model values (unsanitized; the model sanitizes them)
    SOLSettingsModel::FSettingsValues GetValues() const;

    /** Schema version this save was written with. */
    UPROPERTY()
    int32 Version = 0;

    UPROPERTY()
    float MasterVolume = 1.0f;

    UPROPERTY()
    float SfxVolume = 1.0f;

    UPROPERTY()
    float MusicVolume = 1.0f;

    UPROPERTY()
    float MouseSensitivity = 1.0f;

    UPROPERTY()
    bool bInvertY = false;

    UPROPERTY()
    float HudScale = 1.0f;

    UPROPERTY()
    bool bHudVisible = true;

    UPROPERTY()
    bool bDefaultFlightAssist = true;

    UPROPERTY()
    bool bVsync = true;

    UPROPERTY()
    int32 QualityPreset = 3;

    /** The player chose bVsync on the Settings page (else the engine's own vsync is kept at boot). Added within schema
     *  1: a save written before it reads false, which keeps the engine's value. */
    UPROPERTY()
    bool bVsyncChosen = false;

    /** The player chose QualityPreset on the Settings page (else the engine's level, possibly a custom mix, is kept at
     *  boot). Added within schema 1 like bVsyncChosen. */
    UPROPERTY()
    bool bQualityChosen = false;

    /** Rebound keys only: SOLKeyBindings binding id -> engine key name (the other bindings use their defaults). */
    UPROPERTY()
    TMap<FString, FString> KeyOverrides;
};
