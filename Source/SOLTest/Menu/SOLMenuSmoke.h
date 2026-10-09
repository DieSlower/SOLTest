/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class USOLMenuSubsystem;

/**
 * Verification-only menu script (-SOLMenuSmoke=<all|screens|flow>, SDD 8 8b/8c). Driven by USOLMenuSubsystem every
 * frame. It walks the main menu (screenshot), the start-location picker with Mars chosen (the menu camera moves to
 * Mars; screenshot), the Controls and About pages, then Play from Earth (ship entity, ship pawn, ship mapping, hidden
 * cursor), thrust through the pawn (the ship speeds up), Esc through the real ship mapping (pause: input suspended,
 * cursor shown), every pause tab (screenshots), Esc through Slate to the focused pause menu (resume: input back), J
 * then Esc (the map's Esc closes the map and does not pause), and Quit to menu followed by Play twice in a row (the
 * ship is removed and spawned again each time). Each check logs a LogSOL PASS/FAIL line; screenshots (with the UI)
 * go to Saved/Screenshots as MenuSmoke_*.png, except in the "flow" scenario. It quits at the end.
 */
class SOLTEST_API FSOLMenuSmoke
{
public:

    // Reads the scenario and starts the first phase
    void Start(USOLMenuSubsystem& menu, const FString& scenario);

    // Runs one step of the script; returns true once it has finished (and requested the quit)
    bool Update(USOLMenuSubsystem& menu, double realDeltaSeconds);

private:

    // Script phases, in order
    enum class EPhase : uint8
    {
        MainMenu,
        StartPicker,
        MainControls,
        MainAbout,
        Play,
        Thrust,
        Pause,
        TabControls,
        TabInfo,
        TabSettings,
        TabAbout,
        TabQuit,
        ResumeSlate,
        MapOpen,
        MapEscape,
        PauseAgain,
        QuitToMenu,
        PlayAgain,
        PauseThird,
        QuitToMenuAgain,
        PlayThird,
        Done,
    };

    // Runs a phase's entry actions
    void EnterPhase(EPhase phase, USOLMenuSubsystem& menu);

    // Runs a phase's exit checks
    void ExitPhase(EPhase phase, USOLMenuSubsystem& menu);

    // Returns the minimum real duration of a phase in seconds
    static double GetPhaseDuration(EPhase phase);

    // Returns the screenshot name of a phase, or nullptr when it takes none
    static const TCHAR* GetPhaseShot(EPhase phase);

    // Checks the after-Play state: flight mode, ship entity, ship pawn with its mapping, hidden cursor, play count
    void CheckFlight(USOLMenuSubsystem& menu, const TCHAR* label, int32 expectedPlays);

    // Checks the main-menu state: menu mode, no ship entity, the menu camera pawn, visible cursor
    void CheckMainMenu(USOLMenuSubsystem& menu, const TCHAR* label);

    // Checks the paused state: pause mode, ship mapping removed, visible cursor
    void CheckPaused(USOLMenuSubsystem& menu, const TCHAR* label);

    // Presses a game key now (released on the next step)
    void TapKey(const FKey& key);

    // Sends a key press and release through Slate to the focused widget
    void SlateKey(const FKey& key) const;

    // Requests a screenshot with the UI to Saved/Screenshots
    void TakeShot(const TCHAR* name) const;

    // Returns the ship's speed relative to its reference frame (m/s), 0 without a ship
    double GetRelativeSpeedMps(const USOLMenuSubsystem& menu) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;  // Controller the game keys are injected into
    TArray<FKey> mPendingReleases;                  // Keys to release on the next step
    EPhase mPhase = EPhase::MainMenu;               // Current phase
    double mPhaseElapsedS = 0.0;                    // Real time in the current phase
    double mStartSpeedMps = 0.0;                    // Relative speed when the thrust phase began
    int32 mSlateKeysBefore = 0;                     // Pause menu Slate key count before the Slate Esc
    int32 mPassed = 0;                              // Checks passed
    int32 mFailed = 0;                              // Checks failed
    bool mIsShotPending = false;                    // A screenshot was requested; the phase waits one settle period
    bool mTakesShots = true;                        // False in the "flow" scenario
};
