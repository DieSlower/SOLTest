/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLActionTable.h"

#include "Menu/SOLMenuText.h"

namespace
{
    // One row of the table: an action and whether it is only a placeholder
    struct FActionRow
    {
        ESOLAction Action;
        bool bPlaceholder;
    };

    // The rows of one object type, in menu order
    struct FTypeRows
    {
        const FActionRow* Rows;
        int32 Count;
    };

    // Bodies (star, planets, moons, dwarf planets)
    constexpr FActionRow ACTION_TABLE_BODY_ROWS[] =
    {
        { ESOLAction::Target, false },
        { ESOLAction::Info, false },
        { ESOLAction::MoveTo, false },
        { ESOLAction::Follow, false },
        { ESOLAction::JumpTo, false },
        { ESOLAction::MatchSpeed, false },
        { ESOLAction::Communicate, true },
        { ESOLAction::Trade, true },
    };

    // Named asteroids (not registry bodies: no autopilot or jump orders yet)
    constexpr FActionRow ACTION_TABLE_ASTEROID_ROWS[] =
    {
        { ESOLAction::Target, false },
        { ESOLAction::Info, false },
        { ESOLAction::MoveTo, true },
        { ESOLAction::Follow, true },
        { ESOLAction::JumpTo, true },
        { ESOLAction::MatchSpeed, false },
    };

    // NPC ships (not targetable yet; Chase Down is issue #32)
    constexpr FActionRow ACTION_TABLE_SHIP_ROWS[] =
    {
        { ESOLAction::Target, false },
        { ESOLAction::Info, false },
        { ESOLAction::ChaseDown, true },
        { ESOLAction::Follow, true },
        { ESOLAction::Communicate, true },
        { ESOLAction::Trade, true },
    };

    // Dropped targets
    constexpr FActionRow ACTION_TABLE_TARGET_ROWS[] =
    {
        { ESOLAction::Target, false },
        { ESOLAction::Info, false },
        { ESOLAction::Attack, false },
        { ESOLAction::ChaseDown, true },
    };

    static_assert(UE_ARRAY_COUNT(ACTION_TABLE_BODY_ROWS) <= SOL::Interact::MAX_ACTIONS
        && UE_ARRAY_COUNT(ACTION_TABLE_ASTEROID_ROWS) <= SOL::Interact::MAX_ACTIONS
        && UE_ARRAY_COUNT(ACTION_TABLE_SHIP_ROWS) <= SOL::Interact::MAX_ACTIONS
        && UE_ARRAY_COUNT(ACTION_TABLE_TARGET_ROWS) <= SOL::Interact::MAX_ACTIONS, "Rows exceed MAX_ACTIONS");
    static_assert(UE_ARRAY_COUNT(SOLMenuText::INTERACT_ACTION_LABELS) == static_cast<int32>(ESOLAction::Count),
        "One label per action");
    static_assert(UE_ARRAY_COUNT(SOLMenuText::INTERACT_BLOCK_TEXTS) == static_cast<int32>(ESOLActionBlock::Count),
        "One reason text per block");
    static_assert(UE_ARRAY_COUNT(SOLMenuText::INTERACT_TYPE_NAMES) == static_cast<int32>(ESOLObjectType::Count),
        "One name per object type");

