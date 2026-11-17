/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Traffic/SOLLaneTrafficMath.h"

#include "CoreMinimal.h"

class ASOLFlightHud;
class ASOLNpcVisuals;
class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLLaneTrafficSubsystem;
class USOLNpcSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;

/**
 * Verification-only AUTO lane rules script (-SOLSmokeAutoLanes, SDD 50 section 8 item 2, V7). It requests an NPC
 * population (unless one is already requested), shows the lanes and tunnels through their verification overrides and
 * turns the orbit lines and the space dust off in memory, so no player setting is read or written. On the Earth - Mars
 * lane it places the ship and orders the autopilot to Mars under time warp, in short bounded cases: an approach from
 * outside the halo at 1.25 x the traffic speed (it must merge on the right half within 5 % of the traffic speed), a
 * hold engaged on the wrong half (it must move to the right-hand track at the traffic speed, show the LANE tag and an
 * ETA slower than the straight trip), the whole leg under 86400x (held within 5 % of the traffic speed, left at Mars's
 * end zone, arrived) and a War power move from inside the lane (no lane mode, no tag, a G load above the sustained
 * limit
 * with the meter in its pulse band, arriving before plain AUTO's straight estimate) and a route stop waypoint
 * inside the lane (it leaves the lane the braking distance short, never re-enters and stops at the waypoint). A path
 * square across the lane must find no leg (D20). Each check
 * logs a LogSOL CHECK PASS / FAIL line, then a SUMMARY line, and the game quits. Screenshots are
 * AutoLanes_<w>x<h>_<case>.png.
 */
class SOLTEST_API FSOLAutoLanesSmoke
{
public:

    // Caches the subsystems and actors, sets up the overrides, the view and the population
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // The cases, in order
    enum class ECase : uint8
    {
        Approach,
        Hold,
        Leg,
        MidStop,
        WarPower,
        Count
    };

    // Where the ship is relative to the lane this frame (ecliptic), and the traffic there
    struct FLaneSample
    {
        FSOLLanePoint Point;
        double TrafficMps = 0.0;        // On the keep-right track at the ship's arc, outbound
        double AlongMps = 0.0;          // The ship's along-lane speed relative to the lane frame
        double Delta = 0.0;             // (along - traffic) / traffic
    };

    // Returns the Earth - Mars lane where its bodies are now (ecliptic)
    FSOLLaneMergeLane LaneNow() const;

    // Returns the ship's lane sample this frame
    FLaneSample SampleShip() const;

    // Places the ship at a leg fraction, right and up offsets (wall radii), moving along the lane at a factor of the
    // track's traffic speed, nose along the lane, and orders the autopilot (or the War power move) to Mars
    void PlaceAndEngage(double legFraction, double rightRho, double upRho, double speedFactor, bool bWarPower);

    // Starts a case: placement, order and warp
    void StartCase();

    // Runs one frame of the current case; returns true when it has finished (its checks logged)
    bool UpdateCase(double realDeltaS);

    // Sets the time warp to a factor index (0 = 1x)
    void SetWarpIndex(int32 index) const;

    // Finds the Earth - Mars lane and the two bodies; false without them
    bool FindLane();

    // Logs the SUMMARY line, restores the warp, overrides and space dust and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn);

    // Requests a screenshot with the HUD named AutoLanes_<width>x<height>_<name>.png
    void TakeShot(const TCHAR* name) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<ASOLNpcVisuals> mVisuals;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLNpcSubsystem> mNpcs;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLLaneTrafficSubsystem> mLaneTraffic;
    TWeakObjectPtr<USOLSimClockSubsystem> mSimClock;
    bool mIsWaiting = true;                 // Waiting for the population and the lane table
    ECase mCase = ECase::Approach;
    bool mIsCaseStarted = false;
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    double mCaseSimStartS = 0.0;            // Sim seconds since J2000 when the case started
    int32 mCaseFrames = 0;
    int32 mLane = INDEX_NONE;               // The Earth - Mars lane
    int32 mEarth = INDEX_NONE;
    int32 mMars = INDEX_NONE;

    // Per-case observations
    ESOLLaneAutoMode mLastMode = ESOLLaneAutoMode::None;
    bool mHasSeenApproach = false;
    bool mHasSeenHold = false;
    bool mHasSeenOtherMode = false;         // A mode other than the expected one
    FLaneSample mHoldSample;                // At the first hold frame
    double mMaxHoldDelta = 0.0;             // Largest |delta| while holding (after the first hold frames)
    int32 mHoldFrames = 0;
    int32 mSettledFrames = 0;               // Frames in a row on the track at the traffic speed (hold case)
    double mLeftArcFraction = -1.0;         // Leg fraction where the hold ended (left)
    bool mHasArrived = false;
    double mHoldEtaS = 0.0;                 // The hold case's ETA
    double mPlainEtaS = 0.0;                // Plain AUTO's straight ETA from the War power case's start
    double mLeftRemainingM = -1.0;          // Mid-lane stop: ship to waypoint along travel when it left the lane
    double mLeftStopM = 0.0;                // ...and the braking distance of its speed relative to the waypoint then
    double mLeftSpeedMps = 0.0;
    double mMaxOvershootM = 0.0;            // Furthest past the waypoint along travel after leaving
    bool mHasReentered = false;             // A lane step after Left
    FVector3d mWaypointOffsetM = FVector3d::ZeroVector;    // Mid-lane stop waypoint offset from Earth (Unreal axes)
    double mStraightEtaS = 0.0;             // The straight no-lane ETA estimate at the hold case's start
    double mMaxGLoadG = 0.0;
    double mMinMeter = 1.0;
    bool mHasBlackedOut = false;
    bool mHasMarkerLane = false;            // The marker showed the LANE tag during the case
    bool mIsShotTaken = false;
    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
