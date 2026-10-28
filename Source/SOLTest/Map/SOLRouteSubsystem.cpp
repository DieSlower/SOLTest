/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRouteSubsystem.h"

#include "Game/SOLGameMode.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
    // sol.Route <Status|Resume|Clear>: verification hook for the player's route (SDD 20)
    FAutoConsoleCommandWithWorldAndArgs GRouteCommand(TEXT("sol.Route"),
        TEXT("sol.Route <Status|Resume|Clear>: log, resume or clear the player's Move / Patrol route"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            USOLRouteSubsystem* routes = world != nullptr ? world->GetSubsystem<USOLRouteSubsystem>() : nullptr;
            if (routes == nullptr)
            {
                UE_LOG(LogSOL, Warning, TEXT("sol.Route: no route subsystem in this world"));
                return;
            }
            const FString verb = args.Num() > 0 ? args[0] : FString(TEXT("Status"));
            if (verb.Equals(TEXT("Resume"), ESearchCase::IgnoreCase))
            {
                routes->Resume();
            }
            else if (verb.Equals(TEXT("Clear"), ESearchCase::IgnoreCase))
            {
                routes->ClearRoute();
            }
            UE_LOG(LogSOL, Log, TEXT("sol.Route: state %d, kind %d, %d waypoints, target %d, legs %d, leg ETA %.1f s, "
                "route ETA %.1f s, progress %.3f, pass %.1f m/s, capture %.0f m"),
                static_cast<int32>(routes->GetState()), static_cast<int32>(routes->GetKind()),
                routes->GetWaypoints().Num(), routes->GetCursor().TargetIndex, routes->GetCursor().LegsCompleted,
                routes->GetLegEtaS(), routes->GetRouteEtaS(), routes->GetProgress(),
                routes->GetCurrentPlan().PassSpeedMps, routes->GetCurrentPlan().CaptureRadiusM);
        }));
}

//////////////////////////////////////////////////////////////////////////
// Resolves the ship and registry subsystems and listens to the ship step
void USOLRouteSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    Ships = collection.InitializeDependency<USOLShipSubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Super::Initialize(collection);
    if (Ships != nullptr)
    {
        mShipsSteppedHandle = Ships->OnShipsStepped().AddUObject(this, &USOLRouteSubsystem::HandleShipsStepped);
    }
}

