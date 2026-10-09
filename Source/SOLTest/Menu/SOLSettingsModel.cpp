/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsModel.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the value clamped to [lo, hi], or the fallback when it is NaN
    float ClampOrDefault(float value, float lo, float hi, float fallback)
    {
        return FMath::IsNaN(value) ? fallback : FMath::Clamp(value, lo, hi);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two floats are within the comparison tolerance
    bool Near(float a, float b)
    {
        return FMath::Abs(a - b) <= 1.0e-4f;
    }
}

//////////////////////////////////////////////////////////////////////////
// Clamps every field into its valid range
SOLSettingsModel::FSettingsValues SOLSettingsModel::Sanitized(const FSettingsValues& values)
{
    const FSettingsValues defaults;
    FSettingsValues result = values;
    result.MasterVolume = ClampOrDefault(values.MasterVolume, 0.0f, 1.0f, defaults.MasterVolume);
    result.SfxVolume = ClampOrDefault(values.SfxVolume, 0.0f, 1.0f, defaults.SfxVolume);
    result.MusicVolume = ClampOrDefault(values.MusicVolume, 0.0f, 1.0f, defaults.MusicVolume);
    result.MouseSensitivity = ClampOrDefault(values.MouseSensitivity, 0.1f, 5.0f, defaults.MouseSensitivity);
    result.HudScale = ClampOrDefault(values.HudScale, 0.5f, 2.0f, defaults.HudScale);
    result.QualityPreset = FMath::Clamp(values.QualityPreset, 0, 4);
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Compares two value sets after sanitizing
bool SOLSettingsModel::AreEqual(const FSettingsValues& a, const FSettingsValues& b)
{
    const FSettingsValues x = Sanitized(a);
    const FSettingsValues y = Sanitized(b);
    return Near(x.MasterVolume, y.MasterVolume) && Near(x.SfxVolume, y.SfxVolume) && Near(x.MusicVolume, y.MusicVolume)
        && Near(x.MouseSensitivity, y.MouseSensitivity) && x.bInvertY == y.bInvertY && Near(x.HudScale, y.HudScale)
        && x.bHudVisible == y.bHudVisible && x.bDefaultFlightAssist == y.bDefaultFlightAssist && x.bVsync == y.bVsync
        && x.QualityPreset == y.QualityPreset;
}

//////////////////////////////////////////////////////////////////////////
// Creates a clean model from the loaded values
SOLSettingsModel::FSettingsModel SOLSettingsModel::MakeModel(const FSettingsValues& loaded)
{
    FSettingsModel model;
    model.Saved = Sanitized(loaded);
    model.Pending = model.Saved;
    return model;
}

//////////////////////////////////////////////////////////////////////////
// Stores new pending values
void SOLSettingsModel::SetPending(FSettingsModel& model, const FSettingsValues& values)
{
    model.Pending = Sanitized(values);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether there are unsaved changes
bool SOLSettingsModel::IsDirty(const FSettingsModel& model)
{
    return !AreEqual(model.Pending, model.Saved);
}

//////////////////////////////////////////////////////////////////////////
// Throws the pending changes away
SOLSettingsModel::FSettingsValues SOLSettingsModel::Revert(FSettingsModel& model)
{
    model.Pending = model.Saved;
    return model.Saved;
}

//////////////////////////////////////////////////////////////////////////
// Commits only when something changed
bool SOLSettingsModel::CommitIfDirty(FSettingsModel& model)
{
    if (!IsDirty(model))
    {
        return false;
    }
    model.Saved = model.Pending;
    return true;
}
