/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLSimClockSubsystem.h"

#include "Game/SOLGameMode.h"
#include "SOLConstants.h"
#include "SOLTest.h"

#include "Engine/World.h"

//////////////////////////////////////////////////////////////////////////
// Starts the clock at the current real UTC date and time with warp 1x
void USOLSimClockSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);

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
