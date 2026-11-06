/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuProfileSmoke.h"

#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLPauseMenuWidget.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    constexpr int32 PROFILE_SHOT_FRAMES = 15;                   // Frames after the screenshot request
    constexpr int32 PROFILE_MIN_FRAMES = 5;                     // Every step lasts at least this many frames
    constexpr int32 PROFILE_VIEW_STABLE_FRAMES = 10;            // Frames in a row at the requested view size
    constexpr int32 PROFILE_CURSOR_INSET_PX = 8;                // The parked cursor, this far in from the top right
    constexpr int32 PROFILE_FRAME_SKIP = 5;                     // Frames after the stat dumps before the samples
    constexpr int32 PROFILE_FRAME_SAMPLES = 240;                // Plain frames per pass for the p99 times
    constexpr float PROFILE_MAX_WINDOW_PX = 16384.0f;           // The view run's lifted window size limit
    constexpr double PROFILE_SETUP_S = 3.0;                     // The main menu's first frames (and shader loads)
    constexpr double PROFILE_SETUP_TIMEOUT_S = 30.0;
    constexpr double PROFILE_STEP_S = 0.5;                      // A state change step lasts at least this once done
    constexpr double PROFILE_STEP_TIMEOUT_S = 5.0;
    constexpr double PROFILE_VIEW_TIMEOUT_S = 8.0;
    constexpr double PROFILE_FLIGHT_SETTLE_S = 3.0;             // Flight after Play: the ship spawn and the HUD
    constexpr double PROFILE_FLIGHT_TIMEOUT_S = 20.0;
    constexpr double PROFILE_SETTLE_S = 1.5;                    // A pass waits this long on its screen first
    constexpr double PROFILE_QUIT_S = 1.0;
    constexpr double PROFILE_WATCHDOG_BASE_S = 60.0;            // The watchdog: this plus PROFILE_WATCHDOG_PASS_S a pass
    constexpr double PROFILE_WATCHDOG_PASS_S = 45.0;
    constexpr double PROFILE_WATCHDOG_VIEW_PASS_S = 180.0;      // ... a view run pass (two eyes: 50-130 ms frames)
    constexpr double PROFILE_P99 = 0.99;
    constexpr double PROFILE_RANK_EPSILON = 1.0e-9;             // Percentile: a rank's rounding slack (far below 1)
    constexpr float PROFILE_STRENGTH = 1.0f;                    // The effects strength the numbers are taken at
    constexpr float PROFILE_STRENGTH_TOLERANCE = 1.0e-3f;

    // The settle outlasts every open animation of SDD 7 (panel open, then the trace power-up)
    static_assert(PROFILE_SETTLE_S > SOL::SolidMenu::PANEL_OPEN_S + SOL::SolidMenu::TRACE_POWER_DELAY_S
        + SOL::SolidMenu::TRACE_POWER_S + SOL::SolidMenu::PAUSE_SLIDE_S, "The settle must outlast the open animations");

    // The fixed scene behind the menus: the sim date of the other profiling smokes (JD 2461348.5 = 2026-11-04 00:00
    // UTC), so the main menu's view of Earth and the flight under the pause menu are the same every run
    constexpr double PROFILE_SIM_JD = 2461348.5;

    // The default run's view sizes; -ResX / -ResY at the large one keep the default run
    const FIntPoint PROFILE_VIEW_LARGE(1920, 1080);
    const FIntPoint PROFILE_VIEW_SMALL(1280, 720);

    // Pre-46c baseline (SDD 46 section 13, 46c amendment): cpu_slate (Draw Window And Children Time) and gpu_ui (the
    // game window's SlateUI pass), ms, the medians of three runs at 816da1a plus the uncommitted 46a review and 46c
    // step 0 files (today's flat look, no rendering change), on an i9-9960X / RTX 2080 Ti with the Development editor
    // binary in -game, 2026-11-04: main menu with Settings at 1920x1080 and 1280x720, pause menu with Settings at
    // 1280x720 and 1920x1080
    constexpr double PROFILE_BASE_MAIN_LARGE_CPU_MS = 0.423;
    constexpr double PROFILE_BASE_MAIN_SMALL_CPU_MS = 0.444;
    constexpr double PROFILE_BASE_PAUSE_SMALL_CPU_MS = 0.430;
    constexpr double PROFILE_BASE_PAUSE_LARGE_CPU_MS = 0.429;
    constexpr double PROFILE_BASE_MAIN_LARGE_GPU_MS = 0.046;
    constexpr double PROFILE_BASE_MAIN_SMALL_GPU_MS = 0.027;
    constexpr double PROFILE_BASE_PAUSE_SMALL_GPU_MS = 0.037;
    constexpr double PROFILE_BASE_PAUSE_LARGE_GPU_MS = 0.075;

    // The game-thread Slate stats, one stat dumpave window each: Draw Window And Children Time (the whole Slate paint)
    // and Game UI Paint (inside it, for reference)
    const TCHAR* const PROFILE_CPU_ROOT_STATS[] = { TEXT("STAT_SlateDrawWindowTime"), TEXT("STAT_ViewportPaintTime") };
    // The solid menu material draws summed into gpu_solid (M_SOLSolidHull, its instances and MIDs; none before 46c)
    const TCHAR* const PROFILE_SOLID_MATERIAL_TOKEN = TEXT("SOLSolid");

    // One screen of a run: its name, menu, main menu page or pause tab, and Settings tab (Count: the opening tab); a
    // main-menu screen's PauseTab is Resume, never read (the pass reads it only for the pause menu)
    struct FProfileScreen
    {
        const TCHAR* Name;
        bool bPause;
        ESOLMainMenuPage MainPage;
        SOLMenuState::EMenuTab PauseTab;
        ESOLSettingsTab SettingsTab;
    };

    // The view run's screens (46h): the main menu alone and with each page that nests recesses, then the pause menu
    // alone, with Settings and with Info; the main menu's first (Play comes between)
    const FProfileScreen PROFILE_VIEW_SCREENS[] = {
        { TEXT("main"), false, ESOLMainMenuPage::None, SOLMenuState::EMenuTab::Resume, ESOLSettingsTab::Count },
        { TEXT("main-settings"), false, ESOLMainMenuPage::Settings, SOLMenuState::EMenuTab::Resume,
            ESOLSettingsTab::Count },
        { TEXT("main-keys"), false, ESOLMainMenuPage::Settings, SOLMenuState::EMenuTab::Resume,
            ESOLSettingsTab::KeyBindings },
        { TEXT("main-controls"), false, ESOLMainMenuPage::Controls, SOLMenuState::EMenuTab::Resume,
            ESOLSettingsTab::Count },
        { TEXT("main-about"), false, ESOLMainMenuPage::About, SOLMenuState::EMenuTab::Resume, ESOLSettingsTab::Count },
        { TEXT("main-demo"), false, ESOLMainMenuPage::Demo, SOLMenuState::EMenuTab::Resume, ESOLSettingsTab::Count },
        { TEXT("pause"), true, ESOLMainMenuPage::None, SOLMenuState::EMenuTab::Resume, ESOLSettingsTab::Count },
        { TEXT("pause-settings"), true, ESOLMainMenuPage::None, SOLMenuState::EMenuTab::Settings,
            ESOLSettingsTab::Count },
        { TEXT("pause-info"), true, ESOLMainMenuPage::None, SOLMenuState::EMenuTab::Info, ESOLSettingsTab::Count },
    };

    // 46i: the checks a whole run logs (Finish fails a run that logged another number: a skipped check). Default: the
    // setup's two, three views, two screens, Play and Esc; a view run: the setup's two, its view, Play, Esc and one
    // per screen
    constexpr int32 PROFILE_EXPECTED_CHECKS_DEFAULT = 9;
    constexpr int32 PROFILE_EXPECTED_CHECKS_VIEW = 14;
    static_assert(PROFILE_EXPECTED_CHECKS_VIEW == 5 + static_cast<int32>(UE_ARRAY_COUNT(PROFILE_VIEW_SCREENS)),
        "a view run checks the setup (2), its view, Play, Esc and each screen");
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a pass's extra cost is inside the budget: under BUDGET_CPU_MS CPU and BUDGET_GPU_MS GPU
bool SOLSolidMenuProfile::IsWithinBudget(const double deltaCpuMs, const double deltaGpuMs)
{
    return FMath::IsFinite(deltaCpuMs) && FMath::IsFinite(deltaGpuMs) && deltaCpuMs < BUDGET_CPU_MS
        && deltaGpuMs < BUDGET_GPU_MS;
}

