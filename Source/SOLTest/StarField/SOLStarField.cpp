/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "StarField/SOLStarField.h"

#include "SOLConstants.h"
#include "SOLTest.h"
#include "StarField/SOLStarFieldData.h"
#include "Universe/SOLRenderPlacement.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
    constexpr double STAR_QUAD_SIZE_CM = 100.0;      // Edge of the STAR_QUAD_MESH quad in its own units (YZ plane)
    constexpr double STAR_SKY_MESH_RADIUS_CM = 50.0; // Radius of the STAR_SKY_MESH sphere in its own units

    //////////////////////////////////////////////////////////////////////////
    // Turns off everything a background primitive does not need: collision, navigation, shadows and GI contributions
    void MakeBackgroundPrimitive(UPrimitiveComponent* component)
    {
        component->SetMobility(EComponentMobility::Movable);
        component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
        component->SetCanEverAffectNavigation(false);
        component->SetGenerateOverlapEvents(false);
        component->SetCastShadow(false);
        component->bAffectDistanceFieldLighting = false;
        component->bAffectDynamicIndirectLighting = false;
        component->bVisibleInRayTracing = false;
        component->bReceivesDecals = false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the exact solid angle (sr) of a square that subtends angularDiameterRad edge to edge, seen face-on
    double SquareSolidAngleSr(const double angularDiameterRad)
    {
        const double sinHalf = FMath::Sin(0.5 * angularDiameterRad);
        return 4.0 * FMath::Asin(sinHalf * sinHalf);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the root, the bright-star instance component and the sky sphere
ASOLStarField::ASOLStarField()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // The quad mesh's own body is BlockAll (SDD 5): instances must create no physics bodies or navigation data
    BrightStars = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BrightStars"));
    BrightStars->SetupAttachment(SceneRoot);
    MakeBackgroundPrimitive(BrightStars);
    BrightStars->SetNumCustomDataFloats(SOL::StarFieldMaterialParams::SPRITE_CUSTOM_DATA_FLOATS);

    // Below every other translucency: its bounds are centered on the camera, so by default it would sort nearest
    BrightStars->TranslucencySortPriority = SOL::STAR_SPRITE_TRANSLUCENCY_SORT_PRIORITY;

    Sky = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sky"));
    Sky->SetupAttachment(SceneRoot);
    MakeBackgroundPrimitive(Sky);

    // Both are pinned at the Unreal origin, whatever the actor's transform, and never move: moving the 45,653-instance
    // ISM re-derives every instance (~4 ms of game thread per frame, stat dumpave), and the sky samples by view
    // direction so it gains nothing from moving. The render origin keeps the camera within RENDER_REBASE_DISTANCE_M
    // (10 km) plus the chase arm of the origin, an angular error of ~1e6 / STAR_FIELD_SPRITE_RADIUS_CM = 5e-7 rad
    BrightStars->SetUsingAbsoluteLocation(true);
    BrightStars->SetUsingAbsoluteRotation(true);
    Sky->SetUsingAbsoluteLocation(true);
    Sky->SetUsingAbsoluteRotation(true);
}

//////////////////////////////////////////////////////////////////////////
// Fills the bright-star instances and sets up both materials, once; nothing changes afterwards
void ASOLStarField::BeginPlay()
{
    Super::BeginPlay();

    // Either half can fail on its own (each logs why); the other still draws
    SetUpSky();
    SetUpBrightStars();
}

//////////////////////////////////////////////////////////////////////////
// Sets the sky sphere's mesh, material and radius; returns false if an asset is missing
bool ASOLStarField::SetUpSky()
{
    UStaticMesh* mesh = LoadObject<UStaticMesh>(nullptr, SOL::Paths::STAR_SKY_MESH);
    UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::STAR_SKY_MATERIAL);
    if (mesh == nullptr || material == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("StarField %s: failed to load %s or %s"), *GetName(), SOL::Paths::STAR_SKY_MESH,
            SOL::Paths::STAR_SKY_MATERIAL);
        return false;
    }

    UMaterialInstanceDynamic* skyMaterial = UMaterialInstanceDynamic::Create(material, this);
    skyMaterial->SetScalarParameterValue(FName(SOL::StarFieldMaterialParams::BRIGHTNESS_SCALE),
        SOL::STAR_FIELD_BRIGHTNESS_SCALE);
    Sky->SetStaticMesh(mesh);
    Sky->SetMaterial(0, skyMaterial);
    Sky->SetRelativeScale3D(FVector(SOL::STAR_FIELD_SKY_RADIUS_CM / STAR_SKY_MESH_RADIUS_CM));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Adds one camera-facing quad per bright star with its flux-scaled color as custom data; returns false on failure
