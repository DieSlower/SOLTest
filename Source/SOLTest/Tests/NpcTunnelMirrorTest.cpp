/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Misc/AutomationTest.h"

#include "Npc/SOLNpcTunnelMath.h"
#include "Npc/SOLNpcTunnelMirror.h"
#include "SOLConstants.h"

// Part 52b (SDD 52 sections 4.2-4.5): the tunnel shader's float path (Shaders/Private/SOLNpcTunnel.ush, copied
// statement for statement in Npc/SOLNpcTunnelMirror.h) against the double SOLNpcTunnel functions it mirrors, on the
// vectors the material receives (SOLNpcTunnel::TunnelMaterialVectors). A drift between the shader and the C++ (a
// changed knot, a swapped component, a different fade) fails here; the GPU output itself is covered by the smokes.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCMIRROR_ALPHA_TOL = 1.0e-3;      // Float alpha vs the double reference
    constexpr double NPCMIRROR_VISIBLE = 1.0e-2;        // A reference alpha above this must be drawn by the float path
    constexpr double NPCMIRROR_MAX_LANE_SPACINGS = 63100.0; // The longest lane in ring spacings (757,000 Rt / 12)

    // One sweep case: the tunnel radius, the camera's distance from the axis (radii) and the end arcs (spacings)
    struct FNpcMirrorCase
    {
        double TunnelRadiusM;
        double RadialRadii;
        double EndASpacings;
        double EndBSpacings;
    };
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMirrorPackingTest, "SOLTest.Npc.TunnelMirror.Packing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 D16: the far-field constants ride the components the #49 accordion and fades freed, in the layout the shader
// reads: Layout0.y ring gap full px, Style3.x ring gap gone px, Shape.w radius gone px, Layout1.yzw radius solid px,
// knee px and knee alpha, Layout3.xy detail gap gone and full px, and the rebuilt material's Layout4 (end-ring floor,
// ribbon inner fraction); the kept components unchanged; the focal length from the projection equals FocalLengthRefPx
// at the supported resolutions
bool FSOLNpcTunnelMirrorPackingTest::RunTest(const FString& /*parameters*/)
{
    const FSOLNpcTunnelMaterialVectors v = SOLNpcTunnel::TunnelMaterialVectors();
    const auto same = [](const float packed, const double constant)
    {
        return packed == static_cast<float>(constant);
    };
    TestTrue(TEXT("Layout0 = (spacing, ring gap full px, ring slots, gap slots)"),
        same(v.Layout0.X, SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII) && same(v.Layout0.Y,
        SOL::NPC_TUNNEL_RING_GAP_FADE_FULL_PX) && same(v.Layout0.Z, SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE)
        && same(v.Layout0.W, SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE));
    TestTrue(TEXT("Style3 = (ring gap gone px, chevron far start, end, chevrons per gap)"),
        same(v.Style3.X, SOL::NPC_TUNNEL_RING_GAP_FADE_GONE_PX) && same(v.Style3.Y,
        SOL::NPC_TUNNEL_CHEVRON_FADE_START_RADII) && same(v.Style3.Z, SOL::NPC_TUNNEL_CHEVRON_FADE_END_RADII)
        && same(v.Style3.W, SOL::NPC_TUNNEL_CHEVRONS_PER_GAP));
    TestTrue(TEXT("Shape.w = radius fade gone px"), same(v.Shape.W, SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX)
        && same(v.Shape.Z, SOL::NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII));
    TestTrue(TEXT("Layout1 = (end fade spacings, solid px, knee px, knee alpha)"),
        same(v.Layout1.X, SOL::NPC_TUNNEL_END_FADE_SPACINGS) && same(v.Layout1.Y, SOL::NPC_TUNNEL_RADIUS_FADE_SOLID_PX)
        && same(v.Layout1.Z, SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_PX)
        && same(v.Layout1.W, SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_ALPHA));
    TestTrue(TEXT("Layout3 = (detail gap gone px, full px, dots per gap, dot rows)"),
        same(v.Layout3.X, SOL::NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX) && same(v.Layout3.Y,
        SOL::NPC_TUNNEL_DETAIL_GAP_FADE_FULL_PX) && same(v.Layout3.Z, SOL::NPC_TUNNEL_DOTS_PER_GAP)
        && same(v.Layout3.W, SOL::NPC_TUNNEL_DOT_ROWS));
    TestTrue(TEXT("Style0.w = the reference height"), same(v.Style0.W, SOL::NPC_LANE_REFERENCE_HEIGHT_PX));
    TestTrue(TEXT("Layout4 = (end ring min drawn px, ribbon inner fraction, 0, 0), the D16 rebuild"),
        same(v.Layout4.X, SOL::NPC_TUNNEL_END_RING_MIN_DRAWN_PX)
        && same(v.Layout4.Y, SOL::NPC_TUNNEL_RIBBON_INNER_MAX_RADIUS_FRACTION) && v.Layout4.Z == 0.0f
        && v.Layout4.W == 0.0f);

    // The focal length the shader derives from ViewToClip[0][0] = 1 / tan(fov / 2)
    const FVector2f sizes[] = { FVector2f(1280.0f, 720.0f), FVector2f(1920.0f, 1080.0f), FVector2f(3840.0f, 2160.0f) };
    for (const FVector2f& size : sizes)
    {
        const double fovDeg = SOL::SHIP_CAMERA_BASE_FOV_DEG;
        const float projection = static_cast<float>(1.0 / FMath::Tan(FMath::DegreesToRadians(0.5 * fovDeg)));
        const double expected = SOLNpcTunnel::FocalLengthRefPx(fovDeg, size.X, size.Y);
        TestEqual(FString::Printf(TEXT("focal at %.0fx%.0f"), size.X, size.Y),
            static_cast<double>(SOLNpcTunnelMirror::FocalRefPx(projection, size, v.Style0.W)), expected, 1.0e-3);
    }
    TestEqual(TEXT("focal: no view size -> 0"), SOLNpcTunnelMirror::FocalRefPx(1.0f, FVector2f(0.0f, 0.0f), 1080.0f),
        0.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMirrorFadesTest, "SOLTest.Npc.TunnelMirror.Fades",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The float fades against the double ones on a log sweep: RingRadiusFade (the D9 curve on Shape.w and Layout1),
// RingGapPx (on and off the axis), the ring and detail gap fades (the log ramp on Style3.x / Layout0.y and Layout3.xy),
// EndRingDrawnRadiusPx
bool FSOLNpcTunnelMirrorFadesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLNpcTunnelMaterialVectors v = SOLNpcTunnel::TunnelMaterialVectors();
    double worst = 0.0;
    for (int32 step = 0; step <= 400; ++step)
    {
        const double px = 0.1 * FMath::Pow(10.0, static_cast<double>(step) / 100.0);
        const float pxF = static_cast<float>(px);
        worst = FMath::Max(worst, FMath::Abs(SOLNpcTunnelMirror::RadiusFade(pxF, v.Shape, v.Layout1)
            - SOLNpcTunnel::RingRadiusFade(px)));
        worst = FMath::Max(worst, FMath::Abs(SOLNpcTunnelMirror::LogRamp(pxF, v.Style3.X, v.Layout0.Y)
            - SOLNpcTunnel::RingGapFade(px)));
        worst = FMath::Max(worst, FMath::Abs(SOLNpcTunnelMirror::LogRamp(pxF, v.Layout3.X, v.Layout3.Y)
            - SOLNpcTunnel::DetailGapFade(px)));
        TestEqual(FString::Printf(TEXT("end ring drawn radius at %.3f px"), px),
            static_cast<double>(SOLNpcTunnelMirror::EndRingDrawnRadiusPx(pxF, 1.0f)),
            SOLNpcTunnel::EndRingDrawnRadiusPx(px), 1.0e-5 * FMath::Max(1.0, px));
    }
    TestTrue(FString::Printf(TEXT("fades: worst float error %.3g"), worst), worst <= 1.0e-5);
    TestEqual(TEXT("radius fade: NaN -> 0"), SOLNpcTunnelMirror::RadiusFade(NAN, v.Shape, v.Layout1), 0.0f);
    TestEqual(TEXT("log ramp: NaN -> 0"), SOLNpcTunnelMirror::LogRamp(NAN, 2.0f, 3.0f), 0.0f);

    // The ring gap on a grid of axial offsets and radial distances (the shader's float atan2 difference)
    double worstGap = 0.0;
    const double rt = 3.0e6;
    const double f = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX;
    const double s0 = SOLNpcTunnel::RingLatticeSpacingM(rt);
    for (const double hRadii : { 0.0, 0.5, 3.0, 30.0, 300.0, 3000.0 })
    {
        for (int32 k = -80; k <= 80; ++k)
        {
            const double x = (static_cast<double>(k) + 0.37) * s0;
            const double reference = SOLNpcTunnel::RingGapPx(x, hRadii * rt, rt, f);
            const double mirror = SOLNpcTunnelMirror::RingGapPx(static_cast<float>(x), static_cast<float>(hRadii * rt),
                static_cast<float>(rt), static_cast<float>(f), static_cast<float>(s0));
            worstGap = FMath::Max(worstGap, FMath::Abs(mirror - reference) / FMath::Max(1.0, reference));
        }
    }
    TestTrue(FString::Printf(TEXT("ring gap: worst float error %.3g (relative, or px below 1 px)"), worstGap),
        worstGap <= 1.0e-3);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMirrorSlotsTest, "SOLTest.Npc.TunnelMirror.Slots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every ring, dot, chevron and end-ring slot's float alpha and axial offset against RingSlot / DotSlot / ChevronSlot /
// EndRingSlot (inside, beside and far from the tube, near and far from the end rings, on the longest lane near node A,
// the middle and node B, three phases, two focal lengths):
// within NPCMIRROR_ALPHA_TOL, and every element the double path shows above NPCMIRROR_VISIBLE drawn by the float path
bool FSOLNpcTunnelMirrorSlotsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const FSOLNpcTunnelMaterialVectors v = TunnelMaterialVectors();
    const FNpcMirrorCase cases[] = {
        { 3.0e6, 0.0, -2000.0, 3000.0 }, { 3.0e6, 0.4, -2000.0, 3000.0 }, { 3.0e6, 2.0, -2000.0, 3000.0 },
        { 3.0e6, 10.0, -2000.0, 3000.0 }, { 3.0e6, 100.0, -2000.0, 3000.0 }, { 3.0e6, 300.0, -2000.0, 3000.0 },
        { 3.0e6, 1000.0, -2000.0, 3000.0 }, { 2.0e5, 0.2, -3.3, 40.0 }, { 2.0e5, 25.0, -60.0, 2.6 },
        { 1.0e7, 5.0, -0.4, 0.9 },
        // The longest lane (Uranus-Neptune, about 757,000 tunnel radii = 63,100 spacings at Rt 5.9e6 m, end arcs up
        // to 4.5e12 m in float): the camera near node A, at the middle and near node B
        { 5.9e6, 0.0, -1.3, NPCMIRROR_MAX_LANE_SPACINGS }, { 5.9e6, 2.0, -0.5 * NPCMIRROR_MAX_LANE_SPACINGS,
        0.5 * NPCMIRROR_MAX_LANE_SPACINGS }, { 5.9e6, 300.0, -NPCMIRROR_MAX_LANE_SPACINGS, 0.7 } };
    double worstAlpha = 0.0;
    double worstOffsetSpacings = 0.0;
    int32 missed = 0;
    int32 compared = 0;
    for (const FNpcMirrorCase& mirrorCase : cases)
    {
        for (const double focal : { SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX, 0.6 * SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX })
        {
            for (const double phase : { 0.0, 0.31, 0.97 })
            {
                const double rt = mirrorCase.TunnelRadiusM;
                const double s0 = RingLatticeSpacingM(rt);
                FSOLNpcTunnelSlotInput input;
                input.RadialM = mirrorCase.RadialRadii * rt;
                input.TunnelRadiusM = rt;
                input.Phase = phase;
                input.EndArcAM = mirrorCase.EndASpacings * s0;
                input.EndArcBM = mirrorCase.EndBSpacings * s0;
                input.FocalRefPx = focal;
                const float hF = static_cast<float>(input.RadialM);
                const float rtF = static_cast<float>(rt);
                const float fF = static_cast<float>(focal);
                const float phaseF = static_cast<float>(phase);
                const FVector2f endsF(static_cast<float>(input.EndArcAM), static_cast<float>(input.EndArcBM));

                // Records one element's comparison
                const auto compare = [&](const bool bShown, const FSOLNpcTunnelRing& reference, const float alphaF,
                    const float xF)
                {
                    const double referenceAlpha = bShown ? reference.Alpha : 0.0;
                    worstAlpha = FMath::Max(worstAlpha, FMath::Abs(static_cast<double>(alphaF) - referenceAlpha));
                    if (bShown)
                    {
                        worstOffsetSpacings = FMath::Max(worstOffsetSpacings,
                            FMath::Abs(static_cast<double>(xF) - reference.AxialOffsetM) / s0);
                    }
                    missed += referenceAlpha > NPCMIRROR_VISIBLE && !(alphaF > 0.0f) ? 1 : 0;
                    ++compared;
                };
                for (int32 slot = 0; slot < RingSlotCount(); ++slot)
                {
                    FSOLNpcTunnelRing ring;
                    const bool bShown = RingSlot(slot, input, ring);
                    float xF = 0.0f;
                    const float alphaF = SOLNpcTunnelMirror::RingSlotAlpha(slot, phaseF, hF, rtF, fF, endsF, v, xF);
                    compare(bShown, ring, alphaF, xF);
                }
                for (int32 slot = 0; slot < DetailSlotCount(); ++slot)
                {
                    for (int32 index = 0; index < SOL::NPC_TUNNEL_DOTS_PER_GAP; ++index)
                    {
                        FSOLNpcTunnelRing dot;
                        const bool bShown = DotSlot(slot, index, input, dot);
                        float xF = 0.0f;
                        const float alphaF = SOLNpcTunnelMirror::DetailSlotAlpha(slot, index, true, phaseF, hF, rtF,
                            fF, endsF, v, xF);
                        compare(bShown, dot, alphaF, xF);
                    }
                    for (int32 index = 0; index < SOL::NPC_TUNNEL_CHEVRONS_PER_GAP; ++index)
                    {
                        FSOLNpcTunnelRing chevron;
                        const bool bShown = ChevronSlot(slot, index, input, chevron);
                        float xF = 0.0f;
                        const float alphaF = SOLNpcTunnelMirror::DetailSlotAlpha(slot, index, false, phaseF, hF, rtF,
                            fF, endsF, v, xF);
                        compare(bShown, chevron, alphaF, xF);
                    }
                }
                for (int32 end = 0; end < 2; ++end)
                {
                    FSOLNpcTunnelRing ring;
                    const bool bShown = EndRingSlot(end, input, ring);
                    float radiusPx = 0.0f;
                    const float alphaF = SOLNpcTunnelMirror::EndRingAlpha(end == 0 ? endsF.X : endsF.Y, hF, rtF, fF, v,
                        radiusPx);
                    ring.AxialOffsetM = static_cast<double>(end == 0 ? endsF.X : endsF.Y);
                    compare(bShown, ring, alphaF, end == 0 ? endsF.X : endsF.Y);
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d elements compared, worst alpha error %.3g, worst offset %.3g spacings"), compared,
        worstAlpha, worstOffsetSpacings));
    TestTrue(FString::Printf(TEXT("float alpha within %.0e (worst %.3g)"), NPCMIRROR_ALPHA_TOL, worstAlpha),
        worstAlpha <= NPCMIRROR_ALPHA_TOL);
    TestTrue(FString::Printf(TEXT("float offsets within 1e-5 spacings (worst %.3g)"), worstOffsetSpacings),
        worstOffsetSpacings <= 1.0e-5);
    TestEqual(TEXT("no visible element dropped by the float path"), missed, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMirrorRibbonInnerTest, "SOLTest.Npc.TunnelMirror.RibbonInner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The sub-pixel ring fix (52b review): a ring ribbon's inner half never reaches the ring's centre, so a ring smaller
// than its glow cannot fold into overlapping bowties: RingRibbonInnerExtentPx is always below the ring radius (for a
// positive radius), equals the full half extent once the ring is larger than the ribbon (those rings render exactly as
// before), and the shader's float copy (Layout4.y the fraction) matches the double one; bad input gives 0
bool FSOLNpcTunnelMirrorRibbonInnerTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::RingRibbonInnerExtentPx;
    const FSOLNpcTunnelMaterialVectors v = SOLNpcTunnel::TunnelMaterialVectors();
    const double fraction = SOL::NPC_TUNNEL_RIBBON_INNER_MAX_RADIUS_FRACTION;
    int32 reachedCentre = 0;
    int32 changedLargeRing = 0;
    double worst = 0.0;
    for (const double halfPx : { 0.75, 2.0, 12.5, 25.5, 51.0 })
    {
        for (int32 step = 0; step <= 600; ++step)
        {
            const double radiusPx = 0.01 * FMath::Pow(10.0, static_cast<double>(step) / 100.0);
            const double inner = RingRibbonInnerExtentPx(halfPx, radiusPx);
            reachedCentre += inner < radiusPx && inner > 0.0 ? 0 : 1;
            changedLargeRing += fraction * radiusPx >= halfPx && inner != halfPx ? 1 : 0;
            const float innerF = SOLNpcTunnelMirror::RingRibbonInnerExtentPx(static_cast<float>(halfPx),
                static_cast<float>(radiusPx), v.Layout4.Y);
            worst = FMath::Max(worst, FMath::Abs(static_cast<double>(innerF) - inner) / FMath::Max(1.0, inner));
            reachedCentre += innerF < static_cast<float>(radiusPx) ? 0 : 1;
        }
    }
    TestEqual(TEXT("the inner offset always stays short of the ring's centre (double and float)"), reachedCentre, 0);
    TestEqual(TEXT("a ring larger than its ribbon keeps the full half extent"), changedLargeRing, 0);
    TestTrue(FString::Printf(TEXT("float copy matches (worst %.3g)"), worst), worst <= 1.0e-6);

    // Pinned values at the 1080p ring ribbon (12.5 px half extent): a 1 px ring reaches 0.9 px, a 100 px ring 12.5 px
    TestEqual(TEXT("1 px ring: fraction x radius"), RingRibbonInnerExtentPx(12.5, 1.0), fraction, 1.0e-12);
    TestEqual(TEXT("100 px ring: the half extent"), RingRibbonInnerExtentPx(12.5, 100.0), 12.5);

    // Bad input
    TestEqual(TEXT("NaN radius"), RingRibbonInnerExtentPx(12.5, NAN), 0.0);
    TestEqual(TEXT("zero radius"), RingRibbonInnerExtentPx(12.5, 0.0), 0.0);
    TestEqual(TEXT("negative half extent"), RingRibbonInnerExtentPx(-1.0, 5.0), 0.0);
    TestEqual(TEXT("float: NaN radius"), SOLNpcTunnelMirror::RingRibbonInnerExtentPx(12.5f, NAN, v.Layout4.Y), 0.0f);
    return true;
}
