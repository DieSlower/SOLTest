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
#include "Menu/SOLMenuState.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLPlayerGuide.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "SOLTest.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "HAL/FileManager.h"
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
    constexpr double GUIDE_FILE_CHECK_S = 0.25;             // The capture size check's period
    constexpr double GUIDE_FILES_TIMEOUT_S = 20.0;          // The captures-on-disk wait's limit
    constexpr double GUIDE_WATCHDOG_S = 120.0;              // A stuck run fails and quits
    constexpr int32 GUIDE_CURSOR_INSET_PX = 8;              // The parked cursor, this far in from the top right
    const TCHAR* const GUIDE_HOVER_KEY = TEXT("O");         // The hover shot's key (chords and areas in its strip)
    constexpr int32 GUIDE_MAX_WINDOW_PX = 4096;             // The window may grow past a 1080p desktop's work area
    constexpr const TCHAR* GUIDE_STAGED_SUFFIX = TEXT(".new"); // An image copied next to its target before the swap
    constexpr uint8 GUIDE_EXIT_FAILED = 1;                  // The process exit code of a failed run

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
    UE_LOG(LogSOL, Log, TEXT("PlayerGuide: writing %s and %d images"), *SOLPlayerGuide::GuideFilePath(),
        SOLPlayerGuide::Shots().Num());
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
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    USOLMainMenuWidget* mainMenu = menu.GetMainMenu();
    USOLReadOnlyPageWidget* page = GuideHelpPage(menu);
    switch (mStage)
    {
    case EStage::WaitMenu:
    {
        // The main menu shown and the view at the shots' size for a few frames, then Help opened as a click does
        const FIntPoint view = GetViewSize();
        mViewFrames = view == GUIDE_VIEW && menu.IsMenuShowing() && mainMenu != nullptr && GuideShadersReady()
            ? mViewFrames + 1 : 0;
        if (mViewFrames < GUIDE_VIEW_STABLE_FRAMES)
        {
            if (mStageS > GUIDE_VIEW_TIMEOUT_S)
            {
                Fail(menu, FString::Printf(TEXT("no main menu at %dx%d (view %dx%d, menu %d, shaders ready %d)"),
                    GUIDE_VIEW.X, GUIDE_VIEW.Y, view.X, view.Y, mainMenu != nullptr ? 1 : 0,
                    GuideShadersReady() ? 1 : 0));
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
        ParkCursor();
        const FString capture = SOLPlayerGuide::CapturePath(shot.FileName);
        if (!IFileManager::Get().Delete(*capture, false, true, true) && IFileManager::Get().FileExists(*capture))
        {
            Fail(menu, FString::Printf(TEXT("could not remove the old capture %s"), *capture));
            return true;
        }
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

        // The tab and the hovered key (none on a plain shot) as the shot wants them
        const FSOLPlayerGuideShot& shot = shots[mShot];
        const int32 wantHover = shot.bHover ? SOLKeyboardLayout::FindKey(GUIDE_HOVER_KEY) : INDEX_NONE;
        const int32 hovered = page != nullptr && page->GetDiagram() != nullptr ? page->GetDiagram()->GetHoveredKey()
            : INDEX_NONE;
        if (page == nullptr || page->GetSelectedTab() != static_cast<int32>(shot.Tab)
            || (shot.bHover && wantHover == INDEX_NONE) || hovered != wantHover)
        {
            Fail(menu, FString::Printf(TEXT("%s: tab %d or hovered key %d (want %d) not shown"), shot.FileName,
                static_cast<int32>(shot.Tab), hovered, wantHover));
            return true;
        }
        const FString path = SOLPlayerGuide::CapturePath(shot.FileName);
        FScreenshotRequest::RequestScreenshot(path, true, false);
        UE_LOG(LogSOL, Log, TEXT("PlayerGuide: capture %s"), *path);
        ToStage(EStage::Captured);
        return false;
    }
    case EStage::Captured:
        if (mStageFrames < GUIDE_SHOT_FRAMES)
        {
            return false;
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
        const int64 size = IFileManager::Get().FileSize(*SOLPlayerGuide::CapturePath(shots[index].FileName));
        bAll &= size > 0 && size == mCaptureSizes[index];
        mCaptureSizes[index] = size;
    }
    return bAll;
}

//////////////////////////////////////////////////////////////////////////
// Stages every capture next to its image, writes the guide, then swaps the staged images in: a failure before the
// swap leaves the repository's guide and images as they were (the staged copies removed)
bool FSOLPlayerGuideWriter::WriteOutputs(FString& outReason) const
{
    IFileManager& files = IFileManager::Get();
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    // Removes the staged copies left
    const auto dropStaged = [&files, &shots]()
    {
        for (const FSOLPlayerGuideShot& shot : shots)
        {
            files.Delete(*(SOLPlayerGuide::ImagePath(shot.FileName) + GUIDE_STAGED_SUFFIX), false, true, true);
        }
    };

    // Each capture copied next to its target (the same folder, so the swap is a rename)
    for (const FSOLPlayerGuideShot& shot : shots)
    {
        const FString from = SOLPlayerGuide::CapturePath(shot.FileName);
        const FString staged = SOLPlayerGuide::ImagePath(shot.FileName) + GUIDE_STAGED_SUFFIX;
        if (files.Copy(*staged, *from, true, true) != COPY_OK)
        {
            dropStaged();
            outReason = FString::Printf(TEXT("could not copy %s to %s"), *from, *staged);
            return false;
        }
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

    // The swap: each staged image renamed over its target, then its capture removed
    for (const FSOLPlayerGuideShot& shot : shots)
    {
        const FString to = SOLPlayerGuide::ImagePath(shot.FileName);
        if (!files.Move(*to, *(to + GUIDE_STAGED_SUFFIX), true, true))
        {
            dropStaged();
            outReason = FString::Printf(TEXT("could not rename the staged image over %s"), *to);
            return false;
        }
        files.Delete(*SOLPlayerGuide::CapturePath(shot.FileName), false, true, true);
        UE_LOG(LogSOL, Log, TEXT("PlayerGuide WROTE %s (%lld bytes)"), *FPaths::ConvertRelativePathToFull(to),
            files.FileSize(*to));
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
