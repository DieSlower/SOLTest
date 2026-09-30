/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLBodyVisuals.h"

#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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

    // Real-planet placeholder colors (linear); bands follow latitude, noise gives surface patches
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
            0.09f, 0.7f, 0.0f, 0.0f, 0.0f, VISUALS_NO_POLAR_CAP },
        { TEXT("Uranus"), { 0.55f, 0.82f, 0.88f }, { 0.62f, 0.88f, 0.9f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.04f, 0.2f, 0.0f, 0.0f, 0.0f, VISUALS_NO_POLAR_CAP },
        { TEXT("Neptune"), { 0.08f, 0.18f, 0.7f }, { 0.14f, 0.3f, 0.85f }, VISUALS_WHITE, VISUALS_NO_EMISSIVE,
            0.05f, 0.4f, 0.0f, 0.0f, 0.0f, VISUALS_NO_POLAR_CAP },
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

    // Fixed exposure (no eye adaptation) so the lit planets and the emissive Sun keep a stable brightness
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

    // One sphere and one material instance per body, created once; the per-frame update only moves them
    const FSOLBodyRegistry& registry = BodyRegistrySubsystem->GetRegistry();
    const int32 bodyCount = registry.Num();
    BodyMeshes.Reserve(bodyCount);
    BodyMaterials.Reserve(bodyCount);
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
        if (const FSOLBodyAppearance* appearance = FindAppearance(bodyName))
        {
            ApplyAppearance(bodyMaterial, *appearance);
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
// Places every body mesh, updates each body's Sun direction and re-aims the sun light for the current frame
void ASOLBodyVisuals::HandleUniverseUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLBodyVisuals::HandleUniverseUpdated);

    const FSOLBodyRegistry& registry = BodyRegistrySubsystem->GetRegistry();
    const FVector3d sunPositionM = mSunIndex != INDEX_NONE ? registry.GetPositionM(mSunIndex) : FVector3d::ZeroVector;

    // Observer-relative placement (exact nearby, depth-compressed far away) and per-body sunlight direction
    const int32 bodyCount = BodyMeshes.Num();
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const FSOLRenderPlacement placement = AnchorSubsystem->ComputeBodyRenderPlacement(index);
        BodyMeshes[index]->SetWorldTransform(FTransform(FQuat::Identity, FVector(placement.LocationCm),
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
    }
}
