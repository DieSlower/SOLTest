/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class ASOLShipPawn;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMapModeSubsystem;

/**
 * Verification-only scripted destination-pick run (-SOLSmokeMapPick, SDD 3 sub-part 2d): drives the REAL input
 * pipeline by injecting key, mouse-button and mouse-delta events into the player controller, as -SOLSmokeMap does,
 * and sets the map cursor through the pawn's HandleMapCursorMoved (the OS mouse is ignored for the run). It opens the
 * map with J, picks the most isolated body icon on screen, clicks it (reference = that body), drags the planar offset
 * (screenshot of the disc mid-drag), releases (the locked XY point must project back onto the cursor), holds Shift and
 * moves the cursor up then partly back (height preview, reversible; screenshot of the guide line), Shift+clicks (height
 * locked, destination = reference + offsets, tracked live; screenshot of the marker), presses X (pick cleared), then
 * clicks empty space (reference = ship), drags and releases. Enter is not pressed here: since 2e it closes the map and
 * jumps, which -SOLSmokeJump (FSOLJumpSmoke) covers. It then repeats one drag on the OS-cursor path (the scripted
 * cursor off): the real OS cursor is placed with SetMouseLocation, the left button pressed, mouse deltas injected
 * while the OS cursor stays put (as the viewport's capture pins it), and on release the OS cursor is put back at the
 * drag start (as the viewport does when the capture ends) with Shift held; the OS cursor must end on the map cursor,
 * the height preview must be anchored there, and a following OS move must move the map cursor by exactly that move (no
 * jump). Finally it closes the map with J (pick cleared) and reopens it (still cleared). Each check logs a LogSOL
 * PASS/FAIL line; the run then quits.
 */
class SOLTEST_API FSOLMapPickSmoke
{
public:

    // Caches the controller and subsystems and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLMapPickPhase : uint8
    {
        Setup,
        OpenJ,
        SettleWide,
        ClickBody,
        DragBody,
        ShotDisc,
        ReleaseBody,
        ShiftUp,
        ShotGuide,
        ShiftBack,
        ShiftClick,
        ShotMarker,
        ClearX,
        ClickEmpty,
        DragEmpty,
        ReleaseEmpty,
        OsCursorPlace,
        OsCursorPress,
        OsCursorDrag,
        OsCursorRelease,
        OsCursorMove,
        CloseJ,
        ReopenJ,
        CloseAgain,
        Finish,
        Done,
    };

    // Runs a phase's entry actions (key presses, cursor moves)
    void EnterPhase(ESOLMapPickPhase phase, ASOLShipPawn& pawn);

    // Runs a phase's per-frame actions (mouse deltas, cursor moves)
    void TickPhase(ESOLMapPickPhase phase, ASOLShipPawn& pawn);

    // Runs a phase's exit checks and releases held keys
    void ExitPhase(ESOLMapPickPhase phase, ASOLShipPawn& pawn);

    // Returns the minimum real duration of a phase in seconds
    static double GetPhaseDuration(ESOLMapPickPhase phase);

    // Returns the minimum number of frames a phase lasts
    static int32 GetPhaseMinFrames(ESOLMapPickPhase phase);

    // Picks the most isolated on-screen body icon and the emptiest screen point from the body overlay
    void ChooseTargets();

    // Checks the planar lock: offsets locked, the cursor moved by the drag, and the XY point projecting onto the cursor
    void CheckPlanarLock(const TCHAR* label, const ASOLShipPawn& pawn, const FVector2D& expectedCursorPx);

    // Returns true when the pick state is back to "no reference" with no preview
    bool IsPickCleared() const;

    // Projects a universe point to the screen as the HUD does (camera POV, bodies' render placement); false if behind
    bool ProjectToScreen(const FVector3d& pointM, FVector2D& outScreen) const;

    // Reads the OS mouse position in viewport pixels; false when the viewport has none
    bool GetOsCursorPx(FVector2D& outCursorPx) const;

    // Moves the real OS cursor to a viewport pixel position
    void SetOsCursorPx(const FVector2D& cursorPx) const;

    // Takes a verification screenshot and logs its name
    void Screenshot(const ASOLShipPawn& pawn, const TCHAR* name) const;

    // Sends a key press to the player controller
    void PressKey(const FKey& key) const;

    // Sends a key release to the player controller
    void ReleaseKey(const FKey& key) const;

    // Presses a key now and releases it on the next frame
    void TapKey(const FKey& key);

    // Sends one analog axis sample (mouse delta in pixels) to the player controller
    void InjectAxis(const FKey& key, float delta) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;          // Controller the events are injected into
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;          // Map camera and pick state
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;            // Observer (ship) and render placement
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;    // Body names and positions
    TArray<FKey, TInlineAllocator<4>> mPendingReleases;     // Tapped keys to release on the next frame
    ESOLMapPickPhase mPhase = ESOLMapPickPhase::Setup;      // Current phase
    FVector2D mBodyScreenPx = FVector2D::ZeroVector;        // Screen position of the body the script clicks
    FVector2D mEmptyScreenPx = FVector2D::ZeroVector;       // Screen point far from every body icon
    FVector2D mDragStartPx = FVector2D::ZeroVector;         // Map cursor when a drag started
    FVector2D mOsReleaseCursorPx = FVector2D::ZeroVector;   // Map cursor after the OS-cursor drag's release
    FVector3d mReferenceAtLockM = FVector3d::ZeroVector;    // Reference position when the height was locked
    double mHeightAnchorPx = 0.0;                           // Cursor Y when the height preview started
    double mLastHeightPreviewM = 0.0;                       // Height preview of the previous frame
    double mPhaseTimeS = 0.0;                               // Real time in the current phase
    double mTotalTimeS = 0.0;                               // Real time since the script started
    int32 mBodyIndex = INDEX_NONE;                          // Body the script clicks
    int32 mPhaseFrames = 0;                                 // Frames in the current phase
    int32 mChecksPassed = 0;                                // Checks that passed
    int32 mChecksRun = 0;                                   // Checks evaluated
    bool mIsHeightMonotonic = true;                         // Height preview grew with every upward cursor step
};
