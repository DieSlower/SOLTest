/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLShipSubsystem;
class UWorld;

/**
 * Verification-only player thruster plume script (-SOLSmokePlayerThruster, SDD 51 part 51d). With no NPCs, the space
 * dust and orbit lines off in memory (no player setting read or written), it flies the player ship in manual mode with
 * the Alt free-look camera swung to the side, through idle, about 1 G (blue-white), full forward 3 G (yellow-white),
 * boost (9 G, clamped red-orange), a Low-quality and a VR override of the hull light, the developer cvar sol.PlayerPlume
 * off and on, and a War power order to Mars (red-orange pulses). At the end of each state it checks Q1 the floats on
 * the thruster mesh equal the plume the pawn computed (ASOLShipPawn::GetPlayerPlume), Q2 that plume against
 * SOLThruster's colour, brightness and length of the step's forward G (the War power colour while the order flies),
 * Q3 the hull light's visibility, intensity and colour, and the state's own expectation (the G band and colour band).
 * One screenshot per state (PlayerThruster_<w>x<h>_<state>.png). Each check logs a LogSOL PASS / FAIL line, then a
 * SUMMARY line, and the game quits.
 *
 * With -SOLSmokePlayerThrusterProfile (-game only) it instead flies the War power order and profiles sol.PlayerPlume 1
 * (A) against 0 (B: no plume mesh, no hull light) in interleaved windows (A B B A ...), logging the GPU frame, game
 * thread and frame time means per window as one PROFILE line.
 */
class SOLTEST_API FSOLPlayerThrusterSmoke
{
public:

    // Caches the subsystems, sets up the overrides and the camera, and starts the first state
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script states, in order (the profile goes from Settle to Profile)
    enum class ESOLPlayerThrusterPhase : uint8
    {
        Settle,             // Manual mode, the free-look swing, the ship at rest
        Idle,               // No thrust: no plume, no light
        Normal,             // About 1 G forward: blue-white
        Full,               // Full forward, 3 G: yellow-white
        Boost,              // Full forward with boost, 9 G: clamped, red-orange
        LowQuality,         // Full forward at the Low preset override: plume on, light off
        VrLow,              // ...and in VR: light on
        CvarOff,            // sol.PlayerPlume 0: mesh and light hidden
        WarPower,           // A War power order to Mars, until a pulse
        WarRecover,         // ...until a recovery at about 1.5 G: still red-orange
        Profile,            // Interleaved sol.PlayerPlume windows under the War power order
        Done,
    };

    // Runs the actions of a state's start (inputs, overrides, the order)
    void EnterPhase(ASOLShipPawn& pawn, ESOLPlayerThrusterPhase phase);

    // Runs a state's checks and its screenshot
    void ExitPhase(ASOLShipPawn& pawn, ESOLPlayerThrusterPhase phase);

    // Returns whether a state may end now (time, frames, and for War power the pulse it waits for)
    bool IsPhaseDone(ESOLPlayerThrusterPhase phase) const;

    // Checks Q1-Q3 and the state's own expectation
    void CheckPlume(ASOLShipPawn& pawn, const TCHAR* state, double minG, double maxG, double minC, double maxC,
        bool bExpectLight, bool bWarPower);

    // Runs one frame of the interleaved profile; returns true once every window is done
    bool TickProfile(UWorld* world, float realDeltaSeconds);

    // Logs the verdicts and the SUMMARY line, restores the overrides and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn);

    // Requests a screenshot with the HUD named PlayerThruster_<width>x<height>_<name>.png and holds the state's inputs
    // until it is written
    void TakeShot(UWorld* world, const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    ESOLPlayerThrusterPhase mPhase = ESOLPlayerThrusterPhase::Settle;
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    int32 mPhaseFrames = 0;
    double mShotHoldS = 0.0;                // Time left holding the state's inputs for its screenshot
    bool mIsProfile = false;               // -SOLSmokePlayerThrusterProfile (-game only)
    int32 mMarsIndex = INDEX_NONE;

    // The profile
    int32 mProfileWindow = 0;               // Window index
    int32 mProfileFrame = 0;                // Frame in the window
    double mSumGpuMs = 0.0;                 // Window sums over the measured frames
    double mSumGameMs = 0.0;
    double mSumFrameMs = 0.0;
    double mSumThrustG = 0.0;
    int32 mSumFrames = 0;
    int32 mLitFrames = 0;                   // Measured frames with the plume burning

    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
