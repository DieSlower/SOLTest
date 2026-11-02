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
class USOLSettingsSubsystem;

/**
 * Verification-only overlay run (-SOLSmokeOverlay, SDD 22): icons on at flight start, O turns every overlay off and
 * on (icons and orbit lines), Alt+O toggles only the orbit lines without free look or the master, Shift+O toggles the
 * darkening, Ctrl+O opens the Overlay Menu (one row per overlay), a Slate click on a row toggles it, Esc closes the
 * menu and writes the settings once, and the hologram cursor is the viewport's software cursor. Screenshots with the
 * UI go to Saved/Screenshots/OverlaySmoke_*.png. Runs with the smoke settings slot (never the player's file) and
 * quits at the end. Each check is logged as "SmokeOverlay CHECK <label> PASS|FAIL: <detail>".
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
    int32 mSavesBefore = 0;                                 // Slot writes before the menu closed
    bool mWasOrbitLinesShown = false;
    bool mWasDarkening = false;
    bool mWasPlanetsChecked = false;
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
};
