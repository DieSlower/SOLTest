/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSettingsTabs.h"
#include "Menu/SOLSolidMenuMath.h"

#include "CoreMinimal.h"

class APlayerController;
class USOLMenuSubsystem;
class USOLSettingsPageWidget;
class USOLSolidMenuSmokeBoard;

/**
 * Verification-only script of the solid menus (-SOLSmokeSolidMenu, SDD 46 section 14; part 46c scaffold, main menu
 * steps 1-3 in 46d, completed in 46h) in a -game run that starts in the main menu on the smoke settings slot. Steps:
 * the kit's assets loaded (no fallback), the main menu's hull column (its own material, 7 stack buttons, the traces)
 * and the chin recess's version line (SOLBuildVersion::FormatVersionLine, 0.1.YYMM.<n>); the view set to 1280x720;
 * the column's show animation replayed (Opening with the traces dark, the power-up, then Open); a mouse-style hover
 * (NotifyHovered) then Down: one highlight at a time, the settled look and its brush tint equal to
 * SOLSolidMenu::EvaluateButton's, the entry's hover trace drawn; each main menu page opened in the page shell by its
 * entry's click handler (the shell opens; Start location: the start body is the one selected row) and closed by Back
 * (Enter on the focused Back button) or Esc, alternately (the page and the input back at once, the focus on Play, the
 * shell closing input-transparent, then hidden); a reopen during a close (no pop); Settings (46e, SDD 14 step 4: opens
 * on Graphics with six Tab buttons and the solid rows, each other tab shown with its hover trace and the footer kept,
 * the embedded Overlay list in the solid look, a stepper's hover without a trace, a slider / toggle / stepper change
 * then Revert, the 46e follow-ups (the slider fill at 0%, 100%, 50% and while dragged with the real mouse, hover wins
 * over the focus on a slider and an Overlay row, the stepper values and row labels inside their slots),
 * ScrollToBinding showing Key bindings with the row in view, a reopen back on Graphics with every tab at its top); the
 * main menu at strength 0 (open and shell instant, the hover look at once) and 2 (glow doubled); the comparison board
 * (USOLSolidMenuSmokeBoard: every control type enabled and disabled side by side) with a selected tab hovered, then
 * hover-only beside selected-only; strength 0 (the hover look at once, no glow) and strength 2 (glow doubled); then the
 * view at 1000x800 with Settings and Start location open (the column and the shell inside the view). Each step logs
 * "SolidMenu CHECK <id> PASS|FAIL: ..." and takes a window screenshot
 * (Saved/Screenshots/<platform>/SolidMenu_<name>.png) to look at; then a "SolidMenu SUMMARY" line and the quit (a
 * watchdog quits a stuck run).
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

    // Clicks a main menu entry as the mouse does: hovers it (it takes keyboard focus), then its click handler
    void ClickEntry(USOLMenuSubsystem& menu, int32 entry) const;

    // Samples the column's or the shell's animation for the running step (see the sample members)
    void SampleAnim(float progress, float power, bool bShellHitTestInvisible);

    // Clicks a Settings tab as the mouse does: hovers its button (it takes keyboard focus), then its click handler
    void ClickSettingsTab(USOLSettingsPageWidget* page, ESOLSettingsTab tab) const;

    // Runs the Settings steps' checks and screenshots (SDD 14 step 4)
    void ExitSettingsStep(int32 step, USOLMenuSubsystem& menu);

    // Runs the slider fill step's stages (46e follow-up); returns true once done
    bool TickSliderFill(USOLMenuSubsystem& menu);

    // Runs the hover-wins step's stages (46e follow-up); returns true once done
    bool TickHoverWins(USOLMenuSubsystem& menu);

    // Parks the cursor in the view's top-right corner (off every control) with a real Slate mouse move
    void ParkCursor() const;

    // Checks the Settings stepper values against their slots and the row labels against their columns (46e follow-up)
    void CheckSettingsFit(USOLMenuSubsystem& menu, const TCHAR* valuesLabel, const TCHAR* labelsLabel);

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
    int32 mRepeatStep = INDEX_NONE; // The step to run next instead of the following one (the page loop)
    int32 mPageIndex = 0;           // The page loop's page (SOLID_SMOKE_PAGES)
    int32 mSubStage = 0;            // A multi-stage step's stage
    double mSubStageS = 0.0;        // ... and when it began
    uint32 mSeenLeds = 0;           // The column's power-up LEDs seen lit during the replayed open
    bool mIsReturnClosed = false;   // Return from flight: the shell was Closed on the first frame back
    bool mIsReturnTraced = false;   // ... and Play focused with its hover trace drawn then
    FString mReturnDetail;          // ... and what was seen then
    int32 mSettingsTab = 0;         // The Settings tab loop's tab (ESOLSettingsTab position)
    ESOLSettingsTab mScrollFromTab = ESOLSettingsTab::Count;    // ScrollToBinding: the tab before
    ESOLSettingsTab mScrollTabAtOnce = ESOLSettingsTab::Count;  // ... and right after the call
    float mScrollBeforeReopen = -1.0f;  // The Key bindings tab's scroll offset before the reopen's close
    int32 mSubStageFrames = 0;      // The step frame a multi-stage step's stage began on
    int32 mFillRun = 0;             // Slider fills read, and those matching their values
    int32 mFillPassed = 0;
    FString mFillDetail;            // ... and what was read
    bool mIsDragOk = false;         // The drag: the fill followed the dragged value
    FString mDragDetail;
    bool mIsSliderHoverOk = true;   // Hover wins: over a slider, and over an Overlay row
    bool mIsOverlayHoverOk = true;
    bool mIsKeyWinsOk = false;      // ... a key gives the focus back (last input wins)
    bool mIsColumnHoverOk = false;  // ... the main menu's focused Settings entry drops its amber too
    FString mHoverDetail;
    // Animation samples of the running step (SampleAnim)
    int32 mSamples = 0;
    float mFirstProgress = -1.0f;   // Progress and trace power on the first sample
    float mFirstPower = -1.0f;
    float mLastProgress = -1.0f;
    bool mIsMonotonic = true;       // Progress never moved against the phase's direction
    bool mSawMidProgress = false;   // A sample strictly between 0 and 1
    bool mSawMidPower = false;      // A trace power strictly between 0 and 1
    bool mSawHitTestInvisible = false;  // The closing shell was input-transparent on a sample
    bool mIsOpeningSample = true;   // The step samples an open (progress rising), else a close
    bool mWasClosedAtOnce = false;  // A close: the page was None and the focus on Play right after the input
    float mPreProgress = -1.0f;     // A reversal: the shell's progress just before and after the reopen
    float mPostProgress = -1.0f;
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
    bool mIsFinished = false;
};
