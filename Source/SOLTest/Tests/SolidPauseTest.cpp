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
// existed. SOLSolidMenu::EvaluatePausePanel composes the already-tested PanelProgress, PauseSlideX and DimAlpha, so the
// expected looks are built from those; the page rules check that Esc / Back on a page closes the page and that the
// existing Esc-resumes contract of Apply is unchanged
namespace
{
    constexpr float SOLIDPAUSETEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float SOLIDPAUSETEST_INF = std::numeric_limits<float>::infinity();
    constexpr double SOLIDPAUSETEST_NAN_D = std::numeric_limits<double>::quiet_NaN();
    constexpr float SOLIDPAUSETEST_TOLERANCE = 1.0e-4f;
    constexpr float SOLIDPAUSETEST_REVERSAL_TOLERANCE = 1.0e-5f;   // A reopen never pops
    constexpr double SOLIDPAUSETEST_T0_S = 10.0;                    // Exact in double with float durations added
    constexpr float SOLIDPAUSETEST_WIDTH = 420.0f;                  // A side panel width (test input)
    constexpr uint8 SOLIDPAUSETEST_BAD_TAB = 99;                    // A tab value well past Quit

    constexpr ESOLHoloMenuPhase SOLIDPAUSETEST_PHASES[] = {
        ESOLHoloMenuPhase::Closed, ESOLHoloMenuPhase::Opening, ESOLHoloMenuPhase::Open, ESOLHoloMenuPhase::Closing };

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
        SOLSolidMenu::BeginPanelOpen(anim, SOLIDPAUSETEST_T0_S, s);
        return anim;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a Closing anim begun from Open at T0 through the public API
    FSOLHoloMenuAnim SolidPauseTestClosing(const float s)
    {
        FSOLHoloMenuAnim anim = SolidPauseTestAnim(ESOLHoloMenuPhase::Open, 0.0, 1.0f);
        SOLSolidMenu::BeginPanelClose(anim, SOLIDPAUSETEST_T0_S, s);
        return anim;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the look the contract composes from the already-tested functions
    FSOLPausePanelLook SolidPauseTestExpected(const FSOLHoloMenuAnim& anim, const double nowS, const float s,
        const float panelWidth)
    {
        FSOLPausePanelLook look;
        look.Progress = SOLSolidMenu::PanelProgress(anim, nowS, s);
        look.SlideX = SOLSolidMenu::PauseSlideX(look.Progress, panelWidth);
        const bool bZeroStrength = !(s > 0.0f);    // NaN strength reads 0
        const bool bNoDim = anim.Phase == ESOLHoloMenuPhase::Closed ||
            (anim.Phase == ESOLHoloMenuPhase::Closing && bZeroStrength);
        look.DimAlpha = bNoDim ? 0.0f : SOLSolidMenu::DimAlpha(look.Progress, s);
        look.bShown = anim.Phase != ESOLHoloMenuPhase::Closed;
        look.bInteractive = anim.Phase == ESOLHoloMenuPhase::Opening || anim.Phase == ESOLHoloMenuPhase::Open;
        return look;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks two looks match: the amounts within the tolerance, the flags exactly
    void SolidPauseTestLookEqual(FAutomationTestBase& test, const FString& label, const FSOLPausePanelLook& actual,
        const FSOLPausePanelLook& expected)
    {
        test.TestEqual(label + TEXT(" Progress"), actual.Progress, expected.Progress, SOLIDPAUSETEST_TOLERANCE);
        test.TestEqual(label + TEXT(" SlideX"), actual.SlideX, expected.SlideX, SOLIDPAUSETEST_TOLERANCE);
        test.TestEqual(label + TEXT(" DimAlpha"), actual.DimAlpha, expected.DimAlpha, SOLIDPAUSETEST_TOLERANCE);
        test.TestTrue(label + TEXT(" bShown matches"), actual.bShown == expected.bShown);
        test.TestTrue(label + TEXT(" bInteractive matches"), actual.bInteractive == expected.bInteractive);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks EvaluatePausePanel against the composition at one frame
    void SolidPauseTestComposes(FAutomationTestBase& test, const FString& label, const FSOLHoloMenuAnim& anim,
        const double nowS, const float s, const float panelWidth)
    {
        SolidPauseTestLookEqual(test, label, SOLSolidMenu::EvaluatePausePanel(anim, nowS, s, panelWidth),
            SolidPauseTestExpected(anim, nowS, s, panelWidth));
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
            const FString label = FString::Printf(TEXT("Closed s %f t %f"), s, t);
            const FSOLPausePanelLook look = SOLSolidMenu::EvaluatePausePanel(closed, t, s, SOLIDPAUSETEST_WIDTH);
            TestEqual(label + TEXT(" Progress"), look.Progress, 0.0f, SOLIDPAUSETEST_TOLERANCE);
            TestEqual(label + TEXT(" DimAlpha"), look.DimAlpha, 0.0f, SOLIDPAUSETEST_TOLERANCE);
            TestFalse(label + TEXT(" not shown"), look.bShown);
            TestFalse(label + TEXT(" not interactive"), look.bInteractive);
            TestEqual(label + TEXT(" SlideX"), look.SlideX, SOLSolidMenu::PauseSlideX(0.0f, SOLIDPAUSETEST_WIDTH),
                SOLIDPAUSETEST_TOLERANCE);
            TestTrue(label + TEXT(" slid out (SlideX < 0)"), look.SlideX < 0.0f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseOpeningTest, "SOLTest.SolidPause.Panel.Opening",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Half way through an open at s = 1: shown, interactive, and every amount the composed functions' value
bool FSOLSolidPauseOpeningTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloMenuAnim opening = SolidPauseTestOpening(1.0f);
    const double mid = SOLIDPAUSETEST_T0_S + 0.5 * SOL::SolidMenu::PANEL_OPEN_S;
    const FSOLPausePanelLook look = SOLSolidMenu::EvaluatePausePanel(opening, mid, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestTrue(TEXT("Opening is shown"), look.bShown);
    TestTrue(TEXT("Opening is interactive"), look.bInteractive);
    TestEqual(TEXT("half way progress"), look.Progress, 0.5f, SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("Progress is PanelProgress"), look.Progress, SOLSolidMenu::PanelProgress(opening, mid, 1.0f),
        SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("SlideX is PauseSlideX"), look.SlideX, SOLSolidMenu::PauseSlideX(look.Progress,
        SOLIDPAUSETEST_WIDTH), SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("DimAlpha is DimAlpha"), look.DimAlpha, SOLSolidMenu::DimAlpha(look.Progress, 1.0f),
        SOLIDPAUSETEST_TOLERANCE);
    TestTrue(TEXT("part slid in"), look.SlideX < 0.0f &&
        look.SlideX > SOLSolidMenu::PauseSlideX(0.0f, SOLIDPAUSETEST_WIDTH));

    // Through the whole open, at quarter steps
    for (int32 step = 0; step <= 4; ++step)
    {
        const double t = SOLIDPAUSETEST_T0_S + 0.25 * step * SOL::SolidMenu::PANEL_OPEN_S;
        SolidPauseTestComposes(*this, FString::Printf(TEXT("Opening step %d"), step), opening, t, 1.0f,
            SOLIDPAUSETEST_WIDTH);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseOpenTest, "SOLTest.SolidPause.Panel.Open",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Open: progress 1, in place (SlideX 0), the full dim, shown and interactive
bool FSOLSolidPauseOpenTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloMenuAnim open = SolidPauseTestAnim(ESOLHoloMenuPhase::Open, SOLIDPAUSETEST_T0_S, 1.0f);
    const FSOLPausePanelLook look = SOLSolidMenu::EvaluatePausePanel(open, SOLIDPAUSETEST_T0_S + 3.0, 1.0f,
        SOLIDPAUSETEST_WIDTH);
    TestEqual(TEXT("Progress 1"), look.Progress, 1.0f, SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("SlideX 0"), look.SlideX, 0.0f, SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("full dim"), look.DimAlpha, SOL::SolidMenu::PAUSE_DIM_ALPHA, SOLIDPAUSETEST_TOLERANCE);
    TestTrue(TEXT("shown"), look.bShown);
    TestTrue(TEXT("interactive"), look.bInteractive);
    SolidPauseTestComposes(*this, TEXT("Open"), open, SOLIDPAUSETEST_T0_S + 3.0, 1.0f, SOLIDPAUSETEST_WIDTH);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseClosingTest, "SOLTest.SolidPause.Panel.Closing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Part way through a close at s = 1: still shown and dimmed, but it takes no input (the input is already back)
bool FSOLSolidPauseClosingTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloMenuAnim closing = SolidPauseTestClosing(1.0f);
    const double quarter = SOLIDPAUSETEST_T0_S + 0.25 * SOL::SolidMenu::CLOSE_S;
    const FSOLPausePanelLook look = SOLSolidMenu::EvaluatePausePanel(closing, quarter, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestTrue(TEXT("Closing is shown"), look.bShown);
    TestFalse(TEXT("Closing is not interactive"), look.bInteractive);
    TestEqual(TEXT("a quarter closed"), look.Progress, 0.75f, SOLIDPAUSETEST_TOLERANCE);
    TestTrue(TEXT("still dimmed"), look.DimAlpha > 0.0f);
    TestEqual(TEXT("DimAlpha is DimAlpha"), look.DimAlpha, SOLSolidMenu::DimAlpha(look.Progress, 1.0f),
        SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("SlideX is PauseSlideX"), look.SlideX, SOLSolidMenu::PauseSlideX(look.Progress,
        SOLIDPAUSETEST_WIDTH), SOLIDPAUSETEST_TOLERANCE);

    // Through the whole close, at quarter steps: never interactive while Closing
    for (int32 step = 0; step <= 4; ++step)
    {
        const double t = SOLIDPAUSETEST_T0_S + 0.25 * step * SOL::SolidMenu::CLOSE_S;
        const FString label = FString::Printf(TEXT("Closing step %d"), step);
        SolidPauseTestComposes(*this, label, closing, t, 1.0f, SOLIDPAUSETEST_WIDTH);
        TestFalse(label + TEXT(" not interactive"),
            SOLSolidMenu::EvaluatePausePanel(closing, t, 1.0f, SOLIDPAUSETEST_WIDTH).bInteractive);
    }
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
        const FSOLPausePanelLook open = SOLSolidMenu::EvaluatePausePanel(opening, SOLIDPAUSETEST_T0_S, s,
            SOLIDPAUSETEST_WIDTH);
        TestEqual(tag + TEXT(" Opening progress 1"), open.Progress, 1.0f, SOLIDPAUSETEST_TOLERANCE);
        TestEqual(tag + TEXT(" Opening full dim"), open.DimAlpha, SOL::SolidMenu::PAUSE_DIM_ALPHA,
            SOLIDPAUSETEST_TOLERANCE);
        TestEqual(tag + TEXT(" Opening SlideX 0"), open.SlideX, 0.0f, SOLIDPAUSETEST_TOLERANCE);
        TestTrue(tag + TEXT(" Opening shown"), open.bShown);
        TestTrue(tag + TEXT(" Opening interactive"), open.bInteractive);

        // Closing, just begun
        const FSOLHoloMenuAnim closing = SolidPauseTestAnim(ESOLHoloMenuPhase::Closing, SOLIDPAUSETEST_T0_S, 1.0f);
        const FSOLPausePanelLook closed = SOLSolidMenu::EvaluatePausePanel(closing, SOLIDPAUSETEST_T0_S, s,
            SOLIDPAUSETEST_WIDTH);
        TestEqual(tag + TEXT(" Closing progress 0"), closed.Progress, 0.0f, SOLIDPAUSETEST_TOLERANCE);
        TestEqual(tag + TEXT(" Closing no dim"), closed.DimAlpha, 0.0f, SOLIDPAUSETEST_TOLERANCE);
        TestEqual(tag + TEXT(" Closing SlideX fully out"), closed.SlideX,
            SOLSolidMenu::PauseSlideX(0.0f, SOLIDPAUSETEST_WIDTH), SOLIDPAUSETEST_TOLERANCE);
        TestFalse(tag + TEXT(" Closing not interactive"), closed.bInteractive);

        // Open: the dim is not an effect, so it stays full
        const FSOLHoloMenuAnim openAnim = SolidPauseTestAnim(ESOLHoloMenuPhase::Open, SOLIDPAUSETEST_T0_S, 1.0f);
        TestEqual(tag + TEXT(" Open full dim"), SOLSolidMenu::EvaluatePausePanel(openAnim, SOLIDPAUSETEST_T0_S, s,
            SOLIDPAUSETEST_WIDTH).DimAlpha, SOL::SolidMenu::PAUSE_DIM_ALPHA, SOLIDPAUSETEST_TOLERANCE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseStrengthTwoTest, "SOLTest.SolidPause.Panel.StrengthTwo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// At s = 2 every phase composes the same functions; a strength past 2 reads as 2
bool FSOLSolidPauseStrengthTwoTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloMenuAnim anims[] = { SolidPauseTestAnim(ESOLHoloMenuPhase::Closed, SOLIDPAUSETEST_T0_S, 0.0f),
        SolidPauseTestOpening(2.0f), SolidPauseTestAnim(ESOLHoloMenuPhase::Open, SOLIDPAUSETEST_T0_S, 1.0f),
        SolidPauseTestClosing(2.0f) };
    for (const FSOLHoloMenuAnim& anim : anims)
    {
        for (int32 step = 0; step <= 4; ++step)
        {
            const double t = SOLIDPAUSETEST_T0_S + 0.25 * step * SOL::SolidMenu::CLOSE_S;
            const FString label = FString::Printf(TEXT("%s step %d"), SolidPauseTestPhaseName(anim.Phase), step);
            SolidPauseTestComposes(*this, label + TEXT(" s 2"), anim, t, 2.0f, SOLIDPAUSETEST_WIDTH);
            SolidPauseTestLookEqual(*this, label + TEXT(" s 5 reads 2"),
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
    const FSOLPausePanelLook opening = SOLSolidMenu::EvaluatePausePanel(SolidPauseTestOpening(1.0f),
        SOLIDPAUSETEST_NAN_D, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestEqual(TEXT("Opening reads 1"), opening.Progress, 1.0f, SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("Opening SlideX 0"), opening.SlideX, 0.0f, SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("Opening full dim"), opening.DimAlpha, SOL::SolidMenu::PAUSE_DIM_ALPHA, SOLIDPAUSETEST_TOLERANCE);
    TestTrue(TEXT("Opening interactive"), opening.bInteractive);

    const FSOLPausePanelLook closing = SOLSolidMenu::EvaluatePausePanel(SolidPauseTestClosing(1.0f),
        SOLIDPAUSETEST_NAN_D, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestEqual(TEXT("Closing reads 0"), closing.Progress, 0.0f, SOLIDPAUSETEST_TOLERANCE);
    TestFalse(TEXT("Closing not interactive"), closing.bInteractive);

    for (const ESOLHoloMenuPhase phase : SOLIDPAUSETEST_PHASES)
    {
        const FSOLHoloMenuAnim anim = SolidPauseTestAnim(phase, SOLIDPAUSETEST_T0_S, 0.5f);
        const FString label = FString::Printf(TEXT("%s NaN time"), SolidPauseTestPhaseName(phase));
        const FSOLPausePanelLook look = SOLSolidMenu::EvaluatePausePanel(anim, SOLIDPAUSETEST_NAN_D, 1.0f,
            SOLIDPAUSETEST_WIDTH);
        TestTrue(label + TEXT(" finite"), FMath::IsFinite(look.Progress) && FMath::IsFinite(look.SlideX) &&
            FMath::IsFinite(look.DimAlpha));
        SolidPauseTestComposes(*this, label, anim, SOLIDPAUSETEST_NAN_D, 1.0f, SOLIDPAUSETEST_WIDTH);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseSlideSignTest, "SOLTest.SolidPause.Panel.SlideNeverPositive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Over every phase, time, strength and width (bad widths included) SlideX is finite and never positive, Progress in
// [0, 1] and the dim in [0, PAUSE_DIM_ALPHA]
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
                        look.DimAlpha <= SOL::SolidMenu::PAUSE_DIM_ALPHA + SOLIDPAUSETEST_TOLERANCE);
                    TestEqual(label + TEXT(" SlideX is PauseSlideX"), look.SlideX,
                        SOLSolidMenu::PauseSlideX(look.Progress, w), SOLIDPAUSETEST_TOLERANCE);
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPauseReopenTest, "SOLTest.SolidPause.Panel.ReopenDuringClose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Re-pausing mid-close is interactive again at once and continues from the close's progress (no pop)
bool FSOLSolidPauseReopenTest::RunTest(const FString& /*parameters*/)
{
    FSOLHoloMenuAnim anim = SolidPauseTestClosing(1.0f);
    const double t1 = SOLIDPAUSETEST_T0_S + 0.5 * SOL::SolidMenu::CLOSE_S;
    const FSOLPausePanelLook before = SOLSolidMenu::EvaluatePausePanel(anim, t1, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestFalse(TEXT("mid-close is not interactive"), before.bInteractive);
    TestTrue(TEXT("mid-close progress strictly inside (0, 1)"), before.Progress > 0.0f && before.Progress < 1.0f);

    TestTrue(TEXT("BeginPanelOpen from Closing starts an open"), SOLSolidMenu::BeginPanelOpen(anim, t1, 1.0f));
    const FSOLPausePanelLook after = SOLSolidMenu::EvaluatePausePanel(anim, t1, 1.0f, SOLIDPAUSETEST_WIDTH);
    TestTrue(TEXT("reopened is interactive"), after.bInteractive);
    TestTrue(TEXT("reopened is shown"), after.bShown);
    TestEqual(TEXT("progress continues from the close"), after.Progress, before.Progress,
        SOLIDPAUSETEST_REVERSAL_TOLERANCE);
    TestEqual(TEXT("SlideX continues"), after.SlideX, before.SlideX, SOLIDPAUSETEST_TOLERANCE);
    TestEqual(TEXT("dim continues"), after.DimAlpha, before.DimAlpha, SOLIDPAUSETEST_TOLERANCE);

    // It then opens onwards and settles Open
    const double t2 = t1 + 0.25 * SOL::SolidMenu::PANEL_OPEN_S;
    TestTrue(TEXT("progress rises after the reopen"),
        SOLSolidMenu::EvaluatePausePanel(anim, t2, 1.0f, SOLIDPAUSETEST_WIDTH).Progress > before.Progress);
    const double settled = t1 + 2.0 * SOL::SolidMenu::PANEL_OPEN_S;
    SOLSolidMenu::AdvancePanel(anim, settled, 1.0f);
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
