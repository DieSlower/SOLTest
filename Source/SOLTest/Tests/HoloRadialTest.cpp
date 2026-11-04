/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLInteractMenus.h"
#include "SOLConstants.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloMenuMath.h"
#include "UI/SOLHoloRadial.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #42 part 42d hologram radial menu (Docs/SDDs/42-hologram-in-flight-menus.md sections 4.5 and 5,
// the radial row): the per-frame update of its anim, cached state and cues (open at the opening press, hover on each
// sector change while Open and rate limited, select on a confirmed enabled action, none on a cancel, all at any
// strength, D21), the close drawn from the cached state, the strength-0 look (D18), and how a sector and a label draw
// from the anim's look
namespace
{
    constexpr double HOLORADIALTEST_T0 = 100.0;
    constexpr float HOLORADIALTEST_S1 = 1.0f;
    constexpr float HOLORADIALTEST_TOLERANCE = 1.0e-6f;
    constexpr float HOLORADIALTEST_HALF_APERTURE = 0.5f;

    //////////////////////////////////////////////////////////////////////////
    // Returns an open radial menu of count sectors with a highlighted sector
    FSOLRadialMenuState HoloRadialTestOpenMenu(const int32 count, const int32 highlighted)
    {
        FSOLRadialMenuState radial;
        radial.bOpen = true;
        radial.Object.Kind = ESOLObjectKind::Body;
        radial.Object.Index = 3;
        radial.CenterPx = FVector2D(400.0, 300.0);
        radial.Count = count;
        radial.Highlighted = highlighted;
        return radial;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an action list of count entries, every one enabled but the last
    FSOLActionList HoloRadialTestActions(const int32 count)
    {
        FSOLActionList actions;
        actions.Num = count;
        for (int32 index = 0; index < count; ++index)
        {
            actions.Entries[index].Action = static_cast<ESOLAction>(index);
            actions.Entries[index].Block = index == count - 1 ? ESOLActionBlock::NotYetAvailable
                : ESOLActionBlock::None;
        }
        return actions;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a set of cues is all false
    bool HoloRadialTestNoCues(const FSOLHoloRadialCues& cues)
    {
        return !cues.bOpen && !cues.bHover && !cues.bSelect;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a state synced to zero counters with no menu open (the first update done)
    FSOLHoloRadialState HoloRadialTestSynced()
    {
        FSOLHoloRadialState state;
        SOLHoloRadial::Update(state, FSOLRadialMenuState(), FSOLActionList(), 0, 0, true, true, HOLORADIALTEST_T0,
            HOLORADIALTEST_S1);
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a synced state whose menu (5 sectors, nothing highlighted) opened at T0 and is Open by openS
    FSOLHoloRadialState HoloRadialTestOpened(const double openS, const float s)
    {
        FSOLHoloRadialState state = HoloRadialTestSynced();
        const FSOLRadialMenuState radial = HoloRadialTestOpenMenu(5, INDEX_NONE);
        const FSOLActionList actions = HoloRadialTestActions(5);
        SOLHoloRadial::Update(state, radial, actions, 1, 0, true, true, HOLORADIALTEST_T0, s);
        SOLHoloRadial::Update(state, radial, actions, 1, 0, true, true, openS, s);
        return state;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialFirstUpdateTest, "SOLTest.HoloRadial.FirstUpdateSyncs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The first update only syncs the counters: no cues; a menu already open shows whole at once
bool FSOLHoloRadialFirstUpdateTest::RunTest(const FString& /*parameters*/)
{
    FSOLHoloRadialState closed;
    const FSOLHoloRadialCues closedCues = SOLHoloRadial::Update(closed, FSOLRadialMenuState(), FSOLActionList(), 4, 2,
        true, false, HOLORADIALTEST_T0, HOLORADIALTEST_S1);
    TestTrue(TEXT("no cues on the first update (closed)"), HoloRadialTestNoCues(closedCues));
    TestEqual(TEXT("counters synced"), closed.SeenOpenCount, 4);
    TestEqual(TEXT("confirm counter synced"), closed.SeenConfirmCount, 2);
    TestTrue(TEXT("closed stays Closed"), closed.Anim.Phase == ESOLHoloMenuPhase::Closed);

    FSOLHoloRadialState open;
    const FSOLRadialMenuState radial = HoloRadialTestOpenMenu(4, 2);
    const FSOLHoloRadialCues openCues = SOLHoloRadial::Update(open, radial, HoloRadialTestActions(4), 7, 3, true,
        false, HOLORADIALTEST_T0, HOLORADIALTEST_S1);
    TestTrue(TEXT("no cues on the first update (open)"), HoloRadialTestNoCues(openCues));
    TestTrue(TEXT("an open menu shows Open at once"), open.Anim.Phase == ESOLHoloMenuPhase::Open);
    TestEqual(TEXT("its state is cached"), open.Radial.Highlighted, 2);
    TestEqual(TEXT("its actions are cached"), open.Actions.Num, 4);
    TestEqual(TEXT("its highlight is the hovered item"), open.Anim.HoveredItem, 2);
    const FSOLHoloMenuLook look = SOLHoloMenu::Evaluate(open.Anim, HOLORADIALTEST_T0, HOLORADIALTEST_S1);
    TestEqual(TEXT("whole at once"), look.FrameFraction, 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialOpenTest, "SOLTest.HoloRadial.OpenCueAndDrawIn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A changed open count asks for the open cue once; the rim arcs draw in over OPEN_S, then the menu is Open
bool FSOLHoloRadialOpenTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    FSOLHoloRadialState state = HoloRadialTestSynced();
    const FSOLRadialMenuState radial = HoloRadialTestOpenMenu(5, INDEX_NONE);
    const FSOLActionList actions = HoloRadialTestActions(5);
    const FSOLHoloRadialCues opened = SOLHoloRadial::Update(state, radial, actions, 1, 0, true, true,
        HOLORADIALTEST_T0, HOLORADIALTEST_S1);
    TestTrue(TEXT("open cue at the opening press"), opened.bOpen);
    TestFalse(TEXT("no hover cue"), opened.bHover);
    TestFalse(TEXT("no select cue"), opened.bSelect);
    TestTrue(TEXT("Opening"), state.Anim.Phase == ESOLHoloMenuPhase::Opening);
    TestEqual(TEXT("cached count"), state.Radial.Count, 5);
    TestTrue(TEXT("cached open"), state.Radial.bOpen);
    TestEqual(TEXT("draw-in starts at 0"),
        SOLHoloMenu::Evaluate(state.Anim, HOLORADIALTEST_T0, HOLORADIALTEST_S1).FrameFraction, 0.0f);

    const double midS = HOLORADIALTEST_T0 + 0.5 * OPEN_S;
    const FSOLHoloRadialCues mid = SOLHoloRadial::Update(state, radial, actions, 1, 0, true, true, midS,
        HOLORADIALTEST_S1);
    TestTrue(TEXT("no cue again mid-open"), HoloRadialTestNoCues(mid));
    const FSOLHoloMenuLook look = SOLHoloMenu::Evaluate(state.Anim, midS, HOLORADIALTEST_S1);
    TestTrue(TEXT("mid-open draw-in strictly between 0 and 1"), look.FrameFraction > 0.0f && look.FrameFraction < 1.0f);

    SOLHoloRadial::Update(state, radial, actions, 1, 0, true, true, HOLORADIALTEST_T0 + OPEN_S, HOLORADIALTEST_S1);
    TestTrue(TEXT("Open after OPEN_S"), state.Anim.Phase == ESOLHoloMenuPhase::Open);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialHoverTest, "SOLTest.HoloRadial.HoverCues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The highlighted sector is the hovered item: a change while Open asks for the hover cue, rate limited; none while it
// draws in, none for the same sector, none for the centre
bool FSOLHoloRadialHoverTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const FSOLActionList actions = HoloRadialTestActions(5);

    // While opening: the highlight follows, but no cue (the highlight a menu opens with)
    FSOLHoloRadialState opening = HoloRadialTestSynced();
    SOLHoloRadial::Update(opening, HoloRadialTestOpenMenu(5, INDEX_NONE), actions, 1, 0, true, true,
        HOLORADIALTEST_T0, HOLORADIALTEST_S1);
    const FSOLHoloRadialCues early = SOLHoloRadial::Update(opening, HoloRadialTestOpenMenu(5, 1), actions, 1, 0, true,
        true, HOLORADIALTEST_T0 + 0.25 * OPEN_S, HOLORADIALTEST_S1);
    TestFalse(TEXT("no hover cue while opening"), early.bHover);
    TestEqual(TEXT("hovered while opening"), opening.Anim.HoveredItem, 1);

    // Open: a change asks for the cue; the same sector again does not
    const double openS = HOLORADIALTEST_T0 + 2.0 * OPEN_S;
    FSOLHoloRadialState state = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    TestTrue(TEXT("Open"), state.Anim.Phase == ESOLHoloMenuPhase::Open);
    const double cueS = openS + 1.0;
    const FSOLHoloRadialCues first = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 0), actions, 1, 0, true,
        true, cueS, HOLORADIALTEST_S1);
    TestTrue(TEXT("hover cue on a sector change"), first.bHover);
    TestFalse(TEXT("only the hover cue"), first.bOpen || first.bSelect);
    TestEqual(TEXT("cached highlight"), state.Radial.Highlighted, 0);

    // A fast change inside the hover cue interval is not cued; the same sector again is not; a change past it is
    const FSOLHoloRadialCues fast = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 1), actions, 1, 0, true,
        true, cueS + 0.5 * HOVER_CUE_MIN_INTERVAL_S, HOLORADIALTEST_S1);
    TestFalse(TEXT("rate limited"), fast.bHover);
    TestEqual(TEXT("the highlight still follows"), state.Anim.HoveredItem, 1);
    const FSOLHoloRadialCues same = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 1), actions, 1, 0, true,
        true, openS + 2.0, HOLORADIALTEST_S1);
    TestFalse(TEXT("no cue for the same sector"), same.bHover);
    const FSOLHoloRadialCues later = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 2), actions, 1, 0, true,
        true, openS + 3.0, HOLORADIALTEST_S1);
    TestTrue(TEXT("cued past the interval"), later.bHover);

    // The centre (nothing highlighted) is not a hover
    const FSOLHoloRadialCues centre = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, INDEX_NONE), actions, 1,
        0, true, true, openS + 4.0, HOLORADIALTEST_S1);
    TestFalse(TEXT("no cue for the centre"), centre.bHover);
    TestEqual(TEXT("nothing hovered"), state.Anim.HoveredItem, static_cast<int32>(INDEX_NONE));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialCloseTest, "SOLTest.HoloRadial.SelectAndClose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A confirm asks for the select cue; the close plays from the cached state; a cancel asks for nothing
bool FSOLHoloRadialCloseTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const FSOLActionList actions = HoloRadialTestActions(5);
    const double openS = HOLORADIALTEST_T0 + 2.0 * OPEN_S;

    // Confirmed: the interaction's menu is closed (its state reset), the cache keeps the last drawn one
    FSOLHoloRadialState state = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 3), actions, 1, 0, true, true, openS + 0.5,
        HOLORADIALTEST_S1);
    const double closeS = openS + 1.0;
    const FSOLHoloRadialCues confirmed = SOLHoloRadial::Update(state, FSOLRadialMenuState(), FSOLActionList(), 1, 1,
        true, true, closeS, HOLORADIALTEST_S1);
    TestTrue(TEXT("select cue on a confirm"), confirmed.bSelect);
    TestFalse(TEXT("no open or hover cue"), confirmed.bOpen || confirmed.bHover);
    TestTrue(TEXT("Closing"), state.Anim.Phase == ESOLHoloMenuPhase::Closing);
    TestEqual(TEXT("cached highlight kept"), state.Radial.Highlighted, 3);
    TestEqual(TEXT("cached count kept"), state.Radial.Count, 5);
    TestEqual(TEXT("cached actions kept"), state.Actions.Num, 5);
    const FSOLHoloMenuLook mid = SOLHoloMenu::Evaluate(state.Anim, closeS + 0.6 * CLOSE_S, HOLORADIALTEST_S1);
    TestTrue(TEXT("visible while closing"), mid.bVisible);
    TestTrue(TEXT("drawing out"), mid.FrameFraction > 0.0f && mid.FrameFraction < 1.0f);
    SOLHoloRadial::Update(state, FSOLRadialMenuState(), FSOLActionList(), 1, 1, true, true, closeS + CLOSE_S,
        HOLORADIALTEST_S1);
    TestTrue(TEXT("Closed after CLOSE_S"), state.Anim.Phase == ESOLHoloMenuPhase::Closed);

    // Cancelled: closed with the confirm count unchanged, so no cue at all
    FSOLHoloRadialState cancelled = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    const FSOLHoloRadialCues cancel = SOLHoloRadial::Update(cancelled, FSOLRadialMenuState(), FSOLActionList(), 1, 0,
        true, true, closeS, HOLORADIALTEST_S1);
    TestTrue(TEXT("no cue on a cancel"), HoloRadialTestNoCues(cancel));
    TestTrue(TEXT("a cancel closes too"), cancelled.Anim.Phase == ESOLHoloMenuPhase::Closing);

    // Hidden (the map opened) while open: it closes the same way
    FSOLHoloRadialState hidden = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    SOLHoloRadial::Update(hidden, HoloRadialTestOpenMenu(5, 0), actions, 1, 0, false, true, closeS,
        HOLORADIALTEST_S1);
    TestTrue(TEXT("hidden closes"), hidden.Anim.Phase == ESOLHoloMenuPhase::Closing);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialEventsTest, "SOLTest.HoloRadial.MissedFramesAndReopen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Frames skipped while a menu closed: closed at once (no stale close later). A new menu seen while still Open
