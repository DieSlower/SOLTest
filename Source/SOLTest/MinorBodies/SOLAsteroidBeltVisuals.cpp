/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLAsteroidBeltVisuals.h"

#include "MinorBodies/SOLMinorBodySubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
    // Placeholder rock colors (linear) per rendering variant: real named asteroids a warmer gray than the family fill
    const FLinearColor BELT_VARIANT_COLORS[SOLMinorBodyVariant::COUNT] =
    {
        FLinearColor(0.32f, 0.29f, 0.26f),
        FLinearColor(0.22f, 0.22f, 0.23f),
    };

    // Component names per rendering variant
    const TCHAR* const BELT_VARIANT_NAMES[SOLMinorBodyVariant::COUNT] =
    {
        TEXT("RealAsteroids"),
        TEXT("FamilyFill"),
    };

    //////////////////////////////////////////////////////////////////////////
    // Makes a moving primitive that has no collision, navigation, shadows or ray-tracing cost
    void MakeVisualOnlyPrimitive(UPrimitiveComponent* component)
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
}

//////////////////////////////////////////////////////////////////////////
// Creates the root and one instanced mesh component per rendering variant
ASOLAsteroidBeltVisuals::ASOLAsteroidBeltVisuals()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // Pinned at the Unreal origin whatever the actor's transform, so local instance space is world space
    VariantMeshes.Reserve(SOLMinorBodyVariant::COUNT);
    for (int32 variant = 0; variant < SOLMinorBodyVariant::COUNT; ++variant)
    {
        UInstancedStaticMeshComponent* mesh =
            CreateDefaultSubobject<UInstancedStaticMeshComponent>(BELT_VARIANT_NAMES[variant]);
        mesh->SetupAttachment(SceneRoot);
        MakeVisualOnlyPrimitive(mesh);
        mesh->SetUsingAbsoluteLocation(true);
        mesh->SetUsingAbsoluteRotation(true);
        mesh->SetUsingAbsoluteScale(true);
        VariantMeshes.Add(mesh);
    }

    // Forwards each chunk to ProcessChunk
    mChunkFunction = [this](FMassExecutionContext& context)
    {
        ProcessChunk(context);
    };
}

//////////////////////////////////////////////////////////////////////////
// Sizes the instances to the spawned entities, places them once and subscribes to universe updates
void ASOLAsteroidBeltVisuals::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    const USOLMinorBodySubsystem* minorBodies = world->GetSubsystem<USOLMinorBodySubsystem>();
    UMassEntitySubsystem* massEntities = world->GetSubsystem<UMassEntitySubsystem>();
    if (AnchorSubsystem == nullptr || minorBodies == nullptr || massEntities == nullptr)
    {
        UE_LOG(LogSOL, Error,
            TEXT("AsteroidBeltVisuals %s: Mass or universe subsystems missing; nothing will be drawn"), *GetName());
        return;
    }

    UStaticMesh* mesh = LoadObject<UStaticMesh>(nullptr, SOL::Paths::BODY_MESH);
    UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::BODY_MATERIAL);
    if (mesh == nullptr || material == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("AsteroidBeltVisuals %s: failed to load %s or %s"), *GetName(),
            SOL::Paths::BODY_MESH, SOL::Paths::BODY_MATERIAL);
        return;
    }

    // Read-only query over every minor body; Mass matches it to the subsystem's archetype by composition
    mEntityManager = massEntities->GetMutableEntityManager().AsShared();
    mQuery.Initialize(mEntityManager.ToSharedRef());
    mQuery.AddRequirement<FSOLMinorBodyRenderFragment>(EMassFragmentAccess::ReadOnly);
    mQuery.AddRequirement<FSOLMinorBodyStateFragment>(EMassFragmentAccess::ReadOnly);
    mQuery.AddConstSharedRequirement<FSOLMinorBodyAppearanceFragment>();
    mQuery.SetParallelCommandBufferEnabled(false);

    // Scratch transforms sized once to the spawned counts; every frame overwrites them in place
    for (int32 variant = 0; variant < SOLMinorBodyVariant::COUNT; ++variant)
    {
        mScratchTransforms[variant].Init(FTransform::Identity, minorBodies->GetVariantCount(variant));
    }
    FillTransforms();

    // One placeholder material instance and one bulk instance add per variant (the per-frame update only moves them)
    int32 totalInstances = 0;
    for (int32 variant = 0; variant < SOLMinorBodyVariant::COUNT; ++variant)
    {
        UInstancedStaticMeshComponent* variantMesh = VariantMeshes[variant];
        UMaterialInstanceDynamic* variantMaterial = UMaterialInstanceDynamic::Create(material, this);
        variantMaterial->SetVectorParameterValue(FName(SOL::BodyMaterialParams::COLOR_A), BELT_VARIANT_COLORS[variant]);
        variantMaterial->SetVectorParameterValue(FName(SOL::BodyMaterialParams::COLOR_B), BELT_VARIANT_COLORS[variant]);
        variantMaterial->SetScalarParameterValue(FName(SOL::BodyMaterialParams::SUN_ILLUMINANCE),
            SOL::SUN_ILLUMINANCE_LUX);
        variantMesh->SetStaticMesh(mesh);
        variantMesh->SetMaterial(0, variantMaterial);
        variantMesh->SetWorldTransform(FTransform::Identity);
        if (!mScratchTransforms[variant].IsEmpty())
        {
            variantMesh->AddInstances(mScratchTransforms[variant], false, false, false);
        }
        totalInstances += mScratchTransforms[variant].Num();
    }

    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
        &ASOLAsteroidBeltVisuals::HandleUniverseUpdated);
    UE_LOG(LogSOL, Log, TEXT("AsteroidBeltVisuals %s: %d asteroid instances in %d variants"), *GetName(),
        totalInstances, SOLMinorBodyVariant::COUNT);
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from universe updates
void ASOLAsteroidBeltVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mUniverseUpdatedHandle.Reset();
    mEntityManager.Reset();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Snapshots the render origin and viewpoint once, then writes every minor body's render transform for the current
