/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloMenuMath.h"
#include "UI/SOLHudShapeMath.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #42 hologram menu pure logic (Docs/SDDs/42-hologram-in-flight-menus.md section 6), written
// against the UI/SOLHoloMenuMath.h contract before the production code existed
namespace
{
    constexpr float HOLOMENUTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float HOLOMENUTEST_INF = std::numeric_limits<float>::infinity();
    constexpr double HOLOMENUTEST_NAN_D = std::numeric_limits<double>::quiet_NaN();

    // Tolerances
    constexpr float HOLOMENUTEST_TOLERANCE = 1.0e-5f;           // Curve values
    constexpr float HOLOMENUTEST_PROGRESS_TOLERANCE = 1.0e-4f;  // Anim progress after a time step
    constexpr float HOLOMENUTEST_HOVER_TOLERANCE = 1.0e-4f;     // HoverGlow against the reference formula
    constexpr float HOLOMENUTEST_PX_TOLERANCE = 1.0e-2f;        // Worked-example distances (SDD 42 section 6.4)
    constexpr float HOLOMENUTEST_ON_LINE_TOLERANCE = 1.0e-3f;   // A point lies on the outline
    constexpr double HOLOMENUTEST_TIME_EPS_S = 1.0e-4;          // Just past a phase end

    // Output guards: entries after the stated capacity, pre-filled with a sentinel that must survive every call
    constexpr int32 HOLOMENUTEST_GUARD = 4;
    constexpr float HOLOMENUTEST_SENTINEL = -98765.25f;
    constexpr int32 HOLOMENUTEST_DOT_COUNT_SENTINEL = 99;

    // Worked example (SDD 42 section 6.4): rect R = (0,0)-(400,300), title step 200, uiScale 1
    constexpr float HOLOMENUTEST_R_TITLE = 200.0f;
    constexpr float HOLOMENUTEST_SQRT2 = 1.41421356f;
    constexpr float HOLOMENUTEST_R_PERIMETER = 1284.0f + 58.0f * HOLOMENUTEST_SQRT2;   // 1366.024
    constexpr float HOLOMENUTEST_GAP_TOTAL_PX = 96.0f;                              // Sum of FRAME_GAPS lengths at 1x
    constexpr int32 HOLOMENUTEST_OUTLINE_N = 11;

    // A reference outline for the on-outline checks: its 11 points and the arc coordinate u at each point
    struct FSOLHoloMenuTestOutline
    {
        FVector2f Points[HOLOMENUTEST_OUTLINE_N];
        float StartU[HOLOMENUTEST_OUTLINE_N + 1];   // StartU[i] = u at P_i; StartU[11] = L
    };

    // Counts failures of a sweep and keeps the first one's description (avoids flooding the log)
    struct FSOLHoloMenuSweepLog
    {
        int32 Failures = 0;
        FString First;

