/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcCombatVisuals.h"

#include "Npc/SOLNpcCombat.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLRenderOrigin.h"
#include "Universe/SOLRenderPlacement.h"

#include "Engine/World.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Makes a component keep the world transform it is given rather than following its parent
    void MakeNpcCombatComponentAbsolute(USceneComponent* component)
    {
        component->SetUsingAbsoluteLocation(true);
        component->SetUsingAbsoluteRotation(true);
        component->SetUsingAbsoluteScale(true);
    }

    //////////////////////////////////////////////////////////////////////////
    // Loads a Niagara system, logging (once, here) when it is missing
    UNiagaraSystem* LoadNpcCombatSystem(const TCHAR* path, const AActor* owner)
    {
        UNiagaraSystem* system = LoadObject<UNiagaraSystem>(nullptr, path);
        if (system == nullptr)
        {
            UE_LOG(LogSOL, Warning, TEXT("NpcCombatVisuals %s: %s is missing; that effect is disabled"),
                *owner->GetName(), path);
        }
        return system;
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the root and the bolt Niagara component
ASOLNpcCombatVisuals::ASOLNpcCombatVisuals()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // Pinned at the Unreal origin so the system's local space is render space (as ASOLCombatVisuals' bolts)
    BoltNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("NpcBolts"));
    BoltNiagara->SetupAttachment(SceneRoot);
    MakeNpcCombatComponentAbsolute(BoltNiagara);
    BoltNiagara->bAutoActivate = false;
}

//////////////////////////////////////////////////////////////////////////
// Loads the systems, builds the pools and subscribes to NPC updates
void ASOLNpcCombatVisuals::BeginPlay()
{
    Super::BeginPlay();
    Npcs = GetWorld()->GetSubsystem<USOLNpcSubsystem>();
    Anchor = GetWorld()->GetSubsystem<USOLAnchorSubsystem>();
    if (Npcs == nullptr || Anchor == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("NpcCombatVisuals %s: NPC or anchor subsystem missing; NPC combat not drawn"),
            *GetName());
        return;
    }

    // Bolts: the particle pool is spawned once at activation, sized to the NPC render cap
    if (UNiagaraSystem* boltSystem = LoadNpcCombatSystem(SOL::Paths::COMBAT_BOLT_NIAGARA_SYSTEM, this))
    {
        mBoltPositionsCm.Reserve(SOL::NPC_BOLT_RENDER_CAP);
        mBoltVelocitiesCmps.Reserve(SOL::NPC_BOLT_RENDER_CAP);
        mBoltDistancesSq.Reserve(SOL::NPC_MAX_BOLTS);
        BoltNiagara->SetAsset(boltSystem);
        BoltNiagara->SetWorldTransform(FTransform::Identity);
        BoltNiagara->SetVariableInt(SOL::CombatBoltParams::MAX_COUNT, SOL::NPC_BOLT_RENDER_CAP);
        BoltNiagara->SetVariableInt(SOL::CombatBoltParams::COUNT, 0);
        BoltNiagara->Activate(true);
        mIsBoltRendererEnabled = true;
        mIsBoltRendererRunning = true;
    }
    CreatePool(ExplosionPool, LoadNpcCombatSystem(SOL::Paths::COMBAT_EXPLOSION_NIAGARA_SYSTEM, this),
        SOL::NPC_FX_EXPLOSION_POOL_SIZE, SOL::COMBAT_FX_EXPLOSION_DURATION_S, TEXT("NpcExplosion"));
    CreatePool(ImpactPool, LoadNpcCombatSystem(SOL::Paths::COMBAT_IMPACT_NIAGARA_SYSTEM, this),
        SOL::NPC_FX_IMPACT_POOL_SIZE, SOL::COMBAT_FX_IMPACT_DURATION_S, TEXT("NpcImpact"));
    mNpcsUpdatedHandle = Npcs->OnNpcsUpdated().AddUObject(this, &ASOLNpcCombatVisuals::Redraw);
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from NPC updates
void ASOLNpcCombatVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (Npcs != nullptr)
    {
        Npcs->OnNpcsUpdated().Remove(mNpcsUpdatedHandle);
    }
    mNpcsUpdatedHandle.Reset();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Creates a pool of components for a system
void ASOLNpcCombatVisuals::CreatePool(FSOLNpcEffectPool& pool, UNiagaraSystem* system, const int32 size,
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
        MakeNpcCombatComponentAbsolute(component);
        component->bAutoActivate = false;
        component->SetAsset(system);
        component->RegisterComponent();
        pool.Slots[index].Component = component;
    }
}

//////////////////////////////////////////////////////////////////////////
// Redraws the bolts and starts and moves the effects; bound to OnNpcsUpdated
void ASOLNpcCombatVisuals::Redraw()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcCombatVisuals::Redraw);
    UpdateBolts();
    UpdateEffects(FMath::Min(FApp::GetDeltaTime(), SOL::MAX_FRAME_DELTA_S));
}

