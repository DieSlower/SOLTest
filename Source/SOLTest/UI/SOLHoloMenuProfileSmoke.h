/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLActionTable.h"

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "InputCoreTypes.h"
#include "Misc/OutputDevice.h"

class APlayerController;
class ASOLFlightHud;
class ASOLShipPawn;
class USOLInteractionSubsystem;
class USOLShipSubsystem;
class USOLTargetingSubsystem;

/**
 * Verification-only cost capture of the in-flight menus and cards (-SOLSmokeHoloMenuProfile, SDD 42 section 11, D14),
 * in a standalone -game run at 1280x720. The scene is fixed: the sim date is pinned (PROFILE_SIM_JD in the .cpp) and
 * the ship is placed at the default start above Earth's Sun-facing side for that date, at rest in its frame, facing Earth;
 * the menus and cards work on one named body (PROFILE_BODY_NAME), whose icon must be on screen. It runs five passes:
 * P0 flight with no menu open (no target, no info card), then, with the body targeted and its info card shown, P1 the
 * radial menu held open over the body's icon, P2 the Ctrl+O Overlay Menu, P3 the F3 speed panel and P4 the map's list
 * menu, each opened through the same input paths as the other smokes. Each pass waits for the menu to settle, takes a
 * window screenshot (Saved/Screenshots/WindowsEditor/HoloMenuProfile_P<n>.png from the editor binary), then three
 * ProfileGPU frames (r.ProfileGPU.ShowUI 0, so no visualizer window joins the UI pass) and three
 * `stat dumpave -num=240 -ms=0` windows rooted at the HUD Time, Slate "Draw Window And Children Time" and "Game UI
 * Paint" stats; the info card is refreshed and given its open time before each capture. The numbers are read back from
 * the log by this object (an output device) and logged as one line per pass:
 * "HoloMenuProfile RESULT P<n> <PASS|FAIL|NOBASE> key=value ...", compared against the pre-conversion baseline in the
 * .cpp (budget: under 0.1 ms extra CPU and GPU per pass), and a final "HoloMenuProfile SUMMARY" line; then it quits.
 */
class SOLTEST_API FSOLHoloMenuProfileSmoke : public FOutputDevice
{
public:

    // Stops listening to the log
    virtual ~FSOLHoloMenuProfileSmoke() override;

    // Caches the controller, HUD and subsystems, starts listening to the log and starts the first step
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

    // Reads the stat dump and GPU profile lines this script asked for (any thread)
    virtual void Serialize(const TCHAR* text, ELogVerbosity::Type verbosity, const FName& category) override;

    // The log may call in from any thread; Serialize locks
    virtual bool CanBeUsedOnAnyThread() const override { return true; }

private:

    // Where a capture pass is
    enum class EPassPhase : uint8
    {
        Settle,         // The menu opens and its state settles
        Shot,           // A window screenshot, then a few frames for it
        Gpu,            // ProfileGPU frames
        Cpu,            // stat dumpave windows, one per root stat
        Done,
    };

    // One ProfileGPU frame's UI numbers (ms; -1 when the event was not in the profile)
    struct FGpuCapture
    {
        double UiMs = -1.0;         // "SlateUI Title = <game window>" inclusive: the game window's Slate pass
        double SlateMs = -1.0;      // "RenderGraphExecute - Slate" inclusive
        double ShapesMs = 0.0;      // Sum of the "Slate Material: MID_M_SOLHudShape*" draws
        double FrameMs = -1.0;      // <root> inclusive
        int32 UiDraws = 0;          // Draws in the game window's Slate pass
    };

    // Runs the step's action at its start
    void EnterStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's per-frame work; returns true once the step is done
    bool TickStep(int32 step, ASOLShipPawn& pawn, float realDeltaSeconds);

    // Runs the step's checks at its end
    void ExitStep(int32 step, ASOLShipPawn& pawn);

    // Runs one frame of a capture pass; returns true once its result line is logged
    bool TickPass(int32 pass, ASOLShipPawn& pawn, float realDeltaSeconds);

    // Returns whether the pass's menu is open (P0: no menu open)
    bool IsPassMenuOpen(int32 pass, const ASOLShipPawn& pawn) const;

    // Shows the info card again (the same Execute the radial and list menus run), so it outlives a long pass
    void RefreshInfoCard() const;

    // Refreshes the info card once, then returns true once its open time (plus a margin) of real time has passed
    bool RefreshAndWait();

