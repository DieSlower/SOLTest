/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class ASOLFlightHud;
class ASOLShipPawn;
class USOLOverlayMenuWidget;
class USOLSettingsSubsystem;

/**
 * Verification-only overlay run (-SOLSmokeOverlay, SDD 22 and SDD 34 section 12): icons on at flight start, O turns
 * every overlay off and on (icons and orbit lines), Alt+O toggles only the orbit lines without free look or the master,
 * Right Shift+O cycles the overlay profile (HUD tag; wraps past disabled profiles; master and orbit lines are per
 * profile), Left Shift+O does nothing, Ctrl+O opens the Overlay Menu (5 profile tabs, 17 rows, orbit rows indented;
 * its hologram frame and last row inside the view on the 17- and 18-row pages),
 * the Lanes row (SDD 38) is listed in every profile, Space on it toggles it in memory (no write while the menu is
 * open), the master O hides it and each profile keeps its own Lanes bit through a cycle and a re-enable, Slate clicks
 * on tabs and rows enable a profile, uncheck a row, flip the Orbit Lines row and the Profile enabled row
 * (a re-enabled profile keeps its own values), PageUp views the tab above, Esc closes the menu with one settings
 * write, the pawn's leave-flight write point writes an owed change once and nothing when nothing changed (and leaves
 * it to the settings subsystem's shutdown write on quit), and the hologram cursor is the viewport's software cursor.
 * Screenshots with the UI go to Saved/Screenshots/OverlaySmoke_*.png. Runs with the smoke settings slot (never the
 * player's file) and quits at the end. Each check is logged as "SmokeOverlay CHECK <label> PASS|FAIL: <detail>".
 */
class SOLTEST_API FSOLOverlaySmoke
{
public:

    // Caches the controller, HUD and settings and starts the first step
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Runs the step's action at its start
    void EnterStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's checks at its end
    void ExitStep(int32 step, ASOLShipPawn& pawn);

    // Sends a key press / release to the player controller (game input)
    void PressKey(const FKey& key) const;
    void ReleaseKey(const FKey& key) const;

    // Presses a key with a modifier held (both released next frame)
    void TapChord(const FKey& modifier, const FKey& key);

    // Sends a real Slate left click at a screen point
    void ClickAt(const FVector2D& at) const;

    // Sends a key press and release through Slate (to the focused widget)
    void SlateKey(const FKey& key) const;

    // Moves the menu's highlight to a display row with Slate Down presses
    void HighlightRow(const USOLOverlayMenuWidget& menu, int32 row) const;

    // Requests a window screenshot with the UI
    static void TakeUiShot(const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLSettingsSubsystem> mSettings;
    TArray<FKey, TInlineAllocator<4>> mPendingReleases;     // Keys to release on the next frame
    int32 mStep = 0;
    double mStepTimeS = 0.0;
    int32 mStepFrames = 0;
    int32 mStartIcons = 0;                                  // Icons drawn with the overlays on at the start
    int32 mSavesBefore = 0;                                 // Slot writes before the step's action
    int32 mSavesCompletedBefore = 0;                        // Finished slot writes before the step's action
    bool mWasOrbitLinesShown = false;
    bool mWasDarkening = false;
    bool mWasLanesShown = false;                            // Lanes shown before a Lanes step's Space
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
};
