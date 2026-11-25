/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Universe/SOLSimClock.h"

#include "SOLSimClockSubsystem.generated.h"

/** Game-world owner of the sim clock: starts at the real current UTC time and applies stepped time-warp. */
UCLASS()
class SOLTEST_API USOLSimClockSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Starts the clock at the current real UTC date and time with warp 1x
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Advances sim time by one frame's real delta times the warp factor (called by the universe driver)
    void AdvanceFrame(double realDeltaSeconds);

    // Sets the sim time to a Julian date with warp 1x (tests pin a date with it; the body registry picks it up on its
    // next UpdateFromClock)
    void SetJulianDate(double julianDate);

    // Returns how many times the sim time was set to a new date (a time jump, not the per-frame advance; holding the
    // same pinned date again does not count); readers that keep time-derived state (the lane network, SDD 53 4.3)
    // compare it each frame and rebuild when it changed
    uint32 GetTimeSetCount() const { return mTimeSetCount; }

    // Returns the wrapped clock (read-only)
    const FSOLSimClock& GetClock() const { return mClock; }

    // Returns the current sim time as a UTC date and time
    FDateTime GetUtcDateTime() const;

    // Steps the time-warp up one step
    void StepWarpUp();

    // Steps the time-warp down one step
    void StepWarpDown();

    // Resets the time-warp to 1x
    void ResetWarp();

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    FSOLSimClock mClock;            // Sim time and warp state
    uint32 mTimeSetCount = 0;       // SetJulianDate calls with a new date so far
    double mLastSetJulianDate = 0.0;    // The date of the last SetJulianDate
    bool mHasSetJulianDate = false;
};
