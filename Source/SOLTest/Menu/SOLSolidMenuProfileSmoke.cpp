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

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // The script's steps, in order
    enum class EProfileStep : int32
    {
        Setup,              // The main menu shows; the sim date pinned, the effects strength read
        ViewLarge,          // r.SetRes to 1920x1080 (windowed), until the view has that size
        MainSettings,       // The main menu's Settings page opens
        Pass0,              // P0: main menu, Settings, 1920x1080
        ViewSmall,          // r.SetRes to 1280x720
        Pass1,              // P1: main menu, Settings, 1280x720
        Play,               // The page closed, Play: the ship spawns and flight settles
        Pause,              // Esc: the pause menu
        PauseSettings,      // Its Settings tab
        Pass2,              // P2: pause menu, Settings, 1280x720
        ViewLargeAgain,     // r.SetRes to 1920x1080
        Pass3,              // P3: pause menu, Settings, 1920x1080
        Quit,               // The SUMMARY line, then quit
        Count,              // Number of steps
    };

    constexpr int32 PROFILE_PASS_COUNT = 4;
    constexpr int32 PROFILE_SHOT_FRAMES = 15;                   // Frames after the screenshot request
    constexpr int32 PROFILE_MIN_FRAMES = 5;                     // Every step lasts at least this many frames
    constexpr int32 PROFILE_VIEW_STABLE_FRAMES = 10;            // Frames in a row at the requested view size
    constexpr int32 PROFILE_CURSOR_INSET_PX = 8;                // The parked cursor, this far in from the top right
    constexpr double PROFILE_SETUP_S = 3.0;                     // The main menu's first frames (and shader loads)
    constexpr double PROFILE_SETUP_TIMEOUT_S = 30.0;
    constexpr double PROFILE_STEP_S = 0.5;                      // A state change step lasts at least this once done
    constexpr double PROFILE_STEP_TIMEOUT_S = 5.0;
    constexpr double PROFILE_VIEW_TIMEOUT_S = 8.0;
    constexpr double PROFILE_FLIGHT_SETTLE_S = 3.0;             // Flight after Play: the ship spawn and the HUD
    constexpr double PROFILE_FLIGHT_TIMEOUT_S = 20.0;
    constexpr double PROFILE_SETTLE_S = 1.5;                    // A pass waits this long on its screen first
    constexpr double PROFILE_QUIT_S = 1.0;
    constexpr double PROFILE_WATCHDOG_S = 180.0;                // A run still going after this is stuck: it quits
    constexpr double PROFILE_BUDGET_CPU_MS = 0.1;               // SDD 46 Q17: extra Slate paint CPU per screen
    constexpr double PROFILE_BUDGET_GPU_MS = 0.3;               // ... and extra GPU in the Slate UI pass
    constexpr float PROFILE_STRENGTH = 1.0f;                    // The effects strength the numbers are taken at
    constexpr float PROFILE_STRENGTH_TOLERANCE = 1.0e-3f;

    // The settle outlasts every open animation of SDD 7 (panel open, then the trace power-up)
    static_assert(PROFILE_SETTLE_S > SOL::SolidMenu::PANEL_OPEN_S + SOL::SolidMenu::TRACE_POWER_DELAY_S
        + SOL::SolidMenu::TRACE_POWER_S + SOL::SolidMenu::PAUSE_SLIDE_S, "The settle must outlast the open animations");

    // The fixed scene behind the menus: the sim date of the other profiling smokes (JD 2461348.5 = 2026-11-04 00:00
    // UTC), so the main menu's view of Earth and the flight under the pause menu are the same every run
    constexpr double PROFILE_SIM_JD = 2461348.5;

    // The view sizes, and the pass names in the log line
    const FIntPoint PROFILE_VIEW_LARGE(1920, 1080);
    const FIntPoint PROFILE_VIEW_SMALL(1280, 720);
    const FIntPoint PROFILE_PASS_VIEWS[PROFILE_PASS_COUNT] = {
        PROFILE_VIEW_LARGE, PROFILE_VIEW_SMALL, PROFILE_VIEW_SMALL, PROFILE_VIEW_LARGE };
    const TCHAR* const PROFILE_PASS_SCREENS[PROFILE_PASS_COUNT] = {
        TEXT("main-settings"), TEXT("main-settings"), TEXT("pause-settings"), TEXT("pause-settings") };
    constexpr bool PROFILE_PASS_IS_PAUSE[PROFILE_PASS_COUNT] = { false, false, true, true };

    // Pre-46c baseline (SDD 46 section 13, 46c amendment): cpu_slate (Draw Window And Children Time) and gpu_ui (the
    // game window's SlateUI pass), ms, the medians of three runs at 816da1a plus the uncommitted 46a review and 46c
    // step 0 files (today's flat look, no rendering change), on an i9-9960X / RTX 2080 Ti with the Development editor
    // binary in -game, 2026-11-04. Zero means no baseline: the verdict is NOBASE
    constexpr double PROFILE_BASELINE_CPU_MS[PROFILE_PASS_COUNT] = { 0.423, 0.444, 0.430, 0.429 };
    constexpr double PROFILE_BASELINE_GPU_MS[PROFILE_PASS_COUNT] = { 0.046, 0.027, 0.037, 0.075 };

    // The game-thread Slate stats, one stat dumpave window each: Draw Window And Children Time (the whole Slate paint)
    // and Game UI Paint (inside it, for reference)
    const TCHAR* const PROFILE_CPU_ROOT_STATS[] = { TEXT("STAT_SlateDrawWindowTime"), TEXT("STAT_ViewportPaintTime") };
    // The solid menu material draws summed into gpu_solid (M_SOLSolidHull, its instances and MIDs; none before 46c)
    const TCHAR* const PROFILE_SOLID_MATERIAL_TOKEN = TEXT("SOLSolid");

    //////////////////////////////////////////////////////////////////////////
    // Returns the pass a step captures, or INDEX_NONE
    int32 PassOfStep(const EProfileStep step)
    {
        switch (step)
        {
        case EProfileStep::Pass0:
            return 0;
        case EProfileStep::Pass1:
            return 1;
        case EProfileStep::Pass2:
            return 2;
        case EProfileStep::Pass3:
            return 3;
        default:
            return INDEX_NONE;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the view size a resize step asks for, or zero for any other step
    FIntPoint ViewOfStep(const EProfileStep step)
    {
        switch (step)
        {
        case EProfileStep::ViewLarge:
        case EProfileStep::ViewLargeAgain:
            return PROFILE_VIEW_LARGE;
        case EProfileStep::ViewSmall:
            return PROFILE_VIEW_SMALL;
        default:
            return FIntPoint::ZeroValue;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets up the capture with the Slate root stats
FSOLSolidMenuProfileSmoke::FSOLSolidMenuProfileSmoke()
    : mCapture(PROFILE_CPU_ROOT_STATS, PROFILE_SOLID_MATERIAL_TOKEN)
{
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

    // The captures need a standalone -game process: in the editor the editor's own work pollutes the numbers
    if (GIsEditor)
    {
        UE_LOG(LogSOL, Warning, TEXT("SolidMenuProfile: -%s needs -game (standalone); the numbers are not comparable"),
            SOL::CommandLine::SMOKE_SOLID_MENU_PROFILE);
    }
    mCapture.Start(menu.GetWorld());
    UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile: scripted menu cost capture starting (%d passes, %d ProfileGPU frames "
        "and %d stat dumpave windows of %d frames each)"), PROFILE_PASS_COUNT, FSOLUiProfileCapture::GPU_CAPTURES,
        static_cast<int32>(UE_ARRAY_COUNT(PROFILE_CPU_ROOT_STATS)), FSOLUiProfileCapture::DUMP_FRAMES);
    mStep = 0;
    EnterStep(mStep, menu);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished (and requested the quit)
bool FSOLSolidMenuProfileSmoke::Update(USOLMenuSubsystem& menu)
{
    if (mIsFinished || mStep >= static_cast<int32>(EProfileStep::Count))
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
    if (mRunTimeS >= PROFILE_WATCHDOG_S || !mController.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SolidMenuProfile: %s at step %d after %.1f s; aborting"), mController.IsValid()
            ? TEXT("watchdog") : TEXT("no player controller"), mStep, mRunTimeS);
        mPassesFailed += FMath::Max(0, PROFILE_PASS_COUNT - (mPassesFailed + mPassesNoBase));
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
    if (mIsFinished || mStep >= static_cast<int32>(EProfileStep::Count))
    {
        return true;
    }
    EnterStep(mStep, menu);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLSolidMenuProfileSmoke::EnterStep(const int32 step, USOLMenuSubsystem& menu)
{
    UWorld* world = menu.GetWorld();
    const EProfileStep current = static_cast<EProfileStep>(step);
    const int32 pass = PassOfStep(current);
    if (pass != INDEX_NONE)
    {
        // A pass starts from a clean slate
        mPhase = EPassPhase::Settle;
        mPhaseTimeS = 0.0;
        mPhaseFrames = 0;
        mCaptureFrames = 0;
        mScreenFrames = 0;
        // The requested Settings tab (-SOLSmokeSolidMenuProfileTab), else the page's opening tab
        USOLMainMenuWidget* main = menu.GetMainMenu();
        USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        USOLSettingsPageWidget* page = PROFILE_PASS_IS_PAUSE[pass] ? (pause != nullptr ? pause->GetSettingsPage()
            : nullptr) : (main != nullptr ? main->GetSettingsPage() : nullptr);
        if (page != nullptr && SOLSettingsTabs::IsValidTab(mProfileTab))
        {
            page->SelectTab(mProfileTab);
        }
        UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile: pass P%d %s %dx%d tab %d starting"), pass,
            PROFILE_PASS_SCREENS[pass], PROFILE_PASS_VIEWS[pass].X, PROFILE_PASS_VIEWS[pass].Y,
            page != nullptr ? static_cast<int32>(page->GetSelectedTab()) : -1);
        return;
    }
    const FIntPoint view = ViewOfStep(current);
    if (view != FIntPoint::ZeroValue)
    {
        mViewFrames = 0;
        FSOLUiProfileCapture::RunCommand(*FString::Printf(TEXT("r.SetRes %dx%dw"), view.X, view.Y), world);
        return;
    }
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    switch (current)
    {
    case EProfileStep::Setup:
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
    case EProfileStep::MainSettings:
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::Settings);
        }
        break;
    case EProfileStep::Play:
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::None);
        }
        menu.RequestPlay();
        break;
    case EProfileStep::Pause:
        menu.RequestEscape();
        break;
    case EProfileStep::PauseSettings:
        menu.SelectTab(SOLMenuState::EMenuTab::Settings);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's per-frame work; returns true once the step is done
bool FSOLSolidMenuProfileSmoke::TickStep(const int32 step, USOLMenuSubsystem& menu, const float realDeltaSeconds)
{
    const EProfileStep current = static_cast<EProfileStep>(step);
    const int32 pass = PassOfStep(current);
    if (pass != INDEX_NONE)
    {
        return TickPass(pass, menu, realDeltaSeconds);
    }
    const FIntPoint view = ViewOfStep(current);
    if (view != FIntPoint::ZeroValue)
    {
        mViewFrames = GetViewSize() == view ? mViewFrames + 1 : 0;
        return (mViewFrames >= PROFILE_VIEW_STABLE_FRAMES && mStepTimeS >= PROFILE_STEP_S)
            || mStepTimeS >= PROFILE_VIEW_TIMEOUT_S;
    }
    const SOLMenuState::EMenuMode mode = menu.GetState().Mode;
    switch (current)
    {
    case EProfileStep::Setup:
    {
        const bool bShown = mode == SOLMenuState::EMenuMode::MainMenu && menu.GetMainMenu() != nullptr
            && menu.GetMainMenu()->IsInViewport();
        return (bShown && mStepTimeS >= PROFILE_SETUP_S) || mStepTimeS >= PROFILE_SETUP_TIMEOUT_S;
    }
    case EProfileStep::MainSettings:
        return (IsMainSettingsShown(menu) && mStepTimeS >= PROFILE_STEP_S) || mStepTimeS >= PROFILE_STEP_TIMEOUT_S;
    case EProfileStep::Play:
    {
        const bool bFlying = mode == SOLMenuState::EMenuMode::Playing && Cast<ASOLShipPawn>(mController->GetPawn())
            != nullptr;
        return (bFlying && mStepTimeS >= PROFILE_FLIGHT_SETTLE_S) || mStepTimeS >= PROFILE_FLIGHT_TIMEOUT_S;
    }
    case EProfileStep::Pause:
    {
        const bool bPaused = mode == SOLMenuState::EMenuMode::Paused && menu.GetPauseMenu() != nullptr
            && menu.GetPauseMenu()->IsInViewport();
        return (bPaused && mStepTimeS >= PROFILE_STEP_S) || mStepTimeS >= PROFILE_STEP_TIMEOUT_S;
    }
    case EProfileStep::PauseSettings:
        return (IsPauseSettingsShown(menu) && mStepTimeS >= PROFILE_STEP_S) || mStepTimeS >= PROFILE_STEP_TIMEOUT_S;
    case EProfileStep::Quit:
        return mStepTimeS >= PROFILE_QUIT_S;
    default:
        return mStepTimeS >= PROFILE_STEP_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLSolidMenuProfileSmoke::ExitStep(const int32 step, USOLMenuSubsystem& menu)
{
    const EProfileStep current = static_cast<EProfileStep>(step);
    const FIntPoint view = ViewOfStep(current);
    const FIntPoint size = GetViewSize();
    if (view != FIntPoint::ZeroValue)
    {
        Check(view == PROFILE_VIEW_SMALL ? TEXT("view-1280x720") : TEXT("view-1920x1080"), size == view,
            FString::Printf(TEXT("r.SetRes %dx%d: view %dx%d"), view.X, view.Y, size.X, size.Y));
        return;
    }
    const SOLMenuState::EMenuMode mode = menu.GetState().Mode;
    switch (current)
    {
    case EProfileStep::Setup:
    {
        // The settings in use: the smoke slot, cleared at boot, so the default strength
        const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
        mStrength = settings != nullptr ? settings->GetSavedValues().HudEffectsStrength : -1.0f;
        Check(TEXT("a-main-menu"), mode == SOLMenuState::EMenuMode::MainMenu && menu.GetMainMenu() != nullptr
            && mIsDatePinned, FString::Printf(TEXT("main menu %s, date %s, view %dx%d"), mode
            == SOLMenuState::EMenuMode::MainMenu ? TEXT("shown") : TEXT("NOT SHOWN"), mIsDatePinned ? TEXT("pinned")
            : TEXT("NOT PINNED"), size.X, size.Y));
        Check(TEXT("b-strength"), FMath::IsNearlyEqual(mStrength, PROFILE_STRENGTH, PROFILE_STRENGTH_TOLERANCE),
            FString::Printf(TEXT("HUD effects strength %.2f (want %.2f)"), mStrength, PROFILE_STRENGTH));
        break;
    }
    case EProfileStep::MainSettings:
        Check(TEXT("c-main-settings"), IsMainSettingsShown(menu), FString::Printf(TEXT("main menu Settings page %s"),
            IsMainSettingsShown(menu) ? TEXT("open") : TEXT("NOT OPEN")));
        break;
    case EProfileStep::Play:
        Check(TEXT("d-flight"), mode == SOLMenuState::EMenuMode::Playing && Cast<ASOLShipPawn>(mController->GetPawn())
            != nullptr, FString::Printf(TEXT("Play: %s"), mode == SOLMenuState::EMenuMode::Playing ? TEXT("flying")
            : TEXT("NOT FLYING")));
        break;
    case EProfileStep::Pause:
        Check(TEXT("e-pause"), mode == SOLMenuState::EMenuMode::Paused, FString::Printf(TEXT("Esc: %s"), mode
            == SOLMenuState::EMenuMode::Paused ? TEXT("paused") : TEXT("NOT PAUSED")));
        break;
    case EProfileStep::PauseSettings:
        Check(TEXT("f-pause-settings"), IsPauseSettingsShown(menu), FString::Printf(TEXT("pause menu Settings tab %s"),
            IsPauseSettingsShown(menu) ? TEXT("open") : TEXT("NOT OPEN")));
        break;
    case EProfileStep::Quit:
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
    if (mPhase == EPassPhase::Capture && mCapture.IsCapturing())
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
            LogPassResult(pass, menu);
            nextPhase(EPassPhase::Done);
            return true;
        }
        return false;
    }
    default:
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the pass's screen is shown: its menu with Settings open, at its view size
bool FSOLSolidMenuProfileSmoke::IsPassScreenShown(const int32 pass, const USOLMenuSubsystem& menu) const
{
    const bool bMenu = PROFILE_PASS_IS_PAUSE[pass] ? IsPauseSettingsShown(menu) : IsMainSettingsShown(menu);
    return bMenu && GetViewSize() == PROFILE_PASS_VIEWS[pass];
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the main menu shows with its Settings page open
bool FSOLSolidMenuProfileSmoke::IsMainSettingsShown(const USOLMenuSubsystem& menu)
{
    const USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    return menu.GetState().Mode == SOLMenuState::EMenuMode::MainMenu && mainMenu != nullptr
        && mainMenu->IsInViewport() && mainMenu->GetPage() == ESOLMainMenuPage::Settings;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the pause menu shows with its Settings tab open
bool FSOLSolidMenuProfileSmoke::IsPauseSettingsShown(const USOLMenuSubsystem& menu)
{
    const USOLPauseMenuWidget* pauseMenu = menu.GetPauseMenu();
    return menu.GetState().Mode == SOLMenuState::EMenuMode::Paused && pauseMenu != nullptr
        && pauseMenu->IsInViewport() && menu.GetState().Tab == SOLMenuState::EMenuTab::Settings;
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
// Logs the pass's result line with its verdict against the baseline
void FSOLSolidMenuProfileSmoke::LogPassResult(const int32 pass, const USOLMenuSubsystem& menu)
{
    const FSOLUiProfileCapture::FPassResult& result = mCapture.GetResult();
    const FSOLUiProfileCapture::FGpuCapture& gpu = result.Gpu;
    const double cpuSlate = result.CpuMs[0];
    const double cpuGameUi = result.CpuMs[1];
    const FIntPoint size = GetViewSize();
    const bool bStrength = FMath::IsNearlyEqual(mStrength, PROFILE_STRENGTH, PROFILE_STRENGTH_TOLERANCE);

    // Valid only in the baseline's conditions: a standalone game, the pinned date and strength 1, the pass's screen at
    // its view size on every capture frame, every number read
    const bool bValid = !GIsEditor && mIsDatePinned && bStrength && size == PROFILE_PASS_VIEWS[pass] && cpuSlate >= 0.0
        && gpu.UiMs >= 0.0 && mCaptureFrames > 0 && mScreenFrames == mCaptureFrames;
    const UGameUserSettings* userSettings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
    const bool bVSync = userSettings != nullptr && userSettings->IsVSyncEnabled();
    const int32 scalability = userSettings != nullptr ? userSettings->GetOverallScalabilityLevel() : INDEX_NONE;

    // The verdict against the pre-46c baseline (budget: under 0.1 ms extra CPU and 0.3 ms extra GPU)
    const double baseCpu = PROFILE_BASELINE_CPU_MS[pass];
    const double baseGpu = PROFILE_BASELINE_GPU_MS[pass];
    const bool bHasBase = baseCpu > 0.0 && baseGpu > 0.0;
    const double deltaCpu = bHasBase ? cpuSlate - baseCpu : 0.0;
    const double deltaGpu = bHasBase ? gpu.UiMs - baseGpu : 0.0;
    const TCHAR* verdict = !bValid ? TEXT("FAIL") : !bHasBase ? TEXT("NOBASE")
        : deltaCpu < PROFILE_BUDGET_CPU_MS && deltaGpu < PROFILE_BUDGET_GPU_MS ? TEXT("PASS") : TEXT("FAIL");
    mPassesFailed += FCString::Strcmp(verdict, TEXT("FAIL")) == 0 ? 1 : 0;
    mPassesNoBase += FCString::Strcmp(verdict, TEXT("NOBASE")) == 0 ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile RESULT P%d %s screen=%s view=%dx%d cpu_slate=%.3f cpu_gameui=%.3f "
        "gpu_ui=%.3f gpu_slate=%.3f gpu_solid=%.3f gpu_frame=%.3f ui_draws=%d gpu_ui_all=%s frame_ms=%.2f "
        "strength=%.2f vsync=%s scalability=%d editor=%s valid=%s jd=%.5f screen_frames=%d/%d base_cpu=%.3f "
        "base_gpu=%.3f dcpu=%+.3f dgpu=%+.3f budget_cpu=%.3f budget_gpu=%.3f"), pass, verdict,
        PROFILE_PASS_SCREENS[pass], size.X, size.Y, cpuSlate, cpuGameUi, gpu.UiMs, gpu.SlateMs, gpu.MaterialMs,
        gpu.FrameMs, gpu.UiDraws, *result.GpuUiAll, result.FrameMs, mStrength, bVSync ? TEXT("on") : TEXT("off"),
        scalability, GIsEditor ? TEXT("yes") : TEXT("no"), bValid ? TEXT("yes") : TEXT("no"), PROFILE_SIM_JD,
        mScreenFrames, mCaptureFrames, baseCpu, baseGpu, deltaCpu, deltaGpu, PROFILE_BUDGET_CPU_MS,
        PROFILE_BUDGET_GPU_MS);
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
    UE_LOG(LogSOL, Log, TEXT("SolidMenuProfile SUMMARY %d passes: %d PASS, %d FAIL, %d NOBASE; %d/%d checks passed"),
        PROFILE_PASS_COUNT, FMath::Max(0, PROFILE_PASS_COUNT - mPassesFailed - mPassesNoBase), mPassesFailed,
        mPassesNoBase, mChecksPassed, mChecksRun);
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