    // Places the ship at the default start above Earth for the pinned date, at rest, facing Earth; false if no Earth
    bool PlaceShipAtStart(UWorld* world) const;

    // Logs the pass's result line with its verdict against the baseline
    void LogPassResult(int32 pass, const ASOLShipPawn& pawn);

    // Finds the icon hit of the named profile body; false if its icon is not drawn
    bool FindBodyHit(int32 bodyIndex);

    // Returns the current screen centre of the body's icon (the last one found when it is not drawn now)
    FVector2D GetBodyIconPx() const;

    // Presses Fire on the body's icon (opens the radial menu)
    void PressOnBody(ASOLShipPawn& pawn) const;

    // Moves the cursor onto an action's sector of the open radial menu; false when the menu does not offer it
    bool DragToAction(ASOLShipPawn& pawn, ESOLAction action) const;

    // Runs a radial action on the body: press on the icon, drag to the sector, release
    void RadialAction(ASOLShipPawn& pawn, ESOLAction action) const;

    // Sends a key press to the player controller (game input)
    void PressKey(const FKey& key) const;

    // Sends a key release to the player controller (game input)
    void ReleaseKey(const FKey& key) const;

    // Presses a game key now and releases it on the next frame
    void TapKey(const FKey& key);

    // Sends a key press and release through Slate to the focused widget
    static void SlateKey(const FKey& key);

    // Runs a console command in the game world
    static void RunCommand(const TCHAR* command, UWorld* world);

    // Requests a window screenshot with the UI to the screenshot folder (Saved/Screenshots/<platform>)
    static void TakeUiShot(const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    // Parses one ProfileGPU table row into the capture being read (called with the lock held)
    void ParseGpuRow(const TCHAR* text);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLInteractionSubsystem> mInteraction;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLTargetingSubsystem> mTargeting;
    TArray<FKey> mPendingReleases;                          // Game keys released on the next frame
    FSOLObjectRef mBody;                                    // The body the menus and cards work on
    FVector2D mBodyPx = FVector2D::ZeroVector;              // Its icon centre when found
    double mStepTimeS = 0.0;
    double mCardRefreshAtS = -1.0;                          // Real time of the last card refresh; < 0: none pending
    double mPhaseTimeS = 0.0;                               // Time in the pass phase
    double mCpuMs[3] = { -1.0, -1.0, -1.0 };                // This pass: HUD Time, Draw Window, Game UI Paint
    double mWindowTimeS = 0.0;                              // Real time over the stat dumpave windows
    int32 mWindowFrames = 0;                                // Frames over the stat dumpave windows
    int32 mStep = 0;
    int32 mStepFrames = 0;
    int32 mPhaseFrames = 0;                                 // Frames in the pass phase
    int32 mCpuIndex = 0;                                    // Root stat of the stat dumpave window running
    int32 mGpuRequested = 0;                                // ProfileGPU commands run in this pass
    int32 mGpuFirst = 0;                                    // Index in mGpuCaptures of this pass's first capture
    int32 mGpuLastFrame = 0;                                // Phase frame of the last ProfileGPU command
    int32 mCpuStartFrame = INDEX_NONE;                      // Phase frame the running stat dumpave started; none yet
    int32 mMenuFrames = 0;                                  // Capture frames this pass, and those with the menu open
    int32 mMenuOpenFrames = 0;
    int32 mExecutedBefore = 0;
    int32 mCancelsBefore = 0;
    int32 mPassesFailed = 0;
    int32 mPassesNoBase = 0;
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
    EPassPhase mPhase = EPassPhase::Settle;
    bool mIsListening = false;                              // Registered with GLog
    bool mIsShipPlaced = false;                             // Setup placed the ship for the pinned date
    bool mIsBodyFound = false;                              // The named body's icon was on screen at the end of Setup

    // Read from the log (any thread), under mLogLock
    FCriticalSection mLogLock;
    TArray<FGpuCapture> mGpuCaptures;                       // Finished ProfileGPU frames, in order
    FGpuCapture mGpuReading;                                // The one being read
    FString mCpuToken;                                      // " - <stat> - " of the stat dumpave asked for
    double mCpuReadMs = -1.0;                               // Its inclusive average, summed over its root lines
    bool mIsGpuReading = false;
    bool mIsCpuArmed = false;                               // Waiting for the dump's header
    bool mIsCpuReading = false;                             // Header seen, reading its lines
};