//////////////////////////////////////////////////////////////////////////
// Returns the VR floor of a view size: two eyes above one eye's pixels, else one eye; none without pixels
SOLSolidMenuProfile::FVrFloor SOLSolidMenuProfile::VrFloorForView(const int32 width, const int32 height)
{
    FVrFloor floor;
    if (width <= 0 || height <= 0)
    {
        return floor;
    }
    const int64 pixels = static_cast<int64>(width) * height;
    floor.bApplies = true;
    floor.bTwoEyes = pixels > static_cast<int64>(VR_EYE_WIDTH) * VR_EYE_HEIGHT;
    floor.LineMs = floor.bTwoEyes ? VR_TWO_EYES_LINE_MS : VR_ONE_EYE_LINE_MS;
    floor.UiGpuMs = floor.bTwoEyes ? VR_TWO_EYES_UI_GPU_MS : VR_ONE_EYE_UI_GPU_MS;
    return floor;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a view run pass's menu share is inside the VR floor: CPU delta and the menu's GPU under their lines
bool SOLSolidMenuProfile::IsMenuShareWithinVrFloor(const double deltaCpuMs, const double uiGpuMs,
    const FVrFloor& floor)
{
    return floor.bApplies && FMath::IsFinite(deltaCpuMs) && FMath::IsFinite(uiGpuMs) && deltaCpuMs < BUDGET_CPU_MS
        && uiGpuMs < floor.UiGpuMs;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a view is the 4K proxy's size
bool SOLSolidMenuProfile::IsVrProxyView(const int32 width, const int32 height)
{
    return width == VR_PROXY_WIDTH && height == VR_PROXY_HEIGHT;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a p99 frame time meets the 4K proxy: positive, finite and at or under the line
bool SOLSolidMenuProfile::MeetsVrProxy(const double frameP99Ms)
{
    return FMath::IsFinite(frameP99Ms) && frameP99Ms > 0.0 && frameP99Ms <= VR_PROXY_FRAME_P99_MS;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the p99 frame, game thread and GPU times are each positive, finite and at or under the floor's line
bool SOLSolidMenuProfile::MeetsVrFrameLine(const double frameP99Ms, const double gameP99Ms, const double gpuP99Ms,
    const FVrFloor& floor)
{
    // One time against the line
    const auto meets = [&floor](const double ms)
    {
        return FMath::IsFinite(ms) && ms > 0.0 && ms <= floor.LineMs;
    };
    return floor.bApplies && meets(frameP99Ms) && meets(gameP99Ms) && meets(gpuP99Ms);
}

//////////////////////////////////////////////////////////////////////////
// Returns the nearest-rank percentile of the values (fraction clamped to [0, 1], NaN reads 1), sorting them in place
double SOLSolidMenuProfile::Percentile(TArray<double>& values, const double fraction)
{
    if (values.IsEmpty())
    {
        return -1.0;
    }
    values.Sort();
    const double clamped = FMath::IsNaN(fraction) ? 1.0 : FMath::Clamp(fraction, 0.0, 1.0);

    // The rank is ceil(fraction x N); the product's rounding error (0.07 x 100 = 7.000000000000001) must not push an
    // exact rank to the next one
    const int32 rank = FMath::CeilToInt32(clamped * values.Num() - PROFILE_RANK_EPSILON);
    return values[FMath::Clamp(rank - 1, 0, values.Num() - 1)];
}

//////////////////////////////////////////////////////////////////////////
// Sets up the capture with the Slate root stats
FSOLSolidMenuProfileSmoke::FSOLSolidMenuProfileSmoke()
    : mCapture(PROFILE_CPU_ROOT_STATS, PROFILE_SOLID_MATERIAL_TOKEN)
{
    mFrameMs.Reserve(PROFILE_FRAME_SAMPLES);
    mGameMs.Reserve(PROFILE_FRAME_SAMPLES);
    mGpuMs.Reserve(PROFILE_FRAME_SAMPLES);
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, starts the capture and the first step
void FSOLSolidMenuProfileSmoke::Start(USOLMenuSubsystem& menu)
{
    mController = menu.GetPlayerController();
    mLastTimeS = FPlatformTime::Seconds();
    int32 tabIndex = INDEX_NONE;
    mProfileTab = FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_SOLID_MENU_PROFILE_TAB, tabIndex)
        ? SOLSettingsTabs::TabFromIndex(tabIndex) : ESOLSettingsTab::Count;
    BuildScript();
    mWatchdogS = PROFILE_WATCHDOG_BASE_S + (mRunView == FIntPoint::ZeroValue ? PROFILE_WATCHDOG_PASS_S
        : PROFILE_WATCHDOG_VIEW_PASS_S) * mPasses.Num();

    // The captures need a standalone -game process: in the editor the editor's own work pollutes the numbers
    if (GIsEditor)
    {
        UE_LOG(LogSOL, Warning, TEXT("SolidMenuProfile: -%s needs -game (standalone); the numbers are not comparable"),
            SOL::CommandLine::SMOKE_SOLID_MENU_PROFILE);
    }
    mCapture.Start(menu.GetWorld());
    UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile: scripted menu cost capture starting (%s run %dx%d, %d passes, %d "
        "ProfileGPU frames, %d stat dumpave windows of %d frames and %d plain frames each)"), mRunView
        == FIntPoint::ZeroValue ? TEXT("default") : TEXT("view"), mRunView.X, mRunView.Y, mPasses.Num(),
        FSOLUiProfileCapture::GPU_CAPTURES, static_cast<int32>(UE_ARRAY_COUNT(PROFILE_CPU_ROOT_STATS)),
        FSOLUiProfileCapture::DUMP_FRAMES, PROFILE_FRAME_SAMPLES);
    mStep = 0;
    EnterStep(mStep, menu);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished (and requested the quit)
bool FSOLSolidMenuProfileSmoke::Update(USOLMenuSubsystem& menu)
{
    if (mIsFinished || mStep >= mSteps.Num())
    {
        return true;
    }
    const double nowS = FPlatformTime::Seconds();
    const float realDeltaSeconds = static_cast<float>(nowS - mLastTimeS);
    mLastTimeS = nowS;
    mRunTimeS += realDeltaSeconds;
    if (!mController.IsValid())
    {
        mController = menu.GetPlayerController();
    }

    // A stuck run (a screen that never opens, a dump that never prints) still ends, with every pass left FAIL
    if (mRunTimeS >= mWatchdogS || !mController.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SolidMenuProfile: %s at step %d after %.1f s; aborting"), mController.IsValid()
            ? TEXT("watchdog") : TEXT("no player controller"), mStep, mRunTimeS);
        Check(TEXT("z-watchdog"), false, FString::Printf(TEXT("%s at step %d after %.1f s, %d of %d passes logged"),
            mController.IsValid() ? TEXT("stuck") : TEXT("no player controller"), mStep, mRunTimeS, mPassesLogged,
            mPasses.Num()));
        mPassesFailed += FMath::Max(0, mPasses.Num() - mPassesLogged);
        Finish(menu);
        return true;
    }

    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    if (!TickStep(mStep, menu, realDeltaSeconds) || mStepFrames < PROFILE_MIN_FRAMES)
    {
        return false;
    }
    ExitStep(mStep, menu);
    ++mStep;
    mStepTimeS = 0.0;
    mStepFrames = 0;
    if (mIsFinished || mStep >= mSteps.Num())
    {
        return true;
    }
    EnterStep(mStep, menu);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Builds the run's passes and steps: the default four, or the view run's nine at the -ResX / -ResY size
void FSOLSolidMenuProfileSmoke::BuildScript()
{
    int32 resX = 0;
    int32 resY = 0;
    const bool bRes = FParse::Value(FCommandLine::Get(), TEXT("ResX="), resX)
        && FParse::Value(FCommandLine::Get(), TEXT("ResY="), resY) && resX > 0 && resY > 0;
    mRunView = bRes && FIntPoint(resX, resY) != PROFILE_VIEW_LARGE ? FIntPoint(resX, resY) : FIntPoint::ZeroValue;
    mPasses.Reset();
    mSteps.Reset();

    // Appends one step
    const auto addStep = [this](const EStepKind kind, const int32 pass, const FIntPoint view)
    {
        FStep step;
        step.Kind = kind;
        step.Pass = pass;
        step.View = view;
        mSteps.Add(step);
    };
    // Appends one pass and returns its index
    const auto addPass = [this](const TCHAR* name, const bool bPause, const ESOLMainMenuPage mainPage,
        const SOLMenuState::EMenuTab pauseTab, const ESOLSettingsTab settingsTab, const FIntPoint view,
        const double baseCpuMs, const double baseGpuMs)
    {
        FPass pass;
        pass.Screen = name;
        pass.bPause = bPause;
        pass.MainPage = mainPage;
        pass.PauseTab = pauseTab;
        pass.SettingsTab = settingsTab;
        pass.View = view;
        pass.BaseCpuMs = baseCpuMs;
        pass.BaseGpuMs = baseGpuMs;
        return mPasses.Add(pass);
    };
    addStep(EStepKind::Setup, INDEX_NONE, FIntPoint::ZeroValue);

    // The default run: Settings in both menus at both sizes, against the per-pass baseline
    if (mRunView == FIntPoint::ZeroValue)
    {
        const ESOLMainMenuPage settings = ESOLMainMenuPage::Settings;
        const SOLMenuState::EMenuTab resume = SOLMenuState::EMenuTab::Resume;
        const SOLMenuState::EMenuTab pauseSettings = SOLMenuState::EMenuTab::Settings;
        const int32 p0 = addPass(TEXT("main-settings"), false, settings, resume, ESOLSettingsTab::Count,
            PROFILE_VIEW_LARGE, PROFILE_BASE_MAIN_LARGE_CPU_MS, PROFILE_BASE_MAIN_LARGE_GPU_MS);
        const int32 p1 = addPass(TEXT("main-settings"), false, settings, resume, ESOLSettingsTab::Count,
            PROFILE_VIEW_SMALL, PROFILE_BASE_MAIN_SMALL_CPU_MS, PROFILE_BASE_MAIN_SMALL_GPU_MS);
        const int32 p2 = addPass(TEXT("pause-settings"), true, ESOLMainMenuPage::None, pauseSettings,
            ESOLSettingsTab::Count, PROFILE_VIEW_SMALL, PROFILE_BASE_PAUSE_SMALL_CPU_MS,
            PROFILE_BASE_PAUSE_SMALL_GPU_MS);
        const int32 p3 = addPass(TEXT("pause-settings"), true, ESOLMainMenuPage::None, pauseSettings,
            ESOLSettingsTab::Count, PROFILE_VIEW_LARGE, PROFILE_BASE_PAUSE_LARGE_CPU_MS,
            PROFILE_BASE_PAUSE_LARGE_GPU_MS);
        addStep(EStepKind::View, INDEX_NONE, PROFILE_VIEW_LARGE);
        addStep(EStepKind::Screen, p0, FIntPoint::ZeroValue);
        addStep(EStepKind::Pass, p0, FIntPoint::ZeroValue);
        addStep(EStepKind::View, INDEX_NONE, PROFILE_VIEW_SMALL);
        addStep(EStepKind::Pass, p1, FIntPoint::ZeroValue);
        addStep(EStepKind::Play, INDEX_NONE, FIntPoint::ZeroValue);
        addStep(EStepKind::Pause, INDEX_NONE, FIntPoint::ZeroValue);
        addStep(EStepKind::Screen, p2, FIntPoint::ZeroValue);
        addStep(EStepKind::Pass, p2, FIntPoint::ZeroValue);
        addStep(EStepKind::View, INDEX_NONE, PROFILE_VIEW_LARGE);
        addStep(EStepKind::Pass, p3, FIntPoint::ZeroValue);
        addStep(EStepKind::Quit, INDEX_NONE, FIntPoint::ZeroValue);
        return;
    }

    // The view run: every screen at the one size; the CPU against its menu's 1920x1080 baseline (the Slate paint
    // does not scale with pixels), no GPU baseline at this size (the menu's GPU is judged against the VR floor)
    addStep(EStepKind::View, INDEX_NONE, mRunView);
    bool bPaused = false;
    for (const FProfileScreen& screen : PROFILE_VIEW_SCREENS)
    {
        if (screen.bPause && !bPaused)
        {
            addStep(EStepKind::Play, INDEX_NONE, FIntPoint::ZeroValue);
            addStep(EStepKind::Pause, INDEX_NONE, FIntPoint::ZeroValue);
            bPaused = true;
        }
        const int32 pass = addPass(screen.Name, screen.bPause, screen.MainPage, screen.PauseTab, screen.SettingsTab,
            mRunView, screen.bPause ? PROFILE_BASE_PAUSE_LARGE_CPU_MS : PROFILE_BASE_MAIN_LARGE_CPU_MS, 0.0);
        addStep(EStepKind::Screen, pass, FIntPoint::ZeroValue);
        addStep(EStepKind::Pass, pass, FIntPoint::ZeroValue);
    }
    addStep(EStepKind::Quit, INDEX_NONE, FIntPoint::ZeroValue);
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLSolidMenuProfileSmoke::EnterStep(const int32 step, USOLMenuSubsystem& menu)
{
    UWorld* world = menu.GetWorld();
    const FStep& current = mSteps[step];
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    switch (current.Kind)
    {
    case EStepKind::Setup:
    {
        // The pinned date (the main menu's camera follows the start body to it)
        USOLSimClockSubsystem* clock = world->GetSubsystem<USOLSimClockSubsystem>();
        if (clock != nullptr)
        {
            clock->SetJulianDate(PROFILE_SIM_JD);
        }
        mIsDatePinned = clock != nullptr;
        UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile: sim date pinned to JD %.5f%s"), PROFILE_SIM_JD,
            mIsDatePinned ? TEXT("") : TEXT(" FAILED (no sim clock)"));
        break;
    }
    case EStepKind::View:
        mViewFrames = 0;
        if (mRunView != FIntPoint::ZeroValue)
        {
            LiftWindowSizeLimits();
        }
        FSOLUiProfileCapture::RunCommand(*FString::Printf(TEXT("r.SetRes %dx%dw"), current.View.X, current.View.Y),
            world);
        break;
    case EStepKind::Screen:
    {
        // The pass's page (main menu) or tab (pause menu), unless it already shows
        const FPass& pass = mPasses[current.Pass];
        if (!pass.bPause && mainMenu != nullptr && mainMenu->GetPage() != pass.MainPage)
        {
            mainMenu->ShowPage(pass.MainPage);
        }
        if (pass.bPause && menu.GetState().Tab != pass.PauseTab)
        {
            menu.SelectTab(pass.PauseTab);
        }
        break;
    }
    case EStepKind::Pass:
    {
        // A pass starts from a clean slate, on its Settings tab (the pass's, -SOLSmokeSolidMenuProfileTab, else the
        // page's opening tab)
        mPhase = EPassPhase::Settle;
        mPhaseTimeS = 0.0;
        mPhaseFrames = 0;
        mCaptureFrames = 0;
        mScreenFrames = 0;
        const FPass& pass = mPasses[current.Pass];
        USOLSettingsPageWidget* page = GetPassSettingsPage(current.Pass, menu);
        const ESOLSettingsTab tab = SOLSettingsTabs::IsValidTab(pass.SettingsTab) ? pass.SettingsTab : mProfileTab;
        if (page != nullptr && SOLSettingsTabs::IsValidTab(tab))
        {
            page->SelectTab(tab);
        }
        UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile: pass P%d %s %dx%d tab %d starting"), current.Pass, pass.Screen,
            pass.View.X, pass.View.Y, page != nullptr ? static_cast<int32>(page->GetSelectedTab()) : -1);
        break;
    }
    case EStepKind::Play:
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::None);
        }
        menu.RequestPlay();
        break;
    case EStepKind::Pause:
        menu.RequestEscape();
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's per-frame work; returns true once the step is done
bool FSOLSolidMenuProfileSmoke::TickStep(const int32 step, USOLMenuSubsystem& menu, const float realDeltaSeconds)
{
    const FStep& current = mSteps[step];
    const SOLMenuState::EMenuMode mode = menu.GetState().Mode;
    switch (current.Kind)
    {
    case EStepKind::Setup:
    {
        const bool bShown = mode == SOLMenuState::EMenuMode::MainMenu && menu.GetMainMenu() != nullptr
            && menu.GetMainMenu()->IsInViewport();
        return (bShown && mStepTimeS >= PROFILE_SETUP_S) || mStepTimeS >= PROFILE_SETUP_TIMEOUT_S;
    }
    case EStepKind::View:
        mViewFrames = GetViewSize() == current.View ? mViewFrames + 1 : 0;
        return (mViewFrames >= PROFILE_VIEW_STABLE_FRAMES && mStepTimeS >= PROFILE_STEP_S)
            || mStepTimeS >= PROFILE_VIEW_TIMEOUT_S;
    case EStepKind::Screen:
        return (IsPassMenuShown(current.Pass, menu) && mStepTimeS >= PROFILE_STEP_S)
            || mStepTimeS >= PROFILE_STEP_TIMEOUT_S;
    case EStepKind::Pass:
        return TickPass(current.Pass, menu, realDeltaSeconds);
    case EStepKind::Play:
    {
        const bool bFlying = mode == SOLMenuState::EMenuMode::Playing && Cast<ASOLShipPawn>(mController->GetPawn())
            != nullptr;
        return (bFlying && mStepTimeS >= PROFILE_FLIGHT_SETTLE_S) || mStepTimeS >= PROFILE_FLIGHT_TIMEOUT_S;
    }
    case EStepKind::Pause:
    {
        const bool bPaused = mode == SOLMenuState::EMenuMode::Paused && menu.GetPauseMenu() != nullptr
            && menu.GetPauseMenu()->IsInViewport();
        return (bPaused && mStepTimeS >= PROFILE_STEP_S) || mStepTimeS >= PROFILE_STEP_TIMEOUT_S;
    }
    case EStepKind::Quit:
        return mStepTimeS >= PROFILE_QUIT_S;
    }
    checkNoEntry();     // Every step kind returns above
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLSolidMenuProfileSmoke::ExitStep(const int32 step, USOLMenuSubsystem& menu)
{
    const FStep& current = mSteps[step];
    const FIntPoint size = GetViewSize();
    const SOLMenuState::EMenuMode mode = menu.GetState().Mode;
    switch (current.Kind)
    {
    case EStepKind::Setup:
    {
        // The settings in use: the smoke slot, cleared at boot, so the default strength
        const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
        mStrength = settings != nullptr ? settings->GetSavedValues().HudEffectsStrength : -1.0f;
        Check(TEXT("a-main-menu"), mode == SOLMenuState::EMenuMode::MainMenu && menu.GetMainMenu() != nullptr
            && menu.GetMainMenu()->IsInViewport() && mIsDatePinned, FString::Printf(
            TEXT("main menu %s, date %s, view %dx%d"), mode == SOLMenuState::EMenuMode::MainMenu ? TEXT("shown")
            : TEXT("NOT SHOWN"), mIsDatePinned ? TEXT("pinned") : TEXT("NOT PINNED"), size.X, size.Y));
        Check(TEXT("b-strength"), FMath::IsNearlyEqual(mStrength, PROFILE_STRENGTH, PROFILE_STRENGTH_TOLERANCE),
            FString::Printf(TEXT("HUD effects strength %.2f (want %.2f)"), mStrength, PROFILE_STRENGTH));
        break;
    }
    case EStepKind::View:
        Check(*FString::Printf(TEXT("view-%dx%d"), current.View.X, current.View.Y), size == current.View,
            FString::Printf(TEXT("r.SetRes %dx%d: view %dx%d"), current.View.X, current.View.Y, size.X, size.Y));
        break;
    case EStepKind::Screen:
    {
        const bool bShown = IsPassMenuShown(current.Pass, menu);
        Check(*FString::Printf(TEXT("screen-%s"), mPasses[current.Pass].Screen), bShown,
            FString::Printf(TEXT("%s %s"), mPasses[current.Pass].Screen, bShown ? TEXT("shown") : TEXT("NOT SHOWN")));
        break;
    }
    case EStepKind::Play:
        Check(TEXT("d-flight"), mode == SOLMenuState::EMenuMode::Playing && Cast<ASOLShipPawn>(mController->GetPawn())
            != nullptr, FString::Printf(TEXT("Play: %s"), mode == SOLMenuState::EMenuMode::Playing ? TEXT("flying")
            : TEXT("NOT FLYING")));
        break;
    case EStepKind::Pause:
        Check(TEXT("e-pause"), mode == SOLMenuState::EMenuMode::Paused, FString::Printf(TEXT("Esc: %s"), mode
            == SOLMenuState::EMenuMode::Paused ? TEXT("paused") : TEXT("NOT PAUSED")));
        break;
    case EStepKind::Quit:
        Finish(menu);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of a capture pass; returns true once its result line is logged
bool FSOLSolidMenuProfileSmoke::TickPass(const int32 pass, USOLMenuSubsystem& menu, const float realDeltaSeconds)
{
    mPhaseTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    if ((mPhase == EPassPhase::Capture && mCapture.IsCapturing()) || mPhase == EPassPhase::Frames)
    {
        ++mCaptureFrames;
        mScreenFrames += IsPassScreenShown(pass, menu) ? 1 : 0;
    }
    // Moves the pass to a phase with its clock and frame count at zero
    const auto nextPhase = [this](const EPassPhase phase)
    {
        mPhase = phase;
        mPhaseTimeS = 0.0;
        mPhaseFrames = 0;
    };
    switch (mPhase)
    {
    case EPassPhase::Settle:
        if (mPhaseFrames == 1)
        {
            ParkCursor();
        }
        if (mPhaseTimeS >= PROFILE_SETTLE_S)
        {
            TakeUiShot(*FString::Printf(TEXT("P%d"), pass));
            nextPhase(EPassPhase::Shot);
        }
        return false;
    case EPassPhase::Shot:
        if (mPhaseFrames >= PROFILE_SHOT_FRAMES)
        {
            ParkCursor();
            mCapture.BeginPass();
            nextPhase(EPassPhase::Capture);
        }
        return false;
    case EPassPhase::Capture:
    {
        // Nothing to wait for between captures: the screen is static once settled
        const auto canStart = []()
        {
            return true;
        };
        if (mCapture.TickPass(menu.GetWorld(), realDeltaSeconds, canStart))
        {
            mFrameMs.Reset();
            mGameMs.Reset();
            mGpuMs.Reset();
            nextPhase(EPassPhase::Frames);
        }
        return false;
    }
    case EPassPhase::Frames:
        // Plain frames (no capture command running) after a few to let the last dump's hitch pass: this frame's
        // real time, the last game thread and GPU frame times
        if (mPhaseFrames > PROFILE_FRAME_SKIP)
        {
            mFrameMs.Add(1000.0 * realDeltaSeconds);
            mGameMs.Add(FPlatformTime::ToMilliseconds64(GGameThreadTime));
            mGpuMs.Add(FPlatformTime::ToMilliseconds64(RHIGetGPUFrameCycles()));
        }
        if (mFrameMs.Num() >= PROFILE_FRAME_SAMPLES)
        {
            LogPassResult(pass);
            nextPhase(EPassPhase::Done);
            return true;
        }
        return false;
    default:
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the Settings page a pass shows, or null when it shows none
USOLSettingsPageWidget* FSOLSolidMenuProfileSmoke::GetPassSettingsPage(const int32 pass,
    const USOLMenuSubsystem& menu) const
{
    const FPass& spec = mPasses[pass];
    if (spec.bPause)
    {
        return spec.PauseTab == SOLMenuState::EMenuTab::Settings && menu.GetPauseMenu() != nullptr
            ? menu.GetPauseMenu()->GetSettingsPage() : nullptr;
    }
    return spec.MainPage == ESOLMainMenuPage::Settings && menu.GetMainMenu() != nullptr
        ? menu.GetMainMenu()->GetSettingsPage() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the pass's menu shows its page or tab (and Settings tab), whatever the view size
bool FSOLSolidMenuProfileSmoke::IsPassMenuShown(const int32 pass, const USOLMenuSubsystem& menu) const
{
    const FPass& spec = mPasses[pass];
    const SOLMenuState::FMenuState& state = menu.GetState();
    if (spec.bPause)
    {
        const USOLPauseMenuWidget* pauseMenu = menu.GetPauseMenu();
        return state.Mode == SOLMenuState::EMenuMode::Paused && pauseMenu != nullptr && pauseMenu->IsInViewport()
            && state.Tab == spec.PauseTab;
    }
    const USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    return state.Mode == SOLMenuState::EMenuMode::MainMenu && mainMenu != nullptr && mainMenu->IsInViewport()
        && mainMenu->GetPage() == spec.MainPage;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the pass's screen is shown: its menu, page and tab, at its view size
bool FSOLSolidMenuProfileSmoke::IsPassScreenShown(const int32 pass, const USOLMenuSubsystem& menu) const
{
    const FPass& spec = mPasses[pass];
    const USOLSettingsPageWidget* page = GetPassSettingsPage(pass, menu);
    const bool bTab = !SOLSettingsTabs::IsValidTab(spec.SettingsTab)
        || (page != nullptr && page->GetSelectedTab() == spec.SettingsTab);
    return IsPassMenuShown(pass, menu) && bTab && GetViewSize() == spec.View;
}

//////////////////////////////////////////////////////////////////////////
// Returns the game viewport's size in pixels (zero without a controller)
FIntPoint FSOLSolidMenuProfileSmoke::GetViewSize() const
{
    FIntPoint size = FIntPoint::ZeroValue;
    if (mController.IsValid())
    {
        mController->GetViewportSize(size.X, size.Y);
    }
    return size;
}

//////////////////////////////////////////////////////////////////////////
// Lets the game window grow past the desktop (the view run's sizes are larger than any monitor here)
void FSOLSolidMenuProfileSmoke::LiftWindowSizeLimits()
{
    const TSharedPtr<SWindow> window = GEngine != nullptr && GEngine->GameViewport != nullptr
        ? GEngine->GameViewport->GetWindow() : nullptr;
    if (window.IsValid())
    {
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(PROFILE_MAX_WINDOW_PX)
            .SetMaxHeight(PROFILE_MAX_WINDOW_PX));
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves the mouse cursor to the view's top-right corner, clear of every menu, so no button is hovered
void FSOLSolidMenuProfileSmoke::ParkCursor() const
{
    const FIntPoint size = GetViewSize();
    if (mController.IsValid() && size.X > PROFILE_CURSOR_INSET_PX)
    {
        mController->SetMouseLocation(size.X - PROFILE_CURSOR_INSET_PX, PROFILE_CURSOR_INSET_PX);
    }
}

//////////////////////////////////////////////////////////////////////////
// Logs the pass's result line with its verdict against the baseline (and the VR floor in a view run)
void FSOLSolidMenuProfileSmoke::LogPassResult(const int32 pass)
{
    const FPass& spec = mPasses[pass];
    const FSOLUiProfileCapture::FPassResult& result = mCapture.GetResult();
    const FSOLUiProfileCapture::FGpuCapture& gpu = result.Gpu;
    const double cpuSlate = result.CpuMs[0];
    const double cpuGameUi = result.CpuMs[1];
    const FIntPoint size = GetViewSize();
    const bool bStrength = FMath::IsNearlyEqual(mStrength, PROFILE_STRENGTH, PROFILE_STRENGTH_TOLERANCE);

    // The plain frames: the mean frame and GPU times, then the p99 of the frame, the game thread and the GPU (the
    // percentile sorts its array, so the means are taken first)
    double frameSumMs = 0.0;
    double gpuSumMs = 0.0;
    for (int32 index = 0; index < mFrameMs.Num(); ++index)
    {
        frameSumMs += mFrameMs[index];
        gpuSumMs += mGpuMs[index];
    }
    const double frameMeanMs = mFrameMs.IsEmpty() ? -1.0 : frameSumMs / mFrameMs.Num();
    const double gpuMeanMs = mGpuMs.IsEmpty() ? -1.0 : gpuSumMs / mGpuMs.Num();
    const double frameP99Ms = SOLSolidMenuProfile::Percentile(mFrameMs, PROFILE_P99);
    const double gameP99Ms = SOLSolidMenuProfile::Percentile(mGameMs, PROFILE_P99);
    const double gpuP99Ms = SOLSolidMenuProfile::Percentile(mGpuMs, PROFILE_P99);

    // Valid only in the baseline's conditions: a standalone game, the pinned date and strength 1, the pass's screen at
    // its view size on every capture frame, every number read
    const bool bValid = !GIsEditor && mIsDatePinned && bStrength && size == spec.View && cpuSlate >= 0.0
        && gpu.UiMs >= 0.0 && mCaptureFrames > 0 && mScreenFrames == mCaptureFrames && frameP99Ms > 0.0;
    const UGameUserSettings* userSettings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
    const bool bVSync = userSettings != nullptr && userSettings->IsVSyncEnabled();
    const int32 scalability = userSettings != nullptr ? userSettings->GetOverallScalabilityLevel() : INDEX_NONE;

    // The budget: the default run against the pre-46c baseline per pass (Q17); the view run judges only the menu's
    // share (user, 2026-11-06): the CPU against its menu's baseline and the menu's whole GPU (no baseline at this
    // size) against the VR floor's ceiling
    const bool bViewRun = mRunView != FIntPoint::ZeroValue;
    const bool bHasBase = spec.BaseCpuMs > 0.0 && (bViewRun || spec.BaseGpuMs > 0.0);
    const double deltaCpu = bHasBase ? cpuSlate - spec.BaseCpuMs : 0.0;
    const double deltaGpu = bHasBase ? gpu.UiMs - spec.BaseGpuMs : 0.0;
    const SOLSolidMenuProfile::FVrFloor floor = SOLSolidMenuProfile::VrFloorForView(spec.View.X, spec.View.Y);
    const bool bBudget = bViewRun ? SOLSolidMenuProfile::IsMenuShareWithinVrFloor(deltaCpu, gpu.UiMs, floor)
        : SOLSolidMenuProfile::IsWithinBudget(deltaCpu, deltaGpu);

    // Whole frame, informational only (project-level VR work, not the menu's verdict): the p99 frame, game thread and
    // GPU against the XR-4 line, and at 3840x2160 the p99 frame against the 4K / 100 Hz proxy
    const bool bVrFrame = SOLSolidMenuProfile::MeetsVrFrameLine(frameP99Ms, gameP99Ms, gpuP99Ms, floor);
    const bool bProxyView = bViewRun && SOLSolidMenuProfile::IsVrProxyView(spec.View.X, spec.View.Y);
    const TCHAR* proxy = !bProxyView ? TEXT("n/a") : SOLSolidMenuProfile::MeetsVrProxy(frameP99Ms) ? TEXT("PASS")
        : TEXT("MISS");
    const TCHAR* verdict = !bValid ? TEXT("FAIL") : !bHasBase ? TEXT("NOBASE") : bBudget ? TEXT("PASS")
        : TEXT("FAIL");
    mPassesFailed += FCString::Strcmp(verdict, TEXT("FAIL")) == 0 ? 1 : 0;
    mPassesNoBase += FCString::Strcmp(verdict, TEXT("NOBASE")) == 0 ? 1 : 0;
    ++mPassesLogged;
    const FString costs = FString::Printf(TEXT("screen=%s view=%dx%d cpu_slate=%.3f cpu_gameui=%.3f gpu_ui=%.3f "
        "gpu_slate=%.3f gpu_solid=%.3f gpu_frame=%.3f ui_draws=%d gpu_ui_all=%s frame_ms=%.2f"), spec.Screen, size.X,
        size.Y, cpuSlate, cpuGameUi, gpu.UiMs, gpu.SlateMs, gpu.MaterialMs, gpu.FrameMs, gpu.UiDraws,
        *result.GpuUiAll, result.FrameMs);
    const FString frames = FString::Printf(TEXT("plain_frame_ms=%.2f plain_gpu_ms=%.2f frame_p99=%.2f game_p99=%.2f "
        "gpu_p99=%.2f vr_frame=%s vr_eyes=%d vr_line=%.1f vr_ui_gpu=%.1f proxy=%s proxy_line=%.1f"), frameMeanMs,
        gpuMeanMs, frameP99Ms, gameP99Ms, gpuP99Ms, !bViewRun ? TEXT("n/a") : bVrFrame ? TEXT("PASS") : TEXT("MISS"),
        floor.bTwoEyes ? 2 : 1, floor.LineMs, floor.UiGpuMs, proxy, SOLSolidMenuProfile::VR_PROXY_FRAME_P99_MS);
    const FString conditions = FString::Printf(TEXT("strength=%.2f vsync=%s scalability=%d editor=%s valid=%s "
        "jd=%.5f screen_frames=%d/%d base_cpu=%.3f base_gpu=%.3f dcpu=%+.3f dgpu=%+.3f budget=%s budget_cpu=%.3f "
        "budget_gpu=%.3f"), mStrength, bVSync ? TEXT("on") : TEXT("off"), scalability, GIsEditor ? TEXT("yes")
        : TEXT("no"), bValid ? TEXT("yes") : TEXT("no"), PROFILE_SIM_JD, mScreenFrames, mCaptureFrames,
        spec.BaseCpuMs, spec.BaseGpuMs, deltaCpu, deltaGpu, bBudget ? TEXT("PASS") : TEXT("FAIL"),
        SOLSolidMenuProfile::BUDGET_CPU_MS, bViewRun ? floor.UiGpuMs : SOLSolidMenuProfile::BUDGET_GPU_MS);
    UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile RESULT P%d %s %s %s %s"), pass, verdict, *costs, *frames,
        *conditions);
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, stops the capture and quits
void FSOLSolidMenuProfileSmoke::Finish(USOLMenuSubsystem& menu)
{
    if (mIsFinished)
    {
        return;
    }
    mIsFinished = true;

    // A run that logged another number of checks than a whole run does skipped (or doubled) one: a failure
    const int32 expected = mRunView == FIntPoint::ZeroValue ? PROFILE_EXPECTED_CHECKS_DEFAULT
        : PROFILE_EXPECTED_CHECKS_VIEW;
    if (mChecksRun != expected)
    {
        Check(TEXT("z-check-count"), false, FString::Printf(TEXT("%d checks run, a whole run logs %d"), mChecksRun,
            expected));
    }
    UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile SUMMARY %d passes: %d PASS, %d FAIL, %d NOBASE; %d/%d checks passed"),
        mPasses.Num(), FMath::Max(0, mPasses.Num() - mPassesFailed - mPassesNoBase), mPassesFailed, mPassesNoBase,
        mChecksPassed, mChecksRun);
    mCapture.Stop(menu.GetWorld());
    UKismetSystemLibrary::QuitGame(menu.GetWorld(), nullptr, EQuitPreference::Quit, false);
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot with the UI to the screenshot folder (Saved/Screenshots/<platform>)
void FSOLSolidMenuProfileSmoke::TakeUiShot(const TCHAR* name)
{
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("SolidMenuProfile_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile: screenshot with UI %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLSolidMenuProfileSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile CHECK %-18s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
