/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLMenuSmoke.h"
#include "Menu/SOLMenuState.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLMenuSubsystem.generated.h"

class APawn;
class APlayerController;
class USOLBodyRegistrySubsystem;
class USOLJumpSubsystem;
class USOLMainMenuWidget;
class USOLNpcSubsystem;
class USOLPauseMenuWidget;
class USOLSettingsSubsystem;
class USOLShipSubsystem;
class USOLTargetingSubsystem;

// Fired after every menu state change (the pawn swap, the ship spawn or despawn and the widgets are already done)
DECLARE_MULTICAST_DELEGATE_TwoParams(FSOLOnMenuStateChanged, const SOLMenuState::FMenuState& /*previous*/,
    const SOLMenuState::FMenuState& /*next*/);

/**
 * Menu flow (SDD 8, 8b): owns the SOLMenuState state machine and applies its transitions to the world.
 *
 * The game starts in the main menu over the live solar system: the player controller possesses an ASOLMenuCameraPawn
 * that orbits the chosen start body, and no ship entity exists. Play spawns the ship entity above the start body
 * (USOLShipSubsystem::SpawnPlayerShip), destroys the menu pawn and possesses a new ASOLShipPawn. Esc in flight (the
 * pawn's IA_ShipPause, which the map's own Esc pre-empts because the ship mapping is removed while the map is open)
 * opens the pause menu: the simulation keeps running (SDD 8 decision 3) and the pawn suspends its control input from
 * OnStateChanged, exactly as for the map. Resume (or Esc) gives control back. Quit to menu cancels a running jump
 * warp (USOLJumpSubsystem::CancelJump), destroys the ship pawn and the ship entity (USOLShipSubsystem::
 * DespawnPlayerShip, which also clears its surface-lock and frame history), clears the selected target and frame lock,
 * and possesses a fresh menu pawn, so Play works again any number of times.
 *
 * Requests from widgets and the pawn are queued and applied at the start of the next frame, outside Slate and
 * Enhanced Input callbacks, because a transition destroys and spawns pawns and changes the input mode. While a menu
 * shows, the controller is in UI-only input with a visible cursor and the menu widget has keyboard focus; in flight the
 * pawn owns the cursor (hidden and captured, or free while the map is open), which matches ShouldShowCursor.
 *
 * Settings (8d): every menu state change ends the settings visit (USOLSettingsSubsystem::EndVisit through the pages:
 * unapplied changes are reverted and the save is written once if an Apply changed something), and the settings' volume
 * mix is pushed on this world at begin play and popped when the world is torn down.
 *
 * Verification runs: any -SOL* switch other than the menu ones starts straight in flight (ShouldStartInMenu) so the
 * existing smoke scripts are unchanged; -SOLMenu forces the menu, -SOLNoMenu skips it, -SOLMenuSmoke=<all|screens|flow>
 * runs FSOLMenuSmoke.
 */
