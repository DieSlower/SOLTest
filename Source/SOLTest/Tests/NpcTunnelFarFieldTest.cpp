/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>
#include <type_traits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #52 lane tunnel far field (SDD 52 D1-D19, contract Docs/Plans/52a-tunnel-far-field-contract.md),
// written against that contract before the production code existed (part 52a, test first). The ring lattice is flat
// (S0 = 12 Rt everywhere, D2), drawn by a fixed window of 80 ring slots per side around the camera's foot point, every
// ring faded by its on-screen radius in 1080p-reference pixels (D8, D9: solid at 6 px, 0.1 at 1 px, gone at 0.5 px,
// log-linear) and by its on-screen gap to the next ring (D11: 3 -> 2 px); dots and chevrons only where the gap is about
// 20 px or more (D4, D11, D12); end rings on the pixel curve with a 1 px drawn floor (D14); the label strength keeps
// the old 100 -> 50 lane radii band while drawing is not gated by it (D7). Rings are identified by their lattice index
// round((foot arc + x) / S0), never by slot index. Only the numbers the user gave are pinned; the rest is referenced by
// constant name under invariants.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCFF_TOL = 1.0e-9;
    constexpr double NPCFF_ALPHA_TOL = 1.0e-6;          // Slot alpha vs the reference (arcs up to 7.6e12 m)
    constexpr double NPCFF_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double NPCFF_INF = std::numeric_limits<double>::infinity();
    constexpr double NPCFF_ASPECT = 16.0 / 9.0;         // The reference view's aspect
    constexpr double NPCFF_POP_LIMIT = 0.03;            // Largest alpha change one sweep step may make
    constexpr int32 NPCFF_SWEEP_STEPS = 12000;          // Steps per no-pop sweep (as SDD 49 case 11)

    // A lane of SDD 52 D2's length list: name, length in tunnel radii, tunnel radius and the two body radii
    struct FNpcFarFieldLane
    {
        const TCHAR* Name = TEXT("");
        double LengthRadii = 0.0;
        double TunnelRadiusM = 0.0;
        double BodyRadiusAM = 0.0;
        double BodyRadiusBM = 0.0;
    };

    // The five lane lengths of D2 (1,920 / 18,600 / 133,000 / 275,000 / 757,000 Rt) at a plausible tunnel radius each
    const FNpcFarFieldLane NPCFF_LANES[] = {
        { TEXT("Earth-Moon"), 1920.0, 2.0e5, 6.371e6, 1.7374e6 },
        { TEXT("Earth-Mars near"), 18600.0, 3.0e6, 6.371e6, 3.3895e6 },
        { TEXT("Earth-Mars far"), 133000.0, 3.0e6, 6.371e6, 3.3895e6 },
        { TEXT("Jupiter-Saturn"), 275000.0, 5.4e6, 6.9911e7, 5.8232e7 },
        { TEXT("Uranus-Neptune"), 757000.0, 1.0e7, 2.5559e7, 2.4764e7 },
    };

    // A ring or detail element a slot draws: its lattice index, axial offset from the foot point and alpha
    struct FNpcFarFieldSeen
    {
        int64 Index = 0;
        double X = 0.0;
        double Alpha = 0.0;
    };

    // Failure counters of a check, with the first failure's description
    struct FNpcFarFieldFailures
    {
        int32 Count = 0;
        FString First;

        // Counts one failure and keeps its description if it is the first
        void Add(const FString& description)
        {
            if (Count == 0)
            {
                First = description;
            }
            ++Count;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the flat ring spacing S0 = NPC_TUNNEL_RING_BASE_SPACING_RADII x Rt (the test's own copy)
    double NpcFarFieldS0(const double tunnelRadiusM)
    {
        return SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII * tunnelRadiusM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns frac(x) in [0, 1) (a negative x wraps up)
    double NpcFarFieldFrac(const double x)
    {
        return x - FMath::FloorToDouble(x);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the circular distance between two phases in [0, 1)
    double NpcFarFieldWrapDistance(const double a, const double b)
    {
        const double d = FMath::Abs(a - b);
        return FMath::Min(d, 1.0 - d);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the focal length in reference px of a 16:9 view at a horizontal field of view (the test's closed form)
    double NpcFarFieldFocal(const double horizontalFovDeg)
    {
        return 0.5 * NPCFF_ASPECT * SOL::NPC_LANE_REFERENCE_HEIGHT_PX
            / FMath::Tan(FMath::DegreesToRadians(0.5 * horizontalFovDeg));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the D9 curve as the test reads it: 1 from SOLID, KNEE_ALPHA at KNEE, 0 at GONE, linear in log px between
    double NpcFarFieldRefRadiusFade(const double px)
    {
        const double solid = SOL::NPC_TUNNEL_RADIUS_FADE_SOLID_PX;
        const double knee = SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_PX;
        const double kneeAlpha = SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_ALPHA;
        const double gone = SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX;
        if (px >= solid)
        {
            return 1.0;
        }
        if (px >= knee)
        {
            return kneeAlpha + (1.0 - kneeAlpha) * std::log(px / knee) / std::log(solid / knee);
        }
        if (px > gone)
        {
            return kneeAlpha * std::log(px / gone) / std::log(knee / gone);
        }
        return 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the gap between a ring at x and its neighbour away from the camera, as the contract defines it
    double NpcFarFieldRefGapPx(const double axialM, const double radialM, const double rt, const double focalRefPx)
    {
        const double x = FMath::Abs(axialM);
        const double h = FMath::Abs(radialM);
        const double xNext = x + NpcFarFieldS0(rt);
        const double d = FMath::Sqrt(x * x + h * h);
        const double dNext = FMath::Sqrt(xNext * xNext + h * h);
        const double rays = std::atan2(xNext, h) - std::atan2(x, h);
        const double radii = rt / d - rt / dNext;
        return focalRefPx * FMath::Max(rays, radii);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's length in metres
    double NpcFarFieldLengthM(const FNpcFarFieldLane& lane)
    {
        return lane.LengthRadii * lane.TunnelRadiusM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's end-ring arcs from node A (TunnelEndArcsM, tested by the #49 cases)
    FVector2D NpcFarFieldEndArcs(const FNpcFarFieldLane& lane)
    {
        return SOLNpcTunnel::TunnelEndArcsM(NpcFarFieldLengthM(lane), lane.BodyRadiusAM, lane.BodyRadiusBM,
            lane.TunnelRadiusM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the slot input of a camera at a foot arc and distance h from the axis, phase frac(foot arc / S0) computed
    // by the test, end-ring arcs relative to the foot arc and a focal length in reference px
    FSOLNpcTunnelSlotInput NpcFarFieldInput(const double footArcM, const double radialM, const double rt,
        const FVector2D& endArcsM, const double focalRefPx)
    {
        FSOLNpcTunnelSlotInput input;
        input.RadialM = radialM;
        input.TunnelRadiusM = rt;
        input.Phase = NpcFarFieldFrac(footArcM / NpcFarFieldS0(rt));
        input.EndArcAM = endArcsM.X - footArcM;
        input.EndArcBM = endArcsM.Y - footArcM;
        input.FocalRefPx = focalRefPx;
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the axial distance from x to the nearer end ring (negative outside the tunnel)
    double NpcFarFieldDistanceToEnd(const double axialM, const FSOLNpcTunnelSlotInput& input)
    {
        return FMath::Min(axialM - input.EndArcAM, input.EndArcBM - axialM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lattice ring's reference alpha without the window's cap fade: end x near x radius x gap fades, 0
    // outside the end rings (the contract's RingSlot product)
    double NpcFarFieldRingAlphaNoCap(const double axialM, const FSOLNpcTunnelSlotInput& input)
    {
        using namespace SOLNpcTunnel;
        if (!(axialM > input.EndArcAM && axialM < input.EndArcBM))
        {
            return 0.0;
        }
        const double rt = input.TunnelRadiusM;
        const double h = input.RadialM;
        const double f = input.FocalRefPx;
        const double d = FMath::Sqrt(axialM * axialM + h * h);
        return TunnelEndFade(NpcFarFieldDistanceToEnd(axialM, input), NpcFarFieldS0(rt))
            * TunnelNearFadeAt(axialM, h, rt) * RingRadiusFade(RingRadiusPx(d, rt, f))
            * RingGapFade(RingGapPx(axialM, h, rt, f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lattice ring's reference alpha with the window's cap fade (w = x / S0)
    double NpcFarFieldRingAlpha(const double axialM, const FSOLNpcTunnelSlotInput& input)
    {
        return SOLNpcTunnel::RingCapFade(axialM / NpcFarFieldS0(input.TunnelRadiusM))
            * NpcFarFieldRingAlphaNoCap(axialM, input);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a dot's (bChevron false) or chevron's reference alpha without the detail cap: end x near x its size fade
    // (the ring radius for a dot, the chevron's world length for a chevron) x the gap gate (x the chevron far fade)
    double NpcFarFieldDetailAlphaNoCap(const double axialM, const FSOLNpcTunnelSlotInput& input, const bool bChevron)
    {
        using namespace SOLNpcTunnel;
        if (!(axialM > input.EndArcAM && axialM < input.EndArcBM))
        {
            return 0.0;
        }
        const double rt = input.TunnelRadiusM;
        const double h = input.RadialM;
        const double f = input.FocalRefPx;
        const double d = FMath::Sqrt(axialM * axialM + h * h);
        const double base = TunnelEndFade(NpcFarFieldDistanceToEnd(axialM, input), NpcFarFieldS0(rt))
            * TunnelNearFadeAt(axialM, h, rt) * DetailGapFade(RingGapPx(axialM, h, rt, f));
        return bChevron ? base * RingRadiusFade(ChevronLengthPx(d, rt, f)) * TunnelChevronFarFade(d, rt)
            : base * RingRadiusFade(RingRadiusPx(d, rt, f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Collects the rings the ring slots draw for one view, each with its lattice index
    void NpcFarFieldSlotRings(const double footArcM, const FSOLNpcTunnelSlotInput& input,
        TArray<FNpcFarFieldSeen>& outRings)
    {
        outRings.Reset();
        const double s0 = NpcFarFieldS0(input.TunnelRadiusM);
        for (int32 slot = 0; slot < SOLNpcTunnel::RingSlotCount(); ++slot)
        {
            FSOLNpcTunnelRing ring;
            if (SOLNpcTunnel::RingSlot(slot, input, ring))
            {
                FNpcFarFieldSeen seen;
                seen.Index = static_cast<int64>(std::llround((footArcM + ring.AxialOffsetM) / s0));
                seen.X = ring.AxialOffsetM;
                seen.Alpha = ring.Alpha;
                outRings.Add(seen);
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a lattice index is among the drawn rings
    bool NpcFarFieldHasIndex(const TArray<FNpcFarFieldSeen>& rings, const int64 index)
    {
        for (const FNpcFarFieldSeen& ring : rings)
        {
            if (ring.Index == index)
            {
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks one view of the ring window: every drawn ring is a lattice ring strictly between the end rings with the
    // reference alpha, drawn once; every lattice ring with a reference alpha above 0 is drawn; and, when the focal
    // length is within the reference, a ring outside the window has an uncapped alpha of at most the 1 px knee
    void NpcFarFieldCheckRingWindow(const double footArcM, const FSOLNpcTunnelSlotInput& input,
        const bool bWindowCovers, const FString& label, FNpcFarFieldFailures& failures)
    {
        const double s0 = NpcFarFieldS0(input.TunnelRadiusM);
        const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
        TArray<FNpcFarFieldSeen> seen;
        NpcFarFieldSlotRings(footArcM, input, seen);

        // Every drawn ring: on the lattice, inside the tunnel, at the reference alpha, drawn by one slot only
        for (int32 i = 0; i < seen.Num(); ++i)
        {
            const double latticeX = static_cast<double>(seen[i].Index) * s0 - footArcM;
            if (!(FMath::Abs(seen[i].X - latticeX) <= 1.0e-6 * s0))
            {
                failures.Add(FString::Printf(TEXT("%s: ring at %.6e off the lattice"), *label, seen[i].X));
            }
            if (!(seen[i].X > input.EndArcAM && seen[i].X < input.EndArcBM))
            {
                failures.Add(FString::Printf(TEXT("%s: ring at %.6e outside the end rings"), *label, seen[i].X));
            }
            const double expected = NpcFarFieldRingAlpha(latticeX, input);
            if (!(FMath::Abs(seen[i].Alpha - expected) <= NPCFF_ALPHA_TOL))
            {
                failures.Add(FString::Printf(TEXT("%s: ring %lld alpha %.9f vs %.9f"), *label,
                    static_cast<long long>(seen[i].Index), seen[i].Alpha, expected));
            }
            for (int32 j = 0; j < i; ++j)
            {
                if (seen[j].Index == seen[i].Index)
                {
                    failures.Add(FString::Printf(TEXT("%s: ring %lld drawn twice"), *label,
                        static_cast<long long>(seen[i].Index)));
                }
            }
        }

        // Every lattice ring near the window: drawn when its reference alpha is above 0; beyond the window faint
        const int64 footIndex = static_cast<int64>(FMath::FloorToDouble(footArcM / s0));
        for (int64 index = footIndex - 2 * perSide; index <= footIndex + 2 * perSide + 1; ++index)
        {
            const double x = static_cast<double>(index) * s0 - footArcM;
            if (NpcFarFieldRingAlpha(x, input) > NPCFF_TOL && !NpcFarFieldHasIndex(seen, index))
            {
                failures.Add(FString::Printf(TEXT("%s: lattice ring %lld (x %.1f Rt) not drawn"), *label,
                    static_cast<long long>(index), x / input.TunnelRadiusM));
            }
            if (bWindowCovers && FMath::Abs(x) >= perSide * s0
                && NpcFarFieldRingAlphaNoCap(x, input) > SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_ALPHA + NPCFF_TOL)
            {
                failures.Add(FString::Printf(TEXT("%s: ring %lld beyond the window above the knee (%.4f)"), *label,
                    static_cast<long long>(index), NpcFarFieldRingAlphaNoCap(x, input)));
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the largest change of any lattice ring's alpha between consecutive views of a sweep, the views given by
    // viewAt(step, footArcM, radialM, focalRefPx); counts rings drawn by two slots in one view
    template <typename TViewAt>
    double NpcFarFieldSweepMaxJump(const FNpcFarFieldLane& lane, const TViewAt& viewAt, int32& outTwice,
        FString& outWhere)
    {
        const double rt = lane.TunnelRadiusM;
        const FVector2D endArcs = NpcFarFieldEndArcs(lane);
        TMap<int64, double> previous;
        TMap<int64, double> current;
        TArray<FNpcFarFieldSeen> seen;
        double maxJump = 0.0;
        outTwice = 0;
        for (int32 step = 0; step <= NPCFF_SWEEP_STEPS; ++step)
        {
            double footArcM = 0.0;
            double radialM = 0.0;
            double focalRefPx = 0.0;
            viewAt(step, footArcM, radialM, focalRefPx);
            const FSOLNpcTunnelSlotInput input = NpcFarFieldInput(footArcM, radialM, rt, endArcs, focalRefPx);
            NpcFarFieldSlotRings(footArcM, input, seen);

            // This view's alpha per lattice ring
            current.Reset();
            for (const FNpcFarFieldSeen& ring : seen)
            {
                if (current.Contains(ring.Index))
                {
                    ++outTwice;
                }
                current.Add(ring.Index, ring.Alpha);
            }

            // Compare with the previous view; a ring missing from either view counts as alpha 0
            if (step > 0)
            {
                for (const TPair<int64, double>& pair : current)
                {
                    const double jump = FMath::Abs(pair.Value - previous.FindRef(pair.Key));
                    if (jump > maxJump)
                    {
                        maxJump = jump;
                        outWhere = FString::Printf(TEXT("step %d, ring %lld, %.6f -> %.6f"), step,
                            static_cast<long long>(pair.Key), previous.FindRef(pair.Key), pair.Value);
                    }
                }
                for (const TPair<int64, double>& pair : previous)
                {
                    if (!current.Contains(pair.Key) && pair.Value > maxJump)
                    {
                        maxJump = pair.Value;
                        outWhere = FString::Printf(TEXT("step %d, ring %lld vanished at %.6f"), step,
                            static_cast<long long>(pair.Key), pair.Value);
                    }
                }
            }
            Swap(previous, current);
        }
        return maxJump;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldConstantsTest, "SOLTest.Npc.TunnelFarField.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 11: the user's numbers pinned (flat 12 Rt spacing tied to the guide thinning, 80 ring slots per side,
// the D9 curve 6 / 1 / 0.1 / 0.5 px, the D11 gaps 3 -> 2 px and 20 px, the D14 1 px floor, the 1080p reference and
// its 90-degree focal of 960 px); the open values (detail fade band, detail slots) only under invariants; the ring
// window covers every ring above the 1 px knee at the reference focal
bool FSOLNpcTunnelFarFieldConstantsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;

    // Flat lattice: 12 Rt everywhere, the guide thinning's density (6 x the pre-thinning 2 Rt)
    TestEqual(TEXT("ring spacing 12 Rt"), static_cast<double>(SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII), 12.0,
        NPCFF_TOL);
    TestEqual(TEXT("keep every 6th ring"), static_cast<int32>(SOL::NPC_TUNNEL_RING_KEEP_EVERY), 6);
    TestEqual(TEXT("spacing = keep x 2 Rt"), static_cast<double>(SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII),
        SOL::NPC_TUNNEL_RING_KEEP_EVERY * 2.0, NPCFF_TOL);

    // The window: 80 ring slots per side (SDD 52 section 4.3); the detail slots within it
    TestEqual(TEXT("80 ring slots per side"), static_cast<int32>(SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE), 80);
    TestTrue(TEXT("detail slots per side in [2, ring slots per side]"), SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE >= 2
        && SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE <= SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE);

    // Reference pixels (D8): the 1080p height and the focal length of a 16:9 view at the ship camera's base FOV
    TestEqual(TEXT("reference height 1080 px"), static_cast<double>(SOL::NPC_LANE_REFERENCE_HEIGHT_PX), 1080.0,
        NPCFF_TOL);
    TestEqual(TEXT("reference focal 960 px"), static_cast<double>(SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX), 960.0, 1.0e-6);
    TestEqual(TEXT("reference focal = 16:9 at the base FOV"), static_cast<double>(SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX),
        NpcFarFieldFocal(SOL::SHIP_CAMERA_BASE_FOV_DEG), 1.0e-6);

    // The D9 curve
    TestEqual(TEXT("solid at 6 px"), static_cast<double>(SOL::NPC_TUNNEL_RADIUS_FADE_SOLID_PX), 6.0, NPCFF_TOL);
    TestEqual(TEXT("knee at 1 px"), static_cast<double>(SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_PX), 1.0, NPCFF_TOL);
    TestEqual(TEXT("knee alpha 0.1 (90% transparent)"), static_cast<double>(SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_ALPHA),
        0.1, NPCFF_TOL);
    TestEqual(TEXT("gone at 0.5 px"), static_cast<double>(SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX), 0.5, NPCFF_TOL);

    // The D11 gaps: rings 3 -> 2 px; details at 20 px, the fade band around it (open, invariants only)
    TestEqual(TEXT("ring gap full at 3 px"), static_cast<double>(SOL::NPC_TUNNEL_RING_GAP_FADE_FULL_PX), 3.0,
        NPCFF_TOL);
    TestEqual(TEXT("ring gap gone at 2 px"), static_cast<double>(SOL::NPC_TUNNEL_RING_GAP_FADE_GONE_PX), 2.0,
        NPCFF_TOL);
    TestEqual(TEXT("detail gap 20 px"), static_cast<double>(SOL::NPC_TUNNEL_DETAIL_GAP_PX), 20.0, NPCFF_TOL);
    const double detailGone = SOL::NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX;
    const double detailFull = SOL::NPC_TUNNEL_DETAIL_GAP_FADE_FULL_PX;
    TestTrue(TEXT("detail gap band ordered round 20 px"), detailGone > 0.0 && detailGone < detailFull
        && detailGone <= SOL::NPC_TUNNEL_DETAIL_GAP_PX && SOL::NPC_TUNNEL_DETAIL_GAP_PX <= detailFull);
    TestTrue(TEXT("detail gap band within a factor 2 of 20 px"), detailGone >= 0.5 * SOL::NPC_TUNNEL_DETAIL_GAP_PX
        && detailFull <= 2.0 * SOL::NPC_TUNNEL_DETAIL_GAP_PX);

    // D14 and D15
    TestEqual(TEXT("end ring drawn floor 1 px"), static_cast<double>(SOL::NPC_TUNNEL_END_RING_MIN_DRAWN_PX), 1.0,
        NPCFF_TOL);
    TestEqual(TEXT("vertex budget 80,000 (interim)"), static_cast<int32>(SOL::NPC_TUNNEL_VERTEX_BUDGET), 80000);
    TestEqual(TEXT("budget for 8 lanes"), static_cast<int32>(SOL::NPC_TUNNEL_VERTEX_BUDGET_LANES), 8);

    // The window reaches every ring above the knee at the reference focal: 80 x 12 Rt >= 960 Rt
    TestTrue(TEXT("window covers f Rt / knee"), SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE
        * SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII >= SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX
        / SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_PX);
    TestTrue(TEXT("the max FOV's focal is below the reference"),
        NpcFarFieldFocal(SOL::SHIP_CAMERA_MAX_FOV_DEG) < SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldReferencePxTest, "SOLTest.Npc.TunnelFarField.ReferencePixels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D8: pixel sizes are 1080p-reference pixels (actual px x 1080 / viewport height), so 3 px at 1080p is 6 px at 4K;
// the focal length in reference px is the same at 1280x720, 1920x1080 and 3840x2160 for a fixed horizontal FOV and
// aspect (960 px at 90 degrees); bad inputs give 0
bool FSOLNpcTunnelFarFieldReferencePxTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    TestEqual(TEXT("3 px at 1080p = 3 reference px"), ToReferencePx(3.0, 1080.0), 3.0, NPCFF_TOL);
    TestEqual(TEXT("6 px at 4K = 3 reference px"), ToReferencePx(6.0, 2160.0), 3.0, NPCFF_TOL);
    TestEqual(TEXT("2 px at 720p = 3 reference px"), ToReferencePx(2.0, 720.0), 3.0, NPCFF_TOL);
    TestEqual(TEXT("reference px: NaN px"), ToReferencePx(NPCFF_NAN, 1080.0), 0.0);
    for (const double badHeight : { 0.0, -1080.0, NPCFF_NAN, NPCFF_INF })
    {
        TestEqual(FString::Printf(TEXT("reference px: height %f"), badHeight), ToReferencePx(3.0, badHeight), 0.0);
    }

    // The same focal at every supported resolution
    const double heights[] = { 720.0, 1080.0, 2160.0 };
    for (const double height : heights)
    {
        const double width = NPCFF_ASPECT * height;
        TestEqual(FString::Printf(TEXT("focal 960 at %.0f px high"), height), FocalLengthRefPx(90.0, width, height),
            960.0, 1.0e-6);
        TestEqual(FString::Printf(TEXT("focal at the max FOV, %.0f px high"), height),
            FocalLengthRefPx(SOL::SHIP_CAMERA_MAX_FOV_DEG, width, height),
            NpcFarFieldFocal(SOL::SHIP_CAMERA_MAX_FOV_DEG), 1.0e-6);
    }
    TestEqual(TEXT("focal = reference focal at the base FOV, 1080p"),
        FocalLengthRefPx(SOL::SHIP_CAMERA_BASE_FOV_DEG, 1920.0, 1080.0),
        static_cast<double>(SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX), 1.0e-6);
    TestTrue(TEXT("a wider FOV has a shorter focal"), FocalLengthRefPx(110.0, 1920.0, 1080.0)
        < FocalLengthRefPx(90.0, 1920.0, 1080.0));

    // Bad view inputs
    TestEqual(TEXT("focal: fov 0"), FocalLengthRefPx(0.0, 1920.0, 1080.0), 0.0);
    TestEqual(TEXT("focal: fov 180"), FocalLengthRefPx(180.0, 1920.0, 1080.0), 0.0);
    TestEqual(TEXT("focal: NaN fov"), FocalLengthRefPx(NPCFF_NAN, 1920.0, 1080.0), 0.0);
    TestEqual(TEXT("focal: width 0"), FocalLengthRefPx(90.0, 0.0, 1080.0), 0.0);
    TestEqual(TEXT("focal: height 0"), FocalLengthRefPx(90.0, 1920.0, 0.0), 0.0);
    TestEqual(TEXT("focal: infinite width"), FocalLengthRefPx(90.0, NPCFF_INF, 1080.0), 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldRadiusFadeTest, "SOLTest.Npc.TunnelFarField.RadiusFadeCurve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 5 per D9 (it supersedes D3's 3 px = 0.5 point, so 3 px reads about 0.65): RingRadiusFade is 1 from 6
// px, 0.1 at 1 px, 0 at and below 0.5 px, linear in log px between; non-decreasing, no pop at 0.5, 1 or 6 px; NaN and
// negative inputs 0, +inf 1; RingRadiusPx is f Rt / d (+inf at the camera, 0 for bad input)
bool FSOLNpcTunnelFarFieldRadiusFadeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    TestEqual(TEXT("1 at 6 px"), RingRadiusFade(6.0), 1.0, NPCFF_TOL);
    TestEqual(TEXT("1 at 100 px"), RingRadiusFade(100.0), 1.0, NPCFF_TOL);
    TestEqual(TEXT("0.1 at 1 px"), RingRadiusFade(1.0), 0.1, NPCFF_TOL);
    TestEqual(TEXT("0 at 0.5 px"), RingRadiusFade(0.5), 0.0, NPCFF_TOL);
    TestEqual(TEXT("0 at 0.25 px"), RingRadiusFade(0.25), 0.0, NPCFF_TOL);
    TestEqual(TEXT("0 at 0 px"), RingRadiusFade(0.0), 0.0, NPCFF_TOL);
    TestEqual(TEXT("log-linear: 0.55 at sqrt(6) px"), RingRadiusFade(FMath::Sqrt(6.0)), 0.55, NPCFF_TOL);
    TestEqual(TEXT("log-linear: 0.05 at sqrt(0.5) px"), RingRadiusFade(FMath::Sqrt(0.5)), 0.05, NPCFF_TOL);
    const double atThree = RingRadiusFade(3.0);
    TestEqual(TEXT("3 px: 0.1 + 0.9 ln 3 / ln 6"), atThree, 0.1 + 0.9 * std::log(3.0) / std::log(6.0), NPCFF_TOL);
    TestTrue(TEXT("3 px about 0.65 (D9, not D3's 0.5)"), atThree > 0.64 && atThree < 0.66);
    TestEqual(TEXT("NaN -> 0"), RingRadiusFade(NPCFF_NAN), 0.0);
    TestEqual(TEXT("negative -> 0"), RingRadiusFade(-3.0), 0.0);
    TestEqual(TEXT("-inf -> 0"), RingRadiusFade(-NPCFF_INF), 0.0);
    TestEqual(TEXT("+inf -> 1"), RingRadiusFade(NPCFF_INF), 1.0);

    // The whole curve against the test's own closed form, non-decreasing on a fine log grid
    int32 mismatches = 0;
    int32 drops = 0;
    double previous = 0.0;
    for (int32 i = 0; i <= 20000; ++i)
    {
        const double px = std::pow(10.0, -3.0 + 7.0 * i / 20000.0);
        const double alpha = RingRadiusFade(px);
        mismatches += FMath::Abs(alpha - NpcFarFieldRefRadiusFade(px)) <= NPCFF_TOL ? 0 : 1;
        drops += alpha + 1.0e-12 >= previous ? 0 : 1;
        previous = alpha;
    }
    TestEqual(TEXT("curve matches the closed form"), mismatches, 0);
    TestEqual(TEXT("curve non-decreasing"), drops, 0);

    // No pop at the three knots
    for (const double knot : { 0.5, 1.0, 6.0 })
    {
        TestTrue(FString::Printf(TEXT("continuous at %.1f px"), knot),
            FMath::Abs(RingRadiusFade(knot * (1.0 + 1.0e-9)) - RingRadiusFade(knot * (1.0 - 1.0e-9))) < 1.0e-6);
    }

    // The ring's radius on screen: f Rt / d, scale free
    TestEqual(TEXT("radius px: 960 Rt away at f 960 is 1 px"), RingRadiusPx(960.0 * 2.0e5, 2.0e5, 960.0), 1.0,
        NPCFF_TOL);
    TestEqual(TEXT("radius px: scale free"), RingRadiusPx(37.0 * 3.0e7, 3.0e7, 960.0), 960.0 / 37.0, NPCFF_TOL);
    TestEqual(TEXT("radius px: linear in f"), RingRadiusPx(10.0, 1.0, 1200.0), 120.0, NPCFF_TOL);
    TestTrue(TEXT("radius px: +inf at the camera"), RingRadiusPx(0.0, 1.0, 960.0) == NPCFF_INF);
    TestEqual(TEXT("radius px: NaN distance"), RingRadiusPx(NPCFF_NAN, 1.0, 960.0), 0.0);
    for (const double bad : { 0.0, -1.0, NPCFF_NAN, NPCFF_INF })
    {
        TestEqual(FString::Printf(TEXT("radius px: Rt %f"), bad), RingRadiusPx(10.0, bad, 960.0), 0.0);
        TestEqual(FString::Printf(TEXT("radius px: focal %f"), bad), RingRadiusPx(10.0, 1.0, bad), 0.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldGapPxTest, "SOLTest.Npc.TunnelFarField.RingGapPx",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 6: RingGapPx is f x max(the angle between the rays to the ring and its neighbour away from the camera,
// the difference of their angular radii Rt / d); on the axis that is exactly f Rt S0 / (d (d + S0)); from the side
// about S0 / Rt = 12 times the ring's radius in px; falls with distance along and across the lane; symmetric in x and
// h, scale free, linear in f; +inf at the camera; 0 for bad input
bool FSOLNpcTunnelFarFieldGapPxTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double f = 960.0;
    for (const double rt : { 1.0, 2.0e5, 1.0e7 })
    {
        const double s0 = NpcFarFieldS0(rt);

        // On the axis: the angular radius difference, exactly
        for (const double dRt : { 0.7, 5.0, 20.0, 100.0, 1000.0 })
        {
            const double d = dRt * rt;
            const double expected = f * rt * s0 / (d * (d + s0));
            TestEqual(FString::Printf(TEXT("axis %.1f Rt"), dRt), RingGapPx(d, 0.0, rt, f), expected,
                1.0e-9 * expected);
            TestEqual(FString::Printf(TEXT("axis %.1f Rt behind"), dRt), RingGapPx(-d, 0.0, rt, f), expected,
                1.0e-9 * expected);
        }
        AddInfo(FString::Printf(TEXT("gap from inside at 100 Rt: %.3f px"), RingGapPx(100.0 * rt, 0.0, rt, f)));

        // Anywhere: the contract's closed form; symmetric in h
        for (const double xRt : { 0.0, 3.0, 40.0, 500.0 })
        {
            for (const double hRt : { 0.2, 2.0, 30.0, 700.0 })
            {
                const double expected = NpcFarFieldRefGapPx(xRt * rt, hRt * rt, rt, f);
                TestEqual(FString::Printf(TEXT("closed form x %.0f h %.1f Rt"), xRt, hRt),
                    RingGapPx(xRt * rt, hRt * rt, rt, f), expected, 1.0e-9 * expected);
                TestEqual(FString::Printf(TEXT("symmetric in h, x %.0f h %.1f Rt"), xRt, hRt),
                    RingGapPx(xRt * rt, -hRt * rt, rt, f), expected, 1.0e-9 * expected);
            }
        }

        // From the side: about 12 times the ring's radius in px
        for (const double hRt : { 200.0, 600.0, 1500.0 })
        {
            const double ratio = RingGapPx(0.0, hRt * rt, rt, f) / RingRadiusPx(hRt * rt, rt, f);
            TestTrue(FString::Printf(TEXT("side at %.0f Rt: about S0 / Rt x the radius"), hRt),
                FMath::Abs(ratio / SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII - 1.0) < 0.01);
        }

        // Falls with distance along the axis, across the lane and along the lane beside it
        int32 rises = 0;
        double previousAxis = NPCFF_INF;
        double previousSide = NPCFF_INF;
        double previousBeside = NPCFF_INF;
        for (int32 i = 0; i <= 2000; ++i)
        {
            const double dist = std::pow(10.0, 3.7 * i / 2000.0) * rt;
            const double axis = RingGapPx(dist, 0.0, rt, f);
            const double side = RingGapPx(0.0, dist, rt, f);
            const double beside = RingGapPx(dist, 50.0 * rt, rt, f);
            rises += axis <= previousAxis * (1.0 + 1.0e-12) ? 0 : 1;
            rises += side <= previousSide * (1.0 + 1.0e-12) ? 0 : 1;
            rises += beside <= previousBeside * (1.0 + 1.0e-12) ? 0 : 1;
            previousAxis = axis;
            previousSide = side;
            previousBeside = beside;
        }
        TestEqual(TEXT("gap falls with distance"), rises, 0);
    }

    // Scale free and linear in f
    TestEqual(TEXT("scale free"), RingGapPx(30.0 * 4.0e6, 7.0 * 4.0e6, 4.0e6, f), RingGapPx(30.0, 7.0, 1.0, f),
        1.0e-9 * RingGapPx(30.0, 7.0, 1.0, f));
    TestEqual(TEXT("linear in f"), RingGapPx(30.0, 7.0, 1.0, 2.0 * f), 2.0 * RingGapPx(30.0, 7.0, 1.0, f),
        1.0e-9 * RingGapPx(30.0, 7.0, 1.0, f));
    TestTrue(TEXT("+inf at the camera"), RingGapPx(0.0, 0.0, 1.0, f) == NPCFF_INF);

    // Bad input
    TestEqual(TEXT("NaN x"), RingGapPx(NPCFF_NAN, 1.0, 1.0, f), 0.0);
    TestEqual(TEXT("NaN h"), RingGapPx(1.0, NPCFF_NAN, 1.0, f), 0.0);
    for (const double bad : { 0.0, -1.0, NPCFF_NAN, NPCFF_INF })
    {
        TestEqual(FString::Printf(TEXT("Rt %f"), bad), RingGapPx(10.0, 1.0, bad, f), 0.0);
        TestEqual(FString::Printf(TEXT("focal %f"), bad), RingGapPx(10.0, 1.0, 1.0, bad), 0.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldGapFadesTest, "SOLTest.Npc.TunnelFarField.GapFades",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D11: the ring gap fade is 1 from 3 px and 0 at and below 2 px, the detail gate 1 from DETAIL_GAP_FADE_FULL_PX and
// 0 at and below DETAIL_GAP_FADE_GONE_PX (round 20 px), both continuous and non-decreasing (never a cut); NaN 0, +inf 1
bool FSOLNpcTunnelFarFieldGapFadesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double ringGone = SOL::NPC_TUNNEL_RING_GAP_FADE_GONE_PX;
    const double ringFull = SOL::NPC_TUNNEL_RING_GAP_FADE_FULL_PX;
    const double detailGone = SOL::NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX;
    const double detailFull = SOL::NPC_TUNNEL_DETAIL_GAP_FADE_FULL_PX;
    TestEqual(TEXT("ring: 0 at 2 px"), RingGapFade(2.0), 0.0, NPCFF_TOL);
    TestEqual(TEXT("ring: 0 at 1 px"), RingGapFade(1.0), 0.0, NPCFF_TOL);
    TestEqual(TEXT("ring: 1 at 3 px"), RingGapFade(3.0), 1.0, NPCFF_TOL);
    TestEqual(TEXT("ring: 1 at 50 px"), RingGapFade(50.0), 1.0, NPCFF_TOL);
    TestTrue(TEXT("ring: between at 2.5 px"), RingGapFade(2.5) > 0.0 && RingGapFade(2.5) < 1.0);
    TestEqual(TEXT("ring: NaN"), RingGapFade(NPCFF_NAN), 0.0);
    TestEqual(TEXT("ring: +inf"), RingGapFade(NPCFF_INF), 1.0);
    TestEqual(TEXT("detail: 0 at the gone edge"), DetailGapFade(detailGone), 0.0, NPCFF_TOL);
    TestEqual(TEXT("detail: 0 at 3 px"), DetailGapFade(3.0), 0.0, NPCFF_TOL);
    TestEqual(TEXT("detail: 1 at the full edge"), DetailGapFade(detailFull), 1.0, NPCFF_TOL);
    TestEqual(TEXT("detail: 1 at 200 px"), DetailGapFade(200.0), 1.0, NPCFF_TOL);
    TestTrue(TEXT("detail: drawn just above the gone edge"), DetailGapFade(detailGone * 1.01) > 0.0);
    TestEqual(TEXT("detail: NaN"), DetailGapFade(NPCFF_NAN), 0.0);
    TestEqual(TEXT("detail: +inf"), DetailGapFade(NPCFF_INF), 1.0);
    AddInfo(FString::Printf(TEXT("detail gate at 20 px: %.3f (band %.1f .. %.1f px)"),
        DetailGapFade(SOL::NPC_TUNNEL_DETAIL_GAP_PX), detailGone, detailFull));

    // Non-decreasing and without a jump on a fine grid round both bands
    int32 drops = 0;
    double largestStep = 0.0;
    double previousRing = 0.0;
    double previousDetail = 0.0;
    for (int32 i = 0; i <= 40000; ++i)
    {
        const double px = 0.5 + 59.5 * i / 40000.0;
        const double ring = RingGapFade(px);
        const double detail = DetailGapFade(px);
        drops += ring + 1.0e-12 >= previousRing && detail + 1.0e-12 >= previousDetail ? 0 : 1;
        if (i > 0)
        {
            largestStep = FMath::Max(largestStep, FMath::Max(ring - previousRing, detail - previousDetail));
        }
        previousRing = ring;
        previousDetail = detail;
    }
    TestEqual(TEXT("gap fades non-decreasing"), drops, 0);
    TestTrue(TEXT("gap fades without a jump"), largestStep < 0.01);
    TestTrue(TEXT("ring band edges as named"), ringGone == 2.0 && ringFull == 3.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldFlatSpacingTest, "SOLTest.Npc.TunnelFarField.FlatSpacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 1: rings sit at arcs k S0 from node A, S0 = 12 Rt, whatever the camera's distance or h (the accordion
// is gone): ring slot w = k + 1 - phase ahead, -(k + phase) behind, offset w S0; every drawn ring strictly between
// the end rings; RingLatticeSpacingM is 12 Rt, 0 for a bad radius
bool FSOLNpcTunnelFarFieldFlatSpacingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    for (const double rt : { 1.0, 2.0e5, 1.0e7 })
    {
        TestEqual(TEXT("lattice spacing 12 Rt"), RingLatticeSpacingM(rt), 12.0 * rt, 1.0e-12 * rt);
    }
    for (const double bad : { 0.0, -1.0, NPCFF_NAN, NPCFF_INF })
    {
        TestEqual(FString::Printf(TEXT("lattice spacing: Rt %f"), bad), RingLatticeSpacingM(bad), 0.0);
    }

    const FNpcFarFieldLane& lane = NPCFF_LANES[2];
    const double rt = lane.TunnelRadiusM;
    const double s0 = NpcFarFieldS0(rt);
    const FVector2D endArcs = NpcFarFieldEndArcs(lane);
    const double footArc = 0.5 * NpcFarFieldLengthM(lane) + 0.37 * s0;
    const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    FNpcFarFieldFailures failures;
    for (const double hRt : { 0.0, 0.5, 5.0, 50.0, 500.0 })
    {
        const FSOLNpcTunnelSlotInput input = NpcFarFieldInput(footArc, hRt * rt, rt, endArcs, 960.0);
        TArray<double> offsets;
        TArray<bool> drawn;
        offsets.Init(0.0, RingSlotCount());
        drawn.Init(false, RingSlotCount());
        for (int32 slot = 0; slot < RingSlotCount(); ++slot)
        {
            FSOLNpcTunnelRing ring;
            drawn[slot] = RingSlot(slot, input, ring);
            if (!drawn[slot])
            {
                continue;
            }
            offsets[slot] = ring.AxialOffsetM;
            // The slot's warp coordinate and offset on the flat lattice
            const double w = slot < perSide ? static_cast<double>(slot + 1) - input.Phase
                : -(static_cast<double>(slot - perSide) + input.Phase);
            if (!(FMath::Abs(ring.W - w) <= 1.0e-9 && FMath::Abs(ring.AxialOffsetM - w * s0) <= 1.0e-9 * s0))
            {
                failures.Add(FString::Printf(TEXT("h %.1f Rt slot %d: w %.9f x %.6e"), hRt, slot, ring.W,
                    ring.AxialOffsetM));
            }
            // On the lattice: (foot arc + x) / S0 is whole
            const double k = (footArc + ring.AxialOffsetM) / s0;
            if (!(FMath::Abs(k - FMath::RoundToDouble(k)) <= 1.0e-6))
            {
                failures.Add(FString::Printf(TEXT("h %.1f Rt slot %d: arc / S0 = %.9f"), hRt, slot, k));
            }
            if (!(ring.AxialOffsetM > input.EndArcAM && ring.AxialOffsetM < input.EndArcBM))
            {
                failures.Add(FString::Printf(TEXT("h %.1f Rt slot %d outside the end rings"), hRt, slot));
            }
        }
        // Neighbouring slots one spacing apart wherever both draw (ahead, behind, and across the foot point)
        for (int32 slot = 0; slot + 1 < perSide; ++slot)
        {
            if (drawn[slot] && drawn[slot + 1] && !(FMath::Abs(offsets[slot + 1] - offsets[slot] - s0) <= 1.0e-9 * s0))
            {
                failures.Add(FString::Printf(TEXT("h %.1f Rt: ahead slots %d, %d not one S0 apart"), hRt, slot,
                    slot + 1));
            }
            const int32 behind = perSide + slot;
            if (drawn[behind] && drawn[behind + 1]
                && !(FMath::Abs(offsets[behind] - offsets[behind + 1] - s0) <= 1.0e-9 * s0))
            {
                failures.Add(FString::Printf(TEXT("h %.1f Rt: behind slots %d, %d not one S0 apart"), hRt, behind,
                    behind + 1));
            }
        }
        if (drawn[0] && drawn[perSide] && !(FMath::Abs(offsets[0] - offsets[perSide] - s0) <= 1.0e-9 * s0))
        {
            failures.Add(FString::Printf(TEXT("h %.1f Rt: first ahead and behind not one S0 apart"), hRt));
        }
    }
    if (!TestEqual(TEXT("flat lattice at every h"), failures.Count, 0))
    {
        AddInfo(TEXT("first: ") + failures.First);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldPhaseTest, "SOLTest.Npc.TunnelFarField.DirectPhase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 2: RingLatticePhase is frac(foot arc / S0) in [0, 1), computed directly in double for arcs 0 .. 7.6e12 m
// and negative arcs (no history); never 1; 0 for bad input; across the wrap the slot labels shift by one and the set
// of drawn lattice rings and their alphas stay the same
bool FSOLNpcTunnelFarFieldPhaseTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    int32 badPhases = 0;
    for (const double rt : { 2.0e5, 3.0e6, 1.0e7 })
    {
        const double s0 = NpcFarFieldS0(rt);
        for (const double arc : { 0.0, 1.234e6, 3.7e8, 1.0e11, 4.5e12, 7.57e12, -5.0e5, -3.7e8, -7.57e12 })
        {
            const double phase = RingLatticePhase(arc, rt);
            badPhases += phase >= 0.0 && phase < 1.0 ? 0 : 1;
            badPhases += NpcFarFieldWrapDistance(phase, NpcFarFieldFrac(arc / s0)) <= 1.0e-9 ? 0 : 1;
        }
        TestEqual(TEXT("a quarter spacing"), RingLatticePhase(0.25 * s0, rt), 0.25, 1.0e-12);
        TestEqual(TEXT("a quarter spacing behind A wraps up"), RingLatticePhase(-0.25 * s0, rt), 0.75, 1.0e-12);
        TestTrue(TEXT("whole spacings: 0 (mod 1)"), NpcFarFieldWrapDistance(RingLatticePhase(1000.0 * s0, rt), 0.0)
            <= 1.0e-9);
    }
    TestEqual(TEXT("phases in [0, 1) and frac(arc / S0)"), badPhases, 0);
    const double tiny = RingLatticePhase(-1.0e-30, 1.0);
    TestTrue(TEXT("never 1 (a tiny negative arc)"), tiny >= 0.0 && tiny < 1.0);
    TestEqual(TEXT("NaN arc -> 0"), RingLatticePhase(NPCFF_NAN, 1.0), 0.0);
    TestEqual(TEXT("infinite arc -> 0"), RingLatticePhase(NPCFF_INF, 1.0), 0.0);
    for (const double bad : { 0.0, -1.0, NPCFF_NAN, NPCFF_INF })
    {
        TestEqual(FString::Printf(TEXT("Rt %f -> 0"), bad), RingLatticePhase(1.0e6, bad), 0.0);
    }

    // Across the wrap: the same lattice rings at the same alphas (production phase in the slot input)
    FNpcFarFieldFailures failures;
    for (const FNpcFarFieldLane& lane : NPCFF_LANES)
    {
        const double rt = lane.TunnelRadiusM;
        const double s0 = NpcFarFieldS0(rt);
        const FVector2D endArcs = NpcFarFieldEndArcs(lane);
        const double wrapArc = FMath::FloorToDouble(0.5 * NpcFarFieldLengthM(lane) / s0) * s0;
        for (const double hRt : { 0.0, 0.7, 300.0 })
        {
            TArray<FNpcFarFieldSeen> below;
            TArray<FNpcFarFieldSeen> above;
            for (int32 side = 0; side < 2; ++side)
            {
                const double footArc = wrapArc + (side == 0 ? -1.0e-6 : 1.0e-6) * s0;
                FSOLNpcTunnelSlotInput input = NpcFarFieldInput(footArc, hRt * rt, rt, endArcs, 960.0);
                input.Phase = RingLatticePhase(footArc, rt);
                NpcFarFieldSlotRings(footArc, input, side == 0 ? below : above);
            }
            // Every ring of one side with a visible alpha is on the other side at the same alpha
            for (int32 pass = 0; pass < 2; ++pass)
            {
                const TArray<FNpcFarFieldSeen>& from = pass == 0 ? below : above;
                const TArray<FNpcFarFieldSeen>& to = pass == 0 ? above : below;
                for (const FNpcFarFieldSeen& ring : from)
                {
                    if (ring.Alpha <= 1.0e-6)
                    {
                        continue;
                    }
                    const FNpcFarFieldSeen* match = to.FindByPredicate([&ring](const FNpcFarFieldSeen& other)
                    {
                        return other.Index == ring.Index;
                    });
                    if (match == nullptr || !(FMath::Abs(match->Alpha - ring.Alpha) <= 1.0e-5))
                    {
                        failures.Add(FString::Printf(TEXT("%s h %.1f Rt: ring %lld changes across the wrap"),
                            lane.Name, hRt, static_cast<long long>(ring.Index)));
                    }
                }
            }
        }
    }
    if (!TestEqual(TEXT("the ring set survives the wrap"), failures.Count, 0))
    {
        AddInfo(TEXT("first: ") + failures.First);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldWindowTest, "SOLTest.Npc.TunnelFarField.RingWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 3 at the D2 lane lengths (1,920 to 757,000 Rt): for cameras before node A, near it, mid-lane, near B and
// past B, on the axis, inside, beside and far from the tube, at the base and max FOV, every ring a slot draws is a
// lattice ring at the contract's alpha (cap x end x near x radius x gap), drawn once, and every lattice ring with an
// alpha above 0 is drawn; outside the 80-slot window a ring's uncapped alpha is at most the 1 px knee (the window cuts
// only rings already below 1 px, through the cap fade)
bool FSOLNpcTunnelFarFieldWindowTest::RunTest(const FString& /*parameters*/)
{
    FNpcFarFieldFailures failures;
    int32 views = 0;
    for (const FNpcFarFieldLane& lane : NPCFF_LANES)
    {
        const double rt = lane.TunnelRadiusM;
        const double s0 = NpcFarFieldS0(rt);
        const FVector2D endArcs = NpcFarFieldEndArcs(lane);
        TestTrue(FString::Printf(TEXT("%s drawable"), lane.Name), endArcs.Y > endArcs.X);
        const double footArcs[] = { endArcs.X - 400.0 * rt, endArcs.X + 2.0 * rt, endArcs.X + 500.0 * rt,
            0.5 * NpcFarFieldLengthM(lane) + 0.123 * s0, endArcs.Y - 37.3 * rt, endArcs.Y + 100.0 * rt };
        for (const double footArc : footArcs)
        {
            for (const double hRt : { 0.0, 0.3, 1.0, 2.5, 8.0, 60.0, 300.0, 900.0, 1500.0, 2500.0 })
            {
                for (const double fov : { SOL::SHIP_CAMERA_BASE_FOV_DEG, SOL::SHIP_CAMERA_MAX_FOV_DEG })
                {
                    const double f = NpcFarFieldFocal(fov);
                    const FSOLNpcTunnelSlotInput input = NpcFarFieldInput(footArc, hRt * rt, rt, endArcs, f);
                    const FString label = FString::Printf(TEXT("%s arc %.6e h %.1f Rt fov %.0f"), lane.Name, footArc,
                        hRt, fov);
                    NpcFarFieldCheckRingWindow(footArc, input, true, label, failures);
                    ++views;
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d views checked"), views));
    if (!TestEqual(TEXT("every visible lattice ring drawn exactly once at its alpha"), failures.Count, 0))
    {
        AddInfo(TEXT("first: ") + failures.First);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldSubsampleTest, "SOLTest.Npc.TunnelFarField.NeverSubsampled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D11: the lattice is never thinned. Moving the foot point by whole spacings far from the ends gives the same offsets
// and alphas with the lattice indices shifted; the drawn rings form one run of consecutive lattice indices wherever the
// near fade does not open a hole at the foot (beside the tube); from inside on the axis far rings fade with distance,
// one after the next, none skipped
bool FSOLNpcTunnelFarFieldSubsampleTest::RunTest(const FString& /*parameters*/)
{
    const FNpcFarFieldLane& lane = NPCFF_LANES[2];
    const double rt = lane.TunnelRadiusM;
    const double s0 = NpcFarFieldS0(rt);
    const FVector2D endArcs = NpcFarFieldEndArcs(lane);
    const double footArc = 0.5 * NpcFarFieldLengthM(lane) + 0.41 * s0;
    FNpcFarFieldFailures failures;
    for (const double hRt : { 0.0, 0.5, 40.0, 300.0 })
    {
        TArray<FNpcFarFieldSeen> base;
        NpcFarFieldSlotRings(footArc, NpcFarFieldInput(footArc, hRt * rt, rt, endArcs, 960.0), base);
        for (const int32 shift : { 1, 7, 1000 })
        {
            const double shifted = footArc + shift * s0;
            TArray<FNpcFarFieldSeen> moved;
            NpcFarFieldSlotRings(shifted, NpcFarFieldInput(shifted, hRt * rt, rt, endArcs, 960.0), moved);
            if (moved.Num() != base.Num())
            {
                failures.Add(FString::Printf(TEXT("h %.1f Rt shift %d: %d rings vs %d"), hRt, shift, moved.Num(),
                    base.Num()));
                continue;
            }
            for (int32 i = 0; i < base.Num(); ++i)
            {
                if (moved[i].Index != base[i].Index + shift || !(FMath::Abs(moved[i].X - base[i].X) <= 1.0e-6 * s0)
                    || !(FMath::Abs(moved[i].Alpha - base[i].Alpha) <= NPCFF_ALPHA_TOL))
                {
                    failures.Add(FString::Printf(TEXT("h %.1f Rt shift %d: ring %d differs"), hRt, shift, i));
                }
            }
        }

        // Beside the tube the drawn rings are one run of consecutive lattice indices
        if (hRt >= 40.0 && base.Num() > 0)
        {
            int64 lowest = base[0].Index;
            int64 highest = base[0].Index;
            for (const FNpcFarFieldSeen& ring : base)
            {
                lowest = FMath::Min(lowest, ring.Index);
                highest = FMath::Max(highest, ring.Index);
            }
            if (highest - lowest + 1 != base.Num())
            {
                failures.Add(FString::Printf(TEXT("h %.1f Rt: %d rings over %lld lattice indices"), hRt, base.Num(),
                    static_cast<long long>(highest - lowest + 1)));
            }
        }
    }

    // From inside on the axis: ahead slots 1, 2, ... draw consecutive rings with falling alpha until they stop
    const FSOLNpcTunnelSlotInput axis = NpcFarFieldInput(footArc, 0.0, rt, endArcs, 960.0);
    bool bStopped = false;
    double previousAlpha = NPCFF_INF;
    int32 drawnAhead = 0;
    for (int32 slot = 1; slot < SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE; ++slot)
    {
        FSOLNpcTunnelRing ring;
        const bool bDrawn = SOLNpcTunnel::RingSlot(slot, axis, ring);
        if (bDrawn && bStopped)
        {
            failures.Add(FString::Printf(TEXT("axis: slot %d drawn after a gap"), slot));
        }
        if (bDrawn)
        {
            ++drawnAhead;
            if (ring.Alpha > previousAlpha + 1.0e-12)
            {
                failures.Add(FString::Printf(TEXT("axis: slot %d brighter than the one before"), slot));
            }
            previousAlpha = ring.Alpha;
        }
        bStopped |= !bDrawn;
    }
    TestTrue(TEXT("rings drawn ahead from inside"), drawnAhead > 0);
    AddInfo(FString::Printf(TEXT("from inside on the axis %d rings ahead pass the gap fade"), drawnAhead));
    if (!TestEqual(TEXT("never subsampled"), failures.Count, 0))
    {
        AddInfo(TEXT("first: ") + failures.First);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldNoPopsAlongTest, "SOLTest.Npc.TunnelFarField.NoPopsAlong",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 4 along the lane (the window recentring): 12,000-step sweeps of the camera along the axis, across an end
// ring, on the axis, inside off the axis, beside and far beside the tube, on the shortest and the longest lane (arcs
// up to 7.6e12 m): no lattice ring's alpha changes by more than NPCFF_POP_LIMIT in a step and no ring is drawn twice
// (rings enter and leave the window only through the cap fade)
bool FSOLNpcTunnelFarFieldNoPopsAlongTest::RunTest(const FString& /*parameters*/)
{
    struct FSweep
    {
        int32 Lane = 0;
        bool bNearB = false;
        double RadialRadii = 0.0;
        double StepRadii = 0.0;
    };
    const FSweep sweeps[] = {
        { 0, false, 0.0, 0.01 }, { 0, false, 0.6, 0.01 }, { 0, false, 20.0, 0.01 }, { 0, false, 400.0, 0.1 },
        { 4, true, 0.0, 0.01 }, { 4, true, 0.6, 0.01 }, { 4, true, 20.0, 0.01 }, { 2, false, 400.0, 0.1 },
    };
    for (const FSweep& sweep : sweeps)
    {
        const FNpcFarFieldLane& lane = NPCFF_LANES[sweep.Lane];
        const double rt = lane.TunnelRadiusM;
        const FVector2D endArcs = NpcFarFieldEndArcs(lane);
        const double span = sweep.StepRadii * rt * NPCFF_SWEEP_STEPS;
        // Start so the sweep crosses end ring A (or ends past end ring B)
        const double start = sweep.bNearB ? endArcs.Y - 0.75 * span : endArcs.X - 0.25 * span;
        const double f = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX;
        int32 twice = 0;
        FString where;
        const double maxJump = NpcFarFieldSweepMaxJump(lane,
            [&](const int32 step, double& footArcM, double& radialM, double& focalRefPx)
            {
                footArcM = start + step * sweep.StepRadii * rt;
                radialM = sweep.RadialRadii * rt;
                focalRefPx = f;
            }, twice, where);
        const FString label = FString::Printf(TEXT("%s h %.1f Rt"), lane.Name, sweep.RadialRadii);
        TestEqual(label + TEXT(": no ring drawn twice"), twice, 0);
        if (!TestTrue(label + TEXT(": no pop"), maxJump <= NPCFF_POP_LIMIT))
        {
            AddInfo(FString::Printf(TEXT("%s: largest jump %.4f at %s"), *label, maxJump, *where));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldNoPopsAcrossTest, "SOLTest.Npc.TunnelFarField.NoPopsAcross",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 4 across the lane and in the view: the camera moves from 0.001 Rt off the axis to 3,000 Rt (geometric
// steps), and the focal length sweeps 400 -> 1,400 reference px (zoom and FOV) at three distances; no lattice ring's
// alpha jumps and none is drawn twice
bool FSOLNpcTunnelFarFieldNoPopsAcrossTest::RunTest(const FString& /*parameters*/)
{
    const FNpcFarFieldLane& lane = NPCFF_LANES[1];
    const double rt = lane.TunnelRadiusM;
    const double s0 = NpcFarFieldS0(rt);
    const double footArc = 0.5 * NpcFarFieldLengthM(lane) + 0.08 * s0;  // A ring about 1 Rt behind the foot
    const double f = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX;

    // Across: h from 0.001 Rt to 3,000 Rt
    const double hGrowth = std::pow(3.0e6, 1.0 / NPCFF_SWEEP_STEPS);
    int32 twice = 0;
    FString where;
    double maxJump = NpcFarFieldSweepMaxJump(lane,
        [&](const int32 step, double& footArcM, double& radialM, double& focalRefPx)
        {
            footArcM = footArc;
            radialM = 1.0e-3 * rt * std::pow(hGrowth, step);
            focalRefPx = f;
        }, twice, where);
    TestEqual(TEXT("across: no ring drawn twice"), twice, 0);
    if (!TestTrue(TEXT("across: no pop"), maxJump <= NPCFF_POP_LIMIT))
    {
        AddInfo(FString::Printf(TEXT("across: largest jump %.4f at %s"), maxJump, *where));
    }

    // The focal length (viewport, FOV) at three distances
    const double fGrowth = std::pow(1400.0 / 400.0, 1.0 / NPCFF_SWEEP_STEPS);
    for (const double hRt : { 0.0, 50.0, 700.0 })
    {
        maxJump = NpcFarFieldSweepMaxJump(lane,
            [&](const int32 step, double& footArcM, double& radialM, double& focalRefPx)
            {
                footArcM = footArc;
                radialM = hRt * rt;
                focalRefPx = 400.0 * std::pow(fGrowth, step);
            }, twice, where);
        const FString label = FString::Printf(TEXT("focal sweep h %.0f Rt"), hRt);
        TestEqual(label + TEXT(": no ring drawn twice"), twice, 0);
        if (!TestTrue(label + TEXT(": no pop"), maxJump <= NPCFF_POP_LIMIT))
        {
            AddInfo(FString::Printf(TEXT("%s: largest jump %.4f at %s"), *label, maxJump, *where));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldResolutionTest, "SOLTest.Npc.TunnelFarField.ResolutionSameLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D8: at a fixed FOV and aspect, 1280x720, 1920x1080 and 3840x2160 give every ring, dot and chevron slot the same
// alpha (the window and the look do not depend on the resolution); a ring 3 px wide at 1080p and 6 px at 4K fades alike
bool FSOLNpcTunnelFarFieldResolutionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    TestEqual(TEXT("3 px at 1080p fades like 6 px at 4K"), RingRadiusFade(ToReferencePx(3.0, 1080.0)),
        RingRadiusFade(ToReferencePx(6.0, 2160.0)), NPCFF_TOL);
    const FNpcFarFieldLane& lane = NPCFF_LANES[1];
    const double rt = lane.TunnelRadiusM;
    const FVector2D endArcs = NpcFarFieldEndArcs(lane);
    const double footArc = 0.5 * NpcFarFieldLengthM(lane) + 0.61 * NpcFarFieldS0(rt);
    const double reference = FocalLengthRefPx(SOL::SHIP_CAMERA_BASE_FOV_DEG, 1920.0, 1080.0);
    int32 differences = 0;
    for (const double height : { 720.0, 2160.0 })
    {
        const double f = FocalLengthRefPx(SOL::SHIP_CAMERA_BASE_FOV_DEG, NPCFF_ASPECT * height, height);
        for (const double hRt : { 0.0, 0.8, 30.0, 400.0 })
        {
            const FSOLNpcTunnelSlotInput atReference = NpcFarFieldInput(footArc, hRt * rt, rt, endArcs, reference);
            const FSOLNpcTunnelSlotInput atHeight = NpcFarFieldInput(footArc, hRt * rt, rt, endArcs, f);
            // Ring slots
            for (int32 slot = 0; slot < RingSlotCount(); ++slot)
            {
                FSOLNpcTunnelRing a;
                FSOLNpcTunnelRing b;
                const bool bA = RingSlot(slot, atReference, a);
                const bool bB = RingSlot(slot, atHeight, b);
                differences += bA == bB && (!bA || FMath::Abs(a.Alpha - b.Alpha) <= 1.0e-9) ? 0 : 1;
            }
            // Dot and chevron slots
            for (int32 slot = 0; slot < DetailSlotCount(); ++slot)
            {
                for (int32 dot = 0; dot < SOL::NPC_TUNNEL_DOTS_PER_GAP; ++dot)
                {
                    FSOLNpcTunnelRing a;
                    FSOLNpcTunnelRing b;
                    const bool bA = DotSlot(slot, dot, atReference, a);
                    const bool bB = DotSlot(slot, dot, atHeight, b);
                    differences += bA == bB && (!bA || FMath::Abs(a.Alpha - b.Alpha) <= 1.0e-9) ? 0 : 1;
                }
                for (int32 chevron = 0; chevron < SOL::NPC_TUNNEL_CHEVRONS_PER_GAP; ++chevron)
                {
                    FSOLNpcTunnelRing a;
                    FSOLNpcTunnelRing b;
                    const bool bA = ChevronSlot(slot, chevron, atReference, a);
                    const bool bB = ChevronSlot(slot, chevron, atHeight, b);
                    differences += bA == bB && (!bA || FMath::Abs(a.Alpha - b.Alpha) <= 1.0e-9) ? 0 : 1;
                }
            }
        }
    }
    TestEqual(TEXT("the same alphas at 720p, 1080p and 4K"), differences, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldDetailTest, "SOLTest.Npc.TunnelFarField.DetailFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 7 (D4, D11, D12): a dot fades by its ring's radius on screen, a chevron by its world length on screen
// (0.52 Rt, 3 px at about 166 Rt), both on the D9 curve and gated by the ring gap (DetailGapFade of RingGapPx at the
// element), the chevron far fade kept; the dot is never drawn below the 3 px floor; every dot and chevron position with
// an alpha above 0 is drawn once at that alpha; the detail window covers every position the gate lets through
bool FSOLNpcTunnelFarFieldDetailTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double f = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX;

    // Sizes on screen
    const double chevronLength = 2.0 * SOL::NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII;
    TestEqual(TEXT("chevron length px"), ChevronLengthPx(166.0 * 2.0e5, 2.0e5, f), f * chevronLength / 166.0,
        NPCFF_TOL);
    TestTrue(TEXT("a chevron is about 3 px at 166 Rt"), FMath::Abs(ChevronLengthPx(166.0, 1.0, 960.0) - 3.0) < 0.05);
    TestTrue(TEXT("chevron length: +inf at the camera"), ChevronLengthPx(0.0, 1.0, f) == NPCFF_INF);
    TestEqual(TEXT("chevron length: NaN"), ChevronLengthPx(NPCFF_NAN, 1.0, f), 0.0);
    TestEqual(TEXT("chevron length: bad Rt"), ChevronLengthPx(1.0, 0.0, f), 0.0);
    TestEqual(TEXT("chevron length: bad focal"), ChevronLengthPx(1.0, 1.0, 0.0), 0.0);
    const double dotFloor = SOL::NPC_TUNNEL_DOT_SIZE_PX;
    TestEqual(TEXT("dot floor 3 px"), dotFloor, 3.0, NPCFF_TOL);
    TestEqual(TEXT("a near dot at its world size"), DotDrawnSizePx(1.0 * 2.0e5, 2.0e5, f),
        f * SOL::NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII, NPCFF_TOL);
    for (const double dRt : { 11.0, 33.0, 300.0, 1.0e5 })
    {
        TestEqual(FString::Printf(TEXT("dot at %.0f Rt held at the floor"), dRt), DotDrawnSizePx(dRt, 1.0, f),
            dotFloor, NPCFF_TOL);
    }
    TestEqual(TEXT("dot size: NaN -> the floor"), DotDrawnSizePx(NPCFF_NAN, 1.0, f), dotFloor);
    TestEqual(TEXT("dot size: bad Rt -> the floor"), DotDrawnSizePx(1.0, 0.0, f), dotFloor);

    // The detail window covers the gate: no position at |x| >= (DETAIL_PER_SIDE - 3) S0 passes it (f up to the
    // reference, any h)
    const int32 detailPerSide = SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE;
    int32 leaks = 0;
    FString firstLeak;
    for (const double fov : { SOL::SHIP_CAMERA_BASE_FOV_DEG, SOL::SHIP_CAMERA_MAX_FOV_DEG })
    {
        const double focal = NpcFarFieldFocal(fov);
        for (int32 i = 0; i <= 400; ++i)
        {
            const double hRt = std::pow(10.0, -2.0 + 6.0 * i / 400.0);
            for (double w = detailPerSide - 3.0; w <= detailPerSide + 3.0; w += 0.25)
            {
                const double gate = DetailGapFade(RingGapPx(w * NpcFarFieldS0(1.0), hRt, 1.0, focal));
                if (gate > 0.0)
                {
                    if (leaks == 0)
                    {
                        firstLeak = FString::Printf(TEXT("fov %.0f h %.3f Rt w %.2f gate %.4f"), fov, hRt, w, gate);
                    }
                    ++leaks;
                }
            }
        }
    }
    if (!TestEqual(TEXT("the detail window covers the gap gate"), leaks, 0))
    {
        AddInfo(TEXT("first: ") + firstLeak);
    }

    // Slot alphas against the reference, each position drawn once, every visible position drawn
    FNpcFarFieldFailures failures;
    const FNpcFarFieldLane& lane = NPCFF_LANES[1];
    const double rt = lane.TunnelRadiusM;
    const double s0 = NpcFarFieldS0(rt);
    const FVector2D endArcs = NpcFarFieldEndArcs(lane);
    for (const double footArc : { 0.5 * NpcFarFieldLengthM(lane) + 0.33 * s0, endArcs.X + 4.0 * rt })
    {
        for (const double hRt : { 0.0, 0.4, 2.0, 10.0, 100.0, 288.0, 1000.0 })
        {
            const FSOLNpcTunnelSlotInput input = NpcFarFieldInput(footArc, hRt * rt, rt, endArcs, f);
            for (int32 kind = 0; kind < 2; ++kind)
            {
                const bool bChevron = kind == 1;
                const int32 perGap = bChevron ? SOL::NPC_TUNNEL_CHEVRONS_PER_GAP : SOL::NPC_TUNNEL_DOTS_PER_GAP;
                const double step = s0 / perGap;
                TArray<int64> drawnPositions;
                // What the slots draw
                for (int32 slot = 0; slot < DetailSlotCount(); ++slot)
                {
                    for (int32 element = 0; element < perGap; ++element)
                    {
                        FSOLNpcTunnelRing out;
                        const bool bDrawn = bChevron ? ChevronSlot(slot, element, input, out)
                            : DotSlot(slot, element, input, out);
                        if (!bDrawn)
                        {
                            continue;
                        }
                        const double position = (footArc + out.AxialOffsetM) / step;
                        const int64 index = static_cast<int64>(std::llround(position));
                        const double latticeX = static_cast<double>(index) * step - footArc;
                        if (!(FMath::Abs(position - static_cast<double>(index)) <= 1.0e-6))
                        {
                            failures.Add(FString::Printf(TEXT("kind %d h %.1f: element off its positions"), kind,
                                hRt));
                        }
                        if (drawnPositions.Contains(index))
                        {
                            failures.Add(FString::Printf(TEXT("kind %d h %.1f: position drawn twice"), kind, hRt));
                        }
                        drawnPositions.Add(index);
                        const double expected = NpcFarFieldDetailAlphaNoCap(latticeX, input, bChevron);
                        if (!(FMath::Abs(out.Alpha - expected) <= NPCFF_ALPHA_TOL))
                        {
                            failures.Add(FString::Printf(TEXT("kind %d h %.1f x %.2f Rt: alpha %.6f vs %.6f"), kind,
                                hRt, latticeX / rt, out.Alpha, expected));
                        }
                        if (DetailGapFade(RingGapPx(latticeX, input.RadialM, rt, f)) <= 0.0)
                        {
                            failures.Add(FString::Printf(TEXT("kind %d h %.1f: drawn below the gap gate"), kind,
                                hRt));
                        }
                    }
                }
                // Every position near the foot point with a reference alpha above 0 is drawn
                const int64 footIndex = static_cast<int64>(FMath::FloorToDouble(footArc / step));
                for (int64 index = footIndex - perGap * detailPerSide; index <= footIndex + perGap * detailPerSide;
                    ++index)
                {
                    const double x = static_cast<double>(index) * step - footArc;
                    if (NpcFarFieldDetailAlphaNoCap(x, input, bChevron) > NPCFF_TOL && !drawnPositions.Contains(index))
                    {
                        failures.Add(FString::Printf(TEXT("kind %d h %.1f: position at %.2f Rt not drawn"), kind,
                            hRt, x / rt));
                    }
                }
            }
        }
    }
    if (!TestEqual(TEXT("dots and chevrons at their alphas"), failures.Count, 0))
    {
        AddInfo(TEXT("first: ") + failures.First);
    }

    // The gate: from inside on the axis a near dot shows and a 50 Rt one is gated out; nothing from 2,000 Rt beside
    const double midArc = 0.5 * NpcFarFieldLengthM(lane) + 0.5 * s0 - 6.0 * rt;
    const FSOLNpcTunnelSlotInput inside = NpcFarFieldInput(midArc, 0.0, rt, endArcs, f);
    TestTrue(TEXT("gate open 6 Rt ahead on the axis"), DetailGapFade(RingGapPx(6.0 * rt, 0.0, rt, f)) > 0.0);
    TestEqual(TEXT("gate shut 50 Rt ahead on the axis"), DetailGapFade(RingGapPx(50.0 * rt, 0.0, rt, f)), 0.0);
    int32 insideDots = 0;
    int32 farDetails = 0;
    const FSOLNpcTunnelSlotInput farBeside = NpcFarFieldInput(midArc, 2000.0 * rt, rt, endArcs, f);
    for (int32 slot = 0; slot < DetailSlotCount(); ++slot)
    {
        for (int32 element = 0; element < SOL::NPC_TUNNEL_DOTS_PER_GAP; ++element)
        {
            FSOLNpcTunnelRing out;
            insideDots += DotSlot(slot, element, inside, out) ? 1 : 0;
            farDetails += DotSlot(slot, element, farBeside, out) ? 1 : 0;
        }
        for (int32 element = 0; element < SOL::NPC_TUNNEL_CHEVRONS_PER_GAP; ++element)
        {
            FSOLNpcTunnelRing out;
            farDetails += ChevronSlot(slot, element, farBeside, out) ? 1 : 0;
        }
    }
    TestTrue(TEXT("dots drawn near the camera inside"), insideDots > 0);
    TestEqual(TEXT("no dots or chevrons from 2,000 Rt beside"), farDetails, 0);
    AddInfo(FString::Printf(TEXT("dots drawn from inside on the axis: %d"), insideDots));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldFloatTest, "SOLTest.Npc.TunnelFarField.FloatPathAtMaxLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 8 on every D2 lane up to 757,000 Rt (7.57e12 m): RingSlotOffsetM (the shader's float path: slot, phase,
// Rt) matches the double offset w S0 within 1e-3 Rt for rings within 64 Rt of the foot point and within 1e-5 of the
// distance beyond, always finite (no exp, no overflow); the tunnel texels carry the phase within 1e-6 and the end
// arcs relative to the foot within 0.05 Rt
bool FSOLNpcTunnelFarFieldFloatTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    FNpcFarFieldFailures failures;
    int32 checked = 0;
    for (const FNpcFarFieldLane& lane : NPCFF_LANES)
    {
        const double rt = lane.TunnelRadiusM;
        const double s0 = NpcFarFieldS0(rt);
        const FVector2D endArcs = NpcFarFieldEndArcs(lane);
        for (const double footArc : { endArcs.X + 5.3 * rt, 0.5 * NpcFarFieldLengthM(lane) + 0.77 * s0,
            endArcs.Y - 5.9 * rt })
        {
            const double phase = RingLatticePhase(footArc, rt);
            // Slot offsets in float against double
            for (int32 slot = 0; slot < RingSlotCount(); ++slot)
            {
                const double w = slot < perSide ? static_cast<double>(slot + 1) - phase
                    : -(static_cast<double>(slot - perSide) + phase);
                const double expected = w * s0;
                const float rebuilt = RingSlotOffsetM(slot, static_cast<float>(phase), static_cast<float>(rt));
                const double tolerance = FMath::Abs(expected) <= 64.0 * rt ? 1.0e-3 * rt
                    : FMath::Max(1.0e-3 * rt, 1.0e-5 * FMath::Abs(expected));
                ++checked;
                if (!FMath::IsFinite(rebuilt) || !(FMath::Abs(static_cast<double>(rebuilt) - expected) <= tolerance))
                {
                    failures.Add(FString::Printf(TEXT("%s arc %.6e slot %d: %.9e vs %.9e"), lane.Name, footArc, slot,
                        static_cast<double>(rebuilt), expected));
                }
            }

            // The texels the shader reads: phase in row 2 .w, end arcs relative to the foot in row 3 .xy
            FSOLNpcTunnelLaneData data;
            data.Lane = 0;
            data.TunnelRadiusM = rt;
            data.Axis = FVector3d(1.0, 0.0, 0.0);
            data.Up = FVector3d(0.0, 0.0, 1.0);
            data.Fade = 1.0;
            data.Phase = phase;
            data.EndArcAM = endArcs.X - footArc;
            data.EndArcBM = endArcs.Y - footArc;
            TArray<FVector4f> texels;
            texels.SetNumZeroed(4 * width);
            WriteTunnelTexels(MakeArrayView(&data, 1), texels);
            const FVector4f& row2 = texels[2 * width];
            const FVector4f& row3 = texels[3 * width];
            TestEqual(FString::Printf(TEXT("%s: phase texel"), lane.Name), static_cast<double>(row2.W), phase, 1.0e-6);
            TestEqual(FString::Printf(TEXT("%s: end arc A texel"), lane.Name), static_cast<double>(row3.X),
                data.EndArcAM, 0.05 * rt);
            TestEqual(FString::Printf(TEXT("%s: end arc B texel"), lane.Name), static_cast<double>(row3.Y),
                data.EndArcBM, 0.05 * rt);
        }
    }
    TestEqual(TEXT("every slot checked"), checked, 5 * 3 * RingSlotCount());
    if (!TestEqual(TEXT("float path within tolerance at every lane length"), failures.Count, 0))
    {
        AddInfo(TEXT("first: ") + failures.First);
    }

    // Bad input to the float path
    TestEqual(TEXT("float: slot -1"), RingSlotOffsetM(-1, 0.3f, 1.0f), 0.0f);
    TestEqual(TEXT("float: slot out of range"), RingSlotOffsetM(RingSlotCount(), 0.3f, 1.0f), 0.0f);
    TestEqual(TEXT("float: bad Rt"), RingSlotOffsetM(0, 0.3f, 0.0f), 0.0f);
    TestEqual(TEXT("float: a phase of 1.3 wraps to 0.3"), RingSlotOffsetM(3, 1.3f, 1.0f),
        RingSlotOffsetM(3, 0.3f, 1.0f), 1.0e-4f);
    TestTrue(TEXT("float: NaN phase stays finite"), FMath::IsFinite(RingSlotOffsetM(3, NAN, 1.0f)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldHonestTest, "SOLTest.Npc.TunnelFarField.HonestFarView",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 9 (D2, accepted): from beyond d_max = f Rt / 0.5 px (beside a lane's middle, before node A or past node
// B on the axis) no repeating ring, dot or chevron draws; between 1 px and 0.5 px every drawn ring is at most the knee
// alpha; at mid distance a short run of rings shows near the foot; the end rings from beyond d_max are gone (alpha 0:
// they follow the pixel curve to 0 at 0.5 px like every ring, user 2026-11-18, D14) and would be drawn at a radius of
// at least 1 px
bool FSOLNpcTunnelFarFieldHonestTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double f = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX;
    for (const FNpcFarFieldLane& lane : NPCFF_LANES)
    {
        const double rt = lane.TunnelRadiusM;
        const double s0 = NpcFarFieldS0(rt);
        const FVector2D endArcs = NpcFarFieldEndArcs(lane);
        const double dMax = f * rt / SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX;
        const double mid = 0.5 * NpcFarFieldLengthM(lane) + 0.2 * s0;
        struct FView
        {
            double FootArcM = 0.0;
            double RadialM = 0.0;
        };
        const FView farViews[] = { { mid, 1.05 * dMax }, { endArcs.X - 1.05 * dMax, 0.0 },
            { endArcs.Y + 1.05 * dMax, 0.0 } };
        int32 drawn = 0;
        for (const FView& view : farViews)
        {
            const FSOLNpcTunnelSlotInput input = NpcFarFieldInput(view.FootArcM, view.RadialM, rt, endArcs, f);
            for (int32 slot = 0; slot < RingSlotCount(); ++slot)
            {
                FSOLNpcTunnelRing ring;
                drawn += RingSlot(slot, input, ring) ? 1 : 0;
            }
            for (int32 slot = 0; slot < DetailSlotCount(); ++slot)
            {
                FSOLNpcTunnelRing element;
                for (int32 dot = 0; dot < SOL::NPC_TUNNEL_DOTS_PER_GAP; ++dot)
                {
                    drawn += DotSlot(slot, dot, input, element) ? 1 : 0;
                }
                for (int32 chevron = 0; chevron < SOL::NPC_TUNNEL_CHEVRONS_PER_GAP; ++chevron)
                {
                    drawn += ChevronSlot(slot, chevron, input, element) ? 1 : 0;
                }
            }
            // The end rings from beyond d_max: below 0.5 px, so gone; never drawn below 1 px
            for (int32 end = 0; end < 2; ++end)
            {
                const double endX = end == 0 ? input.EndArcAM : input.EndArcBM;
                const double endAlpha = EndRingAlpha(endX, view.RadialM, rt, f);
                const double radiusPx = RingRadiusPx(FMath::Sqrt(endX * endX + view.RadialM * view.RadialM), rt, f);
                TestTrue(FString::Printf(TEXT("%s: far end ring %d below the gone px"), lane.Name, end),
                    radiusPx < SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX);
                TestEqual(FString::Printf(TEXT("%s: far end ring %d gone"), lane.Name, end), endAlpha, 0.0);
                TestTrue(FString::Printf(TEXT("%s: far end ring %d drawn at least 1 px"), lane.Name, end),
                    EndRingDrawnRadiusPx(radiusPx) >= SOL::NPC_TUNNEL_END_RING_MIN_DRAWN_PX);
            }
        }
        TestEqual(FString::Printf(TEXT("%s: nothing repeating from beyond d_max"), lane.Name), drawn, 0);

        // Between 1 and 0.5 px: only faint rings
        const double between = 1.2 * f * rt / SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_PX;
        TArray<FNpcFarFieldSeen> seen;
        NpcFarFieldSlotRings(mid, NpcFarFieldInput(mid, between, rt, endArcs, f), seen);
        double brightest = 0.0;
        for (const FNpcFarFieldSeen& ring : seen)
        {
            brightest = FMath::Max(brightest, ring.Alpha);
        }
        TestTrue(FString::Printf(TEXT("%s: below 1 px at most the knee"), lane.Name),
            brightest <= SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_ALPHA + NPCFF_TOL);

        // Mid distance: a run of rings near the foot point
        NpcFarFieldSlotRings(mid, NpcFarFieldInput(mid, 300.0 * rt, rt, endArcs, f), seen);
        TestTrue(FString::Printf(TEXT("%s: rings show from 300 Rt beside"), lane.Name), seen.Num() > 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldEndRingsTest, "SOLTest.Npc.TunnelFarField.EndRings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D14: the end rings follow the pixel curve (no 0.5 floor, no exemption) times the near fade; below 1 px their alpha
// is at most the knee, does not rise as they shrink and reaches 0 at the 0.5 px gone point like every ring (user
// 2026-11-18: no held alpha); the drawn radius is never below 1 reference px; EndRingSlot carries EndRingAlpha with the
// slot input's focal length, false for a bad one
bool FSOLNpcTunnelFarFieldEndRingsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double f = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX;
    const double rt = 2.0e5;

    // On the curve from 1 px up (on the axis, beyond the near fade)
    for (const double px : { 100.0, 20.0, 6.0, 3.0, 2.0, 1.5, 1.0 })
    {
        const double x = f * rt / px;
        TestEqual(FString::Printf(TEXT("end ring at %.1f px on the curve"), px), EndRingAlpha(x, 0.0, rt, f),
            RingRadiusFade(px), 1.0e-9);
    }
    // The #49 floor of 0.5 is gone: a 2 px end ring is dimmer than that
    TestTrue(TEXT("no 0.5 floor"), EndRingAlpha(f * rt / 2.0, 0.0, rt, f) < 0.5);

    // Below 1 px: at most the knee, not rising as the ring shrinks
    int32 bad = 0;
    double previous = SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_ALPHA;
    for (int32 i = 0; i <= 2000; ++i)
    {
        const double px = std::pow(10.0, -2.0 * i / 2000.0);
        const double alpha = EndRingAlpha(f * rt / px, 0.0, rt, f);
        bad += alpha <= SOL::NPC_TUNNEL_RADIUS_FADE_KNEE_ALPHA + NPCFF_TOL && alpha <= previous + 1.0e-12
            && alpha >= 0.0 ? 0 : 1;
        previous = alpha;
    }
    TestEqual(TEXT("below 1 px: faint and fading"), bad, 0);
    TestEqual(TEXT("end ring at 0.7 px on the curve's tail"), EndRingAlpha(f * rt / 0.7, 0.0, rt, f),
        RingRadiusFade(0.7), 1.0e-9);
    TestEqual(TEXT("end ring at the 0.5 px gone point: 0"), EndRingAlpha(f * rt / SOL::NPC_TUNNEL_RADIUS_FADE_GONE_PX,
        0.0, rt, f), 0.0, 1.0e-9);
    TestEqual(TEXT("end ring at 0.3 px: 0"), EndRingAlpha(f * rt / 0.3, 0.0, rt, f), 0.0);

    // The near fade still acts inside the tube, not beside it
    TestEqual(TEXT("an end ring at the camera inside is hidden"), EndRingAlpha(0.2 * rt, 0.0, rt, f), 0.0, NPCFF_TOL);
    TestEqual(TEXT("an end ring seen from beside is full"), EndRingAlpha(0.2 * rt, 5.0 * rt, rt, f), 1.0, NPCFF_TOL);

    // Bad input
    TestEqual(TEXT("NaN offset"), EndRingAlpha(NPCFF_NAN, 0.0, rt, f), 0.0);
    TestEqual(TEXT("bad Rt"), EndRingAlpha(10.0, 0.0, 0.0, f), 0.0);
    TestEqual(TEXT("bad focal"), EndRingAlpha(10.0 * rt, 0.0, rt, 0.0), 0.0);

    // The drawn radius floor
    TestEqual(TEXT("drawn: 0.3 px drawn at 1 px"), EndRingDrawnRadiusPx(0.3), 1.0, NPCFF_TOL);
    TestEqual(TEXT("drawn: 1 px"), EndRingDrawnRadiusPx(1.0), 1.0, NPCFF_TOL);
    TestEqual(TEXT("drawn: 5 px"), EndRingDrawnRadiusPx(5.0), 5.0, NPCFF_TOL);
    TestEqual(TEXT("drawn: 0 px at the floor"), EndRingDrawnRadiusPx(0.0), 1.0, NPCFF_TOL);
    TestEqual(TEXT("drawn: NaN at the floor"), EndRingDrawnRadiusPx(NPCFF_NAN), 1.0);

    // EndRingSlot carries EndRingAlpha with the input's focal length
    const FNpcFarFieldLane& lane = NPCFF_LANES[0];
    const double laneRt = lane.TunnelRadiusM;
    const FVector2D endArcs = NpcFarFieldEndArcs(lane);
    const double footArc = endArcs.X + 40.0 * laneRt;
    FSOLNpcTunnelSlotInput input = NpcFarFieldInput(footArc, 3.0 * laneRt, laneRt, endArcs, f);
    for (int32 end = 0; end < 2; ++end)
    {
        const double endX = end == 0 ? input.EndArcAM : input.EndArcBM;
        const double expected = EndRingAlpha(endX, input.RadialM, laneRt, f);
        FSOLNpcTunnelRing ring;
        const bool bDrawn = EndRingSlot(end, input, ring);
        TestTrue(FString::Printf(TEXT("end %d drawn when its alpha is above 0"), end), bDrawn == (expected > 0.0));
        if (bDrawn)
        {
            TestEqual(FString::Printf(TEXT("end %d offset"), end), ring.AxialOffsetM, endX, 1.0e-6 * laneRt);
            TestEqual(FString::Printf(TEXT("end %d alpha"), end), ring.Alpha, expected, NPCFF_TOL);
        }
    }

    // A bad focal length draws nothing
    input.FocalRefPx = 0.0;
    FSOLNpcTunnelRing ring;
    TestFalse(TEXT("bad focal: no end ring"), EndRingSlot(0, input, ring));
    TestFalse(TEXT("bad focal: no ring"), RingSlot(1, input, ring));
    TestFalse(TEXT("bad focal: no dot"), DotSlot(0, 0, input, ring));
    TestFalse(TEXT("bad focal: no chevron"), ChevronSlot(0, 0, input, ring));
    input.FocalRefPx = NPCFF_NAN;
    TestFalse(TEXT("NaN focal: no ring"), RingSlot(1, input, ring));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldLabelBandTest, "SOLTest.Npc.TunnelFarField.LabelBandSeparate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D7: drawing is not gated by distance (TunnelDrawFade is 1 whenever the tunnels show, 0 when hidden), while the label
// strength keeps the old 100 -> 50 lane radii band (TunnelFade unchanged); a lane 300 lane radii away draws rings but
// gets no label, a lane at 60 radii gets one
bool FSOLNpcTunnelFarFieldLabelBandTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    TestEqual(TEXT("draw fade 1 when shown"), TunnelDrawFade(true), 1.0);
    TestEqual(TEXT("draw fade 0 when hidden"), TunnelDrawFade(false), 0.0);

    // The label band, unchanged
    const double laneRadius = 2.0e5;
    TestEqual(TEXT("label strength 0 at 1,000 radii"), TunnelFade(1000.0 * laneRadius, laneRadius), 0.0, NPCFF_TOL);
    TestEqual(TEXT("label strength 0 at 100 radii"), TunnelFade(100.0 * laneRadius, laneRadius), 0.0, NPCFF_TOL);
    TestEqual(TEXT("label strength 0.5 at sqrt(5,000) radii"), TunnelFade(FMath::Sqrt(5000.0) * laneRadius,
        laneRadius), 0.5, 1.0e-9);
    TestEqual(TEXT("label strength 1 at 50 radii"), TunnelFade(50.0 * laneRadius, laneRadius), 1.0, NPCFF_TOL);

    // Rings draw from 300 lane radii although the label strength there is 0
    const FNpcFarFieldLane& lane = NPCFF_LANES[0];
    const double rt = lane.TunnelRadiusM;
    const FVector2D endArcs = NpcFarFieldEndArcs(lane);
    const double mid = 0.5 * NpcFarFieldLengthM(lane) + 0.3 * NpcFarFieldS0(rt);
    const double h = 300.0 * laneRadius;
    TArray<FNpcFarFieldSeen> seen;
    NpcFarFieldSlotRings(mid, NpcFarFieldInput(mid, h, rt, endArcs, SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX), seen);
    TestTrue(TEXT("rings drawn where the label strength is 0"), seen.Num() > 0 && TunnelFade(h, laneRadius) == 0.0);

    // Labels follow the label strength only
    TArray<double> fades = { TunnelFade(300.0 * laneRadius, laneRadius), TunnelFade(60.0 * laneRadius, laneRadius) };
    TArray<double> distances = { 300.0, 60.0 };
    TArray<int32> labelled;
    labelled.Init(INDEX_NONE, SOL::NPC_TUNNEL_LABEL_MAX_COUNT);
    const int32 count = SelectLabelLanes(fades, distances, labelled);
    TestEqual(TEXT("one label"), count, 1);
    TestEqual(TEXT("the 60-radii lane is labelled"), labelled[0], 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelFarFieldBudgetTest, "SOLTest.Npc.TunnelFarField.SlotMeshBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 52 case 10 (D15): the vertex budget is an int32 constant in SOLConstants.h (80,000 for 8 lanes, interim); the
// slot mesh count follows the formula (ring ribbons, two end rings at the nearest count, dot and chevron quads) and 8
// lanes fit the budget; 160 ring slots; segments by slot from the nearest count down, the same ahead and behind
bool FSOLNpcTunnelFarFieldBudgetTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    static_assert(std::is_same_v<std::remove_cv_t<decltype(SOL::NPC_TUNNEL_VERTEX_BUDGET)>, int32>,
        "the vertex budget is an int32 constant in SOLConstants.h");
    static_assert(std::is_same_v<std::remove_cv_t<decltype(SOL::NPC_TUNNEL_VERTEX_BUDGET_LANES)>, int32>,
        "the budget's lane count is an int32 constant in SOLConstants.h");
    const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    TestEqual(TEXT("ring slots: 2 x 80"), RingSlotCount(), 2 * perSide);
    TestEqual(TEXT("detail slots: 2 x per side"), DetailSlotCount(), 2 * SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE);

    // Level of detail by slot
    int32 badBands = 0;
    for (int32 k = 0; k < perSide; ++k)
    {
        const int32 ahead = RingSegmentsForSlot(k);
        const int32 behind = RingSegmentsForSlot(perSide + k);
        badBands += ahead > 0 && ahead == behind && ahead <= SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST ? 0 : 1;
        badBands += k == 0 || ahead <= RingSegmentsForSlot(k - 1) ? 0 : 1;
    }
    TestEqual(TEXT("segments: positive, the same both sides, non-increasing"), badBands, 0);
    TestEqual(TEXT("slot 0 at the nearest count"), RingSegmentsForSlot(0), SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST);
    TestEqual(TEXT("slot -1: 0"), RingSegmentsForSlot(-1), 0);
    TestEqual(TEXT("slot 2 x PER_SIDE: 0"), RingSegmentsForSlot(2 * perSide), 0);

    // The count's formula and the budget
    int32 ringVertices = 0;
    for (int32 slot = 0; slot < RingSlotCount(); ++slot)
    {
        ringVertices += 2 * (RingSegmentsForSlot(slot) + 1);
    }
    const int32 endRingVertices = 2 * 2 * (SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST + 1);
    const int32 dotVertices = 4 * DetailSlotCount() * SOL::NPC_TUNNEL_DOTS_PER_GAP * SOL::NPC_TUNNEL_DOT_ROWS;
    const int32 chevronVertices = 4 * DetailSlotCount() * SOL::NPC_TUNNEL_CHEVRONS_PER_GAP * 2;
    TestEqual(TEXT("vertex count: the formula"), TunnelSlotMeshVertexCount(),
        ringVertices + endRingVertices + dotVertices + chevronVertices);
    AddInfo(FString::Printf(TEXT("vertex count %d per lane, %d for %d lanes (budget %d)"),
        TunnelSlotMeshVertexCount(), SOL::NPC_TUNNEL_VERTEX_BUDGET_LANES * TunnelSlotMeshVertexCount(),
        SOL::NPC_TUNNEL_VERTEX_BUDGET_LANES, SOL::NPC_TUNNEL_VERTEX_BUDGET));
    TestTrue(TEXT("8 lanes within NPC_TUNNEL_VERTEX_BUDGET"),
        SOL::NPC_TUNNEL_VERTEX_BUDGET_LANES * TunnelSlotMeshVertexCount() <= SOL::NPC_TUNNEL_VERTEX_BUDGET);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
