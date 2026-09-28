/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Game/SOLGameMode.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/World.h"

//////////////////////////////////////////////////////////////////////////
// Populates the solar system and evaluates it at the clock's start time
void USOLBodyRegistrySubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    SimClock = collection.InitializeDependency<USOLSimClockSubsystem>();
    Super::Initialize(collection);

    mRegistry.PopulateSolarSystem();
    UpdateFromClock();
    UE_LOG(LogSOL, Log, TEXT("BodyRegistry %s: %d bodies"), *GetName(), mRegistry.Num());
}

//////////////////////////////////////////////////////////////////////////
// Re-evaluates every body's position and velocity at the clock's current sim time; warns once outside 1800-2050
void USOLBodyRegistrySubsystem::UpdateFromClock()
{
    if (SimClock == nullptr)
    {
        return;
    }
    const FSOLSimClock& clock = SimClock->GetClock();
    mRegistry.Update(clock.GetSecondsSinceJ2000());

    // The Standish elements are fitted to 1800-2050; outside that window orbits degrade, so say so once
    const double centuries = clock.GetCenturiesSinceJ2000();
    if (!mHasWarnedOutsideWindow && (centuries < SOL::SECULAR_ELEMENTS_MIN_CENTURIES
        || centuries > SOL::SECULAR_ELEMENTS_MAX_CENTURIES))
    {
        mHasWarnedOutsideWindow = true;
        UE_LOG(LogSOL, Warning, TEXT("BodyRegistry %s: sim time (T = %.3f centuries) is outside the 1800-2050 validity "
            "window of the planetary elements; orbits are approximate"), *GetName(), centuries);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLBodyRegistrySubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLBodyRegistrySubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}
