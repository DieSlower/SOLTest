/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapAlignSmoke.h"

#include "Game/SOLGameMode.h"
#include "Map/SOLMapCamera.h"
#include "Map/SOLMapModeSubsystem.h"
#include "OrbitLines/SOLOrbitLineMath.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "SceneView.h"

namespace
{
    // The scripted views: inner and outer planets on their heliocentric lines, moons on their planet-centred lines,
    // and a body seen from close by on its own line (where any offset is largest on screen)
    struct FAlignView
    {
        const TCHAR* FocusName;
        const TCHAR* TargetName;
        double DistanceM;
        double PitchRad;
        double YawRad;
    };
    const FAlignView ALIGN_VIEWS[] = {
        { TEXT("Earth"), TEXT("Moon"), 1.2e9, 0.6, 0.4 },
        { TEXT("Earth"), TEXT("Earth"), 3.0e9, 0.5, 1.2 },
        { TEXT("Earth"), TEXT("Earth"), 4.0e7, 0.5, 1.2 },
        { TEXT("Jupiter"), TEXT("Jupiter"), 6.0e8, 0.4, -0.7 },
        { TEXT("Sun"), TEXT("Earth"), 4.0e11, 0.7, 0.3 },
        { TEXT("Jupiter"), TEXT("Io"), 3.0e9, 0.6, -0.5 },
        { TEXT("Sun"), TEXT("Saturn"), 3.0e12, 0.5, 2.0 },
        { TEXT("Sun"), TEXT("Neptune"), 1.0e13, 0.6, -1.0 },
    };
    constexpr int32 ALIGN_VIEW_COUNT = UE_ARRAY_COUNT(ALIGN_VIEWS);

    // Each view settles for this long (and at least this many frames) before it is measured and photographed
    constexpr double ALIGN_SETTLE_S = 1.0;
    constexpr int32 ALIGN_SETTLE_FRAMES = 20;

    // The motion sweep: orbit drag per frame (pixels) and its length in frames (a fixed count, so the mid-sweep
    // screenshot is taken at the same camera angle on every run); the screenshot is taken half way
    constexpr double ALIGN_SWEEP_PX_PER_FRAME = 4.0;
    constexpr int32 ALIGN_SWEEP_FRAMES = 120;

