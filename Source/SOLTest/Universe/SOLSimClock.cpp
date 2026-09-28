/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLSimClock.h"

#include "SOLConstants.h"

namespace
{
    // Sim seconds per real second: 1x, 10x, 100x, 1000x, 1 h/s, 1 d/s, 10 d/s, 30 d/s
    constexpr double SIMCLOCK_WARP_FACTORS[] = { 1.0, 10.0, 100.0, 1000.0, 3600.0, 86400.0, 864000.0, 2592000.0 };
    constexpr int32 SIMCLOCK_WARP_COUNT = UE_ARRAY_COUNT(SIMCLOCK_WARP_FACTORS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the warp factor table
TConstArrayView<double> FSOLSimClock::WarpFactors()
{
    return TConstArrayView<double>(SIMCLOCK_WARP_FACTORS, SIMCLOCK_WARP_COUNT);
}

//////////////////////////////////////////////////////////////////////////
// Converts a proleptic Gregorian UTC date and time to a Julian date (Fliegel-Van Flandern day number)
double FSOLSimClock::JulianDateFromUtc(const int32 year, const int32 month, const int32 day, const int32 hour,
    const int32 minute, const double second)
{
    // Integer Julian day number of the calendar date (the day that starts at noon)
    const int32 a = (14 - month) / 12;
    const int32 y = year + 4800 - a;
    const int32 m = month + 12 * a - 3;
    const int32 dayNumber = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;

    // Offset from noon in seconds, added as a single day fraction to keep rounding to one step
    const double secondsFromNoon = static_cast<double>((hour - 12) * 3600 + minute * 60) + second;
    return static_cast<double>(dayNumber) + secondsFromNoon / SOL::SECONDS_PER_DAY;
}

//////////////////////////////////////////////////////////////////////////
// Sets the sim time and resets the warp to index 0
void FSOLSimClock::Init(const double startJulianDate)
{
    mSecondsSinceJ2000 = (startJulianDate - J2000JulianDate) * SOL::SECONDS_PER_DAY;
    mWarpIndex = 0;
}

//////////////////////////////////////////////////////////////////////////
// Advances sim time by the real delta times the current warp factor
void FSOLSimClock::Advance(const double realDeltaSeconds)
{
    mSecondsSinceJ2000 += realDeltaSeconds * SIMCLOCK_WARP_FACTORS[mWarpIndex];
}

//////////////////////////////////////////////////////////////////////////
// Returns the current Julian date
double FSOLSimClock::GetJulianDate() const
{
    return J2000JulianDate + mSecondsSinceJ2000 / SOL::SECONDS_PER_DAY;
}

//////////////////////////////////////////////////////////////////////////
// Returns the sim seconds since J2000
double FSOLSimClock::GetSecondsSinceJ2000() const
{
    return mSecondsSinceJ2000;
}

//////////////////////////////////////////////////////////////////////////
// Returns the Julian centuries since J2000
double FSOLSimClock::GetCenturiesSinceJ2000() const
{
    return mSecondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;
}

//////////////////////////////////////////////////////////////////////////
// Returns the current warp step index
int32 FSOLSimClock::GetWarpIndex() const
{
    return mWarpIndex;
}

//////////////////////////////////////////////////////////////////////////
// Returns the current warp factor
double FSOLSimClock::GetWarpFactor() const
{
    return SIMCLOCK_WARP_FACTORS[mWarpIndex];
}

//////////////////////////////////////////////////////////////////////////
// Steps the warp up one step, clamping at the last step
void FSOLSimClock::StepWarpUp()
{
    mWarpIndex = FMath::Min(mWarpIndex + 1, SIMCLOCK_WARP_COUNT - 1);
}

//////////////////////////////////////////////////////////////////////////
// Steps the warp down one step, clamping at index 0
void FSOLSimClock::StepWarpDown()
{
    mWarpIndex = FMath::Max(mWarpIndex - 1, 0);
}

//////////////////////////////////////////////////////////////////////////
// Resets the warp to index 0
void FSOLSimClock::ResetWarp()
{
    mWarpIndex = 0;
}
