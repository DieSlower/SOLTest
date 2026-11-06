/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSettingsTabs.h"
#include "UI/SOLUiProfileCapture.h"

#include "CoreMinimal.h"

class APlayerController;
class USOLMenuSubsystem;

/**
 * Verification-only cost capture of the full-screen menus (-SOLSmokeSolidMenuProfile, SDD 46 section 13, Q17) in a
 * standalone -game run, for the solid restyle's before / after comparison. The run starts in the main menu
 * (USOLMenuSubsystem::ShouldStartInMenu) on the smoke settings slot (HUD effects strength 1, the default, checked),
 * with the sim date pinned (PROFILE_SIM_JD in the .cpp) so the live view behind the menus is the same every run. Four
 * passes, each at a view size set with `r.SetRes` (windowed) and checked: P0 the main menu with Settings open at
 * 1920x1080, P1 the same at 1280x720, then Play and Esc: P2 the pause menu (in flight) with Settings open at 1280x720,
 * P3 the same at 1920x1080. Each pass parks the mouse cursor in the view's top-right corner (no button hovered; the
 * keyboard focus stays where the menu put it), waits for its screen to settle (longer than every panel and trace
 * animation of SDD 7), takes a window screenshot (Saved/Screenshots/WindowsEditor/SolidMenuProfile_P<n>.png from the editor binary), then the shared
 * FSOLUiProfileCapture runs three ProfileGPU frames (the median of the game window's SlateUI pass is used) and one
 * 240-frame `stat dumpave` window each of "Draw Window And Children Time" (the whole Slate paint) and "Game UI Paint".
 * It logs one line per pass, "SolidMenuProfile RESULT P<n> <PASS|FAIL|NOBASE> key=value ...", against the pre-46c
 * baseline in the .cpp (budget: under 0.1 ms extra CPU and 0.3 ms extra GPU per screen), then a "SolidMenuProfile
 * SUMMARY" line, and quits; a watchdog quits a run that is stuck. With -SOLSmokeSolidMenuProfileTab=<n> every pass
 * shows that Settings tab (its ESOLSettingsTab position) instead of the opening one (46e follow-up: the slider fill).
 */
class SOLTEST_API FSOLSolidMenuProfileSmoke
{
public:

    // Sets up the capture with the Slate root stats
    FSOLSolidMenuProfileSmoke();

    // Caches the controller, starts the capture and the first step
    void Start(USOLMenuSubsystem& menu);

    // Runs one frame of the script; returns true once it has finished (and requested the quit)
    bool Update(USOLMenuSubsystem& menu);

private:

    // Where a capture pass is
    enum class EPassPhase : uint8
    {
        Settle,         // The screen settles
        Shot,           // A window screenshot, then a few frames for it
        Capture,        // The shared capture: ProfileGPU frames, then stat dumpave windows
        Done,
    };

    // Runs the step's action at its start
    void EnterStep(int32 step, USOLMenuSubsystem& menu);

    // Runs the step's per-frame work; returns true once the step is done
    bool TickStep(int32 step, USOLMenuSubsystem& menu, float realDeltaSeconds);

    // Runs the step's checks at its end
    void ExitStep(int32 step, USOLMenuSubsystem& menu);

    // Runs one frame of a capture pass; returns true once its result line is logged
    bool TickPass(int32 pass, USOLMenuSubsystem& menu, float realDeltaSeconds);

    // Returns whether the pass's screen is shown: its menu with Settings open, at its view size
    bool IsPassScreenShown(int32 pass, const USOLMenuSubsystem& menu) const;

    // Returns whether the main menu shows with its Settings page open
    static bool IsMainSettingsShown(const USOLMenuSubsystem& menu);

    // Returns whether the pause menu shows with its Settings tab open
    static bool IsPauseSettingsShown(const USOLMenuSubsystem& menu);

    // Returns the game viewport's size in pixels (zero without a controller)
    FIntPoint GetViewSize() const;

    // Moves the mouse cursor to the view's top-right corner, clear of every menu, so no button is hovered
    void ParkCursor() const;

    // Logs the pass's result line with its verdict against the baseline
    void LogPassResult(int32 pass, const USOLMenuSubsystem& menu);

    // Logs the SUMMARY line, stops the capture and quits
    void Finish(USOLMenuSubsystem& menu);

    // Requests a window screenshot with the UI to the screenshot folder (Saved/Screenshots/<platform>)
    static void TakeUiShot(const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    FSOLUiProfileCapture mCapture;                          // ProfileGPU and stat dumpave capture (Slate paint)
    double mLastTimeS = 0.0;                                // Real time of the last update
    double mRunTimeS = 0.0;                                 // Real time since Start (the watchdog)
    double mStepTimeS = 0.0;
    double mPhaseTimeS = 0.0;                               // Time in the pass phase
    float mStrength = -1.0f;                                // The HUD effects strength of the settings in use
    int32 mStep = 0;
    int32 mStepFrames = 0;
    int32 mPhaseFrames = 0;                                 // Frames in the pass phase
    int32 mViewFrames = 0;                                  // Frames in a row at the requested view size
    int32 mCaptureFrames = 0;                               // Capture frames this pass, and those on its screen
    int32 mScreenFrames = 0;
    int32 mPassesFailed = 0;
    int32 mPassesNoBase = 0;
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
    EPassPhase mPhase = EPassPhase::Settle;
    ESOLSettingsTab mProfileTab = ESOLSettingsTab::Count;   // The tab every pass shows (Count: the opening tab)
    bool mIsDatePinned = false;                             // Setup pinned the sim date
    bool mIsFinished = false;                               // SUMMARY logged and the quit requested
};
