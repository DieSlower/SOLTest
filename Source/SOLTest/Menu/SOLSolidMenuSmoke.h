/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSolidMenuMath.h"

#include "CoreMinimal.h"

class APlayerController;
class USOLMenuSubsystem;
class USOLSolidMenuSmokeBoard;

/**
 * Verification-only script of the solid menu kit (-SOLSmokeSolidMenu, SDD 46 section 14; part 46c scaffold, completed
 * in 46h) in a -game run that starts in the main menu on the smoke settings slot. Steps: the kit's assets loaded (no
 * fallback) and the restyled main menu buttons on the hull material; the view set to 1280x720; a mouse-style hover
 * (NotifyHovered) then Down: one highlight at a time, the settled look and its brush tint equal to
 * SOLSolidMenu::EvaluateButton's; the comparison board (USOLSolidMenuSmokeBoard: every control type enabled and
 * disabled side by side) with a selected tab hovered, then hover-only beside selected-only; strength 0 (the hover look
 * at once, no glow) and strength 2 (glow doubled). Each step logs "SolidMenu CHECK <id> PASS|FAIL: ..." and takes a
 * window screenshot (Saved/Screenshots/<platform>/SolidMenu_<name>.png) to look at; then a "SolidMenu SUMMARY" line
 * and the quit (a watchdog quits a stuck run).
 */
class SOLTEST_API FSOLSolidMenuSmoke
{
public:

    // Caches the controller and starts the first step
    void Start(USOLMenuSubsystem& menu);

    // Runs one frame of the script; returns true once it has finished (and requested the quit)
    bool Update(USOLMenuSubsystem& menu);

private:

    // Runs the step's action at its start
    void EnterStep(int32 step, USOLMenuSubsystem& menu);

    // Returns true once the step's wait is over
    bool TickStep(int32 step, USOLMenuSubsystem& menu);

    // Runs the step's checks and screenshot at its end
    void ExitStep(int32 step, USOLMenuSubsystem& menu);

    // Sets the live HUD effects strength (the smoke slot's pending settings; nothing is saved)
    void SetStrength(USOLMenuSubsystem& menu, float strength) const;

    // Returns the game viewport's size in pixels (zero without a controller)
    FIntPoint GetViewSize() const;

    // Logs the SUMMARY line, removes the board and quits
    void Finish(USOLMenuSubsystem& menu);

    // Requests a window screenshot with the UI (Saved/Screenshots/<platform>/SolidMenu_<name>.png); the next step's
    // action waits a few frames so the shot shows this step's screen
    void TakeUiShot(const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<USOLSolidMenuSmokeBoard> mBoard;
    double mLastTimeS = 0.0;        // Real time of the last update
    double mRunTimeS = 0.0;         // Real time since Start (the watchdog)
    double mStepTimeS = 0.0;
    int32 mStep = 0;
    int32 mStepFrames = 0;
    int32 mViewFrames = 0;          // Frames in a row at the requested view size
    int32 mDeferFrames = 0;         // Frames left before the next step's action (after a screenshot)
    FSOLSolidButtonLook mInstantLook;   // Strength 0: the hovered button's look on the frame after the hover
    double mInstantTimeS = -1.0;        // ... and when it was read
    bool mIsShotPending = false;    // A screenshot was requested in this step
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
    bool mIsFinished = false;
};
