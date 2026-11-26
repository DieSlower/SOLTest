/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcLaneMath.h"
#include "Npc/SOLNpcTrafficMirror.h"
#include "Npc/SOLNpcTunnelComponent.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for issue #53 part 53d (tunnel classes and pool), written test-first against SDD 53 sections 5.4, 7.1-7.3 and
// 9 and the resolved decisions Q9, Q17-Q22 and P5, before the production code existed: the knee fix (160 ring slots
// per side, 15,380 vertices per Full lane), the Reduced mesh and the 24-instance pool shared by both classes with its
// overflow report, the per-lane class by projected pixel radius with the live focal and the quality scale, the 3 s
// real-time open fade, the legacy lane controls restated in reference pixels (Q20) and the inactive-lane hide of the
// point vertex shader (always, with the Lanes overlay on or off). The pure class and pool rows of contract cases 10,
// 11, 14 and 23 are in LaneNetworkMathTest.cpp and LaneNetworkTrafficTest.cpp and are not repeated here.
//
// Not test-first: the tests after InactiveHidden (ColumnOpenFades, SlotHoldPreemption, PoolHysteresis,
// InstanceRewrite, LaneTunnelRadius, ComponentMeshes) were added in the 53d review fixes by the implementer of those
// fixes, written after the code they cover and with it in view, not by a spec-only author. The class threshold rows
// above were re-pinned at the same time to the user's 53d review decision (Full on at 10/12 px, off below 0.75 px).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double LTC_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double LTC_TIGHT = 1.0e-12;            // Relative tolerance of exact formulas
    constexpr double LTC_REF_FOCAL_PX = 960.0;       // A 16:9 view at 90 degrees, in 1080p-reference px (literal)
    constexpr double LTC_TUNNEL_RADIUS_M = 1.0e7;    // Tunnel radius of the class cases
    constexpr double LTC_LANE_RADIUS_M = 1.0e6;      // Lane radius of the legacy-control cases
    constexpr double LTC_FRAME_S = 1.0 / 60.0;       // One real frame at 60 Hz

    //////////////////////////////////////////////////////////////////////////
    // Checks actual against expected within a relative tolerance (absolute when expected is 0); NaN never matches
    bool LtcNear(FAutomationTestBase& test, const FString& what, const double actual, const double expected,
        const double relTol)
    {
        const double tol = (expected == 0.0) ? relTol : relTol * FMath::Abs(expected);
        if (FMath::Abs(actual - expected) <= tol)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected %.15g, got %.15g"), *what, expected, actual));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a tunnel class's name for the messages
    const TCHAR* LtcClassName(const ESOLTunnelClass tunnelClass)
    {
        switch (tunnelClass)
        {
        case ESOLTunnelClass::Off:
            return TEXT("Off");
        case ESOLTunnelClass::Reduced:
            return TEXT("Reduced");
        case ESOLTunnelClass::Full:
            return TEXT("Full");
        default:
            return TEXT("?");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a tunnel class against the expected one
    bool LtcClassIs(FAutomationTestBase& test, const FString& what, const ESOLTunnelClass actual,
        const ESOLTunnelClass expected)
    {
        if (actual == expected)
        {
            return true;
        }
        test.AddError(FString::Printf(TEXT("%s: expected %s, got %s"), *what, LtcClassName(expected),
            LtcClassName(actual)));
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the class a lane parallel to x, y off the camera at the origin, steps to from previous (the per-lane
    // path: segment distance, largest on-screen radius at the live focal, StepTunnelClass with the quality scale)
    ESOLTunnelClass LtcLaneClass(const ESOLTunnelClass previous, const double y, const double tunnelRadiusM,
        const double focalRefPx, const double scale)
    {
        const double segmentM = SOLNpcTunnel::LaneSegmentDistanceM(FVector3d(-1.0e11, y, 0.0), FVector3d(1.0e11, y,
            0.0));
        return SOLLaneNetwork::StepTunnelClass(previous,
            SOLLaneNetwork::LaneTunnelMaxRadiusPx(segmentM, tunnelRadiusM, focalRefPx), scale);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the restated lane-ship point fade in px of the lane radius r (Q20): 0 from 9.6 px, 1 to 2.4 px,
    // log-linear
    double LtcLineFadePx(const double radiusPx)
    {
        if (radiusPx >= 9.6)
        {
            return 0.0;
        }
        if (radiusPx <= 2.4)
        {
            return 1.0;
        }
        return std::log(9.6 / radiusPx) / std::log(9.6 / 2.4);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the restated label strength in px of the lane radius r (Q20): 0 to 9.6 px, 1 from 19.2 px, log-linear
    double LtcLabelFadePx(const double radiusPx)
    {
        if (radiusPx <= 9.6)
        {
            return 0.0;
        }
        if (radiusPx >= 19.2)
        {
            return 1.0;
        }
        return std::log(radiusPx / 9.6) / std::log(19.2 / 9.6);
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets count texels (a group texture column) to zero
    void LtcZeroRows(FVector4f* rows, const int32 count)
    {
        for (int32 row = 0; row < count; ++row)
        {
            rows[row] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassRingWindowKneeTest, "SOLTest.LaneTunnelClass.RingWindowKnee",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Knee fix (Q19, SDD 53 7.1): 160 ring slots per side, so the ring window reaches the 0.5 px gone point of the radius
// fade instead of stopping at the 1 px knee (where the last ring was still at the knee alpha and popped)
bool FSOLLaneTunnelClassRingWindowKneeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    TestEqual(TEXT("160 ring slots per side"), perSide, 160);
    TestEqual(TEXT("320 ring slots"), RingSlotCount(), 320);

    // The window, seen along the axis at the reference focal, reaches f / gone px tunnel radii (1,920 = 1,920)
    const double windowRadii = static_cast<double>(perSide) * SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII;
    TestTrue(TEXT("window x S0 >= reference focal / radius-fade gone px"),
        windowRadii >= SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX / SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX);

    // The window's farthest ring is 0.5 px across at the reference focal and fully gone, not at the knee
    const double rt = LTC_TUNNEL_RADIUS_M;
    const double farthestPx = RingRadiusPx(windowRadii * rt, rt, LTC_REF_FOCAL_PX);
    LtcNear(*this, TEXT("farthest ring radius 0.5 px"), farthestPx, SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX, LTC_TIGHT);
    TestEqual(TEXT("farthest ring alpha 0 (no pop at the window's end)"), RingRadiusFade(farthestPx), 0.0);

    // The added slots draw at the farthest segment count, ahead and behind; the last behind slot exists
    TestEqual(TEXT("slot 159 (last ahead): farthest segments"), RingSegmentsForSlot(perSide - 1),
        SOL::NPC_TUNNEL_RING_SEGMENTS_FARTHEST);
    TestEqual(TEXT("slot 319 (last behind): farthest segments"), RingSegmentsForSlot(319),
        SOL::NPC_TUNNEL_RING_SEGMENTS_FARTHEST);
    TestEqual(TEXT("slot 320: out of range"), RingSegmentsForSlot(320), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassVertexBudgetTest, "SOLTest.LaneTunnelClass.VertexBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Vertex budget (Q18, Q19, SDD 53 7.1, 7.3, 9): 15,380 vertices per Full lane; the Reduced mesh is the ring ribbons and
// the two end rings only; the fixed pool of 24 instances shared by both classes is at most 24 x 15,380 = 369,120
// vertices for any mix of Reduced and Full
bool FSOLLaneTunnelClassVertexBudgetTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const int32 full = TunnelSlotMeshVertexCount();
    const int32 reduced = TunnelReducedMeshVertexCount();
    TestEqual(TEXT("Full lane: 15,380 vertices"), full, 15380);

    // The Reduced mesh: ring ribbons ((N + 1) samples x 2 sides per slot) and two end rings at the nearest count
    int32 ringVertices = 0;
    for (int32 slot = 0; slot < RingSlotCount(); ++slot)
    {
        ringVertices += 2 * (RingSegmentsForSlot(slot) + 1);
    }
    const int32 endRingVertices = 2 * 2 * (SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST + 1);
    const int32 detailVertices = 4 * DetailSlotCount() * SOL::NPC_TUNNEL_DOTS_PER_GAP * SOL::NPC_TUNNEL_DOT_ROWS
        + 4 * DetailSlotCount() * SOL::NPC_TUNNEL_CHEVRONS_PER_GAP * 2;
    TestEqual(TEXT("Reduced: ring ribbons and end rings"), reduced, ringVertices + endRingVertices);
    TestEqual(TEXT("Reduced: 7,220 vertices"), reduced, 7220);
    TestEqual(TEXT("Full - Reduced: the dot and chevron quads"), full - reduced, detailVertices);
    TestTrue(TEXT("0 < Reduced < Full"), reduced > 0 && reduced < full);

    // The pool: 24 instances shared by both classes, within the group texture's columns
    const int32 pool = SOL::NPC_TUNNEL_INSTANCE_POOL;
    TestEqual(TEXT("pool of 24"), pool, 24);
    TestTrue(TEXT("pool <= group texture width"), pool <= SOL::NPC_GROUP_TEXTURE_WIDTH);
    const int64 worstVertices = static_cast<int64>(pool) * full;
    TestEqual(TEXT("worst case 24 x 15,380 = 369,120"), worstVertices, static_cast<int64>(369120));

    // Every mix of Full and Reduced instances fits the all-Full bound
    int32 overBudget = 0;
    for (int32 fullCount = 0; fullCount <= pool; ++fullCount)
    {
        const int64 mix = static_cast<int64>(fullCount) * full + static_cast<int64>(pool - fullCount) * reduced;
        overBudget += mix <= worstVertices ? 0 : 1;
    }
    TestEqual(TEXT("every Reduced / Full mix within 369,120"), overBudget, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassPixelSelectionTest, "SOLTest.LaneTunnelClass.PixelSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Per-lane class by projected pixel radius (Q17, SDD 53 7.1): r_max = f_live Rt / d_seg, the live view's focal in
// reference px (the same at every resolution of one aspect, larger when zoomed), stepped with hysteresis and every
// threshold scaled by the QualityPreset (Low x10, Medium x4.6, High x2.2, Epic x1); the class follows the pixels,
// not the distance
bool FSOLLaneTunnelClassPixelSelectionTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::LaneTunnelQualityScale;
    using SOLNpcTunnel::FocalLengthRefPx;
    const ESOLTunnelClass off = ESOLTunnelClass::Off;
    const ESOLTunnelClass reduced = ESOLTunnelClass::Reduced;
    const ESOLTunnelClass full = ESOLTunnelClass::Full;
    const double rt = LTC_TUNNEL_RADIUS_M;

    // Three lanes at 2.0, 0.7 and 0.3 px at the reference focal
    const double y[3] = { LTC_REF_FOCAL_PX * rt / 2.0, LTC_REF_FOCAL_PX * rt / 0.7, LTC_REF_FOCAL_PX * rt / 0.3 };

    // The live focal: 90 degrees is 960 reference px at 720p, 1080p and 4K, 45 degrees (zoomed) about 2,317.6
    const double focal720 = FocalLengthRefPx(90.0, 1280.0, 720.0);
    const double focal1080 = FocalLengthRefPx(90.0, 1920.0, 1080.0);
    const double focal4k = FocalLengthRefPx(90.0, 3840.0, 2160.0);
    const double focalZoom = FocalLengthRefPx(45.0, 1920.0, 1080.0);
    LtcNear(*this, TEXT("focal 1080p"), focal1080, LTC_REF_FOCAL_PX, 1.0e-9);
    LtcNear(*this, TEXT("focal 720p"), focal720, focal1080, 1.0e-12);
    LtcNear(*this, TEXT("focal 4K"), focal4k, focal1080, 1.0e-12);
    LtcNear(*this, TEXT("focal 45 degrees"), focalZoom, LTC_REF_FOCAL_PX / std::tan(FMath::DegreesToRadians(22.5)),
        1.0e-9);

    // From Off, per preset (0 Low .. 3 Epic) and focal: (lane 2.0 px, lane 0.7 px, lane 0.3 px at 960)
    struct FRow
    {
        int32 Preset;
        bool bZoom;
        ESOLTunnelClass Expected[3];
    };
    const FRow rows[] = {
        { 3, false, { full, reduced, off } },       // Epic: 2.0 / 0.7 / 0.3 px
        { 3, true, { full, full, reduced } },       // Epic zoomed: 4.83 / 1.69 / 0.72 px
        { 2, false, { full, off, off } },           // High (on 1.1, full 1.83)
        { 2, true, { full, reduced, off } },
        { 1, false, { off, off, off } },            // Medium (on 2.3, full 3.83)
        { 1, true, { full, off, off } },
        { 0, false, { off, off, off } },            // Low (on 5, full 8.33)
        { 0, true, { off, off, off } },
    };
    for (const FRow& row : rows)
    {
        const double scale = LaneTunnelQualityScale(row.Preset);
        for (int32 lane = 0; lane < 3; ++lane)
        {
            const FString label = FString::Printf(TEXT("preset %d %s lane %d"), row.Preset,
                row.bZoom ? TEXT("45 deg") : TEXT("90 deg"), lane);
            if (row.bZoom)
            {
                LtcClassIs(*this, label, LtcLaneClass(off, y[lane], rt, focalZoom, scale), row.Expected[lane]);
            }
            else
            {
                // The same class at 720p, 1080p and 4K (resolution independent)
                LtcClassIs(*this, label + TEXT(" 720p"), LtcLaneClass(off, y[lane], rt, focal720, scale),
                    row.Expected[lane]);
                LtcClassIs(*this, label + TEXT(" 1080p"), LtcLaneClass(off, y[lane], rt, focal1080, scale),
                    row.Expected[lane]);
                LtcClassIs(*this, label + TEXT(" 4K"), LtcLaneClass(off, y[lane], rt, focal4k, scale),
                    row.Expected[lane]);
            }
        }
    }

    // Pixels, not distance: twice the tunnel radius at twice the distance is the same 2.0 px and the same class
    LtcClassIs(*this, TEXT("3 Rt at 3 d (2.0 px): Full"), LtcLaneClass(off, 3.0 * y[0], 3.0 * rt, LTC_REF_FOCAL_PX,
        1.0), full);
    LtcClassIs(*this, TEXT("Rt at 3 d (0.667 px): Reduced"), LtcLaneClass(off, 3.0 * y[0], rt, LTC_REF_FOCAL_PX, 1.0),
        reduced);

    // Flying away and back at Epic (hysteresis 0.4 / 0.5 / 0.75 / 0.833 px; samples off the boundaries)
    const double pxPath[] = { 2.0, 0.8, 0.7, 0.45, 0.35, 0.45, 0.52, 0.8, 0.86 };
    const ESOLTunnelClass expectedPath[] = { full, full, reduced, reduced, off, off, reduced, reduced, full };
    ESOLTunnelClass current = off;
    for (int32 step = 0; step < static_cast<int32>(UE_ARRAY_COUNT(pxPath)); ++step)
    {
        current = LtcLaneClass(current, LTC_REF_FOCAL_PX * rt / pxPath[step], rt, LTC_REF_FOCAL_PX, 1.0);
        LtcClassIs(*this, FString::Printf(TEXT("path step %d (%.2f px)"), step, pxPath[step]), current,
            expectedPath[step]);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassSharedPoolTest, "SOLTest.LaneTunnelClass.SharedPool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The 24-instance pool shared by Reduced and Full (Q18, SDD 53 7.3): Off lanes take no instance; the drawn lanes of
// both classes are ranked by r_max together and the smallest on screen lose their instance; the overflow is reported
// once per change (a fixed-pool limitation, never the LOD rule)
bool FSOLLaneTunnelClassSharedPoolTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::SelectTunnelPool;
    using SOLLaneNetwork::StepOverflowReport;
    using SOLLaneNetwork::StepTunnelClass;
    constexpr int32 laneCount = 30;
    const int32 pool = SOL::NPC_TUNNEL_INSTANCE_POOL;

    // Rank i (px strictly falling with i) on lane (7 i) mod 30: ranks 0-11 Full (5.0 .. 2.25 px), ranks 12-27 Reduced
    // (0.8 .. 0.6125 px, below the 0.833 px Full on), ranks 28-29 Off (0.3, 0.1 px)
    TArray<double> px;
    px.Init(0.0, laneCount);
    TArray<int32> laneOfRank;
    for (int32 rank = 0; rank < laneCount; ++rank)
    {
        const int32 lane = (7 * rank) % laneCount;
        laneOfRank.Add(lane);
        px[lane] = rank < 12 ? 5.0 - 0.25 * rank : (rank < 28 ? 0.8 - 0.0125 * (rank - 12) : (rank == 28 ? 0.3 : 0.1));
    }

    // One frame: step every lane's class from its previous one, rank the drawn ones into the pool
    TArray<ESOLTunnelClass> classes;
    classes.Init(ESOLTunnelClass::Off, laneCount);
    TArray<uint8> drawn;
    TArray<int32> instanceLanes;
    auto stepFrame = [&]() -> int32
    {
        drawn.Init(0, laneCount);
        for (int32 lane = 0; lane < laneCount; ++lane)
        {
            classes[lane] = StepTunnelClass(classes[lane], px[lane], 1.0);
            drawn[lane] = classes[lane] == ESOLTunnelClass::Off ? 0 : 1;
        }
        return SelectTunnelPool(px, drawn, pool, instanceLanes);
    };

    // Frame 1: 12 Full + 16 Reduced drawn, 2 Off: 24 instances, 4 Reduced lanes left out, overflow 4 reported
    const int32 overflow1 = stepFrame();
    TestEqual(TEXT("frame 1: overflow 4"), overflow1, 4);
    TestEqual(TEXT("frame 1: 24 instances"), instanceLanes.Num(), pool);
    int32 fullInstances = 0;
    int32 reducedInstances = 0;
    int32 wrongOrder = 0;
    for (int32 index = 0; index < instanceLanes.Num(); ++index)
    {
        const int32 lane = instanceLanes[index];
        fullInstances += classes[lane] == ESOLTunnelClass::Full ? 1 : 0;
        reducedInstances += classes[lane] == ESOLTunnelClass::Reduced ? 1 : 0;
        wrongOrder += lane == laneOfRank[index] ? 0 : 1;
    }
    TestEqual(TEXT("frame 1: 12 Full instances"), fullInstances, 12);
    TestEqual(TEXT("frame 1: 12 Reduced instances"), reducedInstances, 12);
    TestEqual(TEXT("frame 1: largest on screen first, both classes in one ranking"), wrongOrder, 0);
    for (const int32 rank : { 28, 29 })
    {
        TestFalse(FString::Printf(TEXT("frame 1: Off lane %d has no instance"), laneOfRank[rank]),
            instanceLanes.Contains(laneOfRank[rank]));
    }
    for (int32 rank = 24; rank < 28; ++rank)
    {
        TestFalse(FString::Printf(TEXT("frame 1: smallest Reduced lane %d lost its instance"), laneOfRank[rank]),
            instanceLanes.Contains(laneOfRank[rank]));
    }
    const int64 frameVertices = static_cast<int64>(fullInstances) * SOLNpcTunnel::TunnelSlotMeshVertexCount()
        + static_cast<int64>(reducedInstances) * SOLNpcTunnel::TunnelReducedMeshVertexCount();
    TestTrue(TEXT("frame 1: within the pool's vertex bound"),
        frameVertices <= static_cast<int64>(pool) * SOLNpcTunnel::TunnelSlotMeshVertexCount());
    TestTrue(TEXT("frame 1: overflow reported"), StepOverflowReport(0, overflow1));

    // Frame 2, nothing moved: the same overflow is not reported again
    const int32 overflow2 = stepFrame();
    TestEqual(TEXT("frame 2: overflow 4"), overflow2, 4);
    TestFalse(TEXT("frame 2: not reported again"), StepOverflowReport(overflow1, overflow2));

    // Frame 3: ranks 26 and 27 fall to 0.3 px (Reduced -> Off below 0.4): overflow 2, reported
    px[laneOfRank[26]] = 0.3;
    px[laneOfRank[27]] = 0.3;
    const int32 overflow3 = stepFrame();
    TestEqual(TEXT("frame 3: overflow 2"), overflow3, 2);
    TestTrue(TEXT("frame 3: change reported"), StepOverflowReport(overflow2, overflow3));
    TestTrue(TEXT("frame 3: rank 26 Off"), classes[laneOfRank[26]] == ESOLTunnelClass::Off);

    // Frame 4: ranks 20-25 also fall to 0.3 px: 20 drawn, no overflow, the clearing reported
    for (int32 rank = 20; rank < 26; ++rank)
    {
        px[laneOfRank[rank]] = 0.3;
    }
    const int32 overflow4 = stepFrame();
    TestEqual(TEXT("frame 4: overflow 0"), overflow4, 0);
    TestEqual(TEXT("frame 4: 20 instances"), instanceLanes.Num(), 20);
    TestTrue(TEXT("frame 4: clearing reported"), StepOverflowReport(overflow3, overflow4));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassOpenFadeTest, "SOLTest.LaneTunnelClass.OpenFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Tunnel open fade (Q9, SDD 53 5.4, 9): only the tunnel fades, 1 -> 0 over 3 s of real time at a close and 0 -> 1 at
// a reopen (real seconds are the only time input, so warp does not shorten it), never outside [0, 1]; it reaches 0
// exactly so the column can be freed after it; written as row 3 .z (open fade x draw flag). The curve between the
// ends and a reversal mid-fade are not pinned here
bool FSOLLaneTunnelClassOpenFadeTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::StepLaneOpenFade;
    const double fadeS = SOL::NPC_LANE_OPEN_FADE_REAL_S;
    TestEqual(TEXT("open fade 3 s real"), fadeS, 3.0);

    // A close at 60 Hz: falling, still visible at 2.98 s, gone by 3 s, exactly 0 one frame later
    {
        double fade = 1.0;
        int32 rising = 0;
        int32 outside = 0;
        double fadeAtHalf = LTC_NAN;
        double fadeBeforeEnd = LTC_NAN;
        for (int32 frame = 1; frame <= 180; ++frame)
        {
            const double next = StepLaneOpenFade(fade, false, LTC_FRAME_S);
            rising += next > fade ? 1 : 0;
            outside += next >= 0.0 && next <= 1.0 ? 0 : 1;
            fade = next;
            fadeAtHalf = frame == 90 ? fade : fadeAtHalf;
            fadeBeforeEnd = frame == 179 ? fade : fadeBeforeEnd;
        }
        TestEqual(TEXT("close: never rising"), rising, 0);
        TestEqual(TEXT("close: never outside [0, 1]"), outside, 0);
        TestTrue(TEXT("close: between 0 and 1 at 1.5 s"), fadeAtHalf > 0.0 && fadeAtHalf < 1.0);
        TestTrue(TEXT("close: still above 0 at 2.98 s"), fadeBeforeEnd > 0.0);
        TestTrue(TEXT("close: gone at 3 s"), fade <= 1.0e-9);
        TestTrue(TEXT("close: exactly 0 a frame after"), StepLaneOpenFade(fade, false, LTC_FRAME_S) == 0.0);
    }

    // A reopen at 60 Hz: rising, below 1 at 2.98 s, full by 3 s, exactly 1 one frame later
    {
        double fade = 0.0;
        int32 falling = 0;
        double fadeBeforeEnd = LTC_NAN;
        for (int32 frame = 1; frame <= 180; ++frame)
        {
            const double next = StepLaneOpenFade(fade, true, LTC_FRAME_S);
            falling += next < fade ? 1 : 0;
            fade = next;
            fadeBeforeEnd = frame == 179 ? fade : fadeBeforeEnd;
        }
        TestEqual(TEXT("reopen: never falling"), falling, 0);
        TestTrue(TEXT("reopen: below 1 at 2.98 s"), fadeBeforeEnd < 1.0);
        TestTrue(TEXT("reopen: full at 3 s"), fade >= 1.0 - 1.0e-9);
        TestTrue(TEXT("reopen: exactly 1 a frame after"), StepLaneOpenFade(fade, true, LTC_FRAME_S) == 1.0);
    }

    // Whole and long steps, steady states, a zero step
    TestTrue(TEXT("one 3 s step closed: exactly 0"), StepLaneOpenFade(1.0, false, fadeS) == 0.0);
    TestTrue(TEXT("one 3 s step open: exactly 1"), StepLaneOpenFade(0.0, true, fadeS) == 1.0);
    const double halfClosed = StepLaneOpenFade(1.0, false, 0.5 * fadeS);
    TestTrue(TEXT("1.5 s step closed: between"), halfClosed > 0.0 && halfClosed < 1.0);
    TestTrue(TEXT("10 s step closed: exactly 0, not below"), StepLaneOpenFade(1.0, false, 10.0) == 0.0);
    TestTrue(TEXT("10 s step open: exactly 1, not above"), StepLaneOpenFade(0.0, true, 10.0) == 1.0);
    TestTrue(TEXT("open and full stays exactly 1"), StepLaneOpenFade(1.0, true, LTC_FRAME_S) == 1.0);
    TestTrue(TEXT("closed and gone stays exactly 0"), StepLaneOpenFade(0.0, false, LTC_FRAME_S) == 0.0);
    TestTrue(TEXT("zero step closed: unchanged"), StepLaneOpenFade(0.4, false, 0.0) == 0.4);
    TestTrue(TEXT("zero step open: unchanged"), StepLaneOpenFade(0.4, true, 0.0) == 0.4);

    // A bad step never yields NaN or a value outside [0, 1]
    for (const double badStep : { LTC_NAN, -1.0 })
    {
        for (const bool bOpen : { false, true })
        {
            const double fade = StepLaneOpenFade(0.4, bOpen, badStep);
            TestTrue(FString::Printf(TEXT("bad step %f, open %d: in [0, 1]"), badStep, bOpen ? 1 : 0),
                fade >= 0.0 && fade <= 1.0);
        }
    }

    // Row 3 .z carries the open fade times the draw flag
    TArray<FVector4f> texels;
    texels.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), 4 * SOL::NPC_GROUP_TEXTURE_WIDTH);
    FSOLNpcTunnelLaneData lane;
    lane.Lane = 5;
    lane.TunnelRadiusM = LTC_TUNNEL_RADIUS_M;
    lane.DrawFade = SOLNpcTunnel::TunnelDrawFade(true) * 0.25;
    SOLNpcTunnel::WriteTunnelTexels(TArrayView<const FSOLNpcTunnelLaneData>(&lane, 1), texels);
    TestEqual(TEXT("row 3 .z: shown, fade 0.25"), SOLNpcTunnel::ReadTunnelDrawFadeTexel(texels, 5), 0.25);
    lane.DrawFade = SOLNpcTunnel::TunnelDrawFade(false) * 0.25;
    SOLNpcTunnel::WriteTunnelTexels(TArrayView<const FSOLNpcTunnelLaneData>(&lane, 1), texels);
    TestEqual(TEXT("row 3 .z: hidden, 0"), SOLNpcTunnel::ReadTunnelDrawFadeTexel(texels, 5), 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassLegacyControlsPxTest, "SOLTest.LaneTunnelClass.LegacyControlsPx",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Legacy controls restated in pixels (Q20, SDD 53 7.2): the lane-ship point fade (100 / 400 lane radii) is 9.6 / 2.4
// reference px of the lane radius and the label band (100 -> 50 lane radii) is 9.6 -> 19.2 px, both with the live
// focal: the same look as today at 960 px, shifted with the field of view, the same at every resolution
bool FSOLLaneTunnelClassLegacyControlsPxTest::RunTest(const FString& /*parameters*/)
{
    const double r = LTC_LANE_RADIUS_M;

    // At the reference focal both equal today's lane-radii forms
    for (const double d : { 5.0e7, 1.2e8, 1.5e8, 2.5e8, 3.5e8, 6.0e8 })
    {
        LtcNear(*this, FString::Printf(TEXT("line fade at 960, d %.3g"), d),
            SOLNpcLane::LaneLineFade(d, r, LTC_REF_FOCAL_PX), SOLNpcLane::LaneLineFade(d, r), LTC_TIGHT);
    }
    for (const double d : { 3.0e7, 4.5e7, 6.0e7, 8.0e7, 1.2e8 })
    {
        LtcNear(*this, FString::Printf(TEXT("label strength at 960, d %.3g"), d),
            SOLNpcTunnel::TunnelFade(d, r, LTC_REF_FOCAL_PX), SOLNpcTunnel::TunnelFade(d, r), LTC_TIGHT);
    }

    // The pixel forms at several focals over 10 .. 1,000 lane radii
    int32 lineMisses = 0;
    int32 labelMisses = 0;
    for (const double focal : { 480.0, 960.0, 1440.0, 1920.0, 2317.6 })
    {
        for (int32 k = 8; k <= 24; ++k)
        {
            const double d = r * std::pow(10.0, k / 8.0);
            const double radiusPx = focal * r / d;
            lineMisses += FMath::Abs(SOLNpcLane::LaneLineFade(d, r, focal) - LtcLineFadePx(radiusPx)) <= 1.0e-9 ? 0 : 1;
            labelMisses += FMath::Abs(SOLNpcTunnel::TunnelFade(d, r, focal) - LtcLabelFadePx(radiusPx)) <= 1.0e-9
                ? 0 : 1;
        }
    }
    TestEqual(TEXT("line fade follows 9.6 / 2.4 px at every focal"), lineMisses, 0);
    TestEqual(TEXT("label strength follows 9.6 -> 19.2 px at every focal"), labelMisses, 0);

    // Off the reference focal the band moves with the pixels, not the lane radii
    LtcNear(*this, TEXT("line fade, f 1920, 400 radii (4.8 px): 0.5"), SOLNpcLane::LaneLineFade(4.0e8, r, 1920.0), 0.5,
        1.0e-12);
    LtcNear(*this, TEXT("line fade, f 480, 100 radii (4.8 px): 0.5"), SOLNpcLane::LaneLineFade(1.0e8, r, 480.0), 0.5,
        1.0e-12);
    LtcNear(*this, TEXT("label, f 1920, 100 radii (19.2 px): 1"), SOLNpcTunnel::TunnelFade(1.0e8, r, 1920.0), 1.0,
        1.0e-12);
    const double dHalfLabel = 480.0 * r / (9.6 * FMath::Sqrt(2.0));
    LtcNear(*this, TEXT("label, f 480, 35.4 radii (13.6 px): 0.5"), SOLNpcTunnel::TunnelFade(dHalfLabel, r, 480.0),
        0.5, 1.0e-12);

    // The camera on the lane: line gone, label full
    TestEqual(TEXT("d 0: line fade 0"), SOLNpcLane::LaneLineFade(0.0, r, 1920.0), 0.0);
    TestEqual(TEXT("d 0: label 1"), SOLNpcTunnel::TunnelFade(0.0, r, 1920.0), 1.0);

    // Resolution independent: 90 degrees at 720p, 1080p and 4K give the same fades
    const double focals[] = { SOLNpcTunnel::FocalLengthRefPx(90.0, 1280.0, 720.0),
        SOLNpcTunnel::FocalLengthRefPx(90.0, 1920.0, 1080.0), SOLNpcTunnel::FocalLengthRefPx(90.0, 3840.0, 2160.0) };
    for (const double focal : focals)
    {
        LtcNear(*this, FString::Printf(TEXT("line fade at focal %.6f"), focal),
            SOLNpcLane::LaneLineFade(2.0e8, r, focal), SOLNpcLane::LaneLineFade(2.0e8, r, focals[1]), 1.0e-9);
        LtcNear(*this, FString::Printf(TEXT("label at focal %.6f"), focal), SOLNpcTunnel::TunnelFade(7.0e7, r, focal),
            SOLNpcTunnel::TunnelFade(7.0e7, r, focals[1]), 1.0e-9);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassInactiveHiddenTest, "SOLTest.LaneTunnelClass.InactiveHidden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Inactive lanes hidden always (Q21, Q22, P5; plan 53d step 4): the point vertex shader's early-out reads the slot's
// active flag (row 6 .z) and skips an inactive lane's ships, with the Lanes overlay on or off, on top of the window
// test (LaneShipWindowDrawn itself still ignores the flag, contract case 25)
bool FSOLLaneTunnelClassInactiveHiddenTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTrafficMirror::LaneShipPointDrawn;
    using SOLNpcTrafficMirror::LaneShipWindowDrawn;
    FVector4f rows[SOL::NPC_GROUP_TEXTURE_ROWS];
    LtcZeroRows(rows, SOL::NPC_GROUP_TEXTURE_ROWS);

    // A ship without a leg (railB.w 0): drawn while the window [0, 100) s is open
    const FVector4f railA(0.0f, 0.0f, 0.0f, 0.0f);
    const FVector4f railB(0.0f, 0.0f, 0.0f, 0.0f);
    const FVector4f parkedB(0.0f, 0.0f, 0.0f, -1.0f);
    for (const bool bActive : { true, false })
    {
        rows[SOL::NPC_GROUP_WINDOW_ROW] = SOLLaneNetwork::LaneWindowTexel(true, 0.0, 100.0, 0.0, bActive);
        const FString tag = bActive ? TEXT("active") : TEXT("inactive");
        TestTrue(tag + TEXT(": window open at 50 s (flag not read by the window test)"),
            LaneShipWindowDrawn(railA, railB, rows, 50.0f));
        TestTrue(tag + TEXT(": point drawn at 50 s only while active"),
            LaneShipPointDrawn(railA, railB, rows, 50.0f) == bActive);
        TestFalse(tag + TEXT(": window closed at 150 s: hidden"), LaneShipPointDrawn(railA, railB, rows, 150.0f));
        TestFalse(tag + TEXT(": parked: hidden"), LaneShipPointDrawn(railA, parkedB, rows, 50.0f));
    }

    // End to end: a lane 3.0 px across at Epic is active and drawn; at High (5.28 px) it is inactive and hidden, though
    // with the Lanes overlay off the point crossfade alone would show every far point (alpha 1)
    const FVector3d a(0.0, 0.0, 0.0);
    const FVector3d b(1.0e10, 0.0, 0.0);
    const FVector3d camera(5.0e9, 6.4e8, 0.0);
    const double laneRadiusM = 2.0e6;
    const double radiusPx = SOLLaneNetwork::LaneTrafficRadiusPx(camera, a, b, laneRadiusM, LTC_REF_FOCAL_PX);
    LtcNear(*this, TEXT("lane radius 3.0 px"), radiusPx, 3.0, LTC_TIGHT);
    for (const int32 preset : { 3, 2 })
    {
        const bool bActive = SOLLaneNetwork::LaneTrafficActive(radiusPx, 0,
            SOLLaneNetwork::LaneTunnelQualityScale(preset));
        TestTrue(FString::Printf(TEXT("preset %d: active only at Epic"), preset), bActive == (preset == 3));
        rows[SOL::NPC_GROUP_WINDOW_ROW] = SOLLaneNetwork::LaneWindowTexel(true, 0.0, 100.0, 0.0, bActive);

        // The early-out takes no overlay input, so one answer serves the overlay on and off
        const bool bDrawn = LaneShipPointDrawn(railA, railB, rows, 50.0f);
        TestTrue(FString::Printf(TEXT("preset %d: point drawn only while active"), preset), bDrawn == bActive);
        const double overlayOffAlpha = SOLNpcLane::LaneShipPointAlpha(1.0e9, laneRadiusM, false, 0.0);
        TestEqual(TEXT("overlay off: the crossfade alone would show the point"), overlayOffAlpha, 1.0);
        TestTrue(FString::Printf(TEXT("preset %d, overlay off: visible only while active"), preset),
            (bDrawn && overlayOffAlpha > 0.0) == bActive);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassColumnOpenFadesTest, "SOLTest.LaneTunnelClass.ColumnOpenFades",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Column open fades (Q9, SDD 53 4.5, 5.4; StepColumnOpenFades, 53d review): the first update and a time jump (the rail
// revision) snap every fade to its target; the target is bOpenNow (LaneOpenAt: open start <= now < close), every
// column open without a network; a closing column fades 1 -> 0 in 3 s real and holds its slot (Fade) until the fade is
// exactly 0; a column reused for another lane starts from 0
bool FSOLLaneTunnelClassColumnOpenFadesTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::StepColumnOpenFades;
    constexpr int32 columns = 4;
    TArray<int32> bodyA = { 1, 2, 3, INDEX_NONE };
    TArray<int32> bodyB = { 5, 6, 7, INDEX_NONE };
    double fades[columns] = { 0.5, 0.5, 0.5, 0.5 };
    int32 fadeA[columns] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
    int32 fadeB[columns] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
    uint8 visible[columns] = {};

    // Steps one update of all four columns
    auto step = [&](TConstArrayView<uint8> open, const double deltaS, const bool bSnap)
    {
        StepColumnOpenFades(bodyA, bodyB, open, deltaS, bSnap, TArrayView<double>(fades), TArrayView<int32>(fadeA),
            TArrayView<int32>(fadeB), TArrayView<uint8>(visible));
    };

    // bOpenNow from the slot window: open from its start, closed at its close
    TestTrue(TEXT("LaneOpenAt: open at the start"), SOLLaneNetwork::LaneOpenAt(10.0, 10.0, 20.0));
    TestFalse(TEXT("LaneOpenAt: closed at the close"), SOLLaneNetwork::LaneOpenAt(20.0, 10.0, 20.0));
    TestFalse(TEXT("LaneOpenAt: NaN closed"), SOLLaneNetwork::LaneOpenAt(LTC_NAN, 10.0, 20.0));

    // The first update snaps: open columns 1, closed 0; a column without end points is never visible
    const uint8 open1[columns] = { 1, 0, 1, 1 };
    step(open1, LTC_FRAME_S, true);
    TestTrue(TEXT("first update: open 1, closed 0"), fades[0] == 1.0 && fades[1] == 0.0 && fades[2] == 1.0);
    TestTrue(TEXT("first update: visible = fade > 0 with end points"), visible[0] == 1 && visible[1] == 0
        && visible[2] == 1 && visible[3] == 0);

    // No network (an empty open view): every column's target is open
    step(TConstArrayView<uint8>(), 1.0, false);
    LtcNear(*this, TEXT("no network: column 1 fades in 1 s / 3 s"), fades[1], 1.0 / 3.0, LTC_TIGHT);

    // Column 0 closes: it fades out over 3 s real and holds its slot (Fade) on every update while above 0
    const uint8 open2[columns] = { 0, 1, 1, 1 };
    int32 frames = 0;
    int32 holdBreaks = 0;
    while (fades[0] > 0.0 && frames < 1000)
    {
        step(open2, LTC_FRAME_S, false);
        ++frames;
        const ESOLLaneSlotHold hold = SOLLaneNetwork::LaneSlotHold(false, visible[0] != 0);
        holdBreaks += (fades[0] > 0.0) == (hold == ESOLLaneSlotHold::Fade) ? 0 : 1;
    }
    TestTrue(TEXT("close: 1 -> 0 in 3 s at 60 Hz (180 frames)"), frames >= 179 && frames <= 181);
    TestEqual(TEXT("close: held (Fade) exactly while the fade is above 0"), holdBreaks, 0);
    TestTrue(TEXT("close: exactly 0 at the end, slot free"), fades[0] == 0.0 && visible[0] == 0
        && SOLLaneNetwork::LaneSlotHold(false, visible[0] != 0) == ESOLLaneSlotHold::None);

    // A time jump (the rail revision) mid-fade snaps to the targets
    step(open1, LTC_FRAME_S, false);
    TestTrue(TEXT("reopen: fading in"), fades[0] > 0.0 && fades[0] < 0.1);
    step(open2, LTC_FRAME_S, true);
    TestTrue(TEXT("revision snap: closed 0, open 1"), fades[0] == 0.0 && fades[1] == 1.0);

    // Column 2 (open, fade 1) is reused for another lane: it starts from 0 and fades in
    bodyA[2] = 9;
    step(open2, LTC_FRAME_S, false);
    LtcNear(*this, TEXT("reused column: from 0, one frame in"), fades[2], LTC_FRAME_S / SOL::NPC_LANE_OPEN_FADE_REAL_S,
        LTC_TIGHT);
    TestTrue(TEXT("reused column: takes the new end points"), fadeA[2] == 9 && fadeB[2] == 7);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassSlotHoldTest, "SOLTest.LaneTunnelClass.SlotHoldPreemption",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The slot hold rule and fade pre-emption (SDD 53 4.5, user decision 53d review): a slot owning blocks is Blocks-held
// and retried every frame while out of the set; one held only by its fading tunnel is Fade-held, never retried per
// frame, and is the first slot a new lane takes when no slot is free (the fade is cut short); a Fade-held slot whose
// lane is in the set and a Blocks-held slot are never taken
bool FSOLLaneTunnelClassSlotHoldTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::AssignLaneSlots;
    using SOLLaneNetwork::IsSlotReleasePending;
    using SOLLaneNetwork::LaneSlotHold;
    TestTrue(TEXT("hold: blocks"), LaneSlotHold(true, false) == ESOLLaneSlotHold::Blocks);
    TestTrue(TEXT("hold: blocks and fading"), LaneSlotHold(true, true) == ESOLLaneSlotHold::Blocks);
    TestTrue(TEXT("hold: fading only"), LaneSlotHold(false, true) == ESOLLaneSlotHold::Fade);
    TestTrue(TEXT("hold: none"), LaneSlotHold(false, false) == ESOLLaneSlotHold::None);
    TestTrue(TEXT("pending: blocks out of the set"), IsSlotReleasePending(true, false));
    TestFalse(TEXT("pending: blocks in the set"), IsSlotReleasePending(true, true));
    TestFalse(TEXT("pending: never for a fade-only hold"), IsSlotReleasePending(false, false));

    // Returns a lane pair
    auto pair = [](const int32 nodeA, const int32 nodeB)
    {
        FSOLLanePair lane;
        lane.NodeA = nodeA;
        lane.NodeB = nodeB;
        return lane;
    };

    // Returns whether two lane pairs name the same lane
    auto same = [](const FSOLLanePair& lhs, const FSOLLanePair& rhs)
    {
        return lhs.NodeA == rhs.NodeA && lhs.NodeB == rhs.NodeB;
    };

    // Base 2, capacity 5: partner slots 2 (L1, left the set), 3 (L2, left the set), 4 (L3, in the set)
    const FSOLLanePair l1 = pair(3, 4);
    const FSOLLanePair l2 = pair(3, 5);
    const FSOLLanePair l3 = pair(4, 5);
    const FSOLLanePair l4 = pair(6, 7);
    const FSOLLanePair l5 = pair(6, 8);
    const TArray<FSOLLanePair> previous = { pair(0, 1), pair(0, 2), l1, l2, l3 };
    const uint8 blocks = static_cast<uint8>(ESOLLaneSlotHold::Blocks);
    const uint8 fade = static_cast<uint8>(ESOLLaneSlotHold::Fade);
    TArray<FSOLLanePair> slots;

    // L4 arrives with no free slot: it takes the Fade-held slot 2 (L1's fade cut short), never the Blocks-held one
    TestEqual(TEXT("pre-empt: no overflow"), AssignLaneSlots(previous, TArray<uint8>{ 0, 0, fade, blocks, 0 },
        TArray<FSOLLanePair>{ l3, l4 }, 2, 5, slots), 0);
    TestTrue(TEXT("pre-empt: L4 in slot 2, L2 kept in 3, L3 in 4"), slots.Num() == 5 && same(slots[2], l4)
        && same(slots[3], l2) && same(slots[4], l3));

    // Both held by blocks: L4 overflows (the same-epoch lag: a lane that closes in the epoch L4 arrives is still
    // Blocks-held until the block step, so L4 waits one epoch), and pre-empts the next epoch once the hold is Fade
    TestEqual(TEXT("blocks only: overflow 1"), AssignLaneSlots(previous, TArray<uint8>{ 0, 0, blocks, blocks, 0 },
        TArray<FSOLLanePair>{ l3, l4 }, 2, 5, slots), 1);
    TestTrue(TEXT("blocks only: L1 and L2 kept"), same(slots[2], l1) && same(slots[3], l2));
    const TArray<FSOLLanePair> afterLag = slots;
    TestEqual(TEXT("next epoch, L1 now fade-held: no overflow"), AssignLaneSlots(afterLag,
        TArray<uint8>{ 0, 0, fade, blocks, 0 }, TArray<FSOLLanePair>{ l3, l4 }, 2, 5, slots), 0);
    TestTrue(TEXT("next epoch: L4 in slot 2"), same(slots[2], l4));

    // A Fade-held slot whose lane is in the set (every open lane reports a fade above 0) is never taken
    TestEqual(TEXT("in-set fade hold: L5 overflows"), AssignLaneSlots(previous,
        TArray<uint8>{ 0, 0, fade, blocks, fade }, TArray<FSOLLanePair>{ l3, l4, l5 }, 2, 5, slots), 1);
    TestTrue(TEXT("in-set fade hold: L3 kept in slot 4, L4 in slot 2"), same(slots[4], l3) && same(slots[2], l4));

    // A free slot is taken before any fade hold is pre-empted
    TArray<FSOLLanePair> previousFree = previous;
    previousFree.Add(FSOLLanePair());
    TestEqual(TEXT("free slot first: no overflow"), AssignLaneSlots(previousFree,
        TArray<uint8>{ 0, 0, fade, blocks, 0, 0 }, TArray<FSOLLanePair>{ l3, l4 }, 2, 6, slots), 0);
    TestTrue(TEXT("free slot first: L4 in slot 5, L1 still fading in slot 2"), same(slots[5], l4)
        && same(slots[2], l1));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassPoolHysteresisTest, "SOLTest.LaneTunnelClass.PoolHysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Pool hysteresis (SDD 53 7.3, 53d review): a lane that had an instance last update keeps it unless a newcomer is
// NPC_TUNNEL_POOL_KEEP_RATIO (1.1) times larger on screen, so near-equal lanes do not swap every frame; the overflow
// count does not depend on it; an empty previous pool or a ratio of 1, below 1 or NaN ranks by the radius alone
bool FSOLLaneTunnelClassPoolHysteresisTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::SelectTunnelPool;
    const double keep = SOL::NPC_TUNNEL_POOL_KEEP_RATIO;
    LtcNear(*this, TEXT("keep ratio 1.1"), keep, 1.1, LTC_TIGHT);
    TArray<double> px = { 3.0, 2.0, 2.1 };
    const TArray<uint8> drawn = { 1, 1, 1 };
    TArray<int32> lanes;
    TestEqual(TEXT("fresh: overflow 1"), SelectTunnelPool(px, drawn, 2, lanes), 1);
    TestTrue(TEXT("fresh: lanes 0 and 2"), lanes.Contains(0) && lanes.Contains(2) && !lanes.Contains(1));

    // Lane 1 grows to 2.2 px, within 10 % of lane 2's 2.1: lane 2 keeps its instance
    const TArray<uint8> pool02 = { 1, 0, 1 };
    px[1] = 2.2;
    TestEqual(TEXT("within 10 %: overflow 1"), SelectTunnelPool(px, drawn, 2, lanes, pool02, keep), 1);
    TestTrue(TEXT("within 10 %: lane 2 kept"), lanes.Contains(2) && !lanes.Contains(1));
    TestTrue(TEXT("no hysteresis: lane 1 would take it"), SelectTunnelPool(px, drawn, 2, lanes) == 1
        && lanes.Contains(1) && !lanes.Contains(2));
    TestTrue(TEXT("ratio NaN: no hysteresis"), SelectTunnelPool(px, drawn, 2, lanes, pool02, LTC_NAN) == 1
        && lanes.Contains(1));
    TestTrue(TEXT("ratio 0.5: no hysteresis"), SelectTunnelPool(px, drawn, 2, lanes, pool02, 0.5) == 1
        && lanes.Contains(1));

    // Lane 1 grows to 2.4 px, more than 10 % above 2.1: it takes lane 2's instance
    px[1] = 2.4;
    TestEqual(TEXT("beyond 10 %: overflow 1"), SelectTunnelPool(px, drawn, 2, lanes, pool02, keep), 1);
    TestTrue(TEXT("beyond 10 %: lane 1 in, lane 2 out"), lanes.Contains(1) && !lanes.Contains(2)
        && lanes.Contains(0));

    // A pool member that is no longer drawn (Off) takes no instance whatever its history
    const TArray<uint8> drawnNo2 = { 1, 1, 0 };
    TestTrue(TEXT("Off pool member: no instance"), SelectTunnelPool(px, drawnNo2, 2, lanes, pool02, keep) == 0
        && !lanes.Contains(2));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassInstanceRewriteTest, "SOLTest.LaneTunnelClass.InstanceRewrite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Per-class instances (SDD 53 7.1, 9): an Off lane builds nothing (not drawn, so no pool entry and no instance of
// either class); the pooled lanes split by class in index order; each class's instances are rewritten only when its own
// lane set changes (a lane moving Reduced -> Full rewrites both, a new Reduced lane only the Reduced set)
bool FSOLLaneTunnelClassInstanceRewriteTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneNetwork::SplitTunnelPoolByClass;
    using SOLLaneNetwork::SyncTunnelClassLanes;
    const ESOLTunnelClass off = ESOLTunnelClass::Off;
    const ESOLTunnelClass reduced = ESOLTunnelClass::Reduced;
    const ESOLTunnelClass full = ESOLTunnelClass::Full;

    // Five lanes from Off at 2.0, 0.7, 0.45, 0.6 and 0.3 px (Epic): Full, Reduced, Off, Reduced, Off
    const double px[5] = { 2.0, 0.7, 0.45, 0.6, 0.3 };
    ESOLTunnelClass classes[5];
    uint8 drawn[5];
    for (int32 lane = 0; lane < 5; ++lane)
    {
        classes[lane] = SOLLaneNetwork::StepTunnelClass(off, px[lane], 1.0);
        drawn[lane] = classes[lane] == off ? 0 : 1;
    }
    TestTrue(TEXT("classes"), classes[0] == full && classes[1] == reduced && classes[2] == off
        && classes[3] == reduced && classes[4] == off);
    TArray<int32> pool;
    TestEqual(TEXT("pool: no overflow"), SOLLaneNetwork::SelectTunnelPool(px, drawn, 24, pool), 0);
    TestTrue(TEXT("Off lanes build nothing: no pool entry"), pool.Num() == 3 && !pool.Contains(2)
        && !pool.Contains(4));
    uint8 inPool[5] = {};
    for (const int32 lane : pool)
    {
        inPool[lane] = 1;
    }
    TArray<int32> nextFull;
    TArray<int32> nextReduced;
    SplitTunnelPoolByClass(inPool, classes, 5, nextFull, nextReduced);
    TestTrue(TEXT("split: Full {0}, Reduced {1, 3}"), nextFull == TArray<int32>{ 0 }
        && nextReduced == TArray<int32>{ 1, 3 });

    // First sync writes both classes; the same sets again rewrite nothing
    TArray<int32> currentFull;
    TArray<int32> currentReduced;
    TestTrue(TEXT("first: Full written"), SyncTunnelClassLanes(currentFull, nextFull));
    TestTrue(TEXT("first: Reduced written"), SyncTunnelClassLanes(currentReduced, nextReduced));
    TestFalse(TEXT("same: Full not rewritten"), SyncTunnelClassLanes(currentFull, nextFull));
    TestFalse(TEXT("same: Reduced not rewritten"), SyncTunnelClassLanes(currentReduced, nextReduced));

    // Lane 3 moves Reduced -> Full: both sets change
    classes[3] = full;
    SplitTunnelPoolByClass(inPool, classes, 5, nextFull, nextReduced);
    TestTrue(TEXT("class move: Full rewritten"), SyncTunnelClassLanes(currentFull, nextFull)
        && currentFull == TArray<int32>{ 0, 3 });
    TestTrue(TEXT("class move: Reduced rewritten"), SyncTunnelClassLanes(currentReduced, nextReduced)
        && currentReduced == TArray<int32>{ 1 });

    // Lane 2 becomes Reduced: only the Reduced set changes
    classes[2] = reduced;
    inPool[2] = 1;
    SplitTunnelPoolByClass(inPool, classes, 5, nextFull, nextReduced);
    TestFalse(TEXT("new Reduced lane: Full untouched"), SyncTunnelClassLanes(currentFull, nextFull));
    TestTrue(TEXT("new Reduced lane: Reduced rewritten"), SyncTunnelClassLanes(currentReduced, nextReduced)
        && currentReduced == TArray<int32>{ 1, 2 });

    // A pooled lane marked Off (defensive) takes no instance; laneCount bounds the split
    classes[1] = off;
    SplitTunnelPoolByClass(inPool, classes, 3, nextFull, nextReduced);
    TestTrue(TEXT("Off and beyond laneCount: none"), nextFull == TArray<int32>{ 0 }
        && nextReduced == TArray<int32>{ 2 });
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassLaneTunnelRadiusTest, "SOLTest.LaneTunnelClass.LaneTunnelRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// LaneTunnelRadiusM (SDD 53 7.1, SDD 50 D13): the lane profile's frozen wall radius when there is one, else the #49
// tunnel radius of the lane radius and its ship count; the class step reads it before any frame is built
bool FSOLLaneTunnelClassLaneTunnelRadiusTest::RunTest(const FString& /*parameters*/)
{
    FSOLNpcLaneEntry entry;
    entry.RadiusM = LTC_LANE_RADIUS_M;
    entry.ShipCount = 5000;
    entry.WallRadiusM = 2.5e6;
    TestEqual(TEXT("profile: the frozen wall radius"), SOLNpcTunnel::LaneTunnelRadiusM(entry), 2.5e6);
    for (const double noWall : { 0.0, -1.0, LTC_NAN })
    {
        entry.WallRadiusM = noWall;
        LtcNear(*this, FString::Printf(TEXT("wall %f: TunnelRadiusM of the lane"), noWall),
            SOLNpcTunnel::LaneTunnelRadiusM(entry), SOLNpcTunnel::TunnelRadiusM(LTC_LANE_RADIUS_M, 5000), LTC_TIGHT);
    }
    entry.ShipCount = 0;
    LtcNear(*this, TEXT("empty lane, no profile: the minimum scale"), SOLNpcTunnel::LaneTunnelRadiusM(entry),
        SOL::NPC_TUNNEL_RADIUS_MIN_SCALE * LTC_LANE_RADIUS_M, LTC_TIGHT);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelClassComponentMeshesTest, "SOLTest.LaneTunnelClass.ComponentMeshes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The real meshes (SDD 53 7.1): the tunnel component built in a preview world has a 15,380-vertex Full slot mesh and a
// 7,220-vertex Reduced mesh, the counts the pure functions give; with no lanes nothing is instanced
bool FSOLLaneTunnelClassComponentMeshesTest::RunTest(const FString& /*parameters*/)
{
    UWorld* world = UWorld::CreateWorld(EWorldType::GamePreview, false, TEXT("SOLLaneTunnelClassTestWorld"));
    FWorldContext& context = GEngine->CreateNewWorldContext(EWorldType::GamePreview);
    context.SetCurrentWorld(world);
    AActor* owner = world->SpawnActor<AActor>();
    USOLNpcTunnelComponent* tunnels = owner != nullptr ? NewObject<USOLNpcTunnelComponent>(owner) : nullptr;
    if (TestNotNull(TEXT("tunnel component"), tunnels))
    {
        owner->SetRootComponent(tunnels);
        tunnels->RegisterComponent();
        if (TestTrue(TEXT("InitializeTunnels (the material loads, so the meshes are built)"),
            tunnels->InitializeTunnels()))
        {
            TestEqual(TEXT("Reduced mesh: 7,220 vertices"), tunnels->GetReducedMeshVertexCount(), 7220);
            TestEqual(TEXT("Reduced mesh = TunnelReducedMeshVertexCount"), tunnels->GetReducedMeshVertexCount(),
                SOLNpcTunnel::TunnelReducedMeshVertexCount());
            TestEqual(TEXT("Full mesh: 15,380 vertices"), tunnels->GetSlotMeshVertexCount(), 15380);
            TestEqual(TEXT("no lanes: no instance"), tunnels->GetInstanceCount(), 0);
        }
    }
    GEngine->DestroyWorldContext(world);
    world->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
