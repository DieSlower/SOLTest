/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLJumpSmoke.h"

#include "Game/SOLGameMode.h"
#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapCamera.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLMapPicking.h"
#include "Map/SOLWarpCurve.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"

namespace
{
    // Phase durations (real seconds) and frame counts
    constexpr double JUMP_SMOKE_SETUP_S = 1.0;
    constexpr double JUMP_SMOKE_TAP_PHASE_S = 0.5;          // Long enough for a tapped key to take effect
    constexpr double JUMP_SMOKE_FOCUS_S = 1.0;              // The HUD projects Mars from the new camera a few times
    constexpr double JUMP_SMOKE_RESTORE_S = 1.5;            // Normal flight view settles before the arrival shot
    constexpr double JUMP_SMOKE_FINISH_S = 1.0;
    constexpr double JUMP_SMOKE_WAIT_TIMEOUT_S = 10.0;      // A wait that never ends fails its check
    constexpr int32 JUMP_SMOKE_DRAG_FRAMES = 10;            // Frames of mouse motion in the drag
    constexpr int32 JUMP_SMOKE_SETTLE_FRAMES = 5;           // Frames for the last injected event to be processed
    constexpr int32 JUMP_SMOKE_START_FRAMES = 4;            // Frames after Enter before the start is checked

    // Map camera on Mars: 150,000 km out at the default pitch, so a 100 px drag is ~14,000 km (Mars radius 3,390 km)
    constexpr double JUMP_SMOKE_CAMERA_DISTANCE_M = 1.5e8;
    constexpr float JUMP_SMOKE_DRAG_X_PX = 10.0f;           // 100 px right, 20 px up
    constexpr float JUMP_SMOKE_DRAG_Y_PX = 2.0f;
    constexpr double JUMP_SMOKE_MIN_OFFSET_RADII = 1.5;     // The destination must be clear of Mars's surface

    // Verdict tolerances
    constexpr double JUMP_SMOKE_FOV_TOLERANCE_DEG = 1.0e-3; // Float FOV against the double curve
    constexpr double JUMP_SMOKE_WEIGHT_TOLERANCE = 1.0e-5;  // Float blend weight against the double curve
    constexpr double JUMP_SMOKE_NEAR_PEAK_FRACTION = 0.99;  // Midpoint: at least 99% of the pulse
    constexpr double JUMP_SMOKE_POSITION_TOLERANCE_M = 1.0; // Double rounding at ~2e11 m coordinates
    constexpr double JUMP_SMOKE_VELOCITY_TOLERANCE_MPS = 1.0e-3;
    constexpr double JUMP_SMOKE_ORIENTATION_TOLERANCE_RAD = 1.0e-3;
    constexpr double JUMP_SMOKE_MIN_LIVE_DRIFT_M = 1.0e3;   // Mars moves ~72 km in 3 s: the arrival must follow it
    constexpr double JUMP_SMOKE_MAX_LIVE_DRIFT_M = 1.0e6;
    constexpr double JUMP_SMOKE_MAX_PAWN_LOCATION_CM = 1.0e6; // The pawn must sit near Unreal's origin after the snap
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and subsystems and starts the first phase
void FSOLJumpSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mJump = world->GetSubsystem<USOLJumpSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mTargeting = world->GetSubsystem<USOLTargetingSubsystem>();

