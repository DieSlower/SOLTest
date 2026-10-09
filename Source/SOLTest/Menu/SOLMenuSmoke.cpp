/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuSmoke.h"

#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuCameraPawn.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLPauseMenuWidget.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    const TCHAR* const MENUSMOKE_FLOW_SCENARIO = TEXT("flow");   // Checks only, no screenshots
    const TCHAR* const MENUSMOKE_SHOT_PREFIX = TEXT("MenuSmoke_");
    const TCHAR* const MENUSMOKE_SHOT_EXTENSION = TEXT(".png");
    constexpr double MENUSMOKE_SHOT_SETTLE_S = 0.5;               // After a screenshot request, before the next phase
    constexpr double MENUSMOKE_MIN_THRUST_GAIN_MPS = 50.0;        // Speed the 2.5 s of forward thrust must add
}

//////////////////////////////////////////////////////////////////////////
// Reads the scenario and starts the first phase
void FSOLMenuSmoke::Start(USOLMenuSubsystem& menu, const FString& scenario)
{
    mController = menu.GetPlayerController();
    mTakesShots = !scenario.Equals(MENUSMOKE_FLOW_SCENARIO, ESearchCase::IgnoreCase);
    mPendingReleases.Reserve(4);
    UE_LOG(LogSOL, Log, TEXT("MenuSmoke: start (scenario '%s', screenshots %s)"), *scenario,
        mTakesShots ? TEXT("on") : TEXT("off"));
    mPhase = EPhase::MainMenu;
    mPhaseElapsedS = 0.0;
    EnterPhase(mPhase, menu);
}

