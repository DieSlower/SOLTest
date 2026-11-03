/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLActionTable.h"

#include "CoreMinimal.h"

class APlayerController;
class ASOLShipPawn;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLInteractionSubsystem;
class USOLJumpSubsystem;
class USOLMapModeSubsystem;
class USOLMenuSubsystem;
class USOLSettingsSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
class USOLSpaceDustComponent;
class USOLTargetingSubsystem;

/**
 * Verification-only space dust run (-SOLSmokeDust, SDD 43 section 11), driven through the pawn's real paths: a stopped
 * ship (no dust), 3 m/s (points), 50 km/s (streaks near the cap, direction), 0.3c (still visible, apparent speed at
 * the cap), the M frame lock on a body the ship moves with (fade out and hide), time warp 10x (hidden at once) and back
 * to 1x (fade-in reset), a Moon jump (hidden for the whole warp), the map (hidden), a Slate click on the Space Dust
 * row of the Ctrl+O Overlay Menu (hidden, shown, one settings write at Esc), the master O (no effect), the pause menu
 * (hidden), a 50 km/s run through render-origin rebases (continuous wrapped offset, the Niagara component tracking the
 * ship) and a ship resting on the Moon's surface (no dust). With -SOLSmokeDustProfile (in -game only) it also captures
 * ProfileGPU and stat dumpave of STAT_SOL_SpaceDustUpdate with the dust shown at 50 km/s and again with
 * sol.SpaceDust 0 (numbers in the log).
 * Screenshots go to Saved/Screenshots/DustSmoke_*.png. Runs with the smoke settings slot and quits at the end. Each
 * check is logged as "SmokeDust CHECK <label> PASS|FAIL: <detail>".
 */
class SOLTEST_API FSOLSpaceDustSmoke
{
public:

    // Caches the controller, subsystems and the dust component and starts the first step
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // How the script keeps the ship's velocity each frame
    enum class EHold : uint8
    {
        None,           // The ship flies on its own (jump, landed)
        Relative,       // Reference-frame velocity plus a fixed relative velocity
        Body,           // The velocity of a body
    };

    // Runs the step's action at its start
    void EnterStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's per-frame work; returns true once the step is done
    bool TickStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's checks at its end
    void ExitStep(int32 step, ASOLShipPawn& pawn);

    // Sets the ship's velocity for this frame from the hold mode
    void ApplyHold() const;

    // Records the first frame the dust is shown and the first frame it is hidden and paused, within the step
    void ObserveDust();

    // Follows the offset continuity and the Niagara location frame by frame (the rebase step)
    void TrackRebase();

    // Holds the ship at a velocity relative to the reference frame (m/s, Unreal-handed)
    void HoldRelative(const FVector3d& relativeVelocityMps);

    // Holds the ship at the velocity of a body
    void HoldBody(int32 bodyIndex);

    // Stops holding the ship's velocity
    void HoldNone();

    // Returns the velocity of the ship relative to its reference frame (m/s)
    FVector3d GetRelativeVelocityMps() const;

    // Moves the ship onto the Moon's surface at rest in the Moon's frame
    void PlaceOnMoonSurface() const;

    // Sends a real Slate click to an Overlay Menu row; false when the menu is not there
    bool ClickRow(const ASOLShipPawn& pawn, int32 row) const;

    // Sends Esc through Slate (closes the Overlay Menu)
    void PressEscape() const;

    // Runs a console command in the game world
    void RunCommand(const TCHAR* command, UWorld* world) const;

    // Requests a window screenshot (with the UI or the scene only) to Saved/Screenshots
    void TakeShot(const TCHAR* name, bool bWithUi);

    // With -SOLSmokeDustProfile: runs one cost capture (stat dumpave, ProfileGPU) from the first frame it is called;
    // returns true once it is done, or at once without the flag
    bool TickProfile(const TCHAR* label, UWorld* world);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<USOLSpaceDustComponent> mDust;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;
    TWeakObjectPtr<USOLSimClockSubsystem> mClock;
    TWeakObjectPtr<USOLJumpSubsystem> mJump;
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;
    TWeakObjectPtr<USOLMenuSubsystem> mMenu;
    TWeakObjectPtr<USOLSettingsSubsystem> mSettings;
    TWeakObjectPtr<USOLTargetingSubsystem> mTargeting;
    TWeakObjectPtr<USOLInteractionSubsystem> mInteraction;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mBodies;
    FSOLObjectRef mMoon;
    FVector3d mHoldVelocityMps = FVector3d::ZeroVector;     // Relative hold: the velocity relative to the frame
    FVector3d mPrevOffsetCm = FVector3d::ZeroVector;        // Rebase step: last frame's wrapped offset
    FVector3d mPrevOriginM = FVector3d::ZeroVector;         // Rebase step: last frame's render origin
    EHold mHold = EHold::None;
    double mStepTimeS = 0.0;
    double mClockS = 0.0;                                   // Real seconds since the script started
    double mLastLogS = 0.0;
    double mShownAtS = -1.0;                                // Step time of the first shown frame, -1 if none yet
    double mHiddenAtS = -1.0;                               // Step time of the first hidden-and-paused frame
    double mShownOpacity = -1.0;                            // Smoothed opacity on the first shown frame
    double mMapOpenedAtS = -1.0;                            // Step time the map was first seen open
    double mPrevSpeedCmPerS = 0.0;                          // Rebase step: last frame's smoothed apparent speed
    double mMaxStepRatio = 0.0;                             // Rebase step: largest offset step over its bound
    double mMaxLocationErrorCm = 0.0;                       // Rebase step: largest Niagara location error
    double mMaxLocationCm = 0.0;                            // Rebase step: largest Niagara distance from the origin
    double mForwardTravelCm = 0.0;                          // Rebase step: offset travel along the relative velocity
    double mProfileStartS = 0.0;                            // Script clock at the capture's start
    uint64 mLastShotFrame = 0;
    int32 mStep = 0;
    int32 mStepFrames = 0;
    int32 mShownFrame = -1;                                 // Step frame of the first shown frame
    int32 mHiddenFrame = -1;                                // Step frame of the first hidden-and-paused frame
    int32 mMatchBodyIndex = INDEX_NONE;                     // Body the match step moves with (not the anchor)
    int32 mHoldBodyIndex = INDEX_NONE;
    int32 mWarpFrames = 0;                                  // Consecutive frames the jump warp has run
    int32 mWarpSeenFrames = 0;                              // Frames the jump warp ran in the jump step
    int32 mWarpShownFrames = 0;                             // ... of which the dust was shown after the grace frames
    int32 mJumpsBefore = 0;
    int32 mSavesBefore = 0;                                 // Slot writes before the Overlay Menu row clicks
    int32 mRebaseCount = 0;
    int32 mContinuityFrames = 0;
    int32 mContinuityViolations = 0;
    int32 mLocationFrames = 0;
    int32 mLocationViolations = 0;
    int32 mProfileFrame = 0;                                // Frames into this step's cost capture
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
    bool mProfile = false;                                  // -SOLSmokeDustProfile
    bool mWasShownAtEnter = false;                          // The dust was shown when the step began
    bool mHasPrevOffset = false;
    bool mHasPrevOrigin = false;
    bool mIsLevelPressed = false;                           // Landed step: L pressed once
    bool mIsRowClicked = false;                             // Click-off step: the Space Dust row clicked once
};