    // The script places the map cursor itself; the real mouse must not move it
    pawn.SetMapCursorScripted(true);
    UE_LOG(LogSOL, Log, TEXT("SmokeJump: scripted pick-and-jump input starting (controller %s)"),
        mController.IsValid() ? *mController->GetName() : TEXT("none"));
    mPhase = ESOLJumpPhase::Setup;
    EnterPhase(mPhase, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLJumpSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLJumpPhase::Done)
    {
        return true;
    }
    if (!mController.IsValid() || !mMapMode.IsValid() || !mJump.IsValid() || !mShips.IsValid() || !mAnchor.IsValid()
        || !mRegistry.IsValid() || !mTargeting.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeJump: controller or subsystems missing; aborting"));
        mPhase = ESOLJumpPhase::Done;
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
    TickPhase(mPhase);
    if (!IsPhaseDone(mPhase, pawn))
    {
        return false;
    }

    // Phase over: checks, then the next phase
    ExitPhase(mPhase, pawn);
    mPhase = static_cast<ESOLJumpPhase>(static_cast<uint8>(mPhase) + 1);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    if (mPhase == ESOLJumpPhase::Done)
    {
        return true;
    }
    EnterPhase(mPhase, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions (key presses, cursor moves, camera placement)
void FSOLJumpSmoke::EnterPhase(const ESOLJumpPhase phase, ASOLShipPawn& pawn)
{
    switch (phase)
    {
    case ESOLJumpPhase::LockFrame:
        // Nothing selected yet, so M locks the frame to the anchor body (the assist frame does not change)
        TapKey(EKeys::M);
        break;
    case ESOLJumpPhase::SelectTarget:
        TapKey(EKeys::R);
        break;
    case ESOLJumpPhase::OpenJ:
        TapKey(EKeys::J);
        break;
    case ESOLJumpPhase::EnterEmpty:
    case ESOLJumpPhase::EnterJump:
        TapKey(EKeys::Enter);
        break;
    case ESOLJumpPhase::FocusMars:
    {
        // Straight onto Mars (a pan cannot leave the ecliptic plane, and Mars sits off it)
        mMarsIndex = mRegistry->GetRegistry().FindByName(FName(SOL::BodyNames::MARS));
        if (mMarsIndex != INDEX_NONE)
        {
            FSOLOrbitCameraState state = mMapMode->GetCameraState();
            state.FocusPositionM = mRegistry->GetRegistry().GetPositionM(mMarsIndex);
            state.DistanceM = JUMP_SMOKE_CAMERA_DISTANCE_M;
            mMapMode->SetCameraState(state);
        }
        break;
    }
    case ESOLJumpPhase::ClickMars:
        // Cursor on Mars first, then the button: the pawn picks at the cursor when the press is processed
        pawn.HandleMapCursorMoved(mMarsScreenPx);
        PressKey(EKeys::LeftMouseButton);
        break;
    case ESOLJumpPhase::ReleaseMars:
        ReleaseKey(EKeys::LeftMouseButton);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's per-frame actions (mouse deltas)
void FSOLJumpSmoke::TickPhase(const ESOLJumpPhase phase)
{
    if (phase == ESOLJumpPhase::DragMars && mPhaseFrames <= JUMP_SMOKE_DRAG_FRAMES)
    {
        InjectAxis(EKeys::MouseX, JUMP_SMOKE_DRAG_X_PX);
        InjectAxis(EKeys::MouseY, JUMP_SMOKE_DRAG_Y_PX);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true once a phase may end (its minimum time and frames, or the condition it waits for)
bool FSOLJumpSmoke::IsPhaseDone(const ESOLJumpPhase phase, const ASOLShipPawn& pawn) const
{
    const double halfDurationS = 0.5 * SOL::WARP_DURATION_S;
    switch (phase)
    {
    case ESOLJumpPhase::Setup:
        return mPhaseTimeS >= JUMP_SMOKE_SETUP_S;
    case ESOLJumpPhase::LockFrame:
    case ESOLJumpPhase::SelectTarget:
    case ESOLJumpPhase::OpenJ:
    case ESOLJumpPhase::EnterEmpty:
        return mPhaseTimeS >= JUMP_SMOKE_TAP_PHASE_S && mPhaseFrames >= JUMP_SMOKE_SETTLE_FRAMES;
    case ESOLJumpPhase::FocusMars:
        return mPhaseTimeS >= JUMP_SMOKE_FOCUS_S && mPhaseFrames >= JUMP_SMOKE_SETTLE_FRAMES;
    case ESOLJumpPhase::DragMars:
        return mPhaseFrames >= JUMP_SMOKE_DRAG_FRAMES + JUMP_SMOKE_SETTLE_FRAMES;
    case ESOLJumpPhase::EnterJump:
        return mPhaseFrames >= JUMP_SMOKE_START_FRAMES;
    case ESOLJumpPhase::WaitMid:
        // The pawn applied the effect for a sequence time at or past the midpoint (or the warp is gone: a failure)
        return (pawn.IsWarpEffectActive() && pawn.GetWarpEffectElapsedS() >= halfDurationS) || !mJump->IsWarping()
            || mPhaseTimeS >= JUMP_SMOKE_WAIT_TIMEOUT_S;
    case ESOLJumpPhase::WaitArrival:
        return mJump->GetCompletedJumpCount() > 0 || mPhaseTimeS >= JUMP_SMOKE_WAIT_TIMEOUT_S;
    case ESOLJumpPhase::Restore:
        return mPhaseTimeS >= JUMP_SMOKE_RESTORE_S && mPhaseFrames >= JUMP_SMOKE_SETTLE_FRAMES;
    case ESOLJumpPhase::Finish:
        return mPhaseTimeS >= JUMP_SMOKE_FINISH_S;
    default:
        return mPhaseFrames >= JUMP_SMOKE_SETTLE_FRAMES;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks
void FSOLJumpSmoke::ExitPhase(const ESOLJumpPhase phase, ASOLShipPawn& pawn)
{
    const FSOLMapPickState& pick = mMapMode->GetPickState();
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
    switch (phase)
    {
    case ESOLJumpPhase::SelectTarget:
        Check(TEXT("a0-lock-and-target"), mTargeting->IsFrameLocked() && mTargeting->GetSelectedIndex() != INDEX_NONE,
            FString::Printf(TEXT("before the jump: frame locked %s (to %s), selected target index %d"),
            mTargeting->IsFrameLocked() ? TEXT("yes") : TEXT("NO"), *mTargeting->GetFrameName().ToString(),
            mTargeting->GetSelectedIndex()));
        break;
    case ESOLJumpPhase::OpenJ:
        Check(TEXT("a-open"), pawn.IsMapOpen() && mMapMode->IsMapOpen() && !pick.bHasReference, FString::Printf(
            TEXT("map open %s, no pick yet %s"), mMapMode->IsMapOpen() ? TEXT("yes") : TEXT("NO"), !pick.bHasReference
            ? TEXT("yes") : TEXT("NO")));
        break;
    case ESOLJumpPhase::EnterEmpty:
        Check(TEXT("a2-enter-no-pick"), pawn.IsMapOpen() && !mJump->IsWarping() && mMapMode->GetJumpRequestCount() == 0
            && pawn.GetWarpPostProcessWeight() == 0.0f, FString::Printf(TEXT("Enter with nothing locked: map still "
            "open %s, warping %s, jump requests %d"), pawn.IsMapOpen() ? TEXT("yes") : TEXT("NO"), mJump->IsWarping()
            ? TEXT("YES") : TEXT("no"), mMapMode->GetJumpRequestCount()));
        break;
    case ESOLJumpPhase::FocusMars:
    {
        const TConstArrayView<FVector2D> screenPx = hud != nullptr ? hud->GetMapBodyOverlay().GetScreenPositionsPx()
            : TConstArrayView<FVector2D>();
        const TConstArrayView<double> pickRadiiPx = hud != nullptr ? hud->GetMapBodyOverlay().GetPickRadiiPx()
            : TConstArrayView<double>();
        const bool bOnScreen = screenPx.IsValidIndex(mMarsIndex) && pickRadiiPx.IsValidIndex(mMarsIndex)
            && pickRadiiPx[mMarsIndex] > 0.0;
        mMarsScreenPx = bOnScreen ? screenPx[mMarsIndex] : FVector2D::ZeroVector;
        Check(TEXT("b-mars-focused"), bOnScreen, FString::Printf(TEXT("map camera %.4g m from Mars (index %d): Mars "
            "at (%.1f, %.1f) px, pick radius %.1f px"), mMapMode->GetCameraState().DistanceM, mMarsIndex,
            mMarsScreenPx.X, mMarsScreenPx.Y, bOnScreen ? pickRadiiPx[mMarsIndex] : 0.0));
        break;
    }
    case ESOLJumpPhase::ClickMars:
        Check(TEXT("c-mars-reference"), pick.bHasReference && pick.bReferenceIsBody
            && pick.ReferenceBodyIndex == mMarsIndex && mMapMode->IsPickDragging(), FString::Printf(TEXT("mouse-down "
            "on Mars: reference body %s, index %d (want %d), dragging %s"), pick.bReferenceIsBody ? TEXT("yes")
            : TEXT("NO"), pick.ReferenceBodyIndex, mMarsIndex, mMapMode->IsPickDragging() ? TEXT("yes") : TEXT("no")));
        break;
    case ESOLJumpPhase::ReleaseMars:
        CheckLockAndAimShip();
        break;
    case ESOLJumpPhase::EnterJump:
    {
        // The sequence started: the map closed through its normal path (pick cleared, ship input back), the FOV
        // pulse and post-process are rising and the HUD shows only the streaks
        const double fovRiseDeg = pawn.GetCameraFovDeg() - pawn.GetWarpBaseFovDeg();
        const bool bPassed = mJump->IsWarping() && mMapMode->GetJumpRequestCount() == 1 && !pawn.IsMapOpen()
            && !mMapMode->IsMapOpen() && !pick.bHasReference && pawn.IsShipInputMapped() && !pawn.IsMapInputMapped()
            && pawn.IsWarpEffectActive() && fovRiseDeg > 0.0 && pawn.GetWarpPostProcessWeight() > 0.0f
            && hud != nullptr && !hud->WasFlightHudDrawn() && hud->GetLastWarpStreaksDrawn() > 0;
        Check(TEXT("e-warp-started"), bPassed, FString::Printf(TEXT("Enter: warping %s at %.3f s, map closed %s, pick "
            "cleared %s, ship input back %s; FOV %.3f deg (base %.3f, +%.3f), post-process weight %.4f, flight HUD "
            "hidden %s, streaks drawn %d"), mJump->IsWarping() ? TEXT("yes") : TEXT("NO"), mJump->GetElapsedS(),
            !mMapMode->IsMapOpen() ? TEXT("yes") : TEXT("NO"), !pick.bHasReference ? TEXT("yes") : TEXT("NO"),
            pawn.IsShipInputMapped() ? TEXT("yes") : TEXT("NO"), pawn.GetCameraFovDeg(), pawn.GetWarpBaseFovDeg(),
            fovRiseDeg, pawn.GetWarpPostProcessWeight(), hud != nullptr && !hud->WasFlightHudDrawn() ? TEXT("yes")
            : TEXT("NO"), hud != nullptr ? hud->GetLastWarpStreaksDrawn() : -1));
        break;
    }
    case ESOLJumpPhase::WaitMid:
    {
        // The FOV and weight are exactly SOLWarpCurve's values for the sequence time the pawn used, near the peak
        const double elapsedS = pawn.GetWarpEffectElapsedS();
        const double baseDeg = pawn.GetWarpBaseFovDeg();
        const double expectedFovDeg = SOLWarpCurve::ComputeFovDeg(elapsedS, SOL::WARP_DURATION_S, baseDeg,
            SOL::WARP_PEAK_FOV_DEG);
        const double expectedWeight = SOLWarpCurve::ComputeStreakIntensity(elapsedS, SOL::WARP_DURATION_S,
            SOL::WARP_PEAK_STREAK_INTENSITY);
        const double fovDeg = pawn.GetCameraFovDeg();
        const double weight = pawn.GetWarpPostProcessWeight();
        const bool bPassed = mJump->IsWarping() && FMath::Abs(fovDeg - expectedFovDeg) <= JUMP_SMOKE_FOV_TOLERANCE_DEG
            && FMath::Abs(weight - expectedWeight) <= JUMP_SMOKE_WEIGHT_TOLERANCE
            && fovDeg - baseDeg >= JUMP_SMOKE_NEAR_PEAK_FRACTION * (SOL::WARP_PEAK_FOV_DEG - baseDeg)
            && weight >= JUMP_SMOKE_NEAR_PEAK_FRACTION * SOL::WARP_PEAK_STREAK_INTENSITY && hud != nullptr
            && !hud->WasFlightHudDrawn() && hud->GetLastWarpStreaksDrawn() == SOL::WARP_STREAK_COUNT;
        Check(TEXT("f-mid-peak"), bPassed, FString::Printf(TEXT("t = %.4f s of %.1f (u = %.4f): FOV %.4f deg (curve "
            "%.4f, peak %.1f, base %.3f), post-process weight %.5f (curve %.5f, peak %.1f), flight HUD hidden %s, "
            "streaks %d"), elapsedS, SOL::WARP_DURATION_S, elapsedS / SOL::WARP_DURATION_S, fovDeg, expectedFovDeg,
            SOL::WARP_PEAK_FOV_DEG, baseDeg, weight, expectedWeight, SOL::WARP_PEAK_STREAK_INTENSITY, hud != nullptr
            && !hud->WasFlightHudDrawn() ? TEXT("yes") : TEXT("NO"), hud != nullptr ? hud->GetLastWarpStreaksDrawn()
            : -1));
        Screenshot(pawn, TEXT("mid-warp"));
        break;
    }
    case ESOLJumpPhase::WaitArrival:
        CheckArrival(pawn);
        break;
    case ESOLJumpPhase::Restore:
    {
        // The FOV went back to its pre-warp value and now eases by speed (never above it: relative speed is ~0)
        const double fovDeg = pawn.GetCameraFovDeg();
        const bool bFovRestored = fovDeg <= pawn.GetWarpBaseFovDeg() + JUMP_SMOKE_FOV_TOLERANCE_DEG
            && fovDeg >= SOL::SHIP_CAMERA_BASE_FOV_DEG - JUMP_SMOKE_FOV_TOLERANCE_DEG;
        const bool bPassed = !mJump->IsWarping() && !pawn.IsWarpEffectActive() && bFovRestored
            && pawn.GetWarpPostProcessWeight() == 0.0f && hud != nullptr && hud->WasFlightHudDrawn()
            && hud->GetLastWarpStreaksDrawn() == 0 && !pick.bHasReference && !mMapMode->IsMapOpen()
            && pawn.IsShipInputMapped();
        Check(TEXT("h-restored"), bPassed, FString::Printf(TEXT("after the warp: FOV %.3f deg (pre-warp %.3f, speed "
            "baseline %.1f), post-process weight %.4f, flight HUD drawn %s, streaks %d, pick cleared %s, map closed "
            "%s, ship input %s"), fovDeg, pawn.GetWarpBaseFovDeg(), SOL::SHIP_CAMERA_BASE_FOV_DEG,
            pawn.GetWarpPostProcessWeight(), hud != nullptr && hud->WasFlightHudDrawn() ? TEXT("yes") : TEXT("NO"),
            hud != nullptr ? hud->GetLastWarpStreaksDrawn() : -1, !pick.bHasReference ? TEXT("yes") : TEXT("NO"),
            !mMapMode->IsMapOpen() ? TEXT("yes") : TEXT("NO"), pawn.IsShipInputMapped() ? TEXT("yes") : TEXT("NO")));
        Screenshot(pawn, TEXT("arrival"));
        break;
    }
    case ESOLJumpPhase::Finish:
    {
        double altitudeM = 0.0;
        const int32 nearest = mShips->FindNearestBody(altitudeM);
        UE_LOG(LogSOL, Log, TEXT("SmokeJump: complete after %.1f s, %d/%d checks passed%s; nearest body %s at %.0f km"),
            mTotalTimeS, mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("") : TEXT(" (FAILURES)"),
            nearest != INDEX_NONE ? *registry.GetName(nearest).ToString() : TEXT("none"),
            altitudeM / SOL::METERS_PER_KM);
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
// Checks that the pick was locked on Mars and turns the ship to face Mars from the destination
void FSOLJumpSmoke::CheckLockAndAimShip()
{
    const FSOLMapPickState& pick = mMapMode->GetPickState();
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    FVector3d destinationM = FVector3d::ZeroVector;
    const bool bHasDestination = mMapMode->GetLiveDestinationM(destinationM);
    mLockedOffsetM = FVector2D(pick.PlanarOffsetXM, pick.PlanarOffsetYM);
    const double marsRadiusM = mMarsIndex != INDEX_NONE ? registry.GetRadiusM(mMarsIndex) : 0.0;
    Check(TEXT("d-planar-locked"), bHasDestination && pick.bPlanarLocked && !pick.bHeightLocked
        && pick.ReferenceBodyIndex == mMarsIndex && mLockedOffsetM.Size() > JUMP_SMOKE_MIN_OFFSET_RADII * marsRadiusM,
        FString::Printf(TEXT("mouse-up: planar offset (%.6g, %.6g) m from Mars (%.3g Mars radii), height unlocked %s, "
        "live destination %s (%.9g, %.9g, %.9g) m"), pick.PlanarOffsetXM, pick.PlanarOffsetYM, marsRadiusM > 0.0
        ? mLockedOffsetM.Size() / marsRadiusM : 0.0, !pick.bHeightLocked ? TEXT("yes") : TEXT("NO"), bHasDestination
        ? TEXT("valid") : TEXT("NONE"), destinationM.X, destinationM.Y, destinationM.Z));

    // Test setup: the ship faces from the destination back toward Mars (the offset reversed), with no spin, so the
    // arrival shot frames Mars. Ship state is in the Unreal-handed frame
    FSOLShipState state = mShips->GetState();
    const FVector3d towardMars = SOLRender::EclipticToUnreal(FVector3d(-mLockedOffsetM.X, -mLockedOffsetM.Y, 0.0))
        .GetSafeNormal();
    if (!towardMars.IsZero())
    {
        state.Orientation = FRotationMatrix::MakeFromXZ(towardMars, FVector3d::UpVector).ToQuat();
        state.AngularVelocityRadPerS = FVector3d::ZeroVector;
        mShips->SetState(state);
    }
    mOrientationAtEnter = state.Orientation;
    UE_LOG(LogSOL, Log, TEXT("SmokeJump: setup, ship turned to face Mars from the destination before Enter"));
}

//////////////////////////////////////////////////////////////////////////
// Checks the arrival: position, velocity, orientation, anchor, render origin, target and frame lock cleared, and the
// warp effect ended on the arrival frame
void FSOLJumpSmoke::CheckArrival(const ASOLShipPawn& pawn)
{
    // Runs on the frame after the arrival, before this frame's universe update: the ship still holds the teleported
    // state and the bodies are where they were at the arrival
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLShipState state = mShips->GetState();
    const FVector3d shipM = mShips->GetUniversePositionM();
    const FVector3d marsM = mMarsIndex != INDEX_NONE ? registry.GetPositionM(mMarsIndex) : FVector3d::ZeroVector;
    const FVector3d expectedM = SOLMapPicking::ComposeDestination(marsM, mLockedOffsetM.X, mLockedOffsetM.Y, 0.0,
        SOL::MAP_PICK_ECLIPTIC_X, SOL::MAP_PICK_ECLIPTIC_Y, SOL::MAP_PICK_UP);
    const double positionErrorM = FVector3d::Dist(shipM, expectedM);
    const FVector3d marsVelocityMps = mMarsIndex != INDEX_NONE
        ? SOLRender::EclipticToUnreal(registry.GetVelocityMps(mMarsIndex)) : FVector3d::ZeroVector;
    const double velocityErrorMps = FVector3d::Dist(state.VelocityMps, marsVelocityMps);
    const double liveDriftM = FVector3d::Dist(shipM, mMapMode->GetLastJumpRequestM());
    Check(TEXT("g1-arrival-position"), mJump->GetCompletedJumpCount() == 1 && positionErrorM
        <= JUMP_SMOKE_POSITION_TOLERANCE_M && liveDriftM >= JUMP_SMOKE_MIN_LIVE_DRIFT_M
        && liveDriftM <= JUMP_SMOKE_MAX_LIVE_DRIFT_M, FString::Printf(TEXT("arrived: ship (%.9g, %.9g, %.9g) m, Mars "
        "now + locked offset (%.9g, %.9g, %.9g) m, %.4g m apart; %.4g km from the destination as it was at Enter "
        "(Mars moved during the warp: re-evaluated live)"), shipM.X, shipM.Y, shipM.Z, expectedM.X, expectedM.Y,
        expectedM.Z, positionErrorM, liveDriftM / SOL::METERS_PER_KM));
    Check(TEXT("g2-arrival-velocity"), velocityErrorMps <= JUMP_SMOKE_VELOCITY_TOLERANCE_MPS
        && mJump->GetLastArrivalVelocityBodyIndex() == mMarsIndex, FString::Printf(TEXT("ship velocity (%.6f, %.6f, "
        "%.6f) m/s, Mars (%.6f, %.6f, %.6f) m/s, %.3g m/s apart; matched body index %d (want %d)"),
        state.VelocityMps.X, state.VelocityMps.Y, state.VelocityMps.Z, marsVelocityMps.X, marsVelocityMps.Y,
        marsVelocityMps.Z, velocityErrorMps, mJump->GetLastArrivalVelocityBodyIndex(), mMarsIndex));

    // Orientation kept; the anchor is Mars and the render origin (and the pawn with it) snapped onto the ship
    const double turnRad = mOrientationAtEnter.AngularDistance(state.Orientation);
    const double originOffsetM = FVector3d::Dist(mAnchor->GetRenderOriginM(), mAnchor->GetObserverPositionM());
    const double pawnLocationCm = pawn.GetActorLocation().Size();
    const bool bAnchorMars = mAnchor->GetAnchorIndex() == mMarsIndex;
    Check(TEXT("g3-orientation-rebase"), turnRad <= JUMP_SMOKE_ORIENTATION_TOLERANCE_RAD && bAnchorMars
        && originOffsetM <= SOL::RENDER_REBASE_DISTANCE_M && pawnLocationCm <= JUMP_SMOKE_MAX_PAWN_LOCATION_CM,
        FString::Printf(TEXT("orientation turned %.3g rad since Enter; anchor %s; render origin %.4g m from the ship, "
        "pawn %.4g cm from Unreal's origin"), turnRad, mAnchor->GetAnchorIndex() != INDEX_NONE
        ? *registry.GetName(mAnchor->GetAnchorIndex()).ToString() : TEXT("none"), originOffsetM, pawnLocationCm));

    // The M lock and the selection referred to the old location: both cleared by the arrival
    Check(TEXT("g4-lock-target-cleared"), !mTargeting->IsFrameLocked() && mTargeting->GetSelectedIndex() == INDEX_NONE,
        FString::Printf(TEXT("after the arrival: frame locked %s (frame %s), selected target index %d"),
        mTargeting->IsFrameLocked() ? TEXT("YES") : TEXT("no"), *mTargeting->GetFrameName().ToString(),
        mTargeting->GetSelectedIndex()));

    // Runs before this frame's pawn FOV update: the effect already ended on the arrival frame itself
    Check(TEXT("g5-effect-ended"), !pawn.IsWarpEffectActive() && pawn.GetWarpPostProcessWeight() == 0.0f,
        FString::Printf(TEXT("on the arrival frame: warp effect active %s, FOV %.3f deg (pre-warp %.3f), "
        "post-process weight %.4f"), pawn.IsWarpEffectActive() ? TEXT("YES") : TEXT("no"), pawn.GetCameraFovDeg(),
        pawn.GetWarpBaseFovDeg(), pawn.GetWarpPostProcessWeight()));
}

//////////////////////////////////////////////////////////////////////////
// Takes a verification screenshot and logs its name
void FSOLJumpSmoke::Screenshot(const ASOLShipPawn& pawn, const TCHAR* name) const
{
    UE_LOG(LogSOL, Log, TEXT("SmokeJump: screenshot (%s)"), name);
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->CaptureScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller
void FSOLJumpSmoke::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller
void FSOLJumpSmoke::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a key now and releases it on the next frame
void FSOLJumpSmoke::TapKey(const FKey& key)
{
    PressKey(key);
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Sends one analog axis sample (mouse delta in pixels) to the player controller
void FSOLJumpSmoke::InjectAxis(const FKey& key, const float delta) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Axis, delta, 1));
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLJumpSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeJump CHECK %-22s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
