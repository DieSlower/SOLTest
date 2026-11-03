/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLActionTable.h"
#include "Interaction/SOLInteractMenus.h"
#include "SOLConstants.h"
#include "UI/SOLOverlayIcons.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #22 part 2 interaction menus (SDD 22 section 2.4): the action table, the arrival geometry of
// its orders, icon hit-testing, the radial menu and the map list menu, written against the contracts in
// SOLActionTable.h and SOLInteractMenus.h before the production code existed. Issue #32 (SDD 32 section 3.7)
// extended the action table contract before its production change: a live Chase Down row on Star..Dwarf (right after
// Follow) and DroppedTarget, the AlreadyChasing block and FSOLActionContext::bIsChased.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    using SOLOverlayIcons::EIconCategory;

    constexpr double INTERACTMENUS_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double INTERACTMENUS_INF = std::numeric_limits<double>::infinity();
    constexpr double INTERACTMENUS_TOLERANCE = 1.0e-9;
    constexpr double INTERACTMENUS_ANGLE_EPSILON = 1.0e-4;
    constexpr int32 INTERACTMENUS_TYPE_COUNT = static_cast<int32>(ESOLObjectType::Count);
    constexpr int32 INTERACTMENUS_ACTION_COUNT = static_cast<int32>(ESOLAction::Count);
    constexpr int32 INTERACTMENUS_BLOCK_COUNT = static_cast<int32>(ESOLActionBlock::Count);
    constexpr int32 INTERACTMENUS_CONTEXT_FLAGS = 9;

    // One expected row of the action table: its action and whether it is a placeholder
    struct FInteractMenusRow
    {
        ESOLAction Action = ESOLAction::Count;
        bool bPlaceholder = false;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the rows of a type in menu order as the contract lists them (empty for Count); issue #32 makes Chase
    // Down live on Star..Dwarf (right after Follow) and DroppedTarget, and keeps it a placeholder on Ship
    TArray<FInteractMenusRow> InteractMenusExpectedRows(const ESOLObjectType type)
    {
        switch (type)
        {
        case ESOLObjectType::Star:
        case ESOLObjectType::Planet:
        case ESOLObjectType::Moon:
        case ESOLObjectType::Dwarf:
            return TArray<FInteractMenusRow>{ { ESOLAction::Target, false }, { ESOLAction::Info, false },
                { ESOLAction::MoveTo, false }, { ESOLAction::Follow, false }, { ESOLAction::ChaseDown, false },
                { ESOLAction::JumpTo, false }, { ESOLAction::MatchSpeed, false }, { ESOLAction::Communicate, true },
                { ESOLAction::Trade, true } };
        case ESOLObjectType::Asteroid:
            return TArray<FInteractMenusRow>{ { ESOLAction::Target, false }, { ESOLAction::Info, false },
                { ESOLAction::MoveTo, true }, { ESOLAction::Follow, true }, { ESOLAction::JumpTo, true },
                { ESOLAction::MatchSpeed, false } };
        case ESOLObjectType::Ship:
            return TArray<FInteractMenusRow>{ { ESOLAction::Target, false }, { ESOLAction::Info, false },
                { ESOLAction::ChaseDown, true }, { ESOLAction::Follow, true }, { ESOLAction::Communicate, true },
                { ESOLAction::Trade, true } };
        case ESOLObjectType::DroppedTarget:
            return TArray<FInteractMenusRow>{ { ESOLAction::Target, false }, { ESOLAction::Info, false },
                { ESOLAction::Attack, false }, { ESOLAction::ChaseDown, false } };
        default:
            return TArray<FInteractMenusRow>();
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected row of a type for an action, or nullptr when the type has no such row
    const FInteractMenusRow* InteractMenusFindRow(const TArray<FInteractMenusRow>& rows, const ESOLAction action)
    {
        return rows.FindByPredicate([action](const FInteractMenusRow& row) { return row.Action == action; });
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a type's printable name for test messages
    FString InteractMenusTypeName(const ESOLObjectType type)
    {
        static const TCHAR* names[] = { TEXT("Star"), TEXT("Planet"), TEXT("Moon"), TEXT("Dwarf"), TEXT("Asteroid"),
            TEXT("Ship"), TEXT("DroppedTarget"), TEXT("Count") };
        return names[FMath::Clamp(static_cast<int32>(type), 0, INTERACTMENUS_TYPE_COUNT)];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an action's printable name for test messages
    FString InteractMenusActionName(const ESOLAction action)
    {
        static const TCHAR* names[] = { TEXT("Target"), TEXT("Info"), TEXT("MoveTo"), TEXT("Follow"),
            TEXT("JumpTo"), TEXT("MatchSpeed"), TEXT("Attack"), TEXT("ChaseDown"), TEXT("Communicate"),
            TEXT("Trade"), TEXT("Count") };
        return names[FMath::Clamp(static_cast<int32>(action), 0, INTERACTMENUS_ACTION_COUNT)];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a block's printable name for test messages
    FString InteractMenusBlockName(const ESOLActionBlock block)
    {
        static const TCHAR* names[] = { TEXT("None"), TEXT("NotYetAvailable"), TEXT("NotOffered"), TEXT("NoShip"),
            TEXT("NoPilotControl"), TEXT("JumpWarping"), TEXT("NotTargetable"), TEXT("AlreadyTargeted"),
            TEXT("AlreadyMatched"), TEXT("AlreadyMovingTo"), TEXT("AlreadyFollowing"), TEXT("AlreadyChasing"),
            TEXT("Count") };
        return names[FMath::Clamp(static_cast<int32>(block), 0, INTERACTMENUS_BLOCK_COUNT)];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a context where nothing blocks a live action: ship, conscious pilot, no warp, targetable, nothing active
    FSOLActionContext InteractMenusFavourable()
    {
        FSOLActionContext context;
        context.bHasShip = true;
        context.bPilotHasControl = true;
        context.bJumpWarping = false;
        context.bIsTargetable = true;
        return context;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a context with every flag set
    FSOLActionContext InteractMenusAllTrue()
    {
        FSOLActionContext context;
        context.bHasShip = true;
        context.bPilotHasControl = true;
        context.bJumpWarping = true;
        context.bIsTargetable = true;
        context.bIsSelected = true;
        context.bIsFrameLocked = true;
        context.bIsMoveTarget = true;
        context.bIsFollowed = true;
        context.bIsChased = true;
        return context;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the context whose flags are the bits of mask (bit 0 bHasShip ... bit 7 bIsFollowed, bit 8 bIsChased)
    FSOLActionContext InteractMenusContextFromMask(const int32 mask)
    {
        FSOLActionContext context;
        context.bHasShip = (mask & (1 << 0)) != 0;
        context.bPilotHasControl = (mask & (1 << 1)) != 0;
        context.bJumpWarping = (mask & (1 << 2)) != 0;
        context.bIsTargetable = (mask & (1 << 3)) != 0;
        context.bIsSelected = (mask & (1 << 4)) != 0;
        context.bIsFrameLocked = (mask & (1 << 5)) != 0;
        context.bIsMoveTarget = (mask & (1 << 6)) != 0;
        context.bIsFollowed = (mask & (1 << 7)) != 0;
        context.bIsChased = (mask & (1 << 8)) != 0;
        return context;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts Evaluate gives the expected block for a type, an action and a context
    void InteractMenusCheckEvaluate(FAutomationTestBase& test, const ESOLObjectType type, const ESOLAction action,
        const FSOLActionContext& context, const ESOLActionBlock expected, const FString& what)
    {
        const ESOLActionBlock actual = SOLActionTable::Evaluate(type, action, context);
        test.TestEqual(FString::Printf(TEXT("%s %s, %s: block (%s expected, %s actual)"), *InteractMenusTypeName(type),
            *InteractMenusActionName(action), *what, *InteractMenusBlockName(expected),
            *InteractMenusBlockName(actual)), static_cast<int32>(actual), static_cast<int32>(expected));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns every type whose row for the action is live (offered and not a placeholder)
    TArray<ESOLObjectType> InteractMenusLiveTypes(const ESOLAction action)
    {
        TArray<ESOLObjectType> types;
        for (int32 typeIndex = 0; typeIndex < INTERACTMENUS_TYPE_COUNT; ++typeIndex)
        {
            const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
            const TArray<FInteractMenusRow> rows = InteractMenusExpectedRows(type);
            const FInteractMenusRow* row = InteractMenusFindRow(rows, action);
            if (row != nullptr && !row->bPlaceholder)
            {
                types.Add(type);
            }
        }
        return types;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a reference to an object of a kind and index
    FSOLObjectRef InteractMenusRef(const ESOLObjectKind kind, const int32 index)
    {
        FSOLObjectRef object;
        object.Kind = kind;
        object.Index = index;
        return object;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the point at a screen angle (radians from +X toward +Y) and distance from a centre
    FVector2D InteractMenusPointAt(const FVector2D& center, const double angleRad, const double distancePx)
    {
        return center + FVector2D(FMath::Cos(angleRad), FMath::Sin(angleRad)) * distancePx;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a radial menu state is back to the default (closed, no object, no sectors, nothing highlighted)
    void InteractMenusCheckRadialDefault(FAutomationTestBase& test, const FSOLRadialMenuState& state,
        const FString& what)
    {
        test.TestFalse(what + TEXT(": closed"), state.bOpen);
        test.TestFalse(what + TEXT(": no object"), state.Object.IsValid());
        test.TestEqual(what + TEXT(": object kind None"), static_cast<int32>(state.Object.Kind),
            static_cast<int32>(ESOLObjectKind::None));
        test.TestEqual(what + TEXT(": object index"), state.Object.Index, static_cast<int32>(INDEX_NONE));
        test.TestEqual(what + TEXT(": centre"), state.CenterPx, FVector2D::ZeroVector);
        test.TestEqual(what + TEXT(": count"), state.Count, 0);
        test.TestEqual(what + TEXT(": highlighted"), state.Highlighted, static_cast<int32>(INDEX_NONE));
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts two radial menu states are equal field by field
    void InteractMenusCheckRadialEqual(FAutomationTestBase& test, const FSOLRadialMenuState& actual,
        const FSOLRadialMenuState& expected, const FString& what)
    {
        test.TestEqual(what + TEXT(": open"), actual.bOpen, expected.bOpen);
        test.TestTrue(what + TEXT(": object"), actual.Object == expected.Object);
        test.TestEqual(what + TEXT(": centre"), actual.CenterPx, expected.CenterPx);
        test.TestEqual(what + TEXT(": count"), actual.Count, expected.Count);
        test.TestEqual(what + TEXT(": highlighted"), actual.Highlighted, expected.Highlighted);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a 2D vector matches the expected one within the tolerance
    void InteractMenusCheckVector2(FAutomationTestBase& test, const FVector2D& actual, const FVector2D& expected,
        const FString& what)
    {
        test.TestEqual(what + TEXT(": x"), actual.X, expected.X, INTERACTMENUS_TOLERANCE);
        test.TestEqual(what + TEXT(": y"), actual.Y, expected.Y, INTERACTMENUS_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a 3D vector matches the expected one within the tolerance
    void InteractMenusCheckVector3(FAutomationTestBase& test, const FVector3d& actual, const FVector3d& expected,
        const FString& what)
    {
        test.TestEqual(what + TEXT(": x"), actual.X, expected.X, INTERACTMENUS_TOLERANCE);
        test.TestEqual(what + TEXT(": y"), actual.Y, expected.Y, INTERACTMENUS_TOLERANCE);
        test.TestEqual(what + TEXT(": z"), actual.Z, expected.Z, INTERACTMENUS_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a radial menu state opened over count sectors of a body at a centre (built field by field)
    FSOLRadialMenuState InteractMenusOpenState(const FVector2D& center, const int32 count)
    {
        FSOLRadialMenuState state;
        state.bOpen = true;
        state.Object = InteractMenusRef(ESOLObjectKind::Body, 3);
        state.CenterPx = center;
        state.Count = count;
        state.Highlighted = INDEX_NONE;
        return state;
    }
}

// --- Action table ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusTypeForCategoryTest, "SOLTest.InteractMenus.TypeForCategory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each icon category maps to its object type; Ring and Count map to Count
bool FSOLInteractMenusTypeForCategoryTest::RunTest(const FString& /*parameters*/)
{
    using SOLActionTable::TypeForCategory;
    const EIconCategory categories[] = { EIconCategory::Star, EIconCategory::Planet, EIconCategory::Moon,
        EIconCategory::Dwarf, EIconCategory::Asteroid, EIconCategory::Ring, EIconCategory::Ship,
        EIconCategory::Target, EIconCategory::Count };
    const ESOLObjectType expected[] = { ESOLObjectType::Star, ESOLObjectType::Planet, ESOLObjectType::Moon,
        ESOLObjectType::Dwarf, ESOLObjectType::Asteroid, ESOLObjectType::Count, ESOLObjectType::Ship,
        ESOLObjectType::DroppedTarget, ESOLObjectType::Count };
    static_assert(UE_ARRAY_COUNT(categories) == UE_ARRAY_COUNT(expected), "one expectation per category");
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(categories)); ++index)
    {
        const ESOLObjectType actual = TypeForCategory(categories[index]);
        TestEqual(FString::Printf(TEXT("category %d -> %s"), static_cast<int32>(categories[index]),
            *InteractMenusTypeName(expected[index])), static_cast<int32>(actual), static_cast<int32>(expected[index]));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusRowCountTest, "SOLTest.InteractMenus.RowCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each type has its listed number of rows; Count and an out-of-range type have none
bool FSOLInteractMenusRowCountTest::RunTest(const FString& /*parameters*/)
{
    using SOLActionTable::RowCount;
    TestEqual(TEXT("Star"), RowCount(ESOLObjectType::Star), 9);
    TestEqual(TEXT("Planet"), RowCount(ESOLObjectType::Planet), 9);
    TestEqual(TEXT("Moon"), RowCount(ESOLObjectType::Moon), 9);
    TestEqual(TEXT("Dwarf"), RowCount(ESOLObjectType::Dwarf), 9);
    TestEqual(TEXT("Asteroid"), RowCount(ESOLObjectType::Asteroid), 6);
    TestEqual(TEXT("Ship"), RowCount(ESOLObjectType::Ship), 6);
    TestEqual(TEXT("DroppedTarget"), RowCount(ESOLObjectType::DroppedTarget), 4);
    TestEqual(TEXT("Count"), RowCount(ESOLObjectType::Count), 0);
    TestEqual(TEXT("out of range"), RowCount(static_cast<ESOLObjectType>(200)), 0);
    TestEqual(TEXT("MAX_ACTIONS is 10"), SOL::Interact::MAX_ACTIONS, 10);
    for (int32 typeIndex = 0; typeIndex < INTERACTMENUS_TYPE_COUNT; ++typeIndex)
    {
        const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
        TestTrue(InteractMenusTypeName(type) + TEXT(": fits the fixed list"),
            RowCount(type) <= SOL::Interact::MAX_ACTIONS);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusBuildRowOrderTest, "SOLTest.InteractMenus.BuildRowOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Build fills each type's rows in the listed menu order with Num = RowCount; Count builds an empty list
bool FSOLInteractMenusBuildRowOrderTest::RunTest(const FString& /*parameters*/)
{
    for (int32 typeIndex = 0; typeIndex < INTERACTMENUS_TYPE_COUNT; ++typeIndex)
    {
        const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
        const TArray<FInteractMenusRow> rows = InteractMenusExpectedRows(type);
        FSOLActionList list;
        SOLActionTable::Build(type, InteractMenusFavourable(), list);
        const FString name = InteractMenusTypeName(type);
        TestEqual(name + TEXT(": Num"), list.Num, rows.Num());
        TestEqual(name + TEXT(": Num = RowCount"), list.Num, SOLActionTable::RowCount(type));
        for (int32 row = 0; row < FMath::Min(list.Num, rows.Num()); ++row)
        {
            TestEqual(FString::Printf(TEXT("%s row %d is %s"), *name, row, *InteractMenusActionName(rows[row].Action)),
                static_cast<int32>(list.Entries[row].Action), static_cast<int32>(rows[row].Action));
        }
    }

    // A list built for Count is emptied even when it held rows before
    FSOLActionList list;
    SOLActionTable::Build(ESOLObjectType::Planet, InteractMenusFavourable(), list);
    SOLActionTable::Build(ESOLObjectType::Count, InteractMenusFavourable(), list);
    TestEqual(TEXT("Count: Num 0"), list.Num, 0);

    // Rebuilding a longer list as a shorter type shrinks Num
    SOLActionTable::Build(ESOLObjectType::Star, InteractMenusFavourable(), list);
    SOLActionTable::Build(ESOLObjectType::DroppedTarget, InteractMenusFavourable(), list);
    TestEqual(TEXT("Star then DroppedTarget: Num 4"), list.Num, 4);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusOffersTest, "SOLTest.InteractMenus.OffersAndPlaceholders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Offers is true exactly for each type's listed rows, IsPlaceholder exactly for its placeholder rows
bool FSOLInteractMenusOffersTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLActionTable;
    for (int32 typeIndex = 0; typeIndex <= INTERACTMENUS_TYPE_COUNT; ++typeIndex)
    {
        const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
        const TArray<FInteractMenusRow> rows = InteractMenusExpectedRows(type);
        for (int32 actionIndex = 0; actionIndex <= INTERACTMENUS_ACTION_COUNT; ++actionIndex)
        {
            const ESOLAction action = static_cast<ESOLAction>(actionIndex);
            const FInteractMenusRow* row = InteractMenusFindRow(rows, action);
            const FString what = InteractMenusTypeName(type) + TEXT(" ") + InteractMenusActionName(action);
            TestEqual(what + TEXT(": offers"), Offers(type, action), row != nullptr);
            TestEqual(what + TEXT(": placeholder"), IsPlaceholder(type, action), row != nullptr && row->bPlaceholder);
        }
    }

    // Spot checks named by the contract
    TestFalse(TEXT("Planet does not offer Attack"), Offers(ESOLObjectType::Planet, ESOLAction::Attack));
    TestFalse(TEXT("Planet Attack is no placeholder"), IsPlaceholder(ESOLObjectType::Planet, ESOLAction::Attack));
    TestFalse(TEXT("DroppedTarget does not offer MoveTo"), Offers(ESOLObjectType::DroppedTarget, ESOLAction::MoveTo));
    TestFalse(TEXT("DroppedTarget MoveTo is no placeholder"),
        IsPlaceholder(ESOLObjectType::DroppedTarget, ESOLAction::MoveTo));
    TestTrue(TEXT("Asteroid offers MoveTo"), Offers(ESOLObjectType::Asteroid, ESOLAction::MoveTo));
    TestTrue(TEXT("Asteroid MoveTo is a placeholder"), IsPlaceholder(ESOLObjectType::Asteroid, ESOLAction::MoveTo));
    TestFalse(TEXT("Planet MoveTo is live"), IsPlaceholder(ESOLObjectType::Planet, ESOLAction::MoveTo));
    TestTrue(TEXT("Ship Follow is a placeholder"), IsPlaceholder(ESOLObjectType::Ship, ESOLAction::Follow));
    TestFalse(TEXT("Ship does not offer MatchSpeed"), Offers(ESOLObjectType::Ship, ESOLAction::MatchSpeed));

    // Chase Down (issue #32): live on bodies and the dropped target, a placeholder on Ship, absent on Asteroid
    TestTrue(TEXT("Planet offers ChaseDown"), Offers(ESOLObjectType::Planet, ESOLAction::ChaseDown));
    TestFalse(TEXT("Planet ChaseDown is live"), IsPlaceholder(ESOLObjectType::Planet, ESOLAction::ChaseDown));
    TestFalse(TEXT("DroppedTarget ChaseDown is live"),
        IsPlaceholder(ESOLObjectType::DroppedTarget, ESOLAction::ChaseDown));
    TestTrue(TEXT("Ship ChaseDown is a placeholder"), IsPlaceholder(ESOLObjectType::Ship, ESOLAction::ChaseDown));
    TestFalse(TEXT("Asteroid does not offer ChaseDown"), Offers(ESOLObjectType::Asteroid, ESOLAction::ChaseDown));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusEvaluateNotOfferedTest, "SOLTest.InteractMenus.EvaluateNotOffered",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An action without a row evaluates NotOffered whatever the context, also for the Count type and action
bool FSOLInteractMenusEvaluateNotOfferedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLActionContext contexts[] = { FSOLActionContext(), InteractMenusAllTrue(), InteractMenusFavourable() };
    const TCHAR* contextNames[] = { TEXT("all false"), TEXT("all true"), TEXT("favourable") };
    for (int32 typeIndex = 0; typeIndex <= INTERACTMENUS_TYPE_COUNT; ++typeIndex)
    {
        const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
        const TArray<FInteractMenusRow> rows = InteractMenusExpectedRows(type);
        for (int32 actionIndex = 0; actionIndex <= INTERACTMENUS_ACTION_COUNT; ++actionIndex)
        {
            const ESOLAction action = static_cast<ESOLAction>(actionIndex);
            if (InteractMenusFindRow(rows, action) != nullptr)
            {
                continue;
            }
            for (int32 contextIndex = 0; contextIndex < static_cast<int32>(UE_ARRAY_COUNT(contexts)); ++contextIndex)
            {
                InteractMenusCheckEvaluate(*this, type, action, contexts[contextIndex], ESOLActionBlock::NotOffered,
                    contextNames[contextIndex]);
            }
        }
    }
    InteractMenusCheckEvaluate(*this, ESOLObjectType::Planet, ESOLAction::Attack, InteractMenusAllTrue(),
        ESOLActionBlock::NotOffered, TEXT("spot check"));
    InteractMenusCheckEvaluate(*this, ESOLObjectType::DroppedTarget, ESOLAction::MoveTo, InteractMenusFavourable(),
        ESOLActionBlock::NotOffered, TEXT("spot check"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusEvaluatePlaceholderTest,
    "SOLTest.InteractMenus.EvaluatePlaceholderAndInfo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Placeholder rows are NotYetAvailable even in a favourable context; Info is available even with all flags false
bool FSOLInteractMenusEvaluatePlaceholderTest::RunTest(const FString& /*parameters*/)
{
    for (int32 typeIndex = 0; typeIndex < INTERACTMENUS_TYPE_COUNT; ++typeIndex)
    {
        const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
        for (const FInteractMenusRow& row : InteractMenusExpectedRows(type))
        {
            if (!row.bPlaceholder)
            {
                continue;
            }
            InteractMenusCheckEvaluate(*this, type, row.Action, InteractMenusFavourable(),
                ESOLActionBlock::NotYetAvailable, TEXT("favourable"));
            InteractMenusCheckEvaluate(*this, type, row.Action, InteractMenusAllTrue(),
                ESOLActionBlock::NotYetAvailable, TEXT("all true"));
            InteractMenusCheckEvaluate(*this, type, row.Action, FSOLActionContext(),
                ESOLActionBlock::NotYetAvailable, TEXT("all false"));
        }

        // Info: always available
        InteractMenusCheckEvaluate(*this, type, ESOLAction::Info, FSOLActionContext(), ESOLActionBlock::None,
            TEXT("all false"));
        InteractMenusCheckEvaluate(*this, type, ESOLAction::Info, InteractMenusAllTrue(), ESOLActionBlock::None,
            TEXT("all true"));
        FSOLActionContext warping;
        warping.bJumpWarping = true;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::Info, warping, ESOLActionBlock::None,
            TEXT("warping, no ship"));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusEvaluateTargetTest, "SOLTest.InteractMenus.EvaluateTargetAndMatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Target and MatchSpeed: NotTargetable first, then AlreadyTargeted / AlreadyMatched; ship and pilot do not matter
bool FSOLInteractMenusEvaluateTargetTest::RunTest(const FString& /*parameters*/)
{
    const TArray<ESOLObjectType> targetTypes = InteractMenusLiveTypes(ESOLAction::Target);
    TestEqual(TEXT("Target is live on every type"), targetTypes.Num(), INTERACTMENUS_TYPE_COUNT);
    for (const ESOLObjectType type : targetTypes)
    {
        FSOLActionContext context = InteractMenusAllTrue();
        context.bIsTargetable = false;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::Target, context, ESOLActionBlock::NotTargetable,
            TEXT("not targetable and selected"));
        InteractMenusCheckEvaluate(*this, type, ESOLAction::Target, FSOLActionContext(),
            ESOLActionBlock::NotTargetable, TEXT("all false"));
        context.bIsTargetable = true;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::Target, context, ESOLActionBlock::AlreadyTargeted,
            TEXT("all true"));

        // No ship, warping, pilot out: still available when targetable and not selected
        FSOLActionContext bare;
        bare.bIsTargetable = true;
        bare.bJumpWarping = true;
        bare.bIsFrameLocked = true;
        bare.bIsMoveTarget = true;
        bare.bIsFollowed = true;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::Target, bare, ESOLActionBlock::None,
            TEXT("targetable, no ship, warping"));
        InteractMenusCheckEvaluate(*this, type, ESOLAction::Target, InteractMenusFavourable(), ESOLActionBlock::None,
            TEXT("favourable"));
    }

    const TArray<ESOLObjectType> matchTypes = InteractMenusLiveTypes(ESOLAction::MatchSpeed);
    TestEqual(TEXT("MatchSpeed is live on Star..Dwarf and Asteroid"), matchTypes.Num(), 5);
    for (const ESOLObjectType type : matchTypes)
    {
        FSOLActionContext context = InteractMenusAllTrue();
        context.bIsTargetable = false;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::MatchSpeed, context, ESOLActionBlock::NotTargetable,
            TEXT("not targetable and locked"));
        context.bIsTargetable = true;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::MatchSpeed, context, ESOLActionBlock::AlreadyMatched,
            TEXT("all true"));

        // Selected does not block MatchSpeed; nor do ship, warp or pilot
        FSOLActionContext selected;
        selected.bIsTargetable = true;
        selected.bIsSelected = true;
        selected.bJumpWarping = true;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::MatchSpeed, selected, ESOLActionBlock::None,
            TEXT("targetable, selected, warping, no ship"));
        InteractMenusCheckEvaluate(*this, type, ESOLAction::MatchSpeed, InteractMenusFavourable(),
            ESOLActionBlock::None, TEXT("favourable"));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusEvaluateAttackTest, "SOLTest.InteractMenus.EvaluateAttack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Attack: NotTargetable, then NoShip, then JumpWarping; pilot control and selection do not block it
bool FSOLInteractMenusEvaluateAttackTest::RunTest(const FString& /*parameters*/)
{
    const TArray<ESOLObjectType> types = InteractMenusLiveTypes(ESOLAction::Attack);
    TestEqual(TEXT("Attack is live on DroppedTarget only"), types.Num(), 1);
    const ESOLObjectType type = ESOLObjectType::DroppedTarget;
    const ESOLAction action = ESOLAction::Attack;

    FSOLActionContext context;
    context.bJumpWarping = true;
    InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::NotTargetable,
        TEXT("not targetable, no ship, warping"));
    context.bIsTargetable = true;
    InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::NoShip,
        TEXT("targetable, no ship, warping"));
    context.bHasShip = true;
    InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::JumpWarping,
        TEXT("targetable, ship, warping"));
    context.bJumpWarping = false;
    InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::None,
        TEXT("targetable, ship, no pilot"));
    context.bIsSelected = true;
    context.bIsFrameLocked = true;
    InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::None,
        TEXT("targetable, ship, selected, locked"));
    InteractMenusCheckEvaluate(*this, type, action, InteractMenusFavourable(), ESOLActionBlock::None,
        TEXT("favourable"));
    InteractMenusCheckEvaluate(*this, type, action, FSOLActionContext(), ESOLActionBlock::NotTargetable,
        TEXT("all false"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusEvaluateMoveFollowTest, "SOLTest.InteractMenus.EvaluateMoveAndFollow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MoveTo and Follow: NoShip, JumpWarping, NoPilotControl, then AlreadyMovingTo / AlreadyFollowing
bool FSOLInteractMenusEvaluateMoveFollowTest::RunTest(const FString& /*parameters*/)
{
    const ESOLAction actions[] = { ESOLAction::MoveTo, ESOLAction::Follow };
    for (const ESOLAction action : actions)
    {
        const TArray<ESOLObjectType> types = InteractMenusLiveTypes(action);
        TestEqual(InteractMenusActionName(action) + TEXT(" is live on Star..Dwarf"), types.Num(), 4);
        const bool bMoveTo = action == ESOLAction::MoveTo;
        for (const ESOLObjectType type : types)
        {
            // Walk the rules in their order, each step clearing the failing condition
            FSOLActionContext context = InteractMenusAllTrue();
            context.bHasShip = false;
            context.bPilotHasControl = false;
            InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::NoShip,
                TEXT("no ship, warping, no pilot, active"));
            context.bHasShip = true;
            InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::JumpWarping,
                TEXT("ship, warping, no pilot, active"));
            context.bJumpWarping = false;
            InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::NoPilotControl,
                TEXT("ship, no pilot, active"));
            context.bPilotHasControl = true;
            InteractMenusCheckEvaluate(*this, type, action, context,
                bMoveTo ? ESOLActionBlock::AlreadyMovingTo : ESOLActionBlock::AlreadyFollowing,
                TEXT("ship, pilot, both moving to and followed"));

            // Only the action's own order blocks it
            FSOLActionContext moving = InteractMenusFavourable();
            moving.bIsMoveTarget = true;
            InteractMenusCheckEvaluate(*this, type, action, moving,
                bMoveTo ? ESOLActionBlock::AlreadyMovingTo : ESOLActionBlock::None, TEXT("favourable, moving to"));
            FSOLActionContext followed = InteractMenusFavourable();
            followed.bIsFollowed = true;
            InteractMenusCheckEvaluate(*this, type, action, followed,
                bMoveTo ? ESOLActionBlock::None : ESOLActionBlock::AlreadyFollowing, TEXT("favourable, followed"));

            // Targetability, selection and the frame lock do not matter
            FSOLActionContext untargetable = InteractMenusFavourable();
            untargetable.bIsTargetable = false;
            untargetable.bIsSelected = true;
            untargetable.bIsFrameLocked = true;
            InteractMenusCheckEvaluate(*this, type, action, untargetable, ESOLActionBlock::None,
                TEXT("not targetable, selected, locked"));
            InteractMenusCheckEvaluate(*this, type, action, InteractMenusFavourable(), ESOLActionBlock::None,
                TEXT("favourable"));
            InteractMenusCheckEvaluate(*this, type, action, FSOLActionContext(), ESOLActionBlock::NoShip,
                TEXT("all false"));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusEvaluateJumpToTest, "SOLTest.InteractMenus.EvaluateJumpTo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// JumpTo: NoShip, then JumpWarping; pilot control, targetability and active orders do not matter
bool FSOLInteractMenusEvaluateJumpToTest::RunTest(const FString& /*parameters*/)
{
    const TArray<ESOLObjectType> types = InteractMenusLiveTypes(ESOLAction::JumpTo);
    TestEqual(TEXT("JumpTo is live on Star..Dwarf"), types.Num(), 4);
    for (const ESOLObjectType type : types)
    {
        FSOLActionContext context = InteractMenusAllTrue();
        context.bHasShip = false;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::JumpTo, context, ESOLActionBlock::NoShip,
            TEXT("no ship, warping"));
        context.bHasShip = true;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::JumpTo, context, ESOLActionBlock::JumpWarping,
            TEXT("ship, warping"));
        context.bJumpWarping = false;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::JumpTo, context, ESOLActionBlock::None,
            TEXT("ship, all other flags set"));

        FSOLActionContext bare;
        bare.bHasShip = true;
        InteractMenusCheckEvaluate(*this, type, ESOLAction::JumpTo, bare, ESOLActionBlock::None,
            TEXT("ship only: no pilot, not targetable"));
        InteractMenusCheckEvaluate(*this, type, ESOLAction::JumpTo, FSOLActionContext(), ESOLActionBlock::NoShip,
            TEXT("all false"));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusChaseDownRowsTest, "SOLTest.InteractMenus.ChaseDownRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #32: Chase Down is live on Star..Dwarf (right after Follow) and DroppedTarget, a placeholder on Ship, absent on
// Asteroid, and AlreadyChasing is the last block before Count
bool FSOLInteractMenusChaseDownRowsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLActionTable;

    // AlreadyChasing is appended after AlreadyFollowing, just before Count
    TestEqual(TEXT("AlreadyChasing follows AlreadyFollowing"), static_cast<int32>(ESOLActionBlock::AlreadyChasing),
        static_cast<int32>(ESOLActionBlock::AlreadyFollowing) + 1);
    TestEqual(TEXT("AlreadyChasing is the last before Count"), static_cast<int32>(ESOLActionBlock::AlreadyChasing),
        static_cast<int32>(ESOLActionBlock::Count) - 1);

    // A new context is not chased
    TestFalse(TEXT("default context is not chased"), FSOLActionContext().bIsChased);

    // The live types and the body rows' position right after Follow
    const TArray<ESOLObjectType> liveTypes = InteractMenusLiveTypes(ESOLAction::ChaseDown);
    TestEqual(TEXT("ChaseDown is live on Star..Dwarf and DroppedTarget"), liveTypes.Num(), 5);
    const ESOLObjectType bodyTypes[] = { ESOLObjectType::Star, ESOLObjectType::Planet, ESOLObjectType::Moon,
        ESOLObjectType::Dwarf };
    for (const ESOLObjectType type : bodyTypes)
    {
        const FString name = InteractMenusTypeName(type);
        TestEqual(name + TEXT(": 9 rows"), RowCount(type), 9);
        TestTrue(name + TEXT(": ChaseDown live"), Offers(type, ESOLAction::ChaseDown)
            && !IsPlaceholder(type, ESOLAction::ChaseDown));
        FSOLActionList list;
        Build(type, InteractMenusFavourable(), list);
        int32 followRow = INDEX_NONE;
        int32 chaseRow = INDEX_NONE;
        for (int32 row = 0; row < list.Num; ++row)
        {
            followRow = list.Entries[row].Action == ESOLAction::Follow ? row : followRow;
            chaseRow = list.Entries[row].Action == ESOLAction::ChaseDown ? row : chaseRow;
        }
        TestEqual(name + TEXT(": Follow is row 3"), followRow, 3);
        TestEqual(name + TEXT(": ChaseDown right after Follow"), chaseRow, followRow + 1);
        TestTrue(name + TEXT(": fits MAX_ACTIONS"), list.Num <= SOL::Interact::MAX_ACTIONS);
    }

    // The dropped target's fourth row is a live Chase Down; the ship's third row stays a placeholder
    FSOLActionList dropped;
    Build(ESOLObjectType::DroppedTarget, InteractMenusFavourable(), dropped);
    TestEqual(TEXT("DroppedTarget: 4 rows"), dropped.Num, 4);
    if (dropped.Num == 4)
    {
        TestEqual(TEXT("DroppedTarget row 3 is ChaseDown"), static_cast<int32>(dropped.Entries[3].Action),
            static_cast<int32>(ESOLAction::ChaseDown));
        TestTrue(TEXT("DroppedTarget ChaseDown enabled when favourable"), dropped.Entries[3].IsEnabled());
    }
    FSOLActionList ship;
    Build(ESOLObjectType::Ship, InteractMenusFavourable(), ship);
    TestEqual(TEXT("Ship: 6 rows"), ship.Num, 6);
    if (ship.Num == 6)
    {
        TestEqual(TEXT("Ship row 2 is ChaseDown"), static_cast<int32>(ship.Entries[2].Action),
            static_cast<int32>(ESOLAction::ChaseDown));
        TestFalse(TEXT("Ship ChaseDown disabled when favourable"), ship.Entries[2].IsEnabled());
    }
    TestEqual(TEXT("Asteroid: 6 rows"), RowCount(ESOLObjectType::Asteroid), 6);
    TestFalse(TEXT("Asteroid has no ChaseDown"), Offers(ESOLObjectType::Asteroid, ESOLAction::ChaseDown));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusEvaluateChaseDownTest, "SOLTest.InteractMenus.EvaluateChaseDown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ChaseDown: NoShip, JumpWarping, NoPilotControl, then AlreadyChasing; the Ship placeholder is NotYetAvailable always
bool FSOLInteractMenusEvaluateChaseDownTest::RunTest(const FString& /*parameters*/)
{
    const ESOLAction action = ESOLAction::ChaseDown;
    const TArray<ESOLObjectType> types = InteractMenusLiveTypes(action);
    TestEqual(TEXT("ChaseDown is live on Star..Dwarf and DroppedTarget"), types.Num(), 5);
    for (const ESOLObjectType type : types)
    {
        // Walk the rules in their order, each step clearing the failing condition
        FSOLActionContext context = InteractMenusAllTrue();
        context.bHasShip = false;
        context.bPilotHasControl = false;
        InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::NoShip,
            TEXT("no ship, warping, no pilot, chased"));
        context.bHasShip = true;
        InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::JumpWarping,
            TEXT("ship, warping, no pilot, chased"));
        context.bJumpWarping = false;
        InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::NoPilotControl,
            TEXT("ship, no pilot, chased"));
        context.bPilotHasControl = true;
        InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::AlreadyChasing,
            TEXT("ship, pilot, chased"));
        context.bIsChased = false;
        InteractMenusCheckEvaluate(*this, type, action, context, ESOLActionBlock::None,
            TEXT("ship, pilot, every other flag set"));

        // Only its own chase blocks it: a Move To or Follow on the object does not
        FSOLActionContext chased = InteractMenusFavourable();
        chased.bIsChased = true;
        InteractMenusCheckEvaluate(*this, type, action, chased, ESOLActionBlock::AlreadyChasing,
            TEXT("favourable, chased"));
        FSOLActionContext ordered = InteractMenusFavourable();
        ordered.bIsMoveTarget = true;
        ordered.bIsFollowed = true;
        InteractMenusCheckEvaluate(*this, type, action, ordered, ESOLActionBlock::None,
            TEXT("favourable, moving to and followed"));

        // Targetability, selection and the frame lock do not matter
        FSOLActionContext untargetable = InteractMenusFavourable();
        untargetable.bIsTargetable = false;
        untargetable.bIsSelected = true;
        untargetable.bIsFrameLocked = true;
        InteractMenusCheckEvaluate(*this, type, action, untargetable, ESOLActionBlock::None,
            TEXT("not targetable, selected, locked"));
        InteractMenusCheckEvaluate(*this, type, action, InteractMenusFavourable(), ESOLActionBlock::None,
            TEXT("favourable"));
        InteractMenusCheckEvaluate(*this, type, action, FSOLActionContext(), ESOLActionBlock::NoShip,
            TEXT("all false"));
    }

    // The chase flag blocks no other action: Move To and Follow stay available on a chased body
    FSOLActionContext chasedPlanet = InteractMenusFavourable();
    chasedPlanet.bIsChased = true;
    InteractMenusCheckEvaluate(*this, ESOLObjectType::Planet, ESOLAction::MoveTo, chasedPlanet, ESOLActionBlock::None,
        TEXT("favourable, chased"));
    InteractMenusCheckEvaluate(*this, ESOLObjectType::Planet, ESOLAction::Follow, chasedPlanet, ESOLActionBlock::None,
        TEXT("favourable, chased"));

    // The Ship row is a placeholder whatever the context, the chase flag included
    FSOLActionContext shipChased = InteractMenusFavourable();
    shipChased.bIsChased = true;
    const FSOLActionContext shipContexts[] = { FSOLActionContext(), InteractMenusFavourable(), InteractMenusAllTrue(),
        shipChased };
    const TCHAR* shipContextNames[] = { TEXT("all false"), TEXT("favourable"), TEXT("all true"),
        TEXT("favourable, chased") };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(shipContexts)); ++index)
    {
        InteractMenusCheckEvaluate(*this, ESOLObjectType::Ship, action, shipContexts[index],
            ESOLActionBlock::NotYetAvailable, shipContextNames[index]);
    }

    // The asteroid has no row
    InteractMenusCheckEvaluate(*this, ESOLObjectType::Asteroid, action, InteractMenusFavourable(),
        ESOLActionBlock::NotOffered, TEXT("favourable"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusBuildBlocksTest, "SOLTest.InteractMenus.BuildBlocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// For every context flag combination, Build's block per row is Evaluate's and IsEnabled is Block None
bool FSOLInteractMenusBuildBlocksTest::RunTest(const FString& /*parameters*/)
{
    for (int32 typeIndex = 0; typeIndex < INTERACTMENUS_TYPE_COUNT; ++typeIndex)
    {
        const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
        int32 mismatches = 0;
        FString firstMismatch;
        for (int32 mask = 0; mask < (1 << INTERACTMENUS_CONTEXT_FLAGS); ++mask)
        {
            const FSOLActionContext context = InteractMenusContextFromMask(mask);
            FSOLActionList list;
            SOLActionTable::Build(type, context, list);
            if (list.Num != SOLActionTable::RowCount(type))
            {
                ++mismatches;
                firstMismatch = firstMismatch.IsEmpty() ? FString::Printf(TEXT("mask %d: Num %d"), mask, list.Num)
                    : firstMismatch;
                continue;
            }
            for (int32 row = 0; row < list.Num; ++row)
            {
                const FSOLActionEntry& entry = list.Entries[row];
                const ESOLActionBlock expected = SOLActionTable::Evaluate(type, entry.Action, context);
                const bool bBlockMatches = entry.Block == expected;
                const bool bEnabledMatches = entry.IsEnabled() == (entry.Block == ESOLActionBlock::None);
                if (!bBlockMatches || !bEnabledMatches)
                {
                    ++mismatches;
                    firstMismatch = firstMismatch.IsEmpty() ? FString::Printf(TEXT("mask %d row %d: %s vs %s"), mask,
                        row, *InteractMenusBlockName(entry.Block), *InteractMenusBlockName(expected)) : firstMismatch;
                }
            }
        }
        TestEqual(InteractMenusTypeName(type) + TEXT(": build matches evaluate (") + firstMismatch + TEXT(")"),
            mismatches, 0);
    }

    // A concrete case: a favourable Planet enables exactly its seven live rows (Chase Down included)
    FSOLActionList list;
    SOLActionTable::Build(ESOLObjectType::Planet, InteractMenusFavourable(), list);
    const bool expectedEnabled[] = { true, true, true, true, true, true, true, false, false };
    TestEqual(TEXT("favourable Planet: Num"), list.Num, static_cast<int32>(UE_ARRAY_COUNT(expectedEnabled)));
    for (int32 row = 0; row < FMath::Min(list.Num, static_cast<int32>(UE_ARRAY_COUNT(expectedEnabled))); ++row)
    {
        TestEqual(FString::Printf(TEXT("favourable Planet row %d enabled"), row), list.Entries[row].IsEnabled(),
            expectedEnabled[row]);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusTextsTest, "SOLTest.InteractMenus.Texts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Action labels, block texts and type names are non-empty and distinct, and empty for None / Count
bool FSOLInteractMenusTextsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLActionTable;

    // Action labels
    TSet<FString> labels;
    for (int32 actionIndex = 0; actionIndex < INTERACTMENUS_ACTION_COUNT; ++actionIndex)
    {
        const ESOLAction action = static_cast<ESOLAction>(actionIndex);
        const TCHAR* label = ActionLabel(action);
        TestNotNull(InteractMenusActionName(action) + TEXT(": label not null"), label);
        const FString text(label);
        TestFalse(InteractMenusActionName(action) + TEXT(": label not empty"), text.IsEmpty());
        TestFalse(InteractMenusActionName(action) + TEXT(": label distinct"), labels.Contains(text));
        labels.Add(text);
    }
    TestNotNull(TEXT("Count label not null"), ActionLabel(ESOLAction::Count));
    TestTrue(TEXT("Count label empty"), FString(ActionLabel(ESOLAction::Count)).IsEmpty());
    TestEqual(TEXT("Target label"), FString(ActionLabel(ESOLAction::Target)), FString(TEXT("Target")));
    TestEqual(TEXT("MoveTo label"), FString(ActionLabel(ESOLAction::MoveTo)), FString(TEXT("Move To")));

    // Block texts
    TSet<FString> reasons;
    for (int32 blockIndex = 1; blockIndex < INTERACTMENUS_BLOCK_COUNT; ++blockIndex)
    {
        const ESOLActionBlock block = static_cast<ESOLActionBlock>(blockIndex);
        const TCHAR* reason = BlockText(block);
        TestNotNull(InteractMenusBlockName(block) + TEXT(": text not null"), reason);
        const FString text(reason);
        TestFalse(InteractMenusBlockName(block) + TEXT(": text not empty"), text.IsEmpty());
        TestFalse(InteractMenusBlockName(block) + TEXT(": text distinct"), reasons.Contains(text));
        reasons.Add(text);
    }
    TestNotNull(TEXT("None text not null"), BlockText(ESOLActionBlock::None));
    TestTrue(TEXT("None text empty"), FString(BlockText(ESOLActionBlock::None)).IsEmpty());
    TestNotNull(TEXT("Count text not null"), BlockText(ESOLActionBlock::Count));
    TestTrue(TEXT("Count text empty"), FString(BlockText(ESOLActionBlock::Count)).IsEmpty());
    TestEqual(TEXT("AlreadyChasing text"), FString(BlockText(ESOLActionBlock::AlreadyChasing)),
        FString(TEXT("Already chasing it")));

    // Type names
    TSet<FString> names;
    for (int32 typeIndex = 0; typeIndex < INTERACTMENUS_TYPE_COUNT; ++typeIndex)
    {
        const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
        const TCHAR* name = TypeName(type);
        TestNotNull(InteractMenusTypeName(type) + TEXT(": name not null"), name);
        const FString text(name);
        TestFalse(InteractMenusTypeName(type) + TEXT(": name not empty"), text.IsEmpty());
        TestFalse(InteractMenusTypeName(type) + TEXT(": name distinct"), names.Contains(text));
        names.Add(text);
    }
    TestNotNull(TEXT("Count name not null"), TypeName(ESOLObjectType::Count));
    TestTrue(TEXT("Count name empty"), FString(TypeName(ESOLObjectType::Count)).IsEmpty());
    TestEqual(TEXT("Planet name"), FString(TypeName(ESOLObjectType::Planet)), FString(TEXT("Planet")));
    TestEqual(TEXT("DroppedTarget name"), FString(TypeName(ESOLObjectType::DroppedTarget)),
        FString(TEXT("Dropped target")));
    return true;
}

// --- Order geometry ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusArrivalOffsetTest, "SOLTest.InteractMenus.ArrivalOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The offset points from the body toward the ship at the distance; degenerate inputs fall back to +Z or zero
bool FSOLInteractMenusArrivalOffsetTest::RunTest(const FString& /*parameters*/)
{
    using SOLActionOrders::ArrivalOffset;
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(0.0, 0.0, 0.0), FVector3d(100.0, 0.0, 0.0), 50.0),
        FVector3d(50.0, 0.0, 0.0), TEXT("ship along +X"));
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(10.0, 20.0, 30.0), FVector3d(10.0, 23.0, 34.0), 10.0),
        FVector3d(0.0, 6.0, 8.0), TEXT("3-4-5 offset"));
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(1.0e12, -2.0e12, 0.0), FVector3d(1.0e12, -2.0e12,
        -5.0e9), 7.0e6), FVector3d(0.0, 0.0, -7.0e6), TEXT("far from the origin, ship below"));
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(0.0, 0.0, 0.0), FVector3d(-3.0, 0.0, 0.0), 0.0),
        FVector3d(0.0, 0.0, 0.0), TEXT("zero distance"));

    // Coincident or non-finite positions: +Z x distance
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(5.0, 5.0, 5.0), FVector3d(5.0, 5.0, 5.0), 20.0),
        FVector3d(0.0, 0.0, 20.0), TEXT("coincident"));
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(0.0, 0.0, 0.0), FVector3d(INTERACTMENUS_NAN, 1.0, 1.0),
        20.0), FVector3d(0.0, 0.0, 20.0), TEXT("ship NaN"));
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(INTERACTMENUS_INF, 0.0, 0.0), FVector3d(1.0, 1.0, 1.0),
        20.0), FVector3d(0.0, 0.0, 20.0), TEXT("body infinite"));

    // A negative or NaN distance gives zero
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(0.0, 0.0, 0.0), FVector3d(100.0, 0.0, 0.0), -5.0),
        FVector3d(0.0, 0.0, 0.0), TEXT("negative distance"));
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(0.0, 0.0, 0.0), FVector3d(100.0, 0.0, 0.0),
        INTERACTMENUS_NAN), FVector3d(0.0, 0.0, 0.0), TEXT("NaN distance"));
    InteractMenusCheckVector3(*this, ArrivalOffset(FVector3d(1.0, 1.0, 1.0), FVector3d(1.0, 1.0, 1.0), -5.0),
        FVector3d(0.0, 0.0, 0.0), TEXT("coincident, negative distance"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusJumpArrivalTest, "SOLTest.InteractMenus.JumpArrivalDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Jump To arrives JUMP_STANDOFF_RADII radii plus JUMP_STANDOFF_M from the centre; a bad radius counts as 0
bool FSOLInteractMenusJumpArrivalTest::RunTest(const FString& /*parameters*/)
{
    using SOLActionOrders::JumpArrivalDistanceM;
    const double radii = SOL::Interact::JUMP_STANDOFF_RADII;
    const double standoff = SOL::Interact::JUMP_STANDOFF_M;
    TestEqual(TEXT("1 km body"), JumpArrivalDistanceM(1000.0), radii * 1000.0 + standoff, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("Earth radius"), JumpArrivalDistanceM(6.371e6), radii * 6.371e6 + standoff, 1.0e-6);
    TestEqual(TEXT("zero radius"), JumpArrivalDistanceM(0.0), standoff, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("negative radius"), JumpArrivalDistanceM(-5.0), standoff, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("NaN radius"), JumpArrivalDistanceM(INTERACTMENUS_NAN), standoff, INTERACTMENUS_TOLERANCE);

    // LT's "1.5 R + 1000"
    TestEqual(TEXT("1.5 R + 1000 for R = 1000"), JumpArrivalDistanceM(1000.0), 2500.0, INTERACTMENUS_TOLERANCE);
    return true;
}

// --- Icon hit-testing ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusHitNearestTest, "SOLTest.InteractMenus.HitTestNearest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Among the qualifying icons the one whose centre is nearest the cursor wins, whatever their radii
bool FSOLInteractMenusHitNearestTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::HitTestIcons;
    TArray<FSOLIconHit> hits;
    FSOLIconHit a;
    a.CenterPx = FVector2D(100.0, 100.0);
    a.RadiusPx = 10.0;
    a.Object = InteractMenusRef(ESOLObjectKind::Body, 0);
    FSOLIconHit b = a;
    b.CenterPx = FVector2D(120.0, 100.0);
    b.Object = InteractMenusRef(ESOLObjectKind::Body, 1);
    hits.Add(a);
    hits.Add(b);

    TestEqual(TEXT("only A in reach"), HitTestIcons(hits, FVector2D(108.0, 100.0), 0.0), 0);
    TestEqual(TEXT("both in reach, A nearer"), HitTestIcons(hits, FVector2D(108.0, 100.0), 6.0), 0);
    TestEqual(TEXT("both in reach, B nearer"), HitTestIcons(hits, FVector2D(112.0, 100.0), 6.0), 1);
    TestEqual(TEXT("only B in reach"), HitTestIcons(hits, FVector2D(130.0, 100.0), 0.0), 1);
    TestEqual(TEXT("neither in reach"), HitTestIcons(hits, FVector2D(100.0, 200.0), 6.0),
        static_cast<int32>(INDEX_NONE));

    // A big icon does not beat a nearer small one
    TArray<FSOLIconHit> sized;
    FSOLIconHit big = a;
    big.RadiusPx = 50.0;
    FSOLIconHit small = a;
    small.CenterPx = FVector2D(105.0, 100.0);
    small.RadiusPx = 5.0;
    sized.Add(big);
    sized.Add(small);
    TestEqual(TEXT("nearer small icon wins"), HitTestIcons(sized, FVector2D(104.0, 100.0), 0.0), 1);
    TestEqual(TEXT("outside the small one, the big one"), HitTestIcons(sized, FVector2D(100.0, 130.0), 0.0), 0);

    // Euclidean distance, not per axis
    TArray<FSOLIconHit> single;
    single.Add(a);
    TestEqual(TEXT("diagonal 6-8-10 on the rim"), HitTestIcons(single, FVector2D(106.0, 108.0), 0.0), 0);
    TestEqual(TEXT("diagonal just past the rim"), HitTestIcons(single, FVector2D(107.5, 107.5), 0.0),
        static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusHitReachTest, "SOLTest.InteractMenus.HitTestReach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Reach is radius + slack, inclusive; a zero or negative radius never qualifies; a negative slack counts as 0
bool FSOLInteractMenusHitReachTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::HitTestIcons;
    TArray<FSOLIconHit> hits;
    FSOLIconHit hit;
    hit.CenterPx = FVector2D(0.0, 0.0);
    hit.RadiusPx = 10.0;
    hit.Object = InteractMenusRef(ESOLObjectKind::Ship, 4);
    hits.Add(hit);

    TestEqual(TEXT("on reach boundary (inclusive)"), HitTestIcons(hits, FVector2D(16.0, 0.0), 6.0), 0);
    TestEqual(TEXT("just past reach"), HitTestIcons(hits, FVector2D(16.001, 0.0), 6.0),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("on the radius, no slack"), HitTestIcons(hits, FVector2D(0.0, -10.0), 0.0), 0);
    TestEqual(TEXT("at the centre"), HitTestIcons(hits, FVector2D(0.0, 0.0), 0.0), 0);
    TestEqual(TEXT("with the project slack"), HitTestIcons(hits, FVector2D(0.0, 15.0),
        SOL::Interact::ICON_HIT_SLACK_PX), 0);

    // A negative slack counts as 0
    TestEqual(TEXT("negative slack, inside the radius"), HitTestIcons(hits, FVector2D(9.0, 0.0), -5.0), 0);
    TestEqual(TEXT("negative slack, on the radius"), HitTestIcons(hits, FVector2D(10.0, 0.0), -5.0), 0);
    TestEqual(TEXT("negative slack, outside the radius"), HitTestIcons(hits, FVector2D(11.0, 0.0), -5.0),
        static_cast<int32>(INDEX_NONE));

    // A zero or negative radius never qualifies, even under the cursor with slack
    TArray<FSOLIconHit> degenerate;
    FSOLIconHit zero = hit;
    zero.RadiusPx = 0.0;
    FSOLIconHit negative = hit;
    negative.RadiusPx = -10.0;
    degenerate.Add(zero);
    degenerate.Add(negative);
    TestEqual(TEXT("zero / negative radius"), HitTestIcons(degenerate, FVector2D(0.0, 0.0), 6.0),
        static_cast<int32>(INDEX_NONE));

    // A degenerate icon before a real one is skipped
    degenerate.Add(hit);
    TestEqual(TEXT("skips degenerate icons"), HitTestIcons(degenerate, FVector2D(0.0, 0.0), 6.0), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusHitEdgeTest, "SOLTest.InteractMenus.HitTestEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ties go to the lower index; an empty view or a non-finite cursor hits nothing
bool FSOLInteractMenusHitEdgeTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::HitTestIcons;
    TArray<FSOLIconHit> hits;
    FSOLIconHit left;
    left.CenterPx = FVector2D(90.0, 100.0);
    left.RadiusPx = 15.0;
    left.Object = InteractMenusRef(ESOLObjectKind::Asteroid, 0);
    FSOLIconHit right = left;
    right.CenterPx = FVector2D(110.0, 100.0);
    right.Object = InteractMenusRef(ESOLObjectKind::Asteroid, 1);
    hits.Add(left);
    hits.Add(right);
    TestEqual(TEXT("equidistant: lower index"), HitTestIcons(hits, FVector2D(100.0, 100.0), 0.0), 0);
    hits.Swap(0, 1);
    TestEqual(TEXT("equidistant, swapped: lower index"), HitTestIcons(hits, FVector2D(100.0, 100.0), 0.0), 0);

    TArray<FSOLIconHit> stacked;
    stacked.Add(left);
    stacked.Add(left);
    stacked.Add(left);
    TestEqual(TEXT("stacked icons: lowest index"), HitTestIcons(stacked, FVector2D(92.0, 101.0), 0.0), 0);

    TestEqual(TEXT("empty view"), HitTestIcons(TConstArrayView<FSOLIconHit>(), FVector2D(100.0, 100.0), 6.0),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("NaN cursor"), HitTestIcons(hits, FVector2D(INTERACTMENUS_NAN, 100.0), 6.0),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("infinite cursor"), HitTestIcons(hits, FVector2D(100.0, INTERACTMENUS_INF), 6.0),
        static_cast<int32>(INDEX_NONE));
    return true;
}

// --- Radial menu geometry ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusSectorMidTest, "SOLTest.InteractMenus.SectorMidAngle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sector 0 sits at the top (-pi/2) and later sectors step clockwise by 2 pi / n; count <= 0 gives 0
bool FSOLInteractMenusSectorMidTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::SectorMidAngleRad;
    TestEqual(TEXT("0 of 4"), SectorMidAngleRad(0, 4), -UE_DOUBLE_HALF_PI, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("1 of 4"), SectorMidAngleRad(1, 4), 0.0, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("2 of 4"), SectorMidAngleRad(2, 4), UE_DOUBLE_HALF_PI, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("3 of 4"), SectorMidAngleRad(3, 4), UE_DOUBLE_PI, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("1 of 8"), SectorMidAngleRad(1, 8), -UE_DOUBLE_PI / 4.0, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("2 of 3"), SectorMidAngleRad(2, 3), 5.0 * UE_DOUBLE_PI / 6.0, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("0 of 1"), SectorMidAngleRad(0, 1), -UE_DOUBLE_HALF_PI, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("count 0"), SectorMidAngleRad(0, 0), 0.0, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("count 0, index 2"), SectorMidAngleRad(2, 0), 0.0, INTERACTMENUS_TOLERANCE);
    TestEqual(TEXT("negative count"), SectorMidAngleRad(1, -3), 0.0, INTERACTMENUS_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusSectorAtTest, "SOLTest.InteractMenus.SectorAt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The four compass directions with four sectors, the cancel zone, far offsets, one and three sectors, bad inputs
bool FSOLInteractMenusSectorAtTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::SectorAt;
    const double inner = 24.0;
    TestEqual(TEXT("up"), SectorAt(FVector2D(0.0, -50.0), 4, inner), 0);
    TestEqual(TEXT("right"), SectorAt(FVector2D(50.0, 0.0), 4, inner), 1);
    TestEqual(TEXT("down"), SectorAt(FVector2D(0.0, 50.0), 4, inner), 2);
    TestEqual(TEXT("left"), SectorAt(FVector2D(-50.0, 0.0), 4, inner), 3);
    TestEqual(TEXT("up-right of the top"), SectorAt(FVector2D(10.0, -50.0), 4, inner), 0);
    TestEqual(TEXT("up-left of the top"), SectorAt(FVector2D(-10.0, -50.0), 4, inner), 0);

    // Cancel zone: shorter than inner; exactly inner selects
    TestEqual(TEXT("centre"), SectorAt(FVector2D(0.0, 0.0), 4, inner), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("inside the cancel zone"), SectorAt(FVector2D(0.0, -23.9), 4, inner),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("inside the cancel zone, diagonal"), SectorAt(FVector2D(16.0, 16.0), 4, inner),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("on the inner radius"), SectorAt(FVector2D(0.0, -24.0), 4, inner), 0);

    // Far beyond the outer radius still selects
    TestEqual(TEXT("far up"), SectorAt(FVector2D(0.0, -1.0e6), 4, inner), 0);
    TestEqual(TEXT("far right"), SectorAt(FVector2D(5000.0, 3.0), 4, inner), 1);
    TestEqual(TEXT("beyond the outer radius"), SectorAt(FVector2D(0.0, SOL::Interact::RADIAL_OUTER_PX * 3.0), 4,
        inner), 2);

    // One sector: everything outside the cancel zone is sector 0
    TestEqual(TEXT("1 sector, up"), SectorAt(FVector2D(0.0, -50.0), 1, inner), 0);
    TestEqual(TEXT("1 sector, down"), SectorAt(FVector2D(0.0, 50.0), 1, inner), 0);
    TestEqual(TEXT("1 sector, left"), SectorAt(FVector2D(-50.0, 0.0), 1, inner), 0);
    TestEqual(TEXT("1 sector, centre"), SectorAt(FVector2D(1.0, 1.0), 1, inner), static_cast<int32>(INDEX_NONE));

    // Three sectors: 0 at the top, 1 lower right (pi/6), 2 lower left (5 pi/6)
    TestEqual(TEXT("3 sectors, up"), SectorAt(FVector2D(0.0, -50.0), 3, inner), 0);
    TestEqual(TEXT("3 sectors, lower right"), SectorAt(InteractMenusPointAt(FVector2D::ZeroVector,
        UE_DOUBLE_PI / 6.0, 50.0), 3, inner), 1);
    TestEqual(TEXT("3 sectors, lower left"), SectorAt(InteractMenusPointAt(FVector2D::ZeroVector,
        5.0 * UE_DOUBLE_PI / 6.0, 50.0), 3, inner), 2);

    // Bad inputs
    TestEqual(TEXT("count 0"), SectorAt(FVector2D(0.0, -50.0), 0, inner), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("negative count"), SectorAt(FVector2D(0.0, -50.0), -2, inner), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("NaN offset"), SectorAt(FVector2D(INTERACTMENUS_NAN, -50.0), 4, inner),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("infinite offset"), SectorAt(FVector2D(0.0, -INTERACTMENUS_INF), 4, inner),
        static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusSectorBoundaryTest, "SOLTest.InteractMenus.SectorBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sector i spans [mid - pi/n, mid + pi/n): just either side of each boundary, the middles, and exact boundaries
bool FSOLInteractMenusSectorBoundaryTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::SectorAt;
    const double inner = 24.0;
    const double distance = 50.0;
    const int32 counts[] = { 2, 4, 5, 8, SOL::Interact::MAX_ACTIONS };
    for (const int32 count : counts)
    {
        const double step = 2.0 * UE_DOUBLE_PI / count;
        for (int32 sector = 0; sector < count; ++sector)
        {
            // Middle, and the lower boundary (between sector - 1 and sector) approached from both sides
            const double mid = -UE_DOUBLE_HALF_PI + sector * step;
            const double lowerBoundary = mid - step / 2.0;
            const int32 previous = (sector + count - 1) % count;
            TestEqual(FString::Printf(TEXT("%d sectors: middle of %d"), count, sector),
                SectorAt(InteractMenusPointAt(FVector2D::ZeroVector, mid, distance), count, inner), sector);
            TestEqual(FString::Printf(TEXT("%d sectors: just before sector %d"), count, sector),
                SectorAt(InteractMenusPointAt(FVector2D::ZeroVector, lowerBoundary - INTERACTMENUS_ANGLE_EPSILON,
                    distance), count, inner), previous);
            TestEqual(FString::Printf(TEXT("%d sectors: just into sector %d"), count, sector),
                SectorAt(InteractMenusPointAt(FVector2D::ZeroVector, lowerBoundary + INTERACTMENUS_ANGLE_EPSILON,
                    distance), count, inner), sector);
        }
    }

    // Exact boundaries with representable angles: the higher sector, the last one wrapping to 0
    TestEqual(TEXT("2 sectors, +X (0 / 1 boundary) -> 1"), SectorAt(FVector2D(50.0, 0.0), 2, inner), 1);
    TestEqual(TEXT("2 sectors, -X (1 / 0 boundary) -> 0"), SectorAt(FVector2D(-50.0, 0.0), 2, inner), 0);
    TestEqual(TEXT("4 sectors, up-right diagonal (0 / 1 boundary) -> 1"), SectorAt(FVector2D(50.0, -50.0), 4,
        inner), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusPlaceCenterTest, "SOLTest.InteractMenus.PlaceCenter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The menu centre is the icon clamped per axis into the margin, or the axis middle on a too-narrow screen
bool FSOLInteractMenusPlaceCenterTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::PlaceCenter;
    const FVector2D view(1920.0, 1080.0);
    const double margin = 90.0;
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(800.0, 500.0), view, margin), FVector2D(800.0, 500.0),
        TEXT("inside"));
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(5.0, 500.0), view, margin), FVector2D(90.0, 500.0),
        TEXT("left edge"));
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(800.0, 20.0), view, margin), FVector2D(800.0, 90.0),
        TEXT("top edge"));
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(1900.0, 1070.0), view, margin), FVector2D(1830.0, 990.0),
        TEXT("bottom-right corner"));
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(-300.0, 5000.0), view, margin), FVector2D(90.0, 990.0),
        TEXT("off screen"));
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(90.0, 990.0), view, margin), FVector2D(90.0, 990.0),
        TEXT("on the margins"));

    // Narrower than 2 x margin on one axis: that axis's middle
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(10.0, 500.0), FVector2D(100.0, 1080.0), margin),
        FVector2D(50.0, 500.0), TEXT("narrow screen, x"));
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(95.0, 1.0), FVector2D(100.0, 150.0), margin),
        FVector2D(50.0, 75.0), TEXT("narrow screen, both axes"));
    InteractMenusCheckVector2(*this, PlaceCenter(FVector2D(800.0, 500.0), view, 0.0), FVector2D(800.0, 500.0),
        TEXT("no margin"));
    return true;
}

