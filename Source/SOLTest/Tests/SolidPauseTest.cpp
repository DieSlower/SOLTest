/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuState.h"
#include "Menu/SOLSolidMenuMath.h"
#include "SOLConstants.h"
#include "UI/SOLHoloMenuMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #46 Part 46g pause side panel and page closing (Docs/SDDs/46-solid-menus.md sections 4.6 and
// Q4), written against the Menu/SOLSolidMenuMath.h and Menu/SOLMenuState.h contracts before the production code
// existed. (46i) The panel's looks are fixed numbers worked out by hand from the SDD's rules (progress linear over
// PAUSE_SLIDE_S / CLOSE_S, the slide -(width + PAUSE_SLIDE_MARGIN_PX) x (1 - p)^3, the dim PAUSE_DIM_ALPHA x
// smoothstep(FILL_PHASE_START, 1, p)) instead of a copy of the composition; the page rules check that Esc / Back on a
// page closes the page and that the existing Esc-resumes contract of Apply is unchanged
namespace
{
    constexpr float SOLIDPAUSETEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float SOLIDPAUSETEST_INF = std::numeric_limits<float>::infinity();
    constexpr double SOLIDPAUSETEST_NAN_D = std::numeric_limits<double>::quiet_NaN();
    constexpr float SOLIDPAUSETEST_TOLERANCE = 1.0e-4f;
    constexpr float SOLIDPAUSETEST_REVERSAL_TOLERANCE = 1.0e-5f;   // A reopen never pops
    constexpr double SOLIDPAUSETEST_T0_S = 10.0;                    // Exact in double with float durations added
    constexpr float SOLIDPAUSETEST_WIDTH = 420.0f;                  // A side panel width (test input)
    constexpr float SOLIDPAUSETEST_SLIDE_PX = 436.0f;               // ... slid fully out: 420 + the 16 margin
    constexpr float SOLIDPAUSETEST_FULL_DIM = 0.55f;                // PAUSE_DIM_ALPHA (SDD 46 section 9)
    constexpr uint8 SOLIDPAUSETEST_BAD_TAB = 99;                    // A tab value well past Quit
    constexpr double SOLIDPAUSETEST_OPEN_S = SOL::SolidMenu::PAUSE_SLIDE_S;   // The side panel's slide-in (SDD 7)
    constexpr double SOLIDPAUSETEST_CLOSE_S = SOL::SolidMenu::CLOSE_S;        // ... and slide-out

    // The hand-worked numbers below assume the SDD's values of the constants they depend on
    static_assert(SOL::SolidMenu::PAUSE_SLIDE_MARGIN_PX == 16.0f, "SOLIDPAUSETEST_SLIDE_PX is 420 + 16");
    static_assert(SOL::SolidMenu::PAUSE_DIM_ALPHA == 0.55f, "SOLIDPAUSETEST_FULL_DIM and the dims are at 0.55");
    static_assert(SOL::HoloMenu::FILL_PHASE_START == 0.4f, "The dims are smoothstep(0.4, 1, p) x 0.55");

    constexpr ESOLHoloMenuPhase SOLIDPAUSETEST_PHASES[] = {
        ESOLHoloMenuPhase::Closed, ESOLHoloMenuPhase::Opening, ESOLHoloMenuPhase::Open, ESOLHoloMenuPhase::Closing };

    // One frame of a slide: the time as a fraction of its duration and the look worked out by hand at width 420
    struct FSolidPauseTestFrame
    {
        double Fraction;
        float Progress;
        float SlideX;       // -436 x (1 - p)^3
        float Dim;          // 0.55 x smoothstep(0.4, 1, p): 0 up to p 0.4, 0.0407407 at 0.5, 0.3431134 at 0.75
    };

    // An open from Closed, over PAUSE_SLIDE_S
    const FSolidPauseTestFrame SOLIDPAUSETEST_OPENING[] = {
        { 0.0, 0.0f, -436.0f, 0.0f },
        { 0.25, 0.25f, -183.9375f, 0.0f },
        { 0.5, 0.5f, -54.5f, 0.0407407f },
        { 0.75, 0.75f, -6.8125f, 0.3431134f },
        { 1.0, 1.0f, 0.0f, 0.55f },
        { 2.0, 1.0f, 0.0f, 0.55f },
    };

    // A close from Open, over CLOSE_S
    const FSolidPauseTestFrame SOLIDPAUSETEST_CLOSING[] = {
        { 0.0, 1.0f, 0.0f, 0.55f },
        { 0.25, 0.75f, -6.8125f, 0.3431134f },
        { 0.5, 0.5f, -54.5f, 0.0407407f },
        { 0.75, 0.25f, -183.9375f, 0.0f },
        { 1.0, 0.0f, -436.0f, 0.0f },
        { 2.0, 0.0f, -436.0f, 0.0f },
    };