UCLASS()
class SOLTEST_API USOLMenuSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Declares its dependencies and picks the starting state (main menu, or straight to flight for verification runs)
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Removes the menu widgets, stops the timers and pops the settings' volume mix from this world
    virtual void Deinitialize() override;

    // Shows the main menu (next frame, once the viewport is up) and starts the menu smoke script if requested
    virtual void OnWorldBeginPlay(UWorld& world) override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Returns true when the game starts in the main menu: no -SOL* switch, or -SOLMenu / -SOLMenuSmoke, and no
    // -SOLNoMenu (switch names and the SOL prefix compare case-insensitively, as the engine's own FParse does)
    static bool ShouldStartInMenu();

    // Returns the current menu state
    const SOLMenuState::FMenuState& GetState() const { return mState; }

    // Returns true while the main menu or the pause menu shows
    bool IsMenuShowing() const { return mState.Mode != SOLMenuState::EMenuMode::Playing; }

    // Returns true while the ship's flight input must be suspended
    bool SuspendsFlightInput() const { return SOLMenuState::SuspendsFlightInput(mState); }

    // Fired after every state change
    FSOLOnMenuStateChanged& OnStateChanged() { return mOnStateChanged; }

    // Queues Play (main menu only): spawns the ship above the start body and hands control to it
    void RequestPlay();

    // Queues an Esc press: pauses in flight (not while the map is open), resumes when paused
    void RequestEscape();

    // Queues Resume (pause menu)
    void RequestResume();

    // Queues Quit to menu (pause menu): removes the ship and returns to the main menu
    void RequestQuitToMenu();

    // Quits the application
    void RequestQuitGame();

    // Selects a pause-menu tab (Resume resumes; the others show their page)
    void SelectTab(SOLMenuState::EMenuTab tab);

    // Records that the jump map opened or closed (its own Esc closes it, so Esc does not pause meanwhile)
    void NotifyMapOpen(bool bOpen);

    // Returns the registry index of the body the ship starts above
    int32 GetStartBodyIndex() const { return mStartBodyIndex; }

    // Sets the start body by registry index (ignored when out of range)
    void SetStartBodyIndex(int32 bodyIndex);

    // Returns the start body's registry name
    FString GetStartBodyName() const;

    // Returns how many times Play spawned the ship (verification)
    int32 GetPlayCount() const { return mPlayCount; }

    // Returns the main menu widget, or nullptr before it was first shown
    USOLMainMenuWidget* GetMainMenu() const { return MainMenu; }

    // Returns the pause menu widget, or nullptr before it was first shown
    USOLPauseMenuWidget* GetPauseMenu() const { return PauseMenu; }

    // Returns the local player controller, or nullptr
    APlayerController* GetPlayerController() const;

    // Returns the Demo page's options (session-only; re-read from the NPC subsystem whenever a menu opens)
    const SOLMenuState::FDemoPage& GetDemoPage() const { return mDemoPage; }

    // Replaces the Demo page's options (its steppers, presets and Reset)
    void SetDemoPage(const SOLMenuState::FDemoPage& page) { mDemoPage = page; }

    // Starts the demo with the Demo page's options: from the main menu as Play does (the NPCs follow the ship), from
    // the pause menu at once (the population respawns around the ship) and resumes
    void RequestStartDemo();

    // Stops the NPC ramp and removes every NPC (pause menu Demo tab)
    void RequestStopNpcs();

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Queues an event and arms the next-frame processing
    void QueueEvent(SOLMenuState::EMenuEvent event);

    // Applies the queued events in order
    void ProcessQueuedEvents();

    // Applies one event: state machine, then its world side effects, then the broadcast
    void ApplyEvent(SOLMenuState::EMenuEvent event);

    // Spawns the ship above the start body and possesses a new ship pawn
    void StartFlight();

    // Removes the ship pawn and entity and possesses a new menu camera pawn
    void ReturnToMainMenu();

    // Destroys the controller's pawn and possesses a new pawn of the game mode's class for the current state
    void SwapPawn();

    // Shows the widget for the current state, hides the others, and sets the input mode and cursor
    void RefreshWidgets();

    // Runs one step of the menu smoke script
    void StepSmoke();

    // Ends the settings visit of both menus' Settings pages (a menu opened or closed)
    void EndSettingsVisit();

    // Re-reads the Demo page's options and combat model from the NPC subsystem (console and flag changes show)
    void SyncDemoPageFromNpcs();

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    UPROPERTY(Transient)
    TObjectPtr<USOLJumpSubsystem> Jump;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcSubsystem> Npcs;

    UPROPERTY(Transient)
    TObjectPtr<USOLMainMenuWidget> MainMenu;

    UPROPERTY(Transient)
    TObjectPtr<USOLPauseMenuWidget> PauseMenu;

    SOLMenuState::FMenuState mState;                   // Current menu state
    TArray<SOLMenuState::EMenuEvent> mQueuedEvents;    // Requests waiting for the next frame (reserved once)
    FSOLOnMenuStateChanged mOnStateChanged;            // Listeners after each state change
    TUniquePtr<FSOLMenuSmoke> mSmoke;                  // Verification-only menu script (-SOLMenuSmoke)
    FTimerHandle mProcessTimer;                        // Next-frame processing of the queued events
    FTimerHandle mShowTimer;                           // First showing of the main menu
    FTimerHandle mSmokeTimer;                          // Menu smoke script steps
    double mLastSmokeStepSeconds = 0.0;                // Real time of the last smoke step
    int32 mStartBodyIndex = INDEX_NONE;                // Body the ship starts above
    int32 mPlayCount = 0;                              // Ship spawns through Play
    SOLMenuState::FDemoPage mDemoPage;                 // Demo page options (session-only, SDD 11 answer 6)
    bool mIsDemoStartPending = false;                  // Start demo pressed in the main menu: NPCs follow the ship
};
