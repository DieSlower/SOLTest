/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class ASOLThrusterRibbons;
class UWorld;

// One synthetic flame configuration of the parity script: the place in the camera frame (forward, right, up metres:
// where the L_p mark sits for side views, the nozzle for the chase and front views), the plume state, the nozzle, the
// view angle between the exhaust and the camera's forward (90 side, 45 oblique, 10 chase, 170 front), the turn rate,
// the anti-aliasing method (-1 the project's), the flame count (1, or a grid) and the Niagara shot's threshold
struct FSOLFlameParityConfig
{
    FString Name;
    FVector3d PlaceM = FVector3d::ZeroVector;
    double PlumeLengthM = 0.0;
    double Brightness = 1.0;
    double ColourC = 0.6;
    float NozzleRadiusM = 1.5f;
    double NozzleAftM = 15.0;
    double ViewDeg = 90.0;
    double TurnDegPerS = 90.0;
    int32 AntiAliasing = -1;
    int32 Trails = 1;
    const TCHAR* NiagaraPx = TEXT("0");
};

/**
 * Verification-only thruster ribbon renderer parity script (-SOLSmokeRibbonParity, SDD 51 part 51e step 5; -game with
 * a real RHI, run with -SOLNpcCount=0). With the space dust, orbit lines and player plume off in memory (no player
 * setting read or written) and the ribbons' history clock frozen, it seeds synthetic curved ribbons (24 samples of a
 * ship turning 90 degrees a second, NPC feed keys from 0) and feeds them every frame at fixed places in the camera's
 * frame, for a sweep of configurations: near (160 m, a 90 m ribbon), far (25 km, a 900 m ribbon whose 1.5 m width is
 * far under a pixel, so the 1 px floor draws it) at four quarter-pixel phases, farfat (the far geometry 10+ px wide),
 * multi (40 trails, 14 of them long enough for Niagara, so the pool shot spans two pool blocks), each with the project's
 * anti-aliasing and with r.AntiAliasingMethod 0 (identical geometry must then rasterise identically). For each it
 * shows the ribbons through the Niagara set (the configuration's sol.ThrusterRibbonNiagaraPx), then the strip pool (a
 * huge threshold), twice each (the A/A noise floor), then with sol.ThrusterRibbons 0, checking the renderers' trail
 * counts, logging the view state and taking one screenshot per state without the HUD
 * (RibbonParity_<w>x<h>_<config>_<state>.png); the crops are diffed outside the game. Each check logs a LogSOL PASS /
 * FAIL line, then a SUMMARY line, and the game quits.
 */
class SOLTEST_API FSOLThrusterRibbonParitySmoke
{
public:

    // Removes the ribbons' verification feeder (it points at this script)
    ~FSOLThrusterRibbonParitySmoke();

    // Caches the actors, sets up the overrides and the frozen clock
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Feeds the synthetic trails for this frame at the current configuration (the ribbons' verification feeder, called with
    // this frame's final camera)
    void FeedTrail(const FVector3d& cameraM);

    // Seeds the synthetic histories for the current configuration (the ribbons drop their histories while switched off)
    void SeedTrail();

    // Applies the cvars of the current shot
    void EnterShot();

    // Checks the current shot's trail counts and takes its screenshot
    void ExitShot(UWorld* world);

    // Logs the verdicts and the SUMMARY line, restores the overrides and quits through the game mode
    void Finish(ASOLShipPawn& pawn);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    // Returns the current configuration's exhaust direction at the frame of now (render-space axes)
    FVector3d ExhaustNow() const;

    TArray<FSOLFlameParityConfig> mConfigs;             // The legacy parity sweep or, with -SOLSmokeFlameSeam, the seam
                                                        // sweep (built in Start)
    TWeakObjectPtr<ASOLThrusterRibbons> mRibbons;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    FVector3d mForward = FVector3d::ForwardVector;      // Camera basis captured after the settle
    FVector3d mRight = FVector3d::RightVector;
    FVector3d mUp = FVector3d::UpVector;
    bool mIsSettled = false;
    FString mAntiAliasingDefault;                       // r.AntiAliasingMethod at the start (restored at the end)
    int32 mRange = 0;                                   // Configuration index
    int32 mShot = 0;                                    // Shot index in the configuration
    int32 mShotFrames = 0;
    double mShotTimeS = 0.0;
    double mHoldS = 0.0;                                // Time left after a screenshot request
    double mTotalTimeS = 0.0;
    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