//////////////////////////////////////////////////////////////////////////
// Feeds the bolts near the camera to the bolt system
void ASOLNpcCombatVisuals::UpdateBolts()
{
    if (!mIsBoltRendererEnabled)
    {
        return;
    }

    // Only bolts within the visual radius of the viewpoint (farther ones are below a pixel)
    const FSOLNpcCombat& combat = Npcs->GetCombat();
    const TConstArrayView<FVector3d> positionsM = combat.GetBoltPositionsM();
    const TConstArrayView<FVector3d> velocitiesMps = combat.GetBoltVelocitiesMps();
    const FSOLRenderOrigin& origin = Anchor->GetRenderOrigin();
    const FVector3d frameBodyM = Npcs->GetCombatFrameBodyM();
    const FVector3d viewFromFrameM = Anchor->GetViewpointM() - frameBodyM;
    double cutoffSq = FMath::Square(SOL::NPC_COMBAT_VISUAL_RADIUS_M);
    mBoltDistancesSq.Reset();
    for (const FVector3d& positionM : positionsM)
    {
        const double distanceSq = (positionM - viewFromFrameM).SizeSquared();
        if (distanceSq <= cutoffSq)
        {
            mBoltDistancesSq.Add(distanceSq);
        }
    }

    // Over the render cap: the nearest ones (the cap-th smallest distance becomes the cutoff; pool order breaks ties)
    if (mBoltDistancesSq.Num() > SOL::NPC_BOLT_RENDER_CAP)
    {
        cutoffSq = SOLNpcScale::SelectNth(mBoltDistancesSq, SOL::NPC_BOLT_RENDER_CAP - 1);
    }
    mBoltPositionsCm.Reset();
    mBoltVelocitiesCmps.Reset();
    for (int32 bolt = 0; bolt < positionsM.Num() && mBoltPositionsCm.Num() < SOL::NPC_BOLT_RENDER_CAP; ++bolt)
    {
        if ((positionsM[bolt] - viewFromFrameM).SizeSquared() > cutoffSq)
        {
            continue;
        }
        mBoltPositionsCm.Add(FVector3f(origin.UniverseToRenderCm(frameBodyM + positionsM[bolt])));
        mBoltVelocitiesCmps.Add(FVector3f(SOLRender::EclipticToUnreal(velocitiesMps[bolt]) * SOL::METERS_TO_CM));
    }

    // Paused and hidden while no bolt is near, so its CPU sim does not walk the idle pool (as the player's bolts)
    const bool bRunning = !mBoltPositionsCm.IsEmpty();
    if (bRunning)
    {
        static const FName positionsName(SOL::CombatBoltParams::POSITIONS_CM);
        static const FName velocitiesName(SOL::CombatBoltParams::VELOCITIES);
        static const FName countName(SOL::CombatBoltParams::COUNT);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(BoltNiagara, positionsName, mBoltPositionsCm);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(BoltNiagara, velocitiesName,
            mBoltVelocitiesCmps);
        BoltNiagara->SetVariableInt(countName, mBoltPositionsCm.Num());
    }
    if (bRunning != mIsBoltRendererRunning)
    {
        mIsBoltRendererRunning = bRunning;
        BoltNiagara->SetPaused(!bRunning);
        BoltNiagara->SetVisibility(bRunning);
    }
}

//////////////////////////////////////////////////////////////////////////
// Starts this frame's effects and moves the live ones
void ASOLNpcCombatVisuals::UpdateEffects(const double deltaS)
{
    for (const FSOLNpcCombatEvent& event : Npcs->GetCombat().GetEvents())
    {
        if (event.Type == ESOLNpcCombatEventType::Killed)
        {
            StartEffect(ExplosionPool, event.PositionM, FVector3d::ZeroVector);
        }
        else
        {
            StartEffect(ImpactPool, event.PositionM, event.DirectionM);
        }
    }

    // Live effects ride with the frame body (the render origin may move under them); expired ones are released
    const FSOLRenderOrigin& origin = Anchor->GetRenderOrigin();
    const FVector3d frameBodyM = Npcs->GetCombatFrameBodyM();
    for (FSOLNpcEffectPool* pool : { &ExplosionPool, &ImpactPool })
    {
        for (FSOLNpcEffectSlot& slot : pool->Slots)
        {
            if (!slot.bIsLive)
            {
                continue;
            }
            slot.AgeS += deltaS;
            if (slot.AgeS >= pool->DurationS)
            {
                slot.bIsLive = false;
                slot.Component->Deactivate();
                continue;
            }
            slot.Component->SetWorldLocation(FVector(origin.UniverseToRenderCm(frameBodyM + slot.FrameOffsetM)));
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Starts one effect of a pool at a frame-relative position
void ASOLNpcCombatVisuals::StartEffect(FSOLNpcEffectPool& pool, const FVector3d& frameOffsetM,
    const FVector3d& directionEcliptic)
{
    if (pool.Slots.IsEmpty())
    {
        return;
    }
    FSOLNpcEffectSlot& slot = pool.Slots[pool.NextSlot];
    pool.NextSlot = (pool.NextSlot + 1) % pool.Slots.Num();
    slot.FrameOffsetM = frameOffsetM;
    slot.AgeS = 0.0;
    slot.bIsLive = true;

    static const FName directionName(SOL::CombatEffectParams::DIRECTION);
    static const FName tintName(SOL::CombatEffectParams::TINT);
    UNiagaraComponent* component = slot.Component;
    component->SetWorldLocation(FVector(Anchor->GetRenderOrigin().UniverseToRenderCm(
        Npcs->GetCombatFrameBodyM() + frameOffsetM)));
    if (!directionEcliptic.IsNearlyZero())
    {
        component->SetVariableVec3(directionName,
            FVector(SOLRender::EclipticToUnreal(directionEcliptic.GetSafeNormal())));
        component->SetVariableLinearColor(tintName, SOL::COMBAT_FX_HULL_HIT_TINT);
    }
    component->Activate(true);
}
