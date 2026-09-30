/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapPickSmoke.h"

#include "Game/SOLGameMode.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLMapPicking.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"

namespace
{
    // Phase durations (real seconds) and frame counts
    constexpr double PICK_SMOKE_SETUP_S = 1.0;
    constexpr double PICK_SMOKE_TAP_PHASE_S = 0.5;          // Long enough for a tapped key to take effect
    constexpr double PICK_SMOKE_SETTLE_WIDE_S = 1.0;        // The HUD draws the body overlay a few times first
    constexpr double PICK_SMOKE_SHOT_SETTLE_S = 2.0;        // Frames rendered before a screenshot
    constexpr double PICK_SMOKE_FINISH_S = 1.0;
    constexpr int32 PICK_SMOKE_DRAG_FRAMES = 10;            // Frames of mouse motion per drag
    constexpr int32 PICK_SMOKE_SETTLE_FRAMES = 5;           // Frames for the last injected event to be processed
    constexpr int32 PICK_SMOKE_HEIGHT_FIRST_FRAME = 2;      // Shift is processed on frame 1; the cursor moves from 2
    constexpr int32 PICK_SMOKE_HEIGHT_UP_FRAMES = 10;       // Upward cursor steps of the height preview
    constexpr int32 PICK_SMOKE_HEIGHT_BACK_FRAMES = 2;      // Downward cursor steps (partial retrace)

    // Drags (pixels per frame, X right / Y up as mouse deltas) and height cursor steps (screen pixels, Y down)
    constexpr float PICK_SMOKE_BODY_DRAG_X_PX = 8.0f;       // Body drag: 80 px right, 50 px up
    constexpr float PICK_SMOKE_BODY_DRAG_Y_PX = 5.0f;
    constexpr float PICK_SMOKE_EMPTY_DRAG_X_PX = -6.0f;     // Empty-space drag: 60 px left, 40 px down
    constexpr float PICK_SMOKE_EMPTY_DRAG_Y_PX = -4.0f;
    constexpr double PICK_SMOKE_HEIGHT_UP_STEP_PX = -6.0;   // 60 px up in all
    constexpr double PICK_SMOKE_HEIGHT_BACK_STEP_PX = 10.0; // then 20 px back down: 40 px up net

    // OS-cursor drag (scripted cursor off): mouse deltas per frame (X right / Y up), then one OS move after the
    // release (screen pixels, Y down). OS readings are whole pixels, hence the tolerance
    constexpr float PICK_SMOKE_OS_DRAG_X_PX = 7.0f;         // 70 px right, 40 px up
    constexpr float PICK_SMOKE_OS_DRAG_Y_PX = 4.0f;
    const FVector2D PICK_SMOKE_OS_MOVE_PX(15.0, -10.0);     // 15 px right, 10 px up
    constexpr double PICK_SMOKE_OS_CURSOR_TOLERANCE_PX = 1.0;

    // Target choice: the clicked body and the empty point stay this far from the screen edges
    constexpr double PICK_SMOKE_SCREEN_MARGIN_PX = 150.0;
    constexpr double PICK_SMOKE_EMPTY_GRID_STEP_PX = 20.0;

    // Verdict tolerances
    constexpr double PICK_SMOKE_DRAG_RELATIVE_TOLERANCE = 0.02; // Mouse units -> pixels round trip through the config
    constexpr double PICK_SMOKE_DRAG_ABSOLUTE_TOLERANCE_PX = 0.5;
    constexpr double PICK_SMOKE_PROJECT_TOLERANCE_PX = 1.5;     // Locked XY point re-projected onto the cursor
    constexpr double PICK_SMOKE_HEIGHT_RELATIVE_TOLERANCE = 1.0e-9;
    constexpr double PICK_SMOKE_COMPOSE_TOLERANCE_M = 1.0e-2;   // Double rounding at ~1e12 m coordinates
    constexpr double PICK_SMOKE_LIVE_TOLERANCE_M = 1.0e6;       // Reference motion over a few frames (< 50 km/s)
    constexpr double PICK_SMOKE_SHIP_REFERENCE_TOLERANCE_M = 1.0;
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and subsystems and starts the first phase
void FSOLMapPickSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();

    // The script places the map cursor itself; the real mouse must not move it
    pawn.SetMapCursorScripted(true);
    UE_LOG(LogSOL, Log, TEXT("SmokeMapPick: scripted destination-pick input starting (controller %s)"),
        mController.IsValid() ? *mController->GetName() : TEXT("none"));
    mPhase = ESOLMapPickPhase::Setup;
    EnterPhase(mPhase, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLMapPickSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLMapPickPhase::Done)
    {
        return true;
    }
    if (!mController.IsValid() || !mMapMode.IsValid() || !mAnchor.IsValid() || !mRegistry.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeMapPick: controller or subsystems missing; aborting"));
        mPhase = ESOLMapPickPhase::Done;
        return true;
    }

    // Keys tapped last frame are released now, so each tap spans exactly one processed frame
    for (const FKey& key : mPendingReleases)
    {
        ReleaseKey(key);
    }
    mPendingReleases.Reset();

    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    TickPhase(mPhase, pawn);
    if (mPhaseTimeS < GetPhaseDuration(mPhase) || mPhaseFrames < GetPhaseMinFrames(mPhase))
    {
        return false;
    }

