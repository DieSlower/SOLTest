/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuSmoke.h"

#include "Menu/SOLBuildVersion.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyCapRow.h"
#include "Menu/SOLKeyDiagram.h"
#include "Menu/SOLKeyboardDiagram.h"
#include "Menu/SOLKeyboardDiagramSmokePage.h"
#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLDemoPageWidget.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLPageTabs.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLPauseMenuWidget.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Menu/SOLSettingsTabs.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSmokeBoard.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidScroll.h"
#include "Menu/SOLSolidSlider.h"
#include "Menu/SOLSolidStyle.h"
#include "Menu/SOLSolidTabRail.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLHoloMenuSubsystem.h"
#include "UI/SOLOverlayMenuWidget.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Layout/WidgetPath.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    // The script's steps, in order
    enum class ESolidSmokeStep : int32
    {
        Setup,          // The main menu shows; the kit's assets, the column, the version line and the first open
        View,           // r.SetRes 1280x720 (windowed), until the view has that size
        MainOpen,       // The column's show animation replayed: Opening, the power-up, Open
        Hover,          // A mouse-style hover on Demo: its settled hover look, one highlight, its hover trace
        Down,           // Down: the highlight and the trace move to the next entry
        CueClick,       // 46g (Q11): Enter held a few frames on the focused entry, twice (its page opens, then
                        // closes): a select cue per click, one open cue (the shell from closed), no hover cue
        PageOpen,       // A page opened by its entry's click handler: the shell opens (looped per page)
        PageClose,      // ... closed by Back or Esc, alternately: input back at once, the shell closes after
        PageReverse,    // A reopen during a close reverses it without a pop
        PlayReturn,     // Play focused and settled with About open, Play, Esc, Quit to menu: the shell Closed at once
                        // on the first frame back, Play's hover trace drawn on the rebuilt trace layer
        SettingsOpen,   // Settings from the main menu (SDD 14 step 4): on Graphics, six Tab buttons, solid rows, footer
        SettingsTab,    // Each other tab clicked in turn (looped): its content shown, its hover trace, the footer kept
        SettingsRowHover,   // A stepper hovered: the amber edge and underline, no trace on the tab rail (Q15)
        SettingsControls,   // A slider, a toggle and a stepper changed (dirty), then Revert (clean, values back)
        SettingsSliderFill, // 46e follow-up: the cyan fill at 0%, 100% and 50% and during a real mouse drag
        SettingsHoverWins,  // 46e follow-up: the mouse over a slider or an Overlay row drops the focused tab's amber
        SettingsScroll, // From Graphics, ScrollToBinding: the Key bindings tab at once, the row in view after
        SettingsReopen, // Esc, then Settings again: Graphics (no last-tab memory, Q8), every tab at its top
        SettingsClose,  // Esc: the page closed for the steps after
        MainStrengthZero,   // Strength 0: the column's open and the shell instant, the hover look at once
        MainStrengthTwo,    // Strength 2: the hovered entry's glow doubled
        BoardCreate,    // Strength 1, the page closed; the comparison board added over the main menu
        Board,          // Its selected tab hovered
        BoardSplit,     // Another tab hovered: hover-only beside selected-only
        StrengthZero,   // Strength 0: the hover look at once, no glow
        StrengthZeroShot,
        StrengthTwo,    // Strength 2: the glow doubled
        BoardRemove,    // Strength back to 1, the board removed
        ViewSmall,      // r.SetRes 1000x800 (windowed)
        SmallSettings,  // Settings open at 1000x800: the column and the shell inside the view, every stepper value
                        // inside its slot (46e follow-up; the longest window mode shown for the screenshot)
        SmallGameplay,  // 46i: the pending window mode reverted, the Gameplay tab: its narrow rows, its sliders' room,
                        // a shot
        SmallKeys,      // 46i: the Key bindings tab: its narrow rows, each key name beside a narrow Rebind, a shot
        SmallStart,     // Start location open at 1000x800
        SmallControls,  // 46f: Controls open at 1000x800: tabs, the rows and columns inside the shell
        SmallControlsRows,  // 46i: every Controls tab shown in turn: its keys and actions inside their slots and the
                        // shell
        SmallDemo,      // 46f: Demo open at 1000x800: the groups, labels and values inside their slots
        SmallAbout,     // About open at 1000x800: the column and the shell inside the view, the rows and rail too
        ViewBack,       // 46g: the About page closed, r.SetRes 1280x720 again
        MainFit,        // 46g: Controls, About and Settings opened in turn (looped), read once the shell has
                        // settled: the read-only pages' fit at 1280x720, every rail laid out without a scroll (D17)
        Diagram,        // 33c: the keyboard diagram test page in the 1280x720 shell content width: fit (D34: the
                        // clusters and mouse stacked under the main block), paint, assignment count, the hover keys
                        // (the panel fixed, D36), Fire rebound to K and back (live rebuild)
        MainScroll,     // 46g (D17): the column capped at 4 entries scrolls; Down to Quit scrolls it whole into
                        // view with its hover trace at the scrolled y; scrolled back, the out-of-view Quit draws no
                        // trace; the cap back at 7: no scroll, offset 0
        RailScroll,     // 46g (D17): a Controls page put on the viewport 300 units tall: its 10-tab rail scrolls,
                        // ScrollToEnd shows the last tab, the keyboard focus on a hidden tab scrolls to it
        PauseFlight,    // 46g: Play, the flight settles
        PauseOpen,      // 46g (SDD 14 step 5): Esc in flight: the side panel slides in from the left, the dim rises,
                        // Resume focused, the 7-tab stack without a scroll
        PausePage,      // ... a page tab clicked (looped per page): the page in the open shell, its fit and rail
        PausePageRows,  // ... 46i: on Controls and Info every tab with a left column shown in turn: its rows inside
                        // their slots and the shell at 1280x720 (Info's tabs screenshotted)
        PausePageClose, // ... closed by Esc or Back, alternately: still paused on Resume at once, the shell
                        // closes after
        PauseQuitTab,   // ... the Quit tab: its two choices in the side panel, no page
        PauseResume,    // ... Esc with no page: Playing with the menu input-transparent and the input back at once,
                        // slid out and gone after CLOSE_S; the sim time advanced while paused (step 7, Q3)
        PauseReverse,   // ... paused again, Esc, and a re-pause mid slide-out reverses it (no pop)
        PauseStrengthZero,  // ... strength 0 (applied to the smoke slot): the resume, the pause and a page open
                        // and close at once; strength 1 applied back
        PauseStrengthTwo,   // ... paused, strength 2 applied with the Settings tab open: the panel and shell shown,
                        // the tab's glow read; strength 1 applied back
        PauseKeys,      // ... 33b: the Key bindings tab at 1280x720: every key cap cell inside its row beside its
                        // Rebind button, a shot (PauseSmall shows Graphics again)
        PauseSmall,     // ... paused with Settings open, r.SetRes 1000x800: the side panel and the shell inside the
                        // view; 46i: every stepper value inside its slot and the shell, the labels in their columns
        PauseSmallKeys, // ... 46i: the Key bindings tab: its narrow rows, each key name beside a narrow Rebind, a shot
        PauseSmallControls, // ... 46i: the Controls tab: every Controls tab's rows inside their slots and the shell
        PauseSmallInfo, // ... 46i: the Info tab: every Info tab with labels: its rows inside their slots and the shell
        Quit,         // SUMMARY, quit
        Count,
    };

    // The main menu pages the page loop opens and closes, in order: the page, its entry, its screenshot name, and
    // whether Back (else Esc) closes it
    struct FSolidSmokePage
    {
        ESOLMainMenuPage Page;
        int32 Entry;
        const TCHAR* Name;
        bool bBack;
    };
    const FSolidSmokePage SOLID_SMOKE_PAGES[] = {
        { ESOLMainMenuPage::StartLocation, 2, TEXT("start"), true },
        { ESOLMainMenuPage::Controls, 3, TEXT("controls"), false },
        { ESOLMainMenuPage::Settings, 4, TEXT("settings"), true },
        { ESOLMainMenuPage::About, 5, TEXT("about"), false },
        { ESOLMainMenuPage::Demo, 1, TEXT("demo"), true },
    };
    constexpr int32 SOLID_SMOKE_PAGE_COUNT = UE_ARRAY_COUNT(SOLID_SMOKE_PAGES);

    // The main menu pages the 1280x720 fit loop opens (46g), in order: the page, its entry and its check id / shot name
    struct FSolidSmokeFitPage
    {
        ESOLMainMenuPage Page;
        int32 Entry;
        const TCHAR* Name;
    };
    const FSolidSmokeFitPage SOLID_SMOKE_FIT_PAGES[] = {
        { ESOLMainMenuPage::Controls, 3, TEXT("controls") },
        { ESOLMainMenuPage::About, 5, TEXT("about") },
        { ESOLMainMenuPage::Settings, 4, TEXT("settings") },
    };
    constexpr int32 SOLID_SMOKE_FIT_PAGE_COUNT = UE_ARRAY_COUNT(SOLID_SMOKE_FIT_PAGES);

    // The pause menu page tabs the pause page loop opens and closes (46g), in order: the tab, its check id / shot name,
    // and whether Back (else Esc) closes it
    struct FSolidSmokePausePage
    {
        SOLMenuState::EMenuTab Tab;
        const TCHAR* Name;
        bool bBack;
    };
    const FSolidSmokePausePage SOLID_SMOKE_PAUSE_PAGES[] = {
        { SOLMenuState::EMenuTab::Controls, TEXT("controls"), false },
        { SOLMenuState::EMenuTab::Info, TEXT("info"), true },
        { SOLMenuState::EMenuTab::Settings, TEXT("settings"), false },
        { SOLMenuState::EMenuTab::Demo, TEXT("demo"), true },
        { SOLMenuState::EMenuTab::About, TEXT("about"), false },
    };
    constexpr int32 SOLID_SMOKE_PAUSE_PAGE_COUNT = UE_ARRAY_COUNT(SOLID_SMOKE_PAUSE_PAGES);

    constexpr int32 SOLID_SMOKE_MIN_FRAMES = 5;                 // Every step lasts at least this many frames
    constexpr int32 SOLID_SMOKE_SHOT_FRAMES = 10;               // A screenshot's frames before the next action
    constexpr int32 SOLID_SMOKE_INSTANT_FRAME = 2;              // Strength 0: the look is read on this frame
    constexpr int32 SOLID_SMOKE_VIEW_STABLE_FRAMES = 10;
    constexpr int32 SOLID_SMOKE_CURSOR_INSET_PX = 8;            // The parked cursor, this far in from the top right
    constexpr int32 SOLID_SMOKE_CURSOR_RETRIES = 3;             // 46i: a real-cursor stage is retried this often when
                                                                // the hover or capture did not take (window in front)
    constexpr double SOLID_SMOKE_CURSOR_MATCH_PX = 1.5;         // ... the OS cursor counts as at its point this close
    constexpr int32 SOLID_SMOKE_BOARD_Z = SOL::MenuStyle::Z_ORDER + 10;
    constexpr double SOLID_SMOKE_SETUP_S = 3.0;                 // The main menu's first frames (and shader loads)
    constexpr double SOLID_SMOKE_SETTLE_S = 0.8;                // Longer than every hover ramp and press linger
    constexpr double SOLID_SMOKE_VIEW_TIMEOUT_S = 8.0;
    constexpr double SOLID_SMOKE_WATCHDOG_S = 270.0;
    constexpr int32 SOLID_SMOKE_EXPECTED_CHECKS = 212;          // 33c: the checks a whole run logs (Finish fails a run
                                                                // that logged another number: a skipped check)
    constexpr double SOLID_SMOKE_REVERSE_OPEN_S = 0.5;          // The reversal: open this long before the close
    constexpr double SOLID_SMOKE_FLIGHT_S = 1.0;                // Return from flight: this long in flight, then Esc
    constexpr double SOLID_SMOKE_PAUSE_S = 0.5;                 // ... this long paused, then Quit to menu
    constexpr float SOLID_SMOKE_TOLERANCE = 1.0e-3f;
    constexpr float SOLID_SMOKE_REVERSAL_TOLERANCE = 0.05f;     // Two reads a few ms apart at the CLOSE_S rate
    constexpr float SOLID_SMOKE_FIT_TOLERANCE_PX = 0.5f;
    const FIntPoint SOLID_SMOKE_VIEW(1280, 720);
    const FIntPoint SOLID_SMOKE_SMALL_VIEW(1000, 800);

    // Main menu entry indices the script drives (USOLMainMenuWidget's EEntry order: Play, Demo, Start, ...)
    constexpr int32 SOLID_SMOKE_PLAY_ENTRY = 0;
    constexpr int32 SOLID_SMOKE_HOVER_ENTRY = 1;
    constexpr int32 SOLID_SMOKE_DOWN_ENTRY = 2;
    constexpr int32 SOLID_SMOKE_STRENGTH_ENTRY = 3;             // Hovered at strength 0 and 2
    constexpr int32 SOLID_SMOKE_SETTINGS_ENTRY = 4;
    constexpr int32 SOLID_SMOKE_CONTROLS_ENTRY = 3;
    constexpr int32 SOLID_SMOKE_ABOUT_ENTRY = 5;
    constexpr int32 SOLID_SMOKE_COLUMN_TRACES = 3;              // The column's lower-edge traces
    constexpr int32 SOLID_SMOKE_OVERLAY_HOVER_ROW = 1;          // The embedded Overlay list's row hovered for the shot
    constexpr float SOLID_SMOKE_SLIDER_DELTA = 0.25f;           // The Settings slider moves this far (then Revert)
    constexpr float SOLID_SMOKE_FILL_TOLERANCE = 0.5f;          // A painted fill's width against its value (units)
    constexpr float SOLID_SMOKE_FIT_TOLERANCE = 0.5f;           // A measured text against its slot (units)
    constexpr float SOLID_SMOKE_DRAG_FROM = 0.25f;              // The drag: pressed this far along the bar...
    constexpr float SOLID_SMOKE_DRAG_TO = 0.75f;                // ... and moved to this far
    constexpr float SOLID_SMOKE_DRAG_TOLERANCE = 0.03f;         // The dragged value against the drag's end
    constexpr int32 SOLID_SMOKE_PAINT_FRAMES = 3;               // Frames for a value change to be painted
    constexpr int32 SOLID_SMOKE_SCROLL_CAP = 4;                 // 46g: the column's forced visible-entry cap
    constexpr int32 SOLID_SMOKE_QUIT_ENTRY = 6;                 // 46g: the column's last entry (Quit)
    constexpr float SOLID_SMOKE_SCROLL_TOLERANCE = 0.5f;        // 46g: two offsets or a drawn y against a laid-out one
    constexpr float SOLID_SMOKE_SCROLL_END_TOLERANCE = 1.5f;    // 46g: Slate's scroll end against the stack's
    constexpr float SOLID_SMOKE_PAUSE_REVERSAL_TOLERANCE = 0.15f;   // 46g: a few frames at the CLOSE_S / open rates
    constexpr float SOLID_SMOKE_REVERSE_AT = 0.75f;             // 46g: the re-pause once the slide-out is below this
    constexpr double SOLID_SMOKE_FLIGHT_SETTLE_S = 2.0;         // 46g: flight after Play (the ship spawn and the HUD)
    constexpr double SOLID_SMOKE_CLOSE_SLACK_S = 0.25;          // 46g: the slide-out's end read after CLOSE_S plus this
    const FVector2D SOLID_SMOKE_RAIL_PAGE_SIZE(900.0, 300.0);   // 46g: the viewport Controls page (units)

    // 33c: the diagram keys the real cursor hovers, in order: the busiest default keys (their strip must fit), then the
    // six modifier keys (D32 revised: each starts chords; the right ones have no action of their own)
    struct FSolidSmokeDiagramHover
    {
        const TCHAR* KeyName;
        const TCHAR* Name;          // Check id / shot name part
        bool bModifier;             // A chord modifier key (bModifier, own actions then chords)
        bool bRightSide;            // Right Ctrl / Alt / Shift: no action of its own at the defaults
    };
    const FSolidSmokeDiagramHover SOLID_SMOKE_DIAGRAM_HOVERS[] = {
        { TEXT("O"), TEXT("o"), false, false },
        { TEXT("LeftControl"), TEXT("lctrl"), true, false },
        { TEXT("LeftMouseButton"), TEXT("lmb"), false, false },
        { TEXT("LeftShift"), TEXT("lshift"), true, false },
        { TEXT("RightShift"), TEXT("rshift"), true, true },
        { TEXT("RightControl"), TEXT("rctrl"), true, true },
        { TEXT("LeftAlt"), TEXT("lalt"), true, false },
        { TEXT("RightAlt"), TEXT("ralt"), true, true },
    };
    constexpr int32 SOLID_SMOKE_DIAGRAM_HOVER_COUNT = UE_ARRAY_COUNT(SOLID_SMOKE_DIAGRAM_HOVERS);
    // The hovers shot (bits of SOLID_SMOKE_DIAGRAM_HOVERS): O, Left Ctrl, Right Shift
    constexpr uint32 SOLID_SMOKE_DIAGRAM_SHOTS = (1u << 0) | (1u << 1) | (1u << 4);
    constexpr float SOLID_SMOKE_DIAGRAM_TOLERANCE = 0.5f;       // A key rect or width against the content width
    const TCHAR* const SOLID_SMOKE_DIAGRAM_REBIND_KEY = TEXT("K");  // Fire is rebound to this free key and back

    // Screenshot names of the Settings tabs, in ESOLSettingsTab order
    const TCHAR* const SOLID_SMOKE_TAB_NAMES[] = { TEXT("graphics"), TEXT("audio"), TEXT("mouse"), TEXT("gameplay"),
        TEXT("overlays"), TEXT("keys") };
    static_assert(UE_ARRAY_COUNT(SOLID_SMOKE_TAB_NAMES) == static_cast<int32>(ESOLSettingsTab::Count),
        "one name per Settings tab");

    static_assert(SOLID_SMOKE_SETTLE_S > SOL::SolidMenu::HOVER_RAMP_S + SOL::SolidMenu::PRESSED_LINGER_S,
        "the settle must outlast the hover ramp and the press linger");

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two looks match within the tolerance (amounts and flags)
    bool SolidSmokeLookMatches(const FSOLSolidButtonLook& a, const FSOLSolidButtonLook& b)
    {
        return FMath::IsNearlyEqual(a.Lift, b.Lift, SOLID_SMOKE_TOLERANCE)
            && FMath::IsNearlyEqual(a.Edge, b.Edge, SOLID_SMOKE_TOLERANCE)
            && FMath::IsNearlyEqual(a.Led, b.Led, SOLID_SMOKE_TOLERANCE)
            && FMath::IsNearlyEqual(a.Underline, b.Underline, SOLID_SMOKE_TOLERANCE)
            && FMath::IsNearlyEqual(a.Trace, b.Trace, SOLID_SMOKE_TOLERANCE)
            && FMath::IsNearlyEqual(a.Glow, b.Glow, SOLID_SMOKE_TOLERANCE) && a.bPressedHull == b.bPressedHull
            && a.bSelectedMark == b.bSelectedMark && a.bDisabled == b.bDisabled;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a look as text for the log
    FString SolidSmokeDescribe(const FSOLSolidButtonLook& look)
    {
        return FString::Printf(TEXT("lift %.3f edge %.3f trace %.3f glow %.3f pressed %d mark %d disabled %d"),
            look.Lift, look.Edge, look.Trace, look.Glow, look.bPressedHull ? 1 : 0, look.bSelectedMark ? 1 : 0,
            look.bDisabled ? 1 : 0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the settled look of a state at a strength (what a refreshed button must show after the ramp)
    FSOLSolidButtonLook SolidSmokeSettled(const ESOLSolidButtonState state, const float s, const bool bSelected)
    {
        return SOLSolidMenu::EvaluateButton(state, ESOLSolidButtonState::Normal, SOLID_SMOKE_SETTLE_S, s, bSelected);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many of the main menu's entries show a lit (hover) look, and the last such index
    int32 SolidSmokeLitEntries(const USOLMainMenuWidget& mainMenu, int32& outIndex)
    {
        int32 count = 0;
        outIndex = INDEX_NONE;
        for (int32 index = 0; index < mainMenu.GetEntryButtonCount(); ++index)
        {
            const USOLMenuButton* button = mainMenu.GetEntryButton(index);
            if (button != nullptr && button->GetAppliedLook().Edge > 0.5f)
            {
                ++count;
                outIndex = index;
            }
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a version has the shape 0.1.DDDD.D+ (the prefix, four YYMM digits, a dot, the commit count)
    bool SolidSmokeIsVersionShape(const FString& version)
    {
        const FString prefix(TEXT("0.1."));
        const int32 yymmEnd = prefix.Len() + 4;
        if (!version.StartsWith(prefix, ESearchCase::CaseSensitive) || version.Len() <= yymmEnd + 1
            || version[yymmEnd] != TEXT('.'))
        {
            return false;
        }
        for (int32 index = prefix.Len(); index < version.Len(); ++index)
        {
            if (index != yymmEnd && !FChar::IsDigit(version[index]))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // (33c) Returns whether two diagram panels lay out the same (every section box, the scale, the strip's lines)
    bool SolidSmokeSamePanel(const FSOLDiagramPanel& a, const FSOLDiagramPanel& b)
    {
        return a.Scale.UnitPx == b.Scale.UnitPx && a.Scale.Placement == b.Scale.Placement
            && a.Scale.OriginPx == b.Scale.OriginPx && a.Legend == b.Legend && a.Diagram == b.Diagram
            && a.Other == b.Other && a.Unbound == b.Unbound && a.Detail == b.Detail && a.DetailLines == b.DetailLines
            && a.Height == b.Height;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a widget's arranged rect lies inside another's (absolute space), with a description
    bool SolidSmokeInside(const UWidget* inner, const UWidget* outer, FString& outDetail)
    {
        if (inner == nullptr || outer == nullptr)
        {
            outDetail = TEXT("missing widget");
            return false;
        }
        const FGeometry& innerGeometry = inner->GetCachedGeometry();
        const FGeometry& outerGeometry = outer->GetCachedGeometry();
        const FVector2D innerMin = innerGeometry.GetAbsolutePosition();
        const FVector2D innerMax = innerMin + innerGeometry.GetAbsoluteSize();
        const FVector2D outerMin = outerGeometry.GetAbsolutePosition();
        const FVector2D outerMax = outerMin + outerGeometry.GetAbsoluteSize();
        outDetail = FString::Printf(TEXT("[%.0f, %.0f]-[%.0f, %.0f] in [%.0f, %.0f]-[%.0f, %.0f]"), innerMin.X,
            innerMin.Y, innerMax.X, innerMax.Y, outerMin.X, outerMin.Y, outerMax.X, outerMax.Y);
        const double tol = SOLID_SMOKE_FIT_TOLERANCE_PX;
        return innerGeometry.GetAbsoluteSize().X > 0.0 && innerMin.X >= outerMin.X - tol
            && innerMin.Y >= outerMin.Y - tol && innerMax.X <= outerMax.X + tol && innerMax.Y <= outerMax.Y + tol;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a widget's geometry freshly arranged down from a painted ancestor (its cached geometry current), so a
    // widget culled from the paint (scrolled out of view) still reads its current layout (Slate's FindPathToWidget
    // reads the cached geometry, so it would not); the widget's cached geometry when it is not under the ancestor
    FGeometry SolidSmokeArrangedGeometry(const UWidget* widget, const UWidget* ancestor)
    {
        const TSharedPtr<SWidget> slate = widget != nullptr ? widget->GetCachedWidget() : nullptr;
        const TSharedPtr<SWidget> from = ancestor != nullptr ? ancestor->GetCachedWidget() : nullptr;
        if (slate.IsValid() && from.IsValid())
        {
            FWidgetPath search;
            const FArrangedChildren found = search.GeneratePathToWidget(FWidgetMatcher(slate.ToSharedRef()),
                FArrangedWidget(from.ToSharedRef(), from->GetCachedGeometry()), EUINavigation::Next, EVisibility::All);
            if (found.Num() > 0)
            {
                return found[found.Num() - 1].Geometry;
            }
        }
        return widget != nullptr ? widget->GetCachedGeometry() : FGeometry();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the LEDs (bit per entry) the power-up blink may light: the entries whose own LED is dark
    uint32 SolidSmokeBlinkableLeds(const USOLMainMenuWidget& mainMenu)
    {
        uint32 mask = 0;
        for (int32 index = 0; index < mainMenu.GetEntryButtonCount() && index < SSOLSolidTraceLayer::MAX_LEDS; ++index)
        {
            const USOLMenuButton* button = mainMenu.GetEntryButton(index);
            const FSOLSolidButtonLook look = button != nullptr ? button->GetAppliedLook() : FSOLSolidButtonLook();
            if (button != nullptr && look.Led <= 0.0f && !look.bSelectedMark && !look.bDisabled)
            {
                mask |= 1u << index;
            }
        }
        return mask;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many times a #42 cue's sound started in the menu's world (-1 without the hologram menu subsystem)
    int32 SolidSmokeCues(const USOLMenuSubsystem& menu, const ESOLHoloCue cue)
    {
        const UWorld* world = menu.GetWorld();
        const USOLHoloMenuSubsystem* holo = world != nullptr ? world->GetSubsystem<USOLHoloMenuSubsystem>() : nullptr;
        return holo != nullptr ? holo->GetCuePlayedCount(cue) : -1;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the main menu column's trace layer, or null
    TSharedPtr<SSOLSolidTraceLayer> SolidSmokeColumnLayer(const USOLMainMenuWidget* mainMenu)
    {
        const USOLSolidPanel* column = mainMenu != nullptr ? mainMenu->GetColumnPanel() : nullptr;
        return column != nullptr ? column->GetTraceLayer() : nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Sends a key press and release through Slate (to the focused widget, as a keyboard would)
    void SolidSmokeKey(const FKey& key)
    {
        if (FSlateApplication::IsInitialized())
        {
            const FKeyEvent event(key, FModifierKeysState(), 0, false, 0, 0);
            FSlateApplication::Get().ProcessKeyDownEvent(event);
            FSlateApplication::Get().ProcessKeyUpEvent(event);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Clicks a button as the mouse does: hovers it (it takes keyboard focus), then its click handler
    void SolidSmokeClick(USOLMenuButton* button)
    {
        if (button != nullptr)
        {
            button->NotifyHovered();
            button->OnMenuClicked.ExecuteIfBound(button->GetIndex());
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the sim time in seconds since J2000, or -1 without a sim clock
    double SolidSmokeSimSeconds(const UWorld* world)
    {
        const USOLSimClockSubsystem* clock = world != nullptr ? world->GetSubsystem<USOLSimClockSubsystem>() : nullptr;
        return clock != nullptr ? clock->GetClock().GetSecondsSinceJ2000() : -1.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a mouse event at an absolute (desktop) point coming from another (the cursor delta a real move has), the
    // left button held when bPressed
    FPointerEvent SolidSmokeMouseEvent(const FVector2D& at, const FVector2D& from, const bool bPressed)
    {
        TSet<FKey> pressed;
        if (bPressed)
        {
            pressed.Add(EKeys::LeftMouseButton);
        }
        return FPointerEvent(FSlateApplication::Get().GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex,
            at, from, pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
    }

    //////////////////////////////////////////////////////////////////////////
    // Moves the OS cursor to an absolute point and sends Slate the mouse move there from the cursor's last place
    // (bPressed: the left button held), as a real mouse would (Slate's own moves at the cursor then agree)
    void SolidSmokeMouseMove(const FVector2D& at, const bool bPressed)
    {
        if (FSlateApplication::IsInitialized())
        {
            const FVector2D from = FSlateApplication::Get().GetCursorPos();
            FSlateApplication::Get().SetCursorPos(at);
            FSlateApplication::Get().ProcessMouseMoveEvent(SolidSmokeMouseEvent(at, from, bPressed));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Sends Slate a left press (bDown) or release at an absolute point
    void SolidSmokeMouseButton(const FVector2D& at, const bool bDown)
    {
        if (!FSlateApplication::IsInitialized())
        {
            return;
        }
        if (bDown)
        {
            FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, SolidSmokeMouseEvent(at, at, true));
        }
        else
        {
            FSlateApplication::Get().ProcessMouseButtonUpEvent(SolidSmokeMouseEvent(at, at, false));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Brings the game window to the front and activates it (46i): Slate's per-tick synthesized mouse moves find no
    // widget under the cursor while the application is inactive and another window covers the point, which drops a
    // hover the smoke set up (the real-cursor checks)
    void SolidSmokeBringGameToFront()
    {
        const TSharedPtr<SWindow> window = GEngine != nullptr && GEngine->GameViewport != nullptr
            ? GEngine->GameViewport->GetWindow() : nullptr;
        if (window.IsValid())
        {
            window->BringToFront(true);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Describes where the OS cursor is against a wanted point and whether the platform sees it directly over a Slate
    // window and the application active (46i: the real-cursor checks' diagnosis); returns whether all three hold
    bool SolidSmokeCursorState(const FVector2D& want, FString& outDetail)
    {
        if (!FSlateApplication::IsInitialized())
        {
            outDetail = TEXT("no Slate");
            return false;
        }
        FSlateApplication& slate = FSlateApplication::Get();
        const FVector2D at = slate.GetCursorPos();
        const bool bAt = FVector2D::Distance(at, want) <= SOLID_SMOKE_CURSOR_MATCH_PX;
        const bool bOver = slate.GetPlatformApplication()->IsCursorDirectlyOverSlateWindow();
        const bool bActive = slate.IsActive();
        outDetail = FString::Printf(TEXT("cursor %.0f,%.0f (want %.0f,%.0f), over a game window %d, active %d"), at.X,
            at.Y, want.X, want.Y, bOver ? 1 : 0, bActive ? 1 : 0);
        return bAt && bOver && bActive;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a slider's knob width (its normal thumb image) and its indent (the knob's width when indented, else 0)
    void SolidSmokeKnob(const USlider& slider, float& outKnob, float& outIndent)
    {
        outKnob = slider.GetWidgetStyle().NormalThumbImage.ImageSize.X;
        outIndent = slider.HasIndentHandle() ? outKnob : 0.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the absolute point a fraction along a slider's travel (the knob's centre at that value), on its centre
    // line
    FVector2D SolidSmokeSliderPoint(const USlider* slider, const float fraction)
    {
        if (slider == nullptr)
        {
            return FVector2D::ZeroVector;
        }
        float knob = 0.0f;
        float indent = 0.0f;
        SolidSmokeKnob(*slider, knob, indent);
        const FGeometry& geometry = slider->GetCachedGeometry();
        const FVector2f size = geometry.GetLocalSize();
        const float x = 0.5f * (knob + indent) + fraction * (size.X - knob - indent);
        return FVector2D(geometry.LocalToAbsolute(FVector2f(x, 0.5f * size.Y)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a slider's last painted fill matches its value: from the bar's start plus the inset to the
    // knob's centre (at most the bar's end less the inset), in the cyan fill brush; with a description
    bool SolidSmokeFillMatches(const USOLSolidSlider* slider, FString& outDetail)
    {
        using namespace SOL::SolidMenu;
        FSOLSliderFill fill;
        float barLength = 0.0f;
        if (slider == nullptr || !slider->GetPaintedFill(fill, barLength) || barLength <= 0.0f)
        {
            outDetail = TEXT("no painted fill");
            return false;
        }
        float knob = 0.0f;
        float indent = 0.0f;
        SolidSmokeKnob(*slider, knob, indent);
        const float barStart = 0.5f * knob;
        const float percent = FMath::Clamp(slider->GetNormalizedValue(), 0.0f, 1.0f);
        const float centre = barStart + 0.5f * indent + percent * (barLength - indent);
        const float end = FMath::Min(centre, barStart + barLength - SLIDER_FILL_INSET_PX);
        const float want = FMath::Max(end - (barStart + SLIDER_FILL_INSET_PX), 0.0f);
        const bool bCyan = slider->GetFillBrush().TintColor.GetSpecifiedColor().Equals(SOLSolidStyle::ValueColor(),
            SOLID_SMOKE_TOLERANCE);
        outDetail = FString::Printf(TEXT("value %.3f: fill %.1f wide from %.1f (want %.1f) on a %.1f bar, cyan %d"),
            percent, fill.Width, fill.StartX, want, barLength, bCyan ? 1 : 0);
        return bCyan && FMath::Abs(fill.Width - want) <= SOLID_SMOKE_FILL_TOLERANCE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a text's width in a font as drawn at a layout scale (glyphs measured at that scale, as STextBlock does),
    // in local units, or -1 without Slate's font measure or a usable scale
    float SolidSmokeTextWidth(const FString& text, const FSlateFontInfo& font, const float scale)
    {
        if (!FSlateApplication::IsInitialized() || !(scale > 0.0f))
        {
            return -1.0f;
        }
        const TSharedRef<FSlateFontMeasure> measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        return static_cast<float>(measure->Measure(text, font, scale).X) / scale;
    }

    //////////////////////////////////////////////////////////////////////////
    // (46i) Returns the widest word of a text as drawn in a font at a layout scale (local units), -1 if not measurable
    float SolidSmokeWidestWord(const FString& text, const FSlateFontInfo& font, const float scale)
    {
        TArray<FString> words;
        text.ParseIntoArray(words, TEXT(" "));
        float widest = words.IsEmpty() ? SolidSmokeTextWidth(text, font, scale) : 0.0f;
        for (const FString& word : words)
        {
            const float width = SolidSmokeTextWidth(word, font, scale);
            if (width < 0.0f)
            {
                return -1.0f;
            }
            widest = FMath::Max(widest, width);
        }
        return widest;
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and starts the first step
void FSOLSolidMenuSmoke::Start(USOLMenuSubsystem& menu)
{
    mController = menu.GetPlayerController();
    mLastTimeS = FPlatformTime::Seconds();
    UE_LOG(LogSOL, Log, TEXT("SolidMenu: scripted solid menu kit check starting (%d steps)"),
        static_cast<int32>(ESolidSmokeStep::Count));
    mStep = 0;
    EnterStep(mStep, menu);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLSolidMenuSmoke::Update(USOLMenuSubsystem& menu)
{
    if (mIsFinished || mStep >= static_cast<int32>(ESolidSmokeStep::Count))
    {
        return true;
    }
    const double nowS = FPlatformTime::Seconds();
    const double deltaS = nowS - mLastTimeS;
    mLastTimeS = nowS;
    mRunTimeS += deltaS;
    if (!mController.IsValid())
    {
        mController = menu.GetPlayerController();
    }
    if (mRunTimeS >= SOLID_SMOKE_WATCHDOG_S)
    {
        Check(TEXT("z-watchdog"), false, FString::Printf(TEXT("stuck at step %d after %.1f s"), mStep, mRunTimeS));
        Finish(menu);
        return true;
    }
    // After a screenshot the next step's action waits, so the shot shows the finished step
    if (mDeferFrames > 0)
    {
        if (--mDeferFrames == 0)
        {
            EnterStep(mStep, menu);
        }
        return false;
    }
    mStepTimeS += deltaS;
    ++mStepFrames;
    if (!TickStep(mStep, menu) || mStepFrames < SOLID_SMOKE_MIN_FRAMES)
    {
        return false;
    }
    mIsShotPending = false;
    ExitStep(mStep, menu);
    mStep = mRepeatStep != INDEX_NONE ? mRepeatStep : mStep + 1;
    mRepeatStep = INDEX_NONE;
    mStepTimeS = 0.0;
    mStepFrames = 0;
    if (mIsFinished || mStep >= static_cast<int32>(ESolidSmokeStep::Count))
    {
        return true;
    }
    if (mIsShotPending)
    {
        mDeferFrames = SOLID_SMOKE_SHOT_FRAMES;
        return false;
    }
    EnterStep(mStep, menu);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLSolidMenuSmoke::EnterStep(const int32 step, USOLMenuSubsystem& menu)
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLSolidMenuSmokeBoard* board = mBoard.Get();
    mSubStage = 0;
    mSubStageS = 0.0;
    mSubStageFrames = 0;
    mCursorRetries = 0;
    mSamples = 0;
    mFirstProgress = mFirstPower = mLastProgress = -1.0f;
    mIsMonotonic = true;
    mSawMidProgress = mSawMidPower = mSawHitTestInvisible = false;
    mSeenLeds = 0;
    mInstantLook = FSOLSolidButtonLook();
    mInstantTimeS = -1.0;
    mSweepTab = INDEX_NONE;
    mSweepFrame = mSweepShots = mSweepRows = 0;
    mIsSweepRead = false;
    mIsSweepOk = true;
    mSweepDetail.Reset();
    mCueOpenAtStart = SolidSmokeCues(menu, ESOLHoloCue::Open);
    mCueHoverAtStart = SolidSmokeCues(menu, ESOLHoloCue::Hover);
    mCueSelectAtStart = SolidSmokeCues(menu, ESOLHoloCue::Select);
    mCueOpenBeforeReopen = -1;
    // Hovers one board button as the mouse would (it takes keyboard focus)
    const auto hover = [board](const ESOLSolidSmokeButton which)
    {
        USOLMenuButton* button = board != nullptr ? board->GetButton(which) : nullptr;
        if (button != nullptr)
        {
            button->NotifyHovered();
        }
    };
    // Sets a windowed view size (the step waits until the view has it)
    const auto setView = [this](const FIntPoint& view)
    {
        mViewFrames = 0;
        if (mController.IsValid())
        {
            mController->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"), view.X, view.Y));
        }
    };
    switch (static_cast<ESolidSmokeStep>(step))
    {
    case ESolidSmokeStep::View:
        setView(SOLID_SMOKE_VIEW);
        break;
    case ESolidSmokeStep::MainOpen:
        mIsOpeningSample = true;
        if (mainMenu != nullptr)
        {
            mainMenu->PlayShowAnimation();
        }
        break;
    case ESolidSmokeStep::PageOpen:
        mIsOpeningSample = true;
        ClickEntry(menu, SOLID_SMOKE_PAGES[mPageIndex].Entry);
        break;
    case ESolidSmokeStep::PageClose:
    {
        // Back: hovered (it takes the focus), then Enter as a keyboard would; Esc: the key on the focused entry
        mIsOpeningSample = false;
        USOLMenuButton* back = mainMenu != nullptr ? mainMenu->GetBackButton() : nullptr;
        if (SOLID_SMOKE_PAGES[mPageIndex].bBack && back != nullptr)
        {
            back->NotifyHovered();
            SolidSmokeKey(EKeys::Enter);
        }
        else
        {
            SolidSmokeKey(EKeys::Escape);
        }
        const USOLMenuButton* play = mainMenu != nullptr ? mainMenu->GetEntryButton(SOLID_SMOKE_PLAY_ENTRY) : nullptr;
        mWasClosedAtOnce = mainMenu != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::None && play != nullptr
            && play->HasKeyboardFocus();
        break;
    }
    case ESolidSmokeStep::PageReverse:
        ClickEntry(menu, SOLID_SMOKE_ABOUT_ENTRY);
        mPreProgress = mPostProgress = -1.0f;
        break;
    case ESolidSmokeStep::PlayReturn:
        // About stays open; Play takes the focus (hover) and settles before it is clicked
        mIsReturnClosed = mIsReturnTraced = false;
        mReturnDetail = TEXT("never back");
        if (USOLMenuButton* play = mainMenu != nullptr ? mainMenu->GetEntryButton(SOLID_SMOKE_PLAY_ENTRY) : nullptr)
        {
            play->NotifyHovered();
        }
        break;
    case ESolidSmokeStep::SettingsOpen:
    {
        // The cursor parked in the top-right corner, off every Settings control, so only the script hovers
        const FIntPoint size = GetViewSize();
        if (mController.IsValid() && size.X > SOLID_SMOKE_CURSOR_INSET_PX)
        {
            mController->SetMouseLocation(size.X - SOLID_SMOKE_CURSOR_INSET_PX, SOLID_SMOKE_CURSOR_INSET_PX);
        }
        mSettingsTab = 0;
        ClickEntry(menu, SOLID_SMOKE_SETTINGS_ENTRY);
        break;
    }
    case ESolidSmokeStep::SettingsTab:
    {
        // The next tab, hovered (it takes the focus) and clicked as the mouse does; on Overlays one embedded row is
        // hovered too, for the screenshot (cleared on the next tab)
        USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
        ++mSettingsTab;
        ClickSettingsTab(page, static_cast<ESOLSettingsTab>(mSettingsTab));
        if (USOLOverlayMenuWidget* list = page != nullptr ? page->GetOverlayList() : nullptr)
        {
            list->SetEmbeddedHoverRow(static_cast<ESOLSettingsTab>(mSettingsTab) == ESOLSettingsTab::Overlays
                ? SOLID_SMOKE_OVERLAY_HOVER_ROW : INDEX_NONE);
        }
        break;
    }
    case ESolidSmokeStep::SettingsRowHover:
    {
        USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
        ClickSettingsTab(page, ESOLSettingsTab::Graphics);
        if (USOLMenuButton* next = page != nullptr ? page->GetStepperButton(ESOLSettingsStepper::Quality, 1) : nullptr)
        {
            next->NotifyHovered();
        }
        break;
    }
    case ESolidSmokeStep::SettingsSliderFill:
    {
        // The Audio tab; Master volume to 0 (read once painted, then 100% and 50%, then the drag: TickStep)
        USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
        ClickSettingsTab(page, ESOLSettingsTab::Audio);
        mFillDetail.Reset();
        mFillPassed = 0;
        mFillRun = 0;
        mDragDetail = TEXT("never dragged");
        mIsDragOk = false;
        if (page != nullptr)
        {
            page->SetSliderValue(ESOLSettingsSlider::MasterVolume, 0.0f);
        }
        break;
    }
    case ESolidSmokeStep::SettingsHoverWins:
    {
        // The Audio tab clicked (its button keeps the focus), then the real cursor over Master volume (TickStep)
        USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
        ClickSettingsTab(page, ESOLSettingsTab::Audio);
        mHoverDetail.Reset();
        mIsSliderHoverOk = mIsOverlayHoverOk = true;
        mIsKeyWinsOk = mIsColumnHoverOk = false;
        break;
    }
    case ESolidSmokeStep::SettingsScroll:
    {
        USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
        ClickSettingsTab(page, ESOLSettingsTab::Graphics);
        mScrollFromTab = page != nullptr ? page->GetSelectedTab() : ESOLSettingsTab::Count;
        if (page != nullptr)
        {
            page->ScrollToBinding(SOLKeyBindings::BINDING_COUNT - 1);
        }
        mScrollTabAtOnce = page != nullptr ? page->GetSelectedTab() : ESOLSettingsTab::Count;
        break;
    }
    case ESolidSmokeStep::SettingsReopen:
    {
        // The Key bindings tab is still scrolled from SettingsScroll: its offset before the close
        const USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
        mScrollBeforeReopen = page != nullptr ? page->GetTabScrollOffset(ESOLSettingsTab::KeyBindings) : -1.0f;
        SolidSmokeKey(EKeys::Escape);
        break;
    }
    case ESolidSmokeStep::SettingsClose:
        SolidSmokeKey(EKeys::Escape);
        break;
    case ESolidSmokeStep::MainStrengthZero:
        SetStrength(menu, 0.0f);
        break;
    case ESolidSmokeStep::MainStrengthTwo:
        // The replayed show first: it closes the Settings page left open, which ends its visit (reverting a pending
        // change), so the strength is set after it
        if (mainMenu != nullptr)
        {
            mainMenu->PlayShowAnimation();
        }
        SetStrength(menu, 2.0f);
        if (mainMenu != nullptr)
        {
            if (USOLMenuButton* button = mainMenu->GetEntryButton(SOLID_SMOKE_STRENGTH_ENTRY))
            {
                button->NotifyHovered();
            }
        }
        break;
    case ESolidSmokeStep::Hover:
    {
        // Park the cursor clear of the menu, then hover an entry as the mouse would (it takes keyboard focus)
        const FIntPoint size = GetViewSize();
        if (mController.IsValid() && size.X > SOLID_SMOKE_CURSOR_INSET_PX)
        {
            mController->SetMouseLocation(size.X - SOLID_SMOKE_CURSOR_INSET_PX, SOLID_SMOKE_CURSOR_INSET_PX);
        }
        USOLMenuButton* button = mainMenu != nullptr ? mainMenu->GetEntryButton(SOLID_SMOKE_HOVER_ENTRY) : nullptr;
        if (button != nullptr)
        {
            button->NotifyHovered();
        }
        break;
    }
    case ESolidSmokeStep::Down:
        if (FSlateApplication::IsInitialized())
        {
            const FKeyEvent down(EKeys::Down, FModifierKeysState(), 0, false, 0, 0);
            FSlateApplication::Get().ProcessKeyDownEvent(down);
            FSlateApplication::Get().ProcessKeyUpEvent(down);
        }
        break;
    case ESolidSmokeStep::CueClick:
        // Enter down on the focused entry (Start location, from Down); the release a few frames later (TickStep)
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0,
                0));
        }
        break;
    case ESolidSmokeStep::BoardCreate:
    {
        SetStrength(menu, SOL::Settings::HUD_EFFECTS_DEFAULT);
        if (mainMenu != nullptr)
        {
            mainMenu->ClosePage();
        }
        APlayerController* controller = mController.Get();
        board = controller != nullptr ? CreateWidget<USOLSolidMenuSmokeBoard>(controller,
            USOLSolidMenuSmokeBoard::StaticClass()) : nullptr;
        mBoard = board;
        if (board != nullptr)
        {
            board->AddToViewport(SOLID_SMOKE_BOARD_Z);
            board->BindHoverTraces();
        }
        break;
    }
    case ESolidSmokeStep::Board:
        hover(ESOLSolidSmokeButton::TabA);
        break;
    case ESolidSmokeStep::BoardSplit:
        hover(ESOLSolidSmokeButton::TabB);
        break;
    case ESolidSmokeStep::StrengthZero:
        SetStrength(menu, 0.0f);
        hover(ESOLSolidSmokeButton::StackOn);
        break;
    case ESolidSmokeStep::StrengthTwo:
        SetStrength(menu, 2.0f);
        hover(ESOLSolidSmokeButton::TabB);
        break;
    case ESolidSmokeStep::BoardRemove:
        SetStrength(menu, SOL::Settings::HUD_EFFECTS_DEFAULT);
        if (board != nullptr)
        {
            board->RemoveFromParent();
        }
        break;
    case ESolidSmokeStep::ViewSmall:
        setView(SOLID_SMOKE_SMALL_VIEW);
        break;
    case ESolidSmokeStep::SmallSettings:
    {
        // The longest window mode name pending (not applied; the step's end reverts it), for the screenshot
        ClickEntry(menu, SOLID_SMOKE_SETTINGS_ENTRY);
        USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
        const UTextBlock* mode = page != nullptr ? page->GetStepperValueText(ESOLSettingsStepper::WindowMode) : nullptr;
        FString longest;
        for (int32 index = 0; index < SOLSettingsRules::WINDOW_MODE_COUNT; ++index)
        {
            const FString name = SOLSettingsRules::WindowModeName(index);
            longest = name.Len() > longest.Len() ? name : longest;
        }
        for (int32 tries = 0; mode != nullptr && tries < SOLSettingsRules::WINDOW_MODE_COUNT
            && mode->GetText().ToString() != longest; ++tries)
        {
            page->Step(ESOLSettingsStepper::WindowMode, 1);
        }
        break;
    }
    case ESolidSmokeStep::SmallGameplay:
    {
        // The pending window mode of SmallSettings reverted after its screenshot, then the Gameplay tab
        USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
        if (page != nullptr)
        {
            page->Revert();
        }
        ClickSettingsTab(page, ESOLSettingsTab::Gameplay);
        break;
    }
    case ESolidSmokeStep::SmallKeys:
        ClickSettingsTab(mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr, ESOLSettingsTab::KeyBindings);
        break;
    case ESolidSmokeStep::SmallStart:
        ClickEntry(menu, SOLID_SMOKE_DOWN_ENTRY);
        break;
    case ESolidSmokeStep::SmallControls:
        ClickEntry(menu, SOLID_SMOKE_CONTROLS_ENTRY);
        break;
    case ESolidSmokeStep::SmallDemo:
        ClickEntry(menu, SOLID_SMOKE_HOVER_ENTRY);
        break;
    case ESolidSmokeStep::SmallAbout:
        ClickEntry(menu, SOLID_SMOKE_ABOUT_ENTRY);
        break;
    case ESolidSmokeStep::ViewBack:
        if (mainMenu != nullptr)
        {
            mainMenu->ClosePage();
        }
        setView(SOLID_SMOKE_VIEW);
        break;
    case ESolidSmokeStep::MainFit:
        // The first page opens from closed (it slides in), the next ones switch the open shell's page
        ParkCursor();
        ClickEntry(menu, SOLID_SMOKE_FIT_PAGES[mFitIndex].Entry);
        break;
    case ESolidSmokeStep::Diagram:
        // 33c: the Controls page in the shell (its content width is read once it settles), the cursor parked
        mDiagramWidth = 0.0f;
        mDiagramHover = 0;
        mCursorRetries = 0;
        if (mainMenu != nullptr && mainMenu->GetPage() != ESOLMainMenuPage::Controls)
        {
            ClickEntry(menu, SOLID_SMOKE_CONTROLS_ENTRY);
        }
        SolidSmokeBringGameToFront();
        ParkCursor();
        break;
    case ESolidSmokeStep::MainScroll:
        // The page closed (the focus back on Play), the column capped at SOLID_SMOKE_SCROLL_CAP entries
        mDownPresses = 0;
        if (mainMenu != nullptr)
        {
            mainMenu->ClosePage();
            mainMenu->SetStackMaxVisibleEntriesForVerification(SOLID_SMOKE_SCROLL_CAP);

            // The real cursor moved onto an entry (the hover takes the focus) and left resting over the column, the
            // focus back on Play: the entries the keyboard scrolls under the still cursor must not take the focus
            if (const USOLMenuButton* rest = mainMenu->GetEntryButton(SOLID_SMOKE_HOVER_ENTRY))
            {
                const FGeometry& geometry = rest->GetCachedGeometry();
                SolidSmokeMouseMove(FVector2D(geometry.LocalToAbsolute(geometry.GetLocalSize() * 0.5f)), false);
            }
            mainMenu->FocusDefault();
        }
        break;
    case ESolidSmokeStep::RailScroll:
    {
        // A Controls page (10 tabs) on the viewport, SOLID_SMOKE_RAIL_PAGE_SIZE at the view's centre: too short for
        // its rail, so the rail scrolls
        USOLReadOnlyPageWidget* page = CreateWidget<USOLReadOnlyPageWidget>(mController.Get(),
            USOLReadOnlyPageWidget::StaticClass());
        if (page != nullptr)
        {
            // The size first: setting it resets the slot's anchors to the top left
            page->Setup(ESOLReadOnlyPage::Controls);
            page->AddToViewport(SOLID_SMOKE_BOARD_Z);
            page->SetDesiredSizeInViewport(SOLID_SMOKE_RAIL_PAGE_SIZE);
            page->SetAnchorsInViewport(FAnchors(0.5f, 0.5f));
            page->SetAlignmentInViewport(FVector2D(0.5, 0.5));
            mRailPage = page;
        }
        break;
    }
    case ESolidSmokeStep::PauseFlight:
        if (mainMenu != nullptr)
        {
            mainMenu->ClosePage();
        }
        ClickEntry(menu, SOLID_SMOKE_PLAY_ENTRY);
        break;
    case ESolidSmokeStep::PauseOpen:
    {
        // Esc in flight as the existing flight steps send it (the pawn's Esc action calls RequestEscape)
        mIsOpeningSample = true;
        mSimAtPauseS = SolidSmokeSimSeconds(menu.GetWorld());
        menu.RequestEscape();
        break;
    }
    case ESolidSmokeStep::PausePage:
    {
        // The page's tab as the mouse clicks it (hover takes the focus, then the click); the real cursor parked in the
        // top-right corner, off the menu
        mIsOpeningSample = true;
        ParkCursor();
        USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        SolidSmokeClick(pause != nullptr ? pause->GetTabButton(SOLID_SMOKE_PAUSE_PAGES[mPausePageIndex].Tab) : nullptr);
        break;
    }
    case ESolidSmokeStep::PausePageClose:
    {
        // Back: hovered (it takes the focus), then Enter; Esc: the key on the focused tab
        mIsOpeningSample = false;
        USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        USOLMenuButton* back = pause != nullptr ? pause->GetBackButton() : nullptr;
        if (SOLID_SMOKE_PAUSE_PAGES[mPausePageIndex].bBack && back != nullptr)
        {
            back->NotifyHovered();
            SolidSmokeKey(EKeys::Enter);
        }
        else
        {
            SolidSmokeKey(EKeys::Escape);
        }
        const USOLMenuButton* resume = pause != nullptr ? pause->GetTabButton(SOLMenuState::EMenuTab::Resume) : nullptr;
        mWasClosedAtOnce = pause != nullptr && menu.GetState().Mode == SOLMenuState::EMenuMode::Paused
            && menu.GetState().Tab == SOLMenuState::EMenuTab::Resume
            && pause->GetTab() == SOLMenuState::EMenuTab::Resume && resume != nullptr && resume->HasKeyboardFocus();
        break;
    }
    case ESolidSmokeStep::PauseQuitTab:
    {
        USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        SolidSmokeClick(pause != nullptr ? pause->GetTabButton(SOLMenuState::EMenuTab::Quit) : nullptr);
        break;
    }
    case ESolidSmokeStep::PauseResume:
        // Esc on the focused Quit tab: no page is open, so it resumes
        mIsOpeningSample = false;
        SolidSmokeKey(EKeys::Escape);
        break;
    case ESolidSmokeStep::PauseReverse:
        mHasLeftViewport = false;
        mPreProgress = mPostProgress = -1.0f;
        menu.RequestEscape();
        break;
    case ESolidSmokeStep::PauseStrengthZero:
        // Applied, not pending: the resume and the pause change the mode, which reverts a pending value
        ApplyStrength(menu, 0.0f);
        break;
    case ESolidSmokeStep::PauseStrengthTwo:
    {
        // Applied, not pending (as at strength 0), then the Settings tab clicked as the mouse would
        ApplyStrength(menu, 2.0f);
        ParkCursor();
        USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        SolidSmokeClick(pause != nullptr ? pause->GetTabButton(SOLMenuState::EMenuTab::Settings) : nullptr);
        break;
    }
    case ESolidSmokeStep::PauseKeys:
    {
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        ClickSettingsTab(pause != nullptr ? pause->GetSettingsPage() : nullptr, ESOLSettingsTab::KeyBindings);
        break;
    }
    case ESolidSmokeStep::PauseSmall:
    {
        // Graphics again after PauseKeys (its stepper values are read at 1000x800), then the small view
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        ClickSettingsTab(pause != nullptr ? pause->GetSettingsPage() : nullptr, ESOLSettingsTab::Graphics);
        setView(SOLID_SMOKE_SMALL_VIEW);
        break;
    }
    case ESolidSmokeStep::PauseSmallKeys:
    {
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        ClickSettingsTab(pause != nullptr ? pause->GetSettingsPage() : nullptr, ESOLSettingsTab::KeyBindings);
        break;
    }
    case ESolidSmokeStep::PauseSmallControls:
    case ESolidSmokeStep::PauseSmallInfo:
    {
        // The page's tab as the mouse clicks it; the open shell switches to the page (read after a settle)
        ParkCursor();
        USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        const SOLMenuState::EMenuTab tab = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::PauseSmallControls
            ? SOLMenuState::EMenuTab::Controls : SOLMenuState::EMenuTab::Info;
        SolidSmokeClick(pause != nullptr ? pause->GetTabButton(tab) : nullptr);
        break;
    }
    case ESolidSmokeStep::Quit:
        SetStrength(menu, SOL::Settings::HUD_EFFECTS_DEFAULT);
        Finish(menu);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true once the step's wait is over
bool FSOLSolidMenuSmoke::TickStep(const int32 step, USOLMenuSubsystem& menu)
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    switch (static_cast<ESolidSmokeStep>(step))
    {
    case ESolidSmokeStep::Setup:
        return mStepTimeS >= SOLID_SMOKE_SETUP_S && mainMenu != nullptr && mainMenu->IsInViewport();
    case ESolidSmokeStep::View:
    case ESolidSmokeStep::ViewSmall:
    case ESolidSmokeStep::ViewBack:
    case ESolidSmokeStep::PauseSmall:
    {
        const bool bSmall = static_cast<ESolidSmokeStep>(step) != ESolidSmokeStep::View
            && static_cast<ESolidSmokeStep>(step) != ESolidSmokeStep::ViewBack;
        const FIntPoint want = bSmall ? SOLID_SMOKE_SMALL_VIEW : SOLID_SMOKE_VIEW;
        mViewFrames = GetViewSize() == want ? mViewFrames + 1 : 0;

        // The pause step also lets the open page settle in the new size before it is read
        const bool bSettled = static_cast<ESolidSmokeStep>(step) != ESolidSmokeStep::PauseSmall
            || mStepTimeS >= SOLID_SMOKE_SETTLE_S;
        return (mViewFrames >= SOLID_SMOKE_VIEW_STABLE_FRAMES && bSettled) || mStepTimeS >= SOLID_SMOKE_VIEW_TIMEOUT_S;
    }
    case ESolidSmokeStep::MainOpen:
        if (mainMenu != nullptr)
        {
            SampleAnim(mainMenu->GetColumnProgress(), mainMenu->GetColumnTracePower(), false);
            const TSharedPtr<SSOLSolidTraceLayer> layer = SolidSmokeColumnLayer(mainMenu);
            mSeenLeds |= layer.IsValid() ? layer->GetLitLeds() : 0u;
        }
        return mStepTimeS >= SOLID_SMOKE_SETTLE_S;
    case ESolidSmokeStep::CueClick:
    {
        // Stage 0: Enter held (pressed look) then released: the click opens Start location; 1: a settle, then Enter
        // down again; 2: held, then released: the page closes; 3: a settle
        const bool bRelease = (mSubStage == 0 || mSubStage == 2)
            && mStepFrames - mSubStageFrames >= SOLID_SMOKE_PAINT_FRAMES;
        const bool bPressAgain = mSubStage == 1 && mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
        if ((bRelease || bPressAgain) && FSlateApplication::IsInitialized())
        {
            const FKeyEvent enter(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
            if (bRelease)
            {
                FSlateApplication::Get().ProcessKeyUpEvent(enter);
            }
            else
            {
                FSlateApplication::Get().ProcessKeyDownEvent(enter);
            }
        }
        if (bRelease || bPressAgain)
        {
            ++mSubStage;
            mSubStageS = mStepTimeS;
            mSubStageFrames = mStepFrames;
        }
        return mSubStage == 3 && mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
    }
    case ESolidSmokeStep::PlayReturn:
    {
        // Stage 0: Play settles focused, then is clicked; 1: in flight, then Esc; 2: paused, then Quit to menu;
        // 3: the first frame the main menu is back (read at once); 4: settle
        const SOLMenuState::EMenuMode mode = menu.GetState().Mode;
        if (mainMenu == nullptr)
        {
            return true;
        }
        if (mSubStage == 0 && mStepTimeS >= SOLID_SMOKE_SETTLE_S)
        {
            ClickEntry(menu, SOLID_SMOKE_PLAY_ENTRY);
            mSubStage = 1;
            mSubStageS = mStepTimeS;
        }
        else if (mSubStage == 1 && mode == SOLMenuState::EMenuMode::Playing
            && mStepTimeS - mSubStageS >= SOLID_SMOKE_FLIGHT_S)
        {
            menu.RequestEscape();
            mSubStage = 2;
            mSubStageS = mStepTimeS;
        }
        else if (mSubStage == 2 && mode == SOLMenuState::EMenuMode::Paused
            && mStepTimeS - mSubStageS >= SOLID_SMOKE_PAUSE_S)
        {
            menu.RequestQuitToMenu();
            mSubStage = 3;
        }
        else if (mSubStage == 3 && mode == SOLMenuState::EMenuMode::MainMenu && mainMenu->IsInViewport())
        {
            const USOLSolidPanel* shell = mainMenu->GetShellPanel();
            int32 selected = 0;
            for (int32 index = 0; index < mainMenu->GetEntryButtonCount(); ++index)
            {
                selected += mainMenu->GetEntryButton(index)->IsSelected() ? 1 : 0;
            }
            mIsReturnClosed = shell != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::None
                && mainMenu->GetShellAnim().Phase == ESOLHoloMenuPhase::Closed
                && shell->GetVisibility() == ESlateVisibility::Hidden && selected == 0
                && mainMenu->GetShownPageIndex() == static_cast<int32>(ESOLMainMenuPage::None);
            const USOLMenuButton* play = mainMenu->GetEntryButton(SOLID_SMOKE_PLAY_ENTRY);
            mIsReturnTraced = play != nullptr && play->HasKeyboardFocus()
                && mainMenu->IsEntryHoverTraceShown(SOLID_SMOKE_PLAY_ENTRY);
            const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
            mIsReturnPauseGone = pause == nullptr || (!pause->IsInViewport()
                && pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Closed);
            mReturnPauseDetail = FString::Printf(TEXT("pause menu %s, in the viewport %d, phase %d"), pause != nullptr
                ? TEXT("built") : TEXT("none"), pause != nullptr && pause->IsInViewport() ? 1 : 0, pause != nullptr
                ? static_cast<int32>(pause->GetPanelAnim().Phase) : -1);
            mReturnDetail = FString::Printf(TEXT("page %d, phase %d, shell visibility %d, shown page %d, %d selected; "
                "Play focused %d"), static_cast<int32>(mainMenu->GetPage()),
                static_cast<int32>(mainMenu->GetShellAnim().Phase), shell != nullptr
                ? static_cast<int32>(shell->GetVisibility()) : -1, mainMenu->GetShownPageIndex(), selected,
                play != nullptr && play->HasKeyboardFocus() ? 1 : 0);
            mSubStage = 4;
            mSubStageS = mStepTimeS;
        }
        return mSubStage == 4 && mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
    }
    case ESolidSmokeStep::PageOpen:
    case ESolidSmokeStep::PageClose:
    {
        const USOLSolidPanel* shell = mainMenu != nullptr ? mainMenu->GetShellPanel() : nullptr;
        if (shell != nullptr)
        {
            SampleAnim(mainMenu->GetShellProgress(), 1.0f,
                shell->GetVisibility() == ESlateVisibility::HitTestInvisible);
        }
        return mStepTimeS >= SOLID_SMOKE_SETTLE_S;
    }
    case ESolidSmokeStep::SettingsSliderFill:
        return TickSliderFill(menu);
    case ESolidSmokeStep::SettingsHoverWins:
        return TickHoverWins(menu);
    case ESolidSmokeStep::SettingsReopen:
        // Stage 0: Esc sent, the shell closes; 1: Settings opened again, then a settle
        if (mSubStage == 0 && mStepTimeS >= SOLID_SMOKE_SETTLE_S)
        {
            ClickEntry(menu, SOLID_SMOKE_SETTINGS_ENTRY);
            mSubStage = 1;
            mSubStageS = mStepTimeS;
        }
        return mSubStage == 1 && mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
    case ESolidSmokeStep::PageReverse:
        // Stage 0: open; 1: Esc; 2 (next frame, mid close): reopen; 3: settle
        if (mainMenu == nullptr)
        {
            return true;
        }
        if (mSubStage == 0 && mStepTimeS >= SOLID_SMOKE_REVERSE_OPEN_S)
        {
            SolidSmokeKey(EKeys::Escape);
            mSubStage = 1;
        }
        else if (mSubStage == 1)
        {
            mPreProgress = mainMenu->GetShellAnim().Phase == ESOLHoloMenuPhase::Closing
                ? mainMenu->GetShellProgress() : -1.0f;
            mCueOpenBeforeReopen = SolidSmokeCues(menu, ESOLHoloCue::Open);
            ClickEntry(menu, SOLID_SMOKE_ABOUT_ENTRY);
            mPostProgress = mainMenu->GetShellProgress();
            mSubStage = 2;
            mSubStageS = mStepTimeS;
        }
        return mSubStage == 2 && mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
    case ESolidSmokeStep::MainStrengthZero:
        // Stage 0: the strength reaches the menu; 1: replay the open and open Settings, hover an entry; 2: read
        if (mainMenu == nullptr)
        {
            return true;
        }
        if (mSubStage == 0 && mStepFrames >= SOLID_SMOKE_INSTANT_FRAME)
        {
            // The open page closes and Settings opens, both at once at strength 0
            mainMenu->PlayShowAnimation();
            mainMenu->ClosePage();
            ClickEntry(menu, SOLID_SMOKE_SETTINGS_ENTRY);
            if (USOLMenuButton* button = mainMenu->GetEntryButton(SOLID_SMOKE_STRENGTH_ENTRY))
            {
                button->NotifyHovered();
            }
            mSubStage = 1;
            mSubStageS = mStepTimeS;
        }
        else if (mSubStage == 1)
        {
            // The frame after: everything already at its end, the hover look included
            const USOLMenuButton* button = mainMenu->GetEntryButton(SOLID_SMOKE_STRENGTH_ENTRY);
            mInstantLook = button != nullptr ? button->GetAppliedLook() : FSOLSolidButtonLook();
            mInstantTimeS = mStepTimeS - mSubStageS;
            mFirstProgress = FMath::Min(mainMenu->GetColumnProgress(), mainMenu->GetShellProgress());
            mFirstPower = mainMenu->GetColumnTracePower();
            mSubStage = 2;
        }
        return mSubStage == 2 && mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
    case ESolidSmokeStep::StrengthZero:
    {
        // The look on the frame after the hover, long before a strength-1 ramp could finish
        const USOLSolidMenuSmokeBoard* board = mBoard.Get();
        const USOLMenuButton* button = board != nullptr ? board->GetButton(ESOLSolidSmokeButton::StackOn) : nullptr;
        if (mStepFrames == SOLID_SMOKE_INSTANT_FRAME && button != nullptr)
        {
            mInstantLook = button->GetAppliedLook();
            mInstantTimeS = mStepTimeS;
        }
        return true;
    }
    case ESolidSmokeStep::SmallControlsRows:
        return TickRowsSweep(TEXT("h-controls"), mainMenu != nullptr ? mainMenu->GetControlsPage() : nullptr,
            mainMenu != nullptr ? mainMenu->GetShellPanel() : nullptr, nullptr, 0);
    case ESolidSmokeStep::PausePageRows:
    {
        // Controls and Info only (the pages with a left column); the other pages go straight on
        const FSolidSmokePausePage& entry = SOLID_SMOKE_PAUSE_PAGES[mPausePageIndex];
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        const bool bInfo = entry.Tab == SOLMenuState::EMenuTab::Info;
        if (entry.Tab != SOLMenuState::EMenuTab::Controls && !bInfo)
        {
            return true;
        }
        USOLReadOnlyPageWidget* page = pause == nullptr ? nullptr : bInfo ? pause->GetInfoPage()
            : pause->GetControlsPage();
        return TickRowsSweep(*FString::Printf(TEXT("v-%s"), entry.Name), page, pause != nullptr
            ? pause->GetShellPanel() : nullptr, bInfo ? TEXT("pause-info") : nullptr, bInfo ? MAX_int32 : 0);
    }
    case ESolidSmokeStep::PauseSmallControls:
    case ESolidSmokeStep::PauseSmallInfo:
    {
        // A settle for the shell's page switch, then the sweep (Controls: its first tab screenshotted; Info: each)
        if (mStepTimeS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        const bool bInfo = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::PauseSmallInfo;
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        USOLReadOnlyPageWidget* page = pause == nullptr ? nullptr : bInfo ? pause->GetInfoPage()
            : pause->GetControlsPage();
        return TickRowsSweep(bInfo ? TEXT("h-pause-small-info") : TEXT("h-pause-small-controls"), page,
            pause != nullptr ? pause->GetShellPanel() : nullptr, bInfo ? TEXT("pause-small-info")
            : TEXT("pause-small-controls"), bInfo ? MAX_int32 : 1);
    }
    case ESolidSmokeStep::Diagram:
        return TickDiagram(menu, TEXT("k-diagram"), TEXT("diagram"));
    case ESolidSmokeStep::MainScroll:
        return TickMainScroll(menu);
    case ESolidSmokeStep::RailScroll:
        return TickRailScroll(menu);
    case ESolidSmokeStep::PauseFlight:
        // Stage 0: Play clicked, until flying; 1: the flight settles
        if (mSubStage == 0 && menu.GetState().Mode == SOLMenuState::EMenuMode::Playing)
        {
            mSubStage = 1;
            mSubStageS = mStepTimeS;
        }
        return mSubStage == 1 && mStepTimeS - mSubStageS >= SOLID_SMOKE_FLIGHT_SETTLE_S;
    case ESolidSmokeStep::PauseOpen:
    {
        // Every frame once the pause menu shows: the side panel's slide sampled, then a settle
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        if (menu.GetState().Mode != SOLMenuState::EMenuMode::Paused || pause == nullptr || !pause->IsInViewport())
        {
            return false;
        }
        if (mSubStage == 0)
        {
            mFirstSlideX = pause->GetPanelLook().SlideX;
            mSubStage = 1;
            mSubStageS = mStepTimeS;
        }
        SampleAnim(pause->GetPanelProgress(), pause->GetTracePower(), false);
        return mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
    }
    case ESolidSmokeStep::PausePage:
    case ESolidSmokeStep::PausePageClose:
    {
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        const USOLSolidPanel* shell = pause != nullptr ? pause->GetShellPanel() : nullptr;
        if (shell != nullptr)
        {
            SampleAnim(pause->GetShellProgress(), 1.0f, shell->GetVisibility() == ESlateVisibility::HitTestInvisible);
        }
        return mStepTimeS >= SOLID_SMOKE_SETTLE_S;
    }
    case ESolidSmokeStep::PauseResume:
        return TickPauseResume(menu);
    case ESolidSmokeStep::PauseReverse:
        return TickPauseReverse(menu);
    case ESolidSmokeStep::PauseStrengthZero:
        return TickPauseStrengthZero(menu);
    default:
        return mStepTimeS >= SOLID_SMOKE_SETTLE_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks and screenshot at its end
void FSOLSolidMenuSmoke::ExitStep(const int32 step, USOLMenuSubsystem& menu)
{
    const USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    const USOLSolidMenuSmokeBoard* board = mBoard.Get();
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(&menu);
    const float s = solid != nullptr ? solid->GetStrength() : -1.0f;

    // Returns a board button's applied look (a default look without the board)
    const auto lookOf = [board](const ESOLSolidSmokeButton which)
    {
        const USOLMenuButton* button = board != nullptr ? board->GetButton(which) : nullptr;
        return button != nullptr ? button->GetAppliedLook() : FSOLSolidButtonLook();
    };
    switch (static_cast<ESolidSmokeStep>(step))
    {
    case ESolidSmokeStep::Setup:
    {
        int32 styles = 0;
        for (int32 style = 0; style < static_cast<int32>(ESOLSolidStyle::Count); ++style)
        {
            styles += solid != nullptr && solid->HasStyleMaterial(static_cast<ESOLSolidStyle>(style)) ? 1 : 0;
        }
        Check(TEXT("a-assets"), solid != nullptr && solid->GetMissingAssetCount() == 0
            && solid->GetMenuFont() != nullptr && styles == static_cast<int32>(ESOLSolidStyle::Count),
            FString::Printf(TEXT("missing %d, font %s, %d/%d style materials"), solid != nullptr
            ? solid->GetMissingAssetCount() : -1, solid != nullptr && solid->GetMenuFont() != nullptr ? TEXT("yes")
            : TEXT("no"), styles, static_cast<int32>(ESOLSolidStyle::Count)));
        int32 drawn = 0;
        const int32 entries = mainMenu != nullptr ? mainMenu->GetEntryButtonCount() : 0;
        for (int32 index = 0; index < entries; ++index)
        {
            drawn += mainMenu->GetEntryButton(index)->IsHullMaterialDrawn() ? 1 : 0;
        }
        Check(TEXT("a-buttons-material"), entries > 0 && drawn == entries,
            FString::Printf(TEXT("%d/%d entry buttons on the hull material"), drawn, entries));
        Check(TEXT("a-strength"), FMath::IsNearlyEqual(s, SOL::Settings::HUD_EFFECTS_DEFAULT),
            FString::Printf(TEXT("HUD effects strength %.2f (smoke slot default)"), s));

        // The column: its own hull material, the stack, the traces; the shell's own material
        const USOLSolidPanel* column = mainMenu != nullptr ? mainMenu->GetColumnPanel() : nullptr;
        const USOLSolidPanel* shell = mainMenu != nullptr ? mainMenu->GetShellPanel() : nullptr;
        int32 stack = 0;
        for (int32 index = 0; index < entries; ++index)
        {
            stack += mainMenu->GetEntryButton(index)->GetVariant() == ESOLSolidButtonVariant::Stack ? 1 : 0;
        }
        const int32 traces = column != nullptr && column->GetTraceLayer().IsValid()
            ? column->GetTraceLayer()->GetTraceCount() : 0;
        Check(TEXT("a-main-panel"), column != nullptr && column->HasUniqueMaterial() && column->IsHullMaterialDrawn()
            && stack == 7 && entries == 7 && traces == SOLID_SMOKE_COLUMN_TRACES && shell != nullptr
            && shell->HasUniqueMaterial(), FString::Printf(TEXT("column material %s, %d/%d stack buttons, %d traces, "
            "shell material %s"), column != nullptr && column->HasUniqueMaterial() ? TEXT("own") : TEXT("no"), stack,
            entries, traces, shell != nullptr && shell->HasUniqueMaterial() ? TEXT("own") : TEXT("no")));

        // The chin recess: the version, studio and engine line
        const FString version = SOLBuildVersion::CurrentVersion();
        const FString want = SOLBuildVersion::FormatVersionLine(version,
            FEngineVersion::Current().ToString(EVersionComponent::Patch));
        const FString shown = mainMenu != nullptr ? mainMenu->GetVersionText() : FString();
        Check(TEXT("a-version"), shown == want && SolidSmokeIsVersionShape(version), FString::Printf(
            TEXT("'%s' (want '%s')"), *shown, *want));

        // The first show's open has finished: Open, traces fully powered
        Check(TEXT("a-first-open"), mainMenu != nullptr && mainMenu->GetColumnAnim().Phase == ESOLHoloMenuPhase::Open
            && FMath::IsNearlyEqual(mainMenu->GetColumnProgress(), 1.0f) && FMath::IsNearlyEqual(
            mainMenu->GetColumnTracePower(), 1.0f), FString::Printf(TEXT("progress %.3f power %.3f"), mainMenu
            != nullptr ? mainMenu->GetColumnProgress() : -1.0f, mainMenu != nullptr
            ? mainMenu->GetColumnTracePower() : -1.0f));

        // The wrapped key hint ends above trace row A (its ring terminals), as laid out
        const float hintBottom = mainMenu != nullptr ? mainMenu->GetHintBottom() : -1.0f;
        const float rowA = mainMenu != nullptr ? mainMenu->GetTraceRowA() : 0.0f;
        Check(TEXT("a-hint-clear"), hintBottom > 0.0f && hintBottom <= rowA - SOL::SolidMenu::COLUMN_HINT_CLEARANCE_PX,
            FString::Printf(TEXT("hint bottom %.1f, trace row A %.1f, clearance %.1f"), hintBottom, rowA,
            SOL::SolidMenu::COLUMN_HINT_CLEARANCE_PX));
        break;
    }
    case ESolidSmokeStep::MainOpen:
    {
        // Opening with the traces dark on the first sample, the power-up seen mid-way, then Open and fully powered
        const bool bEnd = mainMenu != nullptr && mainMenu->GetColumnAnim().Phase == ESOLHoloMenuPhase::Open
            && FMath::IsNearlyEqual(mainMenu->GetColumnProgress(), 1.0f)
            && FMath::IsNearlyEqual(mainMenu->GetColumnTracePower(), 1.0f);
        Check(TEXT("a-open-sequence"), mFirstProgress >= 0.0f && mFirstProgress < 1.0f && mFirstPower == 0.0f
            && mSawMidProgress && mSawMidPower && mIsMonotonic && bEnd, FString::Printf(TEXT("%d samples: first "
            "progress %.3f power %.3f, mid progress %d, mid power %d, monotonic %d, end Open %d"), mSamples,
            mFirstProgress, mFirstPower, mSawMidProgress ? 1 : 0, mSawMidPower ? 1 : 0, mIsMonotonic ? 1 : 0,
            bEnd ? 1 : 0));

        // The power-up blink lit every entry whose own LED is dark, none other, and ended off; the pulse dot rides the
        // longest trace (the first) at strength 1
        const TSharedPtr<SSOLSolidTraceLayer> layer = SolidSmokeColumnLayer(mainMenu);
        const uint32 want = mainMenu != nullptr ? SolidSmokeBlinkableLeds(*mainMenu) : 0u;
        const uint32 litNow = layer.IsValid() ? layer->GetLitLeds() : 1u;
        Check(TEXT("a-led-blink"), want != 0 && mSeenLeds == want && litNow == 0, FString::Printf(TEXT("seen 0x%02x, "
            "want 0x%02x, lit at the end 0x%02x"), mSeenLeds, want, litNow));
        CheckCueDeltas(TEXT("a-open-cue"), menu, 1, -1, -1);
        const float dotAlpha = layer.IsValid() ? layer->GetPulseDotAlpha() : -1.0f;
        Check(TEXT("a-pulse-dot"), layer.IsValid() && layer->GetLongestTrace() == 0 && FMath::IsNearlyEqual(dotAlpha,
            SOLSolidMenu::PulseDotAlpha(s), SOLID_SMOKE_TOLERANCE) && dotAlpha > 0.0f, FString::Printf(TEXT("longest "
            "trace %d, dot alpha %.3f at strength %.2f"), layer.IsValid() ? layer->GetLongestTrace() : -1, dotAlpha,
            s));
        TakeUiShot(TEXT("main"));
        break;
    }
    case ESolidSmokeStep::View:
    {
        const FIntPoint size = GetViewSize();
        Check(TEXT("a-view"), size == SOLID_SMOKE_VIEW, FString::Printf(TEXT("view %dx%d"), size.X, size.Y));
        break;
    }
    case ESolidSmokeStep::Hover:
    case ESolidSmokeStep::Down:
    {
        const bool bHover = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::Hover;
        const int32 expected = bHover ? SOLID_SMOKE_HOVER_ENTRY : SOLID_SMOKE_DOWN_ENTRY;
        int32 litIndex = INDEX_NONE;
        const int32 lit = mainMenu != nullptr ? SolidSmokeLitEntries(*mainMenu, litIndex) : 0;
        const USOLMenuButton* button = mainMenu != nullptr ? mainMenu->GetEntryButton(expected) : nullptr;
        const FSOLSolidButtonLook look = button != nullptr ? button->GetAppliedLook() : FSOLSolidButtonLook();
        const FSOLSolidButtonLook want = SolidSmokeSettled(ESOLSolidButtonState::Hovered, s, button != nullptr
            && button->IsSelected());
        const bool bTint = button != nullptr && button->GetAppliedTint().Equals(SOLSolidStyle::BrushTint(look),
            SOLID_SMOKE_TOLERANCE);
        Check(bHover ? TEXT("b-hover") : TEXT("c-down"), lit == 1 && litIndex == expected
            && SolidSmokeLookMatches(look, want) && bTint, FString::Printf(TEXT("%d lit (entry %d, want %d); %s; "
            "want %s; tint %s"), lit, litIndex, expected, *SolidSmokeDescribe(look), *SolidSmokeDescribe(want),
            bTint ? TEXT("= BrushTint") : TEXT("differs")));
        const int32 other = bHover ? SOLID_SMOKE_DOWN_ENTRY : SOLID_SMOKE_HOVER_ENTRY;
        const bool bTrace = mainMenu != nullptr && mainMenu->IsEntryHoverTraceShown(expected)
            && !mainMenu->IsEntryHoverTraceShown(other);
        Check(bHover ? TEXT("b-hover-trace") : TEXT("c-down-trace"), bTrace, FString::Printf(TEXT("entry %d's trace "
            "%s, entry %d's %s"), expected, mainMenu != nullptr && mainMenu->IsEntryHoverTraceShown(expected)
            ? TEXT("drawn") : TEXT("not drawn"), other, mainMenu != nullptr && mainMenu->IsEntryHoverTraceShown(other)
            ? TEXT("drawn") : TEXT("not drawn")));
        CheckCueDeltas(bHover ? TEXT("b-hover-cue") : TEXT("c-down-cue"), menu, -1, 1, -1);
        if (bHover)
        {
            TakeUiShot(TEXT("main-hover"));
        }
        break;
    }
    case ESolidSmokeStep::CueClick:
    {
        // Two clicks on the entry that keeps the focus: Start location opened (the shell from closed) and closed
        const bool bClosed = mainMenu != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::None;
        const USOLMenuButton* entry = mainMenu != nullptr ? mainMenu->GetEntryButton(SOLID_SMOKE_DOWN_ENTRY) : nullptr;
        Check(TEXT("c-cue-click-page"), bClosed && entry != nullptr && entry->HasKeyboardFocus(), FString::Printf(
            TEXT("page %d after the second click, the entry focused %d"), mainMenu != nullptr
            ? static_cast<int32>(mainMenu->GetPage()) : -1, entry != nullptr && entry->HasKeyboardFocus() ? 1 : 0));
        CheckCueDeltas(TEXT("c-cue-click"), menu, 1, 0, 2);
        break;
    }
    case ESolidSmokeStep::PageOpen:
    {
        const FSolidSmokePage& page = SOLID_SMOKE_PAGES[mPageIndex];
        const USOLSolidPanel* shell = mainMenu != nullptr ? mainMenu->GetShellPanel() : nullptr;
        const USOLMenuButton* back = mainMenu != nullptr ? mainMenu->GetBackButton() : nullptr;
        const bool bOpen = mainMenu != nullptr && shell != nullptr && mainMenu->GetPage() == page.Page
            && mainMenu->GetShownPageIndex() == static_cast<int32>(page.Page)
            && mainMenu->GetShellAnim().Phase == ESOLHoloMenuPhase::Open
            && FMath::IsNearlyEqual(mainMenu->GetShellProgress(), 1.0f)
            && shell->GetVisibility() == ESlateVisibility::Visible && shell->IsHullMaterialDrawn();
        const bool bBack = back != nullptr && back->GetVariant() == ESOLSolidButtonVariant::Small
            && back->IsHullMaterialDrawn();
        CheckCueDeltas(*FString::Printf(TEXT("p-%s-open-cue"), page.Name), menu, 1, -1, -1);
        Check(*FString::Printf(TEXT("p-%s-open"), page.Name), bOpen && bBack && mFirstProgress >= 0.0f
            && mFirstProgress < 1.0f && mSawMidProgress && mIsMonotonic, FString::Printf(TEXT("page %d (want %d), "
            "shown %d, first progress %.3f, mid %d, monotonic %d, end progress %.3f, Back %s"), mainMenu != nullptr
            ? static_cast<int32>(mainMenu->GetPage()) : -1, static_cast<int32>(page.Page), mainMenu != nullptr
            ? mainMenu->GetShownPageIndex() : -1, mFirstProgress, mSawMidProgress ? 1 : 0, mIsMonotonic ? 1 : 0,
            mainMenu != nullptr ? mainMenu->GetShellProgress() : -1.0f, bBack ? TEXT("small, solid") : TEXT("wrong")));

        // Start location: exactly one selected row, the start body, with the cyan mark
        if (page.Page == ESOLMainMenuPage::StartLocation)
        {
            int32 selected = 0;
            int32 selectedIndex = INDEX_NONE;
            int32 small = 0;
            const int32 rows = mainMenu != nullptr ? mainMenu->GetBodyButtonCount() : 0;
            for (int32 index = 0; index < rows; ++index)
            {
                const USOLMenuButton* row = mainMenu->GetBodyButton(index);
                small += row->GetVariant() == ESOLSolidButtonVariant::Small ? 1 : 0;
                if (row->IsSelected())
                {
                    ++selected;
                    selectedIndex = index;
                }
            }
            const USOLMenuButton* selectedRow = mainMenu != nullptr ? mainMenu->GetBodyButton(selectedIndex) : nullptr;
            Check(TEXT("p-start-selected"), rows > 0 && small == rows && selected == 1
                && selectedIndex == menu.GetStartBodyIndex() && selectedRow != nullptr
                && selectedRow->GetAppliedLook().bSelectedMark, FString::Printf(TEXT("%d rows (%d small), %d selected "
                "(row %d, start body %d), mark %d"), rows, small, selected, selectedIndex, menu.GetStartBodyIndex(),
                selectedRow != nullptr && selectedRow->GetAppliedLook().bSelectedMark ? 1 : 0));
        }
        if (page.Page == ESOLMainMenuPage::Controls && mainMenu != nullptr)
        {
            CheckReadOnlyPage(TEXT("p-controls"), mainMenu->GetControlsPage());
        }
        else if (page.Page == ESOLMainMenuPage::About && mainMenu != nullptr)
        {
            CheckReadOnlyPage(TEXT("p-about"), mainMenu->GetAboutPage());
        }
        else if (page.Page == ESOLMainMenuPage::Demo)
        {
            CheckDemoLayout(TEXT("p-demo"), menu);
        }
        TakeUiShot(*FString::Printf(TEXT("page-%s"), page.Name));
        break;
    }
    case ESolidSmokeStep::PageClose:
    {
        const FSolidSmokePage& page = SOLID_SMOKE_PAGES[mPageIndex];
        const USOLSolidPanel* shell = mainMenu != nullptr ? mainMenu->GetShellPanel() : nullptr;
        const bool bClosed = mainMenu != nullptr && shell != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::None
            && mainMenu->GetShellAnim().Phase == ESOLHoloMenuPhase::Closed
            && shell->GetVisibility() == ESlateVisibility::Hidden
            && mainMenu->GetShownPageIndex() == static_cast<int32>(ESOLMainMenuPage::None);
        Check(*FString::Printf(TEXT("q-%s-close-%s"), page.Name, page.bBack ? TEXT("back") : TEXT("esc")),
            mWasClosedAtOnce && mFirstProgress > 0.0f && mSawMidProgress && mSawHitTestInvisible && mIsMonotonic
            && bClosed, FString::Printf(TEXT("page None and focus on Play at once %d, first progress %.3f, mid %d, "
            "input-transparent %d, monotonic %d, closed and hidden %d"), mWasClosedAtOnce ? 1 : 0, mFirstProgress,
            mSawMidProgress ? 1 : 0, mSawHitTestInvisible ? 1 : 0, mIsMonotonic ? 1 : 0, bClosed ? 1 : 0));
        if (++mPageIndex < SOLID_SMOKE_PAGE_COUNT)
        {
            mRepeatStep = static_cast<int32>(ESolidSmokeStep::PageOpen);
        }
        break;
    }
    case ESolidSmokeStep::PageReverse:
    {
        const bool bOpen = mainMenu != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::About
            && mainMenu->GetShellAnim().Phase == ESOLHoloMenuPhase::Open;
        const USOLReadOnlyPageWidget* about = mainMenu != nullptr ? mainMenu->GetAboutPage() : nullptr;
        Check(TEXT("r-about-first-tab"), about != nullptr && about->GetSelectedTab() == SOLPageTabs::OpeningTab()
            && about->GetShownContentIndex() == SOLPageTabs::OpeningTab(), FString::Printf(TEXT("About reopened on "
            "tab %d (content %d) after it was left on its last tab"), about != nullptr ? about->GetSelectedTab() : -1,
            about != nullptr ? about->GetShownContentIndex() : -1));
        Check(TEXT("r-reopen-reverses"), mPreProgress > 0.0f && mPreProgress < 1.0f && FMath::IsNearlyEqual(
            mPreProgress, mPostProgress, SOLID_SMOKE_REVERSAL_TOLERANCE) && bOpen, FString::Printf(TEXT("closing at "
            "%.3f, reopened at %.3f, then Open on About %d"), mPreProgress, mPostProgress, bOpen ? 1 : 0));
        CheckReopenNoCue(TEXT("r-reopen-no-cue"), menu);
        break;
    }
    case ESolidSmokeStep::PlayReturn:
    {
        Check(TEXT("t-return-shell-closed"), mIsReturnClosed, mReturnDetail);
        Check(TEXT("t-return-pause-gone"), mIsReturnPauseGone, mReturnPauseDetail);

        // Read on the first frame back and again a settle later: the input mode's widget to focus is Play, so the
        // focus stays on it (a menu-root focus here would be the old TECH_DEBT symptom)
        const USOLMenuButton* play = mainMenu != nullptr ? mainMenu->GetEntryButton(SOLID_SMOKE_PLAY_ENTRY) : nullptr;
        const bool bSettledOnPlay = play != nullptr && play->HasKeyboardFocus();
        const TSharedPtr<SWidget> focus = FSlateApplication::IsInitialized()
            ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
        Check(TEXT("t-return-play-trace"), mIsReturnTraced && bSettledOnPlay, FString::Printf(TEXT("first frame back: "
            "Play focused with its hover trace %d; a settle later Play focused %d, focus on %s"),
            mIsReturnTraced ? 1 : 0, bSettledOnPlay ? 1 : 0, focus.IsValid() ? *focus->GetTypeAsString()
            : TEXT("nothing")));
        break;
    }
    case ESolidSmokeStep::SettingsOpen:
    case ESolidSmokeStep::SettingsTab:
    case ESolidSmokeStep::SettingsRowHover:
    case ESolidSmokeStep::SettingsControls:
    case ESolidSmokeStep::SettingsSliderFill:
    case ESolidSmokeStep::SettingsHoverWins:
    case ESolidSmokeStep::SettingsScroll:
    case ESolidSmokeStep::SettingsReopen:
    case ESolidSmokeStep::SettingsClose:
        ExitSettingsStep(step, menu);
        break;
    case ESolidSmokeStep::MainStrengthZero:
    {
        const bool bInstant = FMath::IsNearlyEqual(mFirstProgress, 1.0f) && FMath::IsNearlyEqual(mFirstPower, 1.0f);
        Check(TEXT("s-main-s0"), FMath::IsNearlyZero(s) && bInstant && SolidSmokeLookMatches(mInstantLook,
            SolidSmokeSettled(ESOLSolidButtonState::Hovered, 0.0f, false)), FString::Printf(TEXT("strength %.2f: "
            "column and shell at %.3f, power %.3f, %.3f s after the hover %s"), s, mFirstProgress, mFirstPower,
            mInstantTimeS, *SolidSmokeDescribe(mInstantLook)));
        const TSharedPtr<SSOLSolidTraceLayer> layer = SolidSmokeColumnLayer(mainMenu);
        Check(TEXT("s-main-s0-lights"), layer.IsValid() && layer->GetLitLeds() == 0
            && layer->GetPulseDotAlpha() == 0.0f,
            FString::Printf(TEXT("lit LEDs 0x%02x, dot alpha %.3f"), layer.IsValid() ? layer->GetLitLeds() : 0u,
            layer.IsValid() ? layer->GetPulseDotAlpha() : -1.0f));
        TakeUiShot(TEXT("main-s0"));
        break;
    }
    case ESolidSmokeStep::MainStrengthTwo:
    {
        const USOLMenuButton* button = mainMenu != nullptr ? mainMenu->GetEntryButton(SOLID_SMOKE_STRENGTH_ENTRY)
            : nullptr;
        const FSOLSolidButtonLook look = button != nullptr ? button->GetAppliedLook() : FSOLSolidButtonLook();
        Check(TEXT("s-main-s2"), FMath::IsNearlyEqual(s, 2.0f) && FMath::IsNearlyEqual(look.Glow, 2.0f,
            SOLID_SMOKE_TOLERANCE) && mainMenu->IsEntryHoverTraceShown(SOLID_SMOKE_STRENGTH_ENTRY)
            && mainMenu->GetColumnAnim().Phase == ESOLHoloMenuPhase::Open, FString::Printf(TEXT("strength %.2f: %s"),
            s, *SolidSmokeDescribe(look)));
        TakeUiShot(TEXT("main-s2"));
        break;
    }
    case ESolidSmokeStep::Board:
    {
        // Every disabled button decodes disabled with no amber or cyan; every enabled one does not
        int32 wrong = 0;
        FString first;
        int32 disabledCount = 0;
        for (int32 i = 0; i < static_cast<int32>(ESOLSolidSmokeButton::Count); ++i)
        {
            const USOLMenuButton* button = board != nullptr ? board->GetButton(static_cast<ESOLSolidSmokeButton>(i))
                : nullptr;
            const bool bWantDisabled = button != nullptr && button->IsSolidDisabled();
            disabledCount += bWantDisabled ? 1 : 0;
            const FSOLSolidButtonLook decoded = button != nullptr ? SOLSolidMenu::DecodeButtonTint(
                SOLSolidStyle::UnpackVertexTint(button->GetAppliedTint())) : FSOLSolidButtonLook();
            const bool bOk = button != nullptr && decoded.bDisabled == bWantDisabled
                && (!bWantDisabled || (decoded.Edge <= SOLID_SMOKE_TOLERANCE && !decoded.bSelectedMark));
            if (!bOk && wrong++ == 0)
            {
                first = FString::Printf(TEXT("button %d: %s"), i, *SolidSmokeDescribe(decoded));
            }
        }
        Check(TEXT("d-disabled-art"), board != nullptr && wrong == 0 && disabledCount > 0, FString::Printf(
            TEXT("%d disabled buttons, %d wrong%s%s"), disabledCount, wrong, wrong > 0 ? TEXT(", first ") : TEXT(""),
            *first));
        const FSOLSolidButtonLook tabA = lookOf(ESOLSolidSmokeButton::TabA);
        Check(TEXT("d-selected-hovered"), tabA.bSelectedMark && FMath::IsNearlyEqual(tabA.Edge, 1.0f,
            SOLID_SMOKE_TOLERANCE) && FMath::IsNearlyEqual(tabA.Lift, 1.0f, SOLID_SMOKE_TOLERANCE),
            SolidSmokeDescribe(tabA));
        const USOLSolidPanel* panel = board != nullptr ? board->GetPanel() : nullptr;
        Check(TEXT("d-panel"), panel != nullptr && panel->HasUniqueMaterial() && panel->IsHullMaterialDrawn()
            && panel->GetTraceLayer().IsValid() && panel->GetTraceLayer()->GetTraceCount() == 1,
            FString::Printf(TEXT("unique material %s, %d unique alive"), panel != nullptr
            && panel->HasUniqueMaterial() ? TEXT("yes") : TEXT("no"), solid != nullptr
            ? solid->GetUniqueMaterialCount() : -1));
        TakeUiShot(TEXT("board-selected-hovered"));
        break;
    }
    case ESolidSmokeStep::BoardSplit:
    {
        const FSOLSolidButtonLook tabA = lookOf(ESOLSolidSmokeButton::TabA);
        const FSOLSolidButtonLook tabB = lookOf(ESOLSolidSmokeButton::TabB);
        Check(TEXT("e-selected-only"), tabA.bSelectedMark && tabA.Edge <= SOLID_SMOKE_TOLERANCE,
            SolidSmokeDescribe(tabA));
        Check(TEXT("e-hover-only"), !tabB.bSelectedMark && FMath::IsNearlyEqual(tabB.Edge, 1.0f,
            SOLID_SMOKE_TOLERANCE), SolidSmokeDescribe(tabB));
        TakeUiShot(TEXT("board-hover-and-selected"));
        break;
    }
    case ESolidSmokeStep::StrengthZero:
    {
        // Read this step (EnterStep cleared the read), from the board's button
        const bool bButton = board != nullptr && board->GetButton(ESOLSolidSmokeButton::StackOn) != nullptr;
        Check(TEXT("f-s0-instant"), bButton && mInstantTimeS >= 0.0 && mInstantTimeS < SOL::SolidMenu::HOVER_RAMP_S
            && FMath::IsNearlyZero(s) && SolidSmokeLookMatches(mInstantLook, SolidSmokeSettled(
            ESOLSolidButtonState::Hovered, 0.0f, false)), FString::Printf(TEXT("board button %d, after %.3f s at "
            "strength %.2f: %s"), bButton ? 1 : 0, mInstantTimeS, s, *SolidSmokeDescribe(mInstantLook)));
        break;
    }
    case ESolidSmokeStep::StrengthZeroShot:
        TakeUiShot(TEXT("board-s0"));
        break;
    case ESolidSmokeStep::StrengthTwo:
    {
        const FSOLSolidButtonLook look = lookOf(ESOLSolidSmokeButton::TabB);
        Check(TEXT("g-s2-glow"), FMath::IsNearlyEqual(s, 2.0f) && FMath::IsNearlyEqual(look.Glow, 2.0f,
            SOLID_SMOKE_TOLERANCE), FString::Printf(TEXT("strength %.2f: %s"), s, *SolidSmokeDescribe(look)));
        TakeUiShot(TEXT("board-s2"));
        break;
    }
    case ESolidSmokeStep::ViewSmall:
    {
        const FIntPoint size = GetViewSize();
        Check(TEXT("h-view-small"), size == SOLID_SMOKE_SMALL_VIEW, FString::Printf(TEXT("view %dx%d"), size.X,
            size.Y));
        break;
    }
    case ESolidSmokeStep::SmallControls:
    case ESolidSmokeStep::SmallDemo:
    {
        const bool bControls = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::SmallControls;
        FString columnDetail;
        FString shellDetail;
        const bool bColumn = mainMenu != nullptr && SolidSmokeInside(mainMenu->GetColumnPanel(), mainMenu,
            columnDetail);
        const bool bShell = mainMenu != nullptr && SolidSmokeInside(mainMenu->GetShellPanel(), mainMenu,
            shellDetail);
        Check(bControls ? TEXT("h-fit-controls") : TEXT("h-fit-demo"), bColumn && bShell && mainMenu->GetPage()
            == (bControls ? ESOLMainMenuPage::Controls : ESOLMainMenuPage::Demo), FString::Printf(
            TEXT("column %s, shell %s"), *columnDetail, *shellDetail));
        if (bControls)
        {
            CheckReadOnlyPage(TEXT("h-controls"), mainMenu != nullptr ? mainMenu->GetControlsPage() : nullptr);
        }
        else
        {
            CheckDemoLayout(TEXT("h-demo"), menu);
        }
        TakeUiShot(bControls ? TEXT("small-controls") : TEXT("small-demo"));
        break;
    }
    case ESolidSmokeStep::SmallAbout:
    {
        // About at 1000x800: the column and the shell inside the view, the page's rows and rail inside the shell
        FString columnDetail;
        FString shellDetail;
        FString pageDetail;
        USOLReadOnlyPageWidget* about = mainMenu != nullptr ? mainMenu->GetAboutPage() : nullptr;
        const bool bColumn = mainMenu != nullptr && SolidSmokeInside(mainMenu->GetColumnPanel(), mainMenu,
            columnDetail);
        const bool bShell = mainMenu != nullptr && SolidSmokeInside(mainMenu->GetShellPanel(), mainMenu,
            shellDetail);
        const bool bPage = about != nullptr && mainMenu != nullptr && SolidSmokeInside(about,
            mainMenu->GetShellPanel(), pageDetail);
        Check(TEXT("h-fit-about"), bColumn && bShell && bPage && mainMenu->GetPage() == ESOLMainMenuPage::About,
            FString::Printf(TEXT("column %s, shell %s, page %s"), *columnDetail, *shellDetail, *pageDetail));
        CheckReadOnlyPage(TEXT("h-about"), about);
        if (about != nullptr)
        {
            CheckRailFits(TEXT("h-about-rail"), about->GetRail());
        }
        else
        {
            Check(TEXT("h-about-rail"), false, TEXT("no About page"));
        }
        TakeUiShot(TEXT("small-about"));
        break;
    }
    case ESolidSmokeStep::SmallSettings:
    case ESolidSmokeStep::SmallStart:
    {
        // The column and the shell inside the view (the shell narrows to the room right of the column)
        const bool bSettings = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::SmallSettings;
        FString columnDetail;
        FString shellDetail;
        const bool bColumn = mainMenu != nullptr && SolidSmokeInside(mainMenu->GetColumnPanel(), mainMenu,
            columnDetail);
        const bool bShell = mainMenu != nullptr && SolidSmokeInside(mainMenu->GetShellPanel(), mainMenu,
            shellDetail);
        Check(bSettings ? TEXT("h-fit-settings") : TEXT("h-fit-start"), bColumn && bShell && mainMenu->GetPage()
            == (bSettings ? ESOLMainMenuPage::Settings : ESOLMainMenuPage::StartLocation), FString::Printf(
            TEXT("column %s, shell %s"), *columnDetail, *shellDetail));
        if (bSettings)
        {
            CheckSettingsFit(menu, mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr,
                TEXT("h-small-stepper-values"), TEXT("h-small-labels"));
        }

        // The shot shows the pending window mode: SmallGameplay reverts it after the shot
        TakeUiShot(bSettings ? TEXT("small-settings") : TEXT("small-start"));
        break;
    }
    case ESolidSmokeStep::SmallGameplay:
    case ESolidSmokeStep::SmallKeys:
    {
        // The narrow rows at 1000x800 (46i): the key names beside a narrower Rebind, the sliders' room
        const bool bKeys = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::SmallKeys;
        CheckSettingsRoom(bKeys ? TEXT("h-small-keys") : TEXT("h-small-gameplay"), mainMenu != nullptr
            ? mainMenu->GetSettingsPage() : nullptr, bKeys);
        if (bKeys)
        {
            CheckSettingsKeyCaps(TEXT("h-small-keycaps"), mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr);
        }
        TakeUiShot(bKeys ? TEXT("small-keys") : TEXT("small-gameplay"));
        break;
    }
    case ESolidSmokeStep::PauseSmallKeys:
    {
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        CheckSettingsRoom(TEXT("h-pause-small-keys"), pause != nullptr ? pause->GetSettingsPage() : nullptr, true);
        CheckSettingsKeyCaps(TEXT("h-pause-small-keycaps"), pause != nullptr ? pause->GetSettingsPage() : nullptr);
        TakeUiShot(TEXT("pause-small-keys"));
        break;
    }
    case ESolidSmokeStep::ViewBack:
    {
        const FIntPoint size = GetViewSize();
        Check(TEXT("j-view"), size == SOLID_SMOKE_VIEW && mainMenu != nullptr && mainMenu->GetPage()
            == ESOLMainMenuPage::None, FString::Printf(TEXT("view %dx%d, page %d"), size.X, size.Y, mainMenu != nullptr
            ? static_cast<int32>(mainMenu->GetPage()) : -1));
        break;
    }
    case ESolidSmokeStep::MainFit:
    {
        // Read a settle after the open: the slide long over and the page painted in place since, so the cached
        // geometry is current (46f could not read it on the sliding first open; TECH_DEBT); the rails (D17)
        const FSolidSmokeFitPage& fit = SOLID_SMOKE_FIT_PAGES[mFitIndex];
        const bool bOpen = mainMenu != nullptr && mainMenu->GetPage() == fit.Page
            && mainMenu->GetShellAnim().Phase == ESOLHoloMenuPhase::Open;
        Check(*FString::Printf(TEXT("j-%s-open"), fit.Name), bOpen, FString::Printf(TEXT("page %d (want %d), shell "
            "phase %d"), mainMenu != nullptr ? static_cast<int32>(mainMenu->GetPage()) : -1,
            static_cast<int32>(fit.Page), mainMenu != nullptr ? static_cast<int32>(mainMenu->GetShellAnim().Phase)
            : -1));
        USOLReadOnlyPageWidget* readOnly = mainMenu == nullptr ? nullptr : fit.Page == ESOLMainMenuPage::Controls
            ? mainMenu->GetControlsPage() : fit.Page == ESOLMainMenuPage::About ? mainMenu->GetAboutPage() : nullptr;
        USOLSettingsPageWidget* settings = mainMenu != nullptr && fit.Page == ESOLMainMenuPage::Settings
            ? mainMenu->GetSettingsPage() : nullptr;
        if (readOnly != nullptr)
        {
            CheckReadOnlyPage(*FString::Printf(TEXT("j-%s"), fit.Name), readOnly);
            FString inShell;
            Check(*FString::Printf(TEXT("j-%s-in-shell"), fit.Name), SolidSmokeInside(readOnly,
                mainMenu->GetShellPanel(), inShell), inShell);
            CheckRailFits(*FString::Printf(TEXT("j-%s-rail"), fit.Name), readOnly->GetRail());
        }
        else if (settings != nullptr)
        {
            // Back at 1280x720 after the 1000x800 views: the narrow rows are wide again (46i, narrow to wide)
            CheckRailFits(TEXT("j-settings-rail"), settings->GetRail());
            CheckSettingsWide(TEXT("j-settings-wide"), settings);
        }
        else
        {
            // A missing page fails its rail check (and Settings its wide check) instead of skipping it
            Check(*FString::Printf(TEXT("j-%s-rail"), fit.Name), false, TEXT("no page"));
            if (fit.Page == ESOLMainMenuPage::Settings)
            {
                Check(TEXT("j-settings-wide"), false, TEXT("no Settings page"));
            }
        }
        TakeUiShot(*FString::Printf(TEXT("fit-%s"), fit.Name));
        if (++mFitIndex < SOLID_SMOKE_FIT_PAGE_COUNT)
        {
            mRepeatStep = static_cast<int32>(ESolidSmokeStep::MainFit);
        }
        break;
    }
    case ESolidSmokeStep::PauseFlight:
    {
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        Check(TEXT("v-flight"), menu.GetState().Mode == SOLMenuState::EMenuMode::Playing
            && (pause == nullptr || !pause->IsInViewport()), FString::Printf(TEXT("mode %d, pause menu in the viewport "
            "%d"), static_cast<int32>(menu.GetState().Mode), pause != nullptr && pause->IsInViewport() ? 1 : 0));
        break;
    }
    case ESolidSmokeStep::PauseOpen:
    {
        // The slide in from past the left edge, the dim up to PAUSE_DIM_ALPHA (the live view only dimmed, Q3), Resume
        // focused with no page; the 7-tab stack fits without a scroll (D17)
        USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        if (pause == nullptr)
        {
            Check(TEXT("v-pause-open"), false, TEXT("no pause menu"));
            break;
        }
        const FSOLPausePanelLook& look = pause->GetPanelLook();
        const USOLMenuButton* resume = pause->GetTabButton(SOLMenuState::EMenuTab::Resume);
        const USOLSolidPanel* shell = pause->GetShellPanel();
        const bool bEnd = pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Open && look.SlideX == 0.0f
            && FMath::IsNearlyEqual(look.Progress, 1.0f) && look.bInteractive && FMath::IsNearlyEqual(
            pause->GetDimAlpha(), SOL::SolidMenu::PAUSE_DIM_ALPHA, SOLID_SMOKE_TOLERANCE)
            && pause->GetVisibility() != ESlateVisibility::HitTestInvisible;
        // (46i, user, Q9) The selected Resume tab carries the cyan mark like every other selected item, alone
        int32 marked = 0;
        for (int32 tab = 0; tab < pause->GetTabButtonCount(); ++tab)
        {
            const USOLMenuButton* button = pause->GetTabButton(static_cast<SOLMenuState::EMenuTab>(tab));
            marked += button != nullptr && button->IsSelected() ? 1 : 0;
        }
        const bool bResumeMarked = resume != nullptr && resume->IsSelected()
            && resume->GetAppliedLook().bSelectedMark && marked == 1;
        const bool bResume = menu.GetState().Tab == SOLMenuState::EMenuTab::Resume
            && pause->GetTab() == SOLMenuState::EMenuTab::Resume && resume != nullptr && resume->HasKeyboardFocus()
            && !pause->AreQuitChoicesShown() && bResumeMarked;
        const bool bNoPage = shell != nullptr && shell->GetVisibility() == ESlateVisibility::Hidden
            && pause->GetShellAnim().Phase == ESOLHoloMenuPhase::Closed && pause->GetShownPageIndex() == 0;
        Check(TEXT("v-pause-open"), mFirstSlideX < 0.0f && mFirstProgress >= 0.0f && mFirstProgress < 1.0f
            && mSawMidProgress && mIsMonotonic && bEnd && bResume && bNoPage, FString::Printf(TEXT("first frame slide "
            "%.1f progress %.3f, mid %d, monotonic %d; end phase %d slide %.1f progress %.3f dim %.3f (want %.2f), "
            "interactive %d; Resume tab %d focused %d marked %d (%d marked), Quit choices %d, shell hidden and closed "
            "%d"), mFirstSlideX,
            mFirstProgress, mSawMidProgress ? 1 : 0, mIsMonotonic ? 1 : 0,
            static_cast<int32>(pause->GetPanelAnim().Phase), look.SlideX, look.Progress, pause->GetDimAlpha(),
            SOL::SolidMenu::PAUSE_DIM_ALPHA, look.bInteractive ? 1 : 0,
            menu.GetState().Tab == SOLMenuState::EMenuTab::Resume ? 1 : 0,
            resume != nullptr && resume->HasKeyboardFocus() ? 1 : 0, bResumeMarked ? 1 : 0, marked,
            pause->AreQuitChoicesShown() ? 1 : 0, bNoPage ? 1 : 0));
        CheckCueDeltas(TEXT("v-pause-open-cue"), menu, 1, -1, -1);

        // The stack fits; the focused Resume tab (hover is the focus) draws its hover trace on the side panel
        FSOLSolidScrollStack& stack = pause->GetTabStack();
        FString lastDetail = TEXT("missing");
        const bool bLast = SolidSmokeInside(pause->GetTabButton(SOLMenuState::EMenuTab::Quit), stack.Scroll,
            lastDetail);
        const bool bResumeTrace = pause->IsTabHoverTraceShown(SOLMenuState::EMenuTab::Resume)
            && !pause->IsTabHoverTraceShown(SOLMenuState::EMenuTab::Quit);
        Check(TEXT("v-pause-stack"), pause->GetTabButtonCount() == static_cast<int32>(SOLMenuState::EMenuTab::Quit) + 1
            && stack.GetViewHeight() > 0.0f && !stack.NeedsScroll() && stack.GetScrollOffset() == 0.0f && bLast
            && bResumeTrace, FString::Printf(TEXT("%d tabs: content %.1f in a %.1f view, offset %.1f, needs scroll %d, "
            "Quit %s; Resume's hover trace %d"), pause->GetTabButtonCount(), stack.GetContentHeight(),
            stack.GetViewHeight(), stack.GetScrollOffset(), stack.NeedsScroll() ? 1 : 0, *lastDetail,
            bResumeTrace ? 1 : 0));
        TakeUiShot(TEXT("pause"));
        break;
    }
    case ESolidSmokeStep::PausePage:
    {
        // The page in the shell right of the side panel (Q4), its tab selected; the read-only pages' fit at 1280x720
        // through the real pause path, and every rail without a scroll (D17)
        const FSolidSmokePausePage& entry = SOLID_SMOKE_PAUSE_PAGES[mPausePageIndex];
        USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        const USOLSolidPanel* shell = pause != nullptr ? pause->GetShellPanel() : nullptr;
        int32 marks = 0;
        for (int32 tab = 0; pause != nullptr && tab < pause->GetTabButtonCount(); ++tab)
        {
            const USOLMenuButton* button = pause->GetTabButton(static_cast<SOLMenuState::EMenuTab>(tab));
            marks += button != nullptr && button->IsSelected() ? 1 : 0;
        }
        const USOLMenuButton* tabButton = pause != nullptr ? pause->GetTabButton(entry.Tab) : nullptr;
        const bool bOpen = pause != nullptr && shell != nullptr
            && menu.GetState().Mode == SOLMenuState::EMenuMode::Paused && menu.GetState().Tab == entry.Tab
            && pause->GetTab() == entry.Tab && pause->GetShownPageIndex() == static_cast<int32>(entry.Tab)
            && pause->GetShellAnim().Phase == ESOLHoloMenuPhase::Open
            && FMath::IsNearlyEqual(pause->GetShellProgress(), 1.0f)
            && shell->GetVisibility() == ESlateVisibility::Visible && shell->IsHullMaterialDrawn()
            && pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Open
            && !pause->AreQuitChoicesShown() && marks == 1 && tabButton != nullptr && tabButton->IsSelected();
        Check(*FString::Printf(TEXT("v-%s-open"), entry.Name), bOpen && mFirstProgress >= 0.0f && mFirstProgress < 1.0f
            && mSawMidProgress && mIsMonotonic, FString::Printf(TEXT("tab %d (want %d), shown %d, shell phase %d "
            "progress %.3f, first progress %.3f, mid %d, monotonic %d, %d selected mark(s), side panel phase %d"),
            static_cast<int32>(menu.GetState().Tab), static_cast<int32>(entry.Tab), pause != nullptr
            ? pause->GetShownPageIndex() : -1, pause != nullptr ? static_cast<int32>(pause->GetShellAnim().Phase) : -1,
            pause != nullptr ? pause->GetShellProgress() : -1.0f, mFirstProgress, mSawMidProgress ? 1 : 0,
            mIsMonotonic ? 1 : 0, marks, pause != nullptr ? static_cast<int32>(pause->GetPanelAnim().Phase) : -1));
        USOLReadOnlyPageWidget* readOnly = pause == nullptr ? nullptr : entry.Tab == SOLMenuState::EMenuTab::Controls
            ? pause->GetControlsPage() : entry.Tab == SOLMenuState::EMenuTab::Info ? pause->GetInfoPage()
            : entry.Tab == SOLMenuState::EMenuTab::About ? pause->GetAboutPage() : nullptr;
        if (readOnly != nullptr)
        {
            CheckReadOnlyPage(*FString::Printf(TEXT("v-%s"), entry.Name), readOnly);
            FString inShell;
            Check(*FString::Printf(TEXT("v-%s-in-shell"), entry.Name), SolidSmokeInside(readOnly,
                pause->GetShellPanel(), inShell), inShell);
            CheckRailFits(*FString::Printf(TEXT("v-%s-rail"), entry.Name), readOnly->GetRail());
        }
        else if (entry.Tab == SOLMenuState::EMenuTab::Settings && pause != nullptr
            && pause->GetSettingsPage() != nullptr)
        {
            CheckRailFits(TEXT("v-settings-rail"), pause->GetSettingsPage()->GetRail());
        }
        else if (entry.Tab != SOLMenuState::EMenuTab::Demo)
        {
            // A page with a rail but no page widget fails its rail check instead of skipping it (Demo has no rail)
            Check(*FString::Printf(TEXT("v-%s-rail"), entry.Name), false, TEXT("no pause menu or page"));
        }
        TakeUiShot(*FString::Printf(TEXT("pause-%s"), entry.Name));
        break;
    }
    case ESolidSmokeStep::PausePageClose:
    {
        // Esc or Back closed the page at once (still paused, Resume focused); the shell closed after, input-transparent
        // meanwhile; the side panel stayed open
        const FSolidSmokePausePage& entry = SOLID_SMOKE_PAUSE_PAGES[mPausePageIndex];
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        const USOLSolidPanel* shell = pause != nullptr ? pause->GetShellPanel() : nullptr;
        const bool bClosed = pause != nullptr && shell != nullptr
            && menu.GetState().Mode == SOLMenuState::EMenuMode::Paused && pause->IsInViewport()
            && pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Open
            && pause->GetShellAnim().Phase == ESOLHoloMenuPhase::Closed
            && shell->GetVisibility() == ESlateVisibility::Hidden && pause->GetShownPageIndex() == 0
            && pause->GetTab() == SOLMenuState::EMenuTab::Resume;
        Check(*FString::Printf(TEXT("v-%s-close-%s"), entry.Name, entry.bBack ? TEXT("back") : TEXT("esc")),
            mWasClosedAtOnce && mFirstProgress > 0.0f && mSawMidProgress && mSawHitTestInvisible && mIsMonotonic
            && bClosed, FString::Printf(TEXT("paused on Resume, focused, at once %d, first progress %.3f, mid %d, "
            "input-transparent %d, monotonic %d, shell closed and hidden with the side panel open %d"),
            mWasClosedAtOnce ? 1 : 0, mFirstProgress, mSawMidProgress ? 1 : 0, mSawHitTestInvisible ? 1 : 0,
            mIsMonotonic ? 1 : 0, bClosed ? 1 : 0));
        if (++mPausePageIndex < SOLID_SMOKE_PAUSE_PAGE_COUNT)
        {
            mRepeatStep = static_cast<int32>(ESolidSmokeStep::PausePage);
        }
        break;
    }
    case ESolidSmokeStep::PauseQuitTab:
    {
        // Q4: the two ways out inside the side panel, and no page
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        if (pause == nullptr)
        {
            Check(TEXT("v-quit-choices"), false, TEXT("no pause menu"));
            break;
        }
        const USOLSolidPanel* shell = pause->GetShellPanel();
        FString toMenu = TEXT("missing");
        FString toDesktop = TEXT("missing");
        const bool bToMenu = SolidSmokeInside(pause->GetQuitButton(0), pause->GetSidePanel(), toMenu);
        const bool bToDesktop = SolidSmokeInside(pause->GetQuitButton(1), pause->GetSidePanel(), toDesktop);
        const USOLMenuButton* quitTab = pause->GetTabButton(SOLMenuState::EMenuTab::Quit);
        const bool bNoPage = shell != nullptr && shell->GetVisibility() == ESlateVisibility::Hidden
            && pause->GetShellAnim().Phase == ESOLHoloMenuPhase::Closed && pause->GetShownPageIndex() == 0;
        Check(TEXT("v-quit-choices"), menu.GetState().Tab == SOLMenuState::EMenuTab::Quit
            && pause->GetTab() == SOLMenuState::EMenuTab::Quit && pause->AreQuitChoicesShown() && bToMenu && bToDesktop
            && bNoPage && quitTab != nullptr && quitTab->IsSelected(), FString::Printf(TEXT("tab %d, choices shown %d, "
            "to menu %s, to desktop %s, no page %d, Quit tab selected %d"), static_cast<int32>(menu.GetState().Tab),
            pause->AreQuitChoicesShown() ? 1 : 0, *toMenu, *toDesktop, bNoPage ? 1 : 0, quitTab != nullptr
            && quitTab->IsSelected() ? 1 : 0));
        TakeUiShot(TEXT("pause-quit"));
        break;
    }
    case ESolidSmokeStep::PauseStrengthTwo:
    case ESolidSmokeStep::PauseSmall:
    {
        // Paused with the Settings page open in the shell: the side panel and the shell shown, the shell open (the
        // pause menu fills the view, so it is the outer rect); at strength 2 the Settings tab's glow is read too
        const bool bSmall = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::PauseSmall;
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        const USOLSolidPanel* shell = pause != nullptr ? pause->GetShellPanel() : nullptr;
        const USOLMenuButton* tab = pause != nullptr ? pause->GetTabButton(SOLMenuState::EMenuTab::Settings) : nullptr;
        const FSOLSolidButtonLook look = tab != nullptr ? tab->GetAppliedLook() : FSOLSolidButtonLook();
        const bool bOpen = pause != nullptr && shell != nullptr
            && menu.GetState().Mode == SOLMenuState::EMenuMode::Paused
            && menu.GetState().Tab == SOLMenuState::EMenuTab::Settings
            && pause->GetShownPageIndex() == static_cast<int32>(SOLMenuState::EMenuTab::Settings)
            && pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Open
            && pause->GetShellAnim().Phase == ESOLHoloMenuPhase::Open
            && shell->GetVisibility() == ESlateVisibility::Visible && pause->IsInViewport();
        FString sideDetail;
        FString shellDetail;
        const bool bSide = pause != nullptr && SolidSmokeInside(pause->GetSidePanel(), pause, sideDetail);
        const bool bShell = pause != nullptr && SolidSmokeInside(shell, pause, shellDetail);
        if (bSmall)
        {
            const FIntPoint size = GetViewSize();
            Check(TEXT("h-pause-small"), bOpen && bSide && bShell && size == SOLID_SMOKE_SMALL_VIEW, FString::Printf(
                TEXT("view %dx%d, paused with Settings open %d, side panel %s, shell %s"), size.X, size.Y,
                bOpen ? 1 : 0, *sideDetail, *shellDetail));

            // 46i: the pause page's stepper values (every window mode) in their slots, the labels in their columns,
            // and each stepper's value text inside the shell
            const USOLSettingsPageWidget* page = pause != nullptr ? pause->GetSettingsPage() : nullptr;
            CheckSettingsFit(menu, page, TEXT("h-pause-small-stepper-values"), TEXT("h-pause-small-labels"));
            bool bValuesInShell = page != nullptr && shell != nullptr;
            FString valuesDetail;
            for (int32 stepper = 0; page != nullptr && stepper < static_cast<int32>(ESOLSettingsStepper::Count);
                ++stepper)
            {
                FString detail;
                const bool bInside = SolidSmokeInside(page->GetStepperValueText(static_cast<ESOLSettingsStepper>(
                    stepper)), shell, detail);
                bValuesInShell &= bInside;
                valuesDetail += FString::Printf(TEXT(" [stepper %d %s%s]"), stepper, *detail, bInside ? TEXT("")
                    : TEXT(" OUTSIDE"));
            }
            Check(TEXT("h-pause-small-values-in-shell"), bValuesInShell, page != nullptr ? valuesDetail
                : FString(TEXT("no pause Settings page")));
            TakeUiShot(TEXT("pause-small"));
        }
        else
        {
            Check(TEXT("v-pause-s2"), FMath::IsNearlyEqual(s, 2.0f) && bOpen && bSide && bShell, FString::Printf(
                TEXT("strength %.2f: Settings open %d, side panel %s, shell %s, Settings tab %s"), s, bOpen ? 1 : 0,
                *sideDetail, *shellDetail, *SolidSmokeDescribe(look)));
            TakeUiShot(TEXT("pause-s2"));

            // Strength 1 applied back (applied, not pending: see PauseStrengthZero)
            ApplyStrength(menu, SOL::Settings::HUD_EFFECTS_DEFAULT);
        }
        break;
    }
    case ESolidSmokeStep::PauseKeys:
    {
        // 33b: the key cap cells at 1280x720 (PauseSmall shows Graphics again once the shot is taken)
        const USOLPauseMenuWidget* pause = menu.GetPauseMenu();
        CheckSettingsKeyCaps(TEXT("v-keycaps"), pause != nullptr ? pause->GetSettingsPage() : nullptr);
        TakeUiShot(TEXT("pause-keys"));
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the live HUD effects strength in the smoke slot's pending settings (nothing is saved)
void FSOLSolidMenuSmoke::SetStrength(USOLMenuSubsystem& menu, const float strength) const
{
    USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
    if (settings == nullptr)
    {
        return;
    }
    SOLSettingsModel::FSettingsValues values = settings->GetLiveValues();
    values.HudEffectsStrength = strength;
    settings->SetPendingValues(values);
}

//////////////////////////////////////////////////////////////////////////
// Returns the game viewport's size in pixels (zero without a controller)
FIntPoint FSOLSolidMenuSmoke::GetViewSize() const
{
    FIntPoint size = FIntPoint::ZeroValue;
    if (mController.IsValid())
    {
        mController->GetViewportSize(size.X, size.Y);
    }
    return size;
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, removes the board and quits
void FSOLSolidMenuSmoke::Finish(USOLMenuSubsystem& menu)
{
    if (mIsFinished)
    {
        return;
    }
    mIsFinished = true;
    if (mRailPage.IsValid())
    {
        mRailPage->RemoveFromParent();
    }
    if (mDiagramPage.IsValid())
    {
        mDiagramPage->RemoveFromParent();
    }
    if (USOLSolidMenuSmokeBoard* board = mBoard.Get())
    {
        board->RemoveFromParent();
    }
    // A run that logged another number of checks than a whole run does skipped (or doubled) one: a failure
    UE_LOG(LogSOL, Log, TEXT("SolidMenu: %d checks run (a whole run: %d)"), mChecksRun, SOLID_SMOKE_EXPECTED_CHECKS);
    if (mChecksRun != SOLID_SMOKE_EXPECTED_CHECKS)
    {
        Check(TEXT("z-check-count"), false, FString::Printf(TEXT("%d checks run, a whole run logs %d"), mChecksRun,
            SOLID_SMOKE_EXPECTED_CHECKS));
    }
    UE_LOG(LogSOL, Log, TEXT("SolidMenu SUMMARY %d/%d checks passed"), mChecksPassed, mChecksRun);
    UKismetSystemLibrary::QuitGame(menu.GetWorld(), nullptr, EQuitPreference::Quit, false);
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot with the UI to the screenshot folder
void FSOLSolidMenuSmoke::TakeUiShot(const TCHAR* name)
{
    mIsShotPending = true;
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("SolidMenu_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SolidMenu: screenshot with UI %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Clicks a main menu entry as the mouse does: hovers it (it takes keyboard focus), then its click handler
void FSOLSolidMenuSmoke::ClickEntry(USOLMenuSubsystem& menu, const int32 entry) const
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLMenuButton* button = mainMenu != nullptr ? mainMenu->GetEntryButton(entry) : nullptr;
    if (button != nullptr)
    {
        button->NotifyHovered();
        button->OnMenuClicked.ExecuteIfBound(button->GetIndex());
    }
}

//////////////////////////////////////////////////////////////////////////
// Samples an animation: the first progress and power, values strictly between the ends, the direction kept
void FSOLSolidMenuSmoke::SampleAnim(const float progress, const float power, const bool bShellHitTestInvisible)
{
    if (mSamples++ == 0)
    {
        mFirstProgress = progress;
        mFirstPower = power;
    }
    else if (mIsOpeningSample ? progress < mLastProgress - SOLID_SMOKE_TOLERANCE
        : progress > mLastProgress + SOLID_SMOKE_TOLERANCE)
    {
        mIsMonotonic = false;
    }
    mLastProgress = progress;
    mSawMidProgress |= progress > SOLID_SMOKE_TOLERANCE && progress < 1.0f - SOLID_SMOKE_TOLERANCE;
    mSawMidPower |= power > SOLID_SMOKE_TOLERANCE && power < 1.0f - SOLID_SMOKE_TOLERANCE;
    mSawHitTestInvisible |= bShellHitTestInvisible && progress > 0.0f && progress < 1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLSolidMenuSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SolidMenu CHECK %-22s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}

//////////////////////////////////////////////////////////////////////////
// Checks how many open, hover and select cues started since the step began (a negative want is not checked)
void FSOLSolidMenuSmoke::CheckCueDeltas(const TCHAR* label, const USOLMenuSubsystem& menu, const int32 wantOpen,
    const int32 wantHover, const int32 wantSelect)
{
    const int32 open = SolidSmokeCues(menu, ESOLHoloCue::Open) - mCueOpenAtStart;
    const int32 hover = SolidSmokeCues(menu, ESOLHoloCue::Hover) - mCueHoverAtStart;
    const int32 select = SolidSmokeCues(menu, ESOLHoloCue::Select) - mCueSelectAtStart;
    const bool bHasCues = SolidSmokeCues(menu, ESOLHoloCue::Open) >= 0;
    Check(label, bHasCues && (wantOpen < 0 || open == wantOpen) && (wantHover < 0 || hover == wantHover)
        && (wantSelect < 0 || select == wantSelect), FString::Printf(TEXT("cues played in the step: open %d (want %d), "
        "hover %d (want %d), select %d (want %d); -1 wants are not checked"), open, wantOpen, hover, wantHover, select,
        wantSelect));
}

//////////////////////////////////////////////////////////////////////////
// Checks that no open cue started since the reopen during a close (mCueOpenBeforeReopen, read just before it)
void FSOLSolidMenuSmoke::CheckReopenNoCue(const TCHAR* label, const USOLMenuSubsystem& menu)
{
    const int32 now = SolidSmokeCues(menu, ESOLHoloCue::Open);
    Check(label, mCueOpenBeforeReopen >= 0 && now == mCueOpenBeforeReopen, FString::Printf(TEXT("open cues %d "
        "before the reopen, %d a settle after (a reversal plays none)"), mCueOpenBeforeReopen, now));
}

//////////////////////////////////////////////////////////////////////////
// Clicks a Settings tab as the mouse does: hovers its button (it takes keyboard focus), then its click handler
void FSOLSolidMenuSmoke::ClickSettingsTab(USOLSettingsPageWidget* page, const ESOLSettingsTab tab) const
{
    USOLMenuButton* button = page != nullptr ? page->GetTabButton(tab) : nullptr;
    if (button != nullptr)
    {
        button->NotifyHovered();
        button->OnMenuClicked.ExecuteIfBound(button->GetIndex());
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the Settings steps' checks (SDD 14 step 4) and screenshots
void FSOLSolidMenuSmoke::ExitSettingsStep(const int32 step, USOLMenuSubsystem& menu)
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(&menu);
    const float s = solid != nullptr ? solid->GetStrength() : -1.0f;

    // Describes the selected tab, the content shown and the tab buttons' selected marks
    const auto describeTabs = [page]()
    {
        FString text = FString::Printf(TEXT("selected %d, shown %d, marks"), page != nullptr
            ? static_cast<int32>(page->GetSelectedTab()) : -1, page != nullptr ? page->GetShownContentIndex() : -1);
        for (int32 tab = 0; tab < static_cast<int32>(ESOLSettingsTab::Count); ++tab)
        {
            const USOLMenuButton* button = page != nullptr ? page->GetTabButton(static_cast<ESOLSettingsTab>(tab))
                : nullptr;
            text += button != nullptr && button->IsSelected() ? TEXT(" 1") : TEXT(" 0");
        }
        return text;
    };
    // Returns whether the page shows a tab: selected, its content shown, only its button selected
    const auto showsTab = [page](const ESOLSettingsTab want)
    {
        if (page == nullptr || page->GetSelectedTab() != want
            || page->GetShownContentIndex() != static_cast<int32>(want))
        {
            return false;
        }
        for (int32 tab = 0; tab < static_cast<int32>(ESOLSettingsTab::Count); ++tab)
        {
            const USOLMenuButton* button = page->GetTabButton(static_cast<ESOLSettingsTab>(tab));
            if (button == nullptr || button->IsSelected() != (tab == static_cast<int32>(want)))
            {
                return false;
            }
        }
        return true;
    };
    // Returns whether the footer (Revert, Apply: Small buttons) is laid out on screen under the content
    const auto footerShown = [page]()
    {
        const USOLMenuButton* revert = page != nullptr ? page->GetRevertButton() : nullptr;
        const USOLMenuButton* apply = page != nullptr ? page->GetApplyButton() : nullptr;
        return revert != nullptr && apply != nullptr && revert->IsVisible() && apply->IsVisible()
            && revert->GetCachedGeometry().GetLocalSize().X > 0.0f && apply->GetCachedGeometry().GetLocalSize().X > 0.0f
            && revert->GetVariant() == SOLSettingsTabs::RowButtonVariant()
            && apply->GetVariant() == SOLSettingsTabs::RowButtonVariant();
    };

    switch (static_cast<ESolidSmokeStep>(step))
    {
    case ESolidSmokeStep::SettingsOpen:
    {
        // The tab column: six Tab buttons on the hull material
        int32 tabs = 0;
        for (int32 tab = 0; tab < static_cast<int32>(ESOLSettingsTab::Count); ++tab)
        {
            const USOLMenuButton* button = page != nullptr ? page->GetTabButton(static_cast<ESOLSettingsTab>(tab))
                : nullptr;
            tabs += button != nullptr && button->GetVariant() == SOLSettingsTabs::TabButtonVariant()
                && button->IsHullMaterialDrawn() ? 1 : 0;
        }
        const bool bOpen = mainMenu != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::Settings;
        Check(TEXT("u-settings-first-tab"), bOpen && tabs == static_cast<int32>(ESOLSettingsTab::Count)
            && showsTab(ESOLSettingsTab::Graphics) && footerShown(), FString::Printf(TEXT("page open %d, %d solid Tab "
            "buttons, %s, footer %d"), bOpen ? 1 : 0, tabs, *describeTabs(), footerShown() ? 1 : 0));

        // The rows' controls: the solid slider and toggle styles (hull material brushes), Small stepper buttons
        int32 solidSliders = 0;
        for (int32 slider = 0; slider < static_cast<int32>(ESOLSettingsSlider::Count); ++slider)
        {
            const USlider* control = page != nullptr ? page->GetSlider(static_cast<ESOLSettingsSlider>(slider))
                : nullptr;
            solidSliders += control != nullptr
                && control->GetWidgetStyle().NormalBarImage.GetResourceObject() != nullptr
                && control->GetWidgetStyle().NormalThumbImage.GetResourceObject() != nullptr ? 1 : 0;
        }
        int32 solidToggles = 0;
        for (int32 toggle = 0; toggle < static_cast<int32>(ESOLSettingsToggle::Count); ++toggle)
        {
            const UCheckBox* control = page != nullptr ? page->GetToggle(static_cast<ESOLSettingsToggle>(toggle))
                : nullptr;
            solidToggles += control != nullptr
                && control->GetWidgetStyle().UncheckedImage.GetResourceObject() != nullptr ? 1 : 0;
        }
        int32 smallSteppers = 0;
        for (int32 stepper = 0; stepper < static_cast<int32>(ESOLSettingsStepper::Count); ++stepper)
        {
            for (int32 side = 0; side < 2; ++side)
            {
                const USOLMenuButton* button = page != nullptr
                    ? page->GetStepperButton(static_cast<ESOLSettingsStepper>(stepper), side) : nullptr;
                smallSteppers += button != nullptr && button->GetVariant() == SOLSettingsTabs::RowButtonVariant()
                    && button->IsHullMaterialDrawn() ? 1 : 0;
            }
        }
        Check(TEXT("u-settings-solid-rows"), solidSliders == static_cast<int32>(ESOLSettingsSlider::Count)
            && solidToggles == static_cast<int32>(ESOLSettingsToggle::Count)
            && smallSteppers == 2 * static_cast<int32>(ESOLSettingsStepper::Count), FString::Printf(TEXT("%d solid "
            "sliders, %d solid toggles, %d Small stepper buttons"), solidSliders, solidToggles, smallSteppers));
        CheckSettingsFit(menu, page, TEXT("u-stepper-values"), TEXT("u-labels"));
        CheckSettingsWide(TEXT("u-settings-wide"), page);
        TakeUiShot(TEXT("settings-graphics"));
        break;
    }
    case ESolidSmokeStep::SettingsTab:
    {
        // On Overlays an embedded row is hovered too (for the screenshot): when the last input was the mouse, hover
        // wins over the focused tab (46e follow-up) and its trace is dark; after a key the focus keeps it
        const ESOLSettingsTab tab = static_cast<ESOLSettingsTab>(mSettingsTab);
        const bool bTrace = page != nullptr && page->IsTabHoverTraceShown(tab);
        const bool bElsewhere = page != nullptr && page->IsHoverElsewhere();
        const bool bTraceOk = bElsewhere ? !bTrace && tab == ESOLSettingsTab::Overlays : bTrace;
        Check(*FString::Printf(TEXT("u-tab-%s"), SOLID_SMOKE_TAB_NAMES[mSettingsTab]), showsTab(tab) && bTraceOk
            && footerShown(), FString::Printf(TEXT("%s, hover trace %d (hover elsewhere %d), footer %d"),
            *describeTabs(), bTrace ? 1 : 0, bElsewhere ? 1 : 0, footerShown() ? 1 : 0));
        if (tab == ESOLSettingsTab::Overlays)
        {
            // The embedded Overlay list in the solid look: the menu font, its row hovered (the screenshot shows it)
            const USOLOverlayMenuWidget* list = page != nullptr ? page->GetOverlayList() : nullptr;
            const UObject* font = list != nullptr ? list->GetRowFontObject() : nullptr;
            const bool bSolidFont = font != nullptr && solid != nullptr && font == solid->GetMenuFont();
            Check(TEXT("u-overlay-solid"), list != nullptr && list->IsEmbedded() && bSolidFont
                && list->GetEmbeddedHoverRow() == SOLID_SMOKE_OVERLAY_HOVER_ROW, FString::Printf(TEXT("list %s, row "
                "font %s, hovered row %d"), list != nullptr ? TEXT("embedded") : TEXT("MISSING"), font != nullptr
                ? *font->GetName() : TEXT("none"), list != nullptr ? list->GetEmbeddedHoverRow() : -2));
        }

        // A re-click on the shown tab is a no-op: the same tab stays shown with its scroll offset
        if (page != nullptr)
        {
            USOLMenuButton* tabButton = page->GetTabButton(tab);
            const float offsetBefore = page->GetTabScrollOffset(tab);
            if (tabButton != nullptr)
            {
                tabButton->OnMenuClicked.ExecuteIfBound(tabButton->GetIndex());
            }
            Check(*FString::Printf(TEXT("u-tab-%s-reclick"), SOLID_SMOKE_TAB_NAMES[mSettingsTab]),
                tabButton != nullptr && showsTab(tab) && page->GetTabScrollOffset(tab) == offsetBefore,
                FString::Printf(TEXT("%s, scroll %.1f -> %.1f"), *describeTabs(), offsetBefore,
                page->GetTabScrollOffset(tab)));
        }
        TakeUiShot(*FString::Printf(TEXT("settings-%s"), SOLID_SMOKE_TAB_NAMES[mSettingsTab]));
        if (mSettingsTab + 1 < static_cast<int32>(ESOLSettingsTab::Count))
        {
            mRepeatStep = static_cast<int32>(ESolidSmokeStep::SettingsTab);
        }
        break;
    }
    case ESolidSmokeStep::SettingsRowHover:
    {
        // A row's Small button in the full hover look (lift, amber edge, LED, underline), and no trace drawn (Q15)
        const USOLMenuButton* next = page != nullptr ? page->GetStepperButton(ESOLSettingsStepper::Quality, 1)
            : nullptr;
        const FSOLSolidButtonLook look = next != nullptr ? next->GetAppliedLook() : FSOLSolidButtonLook();
        const bool bTrace = page != nullptr && page->IsAnyTabHoverTraceShown();
        Check(TEXT("u-row-hover"), next != nullptr && next->HasKeyboardFocus() && !SOLSolidStyle::HasHoverTrace(
            next->GetVariant()) && SolidSmokeLookMatches(look, SolidSmokeSettled(ESOLSolidButtonState::Hovered, s,
            false)) && !bTrace && showsTab(ESOLSettingsTab::Graphics), FString::Printf(TEXT("Quality forward focused "
            "%d, look %s, rail trace %d, %s"), next != nullptr && next->HasKeyboardFocus() ? 1 : 0,
            *SolidSmokeDescribe(look), bTrace ? 1 : 0, *describeTabs()));
        TakeUiShot(TEXT("settings-row-hover"));
        break;
    }
    case ESolidSmokeStep::SettingsControls:
    {
        // A slider drag, a toggle and a stepper as their controls do: dirty; Revert: clean with the values back
        USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
        if (page == nullptr || settings == nullptr)
        {
            Check(TEXT("u-controls-revert"), false, TEXT("no Settings page or settings"));
            break;
        }
        const SOLSettingsModel::FSettingsValues before = settings->GetLiveValues();
        const bool bWasDirty = settings->IsDirty();
        const float volume = before.MasterVolume >= SOLID_SMOKE_SLIDER_DELTA ? before.MasterVolume
            - SOLID_SMOKE_SLIDER_DELTA : before.MasterVolume + SOLID_SMOKE_SLIDER_DELTA;
        page->SetSliderValue(ESOLSettingsSlider::MasterVolume, volume);
        page->SetToggle(ESOLSettingsToggle::Vsync, !before.bVsync);
        page->Step(ESOLSettingsStepper::Quality, before.QualityPreset > 0 ? -1 : 1);
        const SOLSettingsModel::FSettingsValues changed = settings->GetLiveValues();
        const bool bDirty = settings->IsDirty();
        const ESOLSettingsTab tabBefore = page->GetSelectedTab();
        page->Revert();
        const SOLSettingsModel::FSettingsValues after = settings->GetLiveValues();
        const bool bChanged = !FMath::IsNearlyEqual(changed.MasterVolume, before.MasterVolume)
            && changed.bVsync != before.bVsync && changed.QualityPreset != before.QualityPreset;
        const bool bBack = FMath::IsNearlyEqual(after.MasterVolume, before.MasterVolume)
            && after.bVsync == before.bVsync && after.QualityPreset == before.QualityPreset;
        Check(TEXT("u-controls-revert"), !bWasDirty && bChanged && bDirty && !settings->IsDirty() && bBack
            && page->GetSelectedTab() == tabBefore, FString::Printf(TEXT("dirty before %d, changed %d (volume %.2f -> "
            "%.2f, vsync %d -> %d, quality %d -> %d), dirty %d; after Revert dirty %d, values back %d, tab kept %d"),
            bWasDirty ? 1 : 0, bChanged ? 1 : 0, before.MasterVolume, changed.MasterVolume, before.bVsync ? 1 : 0,
            changed.bVsync ? 1 : 0, before.QualityPreset, changed.QualityPreset, bDirty ? 1 : 0,
            settings->IsDirty() ? 1 : 0, bBack ? 1 : 0, page->GetSelectedTab() == tabBefore ? 1 : 0));
        break;
    }
    case ESolidSmokeStep::SettingsSliderFill:
    {
        // The fill at 0%, 100% and 50% and each Audio slider at its value; then the real drag; Revert afterwards
        Check(TEXT("u-slider-fill"), mFillRun > 0 && mFillPassed == mFillRun, FString::Printf(TEXT("%d/%d fills "
            "match their values:%s"), mFillPassed, mFillRun, *mFillDetail));
        Check(TEXT("u-slider-fill-drag"), mIsDragOk, mDragDetail);
        if (page != nullptr)
        {
            page->Revert();
        }
        break;
    }
    case ESolidSmokeStep::SettingsHoverWins:
        Check(TEXT("u-hover-wins-slider"), mIsSliderHoverOk, mHoverDetail);
        Check(TEXT("u-hover-wins-key"), mIsKeyWinsOk, mHoverDetail);
        Check(TEXT("u-hover-wins-overlay"), mIsOverlayHoverOk, mHoverDetail);
        Check(TEXT("u-hover-wins-column"), mIsColumnHoverOk, mHoverDetail);
        break;
    case ESolidSmokeStep::SettingsScroll:
    {
        const int32 binding = SOLKeyBindings::BINDING_COUNT - 1;
        const bool bInView = page != nullptr && page->IsBindingRowInView(binding);
        Check(TEXT("u-scroll-binding"), mScrollFromTab == ESOLSettingsTab::Graphics
            && mScrollTabAtOnce == ESOLSettingsTab::KeyBindings && showsTab(ESOLSettingsTab::KeyBindings) && bInView
            && footerShown(), FString::Printf(TEXT("from tab %d, tab at once %d, %s, last binding's row in view %d, "
            "footer %d"), static_cast<int32>(mScrollFromTab), static_cast<int32>(mScrollTabAtOnce), *describeTabs(),
            bInView ? 1 : 0, footerShown() ? 1 : 0));
        CheckSettingsKeyCaps(TEXT("u-keycaps"), page);
        TakeUiShot(TEXT("settings-keys-scrolled"));
        break;
    }
    case ESolidSmokeStep::SettingsReopen:
    {
        const bool bOpen = mainMenu != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::Settings;
        Check(TEXT("u-reopen-first-tab"), bOpen && showsTab(SOLSettingsTabs::OpeningTab()), FString::Printf(TEXT("left "
            "on Key bindings, reopened: page open %d, %s"), bOpen ? 1 : 0, *describeTabs()));

        // Every tab back at its top (46e follow-up), the Key bindings tab having been scrolled before the close
        FString offsets;
        bool bAllTop = page != nullptr;
        for (int32 tab = 0; tab < static_cast<int32>(ESOLSettingsTab::Count); ++tab)
        {
            const float offset = page != nullptr ? page->GetTabScrollOffset(static_cast<ESOLSettingsTab>(tab)) : -1.0f;
            bAllTop &= FMath::IsNearlyZero(offset, SOLID_SMOKE_TOLERANCE);
            offsets += FString::Printf(TEXT(" %.1f"), offset);
        }
        Check(TEXT("u-reopen-scroll-top"), bAllTop && mScrollBeforeReopen > SOLID_SMOKE_FIT_TOLERANCE,
            FString::Printf(TEXT("Key bindings offset %.1f before the close; offsets after the reopen%s"),
            mScrollBeforeReopen, *offsets));
        break;
    }
    case ESolidSmokeStep::SettingsClose:
    {
        const bool bClosed = mainMenu != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::None;
        Check(TEXT("u-settings-closed"), bClosed, FString::Printf(TEXT("page %d"), mainMenu != nullptr
            ? static_cast<int32>(mainMenu->GetPage()) : -1));
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// The slider fill step's stages (46e follow-up): Master volume at 0, 100% and 50%, each read and shot once painted,
// with every Audio slider at 50%; then a real drag from SOLID_SMOKE_DRAG_FROM to SOLID_SMOKE_DRAG_TO along its bar
bool FSOLSolidMenuSmoke::TickSliderFill(USOLMenuSubsystem& menu)
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
    const USOLSolidSlider* master = page != nullptr ? page->GetSlider(ESOLSettingsSlider::MasterVolume) : nullptr;
    if (page == nullptr || master == nullptr)
    {
        return true;
    }
    if (mStepFrames - mSubStageFrames < SOLID_SMOKE_PAINT_FRAMES)
    {
        return false;
    }
    // Records one slider's fill against its value
    const auto readFill = [this](const TCHAR* what, const USOLSolidSlider* slider)
    {
        FString detail;
        const bool bOk = SolidSmokeFillMatches(slider, detail);
        ++mFillRun;
        mFillPassed += bOk ? 1 : 0;
        mFillDetail += FString::Printf(TEXT(" [%s %s: %s]"), what, bOk ? TEXT("ok") : TEXT("WRONG"), *detail);
    };
    // Moves to the next stage, its paint frames counted from now
    const auto nextStage = [this]()
    {
        ++mSubStage;
        mSubStageFrames = mStepFrames;
    };
    switch (mSubStage)
    {
    case 0:
        // Each value is read and shot on its own stage, the next value set on the stage after (the shot shows it)
        readFill(TEXT("master 0%"), master);
        TakeUiShot(TEXT("settings-slider-0"));
        nextStage();
        return false;
    case 1:
        page->SetSliderValue(ESOLSettingsSlider::MasterVolume, 1.0f);
        nextStage();
        return false;
    case 2:
        readFill(TEXT("master 100%"), master);
        TakeUiShot(TEXT("settings-slider-100"));
        nextStage();
        return false;
    case 3:
        page->SetSliderValue(ESOLSettingsSlider::MasterVolume, 0.5f);
        page->SetSliderValue(ESOLSettingsSlider::SfxVolume, 0.5f);
        page->SetSliderValue(ESOLSettingsSlider::MusicVolume, 0.5f);
        nextStage();
        return false;
    case 4:
        readFill(TEXT("master 50%"), master);
        readFill(TEXT("effects 50%"), page->GetSlider(ESOLSettingsSlider::SfxVolume));
        readFill(TEXT("music 50%"), page->GetSlider(ESOLSettingsSlider::MusicVolume));
        TakeUiShot(TEXT("settings-slider-fill"));
        nextStage();
        return false;
    case 5:
    {
        // The drag: the real cursor onto the knob's place at SOLID_SMOKE_DRAG_FROM (the game window in front), pressed
        const FVector2D from = SolidSmokeSliderPoint(master, SOLID_SMOKE_DRAG_FROM);
        SolidSmokeBringGameToFront();
        SolidSmokeMouseMove(from, false);
        SolidSmokeMouseButton(from, true);
        nextStage();
        return false;
    }
    case 6:
        // Held: moved to SOLID_SMOKE_DRAG_TO
        SolidSmokeMouseMove(SolidSmokeSliderPoint(master, SOLID_SMOKE_DRAG_TO), true);
        nextStage();
        return false;
    case 7:
    {
        // Still held: the fill follows the dragged value (shot while held)
        FString detail;
        const bool bFill = SolidSmokeFillMatches(master, detail);
        const float value = master->GetNormalizedValue();
        const bool bCaptured = master->HasMouseCapture();
        const FVector2D to = SolidSmokeSliderPoint(master, SOLID_SMOKE_DRAG_TO);
        FString cursor;
        SolidSmokeCursorState(to, cursor);
        mIsDragOk = bFill && bCaptured && FMath::IsNearlyEqual(value, SOLID_SMOKE_DRAG_TO, SOLID_SMOKE_DRAG_TOLERANCE);
        mDragDetail = FString::Printf(TEXT("dragged %.2f -> %.2f: captured %d, value %.3f, %s; %s (retries %d)"),
            SOLID_SMOKE_DRAG_FROM, SOLID_SMOKE_DRAG_TO, bCaptured ? 1 : 0, value, *detail, *cursor, mCursorRetries);

        // (46i) A drag the slider never captured (the real cursor was taken away) is released and made again, with
        // the game window in front, at most SOLID_SMOKE_CURSOR_RETRIES times
        if (!bCaptured && mCursorRetries < SOLID_SMOKE_CURSOR_RETRIES)
        {
            ++mCursorRetries;
            UE_LOG(LogSOL, Warning, TEXT("SolidMenu slider drag: retry %d, not captured (%s)"), mCursorRetries,
                *cursor);
            SolidSmokeMouseButton(to, false);
            page->SetSliderValue(ESOLSettingsSlider::MasterVolume, 0.5f);
            mSubStage = 5;
            mSubStageFrames = mStepFrames;
            return false;
        }
        TakeUiShot(TEXT("settings-slider-drag"));
        nextStage();
        return false;
    }
    case 8:
        // Released, the cursor parked off the controls
        SolidSmokeMouseButton(SolidSmokeSliderPoint(master, SOLID_SMOKE_DRAG_TO), false);
        ParkCursor();
        nextStage();
        return false;
    default:
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// The hover-wins step's stages (46e follow-up): the Audio tab focused and the real cursor over Master volume (the
// tab's amber gone), then parked (back); the Overlays tab focused and an embedded row hovered (gone), then cleared
bool FSOLSolidMenuSmoke::TickHoverWins(USOLMenuSubsystem& menu)
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
    USOLOverlayMenuWidget* list = page != nullptr ? page->GetOverlayList() : nullptr;
    if (page == nullptr || list == nullptr)
    {
        mIsSliderHoverOk = mIsOverlayHoverOk = false;
        mHoverDetail = TEXT("no Settings page or Overlay list");
        return true;
    }
    if (mSubStage > 0 && mStepTimeS - mSubStageS < SOLID_SMOKE_SETTLE_S)
    {
        return false;
    }
    // Reads a tab button's state: focused, its settled look lit (amber, its rail trace drawn) or not (no rail trace),
    // the page's hover-elsewhere as expected; logs it
    const auto read = [this, page](const TCHAR* what, const ESOLSettingsTab tab, const bool bWantLit,
        const bool bWantElsewhere)
    {
        const USOLMenuButton* button = page->GetTabButton(tab);
        const bool bFocused = button != nullptr && button->HasKeyboardFocus();
        const float edge = button != nullptr ? button->GetAppliedLook().Edge : -1.0f;
        const bool bLit = edge > 0.5f;
        const bool bTrace = page->IsAnyTabHoverTraceShown();
        const bool bOk = bFocused && bLit == bWantLit && page->IsHoverElsewhere() == bWantElsewhere
            && (bWantLit ? page->IsTabHoverTraceShown(tab) : edge <= SOLID_SMOKE_TOLERANCE && !bTrace);
        mHoverDetail += FString::Printf(TEXT(" [%s: tab %d focused %d edge %.3f trace %d, hover elsewhere %d: %s]"),
            what, static_cast<int32>(tab), bFocused ? 1 : 0, edge, bTrace ? 1 : 0, page->IsHoverElsewhere() ? 1 : 0,
            bOk ? TEXT("ok") : TEXT("WRONG"));
        return bOk;
    };
    // Reads the main menu's Settings entry: focused, lit or not as expected; logs it
    const auto readEntry = [this, mainMenu](const TCHAR* what, const bool bWantLit)
    {
        const USOLMenuButton* entry = mainMenu->GetEntryButton(SOLID_SMOKE_SETTINGS_ENTRY);
        const bool bFocused = entry != nullptr && entry->HasKeyboardFocus();
        const float edge = entry != nullptr ? entry->GetAppliedLook().Edge : -1.0f;
        const bool bOk = bFocused && (bWantLit ? edge > 0.5f : edge <= SOLID_SMOKE_TOLERANCE);
        mHoverDetail += FString::Printf(TEXT(" [%s: Settings entry focused %d edge %.3f: %s]"), what,
            bFocused ? 1 : 0, edge, bOk ? TEXT("ok") : TEXT("WRONG"));
        return bOk;
    };
    // Moves to the next stage, its settle timed from now
    const auto nextStage = [this]()
    {
        ++mSubStage;
        mSubStageS = mStepTimeS;
    };
    const USOLSolidSlider* master = page->GetSlider(ESOLSettingsSlider::MasterVolume);
    // (46i) A real-cursor move whose hover did not take (the OS cursor moved off, or another window covered the game
    // window while it was inactive) is logged with the cursor's state and made again with the game window in front, at
    // most SOLID_SMOKE_CURSOR_RETRIES times a step; returns whether to wait for the retry
    const auto retryMove = [this, master](const float fraction)
    {
        if (master == nullptr || master->IsHovered() || mCursorRetries >= SOLID_SMOKE_CURSOR_RETRIES)
        {
            return false;
        }
        const FVector2D point = SolidSmokeSliderPoint(master, fraction);
        FString cursor;
        SolidSmokeCursorState(point, cursor);
        ++mCursorRetries;
        mHoverDetail += FString::Printf(TEXT(" [retry %d: the slider not hovered, %s]"), mCursorRetries, *cursor);
        UE_LOG(LogSOL, Warning, TEXT("SolidMenu hover-wins: retry %d, the slider not hovered (%s)"), mCursorRetries,
            *cursor);
        SolidSmokeBringGameToFront();
        SolidSmokeMouseMove(point, false);
        mSubStageS = mStepTimeS;
        return true;
    };
    switch (mSubStage)
    {
    case 0:
        // The Audio tab settled with the focus (lit), then the real cursor onto the slider (the game window in front)
        if (mStepTimeS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        mIsSliderHoverOk &= read(TEXT("audio focused"), ESOLSettingsTab::Audio, true, false);
        SolidSmokeBringGameToFront();
        SolidSmokeMouseMove(SolidSmokeSliderPoint(master, 0.5f), false);
        nextStage();
        return false;
    case 1:
        if (retryMove(0.5f))
        {
            return false;
        }
        mIsSliderHoverOk &= master != nullptr && master->IsHovered();
        mIsSliderHoverOk &= read(TEXT("slider hovered"), ESOLSettingsTab::Audio, false, true);
        TakeUiShot(TEXT("settings-hover-wins"));
        nextStage();
        return false;
    case 2:
        // A key with the cursor still resting on the slider: the last input is the keyboard's
        SolidSmokeKey(EKeys::LeftShift);
        nextStage();
        return false;
    case 3:
        mIsKeyWinsOk = master != nullptr && master->IsHovered();
        mIsKeyWinsOk &= read(TEXT("key on a hovered slider"), ESOLSettingsTab::Audio, true, false);
        SolidSmokeMouseMove(SolidSmokeSliderPoint(master, 0.55f), false);
        nextStage();
        return false;
    case 4:
        if (retryMove(0.55f))
        {
            return false;
        }
        mIsKeyWinsOk &= read(TEXT("mouse moved again"), ESOLSettingsTab::Audio, false, true);
        ParkCursor();
        nextStage();
        return false;
    case 5:
        mIsSliderHoverOk &= read(TEXT("cursor parked"), ESOLSettingsTab::Audio, true, false);
        ClickSettingsTab(page, ESOLSettingsTab::Overlays);
        nextStage();
        return false;
    case 6:
        mIsOverlayHoverOk &= read(TEXT("overlays focused"), ESOLSettingsTab::Overlays, true, false);
        list->SetEmbeddedHoverRow(SOLID_SMOKE_OVERLAY_HOVER_ROW);
        nextStage();
        return false;
    case 7:
        mIsOverlayHoverOk &= read(TEXT("overlay row hovered"), ESOLSettingsTab::Overlays, false, true);
        list->SetEmbeddedHoverRow(INDEX_NONE);
        nextStage();
        return false;
    case 8:
    {
        // The main menu's Settings entry focused (as a click on it leaves it), then an Overlay row hovered
        mIsOverlayHoverOk &= read(TEXT("overlay row left"), ESOLSettingsTab::Overlays, true, false);
        if (USOLMenuButton* entry = mainMenu->GetEntryButton(SOLID_SMOKE_SETTINGS_ENTRY))
        {
            entry->NotifyHovered();
        }
        nextStage();
        return false;
    }
    case 9:
        mIsColumnHoverOk = readEntry(TEXT("entry focused"), true);
        list->SetEmbeddedHoverRow(SOLID_SMOKE_OVERLAY_HOVER_ROW);
        nextStage();
        return false;
    case 10:
        mIsColumnHoverOk &= readEntry(TEXT("entry, overlay row hovered"), false);
        list->SetEmbeddedHoverRow(INDEX_NONE);
        nextStage();
        return false;
    case 11:
        mIsColumnHoverOk &= readEntry(TEXT("entry, overlay row left"), true);
        return true;
    default:
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// Parks the cursor in the view's top-right corner, off every control, and tells Slate (a real move)
void FSOLSolidMenuSmoke::ParkCursor() const
{
    const FIntPoint size = GetViewSize();
    if (mController.IsValid() && size.X > SOLID_SMOKE_CURSOR_INSET_PX)
    {
        mController->SetMouseLocation(size.X - SOLID_SMOKE_CURSOR_INSET_PX, SOLID_SMOKE_CURSOR_INSET_PX);
    }
    if (FSlateApplication::IsInitialized())
    {
        SolidSmokeMouseMove(FSlateApplication::Get().GetCursorPos(), false);
    }
}

//////////////////////////////////////////////////////////////////////////
// Checks a Controls / Info / About tab page: tabs, selected mark, fit, then every tab switched to
void FSOLSolidMenuSmoke::CheckReadOnlyPage(const TCHAR* id, USOLReadOnlyPageWidget* page)
{
    if (page == nullptr)
    {
        Check(*FString::Printf(TEXT("%s-tabs"), id), false, TEXT("no page"));
        return;
    }
    const int32 count = page->GetTabCount();
    int32 selectedMarks = 0;
    for (int32 tab = 0; tab < count; ++tab)
    {
        selectedMarks += page->GetTabButton(tab) != nullptr && page->GetTabButton(tab)->IsSelected() ? 1 : 0;
    }
    Check(*FString::Printf(TEXT("%s-tabs"), id), count == SOLPageTabs::TabCount(page->GetPageKind()) && count > 0
        && page->GetSelectedTab() == SOLPageTabs::OpeningTab() && page->GetShownContentIndex()
        == SOLPageTabs::OpeningTab() && selectedMarks == 1 && page->GetTabButton(SOLPageTabs::OpeningTab())->IsSelected()
        && !page->IsAnyTabHoverTraceShown(), FString::Printf(TEXT("%d tabs (want %d), shown %d, content %d, %d "
        "selected mark(s), hover trace %d"), count, SOLPageTabs::TabCount(page->GetPageKind()),
        page->GetSelectedTab(), page->GetShownContentIndex(), selectedMarks, page->IsAnyTabHoverTraceShown() ? 1 : 0));

    // Every left-column text: its longest word inside its column (measured at the page's layout scale), and every
    // text of the shown tab inside the content horizontally. The rows are read freshly arranged from the tab's scroll
    // box, not from their cached geometry: a row out of view is culled from the paint, so its cached geometry is stale
    // (from the slide-in's first frame, laid out before the wrapped column had its width; the 46f TECH_DEBT reading)
    const float scale = page->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
    const UScrollBox* scroll = page->GetTabScroll(page->GetSelectedTab());
    const FGeometry scrollGeometry = scroll != nullptr ? scroll->GetCachedGeometry() : FGeometry();
    const double scrollMin = scrollGeometry.GetAbsolutePosition().X;
    const double scrollMax = scrollMin + scrollGeometry.GetAbsoluteSize().X;
    bool bFits = scale > 0.0f && scrollMax > scrollMin;
    int32 shown = 0;
    FString detail;
    for (const USOLReadOnlyPageWidget::FColumnText& column : page->GetColumnTexts())
    {
        // A key cap cell (33b): its laid-out caps inside its column; a text: each word inside it
        const UWidget* left = column.Caps != nullptr ? static_cast<const UWidget*>(column.Caps.Get())
            : column.Text.Get();
        const FString leftText = column.Caps != nullptr ? column.Caps->GetText()
            : column.Text != nullptr ? column.Text->GetText().ToString() : FString();
        if (left == nullptr)
        {
            bFits = false;
            detail += TEXT(" [a row without its left cell]");
            continue;
        }
        if (column.Caps != nullptr)
        {
            const float width = column.Caps->GetLaidOutWidth();
            if (!(width > 0.0f && width <= column.Width + SOLID_SMOKE_FIT_TOLERANCE))
            {
                bFits = false;
                detail += FString::Printf(TEXT(" [caps \"%s\" %.1f over %.1f]"), *leftText, width, column.Width);
            }
        }
        else
        {
            TArray<FString> words;
            leftText.ParseIntoArray(words, TEXT(" "));
            for (const FString& word : words)
            {
                const float width = SolidSmokeTextWidth(word, column.Text->GetFont(), scale);
                if (!(width >= 0.0f && width <= column.Width + SOLID_SMOKE_FIT_TOLERANCE))
                {
                    bFits = false;
                    detail += FString::Printf(TEXT(" [\"%s\" %.1f over %.1f]"), *word, width, column.Width);
                }
            }
        }
        if (column.Tab == page->GetSelectedTab())
        {
            ++shown;
            const FGeometry geometry = SolidSmokeArrangedGeometry(left, scroll);
            const double leftX = geometry.GetAbsolutePosition().X;
            const double right = leftX + geometry.GetAbsoluteSize().X;
            if (!(geometry.GetAbsoluteSize().X > 0.0 && leftX >= scrollMin - SOLID_SMOKE_FIT_TOLERANCE_PX
                && right <= scrollMax + SOLID_SMOKE_FIT_TOLERANCE_PX))
            {
                bFits = false;
                detail += FString::Printf(TEXT(" [shown text \"%s\" %.0f-%.0f outside %.0f-%.0f]"),
                    *leftText.Left(24), leftX, right, scrollMin, scrollMax);
            }
        }
    }
    Check(*FString::Printf(TEXT("%s-fit"), id), bFits && (page->GetColumnTexts().Num() == 0 || shown > 0),
        FString::Printf(TEXT("%d column texts (%d on the shown tab) at scale %.3f, content %.0f-%.0f%s"),
        page->GetColumnTexts().Num(), shown, scale, scrollMin, scrollMax, *detail));

    // Every tab switched to as a click does; the last stays shown (the reopen check reads the first again)
    bool bSwitched = true;
    FString switchDetail;
    for (int32 tab = 0; tab < count; ++tab)
    {
        USOLMenuButton* button = page->GetTabButton(tab);
        if (button == nullptr)
        {
            bSwitched = false;
            continue;
        }
        button->OnMenuClicked.ExecuteIfBound(button->GetIndex());
        int32 marks = 0;
        for (int32 other = 0; other < count; ++other)
        {
            marks += page->GetTabButton(other)->IsSelected() ? 1 : 0;
        }
        if (page->GetSelectedTab() != tab || page->GetShownContentIndex() != tab || marks != 1
            || !button->IsSelected())
        {
            bSwitched = false;
            switchDetail += FString::Printf(TEXT(" [tab %d: selected %d, content %d, marks %d]"), tab,
                page->GetSelectedTab(), page->GetShownContentIndex(), marks);
        }
    }

    // A re-click on the shown (last) tab is a no-op: the same tab and content stay, its scroll offset is kept
    USOLMenuButton* lastButton = count > 0 ? page->GetTabButton(count - 1) : nullptr;
    if (lastButton != nullptr)
    {
        const float offsetBefore = page->GetTabScrollOffset(count - 1);
        lastButton->OnMenuClicked.ExecuteIfBound(lastButton->GetIndex());
        Check(*FString::Printf(TEXT("%s-tab-reclick"), id), page->GetSelectedTab() == count - 1
            && page->GetShownContentIndex() == count - 1 && page->GetTabScrollOffset(count - 1) == offsetBefore,
            FString::Printf(TEXT("tab %d re-clicked: selected %d, content %d, scroll %.1f -> %.1f"), count - 1,
            page->GetSelectedTab(), page->GetShownContentIndex(), offsetBefore, page->GetTabScrollOffset(count - 1)));
    }
    if (page->GetPageKind() == ESOLReadOnlyPage::Controls)
    {
        page->SelectTab(SOLPageTabs::OpeningTab());     // The Controls screenshots show the first tab
    }
    Check(*FString::Printf(TEXT("%s-tab-switch"), id), bSwitched, FString::Printf(TEXT("%d tabs clicked in turn%s"),
        count, *switchDetail));
}

//////////////////////////////////////////////////////////////////////////
// Checks the Demo page layout
void FSOLSolidMenuSmoke::CheckDemoLayout(const TCHAR* id, USOLMenuSubsystem& menu)
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    const USOLDemoPageWidget* page = mainMenu != nullptr ? mainMenu->GetDemoPage() : nullptr;
    const UScrollBox* scroll = page != nullptr ? page->GetScroll() : nullptr;
    if (page == nullptr || scroll == nullptr)
    {
        Check(*FString::Printf(TEXT("%s-layout"), id), false, TEXT("no Demo page or scroll"));
        return;
    }
    const float scale = page->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
    const FGeometry scrollGeometry = scroll->GetCachedGeometry();
    const double scrollMin = scrollGeometry.GetAbsolutePosition().X;
    const double scrollMax = scrollMin + scrollGeometry.GetAbsoluteSize().X;
    bool bFits = scale > 0.0f && scrollMax > scrollMin && page->GetScrollOffset() == 0.0f;
    FString detail;
    const int32 rows = SOLMenuState::DemoRowCount();
    for (int32 row = 0; row < rows; ++row)
    {
        const UTextBlock* value = page->GetValueTextWidget(row);
        if (value == nullptr)
        {
            bFits = false;
            continue;
        }
        const FGeometry& geometry = value->GetCachedGeometry();
        const float width = SolidSmokeTextWidth(value->GetText().ToString(), value->GetFont(), scale);
        const double left = geometry.GetAbsolutePosition().X;
        const double right = left + geometry.GetAbsoluteSize().X;
        const bool bInside = left >= scrollMin - SOLID_SMOKE_FIT_TOLERANCE_PX
            && right <= scrollMax + SOLID_SMOKE_FIT_TOLERANCE_PX;
        const bool bRoom = width >= 0.0f && width <= geometry.GetLocalSize().X + SOLID_SMOKE_FIT_TOLERANCE;
        // A row below the fold has no laid-out slot yet; only rows with a size are judged
        if (geometry.GetAbsoluteSize().X > 0.0 && (!bInside || !bRoom))
        {
            bFits = false;
            detail += FString::Printf(TEXT(" [row %d \"%s\" %.1f of %.1f, inside %d]"), row,
                *value->GetText().ToString(), width, geometry.GetLocalSize().X, bInside ? 1 : 0);
        }
    }
    Check(*FString::Printf(TEXT("%s-layout"), id), bFits, FString::Printf(TEXT("scroll offset %.1f, %d value texts "
        "at scale %.3f%s"), page->GetScrollOffset(), rows, scale, *detail));
}

//////////////////////////////////////////////////////////////////////////
// Checks that every stepper value (each resolution choice, "1000 x 800", each window mode and quality name) fits its
// value slot as laid out on a Settings page (the main menu's or the pause menu's), and that no row label outside Key
// bindings runs past its column and gap
void FSOLSolidMenuSmoke::CheckSettingsFit(USOLMenuSubsystem& menu, const USOLSettingsPageWidget* page,
    const TCHAR* valuesLabel, const TCHAR* labelsLabel)
{
    USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
    if (page == nullptr || settings == nullptr)
    {
        Check(valuesLabel, false, TEXT("no Settings page or settings"));
        Check(labelsLabel, false, TEXT("no Settings page or settings"));
        return;
    }

    // Each stepper's possible values against its slot
    TArray<FString> values[static_cast<int32>(ESOLSettingsStepper::Count)];
    values[static_cast<int32>(ESOLSettingsStepper::Resolution)].Add(SOLSettingsRules::FormatResolution(
        SOLID_SMOKE_SMALL_VIEW));
    for (const FIntPoint& choice : settings->GetResolutionChoices())
    {
        values[static_cast<int32>(ESOLSettingsStepper::Resolution)].Add(SOLSettingsRules::FormatResolution(choice));
    }
    for (int32 index = 0; index < SOLSettingsRules::WINDOW_MODE_COUNT; ++index)
    {
        values[static_cast<int32>(ESOLSettingsStepper::WindowMode)].Add(SOLSettingsRules::WindowModeName(index));
    }
    for (int32 index = 0; index < SOLSettingsRules::QUALITY_PRESET_COUNT; ++index)
    {
        values[static_cast<int32>(ESOLSettingsStepper::Quality)].Add(SOLSettingsRules::QualityName(index));
    }
    // The page's layout scale (the text is measured as drawn at it), from the laid-out Resolution value
    const UTextBlock* scaleText = page->GetStepperValueText(ESOLSettingsStepper::Resolution);
    const float scale = scaleText != nullptr ? scaleText->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale()
        : 0.0f;
    bool bValuesFit = scale > 0.0f;
    FString valueDetail = FString::Printf(TEXT("scale %.3f:"), scale);
    for (int32 stepper = 0; stepper < static_cast<int32>(ESOLSettingsStepper::Count); ++stepper)
    {
        const UTextBlock* text = page->GetStepperValueText(static_cast<ESOLSettingsStepper>(stepper));
        const float slot = text != nullptr ? text->GetCachedGeometry().GetLocalSize().X : 0.0f;
        float widest = 0.0f;
        bool bMeasured = text != nullptr;
        FString widestText;
        for (const FString& value : values[stepper])
        {
            const float width = text != nullptr ? SolidSmokeTextWidth(value, text->GetFont(), scale) : -1.0f;
            bMeasured &= width >= 0.0f;
            if (width > widest)
            {
                widest = width;
                widestText = value;
            }
        }
        const bool bFits = bMeasured && slot > 0.0f && widest <= slot + SOLID_SMOKE_FIT_TOLERANCE;
        bValuesFit &= bFits;
        valueDetail += FString::Printf(TEXT(" [stepper %d: widest \"%s\" %.1f in a %.1f slot%s]"), stepper,
            *widestText, widest, slot, bFits ? TEXT("") : bMeasured ? TEXT(" CLIPPED") : TEXT(" NOT MEASURED"));
    }
    Check(valuesLabel, bValuesFit, valueDetail);

    // Row labels: each ends before its row's control (the applied column plus the gap); a narrow tab's labels wrap, so
    // each of their words fits the column; wide Key bindings rows are known tech debt (logged, not checked)
    bool bLabelsFit = true;
    FString labelDetail;
    float worstMargin = TNumericLimits<float>::Max();
    FString worst;
    for (int32 index = 0; index < page->GetRowLabelCount(); ++index)
    {
        ESOLSettingsTab tab = ESOLSettingsTab::Count;
        const UTextBlock* label = page->GetRowLabel(index, tab);
        const USizeBox* labelBox = page->GetRowLabelBox(index);
        const FString text = label != nullptr ? label->GetText().ToString() : FString();
        const bool bNarrow = page->IsNarrowLayout(tab);
        const float column = labelBox != nullptr ? labelBox->GetWidthOverride() : -1.0f;
        const float width = label != nullptr ? (bNarrow ? SolidSmokeWidestWord(text, label->GetFont(), scale)
            : SolidSmokeTextWidth(text, label->GetFont(), scale)) : -1.0f;
        const float room = bNarrow ? column : column + SOL::MenuStyle::BUTTON_GAP;
        const bool bFits = column > 0.0f && width >= 0.0f && width <= room + SOLID_SMOKE_FIT_TOLERANCE;
        if (tab == ESOLSettingsTab::KeyBindings && !bNarrow)
        {
            if (!bFits)
            {
                labelDetail += FString::Printf(TEXT(" (key row \"%s\" %.1f over %.1f, TECH_DEBT)"), *text, width,
                    room);
            }
            continue;
        }
        bLabelsFit &= bFits;
        if (room - width < worstMargin)
        {
            worstMargin = room - width;
            worst = FString::Printf(TEXT("\"%s\" %.1f of %.1f on tab %d"), *text, width, room,
                static_cast<int32>(tab));
        }
        if (!bFits)
        {
            labelDetail += FString::Printf(TEXT(" [\"%s\" %.1f over %.1f]"), *text, width, room);
        }
    }
    Check(labelsLabel, bLabelsFit && scale > 0.0f && page->GetRowLabelCount() > 0, FString::Printf(TEXT("%d labels at "
        "scale %.3f (row %.1f, narrow Gameplay %d, Key bindings %d), the tightest %s%s"), page->GetRowLabelCount(),
        scale, page->GetFitRowWidth(), page->IsNarrowLayout(ESOLSettingsTab::Gameplay) ? 1 : 0,
        page->IsNarrowLayout(ESOLSettingsTab::KeyBindings) ? 1 : 0, *worst, *labelDetail));
}

//////////////////////////////////////////////////////////////////////////
// Checks a real tab rail lays out without a scroll: a laid-out view the tabs fit, at the top, the last tab inside
void FSOLSolidMenuSmoke::CheckRailFits(const TCHAR* id, FSOLSolidTabRail& rail)
{
    const int32 count = rail.Buttons.Num();
    FString lastDetail = TEXT("missing");
    const bool bLastInside = count > 0 && SolidSmokeInside(rail.GetButton(count - 1), rail.GetScrollBox(), lastDetail);
    Check(id, count > 0 && rail.GetViewHeight() > 0.0f && !rail.NeedsScroll() && rail.GetScrollOffset() == 0.0f
        && bLastInside, FString::Printf(TEXT("%d tabs: content %.1f in a %.1f view (room %.1f), offset %.1f, needs "
        "scroll %d, last tab %s"), count, rail.GetContentHeight(), rail.GetViewHeight(), rail.GetViewHeight()
        - rail.GetContentHeight(), rail.GetScrollOffset(), rail.NeedsScroll() ? 1 : 0, *lastDetail));
}

//////////////////////////////////////////////////////////////////////////
// Drops every other pending value, then sets the HUD effects strength and applies it alone (the smoke slot only)
void FSOLSolidMenuSmoke::ApplyStrength(USOLMenuSubsystem& menu, const float strength) const
{
    // A pending resolution or window mode would otherwise reach the engine's GameUserSettings with the Apply
    USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
    if (settings == nullptr)
    {
        return;
    }
    settings->RevertPending();
    SetStrength(menu, strength);
    settings->ApplyPending();
}

//////////////////////////////////////////////////////////////////////////
// The column scroll step's stages (D17): capped at SOLID_SMOKE_SCROLL_CAP entries the column scrolls; Down from Play
// to Quit scrolls Quit whole into view with its hover trace drawn at Quit's laid-out y; the view scrolled back to the
// top with Quit still focused draws no trace; the cap back at STACK_MAX_VISIBLE_ENTRIES: no scroll, offset 0
bool FSOLSolidMenuSmoke::TickMainScroll(USOLMenuSubsystem& menu)
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    UScrollBox* scroll = mainMenu != nullptr ? mainMenu->GetStackScrollBox() : nullptr;
    const TSharedPtr<SSOLSolidTraceLayer> layer = SolidSmokeColumnLayer(mainMenu);
    USOLMenuButton* quit = mainMenu != nullptr ? mainMenu->GetEntryButton(SOLID_SMOKE_QUIT_ENTRY) : nullptr;
    const USOLMenuButton* play = mainMenu != nullptr ? mainMenu->GetEntryButton(SOLID_SMOKE_PLAY_ENTRY) : nullptr;
    if (scroll == nullptr || !layer.IsValid() || quit == nullptr || play == nullptr)
    {
        Check(TEXT("k-column-scroll"), false, TEXT("no column stack, trace layer or entries"));
        return true;
    }
    // Moves to the next stage, its time and frames counted from now
    const auto nextStage = [this]()
    {
        ++mSubStage;
        mSubStageS = mStepTimeS;
        mSubStageFrames = mStepFrames;
    };
    const float viewHeight = static_cast<float>(scroll->GetCachedGeometry().GetLocalSize().Y);
    const UWidget* items = scroll->GetChildrenCount() > 0 ? scroll->GetChildAt(0) : nullptr;
    const float contentHeight = items != nullptr ? static_cast<float>(items->GetDesiredSize().Y) : 0.0f;
    const float offset = mainMenu->GetStackScrollOffset();
    const FString state = FString::Printf(TEXT("content %.1f in a %.1f view, offset %.1f, layer offset %.1f"),
        contentHeight, viewHeight, offset, layer->GetScrollOffset());
    switch (mSubStage)
    {
    case 0:
        // The page closed and the capped column laid out: it needs scrolling, still at the top with Play focused
        if (mStepTimeS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        Check(TEXT("k-column-scroll"), mainMenu->IsStackScrollNeeded() && viewHeight > 0.0f
            && viewHeight + SOLID_SMOKE_SCROLL_TOLERANCE < contentHeight && offset == 0.0f && play->HasKeyboardFocus(),
            FString::Printf(TEXT("cap %d entries: needs scroll %d, %s, Play focused %d"), SOLID_SMOKE_SCROLL_CAP,
            mainMenu->IsStackScrollNeeded() ? 1 : 0, *state, play->HasKeyboardFocus() ? 1 : 0));
        nextStage();
        return false;
    case 1:
        // Down from Play to Quit, one press a frame, through Slate's navigation as a keyboard would
        if (mDownPresses < SOLID_SMOKE_QUIT_ENTRY)
        {
            SolidSmokeKey(EKeys::Down);
            ++mDownPresses;
            return false;
        }
        nextStage();
        return false;
    case 2:
    {
        // Quit focused and scrolled whole into view (the end); the layer scrolled with the stack and Quit's hover
        // trace drawn at Quit's laid-out centre (the lift allowed for)
        if (mStepTimeS - mSubStageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        // Moving down, each newly focused entry is shown whole with the least scroll: Quit's bottom at the view's
        // bottom (its trailing gap stays below)
        FString inside;
        const bool bInside = SolidSmokeInside(quit, scroll, inside);
        const float wantOffset = SOLSolidScroll::StackItemTop(SOLID_SMOKE_QUIT_ENTRY, SOL::MenuStyle::BUTTON_HEIGHT,
            SOL::MenuStyle::BUTTON_GAP, 0.0f) + SOL::MenuStyle::BUTTON_HEIGHT - viewHeight;
        Check(TEXT("k-column-focus-scroll"), quit->HasKeyboardFocus() && bInside && offset > 0.0f
            && FMath::IsNearlyEqual(offset, wantOffset, SOLID_SMOKE_SCROLL_TOLERANCE), FString::Printf(TEXT("Quit "
            "focused %d after %d Down, %s (want %.1f, end %.1f), Quit %s"), quit->HasKeyboardFocus() ? 1 : 0,
            mDownPresses, *state, wantOffset, SOLSolidScroll::MaxOffset(contentHeight, viewHeight), *inside));
        const FGeometry& quitGeometry = quit->GetCachedGeometry();
        const FVector2D quitCentre(quitGeometry.LocalToAbsolute(quitGeometry.GetLocalSize() * 0.5f));
        const float quitY = static_cast<float>(FVector2D(layer->GetCachedGeometry().AbsoluteToLocal(quitCentre)).Y);
        const float drawnY = layer->GetDrawnHoverTraceY();
        Check(TEXT("k-column-trace"), mainMenu->IsEntryHoverTraceShown(SOLID_SMOKE_QUIT_ENTRY)
            && layer->IsHoverTraceDrawn() && FMath::IsNearlyEqual(layer->GetScrollOffset(), offset,
            SOLID_SMOKE_SCROLL_TOLERANCE) && FMath::Abs(drawnY - quitY) <= SOL::SolidMenu::HOVER_LIFT_PX
            + SOLID_SMOKE_SCROLL_TOLERANCE, FString::Printf(TEXT("Quit's trace shown %d, drawn %d at y %.1f "
            "(unscrolled %.1f), Quit's centre at y %.1f; %s"),
            mainMenu->IsEntryHoverTraceShown(SOLID_SMOKE_QUIT_ENTRY) ? 1 : 0, layer->IsHoverTraceDrawn() ? 1 : 0,
            drawnY, drawnY + layer->GetScrollOffset(), quitY, *state));
        TakeUiShot(TEXT("column-scrolled"));
        nextStage();
        return false;
    }
    case 3:
        // The view scrolled back to the top with Quit still focused (the stack follows focus changes only)
        if (mStepFrames - mSubStageFrames < SOLID_SMOKE_PAINT_FRAMES)
        {
            return false;
        }
        scroll->SetScrollOffset(0.0f);
        nextStage();
        return false;
    case 4:
        // Quit out of the view: its hover trace is still set, but the layer (scrolled to the top too) draws none
        if (mStepFrames - mSubStageFrames < SOLID_SMOKE_PAINT_FRAMES)
        {
            return false;
        }
        Check(TEXT("k-column-hidden-trace"), quit->HasKeyboardFocus() && offset == 0.0f
            && layer->GetScrollOffset() == 0.0f && layer->GetHoverAmount() > 0.0f && !layer->IsHoverTraceDrawn(),
            FString::Printf(TEXT("Quit focused %d out of view: trace amount %.2f, drawn %d; %s"),
            quit->HasKeyboardFocus() ? 1 : 0, layer->GetHoverAmount(), layer->IsHoverTraceDrawn() ? 1 : 0, *state));
        mainMenu->SetStackMaxVisibleEntriesForVerification(SOL::SolidMenu::STACK_MAX_VISIBLE_ENTRIES);
        ParkCursor();
        mainMenu->FocusDefault();
        nextStage();
        return false;
    case 5:
    {
        // The real cap: the 7 entries fit, no scroll, everything at the top, Quit inside the view
        if (mStepTimeS - mSubStageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        FString inside;
        const bool bInside = SolidSmokeInside(quit, scroll, inside);
        Check(TEXT("k-column-restore"), !mainMenu->IsStackScrollNeeded() && offset == 0.0f
            && layer->GetScrollOffset() == 0.0f && play->HasKeyboardFocus() && bInside, FString::Printf(TEXT("cap %d: "
            "needs scroll %d, %s, Play focused %d, Quit %s"), SOL::SolidMenu::STACK_MAX_VISIBLE_ENTRIES,
            mainMenu->IsStackScrollNeeded() ? 1 : 0, *state, play->HasKeyboardFocus() ? 1 : 0, *inside));
        return true;
    }
    default:
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// The rail scroll step's stages (D17): the viewport Controls page's 10-tab rail scrolls; ScrollToEnd shows the last
// tab whole; back at the top, the keyboard focus on the hidden last tab scrolls it into view with its hover trace
bool FSOLSolidMenuSmoke::TickRailScroll(USOLMenuSubsystem& menu)
{
    USOLReadOnlyPageWidget* page = mRailPage.Get();
    FSOLSolidTabRail* rail = page != nullptr ? &page->GetRail() : nullptr;
    UScrollBox* scroll = rail != nullptr ? rail->GetScrollBox() : nullptr;
    const int32 last = page != nullptr ? page->GetTabCount() - 1 : INDEX_NONE;
    USOLMenuButton* lastTab = page != nullptr ? page->GetTabButton(last) : nullptr;
    const TSharedPtr<SSOLSolidTraceLayer> layer = rail != nullptr && rail->Panel != nullptr
        ? rail->Panel->GetTraceLayer() : nullptr;
    if (scroll == nullptr || lastTab == nullptr || !layer.IsValid())
    {
        Check(TEXT("k-rail-scroll"), false, TEXT("no viewport page, rail, last tab or trace layer"));
        return true;
    }
    // Moves to the next stage, its time and frames counted from now
    const auto nextStage = [this]()
    {
        ++mSubStage;
        mSubStageS = mStepTimeS;
        mSubStageFrames = mStepFrames;
    };
    const float viewHeight = rail->GetViewHeight();
    const float contentHeight = rail->GetContentHeight();
    const float offset = rail->GetScrollOffset();
    const FString state = FString::Printf(TEXT("content %.1f in a %.1f view, offset %.1f, layer offset %.1f"),
        contentHeight, viewHeight, offset, layer->GetScrollOffset());
    switch (mSubStage)
    {
    case 0:
        if (mStepTimeS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        Check(TEXT("k-rail-scroll"), rail->NeedsScroll() && viewHeight > 0.0f
            && viewHeight + SOLID_SMOKE_SCROLL_TOLERANCE < contentHeight && offset == 0.0f,
            FString::Printf(TEXT("%d tabs in a %.0f x %.0f page: needs scroll %d, %s"), page->GetTabCount(),
            SOLID_SMOKE_RAIL_PAGE_SIZE.X, SOLID_SMOKE_RAIL_PAGE_SIZE.Y, rail->NeedsScroll() ? 1 : 0, *state));
        rail->ScrollToEnd();
        nextStage();
        return false;
    case 1:
    {
        // ScrollToEnd: the last tab whole in view, the layer scrolled with it (at Slate's own offset: its end can
        // differ from content - view by its pixel rounding, SOLID_SMOKE_SCROLL_END_TOLERANCE, and the stack clamps
        // the drawn offset to Slate's end, so the traces stay on the tabs)
        if (mStepFrames - mSubStageFrames < SOLID_SMOKE_PAINT_FRAMES)
        {
            return false;
        }
        FString inside;
        const bool bInside = SolidSmokeInside(lastTab, scroll, inside);
        const float maxOffset = SOLSolidScroll::MaxOffset(contentHeight, viewHeight);
        Check(TEXT("k-rail-end"), bInside && offset > 0.0f && FMath::IsNearlyEqual(offset, maxOffset,
            SOLID_SMOKE_SCROLL_END_TOLERANCE) && FMath::IsNearlyEqual(layer->GetScrollOffset(), offset,
            SOLID_SMOKE_SCROLL_TOLERANCE), FString::Printf(TEXT("%s (end %.1f), last tab %s"), *state, maxOffset,
            *inside));
        TakeUiShot(TEXT("rail-end"));
        nextStage();
        return false;
    }
    case 2:
        if (mStepFrames - mSubStageFrames < SOLID_SMOKE_PAINT_FRAMES)
        {
            return false;
        }
        rail->ScrollToTop();
        nextStage();
        return false;
    case 3:
        // Back at the top (the last tab hidden), then the keyboard focus on it
        if (mStepFrames - mSubStageFrames < SOLID_SMOKE_PAINT_FRAMES)
        {
            return false;
        }
        mPreProgress = offset;
        lastTab->SetKeyboardFocus();
        nextStage();
        return false;
    case 4:
    {
        if (mStepTimeS - mSubStageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        FString inside;
        const bool bInside = SolidSmokeInside(lastTab, scroll, inside);
        Check(TEXT("k-rail-focus"), mPreProgress == 0.0f && lastTab->HasKeyboardFocus() && bInside && offset > 0.0f
            && FMath::IsNearlyEqual(layer->GetScrollOffset(), offset, SOLID_SMOKE_SCROLL_TOLERANCE)
            && rail->IsTraceShown(last) && layer->IsHoverTraceDrawn(), FString::Printf(TEXT("from offset %.1f: last "
            "tab focused %d, its trace shown %d drawn %d; %s, last tab %s"), mPreProgress,
            lastTab->HasKeyboardFocus() ? 1 : 0, rail->IsTraceShown(last) ? 1 : 0, layer->IsHoverTraceDrawn() ? 1 : 0,
            *state, *inside));
        page->RemoveFromParent();
        mRailPage.Reset();
        if (USOLMainMenuWidget* mainMenu = menu.GetMainMenu())
        {
            mainMenu->FocusDefault();
        }
        return true;
    }
    default:
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// The resume step's stages: Esc sent; on the first frame in flight the menu is input-transparent and closing with the
// ship's input back, and the sim time advanced over the pause (Q3); the frame after, the keyboard focus on the game
// viewport; then the slide-out until the widget leaves the viewport (within CLOSE_S plus a slack); then a settle
bool FSOLSolidMenuSmoke::TickPauseResume(USOLMenuSubsystem& menu)
{
    USOLPauseMenuWidget* pause = menu.GetPauseMenu();
    if (pause == nullptr)
    {
        Check(TEXT("v-resume"), false, TEXT("no pause menu"));
        return true;
    }
    // Moves to the next stage, its time and frames counted from now
    const auto nextStage = [this]()
    {
        ++mSubStage;
        mSubStageS = mStepTimeS;
        mSubStageFrames = mStepFrames;
    };
    const SOLMenuState::EMenuMode mode = menu.GetState().Mode;
    switch (mSubStage)
    {
    case 0:
    {
        if (mode != SOLMenuState::EMenuMode::Playing)
        {
            if (mStepTimeS >= SOLID_SMOKE_SETTLE_S)
            {
                Check(TEXT("v-resume"), false, FString::Printf(TEXT("Esc did not resume: mode %d, tab %d"),
                    static_cast<int32>(mode), static_cast<int32>(menu.GetState().Tab)));
                return true;
            }
            return false;
        }
        // At once: the menu input-transparent (a click goes to the game) and closing, the ship's input not suspended
        const bool bSuspended = SOLMenuState::SuspendsFlightInput(menu.GetState());
        Check(TEXT("v-resume"), pause->IsInViewport() && pause->GetVisibility() == ESlateVisibility::HitTestInvisible
            && pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Closing && !pause->GetPanelLook().bInteractive
            && !bSuspended, FString::Printf(TEXT("first frame in flight: in the viewport %d, visibility %d, phase %d, "
            "progress %.3f, flight input suspended %d"), pause->IsInViewport() ? 1 : 0,
            static_cast<int32>(pause->GetVisibility()), static_cast<int32>(pause->GetPanelAnim().Phase),
            pause->GetPanelProgress(), bSuspended ? 1 : 0));

        // SDD 14 step 7 (Q3): the sim time ran on under the pause (the world never paused), the view only dimmed
        const double simNow = SolidSmokeSimSeconds(menu.GetWorld());
        const bool bWorldPaused = menu.GetWorld() != nullptr && menu.GetWorld()->IsPaused();
        Check(TEXT("v-sim-runs"), mSimAtPauseS >= 0.0 && simNow > mSimAtPauseS && !bWorldPaused, FString::Printf(
            TEXT("sim time %.3f s -> %.3f s since J2000 (+%.3f s) over the pause, world paused %d, dim %.2f"),
            mSimAtPauseS, simNow, simNow - mSimAtPauseS, bWorldPaused ? 1 : 0, SOL::SolidMenu::PAUSE_DIM_ALPHA));
        SampleAnim(pause->GetPanelProgress(), 1.0f, false);
        mIsResumeInputChecked = false;
        nextStage();
        return false;
    }
    case 1:
    {
        // The frame after: the keyboard focus on the game viewport (the pawn's input mode, applied by Slate at the
        // frame's end), no pause button focused
        if (mStepFrames == mSubStageFrames + 1)
        {
            mIsResumeInputChecked = true;
            bool bMenuFocused = false;
            for (int32 tab = 0; tab < pause->GetTabButtonCount(); ++tab)
            {
                const USOLMenuButton* button = pause->GetTabButton(static_cast<SOLMenuState::EMenuTab>(tab));
                bMenuFocused |= button != nullptr && button->HasKeyboardFocus();
            }
            const ULocalPlayer* player = mController.IsValid() ? mController->GetLocalPlayer() : nullptr;
            const TSharedPtr<SViewport> viewport = player != nullptr && player->ViewportClient != nullptr
                ? player->ViewportClient->GetGameViewportWidget() : nullptr;
            const TSharedPtr<SWidget> focus = FSlateApplication::IsInitialized()
                ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
            const bool bViewportFocused = viewport.IsValid() && viewport->HasAnyUserFocusOrFocusedDescendants();
            Check(TEXT("v-resume-input"), !bMenuFocused && bViewportFocused, FString::Printf(TEXT("the frame after: a "
                "pause tab focused %d, the game viewport focused %d, focus on %s, cursor %d (the ship's steering "
                "toggle)"), bMenuFocused ? 1 : 0, bViewportFocused ? 1 : 0, focus.IsValid() ? *focus->GetTypeAsString()
                : TEXT("nothing"), mController.IsValid() && mController->bShowMouseCursor ? 1 : 0));
        }

        // The slide-out each frame until the widget leaves the viewport
        const double elapsed = mStepTimeS - mSubStageS;
        if (pause->IsInViewport() && elapsed < SOL::SolidMenu::CLOSE_S + 1.0)
        {
            SampleAnim(pause->GetPanelProgress(), 1.0f, false);
            return false;
        }
        if (!mIsResumeInputChecked)
        {
            Check(TEXT("v-resume-input"), false, FString::Printf(TEXT("the frame after the first in flight was never "
                "read (stage began on step frame %d, left on %d)"), mSubStageFrames, mStepFrames));
        }
        Check(TEXT("v-resume-slide-out"), !pause->IsInViewport() && pause->GetPanelAnim().Phase
            == ESOLHoloMenuPhase::Closed && mSawMidProgress && mIsMonotonic && elapsed <= SOL::SolidMenu::CLOSE_S
            + SOLID_SMOKE_CLOSE_SLACK_S, FString::Printf(TEXT("left the viewport %.3f s after the first frame in "
            "flight (CLOSE_S %.2f), phase %d, first progress %.3f, mid %d, monotonic %d"), elapsed,
            SOL::SolidMenu::CLOSE_S, static_cast<int32>(pause->GetPanelAnim().Phase), mFirstProgress,
            mSawMidProgress ? 1 : 0, mIsMonotonic ? 1 : 0));
        TakeUiShot(TEXT("pause-resumed"));
        nextStage();
        return false;
    }
    default:
        return mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// The re-pause step's stages: paused (RequestEscape at the step's start) and settled open; Esc on Resume; once the
// slide-out is below SOLID_SMOKE_REVERSE_AT, a re-pause: the slide reverses from where it was, never leaving the
// viewport, and ends open
bool FSOLSolidMenuSmoke::TickPauseReverse(USOLMenuSubsystem& menu)
{
    USOLPauseMenuWidget* pause = menu.GetPauseMenu();
    if (pause == nullptr)
    {
        Check(TEXT("v-reopen-reverses"), false, TEXT("no pause menu"));
        return true;
    }
    // Moves to the next stage, its time and frames counted from now
    const auto nextStage = [this]()
    {
        ++mSubStage;
        mSubStageS = mStepTimeS;
        mSubStageFrames = mStepFrames;
    };
    const SOLMenuState::EMenuMode mode = menu.GetState().Mode;
    const float progress = pause->GetPanelProgress();
    switch (mSubStage)
    {
    case 0:
        if (mode == SOLMenuState::EMenuMode::Paused && pause->IsInViewport())
        {
            nextStage();
        }
        return false;
    case 1:
        // Settled open: Esc on the focused Resume tab resumes
        if (mStepTimeS - mSubStageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        SolidSmokeKey(EKeys::Escape);
        nextStage();
        return false;
    case 2:
        // In flight, mid slide-out: the re-pause
        mHasLeftViewport |= !pause->IsInViewport();
        if (mode == SOLMenuState::EMenuMode::Playing && pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Closing
            && progress > 0.0f && progress < SOLID_SMOKE_REVERSE_AT)
        {
            mPreProgress = progress;
            mCueOpenBeforeReopen = SolidSmokeCues(menu, ESOLHoloCue::Open);
            menu.RequestEscape();
            nextStage();
        }
        else if (mHasLeftViewport || mStepTimeS - mSubStageS > SOLID_SMOKE_SETTLE_S)
        {
            Check(TEXT("v-reopen-reverses"), false, FString::Printf(TEXT("no mid slide-out seen: mode %d, phase %d, "
                "progress %.3f, left the viewport %d"), static_cast<int32>(mode),
                static_cast<int32>(pause->GetPanelAnim().Phase), progress, mHasLeftViewport ? 1 : 0));
            return true;
        }
        return false;
    case 3:
        // The first frame paused again: the progress read, then a settle
        mHasLeftViewport |= !pause->IsInViewport();
        if (mode != SOLMenuState::EMenuMode::Paused)
        {
            return false;
        }
        mPostProgress = progress;
        nextStage();
        return false;
    case 4:
    {
        mHasLeftViewport |= !pause->IsInViewport();
        if (mStepTimeS - mSubStageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        const bool bOpen = pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Open
            && pause->GetPanelLook().SlideX == 0.0f && pause->GetVisibility() != ESlateVisibility::HitTestInvisible;
        Check(TEXT("v-reopen-reverses"), mPreProgress > 0.0f && mPreProgress < 1.0f && mPostProgress > 0.0f
            && mPostProgress < 1.0f && FMath::Abs(mPostProgress - mPreProgress) <= SOLID_SMOKE_PAUSE_REVERSAL_TOLERANCE
            && !mHasLeftViewport && bOpen, FString::Printf(TEXT("sliding out at %.3f, re-paused at %.3f, left the "
            "viewport %d, then open %d (phase %d, slide %.1f)"), mPreProgress, mPostProgress, mHasLeftViewport ? 1 : 0,
            bOpen ? 1 : 0, static_cast<int32>(pause->GetPanelAnim().Phase), pause->GetPanelLook().SlideX));
        CheckReopenNoCue(TEXT("v-reopen-no-cue"), menu);
        TakeUiShot(TEXT("pause-reversed"));
        return true;
    }
    default:
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// The pause strength 0 step's stages (strength 0 applied at the step's start): Esc resumes and the menu leaves at
// once; the pause opens at once (no slide, the dim up, the traces lit); a page opens at once and Esc closes it at once;
// strength 1 applied back
bool FSOLSolidMenuSmoke::TickPauseStrengthZero(USOLMenuSubsystem& menu)
{
    USOLPauseMenuWidget* pause = menu.GetPauseMenu();
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(&menu);
    const float s = solid != nullptr ? solid->GetStrength() : -1.0f;
    if (pause == nullptr)
    {
        Check(TEXT("v-s0-close"), false, TEXT("no pause menu"));
        ApplyStrength(menu, SOL::Settings::HUD_EFFECTS_DEFAULT);
        return true;
    }
    // Moves to the next stage, its time and frames counted from now
    const auto nextStage = [this]()
    {
        ++mSubStage;
        mSubStageS = mStepTimeS;
        mSubStageFrames = mStepFrames;
    };
    const SOLMenuState::EMenuMode mode = menu.GetState().Mode;
    const USOLSolidPanel* shell = pause->GetShellPanel();
    switch (mSubStage)
    {
    case 0:
        if (mStepFrames - mSubStageFrames < SOLID_SMOKE_PAINT_FRAMES)
        {
            return false;
        }
        SolidSmokeKey(EKeys::Escape);
        nextStage();
        return false;
    case 1:
        // The first frame in flight: the menu already gone
        if (mode != SOLMenuState::EMenuMode::Playing)
        {
            return false;
        }
        Check(TEXT("v-s0-close"), FMath::IsNearlyZero(s) && !pause->IsInViewport()
            && pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Closed, FString::Printf(TEXT("strength %.2f: first "
            "frame in flight, in the viewport %d, phase %d"), s, pause->IsInViewport() ? 1 : 0,
            static_cast<int32>(pause->GetPanelAnim().Phase)));
        menu.RequestEscape();
        nextStage();
        return false;
    case 2:
    {
        // The first frame paused: open at once; then a page clicked opens at once
        if (mode != SOLMenuState::EMenuMode::Paused)
        {
            return false;
        }
        const FSOLPausePanelLook& look = pause->GetPanelLook();
        Check(TEXT("v-s0-open"), FMath::IsNearlyZero(s) && pause->IsInViewport()
            && pause->GetPanelAnim().Phase == ESOLHoloMenuPhase::Open && look.SlideX == 0.0f
            && FMath::IsNearlyEqual(look.Progress, 1.0f) && FMath::IsNearlyEqual(pause->GetDimAlpha(),
            SOL::SolidMenu::PAUSE_DIM_ALPHA, SOLID_SMOKE_TOLERANCE)
            && FMath::IsNearlyEqual(pause->GetTracePower(), 1.0f),
            FString::Printf(TEXT("strength %.2f: first frame paused, phase %d, slide %.1f, progress %.3f, dim %.3f, "
            "trace power %.3f"), s, static_cast<int32>(pause->GetPanelAnim().Phase), look.SlideX, look.Progress,
            pause->GetDimAlpha(), pause->GetTracePower()));
        SolidSmokeClick(pause->GetTabButton(SOLMenuState::EMenuTab::Controls));
        Check(TEXT("v-s0-page-open"), shell != nullptr && pause->GetShellAnim().Phase == ESOLHoloMenuPhase::Open
            && FMath::IsNearlyEqual(pause->GetShellProgress(), 1.0f) && shell->GetVisibility()
            == ESlateVisibility::Visible && pause->GetShownPageIndex() == static_cast<int32>(
            SOLMenuState::EMenuTab::Controls), FString::Printf(TEXT("right after the click: shell phase %d, progress "
            "%.3f, shown %d"), static_cast<int32>(pause->GetShellAnim().Phase), pause->GetShellProgress(),
            pause->GetShownPageIndex()));
        TakeUiShot(TEXT("pause-s0"));
        nextStage();
        return false;
    }
    case 3:
        // Esc on the page: it closes at once
        if (mStepFrames - mSubStageFrames < SOLID_SMOKE_PAINT_FRAMES)
        {
            return false;
        }
        SolidSmokeKey(EKeys::Escape);
        Check(TEXT("v-s0-page-close"), menu.GetState().Mode == SOLMenuState::EMenuMode::Paused
            && menu.GetState().Tab == SOLMenuState::EMenuTab::Resume && shell != nullptr
            && pause->GetShellAnim().Phase == ESOLHoloMenuPhase::Closed && shell->GetVisibility()
            == ESlateVisibility::Hidden && pause->GetShownPageIndex() == 0, FString::Printf(TEXT("right after Esc: tab "
            "%d, shell phase %d, visibility %d, shown %d"), static_cast<int32>(menu.GetState().Tab),
            static_cast<int32>(pause->GetShellAnim().Phase),
            shell != nullptr ? static_cast<int32>(shell->GetVisibility()) : -1, pause->GetShownPageIndex()));
        ApplyStrength(menu, SOL::Settings::HUD_EFFECTS_DEFAULT);
        nextStage();
        return false;
    default:
        return mStepTimeS - mSubStageS >= SOLID_SMOKE_SETTLE_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// A rows sweep's stages over a read-only page (46i): each tab with a left column clicked in turn, its rows read a few
// frames later (CheckSweepTab) and, for the first shotTabs tabs, screenshotted (the next tab waits for the shot); one
// "<id>-rows" check once every such tab was read; returns true then
bool FSOLSolidMenuSmoke::TickRowsSweep(const TCHAR* id, USOLReadOnlyPageWidget* page, const UWidget* shell,
    const TCHAR* shotName, const int32 shotTabs)
{
    const FString label = FString::Printf(TEXT("%s-rows"), id);
    if (page == nullptr || shell == nullptr)
    {
        Check(*label, false, TEXT("no page or shell"));
        return true;
    }

    // Returns the first tab at or after from with a two-column row, or INDEX_NONE
    const auto nextTab = [page](const int32 from)
    {
        int32 found = INDEX_NONE;
        for (const USOLReadOnlyPageWidget::FColumnText& column : page->GetColumnTexts())
        {
            if (column.Tab >= from && (found == INDEX_NONE || column.Tab < found))
            {
                found = column.Tab;
            }
        }
        return found;
    };
    // Shows a tab as its button's click does and restarts the wait for its paint
    const auto showTab = [this, page](const int32 tab)
    {
        mSweepTab = tab;
        mIsSweepRead = false;
        mIsSweepShot = false;
        mSweepFrame = mStepFrames;
        if (USOLMenuButton* button = page->GetTabButton(tab))
        {
            button->OnMenuClicked.ExecuteIfBound(button->GetIndex());
        }
    };
    if (mSweepTab == INDEX_NONE)
    {
        const int32 first = nextTab(0);
        if (first == INDEX_NONE)
        {
            Check(*label, false, TEXT("the page has no two-column rows"));
            return true;
        }
        showTab(first);
        return false;
    }

    // The tab painted at its layout: its rows read, its screenshot requested
    if (!mIsSweepRead)
    {
        if (mStepFrames - mSweepFrame < SOLID_SMOKE_PAINT_FRAMES)
        {
            return false;
        }
        CheckSweepTab(*page, *shell);
        mIsSweepRead = true;
        mSweepFrame = mStepFrames;
        if (shotName != nullptr && mSweepShots < shotTabs)
        {
            ++mSweepShots;
            mIsSweepShot = true;
            TakeUiShot(*FString::Printf(TEXT("%s-%d"), shotName, mSweepTab));
        }
        return false;
    }
    if (mIsSweepShot && mStepFrames - mSweepFrame < SOLID_SMOKE_SHOT_FRAMES)
    {
        return false;
    }
    const int32 next = nextTab(mSweepTab + 1);
    if (next != INDEX_NONE)
    {
        showTab(next);
        return false;
    }
    Check(*label, mIsSweepOk && mSweepRows > 0, FString::Printf(TEXT("%d rows at left column %.0f:%s"), mSweepRows,
        page->GetLeftColumnWidth(), *mSweepDetail));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Reads a rows sweep's shown tab (46i): each two-column row freshly arranged from the tab's scroll box: the left box
// at the page's column width, the widest word of the left text and of the right cell inside its cell (measured at the
// page's layout scale), the right cell after the left one, both inside the scroll box and the shell; adds to the
// sweep's verdict and detail
void FSOLSolidMenuSmoke::CheckSweepTab(const USOLReadOnlyPageWidget& page, const UWidget& shell)
{
    const float scale = page.GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
    const UScrollBox* scroll = page.GetTabScroll(mSweepTab);
    const FGeometry scrollGeometry = scroll != nullptr ? scroll->GetCachedGeometry() : FGeometry();
    const FGeometry& shellGeometry = shell.GetCachedGeometry();
    const double scrollMin = scrollGeometry.GetAbsolutePosition().X;
    const double scrollMax = scrollMin + scrollGeometry.GetAbsoluteSize().X;
    const double shellMin = shellGeometry.GetAbsolutePosition().X;
    const double shellMax = shellMin + shellGeometry.GetAbsoluteSize().X;
    const double tolPx = SOLID_SMOKE_FIT_TOLERANCE_PX;
    bool bTabOk = page.GetSelectedTab() == mSweepTab && page.GetShownContentIndex() == mSweepTab && scale > 0.0f
        && scrollMax > scrollMin && shellMax > shellMin;

    // Returns the widest word of a text block as drawn at the page's scale (outWord: that word), -1 if not measured
    const auto widestWord = [scale](const UTextBlock& text, FString& outWord)
    {
        TArray<FString> words;
        text.GetText().ToString().ParseIntoArray(words, TEXT(" "));
        float widest = 0.0f;
        for (const FString& word : words)
        {
            const float width = SolidSmokeTextWidth(word, text.GetFont(), scale);
            if (width < 0.0f)
            {
                return -1.0f;
            }
            if (width > widest)
            {
                widest = width;
                outWord = word;
            }
        }
        return widest;
    };

    // Every row of the tab: its cells' widths and places, the widest words against them
    int32 rows = 0;
    int32 capRows = 0;
    int32 paintedCapRows = 0;
    float narrowestValue = TNumericLimits<float>::Max();
    float tightestRoom = TNumericLimits<float>::Max();
    FString tightest;
    FString failures;
    for (const USOLReadOnlyPageWidget::FColumnText& column : page.GetColumnTexts())
    {
        if (column.Tab != mSweepTab)
        {
            continue;
        }
        ++rows;
        if ((column.Text == nullptr && column.Caps == nullptr) || column.Box == nullptr || column.Value == nullptr
            || scroll == nullptr)
        {
            bTabOk = false;
            failures += TEXT(" [a row without its widgets]");
            continue;
        }
        const FGeometry box = SolidSmokeArrangedGeometry(column.Box, scroll);
        const FGeometry value = SolidSmokeArrangedGeometry(column.Value, scroll);
        const float boxWidth = box.GetLocalSize().X;
        const float valueWidth = value.GetLocalSize().X;
        FString labelWord;
        FString valueWord;
        // A key cap cell (33b): its laid-out caps as the "word", every legend measured; a cap row the last paint drew
        // whole (hull material, every legend shaped) is counted (rows below the fold are not painted)
        const bool bCaps = column.Caps != nullptr;
        if (bCaps)
        {
            labelWord = column.Caps->GetText();
            capRows += 1;
            paintedCapRows += column.Caps->WasPaintedWhole() ? 1 : 0;
        }
        const float labelWidest = bCaps ? column.Caps->GetLaidOutWidth() : widestWord(*column.Text, labelWord);
        const bool bCapsTall = !bCaps || column.Caps->AreLegendsMeasured();
        const float valueWidest = widestWord(*column.Value, valueWord);
        const double boxLeft = box.GetAbsolutePosition().X;
        const double boxRight = boxLeft + box.GetAbsoluteSize().X;
        const double valueLeft = value.GetAbsolutePosition().X;
        const double valueRight = valueLeft + value.GetAbsoluteSize().X;
        const bool bBox = FMath::Abs(boxWidth - column.Width) <= SOLID_SMOKE_FIT_TOLERANCE;
        const bool bLabel = labelWidest >= 0.0f && labelWidest <= column.Width + SOLID_SMOKE_FIT_TOLERANCE;
        const bool bValue = valueWidth > 0.0f && valueWidest >= 0.0f
            && valueWidest <= valueWidth + SOLID_SMOKE_FIT_TOLERANCE;
        const bool bInside = valueLeft >= boxRight - tolPx && boxLeft >= scrollMin - tolPx
            && valueRight <= scrollMax + tolPx && boxLeft >= shellMin - tolPx && valueRight <= shellMax + tolPx;
        narrowestValue = FMath::Min(narrowestValue, valueWidth);
        if (valueWidth - valueWidest < tightestRoom)
        {
            tightestRoom = valueWidth - valueWidest;
            tightest = FString::Printf(TEXT("\"%s\" %.1f in %.1f"), *valueWord, valueWidest, valueWidth);
        }
        if (!(bBox && bLabel && bValue && bInside && bCapsTall))
        {
            bTabOk = false;
            failures += FString::Printf(TEXT(" [\"%s\": box %.1f (want %.1f), label word \"%s\" %.1f (caps tall %d), "
                "value word \"%s\" %.1f in %.1f, cells %.0f-%.0f / %.0f-%.0f in content %.0f-%.0f and shell "
                "%.0f-%.0f]"), *(bCaps ? column.Caps->GetText() : column.Text->GetText().ToString()).Left(24), boxWidth,
                column.Width, *labelWord, labelWidest, bCapsTall ? 1 : 0,
                *valueWord, valueWidest, valueWidth, boxLeft, boxRight, valueLeft, valueRight, scrollMin, scrollMax,
                shellMin, shellMax);
        }
    }
    mSweepRows += rows;
    mIsSweepOk &= bTabOk && rows > 0 && (capRows == 0 || paintedCapRows > 0);
    mSweepDetail += FString::Printf(TEXT(" [tab %d: %d rows (%d of %d cap rows painted whole), value cells from %.1f, "
        "tightest %s%s]"), mSweepTab, rows, paintedCapRows, capRows, rows > 0 ? narrowestValue : 0.0f, *tightest,
        *failures);
}

//////////////////////////////////////////////////////////////////////////
// Checks a Settings page's narrow rows at 1000x800 on the shown tab (46i): "<id>-rows" the tab narrow, each label's
// column at the narrow width, its widest word inside it and the wrapped label (its desired height) inside its row box;
// Key bindings: every key name inside its slot, every slot at least SETTINGS_KEY_TEXT_MIN_WIDTH, the slot left of its
// Rebind button and the narrow Rebind box (a minimum width) with room for the button's label ("<id>-names");
// "<id>-sliders" every Gameplay slider at least SETTINGS_SLIDER_MIN_WIDTH wide. The widgets are read freshly arranged
// from the page (a row scrolled out of view is culled from the paint, so its cached geometry is stale)
void FSOLSolidMenuSmoke::CheckSettingsRoom(const TCHAR* id, const USOLSettingsPageWidget* page, const bool bKeys)
{
    const ESOLSettingsTab tab = bKeys ? ESOLSettingsTab::KeyBindings : ESOLSettingsTab::Gameplay;
    const FString rowsId = FString::Printf(TEXT("%s-rows"), id);
    const FString roomId = FString::Printf(TEXT("%s-%s"), id, bKeys ? TEXT("names") : TEXT("sliders"));
    if (page == nullptr)
    {
        Check(*rowsId, false, TEXT("no Settings page"));
        Check(*roomId, false, TEXT("no Settings page"));
        return;
    }
    const float scale = page->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
    const float column = SOLSettingsTabs::LabelColumnWidth(tab, page->GetFitRowWidth());
    const float narrowColumn = bKeys ? SOL::SolidMenu::SETTINGS_KEYS_NARROW_LABEL_WIDTH
        : SOL::SolidMenu::SETTINGS_GAMEPLAY_NARROW_LABEL_WIDTH;

    // Returns whether a widget's freshly arranged rectangle lies inside another's (absolute space)
    const auto inside = [page](const UWidget* inner, const UWidget* outer)
    {
        const FGeometry in = SolidSmokeArrangedGeometry(inner, page);
        const FGeometry out = SolidSmokeArrangedGeometry(outer, page);
        const FVector2D inMin = in.GetAbsolutePosition();
        const FVector2D inMax = inMin + in.GetAbsoluteSize();
        const FVector2D outMin = out.GetAbsolutePosition();
        const FVector2D outMax = outMin + out.GetAbsoluteSize();
        return in.GetAbsoluteSize().X > 0.0 && inMin.X >= outMin.X - SOLID_SMOKE_FIT_TOLERANCE_PX
            && inMin.Y >= outMin.Y - SOLID_SMOKE_FIT_TOLERANCE_PX && inMax.X <= outMax.X + SOLID_SMOKE_FIT_TOLERANCE_PX
            && inMax.Y <= outMax.Y + SOLID_SMOKE_FIT_TOLERANCE_PX;
    };

    // The tab's rows: the narrow column, each word inside it, each (wrapped) label inside its row box
    bool bRows = scale > 0.0f && page->GetSelectedTab() == tab && page->IsNarrowLayout(tab) && column == narrowColumn;
    int32 rows = 0;
    int32 grown = 0;
    float tallest = 0.0f;
    float widestWord = 0.0f;
    FString rowsDetail;
    for (int32 index = 0; index < page->GetRowLabelCount(); ++index)
    {
        ESOLSettingsTab labelTab = ESOLSettingsTab::Count;
        const UTextBlock* label = page->GetRowLabel(index, labelTab);
        if (labelTab != tab || label == nullptr)
        {
            continue;
        }
        ++rows;
        const USizeBox* labelBox = page->GetRowLabelBox(index);
        const USizeBox* rowBox = page->GetRowBox(index);
        const FString text = label->GetText().ToString();
        const float word = SolidSmokeWidestWord(text, label->GetFont(), scale);
        widestWord = FMath::Max(widestWord, word);
        const float rowHeight = rowBox != nullptr ? SolidSmokeArrangedGeometry(rowBox, page).GetLocalSize().Y : 0.0f;
        tallest = FMath::Max(tallest, rowHeight);
        grown += rowHeight > SOL::MenuStyle::SETTINGS_ROW_HEIGHT + SOLID_SMOKE_FIT_TOLERANCE ? 1 : 0;
        const bool bColumn = labelBox != nullptr && labelBox->GetWidthOverride() == column;
        const bool bWord = word >= 0.0f && word <= column + SOLID_SMOKE_FIT_TOLERANCE;
        const bool bInside = inside(label, rowBox);

        // The label's own (wrapped) height against its row: Slate clamps the arranged label to its centred slot, so
        // only the desired height shows a row box that did not grow
        const float labelHeight = static_cast<float>(label->GetDesiredSize().Y);
        const bool bTall = rowBox != nullptr && labelHeight > 0.0f
            && labelHeight <= rowHeight + SOLID_SMOKE_FIT_TOLERANCE;
        if (!(bColumn && bWord && bInside && bTall))
        {
            bRows = false;
            rowsDetail += FString::Printf(TEXT(" [\"%s\": column %.1f, widest word %.1f, inside its row %d, label %.1f "
                "tall in a %.1f row]"), *text.Left(32), labelBox != nullptr ? labelBox->GetWidthOverride() : -1.0f,
                word, bInside ? 1 : 0, labelHeight, rowHeight);
        }
    }
    bRows &= rows > 0;
    Check(*rowsId, bRows, FString::Printf(TEXT("tab %d shown %d, narrow %d at row %.1f, column %.1f (want %.1f), %d "
        "rows: widest word %.1f, %d grown (tallest %.1f) at scale %.3f%s"), static_cast<int32>(tab),
        static_cast<int32>(page->GetSelectedTab()), page->IsNarrowLayout(tab) ? 1 : 0, page->GetFitRowWidth(), column,
        narrowColumn, rows, widestWord, grown, tallest, scale, *rowsDetail));

    if (!bKeys)
    {
        // Gameplay: each slider's laid-out width (the room left of its value readout)
        const ESOLSettingsSlider sliders[] = { ESOLSettingsSlider::HudScale, ESOLSettingsSlider::HudEffects,
            ESOLSettingsSlider::HoloMenuEffects, ESOLSettingsSlider::ChaseSafetyMargin };
        bool bSliders = true;
        FString widths;
        for (const ESOLSettingsSlider slider : sliders)
        {
            const USOLSolidSlider* control = page->GetSlider(slider);
            const float width = control != nullptr ? SolidSmokeArrangedGeometry(control, page).GetLocalSize().X : -1.0f;
            bSliders &= width >= SOL::SolidMenu::SETTINGS_SLIDER_MIN_WIDTH - SOLID_SMOKE_FIT_TOLERANCE;
            widths += FString::Printf(TEXT(" %.1f"), width);
        }
        Check(*roomId, bSliders, FString::Printf(TEXT("slider widths%s (at least %.0f)"), *widths,
            SOL::SolidMenu::SETTINGS_SLIDER_MIN_WIDTH));
        return;
    }

    // Key bindings: each key name inside its slot, the slot left of its Rebind button, each narrow Rebind box with
    // room for the button's desired width (its label and padding)
    bool bKeysFit = scale > 0.0f;
    float narrowestSlot = TNumericLimits<float>::Max();
    float widest = 0.0f;
    float widestRebind = 0.0f;
    FString widestName;
    FString keysDetail;
    for (int32 binding = 0; binding < SOLKeyBindings::BINDING_COUNT; ++binding)
    {
        const USOLKeyCapRow* cell = page->GetKeyCell(binding);
        const USizeBox* rebindBox = page->GetRebindBox(binding);
        const USOLMenuButton* rebind = page->GetRebindButton(binding);
        if (cell == nullptr || rebindBox == nullptr || rebind == nullptr)
        {
            bKeysFit = false;
            keysDetail += FString::Printf(TEXT(" [binding %d: missing widget]"), binding);
            continue;
        }
        // The key's caps (33b): their laid-out width against the cell's slot
        const FString name = cell->GetText();
        const float width = cell->GetLaidOutWidth();
        const FGeometry slotGeometry = SolidSmokeArrangedGeometry(cell, page);
        const FGeometry rebindGeometry = SolidSmokeArrangedGeometry(rebind, page);
        const float slot = slotGeometry.GetLocalSize().X;
        const double slotRight = slotGeometry.GetAbsolutePosition().X + slotGeometry.GetAbsoluteSize().X;
        const double rebindLeft = rebindGeometry.GetAbsolutePosition().X;
        const float rebindWidth = rebindGeometry.GetLocalSize().X;
        const float rebindDesired = static_cast<float>(rebind->GetDesiredSize().X);
        narrowestSlot = FMath::Min(narrowestSlot, slot);
        widestRebind = FMath::Max(widestRebind, rebindDesired);
        if (width > widest)
        {
            widest = width;
            widestName = name;
        }

        // The narrow box: a minimum of SETTINGS_REBIND_NARROW_WIDTH (no fixed width), the button laid out at least
        // that wide and at least its own desired width
        const bool bNarrowBox = !rebindBox->IsWidthOverride() && rebindBox->IsMinDesiredWidthOverride()
            && rebindBox->GetMinDesiredWidth() == SOL::SolidMenu::SETTINGS_REBIND_NARROW_WIDTH
            && rebindWidth >= SOL::SolidMenu::SETTINGS_REBIND_NARROW_WIDTH - SOLID_SMOKE_FIT_TOLERANCE;
        const bool bFits = width >= 0.0f && slot >= SOL::SolidMenu::SETTINGS_KEY_TEXT_MIN_WIDTH
            - SOLID_SMOKE_FIT_TOLERANCE && width <= slot + SOLID_SMOKE_FIT_TOLERANCE
            && slotRight <= rebindLeft + SOLID_SMOKE_FIT_TOLERANCE_PX && bNarrowBox && rebindDesired > 0.0f
            && rebindDesired <= rebindWidth + SOLID_SMOKE_FIT_TOLERANCE;
        if (!bFits)
        {
            bKeysFit = false;
            keysDetail += FString::Printf(TEXT(" [\"%s\" %.1f in a %.1f slot ending %.1f, Rebind at %.1f, box %.1f "
                "(narrow %d), button %.1f]"), *name, width, slot, slotRight, rebindLeft, rebindWidth,
                bNarrowBox ? 1 : 0, rebindDesired);
        }
    }
    Check(*roomId, bKeysFit, FString::Printf(TEXT("%d key rows: the narrowest slot %.1f (at least %.0f), the widest "
        "key \"%s\" %.1f, Rebind boxes at least %.0f (button %.1f wide)%s"), SOLKeyBindings::BINDING_COUNT,
        narrowestSlot, SOL::SolidMenu::SETTINGS_KEY_TEXT_MIN_WIDTH, *widestName, widest,
        SOL::SolidMenu::SETTINGS_REBIND_NARROW_WIDTH, widestRebind, *keysDetail));
}

//////////////////////////////////////////////////////////////////////////
// Checks a Settings page in a wide view keeps (or is back in) the wide layout (46i): no tab narrow, each label column,
// row box and Rebind box at its wide size with no narrow override left (u-settings-wide, j-settings-wide)
void FSOLSolidMenuSmoke::CheckSettingsWide(const TCHAR* id, const USOLSettingsPageWidget* page)
{
    if (page == nullptr)
    {
        Check(id, false, TEXT("no Settings page"));
        return;
    }
    bool bWide = page->GetFitRowWidth() > 0.0f;
    for (int32 tab = 0; tab < static_cast<int32>(ESOLSettingsTab::Count); ++tab)
    {
        bWide &= !page->IsNarrowLayout(static_cast<ESOLSettingsTab>(tab));
    }
    int32 offColumns = 0;
    for (int32 index = 0; index < page->GetRowLabelCount(); ++index)
    {
        ESOLSettingsTab tab = ESOLSettingsTab::Count;
        const UTextBlock* label = page->GetRowLabel(index, tab);
        const USizeBox* labelBox = page->GetRowLabelBox(index);
        const USizeBox* rowBox = page->GetRowBox(index);
        // GetHeightOverride keeps its value after ClearHeightOverride, so the override flags are read too: a row left
        // narrow (a minimum height, no fixed one) counts as off; a Key bindings row (33b) always grows from the
        // minimum height and has no fixed one
        const bool bGrows = tab == ESOLSettingsTab::KeyBindings;
        const float rowHeight = SOL::MenuStyle::SETTINGS_ROW_HEIGHT;
        const bool bHeight = rowBox != nullptr && (bGrows ? !rowBox->IsHeightOverride()
            && rowBox->IsMinDesiredHeightOverride() && rowBox->GetMinDesiredHeight() == rowHeight
            : rowBox->IsHeightOverride() && !rowBox->IsMinDesiredHeightOverride()
            && rowBox->GetHeightOverride() == rowHeight);
        offColumns += label == nullptr || labelBox == nullptr || !bHeight || label->GetAutoWrapText()
            || labelBox->GetWidthOverride() != SOLSettingsTabs::LabelColumnWidth(tab) ? 1 : 0;
    }
    int32 offRebinds = 0;
    for (int32 binding = 0; binding < SOLKeyBindings::BINDING_COUNT; ++binding)
    {
        // A wide box: the fixed SETTINGS_REBIND_WIDTH, none of the narrow minimum width left over, held to the row
        // height (33b: in both states)
        const USizeBox* rebindBox = page->GetRebindBox(binding);
        offRebinds += rebindBox == nullptr || !rebindBox->IsWidthOverride() || rebindBox->IsMinDesiredWidthOverride()
            || !rebindBox->IsHeightOverride() || rebindBox->GetHeightOverride() != SOL::MenuStyle::SETTINGS_ROW_HEIGHT
            || rebindBox->GetWidthOverride() != SOL::MenuStyle::SETTINGS_REBIND_WIDTH ? 1 : 0;
    }
    Check(id, bWide && offColumns == 0 && offRebinds == 0 && page->GetRowLabelCount() > 0, FString::Printf(
        TEXT("row %.1f: narrow Gameplay %d, Key bindings %d; %d of %d rows off their wide column or height, %d of %d "
        "Rebind boxes off %.0f"), page->GetFitRowWidth(), page->IsNarrowLayout(ESOLSettingsTab::Gameplay) ? 1 : 0,
        page->IsNarrowLayout(ESOLSettingsTab::KeyBindings) ? 1 : 0, offColumns, page->GetRowLabelCount(), offRebinds,
        SOLKeyBindings::BINDING_COUNT, SOL::MenuStyle::SETTINGS_REBIND_WIDTH));
}

//////////////////////////////////////////////////////////////////////////
// Checks the shown Key bindings tab's key cap cells (33b, D20): each cell's wrap width the fitted row's KeyCellWidth,
// its caps laid out inside that width and its slot, no taller than its row, left of its Rebind button, and the button
// inside the row. The widgets are read freshly arranged from the page (a row out of view is culled from the paint)
void FSOLSolidMenuSmoke::CheckSettingsKeyCaps(const TCHAR* id, const USOLSettingsPageWidget* page)
{
    if (page == nullptr)
    {
        Check(id, false, TEXT("no Settings page"));
        return;
    }
    const float wrap = SOLSettingsTabs::KeyCellWidth(page->GetFitRowWidth());
    bool bOk = page->GetSelectedTab() == ESOLSettingsTab::KeyBindings && page->GetFitRowWidth() > 0.0f
        && page->GetKeyCellWidth() == wrap;
    int32 rows = 0;
    int32 wrapped = 0;
    int32 painted = 0;
    float widest = 0.0f;
    FString widestName;
    FString detail;
    for (int32 index = 0; index < page->GetRowLabelCount(); ++index)
    {
        ESOLSettingsTab labelTab = ESOLSettingsTab::Count;
        if (page->GetRowLabel(index, labelTab) == nullptr || labelTab != ESOLSettingsTab::KeyBindings)
        {
            continue;
        }

        // The key rows are the tab's labelled rows in binding order
        const int32 binding = rows++;
        const USOLKeyCapRow* cell = page->GetKeyCell(binding);
        const USOLMenuButton* rebind = page->GetRebindButton(binding);
        const USizeBox* rowBox = page->GetRowBox(index);
        if (cell == nullptr || rebind == nullptr || rowBox == nullptr)
        {
            bOk = false;
            detail += FString::Printf(TEXT(" [binding %d: missing widget]"), binding);
            continue;
        }
        const FGeometry slot = SolidSmokeArrangedGeometry(cell, page);
        const FGeometry button = SolidSmokeArrangedGeometry(rebind, page);
        const FGeometry row = SolidSmokeArrangedGeometry(rowBox, page);
        const float width = cell->GetLaidOutWidth();
        const float height = cell->GetLaidOutHeight();
        const double slotRight = slot.GetAbsolutePosition().X + slot.GetAbsoluteSize().X;
        const double buttonLeft = button.GetAbsolutePosition().X;
        const double buttonRight = buttonLeft + button.GetAbsoluteSize().X;
        const double rowLeft = row.GetAbsolutePosition().X;
        const double rowRight = rowLeft + row.GetAbsoluteSize().X;
        const double tolPx = SOLID_SMOKE_FIT_TOLERANCE_PX;
        wrapped += cell->GetLaidOutLines() > 1 ? 1 : 0;
        if (width > widest)
        {
            widest = width;
            widestName = cell->GetText();
        }
        // A row in view was painted: its caps drawn whole (hull material, every legend shaped at the paint scale)
        const bool bInView = page->IsBindingRowInView(binding);
        const bool bPainted = !bInView || cell->WasPaintedWhole();
        painted += bInView && bPainted ? 1 : 0;
        const bool bFits = !cell->IsShowingMessage() && cell->GetModel().Caps.Num() > 0 && cell->AreLegendsMeasured()
            && bPainted && width > 0.0f
            && width <= wrap + SOLID_SMOKE_FIT_TOLERANCE && width <= slot.GetLocalSize().X + SOLID_SMOKE_FIT_TOLERANCE
            && cell->GetWrapWidth() == wrap && height > 0.0f
            && height <= row.GetLocalSize().Y + SOLID_SMOKE_FIT_TOLERANCE && slotRight <= buttonLeft + tolPx
            && buttonLeft >= rowLeft - tolPx && buttonRight <= rowRight + tolPx;
        if (!bFits)
        {
            bOk = false;
            detail += FString::Printf(TEXT(" [\"%s\": caps %.1f x %.1f (wrap %.1f), slot %.1f ending %.1f, Rebind "
                "%.1f-%.1f in row %.1f-%.1f, %.1f tall, in view %d painted %d]"), *cell->GetText(), width, height,
                cell->GetWrapWidth(), slot.GetLocalSize().X, slotRight, buttonLeft, buttonRight, rowLeft, rowRight,
                row.GetLocalSize().Y, bInView ? 1 : 0, bPainted ? 1 : 0);
        }
    }
    bOk &= rows == SOLKeyBindings::BINDING_COUNT && painted > 0;
    Check(id, bOk, FString::Printf(TEXT("tab %d, row %.1f, cell wrap %.1f (want %.1f), %d key rows (want %d, %d in view "
        "painted whole), widest caps \"%s\" %.1f, %d wrapped%s"), static_cast<int32>(page->GetSelectedTab()),
        page->GetFitRowWidth(), page->GetKeyCellWidth(), wrap, rows, SOLKeyBindings::BINDING_COUNT, painted, *widestName,
        widest, wrapped, *detail));
}

//////////////////////////////////////////////////////////////////////////
// (33c) A keyboard diagram step's stages: 0 the Controls page settles in the shell; 1 its content width read and the
// test page laid out in it; 2 the fit, paint and assignment checks, a shot; 3 / 4 each hover key under the real
// cursor (retried when the hover did not take), checked, two of them shot; then on the wide step 5 / 6 / 7 Fire
// rebound to K (checked, shot) and back (checked); 8 the page removed
bool FSOLSolidMenuSmoke::TickDiagram(USOLMenuSubsystem& menu, const TCHAR* id, const TCHAR* shotName)
{
    using namespace SOL::Help;
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLKeyboardDiagramSmokePage* page = mDiagramPage.Get();
    USOLKeyboardDiagram* diagram = page != nullptr ? page->GetDiagram() : nullptr;
    const double stageS = mStepTimeS - mSubStageS;
    const int32 stageFrames = mStepFrames - mSubStageFrames;

    // Moves to a stage; bAfterShot: its first action waits a shot's frames
    auto nextStage = [this](const int32 stage, const bool bAfterShot)
    {
        mSubStage = stage;
        mSubStageS = mStepTimeS;
        mSubStageFrames = bAfterShot ? mStepFrames : mStepFrames - SOLID_SMOKE_SHOT_FRAMES;
    };
    switch (mSubStage)
    {
    case 0:
        if (stageS >= SOLID_SMOKE_SETTLE_S)
        {
            nextStage(1, false);
        }
        return false;
    case 1:
    {
        // The Help tab's content width as the read-only pages lay their rows out: the tab scroll box less a scroll
        // bar's room and the gap (USOLReadOnlyPageWidget::FitLeftColumn)
        const USOLReadOnlyPageWidget* controls = mainMenu != nullptr ? mainMenu->GetControlsPage() : nullptr;
        const UScrollBox* scroll = controls != nullptr ? controls->GetTabScroll(controls->GetSelectedTab()) : nullptr;
        mDiagramWidth = scroll != nullptr ? static_cast<float>(scroll->GetCachedGeometry().GetLocalSize().X)
            - SOL::SolidMenu::SCROLL_BAR_THICKNESS - SOL::MenuStyle::BUTTON_GAP : 0.0f;
        page = mController.IsValid() ? CreateWidget<USOLKeyboardDiagramSmokePage>(mController.Get(),
            USOLKeyboardDiagramSmokePage::StaticClass()) : nullptr;
        if (page != nullptr)
        {
            page->SetContentWidth(mDiagramWidth);
            page->AddToViewport(SOLID_SMOKE_BOARD_Z);
            mDiagramPage = page;
        }
        nextStage(2, false);
        return false;
    }
    case 2:
    {
        if (stageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }

        // Fit: the panel in the content width, every key rect inside it and the diagram box, the arrangement as
        // FitToWidth decides (D34: stacked in the 658-unit shell content of every D37 target, the unit width / 15), the
        // hover strip sized for the busiest key (D36), the leaf as wide as the content and in the view
        const FSOLDiagramPanel panel = diagram != nullptr ? diagram->GetPanel() : FSOLDiagramPanel();
        const FSOLDiagramScale fit = SOLKeyboardLayout::FitToWidth(mDiagramWidth);
        const float tolerance = SOLID_SMOKE_DIAGRAM_TOLERANCE;
        int32 outside = 0;
        for (int32 index = 0; index < SOLKeyboardLayout::KeyCount(); ++index)
        {
            const FBox2f units = SOLKeyboardLayout::KeyRect(index, panel.Scale.Placement);
            const FVector2f min = SOLKeyboardLayout::FromUnits(units.Min, panel.Scale);
            const FVector2f max = SOLKeyboardLayout::FromUnits(units.Max, panel.Scale);
            outside += min.X < -tolerance || max.X > mDiagramWidth + tolerance || min.Y < panel.Diagram.Min.Y
                - tolerance || max.Y > panel.Diagram.Max.Y + tolerance ? 1 : 0;
        }
        const float leafWidth = diagram != nullptr ? static_cast<float>(diagram->GetCachedGeometry().GetLocalSize().X)
            : 0.0f;
        FString insideDetail;
        const bool bInView = diagram != nullptr && SolidSmokeInside(diagram, page, insideDetail);
        const float stackedUnit = mDiagramWidth / SOLKeyboardLayout::Extent(ESOLMousePlacement::Stacked).X;
        const int32 budget = diagram != nullptr ? diagram->GetDetailLineBudget() : 0;
        const float stripHeight = DIAGRAM_DETAIL_CAP_HEIGHT + panel.DetailLines * DIAGRAM_DETAIL_LINE_HEIGHT;
        Check(*FString::Printf(TEXT("%s-fit"), id), diagram != nullptr && mDiagramWidth > 0.0f
            && panel.Diagram.Max.X <= mDiagramWidth + tolerance && panel.Detail.Max.X <= mDiagramWidth + tolerance
            && outside == 0 && panel.Scale.Placement == fit.Placement
            && panel.Scale.Placement == ESOLMousePlacement::Stacked
            && FMath::IsNearlyEqual(panel.Scale.UnitPx, FMath::Min(stackedUnit, DIAGRAM_MAX_UNIT), KINDA_SMALL_NUMBER)
            && panel.Scale.bShortLabels == (panel.Scale.UnitPx >= DIAGRAM_MIN_UNIT)
            && budget >= 1 && budget <= DIAGRAM_DETAIL_LINES && panel.DetailLines == budget
            && FMath::IsNearlyEqual(panel.Detail.GetSize().Y, stripHeight, tolerance)
            && FMath::IsNearlyEqual(leafWidth, mDiagramWidth, tolerance) && bInView,
            FString::Printf(TEXT("content %.1f, unit %.2f (stacked %.2f, min %.0f, mouse below under %.0f, stack under "
            "%.0f), arrangement %d (want stacked %d), diagram %.1fx%.1f, strip %d lines (budget %d, cap %d) %.1f tall, "
            "panel height %.1f, legend %dx%d, labels %d, %d keys outside, leaf %.1f, in view %s"), mDiagramWidth,
            panel.Scale.UnitPx, stackedUnit, DIAGRAM_MIN_UNIT, DIAGRAM_MOUSE_BELOW_UNIT, DIAGRAM_STACK_UNIT,
            static_cast<int32>(panel.Scale.Placement), static_cast<int32>(ESOLMousePlacement::Stacked),
            panel.Diagram.GetSize().X, panel.Diagram.GetSize().Y, panel.DetailLines, budget, DIAGRAM_DETAIL_LINES,
            panel.Detail.GetSize().Y, panel.Height, panel.LegendColumns, panel.LegendRows,
            panel.Scale.bShortLabels ? 1 : 0, outside, leafWidth, *insideDetail));
        mDiagramPanel = panel;

        // Painted whole with every layout key, the keys in their own LED-less Small hull material (D38: not the
        // subsystem's shared SmallButton one, LedX < 0)
        const int32 painted = diagram != nullptr ? diagram->GetPaintedKeyCount() : 0;
        const bool bWhole = diagram != nullptr && diagram->WasPaintedWhole();
        const UMaterialInstanceDynamic* keyMaterial = diagram != nullptr ? diagram->GetKeyMaterial() : nullptr;
        const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(menu.GetWorld());
        const UMaterialInstanceDynamic* sharedSmall = solid != nullptr
            ? solid->GetStyleMaterial(ESOLSolidStyle::SmallButton) : nullptr;
        float keyLedX = 0.0f;
        const bool bKeyLedRead = keyMaterial != nullptr
            && keyMaterial->GetScalarParameterValue(SOL::SolidMenuMaterialParams::LED_X, keyLedX);
        const bool bKeyMaterial = keyMaterial != nullptr && keyMaterial != sharedSmall && bKeyLedRead
            && keyLedX < 0.0f;
        Check(*FString::Printf(TEXT("%s-painted"), id), bWhole && painted == SOLKeyboardLayout::KeyCount()
            && bKeyMaterial, FString::Printf(TEXT("whole %d, %d keys painted (want %d), key material %d (shared "
            "SmallButton %d, LedX read %d %.2f, want < 0)"), bWhole ? 1 : 0, painted, SOLKeyboardLayout::KeyCount(),
            keyMaterial != nullptr ? 1 : 0, keyMaterial != nullptr && keyMaterial == sharedSmall ? 1 : 0,
            bKeyLedRead ? 1 : 0, keyLedX));

        // The assignment count at the default keys (the smoke slot starts from them) equals SOLKeyDiagram::Build's
        const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
        FSOLKeyDiagramModel model;
        SOLKeyDiagram::Build(defaults, model);
        const bool bDefaultKeys = SOLMenuContent::ResolveLiveKeys(menu.GetWorld()) == defaults;
        const int32 count = diagram != nullptr ? diagram->GetAssignmentCount() : -1;
        Check(*FString::Printf(TEXT("%s-assignments"), id), bDefaultKeys && count == model.Assignments.Num(),
            FString::Printf(TEXT("%d assignments (Build: %d), live keys the defaults %d, %d other, %d unbound"), count,
            model.Assignments.Num(), bDefaultKeys ? 1 : 0, diagram != nullptr ? diagram->GetModel().Overflow.Num() : -1,
            diagram != nullptr ? diagram->GetModel().Unbound.Num() : -1));

        // D35 / D38: no legend drawn wider than its key and none clipped (a legend that does not fit uses its
        // abbreviation, every abbreviated key has an entry); the main Enter keeps its legend, the numpad Enter mirror
        // is abbreviated; the abbreviated legends and the "+N" badges hidden on overlap named in the detail
        const int32 wide = diagram != nullptr ? diagram->GetWideLegendCount() : -1;
        const int32 hiddenBadges = diagram != nullptr ? diagram->GetPaintedHiddenBadges() : -1;
        int32 clipped = 0;
        int32 noEntry = 0;
        int32 mainEnter = -1;                           // -1 not found, 0 found and kept, 1 found and abbreviated
        int32 mirrorEnter = -1;
        FString clippedKeys;
        FString abbreviatedKeys;
        FString hiddenBadgeKeys;
        const TConstArrayView<FSOLLayoutKey> layoutKeys = SOLKeyboardLayout::Keys();
        for (int32 index = 0; diagram != nullptr && index < layoutKeys.Num(); ++index)
        {
            const FSOLLayoutKey& layoutKey = layoutKeys[index];
            const FString name = FString::Printf(TEXT("%s%s"), layoutKey.KeyName,
                layoutKey.bMirror ? TEXT("(mirror)") : TEXT(""));
            if (diagram->IsLegendClipped(index))
            {
                ++clipped;
                clippedKeys += (clippedKeys.IsEmpty() ? TEXT("") : TEXT(" ")) + name;
            }
            const bool bAbbreviated = diagram->IsLegendAbbreviated(index);
            abbreviatedKeys += bAbbreviated ? (abbreviatedKeys.IsEmpty() ? TEXT("") : TEXT(" ")) + name : FString();
            noEntry += bAbbreviated && SOLKeyDiagram::LegendAbbreviation(layoutKey.KeyName) == nullptr ? 1 : 0;
            if (FCString::Strcmp(layoutKey.KeyName, TEXT("Enter")) == 0)
            {
                int32& enter = layoutKey.bMirror ? mirrorEnter : mainEnter;
                enter = bAbbreviated ? 1 : 0;
            }
            hiddenBadgeKeys += diagram->IsBadgeHidden(index) ? (hiddenBadgeKeys.IsEmpty() ? TEXT("") : TEXT(" "))
                + name : FString();
        }
        Check(*FString::Printf(TEXT("%s-legends"), id), diagram != nullptr && wide == 0 && clipped == 0
            && hiddenBadges >= 0 && mainEnter == 0 && mirrorEnter == 1 && noEntry == 0,
            FString::Printf(TEXT("%d legends wider than their key, %d clipped [%s], abbreviated [%s] (%d without an "
            "entry, main Enter %d want 0, numpad Enter mirror %d want 1; -1 not found), %d \"+N\" badges hidden "
            "(overlap) [%s] at unit %.2f"), wide, clipped, *clippedKeys, *abbreviatedKeys, noEntry, mainEnter,
            mirrorEnter, hiddenBadges, *hiddenBadgeKeys, panel.Scale.UnitPx));
        TakeUiShot(shotName);
        nextStage(3, true);
        return false;
    }
    case 3:
    {
        // A shot's frames first (when one was taken), then the real cursor to the hover key's centre
        if (stageFrames < SOLID_SMOKE_SHOT_FRAMES)
        {
            return false;
        }
        const int32 key = SOLKeyboardLayout::FindKey(SOLID_SMOKE_DIAGRAM_HOVERS[mDiagramHover].KeyName);
        if (diagram != nullptr && key != INDEX_NONE)
        {
            const FSOLDiagramPanel& panel = diagram->GetPanel();
            const FGeometry& geometry = diagram->GetCachedGeometry();
            const FVector2f centre = SOLKeyboardLayout::FromUnits(SOLKeyboardLayout::KeyRect(key,
                panel.Scale.Placement).GetCenter(), panel.Scale);
            SolidSmokeMouseMove(FVector2D(geometry.LocalToAbsolute(FVector2D(centre))), false);
        }
        nextStage(4, false);
        return false;
    }
    case 4:
    {
        // Past the hover ramp: the hover read (retried from the front when a covering window dropped it), checked
        if (stageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        const int32 key = SOLKeyboardLayout::FindKey(SOLID_SMOKE_DIAGRAM_HOVERS[mDiagramHover].KeyName);
        if (diagram != nullptr && diagram->GetHoveredKey() != key && mCursorRetries < SOLID_SMOKE_CURSOR_RETRIES)
        {
            ++mCursorRetries;
            SolidSmokeBringGameToFront();
            nextStage(3, false);
            return false;
        }
        CheckDiagramHover(id, mDiagramHover, key);
        const bool bShot = (SOLID_SMOKE_DIAGRAM_SHOTS & (1u << mDiagramHover)) != 0;
        if (bShot)
        {
            TakeUiShot(*FString::Printf(TEXT("%s-hover-%s"), shotName, SOLID_SMOKE_DIAGRAM_HOVERS[mDiagramHover].Name));
        }
        nextStage(++mDiagramHover < SOLID_SMOKE_DIAGRAM_HOVER_COUNT ? 3 : 5, bShot);
        return false;
    }
    case 5:
    {
        // A shot's frames, then Fire rebound to K through the settings subsystem and applied (only the smoke slot is
        // written; nothing else pending reaches the engine's GameUserSettings): the owner's handler rebuilds
        if (stageFrames < SOLID_SMOKE_SHOT_FRAMES)
        {
            return false;
        }
        ParkCursor();
        USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
        mDiagramChanges = page != nullptr ? page->GetBindingsChangedCount() : 0;
        if (settings != nullptr)
        {
            int32 conflict = INDEX_NONE;
            settings->RevertPending();
            settings->RebindKey(static_cast<int32>(SOLKeyBindings::EBinding::Fire), SOLID_SMOKE_DIAGRAM_REBIND_KEY,
                conflict);
            settings->ApplyPending();
        }
        nextStage(6, false);
        return false;
    }
    case 6:
        if (stageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        CheckDiagramRebind(id, false);
        TakeUiShot(*FString::Printf(TEXT("%s-rebind"), shotName));
        nextStage(7, true);
        return false;
    case 7:
    {
        // A shot's frames, then Fire back on its default key, applied
        if (stageFrames < SOLID_SMOKE_SHOT_FRAMES)
        {
            return false;
        }
        USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
        if (settings != nullptr)
        {
            const int32 fire = static_cast<int32>(SOLKeyBindings::EBinding::Fire);
            int32 conflict = INDEX_NONE;
            settings->RevertPending();
            settings->RebindKey(fire, SOLKeyBindings::GetDef(fire).DefaultKey, conflict);
            settings->ApplyPending();
        }
        nextStage(8, false);
        return false;
    }
    case 8:
        // A settle after the restore, then its check
        if (stageS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        CheckDiagramRebind(id, true);
        if (diagram != nullptr)
        {
            // Every layout key hovered in turn (not only the real-cursor ones): the busiest key's strip rows equal the
            // strip's line budget (D36: sized once for it) and no key needs more; the panel never changes while
            // hovering or clearing the hover. The panel after the restore is the reference (same keys as the fit)
            ParkCursor();
            const FSOLDiagramPanel reference = diagram->GetPanel();
            int32 changed = 0;
            int32 maxRows = 0;
            int32 maxKey = INDEX_NONE;
            const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
            for (int32 index = 0; index < layout.Num(); ++index)
            {
                if (layout[index].bMirror)
                {
                    continue;
                }
                diagram->SetHoveredKey(index);
                changed += SolidSmokeSamePanel(diagram->GetPanel(), reference) ? 0 : 1;
                const int32 rows = diagram->GetDetailLines().IsEmpty() ? 0 : diagram->GetDetailRowCount();
                if (rows > maxRows)
                {
                    maxRows = rows;
                    maxKey = index;
                }
            }
            diagram->SetHoveredKey(INDEX_NONE);
            changed += SolidSmokeSamePanel(diagram->GetPanel(), reference) ? 0 : 1;
            const bool bSameAsFit = SolidSmokeSamePanel(reference, mDiagramPanel);
            Check(*FString::Printf(TEXT("%s-strip-all"), id), maxRows > 0 && maxRows <= DIAGRAM_DETAIL_LINES
                && maxRows == reference.DetailLines && changed == 0 && bSameAsFit,
                FString::Printf(TEXT("busiest key %s: %d rows (strip %d lines, cap %d) in width %.1f, panel changed on "
                "%d hovers, same as at the fit %d"), layout.IsValidIndex(maxKey) ? layout[maxKey].KeyName
                : TEXT("none"), maxRows, reference.DetailLines, DIAGRAM_DETAIL_LINES, mDiagramWidth, changed,
                bSameAsFit ? 1 : 0));
        }
        else
        {
            Check(*FString::Printf(TEXT("%s-strip-all"), id), false, TEXT("no diagram"));
        }
        nextStage(9, false);
        return false;
    default:
        // The page removed, the cursor parked
        if (page != nullptr)
        {
            page->RemoveFromParent();
        }
        mDiagramPage.Reset();
        ParkCursor();
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// (33c) Checks one hovered diagram key: hovered, its strip filled with its cap and fitting, painted whole; a modifier
// key also its mark and its lines (its own actions, the primary first, then its chords in order; Right Shift lists
// RShift+O, Left Shift does not; the right-side keys have no action of their own)
void FSOLSolidMenuSmoke::CheckDiagramHover(const TCHAR* id, const int32 hoverIndex, const int32 key)
{
    const FSolidSmokeDiagramHover& hover = SOLID_SMOKE_DIAGRAM_HOVERS[hoverIndex];
    const FString label = FString::Printf(TEXT("%s-hover-%s"), id, hover.Name);
    const USOLKeyboardDiagramSmokePage* page = mDiagramPage.Get();
    const USOLKeyboardDiagram* diagram = page != nullptr ? page->GetDiagram() : nullptr;
    if (diagram == nullptr || !diagram->GetModel().Keys.IsValidIndex(key))
    {
        Check(*label, false, FString::Printf(TEXT("no diagram or key %s not on the layout"), hover.KeyName));
        return;
    }
    const FSOLKeyDiagramModel& model = diagram->GetModel();
    const FSOLDiagramKey& diagramKey = model.Keys[key];
    const TArray<FSOLDiagramDetailLine>& lines = diagram->GetDetailLines();
    const int32 rows = diagram->GetDetailRowCount();
    const FSOLDiagramPanel& panel = diagram->GetPanel();
    const bool bPanelFixed = SolidSmokeSamePanel(panel, mDiagramPanel);

    // D36: the hovered key's cap on its own row above the lines (its row fits a key cap, the lines start under it) and
    // no wider than the strip; the rows within the strip's fixed line budget
    const bool bCapRow = diagram->GetDetailCapWidth() > 0.0f && diagram->GetDetailCapWidth() <= panel.Detail.GetSize().X
        && SOL::Help::KEYCAP_HEIGHT <= SOL::Help::DIAGRAM_DETAIL_CAP_HEIGHT;
    bool bOk = diagram->GetHoveredKey() == key && !lines.IsEmpty() && rows <= panel.DetailLines
        && diagram->WasPaintedWhole() && bCapRow && bPanelFixed;

    // The lines as shown, for the log (and for settling DIAGRAM_DETAIL_LINES)
    FString shown;
    for (const FSOLDiagramDetailLine& line : lines)
    {
        shown += FString::Printf(TEXT("%s[%s = %s]"), shown.IsEmpty() ? TEXT("") : TEXT(" "), *line.Keys,
            *line.Action);
    }
    FString modifierDetail;
    if (hover.bModifier)
    {
        // The expected lines, independently of the leaf: the own assignments (any order, the primary first), then
        // the key's chords in their model order
        const TArray<FString> liveKeys = SOLMenuContent::ResolveLiveKeys(mController.IsValid()
            ? mController->GetWorld() : nullptr);
        const int32 own = diagramKey.AssignmentCount;
        bool bLines = lines.Num() == own + diagramKey.ChordCount;
        FSOLDiagramDetailLine expected;
        for (int32 index = 0; bLines && index < lines.Num(); ++index)
        {
            bool bFound = false;
            if (index == 0 && own > 0)
            {
                SOLKeyDiagram::DetailLine(model, diagramKey.Primary, liveKeys, expected);
                bFound = expected.Keys == lines[index].Keys && expected.Action == lines[index].Action;
            }
            else if (index < own)
            {
                for (int32 a = 0; a < own && !bFound; ++a)
                {
                    SOLKeyDiagram::DetailLine(model, diagramKey.FirstAssignment + a, liveKeys, expected);
                    bFound = expected.Keys == lines[index].Keys && expected.Action == lines[index].Action;
                }
            }
            else if (model.Chords.IsValidIndex(diagramKey.FirstChord + index - own))
            {
                SOLKeyDiagram::DetailLine(model, model.Chords[diagramKey.FirstChord + index - own], liveKeys, expected);
                bFound = expected.Keys == lines[index].Keys && expected.Action == lines[index].Action;
            }
            bLines = bFound;
        }

        // RShift+O (CycleOverlayProfile) listed on Right Shift only
        bool bRShiftO = false;
        for (int32 a = 0; a < model.Assignments.Num(); ++a)
        {
            if (model.Assignments[a].Binding == static_cast<int32>(SOLKeyBindings::EBinding::CycleOverlayProfile))
            {
                SOLKeyDiagram::DetailLine(model, a, liveKeys, expected);
                for (const FSOLDiagramDetailLine& line : lines)
                {
                    bRShiftO |= line.Keys == expected.Keys && line.Action == expected.Action;
                }
                break;
            }
        }
        const bool bIsRightShift = FCString::Stricmp(hover.KeyName, TEXT("RightShift")) == 0;
        const bool bIsLeftShift = FCString::Stricmp(hover.KeyName, TEXT("LeftShift")) == 0;
        const bool bRShiftOk = bIsRightShift ? bRShiftO : !bIsLeftShift || !bRShiftO;
        const bool bSide = hover.bRightSide ? own == 0 && diagramKey.Primary == INDEX_NONE : own > 0;
        bOk = bOk && diagramKey.bModifier && diagramKey.ChordCount > 0 && bLines && bSide && bRShiftOk;
        modifierDetail = FString::Printf(TEXT(", modifier %d, own %d, chords %d, lines own-then-chords %d, "
            "RShift+O %d"), diagramKey.bModifier ? 1 : 0, own, diagramKey.ChordCount, bLines ? 1 : 0, bRShiftO ? 1 : 0);
    }
    Check(*label, bOk, FString::Printf(TEXT("hovered %d (want %d), %d lines in %d rows (strip %d), cap %.1f on its row "
        "%d, panel fixed %d, whole %d%s: %s"), diagram->GetHoveredKey(), key, lines.Num(), rows, panel.DetailLines,
        diagram->GetDetailCapWidth(), bCapRow ? 1 : 0, bPanelFixed ? 1 : 0, diagram->WasPaintedWhole() ? 1 : 0,
        *modifierDetail, *shown));
}

//////////////////////////////////////////////////////////////////////////
// (33c) Checks the diagram after the rebind (Fire on K, tinting it, not on LMB; the owner's handler ran) or after the
// restore (Fire back on LMB, K free, the default assignment count)
void FSOLSolidMenuSmoke::CheckDiagramRebind(const TCHAR* id, const bool bRestored)
{
    const USOLKeyboardDiagramSmokePage* page = mDiagramPage.Get();
    const USOLKeyboardDiagram* diagram = page != nullptr ? page->GetDiagram() : nullptr;
    const FString label = FString::Printf(TEXT("%s-%s"), id, bRestored ? TEXT("restore") : TEXT("rebind"));
    const int32 kKey = SOLKeyboardLayout::FindKey(SOLID_SMOKE_DIAGRAM_REBIND_KEY);
    const int32 lmbKey = SOLKeyboardLayout::FindKey(TEXT("LeftMouseButton"));
    if (diagram == nullptr || !diagram->GetModel().Keys.IsValidIndex(kKey)
        || !diagram->GetModel().Keys.IsValidIndex(lmbKey))
    {
        Check(*label, false, TEXT("no diagram, K or LMB"));
        return;
    }
    const FSOLKeyDiagramModel& model = diagram->GetModel();
    const int32 fire = static_cast<int32>(SOLKeyBindings::EBinding::Fire);

    // Whether a key holds Fire among its own assignments
    auto holdsFire = [&model, fire](const FSOLDiagramKey& key)
    {
        for (int32 a = key.FirstAssignment; a < key.FirstAssignment + key.AssignmentCount; ++a)
        {
            if (model.Assignments.IsValidIndex(a) && model.Assignments[a].Binding == fire)
            {
                return true;
            }
        }
        return false;
    };
    const FSOLDiagramKey& k = model.Keys[kKey];
    const bool bFireOnK = holdsFire(k);
    const bool bFireOnLmb = holdsFire(model.Keys[lmbKey]);
    const bool bKTinted = model.Assignments.IsValidIndex(k.Primary) && model.Assignments[k.Primary].Binding == fire
        && k.Area == ESOLControlArea::Combat;
    const int32 changes = page->GetBindingsChangedCount() - mDiagramChanges;
    FSOLKeyDiagramModel defaults;
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), defaults);
    const bool bOk = bRestored
        ? !bFireOnK && k.Primary == INDEX_NONE && bFireOnLmb && SOLKeyDiagram::KeyOfBinding(model, fire) == lmbKey
            && diagram->GetAssignmentCount() == defaults.Assignments.Num() && changes >= 2
        : bFireOnK && bKTinted && !bFireOnLmb && SOLKeyDiagram::KeyOfBinding(model, fire) == kKey && changes >= 1;
    Check(*label, bOk && diagram->WasPaintedWhole(), FString::Printf(TEXT("Fire on K %d (tinted %d, area %s), on LMB "
        "%d, Fire's key %d (K %d, LMB %d), %d assignments (defaults %d), %d bindings changed events, whole %d"),
        bFireOnK ? 1 : 0, bKTinted ? 1 : 0, SOLKeyDiagram::AreaName(k.Area), bFireOnLmb ? 1 : 0,
        SOLKeyDiagram::KeyOfBinding(model, fire), kKey, lmbKey, diagram->GetAssignmentCount(),
        defaults.Assignments.Num(), changes, diagram->WasPaintedWhole() ? 1 : 0));
}