        // Records a failure, keeping the first description
        void Fail(const FString& what)
        {
            if (Failures == 0)
            {
                First = what;
            }
            ++Failures;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the rect (x0, y0)-(x1, y1)
    FBox2f HoloMenuRect(const float x0, const float y0, const float x1, const float y1)
    {
        return FBox2f(FVector2f(x0, y0), FVector2f(x1, y1));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the worked-example rect R
    FBox2f HoloMenuRectR()
    {
        return HoloMenuRect(0.0f, 0.0f, 400.0f, 300.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a reference outline of 11 points with the arc coordinate at each point
    FSOLHoloMenuTestOutline HoloMenuOutlineFrom(const FVector2f (&points)[HOLOMENUTEST_OUTLINE_N])
    {
        FSOLHoloMenuTestOutline outline;
        outline.StartU[0] = 0.0f;
        for (int32 i = 0; i < HOLOMENUTEST_OUTLINE_N; ++i)
        {
            outline.Points[i] = points[i];
            const FVector2f& next = points[(i + 1) % HOLOMENUTEST_OUTLINE_N];
            outline.StartU[i + 1] = outline.StartU[i] + FVector2f::Distance(points[i], next);
        }
        return outline;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the outline of R from the SDD's listed points, with the arc coordinate at each point
    FSOLHoloMenuTestOutline HoloMenuOutlineR()
    {
        const FVector2f points[HOLOMENUTEST_OUTLINE_N] = {
            FVector2f(0.0f, 10.0f), FVector2f(10.0f, 0.0f), FVector2f(200.0f, 0.0f), FVector2f(210.0f, 10.0f),
            FVector2f(386.0f, 10.0f), FVector2f(400.0f, 24.0f), FVector2f(400.0f, 260.0f), FVector2f(390.0f, 270.0f),
            FVector2f(390.0f, 300.0f), FVector2f(14.0f, 300.0f), FVector2f(0.0f, 286.0f) };
        return HoloMenuOutlineFrom(points);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the outline of the small rect (0,0)-(17,32) at uiScale 1 (k = 0.5) from the SDD's listed points
    FSOLHoloMenuTestOutline HoloMenuOutlineSmall()
    {
        const FVector2f points[HOLOMENUTEST_OUTLINE_N] = {
            FVector2f(0.0f, 5.0f), FVector2f(5.0f, 0.0f), FVector2f(5.0f, 0.0f), FVector2f(10.0f, 5.0f),
            FVector2f(10.0f, 5.0f), FVector2f(17.0f, 12.0f), FVector2f(17.0f, 12.0f), FVector2f(12.0f, 17.0f),
            FVector2f(12.0f, 32.0f), FVector2f(7.0f, 32.0f), FVector2f(0.0f, 25.0f) };
        return HoloMenuOutlineFrom(points);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance from a point to the segment a-b
    float HoloMenuDistanceToEdge(const FVector2f& p, const FVector2f& a, const FVector2f& b)
    {
        const FVector2f ab = b - a;
        const float lengthSq = ab.SizeSquared();
        if (lengthSq <= 0.0f)
        {
            return FVector2f::Distance(p, a);
        }
        const float t = FMath::Clamp(FVector2f::DotProduct(p - a, ab) / lengthSq, 0.0f, 1.0f);
        return FVector2f::Distance(p, a + ab * t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance from a point to the nearest edge of the outline
    float HoloMenuDistanceToOutline(const FSOLHoloMenuTestOutline& outline, const FVector2f& p)
    {
        float best = TNumericLimits<float>::Max();
        for (int32 e = 0; e < HOLOMENUTEST_OUTLINE_N; ++e)
        {
            const FVector2f& a = outline.Points[e];
            const FVector2f& b = outline.Points[(e + 1) % HOLOMENUTEST_OUTLINE_N];
            best = FMath::Min(best, HoloMenuDistanceToEdge(p, a, b));
        }
        return best;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the arc coordinate of a point on the outline (the first edge it lies on); false when off the outline
    bool HoloMenuPointU(const FSOLHoloMenuTestOutline& outline, const FVector2f& p, float& outU)
    {
        for (int32 e = 0; e < HOLOMENUTEST_OUTLINE_N; ++e)
        {
            const FVector2f& a = outline.Points[e];
            const FVector2f& b = outline.Points[(e + 1) % HOLOMENUTEST_OUTLINE_N];
            if (HoloMenuDistanceToEdge(p, a, b) < HOLOMENUTEST_ON_LINE_TOLERANCE)
            {
                outU = outline.StartU[e] + FVector2f::Distance(p, a);
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Finds the one edge a piece lies on, running clockwise, and its u range; false when no single edge holds it
    bool HoloMenuPieceU(const FSOLHoloMenuTestOutline& outline, const FSOLHoloSegment& piece, float& outStartU,
        float& outEndU)
    {
        for (int32 e = 0; e < HOLOMENUTEST_OUTLINE_N; ++e)
        {
            const FVector2f& a = outline.Points[e];
            const FVector2f& b = outline.Points[(e + 1) % HOLOMENUTEST_OUTLINE_N];
            const float length = FVector2f::Distance(a, b);
            if (length <= 0.0f || HoloMenuDistanceToEdge(piece.A, a, b) >= HOLOMENUTEST_ON_LINE_TOLERANCE
                || HoloMenuDistanceToEdge(piece.B, a, b) >= HOLOMENUTEST_ON_LINE_TOLERANCE)
            {
                continue;
            }
            const FVector2f dir = (b - a) / length;
            const float tA = FVector2f::DotProduct(piece.A - a, dir);
            const float tB = FVector2f::DotProduct(piece.B - a, dir);
            if (tB > tA)
            {
                outStartU = outline.StartU[e] + tA;
                outEndU = outline.StartU[e] + tB;
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a piece's length
    float HoloMenuLength(const FSOLHoloSegment& piece)
    {
        return FVector2f::Distance(piece.A, piece.B);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the summed length of the first count pieces
    float HoloMenuSumLength(const FSOLHoloSegment* pieces, const int32 count)
    {
        float sum = 0.0f;
        for (int32 i = 0; i < count; ++i)
        {
            sum += HoloMenuLength(pieces[i]);
        }
        return sum;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two points are within a tolerance of each other on both axes
    bool HoloMenuNear(const FVector2f& a, const FVector2f& b, const float tolerance)
    {
        return FMath::Abs(a.X - b.X) <= tolerance && FMath::Abs(a.Y - b.Y) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a point against an expected position
    void HoloMenuTestPoint(FAutomationTestBase& test, const FString& label, const FVector2f& actual,
        const FVector2f& expected, const float tolerance = HOLOMENUTEST_PX_TOLERANCE)
    {
        test.TestEqual(label + TEXT(".x"), actual.X, expected.X, tolerance);
        test.TestEqual(label + TEXT(".y"), actual.Y, expected.Y, tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a piece against its expected ends (A -> B)
    void HoloMenuTestPiece(FAutomationTestBase& test, const FString& label, const FSOLHoloSegment& actual,
        const FVector2f& expectedA, const FVector2f& expectedB, const float tolerance = HOLOMENUTEST_PX_TOLERANCE)
    {
        HoloMenuTestPoint(test, label + TEXT(" A"), actual.A, expectedA, tolerance);
        HoloMenuTestPoint(test, label + TEXT(" B"), actual.B, expectedB, tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills points with the sentinel
    void HoloMenuFillPoints(FVector2f* points, const int32 total)
    {
        for (int32 i = 0; i < total; ++i)
        {
            points[i] = FVector2f(HOLOMENUTEST_SENTINEL, HOLOMENUTEST_SENTINEL);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills pieces with the sentinel
    void HoloMenuFillPieces(FSOLHoloSegment* pieces, const int32 total)
    {
        for (int32 i = 0; i < total; ++i)
        {
            pieces[i].A = FVector2f(HOLOMENUTEST_SENTINEL, HOLOMENUTEST_SENTINEL);
            pieces[i].B = FVector2f(HOLOMENUTEST_SENTINEL, HOLOMENUTEST_SENTINEL);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a point still holds the sentinel
    bool HoloMenuIsSentinel(const FVector2f& p)
    {
        return p.X == HOLOMENUTEST_SENTINEL && p.Y == HOLOMENUTEST_SENTINEL;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the guard points after the capacity still hold the sentinel
    bool HoloMenuPointGuardsIntact(const FVector2f* points, const int32 capacity)
    {
        for (int32 i = capacity; i < capacity + HOLOMENUTEST_GUARD; ++i)
        {
            if (!HoloMenuIsSentinel(points[i]))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the guard pieces after the capacity still hold the sentinel
    bool HoloMenuPieceGuardsIntact(const FSOLHoloSegment* pieces, const int32 capacity)
    {
        for (int32 i = capacity; i < capacity + HOLOMENUTEST_GUARD; ++i)
        {
            if (!HoloMenuIsSentinel(pieces[i].A) || !HoloMenuIsSentinel(pieces[i].B))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns HoverGlow computed straight from the SDD 42 section 6.1 formula and the SOL::HoloMenu constants
    float HoloMenuReferenceHoverGlow(const double sinceS, const float s)
    {
        using namespace SOL::HoloMenu;
        if (sinceS < 0.0 || !(s > 0.0f))
        {
            return 0.0f;
        }
        const float strength = FMath::Min(s, 2.0f);
        const float ramp = FMath::SmoothStep(0.0f, static_cast<float>(HOVER_RAMP_S), static_cast<float>(sinceS));
        float flicker = 1.0f;
        if (sinceS < static_cast<double>(HOVER_FLICKER_S))
        {
            const float noiseAt = static_cast<float>(sinceS * static_cast<double>(HOVER_FLICKER_RATE_HZ)
                + static_cast<double>(HOVER_FLICKER_OFFSET));
            flicker = 1.0f - static_cast<float>(HOVER_FLICKER_DEPTH) * strength * SOLHudShapeMath::SmoothNoise(noiseAt);
        }
        return static_cast<float>(HOVER_GLOW_INTENSITY) * ramp * flicker;
    }

    //////////////////////////////////////////////////////////////////////////
    // Opens an anim fully at strength 1 (BeginOpen at startS, Advance past OPEN_S); returns the time it became Open
    double HoloMenuMakeOpen(FSOLHoloMenuAnim& anim, const double startS)
    {
        SOLHoloMenu::BeginOpen(anim, startS, 1.0f);
        const double openAtS = startS + static_cast<double>(SOL::HoloMenu::OPEN_S) + HOLOMENUTEST_TIME_EPS_S;
        SOLHoloMenu::Advance(anim, openAtS, 1.0f);
        return openAtS;
    }
}

// --- Curves (SDD 42 section 6.1) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuOpenProgressTest, "SOLTest.HoloMenu.OpenProgress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// OpenProgress is the clamped elapsed fraction, 1 for a zero duration, 0 before the start, instant at strength 0
bool FSOLHoloMenuOpenProgressTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::OpenProgress;
    const float openS = static_cast<float>(SOL::HoloMenu::OPEN_S);
    const double openD = static_cast<double>(openS);
    TestEqual(TEXT("0 at the start"), OpenProgress(0.0, openS, 1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("half at half the duration"), OpenProgress(0.5 * openD, openS, 1.0f), 0.5f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("a quarter at a quarter"), OpenProgress(0.25 * openD, openS, 1.0f), 0.25f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("full at the duration"), OpenProgress(openD, openS, 1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("clamped beyond the duration"), OpenProgress(10.0 * openD, openS, 1.0f), 1.0f,
        HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("negative elapsed gives 0"), OpenProgress(-0.1, openS, 1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("zero duration gives 1"), OpenProgress(0.05, 0.0f, 1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("negative duration gives 1"), OpenProgress(0.05, -1.0f, 1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("strength 2 does not change the timing"), OpenProgress(0.5 * openD, openS, 2.0f), 0.5f,
        HOLOMENUTEST_TOLERANCE);

    // Strength 0 (or below, or NaN, which reads as 0) is instant (D18)
    TestEqual(TEXT("s = 0 is instant at the start"), OpenProgress(0.0, openS, 0.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("s = 0 is instant mid-way"), OpenProgress(0.5 * openD, openS, 0.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("s < 0 is instant"), OpenProgress(0.0, openS, -1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("s NaN reads as 0: instant"), OpenProgress(0.0, openS, HOLOMENUTEST_NAN), 1.0f,
        HOLOMENUTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuDrawInFillTest, "SOLTest.HoloMenu.FrameDrawInFillFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FrameDrawIn ends at LINE_PHASE_END and FillFade starts at FILL_PHASE_START; both are smoothsteps in [0, 1]
bool FSOLHoloMenuDrawInFillTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::FrameDrawIn;
    using SOLHoloMenu::FillFade;
    const float lineEnd = static_cast<float>(SOL::HoloMenu::LINE_PHASE_END);
    const float fillStart = static_cast<float>(SOL::HoloMenu::FILL_PHASE_START);
    TestTrue(TEXT("the two phases overlap (FILL_PHASE_START < LINE_PHASE_END)"), fillStart < lineEnd);
    TestTrue(TEXT("LINE_PHASE_END inside (0, 1]"), lineEnd > 0.0f && lineEnd <= 1.0f);
    TestTrue(TEXT("FILL_PHASE_START inside [0, 1)"), fillStart >= 0.0f && fillStart < 1.0f);

    // End values and thresholds
    TestEqual(TEXT("FrameDrawIn(0) = 0"), FrameDrawIn(0.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FrameDrawIn(LINE_PHASE_END) = 1"), FrameDrawIn(lineEnd), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FrameDrawIn(1) = 1"), FrameDrawIn(1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FrameDrawIn mid-phase is 0.5"), FrameDrawIn(0.5f * lineEnd), 0.5f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FrameDrawIn smoothstep at a quarter"), FrameDrawIn(0.25f * lineEnd),
        FMath::SmoothStep(0.0f, lineEnd, 0.25f * lineEnd), HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FrameDrawIn below 0 is 0"), FrameDrawIn(-1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FrameDrawIn above 1 is 1"), FrameDrawIn(2.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FillFade(0) = 0"), FillFade(0.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FillFade(FILL_PHASE_START) = 0"), FillFade(fillStart), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FillFade(1) = 1"), FillFade(1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FillFade mid-phase is 0.5"), FillFade(0.5f * (fillStart + 1.0f)), 0.5f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FillFade smoothstep at three quarters"), FillFade(fillStart + 0.75f * (1.0f - fillStart)),
        FMath::SmoothStep(fillStart, 1.0f, fillStart + 0.75f * (1.0f - fillStart)), HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FillFade below 0 is 0"), FillFade(-1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("FillFade above 1 is 1"), FillFade(2.0f), 1.0f, HOLOMENUTEST_TOLERANCE);

    // Monotonic over 101 samples, in [0, 1], flat outside the phases
    float previousDraw = FrameDrawIn(0.0f);
    float previousFill = FillFade(0.0f);
    bool bDrawMonotonic = true;
    bool bFillMonotonic = true;
    bool bInRange = true;
    bool bDrawFlatAfter = true;
    bool bFillFlatBefore = true;
    for (int32 i = 0; i <= 100; ++i)
    {
        const float p = static_cast<float>(i) / 100.0f;
        const float draw = FrameDrawIn(p);
        const float fill = FillFade(p);
        bDrawMonotonic &= draw >= previousDraw - HOLOMENUTEST_TOLERANCE;
        bFillMonotonic &= fill >= previousFill - HOLOMENUTEST_TOLERANCE;
        bInRange &= draw >= 0.0f && draw <= 1.0f && fill >= 0.0f && fill <= 1.0f;
        if (p >= lineEnd)
        {
            bDrawFlatAfter &= FMath::IsNearlyEqual(draw, 1.0f, HOLOMENUTEST_TOLERANCE);
        }
        if (p <= fillStart)
        {
            bFillFlatBefore &= FMath::IsNearlyEqual(fill, 0.0f, HOLOMENUTEST_TOLERANCE);
        }
        previousDraw = draw;
        previousFill = fill;
    }
    TestTrue(TEXT("FrameDrawIn monotonic"), bDrawMonotonic);
    TestTrue(TEXT("FillFade monotonic"), bFillMonotonic);
    TestTrue(TEXT("both in [0, 1]"), bInRange);
    TestTrue(TEXT("FrameDrawIn is 1 from LINE_PHASE_END on"), bDrawFlatAfter);
    TestTrue(TEXT("FillFade is 0 up to FILL_PHASE_START"), bFillFlatBefore);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuHoverGlowTest, "SOLTest.HoloMenu.HoverGlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// HoverGlow ramps and flickers, settles at exactly HOVER_GLOW_INTENSITY, stays in bounds and is 0 at strength 0
bool FSOLHoloMenuHoverGlowTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::HoverGlow;
    const float intensity = static_cast<float>(SOL::HoloMenu::HOVER_GLOW_INTENSITY);
    const double rampS = static_cast<double>(SOL::HoloMenu::HOVER_RAMP_S);
    const double flickerS = static_cast<double>(SOL::HoloMenu::HOVER_FLICKER_S);
    const double settledS = FMath::Max(rampS, flickerS);

    // Negative time and the start of the ramp
    TestEqual(TEXT("negative time is 0"), HoverGlow(-0.01, 1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("negative time is 0 at s = 2"), HoverGlow(-1.0, 2.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("0 at the hover start (ramp at 0)"), HoverGlow(0.0, 1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);

    // Ramp end: the ramp is 1, the flicker may still be running
    TestEqual(TEXT("ramp end matches the formula"), HoverGlow(rampS, 1.0f), HoloMenuReferenceHoverGlow(rampS, 1.0f),
        HOLOMENUTEST_HOVER_TOLERANCE);
    TestTrue(TEXT("ramp end within the flicker depth"), HoverGlow(rampS, 1.0f)
        >= intensity * (1.0f - static_cast<float>(SOL::HoloMenu::HOVER_FLICKER_DEPTH)) - HOLOMENUTEST_HOVER_TOLERANCE);

    // Exact intensity once both windows are over
    const float strengths[] = { 0.25f, 0.5f, 1.0f, 2.0f };
    const double settledTimes[] = { settledS, settledS + 0.001, settledS + 0.5, 10.0, 1.0e4 };
    for (const float s : strengths)
    {
        for (const double t : settledTimes)
        {
            TestEqual(FString::Printf(TEXT("exact intensity at s %.2f, t %.4f"), s, t), HoverGlow(t, s), intensity,
                1.0e-6f);
        }
    }

    // Strength 0, below 0 and NaN give 0 at every time (D18)
    bool bZeroAtS0 = true;
    for (int32 i = -10; i <= 400; ++i)
    {
        const double t = static_cast<double>(i) * 0.001;
        bZeroAtS0 &= HoverGlow(t, 0.0f) == 0.0f && HoverGlow(t, -1.0f) == 0.0f
            && HoverGlow(t, HOLOMENUTEST_NAN) == 0.0f;
    }
    TestTrue(TEXT("s = 0 (and < 0, NaN) gives 0 at every time"), bZeroAtS0);

    // Bounds and the formula over a sample sweep at s = 0, 1, 2
    const float sweepStrengths[] = { 0.0f, 1.0f, 2.0f };
    for (const float s : sweepStrengths)
    {
        bool bInBounds = true;
        bool bMatches = true;
        for (int32 i = -10; i <= 400; ++i)
        {
            const double t = static_cast<double>(i) * 0.001;
            const float glow = HoverGlow(t, s);
            bInBounds &= glow >= 0.0f && glow <= intensity + HOLOMENUTEST_TOLERANCE;
            bMatches &= FMath::IsNearlyEqual(glow, HoloMenuReferenceHoverGlow(t, s), HOLOMENUTEST_HOVER_TOLERANCE);
        }
        TestTrue(FString::Printf(TEXT("in [0, HOVER_GLOW_INTENSITY] at s %.0f"), s), bInBounds);
        TestTrue(FString::Printf(TEXT("matches the formula at s %.0f"), s), bMatches);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuSweepTest, "SOLTest.HoloMenu.Sweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SweepFraction wraps any age into [0, 1); SweepIntensity is IDLE_SWEEP_INTENSITY per unit of strength
bool FSOLHoloMenuSweepTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::SweepFraction;
    using SOLHoloMenu::SweepIntensity;
    const double periodS = static_cast<double>(SOL::HoloMenu::IDLE_SWEEP_PERIOD_S);
    const double wrapS = static_cast<double>(SOL::Hologram::HUD_TIME_WRAP_S);
    TestEqual(TEXT("0 at age 0 (the top)"), SweepFraction(0.0), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("a quarter period"), SweepFraction(0.25 * periodS), 0.25f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("half a period"), SweepFraction(0.5 * periodS), 0.5f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("one and a half periods"), SweepFraction(1.5 * periodS), 0.5f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("the time wrap: age wrapS + half a period"), SweepFraction(wrapS + 0.5 * periodS),
        SweepFraction(FMath::Fmod(wrapS + 0.5 * periodS, wrapS)), HOLOMENUTEST_TOLERANCE);

    // Always in [0, 1), including large and negative ages
    const double ages[] = { 0.0, 1.0e-9, 0.999 * periodS, periodS, 3.999, 999.999, 1000.0, 1001.0, 12345.678,
        1.0e7, 1.0e9, 1.0e12, -1.0e-9, -1.0e-6, -0.5, -1.0, -periodS, -1000.5, -1.0e7, -1.0e12 };
    for (const double age : ages)
    {
        const float fraction = SweepFraction(age);
        TestTrue(FString::Printf(TEXT("SweepFraction(%g) in [0, 1)"), age), fraction >= 0.0f && fraction < 1.0f);
    }

    // Strength scaling
    const float perUnit = static_cast<float>(SOL::HoloMenu::IDLE_SWEEP_INTENSITY);
    TestEqual(TEXT("SweepIntensity(0) = 0"), SweepIntensity(0.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("SweepIntensity(1) = IDLE_SWEEP_INTENSITY"), SweepIntensity(1.0f), perUnit, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("SweepIntensity(2) = 2 x IDLE_SWEEP_INTENSITY"), SweepIntensity(2.0f), 2.0f * perUnit,
        HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("SweepIntensity(NaN) reads as 0"), SweepIntensity(HOLOMENUTEST_NAN), 0.0f, HOLOMENUTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuBackdropTest, "SOLTest.HoloMenu.BackdropAndGlowScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BackdropAlpha is a flat panel at 0, the faint tint at 1, continuous, clamped; GlowScale is clamp(s, 0, 1)
bool FSOLHoloMenuBackdropTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::BackdropAlpha;
    using SOLHoloMenu::GlowScale;
    const float flat = static_cast<float>(SOL::HoloMenu::FLAT_PANEL_ALPHA);
    const float perUnit = static_cast<float>(SOL::HoloMenu::BACKDROP_ALPHA_PER_UNIT);
    TestEqual(TEXT("FLAT_PANEL_ALPHA at s = 0, fill 1"), BackdropAlpha(0.0f, 1.0f), flat, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("BACKDROP_ALPHA_PER_UNIT at s = 1, fill 1"), BackdropAlpha(1.0f, 1.0f), perUnit,
        HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("lerp at s = 0.5"), BackdropAlpha(0.5f, 1.0f), FMath::Lerp(flat, perUnit, 0.5f),
        HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("PER_UNIT x s at s = 1.5"), BackdropAlpha(1.5f, 1.0f), 1.5f * perUnit, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("PER_UNIT x 2 at s = 2"), BackdropAlpha(2.0f, 1.0f), 2.0f * perUnit, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("scales with the fill"), BackdropAlpha(1.0f, 0.5f), 0.5f * perUnit, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("0 at fill 0 (s = 0)"), BackdropAlpha(0.0f, 0.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("0 at fill 0 (s = 1)"), BackdropAlpha(1.0f, 0.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("0 at fill 0 (s = 2)"), BackdropAlpha(2.0f, 0.0f), 0.0f, HOLOMENUTEST_TOLERANCE);

    // Continuous across s = 1
    TestTrue(TEXT("continuous across s = 1"),
        FMath::Abs(BackdropAlpha(1.0f - 1.0e-4f, 1.0f) - BackdropAlpha(1.0f + 1.0e-4f, 1.0f)) < 1.0e-3f);

    // Clamping of fillAlpha and s (NaN s reads as 0)
    TestEqual(TEXT("fill above 1 clamps"), BackdropAlpha(1.0f, 2.0f), BackdropAlpha(1.0f, 1.0f),
        HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("fill below 0 clamps"), BackdropAlpha(1.0f, -1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("s above 2 clamps"), BackdropAlpha(3.0f, 1.0f), BackdropAlpha(2.0f, 1.0f), HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("s below 0 clamps"), BackdropAlpha(-1.0f, 1.0f), flat, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("s NaN reads as 0"), BackdropAlpha(HOLOMENUTEST_NAN, 1.0f), flat, HOLOMENUTEST_TOLERANCE);

    // Never above max(FLAT_PANEL_ALPHA, 2 x PER_UNIT), never below 0
    const float ceiling = FMath::Max(flat, 2.0f * perUnit);
    bool bBounded = true;
    for (int32 si = -10; si <= 30; ++si)
    {
        for (int32 fi = -5; fi <= 20; ++fi)
        {
            const float alpha = BackdropAlpha(static_cast<float>(si) * 0.1f, static_cast<float>(fi) * 0.1f);
            bBounded &= alpha >= 0.0f && alpha <= ceiling + HOLOMENUTEST_TOLERANCE;
        }
    }
    TestTrue(TEXT("in [0, max(FLAT_PANEL_ALPHA, 2 x BACKDROP_ALPHA_PER_UNIT)]"), bBounded);

    // GlowScale
    TestEqual(TEXT("GlowScale(0) = 0"), GlowScale(0.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("GlowScale(0.5) = 0.5"), GlowScale(0.5f), 0.5f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("GlowScale(1) = 1"), GlowScale(1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("GlowScale(2) = 1"), GlowScale(2.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("GlowScale(-1) = 0"), GlowScale(-1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("GlowScale(NaN) reads as 0"), GlowScale(HOLOMENUTEST_NAN), 0.0f, HOLOMENUTEST_TOLERANCE);
    return true;
}

// --- Anim state (SDD 42 section 6.2) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuAnimOpenCloseTest, "SOLTest.HoloMenu.AnimOpenClose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A menu opens to Open after OPEN_S and closes to Closed after CLOSE_S, its progress the elapsed fraction
bool FSOLHoloMenuAnimOpenCloseTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    const double openS = static_cast<double>(SOL::HoloMenu::OPEN_S);
    const double closeS = static_cast<double>(SOL::HoloMenu::CLOSE_S);
    FSOLHoloMenuAnim anim;
    TestTrue(TEXT("default is Closed"), anim.Phase == ESOLHoloMenuPhase::Closed);
    TestEqual(TEXT("default hover is none"), anim.HoveredItem, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Closed reads 0"), Progress(anim, 5.0, 1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);

    // Open
    const double t0 = 10.0;
    TestTrue(TEXT("BeginOpen from Closed returns true"), BeginOpen(anim, t0, 1.0f));
    TestTrue(TEXT("Opening after BeginOpen"), anim.Phase == ESOLHoloMenuPhase::Opening);
    TestEqual(TEXT("Opening starts from 0"), Progress(anim, t0, 1.0f), 0.0f, HOLOMENUTEST_PROGRESS_TOLERANCE);
    TestEqual(TEXT("half open at OPEN_S / 2"), Progress(anim, t0 + 0.5 * openS, 1.0f), 0.5f,
        HOLOMENUTEST_PROGRESS_TOLERANCE);
    TestEqual(TEXT("Opening clamps to 1"), Progress(anim, t0 + 10.0 * openS, 1.0f), 1.0f,
        HOLOMENUTEST_PROGRESS_TOLERANCE);
    Advance(anim, t0 + 0.5 * openS, 1.0f);
    TestTrue(TEXT("still Opening mid-way"), anim.Phase == ESOLHoloMenuPhase::Opening);
    Advance(anim, t0 + openS + HOLOMENUTEST_TIME_EPS_S, 1.0f);
    TestTrue(TEXT("Open after OPEN_S"), anim.Phase == ESOLHoloMenuPhase::Open);
    TestEqual(TEXT("Open reads 1"), Progress(anim, t0 + openS + HOLOMENUTEST_TIME_EPS_S, 1.0f), 1.0f,
        HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("Open reads 1 much later"), Progress(anim, t0 + 1000.0, 1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);

    // Close
    const double t1 = 20.0;
    TestTrue(TEXT("BeginClose from Open returns true"), BeginClose(anim, t1, 1.0f));
    TestTrue(TEXT("Closing after BeginClose"), anim.Phase == ESOLHoloMenuPhase::Closing);
    TestEqual(TEXT("Closing starts from 1"), Progress(anim, t1, 1.0f), 1.0f, HOLOMENUTEST_PROGRESS_TOLERANCE);
    TestEqual(TEXT("half closed at CLOSE_S / 2"), Progress(anim, t1 + 0.5 * closeS, 1.0f), 0.5f,
        HOLOMENUTEST_PROGRESS_TOLERANCE);
    TestEqual(TEXT("Closing clamps to 0"), Progress(anim, t1 + 10.0 * closeS, 1.0f), 0.0f,
        HOLOMENUTEST_PROGRESS_TOLERANCE);
    Advance(anim, t1 + 0.5 * closeS, 1.0f);
    TestTrue(TEXT("still Closing mid-way"), anim.Phase == ESOLHoloMenuPhase::Closing);
    Advance(anim, t1 + closeS + HOLOMENUTEST_TIME_EPS_S, 1.0f);
    TestTrue(TEXT("Closed after CLOSE_S"), anim.Phase == ESOLHoloMenuPhase::Closed);
    TestEqual(TEXT("Closed reads 0 again"), Progress(anim, t1 + closeS + HOLOMENUTEST_TIME_EPS_S, 1.0f), 0.0f,
        HOLOMENUTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuAnimReverseTest, "SOLTest.HoloMenu.AnimReverse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Reopening during a close and closing during an open resume from the current progress, with no jump
bool FSOLHoloMenuAnimReverseTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    const double openS = static_cast<double>(SOL::HoloMenu::OPEN_S);
    const double closeS = static_cast<double>(SOL::HoloMenu::CLOSE_S);

    // Reopen a quarter of the way into a close: from 0.75 back up
    {
        FSOLHoloMenuAnim anim;
        HoloMenuMakeOpen(anim, 0.0);
        const double closeAt = 20.0;
        BeginClose(anim, closeAt, 1.0f);
        const double reopenAt = closeAt + 0.25 * closeS;
        const float before = Progress(anim, reopenAt, 1.0f);
        TestEqual(TEXT("a quarter into the close reads 0.75"), before, 0.75f, HOLOMENUTEST_PROGRESS_TOLERANCE);
        TestTrue(TEXT("BeginOpen from Closing returns true"), BeginOpen(anim, reopenAt, 1.0f));
        TestTrue(TEXT("Opening after the reversal"), anim.Phase == ESOLHoloMenuPhase::Opening);
        TestEqual(TEXT("no jump at the reversal"), Progress(anim, reopenAt, 1.0f), before,
            HOLOMENUTEST_PROGRESS_TOLERANCE);
        TestEqual(TEXT("PhaseStartProgress holds the reversal point"), anim.PhaseStartProgress, before,
            HOLOMENUTEST_PROGRESS_TOLERANCE);
        TestEqual(TEXT("rises at the OPEN_S rate from there"), Progress(anim, reopenAt + 0.1 * openS, 1.0f),
            before + 0.1f, HOLOMENUTEST_PROGRESS_TOLERANCE);
        Advance(anim, reopenAt + 0.1 * openS, 1.0f);
        TestTrue(TEXT("still Opening before the rest of the way"), anim.Phase == ESOLHoloMenuPhase::Opening);
        Advance(anim, reopenAt + 0.25 * openS + HOLOMENUTEST_TIME_EPS_S, 1.0f);
        TestTrue(TEXT("Open after the remaining quarter of OPEN_S"), anim.Phase == ESOLHoloMenuPhase::Open);
    }

    // Close a quarter of the way into an open: from 0.25 back down
    {
        FSOLHoloMenuAnim anim;
        const double openAt = 5.0;
        BeginOpen(anim, openAt, 1.0f);
        const double closeAt = openAt + 0.25 * openS;
        const float before = Progress(anim, closeAt, 1.0f);
        TestEqual(TEXT("a quarter into the open reads 0.25"), before, 0.25f, HOLOMENUTEST_PROGRESS_TOLERANCE);
        TestTrue(TEXT("BeginClose from Opening returns true"), BeginClose(anim, closeAt, 1.0f));
        TestTrue(TEXT("Closing after the reversal"), anim.Phase == ESOLHoloMenuPhase::Closing);
        TestEqual(TEXT("no jump at the close reversal"), Progress(anim, closeAt, 1.0f), before,
            HOLOMENUTEST_PROGRESS_TOLERANCE);
        TestEqual(TEXT("PhaseStartProgress holds the close reversal point"), anim.PhaseStartProgress, before,
            HOLOMENUTEST_PROGRESS_TOLERANCE);
        TestEqual(TEXT("falls at the CLOSE_S rate from there"), Progress(anim, closeAt + 0.1 * closeS, 1.0f),
            before - 0.1f, HOLOMENUTEST_PROGRESS_TOLERANCE);
        Advance(anim, closeAt + 0.1 * closeS, 1.0f);
        TestTrue(TEXT("still Closing"), anim.Phase == ESOLHoloMenuPhase::Closing);
        Advance(anim, closeAt + 0.25 * closeS + HOLOMENUTEST_TIME_EPS_S, 1.0f);
        TestTrue(TEXT("Closed after the remaining quarter of CLOSE_S"), anim.Phase == ESOLHoloMenuPhase::Closed);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuAnimBeginTwiceTest, "SOLTest.HoloMenu.AnimBeginTwice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BeginOpen / BeginClose report a transition once; a repeat leaves the state alone
bool FSOLHoloMenuAnimBeginTwiceTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    FSOLHoloMenuAnim anim;
    TestTrue(TEXT("first BeginOpen returns true"), BeginOpen(anim, 1.0, 1.0f));
    const double startS = anim.PhaseStartS;
    TestFalse(TEXT("second BeginOpen returns false"), BeginOpen(anim, 1.05, 1.0f));
    TestTrue(TEXT("still Opening"), anim.Phase == ESOLHoloMenuPhase::Opening);
    TestEqual(TEXT("PhaseStartS unchanged by the repeat"), anim.PhaseStartS, startS);
    TestEqual(TEXT("PhaseStartS is the BeginOpen time"), anim.PhaseStartS, 1.0);

    HoloMenuMakeOpen(anim, 2.0);
    TestTrue(TEXT("Open"), anim.Phase == ESOLHoloMenuPhase::Open);
    TestFalse(TEXT("BeginOpen on Open returns false"), BeginOpen(anim, 3.0, 1.0f));
    TestTrue(TEXT("still Open"), anim.Phase == ESOLHoloMenuPhase::Open);

    FSOLHoloMenuAnim closed;
    TestFalse(TEXT("BeginClose on Closed returns false"), BeginClose(closed, 1.0, 1.0f));
    TestTrue(TEXT("still Closed"), closed.Phase == ESOLHoloMenuPhase::Closed);

    TestTrue(TEXT("first BeginClose on Open returns true"), BeginClose(anim, 4.0, 1.0f));
    const double closeStartS = anim.PhaseStartS;
    TestFalse(TEXT("second BeginClose returns false"), BeginClose(anim, 4.05, 1.0f));
    TestTrue(TEXT("still Closing"), anim.Phase == ESOLHoloMenuPhase::Closing);
    TestEqual(TEXT("close PhaseStartS unchanged by the repeat"), anim.PhaseStartS, closeStartS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuAnimAdvanceTest, "SOLTest.HoloMenu.AnimAdvance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Advance is idempotent, only moves Opening -> Open and Closing -> Closed, and never steps backwards
bool FSOLHoloMenuAnimAdvanceTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    const double openS = static_cast<double>(SOL::HoloMenu::OPEN_S);
    const double closeS = static_cast<double>(SOL::HoloMenu::CLOSE_S);

    FSOLHoloMenuAnim closed;
    Advance(closed, 100.0, 1.0f);
    TestTrue(TEXT("Closed stays Closed"), closed.Phase == ESOLHoloMenuPhase::Closed);

    FSOLHoloMenuAnim anim;
    BeginOpen(anim, 0.0, 1.0f);
    for (int32 i = 0; i < 3; ++i)
    {
        Advance(anim, 0.5 * openS, 1.0f);
    }
    TestTrue(TEXT("repeated mid-way Advance stays Opening"), anim.Phase == ESOLHoloMenuPhase::Opening);
    TestEqual(TEXT("repeated Advance keeps PhaseStartS"), anim.PhaseStartS, 0.0);
    TestEqual(TEXT("repeated Advance keeps the progress"), Progress(anim, 0.5 * openS, 1.0f), 0.5f,
        HOLOMENUTEST_PROGRESS_TOLERANCE);

    const double openAt = openS + HOLOMENUTEST_TIME_EPS_S;
    Advance(anim, openAt, 1.0f);
    Advance(anim, openAt, 1.0f);
    TestTrue(TEXT("Open after repeated Advance"), anim.Phase == ESOLHoloMenuPhase::Open);
    Advance(anim, 0.0, 1.0f);
    TestTrue(TEXT("Advance at an earlier time does not step Open back"), anim.Phase == ESOLHoloMenuPhase::Open);
    Advance(anim, 1000.0, 1.0f);
    TestTrue(TEXT("Open stays Open"), anim.Phase == ESOLHoloMenuPhase::Open);

    const double closeAt = 1000.0;
    BeginClose(anim, closeAt, 1.0f);
    Advance(anim, closeAt + 0.5 * closeS, 1.0f);
    Advance(anim, closeAt + 0.5 * closeS, 1.0f);
    TestTrue(TEXT("repeated mid-way Advance stays Closing"), anim.Phase == ESOLHoloMenuPhase::Closing);
    Advance(anim, closeAt + closeS + HOLOMENUTEST_TIME_EPS_S, 1.0f);
    Advance(anim, closeAt + closeS + HOLOMENUTEST_TIME_EPS_S, 1.0f);
    TestTrue(TEXT("Closed after repeated Advance"), anim.Phase == ESOLHoloMenuPhase::Closed);
    Advance(anim, closeAt, 1.0f);
    TestTrue(TEXT("Advance at an earlier time does not step Closed back"), anim.Phase == ESOLHoloMenuPhase::Closed);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuAnimStrengthZeroTest, "SOLTest.HoloMenu.AnimStrengthZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At strength 0 Opening reads 1 and Closing 0, so the next Advance reaches Open / Closed at once (D18)
bool FSOLHoloMenuAnimStrengthZeroTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    FSOLHoloMenuAnim anim;
    TestTrue(TEXT("BeginOpen at s = 0 returns true"), BeginOpen(anim, 5.0, 0.0f));
    TestEqual(TEXT("Opening reads 1 at s = 0"), Progress(anim, 5.0, 0.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
    Advance(anim, 5.0, 0.0f);
    TestTrue(TEXT("Open on the next Advance at s = 0"), anim.Phase == ESOLHoloMenuPhase::Open);

    TestTrue(TEXT("BeginClose at s = 0 returns true"), BeginClose(anim, 6.0, 0.0f));
    TestEqual(TEXT("Closing reads 0 at s = 0"), Progress(anim, 6.0, 0.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
    Advance(anim, 6.0, 0.0f);
    TestTrue(TEXT("Closed on the next Advance at s = 0"), anim.Phase == ESOLHoloMenuPhase::Closed);

    // A strength-0 open (progress 1) closed at strength 1 closes from 1, with no jump
    FSOLHoloMenuAnim mixed;
    BeginOpen(mixed, 1.0, 0.0f);
    Advance(mixed, 1.0, 0.0f);
    BeginClose(mixed, 2.0, 1.0f);
    TestEqual(TEXT("close at s = 1 after an instant open starts from 1"), Progress(mixed, 2.0, 1.0f), 1.0f,
        HOLOMENUTEST_PROGRESS_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuAnimHoverClearedTest, "SOLTest.HoloMenu.AnimHoverCleared",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Opening and closing a menu clear its hovered item
bool FSOLHoloMenuAnimHoverClearedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    FSOLHoloMenuAnim anim;
    SetHovered(anim, 3, 0.5);
    TestEqual(TEXT("hover set while Closed"), anim.HoveredItem, 3);
    BeginOpen(anim, 1.0, 1.0f);
    TestEqual(TEXT("BeginOpen clears the hover"), anim.HoveredItem, static_cast<int32>(INDEX_NONE));

    const double openAt = HoloMenuMakeOpen(anim, 1.0);
    SetHovered(anim, 4, openAt);
    TestEqual(TEXT("hover set while Open"), anim.HoveredItem, 4);
    BeginClose(anim, openAt + 1.0, 1.0f);
    TestEqual(TEXT("BeginClose clears the hover"), anim.HoveredItem, static_cast<int32>(INDEX_NONE));

    // A reopen during the close clears a hover set while closing
    SetHovered(anim, 5, openAt + 1.01);
    BeginOpen(anim, openAt + 1.02, 1.0f);
    TestEqual(TEXT("BeginOpen from Closing clears the hover"), anim.HoveredItem, static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuHoverCueTest, "SOLTest.HoloMenu.HoverCue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetHovered reports a new real item; the hover cue plays only when Open and at most every HOVER_CUE_MIN_INTERVAL_S
bool FSOLHoloMenuHoverCueTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    const double minIntervalS = static_cast<double>(SOL::HoloMenu::HOVER_CUE_MIN_INTERVAL_S);

    // SetHovered
    {
        FSOLHoloMenuAnim anim;
        anim.Phase = ESOLHoloMenuPhase::Open;
        TestFalse(TEXT("INDEX_NONE over no hover: same item, false"), SetHovered(anim, INDEX_NONE, 1.0));
        TestEqual(TEXT("same item: HoverStartS unchanged"), anim.HoverStartS, 0.0);
        TestTrue(TEXT("a new item returns true"), SetHovered(anim, 2, 1.0));
        TestEqual(TEXT("HoveredItem set"), anim.HoveredItem, 2);
        TestEqual(TEXT("HoverStartS set"), anim.HoverStartS, 1.0);
        TestFalse(TEXT("the same item again returns false"), SetHovered(anim, 2, 1.5));
        TestEqual(TEXT("same item keeps HoverStartS"), anim.HoverStartS, 1.0);
        TestTrue(TEXT("another item returns true"), SetHovered(anim, 7, 1.6));
        TestEqual(TEXT("HoveredItem moved"), anim.HoveredItem, 7);
        TestEqual(TEXT("HoverStartS moved"), anim.HoverStartS, 1.6);
        TestFalse(TEXT("leaving to INDEX_NONE returns false"), SetHovered(anim, INDEX_NONE, 1.7));
        TestEqual(TEXT("HoveredItem cleared"), anim.HoveredItem, static_cast<int32>(INDEX_NONE));
        TestEqual(TEXT("HoverStartS set on leaving"), anim.HoverStartS, 1.7);
    }

    // Cue on a new item, none for the same item or for INDEX_NONE
    {
        FSOLHoloMenuAnim anim;
        anim.Phase = ESOLHoloMenuPhase::Open;
        bool bChanged = SetHovered(anim, 2, 1.0);
        TestTrue(TEXT("new item cue"), ShouldPlayHoverCue(anim, bChanged, 1.0));
        TestEqual(TEXT("cue time recorded"), anim.LastHoverCueS, 1.0);
        bChanged = SetHovered(anim, 2, 2.0);
        TestFalse(TEXT("same item no cue"), ShouldPlayHoverCue(anim, bChanged, 2.0));
        TestEqual(TEXT("no cue keeps the cue time"), anim.LastHoverCueS, 1.0);
        bChanged = SetHovered(anim, INDEX_NONE, 3.0);
        TestFalse(TEXT("INDEX_NONE no cue"), ShouldPlayHoverCue(anim, bChanged, 3.0));
        TestEqual(TEXT("INDEX_NONE keeps the cue time"), anim.LastHoverCueS, 1.0);
    }

    // No cue unless Open (no cue for the highlight a menu opens with)
    {
        const ESOLHoloMenuPhase phases[] = { ESOLHoloMenuPhase::Closed, ESOLHoloMenuPhase::Opening,
            ESOLHoloMenuPhase::Closing };
        for (const ESOLHoloMenuPhase phase : phases)
        {
            FSOLHoloMenuAnim anim;
            anim.Phase = phase;
            const double lastBefore = anim.LastHoverCueS;
            TestFalse(FString::Printf(TEXT("no cue in phase %d"), static_cast<int32>(phase)),
                ShouldPlayHoverCue(anim, true, 1.0));
            TestEqual(FString::Printf(TEXT("cue time untouched in phase %d"), static_cast<int32>(phase)),
                anim.LastHoverCueS, lastBefore);
        }
        FSOLHoloMenuAnim anim;
        BeginOpen(anim, 0.0, 1.0f);
        const bool bChanged = SetHovered(anim, 1, 0.01);
        TestFalse(TEXT("no cue while Opening (real open)"), ShouldPlayHoverCue(anim, bChanged, 0.01));
    }

    // Rate limit at HOVER_CUE_MIN_INTERVAL_S
    {
        FSOLHoloMenuAnim anim;
        anim.Phase = ESOLHoloMenuPhase::Open;
        TestTrue(TEXT("first cue plays"), ShouldPlayHoverCue(anim, true, 0.0));
        TestEqual(TEXT("first cue time"), anim.LastHoverCueS, 0.0);
        TestFalse(TEXT("cue inside the interval is dropped"), ShouldPlayHoverCue(anim, true, 0.5 * minIntervalS));
        TestEqual(TEXT("a dropped cue keeps the cue time"), anim.LastHoverCueS, 0.0);
        TestTrue(TEXT("cue exactly at the interval plays"), ShouldPlayHoverCue(anim, true, minIntervalS));
        TestEqual(TEXT("cue time moved"), anim.LastHoverCueS, minIntervalS);
        TestFalse(TEXT("cue half an interval later is dropped"), ShouldPlayHoverCue(anim, true, 1.5 * minIntervalS));
        TestTrue(TEXT("cue past the interval plays"), ShouldPlayHoverCue(anim, true, 2.0 * minIntervalS + 1.0e-6));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuEvaluateTest, "SOLTest.HoloMenu.Evaluate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Evaluate composes the look from the progress, the curves, the hover glow, the sweep and the backdrop
bool FSOLHoloMenuEvaluateTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    const double openS = static_cast<double>(SOL::HoloMenu::OPEN_S);

    // Closed: invisible, all zero
    {
        FSOLHoloMenuAnim anim;
        const FSOLHoloMenuLook look = Evaluate(anim, 3.0, 1.0f);
        TestFalse(TEXT("Closed is invisible"), look.bVisible);
        TestEqual(TEXT("Closed progress 0"), look.Progress, 0.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("Closed frame 0"), look.FrameFraction, 0.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("Closed fill 0"), look.FillAlpha, 0.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("Closed hover 0"), look.HoverIntensity, 0.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("Closed sweep intensity 0"), look.SweepIntensity, 0.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("Closed backdrop 0"), look.BackdropAlpha, 0.0f, HOLOMENUTEST_TOLERANCE);
    }

    // Opening mid-way, hovered, at s = 1
    {
        FSOLHoloMenuAnim anim;
        BeginOpen(anim, 0.0, 1.0f);
        SetHovered(anim, 1, 0.0);
        const double nowS = 0.5 * openS;
        const FSOLHoloMenuLook look = Evaluate(anim, nowS, 1.0f);
        const float p = Progress(anim, nowS, 1.0f);
        TestTrue(TEXT("Opening is visible"), look.bVisible);
        TestEqual(TEXT("progress"), look.Progress, p, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("progress is 0.5"), look.Progress, 0.5f, HOLOMENUTEST_PROGRESS_TOLERANCE);
        TestEqual(TEXT("FrameFraction = FrameDrawIn(p)"), look.FrameFraction, FrameDrawIn(p), HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("FillAlpha = FillFade(p)"), look.FillAlpha, FillFade(p), HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("HoverIntensity = HoverGlow(now - HoverStartS)"), look.HoverIntensity,
            HoverGlow(nowS - anim.HoverStartS, 1.0f), HOLOMENUTEST_TOLERANCE);
        TestTrue(TEXT("SweepFraction in [0, 1)"), look.SweepFraction >= 0.0f && look.SweepFraction < 1.0f);
        TestEqual(TEXT("SweepIntensity x FillAlpha"), look.SweepIntensity, SweepIntensity(1.0f) * FillFade(p),
            HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("BackdropAlpha(s, FillAlpha)"), look.BackdropAlpha, BackdropAlpha(1.0f, FillFade(p)),
            HOLOMENUTEST_TOLERANCE);
    }

    // Open, hovered long ago, at s = 2: full look, settled hover glow
    {
        FSOLHoloMenuAnim anim;
        const double openAt = HoloMenuMakeOpen(anim, 0.0);
        SetHovered(anim, 2, openAt);
        const FSOLHoloMenuLook look = Evaluate(anim, 100.0, 2.0f);
        TestTrue(TEXT("Open is visible"), look.bVisible);
        TestEqual(TEXT("Open progress 1"), look.Progress, 1.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("Open frame 1"), look.FrameFraction, 1.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("Open fill 1"), look.FillAlpha, 1.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("settled hover"), look.HoverIntensity, static_cast<float>(SOL::HoloMenu::HOVER_GLOW_INTENSITY),
            HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("sweep at s = 2"), look.SweepIntensity, SweepIntensity(2.0f), HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("backdrop at s = 2"), look.BackdropAlpha, BackdropAlpha(2.0f, 1.0f), HOLOMENUTEST_TOLERANCE);
    }

    // No hovered item: no hover glow
    {
        FSOLHoloMenuAnim anim;
        HoloMenuMakeOpen(anim, 0.0);
        const FSOLHoloMenuLook look = Evaluate(anim, 50.0, 1.0f);
        TestEqual(TEXT("no hover, no glow"), look.HoverIntensity, 0.0f, HOLOMENUTEST_TOLERANCE);
    }

    // Strength 0 while Opening: full at once, flat panel, no glow or sweep (D18)
    {
        FSOLHoloMenuAnim anim;
        BeginOpen(anim, 0.0, 0.0f);
        SetHovered(anim, 1, 0.0);
        const FSOLHoloMenuLook look = Evaluate(anim, 0.01, 0.0f);
        TestTrue(TEXT("s = 0 visible"), look.bVisible);
        TestEqual(TEXT("s = 0 progress 1"), look.Progress, 1.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("s = 0 frame 1"), look.FrameFraction, 1.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("s = 0 fill 1"), look.FillAlpha, 1.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("s = 0 hover 0"), look.HoverIntensity, 0.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("s = 0 sweep 0"), look.SweepIntensity, 0.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(TEXT("s = 0 flat panel"), look.BackdropAlpha, static_cast<float>(SOL::HoloMenu::FLAT_PANEL_ALPHA),
            HOLOMENUTEST_TOLERANCE);
    }

    // Closing is still visible
    {
        FSOLHoloMenuAnim anim;
        const double openAt = HoloMenuMakeOpen(anim, 0.0);
        BeginClose(anim, openAt + 1.0, 1.0f);
        const FSOLHoloMenuLook look = Evaluate(anim, openAt + 1.0, 1.0f);
        TestTrue(TEXT("Closing is visible"), look.bVisible);
        TestEqual(TEXT("Closing starts at progress 1"), look.Progress, 1.0f, HOLOMENUTEST_PROGRESS_TOLERANCE);
    }
    return true;
}

// --- Frame geometry (SDD 42 section 6.3) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuFrameOutlineTest, "SOLTest.HoloMenu.FrameOutline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The stepped outline's 11 points and perimeter on the worked rects, title step clamping, small and invalid rects
bool FSOLHoloMenuFrameOutlineTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::FRAME_OUTLINE_POINTS;
    TestEqual(TEXT("FRAME_OUTLINE_POINTS is 11"), CAPACITY, HOLOMENUTEST_OUTLINE_N);
    FVector2f points[CAPACITY + HOLOMENUTEST_GUARD];
    const FBox2f rectR = HoloMenuRectR();

    // R: the 11 points of SDD 42 section 6.4 case 8
    HoloMenuFillPoints(points, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("R gives 11 points"), FrameOutline(rectR, HOLOMENUTEST_R_TITLE, 1.0f, points), CAPACITY);
    TestTrue(TEXT("R: no write past the capacity"), HoloMenuPointGuardsIntact(points, CAPACITY));
    const FSOLHoloMenuTestOutline expectedR = HoloMenuOutlineR();
    for (int32 i = 0; i < HOLOMENUTEST_OUTLINE_N; ++i)
    {
        HoloMenuTestPoint(*this, FString::Printf(TEXT("R P%d"), i), points[i], expectedR.Points[i]);
    }

    // R: perimeter, equal to the sum of the point-to-point distances, independent of the title step
    const float perimeterR = FramePerimeter(rectR, HOLOMENUTEST_R_TITLE, 1.0f);
    TestEqual(TEXT("R: L = 1284 + 58 sqrt(2)"), perimeterR, HOLOMENUTEST_R_PERIMETER, HOLOMENUTEST_PX_TOLERANCE);
    TestEqual(TEXT("R: L = 1366.024"), perimeterR, 1366.024f, HOLOMENUTEST_PX_TOLERANCE);
    float sumR = 0.0f;
    for (int32 i = 0; i < HOLOMENUTEST_OUTLINE_N; ++i)
    {
        sumR += FVector2f::Distance(points[i], points[(i + 1) % HOLOMENUTEST_OUTLINE_N]);
    }
    TestEqual(TEXT("R: L equals the sum of the point distances"), perimeterR, sumR, HOLOMENUTEST_PX_TOLERANCE);
    TestEqual(TEXT("R: L with title step 50"), FramePerimeter(rectR, 50.0f, 1.0f), perimeterR,
        HOLOMENUTEST_PX_TOLERANCE);
    TestEqual(TEXT("R: L with title step 300"), FramePerimeter(rectR, 300.0f, 1.0f), perimeterR,
        HOLOMENUTEST_PX_TOLERANCE);

    // R at uiScale 2: k = 2
    TestEqual(TEXT("R at uiScale 2: L = 1332.049"), FramePerimeter(rectR, HOLOMENUTEST_R_TITLE, 2.0f), 1332.049f,
        HOLOMENUTEST_PX_TOLERANCE);
    HoloMenuFillPoints(points, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("R at uiScale 2 gives 11 points"), FrameOutline(rectR, HOLOMENUTEST_R_TITLE, 2.0f, points),
        CAPACITY);
    HoloMenuTestPoint(*this, TEXT("R at uiScale 2 P0 (a = 20)"), points[0], FVector2f(0.0f, 20.0f));
    float sumR2 = 0.0f;
    for (int32 i = 0; i < HOLOMENUTEST_OUTLINE_N; ++i)
    {
        sumR2 += FVector2f::Distance(points[i], points[(i + 1) % HOLOMENUTEST_OUTLINE_N]);
    }
    TestEqual(TEXT("R at uiScale 2: L equals the sum of the point distances"), sumR2, 1332.049f,
        HOLOMENUTEST_PX_TOLERANCE);

    // Title step clamping: 0 and NaN read a (P1 == P2); 1000 clamps to w - s - c
    const float lowTitles[] = { 0.0f, HOLOMENUTEST_NAN };
    for (const float title : lowTitles)
    {
        HoloMenuFillPoints(points, CAPACITY + HOLOMENUTEST_GUARD);
        TestEqual(FString::Printf(TEXT("title %f: 11 points"), title), FrameOutline(rectR, title, 1.0f, points),
            CAPACITY);
        HoloMenuTestPoint(*this, FString::Printf(TEXT("title %f: P1 == P2"), title), points[2], points[1]);
        HoloMenuTestPoint(*this, FString::Printf(TEXT("title %f: P2 at (a, 0)"), title), points[2],
            FVector2f(10.0f, 0.0f));
    }
    HoloMenuFillPoints(points, CAPACITY + HOLOMENUTEST_GUARD);
    FrameOutline(rectR, 1000.0f, 1.0f, points);
    TestEqual(TEXT("title 1000: P2.X = w - s - c = 376"), points[2].X, 376.0f, HOLOMENUTEST_PX_TOLERANCE);

    // Small rect (0,0)-(17,32): k = 0.5, still 11 points, coinciding points allowed
    {
        const FBox2f small = HoloMenuRect(0.0f, 0.0f, 17.0f, 32.0f);
        const FVector2f expected[HOLOMENUTEST_OUTLINE_N] = {
            FVector2f(0.0f, 5.0f), FVector2f(5.0f, 0.0f), FVector2f(5.0f, 0.0f), FVector2f(10.0f, 5.0f),
            FVector2f(10.0f, 5.0f), FVector2f(17.0f, 12.0f), FVector2f(17.0f, 12.0f), FVector2f(12.0f, 17.0f),
            FVector2f(12.0f, 32.0f), FVector2f(7.0f, 32.0f), FVector2f(0.0f, 25.0f) };
        HoloMenuFillPoints(points, CAPACITY + HOLOMENUTEST_GUARD);
        TestEqual(TEXT("small rect: 11 points"), FrameOutline(small, HOLOMENUTEST_R_TITLE, 1.0f, points), CAPACITY);
        for (int32 i = 0; i < HOLOMENUTEST_OUTLINE_N; ++i)
        {
            HoloMenuTestPoint(*this, FString::Printf(TEXT("small P%d"), i), points[i], expected[i]);
        }
        TestEqual(TEXT("small rect: L = 81.012"), FramePerimeter(small, HOLOMENUTEST_R_TITLE, 1.0f), 81.012f,
            HOLOMENUTEST_PX_TOLERANCE);
    }

    // Invalid rects: 0 points and L = 0
    const FBox2f invalidRects[] = {
        HoloMenuRect(0.0f, 0.0f, 0.0f, 300.0f),             // Zero width
        HoloMenuRect(0.0f, 0.0f, 400.0f, 0.0f),             // Zero height
        HoloMenuRect(400.0f, 0.0f, 0.0f, 300.0f),           // Negative width
        HoloMenuRect(0.0f, 300.0f, 400.0f, 0.0f),           // Negative height
        HoloMenuRect(HOLOMENUTEST_NAN, 0.0f, 400.0f, 300.0f),
        HoloMenuRect(0.0f, HOLOMENUTEST_NAN, 400.0f, 300.0f),
        HoloMenuRect(0.0f, 0.0f, HOLOMENUTEST_NAN, 300.0f),
        HoloMenuRect(0.0f, 0.0f, 400.0f, HOLOMENUTEST_NAN),
        HoloMenuRect(-HOLOMENUTEST_INF, 0.0f, 400.0f, 300.0f),
        HoloMenuRect(0.0f, 0.0f, HOLOMENUTEST_INF, 300.0f),
        HoloMenuRect(0.0f, 0.0f, 400.0f, HOLOMENUTEST_INF) };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(invalidRects)); ++i)
    {
        HoloMenuFillPoints(points, CAPACITY + HOLOMENUTEST_GUARD);
        TestEqual(FString::Printf(TEXT("invalid rect %d: 0 points"), i),
            FrameOutline(invalidRects[i], HOLOMENUTEST_R_TITLE, 1.0f, points), 0);
        TestEqual(FString::Printf(TEXT("invalid rect %d: L = 0"), i),
            FramePerimeter(invalidRects[i], HOLOMENUTEST_R_TITLE, 1.0f), 0.0f);
        TestTrue(FString::Printf(TEXT("invalid rect %d: no write past the capacity"), i),
            HoloMenuPointGuardsIntact(points, CAPACITY));
    }

    // uiScale 0, -1, NaN and infinity behave as 1
    const float badScales[] = { 0.0f, -1.0f, HOLOMENUTEST_NAN, HOLOMENUTEST_INF };
    for (const float scale : badScales)
    {
        HoloMenuFillPoints(points, CAPACITY + HOLOMENUTEST_GUARD);
        TestEqual(FString::Printf(TEXT("uiScale %f: 11 points"), scale),
            FrameOutline(rectR, HOLOMENUTEST_R_TITLE, scale, points), CAPACITY);
        for (int32 i = 0; i < HOLOMENUTEST_OUTLINE_N; ++i)
        {
            HoloMenuTestPoint(*this, FString::Printf(TEXT("uiScale %f P%d"), scale, i), points[i],
                expectedR.Points[i]);
        }
        TestEqual(FString::Printf(TEXT("uiScale %f: L as at 1"), scale),
            FramePerimeter(rectR, HOLOMENUTEST_R_TITLE, scale), perimeterR, HOLOMENUTEST_PX_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuFrameDrawSegmentsTest, "SOLTest.HoloMenu.FrameDrawSegments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The visible outline's pieces on R: worked counts and lengths, and the ordering / on-outline rules over a sweep
bool FSOLHoloMenuFrameDrawSegmentsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::FRAME_MAX_SEGMENTS;
    TestEqual(TEXT("FRAME_MAX_SEGMENTS is 24"), CAPACITY, 24);
    TestEqual(TEXT("FRAME_GAP_COUNT is 7"), static_cast<int32>(SOL::HoloMenu::FRAME_GAP_COUNT), 7);
    FSOLHoloSegment pieces[CAPACITY + HOLOMENUTEST_GUARD];
    const FBox2f rectR = HoloMenuRectR();
    const FSOLHoloMenuTestOutline outline = HoloMenuOutlineR();
    const float perimeter = HOLOMENUTEST_R_PERIMETER;

    // Worked counts and summed lengths
    struct FSOLHoloMenuDrawCase
    {
        float Fraction;
        int32 Count;
        float Length;
    };
    const FSOLHoloMenuDrawCase cases[] = {
        { 1.0f, 19, 1270.024f }, { 0.5f, 9, 661.012f }, { 0.25f, 4, 329.506f } };
    for (const FSOLHoloMenuDrawCase& drawCase : cases)
    {
        HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
        const int32 count = FrameDrawSegments(rectR, HOLOMENUTEST_R_TITLE, 1.0f, drawCase.Fraction, pieces);
        TestEqual(FString::Printf(TEXT("f = %.2f: count"), drawCase.Fraction), count, drawCase.Count);
        TestEqual(FString::Printf(TEXT("f = %.2f: summed length"), drawCase.Fraction),
            HoloMenuSumLength(pieces, FMath::Clamp(count, 0, CAPACITY)), drawCase.Length, HOLOMENUTEST_PX_TOLERANCE);
        TestTrue(FString::Printf(TEXT("f = %.2f: no write past the capacity"), drawCase.Fraction),
            HoloMenuPieceGuardsIntact(pieces, CAPACITY));
    }
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    const int32 fullCount = FrameDrawSegments(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, pieces);
    TestEqual(TEXT("f = 1: L minus the gaps"), HoloMenuSumLength(pieces, FMath::Clamp(fullCount, 0, CAPACITY)),
        perimeter - HOLOMENUTEST_GAP_TOTAL_PX, HOLOMENUTEST_PX_TOLERANCE);

    // f = 1: the strokes meet at u = L / 2, (390, 288.787) on the inset right edge; a piece ends and one starts there
    {
        const FVector2f meet(390.0f, 288.787f);
        bool bEndsAtMeet = false;
        bool bStartsAtMeet = false;
        for (int32 i = 0; i < FMath::Clamp(fullCount, 0, CAPACITY); ++i)
        {
            bEndsAtMeet |= HoloMenuNear(pieces[i].B, meet, HOLOMENUTEST_PX_TOLERANCE);
            bStartsAtMeet |= HoloMenuNear(pieces[i].A, meet, HOLOMENUTEST_PX_TOLERANCE);
        }
        TestTrue(TEXT("f = 1: a piece ends at the stroke meeting point"), bEndsAtMeet);
        TestTrue(TEXT("f = 1: a piece starts at the stroke meeting point"), bStartsAtMeet);
    }

    // f = 0.5: the clockwise stroke front at u = L / 4 (333.222, 10), the other front at u = 3L / 4 (59.707, 300)
    {
        HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
        const int32 count = FMath::Clamp(FrameDrawSegments(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 0.5f, pieces), 0,
            CAPACITY);
        bool bClockwiseFront = false;
        bool bCounterFront = false;
        for (int32 i = 0; i < count; ++i)
        {
            bClockwiseFront |= HoloMenuNear(pieces[i].B, FVector2f(333.222f, 10.0f), HOLOMENUTEST_PX_TOLERANCE);
            bCounterFront |= HoloMenuNear(pieces[i].A, FVector2f(59.707f, 300.0f), HOLOMENUTEST_PX_TOLERANCE);
        }
        TestTrue(TEXT("f = 0.5: clockwise stroke ends at u = L / 4"), bClockwiseFront);
        TestTrue(TEXT("f = 0.5: counter-clockwise stroke front at u = 3L / 4"), bCounterFront);
        if (count > 0)
        {
            HoloMenuTestPoint(*this, TEXT("f = 0.5: last piece ends at P0"), pieces[count - 1].B,
                FVector2f(0.0f, 10.0f));
        }
    }

    // Nothing at f <= 0, NaN or an invalid rect
    const float emptyFractions[] = { 0.0f, -0.5f, -1.0f, HOLOMENUTEST_NAN };
    for (const float fraction : emptyFractions)
    {
        HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
        TestEqual(FString::Printf(TEXT("f = %f: 0 pieces"), fraction),
            FrameDrawSegments(rectR, HOLOMENUTEST_R_TITLE, 1.0f, fraction, pieces), 0);
    }
    TestEqual(TEXT("invalid rect: 0 pieces"),
        FrameDrawSegments(HoloMenuRect(0.0f, 0.0f, 0.0f, 300.0f), HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, pieces), 0);
    TestEqual(TEXT("NaN rect: 0 pieces"),
        FrameDrawSegments(HoloMenuRect(0.0f, 0.0f, HOLOMENUTEST_NAN, 300.0f), HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, pieces),
        0);

    // f = 1.5 equals f = 1
    {
        FSOLHoloSegment over[CAPACITY + HOLOMENUTEST_GUARD];
        HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
        HoloMenuFillPieces(over, CAPACITY + HOLOMENUTEST_GUARD);
        const int32 countOne = FrameDrawSegments(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, pieces);
        const int32 countOver = FrameDrawSegments(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 1.5f, over);
        TestEqual(TEXT("f = 1.5: same count as f = 1"), countOver, countOne);
        for (int32 i = 0; i < FMath::Clamp(FMath::Min(countOne, countOver), 0, CAPACITY); ++i)
        {
            HoloMenuTestPiece(*this, FString::Printf(TEXT("f = 1.5 piece %d"), i), over[i], pieces[i].A, pieces[i].B,
                HOLOMENUTEST_ON_LINE_TOLERANCE);
        }
    }

    // Sweep f in 0.05 steps: capacity, piece length, on one outline edge, clockwise, ordered by u without overlap,
    // first piece from P0 toward P1, summed length non-decreasing and within [f L - gaps, f L]
    float previousSum = 0.0f;
    for (int32 step = 1; step <= 20; ++step)
    {
        const float f = static_cast<float>(step) / 20.0f;
        HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
        const int32 count = FrameDrawSegments(rectR, HOLOMENUTEST_R_TITLE, 1.0f, f, pieces);
        const FString at = FString::Printf(TEXT("f = %.2f"), f);
        TestTrue(at + TEXT(": count within [1, FRAME_MAX_SEGMENTS]"), count >= 1 && count <= CAPACITY);
        TestTrue(at + TEXT(": no write past the capacity"), HoloMenuPieceGuardsIntact(pieces, CAPACITY));
        const int32 safeCount = FMath::Clamp(count, 0, CAPACITY);
        if (safeCount == 0)
        {
            continue;
        }
        HoloMenuTestPoint(*this, at + TEXT(": first piece starts at P0"), pieces[0].A, FVector2f(0.0f, 10.0f),
            HOLOMENUTEST_ON_LINE_TOLERANCE);
        const FVector2f firstDir = (pieces[0].B - pieces[0].A).GetSafeNormal();
        HoloMenuTestPoint(*this, at + TEXT(": first piece runs toward P1"), firstDir,
            FVector2f(1.0f, -1.0f) / HOLOMENUTEST_SQRT2, HOLOMENUTEST_ON_LINE_TOLERANCE);

        bool bLongEnough = true;
        bool bOnOneEdge = true;
        bool bOrdered = true;
        float previousEndU = -1.0f;
        float previousStartU = -1.0f;
        for (int32 i = 0; i < safeCount; ++i)
        {
            bLongEnough &= HoloMenuLength(pieces[i]) >= SOL::HoloMenu::MIN_PIECE_PX;
            float startU = 0.0f;
            float endU = 0.0f;
            if (!HoloMenuPieceU(outline, pieces[i], startU, endU))
            {
                bOnOneEdge = false;
                continue;
            }
            bOrdered &= startU > previousStartU && startU >= previousEndU - HOLOMENUTEST_ON_LINE_TOLERANCE;
            previousStartU = startU;
            previousEndU = endU;
        }
        TestTrue(at + TEXT(": every piece at least 1e-3 px"), bLongEnough);
        TestTrue(at + TEXT(": every piece on one outline edge, running clockwise"), bOnOneEdge);
        TestTrue(at + TEXT(": ordered by u, no overlap"), bOrdered);

        const float sum = HoloMenuSumLength(pieces, safeCount);
        TestTrue(at + TEXT(": summed length non-decreasing"), sum >= previousSum - HOLOMENUTEST_ON_LINE_TOLERANCE);
        TestTrue(at + TEXT(": summed length at most f L"), sum <= f * perimeter + HOLOMENUTEST_PX_TOLERANCE);
        TestTrue(at + TEXT(": summed length at least f L minus the gaps"),
            sum >= f * perimeter - HOLOMENUTEST_GAP_TOTAL_PX - HOLOMENUTEST_PX_TOLERANCE);
        previousSum = sum;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuFrameGapDotsTest, "SOLTest.HoloMenu.FrameGapDots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Gap end dots on R: worked positions and counts, on the outline, pairing with the pieces to make up L; merging
bool FSOLHoloMenuFrameGapDotsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::FRAME_MAX_DOTS;
    TestEqual(TEXT("FRAME_MAX_DOTS is 14"), CAPACITY, 14);
    FVector2f dots[CAPACITY + HOLOMENUTEST_GUARD];
    const FBox2f rectR = HoloMenuRectR();
    const FSOLHoloMenuTestOutline outline = HoloMenuOutlineR();

    // f = 1: 14 dots, first and last pairs
    HoloMenuFillPoints(dots, CAPACITY + HOLOMENUTEST_GUARD);
    const int32 fullCount = FrameGapDots(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, dots);
    TestEqual(TEXT("f = 1: 2 x FRAME_GAP_COUNT dots"), fullCount,
        2 * static_cast<int32>(SOL::HoloMenu::FRAME_GAP_COUNT));
    TestEqual(TEXT("f = 1: 14 dots"), fullCount, 14);
    TestTrue(TEXT("f = 1: no write past the capacity"), HoloMenuPointGuardsIntact(dots, CAPACITY));
    if (fullCount == 14)
    {
        HoloMenuTestPoint(*this, TEXT("dot 0"), dots[0], FVector2f(264.921f, 10.0f));
        HoloMenuTestPoint(*this, TEXT("dot 1"), dots[1], FVector2f(274.921f, 10.0f));
        HoloMenuTestPoint(*this, TEXT("dot 12"), dots[12], FVector2f(0.0f, 146.602f));
        HoloMenuTestPoint(*this, TEXT("dot 13"), dots[13], FVector2f(0.0f, 134.602f));
    }

    // Every dot on the outline, ordered by u; pairs of dots plus the visible pieces make up L
    {
        const int32 safeCount = FMath::Clamp(fullCount, 0, CAPACITY);
        bool bOnOutline = true;
        bool bOrdered = true;
        float previousU = -1.0f;
        for (int32 i = 0; i < safeCount; ++i)
        {
            bOnOutline &= HoloMenuDistanceToOutline(outline, dots[i]) < HOLOMENUTEST_ON_LINE_TOLERANCE;
            float u = 0.0f;
            if (HoloMenuPointU(outline, dots[i], u))
            {
                bOrdered &= u >= previousU - HOLOMENUTEST_ON_LINE_TOLERANCE;
                previousU = u;
            }
        }
        TestTrue(TEXT("f = 1: every dot on the outline"), bOnOutline);
        TestTrue(TEXT("f = 1: dots ordered by u"), bOrdered);

        float gapSum = 0.0f;
        for (int32 i = 0; i + 1 < safeCount; i += 2)
        {
            gapSum += FVector2f::Distance(dots[i], dots[i + 1]);
        }
        TestEqual(TEXT("f = 1: gap pairs sum to the gap total"), gapSum, HOLOMENUTEST_GAP_TOTAL_PX,
            HOLOMENUTEST_PX_TOLERANCE);
        FSOLHoloSegment pieces[SOL::HoloMenu::FRAME_MAX_SEGMENTS + HOLOMENUTEST_GUARD];
        HoloMenuFillPieces(pieces, SOL::HoloMenu::FRAME_MAX_SEGMENTS + HOLOMENUTEST_GUARD);
        const int32 pieceCount = FMath::Clamp(FrameDrawSegments(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, pieces), 0,
            static_cast<int32>(SOL::HoloMenu::FRAME_MAX_SEGMENTS));
        TestEqual(TEXT("f = 1: pieces plus gaps equal L"), HoloMenuSumLength(pieces, pieceCount) + gapSum,
            HOLOMENUTEST_R_PERIMETER, HOLOMENUTEST_PX_TOLERANCE);
    }

    // f = 0.5: 4 dots; f = 0.25: 2 dots, the last gap's on the left edge
    HoloMenuFillPoints(dots, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("f = 0.5: 4 dots"), FrameGapDots(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 0.5f, dots), 4);
    HoloMenuFillPoints(dots, CAPACITY + HOLOMENUTEST_GUARD);
    const int32 quarterCount = FrameGapDots(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 0.25f, dots);
    TestEqual(TEXT("f = 0.25: 2 dots"), quarterCount, 2);
    if (quarterCount == 2)
    {
        HoloMenuTestPoint(*this, TEXT("f = 0.25 dot 0"), dots[0], FVector2f(0.0f, 146.602f));
        HoloMenuTestPoint(*this, TEXT("f = 0.25 dot 1"), dots[1], FVector2f(0.0f, 134.602f));
    }

    // Nothing at f <= 0, NaN or an invalid rect
    TestEqual(TEXT("f = 0: 0 dots"), FrameGapDots(rectR, HOLOMENUTEST_R_TITLE, 1.0f, 0.0f, dots), 0);
    TestEqual(TEXT("f < 0: 0 dots"), FrameGapDots(rectR, HOLOMENUTEST_R_TITLE, 1.0f, -1.0f, dots), 0);
    TestEqual(TEXT("f NaN: 0 dots"), FrameGapDots(rectR, HOLOMENUTEST_R_TITLE, 1.0f, HOLOMENUTEST_NAN, dots), 0);
    TestEqual(TEXT("invalid rect: 0 dots"),
        FrameGapDots(HoloMenuRect(0.0f, 0.0f, 400.0f, -1.0f), HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, dots), 0);

    // Small rect (0,0)-(17,32): gaps merge, so an even count below 14
    HoloMenuFillPoints(dots, CAPACITY + HOLOMENUTEST_GUARD);
    const int32 smallCount = FrameGapDots(HoloMenuRect(0.0f, 0.0f, 17.0f, 32.0f), HOLOMENUTEST_R_TITLE, 1.0f, 1.0f,
        dots);
    TestTrue(TEXT("small rect: even count"), smallCount % 2 == 0);
    TestTrue(TEXT("small rect: at most 14"), smallCount <= CAPACITY);
    TestTrue(TEXT("small rect: below 14 (gaps merged)"), smallCount < 14);
    TestTrue(TEXT("small rect: no write past the capacity"), HoloMenuPointGuardsIntact(dots, CAPACITY));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuFrameRulerTicksTest, "SOLTest.HoloMenu.FrameRulerTicks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The left-edge ruler: 6 ticks on R, 3 on a short rect, capped at the capacity, 0 for an invalid rect
bool FSOLHoloMenuFrameRulerTicksTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::FRAME_RULER_TICK_COUNT;
    TestEqual(TEXT("FRAME_RULER_TICK_COUNT is 6"), CAPACITY, 6);
    FSOLHoloSegment ticks[CAPACITY + HOLOMENUTEST_GUARD];

    HoloMenuFillPieces(ticks, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("R: 6 ticks"), FrameRulerTicks(HoloMenuRectR(), 1.0f, ticks), 6);
    TestTrue(TEXT("R: no write past the capacity"), HoloMenuPieceGuardsIntact(ticks, CAPACITY));
    for (int32 i = 0; i < 6; ++i)
    {
        const float y = 150.0f + 9.0f * static_cast<float>(i);
        HoloMenuTestPiece(*this, FString::Printf(TEXT("R tick %d"), i), ticks[i], FVector2f(0.0f, y),
            FVector2f(4.0f, y));
    }

    HoloMenuFillPieces(ticks, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("(0,0)-(40,70): 3 ticks"), FrameRulerTicks(HoloMenuRect(0.0f, 0.0f, 40.0f, 70.0f), 1.0f, ticks), 3);
    const float shortYs[] = { 35.0f, 44.0f, 53.0f };
    for (int32 i = 0; i < 3; ++i)
    {
        HoloMenuTestPiece(*this, FString::Printf(TEXT("short tick %d"), i), ticks[i], FVector2f(0.0f, shortYs[i]),
            FVector2f(4.0f, shortYs[i]));
    }

    HoloMenuFillPieces(ticks, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("tall rect: capped at the capacity"),
        FrameRulerTicks(HoloMenuRect(0.0f, 0.0f, 400.0f, 3000.0f), 1.0f, ticks), CAPACITY);
    TestTrue(TEXT("tall rect: no write past the capacity"), HoloMenuPieceGuardsIntact(ticks, CAPACITY));

    TestEqual(TEXT("invalid rect: 0 ticks"), FrameRulerTicks(HoloMenuRect(0.0f, 0.0f, 0.0f, 300.0f), 1.0f, ticks), 0);
    TestEqual(TEXT("NaN rect: 0 ticks"),
        FrameRulerTicks(HoloMenuRect(HOLOMENUTEST_NAN, 0.0f, 40.0f, 70.0f), 1.0f, ticks), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuFrameAccentsTest, "SOLTest.HoloMenu.FrameAccents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The four accents on R, only the corner accents with no title, 0 for an invalid rect
bool FSOLHoloMenuFrameAccentsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::FRAME_ACCENT_COUNT;
    TestEqual(TEXT("FRAME_ACCENT_COUNT is 4"), CAPACITY, 4);
    FSOLHoloSegment accents[CAPACITY + HOLOMENUTEST_GUARD];

    HoloMenuFillPieces(accents, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("R: 4 accents"), FrameAccents(HoloMenuRectR(), HOLOMENUTEST_R_TITLE, 1.0f, accents), 4);
    TestTrue(TEXT("R: no write past the capacity"), HoloMenuPieceGuardsIntact(accents, CAPACITY));
    HoloMenuTestPiece(*this, TEXT("top-left accent"), accents[0], FVector2f(6.0f, 18.0f), FVector2f(6.0f, 70.0f));
    HoloMenuTestPiece(*this, TEXT("bottom-right accent"), accents[1], FVector2f(384.0f, 294.0f),
        FVector2f(310.0f, 294.0f));
    HoloMenuTestPiece(*this, TEXT("title underline"), accents[2], FVector2f(12.0f, 34.0f), FVector2f(160.0f, 34.0f));
    HoloMenuTestPiece(*this, TEXT("title dash"), accents[3], FVector2f(168.0f, 34.0f), FVector2f(180.0f, 34.0f));

    // No title: only the two corner accents, in their order
    HoloMenuFillPieces(accents, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("title 0: 2 accents"), FrameAccents(HoloMenuRectR(), 0.0f, 1.0f, accents), 2);
    HoloMenuTestPiece(*this, TEXT("title 0: top-left accent"), accents[0], FVector2f(6.0f, 18.0f),
        FVector2f(6.0f, 70.0f));
    HoloMenuTestPiece(*this, TEXT("title 0: bottom-right accent"), accents[1], FVector2f(384.0f, 294.0f),
        FVector2f(310.0f, 294.0f));
    TestTrue(TEXT("title 0: no write past the capacity"), HoloMenuPieceGuardsIntact(accents, CAPACITY));

    TestEqual(TEXT("invalid rect: 0 accents"),
        FrameAccents(HoloMenuRect(0.0f, 0.0f, 400.0f, 0.0f), HOLOMENUTEST_R_TITLE, 1.0f, accents), 0);
    TestEqual(TEXT("infinite rect: 0 accents"),
        FrameAccents(HoloMenuRect(0.0f, 0.0f, HOLOMENUTEST_INF, 300.0f), HOLOMENUTEST_R_TITLE, 1.0f, accents), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuRadialArcTest, "SOLTest.HoloMenu.RadialArcDrawIn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A sector's rim arc grows from its middle with the clamped frame fraction
bool FSOLHoloMenuRadialArcTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::RadialArcDrawIn;
    constexpr float HALF = 0.8f;
    float out = HOLOMENUTEST_SENTINEL;
    RadialArcDrawIn(HALF, 0.0f, out);
    TestEqual(TEXT("fraction 0 gives 0"), out, 0.0f, HOLOMENUTEST_TOLERANCE);
    RadialArcDrawIn(HALF, 1.0f, out);
    TestEqual(TEXT("fraction 1 gives the full half aperture"), out, HALF, HOLOMENUTEST_TOLERANCE);
    RadialArcDrawIn(HALF, 0.5f, out);
    TestEqual(TEXT("fraction 0.5 gives half"), out, 0.5f * HALF, HOLOMENUTEST_TOLERANCE);
    RadialArcDrawIn(HALF, -1.0f, out);
    TestEqual(TEXT("fraction below 0 clamps to 0"), out, 0.0f, HOLOMENUTEST_TOLERANCE);
    RadialArcDrawIn(HALF, 2.0f, out);
    TestEqual(TEXT("fraction above 1 clamps to 1"), out, HALF, HOLOMENUTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuCardCornerTicksTest, "SOLTest.HoloMenu.CardCornerTicks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Eight corner ticks of the drawn-in length, each from a corner along its edge, clamped, symmetric
bool FSOLHoloMenuCardCornerTicksTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::CardCornerTicks;
    constexpr int32 CAPACITY = 8;
    FSOLHoloSegment ticks[CAPACITY + HOLOMENUTEST_GUARD];
    const FBox2f rect = HoloMenuRect(10.0f, 20.0f, 110.0f, 80.0f);
    const float tickPx = static_cast<float>(SOL::HoloMenu::CARD_TICK_PX);
    const FVector2f corners[4] = { FVector2f(10.0f, 20.0f), FVector2f(110.0f, 20.0f), FVector2f(110.0f, 80.0f),
        FVector2f(10.0f, 80.0f) };

    // Checks one call: 8 segments, each from a corner along an edge, of the expected length, two per corner
    auto checkTicks = [&](const FString& label, const FBox2f& box, const FVector2f* boxCorners, const float tick,
        const float fraction, const float expectedLength)
    {
        HoloMenuFillPieces(ticks, CAPACITY + HOLOMENUTEST_GUARD);
        const int32 count = CardCornerTicks(box, tick, fraction, ticks);
        TestEqual(label + TEXT(": 8 segments"), count, CAPACITY);
        TestTrue(label + TEXT(": no write past the capacity"), HoloMenuPieceGuardsIntact(ticks, CAPACITY));
        int32 horizontal[4] = { 0, 0, 0, 0 };
        int32 vertical[4] = { 0, 0, 0, 0 };
        for (int32 i = 0; i < FMath::Clamp(count, 0, CAPACITY); ++i)
        {
            const FSOLHoloSegment& tickSeg = ticks[i];
            TestEqual(FString::Printf(TEXT("%s: segment %d length"), *label, i), HoloMenuLength(tickSeg),
                expectedLength, HOLOMENUTEST_ON_LINE_TOLERANCE);
            int32 corner = INDEX_NONE;
            for (int32 c = 0; c < 4; ++c)
            {
                if (HoloMenuNear(tickSeg.A, boxCorners[c], HOLOMENUTEST_ON_LINE_TOLERANCE))
                {
                    corner = c;
                }
            }
            if (!TestTrue(FString::Printf(TEXT("%s: segment %d starts at a corner"), *label, i), corner != INDEX_NONE))
            {
                continue;
            }
            const bool bHorizontal = FMath::Abs(tickSeg.B.Y - tickSeg.A.Y) <= HOLOMENUTEST_ON_LINE_TOLERANCE;
            const bool bVertical = FMath::Abs(tickSeg.B.X - tickSeg.A.X) <= HOLOMENUTEST_ON_LINE_TOLERANCE;
            TestTrue(FString::Printf(TEXT("%s: segment %d runs along an edge"), *label, i), bHorizontal || bVertical);
            TestTrue(FString::Printf(TEXT("%s: segment %d stays on the rect"), *label, i),
                tickSeg.B.X >= box.Min.X - HOLOMENUTEST_ON_LINE_TOLERANCE
                && tickSeg.B.X <= box.Max.X + HOLOMENUTEST_ON_LINE_TOLERANCE
                && tickSeg.B.Y >= box.Min.Y - HOLOMENUTEST_ON_LINE_TOLERANCE
                && tickSeg.B.Y <= box.Max.Y + HOLOMENUTEST_ON_LINE_TOLERANCE);
            horizontal[corner] += bHorizontal ? 1 : 0;
            vertical[corner] += bVertical ? 1 : 0;
        }
        for (int32 c = 0; c < 4; ++c)
        {
            TestTrue(FString::Printf(TEXT("%s: corner %d has one horizontal and one vertical tick"), *label, c),
                horizontal[c] == 1 && vertical[c] == 1);
        }
    };

    checkTicks(TEXT("fraction 1"), rect, corners, tickPx, 1.0f, tickPx);
    checkTicks(TEXT("fraction 0.5"), rect, corners, tickPx, 0.5f, 0.5f * tickPx);
    checkTicks(TEXT("fraction 2 clamps"), rect, corners, tickPx, 2.0f, tickPx);

    // Mirror-symmetric about both centre lines
    {
        HoloMenuFillPieces(ticks, CAPACITY + HOLOMENUTEST_GUARD);
        const int32 count = FMath::Clamp(CardCornerTicks(rect, tickPx, 1.0f, ticks), 0, CAPACITY);
        const float sumX = rect.Min.X + rect.Max.X;
        const float sumY = rect.Min.Y + rect.Max.Y;
        bool bMirrorX = true;
        bool bMirrorY = true;
        for (int32 i = 0; i < count; ++i)
        {
            const FVector2f ax(sumX - ticks[i].A.X, ticks[i].A.Y);
            const FVector2f bx(sumX - ticks[i].B.X, ticks[i].B.Y);
            const FVector2f ay(ticks[i].A.X, sumY - ticks[i].A.Y);
            const FVector2f by(ticks[i].B.X, sumY - ticks[i].B.Y);
            bool bFoundX = false;
            bool bFoundY = false;
            for (int32 j = 0; j < count; ++j)
            {
                bFoundX |= HoloMenuNear(ticks[j].A, ax, HOLOMENUTEST_ON_LINE_TOLERANCE)
                    && HoloMenuNear(ticks[j].B, bx, HOLOMENUTEST_ON_LINE_TOLERANCE);
                bFoundY |= HoloMenuNear(ticks[j].A, ay, HOLOMENUTEST_ON_LINE_TOLERANCE)
                    && HoloMenuNear(ticks[j].B, by, HOLOMENUTEST_ON_LINE_TOLERANCE);
            }
            bMirrorX &= bFoundX;
            bMirrorY &= bFoundY;
        }
        TestTrue(TEXT("symmetric about the vertical centre line"), bMirrorX);
        TestTrue(TEXT("symmetric about the horizontal centre line"), bMirrorY);
    }

    // Tick clamped to min(w, h) / 2 on a small rect
    {
        const FBox2f small = HoloMenuRect(0.0f, 0.0f, 10.0f, 20.0f);
        const FVector2f smallCorners[4] = { FVector2f(0.0f, 0.0f), FVector2f(10.0f, 0.0f), FVector2f(10.0f, 20.0f),
            FVector2f(0.0f, 20.0f) };
        checkTicks(TEXT("small rect"), small, smallCorners, 16.0f, 1.0f, 5.0f);
    }

    // Nothing for fraction 0 or NaN, zero tick, or an invalid / empty rect
    TestEqual(TEXT("fraction 0: 0"), CardCornerTicks(rect, tickPx, 0.0f, ticks), 0);
    TestEqual(TEXT("fraction NaN: 0"), CardCornerTicks(rect, tickPx, HOLOMENUTEST_NAN, ticks), 0);
    TestEqual(TEXT("fraction below 0: 0"), CardCornerTicks(rect, tickPx, -1.0f, ticks), 0);
    TestEqual(TEXT("tick 0: 0"), CardCornerTicks(rect, 0.0f, 1.0f, ticks), 0);
    TestEqual(TEXT("empty rect: 0"), CardCornerTicks(HoloMenuRect(10.0f, 20.0f, 10.0f, 80.0f), tickPx, 1.0f, ticks), 0);
    TestEqual(TEXT("inverted rect: 0"), CardCornerTicks(HoloMenuRect(110.0f, 20.0f, 10.0f, 80.0f), tickPx, 1.0f, ticks),
        0);
    TestEqual(TEXT("NaN rect: 0"), CardCornerTicks(HoloMenuRect(10.0f, HOLOMENUTEST_NAN, 110.0f, 80.0f), tickPx, 1.0f,
        ticks), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuHoverMarkTest, "SOLTest.HoloMenu.HoverMark",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hovered item's brackets, row pointer and centre ticks on the worked rect, arm clamping, scaling, invalid rect
bool FSOLHoloMenuHoverMarkTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::HOVER_MAX_SEGMENTS;
    TestEqual(TEXT("HOVER_MAX_SEGMENTS is 8"), CAPACITY, 8);
    FSOLHoloSegment pieces[CAPACITY + HOLOMENUTEST_GUARD];
    const FBox2f rect = HoloMenuRect(100.0f, 50.0f, 200.0f, 70.0f);
    FVector2f dot(HOLOMENUTEST_SENTINEL, HOLOMENUTEST_SENTINEL);
    int32 dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;

    // Checks the six bracket pieces for a rect and an arm
    auto checkBrackets = [&](const FString& label, const float x0, const float y0, const float x1, const float y1,
        const float arm)
    {
        HoloMenuTestPiece(*this, label + TEXT(" 0"), pieces[0], FVector2f(x0 + arm, y0), FVector2f(x0, y0));
        HoloMenuTestPiece(*this, label + TEXT(" 1"), pieces[1], FVector2f(x0, y0), FVector2f(x0, y1));
        HoloMenuTestPiece(*this, label + TEXT(" 2"), pieces[2], FVector2f(x0, y1), FVector2f(x0 + arm, y1));
        HoloMenuTestPiece(*this, label + TEXT(" 3"), pieces[3], FVector2f(x1 - arm, y0), FVector2f(x1, y0));
        HoloMenuTestPiece(*this, label + TEXT(" 4"), pieces[4], FVector2f(x1, y0), FVector2f(x1, y1));
        HoloMenuTestPiece(*this, label + TEXT(" 5"), pieces[5], FVector2f(x1, y1), FVector2f(x1 - arm, y1));
    };

    // RowPointer
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("RowPointer: 8 pieces"), HoverMark(rect, ESOLHoloHoverMark::RowPointer, 1.0f, pieces, dot, dotCount),
        8);
    TestEqual(TEXT("RowPointer: 1 dot"), dotCount, 1);
    TestTrue(TEXT("RowPointer: no write past the capacity"), HoloMenuPieceGuardsIntact(pieces, CAPACITY));
    checkBrackets(TEXT("RowPointer bracket"), 100.0f, 50.0f, 200.0f, 70.0f, 5.0f);
    HoloMenuTestPiece(*this, TEXT("RowPointer run"), pieces[6], FVector2f(200.0f, 60.0f), FVector2f(234.0f, 60.0f));
    HoloMenuTestPiece(*this, TEXT("RowPointer rise"), pieces[7], FVector2f(234.0f, 60.0f), FVector2f(242.0f, 52.0f));
    HoloMenuTestPoint(*this, TEXT("RowPointer dot"), dot, FVector2f(244.0f, 50.0f));

    // CentreTicks
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;
    TestEqual(TEXT("CentreTicks: 8 pieces"),
        HoverMark(rect, ESOLHoloHoverMark::CentreTicks, 1.0f, pieces, dot, dotCount), 8);
    TestEqual(TEXT("CentreTicks: 0 dots"), dotCount, 0);
    checkBrackets(TEXT("CentreTicks bracket"), 100.0f, 50.0f, 200.0f, 70.0f, 5.0f);
    HoloMenuTestPiece(*this, TEXT("CentreTicks top"), pieces[6], FVector2f(150.0f, 50.0f), FVector2f(150.0f, 42.0f));
    HoloMenuTestPiece(*this, TEXT("CentreTicks bottom"), pieces[7], FVector2f(150.0f, 70.0f), FVector2f(150.0f, 78.0f));

    // BracketsOnly
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;
    TestEqual(TEXT("BracketsOnly: 6 pieces"),
        HoverMark(rect, ESOLHoloHoverMark::BracketsOnly, 1.0f, pieces, dot, dotCount), 6);
    TestEqual(TEXT("BracketsOnly: 0 dots"), dotCount, 0);
    checkBrackets(TEXT("BracketsOnly bracket"), 100.0f, 50.0f, 200.0f, 70.0f, 5.0f);
    TestTrue(TEXT("BracketsOnly: no write past the capacity"), HoloMenuPieceGuardsIntact(pieces, CAPACITY));

    // A rect 6 px wide: arm w / 2 = 3
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("6 px wide: 6 pieces"), HoverMark(HoloMenuRect(100.0f, 50.0f, 106.0f, 70.0f),
        ESOLHoloHoverMark::BracketsOnly, 1.0f, pieces, dot, dotCount), 6);
    checkBrackets(TEXT("6 px wide bracket"), 100.0f, 50.0f, 106.0f, 70.0f, 3.0f);

    // uiScale 2 doubles the arm, pointer and tick lengths
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("uiScale 2 RowPointer: 8 pieces"),
        HoverMark(rect, ESOLHoloHoverMark::RowPointer, 2.0f, pieces, dot, dotCount), 8);
    checkBrackets(TEXT("uiScale 2 bracket"), 100.0f, 50.0f, 200.0f, 70.0f, 10.0f);
    HoloMenuTestPiece(*this, TEXT("uiScale 2 run"), pieces[6], FVector2f(200.0f, 60.0f), FVector2f(268.0f, 60.0f));
    HoloMenuTestPiece(*this, TEXT("uiScale 2 rise"), pieces[7], FVector2f(268.0f, 60.0f), FVector2f(284.0f, 44.0f));
    HoloMenuTestPoint(*this, TEXT("uiScale 2 dot"), dot, FVector2f(288.0f, 40.0f));
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    HoverMark(rect, ESOLHoloHoverMark::CentreTicks, 2.0f, pieces, dot, dotCount);
    HoloMenuTestPiece(*this, TEXT("uiScale 2 top tick"), pieces[6], FVector2f(150.0f, 50.0f), FVector2f(150.0f, 34.0f));
    HoloMenuTestPiece(*this, TEXT("uiScale 2 bottom tick"), pieces[7], FVector2f(150.0f, 70.0f),
        FVector2f(150.0f, 86.0f));

    // Invalid rect: 0 pieces, dot count 0
    const FBox2f invalidRects[] = { HoloMenuRect(100.0f, 50.0f, 100.0f, 70.0f),
        HoloMenuRect(200.0f, 50.0f, 100.0f, 70.0f), HoloMenuRect(100.0f, 50.0f, 200.0f, HOLOMENUTEST_NAN) };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(invalidRects)); ++i)
    {
        dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;
        TestEqual(FString::Printf(TEXT("invalid rect %d: 0 pieces"), i),
            HoverMark(invalidRects[i], ESOLHoloHoverMark::RowPointer, 1.0f, pieces, dot, dotCount), 0);
        TestEqual(FString::Printf(TEXT("invalid rect %d: dot count 0"), i), dotCount, 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuTabMarkerTest, "SOLTest.HoloMenu.TabMarker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The profile tab marker: a dim bar when not selected, an outline into the divider with a dot when selected
bool FSOLHoloMenuTabMarkerTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::TAB_MAX_SEGMENTS;
    TestEqual(TEXT("TAB_MAX_SEGMENTS is 4"), CAPACITY, 4);
    FSOLHoloSegment pieces[CAPACITY + HOLOMENUTEST_GUARD];
    const FBox2f tab = HoloMenuRect(78.0f, 160.0f, 228.0f, 185.0f);
    FVector2f dot(HOLOMENUTEST_SENTINEL, HOLOMENUTEST_SENTINEL);
    int32 dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;

    // Not selected
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("not selected: 3 pieces"), TabMarker(tab, 238.0f, false, 1.0f, pieces, dot, dotCount), 3);
    TestEqual(TEXT("not selected: 0 dots"), dotCount, 0);
    TestTrue(TEXT("not selected: no write past the capacity"), HoloMenuPieceGuardsIntact(pieces, CAPACITY));
    HoloMenuTestPiece(*this, TEXT("bar top arm"), pieces[0], FVector2f(84.0f, 162.0f), FVector2f(78.0f, 162.0f));
    HoloMenuTestPiece(*this, TEXT("bar"), pieces[1], FVector2f(78.0f, 162.0f), FVector2f(78.0f, 183.0f));
    HoloMenuTestPiece(*this, TEXT("bar bottom arm"), pieces[2], FVector2f(78.0f, 183.0f), FVector2f(84.0f, 183.0f));

    // Selected
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;
    TestEqual(TEXT("selected: 4 pieces"), TabMarker(tab, 238.0f, true, 1.0f, pieces, dot, dotCount), 4);
    TestEqual(TEXT("selected: 1 dot"), dotCount, 1);
    TestTrue(TEXT("selected: no write past the capacity"), HoloMenuPieceGuardsIntact(pieces, CAPACITY));
    HoloMenuTestPiece(*this, TEXT("selected top"), pieces[0], FVector2f(238.0f, 160.0f), FVector2f(84.0f, 160.0f));
    HoloMenuTestPiece(*this, TEXT("selected cut"), pieces[1], FVector2f(84.0f, 160.0f), FVector2f(78.0f, 166.0f));
    HoloMenuTestPiece(*this, TEXT("selected left"), pieces[2], FVector2f(78.0f, 166.0f), FVector2f(78.0f, 185.0f));
    HoloMenuTestPiece(*this, TEXT("selected bottom"), pieces[3], FVector2f(78.0f, 185.0f), FVector2f(238.0f, 185.0f));
    HoloMenuTestPoint(*this, TEXT("selected dot"), dot, FVector2f(74.0f, 172.5f));

    // Divider NaN or left of the tab's right edge reads x1 = 228
    const float badDividers[] = { HOLOMENUTEST_NAN, 100.0f };
    for (const float divider : badDividers)
    {
        HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
        TestEqual(FString::Printf(TEXT("divider %f: 4 pieces"), divider),
            TabMarker(tab, divider, true, 1.0f, pieces, dot, dotCount), 4);
        HoloMenuTestPoint(*this, FString::Printf(TEXT("divider %f: top starts at 228"), divider), pieces[0].A,
            FVector2f(228.0f, 160.0f));
        HoloMenuTestPoint(*this, FString::Printf(TEXT("divider %f: bottom ends at 228"), divider), pieces[3].B,
            FVector2f(228.0f, 185.0f));
    }

    // Invalid rect: 0 pieces, 0 dots
    const bool selections[] = { false, true };
    for (const bool bSelected : selections)
    {
        dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;
        TestEqual(FString::Printf(TEXT("invalid rect (selected %d): 0 pieces"), bSelected ? 1 : 0),
            TabMarker(HoloMenuRect(228.0f, 160.0f, 78.0f, 185.0f), 238.0f, bSelected, 1.0f, pieces, dot, dotCount), 0);
        TestEqual(FString::Printf(TEXT("invalid rect (selected %d): 0 dots"), bSelected ? 1 : 0), dotCount, 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuDashedDividerTest, "SOLTest.HoloMenu.DashedDivider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The dashed column divider: dash layout and clipping, the tab opening, the capacity cap and empty cases
bool FSOLHoloMenuDashedDividerTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::DIVIDER_MAX_DASHES;
    TestEqual(TEXT("DIVIDER_MAX_DASHES is 40"), CAPACITY, 40);
    FSOLHoloSegment dashes[CAPACITY + HOLOMENUTEST_GUARD];
    const FVector2f top(0.0f, 0.0f);

    // Checks the dashes against expected (top, bottom) pairs at x = 0
    auto checkDashes = [&](const FString& label, const int32 count, const float* tops, const float* bottoms,
        const int32 expectedCount)
    {
        TestEqual(label + TEXT(": count"), count, expectedCount);
        for (int32 i = 0; i < FMath::Min(FMath::Clamp(count, 0, CAPACITY), expectedCount); ++i)
        {
            HoloMenuTestPiece(*this, FString::Printf(TEXT("%s: dash %d"), *label, i), dashes[i],
                FVector2f(0.0f, tops[i]), FVector2f(0.0f, bottoms[i]));
        }
    };

    // No opening (openTopY > openBottomY, or NaN): 8 dashes, the last clipped
    const float allTops[] = { 0.0f, 14.0f, 28.0f, 42.0f, 56.0f, 70.0f, 84.0f, 98.0f };
    const float allBottoms[] = { 9.0f, 23.0f, 37.0f, 51.0f, 65.0f, 79.0f, 93.0f, 100.0f };
    const float openings[][2] = { { 1.0f, 0.0f }, { HOLOMENUTEST_NAN, 50.0f }, { 30.0f, HOLOMENUTEST_NAN } };
    for (const auto& opening : openings)
    {
        HoloMenuFillPieces(dashes, CAPACITY + HOLOMENUTEST_GUARD);
        const int32 count = DashedDivider(top, 100.0f, opening[0], opening[1], 1.0f, dashes);
        checkDashes(FString::Printf(TEXT("no opening [%f, %f]"), opening[0], opening[1]), count, allTops, allBottoms,
            8);
        TestTrue(TEXT("no opening: no write past the capacity"), HoloMenuPieceGuardsIntact(dashes, CAPACITY));
    }

    // Opening [30, 50]: the 28-37 and 42-51 dashes are left out
    {
        const float tops[] = { 0.0f, 14.0f, 56.0f, 70.0f, 84.0f, 98.0f };
        const float bottoms[] = { 9.0f, 23.0f, 65.0f, 79.0f, 93.0f, 100.0f };
        HoloMenuFillPieces(dashes, CAPACITY + HOLOMENUTEST_GUARD);
        checkDashes(TEXT("opening [30, 50]"), DashedDivider(top, 100.0f, 30.0f, 50.0f, 1.0f, dashes), tops, bottoms, 6);
    }

    // An opening that only touches a dash end ([23, 27]) still leaves that dash (14-23) out
    {
        const float tops[] = { 0.0f, 28.0f, 42.0f, 56.0f, 70.0f, 84.0f, 98.0f };
        const float bottoms[] = { 9.0f, 37.0f, 51.0f, 65.0f, 79.0f, 93.0f, 100.0f };
        HoloMenuFillPieces(dashes, CAPACITY + HOLOMENUTEST_GUARD);
        checkDashes(TEXT("opening [23, 27]"), DashedDivider(top, 100.0f, 23.0f, 27.0f, 1.0f, dashes), tops, bottoms, 7);
    }

    // Offset start: all dashes at x = top.X, from top.Y
    {
        HoloMenuFillPieces(dashes, CAPACITY + HOLOMENUTEST_GUARD);
        const int32 count = DashedDivider(FVector2f(5.0f, 10.0f), 40.0f, 1.0f, 0.0f, 1.0f, dashes);
        TestEqual(TEXT("offset: 3 dashes"), count, 3);
        HoloMenuTestPiece(*this, TEXT("offset dash 0"), dashes[0], FVector2f(5.0f, 10.0f), FVector2f(5.0f, 19.0f));
        HoloMenuTestPiece(*this, TEXT("offset dash 1"), dashes[1], FVector2f(5.0f, 24.0f), FVector2f(5.0f, 33.0f));
        HoloMenuTestPiece(*this, TEXT("offset dash 2"), dashes[2], FVector2f(5.0f, 38.0f), FVector2f(5.0f, 40.0f));
    }

    // Bottom 1000: exactly the capacity, written top to bottom
    HoloMenuFillPieces(dashes, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("bottom 1000: DIVIDER_MAX_DASHES"), DashedDivider(top, 1000.0f, 1.0f, 0.0f, 1.0f, dashes), CAPACITY);
    TestTrue(TEXT("bottom 1000: no write past the capacity"), HoloMenuPieceGuardsIntact(dashes, CAPACITY));
    HoloMenuTestPiece(*this, TEXT("bottom 1000: last dash"), dashes[CAPACITY - 1],
        FVector2f(0.0f, 14.0f * (CAPACITY - 1)), FVector2f(0.0f, 14.0f * (CAPACITY - 1) + 9.0f));

    // Bottom at or above the top, or a non-finite coordinate: 0
    TestEqual(TEXT("bottom at the top: 0"), DashedDivider(top, 0.0f, 1.0f, 0.0f, 1.0f, dashes), 0);
    TestEqual(TEXT("bottom above the top: 0"), DashedDivider(top, -5.0f, 1.0f, 0.0f, 1.0f, dashes), 0);
    TestEqual(TEXT("NaN top: 0"), DashedDivider(FVector2f(HOLOMENUTEST_NAN, 0.0f), 100.0f, 1.0f, 0.0f, 1.0f, dashes),
        0);
    TestEqual(TEXT("infinite bottom: 0"), DashedDivider(top, HOLOMENUTEST_INF, 1.0f, 0.0f, 1.0f, dashes), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuBrokenDividerTest, "SOLTest.HoloMenu.BrokenDivider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The broken horizontal divider: gaps with and without the 45-degree step, the no-step and empty cases
bool FSOLHoloMenuBrokenDividerTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 CAPACITY = SOL::HoloMenu::DIVIDER_MAX_SEGMENTS;
    TestEqual(TEXT("DIVIDER_MAX_SEGMENTS is 6"), CAPACITY, 6);
    FSOLHoloSegment pieces[CAPACITY + HOLOMENUTEST_GUARD];
    const FVector2f start(0.0f, 0.0f);

    // No step (0, negative and NaN all mean no step): 3 pieces
    const float noSteps[] = { 0.0f, -6.0f, HOLOMENUTEST_NAN };
    for (const float step : noSteps)
    {
        HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
        const FString label = FString::Printf(TEXT("step %f"), step);
        TestEqual(label + TEXT(": 3 pieces"), BrokenDivider(start, 200.0f, 100.0f, step, 1.0f, pieces), 3);
        TestTrue(label + TEXT(": no write past the capacity"), HoloMenuPieceGuardsIntact(pieces, CAPACITY));
        HoloMenuTestPiece(*this, label + TEXT(" piece 0"), pieces[0], FVector2f(0.0f, 0.0f), FVector2f(66.0f, 0.0f));
        HoloMenuTestPiece(*this, label + TEXT(" piece 1"), pieces[1], FVector2f(74.0f, 0.0f), FVector2f(150.0f, 0.0f));
        HoloMenuTestPiece(*this, label + TEXT(" piece 2"), pieces[2], FVector2f(166.0f, 0.0f), FVector2f(200.0f, 0.0f));
    }

    // Step at 100 by 6: path length 194 + 6 sqrt(2) = 202.485, 5 pieces
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("stepped: 5 pieces"), BrokenDivider(start, 200.0f, 100.0f, 6.0f, 1.0f, pieces), 5);
    TestTrue(TEXT("stepped: no write past the capacity"), HoloMenuPieceGuardsIntact(pieces, CAPACITY));
    HoloMenuTestPiece(*this, TEXT("stepped 0"), pieces[0], FVector2f(0.0f, 0.0f), FVector2f(66.820f, 0.0f));
    HoloMenuTestPiece(*this, TEXT("stepped 1"), pieces[1], FVector2f(74.820f, 0.0f), FVector2f(100.0f, 0.0f));
    HoloMenuTestPiece(*this, TEXT("stepped 2"), pieces[2], FVector2f(100.0f, 0.0f), FVector2f(106.0f, -6.0f));
    HoloMenuTestPiece(*this, TEXT("stepped 3"), pieces[3], FVector2f(106.0f, -6.0f), FVector2f(149.379f, -6.0f));
    HoloMenuTestPiece(*this, TEXT("stepped 4"), pieces[4], FVector2f(165.379f, -6.0f), FVector2f(200.0f, -6.0f));
    TestEqual(TEXT("stepped: visible length = path length minus the gaps"), HoloMenuSumLength(pieces, 5),
        194.0f + 6.0f * HOLOMENUTEST_SQRT2 - 24.0f, HOLOMENUTEST_PX_TOLERANCE);

    // Step position clamped to [0, endX - start.X - stepPx]: the path still ends at (endX, start.Y - stepPx)
    HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
    const int32 clampedCount = BrokenDivider(start, 200.0f, 1000.0f, 6.0f, 1.0f, pieces);
    TestTrue(TEXT("step clamped: count within the capacity"), clampedCount >= 1 && clampedCount <= CAPACITY);
    TestTrue(TEXT("step clamped: no write past the capacity"), HoloMenuPieceGuardsIntact(pieces, CAPACITY));
    if (clampedCount >= 1 && clampedCount <= CAPACITY)
    {
        HoloMenuTestPoint(*this, TEXT("step clamped: ends at (200, -6)"), pieces[clampedCount - 1].B,
            FVector2f(200.0f, -6.0f));
    }

    // endX <= start.X or a non-finite value: 0
    TestEqual(TEXT("endX at start: 0"), BrokenDivider(start, 0.0f, 100.0f, 6.0f, 1.0f, pieces), 0);
    TestEqual(TEXT("endX left of start: 0"), BrokenDivider(start, -10.0f, 100.0f, 6.0f, 1.0f, pieces), 0);
    TestEqual(TEXT("NaN start: 0"),
        BrokenDivider(FVector2f(0.0f, HOLOMENUTEST_NAN), 200.0f, 100.0f, 6.0f, 1.0f, pieces), 0);
    TestEqual(TEXT("infinite endX: 0"), BrokenDivider(start, HOLOMENUTEST_INF, 100.0f, 6.0f, 1.0f, pieces), 0);

    // Never more than DIVIDER_MAX_SEGMENTS, over step positions and sizes
    bool bWithin = true;
    for (int32 at = -20; at <= 220; at += 4)
    {
        for (int32 step = 0; step <= 40; step += 2)
        {
            HoloMenuFillPieces(pieces, CAPACITY + HOLOMENUTEST_GUARD);
            const int32 count = BrokenDivider(start, 200.0f, static_cast<float>(at), static_cast<float>(step), 1.0f,
                pieces);
            bWithin &= count >= 0 && count <= CAPACITY && HoloMenuPieceGuardsIntact(pieces, CAPACITY);
        }
    }
    TestTrue(TEXT("never more than DIVIDER_MAX_SEGMENTS"), bWithin);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuCapacityTest, "SOLTest.HoloMenu.Capacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every frame-geometry function, over a sweep of rects, fractions and scales, stays within its stated capacity
bool FSOLHoloMenuCapacityTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    constexpr int32 OUTLINE_CAP = SOL::HoloMenu::FRAME_OUTLINE_POINTS;
    constexpr int32 SEGMENT_CAP = SOL::HoloMenu::FRAME_MAX_SEGMENTS;
    constexpr int32 DOT_CAP = SOL::HoloMenu::FRAME_MAX_DOTS;
    constexpr int32 RULER_CAP = SOL::HoloMenu::FRAME_RULER_TICK_COUNT;
    constexpr int32 ACCENT_CAP = SOL::HoloMenu::FRAME_ACCENT_COUNT;
    constexpr int32 HOVER_CAP = SOL::HoloMenu::HOVER_MAX_SEGMENTS;
    constexpr int32 TAB_CAP = SOL::HoloMenu::TAB_MAX_SEGMENTS;
    constexpr int32 DASH_CAP = SOL::HoloMenu::DIVIDER_MAX_DASHES;
    constexpr int32 DIVIDER_CAP = SOL::HoloMenu::DIVIDER_MAX_SEGMENTS;
    constexpr int32 CARD_CAP = 8;

    FVector2f points[OUTLINE_CAP + HOLOMENUTEST_GUARD];
    FSOLHoloSegment segments[SEGMENT_CAP + HOLOMENUTEST_GUARD];
    FVector2f dots[DOT_CAP + HOLOMENUTEST_GUARD];
    FSOLHoloSegment ruler[RULER_CAP + HOLOMENUTEST_GUARD];
    FSOLHoloSegment accents[ACCENT_CAP + HOLOMENUTEST_GUARD];
    FSOLHoloSegment hover[HOVER_CAP + HOLOMENUTEST_GUARD];
    FSOLHoloSegment tabs[TAB_CAP + HOLOMENUTEST_GUARD];
    FSOLHoloSegment dashes[DASH_CAP + HOLOMENUTEST_GUARD];
    FSOLHoloSegment divider[DIVIDER_CAP + HOLOMENUTEST_GUARD];
    FSOLHoloSegment card[CARD_CAP + HOLOMENUTEST_GUARD];

    // Sweep axes: rect sizes (1x1 up to 4000x2000) at two origins, uiScales, title steps, fractions
    const FVector2f sizes[] = { FVector2f(1.0f, 1.0f), FVector2f(2.0f, 3.0f), FVector2f(1.0f, 2000.0f),
        FVector2f(4000.0f, 1.0f), FVector2f(17.0f, 32.0f), FVector2f(33.0f, 63.0f), FVector2f(34.0f, 64.0f),
        FVector2f(40.0f, 70.0f), FVector2f(100.0f, 40.0f), FVector2f(400.0f, 300.0f), FVector2f(1280.0f, 720.0f),
        FVector2f(4000.0f, 2000.0f) };
    const FVector2f origins[] = { FVector2f(0.0f, 0.0f), FVector2f(-500.0f, 250.0f) };
    const float scales[] = { 0.5f, 1.0f, 2.0f, HOLOMENUTEST_NAN };
    const float titles[] = { 0.0f, 50.0f, 200.0f, 1.0e4f };
    TArray<float> fractions;
    fractions.Reserve(104);
    fractions.Add(-1.0f);
    fractions.Add(0.0f);
    fractions.Add(HOLOMENUTEST_NAN);
    for (int32 i = 1; i <= 100; ++i)
    {
        fractions.Add(static_cast<float>(i) / 100.0f);
    }
    fractions.Add(2.0f);

    FSOLHoloMenuSweepLog outlineLog;
    FSOLHoloMenuSweepLog segmentLog;
    FSOLHoloMenuSweepLog dotLog;
    FSOLHoloMenuSweepLog rulerLog;
    FSOLHoloMenuSweepLog accentLog;
    FSOLHoloMenuSweepLog hoverLog;
    FSOLHoloMenuSweepLog tabLog;
    FSOLHoloMenuSweepLog dashLog;
    FSOLHoloMenuSweepLog dividerLog;
    FSOLHoloMenuSweepLog cardLog;
    FVector2f dot(HOLOMENUTEST_SENTINEL, HOLOMENUTEST_SENTINEL);
    int32 dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;

    for (const FVector2f& origin : origins)
    {
        for (const FVector2f& size : sizes)
        {
            const FBox2f rect(origin, origin + size);
            for (const float scale : scales)
            {
                const FString where = FString::Printf(TEXT("rect (%g,%g)-(%g,%g) uiScale %g"), rect.Min.X, rect.Min.Y,
                    rect.Max.X, rect.Max.Y, scale);

                // Functions with a title step
                for (const float title : titles)
                {
                    HoloMenuFillPoints(points, OUTLINE_CAP + HOLOMENUTEST_GUARD);
                    const int32 outlineCount = FrameOutline(rect, title, scale, points);
                    if (outlineCount < 0 || outlineCount > OUTLINE_CAP
                        || !HoloMenuPointGuardsIntact(points, OUTLINE_CAP))
                    {
                        outlineLog.Fail(FString::Printf(TEXT("%s title %g: count %d"), *where, title, outlineCount));
                    }
                    HoloMenuFillPieces(accents, ACCENT_CAP + HOLOMENUTEST_GUARD);
                    const int32 accentCount = FrameAccents(rect, title, scale, accents);
                    if (accentCount < 0 || accentCount > ACCENT_CAP || !HoloMenuPieceGuardsIntact(accents, ACCENT_CAP))
                    {
                        accentLog.Fail(FString::Printf(TEXT("%s title %g: count %d"), *where, title, accentCount));
                    }
                    for (const float fraction : fractions)
                    {
                        HoloMenuFillPieces(segments, SEGMENT_CAP + HOLOMENUTEST_GUARD);
                        const int32 segmentCount = FrameDrawSegments(rect, title, scale, fraction, segments);
                        if (segmentCount < 0 || segmentCount > SEGMENT_CAP
                            || !HoloMenuPieceGuardsIntact(segments, SEGMENT_CAP))
                        {
                            segmentLog.Fail(FString::Printf(TEXT("%s title %g f %g: count %d"), *where, title, fraction,
                                segmentCount));
                        }
                        HoloMenuFillPoints(dots, DOT_CAP + HOLOMENUTEST_GUARD);
                        const int32 dotsCount = FrameGapDots(rect, title, scale, fraction, dots);
                        if (dotsCount < 0 || dotsCount > DOT_CAP || !HoloMenuPointGuardsIntact(dots, DOT_CAP))
                        {
                            dotLog.Fail(FString::Printf(TEXT("%s title %g f %g: count %d"), *where, title, fraction,
                                dotsCount));
                        }
                    }
                }

                // Ruler ticks
                HoloMenuFillPieces(ruler, RULER_CAP + HOLOMENUTEST_GUARD);
                const int32 rulerCount = FrameRulerTicks(rect, scale, ruler);
                if (rulerCount < 0 || rulerCount > RULER_CAP || !HoloMenuPieceGuardsIntact(ruler, RULER_CAP))
                {
                    rulerLog.Fail(FString::Printf(TEXT("%s: count %d"), *where, rulerCount));
                }

                // Hover marks
                const ESOLHoloHoverMark marks[] = { ESOLHoloHoverMark::BracketsOnly, ESOLHoloHoverMark::RowPointer,
                    ESOLHoloHoverMark::CentreTicks };
                for (const ESOLHoloHoverMark mark : marks)
                {
                    HoloMenuFillPieces(hover, HOVER_CAP + HOLOMENUTEST_GUARD);
                    dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;
                    const int32 hoverCount = HoverMark(rect, mark, scale, hover, dot, dotCount);
                    if (hoverCount < 0 || hoverCount > HOVER_CAP || dotCount < 0 || dotCount > 1
                        || !HoloMenuPieceGuardsIntact(hover, HOVER_CAP))
                    {
                        hoverLog.Fail(FString::Printf(TEXT("%s mark %d: count %d, dots %d"), *where,
                            static_cast<int32>(mark), hoverCount, dotCount));
                    }
                }

                // Tab markers, both states
                const bool selections[] = { false, true };
                for (const bool bSelected : selections)
                {
                    HoloMenuFillPieces(tabs, TAB_CAP + HOLOMENUTEST_GUARD);
                    dotCount = HOLOMENUTEST_DOT_COUNT_SENTINEL;
                    const int32 tabCount = TabMarker(rect, rect.Max.X + 10.0f, bSelected, scale, tabs, dot, dotCount);
                    if (tabCount < 0 || tabCount > TAB_CAP || dotCount < 0 || dotCount > 1
                        || !HoloMenuPieceGuardsIntact(tabs, TAB_CAP))
                    {
                        tabLog.Fail(FString::Printf(TEXT("%s selected %d: count %d, dots %d"), *where,
                            bSelected ? 1 : 0, tabCount, dotCount));
                    }
                }

                // Dashed divider down the rect, with and without an opening
                const float midY = 0.5f * (rect.Min.Y + rect.Max.Y);
                const float openingTops[] = { 1.0f, midY - 10.0f };
                const float openingBottoms[] = { 0.0f, midY + 10.0f };
                for (int32 o = 0; o < 2; ++o)
                {
                    HoloMenuFillPieces(dashes, DASH_CAP + HOLOMENUTEST_GUARD);
                    const int32 dashCount = DashedDivider(rect.Min, rect.Max.Y, openingTops[o], openingBottoms[o],
                        scale, dashes);
                    if (dashCount < 0 || dashCount > DASH_CAP || !HoloMenuPieceGuardsIntact(dashes, DASH_CAP))
                    {
                        dashLog.Fail(FString::Printf(TEXT("%s opening %d: count %d"), *where, o, dashCount));
                    }
                }

                // Broken divider across the rect, with the speed panel's step and without
                const float stepAts[] = { 0.0f, static_cast<float>(SOL::HoloMenu::SPEED_DIVIDER_STEP_AT_PX) * scale };
                const float stepSizes[] = { 0.0f, static_cast<float>(SOL::HoloMenu::SPEED_DIVIDER_STEP_PX) * scale,
                    HOLOMENUTEST_NAN };
                for (const float stepAt : stepAts)
                {
                    for (const float stepSize : stepSizes)
                    {
                        HoloMenuFillPieces(divider, DIVIDER_CAP + HOLOMENUTEST_GUARD);
                        const int32 dividerCount = BrokenDivider(FVector2f(rect.Min.X, rect.Max.Y), rect.Max.X, stepAt,
                            stepSize, scale, divider);
                        if (dividerCount < 0 || dividerCount > DIVIDER_CAP
                            || !HoloMenuPieceGuardsIntact(divider, DIVIDER_CAP))
                        {
                            dividerLog.Fail(FString::Printf(TEXT("%s step %g at %g: count %d"), *where, stepSize,
                                stepAt, dividerCount));
                        }
                    }
                }

                // Card corner ticks over the fractions
                const float tickPx = static_cast<float>(SOL::HoloMenu::CARD_TICK_PX) * scale;
                for (const float fraction : fractions)
                {
                    HoloMenuFillPieces(card, CARD_CAP + HOLOMENUTEST_GUARD);
                    const int32 cardCount = CardCornerTicks(rect, tickPx, fraction, card);
                    if (cardCount < 0 || cardCount > CARD_CAP || !HoloMenuPieceGuardsIntact(card, CARD_CAP))
                    {
                        cardLog.Fail(FString::Printf(TEXT("%s f %g: count %d"), *where, fraction, cardCount));
                    }
                }
            }
        }
    }

    TestEqual(FString::Printf(TEXT("FrameOutline capacity failures (first: %s)"), *outlineLog.First),
        outlineLog.Failures, 0);
    TestEqual(FString::Printf(TEXT("FrameDrawSegments capacity failures (first: %s)"), *segmentLog.First),
        segmentLog.Failures, 0);
    TestEqual(FString::Printf(TEXT("FrameGapDots capacity failures (first: %s)"), *dotLog.First), dotLog.Failures, 0);
    TestEqual(FString::Printf(TEXT("FrameRulerTicks capacity failures (first: %s)"), *rulerLog.First),
        rulerLog.Failures, 0);
    TestEqual(FString::Printf(TEXT("FrameAccents capacity failures (first: %s)"), *accentLog.First),
        accentLog.Failures, 0);
    TestEqual(FString::Printf(TEXT("HoverMark capacity failures (first: %s)"), *hoverLog.First), hoverLog.Failures, 0);
    TestEqual(FString::Printf(TEXT("TabMarker capacity failures (first: %s)"), *tabLog.First), tabLog.Failures, 0);
    TestEqual(FString::Printf(TEXT("DashedDivider capacity failures (first: %s)"), *dashLog.First), dashLog.Failures,
        0);
    TestEqual(FString::Printf(TEXT("BrokenDivider capacity failures (first: %s)"), *dividerLog.First),
        dividerLog.Failures, 0);
    TestEqual(FString::Printf(TEXT("CardCornerTicks capacity failures (first: %s)"), *cardLog.First),
        cardLog.Failures, 0);
    return true;
}

// --- Review additions: time wrap, large application times, the effective scale, extreme inputs ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuSweepTimeWrapTest, "SOLTest.HoloMenu.SweepTimeWrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SweepFraction runs on smoothly across every HUD time wrap (HUD_TIME_WRAP_S is a whole number of sweep periods)
bool FSOLHoloMenuSweepTimeWrapTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::SweepFraction;
    const double periodS = SOL::HoloMenu::IDLE_SWEEP_PERIOD_S;
    const double wrapS = static_cast<double>(SOL::Hologram::HUD_TIME_WRAP_S);
    TestEqual(TEXT("the wrap is a whole number of periods"), FMath::Frac(wrapS / periodS), 0.0);

    // Just before and just after a wrap: 1 - delta / period, then delta / period (one small cyclic step)
    const double deltaS = 0.01;
    const double wraps[] = { wrapS, 2.0 * wrapS, 16789.0 * wrapS };
    for (const double w : wraps)
    {
        const float before = SweepFraction(w - deltaS);
        const float after = SweepFraction(w + deltaS);
        TestEqual(FString::Printf(TEXT("wrap %g: just before"), w), before, static_cast<float>(1.0 - deltaS / periodS),
            HOLOMENUTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("wrap %g: at the wrap"), w), SweepFraction(w), 0.0f, HOLOMENUTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("wrap %g: just after"), w), after, static_cast<float>(deltaS / periodS),
            HOLOMENUTEST_TOLERANCE);
    }

    // A fine walk across the first wrap: every step moves the sweep by step / period, cyclically
    const double stepS = 0.001;
    const float expectedStep = static_cast<float>(stepS / periodS);
    bool bSmooth = true;
    float previous = SweepFraction(wrapS - 0.1);
    for (int32 i = 1; i <= 200; ++i)
    {
        const float current = SweepFraction(wrapS - 0.1 + static_cast<double>(i) * stepS);
        float moved = current - previous;
        if (moved < -0.5f)
        {
            moved += 1.0f;
        }
        bSmooth &= FMath::IsNearlyEqual(moved, expectedStep, HOLOMENUTEST_TOLERANCE);
        previous = current;
    }
    TestTrue(TEXT("no jump across the wrap"), bSmooth);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuLargeAppTimeTest, "SOLTest.HoloMenu.LargeAppTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Open, reverse, hover and cue behave the same at a Windows-sized application time (FPlatformTime adds 2^24 s)
bool FSOLHoloMenuLargeAppTimeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    const double openS = SOL::HoloMenu::OPEN_S;
    const double closeS = SOL::HoloMenu::CLOSE_S;
    const double rampS = SOL::HoloMenu::HOVER_RAMP_S;
    const double cueS = SOL::HoloMenu::HOVER_CUE_MIN_INTERVAL_S;
    const double t0 = 16777216.0 + 12345.678;
    FSOLHoloMenuAnim anim;

    // Open, then reverse a quarter of the way in, then reopen a tenth of a close later
    TestTrue(TEXT("BeginOpen"), BeginOpen(anim, t0, 1.0f));
    TestEqual(TEXT("half open at OPEN_S / 2"), Progress(anim, t0 + 0.5 * openS, 1.0f), 0.5f,
        HOLOMENUTEST_PROGRESS_TOLERANCE);
    const double closeAt = t0 + 0.25 * openS;
    TestTrue(TEXT("BeginClose a quarter in"), BeginClose(anim, closeAt, 1.0f));
    TestEqual(TEXT("no jump at the close"), Progress(anim, closeAt, 1.0f), 0.25f, HOLOMENUTEST_PROGRESS_TOLERANCE);
    const double reopenAt = closeAt + 0.1 * closeS;
    TestEqual(TEXT("falls at the CLOSE_S rate"), Progress(anim, reopenAt, 1.0f), 0.15f,
        HOLOMENUTEST_PROGRESS_TOLERANCE);
    TestTrue(TEXT("BeginOpen from Closing"), BeginOpen(anim, reopenAt, 1.0f));
    TestEqual(TEXT("no jump at the reopen"), Progress(anim, reopenAt, 1.0f), 0.15f, HOLOMENUTEST_PROGRESS_TOLERANCE);
    Advance(anim, reopenAt + 0.5 * openS, 1.0f);
    TestTrue(TEXT("still Opening at 0.65"), anim.Phase == ESOLHoloMenuPhase::Opening);
    const double openAt = reopenAt + 0.85 * openS + HOLOMENUTEST_TIME_EPS_S;
    Advance(anim, openAt, 1.0f);
    TestTrue(TEXT("Open after the remaining 0.85 x OPEN_S"), anim.Phase == ESOLHoloMenuPhase::Open);

    // Hover: the glow follows the formula mid-ramp and settles at exactly HOVER_GLOW_INTENSITY
    TestTrue(TEXT("SetHovered on a new item"), SetHovered(anim, 2, openAt));
    TestTrue(TEXT("first hover cue plays"), ShouldPlayHoverCue(anim, true, openAt));
    const FSOLHoloMenuLook midRamp = Evaluate(anim, openAt + 0.5 * rampS, 1.0f);
    TestEqual(TEXT("mid-ramp glow matches the formula"), midRamp.HoverIntensity,
        HoloMenuReferenceHoverGlow(0.5 * rampS, 1.0f), HOLOMENUTEST_HOVER_TOLERANCE);
    const FSOLHoloMenuLook settled = Evaluate(anim, openAt + 1.0, 1.0f);
    TestTrue(TEXT("visible"), settled.bVisible);
    TestEqual(TEXT("progress 1"), settled.Progress, 1.0f, HOLOMENUTEST_TOLERANCE);
    TestEqual(TEXT("settled glow"), settled.HoverIntensity, SOL::HoloMenu::HOVER_GLOW_INTENSITY, 1.0e-6f);

    // The cue rate limit
    const double soonS = openAt + 0.5 * cueS;
    TestTrue(TEXT("SetHovered soon after"), SetHovered(anim, 3, soonS));
    TestFalse(TEXT("no cue within HOVER_CUE_MIN_INTERVAL_S"), ShouldPlayHoverCue(anim, true, soonS));
    const double laterS = openAt + cueS + HOLOMENUTEST_TIME_EPS_S;
    TestTrue(TEXT("SetHovered after the interval"), SetHovered(anim, 4, laterS));
    TestTrue(TEXT("cue after HOVER_CUE_MIN_INTERVAL_S"), ShouldPlayHoverCue(anim, true, laterS));

    // The sweep: frac(fmod(t0, 1000) / 4) = frac(561.678 / 4) = 0.4195
    TestEqual(TEXT("sweep position at t0"), SweepFraction(t0), 0.4195f, HOLOMENUTEST_PROGRESS_TOLERANCE);

    // Close to Closed, clearing the hover
    const double closeAgainAt = laterS + 1.0;
    TestTrue(TEXT("BeginClose from Open"), BeginClose(anim, closeAgainAt, 1.0f));
    TestEqual(TEXT("hover cleared by the close"), anim.HoveredItem, static_cast<int32>(INDEX_NONE));
    Advance(anim, closeAgainAt + 0.5 * closeS, 1.0f);
    TestTrue(TEXT("still Closing mid-way"), anim.Phase == ESOLHoloMenuPhase::Closing);
    Advance(anim, closeAgainAt + closeS + HOLOMENUTEST_TIME_EPS_S, 1.0f);
    TestTrue(TEXT("Closed after CLOSE_S"), anim.Phase == ESOLHoloMenuPhase::Closed);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuEffectiveScaleTest, "SOLTest.HoloMenu.EffectiveScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Gaps, ruler ticks and accents use the effective scale k (here below uiScale), not uiScale itself
bool FSOLHoloMenuEffectiveScaleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;

    // Small rect (0,0)-(17,32) at uiScale 1: k = 0.5, a = 5, s = 5, c = 7, L = 40 + 29 sqrt(2) = 81.012. The gaps at
    // 0.5x length (5, 10, 6, 5, 12, 4, 6 px) start at 0.20 L, 0.27 L, ... and merge into five u intervals:
    // [16.202, 21.202], [21.873, 35.164] (gaps 2 and 3), [42.126, 47.126], [53.468, 65.468] (5 and 6),
    // [72.911, 78.911]; 41.291 px of gap in all, so 39.721 px of visible outline at f = 1
    const FBox2f smallRect = HoloMenuRect(0.0f, 0.0f, 17.0f, 32.0f);
    const FSOLHoloMenuTestOutline outline = HoloMenuOutlineSmall();
    const float perimeter = 40.0f + 29.0f * HOLOMENUTEST_SQRT2;
    TestEqual(TEXT("small rect: L"), FramePerimeter(smallRect, HOLOMENUTEST_R_TITLE, 1.0f), perimeter,
        HOLOMENUTEST_PX_TOLERANCE);
    const float expectedU[] = { 16.202f, 21.202f, 21.873f, 35.164f, 42.126f, 47.126f, 53.468f, 65.468f, 72.911f,
        78.911f };
    constexpr int32 EXPECTED_DOTS = static_cast<int32>(UE_ARRAY_COUNT(expectedU));
    constexpr int32 DOT_CAPACITY = SOL::HoloMenu::FRAME_MAX_DOTS;
    FVector2f dots[DOT_CAPACITY + HOLOMENUTEST_GUARD];
    HoloMenuFillPoints(dots, DOT_CAPACITY + HOLOMENUTEST_GUARD);
    const int32 dotCount = FrameGapDots(smallRect, HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, dots);
    TestEqual(TEXT("small rect: 10 dots (5 merged gaps)"), dotCount, EXPECTED_DOTS);
    TestTrue(TEXT("small rect: no dot write past the capacity"), HoloMenuPointGuardsIntact(dots, DOT_CAPACITY));
    if (dotCount == EXPECTED_DOTS)
    {
        // Each dot at its expected u; the gap lengths along u are the table's lengths x k where no merge happened
        float dotU[EXPECTED_DOTS] = {};
        for (int32 i = 0; i < EXPECTED_DOTS; ++i)
        {
            TestTrue(FString::Printf(TEXT("dot %d on the outline"), i), HoloMenuPointU(outline, dots[i], dotU[i]));
            TestEqual(FString::Printf(TEXT("dot %d u"), i), dotU[i], expectedU[i], HOLOMENUTEST_PX_TOLERANCE);
        }
        TestEqual(TEXT("gap 1 is 0.5 x 10 px"), dotU[1] - dotU[0], 0.5f * SOL::HoloMenu::FRAME_GAPS[0].LengthPx,
            HOLOMENUTEST_PX_TOLERANCE);
        TestEqual(TEXT("gap 4 is 0.5 x 10 px"), dotU[5] - dotU[4], 0.5f * SOL::HoloMenu::FRAME_GAPS[3].LengthPx,
            HOLOMENUTEST_PX_TOLERANCE);
        TestEqual(TEXT("gap 7 is 0.5 x 12 px"), dotU[9] - dotU[8], 0.5f * SOL::HoloMenu::FRAME_GAPS[6].LengthPx,
            HOLOMENUTEST_PX_TOLERANCE);
        float gapSum = 0.0f;
        for (int32 i = 0; i + 1 < EXPECTED_DOTS; i += 2)
        {
            gapSum += dotU[i + 1] - dotU[i];
        }
        TestEqual(TEXT("small rect: merged gap total"), gapSum, 41.291f, HOLOMENUTEST_PX_TOLERANCE);

        // The visible pieces plus the gaps make up L
        constexpr int32 PIECE_CAPACITY = SOL::HoloMenu::FRAME_MAX_SEGMENTS;
        FSOLHoloSegment pieces[PIECE_CAPACITY + HOLOMENUTEST_GUARD];
        HoloMenuFillPieces(pieces, PIECE_CAPACITY + HOLOMENUTEST_GUARD);
        const int32 pieceCount = FMath::Clamp(FrameDrawSegments(smallRect, HOLOMENUTEST_R_TITLE, 1.0f, 1.0f, pieces), 0,
            PIECE_CAPACITY);
        const float pieceSum = HoloMenuSumLength(pieces, pieceCount);
        TestEqual(TEXT("small rect: visible outline at f = 1"), pieceSum, 39.721f, HOLOMENUTEST_PX_TOLERANCE);
        TestEqual(TEXT("small rect: pieces plus gaps equal L"), pieceSum + gapSum, 81.012f, HOLOMENUTEST_PX_TOLERANCE);
    }

    // Small rect ruler: from y = 16 every 4.5 px, 2 px long, up to y1 - c = 25 (included)
    FSOLHoloSegment ticks[SOL::HoloMenu::FRAME_RULER_TICK_COUNT + HOLOMENUTEST_GUARD];
    HoloMenuFillPieces(ticks, SOL::HoloMenu::FRAME_RULER_TICK_COUNT + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("small rect: 3 ticks"), FrameRulerTicks(smallRect, 1.0f, ticks), 3);
    HoloMenuTestPiece(*this, TEXT("small tick 0"), ticks[0], FVector2f(0.0f, 16.0f), FVector2f(2.0f, 16.0f));
    HoloMenuTestPiece(*this, TEXT("small tick 1"), ticks[1], FVector2f(0.0f, 20.5f), FVector2f(2.0f, 20.5f));
    HoloMenuTestPiece(*this, TEXT("small tick 2"), ticks[2], FVector2f(0.0f, 25.0f), FVector2f(2.0f, 25.0f));

    // Small rect accents: T = a = 5, so the title underline runs the wrong way and both title pieces are skipped
    FSOLHoloSegment accents[SOL::HoloMenu::FRAME_ACCENT_COUNT + HOLOMENUTEST_GUARD];
    HoloMenuFillPieces(accents, SOL::HoloMenu::FRAME_ACCENT_COUNT + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("small rect: 2 accents"), FrameAccents(smallRect, HOLOMENUTEST_R_TITLE, 1.0f, accents), 2);
    HoloMenuTestPiece(*this, TEXT("small accent 1"), accents[0], FVector2f(3.0f, 9.0f), FVector2f(3.0f, 25.0f));
    HoloMenuTestPiece(*this, TEXT("small accent 2"), accents[1], FVector2f(9.0f, 29.0f), FVector2f(7.0f, 29.0f));

    // Wide low rect (0,0)-(400,96) at uiScale 2: k = min(2, 400 / 34, 96 / 64) = 1.5, s = 15, c = 21, T = 200
    const FBox2f lowRect = HoloMenuRect(0.0f, 0.0f, 400.0f, 96.0f);
    HoloMenuFillPieces(ticks, SOL::HoloMenu::FRAME_RULER_TICK_COUNT + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("low rect: 3 ticks"), FrameRulerTicks(lowRect, 2.0f, ticks), 3);
    HoloMenuTestPiece(*this, TEXT("low tick 0"), ticks[0], FVector2f(0.0f, 48.0f), FVector2f(6.0f, 48.0f));
    HoloMenuTestPiece(*this, TEXT("low tick 1"), ticks[1], FVector2f(0.0f, 61.5f), FVector2f(6.0f, 61.5f));
    HoloMenuTestPiece(*this, TEXT("low tick 2"), ticks[2], FVector2f(0.0f, 75.0f), FVector2f(6.0f, 75.0f));
    HoloMenuFillPieces(accents, SOL::HoloMenu::FRAME_ACCENT_COUNT + HOLOMENUTEST_GUARD);
    TestEqual(TEXT("low rect: 4 accents"), FrameAccents(lowRect, HOLOMENUTEST_R_TITLE, 2.0f, accents), 4);
    HoloMenuTestPiece(*this, TEXT("low accent 1"), accents[0], FVector2f(9.0f, 27.0f), FVector2f(9.0f, 75.0f));
    HoloMenuTestPiece(*this, TEXT("low accent 2"), accents[1], FVector2f(376.0f, 87.0f), FVector2f(265.0f, 87.0f));
    HoloMenuTestPiece(*this, TEXT("low underline"), accents[2], FVector2f(18.0f, 51.0f), FVector2f(140.0f, 51.0f));
    HoloMenuTestPiece(*this, TEXT("low dash"), accents[3], FVector2f(152.0f, 51.0f), FVector2f(170.0f, 51.0f));
    TestTrue(TEXT("no tick or accent write past the capacity"),
        HoloMenuPieceGuardsIntact(ticks, SOL::HoloMenu::FRAME_RULER_TICK_COUNT)
        && HoloMenuPieceGuardsIntact(accents, SOL::HoloMenu::FRAME_ACCENT_COUNT));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuExtremeInputsTest, "SOLTest.HoloMenu.ExtremeInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A NaN time finishes a transition at once; uiScale is clamped to [UI_SCALE_MIN, UI_SCALE_MAX]
bool FSOLHoloMenuExtremeInputsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    using namespace SOL::HoloMenu;

    // NaN time: open and close finish at once, never stuck in Opening / Closing
    {
        FSOLHoloMenuAnim anim;
        SetHovered(anim, 1, 0.0);
        TestTrue(TEXT("BeginOpen at NaN returns true"), BeginOpen(anim, HOLOMENUTEST_NAN_D, 1.0f));
        TestTrue(TEXT("BeginOpen at NaN is Open"), anim.Phase == ESOLHoloMenuPhase::Open);
        TestEqual(TEXT("BeginOpen at NaN clears the hover"), anim.HoveredItem, static_cast<int32>(INDEX_NONE));
        TestEqual(TEXT("Open reads 1 afterwards"), Progress(anim, 5.0, 1.0f), 1.0f, HOLOMENUTEST_TOLERANCE);
        TestTrue(TEXT("BeginClose at NaN returns true"), BeginClose(anim, HOLOMENUTEST_NAN_D, 1.0f));
        TestTrue(TEXT("BeginClose at NaN is Closed"), anim.Phase == ESOLHoloMenuPhase::Closed);
        TestEqual(TEXT("Closed reads 0 afterwards"), Progress(anim, 5.0, 1.0f), 0.0f, HOLOMENUTEST_TOLERANCE);
        BeginOpen(anim, 10.0, 1.0f);
        TestTrue(TEXT("BeginClose at NaN from Opening is Closed"), BeginClose(anim, HOLOMENUTEST_NAN_D, 1.0f)
            && anim.Phase == ESOLHoloMenuPhase::Closed);
        BeginOpen(anim, 20.0, 1.0f);
        Advance(anim, 21.0, 1.0f);
        BeginClose(anim, 22.0, 1.0f);
        TestTrue(TEXT("BeginOpen at NaN from Closing is Open"), BeginOpen(anim, HOLOMENUTEST_NAN_D, 1.0f)
            && anim.Phase == ESOLHoloMenuPhase::Open);
    }

    // A huge or tiny uiScale draws exactly as UI_SCALE_MAX / UI_SCALE_MIN
    const FBox2f item = HoloMenuRect(100.0f, 50.0f, 200.0f, 70.0f);
    FSOLHoloSegment hugeMark[HOVER_MAX_SEGMENTS];
    FSOLHoloSegment maxMark[HOVER_MAX_SEGMENTS];
    FVector2f hugeDot = FVector2f::ZeroVector;
    FVector2f maxDot = FVector2f::ZeroVector;
    int32 hugeDots = 0;
    int32 maxDots = 0;
    TestEqual(TEXT("uiScale 1e30: 8 pieces"), HoverMark(item, ESOLHoloHoverMark::RowPointer, 1.0e30f, hugeMark,
        hugeDot, hugeDots), 8);
    HoverMark(item, ESOLHoloHoverMark::RowPointer, UI_SCALE_MAX, maxMark, maxDot, maxDots);
    bool bSameAsMax = hugeDots == maxDots && HoloMenuNear(hugeDot, maxDot, 0.0f);
    for (int32 i = 0; i < HOVER_MAX_SEGMENTS; ++i)
    {
        bSameAsMax &= HoloMenuNear(hugeMark[i].A, maxMark[i].A, 0.0f)
            && HoloMenuNear(hugeMark[i].B, maxMark[i].B, 0.0f);
    }
    TestTrue(TEXT("uiScale 1e30 draws as UI_SCALE_MAX"), bSameAsMax);
    HoloMenuTestPiece(*this, TEXT("uiScale 1e30 run"), hugeMark[6], FVector2f(200.0f, 60.0f),
        FVector2f(200.0f + HOVER_POINTER_RUN_PX * UI_SCALE_MAX, 60.0f));

    FSOLHoloSegment tabTiny[TAB_MAX_SEGMENTS];
    FSOLHoloSegment tabMin[TAB_MAX_SEGMENTS];
    FVector2f tinyDot = FVector2f::ZeroVector;
    FVector2f minDot = FVector2f::ZeroVector;
    int32 tinyDots = 0;
    int32 minDots = 0;
    const FBox2f tab = HoloMenuRect(78.0f, 160.0f, 228.0f, 185.0f);
    TestEqual(TEXT("uiScale 1e-30: 4 tab pieces"), TabMarker(tab, 238.0f, true, 1.0e-30f, tabTiny, tinyDot, tinyDots),
        4);
    TabMarker(tab, 238.0f, true, UI_SCALE_MIN, tabMin, minDot, minDots);
    TestTrue(TEXT("uiScale 1e-30 tab dot as at UI_SCALE_MIN"),
        tinyDots == minDots && HoloMenuNear(tinyDot, minDot, 0.0f));
    HoloMenuTestPoint(*this, TEXT("uiScale 1e-30 tab dot"), tinyDot,
        FVector2f(78.0f - TAB_DOT_OFFSET_PX * UI_SCALE_MIN, 172.5f));

    // DashedDivider at a tiny uiScale over a huge span with a huge opening: period 14 x UI_SCALE_MIN = 1.4, dash 0.9.
    // Dashes 0-6 (y = 0 to 9.3) are written, dash 7 (9.8-10.7) touches the opening [10, 5e5], and the next dash is
    // the first one starting below it, j = 357143 at y = 500000.2; then the capacity (40) is reached
    FSOLHoloSegment dashes[DIVIDER_MAX_DASHES + HOLOMENUTEST_GUARD];
    HoloMenuFillPieces(dashes, DIVIDER_MAX_DASHES + HOLOMENUTEST_GUARD);
    const int32 dashCount = DashedDivider(FVector2f(0.0f, 0.0f), 1.0e6f, 10.0f, 5.0e5f, 1.0e-30f, dashes);
    TestEqual(TEXT("tiny uiScale, huge span: 40 dashes"), dashCount, DIVIDER_MAX_DASHES);
    TestTrue(TEXT("tiny uiScale: no write past the capacity"), HoloMenuPieceGuardsIntact(dashes, DIVIDER_MAX_DASHES));
    const float period = DIVIDER_PERIOD_PX * UI_SCALE_MIN;
    const float dash = DIVIDER_DASH_PX * UI_SCALE_MIN;
    if (dashCount == DIVIDER_MAX_DASHES)
    {
        HoloMenuTestPiece(*this, TEXT("last dash above the opening"), dashes[6], FVector2f(0.0f, 6.0f * period),
            FVector2f(0.0f, 6.0f * period + dash));
        TestEqual(TEXT("first dash below the opening"), dashes[7].A.Y, 500000.2f, 0.1f);
        TestTrue(TEXT("no dash touches the opening"), dashes[7].A.Y > 5.0e5f);
    }
    return true;
}

// --- Duration forms of the anim functions (SDD 46 section 4.5, 46d) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuAnimDurationInstantTest, "SOLTest.HoloMenu.AnimDurationInstant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An open duration of 0, negative or NaN opens at once (progress 1, Advance to Open); a close duration of 0, negative
// or NaN closes at once (progress 0, Advance to Closed); the other duration still times the other phase
bool FSOLHoloMenuAnimDurationInstantTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    const double timed = 0.4;
    const double instant[] = { 0.0, -0.25, HOLOMENUTEST_NAN_D };
    const TCHAR* names[] = { TEXT("0"), TEXT("negative"), TEXT("NaN") };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(instant)); ++i)
    {
        // Instant open, timed close
        FSOLHoloMenuAnim anim;
        const double t0 = 10.0;
        TestTrue(FString::Printf(TEXT("openS %s: BeginOpen returns true"), names[i]),
            BeginOpen(anim, t0, 1.0f, instant[i], timed));
        TestEqual(FString::Printf(TEXT("openS %s: progress 1 at the start"), names[i]),
            Progress(anim, t0, 1.0f, instant[i], timed), 1.0f, HOLOMENUTEST_TOLERANCE);
        Advance(anim, t0, 1.0f, instant[i], timed);
        TestTrue(FString::Printf(TEXT("openS %s: Open after one Advance"), names[i]),
            anim.Phase == ESOLHoloMenuPhase::Open);
        const double t1 = 20.0;
        TestTrue(FString::Printf(TEXT("openS %s: BeginClose returns true"), names[i]),
            BeginClose(anim, t1, 1.0f, instant[i], timed));
        TestEqual(FString::Printf(TEXT("openS %s: the timed close is half way at closeS / 2"), names[i]),
            Progress(anim, t1 + 0.5 * timed, 1.0f, instant[i], timed), 0.5f, HOLOMENUTEST_PROGRESS_TOLERANCE);

        // Timed open, instant close
        FSOLHoloMenuAnim other;
        TestTrue(FString::Printf(TEXT("closeS %s: BeginOpen returns true"), names[i]),
            BeginOpen(other, t0, 1.0f, timed, instant[i]));
        TestEqual(FString::Printf(TEXT("closeS %s: the timed open is half way at openS / 2"), names[i]),
            Progress(other, t0 + 0.5 * timed, 1.0f, timed, instant[i]), 0.5f, HOLOMENUTEST_PROGRESS_TOLERANCE);
        Advance(other, t0 + timed + HOLOMENUTEST_TIME_EPS_S, 1.0f, timed, instant[i]);
        TestTrue(FString::Printf(TEXT("closeS %s: Open after openS"), names[i]),
            other.Phase == ESOLHoloMenuPhase::Open);
        TestTrue(FString::Printf(TEXT("closeS %s: BeginClose returns true"), names[i]),
            BeginClose(other, t1, 1.0f, timed, instant[i]));
        TestEqual(FString::Printf(TEXT("closeS %s: progress 0 at the start"), names[i]),
            Progress(other, t1, 1.0f, timed, instant[i]), 0.0f, HOLOMENUTEST_TOLERANCE);
        Advance(other, t1, 1.0f, timed, instant[i]);
        TestTrue(FString::Printf(TEXT("closeS %s: Closed after one Advance"), names[i]),
            other.Phase == ESOLHoloMenuPhase::Closed);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuAnimDurationDefaultTest, "SOLTest.HoloMenu.AnimDurationDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The three-argument forms are the duration forms at OPEN_S and CLOSE_S: the same return values, phases and progress
// through an open, a reversal mid-close and a close, at strength 1 and 0
bool FSOLHoloMenuAnimDurationDefaultTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHoloMenu;
    const double openS = static_cast<double>(SOL::HoloMenu::OPEN_S);
    const double closeS = static_cast<double>(SOL::HoloMenu::CLOSE_S);
    const float strengths[] = { 1.0f, 0.0f };
    for (const float s : strengths)
    {
        FSOLHoloMenuAnim a;
        FSOLHoloMenuAnim b;
        // Compares the two anims' phases and progress at a time
        const auto same = [&](const TCHAR* what, const double nowS)
        {
            TestTrue(FString::Printf(TEXT("s %.0f, %s: same phase"), s, what), a.Phase == b.Phase);
            TestEqual(FString::Printf(TEXT("s %.0f, %s: same progress"), s, what), Progress(a, nowS, s),
                Progress(b, nowS, s, openS, closeS), HOLOMENUTEST_TOLERANCE);
        };

        // Open, half way, then finished
        const double t0 = 5.0;
        TestEqual(TEXT("BeginOpen returns the same"), BeginOpen(a, t0, s), BeginOpen(b, t0, s, openS, closeS));
        same(TEXT("open start"), t0);
        same(TEXT("open mid"), t0 + 0.5 * openS);
        Advance(a, t0 + openS + HOLOMENUTEST_TIME_EPS_S, s);
        Advance(b, t0 + openS + HOLOMENUTEST_TIME_EPS_S, s, openS, closeS);
        same(TEXT("open end"), t0 + openS + HOLOMENUTEST_TIME_EPS_S);

        // Close, reopened mid-way (the reversal starts from the close's progress), then closed for good
        const double t1 = 10.0;
        TestEqual(TEXT("BeginClose returns the same"), BeginClose(a, t1, s), BeginClose(b, t1, s, openS, closeS));
        same(TEXT("close mid"), t1 + 0.5 * closeS);
        const double t2 = t1 + 0.5 * closeS;
        TestEqual(TEXT("BeginOpen mid-close returns the same"), BeginOpen(a, t2, s),
            BeginOpen(b, t2, s, openS, closeS));
        same(TEXT("reversal start"), t2);
        same(TEXT("reversal mid"), t2 + 0.25 * openS);
        const double t3 = 20.0;
        Advance(a, t3, s);
        Advance(b, t3, s, openS, closeS);
        TestEqual(TEXT("BeginClose again returns the same"), BeginClose(a, t3, s), BeginClose(b, t3, s, openS, closeS));
        Advance(a, t3 + closeS + HOLOMENUTEST_TIME_EPS_S, s);
        Advance(b, t3 + closeS + HOLOMENUTEST_TIME_EPS_S, s, openS, closeS);
        same(TEXT("close end"), t3 + closeS + HOLOMENUTEST_TIME_EPS_S);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
