/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "GasGiants/SOLGasGiantVisuals.h"

#include "MinorBodies/SOLRingShadowMath.h"
#include "MinorBodies/SOLRingShadowSwitch.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"
#include "Visuals/SOLBodyVisuals.h"

#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/StaticMesh.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"

namespace
{
    // Look and motion of one giant: material colours and shape, differential drift (rad/s relative to the body's
    // spin, before SOL::GAS_GIANT_BAND_SPEED_FACTOR) and storms (an optional authored primary storm plus generated
    // ovals)
    struct FSOLGasGiantAppearance
    {
        const TCHAR* Name;
        FLinearColor Zone;
        FLinearColor Belt;
        FLinearColor Accent;
        FLinearColor Polar;
        FLinearColor Storm;
        float BandsPerSinLat;
        float Turbulence;
        float NoiseScale;
        float PolarStart;
        float HazeOpacity;
        double EquatorDriftRadPerS;
        double PolarDriftRadPerS;
        int32 GeneratedStorms;
        bool bHasPrimaryStorm;
        SOLGasGiant::FStorm PrimaryStorm;
    };

    // Jupiter is the tuning reference (Great Red Spot at 22.4 deg S, ~16,000 km long); real equatorial jets are
    // ~100 m/s on Jupiter, ~400 m/s on Saturn and retrograde on Uranus and Neptune, softened here where the 1x speed
    // factor would make them blur
    const FSOLGasGiantAppearance GAS_GIANT_APPEARANCES[] =
    {
        { SOL::BodyNames::JUPITER, { 0.85f, 0.74f, 0.55f }, { 0.5f, 0.28f, 0.13f }, { 0.7f, 0.46f, 0.28f },
            { 0.42f, 0.4f, 0.4f }, { 0.72f, 0.3f, 0.14f }, 4.5f, 0.09f, 6.0f, 0.8f, 0.2f, 1.4e-6, -0.3e-6, 5,
            true, { -0.391, 1.0, 0.07 } },
        { SOL::BodyNames::SATURN, { 0.88f, 0.8f, 0.6f }, { 0.74f, 0.62f, 0.4f }, { 0.82f, 0.72f, 0.52f },
            { 0.55f, 0.58f, 0.62f }, { 0.95f, 0.92f, 0.85f }, 5.0f, 0.03f, 5.0f, 0.88f, 0.25f, 3.0e-6, -0.5e-6, 3,
            false, {} },
        { SOL::BodyNames::URANUS, { 0.62f, 0.86f, 0.9f }, { 0.55f, 0.8f, 0.86f }, { 0.7f, 0.9f, 0.92f },
            { 0.75f, 0.92f, 0.94f }, { 0.85f, 0.95f, 0.97f }, 3.0f, 0.02f, 4.0f, 0.6f, 0.15f, -2.0e-6, 3.0e-6, 2,
            false, {} },
        { SOL::BodyNames::NEPTUNE, { 0.2f, 0.36f, 0.85f }, { 0.1f, 0.22f, 0.7f }, { 0.45f, 0.6f, 0.95f },
            { 0.15f, 0.28f, 0.75f }, { 0.04f, 0.08f, 0.35f }, 4.0f, 0.04f, 5.0f, 0.85f, 0.3f, -4.0e-6, 2.0e-6, 3,
            true, { -0.35, 3.0, 0.06 } },
    };

    const TCHAR* const GAS_GIANT_BODY_NAMES[] = { SOL::BodyNames::JUPITER, SOL::BodyNames::SATURN,
        SOL::BodyNames::URANUS, SOL::BodyNames::NEPTUNE };