    // Phase over: checks, then the next phase
    ExitPhase(mPhase, pawn);
    mPhase = static_cast<ESOLMapPickPhase>(static_cast<uint8>(mPhase) + 1);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    if (mPhase == ESOLMapPickPhase::Done)
    {
        return true;
    }
    EnterPhase(mPhase, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions (key presses, cursor moves)
void FSOLMapPickSmoke::EnterPhase(const ESOLMapPickPhase phase, ASOLShipPawn& pawn)
{
    switch (phase)
    {
    case ESOLMapPickPhase::OpenJ:
    case ESOLMapPickPhase::CloseJ:
    case ESOLMapPickPhase::ReopenJ:
    case ESOLMapPickPhase::CloseAgain:
        TapKey(EKeys::J);
        break;
    case ESOLMapPickPhase::ClickBody:
    case ESOLMapPickPhase::ClickEmpty:
        // Cursor on the target first, then the button: the pawn picks at the cursor when the press is processed
        mDragStartPx = phase == ESOLMapPickPhase::ClickBody ? mBodyScreenPx : mEmptyScreenPx;
        pawn.HandleMapCursorMoved(mDragStartPx);
        PressKey(EKeys::LeftMouseButton);
        break;
    case ESOLMapPickPhase::ReleaseBody:
    case ESOLMapPickPhase::ReleaseEmpty:
        ReleaseKey(EKeys::LeftMouseButton);
        break;
    case ESOLMapPickPhase::ShiftUp:
        mIsHeightMonotonic = true;
        mLastHeightPreviewM = 0.0;
        PressKey(EKeys::LeftShift);
        break;
    case ESOLMapPickPhase::ShiftClick:
        TapKey(EKeys::LeftMouseButton);
        break;
    case ESOLMapPickPhase::EnterBody:
    case ESOLMapPickPhase::EnterShip:
        TapKey(EKeys::Enter);
        break;
    case ESOLMapPickPhase::ClearX:
        TapKey(EKeys::X);
        break;
    case ESOLMapPickPhase::OsCursorPlace:
        // From here the map cursor follows the real OS cursor, which the script places like the player's hand
        pawn.SetMapCursorScripted(false);
        mDragStartPx = FVector2D(FMath::RoundToDouble(mEmptyScreenPx.X), FMath::RoundToDouble(mEmptyScreenPx.Y));
        SetOsCursorPx(mDragStartPx);
        break;
    case ESOLMapPickPhase::OsCursorPress:
        PressKey(EKeys::LeftMouseButton);
        break;
    case ESOLMapPickPhase::OsCursorDrag:
        // Shift goes down mid-drag (no planar lock yet, so it only arms the height preview for the release)
        PressKey(EKeys::LeftShift);
        break;
    case ESOLMapPickPhase::OsCursorRelease:
        // The viewport's capture kept the OS cursor at the drag start and restores it there on mouse-up
        SetOsCursorPx(mDragStartPx);
        ReleaseKey(EKeys::LeftMouseButton);
        break;
    case ESOLMapPickPhase::OsCursorMove:
    {
        FVector2D osCursorPx = mOsReleaseCursorPx;
        GetOsCursorPx(osCursorPx);
        SetOsCursorPx(osCursorPx + PICK_SMOKE_OS_MOVE_PX);
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's per-frame actions (mouse deltas, cursor moves)
void FSOLMapPickSmoke::TickPhase(const ESOLMapPickPhase phase, ASOLShipPawn& pawn)
{
    switch (phase)
    {
    case ESOLMapPickPhase::DragBody:
    case ESOLMapPickPhase::DragEmpty:
        if (mPhaseFrames <= PICK_SMOKE_DRAG_FRAMES)
        {
            const bool bBody = phase == ESOLMapPickPhase::DragBody;
            InjectAxis(EKeys::MouseX, bBody ? PICK_SMOKE_BODY_DRAG_X_PX : PICK_SMOKE_EMPTY_DRAG_X_PX);
            InjectAxis(EKeys::MouseY, bBody ? PICK_SMOKE_BODY_DRAG_Y_PX : PICK_SMOKE_EMPTY_DRAG_Y_PX);
        }
        break;
    case ESOLMapPickPhase::ShiftUp:
    {
        // Frame 1: Shift was processed and the preview started at the current cursor. From then on the cursor steps
        // up each frame, and every frame after a step must show a larger height than the one before
        const double previewM = mMapMode->GetPickHeightOffsetM();
        if (mPhaseFrames == 1)
        {
            mHeightAnchorPx = pawn.GetMapCursorPx().Y;
        }
        if (mPhaseFrames > PICK_SMOKE_HEIGHT_FIRST_FRAME
            && mPhaseFrames <= PICK_SMOKE_HEIGHT_FIRST_FRAME + PICK_SMOKE_HEIGHT_UP_FRAMES)
        {
            mIsHeightMonotonic &= previewM > mLastHeightPreviewM;
            if (mPhaseFrames % 3 == 0)
            {
                UE_LOG(LogSOL, Log, TEXT("SmokeMapPick: height preview %.6g m, cursor %.1f px above the start"),
                    previewM, mHeightAnchorPx - pawn.GetMapCursorPx().Y);
            }
        }
        mLastHeightPreviewM = previewM;
        if (mPhaseFrames >= PICK_SMOKE_HEIGHT_FIRST_FRAME
            && mPhaseFrames < PICK_SMOKE_HEIGHT_FIRST_FRAME + PICK_SMOKE_HEIGHT_UP_FRAMES)
        {
            pawn.HandleMapCursorMoved(pawn.GetMapCursorPx() + FVector2D(0.0, PICK_SMOKE_HEIGHT_UP_STEP_PX));
        }
        break;
    }
    case ESOLMapPickPhase::ShiftBack:
        if (mPhaseFrames <= PICK_SMOKE_HEIGHT_BACK_FRAMES)
        {
            pawn.HandleMapCursorMoved(pawn.GetMapCursorPx() + FVector2D(0.0, PICK_SMOKE_HEIGHT_BACK_STEP_PX));
        }
        break;
    case ESOLMapPickPhase::OsCursorDrag:
        // Deltas only: nothing moves the OS cursor, as while the viewport captures the mouse
        if (mPhaseFrames <= PICK_SMOKE_DRAG_FRAMES)
        {
            InjectAxis(EKeys::MouseX, PICK_SMOKE_OS_DRAG_X_PX);
            InjectAxis(EKeys::MouseY, PICK_SMOKE_OS_DRAG_Y_PX);
        }
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks and releases held keys
void FSOLMapPickSmoke::ExitPhase(const ESOLMapPickPhase phase, ASOLShipPawn& pawn)
{
    const FSOLMapPickState& pick = mMapMode->GetPickState();
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
    switch (phase)
    {
    case ESOLMapPickPhase::OpenJ:
        Check(TEXT("a-open"), pawn.IsMapOpen() && mMapMode->IsMapOpen() && IsPickCleared(), FString::Printf(
            TEXT("map open %s, pick cleared %s"), mMapMode->IsMapOpen() ? TEXT("yes") : TEXT("no"), IsPickCleared()
            ? TEXT("yes") : TEXT("NO")));
        break;
    case ESOLMapPickPhase::SettleWide:
        ChooseTargets();
        Check(TEXT("a2-targets"), mBodyIndex != INDEX_NONE, FString::Printf(TEXT("clicking %s at (%.1f, %.1f) px; "
            "empty space at (%.1f, %.1f) px"), mBodyIndex != INDEX_NONE ? *registry.GetName(mBodyIndex).ToString()
            : TEXT("none"), mBodyScreenPx.X, mBodyScreenPx.Y, mEmptyScreenPx.X, mEmptyScreenPx.Y));
        break;
    case ESOLMapPickPhase::ClickBody:
    {
        const bool bPassed = pick.bHasReference && pick.bReferenceIsBody && pick.ReferenceBodyIndex == mBodyIndex
            && mMapMode->IsPickDragging();
        Check(TEXT("b-body-reference"), bPassed, FString::Printf(TEXT("mouse-down on %s's icon: reference is body "
            "%s, index %d (want %d), dragging %s"), mBodyIndex != INDEX_NONE ? *registry.GetName(mBodyIndex).ToString()
            : TEXT("none"), pick.bReferenceIsBody ? TEXT("yes") : TEXT("NO"), pick.ReferenceBodyIndex, mBodyIndex,
            mMapMode->IsPickDragging() ? TEXT("yes") : TEXT("no")));
        break;
    }
    case ESOLMapPickPhase::ShotDisc:
    {
        const int32 segments = hud != nullptr ? hud->GetMapPickOverlay().GetLastDiscSegmentsDrawn() : -1;
        const FVector2D previewM = mMapMode->GetPickPlanarOffsetM();
        Check(TEXT("c1-disc-drawn"), mMapMode->IsPickDragging() && segments > 0 && previewM.Size() > 0.0,
            FString::Printf(TEXT("dragging %s, live planar preview (%.6g, %.6g) m (range %.6g m), disc rim segments "
            "drawn %d"), mMapMode->IsPickDragging() ? TEXT("yes") : TEXT("NO"), previewM.X, previewM.Y,
            previewM.Size(), segments));
        Screenshot(pawn, TEXT("pick disc mid-drag"));
        break;
    }
    case ESOLMapPickPhase::ReleaseBody:
        CheckPlanarLock(TEXT("c2-planar-body"), pawn, mDragStartPx + FVector2D(
            PICK_SMOKE_DRAG_FRAMES * PICK_SMOKE_BODY_DRAG_X_PX, -PICK_SMOKE_DRAG_FRAMES * PICK_SMOKE_BODY_DRAG_Y_PX));
        break;
    case ESOLMapPickPhase::ShiftUp:
    {
        const double metersPerPixel = mMapMode->GetHeightMetersPerPixel();
        const double expectedM = -PICK_SMOKE_HEIGHT_UP_STEP_PX * PICK_SMOKE_HEIGHT_UP_FRAMES * metersPerPixel;
        const double previewM = mMapMode->GetPickHeightOffsetM();
        Check(TEXT("d-height-preview"), mMapMode->IsHeightPreviewing() && expectedM > 0.0 && mIsHeightMonotonic
            && FMath::Abs(previewM - expectedM) <= PICK_SMOKE_HEIGHT_RELATIVE_TOLERANCE * expectedM,
            FString::Printf(TEXT("Shift held, cursor 60 px up: height preview %.9g m (want %.9g = 60 px x %.6g m/px), "
            "grew every step %s, previewing %s"), previewM, expectedM, metersPerPixel, mIsHeightMonotonic
            ? TEXT("yes") : TEXT("NO"), mMapMode->IsHeightPreviewing() ? TEXT("yes") : TEXT("NO")));
        break;
    }
    case ESOLMapPickPhase::ShotGuide:
    {
        const bool bGuide = hud != nullptr && hud->GetMapPickOverlay().HasDrawnGuide();
        Check(TEXT("d2-guide-drawn"), bGuide && mMapMode->IsHeightPreviewing(), FString::Printf(TEXT("height guide "
            "line drawn %s at a preview of %.6g m"), bGuide ? TEXT("yes") : TEXT("NO"),
            mMapMode->GetPickHeightOffsetM()));
        Screenshot(pawn, TEXT("pick height guide line"));
        break;
    }
    case ESOLMapPickPhase::ShiftBack:
    {
        const double metersPerPixel = mMapMode->GetHeightMetersPerPixel();
        const double netUpPx = -PICK_SMOKE_HEIGHT_UP_STEP_PX * PICK_SMOKE_HEIGHT_UP_FRAMES
            - PICK_SMOKE_HEIGHT_BACK_STEP_PX * PICK_SMOKE_HEIGHT_BACK_FRAMES;
        const double expectedM = netUpPx * metersPerPixel;
        const double previewM = mMapMode->GetPickHeightOffsetM();
        Check(TEXT("e-height-retrace"), mMapMode->IsHeightPreviewing()
            && FMath::Abs(previewM - expectedM) <= PICK_SMOKE_HEIGHT_RELATIVE_TOLERANCE * FMath::Abs(expectedM),
            FString::Printf(TEXT("cursor back down 20 px (net %.0f px up): height preview %.9g m (want %.9g)"),
            netUpPx, previewM, expectedM));
        break;
    }
    case ESOLMapPickPhase::ShiftClick:
    {
        // Height locked at the preview; destination = live reference + (dx, dy, dz), by ComposeDestination and by hand
        const double expectedHeightM = mMapMode->GetHeightMetersPerPixel() * (-PICK_SMOKE_HEIGHT_UP_STEP_PX
            * PICK_SMOKE_HEIGHT_UP_FRAMES - PICK_SMOKE_HEIGHT_BACK_STEP_PX * PICK_SMOKE_HEIGHT_BACK_FRAMES);
        FVector3d destinationM = FVector3d::ZeroVector;
        const bool bHasDestination = mMapMode->GetLiveDestinationM(destinationM);
        const FVector3d& referenceM = mMapMode->GetPickReferencePositionM();
        const FVector3d byHandM = referenceM + FVector3d(pick.PlanarOffsetXM, pick.PlanarOffsetYM,
            pick.HeightOffsetZM);
        const FVector3d composedM = SOLMapPicking::ComposeDestination(referenceM, pick.PlanarOffsetXM,
            pick.PlanarOffsetYM, pick.HeightOffsetZM, SOL::MAP_PICK_ECLIPTIC_X, SOL::MAP_PICK_ECLIPTIC_Y,
            SOL::MAP_PICK_UP);
        const double errorM = FMath::Max((destinationM - byHandM).GetAbsMax(), (destinationM - composedM).GetAbsMax());
        mReferenceAtLockM = referenceM;
        Check(TEXT("f-height-locked"), pick.bHeightLocked && bHasDestination && !mMapMode->IsHeightPreviewing()
            && FMath::Abs(pick.HeightOffsetZM - expectedHeightM) <= PICK_SMOKE_HEIGHT_RELATIVE_TOLERANCE
            * FMath::Abs(expectedHeightM) && errorM <= PICK_SMOKE_COMPOSE_TOLERANCE_M, FString::Printf(TEXT("Shift+"
            "click: height locked %s at %.9g m (want %.9g); destination (%.9g, %.9g, %.9g) m = reference (%.9g, %.9g, "
            "%.9g) + (%.6g, %.6g, %.6g), error %.3g m"), pick.bHeightLocked ? TEXT("yes") : TEXT("NO"),
            pick.HeightOffsetZM, expectedHeightM, destinationM.X, destinationM.Y, destinationM.Z, referenceM.X,
            referenceM.Y, referenceM.Z, pick.PlanarOffsetXM, pick.PlanarOffsetYM, pick.HeightOffsetZM, errorM));
        ReleaseKey(EKeys::LeftShift);
        break;
    }
    case ESOLMapPickPhase::ShotMarker:
    {
        // The destination rides on the moving body: its offset from the live reference stays the locked one
        FVector3d destinationM = FVector3d::ZeroVector;
        mMapMode->GetLiveDestinationM(destinationM);
        const FVector3d& referenceM = mMapMode->GetPickReferencePositionM();
        const FVector3d offsetM = destinationM - referenceM;
        const double offsetErrorM = (offsetM - FVector3d(pick.PlanarOffsetXM, pick.PlanarOffsetYM,
            pick.HeightOffsetZM)).GetAbsMax();
        const bool bMarker = hud != nullptr && hud->GetMapPickOverlay().HasDrawnFinalMarker();
        Check(TEXT("f2-marker-live"), bMarker && offsetErrorM <= PICK_SMOKE_COMPOSE_TOLERANCE_M, FString::Printf(
            TEXT("final destination marker drawn %s; reference moved %.4g m since the lock, destination offset from "
            "it unchanged within %.3g m"), bMarker ? TEXT("yes") : TEXT("NO"),
            FVector3d::Dist(referenceM, mReferenceAtLockM), offsetErrorM));
        Screenshot(pawn, TEXT("pick destination marker"));
        break;
    }
    case ESOLMapPickPhase::EnterBody:
    case ESOLMapPickPhase::EnterShip:
    {
        // The request carries the live destination at that instant (the map stays open until 2e)
        const bool bBody = phase == ESOLMapPickPhase::EnterBody;
        FVector3d destinationM = FVector3d::ZeroVector;
        const bool bHasDestination = mMapMode->GetLiveDestinationM(destinationM);
        const FVector3d& requestM = mMapMode->GetLastJumpRequestM();
        const double driftM = FVector3d::Dist(requestM, destinationM);
        const int32 expectedCount = bBody ? 1 : 2;
        Check(bBody ? TEXT("g-enter-body") : TEXT("k-enter-ship"), bHasDestination && mMapMode->GetJumpRequestCount()
            == expectedCount && driftM <= PICK_SMOKE_LIVE_TOLERANCE_M && pawn.IsMapOpen() && (bBody
            || !pick.bHeightLocked), FString::Printf(TEXT("Enter: %d jump request(s) (want %d), requested universe "
            "position (%.9g, %.9g, %.9g) m, live destination now (%.9g, %.9g, %.9g) m (%.4g m apart: reference "
            "motion), map still open %s"), mMapMode->GetJumpRequestCount(), expectedCount, requestM.X, requestM.Y,
            requestM.Z, destinationM.X, destinationM.Y, destinationM.Z, driftM, pawn.IsMapOpen() ? TEXT("yes")
            : TEXT("NO")));
        break;
    }
    case ESOLMapPickPhase::ClearX:
    {
        const bool bMarker = hud != nullptr && hud->GetMapPickOverlay().HasDrawnMarker();
        Check(TEXT("h-clear-x"), IsPickCleared() && !bMarker && pawn.IsMapOpen(), FString::Printf(TEXT("X: pick "
            "cleared %s, marker still drawn %s, map open %s"), IsPickCleared() ? TEXT("yes") : TEXT("NO"), bMarker
            ? TEXT("YES") : TEXT("no"), pawn.IsMapOpen() ? TEXT("yes") : TEXT("NO")));
        break;
    }
    case ESOLMapPickPhase::ClickEmpty:
    {
        const double shipOffsetM = FVector3d::Dist(mMapMode->GetPickReferencePositionM(),
            mAnchor->GetObserverPositionM());
        Check(TEXT("i-ship-reference"), pick.bHasReference && !pick.bReferenceIsBody
            && pick.ReferenceBodyIndex == INDEX_NONE && mMapMode->IsPickDragging()
            && shipOffsetM <= PICK_SMOKE_SHIP_REFERENCE_TOLERANCE_M, FString::Printf(TEXT("mouse-down on empty space: "
            "reference is the ship %s (body index %d), reference %.3g m from the ship, dragging %s"),
            pick.bHasReference && !pick.bReferenceIsBody ? TEXT("yes") : TEXT("NO"), pick.ReferenceBodyIndex,
            shipOffsetM, mMapMode->IsPickDragging() ? TEXT("yes") : TEXT("no")));
        break;
    }
    case ESOLMapPickPhase::ReleaseEmpty:
        CheckPlanarLock(TEXT("j-planar-ship"), pawn, mDragStartPx + FVector2D(
            PICK_SMOKE_DRAG_FRAMES * PICK_SMOKE_EMPTY_DRAG_X_PX, -PICK_SMOKE_DRAG_FRAMES * PICK_SMOKE_EMPTY_DRAG_Y_PX));
        break;
    case ESOLMapPickPhase::OsCursorPlace:
    {
        FVector2D osCursorPx(-1.0);
        const bool bHasOs = GetOsCursorPx(osCursorPx);
        const double errorPx = FVector2D::Distance(pawn.GetMapCursorPx(), mDragStartPx);
        Check(TEXT("n1-os-follow"), bHasOs && errorPx <= PICK_SMOKE_OS_CURSOR_TOLERANCE_PX
            && FVector2D::Distance(osCursorPx, mDragStartPx) <= PICK_SMOKE_OS_CURSOR_TOLERANCE_PX, FString::Printf(
            TEXT("scripted cursor off, OS cursor set to (%.0f, %.0f) px: OS reads (%.1f, %.1f) px (%s), map cursor "
            "(%.1f, %.1f) px"), mDragStartPx.X, mDragStartPx.Y, osCursorPx.X, osCursorPx.Y, bHasOs ? TEXT("valid")
            : TEXT("NONE"), pawn.GetMapCursorPx().X, pawn.GetMapCursorPx().Y));
        break;
    }
    case ESOLMapPickPhase::OsCursorPress:
        Check(TEXT("n2-os-press"), pick.bHasReference && !pick.bReferenceIsBody && mMapMode->IsPickDragging(),
            FString::Printf(TEXT("mouse-down at the OS cursor: new pick on the ship %s, dragging %s"),
            pick.bHasReference && !pick.bReferenceIsBody ? TEXT("yes") : TEXT("NO"), mMapMode->IsPickDragging()
            ? TEXT("yes") : TEXT("NO")));
        break;
    case ESOLMapPickPhase::OsCursorDrag:
    {
        // The precondition of the desync: mid-drag the map cursor moved while the OS cursor stayed at the start
        FVector2D osCursorPx(-1.0);
        GetOsCursorPx(osCursorPx);
        const FVector2D expectedPx = mDragStartPx + FVector2D(PICK_SMOKE_DRAG_FRAMES * PICK_SMOKE_OS_DRAG_X_PX,
            -PICK_SMOKE_DRAG_FRAMES * PICK_SMOKE_OS_DRAG_Y_PX);
        const double dragPx = (expectedPx - mDragStartPx).Size();
        const double mapErrorPx = FVector2D::Distance(pawn.GetMapCursorPx(), expectedPx);
        Check(TEXT("n3-os-drag"), mapErrorPx <= PICK_SMOKE_DRAG_RELATIVE_TOLERANCE * dragPx
            + PICK_SMOKE_DRAG_ABSOLUTE_TOLERANCE_PX && FVector2D::Distance(osCursorPx, mDragStartPx)
            <= PICK_SMOKE_OS_CURSOR_TOLERANCE_PX && !mMapMode->IsHeightPreviewing(), FString::Printf(TEXT("mid-drag: "
            "map cursor (%.1f, %.1f) px (want (%.1f, %.1f)), OS cursor pinned at (%.1f, %.1f) px, height preview "
            "%s (Shift held, planar not locked yet)"), pawn.GetMapCursorPx().X, pawn.GetMapCursorPx().Y, expectedPx.X,
            expectedPx.Y, osCursorPx.X, osCursorPx.Y, mMapMode->IsHeightPreviewing() ? TEXT("STARTED") : TEXT("off")));
        break;
    }
    case ESOLMapPickPhase::OsCursorRelease:
    {
        // Mouse-up: the OS cursor must end on the map cursor (not at the drag start the viewport restored), the
        // planar lock must sit under it, and the height preview (Shift held) must start from it
        CheckPlanarLock(TEXT("n4-os-planar"), pawn, mDragStartPx + FVector2D(PICK_SMOKE_DRAG_FRAMES
            * PICK_SMOKE_OS_DRAG_X_PX, -PICK_SMOKE_DRAG_FRAMES * PICK_SMOKE_OS_DRAG_Y_PX));
        FVector2D osCursorPx(-1.0);
        const bool bHasOs = GetOsCursorPx(osCursorPx);
        mOsReleaseCursorPx = pawn.GetMapCursorPx();
        const double syncErrorPx = FVector2D::Distance(osCursorPx, mOsReleaseCursorPx);
        Check(TEXT("n5-os-release-sync"), bHasOs && syncErrorPx <= PICK_SMOKE_OS_CURSOR_TOLERANCE_PX
            && mMapMode->IsHeightPreviewing() && mMapMode->GetPickHeightOffsetM() == 0.0, FString::Printf(TEXT(
            "mouse-up (OS cursor put back at the drag start (%.0f, %.0f) px first): OS cursor (%.1f, %.1f) px, map "
            "cursor (%.1f, %.1f) px, %.2f px apart; height preview %s at %.6g m"), mDragStartPx.X, mDragStartPx.Y,
            osCursorPx.X, osCursorPx.Y, mOsReleaseCursorPx.X, mOsReleaseCursorPx.Y, syncErrorPx,
            mMapMode->IsHeightPreviewing() ? TEXT("started") : TEXT("NOT started"), mMapMode->GetPickHeightOffsetM()));
        break;
    }
    case ESOLMapPickPhase::OsCursorMove:
    {
        // The next OS move moves the map cursor by exactly that move (no jump), and the height follows from the
        // release position (10 px up = 10 px of height)
        const FVector2D expectedPx = mOsReleaseCursorPx + PICK_SMOKE_OS_MOVE_PX;
        const double moveErrorPx = FVector2D::Distance(pawn.GetMapCursorPx(), expectedPx);
        const double metersPerPixel = mMapMode->GetHeightMetersPerPixel();
        const double expectedHeightM = -PICK_SMOKE_OS_MOVE_PX.Y * metersPerPixel;
        const double heightM = mMapMode->GetPickHeightOffsetM();
        Check(TEXT("n6-os-no-jump"), moveErrorPx <= PICK_SMOKE_OS_CURSOR_TOLERANCE_PX && mMapMode->IsHeightPreviewing()
            && FMath::Abs(heightM - expectedHeightM) <= PICK_SMOKE_OS_CURSOR_TOLERANCE_PX * metersPerPixel,
            FString::Printf(TEXT("OS move by (%.0f, %.0f) px: map cursor (%.1f, %.1f) px (want (%.1f, %.1f), %.2f px "
            "off); height preview %.6g m (want %.6g = %.0f px x %.6g m/px)"), PICK_SMOKE_OS_MOVE_PX.X,
            PICK_SMOKE_OS_MOVE_PX.Y, pawn.GetMapCursorPx().X, pawn.GetMapCursorPx().Y, expectedPx.X, expectedPx.Y,
            moveErrorPx, heightM, expectedHeightM, -PICK_SMOKE_OS_MOVE_PX.Y, metersPerPixel));
        ReleaseKey(EKeys::LeftShift);
        pawn.SetMapCursorScripted(true);
        break;
    }
    case ESOLMapPickPhase::CloseJ:
        Check(TEXT("l-close-clears"), !pawn.IsMapOpen() && !mMapMode->IsMapOpen() && IsPickCleared(),
            FString::Printf(TEXT("J: map closed %s, pick cleared %s"), !mMapMode->IsMapOpen() ? TEXT("yes")
            : TEXT("NO"), IsPickCleared() ? TEXT("yes") : TEXT("NO")));
        break;
    case ESOLMapPickPhase::ReopenJ:
        Check(TEXT("m-reopen-clear"), pawn.IsMapOpen() && mMapMode->IsMapOpen() && IsPickCleared()
            && mMapMode->GetJumpRequestCount() == 0, FString::Printf(TEXT("J again: map open %s, pick cleared %s, jump "
            "requests %d"), mMapMode->IsMapOpen() ? TEXT("yes") : TEXT("NO"), IsPickCleared() ? TEXT("yes")
            : TEXT("NO"), mMapMode->GetJumpRequestCount()));
        break;
    case ESOLMapPickPhase::Finish:
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeMapPick: complete after %.1f s, %d/%d checks passed%s"), mTotalTimeS,
            mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("") : TEXT(" (FAILURES)"));
        pawn.SetMapCursorScripted(false);
        if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
        {
            gameMode->TakeSmokeScreenshot();
        }
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase in seconds
double FSOLMapPickSmoke::GetPhaseDuration(const ESOLMapPickPhase phase)
{
    switch (phase)
    {
    case ESOLMapPickPhase::Setup:
        return PICK_SMOKE_SETUP_S;
    case ESOLMapPickPhase::SettleWide:
        return PICK_SMOKE_SETTLE_WIDE_S;
    case ESOLMapPickPhase::ShotDisc:
    case ESOLMapPickPhase::ShotGuide:
    case ESOLMapPickPhase::ShotMarker:
        return PICK_SMOKE_SHOT_SETTLE_S;
    case ESOLMapPickPhase::Finish:
        return PICK_SMOKE_FINISH_S;
    case ESOLMapPickPhase::OpenJ:
    case ESOLMapPickPhase::CloseJ:
    case ESOLMapPickPhase::ReopenJ:
    case ESOLMapPickPhase::CloseAgain:
        return PICK_SMOKE_TAP_PHASE_S;
    default:
        return 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum number of frames a phase lasts
int32 FSOLMapPickSmoke::GetPhaseMinFrames(const ESOLMapPickPhase phase)
{
    switch (phase)
    {
    case ESOLMapPickPhase::DragBody:
    case ESOLMapPickPhase::DragEmpty:
    case ESOLMapPickPhase::OsCursorDrag:
        return PICK_SMOKE_DRAG_FRAMES + PICK_SMOKE_SETTLE_FRAMES;
    case ESOLMapPickPhase::ShiftUp:
        return PICK_SMOKE_HEIGHT_FIRST_FRAME + PICK_SMOKE_HEIGHT_UP_FRAMES + PICK_SMOKE_SETTLE_FRAMES;
    case ESOLMapPickPhase::ShiftBack:
        return PICK_SMOKE_HEIGHT_BACK_FRAMES + PICK_SMOKE_SETTLE_FRAMES;
    default:
        return PICK_SMOKE_SETTLE_FRAMES;
    }
}

//////////////////////////////////////////////////////////////////////////
// Picks the most isolated on-screen body icon and the emptiest screen point from the body overlay
void FSOLMapPickSmoke::ChooseTargets()
{
    mBodyIndex = INDEX_NONE;
    const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
    if (hud == nullptr)
    {
        return;
    }
    const FSOLMapBodyOverlay& overlay = hud->GetMapBodyOverlay();
    const TConstArrayView<FVector2D> screenPx = overlay.GetScreenPositionsPx();
    const TConstArrayView<double> pickRadiiPx = overlay.GetPickRadiiPx();
    const int32 bodyCount = FMath::Min(screenPx.Num(), pickRadiiPx.Num());
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
    mController->GetViewportSize(viewportWidth, viewportHeight);
    const FBox2D inner(FVector2D(PICK_SMOKE_SCREEN_MARGIN_PX), FVector2D(viewportWidth - PICK_SMOKE_SCREEN_MARGIN_PX,
        viewportHeight - PICK_SMOKE_SCREEN_MARGIN_PX));

    // Body: the pickable icon inside the margins whose nearest other pickable icon is farthest away
    double bestSeparationPx = -1.0;
    for (int32 index = 0; index < bodyCount; ++index)
    {
        if (pickRadiiPx[index] <= 0.0 || !inner.IsInside(screenPx[index]))
        {
            continue;
        }
        double separationPx = TNumericLimits<double>::Max();
        for (int32 other = 0; other < bodyCount; ++other)
        {
            if (other != index && pickRadiiPx[other] > 0.0)
            {
                separationPx = FMath::Min(separationPx, FVector2D::Distance(screenPx[index], screenPx[other]));
            }
        }
        if (separationPx > bestSeparationPx)
        {
            bestSeparationPx = separationPx;
            mBodyIndex = index;
            mBodyScreenPx = screenPx[index];
        }
    }

    // Empty space: the grid point inside the margins farthest outside every body's pick circle
    double bestClearancePx = -TNumericLimits<double>::Max();
    for (double y = inner.Min.Y; y <= inner.Max.Y; y += PICK_SMOKE_EMPTY_GRID_STEP_PX)
    {
        for (double x = inner.Min.X; x <= inner.Max.X; x += PICK_SMOKE_EMPTY_GRID_STEP_PX)
        {
            double clearancePx = TNumericLimits<double>::Max();
            for (int32 index = 0; index < bodyCount; ++index)
            {
                if (pickRadiiPx[index] > 0.0)
                {
                    clearancePx = FMath::Min(clearancePx, FVector2D::Distance(FVector2D(x, y), screenPx[index])
                        - pickRadiiPx[index]);
                }
            }
            if (clearancePx > bestClearancePx)
            {
                bestClearancePx = clearancePx;
                mEmptyScreenPx = FVector2D(x, y);
            }
        }
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeMapPick: target body index %d, %.1f px from the nearest other icon; empty point "
        "%.1f px outside every pick circle"), mBodyIndex, bestSeparationPx, bestClearancePx);
}

//////////////////////////////////////////////////////////////////////////
// Checks the planar lock: offsets locked, the cursor moved by the drag, and the XY point projecting onto the cursor
void FSOLMapPickSmoke::CheckPlanarLock(const TCHAR* label, const ASOLShipPawn& pawn, const FVector2D& expectedCursorPx)
{
    const FSOLMapPickState& pick = mMapMode->GetPickState();
    const FVector2D& cursorPx = pawn.GetMapCursorPx();
    const double dragPx = (expectedCursorPx - mDragStartPx).Size();
    const double cursorErrorPx = FVector2D::Distance(cursorPx, expectedCursorPx);
    const bool bCursorMoved = cursorErrorPx <= PICK_SMOKE_DRAG_RELATIVE_TOLERANCE * dragPx
        + PICK_SMOKE_DRAG_ABSOLUTE_TOLERANCE_PX;

    // Independent round trip: the subsystem inverted the cursor into a plane point; projecting that point forward
    // through the HUD's camera and the bodies' render placement must land back on the cursor, at the reference's height
    FVector3d destinationM = FVector3d::ZeroVector;
    const bool bHasDestination = mMapMode->GetLiveDestinationM(destinationM);
    FVector2D screenPx = FVector2D::ZeroVector;
    const bool bProjected = bHasDestination && ProjectToScreen(destinationM, screenPx);
    const double projectErrorPx = FVector2D::Distance(screenPx, cursorPx);
    const double heightErrorM = FMath::Abs(destinationM.Z - mMapMode->GetPickReferencePositionM().Z);
    const bool bPassed = pick.bPlanarLocked && !pick.bHeightLocked && !mMapMode->IsPickDragging() && bCursorMoved
        && bProjected && projectErrorPx <= PICK_SMOKE_PROJECT_TOLERANCE_PX && heightErrorM == 0.0
        && (pick.PlanarOffsetXM != 0.0 || pick.PlanarOffsetYM != 0.0);
    Check(label, bPassed, FString::Printf(TEXT("mouse-up: planar locked %s at (%.6g, %.6g) m (range %.6g m); cursor "
        "(%.1f, %.1f) px (want (%.1f, %.1f), drag %.0f px); locked XY point projects to (%.2f, %.2f) px, %.3f px from "
        "the cursor; height above the reference %.3g m"), pick.bPlanarLocked ? TEXT("yes") : TEXT("NO"),
        pick.PlanarOffsetXM, pick.PlanarOffsetYM, FVector2D(pick.PlanarOffsetXM, pick.PlanarOffsetYM).Size(),
        cursorPx.X, cursorPx.Y, expectedCursorPx.X, expectedCursorPx.Y, dragPx, screenPx.X, screenPx.Y,
        projectErrorPx, heightErrorM));
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the pick state is back to "no reference" with no preview
bool FSOLMapPickSmoke::IsPickCleared() const
{
    const FSOLMapPickState& pick = mMapMode->GetPickState();
    return !pick.bHasReference && !pick.bReferenceIsBody && pick.ReferenceBodyIndex == INDEX_NONE
        && !pick.bPlanarLocked && !pick.bHeightLocked && pick.PlanarOffsetXM == 0.0 && pick.PlanarOffsetYM == 0.0
        && pick.HeightOffsetZM == 0.0 && !mMapMode->IsPickDragging() && !mMapMode->IsHeightPreviewing();
}

//////////////////////////////////////////////////////////////////////////
// Projects a universe point to the screen as the HUD does (camera POV, bodies' render placement); false if behind
bool FSOLMapPickSmoke::ProjectToScreen(const FVector3d& pointM, FVector2D& outScreen) const
{
    if (mController->PlayerCameraManager == nullptr)
    {
        return false;
    }
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
    mController->GetViewportSize(viewportWidth, viewportHeight);
    const FMinimalViewInfo& pov = mController->PlayerCameraManager->GetCameraCacheView();
    const double focalPx = 0.5 * viewportWidth / FMath::Tan(FMath::DegreesToRadians(static_cast<double>(pov.FOV))
        * 0.5);
    const FVector local = pov.Rotation.Quaternion().UnrotateVector(mAnchor->ComputePointRenderLocationCm(pointM)
        - pov.Location);
    if (local.X <= 0.0)
    {
        return false;
    }
    outScreen = FVector2D(0.5 * viewportWidth + focalPx * (local.Y / local.X),
        0.5 * viewportHeight - focalPx * (local.Z / local.X));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Reads the OS mouse position in viewport pixels; false when the viewport has none
bool FSOLMapPickSmoke::GetOsCursorPx(FVector2D& outCursorPx) const
{
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    if (!mController->GetMousePosition(mouseX, mouseY))
    {
        return false;
    }
    outCursorPx = FVector2D(mouseX, mouseY);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Moves the real OS cursor to a viewport pixel position
void FSOLMapPickSmoke::SetOsCursorPx(const FVector2D& cursorPx) const
{
    mController->SetMouseLocation(FMath::RoundToInt32(cursorPx.X), FMath::RoundToInt32(cursorPx.Y));
}

//////////////////////////////////////////////////////////////////////////
// Takes a verification screenshot and logs its name
void FSOLMapPickSmoke::Screenshot(const ASOLShipPawn& pawn, const TCHAR* name) const
{
    UE_LOG(LogSOL, Log, TEXT("SmokeMapPick: screenshot (%s)"), name);
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->CaptureScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller
void FSOLMapPickSmoke::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller
void FSOLMapPickSmoke::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a key now and releases it on the next frame
void FSOLMapPickSmoke::TapKey(const FKey& key)
{
    PressKey(key);
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Sends one analog axis sample (mouse delta in pixels) to the player controller
void FSOLMapPickSmoke::InjectAxis(const FKey& key, const float delta) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Axis, delta, 1));
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLMapPickSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeMapPick CHECK %-18s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
