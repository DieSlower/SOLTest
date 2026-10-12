/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudFormat.h"

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

namespace
{
    constexpr double HUD_KM_UNIT_MIN_MPS = 1000.0;                              // Speeds at or above this read in km/s
    constexpr double HUD_LIGHT_UNIT_MIN_MPS = 0.01 * SOL::SPEED_OF_LIGHT_MPS;   // Speeds above this read in c
    constexpr double HUD_KM_DISTANCE_MIN_M = 1000.0;                            // Distances at or above this read in km
    constexpr double HUD_AU_DISTANCE_MIN_M = 0.01 * SOL::AU_M;                  // Distances at or above this read in AU

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of meters per second in one of the unit
    double HudUnitScaleMps(const ESOLSpeedUnit unit)
    {
        switch (unit)
        {
        case ESOLSpeedUnit::KilometersPerSecond:
            return SOL::METERS_PER_KM;
        case ESOLSpeedUnit::LightSpeed:
            return SOL::SPEED_OF_LIGHT_MPS;
        case ESOLSpeedUnit::MetersPerSecond:
        default:
            return 1.0;
        }
    }
}

namespace SOLHudFormat
{
    //////////////////////////////////////////////////////////////////////////
    // Picks the most readable unit for a speed magnitude: m/s under 1000, km/s under 0.01c, else c
    ESOLSpeedUnit PickSpeedUnit(const double speedMps)
    {
        const double magnitude = FMath::Abs(speedMps);
        if (magnitude < HUD_KM_UNIT_MIN_MPS)
        {
            return ESOLSpeedUnit::MetersPerSecond;
        }
        if (magnitude < HUD_LIGHT_UNIT_MIN_MPS)
        {
            return ESOLSpeedUnit::KilometersPerSecond;
        }
        return ESOLSpeedUnit::LightSpeed;
    }

    //////////////////////////////////////////////////////////////////////////
    // Converts m/s to the unit's display value
    double ToUnitValue(const double speedMps, const ESOLSpeedUnit unit)
    {
        return speedMps / HudUnitScaleMps(unit);
    }

    //////////////////////////////////////////////////////////////////////////
    // Converts a unit display value back to m/s
    double FromUnitValue(const double value, const ESOLSpeedUnit unit)
    {
        return value * HudUnitScaleMps(unit);
    }

    //////////////////////////////////////////////////////////////////////////
    // Formats a distance as meters (1 decimal), kilometers (1 decimal) or AU (4 decimals)
    FString FormatDistanceM(const double distanceM)
    {
        FString text;
        AppendDistanceM(text, distanceM);
        return text;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a distance as meters (1 decimal), kilometers (1 decimal) or AU (4 decimals) to the buffer
    void AppendDistanceM(FString& outText, const double distanceM)
    {
        if (distanceM < HUD_KM_DISTANCE_MIN_M)
        {
            outText.Appendf(TEXT("%.1f m"), distanceM);
        }
        else if (distanceM < HUD_AU_DISTANCE_MIN_M)
        {
            outText.Appendf(TEXT("%.1f km"), distanceM / SOL::METERS_PER_KM);
        }
        else
        {
            outText.Appendf(TEXT("%.4f AU"), distanceM / SOL::AU_M);
        }
    }
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a binding's key label (unbound when the array is short)
    FString HudKeyLabel(const TArray<FString>& keys, const SOLKeyBindings::EBinding binding)
    {
        const int32 index = static_cast<int32>(binding);
        return SOLBindingLabel::KeyDisplayName(keys.IsValidIndex(index) ? keys[index] : FString());
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the flight HUD's key hint line from the ship's keys
FString SOLHudFormat::FormatFlightKeyHints(const TArray<FString>& keys, const bool bOrbitLinesOn)
{
    using SOLKeyBindings::EBinding;
    using namespace SOLMenuText;
    FString hints = FString::Format(bOrbitLinesOn ? HUD_HINT_ORBITS_ON_FORMAT : HUD_HINT_ORBITS_OFF_FORMAT,
        { HudKeyLabel(keys, EBinding::ToggleOrbitLines) });
    hints += FString::Format(HUD_HINT_KEYS_FORMAT, { HudKeyLabel(keys, EBinding::SpeedPanel),
        HudKeyLabel(keys, EBinding::ToggleMap), HudKeyLabel(keys, EBinding::SelectTarget),
        HudKeyLabel(keys, EBinding::NextTarget), HudKeyLabel(keys, EBinding::PreviousTarget),
        HudKeyLabel(keys, EBinding::ClearTarget), HudKeyLabel(keys, EBinding::MatchLock),
        HudKeyLabel(keys, EBinding::SurfaceLock), HudKeyLabel(keys, EBinding::WarpDown),
        HudKeyLabel(keys, EBinding::WarpUp), HudKeyLabel(keys, EBinding::RadarZoomIn),
        HudKeyLabel(keys, EBinding::RadarZoomOut), HudKeyLabel(keys, EBinding::RadarAuto),
        HudKeyLabel(keys, EBinding::Fire), HudKeyLabel(keys, EBinding::DropTarget) });
    return hints;
}

//////////////////////////////////////////////////////////////////////////
// Returns the jump map's first hint line
FString SOLHudFormat::FormatMapKeyHints(const TArray<FString>& keys)
{
    return FString::Format(SOLMenuText::HUD_HINT_MAP_FORMAT,
        { HudKeyLabel(keys, SOLKeyBindings::EBinding::ToggleMap) });
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD and speed panel texts that name a key, from the ship's keys
SOLHudFormat::FHudKeyTexts SOLHudFormat::FormatHudKeyTexts(const TArray<FString>& keys)
{
    FHudKeyTexts texts;
    texts.SurfaceLockHint = SOLKeyBindings::FormatKeyText(SOLMenuText::HUD_SURFACE_LOCK_HINT_FORMAT, keys);
    texts.FrameLockTag = SOLKeyBindings::FormatKeyText(SOLMenuText::HUD_FRAME_LOCK_TAG_FORMAT, keys);
    texts.NoTarget = SOLKeyBindings::FormatKeyText(SOLMenuText::HUD_NO_TARGET_FORMAT, keys);
    texts.SpeedPanelTitle = SOLKeyBindings::FormatKeyText(SOLMenuText::SPEED_PANEL_TITLE_FORMAT, keys);
    return texts;
}
