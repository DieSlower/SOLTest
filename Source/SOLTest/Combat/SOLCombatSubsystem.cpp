/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatSubsystem.h"

#include "Combat/SOLBoltMath.h"
#include "Combat/SOLFireCadence.h"
#include "Combat/SOLTargetDrop.h"
#include "Game/SOLGameMode.h"
#include "MinorBodies/SOLMinorBodyFragments.h"
#include "MinorBodies/SOLMinorBodySubsystem.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"

#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"

namespace
{
    // Grid candidates each sweep task reserves room for up front
    constexpr int32 COMBAT_SCRATCH_CANDIDATES = 256;

    // Occupied cells and cell entries each grid reserves up front (a busy ring patch grows them once, then they are reused)
    constexpr int32 COMBAT_GRID_RESERVED_CELLS = 4096;
    constexpr int32 COMBAT_GRID_RESERVED_ENTRIES = 16384;

    //////////////////////////////////////////////////////////////////////////
    // Returns a body's entry of a per-body array (position or velocity), or zero for INDEX_NONE (the Sun frame)
    FVector3d CombatFrameValue(const TConstArrayView<FVector3d> values, const int32 bodyIndex)
    {
        return values.IsValidIndex(bodyIndex) ? values[bodyIndex] : FVector3d::ZeroVector;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a sphere overlaps an axis-aligned box
    bool CombatSphereOverlapsBox(const FBox3d& box, const FVector3d& center, const double radius)
    {
        return box.IsValid && box.ComputeSquaredDistanceToPoint(center) <= radius * radius;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a per-frame displacement is short enough to sweep (false for NaN as well)
    bool CombatIsSweepable(const FVector3d& start, const FVector3d& end)
    {
        return (end - start).SizeSquared() <= SOL::COMBAT_MAX_SWEEP_STEP_M * SOL::COMBAT_MAX_SWEEP_STEP_M;
    }
}

//////////////////////////////////////////////////////////////////////////
// Declares its dependencies, reserves the pools and hooks the per-frame update
void USOLCombatSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    UMassEntitySubsystem* massEntities = collection.InitializeDependency<UMassEntitySubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Anchor = collection.InitializeDependency<USOLAnchorSubsystem>();
    Ships = collection.InitializeDependency<USOLShipSubsystem>();
    Targeting = collection.InitializeDependency<USOLTargetingSubsystem>();
    MinorBodies = collection.InitializeDependency<USOLMinorBodySubsystem>();
    Super::Initialize(collection);

    if (massEntities == nullptr || BodyRegistry == nullptr || Anchor == nullptr || Ships == nullptr
        || Targeting == nullptr || MinorBodies == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("CombatSubsystem %s: Mass, universe or ship subsystems missing"), *GetName());
        return;
    }

    // Read-only query over every minor body (belt asteroids and ring-pool rocks share these fragments)
    mEntityManager = massEntities->GetMutableEntityManager().AsShared();
    mMinorBodyQuery.Initialize(mEntityManager.ToSharedRef());
    mMinorBodyQuery.AddRequirement<FSOLMinorBodyStateFragment>(EMassFragmentAccess::ReadOnly);
    mMinorBodyQuery.AddRequirement<FSOLMinorBodyRenderFragment>(EMassFragmentAccess::ReadOnly);

    // Forwards each chunk to ProcessMinorBodyChunk
    mMinorBodyChunkFunction = [this](FMassExecutionContext& context)
    {
        ProcessMinorBodyChunk(context);
    };

    // Bolt pool: every array reserved to the cap once, so firing and compaction never allocate
    const int32 maxBolts = SOL::COMBAT_MAX_BOLTS;
    mBoltFrameBodies.Reserve(maxBolts);
    mBoltRelPositionsM.Reserve(maxBolts);
    mBoltRelVelocitiesMps.Reserve(maxBolts);
    mBoltLifetimesS.Reserve(maxBolts);
    mBoltOwners.Reserve(maxBolts);
    mBoltPositionsM.Reserve(maxBolts);
    mBoltVelocitiesMps.Reserve(maxBolts);
    mBoltSweepStartsM.Reserve(maxBolts);
    mBoltSweepEndsM.Reserve(maxBolts);
    mBoltSweepValid.Reserve(maxBolts);
    mBoltHitKinds.Reserve(maxBolts);
    mBoltHitIndices.Reserve(maxBolts);
    mBoltHitTimes.Reserve(maxBolts);

    // Target pool: fixed slots, all inactive
    const int32 maxTargets = SOL::COMBAT_MAX_TARGETS;
    mTargetActive.Init(false, maxTargets);
    mTargetFrameBodies.Init(INDEX_NONE, maxTargets);
    mTargetRelPositionsM.Init(FVector3d::ZeroVector, maxTargets);
    mTargetRelVelocitiesMps.Init(FVector3d::ZeroVector, maxTargets);
    mTargetPositionsM.Init(FVector3d::ZeroVector, maxTargets);
    mTargetSweepStartsM.Init(FVector3d::ZeroVector, maxTargets);
    mTargetSweepEndsM.Init(FVector3d::ZeroVector, maxTargets);
    mTargetDamage.Init(SOLDamageMath::FDamageState(), maxTargets);
    mTargetSpawnTimesS.Init(0.0, maxTargets);
    mTargetHitFlash.Init(0.0f, maxTargets);
    mTargetShieldFractions.Init(0.0f, maxTargets);
    mTargetHealthFractions.Init(0.0f, maxTargets);
    mActiveTargetCount = 0;

