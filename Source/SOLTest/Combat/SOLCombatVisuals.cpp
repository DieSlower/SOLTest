/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatVisuals.h"

#include "Combat/SOLCombatRules.h"
#include "Combat/SOLCombatSubsystem.h"
#include "MinorBodies/SOLMinorBodyVisualsUtil.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLRenderOrigin.h"
#include "Universe/SOLRenderPlacement.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
    // Fewest target instances added at once when the highest used slot grows past the instance count
    constexpr int32 COMBAT_TARGET_INSTANCE_MIN_GROWTH = 16;

    //////////////////////////////////////////////////////////////////////////
    // Loads an optional combat Niagara system, warning (not failing) when it is missing
    UNiagaraSystem* LoadOptionalCombatSystem(const TCHAR* path, const AActor* owner)
    {
        UNiagaraSystem* system = LoadObject<UNiagaraSystem>(nullptr, path);
        if (system == nullptr)
        {
            UE_LOG(LogSOL, Warning, TEXT("CombatVisuals %s: %s is missing; that effect is disabled"), *owner->GetName(),
                path);
        }
        return system;
    }

    //////////////////////////////////////////////////////////////////////////
    // Makes a component keep the world transform it is given rather than following its parent
    void MakeCombatComponentAbsolute(USceneComponent* component)
    {
        component->SetUsingAbsoluteLocation(true);
        component->SetUsingAbsoluteRotation(true);
        component->SetUsingAbsoluteScale(true);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the root, the bolt Niagara component and the target instanced mesh
ASOLCombatVisuals::ASOLCombatVisuals()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // Pinned at the Unreal origin with an identity transform, so the system's local space is render space; activated
    // explicitly once its parameters are set (BeginPlay), so the one-time particle burst reads them
    BoltNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Bolts"));
    BoltNiagara->SetupAttachment(SceneRoot);
    MakeCombatComponentAbsolute(BoltNiagara);
    BoltNiagara->bAutoActivate = false;

    // Pinned at the origin too, so instance transforms are world transforms (same as the belt and ring visuals)
    TargetMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Targets"));
    TargetMesh->SetupAttachment(SceneRoot);
    SOLMinorBodyVisualsUtil::MakeVisualOnlyPrimitive(TargetMesh);
    MakeCombatComponentAbsolute(TargetMesh);
}

