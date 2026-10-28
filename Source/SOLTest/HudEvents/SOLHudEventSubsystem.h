/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "HudEvents/SOLHudBus.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLHudEventSubsystem.generated.h"

/**
 * The world's HUD event bus (SDD 21 decision D4): the ship pawn and the flight HUD talk only through it, so neither
 * includes the other. Frame order: the pawn publishes its pilot snapshot in its tick (TG_PrePhysics), the HUD reads it
 * in DrawHUD after the world tick; commands are broadcast when the pawn handles the key.
 */
UCLASS()
class SOLTEST_API USOLHudEventSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Returns the world's bus subsystem, or nullptr
    static USOLHudEventSubsystem* Get(const UObject* worldContext);

    // Returns the bus
    FSOLHudBus& GetBus() { return mBus; }

    // Returns the bus (read only)
    const FSOLHudBus& GetBus() const { return mBus; }

protected:

    // Exists in game and PIE worlds only
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    FSOLHudBus mBus;    // Pilot snapshot, commands and the map overlay
};