// restarts the draw-in from 0 and asks for the open cue; an open and confirm between two updates gives both cues
bool FSOLHoloRadialEventsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const FSOLActionList actions = HoloRadialTestActions(5);
    const double openS = HOLORADIALTEST_T0 + 2.0 * OPEN_S;

    FSOLHoloRadialState skipped = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    SOLHoloRadial::Update(skipped, FSOLRadialMenuState(), FSOLActionList(), 1, 0, true, false, openS + 5.0,
        HOLORADIALTEST_S1);
    TestTrue(TEXT("closed at once after skipped frames"), skipped.Anim.Phase == ESOLHoloMenuPhase::Closed);

    FSOLHoloRadialState closing = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    SOLHoloRadial::Update(closing, FSOLRadialMenuState(), FSOLActionList(), 1, 0, true, true, openS + 1.0,
        HOLORADIALTEST_S1);
    SOLHoloRadial::Update(closing, FSOLRadialMenuState(), FSOLActionList(), 1, 0, true, false,
        openS + 1.0 + 0.1 * CLOSE_S, HOLORADIALTEST_S1);
    TestTrue(TEXT("a close in progress ends at once after skipped frames"),
        closing.Anim.Phase == ESOLHoloMenuPhase::Closed);

    FSOLHoloRadialState reopened = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    SOLHoloRadial::Update(reopened, HoloRadialTestOpenMenu(5, 1), actions, 1, 0, true, true, openS + 1.0,
        HOLORADIALTEST_S1);
    const double lastCueS = reopened.Anim.LastHoverCueS;
    FSOLRadialMenuState other = HoloRadialTestOpenMenu(3, INDEX_NONE);
    other.CenterPx = FVector2D(100.0, 120.0);
    const double reopenS = openS + 2.0;
    const FSOLHoloRadialCues cues = SOLHoloRadial::Update(reopened, other, HoloRadialTestActions(3), 2, 0, true, true,
        reopenS, HOLORADIALTEST_S1);
    TestTrue(TEXT("open cue for the new menu"), cues.bOpen);
    TestTrue(TEXT("draws in again"), reopened.Anim.Phase == ESOLHoloMenuPhase::Opening);
    TestEqual(TEXT("from 0"), SOLHoloMenu::Progress(reopened.Anim, reopenS, HOLORADIALTEST_S1), 0.0f);
    TestEqual(TEXT("the new menu is cached"), reopened.Radial.Count, 3);
    TestEqual(TEXT("the hover cue's rate limit is kept"), reopened.Anim.LastHoverCueS, lastCueS);

    FSOLHoloRadialState quick = HoloRadialTestSynced();
    const FSOLHoloRadialCues both = SOLHoloRadial::Update(quick, FSOLRadialMenuState(), FSOLActionList(), 1, 1, true,
        true, HOLORADIALTEST_T0 + 1.0, HOLORADIALTEST_S1);
    TestTrue(TEXT("open and select cues for a press and release between updates"), both.bOpen && both.bSelect);
    TestTrue(TEXT("nothing to draw"), quick.Anim.Phase == ESOLHoloMenuPhase::Closed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialFlatTest, "SOLTest.HoloRadial.StrengthZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Strength 0 (D18): open and close are instant, no hover glow, flat arcs, the highlight keeps Secondary; the cues
// still play (D21)
bool FSOLHoloRadialFlatTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const FSOLActionList actions = HoloRadialTestActions(5);
    FSOLHoloRadialState state = HoloRadialTestSynced();
    const FSOLHoloRadialCues opened = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, INDEX_NONE), actions, 1,
        0, true, true, HOLORADIALTEST_T0, 0.0f);
    TestTrue(TEXT("open cue at strength 0"), opened.bOpen);
    TestTrue(TEXT("Open on the same update"), state.Anim.Phase == ESOLHoloMenuPhase::Open);
    const FSOLHoloMenuLook first = SOLHoloMenu::Evaluate(state.Anim, HOLORADIALTEST_T0, 0.0f);
    TestEqual(TEXT("whole draw-in at once"), first.FrameFraction, 1.0f);
    TestEqual(TEXT("whole fill at once"), first.FillAlpha, 1.0f);

    const FSOLHoloRadialCues hovered = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 2), actions, 1, 0, true,
        true, HOLORADIALTEST_T0 + 0.01, 0.0f);
    TestTrue(TEXT("hover cue at strength 0"), hovered.bHover);
    const FSOLHoloMenuLook look = SOLHoloMenu::Evaluate(state.Anim, HOLORADIALTEST_T0 + 1.0, 0.0f);
    TestEqual(TEXT("no hover glow"), look.HoverIntensity, 0.0f);
    const FSOLHoloRadialSectorLook sector = SOLHoloRadial::SectorLook(HOLORADIALTEST_HALF_APERTURE, true, true, look,
        0.0f);
    TestEqual(TEXT("full aperture"), sector.HalfApertureRad, HOLORADIALTEST_HALF_APERTURE);
    TestEqual(TEXT("today's highlight, no glow on top"), sector.EdgeIntensity,
        SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY);
    TestTrue(TEXT("highlight keeps Secondary"), sector.Color == SOL::HudPalette::SECONDARY);
    TestEqual(TEXT("flat arcs"), sector.PadPx, FRAME_FLAT_PAD_PX);

    const FSOLHoloRadialCues selected = SOLHoloRadial::Update(state, FSOLRadialMenuState(), FSOLActionList(), 1, 1,
        true, true, HOLORADIALTEST_T0 + 2.0, 0.0f);
    TestTrue(TEXT("select cue at strength 0"), selected.bSelect);
    TestTrue(TEXT("closed at once"), state.Anim.Phase == ESOLHoloMenuPhase::Closed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialSectorLookTest, "SOLTest.HoloRadial.SectorLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A sector's rim arcs grow by the draw-in; today's intensities, the hover glow on the highlighted one only (times the
// fill), the fill arc only on the highlighted enabled one, Secondary when highlighted, the strength's glow pad
bool FSOLHoloRadialSectorLookTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Interact;
    FSOLHoloMenuLook look;
    look.bVisible = true;
    look.FrameFraction = 0.5f;
    look.FillAlpha = 0.25f;
    look.HoverIntensity = 0.8f;
    const float half = HOLORADIALTEST_HALF_APERTURE;

    const FSOLHoloRadialSectorLook normal = SOLHoloRadial::SectorLook(half, false, true, look, 1.0f);
    TestEqual(TEXT("aperture grown from the middle"), normal.HalfApertureRad, 0.5f * half, HOLORADIALTEST_TOLERANCE);
    TestEqual(TEXT("normal edge"), normal.EdgeIntensity, RADIAL_INTENSITY, HOLORADIALTEST_TOLERANCE);
    TestEqual(TEXT("no fill unhighlighted"), normal.FillIntensity, 0.0f);
    TestTrue(TEXT("Primary unhighlighted"), normal.Color == SOL::HudPalette::PRIMARY);
    TestEqual(TEXT("full glow pad at 1"), normal.PadPx, SOLHoloFrameBuild::LinePad(1.0f));

    const FSOLHoloRadialSectorLook disabled = SOLHoloRadial::SectorLook(half, false, false, look, 1.0f);
    TestEqual(TEXT("disabled edge"), disabled.EdgeIntensity, RADIAL_DISABLED_INTENSITY, HOLORADIALTEST_TOLERANCE);

    const FSOLHoloRadialSectorLook highlighted = SOLHoloRadial::SectorLook(half, true, true, look, 1.0f);
    TestEqual(TEXT("highlight plus the hover glow times the fill"), highlighted.EdgeIntensity,
        RADIAL_HIGHLIGHT_INTENSITY + 0.8f * 0.25f, HOLORADIALTEST_TOLERANCE);
    TestEqual(TEXT("fill faded by the fill alpha"), highlighted.FillIntensity, RADIAL_FILL_INTENSITY * 0.25f,
        HOLORADIALTEST_TOLERANCE);
    TestTrue(TEXT("Secondary highlighted"), highlighted.Color == SOL::HudPalette::SECONDARY);

    const FSOLHoloRadialSectorLook highlightedDisabled = SOLHoloRadial::SectorLook(half, true, false, look, 1.0f);
    TestEqual(TEXT("a highlighted disabled sector has no fill"), highlightedDisabled.FillIntensity, 0.0f);
    // How much hover glow a disabled sector takes is SDD 42 Q13 (a): none, so the rim stays dim; asserted through the constant and the literal 0.22
    TestEqual(TEXT("no hover glow on a disabled sector"), SOL::HoloMenu::RADIAL_DISABLED_HOVER_GLOW_SCALE, 0.0f);
    TestEqual(TEXT("its edge is dim plus the hover glow at the disabled glow scale"), highlightedDisabled.EdgeIntensity,
        RADIAL_DISABLED_INTENSITY + 0.8f * 0.25f * SOL::HoloMenu::RADIAL_DISABLED_HOVER_GLOW_SCALE,
        HOLORADIALTEST_TOLERANCE);

    FSOLHoloMenuLook closed;
    const FSOLHoloRadialSectorLook none = SOLHoloRadial::SectorLook(half, true, true, closed, 1.0f);
    TestEqual(TEXT("no aperture at draw-in 0"), none.HalfApertureRad, 0.0f);
    look.FrameFraction = 3.0f;
    TestEqual(TEXT("draw-in clamped to the full aperture"),
        SOLHoloRadial::SectorLook(half, false, true, look, 2.0f).HalfApertureRad, half);
    TestEqual(TEXT("glow pad at 2"), SOLHoloRadial::SectorLook(half, false, true, look, 2.0f).PadPx,
        SOL::Hologram::HUD_GLOW_PAD_PX, HOLORADIALTEST_TOLERANCE);

    // A NaN fill fades everything out (no NaN reaches the batch): no fill arc, no glow on top of today's highlight
    FSOLHoloMenuLook bad = look;
    bad.FrameFraction = 1.0f;
    bad.FillAlpha = std::numeric_limits<float>::quiet_NaN();
    const FSOLHoloRadialSectorLook guarded = SOLHoloRadial::SectorLook(half, true, true, bad, 1.0f);
    TestEqual(TEXT("NaN fill: no fill arc"), guarded.FillIntensity, 0.0f);
    TestEqual(TEXT("NaN fill: today's highlight only"), guarded.EdgeIntensity, RADIAL_HIGHLIGHT_INTENSITY);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialReopenClosingTest, "SOLTest.HoloRadial.ReopenWhileClosing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// SDD 4.5 (radial): a new menu opened while the previous one still closes draws in from Closed with its open cue
// (SDD 5: open at the press), never reversing from the old menu's progress; the new menu is the one cached
bool FSOLHoloRadialReopenClosingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const double openS = HOLORADIALTEST_T0 + 2.0 * OPEN_S;
    FSOLHoloRadialState state = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    const double closeS = openS + 1.0;
    SOLHoloRadial::Update(state, FSOLRadialMenuState(), FSOLActionList(), 1, 0, true, true, closeS,
        HOLORADIALTEST_S1);
    const double reopenS = closeS + 0.4 * CLOSE_S;
    TestTrue(TEXT("still closing"), state.Anim.Phase == ESOLHoloMenuPhase::Closing
        && SOLHoloMenu::Progress(state.Anim, reopenS, HOLORADIALTEST_S1) > 0.0f);

    FSOLRadialMenuState other = HoloRadialTestOpenMenu(3, INDEX_NONE);
    other.Object.Index = 5;
    other.CenterPx = FVector2D(200.0, 150.0);
    const FSOLHoloRadialCues cues = SOLHoloRadial::Update(state, other, HoloRadialTestActions(3), 2, 0, true, true,
        reopenS, HOLORADIALTEST_S1);
    TestTrue(TEXT("open cue for the new menu"), cues.bOpen);
    TestFalse(TEXT("no hover or select cue"), cues.bHover || cues.bSelect);
    TestTrue(TEXT("Opening"), state.Anim.Phase == ESOLHoloMenuPhase::Opening);
    TestEqual(TEXT("draws in from 0, not from the old menu's progress"),
        SOLHoloMenu::Progress(state.Anim, reopenS, HOLORADIALTEST_S1), 0.0f);
    TestEqual(TEXT("the new menu is cached"), state.Radial.Count, 3);
    TestEqual(TEXT("...on its object"), state.Radial.Object.Index, 5);
    TestEqual(TEXT("...with its actions"), state.Actions.Num, 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialSkippedOpenTest, "SOLTest.HoloRadial.SkippedFramesWhileOpen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// HUD frames skipped while the same menu stays open change nothing: it stays Open with its highlight and asks for no
// cue (no open cue without a new press, SDD 5; no hover cue without a sector change, SDD 6.2 ShouldPlayHoverCue)
bool FSOLHoloRadialSkippedOpenTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const double openS = HOLORADIALTEST_T0 + 2.0 * OPEN_S;
    const FSOLActionList actions = HoloRadialTestActions(5);
    FSOLHoloRadialState state = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 2), actions, 1, 0, true, true, openS + 0.5,
        HOLORADIALTEST_S1);
    const FSOLHoloRadialCues cues = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 2), actions, 1, 0, true,
        false, openS + 5.0, HOLORADIALTEST_S1);
    TestTrue(TEXT("no cue after skipped frames"), HoloRadialTestNoCues(cues));
    TestTrue(TEXT("stays Open"), state.Anim.Phase == ESOLHoloMenuPhase::Open);
    TestEqual(TEXT("keeps its hovered sector"), state.Anim.HoveredItem, 2);
    TestEqual(TEXT("whole"), SOLHoloMenu::Evaluate(state.Anim, openS + 5.0, HOLORADIALTEST_S1).FrameFraction, 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialHiddenShownTest, "SOLTest.HoloRadial.HiddenThenShown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The open cue belongs to the press (SDD 5), not to the menu becoming visible: a menu opened while the radial cannot
// show asks for the open cue at the press, then draws in from Closed when it shows, with no second open cue
bool FSOLHoloRadialHiddenShownTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const FSOLActionList actions = HoloRadialTestActions(5);
    FSOLHoloRadialState state = HoloRadialTestSynced();
    const FSOLHoloRadialCues pressed = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, INDEX_NONE), actions, 1,
        0, false, true, HOLORADIALTEST_T0, HOLORADIALTEST_S1);
    TestTrue(TEXT("open cue at the press, hidden or not"), pressed.bOpen);
    TestTrue(TEXT("nothing drawn while hidden"), state.Anim.Phase == ESOLHoloMenuPhase::Closed);

    const double shownS = HOLORADIALTEST_T0 + 0.5;
    const FSOLHoloRadialCues shown = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, INDEX_NONE), actions, 1, 0,
        true, true, shownS, HOLORADIALTEST_S1);
    TestTrue(TEXT("no cue when it shows"), HoloRadialTestNoCues(shown));
    TestTrue(TEXT("draws in"), state.Anim.Phase == ESOLHoloMenuPhase::Opening);
    TestEqual(TEXT("from 0"), SOLHoloMenu::Progress(state.Anim, shownS, HOLORADIALTEST_S1), 0.0f);
    TestEqual(TEXT("the menu is cached when it shows"), state.Radial.Count, 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialFlatPressHoverTest, "SOLTest.HoloRadial.StrengthZeroPressNoHoverCue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// At strength 0 the menu is Open on the opening update (D18), yet a sector highlighted on that press asks for the
// open cue only (no hover cue for the highlight a menu opens with, SDD 6.2; D21); a later change is a hover
bool FSOLHoloRadialFlatPressHoverTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const FSOLActionList actions = HoloRadialTestActions(5);
    FSOLHoloRadialState state = HoloRadialTestSynced();
    const FSOLHoloRadialCues pressed = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 1), actions, 1, 0, true,
        true, HOLORADIALTEST_T0, 0.0f);
    TestTrue(TEXT("open cue"), pressed.bOpen);
    TestFalse(TEXT("no hover cue on the press"), pressed.bHover);
    TestTrue(TEXT("Open at once"), state.Anim.Phase == ESOLHoloMenuPhase::Open);
    TestEqual(TEXT("the press highlight is hovered"), state.Anim.HoveredItem, 1);

    const FSOLHoloRadialCues moved = SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 3), actions, 1, 0, true,
        true, HOLORADIALTEST_T0 + 1.0, 0.0f);
    TestTrue(TEXT("a later sector change is a hover"), moved.bHover);
    TestFalse(TEXT("no second open cue"), moved.bOpen);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialCloseGlowTest, "SOLTest.HoloRadial.GlowKeptThroughClose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// SDD 5: the close plays backwards from the last drawn state, so the confirmed sector's hover glow does not drop in
