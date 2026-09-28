/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Ship/SOLShipSmokeFlight.h"

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLShipSubsystem.generated.h"

class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLShipFlightProcessor;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;
struct FMassCommandBuffer;
struct FMassEntityManager;
struct FSOLShipFrameInputs;

/**
 * Owns the player ship's Mass entity and steps it every frame. The step runs from USOLAnchorSubsystem's
 * OnBodiesUpdated (after the clock and bodies, before the anchor) on REAL time, and the ship's position then becomes
 * the observer's, so the ship, not a spectator, drives anchoring and rebasing. Each step first refreshes
 * USOLTargetingSubsystem's candidates, then takes the assist reference velocity from it (M lock, else the anchor).
 *
 * Time-warp carry: each step computes the active reference frame's carry (SOLFlight::FrameCarryDisplacement from its
 * previous and current position, and the same for its velocity) and hands it to the flight processor, which applies it
 * to every ship. A body reference always has a previous state (all bodies are remembered each frame), so an anchor or
 * lock switch between bodies carries with the new frame at once; only the first frame, and a switch to or between
 * non-body targetables, carries nothing that frame and re-seeds. A paused world does not tick, so nothing is carried.
 *
 * All ship-facing values are in the Unreal-handed universe frame (SOLRender::EclipticToUnreal applied, meters).
 */
UCLASS()
class SOLTEST_API USOLShipSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Declares its dependencies and prepares the flight processor
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Unhooks from the universe update and releases the Mass objects
    virtual void Deinitialize() override;

    // Spawns the player ship in orbit and starts the scripted smoke flight if requested
    virtual void OnWorldBeginPlay(UWorld& world) override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Returns true once the player ship entity exists
    bool HasPlayerShip() const;

    // Sets the player ship's control input; ReferenceVelocityMps is replaced each step by the targeting frame's velocity
    void SetControl(const FSOLShipControl& control);

    // Returns the player ship's current control input
    FSOLShipControl GetControl() const;

    // Returns a copy of the player ship's state (Unreal-handed universe frame)
    FSOLShipState GetState() const;

    // Teleports the player ship to a new state (Unreal-handed universe frame) and moves the observer with it
    void SetState(const FSOLShipState& state);

    // Returns the player ship's universe position in the ecliptic frame (meters)
    FVector3d GetUniversePositionM() const;

    // Sets the player ship's flight-model parameters
    void SetFlightParams(const FSOLFlightParams& params);

    // Returns the player ship's flight-model parameters
    FSOLFlightParams GetFlightParams() const;

    // Returns the body the ship rested on during the last step, or INDEX_NONE
    int32 GetContactBodyIndex() const;

    // Returns the active reference frame's velocity (Unreal-handed universe frame, m/s), from the targeting subsystem
    FVector3d GetReferenceVelocityMps() const;

    // Returns the index of the body whose surface is nearest the ship, and the ship's altitude above it (meters)
    int32 FindNearestBody(double& outAltitudeM) const;

    // Returns true while a verification script (-SOLSmokeFlight) drives the control, so the pawn must not write it
    bool IsControlScripted() const { return mSmokeFlight.IsValid(); }

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Creates the player ship entity on a circular orbit above the start body and makes it the observer
    void SpawnPlayerShip();

    // Runs the ship step for one frame and syncs the observer; bound to the anchor subsystem's OnBodiesUpdated
    void StepShips(float realDeltaSeconds);

    // Fills the frame's warp factor, last frame's bodies and the reference frame's time-warp carry
    void ComputeFrameInputs(double realDeltaS, FSOLShipFrameInputs& outInputs) const;

    // Remembers this frame's bodies and reference frame as the previous state for the next frame's carry
    void SaveFrameHistory();

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipFlightProcessor> FlightProcessor;

    TSharedPtr<FMassEntityManager> mEntityManager;         // The world's default Mass entity manager
    TSharedPtr<FMassCommandBuffer> mCommandBuffer;         // Reused by every run so a step allocates none
    TUniquePtr<FSOLShipSmokeFlight> mSmokeFlight;          // Verification-only scripted flight (-SOLSmokeFlight)
    FMassEntityHandle mPlayerShip;                         // The player's ship entity
    FDelegateHandle mBodiesUpdatedHandle;                  // Binding to the anchor subsystem's OnBodiesUpdated
    TArray<FVector3d> mPrevBodyPositionsM;                 // Last frame's body positions (Unreal-handed), sized once
    TArray<FVector3d> mPrevBodyVelocitiesMps;              // Last frame's body velocities (Unreal-handed), sized once
    FVector3d mPrevReferencePositionM = FVector3d::ZeroVector;  // Last frame's reference-frame position
    FVector3d mPrevReferenceVelocityMps = FVector3d::ZeroVector; // Last frame's reference-frame velocity
    int32 mPrevReferenceIndex = INDEX_NONE;                // Last frame's reference candidate index
    bool mHasFrameHistory = false;                         // False until the first step has saved the bodies
};
