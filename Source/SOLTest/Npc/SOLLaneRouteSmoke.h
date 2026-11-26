/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Traffic/SOLLaneRoutePlan.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "CoreMinimal.h"

class ASOLFlightHud;
class ASOLNpcVisuals;
class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLInteractionSubsystem;
class USOLLaneTrafficSubsystem;
class USOLNpcSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;

/**
 * Verification-only AUTO lane routing and Sun safety script (-SOLSmokeLaneRoute, SDD 53 6.4 and 6.5, plan 53e step 7).
 * It requests an NPC population (the lane table), shows the lanes through their verification overrides and turns the
 * orbit lines and the space dust off in memory, so no player setting is read or written. Cases, in order: the radial
 * menu of a body (11 sectors) toggling the Sun safety override through its row both ways (SUN SAFETY OFF without an
 * order); a 3+ hop plan from Earth at 1x (VIA, only the hop's lane latched, the map polyline, a manual takeover that
 * clears both), at 86400x and at the top warp (hop changes, re-plans and epochs logged, no other lane mid-plan,
 * arrival); a held lane closing for the Sun under the ship (left with the LANE CLOSING notice, never inside 20 Sun
 * radii); a Move To across the Sun with the safety on (detour) and with the override (straight, inside the zone, SUN
 * SAFETY OFF); a War power move across; a Chase Down near the Sun; a Move To the Sun (stops at 22 radii); a fast Sunward
 * entry then an AUTO order; the override flipped on and off mid-detour. The Sun distance is the processor's substep
 * swept closest approach (GetAutopilotSunMinDistanceM), not frame-end samples. Each check logs a LogSOL CHECK PASS /
 * FAIL line, then a SUMMARY line, and the game quits. Screenshots are LaneRoute_<w>x<h>_<name>.png.
 */
