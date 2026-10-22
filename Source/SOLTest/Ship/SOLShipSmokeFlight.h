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
 * (hover in assist, assisted cruise at <= 1.5 G, manual thrust and coast, manual pitch then stop spin, assisted
 * braking, hands-off hover under 1 d/s time-warp, manual collision with Earth under that warp, resting on the surface
 * under that warp, a manual 9 G boost to G-LOC and recovery, the autopilot to the Moon under 1000x user warp), logs
 * LogSOL checkpoints with a PASS/FAIL verdict per phase, resets the warp to 1x and reports when it has finished.
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
        GLoc,
        Autopilot,
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

    // Takes one verification screenshot the first time its condition holds
    void TakeShotOnce(const USOLShipSubsystem& ships, uint32 shotBit, const TCHAR* name, bool bCondition);

    // Tracks the per-frame G, G-LOC and autopilot measurements of the current phase (SDD 18)
    void TrackFlightPhase(USOLShipSubsystem& ships, const FSOLBodyRegistry& registry);

    // Tracks the worst altitude drift and whether the anchor stayed Earth during the warp phases
    void TrackWarpPhase(const FSOLSmokeSample& sample, const USOLAnchorSubsystem& anchor,
        const FSOLBodyRegistry& registry);

    ESOLSmokePhase mPhase = ESOLSmokePhase::Hover;   // Current phase
    FSOLSmokeSample mPhaseStart;                     // Sample taken when the current phase began
    double mReferenceAltitudeM = 0.0;                // Altitude a warp phase must hold (start altitude, or ship radius)
    double mMaxAltitudeDriftM = 0.0;                 // Worst |altitude - reference altitude| during a warp phase
    bool mAnchorStayedEarth = true;                  // False once the anchor left Earth during a warp phase
    double mMaxGLoadG = 0.0;                         // Highest pilot G-load seen in the current phase
    double mPhaseStartMeter = 1.0;                   // Pilot G meter when the current phase began
    uint32 mShotsTaken = 0;                          // Bits of the verification screenshots already taken
    double mMaxBlackoutGLoadG = 0.0;                 // Highest G-load while blacked out (input must be ignored)
    double mBlackoutStartS = -1.0;                   // Phase time G-LOC began, or -1
    double mBlackoutEndS = -1.0;                     // Phase time G-LOC ended, or -1
    double mFirstEtaS = -1.0;                        // Autopilot ETA at the first engaged frame, or -1
    double mEtaAfterS = -1.0;                        // Autopilot ETA a couple of real seconds later, or -1
    double mArrivalS = -1.0;                         // Phase time the autopilot arrived, or -1
    double mArrivalDistanceM = 0.0;                  // Distance to the target body's centre at arrival
    double mArrivalRelSpeedMps = 0.0;                // Speed relative to the target body at arrival
    double mWarpAtArrival = 0.0;                     // Warp factor at arrival (the autopilot must not change it)
    double mPhaseTimeS = 0.0;                        // Real time spent in the current phase
    double mTotalTimeS = 0.0;                        // Real time since the script started
    double mNextLogTimeS = 0.0;                      // Phase time of the next periodic checkpoint
    int32 mChecksPassed = 0;                         // Phases whose verdict passed
    int32 mChecksRun = 0;                            // Phases evaluated
};
