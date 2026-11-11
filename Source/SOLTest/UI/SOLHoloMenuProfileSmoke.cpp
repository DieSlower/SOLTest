/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloMenuProfileSmoke.h"

#include "Flight/SOLFlight.h"
#include "Interaction/SOLInteractionSubsystem.h"
#include "Interaction/SOLInteractMenus.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHudShapeMath.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"
#include "UnrealClient.h"

namespace
{
    // The script's steps, in order
    enum class EProfileStep : int32
    {
        Setup,              // The date pinned, the ship placed at rest facing Earth; the named body's icon is found
        Pass0,              // P0: flight, no menu, no target, no info card
        GlowZero,           // Issue #37: a pending HUD glow of 0% (through the settings, never applied)
        Pass5,              // P5: P0 at HUD glow 0%
        GlowDouble,         // A pending HUD glow of 200%
        Pass6,              // P6: P0 at HUD glow 200%
        GlowRevert,         // Revert: the saved HUD glow back
        PrepTarget,         // Radial Target on the body: the target card shows
        PrepInfo,           // Radial Info on the body: the info card shows
        RadialOpen,         // Fire press on the body's icon, held, the cursor on a sector
        Pass1,              // P1: the radial menu held open
        RadialClose,        // Release in the centre: cancelled
        OverlayOpen,        // Ctrl+O
        Pass2,              // P2: the Overlay Menu
        OverlayClose,       // Esc
        SpeedOpen,          // F3
        Pass3,              // P3: the speed panel
        SpeedClose,         // F3 to the panel
        MapOpen,            // Shift+M path (HandleOpenMap), the map camera settles
        ListOpen,           // The list menu on the body
        Pass4,              // P4: the map's list menu
        ListClose,          // The list menu closed directly
        MapClose,           // The map closed (HandleCloseMap), back to flight
        Quit,               // The SUMMARY line, then quit
        Count,              // Number of steps
    };

    constexpr int32 PROFILE_PASS_COUNT = 7;
    constexpr int32 PROFILE_SHOT_FRAMES = 15;                   // Frames after the screenshot request
    constexpr int32 PROFILE_MIN_FRAMES = 5;
    constexpr double PROFILE_SETUP_S = 3.0;
    constexpr double PROFILE_STEP_S = 0.5;
    constexpr double PROFILE_OPEN_MIN_S = 0.3;                  // An open step lasts at least this once open
    constexpr double PROFILE_OPEN_TIMEOUT_S = 3.0;
    constexpr double PROFILE_MAP_SETTLE_S = 2.0;                // The map camera's move
    constexpr double PROFILE_MAP_TIMEOUT_S = 5.0;
    constexpr double PROFILE_SETTLE_S = 1.5;                    // A pass waits this long after its menu opened
    constexpr double PROFILE_QUIT_S = 1.0;
    constexpr double PROFILE_BUDGET_MS = 0.1;                   // SDD 42 D14: extra CPU and extra GPU per pass
    // Wait after a menu or card (re)opens before a capture starts: the open animation, plus a margin (real seconds)
    constexpr double PROFILE_CARD_WAIT_S = SOL::HoloMenu::OPEN_S + 0.1;
    constexpr int32 PROFILE_PLACE_FRAME = 3;                    // Setup frame the ship is placed (bodies at the date)
    constexpr int32 PROFILE_VIEW_WIDTH = 1280;                  // The view the baseline was measured at
    constexpr int32 PROFILE_VIEW_HEIGHT = 720;
    constexpr float PROFILE_MAX_WINDOW_PX = 16384.0f;           // A view run's lifted window size limit (SDD 37 8)

    // The fixed scene (SDD 42 section 11.1): the sim date is pinned so the bodies behind Earth, the profile body's
    // icon and card and the map's overlays are the same every run (JD 2461348.5 = 2026-11-04 00:00 UTC); the menus and
    // cards work on Neptune, about six weeks past opposition at that date, so its icon is on screen from the start
    // above Earth (the bodies drawn there: Earth, Dione, Oberon, Neptune, Eris)
    constexpr double PROFILE_SIM_JD = 2461348.5;
    const TCHAR* const PROFILE_BODY_NAME = SOL::BodyNames::NEPTUNE;

    // The pass names, in the log line
    const TCHAR* const PROFILE_PASS_NAMES[PROFILE_PASS_COUNT] = {
        TEXT("flight"), TEXT("radial"), TEXT("overlay-menu"), TEXT("speed-panel"), TEXT("map-list"),
        TEXT("flight-glow-0"), TEXT("flight-glow-200") };

    // The pending HUD glow of the glow steps (SDD 37 section 8): P5 at 0%, P6 at 200%; P0 has the saved (default) 100%
    constexpr float PROFILE_GLOW_ZERO = SOL::Settings::HUD_GLOW_MIN;
    constexpr float PROFILE_GLOW_DOUBLE = SOL::Settings::HUD_GLOW_MAX;
    constexpr float PROFILE_GLOW_TOLERANCE = 1.0e-5f;           // Pushed glow scale vs the math

