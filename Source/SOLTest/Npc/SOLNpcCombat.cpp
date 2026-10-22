/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcCombat.h"

#include "Combat/SOLBoltMath.h"
#include "Combat/SOLFireCadence.h"
#include "Npc/SOLNpcScaleMath.h"
#include "SOLConstants.h"

#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"

namespace
{
    // Ships and bolts per parallel batch
    constexpr int32 NPC_AI_MIN_BATCH = 64;
    constexpr int32 NPC_BOLT_MIN_BATCH = 256;

    // NPC combat events kept per frame (only those near the camera are made)
    constexpr int32 NPC_MAX_EVENTS_PER_FRAME = 512;

    // HashUnit streams of the per-ship strafe axis and the statistical rolls
    constexpr uint32 NPC_STRAFE_HASH_STREAM = 0x57AFEu;
    constexpr uint32 NPC_SHOT_HASH_STREAM = 0x5407u;

    // Bolts leave this many hit radii ahead of the shooter's centre
    constexpr double NPC_MUZZLE_OFFSET_RADII = 1.5;

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship's deterministic strafe axis (so ships circle their targets in varied planes)
    FVector3d StrafeAxis(const uint32 seed, const int32 npcIndex)
    {
        const double angle = SOLNpcScale::HashUnit(seed, static_cast<uint32>(npcIndex), NPC_STRAFE_HASH_STREAM)
            * UE_DOUBLE_TWO_PI;
        return FVector3d(FMath::Cos(angle), FMath::Sin(angle), 0.5);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the grids and reserves the pools
FSOLNpcCombat::FSOLNpcCombat()
    : mTargetGrid(SOL::NPC_AI_TARGET_GRID_CELL_M),
      mShipGrid(SOL::NPC_AI_SHIP_GRID_CELL_M)
{
    for (int32 classIndex = 0; classIndex < SOLNpcAi::ClassCount(); ++classIndex)
    {
        mClassEnvelopes.Add(SOLNpcAi::ClassEnvelope(SOLNpcAi::GetClass(classIndex)));
    }
}

//////////////////////////////////////////////////////////////////////////
// Clears every ship, bolt and event, and sizes the population index map
void FSOLNpcCombat::Reset(const int32 populationCount)
{
    mShips.Reset();
    mSlotOfNpc.Init(INDEX_NONE, FMath::Max(populationCount, 0));
    mTargetGrid.Clear();
    mShipGrid.Clear();
    mBoltPositionsM.Reset();
    mBoltVelocitiesMps.Reset();
    mBoltLifetimeS.Reset();
    mBoltDamage.Reset();
    mBoltFaction.Reset();
    mBoltReal.Reset();
    mBoltHitSlot.Reset();
    mBoltHitPositionM.Reset();
    mEvents.Reset();
    mStats = FSOLNpcCombatStats();
    if (populationCount > 0 && mBoltPositionsM.Max() < SOL::NPC_MAX_BOLTS)
    {
        mBoltPositionsM.Reserve(SOL::NPC_MAX_BOLTS);
        mBoltVelocitiesMps.Reserve(SOL::NPC_MAX_BOLTS);
        mBoltLifetimeS.Reserve(SOL::NPC_MAX_BOLTS);
        mBoltDamage.Reserve(SOL::NPC_MAX_BOLTS);
        mBoltFaction.Reserve(SOL::NPC_MAX_BOLTS);
        mBoltReal.Reserve(SOL::NPC_MAX_BOLTS);
        mBoltHitSlot.Reserve(SOL::NPC_MAX_BOLTS);
        mBoltHitPositionM.Reserve(SOL::NPC_MAX_BOLTS);
        mEvents.Reserve(NPC_MAX_EVENTS_PER_FRAME);
    }
}

//////////////////////////////////////////////////////////////////////////
// Starts a frame's gather: forgets last frame's ships (keeps allocations)
void FSOLNpcCombat::BeginGather()
{
    mShips.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Sizes this frame's ship list (every slot must then be written with SetShip)
void FSOLNpcCombat::SetShipCount(const int32 count)
{
    mShips.SetNumUninitialized(FMath::Max(count, 0), EAllowShrinking::No);
}

//////////////////////////////////////////////////////////////////////////
// Writes one gathered ship into its slot and maps its population index to it (thread-safe for distinct slots)
void FSOLNpcCombat::SetShip(const int32 slot, const FSOLNpcAiShip& ship)
{
    mShips[slot] = ship;
    if (mSlotOfNpc.IsValidIndex(ship.NpcIndex))
    {
        mSlotOfNpc[ship.NpcIndex] = slot;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the gathered ship of a population index after Step, or nullptr
const FSOLNpcAiShip* FSOLNpcCombat::FindShip(const int32 npcIndex) const
{
    const int32 slot = mSlotOfNpc.IsValidIndex(npcIndex) ? mSlotOfNpc[npcIndex] : INDEX_NONE;
    return mShips.IsValidIndex(slot) ? &mShips[slot] : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Ends the frame: clears the population index map entries of this frame's ships
void FSOLNpcCombat::EndFrame()
{
    for (const FSOLNpcAiShip& ship : mShips)
    {
        if (mSlotOfNpc.IsValidIndex(ship.NpcIndex))
        {
            mSlotOfNpc[ship.NpcIndex] = INDEX_NONE;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Steers, fires and resolves combat for the gathered ships
void FSOLNpcCombat::Step(const FSOLNpcCombatInputs& inputs)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FSOLNpcCombat::Step);
    const double startS = FPlatformTime::Seconds();
    const double dt = inputs.DeltaS;
    ++mFrame;
    mEvents.Reset();
    mStats = FSOLNpcCombatStats();
    mStats.Ships = mShips.Num();
    if (!(dt > 0.0))
    {
        return;
    }
    if (mScratch.IsEmpty())
    {
        // One scratch per possible parallel task (sized on first use: the task graph is not up for the CDO)
        mScratch.SetNum(FMath::Max(FTaskGraphInterface::Get().GetNumWorkerThreads() + 2, 4));
    }

    // Respawn timers, in their own parallel pass before the steering (so the steering never sees a ship come alive);
    // each ship touches only itself
    mTargetGrid.Clear();
    mShipGrid.Clear();
    int32 respawns = 0;
    ParallelFor(TEXT("NpcRespawn"), mShips.Num(), NPC_AI_MIN_BATCH * 4,
        [this, &inputs, dt, &respawns](const int32 slot)
    {
        FSOLNpcAiShip& ship = mShips[slot];
        const SOLNpcAi::FClassParams& shipClass = SOLNpcAi::GetClass(ship.ClassIndex);
        ship.CameraDistanceM = static_cast<float>((ship.PositionM - inputs.CameraPositionM).Size());
        ship.NewDeviationM = ship.DeviationM;
        ship.NewDeviationVelocityMps = FVector3d::ZeroVector;
        ship.Shots = 0;
        ship.bRespawned = 0;
        if (ship.bAlive == 0)
        {
            const SOLNpcAi::FRespawnStep respawn = SOLNpcAi::StepRespawn(
                SOLNpcAi::FCombatState{ ship.Health, ship.RespawnS, false }, dt, shipClass.MaxHealth);
            ship.Health = static_cast<float>(respawn.State.Health);
            ship.RespawnS = static_cast<float>(respawn.State.RespawnS);
            if (!respawn.bRespawned)
            {
                return;
            }

            // Back on its rail slot with no target
            ship.bAlive = 1;
            ship.bRespawned = 1;
            ship.NewDeviationM = FVector3d::ZeroVector;
            ship.TargetNpc = INDEX_NONE;
            ship.CooldownS = 0.0f;
            FPlatformAtomics::InterlockedIncrement(&respawns);
        }
    });
    mStats.Respawns += respawns;

    // The two grids are independent, so they are built concurrently (each is still a serial build)
    ParallelFor(2, [this, dt](const int32 gridIndex)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(FSOLNpcCombat::BuildGrid);
        for (int32 slot = 0; slot < mShips.Num(); ++slot)
        {
            const FSOLNpcAiShip& ship = mShips[slot];
            if (ship.bAlive == 0 || ship.bRespawned != 0)
            {
                continue;
            }
            if (gridIndex == 0)
            {
                mTargetGrid.Insert(slot, ship.PositionM, 0.0);
                continue;
            }
            const FVector3d halfStepM = ship.VelocityMps * (0.5 * dt);
            mShipGrid.Insert(slot, ship.PositionM - halfStepM,
                SOLNpcAi::GetClass(ship.ClassIndex).HitRadiusM + halfStepM.Size());
        }
    });

    // Flight and gun decisions, in parallel (each task writes only its own ships)
    ParallelForWithExistingTaskContext(MakeArrayView(mScratch), mShips.Num(), NPC_AI_MIN_BATCH,
        [this, &inputs](FScratch& scratch, const int32 slot)
        {
            StepShip(scratch, slot, inputs);
        });
    const double aiS = FPlatformTime::Seconds();

    // Bolts in flight, then this frame's shots
    StepBolts(dt);
    const double boltS = FPlatformTime::Seconds();
    if (inputs.bCombatEnabled)
    {
        FireShots(inputs);
    }
    const double fireS = FPlatformTime::Seconds();

    // Statistics
    for (const FSOLNpcAiShip& ship : mShips)
    {
        mStats.Alive += ship.bAlive;
        mStats.Shots += ship.Shots;
    }
    mStats.LiveBolts = mBoltPositionsM.Num();
    for (const uint8 bReal : mBoltReal)
    {
        mStats.RealBolts += bReal;
    }
    mStats.AiMs = (aiS - startS) * 1000.0;
    mStats.BoltMs = (boltS - aiS) * 1000.0;
    mStats.FireMs = (fireS - boltS) * 1000.0;
}

//////////////////////////////////////////////////////////////////////////
// Flies one ship and decides its shots (parallel; reads the other ships, writes only this one)
void FSOLNpcCombat::StepShip(FScratch& scratch, const int32 slot, const FSOLNpcCombatInputs& inputs)
{
    FSOLNpcAiShip& ship = mShips[slot];
    if (ship.bAlive == 0 || ship.bRespawned != 0)
    {
        return;
    }
    const SOLNpcAi::FClassParams& shipClass = SOLNpcAi::GetClass(ship.ClassIndex);
    const double dt = inputs.DeltaS;
    const double keepRangeM = shipClass.EngageRangeM * SOL::NPC_AI_KEEP_RANGE_SCALE;

    // Target: keep a live one inside the keep range, else pick the nearest enemy in the engage range
    const int32 currentSlot = mSlotOfNpc.IsValidIndex(ship.TargetNpc) ? mSlotOfNpc[ship.TargetNpc] : INDEX_NONE;
    const bool bCurrentValid = mShips.IsValidIndex(currentSlot) && mShips[currentSlot].bAlive != 0
        && mShips[currentSlot].bRespawned == 0
        && (mShips[currentSlot].PositionM - ship.PositionM).SizeSquared() < FMath::Square(keepRangeM);
    // (SelectTarget keeps a valid current target, so only a ship without one searches: first a near ring, which
    // already holds the nearest enemy when it finds one, then the whole engage range)
    // A ship that just lost its target searches at once; one that had none searches every Nth frame, staggered by
    // index (SDD 11 Amendment 6: idle ships in sparse battles otherwise query ~125 grid cells every frame)
    const bool bSearchNow = ship.TargetNpc != INDEX_NONE
        || (static_cast<uint32>(ship.NpcIndex) + mFrame) % SOL::NPC_AI_IDLE_SEARCH_INTERVAL_FRAMES == 0;
    if (!bCurrentValid)
    {
        ship.TargetNpc = INDEX_NONE;
    }
    if (!bCurrentValid && bSearchNow)
    {
        for (const double searchRangeM : { FMath::Min(SOL::NPC_AI_NEAR_TARGET_SEARCH_M, shipClass.EngageRangeM),
            shipClass.EngageRangeM })
        {
            mTargetGrid.QueryCandidates(ship.PositionM, ship.PositionM, searchRangeM, scratch.Ids);
            scratch.Candidates.Reset();
            for (const int32 id : scratch.Ids)
            {
                const FSOLNpcAiShip& other = mShips[id];
                SOLNpcAi::FTargetCandidate& candidate = scratch.Candidates.AddDefaulted_GetRef();
                candidate.Index = other.NpcIndex;
                candidate.PositionM = other.PositionM;
                candidate.Faction = other.Faction;
                candidate.bAlive = other.bAlive != 0 && other.bRespawned == 0;
            }
            ship.TargetNpc = SOLNpcAi::SelectTarget(ship.NpcIndex, ship.PositionM, ship.Faction, INDEX_NONE,
                scratch.Candidates, searchRangeM, SOL::NPC_AI_KEEP_RANGE_SCALE);
            if (ship.TargetNpc != INDEX_NONE)
            {
                break;
            }
        }
    }
    const int32 targetSlot = mSlotOfNpc.IsValidIndex(ship.TargetNpc) ? mSlotOfNpc[ship.TargetNpc] : INDEX_NONE;
    const FSOLNpcAiShip* target = mShips.IsValidIndex(targetSlot) ? &mShips[targetSlot] : nullptr;

    // Desired velocity: pursuit to the standoff (or drift back to the rail), separation, then the leash
    FVector3d desiredMps = -ship.DeviationM * SOL::NPC_AI_IDLE_RETURN_RATE;
    double distanceM = 0.0;
    if (target != nullptr)
    {
        distanceM = (target->PositionM - ship.PositionM).Size();
        desiredMps = SOLNpcAi::PursuitVelocity(ship.PositionM, target->PositionM, target->VelocityMps,
            shipClass.MaxSpeedMps, shipClass.StandoffM, distanceM / shipClass.MuzzleSpeedMps,
            StrafeAxis(inputs.Seed, ship.NpcIndex));
    }
    mShipGrid.QueryCandidates(ship.PositionM, ship.PositionM, shipClass.SeparationRadiusM, scratch.Ids);
    scratch.Neighbours.Reset();
    for (const int32 id : scratch.Ids)
    {
        scratch.Neighbours.Add(mShips[id].PositionM);
    }
    desiredMps += SOLNpcAi::SeparationVelocity(ship.PositionM, scratch.Neighbours, shipClass.SeparationRadiusM,
        shipClass.MaxSpeedMps);
    desiredMps = SOLNpcAi::LeashVelocity(desiredMps, ship.DeviationM, shipClass.LeashM, shipClass.MaxSpeedMps)
        .GetClampedToMaxSize(shipClass.MaxSpeedMps);

    // Lead aim at the target (the nose goes there near the target, and the guns fire along it)
    ship.AimDirection = target != nullptr ? SOLNpcAi::LeadAimDirection(ship.PositionM, ship.VelocityMps,
        target->PositionM, target->VelocityMps, shipClass.MuzzleSpeedMps) : FVector3d::ZeroVector;

    // 6-DOF Newtonian flight in the rail frame under the class envelope (SDD 18 section 3.7); away from the target
    // the nose follows the desired (else the current) velocity relative to the group's anchors
    FSOLShipState flight;
    flight.PositionM = ship.DeviationM;
    flight.VelocityMps = ship.DeviationVelocityMps;
    flight.Orientation = ship.Orientation;
    flight.AngularVelocityRadPerS = ship.AngularVelocityRadPerS;
    const FVector3d desiredForward = SOLNpcAi::FullShipDesiredForward(ship.AimDirection, distanceM,
        shipClass.FireRangeM, ship.RailVelocityMps + desiredMps, ship.RailVelocityMps + ship.DeviationVelocityMps,
        ship.Orientation.GetForwardVector());
    flight = SOLNpcAi::StepFullShip(flight, desiredMps, desiredForward, ship.UpHint,
        mClassEnvelopes[FMath::Clamp(ship.ClassIndex, 0, mClassEnvelopes.Num() - 1)], dt);
    ship.NewDeviationM = flight.PositionM;
    ship.NewDeviationVelocityMps = flight.VelocityMps;
    ship.Orientation = flight.Orientation;
    ship.AngularVelocityRadPerS = flight.AngularVelocityRadPerS;

    // Gun: fired only inside the cone around the nose and within range
    bool bTrigger = false;
    if (target != nullptr && inputs.bCombatEnabled)
    {
        const FVector3d noseDirection = ship.Orientation.GetForwardVector();
        bTrigger = SOLNpcAi::ShouldFire(noseDirection, ship.AimDirection, distanceM, shipClass.FireRangeM,
            shipClass.FireConeRad);

        // Shots leave along the aim only as far as the guns swivel off the nose (fixed guns miss by the nose error)
        ship.AimDirection = SOLNpcAi::GunDirection(noseDirection, ship.AimDirection, shipClass.GunGimbalRad);
    }
    const SOLFireCadence::FStep gun = SOLFireCadence::Step(ship.CooldownS, dt, shipClass.FireRatePerS, bTrigger,
        SOL::NPC_AI_MAX_SHOTS_PER_FRAME);
    ship.Shots = gun.Shots;
    ship.CooldownS = static_cast<float>(gun.CooldownS);
}

//////////////////////////////////////////////////////////////////////////
// Advances, sweeps and resolves the bolts
void FSOLNpcCombat::StepBolts(const double deltaS)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FSOLNpcCombat::StepBolts);
    const int32 boltCount = mBoltPositionsM.Num();
    mBoltHitSlot.SetNumUninitialized(boltCount, EAllowShrinking::No);
    mBoltHitPositionM.SetNumUninitialized(boltCount, EAllowShrinking::No);

    // Advance and sweep in parallel: each bolt's earliest hit on an enemy ship's moving hit sphere
    ParallelForWithExistingTaskContext(MakeArrayView(mScratch), boltCount, NPC_BOLT_MIN_BATCH,
        [this, deltaS](FScratch& scratch, const int32 bolt)
        {
            const FVector3d startM = mBoltPositionsM[bolt];
            const FVector3d endM = SOLBoltMath::AdvancePosition(startM, mBoltVelocitiesMps[bolt], deltaS);
            mBoltHitSlot[bolt] = INDEX_NONE;
            if (mBoltReal[bolt] != 0)
            {
                mShipGrid.QueryCandidates(startM, endM, 0.0, scratch.Ids);
                double earliest = 2.0;
                for (const int32 id : scratch.Ids)
                {
                    const FSOLNpcAiShip& ship = mShips[id];
                    if (ship.bAlive == 0 || ship.bRespawned != 0 || ship.Faction == mBoltFaction[bolt])
                    {
                        continue;
                    }
                    const SOLBoltMath::FSweepHit hit = SOLBoltMath::SweptSphereHitMoving(startM, endM,
                        ship.PositionM - ship.VelocityMps * deltaS, ship.PositionM,
                        SOLNpcAi::GetClass(ship.ClassIndex).HitRadiusM);
                    if (hit.bHit && hit.Time < earliest)
                    {
                        earliest = hit.Time;
                        mBoltHitSlot[bolt] = id;
                    }
                }
                if (mBoltHitSlot[bolt] != INDEX_NONE)
                {
                    mBoltHitPositionM[bolt] = startM + (endM - startM) * earliest;
                }
            }
            mBoltPositionsM[bolt] = endM;
            mBoltLifetimeS[bolt] = static_cast<float>(SOLBoltMath::AdvanceLifetime(mBoltLifetimeS[bolt], deltaS));
        });

    // Resolve hits and expiries (descending, so swap-removal never skips a bolt)
    for (int32 bolt = boltCount - 1; bolt >= 0; --bolt)
    {
        const int32 hitSlot = mBoltHitSlot[bolt];
        if (hitSlot != INDEX_NONE)
        {
            DamageShip(hitSlot, mBoltDamage[bolt], mBoltHitPositionM[bolt], mBoltVelocitiesMps[bolt]);
            RemoveBoltAtSwap(bolt);
        }
        else if (SOLBoltMath::IsExpired(mBoltLifetimeS[bolt]))
        {
            RemoveBoltAtSwap(bolt);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Turns this frame's shots into bolts or statistical hits
void FSOLNpcCombat::FireShots(const FSOLNpcCombatInputs& inputs)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FSOLNpcCombat::FireShots);
    for (int32 slot = 0; slot < mShips.Num(); ++slot)
    {
        const FSOLNpcAiShip& ship = mShips[slot];
        const int32 targetSlot = mSlotOfNpc.IsValidIndex(ship.TargetNpc) ? mSlotOfNpc[ship.TargetNpc] : INDEX_NONE;
        if (ship.Shots <= 0 || ship.bAlive == 0 || !mShips.IsValidIndex(targetSlot))
        {
            continue;
        }
        const SOLNpcAi::FClassParams& shipClass = SOLNpcAi::GetClass(ship.ClassIndex);
        const FVector3d muzzleM = ship.PositionM + ship.AimDirection * (shipClass.HitRadiusM * NPC_MUZZLE_OFFSET_RADII);
        const FVector3d boltVelocityMps = SOLBoltMath::MuzzleVelocity(ship.VelocityMps, ship.AimDirection,
            shipClass.MuzzleSpeedMps);
        const bool bReal = SOLNpcAi::UsesRealBolts(inputs.Model, ship.CameraDistanceM,
            SOL::NPC_COMBAT_VISUAL_RADIUS_M);
        const bool bNearCamera = ship.CameraDistanceM < SOL::NPC_COMBAT_VISUAL_RADIUS_M;
        for (int32 shot = 0; shot < ship.Shots; ++shot)
        {
            if (bReal)
            {
                AddBolt(muzzleM, boltVelocityMps, shipClass.BoltLifetimeS, static_cast<float>(shipClass.BoltDamage),
                    ship.Faction, true);
                continue;
            }

            // Statistical: roll the hit now; a cosmetic tracer near the camera flies to where the target was
            ++mStats.StatisticalShots;
            const FSOLNpcAiShip& target = mShips[targetSlot];
            const FVector3d relativeM = target.PositionM - ship.PositionM;
            const double distanceM = relativeM.Size();
            const double probability = SOLNpcAi::StatisticalHitProbability(distanceM,
                SOLNpcAi::TransverseSpeedMps(relativeM, target.VelocityMps - ship.VelocityMps),
                SOLNpcAi::GetClass(target.ClassIndex).HitRadiusM, shipClass.AimSpreadRad, shipClass.MuzzleSpeedMps,
                shipClass.FireRangeM);
            const double unit = SOLNpcScale::HashUnit(inputs.Seed ^ mFrame,
                static_cast<uint32>(ship.NpcIndex) * SOL::NPC_AI_MAX_SHOTS_PER_FRAME + shot, NPC_SHOT_HASH_STREAM);
            if (SOLNpcAi::RollHit(unit, probability))
            {
                DamageShip(targetSlot, shipClass.BoltDamage, target.PositionM, ship.AimDirection);
            }
            if (bNearCamera)
            {
                AddBolt(muzzleM, boltVelocityMps, FMath::Min(shipClass.BoltLifetimeS,
                    distanceM / shipClass.MuzzleSpeedMps), 0.0f, ship.Faction, false);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Damages a ship and records the hit (and the kill)
void FSOLNpcCombat::DamageShip(const int32 slot, const double damage, const FVector3d& positionM,
    const FVector3d& directionM)
{
    FSOLNpcAiShip& ship = mShips[slot];
    const SOLNpcAi::FClassParams& shipClass = SOLNpcAi::GetClass(ship.ClassIndex);
    const SOLNpcAi::FDamageResult result = SOLNpcAi::ApplyDamage(
        SOLNpcAi::FCombatState{ ship.Health, ship.RespawnS, ship.bAlive != 0 }, damage, shipClass.RespawnDelayS);
    if (!ship.bAlive)
    {
        return;
    }
    ++mStats.Hits;
    ship.Health = static_cast<float>(result.State.Health);
    ship.RespawnS = static_cast<float>(result.State.RespawnS);
    const bool bNearCamera = ship.CameraDistanceM < SOL::NPC_COMBAT_VISUAL_RADIUS_M;
    if (bNearCamera)
    {
        AddEvent(ESOLNpcCombatEventType::Hit, positionM, directionM);
    }
    if (result.bKilled)
    {
        ++mStats.Kills;
        ship.bAlive = 0;
        ship.TargetNpc = INDEX_NONE;
        ship.Shots = 0;
        if (bNearCamera)
        {
            AddEvent(ESOLNpcCombatEventType::Killed, ship.PositionM, FVector3d::ZeroVector);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Appends a bolt to the pool (dropped and counted when full)
void FSOLNpcCombat::AddBolt(const FVector3d& positionM, const FVector3d& velocityMps, const double lifetimeS,
    const float damage, const uint8 faction, const bool bReal)
{
    if (mBoltPositionsM.Num() >= SOL::NPC_MAX_BOLTS)
    {
        ++mStats.DroppedBolts;
        return;
    }
    mBoltPositionsM.Add(positionM);
    mBoltVelocitiesMps.Add(velocityMps);
    mBoltLifetimeS.Add(static_cast<float>(lifetimeS));
    mBoltDamage.Add(damage);
    mBoltFaction.Add(faction);
    mBoltReal.Add(bReal ? 1 : 0);
}

//////////////////////////////////////////////////////////////////////////
// Removes the bolt at an index (swap with the last)
void FSOLNpcCombat::RemoveBoltAtSwap(const int32 bolt)
{
    mBoltPositionsM.RemoveAtSwap(bolt, EAllowShrinking::No);
    mBoltVelocitiesMps.RemoveAtSwap(bolt, EAllowShrinking::No);
    mBoltLifetimeS.RemoveAtSwap(bolt, EAllowShrinking::No);
    mBoltDamage.RemoveAtSwap(bolt, EAllowShrinking::No);
    mBoltFaction.RemoveAtSwap(bolt, EAllowShrinking::No);
    mBoltReal.RemoveAtSwap(bolt, EAllowShrinking::No);
}

//////////////////////////////////////////////////////////////////////////
// Appends an event (dropped beyond the per-frame cap)
void FSOLNpcCombat::AddEvent(const ESOLNpcCombatEventType type, const FVector3d& positionM,
    const FVector3d& directionM)
{
    if (mEvents.Num() < NPC_MAX_EVENTS_PER_FRAME)
    {
        mEvents.Add(FSOLNpcCombatEvent{ type, positionM, directionM });
    }
}
