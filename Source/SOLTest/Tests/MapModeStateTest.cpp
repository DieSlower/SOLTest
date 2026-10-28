/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapModeState.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #20 map mode state machine and draft route (Docs/SDDs/20-hologram-map-and-move-orders.md section
// 2.1), written against the Map/SOLMapModeState.h contract before the production code existed
namespace
{
    constexpr double MAP_MODE_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double MAP_MODE_INF = std::numeric_limits<double>::infinity();

    //////////////////////////////////////////////////////////////////////////
    // Returns a state in a mode with the given draft size (waypoints on body 3, offsets 0, 1000, 2000, ... m)
    FSOLMapModeState MapModeMakeState(const ESOLMapMode mode, const int32 draftCount)
    {
        FSOLMapModeState state;
        state.Mode = mode;
        for (int32 index = 0; index < draftCount; ++index)
        {
            state.Draft.Add(SOLRoute::MakeWaypoint(3, 1000.0 * index, -500.0 * index, 10.0 * index));
        }
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two drafts hold the same waypoints in the same order
    bool MapModeSameDraft(const TArray<FSOLRouteWaypoint>& a, const TArray<FSOLRouteWaypoint>& b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (int32 index = 0; index < a.Num(); ++index)
        {
            if (a[index].ReferenceBodyIndex != b[index].ReferenceBodyIndex || a[index].OffsetM != b[index].OffsetM)
            {
                return false;
            }
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapModeStateDefaultsTest, "SOLTest.MapModeState.DefaultsAndOpen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new state is Move with a Loop patrol and an empty draft; the jump key opens in Jump, the plain key in the last mode
bool FSOLMapModeStateDefaultsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLMapModeState state;
    TestTrue(TEXT("default mode Move"), state.Mode == ESOLMapMode::Move);
    TestFalse(TEXT("default patrol shape Loop"), state.bPatrolPingPong);
    TestEqual(TEXT("empty draft"), state.Draft.Num(), 0);

    const ESOLMapMode modes[] = { ESOLMapMode::Jump, ESOLMapMode::Move, ESOLMapMode::Patrol };
    for (const ESOLMapMode last : modes)
    {
        const int32 lastIndex = static_cast<int32>(last);
        TestTrue(FString::Printf(TEXT("jump key from last mode %d opens Jump"), lastIndex),
            SOLMapModeState::ModeOnOpen(last, true) == ESOLMapMode::Jump);
        TestTrue(FString::Printf(TEXT("plain key from last mode %d keeps it"), lastIndex),
            SOLMapModeState::ModeOnOpen(last, false) == last);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapModeStateModeKeysTest, "SOLTest.MapModeState.ModeKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// J / M / P switch modes; the current mode's key does nothing except P in Patrol, which flips Loop / Ping-pong;
// switching keeps the draft and the patrol shape
bool FSOLMapModeStateModeKeysTest::RunTest(const FString& /*parameters*/)
{
    using SOLMapModeState::PressModeKey;
    FSOLMapModeState state = MapModeMakeState(ESOLMapMode::Move, 2);
    const TArray<FSOLRouteWaypoint> draft = state.Draft;

    TestFalse(TEXT("M in Move does nothing"), PressModeKey(state, ESOLMapMode::Move));
    TestTrue(TEXT("still Move"), state.Mode == ESOLMapMode::Move);

    TestTrue(TEXT("J switches"), PressModeKey(state, ESOLMapMode::Jump));
    TestTrue(TEXT("now Jump"), state.Mode == ESOLMapMode::Jump);
    TestFalse(TEXT("J in Jump does nothing"), PressModeKey(state, ESOLMapMode::Jump));
    TestTrue(TEXT("still Jump"), state.Mode == ESOLMapMode::Jump);
    TestTrue(TEXT("Jump keeps the draft"), MapModeSameDraft(state.Draft, draft));

    TestTrue(TEXT("P switches"), PressModeKey(state, ESOLMapMode::Patrol));
    TestTrue(TEXT("now Patrol"), state.Mode == ESOLMapMode::Patrol);
    TestFalse(TEXT("entering Patrol keeps Loop"), state.bPatrolPingPong);

    TestTrue(TEXT("P in Patrol flips the shape"), PressModeKey(state, ESOLMapMode::Patrol));
    TestTrue(TEXT("still Patrol"), state.Mode == ESOLMapMode::Patrol);
    TestTrue(TEXT("now Ping-pong"), state.bPatrolPingPong);

    // Leaving and re-entering Patrol keeps Ping-pong (the switch itself does not flip it)
    TestTrue(TEXT("M switches"), PressModeKey(state, ESOLMapMode::Move));
    TestTrue(TEXT("now Move"), state.Mode == ESOLMapMode::Move);
    TestTrue(TEXT("Move keeps Ping-pong"), state.bPatrolPingPong);
    TestTrue(TEXT("P switches back"), PressModeKey(state, ESOLMapMode::Patrol));
    TestTrue(TEXT("re-entering Patrol keeps Ping-pong"), state.bPatrolPingPong);

    TestTrue(TEXT("P again flips back"), PressModeKey(state, ESOLMapMode::Patrol));
    TestFalse(TEXT("two flips: Loop again"), state.bPatrolPingPong);
    TestTrue(TEXT("draft kept through every switch"), MapModeSameDraft(state.Draft, draft));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapModeStateRouteKindTest, "SOLTest.MapModeState.RouteKindAndEnter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The route kind Enter starts per mode and shape, and what Enter does with and without a draft
bool FSOLMapModeStateRouteKindTest::RunTest(const FString& /*parameters*/)
{
    using SOLMapModeState::EnterAction;
    using SOLMapModeState::RouteKindFor;
    FSOLMapModeState state = MapModeMakeState(ESOLMapMode::Move, 0);
    TestTrue(TEXT("Move -> Move"), RouteKindFor(state) == ESOLRouteKind::Move);
    state.Mode = ESOLMapMode::Patrol;
    TestTrue(TEXT("Patrol Loop -> PatrolLoop"), RouteKindFor(state) == ESOLRouteKind::PatrolLoop);
    state.bPatrolPingPong = true;
    TestTrue(TEXT("Patrol Ping-pong -> PatrolPingPong"), RouteKindFor(state) == ESOLRouteKind::PatrolPingPong);
    state.Mode = ESOLMapMode::Jump;
    TestTrue(TEXT("Jump -> Move"), RouteKindFor(state) == ESOLRouteKind::Move);
    state.Mode = ESOLMapMode::Move;
    TestTrue(TEXT("Move ignores the patrol shape"), RouteKindFor(state) == ESOLRouteKind::Move);

    const ESOLMapMode routeModes[] = { ESOLMapMode::Move, ESOLMapMode::Patrol };
    for (const ESOLMapMode mode : routeModes)
    {
        const TCHAR* name = mode == ESOLMapMode::Move ? TEXT("Move") : TEXT("Patrol");
        TestTrue(FString::Printf(TEXT("%s, empty draft: nothing"), name),
            EnterAction(MapModeMakeState(mode, 0)) == ESOLMapEnterAction::None);
        TestTrue(FString::Printf(TEXT("%s, one waypoint: start"), name),
            EnterAction(MapModeMakeState(mode, 1)) == ESOLMapEnterAction::StartRoute);
        TestTrue(FString::Printf(TEXT("%s, four waypoints: start"), name),
            EnterAction(MapModeMakeState(mode, 4)) == ESOLMapEnterAction::StartRoute);
    }
    TestTrue(TEXT("Jump, empty draft: jump"), EnterAction(MapModeMakeState(ESOLMapMode::Jump, 0))
        == ESOLMapEnterAction::Jump);
    TestTrue(TEXT("Jump, with a draft: still jump"), EnterAction(MapModeMakeState(ESOLMapMode::Jump, 3))
        == ESOLMapEnterAction::Jump);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapModeStateDraftEditTest, "SOLTest.MapModeState.DraftEditing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Append works (unlimited) in Move and Patrol and not in Jump; the last waypoint's height can be set; undo and clear
bool FSOLMapModeStateDraftEditTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMapModeState;
    const FSOLRouteWaypoint first = SOLRoute::MakeWaypoint(1, 100.0, 200.0, 300.0);
    const FSOLRouteWaypoint second = SOLRoute::MakeWaypoint(4, -5.0e6, 7.5e5, 0.0);

    // Jump mode refuses and leaves the draft alone
    FSOLMapModeState jump = MapModeMakeState(ESOLMapMode::Jump, 2);
    const TArray<FSOLRouteWaypoint> jumpDraft = jump.Draft;
    TestFalse(TEXT("Jump: append refused"), AppendWaypoint(jump, first));
    TestTrue(TEXT("Jump: draft untouched"), MapModeSameDraft(jump.Draft, jumpDraft));

    // Move and Patrol append in order, with no limit
    const ESOLMapMode routeModes[] = { ESOLMapMode::Move, ESOLMapMode::Patrol };
    for (const ESOLMapMode mode : routeModes)
    {
        const TCHAR* name = mode == ESOLMapMode::Move ? TEXT("Move") : TEXT("Patrol");
        FSOLMapModeState state = MapModeMakeState(mode, 0);
        TestTrue(FString::Printf(TEXT("%s: first appended"), name), AppendWaypoint(state, first));
        TestTrue(FString::Printf(TEXT("%s: second appended"), name), AppendWaypoint(state, second));
        TestTrue(FString::Printf(TEXT("%s: in order"), name), state.Draft.Num() == 2
            && state.Draft[0].ReferenceBodyIndex == 1 && state.Draft[0].OffsetM == first.OffsetM
            && state.Draft[1].ReferenceBodyIndex == 4 && state.Draft[1].OffsetM == second.OffsetM);
        bool bAllAppended = true;
        for (int32 index = 0; index < 500; ++index)
        {
            bAllAppended &= AppendWaypoint(state, SOLRoute::MakeWaypoint(index % 9, index, 0.0, 0.0));
        }
        TestTrue(FString::Printf(TEXT("%s: 500 more appended"), name), bAllAppended);
        TestEqual(FString::Printf(TEXT("%s: unlimited"), name), state.Draft.Num(), 502);
    }

    // Height of the last waypoint only
    FSOLMapModeState state = MapModeMakeState(ESOLMapMode::Move, 0);
    TestFalse(TEXT("height on an empty draft"), SetLastWaypointHeight(state, 100.0));
    AppendWaypoint(state, first);
    AppendWaypoint(state, second);
    TestTrue(TEXT("height set"), SetLastWaypointHeight(state, -2500.0));
    TestTrue(TEXT("last height changed, planar kept"), state.Draft[1].OffsetM == FVector3d(-5.0e6, 7.5e5, -2500.0));
    TestTrue(TEXT("last body kept"), state.Draft[1].ReferenceBodyIndex == 4);
    TestTrue(TEXT("first untouched"), state.Draft[0].OffsetM == first.OffsetM);
    TestFalse(TEXT("NaN height refused"), SetLastWaypointHeight(state, MAP_MODE_NAN));
    TestFalse(TEXT("infinite height refused"), SetLastWaypointHeight(state, MAP_MODE_INF));
    TestFalse(TEXT("negative infinite height refused"), SetLastWaypointHeight(state, -MAP_MODE_INF));
    TestTrue(TEXT("refusals left the height"), state.Draft[1].OffsetM.Z == -2500.0);

    // Undo removes the last; clear reports whether there was anything
    TestTrue(TEXT("undo"), UndoWaypoint(state));
    TestTrue(TEXT("undo removed the last"), state.Draft.Num() == 1 && state.Draft[0].ReferenceBodyIndex == 1);
    TestTrue(TEXT("undo again"), UndoWaypoint(state));
    TestFalse(TEXT("undo on empty"), UndoWaypoint(state));
    TestEqual(TEXT("empty after undos"), state.Draft.Num(), 0);

    FSOLMapModeState full = MapModeMakeState(ESOLMapMode::Patrol, 3);
    TestTrue(TEXT("clear with waypoints"), ClearDraft(full));
    TestEqual(TEXT("cleared"), full.Draft.Num(), 0);
    TestFalse(TEXT("clear an empty draft"), ClearDraft(full));
    TestTrue(TEXT("clear keeps the mode"), full.Mode == ESOLMapMode::Patrol);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapModeStateUndoClickTest, "SOLTest.MapModeState.UndoClick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ctrl + a short, still right click is the undo click (limits inclusive); anything else is an orbit drag
bool FSOLMapModeStateUndoClickTest::RunTest(const FString& /*parameters*/)
{
    using SOLMapModeState::IsUndoClick;
    TestEqual(TEXT("travel limit"), SOL::Route::UNDO_CLICK_MAX_TRAVEL_PX, 4.0);
    TestEqual(TEXT("time limit"), SOL::Route::UNDO_CLICK_MAX_S, 0.25);

    TestTrue(TEXT("still, quick, Ctrl"), IsUndoClick(true, 0.0, 0.1));
    TestFalse(TEXT("no Ctrl"), IsUndoClick(false, 0.0, 0.1));
    TestTrue(TEXT("exactly at both limits"), IsUndoClick(true, 4.0, 0.25));
    TestFalse(TEXT("moved too far"), IsUndoClick(true, 4.01, 0.1));
    TestFalse(TEXT("held too long"), IsUndoClick(true, 0.0, 0.26));
    TestFalse(TEXT("NaN travel"), IsUndoClick(true, MAP_MODE_NAN, 0.1));
    TestFalse(TEXT("NaN time"), IsUndoClick(true, 0.0, MAP_MODE_NAN));
    TestFalse(TEXT("infinite travel"), IsUndoClick(true, MAP_MODE_INF, 0.1));
    TestFalse(TEXT("negative travel"), IsUndoClick(true, -1.0, 0.1));
    TestFalse(TEXT("negative time"), IsUndoClick(true, 0.0, -0.1));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