// frame into its variant's scratch array
void ASOLAsteroidBeltVisuals::FillTransforms()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLAsteroidBeltVisuals::FillTransforms);

    // One UObject read per frame (not per entity, per STYLE_GUIDE.md §15): the chunk loop below calls
    // FSOLRenderOrigin::BodyPlacement directly against this snapshot instead of going through AnchorSubsystem
    mFrameRenderOrigin = AnchorSubsystem->GetRenderOrigin();
    mFrameViewpointM = AnchorSubsystem->GetViewpointM();
    FMassExecutionContext context = mEntityManager->CreateExecutionContext(0.0f);
    mQuery.ParallelForEachEntityChunk(context, mChunkFunction);
}

//////////////////////////////////////////////////////////////////////////
// Writes the render transforms of one chunk's bodies into their variant's scratch array
void ASOLAsteroidBeltVisuals::ProcessChunk(FMassExecutionContext& context)
{
    const TConstArrayView<FSOLMinorBodyRenderFragment> renders = context.GetFragmentView<FSOLMinorBodyRenderFragment>();
    const TConstArrayView<FSOLMinorBodyStateFragment> states = context.GetFragmentView<FSOLMinorBodyStateFragment>();
    const int32 variant = context.GetConstSharedFragment<FSOLMinorBodyAppearanceFragment>().MeshVariantIndex;
    if (variant < 0 || variant >= SOLMinorBodyVariant::COUNT)
    {
        return;
    }

    // Each entity owns one slot (its InstanceIndex), so parallel chunks never write the same element. The placement
    // math reads only this frame's snapshot (mFrameRenderOrigin/mFrameViewpointM), never AnchorSubsystem itself
    FTransform* const transforms = mScratchTransforms[variant].GetData();
    const int32 instanceCount = mScratchTransforms[variant].Num();
    const int32 entityCount = context.GetNumEntities();
    for (int32 entity = 0; entity < entityCount; ++entity)
    {
        const int32 instance = renders[entity].InstanceIndex;
        if (instance < 0 || instance >= instanceCount)
        {
            continue;
        }
        const FSOLRenderPlacement placement = mFrameRenderOrigin.BodyPlacement(states[entity].PositionM,
            mFrameViewpointM, renders[entity].RadiusM, SOL::DEFAULT_MAX_RENDER_DISTANCE_CM);
        transforms[instance] = FTransform(FQuat::Identity, FVector(placement.LocationCm),
            FVector(placement.RadiusCm / SOL::BODY_MESH_RADIUS_CM));
    }
}

//////////////////////////////////////////////////////////////////////////
// Re-places every asteroid instance for the current frame, one bulk update per variant
void ASOLAsteroidBeltVisuals::HandleUniverseUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLAsteroidBeltVisuals::HandleUniverseUpdated);
    if (!mEntityManager.IsValid())
    {
        return;
    }
    FillTransforms();

    // Local space equals world space (components pinned at the origin), so no per-instance inverse transform
    for (int32 variant = 0; variant < SOLMinorBodyVariant::COUNT; ++variant)
    {
        if (!mScratchTransforms[variant].IsEmpty())
        {
            // bMarkRenderStateDirty=false: the instance-data manager already pushes these transforms incrementally;
            // true here would rebuild both ISM scene proxies from scratch (all ~9,268 instances) every single frame
            VariantMeshes[variant]->BatchUpdateInstancesTransforms(0, mScratchTransforms[variant], false, false, true);
        }
    }
}
