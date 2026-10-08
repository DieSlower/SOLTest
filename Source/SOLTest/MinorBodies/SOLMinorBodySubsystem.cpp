/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLMinorBodySubsystem.h"

#include "Game/SOLGameMode.h"
#include "MinorBodies/SOLAsteroidBelt.h"
#include "MinorBodies/SOLMinorBodyOrbitProcessor.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/World.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"

//////////////////////////////////////////////////////////////////////////
// Declares its dependencies, prepares the orbit processor and hooks the per-frame body update
void USOLMinorBodySubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    UMassEntitySubsystem* massEntities = collection.InitializeDependency<UMassEntitySubsystem>();
    SimClock = collection.InitializeDependency<USOLSimClockSubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Anchor = collection.InitializeDependency<USOLAnchorSubsystem>();
    Super::Initialize(collection);

    if (massEntities == nullptr || SimClock == nullptr || BodyRegistry == nullptr || Anchor == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("MinorBodySubsystem %s: Mass or universe subsystems missing"), *GetName());
        return;
    }
    mEntityManager = massEntities->GetMutableEntityManager().AsShared();
    mCommandBuffer = MakeShared<FMassCommandBuffer>();

    OrbitProcessor = NewObject<USOLMinorBodyOrbitProcessor>(this);
    OrbitProcessor->CallInitialize(this, mEntityManager.ToSharedRef());

    mBodiesUpdatedHandle = Anchor->OnBodiesUpdated().AddUObject(this, &USOLMinorBodySubsystem::UpdateOrbits);
}

