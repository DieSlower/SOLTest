/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuSmoke.h"

#include "Menu/SOLBuildVersion.h"
#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLDemoPageWidget.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLPageTabs.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Menu/SOLSettingsTabs.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSmokeBoard.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidSlider.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLOverlayMenuWidget.h"

#include "Blueprint/UserWidget.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "UnrealClient.h"

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
        InfoPage,       // 46f: an Info page on the viewport (the main menu has none): tabs, fit, key texts
        InfoShot,       // ... settled on its last tab (the prose) for the screenshot
        ViewSmall,      // r.SetRes 1000x800 (windowed)
        SmallSettings,  // Settings open at 1000x800: the column and the shell inside the view, every stepper value
                        // inside its slot (46e follow-up; the longest window mode shown for the screenshot)
        SmallStart,     // Start location open at 1000x800
        SmallControls,  // 46f: Controls open at 1000x800: tabs, the rows and columns inside the shell
        SmallDemo,      // 46f: Demo open at 1000x800: the groups, labels and values inside their slots
        Quit,           // SUMMARY, quit
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

    constexpr int32 SOLID_SMOKE_MIN_FRAMES = 5;                 // Every step lasts at least this many frames
    constexpr int32 SOLID_SMOKE_SHOT_FRAMES = 10;               // A screenshot's frames before the next action
    constexpr int32 SOLID_SMOKE_INSTANT_FRAME = 2;              // Strength 0: the look is read on this frame
    constexpr int32 SOLID_SMOKE_VIEW_STABLE_FRAMES = 10;
    constexpr int32 SOLID_SMOKE_CURSOR_INSET_PX = 8;            // The parked cursor, this far in from the top right
    constexpr int32 SOLID_SMOKE_BOARD_Z = SOL::MenuStyle::Z_ORDER + 10;
    constexpr double SOLID_SMOKE_SETUP_S = 3.0;                 // The main menu's first frames (and shader loads)
    constexpr double SOLID_SMOKE_SETTLE_S = 0.8;                // Longer than every hover ramp and press linger
    constexpr double SOLID_SMOKE_VIEW_TIMEOUT_S = 8.0;
    constexpr double SOLID_SMOKE_WATCHDOG_S = 180.0;
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
    mSamples = 0;
    mFirstProgress = mFirstPower = mLastProgress = -1.0f;
    mIsMonotonic = true;
    mSawMidProgress = mSawMidPower = mSawHitTestInvisible = false;
    mSeenLeds = 0;
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
    case ESolidSmokeStep::SmallStart:
        ClickEntry(menu, SOLID_SMOKE_DOWN_ENTRY);
        break;
    case ESolidSmokeStep::SmallControls:
        ClickEntry(menu, SOLID_SMOKE_CONTROLS_ENTRY);
        break;
    case ESolidSmokeStep::SmallDemo:
        ClickEntry(menu, SOLID_SMOKE_HOVER_ENTRY);
        break;
    case ESolidSmokeStep::InfoPage:
    {
        // The main menu has no Info page, so one is built and shown full view over it for its checks
        USOLReadOnlyPageWidget* info = CreateWidget<USOLReadOnlyPageWidget>(mController.Get(),
            USOLReadOnlyPageWidget::StaticClass());
        if (info != nullptr)
        {
            info->Setup(ESOLReadOnlyPage::Info);
            info->AddToViewport(SOLID_SMOKE_BOARD_Z);
            mInfoPage = info;
        }
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
    {
        const FIntPoint want = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::View ? SOLID_SMOKE_VIEW
            : SOLID_SMOKE_SMALL_VIEW;
        mViewFrames = GetViewSize() == want ? mViewFrames + 1 : 0;
        return mViewFrames >= SOLID_SMOKE_VIEW_STABLE_FRAMES || mStepTimeS >= SOLID_SMOKE_VIEW_TIMEOUT_S;
    }
    case ESolidSmokeStep::MainOpen:
        if (mainMenu != nullptr)
        {
            SampleAnim(mainMenu->GetColumnProgress(), mainMenu->GetColumnTracePower(), false);
            const TSharedPtr<SSOLSolidTraceLayer> layer = SolidSmokeColumnLayer(mainMenu);
            mSeenLeds |= layer.IsValid() ? layer->GetLitLeds() : 0u;
        }
        return mStepTimeS >= SOLID_SMOKE_SETTLE_S;
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
        if (bHover)
        {
            TakeUiShot(TEXT("main-hover"));
        }
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
            CheckReadOnlyPage(TEXT("p-controls"), mainMenu->GetControlsPage(), false);
        }
        else if (page.Page == ESOLMainMenuPage::About && mainMenu != nullptr)
        {
            CheckReadOnlyPage(TEXT("p-about"), mainMenu->GetAboutPage(), false);
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
        break;
    }
    case ESolidSmokeStep::PlayReturn:
    {
        Check(TEXT("t-return-shell-closed"), mIsReturnClosed, mReturnDetail);

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
        Check(TEXT("f-s0-instant"), mInstantTimeS >= 0.0 && mInstantTimeS < SOL::SolidMenu::HOVER_RAMP_S
            && FMath::IsNearlyZero(s) && SolidSmokeLookMatches(mInstantLook, SolidSmokeSettled(
            ESOLSolidButtonState::Hovered, 0.0f, false)), FString::Printf(TEXT("after %.3f s at strength %.2f: %s"),
            mInstantTimeS, s, *SolidSmokeDescribe(mInstantLook)));
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
    case ESolidSmokeStep::InfoPage:
    {
        CheckReadOnlyPage(TEXT("i-info"), mInfoPage.Get(), true);
        break;
    }
    case ESolidSmokeStep::InfoShot:
        TakeUiShot(TEXT("info"));
        break;
    case ESolidSmokeStep::SmallControls:
    case ESolidSmokeStep::SmallDemo:
    {
        const bool bControls = static_cast<ESolidSmokeStep>(step) == ESolidSmokeStep::SmallControls;
        if (mInfoPage.IsValid())
        {
            mInfoPage->RemoveFromParent();
        }
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
            CheckReadOnlyPage(TEXT("h-controls"), mainMenu != nullptr ? mainMenu->GetControlsPage() : nullptr, true);
        }
        else
        {
            CheckDemoLayout(TEXT("h-demo"), menu);
        }
        TakeUiShot(bControls ? TEXT("small-controls") : TEXT("small-demo"));
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
            CheckSettingsFit(menu, TEXT("h-small-stepper-values"), TEXT("h-small-labels"));
        }
        TakeUiShot(bSettings ? TEXT("small-settings") : TEXT("small-start"));
        if (USOLSettingsPageWidget* page = bSettings && mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr)
        {
            page->Revert();
        }
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
    if (mInfoPage.IsValid())
    {
        mInfoPage->RemoveFromParent();
    }
    if (USOLSolidMenuSmokeBoard* board = mBoard.Get())
    {
        board->RemoveFromParent();
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
        CheckSettingsFit(menu, TEXT("u-stepper-values"), TEXT("u-labels"));
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
        // The drag: the real cursor onto the knob's place at SOLID_SMOKE_DRAG_FROM, pressed
        const FVector2D from = SolidSmokeSliderPoint(master, SOLID_SMOKE_DRAG_FROM);
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
        mIsDragOk = bFill && bCaptured && FMath::IsNearlyEqual(value, SOLID_SMOKE_DRAG_TO, SOLID_SMOKE_DRAG_TOLERANCE);
        mDragDetail = FString::Printf(TEXT("dragged %.2f -> %.2f: captured %d, value %.3f, %s"), SOLID_SMOKE_DRAG_FROM,
            SOLID_SMOKE_DRAG_TO, bCaptured ? 1 : 0, value, *detail);
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
    switch (mSubStage)
    {
    case 0:
        // The Audio tab settled with the focus (lit), then the real cursor onto the slider
        if (mStepTimeS < SOLID_SMOKE_SETTLE_S)
        {
            return false;
        }
        mIsSliderHoverOk &= read(TEXT("audio focused"), ESOLSettingsTab::Audio, true, false);
        SolidSmokeMouseMove(SolidSmokeSliderPoint(master, 0.5f), false);
        nextStage();
        return false;
    case 1:
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
void FSOLSolidMenuSmoke::CheckReadOnlyPage(const TCHAR* id, USOLReadOnlyPageWidget* page, const bool bMeasureFit)
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

    // Every left-column text: its longest word inside its column (measured at the page's layout scale), and the shown
    // tab's texts inside the content horizontally
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
        TArray<FString> words;
        column.Text->GetText().ToString().ParseIntoArray(words, TEXT(" "));
        for (const FString& word : words)
        {
            const float width = SolidSmokeTextWidth(word, column.Text->GetFont(), scale);
            if (!(width >= 0.0f && width <= column.Width + SOLID_SMOKE_FIT_TOLERANCE))
            {
                bFits = false;
                detail += FString::Printf(TEXT(" [\"%s\" %.1f over %.1f]"), *word, width, column.Width);
            }
        }
        if (column.Tab == page->GetSelectedTab())
        {
            ++shown;
            const FGeometry& geometry = column.Text->GetCachedGeometry();
            const double left = geometry.GetAbsolutePosition().X;
            const double right = left + geometry.GetAbsoluteSize().X;
            if (!(geometry.GetAbsoluteSize().X > 0.0 && left >= scrollMin - SOLID_SMOKE_FIT_TOLERANCE_PX
                && right <= scrollMax + SOLID_SMOKE_FIT_TOLERANCE_PX))
            {
                bFits = false;
                detail += FString::Printf(TEXT(" [shown text \"%s\" %.0f-%.0f outside %.0f-%.0f]"),
                    *column.Text->GetText().ToString().Left(24), left, right, scrollMin, scrollMax);
            }
        }
    }
    if (!bMeasureFit)
    {
        // A page opened from closed slides in: its children's cached geometry lags the slide, so the layout is not
        // read here (the same page is read at 1000x800 with the shell already open)
        bFits = true;
        shown = 1;
    }
    Check(*FString::Printf(TEXT("%s-fit"), id), bFits && (page->GetColumnTexts().Num() == 0 || shown > 0),
        FString::Printf(TEXT("%d column texts (%d on the shown tab) at scale %.3f%s"), page->GetColumnTexts().Num(),
        shown, scale, *detail));

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
// value slot as laid out, and that no row label outside Key bindings runs past its column and gap
void FSOLSolidMenuSmoke::CheckSettingsFit(USOLMenuSubsystem& menu, const TCHAR* valuesLabel, const TCHAR* labelsLabel)
{
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    const USOLSettingsPageWidget* page = mainMenu != nullptr ? mainMenu->GetSettingsPage() : nullptr;
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

    // Row labels: each ends before its row's control (the column plus the gap); Key bindings rows are known tech debt
    // (logged, not checked)
    bool bLabelsFit = true;
    FString labelDetail;
    float worstMargin = TNumericLimits<float>::Max();
    FString worst;
    for (int32 index = 0; index < page->GetRowLabelCount(); ++index)
    {
        ESOLSettingsTab tab = ESOLSettingsTab::Count;
        const UTextBlock* label = page->GetRowLabel(index, tab);
        const FString text = label != nullptr ? label->GetText().ToString() : FString();
        const float width = label != nullptr ? SolidSmokeTextWidth(text, label->GetFont(), scale) : -1.0f;
        const float room = SOLSettingsTabs::LabelColumnWidth(tab) + SOL::MenuStyle::BUTTON_GAP;
        const bool bFits = width >= 0.0f && width <= room + SOLID_SMOKE_FIT_TOLERANCE;
        if (tab == ESOLSettingsTab::KeyBindings)
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
        "scale %.3f, the tightest %s%s"), page->GetRowLabelCount(), scale, *worst, *labelDetail));
}
