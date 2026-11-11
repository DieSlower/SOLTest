/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSettingsModel.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "SOLSettingsSave.generated.h"

/** One overlay profile on disk (SDD 22, SDD 34 section 5): every Overlay Menu row id mapped to whether it is checked
 *  (SOLOverlay::ToSavedProfile / FromSavedProfile; rows missing from an older file take their default), with the
 *  profile's enabled flag, master O state and Alt+O orbit-lines switch. From schema 10 a profile that was never used
 *  is written with an empty row map (that is how SOLOverlay::FOverlayPrefs::UsedProfiles is stored). */
USTRUCT()
struct FSOLOverlayProfileSave
{
    GENERATED_BODY()

    UPROPERTY()
    TMap<FString, bool> Overlays;

    /** The profile is enabled (schema 10; entry 0, Profile 1, always reads enabled). */
    UPROPERTY()
    bool bEnabled = false;

    /** The profile's master O state (schema 10, D10). */
    UPROPERTY()
    bool bMasterOn = true;

    /** The profile's Alt+O orbit-lines switch (schema 10, D18). */
    UPROPERTY()
    bool bOrbitLinesOn = true;
};

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

    // Upgrades an older schema step by step to SOL::Settings::SAVE_VERSION (current, unversioned or newer: unchanged)
    void MigrateToCurrentVersion();

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

    /** Legacy global orbit-lines switch (SDD 17). Added in schema 2 (a schema 1 save is migrated to true). From schema
     *  10 (SDD 34 D18) each overlay profile holds its own switch (FSOLOverlayProfileSave::bOrbitLinesOn): this field
     *  is no longer written or read by SetValues / GetValues, only by the 9 -> 10 step, which copies it into every
     *  stored profile. Kept so an old file's value can always be migrated. */
    UPROPERTY()
    bool bShowOrbitLines = true;

    /** HUD hologram effects strength (SDD 21). Added in schema 5; an older save is migrated to the default. */
    UPROPERTY()
    float HudEffectsStrength = SOL::Settings::HUD_EFFECTS_DEFAULT;

    /** Hologram menu effects strength (SDD 42 section 8). Added in schema 11; an older save is migrated per A2 (the
     *  10 -> 11 step copies its HUD effects strength, sanitized). */
    UPROPERTY()
    float HoloMenuEffects = SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;

    /** HUD glow, 1 = 100% (SDD 37 D2; x SOL::Hologram::HUD_GLOW_DEFAULT_LEVEL of the issue #31 glow). Added in schema
     *  12; an older save is migrated to 100% (the 11 -> 12 step). */
    UPROPERTY()
    float HudGlow = SOL::Settings::HUD_GLOW_DEFAULT;

    /** Mouse steering toggle on at every flight start (SDD 19 Amendment 5). Added in schema 6; an older save is
     *  migrated to off (the new default). */
    UPROPERTY()
    bool bMouseSteerAtStart = SOL::SHIP_STEER_TOGGLE_START_ON;

    /** Overlay profiles (SDD 22, SDD 34). Added in schema 7; an older save is migrated to one default profile (every
     *  overlay on except Asteroid Orbits). Schema 9 (SDD 43) wrote SpaceDust = on into every profile; schema 10
     *  (SDD 34) moves that value to bSpaceDust, removes the key, and enables every stored entry. From schema 10 it is
     *  written with SOL::Overlay::MAX_PROFILES entries (a missing entry reads as disabled and unused). */
    UPROPERTY()
    TArray<FSOLOverlayProfileSave> OverlayProfiles;

    /** The active overlay profile (index into OverlayProfiles, any enabled profile). Added in schema 7. */
    UPROPERTY()
    int32 ActiveOverlayProfile = 0;

    /** Space dust speed cues, one global setting (SDD 34 section 9). Added in schema 10; the 9 -> 10 step takes it
     *  from the first profile's old SpaceDust row (on when it has none). */
    UPROPERTY()
    bool bSpaceDust = SOL::Settings::SPACE_DUST_DEFAULT;

    /** Chase Down safety margin, a floor on the G meter as a fraction of a full meter (SDD 32). Added in schema 8; an
     *  older save is migrated to the default. */
    UPROPERTY()
    float ChaseSafetyMargin = SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT;

    /** Rebound keys only: SOLKeyBindings binding id -> engine key name (the other bindings use their defaults). */
    UPROPERTY()
    TMap<FString, FString> KeyOverrides;
};
