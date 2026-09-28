/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "SOLDebugHUD.generated.h"

class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;

/**
 * TEMPORARY debug text readout: sim date and warp, nearest body and altitude, anchor, ship speed relative to the
 * active reference frame and absolute, speed cap, assist and boost, frame name, selected target, universe position.
 * Replaced by the real flight HUD in sub-part 1c.
 */
UCLASS()
class SOLTEST_API ASOLDebugHUD : public AHUD
{
    GENERATED_BODY()

public:

    // Draws the debug readout lines
    virtual void DrawHUD() override;

protected:

    // Caches the universe, ship and targeting subsystems
    virtual void BeginPlay() override;

private:

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;
};
