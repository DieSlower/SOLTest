/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLRoute.h"
#include "Map/SOLRoutePlanner.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLRouteSubsystem.generated.h"

class USOLBodyRegistrySubsystem;
class USOLShipSubsystem;

// Where the player's active route is
enum class ESOLRouteState : uint8
{
    None,       // No route
    Active,     // The autopilot flies it
    Paused,     // Manual input (or a jump, C, another autopilot order) took over; Resume re-plans from here
    Finished,   // A Move reached its last waypoint (kept, dimmed, until cleared or replaced)
};

/**
 * The player ship's active Move / Patrol route (SDD 20 sections 2.2 and 2.3). Owns the waypoints (each follows its
 * reference body), the sequencing cursor (SOLRoute) and the current waypoint's corner plan (SOLRoutePlanner), and
 * drives USOLShipSubsystem's autopilot through them with point orders: intermediate waypoints are flown through at the
 * planned pass speed (AUTO stays on), the last waypoint of a Move is a full stop that hands back to assisted flight.
 * Only the current leg is planned (lazily, when it starts); the Move's remaining-time estimate of the later legs is
 * built once per start (suffix sums, O(waypoints)) and then only looked up. An arrival advances the route only when it
 * belongs to the route's own current order (the ship's order serial).
 *
 * Frame order: runs from the ship subsystem's OnShipsStepped (after the flight run and its arrival handling, before the
 * anchor update), so an arrival is answered with the next order in the same frame. Any frame the ship is not in AUTO on
 * one of this route's orders (manual input cancelled it, C, a jump's pilot reset, another autopilot order) pauses the
 * route; Resume re-plans the current waypoint from where the ship is now. The route never changes time warp.
 */
UCLASS()
class SOLTEST_API USOLRouteSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Resolves the ship and registry subsystems and listens to the ship step
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Stops listening and drops the route
    virtual void Deinitialize() override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Starts a route (replacing the active one) and orders the autopilot to its first waypoint; false (nothing
    // changed) without waypoints, with a waypoint whose body does not resolve, without a player ship or while the pilot
    // has no control. A one-waypoint route is a Move; a waypoint closer to its body than the autopilot's geometric
    // standoff is pushed out to it. If the first order still fails, the new route is kept, Paused, and false returned
    bool StartRoute(ESOLRouteKind kind, TConstArrayView<FSOLRouteWaypoint> waypoints);

    // Resumes a paused route: re-plans the current waypoint from the ship's state and engages the autopilot; false
    // when there is nothing paused or the order fails
    bool Resume();

    // Clears the route (Delete); disengages the autopilot if it was flying it. Returns whether there was one
    bool ClearRoute();

    // Flips the route overlay's visibility (P in flight); returns the new value
    bool ToggleOverlay();

    // Returns whether the route overlay is shown (default on)
    bool IsOverlayVisible() const { return mIsOverlayVisible; }

    // Returns the route's state
    ESOLRouteState GetState() const { return mState; }

    // Returns the route's kind (meaningful unless None)
    ESOLRouteKind GetKind() const { return mKind; }

    // Returns the route's waypoints (empty with no route)
    TConstArrayView<FSOLRouteWaypoint> GetWaypoints() const { return mWaypoints; }

    // Returns the sequencing cursor (target waypoint, ping-pong direction, legs completed)
    const FSOLRouteCursor& GetCursor() const { return mCursor; }

    // Returns how the current waypoint is passed (stop or fly-through speed and capture radius)
    const FSOLCornerPlan& GetCurrentPlan() const { return mPlan; }

    // Returns a waypoint's live universe position (ecliptic meters) from its body's current position; false when the
    // index or its body does not resolve
    bool GetWaypointPositionM(int32 waypointIndex, FVector3d& outPositionM) const;

    // Returns the estimated seconds to reach the current waypoint (as of the last frame; 0 unless Active)
    double GetLegEtaS() const { return mLegEtaS; }

    // Returns the estimated seconds to the end of a Move (current leg plus the later legs); the leg ETA for a patrol
    double GetRouteEtaS() const { return mLegEtaS + mTailEtaS; }

    // Returns a Move's progress in [0, 1] (1 when finished); 0 for a patrol
    double GetProgress() const { return mProgress; }

    // Returns how many times a route was paused so far (verification)
    int32 GetPauseCount() const { return mPauseCount; }

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Follows the ship's step: advances on an arrival, pauses when the autopilot no longer flies the route, and
    // updates the ETA and progress
    void HandleShipsStepped(float realDeltaSeconds);

    // Plans the current waypoint from the ship's position and orders the autopilot to it; false if the order fails
    bool IssueCurrentOrder();

    // Builds a Move's per-waypoint remaining time of the later legs, once per start (O(waypoints))
    void BuildTailEtas();

    // Returns the corner G budget (m/s^2), the autopilot's planning acceleration (gravity not included, so the ETAs
    // are optimistic) and its cruise cap
    void GetFlightBudgets(double& outCornerAccelMps2, double& outPlanAccelMps2, double& outCruiseCapMps) const;

    // Plans how waypoint index is passed when reached from fromM; stop when no waypoint follows it
    FSOLCornerPlan PlanWaypoint(const FSOLRouteCursor& cursor, const FVector3d& fromM, double cornerAccelMps2,
        double cruiseCapMps) const;

    // Switches to Paused (logged with the reason)
    void Pause(const TCHAR* reason);

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    TArray<FSOLRouteWaypoint> mWaypoints;                   // Active route (unlimited)
    FSOLRouteCursor mCursor;                                // Sequencing state
    FSOLCornerPlan mPlan;                                   // Current waypoint's pass plan
    FDelegateHandle mShipsSteppedHandle;                    // Binding to USOLShipSubsystem::OnShipsStepped
    double mCurrentLegLengthM = 0.0;                        // Distance to the waypoint when the leg was ordered
    double mLegEtaS = 0.0;                                  // Current leg's remaining time estimate
    TArray<double> mTailSuffixEtaS;                         // Move: time of every leg after waypoint k (per start)
    double mTailEtaS = 0.0;                                 // Later legs of the current waypoint (from the above)
    double mProgress = 0.0;                                 // Move progress [0, 1]
    uint32 mLastArrivalCount = 0;                           // Ship's arrival count when the current order went out
    uint32 mOrderSerial = 0;                                // Ship's order serial of the current order
    int32 mPauseCount = 0;                                  // Pauses so far (verification)
    ESOLRouteKind mKind = ESOLRouteKind::Move;
    ESOLRouteState mState = ESOLRouteState::None;
    bool mIsOverlayVisible = true;                          // P toggles the route overlay (SDD 20 section 2.1)
};
