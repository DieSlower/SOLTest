/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapSmoke.h"

#include "Game/SOLGameMode.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"

namespace
{
    // Phase durations (real seconds) and frame counts
    constexpr double MAP_SMOKE_SETUP_S = 1.0;
    constexpr double MAP_SMOKE_TAP_PHASE_S = 0.5;           // Long enough for a tapped key to take effect
    constexpr double MAP_SMOKE_HOLD_W_S = 0.5;
    constexpr double MAP_SMOKE_SHOT_SETTLE_S = 2.0;         // Frames rendered before a screenshot
    constexpr double MAP_SMOKE_FINISH_S = 1.0;
    constexpr int32 MAP_SMOKE_DRAG_FRAMES = 10;             // Frames of mouse motion per drag
    constexpr int32 MAP_SMOKE_WHEEL_FRAMES = 3;             // One wheel notch per frame
    constexpr int32 MAP_SMOKE_SETTLE_FRAMES = 5;            // Frames for the last injected event to be processed
    constexpr int32 MAP_SMOKE_CLOSE_ZOOM_NOTCHES = 67;      // 1e13 m / 1.2^67 = ~5e7 m: Earth ~100 px across

    // Drag and wheel amounts (pixels per frame, X right / Y up; wheel notches, + = forward = zoom in)
    constexpr float MAP_SMOKE_ORBIT_X_PX = 20.0f;           // 200 px right: yaw -1.0 rad at 0.005 rad/px
    constexpr float MAP_SMOKE_ORBIT_Y_PX = -10.0f;          // 100 px down: pitch 0.5 -> 1.0 rad (looking down more)
    constexpr float MAP_SMOKE_PAN_X_PX = 15.0f;
    constexpr float MAP_SMOKE_PAN_Y_PX = 10.0f;
    constexpr float MAP_SMOKE_WHEEL_NOTCH = 1.0f;

