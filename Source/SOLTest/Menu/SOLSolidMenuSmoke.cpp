/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuSmoke.h"

#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSmokeBoard.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"
#include "SOLTest.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // The script's steps, in order
    enum class ESolidSmokeStep : int32
    {
        Setup,          // The main menu shows; the kit's assets and the buttons' material checked
        View,           // r.SetRes 1280x720 (windowed), until the view has that size
        Hover,          // A mouse-style hover on Demo: its settled hover look, one highlight
        Down,           // Down: the highlight moves to the next entry
        BoardCreate,    // The comparison board added over the main menu
        Board,          // Its selected tab hovered
        BoardSplit,     // Another tab hovered: hover-only beside selected-only
        StrengthZero,   // Strength 0: the hover look at once, no glow
        StrengthZeroShot,
        StrengthTwo,    // Strength 2: the glow doubled
        Quit,           // Strength back to 1, the board removed, SUMMARY, quit
        Count,
    };

    constexpr int32 SOLID_SMOKE_MIN_FRAMES = 5;                 // Every step lasts at least this many frames
    constexpr int32 SOLID_SMOKE_SHOT_FRAMES = 10;               // A screenshot's frames before the next action
    constexpr int32 SOLID_SMOKE_INSTANT_FRAME = 2;              // Strength 0: the look is read on this frame
    constexpr int32 SOLID_SMOKE_VIEW_STABLE_FRAMES = 10;
    constexpr int32 SOLID_SMOKE_CURSOR_INSET_PX = 8;            // The parked cursor, this far in from the top right
    constexpr int32 SOLID_SMOKE_BOARD_Z = SOL::MenuStyle::Z_ORDER + 10;
    constexpr double SOLID_SMOKE_SETUP_S = 3.0;                 // The main menu's first frames (and shader loads)
    constexpr double SOLID_SMOKE_SETTLE_S = 0.8;                // Longer than every hover ramp and press linger
    constexpr double SOLID_SMOKE_VIEW_TIMEOUT_S = 8.0;
    constexpr double SOLID_SMOKE_WATCHDOG_S = 120.0;
    constexpr float SOLID_SMOKE_TOLERANCE = 1.0e-3f;
    const FIntPoint SOLID_SMOKE_VIEW(1280, 720);

    // Main menu entry indices the script drives (USOLMainMenuWidget's EEntry order: Play, Demo, Start, ...)
    constexpr int32 SOLID_SMOKE_HOVER_ENTRY = 1;
    constexpr int32 SOLID_SMOKE_DOWN_ENTRY = 2;

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
    ++mStep;
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
    // Hovers one board button as the mouse would (it takes keyboard focus)
    const auto hover = [board](const ESOLSolidSmokeButton which)
    {
        USOLMenuButton* button = board != nullptr ? board->GetButton(which) : nullptr;
        if (button != nullptr)
        {
            button->NotifyHovered();
        }
    };
    switch (static_cast<ESolidSmokeStep>(step))
    {
    case ESolidSmokeStep::View:
        mViewFrames = 0;
        if (mController.IsValid())
        {
            mController->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"), SOLID_SMOKE_VIEW.X,
                SOLID_SMOKE_VIEW.Y));
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
    switch (static_cast<ESolidSmokeStep>(step))
    {
    case ESolidSmokeStep::Setup:
        return mStepTimeS >= SOLID_SMOKE_SETUP_S && menu.GetMainMenu() != nullptr
            && menu.GetMainMenu()->IsInViewport();
    case ESolidSmokeStep::View:
        mViewFrames = GetViewSize() == SOLID_SMOKE_VIEW ? mViewFrames + 1 : 0;
        return mViewFrames >= SOLID_SMOKE_VIEW_STABLE_FRAMES || mStepTimeS >= SOLID_SMOKE_VIEW_TIMEOUT_S;
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
        if (bHover)
        {
            TakeUiShot(TEXT("main-hover"));
        }
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
// Logs one check with its verdict and counts it
void FSOLSolidMenuSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SolidMenu CHECK %-22s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
