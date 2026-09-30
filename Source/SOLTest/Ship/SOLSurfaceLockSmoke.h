/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLJumpSubsystem;
class USOLShipSubsystem;
class USOLTargetingSubsystem;

/**
 * Verification-only scripted surface-lock run (-SOLSmokeLevel, SDD 4 part 3b): drives the REAL input pipeline by
 * injecting L key events into the player controller, as -SOLSmokeJump does, and places the ship at scripted altitudes
 * above Earth by teleport (USOLShipSubsystem::SetState, velocity matched to Earth so flight-assist holds the altitude).
 * From the default spawn (10,000 km, beyond Earth's manual range) L must do nothing and no HUD line shows. With the
 * reference frame first M-locked to another candidate, at 5 km, tilted 90 degrees off the local vertical, the lock must
 * auto-engage with no key press, replace that frame lock with Earth and align the ship's up with the local vertical at
 * the SDD's time constant (checked one time constant in) and within a few of them (screenshot). L must then release it,
 * stop the alignment (re-tilted, the up must stay 90 degrees off) and keep it released at 5 km (the auto-engage
 * suppression latch), until a climb past 12.5 km clears the latch and the next descent re-engages it (screenshot of
 * the hint while released). At 11 km the warning must show (screenshot), past 12.5 km it must hard-release (keeping
 * the frame lock) and stop aligning. At 500 km the HUD must show the L hint, and L must engage a manual lock
 * (screenshot) that warns past Earth's manual range (its radius) and releases past 1.25 times that. Finally, auto-locked
 * at 5 km, a scripted jump (USOLJumpSubsystem::StartJump) must drop an L pressed during the warp and release the lock
 * on arrival. Each check logs a LogSOL PASS/FAIL line; the run then quits.
 */
class SOLTEST_API FSOLSurfaceLockSmoke
{
public:

    // Caches the controller and subsystems and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLLevelPhase : uint8
    {
        Setup,
        PressOutOfRange,
        PlaceLow,
        Align,
        PressRelease,
        HoldSuppressed,
        ClimbClear,
        Descend,
        ClimbWarn,
        ClimbRelease,
        HoldAfterAutoRelease,
        PlaceOrbit,
        PressManual,
        ManualWarn,
        ManualRelease,
        JumpPlace,
        JumpWarpPress,
        JumpArrive,
        Finish,
        Done,
    };

    // How a placement sets the ship's up: kept, tilted 90 degrees off the local vertical, or along it
    enum class ESOLLevelPlaceUp : uint8
    {
        Keep,
        Tilt,
        Level,
    };

    // Runs a phase's entry actions (key taps, teleports)
    void EnterPhase(ESOLLevelPhase phase);

    // Runs a phase's per-frame work: the suppression hold's watch, and the screenshots before a phase ends
    void TickPhase(ESOLLevelPhase phase, const ASOLShipPawn& pawn);

    // Runs a phase's exit checks
    void ExitPhase(ESOLLevelPhase phase, ASOLShipPawn& pawn);

    // Returns true once a phase may end (its minimum time and frames)
    bool IsPhaseDone(ESOLLevelPhase phase) const;

    // Teleports the ship straight above Earth at an altitude (keeping its bearing), matched to Earth's velocity, with its
    // up kept, tilted 90 degrees off the local vertical, or along it
    void PlaceAboveEarth(double altitudeM, ESOLLevelPlaceUp up);

    // Locks the reference frame (as M does) to the nearest candidate that is not Earth; returns its index or INDEX_NONE
    int32 LockFrameToOtherCandidate();

    // Records when the alignment starts after the tilted placement and samples the up error one time constant later
    void TrackAlignment();

    // Returns the angle between the ship's up and the local vertical above Earth (degrees)
    double ComputeUpErrorDeg() const;

    // Returns a one-line description of the lock state for the check details
    FString DescribeLock() const;

    // Takes a verification screenshot and logs its name
    void Screenshot(const ASOLShipPawn& pawn, const TCHAR* name) const;

    // Presses a key now and releases it on the next frame
    void TapKey(const FKey& key);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;          // Controller the events are injected into
    TWeakObjectPtr<USOLShipSubsystem> mShips;               // Ship state and the surface-lock state
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;    // Body positions, velocities and radii
    TWeakObjectPtr<USOLTargetingSubsystem> mTargeting;      // Reference-frame lock
    TWeakObjectPtr<USOLJumpSubsystem> mJump;                // Scripted jump for the arrival checks
    TArray<FKey, TInlineAllocator<2>> mPendingReleases;     // Tapped keys to release on the next frame
    ESOLLevelPhase mPhase = ESOLLevelPhase::Setup;          // Current phase
    double mPhaseTimeS = 0.0;                               // Real time in the current phase
    double mTotalTimeS = 0.0;                               // Real time since the script started
    double mStartUpErrorDeg = 0.0;                          // Up error right after the tilted placement
    double mRetiltUpErrorDeg = 0.0;                         // Up error right after a re-tilt with the lock released
    double mAlignStartS = -1.0;                             // Script time the engaged lock was first seen (< 0: not yet)
    double mAlignStartErrorDeg = 0.0;                       // Up error at that moment
    double mTauElapsedS = -1.0;                             // Time from then to the one-time-constant sample (< 0: none)
    double mTauErrorDeg = 0.0;                              // Up error at the one-time-constant sample
    int32 mEarthIndex = INDEX_NONE;                         // Earth's registry index
    int32 mPreLockIndex = INDEX_NONE;                       // Candidate M-locked before the auto-engage
    int32 mJumpCountBefore = 0;                             // Completed jumps before the scripted one
    int32 mPhaseFrames = 0;                                 // Frames in the current phase
    int32 mEngagedFramesWhileHeld = 0;                      // Frames the lock was engaged during the suppression hold
    int32 mChecksPassed = 0;                                // Checks that passed
    int32 mChecksRun = 0;                                   // Checks evaluated
    bool mIsShotTaken = false;                              // The current phase's screenshot was requested
    bool mIsJumpStarted = false;                            // The scripted jump's StartJump accepted the pick
};
