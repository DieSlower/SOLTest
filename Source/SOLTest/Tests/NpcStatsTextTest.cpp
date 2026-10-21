/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcStatsText.h"

#include "Npc/SOLNpcScaleMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #11 (Part 10e) NPC stats overlay text, written against the 10e contract in
// Docs/Plans/11-scale-and-performance-demo-plan.md before any production code existed.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Number of lines the overlay always has
    constexpr int32 NPCSTATSTEXT_LINE_COUNT = 8;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the non-finite input cases
    double NpcStatsTextNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the non-finite input cases
    double NpcStatsTextInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one overlay line, or an empty string when the line is missing (the count is tested separately)
    FString NpcStatsTextLine(const TArray<FString>& lines, int32 index)
    {
        return lines.IsValidIndex(index) ? lines[index] : FString();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an input with every field set to a distinct value, measuring stage 3 of 5 with a previous result
    SOLNpcStatsText::FOverlayInput NpcStatsTextFullInput()
    {
        SOLNpcStatsText::FOverlayInput in;
        in.FrameMs = 8.0;
        in.FrameP95Ms = 9.5;
        in.GameMs = 3.25;
        in.RenderMs = 4.5;
        in.GpuMs = 7.12;
        in.NpcMs = 1.75;
        in.Total = 1000000;
        in.Far = 900000;
        in.Mid = 96000;
        in.Full = 4000;
        in.Point = 980000;
        in.Instanced = 19968;
        in.Mesh = 32;
        in.Awake = 1234;
        in.TierChanges = 56;
        in.bCombat = true;
        in.CombatModel = 2;
        in.CombatShips = 2000;
        in.Alive = 1873;
        in.LiveBolts = 12345;
        in.RealBolts = 678;
        in.UsedMemoryMB = 4321.6;
        in.RampPhase = SOLNpcScale::ERampPhase::Measuring;
        in.RampStageIndex = 2;
        in.RampStageCount = 5;
        in.RampStageNpcCount = 250000;
        in.RampPhaseElapsedS = 3.2;
        in.RampPhaseLengthS = 10.0;
        in.RampResult = TEXT("100,000 ships p95 12.34 ms PASS");
        return in;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextGroupedIntegerTest, "SOLTest.NpcStatsText.GroupedInteger",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Integers print with ',' between groups of three digits and a leading '-' for negatives, across the int64 range
bool FNpcStatsTextGroupedIntegerTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcStatsText::FormatGroupedInteger;
    TestEqual(TEXT("0"), FormatGroupedInteger(0), FString(TEXT("0")));
    TestEqual(TEXT("1"), FormatGroupedInteger(1), FString(TEXT("1")));
    TestEqual(TEXT("-1"), FormatGroupedInteger(-1), FString(TEXT("-1")));
    TestEqual(TEXT("999"), FormatGroupedInteger(999), FString(TEXT("999")));
    TestEqual(TEXT("-999"), FormatGroupedInteger(-999), FString(TEXT("-999")));
    TestEqual(TEXT("1000"), FormatGroupedInteger(1000), FString(TEXT("1,000")));
    TestEqual(TEXT("-1000"), FormatGroupedInteger(-1000), FString(TEXT("-1,000")));
    TestEqual(TEXT("100000"), FormatGroupedInteger(100000), FString(TEXT("100,000")));
    TestEqual(TEXT("999999"), FormatGroupedInteger(999999), FString(TEXT("999,999")));
    TestEqual(TEXT("1000000"), FormatGroupedInteger(1000000), FString(TEXT("1,000,000")));
    TestEqual(TEXT("-1234567"), FormatGroupedInteger(-1234567), FString(TEXT("-1,234,567")));
    TestEqual(TEXT("int64 max"), FormatGroupedInteger(std::numeric_limits<int64>::max()),
        FString(TEXT("9,223,372,036,854,775,807")));
    TestEqual(TEXT("int64 min"), FormatGroupedInteger(std::numeric_limits<int64>::min()),
        FString(TEXT("-9,223,372,036,854,775,808")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextRampResultTest, "SOLTest.NpcStatsText.RampResult",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A finished ramp stage reads "<grouped count> ships p95 <ms, 2 decimals> ms PASS|FAIL"
bool FNpcStatsTextRampResultTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcStatsText::FormatRampResult;
    TestEqual(TEXT("pass"), FormatRampResult(250000, 14.2, true), FString(TEXT("250,000 ships p95 14.20 ms PASS")));
    TestEqual(TEXT("fail"), FormatRampResult(1000000, 21.789, false),
        FString(TEXT("1,000,000 ships p95 21.79 ms FAIL")));
    TestEqual(TEXT("zero"), FormatRampResult(0, 0.0, true), FString(TEXT("0 ships p95 0.00 ms PASS")));
    TestEqual(TEXT("small count"), FormatRampResult(999, 3.0, false), FString(TEXT("999 ships p95 3.00 ms FAIL")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextCombatModelNameTest, "SOLTest.NpcStatsText.CombatModelName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Models 0, 1 and 2 are bolts, statistical and hybrid; any other value reads as bolts
bool FNpcStatsTextCombatModelNameTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcStatsText::CombatModelName;
    TestEqual(TEXT("0"), CombatModelName(0), FString(TEXT("bolts")));
    TestEqual(TEXT("1"), CombatModelName(1), FString(TEXT("statistical")));
    TestEqual(TEXT("2"), CombatModelName(2), FString(TEXT("hybrid")));
    TestEqual(TEXT("3"), CombatModelName(3), FString(TEXT("bolts")));
    TestEqual(TEXT("-1"), CombatModelName(-1), FString(TEXT("bolts")));
    TestEqual(TEXT("int32 max"), CombatModelName(std::numeric_limits<int32>::max()), FString(TEXT("bolts")));
    TestEqual(TEXT("int32 min"), CombatModelName(std::numeric_limits<int32>::min()), FString(TEXT("bolts")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextOverlayFullTest, "SOLTest.NpcStatsText.OverlayFull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A fully populated input gives exactly the eight contract lines
bool FNpcStatsTextOverlayFullTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> lines = SOLNpcStatsText::FormatOverlayLines(NpcStatsTextFullInput());
    TestEqual(TEXT("eight lines"), lines.Num(), NPCSTATSTEXT_LINE_COUNT);
    TestEqual(TEXT("frame line"), NpcStatsTextLine(lines, 0), FString(TEXT("FPS 125  frame 8.00 ms  p95 9.50 ms")));
    TestEqual(TEXT("thread line"), NpcStatsTextLine(lines, 1),
        FString(TEXT("game 3.25  render 4.50  GPU 7.12  NPC 1.75 ms")));
    TestEqual(TEXT("AI tier line"), NpcStatsTextLine(lines, 2),
        FString(TEXT("NPCs 1,000,000  far 900,000  mid 96,000  full 4,000")));
    TestEqual(TEXT("render tier line"), NpcStatsTextLine(lines, 3),
        FString(TEXT("render  point 980,000  instanced 19,968  mesh 32")));
    TestEqual(TEXT("awake line"), NpcStatsTextLine(lines, 4), FString(TEXT("awake 1,234  tier changes 56")));
    TestEqual(TEXT("combat line"), NpcStatsTextLine(lines, 5),
        FString(TEXT("combat hybrid  alive 1,873/2,000  bolts 12,345 (678 real)")));
    TestEqual(TEXT("memory line"), NpcStatsTextLine(lines, 6), FString(TEXT("memory 4,322 MB")));
    TestEqual(TEXT("ramp line"), NpcStatsTextLine(lines, 7),
        FString(TEXT("ramp stage 3/5  250,000 ships  measuring 3/10 s  last 100,000 ships p95 12.34 ms PASS")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextOverlayDefaultTest, "SOLTest.NpcStatsText.OverlayDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A default-constructed input prints zeros, bolts combat and an idle ramp
bool FNpcStatsTextOverlayDefaultTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> lines = SOLNpcStatsText::FormatOverlayLines(SOLNpcStatsText::FOverlayInput());
    TestEqual(TEXT("eight lines"), lines.Num(), NPCSTATSTEXT_LINE_COUNT);
    TestEqual(TEXT("frame line"), NpcStatsTextLine(lines, 0), FString(TEXT("FPS 0  frame 0.00 ms  p95 0.00 ms")));
    TestEqual(TEXT("thread line"), NpcStatsTextLine(lines, 1),
        FString(TEXT("game 0.00  render 0.00  GPU 0.00  NPC 0.00 ms")));
    TestEqual(TEXT("AI tier line"), NpcStatsTextLine(lines, 2), FString(TEXT("NPCs 0  far 0  mid 0  full 0")));
    TestEqual(TEXT("render tier line"), NpcStatsTextLine(lines, 3),
        FString(TEXT("render  point 0  instanced 0  mesh 0")));
    TestEqual(TEXT("awake line"), NpcStatsTextLine(lines, 4), FString(TEXT("awake 0  tier changes 0")));
    TestEqual(TEXT("combat line"), NpcStatsTextLine(lines, 5),
        FString(TEXT("combat bolts  alive 0/0  bolts 0 (0 real)")));
    TestEqual(TEXT("memory line"), NpcStatsTextLine(lines, 6), FString(TEXT("memory 0 MB")));
    TestEqual(TEXT("ramp line"), NpcStatsTextLine(lines, 7), FString(TEXT("ramp idle")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextOverlayFpsTest, "SOLTest.NpcStatsText.OverlayFps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FPS is 1000 / frame ms for a positive frame time and 0 for a zero, negative or non-finite one
bool FNpcStatsTextOverlayFpsTest::RunTest(const FString& /*parameters*/)
{
    SOLNpcStatsText::FOverlayInput in;
    in.FrameP95Ms = 2.0;

    in.FrameMs = 0.5;
    TestEqual(TEXT("0.5 ms"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 0),
        FString(TEXT("FPS 2000  frame 0.50 ms  p95 2.00 ms")));
    in.FrameMs = 20.0;
    TestEqual(TEXT("20 ms"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 0),
        FString(TEXT("FPS 50  frame 20.00 ms  p95 2.00 ms")));
    in.FrameMs = 0.0;
    TestEqual(TEXT("zero frame time"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 0),
        FString(TEXT("FPS 0  frame 0.00 ms  p95 2.00 ms")));
    in.FrameMs = -5.0;
    TestEqual(TEXT("negative frame time"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 0),
        FString(TEXT("FPS 0  frame -5.00 ms  p95 2.00 ms")));
    in.FrameMs = NpcStatsTextNaN();
    TestEqual(TEXT("NaN frame time"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 0),
        FString(TEXT("FPS 0  frame 0.00 ms  p95 2.00 ms")));
    in.FrameMs = NpcStatsTextInf();
    TestEqual(TEXT("infinite frame time"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 0),
        FString(TEXT("FPS 0  frame 0.00 ms  p95 2.00 ms")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextOverlayNonFiniteTest, "SOLTest.NpcStatsText.OverlayNonFinite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every non-finite double input prints as 0, and the overlay still has eight lines
bool FNpcStatsTextOverlayNonFiniteTest::RunTest(const FString& /*parameters*/)
{
    SOLNpcStatsText::FOverlayInput in = NpcStatsTextFullInput();
    in.FrameMs = 10.0;
    in.FrameP95Ms = -NpcStatsTextInf();
    in.GameMs = NpcStatsTextInf();
    in.RenderMs = NpcStatsTextNaN();
    in.GpuMs = NpcStatsTextNaN();
    in.NpcMs = -NpcStatsTextInf();
    in.UsedMemoryMB = NpcStatsTextInf();
    in.RampPhase = SOLNpcScale::ERampPhase::Settling;
    in.RampStageIndex = 0;
    in.RampStageCount = 3;
    in.RampStageNpcCount = 1000;
    in.RampPhaseElapsedS = NpcStatsTextNaN();
    in.RampPhaseLengthS = NpcStatsTextInf();
    in.RampResult.Empty();

    const TArray<FString> lines = SOLNpcStatsText::FormatOverlayLines(in);
    TestEqual(TEXT("eight lines"), lines.Num(), NPCSTATSTEXT_LINE_COUNT);
    TestEqual(TEXT("-inf p95 prints 0"), NpcStatsTextLine(lines, 0),
        FString(TEXT("FPS 100  frame 10.00 ms  p95 0.00 ms")));
    TestEqual(TEXT("non-finite thread times print 0"), NpcStatsTextLine(lines, 1),
        FString(TEXT("game 0.00  render 0.00  GPU 0.00  NPC 0.00 ms")));
    TestEqual(TEXT("infinite memory prints 0"), NpcStatsTextLine(lines, 6), FString(TEXT("memory 0 MB")));
    TestEqual(TEXT("non-finite ramp times print 0"), NpcStatsTextLine(lines, 7),
        FString(TEXT("ramp stage 1/3  1,000 ships  settling 0/0 s")));

    // A single NaN GPU time on otherwise valid input
    SOLNpcStatsText::FOverlayInput gpuOnly = NpcStatsTextFullInput();
    gpuOnly.GpuMs = NpcStatsTextNaN();
    TestEqual(TEXT("NaN GPU prints 0.00"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(gpuOnly), 1),
        FString(TEXT("game 3.25  render 4.50  GPU 0.00  NPC 1.75 ms")));
    gpuOnly.UsedMemoryMB = NpcStatsTextNaN();
    TestEqual(TEXT("NaN memory prints 0"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(gpuOnly), 6),
        FString(TEXT("memory 0 MB")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextOverlayCombatTest, "SOLTest.NpcStatsText.OverlayCombat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The combat line reads "combat off" when combat is off, and names the model otherwise (unknown models as bolts)
bool FNpcStatsTextOverlayCombatTest::RunTest(const FString& /*parameters*/)
{
    SOLNpcStatsText::FOverlayInput in = NpcStatsTextFullInput();
    in.bCombat = false;
    const TArray<FString> off = SOLNpcStatsText::FormatOverlayLines(in);
    TestEqual(TEXT("eight lines with combat off"), off.Num(), NPCSTATSTEXT_LINE_COUNT);
    TestEqual(TEXT("combat off"), NpcStatsTextLine(off, 5), FString(TEXT("combat off")));
    TestEqual(TEXT("memory still on line 7"), NpcStatsTextLine(off, 6), FString(TEXT("memory 4,322 MB")));

    in.bCombat = true;
    in.CombatModel = 1;
    TestEqual(TEXT("statistical"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 5),
        FString(TEXT("combat statistical  alive 1,873/2,000  bolts 12,345 (678 real)")));
    in.CombatModel = 0;
    TestEqual(TEXT("bolts"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 5),
        FString(TEXT("combat bolts  alive 1,873/2,000  bolts 12,345 (678 real)")));
    in.CombatModel = 9;
    in.Alive = 0;
    in.CombatShips = 1000000;
    in.LiveBolts = 65536;
    in.RealBolts = 8192;
    TestEqual(TEXT("unknown model reads as bolts"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 5),
        FString(TEXT("combat bolts  alive 0/1,000,000  bolts 65,536 (8,192 real)")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextOverlayMemoryTest, "SOLTest.NpcStatsText.OverlayMemory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Used memory rounds to the nearest whole MB and is grouped
bool FNpcStatsTextOverlayMemoryTest::RunTest(const FString& /*parameters*/)
{
    SOLNpcStatsText::FOverlayInput in;
    in.UsedMemoryMB = 1234.6;
    TestEqual(TEXT("1234.6 rounds up"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 6),
        FString(TEXT("memory 1,235 MB")));
    in.UsedMemoryMB = 1234.4;
    TestEqual(TEXT("1234.4 rounds down"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 6),
        FString(TEXT("memory 1,234 MB")));
    in.UsedMemoryMB = 999.0;
    TestEqual(TEXT("999"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 6),
        FString(TEXT("memory 999 MB")));
    in.UsedMemoryMB = 2000000.2;
    TestEqual(TEXT("two million"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 6),
        FString(TEXT("memory 2,000,000 MB")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcStatsTextOverlayRampTest, "SOLTest.NpcStatsText.OverlayRamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ramp line per phase, with stages numbered from 1 and "  last <result>" appended in every phase when set
bool FNpcStatsTextOverlayRampTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcScale::ERampPhase;
    SOLNpcStatsText::FOverlayInput in;
    in.RampStageIndex = 0;
    in.RampStageCount = 5;
    in.RampStageNpcCount = 10000;
    in.RampPhaseElapsedS = 1.6;
    in.RampPhaseLengthS = 5.0;
    const FString result = TEXT("10,000 ships p95 4.20 ms PASS");

    // Without a previous result
    in.RampPhase = ERampPhase::Idle;
    TestEqual(TEXT("idle"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 7), FString(TEXT("ramp idle")));
    in.RampPhase = ERampPhase::Settling;
    TestEqual(TEXT("settling"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 7),
        FString(TEXT("ramp stage 1/5  10,000 ships  settling 2/5 s")));
    in.RampPhase = ERampPhase::Measuring;
    in.RampStageIndex = 4;
    in.RampStageNpcCount = 1000000;
    in.RampPhaseElapsedS = 7.2;
    in.RampPhaseLengthS = 10.0;
    TestEqual(TEXT("measuring the last stage"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 7),
        FString(TEXT("ramp stage 5/5  1,000,000 ships  measuring 7/10 s")));
    in.RampPhase = ERampPhase::Done;
    TestEqual(TEXT("done"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 7), FString(TEXT("ramp done")));

    // With a previous result, every phase appends it
    in.RampResult = result;
    in.RampPhase = ERampPhase::Idle;
    TestEqual(TEXT("idle with result"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 7),
        FString(TEXT("ramp idle  last 10,000 ships p95 4.20 ms PASS")));
    in.RampPhase = ERampPhase::Settling;
    in.RampStageIndex = 1;
    in.RampStageNpcCount = 100000;
    in.RampPhaseElapsedS = 0.0;
    in.RampPhaseLengthS = 5.0;
    TestEqual(TEXT("settling with result"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 7),
        FString(TEXT("ramp stage 2/5  100,000 ships  settling 0/5 s  last 10,000 ships p95 4.20 ms PASS")));
    in.RampPhase = ERampPhase::Measuring;
    in.RampPhaseElapsedS = 9.9;
    in.RampPhaseLengthS = 10.0;
    TestEqual(TEXT("measuring with result"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 7),
        FString(TEXT("ramp stage 2/5  100,000 ships  measuring 10/10 s  last 10,000 ships p95 4.20 ms PASS")));
    in.RampPhase = ERampPhase::Done;
    in.RampResult = TEXT("1,000,000 ships p95 31.50 ms FAIL");
    TestEqual(TEXT("done with result"), NpcStatsTextLine(SOLNpcStatsText::FormatOverlayLines(in), 7),
        FString(TEXT("ramp done  last 1,000,000 ships p95 31.50 ms FAIL")));

    // Every phase keeps eight lines
    const ERampPhase phases[] = { ERampPhase::Idle, ERampPhase::Settling, ERampPhase::Measuring, ERampPhase::Done };
    for (const ERampPhase phase : phases)
    {
        in.RampPhase = phase;
        TestEqual(TEXT("eight lines in every phase"), SOLNpcStatsText::FormatOverlayLines(in).Num(),
            NPCSTATSTEXT_LINE_COUNT);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
