/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuSmoke.h"

#include "Combat/SOLCombatSubsystem.h"
#include "Menu/SOLDemoPageWidget.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuCameraPawn.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLPauseMenuWidget.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Npc/SOLNpcStatsWidget.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UI/SOLFlightHud.h"
#include "UnrealClient.h"

namespace
{
    const TCHAR* const MENUSMOKE_FLOW_SCENARIO = TEXT("flow");   // Checks only, no screenshots
    const TCHAR* const MENUSMOKE_SHOT_PREFIX = TEXT("MenuSmoke_");
    const TCHAR* const MENUSMOKE_SHOT_EXTENSION = TEXT(".png");
    constexpr double MENUSMOKE_SHOT_SETTLE_S = 0.5;               // After a screenshot request, before the next phase
    constexpr double MENUSMOKE_MIN_THRUST_GAIN_MPS = 50.0;        // Speed the 2.5 s of forward thrust must add

    // Settings phases (8d): the values set through the page, and the slot writes expected after each visit
    constexpr float MENUSMOKE_MASTER_VOLUME = 0.5f;
    constexpr float MENUSMOKE_SFX_VOLUME = 0.8f;
    constexpr float MENUSMOKE_MUSIC_VOLUME = 0.4f;
    constexpr float MENUSMOKE_MOUSE_SENSITIVITY = 1.5f;
    constexpr float MENUSMOKE_HUD_SCALE = 1.25f;
    constexpr float MENUSMOKE_PREVIEW_VOLUME = 0.2f;              // Previewed, then reverted
    constexpr float MENUSMOKE_VOLUME_TOLERANCE = 0.01f;
    constexpr int32 MENUSMOKE_WRITES_AFTER_REBIND = 2;            // Main-menu visit, then the pause rebind visit
    constexpr int32 MENUSMOKE_WRITES_AFTER_RESTORE = 3;           // ...then the restore at the end
    const TCHAR* const MENUSMOKE_FIRE_ROW = TEXT("@Fire");        // Controls row of the fire binding

    //////////////////////////////////////////////////////////////////////////
    // Returns the SOLMenuText::CONTROL_ROWS index whose bindings are exactly this text, or INDEX_NONE
    int32 FindControlsRow(const TCHAR* bindings)
    {
        for (int32 row = 0; row < UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS); ++row)
        {
            if (FCString::Strcmp(SOLMenuText::CONTROL_ROWS[row].Bindings, bindings) == 0)
            {
                return row;
            }
        }
        return INDEX_NONE;
    }
}