//////////////////////////////////////////////////////////////////////////
// Stops listening and drops the route
void USOLRouteSubsystem::Deinitialize()
{
    if (Ships != nullptr)
    {
        Ships->OnShipsStepped().Remove(mShipsSteppedHandle);
    }
    mShipsSteppedHandle.Reset();
    mWaypoints.Empty();
    mState = ESOLRouteState::None;
    Ships = nullptr;
    BodyRegistry = nullptr;
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLRouteSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLRouteSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Starts a route (replacing the active one) and orders the autopilot to its first waypoint
bool USOLRouteSubsystem::StartRoute(const ESOLRouteKind kind, const TConstArrayView<FSOLRouteWaypoint> waypoints)
{
    if (waypoints.IsEmpty() || Ships == nullptr || !Ships->HasPlayerShip() || !Ships->PilotHasControl()
        || BodyRegistry == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("Route: not started (no waypoints, no player ship or the pilot has no control)"));
        return false;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    for (const FSOLRouteWaypoint& waypoint : waypoints)
    {
        if (waypoint.ReferenceBodyIndex < 0 || waypoint.ReferenceBodyIndex >= registry.Num())
        {
            UE_LOG(LogSOL, Warning, TEXT("Route: not started (waypoint body %d does not resolve)"),
                waypoint.ReferenceBodyIndex);
            return false;
        }
    }

    // A new route replaces the active one; the old one's order is simply overwritten by the new first order. A
    // patrol needs two waypoints to repeat, so a one-waypoint route of any kind is a Move
    mWaypoints = waypoints;
    mKind = mWaypoints.Num() < 2 ? ESOLRouteKind::Move : kind;

    // A waypoint inside (or just above) its body could never be reached: it is pushed out along its offset to the
    // autopilot's geometric standoff (straight up when the offset is zero)
    for (FSOLRouteWaypoint& waypoint : mWaypoints)
    {
        const double minimumM = SOLFlight::AutopilotStandoffM(registry.GetRadiusM(waypoint.ReferenceBodyIndex), 0.0,
            0.0);
        if (waypoint.OffsetM.Size() < minimumM)
        {
            waypoint.OffsetM = waypoint.OffsetM.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitZ()) * minimumM;
        }
    }
    mCursor = SOLRoute::MakeStartCursor(mWaypoints.Num());
    mProgress = 0.0;
    mState = ESOLRouteState::Active;
    BuildTailEtas();
    if (!IssueCurrentOrder())
    {
        Pause(TEXT("the first order failed"));
        return false;
    }
    UE_LOG(LogSOL, Log, TEXT("Route: %s started with %d waypoints"), mKind == ESOLRouteKind::Move ? TEXT("Move")
        : mKind == ESOLRouteKind::PatrolLoop ? TEXT("Patrol loop") : TEXT("Patrol ping-pong"), mWaypoints.Num());
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Resumes a paused route: re-plans the current waypoint from the ship's state and engages the autopilot
bool USOLRouteSubsystem::Resume()
{
    if (mState != ESOLRouteState::Paused || Ships == nullptr || !Ships->PilotHasControl())
    {
        UE_LOG(LogSOL, Log, TEXT("Route: resume ignored (nothing paused, or the pilot has no control)"));
        return false;
    }
    mState = ESOLRouteState::Active;
    if (!IssueCurrentOrder())
    {
        Pause(TEXT("the resumed order failed"));
        return false;
    }
    UE_LOG(LogSOL, Log, TEXT("Route: resumed toward waypoint %d"), mCursor.TargetIndex);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Clears the route; disengages the autopilot if it was flying it
bool USOLRouteSubsystem::ClearRoute()
{
    if (mState == ESOLRouteState::None)
    {
        return false;
    }
    if (mState == ESOLRouteState::Active && Ships != nullptr && Ships->IsAutopilotPointOrder())
    {
        Ships->DisengageAutopilot();
    }
    mWaypoints.Reset();
    mTailSuffixEtaS.Reset();
    mCursor = FSOLRouteCursor();
    mPlan = FSOLCornerPlan();
    mLegEtaS = 0.0;
    mTailEtaS = 0.0;
    mProgress = 0.0;
    mState = ESOLRouteState::None;
    UE_LOG(LogSOL, Log, TEXT("Route: cleared"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Flips the route overlay's visibility; returns the new value
bool USOLRouteSubsystem::ToggleOverlay()
{
    mIsOverlayVisible = !mIsOverlayVisible;
    UE_LOG(LogSOL, Log, TEXT("Route: overlay %s"), mIsOverlayVisible ? TEXT("shown") : TEXT("hidden"));
    return mIsOverlayVisible;
}

//////////////////////////////////////////////////////////////////////////
// Returns a waypoint's live universe position (ecliptic meters) from its body's current position
bool USOLRouteSubsystem::GetWaypointPositionM(const int32 waypointIndex, FVector3d& outPositionM) const
{
    if (!mWaypoints.IsValidIndex(waypointIndex) || BodyRegistry == nullptr)
    {
        return false;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const FSOLRouteWaypoint& waypoint = mWaypoints[waypointIndex];
    if (waypoint.ReferenceBodyIndex < 0 || waypoint.ReferenceBodyIndex >= registry.Num())
    {
        return false;
    }
    outPositionM = SOLRoute::ResolveWaypointM(waypoint, registry.GetPositionM(waypoint.ReferenceBodyIndex));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Follows the ship's step: advances on an arrival, pauses when the autopilot no longer flies the route
void USOLRouteSubsystem::HandleShipsStepped(const float /*realDeltaSeconds*/)
{
    if (mState != ESOLRouteState::Active || Ships == nullptr)
    {
        return;
    }

    // An arrival (the count moved since this leg's order) advances the sequence: the next waypoint is ordered now, or
    // a Move that reached its last waypoint is finished (the ship already stopped and dropped to assisted flight)
    // Only an arrival of this route's own current order counts (another order's arrival falls to the pause check)
    const uint32 arrivals = Ships->GetAutopilotArrivalCount();
    if (arrivals != mLastArrivalCount && Ships->GetLastArrivalOrderSerial() == mOrderSerial)
    {
        mLastArrivalCount = arrivals;
        if (!SOLRoute::Advance(mKind, mWaypoints.Num(), mCursor))
        {
            mState = ESOLRouteState::Finished;
            mLegEtaS = 0.0;
            mTailEtaS = 0.0;
            mProgress = 1.0;
            UE_LOG(LogSOL, Log, TEXT("Route: finished after %d legs"), mCursor.LegsCompleted);
            return;
        }
        if (!IssueCurrentOrder())
        {
            Pause(TEXT("the next order failed"));
        }
        return;
    }

    // Manual input, C, a jump's pilot reset or another autopilot order ended this route's order: keep it, paused
    if (Ships->GetFlightMode() != ESOLFlightMode::Auto || !Ships->IsAutopilotPointOrder())
    {
        Pause(TEXT("the autopilot no longer flies it"));
        return;
    }

    // Remaining time of the current leg at the planned pass speed, and the Move's progress
    double cornerAccelMps2 = 0.0;
    double planAccelMps2 = 0.0;
    double cruiseCapMps = 0.0;
    GetFlightBudgets(cornerAccelMps2, planAccelMps2, cruiseCapMps);
    const FSOLGuidanceOutput guidance = Ships->GetAutopilotGuidance();
    const double legEtaS = SOLRoutePlanner::LegTimeS(guidance.DistanceM, guidance.ClosingSpeedMps,
        mPlan.PassSpeedMps, planAccelMps2, cruiseCapMps);
    mLegEtaS = legEtaS < TNumericLimits<double>::Max() ? legEtaS : 0.0;
    mProgress = SOLRoutePlanner::RouteProgress(mCursor.LegsCompleted, SOLRoute::TotalLegs(mKind, mWaypoints.Num()),
        SOLRoutePlanner::LegFraction(guidance.DistanceM, mCurrentLegLengthM));
}

//////////////////////////////////////////////////////////////////////////
// Plans the current waypoint from the ship's position and orders the autopilot to it
bool USOLRouteSubsystem::IssueCurrentOrder()
{
    FVector3d targetM;
    if (Ships == nullptr || !GetWaypointPositionM(mCursor.TargetIndex, targetM))
    {
        return false;
    }
    double cornerAccelMps2 = 0.0;
    double planAccelMps2 = 0.0;
    double cruiseCapMps = 0.0;
    GetFlightBudgets(cornerAccelMps2, planAccelMps2, cruiseCapMps);

    // The leg starts where the ship is now; only this waypoint's pass is planned (the next one when it is reached)
    const FVector3d shipM = Ships->GetUniversePositionM();
    mPlan = PlanWaypoint(mCursor, shipM, cornerAccelMps2, cruiseCapMps);
    mCurrentLegLengthM = FVector3d::Dist(shipM, targetM);

    // The ship flies in the Unreal-handed frame (Y flipped); the waypoint offset converts the same way
    const FSOLRouteWaypoint& waypoint = mWaypoints[mCursor.TargetIndex];
    FSOLAutopilotPointOrder order;
    order.BodyIndex = waypoint.ReferenceBodyIndex;
    order.OffsetM = SOLRender::EclipticToUnreal(waypoint.OffsetM);
    order.PassSpeedMps = mPlan.bStop ? 0.0 : mPlan.PassSpeedMps;
    order.CaptureRadiusM = mPlan.bStop ? 0.0 : mPlan.CaptureRadiusM;
    order.bStayAutoOnArrival = !SOLRoute::IsStopWaypoint(mKind, mWaypoints.Num(), mCursor);
    mLastArrivalCount = Ships->GetAutopilotArrivalCount();
    if (!Ships->EngageAutopilotPoint(order))
    {
        return false;
    }
    mOrderSerial = Ships->GetAutopilotOrderSerial();
    mLegEtaS = SOLRoutePlanner::LegTimeS(mCurrentLegLengthM, 0.0, order.PassSpeedMps, planAccelMps2, cruiseCapMps);
    mLegEtaS = mLegEtaS < TNumericLimits<double>::Max() ? mLegEtaS : 0.0;
    mTailEtaS = mTailSuffixEtaS.IsValidIndex(mCursor.TargetIndex) ? mTailSuffixEtaS[mCursor.TargetIndex] : 0.0;
    UE_LOG(LogSOL, Log, TEXT("Route: leg to waypoint %d (%.0f km, %s %.0f m/s, capture %.0f m)"), mCursor.TargetIndex,
        mCurrentLegLengthM / SOL::METERS_PER_KM, mPlan.bStop ? TEXT("stop") : TEXT("pass at"), order.PassSpeedMps,
        order.CaptureRadiusM);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Builds a Move's per-waypoint remaining time of the later legs, once per start (O(waypoints))
void USOLRouteSubsystem::BuildTailEtas()
{
    mTailSuffixEtaS.Reset();
    mTailEtaS = 0.0;
    if (SOLRoute::IsPatrol(mKind) || mWaypoints.Num() < 2)
    {
        return;
    }
    double cornerAccelMps2 = 0.0;
    double planAccelMps2 = 0.0;
    double cruiseCapMps = 0.0;
    GetFlightBudgets(cornerAccelMps2, planAccelMps2, cruiseCapMps);

    // One pass over the legs between waypoints (k - 1 -> k), each from the previous waypoint's pass speed to its own,
    // then suffix sums: entry k is the time of every leg after waypoint k. Positions as of now (the bodies move on, so
    // this is an estimate); the first leg starts at the first waypoint, so its own pass plan comes from there
    const int32 count = mWaypoints.Num();
    TArray<double> legTimesS;
    legTimesS.SetNumZeroed(count);
    FSOLRouteCursor cursor = SOLRoute::MakeStartCursor(count);
    FVector3d fromM;
    if (!GetWaypointPositionM(0, fromM))
    {
        return;
    }
    double entrySpeedMps = 0.0;
    while (SOLRoute::Advance(mKind, count, cursor))
    {
        FVector3d toM;
        if (!GetWaypointPositionM(cursor.TargetIndex, toM))
        {
            return;
        }
        const FSOLCornerPlan plan = PlanWaypoint(cursor, fromM, cornerAccelMps2, cruiseCapMps);
        const double exitSpeedMps = plan.bStop ? 0.0 : plan.PassSpeedMps;
        const double legS = SOLRoutePlanner::LegTimeS(FVector3d::Dist(fromM, toM), entrySpeedMps, exitSpeedMps,
            planAccelMps2, cruiseCapMps);
        legTimesS[cursor.TargetIndex] = legS < TNumericLimits<double>::Max() ? legS : 0.0;
        entrySpeedMps = exitSpeedMps;
        fromM = toM;
    }
    mTailSuffixEtaS.SetNumZeroed(count);
    for (int32 index = count - 2; index >= 0; --index)
    {
        mTailSuffixEtaS[index] = mTailSuffixEtaS[index + 1] + legTimesS[index + 1];
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the corner G budget (m/s^2), the autopilot's planning acceleration and its cruise cap
void USOLRouteSubsystem::GetFlightBudgets(double& outCornerAccelMps2, double& outPlanAccelMps2,
    double& outCruiseCapMps) const
{
    // The player is a human pilot: corners at the sustained G limit (and the hull's cap, the pilot's max G when the
    // envelope has none); legs at the autopilot's own plan acceleration
    const FSOLFlightParams params = Ships != nullptr ? Ships->GetFlightParams() : FSOLFlightParams();
    const double hullCapMps2 = params.Envelope.MaxTotalAccelMps2;
    const double hullCapG = FMath::IsFinite(hullCapMps2) && hullCapMps2 > 0.0
        ? hullCapMps2 / SOLNewtonian::STANDARD_GRAVITY_MPS2 : params.PilotLimits.MaxG;
    outCornerAccelMps2 = SOLRoutePlanner::CornerAccelBudgetMps2(ESOLPilotKind::Human, params.PilotLimits.SustainedG,
        hullCapG);
    const FSOLGMeterState meter = Ships != nullptr ? Ships->GetGMeter() : FSOLGMeterState();
    const FSOLGuidanceParams guidance = SOLNewtonian::MakeGuidanceParams(params.Envelope,
        SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, meter), 0.0, true, false);
    outPlanAccelMps2 = guidance.PlanAccelMps2;
    outCruiseCapMps = guidance.MaxCruiseSpeedMps;
}

//////////////////////////////////////////////////////////////////////////
// Plans how the cursor's target waypoint is passed when reached from fromM; stop when no waypoint follows it
FSOLCornerPlan USOLRouteSubsystem::PlanWaypoint(const FSOLRouteCursor& cursor, const FVector3d& fromM,
    const double cornerAccelMps2, const double cruiseCapMps) const
{
    const int32 nextIndex = SOLRoute::PeekNextIndex(mKind, mWaypoints.Num(), cursor);
    FVector3d cornerM;
    FVector3d nextM;
    if (nextIndex == INDEX_NONE || !GetWaypointPositionM(cursor.TargetIndex, cornerM)
        || !GetWaypointPositionM(nextIndex, nextM))
    {
        return SOLRoutePlanner::MakeStopPlan();
    }
    return SOLRoutePlanner::PlanCorner(SOLRoutePlanner::TurnAngleRad(fromM, cornerM, nextM),
        FVector3d::Dist(fromM, cornerM), FVector3d::Dist(cornerM, nextM), cornerAccelMps2, cruiseCapMps,
        FSOLRoutePlannerParams());
}

//////////////////////////////////////////////////////////////////////////
// Switches to Paused (logged with the reason)
void USOLRouteSubsystem::Pause(const TCHAR* reason)
{
    mState = ESOLRouteState::Paused;
    mLegEtaS = 0.0;
    mTailEtaS = 0.0;
    ++mPauseCount;
    UE_LOG(LogSOL, Log, TEXT("Route: paused at waypoint %d (%s)"), mCursor.TargetIndex, reason);
}
