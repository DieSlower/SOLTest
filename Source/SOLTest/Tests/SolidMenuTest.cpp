/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuMath.h"
#include "SOLConstants.h"
#include "UI/SOLHoloMenuMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #46 solid menu pure logic (Docs/SDDs/46-solid-menus.md section 8, constants section 9), written
// against the Menu/SOLSolidMenuMath.h contract before the production code existed. Every number comes from
// SOL::SolidMenu, never a literal. Hull points are in the widget's local space: origin top-left, x right, y down,
// the rect spans [0, size.X] x [0, size.Y]
namespace
{
    constexpr float SOLIDMENUTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float SOLIDMENUTEST_INF = std::numeric_limits<float>::infinity();
    constexpr double SOLIDMENUTEST_NAN_D = std::numeric_limits<double>::quiet_NaN();
    constexpr double SOLIDMENUTEST_INF_D = std::numeric_limits<double>::infinity();

    // Tolerances
    constexpr float SOLIDMENUTEST_TOLERANCE = 1.0e-4f;          // Curve and ramp values
    constexpr float SOLIDMENUTEST_SDF_TOLERANCE = 1.0e-3f;      // Hull distance (SDD 8.1)
    constexpr float SOLIDMENUTEST_PX_TOLERANCE = 1.0e-3f;       // Polyline points and lengths
    constexpr float SOLIDMENUTEST_QUANT_TOLERANCE = 1.0f / 255.0f + 1.0e-6f;   // One 8-bit step (SDD 8.3)
    constexpr double SOLIDMENUTEST_SETTLED_S = 5.0;             // Long past every ramp and linger

    // Output guard for the polyline: entries past the capacity are pre-filled with a sentinel that must survive
    constexpr int32 SOLIDMENUTEST_OUT_SIZE = 8;
    constexpr float SOLIDMENUTEST_SENTINEL = -98765.25f;

    // The reference hull of SDD 9 (TL / TR / BR / BL chamfers) on a 400 x 300 rect
    constexpr float SOLIDMENUTEST_W = 400.0f;
    constexpr float SOLIDMENUTEST_H = 300.0f;
    constexpr float SOLIDMENUTEST_TL = 28.0f;
    constexpr float SOLIDMENUTEST_TR = 12.0f;
    constexpr float SOLIDMENUTEST_BR = 28.0f;
    constexpr float SOLIDMENUTEST_BL = 12.0f;
    constexpr float SOLIDMENUTEST_SQRT2 = 1.41421356f;

    constexpr ESOLSolidButtonState SOLIDMENUTEST_STATES[] = {
        ESOLSolidButtonState::Normal, ESOLSolidButtonState::Hovered, ESOLSolidButtonState::Pressed,
        ESOLSolidButtonState::Selected, ESOLSolidButtonState::Disabled };

    // Counts failures of a sweep and keeps the first one's description (avoids flooding the log)
    struct FSOLSolidMenuSweepLog
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