//////////////////////////////////////////////////////////////////////////
// Destroys the minor-body entities while the entity manager is still alive (before any subsystem deinitializes)
void USOLMinorBodySubsystem::PreDeinitialize()
{
    // Deinitialize is too late: UMassEntitySubsystem may already have torn the entity manager's storage down by then
    // (subsystem deinitialization order is not dependency-ordered), and touching it there asserts
    if (mEntityManager.IsValid() && !mEntities.IsEmpty())
    {
        mEntityManager->BatchDestroyEntities(mEntities);
    }
    mEntities.Empty();
    for (int32& count : mVariantCounts)
    {
        count = 0;
    }
    Super::PreDeinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Unhooks from the body update and releases the Mass objects
void USOLMinorBodySubsystem::Deinitialize()
{
    if (Anchor != nullptr)
    {
        Anchor->OnBodiesUpdated().Remove(mBodiesUpdatedHandle);
    }
    mBodiesUpdatedHandle.Reset();
    mEntities.Empty();
    mCommandBuffer.Reset();
    mEntityManager.Reset();
    OrbitProcessor = nullptr;
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Spawns the asteroid belt and places it at the current time
void USOLMinorBodySubsystem::OnWorldBeginPlay(UWorld& world)
{
    Super::OnWorldBeginPlay(world);
    SpawnAsteroidBelt();

    // Place the belt now so the visuals' first draw (at their begin-play, before the next frame) has real positions
    UpdateOrbits(0.0f);
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLMinorBodySubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLMinorBodySubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Returns how many entities were spawned for a rendering variant (SOLMinorBodyVariant), 0 for an unknown one
int32 USOLMinorBodySubsystem::GetVariantCount(const int32 variantIndex) const
{
    return variantIndex >= 0 && variantIndex < SOLMinorBodyVariant::COUNT ? mVariantCounts[variantIndex] : 0;
}

//////////////////////////////////////////////////////////////////////////
// Generates the belt and batch-creates its entities, one batch per rendering variant
void USOLMinorBodySubsystem::SpawnAsteroidBelt()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLMinorBodySubsystem::SpawnAsteroidBelt);
    if (!mEntityManager.IsValid() || !mEntities.IsEmpty())
    {
        return;
    }
    const int32 sunIndex = BodyRegistry->GetRegistry().FindByName(FName(SOL::BodyNames::SUN));
    if (sunIndex == INDEX_NONE)
    {
        UE_LOG(LogSOL, Error, TEXT("MinorBodySubsystem %s: no Sun in the registry; belt not spawned"), *GetName());
        return;
    }

    // Partition the belt by rendering variant: named real asteroids, then the procedural family fill
    const TArray<FSOLAsteroidDef> belt = SOLAsteroidBelt::GenerateBelt(SOL::ASTEROID_BELT_SEED);
    TArray<const FSOLAsteroidDef*> variantDefs[SOLMinorBodyVariant::COUNT];
    for (TArray<const FSOLAsteroidDef*>& defs : variantDefs)
    {
        defs.Reserve(belt.Num());
    }
    for (const FSOLAsteroidDef& def : belt)
    {
        const int32 variant = def.Name != NAME_None ? SOLMinorBodyVariant::REAL_ASTEROID
            : SOLMinorBodyVariant::FAMILY_FILL;
        variantDefs[variant].Add(&def);
    }

    // One archetype; each variant is one batch whose shared appearance value selects its ISM component.
    // FSOLBeltRockTag lets ASOLAsteroidBeltVisuals's query match only belt entities at the archetype level (SDD 6
    // Amendment 11's follow-up, 5e-iii)
    const UScriptStruct* const elements[] = {
        FSOLMinorBodyOrbitFragment::StaticStruct(),
        FSOLMinorBodyRenderFragment::StaticStruct(),
        FSOLMinorBodyStateFragment::StaticStruct(),
        FSOLBeltRockTag::StaticStruct(),
    };
    const FMassArchetypeHandle archetype = mEntityManager->CreateArchetype(MakeArrayView(elements));
    mEntities.Reserve(belt.Num());
    for (int32 variant = 0; variant < SOLMinorBodyVariant::COUNT; ++variant)
    {
        const TArray<const FSOLAsteroidDef*>& defs = variantDefs[variant];
        mVariantCounts[variant] = defs.Num();
        if (defs.IsEmpty())
        {
            continue;
        }
        FMassArchetypeSharedFragmentValues sharedValues;
        sharedValues.Add(
            mEntityManager->GetOrCreateConstSharedFragment(FSOLMinorBodyAppearanceFragment::Make(variant)));
        sharedValues.Sort();

        // BatchCreateEntities appends, so this batch's handles start at firstEntity; startup only, not per frame
        const int32 firstEntity = mEntities.Num();
        {
            const TSharedRef<FMassEntityManager::FEntityCreationContext> creation =
                mEntityManager->BatchCreateEntities(archetype, sharedValues, defs.Num(), mEntities);
            for (int32 instance = 0; instance < defs.Num(); ++instance)
            {
                const FSOLAsteroidDef& def = *defs[instance];
                const FMassEntityHandle entity = mEntities[firstEntity + instance];
                FSOLMinorBodyOrbitFragment& orbit =
                    mEntityManager->GetFragmentDataChecked<FSOLMinorBodyOrbitFragment>(entity);
                orbit.Elements = def.Elements;
                orbit.ParentBodyIndex = sunIndex;
                FSOLMinorBodyRenderFragment& render =
                    mEntityManager->GetFragmentDataChecked<FSOLMinorBodyRenderFragment>(entity);
                render.RadiusM = def.RadiusM;
                render.InstanceIndex = instance;
            }
        }
    }
    UE_LOG(LogSOL, Log, TEXT("MinorBodySubsystem %s: asteroid belt spawned, %d real + %d family-fill entities"),
        *GetName(), mVariantCounts[SOLMinorBodyVariant::REAL_ASTEROID],
        mVariantCounts[SOLMinorBodyVariant::FAMILY_FILL]);
}

//////////////////////////////////////////////////////////////////////////
// Places every minor body at the clock's current time; bound to the anchor subsystem's OnBodiesUpdated
void USOLMinorBodySubsystem::UpdateOrbits(const float realDeltaSeconds)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLMinorBodySubsystem::UpdateOrbits);
    if (mEntities.IsEmpty() || OrbitProcessor == nullptr)
    {
        return;
    }

    // Raw ecliptic registry positions (the render frame), not the Unreal-handed ship-physics frame cache
    OrbitProcessor->SetParentPositionsM(BodyRegistry->GetRegistry().GetPositionsM());
    OrbitProcessor->SetSecondsSinceJ2000(SimClock->GetClock().GetSecondsSinceJ2000());
    UE::Mass::FProcessingContext context(*mEntityManager, realDeltaSeconds);
    context.SetCommandBuffer(mCommandBuffer);
    UE::Mass::Executor::Run(*OrbitProcessor, context);
}
