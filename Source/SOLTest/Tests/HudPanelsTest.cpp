/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudShapeMath.h"
#include "UI/SOLHudTextList.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // Tolerances
    constexpr float HUDPANELS_TOLERANCE = 1.0e-5f;          // Pure math values (relative, scaled by max(1, |expected|))
    constexpr float HUDPANELS_WRAP_TOLERANCE = 1.0e-3f;     // Flicker at age vs age + wrap (float fmod near the wrap)
    constexpr float HUDPANELS_POS_TOLERANCE = 1.0e-4f;      // Panel centres and half sizes in pixels
    constexpr float HUDPANELS_BACKING_TOLERANCE = 1.0e-4f;  // Backing layer rects, corners and alphas

    // Pad fade geometry
    constexpr float HUDPANELS_PAD_PX = 8.0f;

    // Panel fill parameters
    constexpr float HUDPANELS_FILL = 0.6f;
    constexpr float HUDPANELS_STRENGTH = 0.7f;
    constexpr float HUDPANELS_DEEP_INSIDE_PX = -1000.0f;

    // Sweeps
    constexpr int32 HUDPANELS_SWEEP_STEPS = 400;
    constexpr float HUDPANELS_FLICKER_MAX_AGE_S = 50.0f;
    constexpr float HUDPANELS_TONE_MAX = 3.0f;

    // Text list capacities
    constexpr int32 HUDPANELS_SMALL_CAPACITY = 3;

    //////////////////////////////////////////////////////////////////////////
    // Checks two floats agree within a relative tolerance (absolute below magnitude 1)
    void HudPanelsTestNear(FAutomationTestBase& test, const FString& label, const float actual, const double expected,
        const float tolerance = HUDPANELS_TOLERANCE)
    {
        const double scale = FMath::Max(1.0, FMath::Abs(expected));
        test.TestEqual(label, static_cast<double>(actual), expected, static_cast<double>(tolerance) * scale);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the standard smoothstep of x between e0 and e1 (the reference formula)
    double HudPanelsSmoothstep(const double e0, const double e1, const double x)
    {
        const double t = FMath::Clamp((x - e0) / (e1 - e0), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the pad fade computed straight from the SOL::Hologram constants (the reference formula)
    double HudPanelsReferencePadFade(const double distancePx, const double padPx)
    {
        if (padPx <= 0.0)
        {
            return 1.0;
        }
        return 1.0 - HudPanelsSmoothstep(SOL::Hologram::HUD_PAD_FADE_START * padPx, padPx, distancePx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the panel fill computed straight from the SOL::Hologram constants (the reference formula)
    double HudPanelsReferenceFill(const double distancePx, const double fill, const double screenY,
        const double strength)
    {
        using namespace SOL::Hologram;
        if (distancePx >= 0.0)
        {
            return 0.0;
        }
        const double feather = HudPanelsSmoothstep(0.0, HUD_PANEL_FEATHER_PX, -distancePx);
        const double scan = 1.0 + HUD_PANEL_SCANLINE * FMath::Max(strength, 0.0) * FMath::Sin(UE_DOUBLE_PI * screenY);
        return fill * feather * scan;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the full (unfeathered) scanlined fill at a screen row: the value deep inside the panel
    double HudPanelsFullFill(const double fill, const double screenY, const double strength)
    {
        using namespace SOL::Hologram;
        return fill * (1.0 + HUD_PANEL_SCANLINE * FMath::Max(strength, 0.0) * FMath::Sin(UE_DOUBLE_PI * screenY));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the panel flicker computed from the constants with SmoothNoise as the noise oracle
    double HudPanelsReferenceFlicker(const float ageS, const float strength)
    {
        using namespace SOL::Hologram;
        const float x = static_cast<float>(FMath::Fmod(ageS, HUD_TIME_WRAP_S) * HUD_PANEL_FLICKER_RATE_HZ
            + HUD_PANEL_FLICKER_OFFSET);
        return 1.0 - HUD_PANEL_FLICKER * FMath::Max(static_cast<double>(strength), 0.0)
            * SOLHudShapeMath::SmoothNoise(x);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns LT's tone curve computed straight from the constants (the reference formula)
    double HudPanelsReferenceTone(const double c)
    {
        using namespace SOL::Hologram;
        return 1.0 - FMath::Exp(-HUD_TONE_GAIN * FMath::Pow(FMath::Max(c, 0.0), HUD_TONE_POWER));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns LT's blue grade quartic Bezier computed straight from the constants (the reference formula)
    double HudPanelsReferenceGrade(const double tIn)
    {
        using namespace SOL::Hologram;
        const double t = FMath::Clamp(tIn, 0.0, 1.0);
        const double u = 1.0 - t;
        return 4.0 * t * u * u * u * HUD_GRADE_BLUE_1 + 6.0 * t * t * u * u * HUD_GRADE_BLUE_2
            + 4.0 * t * t * t * u * HUD_GRADE_BLUE_3 + t * t * t * t;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a distinct test text for line index i
    FText HudPanelsText(const int32 index)
    {
        return FText::FromString(FString::Printf(TEXT("Line %d"), index));
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds a simple line i to a list and returns Add's result
    bool HudPanelsAddLine(FSOLHudTextList& list, const int32 index)
    {
        return list.Add(HudPanelsText(index), FVector2f(10.0f * index, 20.0f * index), 1.0f, FLinearColor::White,
            ESOLHudFont::Small);
    }
}

// --- SOLHudShapeMath: panels, fade and grading ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsPadFadeTest, "SOLTest.HudPanels.PadFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The pad fade is 1 up to the fade start, 0 from the pad on, smooth and non-increasing between, and 1 without a pad
bool FSOLHudPanelsPadFadeTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::PadFade;
    constexpr float pad = HUDPANELS_PAD_PX;
    const float fadeStart = static_cast<float>(SOL::Hologram::HUD_PAD_FADE_START) * pad;

    // Flat regions and boundaries
    HudPanelsTestNear(*this, TEXT("inside the shape is 1"), PadFade(-3.0f, pad), 1.0);
    HudPanelsTestNear(*this, TEXT("at the core is 1"), PadFade(0.0f, pad), 1.0);
    HudPanelsTestNear(*this, TEXT("at the fade start is 1"), PadFade(fadeStart, pad), 1.0);
    HudPanelsTestNear(*this, TEXT("at the pad is 0"), PadFade(pad, pad), 0.0);
    HudPanelsTestNear(*this, TEXT("past the pad is 0"), PadFade(pad + 5.0f, pad), 0.0);

    // Formula between the edges
    const float samples[] = { fadeStart + 0.1f, 0.5f * (fadeStart + pad), pad - 0.1f, 0.25f * fadeStart + 0.75f * pad };
    for (const float d : samples)
    {
        HudPanelsTestNear(*this, FString::Printf(TEXT("formula at %.3f px"), d), PadFade(d, pad),
            HudPanelsReferencePadFade(d, pad));
    }
    HudPanelsTestNear(*this, TEXT("midway is one half"), PadFade(0.5f * (fadeStart + pad), pad), 0.5);

    // Non-increasing across the pad and within [0, 1]
    bool bNonIncreasing = true;
    bool bInRange = true;
    float previous = PadFade(0.0f, pad);
    for (int32 i = 1; i <= HUDPANELS_SWEEP_STEPS; ++i)
    {
        const float d = (pad + 2.0f) * static_cast<float>(i) / static_cast<float>(HUDPANELS_SWEEP_STEPS);
        const float value = PadFade(d, pad);
        bNonIncreasing &= value <= previous + HUDPANELS_TOLERANCE;
        bInRange &= value >= -HUDPANELS_TOLERANCE && value <= 1.0f + HUDPANELS_TOLERANCE;
        previous = value;
    }
    TestTrue(TEXT("non-increasing in distance"), bNonIncreasing);
    TestTrue(TEXT("in [0, 1]"), bInRange);

    // No pad: no fade at any distance
    HudPanelsTestNear(*this, TEXT("pad 0 at 0 px"), PadFade(0.0f, 0.0f), 1.0);
    HudPanelsTestNear(*this, TEXT("pad 0 at 10 px"), PadFade(10.0f, 0.0f), 1.0);
    HudPanelsTestNear(*this, TEXT("negative pad at 10 px"), PadFade(10.0f, -4.0f), 1.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsPanelFillTest, "SOLTest.HudPanels.PanelFill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The fill is 0 outside, fades in from the edge over HUD_PANEL_FEATHER_PX (no bright rim), and is the full scanlined
// fill from the feather inward
bool FSOLHudPanelsPanelFillTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::PanelFill;
    using namespace SOL::Hologram;
    constexpr float fill = HUDPANELS_FILL;
    constexpr float s = HUDPANELS_STRENGTH;
    const float feather = static_cast<float>(HUD_PANEL_FEATHER_PX);

    // The contract's constants
    HudPanelsTestNear(*this, TEXT("feather is 8 px"), feather, 8.0);
    HudPanelsTestNear(*this, TEXT("scanline is 0.2"), static_cast<float>(HUD_PANEL_SCANLINE), 0.2);

    // Outside (and on the edge) is empty
    HudPanelsTestNear(*this, TEXT("on the edge is 0"), PanelFill(0.0f, fill, 0.5f, s), 0.0);
    HudPanelsTestNear(*this, TEXT("outside is 0"), PanelFill(3.0f, fill, 0.5f, s), 0.0);
    HudPanelsTestNear(*this, TEXT("far outside is 0"), PanelFill(100.0f, fill, 10.25f, s), 0.0);

    // Just inside the edge the fill tends to 0
    TestTrue(TEXT("just inside the edge is near 0"), PanelFill(-0.01f, fill, 0.5f, s) < 1.0e-3f * fill);
    TestTrue(TEXT("just inside the edge is not negative"), PanelFill(-0.01f, fill, 0.5f, s) >= 0.0f);

    // Formula inside at several depths and rows
    const float depths[] = { -0.01f, -1.0f, -3.0f, -4.0f, -6.0f, -7.99f, -8.0f, -20.0f };
    const float rows[] = { 0.0f, 0.25f, 0.5f, 7.3f, 123.75f };
    for (const float d : depths)
    {
        for (const float y : rows)
        {
            HudPanelsTestNear(*this, FString::Printf(TEXT("formula at d %.2f y %.2f"), d, y), PanelFill(d, fill, y, s),
                HudPanelsReferenceFill(d, fill, y, s));
        }
    }

    // Exactly half way through the feather the smoothstep is one half
    const float halfDepth = -0.5f * feather;
    HudPanelsTestNear(*this, TEXT("half feather is half the fill (y 0)"), PanelFill(halfDepth, fill, 0.0f, s),
        0.5 * HudPanelsFullFill(fill, 0.0, s));
    HudPanelsTestNear(*this, TEXT("half feather is half the fill (y 0.5)"), PanelFill(halfDepth, fill, 0.5f, s),
        0.5 * HudPanelsFullFill(fill, 0.5, s));

    // At and beyond the feather the fill is the full scanlined fill
    const float fullDepths[] = { -feather, -feather - 0.5f, -20.0f, HUDPANELS_DEEP_INSIDE_PX };
    for (const float d : fullDepths)
    {
        HudPanelsTestNear(*this, FString::Printf(TEXT("full fill at d %.2f (y 0.5)"), d), PanelFill(d, fill, 0.5f, s),
            HudPanelsFullFill(fill, 0.5, s));
        HudPanelsTestNear(*this, FString::Printf(TEXT("full fill at d %.2f (y 1.5)"), d), PanelFill(d, fill, 1.5f, s),
            HudPanelsFullFill(fill, 1.5, s));
    }

    // Monotonic non-decreasing going inward, and never brighter near the edge than deep inside (no bright rim)
    const float rowsForSweep[] = { 0.0f, 0.5f, 1.5f };
    for (const float y : rowsForSweep)
    {
        const float deep = PanelFill(HUDPANELS_DEEP_INSIDE_PX, fill, y, s);
        bool bNonDecreasing = true;
        bool bNoRim = true;
        float previous = PanelFill(0.0f, fill, y, s);
        for (int32 i = 1; i <= HUDPANELS_SWEEP_STEPS; ++i)
        {
            const float d = -(feather + 2.0f) * static_cast<float>(i) / static_cast<float>(HUDPANELS_SWEEP_STEPS);
            const float value = PanelFill(d, fill, y, s);
            bNonDecreasing &= value >= previous - HUDPANELS_TOLERANCE;
            bNoRim &= value <= deep + HUDPANELS_TOLERANCE;
            previous = value;
        }
        TestTrue(FString::Printf(TEXT("non-decreasing inward at y %.1f"), y), bNonDecreasing);
        TestTrue(FString::Printf(TEXT("no brighter edge at y %.1f"), y), bNoRim);
    }

    // Strength 0 (and negative strength) removes the scanlines: no row dependence
    const double noScan = HudPanelsReferenceFill(-2.0, fill, 0.0, 0.0);
    HudPanelsTestNear(*this, TEXT("strength 0 at y 0.5"), PanelFill(-2.0f, fill, 0.5f, 0.0f), noScan);
    HudPanelsTestNear(*this, TEXT("strength 0 at y 1.5"), PanelFill(-2.0f, fill, 1.5f, 0.0f), noScan);
    HudPanelsTestNear(*this, TEXT("negative strength at y 0.5"), PanelFill(-2.0f, fill, 0.5f, -1.0f), noScan);
    HudPanelsTestNear(*this, TEXT("negative strength at y 1.5"), PanelFill(-2.0f, fill, 1.5f, -1.0f), noScan);
    HudPanelsTestNear(*this, TEXT("strength 0 deep inside is the plain fill"),
        PanelFill(HUDPANELS_DEEP_INSIDE_PX, fill, 0.5f, 0.0f), fill);

    // Zero fill is dark everywhere inside
    HudPanelsTestNear(*this, TEXT("fill 0 near the edge"), PanelFill(-0.5f, 0.0f, 0.5f, s), 0.0);
    HudPanelsTestNear(*this, TEXT("fill 0 deep inside"), PanelFill(-50.0f, 0.0f, 0.5f, s), 0.0);

    // Deep inside: fill x scan term
    const double base = fill;
    HudPanelsTestNear(*this, TEXT("deep inside, y 0 (sin 0)"), PanelFill(HUDPANELS_DEEP_INSIDE_PX, fill, 0.0f, s),
        base);
    HudPanelsTestNear(*this, TEXT("deep inside, y 0.5 (sin +1)"), PanelFill(HUDPANELS_DEEP_INSIDE_PX, fill, 0.5f, s),
        base * (1.0 + HUD_PANEL_SCANLINE * s));
    HudPanelsTestNear(*this, TEXT("deep inside, y 1.5 (sin -1)"), PanelFill(HUDPANELS_DEEP_INSIDE_PX, fill, 1.5f, s),
        base * (1.0 - HUD_PANEL_SCANLINE * s));
    HudPanelsTestNear(*this, TEXT("deep inside, y 2.5 (sin +1)"), PanelFill(HUDPANELS_DEEP_INSIDE_PX, fill, 2.5f, s),
        base * (1.0 + HUD_PANEL_SCANLINE * s));

    // Scanlines alternate row to row at half-pixel rows
    for (int32 n = 0; n < 6; ++n)
    {
        const float a = PanelFill(-4.0f, fill, static_cast<float>(n) + 0.5f, s);
        const float b = PanelFill(-4.0f, fill, static_cast<float>(n) + 1.5f, s);
        TestTrue(FString::Printf(TEXT("row %d and %d differ in brightness"), n, n + 1), (n % 2 == 0) ? a > b : a < b);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsFlickerTest, "SOLTest.HudPanels.Flicker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The flicker matches its SmoothNoise formula, stays in [1 - FLICKER x s, 1], is off without strength and wraps in time
bool FSOLHudPanelsFlickerTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::PanelFlicker;
    using namespace SOL::Hologram;
    constexpr float s = HUDPANELS_STRENGTH;
    const float lower = static_cast<float>(1.0 - HUD_PANEL_FLICKER * s);

    bool bBounded = true;
    bool bOffAtZero = true;
    bool bOffNegative = true;
    bool bVaries = false;
    const float first = PanelFlicker(0.0f, s);
    for (int32 i = 0; i <= HUDPANELS_SWEEP_STEPS; ++i)
    {
        const float age = HUDPANELS_FLICKER_MAX_AGE_S * static_cast<float>(i)
            / static_cast<float>(HUDPANELS_SWEEP_STEPS);
        const float value = PanelFlicker(age, s);
        bBounded &= value >= lower - HUDPANELS_TOLERANCE && value <= 1.0f + HUDPANELS_TOLERANCE;
        bOffAtZero &= PanelFlicker(age, 0.0f) == 1.0f;
        bOffNegative &= PanelFlicker(age, -2.0f) == 1.0f;
        bVaries |= FMath::Abs(value - first) > 0.1f * static_cast<float>(HUD_PANEL_FLICKER) * s;
    }
    TestTrue(TEXT("in [1 - FLICKER x s, 1]"), bBounded);
    TestTrue(TEXT("exactly 1 at strength 0"), bOffAtZero);
    TestTrue(TEXT("exactly 1 at negative strength"), bOffNegative);
    TestTrue(TEXT("varies over time"), bVaries);

    // Formula with SmoothNoise as the oracle
    const float ages[] = { 0.0f, 0.37f, 1.0f, 2.718f, 13.5f, 499.9f };
    for (const float age : ages)
    {
        HudPanelsTestNear(*this, FString::Printf(TEXT("formula at %.3f s"), age), PanelFlicker(age, s),
            HudPanelsReferenceFlicker(age, s));
        HudPanelsTestNear(*this, FString::Printf(TEXT("formula at %.3f s, strength 1"), age), PanelFlicker(age, 1.0f),
            HudPanelsReferenceFlicker(age, 1.0f));
    }

    // Wraps with HUD_TIME_WRAP_S
    const float wrapAges[] = { 0.25f, 3.7f, 42.0f };
    for (const float age : wrapAges)
    {
        HudPanelsTestNear(*this, FString::Printf(TEXT("wraps at %.2f s"), age), PanelFlicker(age + HUD_TIME_WRAP_S, s),
            PanelFlicker(age, s), HUDPANELS_WRAP_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsToneCurveTest, "SOLTest.HudPanels.ToneCurve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The tone curve is 0 at and below 0, matches LT's formula, rises monotonically and stays below 1
bool FSOLHudPanelsToneCurveTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::ToneCurve;
    HudPanelsTestNear(*this, TEXT("0 maps to 0"), ToneCurve(0.0f), 0.0);
    HudPanelsTestNear(*this, TEXT("negative maps to 0"), ToneCurve(-0.5f), 0.0);
    HudPanelsTestNear(*this, TEXT("very negative maps to 0"), ToneCurve(-100.0f), 0.0);

    const float samples[] = { 0.01f, 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f };
    for (const float c : samples)
    {
        HudPanelsTestNear(*this, FString::Printf(TEXT("formula at %.2f"), c), ToneCurve(c), HudPanelsReferenceTone(c));
    }

    bool bIncreasing = true;
    bool bBelowOne = true;
    float previous = ToneCurve(0.0f);
    for (int32 i = 1; i <= HUDPANELS_SWEEP_STEPS; ++i)
    {
        const float c = HUDPANELS_TONE_MAX * static_cast<float>(i) / static_cast<float>(HUDPANELS_SWEEP_STEPS);
        const float value = ToneCurve(c);
        bIncreasing &= value > previous;
        bBelowOne &= value < 1.0f;
        previous = value;
    }
    TestTrue(TEXT("strictly increasing on (0, 3]"), bIncreasing);
    TestTrue(TEXT("below 1 on (0, 3]"), bBelowOne);
    TestTrue(TEXT("never above 1 for large input"), ToneCurve(1000.0f) <= 1.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsGradeBlueTest, "SOLTest.HudPanels.GradeBlue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The blue grade is the quartic Bezier 0, B1, B2, B3, 1, clamped outside [0, 1] and monotonic inside
bool FSOLHudPanelsGradeBlueTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::GradeBlue;
    HudPanelsTestNear(*this, TEXT("0 maps to 0"), GradeBlue(0.0f), 0.0);
    HudPanelsTestNear(*this, TEXT("1 maps to 1"), GradeBlue(1.0f), 1.0);
    HudPanelsTestNear(*this, TEXT("below 0 clamps to 0"), GradeBlue(-0.5f), 0.0);
    HudPanelsTestNear(*this, TEXT("far below 0 clamps to 0"), GradeBlue(-10.0f), 0.0);
    HudPanelsTestNear(*this, TEXT("above 1 clamps to 1"), GradeBlue(1.5f), 1.0);
    HudPanelsTestNear(*this, TEXT("far above 1 clamps to 1"), GradeBlue(10.0f), 1.0);

    const float samples[] = { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f };
    for (const float t : samples)
    {
        HudPanelsTestNear(*this, FString::Printf(TEXT("formula at %.2f"), t), GradeBlue(t), HudPanelsReferenceGrade(t));
    }

    bool bMonotonic = true;
    float previous = GradeBlue(0.0f);
    for (int32 i = 1; i <= HUDPANELS_SWEEP_STEPS; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(HUDPANELS_SWEEP_STEPS);
        const float value = GradeBlue(t);
        bMonotonic &= value >= previous - HUDPANELS_TOLERANCE;
        previous = value;
    }
    TestTrue(TEXT("non-decreasing on [0, 1]"), bMonotonic);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsPanelAroundRectTest, "SOLTest.HudPanels.PanelAroundRect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The panel is centred on the content and its half size is the content's half size plus the padding
bool FSOLHudPanelsPanelAroundRectTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::PanelAroundRect;
    const FBox2f content(FVector2f(100.0f, 40.0f), FVector2f(180.0f, 70.0f));
    FVector2f center(-1.0f, -1.0f);
    FVector2f half(-1.0f, -1.0f);

    // Padded box
    PanelAroundRect(content, 6.0f, center, half);
    TestEqual(TEXT("padded: centre x"), center.X, 140.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("padded: centre y"), center.Y, 55.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("padded: half x"), half.X, 46.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("padded: half y"), half.Y, 21.0f, HUDPANELS_POS_TOLERANCE);

    // No padding
    PanelAroundRect(content, 0.0f, center, half);
    TestEqual(TEXT("unpadded: centre x"), center.X, 140.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("unpadded: centre y"), center.Y, 55.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("unpadded: half x"), half.X, 40.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("unpadded: half y"), half.Y, 15.0f, HUDPANELS_POS_TOLERANCE);

    // Box straddling the origin
    const FBox2f straddle(FVector2f(-10.0f, -4.0f), FVector2f(30.0f, 4.0f));
    PanelAroundRect(straddle, 2.0f, center, half);
    TestEqual(TEXT("straddle: centre x"), center.X, 10.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("straddle: centre y"), center.Y, 0.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("straddle: half x"), half.X, 22.0f, HUDPANELS_POS_TOLERANCE);
    TestEqual(TEXT("straddle: half y"), half.Y, 6.0f, HUDPANELS_POS_TOLERANCE);

    // Invalid box: zero centre and half size, whatever the padding
    const FBox2f invalid(ForceInit);
    center = FVector2f(5.0f, 5.0f);
    half = FVector2f(5.0f, 5.0f);
    PanelAroundRect(invalid, 6.0f, center, half);
    TestEqual(TEXT("invalid: centre x"), center.X, 0.0f);
    TestEqual(TEXT("invalid: centre y"), center.Y, 0.0f);
    TestEqual(TEXT("invalid: half x"), half.X, 0.0f);
    TestEqual(TEXT("invalid: half y"), half.Y, 0.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsBackingLayerGeometryTest, "SOLTest.HudPanels.BackingLayerGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Backing layers start at the rect, nest inward by feather x index / count, and round their corners outward-first
bool FSOLHudPanelsBackingLayerGeometryTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::BackingLayer;
    constexpr float tol = HUDPANELS_BACKING_TOLERANCE;
    const FBox2f rect(FVector2f(100.0f, 40.0f), FVector2f(300.0f, 120.0f));    // 200 x 80
    constexpr float corner = 6.0f;
    constexpr float feather = 8.0f;
    constexpr int32 count = 6;

    FBox2f previousRect(ForceInit);
    float previousCorner = 0.0f;
    for (int32 index = 0; index < count; ++index)
    {
        const FString label = FString::Printf(TEXT("layer %d"), index);
        FBox2f layer(ForceInit);
        float layerCorner = -1.0f;
        float layerAlpha = -1.0f;
        if (!TestTrue(label + TEXT(": accepted"), BackingLayer(rect, corner, feather, 0.45f, index, count, layer,
            layerCorner, layerAlpha)))
        {
            continue;
        }
        TestTrue(label + TEXT(": valid"), layer.bIsValid != 0);

        // Inset by f x index / count on every side
        const float inset = feather * static_cast<float>(index) / static_cast<float>(count);
        TestEqual(label + TEXT(": min x"), layer.Min.X, rect.Min.X + inset, tol);
        TestEqual(label + TEXT(": min y"), layer.Min.Y, rect.Min.Y + inset, tol);
        TestEqual(label + TEXT(": max x"), layer.Max.X, rect.Max.X - inset, tol);
        TestEqual(label + TEXT(": max y"), layer.Max.Y, rect.Max.Y - inset, tol);

        // Corner radius max(corner, 0) + f - inset, within half the smaller side
        const float width = layer.Max.X - layer.Min.X;
        const float height = layer.Max.Y - layer.Min.Y;
        TestEqual(label + TEXT(": corner"), layerCorner, FMath::Clamp(corner + feather - inset, 0.0f,
            0.5f * FMath::Min(width, height)), tol);
        TestTrue(label + TEXT(": corner within half the smaller side"),
            layerCorner <= 0.5f * FMath::Min(width, height) + tol);

        if (index == 0)
        {
            TestEqual(TEXT("layer 0 is the rect: min x"), layer.Min.X, rect.Min.X, tol);
            TestEqual(TEXT("layer 0 is the rect: min y"), layer.Min.Y, rect.Min.Y, tol);
            TestEqual(TEXT("layer 0 is the rect: max x"), layer.Max.X, rect.Max.X, tol);
            TestEqual(TEXT("layer 0 is the rect: max y"), layer.Max.Y, rect.Max.Y, tol);
        }
        else
        {
            // Nested: inside the previous layer, with a corner no larger than the outer one
            TestTrue(label + TEXT(": nested inside the previous layer"),
                layer.Min.X >= previousRect.Min.X - tol && layer.Min.Y >= previousRect.Min.Y - tol
                && layer.Max.X <= previousRect.Max.X + tol && layer.Max.Y <= previousRect.Max.Y + tol);
            TestTrue(label + TEXT(": strictly smaller than the previous layer"),
                layer.Max.X - layer.Min.X < previousRect.Max.X - previousRect.Min.X);
            TestTrue(label + TEXT(": outer corner >= inner corner"), previousCorner >= layerCorner - tol);
        }
        previousRect = layer;
        previousCorner = layerCorner;
    }

    // A single layer is the rect, with the full alpha and the corner grown by the feather
    FBox2f single(ForceInit);
    float singleCorner = -1.0f;
    float singleAlpha = -1.0f;
    TestTrue(TEXT("count 1 accepted"), BackingLayer(rect, corner, feather, 0.45f, 0, 1, single, singleCorner,
        singleAlpha));
    TestEqual(TEXT("count 1: min x"), single.Min.X, rect.Min.X, tol);
    TestEqual(TEXT("count 1: max y"), single.Max.Y, rect.Max.Y, tol);
    TestEqual(TEXT("count 1: alpha is the total"), singleAlpha, 0.45f, tol);
    TestEqual(TEXT("count 1: corner + feather"), singleCorner, corner + feather, tol);

    // Negative feather is 0: every layer is the rect with the plain corner
    for (int32 index = 0; index < count; ++index)
    {
        FBox2f layer(ForceInit);
        float layerCorner = -1.0f;
        float layerAlpha = -1.0f;
        BackingLayer(rect, corner, -5.0f, 0.45f, index, count, layer, layerCorner, layerAlpha);
        const FString label = FString::Printf(TEXT("no feather layer %d"), index);
        TestEqual(label + TEXT(": min x"), layer.Min.X, rect.Min.X, tol);
        TestEqual(label + TEXT(": min y"), layer.Min.Y, rect.Min.Y, tol);
        TestEqual(label + TEXT(": max x"), layer.Max.X, rect.Max.X, tol);
        TestEqual(label + TEXT(": max y"), layer.Max.Y, rect.Max.Y, tol);
        TestEqual(label + TEXT(": corner"), layerCorner, corner, tol);
    }

    // Negative corner counts as 0 (no feather: corner 0)
    FBox2f sharp(ForceInit);
    float sharpCorner = -1.0f;
    float sharpAlpha = -1.0f;
    BackingLayer(rect, -4.0f, 0.0f, 0.45f, 0, count, sharp, sharpCorner, sharpAlpha);
    TestEqual(TEXT("negative corner, no feather: corner 0"), sharpCorner, 0.0f, tol);

    // A corner larger than the rect allows is clamped to half the smaller side
    FBox2f rounded(ForceInit);
    float roundCorner = -1.0f;
    float roundAlpha = -1.0f;
    BackingLayer(rect, 500.0f, feather, 0.45f, 2, count, rounded, roundCorner, roundAlpha);
    TestEqual(TEXT("huge corner clamped"), roundCorner,
        0.5f * FMath::Min(rounded.Max.X - rounded.Min.X, rounded.Max.Y - rounded.Min.Y), tol);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsBackingLayerAlphaTest, "SOLTest.HudPanels.BackingLayerAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// All layers' alphas composited alpha-over reach the total alpha; the total is clamped to [0, 1]
bool FSOLHudPanelsBackingLayerAlphaTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::BackingLayer;
    constexpr float tol = HUDPANELS_BACKING_TOLERANCE;
    const FBox2f rect(FVector2f(0.0f, 0.0f), FVector2f(160.0f, 48.0f));
    const int32 counts[] = { 1, 6, 8 };
    const float totals[] = { 0.45f, 0.0f, 0.9f, 1.0f };
    for (const int32 count : counts)
    {
        for (const float total : totals)
        {
            double transmit = 1.0;
            float firstAlpha = -1.0f;
            bool bSameAlpha = true;
            for (int32 index = 0; index < count; ++index)
            {
                FBox2f layer(ForceInit);
                float layerCorner = 0.0f;
                float layerAlpha = -1.0f;
                BackingLayer(rect, 6.0f, 8.0f, total, index, count, layer, layerCorner, layerAlpha);
                transmit *= 1.0 - static_cast<double>(layerAlpha);
                if (index == 0)
                {
                    firstAlpha = layerAlpha;
                }
                bSameAlpha &= FMath::Abs(layerAlpha - firstAlpha) <= tol;
            }
            const FString label = FString::Printf(TEXT("count %d total %.2f"), count, total);
            TestEqual(label + TEXT(": composite"), 1.0 - transmit, static_cast<double>(total),
                static_cast<double>(tol));
            TestTrue(label + TEXT(": every layer has the same alpha"), bSameAlpha);
            TestEqual(label + TEXT(": alpha formula"), firstAlpha,
                static_cast<float>(1.0 - FMath::Pow(1.0 - static_cast<double>(total), 1.0 / count)), tol);
        }
    }

    // Out-of-range totals clamp: above 1 -> every layer opaque, below 0 -> every layer clear
    for (int32 index = 0; index < 6; ++index)
    {
        FBox2f layer(ForceInit);
        float layerCorner = 0.0f;
        float over = -1.0f;
        float under = -1.0f;
        BackingLayer(rect, 6.0f, 8.0f, 2.0f, index, 6, layer, layerCorner, over);
        BackingLayer(rect, 6.0f, 8.0f, -1.0f, index, 6, layer, layerCorner, under);
        TestEqual(FString::Printf(TEXT("total 2, layer %d: alpha 1"), index), over, 1.0f, tol);
        TestEqual(FString::Printf(TEXT("total -1, layer %d: alpha 0"), index), under, 0.0f, tol);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsBackingLayerClampTest, "SOLTest.HudPanels.BackingLayerClamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A huge feather is clamped to half the smaller side; invalid input returns false and leaves the outputs untouched
bool FSOLHudPanelsBackingLayerClampTest::RunTest(const FString& /*parameters*/)
{
    using SOLHudShapeMath::BackingLayer;
    constexpr float tol = HUDPANELS_BACKING_TOLERANCE;

    // A small 20 x 10 rect with a 100 px feather: f = 5, the innermost layer still has area
    const FBox2f smallRect(FVector2f(10.0f, 10.0f), FVector2f(30.0f, 20.0f));
    constexpr int32 count = 8;
    FBox2f inner(ForceInit);
    float innerCorner = -1.0f;
    float innerAlpha = -1.0f;
    TestTrue(TEXT("huge feather accepted"), BackingLayer(smallRect, 2.0f, 100.0f, 0.45f, count - 1, count, inner,
        innerCorner, innerAlpha));
    const float f = 5.0f;
    const float inset = f * static_cast<float>(count - 1) / static_cast<float>(count);
    TestEqual(TEXT("huge feather: inset clamped (min x)"), inner.Min.X, smallRect.Min.X + inset, tol);
    TestEqual(TEXT("huge feather: inset clamped (max y)"), inner.Max.Y, smallRect.Max.Y - inset, tol);
    TestTrue(TEXT("huge feather: innermost width positive"), inner.Max.X - inner.Min.X > 0.0f);
    TestTrue(TEXT("huge feather: innermost height positive"), inner.Max.Y - inner.Min.Y > 0.0f);
    TestTrue(TEXT("huge feather: corner within half the smaller side"),
        innerCorner <= 0.5f * FMath::Min(inner.Max.X - inner.Min.X, inner.Max.Y - inner.Min.Y) + tol);
    TestTrue(TEXT("huge feather: corner not negative"), innerCorner >= 0.0f);

    // Every layer of the small rect keeps its corner within half its smaller side
    for (int32 index = 0; index < count; ++index)
    {
        FBox2f layer(ForceInit);
        float layerCorner = -1.0f;
        float layerAlpha = -1.0f;
        BackingLayer(smallRect, 50.0f, 100.0f, 0.45f, index, count, layer, layerCorner, layerAlpha);
        TestTrue(FString::Printf(TEXT("small rect layer %d: corner within half the smaller side"), index),
            layerCorner <= 0.5f * FMath::Min(layer.Max.X - layer.Min.X, layer.Max.Y - layer.Min.Y) + tol);
    }

    // Rejected input leaves sentinel outputs untouched
    const FBox2f rect(FVector2f(0.0f, 0.0f), FVector2f(100.0f, 50.0f));
    const FBox2f sentinelRect(FVector2f(-7.0f, -7.0f), FVector2f(-3.0f, -3.0f));
    struct FRejectCase
    {
        const TCHAR* Label;
        FBox2f Rect;
        int32 Index;
        int32 Count;
    };
    const FRejectCase cases[] = {
        { TEXT("invalid rect"), FBox2f(ForceInit), 0, 6 },
        { TEXT("count 0"), rect, 0, 0 },
        { TEXT("negative count"), rect, 0, -2 },
        { TEXT("negative index"), rect, -1, 6 },
        { TEXT("index == count"), rect, 6, 6 },
        { TEXT("index > count"), rect, 9, 6 },
    };
    for (const FRejectCase& reject : cases)
    {
        FBox2f outRect = sentinelRect;
        float outCorner = -11.0f;
        float outAlpha = -13.0f;
        TestFalse(FString::Printf(TEXT("%s: rejected"), reject.Label), BackingLayer(reject.Rect, 6.0f, 8.0f, 0.45f,
            reject.Index, reject.Count, outRect, outCorner, outAlpha));
        TestTrue(FString::Printf(TEXT("%s: rect untouched"), reject.Label), outRect.Min == sentinelRect.Min
            && outRect.Max == sentinelRect.Max && outRect.bIsValid == sentinelRect.bIsValid);
        TestEqual(FString::Printf(TEXT("%s: corner untouched"), reject.Label), outCorner, -11.0f);
        TestEqual(FString::Printf(TEXT("%s: alpha untouched"), reject.Label), outAlpha, -13.0f);
    }
    return true;
}

// --- FSOLHudTextList ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsTextListAddTest, "SOLTest.HudPanels.TextListAdd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Add stores every field of every line, in the order the lines were added
bool FSOLHudPanelsTextListAddTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudTextList list;
    list.Initialize(SOL::Hologram::HUD_MAX_TEXTS);
    TestEqual(TEXT("capacity"), list.GetMaxItems(), SOL::Hologram::HUD_MAX_TEXTS);
    TestEqual(TEXT("empty after Initialize"), list.Num(), 0);
    TestEqual(TEXT("nothing dropped after Initialize"), list.GetDroppedCount(), 0);

    const FText texts[] = { FText::FromString(TEXT("SPEED 120 m/s")), FText::FromString(TEXT("TARGET")),
        FText::FromString(TEXT("")) };
    const FVector2f positions[] = { FVector2f(12.0f, 34.0f), FVector2f(-5.5f, 800.25f), FVector2f(0.0f, 0.0f) };
    const float scales[] = { 1.0f, 0.75f, 2.5f };
    const FLinearColor colors[] = { FLinearColor(0.2f, 0.6f, 1.0f, 1.0f), FLinearColor(1.0f, 0.3f, 0.1f, 0.5f),
        FLinearColor::Black };
    const ESOLHudFont fonts[] = { ESOLHudFont::Small, ESOLHudFont::Large, ESOLHudFont::Medium };
    constexpr int32 count = UE_ARRAY_COUNT(texts);

    for (int32 i = 0; i < count; ++i)
    {
        TestTrue(FString::Printf(TEXT("line %d added"), i), list.Add(texts[i], positions[i], scales[i], colors[i],
            fonts[i]));
    }
    TestEqual(TEXT("count"), list.Num(), count);
    TestEqual(TEXT("nothing dropped"), list.GetDroppedCount(), 0);

    const TConstArrayView<FSOLHudTextItem> items = list.GetItems();
    if (!TestEqual(TEXT("items view size"), items.Num(), count))
    {
        return false;
    }
    for (int32 i = 0; i < count; ++i)
    {
        const FSOLHudTextItem& item = items[i];
        const FString label = FString::Printf(TEXT("line %d"), i);
        TestEqual(label + TEXT(": text string"), item.Text.ToString(), texts[i].ToString());
        TestTrue(label + TEXT(": text identical"), item.Text.IdenticalTo(texts[i]));
        TestEqual(label + TEXT(": position x"), item.Position.X, positions[i].X);
        TestEqual(label + TEXT(": position y"), item.Position.Y, positions[i].Y);
        TestEqual(label + TEXT(": scale"), item.Scale, scales[i]);
        TestTrue(label + TEXT(": colour"), item.Color == colors[i]);
        TestTrue(label + TEXT(": font"), item.Font == fonts[i]);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsTextListCapacityTest, "SOLTest.HudPanels.TextListCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A full list drops and counts further lines without reallocating; Reset empties it, clears drops and keeps capacity
bool FSOLHudPanelsTextListCapacityTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 capacity = HUDPANELS_SMALL_CAPACITY;
    FSOLHudTextList list;
    list.Initialize(capacity);
    TestEqual(TEXT("capacity"), list.GetMaxItems(), capacity);

    // Fill: the storage pointer stays put from the first Add on
    TestTrue(TEXT("line 0 fits"), HudPanelsAddLine(list, 0));
    const FSOLHudTextItem* storage = list.GetItems().GetData();
    TestNotNull(TEXT("storage allocated"), storage);
    for (int32 i = 1; i < capacity; ++i)
    {
        TestTrue(FString::Printf(TEXT("line %d fits"), i), HudPanelsAddLine(list, i));
        TestTrue(FString::Printf(TEXT("no reallocation at line %d"), i), list.GetItems().GetData() == storage);
    }
    TestEqual(TEXT("full"), list.Num(), capacity);
    TestEqual(TEXT("nothing dropped while filling"), list.GetDroppedCount(), 0);

    // Past capacity: dropped, counted, nothing added
    TestFalse(TEXT("extra line 1 dropped"), HudPanelsAddLine(list, capacity));
    TestFalse(TEXT("extra line 2 dropped"), HudPanelsAddLine(list, capacity + 1));
    TestEqual(TEXT("two dropped"), list.GetDroppedCount(), 2);
    TestEqual(TEXT("count stays at capacity"), list.Num(), capacity);
    TestTrue(TEXT("no reallocation past capacity"), list.GetItems().GetData() == storage);
    if (list.Num() == capacity)
    {
        TestEqual(TEXT("last kept line is the last that fit"), list.GetItems()[capacity - 1].Text.ToString(),
            HudPanelsText(capacity - 1).ToString());
    }

    // Reset: empty, drops cleared, capacity kept, same storage on refill
    list.Reset();
    TestEqual(TEXT("reset: empty"), list.Num(), 0);
    TestEqual(TEXT("reset: items view empty"), list.GetItems().Num(), 0);
    TestEqual(TEXT("reset: dropped cleared"), list.GetDroppedCount(), 0);
    TestEqual(TEXT("reset: capacity kept"), list.GetMaxItems(), capacity);
    for (int32 i = 0; i < capacity; ++i)
    {
        TestTrue(FString::Printf(TEXT("refill line %d fits"), i), HudPanelsAddLine(list, 10 + i));
        TestTrue(FString::Printf(TEXT("refill line %d: same storage"), i), list.GetItems().GetData() == storage);
    }
    if (TestTrue(TEXT("refill: not empty"), list.Num() > 0))
    {
        TestEqual(TEXT("refill: first line is the new one"), list.GetItems()[0].Text.ToString(),
            HudPanelsText(10).ToString());
    }
    TestFalse(TEXT("refill: over capacity dropped"), HudPanelsAddLine(list, 99));
    TestEqual(TEXT("refill: one dropped"), list.GetDroppedCount(), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsTextListFullCapacityTest, "SOLTest.HudPanels.TextListFullCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At the HUD's real capacity every line up to HUD_MAX_TEXTS fits in one allocation and the next is dropped
bool FSOLHudPanelsTextListFullCapacityTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 capacity = SOL::Hologram::HUD_MAX_TEXTS;
    FSOLHudTextList list;
    list.Initialize(capacity);

    bool bAllFit = true;
    bool bStable = true;
    const FSOLHudTextItem* storage = nullptr;
    for (int32 i = 0; i < capacity; ++i)
    {
        bAllFit &= HudPanelsAddLine(list, i);
        if (i == 0)
        {
            storage = list.GetItems().GetData();
        }
        bStable &= list.GetItems().GetData() == storage;
    }
    TestTrue(TEXT("every line up to capacity fits"), bAllFit);
    TestTrue(TEXT("storage never reallocated"), bStable);
    TestEqual(TEXT("full"), list.Num(), capacity);
    TestFalse(TEXT("one more is dropped"), HudPanelsAddLine(list, capacity));
    TestEqual(TEXT("one dropped"), list.GetDroppedCount(), 1);

    // Order is kept across the whole list
    bool bOrdered = true;
    const TConstArrayView<FSOLHudTextItem> items = list.GetItems();
    for (int32 i = 0; i < items.Num(); ++i)
    {
        bOrdered &= items[i].Text.ToString() == HudPanelsText(i).ToString();
    }
    TestTrue(TEXT("lines in the order added"), bOrdered);

    // Re-initializing empties the list
    list.Initialize(capacity);
    TestEqual(TEXT("re-initialize: empty"), list.Num(), 0);
    TestEqual(TEXT("re-initialize: dropped cleared"), list.GetDroppedCount(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsTextListZeroCapacityTest, "SOLTest.HudPanels.TextListZeroCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or negative capacity holds nothing: every Add is dropped and counted
bool FSOLHudPanelsTextListZeroCapacityTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudTextList zero;
    zero.Initialize(0);
    TestEqual(TEXT("zero: capacity 0"), zero.GetMaxItems(), 0);
    TestFalse(TEXT("zero: first Add dropped"), HudPanelsAddLine(zero, 0));
    TestFalse(TEXT("zero: second Add dropped"), HudPanelsAddLine(zero, 1));
    TestEqual(TEXT("zero: empty"), zero.Num(), 0);
    TestEqual(TEXT("zero: two dropped"), zero.GetDroppedCount(), 2);

    FSOLHudTextList negative;
    negative.Initialize(-5);
    TestEqual(TEXT("negative: capacity treated as 0"), negative.GetMaxItems(), 0);
    TestFalse(TEXT("negative: Add dropped"), HudPanelsAddLine(negative, 0));
    TestEqual(TEXT("negative: empty"), negative.Num(), 0);
    TestEqual(TEXT("negative: one dropped"), negative.GetDroppedCount(), 1);

    // An uninitialized list behaves as capacity 0
    FSOLHudTextList uninitialized;
    TestFalse(TEXT("uninitialized: Add dropped"), HudPanelsAddLine(uninitialized, 0));
    TestEqual(TEXT("uninitialized: empty"), uninitialized.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudPanelsBoxCapsuleTest, "SOLTest.HudPanels.BoxCapsule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A box's capsule is thinner than the box by the core reach, never wider than the box is long, and spans the box
bool FSOLHudPanelsBoxCapsuleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLHudShapeMath;
    constexpr float core = 0.5f;
    FVector2f start;
    FVector2f end;
    float halfWidth = 0.0f;

    // A long bar 100 x 6 at (10, 20): half width 3 - 0.5, ends inset by it, on the box's mid row
    TestTrue(TEXT("long bar accepted"), BoxCapsule(10.0f, 20.0f, 100.0f, 6.0f, core, start, end, halfWidth));
    HudPanelsTestNear(*this, TEXT("long bar half width"), halfWidth, 2.5);
    HudPanelsTestNear(*this, TEXT("long bar start x"), start.X, 12.5);
    HudPanelsTestNear(*this, TEXT("long bar end x"), end.X, 107.5);
    HudPanelsTestNear(*this, TEXT("long bar start y"), start.Y, 23.0);
    HudPanelsTestNear(*this, TEXT("long bar end y"), end.Y, 23.0);

    // A fill shorter than the bar is tall (0.2 px of a 6 px bar): a dot no wider than the fill, inside the box
    TestTrue(TEXT("short fill accepted"), BoxCapsule(10.0f, 20.0f, 0.2f, 6.0f, core, start, end, halfWidth));
    HudPanelsTestNear(*this, TEXT("short fill half width is half the fill"), halfWidth, 0.1);
    HudPanelsTestNear(*this, TEXT("short fill left edge at the box start"), start.X - halfWidth, 10.0);
    HudPanelsTestNear(*this, TEXT("short fill right edge at the box end"), end.X + halfWidth, 10.2);

    // A box thinner than twice the core keeps the core half width (when it is long enough)
    TestTrue(TEXT("thin box accepted"), BoxCapsule(0.0f, 0.0f, 50.0f, 0.6f, core, start, end, halfWidth));
    HudPanelsTestNear(*this, TEXT("thin box half width is the core"), halfWidth, 0.5);

    // Every width: the capsule's extent equals the box width and never starts left of the box
    for (int32 i = 1; i <= HUDPANELS_SWEEP_STEPS; ++i)
    {
        const float width = 0.05f * static_cast<float>(i);
        BoxCapsule(5.0f, 0.0f, width, 6.0f, core, start, end, halfWidth);
        HudPanelsTestNear(*this, FString::Printf(TEXT("extent at width %.2f"), width),
            end.X + halfWidth - (start.X - halfWidth), width);
        TestTrue(FString::Printf(TEXT("start inside the box at width %.2f"), width),
            start.X - halfWidth >= 5.0f - HUDPANELS_POS_TOLERANCE);
    }

    // A non-positive width or height draws nothing and leaves the outputs untouched
    halfWidth = -1.0f;
    TestFalse(TEXT("zero width rejected"), BoxCapsule(0.0f, 0.0f, 0.0f, 6.0f, core, start, end, halfWidth));
    TestFalse(TEXT("negative width rejected"), BoxCapsule(0.0f, 0.0f, -3.0f, 6.0f, core, start, end, halfWidth));
    TestFalse(TEXT("zero height rejected"), BoxCapsule(0.0f, 0.0f, 10.0f, 0.0f, core, start, end, halfWidth));
    HudPanelsTestNear(*this, TEXT("rejected leaves the half width"), halfWidth, -1.0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
