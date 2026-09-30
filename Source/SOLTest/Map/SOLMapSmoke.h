/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapCamera.h"

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class ASOLShipPawn;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMapModeSubsystem;
class USOLSimClockSubsystem;

/**
 * Verification-only scripted jump-map run (-SOLSmokeMap): drives the REAL input pipeline by injecting key,
 * mouse-button, mouse-delta and wheel events into the player controller (APlayerController::InputKey), as
 * -SOLSmokeInput does. It opens the map with J (cursor shown, ship control suspended, map camera active, render
 * viewpoint on the map camera, sim time still advancing), checks that W does nothing while it is open, checks that a
 * left drag leaves the camera alone (it is destination picking, see FSOLMapPickSmoke), right-drags to orbit, wheels
 * to zoom, middle-drags and then Shift+right-drags to pan across the ecliptic plane, takes two screenshots (plain, then
 * with the orbit ellipses), closes with J, checks the ship's input and view are back, reopens, then wheels in over
 * three steps with a screenshot after each (inner system ~9e10 m, Earth mid mesh/icon cross-fade ~8e8 m, close to
 * Earth ~5e7 m with the real mesh only; between the last two it orbits to look at Earth from the side away from the
 * Sun, so the Sun sits right behind Earth and its icon must be hidden), closes with Esc, then screenshots the ship view
 * and quits. At each map screenshot it checks the body overlay's icon state (2c). Each check logs a LogSOL PASS/FAIL
 * line.
 */
class SOLTEST_API FSOLMapSmoke
{
public:

    // Caches the controller and subsystems and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLMapPhase : uint8
    {
        Setup,
        OpenJ,
        SuppressW,
        ShotWide,
        LeftNoop,
        Orbit,
        Zoom,
        Pan,
        PanShift,
        ShotOrbits,
        CloseJ,
        RestoreW,
        ReopenJ,
        ZoomSystem,
        ShotSystem,
        ZoomMid,
        ShotMid,
        OrbitOcclude,
        ShotOcclude,
        ZoomClose,
        ShotClose,
        CloseEsc,
        Finish,
        Done,
    };

    // Runs a phase's entry actions (key presses, records)
    void EnterPhase(ESOLMapPhase phase, ASOLShipPawn& pawn);

    // Queues the orbit drag that puts the map camera on the far side of the focus from the Sun (Sun right behind Earth)
    void QueueOrbitAwayFromSun() const;

    // Runs a phase's per-frame actions (mouse deltas, wheel notches)
    void TickPhase(ESOLMapPhase phase);

    // Runs a phase's exit checks and releases held keys
    void ExitPhase(ESOLMapPhase phase, ASOLShipPawn& pawn);

    // Returns the minimum real duration of a phase in seconds
    static double GetPhaseDuration(ESOLMapPhase phase);

    // Returns the minimum number of frames a phase lasts
    static int32 GetPhaseMinFrames(ESOLMapPhase phase);

    // Checks that the map is open with the cursor free, the ship input suspended and the map camera active
    void CheckOpen(const TCHAR* label, ASOLShipPawn& pawn);

    // Checks that the map is closed with the cursor captured, the ship input back and the ship camera active
    void CheckClosed(const TCHAR* label, ASOLShipPawn& pawn);

    // Logs each body's apparent radius in pixels as seen from the map camera (honest visibility evidence)
    void LogBodyPixelSizes() const;

    // Logs the map overlay's per-body icon alphas and pick radii and checks the icon state expected at a screenshot
    void CheckBodyOverlay(ESOLMapPhase phase);

    // Returns the number of wheel notches a zoom phase injects (0 for other phases)
    static int32 GetZoomNotches(ESOLMapPhase phase);

    // Sends a key press to the player controller
    void PressKey(const FKey& key) const;

    // Sends a key release to the player controller
    void ReleaseKey(const FKey& key) const;

    // Presses a key now and releases it on the next frame
    void TapKey(const FKey& key);

    // Sends one analog axis sample (mouse delta in pixels, wheel notches) to the player controller
    void InjectAxis(const FKey& key, float delta) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;          // Controller the events are injected into
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;          // Map camera state
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;            // Observer, viewpoint and render origin
    TWeakObjectPtr<USOLSimClockSubsystem> mClock;           // Sim time (keeps running while mapped)
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;    // Body positions and radii
    TArray<FKey, TInlineAllocator<4>> mPendingReleases;     // Tapped keys to release on the next frame
    ESOLMapPhase mPhase = ESOLMapPhase::Setup;              // Current phase
    FSOLOrbitCameraState mReferenceState;                   // Map camera state recorded at a phase start
    double mReferenceSimSeconds = 0.0;                      // Sim seconds since J2000 recorded at a phase start
    double mPhaseTimeS = 0.0;                               // Real time in the current phase
    double mTotalTimeS = 0.0;                               // Real time since the script started
    int32 mPhaseFrames = 0;                                 // Frames in the current phase
    int32 mChecksPassed = 0;                                // Checks that passed
    int32 mChecksRun = 0;                                   // Checks evaluated
};