    // Clip-space W (view depth, cm) a line chord is clipped to before projecting; approximately the rasterizer's near
    // clip (GNearClippingPlane, 10 cm), which makes no measurable difference at these distances
    constexpr double ALIGN_NEAR_W_CM = 1.0;
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and subsystems and opens the map
void FSOLMapAlignSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mClock = world->GetSubsystem<USOLSimClockSubsystem>();
    mTargeting = world->GetSubsystem<USOLTargetingSubsystem>();
    UE_LOG(LogSOL, Log, TEXT("SmokeMapAlign: map alignment run starting (%d views)"), ALIGN_VIEW_COUNT);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLMapAlignSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mIsFinished)
    {
        return true;
    }
    if (!mController.IsValid() || !mMapMode.IsValid() || !mAnchor.IsValid() || !mRegistry.IsValid()
        || !mClock.IsValid() || !mTargeting.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeMapAlign: controller or subsystems missing; aborting"));
        mIsFinished = true;
        if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
        {
            gameMode->TakeSmokeScreenshot();     // screenshot, then quit, so a headless run cannot hang
        }
        return true;
    }
    if (mHasPendingMapKeyRelease)
    {
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::M, IE_Released, 0.0f));
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Released, 0.0f));
        mHasPendingMapKeyRelease = false;
    }
    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>();

    // Step 0: wait for the orbit lines, turn them on for the run, open the map with J
    if (mStep == 0)
    {
        const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
        ASOLOrbitLineVisuals* lines = hud != nullptr ? hud->GetOrbitLineVisuals() : nullptr;
        if (lines == nullptr || mStepTimeS < ALIGN_SETTLE_S)
        {
            return false;
        }
        mOrbitLines = lines;
        lines->BeginVerificationOverride();

        // The HUD/renderer FOV check needs a viewport that is not 16:9 (at 16:9 both FOV rules give the same view)
        FVector2D viewportPx = FVector2D::ZeroVector;
        if (GEngine != nullptr && GEngine->GameViewport != nullptr)
        {
            GEngine->GameViewport->GetViewportSize(viewportPx);
        }
        if (viewportPx.Y > 0.0 && FMath::IsNearlyEqual(viewportPx.X / viewportPx.Y, 16.0 / 9.0, 0.01))
        {
            UE_LOG(LogSOL, Warning, TEXT("SmokeMapAlign: the viewport is 16:9 (%.0f x %.0f), where a HUD/renderer FOV "
                "mismatch cannot show; run with e.g. -windowed -ResX=1280 -ResY=1024"), viewportPx.X, viewportPx.Y);
        }
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Pressed, 1.0f));
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::M, IE_Pressed, 1.0f));
        mHasPendingMapKeyRelease = true;
        mStep = 1;
        mStepFrames = 0;
        mStepTimeS = 0.0;
        return false;
    }

    // Steps 1..N: one view each; the camera is re-aimed every frame so the focus follows its (moving) body
    if (mStep <= ALIGN_VIEW_COUNT)
    {
        const FAlignView& view = ALIGN_VIEWS[mStep - 1];
        const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
        const int32 focus = registry.FindByName(FName(view.FocusName));
        const int32 target = registry.FindByName(FName(view.TargetName));
        if (mStepFrames == 1)
        {
            Check(TEXT("target selected"), target != INDEX_NONE && SelectTarget(target),
                FString::Printf(TEXT("%s"), view.TargetName));
        }
        if (focus != INDEX_NONE)
        {
            FSOLOrbitCameraState state;
            state.FocusPositionM = registry.GetPositionM(focus);
            state.DistanceM = view.DistanceM;
            state.PitchRad = view.PitchRad;
            state.YawRad = view.YawRad;
            mMapMode->SetCameraState(state);
        }
        if (mStepTimeS < ALIGN_SETTLE_S || mStepFrames < ALIGN_SETTLE_FRAMES)
        {
            return false;
        }

        if (mStep == 1)
        {
            Check(TEXT("map open"), mMapMode->IsMapOpen(), TEXT("J opened the map"));
        }

        // Measured on this frame's state (camera cache, bodies and lines all from the last universe update)
        FVector2D bodyPx = FVector2D::ZeroVector;
        double distancePx = 0.0;
        double chordErrorPx = 0.0;
        const bool bMeasured = target != INDEX_NONE
            && MeasureBodyToLinePx(target, bodyPx, distancePx, chordErrorPx);
        const double tolerancePx = SOL::MAP_ALIGN_SMOKE_TOLERANCE_PX + chordErrorPx;
        const FString label = FString::Printf(TEXT("%s on line"), view.TargetName);
        Check(*label, bMeasured && distancePx <= tolerancePx, FString::Printf(
            TEXT("focus %s at %.3g m: body at (%.1f, %.1f) px, %.3f px from its line (tolerance %.3f px)"),
            view.FocusName, view.DistanceM, bodyPx.X, bodyPx.Y, distancePx, tolerancePx));
        const int32 highlighted = mOrbitLines.IsValid() ? mOrbitLines->GetHighlightedSlot() : INDEX_NONE;
        const int32 expectedSlot = target != INDEX_NONE ? PermanentLineSlot(target) : INDEX_NONE;
        Check(TEXT("line highlighted"), highlighted != INDEX_NONE && highlighted == expectedSlot,
            FString::Printf(TEXT("slot %d, %s's line is slot %d"), highlighted, view.TargetName, expectedSlot));
        FVector2D viewportPx = FVector2D::ZeroVector;
        if (GEngine != nullptr && GEngine->GameViewport != nullptr)
        {
            GEngine->GameViewport->GetViewportSize(viewportPx);
        }
        UE_LOG(LogSOL, Log,
            TEXT("SmokeMapAlign: screenshot (view %d: %s around %s, body at %.1f, %.1f px of %.0f x %.0f)"), mStep,
            view.TargetName, view.FocusName, bodyPx.X, bodyPx.Y, viewportPx.X, viewportPx.Y);
        Screenshot();
        ++mStep;
        mStepFrames = 0;
        mStepTimeS = 0.0;
        return false;
    }

    // Motion sweep: the camera orbits every frame; a screenshot half way catches the lines while moving
    if (mStep == ALIGN_VIEW_COUNT + 1)
    {
        mMapMode->AddOrbitPixels(FVector2d(ALIGN_SWEEP_PX_PER_FRAME, 0.5 * ALIGN_SWEEP_PX_PER_FRAME));
        if (mStepFrames == ALIGN_SWEEP_FRAMES / 2)
        {
            UE_LOG(LogSOL, Log, TEXT("SmokeMapAlign: screenshot (orbiting)"));
            Screenshot();
        }
        if (mStepFrames < ALIGN_SWEEP_FRAMES)
        {
            return false;
        }
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Pressed, 1.0f));
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::M, IE_Pressed, 1.0f));
        mHasPendingMapKeyRelease = true;
        ++mStep;
        mStepFrames = 0;
        mStepTimeS = 0.0;
        return false;
    }

    // Close: the map is shut, the run is summarised and the game quits after a last screenshot
    if (mStepTimeS < ALIGN_SETTLE_S)
    {
        return false;
    }
    Check(TEXT("map closed"), !mMapMode->IsMapOpen(), TEXT("J closed the map"));
    UE_LOG(LogSOL, Log, TEXT("SmokeMapAlign: complete, %d/%d checks passed%s"), mPassed, mPassed + mFailed,
        mFailed == 0 ? TEXT("") : TEXT(" (FAILURES)"));
    if (gameMode != nullptr)
    {
        gameMode->TakeSmokeScreenshot();
    }
    mIsFinished = true;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Selects the body as the target (cycling the targeting list until it is selected); false if it never is. Registry
