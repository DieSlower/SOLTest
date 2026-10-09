/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuCameraPawn.h"

#include "Menu/SOLMenuSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"

//////////////////////////////////////////////////////////////////////////
// Creates the camera
ASOLMenuCameraPawn::ASOLMenuCameraPawn()
{
    PrimaryActorTick.bCanEverTick = false;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->bUsePawnControlRotation = false;
    Camera->SetFieldOfView(SOL::MENU_CAMERA_FOV_DEG);
    RootComponent = Camera;
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;
}

//////////////////////////////////////////////////////////////////////////
// Becomes the observer, places it at the start body and hooks the universe update
void ASOLMenuCameraPawn::BeginPlay()
{
    Super::BeginPlay();
    UWorld* world = GetWorld();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    BodyRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    Menu = world->GetSubsystem<USOLMenuSubsystem>();
    if (AnchorSubsystem == nullptr || BodyRegistry == nullptr || Menu == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("MenuCameraPawn %s: universe or menu subsystems missing"), *GetName());
        return;
    }

    // A teleport onto the camera path (re-picks the anchor and snaps the render origin), then follow it every frame
    int32 bodyIndex = INDEX_NONE;
    AnchorSubsystem->SetObserverPositionM(ComputeObserverPositionM(bodyIndex));
    mBodyIndex = bodyIndex;
    AnchorSubsystem->SetObserverActor(this);
    mBodiesUpdatedHandle = AnchorSubsystem->OnBodiesUpdated().AddUObject(this, &ASOLMenuCameraPawn::UpdateObserver);
    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
        &ASOLMenuCameraPawn::FollowObserver);
    FollowObserver();
    UE_LOG(LogSOL, Log, TEXT("MenuCameraPawn %s: orbiting %s"), *GetName(),
        *BodyRegistry->GetRegistry().GetName(mBodyIndex).ToString());
}

//////////////////////////////////////////////////////////////////////////
// Unhooks from the universe update and stops being the observer actor
void ASOLMenuCameraPawn::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnBodiesUpdated().Remove(mBodiesUpdatedHandle);
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
        AnchorSubsystem->SetObserverActor(nullptr);
    }
    mBodiesUpdatedHandle.Reset();
    mUniverseUpdatedHandle.Reset();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Returns the camera's universe position (ecliptic m) for this moment, and the body it looks at
FVector3d ASOLMenuCameraPawn::ComputeObserverPositionM(int32& outBodyIndex) const
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    outBodyIndex = registry.Num() > 0 ? FMath::Clamp(Menu->GetStartBodyIndex(), 0, registry.Num() - 1) : INDEX_NONE;
    if (outBodyIndex == INDEX_NONE)
    {
        return FVector3d::ZeroVector;
    }

    // Lit side: from the sunward direction (the Sun sits at the origin; +X for the Sun itself), turned around the
    // ecliptic pole by the base azimuth plus a slow real-time sway, and raised above the ecliptic
    const FVector3d bodyM = registry.GetPositionM(outBodyIndex);
    FVector3d sunward = (-bodyM).GetSafeNormal();
    if (sunward.IsNearlyZero())
    {
        sunward = FVector3d::XAxisVector;
    }
    const double swayPhase = 2.0 * UE_DOUBLE_PI * GetWorld()->GetRealTimeSeconds() / SOL::MENU_CAMERA_SWAY_PERIOD_S;
    const double azimuthRad = FMath::DegreesToRadians(SOL::MENU_CAMERA_BASE_AZIMUTH_DEG
        + SOL::MENU_CAMERA_SWAY_AZIMUTH_DEG * FMath::Sin(swayPhase));
    const FVector3d horizontal = FQuat4d(FVector3d::ZAxisVector, azimuthRad).RotateVector(
        FVector3d(sunward.X, sunward.Y, 0.0).GetSafeNormal());
    const double elevationRad = FMath::DegreesToRadians(SOL::MENU_CAMERA_ELEVATION_DEG);
    const FVector3d direction = horizontal * FMath::Cos(elevationRad) + FVector3d::ZAxisVector * FMath::Sin(elevationRad);
    return bodyM + direction * (registry.GetRadiusM(outBodyIndex) * SOL::MENU_CAMERA_DISTANCE_RADII);
}

//////////////////////////////////////////////////////////////////////////
// Moves the observer along the camera path; bound to OnBodiesUpdated (before the anchor update)
void ASOLMenuCameraPawn::UpdateObserver(const float /*realDeltaSeconds*/)
{
    int32 bodyIndex = INDEX_NONE;
    const FVector3d observerM = ComputeObserverPositionM(bodyIndex);

    // A new start body is a teleport (anchor re-pick and origin snap); otherwise the camera glides with its body
    if (bodyIndex != mBodyIndex)
    {
        mBodyIndex = bodyIndex;
        AnchorSubsystem->SetObserverPositionM(observerM);
        return;
    }
    AnchorSubsystem->SyncObserverPositionM(observerM);
}

//////////////////////////////////////////////////////////////////////////
// Places the actor at the observer's render location looking at the body; bound to OnUniverseUpdated
void ASOLMenuCameraPawn::FollowObserver()
{
    if (mBodyIndex == INDEX_NONE)
    {
        return;
    }
    // Both points go through the body placement, which keeps directions, so the look direction is exact even when
    // the body is placed closer than it really is
    const FVector cameraCm = AnchorSubsystem->ComputePointRenderLocationCm(AnchorSubsystem->GetObserverPositionM());
    const FVector bodyCm = AnchorSubsystem->ComputePointRenderLocationCm(
        BodyRegistry->GetRegistry().GetPositionM(mBodyIndex));
    FRotator lookRotation = (bodyCm - cameraCm).Rotation();
    lookRotation.Yaw += SOL::MENU_CAMERA_LOOK_YAW_OFFSET_DEG;
    SetActorLocationAndRotation(cameraCm, lookRotation);
}
