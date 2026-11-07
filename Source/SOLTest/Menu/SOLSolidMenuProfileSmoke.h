/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLSettingsTabs.h"
#include "UI/SOLUiProfileCapture.h"

#include "CoreMinimal.h"

class APlayerController;
class USOLMenuSubsystem;
class USOLSettingsPageWidget;

/**
 * The profiling smoke's verdict maths (SDD 46 section 13, Q17; the VR floor of Docs/Research/VR_PERFORMANCE_TARGETS.md
 * section 5a), pure and widget-free so the tests can check the thresholds.
 */
namespace SOLSolidMenuProfile
{
    // Q17 budget per screen against the pre-46c look: extra Slate paint CPU (user, 2026-11-06: 0.2 ms, was 0.1 ms) ...
    inline constexpr double BUDGET_CPU_MS = 0.2;
    // ... and extra GPU in the Slate UI pass
    inline constexpr double BUDGET_GPU_MS = 0.3;

    // The Varjo XR-4 panel per eye; a view of more pixels than one eye is judged as both eyes (7680x3744)
    inline constexpr int32 VR_EYE_WIDTH = 3840;
    inline constexpr int32 VR_EYE_HEIGHT = 3744;
    // p99 frame, game thread and GPU lines: one eye 90% of 11.11 ms, two eyes about 5% less (second view's draws)
    inline constexpr double VR_ONE_EYE_LINE_MS = 10.0;
    inline constexpr double VR_TWO_EYES_LINE_MS = 9.5;
    // The provisional menu GPU ceilings (the 0.3 ms budget scaled by pixel count, VR doc section 7)
    inline constexpr double VR_ONE_EYE_UI_GPU_MS = 1.2;
    inline constexpr double VR_TWO_EYES_UI_GPU_MS = 2.4;

    // The whole-frame stand-in on this machine (user, 2026-11-06; VR doc "Proxy on this machine"): 4K at 100 Hz or
    // better, the p99 frame at or under 10.0 ms; informational (project-level VR work, not a menu verdict)
    inline constexpr int32 VR_PROXY_WIDTH = 3840;
    inline constexpr int32 VR_PROXY_HEIGHT = 2160;
    inline constexpr double VR_PROXY_FRAME_P99_MS = 10.0;

    // The VR floor a view is judged against
    struct FVrFloor
    {
        bool bApplies = false;      // A view with pixels
        bool bTwoEyes = false;      // More pixels than one eye
        double LineMs = 0.0;        // The p99 line of the frame, the game thread and the GPU
        double UiGpuMs = 0.0;       // The ceiling of the menu's GPU (the game window's Slate UI pass)
    };

    // Returns whether a pass's extra cost is inside the budget: under BUDGET_CPU_MS CPU and BUDGET_GPU_MS GPU (a
    // non-finite delta is not)
    SOLTEST_API bool IsWithinBudget(double deltaCpuMs, double deltaGpuMs);

    // Returns the VR floor of a view size: two eyes above one eye's pixels, else one eye; none without pixels
    SOLTEST_API FVrFloor VrFloorForView(int32 width, int32 height);

    // Returns whether a view run pass's menu share is inside the VR floor (user, 2026-11-06): the CPU delta under
    // BUDGET_CPU_MS and the menu's GPU under the floor's ceiling (a non-finite number, or a floor that does not apply,
    // is not)
    SOLTEST_API bool IsMenuShareWithinVrFloor(double deltaCpuMs, double uiGpuMs, const FVrFloor& floor);

    // Returns whether a view is the 4K proxy's size (VR_PROXY_WIDTH x VR_PROXY_HEIGHT)
    SOLTEST_API bool IsVrProxyView(int32 width, int32 height);

    // Returns whether a p99 frame time meets the 4K proxy: positive, finite and at or under VR_PROXY_FRAME_P99_MS
    SOLTEST_API bool MeetsVrProxy(double frameP99Ms);

    // (46i) Returns whether the p99 frame, game thread and GPU times meet a VR floor's line (`vr_frame=`): each
    // positive, finite and at or under floor.LineMs (a floor that does not apply has no line, so nothing meets it)
    SOLTEST_API bool MeetsVrFrameLine(double frameP99Ms, double gameP99Ms, double gpuP99Ms, const FVrFloor& floor);

