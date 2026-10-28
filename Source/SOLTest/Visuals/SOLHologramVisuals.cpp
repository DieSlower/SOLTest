/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLHologramVisuals.h"

#include "GasGiants/SOLGasGiantVisuals.h"
#include "Map/SOLMapModeSubsystem.h"
#include "MinorBodies/SOLPlanetRing.h"
#include "MinorBodies/SOLRingVisuals.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Visuals/SOLBodyVisuals.h"
#include "Visuals/SOLHologramMath.h"

#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
    TAutoConsoleVariable<int32> CVarSOLHologram(SOL::Hologram::CVAR_ENABLED, 1,
        TEXT("Map hologram look (SDD 20): 1 = hologram proxies, post-process, grid and stems in the map (default), ")
        TEXT("0 = the map shows the real bodies with no hologram layers"),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarSOLHologramPostStrength(SOL::Hologram::CVAR_POST_STRENGTH,
        static_cast<float>(SOL::Hologram::POST_STRENGTH_DEFAULT),
        TEXT("Strength of the map hologram post-process (tint, scanlines, flicker, jitter): 0 = off, 1 = default, ")
        TEXT("at most 2"),
        ECVF_Default);

    // The plane mesh's side (cm) that ring discs are scaled from
    constexpr double HOLOGRAM_PLANE_MESH_SIZE_CM = 100.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns true for the real visuals the hologram replaces in the map (bodies, rings and gas giants)
    bool IsReplacedVisual(const AActor& actor)
    {
        return actor.IsA<ASOLBodyVisuals>() || actor.IsA<ASOLRingVisuals>() || actor.IsA<ASOLGasGiantVisuals>();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true while sol.Hologram is on
bool SOLHologramSwitch::IsHologramEnabled()
{
    return CVarSOLHologram.GetValueOnGameThread() != 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns sol.HologramPostStrength clamped
double SOLHologramSwitch::GetPostStrength()
{
    return SOLHologramMath::ClampPostStrength(CVarSOLHologramPostStrength.GetValueOnGameThread());
}

//////////////////////////////////////////////////////////////////////////
// Creates the root and the (disabled) map post-process
ASOLHologramVisuals::ASOLHologramVisuals()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // Unbound, above ASOLBodyVisuals' post-process: a fixed exposure (the bodies' exposure estimate does not apply to
    // the proxies) and the hologram material, only while the look shows
    PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
    PostProcess->SetupAttachment(SceneRoot);
    PostProcess->bUnbound = true;
    PostProcess->bEnabled = false;
    PostProcess->Priority = SOL::Hologram::POST_PRIORITY;
    FPostProcessSettings& settings = PostProcess->Settings;
    settings.bOverride_AutoExposureBias = true;
    settings.AutoExposureBias = SOL::Hologram::POST_EXPOSURE_BIAS;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of proxy components currently visible
int32 ASOLHologramVisuals::CountVisibleProxies() const
{
    int32 count = 0;
    for (const TObjectPtr<UStaticMeshComponent>& proxy : BodyProxies)
    {
        count += proxy != nullptr && proxy->IsVisible() ? 1 : 0;
    }
    for (const TObjectPtr<UStaticMeshComponent>& proxy : RingProxies)
    {
        count += proxy != nullptr && proxy->IsVisible() ? 1 : 0;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the map post-process is enabled
bool ASOLHologramVisuals::IsPostProcessEnabled() const
{
    return PostProcess != nullptr && PostProcess->bEnabled;
}

//////////////////////////////////////////////////////////////////////////
// Creates the proxies and the post-process material, then follows the universe updates
void ASOLHologramVisuals::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    BodyRegistrySubsystem = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    MapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    if (AnchorSubsystem == nullptr || BodyRegistrySubsystem == nullptr || MapMode == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("HologramVisuals %s: subsystems missing; no map hologram"), *GetName());
        return;
    }
    UStaticMesh* sphere = LoadObject<UStaticMesh>(nullptr, SOL::Paths::BODY_MESH);
    UStaticMesh* plane = LoadObject<UStaticMesh>(nullptr, SOL::Paths::HOLOGRAM_RING_MESH);
    UMaterialInterface* bodyMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::HOLOGRAM_BODY_MATERIAL);
    UMaterialInterface* ringMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::HOLOGRAM_RING_MATERIAL);
    UMaterialInterface* postMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::HOLOGRAM_POST_MATERIAL);
    if (sphere == nullptr || plane == nullptr || bodyMaterial == nullptr || ringMaterial == nullptr
        || postMaterial == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("HologramVisuals %s: hologram meshes or materials missing (run "
            "Tools/Map/create_hologram_assets.py); no map hologram"), *GetName());
        return;
    }

    // One proxy sphere per body: the Sun in Secondary, planets and moons in Primary (moons dimmer)
    const FSOLBodyRegistry& registry = BodyRegistrySubsystem->GetRegistry();
    const int32 bodyCount = registry.Num();
    const int32 sunIndex = registry.FindByName(FName(SOL::BodyNames::SUN));
    BodyProxies.Reserve(bodyCount);
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const int32 parent = registry.GetParent(index);
        const bool bIsSun = index == sunIndex;
        const bool bIsMoon = !bIsSun && parent != INDEX_NONE && parent != sunIndex;
        const double intensity = bIsSun ? SOL::Hologram::PROXY_INTENSITY_SUN
            : (bIsMoon ? SOL::Hologram::PROXY_INTENSITY_MOON : SOL::Hologram::PROXY_INTENSITY_PLANET);
        BodyProxies.Add(CreateProxy(FString::Printf(TEXT("Holo_%s"), *registry.GetName(index).ToString()), sphere,
            bodyMaterial, bIsSun ? SOL::Hologram::PROXY_COLOR_SUN : SOL::Hologram::PROXY_COLOR_BODY, intensity));
    }

    // One disc per ringed planet, its inner edge as a fraction of the outer radius
    for (const FSOLPlanetRingDef& ringDef : SOLPlanetRing::RealRings())
    {
        const int32 planetIndex = registry.FindByName(ringDef.PlanetName);
        if (planetIndex == INDEX_NONE || ringDef.OuterRadiusM <= 0.0)
        {
            continue;
        }
        UStaticMeshComponent* disc = CreateProxy(FString::Printf(TEXT("HoloRing_%s"),
            *ringDef.PlanetName.ToString()), plane, ringMaterial, SOL::Hologram::PROXY_COLOR_BODY,
            SOL::Hologram::PROXY_INTENSITY_RING);
        if (UMaterialInstanceDynamic* material = Cast<UMaterialInstanceDynamic>(disc->GetMaterial(0)))
        {
            material->SetScalarParameterValue(FName(SOL::HologramMaterialParams::RING_INNER_FRACTION),
                static_cast<float>(ringDef.InnerRadiusM / ringDef.OuterRadiusM));
        }
        RingProxies.Add(disc);
        mRingPlanetIndices.Add(planetIndex);
        mRingOuterRadiiM.Add(ringDef.OuterRadiusM);
    }

    // The post-process material, blended in only while the component is enabled
    PostMaterial = UMaterialInstanceDynamic::Create(postMaterial, this);
    PostMaterial->SetVectorParameterValue(FName(SOL::HologramMaterialParams::POST_TINT),
        SOL::Hologram::PROXY_COLOR_BODY);
    PostProcess->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, PostMaterial));

    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
        &ASOLHologramVisuals::HandleUniverseUpdated);
}