//////////////////////////////////////////////////////////////////////////
// Loads the assets, sizes the pools and scratch arrays once, and subscribes to the combat update
void ASOLCombatVisuals::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    Combat = world->GetSubsystem<USOLCombatSubsystem>();
    Anchor = world->GetSubsystem<USOLAnchorSubsystem>();
    Ships = world->GetSubsystem<USOLShipSubsystem>();
    if (Combat == nullptr || Anchor == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("CombatVisuals %s: combat or anchor subsystem missing; nothing will be drawn"),
            *GetName());
        return;
    }

    // Bolts: the particle pool is spawned once at activation, so it is sized to the render cap; the first combat update
    // with no bolt pauses and hides it until one is fired
    if (UNiagaraSystem* boltSystem = LoadOptionalCombatSystem(SOL::Paths::COMBAT_BOLT_NIAGARA_SYSTEM, this))
    {
        mBoltPositionsCm.Reserve(SOL::COMBAT_BOLT_RENDER_CAP);
        mBoltVelocitiesCmps.Reserve(SOL::COMBAT_BOLT_RENDER_CAP);
        BoltNiagara->SetAsset(boltSystem);
        BoltNiagara->SetWorldTransform(FTransform::Identity);
        BoltNiagara->SetVariableInt(SOL::CombatBoltParams::MAX_COUNT, SOL::COMBAT_BOLT_RENDER_CAP);
        BoltNiagara->SetVariableInt(SOL::CombatBoltParams::COUNT, 0);
        BoltNiagara->Activate(true);
        mIsBoltRendererEnabled = true;
        mIsBoltRendererRunning = true;
    }

    // Targets: no instances yet (they are added as the highest used slot grows) and hidden until a target is active
    UStaticMesh* targetMesh = LoadObject<UStaticMesh>(nullptr, SOL::Paths::COMBAT_TARGET_MESH);
    UMaterialInterface* targetMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::COMBAT_TARGET_MATERIAL);
    const int32 slotCount = Combat->GetTargetSlotCount();
    if (targetMesh != nullptr && targetMaterial != nullptr && slotCount > 0)
    {
        TargetMesh->SetStaticMesh(targetMesh);
        TargetMesh->SetMaterial(0, targetMaterial);
        TargetMesh->SetWorldTransform(FTransform::Identity);
        TargetMesh->SetNumCustomDataFloats(SOL::COMBAT_TARGET_CUSTOM_DATA_FLOATS);
        TargetMesh->SetVisibility(false);
        mTargetTransforms.Reserve(slotCount);
        mTargetCustomData.Init(-1.0f, slotCount * SOL::COMBAT_TARGET_CUSTOM_DATA_FLOATS);
        mIsTargetRendererEnabled = true;
    }
    else
    {
        UE_LOG(LogSOL, Warning, TEXT("CombatVisuals %s: %s or %s is missing; targets are not drawn"), *GetName(),
            SOL::Paths::COMBAT_TARGET_MESH, SOL::Paths::COMBAT_TARGET_MATERIAL);
    }

    // One-shot effect pools
    EffectComponents.Reserve(SOL::COMBAT_FX_IMPACT_POOL_SIZE + SOL::COMBAT_FX_SHIELD_BREAK_POOL_SIZE
        + SOL::COMBAT_FX_SPARK_POOL_SIZE + SOL::COMBAT_FX_EXPLOSION_POOL_SIZE);
    CreatePool(mImpactPool, LoadOptionalCombatSystem(SOL::Paths::COMBAT_IMPACT_NIAGARA_SYSTEM, this),
        SOL::COMBAT_FX_IMPACT_POOL_SIZE, SOL::COMBAT_FX_IMPACT_DURATION_S, TEXT("Impact"));
    CreatePool(mShieldBreakPool, LoadOptionalCombatSystem(SOL::Paths::COMBAT_SHIELD_BREAK_NIAGARA_SYSTEM, this),
        SOL::COMBAT_FX_SHIELD_BREAK_POOL_SIZE, SOL::COMBAT_FX_SHIELD_BREAK_DURATION_S, TEXT("ShieldBreak"));
    CreatePool(mSparkPool, LoadOptionalCombatSystem(SOL::Paths::COMBAT_SPARK_NIAGARA_SYSTEM, this),
        SOL::COMBAT_FX_SPARK_POOL_SIZE, SOL::COMBAT_FX_SPARK_DURATION_S, TEXT("Spark"));
    CreatePool(mExplosionPool, LoadOptionalCombatSystem(SOL::Paths::COMBAT_EXPLOSION_NIAGARA_SYSTEM, this),
        SOL::COMBAT_FX_EXPLOSION_POOL_SIZE, SOL::COMBAT_FX_EXPLOSION_DURATION_S, TEXT("Explosion"));

    mCombatUpdatedHandle = Combat->OnCombatUpdated().AddUObject(this, &ASOLCombatVisuals::HandleCombatUpdated);
    UE_LOG(LogSOL, Log, TEXT("CombatVisuals %s: bolts %s, %d target slots, %d pooled effects"), *GetName(),
        mIsBoltRendererEnabled ? TEXT("on") : TEXT("off"), mIsTargetRendererEnabled ? slotCount : 0,
        EffectComponents.Num());
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from the combat update
void ASOLCombatVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (Combat != nullptr)
    {
        Combat->OnCombatUpdated().Remove(mCombatUpdatedHandle);
    }
    mCombatUpdatedHandle.Reset();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Creates a pool of components for one effect system (nothing if the system is missing)
void ASOLCombatVisuals::CreatePool(FSOLEffectPool& pool, UNiagaraSystem* system, const int32 size,
    const double durationS, const TCHAR* name)
{
    pool.DurationS = durationS;
    if (system == nullptr)
    {
        return;
    }
    pool.Slots.SetNum(size);
    for (int32 index = 0; index < size; ++index)
    {
        UNiagaraComponent* component = NewObject<UNiagaraComponent>(this,
            FName(*FString::Printf(TEXT("%sFx%d"), name, index)));
        component->SetupAttachment(SceneRoot);
        MakeCombatComponentAbsolute(component);
        component->bAutoActivate = false;
        component->SetAsset(system);
        component->RegisterComponent();
        pool.Slots[index].Component = component;
        EffectComponents.Add(component);
    }
}

