/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Flight/SOLFlightMode.h"
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

// A route waypoint order for the player's autopilot (SDD 20): a point at a fixed offset from a body, flown through or
// stopped at. Vectors are in the Unreal-handed universe frame (meters)
struct FSOLAutopilotPointOrder
{
    int32 BodyIndex = INDEX_NONE;                   // Body the point moves with
    FVector3d OffsetM = FVector3d::ZeroVector;      // Point offset from the body's centre
    double PassSpeedMps = 0.0;                      // Fly-through speed at the point (0 = stop)
    double CaptureRadiusM = 0.0;                    // Fly-through capture radius (0 = stop)
    bool bStayAutoOnArrival = false;                // Arrival keeps AUTO and the order (the route sets the next one)
};

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

    // Destroys the player ship entity while the entity manager is still alive (before any subsystem deinitializes)
    virtual void PreDeinitialize() override;

    // Unhooks from the universe update and releases the Mass objects
    virtual void Deinitialize() override;

    // Spawns the player ship in orbit and starts the scripted smoke flight if requested
    virtual void OnWorldBeginPlay(UWorld& world) override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Returns true once the player ship entity exists
    bool HasPlayerShip() const;

    // Creates the player ship entity on a circular orbit above a body (by registry name; Earth if unknown) and makes it
    // the observer; does nothing while a ship exists. Called at begin play when the menu is skipped, else by Play
    void SpawnPlayerShip(const FString& startBody);

    // Destroys the player ship entity (Quit to menu): releases its surface-lock and forgets the frame history, so a
    // later SpawnPlayerShip starts clean
    void DespawnPlayerShip();

    // Returns the -SOLStart=<Body> value, or Earth
    static FString GetCommandLineStartBody();

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

    // Cancels the autopilot order (back to assisted flight), wakes the pilot with a full G meter and counts a pilot
    // reset, which turns the pawn's boost off (a jump arrival or a spawn)
    void ResetPilotState();

    // Returns how many pilot resets happened (the pawn turns its boost toggle off when this changes)
    uint32 GetPilotResetCount() const { return mPilotResetCount; }

    // Orders the autopilot (SDD 18) to fly to a point above a body on the ship's current side and enters AUTO, with
    // the pilot input of the last frame latched (SDD 19 section 3.3); false if invalid
    bool EngageAutopilot(int32 bodyIndex);

    // Cancels the autopilot order, if any, and hands back to assisted flight
    void DisengageAutopilot();

    // Orders the autopilot to a route waypoint (SDD 20). Outside AUTO it enters AUTO with the last frame's held input
    // latched (as EngageAutopilot); in AUTO it only replaces the order. False (nothing changed) if invalid
    bool EngageAutopilotPoint(const FSOLAutopilotPointOrder& order);

    // Returns whether the current autopilot order is a route waypoint (false without an order)
    bool IsAutopilotPointOrder() const;

    // Returns how many times the autopilot arrived so far (bodies and waypoints); a route compares it each frame
    uint32 GetAutopilotArrivalCount() const { return mAutopilotArrivalCount; }

    // Returns the serial of the current (or last) autopilot order; every engage gives a new one
    uint32 GetAutopilotOrderSerial() const { return mAutopilotOrderSerial; }

    // Returns the serial of the order the last arrival belonged to (a route only advances on its own orders)
    uint32 GetLastArrivalOrderSerial() const { return mLastArrivalOrderSerial; }

    // Returns the player's flight mode (SDD 19)
    ESOLFlightMode GetFlightMode() const { return mFlightMode.Mode; }

    // Returns the AUTO input latch (SOLFlightMode::CHANNEL_* bits; verification)
    uint16 GetLatchedChannels() const { return mFlightMode.LatchedChannels; }

    // Toggles assisted / manual flight (C); in AUTO it takes control back in assisted flight. Ignored while the pilot
    // has no control (G-LOC). Returns whether the mode changed
    bool ToggleAssist();

    // Hands the frame's gated pilot input to the cancel rule: manual input in AUTO takes control back in assisted
    // flight (each channel held when AUTO engaged is latched until released). Returns whether it cancelled the
    // autopilot
    bool ApplyPilotInput(const FSOLPilotInput& input);

    // In AUTO, latches the channels of input held when the pawn's control comes back (map, menu, F3 panel or jump
    // warp closed), so they do not cancel the autopilot until released
    void RelatchPilotInput(const FSOLPilotInput& heldInput);

    // Returns whether the pilot has control (not blacked out) as of the last step
    bool PilotHasControl() const;

    // Returns the body the autopilot flies to, or INDEX_NONE
    int32 GetAutopilotBodyIndex() const;

    // Returns the autopilot guidance of the last step (ETA, phase, distance); default when not engaged
    FSOLGuidanceOutput GetAutopilotGuidance() const;

    // Returns the pilot's G meter state as of the last step
    FSOLGMeterState GetGMeter() const;

    // Returns the pilot's G-load (thrust plus surface contact) as of the last step
    double GetGLoadG() const;

    // Returns true while the autopilot flies under a warp too high for precise gravity near a body (HUD hint)
    bool IsAutopilotPrecisionReduced() const;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Runs the ship step for one frame and syncs the observer; bound to the anchor subsystem's OnBodiesUpdated
    void StepShips(float realDeltaSeconds);

    // Fills the frame's warp factor, last frame's bodies and the reference frame's time-warp carry
    void ComputeFrameInputs(double realDeltaS, FSOLShipFrameInputs& outInputs) const;

    // Remembers this frame's bodies and reference frame as the previous state for the next frame's carry
    void SaveFrameHistory();

    // Clears the autopilot order fragment and guidance, without touching the flight mode
    void ClearAutopilotOrder();

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
    double mLastWarpFactor = 1.0;                          // Sim seconds per real second of the last step
    FSOLFlightModeState mFlightMode;                       // Player's flight mode and auto input latch (SDD 19)
    FSOLPilotInput mLastPilotInput;                        // Last gated pilot input (latched when AUTO engages)
    FSOLPilotInput mRelatchInput;                          // Input held when control came back (RelatchPilotInput)
    int32 mRelatchFramesLeft = 0;                          // Frames mRelatchInput is still re-applied to the latch
    uint32 mPilotResetCount = 0;                           // Pilot resets so far (jump arrival, spawn)
    uint32 mAutopilotArrivalCount = 0;                     // Autopilot arrivals so far (SDD 20 routes)
    uint32 mAutopilotOrderSerial = 0;                      // Serial of the current / last autopilot order
    uint32 mLastArrivalOrderSerial = 0;                    // Serial of the order the last arrival belonged to
    bool mIsAutopilotPassThrough = false;                  // Point order whose arrival keeps AUTO (route waypoint)
    bool mHasFrameHistory = false;                         // False until the first step has saved the bodies
    bool mIsSurfaceLockToggleRequested = false;            // L pressed since the last step
};
