/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOverlaySmoke.h"

#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHoloFrameWidget.h"
#include "UI/SOLHologramCursorSubsystem.h"
#include "UI/SOLOverlayMenuWidget.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

namespace
{
    // The script's steps, in order
    enum class EOverlayStep : int32
    {
        Setup,              // Step 1: let the HUD draw a few frames with the overlays on
        MasterOff,          // O
        MasterOn,           // O again
        OrbitLinesOff,      // Alt+O
        OrbitLinesOn,       // Alt+O again
        CycleSolo,          // Step 2: Right Shift+O with only Profile 1
        LeftShift,          // Step 3: Left Shift+O does nothing (D1)
        MenuOpen,           // Step 4: Ctrl+O
        LanesSpace,         // Step 4b: Space on the Lanes row of Profile 1 unchecks it (SDD 38)
        LanesSpaceBack,     // Step 4b: Space on it again checks it
        TabClick,           // Step 5: a Slate click on the Profile 3 tab (disabled: its enable page)
        EnableClick,        // Step 5: a Slate click on its Enable row
        TabsShot,           // Step 5: screenshot of the tabs
        PlanetsClick,       // Step 6: a Slate click on Planets in Profile 3
        LanesThirdSpace,    // Step 6b: Space on the Lanes row of Profile 3 unchecks it there only (SDD 38)
        PageUp,             // Step 7: PageUp views Profile 2 (disabled)
        MenuClose,          // Step 8: Esc through Slate
        FinalShot,          // Screenshot with the icons
        CycleToFirst,       // Step 9: Right Shift+O, Profile 3 -> 1
        TagShot,            // Step 9: screenshot of the tag
        CycleToThird,       // Step 10: Right Shift+O, back to Profile 3
        MasterThirdOff,     // Step 11: O in Profile 3
        MasterCycleFirst,   // Step 11: Right Shift+O to Profile 1
        MasterCycleThird,   // Step 11: Right Shift+O to Profile 3
        OrbitThirdOff,      // Step 12: Alt+O in Profile 3
        OrbitCycleFirst,    // Step 12: Right Shift+O to Profile 1
        OrbitCycleThird,    // Step 12: Right Shift+O to Profile 3
        MenuReopen,         // Step 12: Ctrl+O
        OrbitRowClick,      // Step 12: a Slate click on the Orbit Lines row
        DisableClick,       // Step 13: a Slate click on Profile enabled (off)
        ReenableTab,        // Step 13: a Slate click on the Profile 3 tab
        ReenableClick,      // Step 13: a Slate click on its Enable row
        MenuCloseAgain,     // Esc again
        FinalChange,        // Step 14: O makes a write owed (no write yet)
        LeaveOwed,          // Step 14: the leave-flight write point (level transition) writes it once
        LeaveUnchanged,     // Step 14: the write point again, nothing changed: no write
        QuitChange,         // Step 14: O again makes a write owed
        LeaveQuit,          // Step 14: the write point on quit / PIE end starts none (shutdown writes it)
        Quit,
        Count,
    };

    // Step durations (real seconds) and the minimum frames per step
    constexpr double OVERLAYSMOKE_SETUP_S = 3.0;
    constexpr double OVERLAYSMOKE_STEP_S = 0.6;
    constexpr double OVERLAYSMOKE_SHOT_S = 1.5;
    constexpr double OVERLAYSMOKE_TAG_FADE_S = 3.0;             // Longer than the tag's show time and fade
    constexpr double OVERLAYSMOKE_QUIT_S = 3.0;
    constexpr int32 OVERLAYSMOKE_MIN_FRAMES = 5;
    constexpr int32 OVERLAYSMOKE_PROFILE_COUNT = 5;
    constexpr int32 OVERLAYSMOKE_THIRD = 2;                     // Profile 3 (0-based)
    constexpr int32 OVERLAYSMOKE_SECOND = 1;                    // Profile 2 (0-based)
    constexpr int32 OVERLAYSMOKE_PAGE_ROWS = 17;                // Profile 1's page: the 16 table rows and Orbit Lines

