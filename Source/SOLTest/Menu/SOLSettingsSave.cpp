/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsSave.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Starts with the default settings values (no overlay profile stored yet)
USOLSettingsSave::USOLSettingsSave()
{
    SetValues(SOLSettingsModel::FSettingsValues());

    // No overlay profile stored until the first write (an empty list reads as the default profile, SDD 22)
    OverlayProfiles.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Copies model values into the saved fields
void USOLSettingsSave::SetValues(const SOLSettingsModel::FSettingsValues& values)
{
    MasterVolume = values.MasterVolume;
    SfxVolume = values.SfxVolume;
    MusicVolume = values.MusicVolume;
    MouseSensitivity = values.MouseSensitivity;
    bInvertY = values.bInvertY;
    HudScale = values.HudScale;
    bHudVisible = values.bHudVisible;
    bDefaultFlightAssist = values.bDefaultFlightAssist;
    bVsync = values.bVsync;
    QualityPreset = values.QualityPreset;
    bShowOrbitLines = values.bShowOrbitLines;
    HudEffectsStrength = values.HudEffectsStrength;
    bMouseSteerAtStart = values.bMouseSteerAtStart;

    // One saved profile per overlay profile (SDD 22)
    const SOLOverlay::FOverlayPrefs prefs = SOLOverlay::Sanitized(values.OverlayPrefs);
    OverlayProfiles.SetNum(prefs.ProfileCount);
    for (int32 profile = 0; profile < prefs.ProfileCount; ++profile)
    {
        OverlayProfiles[profile].Overlays = SOLOverlay::ToSavedProfile(prefs.Masks[profile]);
    }
    ActiveOverlayProfile = prefs.ActiveProfile;
}

//////////////////////////////////////////////////////////////////////////
// Returns the saved fields as model values
SOLSettingsModel::FSettingsValues USOLSettingsSave::GetValues() const
{
    SOLSettingsModel::FSettingsValues values;
    values.MasterVolume = MasterVolume;
    values.SfxVolume = SfxVolume;
    values.MusicVolume = MusicVolume;
    values.MouseSensitivity = MouseSensitivity;
    values.bInvertY = bInvertY;
    values.HudScale = HudScale;
    values.bHudVisible = bHudVisible;
    values.bDefaultFlightAssist = bDefaultFlightAssist;
    values.bVsync = bVsync;
    values.QualityPreset = QualityPreset;
    values.bShowOrbitLines = bShowOrbitLines;
    values.HudEffectsStrength = HudEffectsStrength;
    values.bMouseSteerAtStart = bMouseSteerAtStart;

    // Overlay profiles: none saved (an older file) gives the defaults; the model sanitizes the active profile
    values.OverlayPrefs = SOLOverlay::MakeDefaultPrefs();
    values.OverlayPrefs.ProfileCount = FMath::Clamp(OverlayProfiles.Num(), 1, SOL::Overlay::MAX_PROFILES);
    for (int32 profile = 0; profile < FMath::Min(OverlayProfiles.Num(), SOL::Overlay::MAX_PROFILES); ++profile)
    {
        values.OverlayPrefs.Masks[profile] = SOLOverlay::FromSavedProfile(OverlayProfiles[profile].Overlays);
    }
    values.OverlayPrefs.ActiveProfile = ActiveOverlayProfile;
    return values;
}

//////////////////////////////////////////////////////////////////////////
// Upgrades an older schema step by step to SOL::Settings::SAVE_VERSION; current, unversioned or newer saves are left
// alone (the caller ignores the last two)
void USOLSettingsSave::MigrateToCurrentVersion()
{
    if (Version <= 0 || Version > SOL::Settings::SAVE_VERSION)
    {
        return;
    }

    // Schema 1 -> 2 (SDD 17): orbit lines added, shown by default
    if (Version == 1)
    {
        bShowOrbitLines = true;
        Version = 2;
    }

    // Schema 2 -> 3 (SDD 19 section 3.5): the new default key layout. Schemas 1-3 share binding ids and overrides are
    // deltas from the defaults, so every override is kept verbatim (ResolveKeys ignores invalid ones) and every binding
    // without one takes its new default; conflicts with the new defaults are flagged by Settings, not resolved here
    if (Version == 2)
    {
        Version = 3;
    }

    // Schema 3 -> 4 (SDD 20 section 2.1): the J map toggle became Shift+J (OpenMapJump) and Shift+M (OpenMap); a
    // rebound toggle's key moves to OpenMapJump (the map it opened), unless that already has its own override
    if (Version == 3)
    {
        FString toggleKey;
        if (KeyOverrides.RemoveAndCopyValue(TEXT("ToggleMap"), toggleKey)
            && !KeyOverrides.Contains(TEXT("OpenMapJump")))
        {
            KeyOverrides.Add(TEXT("OpenMapJump"), toggleKey);
        }
        Version = 4;
    }

    // Schema 4 -> 5 (SDD 21): the HUD hologram effects strength, which a v4 file does not have, starts at its default
    if (Version == 4)
    {
        HudEffectsStrength = SOL::Settings::HUD_EFFECTS_DEFAULT;
        Version = 5;
    }

    // Schema 5 -> 6 (SDD 19 Amendment 5): mouse steering at start, which a v5 file does not have, starts off (the new
    // default; the user chose off for migrated saves too, so Space is the way to steer)
    if (Version == 5)
    {
        bMouseSteerAtStart = SOL::SHIP_STEER_TOGGLE_START_ON;
        Version = 6;
    }

    // Schema 6 -> 7 (SDD 22): one default overlay profile (every overlay on except Asteroid Orbits). The O key now
    // turns the overlays on and off and the orbit lines moved to Alt+O; a rebound ToggleOrbitLines key keeps its
    // override (the same action, now pressed with Alt), so the overrides are kept verbatim
    if (Version == 6)
    {
        OverlayProfiles.SetNum(1);
        OverlayProfiles[0].Overlays = SOLOverlay::ToSavedProfile(SOLOverlay::DefaultMask());
        ActiveOverlayProfile = 0;
        Version = 7;
    }
}
