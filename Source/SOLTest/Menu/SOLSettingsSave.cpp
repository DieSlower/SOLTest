/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsSave.h"

#include "SOLConstants.h"
#include "UI/SOLOverlayModel.h"

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
    HudEffectsStrength = values.HudEffectsStrength;
    bMouseSteerAtStart = values.bMouseSteerAtStart;
    ChaseSafetyMargin = values.ChaseSafetyMargin;
    bSpaceDust = values.bSpaceDust;

    // Overlay profiles (SDD 34 section 5.1): always MAX_PROFILES entries, so each slot's enabled flag is explicit. A
    // used slot (enabled now or before) keeps its full row map, master and orbit-lines switch; an unused slot is
    // written with an empty row map (that is how UsedProfiles is stored) and both switches on. The legacy global
    // bShowOrbitLines is no longer written (the switch lives in each profile, D18)
    const SOLOverlay::FOverlayPrefs prefs = SOLOverlay::Sanitized(values.OverlayPrefs);
    OverlayProfiles.SetNum(SOL::Overlay::MAX_PROFILES);
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        FSOLOverlayProfileSave& entry = OverlayProfiles[profile];
        const bool bUsed = SOLOverlay::IsProfileUsed(prefs, profile);
        entry.bEnabled = SOLOverlay::IsProfileEnabled(prefs, profile);
        entry.bMasterOn = !bUsed || SOLOverlay::HasProfileBit(prefs.MasterOnProfiles, profile);
        entry.bOrbitLinesOn = !bUsed || SOLOverlay::HasProfileBit(prefs.OrbitLinesProfiles, profile);
        if (bUsed)
        {
            entry.Overlays = SOLOverlay::ToSavedProfile(prefs.Masks[profile]);
        }
        else
        {
            entry.Overlays.Reset();
        }
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
    values.HudEffectsStrength = HudEffectsStrength;
    values.bMouseSteerAtStart = bMouseSteerAtStart;
    values.ChaseSafetyMargin = ChaseSafetyMargin;
    values.bSpaceDust = bSpaceDust;

    // Overlay profiles (SDD 34 section 5.2): entry 0 is always enabled, entry p by its bEnabled, and it is used when
    // enabled or its row map is not empty; a missing entry (none saved by an older file, or fewer than five) keeps the
    // defaults, disabled and unused, and entries beyond MAX_PROFILES are ignored. The model sanitizes the result
    values.OverlayPrefs = SOLOverlay::MakeDefaultPrefs();
    SOLOverlay::FOverlayPrefs& prefs = values.OverlayPrefs;
    const int32 stored = FMath::Min(OverlayProfiles.Num(), SOL::Overlay::MAX_PROFILES);
    for (int32 profile = 0; profile < stored; ++profile)
    {
        const FSOLOverlayProfileSave& entry = OverlayProfiles[profile];
        const bool bEnabled = profile == 0 || entry.bEnabled;
        SOLOverlay::SetProfileBit(prefs.EnabledProfiles, profile, bEnabled);
        SOLOverlay::SetProfileBit(prefs.UsedProfiles, profile, bEnabled || entry.Overlays.Num() > 0);
        SOLOverlay::SetProfileBit(prefs.MasterOnProfiles, profile, entry.bMasterOn);
        SOLOverlay::SetProfileBit(prefs.OrbitLinesProfiles, profile, entry.bOrbitLinesOn);
        prefs.Masks[profile] = SOLOverlay::FromSavedProfile(entry.Overlays);
    }
    prefs.ActiveProfile = ActiveOverlayProfile;
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

    // Schema 7 -> 8 (SDD 32 section 3.10): the Chase Down safety margin, which a v7 file does not have, starts at its
    // default; every other field is kept as loaded, and the key layout is unchanged (no controls-changed notice)
    if (Version == 7)
    {
        ChaseSafetyMargin = SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT;
        Version = 8;
    }

    // Schema 8 -> 9 (SDD 43 section 6.3): every stored overlay profile without a Space Dust entry gains it as on (its
    // default); a profile that already holds the key keeps its value, every other key and the active profile are kept,
    // and a save without profiles gains none (the key layout is unchanged: no controls-changed notice)
    if (Version == 8)
    {
        // The id as it was written at schema 9, a literal on purpose: a migration step must stay fixed forever, even if
        // the live overlay table is renamed or reordered later
        const TCHAR* dustId = TEXT("SpaceDust");
        for (FSOLOverlayProfileSave& profile : OverlayProfiles)
        {
            if (!profile.Overlays.Contains(dustId))
            {
                profile.Overlays.Add(dustId, true);
            }
        }
        Version = 9;
    }

    // Schema 9 -> 10 (SDD 34 section 5.3): overlay profiles. The space dust leaves the profiles and becomes one global
    // setting (profile 1's value, else on); every stored entry was a live profile, so each is enabled with its master
    // on (the old "on at flight start") and its orbit-lines switch from the old global flag (D18), which is left as it
    // is; entries are not padded (a missing one reads disabled and unused) and the active profile is kept. The
    // darkening key's override is dropped (the action is gone, D15); every other override is kept verbatim
    if (Version == 9)
    {
        // The ids as they were written at schema 9, literals on purpose: a migration step must stay fixed forever
        const TCHAR* dustId = TEXT("SpaceDust");
        const TCHAR* darkeningKeyId = TEXT("ToggleDarkening");
        const bool* firstDust = OverlayProfiles.Num() > 0 ? OverlayProfiles[0].Overlays.Find(dustId) : nullptr;
        bSpaceDust = firstDust != nullptr ? *firstDust : SOL::Settings::SPACE_DUST_DEFAULT;
        for (FSOLOverlayProfileSave& profile : OverlayProfiles)
        {
            profile.Overlays.Remove(dustId);
            profile.bEnabled = true;
            profile.bMasterOn = true;
            profile.bOrbitLinesOn = bShowOrbitLines;
        }
        KeyOverrides.Remove(darkeningKeyId);
        Version = 10;
    }
}
