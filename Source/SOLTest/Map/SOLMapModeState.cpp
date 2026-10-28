/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapModeState.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Returns the mode the map opens in: Jump for the jump key, else the last used mode
ESOLMapMode SOLMapModeState::ModeOnOpen(const ESOLMapMode lastMode, const bool bOpenedForJump)
{
    return bOpenedForJump ? ESOLMapMode::Jump : lastMode;
}

//////////////////////////////////////////////////////////////////////////
// Applies an in-map mode key; P in Patrol mode flips the patrol shape. Returns whether the state changed
bool SOLMapModeState::PressModeKey(FSOLMapModeState& state, const ESOLMapMode key)
{
    if (state.Mode != key)
    {
        state.Mode = key;
        return true;
    }
    if (key == ESOLMapMode::Patrol)
    {
        state.bPatrolPingPong = !state.bPatrolPingPong;
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the route kind Enter would start
ESOLRouteKind SOLMapModeState::RouteKindFor(const FSOLMapModeState& state)
{
    if (state.Mode != ESOLMapMode::Patrol)
    {
        return ESOLRouteKind::Move;
    }
    return state.bPatrolPingPong ? ESOLRouteKind::PatrolPingPong : ESOLRouteKind::PatrolLoop;
}

//////////////////////////////////////////////////////////////////////////
// Returns what Enter does in the current state
ESOLMapEnterAction SOLMapModeState::EnterAction(const FSOLMapModeState& state)
{
    if (state.Mode == ESOLMapMode::Jump)
    {
        return ESOLMapEnterAction::Jump;
    }
    return state.Draft.IsEmpty() ? ESOLMapEnterAction::None : ESOLMapEnterAction::StartRoute;
}

//////////////////////////////////////////////////////////////////////////
// Appends a waypoint to the draft in Move or Patrol mode
bool SOLMapModeState::AppendWaypoint(FSOLMapModeState& state, const FSOLRouteWaypoint& waypoint)
{
    if (state.Mode == ESOLMapMode::Jump)
    {
        return false;
    }
    state.Draft.Add(waypoint);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the height of the last draft waypoint
bool SOLMapModeState::SetLastWaypointHeight(FSOLMapModeState& state, const double heightM)
{
    if (state.Draft.IsEmpty() || !FMath::IsFinite(heightM))
    {
        return false;
    }
    state.Draft.Last().OffsetM.Z = heightM;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Removes the last draft waypoint
bool SOLMapModeState::UndoWaypoint(FSOLMapModeState& state)
{
    if (state.Draft.IsEmpty())
    {
        return false;
    }
    state.Draft.Pop(EAllowShrinking::No);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Empties the draft; returns whether it had any waypoint
bool SOLMapModeState::ClearDraft(FSOLMapModeState& state)
{
    const bool bHadAny = !state.Draft.IsEmpty();
    state.Draft.Reset();
    return bHadAny;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a right-button release is the undo click (Ctrl held, short and still)
bool SOLMapModeState::IsUndoClick(const bool bControlDown, const double travelPx, const double heldS)
{
    return bControlDown && FMath::IsFinite(travelPx) && FMath::IsFinite(heldS) && travelPx >= 0.0 && heldS >= 0.0
        && travelPx <= SOL::Route::UNDO_CLICK_MAX_TRAVEL_PX && heldS <= SOL::Route::UNDO_CLICK_MAX_S;
}
