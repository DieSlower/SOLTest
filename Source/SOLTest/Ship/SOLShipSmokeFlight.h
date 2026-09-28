/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class FSOLBodyRegistry;
class USOLAnchorSubsystem;
class USOLShipSubsystem;

/**
 * Verification-only scripted flight (-SOLSmokeFlight): drives the player ship through fixed phases without a pawn
 * (hover in assist, assisted cruise, Newtonian thrust and coast, pitch, assisted braking, hands-off hover under 1 d/s
 * time-warp, Newtonian collision with Earth under that warp, resting on the surface under that warp), logs LogSOL
 * checkpoints with a PASS/FAIL verdict per phase, resets the warp to 1x and reports when it has finished.
 */
class SOLTEST_API FSOLShipSmokeFlight
{
public:

    // Starts the first phase
    void Start(USOLShipSubsystem& ships, const USOLAnchorSubsystem& anchor, const FSOLBodyRegistry& registry);

    // Advances the script after a ship step by the frame's real delta; returns true once every phase has finished
    bool Update(USOLShipSubsystem& ships, const USOLAnchorSubsystem& anchor, const FSOLBodyRegistry& registry,
        float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLSmokePhase : uint8
    {
        Hover,
        Cruise,
        NewtonThrust,
        NewtonCoast,
        Pitch,
        Brake,
        Warp,
        Collide,
        Surface,
        Done,
    };

    // Measurements of the ship taken at a checkpoint
    struct FSOLSmokeSample
    {
        FVector3d RelativeVelocityMps = FVector3d::ZeroVector;  // Velocity relative to the active reference frame
        FVector3d ForwardDir = FVector3d::XAxisVector;          // Ship forward (Unreal-handed universe frame)
        FVector3d UpDir = FVector3d::ZAxisVector;               // Ship up
        FVector3d RadialDir = FVector3d::ZAxisVector;           // Unit vector from the nearest body to the ship
        double AbsoluteSpeedMps = 0.0;                          // Sun-frame speed
        double AltitudeM = 0.0;                                 // Above the nearest body's surface
        int32 NearestBody = INDEX_NONE;                         // Body whose surface is nearest
    };

    // Measures the ship now
    static FSOLSmokeSample Sample(const USOLShipSubsystem& ships, const FSOLBodyRegistry& registry);

    // Logs one checkpoint line
    void LogSample(const TCHAR* label, const FSOLSmokeSample& sample, const USOLShipSubsystem& ships,
        const USOLAnchorSubsystem& anchor, const FSOLBodyRegistry& registry) const;

    // Sets up the control (and for the collision phase, the ship's state) for a phase
    void BeginPhase(ESOLSmokePhase phase, USOLShipSubsystem& ships, const USOLAnchorSubsystem& anchor,
        const FSOLBodyRegistry& registry);

    // Evaluates and logs the verdict of the phase that just ended
    void EndPhase(const FSOLSmokeSample& sample, const USOLShipSubsystem& ships, const FSOLBodyRegistry& registry);

    // Returns the phase's scripted duration in seconds
    static double GetPhaseDuration(ESOLSmokePhase phase);

    // Returns the phase's display name
    static const TCHAR* GetPhaseName(ESOLSmokePhase phase);

    // Sets the sim clock's time-warp step (0 = 1x) through the clock's public stepping API
    static void SetWarpIndex(const USOLShipSubsystem& ships, int32 warpIndex);

    // Tracks the worst altitude drift and whether the anchor stayed Earth during the warp phases
    void TrackWarpPhase(const FSOLSmokeSample& sample, const USOLAnchorSubsystem& anchor,
        const FSOLBodyRegistry& registry);

    ESOLSmokePhase mPhase = ESOLSmokePhase::Hover;   // Current phase
    FSOLSmokeSample mPhaseStart;                     // Sample taken when the current phase began
    double mReferenceAltitudeM = 0.0;                // Altitude a warp phase must hold (start altitude, or ship radius)
    double mMaxAltitudeDriftM = 0.0;                 // Worst |altitude - reference altitude| during a warp phase
    bool mAnchorStayedEarth = true;                  // False once the anchor left Earth during a warp phase
    double mPhaseTimeS = 0.0;                        // Real time spent in the current phase
    double mTotalTimeS = 0.0;                        // Real time since the script started
    double mNextLogTimeS = 0.0;                      // Phase time of the next periodic checkpoint
    int32 mChecksPassed = 0;                         // Phases whose verdict passed
    int32 mChecksRun = 0;                            // Phases evaluated
};
