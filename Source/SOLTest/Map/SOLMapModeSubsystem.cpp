/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapModeSubsystem.h"

#include "Game/SOLGameMode.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

//////////////////////////////////////////////////////////////////////////
// Resolves the anchor subsystem and subscribes to its frame events
void USOLMapModeSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    AnchorSubsystem = collection.InitializeDependency<USOLAnchorSubsystem>();
    Super::Initialize(collection);

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
    CameraActor = nullptr;
    AnchorSubsystem = nullptr;
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
