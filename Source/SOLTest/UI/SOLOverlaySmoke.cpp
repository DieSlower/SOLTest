/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOverlaySmoke.h"

#include "Menu/SOLSettingsSubsystem.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHologramCursorSubsystem.h"
#include "UI/SOLOverlayMenuWidget.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // The script's steps, in order
    enum class EOverlayStep : int32
    {
        Setup,              // Let the HUD draw a few frames with the overlays on
        MasterOff,          // O
        MasterOn,           // O again
        OrbitLinesOff,      // Alt+O
        OrbitLinesOn,       // Alt+O again
        Darkening,          // Shift+O
        DarkeningBack,      // Shift+O again
        MenuOpen,           // Ctrl+O
        MenuClick,          // A Slate click on the Planets row
        MenuClose,          // Esc through Slate
        FinalShot,          // Screenshot with the icons
        Quit,
        Count,
    };

    // Step durations (real seconds) and the minimum frames per step
    constexpr double OVERLAYSMOKE_SETUP_S = 3.0;
    constexpr double OVERLAYSMOKE_STEP_S = 0.6;
    constexpr double OVERLAYSMOKE_SHOT_S = 1.5;
    constexpr double OVERLAYSMOKE_QUIT_S = 3.0;
    constexpr int32 OVERLAYSMOKE_MIN_FRAMES = 5;
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, HUD and settings and starts the first step
void FSOLOverlaySmoke::Start(ASOLShipPawn& pawn)
{
    mController = pawn.GetController<APlayerController>();
    mHud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    mSettings = USOLSettingsSubsystem::Get(&pawn);
    UE_LOG(LogSOL, Log, TEXT("SmokeOverlay: scripted overlay input starting (settings slot %s)"),
        mSettings.IsValid() ? *mSettings->GetSlotName() : TEXT("none"));
    mStep = 0;
    EnterStep(mStep, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLOverlaySmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mStep >= static_cast<int32>(EOverlayStep::Count))
    {
        return true;
    }
    if (!mController.IsValid() || !mSettings.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeOverlay: controller or settings missing; aborting"));
        mStep = static_cast<int32>(EOverlayStep::Count);
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

    // Each step lasts its duration and at least a few frames, then checks and moves on
    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    const EOverlayStep step = static_cast<EOverlayStep>(mStep);
    const double durationS = step == EOverlayStep::Setup ? OVERLAYSMOKE_SETUP_S
        : step == EOverlayStep::FinalShot || step == EOverlayStep::MenuOpen ? OVERLAYSMOKE_SHOT_S
        : step == EOverlayStep::Quit ? OVERLAYSMOKE_QUIT_S : OVERLAYSMOKE_STEP_S;
    if (mStepTimeS < durationS || mStepFrames < OVERLAYSMOKE_MIN_FRAMES)
    {
        return false;
    }
    ExitStep(mStep, pawn);
    ++mStep;
    mStepTimeS = 0.0;
    mStepFrames = 0;
    if (mStep >= static_cast<int32>(EOverlayStep::Count))
    {
        return true;
    }
    EnterStep(mStep, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLOverlaySmoke::EnterStep(const int32 step, ASOLShipPawn& pawn)
{
    switch (static_cast<EOverlayStep>(step))
    {
    case EOverlayStep::MasterOff:
    case EOverlayStep::MasterOn:
        PressKey(EKeys::O);
        mPendingReleases.Add(EKeys::O);
        break;
    case EOverlayStep::OrbitLinesOff:
    case EOverlayStep::OrbitLinesOn:
        mWasOrbitLinesShown = mSettings->AreOrbitLinesShown();
        TapChord(EKeys::LeftAlt, EKeys::O);
        break;
    case EOverlayStep::Darkening:
    case EOverlayStep::DarkeningBack:
        mWasDarkening = SOLOverlay::IsEnabled(mSettings->GetOverlayPrefs(), SOLOverlay::EOverlay::Darkening);
        TapChord(EKeys::LeftShift, EKeys::O);
        break;
    case EOverlayStep::MenuOpen:
        TapChord(EKeys::LeftControl, EKeys::O);
        break;
    case EOverlayStep::MenuClick:
    {
        // A real Slate click at the Planets row's centre: the pointer's hot spot is where the click lands
        mWasPlanetsChecked = SOLOverlay::IsEnabled(mSettings->GetOverlayPrefs(), SOLOverlay::EOverlay::IconPlanets);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        if (menu != nullptr && FSlateApplication::IsInitialized())
        {
            FSlateApplication& slate = FSlateApplication::Get();
            const FVector2D at = menu->GetRowScreenCenter(static_cast<int32>(SOLOverlay::EOverlay::IconPlanets));
            slate.SetCursorPos(at);
            const FPointerEvent down(slate.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, at, at,
                TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
            slate.ProcessMouseButtonDownEvent(nullptr, down);
            const FPointerEvent up(slate.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, at, at,
                TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
            slate.ProcessMouseButtonUpEvent(up);
            UE_LOG(LogSOL, Log, TEXT("SmokeOverlay: Slate click at (%.0f, %.0f)"), at.X, at.Y);
        }
        TakeUiShot(TEXT("Menu"));
        break;
    }
    case EOverlayStep::MenuClose:
    {
        mSavesBefore = mSettings->GetSaveRequestCount();
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication& slate = FSlateApplication::Get();
            const FKeyEvent keyEvent(EKeys::Escape, FModifierKeysState(), slate.GetUserIndexForKeyboard(), false, 0,
                0);
            slate.ProcessKeyDownEvent(keyEvent);
            slate.ProcessKeyUpEvent(keyEvent);
        }
        break;
    }
    case EOverlayStep::FinalShot:
        TakeUiShot(TEXT("Icons"));
        break;
    case EOverlayStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("SmokeOverlay: %d/%d checks passed"), mChecksPassed, mChecksRun);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLOverlaySmoke::ExitStep(const int32 step, ASOLShipPawn& pawn)
{
    const ASOLFlightHud* hud = mHud.Get();
    const int32 icons = hud != nullptr ? hud->GetIconLayer().GetLastIconCount() : 0;
    const int32 arrows = hud != nullptr ? hud->GetIconLayer().GetLastArrowCount() : 0;
    const ASOLOrbitLineVisuals* lines = hud != nullptr ? hud->GetOrbitLineVisuals() : nullptr;
    const int32 visibleLines = lines != nullptr ? lines->CountVisibleLines() : 0;
    const bool bMaster = mSettings->IsOverlayMasterOn();
    switch (static_cast<EOverlayStep>(step))
    {
    case EOverlayStep::Setup:
    {
        mStartIcons = icons;
        Check(TEXT("a-start-on"), bMaster && icons + arrows > 0, FString::Printf(TEXT("flight start: master %s, %d "
            "icons, %d edge arrows, %d orbit lines, %d NPC ship contacts"), bMaster ? TEXT("on") : TEXT("off"),
            icons, arrows, visibleLines, hud != nullptr ? hud->GetLastRadarShipCount() : 0));
        const USOLHologramCursorSubsystem* cursor = pawn.GetWorld()->GetSubsystem<USOLHologramCursorSubsystem>();
        const UGameViewportClient* viewport = pawn.GetWorld()->GetGameViewport();
        const bool bInstalled = cursor != nullptr && cursor->GetCursorWidget().IsValid() && viewport != nullptr
            && viewport->GetSoftwareCursorWidget(EMouseCursor::Default) == cursor->GetCursorWidget();
        const int32 paints = bInstalled ? cursor->GetCursorWidget()->GetPaintCount() : 0;
        Check(TEXT("a-cursor"), bInstalled && paints > 0, FString::Printf(TEXT("hologram pointer %s as the software "
            "cursor, painted %d times"), bInstalled ? TEXT("installed") : TEXT("NOT installed"), paints));
        TakeUiShot(TEXT("Start"));
        break;
    }
    case EOverlayStep::MasterOff:
        Check(TEXT("b-master-off"), !bMaster && icons == 0 && arrows == 0 && visibleLines == 0,
            FString::Printf(TEXT("O: master %s, %d icons, %d arrows, %d orbit lines"), bMaster ? TEXT("on")
            : TEXT("off"), icons, arrows, visibleLines));
        break;
    case EOverlayStep::MasterOn:
        Check(TEXT("c-master-on"), bMaster && icons == mStartIcons, FString::Printf(TEXT("O again: master %s, %d "
            "icons (start %d), %d orbit lines"), bMaster ? TEXT("on") : TEXT("off"), icons, mStartIcons,
            visibleLines));
        break;
    case EOverlayStep::OrbitLinesOff:
    case EOverlayStep::OrbitLinesOn:
    {
        const bool bShown = mSettings->AreOrbitLinesShown();
        Check(step == static_cast<int32>(EOverlayStep::OrbitLinesOff) ? TEXT("d-alt-o-off") : TEXT("d-alt-o-on"),
            bShown != mWasOrbitLinesShown && bMaster && !pawn.IsFreeLooking() && (bShown == (visibleLines > 0)),
            FString::Printf(TEXT("Alt+O: orbit lines %s -> %s (%d visible), master %s, free look %s"),
            mWasOrbitLinesShown ? TEXT("on") : TEXT("off"), bShown ? TEXT("on") : TEXT("off"), visibleLines,
            bMaster ? TEXT("on") : TEXT("off"), pawn.IsFreeLooking() ? TEXT("ON") : TEXT("off")));
        break;
    }
    case EOverlayStep::Darkening:
    case EOverlayStep::DarkeningBack:
    {
        const bool bDark = SOLOverlay::IsEnabled(mSettings->GetOverlayPrefs(), SOLOverlay::EOverlay::Darkening);
        Check(step == static_cast<int32>(EOverlayStep::Darkening) ? TEXT("e-shift-o") : TEXT("e-shift-o-back"),
            bDark != mWasDarkening && bMaster, FString::Printf(TEXT("Shift+O: darkening %s -> %s, master %s"),
            mWasDarkening ? TEXT("on") : TEXT("off"), bDark ? TEXT("on") : TEXT("off"), bMaster ? TEXT("on")
            : TEXT("off")));
        break;
    }
    case EOverlayStep::MenuOpen:
    {
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const int32 rows = menu != nullptr ? menu->GetRowCount() : 0;
        Check(TEXT("f-menu-open"), pawn.IsOverlayMenuOpen() && rows == SOLOverlay::OVERLAY_COUNT && bMaster,
            FString::Printf(TEXT("Ctrl+O: menu %s, %d rows (table %d), master %s"), pawn.IsOverlayMenuOpen()
            ? TEXT("open") : TEXT("closed"), rows, SOLOverlay::OVERLAY_COUNT, bMaster ? TEXT("on") : TEXT("off")));
        break;
    }
    case EOverlayStep::MenuClick:
    {
        const bool bChecked = SOLOverlay::IsEnabled(mSettings->GetOverlayPrefs(), SOLOverlay::EOverlay::IconPlanets);
        const int32 planets = hud != nullptr
            ? hud->GetIconLayer().GetLastIconCount(SOLOverlayIcons::EIconCategory::Planet) : 0;
        Check(TEXT("g-menu-click"), bChecked != mWasPlanetsChecked && (bChecked || planets == 0),
            FString::Printf(TEXT("click on Planets: %s -> %s, %d planet icons"), mWasPlanetsChecked ? TEXT("on")
            : TEXT("off"), bChecked ? TEXT("on") : TEXT("off"), planets));
        break;
    }
    case EOverlayStep::MenuClose:
    {
        const int32 saves = mSettings->GetSaveRequestCount() - mSavesBefore;
        Check(TEXT("h-menu-close"), !pawn.IsOverlayMenuOpen() && saves == 1, FString::Printf(TEXT("Esc: menu %s, "
            "%d settings write(s) at the close"), pawn.IsOverlayMenuOpen() ? TEXT("open") : TEXT("closed"), saves));
        break;
    }
    case EOverlayStep::Quit:
        UKismetSystemLibrary::QuitGame(pawn.GetWorld(), nullptr, EQuitPreference::Quit, false);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller (game input)
void FSOLOverlaySmoke::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller (game input)
void FSOLOverlaySmoke::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a key with a modifier held; both are released on the next frame
void FSOLOverlaySmoke::TapChord(const FKey& modifier, const FKey& key)
{
    PressKey(modifier);
    PressKey(key);
    mPendingReleases.Add(key);
    mPendingReleases.Add(modifier);
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot with the UI to Saved/Screenshots
void FSOLOverlaySmoke::TakeUiShot(const TCHAR* name)
{
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("OverlaySmoke_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeOverlay: screenshot with UI %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLOverlaySmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeOverlay CHECK %-16s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
