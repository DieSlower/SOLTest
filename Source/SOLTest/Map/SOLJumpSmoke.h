/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class ASOLShipPawn;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLJumpSubsystem;
class USOLMapModeSubsystem;
class USOLShipSubsystem;
class USOLTargetingSubsystem;

/**
 * Verification-only scripted jump (-SOLSmokeJump, SDD 3 sub-part 2e): drives the REAL input pipeline by injecting key,
 * mouse-button and mouse-delta events into the player controller, as -SOLSmokeMapPick does. It first locks the
 * reference frame with M and selects a target with R (both must be cleared by the arrival), then opens the map with J,
 * presses Enter with nothing picked (must do nothing), moves the map camera onto Mars (a verification-only camera
 * setter, since a pan never leaves the ecliptic plane), clicks Mars (reference = Mars), drags and releases (planar
 * offset locked, height left unlocked), turns the ship to face Mars from the destination (test setup, so the arrival
 * screenshot frames Mars), and presses Enter. It then checks the sequence starting (map closed, ship input back, FOV
 * and streak rising, HUD hidden), the midpoint (FOV and streak at SOLWarpCurve's values for the elapsed time used, near
 * their peaks; screenshot), the arrival (ship at Mars's CURRENT position + the locked offset, velocity = Mars's,
 * orientation unchanged, anchor Mars, render origin snapped onto the ship) and the restore (FOV back to its pre-warp
 * value or easing below it, post-process off, HUD drawn, pick cleared; screenshot). The warp plays out in real time
 * (3 s). Each check logs a LogSOL PASS/FAIL line; the run then quits.
 */
class SOLTEST_API FSOLJumpSmoke
{
public:

    // Caches the controller and subsystems and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLJumpPhase : uint8
    {
        Setup,
        LockFrame,
        SelectTarget,
        OpenJ,
        EnterEmpty,
        FocusMars,
        ClickMars,
        DragMars,
        ReleaseMars,
        EnterJump,
        WaitMid,
        WaitArrival,
        Restore,
        Finish,
        Done,
    };

    // Runs a phase's entry actions (key presses, cursor moves, camera placement)
    void EnterPhase(ESOLJumpPhase phase, ASOLShipPawn& pawn);

    // Runs a phase's per-frame actions (mouse deltas)
    void TickPhase(ESOLJumpPhase phase);

    // Runs a phase's exit checks
    void ExitPhase(ESOLJumpPhase phase, ASOLShipPawn& pawn);

    // Returns true once a phase may end (its minimum time and frames, or the condition it waits for)
    bool IsPhaseDone(ESOLJumpPhase phase, const ASOLShipPawn& pawn) const;

    // Checks that the pick was locked on Mars and turns the ship to face Mars from the destination
    void CheckLockAndAimShip();

    // Checks the arrival: position, velocity, orientation, anchor, render origin, target and frame lock cleared, and
    // the warp effect ended on the arrival frame
    void CheckArrival(const ASOLShipPawn& pawn);

    // Takes a verification screenshot and logs its name
    void Screenshot(const ASOLShipPawn& pawn, const TCHAR* name) const;

    // Sends a key press to the player controller
    void PressKey(const FKey& key) const;

    // Sends a key release to the player controller
    void ReleaseKey(const FKey& key) const;

    // Presses a key now and releases it on the next frame
    void TapKey(const FKey& key);

    // Sends one analog axis sample (mouse delta in pixels) to the player controller
    void InjectAxis(const FKey& key, float delta) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;          // Controller the events are injected into
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;          // Map camera and pick state
    TWeakObjectPtr<USOLJumpSubsystem> mJump;                // Warp sequence and arrival
    TWeakObjectPtr<USOLShipSubsystem> mShips;               // Ship state
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;            // Observer, anchor and render origin
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;    // Body names, positions and velocities
    TWeakObjectPtr<USOLTargetingSubsystem> mTargeting;      // Selected target and M frame lock
    TArray<FKey, TInlineAllocator<4>> mPendingReleases;     // Tapped keys to release on the next frame
    ESOLJumpPhase mPhase = ESOLJumpPhase::Setup;            // Current phase
    FQuat4d mOrientationAtEnter = FQuat4d::Identity;        // Ship orientation set before Enter
    FVector2D mMarsScreenPx = FVector2D::ZeroVector;        // Mars's screen position after the camera moved
    FVector2D mLockedOffsetM = FVector2D::ZeroVector;       // Planar offset locked from Mars
    double mPhaseTimeS = 0.0;                               // Real time in the current phase
    double mTotalTimeS = 0.0;                               // Real time since the script started
    int32 mMarsIndex = INDEX_NONE;                          // Mars's registry index
    int32 mPhaseFrames = 0;                                 // Frames in the current phase
    int32 mChecksPassed = 0;                                // Checks that passed
    int32 mChecksRun = 0;                                   // Checks evaluated
};
