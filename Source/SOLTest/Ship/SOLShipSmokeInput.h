/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;

/**
 * Verification-only scripted input (-SOLSmokeInput): drives the REAL input pipeline by injecting key and mouse events
 * into the player controller (APlayerController::InputKey), so the mapping context, Enhanced Input triggers and the
 * pawn's callbacks run end to end. It checks thrust and braking, the assist toggle, virtual-joystick yaw and pitch,
 * wheel speed-cap steps and clamps, target selection and cycling, the M frame lock and the time-warp keys, logging a
 * LogSOL PASS/FAIL line per check. It ends with a screenshot from behind the ship facing Earth, then quits.
 * Setup steps (ship at rest, aiming at Earth) teleport the ship with USOLShipSubsystem::SetState; everything that is
 * checked goes through input.
 */
class SOLTEST_API FSOLShipSmokeInput
{
public:

    // Caches the subsystems and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLInputPhase : uint8
    {
        Setup,
        ThrustHold,
        Brake,
        AssistOffThrust,
        Coast,
        AssistOn,
        YawRight,
        YawRecenter,
        PitchUp,
        PitchRecenter,
        WheelOneUp,
        WheelOneDown,
        WheelToMax,
        WheelToMin,
        WheelRestore,
        AimEarth,
        SelectEarth,
        CycleNext1,
        CycleNext2,
        CyclePrevious1,
        CyclePrevious2,
        ClearTarget,
        SeekMars,
        LockMars,
        UnlockMars,
        ClearForNearest,
        LockNearest,
        ReleaseNearest,
        WarpUp1,
        WarpUp2,
        WarpDown,
        WarpReset,
        Screenshot,
        Done,
    };

    // Runs a phase's entry actions (key presses, setup)
    void EnterPhase(ESOLInputPhase phase, ASOLShipPawn& pawn);

    // Runs a phase's per-frame actions (continuous mouse or wheel input)
    void TickPhase(ESOLInputPhase phase, ASOLShipPawn& pawn);

    // Runs a phase's exit checks and releases or taps the keys for the next phase
    void ExitPhase(ESOLInputPhase phase, ASOLShipPawn& pawn);

    // Returns the minimum real duration of a phase in seconds
    static double GetPhaseDuration(ESOLInputPhase phase);

    // Returns the minimum number of frames a phase lasts
    static int32 GetPhaseMinFrames(ESOLInputPhase phase);

    // Sends a key press to the player controller
    void PressKey(const FKey& key) const;

    // Sends a key release to the player controller
    void ReleaseKey(const FKey& key) const;

    // Presses a key now and releases it on the next frame
    void TapKey(const FKey& key);

    // Sends one analog axis sample (mouse delta in pixels, wheel notches) to the player controller
    void InjectAxis(const FKey& key, float delta) const;

    // Teleports the ship to rest relative to the reference frame, optionally facing a body, with no rotation
    void SetupShipAtRest(int32 faceBodyIndex) const;

    // Returns the ship's velocity relative to the active reference frame
    FVector3d GetRelativeVelocityMps() const;

    // Returns the registry index of a body by name
    int32 FindBody(const TCHAR* name) const;

    // Returns the display name of a candidate index, or "none"
    FString GetCandidateName(int32 index) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;          // Controller the events are injected into
    TWeakObjectPtr<USOLShipSubsystem> mShips;               // Ship state and control
    TWeakObjectPtr<USOLTargetingSubsystem> mTargeting;      // Selection and frame lock
    TWeakObjectPtr<USOLSimClockSubsystem> mClock;           // Time-warp
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;    // Body names and velocities
    TArray<FKey, TInlineAllocator<4>> mPendingReleases;     // Tapped keys to release on the next frame
    ESOLInputPhase mPhase = ESOLInputPhase::Setup;          // Current phase
    FSOLShipState mReference;                               // Ship state recorded at a phase boundary
    FVector3d mReferenceRelVelocityMps = FVector3d::ZeroVector; // Relative velocity recorded at a phase boundary
    double mPhaseTimeS = 0.0;                               // Real time in the current phase
    double mTotalTimeS = 0.0;                               // Real time since the script started
    double mReferenceValue = 0.0;                           // Scalar recorded at a phase boundary (cap, speed)
    int32 mPhaseFrames = 0;                                 // Frames in the current phase
    int32 mReferenceIndex = INDEX_NONE;                     // Candidate or warp index recorded at a phase boundary
    int32 mEarthIndex = INDEX_NONE;                         // Registry index of Earth
    int32 mMarsIndex = INDEX_NONE;                          // Registry index of Mars
    int32 mFirstCycled = INDEX_NONE;                        // Target after the first R
    int32 mChecksPassed = 0;                                // Checks that passed
    int32 mChecksRun = 0;                                   // Checks evaluated
};
