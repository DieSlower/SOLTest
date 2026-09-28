/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Universe/SOLBodyRegistry.h"

#include "SOLBodyRegistrySubsystem.generated.h"

class USOLSimClockSubsystem;

/** Game-world owner of the body registry (Sun and planets), kept in step with the sim clock. */
UCLASS()
class SOLTEST_API USOLBodyRegistrySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Populates the solar system and evaluates it at the clock's start time
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Re-evaluates every body's position and velocity at the clock's current sim time; warns once outside 1800-2050
    void UpdateFromClock();

    // Returns the wrapped registry (read-only)
    const FSOLBodyRegistry& GetRegistry() const { return mRegistry; }

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    FSOLBodyRegistry mRegistry;                  // Sun-frame body data (structure of arrays)
    bool mHasWarnedOutsideWindow = false;        // Set once the out-of-validity-window warning has been logged
};