class SOLTEST_API FSOLLaneRouteSmoke
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
        Menus,
        Plan1x,
        PlanDay,
        PlanTop,
        HeldClosing,
        SunMove,
        SunOverride,
        SunWar,
        SunChase,
        SunTarget,
        FastEntry,
        SunFlip,
        Count
    };

    // Runs one frame of the current case; returns true when it has finished (its checks logged)
    bool UpdateCase(ASOLShipPawn& pawn);

    // The radial menu case: 11 sectors, the override row toggled on and off through the menu
    bool UpdateMenus(ASOLShipPawn& pawn);

    // A plan case at a warp index: probe the targets for a 3+ hop plan, fly it, check it
    bool UpdatePlan(ASOLShipPawn& pawn, int32 warpIndex);

    // The held lane closing for the Sun case
    bool UpdateHeldClosing();

    // The Sun cases (detour, override, War power, chase, the Sun as target, fast entry, flips)
    bool UpdateSun();

    // Observes this frame's Sun distance, detour, notices, plan, lane and epoch (every running case)
    void Observe();

    // Drops the warp to 3600x once the order's last leg is near its target (the 10 m / 1 m/s arrival is not reached
    // at 86400x and above; a player slows down there too)
    void SlowNearTarget(int32 targetBody);

    // Places the ship at rest near Earth (inside the plan's at-node radius), nose away from the Sun
    void PlaceNearEarth();

    // Places the ship across the Sun from a body (Sun between them, the straight path offset by Sun radii), at rest
    void PlaceAcrossSun(int32 body, double shipSunDistanceM, double offsetSunRadii);

    // Places the ship (Unreal-handed universe metres) with a velocity and a nose direction, no order
    void Place(const FVector3d& positionM, const FVector3d& velocityMps, const FVector3d& noseDirection);

    // Sets the Sun safety override to a state (through the subsystem's toggle)
    void SetOverride(bool bOverridden) const;

    // Returns a body's position in the Unreal-handed universe frame
    FVector3d BodyM(int32 body) const;

    // Returns a body's velocity in the Unreal-handed universe frame
    FVector3d BodyMps(int32 body) const;

    // Picks the open lane whose window closes soonest for the Sun; false if none in the schedule's horizon
    bool FindSunClosingLane();

    // Sets the time warp to a factor index (0 = 1x)
    void SetWarpIndex(int32 index) const;

    // Logs the SUMMARY line, restores the warp, overrides and space dust and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn);

    // Requests a screenshot with the HUD named LaneRoute_<width>x<height>_<name>.png
    void TakeShot(const TCHAR* name) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    // Resets the per-case observations
    void ResetObservations();

    TWeakObjectPtr<ASOLNpcVisuals> mVisuals;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLNpcSubsystem> mNpcs;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLLaneTrafficSubsystem> mLaneTraffic;
    TWeakObjectPtr<USOLSimClockSubsystem> mSimClock;
    TWeakObjectPtr<USOLInteractionSubsystem> mInteraction;
    bool mIsWaiting = true;                 // Waiting for the population, the lane table and the network
    ECase mCase = ECase::Menus;
    bool mIsCaseStarted = false;
    int32 mStage = 0;                       // The current case's stage
    double mStageRealS = 0.0;               // Real seconds in the stage
    int32 mStageFrames = 0;                 // Frames in the stage
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    double mCaseRealS = 0.0;
    double mCaseSimStartS = 0.0;            // Sim seconds since J2000 when the case's order was given
    int32 mOrderFrames = 0;                 // Frames since the case's order was given
    int32 mSun = INDEX_NONE;
    int32 mMercury = INDEX_NONE;
    int32 mVenus = INDEX_NONE;
    int32 mEarth = INDEX_NONE;
    int32 mMars = INDEX_NONE;

    // The plan cases' probe: the candidate index, the best target so far
    int32 mProbe = 0;
    int32 mPlanTarget = INDEX_NONE;
    int32 mPlanHops = 0;
    double mPlanTimeS = 0.0;

    // The held lane closing case's lane
    int32 mClosingEntry = INDEX_NONE;
    double mClosingCloseS = 0.0;

    // Per-case observations
    double mMinSunM = 0.0;                  // Closest approach with the safety on (substep swept; 0 = none seen)
    double mMinSunAnyM = 0.0;               // Closest approach over the whole order (override or not)
    bool mHasSeenDetour = false;
    bool mHasSeenDetourLine = false;
    bool mHasSeenSafetyOffLine = false;
    bool mHasSeenStoppedLine = false;
    bool mHasSeenLeftLine = false;
    bool mHasSeenLeftForSun = false;
    bool mHasSeenHold = false;
    bool mHasSeenLaneLatch = false;
    bool mHasArrived = false;
    int32 mWrongLaneFrames = 0;             // Frames a lane other than the current hop's was latched mid-plan
    int32 mHopChanges = 0;
    int32 mReplans = 0;
    int32 mHeldEpochChanges = 0;            // Lane epochs that changed while a plan hop was held
    int32 mMaxHopCount = 0;
    int32 mViaFrames = 0;
    int32 mLastViaBody = INDEX_NONE;
    int32 mLastLegA = INDEX_NONE;           // The last latched lane leg's end bodies (a change is a hop change)
    int32 mLastLegB = INDEX_NONE;
    int32 mLastHop = INDEX_NONE;
    int32 mHopFromBody = INDEX_NONE;        // The current plan hop's end bodies (a change ends the hop)
    int32 mHopToBody = INDEX_NONE;
    bool mHasHopHeld = false;               // The current plan hop held its lane
    bool mWasHopLeftEarly = false;          // The current plan hop was left early (last frame)
    bool mIsHopLast = false;                // The current plan hop is the plan's last (no later hop, last frame)
    int32 mHopsEndedAway = 0;               // Plan hops that ended (or dropped) away from their end node unheld
    int32 mEarlyPendingBody = INDEX_NONE;   // End body of a hop left early whose end node the ship has not yet reached
    int32 mEarlyHopsReached = 0;            // Hops left early whose end node (within the at-node radius) the ship then reached
    int32 mEarlyHopsUnreached = 0;          // Hops left early whose end node the ship never reached afterwards
    int32 mEarlyHopsEnded = 0;              // Hops left early that ended via FinishEarlyHop (the plan moved on or was re-made)
    int32 mEarlyProximitySkipped = 0;       // Left-early hops whose proximity sample was skipped (a frame too large)
    FVector3d mPrevShipPosM = FVector3d::ZeroVector;    // The ship's position on the previous sampled frame
    bool mHasPrevShipPos = false;
    uint32 mLastSerial = 0;
    int64 mLastEpoch = 0;
    ESOLLaneAutoMode mLastMode = ESOLLaneAutoMode::None;
    bool mLastDetour = false;
    int32 mFlipFrame = INDEX_NONE;          // Flip case: the frame the override was switched on, then off
    bool mHasFlipOnCleared = false;
    bool mHasFlipOffLatched = false;
    bool mIsShotTaken = false;
    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