//////////////////////////////////////////////////////////////////////////
// Redraws bolts and targets and starts and moves the one-shot effects; bound to OnCombatUpdated
void ASOLCombatVisuals::HandleCombatUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLCombatVisuals::HandleCombatUpdated);
    const FSOLRenderOrigin& origin = Anchor->GetRenderOrigin();
    UpdateBolts(origin);
    UpdateTargets(origin);
    StartEventEffects();
    UpdateEffects(origin, Combat->GetLastUpdateDeltaS());
}

//////////////////////////////////////////////////////////////////////////
// Returns the player ship's universe velocity (ecliptic), or zero without a ship
FVector3d ASOLCombatVisuals::GetObserverVelocityMps() const
{
    // The ship's state is Unreal-handed; EclipticToUnreal (a Y flip) is its own inverse
    return Ships != nullptr && Ships->HasPlayerShip() ? SOLRender::EclipticToUnreal(Ships->GetState().VelocityMps)
        : FVector3d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Copies the bolt arrays into the bolt Niagara system
void ASOLCombatVisuals::UpdateBolts(const FSOLRenderOrigin& origin)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLCombatVisuals::UpdateBolts);
    if (!mIsBoltRendererEnabled)
    {
        return;
    }

    // No bolt: pause and hide the system so its CPU sim does not walk the idle particle pool every frame
    const int32 liveCount = Combat->GetBoltCount();
    const int32 boltCount = SOLCombatRules::DrawnBoltCount(liveCount, SOL::COMBAT_BOLT_RENDER_CAP);
    if (boltCount == 0)
    {
        SetBoltRendererRunning(false);
        return;
    }
    if (liveCount > boltCount && !mHasLoggedBoltRenderCap)
    {
        mHasLoggedBoltRenderCap = true;
        UE_LOG(LogSOL, Warning, TEXT("CombatVisuals %s: %d live bolts exceed the render cap of %d; the newest are not "
            "drawn (they still fly and hit). Logged once"), *GetName(), liveCount, SOL::COMBAT_BOLT_RENDER_CAP);
    }

    // Parameter names built once, not per frame
    static const FName positionsName(SOL::CombatBoltParams::POSITIONS_CM);
    static const FName velocitiesName(SOL::CombatBoltParams::VELOCITIES);
    static const FName countName(SOL::CombatBoltParams::COUNT);

    // Positions in render cm (small: within a few km of the render origin, so float-exact enough); directions are the
    // velocity relative to the ship the camera rides on, which is how a bolt appears to move on screen
    const TConstArrayView<FVector3d> positionsM = Combat->GetBoltPositionsM();
    const TConstArrayView<FVector3d> velocitiesMps = Combat->GetBoltVelocitiesMps();
    const FVector3d observerVelocityMps = GetObserverVelocityMps();
    mBoltPositionsCm.SetNum(boltCount, EAllowShrinking::No);
    mBoltVelocitiesCmps.SetNum(boltCount, EAllowShrinking::No);
    for (int32 bolt = 0; bolt < boltCount; ++bolt)
    {
        mBoltPositionsCm[bolt] = FVector3f(origin.UniverseToRenderCm(positionsM[bolt]));
        mBoltVelocitiesCmps[bolt] = FVector3f(
            SOLRender::EclipticToUnreal(velocitiesMps[bolt] - observerVelocityMps) * SOL::METERS_TO_CM);
    }
    UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(BoltNiagara, positionsName, mBoltPositionsCm);
    UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(BoltNiagara, velocitiesName, mBoltVelocitiesCmps);
    BoltNiagara->SetVariableInt(countName, boltCount);
    SetBoltRendererRunning(true);
}

//////////////////////////////////////////////////////////////////////////
// Unpauses and shows the bolt system (some bolt is live), or pauses and hides it (none is)
void ASOLCombatVisuals::SetBoltRendererRunning(const bool bRunning)
{
    // Paused rather than deactivated: reactivating would respawn the particle burst, and the bolt slot index relies on
    // the burst's particle ids (SDD 7 Amendment 2)
    if (bRunning == mIsBoltRendererRunning)
    {
        return;
    }
    mIsBoltRendererRunning = bRunning;
    BoltNiagara->SetPaused(!bRunning);
    BoltNiagara->SetVisibility(bRunning);
}