    // Pre-conversion baseline (SDD 42 section 11.1, measured before 42c): cpu_total (HUD Time + Draw Window And
    // Children Time) and gpu_ui (the game window's SlateUI pass), ms, the mean of the two baseline runs at commit
    // 49949c0 plus the 42a files (no rendering change), PROFILE_SIM_JD 2461348.5 with Neptune as the body, on an
    // i9-9960X / RTX 2080 Ti with the Development editor binary in -game. Zero means no baseline: the verdict is NOBASE
    // (the glow passes P5 and P6, issue #37, are compared with the same run's P0 by hand)
    constexpr double PROFILE_BASELINE_CPU_MS[PROFILE_PASS_COUNT] = { 0.2365, 0.285, 0.4015, 0.4275, 0.927, 0.0, 0.0 };
    constexpr double PROFILE_BASELINE_GPU_MS[PROFILE_PASS_COUNT] = { 0.0785, 0.1075, 0.114, 0.127, 0.0785, 0.0, 0.0 };

    // The game-thread stats, one stat dumpave window each: HUD Time, Draw Window And Children Time, Game UI Paint
    const TCHAR* const PROFILE_CPU_ROOT_STATS[] = {
        TEXT("STAT_HudTime"), TEXT("STAT_SlateDrawWindowTime"), TEXT("STAT_ViewportPaintTime") };
    // The HUD shape draws summed into gpu_shapes ("Slate Material: MID_M_SOLHudShape*")
    const TCHAR* const PROFILE_SHAPE_MATERIAL_TOKEN = TEXT("MID_M_SOLHudShape");

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
        case EProfileStep::Pass4:
            return 4;
        case EProfileStep::Pass5:
            return 5;
        case EProfileStep::Pass6:
            return 6;
        default:
            return INDEX_NONE;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a pass runs with the body targeted and its info card shown (P1-P4; P0, P5 and P6 fly bare)
    bool PassHasCards(const int32 pass)
    {
        return pass >= 1 && pass <= 4;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets up the capture with the pass's root stats
FSOLHoloMenuProfileSmoke::FSOLHoloMenuProfileSmoke()
    : mCapture(PROFILE_CPU_ROOT_STATS, PROFILE_SHAPE_MATERIAL_TOKEN)
{
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, HUD and subsystems, starts listening to the log and starts the first step
void FSOLHoloMenuProfileSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mHud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    mInteraction = world->GetSubsystem<USOLInteractionSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mTargeting = world->GetSubsystem<USOLTargetingSubsystem>();
    pawn.SetFlightCursorScripted(true);

    // The captures need a standalone -game process: in the editor the editor's own work pollutes the numbers
    if (GIsEditor)
    {
        UE_LOG(LogSOL, Warning, TEXT("HoloMenuProfile: -%s needs -game (standalone); the numbers are not comparable"),
            SOL::CommandLine::SMOKE_HOLO_MENU_PROFILE);
    }
    mCapture.Start(world);
    UE_LOG(LogSOL, Log, TEXT("HoloMenuProfile: scripted menu cost capture starting (%d passes, %d ProfileGPU frames "
        "and %d stat dumpave windows of %d frames each)"), PROFILE_PASS_COUNT, FSOLUiProfileCapture::GPU_CAPTURES,
        static_cast<int32>(UE_ARRAY_COUNT(PROFILE_CPU_ROOT_STATS)), FSOLUiProfileCapture::DUMP_FRAMES);
    mStep = 0;
    EnterStep(mStep, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Sets the -ResX / -ResY view of a view run (SDD 37 section 8); false while the game window does not exist yet
bool FSOLHoloMenuProfileSmoke::ApplyCommandLineView(UWorld* world)
{
    // The game window may grow past the desktop and the view is set with r.SetRes (a windowed game is otherwise clamped
    // to the desktop's work area); the setup step retries every frame, and its wait covers the resize
    int32 width = 0;
    int32 height = 0;
    if (!FParse::Value(FCommandLine::Get(), TEXT("ResX="), width) || !FParse::Value(FCommandLine::Get(), TEXT("ResY="),
        height) || width <= 0 || height <= 0 || (width == PROFILE_VIEW_WIDTH && height == PROFILE_VIEW_HEIGHT))
    {
        return true;
    }
    const TSharedPtr<SWindow> window = GEngine != nullptr && GEngine->GameViewport != nullptr
        ? GEngine->GameViewport->GetWindow() : nullptr;
    if (!window.IsValid())
    {
        return false;
    }
    window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(PROFILE_MAX_WINDOW_PX).SetMaxHeight(PROFILE_MAX_WINDOW_PX));
    FSOLUiProfileCapture::RunCommand(*FString::Printf(TEXT("r.SetRes %dx%dw"), width, height), world);
    UE_LOG(LogSOL, Log, TEXT("HoloMenuProfile: view run at %dx%d (window size limit lifted)"), width, height);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLHoloMenuProfileSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mStep >= static_cast<int32>(EProfileStep::Count))
    {
        return true;
    }
    if (!mController.IsValid() || !mInteraction.IsValid() || !mShips.IsValid() || !mTargeting.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("HoloMenuProfile: controller or subsystems missing; aborting"));
        mStep = static_cast<int32>(EProfileStep::Count);
        UKismetSystemLibrary::QuitGame(pawn.GetWorld(), nullptr, EQuitPreference::Quit, false);
        return true;
    }
    if (!mHud.IsValid())
    {
        mHud = mController->GetHUD<ASOLFlightHud>();
    }
    for (const FKey& key : mPendingReleases)
    {
        ReleaseKey(key);
    }
    mPendingReleases.Reset();

    // The fixed scene: the ship stays at rest in its reference frame for the whole run
    mShips->SetVelocityMps(mShips->GetReferenceVelocityMps());

    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    if (!TickStep(mStep, pawn, realDeltaSeconds) || mStepFrames < PROFILE_MIN_FRAMES)
    {
        return false;
    }
    ExitStep(mStep, pawn);
    ++mStep;
    mStepTimeS = 0.0;
    mStepFrames = 0;
    if (mStep >= static_cast<int32>(EProfileStep::Count))
    {
        return true;
    }
    EnterStep(mStep, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLHoloMenuProfileSmoke::EnterStep(const int32 step, ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mExecutedBefore = mInteraction->GetExecutedCount();
    mCancelsBefore = mInteraction->GetRadialCancelCount();
    const EProfileStep current = static_cast<EProfileStep>(step);
    if (PassOfStep(current) != INDEX_NONE)
    {
        // A pass starts from a clean slate
        mPhase = EPassPhase::Settle;
        mPhaseTimeS = 0.0;
        mPhaseFrames = 0;
        mCardRefreshAtS = -1.0;
        mMenuFrames = 0;
        mMenuOpenFrames = 0;
        UE_LOG(LogSOL, Log, TEXT("HoloMenuProfile: pass P%d %s starting"), PassOfStep(current),
            PROFILE_PASS_NAMES[PassOfStep(current)]);
        return;
    }
    switch (current)
    {
    case EProfileStep::Setup:
    {
        // The pinned date; the ship is placed for it a few frames later, once the bodies have moved there
        USOLSimClockSubsystem* clock = world->GetSubsystem<USOLSimClockSubsystem>();
        if (clock != nullptr)
        {
            clock->SetJulianDate(PROFILE_SIM_JD);
        }
        mShips->DisengageAutopilot();
        UE_LOG(LogSOL, Log, TEXT("HoloMenuProfile: sim date pinned to JD %.5f%s; profile body %s"), PROFILE_SIM_JD,
            clock != nullptr ? TEXT("") : TEXT(" FAILED (no sim clock)"), PROFILE_BODY_NAME);
        break;
    }
    case EProfileStep::GlowZero:
    case EProfileStep::GlowDouble:
        // SDD 37 section 8: a pending HUD glow, as the Settings slider sets it (previewed at once, never applied)
        if (USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&pawn))
        {
            SOLSettingsModel::FSettingsValues values = settings->GetLiveValues();
            values.HudGlow = current == EProfileStep::GlowZero ? PROFILE_GLOW_ZERO : PROFILE_GLOW_DOUBLE;
            settings->SetPendingValues(values);
        }
        break;
    case EProfileStep::GlowRevert:
        if (USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&pawn))
        {
            settings->RevertPending();
        }
        break;
    case EProfileStep::PrepTarget:
        RadialAction(pawn, ESOLAction::Target);
        break;
    case EProfileStep::PrepInfo:
        RadialAction(pawn, ESOLAction::Info);
        break;
    case EProfileStep::RadialOpen:
        // Held open with a sector highlighted (the pawn's tick moves the highlight before the HUD draws)
        PressOnBody(pawn);
        DragToAction(pawn, ESOLAction::Info);
        break;
    case EProfileStep::RadialClose:
        pawn.HandleFlightCursorMoved(mInteraction->GetRadial().CenterPx);
        mInteraction->MovePointer(pawn.GetFlightCursorPx());
        pawn.HandleFire(false);
        break;
    case EProfileStep::OverlayOpen:
        // Ctrl+O through the mapping context; both keys are released on the next frame
        PressKey(EKeys::LeftControl);
        TapKey(EKeys::O);
        mPendingReleases.Add(EKeys::LeftControl);
        break;
    case EProfileStep::OverlayClose:
        SlateKey(EKeys::Escape);
        break;
    case EProfileStep::SpeedOpen:
        TapKey(EKeys::F3);
        break;
    case EProfileStep::SpeedClose:
        SlateKey(EKeys::F3);
        break;
    case EProfileStep::MapOpen:
        pawn.HandleOpenMap(false);
        break;
    case EProfileStep::ListOpen:
    {
        int32 width = 0;
        int32 height = 0;
        mController->GetViewportSize(width, height);
        pawn.OpenActionList(mBody, FVector2D(width * 0.5, height * 0.5));
        break;
    }
    case EProfileStep::ListClose:
        pawn.CloseActionList();
        break;
    case EProfileStep::MapClose:
        pawn.HandleCloseMap();
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's per-frame work; returns true once the step is done
bool FSOLHoloMenuProfileSmoke::TickStep(const int32 step, ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    const EProfileStep current = static_cast<EProfileStep>(step);
    const int32 pass = PassOfStep(current);
    if (pass != INDEX_NONE)
    {
        return TickPass(pass, pawn, realDeltaSeconds);
    }
    switch (current)
    {
    case EProfileStep::Setup:
        if (!mIsViewApplied)
        {
            mIsViewApplied = ApplyCommandLineView(pawn.GetWorld());
        }
        if (mStepFrames == PROFILE_PLACE_FRAME)
        {
            UWorld* world = pawn.GetWorld();
            mIsShipPlaced = world->GetSubsystem<USOLSimClockSubsystem>() != nullptr && PlaceShipAtStart(world);
        }
        return mStepFrames > PROFILE_PLACE_FRAME && mStepTimeS >= PROFILE_SETUP_S;
    case EProfileStep::RadialOpen:
        return (mInteraction->IsRadialOpen() && mStepTimeS >= PROFILE_OPEN_MIN_S)
            || mStepTimeS >= PROFILE_OPEN_TIMEOUT_S;
    case EProfileStep::OverlayOpen:
        return (pawn.IsOverlayMenuOpen() && mStepTimeS >= PROFILE_OPEN_MIN_S) || mStepTimeS >= PROFILE_OPEN_TIMEOUT_S;
    case EProfileStep::SpeedOpen:
        return (pawn.IsSpeedPanelOpen() && mStepTimeS >= PROFILE_OPEN_MIN_S) || mStepTimeS >= PROFILE_OPEN_TIMEOUT_S;
    case EProfileStep::MapOpen:
        return (pawn.IsMapOpen() && mStepTimeS >= PROFILE_MAP_SETTLE_S) || mStepTimeS >= PROFILE_MAP_TIMEOUT_S;
    case EProfileStep::ListOpen:
        return (pawn.IsActionListOpen() && mStepTimeS >= PROFILE_OPEN_MIN_S) || mStepTimeS >= PROFILE_OPEN_TIMEOUT_S;
    case EProfileStep::Quit:
        return mStepTimeS >= PROFILE_QUIT_S;
    default:
        return mStepTimeS >= PROFILE_STEP_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLHoloMenuProfileSmoke::ExitStep(const int32 step, ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
    const int32 cancels = mInteraction->GetRadialCancelCount() - mCancelsBefore;
    switch (static_cast<EProfileStep>(step))
    {
    case EProfileStep::Setup:
    {
        // The named body's icon; the icon hits are listed by name so a missing icon can be diagnosed
        const USOLBodyRegistrySubsystem* bodies = world->GetSubsystem<USOLBodyRegistrySubsystem>();
        const USOLAnchorSubsystem* anchor = world->GetSubsystem<USOLAnchorSubsystem>();
        const int32 bodyIndex = bodies != nullptr ? bodies->GetRegistry().FindByName(FName(PROFILE_BODY_NAME))
            : INDEX_NONE;
        const int32 anchorIndex = anchor != nullptr ? anchor->GetAnchorIndex() : INDEX_NONE;
        mIsBodyFound = bodyIndex != INDEX_NONE && FindBodyHit(bodyIndex);
        FString hitNames;
        for (const FSOLIconHit& hit : mInteraction->GetIconHits())
        {
            if (hit.Object.Kind == ESOLObjectKind::Body && bodies != nullptr)
            {
                hitNames += FString::Printf(TEXT("%s%s"), hitNames.IsEmpty() ? TEXT("") : TEXT(", "),
                    *bodies->GetRegistry().GetName(hit.Object.Index).ToString());
            }
        }
        const FString anchorName = anchorIndex != INDEX_NONE && bodies != nullptr
            ? bodies->GetRegistry().GetName(anchorIndex).ToString() : FString(TEXT("none"));
        Check(TEXT("a-body-icon"), mIsShipPlaced && mIsBodyFound, FString::Printf(TEXT("ship %s, anchor %s; %s %d "
            "at (%.0f, %.0f); %d icon hits: %s"), mIsShipPlaced ? TEXT("placed") : TEXT("NOT PLACED"), *anchorName,
            PROFILE_BODY_NAME, bodyIndex, mBodyPx.X, mBodyPx.Y, mInteraction->GetIconHits().Num(), *hitNames));
        if (!mIsBodyFound)
        {
            UE_LOG(LogSOL, Error, TEXT("HoloMenuProfile: the %s icon is not on screen at JD %.5f; every pass is "
                "INVALID (FAIL)"), PROFILE_BODY_NAME, PROFILE_SIM_JD);
        }
        break;
    }
    case EProfileStep::GlowZero:
        CheckHudGlow(TEXT("m-glow-0"), PROFILE_GLOW_ZERO, pawn);
        break;
    case EProfileStep::GlowDouble:
        CheckHudGlow(TEXT("m-glow-200"), PROFILE_GLOW_DOUBLE, pawn);
        break;
    case EProfileStep::GlowRevert:
    {
        const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&pawn);
        CheckHudGlow(TEXT("m-glow-revert"), settings != nullptr ? settings->GetSavedValues().HudGlow : -1.0f, pawn);
        break;
    }
    case EProfileStep::PrepTarget:
        Check(TEXT("b-target"), mBody.IsValid() && mTargeting->GetSelectedIndex() == mBody.Index
            && !mInteraction->IsRadialOpen(),
            FString::Printf(TEXT("radial Target: selected %d (body %d)"), mTargeting->GetSelectedIndex(),
            mBody.Index));
        break;
    case EProfileStep::PrepInfo:
        Check(TEXT("c-info"), mInteraction->GetInfoObject() == mBody && mHud.IsValid()
            && mHud->GetLastInfoCardLineCount() > 0, FString::Printf(TEXT("radial Info: card for %d, %d lines"),
            mInteraction->GetInfoObject().Index, mHud.IsValid() ? mHud->GetLastInfoCardLineCount() : 0));
        break;
    case EProfileStep::RadialOpen:
        Check(TEXT("d-radial-open"), mInteraction->IsRadialOpen() && mHud.IsValid()
            && mHud->GetLastRadialSectorCount() > 0 && mInteraction->GetRadial().Highlighted >= 0,
            FString::Printf(TEXT("radial %s, %d sectors drawn, highlighted %d"), mInteraction->IsRadialOpen()
            ? TEXT("open") : TEXT("CLOSED"), mHud.IsValid() ? mHud->GetLastRadialSectorCount() : 0,
            mInteraction->GetRadial().Highlighted));
        break;
    case EProfileStep::RadialClose:
        Check(TEXT("e-radial-close"), !mInteraction->IsRadialOpen() && cancels >= 1 && executed == 0,
            FString::Printf(TEXT("release in the centre: menu %s, %d cancel(s), %d action(s)"),
            mInteraction->IsRadialOpen() ? TEXT("OPEN") : TEXT("closed"), cancels, executed));
        break;
    case EProfileStep::OverlayOpen:
        Check(TEXT("f-overlay-open"), pawn.IsOverlayMenuOpen(), FString::Printf(TEXT("Ctrl+O: Overlay Menu %s"),
            pawn.IsOverlayMenuOpen() ? TEXT("open") : TEXT("CLOSED")));
        break;
    case EProfileStep::OverlayClose:
    {
        const bool bClosed = !pawn.IsOverlayMenuOpen();
        if (!bClosed)
        {
            pawn.CloseOverlayMenu();
        }
        Check(TEXT("g-overlay-close"), bClosed, FString::Printf(TEXT("Esc: Overlay Menu %s"), bClosed
            ? TEXT("closed") : TEXT("STILL OPEN (closed directly)")));
        break;
    }
    case EProfileStep::SpeedOpen:
        Check(TEXT("h-speed-open"), pawn.IsSpeedPanelOpen(), FString::Printf(TEXT("F3: speed panel %s"),
            pawn.IsSpeedPanelOpen() ? TEXT("open") : TEXT("CLOSED")));
        break;
    case EProfileStep::SpeedClose:
    {
        const bool bClosed = !pawn.IsSpeedPanelOpen();
        if (!bClosed)
        {
            pawn.CloseSpeedPanel();
        }
        Check(TEXT("i-speed-close"), bClosed, FString::Printf(TEXT("F3: speed panel %s"), bClosed ? TEXT("closed")
            : TEXT("STILL OPEN (closed directly)")));
        break;
    }
    case EProfileStep::MapOpen:
        Check(TEXT("j-map-open"), pawn.IsMapOpen(), FString::Printf(TEXT("map %s"), pawn.IsMapOpen() ? TEXT("open")
            : TEXT("CLOSED")));
        break;
    case EProfileStep::ListOpen:
        Check(TEXT("k-list-open"), pawn.IsMapOpen() && pawn.IsActionListOpen(), FString::Printf(TEXT("list menu %s"),
            pawn.IsActionListOpen() ? TEXT("open") : TEXT("CLOSED")));
        break;
    case EProfileStep::MapClose:
        Check(TEXT("l-map-close"), !pawn.IsMapOpen() && !pawn.IsActionListOpen(), FString::Printf(TEXT("map %s, "
            "list %s"), pawn.IsMapOpen() ? TEXT("OPEN") : TEXT("closed"), pawn.IsActionListOpen() ? TEXT("OPEN")
            : TEXT("closed")));
        break;
    case EProfileStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("HoloMenuProfile SUMMARY %d passes: %d PASS, %d FAIL, %d NOBASE; %d/%d checks "
            "passed"), PROFILE_PASS_COUNT, PROFILE_PASS_COUNT - mPassesFailed - mPassesNoBase, mPassesFailed,
            mPassesNoBase, mChecksPassed, mChecksRun);
        mCapture.Stop(world);
        pawn.SetFlightCursorScripted(false);
        UKismetSystemLibrary::QuitGame(world, nullptr, EQuitPreference::Quit, false);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of a capture pass; returns true once its result line is logged
bool FSOLHoloMenuProfileSmoke::TickPass(const int32 pass, ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    UWorld* world = pawn.GetWorld();
    mPhaseTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    if (mPhase == EPassPhase::Capture && mCapture.IsCapturing())
    {
        ++mMenuFrames;
        mMenuOpenFrames += IsPassMenuOpen(pass, pawn) ? 1 : 0;
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
        if (mPhaseTimeS >= PROFILE_SETTLE_S)
        {
            TakeUiShot(*FString::Printf(TEXT("P%d"), pass));
            nextPhase(EPassPhase::Shot);
        }
        return false;
    case EPassPhase::Shot:
        if (mPhaseFrames >= PROFILE_SHOT_FRAMES)
        {
            mCapture.BeginPass();
            nextPhase(EPassPhase::Capture);
        }
        return false;
    case EPassPhase::Capture:
    {
        // Every capture (each ProfileGPU frame and each stat dumpave window) after a card refresh and its open time
        const auto canStart = [this]()
        {
            if (!RefreshAndWait())
            {
                return false;
            }
            mCardRefreshAtS = -1.0;
            return true;
        };
        if (mCapture.TickPass(world, realDeltaSeconds, canStart))
        {
            LogPassResult(pass, pawn);
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
// Returns whether the pass's menu is open (P0: no menu open)
bool FSOLHoloMenuProfileSmoke::IsPassMenuOpen(const int32 pass, const ASOLShipPawn& pawn) const
{
    switch (pass)
    {
    case 0:
    case 5:
    case 6:
        return !mInteraction->IsRadialOpen() && !pawn.IsOverlayMenuOpen() && !pawn.IsSpeedPanelOpen()
            && !pawn.IsMapOpen();
    case 1:
        return mInteraction->IsRadialOpen();
    case 2:
        return pawn.IsOverlayMenuOpen();
    case 3:
        return pawn.IsSpeedPanelOpen();
    case 4:
        return pawn.IsMapOpen() && pawn.IsActionListOpen();
    default:
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows the info card again (the same Execute the radial and list menus run), so it outlives a long pass
void FSOLHoloMenuProfileSmoke::RefreshInfoCard() const
{
    if (PassHasCards(PassOfStep(static_cast<EProfileStep>(mStep))) && mBody.IsValid())
    {
        mInteraction->Execute(mBody, ESOLAction::Info);
    }
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the info card once, then returns true once its open time (plus a margin) of real time has passed
bool FSOLHoloMenuProfileSmoke::RefreshAndWait()
{
    const double nowS = FPlatformTime::Seconds();
    if (mCardRefreshAtS < 0.0)
    {
        RefreshInfoCard();
        mCardRefreshAtS = nowS;
        return false;
    }
    return nowS - mCardRefreshAtS >= PROFILE_CARD_WAIT_S;
}

//////////////////////////////////////////////////////////////////////////
// Places the ship at the default start above Earth for the pinned date, at rest, facing Earth; false if no Earth
bool FSOLHoloMenuProfileSmoke::PlaceShipAtStart(UWorld* world) const
{
    // The ship spawn's start (USOLShipSubsystem::SpawnPlayerShip) at the body positions of the pinned date: the
    // default altitude on Earth's Sun-facing side, then at rest in the reference frame, facing Earth, not rotating
    const USOLBodyRegistrySubsystem* bodies = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    if (bodies == nullptr)
    {
        return false;
    }
    const int32 earth = bodies->GetRegistry().FindByName(FName(SOL::BodyNames::EARTH));
    const int32 sun = bodies->GetRegistry().FindByName(FName(SOL::BodyNames::SUN));
    const FSOLBodyFrameCache& cache = bodies->GetUnrealFrameCache();
    if (!cache.PositionsM.IsValidIndex(earth) || !cache.PositionsM.IsValidIndex(sun))
    {
        return false;
    }
    FSOLShipState state = SOLFlight::MakeCircularOrbitState(cache.PositionsM[earth], cache.VelocitiesMps[earth],
        cache.GMs[earth], cache.RadiiM[earth], SOL::DEFAULT_SPAWN_ALTITUDE_M, cache.PositionsM[sun]
        - cache.PositionsM[earth]);
    const FVector3d forward = (cache.PositionsM[earth] - state.PositionM).GetSafeNormal();
    if (forward.IsNearlyZero())
    {
        return false;
    }
    state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(forward).ToQuat();
    state.VelocityMps = cache.VelocitiesMps[earth];
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    mShips->SetState(state);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Logs the pass's result line with its verdict against the baseline
void FSOLHoloMenuProfileSmoke::LogPassResult(const int32 pass, const ASOLShipPawn& pawn)
{
    // The median ProfileGPU frame of this pass, by the game window's UI pass time
    const FSOLUiProfileCapture::FPassResult& result = mCapture.GetResult();
    const FSOLUiProfileCapture::FGpuCapture& gpu = result.Gpu;

    // The game-thread total: the HUD's DrawHUD plus the whole Slate paint (Game UI Paint is inside the latter)
    const double cpuHud = result.CpuMs[0];
    const double cpuSlate = result.CpuMs[1];
    const double cpuGameUi = result.CpuMs[2];
    const double cpuTotal = cpuHud >= 0.0 && cpuSlate >= 0.0 ? cpuHud + cpuSlate : -1.0;
    const bool bCards = PassHasCards(pass);
    const bool bTarget = mBody.IsValid() && mTargeting->GetSelectedIndex() == mBody.Index;
    const int32 infoLines = mHud.IsValid() ? mHud->GetLastInfoCardLineCount() : 0;
    const bool bCardsAsWanted = bCards ? bTarget && infoLines > 0 : !bTarget && infoLines == 0;
    int32 width = 0;
    int32 height = 0;
    mController->GetViewportSize(width, height);

    // Valid in the baseline's conditions: the fixed scene with its body, a standalone game, every number read, the
    // pass's menu open on every capture frame and the cards as the pass wants them. Only a 1280x720 view is compared
    // with the baseline; at another size (a view run, SDD 37 section 8) the numbers are logged as NOBASE
    const bool bValid = mIsShipPlaced && mIsBodyFound && !GIsEditor && cpuTotal >= 0.0 && gpu.UiMs >= 0.0
        && mMenuFrames > 0 && mMenuOpenFrames == mMenuFrames && bCardsAsWanted;
    const bool bBaseView = width == PROFILE_VIEW_WIDTH && height == PROFILE_VIEW_HEIGHT;
    const UGameUserSettings* userSettings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
    const bool bVSync = userSettings != nullptr && userSettings->IsVSyncEnabled();
    const int32 scalability = userSettings != nullptr ? userSettings->GetOverallScalabilityLevel() : INDEX_NONE;

    // The verdict against the pre-conversion baseline (budget: under 0.1 ms extra CPU and GPU)
    const double baseCpu = PROFILE_BASELINE_CPU_MS[pass];
    const double baseGpu = PROFILE_BASELINE_GPU_MS[pass];
    const bool bHasBase = baseCpu > 0.0 && baseGpu > 0.0 && bBaseView;
    const double deltaCpu = bHasBase ? cpuTotal - baseCpu : 0.0;
    const double deltaGpu = bHasBase ? gpu.UiMs - baseGpu : 0.0;
    const TCHAR* verdict = !bValid ? TEXT("FAIL") : !bHasBase ? TEXT("NOBASE")
        : deltaCpu < PROFILE_BUDGET_MS && deltaGpu < PROFILE_BUDGET_MS ? TEXT("PASS") : TEXT("FAIL");
    mPassesFailed += FCString::Strcmp(verdict, TEXT("FAIL")) == 0 ? 1 : 0;
    mPassesNoBase += FCString::Strcmp(verdict, TEXT("NOBASE")) == 0 ? 1 : 0;
    const double frameMs = result.FrameMs;
    const float hudGlowScale = mHud.IsValid() ? mHud->GetShapeGlowScale() : -1.0f;
    UE_LOG(LogSOL, Log, TEXT("HoloMenuProfile RESULT P%d %s pass=%s cpu_hud=%.3f cpu_slate=%.3f cpu_gameui=%.3f "
        "cpu_total=%.3f gpu_ui=%.3f gpu_slate=%.3f gpu_shapes=%.3f gpu_frame=%.3f ui_draws=%d gpu_ui_all=%s "
        "frame_ms=%.2f view=%dx%d vsync=%s scalability=%d editor=%s valid=%s jd=%.5f body=%s menu_frames=%d/%d "
        "target=%s info_lines=%d base_cpu=%.3f base_gpu=%.3f dcpu=%+.3f dgpu=%+.3f budget=%.3f map=%s "
        "glow_scale=%.3f"), pass,
        verdict, PROFILE_PASS_NAMES[pass], cpuHud, cpuSlate, cpuGameUi, cpuTotal, gpu.UiMs, gpu.SlateMs, gpu.MaterialMs,
        gpu.FrameMs, gpu.UiDraws, *result.GpuUiAll, frameMs, width, height, bVSync ? TEXT("on") : TEXT("off"), scalability,
        GIsEditor ? TEXT("yes") : TEXT("no"), bValid ? TEXT("yes") : TEXT("no"), PROFILE_SIM_JD,
        mIsBodyFound ? PROFILE_BODY_NAME : TEXT("MISSING"), mMenuOpenFrames, mMenuFrames, bTarget ? TEXT("yes")
        : TEXT("no"), infoLines, baseCpu, baseGpu, deltaCpu, deltaGpu, PROFILE_BUDGET_MS, pawn.IsMapOpen()
        ? TEXT("open") : TEXT("closed"), hudGlowScale);
}

//////////////////////////////////////////////////////////////////////////
// Checks that the HUD pushed the glow scale of a Settings HUD glow value to its material (SDD 37 section 8)
void FSOLHoloMenuProfileSmoke::CheckHudGlow(const TCHAR* label, const float setting, const ASOLShipPawn& pawn)
{
    const float expected = SOLHudShapeMath::HudGlowScale(setting);
    const float pushed = mHud.IsValid() ? mHud->GetShapeGlowScale() : -1.0f;
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&pawn);
    Check(label, setting >= 0.0f && settings != nullptr
        && FMath::IsNearlyEqual(pushed, expected, PROFILE_GLOW_TOLERANCE), FString::Printf(TEXT("HUD glow %.0f%%: "
        "glow scale %.4f pushed (want %.4f)"), setting * 100.0f, pushed, expected));
}

//////////////////////////////////////////////////////////////////////////
// Finds the icon hit of the named profile body; false if its icon is not drawn
bool FSOLHoloMenuProfileSmoke::FindBodyHit(const int32 bodyIndex)
{
    for (const FSOLIconHit& hit : mInteraction->GetIconHits())
    {
        if (hit.Object.Kind == ESOLObjectKind::Body && hit.Object.Index == bodyIndex)
        {
            mBody = hit.Object;
            mBodyPx = hit.CenterPx;
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the current screen centre of the body's icon (the last one found when it is not drawn now)
FVector2D FSOLHoloMenuProfileSmoke::GetBodyIconPx() const
{
    for (const FSOLIconHit& hit : mInteraction->GetIconHits())
    {
        if (hit.Object == mBody)
        {
            return hit.CenterPx;
        }
    }
    return mBodyPx;
}

//////////////////////////////////////////////////////////////////////////
// Presses Fire on the body's icon (opens the radial menu)
void FSOLHoloMenuProfileSmoke::PressOnBody(ASOLShipPawn& pawn) const
{
    pawn.HandleFlightCursorMoved(GetBodyIconPx());
    mInteraction->UpdateHover(pawn.GetFlightCursorPx(), true);
    pawn.HandleFire(true);
}

//////////////////////////////////////////////////////////////////////////
// Moves the cursor onto an action's sector of the open radial menu (the middle of the sector ring)
bool FSOLHoloMenuProfileSmoke::DragToAction(ASOLShipPawn& pawn, const ESOLAction action) const
{
    const FSOLRadialMenuState& radial = mInteraction->GetRadial();
    const FSOLActionList& actions = mInteraction->GetRadialActions();
    const float uiScale = mHud.IsValid() ? mHud->GetUiScale() : 1.0f;
    for (int32 index = 0; radial.bOpen && index < actions.Num; ++index)
    {
        if (actions.Entries[index].Action == action)
        {
            const double angle = SOLRadialMenu::SectorMidAngleRad(index, radial.Count);
            const double radiusPx = 0.5 * (SOL::Interact::RADIAL_INNER_PX + SOL::Interact::RADIAL_OUTER_PX) * uiScale;
            pawn.HandleFlightCursorMoved(radial.CenterPx + FVector2D(FMath::Cos(angle), FMath::Sin(angle)) * radiusPx);
            return true;
        }
    }
    UE_LOG(LogSOL, Warning, TEXT("HoloMenuProfile: the radial menu does not offer %s"),
        SOLActionTable::ActionLabel(action));
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a radial action on the body: press on the icon, drag to the sector, release
void FSOLHoloMenuProfileSmoke::RadialAction(ASOLShipPawn& pawn, const ESOLAction action) const
{
    PressOnBody(pawn);
    DragToAction(pawn, action);
    mInteraction->MovePointer(pawn.GetFlightCursorPx());
    pawn.HandleFire(false);
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller (game input)
void FSOLHoloMenuProfileSmoke::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller (game input)
void FSOLHoloMenuProfileSmoke::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a game key now and releases it on the next frame
void FSOLHoloMenuProfileSmoke::TapKey(const FKey& key)
{
    PressKey(key);
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press and release through Slate to the focused widget
void FSOLHoloMenuProfileSmoke::SlateKey(const FKey& key)
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication& slate = FSlateApplication::Get();
        const FKeyEvent keyEvent(key, FModifierKeysState(), slate.GetUserIndexForKeyboard(), false, 0, 0);
        slate.ProcessKeyDownEvent(keyEvent);
        slate.ProcessKeyUpEvent(keyEvent);
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot with the UI to the screenshot folder (Saved/Screenshots/<platform>)
void FSOLHoloMenuProfileSmoke::TakeUiShot(const TCHAR* name)
{
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("HoloMenuProfile_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("HoloMenuProfile: screenshot with UI %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLHoloMenuProfileSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("HoloMenuProfile CHECK %-16s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