    //////////////////////////////////////////////////////////////////////////
    // Returns the appearance row for a giant's name, or INDEX_NONE
    int32 FindGasGiantAppearance(const FName name)
    {
        for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(GAS_GIANT_APPEARANCES)); ++index)
        {
            if (name == FName(GAS_GIANT_APPEARANCES[index].Name))
            {
                return index;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates one shell mesh component attached to the root: movable, no collision, no shadows
    UStaticMeshComponent* CreateGasGiantShell(AActor* owner, USceneComponent* root, const TCHAR* name)
    {
        UStaticMeshComponent* shell = owner->CreateDefaultSubobject<UStaticMeshComponent>(name);
        shell->SetupAttachment(root);
        shell->SetMobility(EComponentMobility::Movable);
        shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        shell->SetCastShadow(false);
        shell->bAffectDistanceFieldLighting = false;
        return shell;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if a render-space sphere may be in the first local player's view (true without a camera, e.g. in
    // tests); a cone over the horizontal FOV's diagonal, conservative for any aspect ratio of at least 1
    bool IsRenderSphereInPlayerView(const UWorld* world, const FVector& centerCm, const double radiusCm)
    {
        const APlayerController* controller = world != nullptr ? world->GetFirstPlayerController() : nullptr;
        const APlayerCameraManager* camera = controller != nullptr ? controller->PlayerCameraManager.Get() : nullptr;
        if (camera == nullptr)
        {
            return true;
        }
        const double halfAngleRad = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(camera->GetFOVAngle() * 0.5)) *
            UE_SQRT_2);
        return SOLGasGiant::IsSphereInViewCone(FVector3d(camera->GetCameraLocation()),
            FVector3d(camera->GetCameraRotation().Vector()), halfAngleRad, FVector3d(centerCm), radiusCm);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns 4 consecutive phases as a material vector
    FLinearColor GasGiantPhaseVector(const float* phases)
    {
        return FLinearColor(phases[0], phases[1], phases[2], phases[3]);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the root and the two shell meshes
ASOLGasGiantVisuals::ASOLGasGiantVisuals()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
    DeckMesh = CreateGasGiantShell(this, SceneRoot, TEXT("DeckMesh"));
    HazeMesh = CreateGasGiantShell(this, SceneRoot, TEXT("HazeMesh"));
    HazeMesh->TranslucencySortPriority = SOL::GAS_GIANT_HAZE_TRANSLUCENCY_SORT_PRIORITY;
}

//////////////////////////////////////////////////////////////////////////
// Returns the registry names of the four giants
TConstArrayView<const TCHAR*> ASOLGasGiantVisuals::GetGasGiantBodyNames()
{
    return MakeArrayView(GAS_GIANT_BODY_NAMES);
}

//////////////////////////////////////////////////////////////////////////
// Resolves the body, creates the material instances, generates the storms and subscribes to universe updates
void ASOLGasGiantVisuals::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    BodyRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    SimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    if (AnchorSubsystem == nullptr || BodyRegistry == nullptr || SimClock == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("GasGiantVisuals %s: universe subsystems missing; nothing will be drawn"),
            *GetName());
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    mBodyIndex = registry.FindByName(PlanetName);
    mSunIndex = registry.FindByName(FName(SOL::BodyNames::SUN));
    mAppearanceIndex = FindGasGiantAppearance(PlanetName);
    if (mBodyIndex == INDEX_NONE || mAppearanceIndex == INDEX_NONE)
    {
        UE_LOG(LogSOL, Error, TEXT("GasGiantVisuals %s: no body or appearance for '%s'"), *GetName(),
            *PlanetName.ToString());
        return;
    }

    // Shared mesh and materials; a missing material leaves the placeholder body mesh in place
    UStaticMesh* mesh = LoadObject<UStaticMesh>(nullptr, SOL::Paths::BODY_MESH);
    UMaterialInterface* deck = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::GAS_GIANT_DECK_MATERIAL);
    UMaterialInterface* haze = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::GAS_GIANT_HAZE_MATERIAL);
    CloudMaterialAsset = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::GAS_GIANT_CLOUD_MATERIAL);
    if (CloudMaterialAsset == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("GasGiantVisuals %s: failed to load %s (run "
            "Tools/GasGiants/create_gas_giant_materials.py); no near cloud layer"), *GetName(),
            SOL::Paths::GAS_GIANT_CLOUD_MATERIAL);
    }
    if (mesh == nullptr || deck == nullptr || haze == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("GasGiantVisuals %s: failed to load %s, %s or %s (run "
            "Tools/GasGiants/create_gas_giant_materials.py); keeping the placeholder body"), *GetName(),
            SOL::Paths::BODY_MESH, SOL::Paths::GAS_GIANT_DECK_MATERIAL, SOL::Paths::GAS_GIANT_HAZE_MATERIAL);
        mBodyIndex = INDEX_NONE;
        return;
    }
    DeckMesh->SetStaticMesh(mesh);
    HazeMesh->SetStaticMesh(mesh);
    DeckMaterial = UMaterialInstanceDynamic::Create(deck, this);
    HazeMaterial = UMaterialInstanceDynamic::Create(haze, this);

    // Per-planet look, written once
    using namespace SOL::GasGiantMaterialParams;
    const FSOLGasGiantAppearance& look = GAS_GIANT_APPEARANCES[mAppearanceIndex];
    const FLinearColor shape(look.BandsPerSinLat, look.Turbulence, look.NoiseScale, look.PolarStart);
    const FLinearColor hazeShape(look.BandsPerSinLat, look.HazeOpacity, look.NoiseScale, look.PolarStart);
    DeckMaterial->SetVectorParameterValue(FName(ZONE_COLOR), look.Zone);
    DeckMaterial->SetVectorParameterValue(FName(BELT_COLOR), look.Belt);
    DeckMaterial->SetVectorParameterValue(FName(ACCENT_COLOR), look.Accent);
    DeckMaterial->SetVectorParameterValue(FName(POLAR_COLOR), look.Polar);
    DeckMaterial->SetVectorParameterValue(FName(STORM_COLOR), look.Storm);
    DeckMaterial->SetVectorParameterValue(FName(SHAPE_PARAMS), shape);
    DeckMaterial->SetScalarParameterValue(FName(SUN_ILLUMINANCE), SOL::SUN_ILLUMINANCE_LUX);
    DeckMaterial->SetScalarParameterValue(FName(AMBIENT_LIGHT), SOL::GAS_GIANT_AMBIENT_LIGHT);
    DeckMaterial->SetVectorParameterValue(FName(STORM_SHAPE), FLinearColor(
        static_cast<float>(SOL::GAS_GIANT_STORM_EAST_WEST_STRETCH), SOL::GAS_GIANT_PRIMARY_STORM_KIND_THRESHOLD,
        0.0f, 0.0f));
    HazeMaterial->SetVectorParameterValue(FName(ZONE_COLOR), look.Zone);
    HazeMaterial->SetVectorParameterValue(FName(SHAPE_PARAMS), hazeShape);
    HazeMaterial->SetScalarParameterValue(FName(SUN_ILLUMINANCE), SOL::SUN_ILLUMINANCE_LUX);
    HazeMaterial->SetScalarParameterValue(FName(AMBIENT_LIGHT), SOL::GAS_GIANT_AMBIENT_LIGHT);

    // Cache the per-frame parameter slots so updates set them by index without name lookups
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(BAND_PHASES)); ++index)
    {
        DeckMaterial->InitializeVectorParameterAndGetIndex(FName(BAND_PHASES[index]), FLinearColor::Black,
            mBandParamIndices[index]);
        HazeMaterial->InitializeVectorParameterAndGetIndex(FName(HAZE_PHASES[index]), FLinearColor::Black,
            mHazeParamIndices[index]);
    }
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(STORMS)); ++index)
    {
        DeckMaterial->InitializeVectorParameterAndGetIndex(FName(STORMS[index]), FLinearColor::Black,
            mStormParamIndices[index]);
    }
    HazeMaterial->InitializeVectorParameterAndGetIndex(FName(SHAPE_PARAMS), hazeShape, mHazeShapeParamIndex);
    mHazeOpacity = look.HazeOpacity;
    DeckMaterial->InitializeVectorParameterAndGetIndex(FName(SUN_DIRECTION), FLinearColor::Black, mDeckSunParamIndex);
    HazeMaterial->InitializeVectorParameterAndGetIndex(FName(SUN_DIRECTION), FLinearColor::Black, mHazeSunParamIndex);
    // Ring shadow on the deck and haze (SDD 16): static profile and radius, per-frame Sun in the mesh's local axes
    mRingShadowParams = FLinearColor(static_cast<float>(SOLRingShadow::ProfileIndexForPlanet(PlanetName)),
        static_cast<float>(registry.GetRadiusM(mBodyIndex) / SOL::METERS_PER_KM), 0.0f, 0.0f);
    DeckMaterial->SetVectorParameterValue(FName(RING_SHADOW_PARAMS), mRingShadowParams);
    HazeMaterial->SetVectorParameterValue(FName(RING_SHADOW_PARAMS), mRingShadowParams);
    DeckMaterial->InitializeVectorParameterAndGetIndex(FName(RING_SHADOW_SUN), FLinearColor::Transparent,
        mDeckRingSunParamIndex);
    HazeMaterial->InitializeVectorParameterAndGetIndex(FName(RING_SHADOW_SUN), FLinearColor::Transparent,
        mHazeRingSunParamIndex);
    DeckMesh->SetMaterial(0, DeckMaterial);
    HazeMesh->SetMaterial(0, HazeMaterial);

    // Storms, once: the authored primary storm first, then generated ovals that do not overlap it
    mStorms.Reset(SOL::GAS_GIANT_MAX_STORMS);
    if (look.bHasPrimaryStorm)
    {
        mStorms.Add(look.PrimaryStorm);
    }
    TArray<SOLGasGiant::FStorm> generated;
    SOLGasGiant::GenerateStorms(SOL::GAS_GIANT_STORM_SEED + mBodyIndex, look.GeneratedStorms,
        SOL::GAS_GIANT_STORM_MAX_LATITUDE_RAD, SOL::GAS_GIANT_STORM_MIN_RADIUS_RAD, SOL::GAS_GIANT_STORM_MAX_RADIUS_RAD,
        generated);
    for (const SOLGasGiant::FStorm& storm : generated)
    {
        const bool bClearOfPrimary = !look.bHasPrimaryStorm ||
            FMath::Abs(storm.LatitudeRad - look.PrimaryStorm.LatitudeRad) >
            storm.RadiusRad + SOL::GAS_GIANT_STORM_EAST_WEST_STRETCH * look.PrimaryStorm.RadiusRad;
        if (bClearOfPrimary && mStorms.Num() < SOL::GAS_GIANT_MAX_STORMS)
        {
            mStorms.Add(storm);
        }
    }

    // The drift clock starts at the sim time, so drift is the exact function of sim time until warp exceeds the cap
    mLastSimSeconds = SimClock->GetClock().GetSecondsSinceJ2000();
    mDriftSeconds = mLastSimSeconds;

    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
        &ASOLGasGiantVisuals::HandleUniverseUpdated);
    HandleUniverseUpdated();
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from universe updates and shows the placeholder body mesh again
void ASOLGasGiantVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mUniverseUpdatedHandle.Reset();
    ReleaseNearLayer();
    if (mIsBodyMeshHidden && IsValid(BodyVisuals))
    {
        BodyVisuals->SetBodyMeshHidden(mBodyIndex, false);
    }
    mIsBodyMeshHidden = false;
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Places both shells like the body mesh, and pushes this frame's band phases, storm positions and Sun direction
void ASOLGasGiantVisuals::HandleUniverseUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLGasGiantVisuals::HandleUniverseUpdated);
    if (mBodyIndex == INDEX_NONE)
    {
        return;
    }

    // The deck replaces the placeholder body mesh (retried until the body visuals have created their meshes)
    if (!mIsBodyMeshHidden && BodyVisuals != nullptr)
    {
        mIsBodyMeshHidden = BodyVisuals->SetBodyMeshHidden(mBodyIndex, true);
    }

    // Same placement and orientation as the body mesh; the haze sits just above the deck
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const FSOLRenderPlacement placement = AnchorSubsystem->ComputeBodyRenderPlacement(mBodyIndex);
    const FQuat rotation(SOLRender::EclipticToUnreal(registry.GetOrientation(mBodyIndex)));
    const double deckScale = placement.RadiusCm / SOL::BODY_MESH_RADIUS_CM;
    DeckMesh->SetWorldTransform(FTransform(rotation, FVector(placement.LocationCm), FVector(deckScale)));
    HazeMesh->SetWorldTransform(FTransform(rotation, FVector(placement.LocationCm),
        FVector(deckScale * SOL::GAS_GIANT_HAZE_RADIUS_SCALE)));

    // Ring shadow input: the Sun in the meshes' local axes (z = pole = ring normal) and its angular radius (SDD 16)
    const FVector3d towardSunEcliptic = (mSunIndex != INDEX_NONE ? registry.GetPositionM(mSunIndex) :
        FVector3d::ZeroVector) - registry.GetPositionM(mBodyIndex);
    const FVector4d ringSun = SOLRingShadow::SunDirectionAndAngularRadius(
        FVector3d(rotation.UnrotateVector(FVector(SOLRender::EclipticToUnreal(towardSunEcliptic)))),
        towardSunEcliptic.Length());
    mRingShadowSun = FLinearColor(static_cast<float>(ringSun.X), static_cast<float>(ringSun.Y),
        static_cast<float>(ringSun.Z), static_cast<float>(ringSun.W));
    // A zero Sun vector switches the shadow off in the material (the profiling switch)
    if (!SOLRingShadow::AreShadowsEnabled())
    {
        mRingShadowSun = FLinearColor::Transparent;
    }

    // Near volumetric layer; the haze shell fades out as it fades in (hidden once fully replaced)
    const double nearBlend = UpdateNearLayer(placement.LocationCm, rotation, registry.GetRadiusM(mBodyIndex));
    const FSOLGasGiantAppearance& look = GAS_GIANT_APPEARANCES[mAppearanceIndex];
    const float hazeOpacity = static_cast<float>(look.HazeOpacity * SOLGasGiant::HazeFadeForNearBlend(nearBlend));
    if (hazeOpacity != mHazeOpacity)
    {
        mHazeOpacity = hazeOpacity;
        HazeMaterial->SetVectorParameterByIndex(mHazeShapeParamIndex, FLinearColor(look.BandsPerSinLat, hazeOpacity,
            look.NoiseScale, look.PolarStart));
        HazeMesh->SetVisibility(hazeOpacity > 0.0f);
    }

    // Drift clock: sim time, but no faster than GAS_GIANT_BAND_MAX_TIME_WARP so high warp does not alias the bands
    const double simSeconds = SimClock->GetClock().GetSecondsSinceJ2000();
    mDriftSeconds += SOLGasGiant::CappedDriftStepSeconds(simSeconds - mLastSimSeconds, FApp::GetDeltaTime(),
        SOL::GAS_GIANT_BAND_MAX_TIME_WARP);
    mLastSimSeconds = simSeconds;

    // Material updates only while the giant may be on screen: a shell rendered recently, the near layer shown, or the
    // giant in the camera's view cone (so the first frame back in view already has current parameters)
    if (!mHasAnimated || nearBlend > 0.0 || DeckMesh->WasRecentlyRendered(SOL::GAS_GIANT_RECENT_RENDER_SECONDS) ||
        HazeMesh->WasRecentlyRendered(SOL::GAS_GIANT_RECENT_RENDER_SECONDS) ||
        IsRenderSphereInPlayerView(GetWorld(), FVector(placement.LocationCm),
            placement.RadiusCm * SOL::GAS_GIANT_HAZE_RADIUS_SCALE))
    {
        mHasAnimated = true;
        const FVector3d sunM = mSunIndex != INDEX_NONE ? registry.GetPositionM(mSunIndex) : FVector3d::ZeroVector;
        const FVector3d towardSun = SOLRender::EclipticToUnreal(sunM - registry.GetPositionM(mBodyIndex))
            .GetSafeNormal();
        const FLinearColor sunDirection(static_cast<float>(towardSun.X), static_cast<float>(towardSun.Y),
            static_cast<float>(towardSun.Z), 0.0f);
        DeckMaterial->SetVectorParameterByIndex(mDeckSunParamIndex, sunDirection);
        HazeMaterial->SetVectorParameterByIndex(mHazeSunParamIndex, sunDirection);
        DeckMaterial->SetVectorParameterByIndex(mDeckRingSunParamIndex, mRingShadowSun);
        HazeMaterial->SetVectorParameterByIndex(mHazeRingSunParamIndex, mRingShadowSun);
        UpdateAnimation(mDriftSeconds);
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes this frame's band/haze phases and storm longitudes (at the drift clock) into the material instances
void ASOLGasGiantVisuals::UpdateAnimation(const double driftSeconds)
{
    const FSOLGasGiantAppearance& look = GAS_GIANT_APPEARANCES[mAppearanceIndex];
    const double factor = SOL::GAS_GIANT_BAND_SPEED_FACTOR;
    const TArrayView<float> phases = MakeArrayView(mPhaseScratch);

    // Deck bands, then the faster haze, 4 phases per vector parameter
    SOLGasGiant::ComputeBandPhases(driftSeconds, look.EquatorDriftRadPerS, look.PolarDriftRadPerS, factor, phases);
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(mBandParamIndices)); ++index)
    {
        const FLinearColor bandPhases = GasGiantPhaseVector(&mPhaseScratch[4 * index]);
        DeckMaterial->SetVectorParameterByIndex(mBandParamIndices[index], bandPhases);
        if (CloudMaterial != nullptr)
        {
            CloudMaterial->SetVectorParameterByIndex(mCloudBandParamIndices[index], bandPhases);
        }
    }
    SOLGasGiant::ComputeBandPhases(driftSeconds, look.EquatorDriftRadPerS, look.PolarDriftRadPerS,
        factor * SOL::GAS_GIANT_HAZE_SPEED_SCALE, phases);
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(mHazeParamIndices)); ++index)
    {
        HazeMaterial->SetVectorParameterByIndex(mHazeParamIndices[index],
            GasGiantPhaseVector(&mPhaseScratch[4 * index]));
    }

    // Storms ride their bands; unused slots are zero (kind 0 = none)
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(mStormParamIndices)); ++index)
    {
        FLinearColor value = FLinearColor::Transparent;
        if (mStorms.IsValidIndex(index))
        {
            const SOLGasGiant::FStorm& storm = mStorms[index];
            const double longitude = SOLGasGiant::StormLongitudeRad(storm, driftSeconds,
                look.EquatorDriftRadPerS, look.PolarDriftRadPerS, factor);
            const bool bPrimary = look.bHasPrimaryStorm && index == 0;
            const float kind = bPrimary ? SOL::GAS_GIANT_PRIMARY_STORM_KIND : SOL::GAS_GIANT_OVAL_STORM_KIND;
            value = FLinearColor(static_cast<float>(storm.LatitudeRad), static_cast<float>(longitude),
                static_cast<float>(storm.RadiusRad), kind);
        }
        DeckMaterial->SetVectorParameterByIndex(mStormParamIndices[index], value);
    }
}