//////////////////////////////////////////////////////////////////////////
// Re-places the in-use target slots and writes changed per-instance custom data
void ASOLCombatVisuals::UpdateTargets(const FSOLRenderOrigin& origin)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLCombatVisuals::UpdateTargets);
    if (!mIsTargetRendererEnabled)
    {
        return;
    }

    // Slots fill lowest-first, so the in-use range ends just past the highest active slot; slots beyond both this
    // frame's and last frame's range are already hidden and are not touched
    const TConstArrayView<bool> active = Combat->GetTargetActive();
    const int32 slotCount = FMath::Min(active.Num(), mTargetCustomData.Num() / SOL::COMBAT_TARGET_CUSTOM_DATA_FLOATS);
    int32 activeEnd = 0;
    if (Combat->GetActiveTargetCount() > 0)
    {
        for (int32 slot = slotCount - 1; slot >= 0; --slot)
        {
            if (active[slot])
            {
                activeEnd = slot + 1;
                break;
            }
        }
    }
    const int32 rangeEnd = FMath::Max(activeEnd, mTargetRangeEnd);
    mTargetRangeEnd = activeEnd;

    // Hidden while no target is active (its stale instances were zero-scaled by the pass that saw the last one go)
    const bool bVisible = activeEnd > 0;
    if (bVisible != mIsTargetMeshVisible)
    {
        mIsTargetMeshVisible = bVisible;
        TargetMesh->SetVisibility(bVisible);
    }
    if (rangeEnd == 0)
    {
        return;
    }
    EnsureTargetInstances(rangeEnd);

    const TConstArrayView<FVector3d> positionsM = Combat->GetTargetPositionsM();
    const TConstArrayView<float> shields = Combat->GetTargetShieldFractions();
    const TConstArrayView<float> healths = Combat->GetTargetHealthFractions();
    const TConstArrayView<float> flashes = Combat->GetTargetHitFlash();
    const FVector scale(Combat->GetTargetRadiusM() * SOL::METERS_TO_CM / SOL::BODY_MESH_RADIUS_CM);
    const FTransform hidden(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
    mTargetTransforms.SetNum(rangeEnd, EAllowShrinking::No);
    for (int32 slot = 0; slot < rangeEnd; ++slot)
    {
        if (!active[slot])
        {
            mTargetTransforms[slot] = hidden;
            continue;
        }
        mTargetTransforms[slot] = FTransform(FQuat::Identity, FVector(origin.UniverseToRenderCm(positionsM[slot])), scale);

        // Custom data only where it changed (the hit flash fades over a few frames; shields regenerate slowly)
        float* cached = &mTargetCustomData[slot * SOL::COMBAT_TARGET_CUSTOM_DATA_FLOATS];
        const float values[SOL::COMBAT_TARGET_CUSTOM_DATA_FLOATS] = { shields[slot], healths[slot], flashes[slot] };
        if (cached[0] != values[0] || cached[1] != values[1] || cached[2] != values[2])
        {
            cached[0] = values[0];
            cached[1] = values[1];
            cached[2] = values[2];
            TargetMesh->SetCustomData(slot, MakeArrayView(values), false);
        }
    }

    // bMarkRenderStateDirty=false: the instance-data manager pushes changed instances incrementally (as for the belt)
    TargetMesh->BatchUpdateInstancesTransforms(0, mTargetTransforms, true, false, true);
}

//////////////////////////////////////////////////////////////////////////
// Adds hidden target instances so at least instanceCount exist (grows geometrically, up to the slot count)
void ASOLCombatVisuals::EnsureTargetInstances(const int32 instanceCount)
{
    const int32 current = TargetMesh->GetInstanceCount();
    if (instanceCount <= current)
    {
        return;
    }

    // Only when the high-water mark rises (rare); doubling keeps a session to a handful of these, never per frame
    const int32 slotCount = mTargetCustomData.Num() / SOL::COMBAT_TARGET_CUSTOM_DATA_FLOATS;
    const int32 target = FMath::Min(FMath::Max3(instanceCount, current * 2, COMBAT_TARGET_INSTANCE_MIN_GROWTH),
        slotCount);
    mTargetTransforms.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), target - current);
    TargetMesh->AddInstances(mTargetTransforms, false, false, false);
    mTargetTransforms.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Starts the one-shot effects of this frame's events
