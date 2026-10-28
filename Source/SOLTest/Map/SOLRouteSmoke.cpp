/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRouteSmoke.h"

#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLRouteSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
    // Frames each script step lasts (its entry actions run on the first, its checks after the last)
    constexpr int32 ROUTE_SMOKE_STEP_FRAMES[] =
    {
        90,                     // 0 settle
        2, 10,                  // 1-2 Shift, M: map open in Move mode
        6, 6, 6, 6, 6,          // 3-7 J, P, P, M, M
        3, 6, 3, 6,             // 8-11 two left clicks
        3, 3, 4,                // 12-14 Ctrl+right click (undo), release Ctrl
        3, 6,                   // 15-16 left click again
        12,                     // 17 Enter: route starts
        2, 15, 10,              // 18-20 Shift+M closes the map, Shift released
        12, 10, 12,             // 21-23 W pauses, W released, V resumes
        6, 6,                   // 24-25 P, P
        2, 10, 10,              // 26-28 Shift+M opens, Delete clears the route
        2, 10,                  // 29-30 Shift+M closes
        2, 10, 10,              // 31-33 Shift+J opens in Jump mode, Esc closes
        5,                      // 34 scripted short Move starts
        TNumericLimits<int32>::Max(),   // 35 fly it (ends early once finished, or after ROUTE_SMOKE_FLY_LIMIT_S)
        1,                      // 36 finish
    };
    constexpr int32 ROUTE_SMOKE_STEP_COUNT = UE_ARRAY_COUNT(ROUTE_SMOKE_STEP_FRAMES);

    // The scripted Move's flying step, its real-time limit and its legs (meters, ecliptic: ahead, then aside)
    constexpr int32 ROUTE_SMOKE_FLY_STEP = 35;
    constexpr double ROUTE_SMOKE_FLY_LIMIT_S = 240.0;
    const FVector3d ROUTE_SMOKE_FIRST_LEG_M(3000.0, 0.0, 0.0);
    const FVector3d ROUTE_SMOKE_SECOND_LEG_M(0.0, 3000.0, 0.0);

    // The scripted Move must end at rest relative to the waypoint's body within this speed (m/s)
    constexpr double ROUTE_SMOKE_REST_SPEED_MPS = 2.0;

    // Scripted map cursor offsets from the viewport centre (pixels) of the two waypoint clicks
    const FVector2D ROUTE_SMOKE_FIRST_CLICK_PX(120.0, 40.0);
    const FVector2D ROUTE_SMOKE_SECOND_CLICK_PX(-160.0, -60.0);

    //////////////////////////////////////////////////////////////////////////
    // Returns the viewport centre plus an offset (pixels), or the offset alone without a viewport
    FVector2D RouteSmokeCursorPx(const APlayerController& controller, const FVector2D& offsetPx)
    {
        int32 width = 0;
        int32 height = 0;
        controller.GetViewportSize(width, height);
        return FVector2D(0.5 * width, 0.5 * height) + offsetPx;
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and subsystems and starts the script
void FSOLRouteSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mRoutes = world->GetSubsystem<USOLRouteSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    UE_LOG(LogSOL, Log, TEXT("SmokeRoute: route run starting (%d steps)"), ROUTE_SMOKE_STEP_COUNT);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLRouteSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mIsFinished)
    {
        return true;
    }
    if (!mController.IsValid() || !mMapMode.IsValid() || !mRoutes.IsValid() || !mShips.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeRoute: controller or subsystems missing; aborting"));
        mIsFinished = true;
        UKismetSystemLibrary::QuitGame(pawn.GetWorld(), nullptr, EQuitPreference::Quit, false);
        return true;
    }

    // Keys tapped last frame are released first
    for (const FKey& key : mPendingReleases)
    {
        ReleaseKey(key);
    }
    mPendingReleases.Reset();

    if (!mHasEnteredStep)
    {
        mHasEnteredStep = true;
        mStepFrames = 0;
        EnterStep(mStep, pawn);
    }
    // The scripted Move's step watches the flight each frame and ends once the route is no longer Active
    if (mStep == ROUTE_SMOKE_FLY_STEP)
    {
        mFlyElapsedS += realDeltaSeconds;
        if (mRoutes->GetState() == ESOLRouteState::Active && mRoutes->GetCursor().LegsCompleted >= 1
            && mShips->GetFlightMode() == ESOLFlightMode::Auto)
        {
            mHasPassedInAuto = true;
        }
        if (mRoutes->GetState() == ESOLRouteState::Active && mFlyElapsedS < ROUTE_SMOKE_FLY_LIMIT_S)
        {
            return false;
        }
        mStepFrames = ROUTE_SMOKE_STEP_FRAMES[mStep];
    }
    else if (++mStepFrames < ROUTE_SMOKE_STEP_FRAMES[mStep])
    {
        return false;
    }
    CheckStep(mStep, pawn);
    mHasEnteredStep = false;
    if (++mStep >= ROUTE_SMOKE_STEP_COUNT)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeRoute: finished, %d/%d checks passed"), mChecks - mFailures, mChecks);
        mIsFinished = true;
        UKismetSystemLibrary::QuitGame(pawn.GetWorld(), nullptr, EQuitPreference::Quit, false);
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a step's entry actions (key presses, cursor moves)
void FSOLRouteSmoke::EnterStep(const int32 step, ASOLShipPawn& pawn)
{
    switch (step)
    {
    case 0:
        pawn.SetMapCursorScripted(true);
        break;
    case 1: case 18: case 26: case 29: case 31:
        PressKey(EKeys::LeftShift);
        break;
    case 2: case 19: case 27: case 30:
        TapKey(EKeys::M);
        break;
    case 3:
        ReleaseKey(EKeys::LeftShift);
        TapKey(EKeys::J);
        break;
    case 4: case 5: case 24: case 25:
        TapKey(EKeys::P);
        break;
    case 6: case 7:
        TapKey(EKeys::M);
        break;
    case 8:
        pawn.HandleMapCursorMoved(RouteSmokeCursorPx(*mController, ROUTE_SMOKE_FIRST_CLICK_PX));
        PressKey(EKeys::LeftMouseButton);
        break;
    case 10:
        pawn.HandleMapCursorMoved(RouteSmokeCursorPx(*mController, ROUTE_SMOKE_SECOND_CLICK_PX));
        PressKey(EKeys::LeftMouseButton);
        break;
    case 15:
        PressKey(EKeys::LeftMouseButton);
        break;
    case 9: case 11: case 16:
        ReleaseKey(EKeys::LeftMouseButton);
        break;
    case 12:
        PressKey(EKeys::LeftControl);
        PressKey(EKeys::RightMouseButton);
        break;
    case 13:
        ReleaseKey(EKeys::RightMouseButton);
        break;
    case 14:
        ReleaseKey(EKeys::LeftControl);
        break;
    case 17:
        TapKey(EKeys::Enter);
        break;
    case 20:
        ReleaseKey(EKeys::LeftShift);
        break;
    case 21:
        PressKey(EKeys::W);
        break;
    case 22:
        ReleaseKey(EKeys::W);
        break;
    case 23:
        TapKey(EKeys::V);
        break;
    case 28:
        ReleaseKey(EKeys::LeftShift);
        TapKey(EKeys::Delete);
        break;
    case 32:
        TapKey(EKeys::J);
        break;
    case 33:
        ReleaseKey(EKeys::LeftShift);
        TapKey(EKeys::Escape);
        break;
    case 34:
        StartScriptedMove(pawn);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a step's checks once its frames have passed
void FSOLRouteSmoke::CheckStep(const int32 step, ASOLShipPawn& pawn)
{
    const USOLMapModeSubsystem& map = *mMapMode;
    const USOLRouteSubsystem& routes = *mRoutes;
    const USOLShipSubsystem& ships = *mShips;
    const FSOLMapModeState& mode = map.GetModeState();
    const bool bAuto = ships.GetFlightMode() == ESOLFlightMode::Auto;
    const ESOLRouteState state = routes.GetState();
    const FString status = FString::Printf(TEXT("map %s, mode %d%s, draft %d, route state %d (%d waypoints), flight "
        "mode %d, point order %d"), pawn.IsMapOpen() ? TEXT("open") : TEXT("closed"), static_cast<int32>(mode.Mode),
        mode.bPatrolPingPong ? TEXT(" ping-pong") : TEXT(""), mode.Draft.Num(), static_cast<int32>(state),
        routes.GetWaypoints().Num(), static_cast<int32>(ships.GetFlightMode()), ships.IsAutopilotPointOrder());
    switch (step)
    {
    case 0:
        Check(TEXT("a-ship"), ships.HasPlayerShip(), status);
        break;
    case 2:
        Check(TEXT("b-open-move"), pawn.IsMapOpen() && mode.Mode == ESOLMapMode::Move, status);
        break;
    case 3:
        Check(TEXT("c-mode-jump"), mode.Mode == ESOLMapMode::Jump, status);
        break;
    case 4:
        Check(TEXT("c-mode-patrol"), mode.Mode == ESOLMapMode::Patrol && !mode.bPatrolPingPong, status);
        break;
    case 5:
        Check(TEXT("c-patrol-flip"), mode.Mode == ESOLMapMode::Patrol && mode.bPatrolPingPong, status);
        break;
    case 7:
        Check(TEXT("c-mode-move"), pawn.IsMapOpen() && mode.Mode == ESOLMapMode::Move && mode.Draft.IsEmpty(), status);
        break;
    case 9:
        Check(TEXT("d-waypoint-1"), mode.Draft.Num() == 1, status);
        break;
    case 11:
        Check(TEXT("d-waypoint-2"), mode.Draft.Num() == 2, status);
        break;
    case 14:
        Check(TEXT("d-undo"), mode.Draft.Num() == 1, status);
        break;
    case 16:
        Check(TEXT("d-waypoint-again"), mode.Draft.Num() == 2, status);
        break;
    case 17:
        Check(TEXT("e-start"), state == ESOLRouteState::Active && routes.GetWaypoints().Num() == 2
            && mode.Draft.IsEmpty() && bAuto && ships.IsAutopilotPointOrder(), status);
        break;
    case 19:
        Check(TEXT("f-close-keeps-auto"), !pawn.IsMapOpen() && state == ESOLRouteState::Active && bAuto, status);
        break;
    case 20:
        Check(TEXT("f-shift-released"), state == ESOLRouteState::Active && bAuto, status);
        break;
    case 21:
        Check(TEXT("g-pause"), state == ESOLRouteState::Paused && !bAuto && routes.GetWaypoints().Num() == 2, status);
        break;
    case 22:
        Check(TEXT("g-still-paused"), state == ESOLRouteState::Paused, status);
        break;
    case 23:
        Check(TEXT("h-resume"), state == ESOLRouteState::Active && bAuto && ships.IsAutopilotPointOrder(), status);
        break;
    case 24:
        Check(TEXT("i-overlay-off"), !routes.IsOverlayVisible(), status);
        break;
    case 25:
        Check(TEXT("i-overlay-on"), routes.IsOverlayVisible(), status);
        break;
    case 27:
        Check(TEXT("j-map-open"), pawn.IsMapOpen(), status);
        break;
    case 28:
        Check(TEXT("j-delete"), state == ESOLRouteState::None && !bAuto, status);
        break;
    case 30:
        Check(TEXT("j-map-closed"), !pawn.IsMapOpen(), status);
        break;
    case 32:
        Check(TEXT("k-open-jump"), pawn.IsMapOpen() && mode.Mode == ESOLMapMode::Jump, status);
        break;
    case 33:
        Check(TEXT("k-esc-close"), !pawn.IsMapOpen(), status);
        break;
    case 34:
        Check(TEXT("l-move-start"), state == ESOLRouteState::Active && bAuto && routes.GetWaypoints().Num() == 2,
            status);
        break;
    case 35:
    {
        // At rest relative to the last waypoint's body, back in assisted flight, the first waypoint passed in AUTO
        const USOLBodyRegistrySubsystem* registry = pawn.GetWorld()->GetSubsystem<USOLBodyRegistrySubsystem>();
        double restSpeedMps = TNumericLimits<double>::Max();
        if (registry != nullptr && mFlyBodyIndex >= 0 && mFlyBodyIndex < registry->GetUnrealFrameCache().Num())
        {
            restSpeedMps = (ships.GetState().VelocityMps
                - registry->GetUnrealFrameCache().VelocitiesMps[mFlyBodyIndex]).Size();
        }
        Check(TEXT("l-pass-kept-auto"), mHasPassedInAuto, status);
        Check(TEXT("l-move-finished"), state == ESOLRouteState::Finished && !bAuto
            && restSpeedMps <= ROUTE_SMOKE_REST_SPEED_MPS && routes.GetCursor().LegsCompleted == 2,
            FString::Printf(TEXT("%s; %.1f s, speed %.2f m/s relative to the body"), *status, mFlyElapsedS,
            restSpeedMps));
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the ship at rest beside its anchor body and starts a two-waypoint Move (ahead, then aside: a 90 deg corner)
void FSOLRouteSmoke::StartScriptedMove(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const USOLAnchorSubsystem* anchor = world->GetSubsystem<USOLAnchorSubsystem>();
    const USOLBodyRegistrySubsystem* registry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mFlyBodyIndex = anchor != nullptr ? anchor->GetAnchorIndex() : INDEX_NONE;
    if (registry == nullptr || mFlyBodyIndex < 0 || mFlyBodyIndex >= registry->GetUnrealFrameCache().Num())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeRoute: no anchor body for the scripted Move"));
        return;
    }

    // Test setup (a verification teleport, as the other smokes do): at rest relative to the body, no spin
    FSOLShipState state = mShips->GetState();
    state.VelocityMps = registry->GetUnrealFrameCache().VelocitiesMps[mFlyBodyIndex];
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    mShips->SetState(state);

    // Waypoints relative to the body (ecliptic), from the ship's position now
    const FVector3d bodyM = registry->GetRegistry().GetPositionM(mFlyBodyIndex);
    const FVector3d firstM = mShips->GetUniversePositionM() + ROUTE_SMOKE_FIRST_LEG_M;
    const FSOLRouteWaypoint waypoints[] =
    {
        SOLRoute::MakeWaypointAtPoint(mFlyBodyIndex, firstM, bodyM),
        SOLRoute::MakeWaypointAtPoint(mFlyBodyIndex, firstM + ROUTE_SMOKE_SECOND_LEG_M, bodyM),
    };
    mFlyElapsedS = 0.0;
    mHasPassedInAuto = false;
    mRoutes->StartRoute(ESOLRouteKind::Move, waypoints);
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller
void FSOLRouteSmoke::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller
void FSOLRouteSmoke::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a key now and releases it on the next frame
void FSOLRouteSmoke::TapKey(const FKey& key)
{
    PressKey(key);
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Logs one checkpoint as PASS or FAIL and counts failures
void FSOLRouteSmoke::Check(const TCHAR* name, const bool bPassed, const FString& detail)
{
    ++mChecks;
    if (!bPassed)
    {
        ++mFailures;
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeRoute: %s %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), name, *detail);
}
