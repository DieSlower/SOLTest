/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "SOLSpectatorPawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class ULocalPlayer;
class USOLAnchorSubsystem;
class USOLSimClockSubsystem;
struct FInputActionValue;

/**
 * Debug free-fly camera for verifying the universe (not the ship), selected with -SOLSpectator. Its authoritative position is the anchor subsystem's
 * double-precision observer; the actor only mirrors it in render space. WASD/Space/Ctrl move, the mouse looks, the wheel
 * steps speed on a log scale (1 m/s to 0.5c), and [ ] Backspace step the time-warp. Start body and altitude can be set
 * on the command line with -SOLStart=<Body> and -SOLAltitudeKm=<km>; -SOLLookAt=<Body> aims the initial view.
 * When the player ship exists (1b), the ship is the observer: the spectator rides it as a free-look camera and no
 * longer moves the observer itself.
 */
UCLASS()
class SOLTEST_API ASOLSpectatorPawn : public APawn
{
    GENERATED_BODY()

public:

    // Creates the camera and enables ticking
    ASOLSpectatorPawn();

    // Moves the observer by the current input and mirrors it into render space
    virtual void Tick(float deltaSeconds) override;

    // Builds the input objects and binds the actions
    virtual void SetupPlayerInputComponent(UInputComponent* playerInputComponent) override;

    // Moves the mapping context from the previous local player (if any) to the new local controller
    virtual void NotifyControllerChanged() override;

    // Removes the mapping context before the controller lets go of this pawn
    virtual void UnPossessed() override;

    // Returns the current speed cap in m/s
    double GetSpeedCapMps() const;

    // Returns the observer's speed over the last frame in m/s
    double GetCurrentSpeedMps() const { return mCurrentSpeedMps; }

    /** Speed-cap steps per factor of ten on the log scale. */
    UPROPERTY(EditAnywhere, Category = "SOL|Spectator", meta = (ClampMin = "1"))
    int32 SpeedStepsPerDecade = 3;

    /** Speed step at spawn (step 12 at 3 steps per decade is 10 km/s). */
    UPROPERTY(EditAnywhere, Category = "SOL|Spectator", meta = (ClampMin = "0"))
    int32 StartSpeedStep = 12;

protected:

    // Places the observer above the start body (or rides the ship) and registers this actor as the observer
    virtual void BeginPlay() override;

    // Removes the mapping context and unregisters this actor as the observer
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Creates the input actions and the mapping context once
    void CreateInputObjects();

    // Removes the mapping context from the local player it was added to, if any
    void RemoveMappingContext();

    // Moves the actor onto the observer's render location after the universe update (ship-riding mode)
    void FollowObserver();

    // Stores the move input (x forward, y right, z up)
    void HandleMove(const FInputActionValue& value);

    // Clears the move input when the keys are released
    void HandleMoveCompleted(const FInputActionValue& value);

    // Applies mouse look to the control rotation
    void HandleLook(const FInputActionValue& value);

    // Steps the speed cap up or down by the wheel direction
    void HandleSpeedStep(const FInputActionValue& value);

    // Steps the time-warp up
    void HandleWarpUp(const FInputActionValue& value);

    // Steps the time-warp down
    void HandleWarpDown(const FInputActionValue& value);

    // Resets the time-warp to 1x
    void HandleWarpReset(const FInputActionValue& value);

    /** First-person camera following the control rotation. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Spectator")
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> MappingContext;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SpeedAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> WarpUpAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> WarpDownAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> WarpResetAction;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    TWeakObjectPtr<ULocalPlayer> mMappedLocalPlayer; // Local player that currently has MappingContext added
    FVector mMoveInput = FVector::ZeroVector; // Current move input (x forward, y right, z up)
    int32 mSpeedStep = 0;                     // Current step on the log speed scale
    double mCurrentSpeedMps = 0.0;            // Observer speed over the last frame
    FDelegateHandle mUniverseUpdatedHandle;   // Binding to OnUniverseUpdated while riding the ship
    bool mIsRidingShip = false;               // True when the player ship is the observer and this is only a camera
};