//////////////////////////////////////////////////////////////////////////
// Runs one step of the script; returns true once it has finished (and requested the quit)
bool FSOLMenuSmoke::Update(USOLMenuSubsystem& menu, const double realDeltaSeconds)
{
    if (!mController.IsValid())
    {
        mController = menu.GetPlayerController();
        if (!mController.IsValid())
        {
            return false;
        }
    }

    // Keys pressed last step are released now
    for (const FKey& key : mPendingReleases)
    {
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
    }
    mPendingReleases.Reset();

    mPhaseElapsedS += realDeltaSeconds;
    if (mPhaseElapsedS < GetPhaseDuration(mPhase))
    {
        return false;
    }

    // Phase over: checks, then the screenshot (and its settle time), then the next phase
    if (!mIsShotPending)
    {
        ExitPhase(mPhase, menu);
        const TCHAR* shot = GetPhaseShot(mPhase);
        if (mTakesShots && shot != nullptr)
        {
            TakeShot(shot);
            mIsShotPending = true;
            mPhaseElapsedS = GetPhaseDuration(mPhase) - MENUSMOKE_SHOT_SETTLE_S;
            return false;
        }
    }
    mIsShotPending = false;
    mPhase = static_cast<EPhase>(static_cast<uint8>(mPhase) + 1);
    mPhaseElapsedS = 0.0;
    if (mPhase == EPhase::Done)
    {
        UE_LOG(LogSOL, Log, TEXT("MenuSmoke: done, %d passed, %d failed"), mPassed, mFailed);
        UKismetSystemLibrary::QuitGame(menu.GetWorld(), nullptr, EQuitPreference::Quit, false);
        return true;
    }
    EnterPhase(mPhase, menu);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions
void FSOLMenuSmoke::EnterPhase(const EPhase phase, USOLMenuSubsystem& menu)
{
    const USOLBodyRegistrySubsystem* registry = menu.GetWorld()->GetSubsystem<USOLBodyRegistrySubsystem>();
    const auto bodyIndex = [registry](const TCHAR* name)
    {
        return registry != nullptr ? registry->GetRegistry().FindByName(FName(name)) : INDEX_NONE;
    };
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    ASOLShipPawn* shipPawn = mController.IsValid() ? Cast<ASOLShipPawn>(mController->GetPawn()) : nullptr;
    switch (phase)
    {
    case EPhase::StartPicker:
        menu.SetStartBodyIndex(bodyIndex(SOL::BodyNames::MARS));
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::StartLocation);
        }
        break;
    case EPhase::MainControls:
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::Controls);
        }
        break;
    case EPhase::MainAbout:
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::About);
        }
        break;
    case EPhase::Play:
        menu.SetStartBodyIndex(bodyIndex(SOL::BodyNames::EARTH));
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::None);
        }
        menu.RequestPlay();
        break;
    case EPhase::Thrust:
        mStartSpeedMps = GetRelativeSpeedMps(menu);
        if (shipPawn != nullptr)
        {
            shipPawn->HandleThrust(FVector3d(1.0, 0.0, 0.0));
        }
        break;
    case EPhase::Pause:
    case EPhase::PauseAgain:
        TapKey(EKeys::Escape);
        break;
    case EPhase::TabControls:
        menu.SelectTab(SOLMenuState::EMenuTab::Controls);
        break;
    case EPhase::TabInfo:
        menu.SelectTab(SOLMenuState::EMenuTab::Info);
        break;
    case EPhase::TabSettings:
        menu.SelectTab(SOLMenuState::EMenuTab::Settings);
        break;
    case EPhase::TabAbout:
        menu.SelectTab(SOLMenuState::EMenuTab::About);
        break;
    case EPhase::TabQuit:
        menu.SelectTab(SOLMenuState::EMenuTab::Quit);
        break;
    case EPhase::ResumeSlate:
        mSlateKeysBefore = menu.GetPauseMenu() != nullptr ? menu.GetPauseMenu()->GetSlateKeyCount() : 0;
        SlateKey(EKeys::Escape);
        break;
    case EPhase::MapOpen:
        TapKey(EKeys::J);
        break;
    case EPhase::MapEscape:
        TapKey(EKeys::Escape);
        break;
    case EPhase::QuitToMenu:
    case EPhase::QuitToMenuAgain:
        menu.RequestQuitToMenu();
        break;
    case EPhase::PlayAgain:
    case EPhase::PlayThird:
        menu.RequestPlay();
        break;
    case EPhase::PauseThird:
        menu.RequestEscape();
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks
void FSOLMenuSmoke::ExitPhase(const EPhase phase, USOLMenuSubsystem& menu)
{
    using SOLMenuState::EMenuMode;
    ASOLShipPawn* shipPawn = mController.IsValid() ? Cast<ASOLShipPawn>(mController->GetPawn()) : nullptr;
    switch (phase)
    {
    case EPhase::MainMenu:
        CheckMainMenu(menu, TEXT("starts in the main menu"));
        Check(TEXT("main menu widget shown"), menu.GetMainMenu() != nullptr && menu.GetMainMenu()->IsInViewport(),
            FString());
        break;
    case EPhase::StartPicker:
        Check(TEXT("picker sets the start body"), menu.GetStartBodyName() == SOL::BodyNames::MARS,
            menu.GetStartBodyName());
        break;
    case EPhase::Play:
        CheckFlight(menu, TEXT("Play"), 1);
        break;
    case EPhase::Thrust:
    {
        const double speedMps = GetRelativeSpeedMps(menu);
        Check(TEXT("thrust flies the ship"), speedMps - mStartSpeedMps > MENUSMOKE_MIN_THRUST_GAIN_MPS,
            FString::Printf(TEXT("%.1f -> %.1f m/s"), mStartSpeedMps, speedMps));
        if (shipPawn != nullptr)
        {
            shipPawn->HandleThrust(FVector3d::ZeroVector);
        }
        break;
    }
    case EPhase::Pause:
        CheckPaused(menu, TEXT("Esc pauses"));
        Check(TEXT("sim keeps running while paused"), menu.GetWorld()->IsPaused() == false, FString());
        break;
    case EPhase::TabQuit:
        Check(TEXT("tab selection"), menu.GetState().Tab == SOLMenuState::EMenuTab::Quit, FString());
        break;
    case EPhase::ResumeSlate:
    {
        const int32 slateKeys = menu.GetPauseMenu() != nullptr ? menu.GetPauseMenu()->GetSlateKeyCount() : 0;
        Check(TEXT("Esc reached the pause menu through Slate"), slateKeys > mSlateKeysBefore,
            FString::Printf(TEXT("%d -> %d"), mSlateKeysBefore, slateKeys));
        CheckFlight(menu, TEXT("Esc resumes"), 1);
        break;
    }
    case EPhase::MapOpen:
        Check(TEXT("J opens the map"), shipPawn != nullptr && shipPawn->IsMapOpen() && menu.GetState().bMapOpen,
            FString());
        break;
    case EPhase::MapEscape:
        Check(TEXT("Esc closes the map and does not pause"), shipPawn != nullptr && !shipPawn->IsMapOpen()
            && !menu.GetState().bMapOpen && menu.GetState().Mode == EMenuMode::Playing, FString::Printf(
            TEXT("mode %d"), static_cast<int32>(menu.GetState().Mode)));
        break;
    case EPhase::PauseAgain:
        CheckPaused(menu, TEXT("Esc pauses again"));
        break;
    case EPhase::PauseThird:
        CheckPaused(menu, TEXT("pause before the second quit"));
        break;
    case EPhase::QuitToMenu:
        CheckMainMenu(menu, TEXT("Quit to menu"));
        break;
    case EPhase::QuitToMenuAgain:
        CheckMainMenu(menu, TEXT("second Quit to menu"));
        break;
    case EPhase::PlayAgain:
        CheckFlight(menu, TEXT("Play again"), 2);
        break;
    case EPhase::PlayThird:
        CheckFlight(menu, TEXT("Play a third time"), 3);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase in seconds
double FSOLMenuSmoke::GetPhaseDuration(const EPhase phase)
{
    switch (phase)
    {
    case EPhase::MainMenu:
        return 10.0;
    case EPhase::StartPicker:
        return 3.0;
    case EPhase::Play:
    case EPhase::QuitToMenu:
    case EPhase::QuitToMenuAgain:
    case EPhase::PlayAgain:
    case EPhase::PlayThird:
        return 4.0;
    case EPhase::Thrust:
        return 2.5;
    default:
        return 1.5;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the screenshot name of a phase, or nullptr when it takes none
const TCHAR* FSOLMenuSmoke::GetPhaseShot(const EPhase phase)
{
    switch (phase)
    {
    case EPhase::MainMenu:
        return TEXT("01_MainMenu");
    case EPhase::StartPicker:
        return TEXT("02_StartPicker_Mars");
    case EPhase::MainControls:
        return TEXT("03_MainControls");
    case EPhase::MainAbout:
        return TEXT("04_MainAbout");
    case EPhase::Thrust:
        return TEXT("05_Flight");
    case EPhase::Pause:
        return TEXT("06_Pause_Resume");
    case EPhase::TabControls:
        return TEXT("07_Pause_Controls");
    case EPhase::TabInfo:
        return TEXT("08_Pause_Info");
    case EPhase::TabSettings:
        return TEXT("09_Pause_Settings");
    case EPhase::TabAbout:
        return TEXT("10_Pause_About");
    case EPhase::TabQuit:
        return TEXT("11_Pause_Quit");
    case EPhase::QuitToMenu:
        return TEXT("12_BackAtMenu");
    case EPhase::PlayThird:
        return TEXT("13_ThirdFlight");
    default:
        return nullptr;
    }
}

//////////////////////////////////////////////////////////////////////////
// Checks the after-Play state: flight mode, ship entity, ship pawn with its mapping, hidden cursor, play count
void FSOLMenuSmoke::CheckFlight(USOLMenuSubsystem& menu, const TCHAR* label, const int32 expectedPlays)
{
    const USOLShipSubsystem* ships = menu.GetWorld()->GetSubsystem<USOLShipSubsystem>();
    const ASOLShipPawn* pawn = mController.IsValid() ? Cast<ASOLShipPawn>(mController->GetPawn()) : nullptr;
    const bool bCursor = mController.IsValid() && mController->bShowMouseCursor;
    Check(*FString::Printf(TEXT("%s: flying"), label), menu.GetState().Mode == SOLMenuState::EMenuMode::Playing
        && ships != nullptr && ships->HasPlayerShip() && pawn != nullptr && pawn->IsShipInputMapped()
        && bCursor == SOLMenuState::ShouldShowCursor(menu.GetState()) && !bCursor
        && menu.GetPlayCount() == expectedPlays, FString::Printf(TEXT("mode %d ship %d pawn %d mapped %d cursor %d "
        "plays %d"), static_cast<int32>(menu.GetState().Mode), ships != nullptr && ships->HasPlayerShip(),
        pawn != nullptr, pawn != nullptr && pawn->IsShipInputMapped(), bCursor, menu.GetPlayCount()));
}

//////////////////////////////////////////////////////////////////////////
// Checks the main-menu state: menu mode, no ship entity, the menu camera pawn, visible cursor
void FSOLMenuSmoke::CheckMainMenu(USOLMenuSubsystem& menu, const TCHAR* label)
{
    const USOLShipSubsystem* ships = menu.GetWorld()->GetSubsystem<USOLShipSubsystem>();
    const bool bMenuPawn = mController.IsValid() && Cast<ASOLMenuCameraPawn>(mController->GetPawn()) != nullptr;
    const bool bCursor = mController.IsValid() && mController->bShowMouseCursor;
    Check(*FString::Printf(TEXT("%s: main menu"), label), menu.GetState().Mode == SOLMenuState::EMenuMode::MainMenu
        && ships != nullptr && !ships->HasPlayerShip() && bMenuPawn && bCursor, FString::Printf(TEXT("mode %d ship %d "
        "menu pawn %d cursor %d"), static_cast<int32>(menu.GetState().Mode), ships != nullptr && ships->HasPlayerShip(),
        bMenuPawn, bCursor));
}

//////////////////////////////////////////////////////////////////////////
// Checks the paused state: pause mode, ship mapping removed, visible cursor
void FSOLMenuSmoke::CheckPaused(USOLMenuSubsystem& menu, const TCHAR* label)
{
    const ASOLShipPawn* pawn = mController.IsValid() ? Cast<ASOLShipPawn>(mController->GetPawn()) : nullptr;
    const bool bCursor = mController.IsValid() && mController->bShowMouseCursor;
    Check(*FString::Printf(TEXT("%s: paused"), label), menu.GetState().Mode == SOLMenuState::EMenuMode::Paused
        && pawn != nullptr && !pawn->IsShipInputMapped() && bCursor && menu.GetPauseMenu() != nullptr
        && menu.GetPauseMenu()->IsInViewport(), FString::Printf(TEXT("mode %d mapped %d cursor %d"),
        static_cast<int32>(menu.GetState().Mode), pawn != nullptr && pawn->IsShipInputMapped(), bCursor));
}

//////////////////////////////////////////////////////////////////////////
// Presses a game key now (released on the next step)
void FSOLMenuSmoke::TapKey(const FKey& key)
{
    if (mController.IsValid())
    {
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
        mPendingReleases.Add(key);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press and release through Slate to the focused widget
void FSOLMenuSmoke::SlateKey(const FKey& key) const
{
    if (!FSlateApplication::IsInitialized())
    {
        UE_LOG(LogSOL, Warning, TEXT("MenuSmoke: no Slate for key %s"), *key.ToString());
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    const FKeyEvent keyEvent(key, FModifierKeysState(), slate.GetUserIndexForKeyboard(), false, 0, 0);
    slate.ProcessKeyDownEvent(keyEvent);
    slate.ProcessKeyUpEvent(keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot with the UI to Saved/Screenshots
void FSOLMenuSmoke::TakeShot(const TCHAR* name) const
{
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / (FString(MENUSMOKE_SHOT_PREFIX)
        + name + MENUSMOKE_SHOT_EXTENSION));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("MenuSmoke: screenshot %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship's speed relative to its reference frame (m/s), 0 without a ship
double FSOLMenuSmoke::GetRelativeSpeedMps(const USOLMenuSubsystem& menu) const
{
    const USOLShipSubsystem* ships = menu.GetWorld()->GetSubsystem<USOLShipSubsystem>();
    if (ships == nullptr || !ships->HasPlayerShip())
    {
        return 0.0;
    }
    return (ships->GetState().VelocityMps - ships->GetReferenceVelocityMps()).Size();
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLMenuSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    if (bPassed)
    {
        ++mPassed;
        UE_LOG(LogSOL, Log, TEXT("MenuSmoke PASS: %s %s"), label, *detail);
    }
    else
    {
        ++mFailed;
        UE_LOG(LogSOL, Error, TEXT("MenuSmoke FAIL: %s %s"), label, *detail);
    }
}
