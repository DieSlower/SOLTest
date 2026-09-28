/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLAnchorSubsystem.h"

#include "Game/SOLGameMode.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

//////////////////////////////////////////////////////////////////////////
// Resolves the clock and registry dependencies and picks the initial anchor
void USOLAnchorSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    SimClock = collection.InitializeDependency<USOLSimClockSubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Super::Initialize(collection);

    if (BodyRegistry != nullptr)
    {
        mAnchor.Update(BodyRegistry->GetRegistry().GetPositionsM(), mObserverPositionM);
    }
}

//////////////////////////////////////////////////////////////////////////
// Drops the observer actor reference
void USOLAnchorSubsystem::Deinitialize()
{
    mObserverActor.Reset();
    mOnUniverseUpdated.Clear();
    mOnRenderOriginShifted.Clear();
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Advances the universe one frame: clock, registry, anchor, rebase, then notifies listeners
void USOLAnchorSubsystem::Tick(const float deltaTime)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLAnchorSubsystem::Tick);
    Super::Tick(deltaTime);

    if (SimClock == nullptr || BodyRegistry == nullptr)
    {
        return;
    }
    SimClock->AdvanceFrame(deltaTime);
    BodyRegistry->UpdateFromClock();

    // Re-anchor with hysteresis; a new anchor, or drifting too far from the render origin, rebases onto the observer
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const int32 previousAnchor = mAnchor.GetAnchorIndex();
    const int32 anchor = mAnchor.Update(registry.GetPositionsM(), mObserverPositionM);
    const bool bAnchorChanged = anchor != previousAnchor;
    if (bAnchorChanged)
    {
        UE_LOG(LogSOL, Log, TEXT("Anchor %s: %s -> %s"), *GetName(),
            previousAnchor == INDEX_NONE ? TEXT("none") : *registry.GetName(previousAnchor).ToString(),
            anchor == INDEX_NONE ? TEXT("none") : *registry.GetName(anchor).ToString());
    }
    RebaseRenderOrigin(bAnchorChanged);

    mOnUniverseUpdated.Broadcast();
}

//////////////////////////////////////////////////////////////////////////
// Returns the stat id used by the tickable-object profiler
TStatId USOLAnchorSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(USOLAnchorSubsystem, STATGROUP_Tickables);
}

//////////////////////////////////////////////////////////////////////////
// Teleports the observer: re-picks the plain nearest body as anchor and rebases the render origin onto it
void USOLAnchorSubsystem::SetObserverPositionM(const FVector3d& positionM)
{
    mObserverPositionM = positionM;
    if (BodyRegistry != nullptr)
    {
        // A teleport carries no hysteresis from the old anchor
        const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
        mAnchor = FSOLAnchorSelector();
        const int32 anchor = mAnchor.Update(registry.GetPositionsM(), mObserverPositionM);
        UE_LOG(LogSOL, Log, TEXT("Anchor %s: observer placed, anchor %s"), *GetName(),
            anchor == INDEX_NONE ? TEXT("none") : *registry.GetName(anchor).ToString());
    }
    RebaseRenderOrigin(true);
}

//////////////////////////////////////////////////////////////////////////
// Moves the observer by a universe-space delta
void USOLAnchorSubsystem::MoveObserverM(const FVector3d& deltaM)
{
    mObserverPositionM += deltaM;
}

//////////////////////////////////////////////////////////////////////////
// Registers the actor that represents the observer and moves it to the observer's render location
void USOLAnchorSubsystem::SetObserverActor(AActor* observerActor)
{
    mObserverActor = observerActor;
    if (observerActor != nullptr)
    {
        observerActor->SetActorLocation(UniverseToRenderCm(mObserverPositionM));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the anchor body's universe position, or the Sun-frame origin if there is no anchor
FVector3d USOLAnchorSubsystem::GetAnchorPositionM() const
{
    const int32 anchor = mAnchor.GetAnchorIndex();
    if (BodyRegistry == nullptr || anchor == INDEX_NONE)
    {
        return FVector3d::ZeroVector;
    }
    return BodyRegistry->GetRegistry().GetPositionM(anchor);
}

//////////////////////////////////////////////////////////////////////////
// Converts a universe position (meters, ecliptic) to an Unreal render location (cm, Unreal axes)
FVector USOLAnchorSubsystem::UniverseToRenderCm(const FVector3d& universeM) const
{
    return FVector(mRenderOrigin.UniverseToRenderCm(universeM));
}

//////////////////////////////////////////////////////////////////////////
// Converts an Unreal render location (cm, Unreal axes) to a universe position (meters, ecliptic)
FVector3d USOLAnchorSubsystem::RenderToUniverseM(const FVector& renderCm) const
{
    return mRenderOrigin.RenderCmToUniverseM(FVector3d(renderCm));
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's render location and radius (cm, Unreal axes); 1:1 near the observer, angular size kept far away
FSOLRenderPlacement USOLAnchorSubsystem::ComputeBodyRenderPlacement(const int32 bodyIndex) const
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    return mRenderOrigin.BodyPlacement(registry.GetPositionM(bodyIndex), mObserverPositionM,
        registry.GetRadiusM(bodyIndex), SOL::DEFAULT_MAX_RENDER_DISTANCE_CM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the index of the body whose surface is nearest the observer, and its altitude in meters
int32 USOLAnchorSubsystem::FindNearestBody(double& outAltitudeM) const
{
    outAltitudeM = 0.0;
    if (BodyRegistry == nullptr)
    {
        return INDEX_NONE;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    int32 nearest = INDEX_NONE;
    double bestAltitudeM = TNumericLimits<double>::Max();
    for (int32 index = 0; index < registry.Num(); ++index)
    {
        const double altitudeM = FVector3d::Dist(registry.GetPositionM(index), mObserverPositionM)
            - registry.GetRadiusM(index);
        if (altitudeM < bestAltitudeM)
        {
            bestAltitudeM = altitudeM;
            nearest = index;
        }
    }
    outAltitudeM = nearest == INDEX_NONE ? 0.0 : bestAltitudeM;
    return nearest;
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLAnchorSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLAnchorSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Snaps the render origin onto the observer when forced or due, then shifts the observer actor and notifies
void USOLAnchorSubsystem::RebaseRenderOrigin(const bool bForceSnap)
{
    const FVector3d previousOriginM = mRenderOrigin.OriginM;
    if (!mRenderOrigin.Update(mObserverPositionM, bForceSnap))
    {
        return;
    }

    // The shift is where the old origin now renders: fixed universe points move by this much in Unreal space
    const FVector shiftCm(mRenderOrigin.UniverseToRenderCm(previousOriginM));
    if (AActor* observerActor = mObserverActor.Get())
    {
        observerActor->SetActorLocation(FVector::ZeroVector);
    }
    mOnRenderOriginShifted.Broadcast(shiftCm);
}
