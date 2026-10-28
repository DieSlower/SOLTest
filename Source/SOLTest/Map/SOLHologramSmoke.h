/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class APlayerController;
class ASOLBodyVisuals;
class ASOLHologramVisuals;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLMapModeSubsystem;

/**
 * Verification-only map hologram run (-SOLSmokeHologram, SDD 20 plan step 6). Before the map opens it records which
 * real body meshes are hidden (the gas giants hide their placeholders); it opens the map and checks the proxy swap
 * (every proxy visible, the body, ring and gas-giant actors in the controller's HiddenActors, the post-process on, the
 * grid and stems drawn), takes a screenshot of each view (Earth and Moon, Earth close, Jupiter and its moons, the Sun,
 * the inner system, Saturn with its rings), then turns sol.Hologram off (real visuals back, nothing hidden, no grid)
 * and on again (logging the mean GPU frame time of the last view with the look on and off), closes the map and checks
 * that nothing stays hidden and the body meshes are as before. Each check logs a LogSOL PASS/FAIL line; the run then
 * quits. Run it at more than one aspect ratio (-ResX/-ResY).
 */
class SOLTEST_API FSOLHologramSmoke
{
public:

    // Caches the controller, subsystems and visuals actors
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Points the map camera at view index mView
    void AimView() const;

    // Returns the number of replaced real actors (bodies, rings, gas giants) in the controller's HiddenActors
    int32 CountHiddenRealActors() const;

    // Returns the number of hidden body placeholder meshes
    int32 CountHiddenBodyMeshes() const;

    // Advances to the next step
    void NextStep();

    // Logs one checkpoint as PASS or FAIL and counts failures
    void Check(const TCHAR* name, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<ASOLHologramVisuals> mHologram;
    TWeakObjectPtr<ASOLBodyVisuals> mBodyVisuals;
    int32 mExpectedHidden = 0;              // Real visuals actors the look must hide
    int32 mHiddenMeshesBefore = 0;          // Hidden body placeholders before the map opened
    double mGpuMsSum = 0.0;                 // GPU frame time summed over the current measurement
    int32 mGpuSamples = 0;                  // Frames in the current measurement
    int32 mView = 0;                        // Current view of the screenshot tour
    int32 mStep = 0;                        // Current script step
    int32 mStepFrames = 0;                  // Frames spent in the current step
    double mStepTimeS = 0.0;                // Real seconds spent in the current step
    int32 mChecks = 0;                      // Checkpoints logged
    int32 mFailures = 0;                    // Checkpoints failed
    bool mIsFinished = false;               // The script ended (quit requested)
};
