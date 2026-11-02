/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"
#include "UI/SOLOverlayIcons.h"

#include "CoreMinimal.h"

// What an overlay icon or a map body stands for: a registry body, a named asteroid (index into the icon layer's
// named-asteroid list), a dropped target (combat slot) or an NPC ship (population index)
enum class ESOLObjectKind : uint8
{
    None,
    Body,
    Asteroid,
    DroppedTarget,
    Ship,
};

// A reference to one selectable object (SDD 22 section 2.4); default = no object
struct FSOLObjectRef
{
    ESOLObjectKind Kind = ESOLObjectKind::None;
    int32 Index = INDEX_NONE;

    // Returns true for a kind other than None with a non-negative index
    bool IsValid() const { return Kind != ESOLObjectKind::None && Index >= 0; }

    // Two references are equal when kind and index match
    bool operator==(const FSOLObjectRef& other) const { return Kind == other.Kind && Index == other.Index; }
    bool operator!=(const FSOLObjectRef& other) const { return !(*this == other); }
};

// The object types the action table knows (one row set each); fleets, stations and NPC orders add types later
enum class ESOLObjectType : uint8
{
    Star,
    Planet,
    Moon,
    Dwarf,
    Asteroid,
    Ship,
    DroppedTarget,
    Count,
};

// The actions of the interaction menus, v1 (SDD 22 section 2.4)
enum class ESOLAction : uint8
{
    Target,         // Select it as the target
    Info,           // Show its info card
    MoveTo,         // Autopilot: fly to it and stop
    Follow,         // Autopilot: station keeping at a radius around it (Follow / Orbit)
    JumpTo,         // Jump next to it
    MatchSpeed,     // Lock the reference frame to it (as M)
    Attack,         // Select it and show the fire hint
    ChaseDown,      // Issue #32: greyed until it lands
    Communicate,    // Placeholder
    Trade,          // Placeholder
    Count,
};

// Why an action is unavailable (None = available)
enum class ESOLActionBlock : uint8
{
    None,
    NotYetAvailable,    // A placeholder row (Chase Down until #32, Communications, Trade, ...)
    NotOffered,         // The object type has no such row
    NoShip,             // No player ship
    NoPilotControl,     // The pilot is blacked out (G-LOC)
    JumpWarping,        // A jump warp runs
    NotTargetable,      // The object cannot be a target (yet)
    AlreadyTargeted,    // It is the selected target already
    AlreadyMatched,     // The reference frame is locked to it already
    AlreadyMovingTo,    // The autopilot flies to it already
    AlreadyFollowing,   // The autopilot keeps station on it already
    Count,
};

// The facts an action's availability depends on, gathered by the caller for one object
struct FSOLActionContext
{
    bool bHasShip = false;          // A player ship exists
    bool bPilotHasControl = false;  // The pilot is conscious
    bool bJumpWarping = false;      // A jump warp runs
    bool bIsTargetable = false;     // The object can be a targeting candidate
    bool bIsSelected = false;       // The object is the selected target
    bool bIsFrameLocked = false;    // The reference frame is locked to the object
    bool bIsMoveTarget = false;     // The autopilot flies a Move To order to the object
    bool bIsFollowed = false;       // The autopilot keeps station on the object
};

// One action of a menu and why it is unavailable (Block None = available)
struct FSOLActionEntry
{
    ESOLAction Action = ESOLAction::Count;
    ESOLActionBlock Block = ESOLActionBlock::NotOffered;

    // Returns true when the action can be chosen
    bool IsEnabled() const { return Block == ESOLActionBlock::None; }
};

// A fixed-capacity action list (no allocation): Entries[0, Num)
struct FSOLActionList
{
    FSOLActionEntry Entries[SOL::Interact::MAX_ACTIONS];
    int32 Num = 0;
};