    //////////////////////////////////////////////////////////////////////////
    // Returns how long a step lasts at least (real seconds)
    double StepDurationS(const EOverlayStep step)
    {
        switch (step)
        {
        case EOverlayStep::Setup:
            return OVERLAYSMOKE_SETUP_S;
        case EOverlayStep::CycleSolo:
            return OVERLAYSMOKE_TAG_FADE_S;
        case EOverlayStep::FinalShot:
        case EOverlayStep::MenuOpen:
        case EOverlayStep::TabsShot:
        case EOverlayStep::TagShot:
            return OVERLAYSMOKE_SHOT_S;
        case EOverlayStep::Quit:
            return OVERLAYSMOKE_QUIT_S;
        default:
            return OVERLAYSMOKE_STEP_S;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a row is checked in one profile's own mask (not only the active profile's)
    bool IsRowCheckedIn(const SOLOverlay::FOverlayPrefs& prefs, const int32 profile, const SOLOverlay::EOverlay overlay)
    {
        return (prefs.Masks[profile] & (1u << static_cast<int32>(overlay))) != 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the viewed page's Lanes row shows the Lanes label, top level (not indented), and the row shown
    // right below it is Darkening
    bool IsLanesRowListed(const USOLOverlayMenuWidget* menu)
    {
        if (menu == nullptr)
        {
            return false;
        }
        const int32 lanesRow = USOLOverlayMenuWidget::GetOverlayRow(SOLOverlay::EOverlay::Lanes);
        const FString text = menu->GetRowText(lanesRow);
        const FString below = menu->GetRowText(lanesRow + 1);
        return text.Contains(SOLOverlay::GetDef(static_cast<int32>(SOLOverlay::EOverlay::Lanes)).Label)
            && !text.StartsWith(SOLMenuText::OVERLAY_MENU_CHILD_INDENT)
            && below.Contains(SOLOverlay::GetDef(static_cast<int32>(SOLOverlay::EOverlay::Darkening)).Label);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the standalone menu's hologram frame lies inside the game viewport and the page's last row sits
    // inside the frame (absolute Slate space, as last laid out); fills a detail line either way
    bool IsMenuInView(const UWorld* world, const USOLOverlayMenuWidget* menu, FString& outDetail)
    {
        const UGameViewportClient* viewportClient = world != nullptr ? world->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> viewportWidget = viewportClient != nullptr
            ? viewportClient->GetGameViewportWidget() : nullptr;
        const USOLHoloFrameWidget* frame = menu != nullptr ? menu->GetHoloFrame() : nullptr;
        if (!viewportWidget.IsValid() || frame == nullptr)
        {
            outDetail = TEXT("no viewport widget or no hologram frame");
            return false;
        }

        // Both rectangles in absolute space; half a pixel of slack for the layout's rounding
        constexpr double slackPx = 0.5;
        const FGeometry& viewGeometry = viewportWidget->GetCachedGeometry();
        const FVector2D viewMin(viewGeometry.LocalToAbsolute(FVector2D::ZeroVector));
        const FVector2D viewMax(viewGeometry.LocalToAbsolute(viewGeometry.GetLocalSize()));
        const FGeometry& frameGeometry = frame->GetCachedGeometry();
        const FVector2D frameMin(frameGeometry.LocalToAbsolute(FVector2D::ZeroVector));
        const FVector2D frameMax(frameGeometry.LocalToAbsolute(frameGeometry.GetLocalSize()));
        const FVector2D lastRow = menu->GetRowScreenCenter(menu->GetRowCount() - 1);
        const bool bFrameInView = frameMin.X >= viewMin.X - slackPx && frameMin.Y >= viewMin.Y - slackPx
            && frameMax.X <= viewMax.X + slackPx && frameMax.Y <= viewMax.Y + slackPx && frameMax.Y > frameMin.Y;
        const bool bRowInFrame = lastRow.Y > frameMin.Y && lastRow.Y < frameMax.Y;
        outDetail = FString::Printf(TEXT("viewport %.0fx%.0f, frame bottom %.1f (viewport bottom %.1f), last row %d "
            "centre y %.1f"), viewMax.X - viewMin.X, viewMax.Y - viewMin.Y, frameMax.Y, viewMax.Y,
            menu->GetRowCount() - 1, lastRow.Y);
        return bFrameInView && bRowInFrame;
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, HUD and settings and starts the first step
void FSOLOverlaySmoke::Start(ASOLShipPawn& pawn)
{
    mController = pawn.GetController<APlayerController>();
    mHud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    mSettings = USOLSettingsSubsystem::Get(&pawn);
    UE_LOG(LogSOL, Log, TEXT("SmokeOverlay: scripted overlay input starting (settings slot %s)"),
        mSettings.IsValid() ? *mSettings->GetSlotName() : TEXT("none"));
    mStep = 0;
    EnterStep(mStep, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLOverlaySmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mStep >= static_cast<int32>(EOverlayStep::Count))
    {
        return true;
    }
    if (!mController.IsValid() || !mSettings.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeOverlay: controller or settings missing; aborting"));
        mStep = static_cast<int32>(EOverlayStep::Count);
        return true;
    }
    if (!mHud.IsValid())
    {
        mHud = mController->GetHUD<ASOLFlightHud>();
    }
    for (const FKey& key : mPendingReleases)
    {
        ReleaseKey(key);
    }
    mPendingReleases.Reset();

    // Each step lasts its duration and at least a few frames, then checks and moves on
    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    if (mStepTimeS < StepDurationS(static_cast<EOverlayStep>(mStep)) || mStepFrames < OVERLAYSMOKE_MIN_FRAMES)
    {
        return false;
    }
    ExitStep(mStep, pawn);
    ++mStep;
    mStepTimeS = 0.0;
    mStepFrames = 0;
    if (mStep >= static_cast<int32>(EOverlayStep::Count))
    {
        return true;
    }
    EnterStep(mStep, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLOverlaySmoke::EnterStep(const int32 step, ASOLShipPawn& pawn)
{
    const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
    switch (static_cast<EOverlayStep>(step))
    {
    case EOverlayStep::MasterOff:
    case EOverlayStep::MasterOn:
    case EOverlayStep::MasterThirdOff:
    case EOverlayStep::FinalChange:
        PressKey(EKeys::O);
        mPendingReleases.Add(EKeys::O);
        if (static_cast<EOverlayStep>(step) == EOverlayStep::FinalChange)
        {
            mSavesBefore = mSettings->GetSaveRequestCount();
        }
        break;
    case EOverlayStep::OrbitLinesOff:
    case EOverlayStep::OrbitLinesOn:
    case EOverlayStep::OrbitThirdOff:
        mWasOrbitLinesShown = mSettings->AreOrbitLinesShown();
        TapChord(EKeys::LeftAlt, EKeys::O);
        break;
    case EOverlayStep::CycleSolo:
    case EOverlayStep::CycleToFirst:
    case EOverlayStep::CycleToThird:
    case EOverlayStep::MasterCycleFirst:
    case EOverlayStep::MasterCycleThird:
    case EOverlayStep::OrbitCycleFirst:
    case EOverlayStep::OrbitCycleThird:
        mWasDarkening = SOLOverlay::IsEnabled(mSettings->GetOverlayPrefs(), SOLOverlay::EOverlay::Darkening);
        TapChord(EKeys::RightShift, EKeys::O);
        break;
    case EOverlayStep::LeftShift:
        mWasDarkening = SOLOverlay::IsEnabled(mSettings->GetOverlayPrefs(), SOLOverlay::EOverlay::Darkening);
        TapChord(EKeys::LeftShift, EKeys::O);
        break;
    case EOverlayStep::MenuOpen:
        // The Lanes steps check that no write happens while the menu is open (MenuClose takes its own count)
        mSavesBefore = mSettings->GetSaveRequestCount();
        TapChord(EKeys::LeftControl, EKeys::O);
        break;
    case EOverlayStep::MenuReopen:
        TapChord(EKeys::LeftControl, EKeys::O);
        break;
    case EOverlayStep::LanesSpace:
    case EOverlayStep::LanesSpaceBack:
    case EOverlayStep::LanesThirdSpace:
        // Up / Down move the highlight to the Lanes row, then Space toggles it, all through Slate like a player
        if (menu != nullptr)
        {
            mWasLanesShown = mSettings->IsOverlayShown(SOLOverlay::EOverlay::Lanes);
            HighlightRow(*menu, USOLOverlayMenuWidget::GetOverlayRow(SOLOverlay::EOverlay::Lanes));
            SlateKey(EKeys::SpaceBar);
        }
        break;
    case EOverlayStep::TabClick:
    case EOverlayStep::ReenableTab:
        if (menu != nullptr)
        {
            ClickAt(menu->GetTabScreenCenter(OVERLAYSMOKE_THIRD));
        }
        break;
    case EOverlayStep::EnableClick:
    case EOverlayStep::ReenableClick:
        if (menu != nullptr)
        {
            ClickAt(menu->GetRowScreenCenter(0));
        }
        break;
    case EOverlayStep::PlanetsClick:
        // A real Slate click at the Planets row's centre
        if (menu != nullptr)
        {
            ClickAt(menu->GetRowScreenCenter(USOLOverlayMenuWidget::GetOverlayRow(SOLOverlay::EOverlay::IconPlanets)));
        }
        break;
    case EOverlayStep::OrbitRowClick:
        if (menu != nullptr)
        {
            ClickAt(menu->GetRowScreenCenter(USOLOverlayMenuWidget::ORBIT_LINES_ROW));
        }
        break;
    case EOverlayStep::DisableClick:
        if (menu != nullptr)
        {
            ClickAt(menu->GetRowScreenCenter(USOLOverlayMenuWidget::PROFILE_ENABLED_ROW));
        }
        break;
    case EOverlayStep::PageUp:
        SlateKey(EKeys::PageUp);
        break;
    case EOverlayStep::MenuClose:
    case EOverlayStep::MenuCloseAgain:
        mSavesBefore = mSettings->GetSaveRequestCount();
        SlateKey(EKeys::Escape);
        break;
    case EOverlayStep::LeaveOwed:
    case EOverlayStep::LeaveUnchanged:
        // The pawn's EndPlay write point with a reason that is not quit (a level transition, the pawn destroyed)
        mSavesBefore = mSettings->GetSaveRequestCount();
        mSavesCompletedBefore = mSettings->GetSaveCompletedCount();
        pawn.FlushSettingsAtLeave(static_cast<EOverlayStep>(step) == EOverlayStep::LeaveOwed
            ? EEndPlayReason::LevelTransition : EEndPlayReason::Destroyed);
        break;
    case EOverlayStep::QuitChange:
        PressKey(EKeys::O);
        mPendingReleases.Add(EKeys::O);
        break;
    case EOverlayStep::LeaveQuit:
        // Quit and PIE end leave the owed write to the settings subsystem's synchronous shutdown write
        mSavesBefore = mSettings->GetSaveRequestCount();
        pawn.FlushSettingsAtLeave(EEndPlayReason::Quit);
        pawn.FlushSettingsAtLeave(EEndPlayReason::EndPlayInEditor);
        break;
    case EOverlayStep::TabsShot:
        TakeUiShot(TEXT("Tabs"));
        break;
    case EOverlayStep::TagShot:
        TakeUiShot(TEXT("Tag"));
        break;
    case EOverlayStep::FinalShot:
        TakeUiShot(TEXT("Icons"));
        break;
    case EOverlayStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("SmokeOverlay: %d/%d checks passed"), mChecksPassed, mChecksRun);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLOverlaySmoke::ExitStep(const int32 step, ASOLShipPawn& pawn)
{
    const ASOLFlightHud* hud = mHud.Get();
    const int32 icons = hud != nullptr ? hud->GetIconLayer().GetLastIconCount() : 0;
    const int32 arrows = hud != nullptr ? hud->GetIconLayer().GetLastArrowCount() : 0;
    const int32 planets = hud != nullptr
        ? hud->GetIconLayer().GetLastIconCount(SOLOverlayIcons::EIconCategory::Planet) : 0;
    const ASOLOrbitLineVisuals* lines = hud != nullptr ? hud->GetOrbitLineVisuals() : nullptr;
    const int32 visibleLines = lines != nullptr ? lines->CountVisibleLines() : 0;
    const bool bMaster = mSettings->IsOverlayMasterOn();
    const SOLOverlay::FOverlayPrefs& prefs = mSettings->GetOverlayPrefs();
    const int32 active = prefs.ActiveProfile + 1;
    const float tagAlpha = hud != nullptr ? hud->GetLastProfileTagAlpha() : 0.0f;
    const FString tagText = hud != nullptr ? hud->GetProfileTagText() : FString();
    const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
    const int32 rows = menu != nullptr ? menu->GetRowCount() : 0;
    const int32 viewed = menu != nullptr ? menu->GetViewedProfile() + 1 : 0;
    const bool bLanesShown = mSettings->IsOverlayShown(SOLOverlay::EOverlay::Lanes);
    const bool bLanesChecked = SOLOverlay::IsEnabled(prefs, SOLOverlay::EOverlay::Lanes);
    const int32 writesInMenu = mSettings->GetSaveRequestCount() - mSavesBefore;
    switch (static_cast<EOverlayStep>(step))
    {
    case EOverlayStep::Setup:
    {
        mStartIcons = icons;
        Check(TEXT("a-start-on"), bMaster && icons + arrows > 0, FString::Printf(TEXT("flight start: master %s, %d "
            "icons (%d planets), %d edge arrows, %d orbit lines, %d NPC ship contacts"), bMaster ? TEXT("on")
            : TEXT("off"), icons, planets, arrows, visibleLines, hud != nullptr ? hud->GetLastRadarShipCount() : 0));
        Check(TEXT("1-start"), prefs.ActiveProfile == 0 && prefs.EnabledProfiles == 0x01 && bMaster
            && tagAlpha == 0.0f, FString::Printf(TEXT("active Profile %d, enabled bits 0x%02x, master %s, tag alpha "
            "%.2f"), active, prefs.EnabledProfiles, bMaster ? TEXT("on") : TEXT("off"), tagAlpha));
        Check(TEXT("a-lanes-start"), bLanesChecked && bLanesShown, FString::Printf(TEXT("flight start: Lanes row %s, "
            "shown %s (on by default, D5)"), bLanesChecked ? TEXT("checked") : TEXT("UNCHECKED"), bLanesShown
            ? TEXT("yes") : TEXT("NO")));
        const USOLHologramCursorSubsystem* cursor = pawn.GetWorld()->GetSubsystem<USOLHologramCursorSubsystem>();
        const UGameViewportClient* viewport = pawn.GetWorld()->GetGameViewport();
        const bool bInstalled = cursor != nullptr && cursor->GetCursorWidget().IsValid() && viewport != nullptr
            && viewport->GetSoftwareCursorWidget(EMouseCursor::Default) == cursor->GetCursorWidget();
        const int32 paints = bInstalled ? cursor->GetCursorWidget()->GetPaintCount() : 0;
        Check(TEXT("a-cursor"), bInstalled && paints > 0, FString::Printf(TEXT("hologram pointer %s as the software "
            "cursor, painted %d times"), bInstalled ? TEXT("installed") : TEXT("NOT installed"), paints));
        TakeUiShot(TEXT("Start"));
        break;
    }
    case EOverlayStep::MasterOff:
        Check(TEXT("b-master-off"), !bMaster && icons == 0 && arrows == 0 && visibleLines == 0,
            FString::Printf(TEXT("O: master %s, %d icons, %d arrows, %d orbit lines"), bMaster ? TEXT("on")
            : TEXT("off"), icons, arrows, visibleLines));
        Check(TEXT("b-lanes-off"), !bLanesShown && bLanesChecked, FString::Printf(TEXT("O: Lanes %s, its row %s "
            "(the master governs it, D11)"), bLanesShown ? TEXT("SHOWN") : TEXT("hidden"), bLanesChecked
            ? TEXT("still checked") : TEXT("UNCHECKED")));
        break;
    case EOverlayStep::MasterOn:
        Check(TEXT("c-master-on"), bMaster && icons == mStartIcons, FString::Printf(TEXT("O again: master %s, %d "
            "icons (start %d), %d orbit lines"), bMaster ? TEXT("on") : TEXT("off"), icons, mStartIcons,
            visibleLines));
        Check(TEXT("c-lanes-on"), bLanesShown, FString::Printf(TEXT("O again: Lanes %s"), bLanesShown
            ? TEXT("shown") : TEXT("HIDDEN")));
        break;
    case EOverlayStep::OrbitLinesOff:
    case EOverlayStep::OrbitLinesOn:
    {
        const bool bShown = mSettings->AreOrbitLinesShown();
        Check(step == static_cast<int32>(EOverlayStep::OrbitLinesOff) ? TEXT("d-alt-o-off") : TEXT("d-alt-o-on"),
            bShown != mWasOrbitLinesShown && bMaster && !pawn.IsFreeLooking() && (bShown == (visibleLines > 0)),
            FString::Printf(TEXT("Alt+O: orbit lines %s -> %s (%d visible), master %s, free look %s"),
            mWasOrbitLinesShown ? TEXT("on") : TEXT("off"), bShown ? TEXT("on") : TEXT("off"), visibleLines,
            bMaster ? TEXT("on") : TEXT("off"), pawn.IsFreeLooking() ? TEXT("ON") : TEXT("off")));
        break;
    }
    case EOverlayStep::CycleSolo:
    {
        // Checked after the tag faded: its text is still what the press formatted
        const bool bDark = SOLOverlay::IsEnabled(prefs, SOLOverlay::EOverlay::Darkening);
        Check(TEXT("2-rshift-solo"), prefs.ActiveProfile == 0 && bMaster && bDark == mWasDarkening
            && tagText.Contains(TEXT("PROFILE 1")) && tagAlpha == 0.0f, FString::Printf(TEXT("Right Shift+O, only "
            "Profile 1: active Profile %d, master %s, darkening unchanged %s, tag text '%s' (faded, alpha %.2f)"),
            active, bMaster ? TEXT("on") : TEXT("off"), bDark == mWasDarkening ? TEXT("yes") : TEXT("NO"), *tagText,
            tagAlpha));
        break;
    }
    case EOverlayStep::LeftShift:
    {
        const bool bDark = SOLOverlay::IsEnabled(prefs, SOLOverlay::EOverlay::Darkening);
        Check(TEXT("3-lshift-none"), prefs.ActiveProfile == 0 && bMaster && bDark == mWasDarkening
            && !pawn.IsOverlayMenuOpen() && tagAlpha == 0.0f && icons == mStartIcons, FString::Printf(TEXT("Left "
            "Shift+O: active Profile %d, master %s, darkening %s, menu %s, tag alpha %.2f, %d icons (start %d)"),
            active, bMaster ? TEXT("on") : TEXT("off"), bDark == mWasDarkening ? TEXT("unchanged") : TEXT("CHANGED"),
            pawn.IsOverlayMenuOpen() ? TEXT("OPEN") : TEXT("closed"), tagAlpha, icons, mStartIcons));
        break;
    }
    case EOverlayStep::MenuOpen:
    {
        const int32 orbitRow = USOLOverlayMenuWidget::GetOverlayRow(SOLOverlay::EOverlay::PlanetOrbits);
        const int32 moonRow = USOLOverlayMenuWidget::GetOverlayRow(SOLOverlay::EOverlay::MoonOrbits);
        const bool bIndented = menu != nullptr
            && menu->GetRowText(orbitRow).Contains(SOLMenuText::OVERLAY_MENU_CHILD_INDENT)
            && menu->GetRowText(moonRow).Contains(SOLMenuText::OVERLAY_MENU_CHILD_INDENT)
            && !menu->GetRowText(USOLOverlayMenuWidget::ORBIT_LINES_ROW).StartsWith(
            SOLMenuText::OVERLAY_MENU_CHILD_INDENT);
        Check(TEXT("4-menu-open"), pawn.IsOverlayMenuOpen() && menu != nullptr
            && menu->GetTabCount() == OVERLAYSMOKE_PROFILE_COUNT && rows == OVERLAYSMOKE_PAGE_ROWS && bIndented
            && bMaster, FString::Printf(TEXT("Ctrl+O: menu %s, %d tabs, %d rows (want %d), orbit rows indented %s, "
            "master %s"), pawn.IsOverlayMenuOpen() ? TEXT("open") : TEXT("closed"),
            menu != nullptr ? menu->GetTabCount() : 0, rows, OVERLAYSMOKE_PAGE_ROWS, bIndented ? TEXT("yes")
            : TEXT("NO"), bMaster ? TEXT("on") : TEXT("off")));
        Check(TEXT("4-lanes-row"), IsLanesRowListed(menu) && bLanesChecked && bLanesShown, FString::Printf(TEXT(
            "Profile 1's page: Lanes row '%s' (top level, above Darkening: %s), checked %s, shown %s"),
            menu != nullptr ? *menu->GetRowText(USOLOverlayMenuWidget::GetOverlayRow(SOLOverlay::EOverlay::Lanes))
            : TEXT(""), IsLanesRowListed(menu) ? TEXT("yes") : TEXT("NO"), bLanesChecked ? TEXT("yes") : TEXT("NO"),
            bLanesShown ? TEXT("yes") : TEXT("NO")));
        FString fitDetail;
        const bool bInView = IsMenuInView(pawn.GetWorld(), menu, fitDetail);
        Check(TEXT("4-menu-fits"), bInView, FString::Printf(TEXT("Profile 1's %d-row page inside the view: %s"), rows,
            *fitDetail));
        break;
    }
    case EOverlayStep::LanesSpace:
    case EOverlayStep::LanesSpaceBack:
    {
        // Space flips the row in memory only: no write while the menu is open (the close writes once)
        const bool bWantOn = static_cast<EOverlayStep>(step) == EOverlayStep::LanesSpaceBack;
        const int32 lanesRow = USOLOverlayMenuWidget::GetOverlayRow(SOLOverlay::EOverlay::Lanes);
        const int32 highlighted = menu != nullptr ? menu->GetHighlightedRow() : INDEX_NONE;
        Check(bWantOn ? TEXT("4-lanes-space-on") : TEXT("4-lanes-space-off"), prefs.ActiveProfile == 0
            && highlighted == lanesRow && mWasLanesShown != bWantOn && bLanesChecked == bWantOn
            && bLanesShown == bWantOn && bMaster && writesInMenu == 0, FString::Printf(TEXT("Space on the Lanes row "
            "(highlight %d, want %d) in Profile %d: shown %s -> %s, checked %s, master %s, %d write(s) in the menu "
            "(want 0)"), highlighted, lanesRow, active, mWasLanesShown ? TEXT("on") : TEXT("off"), bLanesShown
            ? TEXT("on") : TEXT("off"), bLanesChecked ? TEXT("yes") : TEXT("no"), bMaster ? TEXT("on") : TEXT("OFF"),
            writesInMenu));
        break;
    }
    case EOverlayStep::LanesThirdSpace:
    {
        // Per profile: Profile 3 loses its Lanes bit, Profile 1 keeps its own
        const bool bThird = IsRowCheckedIn(prefs, OVERLAYSMOKE_THIRD, SOLOverlay::EOverlay::Lanes);
        const bool bFirst = IsRowCheckedIn(prefs, 0, SOLOverlay::EOverlay::Lanes);
        Check(TEXT("6-lanes-third"), prefs.ActiveProfile == OVERLAYSMOKE_THIRD && mWasLanesShown && !bLanesShown
            && !bThird && bFirst && writesInMenu == 0, FString::Printf(TEXT("Space on the Lanes row in Profile %d: "
            "shown %s -> %s, Profile 3 %s, Profile 1 %s, %d write(s) in the menu (want 0)"), active, mWasLanesShown
            ? TEXT("on") : TEXT("off"), bLanesShown ? TEXT("ON") : TEXT("off"), bThird ? TEXT("CHECKED")
            : TEXT("unchecked"), bFirst ? TEXT("checked") : TEXT("UNCHECKED"), writesInMenu));
        break;
    }
    case EOverlayStep::TabClick:
        Check(TEXT("5-tab-click"), viewed == OVERLAYSMOKE_THIRD + 1 && rows == 1 && prefs.ActiveProfile == 0
            && !SOLOverlay::IsProfileEnabled(prefs, OVERLAYSMOKE_THIRD), FString::Printf(TEXT("click on the "
            "Profile 3 tab: viewing Profile %d, %d row(s), active Profile %d, Profile 3 %s"), viewed, rows, active,
            SOLOverlay::IsProfileEnabled(prefs, OVERLAYSMOKE_THIRD) ? TEXT("ENABLED") : TEXT("disabled")));
        break;
    case EOverlayStep::EnableClick:
        Check(TEXT("5-enable"), SOLOverlay::IsProfileEnabled(prefs, OVERLAYSMOKE_THIRD)
            && prefs.ActiveProfile == OVERLAYSMOKE_THIRD && prefs.Masks[OVERLAYSMOKE_THIRD] == prefs.Masks[0]
            && rows == OVERLAYSMOKE_PAGE_ROWS + 1, FString::Printf(TEXT("click on Enable: Profile 3 %s, active "
            "Profile %d, mask 0x%x (Profile 1: 0x%x), %d rows"), SOLOverlay::IsProfileEnabled(prefs,
            OVERLAYSMOKE_THIRD) ? TEXT("enabled") : TEXT("NOT enabled"), active, prefs.Masks[OVERLAYSMOKE_THIRD],
            prefs.Masks[0], rows));
        Check(TEXT("5-lanes-row"), IsLanesRowListed(menu) && bLanesChecked, FString::Printf(TEXT("Profile 3's page: "
            "Lanes row %s, checked (copied from Profile 1) %s"), IsLanesRowListed(menu) ? TEXT("listed")
            : TEXT("MISSING"), bLanesChecked ? TEXT("yes") : TEXT("NO")));
        {
            // The tallest page (the table, Orbit Lines and Profile enabled) still fits
            FString fitDetail;
            const bool bInView = IsMenuInView(pawn.GetWorld(), menu, fitDetail);
            Check(TEXT("5-menu-fits"), bInView, FString::Printf(TEXT("Profile 3's %d-row page inside the view: %s"),
                rows, *fitDetail));
        }
        break;
    case EOverlayStep::PlanetsClick:
    {
        const bool bThird = IsRowCheckedIn(prefs, OVERLAYSMOKE_THIRD, SOLOverlay::EOverlay::IconPlanets);
        const bool bFirst = IsRowCheckedIn(prefs, 0, SOLOverlay::EOverlay::IconPlanets);
        Check(TEXT("6-planets-click"), !bThird && bFirst && planets == 0, FString::Printf(TEXT("click on Planets in "
            "Profile 3: Profile 3 %s, Profile 1 %s, %d planet icons"), bThird ? TEXT("CHECKED") : TEXT("unchecked"),
            bFirst ? TEXT("checked") : TEXT("UNCHECKED"), planets));
        break;
    }
    case EOverlayStep::PageUp:
        Check(TEXT("7-page-up"), viewed == OVERLAYSMOKE_SECOND + 1 && rows == 1 && prefs.ActiveProfile
            == OVERLAYSMOKE_THIRD, FString::Printf(TEXT("PageUp: viewing Profile %d, %d row(s), active Profile %d"),
            viewed, rows, active));
        break;
    case EOverlayStep::MenuClose:
    {
        const int32 saves = mSettings->GetSaveRequestCount() - mSavesBefore;
        Check(TEXT("8-menu-close"), !pawn.IsOverlayMenuOpen() && saves == 1, FString::Printf(TEXT("Esc: menu %s, "
            "%d settings write(s) at the close"), pawn.IsOverlayMenuOpen() ? TEXT("open") : TEXT("closed"), saves));
        break;
    }
    case EOverlayStep::CycleToFirst:
    {
        // No planet is on screen at the start position (they are edge arrows), so the HUD's planet icon count cannot
        // tell the profiles apart: the model's verdict for the Planets row is checked next to a live icon count
        const bool bShown = mSettings->IsOverlayShown(SOLOverlay::EOverlay::IconPlanets);
        Check(TEXT("9-cycle-first"), prefs.ActiveProfile == 0 && bShown && icons > 0
            && tagText.Contains(TEXT("PROFILE 1")) && tagAlpha > 0.0f, FString::Printf(TEXT("Right Shift+O: active "
            "Profile %d, Planets row %s, %d icons (%d planet icons), tag '%s' alpha %.2f"), active, bShown
            ? TEXT("shown") : TEXT("HIDDEN"), icons, planets, *tagText, tagAlpha));
        Check(TEXT("9-lanes-first"), prefs.ActiveProfile == 0 && bLanesShown, FString::Printf(TEXT("Right Shift+O "
            "to Profile %d: Lanes %s (its own bit)"), active, bLanesShown ? TEXT("shown") : TEXT("HIDDEN")));
        break;
    }
    case EOverlayStep::CycleToThird:
    {
        const bool bShown = mSettings->IsOverlayShown(SOLOverlay::EOverlay::IconPlanets);
        Check(TEXT("10-cycle-third"), prefs.ActiveProfile == OVERLAYSMOKE_THIRD && !bShown && planets == 0
            && tagText.Contains(TEXT("PROFILE 3")), FString::Printf(TEXT("Right Shift+O: active Profile %d, Planets "
            "row %s, %d planet icons, tag '%s'"), active, bShown ? TEXT("SHOWN") : TEXT("hidden"), planets,
            *tagText));
        Check(TEXT("10-lanes-third"), prefs.ActiveProfile == OVERLAYSMOKE_THIRD && bMaster && !bLanesShown
            && IsRowCheckedIn(prefs, 0, SOLOverlay::EOverlay::Lanes), FString::Printf(TEXT("Right Shift+O to Profile "
            "%d (master %s): Lanes %s (its own bit), Profile 1's Lanes %s"), active, bMaster ? TEXT("on")
            : TEXT("OFF"), bLanesShown ? TEXT("SHOWN") : TEXT("hidden"), IsRowCheckedIn(prefs, 0,
            SOLOverlay::EOverlay::Lanes) ? TEXT("checked") : TEXT("UNCHECKED")));
        break;
    }
    case EOverlayStep::MasterThirdOff:
        Check(TEXT("11-master-third"), prefs.ActiveProfile == OVERLAYSMOKE_THIRD && !bMaster && icons == 0,
            FString::Printf(TEXT("O in Profile 3: master %s, %d icons"), bMaster ? TEXT("ON") : TEXT("off"), icons));
        break;
    case EOverlayStep::MasterCycleFirst:
        Check(TEXT("11-master-first"), prefs.ActiveProfile == 0 && bMaster && icons > 0, FString::Printf(TEXT("Right "
            "Shift+O to Profile %d: master %s, %d icons"), active, bMaster ? TEXT("on") : TEXT("OFF"), icons));
        break;
    case EOverlayStep::MasterCycleThird:
        Check(TEXT("11-master-kept"), prefs.ActiveProfile == OVERLAYSMOKE_THIRD && !bMaster, FString::Printf(TEXT(
            "Right Shift+O to Profile %d: master %s (Profile 3 left it off)"), active, bMaster ? TEXT("ON")
            : TEXT("off")));
        break;
    case EOverlayStep::OrbitThirdOff:
        Check(TEXT("12-orbit-third"), prefs.ActiveProfile == OVERLAYSMOKE_THIRD && mWasOrbitLinesShown
            && !mSettings->AreOrbitLinesShown(), FString::Printf(TEXT("Alt+O in Profile %d: orbit lines %s -> %s"),
            active, mWasOrbitLinesShown ? TEXT("on") : TEXT("off"), mSettings->AreOrbitLinesShown() ? TEXT("ON")
            : TEXT("off")));
        break;
    case EOverlayStep::OrbitCycleFirst:
        Check(TEXT("12-orbit-first"), prefs.ActiveProfile == 0 && mSettings->AreOrbitLinesShown(),
            FString::Printf(TEXT("Right Shift+O to Profile %d: orbit lines %s"), active,
            mSettings->AreOrbitLinesShown() ? TEXT("on") : TEXT("OFF")));
        break;
    case EOverlayStep::OrbitCycleThird:
        Check(TEXT("12-orbit-kept"), prefs.ActiveProfile == OVERLAYSMOKE_THIRD && !mSettings->AreOrbitLinesShown(),
            FString::Printf(TEXT("Right Shift+O to Profile %d: orbit lines %s (Profile 3 left them off)"), active,
            mSettings->AreOrbitLinesShown() ? TEXT("ON") : TEXT("off")));
        break;
    case EOverlayStep::MenuReopen:
        Check(TEXT("12-menu-reopen"), pawn.IsOverlayMenuOpen() && viewed == OVERLAYSMOKE_THIRD + 1,
            FString::Printf(TEXT("Ctrl+O: menu %s, viewing Profile %d"), pawn.IsOverlayMenuOpen() ? TEXT("open")
            : TEXT("CLOSED"), viewed));
        break;
    case EOverlayStep::OrbitRowClick:
        Check(TEXT("12-orbit-row"), prefs.ActiveProfile == OVERLAYSMOKE_THIRD && mSettings->AreOrbitLinesShown(),
            FString::Printf(TEXT("click on the Orbit Lines row in Profile %d: orbit lines %s"), active,
            mSettings->AreOrbitLinesShown() ? TEXT("on") : TEXT("OFF")));
        break;
    case EOverlayStep::DisableClick:
        Check(TEXT("13-disable"), !SOLOverlay::IsProfileEnabled(prefs, OVERLAYSMOKE_THIRD) && prefs.ActiveProfile == 0
            && viewed == 1, FString::Printf(TEXT("click on Profile enabled: Profile 3 %s, active Profile %d, viewing "
            "Profile %d"), SOLOverlay::IsProfileEnabled(prefs, OVERLAYSMOKE_THIRD) ? TEXT("ENABLED")
            : TEXT("disabled"), active, viewed));
        break;
    case EOverlayStep::ReenableTab:
        Check(TEXT("13-tab"), viewed == OVERLAYSMOKE_THIRD + 1 && rows == 1 && prefs.ActiveProfile == 0,
            FString::Printf(TEXT("click on the Profile 3 tab: viewing Profile %d, %d row(s), active Profile %d"),
            viewed, rows, active));
        break;
    case EOverlayStep::ReenableClick:
    {
        // Its own values come back (D12): Planets and Lanes unchecked, master and orbit lines as left, not a copy of
        // Profile 1
        const bool bPlanets = IsRowCheckedIn(prefs, OVERLAYSMOKE_THIRD, SOLOverlay::EOverlay::IconPlanets);
        Check(TEXT("13-reenable"), SOLOverlay::IsProfileEnabled(prefs, OVERLAYSMOKE_THIRD)
            && prefs.ActiveProfile == OVERLAYSMOKE_THIRD && !bPlanets && !bLanesChecked && !bMaster
            && mSettings->AreOrbitLinesShown(), FString::Printf(TEXT("click on Enable: Profile 3 %s, active Profile "
            "%d, Planets %s, Lanes %s, master %s, orbit lines %s"), SOLOverlay::IsProfileEnabled(prefs,
            OVERLAYSMOKE_THIRD) ? TEXT("enabled") : TEXT("NOT enabled"), active, bPlanets ? TEXT("CHECKED")
            : TEXT("unchecked"), bLanesChecked ? TEXT("CHECKED") : TEXT("unchecked"), bMaster ? TEXT("ON")
            : TEXT("off"), mSettings->AreOrbitLinesShown() ? TEXT("on") : TEXT("OFF")));
        break;
    }
    case EOverlayStep::MenuCloseAgain:
    {
        const int32 saves = mSettings->GetSaveRequestCount() - mSavesBefore;
        Check(TEXT("13-menu-close"), !pawn.IsOverlayMenuOpen() && saves == 1,
            FString::Printf(TEXT("Esc: menu %s, %d settings write(s)"), pawn.IsOverlayMenuOpen()
            ? TEXT("open") : TEXT("closed"), saves));
        break;
    }
    case EOverlayStep::FinalChange:
        Check(TEXT("14-no-write-yet"), bMaster && mSettings->GetSaveRequestCount() == mSavesBefore,
            FString::Printf(TEXT("O in Profile 3: master %s, %d write(s) yet (want 0)"), bMaster ? TEXT("on")
            : TEXT("OFF"), mSettings->GetSaveRequestCount() - mSavesBefore));
        break;
    case EOverlayStep::LeaveOwed:
    {
        const int32 saves = mSettings->GetSaveRequestCount() - mSavesBefore;
        const int32 completed = mSettings->GetSaveCompletedCount() - mSavesCompletedBefore;
        Check(TEXT("14-leave-owed"), saves == 1 && completed == 1, FString::Printf(TEXT("leaving flight (level "
            "transition) with the O change owed: %d write(s) started, %d finished (want 1 and 1)"), saves, completed));
        break;
    }
    case EOverlayStep::LeaveUnchanged:
    {
        const int32 saves = mSettings->GetSaveRequestCount() - mSavesBefore;
        Check(TEXT("14-leave-unchanged"), saves == 0, FString::Printf(TEXT("leaving flight (destroyed) with nothing "
            "changed since the last write: %d write(s) (want 0)"), saves));
        break;
    }
    case EOverlayStep::QuitChange:
        Check(TEXT("14-quit-change"), !bMaster && mSettings->GetSaveRequestCount() == mSavesBefore,
            FString::Printf(TEXT("O in Profile 3 again: master %s, %d write(s) (want 0)"), bMaster ? TEXT("ON")
            : TEXT("off"), mSettings->GetSaveRequestCount() - mSavesBefore));
        break;
    case EOverlayStep::LeaveQuit:
    {
        // The quit that follows writes the owed change once, synchronously ("shutdown write" in the log)
        const int32 saves = mSettings->GetSaveRequestCount() - mSavesBefore;
        Check(TEXT("14-leave-quit"), saves == 0, FString::Printf(TEXT("leaving flight on quit / PIE end with a change "
            "owed: %d async write(s) (want 0; the shutdown write takes it)"), saves));
        break;
    }
    case EOverlayStep::Quit:
        UKismetSystemLibrary::QuitGame(pawn.GetWorld(), nullptr, EQuitPreference::Quit, false);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a real Slate left click at a screen point (the pointer's hot spot is where the click lands)
void FSOLOverlaySmoke::ClickAt(const FVector2D& at) const
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    slate.SetCursorPos(at);
    const FPointerEvent down(slate.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, at, at,
        TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
    slate.ProcessMouseButtonDownEvent(nullptr, down);
    const FPointerEvent up(slate.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, at, at,
        TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
    slate.ProcessMouseButtonUpEvent(up);
    UE_LOG(LogSOL, Log, TEXT("SmokeOverlay: Slate click at (%.0f, %.0f)"), at.X, at.Y);
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press and release through Slate (to the focused widget)
void FSOLOverlaySmoke::SlateKey(const FKey& key) const
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    const FKeyEvent keyEvent(key, FModifierKeysState(), slate.GetUserIndexForKeyboard(), false, 0, 0);
    slate.ProcessKeyDownEvent(keyEvent);
    slate.ProcessKeyUpEvent(keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// Moves the menu's highlight to a display row with Slate Down presses (it wraps over the viewed page's rows)
void FSOLOverlaySmoke::HighlightRow(const USOLOverlayMenuWidget& menu, const int32 row) const
{
    const int32 count = FMath::Max(menu.GetRowCount(), 1);
    const int32 presses = ((row - menu.GetHighlightedRow()) % count + count) % count;
    for (int32 press = 0; press < presses; ++press)
    {
        SlateKey(EKeys::Down);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller (game input)
void FSOLOverlaySmoke::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller (game input)
void FSOLOverlaySmoke::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a key with a modifier held; both are released on the next frame
void FSOLOverlaySmoke::TapChord(const FKey& modifier, const FKey& key)
{
    PressKey(modifier);
    PressKey(key);
    mPendingReleases.Add(key);
    mPendingReleases.Add(modifier);
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot with the UI to Saved/Screenshots
void FSOLOverlaySmoke::TakeUiShot(const TCHAR* name)
{
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("OverlaySmoke_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeOverlay: screenshot with UI %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLOverlaySmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeOverlay CHECK %-16s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
