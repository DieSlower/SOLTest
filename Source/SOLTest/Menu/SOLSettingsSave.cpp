/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsSave.h"

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
    return values;
}