bool ASOLStarField::SetUpBrightStars()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLStarField::SetUpBrightStars);

    const USOLStarFieldData* data = LoadObject<USOLStarFieldData>(nullptr, SOL::Paths::STAR_FIELD_DATA);
    UStaticMesh* mesh = LoadObject<UStaticMesh>(nullptr, SOL::Paths::STAR_QUAD_MESH);
    UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::STAR_SPRITE_MATERIAL);
    if (data == nullptr || mesh == nullptr || material == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("StarField %s: failed to load %s, %s or %s"), *GetName(),
            SOL::Paths::STAR_FIELD_DATA, SOL::Paths::STAR_QUAD_MESH, SOL::Paths::STAR_SPRITE_MATERIAL);
        return false;
    }

    // Brightness contract (SOLStarFieldData.h): flux F over the sprite's solid angle emits F * texel sr / sprite sr
    using namespace SOL::StarFieldMaterialParams;
    const double spriteSolidAngleSr = SquareSolidAngleSr(SOL::STAR_SPRITE_ANGULAR_DIAMETER_RAD);
    UMaterialInstanceDynamic* spriteMaterial = UMaterialInstanceDynamic::Create(material, this);
    spriteMaterial->SetScalarParameterValue(FName(CUBE_TEXEL_SOLID_ANGLE), data->CubeTexelSolidAngleSr);
    spriteMaterial->SetScalarParameterValue(FName(SPRITE_SOLID_ANGLE), static_cast<float>(spriteSolidAngleSr));
    spriteMaterial->SetScalarParameterValue(FName(BRIGHTNESS_SCALE), SOL::STAR_FIELD_BRIGHTNESS_SCALE);
    BrightStars->SetStaticMesh(mesh);
    BrightStars->SetMaterial(0, spriteMaterial);

    // Every quad sits at the same radius, scaled to the fixed angular size and turned to face the center, where the
    // camera is (its roll is irrelevant for a round sprite). The camera stays within ~1e6 cm of the center, so each
    // quad stays face-on to it within 5e-7 rad
    const TArray<FSOLBrightStar>& stars = data->BrightStars;
    const int32 starCount = stars.Num();
    if (starCount == 0)
    {
        UE_LOG(LogSOL, Warning, TEXT("StarField %s: %s has no bright stars"), *GetName(), SOL::Paths::STAR_FIELD_DATA);
        return false;
    }
    const double radiusCm = SOL::STAR_FIELD_SPRITE_RADIUS_CM;
    const double quadScale = 2.0 * radiusCm * FMath::Tan(0.5 * SOL::STAR_SPRITE_ANGULAR_DIAMETER_RAD)
        / STAR_QUAD_SIZE_CM;
    const FVector instanceScale(1.0, quadScale, quadScale);

    TArray<FTransform> transforms;
    transforms.Reserve(starCount);
    TArray<float> customData;
    customData.Reserve(starCount * SPRITE_CUSTOM_DATA_FLOATS);
    for (const FSOLBrightStar& star : stars)
    {
        const FVector direction = SOLRender::EclipticToUnreal(FVector3d(star.Direction)).GetSafeNormal();
        transforms.Emplace(FRotationMatrix::MakeFromX(-direction).ToQuat(), direction * radiusCm, instanceScale);
        customData.Add(star.Flux * star.Color.R);
        customData.Add(star.Flux * star.Color.G);
        customData.Add(star.Flux * star.Color.B);
    }

    // One bulk add and one bulk custom-data write, then a single render-state update
    BrightStars->AddInstances(transforms, false, false, false);
    BrightStars->SetCustomData(0, starCount - 1, customData, true);
    UE_LOG(LogSOL, Log, TEXT("StarField %s: %d bright-star sprites, %.4f rad across (%.3g sr)"), *GetName(), starCount,
        SOL::STAR_SPRITE_ANGULAR_DIAMETER_RAD, spriteSolidAngleSr);
    return true;
}