//////////////////////////////////////////////////////////////////////////
// Restores the real visuals if shown and unsubscribes
void ASOLHologramVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (mIsShown)
    {
        Hide();
    }
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mUniverseUpdatedHandle.Reset();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Creates one unlit proxy component with a material instance
UStaticMeshComponent* ASOLHologramVisuals::CreateProxy(const FString& name, UStaticMesh* mesh,
    UMaterialInterface* material, const FLinearColor& color, const double intensity)
{
    UStaticMeshComponent* proxy = NewObject<UStaticMeshComponent>(this, *name);
    proxy->SetMobility(EComponentMobility::Movable);
    proxy->SetStaticMesh(mesh);
    proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    proxy->SetCastShadow(false);
    proxy->bAffectDistanceFieldLighting = false;
    proxy->SetVisibility(false);
    proxy->SetupAttachment(SceneRoot);
    UMaterialInstanceDynamic* instance = UMaterialInstanceDynamic::Create(material, this);
    instance->SetVectorParameterValue(FName(SOL::HologramMaterialParams::COLOR), color);
    instance->SetScalarParameterValue(FName(SOL::HologramMaterialParams::INTENSITY), static_cast<float>(intensity));
    proxy->SetMaterial(0, instance);
    proxy->RegisterComponent();
    return proxy;
}

//////////////////////////////////////////////////////////////////////////
// Switches on map/cvar transitions and, while shown, places the proxies and updates the post-process strength
void ASOLHologramVisuals::HandleUniverseUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLHologramVisuals::HandleUniverseUpdated);

    switch (SOLHologramMath::NextTransition(mIsShown, MapMode->IsMapOpen(), SOLHologramSwitch::IsHologramEnabled()))
    {
    case ESOLHologramTransition::Show:
        Show();
        break;
    case ESOLHologramTransition::Hide:
        Hide();
        break;
    case ESOLHologramTransition::None:
        break;
    }
    if (!mIsShown)
    {
        return;
    }
    PlaceProxies();
    const float strength = static_cast<float>(SOLHologramSwitch::GetPostStrength());
    if (strength != mPostStrength)
    {
        mPostStrength = strength;
        PostMaterial->SetScalarParameterValue(FName(SOL::HologramMaterialParams::POST_STRENGTH), strength);
        // At strength 0 the full-screen pass is skipped entirely
        PostProcess->Settings.WeightedBlendables.Array[0].Weight = strength > 0.0f ? 1.0f : 0.0f;
    }
}

