/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLRoute.h"

#include "CoreMinimal.h"

// Issue #20 map modes (SDD 20 section 2.1): the Jump / Move / Patrol mode state machine, the draft route edited in Move
// and Patrol mode, and the map's route-input rules. Pure logic: no UObjects, no world access.

// The map's mode
enum class ESOLMapMode : uint8
{
    Jump,       // Pick one destination; Enter jumps (SDD 3)
    Move,       // Each locked pick appends a waypoint; Enter starts a Move route
    Patrol,     // As Move; Enter starts a patrol (Loop or Ping-pong)
};

// What Enter does on the map
enum class ESOLMapEnterAction : uint8
{
    None,           // Nothing to do (Move / Patrol with an empty draft)
    Jump,           // Jump mode: jump to the pick (the jump itself still needs a locked pick)
    StartRoute,     // Move / Patrol with at least one draft waypoint
};

// The map's mode, patrol shape and draft route (kept while the map is closed)
struct FSOLMapModeState
{
    ESOLMapMode Mode = ESOLMapMode::Move;   // Current (and last used) mode
    bool bPatrolPingPong = false;           // Patrol shape: false = Loop, true = Ping-pong
    TArray<FSOLRouteWaypoint> Draft;        // Waypoints being planned, in order
};

namespace SOLMapModeState
{
    // Returns the mode the map opens in: Jump when opened with the jump key, else the last used mode
    SOLTEST_API ESOLMapMode ModeOnOpen(ESOLMapMode lastMode, bool bOpenedForJump);

    // Applies an in-map mode key (J = Jump, M = Move, P = Patrol). The key of the current mode does nothing, except P
    // in Patrol mode, which flips Loop / Ping-pong. Returns whether the state changed
    SOLTEST_API bool PressModeKey(FSOLMapModeState& state, ESOLMapMode key);

    // Returns the route kind Enter would start: Move in Move mode, the patrol shape in Patrol mode (Move in Jump mode)
    SOLTEST_API ESOLRouteKind RouteKindFor(const FSOLMapModeState& state);

    // Returns what Enter does in the current state
    SOLTEST_API ESOLMapEnterAction EnterAction(const FSOLMapModeState& state);

    // Appends a waypoint to the draft in Move or Patrol mode (unlimited); returns false (nothing added) in Jump mode
    SOLTEST_API bool AppendWaypoint(FSOLMapModeState& state, const FSOLRouteWaypoint& waypoint);

    // Sets the height (offset Z, meters) of the last draft waypoint; returns false with an empty draft or non-finite
    // height
    SOLTEST_API bool SetLastWaypointHeight(FSOLMapModeState& state, double heightM);

    // Removes the last draft waypoint; returns false with an empty draft
    SOLTEST_API bool UndoWaypoint(FSOLMapModeState& state);

    // Empties the draft; returns whether it had any waypoint
    SOLTEST_API bool ClearDraft(FSOLMapModeState& state);

    // Returns whether a right-button release is the undo click: Ctrl held, the cursor moved at most
    // SOL::Route::UNDO_CLICK_MAX_TRAVEL_PX and the button was held at most SOL::Route::UNDO_CLICK_MAX_S (both
    // inclusive; non-finite or negative values are not a click). Anything else is an orbit drag
    SOLTEST_API bool IsUndoClick(bool bControlDown, double travelPx, double heldS);
}