// one frame: right at the close it is unchanged, then it fades with the fill to today's highlight
bool FSOLHoloRadialCloseGlowTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const double openS = HOLORADIALTEST_T0 + 2.0 * OPEN_S;
    const FSOLActionList actions = HoloRadialTestActions(5);
    FSOLHoloRadialState state = HoloRadialTestOpened(openS, HOLORADIALTEST_S1);
    SOLHoloRadial::Update(state, HoloRadialTestOpenMenu(5, 2), actions, 1, 0, true, true, openS + 0.1,
        HOLORADIALTEST_S1);
    const double closeS = openS + 1.0;
    const float rimOpen = SOLHoloRadial::SectorLook(HOLORADIALTEST_HALF_APERTURE, true, true,
        SOLHoloMenu::Evaluate(state.Anim, closeS, HOLORADIALTEST_S1), HOLORADIALTEST_S1).EdgeIntensity;
    TestTrue(TEXT("glowing while Open"), rimOpen > SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY);

    SOLHoloRadial::Update(state, FSOLRadialMenuState(), FSOLActionList(), 1, 1, true, true, closeS, HOLORADIALTEST_S1);
    TestTrue(TEXT("Closing"), state.Anim.Phase == ESOLHoloMenuPhase::Closing);
    TestEqual(TEXT("the confirmed sector stays hovered"), state.Anim.HoveredItem, 2);
    const float rimClosing = SOLHoloRadial::SectorLook(HOLORADIALTEST_HALF_APERTURE, true, true,
        SOLHoloMenu::Evaluate(state.Anim, closeS, HOLORADIALTEST_S1), HOLORADIALTEST_S1).EdgeIntensity;
    TestEqual(TEXT("no drop on the close frame"), rimClosing, rimOpen, HOLORADIALTEST_TOLERANCE);

    const double endS = closeS + CLOSE_S - 1.0e-3;
    const FSOLHoloMenuLook late = SOLHoloMenu::Evaluate(state.Anim, endS, HOLORADIALTEST_S1);
    const float rimLate = SOLHoloRadial::SectorLook(HOLORADIALTEST_HALF_APERTURE, true, true, late, HOLORADIALTEST_S1)
        .EdgeIntensity;
    TestTrue(TEXT("the glow is still there late in the close"), late.HoverIntensity > 0.0f);
    TestTrue(TEXT("...faded with the fill"), late.FillAlpha < 1.0f && rimLate < rimOpen
        && rimLate >= SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY);
    TestEqual(TEXT("...as the glow times the fill"), rimLate,
        SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY + late.HoverIntensity * late.FillAlpha, HOLORADIALTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloRadialLabelTest, "SOLTest.HoloRadial.LabelColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Labels keep today's colours with their alpha faded by the fill (clamped; NaN reads 0)
bool FSOLHoloRadialLabelTest::RunTest(const FString& /*parameters*/)
{
    const FLinearColor highlighted = SOLHoloRadial::LabelColor(true, false, 0.5f);
    TestEqual(TEXT("highlighted: Secondary"), highlighted.R, SOL::HudPalette::SECONDARY.R);
    TestEqual(TEXT("highlighted alpha"), highlighted.A, SOL::HudPalette::SECONDARY.A * 0.5f,
        HOLORADIALTEST_TOLERANCE);
    const FLinearColor enabled = SOLHoloRadial::LabelColor(false, true, 1.0f);
    TestTrue(TEXT("enabled: Primary"), enabled == SOL::HudPalette::PRIMARY);
    const FLinearColor disabled = SOLHoloRadial::LabelColor(false, false, 1.0f);
    const FLinearColor dim = SOL::HudPalette::PRIMARY * SOL::Interact::DISABLED_TEXT_SCALE;
    TestTrue(TEXT("disabled: dim Primary as before"), disabled.Equals(dim, HOLORADIALTEST_TOLERANCE));
    TestEqual(TEXT("fill above 1 reads 1"), SOLHoloRadial::LabelColor(false, true, 4.0f).A,
        SOL::HudPalette::PRIMARY.A);
    TestEqual(TEXT("fill 0: invisible"), SOLHoloRadial::LabelColor(false, true, 0.0f).A, 0.0f);
    TestEqual(TEXT("fill NaN: invisible"), SOLHoloRadial::LabelColor(false, true,
        std::numeric_limits<float>::quiet_NaN()).A, 0.0f);

    // The title and the reason fade the same way (SDD 5: they fade up with FillAlpha)
    const FLinearColor title = SOLHoloRadial::Faded(SOL::HudPalette::PRIMARY, 0.5f);
    TestEqual(TEXT("title alpha"), title.A, SOL::HudPalette::PRIMARY.A * 0.5f, HOLORADIALTEST_TOLERANCE);
    TestEqual(TEXT("title colour kept"), title.G, SOL::HudPalette::PRIMARY.G);
    TestEqual(TEXT("faded: above 1 reads 1"), SOLHoloRadial::Faded(SOL::HudPalette::SECONDARY, 3.0f).A,
        SOL::HudPalette::SECONDARY.A);
    TestEqual(TEXT("faded: below 0 reads 0"), SOLHoloRadial::Faded(SOL::HudPalette::SECONDARY, -1.0f).A, 0.0f);
    TestEqual(TEXT("faded: NaN reads 0"), SOLHoloRadial::Faded(SOL::HudPalette::SECONDARY,
        std::numeric_limits<float>::quiet_NaN()).A, 0.0f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