        // Reports the sweep as one assertion
        void Report(FAutomationTestBase& test, const TCHAR* what) const
        {
            test.TestEqual(FString::Printf(TEXT("%s (first failure: %s)"), what, *First), Failures, 0);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the sanitized effects strength (SDD 46 section 8: NaN 0, clamped to the HUD effects range [0, 2]); the
    // glow halo is this x Edge, so it doubles at strength 2 (SDD 46 section 7, Q2)
    float SolidMenuTestStrength(const float s)
    {
        return FMath::IsNaN(s) ? 0.0f : FMath::Clamp(s, SOL::Settings::HUD_EFFECTS_MIN, SOL::Settings::HUD_EFFECTS_MAX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a hull with only the four chamfers set (no step, notch or chin, not a recess)
    FSOLSolidHull SolidMenuTestHull(const float tl, const float tr, const float br, const float bl)
    {
        FSOLSolidHull hull{};
        hull.ChamferTL = tl;
        hull.ChamferTR = tr;
        hull.ChamferBR = br;
        hull.ChamferBL = bl;
        hull.TitleStepWidth = 0.0f;
        hull.TitleStepHeight = 0.0f;
        hull.NotchY = -1.0f;
        hull.NotchDepth = 0.0f;
        hull.NotchHeight = 0.0f;
        hull.ChinWidth = 0.0f;
        hull.ChinHeight = 0.0f;
        hull.bRecess = false;
        return hull;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the reference hull (SDD 9 chamfers)
    FSOLSolidHull SolidMenuTestReferenceHull()
    {
        return SolidMenuTestHull(SOLIDMENUTEST_TL, SOLIDMENUTEST_TR, SOLIDMENUTEST_BR, SOLIDMENUTEST_BL);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the reference rect size
    FVector2f SolidMenuTestSize()
    {
        return FVector2f(SOLIDMENUTEST_W, SOLIDMENUTEST_H);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks every field of two hulls is equal within tolerance (NotchY only when bCompareNotchY: a "no notch" hull may
    // spell its NotchY < 0 sentinel either way)
    void SolidMenuTestHullEqual(FAutomationTestBase& test, const FString& label, const FSOLSolidHull& actual,
        const FSOLSolidHull& expected, const float tolerance, const bool bCompareNotchY = true)
    {
        test.TestEqual(label + TEXT(" ChamferTL"), actual.ChamferTL, expected.ChamferTL, tolerance);
        test.TestEqual(label + TEXT(" ChamferTR"), actual.ChamferTR, expected.ChamferTR, tolerance);
        test.TestEqual(label + TEXT(" ChamferBR"), actual.ChamferBR, expected.ChamferBR, tolerance);
        test.TestEqual(label + TEXT(" ChamferBL"), actual.ChamferBL, expected.ChamferBL, tolerance);
        test.TestEqual(label + TEXT(" TitleStepWidth"), actual.TitleStepWidth, expected.TitleStepWidth, tolerance);
        test.TestEqual(label + TEXT(" TitleStepHeight"), actual.TitleStepHeight, expected.TitleStepHeight, tolerance);
        if (bCompareNotchY)
        {
            test.TestEqual(label + TEXT(" NotchY"), actual.NotchY, expected.NotchY, tolerance);
        }
        test.TestEqual(label + TEXT(" NotchDepth"), actual.NotchDepth, expected.NotchDepth, tolerance);
        test.TestEqual(label + TEXT(" NotchHeight"), actual.NotchHeight, expected.NotchHeight, tolerance);
        test.TestEqual(label + TEXT(" ChinWidth"), actual.ChinWidth, expected.ChinWidth, tolerance);
        test.TestEqual(label + TEXT(" ChinHeight"), actual.ChinHeight, expected.ChinHeight, tolerance);
        test.TestTrue(label + TEXT(" bRecess"), actual.bRecess == expected.bRecess);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the four chamfers keep every edge's pair within HULL_MAX_CHAMFER_FRACTION of that edge
    void SolidMenuTestChamferLimits(FAutomationTestBase& test, const FString& label, const FSOLSolidHull& hull,
        const FVector2f size)
    {
        const float fraction = SOL::SolidMenu::HULL_MAX_CHAMFER_FRACTION;
        const float tol = SOLIDMENUTEST_SDF_TOLERANCE;
        test.TestTrue(label + TEXT(": top pair within the limit"),
            hull.ChamferTL + hull.ChamferTR <= fraction * size.X + tol);
        test.TestTrue(label + TEXT(": right pair within the limit"),
            hull.ChamferTR + hull.ChamferBR <= fraction * size.Y + tol);
        test.TestTrue(label + TEXT(": bottom pair within the limit"),
            hull.ChamferBR + hull.ChamferBL <= fraction * size.X + tol);
        test.TestTrue(label + TEXT(": left pair within the limit"),
            hull.ChamferBL + hull.ChamferTL <= fraction * size.Y + tol);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether every float field of a hull is finite
    bool SolidMenuTestHullFinite(const FSOLSolidHull& hull)
    {
        return FMath::IsFinite(hull.ChamferTL) && FMath::IsFinite(hull.ChamferTR) && FMath::IsFinite(hull.ChamferBR)
            && FMath::IsFinite(hull.ChamferBL) && FMath::IsFinite(hull.TitleStepWidth)
            && FMath::IsFinite(hull.TitleStepHeight) && FMath::IsFinite(hull.NotchY)
            && FMath::IsFinite(hull.NotchDepth) && FMath::IsFinite(hull.NotchHeight)
            && FMath::IsFinite(hull.ChinWidth) && FMath::IsFinite(hull.ChinHeight);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the midpoint of a corner's chamfer diagonal on a size rect (corner 0 TL, 1 TR, 2 BR, 3 BL)
    FVector2f SolidMenuTestDiagonalMid(const int32 corner, const float chamfer, const FVector2f size)
    {
        const float half = 0.5f * chamfer;
        switch (corner)
        {
        case 0:
            return FVector2f(half, half);
        case 1:
            return FVector2f(size.X - half, half);
        case 2:
            return FVector2f(size.X - half, size.Y - half);
        default:
            return FVector2f(half, size.Y - half);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the outward unit normal of a corner's chamfer diagonal (corner 0 TL, 1 TR, 2 BR, 3 BL)
    FVector2f SolidMenuTestDiagonalNormal(const int32 corner)
    {
        const float k = 1.0f / SOLIDMENUTEST_SQRT2;
        switch (corner)
        {
        case 0:
            return FVector2f(-k, -k);
        case 1:
            return FVector2f(k, -k);
        case 2:
            return FVector2f(k, k);
        default:
            return FVector2f(-k, k);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rect corner point (corner 0 TL, 1 TR, 2 BR, 3 BL)
    FVector2f SolidMenuTestCorner(const int32 corner, const FVector2f size)
    {
        switch (corner)
        {
        case 0:
            return FVector2f(0.0f, 0.0f);
        case 1:
            return FVector2f(size.X, 0.0f);
        case 2:
            return FVector2f(size.X, size.Y);
        default:
            return FVector2f(0.0f, size.Y);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a hull's chamfer by corner index (0 TL, 1 TR, 2 BR, 3 BL)
    float SolidMenuTestChamfer(const FSOLSolidHull& hull, const int32 corner)
    {
        switch (corner)
        {
        case 0:
            return hull.ChamferTL;
        case 1:
            return hull.ChamferTR;
        case 2:
            return hull.ChamferBR;
        default:
            return hull.ChamferBL;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the five animated look fields all equal value within tolerance
    bool SolidMenuTestLookAll(const FSOLSolidButtonLook& look, const float value, const float tolerance)
    {
        return FMath::IsNearlyEqual(look.Lift, value, tolerance) && FMath::IsNearlyEqual(look.Edge, value, tolerance)
            && FMath::IsNearlyEqual(look.Led, value, tolerance)
            && FMath::IsNearlyEqual(look.Underline, value, tolerance)
            && FMath::IsNearlyEqual(look.Trace, value, tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the five animated look fields all equal value
    void SolidMenuTestExpectLookAll(FAutomationTestBase& test, const FString& label, const FSOLSolidButtonLook& look,
        const float value)
    {
        test.TestEqual(label + TEXT(" Lift"), look.Lift, value, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Edge"), look.Edge, value, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Led"), look.Led, value, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Underline"), look.Underline, value, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Trace"), look.Trace, value, SOLIDMENUTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks two looks are equal field by field
    void SolidMenuTestLookEqual(FAutomationTestBase& test, const FString& label, const FSOLSolidButtonLook& actual,
        const FSOLSolidButtonLook& expected)
    {
        test.TestEqual(label + TEXT(" Lift"), actual.Lift, expected.Lift, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Edge"), actual.Edge, expected.Edge, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Led"), actual.Led, expected.Led, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Underline"), actual.Underline, expected.Underline, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Trace"), actual.Trace, expected.Trace, SOLIDMENUTEST_TOLERANCE);
        test.TestEqual(label + TEXT(" Glow"), actual.Glow, expected.Glow, SOLIDMENUTEST_TOLERANCE);
        test.TestTrue(label + TEXT(" bPressedHull"), actual.bPressedHull == expected.bPressedHull);
        test.TestTrue(label + TEXT(" bSelectedMark"), actual.bSelectedMark == expected.bSelectedMark);
        test.TestTrue(label + TEXT(" bDisabled"), actual.bDisabled == expected.bDisabled);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the length of a polyline
    float SolidMenuTestPolylineLength(const FVector2f* points, const int32 count)
    {
        float length = 0.0f;
        for (int32 index = 1; index < count; ++index)
        {
            length += FVector2f::Distance(points[index - 1], points[index]);
        }
        return length;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the polyline output buffer with the sentinel
    void SolidMenuTestFillSentinel(FVector2f (&out)[SOLIDMENUTEST_OUT_SIZE])
    {
        for (FVector2f& point : out)
        {
            point = FVector2f(SOLIDMENUTEST_SENTINEL, SOLIDMENUTEST_SENTINEL);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many entries from first on still hold the sentinel
    int32 SolidMenuTestSentinelsFrom(const FVector2f (&out)[SOLIDMENUTEST_OUT_SIZE], const int32 first)
    {
        int32 intact = 0;
        for (int32 index = first; index < SOLIDMENUTEST_OUT_SIZE; ++index)
        {
            if (out[index].X == SOLIDMENUTEST_SENTINEL && out[index].Y == SOLIDMENUTEST_SENTINEL)
            {
                ++intact;
            }
        }
        return intact;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a point against its expected coordinates
    void SolidMenuTestPointEqual(FAutomationTestBase& test, const FString& label, const FVector2f actual,
        const FVector2f expected)
    {
        test.TestEqual(label + TEXT(".x"), actual.X, expected.X, SOLIDMENUTEST_PX_TOLERANCE);
        test.TestEqual(label + TEXT(".y"), actual.Y, expected.Y, SOLIDMENUTEST_PX_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a value lies on the 8-bit grid k / 255
    bool SolidMenuTestOn8BitGrid(const float value)
    {
        const float scaled = value * 255.0f;
        return FMath::Abs(scaled - FMath::RoundToFloat(scaled)) <= 1.0e-3f;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Constants (SDD 9): the tests below divide and step by these, so they must be usable
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuConstantsTest, "SOLTest.SolidMenu.Constants.Sane",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every timing is positive, every fraction and alpha lies in (0, 1]
bool FSOLSolidMenuConstantsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    TestTrue(TEXT("HOVER_LIFT_PX > 0"), HOVER_LIFT_PX > 0.0f);
    TestTrue(TEXT("HOVER_RAMP_S > 0"), HOVER_RAMP_S > 0.0f);
    TestTrue(TEXT("PRESSED_LINGER_S > 0"), PRESSED_LINGER_S > 0.0f);
    TestTrue(TEXT("PANEL_OPEN_S > 0"), PANEL_OPEN_S > 0.0f);
    TestTrue(TEXT("PANEL_SLIDE_PX > 0"), PANEL_SLIDE_PX > 0.0f);
    TestTrue(TEXT("PAUSE_SLIDE_S > 0"), PAUSE_SLIDE_S > 0.0f);
    TestTrue(TEXT("PAUSE_SLIDE_MARGIN_PX >= 0"), PAUSE_SLIDE_MARGIN_PX >= 0.0f);
    TestTrue(TEXT("PAUSE_DIM_ALPHA in (0, 1]"), PAUSE_DIM_ALPHA > 0.0f && PAUSE_DIM_ALPHA <= 1.0f);
    TestTrue(TEXT("TRACE_POWER_DELAY_S > 0"), TRACE_POWER_DELAY_S > 0.0f);
    TestTrue(TEXT("TRACE_POWER_S > 0"), TRACE_POWER_S > 0.0f);
    TestTrue(TEXT("IDLE_PULSE_PERIOD_S > 0"), IDLE_PULSE_PERIOD_S > 0.0f);
    TestTrue(TEXT("IDLE_PULSE_DEPTH in (0, 0.5)"), IDLE_PULSE_DEPTH > 0.0f && IDLE_PULSE_DEPTH < 0.5f);
    TestTrue(TEXT("CLOSE_S > 0"), CLOSE_S > 0.0f);
    TestTrue(TEXT("HULL_MAX_CHAMFER_FRACTION in (0, 1]"),
        HULL_MAX_CHAMFER_FRACTION > 0.0f && HULL_MAX_CHAMFER_FRACTION <= 1.0f);
    TestTrue(TEXT("DISABLED_ALPHA in (0, 1)"), DISABLED_ALPHA > 0.0f && DISABLED_ALPHA < 1.0f);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hull (SDD 8.1)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullClampNoOpTest, "SOLTest.SolidMenu.Hull.ClampKeepsValidHull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A hull whose chamfers, step, notch and chin all fit comfortably comes back unchanged
bool FSOLSolidMenuHullClampNoOpTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSolidHull plain = SolidMenuTestReferenceHull();
    const FSOLSolidHull plainClamped = SOLSolidMenu::ClampHull(plain, SolidMenuTestSize());
    SolidMenuTestHullEqual(*this, TEXT("plain reference hull"), plainClamped, plain, SOLIDMENUTEST_SDF_TOLERANCE,
        false);
    TestTrue(TEXT("plain reference hull has no notch"), plainClamped.NotchY < 0.0f || plainClamped.NotchDepth == 0.0f
        || plainClamped.NotchHeight == 0.0f);

    FSOLSolidHull full = SolidMenuTestReferenceHull();
    full.TitleStepWidth = 120.0f;
    full.TitleStepHeight = 10.0f;
    full.NotchY = 150.0f;
    full.NotchDepth = 6.0f;
    full.NotchHeight = 20.0f;
    full.ChinWidth = 120.0f;
    full.ChinHeight = 10.0f;
    SolidMenuTestHullEqual(*this, TEXT("hull with step, notch and chin"),
        SOLSolidMenu::ClampHull(full, SolidMenuTestSize()), full, SOLIDMENUTEST_SDF_TOLERANCE);

    FSOLSolidHull recess = SolidMenuTestReferenceHull();
    recess.bRecess = true;
    TestTrue(TEXT("the recess flag survives"), SOLSolidMenu::ClampHull(recess, SolidMenuTestSize()).bRecess);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullClampChamfersTest, "SOLTest.SolidMenu.Hull.ClampChamfersPerEdge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Chamfers too big for an edge are scaled down together (one factor, ratios kept) until the tightest edge's pair sums
// to exactly HULL_MAX_CHAMFER_FRACTION of that edge, and no edge exceeds it
bool FSOLSolidMenuHullClampChamfersTest::RunTest(const FString& /*parameters*/)
{
    const float fraction = SOL::SolidMenu::HULL_MAX_CHAMFER_FRACTION;

    // Equal chamfers on a wide short rect: the left and right edges are the tight ones
    {
        const FVector2f size(100.0f, 60.0f);
        const FSOLSolidHull clamped = SOLSolidMenu::ClampHull(SolidMenuTestHull(40.0f, 40.0f, 40.0f, 40.0f), size);
        SolidMenuTestChamferLimits(*this, TEXT("equal chamfers"), clamped, size);
        const float expected = 0.5f * fraction * size.Y;
        TestEqual(TEXT("equal chamfers: TL scaled to the side limit"), clamped.ChamferTL, expected,
            SOLIDMENUTEST_SDF_TOLERANCE);
        TestEqual(TEXT("equal chamfers: TR equal"), clamped.ChamferTR, expected, SOLIDMENUTEST_SDF_TOLERANCE);
        TestEqual(TEXT("equal chamfers: BR equal"), clamped.ChamferBR, expected, SOLIDMENUTEST_SDF_TOLERANCE);
        TestEqual(TEXT("equal chamfers: BL equal"), clamped.ChamferBL, expected, SOLIDMENUTEST_SDF_TOLERANCE);
    }

    // Unequal chamfers on a narrow tall rect: the top and bottom edges are the tight ones, ratios are kept
    {
        const FVector2f size(100.0f, 200.0f);
        const FSOLSolidHull input = SolidMenuTestHull(50.0f, 10.0f, 50.0f, 10.0f);
        const FSOLSolidHull clamped = SOLSolidMenu::ClampHull(input, size);
        SolidMenuTestChamferLimits(*this, TEXT("unequal chamfers"), clamped, size);
        const float k = fraction * size.X / (input.ChamferTL + input.ChamferTR);
        TestEqual(TEXT("unequal chamfers: TL scaled by the common factor"), clamped.ChamferTL, input.ChamferTL * k,
            SOLIDMENUTEST_SDF_TOLERANCE);
        TestEqual(TEXT("unequal chamfers: TR scaled by the common factor"), clamped.ChamferTR, input.ChamferTR * k,
            SOLIDMENUTEST_SDF_TOLERANCE);
        TestEqual(TEXT("unequal chamfers: BR scaled by the common factor"), clamped.ChamferBR, input.ChamferBR * k,
            SOLIDMENUTEST_SDF_TOLERANCE);
        TestEqual(TEXT("unequal chamfers: BL scaled by the common factor"), clamped.ChamferBL, input.ChamferBL * k,
            SOLIDMENUTEST_SDF_TOLERANCE);
        TestEqual(TEXT("unequal chamfers: the top pair is tight"), clamped.ChamferTL + clamped.ChamferTR,
            fraction * size.X, SOLIDMENUTEST_SDF_TOLERANCE);
    }

    // Only one corner too big: the factor still applies to all four
    {
        const FVector2f size(SOLIDMENUTEST_W, SOLIDMENUTEST_H);
        const FSOLSolidHull input = SolidMenuTestHull(400.0f, 12.0f, 28.0f, 12.0f);
        const FSOLSolidHull clamped = SOLSolidMenu::ClampHull(input, size);
        SolidMenuTestChamferLimits(*this, TEXT("one big corner"), clamped, size);
        TestTrue(TEXT("one big corner: TL reduced"), clamped.ChamferTL < input.ChamferTL);
        TestEqual(TEXT("one big corner: TR keeps its ratio to TL"), clamped.ChamferTR * input.ChamferTL,
            input.ChamferTR * clamped.ChamferTL, SOLIDMENUTEST_SDF_TOLERANCE * input.ChamferTL);
        TestEqual(TEXT("one big corner: BR keeps its ratio to TL"), clamped.ChamferBR * input.ChamferTL,
            input.ChamferBR * clamped.ChamferTL, SOLIDMENUTEST_SDF_TOLERANCE * input.ChamferTL);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullClampInvalidTest, "SOLTest.SolidMenu.Hull.ClampNaNAndNegative",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Negative or NaN values read 0
bool FSOLSolidMenuHullClampInvalidTest::RunTest(const FString& /*parameters*/)
{
    FSOLSolidHull input = SolidMenuTestHull(SOLIDMENUTEST_NAN, -5.0f, SOLIDMENUTEST_BR, SOLIDMENUTEST_NAN);
    input.TitleStepWidth = SOLIDMENUTEST_NAN;
    input.TitleStepHeight = -3.0f;
    input.NotchY = 150.0f;
    input.NotchDepth = SOLIDMENUTEST_NAN;
    input.NotchHeight = -2.0f;
    input.ChinWidth = -1.0f;
    input.ChinHeight = SOLIDMENUTEST_NAN;
    const FSOLSolidHull clamped = SOLSolidMenu::ClampHull(input, SolidMenuTestSize());

    TestEqual(TEXT("NaN TL reads 0"), clamped.ChamferTL, 0.0f);
    TestEqual(TEXT("negative TR reads 0"), clamped.ChamferTR, 0.0f);
    TestEqual(TEXT("valid BR kept"), clamped.ChamferBR, SOLIDMENUTEST_BR, SOLIDMENUTEST_SDF_TOLERANCE);
    TestEqual(TEXT("NaN BL reads 0"), clamped.ChamferBL, 0.0f);
    TestEqual(TEXT("NaN step width reads 0"), clamped.TitleStepWidth, 0.0f);
    TestEqual(TEXT("negative step height reads 0"), clamped.TitleStepHeight, 0.0f);
    TestEqual(TEXT("NaN notch depth reads 0"), clamped.NotchDepth, 0.0f);
    TestEqual(TEXT("negative notch height reads 0"), clamped.NotchHeight, 0.0f);
    TestEqual(TEXT("negative chin width reads 0"), clamped.ChinWidth, 0.0f);
    TestEqual(TEXT("NaN chin height reads 0"), clamped.ChinHeight, 0.0f);
    TestTrue(TEXT("every field finite"), SolidMenuTestHullFinite(clamped));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullClampSizeTest, "SOLTest.SolidMenu.Hull.ClampInvalidSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A size with a component <= 0 or non-finite returns a hull with every value 0
bool FSOLSolidMenuHullClampSizeTest::RunTest(const FString& /*parameters*/)
{
    FSOLSolidHull input = SolidMenuTestReferenceHull();
    input.TitleStepWidth = 120.0f;
    input.TitleStepHeight = 10.0f;
    input.NotchY = 150.0f;
    input.NotchDepth = 6.0f;
    input.NotchHeight = 20.0f;
    input.ChinWidth = 120.0f;
    input.ChinHeight = 10.0f;

    const FVector2f sizes[] = {
        FVector2f(0.0f, SOLIDMENUTEST_H), FVector2f(SOLIDMENUTEST_W, 0.0f), FVector2f(-10.0f, SOLIDMENUTEST_H),
        FVector2f(SOLIDMENUTEST_W, -1.0f), FVector2f(SOLIDMENUTEST_NAN, SOLIDMENUTEST_H),
        FVector2f(SOLIDMENUTEST_W, SOLIDMENUTEST_NAN), FVector2f(SOLIDMENUTEST_INF, SOLIDMENUTEST_H),
        FVector2f(SOLIDMENUTEST_W, -SOLIDMENUTEST_INF), FVector2f(0.0f, 0.0f) };
    for (const FVector2f& size : sizes)
    {
        const FSOLSolidHull clamped = SOLSolidMenu::ClampHull(input, size);
        const FString label = FString::Printf(TEXT("size (%f, %f)"), size.X, size.Y);
        TestEqual(label + TEXT(" ChamferTL"), clamped.ChamferTL, 0.0f);
        TestEqual(label + TEXT(" ChamferTR"), clamped.ChamferTR, 0.0f);
        TestEqual(label + TEXT(" ChamferBR"), clamped.ChamferBR, 0.0f);
        TestEqual(label + TEXT(" ChamferBL"), clamped.ChamferBL, 0.0f);
        TestEqual(label + TEXT(" TitleStepWidth"), clamped.TitleStepWidth, 0.0f);
        TestEqual(label + TEXT(" TitleStepHeight"), clamped.TitleStepHeight, 0.0f);
        TestEqual(label + TEXT(" NotchY"), clamped.NotchY, 0.0f);
        TestEqual(label + TEXT(" NotchDepth"), clamped.NotchDepth, 0.0f);
        TestEqual(label + TEXT(" NotchHeight"), clamped.NotchHeight, 0.0f);
        TestEqual(label + TEXT(" ChinWidth"), clamped.ChinWidth, 0.0f);
        TestEqual(label + TEXT(" ChinHeight"), clamped.ChinHeight, 0.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullClampFeaturesTest, "SOLTest.SolidMenu.Hull.ClampStepNotchChin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Oversized title step, notches and chin are clamped inside the rect; the result is finite, non-negative and a second
// clamp changes nothing
bool FSOLSolidMenuHullClampFeaturesTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f size = SolidMenuTestSize();
    FSOLSolidHull input = SolidMenuTestReferenceHull();
    input.TitleStepWidth = 1.0e5f;
    input.TitleStepHeight = 1.0e5f;
    input.NotchY = 1.0e5f;
    input.NotchDepth = 1.0e5f;
    input.NotchHeight = 1.0e5f;
    input.ChinWidth = 1.0e5f;
    input.ChinHeight = 1.0e5f;
    const FSOLSolidHull clamped = SOLSolidMenu::ClampHull(input, size);

    TestTrue(TEXT("every field finite"), SolidMenuTestHullFinite(clamped));
    SolidMenuTestChamferLimits(*this, TEXT("oversized features"), clamped, size);
    TestTrue(TEXT("step width inside the rect"), clamped.TitleStepWidth >= 0.0f && clamped.TitleStepWidth <= size.X);
    TestTrue(TEXT("step height inside the rect"),
        clamped.TitleStepHeight >= 0.0f && clamped.TitleStepHeight <= size.Y);
    TestTrue(TEXT("chin width inside the rect"), clamped.ChinWidth >= 0.0f && clamped.ChinWidth <= size.X);
    TestTrue(TEXT("chin height inside the rect"), clamped.ChinHeight >= 0.0f && clamped.ChinHeight <= size.Y);
    TestTrue(TEXT("notch depth inside the rect"), clamped.NotchDepth >= 0.0f && clamped.NotchDepth <= size.X);
    TestTrue(TEXT("notch height inside the rect"), clamped.NotchHeight >= 0.0f && clamped.NotchHeight <= size.Y);
    TestTrue(TEXT("notch Y inside the rect (or the notch removed)"),
        clamped.NotchY < 0.0f || clamped.NotchY <= size.Y);

    SolidMenuTestHullEqual(*this, TEXT("clamping twice"), SOLSolidMenu::ClampHull(clamped, size), clamped,
        SOLIDMENUTEST_SDF_TOLERANCE);

    const FSOLSolidHull plainClamped = SOLSolidMenu::ClampHull(SolidMenuTestHull(400.0f, 60.0f, 5.0f, 80.0f), size);
    SolidMenuTestHullEqual(*this, TEXT("clamping scaled chamfers twice"), SOLSolidMenu::ClampHull(plainClamped, size),
        plainClamped, SOLIDMENUTEST_SDF_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullNotchTest, "SOLTest.SolidMenu.Hull.NotchClampAvoidsChamfers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A notch as tall and deep as the rect is clamped so it never cuts a chamfer: every chamfer diagonal stays on the edge;
// NotchY < 0 means no notch, so the side edges stay whole
bool FSOLSolidMenuHullNotchTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f size = SolidMenuTestSize();

    FSOLSolidHull big = SolidMenuTestReferenceHull();
    big.NotchY = 0.5f * size.Y;
    big.NotchDepth = size.X;
    big.NotchHeight = size.Y;
    const FSOLSolidHull clamped = SOLSolidMenu::ClampHull(big, size);
    for (int32 corner = 0; corner < 4; ++corner)
    {
        const float chamfer = SolidMenuTestChamfer(clamped, corner);
        const FVector2f mid = SolidMenuTestDiagonalMid(corner, chamfer, size);
        TestEqual(FString::Printf(TEXT("corner %d diagonal midpoint stays on the edge"), corner),
            SOLSolidMenu::HullDistance(mid, size, clamped), 0.0f, SOLIDMENUTEST_SDF_TOLERANCE);
    }

    FSOLSolidHull none = SolidMenuTestReferenceHull();
    none.NotchY = -1.0f;
    none.NotchDepth = 6.0f;
    none.NotchHeight = 20.0f;
    const FSOLSolidHull noneClamped = SOLSolidMenu::ClampHull(none, size);
    TestTrue(TEXT("NotchY < 0 stays no notch"), noneClamped.NotchY < 0.0f || noneClamped.NotchDepth == 0.0f
        || noneClamped.NotchHeight == 0.0f);
    FSOLSolidMenuSweepLog log;
    for (float y = SOLIDMENUTEST_TL + 1.0f; y < size.Y - SOLIDMENUTEST_BR - 1.0f; y += 7.0f)
    {
        const float left = SOLSolidMenu::HullDistance(FVector2f(0.0f, y), size, noneClamped);
        const float right = SOLSolidMenu::HullDistance(FVector2f(size.X, y), size, noneClamped);
        if (!FMath::IsNearlyEqual(left, 0.0f, SOLIDMENUTEST_SDF_TOLERANCE)
            || !FMath::IsNearlyEqual(right, 0.0f, SOLIDMENUTEST_SDF_TOLERANCE))
        {
            log.Fail(FString::Printf(TEXT("y %f: left %f right %f"), y, left, right));
        }
    }
    log.Report(*this, TEXT("with no notch the side edges are whole"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullEdgesTest, "SOLTest.SolidMenu.Hull.DistanceStraightEdges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The SDF is 0 on the straight edges, negative inside and positive outside, exact as a distance near each edge
bool FSOLSolidMenuHullEdgesTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f size = SolidMenuTestSize();
    const FSOLSolidHull hull = SolidMenuTestReferenceHull();
    const float tol = SOLIDMENUTEST_SDF_TOLERANCE;

    // Edge midpoints and their outward normals: top, right, bottom, left
    const FVector2f mids[] = {
        FVector2f(0.5f * size.X, 0.0f), FVector2f(size.X, 0.5f * size.Y), FVector2f(0.5f * size.X, size.Y),
        FVector2f(0.0f, 0.5f * size.Y) };
    const FVector2f normals[] = {
        FVector2f(0.0f, -1.0f), FVector2f(1.0f, 0.0f), FVector2f(0.0f, 1.0f), FVector2f(-1.0f, 0.0f) };
    const TCHAR* names[] = { TEXT("top"), TEXT("right"), TEXT("bottom"), TEXT("left") };
    for (int32 edge = 0; edge < 4; ++edge)
    {
        const FString name = names[edge];
        TestEqual(name + TEXT(" edge midpoint is on the edge"), SOLSolidMenu::HullDistance(mids[edge], size, hull),
            0.0f, tol);
        TestEqual(name + TEXT(" 1 outside"), SOLSolidMenu::HullDistance(mids[edge] + normals[edge], size, hull), 1.0f,
            tol);
        TestEqual(name + TEXT(" 5 outside"),
            SOLSolidMenu::HullDistance(mids[edge] + 5.0f * normals[edge], size, hull), 5.0f, tol);
        TestEqual(name + TEXT(" 2 inside"), SOLSolidMenu::HullDistance(mids[edge] - 2.0f * normals[edge], size, hull),
            -2.0f, tol);
    }

    // Points along the top edge's straight part, between the TL and TR chamfers
    FSOLSolidMenuSweepLog log;
    for (float x = SOLIDMENUTEST_TL + 0.5f; x < size.X - SOLIDMENUTEST_TR - 0.5f; x += 13.0f)
    {
        const float d = SOLSolidMenu::HullDistance(FVector2f(x, 0.0f), size, hull);
        if (!FMath::IsNearlyEqual(d, 0.0f, tol))
        {
            log.Fail(FString::Printf(TEXT("x %f: %f"), x, d));
        }
    }
    log.Report(*this, TEXT("the top edge's straight part is at distance 0"));

    TestEqual(TEXT("the centre is half the short side inside"),
        SOLSolidMenu::HullDistance(0.5f * size, size, hull), -0.5f * FMath::Min(size.X, size.Y), tol);
    TestTrue(TEXT("far outside is positive"), SOLSolidMenu::HullDistance(FVector2f(-100.0f, -100.0f), size, hull)
        > 0.0f);
    TestTrue(TEXT("far outside (right) is positive"),
        SOLSolidMenu::HullDistance(FVector2f(size.X + 50.0f, 0.5f * size.Y), size, hull) > 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullChamfersTest, "SOLTest.SolidMenu.Hull.DistanceChamferDiagonals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The SDF is 0 on each chamfer diagonal and exact as a distance along the diagonal's normal, both ways
bool FSOLSolidMenuHullChamfersTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f size = SolidMenuTestSize();
    const FSOLSolidHull hull = SolidMenuTestReferenceHull();
    const float tol = SOLIDMENUTEST_SDF_TOLERANCE;
    for (int32 corner = 0; corner < 4; ++corner)
    {
        const float chamfer = SolidMenuTestChamfer(hull, corner);
        const FVector2f normal = SolidMenuTestDiagonalNormal(corner);
        const FVector2f mid = SolidMenuTestDiagonalMid(corner, chamfer, size);
        const FString label = FString::Printf(TEXT("corner %d"), corner);

        TestEqual(label + TEXT(" diagonal midpoint on the edge"), SOLSolidMenu::HullDistance(mid, size, hull), 0.0f,
            tol);
        TestEqual(label + TEXT(" 3 outside the diagonal"), SOLSolidMenu::HullDistance(mid + 3.0f * normal, size, hull),
            3.0f, tol);
        TestEqual(label + TEXT(" 2 inside the diagonal"), SOLSolidMenu::HullDistance(mid - 2.0f * normal, size, hull),
            -2.0f, tol);

        // Points along the diagonal, between its ends on the two edges
        const FVector2f tangent(-normal.Y, normal.X);
        const float halfLength = 0.5f * chamfer * SOLIDMENUTEST_SQRT2;
        const FVector2f endA = mid + tangent * halfLength;
        const FVector2f endB = mid - tangent * halfLength;
        FSOLSolidMenuSweepLog log;
        for (int32 step = 1; step < 10; ++step)
        {
            const float t = static_cast<float>(step) / 10.0f;
            const FVector2f p = endA + t * (endB - endA);
            const float d = SOLSolidMenu::HullDistance(p, size, hull);
            if (!FMath::IsNearlyEqual(d, 0.0f, tol))
            {
                log.Fail(FString::Printf(TEXT("t %f: %f"), t, d));
            }
        }
        log.Report(*this, *(label + TEXT(" points along the diagonal are at distance 0")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullTipsTest, "SOLTest.SolidMenu.Hull.DistanceCornerTips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A cut corner's tip region is outside (the rect corner is chamfer / sqrt 2 away); an uncut corner is on the edge
bool FSOLSolidMenuHullTipsTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f size = SolidMenuTestSize();
    const FSOLSolidHull hull = SolidMenuTestReferenceHull();
    for (int32 corner = 0; corner < 4; ++corner)
    {
        const float chamfer = SolidMenuTestChamfer(hull, corner);
        const FVector2f tip = SolidMenuTestCorner(corner, size);
        const FVector2f inward = -SolidMenuTestDiagonalNormal(corner);
        const FString label = FString::Printf(TEXT("corner %d"), corner);
        TestEqual(label + TEXT(" rect corner is chamfer / sqrt 2 outside"), SOLSolidMenu::HullDistance(tip, size, hull),
            chamfer / SOLIDMENUTEST_SQRT2, SOLIDMENUTEST_SDF_TOLERANCE);
        TestTrue(label + TEXT(" a point inside the rect but in the cut is outside"),
            SOLSolidMenu::HullDistance(tip + inward * (0.25f * chamfer), size, hull) > 0.0f);
    }

    const FSOLSolidHull square = SolidMenuTestHull(0.0f, 0.0f, 0.0f, 0.0f);
    for (int32 corner = 0; corner < 4; ++corner)
    {
        const FVector2f tip = SolidMenuTestCorner(corner, size);
        const FString label = FString::Printf(TEXT("uncut corner %d"), corner);
        TestEqual(label + TEXT(" is on the edge"), SOLSolidMenu::HullDistance(tip, size, square), 0.0f,
            SOLIDMENUTEST_SDF_TOLERANCE);
        TestTrue(label + TEXT(" beyond it is outside"),
            SOLSolidMenu::HullDistance(tip + 3.0f * SolidMenuTestDiagonalNormal(corner), size, square) > 0.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullFeaturesTest, "SOLTest.SolidMenu.Hull.DistanceStepChinNotch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The SDF of the title step, chin and side notches (SDD 8.1 amendment): 0 on their flanks and edges, the lowered top
// and raised bottom edges outside their old line, the notch cut out of both sides, the moved chamfers on their corners
bool FSOLSolidMenuHullFeaturesTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f size = SolidMenuTestSize();
    FSOLSolidHull input = SolidMenuTestReferenceHull();
    input.TitleStepWidth = 120.0f;
    input.TitleStepHeight = 10.0f;
    input.NotchY = 150.0f;
    input.NotchDepth = 6.0f;
    input.NotchHeight = 20.0f;
    input.ChinWidth = 120.0f;
    input.ChinHeight = 10.0f;
    const FSOLSolidHull hull = SOLSolidMenu::ClampHull(input, size);
    const float tol = SOLIDMENUTEST_SDF_TOLERANCE;
    const float w = size.X;
    const float h = size.Y;

    // Expected distances at sample points: label, point, distance
    struct FSample
    {
        const TCHAR* Label;
        FVector2f Point;
        float Distance;
    };
    const FSample samples[] = {
        { TEXT("raised title edge"), FVector2f(60.0f, 0.0f), 0.0f },
        { TEXT("2 above the raised title edge"), FVector2f(60.0f, -2.0f), 2.0f },
        { TEXT("step flank midpoint"), FVector2f(125.0f, 5.0f), 0.0f },
        { TEXT("lowered top edge"), FVector2f(200.0f, 10.0f), 0.0f },
        { TEXT("lowered top edge by the top-right chamfer"), FVector2f(w - SOLIDMENUTEST_TR - 1.0f, 10.0f), 0.0f },
        { TEXT("half the step above the lowered top edge"), FVector2f(200.0f, 5.0f), 5.0f },
        { TEXT("top-right diagonal midpoint on the lowered corner"), FVector2f(w - 6.0f, 16.0f), 0.0f },
        { TEXT("chin bottom edge"), FVector2f(60.0f, h), 0.0f },
        { TEXT("chin flank midpoint"), FVector2f(125.0f, h - 5.0f), 0.0f },
        { TEXT("raised bottom edge"), FVector2f(200.0f, h - 10.0f), 0.0f },
        { TEXT("half the chin below the raised bottom edge"), FVector2f(200.0f, h - 5.0f), 5.0f },
        { TEXT("bottom-right diagonal midpoint on the raised corner"), FVector2f(w - 14.0f, h - 24.0f), 0.0f },
        { TEXT("left notch inner face"), FVector2f(6.0f, 150.0f), 0.0f },
        { TEXT("1 inside the left notch face"), FVector2f(7.0f, 150.0f), -1.0f },
        { TEXT("left notch top flank midpoint"), FVector2f(3.0f, 143.0f), 0.0f },
        { TEXT("right notch inner face"), FVector2f(w - 6.0f, 150.0f), 0.0f },
        { TEXT("1 inside the right notch face"), FVector2f(w - 7.0f, 150.0f), -1.0f },
        { TEXT("right notch bottom flank midpoint"), FVector2f(w - 3.0f, 157.0f), 0.0f },
    };
    for (const FSample& sample : samples)
    {
        TestEqual(sample.Label, SOLSolidMenu::HullDistance(sample.Point, size, hull), sample.Distance, tol);
    }
    TestTrue(TEXT("the old left edge inside the notch is outside"),
        SOLSolidMenu::HullDistance(FVector2f(0.0f, 150.0f), size, hull) > 0.0f);
    TestTrue(TEXT("the old right edge inside the notch is outside"),
        SOLSolidMenu::HullDistance(FVector2f(w, 150.0f), size, hull) > 0.0f);
    TestTrue(TEXT("the cut-off top-right rect corner is outside"),
        SOLSolidMenu::HullDistance(FVector2f(w, 0.0f), size, hull) > 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuHullClampSweepTest, "SOLTest.SolidMenu.Hull.ClampSweepClearAndIdempotent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Over adversarial sizes and hulls (zero, normal and huge chamfers, small and huge features): the step and chin flanks
// stay clear of their chamfers, the notch inside the straight part both sides share, a removed feature reads all 0,
// and a second clamp changes nothing
bool FSOLSolidMenuHullClampSweepTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f sizes[] = {
        FVector2f(100.0f, 1000.0f), FVector2f(1000.0f, 60.0f), FVector2f(10.0f, 10.0f), FVector2f(400.0f, 300.0f),
        FVector2f(1.0f, 500.0f), FVector2f(500.0f, 1.0f) };
    const float chamfers[] = { 0.0f, 12.0f, 28.0f, 1.0e4f };
    const float features[] = { 1.0f, 50.0f, 200.0f, 1.0e5f };
    const float tol = SOLIDMENUTEST_SDF_TOLERANCE;
    FSOLSolidMenuSweepLog log;
    for (const FVector2f& size : sizes)
    {
        for (const float chamfer : chamfers)
        {
            for (const float feature : features)
            {
                FSOLSolidHull input = SolidMenuTestHull(chamfer, 0.5f * chamfer, chamfer, 0.5f * chamfer);
                input.TitleStepWidth = 0.25f * feature;
                input.TitleStepHeight = feature;
                input.NotchY = 0.5f * size.Y;
                input.NotchDepth = feature;
                input.NotchHeight = feature;
                input.ChinWidth = feature;
                input.ChinHeight = 0.25f * feature;
                const FSOLSolidHull c = SOLSolidMenu::ClampHull(input, size);
                const FSOLSolidHull again = SOLSolidMenu::ClampHull(c, size);
                const FString at = FString::Printf(TEXT("size (%f, %f) chamfer %f feature %f"), size.X, size.Y,
                    chamfer, feature);

                // Step and chin: present with both values or removed whole, flanks clear of the chamfers
                const bool bStep = c.TitleStepWidth > 0.0f && c.TitleStepHeight > 0.0f;
                const bool bStepOk = bStep ? c.TitleStepWidth >= c.ChamferTL - tol
                    && c.TitleStepWidth + c.TitleStepHeight <= size.X - c.ChamferTR + tol
                    : c.TitleStepWidth == 0.0f && c.TitleStepHeight == 0.0f;
                const bool bChin = c.ChinWidth > 0.0f && c.ChinHeight > 0.0f;
                const bool bChinOk = bChin ? c.ChinWidth >= c.ChamferBL - tol
                    && c.ChinWidth + c.ChinHeight <= size.X - c.ChamferBR + tol
                    : c.ChinWidth == 0.0f && c.ChinHeight == 0.0f;

                // Notch: inside the shared straight part, at most half as deep as tall, or removed
                const bool bNotch = c.NotchY >= 0.0f && c.NotchDepth > 0.0f && c.NotchHeight > 0.0f;
                const float low = FMath::Max(c.ChamferTL, c.TitleStepHeight + c.ChamferTR);
                const float high = FMath::Min(size.Y - c.ChamferBL, size.Y - c.ChinHeight - c.ChamferBR);
                const bool bNotchOk = bNotch ? c.NotchY - 0.5f * c.NotchHeight >= low - tol
                    && c.NotchY + 0.5f * c.NotchHeight <= high + tol && c.NotchDepth <= 0.5f * c.NotchHeight + tol
                    && c.NotchDepth <= SOL::SolidMenu::HULL_MAX_FEATURE_FRACTION * size.X + tol
                    : c.NotchDepth == 0.0f && c.NotchHeight == 0.0f;

                // Idempotent
                const bool bSame = FMath::IsNearlyEqual(again.ChamferTL, c.ChamferTL, tol)
                    && FMath::IsNearlyEqual(again.ChamferTR, c.ChamferTR, tol)
                    && FMath::IsNearlyEqual(again.ChamferBR, c.ChamferBR, tol)
                    && FMath::IsNearlyEqual(again.ChamferBL, c.ChamferBL, tol)
                    && FMath::IsNearlyEqual(again.TitleStepWidth, c.TitleStepWidth, tol)
                    && FMath::IsNearlyEqual(again.TitleStepHeight, c.TitleStepHeight, tol)
                    && FMath::IsNearlyEqual(again.NotchY, c.NotchY, tol)
                    && FMath::IsNearlyEqual(again.NotchDepth, c.NotchDepth, tol)
                    && FMath::IsNearlyEqual(again.NotchHeight, c.NotchHeight, tol)
                    && FMath::IsNearlyEqual(again.ChinWidth, c.ChinWidth, tol)
                    && FMath::IsNearlyEqual(again.ChinHeight, c.ChinHeight, tol);
                if (!bStepOk || !bChinOk || !bNotchOk || !bSame || !SolidMenuTestHullFinite(c))
                {
                    log.Fail(FString::Printf(TEXT("%s: step %d chin %d notch %d idempotent %d"), *at,
                        bStepOk ? 1 : 0, bChinOk ? 1 : 0, bNotchOk ? 1 : 0, bSame ? 1 : 0));
                }
            }
        }
    }
    log.Report(*this, TEXT("clamped features clear of the chamfers and idempotent"));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Button look (SDD 8.2)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonRampUpTest, "SOLTest.SolidMenu.Button.HoverRampUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Normal to Hovered: lift, edge, LED, underline and trace rise linearly from 0 to 1 over HOVER_RAMP_S
bool FSOLSolidMenuButtonRampUpTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const double ramp = SOL::SolidMenu::HOVER_RAMP_S;
    const ESOLSolidButtonState hovered = ESOLSolidButtonState::Hovered;
    const ESOLSolidButtonState normal = ESOLSolidButtonState::Normal;

    SolidMenuTestExpectLookAll(*this, TEXT("at the change"), EvaluateButton(hovered, normal, 0.0, 1.0f), 0.0f);
    SolidMenuTestExpectLookAll(*this, TEXT("a quarter in"), EvaluateButton(hovered, normal, 0.25 * ramp, 1.0f),
        0.25f);
    SolidMenuTestExpectLookAll(*this, TEXT("half way"), EvaluateButton(hovered, normal, 0.5 * ramp, 1.0f), 0.5f);
    SolidMenuTestExpectLookAll(*this, TEXT("at the ramp end"), EvaluateButton(hovered, normal, ramp, 1.0f), 1.0f);
    SolidMenuTestExpectLookAll(*this, TEXT("long after"), EvaluateButton(hovered, normal, SOLIDMENUTEST_SETTLED_S,
        1.0f), 1.0f);
    SolidMenuTestExpectLookAll(*this, TEXT("staying hovered"), EvaluateButton(hovered, hovered, 0.0, 1.0f), 1.0f);

    const FSOLSolidButtonLook half = EvaluateButton(hovered, normal, 0.5 * ramp, 1.0f);
    TestFalse(TEXT("hover is not the pressed hull"), half.bPressedHull);
    TestFalse(TEXT("hover is not the selected mark"), half.bSelectedMark);

    // Durations do not depend on the strength (SDD 7: strength 2 keeps the durations)
    SolidMenuTestExpectLookAll(*this, TEXT("half way at strength 0.5"), EvaluateButton(hovered, normal, 0.5 * ramp,
        0.5f), 0.5f);
    SolidMenuTestExpectLookAll(*this, TEXT("half way at strength 2"), EvaluateButton(hovered, normal, 0.5 * ramp,
        2.0f), 0.5f);

    // From Selected (no lift) the lift also rises from 0
    TestEqual(TEXT("from Selected the lift rises from 0"),
        EvaluateButton(hovered, ESOLSolidButtonState::Selected, 0.5 * ramp, 1.0f).Lift, 0.5f, SOLIDMENUTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonRampDownTest, "SOLTest.SolidMenu.Button.HoverRampDown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Hovered to Normal: the five values fall linearly from 1 to 0 over the same HOVER_RAMP_S
bool FSOLSolidMenuButtonRampDownTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const double ramp = SOL::SolidMenu::HOVER_RAMP_S;
    const ESOLSolidButtonState hovered = ESOLSolidButtonState::Hovered;
    const ESOLSolidButtonState normal = ESOLSolidButtonState::Normal;

    SolidMenuTestExpectLookAll(*this, TEXT("at the change"), EvaluateButton(normal, hovered, 0.0, 1.0f), 1.0f);
    SolidMenuTestExpectLookAll(*this, TEXT("a quarter in"), EvaluateButton(normal, hovered, 0.25 * ramp, 1.0f),
        0.75f);
    SolidMenuTestExpectLookAll(*this, TEXT("half way"), EvaluateButton(normal, hovered, 0.5 * ramp, 1.0f), 0.5f);
    SolidMenuTestExpectLookAll(*this, TEXT("at the ramp end"), EvaluateButton(normal, hovered, ramp, 1.0f), 0.0f);
    SolidMenuTestExpectLookAll(*this, TEXT("long after"), EvaluateButton(normal, hovered, SOLIDMENUTEST_SETTLED_S,
        1.0f), 0.0f);

    const FSOLSolidButtonLook rest = EvaluateButton(normal, normal, 0.0, 1.0f);
    SolidMenuTestExpectLookAll(*this, TEXT("normal at rest"), rest, 0.0f);
    TestEqual(TEXT("normal at rest: no glow"), rest.Glow, 0.0f, SOLIDMENUTEST_TOLERANCE);
    TestFalse(TEXT("normal at rest: no pressed hull"), rest.bPressedHull);
    TestFalse(TEXT("normal at rest: no selected mark"), rest.bSelectedMark);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonStrengthZeroTest, "SOLTest.SolidMenu.Button.StrengthZeroInstant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// At strength 0 (and a negative or NaN strength, which sanitize to 0) the hover look is instant and fully present but
// flat (no glow)
bool FSOLSolidMenuButtonStrengthZeroTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const ESOLSolidButtonState hovered = ESOLSolidButtonState::Hovered;
    const ESOLSolidButtonState normal = ESOLSolidButtonState::Normal;
    const float strengths[] = { 0.0f, -1.0f, SOLIDMENUTEST_NAN };
    for (const float s : strengths)
    {
        const FString label = FString::Printf(TEXT("strength %f"), s);
        const FSOLSolidButtonLook on = EvaluateButton(hovered, normal, 0.0, s);
        SolidMenuTestExpectLookAll(*this, label + TEXT(" hover at the change"), on, 1.0f);
        TestEqual(label + TEXT(" hover has no glow"), on.Glow, 0.0f);
        SolidMenuTestExpectLookAll(*this, label + TEXT(" hover a moment later"),
            EvaluateButton(hovered, normal, 0.01, s), 1.0f);
        SolidMenuTestExpectLookAll(*this, label + TEXT(" leaving hover at the change"),
            EvaluateButton(normal, hovered, 0.0, s), 0.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonGlowTest, "SOLTest.SolidMenu.Button.Glow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Glow = s x Edge (s sanitized to [0, 2]: the halo doubles at strength 2, SDD 46 section 7), at strengths 0, 0.5, 1 and
// 2, settled and mid-ramp
bool FSOLSolidMenuButtonGlowTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const double ramp = SOL::SolidMenu::HOVER_RAMP_S;
    const ESOLSolidButtonState hovered = ESOLSolidButtonState::Hovered;
    const ESOLSolidButtonState normal = ESOLSolidButtonState::Normal;
    const float strengths[] = { 0.0f, 0.5f, 1.0f, 2.0f };
    for (const float s : strengths)
    {
        const FString label = FString::Printf(TEXT("strength %f"), s);
        const FSOLSolidButtonLook full = EvaluateButton(hovered, normal, SOLIDMENUTEST_SETTLED_S, s);
        TestEqual(label + TEXT(" settled hover glow"), full.Glow, SolidMenuTestStrength(s) * full.Edge,
            SOLIDMENUTEST_TOLERANCE);
        const FSOLSolidButtonLook half = EvaluateButton(hovered, normal, 0.5 * ramp, s);
        TestEqual(label + TEXT(" mid-ramp glow"), half.Glow, SolidMenuTestStrength(s) * half.Edge,
            SOLIDMENUTEST_TOLERANCE);
    }
    TestEqual(TEXT("strength 1 settled glow is 1"),
        EvaluateButton(hovered, normal, SOLIDMENUTEST_SETTLED_S, 1.0f).Glow, 1.0f, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("strength 0.5 settled glow is 0.5"),
        EvaluateButton(hovered, normal, SOLIDMENUTEST_SETTLED_S, 0.5f).Glow, 0.5f, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("strength 2 settled glow is doubled"),
        EvaluateButton(hovered, normal, SOLIDMENUTEST_SETTLED_S, 2.0f).Glow, 2.0f, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("strength 5 glow capped at the strength 2 value"),
        EvaluateButton(hovered, normal, SOLIDMENUTEST_SETTLED_S, 5.0f).Glow, 2.0f, SOLIDMENUTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonPressedTest, "SOLTest.SolidMenu.Button.Pressed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Pressed: the pressed hull, no lift, the edge fully on, at once from any previous state and at any strength
bool FSOLSolidMenuButtonPressedTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const double times[] = { 0.0, 0.5 * SOL::SolidMenu::HOVER_RAMP_S, SOLIDMENUTEST_SETTLED_S };
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    const ESOLSolidButtonState previous[] = {
        ESOLSolidButtonState::Normal, ESOLSolidButtonState::Hovered, ESOLSolidButtonState::Selected };
    FSOLSolidMenuSweepLog log;
    for (const ESOLSolidButtonState from : previous)
    {
        for (const double t : times)
        {
            for (const float s : strengths)
            {
                const FSOLSolidButtonLook look = EvaluateButton(ESOLSolidButtonState::Pressed, from, t, s);
                if (!look.bPressedHull || !FMath::IsNearlyEqual(look.Lift, 0.0f, SOLIDMENUTEST_TOLERANCE)
                    || !FMath::IsNearlyEqual(look.Edge, 1.0f, SOLIDMENUTEST_TOLERANCE))
                {
                    log.Fail(FString::Printf(TEXT("from %d t %f s %f: pressed %d lift %f edge %f"),
                        static_cast<int32>(from), t, s, look.bPressedHull ? 1 : 0, look.Lift, look.Edge));
                }
            }
        }
    }
    log.Report(*this, TEXT("pressed hull, lift 0, edge 1"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonLingerTest, "SOLTest.SolidMenu.Button.PressedLinger",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A press that ended before PRESSED_LINGER_S (sinceChangeS measured from the press) still reports the pressed look
// until then; well after it the button shows its new state
bool FSOLSolidMenuButtonLingerTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const double linger = SOL::SolidMenu::PRESSED_LINGER_S;
    const ESOLSolidButtonState pressed = ESOLSolidButtonState::Pressed;
    const double settled = linger + SOL::SolidMenu::HOVER_RAMP_S + SOLIDMENUTEST_SETTLED_S;

    const ESOLSolidButtonState afters[] = { ESOLSolidButtonState::Hovered, ESOLSolidButtonState::Normal };
    for (const ESOLSolidButtonState after : afters)
    {
        const FString label = after == ESOLSolidButtonState::Hovered ? TEXT("to Hovered") : TEXT("to Normal");
        const double earlies[] = { 0.0, 0.5 * linger, 0.95 * linger };
        for (const double t : earlies)
        {
            const FSOLSolidButtonLook look = EvaluateButton(after, pressed, t, 1.0f);
            const FString at = FString::Printf(TEXT("%s at %f"), *label, t);
            TestTrue(at + TEXT(": still the pressed hull"), look.bPressedHull);
            TestEqual(at + TEXT(": no lift"), look.Lift, 0.0f, SOLIDMENUTEST_TOLERANCE);
            TestEqual(at + TEXT(": edge on"), look.Edge, 1.0f, SOLIDMENUTEST_TOLERANCE);
        }
        TestFalse(label + TEXT(" well after the linger: not pressed"),
            EvaluateButton(after, pressed, settled, 1.0f).bPressedHull);
    }
    TestEqual(TEXT("to Hovered well after: fully lifted"),
        EvaluateButton(ESOLSolidButtonState::Hovered, pressed, settled, 1.0f).Lift, 1.0f, SOLIDMENUTEST_TOLERANCE);
    SolidMenuTestExpectLookAll(*this, TEXT("to Normal well after"),
        EvaluateButton(ESOLSolidButtonState::Normal, pressed, settled, 1.0f), 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonSelectedTest, "SOLTest.SolidMenu.Button.SelectedMark",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Selected: the cyan selected mark, no lift, no amber edge or trace, not the pressed hull
bool FSOLSolidMenuButtonSelectedTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const ESOLSolidButtonState selected = ESOLSolidButtonState::Selected;
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    for (const float s : strengths)
    {
        const FString label = FString::Printf(TEXT("strength %f"), s);
        const FSOLSolidButtonLook atOnce = EvaluateButton(selected, ESOLSolidButtonState::Normal, 0.0, s);
        TestTrue(label + TEXT(" mark at once"), atOnce.bSelectedMark);
        TestEqual(label + TEXT(" no lift at once"), atOnce.Lift, 0.0f, SOLIDMENUTEST_TOLERANCE);

        const FSOLSolidButtonLook rest = EvaluateButton(selected, ESOLSolidButtonState::Normal, SOLIDMENUTEST_SETTLED_S,
            s);
        TestTrue(label + TEXT(" mark settled"), rest.bSelectedMark);
        TestFalse(label + TEXT(" not the pressed hull"), rest.bPressedHull);
        TestEqual(label + TEXT(" no lift"), rest.Lift, 0.0f, SOLIDMENUTEST_TOLERANCE);
        TestEqual(label + TEXT(" no amber edge"), rest.Edge, 0.0f, SOLIDMENUTEST_TOLERANCE);
        TestEqual(label + TEXT(" no amber trace"), rest.Trace, 0.0f, SOLIDMENUTEST_TOLERANCE);

        TestTrue(label + TEXT(" settled from Hovered: mark"),
            EvaluateButton(selected, ESOLSolidButtonState::Hovered, SOLIDMENUTEST_SETTLED_S, s).bSelectedMark);
        TestEqual(label + TEXT(" settled from Hovered: no lift"),
            EvaluateButton(selected, ESOLSolidButtonState::Hovered, SOLIDMENUTEST_SETTLED_S, s).Lift, 0.0f,
            SOLIDMENUTEST_TOLERANCE);
    }
    TestFalse(TEXT("Normal has no mark"),
        EvaluateButton(ESOLSolidButtonState::Normal, selected, SOLIDMENUTEST_SETTLED_S, 1.0f).bSelectedMark);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonDisabledTest, "SOLTest.SolidMenu.Button.DisabledZeros",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Disabled: everything 0, the pressed and selected flags off and bDisabled on, from any previous state, at any time
// and strength
bool FSOLSolidMenuButtonDisabledTest::RunTest(const FString& /*parameters*/)
{
    const double times[] = { 0.0, 0.5 * SOL::SolidMenu::HOVER_RAMP_S, 0.5 * SOL::SolidMenu::PRESSED_LINGER_S,
        SOLIDMENUTEST_SETTLED_S };
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    FSOLSolidMenuSweepLog log;
    for (const ESOLSolidButtonState from : SOLIDMENUTEST_STATES)
    {
        for (const double t : times)
        {
            for (const float s : strengths)
            {
                const FSOLSolidButtonLook look = SOLSolidMenu::EvaluateButton(ESOLSolidButtonState::Disabled, from, t,
                    s);
                if (!SolidMenuTestLookAll(look, 0.0f, 0.0f) || look.Glow != 0.0f || look.bPressedHull
                    || look.bSelectedMark || !look.bDisabled)
                {
                    log.Fail(FString::Printf(TEXT("from %d t %f s %f"), static_cast<int32>(from), t, s));
                }
            }
        }
    }
    log.Report(*this, TEXT("disabled is all zero"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonNaNTimeTest, "SOLTest.SolidMenu.Button.NaNAndNegativeTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A NaN or negative sinceChangeS reads 0
bool FSOLSolidMenuButtonNaNTimeTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const double badTimes[] = { SOLIDMENUTEST_NAN_D, -0.5, -1.0e9 };
    for (const ESOLSolidButtonState state : SOLIDMENUTEST_STATES)
    {
        for (const ESOLSolidButtonState from : SOLIDMENUTEST_STATES)
        {
            const FSOLSolidButtonLook atZero = EvaluateButton(state, from, 0.0, 1.0f);
            for (const double t : badTimes)
            {
                SolidMenuTestLookEqual(*this, FString::Printf(TEXT("state %d from %d at %f"),
                    static_cast<int32>(state), static_cast<int32>(from), t), EvaluateButton(state, from, t, 1.0f),
                    atZero);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonRangeTest, "SOLTest.SolidMenu.Button.Range",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every animated value is finite and in [0, 1], and Glow = s x Edge (in [0, 2], s sanitized), for every state pair,
// time and strength; only a Disabled look is flagged disabled
bool FSOLSolidMenuButtonRangeTest::RunTest(const FString& /*parameters*/)
{
    const double ramp = SOL::SolidMenu::HOVER_RAMP_S;
    const double times[] = { SOLIDMENUTEST_NAN_D, -1.0, 0.0, 0.25 * ramp, 0.5 * ramp, ramp,
        0.5 * SOL::SolidMenu::PRESSED_LINGER_S, 1.0, 1.0e6, SOLIDMENUTEST_INF_D };
    const float strengths[] = { SOLIDMENUTEST_NAN, -1.0f, 0.0f, 0.5f, 1.0f, 2.0f, 5.0f, SOLIDMENUTEST_INF };
    FSOLSolidMenuSweepLog log;
    for (const ESOLSolidButtonState state : SOLIDMENUTEST_STATES)
    {
        for (const ESOLSolidButtonState from : SOLIDMENUTEST_STATES)
        {
            for (const double t : times)
            {
                for (const float s : strengths)
                {
                    const FSOLSolidButtonLook look = SOLSolidMenu::EvaluateButton(state, from, t, s);
                    const float values[] = { look.Lift, look.Edge, look.Led, look.Underline, look.Trace };
                    bool bInRange = true;
                    for (const float value : values)
                    {
                        bInRange = bInRange && FMath::IsFinite(value) && value >= 0.0f && value <= 1.0f;
                    }
                    bInRange = bInRange && FMath::IsFinite(look.Glow) && look.Glow >= 0.0f && look.Glow <= 2.0f;
                    const bool bGlow = FMath::IsNearlyEqual(look.Glow, SolidMenuTestStrength(s) * look.Edge,
                        SOLIDMENUTEST_TOLERANCE);
                    const bool bDisabledFlag = look.bDisabled == (state == ESOLSolidButtonState::Disabled);
                    if (!bInRange || !bGlow || !bDisabledFlag)
                    {
                        log.Fail(FString::Printf(TEXT("state %d from %d t %f s %f: lift %f edge %f glow %f"),
                            static_cast<int32>(state), static_cast<int32>(from), t, s, look.Lift, look.Edge,
                            look.Glow));
                    }
                }
            }
        }
    }
    log.Report(*this, TEXT("values in [0, 1] and glow from the edge"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonLiftTest, "SOLTest.SolidMenu.Button.LiftOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// LiftOffset = -HOVER_LIFT_PX x Lift (up is negative Y)
bool FSOLSolidMenuButtonLiftTest::RunTest(const FString& /*parameters*/)
{
    const float lift = SOL::SolidMenu::HOVER_LIFT_PX;
    FSOLSolidButtonLook look{};
    look.Lift = 0.0f;
    TestEqual(TEXT("no lift"), SOLSolidMenu::LiftOffset(look), 0.0f, SOLIDMENUTEST_TOLERANCE);
    look.Lift = 0.5f;
    TestEqual(TEXT("half lift"), SOLSolidMenu::LiftOffset(look), -0.5f * lift, SOLIDMENUTEST_TOLERANCE);
    look.Lift = 1.0f;
    TestEqual(TEXT("full lift"), SOLSolidMenu::LiftOffset(look), -lift, SOLIDMENUTEST_TOLERANCE);

    const FSOLSolidButtonLook hovered = SOLSolidMenu::EvaluateButton(ESOLSolidButtonState::Hovered,
        ESOLSolidButtonState::Normal, SOLIDMENUTEST_SETTLED_S, 1.0f);
    TestEqual(TEXT("a settled hover lifts HOVER_LIFT_PX up"), SOLSolidMenu::LiftOffset(hovered), -lift,
        SOLIDMENUTEST_TOLERANCE);
    const FSOLSolidButtonLook pressed = SOLSolidMenu::EvaluateButton(ESOLSolidButtonState::Pressed,
        ESOLSolidButtonState::Hovered, 0.0, 1.0f);
    TestEqual(TEXT("a press does not lift"), SOLSolidMenu::LiftOffset(pressed), 0.0f, SOLIDMENUTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonLingerRampTest, "SOLTest.SolidMenu.Button.RampAfterLinger",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Leaving a press, the ramp to the new state starts when PRESSED_LINGER_S ends (sinceChangeS measured from the press)
bool FSOLSolidMenuButtonLingerRampTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButton;
    const double linger = SOL::SolidMenu::PRESSED_LINGER_S;
    const double ramp = SOL::SolidMenu::HOVER_RAMP_S;
    const ESOLSolidButtonState pressed = ESOLSolidButtonState::Pressed;
    const FSOLSolidButtonLook toHover = EvaluateButton(ESOLSolidButtonState::Hovered, pressed, linger + 0.5 * ramp,
        1.0f);
    TestEqual(TEXT("to Hovered half a ramp after the linger: half lifted"), toHover.Lift, 0.5f,
        SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("to Hovered: the edge stays on"), toHover.Edge, 1.0f, SOLIDMENUTEST_TOLERANCE);
    TestFalse(TEXT("to Hovered after the linger: not pressed"), toHover.bPressedHull);
    const FSOLSolidButtonLook toNormal = EvaluateButton(ESOLSolidButtonState::Normal, pressed, linger + 0.5 * ramp,
        1.0f);
    TestEqual(TEXT("to Normal half a ramp after the linger: half the edge"), toNormal.Edge, 0.5f,
        SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("to Normal: no lift"), toNormal.Lift, 0.0f, SOLIDMENUTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuButtonFromTest, "SOLTest.SolidMenu.Button.RampFromLookAtChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// EvaluateButtonFrom: a hover that reverses mid-ramp continues from the look at the change (no pop); Pressed, Selected
// and Disabled behave as in EvaluateButton; bad amounts in the look at the change are clamped to [0, 1]
bool FSOLSolidMenuButtonFromTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenu::EvaluateButtonFrom;
    const double ramp = SOL::SolidMenu::HOVER_RAMP_S;
    const ESOLSolidButtonState normal = ESOLSolidButtonState::Normal;
    const ESOLSolidButtonState hovered = ESOLSolidButtonState::Hovered;

    // Enter, then leave a third of the way in
    const FSOLSolidButtonLook atLeave = SOLSolidMenu::EvaluateButton(hovered, normal, ramp / 3.0, 1.0f);
    SolidMenuTestExpectLookAll(*this, TEXT("leaving: continues from the look at the change"),
        EvaluateButtonFrom(normal, atLeave, 0.0, 1.0f), atLeave.Edge);
    SolidMenuTestExpectLookAll(*this, TEXT("leaving: half a ramp later"),
        EvaluateButtonFrom(normal, atLeave, 0.5 * ramp, 1.0f), 0.5f * atLeave.Edge);
    SolidMenuTestExpectLookAll(*this, TEXT("leaving: settled"),
        EvaluateButtonFrom(normal, atLeave, SOLIDMENUTEST_SETTLED_S, 1.0f), 0.0f);
    SolidMenuTestExpectLookAll(*this, TEXT("re-entering: half a ramp later"),
        EvaluateButtonFrom(hovered, atLeave, 0.5 * ramp, 1.0f), 0.5f * (atLeave.Edge + 1.0f));
    SolidMenuTestExpectLookAll(*this, TEXT("leaving at strength 0: instant"),
        EvaluateButtonFrom(normal, atLeave, 0.0, 0.0f), 0.0f);

    // Settled looks match the state-based form
    const ESOLSolidButtonState states[] = { normal, hovered, ESOLSolidButtonState::Pressed,
        ESOLSolidButtonState::Selected, ESOLSolidButtonState::Disabled };
    for (const ESOLSolidButtonState state : states)
    {
        SolidMenuTestLookEqual(*this, FString::Printf(TEXT("state %d settled"), static_cast<int32>(state)),
            EvaluateButtonFrom(state, atLeave, SOLIDMENUTEST_SETTLED_S, 2.0f),
            SOLSolidMenu::EvaluateButton(state, normal, SOLIDMENUTEST_SETTLED_S, 2.0f));
    }
    const FSOLSolidButtonLook press = EvaluateButtonFrom(ESOLSolidButtonState::Pressed, atLeave, 0.0, 1.0f);
    TestTrue(TEXT("Pressed at once: the pressed hull"), press.bPressedHull);
    TestEqual(TEXT("Pressed at once: edge 1"), press.Edge, 1.0f, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("Pressed at once: no lift"), press.Lift, 0.0f, SOLIDMENUTEST_TOLERANCE);

    // Bad amounts at the change
    FSOLSolidButtonLook bad;
    bad.Lift = SOLIDMENUTEST_NAN;
    bad.Edge = 5.0f;
    bad.Led = -3.0f;
    bad.Underline = SOLIDMENUTEST_INF;
    bad.Trace = SOLIDMENUTEST_NAN;
    const FSOLSolidButtonLook fromBad = EvaluateButtonFrom(normal, bad, 0.0, 1.0f);
    const float values[] = { fromBad.Lift, fromBad.Edge, fromBad.Led, fromBad.Underline, fromBad.Trace };
    bool bInRange = true;
    for (const float value : values)
    {
        bInRange = bInRange && FMath::IsFinite(value) && value >= 0.0f && value <= 1.0f;
    }
    TestTrue(TEXT("bad amounts at the change read in [0, 1]"), bInRange);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tint encoding (SDD 8.3)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuTintEncodeTest, "SOLTest.SolidMenu.Tint.Encode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// R = Edge, G = pressed hull, B = selected mark, A = 1 for an enabled look
bool FSOLSolidMenuTintEncodeTest::RunTest(const FString& /*parameters*/)
{
    const float tol = SOLIDMENUTEST_QUANT_TOLERANCE;
    FSOLSolidButtonLook look{};
    look.Lift = 1.0f;
    look.Edge = 0.25f;
    look.Led = 0.25f;
    look.Underline = 0.25f;
    look.Trace = 0.25f;
    look.Glow = 0.25f;
    look.bPressedHull = false;
    look.bSelectedMark = false;
    FLinearColor tint = SOLSolidMenu::EncodeButtonTint(look);
    TestEqual(TEXT("R carries the edge"), tint.R, 0.25f, tol);
    TestEqual(TEXT("G off"), tint.G, 0.0f);
    TestEqual(TEXT("B off"), tint.B, 0.0f);
    TestEqual(TEXT("A enabled"), tint.A, 1.0f);

    look.Edge = 1.0f;
    look.bPressedHull = true;
    tint = SOLSolidMenu::EncodeButtonTint(look);
    TestEqual(TEXT("pressed: R 1"), tint.R, 1.0f);
    TestEqual(TEXT("pressed: G 1"), tint.G, 1.0f);
    TestEqual(TEXT("pressed: B 0"), tint.B, 0.0f);

    look.Edge = 0.0f;
    look.bPressedHull = false;
    look.bSelectedMark = true;
    tint = SOLSolidMenu::EncodeButtonTint(look);
    TestEqual(TEXT("selected: R 0"), tint.R, 0.0f);
    TestEqual(TEXT("selected: G 0"), tint.G, 0.0f);
    TestEqual(TEXT("selected: B 1"), tint.B, 1.0f);
    TestEqual(TEXT("selected: A enabled"), tint.A, 1.0f);

    const ESOLSolidButtonState enabled[] = { ESOLSolidButtonState::Normal, ESOLSolidButtonState::Hovered,
        ESOLSolidButtonState::Pressed, ESOLSolidButtonState::Selected };
    for (const ESOLSolidButtonState state : enabled)
    {
        const FSOLSolidButtonLook evaluated = SOLSolidMenu::EvaluateButton(state, ESOLSolidButtonState::Normal,
            SOLIDMENUTEST_SETTLED_S, 1.0f);
        TestEqual(FString::Printf(TEXT("state %d: A enabled"), static_cast<int32>(state)),
            SOLSolidMenu::EncodeButtonTint(evaluated).A, 1.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuTintQuantizedTest, "SOLTest.SolidMenu.Tint.Quantized",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every channel of the tint lies on the 8-bit grid k / 255, within one step of the value it encodes
bool FSOLSolidMenuTintQuantizedTest::RunTest(const FString& /*parameters*/)
{
    FSOLSolidMenuSweepLog log;
    for (int32 step = 0; step <= 1000; ++step)
    {
        FSOLSolidButtonLook look{};
        look.Edge = static_cast<float>(step) / 1000.0f;
        look.Led = look.Edge;
        look.Underline = look.Edge;
        look.bPressedHull = (step % 2) == 0;
        look.bSelectedMark = (step % 3) == 0;
        const FLinearColor tint = SOLSolidMenu::EncodeButtonTint(look);
        const bool bGrid = SolidMenuTestOn8BitGrid(tint.R) && SolidMenuTestOn8BitGrid(tint.G)
            && SolidMenuTestOn8BitGrid(tint.B) && SolidMenuTestOn8BitGrid(tint.A);
        const bool bClose = FMath::Abs(tint.R - look.Edge) <= SOLIDMENUTEST_QUANT_TOLERANCE;
        if (!bGrid || !bClose)
        {
            log.Fail(FString::Printf(TEXT("edge %f: tint (%f, %f, %f, %f)"), look.Edge, tint.R, tint.G, tint.B,
                tint.A));
        }
    }
    log.Report(*this, TEXT("8-bit quantized channels"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuTintRoundTripTest, "SOLTest.SolidMenu.Tint.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Decode(Encode(look)) gives back the edge within one 8-bit step and both flags exactly, for every state, time and
// strength; the LED and underline decode as the edge (they move together)
bool FSOLSolidMenuTintRoundTripTest::RunTest(const FString& /*parameters*/)
{
    const double ramp = SOL::SolidMenu::HOVER_RAMP_S;
    const double times[] = { 0.0, 0.1 * ramp, 0.37 * ramp, 0.5 * ramp, 0.81 * ramp, SOLIDMENUTEST_SETTLED_S };
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    FSOLSolidMenuSweepLog log;
    for (const ESOLSolidButtonState state : SOLIDMENUTEST_STATES)
    {
        for (const ESOLSolidButtonState from : SOLIDMENUTEST_STATES)
        {
            for (const double t : times)
            {
                for (const float s : strengths)
                {
                    const FSOLSolidButtonLook look = SOLSolidMenu::EvaluateButton(state, from, t, s);
                    const FSOLSolidButtonLook back = SOLSolidMenu::DecodeButtonTint(
                        SOLSolidMenu::EncodeButtonTint(look));
                    const bool bOk = FMath::Abs(back.Edge - look.Edge) <= SOLIDMENUTEST_QUANT_TOLERANCE
                        && back.bPressedHull == look.bPressedHull && back.bSelectedMark == look.bSelectedMark
                        && back.bDisabled == look.bDisabled
                        && FMath::IsNearlyEqual(back.Led, back.Edge, SOLIDMENUTEST_QUANT_TOLERANCE)
                        && FMath::IsNearlyEqual(back.Underline, back.Edge, SOLIDMENUTEST_QUANT_TOLERANCE);
                    if (!bOk)
                    {
                        log.Fail(FString::Printf(TEXT("state %d from %d t %f s %f: edge %f -> %f"),
                            static_cast<int32>(state), static_cast<int32>(from), t, s, look.Edge, back.Edge));
                    }
                }
            }
        }
    }
    log.Report(*this, TEXT("tint round trip"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuTintDecodeTest, "SOLTest.SolidMenu.Tint.Decode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Decoding a raw tint: R is the edge, G 1 the pressed hull, B 1 the selected mark, 0 means off
bool FSOLSolidMenuTintDecodeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSolidButtonLook a = SOLSolidMenu::DecodeButtonTint(FLinearColor(0.6f, 1.0f, 0.0f, 1.0f));
    TestEqual(TEXT("edge from R"), a.Edge, 0.6f, SOLIDMENUTEST_QUANT_TOLERANCE);
    TestTrue(TEXT("G 1 is the pressed hull"), a.bPressedHull);
    TestFalse(TEXT("B 0 is no mark"), a.bSelectedMark);

    const FSOLSolidButtonLook b = SOLSolidMenu::DecodeButtonTint(FLinearColor(0.0f, 0.0f, 1.0f, 1.0f));
    TestEqual(TEXT("edge 0 from R"), b.Edge, 0.0f, SOLIDMENUTEST_QUANT_TOLERANCE);
    TestFalse(TEXT("G 0 is no pressed hull"), b.bPressedHull);
    TestTrue(TEXT("B 1 is the mark"), b.bSelectedMark);
    TestFalse(TEXT("A 1 is enabled"), b.bDisabled);

    const FSOLSolidButtonLook c = SOLSolidMenu::DecodeButtonTint(
        FLinearColor(0.0f, 0.0f, 0.0f, SOL::SolidMenu::DISABLED_ALPHA));
    TestTrue(TEXT("A DISABLED_ALPHA is disabled"), c.bDisabled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuTintDisabledTest, "SOLTest.SolidMenu.Tint.DisabledAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A disabled button's tint has A = DISABLED_ALPHA (8-bit quantized); an enabled one A = 1. The look carries the state
// in bDisabled, since a disabled look's amounts equal a Normal one's
bool FSOLSolidMenuTintDisabledTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSolidButtonLook disabled = SOLSolidMenu::EvaluateButton(ESOLSolidButtonState::Disabled,
        ESOLSolidButtonState::Normal, SOLIDMENUTEST_SETTLED_S, 1.0f);
    TestTrue(TEXT("the disabled look is flagged"), disabled.bDisabled);
    const FLinearColor tint = SOLSolidMenu::EncodeButtonTint(disabled);
    TestEqual(TEXT("disabled A"), tint.A, SOL::SolidMenu::DISABLED_ALPHA, SOLIDMENUTEST_QUANT_TOLERANCE);
    TestTrue(TEXT("disabled A on the 8-bit grid"), SolidMenuTestOn8BitGrid(tint.A));
    TestEqual(TEXT("disabled R"), tint.R, 0.0f);

    const FSOLSolidButtonLook normal = SOLSolidMenu::EvaluateButton(ESOLSolidButtonState::Normal,
        ESOLSolidButtonState::Normal, SOLIDMENUTEST_SETTLED_S, 1.0f);
    TestEqual(TEXT("normal A"), SOLSolidMenu::EncodeButtonTint(normal).A, 1.0f);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Panels and pause (SDD 8.4)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPanelSlideTest, "SOLTest.SolidMenu.Panel.SlideEndpointsAndEase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// PanelSlide: PANEL_SLIDE_PX at 0, 0 at 1, non-increasing, an ease-out (at or ahead of linear), clamped, NaN reads 0
bool FSOLSolidMenuPanelSlideTest::RunTest(const FString& /*parameters*/)
{
    const float slide = SOL::SolidMenu::PANEL_SLIDE_PX;
    TestEqual(TEXT("closed: the full slide"), SOLSolidMenu::PanelSlide(0.0f), slide, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("open: no offset"), SOLSolidMenu::PanelSlide(1.0f), 0.0f, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("below 0 clamps"), SOLSolidMenu::PanelSlide(-1.0f), slide, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("above 1 clamps"), SOLSolidMenu::PanelSlide(2.0f), 0.0f, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("NaN progress reads 0"), SOLSolidMenu::PanelSlide(SOLIDMENUTEST_NAN), slide,
        SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("half way: the cubic ease-out leaves an eighth"), SOLSolidMenu::PanelSlide(0.5f), 0.125f * slide,
        SOLIDMENUTEST_TOLERANCE);

    FSOLSolidMenuSweepLog log;
    float last = SOLSolidMenu::PanelSlide(0.0f);
    for (int32 step = 1; step <= 100; ++step)
    {
        const float p = static_cast<float>(step) / 100.0f;
        const float value = SOLSolidMenu::PanelSlide(p);
        const bool bMonotonic = value <= last + SOLIDMENUTEST_TOLERANCE;
        const bool bRange = value >= -SOLIDMENUTEST_TOLERANCE && value <= slide + SOLIDMENUTEST_TOLERANCE;
        const bool bEaseOut = value <= slide * (1.0f - p) + SOLIDMENUTEST_TOLERANCE;
        if (!bMonotonic || !bRange || !bEaseOut)
        {
            log.Fail(FString::Printf(TEXT("p %f: %f (previous %f)"), p, value, last));
        }
        last = value;
    }
    log.Report(*this, TEXT("non-increasing ease-out in [0, PANEL_SLIDE_PX]"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPauseSlideTest, "SOLTest.SolidMenu.Panel.PauseSlideNeverPositive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// PauseSlideX: -(width + PAUSE_SLIDE_MARGIN_PX) closed, 0 open, non-decreasing, the same ease as PanelSlide, never
// positive (even for a bad width or progress)
bool FSOLSolidMenuPauseSlideTest::RunTest(const FString& /*parameters*/)
{
    const float margin = SOL::SolidMenu::PAUSE_SLIDE_MARGIN_PX;
    const float width = 420.0f;
    TestEqual(TEXT("closed: off screen by width + margin"), SOLSolidMenu::PauseSlideX(0.0f, width),
        -(width + margin), SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("open: 0"), SOLSolidMenu::PauseSlideX(1.0f, width), 0.0f, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("above 1 clamps"), SOLSolidMenu::PauseSlideX(2.0f, width), 0.0f, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("below 0 clamps"), SOLSolidMenu::PauseSlideX(-1.0f, width), -(width + margin),
        SOLIDMENUTEST_TOLERANCE);

    FSOLSolidMenuSweepLog log;
    float last = SOLSolidMenu::PauseSlideX(0.0f, width);
    for (int32 step = 1; step <= 100; ++step)
    {
        const float p = static_cast<float>(step) / 100.0f;
        const float value = SOLSolidMenu::PauseSlideX(p, width);
        const float expected = -(width + margin) * SOLSolidMenu::PanelSlide(p) / SOL::SolidMenu::PANEL_SLIDE_PX;
        if (value < last - SOLIDMENUTEST_TOLERANCE || value > 0.0f
            || !FMath::IsNearlyEqual(value, expected, 1.0e-3f))
        {
            log.Fail(FString::Printf(TEXT("p %f: %f (previous %f, expected %f)"), p, value, last, expected));
        }
        last = value;
    }
    log.Report(*this, TEXT("non-decreasing, never positive, same ease as PanelSlide"));

    const float widths[] = { 0.0f, -1000.0f, -10.0f, SOLIDMENUTEST_NAN, -SOLIDMENUTEST_INF };
    const float progresses[] = { 0.0f, 0.3f, 1.0f, SOLIDMENUTEST_NAN };
    for (const float w : widths)
    {
        for (const float p : progresses)
        {
            TestFalse(FString::Printf(TEXT("width %f progress %f is not positive"), w, p),
                SOLSolidMenu::PauseSlideX(p, w) > 0.0f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuDimAlphaTest, "SOLTest.SolidMenu.Panel.DimAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// DimAlpha = PAUSE_DIM_ALPHA x SOLHoloMenu::FillFade(progress); at strength 0 the full dim at once (not an effect)
bool FSOLSolidMenuDimAlphaTest::RunTest(const FString& /*parameters*/)
{
    const float dim = SOL::SolidMenu::PAUSE_DIM_ALPHA;
    TestEqual(TEXT("open: full dim"), SOLSolidMenu::DimAlpha(1.0f, 1.0f), dim, SOLIDMENUTEST_TOLERANCE);
    TestEqual(TEXT("closed: no dim"), SOLSolidMenu::DimAlpha(0.0f, 1.0f), 0.0f, SOLIDMENUTEST_TOLERANCE);

    FSOLSolidMenuSweepLog log;
    for (int32 step = 0; step <= 100; ++step)
    {
        const float p = static_cast<float>(step) / 100.0f;
        const float expected = dim * SOLHoloMenu::FillFade(p);
        const float one = SOLSolidMenu::DimAlpha(p, 1.0f);
        const float two = SOLSolidMenu::DimAlpha(p, 2.0f);
        if (!FMath::IsNearlyEqual(one, expected, SOLIDMENUTEST_TOLERANCE)
            || !FMath::IsNearlyEqual(two, expected, SOLIDMENUTEST_TOLERANCE))
        {
            log.Fail(FString::Printf(TEXT("p %f: s1 %f s2 %f expected %f"), p, one, two, expected));
        }
    }
    log.Report(*this, TEXT("dim follows the fill fade at strength 1 and 2"));

    const float zeros[] = { 0.0f, -1.0f, SOLIDMENUTEST_NAN };
    const float progresses[] = { 0.0f, 0.3f, 1.0f };
    for (const float s : zeros)
    {
        for (const float p : progresses)
        {
            TestEqual(FString::Printf(TEXT("strength %f progress %f: full dim at once"), s, p),
                SOLSolidMenu::DimAlpha(p, s), dim, SOLIDMENUTEST_TOLERANCE);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Traces (SDD 8.4)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuTracePowerTest, "SOLTest.SolidMenu.Trace.PowerDelayAndRamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// TracePower: 0 before TRACE_POWER_DELAY_S, then linear to 1 over TRACE_POWER_S, then 1; monotonic; same at strength 2
bool FSOLSolidMenuTracePowerTest::RunTest(const FString& /*parameters*/)
{
    const double delay = SOL::SolidMenu::TRACE_POWER_DELAY_S;
    const double power = SOL::SolidMenu::TRACE_POWER_S;
    const float tol = SOLIDMENUTEST_TOLERANCE;

    TestEqual(TEXT("at open"), SOLSolidMenu::TracePower(0.0, 1.0f), 0.0f, tol);
    TestEqual(TEXT("before open"), SOLSolidMenu::TracePower(-1.0, 1.0f), 0.0f, tol);
    TestEqual(TEXT("half the delay"), SOLSolidMenu::TracePower(0.5 * delay, 1.0f), 0.0f, tol);
    TestEqual(TEXT("at the delay"), SOLSolidMenu::TracePower(delay, 1.0f), 0.0f, tol);
    TestEqual(TEXT("a quarter in"), SOLSolidMenu::TracePower(delay + 0.25 * power, 1.0f), 0.25f, tol);
    TestEqual(TEXT("half way"), SOLSolidMenu::TracePower(delay + 0.5 * power, 1.0f), 0.5f, tol);
    TestEqual(TEXT("at the end"), SOLSolidMenu::TracePower(delay + power, 1.0f), 1.0f, tol);
    TestEqual(TEXT("long after"), SOLSolidMenu::TracePower(delay + power + 100.0, 1.0f), 1.0f, tol);
    TestEqual(TEXT("half way at strength 2"), SOLSolidMenu::TracePower(delay + 0.5 * power, 2.0f), 0.5f, tol);
    TestEqual(TEXT("half way at strength 0.5"), SOLSolidMenu::TracePower(delay + 0.5 * power, 0.5f), 0.5f, tol);

    FSOLSolidMenuSweepLog log;
    float last = SOLSolidMenu::TracePower(0.0, 1.0f);
    const double end = delay + power + 0.2;
    for (int32 step = 1; step <= 200; ++step)
    {
        const double t = end * static_cast<double>(step) / 200.0;
        const float value = SOLSolidMenu::TracePower(t, 1.0f);
        if (value < last - tol || value < 0.0f || value > 1.0f)
        {
            log.Fail(FString::Printf(TEXT("t %f: %f (previous %f)"), t, value, last));
        }
        last = value;
    }
    log.Report(*this, TEXT("monotonic in [0, 1]"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuTraceStrengthZeroTest, "SOLTest.SolidMenu.Trace.PowerStrengthZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// At strength 0 (and negative or NaN, sanitized to 0) the traces are lit at once
bool FSOLSolidMenuTraceStrengthZeroTest::RunTest(const FString& /*parameters*/)
{
    const float zeros[] = { 0.0f, -1.0f, SOLIDMENUTEST_NAN };
    const double times[] = { 0.0, 0.5 * SOL::SolidMenu::TRACE_POWER_DELAY_S, SOL::SolidMenu::TRACE_POWER_DELAY_S
        + 0.5 * SOL::SolidMenu::TRACE_POWER_S };
    for (const float s : zeros)
    {
        for (const double t : times)
        {
            TestEqual(FString::Printf(TEXT("strength %f at %f: lit"), s, t), SOLSolidMenu::TracePower(t, s), 1.0f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPolylinePartialTest, "SOLTest.SolidMenu.Trace.PolylinePartialSegment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The first fraction of a polyline's length: its whole segments, then the partial last one
bool FSOLSolidMenuPolylinePartialTest::RunTest(const FString& /*parameters*/)
{
    // An L-shaped path of three 10-long segments, length 30
    const FVector2f path[] = {
        FVector2f(0.0f, 0.0f), FVector2f(10.0f, 0.0f), FVector2f(10.0f, 10.0f), FVector2f(0.0f, 10.0f) };
    FVector2f out[SOLIDMENUTEST_OUT_SIZE];

    SolidMenuTestFillSentinel(out);
    int32 count = SOLSolidMenu::TracePolyline(path, 4, 0.25f, out, SOLIDMENUTEST_OUT_SIZE);
    TestEqual(TEXT("a quarter: 2 points"), count, 2);
    SolidMenuTestPointEqual(*this, TEXT("a quarter: start"), out[0], path[0]);
    SolidMenuTestPointEqual(*this, TEXT("a quarter: end part way along the first segment"), out[1],
        FVector2f(7.5f, 0.0f));
    TestEqual(TEXT("a quarter: nothing written after"), SolidMenuTestSentinelsFrom(out, 2),
        SOLIDMENUTEST_OUT_SIZE - 2);

    SolidMenuTestFillSentinel(out);
    count = SOLSolidMenu::TracePolyline(path, 4, 0.5f, out, SOLIDMENUTEST_OUT_SIZE);
    TestEqual(TEXT("a half: 3 points"), count, 3);
    SolidMenuTestPointEqual(*this, TEXT("a half: start"), out[0], path[0]);
    SolidMenuTestPointEqual(*this, TEXT("a half: the first corner"), out[1], path[1]);
    SolidMenuTestPointEqual(*this, TEXT("a half: half way down the second segment"), out[2], FVector2f(10.0f, 5.0f));

    SolidMenuTestFillSentinel(out);
    count = SOLSolidMenu::TracePolyline(path, 4, 0.9f, out, SOLIDMENUTEST_OUT_SIZE);
    TestEqual(TEXT("0.9: 4 points"), count, 4);
    SolidMenuTestPointEqual(*this, TEXT("0.9: the second corner"), out[2], path[2]);
    SolidMenuTestPointEqual(*this, TEXT("0.9: part way along the last segment"), out[3], FVector2f(3.0f, 10.0f));

    // The whole path, and a fraction above 1 clamps to it
    const float wholes[] = { 1.0f, 1.5f };
    for (const float fraction : wholes)
    {
        SolidMenuTestFillSentinel(out);
        count = SOLSolidMenu::TracePolyline(path, 4, fraction, out, SOLIDMENUTEST_OUT_SIZE);
        const FString label = FString::Printf(TEXT("fraction %f"), fraction);
        TestEqual(label + TEXT(": every point"), count, 4);
        for (int32 index = 0; index < FMath::Min(count, 4); ++index)
        {
            SolidMenuTestPointEqual(*this, label + FString::Printf(TEXT(" point %d"), index), out[index],
                path[index]);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPolylineZeroTest, "SOLTest.SolidMenu.Trace.PolylineZeroFraction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Fraction 0 (or below), or fewer than 2 points, returns 0 and writes nothing
bool FSOLSolidMenuPolylineZeroTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f path[] = { FVector2f(0.0f, 0.0f), FVector2f(10.0f, 0.0f), FVector2f(10.0f, 10.0f) };
    FVector2f out[SOLIDMENUTEST_OUT_SIZE];

    const float zeros[] = { 0.0f, -0.5f };
    for (const float fraction : zeros)
    {
        SolidMenuTestFillSentinel(out);
        TestEqual(FString::Printf(TEXT("fraction %f returns 0"), fraction),
            SOLSolidMenu::TracePolyline(path, 3, fraction, out, SOLIDMENUTEST_OUT_SIZE), 0);
        TestEqual(FString::Printf(TEXT("fraction %f writes nothing"), fraction), SolidMenuTestSentinelsFrom(out, 0),
            SOLIDMENUTEST_OUT_SIZE);
    }

    SolidMenuTestFillSentinel(out);
    TestEqual(TEXT("one point returns 0"), SOLSolidMenu::TracePolyline(path, 1, 1.0f, out, SOLIDMENUTEST_OUT_SIZE), 0);
    TestEqual(TEXT("no points returns 0"), SOLSolidMenu::TracePolyline(path, 0, 1.0f, out, SOLIDMENUTEST_OUT_SIZE), 0);
    TestEqual(TEXT("a null path returns 0"),
        SOLSolidMenu::TracePolyline(nullptr, 0, 1.0f, out, SOLIDMENUTEST_OUT_SIZE), 0);
    TestEqual(TEXT("too few points write nothing"), SolidMenuTestSentinelsFrom(out, 0), SOLIDMENUTEST_OUT_SIZE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPolylineCapacityTest, "SOLTest.SolidMenu.Trace.PolylineCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Never writes past outCapacity and never returns more points than it
bool FSOLSolidMenuPolylineCapacityTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f path[] = {
        FVector2f(0.0f, 0.0f), FVector2f(10.0f, 0.0f), FVector2f(10.0f, 10.0f), FVector2f(0.0f, 10.0f) };
    FVector2f out[SOLIDMENUTEST_OUT_SIZE];
    const float fractions[] = { 0.25f, 0.5f, 0.9f, 1.0f };
    for (int32 capacity = 0; capacity <= 4; ++capacity)
    {
        for (const float fraction : fractions)
        {
            SolidMenuTestFillSentinel(out);
            const int32 count = SOLSolidMenu::TracePolyline(path, 4, fraction, out, capacity);
            const FString label = FString::Printf(TEXT("capacity %d fraction %f"), capacity, fraction);
            TestTrue(label + TEXT(": count within capacity"), count >= 0 && count <= capacity);
            TestTrue(label + TEXT(": a drawable line or nothing"), count == 0 || count >= 2);
            TestEqual(label + TEXT(": nothing past capacity"), SolidMenuTestSentinelsFrom(out, capacity),
                SOLIDMENUTEST_OUT_SIZE - capacity);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPolylineLengthTest, "SOLTest.SolidMenu.Trace.PolylineLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// On a path with diagonal segments the output starts at the first point, keeps the input points in order and is
// fraction x the total length long
bool FSOLSolidMenuPolylineLengthTest::RunTest(const FString& /*parameters*/)
{
    // Segments of 50, 40 and 40: length 130
    const FVector2f path[] = {
        FVector2f(0.0f, 0.0f), FVector2f(30.0f, 40.0f), FVector2f(30.0f, 0.0f), FVector2f(-10.0f, 0.0f) };
    const float total = SolidMenuTestPolylineLength(path, 4);
    FVector2f out[SOLIDMENUTEST_OUT_SIZE];
    FSOLSolidMenuSweepLog log;
    for (int32 step = 0; step < 10; ++step)
    {
        const float fraction = 0.05f + 0.1f * static_cast<float>(step);
        SolidMenuTestFillSentinel(out);
        const int32 count = SOLSolidMenu::TracePolyline(path, 4, fraction, out, SOLIDMENUTEST_OUT_SIZE);
        if (count < 2 || count > 4)
        {
            log.Fail(FString::Printf(TEXT("fraction %f: count %d"), fraction, count));
            continue;
        }
        const float length = SolidMenuTestPolylineLength(out, count);
        bool bPrefix = true;
        for (int32 index = 0; index < count - 1; ++index)
        {
            bPrefix = bPrefix && FVector2f::Distance(out[index], path[index]) <= SOLIDMENUTEST_PX_TOLERANCE;
        }
        if (!FMath::IsNearlyEqual(length, fraction * total, SOLIDMENUTEST_PX_TOLERANCE) || !bPrefix)
        {
            log.Fail(FString::Printf(TEXT("fraction %f: length %f expected %f prefix %d"), fraction, length,
                fraction * total, bPrefix ? 1 : 0));
        }
    }
    log.Report(*this, TEXT("partial polyline length and prefix"));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Idle pulse (SDD 8.4)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPulseZeroTest, "SOLTest.SolidMenu.Pulse.StrengthZeroExactOne",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// At strength 0 (and negative or NaN, sanitized to 0) the pulse is exactly 1 at any time
bool FSOLSolidMenuPulseZeroTest::RunTest(const FString& /*parameters*/)
{
    const float period = SOL::SolidMenu::IDLE_PULSE_PERIOD_S;
    const float zeros[] = { 0.0f, -1.0f, SOLIDMENUTEST_NAN };
    const double times[] = { 0.0, 0.25 * period, 0.75 * period, 1234.5, 1.0e6 };
    for (const float s : zeros)
    {
        for (const double t : times)
        {
            TestTrue(FString::Printf(TEXT("strength %f at %f is exactly 1"), s, t),
                SOLSolidMenu::IdlePulse(t, s) == 1.0f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPulseShapeTest, "SOLTest.SolidMenu.Pulse.PeriodAndDepth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 1 + IDLE_PULSE_DEPTH x min(s, 2) x sin(2 pi t / IDLE_PULSE_PERIOD_S): peaks, troughs, period and the strength cap
bool FSOLSolidMenuPulseShapeTest::RunTest(const FString& /*parameters*/)
{
    const double period = SOL::SolidMenu::IDLE_PULSE_PERIOD_S;
    const float depth = SOL::SolidMenu::IDLE_PULSE_DEPTH;
    const float tol = SOLIDMENUTEST_TOLERANCE;

    TestEqual(TEXT("1 at t 0"), SOLSolidMenu::IdlePulse(0.0, 1.0f), 1.0f, tol);
    TestEqual(TEXT("peak at a quarter period"), SOLSolidMenu::IdlePulse(0.25 * period, 1.0f), 1.0f + depth, tol);
    TestEqual(TEXT("1 at half a period"), SOLSolidMenu::IdlePulse(0.5 * period, 1.0f), 1.0f, tol);
    TestEqual(TEXT("trough at three quarters"), SOLSolidMenu::IdlePulse(0.75 * period, 1.0f), 1.0f - depth, tol);
    TestEqual(TEXT("strength 0.5 peak"), SOLSolidMenu::IdlePulse(0.25 * period, 0.5f), 1.0f + 0.5f * depth, tol);
    TestEqual(TEXT("strength 2 peak doubled"), SOLSolidMenu::IdlePulse(0.25 * period, 2.0f), 1.0f + 2.0f * depth,
        tol);
    TestEqual(TEXT("strength 5 capped at 2"), SOLSolidMenu::IdlePulse(0.25 * period, 5.0f), 1.0f + 2.0f * depth, tol);

    FSOLSolidMenuSweepLog log;
    for (int32 step = 0; step < 100; ++step)
    {
        const double t = 0.137 * static_cast<double>(step);
        const float value = SOLSolidMenu::IdlePulse(t, 1.0f);
        const float next = SOLSolidMenu::IdlePulse(t + period, 1.0f);
        const float expected = 1.0f + depth * static_cast<float>(FMath::Sin(2.0 * UE_DOUBLE_PI * t / period));
        if (!FMath::IsNearlyEqual(value, expected, tol) || !FMath::IsNearlyEqual(value, next, tol))
        {
            log.Fail(FString::Printf(TEXT("t %f: %f next period %f expected %f"), t, value, next, expected));
        }
    }
    log.Report(*this, TEXT("sine of the period"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPulseNaNTest, "SOLTest.SolidMenu.Pulse.NaNTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A NaN nowS reads 1 at any strength
bool FSOLSolidMenuPulseNaNTest::RunTest(const FString& /*parameters*/)
{
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    for (const float s : strengths)
    {
        TestTrue(FString::Printf(TEXT("NaN time at strength %f is 1"), s),
            SOLSolidMenu::IdlePulse(SOLIDMENUTEST_NAN_D, s) == 1.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuPulseLargeTimeTest, "SOLTest.SolidMenu.Pulse.LargeTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Real application time can be large: the pulse stays exact (double time) a day into the session
bool FSOLSolidMenuPulseLargeTimeTest::RunTest(const FString& /*parameters*/)
{
    const double period = SOL::SolidMenu::IDLE_PULSE_PERIOD_S;
    const float depth = SOL::SolidMenu::IDLE_PULSE_DEPTH;
    const double base = period * 25000.0;
    TestEqual(TEXT("peak far into the session"), SOLSolidMenu::IdlePulse(base + 0.25 * period, 1.0f), 1.0f + depth,
        1.0e-3f);
    TestEqual(TEXT("trough far into the session"), SOLSolidMenu::IdlePulse(base + 0.75 * period, 1.0f), 1.0f - depth,
        1.0e-3f);
    const float value = SOLSolidMenu::IdlePulse(1.0e9, 1.0f);
    TestTrue(TEXT("finite and in range at 1e9 s"), FMath::IsFinite(value) && value >= 1.0f - depth - 1.0e-3f
        && value <= 1.0f + depth + 1.0e-3f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
