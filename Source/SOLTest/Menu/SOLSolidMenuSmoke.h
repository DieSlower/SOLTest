/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSettingsTabs.h"
#include "Menu/SOLSolidMenuMath.h"

#include "CoreMinimal.h"

class APlayerController;
class UWidget;
class USOLMenuSubsystem;
class USOLReadOnlyPageWidget;
class USOLSettingsPageWidget;
class USOLSolidMenuSmokeBoard;
struct FSOLSolidTabRail;

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
 * view at 1000x800 with Settings, Start location, Controls, Demo and About open (the column and the shell inside the
 * view, the About page's rows and rail inside the shell); then (46g) back at
 * 1280x720: the Controls, About and Settings pages' fit and rails (no scroll), the column capped at 4 entries scrolling
 * (D17: the focused entry scrolled into view, its hover trace drawn at the scrolled y, none out of view), a Controls
 * page put on the viewport 300 units tall (its rail scrolls), then flight and the solid pause menu (SDD 14 steps 5 and
 * 7: the side panel's slide-in and dim, each page in the shell with its fit and rail, Esc / Back closing a page, the
 * Quit choices, Esc resuming with the input back at once and the slide-out after, a re-pause reversing the slide-out,
 * the simulation running while paused, strength 0 instant, strength 2 with a page open (the glow doubled), and the
 * pause menu with Settings open at 1000x800 (the side panel and the shell inside the view); the #42 open, hover and
 * select cues counted where a step opens a panel, moves the focus, clicks or reverses a close, and a list scrolled
 * under the resting real cursor keeping the keyboard focus). 46i: the pause Settings page's stepper values, labels and
 * value texts in their slots and the shell at 1000x800; the Gameplay and Key bindings tabs' room logged ("SolidMenu
 * NOTE") at 1000x800 in both menus; the Controls and Info rows of every tab with a left column inside their cells and
 * the shell at 1000x800 (main and pause) and 1280x720 (pause); a run that logs another number of checks than a whole
 * run (SOLID_SMOKE_EXPECTED_CHECKS) fails. Each step logs
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

    // (46g, Q11) Checks the open, hover and select cues played since the step began against wants (< 0: unchecked)
    void CheckCueDeltas(const TCHAR* label, const USOLMenuSubsystem& menu, int32 wantOpen, int32 wantHover,
        int32 wantSelect);

    // (46g, Q11) Checks that no open cue played since mCueOpenBeforeReopen was read (a reopen during a close)
    void CheckReopenNoCue(const TCHAR* label, const USOLMenuSubsystem& menu);

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

    // Checks a Controls / Info / About tab page (46f, SDD 14 step 3): the first tab shown, the tab count and selected
    // mark, the shown tab's rows inside the content, every left-column text fitting its column, no hover trace, then
    // every tab switched to (the last stays shown, except on Controls: back on the first for its screenshots); id is
    // the check id prefix (46g: the layout read freshly arranged, so also on the open that slid the page in)
    void CheckReadOnlyPage(const TCHAR* id, USOLReadOnlyPageWidget* page);

    // Checks the Demo page layout (46f, Q7): opened at the top, one scroll with the groups, every value text and
    // label inside its slot
    void CheckDemoLayout(const TCHAR* id, USOLMenuSubsystem& menu);

    // Checks a Settings page's stepper values against their slots and its row labels against their columns (46e
    // follow-up; 46i: the main menu's or the pause menu's page)
    void CheckSettingsFit(USOLMenuSubsystem& menu, const USOLSettingsPageWidget* page, const TCHAR* valuesLabel,
        const TCHAR* labelsLabel);

    // (46i) Runs a rows sweep's stages over a read-only page: each tab with a left column shown in turn, painted, its
    // rows read (CheckSweepTab) and the first shotTabs of them screenshotted (SolidMenu_<shotName>-<tab>.png); logs one
    // "<id>-rows" check at the end and returns true then
    bool TickRowsSweep(const TCHAR* id, USOLReadOnlyPageWidget* page, const UWidget* shell, const TCHAR* shotName,
        int32 shotTabs);

    // (46i) Reads the sweep's shown tab: each two-column row's left box at the page's column width, the widest word of
    // each cell inside it, both cells inside the content and the shell (added to the sweep's verdict and detail)
    void CheckSweepTab(const USOLReadOnlyPageWidget& page, const UWidget& shell);

    // (46i) Checks a Settings page's narrow rows at 1000x800 on the shown tab (Key bindings when bKeys, else Gameplay):
    // the tab narrow with its narrow column, each wrapped label inside its row; Key bindings: every key name inside its
    // slot, the slot left of its Rebind button, the narrow Rebind box with room for its label ("<id>-names"); Gameplay:
    // every slider at least SETTINGS_SLIDER_MIN_WIDTH wide ("<id>-sliders")
    void CheckSettingsRoom(const TCHAR* id, const USOLSettingsPageWidget* page, bool bKeys);

    // (46i) Checks a Settings page in a wide view keeps (or is back in) the wide layout: no tab narrow, every label
    // column, row box and Rebind box at its wide size with no narrow override left
    void CheckSettingsWide(const TCHAR* id, const USOLSettingsPageWidget* page);

    // (33b, D20) Checks the shown Key bindings tab's key cap cells ("<id>"): each cell wraps in SOLSettingsTabs::
    // KeyCellWidth of the fitted row (SETTINGS_KEYCAP_CELL_WIDTH when wide), its laid-out caps inside that width and its
    // slot, as tall as its row at most, left of its Rebind button, and the button inside the row
    void CheckSettingsKeyCaps(const TCHAR* id, const USOLSettingsPageWidget* page);

    // Checks a real tab rail lays out without a scroll (46g, D17): a laid-out view, the tabs fitting it, the offset at
    // the top and the last tab inside the view
    void CheckRailFits(const TCHAR* id, FSOLSolidTabRail& rail);

    // Drops every other pending value, then sets the HUD effects strength and applies it alone (46g: the pause opens
    // and closes after a mode change, which reverts a pending value; 46i: so no pending resolution or window mode
    // reaches the engine's GameUserSettings; only the smoke settings slot is written)
    void ApplyStrength(USOLMenuSubsystem& menu, float strength) const;

    // Runs the column scroll step's stages (46g, D17); returns true once done
    bool TickMainScroll(USOLMenuSubsystem& menu);

    // Runs the rail scroll step's stages (46g, D17); returns true once done
    bool TickRailScroll(USOLMenuSubsystem& menu);

    // Runs the resume step's stages (46g, SDD 14 steps 5 and 7); returns true once done
    bool TickPauseResume(USOLMenuSubsystem& menu);

    // Runs the re-pause step's stages (46g, SDD 4.6); returns true once done
    bool TickPauseReverse(USOLMenuSubsystem& menu);

    // Runs the pause strength 0 step's stages (46g, SDD 7); returns true once done
    bool TickPauseStrengthZero(USOLMenuSubsystem& menu);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<USOLSolidMenuSmokeBoard> mBoard;
    TWeakObjectPtr<USOLReadOnlyPageWidget> mRailPage;   // The Controls page the RailScroll step puts on the viewport
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
    int32 mCursorRetries = 0;       // 46i: real-cursor stages retried in this step (a window in front, a moved mouse)
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
    int32 mFitIndex = 0;            // 46g: the 1280x720 fit loop's page (SOLID_SMOKE_FIT_PAGES)
    int32 mPausePageIndex = 0;      // 46g: the pause page loop's page (SOLID_SMOKE_PAUSE_PAGES)
    int32 mDownPresses = 0;         // 46g: Down keys sent by the column scroll step
    float mFirstSlideX = 0.0f;      // 46g: the side panel's slide on the pause's first sampled frame
    double mSimAtPauseS = 0.0;      // 46g: the sim time (s since J2000) when the pause was requested
    bool mHasLeftViewport = false;  // 46g: the pause menu left the viewport during a reversal
    int32 mCueOpenAtStart = 0;      // 46g: the open, hover and select cues played when the step began
    int32 mCueHoverAtStart = 0;
    int32 mCueSelectAtStart = 0;
    int32 mCueOpenBeforeReopen = -1;    // 46g: open cues just before a reopen during a close (-1: none read)
    bool mIsReturnPauseGone = false;    // 46g: return from flight: the pause menu out of the viewport, Closed
    FString mReturnPauseDetail;         // ... and what was seen
    bool mIsResumeInputChecked = false; // 46g: the resume step read the frame after the first in flight
    int32 mSweepTab = INDEX_NONE;   // 46i: a rows sweep's shown tab (INDEX_NONE before the first)
    int32 mSweepFrame = 0;          // ... the step frame it was shown or read on
    int32 mSweepShots = 0;          // ... tabs screenshotted so far
    int32 mSweepRows = 0;           // ... rows read so far
    bool mIsSweepRead = false;      // ... the shown tab was read
    bool mIsSweepShot = false;      // ... and screenshotted (the next tab waits for the shot)
    bool mIsSweepOk = true;         // ... every row read fits
    FString mSweepDetail;           // ... what was read
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
    bool mIsFinished = false;
};
