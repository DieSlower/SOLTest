/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLBodyVisuals.h"

#include "MinorBodies/SOLRingShadowMath.h"
#include "MinorBodies/SOLRingShadowSwitch.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Visuals/SOLSceneExposure.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
    // Placeholder look of one body: parameters of the parametric body material
    struct FSOLBodyAppearance
    {
        const TCHAR* Name;
        FLinearColor ColorA;
        FLinearColor ColorB;
        FLinearColor PolarColor;
        FLinearColor Emissive;
        float BandFrequency;
        float BandStrength;
        float NoiseScale;
        float NoiseStrength;
        float NoiseBias;
        float PolarStart;
    };

    const FLinearColor VISUALS_WHITE(0.9f, 0.9f, 0.92f);
    const FLinearColor VISUALS_NO_EMISSIVE(0.0f, 0.0f, 0.0f);
    constexpr float VISUALS_NO_POLAR_CAP = 2.0f;

    // Real-planet placeholder colors (linear); bands follow latitude, noise gives surface patches. The Sun keeps
    // NoiseStrength at 0: its ColorA/ColorB lerp is swamped by its fixed {60,48,30} emissive (see M_SOLBody), so no
    // noise value makes its surface pattern visible; its spin is exercised by the 12a unit tests instead.
    const FSOLBodyAppearance VISUALS_APPEARANCES[] =
    {
        { SOL::BodyNames::SUN, { 1.0f, 0.9f, 0.7f }, { 1.0f, 0.8f, 0.5f }, VISUALS_WHITE, { 60.0f, 48.0f, 30.0f },
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, VISUALS_NO_POLAR_CAP },
        { TEXT("Mercury"), { 0.36f, 0.34f, 0.32f }, { 0.2f, 0.19f, 0.18f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.0f, 0.0f, 0.08f, 0.6f, 0.0f, VISUALS_NO_POLAR_CAP },
        { TEXT("Venus"), { 0.85f, 0.74f, 0.48f }, { 0.95f, 0.9f, 0.72f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.03f, 0.35f, 0.03f, 0.2f, 0.0f, VISUALS_NO_POLAR_CAP },
        { SOL::BodyNames::EARTH, { 0.02f, 0.1f, 0.42f }, { 0.1f, 0.32f, 0.06f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.0f, 0.0f, 0.04f, 1.0f, 0.25f, 0.82f },
        { TEXT("Mars"), { 0.62f, 0.22f, 0.07f }, { 0.4f, 0.13f, 0.04f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.0f, 0.0f, 0.05f, 0.6f, 0.0f, 0.92f },
        { TEXT("Jupiter"), { 0.6f, 0.38f, 0.2f }, { 0.86f, 0.8f, 0.7f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.11f, 1.0f, 0.06f, 0.15f, 0.0f, VISUALS_NO_POLAR_CAP },
        { TEXT("Saturn"), { 0.82f, 0.7f, 0.45f }, { 0.95f, 0.88f, 0.64f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.09f, 0.7f, 0.04f, 0.5f, 0.0f, VISUALS_NO_POLAR_CAP },
        { TEXT("Uranus"), { 0.55f, 0.82f, 0.88f }, { 0.62f, 0.88f, 0.9f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.2f, 0.6f, 0.04f, 0.6f, 0.0f, VISUALS_NO_POLAR_CAP },
        { TEXT("Neptune"), { 0.08f, 0.18f, 0.7f }, { 0.14f, 0.3f, 0.85f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.05f, 0.4f, 0.04f, 0.4f, 0.0f, VISUALS_NO_POLAR_CAP },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the appearance for a body name, or nullptr if the body has none
    const FSOLBodyAppearance* FindAppearance(const FName name)
    {
        for (const FSOLBodyAppearance& appearance : VISUALS_APPEARANCES)
        {
            if (name == FName(appearance.Name))
            {
                return &appearance;
            }
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes an appearance into a body material instance
    void ApplyAppearance(UMaterialInstanceDynamic* material, const FSOLBodyAppearance& appearance)
    {
        using namespace SOL::BodyMaterialParams;
        material->SetVectorParameterValue(FName(COLOR_A), appearance.ColorA);
        material->SetVectorParameterValue(FName(COLOR_B), appearance.ColorB);
        material->SetVectorParameterValue(FName(POLAR_COLOR), appearance.PolarColor);
        material->SetVectorParameterValue(FName(EMISSIVE_COLOR), appearance.Emissive);
        material->SetScalarParameterValue(FName(BAND_FREQUENCY), appearance.BandFrequency);
        material->SetScalarParameterValue(FName(BAND_STRENGTH), appearance.BandStrength);
        material->SetScalarParameterValue(FName(NOISE_SCALE), appearance.NoiseScale);
        material->SetScalarParameterValue(FName(NOISE_STRENGTH), appearance.NoiseStrength);
        material->SetScalarParameterValue(FName(NOISE_BIAS), appearance.NoiseBias);
        material->SetScalarParameterValue(FName(POLAR_START), appearance.PolarStart);
    }
}

//////////////////////////////////////////////////////////////////////////
// Puts a primitive on lighting channel 1 only (the ship sunlight's channel)
void SOLShipSunlight::PutOnShipLightingChannel(UPrimitiveComponent* primitive)
{
    if (primitive != nullptr)
    {
        primitive->LightingChannels.bChannel0 = false;
        primitive->LightingChannels.bChannel1 = true;
        primitive->LightingChannels.bChannel2 = false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes a body's base color (the material's linear ColorA) and returns true, or returns false if it has none
bool SOLBodyAppearance::FindBaseColor(const FName bodyName, FLinearColor& outColor)
{
    const FSOLBodyAppearance* appearance = FindAppearance(bodyName);
    if (appearance == nullptr)
    {
        return false;
    }
    outColor = appearance->ColorA;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Creates the root, the sun light and the post-process component
ASOLBodyVisuals::ASOLBodyVisuals()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // Sunlight for lit objects near the observer: no shadows and no distance-field cost (bodies shade themselves)
    SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
    SunLight->SetupAttachment(SceneRoot);
    SunLight->SetMobility(EComponentMobility::Movable);
    SunLight->SetIntensity(SunIlluminanceLux);
    SunLight->SetCastShadows(false);
    SunLight->bAffectsWorld = true;
    SunLight->ForwardShadingPriority = SOL::SUN_FORWARD_SHADING_PRIORITY;
    SunLight->bAtmosphereSunLight = false;
    SunLight->AtmosphereSunDiskColorScale = FLinearColor::Black;
    SunLight->LightingChannels.bChannel1 = true;

    // Shadowed sunlight for the ship and targets (channel 1 only), hidden until the observer enters a shadow (SDD 16)
    ShipSunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("ShipSunLight"));
    ShipSunLight->SetupAttachment(SceneRoot);
    ShipSunLight->SetMobility(EComponentMobility::Movable);
    ShipSunLight->SetIntensity(SunIlluminanceLux);
    ShipSunLight->SetCastShadows(false);
    ShipSunLight->bAffectsWorld = true;
    ShipSunLight->bAtmosphereSunLight = false;
    ShipSunLight->AtmosphereSunDiskColorScale = FLinearColor::Black;
    ShipSunLight->LightingChannels.bChannel0 = false;
    ShipSunLight->LightingChannels.bChannel1 = true;
    ShipSunLight->SetVisibleFlag(false);

    // Manual exposure moved by this actor (SDD 13 decision 2, Amendment 1): bias 0 is the dark-sky exposure the lit
    // planets and the emissive Sun were tuned against; UpdateExposure lowers it (at most EXPOSURE_MAX_EV100 -
    // EXPOSURE_MIN_EV100) when bright bodies fill the view. The engine's eye adaptation is not used: the render-origin
    // rebase resets the previous view every frame at speed, which makes it snap instead of adapting
    PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
    PostProcess->SetupAttachment(SceneRoot);
    PostProcess->bUnbound = true;
    FPostProcessSettings& settings = PostProcess->Settings;
    settings.bOverride_AutoExposureMethod = true;
    settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    settings.AutoExposureApplyPhysicalCameraExposure = false;
    settings.bOverride_AutoExposureBias = true;
    settings.AutoExposureBias = 0.0f;
    settings.bOverride_BloomIntensity = true;
    settings.BloomIntensity = 1.0f;
    settings.bOverride_VignetteIntensity = true;
    settings.VignetteIntensity = 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Hides or shows one body's placeholder mesh; false if the meshes do not exist yet or the index is invalid
bool ASOLBodyVisuals::SetBodyMeshHidden(const int32 bodyIndex, const bool bHide)
{
    if (!BodyMeshes.IsValidIndex(bodyIndex) || BodyMeshes[bodyIndex] == nullptr)
    {
        return false;
    }
    BodyMeshes[bodyIndex]->SetVisibility(!bHide);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns true if a body's placeholder mesh exists and is currently hidden
bool ASOLBodyVisuals::IsBodyMeshHidden(const int32 bodyIndex) const
{
    return BodyMeshes.IsValidIndex(bodyIndex) && BodyMeshes[bodyIndex] != nullptr &&
        !BodyMeshes[bodyIndex]->IsVisible();
}

//////////////////////////////////////////////////////////////////////////
// Adds or removes a requester of the atmosphere-sun flag; the flag is on while any requester holds it
void ASOLBodyVisuals::SetSunDrivesAtmosphere(const UObject* requester, const bool bDrives)
{
    if (bDrives)
    {
        mSunAtmosphereRequesters.Add(FObjectKey(requester));
    }
    else
    {
        mSunAtmosphereRequesters.Remove(FObjectKey(requester));
    }
    const bool bOn = mSunAtmosphereRequesters.Num() > 0;
    if (SunLight->IsUsedAsAtmosphereSunLight() != bOn)
    {
        SunLight->SetAtmosphereSunLight(bOn);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the sun light is flagged as the atmosphere sun light
bool ASOLBodyVisuals::IsSunDrivingAtmosphere() const
{
    return SunLight != nullptr && SunLight->IsUsedAsAtmosphereSunLight();
}

//////////////////////////////////////////////////////////////////////////
// Creates one mesh and material instance per body and subscribes to universe updates
void ASOLBodyVisuals::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    BodyRegistrySubsystem = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    if (AnchorSubsystem == nullptr || BodyRegistrySubsystem == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("BodyVisuals %s: universe subsystems missing; nothing will be drawn"), *GetName());
        return;
    }

    UStaticMesh* mesh = LoadObject<UStaticMesh>(nullptr, SOL::Paths::BODY_MESH);
    UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::BODY_MATERIAL);
    if (mesh == nullptr || material == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("BodyVisuals %s: failed to load %s or %s"), *GetName(), SOL::Paths::BODY_MESH,
            SOL::Paths::BODY_MATERIAL);
        return;
    }
    SunLight->SetIntensity(SunIlluminanceLux);

    // The ship sun light is the Sun light seen through a shadow: same colour and shape, only dimmer
    ShipSunLight->SetLightColor(SunLight->GetLightColor());
    ShipSunLight->SetUseTemperature(SunLight->bUseTemperature);
    ShipSunLight->SetTemperature(SunLight->Temperature);
    ShipSunLight->SetSpecularScale(SunLight->SpecularScale);
    ShipSunLight->SetDiffuseScale(SunLight->DiffuseScale);
    ShipSunLight->SetLightSourceAngle(SunLight->LightSourceAngle);
    ShipSunLight->SetLightSourceSoftAngle(SunLight->LightSourceSoftAngle);
    ShipSunLight->SetIntensity(SunIlluminanceLux);

    // One sphere and one material instance per body, created once; the per-frame update only moves them
    const FSOLBodyRegistry& registry = BodyRegistrySubsystem->GetRegistry();
    const int32 bodyCount = registry.Num();
    BodyMeshes.Reserve(bodyCount);
    BodyMaterials.Reserve(bodyCount);
    mRingProfileIndices.Reserve(bodyCount);
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const FName bodyName = registry.GetName(index);
        UStaticMeshComponent* bodyMesh = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Body_%s"),
            *bodyName.ToString()));
        bodyMesh->SetMobility(EComponentMobility::Movable);
        bodyMesh->SetStaticMesh(mesh);
        bodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        bodyMesh->SetCastShadow(false);
        bodyMesh->bAffectDistanceFieldLighting = false;
        bodyMesh->SetupAttachment(SceneRoot);

        UMaterialInstanceDynamic* bodyMaterial = UMaterialInstanceDynamic::Create(material, this);
        double albedoLuminance = 0.0;
        double emissiveLuminance = 0.0;
        if (const FSOLBodyAppearance* appearance = FindAppearance(bodyName))
        {
            ApplyAppearance(bodyMaterial, *appearance);

            // Exposure inputs: the surface's mean albedo (halfway between its two colors) and its own emission
            albedoLuminance = 0.5 * (appearance->ColorA.GetLuminance() + appearance->ColorB.GetLuminance());
            emissiveLuminance = appearance->Emissive.GetLuminance();
        }
        bodyMaterial->SetScalarParameterValue(FName(SOL::BodyMaterialParams::SUN_ILLUMINANCE), SunIlluminanceLux);

        // Cache the Sun-direction parameter slot so the per-frame update sets it by index without a name lookup
        int32 sunDirectionParamIndex = INDEX_NONE;
        bodyMaterial->InitializeVectorParameterAndGetIndex(FName(SOL::BodyMaterialParams::SUN_DIRECTION),
            FLinearColor::Black, sunDirectionParamIndex);
        bodyMesh->SetMaterial(0, bodyMaterial);
        bodyMesh->RegisterComponent();

        BodyMeshes.Add(bodyMesh);
        BodyMaterials.Add(bodyMaterial);
        mSunDirectionParamIndices.Add(sunDirectionParamIndex);
        mAlbedoLuminances.Add(albedoLuminance);
        mEmissiveLuminances.Add(emissiveLuminance);
        mRingProfileIndices.Add(SOLRingShadow::ProfileIndexForPlanet(bodyName));
    }
    mSunIndex = registry.FindByName(FName(SOL::BodyNames::SUN));

    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
        &ASOLBodyVisuals::HandleUniverseUpdated);
    HandleUniverseUpdated();
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from universe updates
void ASOLBodyVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mUniverseUpdatedHandle.Reset();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Places and orients every body mesh, updates each body's Sun direction and re-aims the sun light this frame
void ASOLBodyVisuals::HandleUniverseUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLBodyVisuals::HandleUniverseUpdated);

    const FSOLBodyRegistry& registry = BodyRegistrySubsystem->GetRegistry();
    const FVector3d sunPositionM = mSunIndex != INDEX_NONE ? registry.GetPositionM(mSunIndex) : FVector3d::ZeroVector;

    // Observer-relative placement (exact nearby, depth-compressed far away), axial orientation and sunlight direction
    const int32 bodyCount = BodyMeshes.Num();
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const FSOLRenderPlacement placement = AnchorSubsystem->ComputeBodyRenderPlacement(index);
        const FQuat bodyRotation(SOLRender::EclipticToUnreal(registry.GetOrientation(index)));
        BodyMeshes[index]->SetWorldTransform(FTransform(bodyRotation, FVector(placement.LocationCm),
            FVector(placement.RadiusCm / SOL::BODY_MESH_RADIUS_CM)));

        // Unit vector from the body toward the Sun in Unreal axes; zero for the Sun itself (it is emissive)
        const FVector3d towardSun = SOLRender::EclipticToUnreal(sunPositionM - registry.GetPositionM(index))
            .GetSafeNormal();
        BodyMaterials[index]->SetVectorParameterByIndex(mSunDirectionParamIndices[index],
            FLinearColor(static_cast<float>(towardSun.X), static_cast<float>(towardSun.Y),
                static_cast<float>(towardSun.Z), 0.0f));
    }

    // The light travels from the Sun toward the observer, for lit objects near the observer
    if (mSunIndex != INDEX_NONE)
    {
        const FVector3d sunToObserverM = AnchorSubsystem->GetObserverPositionM() - sunPositionM;
        const FVector3d lightDirection = SOLRender::EclipticToUnreal(sunToObserverM).GetSafeNormal();
        if (!lightDirection.IsNearlyZero())
        {
            SunLight->SetWorldRotation(FVector(lightDirection).Rotation());
        }
        UpdateShipSunlight(sunPositionM);
    }

    UpdateExposure(sunPositionM);
}

//////////////////////////////////////////////////////////////////////////
// Shadows the ship's and targets' sunlight by every body and ring at the observer (SDD 16, 16d): one CPU evaluation
// per frame (the ship and targets are metres across, every penumbra kilometres wide), applied by lighting channel
void ASOLBodyVisuals::UpdateShipSunlight(const FVector3d& sunPositionM)
{
    const FSOLBodyRegistry& registry = BodyRegistrySubsystem->GetRegistry();
    const FVector3d observerM = AnchorSubsystem->GetObserverPositionM();
    const FVector3d towardSun = sunPositionM - observerM;
    const double sunAngularRadiusRad = SOLRingShadow::SunAngularRadiusRad(towardSun.Length());

    // Product of every body's planet shadow and, for the ringed planets, their ring shadow (none while sol.RingShadows
    // is 0)
    double transmittance = 1.0;
    const int32 bodyCount = SOLRingShadow::AreShadowsEnabled() ? mRingProfileIndices.Num() : 0;
    for (int32 index = 0; index < bodyCount && transmittance > 0.0; ++index)
    {
        if (index == mSunIndex)
        {
            continue;
        }
        const int32 profileIndex = mRingProfileIndices[index];
        const FVector3d ringNormal = profileIndex != INDEX_NONE ?
            registry.GetOrientation(index).RotateVector(FVector3d(0.0, 0.0, 1.0)) : FVector3d::ZeroVector;
        transmittance *= SOLRingShadow::BodySunTransmittance(observerM - registry.GetPositionM(index), towardSun,
            registry.GetRadiusM(index), sunAngularRadiusRad, ringNormal, SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M,
            SOLRingShadow::RealTauProfile(profileIndex));
    }
    mShipSunTransmittance = transmittance;

    // Lit: the Sun light covers both channels and the ship light stays off (no extra light pass); shadowed: the Sun
    // light keeps channel 0 (the rocks shadow themselves per pixel) and the ship light lights channel 1, dimmed.
    // Visibility and channel changes rebuild the light proxies, so they happen only on a transition
    const bool bShadowed = SOLRingShadow::NextShipShadowedState(mIsShipShadowed, transmittance);
    if (bShadowed != mIsShipShadowed)
    {
        mIsShipShadowed = bShadowed;
        SunLight->SetLightingChannels(true, !bShadowed, false);
        ShipSunLight->SetVisibility(bShadowed);
        UE_LOG(LogSOL, Verbose, TEXT("BodyVisuals %s: ship sunlight %s (%.6f of full)"), *GetName(),
            bShadowed ? TEXT("shadowed") : TEXT("lit"), transmittance);
    }
    if (bShadowed)
    {
        ShipSunLight->SetWorldRotation(SunLight->GetComponentRotation());
        const float intensity = SunIlluminanceLux * static_cast<float>(transmittance);
        if (ShipSunLight->Intensity != intensity)
        {
            ShipSunLight->SetIntensity(intensity);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves the manual exposure toward the target set by the bodies in view (SDD 13 decision 2, Amendment 1)
void ASOLBodyVisuals::UpdateExposure(const FVector3d& sunPositionM)
{
    const UWorld* world = GetWorld();
    const APlayerController* playerController = world->GetFirstPlayerController();
    const APlayerCameraManager* cameraManager = playerController != nullptr ? playerController->PlayerCameraManager
        : nullptr;
    if (cameraManager == nullptr)
    {
        return;
    }

    // The view as last rendered: position and axis in Unreal space, and its solid angle
    const FMinimalViewInfo& view = cameraManager->GetCameraCacheView();
    const FVector3d cameraCm = view.Location;
    const FVector3d viewAxis = view.Rotation.Vector();
    const double tanHalfFovX = FMath::Tan(FMath::DegreesToRadians(0.5 * static_cast<double>(view.FOV)));
    // The FOV is horizontal across the viewport (Config/DefaultEngine.ini, issue #25), so the vertical extent follows
    // the viewport's own aspect ratio, not the camera's 16:9 default
    FVector2D viewportPx(16.0, 9.0);
    if (const UGameViewportClient* gameViewport = world->GetGameViewport())
    {
        gameViewport->GetViewportSize(viewportPx);
    }
    const double aspectRatio = viewportPx.Y > 0.0 ? viewportPx.X / viewportPx.Y : 16.0 / 9.0;
    const double tanHalfFovY = tanHalfFovX / FMath::Max(aspectRatio, UE_DOUBLE_KINDA_SMALL_NUMBER);
    const double viewSr = SOLSceneExposure::ViewSolidAngleSr(tanHalfFovX, tanHalfFovY);

    // Mean radiance of the view: each body's share of it times its mean radiance at its phase angle (the sky is ~0)
    const FSOLBodyRegistry& registry = BodyRegistrySubsystem->GetRegistry();
    double meanRadiance = 0.0;
    const int32 bodyCount = BodyMeshes.Num();
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const FSOLRenderPlacement placement = AnchorSubsystem->ComputeBodyRenderPlacement(index);
        const FVector3d toBody = placement.LocationCm - cameraCm;
        const double distanceCm = toBody.Size();
        if (distanceCm <= UE_DOUBLE_KINDA_SMALL_NUMBER)
        {
            continue;
        }
        const FVector3d toBodyDir = toBody / distanceCm;
        const double angularRadius = FMath::Asin(FMath::Min(1.0, placement.RadiusCm / distanceCm));
        const double offAxis = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(toBodyDir, viewAxis), -1.0, 1.0));
        const double coverage = SOLSceneExposure::DiscViewCoverage(angularRadius, offAxis, viewSr);
        if (coverage <= 0.0)
        {
            continue;
        }

        // Phase angle Sun-body-camera; the Sun itself is emissive, so its phase does not matter
        const FVector3d towardSun = SOLRender::EclipticToUnreal(sunPositionM - registry.GetPositionM(index))
            .GetSafeNormal();
        const double phase = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(-toBodyDir, towardSun), -1.0, 1.0));
        meanRadiance += coverage * SOLSceneExposure::BodyMeanRadiance(mEmissiveLuminances[index],
            mAlbedoLuminances[index], SunIlluminanceLux, phase);
    }

    // Exponential approach to the target, frame-rate independent; the first frame starts at the target
    const double target = SOLSceneExposure::TargetEv100(meanRadiance, SOL::EXPOSURE_KEY_RADIANCE,
        SOL::EXPOSURE_MIN_EV100, SOL::EXPOSURE_MAX_EV100);
    mExposureEv100 = mExposureInitialized
        ? SOLSceneExposure::SmoothEv100(mExposureEv100, target, world->GetDeltaSeconds(),
            SOL::EXPOSURE_TIME_CONSTANT_SECONDS)
        : target;
    mExposureInitialized = true;
    PostProcess->Settings.AutoExposureBias = static_cast<float>(
        SOLSceneExposure::ExposureBias(mExposureEv100, SOL::EXPOSURE_MIN_EV100));
}
