/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLPlayerGuideWriter.h"

#if !UE_BUILD_SHIPPING

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyboardDiagram.h"
#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLMainMenuWidget.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuCameraPawn.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLPlayerGuide.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLSimClock.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Components/ScrollBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
    const FIntPoint GUIDE_VIEW(1920, 1080);                 // The shots' view size (SDD 33 section 9, D37)
    constexpr int32 GUIDE_VIEW_STABLE_FRAMES = 10;          // Frames in a row at the view size before Help opens
    constexpr double GUIDE_VIEW_TIMEOUT_S = 30.0;           // The menu and view size wait's limit
    constexpr double GUIDE_OPEN_SETTLE_S = 1.5;             // After the Help click (and the shell open): powered up
    constexpr double GUIDE_OPEN_TIMEOUT_S = 15.0;           // The Help open wait's limit
    constexpr double GUIDE_SHOT_SETTLE_S = 1.0;             // A shown tab's hover ramps and first paint settled
    constexpr int32 GUIDE_SHOT_SETTLE_FRAMES = 10;          // ... and at least this many frames
    constexpr int32 GUIDE_SHOT_FRAMES = 10;                 // A capture's frames before the next tab is shown
    constexpr int32 GUIDE_END_STABLE_FRAMES = 10;           // Frames a tab's scroll end holds still before it counts
    constexpr double GUIDE_MEASURE_TIMEOUT_S = 30.0;        // A tab's scroll-through (measure) wait's limit
    constexpr double GUIDE_FILE_CHECK_S = 0.25;             // The capture size check's period
    constexpr double GUIDE_FILES_TIMEOUT_S = 20.0;          // The captures-on-disk wait's limit
    constexpr double GUIDE_WATCHDOG_S = 120.0;              // A stuck run fails and quits
    constexpr int32 GUIDE_CURSOR_INSET_PX = 8;              // The parked cursor, this far in from the top right
    const TCHAR* const GUIDE_HOVER_KEY = TEXT("O");         // The hover shot's key (chords and areas in its popup)
    constexpr int32 GUIDE_MAX_WINDOW_PX = 4096;             // The window may grow past a 1080p desktop's work area
    constexpr const TCHAR* GUIDE_STAGED_SUFFIX = TEXT(".new"); // An image copied next to its target before the swap
    constexpr uint8 GUIDE_EXIT_FAILED = 1;                  // The process exit code of a failed run
    constexpr double GUIDE_PULSE_PHASE = 0.25;              // 33e-iv: the idle pulse's phase in every shot (the
                                                            // traces brightest, the pulse dot a quarter along)
    constexpr double GUIDE_MATERIAL_TIME_S = 0.5;           // 33e-iv: the materials' Time (within the HUD time
                                                            // wrap) on every capture frame
    constexpr int32 GUIDE_FRONT_FRAMES = 60;                // Frames between two pulls of an inactive window to front

    //////////////////////////////////////////////////////////////////////////
    // Holds the materials' game time at a fixed HUD time wrap phase for the rest of this frame (see below)
    void GuideHoldMaterialTime()
    {
        // The application's game time is what the materials' Time node reads (the HUD shape material's flicker and
        // grain on the glossary symbols, drawn again by the capture). The engine sets it from the real time at the
        // next frame's start, so the hold lasts this frame only; it is never later than now. It must run on the game
        // thread outside a task (a task's context scope would restore the time when it ends), and is skipped under a
        // fixed time step or benchmarking, where the engine builds the next frame's time from this one
        if (FApp::UseFixedTimeStep() || FApp::IsBenchmarking())
        {
            return;
        }
        const double wrapS = static_cast<double>(SOL::Hologram::HUD_TIME_WRAP_S);
        const double gameS = FApp::GetCurrentTime() - GStartTime;
        const double heldS = FMath::FloorToDouble((gameS - GUIDE_MATERIAL_TIME_S) / wrapS) * wrapS
            + GUIDE_MATERIAL_TIME_S;
        FApp::SetCurrentTime(GStartTime + heldS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the main menu's Help page, or nullptr
    USOLReadOnlyPageWidget* GuideHelpPage(const USOLMenuSubsystem& menu)
    {
        const USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
        return mainMenu != nullptr ? mainMenu->GetHelpPage() : nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the game window, or nullptr
    TSharedPtr<SWindow> GuideGameWindow()
    {
        return GEngine != nullptr && GEngine->GameViewport != nullptr ? GEngine->GameViewport->GetWindow() : nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the game application is the active one (its window in front)
    bool GuideWindowActive()
    {
        return FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive() && FApp::HasFocus();
    }

    //////////////////////////////////////////////////////////////////////////
    // Pulls the game window to the front now and then while it is inactive (SetForegroundWindow on Windows)
    void GuidePullToFront(const int32 stageFrames)
    {
        const TSharedPtr<SWindow> window = GuideGameWindow();
        if (window.IsValid() && stageFrames % GUIDE_FRONT_FRAMES == 1)
        {
            window->HACK_ForceToFront();
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether no shader is still compiling (an editor-binary -game run compiles them on demand, and a shot
    // taken before would show placeholder materials)
    bool GuideShadersReady()
    {
#if WITH_EDITOR
        return GShaderCompilingManager == nullptr || GShaderCompilingManager->GetNumRemainingJobs() == 0;
#else
        return true;
#endif
    }

    //////////////////////////////////////////////////////////////////////////
    // Asks the engine to quit with an exit code (graceful: the settings snapshot is still given back at shutdown)
    void GuideQuit(const uint8 exitCode)
    {
        FPlatformMisc::RequestExitWithStatus(false, exitCode, TEXT("FSOLPlayerGuideWriter"));
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and starts waiting for the main menu
void FSOLPlayerGuideWriter::Start(USOLMenuSubsystem& menu)
{
    mController = menu.GetPlayerController();
    mLastTimeS = FPlatformTime::Seconds();
    mCaptureSizes.Init(INDEX_NONE, SOLPlayerGuide::Shots().Num());

    // A windowed game is clamped to the desktop's work area: lift the limit and ask for the shots' size, so a 1080p
    // monitor gets the full 1920x1080 view too
    if (const TSharedPtr<SWindow> window = GuideGameWindow())
    {
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(GUIDE_MAX_WINDOW_PX).SetMaxHeight(GUIDE_MAX_WINDOW_PX));
    }
    if (GEngine != nullptr)
    {
        GEngine->Exec(menu.GetWorld(), *FString::Printf(TEXT("r.SetRes %dx%dw"), GUIDE_VIEW.X, GUIDE_VIEW.Y));
    }
    if (!FSOLSimClock::TryParseUtcJulianDate(SOL::Help::GUIDE_SHOT_SIM_DATE, mPinnedJulianDate))
    {
        mPinnedJulianDate = -1.0;
    }
    UE_LOG(LogSOL, Log, TEXT("PlayerGuide: writing %s and %d images (sim date %s)"), *SOLPlayerGuide::GuideFilePath(),
        SOLPlayerGuide::Shots().Num(), SOL::Help::GUIDE_SHOT_SIM_DATE);
    ToStage(EStage::WaitMenu);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the run: the menu, Help, each shot, the captures on disk, then the outputs
bool FSOLPlayerGuideWriter::Update(USOLMenuSubsystem& menu)
{
    if (mIsFinished)
    {
        return true;
    }
    const double nowS = FPlatformTime::Seconds();
    const double deltaS = nowS - mLastTimeS;
    mLastTimeS = nowS;
    mStageS += deltaS;
    mRunS += deltaS;
    ++mStageFrames;
    if (mRunS > GUIDE_WATCHDOG_S)
    {
        Fail(menu, FString::Printf(TEXT("watchdog: stuck in stage %d after %.0f s"), static_cast<int32>(mStage),
            mRunS));
        return true;
    }
    if (!mController.IsValid())
    {
        mController = menu.GetPlayerController();
    }
    if (mPinnedJulianDate < 0.0)
    {
        Fail(menu, FString::Printf(TEXT("the pinned date %s does not parse"), SOL::Help::GUIDE_SHOT_SIM_DATE));
        return true;
    }
    HoldScene(menu);
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLReadOnlyPageWidget* page = GuideHelpPage(menu);
    switch (mStage)
    {
    case EStage::WaitMenu:
    {
        // The main menu shown, the view at the shots' size and the game window in front (33e-iv: an inactive window
        // draws no cursor and its shots differ) for a few frames, then Help opened as a click does
        const FIntPoint view = GetViewSize();
        const bool bIsActive = GuideWindowActive();
        mViewFrames = view == GUIDE_VIEW && menu.IsMenuShowing() && mainMenu != nullptr && GuideShadersReady()
            && bIsActive ? mViewFrames + 1 : 0;
        if (mViewFrames < GUIDE_VIEW_STABLE_FRAMES)
        {
            if (!bIsActive)
            {
                GuidePullToFront(mStageFrames);
            }
            if (mStageS > GUIDE_VIEW_TIMEOUT_S)
            {
                Fail(menu, FString::Printf(TEXT("no main menu at %dx%d in front (view %dx%d, menu %d, shaders ready "
                    "%d, window active %d)"), GUIDE_VIEW.X, GUIDE_VIEW.Y, view.X, view.Y, mainMenu != nullptr ? 1 : 0,
                    GuideShadersReady() ? 1 : 0, bIsActive ? 1 : 0));
                return true;
            }
            return false;
        }

        // The guide shows the default keys: the run's settings slot starts from them
        const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&menu);
        if (settings == nullptr || settings->GetAppliedKeys() != SOLKeyBindings::DefaultKeys())
        {
            Fail(menu, TEXT("the keys are not the default keys (the smoke settings slot was not used)"));
            return true;
        }
        if (const TSharedPtr<SWindow> window = GuideGameWindow())
        {
            window->BringToFront(true);
        }
        ParkCursor();
        USOLMenuButton* button = mainMenu->GetEntryButton(static_cast<int32>(SOLMenuState::EMainMenuEntry::Help));
        if (button == nullptr)
        {
            Fail(menu, TEXT("no Help entry in the main menu"));
            return true;
        }
        button->NotifyHovered();
        button->OnMenuClicked.ExecuteIfBound(button->GetIndex());
        ParkCursor();
        ToStage(EStage::WaitOpen);
        return false;
    }
    case EStage::WaitOpen:
    {
        // Help shown, its shell open and powered up
        const bool bOpen = mainMenu != nullptr && page != nullptr && mainMenu->GetPage() == ESOLMainMenuPage::Help
            && mainMenu->GetShellAnim().Phase == ESOLHoloMenuPhase::Open;
        if (!bOpen || mStageS < GUIDE_OPEN_SETTLE_S)
        {
            if (mStageS > GUIDE_OPEN_TIMEOUT_S)
            {
                Fail(menu, TEXT("the Help page did not open"));
                return true;
            }
            return false;
        }
        if (page->GetTabCount() != static_cast<int32>(ESOLHelpTab::Count) || page->GetDiagram() == nullptr)
        {
            Fail(menu, FString::Printf(TEXT("the Help page has %d tabs (want %d), diagram %d"), page->GetTabCount(),
                static_cast<int32>(ESOLHelpTab::Count), page->GetDiagram() != nullptr ? 1 : 0));
            return true;
        }
        mShot = 0;
        ToStage(EStage::Show);
        return false;
    }
    case EStage::Show:
    {
        // The shot's tab and hover shown, its old capture removed so only this run's file counts
        const FSOLPlayerGuideShot& shot = shots[mShot];
        if (page == nullptr || page->GetDiagram() == nullptr)
        {
            Fail(menu, TEXT("the Help page or its diagram went away"));
            return true;
        }
        page->SelectTab(static_cast<int32>(shot.Tab));
        page->GetDiagram()->SetHoveredKey(shot.bHover ? SOLKeyboardLayout::FindKey(GUIDE_HOVER_KEY) : INDEX_NONE);

        // D46: the tab's scroll at the page's offset (the first page at the top, measured first; the later ones from
        // the extent the first page measured)
        UScrollBox* scroll = page->GetTabScroll(static_cast<int32>(shot.Tab));
        if (scroll == nullptr)
        {
            Fail(menu, FString::Printf(TEXT("%s: no scroll for tab %d"), *shot.FileName, static_cast<int32>(shot.Tab)));
            return true;
        }
        const bool bMeasure = shot.Page == 0 && !shot.bHover;
        scroll->SetScrollOffset(bMeasure ? 0.0f : SOLPlayerGuide::PageOffset(shot.Page, mViewHeight, mScrollEnd));
        ResetEndStable();
        ParkCursor();
        const FString capture = SOLPlayerGuide::CapturePath(*shot.FileName);
        if (!IFileManager::Get().Delete(*capture, false, true, true) && IFileManager::Get().FileExists(*capture))
        {
            Fail(menu, FString::Printf(TEXT("could not remove the old capture %s"), *capture));
            return true;
        }
        ToStage(bMeasure ? EStage::Measure : EStage::Settle);
        return false;
    }
    case EStage::Measure:
    {
        // D46: a description below the view wraps at its real width only once it has been painted, so the tab's
        // end grows as it is scrolled (as SOLSolidMenuSmoke re-scrolls while the end moves): the tab stepped down a
        // view at a time, each step held until the end stops moving, until the offset is at the stable end
        const FSOLPlayerGuideShot& shot = shots[mShot];
        UScrollBox* scroll = page != nullptr ? page->GetTabScroll(static_cast<int32>(shot.Tab)) : nullptr;
        if (scroll == nullptr)
        {
            Fail(menu, FString::Printf(TEXT("%s: no scroll to measure"), *shot.FileName));
            return true;
        }
        const float view = static_cast<float>(scroll->GetCachedGeometry().GetLocalSize().Y);
        if (!IsEndStable(*scroll) || view <= 0.0f || !GuideShadersReady())
        {
            if (mStageS > GUIDE_MEASURE_TIMEOUT_S)
            {
                Fail(menu, FString::Printf(TEXT("%s: the scroll end did not settle (end %.1f, view %.1f)"),
                    *shot.FileName, scroll->GetScrollOffsetOfEnd(), view));
                return true;
            }
            return false;
        }
        const float end = scroll->GetScrollOffsetOfEnd();
        const float offset = scroll->GetScrollOffset();
        if (offset < end - SOL::Paths::PLAYER_GUIDE_SCROLL_TOLERANCE)
        {
            scroll->SetScrollOffset(FMath::Min(offset + view, end));
            ResetEndStable();
            return false;
        }

        // At the stable end: the pages it needs must match SOL::Paths' table; then back to the top for page 0
        mViewHeight = view;
        mScrollEnd = end;
        const int32 pages = SOLPlayerGuide::PageCount(mViewHeight, mScrollEnd);
        UE_LOG(LogSOL, Log, TEXT("PlayerGuide: tab %d view %.1f, scroll end %.1f: %d pages (table %d)"),
            static_cast<int32>(shot.Tab), mViewHeight, mScrollEnd, pages, SOLPlayerGuide::TabPages(shot.Tab));
        if (pages != SOLPlayerGuide::TabPages(shot.Tab))
        {
            Fail(menu, FString::Printf(TEXT("tab %d needs %d shots at this size (view %.1f, scroll end %.1f), "
                "SOL::Paths::PLAYER_GUIDE_TAB_PAGES says %d: update the table"), static_cast<int32>(shot.Tab),
                pages, mViewHeight, mScrollEnd, SOLPlayerGuide::TabPages(shot.Tab)));
            return true;
        }
        scroll->SetScrollOffset(0.0f);
        ResetEndStable();
        ToStage(EStage::Settle);
        return false;
    }
    case EStage::Settle:
    {
        // Settled: the tab checked, then a window screenshot with the UI
        if (mStageS < GUIDE_SHOT_SETTLE_S || mStageFrames < GUIDE_SHOT_SETTLE_FRAMES || !GuideShadersReady())
        {
            if (mStageS > GUIDE_OPEN_TIMEOUT_S)
            {
                Fail(menu, TEXT("shaders still compiling"));
                return true;
            }
            return false;
        }

        // The window still in front (a click elsewhere would change the shot): else pulled back, and the settle
        // frames counted again once it is
        if (!GuideWindowActive())
        {
            if (mStageS > GUIDE_OPEN_TIMEOUT_S)
            {
                Fail(menu, TEXT("the game window is not active (keep it in front while the guide is written)"));
                return true;
            }
            GuidePullToFront(mStageFrames);
            mStageFrames = 0;
            return false;
        }

        // The tab and the hovered key (none on a plain shot) as the shot wants them
        const FSOLPlayerGuideShot& shot = shots[mShot];
        const int32 wantHover = shot.bHover ? SOLKeyboardLayout::FindKey(GUIDE_HOVER_KEY) : INDEX_NONE;
        const int32 hovered = page != nullptr && page->GetDiagram() != nullptr ? page->GetDiagram()->GetHoveredKey()
            : INDEX_NONE;
        if (page == nullptr || page->GetSelectedTab() != static_cast<int32>(shot.Tab)
            || (shot.bHover && wantHover == INDEX_NONE) || hovered != wantHover)
        {
            Fail(menu, FString::Printf(TEXT("%s: tab %d or hovered key %d (want %d) not shown"), *shot.FileName,
                static_cast<int32>(shot.Tab), hovered, wantHover));
            return true;
        }

        // D46: the tab's end still the one Measure found and held still for a few frames (else the pages are stale)
        const UScrollBox* scroll = page->GetTabScroll(static_cast<int32>(shot.Tab));
        if (scroll == nullptr)
        {
            Fail(menu, FString::Printf(TEXT("%s: no scroll"), *shot.FileName));
            return true;
        }
        const float end = scroll->GetScrollOffsetOfEnd();
        const bool bSameEnd = FMath::Abs(end - mScrollEnd) <= SOL::Paths::PLAYER_GUIDE_SCROLL_TOLERANCE;
        if (!IsEndStable(*scroll) || !bSameEnd)
        {
            if (mStageS > GUIDE_OPEN_TIMEOUT_S)
            {
                Fail(menu, FString::Printf(TEXT("%s: scroll end %.1f, measured %.1f"), *shot.FileName, end,
                    mScrollEnd));
                return true;
            }
            return false;
        }
        // The requested offset (SScrollBox exposes no drawn offset, and a scrolled-out child's geometry is stale): a
        // sanity check that Show asked for this page, and that a tab's last scrolled page sits at its end; the images
        // themselves are looked at when regenerated
        const float wantOffset = SOLPlayerGuide::PageOffset(shot.Page, mViewHeight, mScrollEnd);
        const float offset = scroll->GetScrollOffset();
        const bool bLastPage = !shot.bHover && shot.Page > 0 && shot.Page == SOLPlayerGuide::TabPages(shot.Tab) - 1;
        if (FMath::Abs(offset - wantOffset) > SOL::Paths::PLAYER_GUIDE_SCROLL_TOLERANCE
            || (bLastPage && FMath::Abs(offset - end) > SOL::Paths::PLAYER_GUIDE_SCROLL_TOLERANCE))
        {
            Fail(menu, FString::Printf(TEXT("%s: scroll offset %.1f (want %.1f, end %.1f)"), *shot.FileName, offset,
                wantOffset, end));
            return true;
        }
        // The menu clock held at the shot's pulse phase until the capture is done (released in Captured)
        if (USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(menu.GetWorld()))
        {
            constexpr double period = SOL::SolidMenu::IDLE_PULSE_PERIOD_S;
            solid->PinClock((FMath::FloorToDouble(solid->GetClockS() / period) + 1.0 + GUIDE_PULSE_PHASE) * period);
        }
        GuideHoldMaterialTime();
        const FString path = SOLPlayerGuide::CapturePath(*shot.FileName);
        FScreenshotRequest::RequestScreenshot(path, true, false);
        UE_LOG(LogSOL, Log, TEXT("PlayerGuide: capture %s (offset %.1f, end %.1f)"), *path, offset, end);
        ToStage(EStage::Captured);
        return false;
    }
    case EStage::Captured:
        // The material time held on every frame the capture may be drawn in
        GuideHoldMaterialTime();
        if (mStageFrames < GUIDE_SHOT_FRAMES)
        {
            return false;
        }
        if (USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(menu.GetWorld()))
        {
            solid->ReleaseClock();
        }
        ++mShot;
        ToStage(mShot < shots.Num() ? EStage::Show : EStage::WaitFiles);
        return false;
    case EStage::WaitFiles:
    {
        // Every capture written, then the guide and the images put in place
        if (!AreCapturesWritten())
        {
            if (mStageS > GUIDE_FILES_TIMEOUT_S)
            {
                Fail(menu, TEXT("a capture was not written"));
                return true;
            }
            return false;
        }
        FString reason;
        if (!WriteOutputs(reason))
        {
            Fail(menu, reason);
            return true;
        }
        Finish(menu);
        return true;
    }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Holds the sim clock at the pinned date and the menu camera's sway at its base azimuth (33e-iv), every frame
void FSOLPlayerGuideWriter::HoldScene(const USOLMenuSubsystem& menu) const
{
    if (USOLSimClockSubsystem* clock = menu.GetWorld() != nullptr
        ? menu.GetWorld()->GetSubsystem<USOLSimClockSubsystem>() : nullptr)
    {
        clock->SetJulianDate(mPinnedJulianDate);
    }
    if (ASOLMenuCameraPawn* camera = mController.IsValid() ? mController->GetPawn<ASOLMenuCameraPawn>() : nullptr)
    {
        camera->SetSwayPinned(true);
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves to a stage, its time and frames counted from now
void FSOLPlayerGuideWriter::ToStage(const EStage stage)
{
    mStage = stage;
    mStageS = 0.0;
    mStageFrames = 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the game viewport's size in pixels
FIntPoint FSOLPlayerGuideWriter::GetViewSize() const
{
    FIntPoint size = FIntPoint::ZeroValue;
    if (mController.IsValid())
    {
        mController->GetViewportSize(size.X, size.Y);
    }
    return size;
}

//////////////////////////////////////////////////////////////////////////
// Parks the cursor in the view's top-right corner and sends Slate a mouse move there, so no control is hovered
void FSOLPlayerGuideWriter::ParkCursor() const
{
    const FIntPoint size = GetViewSize();
    if (mController.IsValid() && size.X > GUIDE_CURSOR_INSET_PX)
    {
        mController->SetMouseLocation(size.X - GUIDE_CURSOR_INSET_PX, GUIDE_CURSOR_INSET_PX);
    }
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication& slate = FSlateApplication::Get();
        const FVector2D at = slate.GetCursorPos();
        slate.ProcessMouseMoveEvent(FPointerEvent(slate.GetUserIndexForMouse(),
            FSlateApplicationBase::CursorPointerIndex, at, at, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f,
            FModifierKeysState()));
    }
}

//////////////////////////////////////////////////////////////////////////
// Counts the frames the scroll's end has stayed put; returns whether it has held still long enough
bool FSOLPlayerGuideWriter::IsEndStable(const UScrollBox& scroll)
{
    const float end = scroll.GetScrollOffsetOfEnd();
    mEndStableFrames = mLastEnd >= 0.0f && FMath::Abs(end - mLastEnd) <= SOL::Paths::PLAYER_GUIDE_SCROLL_TOLERANCE
        ? mEndStableFrames + 1 : 0;
    mLastEnd = end;
    return mEndStableFrames >= GUIDE_END_STABLE_FRAMES;
}

//////////////////////////////////////////////////////////////////////////
// Forgets the end stability count
void FSOLPlayerGuideWriter::ResetEndStable()
{
    mLastEnd = -1.0f;
    mEndStableFrames = 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether every capture is on disk, not empty and the same size as at the last check
bool FSOLPlayerGuideWriter::AreCapturesWritten()
{
    if (mStageS - mLastFileCheckS < GUIDE_FILE_CHECK_S && mStageFrames > 1)
    {
        return false;
    }
    mLastFileCheckS = mStageS;
    bool bAll = true;
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    for (int32 index = 0; index < shots.Num(); ++index)
    {
        const int64 size = IFileManager::Get().FileSize(*SOLPlayerGuide::CapturePath(*shots[index].FileName));
        bAll &= size > 0 && size == mCaptureSizes[index];
        mCaptureSizes[index] = size;
    }
    return bAll;
}

//////////////////////////////////////////////////////////////////////////
// Compares every capture with its image: one within the capture noise keeps the image untouched (no new bytes for
// Git LFS), the others are staged next to theirs. Then writes the guide and swaps the staged images in: a failure
// before the swap leaves the repository's guide and images as they were (the staged copies removed)
bool FSOLPlayerGuideWriter::WriteOutputs(FString& outReason) const
{
    IFileManager& files = IFileManager::Get();
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    const FSOLImageNoise noise = SOLPlayerGuide::HelpShotNoise();
    TArray<bool> changed;       // The capture differs from its image (staged, to be swapped in)
    changed.Init(false, shots.Num());
    // Removes the staged copies left
    const auto dropStaged = [&files, &shots]()
    {
        for (const FSOLPlayerGuideShot& shot : shots)
        {
            files.Delete(*(SOLPlayerGuide::ImagePath(*shot.FileName) + GUIDE_STAGED_SUFFIX), false, true, true);
        }
    };

    // Each capture loaded and compared with its image: kept, or copied next to its target (the same folder, so the
    // swap is a rename); a staged copy a crashed run left goes first
    dropStaged();
    FImage capture;
    for (int32 index = 0; index < shots.Num(); ++index)
    {
        const FSOLPlayerGuideShot& shot = shots[index];
        const FString from = SOLPlayerGuide::CapturePath(*shot.FileName);
        const FString image = SOLPlayerGuide::ImagePath(*shot.FileName);
        if (!FImageUtils::LoadImage(*from, capture))
        {
            dropStaged();
            outReason = FString::Printf(TEXT("could not read the capture %s"), *from);
            return false;
        }
        capture.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
        const FSOLImageDiff diff = SOLPlayerGuide::CompareWithFile(capture, image, noise);
        if (SOLPlayerGuide::IsCaptureNoise(diff, noise))
        {
            UE_LOG(LogSOL, Log, TEXT("PlayerGuide KEPT %s (max delta %d, %d pixels above %d)"),
                *FPaths::ConvertRelativePathToFull(image), diff.MaxDelta, diff.NoisyPixels, noise.NoiseDelta);
            continue;
        }
        UE_LOG(LogSOL, Log, TEXT("PlayerGuide: %s changed (comparable %d, max delta %d, %d pixels above %d)"),
            *shot.FileName, diff.bComparable ? 1 : 0, diff.MaxDelta, diff.NoisyPixels, noise.NoiseDelta);
        const FString staged = image + GUIDE_STAGED_SUFFIX;
        if (files.Copy(*staged, *from, true, true) != COPY_OK)
        {
            dropStaged();
            outReason = FString::Printf(TEXT("could not copy %s to %s"), *from, *staged);
            return false;
        }
        changed[index] = true;
    }
    const FString guidePath = SOLPlayerGuide::GuideFilePath();
    if (!SOLPlayerGuide::WriteGuideFile(guidePath, SOLPlayerGuide::BuildMarkdown(SOLKeyBindings::DefaultKeys())))
    {
        dropStaged();
        outReason = FString::Printf(TEXT("could not write %s"), *guidePath);
        return false;
    }
    UE_LOG(LogSOL, Log, TEXT("PlayerGuide WROTE %s (%lld bytes)"), *FPaths::ConvertRelativePathToFull(guidePath),
        files.FileSize(*guidePath));

    // The swap: each changed image's staged copy renamed over it; every capture removed
    for (int32 index = 0; index < shots.Num(); ++index)
    {
        const FSOLPlayerGuideShot& shot = shots[index];
        const FString to = SOLPlayerGuide::ImagePath(*shot.FileName);
        if (changed[index])
        {
            if (!files.Move(*to, *(to + GUIDE_STAGED_SUFFIX), true, true))
            {
                dropStaged();
                outReason = FString::Printf(TEXT("could not rename the staged image over %s"), *to);
                return false;
            }
            UE_LOG(LogSOL, Log, TEXT("PlayerGuide WROTE %s (%lld bytes)"), *FPaths::ConvertRelativePathToFull(to),
                files.FileSize(*to));
        }
        files.Delete(*SOLPlayerGuide::CapturePath(*shot.FileName), false, true, true);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Logs the failure and quits
void FSOLPlayerGuideWriter::Fail(USOLMenuSubsystem& /*menu*/, const FString& reason)
{
    UE_LOG(LogSOL, Error, TEXT("PlayerGuide FAIL %s"), *reason);
    mIsFinished = true;
    GuideQuit(GUIDE_EXIT_FAILED);
}

//////////////////////////////////////////////////////////////////////////
// Logs the end and quits
void FSOLPlayerGuideWriter::Finish(USOLMenuSubsystem& /*menu*/)
{
    UE_LOG(LogSOL, Log, TEXT("PlayerGuide DONE the guide and %d images in %.1f s"), SOLPlayerGuide::Shots().Num(),
        mRunS);
    mIsFinished = true;
    GuideQuit(0);
}

#endif