    // Broad phase, events and one sweep scratch per task the parallel sweep may run
    mTargetGrid.Reserve(COMBAT_GRID_RESERVED_CELLS, COMBAT_GRID_RESERVED_ENTRIES);
    mMinorBodyGrid.Reserve(COMBAT_GRID_RESERVED_CELLS, COMBAT_GRID_RESERVED_ENTRIES);
    mEvents.Reserve(SOL::COMBAT_MAX_EVENTS_PER_UPDATE);
    mSweepScratch.SetNum(FMath::Max(FTaskGraphInterface::Get().GetNumWorkerThreads() + 1, 1));
    for (FSOLSweepScratch& scratch : mSweepScratch)
    {
        scratch.CandidateIds.Reserve(COMBAT_SCRATCH_CANDIDATES);
    }

    mBodiesUpdatedHandle = Anchor->OnBodiesUpdated().AddUObject(this, &USOLCombatSubsystem::CaptureFrameDelta);
    mUniverseUpdatedHandle = Anchor->OnUniverseUpdated().AddUObject(this, &USOLCombatSubsystem::Update);
}

//////////////////////////////////////////////////////////////////////////
// Unhooks from the universe update and releases the pools
void USOLCombatSubsystem::Deinitialize()
{
    if (Anchor != nullptr)
    {
        Anchor->OnBodiesUpdated().Remove(mBodiesUpdatedHandle);
        Anchor->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mBodiesUpdatedHandle.Reset();
    mUniverseUpdatedHandle.Reset();
    mOnCombatUpdated.Clear();
    mMinorBodyChunkFunction = nullptr;
    mEntityManager.Reset();
    mEvents.Empty();
    mMinorHandles.Empty();
    mMinorPositionsM.Empty();
    mMinorSweepStartsM.Empty();
    mMinorSweepEndsM.Empty();
    mMinorHitRadiiM.Empty();
    mIsMinorSnapshotFresh = false;
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLCombatSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLCombatSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Sets the player's aim: the camera's offset from the ship and its forward direction (ecliptic, meters)
void USOLCombatSubsystem::SetPlayerAim(const FVector3d& cameraOffsetM, const FVector3d& cameraForward)
{
    mHasAim = !cameraOffsetM.ContainsNaN() && !cameraForward.ContainsNaN()
        && cameraForward.SizeSquared() > UE_DOUBLE_SMALL_NUMBER;
    if (mHasAim)
    {
        mAimCameraOffsetM = cameraOffsetM;
        mAimForward = cameraForward.GetSafeNormal();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the radius every target is hit-tested with (meters)
double USOLCombatSubsystem::GetTargetRadiusM() const
{
    return SOL::COMBAT_TARGET_RADIUS_M;
}

//////////////////////////////////////////////////////////////////////////
// Returns the body bolts and targets are stored relative to: the player's reference body, else the anchor, else none
int32 USOLCombatSubsystem::ResolveFrameBody() const
{
    if (BodyRegistry == nullptr)
    {
        return INDEX_NONE;
    }

    // Targeting candidates start with every registry body at its own index, so an index below the body count is a body
    const int32 bodyCount = BodyRegistry->GetRegistry().Num();
    const int32 reference = Targeting != nullptr ? Targeting->GetReferenceIndex() : INDEX_NONE;
    if (reference >= 0 && reference < bodyCount)
    {
        return reference;
    }
    const int32 anchor = Anchor != nullptr ? Anchor->GetAnchorIndex() : INDEX_NONE;
    return anchor >= 0 && anchor < bodyCount ? anchor : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's current position (ecliptic), or the origin for INDEX_NONE
FVector3d USOLCombatSubsystem::GetFrameBodyPositionM(const int32 bodyIndex) const
{
    return BodyRegistry != nullptr ? CombatFrameValue(BodyRegistry->GetRegistry().GetPositionsM(), bodyIndex)
        : FVector3d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's current velocity (ecliptic), or zero for INDEX_NONE
FVector3d USOLCombatSubsystem::GetFrameBodyVelocityMps(const int32 bodyIndex) const
{
    return BodyRegistry != nullptr ? CombatFrameValue(BodyRegistry->GetRegistry().GetVelocitiesMps(), bodyIndex)
        : FVector3d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Spawns a bolt at a universe position with a universe velocity; false at the bolt cap
bool USOLCombatSubsystem::FireBolt(const FVector3d& muzzlePositionM, const FVector3d& velocityMps, const int32 owner)
{
    if (BodyRegistry == nullptr || mBoltPositionsM.Num() >= SOL::COMBAT_MAX_BOLTS || muzzlePositionM.ContainsNaN()
        || velocityMps.ContainsNaN())
    {
        return false;
    }

    // Stored relative to the reference body as it is now (the bodies of the update in progress, or of the last one
    // when called between updates); the universe position is the start of the bolt's first sweep
    const int32 frameBody = ResolveFrameBody();
    mBoltFrameBodies.Add(frameBody);
    mBoltRelPositionsM.Add(muzzlePositionM - GetFrameBodyPositionM(frameBody));
    mBoltRelVelocitiesMps.Add(velocityMps - GetFrameBodyVelocityMps(frameBody));
    mBoltLifetimesS.Add(SOL::COMBAT_BOLT_LIFETIME_S);
    mBoltOwners.Add(owner);
    mBoltPositionsM.Add(muzzlePositionM);
    mBoltVelocitiesMps.Add(velocityMps);
    AddEvent(ESOLCombatEventType::BoltFired, muzzlePositionM, velocityMps, GetFrameBodyVelocityMps(frameBody),
        INDEX_NONE, owner);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Drops a target ahead of a ship, at rest in the player's reference frame; returns its slot, or INDEX_NONE
int32 USOLCombatSubsystem::DropTarget(const FVector3d& shipPositionM, const FVector3d& shipForward)
{
    if (BodyRegistry == nullptr || Targeting == nullptr || mTargetActive.IsEmpty() || shipPositionM.ContainsNaN()
        || !SOLTargetDrop::ShouldAllowDrop(mCombatTimeS - mLastDropTimeS, SOL::COMBAT_TARGET_DROP_COOLDOWN_S))
    {
        return INDEX_NONE;
    }

    // A free slot, else the oldest target makes room (every slot is active, so every spawn time is live)
    int32 slot = mTargetActive.Find(false);
    if (slot == INDEX_NONE)
    {
        slot = SOLTargetDrop::EvictionIndex(mTargetSpawnTimesS, mTargetActive.Num());
        if (slot == INDEX_NONE)
        {
            return INDEX_NONE;
        }
        AddEvent(ESOLCombatEventType::TargetEvicted, mTargetPositionsM[slot], FVector3d::ZeroVector,
            GetTargetVelocityMps(slot), slot, SOLCombat::NO_OWNER);
        DeactivateTarget(slot);
    }

    // At rest in the reference frame: still relative to that body when it is one, else moving with the reference's
    // own velocity relative to the storage body (a non-body reference, such as a locked target)
    const FVector3d positionM = SOLTargetDrop::DropPosition(shipPositionM, shipForward,
        SOL::COMBAT_TARGET_DROP_DISTANCE_M);
    const int32 frameBody = ResolveFrameBody();
    const int32 reference = Targeting->GetReferenceIndex();
    const FVector3d relVelocityMps = reference == frameBody ? FVector3d::ZeroVector
        : SOLRender::EclipticToUnreal(Targeting->GetReferenceVelocityMps()) - GetFrameBodyVelocityMps(frameBody);

    // Fill the slot; its sweep segment is a point until the next update moves it
    mTargetActive[slot] = true;
    mTargetFrameBodies[slot] = frameBody;
    mTargetRelPositionsM[slot] = positionM - GetFrameBodyPositionM(frameBody);
    mTargetRelVelocitiesMps[slot] = relVelocityMps;
    mTargetPositionsM[slot] = positionM;
    mTargetSweepStartsM[slot] = positionM - mSweepOriginM;
    mTargetSweepEndsM[slot] = positionM - mSweepOriginM;
    mTargetDamage[slot] = SOLDamageMath::MakeState(SOL::COMBAT_TARGET_SHIELD, SOL::COMBAT_TARGET_HEALTH);
    mTargetSpawnTimesS[slot] = mCombatTimeS;
    mTargetHitFlash[slot] = 0.0f;
    mTargetShieldFractions[slot] = 1.0f;
    mTargetHealthFractions[slot] = 1.0f;
    ++mActiveTargetCount;
    mLastDropTimeS = mCombatTimeS;
    AddEvent(ESOLCombatEventType::TargetDropped, positionM, FVector3d::ZeroVector, GetTargetVelocityMps(slot), slot,
        SOLCombat::NO_OWNER);
    UE_LOG(LogSOL, Verbose, TEXT("CombatSubsystem %s: target dropped in slot %d (%d active)"), *GetName(), slot,
        mActiveTargetCount);
    return slot;
}

//////////////////////////////////////////////////////////////////////////
// Removes a target from its slot (no event)
void USOLCombatSubsystem::DeactivateTarget(const int32 slot)
{
    if (mTargetActive.IsValidIndex(slot) && mTargetActive[slot])
    {
        mTargetActive[slot] = false;
        mTargetHitFlash[slot] = 0.0f;
        --mActiveTargetCount;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a target slot's current universe velocity (its frame body's velocity plus its own relative velocity)
FVector3d USOLCombatSubsystem::GetTargetVelocityMps(const int32 slot) const
{
    return mTargetFrameBodies.IsValidIndex(slot)
        ? GetFrameBodyVelocityMps(mTargetFrameBodies[slot]) + mTargetRelVelocitiesMps[slot]
        : FVector3d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Queues one event unless the cap was reached
void USOLCombatSubsystem::AddEvent(const ESOLCombatEventType type, const FVector3d& positionM,
    const FVector3d& velocityMps, const FVector3d& frameVelocityMps, const int32 targetSlot, const int32 owner)
{
    if (mEvents.Num() >= SOL::COMBAT_MAX_EVENTS_PER_UPDATE)
    {
        ++mDroppedEventCount;
        return;
    }
    FSOLCombatEvent& event = mEvents.AddDefaulted_GetRef();
    event.Type = type;
    event.PositionM = positionM;
    event.VelocityMps = velocityMps;
    event.FrameVelocityMps = frameVelocityMps;
    event.TargetSlot = targetSlot;
    event.Owner = owner;
}

//////////////////////////////////////////////////////////////////////////
// Runs one combat frame; bound to the anchor's OnUniverseUpdated (after every OnBodiesUpdated listener has run)
void USOLCombatSubsystem::Update()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLCombatSubsystem::Update);
    if (BodyRegistry == nullptr || Anchor == nullptr || Ships == nullptr || Targeting == nullptr
        || !mEntityManager.IsValid())
    {
        return;
    }

    // Events broadcast by the previous update are dropped; any queued since (calls between updates) are kept
    if (mPublishedEventCount > 0)
    {
        mEvents.RemoveAt(0, FMath::Min(mPublishedEventCount, mEvents.Num()), EAllowShrinking::No);
        mPublishedEventCount = 0;
    }
    mDroppedEventCount = 0;

    // The real, hitch-clamped delta of this frame's body update (zero if none ran)
    const double dt = FMath::Max(static_cast<double>(mFrameDeltaS), 0.0);
    mFrameDeltaS = 0.0f;
    mCombatTimeS += dt;

    // Sweep frame: centred on the player ship (ecliptic) now and at the previous update
    const bool bHasShip = Ships->HasPlayerShip();
    const FSOLShipState shipState = bHasShip ? Ships->GetState() : FSOLShipState();
    mSweepOriginM = bHasShip ? SOLRender::EclipticToUnreal(shipState.PositionM) : FVector3d::ZeroVector;
    if (!mHasPrevSweepOrigin)
    {
        mPrevSweepOriginM = mSweepOriginM;
    }

    // Move what exists, then add a requested target (it joins this frame's sweep at its drop point)
    AdvanceTargets(dt);
    if (mIsPlayerDropRequested)
    {
        mIsPlayerDropRequested = false;
        if (bHasShip)
        {
            DropTarget(mSweepOriginM, SOLRender::EclipticToUnreal(shipState.Orientation.GetForwardVector()));
        }
    }
    AdvanceBolts(dt);

    // Broad phase and the parallel sweep, only when some bolt has a segment to sweep this frame
    if (mBoltSweepBounds.IsValid)
    {
        BuildTargetGrid();
        BuildMinorBodyGrid();
        ParallelForWithExistingTaskContext(TEXT("SOLCombatSweep"), MakeArrayView(mSweepScratch),
            mBoltPositionsM.Num(), SOL::COMBAT_SWEEP_MIN_BOLTS_PER_TASK,
            [this](FSOLSweepScratch& scratch, const int32 boltIndex)
            {
                SweepBolt(scratch, boltIndex);
            });
    }
    else
    {
        // No snapshot this frame, so the next one cannot use it as previous positions
        mIsMinorSnapshotFresh = false;
    }
    ApplyBoltHits();

    // New bolts start sweeping next frame; then the fractions the visuals read
    FirePlayerGuns(dt, bHasShip, shipState);
    RefreshTargetFractions();
    mPrevSweepOriginM = mSweepOriginM;
    mHasPrevSweepOrigin = bHasShip;

    mOnCombatUpdated.Broadcast();
    mPublishedEventCount = mEvents.Num();
}

//////////////////////////////////////////////////////////////////////////
// Moves every active target with its frame, regenerates its shield, fades its flash and computes its sweep segment
void USOLCombatSubsystem::AdvanceTargets(const double dt)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLCombatSubsystem::AdvanceTargets);
    if (mActiveTargetCount == 0)
    {
        return;
    }

    // One registry read for the whole loop (no UObject access per target)
    const TConstArrayView<FVector3d> bodyPositionsM = BodyRegistry->GetRegistry().GetPositionsM();
    const float flashFade = static_cast<float>(dt / SOL::COMBAT_HIT_FLASH_DURATION_S);
    const int32 slotCount = mTargetActive.Num();
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        if (!mTargetActive[slot])
        {
            continue;
        }

        // Position in its frame, then the segment from last update's position (each relative to the ship then)
        mTargetRelPositionsM[slot] = SOLBoltMath::AdvancePosition(mTargetRelPositionsM[slot],
            mTargetRelVelocitiesMps[slot], dt);
        const FVector3d positionM = CombatFrameValue(bodyPositionsM, mTargetFrameBodies[slot])
            + mTargetRelPositionsM[slot];
        const FVector3d sweepEndM = positionM - mSweepOriginM;
        const FVector3d sweepStartM = mTargetPositionsM[slot] - mPrevSweepOriginM;
        mTargetSweepStartsM[slot] = CombatIsSweepable(sweepStartM, sweepEndM) ? sweepStartM : sweepEndM;
        mTargetSweepEndsM[slot] = sweepEndM;
        mTargetPositionsM[slot] = positionM;

        // Shield regeneration after the delay, and the hit flash fading out
        mTargetDamage[slot] = SOLDamageMath::TickRegen(mTargetDamage[slot], dt,
            SOL::COMBAT_TARGET_SHIELD_REGEN_DELAY_S, SOL::COMBAT_TARGET_SHIELD_REGEN_PER_S, SOL::COMBAT_TARGET_SHIELD);
        mTargetHitFlash[slot] = FMath::Max(mTargetHitFlash[slot] - flashFade, 0.0f);
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves every bolt with its frame, ages it, computes its sweep segment and grows the frame's bolt bounding box
void USOLCombatSubsystem::AdvanceBolts(const double dt)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLCombatSubsystem::AdvanceBolts);
    const int32 boltCount = mBoltPositionsM.Num();
    mBoltSweepBounds.Init();

    // Per-frame scratch sized to the live bolts, inside the capacity reserved at initialization
    mBoltSweepStartsM.SetNumUninitialized(boltCount, EAllowShrinking::No);
    mBoltSweepEndsM.SetNumUninitialized(boltCount, EAllowShrinking::No);
    mBoltSweepValid.SetNumUninitialized(boltCount, EAllowShrinking::No);
    mBoltHitKinds.SetNumUninitialized(boltCount, EAllowShrinking::No);
    mBoltHitIndices.SetNumUninitialized(boltCount, EAllowShrinking::No);
    mBoltHitTimes.SetNumUninitialized(boltCount, EAllowShrinking::No);
    if (boltCount == 0)
    {
        return;
    }

    // One registry read for the whole loop (no UObject access per bolt)
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const TConstArrayView<FVector3d> bodyPositionsM = registry.GetPositionsM();
    const TConstArrayView<FVector3d> bodyVelocitiesMps = registry.GetVelocitiesMps();
    for (int32 bolt = 0; bolt < boltCount; ++bolt)
    {
        // Integrate in the bolt's frame, then back to universe meters
        const int32 frameBody = mBoltFrameBodies[bolt];
        mBoltRelPositionsM[bolt] = SOLBoltMath::AdvancePosition(mBoltRelPositionsM[bolt], mBoltRelVelocitiesMps[bolt],
            dt);
        mBoltLifetimesS[bolt] = SOLBoltMath::AdvanceLifetime(mBoltLifetimesS[bolt], dt);
        const FVector3d positionM = CombatFrameValue(bodyPositionsM, frameBody) + mBoltRelPositionsM[bolt];

        // This frame's segment in the ship-centred sweep frame; an overlong one (a teleport) is not swept
        const FVector3d sweepStartM = mBoltPositionsM[bolt] - mPrevSweepOriginM;
        const FVector3d sweepEndM = positionM - mSweepOriginM;
        const bool bSweepable = CombatIsSweepable(sweepStartM, sweepEndM);
        mBoltSweepStartsM[bolt] = sweepStartM;
        mBoltSweepEndsM[bolt] = sweepEndM;
        mBoltSweepValid[bolt] = bSweepable;
        mBoltHitKinds[bolt] = ESOLBoltHit::None;
        mBoltHitIndices[bolt] = INDEX_NONE;
        mBoltHitTimes[bolt] = 0.0;
        mBoltPositionsM[bolt] = positionM;
        mBoltVelocitiesMps[bolt] = CombatFrameValue(bodyVelocitiesMps, frameBody) + mBoltRelVelocitiesMps[bolt];
        if (bSweepable)
        {
            mBoltSweepBounds += sweepStartM;
            mBoltSweepBounds += sweepEndM;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Fills the target grid with every active target's swept bounding sphere that can reach a bolt segment
void USOLCombatSubsystem::BuildTargetGrid()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLCombatSubsystem::BuildTargetGrid);
    mTargetGrid.Clear();
    if (mActiveTargetCount == 0)
    {
        return;
    }
    const FBox3d reachBounds = mBoltSweepBounds.ExpandBy(SOL::COMBAT_BOLT_HIT_RADIUS_M);
    const int32 slotCount = mTargetActive.Num();
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        if (!mTargetActive[slot])
        {
            continue;
        }
        const FVector3d centerM = 0.5 * (mTargetSweepStartsM[slot] + mTargetSweepEndsM[slot]);
        const double radiusM = SOL::COMBAT_TARGET_RADIUS_M
            + 0.5 * FVector3d::Dist(mTargetSweepStartsM[slot], mTargetSweepEndsM[slot]);
        if (CombatSphereOverlapsBox(reachBounds, centerM, radiusM))
        {
            mTargetGrid.Insert(slot, centerM, radiusM);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Snapshots the minor bodies and fills the minor-body grid with those whose swept sphere reaches the bolt box
void USOLCombatSubsystem::BuildMinorBodyGrid()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLCombatSubsystem::BuildMinorBodyGrid);
    mMinorBodyGrid.Clear();
    mMinorScanIndex = 0;

    // Serial chunks: the snapshot is indexed by the running position in the query's iteration order
    FMassExecutionContext context = mEntityManager->CreateExecutionContext(0.0f);
    mMinorBodyQuery.ForEachEntityChunk(context, mMinorBodyChunkFunction);

    // Entities that went away leave the snapshot (no reallocation)
    if (mMinorScanIndex < mMinorHandles.Num())
    {
        mMinorHandles.SetNum(mMinorScanIndex, EAllowShrinking::No);
        mMinorPositionsM.SetNum(mMinorScanIndex, EAllowShrinking::No);
        mMinorSweepStartsM.SetNum(mMinorScanIndex, EAllowShrinking::No);
        mMinorSweepEndsM.SetNum(mMinorScanIndex, EAllowShrinking::No);
        mMinorHitRadiiM.SetNum(mMinorScanIndex, EAllowShrinking::No);
    }
    mIsMinorSnapshotFresh = true;
}

//////////////////////////////////////////////////////////////////////////
// Reads one chunk of minor bodies into the snapshot and the grid
void USOLCombatSubsystem::ProcessMinorBodyChunk(FMassExecutionContext& context)
{
    const TConstArrayView<FSOLMinorBodyStateFragment> states = context.GetFragmentView<FSOLMinorBodyStateFragment>();
    const TConstArrayView<FSOLMinorBodyRenderFragment> renders =
        context.GetFragmentView<FSOLMinorBodyRenderFragment>();
    const int32 entityCount = context.GetNumEntities();
    const int32 first = mMinorScanIndex;
    mMinorScanIndex += entityCount;

    // The snapshot only grows when the minor-body population does (the first scans after spawning)
    if (mMinorHandles.Num() < mMinorScanIndex)
    {
        mMinorHandles.SetNum(mMinorScanIndex);
        mMinorPositionsM.SetNum(mMinorScanIndex);
        mMinorSweepStartsM.SetNum(mMinorScanIndex);
        mMinorSweepEndsM.SetNum(mMinorScanIndex);
        mMinorHitRadiiM.SetNum(mMinorScanIndex);
    }

    const FBox3d reachBounds = mBoltSweepBounds.ExpandBy(SOL::COMBAT_BOLT_HIT_RADIUS_M);
    for (int32 entity = 0; entity < entityCount; ++entity)
    {
        // Previous position only if this index held the same entity at the previous update
        const int32 index = first + entity;
        const FMassEntityHandle handle = context.GetEntity(entity);
        const FVector3d positionM = states[entity].PositionM;
        const bool bHasPrevious = mIsMinorSnapshotFresh && mMinorHandles[index] == handle;
        const FVector3d previousM = bHasPrevious ? mMinorPositionsM[index] : positionM;
        mMinorHandles[index] = handle;
        mMinorPositionsM[index] = positionM;
        if (!renders[entity].bActive)
        {
            continue;
        }

        // Swept segment in the sweep frame (a jump such as a ring-group reassignment is tested at its end only)
        const FVector3d sweepEndM = positionM - mSweepOriginM;
        const FVector3d rawStartM = previousM - mPrevSweepOriginM;
        const FVector3d sweepStartM = bHasPrevious && CombatIsSweepable(rawStartM, sweepEndM) ? rawStartM : sweepEndM;
        const double hitRadiusM = FMath::Max(renders[entity].RadiusM * SOL::COMBAT_MINOR_BODY_HIT_RADIUS_SCALE,
            SOL::COMBAT_MINOR_BODY_MIN_HIT_RADIUS_M);
        const FVector3d centerM = 0.5 * (sweepStartM + sweepEndM);
        const double boundRadiusM = hitRadiusM + 0.5 * FVector3d::Dist(sweepStartM, sweepEndM);
        if (!CombatSphereOverlapsBox(reachBounds, centerM, boundRadiusM))
        {
            continue;
        }
        mMinorSweepStartsM[index] = sweepStartM;
        mMinorSweepEndsM[index] = sweepEndM;
        mMinorHitRadiiM[index] = hitRadiusM;
        mMinorBodyGrid.Insert(index, centerM, boundRadiusM);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sweeps one bolt against the grids and records its earliest hit (runs on worker threads; writes only boltIndex)
void USOLCombatSubsystem::SweepBolt(FSOLSweepScratch& scratch, const int32 boltIndex)
{
    if (!mBoltSweepValid[boltIndex])
    {
        return;
    }
    const FVector3d& startM = mBoltSweepStartsM[boltIndex];
    const FVector3d& endM = mBoltSweepEndsM[boltIndex];
    ESOLBoltHit hitKind = ESOLBoltHit::None;
    int32 hitIndex = INDEX_NONE;
    double hitTime = TNumericLimits<double>::Max();

    // Targets, each moving linearly over the frame
    if (mTargetGrid.Num() > 0)
    {
        mTargetGrid.QueryCandidates(startM, endM, SOL::COMBAT_BOLT_HIT_RADIUS_M, scratch.CandidateIds);
        for (const int32 slot : scratch.CandidateIds)
        {
            const SOLBoltMath::FSweepHit hit = SOLBoltMath::SweptSphereHitMoving(startM, endM,
                mTargetSweepStartsM[slot], mTargetSweepEndsM[slot],
                SOL::COMBAT_TARGET_RADIUS_M + SOL::COMBAT_BOLT_HIT_RADIUS_M);
            if (hit.bHit && hit.Time < hitTime)
            {
                hitKind = ESOLBoltHit::Target;
                hitIndex = slot;
                hitTime = hit.Time;
            }
        }
    }

    // Belt asteroids and ring rocks, each moving linearly over the frame
    if (mMinorBodyGrid.Num() > 0)
    {
        mMinorBodyGrid.QueryCandidates(startM, endM, SOL::COMBAT_BOLT_HIT_RADIUS_M, scratch.CandidateIds);
        for (const int32 index : scratch.CandidateIds)
        {
            const SOLBoltMath::FSweepHit hit = SOLBoltMath::SweptSphereHitMoving(startM, endM,
                mMinorSweepStartsM[index], mMinorSweepEndsM[index],
                mMinorHitRadiiM[index] + SOL::COMBAT_BOLT_HIT_RADIUS_M);
            if (hit.bHit && hit.Time < hitTime)
            {
                hitKind = ESOLBoltHit::MinorBody;
                hitIndex = index;
                hitTime = hit.Time;
            }
        }
    }
    mBoltHitKinds[boltIndex] = hitKind;
    mBoltHitIndices[boltIndex] = hitIndex;
    mBoltHitTimes[boltIndex] = hitKind == ESOLBoltHit::None ? 0.0 : hitTime;
}

//////////////////////////////////////////////////////////////////////////
// Returns where a bolt's recorded hit happened: its sweep-frame point at the hit time plus the sweep origin then
FVector3d USOLCombatSubsystem::ComputeHitPositionM(const int32 boltIndex) const
{
    const double t = mBoltHitTimes[boltIndex];
    return FMath::Lerp(mBoltSweepStartsM[boltIndex], mBoltSweepEndsM[boltIndex], t)
        + FMath::Lerp(mPrevSweepOriginM, mSweepOriginM, t);
}

//////////////////////////////////////////////////////////////////////////
// Applies each bolt's recorded hit in bolt order, then removes the hit and expired bolts
void USOLCombatSubsystem::ApplyBoltHits()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLCombatSubsystem::ApplyBoltHits);
    const int32 boltCount = mBoltPositionsM.Num();
    int32 write = 0;
    for (int32 bolt = 0; bolt < boltCount; ++bolt)
    {
        bool bRemove = SOLBoltMath::IsExpired(mBoltLifetimesS[bolt]);
        const ESOLBoltHit hitKind = mBoltHitKinds[bolt];
        const int32 hitIndex = mBoltHitIndices[bolt];

        // A target hit: shield first, then health; a target an earlier bolt destroyed this frame lets this one fly on
        if (hitKind == ESOLBoltHit::Target && mTargetActive.IsValidIndex(hitIndex) && mTargetActive[hitIndex]
            && !mTargetDamage[hitIndex].bDead)
        {
            const SOLDamageMath::FDamageState before = mTargetDamage[hitIndex];
            const SOLDamageMath::FDamageState after = SOLDamageMath::ApplyDamage(before, SOL::COMBAT_BOLT_DAMAGE);
            mTargetDamage[hitIndex] = after;
            mTargetHitFlash[hitIndex] = 1.0f;
            const FVector3d targetVelocityMps = GetTargetVelocityMps(hitIndex);
            AddEvent(ESOLCombatEventType::TargetHit, ComputeHitPositionM(bolt), mBoltVelocitiesMps[bolt],
                targetVelocityMps, hitIndex, mBoltOwners[bolt]);
            if (before.Shield > 0.0 && after.Shield <= 0.0)
            {
                AddEvent(ESOLCombatEventType::ShieldBroken, mTargetPositionsM[hitIndex], FVector3d::ZeroVector,
                    targetVelocityMps, hitIndex, mBoltOwners[bolt]);
            }
            if (after.bDead)
            {
                AddEvent(ESOLCombatEventType::TargetDestroyed, mTargetPositionsM[hitIndex], FVector3d::ZeroVector,
                    targetVelocityMps, hitIndex, mBoltOwners[bolt]);
                DeactivateTarget(hitIndex);
            }
            bRemove = true;
        }
        else if (hitKind == ESOLBoltHit::MinorBody)
        {
            // Absorbed with a spark puff; the rock is on rails and unharmed
            AddEvent(ESOLCombatEventType::BoltAbsorbed, ComputeHitPositionM(bolt), mBoltVelocitiesMps[bolt],
                GetFrameBodyVelocityMps(mBoltFrameBodies[bolt]), INDEX_NONE, mBoltOwners[bolt]);
            bRemove = true;
        }

        // Compact the survivors in place, keeping their order
        if (!bRemove)
        {
            if (write != bolt)
            {
                mBoltFrameBodies[write] = mBoltFrameBodies[bolt];
                mBoltRelPositionsM[write] = mBoltRelPositionsM[bolt];
                mBoltRelVelocitiesMps[write] = mBoltRelVelocitiesMps[bolt];
                mBoltLifetimesS[write] = mBoltLifetimesS[bolt];
                mBoltOwners[write] = mBoltOwners[bolt];
                mBoltPositionsM[write] = mBoltPositionsM[bolt];
                mBoltVelocitiesMps[write] = mBoltVelocitiesMps[bolt];
            }
            ++write;
        }
    }
    mBoltFrameBodies.SetNum(write, EAllowShrinking::No);
    mBoltRelPositionsM.SetNum(write, EAllowShrinking::No);
    mBoltRelVelocitiesMps.SetNum(write, EAllowShrinking::No);
    mBoltLifetimesS.SetNum(write, EAllowShrinking::No);
    mBoltOwners.SetNum(write, EAllowShrinking::No);
    mBoltPositionsM.SetNum(write, EAllowShrinking::No);
    mBoltVelocitiesMps.SetNum(write, EAllowShrinking::No);
}

//////////////////////////////////////////////////////////////////////////
// Fires the player's guns for this frame at the cadence rate, alternating muzzles
void USOLCombatSubsystem::FirePlayerGuns(const double dt, const bool bHasShip, const FSOLShipState& shipState)
{
    const SOLFireCadence::FStep step = SOLFireCadence::Step(mGunCooldownS, dt, SOL::COMBAT_FIRE_RATE_PER_S,
        bHasShip && mIsPlayerTriggerHeld, SOL::COMBAT_MAX_SHOTS_PER_FRAME);
    mGunCooldownS = step.CooldownS;
    if (step.Shots == 0)
    {
        return;
    }

    // Ship pose and the camera ray in the ecliptic frame (the ship's own state is Unreal-handed)
    const FVector3d shipPositionM = SOLRender::EclipticToUnreal(shipState.PositionM);
    const FVector3d shipVelocityMps = SOLRender::EclipticToUnreal(shipState.VelocityMps);
    const FVector3d cameraPositionM = mHasAim ? shipPositionM + mAimCameraOffsetM : shipPositionM;
    const FVector3d cameraForward = mHasAim ? mAimForward
        : SOLRender::EclipticToUnreal(shipState.Orientation.GetForwardVector());

    // Each shot from the next muzzle, aimed at the crosshair point, placed where it has flown since it was fired
    for (int32 shot = 0; shot < step.Shots; ++shot)
    {
        FVector3d localOffsetM = SOL::COMBAT_GUN_MUZZLE_OFFSET_M;
        localOffsetM.Y *= mNextGunSide == 0 ? 1.0 : -1.0;
        mNextGunSide = 1 - mNextGunSide;
        const FVector3d muzzleM = shipPositionM
            + SOLRender::EclipticToUnreal(shipState.Orientation.RotateVector(localOffsetM));
        const FVector3d aimDirection = SOLBoltMath::ConvergedAimDirection(muzzleM, cameraPositionM, cameraForward,
            SOL::COMBAT_CONVERGENCE_DISTANCE_M);
        const FVector3d velocityMps = SOLBoltMath::MuzzleVelocity(shipVelocityMps, aimDirection,
            SOL::COMBAT_MUZZLE_SPEED_MPS);
        const double ageS = SOLFireCadence::ShotAgeS(step, shot, SOL::COMBAT_FIRE_RATE_PER_S);
        if (FireBolt(muzzleM + (velocityMps - shipVelocityMps) * ageS, velocityMps, SOLCombat::PLAYER_OWNER))
        {
            mBoltLifetimesS.Last() = SOLBoltMath::AdvanceLifetime(mBoltLifetimesS.Last(), ageS);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the per-slot shield and health fractions the visuals read
void USOLCombatSubsystem::RefreshTargetFractions()
{
    if (mActiveTargetCount == 0)
    {
        return;
    }
    const int32 slotCount = mTargetActive.Num();
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        if (mTargetActive[slot])
        {
            mTargetShieldFractions[slot] = static_cast<float>(mTargetDamage[slot].Shield / SOL::COMBAT_TARGET_SHIELD);
            mTargetHealthFractions[slot] = static_cast<float>(mTargetDamage[slot].Health / SOL::COMBAT_TARGET_HEALTH);
        }
    }
}
