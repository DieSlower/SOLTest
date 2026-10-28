/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "HudEvents/SOLHudBus.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // Frame numbers used by the publish tests
    constexpr uint64 HUDBUS_FRAME_A = 41;
    constexpr uint64 HUDBUS_FRAME_B = 42;

    //////////////////////////////////////////////////////////////////////////
    // Returns a snapshot with every field off its default
    FSOLHudPilotSnapshot HudBusMakeSnapshot(const double offsetX, const ESOLSteerIcon icon)
    {
        FSOLHudPilotSnapshot snapshot;
        snapshot.StickOffsetPx = FVector2d(offsetX, -7.5);
        snapshot.StickRadiusPx = 120.0;
        snapshot.SteerIcon = icon;
        snapshot.bIsSteering = true;
        snapshot.bIsFreeLooking = true;
        return snapshot;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if two snapshots are equal field by field
    bool HudBusSameSnapshot(const FSOLHudPilotSnapshot& a, const FSOLHudPilotSnapshot& b)
    {
        return a.StickOffsetPx == b.StickOffsetPx && a.StickRadiusPx == b.StickRadiusPx && a.SteerIcon == b.SteerIcon
            && a.bIsSteering == b.bIsSteering && a.bIsFreeLooking == b.bIsFreeLooking;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudEventBusDefaultsTest, "SOLTest.HudEventBus.Defaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new bus has no pilot, no counts, no overlay and a default snapshot
bool FSOLHudEventBusDefaultsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudBus bus;
    TestFalse(TEXT("no pilot"), bus.HasPilot());
    TestTrue(TEXT("pilot frame 0"), bus.GetPilotFrame() == 0);
    TestEqual(TEXT("no publishes"), bus.GetPilotPublishCount(), 0);
    TestEqual(TEXT("no commands"), bus.GetCommandCount(), 0);
    TestNull(TEXT("no overlay"), bus.GetMapBodyOverlay());
    TestTrue(TEXT("default snapshot"), HudBusSameSnapshot(bus.GetPilot(), FSOLHudPilotSnapshot()));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudEventBusPublishTest, "SOLTest.HudEventBus.PublishPilot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Publishing stores the snapshot and frame; a second publish coalesces (the latest wins) and both are counted
bool FSOLHudEventBusPublishTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudBus bus;
    const FSOLHudPilotSnapshot first = HudBusMakeSnapshot(3.0, ESOLSteerIcon::Held);
    bus.PublishPilot(first, HUDBUS_FRAME_A);
    TestTrue(TEXT("has pilot"), bus.HasPilot());
    TestTrue(TEXT("snapshot stored"), HudBusSameSnapshot(bus.GetPilot(), first));
    TestTrue(TEXT("frame stored"), bus.GetPilotFrame() == HUDBUS_FRAME_A);
    TestEqual(TEXT("one publish"), bus.GetPilotPublishCount(), 1);

    FSOLHudPilotSnapshot second = HudBusMakeSnapshot(-9.0, ESOLSteerIcon::Locked);
    second.bIsFreeLooking = false;
    bus.PublishPilot(second, HUDBUS_FRAME_B);
    TestTrue(TEXT("latest snapshot wins"), HudBusSameSnapshot(bus.GetPilot(), second));
    TestTrue(TEXT("latest frame wins"), bus.GetPilotFrame() == HUDBUS_FRAME_B);
    TestEqual(TEXT("two publishes"), bus.GetPilotPublishCount(), 2);
    TestTrue(TEXT("still has pilot"), bus.HasPilot());
    TestEqual(TEXT("publishing is not a command"), bus.GetCommandCount(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudEventBusClearTest, "SOLTest.HudEventBus.ClearPilot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Clearing forgets the pilot and resets the snapshot (the count stays); a later publish works again
bool FSOLHudEventBusClearTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudBus bus;
    bus.PublishPilot(HudBusMakeSnapshot(5.0, ESOLSteerIcon::Locked), HUDBUS_FRAME_A);
    bus.ClearPilot();
    TestFalse(TEXT("pilot cleared"), bus.HasPilot());
    TestTrue(TEXT("snapshot back to defaults"), HudBusSameSnapshot(bus.GetPilot(), FSOLHudPilotSnapshot()));
    TestEqual(TEXT("count unchanged"), bus.GetPilotPublishCount(), 1);

    // Clearing twice (or with no pilot) is harmless
    bus.ClearPilot();
    TestFalse(TEXT("second clear: still no pilot"), bus.HasPilot());

    const FSOLHudPilotSnapshot again = HudBusMakeSnapshot(1.0, ESOLSteerIcon::Held);
    bus.PublishPilot(again, HUDBUS_FRAME_B);
    TestTrue(TEXT("publish after clear: has pilot"), bus.HasPilot());
    TestTrue(TEXT("publish after clear: snapshot"), HudBusSameSnapshot(bus.GetPilot(), again));
    TestTrue(TEXT("publish after clear: frame"), bus.GetPilotFrame() == HUDBUS_FRAME_B);
    TestEqual(TEXT("publish after clear: count"), bus.GetPilotPublishCount(), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudEventBusCommandTest, "SOLTest.HudEventBus.SendCommand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Commands reach every subscriber synchronously, once each (no order between subscribers); removed ones are not called
bool FSOLHudEventBusCommandTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudBus bus;

    // No subscribers: safe, still counted
    bus.SendCommand(ESOLHudCommand::RadarAuto);
    TestEqual(TEXT("unsubscribed send counted"), bus.GetCommandCount(), 1);

    // Two subscribers log into one sequence, tagged with their id
    TArray<TPair<int32, ESOLHudCommand>> received;
    const FDelegateHandle firstHandle = bus.OnCommand().AddLambda([&received](const ESOLHudCommand command)
    {
        received.Emplace(1, command);
    });
    const FDelegateHandle secondHandle = bus.OnCommand().AddLambda([&received](const ESOLHudCommand command)
    {
        received.Emplace(2, command);
    });

    bus.SendCommand(ESOLHudCommand::ToggleOrbitLines);
    // UE multicast delegates do not guarantee an order between subscribers, so only that each got it once
    if (TestEqual(TEXT("both received synchronously, once each"), received.Num(), 2))
    {
        TestTrue(TEXT("each subscriber once"), received[0].Key != received[1].Key);
        TestTrue(TEXT("both got the command"), received[0].Value == ESOLHudCommand::ToggleOrbitLines
            && received[1].Value == ESOLHudCommand::ToggleOrbitLines);
    }
    TestEqual(TEXT("two commands counted"), bus.GetCommandCount(), 2);

    // A removed subscriber is not called
    received.Reset();
    bus.OnCommand().Remove(firstHandle);
    bus.SendCommand(ESOLHudCommand::RadarZoomIn);
    if (TestEqual(TEXT("only the remaining subscriber"), received.Num(), 1))
    {
        TestTrue(TEXT("remaining subscriber got it"), received[0].Key == 2
            && received[0].Value == ESOLHudCommand::RadarZoomIn);
    }
    TestEqual(TEXT("three commands counted"), bus.GetCommandCount(), 3);

    // Four commands in a row arrive in order
    received.Reset();
    const ESOLHudCommand sequence[] = { ESOLHudCommand::ToggleOrbitLines, ESOLHudCommand::RadarZoomIn,
        ESOLHudCommand::RadarZoomOut, ESOLHudCommand::RadarAuto };
    for (const ESOLHudCommand command : sequence)
    {
        bus.SendCommand(command);
    }
    if (TestEqual(TEXT("four received"), received.Num(), 4))
    {
        for (int32 i = 0; i < 4; ++i)
        {
            TestTrue(FString::Printf(TEXT("command %d in order"), i), received[i].Value == sequence[i]);
        }
    }
    TestEqual(TEXT("seven commands counted"), bus.GetCommandCount(), 7);

    // Every subscriber removed: still safe and counted
    bus.OnCommand().Remove(secondHandle);
    received.Reset();
    bus.SendCommand(ESOLHudCommand::RadarZoomOut);
    TestEqual(TEXT("nobody called"), received.Num(), 0);
    TestEqual(TEXT("eight commands counted"), bus.GetCommandCount(), 8);
    TestFalse(TEXT("commands do not publish a pilot"), bus.HasPilot());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudEventBusOverlayTest, "SOLTest.HudEventBus.MapBodyOverlay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The overlay pointer round-trips and nullptr clears it (the bus never dereferences it)
bool FSOLHudEventBusOverlayTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudBus bus;
    alignas(16) uint8 storage[16] = {};
    const FSOLMapBodyOverlay* overlay = reinterpret_cast<const FSOLMapBodyOverlay*>(storage);
    bus.SetMapBodyOverlay(overlay);
    TestTrue(TEXT("overlay stored"), bus.GetMapBodyOverlay() == overlay);
    bus.SetMapBodyOverlay(nullptr);
    TestNull(TEXT("nullptr clears"), bus.GetMapBodyOverlay());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
