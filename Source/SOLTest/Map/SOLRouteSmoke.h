/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class ASOLShipPawn;
class USOLMapModeSubsystem;
class USOLRouteSubsystem;
class USOLShipSubsystem;

/**
 * Verification-only scripted route run (-SOLSmokeRoute, SDD 20 steps 1-4): drives the REAL input pipeline by injecting
 * key and mouse-button events into the player controller. It opens the map with Shift+M (Move mode by default), cycles
 * the modes with J, P, P (ping-pong), M and M again (no change), adds two waypoints with left clicks at scripted cursor
 * positions, undoes one with Ctrl+right click and adds it again, starts the route with Enter (AUTO on a waypoint order,
 * draft emptied), closes the map with Shift+M (the autopilot must survive the Shift), pauses it with W (route Paused,
 * route kept), resumes it with V (AUTO again), toggles the route overlay with P twice, clears the route with Delete on
 * the map, and opens the map with Shift+J (Jump mode) and closes it with Esc. Finally it flies a short scripted Move
 * (ship set at rest beside its anchor body, waypoints 3 km ahead and then 3 km aside): the first waypoint must be flown
 * through in AUTO and the route must finish at rest in ASSISTED. Each check logs a LogSOL PASS/FAIL line; the run then
 * quits.
 */
class SOLTEST_API FSOLRouteSmoke
{
public:

    // Caches the controller and subsystems and starts the script
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Runs step index's entry actions (key presses, cursor moves)
    void EnterStep(int32 step, ASOLShipPawn& pawn);

    // Runs step index's checks once its frames have passed
    void CheckStep(int32 step, ASOLShipPawn& pawn);

    // Sets the ship at rest beside its anchor body and starts a two-waypoint Move (ahead, then aside: a 90 deg corner)
    void StartScriptedMove(ASOLShipPawn& pawn);

    // Sends a key press to the player controller
    void PressKey(const FKey& key) const;

    // Sends a key release to the player controller
    void ReleaseKey(const FKey& key) const;

    // Presses a key now and releases it on the next frame
    void TapKey(const FKey& key);

    // Logs one checkpoint as PASS or FAIL and counts failures
    void Check(const TCHAR* name, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;
    TWeakObjectPtr<USOLRouteSubsystem> mRoutes;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TArray<FKey, TInlineAllocator<4>> mPendingReleases;     // Tapped keys to release on the next frame
    double mFlyElapsedS = 0.0;                              // Real seconds the scripted Move has flown
    int32 mFlyBodyIndex = INDEX_NONE;                       // Body the scripted Move's waypoints follow
    int32 mStep = 0;                                        // Current script step
    int32 mStepFrames = 0;                                  // Frames spent in the current step
    int32 mChecks = 0;                                      // Checkpoints logged
    int32 mFailures = 0;                                    // Checkpoints failed
    bool mHasEnteredStep = false;                           // The current step's entry actions ran
    bool mIsFinished = false;                               // The script ended (quit requested)
    bool mHasPassedInAuto = false;                          // The scripted Move passed its first waypoint in AUTO
};
