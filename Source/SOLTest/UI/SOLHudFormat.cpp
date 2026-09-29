/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudFormat.h"

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