// bodies come first in the candidate list, at their body index (USOLTargetingSubsystem), so the two indices match
bool FSOLMapAlignSmoke::SelectTarget(const int32 bodyIndex) const
{
    const int32 candidateCount = mTargeting->GetCandidates().Num();
    const FVector3d observerM = mAnchor->GetObserverPositionM();
    for (int32 attempt = 0; attempt <= candidateCount && mTargeting->GetSelectedIndex() != bodyIndex; ++attempt)
    {
        mTargeting->CycleTarget(observerM, 1);
    }
    return mTargeting->GetSelectedIndex() == bodyIndex;
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's permanent orbit-line slot: registry bodies with a parent come first, in registry order
// (SOLOrbitLineMath::BuildPermanentLines); INDEX_NONE for a body without a parent
int32 FSOLMapAlignSmoke::PermanentLineSlot(const int32 bodyIndex) const
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    if (registry.GetParent(bodyIndex) == INDEX_NONE)
    {
        return INDEX_NONE;
    }
    int32 slot = 0;
    for (int32 index = 0; index < bodyIndex; ++index)
    {
        slot += registry.GetParent(index) != INDEX_NONE ? 1 : 0;
    }
    return slot;
}

//////////////////////////////////////////////////////////////////////////
// Returns the screen distance (px) from the body's map icon to its orbit line as the GPU draws it, and the line's
// designed chord error seen from the camera (px)
bool FSOLMapAlignSmoke::MeasureBodyToLinePx(const int32 bodyIndex, FVector2D& outBodyPx, double& outDistancePx,
    double& outChordErrorPx) const
{
    outBodyPx = FVector2D::ZeroVector;
    outDistancePx = TNumericLimits<double>::Max();
    outChordErrorPx = 0.0;
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const int32 parent = registry.GetParent(bodyIndex);
    FMatrix viewProjection;
    FIntRect viewRect;
    if (parent == INDEX_NONE || !GetRendererProjection(viewProjection, viewRect)
        || !ProjectLikeHud(FVector(mAnchor->ComputeBodyRenderPlacement(bodyIndex).LocationCm), outBodyPx))
    {
        return false;
    }

    // The line actor's sampling (its own segment count, so its own chord error), then the shader's placement:
    // (parent - viewpoint) + point, log-compressed about the viewpoint, at its render location, in clip space
    TArray<FVector3d> pointsM;
    TArray<FVector3d> tangents;
    TArray<double> arcs;
    const FSOLKeplerElements elements = registry.GetElements(bodyIndex).AtCenturies(
        mClock->GetClock().GetCenturiesSinceJ2000());
    const int32 segments = SOLOrbitLineMath::SegmentCountFor(elements.SemiMajorAxisM, registry.GetRadiusM(bodyIndex));
    if (SOLOrbitLineMath::SampleOrbit(elements, segments, pointsM, tangents, arcs) < 2)
    {
        return false;
    }
    const FVector3d& viewpointM = mAnchor->GetViewpointM();
    const FVector3d parentOffsetM = registry.GetPositionM(parent) - viewpointM;
    const FVector viewpointRenderCm = mAnchor->UniverseToRenderCm(viewpointM);
    TArray<FVector4> clipPoints;
    clipPoints.Reserve(pointsM.Num());
    for (const FVector3d& pointM : pointsM)
    {
        const FVector renderCm = viewpointRenderCm + FVector(SOLRender::EclipticToUnreal(
            SOLOrbitLineMath::PlaceLinePointCm(parentOffsetM + pointM, SOL::DEFAULT_MAX_RENDER_DISTANCE_CM)));
        clipPoints.Add(viewProjection.TransformFVector4(FVector4(renderCm, 1.0)));
    }
    outDistancePx = ScreenDistanceToClipPolylinePx(outBodyPx, clipPoints, viewRect, ALIGN_NEAR_W_CM);

    // The chord error the line is built with (SDD 17: a fraction of the body's radius), at the body's true distance
    // from the camera, through the HUD's focal length
    const APlayerCameraManager* cameraManager = mController->PlayerCameraManager;
    FVector2D viewportPx = FVector2D::ZeroVector;
    GEngine->GameViewport->GetViewportSize(viewportPx);
    const FMinimalViewInfo& pov = cameraManager->GetCameraCacheView();
    const double cameraDistanceM = FVector3d::Dist(registry.GetPositionM(bodyIndex),
        mAnchor->RenderToUniverseM(pov.Location));
    const double focalPx = 0.5 * viewportPx.X
        / FMath::Tan(FMath::DegreesToRadians(static_cast<double>(pov.FOV)) * 0.5);
    if (cameraDistanceM > 0.0)
    {
        outChordErrorPx = focalPx * SOLOrbitLineMath::ChordSagittaM(elements.SemiMajorAxisM, segments)
            / cameraDistanceM;
    }
    return outDistancePx < TNumericLimits<double>::Max();
}

