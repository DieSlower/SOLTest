/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuSubsystem.h"

#include "Game/SOLGameMode.h"
#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuSmoke.h"
#include "Menu/SOLPauseMenuWidget.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace
{
    constexpr int32 MENU_EVENT_QUEUE_RESERVE = 8;     // Requests that can wait for one frame without growing the queue
    constexpr float MENU_SMOKE_STEP_SECONDS = 0.02f;  // -SOLMenuSmoke script step interval
    const TCHAR* const MENU_SMOKE_DEFAULT_SCENARIO = TEXT("all");
    const TCHAR* const MENU_SWITCH_VALUE_SEPARATOR = TEXT("=");

    //////////////////////////////////////////////////////////////////////////
    // Returns a menu mode's name for the log
    const TCHAR* MenuModeName(const SOLMenuState::EMenuMode mode)
    {
        switch (mode)
        {
        case SOLMenuState::EMenuMode::MainMenu:
            return TEXT("MainMenu");
        case SOLMenuState::EMenuMode::Paused:
            return TEXT("Paused");
        case SOLMenuState::EMenuMode::Playing:
        default:
            return TEXT("Playing");
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Declares its dependencies and picks the starting state (main menu, or straight to flight for verification runs)
void USOLMenuSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Ships = collection.InitializeDependency<USOLShipSubsystem>();
    Targeting = collection.InitializeDependency<USOLTargetingSubsystem>();
    Super::Initialize(collection);
    mQueuedEvents.Reserve(MENU_EVENT_QUEUE_RESERVE);

    // The start body defaults to -SOLStart (else Earth); verification runs skip the menu and start in flight
    mState = SOLMenuState::Initial();
    if (!ShouldStartInMenu())
    {
        mState = SOLMenuState::Apply(mState, SOLMenuState::EMenuEvent::PlayPressed);
    }
    if (BodyRegistry != nullptr)
    {
        const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
        mStartBodyIndex = registry.FindByName(FName(*USOLShipSubsystem::GetCommandLineStartBody()));
        if (mStartBodyIndex == INDEX_NONE)
        {
            mStartBodyIndex = registry.FindByName(FName(SOL::BodyNames::EARTH));
        }
    }
    UE_LOG(LogSOL, Log, TEXT("MenuSubsystem %s: starting in %s, start body %s"), *GetName(), MenuModeName(mState.Mode),
        *GetStartBodyName());
}

//////////////////////////////////////////////////////////////////////////
// Removes the menu widgets and stops the timers
void USOLMenuSubsystem::Deinitialize()
{
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(mProcessTimer);
        world->GetTimerManager().ClearTimer(mShowTimer);
        world->GetTimerManager().ClearTimer(mSmokeTimer);
    }
    if (MainMenu != nullptr)
    {
        MainMenu->RemoveFromParent();
    }
    if (PauseMenu != nullptr)
    {
        PauseMenu->RemoveFromParent();
    }
    MainMenu = nullptr;
    PauseMenu = nullptr;
    mSmoke.Reset();
    mQueuedEvents.Empty();
    mOnStateChanged.Clear();
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Shows the main menu (next frame, once the viewport is up) and starts the menu smoke script if requested
void USOLMenuSubsystem::OnWorldBeginPlay(UWorld& world)
{
    Super::OnWorldBeginPlay(world);
    if (IsMenuShowing())
    {
        mShowTimer = world.GetTimerManager().SetTimerForNextTick(this, &USOLMenuSubsystem::RefreshWidgets);
    }

    // Verification hook: walk the menus and the flow, log checks and screenshots, then quit
    FString scenario;
    const bool bHasScenario = FParse::Value(FCommandLine::Get(), SOL::CommandLine::MENU_SMOKE, scenario);
    if (bHasScenario || FParse::Param(FCommandLine::Get(), SOL::CommandLine::MENU_SMOKE_NAME))
    {
        mSmoke = MakeUnique<FSOLMenuSmoke>();
        mSmoke->Start(*this, scenario.IsEmpty() ? FString(MENU_SMOKE_DEFAULT_SCENARIO) : scenario);
        mLastSmokeStepSeconds = world.GetRealTimeSeconds();
        world.GetTimerManager().SetTimer(mSmokeTimer, this, &USOLMenuSubsystem::StepSmoke, MENU_SMOKE_STEP_SECONDS,
            true);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLMenuSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLMenuSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the game starts in the main menu
bool USOLMenuSubsystem::ShouldStartInMenu()
{
    // -SOLNoMenu wins, then -SOLMenu / -SOLMenuSmoke; any other -SOL* switch is a verification or start flag that has
    // always started straight in flight
    TArray<FString> tokens;
    TArray<FString> switches;
    FCommandLine::Parse(FCommandLine::Get(), tokens, switches);
    bool bForceMenu = false;
    bool bHasOtherSolSwitch = false;
    for (const FString& commandSwitch : switches)
    {
        FString name;
        FString value;
        if (!commandSwitch.Split(MENU_SWITCH_VALUE_SEPARATOR, &name, &value))
        {
            name = commandSwitch;
        }
        if (name.Equals(SOL::CommandLine::NO_MENU, ESearchCase::IgnoreCase))
        {
            return false;
        }
        if (name.Equals(SOL::CommandLine::MENU, ESearchCase::IgnoreCase)
            || name.Equals(SOL::CommandLine::MENU_SMOKE_NAME, ESearchCase::IgnoreCase))
        {
            bForceMenu = true;
        }
        else if (name.StartsWith(SOL::CommandLine::SWITCH_PREFIX, ESearchCase::CaseSensitive))
        {
            bHasOtherSolSwitch = true;
        }
    }
    return bForceMenu || !bHasOtherSolSwitch;
}

//////////////////////////////////////////////////////////////////////////
// Queues Play (main menu only)
void USOLMenuSubsystem::RequestPlay()
{
    QueueEvent(SOLMenuState::EMenuEvent::PlayPressed);
}

//////////////////////////////////////////////////////////////////////////
// Queues an Esc press
void USOLMenuSubsystem::RequestEscape()
{
    QueueEvent(SOLMenuState::EMenuEvent::EscapePressed);
}

//////////////////////////////////////////////////////////////////////////
// Queues Resume
void USOLMenuSubsystem::RequestResume()
{
    QueueEvent(SOLMenuState::EMenuEvent::ResumePressed);
}

//////////////////////////////////////////////////////////////////////////
// Queues Quit to menu
void USOLMenuSubsystem::RequestQuitToMenu()
{
    QueueEvent(SOLMenuState::EMenuEvent::QuitToMenuPressed);
}

//////////////////////////////////////////////////////////////////////////
// Quits the application
void USOLMenuSubsystem::RequestQuitGame()
{
    UE_LOG(LogSOL, Log, TEXT("MenuSubsystem %s: quit requested"), *GetName());
    UKismetSystemLibrary::QuitGame(GetWorld(), GetPlayerController(), EQuitPreference::Quit, false);
}

//////////////////////////////////////////////////////////////////////////
// Selects a pause-menu tab (Resume resumes; the others show their page)
void USOLMenuSubsystem::SelectTab(const SOLMenuState::EMenuTab tab)
{
    if (tab == SOLMenuState::EMenuTab::Resume)
    {
        RequestResume();
        return;
    }
    mState = SOLMenuState::SelectTab(mState, tab);
    if (PauseMenu != nullptr)
    {
        PauseMenu->ShowTab(mState.Tab);
    }
}

//////////////////////////////////////////////////////////////////////////
// Records that the jump map opened or closed
void USOLMenuSubsystem::NotifyMapOpen(const bool bOpen)
{
    // Applied at once: it changes no pawn, widget or input mode (the pawn owns the map's cursor), only what Esc means
    mState = SOLMenuState::Apply(mState, bOpen ? SOLMenuState::EMenuEvent::MapOpened
        : SOLMenuState::EMenuEvent::MapClosed);
}

//////////////////////////////////////////////////////////////////////////
// Sets the start body by registry index (ignored when out of range)
void USOLMenuSubsystem::SetStartBodyIndex(const int32 bodyIndex)
{
    if (BodyRegistry == nullptr || bodyIndex < 0 || bodyIndex >= BodyRegistry->GetRegistry().Num())
    {
        return;
    }
    mStartBodyIndex = bodyIndex;
    if (MainMenu != nullptr)
    {
        MainMenu->RefreshStartBody();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the start body's registry name
FString USOLMenuSubsystem::GetStartBodyName() const
{
    return BodyRegistry != nullptr && mStartBodyIndex >= 0 && mStartBodyIndex < BodyRegistry->GetRegistry().Num()
        ? BodyRegistry->GetRegistry().GetName(mStartBodyIndex).ToString() : FString(SOL::BodyNames::EARTH);
}

//////////////////////////////////////////////////////////////////////////
// Returns the local player controller, or nullptr
APlayerController* USOLMenuSubsystem::GetPlayerController() const
{
    const UWorld* world = GetWorld();
    return world != nullptr ? world->GetFirstPlayerController() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Queues an event and arms the next-frame processing
void USOLMenuSubsystem::QueueEvent(const SOLMenuState::EMenuEvent event)
{
    mQueuedEvents.Add(event);
    UWorld* world = GetWorld();
    if (world != nullptr && !world->GetTimerManager().TimerExists(mProcessTimer))
    {
        mProcessTimer = world->GetTimerManager().SetTimerForNextTick(this, &USOLMenuSubsystem::ProcessQueuedEvents);
    }
}

//////////////////////////////////////////////////////////////////////////
// Applies the queued events in order
void USOLMenuSubsystem::ProcessQueuedEvents()
{
    mProcessTimer.Invalidate();
    for (int32 index = 0; index < mQueuedEvents.Num(); ++index)
    {
        ApplyEvent(mQueuedEvents[index]);
    }
    mQueuedEvents.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Applies one event: state machine, then its world side effects, then the broadcast
void USOLMenuSubsystem::ApplyEvent(const SOLMenuState::EMenuEvent event)
{
    const SOLMenuState::FMenuState previous = mState;
    mState = SOLMenuState::Apply(previous, event);
    if (mState.Mode == previous.Mode)
    {
        return;
    }
    UE_LOG(LogSOL, Log, TEXT("MenuSubsystem %s: %s -> %s"), *GetName(), MenuModeName(previous.Mode),
        MenuModeName(mState.Mode));

    // Pawn and ship changes first, so the widgets, the input mode and the listeners see the new world
    if (SOLMenuState::ShouldSpawnShip(previous, mState))
    {
        StartFlight();
    }
    else if (mState.Mode == SOLMenuState::EMenuMode::MainMenu)
    {
        ReturnToMainMenu();
    }
    RefreshWidgets();
    mOnStateChanged.Broadcast(previous, mState);
}

//////////////////////////////////////////////////////////////////////////
// Spawns the ship above the start body and possesses a new ship pawn
void USOLMenuSubsystem::StartFlight()
{
    if (Ships == nullptr)
    {
        return;
    }
    // The ship entity first (it becomes the observer, and the ship pawn's BeginPlay needs it), then the swap destroys
    // the menu pawn (unregistering it as the observer actor) and spawns and possesses the ship pawn
    Ships->SpawnPlayerShip(GetStartBodyName());
    ++mPlayCount;
    SwapPawn();
}

//////////////////////////////////////////////////////////////////////////
// Removes the ship pawn and entity and possesses a new menu camera pawn
void USOLMenuSubsystem::ReturnToMainMenu()
{
    // The new pawn class comes from the game mode, which reads this (already MainMenu) state
    SwapPawn();
    if (Ships != nullptr)
    {
        Ships->DespawnPlayerShip();
    }
    if (Targeting != nullptr)
    {
        // The next flight starts with no target and no frame lock, like the first one
        Targeting->ClearTarget();
        if (Targeting->IsFrameLocked())
        {
            Targeting->ToggleFrameLock();
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Destroys the controller's pawn and possesses a new pawn of the game mode's class for the current state
void USOLMenuSubsystem::SwapPawn()
{
    UWorld* world = GetWorld();
    APlayerController* playerController = GetPlayerController();
    AGameModeBase* gameMode = world != nullptr ? world->GetAuthGameMode() : nullptr;
    if (playerController == nullptr || gameMode == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("MenuSubsystem %s: no player controller or game mode to swap pawns"), *GetName());
        return;
    }

    // Destroying the old pawn runs its EndPlay now, so it unregisters as the observer actor before the new one registers
    if (APawn* oldPawn = playerController->GetPawn())
    {
        playerController->UnPossess();
        oldPawn->Destroy();
    }
    UClass* pawnClass = gameMode->GetDefaultPawnClassForController(playerController);
    FActorSpawnParameters params;
    params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APawn* pawn = pawnClass != nullptr ? world->SpawnActor<APawn>(pawnClass, FTransform::Identity, params) : nullptr;
    if (pawn == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("MenuSubsystem %s: failed to spawn the %s pawn"), *GetName(),
            pawnClass != nullptr ? *pawnClass->GetName() : TEXT("(none)"));
        return;
    }
    playerController->Possess(pawn);
}

//////////////////////////////////////////////////////////////////////////
// Shows the widget for the current state, hides the others, and sets the input mode and cursor
void USOLMenuSubsystem::RefreshWidgets()
{
    APlayerController* playerController = GetPlayerController();
    if (playerController == nullptr)
    {
        return;
    }
    const bool bMainMenu = mState.Mode == SOLMenuState::EMenuMode::MainMenu;
    const bool bPaused = mState.Mode == SOLMenuState::EMenuMode::Paused;

    // Widgets are created once, on first use, and only added to or removed from the viewport afterwards
    if (bMainMenu && MainMenu == nullptr)
    {
        MainMenu = CreateWidget<USOLMainMenuWidget>(playerController, USOLMainMenuWidget::StaticClass());
    }
    if (bPaused && PauseMenu == nullptr)
    {
        PauseMenu = CreateWidget<USOLPauseMenuWidget>(playerController, USOLPauseMenuWidget::StaticClass());
    }
    if (MainMenu != nullptr && !bMainMenu)
    {
        MainMenu->RemoveFromParent();
    }
    if (PauseMenu != nullptr && !bPaused)
    {
        PauseMenu->RemoveFromParent();
    }
    UUserWidget* shown = bMainMenu ? static_cast<UUserWidget*>(MainMenu) : (bPaused ? PauseMenu : nullptr);
    if (shown == nullptr)
    {
        // In flight the pawn owns the input mode and cursor (hidden and captured, or free while the map is open)
        return;
    }
    if (!shown->IsInViewport())
    {
        shown->AddToViewport(SOL::MenuStyle::Z_ORDER);
    }
    if (bMainMenu)
    {
        MainMenu->ShowPage(ESOLMainMenuPage::None);
        MainMenu->RefreshStartBody();
    }
    else
    {
        PauseMenu->ShowTab(mState.Tab);
    }

    // A menu takes the keyboard and shows the cursor; the game viewport gets no input meanwhile
    FInputModeUIOnly inputMode;
    inputMode.SetWidgetToFocus(shown->TakeWidget());
    inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    playerController->SetInputMode(inputMode);
    playerController->bShowMouseCursor = SOLMenuState::ShouldShowCursor(mState);
    if (bMainMenu)
    {
        MainMenu->FocusDefault();
    }
    else
    {
        PauseMenu->FocusDefault();
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one step of the menu smoke script
void USOLMenuSubsystem::StepSmoke()
{
    UWorld* world = GetWorld();
    if (!mSmoke.IsValid() || world == nullptr)
    {
        return;
    }
    const double nowSeconds = world->GetRealTimeSeconds();
    const double deltaSeconds = nowSeconds - mLastSmokeStepSeconds;
    mLastSmokeStepSeconds = nowSeconds;
    if (mSmoke->Update(*this, deltaSeconds))
    {
        world->GetTimerManager().ClearTimer(mSmokeTimer);
        mSmoke.Reset();
    }
}
