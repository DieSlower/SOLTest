/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRouteOverlaySmoke.h"

#include "Game/SOLGameMode.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLRouteOverlayMath.h"
#include "Map/SOLRouteSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
    // Script timing: settle after start, and the waits before each check
    constexpr double OVERLAY_SMOKE_SETTLE_S = 3.0;
    constexpr double OVERLAY_SMOKE_VIEW_WAIT_S = 0.75;
    constexpr int32 OVERLAY_SMOKE_SHORT_FRAMES = 4;

    // A drawn marker must lie within this many pixels of the engine's projection of its waypoint
    constexpr double OVERLAY_SMOKE_TOLERANCE_PX = 2.0;

    // Waypoints in front of the camera (render cm in camera axes: forward, right, up), all within 1,000,000 km so
    // the render placement is uncompressed
    const FVector OVERLAY_SMOKE_OFFSETS_CM[] =
    {
        FVector(6.0e6, -1.5e6, 0.8e6),
        FVector(8.0e6, 1.8e6, -0.6e6),
        FVector(5.0e6, 0.5e6, 1.2e6),
    };
    constexpr int32 OVERLAY_SMOKE_WAYPOINTS = UE_ARRAY_COUNT(OVERLAY_SMOKE_OFFSETS_CM);

    // Map camera around the route: distance (m), pitch and yaw (rad)
    constexpr double OVERLAY_SMOKE_MAP_DISTANCE_M = 4.0e5;
    constexpr double OVERLAY_SMOKE_MAP_PITCH_RAD = 0.5;
    constexpr double OVERLAY_SMOKE_MAP_YAW_RAD = 0.4;

    // Draft picks (fractions of the viewport) and the drag applied after each press (pixels)
    const FVector2D OVERLAY_SMOKE_PICKS[] = { FVector2D(0.3, 0.7), FVector2D(0.7, 0.75) };
    constexpr int32 OVERLAY_SMOKE_PICK_COUNT = UE_ARRAY_COUNT(OVERLAY_SMOKE_PICKS);
    const FVector2D OVERLAY_SMOKE_DRAG_PX(70.0, -30.0);

    // Script steps
    enum EOverlayStep : int32
    {
        StepSettle,
        StepFlightCheck,
        StepOverlayOff,
        StepOverlayOn,
        StepMapCheck,
        StepPickPress,
        StepPickDrag,
        StepPickRelease,
        StepDraftCheck,
        StepClose,
        StepDone,
    };
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and subsystems
void FSOLRouteOverlaySmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mRoutes = world->GetSubsystem<USOLRouteSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    UE_LOG(LogSOL, Log, TEXT("SmokeRouteOverlay: route overlay run starting"));
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLRouteOverlaySmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mIsFinished)
    {
        return true;
    }
    ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>();
    const ASOLFlightHud* hud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    if (hud == nullptr || !mMapMode.IsValid() || !mRoutes.IsValid() || !mShips.IsValid() || !mAnchor.IsValid()
        || !mRegistry.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeRouteOverlay: controller, HUD or subsystems missing; aborting"));
        mIsFinished = true;
        if (gameMode != nullptr)
        {
            gameMode->TakeSmokeScreenshot();     // screenshot, then quit, so a headless run cannot hang
        }
        return true;
    }
    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    if (mStep < StepClose)
    {
        HoldShipAtRest();
    }
    if (mMapMode->IsMapOpen())
    {
        AimMapCamera();
    }
    const FSOLMapRouteOverlay& overlay = hud->GetRouteOverlay();
    const FVector3d shipM = mShips->GetUniversePositionM();

    switch (mStep)
    {
    case StepSettle:
        if (mStepTimeS >= OVERLAY_SMOKE_SETTLE_S && mShips->HasPlayerShip())
        {
            Check(TEXT("route started and paused"), StartPausedRoute(), FString::Printf(TEXT("state %d"),
                static_cast<int32>(mRoutes->GetState())));
            NextStep();
        }
        break;

    case StepFlightCheck:
        if (mStepTimeS >= OVERLAY_SMOKE_VIEW_WAIT_S)
        {
            Check(TEXT("route paused"), mRoutes->GetState() == ESOLRouteState::Paused, TEXT("autopilot disengaged"));
            Check(TEXT("flight legs"), overlay.GetLastLegsDrawn() == OVERLAY_SMOKE_WAYPOINTS,
                FString::Printf(TEXT("%d drawn, %d expected (loop)"), overlay.GetLastLegsDrawn(),
                OVERLAY_SMOKE_WAYPOINTS));
            Check(TEXT("dashed closing leg"), overlay.GetLastDashesDrawn() > 0,
                FString::Printf(TEXT("%d dashes"), overlay.GetLastDashesDrawn()));
            Check(TEXT("direction arrows"), overlay.GetLastArrowsDrawn() >= OVERLAY_SMOKE_WAYPOINTS,
                FString::Printf(TEXT("%d arrows"), overlay.GetLastArrowsDrawn()));
            Check(TEXT("ship leg"), overlay.HasDrawnShipLeg(), TEXT("ship to the target"));
            Check(TEXT("PAUSED tag"), overlay.HasDrawnPausedTag(), TEXT("beside the target marker"));
            Check(TEXT("triangle budget"), !overlay.WasBudgetExhausted() && overlay.GetLastTriangles() > 0,
                FString::Printf(TEXT("%d triangles"), overlay.GetLastTriangles()));
            CheckRouteMarkers(TEXT("flight"), shipM);
            const FString& line = hud->GetLastRouteLineText();
            Check(TEXT("HUD route marker"), line.Contains(TEXT("PAUSED")) && line.Contains(TEXT("PATROL LOOP")),
                FString::Printf(TEXT("\"%s\""), *line));
            const FBox2D& lineRect = hud->GetLastRouteLineRect();
            const FBox2D& hintsRect = hud->GetLastHintsRect();
            Check(TEXT("route marker clear of the key hints"), lineRect.bIsValid
                && !(hintsRect.bIsValid && lineRect.Intersect(hintsRect)), FString::Printf(
                TEXT("marker %s, hints %s at %.0f x %.0f"), *lineRect.ToString(), *hintsRect.ToString(), mViewportPx.X,
                mViewportPx.Y));
            UE_LOG(LogSOL, Log, TEXT("SmokeRouteOverlay: screenshot (flight view, paused route)"));
            gameMode->CaptureScreenshot();
            NextStep();
        }
        break;

    case StepOverlayOff:
        if (mStepFrames == OVERLAY_SMOKE_SHORT_FRAMES)
        {
            mRoutes->ToggleOverlay();       // After the screenshot frame has rendered
        }
        if (mStepFrames >= 2 * OVERLAY_SMOKE_SHORT_FRAMES)
        {
            Check(TEXT("P hides the overlay"), !mRoutes->IsOverlayVisible() && overlay.GetLastLegsDrawn() == 0
                && overlay.GetLastMarkers().Num() == 0, FString::Printf(TEXT("%d legs, %d markers"),
                overlay.GetLastLegsDrawn(), overlay.GetLastMarkers().Num()));
            mRoutes->ToggleOverlay();
            NextStep();
        }
        break;

    case StepOverlayOn:
        if (mStepFrames >= OVERLAY_SMOKE_SHORT_FRAMES)
        {
            Check(TEXT("P shows it again"), mRoutes->IsOverlayVisible()
                && overlay.GetLastLegsDrawn() == OVERLAY_SMOKE_WAYPOINTS, FString::Printf(TEXT("%d legs"),
                overlay.GetLastLegsDrawn()));
            mMapMode->OpenMap(mController.Get());
            mMapMode->PressModeKey(ESOLMapMode::Move);
            AimMapCamera();
            NextStep();
        }
        break;

    case StepMapCheck:
        if (mStepTimeS >= OVERLAY_SMOKE_VIEW_WAIT_S)
        {
            Check(TEXT("map open"), mMapMode->IsMapOpen(), TEXT("OpenMap"));
            Check(TEXT("map legs"), overlay.GetLastLegsDrawn() == OVERLAY_SMOKE_WAYPOINTS,
                FString::Printf(TEXT("%d drawn"), overlay.GetLastLegsDrawn()));
            CheckRouteMarkers(TEXT("map"), mMapMode->GetCameraState().FocusPositionM);
            UE_LOG(LogSOL, Log, TEXT("SmokeRouteOverlay: screenshot (map view, paused route)"));
            gameMode->CaptureScreenshot();
            NextStep();
        }
        break;

    case StepPickPress:
    {
        // Each pick: press at a scripted point, drag, release (Move mode appends a draft waypoint)
        const int32 pick = mMapMode->GetModeState().Draft.Num();
        const FVector2D cursorPx = mViewportPx * OVERLAY_SMOKE_PICKS[FMath::Min(pick, 1)];
        mMapMode->SetPickCursorPx(cursorPx, mViewportPx);
        if (mStepFrames >= OVERLAY_SMOKE_SHORT_FRAMES)
        {
            mMapMode->PressPick(&hud->GetMapBodyOverlay(), false);
            NextStep();
        }
        break;
    }

    case StepPickDrag:
    {
        const int32 pick = mMapMode->GetModeState().Draft.Num();
        mMapMode->SetPickCursorPx(mViewportPx * OVERLAY_SMOKE_PICKS[FMath::Min(pick, 1)] + OVERLAY_SMOKE_DRAG_PX,
            mViewportPx);
        if (mStepFrames >= OVERLAY_SMOKE_SHORT_FRAMES)
        {
            const FString label = FString::Printf(TEXT("cursor segment (pick %d)"), pick + 1);
            Check(*label, mMapMode->IsPickDragging() && overlay.HasDrawnCursorSegment(), FString::Printf(
                TEXT("dragging %d"), mMapMode->IsPickDragging() ? 1 : 0));
            mMapMode->ReleasePick();
            NextStep();
        }
        break;
    }

    case StepPickRelease:
        if (mStepFrames >= OVERLAY_SMOKE_SHORT_FRAMES)
        {
            if (mMapMode->GetModeState().Draft.Num() < OVERLAY_SMOKE_PICK_COUNT
                && mMapMode->GetModeState().Draft.Num() > 0)
            {
                mStep = StepPickPress;      // Next pick
                mStepFrames = 0;
                mStepTimeS = 0.0;
                break;
            }
            NextStep();
        }
        break;

    case StepDraftCheck:
        if (mStepFrames >= OVERLAY_SMOKE_SHORT_FRAMES)
        {
            int32 draftMarkers = 0;
            for (const FSOLMapRouteOverlay::FMarker& marker : overlay.GetLastMarkers())
            {
                draftMarkers += marker.bDraft ? 1 : 0;
            }
            const int32 draftCount = mMapMode->GetModeState().Draft.Num();
            Check(TEXT("draft waypoints"), draftCount == OVERLAY_SMOKE_PICK_COUNT,
                FString::Printf(TEXT("%d in the draft"), draftCount));
            Check(TEXT("draft markers"), draftMarkers == draftCount, FString::Printf(TEXT("%d drawn"), draftMarkers));
            Check(TEXT("draft leg"), overlay.GetLastLegsDrawn() == OVERLAY_SMOKE_WAYPOINTS + draftCount - 1,
                FString::Printf(TEXT("%d legs (route + draft)"), overlay.GetLastLegsDrawn()));
            Check(TEXT("no cursor segment after release"), !overlay.HasDrawnCursorSegment(), TEXT(""));
            UE_LOG(LogSOL, Log, TEXT("SmokeRouteOverlay: screenshot (map view, draft)"));
            gameMode->CaptureScreenshot();
            NextStep();
        }
        break;

    case StepClose:
        if (mStepFrames == OVERLAY_SMOKE_SHORT_FRAMES)
        {
            mMapMode->ClearPickAndDraft();
            mMapMode->CloseMap(mController.Get());
            mRoutes->ClearRoute();
        }
        if (mStepFrames >= 2 * OVERLAY_SMOKE_SHORT_FRAMES)
        {
            Check(TEXT("cleared route draws nothing"), !mMapMode->IsMapOpen() && overlay.GetLastLegsDrawn() == 0
                && overlay.GetLastMarkers().Num() == 0 && hud->GetLastRouteLineText().IsEmpty(), FString::Printf(
                TEXT("%d legs, %d markers"), overlay.GetLastLegsDrawn(), overlay.GetLastMarkers().Num()));
            UE_LOG(LogSOL, Log, TEXT("SmokeRouteOverlay: complete, %d/%d checks passed%s"), mChecks - mFailures,
                mChecks, mFailures == 0 ? TEXT("") : TEXT(" (FAILURES)"));
            mIsFinished = true;
            gameMode->TakeSmokeScreenshot();
            return true;
        }
        break;

    default:
        break;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Sets the ship at rest relative to its anchor body (no spin)
void FSOLRouteOverlaySmoke::HoldShipAtRest() const
{
    const int32 anchorIndex = mAnchor->GetAnchorIndex();
    if (!mShips->HasPlayerShip() || anchorIndex < 0 || anchorIndex >= mRegistry->GetUnrealFrameCache().Num())
    {
        return;
    }
    FSOLShipState state = mShips->GetState();
    state.VelocityMps = mRegistry->GetUnrealFrameCache().VelocitiesMps[anchorIndex];
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Starts the Patrol Loop of three waypoints in front of the camera and pauses it; false on failure
bool FSOLRouteOverlaySmoke::StartPausedRoute()
{
    mBodyIndex = mAnchor->GetAnchorIndex();
    if (mBodyIndex == INDEX_NONE || mController->PlayerCameraManager == nullptr || GEngine == nullptr
        || GEngine->GameViewport == nullptr)
    {
        return false;
    }
    GEngine->GameViewport->GetViewportSize(mViewportPx);

    // Waypoints at fixed offsets in the camera's axes, expressed relative to the anchor body
    const FMinimalViewInfo& pov = mController->PlayerCameraManager->GetCameraCacheView();
    const FRotationMatrix axes(pov.Rotation);
    const FVector3d bodyM = mRegistry->GetRegistry().GetPositionM(mBodyIndex);
    FSOLRouteWaypoint waypoints[OVERLAY_SMOKE_WAYPOINTS];
    for (int32 index = 0; index < OVERLAY_SMOKE_WAYPOINTS; ++index)
    {
        const FVector& offset = OVERLAY_SMOKE_OFFSETS_CM[index];
        const FVector renderCm = pov.Location + axes.GetScaledAxis(EAxis::X) * offset.X
            + axes.GetScaledAxis(EAxis::Y) * offset.Y + axes.GetScaledAxis(EAxis::Z) * offset.Z;
        waypoints[index] = SOLRoute::MakeWaypointAtPoint(mBodyIndex, mAnchor->RenderToUniverseM(renderCm), bodyM);
    }
    if (!mRoutes->StartRoute(ESOLRouteKind::PatrolLoop, waypoints))
    {
        return false;
    }
    mShips->DisengageAutopilot();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Aims the map camera at the route's waypoints
void FSOLRouteOverlaySmoke::AimMapCamera() const
{
    FVector3d centroidM = FVector3d::ZeroVector;
    int32 count = 0;
    for (int32 index = 0; index < mRoutes->GetWaypoints().Num(); ++index)
    {
        FVector3d pointM;
        if (mRoutes->GetWaypointPositionM(index, pointM))
        {
            centroidM += pointM;
            ++count;
        }
    }
    if (count == 0)
    {
        return;
    }
    FSOLOrbitCameraState state;
    state.FocusPositionM = centroidM / count;
    state.DistanceM = OVERLAY_SMOKE_MAP_DISTANCE_M;
    state.PitchRad = OVERLAY_SMOKE_MAP_PITCH_RAD;
    state.YawRad = OVERLAY_SMOKE_MAP_YAW_RAD;
    mMapMode->SetCameraState(state);
}

//////////////////////////////////////////////////////////////////////////
// Checks the route markers against the engine projection; label names the view
void FSOLRouteOverlaySmoke::CheckRouteMarkers(const TCHAR* label, const FVector3d& planePointM)
{
    const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
    const FSOLMapRouteOverlay& overlay = hud->GetRouteOverlay();
    int32 expectedVisible = 0;
    int32 matched = 0;
    double worstPx = 0.0;
    int32 planeMismatches = 0;
    for (int32 index = 0; index < mRoutes->GetWaypoints().Num(); ++index)
    {
        FVector3d pointM;
        FVector2D expectedPx;
        if (!mRoutes->GetWaypointPositionM(index, pointM) || !mController->ProjectWorldLocationToScreen(
            mAnchor->ComputePointRenderLocationCm(pointM), expectedPx, false))
        {
            continue;
        }
        if (expectedPx.X < 0.0 || expectedPx.Y < 0.0 || expectedPx.X > mViewportPx.X || expectedPx.Y > mViewportPx.Y)
        {
            continue;
        }
        ++expectedVisible;
        for (const FSOLMapRouteOverlay::FMarker& marker : overlay.GetLastMarkers())
        {
            if (!marker.bDraft && marker.WaypointIndex == index)
            {
                const double errorPx = FVector2D::Distance(marker.ScreenPx, expectedPx);
                worstPx = FMath::Max(worstPx, errorPx);
                matched += errorPx <= OVERLAY_SMOKE_TOLERANCE_PX ? 1 : 0;
                planeMismatches += marker.bBelowPlane != SOLRouteOverlayMath::IsBelowPlane(pointM, planePointM) ? 1 : 0;
                UE_LOG(LogSOL, Log, TEXT("SmokeRouteOverlay: %s waypoint %d drawn at (%.1f, %.1f), engine (%.1f, %.1f),"
                    " %s the plane"), label, index, marker.ScreenPx.X, marker.ScreenPx.Y, expectedPx.X, expectedPx.Y,
                    marker.bBelowPlane ? TEXT("below") : TEXT("above"));
            }
        }
    }
    const FString name = FString::Printf(TEXT("%s markers at the projected waypoints"), label);
    Check(*name, expectedVisible == OVERLAY_SMOKE_WAYPOINTS && matched == expectedVisible, FString::Printf(
        TEXT("%d/%d within %.1f px (worst %.2f px), %d expected on screen"), matched, expectedVisible,
        OVERLAY_SMOKE_TOLERANCE_PX, worstPx, expectedVisible));
    const FString planeName = FString::Printf(TEXT("%s above/below-plane markers"), label);
    Check(*planeName, planeMismatches == 0, FString::Printf(TEXT("%d mismatches"), planeMismatches));
}

//////////////////////////////////////////////////////////////////////////
// Advances to the next step
void FSOLRouteOverlaySmoke::NextStep()
{
    ++mStep;
    mStepFrames = 0;
    mStepTimeS = 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Logs one checkpoint as PASS or FAIL and counts failures
void FSOLRouteOverlaySmoke::Check(const TCHAR* name, const bool bPassed, const FString& detail)
{
    ++mChecks;
    if (!bPassed)
    {
        ++mFailures;
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeRouteOverlay: %s %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), name, *detail);
}