//////////////////////////////////////////////////////////////////////////
// Returns the screen distance (px) from a point to a clip-space polyline, each segment clipped to W >= nearW first
double FSOLMapAlignSmoke::ScreenDistanceToClipPolylinePx(const FVector2D& pointPx,
    const TConstArrayView<FVector4> clipPoints, const FIntRect& viewRect, const double nearW)
{
    double distancePx = TNumericLimits<double>::Max();
    const auto toScreen = [&viewRect](const FVector4& clip)
    {
        return FVector2D(viewRect.Min.X + (0.5 + 0.5 * clip.X / clip.W) * viewRect.Width(),
            viewRect.Min.Y + (0.5 - 0.5 * clip.Y / clip.W) * viewRect.Height());
    };
    for (int32 index = 1; index < clipPoints.Num(); ++index)
    {
        // Clip the chord against the near plane in homogeneous space (linear in clip space, as the rasterizer does);
        // a chord entirely behind it is not drawn
        FVector4 start = clipPoints[index - 1];
        FVector4 end = clipPoints[index];
        if (start.W < nearW && end.W < nearW)
        {
            continue;
        }
        if (start.W < nearW)
        {
            start = start + (end - start) * ((nearW - start.W) / (end.W - start.W));
        }
        else if (end.W < nearW)
        {
            end = end + (start - end) * ((nearW - end.W) / (start.W - end.W));
        }
        const FVector2D closest = FMath::ClosestPointOnSegment2D(pointPx, toScreen(start), toScreen(end));
        distancePx = FMath::Min(distancePx, FVector2D::Distance(closest, pointPx));
    }
    return distancePx;
}

//////////////////////////////////////////////////////////////////////////
// Fills the renderer's view-projection matrix and view rectangle for the local player; false without a viewport
bool FSOLMapAlignSmoke::GetRendererProjection(FMatrix& outViewProjection, FIntRect& outViewRect) const
{
    ULocalPlayer* localPlayer = mController->GetLocalPlayer();
    FSceneViewProjectionData projection;
    if (localPlayer == nullptr || localPlayer->ViewportClient == nullptr
        || !localPlayer->GetProjectionData(localPlayer->ViewportClient->Viewport, projection))
    {
        return false;
    }
    outViewProjection = projection.ComputeViewProjectionMatrix();
    outViewRect = projection.GetConstrainedViewRect();
    return outViewRect.Width() > 0 && outViewRect.Height() > 0;
}

//////////////////////////////////////////////////////////////////////////
// Projects a render location (cm) as the flight HUD and the map icons do (ASOLFlightHud::BuildView: the camera
// cache's FOV as horizontal across the viewport width); false when behind the camera
bool FSOLMapAlignSmoke::ProjectLikeHud(const FVector& renderCm, FVector2D& outScreenPx) const
{
    const APlayerCameraManager* cameraManager = mController->PlayerCameraManager;
    if (cameraManager == nullptr || GEngine == nullptr || GEngine->GameViewport == nullptr)
    {
        return false;
    }
    FVector2D viewportPx;
    GEngine->GameViewport->GetViewportSize(viewportPx);
    const FMinimalViewInfo& pov = cameraManager->GetCameraCacheView();
    const FVector local = pov.Rotation.Quaternion().UnrotateVector(renderCm - pov.Location);
    if (local.X <= 1.0 || viewportPx.X <= 0.0)
    {
        return false;
    }
    const FVector2D center = 0.5 * viewportPx;
    const double focalPx = center.X / FMath::Tan(FMath::DegreesToRadians(static_cast<double>(pov.FOV)) * 0.5);
    outScreenPx = FVector2D(center.X + focalPx * local.Y / local.X, center.Y - focalPx * local.Z / local.X);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Takes a screenshot at the viewport's own size, so the HUD and the 3D view line up pixel for pixel in the image
void FSOLMapAlignSmoke::Screenshot() const
{
    mController->ConsoleCommand(TEXT("HighResShot 1"));
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLMapAlignSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    (bPassed ? mPassed : mFailed) += 1;
    UE_LOG(LogSOL, Log, TEXT("SmokeMapAlign CHECK %-18s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