//////////////////////////////////////////////////////////////////////////
// Reads the scenario and starts the first phase
void FSOLMenuSmoke::Start(USOLMenuSubsystem& menu, const FString& scenario)
{
    mController = menu.GetPlayerController();
    mTakesShots = !scenario.Equals(MENUSMOKE_FLOW_SCENARIO, ESearchCase::IgnoreCase);
    mPendingReleases.Reserve(4);

    // The settings the run starts from (its own slot, so the defaults seeded from the engine) are restored at the end
    if (const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu))
    {
        mOriginalValues = settings->GetSavedValues();
    }
    if (const UGameUserSettings* engineSettings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr)
    {
        mOriginalQualityLevel = engineSettings->GetOverallScalabilityLevel();
    }
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
    EnterSettingsPhase(phase, menu);
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
        // Shift+M opens the map (SDD 20); Shift is released with M next frame
        TapKey(EKeys::LeftShift);
        TapKey(EKeys::M);
        break;
    case EPhase::MapEscape:
        TapKey(EKeys::Escape);
        break;
    case EPhase::MainDemo:
        if (mainMenu != nullptr && mainMenu->GetDemoPage() != nullptr)
        {
            // Showcase (250k), one preset step down (100k), the battle max distance one step up (21 km)
            mainMenu->ShowPage(ESOLMainMenuPage::Demo);
            mainMenu->GetDemoPage()->ApplyPreset(SOLMenuState::EDemoPreset::Showcase);
            mainMenu->GetDemoPage()->Step(static_cast<int32>(SOLNpcScale::EDemoOption::NpcCount), -1);
            mainMenu->GetDemoPage()->Step(static_cast<int32>(SOLNpcScale::EDemoOption::BattleMaxDistanceM), 1);
        }
        break;
    case EPhase::TabDemo:
        menu.SelectTab(SOLMenuState::EMenuTab::Demo);
        break;
    case EPhase::HotkeyPause:
        PressKey(EKeys::LeftControl);
        TapKey(EKeys::F1);
        break;
    case EPhase::HotkeyResume:
        ReleaseKey(EKeys::LeftControl);
        mSlateKeysBefore = menu.GetPauseMenu() != nullptr ? menu.GetPauseMenu()->GetSlateKeyCount() : 0;
        SlateKey(EKeys::F1, true);
        break;
    case EPhase::DemoApply:
        menu.RequestEscape();
        break;
    case EPhase::DemoRunning:
    {
        // From the pause menu's Demo tab: Showcase at 100k with the overlay on, applied (respawn and resume)
        menu.SelectTab(SOLMenuState::EMenuTab::Demo);
        SOLMenuState::FDemoPage page = SOLMenuState::ApplyDemoPreset(menu.GetDemoPage(),
            SOLMenuState::EDemoPreset::Showcase);
        page = SOLMenuState::StepDemoRow(page, static_cast<int32>(SOLNpcScale::EDemoOption::NpcCount), -1);
        menu.SetDemoPage(page);
        if (menu.GetPauseMenu() != nullptr && menu.GetPauseMenu()->GetDemoPage() != nullptr)
        {
            menu.GetPauseMenu()->GetDemoPage()->RefreshFromMenu();
            menu.GetPauseMenu()->GetDemoPage()->Start();
        }
        break;
    }
    case EPhase::StatsHotkey:
        PressKey(EKeys::LeftControl);
        TapKey(EKeys::F2);
        break;
    case EPhase::DemoStop:
        ReleaseKey(EKeys::LeftControl);
        menu.RequestStopNpcs();
        break;    case EPhase::QuitToMenu:
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
    ExitSettingsPhase(phase, menu);
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
    case EPhase::MainDemo:
    {
        const USOLDemoPageWidget* demo = menu.GetMainMenu() != nullptr ? menu.GetMainMenu()->GetDemoPage() : nullptr;
        const FString count = demo != nullptr ? demo->GetValueText(0) : FString();
        const FString distance = demo != nullptr
            ? demo->GetValueText(static_cast<int32>(SOLNpcScale::EDemoOption::BattleMaxDistanceM)) : FString();
        Check(TEXT("Demo page steppers and preset"), count == TEXT("100,000") && distance == TEXT("21 km")
            && menu.GetDemoPage().Options.NpcCount == 100000, FString::Printf(TEXT("count %s distance %s"), *count,
            *distance));
        break;
    }
    case EPhase::TabDemo:
        Check(TEXT("pause Demo tab"), menu.GetState().Tab == SOLMenuState::EMenuTab::Demo, FString());
        break;
    case EPhase::HotkeyPause:
        CheckPaused(menu, TEXT("Ctrl+F1 pauses"));
        break;
    case EPhase::HotkeyResume:
    {
        const int32 slateKeys = menu.GetPauseMenu() != nullptr ? menu.GetPauseMenu()->GetSlateKeyCount() : 0;
        Check(TEXT("Ctrl+F1 reached the pause menu through Slate"), slateKeys > mSlateKeysBefore,
            FString::Printf(TEXT("%d -> %d"), mSlateKeysBefore, slateKeys));
        CheckFlight(menu, TEXT("Ctrl+F1 resumes"), 1);
        break;
    }
    case EPhase::DemoRunning:
    {
        const USOLNpcSubsystem* npcs = menu.GetWorld()->GetSubsystem<USOLNpcSubsystem>();
        const FString overlay = npcs != nullptr && npcs->GetStatsWidget() != nullptr
            ? npcs->GetStatsWidget()->GetText() : FString();
        Check(TEXT("Demo applied from the pause menu"), npcs != nullptr && npcs->GetStats().Total == 100000
            && menu.GetState().Mode == SOLMenuState::EMenuMode::Playing, FString::Printf(TEXT("total %d"),
            npcs != nullptr ? npcs->GetStats().Total : -1));
        Check(TEXT("stats overlay shows"), npcs != nullptr && npcs->IsStatsVisible()
            && overlay.Contains(TEXT("NPCs 100,000")), overlay.Replace(TEXT("\n"), TEXT(" | ")));
        break;
    }
    case EPhase::StatsHotkey:
    {
        const USOLNpcSubsystem* npcs = menu.GetWorld()->GetSubsystem<USOLNpcSubsystem>();
        Check(TEXT("Ctrl+F2 hides the stats overlay"), npcs != nullptr && !npcs->IsStatsVisible(), FString());
        break;
    }
    case EPhase::DemoStop:
    {
        const USOLNpcSubsystem* npcs = menu.GetWorld()->GetSubsystem<USOLNpcSubsystem>();
        Check(TEXT("Stop NPCs removes the population"), npcs != nullptr && npcs->GetStats().Total == 0
            && npcs->GetRequestedCount() == 0, FString());
        break;
    }    case EPhase::PauseAgain:
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
    case EPhase::DemoRunning:
        return 6.0;
    case EPhase::SettingsLeave:
    case EPhase::FireNewKey:
        return 2.0;
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
    case EPhase::MainDemo:
        return TEXT("04d_MainDemo");
    case EPhase::TabDemo:
        return TEXT("09d_Pause_Demo");
    case EPhase::DemoRunning:
        return TEXT("14_Demo_Overlay");
    case EPhase::MainSettings:
        return TEXT("04b_MainSettings");
    case EPhase::SettingsChange:
        return TEXT("04c_MainSettings_Changed");
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
    case EPhase::RebindFire:
        return TEXT("09b_Pause_Settings_Rebound");
    case EPhase::ControlsRebound:
        return TEXT("09c_Pause_Controls_Rebound");
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
// Sends a key press and release through Slate to the focused widget (with Left Ctrl held when bControl)
void FSOLMenuSmoke::SlateKey(const FKey& key, const bool bControl) const
{
    if (!FSlateApplication::IsInitialized())
    {
        UE_LOG(LogSOL, Warning, TEXT("MenuSmoke: no Slate for key %s"), *key.ToString());
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    const FModifierKeysState modifiers(false, false, bControl, false, false, false, false, false, false);
    const FKeyEvent keyEvent(key, modifiers, slate.GetUserIndexForKeyboard(), false, 0, 0);
    slate.ProcessKeyDownEvent(keyEvent);
    slate.ProcessKeyUpEvent(keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// Sends a mouse button press and release through Slate at the cursor
void FSOLMenuSmoke::SlateMouseButton(const FKey& button) const
{
    if (!FSlateApplication::IsInitialized())
    {
        UE_LOG(LogSOL, Warning, TEXT("MenuSmoke: no Slate for mouse button %s"), *button.ToString());
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    const FVector2D cursor = slate.GetCursorPos();
    TSet<FKey> pressed;
    pressed.Add(button);
    const FPointerEvent down(slate.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, cursor, cursor,
        pressed, button, 0.0f, FModifierKeysState());
    slate.ProcessMouseButtonDownEvent(nullptr, down);
    const FPointerEvent up(slate.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, cursor, cursor,
        TSet<FKey>(), button, 0.0f, FModifierKeysState());
    slate.ProcessMouseButtonUpEvent(up);
}

//////////////////////////////////////////////////////////////////////////
// Drives the rebind capture through real Slate events with the focus on another button
void FSOLMenuSmoke::RunRebindCapture(USOLMenuSubsystem& menu)
{
    using SOLKeyBindings::EBinding;
    USOLPauseMenuWidget* pauseMenu = menu.GetPauseMenu();
    USOLSettingsPageWidget* page = pauseMenu != nullptr ? pauseMenu->GetSettingsPage() : nullptr;
    USOLMenuButton* resume = pauseMenu != nullptr ? pauseMenu->GetTabButton(SOLMenuState::EMenuTab::Resume) : nullptr;
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
    mIsCaptureOk = false;
    mSlateKeysBefore = pauseMenu != nullptr ? pauseMenu->GetSlateKeyCount() : 0;
    if (page == nullptr || resume == nullptr || settings == nullptr)
    {
        mCaptureDetail = TEXT("no pause menu, settings page or Resume tab");
        return;
    }
    const int32 drop = static_cast<int32>(EBinding::DropTarget);
    const int32 toggleMap = static_cast<int32>(EBinding::Resume);

    // Space while listening, with the mouse hovering Resume (no focus change) and then the keyboard focus forced onto
    // it anyway: Space is the new key and must not click Resume
    page->BeginRebind(drop);
    const bool bHadFocus = resume->HasKeyboardFocus();
    resume->NotifyHovered();
    const bool bHoverMovedFocus = !bHadFocus && resume->HasKeyboardFocus();
    resume->SetKeyboardFocus();
    SlateKey(EKeys::SpaceBar);
    const FString spaceKey = settings->GetPendingKeys()[drop];

    // Enter for Resume (one of the map's fixed keys; Resume is mapped on the map too): refused, and must not click the
    // focused Resume button either
    page->BeginRebind(toggleMap);
    SlateKey(EKeys::Enter);
    const FString mapKey = settings->GetPendingKeys()[toggleMap];
    const FString refusedStatus = page->GetStatusText();

    // A right mouse press is a key like any other
    page->BeginRebind(drop);
    SlateMouseButton(EKeys::RightMouseButton);
    const FString mouseKey = settings->GetPendingKeys()[drop];
    const bool bStillListening = page->IsListening();

    // Back to the default drop key, so the next phase starts from the defaults
    page->BeginRebind(drop);
    page->CaptureKey(EKeys::G);
    mIsCaptureOk = !bHoverMovedFocus && spaceKey == TEXT("SpaceBar") && mapKey == TEXT("V")
        && !refusedStatus.IsEmpty() && mouseKey == TEXT("RightMouseButton") && !bStillListening
        && settings->GetPendingKeys()[drop] == TEXT("G") && !settings->IsKeyCaptureActive();
    mCaptureDetail = FString::Printf(TEXT("hover moved focus %d, space -> %s, map Enter -> %s (%s), mouse -> %s"),
        bHoverMovedFocus, *spaceKey, *mapKey, *refusedStatus, *mouseKey);
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

//////////////////////////////////////////////////////////////////////////
// Runs a settings phase's entry actions (part 8d)
void FSOLMenuSmoke::EnterSettingsPhase(const EPhase phase, USOLMenuSubsystem& menu)
{
    using SOLKeyBindings::EBinding;
    USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLSettingsPageWidget* mainPage = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
    USOLSettingsPageWidget* pausePage = menu.GetPauseMenu() != nullptr ? menu.GetPauseMenu()->GetSettingsPage()
        : nullptr;
    if (settings == nullptr)
    {
        return;
    }
    mSaveRequestsBefore = settings->GetSaveRequestCount();
    switch (phase)
    {
    case EPhase::MainSettings:
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::Settings);
        }
        break;
    case EPhase::SettingsChange:
        if (mainPage != nullptr)
        {
            // Two Applies in one visit: the volumes, mouse, HUD scale and quality first, then the music volume
            mainPage->SetSliderValue(ESOLSettingsSlider::MasterVolume, MENUSMOKE_MASTER_VOLUME);
            mainPage->SetSliderValue(ESOLSettingsSlider::SfxVolume, MENUSMOKE_SFX_VOLUME);
            mainPage->SetSliderValue(ESOLSettingsSlider::MouseSensitivity, MENUSMOKE_MOUSE_SENSITIVITY);
            mainPage->SetToggle(ESOLSettingsToggle::InvertY, true);
            mainPage->SetSliderValue(ESOLSettingsSlider::HudScale, MENUSMOKE_HUD_SCALE);
            mainPage->Step(ESOLSettingsStepper::Quality, -1);
            mainPage->Apply();
            mainPage->SetSliderValue(ESOLSettingsSlider::MusicVolume, MENUSMOKE_MUSIC_VOLUME);
            mainPage->Apply();
        }
        mExpectedValues = mOriginalValues;
        mExpectedValues.MasterVolume = MENUSMOKE_MASTER_VOLUME;
        mExpectedValues.SfxVolume = MENUSMOKE_SFX_VOLUME;
        mExpectedValues.MusicVolume = MENUSMOKE_MUSIC_VOLUME;
        mExpectedValues.MouseSensitivity = MENUSMOKE_MOUSE_SENSITIVITY;
        mExpectedValues.bInvertY = true;
        mExpectedValues.HudScale = MENUSMOKE_HUD_SCALE;
        mExpectedValues.QualityPreset = SOLSettingsRules::StepIndex(mOriginalValues.QualityPreset,
            SOLSettingsRules::QUALITY_PRESET_COUNT, -1);
        break;
    case EPhase::SettingsLeave:
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::None);
        }
        break;
    case EPhase::SettingsNoChange:
        if (mainMenu != nullptr)
        {
            mainMenu->ShowPage(ESOLMainMenuPage::Settings);
            mainMenu->ShowPage(ESOLMainMenuPage::None);
        }
        break;
    case EPhase::SettingsRevert:
        if (mainMenu != nullptr && mainPage != nullptr)
        {
            // The preview reaches the live values at once; Revert puts the applied ones back
            mainMenu->ShowPage(ESOLMainMenuPage::Settings);
            mainPage->SetSliderValue(ESOLSettingsSlider::MasterVolume, MENUSMOKE_PREVIEW_VOLUME);
            mRevertPreviewOk = FMath::IsNearlyEqual(settings->GetLiveValues().MasterVolume, MENUSMOKE_PREVIEW_VOLUME)
                && settings->IsDirty();
            mainPage->Revert();
            mainMenu->ShowPage(ESOLMainMenuPage::None);
        }
        break;
    case EPhase::AudioProbe:
        settings->ProbeEffectsVolume();
        break;
    case EPhase::RebindCapture:
        RunRebindCapture(menu);
        break;
    case EPhase::RebindFire:
        if (pausePage != nullptr)
        {
            // A fixed key is refused, a used key is bound with a warning (and put back), then Fire goes to K
            pausePage->BeginRebind(static_cast<int32>(EBinding::DropTarget));
            pausePage->CaptureKey(EKeys::F1);
            const bool bRefused = settings->GetPendingKeys()[static_cast<int32>(EBinding::DropTarget)] == TEXT("G");
            pausePage->BeginRebind(static_cast<int32>(EBinding::Boost));
            pausePage->CaptureKey(EKeys::G);
            const bool bConflict = SOLKeyBindings::HasConflict(settings->GetPendingKeys(),
                static_cast<int32>(EBinding::Boost));
            const FString conflictStatus = pausePage->GetStatusText();
            pausePage->BeginRebind(static_cast<int32>(EBinding::Boost));
            pausePage->CaptureKey(EKeys::Tab);
            pausePage->BeginRebind(static_cast<int32>(EBinding::Fire));
            const bool bListening = pausePage->IsListening();
            pausePage->CaptureKey(EKeys::K);
            Check(TEXT("rebind: fixed key refused, used key flagged, Fire listening"), bRefused && bConflict
                && bListening && !conflictStatus.IsEmpty(), conflictStatus);
            Check(TEXT("rebind: Fire pending K and the page is dirty"), pausePage->GetKeyRowText(
                static_cast<int32>(EBinding::Fire)) == TEXT("K") && settings->IsDirty(), pausePage->GetStatusText());
            pausePage->Apply();
            pausePage->ScrollToBinding(static_cast<int32>(EBinding::Fire));
        }
        break;
    case EPhase::ControlsRebound:
        menu.SelectTab(SOLMenuState::EMenuTab::Controls);
        break;
    case EPhase::FireOldKey:
        mBoltsBefore = GetBoltCount(menu);
        PressKey(EKeys::LeftMouseButton);
        break;
    case EPhase::FireNewKey:
        mBoltsBefore = GetBoltCount(menu);
        PressKey(EKeys::K);
        break;
    case EPhase::RestorePause:
        menu.RequestEscape();
        break;
    case EPhase::RestoreSettings:
        menu.SelectTab(SOLMenuState::EMenuTab::Settings);
        if (pausePage != nullptr)
        {
            settings->SetPendingValues(mOriginalValues);
            pausePage->ResetKeys();
            pausePage->Apply();
        }
        break;
    case EPhase::RestoreResume:
        menu.RequestResume();
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a settings phase's exit checks (part 8d)
void FSOLMenuSmoke::ExitSettingsPhase(const EPhase phase, USOLMenuSubsystem& menu)
{
    using SOLKeyBindings::EBinding;
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
    const ASOLShipPawn* shipPawn = mController.IsValid() ? Cast<ASOLShipPawn>(mController->GetPawn()) : nullptr;
    const UGameUserSettings* engineSettings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
    if (settings == nullptr)
    {
        if (phase == EPhase::MainSettings)
        {
            Check(TEXT("settings subsystem exists"), false, FString());
        }
        return;
    }
    const int32 writes = settings->GetSaveRequestCount();
    const FString writeDetail = FString::Printf(TEXT("writes %d -> %d, completed %d"), mSaveRequestsBefore, writes,
        settings->GetSaveCompletedCount());
    switch (phase)
    {
    case EPhase::MainSettings:
        Check(TEXT("Settings page shown"), menu.GetMainMenu() != nullptr
            && menu.GetMainMenu()->GetPage() == ESOLMainMenuPage::Settings, settings->GetSlotName());
        break;
    case EPhase::SettingsChange:
        Check(TEXT("two Applies commit every value"), SOLSettingsModel::AreEqual(settings->GetSavedValues(),
            mExpectedValues) && !settings->IsDirty(), FString::Printf(TEXT("master %.2f music %.2f quality %d"),
            settings->GetSavedValues().MasterVolume, settings->GetSavedValues().MusicVolume,
            settings->GetSavedValues().QualityPreset));
        Check(TEXT("Apply does not write (the write waits for the page to close)"), writes == mSaveRequestsBefore,
            writeDetail);
        Check(TEXT("quality preset reached the engine"), engineSettings != nullptr
            && engineSettings->GetOverallScalabilityLevel() == mExpectedValues.QualityPreset, FString::Printf(
            TEXT("engine %d expected %d"), engineSettings != nullptr ? engineSettings->GetOverallScalabilityLevel()
            : -2, mExpectedValues.QualityPreset));
        break;
    case EPhase::SettingsLeave:
    {
        mSlotTimeStamp = IFileManager::Get().GetTimeStamp(*settings->GetSlotFilePath());
        Check(TEXT("leaving the page writes the slot exactly once"), writes == mSaveRequestsBefore + 1
            && settings->GetSaveCompletedCount() == writes && mSlotTimeStamp != FDateTime::MinValue(),
            FString::Printf(TEXT("%s, file %s"), *writeDetail, *settings->GetSlotFilePath()));
        break;
    }
    case EPhase::SettingsNoChange:
        Check(TEXT("a visit without changes does not write"), writes == mSaveRequestsBefore
            && IFileManager::Get().GetTimeStamp(*settings->GetSlotFilePath()) == mSlotTimeStamp, writeDetail);
        break;
    case EPhase::SettingsReload:
    {
        SOLSettingsModel::FSettingsValues reloaded;
        TArray<FString> keys;
        const bool bRead = settings->ReadSlotForVerification(reloaded, keys);
        Check(TEXT("re-reading the slot restores the values"), bRead && SOLSettingsModel::AreEqual(reloaded,
            mExpectedValues) && keys == SOLKeyBindings::DefaultKeys(), FString::Printf(TEXT("read %d master %.2f "
            "invert %d hud %.2f"), bRead, reloaded.MasterVolume, reloaded.bInvertY, reloaded.HudScale));
        break;
    }
    case EPhase::SettingsRevert:
        Check(TEXT("preview then Revert"), mRevertPreviewOk && FMath::IsNearlyEqual(settings->GetLiveValues()
            .MasterVolume, MENUSMOKE_MASTER_VOLUME) && !settings->IsDirty() && writes == mSaveRequestsBefore,
            FString::Printf(TEXT("preview %d live %.2f %s"), mRevertPreviewOk, settings->GetLiveValues().MasterVolume,
            *writeDetail));
        break;
    case EPhase::AudioProbe:
    {
        const float expected = MENUSMOKE_MASTER_VOLUME * MENUSMOKE_SFX_VOLUME;
        Check(TEXT("effects class mixed volume = master x effects"), FMath::IsNearlyEqual(
            settings->GetProbedEffectsVolume(), expected, MENUSMOKE_VOLUME_TOLERANCE), FString::Printf(
            TEXT("%.3f expected %.3f"), settings->GetProbedEffectsVolume(), expected));
        break;
    }
    case EPhase::Thrust:
    {
        const ASOLFlightHud* hud = mController.IsValid() ? Cast<ASOLFlightHud>(mController->GetHUD()) : nullptr;
        Check(TEXT("HUD scale applied"), hud != nullptr && hud->GetUiScale() >= MENUSMOKE_HUD_SCALE
            - MENUSMOKE_VOLUME_TOLERANCE, FString::Printf(TEXT("ui scale %.3f"), hud != nullptr ? hud->GetUiScale()
            : 0.0f));
        break;
    }
    case EPhase::TabControls:
    {
        const int32 fireRow = FindControlsRow(MENUSMOKE_FIRE_ROW);
        const FString text = menu.GetPauseMenu() != nullptr ? menu.GetPauseMenu()->GetControlsRowText(fireRow)
            : FString();
        Check(TEXT("Controls shows the default fire key"), text == TEXT("LMB"), text);
        break;
    }
    case EPhase::RebindCapture:
    {
        // A click on the focused Resume tab would have queued a resume (applied a frame later): the menu must still be
        // paused on the Settings tab, and the pause menu must not have seen the keys
        const int32 slateKeys = menu.GetPauseMenu() != nullptr ? menu.GetPauseMenu()->GetSlateKeyCount() : -1;
        Check(TEXT("rebind capture: keys bound or refused, nothing clicked"), mIsCaptureOk
            && menu.GetState().Mode == SOLMenuState::EMenuMode::Paused
            && menu.GetState().Tab == SOLMenuState::EMenuTab::Settings && slateKeys == mSlateKeysBefore,
            FString::Printf(TEXT("%s; mode %d tab %d slate keys %d -> %d"), *mCaptureDetail,
            static_cast<int32>(menu.GetState().Mode), static_cast<int32>(menu.GetState().Tab), mSlateKeysBefore,
            slateKeys));
        break;
    }
    case EPhase::RebindFire:
        Check(TEXT("rebind applied through the pawn"), shipPawn != nullptr
            && shipPawn->GetBoundKeyNames()[static_cast<int32>(EBinding::Fire)] == TEXT("K")
            && shipPawn->IsKeyMappedToBinding(static_cast<int32>(EBinding::Fire), EKeys::K)
            && !shipPawn->IsKeyMappedToBinding(static_cast<int32>(EBinding::Fire), EKeys::LeftMouseButton)
            && shipPawn->IsKeyMappedToBinding(static_cast<int32>(EBinding::Boost), EKeys::Tab)
            && writes == mSaveRequestsBefore, writeDetail);
        break;
    case EPhase::ControlsRebound:
    {
        const int32 fireRow = FindControlsRow(MENUSMOKE_FIRE_ROW);
        const FString text = menu.GetPauseMenu() != nullptr ? menu.GetPauseMenu()->GetControlsRowText(fireRow)
            : FString();
        Check(TEXT("Controls shows the rebound fire key"), text == TEXT("K"), text);
        break;
    }
    case EPhase::ResumeSlate:
        Check(TEXT("resuming writes the rebind once"), writes == MENUSMOKE_WRITES_AFTER_REBIND
            && settings->GetSaveRequestCount() == MENUSMOKE_WRITES_AFTER_REBIND, writeDetail);
        break;
    case EPhase::FireOldKey:
    {
        const int32 bolts = GetBoltCount(menu);
        ReleaseKey(EKeys::LeftMouseButton);
        Check(TEXT("the old fire key no longer fires"), bolts <= mBoltsBefore, FString::Printf(TEXT("bolts %d -> %d"),
            mBoltsBefore, bolts));
        break;
    }
    case EPhase::FireNewKey:
    {
        const int32 bolts = GetBoltCount(menu);
        ReleaseKey(EKeys::K);
        SOLSettingsModel::FSettingsValues reloaded;
        TArray<FString> keys;
        const bool bRead = settings->ReadSlotForVerification(reloaded, keys);
        Check(TEXT("the new fire key fires bolts"), bolts > mBoltsBefore, FString::Printf(TEXT("bolts %d -> %d"),
            mBoltsBefore, bolts));
        const ASOLFlightHud* hud = mController.IsValid() ? Cast<ASOLFlightHud>(mController->GetHUD()) : nullptr;
        Check(TEXT("the HUD hint line shows the rebound fire key"), hud != nullptr
            && hud->GetFlightKeyHints().Contains(TEXT("   K fire   ")), hud != nullptr ? hud->GetFlightKeyHints()
            : FString());
        Check(TEXT("the slot holds the rebind"), bRead && keys[static_cast<int32>(EBinding::Fire)] == TEXT("K"),
            bRead ? keys[static_cast<int32>(EBinding::Fire)] : FString());
        break;
    }
    case EPhase::RestoreSettings:
        Check(TEXT("restore applied"), SOLSettingsModel::AreEqual(settings->GetSavedValues(), mOriginalValues)
            && settings->GetAppliedKeys() == SOLKeyBindings::DefaultKeys(), FString());
        break;
    case EPhase::RestoreResume:
    {
        SOLSettingsModel::FSettingsValues reloaded;
        TArray<FString> keys;
        const bool bRead = settings->ReadSlotForVerification(reloaded, keys);
        Check(TEXT("restored settings written (third write) and the engine quality is back"), bRead
            && SOLSettingsModel::AreEqual(reloaded, mOriginalValues) && keys == SOLKeyBindings::DefaultKeys()
            && writes == MENUSMOKE_WRITES_AFTER_RESTORE && engineSettings != nullptr
            && (mOriginalQualityLevel < 0 || engineSettings->GetOverallScalabilityLevel() == mOriginalQualityLevel),
            FString::Printf(TEXT("read %d %s quality %d (was %d)"), bRead, *writeDetail, engineSettings != nullptr
            ? engineSettings->GetOverallScalabilityLevel() : -2, mOriginalQualityLevel));
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the live bolt count of the combat subsystem (0 without it)
int32 FSOLMenuSmoke::GetBoltCount(const USOLMenuSubsystem& menu)
{
    const USOLCombatSubsystem* combat = menu.GetWorld()->GetSubsystem<USOLCombatSubsystem>();
    return combat != nullptr ? combat->GetBoltCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Presses a game key now and holds it until ReleaseKey
void FSOLMenuSmoke::PressKey(const FKey& key)
{
    if (mController.IsValid())
    {
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
    }
}

//////////////////////////////////////////////////////////////////////////
// Releases a held game key
void FSOLMenuSmoke::ReleaseKey(const FKey& key)
{
    if (mController.IsValid())
    {
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
    }
}
