/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcTunnelMath.h"
#include "Npc/SOLNpcLaneMath.h"
#include "SOLConstants.h"
#include "Universe/SOLRenderPlacement.h"

#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #49 lane guidance tunnel math (SOLNpcTunnel, SDD 49 section 6 cases 1-16), written against that
// contract before the production code existed (part 49a, test first). Doubles are compared to 1e-9 unless a case says
// otherwise. Frames: universe ecliptic metres, right-handed, +Z north. The ring layout follows the continuous
// "accordion" spacing of SDD 49 section 4.5: the test carries its own closed form of the warp and its inverse, so the
// slot coverage is checked against an independent brute-force reference. Review amendments (user, 2026-11-12): the near
// fade is weighted by the camera's distance from the axis (TunnelNearFadeAt), the up reference turns smoothly from +X
// to north between 1 and 2 degrees off the pole, the detail cap ends one gap early, slot phases are wrapped, the
// along-lane no-pop sweeps travel far enough for rings to cross an end ring, and dots and chevrons get their own sweep.
// Part 49b (test first, plan steps 7b and 8) adds the renderer-facing cases at the end of the file.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCTUNNEL_TOL = 1.0e-9;
    constexpr double NPCTUNNEL_ORTHO_TOL = 1.0e-12;                 // Basis orthonormality (case 4)
    constexpr double NPCTUNNEL_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double NPCTUNNEL_INF = std::numeric_limits<double>::infinity();
    constexpr double NPCTUNNEL_AU_M = 1.495978707e11;

    // The shortest and longest default lanes of case 11 (SOLNpcScale::DefaultLayout radii, mean body radii)
    constexpr double NPCTUNNEL_EARTH_MOON_PERIGEE_M = 3.633e8;
    constexpr double NPCTUNNEL_EARTH_RADIUS_M = 6.371e6;
    constexpr double NPCTUNNEL_MOON_RADIUS_M = 1.7374e6;
    constexpr double NPCTUNNEL_EARTH_MOON_LANE_RADIUS_M = 2.0e5;
    constexpr double NPCTUNNEL_URANUS_NEPTUNE_M = 30.0 * NPCTUNNEL_AU_M;
    constexpr double NPCTUNNEL_URANUS_RADIUS_M = 2.5559e7;
    constexpr double NPCTUNNEL_NEPTUNE_RADIUS_M = 2.4764e7;
    constexpr double NPCTUNNEL_URANUS_NEPTUNE_LANE_RADIUS_M = 1.0e7;

    // A ring or detail element of the brute-force reference: its warp coordinate, axial offset and alpha
    struct FNpcTunnelRefElement
    {
        double W = 0.0;
        double X = 0.0;
        double Alpha = 0.0;
    };

    // A ring seen by the no-pop sweep: its axial offset and alpha
    struct FNpcTunnelSeenRing
    {
        double X = 0.0;
        double Alpha = 0.0;
    };

    // Failure counters of a coverage or sweep check, with the first failure's description
    struct FNpcTunnelFailures
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
    // Returns true when two vectors agree component-wise within a tolerance
    bool NpcTunnelVecNear(const FVector3d& a, const FVector3d& b, const double tolerance)
    {
        return (a - b).GetAbsMax() <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns frac(x) in [0, 1) (a negative x wraps up)
    double NpcTunnelFrac(const double x)
    {
        return x - FMath::FloorToDouble(x);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the circular distance between two phases in [0, 1)
    double NpcTunnelWrapDistance(const double a, const double b)
    {
        const double d = FMath::Abs(a - b);
        return FMath::Min(d, 1.0 - d);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the constant ring spacing near the camera, S0 = NPC_TUNNEL_RING_BASE_SPACING_RADII x Rt
    double NpcTunnelS0(const double tunnelRadiusM)
    {
        return SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII * tunnelRadiusM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the spacing growth per distance, rho = NPC_TUNNEL_RING_SPACING_PER_DISTANCE
    double NpcTunnelRho()
    {
        return SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the half length of the constant-spacing zone on the camera's foot point, x0 = sqrt(max(0, D0^2 - h^2))
    // with D0 = S0 / rho
    double NpcTunnelX0(const double radialM, const double tunnelRadiusM)
    {
        const double d0 = NpcTunnelS0(tunnelRadiusM) / NpcTunnelRho();
        return FMath::Sqrt(FMath::Max(0.0, d0 * d0 - radialM * radialM));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's local ring spacing, max(S0, rho x sqrt(x^2 + h^2)) (the test's own closed form)
    double NpcTunnelRefSpacing(const double axialM, const double radialM, const double tunnelRadiusM)
    {
        return FMath::Max(NpcTunnelS0(tunnelRadiusM),
            NpcTunnelRho() * FMath::Sqrt(axialM * axialM + radialM * radialM));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's ring warp W(x, h), the integral of 1 / spacing from the foot point (the test's own closed
    // form, in the asinh form so it is well conditioned at h = 0 and x0 = 0)
    double NpcTunnelRefWarp(const double axialM, const double radialM, const double tunnelRadiusM)
    {
        const double s0 = NpcTunnelS0(tunnelRadiusM);
        const double x0 = NpcTunnelX0(radialM, tunnelRadiusM);
        const double u = FMath::Abs(axialM);
        if (u <= x0)
        {
            return axialM / s0;
        }
        const double logPart = radialM > 0.0 ? std::asinh(u / radialM) - std::asinh(x0 / radialM)
            : std::log(u / x0);
        const double magnitude = x0 / s0 + logPart / NpcTunnelRho();
        return axialM < 0.0 ? -magnitude : magnitude;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's inverse warp: x = w S0 inside the constant zone, else u = (v^2 - h^2) / (2 v) with
    // v = (x0 + sqrt(x0^2 + h^2)) e^(rho (|w| - x0 / S0)), written with expm1 so it does not cancel
    double NpcTunnelRefWarpInverse(const double w, const double radialM, const double tunnelRadiusM)
    {
        const double s0 = NpcTunnelS0(tunnelRadiusM);
        const double x0 = NpcTunnelX0(radialM, tunnelRadiusM);
        const double a = FMath::Abs(w);
        if (a <= x0 / s0)
        {
            return w * s0;
        }
        const double c = x0 + FMath::Sqrt(x0 * x0 + radialM * radialM);
        const double t = NpcTunnelRho() * (a - x0 / s0);
        const double u = (0.5 * c * std::expm1(2.0 * t) + x0) * std::exp(-t);
        return w < 0.0 ? -u : u;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns ring slot k's warp coordinate (SDD 49 section 4.5): slots 0 .. PER_SIDE - 1 ahead at k + 1 - phase,
    // slots PER_SIDE .. 2 PER_SIDE - 1 behind at -((k - PER_SIDE) + phase)
    double NpcTunnelSlotW(const int32 slot, const double phase)
    {
        const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
        return slot < perSide ? static_cast<double>(slot + 1) - phase
            : -(static_cast<double>(slot - perSide) + phase);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a slot input (radial distance, tunnel radius, phase, end-ring arcs relative to the foot arc)
    FSOLNpcTunnelSlotInput NpcTunnelInput(const double radialM, const double tunnelRadiusM, const double phase,
        const double endArcAM, const double endArcBM)
    {
        FSOLNpcTunnelSlotInput input;
        input.RadialM = radialM;
        input.TunnelRadiusM = tunnelRadiusM;
        input.Phase = phase;
        input.EndArcAM = endArcAM;
        input.EndArcBM = endArcBM;
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the axial distance from x to the nearer end ring, inside the tunnel (negative outside)
    double NpcTunnelDistanceToEnd(const double axialM, const FSOLNpcTunnelSlotInput& input)
    {
        return FMath::Min(axialM - input.EndArcAM, input.EndArcBM - axialM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the reference alpha of a ring (SDD 49 sections 4.5 and 4.7): cap fade x end fade x depth fade x near
    // fade (weighted by h, TunnelNearFadeAt), the depth measured from the camera to the ring's centre on the axis,
    // sqrt(x^2 + h^2)
    double NpcTunnelRefRingAlpha(const double w, const double axialM, const FSOLNpcTunnelSlotInput& input)
    {
        using namespace SOLNpcTunnel;
        const double rt = input.TunnelRadiusM;
        const double distanceM = FMath::Sqrt(axialM * axialM + input.RadialM * input.RadialM);
        return RingCapFade(w) * TunnelEndFade(NpcTunnelDistanceToEnd(axialM, input), RingSpacingM(axialM,
            input.RadialM, rt)) * TunnelDepthFade(distanceM, rt) * TunnelNearFadeAt(axialM, input.RadialM, rt);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the reference alpha of a guide dot or chevron (its cap fade is 1 wherever the detail fade is not 0):
    // end fade x depth fade x near fade x detail fade, distances from the camera to the axis point at x
    double NpcTunnelRefDetailAlpha(const double axialM, const FSOLNpcTunnelSlotInput& input)
    {
        using namespace SOLNpcTunnel;
        const double rt = input.TunnelRadiusM;
        const double distanceM = FMath::Sqrt(axialM * axialM + input.RadialM * input.RadialM);
        return TunnelEndFade(NpcTunnelDistanceToEnd(axialM, input), RingSpacingM(axialM, input.RadialM, rt))
            * TunnelDepthFade(distanceM, rt) * TunnelNearFadeAt(axialM, input.RadialM, rt)
            * TunnelDetailFade(distanceM, rt);
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the brute-force rings: every whole n with W^-1(n - phase) strictly between the end rings and a non-zero
    // reference alpha (the cap fade removes every ring with |w| >= PER_SIDE)
    void NpcTunnelBruteRings(const FSOLNpcTunnelSlotInput& input, TArray<FNpcTunnelRefElement>& outRings)
    {
        outRings.Reset();
        const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
        for (int32 n = -perSide - 2; n <= perSide + 2; ++n)
        {
            const double w = static_cast<double>(n) - input.Phase;
            const double x = NpcTunnelRefWarpInverse(w, input.RadialM, input.TunnelRadiusM);
            if (!(x > input.EndArcAM && x < input.EndArcBM))
            {
                continue;
            }
            const double alpha = NpcTunnelRefRingAlpha(w, x, input);
            if (alpha > 0.0)
            {
                outRings.Add({ w, x, alpha });
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the brute-force dots (at n - phase + 0.25 and + 0.75, bChevrons false) or chevrons (n - phase + 0.5)
    // strictly between the end rings with a non-zero reference alpha
    void NpcTunnelBruteDetails(const FSOLNpcTunnelSlotInput& input, const bool bChevrons,
        TArray<FNpcTunnelRefElement>& outElements)
    {
        outElements.Reset();
        const int32 perSide = SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE;
        const double dotOffsets[] = { 0.25, 0.75 };
        const double chevronOffsets[] = { 0.5 };
        const TConstArrayView<double> offsets = bChevrons ? TConstArrayView<double>(chevronOffsets)
            : TConstArrayView<double>(dotOffsets);
        for (int32 n = -perSide - 2; n <= perSide + 2; ++n)
        {
            for (const double offset : offsets)
            {
                const double w = static_cast<double>(n) - input.Phase + offset;
                const double x = NpcTunnelRefWarpInverse(w, input.RadialM, input.TunnelRadiusM);
                if (!(x > input.EndArcAM && x < input.EndArcBM))
                {
                    continue;
                }
                // Chevrons also take their own far fade (D22), by the camera-to-axis-point distance
                const double alpha = NpcTunnelRefDetailAlpha(x, input) * (bChevrons
                    ? SOLNpcTunnel::TunnelChevronFarFade(FMath::Sqrt(x * x + input.RadialM * input.RadialM),
                        input.TunnelRadiusM)
                    : 1.0);
                if (alpha > 0.0)
                {
                    outElements.Add({ w, x, alpha });
                }
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Matches the elements a set of slots returned against the brute-force reference: every reference element with a
    // visible alpha is returned exactly once with the same axial offset (1e-6 relative) and alpha (1e-6), and no slot
    // returns an element outside the end rings or one the reference does not have
    void NpcTunnelMatchCoverage(const TArray<FNpcTunnelRefElement>& reference,
        const TArray<FSOLNpcTunnelRing>& returned, const FSOLNpcTunnelSlotInput& input, const FString& label,
        FNpcTunnelFailures& failures)
    {
        constexpr double visibleAlpha = 1.0e-6;
        TArray<int32> hits;
        hits.Init(0, reference.Num());
        const double offsetScale = input.TunnelRadiusM;
        for (const FSOLNpcTunnelRing& ring : returned)
        {
            if (ring.AxialOffsetM < input.EndArcAM || ring.AxialOffsetM > input.EndArcBM)
            {
                failures.Add(label + FString::Printf(TEXT(": element at %.6e m outside the end rings"),
                    ring.AxialOffsetM));
                continue;
            }

            // Find the reference element with the same warp coordinate
            int32 match = INDEX_NONE;
            for (int32 index = 0; index < reference.Num(); ++index)
            {
                if (FMath::Abs(reference[index].W - ring.W) < 1.0e-6)
                {
                    match = index;
                    break;
                }
            }
            if (match == INDEX_NONE)
            {
                if (ring.Alpha > visibleAlpha)
                {
                    failures.Add(label + FString::Printf(TEXT(": stray element w %.6f alpha %.6f"), ring.W,
                        ring.Alpha));
                }
                continue;
            }
            ++hits[match];
            const FNpcTunnelRefElement& expected = reference[match];
            if (FMath::Abs(ring.AxialOffsetM - expected.X) > 1.0e-6 * FMath::Max(FMath::Abs(expected.X), offsetScale))
            {
                failures.Add(label + FString::Printf(TEXT(": w %.6f offset %.9e, expected %.9e"), ring.W,
                    ring.AxialOffsetM, expected.X));
            }
            if (FMath::Abs(ring.Alpha - expected.Alpha) > 1.0e-6)
            {
                failures.Add(label + FString::Printf(TEXT(": w %.6f alpha %.9f, expected %.9f"), ring.W, ring.Alpha,
                    expected.Alpha));
            }
        }

        // Every visible reference element exactly once
        for (int32 index = 0; index < reference.Num(); ++index)
        {
            if (hits[index] > 1 || (hits[index] == 0 && reference[index].Alpha > visibleAlpha))
            {
                failures.Add(label + FString::Printf(TEXT(": w %.6f (x %.6e, alpha %.6f) returned %d times"),
                    reference[index].W, reference[index].X, reference[index].Alpha, hits[index]));
            }
        }
    }

    // The tunnel element kinds a no-pop sweep follows
    enum class ENpcTunnelElement : uint8
    {
        Ring,
        Dot,
        Chevron
    };

    //////////////////////////////////////////////////////////////////////////
    // Fills the elements of one kind every slot returns for an input
    void NpcTunnelVisibleElements(const ENpcTunnelElement kind, const FSOLNpcTunnelSlotInput& input,
        TArray<FNpcTunnelSeenRing>& outElements)
    {
        outElements.Reset();
        FSOLNpcTunnelRing element;
        if (kind == ENpcTunnelElement::Ring)
        {
            for (int32 slot = 0; slot < SOLNpcTunnel::RingSlotCount(); ++slot)
            {
                if (SOLNpcTunnel::RingSlot(slot, input, element))
                {
                    outElements.Add({ element.AxialOffsetM, element.Alpha });
                }
            }
            return;
        }
        for (int32 slot = 0; slot < SOLNpcTunnel::DetailSlotCount(); ++slot)
        {
            if (kind == ENpcTunnelElement::Chevron)
            {
                if (SOLNpcTunnel::ChevronSlot(slot, input, element))
                {
                    outElements.Add({ element.AxialOffsetM, element.Alpha });
                }
                continue;
            }
            for (int32 dot = 0; dot < SOL::NPC_TUNNEL_DOTS_PER_GAP; ++dot)
            {
                if (SOLNpcTunnel::DotSlot(slot, dot, input, element))
                {
                    outElements.Add({ element.AxialOffsetM, element.Alpha });
                }
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the rings every ring slot returns for an input
    void NpcTunnelVisibleRings(const FSOLNpcTunnelSlotInput& input, TArray<FNpcTunnelSeenRing>& outRings)
    {
        NpcTunnelVisibleElements(ENpcTunnelElement::Ring, input, outRings);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks one step of a no-pop sweep: every ring of 'from' has a ring of 'to' within a quarter of its local spacing
    // (a ring moves far less per step, and its neighbours are a whole spacing away) with an alpha change under 0.05, or
    // else its alpha is under 1e-3 (it entered or left at the cap, an end ring or the near fade). With 'fromInput' an
    // unmatched element within one local spacing of an end ring counts as an end-ring transition
    void NpcTunnelCheckStep(const TArray<FNpcTunnelSeenRing>& from, const TArray<FNpcTunnelSeenRing>& to,
        const double radialM, const double tunnelRadiusM, const FString& label, FNpcTunnelFailures& failures,
        const FSOLNpcTunnelSlotInput* fromInput = nullptr, int32* endTransitions = nullptr)
    {
        for (const FNpcTunnelSeenRing& ring : from)
        {
            const double bound = 0.25 * NpcTunnelRefSpacing(ring.X, radialM, tunnelRadiusM);
            const FNpcTunnelSeenRing* nearest = nullptr;
            double nearestGap = NPCTUNNEL_INF;
            for (const FNpcTunnelSeenRing& other : to)
            {
                const double gap = FMath::Abs(other.X - ring.X);
                if (gap < nearestGap)
                {
                    nearestGap = gap;
                    nearest = &other;
                }
            }
            if (nearest != nullptr && nearestGap <= bound)
            {
                if (FMath::Abs(nearest->Alpha - ring.Alpha) > 0.05)
                {
                    failures.Add(label + FString::Printf(TEXT(": ring at %.6e m alpha jumps %.4f -> %.4f"), ring.X,
                        ring.Alpha, nearest->Alpha));
                }
            }
            else if (ring.Alpha >= 1.0e-3)
            {
                failures.Add(label + FString::Printf(TEXT(": ring at %.6e m (alpha %.5f) has no match"), ring.X,
                    ring.Alpha));
            }
            else if (fromInput != nullptr && endTransitions != nullptr
                && FMath::Min(FMath::Abs(ring.X - fromInput->EndArcAM), FMath::Abs(fromInput->EndArcBM - ring.X))
                    < NpcTunnelRefSpacing(ring.X, radialM, tunnelRadiusM))
            {
                ++*endTransitions;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the horizontal screen coordinate (x right, divided by depth) of an ecliptic point seen by a camera at
    // cameraM looking along the ecliptic direction lookDirection, in the Unreal frame with the camera's up = Unreal +Z
    double NpcTunnelScreenX(const FVector3d& cameraM, const FVector3d& lookDirection, const FVector3d& pointM)
    {
        const FVector3d forward = SOLRender::EclipticToUnreal(lookDirection).GetSafeNormal();
        const FVector3d right = FVector3d::CrossProduct(FVector3d::UnitZ(), forward).GetSafeNormal();
        const FVector3d relative = SOLRender::EclipticToUnreal(pointM - cameraM);
        return FVector3d::DotProduct(relative, right) / FVector3d::DotProduct(relative, forward);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a basis is unit, mutually orthogonal and right-handed (Axis x Up == Right) within tolerance
    bool NpcTunnelIsOrthonormal(const FSOLNpcTunnelBasis& basis, const double tolerance)
    {
        return FMath::Abs(basis.Axis.Size() - 1.0) <= tolerance && FMath::Abs(basis.Up.Size() - 1.0) <= tolerance
            && FMath::Abs(basis.Right.Size() - 1.0) <= tolerance
            && FMath::Abs(FVector3d::DotProduct(basis.Axis, basis.Up)) <= tolerance
            && FMath::Abs(FVector3d::DotProduct(basis.Axis, basis.Right)) <= tolerance
            && FMath::Abs(FVector3d::DotProduct(basis.Up, basis.Right)) <= tolerance
            && NpcTunnelVecNear(FVector3d::CrossProduct(basis.Axis, basis.Up), basis.Right, tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the direction of a unit vector at an elevation above the ecliptic and an azimuth (degrees)
    FVector3d NpcTunnelDirection(const double elevationDeg, const double azimuthDeg)
    {
        const double elevation = FMath::DegreesToRadians(elevationDeg);
        const double azimuth = FMath::DegreesToRadians(azimuthDeg);
        return FVector3d(FMath::Cos(elevation) * FMath::Cos(azimuth), FMath::Cos(elevation) * FMath::Sin(azimuth),
            FMath::Sin(elevation));
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathConstantsTest, "SOLTest.NpcTunnelMath.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1: the proposed tuning values (asserted so a tuning change is a visible test edit), the hand-over meeting the
// lane line's fade, and the colours: the HUD kit cyan for the rings, muted green / red dots and lighter chevrons
bool FSOLNpcTunnelMathConstantsTest::RunTest(const FString& /*parameters*/)
{
    // Trigger band: start > full > 1, and the start meets the #38 line's close-up fade
    TestEqual(TEXT("fade start 100 radii"), static_cast<double>(SOL::NPC_TUNNEL_FADE_START_RADII), 100.0,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("fade full 50 radii"), static_cast<double>(SOL::NPC_TUNNEL_FADE_FULL_RADII), 50.0, NPCTUNNEL_TOL);
    TestTrue(TEXT("start > full > 1"), SOL::NPC_TUNNEL_FADE_START_RADII > SOL::NPC_TUNNEL_FADE_FULL_RADII
        && SOL::NPC_TUNNEL_FADE_FULL_RADII > 1.0);
    TestEqual(TEXT("start == the lane line's near fade"), static_cast<double>(SOL::NPC_TUNNEL_FADE_START_RADII),
        static_cast<double>(SOL::NPC_LANE_FADE_NEAR_RADII), NPCTUNNEL_TOL);

    // Radius, spacing and slots
    TestEqual(TEXT("radius min scale 1"), static_cast<double>(SOL::NPC_TUNNEL_RADIUS_MIN_SCALE), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("radius max scale 3"), static_cast<double>(SOL::NPC_TUNNEL_RADIUS_MAX_SCALE), 3.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("ring base spacing 2 radii"), static_cast<double>(SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII), 2.0,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("ring spacing per distance 0.25"), static_cast<double>(SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE),
        0.25, NPCTUNNEL_TOL);
    TestEqual(TEXT("64 ring slots per side"), static_cast<int32>(SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE), 64);
    TestEqual(TEXT("24 detail slots per side"), static_cast<int32>(SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE), 24);

    // Fades
    TestEqual(TEXT("end fade 0.5 spacings"), static_cast<double>(SOL::NPC_TUNNEL_END_FADE_SPACINGS), 0.5,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("depth fade start 8 radii"), static_cast<double>(SOL::NPC_TUNNEL_DEPTH_FADE_START_RADII), 8.0,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("depth fade end 65,536 radii"), static_cast<double>(SOL::NPC_TUNNEL_DEPTH_FADE_END_RADII), 65536.0,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("depth fade floor 0.2"), static_cast<double>(SOL::NPC_TUNNEL_DEPTH_FADE_FLOOR), 0.2,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("near fade start 0.5 radii"), static_cast<double>(SOL::NPC_TUNNEL_NEAR_FADE_START_RADII), 0.5,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("near fade full 1.5 radii"), static_cast<double>(SOL::NPC_TUNNEL_NEAR_FADE_FULL_RADII), 1.5,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("near fade acts fully within 1.5 radii of the axis"),
        static_cast<double>(SOL::NPC_TUNNEL_NEAR_FADE_RADIAL_FULL_RADII), 1.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("near fade gone 3 radii off the axis"),
        static_cast<double>(SOL::NPC_TUNNEL_NEAR_FADE_RADIAL_NONE_RADII), 3.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail fade start 256 radii"), static_cast<double>(SOL::NPC_TUNNEL_DETAIL_FADE_START_RADII),
        256.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail fade end 512 radii"), static_cast<double>(SOL::NPC_TUNNEL_DETAIL_FADE_END_RADII), 512.0,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("end margin 3 radii (SDD 50 D14)"), static_cast<double>(SOL::NPC_TUNNEL_END_MARGIN_RADII), 3.0,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("end ring min alpha 0.5"), static_cast<double>(SOL::NPC_TUNNEL_END_RING_MIN_ALPHA), 0.5,
        NPCTUNNEL_TOL);

    // Dots, orientation and labels
    TestEqual(TEXT("8 dot rows"), static_cast<int32>(SOL::NPC_TUNNEL_DOT_ROWS), 8);
    TestEqual(TEXT("2 dots per gap"), static_cast<int32>(SOL::NPC_TUNNEL_DOTS_PER_GAP), 2);
    TestEqual(TEXT("dot radius scale 1.1"), static_cast<double>(SOL::NPC_TUNNEL_DOT_RADIUS_SCALE), 1.1,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("up fallback 1 degree"), static_cast<double>(SOL::NPC_TUNNEL_UP_FALLBACK_DEG), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("up blend to 2 degrees"), static_cast<double>(SOL::NPC_TUNNEL_UP_BLEND_DEG), 2.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("at most 4 labels"), static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_COUNT), 4);
    TestEqual(TEXT("side label alpha 0.55"), static_cast<double>(SOL::NPC_TUNNEL_LABEL_SIDE_ALPHA), 0.55,
        NPCTUNNEL_TOL);
    TestTrue(TEXT("side label alpha in (0, 1)"), SOL::NPC_TUNNEL_LABEL_SIDE_ALPHA > 0.0
        && SOL::NPC_TUNNEL_LABEL_SIDE_ALPHA < 1.0);
    TestEqual(TEXT("label min alpha 0.05"), static_cast<double>(SOL::NPC_TUNNEL_LABEL_MIN_ALPHA), 0.05,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("label gap 6 px"), static_cast<double>(SOL::NPC_TUNNEL_LABEL_GAP_PX), 6.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("label 2.5 radii ahead"), static_cast<double>(SOL::NPC_TUNNEL_LABEL_AHEAD_RADII), 2.5,
        NPCTUNNEL_TOL);

    // Colours, stored as sRGB FColor
    TestTrue(TEXT("ring colour is the HUD kit cyan"), SOL::NPC_TUNNEL_RING_COLOR == SOL::SolidMenu::CYAN_COLOR);
    TestTrue(TEXT("ring colour #28D2F0"), SOL::NPC_TUNNEL_RING_COLOR == FColor(0x28, 0xD2, 0xF0));
    TestTrue(TEXT("outbound dots #5C8F6C"), SOL::NPC_TUNNEL_DOT_OUTBOUND_COLOR == FColor(0x5C, 0x8F, 0x6C));
    TestTrue(TEXT("inbound dots #9A5656"), SOL::NPC_TUNNEL_DOT_INBOUND_COLOR == FColor(0x9A, 0x56, 0x56));
    TestTrue(TEXT("outbound chevrons #84AB90"), SOL::NPC_TUNNEL_CHEVRON_OUTBOUND_COLOR == FColor(0x84, 0xAB, 0x90));
    TestTrue(TEXT("inbound chevrons #B38080"), SOL::NPC_TUNNEL_CHEVRON_INBOUND_COLOR == FColor(0xB3, 0x80, 0x80));
    TestTrue(TEXT("dot colours differ"), SOL::NPC_TUNNEL_DOT_OUTBOUND_COLOR != SOL::NPC_TUNNEL_DOT_INBOUND_COLOR);
    TestTrue(TEXT("chevron colours differ"),
        SOL::NPC_TUNNEL_CHEVRON_OUTBOUND_COLOR != SOL::NPC_TUNNEL_CHEVRON_INBOUND_COLOR);
    TestTrue(TEXT("outbound dots: green above red"),
        SOL::NPC_TUNNEL_DOT_OUTBOUND_COLOR.G > SOL::NPC_TUNNEL_DOT_OUTBOUND_COLOR.R);
    TestTrue(TEXT("outbound chevrons: green above red"),
        SOL::NPC_TUNNEL_CHEVRON_OUTBOUND_COLOR.G > SOL::NPC_TUNNEL_CHEVRON_OUTBOUND_COLOR.R);
    TestTrue(TEXT("inbound dots: red above green"),
        SOL::NPC_TUNNEL_DOT_INBOUND_COLOR.R > SOL::NPC_TUNNEL_DOT_INBOUND_COLOR.G);
    TestTrue(TEXT("inbound chevrons: red above green"),
        SOL::NPC_TUNNEL_CHEVRON_INBOUND_COLOR.R > SOL::NPC_TUNNEL_CHEVRON_INBOUND_COLOR.G);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathFadeTest, "SOLTest.NpcTunnelMath.TunnelFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 2: the tunnel fade is 0 at and beyond START x R, 1 at and inside FULL x R, 0.5 at the geometric mean and linear
// in log d between; non-increasing, scale free; a NaN distance or a bad radius shows no tunnel (0)
bool FSOLNpcTunnelMathFadeTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::TunnelFade;
    const double startRadii = SOL::NPC_TUNNEL_FADE_START_RADII;
    const double fullRadii = SOL::NPC_TUNNEL_FADE_FULL_RADII;
    for (const double radiusM : { 2.0e5, 1.0e7 })
    {
        const FString label = FString::Printf(TEXT("R %.0f m"), radiusM);
        const double startM = startRadii * radiusM;
        const double fullM = fullRadii * radiusM;

        // The ends of the band and beyond
        TestEqual(label + TEXT(": 0 at START R"), TunnelFade(startM, radiusM), 0.0, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 0 beyond START R"), TunnelFade(startM * 2.0, radiusM), 0.0, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 0 at +inf"), TunnelFade(NPCTUNNEL_INF, radiusM), 0.0, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 1 at FULL R"), TunnelFade(fullM, radiusM), 1.0, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 1 inside FULL R"), TunnelFade(fullM * 0.5, radiusM), 1.0, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 1 at d = 0"), TunnelFade(0.0, radiusM), 1.0, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 1 at a negative d"), TunnelFade(-radiusM, radiusM), 1.0, NPCTUNNEL_TOL);

        // 0.5 at the geometric mean
        TestEqual(label + TEXT(": 0.5 at sqrt(START FULL) R"),
            TunnelFade(FMath::Sqrt(startRadii * fullRadii) * radiusM, radiusM), 0.5, NPCTUNNEL_TOL);

        // Linear in log d: three interior points against the closed form ln(START R / d) / ln(START / FULL)
        for (const double t : { 0.25, 0.5, 0.75 })
        {
            const double d = fullM * FMath::Pow(startRadii / fullRadii, t);
            const double closedForm = FMath::Loge(startM / d) / FMath::Loge(startRadii / fullRadii);
            TestEqual(label + FString::Printf(TEXT(": 1 - t at t %.2f"), t), TunnelFade(d, radiusM), 1.0 - t,
                NPCTUNNEL_TOL);
            TestEqual(label + FString::Printf(TEXT(": closed form at t %.2f"), t), TunnelFade(d, radiusM), closedForm,
                NPCTUNNEL_TOL);
        }
        const double oddM = 75.0 * radiusM;
        TestEqual(label + TEXT(": closed form at 75 R (about 0.415)"), TunnelFade(oddM, radiusM),
            FMath::Loge(startM / oddM) / FMath::Loge(startRadii / fullRadii), NPCTUNNEL_TOL);

        // Non-increasing in d, in [0, 1]
        double previous = 2.0;
        for (int32 step = 0; step <= 200; ++step)
        {
            const double d = radiusM * 10.0 * FMath::Pow(10.0, static_cast<double>(step) / 100.0);
            const double fade = TunnelFade(d, radiusM);
            TestTrue(label + FString::Printf(TEXT(": in [0, 1] at step %d"), step), fade >= 0.0 && fade <= 1.0);
            TestTrue(label + FString::Printf(TEXT(": non-increasing at step %d"), step), fade <= previous + 1.0e-12);
            previous = fade;
        }
    }

    // Scale free: fade(k d, k R) == fade(d, R)
    constexpr double scaleK = 1.0e3;
    for (const double inRadii : { 10.0, 50.0, 60.0, 70.7, 75.0, 90.0, 100.0, 300.0 })
    {
        const double radiusM = 2.0e5;
        TestEqual(FString::Printf(TEXT("scale free at %.1f R"), inRadii),
            TunnelFade(scaleK * inRadii * radiusM, scaleK * radiusM), TunnelFade(inRadii * radiusM, radiusM),
            NPCTUNNEL_TOL);
    }

    // Bad input shows no tunnel
    TestEqual(TEXT("NaN distance -> 0"), TunnelFade(NPCTUNNEL_NAN, 2.0e5), 0.0, NPCTUNNEL_TOL);
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -2.0e5, NPCTUNNEL_INF, -NPCTUNNEL_INF })
    {
        const FString label = FString::Printf(TEXT("radius %f"), badRadius);
        TestEqual(label + TEXT(": 0 at d = 0"), TunnelFade(0.0, badRadius), 0.0, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 0 at 1.5e7 m"), TunnelFade(1.5e7, badRadius), 0.0, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 0 at 1 AU"), TunnelFade(NPCTUNNEL_AU_M, badRadius), 0.0, NPCTUNNEL_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathRadiusTest, "SOLTest.NpcTunnelMath.TunnelRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 3 (D8, Q3): the tunnel radius is R x lerp(MIN, MAX, LaneTrafficWeight(n)), the lane radius for an empty or
// quiet lane (never 0), 3 R for a busy one, non-decreasing in the count; a bad radius gives 0
bool FSOLNpcTunnelMathRadiusTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::TunnelRadiusM;
    const double minScale = SOL::NPC_TUNNEL_RADIUS_MIN_SCALE;
    const double maxScale = SOL::NPC_TUNNEL_RADIUS_MAX_SCALE;
    for (const double radiusM : { 2.0e5, 3.0e6, 1.0e7 })
    {
        const FString label = FString::Printf(TEXT("R %.0f m"), radiusM);
        TestEqual(label + TEXT(": 0 ships -> R x MIN"), TunnelRadiusM(radiusM, 0), radiusM * minScale, NPCTUNNEL_TOL);
        TestTrue(label + TEXT(": 0 ships never 0"), TunnelRadiusM(radiusM, 0) > 0.0);
        TestEqual(label + TEXT(": -5 ships -> R x MIN"), TunnelRadiusM(radiusM, -5), radiusM * minScale,
            NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": INT32_MIN -> R x MIN"), TunnelRadiusM(radiusM, TNumericLimits<int32>::Min()),
            radiusM * minScale, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 100 ships -> R"), TunnelRadiusM(radiusM, 100), radiusM, NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": 1,000 ships -> R (1 + 2/3)"), TunnelRadiusM(radiusM, 1000),
            radiusM * (1.0 + 2.0 / 3.0), NPCTUNNEL_TOL * radiusM);
        TestEqual(label + TEXT(": 100,000 ships -> 3 R"), TunnelRadiusM(radiusM, 100000), 3.0 * radiusM,
            NPCTUNNEL_TOL * radiusM);
        TestEqual(label + TEXT(": 1,000,000 ships -> 3 R"), TunnelRadiusM(radiusM, 1000000), 3.0 * radiusM,
            NPCTUNNEL_TOL * radiusM);

        // The formula against the lane traffic weight, non-decreasing
        double previous = 0.0;
        for (int32 count = 0; count <= 2000000; count = count < 10 ? count + 1 : count + count / 7)
        {
            const double radius = TunnelRadiusM(radiusM, count);
            const double expected = radiusM * FMath::Lerp(minScale, maxScale, SOLNpcLane::LaneTrafficWeight(count));
            TestEqual(label + FString::Printf(TEXT(": %d ships, the formula"), count), radius, expected,
                NPCTUNNEL_TOL * radiusM);
            TestTrue(label + FString::Printf(TEXT(": %d ships, non-decreasing"), count), radius >= previous - 1.0e-6);
            previous = radius;
        }
    }

    // A bad lane radius gives no tunnel
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -3.0e6, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("radius %f -> 0"), badRadius), TunnelRadiusM(badRadius, 5000), 0.0,
            NPCTUNNEL_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathBasisTest, "SOLTest.NpcTunnelMath.TunnelBasis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 4 (D5): the tunnel basis is the outbound axis, up = ecliptic north projected off the axis, right = axis x up
// (right-handed); within 1 degree of +Z the reference becomes +X; A == B or a non-finite input is invalid; the basis
// does not depend on the lane's length or position
bool FSOLNpcTunnelMathBasisTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::TunnelBasis;
    const FVector3d origin(5.0e10, -3.0e9, 1.0e7);

    // A -> B along +X
    const FSOLNpcTunnelBasis alongX = TunnelBasis(origin, origin + FVector3d(4.0e11, 0.0, 0.0));
    TestTrue(TEXT("+X: valid"), alongX.bValid);
    TestTrue(TEXT("+X: axis (1, 0, 0)"), NpcTunnelVecNear(alongX.Axis, FVector3d(1.0, 0.0, 0.0), NPCTUNNEL_ORTHO_TOL));
    TestTrue(TEXT("+X: up (0, 0, 1)"), NpcTunnelVecNear(alongX.Up, FVector3d(0.0, 0.0, 1.0), NPCTUNNEL_ORTHO_TOL));
    TestTrue(TEXT("+X: right (0, -1, 0)"),
        NpcTunnelVecNear(alongX.Right, FVector3d(0.0, -1.0, 0.0), NPCTUNNEL_ORTHO_TOL));
    TestTrue(TEXT("+X: the Unreal right is (0, 1, 0)"),
        NpcTunnelVecNear(SOLRender::EclipticToUnreal(alongX.Right), FVector3d(0.0, 1.0, 0.0), NPCTUNNEL_ORTHO_TOL));

    // A -> B along (1, 1, 0) / sqrt 2
    const FSOLNpcTunnelBasis diagonal = TunnelBasis(origin, origin + FVector3d(2.0e11, 2.0e11, 0.0));
    TestTrue(TEXT("diagonal: valid"), diagonal.bValid);
    TestTrue(TEXT("diagonal: axis"), NpcTunnelVecNear(diagonal.Axis,
        FVector3d(1.0, 1.0, 0.0) / FMath::Sqrt(2.0), NPCTUNNEL_ORTHO_TOL));
    TestTrue(TEXT("diagonal: up (0, 0, 1)"),
        NpcTunnelVecNear(diagonal.Up, FVector3d(0.0, 0.0, 1.0), NPCTUNNEL_ORTHO_TOL));
    TestTrue(TEXT("diagonal: right = axis x up"), NpcTunnelVecNear(diagonal.Right,
        FVector3d::CrossProduct(diagonal.Axis, diagonal.Up), NPCTUNNEL_ORTHO_TOL));

    // Inclined 10 degrees above the ecliptic: up perpendicular to the axis, in the plane of the axis and +Z, z > 0
    const FVector3d inclinedAxis = NpcTunnelDirection(10.0, 35.0);
    const FSOLNpcTunnelBasis inclined = TunnelBasis(origin, origin + inclinedAxis * 7.0e11);
    TestTrue(TEXT("inclined: valid"), inclined.bValid);
    TestTrue(TEXT("inclined: axis"), NpcTunnelVecNear(inclined.Axis, inclinedAxis, NPCTUNNEL_ORTHO_TOL));
    TestTrue(TEXT("inclined: up perpendicular to the axis"),
        FMath::Abs(FVector3d::DotProduct(inclined.Up, inclined.Axis)) <= NPCTUNNEL_ORTHO_TOL);
    TestTrue(TEXT("inclined: up in the plane of the axis and +Z"), FMath::Abs(FVector3d::DotProduct(inclined.Up,
        FVector3d::CrossProduct(inclinedAxis, FVector3d::UnitZ()))) <= NPCTUNNEL_ORTHO_TOL);
    TestTrue(TEXT("inclined: up has positive z"), inclined.Up.Z > 0.0);
    const FVector3d northProjected = (FVector3d::UnitZ()
        - inclinedAxis * FVector3d::DotProduct(FVector3d::UnitZ(), inclinedAxis)).GetSafeNormal();
    TestTrue(TEXT("inclined: up = N projected off the axis"),
        NpcTunnelVecNear(inclined.Up, northProjected, NPCTUNNEL_ORTHO_TOL));

    // Orthonormal and right-handed for a spread of directions away from the poles
    for (int32 index = 0; index < 24; ++index)
    {
        const double elevation = -80.0 + 160.0 * static_cast<double>(index) / 23.0;
        const double azimuth = 17.0 + 47.0 * static_cast<double>(index);
        const FVector3d direction = NpcTunnelDirection(elevation, azimuth);
        const FSOLNpcTunnelBasis basis = TunnelBasis(origin, origin + direction * 3.0e9);
        const FString label = FString::Printf(TEXT("elevation %.1f azimuth %.1f"), elevation, azimuth);
        TestTrue(label + TEXT(": valid"), basis.bValid);
        TestTrue(label + TEXT(": orthonormal and right-handed"), NpcTunnelIsOrthonormal(basis, NPCTUNNEL_ORTHO_TOL));
        TestTrue(label + TEXT(": up has positive z"), basis.Up.Z > 0.0);
    }

    // Within 1 degree of +Z the reference is +X; from 2 degrees it is north; between, up turns smoothly about the axis
    for (const double offPoleDeg : { 0.0, 0.5, 0.9 })
    {
        for (const double sign : { 1.0, -1.0 })
        {
            const FVector3d direction = NpcTunnelDirection(sign * (90.0 - offPoleDeg), 20.0);
            const FSOLNpcTunnelBasis basis = TunnelBasis(origin, origin + direction * 5.0e8);
            const FString label = FString::Printf(TEXT("%.1f deg off %sZ"), offPoleDeg, sign > 0.0 ? TEXT("+")
                : TEXT("-"));
            const FVector3d xProjected = (FVector3d::UnitX()
                - direction * FVector3d::DotProduct(FVector3d::UnitX(), direction)).GetSafeNormal();
            TestTrue(label + TEXT(": valid"), basis.bValid);
            TestTrue(label + TEXT(": orthonormal"), NpcTunnelIsOrthonormal(basis, NPCTUNNEL_ORTHO_TOL));
            TestTrue(label + TEXT(": up from +X"), NpcTunnelVecNear(basis.Up, xProjected, 1.0e-9));
        }
    }
    for (const double sign : { 1.0, -1.0 })
    {
        const TCHAR* pole = sign > 0.0 ? TEXT("+Z") : TEXT("-Z");
        for (const double offPoleDeg : { 2.0, 2.5, 5.0 })
        {
            const FVector3d direction = NpcTunnelDirection(sign * (90.0 - offPoleDeg), 20.0);
            const FSOLNpcTunnelBasis basis = TunnelBasis(origin, origin + direction * 5.0e8);
            const FVector3d zProjected = (FVector3d::UnitZ()
                - direction * FVector3d::DotProduct(FVector3d::UnitZ(), direction)).GetSafeNormal();
            const FString label = FString::Printf(TEXT("%.1f deg off %s"), offPoleDeg, pole);
            TestTrue(label + TEXT(": up from north"), NpcTunnelVecNear(basis.Up, zProjected, 1.0e-9));
            TestTrue(label + TEXT(": orthonormal"), NpcTunnelIsOrthonormal(basis, NPCTUNNEL_ORTHO_TOL));
        }

        // Half way through the blend (1.5 degrees) up is the bisector of the north and +X ups
        const FVector3d direction = NpcTunnelDirection(sign * 88.5, 20.0);
        const FSOLNpcTunnelBasis basis = TunnelBasis(origin, origin + direction * 5.0e8);
        const FVector3d zUp = (FVector3d::UnitZ() - direction * direction.Z).GetSafeNormal();
        const FVector3d xUp = (FVector3d::UnitX() - direction * direction.X).GetSafeNormal();
        const double toNorth = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(basis.Up, zUp), -1.0, 1.0));
        const double toX = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(basis.Up, xUp), -1.0, 1.0));
        const FString label = FString::Printf(TEXT("1.5 deg off %s"), pole);
        TestTrue(label + TEXT(": orthonormal"), NpcTunnelIsOrthonormal(basis, NPCTUNNEL_ORTHO_TOL));
        TestTrue(label + TEXT(": neither pure north nor pure +X"), toNorth > 1.0e-3 && toX > 1.0e-3);
        TestEqual(label + TEXT(": half way between them"), toNorth, toX, 1.0e-9);

        // Continuous along the pole angle from 0.8 to 2.2 degrees (azimuth 20 degrees)
        FVector3d previousUp = FVector3d::ZeroVector;
        int32 jumps = 0;
        int32 notOrthonormal = 0;
        for (int32 step = 0; step <= 1400; ++step)
        {
            const double offPoleDeg = 0.8 + 0.001 * static_cast<double>(step);
            const FVector3d sweepDirection = NpcTunnelDirection(sign * (90.0 - offPoleDeg), 20.0);
            const FSOLNpcTunnelBasis sweepBasis = TunnelBasis(origin, origin + sweepDirection * 5.0e8);
            notOrthonormal += NpcTunnelIsOrthonormal(sweepBasis, NPCTUNNEL_ORTHO_TOL) ? 0 : 1;
            jumps += step > 0 && (sweepBasis.Up - previousUp).Size() > 0.01 ? 1 : 0;
            previousUp = sweepBasis.Up;
        }
        TestEqual(FString::Printf(TEXT("%s sweep: orthonormal"), pole), notOrthonormal, 0);
        TestEqual(FString::Printf(TEXT("%s sweep: no jump in up"), pole), jumps, 0);
    }

    // Invalid inputs: zero vectors
    const FVector3d invalidEnds[][2] = {
        { origin, origin },
        { FVector3d(NPCTUNNEL_NAN, 0.0, 0.0), origin },
        { origin, FVector3d(0.0, NPCTUNNEL_INF, 0.0) },
        { FVector3d(0.0, 0.0, -NPCTUNNEL_INF), FVector3d(1.0e9, 0.0, 0.0) },
    };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(invalidEnds)); ++index)
    {
        const FSOLNpcTunnelBasis basis = TunnelBasis(invalidEnds[index][0], invalidEnds[index][1]);
        const FString label = FString::Printf(TEXT("invalid case %d"), index);
        TestFalse(label + TEXT(": not valid"), basis.bValid);
        TestTrue(label + TEXT(": zero vectors"), basis.Axis.IsZero() && basis.Up.IsZero() && basis.Right.IsZero());
    }

    // Independent of the lane length and of a common translation
    const FVector3d direction = NpcTunnelDirection(4.0, 123.0);
    const FSOLNpcTunnelBasis shortLane = TunnelBasis(origin, origin + direction * 1.0e3);
    const FSOLNpcTunnelBasis longLane = TunnelBasis(origin, origin + direction * 1.0e12);
    // 1e-8: B = origin + d x 1e3 at 5e10 m already rounds the 1 km lane's direction by about 2e-9
    TestTrue(TEXT("length: same axis"), NpcTunnelVecNear(shortLane.Axis, longLane.Axis, 1.0e-8));
    TestTrue(TEXT("length: same up"), NpcTunnelVecNear(shortLane.Up, longLane.Up, 1.0e-8));
    TestTrue(TEXT("length: same right"), NpcTunnelVecNear(shortLane.Right, longLane.Right, 1.0e-8));
    const FVector3d shift(-2.0e12, 7.0e11, 3.0e10);
    const FSOLNpcTunnelBasis shifted = TunnelBasis(origin + shift, origin + shift + direction * 1.0e9);
    const FSOLNpcTunnelBasis unshifted = TunnelBasis(origin, origin + direction * 1.0e9);
    TestTrue(TEXT("translation: same axis"), NpcTunnelVecNear(shifted.Axis, unshifted.Axis, 1.0e-9));
    TestTrue(TEXT("translation: same up"), NpcTunnelVecNear(shifted.Up, unshifted.Up, 1.0e-9));
    TestTrue(TEXT("translation: same right"), NpcTunnelVecNear(shifted.Right, unshifted.Right, 1.0e-9));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathKeepRightTest, "SOLTest.NpcTunnelMath.KeepRight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 5 (D4): dot row g sits at (g + 0.5) x 360 / ROWS degrees clockwise from 12 o'clock; rows 0-3 are the outbound
// (right) half, 4-7 the inbound; the 90-degree wall projects to the right of a camera looking from A toward B and to
// the left of one looking from B toward A (each direction keeps to the right of its own travel)
bool FSOLNpcTunnelMathKeepRightTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const int32 rows = SOL::NPC_TUNNEL_DOT_ROWS;

    // Row angles and sides
    TestEqual(TEXT("row 0 at 22.5 deg"), DotRowAngleDeg(0), 22.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("row 1 at 67.5 deg"), DotRowAngleDeg(1), 67.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("row 7 at 337.5 deg"), DotRowAngleDeg(7), 337.5, NPCTUNNEL_TOL);
    for (int32 row = 0; row < rows; ++row)
    {
        const FString label = FString::Printf(TEXT("row %d"), row);
        TestEqual(label + TEXT(": angle"), DotRowAngleDeg(row),
            (static_cast<double>(row) + 0.5) * 360.0 / static_cast<double>(rows), NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": side"), DotRowSide(row), row < rows / 2 ? 1 : -1);
        const double angle = DotRowAngleDeg(row);
        TestEqual(label + TEXT(": outbound rows lie in (0, 180) deg"), DotRowSide(row) == 1,
            angle > 0.0 && angle < 180.0);
    }
    for (const int32 badRow : { -1, rows, 100, TNumericLimits<int32>::Min() })
    {
        TestEqual(FString::Printf(TEXT("row %d: side 0"), badRow), DotRowSide(badRow), 0);
    }

    // Wall directions from the basis
    const FVector3d aM(1.0e10, -4.0e9, 2.0e8);
    const FVector3d axis = FVector3d(1.0, 0.6, 0.15).GetSafeNormal();
    const double laneLengthM = 1.0e9;
    const FVector3d bM = aM + axis * laneLengthM;
    const FSOLNpcTunnelBasis basis = TunnelBasis(aM, bM);
    TestTrue(TEXT("0 deg: up"), NpcTunnelVecNear(WallDirection(basis, 0.0), basis.Up, NPCTUNNEL_TOL));
    TestTrue(TEXT("90 deg: right"), NpcTunnelVecNear(WallDirection(basis, 90.0), basis.Right, NPCTUNNEL_TOL));
    TestTrue(TEXT("180 deg: down"), NpcTunnelVecNear(WallDirection(basis, 180.0), -basis.Up, NPCTUNNEL_TOL));
    TestTrue(TEXT("270 deg: left"), NpcTunnelVecNear(WallDirection(basis, 270.0), -basis.Right, NPCTUNNEL_TOL));
    for (int32 row = 0; row < rows; ++row)
    {
        const double rightness = FVector3d::DotProduct(WallDirection(basis, DotRowAngleDeg(row)), basis.Right);
        TestEqual(FString::Printf(TEXT("row %d: wall side matches the row side"), row), rightness > 0.0 ? 1 : -1,
            DotRowSide(row));
    }

    // Seen from behind A looking toward B, the 90-degree wall is on the right of the screen
    const double tunnelRadiusM = 1.0e6;
    const FVector3d cameraBehindA = aM - axis * (20.0 * tunnelRadiusM);
    const FVector3d nearA = aM + axis * (100.0 * tunnelRadiusM);
    const FVector3d wall90A = nearA + WallDirection(basis, 90.0) * tunnelRadiusM;
    const FVector3d wall270A = nearA + WallDirection(basis, 270.0) * tunnelRadiusM;
    TestTrue(TEXT("from A: 90-degree wall on the right"), NpcTunnelScreenX(cameraBehindA, axis, wall90A) > 0.0);
    TestTrue(TEXT("from A: 270-degree wall on the left"), NpcTunnelScreenX(cameraBehindA, axis, wall270A) < 0.0);

    // Seen from beyond B looking toward A, the outbound (90-degree) wall is on the left
    const FVector3d cameraBeyondB = bM + axis * (20.0 * tunnelRadiusM);
    const FVector3d nearB = bM - axis * (100.0 * tunnelRadiusM);
    const FVector3d wall90B = nearB + WallDirection(basis, 90.0) * tunnelRadiusM;
    const FVector3d wall270B = nearB + WallDirection(basis, 270.0) * tunnelRadiusM;
    TestTrue(TEXT("from B: 90-degree wall on the left"), NpcTunnelScreenX(cameraBeyondB, -axis, wall90B) < 0.0);
    TestTrue(TEXT("from B: 270-degree wall on the right"), NpcTunnelScreenX(cameraBeyondB, -axis, wall270B) > 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathNearestTest, "SOLTest.NpcTunnelMath.LaneNearestParam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 6: the segment point nearest the viewpoint (at the origin), its parameter in [0, 1] and its distance; past an
// end the end itself; a degenerate segment gives 0 and a non-finite one 0 and +inf; exact at AU scale
bool FSOLNpcTunnelMathNearestTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;

    // Beside the middle
    const FVector3d midA(-5.0e6, 3.0e6, 0.0);
    const FVector3d midB(5.0e6, 3.0e6, 0.0);
    TestEqual(TEXT("beside the middle: 0.5"), LaneNearestParam(midA, midB), 0.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("beside the middle: distance"), LaneSegmentDistanceM(midA, midB), 3.0e6, NPCTUNNEL_TOL * 3.0e6);

    // Before A and beyond B
    const FVector3d aheadA(1.0e6, 2.0e5, 0.0);
    const FVector3d aheadB(3.0e6, 2.0e5, 0.0);
    TestEqual(TEXT("before A: 0"), LaneNearestParam(aheadA, aheadB), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("before A: distance to A"), LaneSegmentDistanceM(aheadA, aheadB), aheadA.Size(),
        NPCTUNNEL_TOL * aheadA.Size());
    const FVector3d pastA(-3.0e6, 1.0e6, 0.0);
    const FVector3d pastB(-1.0e6, 1.0e6, 0.0);
    TestEqual(TEXT("beyond B: 1"), LaneNearestParam(pastA, pastB), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("beyond B: distance to B"), LaneSegmentDistanceM(pastA, pastB), pastB.Size(),
        NPCTUNNEL_TOL * pastB.Size());

    // The closed form for a spread of segments
    for (int32 index = 0; index < 16; ++index)
    {
        const double k = static_cast<double>(index);
        const FVector3d a(-4.0e7 + 7.3e6 * k, 2.1e7 - 3.3e6 * k, 1.1e6 * (k - 7.0));
        const FVector3d b(3.9e7 - 2.9e6 * k, -1.7e7 + 4.1e6 * k, 9.0e5 * (6.0 - k));
        const FVector3d ab = b - a;
        const double s = FMath::Clamp(-FVector3d::DotProduct(a, ab) / ab.SizeSquared(), 0.0, 1.0);
        const double expectedDistance = (a + ab * s).Size();
        TestEqual(FString::Printf(TEXT("segment %d: param"), index), LaneNearestParam(a, b), s, NPCTUNNEL_TOL);
        TestEqual(FString::Printf(TEXT("segment %d: distance"), index), LaneSegmentDistanceM(a, b), expectedDistance,
            NPCTUNNEL_TOL * FMath::Max(expectedDistance, 1.0));
    }

    // Degenerate and non-finite segments
    const FVector3d point(4.0e6, -3.0e6, 0.0);
    TestEqual(TEXT("degenerate: 0"), LaneNearestParam(point, point), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("degenerate: distance to the point"), LaneSegmentDistanceM(point, point), 5.0e6,
        NPCTUNNEL_TOL * 5.0e6);
    const FVector3d nanA(NPCTUNNEL_NAN, 0.0, 0.0);
    const FVector3d infB(0.0, NPCTUNNEL_INF, 0.0);
    TestEqual(TEXT("NaN A: 0"), LaneNearestParam(nanA, midB), 0.0, NPCTUNNEL_TOL);
    TestTrue(TEXT("NaN A: +inf distance"), LaneSegmentDistanceM(nanA, midB) == NPCTUNNEL_INF);
    TestEqual(TEXT("inf B: 0"), LaneNearestParam(midA, infB), 0.0, NPCTUNNEL_TOL);
    TestTrue(TEXT("inf B: +inf distance"), LaneSegmentDistanceM(midA, infB) == NPCTUNNEL_INF);

    // AU scale: a lane 1 AU long passing 7,500 km from the viewpoint at 40% of its length
    const FVector3d axis = FVector3d(0.8, -0.55, 0.05).GetSafeNormal();
    const FVector3d normal = FVector3d::CrossProduct(axis, FVector3d::UnitZ()).GetSafeNormal();
    const double offsetM = 7.5e6;
    const FVector3d foot = normal * offsetM;
    const FVector3d auA = foot - axis * (0.4 * NPCTUNNEL_AU_M);
    const FVector3d auB = foot + axis * (0.6 * NPCTUNNEL_AU_M);
    TestEqual(TEXT("1 AU lane: param 0.4"), LaneNearestParam(auA, auB), 0.4, NPCTUNNEL_TOL);
    TestEqual(TEXT("1 AU lane: distance 7,500 km"), LaneSegmentDistanceM(auA, auB), offsetM, NPCTUNNEL_TOL * offsetM);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathSpacingTest, "SOLTest.NpcTunnelMath.RingSpacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 7: the local ring spacing is max(BASE x Rt, rho x sqrt(x^2 + h^2)): 2 Rt near the camera, a quarter of the
// distance beyond; continuous, even in x, non-decreasing in |x| and h, scale free; bad inputs give 0
bool FSOLNpcTunnelMathSpacingTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::RingSpacingM;
    const double rt = 1.0e6;
    TestEqual(TEXT("on the axis at the foot: 2 Rt"), RingSpacingM(0.0, 0.0, rt), 2.0 * rt, NPCTUNNEL_TOL * rt);
    TestEqual(TEXT("5 Rt ahead: 2 Rt"), RingSpacingM(5.0 * rt, 0.0, rt), 2.0 * rt, NPCTUNNEL_TOL * rt);
    TestEqual(TEXT("8 Rt ahead: 2 Rt"), RingSpacingM(8.0 * rt, 0.0, rt), 2.0 * rt, NPCTUNNEL_TOL * rt);
    TestEqual(TEXT("100 Rt ahead: 25 Rt"), RingSpacingM(100.0 * rt, 0.0, rt), 25.0 * rt, NPCTUNNEL_TOL * rt);
    TestEqual(TEXT("100 Rt behind: 25 Rt"), RingSpacingM(-100.0 * rt, 0.0, rt), 25.0 * rt, NPCTUNNEL_TOL * rt);
    TestEqual(TEXT("foot point 50 Rt off the axis: 12.5 Rt"), RingSpacingM(0.0, 50.0 * rt, rt), 12.5 * rt,
        NPCTUNNEL_TOL * rt);
    TestEqual(TEXT("3 Rt off, 4 Rt ahead: 2 Rt"), RingSpacingM(4.0 * rt, 3.0 * rt, rt), 2.0 * rt, NPCTUNNEL_TOL * rt);

    // The closed form over a grid, even in x
    for (const double h : { 0.0, 0.5, 7.9, 8.0, 12.0, 50.0, 100.0 })
    {
        for (const double x : { 0.0, 0.3, 1.0, 6.0, 7.99, 8.0, 8.01, 20.0, 300.0, 4.0e5 })
        {
            const FString label = FString::Printf(TEXT("x %.2f Rt, h %.1f Rt"), x, h);
            const double expected = NpcTunnelRefSpacing(x * rt, h * rt, rt);
            TestEqual(label + TEXT(": closed form"), RingSpacingM(x * rt, h * rt, rt), expected,
                NPCTUNNEL_TOL * expected);
            TestEqual(label + TEXT(": even in x"), RingSpacingM(-x * rt, h * rt, rt), RingSpacingM(x * rt, h * rt, rt),
                NPCTUNNEL_TOL * expected);
        }
    }

    // Non-decreasing in |x| and in h, continuous (no step over a fine sweep)
    for (const double h : { 0.0, 2.0, 7.9, 50.0 })
    {
        double previous = 0.0;
        for (int32 step = 0; step <= 4000; ++step)
        {
            const double x = static_cast<double>(step) * 0.01 * rt;
            const double spacing = RingSpacingM(x, h * rt, rt);
            TestTrue(FString::Printf(TEXT("h %.1f: non-decreasing in |x| at step %d"), h, step),
                spacing >= previous - 1.0e-9 * rt);
            TestTrue(FString::Printf(TEXT("h %.1f: continuous at step %d"), h, step),
                step == 0 || spacing - previous <= 0.0026 * rt);
            previous = spacing;
        }
    }
    for (const double x : { 0.0, 3.0, 10.0, 200.0 })
    {
        double previous = 0.0;
        for (int32 step = 0; step <= 400; ++step)
        {
            const double h = static_cast<double>(step) * 0.25 * rt;
            const double spacing = RingSpacingM(x * rt, h, rt);
            TestTrue(FString::Printf(TEXT("x %.1f: non-decreasing in h at step %d"), x, step),
                spacing >= previous - 1.0e-9 * rt);
            previous = spacing;
        }
    }

    // Scale free: spacing(k x, k h, k Rt) == k spacing(x, h, Rt)
    for (const double k : { 1.0e-3, 7.0, 1.0e3 })
    {
        for (const double x : { 0.0, 4.0, 30.0 })
        {
            const double expected = k * RingSpacingM(x * rt, 9.0 * rt, rt);
            TestEqual(FString::Printf(TEXT("scale free k %.3f at x %.0f Rt"), k, x),
                RingSpacingM(k * x * rt, k * 9.0 * rt, k * rt), expected, 1.0e-12 * expected);
        }
    }

    // Bad inputs
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -rt, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("radius %f -> 0"), badRadius), RingSpacingM(10.0 * rt, rt, badRadius), 0.0,
            NPCTUNNEL_TOL);
    }
    TestEqual(TEXT("NaN x -> 0"), RingSpacingM(NPCTUNNEL_NAN, rt, rt), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("NaN h -> 0"), RingSpacingM(10.0 * rt, NPCTUNNEL_NAN, rt), 0.0, NPCTUNNEL_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathWarpTest, "SOLTest.NpcTunnelMath.RingWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8 (Q9, the accordion): the ring warp is odd, strictly increasing, x / (2 Rt) inside x0, its derivative is
// 1 / RingSpacingM, it is continuous at x0 and as h crosses 8 Rt, 4 + 4 ln(|x| / (8 Rt)) on the axis past 8 Rt; the
// inverse round-trips both ways; on the axis the rings are 2 Rt apart inside 8 Rt and e^0.25 apart in distance past it
bool FSOLNpcTunnelMathWarpTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double radialsRt[] = { 0.0, 0.5, 7.9, 8.0, 50.0, 100.0 };
    for (const double rt : { 2.0e5, 3.0e7 })
    {
        for (const double hRt : radialsRt)
        {
            const double h = hRt * rt;
            const double x0 = NpcTunnelX0(h, rt);
            const FString label = FString::Printf(TEXT("Rt %.0e, h %.1f Rt"), rt, hRt);

            // Inside the constant zone: x / (2 Rt)
            for (const double fraction : { 0.0, 0.1, 0.5, 0.97 })
            {
                const double x = fraction * x0;
                TestEqual(label + FString::Printf(TEXT(": x / (2 Rt) at %.2f x0"), fraction), RingWarp(x, h, rt),
                    x / (2.0 * rt), NPCTUNNEL_TOL);
            }

            // Against the test's closed form, odd, over a log sweep of |x| from 0.01 Rt to 1e6 Rt
            double previous = -NPCTUNNEL_INF;
            for (int32 step = -120; step <= 120; ++step)
            {
                const double magnitude = rt * FMath::Pow(10.0, -2.0 + 8.0 * FMath::Abs(static_cast<double>(step))
                    / 120.0);
                const double x = step < 0 ? -magnitude : (step == 0 ? 0.0 : magnitude);
                const double warp = RingWarp(x, h, rt);
                const double expected = NpcTunnelRefWarp(x, h, rt);
                TestEqual(label + FString::Printf(TEXT(": closed form at step %d"), step), warp, expected,
                    NPCTUNNEL_TOL * FMath::Max(1.0, FMath::Abs(expected)));
                TestEqual(label + FString::Printf(TEXT(": odd at step %d"), step), RingWarp(-x, h, rt), -warp,
                    NPCTUNNEL_TOL * FMath::Max(1.0, FMath::Abs(warp)));
                TestTrue(label + FString::Printf(TEXT(": strictly increasing at step %d"), step), warp > previous);
                previous = warp;
            }

            // The derivative is 1 / spacing (central differences at 40 points, away from the x0 kink)
            for (int32 point = 0; point < 40; ++point)
            {
                const double magnitude = rt * FMath::Pow(10.0, -1.5 + 7.0 * static_cast<double>(point) / 39.0);
                const double x = (point % 2 == 0) ? magnitude : -magnitude;
                const double spacing = NpcTunnelRefSpacing(x, h, rt);
                const double dx = 1.0e-4 * spacing;
                if (x0 > 0.0 && FMath::Abs(FMath::Abs(x) - x0) < 4.0 * dx)
                {
                    continue;
                }
                const double derivative = (RingWarp(x + dx, h, rt) - RingWarp(x - dx, h, rt)) / (2.0 * dx);
                TestEqual(label + FString::Printf(TEXT(": derivative at %.4e m"), x), derivative * spacing, 1.0,
                    1.0e-6);
                TestEqual(label + FString::Printf(TEXT(": derivative = 1 / RingSpacingM at %.4e m"), x),
                    derivative * RingSpacingM(x, h, rt), 1.0, 1.0e-6);
            }

            // Continuous at x0
            if (x0 > 0.0)
            {
                const double inner = RingWarp(x0 * (1.0 - 1.0e-10), h, rt);
                const double outer = RingWarp(x0 * (1.0 + 1.0e-10), h, rt);
                TestTrue(label + TEXT(": continuous at x0"), FMath::Abs(outer - inner) < 1.0e-6);
            }

            // Inverse round trips: x -> w -> x up to 4.5e12 m and w -> x -> w for |w| up to 64
            for (const double x : { 0.02 * rt, -0.7 * rt, 3.0 * rt, -7.5 * rt, 9.0 * rt, -64.0 * rt, 1.0e3 * rt,
                -3.7e8, 1.0e11, -4.5e12, 4.5e12 })
            {
                const double back = RingWarpInverse(RingWarp(x, h, rt), h, rt);
                TestEqual(label + FString::Printf(TEXT(": x round trip at %.4e m"), x), back, x,
                    1.0e-9 * FMath::Max(FMath::Abs(x), rt));
            }
            for (int32 step = -64; step <= 64; ++step)
            {
                const double w = static_cast<double>(step) + (step < 0 ? 0.37 : -0.29) * (step == 0 ? 0.0 : 1.0);
                if (FMath::Abs(w) > static_cast<double>(SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE))
                {
                    continue;
                }
                const double x = RingWarpInverse(w, h, rt);
                TestEqual(label + FString::Printf(TEXT(": inverse closed form at w %.2f"), w), x,
                    NpcTunnelRefWarpInverse(w, h, rt), 1.0e-9 * FMath::Max(FMath::Abs(x), rt));
                TestEqual(label + FString::Printf(TEXT(": w round trip at %.2f"), w), RingWarp(x, h, rt), w,
                    1.0e-9 * FMath::Max(FMath::Abs(w), 1.0));
            }
        }

        // Continuous as h crosses 8 Rt
        for (const double xRt : { 0.5, 3.0, 7.0, 20.0, 1.0e3 })
        {
            const double below = RingWarp(xRt * rt, 8.0 * rt * (1.0 - 1.0e-9), rt);
            const double above = RingWarp(xRt * rt, 8.0 * rt * (1.0 + 1.0e-9), rt);
            TestTrue(FString::Printf(TEXT("Rt %.0e: continuous across h = 8 Rt at x %.1f Rt"), rt, xRt),
                FMath::Abs(above - below) < 1.0e-6);
        }

        // The closed form on the axis past 8 Rt
        for (const double x : { 8.0 * rt, 20.0 * rt, 1.0e4 * rt, 4.5e12 })
        {
            const double expected = 4.0 + 4.0 * FMath::Loge(x / (8.0 * rt));
            TestEqual(FString::Printf(TEXT("Rt %.0e: 4 + 4 ln(x / 8 Rt) at %.3e m"), rt, x), RingWarp(x, 0.0, rt),
                expected, NPCTUNNEL_TOL * FMath::Max(1.0, expected));
            TestEqual(FString::Printf(TEXT("Rt %.0e: behind at %.3e m"), rt, x), RingWarp(-x, 0.0, rt), -expected,
                NPCTUNNEL_TOL * FMath::Max(1.0, expected));
        }

        // Spacing on the axis: 2 Rt apart inside 8 Rt, neighbouring distances e^0.25 apart past it
        for (const double phase : { 0.0, 0.3, 0.999 })
        {
            for (int32 n = -3; n <= 3; ++n)
            {
                const double w = static_cast<double>(n) - phase;
                if (w < -4.0 || w + 1.0 > 4.0)
                {
                    continue;
                }
                TestEqual(FString::Printf(TEXT("Rt %.0e, phase %.3f: rings %d, %d 2 Rt apart"), rt, phase, n, n + 1),
                    RingWarpInverse(w + 1.0, 0.0, rt) - RingWarpInverse(w, 0.0, rt), 2.0 * rt, NPCTUNNEL_TOL * rt);
            }
            for (int32 n = 5; n <= 60; n += 5)
            {
                const double w = static_cast<double>(n) - phase;
                const double ratio = RingWarpInverse(w + 1.0, 0.0, rt) / RingWarpInverse(w, 0.0, rt);
                TestEqual(FString::Printf(TEXT("Rt %.0e, phase %.3f: ratio e^0.25 after ring %d"), rt, phase, n),
                    ratio, FMath::Exp(0.25), NPCTUNNEL_TOL * FMath::Exp(0.25));
                const double behindRatio = RingWarpInverse(-w - 1.0, 0.0, rt) / RingWarpInverse(-w, 0.0, rt);
                TestEqual(FString::Printf(TEXT("Rt %.0e, phase %.3f: behind ratio e^0.25 after ring %d"), rt, phase,
                    n), behindRatio, FMath::Exp(0.25), NPCTUNNEL_TOL * FMath::Exp(0.25));
            }
        }
    }

    // Bad inputs
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -1.0e6, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("warp, radius %f -> 0"), badRadius), RingWarp(3.0e7, 1.0e6, badRadius), 0.0,
            NPCTUNNEL_TOL);
        TestEqual(FString::Printf(TEXT("inverse, radius %f -> 0"), badRadius), RingWarpInverse(12.0, 1.0e6,
            badRadius), 0.0, NPCTUNNEL_TOL);
    }
    TestEqual(TEXT("warp, NaN x -> 0"), RingWarp(NPCTUNNEL_NAN, 1.0e6, 1.0e6), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("warp, NaN h -> 0"), RingWarp(3.0e7, NPCTUNNEL_NAN, 1.0e6), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("inverse, NaN w -> 0"), RingWarpInverse(NPCTUNNEL_NAN, 1.0e6, 1.0e6), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("inverse, NaN h -> 0"), RingWarpInverse(12.0, NPCTUNNEL_NAN, 1.0e6), 0.0, NPCTUNNEL_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathNoPopTest, "SOLTest.NpcTunnelMath.NoPopSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8, no pops: for a camera moving in 12,000 small steps along a lane inside the tube (rings leaving at end ring A
// and entering at the cap ahead), across it (h from 0 to 100 Rt, 2,000 steps) and along it outside the tube (rings
// crossing end ring B), with the phase advanced by AdvanceRingPhase and wrapping, the visible rings, dots and
// chevrons change continuously: every element of a step matches one of the neighbouring step, and an unmatched one has
// alpha below 1e-3 (it entered or left at the cap, an end ring or the near fade); along the lane at least one element
// actually crosses an end ring. Per step the phase moves 2e-4, so an element's fades move less than 1e-3
bool FSOLNpcTunnelMathNoPopTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    constexpr int32 stepCount = 2000;
    constexpr int32 alongStepCount = 12000;
    constexpr double phaseStep = 2.0e-4;
    TArray<FNpcTunnelSeenRing> previous;
    TArray<FNpcTunnelSeenRing> current;
    previous.Reserve(RingSlotCount());
    current.Reserve(RingSlotCount());

    // One along-the-lane sweep of one element kind: the foot arc and the phase advance together, the end rings are
    // fixed to the lane; some element must cross an end ring on the way
    const auto alongSweep = [&](const FString& label, const ENpcTunnelElement kind, const double tunnelRadiusM,
        const double radialM, const double endArcAM, const double endArcBM, const double startFootArcM)
    {
        FNpcTunnelFailures failures;
        const double footSpacingM = NpcTunnelRefSpacing(0.0, radialM, tunnelRadiusM);
        const double deltaM = phaseStep * footSpacingM;
        double footArcM = startFootArcM;
        double phase = InitialRingPhase(footArcM, radialM, tunnelRadiusM);
        int32 wraps = 0;
        int32 seenTotal = 0;
        int32 endTransitions = 0;
        FSOLNpcTunnelSlotInput previousInput = NpcTunnelInput(radialM, tunnelRadiusM, phase, endArcAM - footArcM,
            endArcBM - footArcM);
        NpcTunnelVisibleElements(kind, previousInput, previous);
        for (int32 step = 1; step <= alongStepCount; ++step)
        {
            const double nextPhase = AdvanceRingPhase(phase, deltaM, radialM, tunnelRadiusM);
            wraps += nextPhase < phase ? 1 : 0;
            phase = nextPhase;
            footArcM += deltaM;
            const FSOLNpcTunnelSlotInput input = NpcTunnelInput(radialM, tunnelRadiusM, phase, endArcAM - footArcM,
                endArcBM - footArcM);
            NpcTunnelVisibleElements(kind, input, current);
            const FString stepLabel = label + FString::Printf(TEXT(" step %d"), step);
            NpcTunnelCheckStep(current, previous, radialM, tunnelRadiusM, stepLabel + TEXT(" (new vs old)"), failures,
                &input, &endTransitions);
            NpcTunnelCheckStep(previous, current, radialM, tunnelRadiusM, stepLabel + TEXT(" (old vs new)"), failures,
                &previousInput, &endTransitions);
            seenTotal += current.Num();
            Swap(previous, current);
            previousInput = input;
        }
        TestTrue(label + TEXT(": elements were visible"), seenTotal > alongStepCount);
        TestTrue(label + TEXT(": the phase wrapped"), wraps >= 1);
        TestTrue(label + TEXT(": an element crossed an end ring"), endTransitions >= 1);
        if (!TestEqual(label + TEXT(": pops"), failures.Count, 0))
        {
            AddInfo(label + TEXT(": first pop: ") + failures.First);
        }
    };

    // Inside the tube near end ring A (behind elements reach it), a lane long enough that the cap is reached ahead
    {
        const double rt = 3.0e6;
        const double s0 = NpcTunnelS0(rt);
        alongSweep(TEXT("rings along, inside"), ENpcTunnelElement::Ring, rt, 0.5 * rt, 10.0 * rt, 1.0e14,
            s0 * (15.0 + 0.8));
        alongSweep(TEXT("dots along, inside"), ENpcTunnelElement::Dot, rt, 0.5 * rt, 10.0 * rt, 1.0e14,
            s0 * (15.0 + 0.8));
        alongSweep(TEXT("chevrons along, inside"), ENpcTunnelElement::Chevron, rt, 0.5 * rt, 10.0 * rt, 1.0e14,
            s0 * (15.0 + 0.8));
    }

    // Outside the tube (h = 20 Rt, every ring slides) near end ring B
    {
        const double rt = 2.0e5;
        const double h = 20.0 * rt;
        const double footSpacing = NpcTunnelRefSpacing(0.0, h, rt);
        const double endB = 1.0e9;
        alongSweep(TEXT("rings along, outside"), ENpcTunnelElement::Ring, rt, h, 10.0 * rt, endB,
            footSpacing * (FMath::FloorToDouble((endB - 30.0 * rt) / footSpacing) + 0.8));
    }

    // Across the lane: h from 0 to 100 Rt, the phase fixed, end rings out of reach (only the near fade lets a ring
    // in or out)
    {
        const double rt = 1.0e7;
        const double phase = 0.37;
        FNpcTunnelFailures failures;
        double previousH = 0.0;
        NpcTunnelVisibleRings(NpcTunnelInput(0.0, rt, phase, -1.0e20, 1.0e20), previous);
        for (int32 step = 1; step < stepCount; ++step)
        {
            const double h = 100.0 * rt * static_cast<double>(step) / static_cast<double>(stepCount - 1);
            NpcTunnelVisibleRings(NpcTunnelInput(h, rt, phase, -1.0e20, 1.0e20), current);
            const double lowerH = FMath::Min(h, previousH);
            const FString stepLabel = FString::Printf(TEXT("across step %d"), step);
            NpcTunnelCheckStep(current, previous, lowerH, rt, stepLabel + TEXT(" (new vs old)"), failures);
            NpcTunnelCheckStep(previous, current, lowerH, rt, stepLabel + TEXT(" (old vs new)"), failures);
            previousH = h;
            Swap(previous, current);
        }
        if (!TestEqual(TEXT("across: pops"), failures.Count, 0))
        {
            AddInfo(TEXT("across: first pop: ") + failures.First);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathLaneFixedTest, "SOLTest.NpcTunnelMath.LaneFixedNearRings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8, lane-fixed near rings (D6, Q2): within x0 a ring's lane arc (foot arc + x) does not change when the camera
// moves along the lane and the phase is advanced by AdvanceRingPhase (1e-6 Rt), from any starting phase; with
// InitialRingPhase they sit on whole multiples of the base spacing from node A
bool FSOLNpcTunnelMathLaneFixedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double rt = 3.0e6;
    const double h = 2.0 * rt;
    const double x0 = NpcTunnelX0(h, rt);
    const double s0 = NpcTunnelS0(rt);
    const double nearLimit = x0 - 0.25 * rt;
    const double deltasRt[] = { 0.37, -0.11, 1.9, -2.6, 0.05, 3.3, -0.71 };

    for (const bool bInitialPhase : { false, true })
    {
        const FString label = bInitialPhase ? TEXT("initial phase") : TEXT("phase 0.123");
        double footArcM = 1.0e9 + 0.3 * rt;
        double phase = bInitialPhase ? InitialRingPhase(footArcM, h, rt) : 0.123;

        // The reference lattice: one near ring's lane arc at the start
        double referenceArcM = NPCTUNNEL_NAN;
        const FSOLNpcTunnelSlotInput startInput = NpcTunnelInput(h, rt, phase, -1.0e12, 1.0e12);
        for (int32 slot = 0; slot < RingSlotCount(); ++slot)
        {
            FSOLNpcTunnelRing ring;
            if (RingSlot(slot, startInput, ring) && FMath::Abs(ring.AxialOffsetM) < nearLimit)
            {
                referenceArcM = footArcM + ring.AxialOffsetM;
                break;
            }
        }
        if (!TestFalse(label + TEXT(": a near ring at the start"), FMath::IsNaN(referenceArcM)))
        {
            continue;
        }
        if (bInitialPhase)
        {
            const double multiple = referenceArcM / s0;
            TestTrue(label + TEXT(": on a whole multiple of the base spacing"),
                FMath::Abs(multiple - FMath::RoundToDouble(multiple)) * s0 <= 1.0e-6 * rt);
        }

        // Move along the lane; every near ring stays on the lattice
        FNpcTunnelFailures failures;
        int32 nearRings = 0;
        for (int32 step = 0; step < 70; ++step)
        {
            const double deltaM = deltasRt[step % static_cast<int32>(UE_ARRAY_COUNT(deltasRt))] * rt;
            phase = AdvanceRingPhase(phase, deltaM, h, rt);
            footArcM += deltaM;
            const FSOLNpcTunnelSlotInput input = NpcTunnelInput(h, rt, phase, -1.0e12, 1.0e12);
            for (int32 slot = 0; slot < RingSlotCount(); ++slot)
            {
                FSOLNpcTunnelRing ring;
                if (!RingSlot(slot, input, ring) || FMath::Abs(ring.AxialOffsetM) >= nearLimit)
                {
                    continue;
                }
                ++nearRings;
                const double offset = (footArcM + ring.AxialOffsetM - referenceArcM) / s0;
                const double errorM = FMath::Abs(offset - FMath::RoundToDouble(offset)) * s0;
                if (errorM > 1.0e-6 * rt)
                {
                    failures.Add(FString::Printf(TEXT("step %d slot %d: lane arc off the lattice by %.6e m"), step,
                        slot, errorM));
                }
            }
        }
        TestTrue(label + TEXT(": near rings were checked"), nearRings > 70);
        if (!TestEqual(label + TEXT(": near rings fixed to the lane"), failures.Count, 0))
        {
            AddInfo(label + TEXT(": first: ") + failures.First);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathPhaseTest, "SOLTest.NpcTunnelMath.RingPhase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 9: the ring phase advances by the foot arc change over the spacing at the foot point, frac in [0, 1) in double
// (a negative delta wraps up, a 4.5e12 m delta stays in range); a NaN or infinite delta or a bad radius keeps it; the
// initial phase is frac(foot arc / spacing), NaN -> 0
bool FSOLNpcTunnelMathPhaseTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double rt = 1.0e6;
    const double s0 = NpcTunnelS0(rt);
    TestEqual(TEXT("0.2 + half a spacing -> 0.7"), AdvanceRingPhase(0.2, 0.5 * s0, 0.0, rt), 0.7, NPCTUNNEL_TOL);
    TestEqual(TEXT("0.7 + half a spacing wraps to 0.2"), AdvanceRingPhase(0.7, 0.5 * s0, 0.0, rt), 0.2,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("0.2 - half a spacing wraps up to 0.7"), AdvanceRingPhase(0.2, -0.5 * s0, 0.0, rt), 0.7,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("h 5 Rt: still the base spacing"), AdvanceRingPhase(0.1, 0.25 * s0, 5.0 * rt, rt), 0.35,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("h 50 Rt: spacing 12.5 Rt"), AdvanceRingPhase(0.1, 6.25 * rt, 50.0 * rt, rt), 0.6, NPCTUNNEL_TOL);
    TestEqual(TEXT("zero delta keeps the phase"), AdvanceRingPhase(0.42, 0.0, 0.0, rt), 0.42, NPCTUNNEL_TOL);

    // Huge deltas stay in range and match the closed form
    for (const double delta : { 4.5e12, -4.5e12, 4.5e12 + 0.37 * s0, 1.234567e11 })
    {
        const double phase = AdvanceRingPhase(0.25, delta, 0.0, rt);
        TestTrue(FString::Printf(TEXT("delta %.4e: in [0, 1)"), delta), phase >= 0.0 && phase < 1.0);
        TestTrue(FString::Printf(TEXT("delta %.4e: closed form"), delta),
            NpcTunnelWrapDistance(phase, NpcTunnelFrac(0.25 + delta / s0)) < 1.0e-6);
    }

    // Always in [0, 1) over a sweep of deltas
    double phase = 0.0;
    for (int32 step = -300; step <= 300; ++step)
    {
        phase = AdvanceRingPhase(phase, static_cast<double>(step) * 0.173 * rt, 3.0 * rt, rt);
        TestTrue(FString::Printf(TEXT("sweep step %d: in [0, 1)"), step), phase >= 0.0 && phase < 1.0);
    }

    // A bad delta or radius keeps the phase
    for (const double badDelta : { NPCTUNNEL_NAN, NPCTUNNEL_INF, -NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("delta %f keeps the phase"), badDelta), AdvanceRingPhase(0.31, badDelta, 0.0,
            rt), 0.31, NPCTUNNEL_TOL);
    }
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -rt, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("radius %f keeps the phase"), badRadius), AdvanceRingPhase(0.31, 1.0e6, 0.0,
            badRadius), 0.31, NPCTUNNEL_TOL);
    }

    // A phase outside [0, 1) is wrapped and a non-finite one reads as 0, so the result is always finite and in range
    TestEqual(TEXT("phase 1.3 wraps to 0.3"), AdvanceRingPhase(1.3, 0.0, 0.0, rt), 0.3, NPCTUNNEL_TOL);
    TestEqual(TEXT("phase -0.25 wraps to 0.75"), AdvanceRingPhase(-0.25, 0.0, 0.0, rt), 0.75, NPCTUNNEL_TOL);
    TestEqual(TEXT("NaN phase reads as 0"), AdvanceRingPhase(NPCTUNNEL_NAN, 0.5 * s0, 0.0, rt), 0.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("infinite phase reads as 0"), AdvanceRingPhase(NPCTUNNEL_INF, 0.0, 0.0, rt), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("NaN phase and NaN delta: 0"), AdvanceRingPhase(NPCTUNNEL_NAN, NPCTUNNEL_NAN, 0.0, rt), 0.0,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("phase 1.31 kept wrapped on a bad delta"), AdvanceRingPhase(1.31, NPCTUNNEL_NAN, 0.0, rt), 0.31,
        1.0e-12);

    // The slots wrap the phase too: 1.3 lays out the rings of 0.3, a NaN phase those of 0
    {
        const FSOLNpcTunnelSlotInput wrapped = NpcTunnelInput(0.0, rt, 1.3, -1.0e12, 1.0e12);
        const FSOLNpcTunnelSlotInput plain = NpcTunnelInput(0.0, rt, 0.3, -1.0e12, 1.0e12);
        const FSOLNpcTunnelSlotInput nanPhase = NpcTunnelInput(0.0, rt, NPCTUNNEL_NAN, -1.0e12, 1.0e12);
        const FSOLNpcTunnelSlotInput zeroPhase = NpcTunnelInput(0.0, rt, 0.0, -1.0e12, 1.0e12);
        int32 mismatches = 0;
        for (int32 slot = 0; slot < RingSlotCount(); ++slot)
        {
            FSOLNpcTunnelRing a;
            FSOLNpcTunnelRing b;
            const bool bA = RingSlot(slot, wrapped, a);
            const bool bB = RingSlot(slot, plain, b);
            mismatches += bA != bB || FMath::Abs(a.AxialOffsetM - b.AxialOffsetM) > 1.0e-6 * rt ? 1 : 0;
            const bool bC = RingSlot(slot, nanPhase, a);
            const bool bD = RingSlot(slot, zeroPhase, b);
            mismatches += bC != bD || FMath::Abs(a.AxialOffsetM - b.AxialOffsetM) > 1.0e-6 * rt ? 1 : 0;
            mismatches += FMath::Abs(RingSlotOffsetM(slot, 1.3f, 0.0f, static_cast<float>(rt))
                - RingSlotOffsetM(slot, 0.3f, 0.0f, static_cast<float>(rt))) > 1.0e-3 * rt ? 1 : 0;
            mismatches += FMath::IsFinite(RingSlotOffsetM(slot, NAN, 0.0f, static_cast<float>(rt))) ? 0 : 1;
        }
        for (int32 slot = 0; slot < DetailSlotCount(); ++slot)
        {
            FSOLNpcTunnelRing a;
            FSOLNpcTunnelRing b;
            const bool bA = ChevronSlot(slot, wrapped, a);
            const bool bB = ChevronSlot(slot, plain, b);
            mismatches += bA != bB || FMath::Abs(a.AxialOffsetM - b.AxialOffsetM) > 1.0e-6 * rt ? 1 : 0;
            const bool bC = DotSlot(slot, 1, wrapped, a);
            const bool bD = DotSlot(slot, 1, plain, b);
            mismatches += bC != bD || FMath::Abs(a.AxialOffsetM - b.AxialOffsetM) > 1.0e-6 * rt ? 1 : 0;
        }
        TestEqual(TEXT("slots wrap the phase"), mismatches, 0);
    }

    // The initial phase
    TestEqual(TEXT("initial: 0 at arc 0"), InitialRingPhase(0.0, 0.0, rt), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("initial: 3.25 spacings -> 0.25"), InitialRingPhase(3.25 * s0, 0.0, rt), 0.25, NPCTUNNEL_TOL);
    TestEqual(TEXT("initial: -0.25 spacings wraps up to 0.75"), InitialRingPhase(-0.25 * s0, 0.0, rt), 0.75,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("initial: h 50 Rt uses 12.5 Rt"), InitialRingPhase(31.25 * rt, 50.0 * rt, rt), 0.5,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("initial: NaN -> 0"), InitialRingPhase(NPCTUNNEL_NAN, 0.0, rt), 0.0, NPCTUNNEL_TOL);
    for (const double footArc : { 1.0e6, 3.7e8, 4.5e12 })
    {
        const double initial = InitialRingPhase(footArc, 0.0, rt);
        TestTrue(FString::Printf(TEXT("initial at %.2e m: in [0, 1)"), footArc), initial >= 0.0 && initial < 1.0);
        TestTrue(FString::Printf(TEXT("initial at %.2e m: closed form"), footArc),
            NpcTunnelWrapDistance(initial, NpcTunnelFrac(footArc / s0)) < 1.0e-6);

        // Advancing from the initial phase agrees with the initial phase of the moved foot point
        const double delta = 0.613 * s0;
        TestTrue(FString::Printf(TEXT("initial at %.2e m: advance agrees"), footArc),
            NpcTunnelWrapDistance(AdvanceRingPhase(initial, delta, 0.0, rt), InitialRingPhase(footArc + delta, 0.0,
                rt)) < 1.0e-6);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathFloatPathTest, "SOLTest.NpcTunnelMath.RingSlotFloatPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 9, float path: rebuilding a ring's camera-relative axial offset from the phase and the slot index in float (the
// shader's path, RingSlotOffsetM) is within 1e-3 Rt of the double result for foot arcs up to 4.5e12 m at Rt = 2e5 m
// and 3e7 m and cameras up to 100 Rt off the axis, for every ring within 64 Rt of the foot point; farther rings stay
// within 1e-5 of their distance (a fraction of a pixel there)
bool FSOLNpcTunnelMathFloatPathTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    FNpcTunnelFailures failures;
    int32 checked = 0;
    for (const double rt : { 2.0e5, 3.0e7 })
    {
        for (const double footArc : { 0.0, 1.234e6, 3.7e8, 1.0e11, 4.5e12 })
        {
            for (const double hRt : { 0.0, 0.5, 5.0, 20.0, 50.0, 100.0 })
            {
                const double h = hRt * rt;
                const double phase = InitialRingPhase(footArc, h, rt);
                for (int32 slot = 0; slot < RingSlotCount(); ++slot)
                {
                    const double expected = NpcTunnelRefWarpInverse(NpcTunnelSlotW(slot, phase), h, rt);
                    const double rebuilt = static_cast<double>(RingSlotOffsetM(slot, static_cast<float>(phase),
                        static_cast<float>(h), static_cast<float>(rt)));
                    const double tolerance = FMath::Abs(expected) <= 64.0 * rt ? 1.0e-3 * rt
                        : FMath::Max(1.0e-3 * rt, 1.0e-5 * FMath::Abs(expected));
                    ++checked;
                    if (!(FMath::Abs(rebuilt - expected) <= tolerance))
                    {
                        failures.Add(FString::Printf(TEXT("Rt %.0e, arc %.3e, h %.1f Rt, slot %d: %.9e vs %.9e"), rt,
                            footArc, hRt, slot, rebuilt, expected));
                    }
                }
            }
        }
    }
    TestEqual(TEXT("every slot checked"), checked, 2 * 5 * 6 * RingSlotCount());
    if (!TestEqual(TEXT("float path within tolerance"), failures.Count, 0))
    {
        AddInfo(TEXT("first: ") + failures.First);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathFadesTest, "SOLTest.NpcTunnelMath.Fades",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 10: the depth fade (1 inside 8 Rt, log-linear to the floor at 65,536 Rt, never below it), the near fade (0
// within 0.5 Rt, smoothstep to 1 at 1.5 Rt, symmetric), the detail fade (1 inside 256 Rt, 0 from 512 Rt), the end
// fade, the ring cap fade and the detail cap fade; all scale free; bad inputs give the floor (depth) or 0
bool FSOLNpcTunnelMathFadesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double rt = 1.0e6;
    const double floorAlpha = SOL::NPC_TUNNEL_DEPTH_FADE_FLOOR;
    const double depthStart = SOL::NPC_TUNNEL_DEPTH_FADE_START_RADII;
    const double depthEnd = SOL::NPC_TUNNEL_DEPTH_FADE_END_RADII;

    // Depth fade
    TestEqual(TEXT("depth: 1 at 0"), TunnelDepthFade(0.0, rt), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("depth: 1 at 4 Rt"), TunnelDepthFade(4.0 * rt, rt), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("depth: 1 at 8 Rt"), TunnelDepthFade(8.0 * rt, rt), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("depth: the floor at 65,536 Rt"), TunnelDepthFade(65536.0 * rt, rt), floorAlpha, NPCTUNNEL_TOL);
    TestEqual(TEXT("depth: the floor at 1e7 Rt"), TunnelDepthFade(1.0e7 * rt, rt), floorAlpha, NPCTUNNEL_TOL);
    TestEqual(TEXT("depth: the floor at +inf"), TunnelDepthFade(NPCTUNNEL_INF, rt), floorAlpha, NPCTUNNEL_TOL);
    for (const double t : { 0.25, 0.5 })
    {
        const double d = depthStart * rt * FMath::Pow(depthEnd / depthStart, t);
        TestEqual(FString::Printf(TEXT("depth: log-linear at t %.2f"), t), TunnelDepthFade(d, rt),
            1.0 - (1.0 - floorAlpha) * t, NPCTUNNEL_TOL);
    }
    double previous = 2.0;
    for (int32 step = 0; step <= 300; ++step)
    {
        const double d = rt * FMath::Pow(10.0, -1.0 + 7.0 * static_cast<double>(step) / 300.0);
        const double fade = TunnelDepthFade(d, rt);
        TestTrue(FString::Printf(TEXT("depth: non-increasing at step %d"), step), fade <= previous + 1.0e-12);
        TestTrue(FString::Printf(TEXT("depth: in [floor, 1] at step %d"), step), fade >= floorAlpha - 1.0e-12
            && fade <= 1.0);
        previous = fade;
    }
    TestEqual(TEXT("depth: scale free"), TunnelDepthFade(1.0e3 * 777.0 * rt, 1.0e3 * rt), TunnelDepthFade(777.0 * rt,
        rt), NPCTUNNEL_TOL);
    TestEqual(TEXT("depth: NaN distance -> the floor"), TunnelDepthFade(NPCTUNNEL_NAN, rt), floorAlpha,
        NPCTUNNEL_TOL);
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -rt, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("depth: radius %f -> the floor"), badRadius), TunnelDepthFade(10.0 * rt,
            badRadius), floorAlpha, NPCTUNNEL_TOL);
    }

    // Near fade
    for (const double x : { 0.0, 0.3, -0.3, 0.5, -0.5 })
    {
        TestEqual(FString::Printf(TEXT("near: 0 at %.1f Rt"), x), TunnelNearFade(x * rt, rt), 0.0, NPCTUNNEL_TOL);
    }
    for (const double x : { 1.5, -1.5, 2.0, 100.0, -1.0e6 })
    {
        TestEqual(FString::Printf(TEXT("near: 1 at %.1f Rt"), x), TunnelNearFade(x * rt, rt), 1.0, NPCTUNNEL_TOL);
    }
    for (const double x : { 0.6, 0.75, 1.0, 1.25, 1.4 })
    {
        const double t = x - 0.5;
        const double expected = t * t * (3.0 - 2.0 * t);
        TestEqual(FString::Printf(TEXT("near: smoothstep at %.2f Rt"), x), TunnelNearFade(x * rt, rt), expected,
            NPCTUNNEL_TOL);
        TestEqual(FString::Printf(TEXT("near: symmetric at %.2f Rt"), x), TunnelNearFade(-x * rt, rt), expected,
            NPCTUNNEL_TOL);
    }
    TestEqual(TEXT("near: 0.5 at 1 Rt"), TunnelNearFade(rt, rt), 0.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("near: scale free"), TunnelNearFade(1.0e3 * 0.9 * rt, 1.0e3 * rt), TunnelNearFade(0.9 * rt, rt),
        NPCTUNNEL_TOL);
    TestEqual(TEXT("near: NaN x -> 0"), TunnelNearFade(NPCTUNNEL_NAN, rt), 0.0, NPCTUNNEL_TOL);
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -rt, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("near: radius %f -> 0"), badRadius), TunnelNearFade(3.0 * rt, badRadius), 0.0,
            NPCTUNNEL_TOL);
    }

    // Detail fade
    for (const double d : { 0.0, 100.0, 256.0 })
    {
        TestEqual(FString::Printf(TEXT("detail: 1 at %.0f Rt"), d), TunnelDetailFade(d * rt, rt), 1.0, NPCTUNNEL_TOL);
    }
    for (const double d : { 512.0, 1.0e4, 1.0e9 })
    {
        TestEqual(FString::Printf(TEXT("detail: 0 at %.0f Rt"), d), TunnelDetailFade(d * rt, rt), 0.0, NPCTUNNEL_TOL);
    }
    TestEqual(TEXT("detail: 0 at +inf"), TunnelDetailFade(NPCTUNNEL_INF, rt), 0.0, NPCTUNNEL_TOL);
    previous = 1.0;
    for (int32 step = 1; step < 1000; ++step)
    {
        const double d = (256.0 + 256.0 * static_cast<double>(step) / 1000.0) * rt;
        const double fade = TunnelDetailFade(d, rt);
        TestTrue(FString::Printf(TEXT("detail: inside (0, 1) at step %d"), step), fade > 0.0 && fade < 1.0);
        TestTrue(FString::Printf(TEXT("detail: non-increasing and continuous at step %d"), step),
            fade <= previous + 1.0e-12 && previous - fade < 0.01);
        previous = fade;
    }
    TestEqual(TEXT("detail: scale free"), TunnelDetailFade(1.0e3 * 300.0 * rt, 1.0e3 * rt),
        TunnelDetailFade(300.0 * rt, rt), NPCTUNNEL_TOL);
    TestEqual(TEXT("detail: NaN -> 0"), TunnelDetailFade(NPCTUNNEL_NAN, rt), 0.0, NPCTUNNEL_TOL);
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -rt, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("detail: radius %f -> 0"), badRadius), TunnelDetailFade(10.0 * rt, badRadius),
            0.0, NPCTUNNEL_TOL);
    }

    // End fade: clamp(dEnd / (0.5 spacing), 0, 1)
    const double spacing = 3.0 * rt;
    TestEqual(TEXT("end: 0 at the end ring"), TunnelEndFade(0.0, spacing), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("end: 0 outside the tunnel"), TunnelEndFade(-rt, spacing), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("end: 0.5 a quarter spacing inside"), TunnelEndFade(0.25 * spacing, spacing), 0.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("end: 1 half a spacing inside"), TunnelEndFade(0.5 * spacing, spacing), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("end: 1 far inside"), TunnelEndFade(40.0 * spacing, spacing), 1.0, NPCTUNNEL_TOL);
    for (const double fraction : { 0.05, 0.13, 0.31, 0.49 })
    {
        TestEqual(FString::Printf(TEXT("end: closed form at %.2f spacings"), fraction),
            TunnelEndFade(fraction * spacing, spacing),
            fraction / static_cast<double>(SOL::NPC_TUNNEL_END_FADE_SPACINGS), NPCTUNNEL_TOL);
    }
    TestEqual(TEXT("end: NaN distance -> 0"), TunnelEndFade(NPCTUNNEL_NAN, spacing), 0.0, NPCTUNNEL_TOL);
    for (const double badSpacing : { NPCTUNNEL_NAN, 0.0, -spacing, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("end: spacing %f -> 0"), badSpacing), TunnelEndFade(rt, badSpacing), 0.0,
            NPCTUNNEL_TOL);
    }

    // Cap fade: clamp(PER_SIDE - |w|, 0, 1)
    const double perSide = static_cast<double>(SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE);
    TestEqual(TEXT("cap: 1 at w 0"), RingCapFade(0.0), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("cap: 1 at w 63"), RingCapFade(perSide - 1.0), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("cap: 0.5 at w 63.5"), RingCapFade(perSide - 0.5), 0.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("cap: 0.75 at w -63.25"), RingCapFade(-(perSide - 0.75)), 0.75, NPCTUNNEL_TOL);
    TestEqual(TEXT("cap: 0 at w 64"), RingCapFade(perSide), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("cap: 0 at w -70"), RingCapFade(-70.0), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("cap: NaN -> 0"), RingCapFade(NPCTUNNEL_NAN), 0.0, NPCTUNNEL_TOL);

    // Detail cap fade: clamp(DETAIL_PER_SIDE - 1 - |w|, 0, 1), so an element appearing at the far end of the gap slots
    // (up to |w| = DETAIL_PER_SIDE - 0.25) enters at alpha 0
    const double detailPerSide = static_cast<double>(SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE);
    TestEqual(TEXT("detail cap: 1 at w 0"), DetailCapFade(0.0), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail cap: 1 at w 22"), DetailCapFade(detailPerSide - 2.0), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail cap: 0.5 at w -22.5"), DetailCapFade(-(detailPerSide - 1.5)), 0.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail cap: 0 at w 23"), DetailCapFade(detailPerSide - 1.0), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail cap: 0 at w 23.75"), DetailCapFade(detailPerSide - 0.25), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail cap: NaN -> 0"), DetailCapFade(NPCTUNNEL_NAN), 0.0, NPCTUNNEL_TOL);
    TestTrue(TEXT("detail cap: 1 wherever the detail fade shows (on the axis out to 512 Rt)"),
        RingWarp(SOL::NPC_TUNNEL_DETAIL_FADE_END_RADII * rt, 0.0, rt) <= detailPerSide - 2.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathNearFadeRadialTest, "SOLTest.NpcTunnelMath.NearFadeRadial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 10 amendment (user, 2026-11-12): the near fade acts fully while the camera is within 1.5 Rt of the axis and not
// at all from 3 Rt (smoothstep between), so seen from outside the tube the nearest ring stays; inside it is hidden;
// the nearest ring's alpha changes continuously as the camera crosses the band
bool FSOLNpcTunnelMathNearFadeRadialTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double rt = 1.0e6;
    const double fullRadii = SOL::NPC_TUNNEL_NEAR_FADE_RADIAL_FULL_RADII;
    const double noneRadii = SOL::NPC_TUNNEL_NEAR_FADE_RADIAL_NONE_RADII;

    // The weight: 1 near the axis, 0 well outside, smoothstep between, non-increasing and continuous
    for (const double h : { 0.0, 0.7, fullRadii })
    {
        TestEqual(FString::Printf(TEXT("weight 1 at h %.2f Rt"), h), TunnelNearFadeRadialWeight(h * rt, rt), 1.0,
            NPCTUNNEL_TOL);
    }
    for (const double h : { noneRadii, 10.0, 100.0 })
    {
        TestEqual(FString::Printf(TEXT("weight 0 at h %.2f Rt"), h), TunnelNearFadeRadialWeight(h * rt, rt), 0.0,
            NPCTUNNEL_TOL);
    }
    TestEqual(TEXT("weight 0.5 mid-band"), TunnelNearFadeRadialWeight(0.5 * (fullRadii + noneRadii) * rt, rt), 0.5,
        NPCTUNNEL_TOL);
    {
        const double t = 0.25;
        TestEqual(TEXT("weight smoothstep a quarter in"),
            TunnelNearFadeRadialWeight((fullRadii + t * (noneRadii - fullRadii)) * rt, rt),
            1.0 - t * t * (3.0 - 2.0 * t), NPCTUNNEL_TOL);
    }
    double previous = 1.0;
    for (int32 step = 0; step <= 1000; ++step)
    {
        const double weight = TunnelNearFadeRadialWeight(0.005 * static_cast<double>(step) * rt, rt);
        TestTrue(FString::Printf(TEXT("weight non-increasing and continuous at step %d"), step),
            weight <= previous + 1.0e-12 && previous - weight <= 0.01);
        previous = weight;
    }
    TestEqual(TEXT("weight: scale free"), TunnelNearFadeRadialWeight(1.0e3 * 2.0 * rt, 1.0e3 * rt),
        TunnelNearFadeRadialWeight(2.0 * rt, rt), NPCTUNNEL_TOL);
    TestEqual(TEXT("weight: NaN h -> 1"), TunnelNearFadeRadialWeight(NPCTUNNEL_NAN, rt), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("weight: bad radius -> 1"), TunnelNearFadeRadialWeight(rt, 0.0), 1.0, NPCTUNNEL_TOL);

    // The weighted near fade: the plain one near the axis, 1 well outside, the blend between
    for (const double x : { 0.0, 0.3, 0.8, 1.2, 2.0 })
    {
        for (const double h : { 0.0, 1.0, fullRadii })
        {
            TestEqual(FString::Printf(TEXT("near at: x %.1f Rt, h %.1f Rt = the plain near fade"), x, h),
                TunnelNearFadeAt(x * rt, h * rt, rt), TunnelNearFade(x * rt, rt), NPCTUNNEL_TOL);
        }
        for (const double h : { noneRadii, 25.0, 100.0 })
        {
            TestEqual(FString::Printf(TEXT("near at: x %.1f Rt, h %.1f Rt = 1"), x, h),
                TunnelNearFadeAt(x * rt, h * rt, rt), 1.0, NPCTUNNEL_TOL);
        }
        const double hMid = 2.0 * rt;
        TestEqual(FString::Printf(TEXT("near at: x %.1f Rt, mid-band"), x), TunnelNearFadeAt(x * rt, hMid, rt),
            1.0 - TunnelNearFadeRadialWeight(hMid, rt) * (1.0 - TunnelNearFade(x * rt, rt)), NPCTUNNEL_TOL);
    }
    TestEqual(TEXT("near at: NaN x -> 0"), TunnelNearFadeAt(NPCTUNNEL_NAN, 50.0 * rt, rt), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("near at: NaN h -> 0"), TunnelNearFadeAt(rt, NPCTUNNEL_NAN, rt), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("near at: bad radius -> 0"), TunnelNearFadeAt(rt, 50.0 * rt, -rt), 0.0, NPCTUNNEL_TOL);

    // The ring at the foot point (phase 0: the first behind slot, w = 0, x = 0): hidden inside the tube, present with
    // its depth fade from outside
    const int32 footSlot = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    for (const double h : { 0.0, 0.5, 1.0, fullRadii })
    {
        FSOLNpcTunnelRing ring;
        TestFalse(FString::Printf(TEXT("foot ring hidden at h %.1f Rt"), h),
            RingSlot(footSlot, NpcTunnelInput(h * rt, rt, 0.0, -1.0e12, 1.0e12), ring));
    }
    for (const double h : { 25.0, 50.0, 75.0, 100.0 })
    {
        FSOLNpcTunnelRing ring;
        const bool bShown = RingSlot(footSlot, NpcTunnelInput(h * rt, rt, 0.0, -1.0e12, 1.0e12), ring);
        TestTrue(FString::Printf(TEXT("foot ring present at h %.0f Rt"), h), bShown);
        TestEqual(FString::Printf(TEXT("foot ring at h %.0f Rt: at the foot point"), h), ring.AxialOffsetM, 0.0,
            NPCTUNNEL_TOL * rt);
        TestEqual(FString::Printf(TEXT("foot ring at h %.0f Rt: only the depth fade"), h), ring.Alpha,
            TunnelDepthFade(h * rt, rt), 1.0e-12);
    }

    // The foot ring's alpha changes continuously as the camera moves out through the band
    double previousAlpha = 0.0;
    int32 jumps = 0;
    for (int32 step = 0; step <= 1000; ++step)
    {
        const double h = 0.005 * static_cast<double>(step) * rt;
        FSOLNpcTunnelRing ring;
        const double alpha = RingSlot(footSlot, NpcTunnelInput(h, rt, 0.0, -1.0e12, 1.0e12), ring) ? ring.Alpha : 0.0;
        jumps += FMath::Abs(alpha - previousAlpha) > 0.01 ? 1 : 0;
        previousAlpha = alpha;
    }
    TestEqual(TEXT("foot ring: no alpha jump across the band"), jumps, 0);
    TestEqual(TEXT("foot ring: full at 5 Rt"), previousAlpha, 1.0, NPCTUNNEL_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathRingCoverageTest, "SOLTest.NpcTunnelMath.RingSlotCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 11: against a brute-force reference (every whole n with W^-1(n - phase) between the end rings and a non-zero
// alpha), for cameras on the axis and 0.5-100 Rt off it, at both ends (and just past end A) and the middle of the
// shortest (Earth-Moon at perigee) and longest (Uranus-Neptune at 30 AU) default lanes, at both radius scales and
// phases 0, 0.3 and 0.999, every visible ring is returned by exactly one slot with the same offset and alpha, and no
// slot returns a ring outside the end rings; slot k is the ring at w = k + 1 - phase ahead, -(k + phase) behind; every
// default lane fits its rings in the slots
bool FSOLNpcTunnelMathRingCoverageTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    TestEqual(TEXT("two slots per ring index"), RingSlotCount(), 2 * perSide);

    // The two lanes: length, body radii, lane radius
    struct FLaneCase
    {
        const TCHAR* Name;
        double LengthM;
        double BodyRadiusAM;
        double BodyRadiusBM;
        double LaneRadiusM;
    };
    const FLaneCase lanes[] = {
        { TEXT("Earth-Moon"), NPCTUNNEL_EARTH_MOON_PERIGEE_M, NPCTUNNEL_EARTH_RADIUS_M, NPCTUNNEL_MOON_RADIUS_M,
            NPCTUNNEL_EARTH_MOON_LANE_RADIUS_M },
        { TEXT("Uranus-Neptune"), NPCTUNNEL_URANUS_NEPTUNE_M, NPCTUNNEL_URANUS_RADIUS_M, NPCTUNNEL_NEPTUNE_RADIUS_M,
            NPCTUNNEL_URANUS_NEPTUNE_LANE_RADIUS_M },
    };
    const double scales[] = { SOL::NPC_TUNNEL_RADIUS_MIN_SCALE, SOL::NPC_TUNNEL_RADIUS_MAX_SCALE };
    const double radialsRt[] = { 0.0, 0.5, 1.0, 10.0, 50.0, 100.0 };
    const double phases[] = { 0.0, 0.3, 0.999 };

    FNpcTunnelFailures failures;
    FNpcTunnelFailures layoutFailures;
    TArray<FNpcTunnelRefElement> reference;
    TArray<FSOLNpcTunnelRing> returned;
    int32 visibleTotal = 0;
    for (const FLaneCase& lane : lanes)
    {
        for (const double scale : scales)
        {
            const double rt = lane.LaneRadiusM * scale;
            const double endA = lane.BodyRadiusAM + SOL::NPC_TUNNEL_END_MARGIN_RADII * rt;
            const double endB = lane.LengthM - lane.BodyRadiusBM - SOL::NPC_TUNNEL_END_MARGIN_RADII * rt;
            const double footArcs[] = { endA, 0.5 * (endA + endB), endB, endA - 3.0 * rt };
            for (const double footArc : footArcs)
            {
                for (const double hRt : radialsRt)
                {
                    for (const double phase : phases)
                    {
                        const FSOLNpcTunnelSlotInput input = NpcTunnelInput(hRt * rt, rt, phase, endA - footArc,
                            endB - footArc);
                        const FString label = FString::Printf(TEXT("%s x%.0f, foot %.4e m, h %.1f Rt, phase %.3f"),
                            lane.Name, scale, footArc, hRt, phase);
                        NpcTunnelBruteRings(input, reference);
                        visibleTotal += reference.Num();
                        returned.Reset();
                        for (int32 slot = 0; slot < RingSlotCount(); ++slot)
                        {
                            FSOLNpcTunnelRing ring;
                            if (!RingSlot(slot, input, ring))
                            {
                                continue;
                            }
                            returned.Add(ring);

                            // The slot layout of SDD 49 section 4.5
                            if (FMath::Abs(ring.W - NpcTunnelSlotW(slot, phase)) > NPCTUNNEL_TOL)
                            {
                                layoutFailures.Add(label + FString::Printf(TEXT(": slot %d at w %.9f, expected %.9f"),
                                    slot, ring.W, NpcTunnelSlotW(slot, phase)));
                            }
                        }
                        NpcTunnelMatchCoverage(reference, returned, input, label, failures);
                    }
                }
            }
        }
    }
    TestTrue(TEXT("the reference had visible rings"), visibleTotal > 1000);
    if (!TestEqual(TEXT("every visible ring exactly once, none outside"), failures.Count, 0))
    {
        AddInfo(TEXT("first: ") + failures.First);
    }
    if (!TestEqual(TEXT("slot layout"), layoutFailures.Count, 0))
    {
        AddInfo(TEXT("first: ") + layoutFailures.First);
    }

    // Every default lane at its minimum tunnel radius fits its whole length in the slots on the axis (lengths are the
    // bodies' aphelia on opposite sides of the Sun, an upper bound; Earth-Moon at its apogee)
    struct FDefaultLane
    {
        const TCHAR* Name;
        double MaxLengthM;
        double LaneRadiusM;
    };
    const FDefaultLane defaultLanes[] = {
        { TEXT("Mercury-Venus"), 1.20 * NPCTUNNEL_AU_M, 2.0e6 },
        { TEXT("Venus-Earth"), 1.75 * NPCTUNNEL_AU_M, 2.0e6 },
        { TEXT("Earth-Moon"), 4.06e8, 2.0e5 },
        { TEXT("Earth-Mars"), 2.69 * NPCTUNNEL_AU_M, 3.0e6 },
        { TEXT("Mars-Jupiter"), 7.13 * NPCTUNNEL_AU_M, 5.0e6 },
        { TEXT("Jupiter-Saturn"), 15.6 * NPCTUNNEL_AU_M, 8.0e6 },
        { TEXT("Saturn-Uranus"), 30.2 * NPCTUNNEL_AU_M, 1.0e7 },
        { TEXT("Uranus-Neptune"), 50.6 * NPCTUNNEL_AU_M, 1.0e7 },
    };
    for (const FDefaultLane& lane : defaultLanes)
    {
        const double rt = lane.LaneRadiusM * SOL::NPC_TUNNEL_RADIUS_MIN_SCALE;
        TestTrue(FString::Printf(TEXT("%s: whole length under %d ring slots"), lane.Name, perSide - 1),
            RingWarp(lane.MaxLengthM, 0.0, rt) < static_cast<double>(perSide - 1));
    }

    // The detail slots cover the dots and chevrons out to the detail fade's end
    TestTrue(TEXT("detail slots reach 512 Rt"), RingWarp(SOL::NPC_TUNNEL_DETAIL_FADE_END_RADII * 1.0e6, 0.0, 1.0e6)
        < static_cast<double>(SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE - 1));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathDetailCoverageTest, "SOLTest.NpcTunnelMath.DetailSlotCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 11, dots and chevrons: with the same warp and phase, two dots per ring gap (at + 0.25 and + 0.75 of the gap)
// and one chevron (mid-gap); against a brute-force reference every visible one out to 512 Rt is returned by exactly
// one gap slot (DotSlot with its dot index, ChevronSlot) with the same offset and alpha, none outside the end rings
bool FSOLNpcTunnelMathDetailCoverageTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    TestEqual(TEXT("two detail slots per gap index"), DetailSlotCount(), 2 * SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE);

    struct FLaneCase
    {
        const TCHAR* Name;
        double LengthM;
        double BodyRadiusAM;
        double BodyRadiusBM;
        double LaneRadiusM;
    };
    const FLaneCase lanes[] = {
        { TEXT("Earth-Moon"), NPCTUNNEL_EARTH_MOON_PERIGEE_M, NPCTUNNEL_EARTH_RADIUS_M, NPCTUNNEL_MOON_RADIUS_M,
            NPCTUNNEL_EARTH_MOON_LANE_RADIUS_M },
        { TEXT("Uranus-Neptune"), NPCTUNNEL_URANUS_NEPTUNE_M, NPCTUNNEL_URANUS_RADIUS_M, NPCTUNNEL_NEPTUNE_RADIUS_M,
            NPCTUNNEL_URANUS_NEPTUNE_LANE_RADIUS_M },
    };
    const double scales[] = { SOL::NPC_TUNNEL_RADIUS_MIN_SCALE, SOL::NPC_TUNNEL_RADIUS_MAX_SCALE };
    const double radialsRt[] = { 0.0, 1.0, 10.0, 100.0 };
    const double phases[] = { 0.0, 0.3, 0.999 };

    FNpcTunnelFailures dotFailures;
    FNpcTunnelFailures chevronFailures;
    TArray<FNpcTunnelRefElement> reference;
    TArray<FSOLNpcTunnelRing> returned;
    int32 visibleDots = 0;
    int32 visibleChevrons = 0;
    for (const FLaneCase& lane : lanes)
    {
        for (const double scale : scales)
        {
            const double rt = lane.LaneRadiusM * scale;
            const double endA = lane.BodyRadiusAM + SOL::NPC_TUNNEL_END_MARGIN_RADII * rt;
            const double endB = lane.LengthM - lane.BodyRadiusBM - SOL::NPC_TUNNEL_END_MARGIN_RADII * rt;
            const double footArcs[] = { endA, 0.5 * (endA + endB), endB, endA - 3.0 * rt };
            for (const double footArc : footArcs)
            {
                for (const double hRt : radialsRt)
                {
                    for (const double phase : phases)
                    {
                        const FSOLNpcTunnelSlotInput input = NpcTunnelInput(hRt * rt, rt, phase, endA - footArc,
                            endB - footArc);
                        const FString label = FString::Printf(TEXT("%s x%.0f, foot %.4e m, h %.1f Rt, phase %.3f"),
                            lane.Name, scale, footArc, hRt, phase);

                        // Dots
                        NpcTunnelBruteDetails(input, false, reference);
                        visibleDots += reference.Num();
                        returned.Reset();
                        for (int32 slot = 0; slot < DetailSlotCount(); ++slot)
                        {
                            for (int32 dot = 0; dot < SOL::NPC_TUNNEL_DOTS_PER_GAP; ++dot)
                            {
                                FSOLNpcTunnelRing element;
                                if (DotSlot(slot, dot, input, element))
                                {
                                    returned.Add(element);
                                }
                            }
                        }
                        NpcTunnelMatchCoverage(reference, returned, input, label + TEXT(" (dots)"), dotFailures);

                        // Chevrons
                        NpcTunnelBruteDetails(input, true, reference);
                        visibleChevrons += reference.Num();
                        returned.Reset();
                        for (int32 slot = 0; slot < DetailSlotCount(); ++slot)
                        {
                            FSOLNpcTunnelRing element;
                            if (ChevronSlot(slot, input, element))
                            {
                                returned.Add(element);
                            }
                        }
                        NpcTunnelMatchCoverage(reference, returned, input, label + TEXT(" (chevrons)"),
                            chevronFailures);
                    }
                }
            }
        }
    }
    TestTrue(TEXT("the reference had visible dots"), visibleDots > 500);
    TestTrue(TEXT("the reference had visible chevrons"), visibleChevrons > 250);
    if (!TestEqual(TEXT("every visible dot exactly once"), dotFailures.Count, 0))
    {
        AddInfo(TEXT("first: ") + dotFailures.First);
    }
    if (!TestEqual(TEXT("every visible chevron exactly once"), chevronFailures.Count, 0))
    {
        AddInfo(TEXT("first: ") + chevronFailures.First);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathEndArcsTest, "SOLTest.NpcTunnelMath.EndArcs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 12 (D12, Q10): the end rings sit one margin of tunnel radii outside each body's surface; when they cross the
// tunnel is not drawable and the arcs are (0, 0); a zero body radius leaves just the margin; non-finite input is not
// drawable
bool FSOLNpcTunnelMathEndArcsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double margin = SOL::NPC_TUNNEL_END_MARGIN_RADII;
    const double laneLengthM = 4.0e11;
    const double rt = 3.0e6;
    const FVector2D arcs = TunnelEndArcsM(laneLengthM, 6.371e6, 3.3895e6, rt);
    TestEqual(TEXT("end A: surface plus the margin"), arcs.X, 6.371e6 + margin * rt, NPCTUNNEL_TOL * laneLengthM);
    TestEqual(TEXT("end B: length minus surface and margin"), arcs.Y, laneLengthM - 3.3895e6 - margin * rt,
        NPCTUNNEL_TOL * laneLengthM);
    TestTrue(TEXT("drawable"), IsTunnelDrawable(laneLengthM, 6.371e6, 3.3895e6, rt));

    const FVector2D bare = TunnelEndArcsM(laneLengthM, 0.0, 0.0, rt);
    TestEqual(TEXT("zero radius A: just the margin"), bare.X, margin * rt, NPCTUNNEL_TOL * laneLengthM);
    TestEqual(TEXT("zero radius B: just the margin"), bare.Y, laneLengthM - margin * rt, NPCTUNNEL_TOL * laneLengthM);
    TestTrue(TEXT("zero radii: drawable"), IsTunnelDrawable(laneLengthM, 0.0, 0.0, rt));

    // Crossing end rings: a lane shorter than its bodies plus margins
    const FVector2D crossed = TunnelEndArcsM(1.0e7, 6.0e6, 3.0e6, 1.0e6);
    TestTrue(TEXT("crossed: (0, 0)"), crossed.X == 0.0 && crossed.Y == 0.0);
    TestFalse(TEXT("crossed: not drawable"), IsTunnelDrawable(1.0e7, 6.0e6, 3.0e6, 1.0e6));

    // Non-finite input
    TestFalse(TEXT("NaN length: not drawable"), IsTunnelDrawable(NPCTUNNEL_NAN, 6.0e6, 3.0e6, rt));
    TestFalse(TEXT("inf length: not drawable"), IsTunnelDrawable(NPCTUNNEL_INF, 6.0e6, 3.0e6, rt));
    TestFalse(TEXT("NaN body A: not drawable"), IsTunnelDrawable(laneLengthM, NPCTUNNEL_NAN, 3.0e6, rt));
    TestFalse(TEXT("inf body B: not drawable"), IsTunnelDrawable(laneLengthM, 6.0e6, NPCTUNNEL_INF, rt));
    TestFalse(TEXT("NaN tunnel radius: not drawable"), IsTunnelDrawable(laneLengthM, 6.0e6, 3.0e6, NPCTUNNEL_NAN));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathRingSampleTest, "SOLTest.NpcTunnelMath.RingSamplePoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13: ring samples lie on the circle in the plane normal to the axis, sample 0 at 12 o'clock (Up), a quarter of
// the way round at 3 o'clock (Right), clockwise as seen looking along the axis; no segments gives the centre
bool FSOLNpcTunnelMathRingSampleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const FVector3d aM(2.0e10, 1.0e10, -3.0e8);
    const FSOLNpcTunnelBasis basis = TunnelBasis(aM, aM + NpcTunnelDirection(6.0, 250.0) * 4.0e11);
    const FVector3d centre(1.0e9, -2.0e8, 3.0e7);
    const double radiusM = 4.0e6;
    for (const int32 segments : { 12, 48 })
    {
        for (int32 sample = 0; sample < segments; ++sample)
        {
            const FString label = FString::Printf(TEXT("%d segments, sample %d"), segments, sample);
            const FVector3d point = RingSamplePointM(basis, centre, radiusM, sample, segments);
            const FVector3d offset = point - centre;
            TestEqual(label + TEXT(": on the circle"), offset.Size(), radiusM, NPCTUNNEL_TOL * radiusM);
            TestTrue(label + TEXT(": in the plane normal to the axis"),
                FMath::Abs(FVector3d::DotProduct(offset, basis.Axis)) <= NPCTUNNEL_TOL * radiusM);
            const double angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(sample) / static_cast<double>(segments);
            const FVector3d expected = centre + (basis.Up * FMath::Cos(angle) + basis.Right * FMath::Sin(angle))
                * radiusM;
            TestTrue(label + TEXT(": clockwise from 12 o'clock"), NpcTunnelVecNear(point, expected,
                NPCTUNNEL_TOL * radiusM));

            // Clockwise as seen along the axis: the turn from this sample to the next points along +Axis
            const FVector3d next = RingSamplePointM(basis, centre, radiusM, (sample + 1) % segments, segments);
            TestTrue(label + TEXT(": turns clockwise"),
                FVector3d::DotProduct(FVector3d::CrossProduct(offset, next - centre), basis.Axis) > 0.0);
        }
        TestTrue(FString::Printf(TEXT("%d segments: sample 0 at Up"), segments), NpcTunnelVecNear(
            RingSamplePointM(basis, centre, radiusM, 0, segments), centre + basis.Up * radiusM,
            NPCTUNNEL_TOL * radiusM));
        TestTrue(FString::Printf(TEXT("%d segments: a quarter round at Right"), segments), NpcTunnelVecNear(
            RingSamplePointM(basis, centre, radiusM, segments / 4, segments), centre + basis.Right * radiusM,
            NPCTUNNEL_TOL * radiusM));
    }
    for (const int32 badSegments : { 0, -3 })
    {
        TestTrue(FString::Printf(TEXT("%d segments: the centre"), badSegments), NpcTunnelVecNear(
            RingSamplePointM(basis, centre, radiusM, 2, badSegments), centre, NPCTUNNEL_TOL));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathLabelLanesTest, "SOLTest.NpcTunnelMath.SelectLabelLanes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14 (Q4): one label per shown tunnel, strongest first (the highest fade; ties to the smallest d / R, then the
// lower index), at most LABEL_MAX_COUNT and the output's size; NaN entries ignored, mismatched lengths use the shorter
bool FSOLNpcTunnelMathLabelLanesTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::SelectLabelLanes;
    TArray<int32> out;

    // Returns whether the first count entries of out are the expected lanes
    const auto matches = [&out](const int32 count, std::initializer_list<int32> expected)
    {
        if (count != static_cast<int32>(expected.size()))
        {
            return false;
        }
        int32 index = 0;
        for (const int32 lane : expected)
        {
            if (out[index++] != lane)
            {
                return false;
            }
        }
        return true;
    };

    out.Init(INDEX_NONE, 8);
    {
        const double fades[] = { 0.0, 0.5, 1.0, 0.2 };
        const double distances[] = { 200.0, 70.0, 30.0, 90.0 };
        TestTrue(TEXT("strongest first, fade 0 left out"), matches(SelectLabelLanes(fades, distances, out),
            { 2, 1, 3 }));
    }
    {
        const double fades[] = { 1.0, 1.0, 1.0, 0.0 };
        const double distances[] = { 40.0, 10.0, 25.0, 300.0 };
        TestTrue(TEXT("ties at 1: the smallest d / R first"), matches(SelectLabelLanes(fades, distances, out),
            { 1, 2, 0 }));
    }
    {
        const double fades[] = { 0.7, 0.7 };
        const double distances[] = { 60.0, 60.0 };
        TestTrue(TEXT("full ties: the lower index first"), matches(SelectLabelLanes(fades, distances, out), { 0, 1 }));
    }
    {
        const double fades[] = { 0.0, 0.0, -0.1 };
        const double distances[] = { 120.0, 150.0, 30.0 };
        TestEqual(TEXT("no fade above 0: no labels"), SelectLabelLanes(fades, distances, out), 0);
    }
    {
        const double fades[] = { 0.3, 0.9, 0.5, 1.0, 0.8 };
        const double distances[] = { 80.0, 60.0, 70.0 };
        TestTrue(TEXT("mismatched lengths: the shorter"), matches(SelectLabelLanes(fades, distances, out),
            { 1, 2, 0 }));
    }
    {
        const double fades[] = { NPCTUNNEL_NAN, 0.4, 0.6 };
        const double distances[] = { 10.0, 80.0, 70.0 };
        TestTrue(TEXT("NaN fade ignored"), matches(SelectLabelLanes(fades, distances, out), { 2, 1 }));
    }
    {
        const double fades[] = { 0.4, 0.6, 0.5 };
        const double distances[] = { 80.0, NPCTUNNEL_NAN, 75.0 };
        TestTrue(TEXT("NaN distance ignored"), matches(SelectLabelLanes(fades, distances, out), { 2, 0 }));
    }
    const double fiveFades[] = { 0.2, 0.9, 0.5, 1.0, 0.7 };
    const double fiveDistances[] = { 95.0, 60.0, 80.0, 20.0, 70.0 };
    TestTrue(TEXT("a fifth tunnel: the weakest dropped"), matches(SelectLabelLanes(fiveFades, fiveDistances, out),
        { 3, 1, 4, 2 }));
    TArray<int32> two;
    two.Init(INDEX_NONE, 2);
    const int32 twoCount = SelectLabelLanes(fiveFades, fiveDistances, two);
    TestTrue(TEXT("output of 2: the two strongest"), twoCount == 2 && two[0] == 3 && two[1] == 1);
    TArray<int32> none;
    TestEqual(TEXT("empty output: 0"), SelectLabelLanes(fiveFades, fiveDistances, none), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathLabelAlphaTest, "SOLTest.NpcTunnelMath.LaneLabelAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14 (Q4): the strongest tunnel's label has alpha F, every other F x SIDE_ALPHA (fade clamped, NaN -> 0), so a
// side label is always dimmer than the strongest at the same or a higher fade; a label is placed from MIN_ALPHA
bool FSOLNpcTunnelMathLabelAlphaTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double side = SOL::NPC_TUNNEL_LABEL_SIDE_ALPHA;
    TestEqual(TEXT("strongest, full: 1"), LaneLabelAlpha(1.0, true), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("side, full: 0.55"), LaneLabelAlpha(1.0, false), side, NPCTUNNEL_TOL);
    TestEqual(TEXT("strongest, 0.4: 0.4"), LaneLabelAlpha(0.4, true), 0.4, NPCTUNNEL_TOL);
    TestEqual(TEXT("side, 0.4: 0.4 x side"), LaneLabelAlpha(0.4, false), 0.4 * side, NPCTUNNEL_TOL);
    TestEqual(TEXT("fade 1.5 clamped"), LaneLabelAlpha(1.5, true), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("fade -0.2 clamped"), LaneLabelAlpha(-0.2, false), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("NaN, strongest: 0"), LaneLabelAlpha(NPCTUNNEL_NAN, true), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("NaN, side: 0"), LaneLabelAlpha(NPCTUNNEL_NAN, false), 0.0, NPCTUNNEL_TOL);

    // A side label is strictly dimmer than the strongest one at the same or a higher fade
    for (int32 sideStep = 1; sideStep <= 20; ++sideStep)
    {
        const double sideFade = static_cast<double>(sideStep) / 20.0;
        for (int32 strongStep = sideStep; strongStep <= 20; ++strongStep)
        {
            const double strongFade = static_cast<double>(strongStep) / 20.0;
            TestTrue(FString::Printf(TEXT("side %.2f dimmer than strongest %.2f"), sideFade, strongFade),
                LaneLabelAlpha(sideFade, false) < LaneLabelAlpha(strongFade, true));
        }
    }

    // Placement threshold
    TestTrue(TEXT("placed at MIN_ALPHA"), IsLabelPlaced(SOL::NPC_TUNNEL_LABEL_MIN_ALPHA));
    TestTrue(TEXT("placed at 1"), IsLabelPlaced(1.0));
    TestFalse(TEXT("not placed just below MIN_ALPHA"), IsLabelPlaced(SOL::NPC_TUNNEL_LABEL_MIN_ALPHA - 1.0e-4));
    TestFalse(TEXT("not placed at 0"), IsLabelPlaced(0.0));
    TestFalse(TEXT("not placed at NaN"), IsLabelPlaced(NPCTUNNEL_NAN));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathEndDistancesTest, "SOLTest.NpcTunnelMath.LaneEndDistances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 15 (Q4): the label's distances are the straight lines from the ship to the end rings' centres (A + a_A axis,
// A + a_B axis); on the axis between them they are the arc differences
bool FSOLNpcTunnelMathEndDistancesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    FSOLNpcLaneEntry lane;
    lane.A = FVector3d(1.0e10, 2.0e9, -1.0e8);
    lane.B = lane.A + FVector3d(3.0e11, 1.0e11, 2.0e9);
    lane.RadiusM = 3.0e6;
    lane.ShipCount = 5000;
    const FSOLNpcTunnelBasis basis = TunnelBasis(lane.A, lane.B);
    const double laneLengthM = (lane.B - lane.A).Size();
    const FVector2D endArcs(1.0e7, laneLengthM - 5.0e6);
    const FVector3d ringA = lane.A + basis.Axis * endArcs.X;
    const FVector3d ringB = lane.A + basis.Axis * endArcs.Y;

    // On the axis between the end rings
    const FVector3d onAxis = lane.A + basis.Axis * 1.0e9;
    const FVector2D onAxisDistances = LaneEndDistancesM(onAxis, lane, basis, endArcs);
    TestEqual(TEXT("on the axis: to A"), onAxisDistances.X, 1.0e9 - endArcs.X, NPCTUNNEL_TOL * laneLengthM);
    TestEqual(TEXT("on the axis: to B"), onAxisDistances.Y, endArcs.Y - 1.0e9, NPCTUNNEL_TOL * laneLengthM);

    // Off the axis and before end A: straight lines to the ring centres
    const FVector3d ships[] = {
        lane.A + basis.Axis * 2.0e9 + basis.Up * 5.0e7,
        lane.A + basis.Axis * 1.5e11 - basis.Right * 4.0e8 + basis.Up * 1.0e6,
        lane.A - basis.Axis * 1.0e8,
    };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(ships)); ++index)
    {
        const FVector2D distances = LaneEndDistancesM(ships[index], lane, basis, endArcs);
        TestEqual(FString::Printf(TEXT("ship %d: to A"), index), distances.X, (ships[index] - ringA).Size(),
            NPCTUNNEL_TOL * laneLengthM);
        TestEqual(FString::Printf(TEXT("ship %d: to B"), index), distances.Y, (ships[index] - ringB).Size(),
            NPCTUNNEL_TOL * laneLengthM);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathTexelsTest, "SOLTest.NpcTunnelMath.WriteTunnelTexels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 16 (Q5): the tunnel texture has 4 rows of GROUP_TEXTURE_WIDTH texels; a tunnel's lane column holds (foot point
// relative to the viewpoint, Rt), (axis, fade), (up, phase), (end arc A, end arc B relative to the foot arc, 0, 0);
// every other column is zero (fade 0); an empty list writes all zeros; a buffer shorter than 4 rows is left alone
bool FSOLNpcTunnelMathTexelsTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::WriteTunnelTexels;
    const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    const FVector4f garbage(7.0f, -7.0f, 7.0f, -7.0f);

    // Returns true when a texel matches the expected double values within float precision
    const auto texelNear = [](const FVector4f& texel, const double x, const double y, const double z, const double w)
    {
        const double expected[] = { x, y, z, w };
        const float actual[] = { texel.X, texel.Y, texel.Z, texel.W };
        for (int32 channel = 0; channel < 4; ++channel)
        {
            if (FMath::Abs(static_cast<double>(actual[channel]) - expected[channel])
                > 1.0e-6 * FMath::Max(1.0, FMath::Abs(expected[channel])))
            {
                return false;
            }
        }
        return true;
    };

    // Two tunnels, lanes 2 and 5
    TArray<FSOLNpcTunnelLaneData> lanes;
    for (const int32 laneIndex : { 2, 5 })
    {
        const double k = static_cast<double>(laneIndex);
        FSOLNpcTunnelLaneData data;
        data.Lane = laneIndex;
        data.FootRelM = FVector3d(1.5e6 * k, -2.5e6, 3.0e5 * k);
        data.TunnelRadiusM = 1.0e6 * k;
        data.Axis = FVector3d(0.6, 0.8, 0.0);
        data.Fade = 0.1 * k;
        data.Up = FVector3d(0.0, 0.0, 1.0);
        data.Phase = 0.07 * k;
        data.EndArcAM = -4.0e7 * k;
        data.EndArcBM = 9.0e8 * k;
        lanes.Add(data);
    }
    TArray<FVector4f> texels;
    texels.Init(garbage, width * 4);
    WriteTunnelTexels(lanes, texels);
    for (int32 column = 0; column < width; ++column)
    {
        const FSOLNpcTunnelLaneData* data = nullptr;
        for (const FSOLNpcTunnelLaneData& lane : lanes)
        {
            data = lane.Lane == column ? &lane : data;
        }
        const FString label = FString::Printf(TEXT("column %d"), column);
        if (data == nullptr)
        {
            bool bZero = true;
            for (int32 row = 0; row < 4; ++row)
            {
                bZero &= texels[row * width + column] == FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
            }
            TestTrue(label + TEXT(": zero (fade 0)"), bZero);
            continue;
        }
        TestTrue(label + TEXT(": row 0 = foot point, Rt"), texelNear(texels[column], data->FootRelM.X,
            data->FootRelM.Y, data->FootRelM.Z, data->TunnelRadiusM));
        TestTrue(label + TEXT(": row 1 = axis, fade"), texelNear(texels[width + column], data->Axis.X, data->Axis.Y,
            data->Axis.Z, data->Fade));
        TestTrue(label + TEXT(": row 2 = up, phase"), texelNear(texels[2 * width + column], data->Up.X, data->Up.Y,
            data->Up.Z, data->Phase));
        TestTrue(label + TEXT(": row 3 = end arcs, 0, 0"), texelNear(texels[3 * width + column], data->EndArcAM,
            data->EndArcBM, 0.0, 0.0));
        TestEqual(label + TEXT(": the fade is row 1 .w"), static_cast<double>(texels[width + column].W), data->Fade,
            1.0e-6);
    }

    // The frame the last tunnel goes away: all zeros
    const TArray<FSOLNpcTunnelLaneData> noLanes;
    WriteTunnelTexels(noLanes, texels);
    bool bAllZero = true;
    for (const FVector4f& texel : texels)
    {
        bAllZero &= texel == FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
    }
    TestTrue(TEXT("empty list: all zeros"), bAllZero);

    // A buffer shorter than 4 rows is left alone
    TArray<FVector4f> shortTexels;
    shortTexels.Init(garbage, width * 4 - 1);
    WriteTunnelTexels(lanes, shortTexels);
    bool bUntouched = true;
    for (const FVector4f& texel : shortTexels)
    {
        bUntouched &= texel == garbage;
    }
    TestTrue(TEXT("short buffer: untouched"), bUntouched);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathCollectTest, "SOLTest.NpcTunnelMath.CollectTunnelLanes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 16: the instance list is the lanes with a fade above 0 in index order; the call reports a change only when that
// set changes (not when a fade value moves), and keeps the array's allocation when the count does not grow
bool FSOLNpcTunnelMathCollectTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::CollectTunnelLanes;
    TArray<int32> lanes;

    // Returns whether the lane list equals the expected one
    const auto equals = [&lanes](std::initializer_list<int32> expected)
    {
        if (lanes.Num() != static_cast<int32>(expected.size()))
        {
            return false;
        }
        int32 index = 0;
        for (const int32 lane : expected)
        {
            if (lanes[index++] != lane)
            {
                return false;
            }
        }
        return true;
    };

    const double none[] = { 0.0, 0.0, 0.0 };
    TestFalse(TEXT("nothing shown, empty list: no change"), CollectTunnelLanes(none, lanes));
    TestEqual(TEXT("nothing shown: empty"), lanes.Num(), 0);

    const double first[] = { 0.0, 0.3, 0.0, 1.0, NPCTUNNEL_NAN, 0.01 };
    TestTrue(TEXT("first set: changed"), CollectTunnelLanes(first, lanes));
    TestTrue(TEXT("first set: lanes 1, 3, 5 in order"), equals({ 1, 3, 5 }));

    TestFalse(TEXT("same fades: no change"), CollectTunnelLanes(first, lanes));
    TestTrue(TEXT("same fades: same lanes"), equals({ 1, 3, 5 }));

    const double moved[] = { 0.0, 0.9, 0.0, 0.5, NPCTUNNEL_NAN, 0.2 };
    TestFalse(TEXT("moved fades, same set: no change"), CollectTunnelLanes(moved, lanes));
    TestTrue(TEXT("moved fades: same lanes"), equals({ 1, 3, 5 }));

    // Shrinking keeps the allocation
    const int32 capacity = lanes.Max();
    const double shrunk[] = { 0.0, 0.9, 0.0, 0.0, 0.0, 0.2 };
    TestTrue(TEXT("lane 3 left: changed"), CollectTunnelLanes(shrunk, lanes));
    TestTrue(TEXT("lane 3 left: lanes 1, 5"), equals({ 1, 5 }));
    TestEqual(TEXT("lane 3 left: allocation kept"), lanes.Max(), capacity);

    const double swapped[] = { 0.4, 0.0, 0.0, 0.0, 0.0, 0.2 };
    TestTrue(TEXT("same count, other lane: changed"), CollectTunnelLanes(swapped, lanes));
    TestTrue(TEXT("same count, other lane: lanes 0, 5"), equals({ 0, 5 }));
    TestEqual(TEXT("same count: allocation kept"), lanes.Max(), capacity);

    const double zero[] = { 0.0, 0.0, 0.0, 0.0, -0.5, 0.0 };
    TestTrue(TEXT("all gone: changed"), CollectTunnelLanes(zero, lanes));
    TestEqual(TEXT("all gone: empty"), lanes.Num(), 0);
    TestEqual(TEXT("all gone: allocation kept"), lanes.Max(), capacity);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathShownTest, "SOLTest.NpcTunnelMath.TunnelsShown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 16 (D3, Q6): the tunnels show only with the lanes shown, sol.NpcTunnel on and the jump map closed
bool FSOLNpcTunnelMathShownTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::AreTunnelsShown;
    for (const bool bLanesShown : { false, true })
    {
        for (const bool bCvarOn : { false, true })
        {
            for (const bool bMapOpen : { false, true })
            {
                TestEqual(FString::Printf(TEXT("lanes %d, cvar %d, map %d"), bLanesShown ? 1 : 0, bCvarOn ? 1 : 0,
                    bMapOpen ? 1 : 0), AreTunnelsShown(bLanesShown, bCvarOn, bMapOpen),
                    bLanesShown && bCvarOn && !bMapOpen);
            }
        }
    }
    return true;
}

// Part 49b (test first, plan steps 7b and 8): the renderer's constants, the unclamped foot point, the end rings' alpha,
// the label anchors with their ahead sign's hysteresis, the projected dot rows' extent, the per-lane renderer feed, the
// phase step, the visibility and upload rules, the slot mesh's size and the fade texel the lane and point materials
// read
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a uniformly random unit vector from a stream
    FVector3d NpcTunnelRandomUnit(FRandomStream& stream)
    {
        const double z = stream.FRandRange(-1.0, 1.0);
        const double azimuth = stream.FRandRange(0.0, 2.0 * UE_DOUBLE_PI);
        const double s = FMath::Sqrt(FMath::Max(0.0, 1.0 - z * z));
        return FVector3d(s * FMath::Cos(azimuth), s * FMath::Sin(azimuth), z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Earth-Mars-like test lane of the 49b cases: resolved, drawn, 62,400 ships, radius 3e6 m
    FSOLNpcLaneEntry NpcTunnelTestLane()
    {
        FSOLNpcLaneEntry lane;
        lane.A = FVector3d(1.0e11, 2.0e10, 1.0e8);
        lane.B = lane.A + FVector3d(8.0e10, 3.0e10, 1.0e9);
        lane.RadiusM = 3.0e6;
        lane.ShipCount = 62400;
        lane.TrafficWeight = SOLNpcLane::LaneTrafficWeight(lane.ShipCount);
        lane.bResolved = true;
        lane.bDrawn = true;
        return lane;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathRendererConstantsTest, "SOLTest.NpcTunnelMath.RendererConstants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 sections 4.6, 4.7 and 4.9 (proposed values, so a tuning change is a visible test edit): ring and end-ring
// widths, dot size, the chevrons' size, place and alpha, the emissive scale, the near ring segments, the sort priority
// above the lanes, the label's dot margin and its ahead sign's dead band
bool FSOLNpcTunnelMathRendererConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("ring width 1.5 px"), static_cast<double>(SOL::NPC_TUNNEL_RING_WIDTH_PX), 1.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("end ring width 3 px"), static_cast<double>(SOL::NPC_TUNNEL_END_RING_WIDTH_PX), 3.0,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("dot size 3 px"), static_cast<double>(SOL::NPC_TUNNEL_DOT_SIZE_PX), 3.0, NPCTUNNEL_TOL);
    TestTrue(TEXT("end rings wider than rings"), SOL::NPC_TUNNEL_END_RING_WIDTH_PX > SOL::NPC_TUNNEL_RING_WIDTH_PX);

    // Chevrons: on the wall inside the dot rows, the mock-up's proportions (alpha below)
    TestEqual(TEXT("chevron radius 0.97 Rt"), static_cast<double>(SOL::NPC_TUNNEL_CHEVRON_RADIUS_SCALE), 0.97,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("chevron half width 0.17 Rt"), static_cast<double>(SOL::NPC_TUNNEL_CHEVRON_HALF_WIDTH_RADII), 0.17,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("chevron half length 0.26 Rt"), static_cast<double>(SOL::NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII),
        0.26, NPCTUNNEL_TOL);
    // 49c (the user's report: match the mock-up): a filled dart notched at 0.3 half lengths, linear alpha 0.3 (reads
    // like the mock-up's sRGB 0.75 x fog over black); near dots world-sized like the mock-up's, 3 px floor
    TestEqual(TEXT("chevron notch 0.3"), static_cast<double>(SOL::NPC_TUNNEL_CHEVRON_NOTCH_FRACTION), 0.3,
        NPCTUNNEL_TOL);
    TestEqual(TEXT("chevron alpha 0.3"), static_cast<double>(SOL::NPC_TUNNEL_CHEVRON_ALPHA), 0.3, NPCTUNNEL_TOL);
    TestEqual(TEXT("dot world diameter 0.034 Rt"), static_cast<double>(SOL::NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII),
        0.034, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail near fade from 0.5 Rt"), static_cast<double>(SOL::NPC_TUNNEL_DETAIL_NEAR_FADE_START_RADII),
        0.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("detail near fade full at 1 Rt"), static_cast<double>(SOL::NPC_TUNNEL_DETAIL_NEAR_FADE_FULL_RADII),
        1.0, NPCTUNNEL_TOL);
    // A chevron beside a camera on the axis is 0.97 Rt away, just inside the near fade's full radius: it keeps about
    // 0.99 (smoothstep at 0.94), so the wall chevrons never visibly dim from the axis
    const double besideFade = SOLNpcTunnel::TunnelDetailNearFade(SOL::NPC_TUNNEL_CHEVRON_RADIUS_SCALE, 1.0);
    TestTrue(FString::Printf(TEXT("chevrons beside a camera on the axis keep at least 0.985 (%.4f)"), besideFade),
        besideFade >= 0.985);
    TestTrue(TEXT("chevrons inside the dot rows (dots outside the rings, D6)"),
        SOL::NPC_TUNNEL_CHEVRON_RADIUS_SCALE < 1.0 && 1.0 < SOL::NPC_TUNNEL_DOT_RADIUS_SCALE);

    // Emissive (tuned in 49c), segments, sort priority
    const double emissive = SOL::NPC_TUNNEL_EMISSIVE;
    TestTrue(TEXT("emissive positive and finite"), FMath::IsFinite(emissive) && emissive > 0.0);
    TestEqual(TEXT("near ring segments 48"), SOL::NPC_TUNNEL_RING_SEGMENTS_NEAR, 48);
    TestEqual(TEXT("nearest ring segments 96"), SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST, 96);
    TestEqual(TEXT("sort priority 2"), SOL::NPC_TUNNEL_TRANSLUCENCY_SORT_PRIORITY, 2);
    TestTrue(TEXT("sorted above the lanes in the after-motion-blur pass"),
        SOL::NPC_TUNNEL_TRANSLUCENCY_SORT_PRIORITY > SOL::NPC_LANE_TRANSLUCENCY_SORT_PRIORITY);

    // Labels
    TestEqual(TEXT("label dot margin 6 px"), static_cast<double>(SOL::NPC_TUNNEL_LABEL_DOT_MARGIN_PX), 6.0,
        NPCTUNNEL_TOL);
    const double band = SOL::NPC_TUNNEL_LABEL_FLIP_HYSTERESIS;
    TestTrue(TEXT("ahead-sign dead band in (0, 1)"), band > 0.0 && band < 1.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathLaneFootTest, "SOLTest.NpcTunnelMath.LaneFoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Step 7b (SDD 49 sections 4.3 and 4.4): LaneFoot(aRel, bRel) is the viewpoint's (origin's) foot on the lane's axis
// LINE, not clamped to the segment: its arc from node A (below 0 before A, above L past B), the foot point relative to
// the viewpoint (perpendicular to the axis, its length h) and h; inside the segment it is the nearest point; moving the
// viewpoint along the axis moves the arc by the same amount and keeps h; A == B or a non-finite input is invalid
bool FSOLNpcTunnelMathLaneFootTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;

    // Beside a lane along +X: the foot is the nearest point
    {
        const FVector3d aRel(-4.0e6, 2.0e5, -1.0e5);
        const FVector3d bRel(6.0e6, 2.0e5, -1.0e5);
        const FSOLNpcTunnelFoot foot = LaneFoot(aRel, bRel);
        TestTrue(TEXT("beside: valid"), foot.bValid);
        TestEqual(TEXT("beside: arc 4e6"), foot.ArcM, 4.0e6, NPCTUNNEL_TOL * 1.0e7);
        TestEqual(TEXT("beside: arc = nearest param x L"), foot.ArcM, LaneNearestParam(aRel, bRel) * 1.0e7,
            NPCTUNNEL_TOL * 1.0e7);
        TestTrue(TEXT("beside: foot point (0, 2e5, -1e5)"),
            NpcTunnelVecNear(foot.PointRelM, FVector3d(0.0, 2.0e5, -1.0e5), NPCTUNNEL_TOL * 1.0e7));
        TestEqual(TEXT("beside: h"), foot.RadialM, FMath::Sqrt(5.0e10), NPCTUNNEL_TOL * 1.0e7);
    }

    // Before A and past B: the arc leaves [0, L] (the nearest param is clamped, the foot is not)
    {
        const FVector3d aRel(3.0e6, 1.0e5, 0.0);
        const FVector3d bRel(1.3e7, 1.0e5, 0.0);
        const FSOLNpcTunnelFoot foot = LaneFoot(aRel, bRel);
        TestTrue(TEXT("before A: valid"), foot.bValid);
        TestEqual(TEXT("before A: arc -3e6"), foot.ArcM, -3.0e6, NPCTUNNEL_TOL * 1.0e7);
        TestEqual(TEXT("before A: nearest param clamped to 0"), LaneNearestParam(aRel, bRel), 0.0, NPCTUNNEL_TOL);
        TestTrue(TEXT("before A: foot point on the line"),
            NpcTunnelVecNear(foot.PointRelM, FVector3d(0.0, 1.0e5, 0.0), NPCTUNNEL_TOL * 1.0e7));
        TestEqual(TEXT("before A: h"), foot.RadialM, 1.0e5, NPCTUNNEL_TOL * 1.0e7);
    }
    {
        const FVector3d aRel(-1.3e7, 0.0, 4.0e5);
        const FVector3d bRel(-3.0e6, 0.0, 4.0e5);
        const FSOLNpcTunnelFoot foot = LaneFoot(aRel, bRel);
        TestEqual(TEXT("past B: arc 1.3e7 (above L = 1e7)"), foot.ArcM, 1.3e7, NPCTUNNEL_TOL * 1.0e7);
        TestEqual(TEXT("past B: h"), foot.RadialM, 4.0e5, NPCTUNNEL_TOL * 1.0e7);
    }

    // Random lanes at AU scale: the foot point is A + arc x axis, perpendicular to the axis, of length h; moving the
    // viewpoint along the axis shifts the arc by the move and keeps h
    FRandomStream stream(49);
    for (int32 index = 0; index < 32; ++index)
    {
        const FVector3d aRel = NpcTunnelRandomUnit(stream) * stream.FRandRange(1.0e6, 3.0 * NPCTUNNEL_AU_M);
        const FVector3d bRel = aRel + NpcTunnelRandomUnit(stream) * stream.FRandRange(4.0e8, 30.0 * NPCTUNNEL_AU_M);
        const double scale = FMath::Max(aRel.Size(), bRel.Size());
        const FVector3d axis = (bRel - aRel).GetSafeNormal();
        const FSOLNpcTunnelFoot foot = LaneFoot(aRel, bRel);
        const FString label = FString::Printf(TEXT("random %d"), index);
        if (!TestTrue(label + TEXT(": valid"), foot.bValid))
        {
            continue;
        }
        TestTrue(label + TEXT(": foot = A + arc axis"), NpcTunnelVecNear(foot.PointRelM, aRel + axis * foot.ArcM,
            NPCTUNNEL_TOL * scale));
        TestTrue(label + TEXT(": perpendicular to the axis"),
            FMath::Abs(FVector3d::DotProduct(foot.PointRelM, axis)) <= NPCTUNNEL_TOL * scale);
        TestEqual(label + TEXT(": h = |foot|"), foot.RadialM, foot.PointRelM.Size(), NPCTUNNEL_TOL * scale);

        const double moveM = stream.FRandRange(-1.0e9, 1.0e9);
        const FSOLNpcTunnelFoot moved = LaneFoot(aRel - axis * moveM, bRel - axis * moveM);
        TestEqual(label + TEXT(": a move along the axis shifts the arc"), moved.ArcM - foot.ArcM, moveM,
            NPCTUNNEL_TOL * scale);
        TestEqual(label + TEXT(": a move along the axis keeps h"), moved.RadialM, foot.RadialM,
            NPCTUNNEL_TOL * scale);
    }

    // Invalid inputs
    const FVector3d invalidEnds[][2] = {
        { FVector3d(1.0e6, 0.0, 0.0), FVector3d(1.0e6, 0.0, 0.0) },
        { FVector3d(NPCTUNNEL_NAN, 0.0, 0.0), FVector3d(1.0e6, 0.0, 0.0) },
        { FVector3d(0.0, 0.0, 0.0), FVector3d(NPCTUNNEL_INF, 0.0, 0.0) },
    };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(invalidEnds)); ++index)
    {
        const FSOLNpcTunnelFoot foot = LaneFoot(invalidEnds[index][0], invalidEnds[index][1]);
        const FString label = FString::Printf(TEXT("invalid %d"), index);
        TestFalse(label + TEXT(": not valid"), foot.bValid);
        TestEqual(label + TEXT(": arc 0"), foot.ArcM, 0.0);
        TestEqual(label + TEXT(": h 0"), foot.RadialM, 0.0);
        TestTrue(label + TEXT(": foot point zero"), foot.PointRelM == FVector3d::ZeroVector);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathEndRingTest, "SOLTest.NpcTunnelMath.EndRing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Step 7b (SDD 49 sections 4.5 and 4.6): EndRingAlpha(x, h, Rt) = max(TunnelDepthFade(sqrt(x^2 + h^2), Rt),
// NPC_TUNNEL_END_RING_MIN_ALPHA) x TunnelNearFadeAt(x, h, Rt): the depth floor is lifted to 0.5 so an end ring stays
// readable to the far node, while the near fade still removes the end ring the camera passes through on the axis; no
// cap, end or detail fade applies and the tunnel fade is left to the caller (as for the ring slots). EndRingSlot(end)
// returns end ring A (0) or B (1) at its arc with its warp coordinate and that alpha, beyond the ring cap too
bool FSOLNpcTunnelMathEndRingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double rt = 1.0e6;
    const double floorAlpha = SOL::NPC_TUNNEL_END_RING_MIN_ALPHA;

    // Seen from outside the tube (no near fade): the depth fade with its floor raised to 0.5
    for (const double inRadii : { 9.0, 100.0, 1.0e3, 1.0e4, 65536.0, 1.0e6, 1.0e9 })
    {
        const double h = 4.0 * rt;
        const double d = inRadii * rt;
        const double x = FMath::Sqrt(d * d - h * h);
        const double expected = FMath::Max(TunnelDepthFade(d, rt), floorAlpha);
        TestEqual(FString::Printf(TEXT("%g Rt: max(depth, 0.5)"), inRadii), EndRingAlpha(x, h, rt), expected,
            NPCTUNNEL_TOL);
        TestTrue(FString::Printf(TEXT("%g Rt: never below 0.5"), inRadii), EndRingAlpha(-x, h, rt) >= floorAlpha);
    }
    TestEqual(TEXT("near, outside the tube: 1"), EndRingAlpha(5.0 * rt, 4.0 * rt, rt), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("far: the floor, not the depth floor 0.2"), EndRingAlpha(1.0e8 * rt, 0.0, rt), floorAlpha,
        NPCTUNNEL_TOL);

    // On the axis the near fade still applies: the end ring the camera flies through does not sweep the screen
    TestEqual(TEXT("at the camera's foot on the axis: 0"), EndRingAlpha(0.0, 0.0, rt), 0.0, NPCTUNNEL_TOL);
    for (const double x : { 0.3, 0.75, 1.0, 1.4, 2.0 })
    {
        for (const double h : { 0.0, 1.0, 2.0, 2.5, 3.0 })
        {
            const double d = FMath::Sqrt(x * x + h * h) * rt;
            const double expected = FMath::Max(TunnelDepthFade(d, rt), floorAlpha)
                * TunnelNearFadeAt(x * rt, h * rt, rt);
            TestEqual(FString::Printf(TEXT("x %g Rt, h %g Rt: floored depth x near"), x, h), EndRingAlpha(x * rt,
                h * rt, rt), expected, NPCTUNNEL_TOL);
            TestEqual(FString::Printf(TEXT("x %g Rt, h %g Rt: symmetric in x"), x, h), EndRingAlpha(-x * rt, h * rt,
                rt), EndRingAlpha(x * rt, h * rt, rt), NPCTUNNEL_TOL);
        }
    }

    // Bad inputs
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -rt, NPCTUNNEL_INF })
    {
        TestEqual(FString::Printf(TEXT("radius %g: 0"), badRadius), EndRingAlpha(10.0 * rt, 0.0, badRadius), 0.0);
    }
    TestEqual(TEXT("NaN offset: 0"), EndRingAlpha(NPCTUNNEL_NAN, 0.0, rt), 0.0);
    TestEqual(TEXT("NaN radial: 0"), EndRingAlpha(10.0 * rt, NPCTUNNEL_NAN, rt), 0.0);

    // The two end-ring slots
    const double h = 0.5 * rt;
    const FSOLNpcTunnelSlotInput input = NpcTunnelInput(h, rt, 0.3, -20.0 * rt, 500.0 * rt);
    const double ends[] = { input.EndArcAM, input.EndArcBM };
    for (int32 end = 0; end < 2; ++end)
    {
        FSOLNpcTunnelRing ring;
        const FString label = FString::Printf(TEXT("end %d"), end);
        if (!TestTrue(label + TEXT(": drawn"), EndRingSlot(end, input, ring)))
        {
            continue;
        }
        TestEqual(label + TEXT(": at its arc"), ring.AxialOffsetM, ends[end], NPCTUNNEL_TOL * rt);
        TestEqual(label + TEXT(": warp coordinate"), ring.W, RingWarp(ends[end], h, rt), NPCTUNNEL_TOL);
        TestEqual(label + TEXT(": alpha"), ring.Alpha, EndRingAlpha(ends[end], h, rt), NPCTUNNEL_TOL);
    }

    // Beyond the ring cap (|w| > 64) the end ring is still drawn, at least at the floor
    {
        const FSOLNpcTunnelSlotInput farInput = NpcTunnelInput(0.0, rt, 0.0, -5.0 * rt, 1.0e9 * rt);
        FSOLNpcTunnelRing ring;
        TestTrue(TEXT("far end ring: drawn"), EndRingSlot(1, farInput, ring));
        TestTrue(TEXT("far end ring: past the ring cap"), ring.W > SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE);
        TestTrue(TEXT("far end ring: at least the floor"), ring.Alpha >= floorAlpha);
    }

    // The camera on the axis at end ring A: that ring is hidden by the near fade, end ring B is drawn
    {
        const FSOLNpcTunnelSlotInput atEnd = NpcTunnelInput(0.0, rt, 0.0, 0.0, 300.0 * rt);
        FSOLNpcTunnelRing ring;
        TestFalse(TEXT("camera at end ring A: A hidden"), EndRingSlot(0, atEnd, ring));
        TestTrue(TEXT("camera at end ring A: B drawn"), EndRingSlot(1, atEnd, ring));
    }

    // Crossed or equal end arcs, a bad index or a bad radius: nothing
    FSOLNpcTunnelRing ring;
    const FSOLNpcTunnelSlotInput crossed = NpcTunnelInput(h, rt, 0.3, 50.0 * rt, 10.0 * rt);
    const FSOLNpcTunnelSlotInput equal = NpcTunnelInput(h, rt, 0.3, 10.0 * rt, 10.0 * rt);
    for (int32 end = 0; end < 2; ++end)
    {
        TestFalse(FString::Printf(TEXT("crossed: end %d"), end), EndRingSlot(end, crossed, ring));
        TestFalse(FString::Printf(TEXT("equal: end %d"), end), EndRingSlot(end, equal, ring));
    }
    TestFalse(TEXT("end -1"), EndRingSlot(-1, input, ring));
    TestFalse(TEXT("end 2"), EndRingSlot(2, input, ring));
    TestFalse(TEXT("bad radius"), EndRingSlot(0, NpcTunnelInput(h, NPCTUNNEL_NAN, 0.3, -20.0 * rt, 500.0 * rt), ring));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathLabelAheadTest, "SOLTest.NpcTunnelMath.LabelAheadSign",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Step 7b (SDD 49 section 4.9): a label sits ahead of the foot point in the direction the camera looks along the axis,
// LabelAheadSign(dot(camera forward, axis), previous sign) = +1 or -1 with a dead band of
// NPC_TUNNEL_LABEL_FLIP_HYSTERESIS: with no previous sign (0, the lane just came in range) the sign of the dot (+1 at 0
// and for NaN); a held +1 flips only once the dot is below -band and a held -1 only once it is above +band; a NaN dot
// keeps the held sign; any other previous value reads by its sign
bool FSOLNpcTunnelMathLabelAheadTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcTunnel::LabelAheadSign;
    const double band = SOL::NPC_TUNNEL_LABEL_FLIP_HYSTERESIS;
    const double eps = 1.0e-6;

    // No previous sign
    TestEqual(TEXT("new: looking along +axis"), LabelAheadSign(0.5, 0), 1);
    TestEqual(TEXT("new: looking along -axis"), LabelAheadSign(-0.5, 0), -1);
    TestEqual(TEXT("new: inside the band, negative"), LabelAheadSign(-0.5 * band, 0), -1);
    TestEqual(TEXT("new: exactly sideways"), LabelAheadSign(0.0, 0), 1);
    TestEqual(TEXT("new: NaN"), LabelAheadSign(NPCTUNNEL_NAN, 0), 1);

    // Holding +1 or -1
    TestEqual(TEXT("+1: inside the band stays"), LabelAheadSign(-band + eps, 1), 1);
    TestEqual(TEXT("+1: past -band flips"), LabelAheadSign(-band - eps, 1), -1);
    TestEqual(TEXT("+1: fully backwards flips"), LabelAheadSign(-1.0, 1), -1);
    TestEqual(TEXT("-1: inside the band stays"), LabelAheadSign(band - eps, -1), -1);
    TestEqual(TEXT("-1: past +band flips"), LabelAheadSign(band + eps, -1), 1);
    TestEqual(TEXT("+1: NaN keeps"), LabelAheadSign(NPCTUNNEL_NAN, 1), 1);
    TestEqual(TEXT("-1: NaN keeps"), LabelAheadSign(NPCTUNNEL_NAN, -1), -1);
    TestEqual(TEXT("previous 5 reads as +1"), LabelAheadSign(-0.5 * band, 5), 1);
    TestEqual(TEXT("previous -3 reads as -1"), LabelAheadSign(0.5 * band, -3), -1);

    // A full turn of the camera flips exactly twice, each time only past the band
    int32 sign = 0;
    int32 flips = 0;
    bool bFlipsOutsideBand = true;
    bool bAlwaysUnit = true;
    for (int32 step = 0; step <= 3600; ++step)
    {
        const double dot = FMath::Cos(2.0 * UE_DOUBLE_PI * static_cast<double>(step) / 3600.0);
        const int32 next = LabelAheadSign(dot, sign);
        bAlwaysUnit &= next == 1 || next == -1;
        if (sign != 0 && next != sign)
        {
            ++flips;
            bFlipsOutsideBand &= FMath::Abs(dot) > band;
        }
        sign = next;
    }
    TestEqual(TEXT("a full turn: two flips"), flips, 2);
    TestTrue(TEXT("a full turn: each flip past the band"), bFlipsOutsideBand);
    TestTrue(TEXT("always +1 or -1"), bAlwaysUnit);

    // Jitter round sideways inside the band never flips
    sign = 1;
    flips = 0;
    for (int32 step = 0; step < 1000; ++step)
    {
        const double dot = 0.9 * band * FMath::Sin(0.37 * static_cast<double>(step));
        const int32 next = LabelAheadSign(dot, sign);
        flips += next != sign ? 1 : 0;
        sign = next;
    }
    TestEqual(TEXT("jitter inside the band: no flip"), flips, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathLabelAnchorTest, "SOLTest.NpcTunnelMath.LabelAnchor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Step 7b (SDD 49 section 4.9): LaneLabelAnchorRelM(foot, basis, Rt, aheadSign, clockDeg) = foot + sign x
// NPC_TUNNEL_LABEL_AHEAD_RADII x Rt x Axis + Rt x WallDirection(basis, clock), on the tube wall (radius Rt) at the
// clock angle (0 = 12 o'clock, 180 = 6 o'clock), viewpoint-relative like the foot point; sign 0 reads as +1, any other
// value by its sign; an invalid basis or a bad tunnel radius returns the foot point
bool FSOLNpcTunnelMathLabelAnchorTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double rt = 3.0e6;
    const double ahead = SOL::NPC_TUNNEL_LABEL_AHEAD_RADII * rt;
    const FSOLNpcTunnelBasis basis = TunnelBasis(FVector3d::ZeroVector, FVector3d(1.0e9, 0.0, 0.0));
    const FVector3d foot(0.0, 2.0e5, -1.0e5);
    const double tol = NPCTUNNEL_TOL * rt;

    TestTrue(TEXT("12 o'clock ahead"), NpcTunnelVecNear(LaneLabelAnchorRelM(foot, basis, rt, 1, 0.0),
        foot + FVector3d(ahead, 0.0, rt), tol));
    TestTrue(TEXT("6 o'clock ahead"), NpcTunnelVecNear(LaneLabelAnchorRelM(foot, basis, rt, 1, 180.0),
        foot + FVector3d(ahead, 0.0, -rt), tol));
    TestTrue(TEXT("12 o'clock behind"), NpcTunnelVecNear(LaneLabelAnchorRelM(foot, basis, rt, -1, 0.0),
        foot + FVector3d(-ahead, 0.0, rt), tol));
    TestTrue(TEXT("sign 0 reads as +1"), NpcTunnelVecNear(LaneLabelAnchorRelM(foot, basis, rt, 0, 0.0),
        LaneLabelAnchorRelM(foot, basis, rt, 1, 0.0), tol));
    TestTrue(TEXT("sign 7 reads as +1"), NpcTunnelVecNear(LaneLabelAnchorRelM(foot, basis, rt, 7, 0.0),
        LaneLabelAnchorRelM(foot, basis, rt, 1, 0.0), tol));
    TestTrue(TEXT("sign -7 reads as -1"), NpcTunnelVecNear(LaneLabelAnchorRelM(foot, basis, rt, -7, 180.0),
        LaneLabelAnchorRelM(foot, basis, rt, -1, 180.0), tol));

    // An inclined lane: the anchor is the given axial distance along the axis and on the wall at its clock angle
    FRandomStream stream(4949);
    for (int32 index = 0; index < 16; ++index)
    {
        const FVector3d a = NpcTunnelRandomUnit(stream) * 1.0e11;
        const FVector3d direction = NpcTunnelRandomUnit(stream);
        const FVector3d b = a + FVector3d(direction.X, direction.Y, 0.1) * 1.0e11;
        const FSOLNpcTunnelBasis tilted = TunnelBasis(a, b);
        const FVector3d tiltedFoot = NpcTunnelRandomUnit(stream) * 5.0e7;
        const int32 sign = (index % 2) == 0 ? 1 : -1;
        for (const double clock : { 0.0, 180.0, 90.0 })
        {
            const FVector3d offset = LaneLabelAnchorRelM(tiltedFoot, tilted, rt, sign, clock) - tiltedFoot;
            const FString label = FString::Printf(TEXT("lane %d, clock %g"), index, clock);
            TestEqual(label + TEXT(": axial"), FVector3d::DotProduct(offset, tilted.Axis), sign * ahead, tol);
            TestTrue(label + TEXT(": on the wall at the clock angle"), NpcTunnelVecNear(offset - tilted.Axis
                * FVector3d::DotProduct(offset, tilted.Axis), WallDirection(tilted, clock) * rt, tol));
        }
    }

    // Bad inputs return the foot point
    const FSOLNpcTunnelBasis invalid;
    TestTrue(TEXT("invalid basis: the foot"), NpcTunnelVecNear(LaneLabelAnchorRelM(foot, invalid, rt, 1, 0.0), foot,
        NPCTUNNEL_TOL));
    for (const double badRadius : { NPCTUNNEL_NAN, 0.0, -rt, NPCTUNNEL_INF })
    {
        TestTrue(FString::Printf(TEXT("radius %g: the foot"), badRadius),
            NpcTunnelVecNear(LaneLabelAnchorRelM(foot, basis, badRadius, 1, 0.0), foot, NPCTUNNEL_TOL));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathDotRowSegmentTest, "SOLTest.NpcTunnelMath.DotRowSegment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 section 4.9 (49b, what the HUD projects to keep labels off the dots): TunnelDotRowSegmentRelM gives a dot
// row's straight segment along the tube, viewpoint-relative: foot + x Axis + NPC_TUNNEL_DOT_RADIUS_SCALE x Rt x
// WallDirection(basis, DotRowAngleDeg(row)) for x from max(end arc A, -X) to min(end arc B, X), X = sqrt(max(0,
// (NPC_TUNNEL_DETAIL_FADE_END_RADII Rt)^2 - h^2)) (where the dots end), h = |foot|; false when that range is empty,
// the row is out of range, the basis invalid or the tunnel radius bad
bool FSOLNpcTunnelMathDotRowSegmentTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double rt = 1.0e6;
    const double detailEnd = SOL::NPC_TUNNEL_DETAIL_FADE_END_RADII * rt;
    const double dotRadius = SOL::NPC_TUNNEL_DOT_RADIUS_SCALE * rt;
    const FSOLNpcTunnelBasis basis = TunnelBasis(FVector3d::ZeroVector, FVector3d(1.0e12, 0.0, 0.0));
    const double tol = NPCTUNNEL_TOL * detailEnd;

    // Returns the expected row point at axial offset x from a foot point
    const auto rowPoint = [&](const FVector3d& foot, const int32 row, const double x)
    {
        return foot + basis.Axis * x + WallDirection(basis, DotRowAngleDeg(row)) * dotRadius;
    };

    // On the axis: from end ring A to where the dots end
    for (int32 row = 0; row < SOL::NPC_TUNNEL_DOT_ROWS; ++row)
    {
        FVector3d start;
        FVector3d end;
        const FString label = FString::Printf(TEXT("on the axis, row %d"), row);
        if (!TestTrue(label + TEXT(": a segment"), TunnelDotRowSegmentRelM(FVector3d::ZeroVector, basis, rt,
            -100.0 * rt, 1.0e4 * rt, row, start, end)))
        {
            continue;
        }
        TestTrue(label + TEXT(": starts at end ring A"), NpcTunnelVecNear(start, rowPoint(FVector3d::ZeroVector, row,
            -100.0 * rt), tol));
        TestTrue(label + TEXT(": ends where the dots end"), NpcTunnelVecNear(end, rowPoint(FVector3d::ZeroVector, row,
            detailEnd), tol));
    }

    // 300 Rt off the axis: the far end at the true distance where the dots end
    {
        const FVector3d foot(0.0, 0.0, -300.0 * rt);
        const double reach = FMath::Sqrt(detailEnd * detailEnd - 300.0 * rt * 300.0 * rt);
        FVector3d start;
        FVector3d end;
        TestTrue(TEXT("off the axis: a segment"), TunnelDotRowSegmentRelM(foot, basis, rt, -1.0e4 * rt, 1.0e4 * rt, 2,
            start, end));
        TestTrue(TEXT("off the axis: from -X"), NpcTunnelVecNear(start, rowPoint(foot, 2, -reach), tol));
        TestTrue(TEXT("off the axis: to +X"), NpcTunnelVecNear(end, rowPoint(foot, 2, reach), tol));
    }

    // Both end rings inside the reach clip both ends
    {
        FVector3d start;
        FVector3d end;
        TestTrue(TEXT("short tunnel: a segment"), TunnelDotRowSegmentRelM(FVector3d::ZeroVector, basis, rt, -10.0 * rt,
            20.0 * rt, 5, start, end));
        TestTrue(TEXT("short tunnel: from end ring A"), NpcTunnelVecNear(start, rowPoint(FVector3d::ZeroVector, 5,
            -10.0 * rt), tol));
        TestTrue(TEXT("short tunnel: to end ring B"), NpcTunnelVecNear(end, rowPoint(FVector3d::ZeroVector, 5,
            20.0 * rt), tol));
    }

    // Nothing to draw
    FVector3d start;
    FVector3d end;
    TestFalse(TEXT("600 Rt off the axis: no dots"), TunnelDotRowSegmentRelM(FVector3d(0.0, 600.0 * rt, 0.0), basis,
        rt, -1.0e4 * rt, 1.0e4 * rt, 0, start, end));
    TestFalse(TEXT("tunnel beyond the dots' reach"), TunnelDotRowSegmentRelM(FVector3d::ZeroVector, basis, rt,
        600.0 * rt, 700.0 * rt, 0, start, end));
    TestFalse(TEXT("crossed end arcs"), TunnelDotRowSegmentRelM(FVector3d::ZeroVector, basis, rt, 20.0 * rt,
        -10.0 * rt, 0, start, end));
    TestFalse(TEXT("row -1"), TunnelDotRowSegmentRelM(FVector3d::ZeroVector, basis, rt, -10.0 * rt, 20.0 * rt, -1,
        start, end));
    TestFalse(TEXT("row 8"), TunnelDotRowSegmentRelM(FVector3d::ZeroVector, basis, rt, -10.0 * rt, 20.0 * rt,
        SOL::NPC_TUNNEL_DOT_ROWS, start, end));
    TestFalse(TEXT("invalid basis"), TunnelDotRowSegmentRelM(FVector3d::ZeroVector, FSOLNpcTunnelBasis(), rt,
        -10.0 * rt, 20.0 * rt, 0, start, end));
    TestFalse(TEXT("bad radius"), TunnelDotRowSegmentRelM(FVector3d::ZeroVector, basis, NPCTUNNEL_NAN, -10.0 * rt,
        20.0 * rt, 0, start, end));

    // 49c: the batch writes every row's segment (all rows, capped by the outputs), 0 when empty. Off the axis by
    // sqrt(13) Rt the dots reach about 512 Rt, so both end rings clip: each row runs from end ring A to end ring B at
    // its own clock angle, and matches the single-row function
    FVector3d starts[SOL::NPC_TUNNEL_DOT_ROWS];
    FVector3d ends[SOL::NPC_TUNNEL_DOT_ROWS];
    const FVector3d foot(0.0, 3.0 * rt, -2.0 * rt);
    TestEqual(TEXT("batch: every row"), TunnelDotRowSegmentsRelM(foot, basis, rt, -40.0 * rt, 90.0 * rt,
        TArrayView<FVector3d>(starts), TArrayView<FVector3d>(ends)), static_cast<int32>(SOL::NPC_TUNNEL_DOT_ROWS));
    for (int32 row = 0; row < SOL::NPC_TUNNEL_DOT_ROWS; ++row)
    {
        const FString label = FString::Printf(TEXT("batch, row %d"), row);
        TestTrue(label + TEXT(": starts at end ring A"), NpcTunnelVecNear(starts[row], rowPoint(foot, row, -40.0 * rt),
            tol));
        TestTrue(label + TEXT(": ends at end ring B"), NpcTunnelVecNear(ends[row], rowPoint(foot, row, 90.0 * rt),
            tol));
        TestTrue(label + TEXT(": the single row's segment"), TunnelDotRowSegmentRelM(foot, basis, rt, -40.0 * rt,
            90.0 * rt, row, start, end) && start == starts[row] && end == ends[row]);
    }
    TestEqual(TEXT("batch: capped by the outputs"), TunnelDotRowSegmentsRelM(foot, basis, rt, -40.0 * rt, 90.0 * rt,
        TArrayView<FVector3d>(starts, 3), TArrayView<FVector3d>(ends, 5)), 3);
    TestEqual(TEXT("batch: crossed end arcs"), TunnelDotRowSegmentsRelM(foot, basis, rt, 20.0 * rt, -10.0 * rt,
        TArrayView<FVector3d>(starts), TArrayView<FVector3d>(ends)), 0);
    TestEqual(TEXT("batch: invalid basis"), TunnelDotRowSegmentsRelM(foot, FSOLNpcTunnelBasis(), rt, -10.0 * rt,
        20.0 * rt, TArrayView<FVector3d>(starts), TArrayView<FVector3d>(ends)), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathLaneDataTest, "SOLTest.NpcTunnelMath.BuildTunnelLaneData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 sections 4.2, 4.3 and 4.7 (the per-lane renderer feed, what the smoke compares every texel with):
// BuildTunnelLaneData(lane, entry, viewpoint, body radius A, body radius B, out data, out foot) fills the lane index,
// the foot point relative to the viewpoint (LaneFoot of A and B relative to it), Rt = TunnelRadiusM(R, ships), the
// basis' axis and up, the fade TunnelFade(LaneSegmentDistanceM, R) and the end arcs (TunnelEndArcsM) minus the foot
// arc, leaving the phase at 0 for the caller; it returns whether the lane can have a tunnel (both bodies resolved, a
// valid basis, drawable end rings), whatever its fade. An empty lane gets the minimum radius
bool FSOLNpcTunnelMathLaneDataTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const FSOLNpcLaneEntry lane = NpcTunnelTestLane();
    const double radiusA = 6.371e6;
    const double radiusB = 3.3895e6;
    const FSOLNpcTunnelBasis basis = TunnelBasis(lane.A, lane.B);
    const double laneLengthM = (lane.B - lane.A).Size();
    const double tol = NPCTUNNEL_TOL * lane.B.Size();

    // Beside the middle, 75 lane radii off the axis (smoke view T1)
    const FVector3d viewpoint = lane.A + basis.Axis * (0.5 * laneLengthM) + basis.Up * (75.0 * lane.RadiusM);
    FSOLNpcTunnelLaneData data;
    FSOLNpcTunnelFoot foot;
    TestTrue(TEXT("T1: can have a tunnel"), BuildTunnelLaneData(5, lane, viewpoint, radiusA, radiusB, data, foot));
    TestEqual(TEXT("T1: lane index"), data.Lane, 5);
    const double rt = TunnelRadiusM(lane.RadiusM, lane.ShipCount);
    TestEqual(TEXT("T1: Rt"), data.TunnelRadiusM, rt, NPCTUNNEL_TOL * rt);
    TestTrue(TEXT("T1: axis"), NpcTunnelVecNear(data.Axis, basis.Axis, 1.0e-12));
    TestTrue(TEXT("T1: up"), NpcTunnelVecNear(data.Up, basis.Up, 1.0e-12));
    TestEqual(TEXT("T1: fade = TunnelFade(75 R)"), data.Fade, TunnelFade(75.0 * lane.RadiusM, lane.RadiusM), 1.0e-6);
    TestEqual(TEXT("T1: fade about 0.415"), data.Fade, 0.415, 1.0e-3);
    TestEqual(TEXT("T1: phase left to the caller"), data.Phase, 0.0);
    TestTrue(TEXT("T1: foot valid"), foot.bValid);
    TestEqual(TEXT("T1: foot arc mid-lane"), foot.ArcM, 0.5 * laneLengthM, tol);
    TestEqual(TEXT("T1: h 75 R"), foot.RadialM, 75.0 * lane.RadiusM, tol);
    TestTrue(TEXT("T1: foot point relative to the viewpoint"), NpcTunnelVecNear(data.FootRelM,
        lane.A + basis.Axis * foot.ArcM - viewpoint, tol));
    TestTrue(TEXT("T1: foot point = LaneFoot's"), NpcTunnelVecNear(data.FootRelM, foot.PointRelM, tol));
    const FVector2D endArcs = TunnelEndArcsM(laneLengthM, radiusA, radiusB, rt);
    TestEqual(TEXT("T1: end arc A relative to the foot arc"), data.EndArcAM, endArcs.X - foot.ArcM, tol);
    TestEqual(TEXT("T1: end arc B relative to the foot arc"), data.EndArcBM, endArcs.Y - foot.ArcM, tol);

    // Out of range: still a tunnel lane, fade 0
    const FVector3d farView = lane.A + basis.Axis * (0.5 * laneLengthM) + basis.Right * (500.0 * lane.RadiusM);
    TestTrue(TEXT("500 R: can have a tunnel"), BuildTunnelLaneData(5, lane, farView, radiusA, radiusB, data, foot));
    TestEqual(TEXT("500 R: fade 0"), data.Fade, 0.0);

    // On the axis a quarter of the way along (smoke view T2): full fade, h 0
    const FVector3d onAxis = lane.A + basis.Axis * (0.25 * laneLengthM);
    TestTrue(TEXT("T2: can have a tunnel"), BuildTunnelLaneData(5, lane, onAxis, radiusA, radiusB, data, foot));
    TestEqual(TEXT("T2: full fade"), data.Fade, 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("T2: h about 0"), foot.RadialM, 0.0, tol);

    // An empty lane keeps a tunnel at the minimum radius (D8)
    FSOLNpcLaneEntry empty = lane;
    empty.ShipCount = 0;
    empty.TrafficWeight = 0.0;
    empty.bDrawn = false;
    TestTrue(TEXT("empty lane: can have a tunnel"), BuildTunnelLaneData(2, empty, onAxis, radiusA, radiusB, data,
        foot));
    TestEqual(TEXT("empty lane: Rt = R x min scale"), data.TunnelRadiusM,
        lane.RadiusM * SOL::NPC_TUNNEL_RADIUS_MIN_SCALE, NPCTUNNEL_TOL * lane.RadiusM);

    // No tunnel: unresolved, crossed end rings, A == B
    FSOLNpcLaneEntry unresolved = lane;
    unresolved.bResolved = false;
    unresolved.bDrawn = false;
    TestFalse(TEXT("unresolved: no tunnel"), BuildTunnelLaneData(1, unresolved, onAxis, radiusA, radiusB, data, foot));
    TestEqual(TEXT("unresolved: fade 0"), data.Fade, 0.0);
    FSOLNpcLaneEntry shortLane = lane;
    shortLane.B = lane.A + basis.Axis * 1.0e7;
    TestFalse(TEXT("crossed end rings: no tunnel"), BuildTunnelLaneData(1, shortLane, lane.A + basis.Axis * 5.0e6,
        radiusA, radiusB, data, foot));
    TestEqual(TEXT("crossed end rings: fade 0"), data.Fade, 0.0);
    FSOLNpcLaneEntry point = lane;
    point.B = lane.A;
    TestFalse(TEXT("A == B: no tunnel"), BuildTunnelLaneData(1, point, onAxis, radiusA, radiusB, data, foot));
    TestEqual(TEXT("A == B: fade 0"), data.Fade, 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathStepPhaseTest, "SOLTest.NpcTunnelMath.StepRingPhase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 section 4.4: StepRingPhase(bWasInRange, phase, previous foot arc, foot arc, h, Rt) starts a lane entering
// range at InitialRingPhase(foot arc) and otherwise advances it by the foot arc's change (AdvanceRingPhase), so a lane
// that entered at one arc and moved reads the phase it would have entered with at the new arc (the rings stay fixed to
// the lane), and a camera at rest relative to the lane sees no change
bool FSOLNpcTunnelMathStepPhaseTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const double rt = 1.0e6;
    const double h = 0.5 * rt;
    TestEqual(TEXT("entering: the initial phase"), StepRingPhase(false, 0.77, 123.0, 5.0e8, h, rt),
        InitialRingPhase(5.0e8, h, rt), NPCTUNNEL_TOL);
    TestEqual(TEXT("in range: advanced by the arc change"), StepRingPhase(true, 0.3, 5.0e8, 5.0e8 + 1.3e6, h, rt),
        AdvanceRingPhase(0.3, 1.3e6, h, rt), NPCTUNNEL_TOL);
    TestEqual(TEXT("at rest: unchanged"), StepRingPhase(true, 0.42, 7.0e9, 7.0e9, h, rt), 0.42, NPCTUNNEL_TOL);
    TestEqual(TEXT("NaN previous arc: unchanged"), StepRingPhase(true, 0.42, NPCTUNNEL_NAN, 7.0e9, h, rt), 0.42,
        NPCTUNNEL_TOL);

    // Enter, then move in steps: the phase matches entering at the end arc
    for (const double h2 : { 0.0, 0.5 * rt, 50.0 * rt })
    {
        double phase = StepRingPhase(false, 0.0, 0.0, 3.3e8, h2, rt);
        double arc = 3.3e8;
        for (int32 step = 1; step <= 200; ++step)
        {
            const double next = arc + 1.7e5 * FMath::Sin(0.1 * static_cast<double>(step)) + 2.1e4;
            phase = StepRingPhase(true, phase, arc, next, h2, rt);
            arc = next;
        }
        TestTrue(FString::Printf(TEXT("h %g Rt: lane-fixed after 200 steps"), h2 / rt),
            NpcTunnelWrapDistance(phase, InitialRingPhase(arc, h2, rt)) < 1.0e-9);
        TestTrue(FString::Printf(TEXT("h %g Rt: in [0, 1)"), h2 / rt), phase >= 0.0 && phase < 1.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathVisibilityTest, "SOLTest.NpcTunnelMath.VisibilityRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 sections 4.2 and 4.7 (Q5, Q6, D12): ResolveTunnelsShown follows AreTunnelsShown without the verification
// override, and with it the override's choice (the player's settings ignored) still under sol.NpcTunnel and the map;
// EffectiveTunnelFade is 0 while hidden, 1 for every lane when forced (the all-lanes budget case), else the fade
// clamped (NaN as 0); ShouldDrawTunnels needs the tunnels shown and a fade above 0; the lanes report "drawn" to the
// shared occluders when lane lines or tunnels draw (IsLaneOverlayDrawn); the texture uploads while a tunnel shows and
// once more (all zeros) on the frame the last one goes away (ShouldUploadTunnelTexels)
bool FSOLNpcTunnelMathVisibilityTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    for (int32 bits = 0; bits < 32; ++bits)
    {
        const bool bOverride = (bits & 1) != 0;
        const bool bOverrideShown = (bits & 2) != 0;
        const bool bLanesShown = (bits & 4) != 0;
        const bool bCvarOn = (bits & 8) != 0;
        const bool bMapOpen = (bits & 16) != 0;
        const bool expected = bOverride ? bOverrideShown && bCvarOn && !bMapOpen
            : AreTunnelsShown(bLanesShown, bCvarOn, bMapOpen);
        TestEqual(FString::Printf(TEXT("override %d shown %d, lanes %d, cvar %d, map %d"), bOverride ? 1 : 0,
            bOverrideShown ? 1 : 0, bLanesShown ? 1 : 0, bCvarOn ? 1 : 0, bMapOpen ? 1 : 0),
            ResolveTunnelsShown(bOverride, bOverrideShown, bLanesShown, bCvarOn, bMapOpen), expected);
    }

    // The effective fade
    TestEqual(TEXT("hidden: 0"), EffectiveTunnelFade(0.4, false, false), 0.0);
    TestEqual(TEXT("hidden and forced: 0"), EffectiveTunnelFade(0.4, false, true), 0.0);
    TestEqual(TEXT("shown: the fade"), EffectiveTunnelFade(0.4, true, false), 0.4, NPCTUNNEL_TOL);
    TestEqual(TEXT("forced: 1"), EffectiveTunnelFade(0.4, true, true), 1.0);
    TestEqual(TEXT("forced, out of range: 1"), EffectiveTunnelFade(0.0, true, true), 1.0);
    TestEqual(TEXT("forced, NaN: 1"), EffectiveTunnelFade(NPCTUNNEL_NAN, true, true), 1.0);
    TestEqual(TEXT("NaN: 0"), EffectiveTunnelFade(NPCTUNNEL_NAN, true, false), 0.0);
    TestEqual(TEXT("above 1: clamped"), EffectiveTunnelFade(1.5, true, false), 1.0);
    TestEqual(TEXT("below 0: clamped"), EffectiveTunnelFade(-0.2, true, false), 0.0);

    // Drawing the tunnel component
    const double none[] = { 0.0, 0.0, 0.0 };
    const double one[] = { 0.0, 0.2, 0.0 };
    const double nanOnly[] = { NPCTUNNEL_NAN, 0.0 };
    TestFalse(TEXT("hidden: not drawn"), ShouldDrawTunnels(false, one));
    TestFalse(TEXT("no fade above 0: not drawn"), ShouldDrawTunnels(true, none));
    TestTrue(TEXT("one fade above 0: drawn"), ShouldDrawTunnels(true, one));
    TestFalse(TEXT("NaN only: not drawn"), ShouldDrawTunnels(true, nanOnly));
    TestFalse(TEXT("no lanes: not drawn"), ShouldDrawTunnels(true, TConstArrayView<double>()));

    // The shared occluders' "lanes drawn" flag
    TestFalse(TEXT("neither: not drawn"), IsLaneOverlayDrawn(false, false));
    TestTrue(TEXT("lane lines only"), IsLaneOverlayDrawn(true, false));
    TestTrue(TEXT("tunnels only (an empty lane's tunnel, T6)"), IsLaneOverlayDrawn(false, true));
    TestTrue(TEXT("both"), IsLaneOverlayDrawn(true, true));

    // The upload rule over a sequence of frames: shown, shown, gone (zeros once), gone (no upload)
    const bool shownByFrame[] = { true, true, false, false, true, false };
    const bool uploadByFrame[] = { true, true, true, false, true, true };
    bool bLast = false;
    for (int32 frame = 0; frame < static_cast<int32>(UE_ARRAY_COUNT(shownByFrame)); ++frame)
    {
        TestEqual(FString::Printf(TEXT("frame %d: upload"), frame), ShouldUploadTunnelTexels(shownByFrame[frame],
            bLast), uploadByFrame[frame]);
        bLast = shownByFrame[frame];
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathSlotMeshTest, "SOLTest.NpcTunnelMath.SlotMesh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 section 4.7 (level of detail by slot, the mesh built once; 49c: 96 segments for the two nearest slots):
// RingSegmentsForSlot gives 96 (NPC_TUNNEL_RING_SEGMENTS_NEAREST) segments to ring slots 0-1 of each side, 48 to 2-7,
// 24 to 8-23 and 12 beyond (behind slots like the ahead slot of the same index), 0 out of range;
// TunnelSlotMeshVertexCount is the rings' ribbons ((N + 1) samples x 2 sides each), two end rings at the nearest
// segment count, a quad per dot (rows x dots per gap x gap slots) and a quad per chevron (2 walls x gap slots): 9,476
// vertices per lane. The 8-lane total stays within the SDD's fixed budget of 80,000 vertices (section 4.7), a limit
// set by the design, not a figure moved to follow the code
bool FSOLNpcTunnelMathSlotMeshTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    for (int32 slot = 0; slot < 2 * perSide; ++slot)
    {
        const int32 k = slot < perSide ? slot : slot - perSide;
        const int32 expected = k < 2 ? 96 : k < 8 ? 48 : k < 24 ? 24 : 12;
        TestEqual(FString::Printf(TEXT("slot %d segments"), slot), RingSegmentsForSlot(slot), expected);
    }
    TestEqual(TEXT("slot 0 at the nearest count"), RingSegmentsForSlot(0), SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST);
    TestEqual(TEXT("slot 2 at the near count"), RingSegmentsForSlot(2), SOL::NPC_TUNNEL_RING_SEGMENTS_NEAR);
    TestEqual(TEXT("slot -1: 0"), RingSegmentsForSlot(-1), 0);
    TestEqual(TEXT("slot 128: 0"), RingSegmentsForSlot(2 * perSide), 0);

    int32 ringVertices = 0;
    for (int32 slot = 0; slot < RingSlotCount(); ++slot)
    {
        ringVertices += 2 * (RingSegmentsForSlot(slot) + 1);
    }
    const int32 endRingVertices = 2 * 2 * (SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST + 1);
    const int32 dotVertices = 4 * DetailSlotCount() * SOL::NPC_TUNNEL_DOTS_PER_GAP * SOL::NPC_TUNNEL_DOT_ROWS;
    const int32 chevronVertices = 4 * DetailSlotCount() * 2;
    TestEqual(TEXT("vertex count: the formula"), TunnelSlotMeshVertexCount(),
        ringVertices + endRingVertices + dotVertices + chevronVertices);
    TestEqual(TEXT("vertex count: 9,476 (49c value)"), TunnelSlotMeshVertexCount(), 9476);
    constexpr int32 eightLaneVertexBudget = 80000;
    TestTrue(TEXT("all 8 default lanes within the SDD's fixed vertex budget"),
        8 * TunnelSlotMeshVertexCount() <= eightLaneVertexBudget);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathShapeFadesTest, "SOLTest.NpcTunnelMath.ShapeFades",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 49c (SDD 49 section 12c): the detail near fade by the element's own distance (0 to 0.5 Rt, smoothstep, 1 from 1 Rt,
// scale free, 0 for bad input); the screen-size fade (projected radius / (k x ribbon half extent), clamped; 0 for bad
// input); the chevrons' far fade (D22); the filled chevron dart (tip, wings and notch on the edge, the centre and the
// shaft inside, the notch's rear and the corners beside the tip outside)
bool FSOLNpcTunnelMathShapeFadesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    for (const double rt : { 1.0, 3.0e4, 2.0e8 })
    {
        TestEqual(TEXT("near fade 0 at the camera"), TunnelDetailNearFade(0.0, rt), 0.0, NPCTUNNEL_TOL);
        TestEqual(TEXT("near fade 0 at 0.5 Rt"), TunnelDetailNearFade(0.5 * rt, rt), 0.0, NPCTUNNEL_TOL);
        TestEqual(TEXT("near fade 0.5 at 0.75 Rt"), TunnelDetailNearFade(0.75 * rt, rt), 0.5, 1.0e-9);
        TestEqual(TEXT("near fade 1 at 1 Rt"), TunnelDetailNearFade(rt, rt), 1.0, NPCTUNNEL_TOL);
        TestEqual(TEXT("near fade 1 far away"), TunnelDetailNearFade(1.0e3 * rt, rt), 1.0, NPCTUNNEL_TOL);
        TestTrue(TEXT("near fade rises"), TunnelDetailNearFade(0.6 * rt, rt) < TunnelDetailNearFade(0.9 * rt, rt));
    }
    TestEqual(TEXT("near fade: NaN"), TunnelDetailNearFade(NAN, 1.0), 0.0);
    TestEqual(TEXT("near fade: bad radius"), TunnelDetailNearFade(1.0, 0.0), 0.0);

    // The screen fade reaches 1 at NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS half extents (49c: more than one, so the
    // converging far rings do not pile up into a white vanishing point)
    const double k = SOL::NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS;
    TestTrue(TEXT("screen fade: more than one half extent"), k > 1.0);
    TestEqual(TEXT("screen fade 0 at 0 px"), TunnelScreenSizeFade(0.0, 4.0), 0.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("screen fade 0.5"), TunnelScreenSizeFade(2.0 * k, 4.0), 0.5, NPCTUNNEL_TOL);
    TestEqual(TEXT("screen fade below 1 at one half extent"), TunnelScreenSizeFade(4.0, 4.0), 1.0 / k, NPCTUNNEL_TOL);
    TestEqual(TEXT("screen fade 1 at k half extents"), TunnelScreenSizeFade(4.0 * k, 4.0), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("screen fade 1 beyond"), TunnelScreenSizeFade(400.0 * k, 4.0), 1.0, NPCTUNNEL_TOL);
    TestEqual(TEXT("screen fade: NaN"), TunnelScreenSizeFade(NAN, 4.0), 0.0);
    TestEqual(TEXT("screen fade: no half extent"), TunnelScreenSizeFade(4.0, 0.0), 0.0);

    // Review fix (user, 2026-11-13): the end rings are exempt, so a far lane end keeps its 0.5 floor; a repeating
    // ring's projected radius is Rt x the pixels per metre at its centre (one value round an oblique ring)
    TestEqual(TEXT("ring screen fade: a tiny end ring stays 1"), TunnelRingScreenFade(true, 1.0e6, 1.0e-9, 4.0), 1.0);
    TestEqual(TEXT("ring screen fade: an end ring with no half extent stays 1"), TunnelRingScreenFade(true, 1.0e6,
        1.0e-3, 0.0), 1.0);
    TestEqual(TEXT("ring screen fade: a far end ring keeps the floor"), TunnelRingScreenFade(true, 1.0e6, 1.0e-9, 4.0)
        * EndRingAlpha(1.0e8 * 1.0e6, 0.0, 1.0e6), SOL::NPC_TUNNEL_END_RING_MIN_ALPHA, NPCTUNNEL_TOL);
    TestEqual(TEXT("ring screen fade: a ring is Rt x px per m"), TunnelRingScreenFade(false, 1.0e6, 2.0e-5, 4.0),
        TunnelScreenSizeFade(20.0, 4.0), NPCTUNNEL_TOL);
    TestEqual(TEXT("ring screen fade: a tiny ring fades out"), TunnelRingScreenFade(false, 1.0e6, 1.0e-12, 4.0), 0.0,
        1.0e-6);
    TestEqual(TEXT("ring screen fade: a large ring is full"), TunnelRingScreenFade(false, 1.0e6, 1.0e-3, 4.0), 1.0,
        NPCTUNNEL_TOL);

    // The chevrons' far fade (D22): full inside the start, gone from the end, falling smoothly, scale free
    const double startRadii = SOL::NPC_TUNNEL_CHEVRON_FADE_START_RADII;
    const double endRadii = SOL::NPC_TUNNEL_CHEVRON_FADE_END_RADII;
    TestTrue(TEXT("chevron fade: the mock-up's range"), startRadii >= 8.0 && startRadii <= 16.0
        && endRadii > startRadii && endRadii <= 40.0);
    for (const double rt : { 1.0, 3.0e4, 2.0e8 })
    {
        TestEqual(TEXT("chevron fade 1 near"), TunnelChevronFarFade(2.0 * rt, rt), 1.0, NPCTUNNEL_TOL);
        TestEqual(TEXT("chevron fade 1 at the start"), TunnelChevronFarFade(startRadii * rt, rt), 1.0, NPCTUNNEL_TOL);
        TestEqual(TEXT("chevron fade 0.5 mid-band"), TunnelChevronFarFade(0.5 * (startRadii + endRadii) * rt, rt),
            0.5, 1.0e-9);
        TestEqual(TEXT("chevron fade 0 at the end"), TunnelChevronFarFade(endRadii * rt, rt), 0.0, NPCTUNNEL_TOL);
        TestEqual(TEXT("chevron fade 0 far"), TunnelChevronFarFade(300.0 * rt, rt), 0.0, NPCTUNNEL_TOL);
        TestTrue(TEXT("chevron fade falls"), TunnelChevronFarFade((startRadii + 2.0) * rt, rt)
            > TunnelChevronFarFade((endRadii - 2.0) * rt, rt));
    }
    TestEqual(TEXT("chevron fade: NaN"), TunnelChevronFarFade(NAN, 1.0), 0.0);
    TestEqual(TEXT("chevron fade: bad radius"), TunnelChevronFarFade(1.0, 0.0), 0.0);

    const double n = SOL::NPC_TUNNEL_CHEVRON_NOTCH_FRACTION;
    TestEqual(TEXT("dart: tip on the edge"), ChevronDartDistance(0.0, 1.0, n), 0.0, 1.0e-12);
    TestEqual(TEXT("dart: wing on the edge"), ChevronDartDistance(1.0, -1.0, n), 0.0, 1.0e-12);
    TestEqual(TEXT("dart: other wing on the edge"), ChevronDartDistance(-1.0, -1.0, n), 0.0, 1.0e-12);
    TestEqual(TEXT("dart: notch on the edge"), ChevronDartDistance(0.0, -n, n), 0.0, 1.0e-12);
    TestTrue(TEXT("dart: centre inside"), ChevronDartDistance(0.0, 0.0, n) < 0.0);
    TestTrue(TEXT("dart: shaft inside"), ChevronDartDistance(0.3, -0.4, n) < 0.0);
    TestTrue(TEXT("dart: behind the notch outside"), ChevronDartDistance(0.0, -0.6, n) > 0.0);
    TestTrue(TEXT("dart: beside the tip outside"), ChevronDartDistance(0.9, 0.9, n) > 0.0);
    TestTrue(TEXT("dart: mirror symmetric"),
        FMath::IsNearlyEqual(ChevronDartDistance(0.4, -0.2, n), ChevronDartDistance(-0.4, -0.2, n)));
    TestEqual(TEXT("dart: front edge distance"), ChevronDartDistance(0.0, 2.0, n), 1.0 / FMath::Sqrt(5.0), 1.0e-12);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcTunnelMathTexelCrossfadeTest, "SOLTest.NpcTunnelMath.TexelCrossfade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 17c's data path (Q5, section 4.2): ReadTunnelFadeTexel(texels, lane) reads a lane's fade where the lane and
// point materials read it, row 1 .w of its column (0 out of range or for a buffer shorter than two rows); with that
// fade the lane line's alpha and its ships' point alpha sum to 1 at every distance, a lane without a tunnel keeps the
// #38 values exactly, and after the all-zeros upload every lane is back to #38
bool FSOLNpcTunnelMathTexelCrossfadeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcTunnel;
    const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    TArray<FSOLNpcTunnelLaneData> lanes;
    for (const TPair<int32, double>& laneFade : { TPair<int32, double>(1, 0.25), TPair<int32, double>(4, 1.0) })
    {
        FSOLNpcTunnelLaneData data;
        data.Lane = laneFade.Key;
        data.FootRelM = FVector3d(1.0e5, 2.0e5, 3.0e5);
        data.TunnelRadiusM = 3.0e6;
        data.Axis = FVector3d(1.0, 0.0, 0.0);
        data.Fade = laneFade.Value;
        data.Up = FVector3d(0.0, 0.0, 1.0);
        data.Phase = 0.5;
        data.EndArcAM = -1.0e8;
        data.EndArcBM = 1.0e9;
        lanes.Add(data);
    }
    TArray<FVector4f> texels;
    texels.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), width * 4);
    WriteTunnelTexels(lanes, texels);
    TestEqual(TEXT("lane 1 reads 0.25"), ReadTunnelFadeTexel(texels, 1), 0.25, 1.0e-6);
    TestEqual(TEXT("lane 4 reads 1"), ReadTunnelFadeTexel(texels, 4), 1.0, 1.0e-6);
    TestEqual(TEXT("lane 0 reads 0"), ReadTunnelFadeTexel(texels, 0), 0.0);
    TestEqual(TEXT("lane 63 reads 0"), ReadTunnelFadeTexel(texels, width - 1), 0.0);
    TestEqual(TEXT("lane -1 reads 0"), ReadTunnelFadeTexel(texels, -1), 0.0);
    TestEqual(TEXT("lane 64 reads 0"), ReadTunnelFadeTexel(texels, width), 0.0);
    TArray<FVector4f> shortTexels;
    shortTexels.Init(FVector4f(1.0f, 1.0f, 1.0f, 1.0f), 2 * width - 1);
    TestEqual(TEXT("short buffer reads 0"), ReadTunnelFadeTexel(shortTexels, 1), 0.0);

    // The crossfade with the fade read back: sum 1, #38 values where there is no tunnel
    const double distancesInRadii[] = { 0.0, 1.0e-3, 50.0, 75.0, 100.0, 200.0, 400.0, 1.0e5 };
    for (const double radiusM : { 2.0e5, 3.0e6 })
    {
        for (int32 lane = 0; lane < 8; ++lane)
        {
            const double fade = ReadTunnelFadeTexel(texels, lane);
            for (const double inRadii : distancesInRadii)
            {
                const double d = inRadii * radiusM;
                const double line = SOLNpcLane::LaneLineFadeWithTunnel(d, radiusM, fade);
                const double point = SOLNpcLane::LaneShipPointAlpha(d, radiusM, true, fade);
                const FString label = FString::Printf(TEXT("R %g, lane %d, %g R"), radiusM, lane, inRadii);
                TestEqual(label + TEXT(": sum 1"), line + point, 1.0, NPCTUNNEL_TOL);
                if (lane != 1 && lane != 4)
                {
                    TestEqual(label + TEXT(": no tunnel, the #38 line"), line, SOLNpcLane::LaneLineFade(d, radiusM));
                }
                if (lane == 4)
                {
                    TestEqual(label + TEXT(": full tunnel, no line"), line, 0.0, NPCTUNNEL_TOL);
                }
            }
        }
    }

    // The frame the last tunnel goes away: every lane back to #38
    WriteTunnelTexels(TArray<FSOLNpcTunnelLaneData>(), texels);
    for (int32 lane = 0; lane < width; ++lane)
    {
        const double fade = ReadTunnelFadeTexel(texels, lane);
        TestEqual(FString::Printf(TEXT("after the zeros, lane %d: the #38 line at 400 R"), lane),
            SOLNpcLane::LaneLineFadeWithTunnel(400.0 * 3.0e6, 3.0e6, fade), SOLNpcLane::LaneLineFade(400.0 * 3.0e6,
            3.0e6));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
