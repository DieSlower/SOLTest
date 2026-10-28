/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudShapeMath.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // Tolerances
    constexpr float HUDSHAPES_TOLERANCE = 1.0e-5f;          // Pure math values
    constexpr float HUDSHAPES_POS_TOLERANCE = 1.0e-3f;      // Pixel positions and local coordinates
    constexpr float HUDSHAPES_KIND_TOLERANCE = 1.0e-3f;     // Decoded size parameters

    // Shape geometry used by the distance tests
    constexpr float HUDSHAPES_LINE_HALF_LENGTH = 10.0f;
    constexpr float HUDSHAPES_LINE_HALF_WIDTH = 1.5f;
    constexpr float HUDSHAPES_ARC_RADIUS = 10.0f;
    constexpr float HUDSHAPES_ARC_HALF_WIDTH = 1.0f;
    constexpr float HUDSHAPES_PANEL_CORNER = 4.0f;

    // Jitter sweep: screen rows and ages
    constexpr int32 HUDSHAPES_JITTER_ROWS = 720;
    constexpr int32 HUDSHAPES_JITTER_AGES = 200;
    constexpr float HUDSHAPES_JITTER_MAX_AGE_S = 100.0f;
    constexpr double HUDSHAPES_JITTER_SMALL_SHARE = 0.1;    // "Visible" jitter: above this fraction of the max
    constexpr double HUDSHAPES_JITTER_MAX_SHARE = 0.2;      // At most this share of samples is visible
    constexpr double HUDSHAPES_JITTER_TEAR_SHARE = 0.25;    // At least one tear reaches this fraction of the max

    // Quad layout: 4 vertices and 6 indices per shape
    constexpr int32 HUDSHAPES_VERTS_PER_SHAPE = 4;
    constexpr int32 HUDSHAPES_INDICES_PER_SHAPE = 6;

    //////////////////////////////////////////////////////////////////////////
    // Returns the glow profile computed straight from the SOL::Hologram constants (the reference formula)
    float HudShapesReferenceGlow(const float distancePx)
    {
        using namespace SOL::Hologram;
        const double d = FMath::Max(0.0, static_cast<double>(distancePx));
        const double core = HUD_GLOW_CORE_WEIGHT * FMath::Exp(-HUD_GLOW_CORE_FALLOFF * FMath::Max(0.0, d - 0.5));
        const double halo = HUD_GLOW_HALO_WEIGHT * FMath::Exp(-FMath::Pow(HUD_GLOW_HALO_SCALE * d, HUD_GLOW_HALO_POWER));
        return static_cast<float>(core + halo);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a vertex's local-frame position (TexCoords[0..1])
    FVector2f HudShapesLocal(const FSlateVertex& vertex)
    {
        return FVector2f(vertex.TexCoords[0], vertex.TexCoords[1]);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a vertex's canvas-space position
    FVector2f HudShapesPos(const FSlateVertex& vertex)
    {
        return FVector2f(vertex.Position.X, vertex.Position.Y);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that shape k's six indices are 4k + {0, 1, 2, 0, 2, 3}
    void HudShapesCheckIndices(FAutomationTestBase& test, const FSOLHudShapeBatch& batch, const int32 shapeIndex)
    {
        static constexpr int32 PATTERN[HUDSHAPES_INDICES_PER_SHAPE] = { 0, 1, 2, 0, 2, 3 };
        const TArray<SlateIndex>& indices = batch.GetIndices();
        const int32 first = shapeIndex * HUDSHAPES_INDICES_PER_SHAPE;
        if (!test.TestTrue(TEXT("index buffer holds the shape"), indices.Num() >= first + HUDSHAPES_INDICES_PER_SHAPE))
        {
            return;
        }
        for (int32 i = 0; i < HUDSHAPES_INDICES_PER_SHAPE; ++i)
        {
            test.TestEqual(FString::Printf(TEXT("shape %d index %d"), shapeIndex, i),
                static_cast<int32>(indices[first + i]), shapeIndex * HUDSHAPES_VERTS_PER_SHAPE + PATTERN[i]);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that every vertex of a shape carries the same size parameter, kind and size2, and the same colour
    void HudShapesCheckParams(FAutomationTestBase& test, const FSOLHudShapeBatch& batch, const int32 shapeIndex,
        const float expectedSize1, const ESOLHudShapeKind expectedKind, const float expectedSize2, const FColor& expectedRgb,
        const int32 expectedAlpha, const int32 alphaTolerance)
    {
        const TArray<FSlateVertex>& vertices = batch.GetVertices();
        const int32 first = shapeIndex * HUDSHAPES_VERTS_PER_SHAPE;
        if (!test.TestTrue(TEXT("vertex buffer holds the shape"), vertices.Num() >= first + HUDSHAPES_VERTS_PER_SHAPE))
        {
            return;
        }
        for (int32 v = 0; v < HUDSHAPES_VERTS_PER_SHAPE; ++v)
        {
            const FSlateVertex& vertex = vertices[first + v];
            const FString label = FString::Printf(TEXT("shape %d vertex %d"), shapeIndex, v);
            test.TestEqual(label + TEXT(": size1"), vertex.TexCoords[2], expectedSize1, HUDSHAPES_KIND_TOLERANCE);
            ESOLHudShapeKind kind = ESOLHudShapeKind::Line;
            float size2 = -1.0f;
            SOLHudShapeMath::DecodeKind(vertex.TexCoords[3], kind, size2);
            test.TestTrue(label + TEXT(": kind"), kind == expectedKind);
            test.TestEqual(label + TEXT(": size2"), size2, expectedSize2, HUDSHAPES_KIND_TOLERANCE);
            test.TestEqual(label + TEXT(": R"), static_cast<int32>(vertex.Color.R), static_cast<int32>(expectedRgb.R));
            test.TestEqual(label + TEXT(": G"), static_cast<int32>(vertex.Color.G), static_cast<int32>(expectedRgb.G));
            test.TestEqual(label + TEXT(": B"), static_cast<int32>(vertex.Color.B), static_cast<int32>(expectedRgb.B));
            test.TestTrue(label + TEXT(": alpha"), FMath::Abs(static_cast<int32>(vertex.Color.A) - expectedAlpha)
                <= alphaTolerance);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that a shape's four local positions are the four corners (+-extent.x, +-extent.y), one each
    void HudShapesCheckCorners(FAutomationTestBase& test, const FSOLHudShapeBatch& batch, const int32 shapeIndex,
        const FVector2f& extent)
    {
        const TArray<FSlateVertex>& vertices = batch.GetVertices();
        const int32 first = shapeIndex * HUDSHAPES_VERTS_PER_SHAPE;
        if (vertices.Num() < first + HUDSHAPES_VERTS_PER_SHAPE)
        {
            test.AddError(TEXT("vertex buffer does not hold the shape"));
            return;
        }
        bool bSeen[2][2] = { { false, false }, { false, false } };
        for (int32 v = 0; v < HUDSHAPES_VERTS_PER_SHAPE; ++v)
        {
            const FVector2f local = HudShapesLocal(vertices[first + v]);
            const FString label = FString::Printf(TEXT("shape %d vertex %d"), shapeIndex, v);
            test.TestEqual(label + TEXT(": |local.x| = extent.x"), FMath::Abs(local.X), extent.X,
                HUDSHAPES_POS_TOLERANCE);
            test.TestEqual(label + TEXT(": |local.y| = extent.y"), FMath::Abs(local.Y), extent.Y,
                HUDSHAPES_POS_TOLERANCE);
            bSeen[local.X > 0.0f ? 1 : 0][local.Y > 0.0f ? 1 : 0] = true;
        }
        test.TestTrue(FString::Printf(TEXT("shape %d covers all four corners"), shapeIndex),
            bSeen[0][0] && bSeen[0][1] && bSeen[1][0] && bSeen[1][1]);
    }
}

// --- SOLHudShapeMath ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesGlowProfileTest, "SOLTest.HudShapes.GlowProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The glow is 1 at the core, clamps negative distances, matches LT's formula, never rises and fades out by the pad
bool FSOLHudShapesGlowProfileTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::GlowProfile;
    TestEqual(TEXT("core is 1"), GlowProfile(0.0f), 1.0f, HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("negative clamps to the core"), GlowProfile(-5.0f), GlowProfile(0.0f), HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("inside the half-pixel core the core term is flat"), GlowProfile(0.5f), HudShapesReferenceGlow(0.5f),
        HUDSHAPES_TOLERANCE);

    // Exact values from the formula
    const float samples[] = { 0.25f, 1.0f, 3.0f, 10.0f, 20.0f };
    for (const float d : samples)
    {
        TestEqual(FString::Printf(TEXT("formula at %.2f px"), d), GlowProfile(d), HudShapesReferenceGlow(d),
            HUDSHAPES_TOLERANCE);
    }

    // Non-increasing outward, and nearly gone at the quad padding
    float previous = GlowProfile(0.0f);
    bool bNonIncreasing = true;
    for (float d = 0.05f; d <= SOL::Hologram::HUD_GLOW_PAD_PX; d += 0.05f)
    {
        const float value = GlowProfile(d);
        bNonIncreasing &= value <= previous + HUDSHAPES_TOLERANCE;
        previous = value;
    }
    TestTrue(TEXT("non-increasing in distance"), bNonIncreasing);
    TestTrue(TEXT("below 1% at the pad"), GlowProfile(SOL::Hologram::HUD_GLOW_PAD_PX) < 0.01f);
    TestTrue(TEXT("positive at the pad"), GlowProfile(SOL::Hologram::HUD_GLOW_PAD_PX) > 0.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesLineDistanceTest, "SOLTest.HudShapes.LineDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The capsule distance is negative inside, measured from the segment beside it and from the end point past its end
bool FSOLHudShapesLineDistanceTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::LineDistancePx;
    constexpr float L = HUDSHAPES_LINE_HALF_LENGTH;
    constexpr float w = HUDSHAPES_LINE_HALF_WIDTH;
    TestEqual(TEXT("centre is -w"), LineDistancePx(FVector2f(0.0f, 0.0f), L, w), -w, HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("beside the middle"), LineDistancePx(FVector2f(0.0f, 5.0f), L, w), 5.0f - w, HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("below the middle"), LineDistancePx(FVector2f(0.0f, -5.0f), L, w), 5.0f - w, HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("beside, along the segment"), LineDistancePx(FVector2f(L - 1.0f, 5.0f), L, w), 5.0f - w,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("past the +X end (3-4-5)"), LineDistancePx(FVector2f(L + 3.0f, 4.0f), L, w), 5.0f - w,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("past the -X end (3-4-5)"), LineDistancePx(FVector2f(-L - 3.0f, -4.0f), L, w), 5.0f - w,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("on the end point"), LineDistancePx(FVector2f(L, 0.0f), L, w), -w, HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("zero length is a disc"), LineDistancePx(FVector2f(3.0f, 4.0f), 0.0f, w), 5.0f - w,
        HUDSHAPES_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesArcDistanceTest, "SOLTest.HudShapes.ArcDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The arc is centred on +Y, symmetric in X, measured from its end caps outside its aperture; pi is the full ring
bool FSOLHudShapesArcDistanceTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::ArcDistancePx;
    constexpr float R = HUDSHAPES_ARC_RADIUS;
    constexpr float w = HUDSHAPES_ARC_HALF_WIDTH;
    const float halfPi = UE_PI * 0.5f;

    // Half aperture pi/2: the upper half ring, its caps at (+-R, 0)
    TestEqual(TEXT("top of the arc"), ArcDistancePx(FVector2f(0.0f, R), R, w, halfPi), -w, HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("bottom is measured from the cap"), ArcDistancePx(FVector2f(0.0f, -R), R, w, halfPi),
        R * UE_SQRT_2 - w, HUDSHAPES_POS_TOLERANCE);
    TestEqual(TEXT("outside the top"), ArcDistancePx(FVector2f(0.0f, R + 4.0f), R, w, halfPi), 4.0f - w,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("inside the ring at the top"), ArcDistancePx(FVector2f(0.0f, R - 3.0f), R, w, halfPi), 3.0f - w,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("below the +X cap"), ArcDistancePx(FVector2f(R, -5.0f), R, w, halfPi), 5.0f - w,
        HUDSHAPES_POS_TOLERANCE);

    // Symmetric in X
    const FVector2f probes[] = { FVector2f(3.0f, 7.0f), FVector2f(8.0f, -2.0f), FVector2f(12.0f, 1.0f),
        FVector2f(4.0f, -11.0f) };
    for (const FVector2f& p : probes)
    {
        TestEqual(FString::Printf(TEXT("symmetric at (%.0f, %.0f)"), p.X, p.Y), ArcDistancePx(p, R, w, halfPi),
            ArcDistancePx(FVector2f(-p.X, p.Y), R, w, halfPi), HUDSHAPES_TOLERANCE);
    }

    // Half aperture pi: the full ring, |length(p) - R| - w everywhere (including the bottom and the X = 0 column)
    const FVector2f ringProbes[] = { FVector2f(0.0f, R), FVector2f(0.0f, -R), FVector2f(0.0f, -R - 4.0f),
        FVector2f(0.0f, 2.0f), FVector2f(6.0f, -8.0f), FVector2f(-6.0f, -8.0f), FVector2f(15.0f, 0.0f),
        FVector2f(-1.0f, -3.0f), FVector2f(0.0f, 0.0f) };
    for (const FVector2f& p : ringProbes)
    {
        const float expected = FMath::Abs(p.Size() - R) - w;
        TestEqual(FString::Printf(TEXT("full ring at (%.0f, %.0f)"), p.X, p.Y), ArcDistancePx(p, R, w, UE_PI),
            expected, HUDSHAPES_POS_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesPanelDistanceTest, "SOLTest.HudShapes.PanelDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rounded box is -min(half) at its centre, 0 on its edge midpoints, positive outside and rounded at the corners
bool FSOLHudShapesPanelDistanceTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::PanelDistancePx;
    const FVector2f half(20.0f, 10.0f);
    constexpr float r = HUDSHAPES_PANEL_CORNER;
    TestEqual(TEXT("centre is -min(half)"), PanelDistancePx(FVector2f::ZeroVector, half, r), -10.0f,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("right edge midpoint"), PanelDistancePx(FVector2f(half.X, 0.0f), half, r), 0.0f,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("top edge midpoint"), PanelDistancePx(FVector2f(0.0f, -half.Y), half, r), 0.0f,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("3 px outside along X"), PanelDistancePx(FVector2f(half.X + 3.0f, 0.0f), half, r), 3.0f,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("3 px outside along -X"), PanelDistancePx(FVector2f(-half.X - 3.0f, 0.0f), half, r), 3.0f,
        HUDSHAPES_TOLERANCE);
    TestEqual(TEXT("2 px inside along Y"), PanelDistancePx(FVector2f(0.0f, half.Y - 2.0f), half, r), -2.0f,
        HUDSHAPES_TOLERANCE);

    // The sharp corner lies outside the rounded one: sqrt(2) r - r
    TestEqual(TEXT("sharp corner is outside"), PanelDistancePx(half, half, r), r * UE_SQRT_2 - r,
        HUDSHAPES_POS_TOLERANCE);
    TestTrue(TEXT("square box (r = 0) corner is on the edge"), FMath::Abs(PanelDistancePx(half, half, 0.0f))
        < HUDSHAPES_POS_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesKindPackingTest, "SOLTest.HudShapes.KindPacking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// EncodeKind is kind x stride + param, and DecodeKind gives back the kind and the param for every kind
bool FSOLHudShapesKindPackingTest::RunTest(const FString& /*parameters*/)
{
    const ESOLHudShapeKind kinds[] = { ESOLHudShapeKind::Line, ESOLHudShapeKind::Arc, ESOLHudShapeKind::Panel };
    const float params[] = { 0.0f, 0.5f, 1.25f, 400.0f, 2000.0f };
    for (const ESOLHudShapeKind kind : kinds)
    {
        for (const float param : params)
        {
            const FString label = FString::Printf(TEXT("kind %d param %.2f"), static_cast<int32>(kind), param);
            const float encoded = SOLHudShapeMath::EncodeKind(kind, param);
            TestEqual(label + TEXT(": encoded"), encoded,
                static_cast<float>(kind) * SOL::Hologram::HUD_SHAPE_KIND_STRIDE + param, HUDSHAPES_KIND_TOLERANCE);
            ESOLHudShapeKind decodedKind = ESOLHudShapeKind::Line;
            float decodedParam = -1.0f;
            SOLHudShapeMath::DecodeKind(encoded, decodedKind, decodedParam);
            TestTrue(label + TEXT(": kind round-trips"), decodedKind == kind);
            TestEqual(label + TEXT(": param round-trips"), decodedParam, param, HUDSHAPES_KIND_TOLERANCE);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesSmoothNoiseTest, "SOLTest.HudShapes.SmoothNoise",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The noise stays in [0, 1], is continuous and deterministic, and actually varies
bool FSOLHudShapesSmoothNoiseTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::SmoothNoise;
    constexpr float sweepMin = -100.0f;
    constexpr float sweepStep = 0.01f;
    constexpr int32 sweepCount = 20000;     // -100 .. 100
    constexpr float continuityStep = 1.0e-3f;
    constexpr float continuityMax = 0.01f;

    bool bInRange = true;
    bool bContinuous = true;
    bool bDeterministic = true;
    float minValue = 1.0f;
    float maxValue = 0.0f;
    for (int32 i = 0; i <= sweepCount; ++i)
    {
        const float x = sweepMin + sweepStep * static_cast<float>(i);
        const float n = SmoothNoise(x);
        bInRange &= n >= 0.0f && n <= 1.0f;
        bContinuous &= FMath::Abs(SmoothNoise(x + continuityStep) - n) < continuityMax;
        bDeterministic &= SmoothNoise(x) == n;
        minValue = FMath::Min(minValue, n);
        maxValue = FMath::Max(maxValue, n);
    }
    TestTrue(TEXT("in [0, 1]"), bInRange);
    TestTrue(TEXT("continuous"), bContinuous);
    TestTrue(TEXT("deterministic"), bDeterministic);
    TestTrue(TEXT("not constant (spans more than half the range)"), maxValue - minValue > 0.5f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesJitterTest, "SOLTest.HudShapes.Jitter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Row jitter is off at zero strength, bounded, deterministic, linear in strength, and sparse with rare tears both ways
bool FSOLHudShapesJitterTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::JitterOffsetFraction;
    const float maxFraction = static_cast<float>(SOL::Hologram::HUD_JITTER_MAX_FRACTION);
    constexpr float boundEps = 1.0e-7f;

    // Off at zero or negative strength
    TestEqual(TEXT("strength 0"), JitterOffsetFraction(0.3f, 2.0f, 0.0f), 0.0f);
    TestEqual(TEXT("negative strength"), JitterOffsetFraction(0.3f, 2.0f, -1.0f), 0.0f);

    int32 visibleCount = 0;
    int32 total = 0;
    bool bBounded = true;
    bool bBoundedAtTwo = true;
    bool bDeterministic = true;
    bool bLinear = true;
    bool bAnyTear = false;
    bool bPositive = false;
    bool bNegative = false;
    bool bOffAtZero = true;
    for (int32 ageIndex = 0; ageIndex < HUDSHAPES_JITTER_AGES; ++ageIndex)
    {
        const float age = HUDSHAPES_JITTER_MAX_AGE_S * static_cast<float>(ageIndex)
            / static_cast<float>(HUDSHAPES_JITTER_AGES);
        for (int32 row = 0; row < HUDSHAPES_JITTER_ROWS; ++row)
        {
            const float y01 = static_cast<float>(row) / static_cast<float>(HUDSHAPES_JITTER_ROWS);
            const float value = JitterOffsetFraction(y01, age, 1.0f);
            const float doubled = JitterOffsetFraction(y01, age, 2.0f);
            bBounded &= FMath::Abs(value) <= maxFraction + boundEps;
            bBoundedAtTwo &= FMath::Abs(doubled) <= 2.0f * maxFraction + boundEps;
            bDeterministic &= JitterOffsetFraction(y01, age, 1.0f) == value;
            bLinear &= FMath::Abs(doubled - 2.0f * value) <= 1.0e-4f * maxFraction;
            bOffAtZero &= JitterOffsetFraction(y01, age, 0.0f) == 0.0f;
            visibleCount += FMath::Abs(value) > HUDSHAPES_JITTER_SMALL_SHARE * maxFraction ? 1 : 0;
            bAnyTear |= FMath::Abs(value) > HUDSHAPES_JITTER_TEAR_SHARE * maxFraction;
            bPositive |= value > 0.0f;
            bNegative |= value < 0.0f;
            ++total;
        }
    }
    TestTrue(TEXT("bounded by max fraction x strength"), bBounded);
    TestTrue(TEXT("bounded at strength 2"), bBoundedAtTwo);
    TestTrue(TEXT("deterministic"), bDeterministic);
    TestTrue(TEXT("linear in strength"), bLinear);
    TestTrue(TEXT("zero strength is off everywhere"), bOffAtZero);
    const double visibleShare = static_cast<double>(visibleCount) / static_cast<double>(total);
    TestTrue(FString::Printf(TEXT("sparse: visible share %.3f below the limit"), visibleShare),
        visibleShare < HUDSHAPES_JITTER_MAX_SHARE);
    TestTrue(TEXT("rare tears happen"), bAnyTear);
    TestTrue(TEXT("tears go right"), bPositive);
    TestTrue(TEXT("tears go left"), bNegative);
    return true;
}

// --- FSOLHudShapeBatch ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesBatchLineTest, "SOLTest.HudShapes.BatchLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A line is one quad around its midpoint, its local X along a -> b, padded by width and pad, with its parameters
bool FSOLHudShapesBatchLineTest::RunTest(const FString& /*parameters*/)
{
    constexpr float halfWidth = 1.0f;
    constexpr float pad = SOL::Hologram::HUD_GLOW_PAD_PX;
    const FVector2f a(100.0f, 50.0f);
    const FVector2f b(160.0f, 130.0f);          // 60-80-100 triangle: length 100
    const FVector2f mid = (a + b) * 0.5f;
    const FVector2f dir(0.6f, 0.8f);
    const FVector2f perp(-dir.Y, dir.X);
    constexpr float halfLength = 50.0f;
    const FLinearColor color(0.2f, 0.6f, 1.0f, 1.0f);
    constexpr float intensity = 0.75f;

    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Hologram::HUD_MAX_SHAPES);
    TestEqual(TEXT("capacity"), batch.GetMaxShapes(), SOL::Hologram::HUD_MAX_SHAPES);
    TestEqual(TEXT("empty after Initialize"), batch.GetShapeCount(), 0);
    TestTrue(TEXT("line added"), batch.AddLine(a, b, halfWidth, color, intensity, pad));
    TestEqual(TEXT("one shape"), batch.GetShapeCount(), 1);
    TestEqual(TEXT("four vertices"), batch.GetVertices().Num(), HUDSHAPES_VERTS_PER_SHAPE);
    TestEqual(TEXT("six indices"), batch.GetIndices().Num(), HUDSHAPES_INDICES_PER_SHAPE);
    HudShapesCheckIndices(*this, batch, 0);

    const FColor rgb = (color * intensity).ToFColorSRGB();
    HudShapesCheckParams(*this, batch, 0, halfLength, ESOLHudShapeKind::Line, halfWidth, rgb, 255, 0);
    HudShapesCheckCorners(*this, batch, 0, FVector2f(halfLength + halfWidth + pad, halfWidth + pad));

    // Canvas position = mid + dir local.x + perp local.y
    for (const FSlateVertex& vertex : batch.GetVertices())
    {
        const FVector2f local = HudShapesLocal(vertex);
        const FVector2f offset = HudShapesPos(vertex) - mid;
        TestEqual(TEXT("along the line = local.x"), FVector2f::DotProduct(offset, dir), local.X, HUDSHAPES_POS_TOLERANCE);
        TestEqual(TEXT("across the line = local.y"), FVector2f::DotProduct(offset, perp), local.Y,
            HUDSHAPES_POS_TOLERANCE);
    }

    // A second shape's indices continue from its own four vertices
    TestTrue(TEXT("second line added"), batch.AddLine(b, a, halfWidth, color, intensity, pad));
    HudShapesCheckIndices(*this, batch, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesBatchDegenerateLineTest, "SOLTest.HudShapes.BatchDegenerateLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero-length line is still added, as a dot: local X is screen +X and the half length is 0
bool FSOLHudShapesBatchDegenerateLineTest::RunTest(const FString& /*parameters*/)
{
    constexpr float halfWidth = 2.0f;
    constexpr float pad = 3.0f;
    const FVector2f point(10.0f, 10.0f);

    FSOLHudShapeBatch batch;
    batch.Initialize(1);
    TestTrue(TEXT("degenerate line added"), batch.AddLine(point, point, halfWidth, FLinearColor::White, 1.0f, pad));
    TestEqual(TEXT("one shape"), batch.GetShapeCount(), 1);
    HudShapesCheckParams(*this, batch, 0, 0.0f, ESOLHudShapeKind::Line, halfWidth,
        FLinearColor::White.ToFColorSRGB(), 255, 0);
    HudShapesCheckCorners(*this, batch, 0, FVector2f(halfWidth + pad, halfWidth + pad));
    for (const FSlateVertex& vertex : batch.GetVertices())
    {
        const FVector2f offset = HudShapesPos(vertex) - point;
        const FVector2f local = HudShapesLocal(vertex);
        TestFalse(TEXT("finite position"), offset.ContainsNaN());
        TestEqual(TEXT("local X is screen X"), offset.X, local.X, HUDSHAPES_POS_TOLERANCE);
        TestEqual(TEXT("local Y is screen Y"), offset.Y, local.Y, HUDSHAPES_POS_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesBatchArcTest, "SOLTest.HudShapes.BatchArc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An arc's quad is centred on the ring, its local +Y points at the arc's middle, and alpha carries the half aperture
bool FSOLHudShapesBatchArcTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f center(300.0f, 200.0f);
    constexpr float radius = 40.0f;
    constexpr float halfWidth = 1.0f;
    constexpr float midAngle = 0.7f;
    constexpr float halfAperture = 1.0f;
    constexpr float pad = SOL::Hologram::HUD_GLOW_PAD_PX;
    const FLinearColor color(1.0f, 0.5f, 0.25f, 1.0f);
    constexpr float intensity = 1.0f;
    const FVector2f up(FMath::Cos(midAngle), FMath::Sin(midAngle));

    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Hologram::HUD_MAX_SHAPES);
    TestTrue(TEXT("arc added"), batch.AddArc(center, radius, halfWidth, midAngle, halfAperture, color, intensity, pad));
    TestEqual(TEXT("one shape"), batch.GetShapeCount(), 1);
    HudShapesCheckIndices(*this, batch, 0);
    const int32 alpha = FMath::RoundToInt(255.0f * halfAperture / UE_PI);
    HudShapesCheckParams(*this, batch, 0, radius, ESOLHudShapeKind::Arc, halfWidth, (color * intensity).ToFColorSRGB(),
        alpha, 1);
    const float extent = radius + halfWidth + pad;
    HudShapesCheckCorners(*this, batch, 0, FVector2f(extent, extent));
    for (const FSlateVertex& vertex : batch.GetVertices())
    {
        const FVector2f local = HudShapesLocal(vertex);
        const FVector2f offset = HudShapesPos(vertex) - center;
        TestEqual(TEXT("toward the arc's middle = local.y"), FVector2f::DotProduct(offset, up), local.Y,
            HUDSHAPES_POS_TOLERANCE);
        TestEqual(TEXT("same distance from the centre"), offset.Size(), local.Size(), HUDSHAPES_POS_TOLERANCE);
    }

    // The half aperture clamps to [0, pi]
    TestTrue(TEXT("wide arc added"), batch.AddArc(center, radius, halfWidth, midAngle, 5.0f, color, intensity, pad));
    HudShapesCheckParams(*this, batch, 1, radius, ESOLHudShapeKind::Arc, halfWidth, (color * intensity).ToFColorSRGB(),
        255, 1);
    TestTrue(TEXT("negative arc added"), batch.AddArc(center, radius, halfWidth, midAngle, -1.0f, color, intensity,
        pad));
    HudShapesCheckParams(*this, batch, 2, radius, ESOLHudShapeKind::Arc, halfWidth, (color * intensity).ToFColorSRGB(),
        0, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesBatchRingTest, "SOLTest.HudShapes.BatchRing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ring is an arc with a half aperture of pi (alpha 255) around its centre
bool FSOLHudShapesBatchRingTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f center(64.0f, 48.0f);
    constexpr float radius = 12.0f;
    constexpr float halfWidth = SOL::Hologram::HUD_LINE_HALF_WIDTH_PX;
    constexpr float pad = SOL::Hologram::HUD_GLOW_PAD_PX;
    const FLinearColor color(0.1f, 0.9f, 0.3f, 1.0f);
    constexpr float intensity = 0.5f;

    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Hologram::HUD_MAX_SHAPES);
    TestTrue(TEXT("ring added"), batch.AddRing(center, radius, halfWidth, color, intensity, pad));
    TestEqual(TEXT("one shape"), batch.GetShapeCount(), 1);
    HudShapesCheckIndices(*this, batch, 0);
    HudShapesCheckParams(*this, batch, 0, radius, ESOLHudShapeKind::Arc, halfWidth, (color * intensity).ToFColorSRGB(),
        255, 1);
    const float extent = radius + halfWidth + pad;
    HudShapesCheckCorners(*this, batch, 0, FVector2f(extent, extent));
    for (const FSlateVertex& vertex : batch.GetVertices())
    {
        TestEqual(TEXT("same distance from the centre"), (HudShapesPos(vertex) - center).Size(),
            HudShapesLocal(vertex).Size(), HUDSHAPES_POS_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesBatchPanelTest, "SOLTest.HudShapes.BatchPanel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A panel is an axis-aligned quad around its centre, its half size in the size params and its fill in alpha
bool FSOLHudShapesBatchPanelTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f center(500.0f, 400.0f);
    const FVector2f half(60.0f, 30.0f);
    constexpr float pad = SOL::Hologram::HUD_GLOW_PAD_PX;
    const FLinearColor color(0.3f, 0.7f, 1.0f, 1.0f);
    constexpr float edgeIntensity = 0.8f;
    constexpr float fill = 0.25f;

    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Hologram::HUD_MAX_SHAPES);
    TestTrue(TEXT("panel added"), batch.AddPanel(center, half, color, edgeIntensity, fill, pad));
    TestEqual(TEXT("one shape"), batch.GetShapeCount(), 1);
    HudShapesCheckIndices(*this, batch, 0);
    const FColor rgb = (color * edgeIntensity).ToFColorSRGB();
    HudShapesCheckParams(*this, batch, 0, half.X, ESOLHudShapeKind::Panel, half.Y, rgb,
        FMath::RoundToInt(255.0f * fill), 1);
    HudShapesCheckCorners(*this, batch, 0, half + FVector2f(pad, pad));
    for (const FSlateVertex& vertex : batch.GetVertices())
    {
        const FVector2f offset = HudShapesPos(vertex) - center;
        const FVector2f local = HudShapesLocal(vertex);
        TestEqual(TEXT("local.x = pos.x - centre.x"), local.X, offset.X, HUDSHAPES_POS_TOLERANCE);
        TestEqual(TEXT("local.y = pos.y - centre.y"), local.Y, offset.Y, HUDSHAPES_POS_TOLERANCE);
    }

    // The fill clamps to [0, 1]
    TestTrue(TEXT("over-full panel added"), batch.AddPanel(center, half, color, edgeIntensity, 1.7f, pad));
    HudShapesCheckParams(*this, batch, 1, half.X, ESOLHudShapeKind::Panel, half.Y, rgb, 255, 1);
    TestTrue(TEXT("negative-fill panel added"), batch.AddPanel(center, half, color, edgeIntensity, -0.2f, pad));
    HudShapesCheckParams(*this, batch, 2, half.X, ESOLHudShapeKind::Panel, half.Y, rgb, 0, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesBatchCapacityTest, "SOLTest.HudShapes.BatchCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A full batch drops (and counts) further shapes without growing its buffers; Reset empties it and keeps the capacity
bool FSOLHudShapesBatchCapacityTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 capacity = 3;
    constexpr float halfWidth = SOL::Hologram::HUD_LINE_HALF_WIDTH_PX;
    constexpr float pad = SOL::Hologram::HUD_GLOW_PAD_PX;
    const FVector2f a(0.0f, 0.0f);
    const FVector2f b(10.0f, 0.0f);

    FSOLHudShapeBatch batch;
    batch.Initialize(capacity);
    const int32 vertexMax = batch.GetVertices().Max();
    const int32 indexMax = batch.GetIndices().Max();
    TestTrue(TEXT("vertices reserved"), vertexMax >= capacity * HUDSHAPES_VERTS_PER_SHAPE);
    TestTrue(TEXT("indices reserved"), indexMax >= capacity * HUDSHAPES_INDICES_PER_SHAPE);
    TestEqual(TEXT("nothing dropped yet"), batch.GetDroppedCount(), 0);

    // One of each kind fills the batch
    TestTrue(TEXT("line fits"), batch.AddLine(a, b, halfWidth, FLinearColor::White, 1.0f, pad));
    TestTrue(TEXT("ring fits"), batch.AddRing(a, 5.0f, halfWidth, FLinearColor::White, 1.0f, pad));
    TestTrue(TEXT("panel fits"), batch.AddPanel(a, FVector2f(4.0f, 3.0f), FLinearColor::White, 1.0f, 0.5f, pad));
    TestEqual(TEXT("full"), batch.GetShapeCount(), capacity);
    TestEqual(TEXT("nothing dropped while filling"), batch.GetDroppedCount(), 0);

    // Past capacity: dropped, counted, nothing added, no reallocation
    TestFalse(TEXT("extra line dropped"), batch.AddLine(a, b, halfWidth, FLinearColor::White, 1.0f, pad));
    TestEqual(TEXT("one dropped"), batch.GetDroppedCount(), 1);
    TestEqual(TEXT("count unchanged"), batch.GetShapeCount(), capacity);
    TestEqual(TEXT("vertex count unchanged"), batch.GetVertices().Num(), capacity * HUDSHAPES_VERTS_PER_SHAPE);
    TestEqual(TEXT("index count unchanged"), batch.GetIndices().Num(), capacity * HUDSHAPES_INDICES_PER_SHAPE);
    TestFalse(TEXT("extra arc dropped"), batch.AddArc(a, 5.0f, halfWidth, 0.0f, 1.0f, FLinearColor::White, 1.0f, pad));
    TestFalse(TEXT("extra panel dropped"), batch.AddPanel(a, FVector2f(4.0f, 3.0f), FLinearColor::White, 1.0f, 0.5f,
        pad));
    TestEqual(TEXT("three dropped"), batch.GetDroppedCount(), 3);
    TestEqual(TEXT("vertex capacity did not grow"), batch.GetVertices().Max(), vertexMax);
    TestEqual(TEXT("index capacity did not grow"), batch.GetIndices().Max(), indexMax);

    // Reset empties and keeps the capacity; the next shape starts again at vertex 0
    batch.Reset();
    TestEqual(TEXT("reset: empty"), batch.GetShapeCount(), 0);
    TestEqual(TEXT("reset: dropped cleared"), batch.GetDroppedCount(), 0);
    TestEqual(TEXT("reset: no vertices"), batch.GetVertices().Num(), 0);
    TestEqual(TEXT("reset: no indices"), batch.GetIndices().Num(), 0);
    TestEqual(TEXT("reset: vertex capacity kept"), batch.GetVertices().Max(), vertexMax);
    TestEqual(TEXT("reset: index capacity kept"), batch.GetIndices().Max(), indexMax);
    TestEqual(TEXT("reset: max shapes kept"), batch.GetMaxShapes(), capacity);
    TestTrue(TEXT("after reset a line fits"), batch.AddLine(a, b, halfWidth, FLinearColor::White, 1.0f, pad));
    HudShapesCheckIndices(*this, batch, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapesBatchColorTest, "SOLTest.HudShapes.BatchColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The vertex colour is sRGB(colour x intensity)
bool FSOLHudShapesBatchColorTest::RunTest(const FString& /*parameters*/)
{
    constexpr float halfWidth = SOL::Hologram::HUD_LINE_HALF_WIDTH_PX;
    constexpr float pad = SOL::Hologram::HUD_GLOW_PAD_PX;
    const FVector2f a(0.0f, 0.0f);
    const FVector2f b(20.0f, 0.0f);
    const FLinearColor red(1.0f, 0.0f, 0.0f, 1.0f);

    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Hologram::HUD_MAX_SHAPES);
    TestTrue(TEXT("full red added"), batch.AddLine(a, b, halfWidth, red, 1.0f, pad));
    TestTrue(TEXT("half red added"), batch.AddLine(a, b, halfWidth, red, 0.5f, pad));
    const TArray<FSlateVertex>& vertices = batch.GetVertices();
    if (!TestEqual(TEXT("two quads"), vertices.Num(), 2 * HUDSHAPES_VERTS_PER_SHAPE))
    {
        return false;
    }
    const FColor full = vertices[0].Color;
    TestEqual(TEXT("full red: R"), static_cast<int32>(full.R), 255);
    TestEqual(TEXT("full red: G"), static_cast<int32>(full.G), 0);
    TestEqual(TEXT("full red: B"), static_cast<int32>(full.B), 0);
    const FColor half = vertices[HUDSHAPES_VERTS_PER_SHAPE].Color;
    TestEqual(TEXT("half red: sRGB R"), static_cast<int32>(half.R),
        static_cast<int32>(FLinearColor(0.5f, 0.0f, 0.0f, 1.0f).ToFColorSRGB().R));
    TestEqual(TEXT("half red: G"), static_cast<int32>(half.G), 0);
    TestEqual(TEXT("half red: B"), static_cast<int32>(half.B), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