    //////////////////////////////////////////////////////////////////////////
    // Returns a type's rows (none for Count or an out-of-range value)
    FTypeRows ActionTableRows(const ESOLObjectType type)
    {
        switch (type)
        {
        case ESOLObjectType::Star:
        case ESOLObjectType::Planet:
        case ESOLObjectType::Moon:
        case ESOLObjectType::Dwarf:
            return { ACTION_TABLE_BODY_ROWS, UE_ARRAY_COUNT(ACTION_TABLE_BODY_ROWS) };
        case ESOLObjectType::Asteroid:
            return { ACTION_TABLE_ASTEROID_ROWS, UE_ARRAY_COUNT(ACTION_TABLE_ASTEROID_ROWS) };
        case ESOLObjectType::Ship:
            return { ACTION_TABLE_SHIP_ROWS, UE_ARRAY_COUNT(ACTION_TABLE_SHIP_ROWS) };
        case ESOLObjectType::DroppedTarget:
            return { ACTION_TABLE_TARGET_ROWS, UE_ARRAY_COUNT(ACTION_TABLE_TARGET_ROWS) };
        default:
            return { nullptr, 0 };
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the type's row for an action, or nullptr
    const FActionRow* ActionTableFindRow(const ESOLObjectType type, const ESOLAction action)
    {
        const FTypeRows rows = ActionTableRows(type);
        for (int32 index = 0; index < rows.Count; ++index)
        {
            if (rows.Rows[index].Action == action)
            {
                return &rows.Rows[index];
            }
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the block of an autopilot order (Move To, Follow) before its "already" rule
    ESOLActionBlock ActionTableOrderBlock(const FSOLActionContext& context)
    {
        if (!context.bHasShip)
        {
            return ESOLActionBlock::NoShip;
        }
        if (context.bJumpWarping)
        {
            return ESOLActionBlock::JumpWarping;
        }
        return context.bPilotHasControl ? ESOLActionBlock::None : ESOLActionBlock::NoPilotControl;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the action-table type of an icon category (Ring and Count have none)
ESOLObjectType SOLActionTable::TypeForCategory(const SOLOverlayIcons::EIconCategory category)
{
    using SOLOverlayIcons::EIconCategory;
    switch (category)
    {
    case EIconCategory::Star:
        return ESOLObjectType::Star;
    case EIconCategory::Planet:
        return ESOLObjectType::Planet;
    case EIconCategory::Moon:
        return ESOLObjectType::Moon;
    case EIconCategory::Dwarf:
        return ESOLObjectType::Dwarf;
    case EIconCategory::Asteroid:
        return ESOLObjectType::Asteroid;
    case EIconCategory::Ship:
        return ESOLObjectType::Ship;
    case EIconCategory::Target:
        return ESOLObjectType::DroppedTarget;
    default:
        return ESOLObjectType::Count;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of rows of a type
int32 SOLActionTable::RowCount(const ESOLObjectType type)
{
    return ActionTableRows(type).Count;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the type has a row for the action
bool SOLActionTable::Offers(const ESOLObjectType type, const ESOLAction action)
{
    return ActionTableFindRow(type, action) != nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the type's row for the action is a placeholder
bool SOLActionTable::IsPlaceholder(const ESOLObjectType type, const ESOLAction action)
{
    const FActionRow* row = ActionTableFindRow(type, action);
    return row != nullptr && row->bPlaceholder;
}

//////////////////////////////////////////////////////////////////////////
// Returns why the type's action is unavailable in a context (the first failing rule)
ESOLActionBlock SOLActionTable::Evaluate(const ESOLObjectType type, const ESOLAction action,
    const FSOLActionContext& context)
{
    const FActionRow* row = ActionTableFindRow(type, action);
    if (row == nullptr)
    {
        return ESOLActionBlock::NotOffered;
    }
    if (row->bPlaceholder)
    {
        return ESOLActionBlock::NotYetAvailable;
    }
    switch (action)
    {
    case ESOLAction::Info:
        return ESOLActionBlock::None;
    case ESOLAction::Target:
        return !context.bIsTargetable ? ESOLActionBlock::NotTargetable
            : context.bIsSelected ? ESOLActionBlock::AlreadyTargeted : ESOLActionBlock::None;
    case ESOLAction::MatchSpeed:
        return !context.bIsTargetable ? ESOLActionBlock::NotTargetable
            : context.bIsFrameLocked ? ESOLActionBlock::AlreadyMatched : ESOLActionBlock::None;
    case ESOLAction::Attack:
        return !context.bIsTargetable ? ESOLActionBlock::NotTargetable
            : !context.bHasShip ? ESOLActionBlock::NoShip
            : context.bJumpWarping ? ESOLActionBlock::JumpWarping : ESOLActionBlock::None;
    case ESOLAction::MoveTo:
    {
        const ESOLActionBlock block = ActionTableOrderBlock(context);
        return block != ESOLActionBlock::None ? block
            : context.bIsMoveTarget ? ESOLActionBlock::AlreadyMovingTo : ESOLActionBlock::None;
    }
    case ESOLAction::Follow:
    {
        const ESOLActionBlock block = ActionTableOrderBlock(context);
        return block != ESOLActionBlock::None ? block
            : context.bIsFollowed ? ESOLActionBlock::AlreadyFollowing : ESOLActionBlock::None;
    }
    case ESOLAction::JumpTo:
        return !context.bHasShip ? ESOLActionBlock::NoShip
            : context.bJumpWarping ? ESOLActionBlock::JumpWarping : ESOLActionBlock::None;
    default:
        // ChaseDown, Communicate and Trade are placeholder rows only in v1
        return ESOLActionBlock::NotYetAvailable;
    }
}

//////////////////////////////////////////////////////////////////////////
// Fills the type's rows in table order with each one's block in the context
void SOLActionTable::Build(const ESOLObjectType type, const FSOLActionContext& context, FSOLActionList& outList)
{
    const FTypeRows rows = ActionTableRows(type);
    outList.Num = FMath::Min(rows.Count, SOL::Interact::MAX_ACTIONS);
    for (int32 index = 0; index < outList.Num; ++index)
    {
        outList.Entries[index].Action = rows.Rows[index].Action;
        outList.Entries[index].Block = Evaluate(type, rows.Rows[index].Action, context);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the menu label of an action (empty for Count)
const TCHAR* SOLActionTable::ActionLabel(const ESOLAction action)
{
    const int32 index = static_cast<int32>(action);
    return index >= 0 && index < static_cast<int32>(ESOLAction::Count) ? SOLMenuText::INTERACT_ACTION_LABELS[index]
        : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Returns the player-facing reason text of a block (empty for None and Count)
const TCHAR* SOLActionTable::BlockText(const ESOLActionBlock block)
{
    const int32 index = static_cast<int32>(block);
    return index > 0 && index < static_cast<int32>(ESOLActionBlock::Count) ? SOLMenuText::INTERACT_BLOCK_TEXTS[index]
        : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Returns an object type's display name (empty for Count)
const TCHAR* SOLActionTable::TypeName(const ESOLObjectType type)
{
    const int32 index = static_cast<int32>(type);
    return index >= 0 && index < static_cast<int32>(ESOLObjectType::Count) ? SOLMenuText::INTERACT_TYPE_NAMES[index]
        : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Returns an arrival offset from a body's centre: toward the ship (else +Z) times the distance
FVector3d SOLActionOrders::ArrivalOffset(const FVector3d& bodyM, const FVector3d& shipM, const double distanceM)
{
    if (!FMath::IsFinite(distanceM) || distanceM < 0.0)
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d towardShip = shipM - bodyM;
    const bool bFinite = FMath::IsFinite(towardShip.X) && FMath::IsFinite(towardShip.Y)
        && FMath::IsFinite(towardShip.Z);
    const FVector3d direction = bFinite ? towardShip.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitZ())
        : FVector3d::UnitZ();
    return direction * distanceM;
}

//////////////////////////////////////////////////////////////////////////
// Returns the Jump To arrival distance from an object's centre (LT's "1.5 R + 1000")
double SOLActionOrders::JumpArrivalDistanceM(const double radiusM)
{
    const double radius = FMath::IsFinite(radiusM) ? FMath::Max(radiusM, 0.0) : 0.0;
    return SOL::Interact::JUMP_STANDOFF_RADII * radius + SOL::Interact::JUMP_STANDOFF_M;
}
