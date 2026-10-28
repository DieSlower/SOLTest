/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class APlayerController;
class ASOLFlightHud;
class ASOLShipPawn;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMapModeSubsystem;
class USOLRouteSubsystem;
class USOLShipSubsystem;

/**
 * Verification-only route overlay run (-SOLSmokeRouteOverlay, SDD 20 plan step 5). It sets the ship at rest beside its
 * anchor body, starts a three-waypoint Patrol Loop in front of the camera and pauses it (the autopilot is disengaged,
 * so nothing moves), then checks in the flight view: three legs (one dashed closing leg), arrows, three markers each
 * within 2 px of the engine's own projection of the waypoint (PlayerController
 * ProjectWorldLocationToScreen), the above/below-plane flags, the ship leg, the PAUSED tag and the HUD route marker
 * (not overlapping the key hints). P (the overlay flag) off must hide everything and on must bring it back. Then it
 * opens the map, aims its camera at the route and repeats the projection checks there, and plans a draft of two
 * waypoints with scripted picks, checking the Secondary cursor segment while each pick drags and the draft markers
 * after. Screenshots in flight, map and with the draft. Each check logs a LogSOL PASS/FAIL line; the run then quits.
 */
class SOLTEST_API FSOLRouteOverlaySmoke
{
public:

    // Caches the controller and subsystems
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Sets the ship at rest relative to its anchor body (no spin)
    void HoldShipAtRest() const;

    // Starts the Patrol Loop of three waypoints in front of the camera and pauses it; false on failure
    bool StartPausedRoute();

    // Aims the map camera at the route's waypoints
    void AimMapCamera() const;

    // Checks the route markers against the engine projection; label names the view
    void CheckRouteMarkers(const TCHAR* label, const FVector3d& planePointM);

    // Advances to the next step
    void NextStep();

    // Logs one checkpoint as PASS or FAIL and counts failures
    void Check(const TCHAR* name, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;
    TWeakObjectPtr<USOLRouteSubsystem> mRoutes;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    FVector2D mViewportPx = FVector2D::ZeroVector;          // Viewport size at the route start
    int32 mBodyIndex = INDEX_NONE;                          // Body the waypoints follow
    int32 mStep = 0;                                        // Current script step
    int32 mStepFrames = 0;                                  // Frames spent in the current step
    double mStepTimeS = 0.0;                                // Real seconds spent in the current step
    int32 mChecks = 0;                                      // Checkpoints logged
    int32 mFailures = 0;                                    // Checkpoints failed
    bool mIsFinished = false;                               // The script ended (quit requested)
};