    // Verdict tolerances
    constexpr double MAP_SMOKE_DRAG_RELATIVE_TOLERANCE = 0.02;  // Mouse units -> pixels round trip through the config
    constexpr double MAP_SMOKE_ZOOM_RELATIVE_TOLERANCE = 1.0e-9;
    constexpr double MAP_SMOKE_FOCUS_MAX_OFFSET_M = 1.0e7;      // Focus vs the ship: ship motion over a few frames
    constexpr double MAP_SMOKE_PAN_MAX_UP_M = 1.0;              // Pan never moves the focus along Up (rounding only)
    constexpr double MAP_SMOKE_UNCHANGED_TOLERANCE = 1.0e-12;
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and subsystems and starts the first phase
void FSOLMapSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    mClock = world->GetSubsystem<USOLSimClockSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    UE_LOG(LogSOL, Log, TEXT("SmokeMap: scripted jump-map input starting (controller %s)"),
        mController.IsValid() ? *mController->GetName() : TEXT("none"));
    mPhase = ESOLMapPhase::Setup;
    EnterPhase(mPhase, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLMapSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLMapPhase::Done)
    {
        return true;
    }
    if (!mController.IsValid() || !mMapMode.IsValid() || !mAnchor.IsValid() || !mClock.IsValid()
        || !mRegistry.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeMap: controller or subsystems missing; aborting"));
        mPhase = ESOLMapPhase::Done;
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
    if (mPhaseTimeS < GetPhaseDuration(mPhase) || mPhaseFrames < GetPhaseMinFrames(mPhase))
    {
        return false;
    }

    // Phase over: checks, then the next phase
    ExitPhase(mPhase, pawn);
    mPhase = static_cast<ESOLMapPhase>(static_cast<uint8>(mPhase) + 1);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    if (mPhase == ESOLMapPhase::Done)
    {
        return true;
    }
    EnterPhase(mPhase, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions (key presses, records)
void FSOLMapSmoke::EnterPhase(const ESOLMapPhase phase, ASOLShipPawn& /*pawn*/)
{
    mReferenceState = mMapMode->GetCameraState();
    mReferenceSimSeconds = mClock->GetClock().GetSecondsSinceJ2000();
    switch (phase)
    {
    case ESOLMapPhase::OpenJ:
    case ESOLMapPhase::CloseJ:
    case ESOLMapPhase::ReopenJ:
        TapKey(EKeys::J);
        break;
    case ESOLMapPhase::SuppressW:
    case ESOLMapPhase::RestoreW:
        PressKey(EKeys::W);
        break;
    case ESOLMapPhase::LeftNoop:
        PressKey(EKeys::LeftMouseButton);
        break;
    case ESOLMapPhase::Orbit:
        PressKey(EKeys::RightMouseButton);
        break;
    case ESOLMapPhase::Pan:
        PressKey(EKeys::MiddleMouseButton);
        break;
    case ESOLMapPhase::PanShift:
        PressKey(EKeys::LeftShift);
        PressKey(EKeys::RightMouseButton);
        break;
    case ESOLMapPhase::ShotOrbits:
        if (ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>())
        {
            if (!hud->AreOrbitLinesVisible())
            {
                hud->ToggleOrbitLines();
            }
        }
        break;
    case ESOLMapPhase::CloseEsc:
        TapKey(EKeys::Escape);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's per-frame actions (mouse deltas, wheel notches)
void FSOLMapSmoke::TickPhase(const ESOLMapPhase phase)
{
    switch (phase)
    {
    case ESOLMapPhase::LeftNoop:
    case ESOLMapPhase::Orbit:
        if (mPhaseFrames <= MAP_SMOKE_DRAG_FRAMES)
        {
            InjectAxis(EKeys::MouseX, MAP_SMOKE_ORBIT_X_PX);
            InjectAxis(EKeys::MouseY, MAP_SMOKE_ORBIT_Y_PX);
        }
        break;
    case ESOLMapPhase::Pan:
    case ESOLMapPhase::PanShift:
        if (mPhaseFrames <= MAP_SMOKE_DRAG_FRAMES)
        {
            InjectAxis(EKeys::MouseX, MAP_SMOKE_PAN_X_PX);
            InjectAxis(EKeys::MouseY, MAP_SMOKE_PAN_Y_PX);
        }
        break;
    case ESOLMapPhase::Zoom:
        if (mPhaseFrames <= MAP_SMOKE_WHEEL_FRAMES)
        {
            InjectAxis(EKeys::MouseWheelAxis, MAP_SMOKE_WHEEL_NOTCH);
        }
        break;
    case ESOLMapPhase::ZoomClose:
        if (mPhaseFrames <= MAP_SMOKE_CLOSE_ZOOM_NOTCHES)
        {
            InjectAxis(EKeys::MouseWheelAxis, MAP_SMOKE_WHEEL_NOTCH);
        }
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks and releases held keys
void FSOLMapSmoke::ExitPhase(const ESOLMapPhase phase, ASOLShipPawn& pawn)
{
    const FSOLOrbitCameraState& state = mMapMode->GetCameraState();
    const FSOLOrbitCameraParams& params = mMapMode->GetCameraParams();
    switch (phase)
    {
    case ESOLMapPhase::OpenJ:
    {
        CheckOpen(TEXT("a-open-j"), pawn);
        const double focusOffsetM = FVector3d::Dist(state.FocusPositionM, mAnchor->GetObserverPositionM());
        const double simAdvancedS = mClock->GetClock().GetSecondsSinceJ2000() - mReferenceSimSeconds;
        Check(TEXT("a-open-state"), state.DistanceM == SOL::MAP_DEFAULT_DISTANCE_M
            && focusOffsetM <= MAP_SMOKE_FOCUS_MAX_OFFSET_M && simAdvancedS > 0.0, FString::Printf(TEXT("distance "
            "%.4g m (default %.4g), yaw %.3f, pitch %.3f, focus %.0f m from the ship, sim time advanced %.3f s"),
            state.DistanceM, SOL::MAP_DEFAULT_DISTANCE_M, state.YawRad, state.PitchRad, focusOffsetM, simAdvancedS));
        break;
    }
    case ESOLMapPhase::SuppressW:
    {
        const FVector3d thrust = pawn.GetComposedControl().Thrust;
        Check(TEXT("b-ship-suppressed"), thrust.IsZero() && pawn.IsMapOpen(), FString::Printf(TEXT("W held %.2f s "
            "with the map open: composed thrust (%.2f, %.2f, %.2f)"), mPhaseTimeS, thrust.X, thrust.Y, thrust.Z));
        ReleaseKey(EKeys::W);
        break;
    }
    case ESOLMapPhase::ShotWide:
    case ESOLMapPhase::ShotOrbits:
    case ESOLMapPhase::ShotClose:
    {
        LogBodyPixelSizes();
        const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
        UE_LOG(LogSOL, Log, TEXT("SmokeMap: screenshot (%s), distance %.4g m, yaw %.3f, pitch %.3f, orbit segments "
            "drawn %d"), phase == ESOLMapPhase::ShotWide ? TEXT("map, plain")
            : phase == ESOLMapPhase::ShotOrbits ? TEXT("map, orbit ellipses") : TEXT("map, zoomed in near Earth"),
            state.DistanceM, state.YawRad, state.PitchRad, hud != nullptr ? hud->GetLastOrbitSegmentsDrawn() : -1);
        if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
        {
            gameMode->CaptureScreenshot();
        }
        break;
    }
    case ESOLMapPhase::LeftNoop:
    {
        // The left button is reserved for destination picking: a left drag must leave the camera exactly as it was
        const bool bUnchanged = state.YawRad == mReferenceState.YawRad && state.PitchRad == mReferenceState.PitchRad
            && state.DistanceM == mReferenceState.DistanceM && state.FocusPositionM == mReferenceState.FocusPositionM;
        Check(TEXT("c0-left-drag-noop"), bUnchanged && pawn.IsMapOpen(), FString::Printf(TEXT("left drag (%.0f, %.0f) "
            "px: yaw %.4f -> %.4f, pitch %.4f -> %.4f, distance %.4g -> %.4g m, camera unchanged %s"),
            MAP_SMOKE_DRAG_FRAMES * MAP_SMOKE_ORBIT_X_PX, MAP_SMOKE_DRAG_FRAMES * MAP_SMOKE_ORBIT_Y_PX,
            mReferenceState.YawRad, state.YawRad, mReferenceState.PitchRad, state.PitchRad, mReferenceState.DistanceM,
            state.DistanceM, bUnchanged ? TEXT("yes") : TEXT("NO")));
        ReleaseKey(EKeys::LeftMouseButton);
        break;
    }
    case ESOLMapPhase::Orbit:
    {
        // Grab-the-scene orbit: dragging right turns the camera left (yaw down), dragging down raises it (pitch up)
        const double dragXPx = MAP_SMOKE_DRAG_FRAMES * MAP_SMOKE_ORBIT_X_PX;
        const double dragYPx = MAP_SMOKE_DRAG_FRAMES * MAP_SMOKE_ORBIT_Y_PX;
        const double expectedYaw = FMath::UnwindRadians(mReferenceState.YawRad
            - dragXPx * SOL::MAP_ORBIT_RAD_PER_PIXEL);
        const double expectedPitch = FMath::Clamp(mReferenceState.PitchRad - dragYPx * SOL::MAP_ORBIT_RAD_PER_PIXEL,
            params.MinPitchRad, params.MaxPitchRad);
        const double yawDelta = FMath::UnwindRadians(state.YawRad - mReferenceState.YawRad);
        const double expectedYawDelta = FMath::UnwindRadians(expectedYaw - mReferenceState.YawRad);
        const double pitchDelta = state.PitchRad - mReferenceState.PitchRad;
        const double expectedPitchDelta = expectedPitch - mReferenceState.PitchRad;
        const bool bUnchanged = state.DistanceM == mReferenceState.DistanceM
            && state.FocusPositionM == mReferenceState.FocusPositionM;
        Check(TEXT("c-orbit-drag"), FMath::Abs(yawDelta - expectedYawDelta)
            <= MAP_SMOKE_DRAG_RELATIVE_TOLERANCE * FMath::Abs(expectedYawDelta)
            && FMath::Abs(pitchDelta - expectedPitchDelta)
            <= MAP_SMOKE_DRAG_RELATIVE_TOLERANCE * FMath::Abs(expectedPitchDelta) && bUnchanged,
            FString::Printf(TEXT("right drag (%.0f, %.0f) px: yaw %.4f -> %.4f (delta %.4f, expected %.4f), pitch "
                "%.4f -> %.4f (delta %.4f, expected %.4f), focus/distance unchanged %s"), dragXPx, dragYPx,
                mReferenceState.YawRad, state.YawRad, yawDelta, expectedYawDelta, mReferenceState.PitchRad,
                state.PitchRad, pitchDelta, expectedPitchDelta, bUnchanged ? TEXT("yes") : TEXT("NO")));
        ReleaseKey(EKeys::RightMouseButton);
        break;
    }
    case ESOLMapPhase::Zoom:
    {
        // Wheel forward zooms in: the distance shrinks by the zoom step per notch
        const double expected = FMath::Clamp(mReferenceState.DistanceM
            / FMath::Pow(params.ZoomStepFactor, static_cast<double>(MAP_SMOKE_WHEEL_FRAMES)), params.MinDistanceM,
            params.MaxDistanceM);
        const bool bUnchanged = state.YawRad == mReferenceState.YawRad && state.PitchRad == mReferenceState.PitchRad
            && state.FocusPositionM == mReferenceState.FocusPositionM;
        Check(TEXT("d-wheel-zoom"), FMath::Abs(state.DistanceM - expected)
            <= MAP_SMOKE_ZOOM_RELATIVE_TOLERANCE * expected && bUnchanged, FString::Printf(TEXT("%d notches forward: "
            "distance %.6g -> %.6g m (expected %.6g, factor 1/%.4f), yaw/pitch/focus unchanged %s"),
            MAP_SMOKE_WHEEL_FRAMES, mReferenceState.DistanceM, state.DistanceM, expected,
            mReferenceState.DistanceM / FMath::Max(state.DistanceM, 1.0), bUnchanged ? TEXT("yes") : TEXT("NO")));
        break;
    }
    case ESOLMapPhase::Pan:
    case ESOLMapPhase::PanShift:
    {
        // Grab-the-scene pan across the ecliptic plane, scaled by the distance; never along Up (middle drag, then
        // the Shift+right alternate)
        const bool bShiftRight = phase == ESOLMapPhase::PanShift;
        const double metersPerPixel = mReferenceState.DistanceM * SOL::MAP_PAN_DISTANCE_FRACTION_PER_PIXEL;
        const double dragXPx = MAP_SMOKE_DRAG_FRAMES * MAP_SMOKE_PAN_X_PX;
        const double dragYPx = MAP_SMOKE_DRAG_FRAMES * MAP_SMOKE_PAN_Y_PX;
        const FSOLOrbitCameraState expectedState = SOLMapCamera::ApplyPan(mReferenceState, -dragXPx * metersPerPixel,
            -dragYPx * metersPerPixel);
        const FVector3d delta = state.FocusPositionM - mReferenceState.FocusPositionM;
        const FVector3d expectedDelta = expectedState.FocusPositionM - mReferenceState.FocusPositionM;
        const double error = (delta - expectedDelta).Size();
        const bool bUnchanged = state.YawRad == mReferenceState.YawRad && state.PitchRad == mReferenceState.PitchRad
            && FMath::Abs(state.DistanceM - mReferenceState.DistanceM)
            <= MAP_SMOKE_UNCHANGED_TOLERANCE * mReferenceState.DistanceM;
        Check(bShiftRight ? TEXT("e2-pan-shift-right") : TEXT("e-pan-middle-drag"), error
            <= MAP_SMOKE_DRAG_RELATIVE_TOLERANCE * expectedDelta.Size() && FMath::Abs(delta.Z) <= MAP_SMOKE_PAN_MAX_UP_M
            && !delta.IsNearlyZero() && bUnchanged, FString::Printf(TEXT("%s drag (%.0f, %.0f) px at %.4g m/px: focus "
                "moved (%.4g, %.4g, %.4g) m, expected (%.4g, %.4g, %.4g) m (error %.3g m), along Up %.3g m, "
                "yaw/pitch/distance unchanged %s"), bShiftRight ? TEXT("Shift+right") : TEXT("middle"), dragXPx,
                dragYPx, metersPerPixel, delta.X, delta.Y, delta.Z, expectedDelta.X, expectedDelta.Y, expectedDelta.Z,
                error, delta.Z, bUnchanged ? TEXT("yes") : TEXT("NO")));
        if (bShiftRight)
        {
            ReleaseKey(EKeys::RightMouseButton);
            ReleaseKey(EKeys::LeftShift);
        }
        else
        {
            ReleaseKey(EKeys::MiddleMouseButton);
        }
        break;
    }
    case ESOLMapPhase::CloseJ:
        CheckClosed(TEXT("f-close-j"), pawn);
        break;
    case ESOLMapPhase::RestoreW:
    {
        const FVector3d thrust = pawn.GetComposedControl().Thrust;
        Check(TEXT("g-ship-restored"), thrust.X > 0.0 && !pawn.IsMapOpen(), FString::Printf(TEXT("W held %.2f s "
            "after closing: composed thrust (%.2f, %.2f, %.2f)"), mPhaseTimeS, thrust.X, thrust.Y, thrust.Z));
        ReleaseKey(EKeys::W);
        break;
    }
    case ESOLMapPhase::ReopenJ:
        CheckOpen(TEXT("h-reopen-j"), pawn);
        break;
    case ESOLMapPhase::CloseEsc:
        CheckClosed(TEXT("h-close-esc"), pawn);
        break;
    case ESOLMapPhase::Finish:
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeMap: complete after %.1f s, %d/%d checks passed%s"), mTotalTimeS,
            mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("") : TEXT(" (FAILURES)"));
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
double FSOLMapSmoke::GetPhaseDuration(const ESOLMapPhase phase)
{
    switch (phase)
    {
    case ESOLMapPhase::Setup:
        return MAP_SMOKE_SETUP_S;
    case ESOLMapPhase::SuppressW:
    case ESOLMapPhase::RestoreW:
        return MAP_SMOKE_HOLD_W_S;
    case ESOLMapPhase::ShotWide:
    case ESOLMapPhase::ShotOrbits:
    case ESOLMapPhase::ShotClose:
        return MAP_SMOKE_SHOT_SETTLE_S;
    case ESOLMapPhase::LeftNoop:
    case ESOLMapPhase::Orbit:
    case ESOLMapPhase::Zoom:
    case ESOLMapPhase::Pan:
    case ESOLMapPhase::PanShift:
    case ESOLMapPhase::ZoomClose:
        return 0.0;
    case ESOLMapPhase::Finish:
        return MAP_SMOKE_FINISH_S;
    default:
        return MAP_SMOKE_TAP_PHASE_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum number of frames a phase lasts
int32 FSOLMapSmoke::GetPhaseMinFrames(const ESOLMapPhase phase)
{
    switch (phase)
    {
    case ESOLMapPhase::LeftNoop:
    case ESOLMapPhase::Orbit:
    case ESOLMapPhase::Pan:
    case ESOLMapPhase::PanShift:
        return MAP_SMOKE_DRAG_FRAMES + MAP_SMOKE_SETTLE_FRAMES;
    case ESOLMapPhase::Zoom:
        return MAP_SMOKE_WHEEL_FRAMES + MAP_SMOKE_SETTLE_FRAMES;
    case ESOLMapPhase::ZoomClose:
        return MAP_SMOKE_CLOSE_ZOOM_NOTCHES + MAP_SMOKE_SETTLE_FRAMES;
    default:
        return MAP_SMOKE_SETTLE_FRAMES;
    }
}

//////////////////////////////////////////////////////////////////////////
// Checks that the map is open with the cursor free, the ship input suspended and the map camera active
void FSOLMapSmoke::CheckOpen(const TCHAR* label, ASOLShipPawn& pawn)
{
    const ACameraActor* camera = mMapMode->GetCameraActor();
    const AActor* viewTarget = mController->GetViewTarget();
    const bool bCameraActive = camera != nullptr && viewTarget == camera;

    // The render origin follows the map camera, so the camera renders near Unreal's origin even ~1e13 m from the ship
    const double cameraRenderM = camera != nullptr ? camera->GetActorLocation().Size() * SOL::CM_TO_METERS : -1.0;
    const bool bCameraNearOrigin = camera != nullptr && cameraRenderM <= SOL::RENDER_REBASE_DISTANCE_M;
    const bool bPassed = pawn.IsMapOpen() && mMapMode->IsMapOpen() && mController->bShowMouseCursor
        && !pawn.IsShipInputMapped() && pawn.IsMapInputMapped() && pawn.GetComposedControl().Thrust.IsZero()
        && bCameraActive && mAnchor->HasViewpointOverride() && bCameraNearOrigin;
    Check(label, bPassed, FString::Printf(TEXT("map open %s, cursor %s, ship mapping %s, map mapping %s, view target "
        "%s (map camera %s), viewpoint override %s, camera %.1f m from the render origin"),
        mMapMode->IsMapOpen() ? TEXT("yes") : TEXT("no"), mController->bShowMouseCursor ? TEXT("visible")
        : TEXT("hidden"), pawn.IsShipInputMapped() ? TEXT("active") : TEXT("removed"), pawn.IsMapInputMapped()
        ? TEXT("active") : TEXT("removed"), viewTarget != nullptr ? *viewTarget->GetName() : TEXT("none"),
        bCameraActive ? TEXT("active") : TEXT("NOT active"), mAnchor->HasViewpointOverride() ? TEXT("on")
        : TEXT("off"), cameraRenderM));
}

//////////////////////////////////////////////////////////////////////////
// Checks that the map is closed with the cursor captured, the ship input back and the ship camera active
void FSOLMapSmoke::CheckClosed(const TCHAR* label, ASOLShipPawn& pawn)
{
    const AActor* viewTarget = mController->GetViewTarget();
    const double originOffsetM = FVector3d::Dist(mAnchor->GetRenderOriginM(), mAnchor->GetObserverPositionM());
    const bool bPassed = !pawn.IsMapOpen() && !mMapMode->IsMapOpen() && !mController->bShowMouseCursor
        && pawn.IsShipInputMapped() && !pawn.IsMapInputMapped() && viewTarget == &pawn
        && !mAnchor->HasViewpointOverride() && originOffsetM <= SOL::RENDER_REBASE_DISTANCE_M;
    Check(label, bPassed, FString::Printf(TEXT("map open %s, cursor %s, ship mapping %s, map mapping %s, view target "
        "%s (ship pawn %s), viewpoint override %s, render origin %.1f m from the ship"),
        mMapMode->IsMapOpen() ? TEXT("yes") : TEXT("no"), mController->bShowMouseCursor ? TEXT("visible")
        : TEXT("hidden"), pawn.IsShipInputMapped() ? TEXT("active") : TEXT("removed"), pawn.IsMapInputMapped()
        ? TEXT("active") : TEXT("removed"), viewTarget != nullptr ? *viewTarget->GetName() : TEXT("none"),
        viewTarget == &pawn ? TEXT("yes") : TEXT("NO"), mAnchor->HasViewpointOverride() ? TEXT("on") : TEXT("off"),
        originOffsetM));
}

//////////////////////////////////////////////////////////////////////////
// Logs each body's apparent radius in pixels as seen from the map camera (honest visibility evidence)
void FSOLMapSmoke::LogBodyPixelSizes() const
{
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
    mController->GetViewportSize(viewportWidth, viewportHeight);
    const double focalPx = 0.5 * viewportWidth / FMath::Tan(FMath::DegreesToRadians(0.5 * SOL::MAP_CAMERA_FOV_DEG));
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FVector3d& cameraM = mMapMode->GetCameraPositionM();
    FString line;
    for (int32 index = 0; index < registry.Num(); ++index)
    {
        const double distanceM = FVector3d::Dist(registry.GetPositionM(index), cameraM);
        line.Appendf(TEXT("%s %.3g px  "), *registry.GetName(index).ToString(),
            focalPx * registry.GetRadiusM(index) / FMath::Max(distanceM, 1.0));
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeMap: apparent body radii from the map camera (%dx%d, focal %.0f px): %s"),
        viewportWidth, viewportHeight, focalPx, *line);
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller
void FSOLMapSmoke::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller
void FSOLMapSmoke::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a key now and releases it on the next frame
void FSOLMapSmoke::TapKey(const FKey& key)
{
    PressKey(key);
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Sends one analog axis sample (mouse delta in pixels, wheel notches) to the player controller
void FSOLMapSmoke::InjectAxis(const FKey& key, const float delta) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Axis, delta, 1));
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLMapSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeMap CHECK %-18s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
