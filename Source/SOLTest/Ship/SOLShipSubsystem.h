/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Level/SOLSurfaceLock.h"
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

// Fired at the end of every ship step (after the observer sync, before the anchor update) with the step's real delta
DECLARE_MULTICAST_DELEGATE_OneParam(FSOLOnShipsStepped, float /*realDeltaSeconds*/);

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
 * Surface-lock (SDD 4): the subsystem owns the player's FSOLSurfaceLockState and advances it once per step, right after
 * the flight run (ship and bodies then both hold this frame's positions, so the altitudes are exact) and before
 * OnShipsStepped, whatever the map or a jump warp is doing (so the auto-engage suppression latch always sees this
 * frame's altitude). Engaging (false -> true) locks the reference frame to the body
 * (USOLTargetingSubsystem::LockToBodyIndex), which the next step's assist uses. L presses arrive through
 * RequestSurfaceLockToggle and are consumed by exactly one step as its edge-triggered press. While engaged, the step
 * names the locked body in the ship's control fragment and the next flight run applies the alignment per substep.
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

    // Fired at the end of every ship step, once the ship and the observer hold this frame's state and before the anchor
    // update; a listener may teleport the ship here (SetState) and the anchor update that follows sees the result
    FSOLOnShipsStepped& OnShipsStepped() { return mOnShipsStepped; }

    // Returns true while a verification script (-SOLSmokeFlight) drives the control, so the pawn must not write it
    bool IsControlScripted() const { return mSmokeFlight.IsValid(); }

    // Requests a surface-lock toggle (L); the next ship step consumes it as that step's one edge-triggered press
    void RequestSurfaceLockToggle() { mIsSurfaceLockToggleRequested = true; }

    // Returns the player ship's surface-lock state as of the last step
    const FSOLSurfaceLockState& GetSurfaceLockState() const { return mSurfaceLockState; }

    // Returns the surface-lock ranges and alignment time constant in use
    const FSOLSurfaceLockParams& GetSurfaceLockParams() const { return mSurfaceLockParams; }

    // Returns the ship's altitude above the locked body's surface at the last step (meters; 0 when not engaged)
    double GetSurfaceLockAltitudeM() const { return mSurfaceLockAltitudeM; }

    // Releases any surface-lock and clears its auto-engage suppression and a pending L press (used by a jump arrival)
    void ClearSurfaceLock();

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

    // Advances the surface-lock state one step (consuming a pending L press) and names the body to align to, if any
    void UpdateSurfaceLock();

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
    FSOLOnShipsStepped mOnShipsStepped;                    // Listeners after each ship step
    TArray<FVector3d> mPrevBodyPositionsM;                 // Last frame's body positions (Unreal-handed), sized once
    TArray<FVector3d> mPrevBodyVelocitiesMps;              // Last frame's body velocities (Unreal-handed), sized once
    FVector3d mPrevReferencePositionM = FVector3d::ZeroVector;  // Last frame's reference-frame position
    FVector3d mPrevReferenceVelocityMps = FVector3d::ZeroVector; // Last frame's reference-frame velocity
    int32 mPrevReferenceIndex = INDEX_NONE;                // Last frame's reference candidate index
    FSOLSurfaceLockState mSurfaceLockState;                // Player ship's surface-lock (default: released)
    FSOLSurfaceLockParams mSurfaceLockParams;              // Surface-lock ranges and time constant (SOLConstants)
    double mSurfaceLockAltitudeM = 0.0;                    // Altitude above the locked body at the last step
    bool mHasFrameHistory = false;                         // False until the first step has saved the bodies
    bool mIsSurfaceLockToggleRequested = false;            // L pressed since the last step
};