//////////////////////////////////////////////////////////////////////////
// Hides the real bodies, rings and gas giants for the player's view and shows the proxies and the post-process
void ASOLHologramVisuals::Show()
{
    // Without a player controller the real visuals cannot be hidden: stay off and try again next frame
    APlayerController* controller = GetWorld()->GetFirstPlayerController();
    if (controller == nullptr)
    {
        return;
    }
    mIsShown = true;
    mHidingController = controller;
    mHiddenActors.Reset();
    for (TActorIterator<AActor> iterator(GetWorld()); iterator; ++iterator)
    {
        AActor* actor = *iterator;
        if (IsReplacedVisual(*actor) && !controller->HiddenActors.Contains(actor))
        {
            controller->HiddenActors.Add(actor);
            mHiddenActors.Add(actor);
        }
    }
    for (const TObjectPtr<UStaticMeshComponent>& proxy : BodyProxies)
    {
        proxy->SetVisibility(true);
    }
    for (const TObjectPtr<UStaticMeshComponent>& proxy : RingProxies)
    {
        proxy->SetVisibility(true);
    }
    PostProcess->bEnabled = true;
    mPostStrength = -1.0f;
    UE_LOG(LogSOL, Log, TEXT("HologramVisuals %s: shown (%d proxies, %d real actors hidden)"), *GetName(),
        GetProxyCount(), mHiddenActors.Num());
}

//////////////////////////////////////////////////////////////////////////
// Gives the real visuals back and hides the proxies and the post-process
void ASOLHologramVisuals::Hide()
{
    mIsShown = false;
    if (APlayerController* controller = mHidingController.Get())
    {
        for (const TWeakObjectPtr<AActor>& actor : mHiddenActors)
        {
            controller->HiddenActors.RemoveSingle(actor.Get());
        }
        // Drop entries of actors destroyed meanwhile
        controller->HiddenActors.RemoveAll([](const TObjectPtr<AActor>& actor) { return actor == nullptr; });
    }
    mHiddenActors.Reset();
    mHidingController.Reset();
    for (const TObjectPtr<UStaticMeshComponent>& proxy : BodyProxies)
    {
        proxy->SetVisibility(false);
    }
    for (const TObjectPtr<UStaticMeshComponent>& proxy : RingProxies)
    {
        proxy->SetVisibility(false);
    }
    PostProcess->bEnabled = false;
    UE_LOG(LogSOL, Log, TEXT("HologramVisuals %s: hidden, real visuals restored"), *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Places every body proxy and ring disc for this frame, exactly as ASOLBodyVisuals places the bodies
void ASOLHologramVisuals::PlaceProxies()
{
    const FSOLBodyRegistry& registry = BodyRegistrySubsystem->GetRegistry();
    const int32 bodyCount = FMath::Min(BodyProxies.Num(), registry.Num());
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const FSOLRenderPlacement placement = AnchorSubsystem->ComputeBodyRenderPlacement(index);
        const FQuat bodyRotation(SOLRender::EclipticToUnreal(registry.GetOrientation(index)));
        BodyProxies[index]->SetWorldTransform(FTransform(bodyRotation, FVector(placement.LocationCm),
            FVector(placement.RadiusCm / SOL::BODY_MESH_RADIUS_CM)));
    }

    // Ring discs in the planet's equator plane, scaled by the planet's own render scale (cm per metre)
    const int32 ringCount = RingProxies.Num();
    for (int32 ring = 0; ring < ringCount; ++ring)
    {
        const int32 planetIndex = mRingPlanetIndices[ring];
        const double planetRadiusM = registry.GetRadiusM(planetIndex);
        if (planetRadiusM <= 0.0)
        {
            // No scale to place it with: keep it out of sight rather than at its default transform
            RingProxies[ring]->SetVisibility(false);
            continue;
        }
        const FSOLRenderPlacement placement = AnchorSubsystem->ComputeBodyRenderPlacement(planetIndex);
        const double outerCm = mRingOuterRadiiM[ring] * placement.RadiusCm / planetRadiusM;
        const FQuat planetRotation(SOLRender::EclipticToUnreal(registry.GetOrientation(planetIndex)));
        const double scale = 2.0 * outerCm / HOLOGRAM_PLANE_MESH_SIZE_CM;
        RingProxies[ring]->SetWorldTransform(FTransform(planetRotation, FVector(placement.LocationCm),
            FVector(scale, scale, 1.0)));
    }
}
