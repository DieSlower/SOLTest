/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class APlayerController;
class UScrollBox;
class USOLMenuSubsystem;

/**
 * The -SOLWritePlayerGuide run (SDD 33 section 9, D8, D24; editor and development builds only). Started by
 * USOLMenuSubsystem in the main menu, one step per frame, on the smoke settings slot (the default keys) with the
 * engine settings snapshot (the player's GameUserSettings.ini is given back). It opens Help, shows each
 * SOLPlayerGuide::Shots() entry (the Keyboard tab, the Keyboard tab with a key hovered, each glossary tab), waits for
 * it to settle with the cursor parked (a tab's first page first steps through the tab until its scroll end stops
 * moving, D46, so the last page reaches the tab's end), and captures a window screenshot with the UI; once every
 * capture is on disk it writes SOLPlayerGuide::BuildMarkdown(DefaultKeys()) to Docs/PLAYER_GUIDE.md (UTF-8 without a
 * BOM, LF) and moves the captures into Docs/Images/ (staged next to them first, so a failure before the final renames
 * leaves the old guide and images). It logs "PlayerGuide WROTE <path>" per file and "PlayerGuide DONE", or
 * "PlayerGuide FAIL <reason>", then quits (exit code 1 on a failure); Tools\WritePlayerGuide.bat reads those lines.
 */
class SOLTEST_API FSOLPlayerGuideWriter
{
public:

    // Caches the controller and starts waiting for the main menu
    void Start(USOLMenuSubsystem& menu);

    // Runs one frame of the run; returns true once it has finished (and requested the quit)
    bool Update(USOLMenuSubsystem& menu);

private:

    // The run's stages, in order
    enum class EStage : uint8
    {
        WaitMenu,       // The main menu shown and the view at its size
        WaitOpen,       // Help opened and its shell open
        Show,           // The next shot's tab (and hover) shown
        Measure,        // A tab's first page: scrolled down a view at a time until its end stops moving, measured
        Settle,         // ... settled, then captured
        Captured,       // A few frames so the capture shows this shot
        WaitFiles,      // Every capture on disk, then the outputs written
    };

    // Moves to a stage, its time and frames counted from now
    void ToStage(EStage stage);

    // Returns the game viewport's size in pixels (zero without a controller)
    FIntPoint GetViewSize() const;

    // Parks the cursor in the view's top-right corner (off every control)
    void ParkCursor() const;

    // Counts the frames a scroll's end has not moved (the descriptions re-wrap once first painted); returns whether
    // it has held still for GUIDE_END_STABLE_FRAMES
    bool IsEndStable(const UScrollBox& scroll);

    // Forgets the end stability count (a new tab or a new offset)
    void ResetEndStable();

    // Returns whether every capture of the run is on disk, not empty and no longer growing
    bool AreCapturesWritten();

    // Writes the guide and moves the captures into place; returns false (with a reason) on the first failure
    bool WriteOutputs(FString& outReason) const;

    // Logs the failure and quits; nothing more is written
    void Fail(USOLMenuSubsystem& menu, const FString& reason);

    // Logs the end and quits
    void Finish(USOLMenuSubsystem& menu);

    TWeakObjectPtr<APlayerController> mController;     // The player's controller (the view size, the cursor)
    TArray<int64> mCaptureSizes;                       // Each capture's size at the last check (-1: not there)
    EStage mStage = EStage::WaitMenu;                  // Current stage
    int32 mShot = 0;                                   // Index in SOLPlayerGuide::Shots() of the shot in progress
    int32 mStageFrames = 0;                            // Frames since the stage began
    int32 mViewFrames = 0;                             // Frames in a row at the wanted view size
    double mStageS = 0.0;                              // Seconds since the stage began
    double mRunS = 0.0;                                // Seconds since the run began (the watchdog)
    double mLastTimeS = 0.0;                           // Platform time of the last update
    double mLastFileCheckS = 0.0;                      // Stage time of the last capture size check
    float mViewHeight = 0.0f;                          // D46: the shown tab's scroll view height and largest offset,
    float mScrollEnd = 0.0f;                           // measured on its first page (menu units)
    float mLastEnd = -1.0f;                            // The shown tab's scroll end on the last frame
    int32 mEndStableFrames = 0;                        // Frames in a row that end has not moved
    bool mIsFinished = false;                          // The quit has been requested
};