// --- Radial menu state ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusRadialOpenTest, "SOLTest.InteractMenus.RadialOpen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Open sets the state for a valid object and count; refuses an invalid object or no actions; clamps the count
bool FSOLInteractMenusRadialOpenTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLRadialMenu;
    const FSOLObjectRef body = InteractMenusRef(ESOLObjectKind::Body, 3);
    const FVector2D center(400.0, 300.0);

    FSOLRadialMenuState state;
    state.Highlighted = 5;
    TestEqual(TEXT("opens"), static_cast<int32>(Open(state, body, center, 8)),
        static_cast<int32>(ESOLRadialOutcome::Opened));
    TestTrue(TEXT("open"), state.bOpen);
    TestTrue(TEXT("object"), state.Object == body);
    TestEqual(TEXT("centre"), state.CenterPx, center);
    TestEqual(TEXT("count"), state.Count, 8);
    TestEqual(TEXT("nothing highlighted"), state.Highlighted, static_cast<int32>(INDEX_NONE));

    // Refusals leave an open state unchanged
    const FSOLRadialMenuState before = state;
    const FVector2D elsewhere(10.0, 10.0);
    TestEqual(TEXT("negative index refused"), static_cast<int32>(Open(state,
        InteractMenusRef(ESOLObjectKind::Body, -1), elsewhere, 4)), static_cast<int32>(ESOLRadialOutcome::None));
    InteractMenusCheckRadialEqual(*this, state, before, TEXT("after negative index"));
    TestEqual(TEXT("kind None refused"), static_cast<int32>(Open(state, InteractMenusRef(ESOLObjectKind::None, 2),
        elsewhere, 4)), static_cast<int32>(ESOLRadialOutcome::None));
    InteractMenusCheckRadialEqual(*this, state, before, TEXT("after kind None"));
    TestEqual(TEXT("count 0 refused"), static_cast<int32>(Open(state, body, elsewhere, 0)),
        static_cast<int32>(ESOLRadialOutcome::None));
    InteractMenusCheckRadialEqual(*this, state, before, TEXT("after count 0"));
    TestEqual(TEXT("negative count refused"), static_cast<int32>(Open(state, body, elsewhere, -3)),
        static_cast<int32>(ESOLRadialOutcome::None));
    InteractMenusCheckRadialEqual(*this, state, before, TEXT("after negative count"));

    // Refusals leave a closed state closed
    FSOLRadialMenuState closed;
    TestEqual(TEXT("closed, invalid object refused"), static_cast<int32>(Open(closed, FSOLObjectRef(), center, 4)),
        static_cast<int32>(ESOLRadialOutcome::None));
    InteractMenusCheckRadialDefault(*this, closed, TEXT("closed after refusal"));

    // Count clamped to MAX_ACTIONS
    FSOLRadialMenuState clamped;
    TestEqual(TEXT("many actions opens"), static_cast<int32>(Open(clamped,
        InteractMenusRef(ESOLObjectKind::DroppedTarget, 0), center, SOL::Interact::MAX_ACTIONS + 5)),
        static_cast<int32>(ESOLRadialOutcome::Opened));
    TestEqual(TEXT("count clamped"), clamped.Count, SOL::Interact::MAX_ACTIONS);
    TestTrue(TEXT("index 0 is a valid object"), clamped.Object == InteractMenusRef(ESOLObjectKind::DroppedTarget, 0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusRadialMoveTest, "SOLTest.InteractMenus.RadialMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Move highlights the sector under the pointer relative to the centre, none in the cancel zone; no-op while closed
bool FSOLInteractMenusRadialMoveTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::Move;
    const FVector2D center(400.0, 300.0);
    const double inner = 24.0;
    FSOLRadialMenuState state = InteractMenusOpenState(center, 4);

    Move(state, FVector2D(400.0, 250.0), inner);
    TestEqual(TEXT("up"), state.Highlighted, 0);
    Move(state, FVector2D(450.0, 300.0), inner);
    TestEqual(TEXT("right"), state.Highlighted, 1);
    Move(state, FVector2D(400.0, 350.0), inner);
    TestEqual(TEXT("down"), state.Highlighted, 2);
    Move(state, FVector2D(350.0, 300.0), inner);
    TestEqual(TEXT("left"), state.Highlighted, 3);
    Move(state, FVector2D(400.0, -10000.0), inner);
    TestEqual(TEXT("far up"), state.Highlighted, 0);
    Move(state, FVector2D(410.0, 300.0), inner);
    TestEqual(TEXT("cancel zone"), state.Highlighted, static_cast<int32>(INDEX_NONE));
    Move(state, FVector2D(450.0, 300.0), inner);
    Move(state, center, inner);
    TestEqual(TEXT("back to the centre"), state.Highlighted, static_cast<int32>(INDEX_NONE));
    TestTrue(TEXT("still open"), state.bOpen);
    TestEqual(TEXT("count unchanged"), state.Count, 4);
    TestEqual(TEXT("centre unchanged"), state.CenterPx, center);

    // Closed: no-op
    FSOLRadialMenuState closed;
    closed.CenterPx = center;
    closed.Count = 4;
    const FSOLRadialMenuState before = closed;
    Move(closed, FVector2D(400.0, 250.0), inner);
    InteractMenusCheckRadialEqual(*this, closed, before, TEXT("closed"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusRadialReleaseTest, "SOLTest.InteractMenus.RadialRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Release confirms an enabled sector under the pointer, else cancels; it always closes; None while closed
bool FSOLInteractMenusRadialReleaseTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadialMenu::Release;
    const FVector2D center(400.0, 300.0);
    const double inner = 24.0;
    const TArray<bool> enabled = { true, false, true, true };
    int32 sector = 7;

    // Over an enabled sector: chosen by the pointer, not the last highlight
    FSOLRadialMenuState state = InteractMenusOpenState(center, 4);
    state.Highlighted = 2;
    TestEqual(TEXT("up confirms"), static_cast<int32>(Release(state, FVector2D(400.0, 250.0), inner, enabled,
        sector)), static_cast<int32>(ESOLRadialOutcome::Confirmed));
    TestEqual(TEXT("up: sector 0"), sector, 0);
    InteractMenusCheckRadialDefault(*this, state, TEXT("after confirm"));

    state = InteractMenusOpenState(center, 4);
    sector = 7;
    TestEqual(TEXT("left confirms"), static_cast<int32>(Release(state, FVector2D(300.0, 300.0), inner, enabled,
        sector)), static_cast<int32>(ESOLRadialOutcome::Confirmed));
    TestEqual(TEXT("left: sector 3"), sector, 3);
    InteractMenusCheckRadialDefault(*this, state, TEXT("after confirm left"));

    // Over a disabled sector
    state = InteractMenusOpenState(center, 4);
    sector = 7;
    TestEqual(TEXT("disabled sector cancels"), static_cast<int32>(Release(state, FVector2D(450.0, 300.0), inner,
        enabled, sector)), static_cast<int32>(ESOLRadialOutcome::Cancelled));
    TestEqual(TEXT("disabled: no sector"), sector, static_cast<int32>(INDEX_NONE));
    InteractMenusCheckRadialDefault(*this, state, TEXT("after disabled"));

    // In the centre
    state = InteractMenusOpenState(center, 4);
    sector = 7;
    TestEqual(TEXT("centre cancels"), static_cast<int32>(Release(state, FVector2D(405.0, 295.0), inner, enabled,
        sector)), static_cast<int32>(ESOLRadialOutcome::Cancelled));
    TestEqual(TEXT("centre: no sector"), sector, static_cast<int32>(INDEX_NONE));
    InteractMenusCheckRadialDefault(*this, state, TEXT("after centre"));

    // A sector beyond the enabled view counts as disabled
    const TArray<bool> shortView = { true, true };
    state = InteractMenusOpenState(center, 4);
    sector = 7;
    TestEqual(TEXT("beyond the view cancels"), static_cast<int32>(Release(state, FVector2D(400.0, 350.0), inner,
        shortView, sector)), static_cast<int32>(ESOLRadialOutcome::Cancelled));
    TestEqual(TEXT("beyond the view: no sector"), sector, static_cast<int32>(INDEX_NONE));
    InteractMenusCheckRadialDefault(*this, state, TEXT("after beyond the view"));

    state = InteractMenusOpenState(center, 4);
    sector = 7;
    TestEqual(TEXT("empty view cancels"), static_cast<int32>(Release(state, FVector2D(400.0, 250.0), inner,
        TConstArrayView<bool>(), sector)), static_cast<int32>(ESOLRadialOutcome::Cancelled));
    TestEqual(TEXT("empty view: no sector"), sector, static_cast<int32>(INDEX_NONE));

    // Closed: None
    FSOLRadialMenuState closed;
    sector = 3;
    TestEqual(TEXT("closed: None"), static_cast<int32>(Release(closed, FVector2D(400.0, 250.0), inner, enabled,
        sector)), static_cast<int32>(ESOLRadialOutcome::None));
    TestEqual(TEXT("closed: no sector"), sector, static_cast<int32>(INDEX_NONE));
    InteractMenusCheckRadialDefault(*this, closed, TEXT("closed stays default"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusRadialCancelTest, "SOLTest.InteractMenus.RadialCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Cancel closes an open menu (Cancelled) and does nothing to a closed one (None); a full open-move-cancel cycle
bool FSOLInteractMenusRadialCancelTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLRadialMenu;
    FSOLRadialMenuState state = InteractMenusOpenState(FVector2D(400.0, 300.0), 6);
    state.Highlighted = 4;
    TestEqual(TEXT("open: Cancelled"), static_cast<int32>(Cancel(state)),
        static_cast<int32>(ESOLRadialOutcome::Cancelled));
    InteractMenusCheckRadialDefault(*this, state, TEXT("after cancel"));
    TestEqual(TEXT("closed: None"), static_cast<int32>(Cancel(state)), static_cast<int32>(ESOLRadialOutcome::None));
    InteractMenusCheckRadialDefault(*this, state, TEXT("after second cancel"));

    // Open, move, cancel, then a release does nothing
    FSOLRadialMenuState cycle;
    Open(cycle, InteractMenusRef(ESOLObjectKind::Ship, 12), FVector2D(200.0, 200.0), 3);
    Move(cycle, FVector2D(200.0, 150.0), SOL::Interact::RADIAL_INNER_PX);
    TestEqual(TEXT("cycle: highlighted"), cycle.Highlighted, 0);
    TestEqual(TEXT("cycle: cancelled"), static_cast<int32>(Cancel(cycle)),
        static_cast<int32>(ESOLRadialOutcome::Cancelled));
    const TArray<bool> enabled = { true, true, true };
    int32 sector = 1;
    TestEqual(TEXT("cycle: release after cancel"), static_cast<int32>(Release(cycle, FVector2D(200.0, 150.0),
        SOL::Interact::RADIAL_INNER_PX, enabled, sector)), static_cast<int32>(ESOLRadialOutcome::None));
    TestEqual(TEXT("cycle: no sector"), sector, static_cast<int32>(INDEX_NONE));
    return true;
}

// --- List menu ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusListOpenTest, "SOLTest.InteractMenus.ListOpen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Open highlights the first enabled row, row 0 when none is, INDEX_NONE for an empty list
bool FSOLInteractMenusListOpenTest::RunTest(const FString& /*parameters*/)
{
    using SOLListMenu::Open;
    FSOLListMenuState state;
    const TArray<bool> thirdFirst = { false, false, true, true };
    Open(state, thirdFirst);
    TestEqual(TEXT("first enabled: count"), state.Count, 4);
    TestEqual(TEXT("first enabled: row 2"), state.Highlighted, 2);

    const TArray<bool> firstEnabled = { true, false };
    Open(state, firstEnabled);
    TestEqual(TEXT("row 0 enabled: count"), state.Count, 2);
    TestEqual(TEXT("row 0 enabled: row 0"), state.Highlighted, 0);

    const TArray<bool> noneEnabled = { false, false, false };
    Open(state, noneEnabled);
    TestEqual(TEXT("none enabled: count"), state.Count, 3);
    TestEqual(TEXT("none enabled: row 0"), state.Highlighted, 0);

    const TArray<bool> lastEnabled = { false, false, false, false, false, true };
    Open(state, lastEnabled);
    TestEqual(TEXT("last enabled: row 5"), state.Highlighted, 5);

    Open(state, TConstArrayView<bool>());
    TestEqual(TEXT("empty: count"), state.Count, 0);
    TestEqual(TEXT("empty: no highlight"), state.Highlighted, static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusListMoveTest, "SOLTest.InteractMenus.ListMoveHighlight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The highlight wraps both ways over any step, starts from INDEX_NONE at row 0 or the last row; empty is a no-op
bool FSOLInteractMenusListMoveTest::RunTest(const FString& /*parameters*/)
{
    using SOLListMenu::MoveHighlight;
    FSOLListMenuState state;
    state.Count = 5;
    state.Highlighted = 0;
    MoveHighlight(state, -1);
    TestEqual(TEXT("0 - 1 wraps to 4"), state.Highlighted, 4);
    MoveHighlight(state, 1);
    TestEqual(TEXT("4 + 1 wraps to 0"), state.Highlighted, 0);
    MoveHighlight(state, 1);
    TestEqual(TEXT("0 + 1"), state.Highlighted, 1);
    MoveHighlight(state, 7);
    TestEqual(TEXT("1 + 7 wraps to 3"), state.Highlighted, 3);
    MoveHighlight(state, -7);
    TestEqual(TEXT("3 - 7 wraps to 1"), state.Highlighted, 1);
    MoveHighlight(state, -12);
    TestEqual(TEXT("1 - 12 wraps to 4"), state.Highlighted, 4);
    MoveHighlight(state, 0);
    TestEqual(TEXT("step 0"), state.Highlighted, 4);
    MoveHighlight(state, 10);
    TestEqual(TEXT("full turns"), state.Highlighted, 4);
    TestEqual(TEXT("count unchanged"), state.Count, 5);

    // From INDEX_NONE
    state.Highlighted = INDEX_NONE;
    MoveHighlight(state, 1);
    TestEqual(TEXT("none + 1 -> row 0"), state.Highlighted, 0);
    state.Highlighted = INDEX_NONE;
    MoveHighlight(state, -1);
    TestEqual(TEXT("none - 1 -> last row"), state.Highlighted, 4);
    state.Highlighted = INDEX_NONE;
    MoveHighlight(state, 3);
    TestEqual(TEXT("none + 3 -> row 0"), state.Highlighted, 0);
    state.Highlighted = INDEX_NONE;
    MoveHighlight(state, -2);
    TestEqual(TEXT("none - 2 -> last row"), state.Highlighted, 4);

    // One row: always row 0
    FSOLListMenuState one;
    one.Count = 1;
    one.Highlighted = 0;
    MoveHighlight(one, 1);
    TestEqual(TEXT("one row + 1"), one.Highlighted, 0);
    MoveHighlight(one, -3);
    TestEqual(TEXT("one row - 3"), one.Highlighted, 0);

    // Empty: no-op
    FSOLListMenuState empty;
    MoveHighlight(empty, 1);
    TestEqual(TEXT("empty + 1: highlight"), empty.Highlighted, static_cast<int32>(INDEX_NONE));
    MoveHighlight(empty, -1);
    TestEqual(TEXT("empty - 1: highlight"), empty.Highlighted, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("empty: count"), empty.Count, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusListConfirmTest, "SOLTest.InteractMenus.ListCanConfirm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Confirm needs a valid highlighted row that is enabled; a row beyond the view counts as disabled
bool FSOLInteractMenusListConfirmTest::RunTest(const FString& /*parameters*/)
{
    using SOLListMenu::CanConfirm;
    const TArray<bool> enabled = { true, false, true };
    FSOLListMenuState state;
    state.Count = 3;

    state.Highlighted = 0;
    TestTrue(TEXT("row 0 enabled"), CanConfirm(state, enabled));
    state.Highlighted = 1;
    TestFalse(TEXT("row 1 disabled"), CanConfirm(state, enabled));
    state.Highlighted = 2;
    TestTrue(TEXT("row 2 enabled"), CanConfirm(state, enabled));
    state.Highlighted = INDEX_NONE;
    TestFalse(TEXT("no highlight"), CanConfirm(state, enabled));
    state.Highlighted = -2;
    TestFalse(TEXT("negative highlight"), CanConfirm(state, enabled));
    state.Highlighted = 5;
    TestFalse(TEXT("highlight past the rows"), CanConfirm(state, enabled));

    // Row beyond a shorter view
    state.Highlighted = 2;
    const TArray<bool> shortView = { true, true };
    TestFalse(TEXT("row beyond the view"), CanConfirm(state, shortView));
    TestFalse(TEXT("empty view"), CanConfirm(state, TConstArrayView<bool>()));

    FSOLListMenuState empty;
    TestFalse(TEXT("empty list"), CanConfirm(empty, TConstArrayView<bool>()));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInteractMenusListPlaceTest, "SOLTest.InteractMenus.ListPlacePanel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The panel opens right / below the cursor, flips at the right / bottom edge, and is clamped into the margin
bool FSOLInteractMenusListPlaceTest::RunTest(const FString& /*parameters*/)
{
    using SOLListMenu::PlacePanel;
    const FVector2D view(1920.0, 1080.0);
    const FVector2D panel(200.0, 150.0);
    const double offset = 14.0;
    const double margin = 12.0;

    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(500.0, 400.0), panel, view, offset, margin),
        FVector2D(514.0, 414.0), TEXT("normal"));
    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(0.0, 0.0), panel, view, offset, margin),
        FVector2D(14.0, 14.0), TEXT("top-left corner"));
    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(1800.0, 400.0), panel, view, offset, margin),
        FVector2D(1586.0, 414.0), TEXT("flip at the right edge"));
    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(500.0, 1000.0), panel, view, offset, margin),
        FVector2D(514.0, 836.0), TEXT("flip at the bottom edge"));
    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(1800.0, 1000.0), panel, view, offset, margin),
        FVector2D(1586.0, 836.0), TEXT("flip in the bottom-right corner"));

    // Off-screen cursor above-left: clamped to the margin
    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(-50.0, -50.0), panel, view, offset, margin),
        FVector2D(12.0, 12.0), TEXT("cursor above-left of the screen"));

    // Flipped past the left edge: clamped to the margin
    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(210.0, 50.0), FVector2D(200.0, 100.0),
        FVector2D(400.0, 300.0), offset, margin), FVector2D(12.0, 64.0), TEXT("flip then clamp"));

    // A panel that does not fit on an axis: the margin
    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(100.0, 50.0), FVector2D(400.0, 100.0),
        FVector2D(300.0, 200.0), offset, margin), FVector2D(12.0, 64.0), TEXT("too wide"));
    InteractMenusCheckVector2(*this, PlacePanel(FVector2D(500.0, 100.0), FVector2D(100.0, 500.0),
        FVector2D(1920.0, 300.0), offset, margin), FVector2D(514.0, 12.0), TEXT("too tall"));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