//////////////////////////////////////////////////////////////////////////
// Advances the near volumetric layer (blend from the observer distance, delayed release) and places it
double ASOLGasGiantVisuals::UpdateNearLayer(const FVector3d& centerCm, const FQuat& rotation, const double radiusM)
{
    // Blend from the exact (double, metres) observer distance; real seconds drive the release timer
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const double distanceM = FVector3d::Distance(AnchorSubsystem->GetObserverPositionM(),
        registry.GetPositionM(mBodyIndex));
    // Without the cloud material the layer stays permanently inert (warned once in BeginPlay)
    const double blend = CloudMaterialAsset == nullptr ? 0.0 : SOLGasGiant::NearLayerBlend(distanceM, radiusM,
        SOL::GAS_GIANT_NEAR_FADE_IN_RADII, SOL::GAS_GIANT_NEAR_FADE_OUT_RADII);
    const SOLGasGiant::ENearLayerAction action = SOLGasGiant::StepNearLayer(mNearLayer, blend, FApp::GetDeltaTime(),
        SOL::GAS_GIANT_NEAR_RELEASE_DELAY_SECONDS);
    if (action == SOLGasGiant::ENearLayerAction::Activate)
    {
        ActivateNearLayer(radiusM);
    }
    else if (action == SOLGasGiant::ENearLayerAction::Release)
    {
        ReleaseNearLayer();
    }

    const bool bShown = SOLGasGiant::IsNearLayerVisible(blend) && NearClouds != nullptr;
    if (bShown != mIsNearLayerShown)
    {
        SetNearLayerShown(bShown);
    }
    if (!bShown)
    {
        return 0.0;
    }

    // The atmosphere component's location is the planet centre for the cloud renderer; the material gets the same
    // centre and the body axes to find latitude and band-advected longitude
    NearAtmosphere->SetWorldLocation(FVector(centerCm));
    CloudMaterial->SetVectorParameterByIndex(mCloudCenterParamIndex, FLinearColor(static_cast<float>(centerCm.X),
        static_cast<float>(centerCm.Y), static_cast<float>(centerCm.Z),
        static_cast<float>(radiusM * SOL::METERS_TO_CM)));
    CloudMaterial->SetVectorParameterByIndex(mCloudAxisParamIndices[0], FLinearColor(FVector3f(rotation.GetAxisX())));
    CloudMaterial->SetVectorParameterByIndex(mCloudAxisParamIndices[1], FLinearColor(FVector3f(rotation.GetAxisY())));
    CloudMaterial->SetVectorParameterByIndex(mCloudAxisParamIndices[2], FLinearColor(FVector3f(rotation.GetAxisZ())));
    CloudMaterial->SetScalarParameterByIndex(mCloudBlendParamIndex, static_cast<float>(blend));
    CloudMaterial->SetVectorParameterByIndex(mCloudRingSunParamIndex, mRingShadowSun);
    return blend;
}

