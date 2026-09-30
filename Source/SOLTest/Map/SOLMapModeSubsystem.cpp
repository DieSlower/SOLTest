/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapModeSubsystem.h"

#include "Game/SOLGameMode.h"
#include "Map/SOLMapBodyOverlay.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

//////////////////////////////////////////////////////////////////////////
// Resolves the anchor subsystem and subscribes to its frame events
void USOLMapModeSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    AnchorSubsystem = collection.InitializeDependency<USOLAnchorSubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Super::Initialize(collection);
    if (BodyRegistry != nullptr)
    {
        mPickCandidates.Reserve(BodyRegistry->GetRegistry().Num());
    }

    if (AnchorSubsystem != nullptr)
    {
        mBodiesUpdatedHandle = AnchorSubsystem->OnBodiesUpdated().AddUObject(this,
            &USOLMapModeSubsystem::HandleBodiesUpdated);
        mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
            &USOLMapModeSubsystem::HandleUniverseUpdated);
    }
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from the anchor subsystem and drops the camera
void USOLMapModeSubsystem::Deinitialize()
{
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnBodiesUpdated().Remove(mBodiesUpdatedHandle);
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
        if (mIsOpen)
        {
            AnchorSubsystem->ClearViewpointOverride();
        }
    }
    mBodiesUpdatedHandle.Reset();
    mUniverseUpdatedHandle.Reset();
    mIsOpen = false;
    ResetPick();
    CameraActor = nullptr;
    AnchorSubsystem = nullptr;
    BodyRegistry = nullptr;
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLMapModeSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLMapModeSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Opens the map for a controller: orbit state around the ship, map camera as the view target; false on failure
bool USOLMapModeSubsystem::OpenMap(APlayerController* playerController)
{
    if (mIsOpen)
    {
        return true;
    }
    if (playerController == nullptr || AnchorSubsystem == nullptr || !EnsureCameraActor())
    {
        UE_LOG(LogSOL, Error, TEXT("MapMode %s: cannot open the map (controller, anchor or camera missing)"),
            *GetName());
        return false;
    }

    // Start around the ship's current position, far enough out to frame the whole planetary system.
    // Intentional: the focus is set once here and never re-centered on the (still coasting) ship; only pan moves it
    mState = FSOLOrbitCameraState();
    mState.FocusPositionM = AnchorSubsystem->GetObserverPositionM();
    mState.DistanceM = FMath::Clamp(SOL::MAP_DEFAULT_DISTANCE_M, mParams.MinDistanceM, mParams.MaxDistanceM);
    mPendingOrbitPx = FVector2d::ZeroVector;
    mPendingPanPx = FVector2d::ZeroVector;
    mPendingZoomNotches = 0;
    ResetPick();
    mIsHeightModifierHeld = false;
    mIsCameraDragging = false;
    mJumpRequestCount = 0;

    // The camera becomes the render viewpoint now; the anchor's next update rebases onto it and places every body
    mCameraPositionM = SOLMapCamera::ComputeCameraPositionM(mState);
    AnchorSubsystem->SetViewpointOverrideM(mCameraPositionM);
    mPreviousViewTarget = playerController->GetViewTarget();
    playerController->SetViewTarget(CameraActor);
    mIsOpen = true;
    UE_LOG(LogSOL, Log, TEXT("MapMode %s: map open, focus (%.4g, %.4g, %.4g) m, distance %.4g m"), *GetName(),
        mState.FocusPositionM.X, mState.FocusPositionM.Y, mState.FocusPositionM.Z, mState.DistanceM);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Closes the map: gives the view back to the controller's pawn and the render viewpoint back to the observer
void USOLMapModeSubsystem::CloseMap(APlayerController* playerController)
{
    if (!mIsOpen)
    {
        return;
    }
    mIsOpen = false;

    // Esc / J abandon the pick: nothing carries over to the next time the map opens
    ResetPick();
    mIsHeightModifierHeld = false;
    mIsCameraDragging = false;
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->ClearViewpointOverride();
    }
    if (playerController != nullptr)
    {
        AActor* viewTarget = mPreviousViewTarget.Get();
        playerController->SetViewTarget(viewTarget != nullptr ? viewTarget : playerController->GetPawn());
    }
    mPreviousViewTarget.Reset();
    UE_LOG(LogSOL, Log, TEXT("MapMode %s: map closed"), *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Queues an orbit drag in mouse pixels (X right, Y up), applied at the next universe update
void USOLMapModeSubsystem::AddOrbitPixels(const FVector2d& deltaPixels)
{
    if (mIsOpen)
    {
        mPendingOrbitPx += deltaPixels;
    }
}

//////////////////////////////////////////////////////////////////////////
// Queues a pan drag in mouse pixels (X right, Y up), applied at the next universe update
void USOLMapModeSubsystem::AddPanPixels(const FVector2d& deltaPixels)
{
    if (mIsOpen)
    {
        mPendingPanPx += deltaPixels;
    }
}

//////////////////////////////////////////////////////////////////////////
// Queues wheel notches (positive = wheel forward = zoom in), applied at the next universe update
void USOLMapModeSubsystem::AddZoomNotches(const int32 notches)
{
    if (mIsOpen)
    {
        mPendingZoomNotches += notches;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the map camera's vertical field of view (radians) for a viewport aspect ratio (width / height)
double USOLMapModeSubsystem::GetVerticalFovRad(const double aspectRatio) const
{
    const UCameraComponent* camera = CameraActor != nullptr ? CameraActor->GetCameraComponent() : nullptr;
    const double horizontalFovDeg = camera != nullptr ? camera->FieldOfView : SOL::MAP_CAMERA_FOV_DEG;
    const double halfHorizontalRad = FMath::DegreesToRadians(horizontalFovDeg) * 0.5;
    return 2.0 * FMath::Atan(FMath::Tan(halfHorizontalRad) / FMath::Max(aspectRatio, UE_DOUBLE_KINDA_SMALL_NUMBER));
}

//////////////////////////////////////////////////////////////////////////
// Applies the queued input and makes the resulting camera position the render viewpoint (before the rebase)
void USOLMapModeSubsystem::HandleBodiesUpdated(const float /*realDeltaSeconds*/)
{
    if (!mIsOpen || AnchorSubsystem == nullptr)
    {
        return;
    }

    // "Grab the scene" drags: the scene follows the cursor, so the camera orbits and the focus pans opposite to it.
    // Pan meters per pixel scale with the distance so a drag covers the same screen fraction at any zoom
    if (!mPendingOrbitPx.IsZero())
    {
        mState = SOLMapCamera::ApplyOrbitDelta(mState, -mPendingOrbitPx.X * SOL::MAP_ORBIT_RAD_PER_PIXEL,
            -mPendingOrbitPx.Y * SOL::MAP_ORBIT_RAD_PER_PIXEL, mParams);
    }
    if (!mPendingPanPx.IsZero())
    {
        const double metersPerPixel = mState.DistanceM * SOL::MAP_PAN_DISTANCE_FRACTION_PER_PIXEL;
        mState = SOLMapCamera::ApplyPan(mState, -mPendingPanPx.X * metersPerPixel, -mPendingPanPx.Y * metersPerPixel);
    }
    if (mPendingZoomNotches != 0)
    {
        // Wheel forward zooms in, i.e. a negative step of the orbit camera's distance factor
        mState = SOLMapCamera::ApplyZoom(mState, -mPendingZoomNotches, mParams);
    }
    mPendingOrbitPx = FVector2d::ZeroVector;
    mPendingPanPx = FVector2d::ZeroVector;
    mPendingZoomNotches = 0;

    mCameraPositionM = SOLMapCamera::ComputeCameraPositionM(mState);
    AnchorSubsystem->SetViewpointOverrideM(mCameraPositionM);
}

//////////////////////////////////////////////////////////////////////////
// Places the camera actor from the final render origin of the frame
void USOLMapModeSubsystem::HandleUniverseUpdated()
{
    if (!mIsOpen || CameraActor == nullptr || AnchorSubsystem == nullptr)
    {
        return;
    }

    // The render origin sits on the camera (the viewpoint), so this location is within the rebase distance of (0,0,0)
    CameraActor->SetActorLocationAndRotation(AnchorSubsystem->UniverseToRenderCm(mCameraPositionM),
        FQuat(SOLMapCamera::ComputeCameraOrientation(mState)));

    // Body positions and the camera are final for the frame: the pick follows its (possibly moving) reference
    UpdatePick();
}

//////////////////////////////////////////////////////////////////////////
// Sets the map cursor (pixels, top-left origin) and the size of the viewport it is measured in
void USOLMapModeSubsystem::SetPickCursorPx(const FVector2D& cursorPx, const FVector2D& viewportSizePx)
{
    mPickCursorPx = cursorPx;
    mViewportSizePx = viewportSizePx;
}

//////////////////////////////////////////////////////////////////////////
// Left mouse down: locks the height (Shift during a height preview) or starts a new pick on a body or the ship
void USOLMapModeSubsystem::PressPick(const FSOLMapBodyOverlay* overlay, const bool bHeightModifierHeld)
{
    if (!mIsOpen)
    {
        return;
    }

    // Shift + click during an active height preview locks the height at the current cursor (refreshed now, as the
    // planar mouse-up does, so the lock is not last frame's value). Shift held for anything else (e.g. left over from
    // a Shift+right pan) does not count: the click starts a new pick
    if (bHeightModifierHeld && mIsHeightPreviewing && mPick.bPlanarLocked && !mPick.bHeightLocked)
    {
        UpdatePick();
        mPick.HeightOffsetZM = mPreviewHeightM;
        mPick.bHeightLocked = true;
        mIsHeightPreviewing = false;
        mPreviewHeightM = 0.0;
        FVector3d destinationM;
        GetLiveDestinationM(destinationM);
        UE_LOG(LogSOL, Log, TEXT("MapPick: height locked at %.6g m; destination (%.6g, %.6g, %.6g) m"),
            mPick.HeightOffsetZM, destinationM.X, destinationM.Y, destinationM.Z);
        return;
    }

    // Any other click starts over: the body under the cursor on screen (nearest center within its pick radius) is
    // the reference, else the ship. The candidates are the bodies the overlay projected last frame
    ResetPick();
    int32 bodyIndex = INDEX_NONE;
    if (overlay != nullptr)
    {
        const TConstArrayView<FVector2D> screenPx = overlay->GetScreenPositionsPx();
        const TConstArrayView<double> pickRadiiPx = overlay->GetPickRadiiPx();
        const int32 bodyCount = FMath::Min(screenPx.Num(), pickRadiiPx.Num());
        mPickCandidates.Reset();
        for (int32 index = 0; index < bodyCount; ++index)
        {
            if (pickRadiiPx[index] > 0.0)
            {
                FSOLScreenPickCandidate& candidate = mPickCandidates.AddDefaulted_GetRef();
                candidate.BodyIndex = index;
                candidate.ScreenPositionPx = screenPx[index];
                candidate.PickRadiusPx = pickRadiiPx[index];
            }
        }
        bodyIndex = SOLMapScreenPick::PickNearestOnScreen(mPickCandidates, mPickCursorPx);
    }
    mPick.bHasReference = true;
    mPick.bReferenceIsBody = bodyIndex != INDEX_NONE;
    mPick.ReferenceBodyIndex = bodyIndex;
    mPickReferenceM = ResolvePickReferenceM();
    mIsPickDragging = true;
    const FName referenceName = mPick.bReferenceIsBody && BodyRegistry != nullptr
        ? BodyRegistry->GetRegistry().GetName(bodyIndex) : FName(TEXT("ship"));
    UE_LOG(LogSOL, Log, TEXT("MapPick: mouse-down at (%.1f, %.1f) px; reference %s (body index %d)"),
        mPickCursorPx.X, mPickCursorPx.Y, *referenceName.ToString(), bodyIndex);
}

//////////////////////////////////////////////////////////////////////////
// Left mouse up: locks the planar offset of the drag
void USOLMapModeSubsystem::ReleasePick()
{
    if (!mIsOpen || !mIsPickDragging)
    {
        return;
    }

    // The preview of the latest cursor position is the lock (refreshed now so a same-frame release is not stale)
    UpdatePick();
    mIsPickDragging = false;
    mPick.PlanarOffsetXM = mPreviewPlanarM.X;
    mPick.PlanarOffsetYM = mPreviewPlanarM.Y;
    mPick.bPlanarLocked = true;
    UE_LOG(LogSOL, Log, TEXT("MapPick: planar offset locked at (%.6g, %.6g) m from the reference (range %.6g m)"),
        mPick.PlanarOffsetXM, mPick.PlanarOffsetYM, mPreviewPlanarM.Size());
    if (mIsHeightModifierHeld && !mIsCameraDragging)
    {
        StartHeightPreview();
    }
}

//////////////////////////////////////////////////////////////////////////
// Shift held or released: starts or drops the height preview (only between the planar and the height lock, and
// never during a camera drag, where Shift is the pan modifier)
void USOLMapModeSubsystem::SetHeightModifierHeld(const bool bHeld)
{
    mIsHeightModifierHeld = bHeld;
    if (!mIsOpen)
    {
        return;
    }
    if (bHeld && mPick.bPlanarLocked && !mPick.bHeightLocked && !mIsHeightPreviewing && !mIsCameraDragging)
    {
        StartHeightPreview();
    }
    else if (!bHeld && mIsHeightPreviewing)
    {
        // Releasing Shift without a click abandons the preview; the next Shift starts again from 0
        StopHeightPreview();
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets whether a camera drag (right-drag orbit, or middle / Shift+right pan) is active; one starting drops the height
// preview, and none starts while it lasts (Shift then means pan). Releasing the drag does not start one
void USOLMapModeSubsystem::SetCameraDragActive(const bool bActive)
{
    mIsCameraDragging = bActive;
    if (bActive && mIsHeightPreviewing)
    {
        StopHeightPreview();
    }
}

//////////////////////////////////////////////////////////////////////////
// Clears the whole pick (X): no reference, nothing locked, no preview
void USOLMapModeSubsystem::ClearPick()
{
    if (!mIsOpen)
    {
        return;
    }
    ResetPick();
    UE_LOG(LogSOL, Log, TEXT("MapPick: pick cleared"));
}

//////////////////////////////////////////////////////////////////////////
// Enter: logs the live destination a jump would go to (the jump itself is 2e); false when none is locked yet
bool USOLMapModeSubsystem::RequestJump()
{
    FVector3d destinationM;
    if (!mIsOpen || !GetLiveDestinationM(destinationM))
    {
        UE_LOG(LogSOL, Log, TEXT("MapPick: Enter ignored, no destination locked yet"));
        return false;
    }

    // Placeholder until 2e: the jump would execute to this universe position; the map stays open
    ++mJumpRequestCount;
    mLastJumpRequestM = destinationM;
    UE_LOG(LogSOL, Log, TEXT("MapPick: JUMP would execute to universe position (%.9g, %.9g, %.9g) m (reference %s, "
        "offset (%.6g, %.6g, %.6g) m); jump execution is not built yet (2e), map stays open"), destinationM.X,
        destinationM.Y, destinationM.Z, mPick.bReferenceIsBody ? TEXT("body") : TEXT("ship"), mPick.PlanarOffsetXM,
        mPick.PlanarOffsetYM, mPick.bHeightLocked ? mPick.HeightOffsetZM : 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the planar offset (dx, dy) in meters: the live drag preview, or the locked value after mouse-up
FVector2D USOLMapModeSubsystem::GetPickPlanarOffsetM() const
{
    return mPick.bPlanarLocked ? FVector2D(mPick.PlanarOffsetXM, mPick.PlanarOffsetYM) : mPreviewPlanarM;
}

//////////////////////////////////////////////////////////////////////////
// Returns the height offset in meters: the locked value, else the live Shift preview, else 0
double USOLMapModeSubsystem::GetPickHeightOffsetM() const
{
    if (mPick.bHeightLocked)
    {
        return mPick.HeightOffsetZM;
    }
    return mIsHeightPreviewing ? mPreviewHeightM : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the live destination and true once the planar offset is locked; false before that
bool USOLMapModeSubsystem::GetLiveDestinationM(FVector3d& outDestinationM) const
{
    if (!mPick.bHasReference || !mPick.bPlanarLocked)
    {
        return false;
    }
    outDestinationM = SOLMapPicking::ComposeDestination(mPickReferenceM, mPick.PlanarOffsetXM, mPick.PlanarOffsetYM,
        mPick.bHeightLocked ? mPick.HeightOffsetZM : 0.0, SOL::MAP_PICK_ECLIPTIC_X, SOL::MAP_PICK_ECLIPTIC_Y,
        SOL::MAP_PICK_UP);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Recomputes the reference's live position, the drag and height previews and the destination for this frame
void USOLMapModeSubsystem::UpdatePick()
{
    if (!mIsOpen || !mPick.bHasReference)
    {
        return;
    }
    mPickReferenceM = ResolvePickReferenceM();

    // Planar drag: the cursor ray meets the ecliptic plane through the reference's current position. A ray parallel
    // to the plane or pointing away from it (cursor above the horizon) keeps the last valid preview
    if (mIsPickDragging)
    {
        FVector3d rayDirection;
        FVector3d hitM;
        if (ComputeCursorRay(rayDirection) && SOLMapPicking::RayPlaneIntersect(mCameraPositionM, rayDirection,
            mPickReferenceM, SOL::MAP_PICK_UP, hitM))
        {
            mPreviewPlanarM = SOLMapPicking::DecomposePlanarOffset(hitM, mPickReferenceM, SOL::MAP_PICK_ECLIPTIC_X,
                SOL::MAP_PICK_ECLIPTIC_Y);
        }
    }

    // Height: vertical cursor travel since the preview started (screen Y grows downward, so up = +Up)
    if (mIsHeightPreviewing)
    {
        mPreviewHeightM = (mHeightAnchorCursorY - mPickCursorPx.Y) * mHeightMetersPerPixel;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the unit ray (ecliptic) from the map camera through the cursor; false without a valid viewport
bool USOLMapModeSubsystem::ComputeCursorRay(FVector3d& outDirection) const
{
    if (mViewportSizePx.X <= 0.0 || mViewportSizePx.Y <= 0.0)
    {
        return false;
    }

    // Camera-local ray (X forward, Y right, Z up) with the horizontal FOV across the viewport width, as the HUD
    // projects; the bodies are placed along their true directions from this camera, so the ray is exact in universe
    // space. EclipticToUnreal flips Y, so applying it to the Unreal-space direction converts it back
    const UCameraComponent* camera = CameraActor != nullptr ? CameraActor->GetCameraComponent() : nullptr;
    const double horizontalFovDeg = camera != nullptr ? camera->FieldOfView : SOL::MAP_CAMERA_FOV_DEG;
    const FVector2D centerPx = 0.5 * mViewportSizePx;
    const double focalPx = centerPx.X / FMath::Tan(FMath::DegreesToRadians(horizontalFovDeg) * 0.5);
    const FVector3d localDirection(focalPx, mPickCursorPx.X - centerPx.X, centerPx.Y - mPickCursorPx.Y);
    const FVector3d unrealDirection = SOLMapCamera::ComputeCameraOrientation(mState).RotateVector(localDirection);
    outDirection = SOLRender::EclipticToUnreal(unrealDirection).GetSafeNormal();
    return !outDirection.IsZero();
}

//////////////////////////////////////////////////////////////////////////
// Returns the reference's live universe position: the picked body's, or the ship's (the observer). A body index that
// no longer resolves turns the reference into the ship
FVector3d USOLMapModeSubsystem::ResolvePickReferenceM()
{
    if (mPick.bReferenceIsBody)
    {
        // Assumes registry indices are stable for the session (bodies are never added or removed while playing)
        const FSOLBodyRegistry* registry = BodyRegistry != nullptr ? &BodyRegistry->GetRegistry() : nullptr;
        if (registry != nullptr && mPick.ReferenceBodyIndex >= 0 && mPick.ReferenceBodyIndex < registry->Num())
        {
            return registry->GetPositionM(mPick.ReferenceBodyIndex);
        }
        UE_LOG(LogSOL, Warning, TEXT("MapPick: reference body index %d is out of range; falling back to the ship"),
            mPick.ReferenceBodyIndex);
        mPick.bReferenceIsBody = false;
        mPick.ReferenceBodyIndex = INDEX_NONE;
    }
    return AnchorSubsystem != nullptr ? AnchorSubsystem->GetObserverPositionM() : FVector3d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Starts the Shift height preview at the current cursor, freezing its meters-per-pixel scale
void USOLMapModeSubsystem::StartHeightPreview()
{
    // Proportional to the camera's distance from the locked XY point, frozen so the mapping stays reversible
    const FVector3d planarPointM = SOLMapPicking::ComposeDestination(mPickReferenceM, mPick.PlanarOffsetXM,
        mPick.PlanarOffsetYM, 0.0, SOL::MAP_PICK_ECLIPTIC_X, SOL::MAP_PICK_ECLIPTIC_Y, SOL::MAP_PICK_UP);
    mHeightMetersPerPixel = FVector3d::Dist(planarPointM, mCameraPositionM)
        * SOL::MAP_HEIGHT_DISTANCE_FRACTION_PER_PIXEL;
    mHeightAnchorCursorY = mPickCursorPx.Y;
    mPreviewHeightM = 0.0;
    mIsHeightPreviewing = true;
    UE_LOG(LogSOL, Log, TEXT("MapPick: height preview started at cursor Y %.1f px, %.6g m per pixel"),
        mHeightAnchorCursorY, mHeightMetersPerPixel);
}

//////////////////////////////////////////////////////////////////////////
// Drops the height preview (back to 0); the next one starts afresh
void USOLMapModeSubsystem::StopHeightPreview()
{
    mIsHeightPreviewing = false;
    mPreviewHeightM = 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Drops the pick state and every preview back to "no reference"
void USOLMapModeSubsystem::ResetPick()
{
    mPick = FSOLMapPickState();
    mPickReferenceM = FVector3d::ZeroVector;
    mPreviewPlanarM = FVector2D::ZeroVector;
    mPreviewHeightM = 0.0;
    mHeightMetersPerPixel = 0.0;
    mIsPickDragging = false;
    mIsHeightPreviewing = false;
}

//////////////////////////////////////////////////////////////////////////
// Spawns the map camera actor once
bool USOLMapModeSubsystem::EnsureCameraActor()
{
    if (CameraActor != nullptr)
    {
        return true;
    }
    UWorld* world = GetWorld();
    if (world == nullptr)
    {
        return false;
    }
    FActorSpawnParameters params;
    params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    params.ObjectFlags |= RF_Transient;
    CameraActor = world->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, params);
    if (CameraActor == nullptr)
    {
        return false;
    }

    // A plain perspective camera: no tick, no aspect-ratio letterboxing, the map's own field of view
    CameraActor->SetActorTickEnabled(false);
    UCameraComponent* camera = CameraActor->GetCameraComponent();
    camera->SetFieldOfView(SOL::MAP_CAMERA_FOV_DEG);
    camera->SetConstraintAspectRatio(false);
    return true;
}
