/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "HudEvents/SOLHudBus.h"
#include "SOLConstants.h"

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

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane tunnel label with every field off its default, distinct per seed
    FSOLHudLaneTunnelLabel HudBusMakeLabel(const int32 seed)
    {
        const double k = static_cast<double>(seed + 1);
        FSOLHudLaneTunnelLabel label;
        label.Lane = seed + 2;
        label.Strength = 0.1 * k;
        label.bStrongest = seed == 0;
        label.Alpha = 0.05 * k;
        label.DistanceAM = 1.0e9 * k;
        label.DistanceBM = 7.5e10 * k;
        label.ShipCount = 1000 * (seed + 3);
        label.BodyA = 3 * seed + 1;
        label.BodyB = 3 * seed + 2;
        label.TopAnchorM = FVector3d(1.0e6 * k, -2.0e6, 3.0e5 * k);
        label.BottomAnchorM = FVector3d(-4.0e6 * k, 5.0e5, -6.0e5 * k);
        return label;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if two lane tunnel labels are equal field by field
    bool HudBusSameLabel(const FSOLHudLaneTunnelLabel& a, const FSOLHudLaneTunnelLabel& b)
    {
        return a.Lane == b.Lane && a.Strength == b.Strength && a.bStrongest == b.bStrongest && a.Alpha == b.Alpha
            && a.DistanceAM == b.DistanceAM && a.DistanceBM == b.DistanceBM && a.ShipCount == b.ShipCount
            && a.BodyA == b.BodyA && a.BodyB == b.BodyB && a.TopAnchorM == b.TopAnchorM
            && a.BottomAnchorM == b.BottomAnchorM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one end of a lane tunnel dot row with full double precision, distinct per row and end
    FVector3d HudBusMakeRowPoint(const int32 row, const bool bEnd)
    {
        const double k = static_cast<double>(row + 1);
        return bEnd ? FVector3d(-3.25e9 * k - 0.125, 1.0e10 / 3.0 + k, 7.0e-3 * k)
            : FVector3d(1.5e11 + k / 3.0, -2.0e6 * k, 4.0e8 + 0.1 * k);
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
    TestEqual(TEXT("no lane tunnel labels"), bus.GetLaneTunnelLabels().Count, 0);
    TestEqual(TEXT("no lane tunnel label frame"), bus.GetLaneTunnelLabelsFrame(), static_cast<uint64>(0));
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

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudEventBusLaneLabelsTest, "SOLTest.HudEventBus.LaneTunnelLabels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 case 21 (Q4): the lane tunnel label snapshot is stored as a copy and round-trips every field of every entry
// (BodyA and BodyB included), the count and the frame it was made for; the count is clamped to
// [0, NPC_TUNNEL_LABEL_MAX_COUNT] (negative reads as 0); a cleared snapshot (count 0, or ClearLaneTunnelLabels, which
// also resets the frame and the row count) reads as no labels; the last publish wins. The dot rows (RowCount clamped
// to [0, NPC_TUNNEL_LABEL_MAX_ROWS], RowStartM / RowEndM round-tripping) are tested in HudEventBus.LaneTunnelRows
bool FSOLHudEventBusLaneLabelsTest::RunTest(const FString& /*parameters*/)
{
    const int32 maxCount = SOL::NPC_TUNNEL_LABEL_MAX_COUNT;
    FSOLHudBus bus;

    // A full snapshot round-trips
    FSOLHudLaneTunnelLabels full;
    full.Count = maxCount;
    for (int32 index = 0; index < maxCount; ++index)
    {
        full.Labels[index] = HudBusMakeLabel(index);
    }
    bus.PublishLaneTunnelLabels(full, 120);
    TestEqual(TEXT("full: frame"), bus.GetLaneTunnelLabelsFrame(), static_cast<uint64>(120));
    if (TestEqual(TEXT("full: count"), bus.GetLaneTunnelLabels().Count, maxCount))
    {
        for (int32 index = 0; index < maxCount; ++index)
        {
            TestTrue(FString::Printf(TEXT("full: entry %d"), index),
                HudBusSameLabel(bus.GetLaneTunnelLabels().Labels[index], HudBusMakeLabel(index)));
        }
    }

    // A second publish in the frame replaces the first (two labels)
    FSOLHudLaneTunnelLabels partial;
    partial.Count = 2;
    partial.Labels[0] = HudBusMakeLabel(5);
    partial.Labels[1] = HudBusMakeLabel(6);
    bus.PublishLaneTunnelLabels(partial, 121);
    TestEqual(TEXT("last publish: frame"), bus.GetLaneTunnelLabelsFrame(), static_cast<uint64>(121));
    if (TestEqual(TEXT("last publish: count"), bus.GetLaneTunnelLabels().Count, 2))
    {
        TestTrue(TEXT("last publish: entry 0"), HudBusSameLabel(bus.GetLaneTunnelLabels().Labels[0],
            HudBusMakeLabel(5)));
        TestTrue(TEXT("last publish: entry 1"), HudBusSameLabel(bus.GetLaneTunnelLabels().Labels[1],
            HudBusMakeLabel(6)));
    }

    // A count above the cap is clamped, the entries kept
    FSOLHudLaneTunnelLabels over = full;
    over.Count = maxCount + 5;
    bus.PublishLaneTunnelLabels(over, 122);
    if (TestEqual(TEXT("over: count clamped"), bus.GetLaneTunnelLabels().Count, maxCount))
    {
        for (int32 index = 0; index < maxCount; ++index)
        {
            TestTrue(FString::Printf(TEXT("over: entry %d"), index),
                HudBusSameLabel(bus.GetLaneTunnelLabels().Labels[index], HudBusMakeLabel(index)));
        }
    }

    // A negative count reads as no labels
    FSOLHudLaneTunnelLabels negative = full;
    negative.Count = -3;
    bus.PublishLaneTunnelLabels(negative, 122);
    TestEqual(TEXT("negative count: no labels"), bus.GetLaneTunnelLabels().Count, 0);

    // A cleared snapshot reads as no labels
    FSOLHudLaneTunnelLabels cleared;
    cleared.Count = 0;
    bus.PublishLaneTunnelLabels(cleared, 123);
    TestEqual(TEXT("cleared: no labels"), bus.GetLaneTunnelLabels().Count, 0);
    TestEqual(TEXT("cleared: the frame of the empty snapshot"), bus.GetLaneTunnelLabelsFrame(),
        static_cast<uint64>(123));

    // ClearLaneTunnelLabels forgets the labels and the frame (a stale snapshot never outlives its tunnels)
    bus.PublishLaneTunnelLabels(full, 124);
    bus.ClearLaneTunnelLabels();
    TestEqual(TEXT("clear: no labels"), bus.GetLaneTunnelLabels().Count, 0);
    TestEqual(TEXT("clear: frame 0"), bus.GetLaneTunnelLabelsFrame(), static_cast<uint64>(0));
    TestTrue(TEXT("clear: entries back to their defaults"),
        HudBusSameLabel(bus.GetLaneTunnelLabels().Labels[0], FSOLHudLaneTunnelLabel()));

    // Labels are not a pilot snapshot nor a command
    TestFalse(TEXT("labels publish no pilot"), bus.HasPilot());
    TestEqual(TEXT("labels send no command"), bus.GetCommandCount(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudEventBusLaneRowsTest, "SOLTest.HudEventBus.LaneTunnelRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The lane tunnel snapshot's dot rows: RowCount is clamped to [0, NPC_TUNNEL_LABEL_MAX_ROWS] (negative reads as 0,
// above the cap as the cap) and RowStartM / RowEndM entries 0 .. RowCount - 1 round-trip exactly; the bus keeps a copy
// (changing the published struct afterwards changes nothing); rows and labels are independent (rows with no labels,
// labels with no rows); ClearLaneTunnelLabels resets RowCount to 0
bool FSOLHudEventBusLaneRowsTest::RunTest(const FString& /*parameters*/)
{
    const int32 maxRows = SOL::NPC_TUNNEL_LABEL_MAX_ROWS;
    FSOLHudBus bus;
    TestEqual(TEXT("new bus: no rows"), bus.GetLaneTunnelLabels().RowCount, 0);

    // Every row slot filled; each case only changes the counts
    FSOLHudLaneTunnelLabels snapshot;
    for (int32 row = 0; row < maxRows; ++row)
    {
        snapshot.RowStartM[row] = HudBusMakeRowPoint(row, false);
        snapshot.RowEndM[row] = HudBusMakeRowPoint(row, true);
    }

    // Checks the stored row count and that the stored rows 0 .. count - 1 are the made ones, exactly
    const auto expectRows = [this, &bus](const int32 expectedCount, const FString& what)
    {
        const FSOLHudLaneTunnelLabels& stored = bus.GetLaneTunnelLabels();
        if (!TestEqual(what + TEXT(": row count"), stored.RowCount, expectedCount))
        {
            return;
        }
        int32 mismatches = 0;
        for (int32 row = 0; row < expectedCount; ++row)
        {
            const bool bSame = stored.RowStartM[row] == HudBusMakeRowPoint(row, false)
                && stored.RowEndM[row] == HudBusMakeRowPoint(row, true);
            mismatches += bSame ? 0 : 1;
        }
        TestEqual(what + TEXT(": rows round-trip exactly"), mismatches, 0);
    };

    // Every row, no labels
    snapshot.Count = 0;
    snapshot.RowCount = maxRows;
    bus.PublishLaneTunnelLabels(snapshot, 200);
    expectRows(maxRows, TEXT("all rows"));
    TestEqual(TEXT("all rows: no labels"), bus.GetLaneTunnelLabels().Count, 0);
    TestEqual(TEXT("all rows: frame"), bus.GetLaneTunnelLabelsFrame(), static_cast<uint64>(200));

    // The bus keeps a copy: changing the published struct afterwards changes nothing
    snapshot.RowStartM[0] = FVector3d::ZeroVector;
    snapshot.RowEndM[maxRows - 1] = FVector3d::ZeroVector;
    snapshot.RowCount = 1;
    expectRows(maxRows, TEXT("source changed after publishing"));
    snapshot.RowStartM[0] = HudBusMakeRowPoint(0, false);
    snapshot.RowEndM[maxRows - 1] = HudBusMakeRowPoint(maxRows - 1, true);

    // A few rows with two labels: both round-trip
    snapshot.Count = 2;
    snapshot.Labels[0] = HudBusMakeLabel(0);
    snapshot.Labels[1] = HudBusMakeLabel(1);
    snapshot.RowCount = 3;
    bus.PublishLaneTunnelLabels(snapshot, 201);
    expectRows(3, TEXT("three rows"));
    if (TestEqual(TEXT("three rows: two labels"), bus.GetLaneTunnelLabels().Count, 2))
    {
        TestTrue(TEXT("three rows: label 0"), HudBusSameLabel(bus.GetLaneTunnelLabels().Labels[0],
            HudBusMakeLabel(0)));
        TestTrue(TEXT("three rows: label 1"), HudBusSameLabel(bus.GetLaneTunnelLabels().Labels[1],
            HudBusMakeLabel(1)));
    }

    // One row, then labels with no rows
    snapshot.RowCount = 1;
    bus.PublishLaneTunnelLabels(snapshot, 202);
    expectRows(1, TEXT("one row"));
    snapshot.RowCount = 0;
    bus.PublishLaneTunnelLabels(snapshot, 203);
    expectRows(0, TEXT("labels with no rows"));
    TestEqual(TEXT("labels with no rows: two labels"), bus.GetLaneTunnelLabels().Count, 2);

    // A row count above the cap reads as the cap, the rows kept
    for (const int32 overCount : { maxRows + 1, maxRows + 10, static_cast<int32>(MAX_int32) })
    {
        snapshot.RowCount = overCount;
        bus.PublishLaneTunnelLabels(snapshot, 204);
        expectRows(maxRows, FString::Printf(TEXT("row count %d"), overCount));
    }

    // A negative row count reads as 0 (the labels unaffected)
    for (const int32 negativeCount : { -1, -1000, static_cast<int32>(MIN_int32) })
    {
        snapshot.RowCount = maxRows;
        bus.PublishLaneTunnelLabels(snapshot, 205);
        snapshot.RowCount = negativeCount;
        bus.PublishLaneTunnelLabels(snapshot, 206);
        expectRows(0, FString::Printf(TEXT("row count %d"), negativeCount));
        TestEqual(FString::Printf(TEXT("row count %d: two labels"), negativeCount), bus.GetLaneTunnelLabels().Count,
            2);
    }

    // A negative label count reads as 0, the rows unaffected
    snapshot.Count = -3;
    snapshot.RowCount = 5;
    bus.PublishLaneTunnelLabels(snapshot, 207);
    TestEqual(TEXT("negative label count: no labels"), bus.GetLaneTunnelLabels().Count, 0);
    expectRows(5, TEXT("negative label count"));

    // ClearLaneTunnelLabels resets the rows with the labels and the frame
    snapshot.Count = 2;
    snapshot.RowCount = maxRows;
    bus.PublishLaneTunnelLabels(snapshot, 208);
    bus.ClearLaneTunnelLabels();
    TestEqual(TEXT("clear: no rows"), bus.GetLaneTunnelLabels().RowCount, 0);
    TestEqual(TEXT("clear: no labels"), bus.GetLaneTunnelLabels().Count, 0);
    TestEqual(TEXT("clear: frame 0"), bus.GetLaneTunnelLabelsFrame(), static_cast<uint64>(0));

    // Publishing after a clear works again
    snapshot.RowCount = 4;
    bus.PublishLaneTunnelLabels(snapshot, 209);
    expectRows(4, TEXT("after clear"));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
