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
class USOLBodyRegistrySubsystem;
class USOLShipSubsystem;
class USOLTargetingSubsystem;

/**
 * Verification-only HUD script (-SOLSmokeHud). Game keys (T, O, F3, W) are injected into the player controller with
 * FInputKeyEventArgs::CreateSimulated exactly like -SOLSmokeInput, so the mapping context and the pawn's callbacks run
 * end to end; keys meant for the open F3 panel go through FSlateApplication::ProcessKeyDownEvent to the focused widget,
 * as a real keyboard would (the log says if Slate did not deliver one and the panel was called directly instead).
 * It checks the O toggle, that W does nothing while the panel is open and works again after it closes, digit stepping
 * with carry, the unit cycle, setting the cap from the body list, closing with F3 and Esc, the radar's '-' manual
 * zoom and Home back to AUTO, logging a LogSOL PASS/FAIL line per check. It ends with two screenshots: the HUD over
 * Earth with Earth targeted and the radar zoomed in manually, and the orbit ellipses seen from 3 AU above the Sun;
 * then it quits.
 */
class SOLTEST_API FSOLHudSmoke
{
public:

    // Caches the controller, HUD and subsystems and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLHudPhase : uint8
    {
        Setup,
        SelectEarth,
        OrbitsOn,
        OrbitsOff,
        PredictedPath,
        PanelOpen,
        PanelBlocksThrust,
        DigitUp,
        DigitDown,
        UnitCycle,
        ListSelect,
        PanelClose,
        ThrustAfterClose,
        EscOpen,
        EscClose,
        RadarZoomIn,
        HudShotSetup,
        HudShot,
        RadarAuto,
        OrbitShotSetup,
        OrbitShot,
        Done,
    };

    // Runs a phase's entry actions (key presses, setup)
    void EnterPhase(ESOLHudPhase phase, ASOLShipPawn& pawn);

    // Runs a phase's exit checks
    void ExitPhase(ESOLHudPhase phase, ASOLShipPawn& pawn);

    // Returns the minimum real duration of a phase in seconds
    static double GetPhaseDuration(ESOLHudPhase phase);

    // Sends a key press to the player controller (game input)
    void PressKey(const FKey& key) const;

    // Sends a key release to the player controller (game input)
    void ReleaseKey(const FKey& key) const;

    // Presses a game key now and releases it on the next frame
    void TapKey(const FKey& key);

    // Sends a key press and release through Slate to the focused widget (the open panel)
    void SlateKey(ASOLShipPawn& pawn, const FKey& key);

    // Teleports the ship to a position (Unreal-handed m) with a velocity, facing a direction, not rotating
    void SetupShip(const FVector3d& positionM, const FVector3d& forwardDir, const FVector3d& velocityMps) const;

    // Returns the ship's velocity relative to the active reference frame
    FVector3d GetRelativeVelocityMps() const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;          // Controller the game keys are injected into
    TWeakObjectPtr<ASOLFlightHud> mHud;                     // The flight HUD
    TWeakObjectPtr<USOLShipSubsystem> mShips;               // Ship state and control
    TWeakObjectPtr<USOLTargetingSubsystem> mTargeting;      // Selection
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;    // Body positions
    TArray<FKey, TInlineAllocator<4>> mPendingReleases;     // Tapped game keys to release on the next frame
    ESOLHudPhase mPhase = ESOLHudPhase::Setup;              // Current phase
    double mPhaseTimeS = 0.0;                               // Real time in the current phase
    double mTotalTimeS = 0.0;                               // Real time since the script started
    double mReferenceValue = 0.0;                           // Value recorded at a phase boundary (cap, speed)
    int32 mPhaseFrames = 0;                                 // Frames in the current phase
    int32 mRadarZoomTaps = 0;                               // '-' presses made so far in the RadarZoomIn phase
    int32 mEarthIndex = INDEX_NONE;                         // Registry index of Earth
    int32 mSlateMisses = 0;                                 // Panel keys Slate did not deliver (sent directly)
    int32 mChecksPassed = 0;                                // Checks that passed
    int32 mChecksRun = 0;                                   // Checks evaluated
};
