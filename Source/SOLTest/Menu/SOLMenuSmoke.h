/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSettingsModel.h"

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
 *
 * Settings (8d), all through the Settings page's own functions (the paths its controls call), on the smoke's own save
 * slot: in the main menu it changes volumes, mouse, HUD scale and the quality preset over two Applies (nothing is
 * written yet), leaves the page (exactly one slot write), visits it again without changes (no write, file untouched),
 * re-reads the slot from disk as a restart would (values restored), previews a volume and reverts it, and probes the
 * effects class's mixed volume on the audio thread. In the pause menu it first checks the rebind capture through real
 * Slate events while the keyboard focus sits on the Resume tab (a hover over it while listening does not move the
 * focus): Space becomes the key and clicks nothing, Enter for the map toggle is refused and clicks nothing, and a right
 * mouse press becomes the key; the pause menu sees none of them and stays open. Then it tries a fixed key (refused), a
 * conflicting
 * key (reported), rebinds Fire to K and applies (the pawn's mapping has K, not the left mouse), checks the Controls tab
 * shows K, resumes (the second write), then holds the left mouse (no bolt) and K (bolts fire). At the end it restores
 * the starting values and default keys through the page (the third write) and checks the slot and the engine's
 * quality level are back.
 *
 * Demo (SDD 11, Part 10e): the main menu's Demo page (stepper and preset, screenshot), the pause menu's Demo tab
 * (screenshot), Ctrl+F1 through the ship mapping (pauses) and through Slate (resumes), then a demo applied from the
 * pause menu's Demo tab with 100k ships (the population spawns and the stats overlay shows; screenshot), Ctrl+F2 hides
 * the overlay, and Stop NPCs removes the population.
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
        MainDemo,
        MainSettings,
        SettingsChange,
        SettingsLeave,
        SettingsNoChange,
        SettingsReload,
        SettingsRevert,
        AudioProbe,
        Play,
        Thrust,
        Pause,
        TabControls,
        TabInfo,
        TabSettings,
        RebindCapture,
        RebindFire,
        ControlsRebound,
        TabDemo,
        TabAbout,
        TabQuit,
        ResumeSlate,
        HotkeyPause,
        HotkeyResume,
        FireOldKey,
        FireNewKey,
        MapOpen,
        MapEscape,
        PauseAgain,
        QuitToMenu,
        PlayAgain,
        PauseThird,
        QuitToMenuAgain,
        PlayThird,
        RestorePause,
        RestoreSettings,
        RestoreResume,
        DemoApply,
        DemoRunning,
        StatsHotkey,
        DemoStop,
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

    // Presses a game key now and holds it until ReleaseKey
    void PressKey(const FKey& key);

    // Releases a held game key
    void ReleaseKey(const FKey& key);

    // Runs a settings phase's entry actions (part 8d)
    void EnterSettingsPhase(EPhase phase, USOLMenuSubsystem& menu);

    // Runs a settings phase's exit checks (part 8d)
    void ExitSettingsPhase(EPhase phase, USOLMenuSubsystem& menu);

    // Returns the live bolt count of the combat subsystem (0 without it)
    static int32 GetBoltCount(const USOLMenuSubsystem& menu);

    // Sends a key press and release through Slate to the focused widget (with Left Ctrl held when bControl)
    void SlateKey(const FKey& key, bool bControl = false) const;

    // Sends a mouse button press and release through Slate at the cursor
    void SlateMouseButton(const FKey& button) const;

    // Drives the rebind capture through real Slate events with the focus on another button (listening must never
    // click other buttons); the checks follow in ExitSettingsPhase, after a queued click would have been applied
    void RunRebindCapture(USOLMenuSubsystem& menu);

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
    SOLSettingsModel::FSettingsValues mOriginalValues;  // Settings at the start (restored at the end)
    SOLSettingsModel::FSettingsValues mExpectedValues;  // Settings the two Applies should have committed
    FDateTime mSlotTimeStamp;                       // Settings slot file time after the first write
    int32 mOriginalQualityLevel = INDEX_NONE;       // Engine scalability level at the start (-1 = custom)
    int32 mSaveRequestsBefore = 0;                  // Settings slot writes before the current check
    int32 mBoltsBefore = 0;                         // Live bolts before a fire phase
    bool mRevertPreviewOk = false;                  // The volume preview reached the live values before Revert
    int32 mSlateKeysBefore = 0;                     // Pause menu Slate key count before the Slate Esc / capture
    FString mCaptureDetail;                         // What the rebind capture phase saw (for its check line)
    bool mIsCaptureOk = false;                      // Space, Enter and the right mouse were captured as expected
    int32 mPassed = 0;                              // Checks passed
    int32 mFailed = 0;                              // Checks failed
    bool mIsShotPending = false;                    // A screenshot was requested; the phase waits one settle period
    bool mTakesShots = true;                        // False in the "flow" scenario
};