    // Every pause-menu tab, with whether it opens a page (SDD 46 Q4, independent of production)
    struct FSolidPauseTestTab
    {
        SOLMenuState::EMenuTab Tab;
        const TCHAR* Name;
        bool bPage;
    };
    const FSolidPauseTestTab SOLIDPAUSETEST_TABS[] = {
        { SOLMenuState::EMenuTab::Resume, TEXT("Resume"), false },
        { SOLMenuState::EMenuTab::Controls, TEXT("Controls"), true },
        { SOLMenuState::EMenuTab::Info, TEXT("Info"), true },
        { SOLMenuState::EMenuTab::Settings, TEXT("Settings"), true },
        { SOLMenuState::EMenuTab::Demo, TEXT("Demo"), true },
        { SOLMenuState::EMenuTab::About, TEXT("About"), true },
        { SOLMenuState::EMenuTab::Quit, TEXT("Quit"), false },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a phase's name for messages
    const TCHAR* SolidPauseTestPhaseName(const ESOLHoloMenuPhase phase)
    {
        switch (phase)
        {
        case ESOLHoloMenuPhase::Closed:
            return TEXT("Closed");
        case ESOLHoloMenuPhase::Opening:
            return TEXT("Opening");
        case ESOLHoloMenuPhase::Open:
            return TEXT("Open");
        case ESOLHoloMenuPhase::Closing:
            return TEXT("Closing");
        default:
            return TEXT("?");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a panel anim state in a phase that started at startS with startProgress
    FSOLHoloMenuAnim SolidPauseTestAnim(const ESOLHoloMenuPhase phase, const double startS, const float startProgress)
    {
        FSOLHoloMenuAnim anim;
        anim.Phase = phase;
        anim.PhaseStartS = startS;
        anim.PhaseStartProgress = startProgress;
        return anim;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an Opening anim begun from Closed at T0 through the public API
    FSOLHoloMenuAnim SolidPauseTestOpening(const float s)
    {
        FSOLHoloMenuAnim anim = SolidPauseTestAnim(ESOLHoloMenuPhase::Closed, 0.0, 0.0f);
        SOLSolidMenu::BeginPanelOpen(anim, SOLIDPAUSETEST_T0_S, s, SOLIDPAUSETEST_OPEN_S, SOLIDPAUSETEST_CLOSE_S);
        return anim;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a Closing anim begun from Open at T0 through the public API
    FSOLHoloMenuAnim SolidPauseTestClosing(const float s)
    {
        FSOLHoloMenuAnim anim = SolidPauseTestAnim(ESOLHoloMenuPhase::Open, 0.0, 1.0f);
        SOLSolidMenu::BeginPanelClose(anim, SOLIDPAUSETEST_T0_S, s, SOLIDPAUSETEST_OPEN_S, SOLIDPAUSETEST_CLOSE_S);
        return anim;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a look against written-out amounts (within the tolerance) and flags (exactly)
    void SolidPauseTestCheck(FAutomationTestBase& test, const FString& label, const FSOLPausePanelLook& look,
        const float progress, const float slideX, const float dim, const bool bShown, const bool bInteractive)
    {
        test.TestEqual(label + TEXT(" Progress"), look.Progress, progress, SOLIDPAUSETEST_TOLERANCE);
        test.TestEqual(label + TEXT(" SlideX"), look.SlideX, slideX, SOLIDPAUSETEST_TOLERANCE);
        test.TestEqual(label + TEXT(" DimAlpha"), look.DimAlpha, dim, SOLIDPAUSETEST_TOLERANCE);
        test.TestTrue(label + (bShown ? TEXT(" shown") : TEXT(" not shown")), look.bShown == bShown);
        test.TestTrue(label + (bInteractive ? TEXT(" interactive") : TEXT(" not interactive")),
            look.bInteractive == bInteractive);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks every frame of a slide table at T0 + fraction x duration, at strength s
    void SolidPauseTestFrames(FAutomationTestBase& test, const FString& label, const FSOLHoloMenuAnim& anim,
        TConstArrayView<FSolidPauseTestFrame> frames, const double durationS, const float s, const bool bInteractive)
    {
        for (const FSolidPauseTestFrame& frame : frames)
        {
            const double t = SOLIDPAUSETEST_T0_S + frame.Fraction * durationS;
            SolidPauseTestCheck(test, FString::Printf(TEXT("%s at %.2f of its duration"), *label, frame.Fraction),
                SOLSolidMenu::EvaluatePausePanel(anim, t, s, SOLIDPAUSETEST_WIDTH), frame.Progress, frame.SlideX,
                frame.Dim, true, bInteractive);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks two looks match: the amounts within the tolerance, the flags exactly
    void SolidPauseTestLookEqual(FAutomationTestBase& test, const FString& label, const FSOLPausePanelLook& actual,
        const FSOLPausePanelLook& expected)
    {
        SolidPauseTestCheck(test, label, actual, expected.Progress, expected.SlideX, expected.DimAlpha,
            expected.bShown, expected.bInteractive);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a paused state reached through the public API (Play, then Esc)
    SOLMenuState::FMenuState SolidPauseTestPaused()
    {
        using namespace SOLMenuState;
        return Apply(Apply(Initial(), EMenuEvent::PlayPressed), EMenuEvent::EscapePressed);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a state with the given fields (for modes and tabs the public API does not reach)
    SOLMenuState::FMenuState SolidPauseTestState(const SOLMenuState::EMenuMode mode, const SOLMenuState::EMenuTab tab)
    {
        SOLMenuState::FMenuState state;
        state.Mode = mode;
        state.Tab = tab;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether every field of two menu states matches
    bool SolidPauseTestSameState(const SOLMenuState::FMenuState& a, const SOLMenuState::FMenuState& b)
    {
        return a.Mode == b.Mode && a.Tab == b.Tab && a.bMapOpen == b.bMapOpen;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an out-of-range tab value (a corrupt index cast to the enum)
    SOLMenuState::EMenuTab SolidPauseTestBadTab()
    {
        return static_cast<SOLMenuState::EMenuTab>(SOLIDPAUSETEST_BAD_TAB);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Pause side panel look (SDD 46 section 4.6)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseClosedTest, "SOLTest.SolidPause.Panel.Closed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Closed is all off at any time and strength: no dim, not shown, no input, slid fully out
bool FSOLSolidPauseClosedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloMenuAnim closed = SolidPauseTestAnim(ESOLHoloMenuPhase::Closed, SOLIDPAUSETEST_T0_S, 0.0f);
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    const double times[] = { 0.0, SOLIDPAUSETEST_T0_S, SOLIDPAUSETEST_T0_S + 5.0, SOLIDPAUSETEST_NAN_D };
    for (const float s : strengths)
    {
        for (const double t : times)
        {
            SolidPauseTestCheck(*this, FString::Printf(TEXT("Closed s %f t %f"), s, t),
                SOLSolidMenu::EvaluatePausePanel(closed, t, s, SOLIDPAUSETEST_WIDTH), 0.0f, -SOLIDPAUSETEST_SLIDE_PX,
                0.0f, false, false);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseOpeningTest, "SOLTest.SolidPause.Panel.Opening",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Through an open at s = 1: shown and interactive throughout, the slide and dim at the hand-worked numbers
bool FSOLSolidPauseOpeningTest::RunTest(const FString& /*parameters*/)
{
    SolidPauseTestFrames(*this, TEXT("Opening"), SolidPauseTestOpening(1.0f), SOLIDPAUSETEST_OPENING,
        SOLIDPAUSETEST_OPEN_S, 1.0f, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseSlideTimingTest, "SOLTest.SolidPause.Panel.SlideTiming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// (46i, SDD 6.6 and 7) The pause slide-in reaches 1 exactly at PAUSE_SLIDE_S and not before, and the slide-out reaches 0
// exactly at CLOSE_S and not before; AdvancePanel finishes each only then
bool FSOLSolidPauseSlideTimingTest::RunTest(const FString& /*parameters*/)
{
    const float tol = SOLIDPAUSETEST_TOLERANCE;
    const double t0 = SOLIDPAUSETEST_T0_S;

    // The slide-in: 0.99 at 99% of PAUSE_SLIDE_S (the slide 436 x 0.01^3 short), exactly 1 at PAUSE_SLIDE_S
    FSOLHoloMenuAnim anim = SolidPauseTestOpening(1.0f);
    const double almostOpen = t0 + 0.99 * SOLIDPAUSETEST_OPEN_S;
    const FSOLPausePanelLook early = SOLSolidMenu::EvaluatePausePanel(anim, almostOpen, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestEqual(TEXT("0.99 at 99% of PAUSE_SLIDE_S"), early.Progress, 0.99f, tol);
    TestTrue(TEXT("not yet 1 at 99% of PAUSE_SLIDE_S"), early.Progress < 1.0f);
    TestEqual(TEXT("still 0.000436 out"), early.SlideX, -0.000436f, 1.0e-6f);
    SOLSolidMenu::AdvancePanel(anim, almostOpen, 1.0f, SOLIDPAUSETEST_OPEN_S, SOLIDPAUSETEST_CLOSE_S);
    TestTrue(TEXT("still Opening at 99% of PAUSE_SLIDE_S"), anim.Phase == ESOLHoloMenuPhase::Opening);
    const double openEnd = t0 + SOLIDPAUSETEST_OPEN_S;
    const FSOLPausePanelLook atEnd = SOLSolidMenu::EvaluatePausePanel(anim, openEnd, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestTrue(TEXT("exactly 1 at PAUSE_SLIDE_S"), atEnd.Progress == 1.0f);
    TestTrue(TEXT("exactly in place at PAUSE_SLIDE_S"), atEnd.SlideX == 0.0f);
    SOLSolidMenu::AdvancePanel(anim, openEnd, 1.0f, SOLIDPAUSETEST_OPEN_S, SOLIDPAUSETEST_CLOSE_S);
    TestTrue(TEXT("Open at PAUSE_SLIDE_S"), anim.Phase == ESOLHoloMenuPhase::Open);

    // The slide-out: 0.01 at 99% of CLOSE_S, exactly 0 at CLOSE_S, then Closed and hidden
    const double t1 = t0 + 1.0;
    TestTrue(TEXT("the close starts"), SOLSolidMenu::BeginPanelClose(anim, t1, 1.0f, SOLIDPAUSETEST_OPEN_S,
        SOLIDPAUSETEST_CLOSE_S));
    const double almostClosed = t1 + 0.99 * SOLIDPAUSETEST_CLOSE_S;
    const FSOLPausePanelLook late = SOLSolidMenu::EvaluatePausePanel(anim, almostClosed, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestEqual(TEXT("0.01 at 99% of CLOSE_S"), late.Progress, 0.01f, tol);
    TestTrue(TEXT("not yet 0 at 99% of CLOSE_S"), late.Progress > 0.0f);
    TestTrue(TEXT("still shown at 99% of CLOSE_S"), late.bShown);
    SOLSolidMenu::AdvancePanel(anim, almostClosed, 1.0f, SOLIDPAUSETEST_OPEN_S, SOLIDPAUSETEST_CLOSE_S);
    TestTrue(TEXT("still Closing at 99% of CLOSE_S"), anim.Phase == ESOLHoloMenuPhase::Closing);
    const double closeEnd = t1 + SOLIDPAUSETEST_CLOSE_S;
    TestTrue(TEXT("exactly 0 at CLOSE_S"),
        SOLSolidMenu::EvaluatePausePanel(anim, closeEnd, 1.0f, SOLIDPAUSETEST_WIDTH).Progress == 0.0f);
    SOLSolidMenu::AdvancePanel(anim, closeEnd, 1.0f, SOLIDPAUSETEST_OPEN_S, SOLIDPAUSETEST_CLOSE_S);
    TestTrue(TEXT("Closed at CLOSE_S"), anim.Phase == ESOLHoloMenuPhase::Closed);
    SolidPauseTestCheck(*this, TEXT("after the close"), SOLSolidMenu::EvaluatePausePanel(anim, closeEnd, 1.0f,
        SOLIDPAUSETEST_WIDTH), 0.0f, -SOLIDPAUSETEST_SLIDE_PX, 0.0f, false, false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseOpenTest, "SOLTest.SolidPause.Panel.Open",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Open: progress 1, in place (SlideX 0), the full dim, shown and interactive
bool FSOLSolidPauseOpenTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloMenuAnim open = SolidPauseTestAnim(ESOLHoloMenuPhase::Open, SOLIDPAUSETEST_T0_S, 1.0f);
    SolidPauseTestCheck(*this, TEXT("Open"), SOLSolidMenu::EvaluatePausePanel(open, SOLIDPAUSETEST_T0_S + 3.0, 1.0f,
        SOLIDPAUSETEST_WIDTH), 1.0f, 0.0f, SOLIDPAUSETEST_FULL_DIM, true, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseClosingTest, "SOLTest.SolidPause.Panel.Closing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Through a close at s = 1: still shown and dimmed until the end, but it takes no input (the input is already back)
bool FSOLSolidPauseClosingTest::RunTest(const FString& /*parameters*/)
{
    SolidPauseTestFrames(*this, TEXT("Closing"), SolidPauseTestClosing(1.0f), SOLIDPAUSETEST_CLOSING,
        SOLIDPAUSETEST_CLOSE_S, 1.0f, false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseZeroStrengthTest, "SOLTest.SolidPause.Panel.ZeroStrength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// At s <= 0 an open is instant (progress 1, full dim, in place) and so is a close (progress 0, no dim)
bool FSOLSolidPauseZeroStrengthTest::RunTest(const FString& /*parameters*/)
{
    const float zeros[] = { 0.0f, -1.0f, SOLIDPAUSETEST_NAN };
    for (const float s : zeros)
    {
        const FString tag = FString::Printf(TEXT("s %f"), s);

        // Opening, just begun
        const FSOLHoloMenuAnim opening = SolidPauseTestAnim(ESOLHoloMenuPhase::Opening, SOLIDPAUSETEST_T0_S, 0.0f);
        SolidPauseTestCheck(*this, tag + TEXT(" Opening"), SOLSolidMenu::EvaluatePausePanel(opening,
            SOLIDPAUSETEST_T0_S, s, SOLIDPAUSETEST_WIDTH), 1.0f, 0.0f, SOLIDPAUSETEST_FULL_DIM, true, true);

        // Closing, just begun: slid fully out, no dim, still shown until the caller finishes it
        const FSOLHoloMenuAnim closing = SolidPauseTestAnim(ESOLHoloMenuPhase::Closing, SOLIDPAUSETEST_T0_S, 1.0f);
        SolidPauseTestCheck(*this, tag + TEXT(" Closing"), SOLSolidMenu::EvaluatePausePanel(closing,
            SOLIDPAUSETEST_T0_S, s, SOLIDPAUSETEST_WIDTH), 0.0f, -SOLIDPAUSETEST_SLIDE_PX, 0.0f, true, false);

        // Open: the dim is not an effect, so it stays full
        const FSOLHoloMenuAnim openAnim = SolidPauseTestAnim(ESOLHoloMenuPhase::Open, SOLIDPAUSETEST_T0_S, 1.0f);
        SolidPauseTestCheck(*this, tag + TEXT(" Open"), SOLSolidMenu::EvaluatePausePanel(openAnim,
            SOLIDPAUSETEST_T0_S, s, SOLIDPAUSETEST_WIDTH), 1.0f, 0.0f, SOLIDPAUSETEST_FULL_DIM, true, true);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseStrengthTwoTest, "SOLTest.SolidPause.Panel.StrengthTwo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// At s = 2 the slides keep their durations and the dim its values (neither is an effect that scales); a strength past 2
// reads as 2
bool FSOLSolidPauseStrengthTwoTest::RunTest(const FString& /*parameters*/)
{
    SolidPauseTestFrames(*this, TEXT("s 2 Opening"), SolidPauseTestOpening(2.0f), SOLIDPAUSETEST_OPENING,
        SOLIDPAUSETEST_OPEN_S, 2.0f, true);
    SolidPauseTestFrames(*this, TEXT("s 2 Closing"), SolidPauseTestClosing(2.0f), SOLIDPAUSETEST_CLOSING,
        SOLIDPAUSETEST_CLOSE_S, 2.0f, false);
    const FSOLHoloMenuAnim closed = SolidPauseTestAnim(ESOLHoloMenuPhase::Closed, SOLIDPAUSETEST_T0_S, 0.0f);
    const FSOLHoloMenuAnim open = SolidPauseTestAnim(ESOLHoloMenuPhase::Open, SOLIDPAUSETEST_T0_S, 1.0f);
    SolidPauseTestCheck(*this, TEXT("s 2 Closed"), SOLSolidMenu::EvaluatePausePanel(closed, SOLIDPAUSETEST_T0_S, 2.0f,
        SOLIDPAUSETEST_WIDTH), 0.0f, -SOLIDPAUSETEST_SLIDE_PX, 0.0f, false, false);
    SolidPauseTestCheck(*this, TEXT("s 2 Open"), SOLSolidMenu::EvaluatePausePanel(open, SOLIDPAUSETEST_T0_S, 2.0f,
        SOLIDPAUSETEST_WIDTH), 1.0f, 0.0f, SOLIDPAUSETEST_FULL_DIM, true, true);

    // Past 2 reads 2
    const FSOLHoloMenuAnim anims[] = { closed, SolidPauseTestOpening(2.0f), open, SolidPauseTestClosing(2.0f) };
    for (const FSOLHoloMenuAnim& anim : anims)
    {
        for (int32 step = 0; step <= 4; ++step)
        {
            const double t = SOLIDPAUSETEST_T0_S + 0.25 * step * SOLIDPAUSETEST_CLOSE_S;
            SolidPauseTestLookEqual(*this, FString::Printf(TEXT("%s step %d s 5 reads 2"),
                SolidPauseTestPhaseName(anim.Phase), step),
                SOLSolidMenu::EvaluatePausePanel(anim, t, 5.0f, SOLIDPAUSETEST_WIDTH),
                SOLSolidMenu::EvaluatePausePanel(anim, t, 2.0f, SOLIDPAUSETEST_WIDTH));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseNanTimeTest, "SOLTest.SolidPause.Panel.NanTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A NaN time settles the panel (Opening reads 1, Closing 0) and every amount stays finite
bool FSOLSolidPauseNanTimeTest::RunTest(const FString& /*parameters*/)
{
    SolidPauseTestCheck(*this, TEXT("Opening at NaN"), SOLSolidMenu::EvaluatePausePanel(SolidPauseTestOpening(1.0f),
        SOLIDPAUSETEST_NAN_D, 1.0f, SOLIDPAUSETEST_WIDTH), 1.0f, 0.0f, SOLIDPAUSETEST_FULL_DIM, true, true);
    SolidPauseTestCheck(*this, TEXT("Closing at NaN"), SOLSolidMenu::EvaluatePausePanel(SolidPauseTestClosing(1.0f),
        SOLIDPAUSETEST_NAN_D, 1.0f, SOLIDPAUSETEST_WIDTH), 0.0f, -SOLIDPAUSETEST_SLIDE_PX, 0.0f, true, false);

    // Each phase part way (start progress 0.5) at a NaN time: the phase's end look
    struct FNanCase
    {
        ESOLHoloMenuPhase Phase;
        float Progress;
        float SlideX;
        float Dim;
        bool bShown;
        bool bInteractive;
    };
    const FNanCase cases[] = {
        { ESOLHoloMenuPhase::Closed, 0.0f, -SOLIDPAUSETEST_SLIDE_PX, 0.0f, false, false },
        { ESOLHoloMenuPhase::Opening, 1.0f, 0.0f, SOLIDPAUSETEST_FULL_DIM, true, true },
        { ESOLHoloMenuPhase::Open, 1.0f, 0.0f, SOLIDPAUSETEST_FULL_DIM, true, true },
        { ESOLHoloMenuPhase::Closing, 0.0f, -SOLIDPAUSETEST_SLIDE_PX, 0.0f, true, false },
    };
    for (const FNanCase& entry : cases)
    {
        const FSOLHoloMenuAnim anim = SolidPauseTestAnim(entry.Phase, SOLIDPAUSETEST_T0_S, 0.5f);
        SolidPauseTestCheck(*this, FString::Printf(TEXT("%s NaN time"), SolidPauseTestPhaseName(entry.Phase)),
            SOLSolidMenu::EvaluatePausePanel(anim, SOLIDPAUSETEST_NAN_D, 1.0f, SOLIDPAUSETEST_WIDTH), entry.Progress,
            entry.SlideX, entry.Dim, entry.bShown, entry.bInteractive);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseSlideSignTest, "SOLTest.SolidPause.Panel.SlideNeverPositive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Over every phase, time, strength and width (bad widths included) SlideX is finite and never positive, Progress in
// [0, 1] and the dim in [0, PAUSE_DIM_ALPHA]; a negative, NaN or infinite width slides as width 0 (16 x (1 - p)^3)
bool FSOLSolidPauseSlideSignTest::RunTest(const FString& /*parameters*/)
{
    const float strengths[] = { 0.0f, 0.5f, 1.0f, 2.0f };
    const float widths[] = { 0.0f, 100.0f, SOLIDPAUSETEST_WIDTH, -50.0f, SOLIDPAUSETEST_NAN, SOLIDPAUSETEST_INF };
    for (const ESOLHoloMenuPhase phase : SOLIDPAUSETEST_PHASES)
    {
        const FSOLHoloMenuAnim anim = SolidPauseTestAnim(phase, SOLIDPAUSETEST_T0_S,
            phase == ESOLHoloMenuPhase::Closing ? 1.0f : 0.0f);
        for (const float s : strengths)
        {
            for (const float w : widths)
            {
                for (int32 step = -2; step <= 6; ++step)
                {
                    const double t = SOLIDPAUSETEST_T0_S + 0.1 * step;
                    const FSOLPausePanelLook look = SOLSolidMenu::EvaluatePausePanel(anim, t, s, w);
                    const FString label = FString::Printf(TEXT("%s s %f w %f t %f"),
                        SolidPauseTestPhaseName(phase), s, w, t);
                    TestTrue(label + TEXT(" SlideX finite and <= 0"), FMath::IsFinite(look.SlideX) &&
                        look.SlideX <= 0.0f);
                    TestTrue(label + TEXT(" Progress in [0, 1]"), look.Progress >= 0.0f && look.Progress <= 1.0f);
                    TestTrue(label + TEXT(" DimAlpha in [0, PAUSE_DIM_ALPHA]"), look.DimAlpha >= 0.0f &&
                        look.DimAlpha <= SOLIDPAUSETEST_FULL_DIM + SOLIDPAUSETEST_TOLERANCE);
                    if (!(w >= 0.0f) || !FMath::IsFinite(w))
                    {
                        TestEqual(label + TEXT(" a bad width slides as width 0"), look.SlideX,
                            SOLSolidMenu::EvaluatePausePanel(anim, t, s, 0.0f).SlideX, SOLIDPAUSETEST_TOLERANCE);
                    }
                }
            }
        }
    }

    // Width 0, fully out: the margin alone
    const FSOLHoloMenuAnim closed = SolidPauseTestAnim(ESOLHoloMenuPhase::Closed, SOLIDPAUSETEST_T0_S, 0.0f);
    TestEqual(TEXT("width 0 Closed slides the 16 margin"),
        SOLSolidMenu::EvaluatePausePanel(closed, SOLIDPAUSETEST_T0_S, 1.0f, 0.0f).SlideX, -16.0f,
        SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("width NaN Closed slides the 16 margin"),
        SOLSolidMenu::EvaluatePausePanel(closed, SOLIDPAUSETEST_T0_S, 1.0f, SOLIDPAUSETEST_NAN).SlideX, -16.0f,
        SOLIDPAUSETEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseReopenTest, "SOLTest.SolidPause.Panel.ReopenDuringClose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Re-pausing mid-close is interactive again at once and continues from the close's progress (no pop)
bool FSOLSolidPauseReopenTest::RunTest(const FString& /*parameters*/)
{
    FSOLHoloMenuAnim anim = SolidPauseTestClosing(1.0f);
    const double t1 = SOLIDPAUSETEST_T0_S + 0.5 * SOLIDPAUSETEST_CLOSE_S;
    const FSOLPausePanelLook before = SOLSolidMenu::EvaluatePausePanel(anim, t1, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestFalse(TEXT("mid-close is not interactive"), before.bInteractive);
    TestTrue(TEXT("mid-close progress strictly inside (0, 1)"), before.Progress > 0.0f && before.Progress < 1.0f);

    TestTrue(TEXT("BeginPanelOpen from Closing starts an open"), SOLSolidMenu::BeginPanelOpen(anim, t1, 1.0f,
        SOLIDPAUSETEST_OPEN_S, SOLIDPAUSETEST_CLOSE_S));
    const FSOLPausePanelLook after = SOLSolidMenu::EvaluatePausePanel(anim, t1, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestTrue(TEXT("reopened is interactive"), after.bInteractive);
    TestTrue(TEXT("reopened is shown"), after.bShown);
    TestEqual(TEXT("progress continues from the close"), after.Progress, before.Progress,
        SOLIDPAUSETEST_REVERSAL_TOLERANCE);
    TestEqual(TEXT("SlideX continues"), after.SlideX, before.SlideX, SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("dim continues"), after.DimAlpha, before.DimAlpha, SOLIDPAUSETEST_TOLERANCE);

    // It then opens onwards and settles Open
    const double t2 = t1 + 0.25 * SOLIDPAUSETEST_OPEN_S;
    TestTrue(TEXT("progress rises after the reopen"),
        SOLSolidMenu::EvaluatePausePanel(anim, t2, 1.0f, SOLIDPAUSETEST_WIDTH).Progress > before.Progress);
    const double settled = t1 + 2.0 * SOLIDPAUSETEST_OPEN_S;
    SOLSolidMenu::AdvancePanel(anim, settled, 1.0f, SOLIDPAUSETEST_OPEN_S, SOLIDPAUSETEST_CLOSE_S);
    const FSOLPausePanelLook open = SOLSolidMenu::EvaluatePausePanel(anim, settled, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestEqual(TEXT("settled Open progress 1"), open.Progress, 1.0f, SOLIDPAUSETEST_TOLERANCE);
    TestTrue(TEXT("settled Open interactive"), open.bInteractive);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Pages in the pause shell (SDD 46 Q4)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseIsPageTabTest, "SOLTest.SolidPause.Page.IsPageTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Controls, Info, Settings, Demo and About open a page; Resume, Quit and an out-of-range value do not
bool FSOLSolidPauseIsPageTabTest::RunTest(const FString& /*parameters*/)
{
    for (const FSolidPauseTestTab& entry : SOLIDPAUSETEST_TABS)
    {
        TestTrue(FString::Printf(TEXT("IsPageTab(%s) is %d"), entry.Name, entry.bPage ? 1 : 0),
            SOLMenuState::IsPageTab(entry.Tab) == entry.bPage);
    }
    TestFalse(TEXT("IsPageTab(99)"), SOLMenuState::IsPageTab(SolidPauseTestBadTab()));
    TestFalse(TEXT("IsPageTab(one past Quit)"), SOLMenuState::IsPageTab(static_cast<SOLMenuState::EMenuTab>(
        static_cast<uint8>(SOLMenuState::EMenuTab::Quit) + 1)));
    TestFalse(TEXT("IsPageTab(255)"), SOLMenuState::IsPageTab(static_cast<SOLMenuState::EMenuTab>(255)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseEscapeClosesPageTest, "SOLTest.SolidPause.Page.EscapeClosesPage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Esc closes the page only while paused on a page tab; never in the main menu, while playing or on a bad tab
bool FSOLSolidPauseEscapeClosesPageTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState paused = SolidPauseTestPaused();
    TestFalse(TEXT("paused on Resume (the pause menu's opening tab)"), EscapeClosesPage(paused));
    for (const FSolidPauseTestTab& entry : SOLIDPAUSETEST_TABS)
    {
        const FMenuState onTab = SelectTab(paused, entry.Tab);
        TestTrue(FString::Printf(TEXT("paused on %s: Esc closes the page is %d"), entry.Name, entry.bPage ? 1 : 0),
            EscapeClosesPage(onTab) == entry.bPage);
        TestFalse(FString::Printf(TEXT("main menu on %s (API)"), entry.Name),
            EscapeClosesPage(SelectTab(Initial(), entry.Tab)));
        TestFalse(FString::Printf(TEXT("main menu on %s"), entry.Name),
            EscapeClosesPage(SolidPauseTestState(EMenuMode::MainMenu, entry.Tab)));
        TestFalse(FString::Printf(TEXT("playing on %s"), entry.Name),
            EscapeClosesPage(SolidPauseTestState(EMenuMode::Playing, entry.Tab)));
    }
    TestFalse(TEXT("paused on tab 99"), EscapeClosesPage(SolidPauseTestState(EMenuMode::Paused,
        SolidPauseTestBadTab())));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseClosePageTest, "SOLTest.SolidPause.Page.ClosePage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Paused on a page: back to the Resume tab, still paused, the map flag kept; every other state unchanged
bool FSOLSolidPauseClosePageTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState paused = SolidPauseTestPaused();
    for (const FSolidPauseTestTab& entry : SOLIDPAUSETEST_TABS)
    {
        const FMenuState onTab = SelectTab(paused, entry.Tab);
        const FMenuState closed = ClosePage(onTab);
        if (entry.bPage)
        {
            TestTrue(FString::Printf(TEXT("%s: still paused"), entry.Name), closed.Mode == EMenuMode::Paused);
            TestTrue(FString::Printf(TEXT("%s: back to Resume"), entry.Name), closed.Tab == EMenuTab::Resume);
            TestTrue(FString::Printf(TEXT("%s: map flag kept"), entry.Name), closed.bMapOpen == onTab.bMapOpen);
        }
        else
        {
            TestTrue(FString::Printf(TEXT("%s: unchanged"), entry.Name), SolidPauseTestSameState(closed, onTab));
        }

        // Main menu and playing: unchanged whatever the tab
        const FMenuState mainMenu = SolidPauseTestState(EMenuMode::MainMenu, entry.Tab);
        TestTrue(FString::Printf(TEXT("main menu on %s unchanged"), entry.Name),
            SolidPauseTestSameState(ClosePage(mainMenu), mainMenu));
        const FMenuState mainMenuApi = SelectTab(Initial(), entry.Tab);
        TestTrue(FString::Printf(TEXT("main menu on %s (API) unchanged"), entry.Name),
            SolidPauseTestSameState(ClosePage(mainMenuApi), mainMenuApi));
        const FMenuState playing = SolidPauseTestState(EMenuMode::Playing, entry.Tab);
        TestTrue(FString::Printf(TEXT("playing on %s unchanged"), entry.Name),
            SolidPauseTestSameState(ClosePage(playing), playing));
    }
    const FMenuState bad = SolidPauseTestState(EMenuMode::Paused, SolidPauseTestBadTab());
    TestTrue(TEXT("paused on tab 99 unchanged"), SolidPauseTestSameState(ClosePage(bad), bad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseClosePageIdempotentTest, "SOLTest.SolidPause.Page.ClosePageIdempotent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Closing twice is closing once, and a closed page no longer takes Esc (the next Esc resumes)
bool FSOLSolidPauseClosePageIdempotentTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const EMenuMode modes[] = { EMenuMode::MainMenu, EMenuMode::Playing, EMenuMode::Paused };
    for (const EMenuMode mode : modes)
    {
        for (const FSolidPauseTestTab& entry : SOLIDPAUSETEST_TABS)
        {
            const FMenuState state = SolidPauseTestState(mode, entry.Tab);
            const FMenuState once = ClosePage(state);
            const FString label = FString::Printf(TEXT("mode %d on %s"), static_cast<int32>(mode), entry.Name);
            TestTrue(label + TEXT(": twice is once"), SolidPauseTestSameState(ClosePage(once), once));
            TestFalse(label + TEXT(": no page to close afterwards"), EscapeClosesPage(once));
        }
    }

    // Paused on Settings: Esc-as-Back closes the page, then Esc resumes
    const FMenuState settings = SelectTab(SolidPauseTestPaused(), EMenuTab::Settings);
    TestTrue(TEXT("Settings takes Esc"), EscapeClosesPage(settings));
    const FMenuState closed = ClosePage(settings);
    TestFalse(TEXT("the closed page no longer takes Esc"), EscapeClosesPage(closed));
    TestTrue(TEXT("then Esc resumes"), Apply(closed, EMenuEvent::EscapePressed).Mode == EMenuMode::Playing);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseEscapeUnchangedTest, "SOLTest.SolidPause.Page.ApplyEscapeUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Apply's Esc contract is unchanged: from any pause tab, page or not, it still resumes (the page rule is the caller's)
bool FSOLSolidPauseEscapeUnchangedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState paused = SolidPauseTestPaused();
    for (const FSolidPauseTestTab& entry : SOLIDPAUSETEST_TABS)
    {
        const FMenuState onTab = SelectTab(paused, entry.Tab);
        TestTrue(FString::Printf(TEXT("Esc on %s resumes"), entry.Name),
            Apply(onTab, EMenuEvent::EscapePressed).Mode == EMenuMode::Playing);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