    // Returns the nearest-rank percentile of the values (fraction clamped to [0, 1], NaN reads 1), sorting them in
    // place; -1 when there are none
    SOLTEST_API double Percentile(TArray<double>& values, double fraction);
}

/**
 * Verification-only cost capture of the full-screen menus (-SOLSmokeSolidMenuProfile, SDD 46 section 13, Q17) in a
 * standalone -game run, for the solid restyle's before / after comparison. The run starts in the main menu
 * (USOLMenuSubsystem::ShouldStartInMenu) on the smoke settings slot (HUD effects strength 1, the default, checked),
 * with the sim date pinned (PROFILE_SIM_JD in the .cpp) so the live view behind the menus is the same every run. The
 * default run has twelve passes, each at a view size set with `r.SetRes` (windowed) and checked: P0 the main menu with
 * Settings open at 1920x1080, P1 the same at 1280x720, then Play and Esc: P2 the pause menu (in flight) with Settings
 * open at 1280x720, P3 the same at 1920x1080, each against the pre-46c baseline in the .cpp; then (33a) the Settings
 * Key bindings tab and the Controls page of each menu at both sizes (P4 main-settings-keys and P5 main-controls at
 * 1920x1080, P6 main-controls and P7 main-settings-keys at 1280x720, P8 pause-settings-keys and P9 pause-controls at
 * 1280x720, P10 pause-controls and P11 pause-settings-keys at 1920x1080), against the 33a baseline taken before their
 * key cap restyle. With -ResX / -ResY other than 1920x1080 it is a view run (46h, the VR floor): nine passes at that
 * size (the game window's size limits lifted so it may be larger than the desktop): the main menu alone, with Settings,
 * Settings on Key bindings, Controls, About and Demo, then the pause menu alone, with Settings and with Info; the
 * verdict judges only the menu's share (user, 2026-11-06): the CPU against the 1920x1080 baseline of its menu and the
 * menu's GPU against the VR floor's ceiling. The p99 frame, game thread and GPU are logged against the XR-4 line
 * (`vr_frame=`, informational) and, at 3840x2160, the p99 frame against the 4K / 100 Hz proxy (`proxy=`, informational:
 * project-level VR work). Each pass parks the mouse cursor in the view's top-right corner (no button hovered), waits
 * for its screen to settle (longer than every panel and trace animation of SDD 7), takes a window screenshot
 * (Saved/Screenshots/WindowsEditor/SolidMenuProfile_P<n>.png from the editor binary), then the shared
 * FSOLUiProfileCapture runs three ProfileGPU frames (the median of the game window's SlateUI pass is used) and one
 * 240-frame `stat dumpave` window each of "Draw Window And Children Time" (the whole Slate paint) and "Game UI Paint",
 * then PROFILE_FRAME_SAMPLES plain frames give the p99 of the frame, the game thread and the GPU. It logs one line per
 * pass, "SolidMenuProfile RESULT P<n> <PASS|FAIL|NOBASE> key=value ...", then a "SolidMenuProfile SUMMARY" line, and
 * quits; a watchdog quits a run that is stuck. With -SOLSmokeSolidMenuProfileTab=<n> every Settings pass shows that tab
 * (its ESOLSettingsTab position) instead of its own (46e follow-up: the slider fill).
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

    // What a step of the script does
    enum class EStepKind : uint8
    {
        Setup,          // The main menu shows; the sim date pinned, the effects strength read
        View,           // r.SetRes to the step's view size (windowed), until the view has that size
        Screen,         // The step's pass screen opens (a main menu page, a pause tab)
        Pass,           // The step's capture pass
        Play,           // The page closed, Play: the ship spawns and flight settles
        Pause,          // Esc: the pause menu
        Quit,           // The SUMMARY line, then quit
    };

    // One step of the script
    struct FStep
    {
        EStepKind Kind = EStepKind::Setup;
        int32 Pass = INDEX_NONE;                            // The pass a Screen or Pass step is for
        FIntPoint View = FIntPoint::ZeroValue;              // The view size a View step asks for
    };

    // One pass of this run: its screen, view size and baseline
    struct FPass
    {
        const TCHAR* Screen = TEXT("");                     // The screen's name in the log line
        bool bPause = false;                                // The pause menu (else the main menu)
        ESOLMainMenuPage MainPage = ESOLMainMenuPage::None; // The main menu's page
        SOLMenuState::EMenuTab PauseTab = SOLMenuState::EMenuTab::Resume;  // The pause menu's tab
        ESOLSettingsTab SettingsTab = ESOLSettingsTab::Count;   // The Settings tab shown (Count: the opening tab)
        FIntPoint View = FIntPoint::ZeroValue;
        double BaseCpuMs = 0.0;                             // The CPU and GPU baseline (zero: none)
        double BaseGpuMs = 0.0;
    };

    // Where a capture pass is
    enum class EPassPhase : uint8
    {
        Settle,         // The screen settles
        Shot,           // A window screenshot, then a few frames for it
        Capture,        // The shared capture: ProfileGPU frames, then stat dumpave windows
        Frames,         // Plain frames for the p99 frame, game thread and GPU times
        Done,
    };

    // Builds the run's passes and steps: the default twelve, or the view run's nine at the -ResX / -ResY size
    void BuildScript();

    // Runs the step's action at its start
    void EnterStep(int32 step, USOLMenuSubsystem& menu);

    // Runs the step's per-frame work; returns true once the step is done
    bool TickStep(int32 step, USOLMenuSubsystem& menu, float realDeltaSeconds);

    // Runs the step's checks at its end
    void ExitStep(int32 step, USOLMenuSubsystem& menu);

    // Runs one frame of a capture pass; returns true once its result line is logged
    bool TickPass(int32 pass, USOLMenuSubsystem& menu, float realDeltaSeconds);

    // Returns the Settings page a pass shows, or null when it shows none
    USOLSettingsPageWidget* GetPassSettingsPage(int32 pass, const USOLMenuSubsystem& menu) const;

    // Returns whether the pass's menu shows its page or tab (and Settings tab), whatever the view size
    bool IsPassMenuShown(int32 pass, const USOLMenuSubsystem& menu) const;

    // Returns whether the pass's screen is shown: its menu, page and tab, at its view size
    bool IsPassScreenShown(int32 pass, const USOLMenuSubsystem& menu) const;

    // Returns the game viewport's size in pixels (zero without a controller)
    FIntPoint GetViewSize() const;

    // Lets the game window grow past the desktop (the view run's sizes are larger than any monitor here)
    static void LiftWindowSizeLimits();

    // Moves the mouse cursor to the view's top-right corner, clear of every menu, so no button is hovered
    void ParkCursor() const;

    // Logs the pass's result line with its verdict against the baseline (and the VR floor in a view run)
    void LogPassResult(int32 pass);

    // Logs the SUMMARY line, stops the capture and quits
    void Finish(USOLMenuSubsystem& menu);

    // Requests a window screenshot with the UI to the screenshot folder (Saved/Screenshots/<platform>)
    static void TakeUiShot(const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    FSOLUiProfileCapture mCapture;                          // ProfileGPU and stat dumpave capture (Slate paint)
    TArray<FPass> mPasses;
    TArray<FStep> mSteps;
    TArray<double> mFrameMs;                                // The pass's plain frames: real frame time, ...
    TArray<double> mGameMs;                                 // ... game thread time ...
    TArray<double> mGpuMs;                                  // ... and GPU frame time, ms
    FIntPoint mRunView = FIntPoint::ZeroValue;              // The view run's size (zero: the default run)
    double mLastTimeS = 0.0;                                // Real time of the last update
    double mRunTimeS = 0.0;                                 // Real time since Start (the watchdog)
    double mWatchdogS = 0.0;                                // The run's watchdog, by its pass count
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
    int32 mPassesLogged = 0;                                // Passes with a RESULT line (an abort fails the rest)
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
    EPassPhase mPhase = EPassPhase::Settle;
    ESOLSettingsTab mProfileTab = ESOLSettingsTab::Count;   // The tab every Settings pass shows (Count: its own)
    bool mIsDatePinned = false;                             // Setup pinned the sim date
    bool mIsFinished = false;                               // SUMMARY logged and the quit requested
};
