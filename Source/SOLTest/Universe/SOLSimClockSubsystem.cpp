/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLSimClockSubsystem.h"

#include "Game/SOLGameMode.h"
#include "SOLConstants.h"
#include "SOLTest.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

//////////////////////////////////////////////////////////////////////////
// Starts the clock at the current real UTC date and time (or the -SOLSimDate= override) with warp 1x
void USOLSimClockSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);

#if !UE_BUILD_SHIPPING
    // Developer verification override: start at a fixed date (SDD 16 section 7, question 1); not in Shipping builds
    FString simDate;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::SIM_DATE, simDate))
    {
        double julianDate = 0.0;
        if (FSOLSimClock::TryParseUtcJulianDate(simDate, julianDate))
        {
            mClock.Init(julianDate);
            UE_LOG(LogSOL, Log, TEXT("SimClock %s: -SOLSimDate=%s, started at JD %.5f"), *GetName(), *simDate,
                mClock.GetJulianDate());
            return;
        }
        UE_LOG(LogSOL, Warning, TEXT("SimClock %s: -SOLSimDate=%s does not parse, using the real date"), *GetName(),
            *simDate);
    }
#endif

    const FDateTime now = FDateTime::UtcNow();
    const double seconds = static_cast<double>(now.GetSecond()) + static_cast<double>(now.GetMillisecond())
        / SOL::MILLISECONDS_PER_SECOND;
    mClock.Init(FSOLSimClock::JulianDateFromUtc(now.GetYear(), now.GetMonth(), now.GetDay(), now.GetHour(),
        now.GetMinute(), seconds));
    UE_LOG(LogSOL, Log, TEXT("SimClock %s: started at %s UTC (JD %.5f)"), *GetName(), *now.ToString(),
        mClock.GetJulianDate());
}

//////////////////////////////////////////////////////////////////////////
// Advances sim time by one frame's real delta times the warp factor
void USOLSimClockSubsystem::AdvanceFrame(const double realDeltaSeconds)
{
    mClock.Advance(realDeltaSeconds);
}

//////////////////////////////////////////////////////////////////////////
// Sets the sim time to a Julian date with warp 1x (tests pin a date with it)
void USOLSimClockSubsystem::SetJulianDate(const double julianDate)
{
    mClock.Init(julianDate);
}

//////////////////////////////////////////////////////////////////////////
// Returns the current sim time as a UTC date and time
FDateTime USOLSimClockSubsystem::GetUtcDateTime() const
{
    return FDateTime::FromJulianDay(mClock.GetJulianDate());
}

//////////////////////////////////////////////////////////////////////////
// Steps the time-warp up one step
void USOLSimClockSubsystem::StepWarpUp()
{
    mClock.StepWarpUp();
}

//////////////////////////////////////////////////////////////////////////
// Steps the time-warp down one step
void USOLSimClockSubsystem::StepWarpDown()
{
    mClock.StepWarpDown();
}

//////////////////////////////////////////////////////////////////////////
// Resets the time-warp to 1x
void USOLSimClockSubsystem::ResetWarp()
{
    mClock.ResetWarp();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLSimClockSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLSimClockSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}
