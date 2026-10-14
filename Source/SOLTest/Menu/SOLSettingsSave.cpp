/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsSave.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Starts with the default settings values
USOLSettingsSave::USOLSettingsSave()
{
    SetValues(SOLSettingsModel::FSettingsValues());
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
}