//////////////////////////////////////////////////////////////////////////
// Creates and registers the centre-carrying atmosphere and the volumetric cloud layer, hidden until shown
void ASOLGasGiantVisuals::ActivateNearLayer(const double radiusM)
{
    const float radiusKm = static_cast<float>(radiusM / 1000.0);

    // Planet-centre carrier only: no scattering, absorption or sky/aerial-perspective luminance, not drawn in the
    // main pass, so planets, stars and rings elsewhere are untouched (SDD 14 Amendment 3). Unique names: a destroyed
    // previous component may still await garbage collection under its name
    NearAtmosphere = NewObject<USkyAtmosphereComponent>(this, NAME_None);
    NearAtmosphere->SetupAttachment(SceneRoot);
    NearAtmosphere->SetMobility(EComponentMobility::Movable);
    NearAtmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
    NearAtmosphere->BottomRadius = radiusKm;
    NearAtmosphere->AtmosphereHeight = SOL::GAS_GIANT_CLOUD_LAYER_BOTTOM_KM + SOL::GAS_GIANT_CLOUD_LAYER_HEIGHT_KM;
    NearAtmosphere->RayleighScatteringScale = 0.0f;
    NearAtmosphere->MieScatteringScale = 0.0f;
    NearAtmosphere->MieAbsorptionScale = 0.0f;
    NearAtmosphere->OtherAbsorptionScale = 0.0f;
    NearAtmosphere->MultiScatteringFactor = 0.0f;
    NearAtmosphere->SkyLuminanceFactor = FLinearColor::Black;
    NearAtmosphere->SkyAndAerialPerspectiveLuminanceFactor = FLinearColor::Black;
    NearAtmosphere->AerialPespectiveViewDistanceScale = 0.0f;
    NearAtmosphere->HeightFogContribution = 0.0f;
    NearAtmosphere->bRenderInMainPass = false;
    NearAtmosphere->SetVisibility(false);
    NearAtmosphere->RegisterComponent();

    // The cloud layer: one project material instance, cheap tracing settings from the 14b spike
    CloudMaterial = UMaterialInstanceDynamic::Create(CloudMaterialAsset, this);
    const FSOLGasGiantAppearance& look = GAS_GIANT_APPEARANCES[mAppearanceIndex];
    using namespace SOL::GasGiantMaterialParams;
    CloudMaterial->SetVectorParameterValue(FName(ZONE_COLOR), look.Zone);
    CloudMaterial->SetVectorParameterValue(FName(BELT_COLOR), look.Belt);
    CloudMaterial->SetVectorParameterValue(FName(SHAPE_PARAMS),
        FLinearColor(look.BandsPerSinLat, look.Turbulence, look.NoiseScale, look.PolarStart));
    CloudMaterial->SetVectorParameterValue(FName(CLOUD_LAYER), FLinearColor(SOL::GAS_GIANT_CLOUD_LAYER_BOTTOM_KM,
        SOL::GAS_GIANT_CLOUD_LAYER_HEIGHT_KM, 0.0f, 0.0f));
    CloudMaterial->SetVectorParameterValue(FName(CLOUD_NOISE), FLinearColor(SOL::GAS_GIANT_CLOUD_FEATURE_KM,
        SOL::GAS_GIANT_CLOUD_BELT_COVERAGE, SOL::GAS_GIANT_CLOUD_ZONE_COVERAGE, SOL::GAS_GIANT_CLOUD_EXTINCTION_PER_M));
    CloudMaterial->SetScalarParameterValue(FName(CLOUD_AMBIENT_LUMINANCE), SOL::GAS_GIANT_CLOUD_AMBIENT_LUMINANCE);
    CloudMaterial->SetVectorParameterValue(FName(RING_SHADOW_PARAMS), mRingShadowParams);
    CloudMaterial->InitializeVectorParameterAndGetIndex(FName(RING_SHADOW_SUN), FLinearColor::Transparent,
        mCloudRingSunParamIndex);
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(BAND_PHASES)); ++index)
    {
        CloudMaterial->InitializeVectorParameterAndGetIndex(FName(BAND_PHASES[index]), FLinearColor::Black,
            mCloudBandParamIndices[index]);
    }
    CloudMaterial->InitializeVectorParameterAndGetIndex(FName(PLANET_CENTER), FLinearColor::Black,
        mCloudCenterParamIndex);
    const TCHAR* const axisNames[] = { BODY_AXIS_X, BODY_AXIS_Y, BODY_AXIS_Z };
    for (int32 index = 0; index < 3; ++index)
    {
        CloudMaterial->InitializeVectorParameterAndGetIndex(FName(axisNames[index]), FLinearColor::Black,
            mCloudAxisParamIndices[index]);
    }
    CloudMaterial->InitializeScalarParameterAndGetIndex(FName(NEAR_BLEND), 0.0f, mCloudBlendParamIndex);

    NearClouds = NewObject<UVolumetricCloudComponent>(this, NAME_None);
    NearClouds->SetupAttachment(SceneRoot);
    NearClouds->PlanetRadius = radiusKm;
    NearClouds->LayerBottomAltitude = SOL::GAS_GIANT_CLOUD_LAYER_BOTTOM_KM;
    NearClouds->LayerHeight = SOL::GAS_GIANT_CLOUD_LAYER_HEIGHT_KM;
    NearClouds->TracingStartMaxDistance = radiusKm * static_cast<float>(SOL::GAS_GIANT_NEAR_FADE_OUT_RADII);
    NearClouds->TracingMaxDistance = SOL::GAS_GIANT_CLOUD_TRACING_MAX_KM;
    NearClouds->ViewSampleCountScale = SOL::GAS_GIANT_CLOUD_VIEW_SAMPLE_SCALE;
    NearClouds->ShadowViewSampleCountScale = SOL::GAS_GIANT_CLOUD_SHADOW_SAMPLE_SCALE;
    NearClouds->ShadowTracingDistance = SOL::GAS_GIANT_CLOUD_SHADOW_TRACING_KM;
    NearClouds->bUsePerSampleAtmosphericLightTransmittance = true;
    NearClouds->bVisibleInRealTimeSkyCaptures = false;
    NearClouds->Material = CloudMaterial.Get();
    NearClouds->SetVisibility(false);
    NearClouds->RegisterComponent();
    mIsNearLayerShown = false;

    // The next animation update fills the cloud band phases even if no shell was rendered recently
    mHasAnimated = false;
}

//////////////////////////////////////////////////////////////////////////
// Destroys the near layer's components and material, freeing their render resources
void ASOLGasGiantVisuals::ReleaseNearLayer()
{
    if (mIsNearLayerShown)
    {
        SetNearLayerShown(false);
    }
    if (NearClouds != nullptr)
    {
        NearClouds->DestroyComponent();
        NearClouds = nullptr;
    }
    if (NearAtmosphere != nullptr)
    {
        NearAtmosphere->DestroyComponent();
        NearAtmosphere = nullptr;
    }
    CloudMaterial = nullptr;
    mNearLayer = SOLGasGiant::FNearLayerState();
}

//////////////////////////////////////////////////////////////////////////
// Shows or hides the near layer's components and flags the sun light as the atmosphere light accordingly
void ASOLGasGiantVisuals::SetNearLayerShown(const bool bShown)
{
    mIsNearLayerShown = bShown;
    if (NearAtmosphere != nullptr)
    {
        NearAtmosphere->SetVisibility(bShown);
    }
    if (NearClouds != nullptr)
    {
        NearClouds->SetVisibility(bShown);
    }
    if (IsValid(BodyVisuals))
    {
        BodyVisuals->SetSunDrivesAtmosphere(this, bShown);
    }
}