/**
 * The generic action table of the interaction menus (SDD 22 section 2.4): for each object type an ordered list of
 * rows (the order is the menu order: the radial menu's sectors clockwise from the top, the list menu's rows top
 * down), each either a live action or a placeholder. A row's availability follows fixed rules over FSOLActionContext,
 * checked in this order (the first failing rule names the block):
 *
 * - a placeholder row: NotYetAvailable, whatever the context;
 * - Info: always available;
 * - Target: NotTargetable unless bIsTargetable; then AlreadyTargeted if bIsSelected;
 * - MatchSpeed: NotTargetable unless bIsTargetable; then AlreadyMatched if bIsFrameLocked;
 * - Attack: NotTargetable unless bIsTargetable; NoShip unless bHasShip; JumpWarping if bJumpWarping;
 * - MoveTo: NoShip unless bHasShip; JumpWarping if bJumpWarping; NoPilotControl unless bPilotHasControl;
 *   AlreadyMovingTo if bIsMoveTarget;
 * - Follow: as MoveTo, with AlreadyFollowing if bIsFollowed in place of the last rule;
 * - JumpTo: NoShip unless bHasShip; JumpWarping if bJumpWarping;
 * - ChaseDown, Communicate, Trade: only ever placeholder rows in v1.
 *
 * Rows (v1):
 * - Star, Planet, Moon, Dwarf: Target, Info, MoveTo, Follow, JumpTo, MatchSpeed, Communicate (placeholder),
 *   Trade (placeholder);
 * - Asteroid: Target, Info, MoveTo (placeholder), Follow (placeholder), JumpTo (placeholder), MatchSpeed;
 * - Ship: Target, Info, ChaseDown (placeholder), Follow (placeholder), Communicate (placeholder), Trade (placeholder);
 * - DroppedTarget: Target, Info, Attack, ChaseDown (placeholder).
 */
namespace SOLActionTable
{
    // Returns the action-table type of an icon category (Star..Dwarf, Asteroid, Ship, Target -> DroppedTarget); Count
    // for Ring and Count (a ring system is drawn around its planet, not an object of its own)
    SOLTEST_API ESOLObjectType TypeForCategory(SOLOverlayIcons::EIconCategory category);

    // Returns the number of rows of a type (0 for Count or an out-of-range value)
    SOLTEST_API int32 RowCount(ESOLObjectType type);

    // Returns true when the type has a row for the action (placeholder rows count)
    SOLTEST_API bool Offers(ESOLObjectType type, ESOLAction action);

    // Returns true when the type's row for the action is a placeholder (false when there is no row)
    SOLTEST_API bool IsPlaceholder(ESOLObjectType type, ESOLAction action);

    // Returns why the type's action is unavailable in a context by the rules above (NotOffered without a row)
    SOLTEST_API ESOLActionBlock Evaluate(ESOLObjectType type, ESOLAction action, const FSOLActionContext& context);

    // Fills the type's rows in table order with each one's block in the context (Num = RowCount; 0 for Count)
    SOLTEST_API void Build(ESOLObjectType type, const FSOLActionContext& context, FSOLActionList& outList);

    // Returns the menu label of an action ("Target", "Move To", ...; empty for Count)
    SOLTEST_API const TCHAR* ActionLabel(ESOLAction action);

    // Returns the player-facing reason text of a block (empty for None and Count)
    SOLTEST_API const TCHAR* BlockText(ESOLActionBlock block);

    // Returns an object type's display name ("Planet", "Dropped target", ...; empty for Count)
    SOLTEST_API const TCHAR* TypeName(ESOLObjectType type);
}

// Where the orders of the interaction menus put the ship (pure geometry)
namespace SOLActionOrders
{
    // Returns an arrival offset from a body's centre: the unit direction from the body to the ship times distanceM, or
    // +Z x distanceM when the two coincide or a position is not finite; a negative or NaN distance gives zero
    SOLTEST_API FVector3d ArrivalOffset(const FVector3d& bodyM, const FVector3d& shipM, double distanceM);

    // Returns the Jump To arrival distance from an object's centre: JUMP_STANDOFF_RADII x radius + JUMP_STANDOFF_M
    // (a negative or NaN radius counts as 0)
    SOLTEST_API double JumpArrivalDistanceM(double radiusM);
}