void ASOLCombatVisuals::StartEventEffects()
{
    mImpactPool.StartedThisFrame = 0;
    mShieldBreakPool.StartedThisFrame = 0;
    mSparkPool.StartedThisFrame = 0;
    mExplosionPool.StartedThisFrame = 0;

    const TConstArrayView<float> shields = Combat->GetTargetShieldFractions();
    for (const FSOLCombatEvent& event : Combat->GetEvents())
    {
        switch (event.Type)
        {
        case ESOLCombatEventType::TargetHit:
        {
            // Blue while the shield still holds, orange once the hit reached the hull
            const bool bShielded = shields.IsValidIndex(event.TargetSlot) && shields[event.TargetSlot] > 0.0f;
            const FLinearColor& tint = bShielded ? SOL::COMBAT_FX_SHIELD_HIT_TINT : SOL::COMBAT_FX_HULL_HIT_TINT;            StartEffect(mImpactPool, event, event.VelocityMps - event.FrameVelocityMps, &tint);
            break;
        }
        case ESOLCombatEventType::ShieldBroken:
            StartEffect(mShieldBreakPool, event, FVector3d::ZeroVector, nullptr);
            break;
        case ESOLCombatEventType::TargetDestroyed:
            StartEffect(mExplosionPool, event, FVector3d::ZeroVector, nullptr);
            break;
        case ESOLCombatEventType::BoltAbsorbed:
            StartEffect(mSparkPool, event, event.VelocityMps - event.FrameVelocityMps, nullptr);
            break;
        default:
            break;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Starts one effect from a pool at a universe position; the direction (ecliptic) and tint are optional parameters
void ASOLCombatVisuals::StartEffect(FSOLEffectPool& pool, const FSOLCombatEvent& event,
    const FVector3d& directionEcliptic, const FLinearColor* tint)
{
    // A burst of more events than the pool holds in one frame keeps the first ones rather than restarting a slot
    if (pool.Slots.IsEmpty() || pool.StartedThisFrame >= pool.Slots.Num())
    {
        return;
    }
    ++pool.StartedThisFrame;

    FSOLEffectSlot& slot = pool.Slots[pool.NextSlot];
    pool.NextSlot = (pool.NextSlot + 1) % pool.Slots.Num();
    slot.FrameBody = event.FrameBody;
    slot.FrameOffsetM = event.FrameOffsetM;
    slot.RelativeVelocityMps = event.RelativeVelocityMps;
    slot.AgeS = 0.0;
    slot.bIsLive = true;

    // Placed before activation so the burst spawns at the event; the next UpdateEffects keeps it riding along
    static const FName directionName(SOL::CombatEffectParams::DIRECTION);
    static const FName tintName(SOL::CombatEffectParams::TINT);
    UNiagaraComponent* component = slot.Component;
    component->SetWorldLocation(FVector(Anchor->GetRenderOrigin().UniverseToRenderCm(event.PositionM)));
    if (!directionEcliptic.IsNearlyZero())
    {
        component->SetVariableVec3(directionName, FVector(SOLRender::EclipticToUnreal(directionEcliptic.GetSafeNormal())));
    }
    if (tint != nullptr)
    {
        component->SetVariableLinearColor(tintName, *tint);
    }
    component->Activate(true);
}

//////////////////////////////////////////////////////////////////////////
// Moves every live pooled effect with its frame and retires the ones past their duration
void ASOLCombatVisuals::UpdateEffects(const FSOLRenderOrigin& origin, const double deltaSeconds)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLCombatVisuals::UpdateEffects);
    FSOLEffectPool* const pools[] = { &mImpactPool, &mShieldBreakPool, &mSparkPool, &mExplosionPool };
    for (FSOLEffectPool* pool : pools)
    {
        for (FSOLEffectSlot& slot : pool->Slots)
        {
            if (!slot.bIsLive)
            {
                continue;
            }
            slot.AgeS += deltaSeconds;
            if (slot.AgeS > pool->DurationS)
            {
                // The system has finished its single loop by now and completes on its own
                slot.bIsLive = false;
                continue;
            }
            // Effects are local-space, so moving the component carries its particles with what was hit; recomputed from
            // the frame body's current position, so time warp's carry is followed exactly (FSOLCombatEvent)
            const FVector3d positionM = SOLCombatRules::EventPositionNowM(Combat->GetFrameBodyPositionM(slot.FrameBody),
                slot.FrameOffsetM, slot.RelativeVelocityMps, slot.AgeS);
            slot.Component->SetWorldLocation(FVector(origin.UniverseToRenderCm(positionM)));        }
    }
}
