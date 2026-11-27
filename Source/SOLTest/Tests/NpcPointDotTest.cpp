/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcPixelMath.h"
#include "Npc/SOLNpcPointMirror.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #54 (Part 54d) point material wiring: the composed SOLNpcPointMirror::PointDot (the float copy of
// SOLNP_PointDot, the only call the baked vertex node of M_SOLNpcPoints makes), the bit-based NaN and infinity guards
// that fast math cannot fold, the material parameter vectors (SOLNpcPixel::PointParams2 / PointParams5 / PointParams6)
// and the far layer's shown rule (SOLNpcPixel::FarLayerShown). Written test-after by the implementer of 54d, not a
// separate contract author; the 54a contract tests in NpcPixelTest.cpp stay the reference for the underlying rule. The
// dot cap tests (SOLTest.NpcPointDot.Cap and the capped rows elsewhere) were written after the code, against the
// user's 2026-11-27 decision (SDD 54 Q13), by the same agent that implemented it. Sizes are in 1080p reference px;
// every threshold is a pixel size, never a distance or a count.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float NPCDOT_TOL = 1.0e-5f;               // Alpha, float, absolute
    constexpr float NPCDOT_REL = 1.0e-4f;               // Radii, float, relative
    constexpr float NPCDOT_SCALE = 100.0f;              // cm per m without far-depth compression

    //////////////////////////////////////////////////////////////////////////
    // Returns a view's projection [1][1] for a 90 degree horizontal FOV ([0][0] = 1) and the view's aspect
    float NpcDotProjectionY(const FVector2f& viewSize)
    {
        return viewSize.X / viewSize.Y;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one device px in cm at an uncompressed distance, the vertex node's 2 placedCm / ([1][1] H)
    float NpcDotPixelCm(const float distanceM, const FVector2f& viewSize)
    {
        return 2.0f * distanceM * NPCDOT_SCALE / (NpcDotProjectionY(viewSize) * viewSize.Y);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs the mirror's PointDot at 90 degrees with the shipped parameters; writes the radius
    float NpcDotRun(const float distanceM, const FVector2f& viewSize, const bool bMapView, float& outRadiusCm)
    {
        const FLinearColor p2 = SOLNpcPixel::PointParams2();
        const FLinearColor p5 = SOLNpcPixel::PointParams5(bMapView);
        const FLinearColor p6 = SOLNpcPixel::PointParams6();
        return SOLNpcPointMirror::PointDot(distanceM, NPCDOT_SCALE, NpcDotPixelCm(distanceM, viewSize), 1.0f, viewSize,
            FVector4f(p2.R, p2.G, p2.B, p2.A), FVector4f(p5.R, p5.G, p5.B, p5.A), FVector4f(p6.R, p6.G, p6.B, p6.A),
            outRadiusCm);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance (m) at which the bounding sphere is sizePx reference px across at 90 degrees
    float NpcDotDistanceForPx(const float sizePx)
    {
        return static_cast<float>(SOLNpcPixel::PixelRadiusM(SOL::NPC_HULL_BOUNDING_DIAMETER_M, 960.0, sizePx));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two floats agree within a relative tolerance
    bool NpcDotNearRel(const float a, const float b)
    {
        return FMath::Abs(a - b) <= NPCDOT_REL * FMath::Max(FMath::Abs(b), 1.0e-6f);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPointDotParamsTest, "SOLTest.NpcPointDot.Params",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The material parameter vectors carry the pinned constants: NpcParams2 = (raster floor 1 device px, D / 2, brightness
// ref, brightness floor), NpcParams5 = (hide 1 px, full 2 px, reference height 1080, map flag), NpcParams6 = (dot cap
// 4 px, 0, 0, 0)
bool FSOLNpcPointDotParamsTest::RunTest(const FString& /*parameters*/)
{
    const FLinearColor p2 = SOLNpcPixel::PointParams2();
    TestEqual(TEXT("NpcParams2.x: raster floor 1 device px (Q5)"), p2.R, 1.0f, 0.0f);
    TestEqual(TEXT("NpcParams2.y: half the bounding diameter"), p2.G, static_cast<float>(34.467 * 0.5), 1.0e-4f);
    TestEqual(TEXT("NpcParams2.z: brightness reference"), p2.B, static_cast<float>(SOL::NPC_POINT_BRIGHTNESS_REF_M),
        0.0f);
    TestEqual(TEXT("NpcParams2.w: brightness floor"), p2.A, static_cast<float>(SOL::NPC_POINT_BRIGHTNESS_MIN), 0.0f);

    const FLinearColor flight = SOLNpcPixel::PointParams5(false);
    const FLinearColor map = SOLNpcPixel::PointParams5(true);
    TestEqual(TEXT("NpcParams5.x: hide at 1 px"), flight.R, 1.0f, 0.0f);
    TestEqual(TEXT("NpcParams5.y: full at 2 px"), flight.G, 2.0f, 0.0f);
    TestEqual(TEXT("NpcParams5.z: 1080p reference"), flight.B, 1080.0f, 0.0f);
    TestEqual(TEXT("NpcParams5.w: flight view 0"), flight.A, 0.0f, 0.0f);
    TestEqual(TEXT("NpcParams5.w: jump map 1"), map.A, 1.0f, 0.0f);
    TestTrue(TEXT("map and flight differ only in the flag"), map.R == flight.R && map.G == flight.G
        && map.B == flight.B);

    const FLinearColor p6 = SOLNpcPixel::PointParams6();
    TestEqual(TEXT("NpcParams6.x: dot cap 4 px (Q13)"), p6.R, 4.0f, 0.0f);
    TestEqual(TEXT("NpcParams6.x: the reference hull switch"), static_cast<double>(p6.R), SOL::NPC_HULL_DOT_PX, 0.0);
    TestTrue(TEXT("NpcParams6.yzw: unused, 0"), p6.G == 0.0f && p6.B == 0.0f && p6.A == 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPointDotFlightTest, "SOLTest.NpcPointDot.Flight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// In the flight view: nothing at or under 1 px, the linear 1-2 px ramp, full beyond 2 px, the true-size footprint up to
// the 4 px dot cap (Q13; the 16 px row is drawn at the cap) and parity with the double rule over a sweep (the same
// function covers rail, CPU, lane, cluster and battle points)
bool FSOLNpcPointDotFlightTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f hd(1920.0f, 1080.0f);
    struct FRow { float SizePx; float Alpha; };
    const FRow rows[] = { { 0.5f, 0.0f }, { 0.999f, 0.0f }, { 1.25f, 0.25f }, { 1.5f, 0.5f }, { 1.75f, 0.75f },
        { 2.5f, 1.0f }, { 16.0f, 1.0f } };
    for (const FRow& row : rows)
    {
        float radiusCm = -1.0f;
        const float distanceM = NpcDotDistanceForPx(row.SizePx);
        const float alpha = NpcDotRun(distanceM, hd, false, radiusCm);
        const FString tag = FString::Printf(TEXT("%.3f px"), row.SizePx);
        TestTrue(tag + TEXT(": alpha"), FMath::Abs(alpha - row.Alpha) <= 1.0e-3f);
        if (row.Alpha == 0.0f)
        {
            TestEqual(tag + TEXT(": hidden, radius 0"), radiusCm, 0.0f, 0.0f);
            continue;
        }

        // At 1080p a drawn dot is at least 1 device px, so the footprint is the true radius D / 2 x scale up to the
        // cap; above it the dot is 4 device px across
        if (row.SizePx > static_cast<float>(SOL::NPC_POINT_DOT_MAX_PX))
        {
            TestTrue(tag + TEXT(": capped at 4 device px at 1080p"), NpcDotNearRel(radiusCm,
                0.5f * 4.0f * NpcDotPixelCm(distanceM, hd)));
            continue;
        }
        TestTrue(tag + TEXT(": true-size footprint at 1080p"), NpcDotNearRel(radiusCm,
            static_cast<float>(SOL::NPC_HULL_BOUNDING_DIAMETER_M * 0.5) * NPCDOT_SCALE));
    }

    // Exactly 1 px is not drawn (strictly above the hide size); a NaN or negative distance is not drawn
    float radiusCm = 0.0f;
    const float exactM = static_cast<float>(SOL::NPC_HULL_BOUNDING_DIAMETER_M * 960.0);
    TestTrue(TEXT("exactly 1 px (or a rounding under it): hidden"), NpcDotRun(exactM * 1.0001f, hd, false, radiusCm)
        == 0.0f);
    TestEqual(TEXT("NaN distance: hidden"), NpcDotRun(std::numeric_limits<float>::quiet_NaN(), hd, false, radiusCm),
        0.0f, 0.0f);
    TestEqual(TEXT("negative distance: hidden"), NpcDotRun(-5.0f, hd, false, radiusCm), 0.0f, 0.0f);
    TestEqual(TEXT("infinite distance: hidden"), NpcDotRun(std::numeric_limits<float>::infinity(), hd, false,
        radiusCm), 0.0f, 0.0f);

    // Parity with the double rule (DotAlpha of ShipSizePx) over 400 log-spaced distances, 1 m to 1e9 m
    int32 mismatches = 0;
    for (int32 step = 0; step < 400; ++step)
    {
        const double distanceM = FMath::Pow(10.0, 9.0 * step / 399.0);
        const double sizePx = SOLNpcPixel::ShipSizePx(SOL::NPC_HULL_BOUNDING_DIAMETER_M, distanceM, 960.0);
        const double expected = SOLNpcPixel::DotAlpha(sizePx, SOL::NPC_SHIP_HIDE_PX, SOL::NPC_SHIP_DOT_FULL_PX);
        const float alpha = NpcDotRun(static_cast<float>(distanceM), hd, false, radiusCm);
        const bool bNearCut = FMath::Abs(sizePx - SOL::NPC_SHIP_HIDE_PX) < 1.0e-4;
        if (FMath::Abs(alpha - expected) > NPCDOT_TOL || (!bNearCut && (alpha > 0.0f) != (expected > 0.0)))
        {
            ++mismatches;
        }
    }
    TestEqual(TEXT("mirror PointDot matches the double rule over the sweep"), mismatches, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPointDotFootprintTest, "SOLTest.NpcPointDot.Footprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The 1 device px raster floor (Q5): at 720p a 1.2 px dot (0.8 device px) is widened to 1 device px while its
// brightness stays the true size's; at 4K the same dot is 2.4 device px, its true size
bool FSOLNpcPointDotFootprintTest::RunTest(const FString& /*parameters*/)
{
    const float distanceM = NpcDotDistanceForPx(1.2f);
    float radius720 = 0.0f;
    const FVector2f low(1280.0f, 720.0f);
    const float alpha720 = NpcDotRun(distanceM, low, false, radius720);
    TestTrue(TEXT("720p: alpha from the true size (0.2)"), FMath::Abs(alpha720 - 0.2f) <= 1.0e-3f);
    TestTrue(TEXT("720p: footprint floored to 1 device px"), NpcDotNearRel(radius720,
        0.5f * NpcDotPixelCm(distanceM, low)));

    float radius4k = 0.0f;
    const FVector2f uhd(3840.0f, 2160.0f);
    const float alpha4k = NpcDotRun(distanceM, uhd, false, radius4k);
    TestTrue(TEXT("4K: the same alpha (reference px)"), FMath::Abs(alpha4k - alpha720) <= NPCDOT_TOL);
    TestTrue(TEXT("4K: 2.4 device px, the true size"), NpcDotNearRel(radius4k,
        0.5f * 2.4f * NpcDotPixelCm(distanceM, uhd)));
    TestTrue(TEXT("4K: the true radius D / 2 x scale"), NpcDotNearRel(radius4k,
        static_cast<float>(SOL::NPC_HULL_BOUNDING_DIAMETER_M * 0.5) * NPCDOT_SCALE));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPointDotCapTest, "SOLTest.NpcPointDot.Cap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The dot cap (SDD 54 Q13, user decision 2026-11-27; written after the code against that decision): in flight a dot is
// drawn at its true size up to NPC_POINT_DOT_MAX_PX (4 reference px, the reference preset's hull switch) and at the cap
// beyond, in device px scaled by the view height, the raster floor still last; the alpha and the fading ramp are
// untouched, and at every quality preset a dot just under its scaled hull switch is no wider than the cap
bool FSOLNpcPointDotCapTest::RunTest(const FString& /*parameters*/)
{
    // Returns the drawn diameter in device px of a dot of sizePx reference px in a view of the given size
    const auto drawnDevicePx = [](const float sizePx, const FVector2f& viewSize, float& outAlpha)
    {
        float radiusCm = 0.0f;
        const float distanceM = NpcDotDistanceForPx(sizePx);
        outAlpha = NpcDotRun(distanceM, viewSize, false, radiusCm);
        return 2.0f * radiusCm / NpcDotPixelCm(distanceM, viewSize);
    };
    const FVector2f hd(1920.0f, 1080.0f);
    struct FRow { float SizePx; float DevicePx; };
    const FRow rows[] = { { 1.5f, 1.5f }, { 3.0f, 3.0f }, { 3.9f, 3.9f }, { 5.0f, 4.0f }, { 10.0f, 4.0f },
        { 40.0f, 4.0f } };
    for (const FRow& row : rows)
    {
        float alpha = 0.0f;
        const float devicePx = drawnDevicePx(row.SizePx, hd, alpha);
        const FString tag = FString::Printf(TEXT("1080p %.1f px"), row.SizePx);
        TestTrue(tag + TEXT(": drawn size"), FMath::Abs(devicePx - row.DevicePx) <= 1.0e-3f * row.DevicePx);
        TestTrue(tag + TEXT(": alpha from the true size"), FMath::Abs(alpha
            - (row.SizePx >= 2.0f ? 1.0f : row.SizePx - 1.0f)) <= 1.0e-3f);
    }

    // The cap is in reference px: 4 px is 2.67 device px at 720p and 8 at 4K
    float alpha = 0.0f;
    TestTrue(TEXT("720p 40 px: capped at 2.67 device px"), FMath::Abs(drawnDevicePx(40.0f, FVector2f(1280.0f, 720.0f),
        alpha) - 4.0f * 720.0f / 1080.0f) <= 1.0e-3f);
    TestTrue(TEXT("4K 40 px: capped at 8 device px"), FMath::Abs(drawnDevicePx(40.0f, FVector2f(3840.0f, 2160.0f),
        alpha) - 8.0f) <= 1.0e-2f);

    // Every quality preset: a dot just under its scaled hull switch (Low x10 = 40 px) is drawn at most 4 px across
    for (int32 preset = 0; preset < static_cast<int32>(UE_ARRAY_COUNT(SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY));
        ++preset)
    {
        const float switchPx = static_cast<float>(SOL::NPC_HULL_DOT_PX * SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY[
            preset] / SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY[SOL::NPC_TUNNEL_QUALITY_REFERENCE_PRESET]);
        TestTrue(FString::Printf(TEXT("preset %d: a %.1f px dot is at most 4 px"), preset, 0.999f * switchPx),
            drawnDevicePx(0.999f * switchPx, hd, alpha) <= 4.0f * (1.0f + 1.0e-4f));
    }

    // The cap's guards: a non-positive, infinite or NaN cap leaves the true size; an infinite size is capped
    using SOLNpcPointMirror::CappedSizePx;
    const float inf = std::numeric_limits<float>::infinity();
    TestEqual(TEXT("cap 0: the true size"), CappedSizePx(10.0f, 0.0f), 10.0f, 0.0f);
    TestEqual(TEXT("negative cap: the true size"), CappedSizePx(10.0f, -4.0f), 10.0f, 0.0f);
    TestEqual(TEXT("infinite cap: the true size"), CappedSizePx(10.0f, inf), 10.0f, 0.0f);
    TestEqual(TEXT("NaN cap: the true size"), CappedSizePx(10.0f, std::numeric_limits<float>::quiet_NaN()), 10.0f,
        0.0f);
    TestEqual(TEXT("infinite size: the cap"), CappedSizePx(inf, 4.0f), 4.0f, 0.0f);
    TestEqual(TEXT("under the cap: unchanged"), CappedSizePx(2.5f, 4.0f), 2.5f, 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPointDotMapTest, "SOLTest.NpcPointDot.Map",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The jump map keeps today's dots (Q9): full brightness at any distance, never under NPC_SHIP_DOT_FULL_PX device px
// across, and no cutoff; close up the true radius still wins
bool FSOLNpcPointDotMapTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f hd(1920.0f, 1080.0f);
    for (const float distanceM : { 1.0e5f, 2.0e6f, 1.0e9f })
    {
        float radiusCm = 0.0f;
        const float alpha = NpcDotRun(distanceM, hd, true, radiusCm);
        const FString tag = FString::Printf(TEXT("map at %.0f m"), distanceM);
        TestEqual(tag + TEXT(": alpha 1"), alpha, 1.0f, 0.0f);
        TestTrue(tag + TEXT(": 2 device px dot"), NpcDotNearRel(radiusCm,
            0.5f * static_cast<float>(SOL::NPC_SHIP_DOT_FULL_PX) * NpcDotPixelCm(distanceM, hd)));
    }
    float radiusNear = 0.0f;
    NpcDotRun(100.0f, hd, true, radiusNear);
    TestTrue(TEXT("map close up: the true radius"), NpcDotNearRel(radiusNear,
        static_cast<float>(SOL::NPC_HULL_BOUNDING_DIAMETER_M * 0.5) * NPCDOT_SCALE));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPointDotBitGuardsTest, "SOLTest.NpcPointDot.BitGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The bit-pattern guards (SOLNP_IsFinite / IsNan / IsPositiveFinite) classify every special value, and DotAlpha keeps
// the contract's infinity rows (+inf drawn full, -inf and NaN hidden) through them
bool FSOLNpcPointDotBitGuardsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcPointMirror;
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float maxF = std::numeric_limits<float>::max();
    const float denormal = std::numeric_limits<float>::denorm_min();
    TestTrue(TEXT("IsFinite: 0, -0, denormal, max, -max"), IsFinite(0.0f) && IsFinite(-0.0f) && IsFinite(denormal)
        && IsFinite(maxF) && IsFinite(-maxF));
    TestTrue(TEXT("IsFinite: not inf, -inf, NaN, -NaN"), !IsFinite(inf) && !IsFinite(-inf) && !IsFinite(nan)
        && !IsFinite(-nan));
    TestTrue(TEXT("IsNan: NaN and -NaN only"), IsNan(nan) && IsNan(-nan) && !IsNan(inf) && !IsNan(-inf)
        && !IsNan(maxF) && !IsNan(0.0f));
    TestTrue(TEXT("IsPositiveFinite: denormal and max yes; 0, -0, inf, NaN no"), IsPositiveFinite(denormal)
        && IsPositiveFinite(maxF) && !IsPositiveFinite(0.0f) && !IsPositiveFinite(-0.0f) && !IsPositiveFinite(inf)
        && !IsPositiveFinite(nan));
    TestEqual(TEXT("DotAlpha(+inf) = 1"), DotAlpha(inf, 1.0f, 2.0f), 1.0f, 0.0f);
    TestEqual(TEXT("DotAlpha(-inf) = 0"), DotAlpha(-inf, 1.0f, 2.0f), 0.0f, 0.0f);
    TestEqual(TEXT("DotAlpha(-NaN) = 0"), DotAlpha(-nan, 1.0f, 2.0f), 0.0f, 0.0f);
    TestEqual(TEXT("DotAlpha(max float) = 1"), DotAlpha(maxF, 1.0f, 2.0f), 1.0f, 0.0f);
    TestEqual(TEXT("DrawnDiameterDevicePx with an infinite reference: the floor"),
        DrawnDiameterDevicePx(1.5f, 1080.0f, inf, 1.0f), 1.0f, 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPointDotFarLayerTest, "SOLTest.NpcPointDot.FarLayerShown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The far layer is shown on the jump map always (Q9) and in flight exactly when FarLayerCanDraw of the bounding
// diameter at the Mid radius says its nearest ship can pass the 1 px cutoff (Q10: dark at the default Mid radius)
bool FSOLNpcPointDotFarLayerTest::RunTest(const FString& /*parameters*/)
{
    TestFalse(TEXT("flight, 90 degrees, default Mid radius: hidden"), SOLNpcPixel::FarLayerShown(false, 960.0,
        2.0e6));
    TestTrue(TEXT("map, default Mid radius: shown"), SOLNpcPixel::FarLayerShown(true, 960.0, 2.0e6));
    TestTrue(TEXT("flight, a 30 km Mid radius: shown"), SOLNpcPixel::FarLayerShown(false, 960.0, 30000.0));
    TestTrue(TEXT("flight, a very narrow FOV: shown"), SOLNpcPixel::FarLayerShown(false, 60000.0, 2.0e6));
    TestTrue(TEXT("flight, bad focal: shown (the shader decides)"), SOLNpcPixel::FarLayerShown(false, 0.0, 2.0e6));
    int32 mismatches = 0;
    for (int32 step = 0; step < 200; ++step)
    {
        const double midM = FMath::Pow(10.0, 3.0 + 6.0 * step / 199.0);
        const bool bExpected = SOLNpcPixel::FarLayerCanDraw(SOL::NPC_HULL_BOUNDING_DIAMETER_M, 960.0, midM,
            SOL::NPC_SHIP_HIDE_PX);
        mismatches += SOLNpcPixel::FarLayerShown(false, 960.0, midM) == bExpected ? 0 : 1;
        mismatches += SOLNpcPixel::FarLayerShown(true, 960.0, midM) ? 0 : 1;
    }
    TestEqual(TEXT("flight equals FarLayerCanDraw, the map always shown"), mismatches, 0);
    return true;
}

#endif
