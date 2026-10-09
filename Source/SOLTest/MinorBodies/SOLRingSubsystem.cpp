/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingSubsystem.h"

#include "Game/SOLGameMode.h"
#include "MinorBodies/SOLMinorBodyFragments.h"
#include "MinorBodies/SOLMinorBodySubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Ship/SOLShipSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Math/NumericLimits.h"

namespace
{
    // Returns the host planet's ring-plane unit normal: its pole direction, unaffected by spin phase (a rotation about
    // the pole leaves the pole itself fixed) and constant after spawn, so UpdateRing computes it once per ring
    FVector3d RingPlaneNormal(const FSOLBodyRegistry& registry, const int32 planetIndex)
    {
        return registry.GetOrientation(planetIndex).RotateVector(FVector3d(0.0, 0.0, 1.0));
    }

    // Returns a safe, valid, boring circular orbit at the ring's own outer edge: never NaN/Inf (SOLKepler::
    // ElementsToState would divide by zero on a default-constructed, all-zero FSOLSecularElements), used for every
    // pool entity's data until it is first assigned a real cell
    FSOLSecularElements RingSafeDefaultElements(const FSOLPlanetRingDef& ringDef, const double planetGM)
    {
        FSOLSecularElements elements;
        elements.A0AU = ringDef.OuterRadiusM / SOL::AU_M;
        elements.LDotDegPerCy = SOLKepler::MeanMotionDegPerCy(ringDef.OuterRadiusM, planetGM);
        return elements;
    }
}

//////////////////////////////////////////////////////////////////////////
// Declares its dependencies and hooks the per-frame cell-tracking update
void USOLRingSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    UMassEntitySubsystem* massEntities = collection.InitializeDependency<UMassEntitySubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    SimClock = collection.InitializeDependency<USOLSimClockSubsystem>();
    Anchor = collection.InitializeDependency<USOLAnchorSubsystem>();
    Ship = collection.InitializeDependency<USOLShipSubsystem>();
    MinorBodySubsystem = collection.InitializeDependency<USOLMinorBodySubsystem>();
    Super::Initialize(collection);

    if (massEntities == nullptr || BodyRegistry == nullptr || SimClock == nullptr || Anchor == nullptr
        || Ship == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("RingSubsystem %s: Mass, ship or universe subsystems missing"), *GetName());
        return;
    }
    mEntityManager = massEntities->GetMutableEntityManager().AsShared();
    // OnShipsStepped fires once the ship and observer hold this frame's position (SDD 6 Amendment 10), so the
    // observer position UpdateRings reads is current, unlike OnBodiesUpdated's unordered listener list
    mShipsSteppedHandle = Ship->OnShipsStepped().AddUObject(this, &USOLRingSubsystem::UpdateRings);
}

//////////////////////////////////////////////////////////////////////////
// Destroys the ring-rock entities while the entity manager is still alive (before any subsystem deinitializes)
void USOLRingSubsystem::PreDeinitialize()
{
    // Deinitialize is too late: UMassEntitySubsystem may already have torn the entity manager's storage down by then
    // (subsystem deinitialization order is not dependency-ordered), and touching it there asserts - same hazard
    // documented on USOLMinorBodySubsystem::PreDeinitialize and (once latent) USOLShipSubsystem's
    if (mEntityManager.IsValid())
    {
        for (FSOLRingState& ring : mRings)
        {
            if (!ring.Entities.IsEmpty())
            {
                mEntityManager->BatchDestroyEntities(ring.Entities);
                ring.Entities.Empty();
            }
        }
    }
    Super::PreDeinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Unhooks from the ship step and releases the Mass objects
void USOLRingSubsystem::Deinitialize()
{
    if (Ship != nullptr)
    {
        Ship->OnShipsStepped().Remove(mShipsSteppedHandle);
    }
    mShipsSteppedHandle.Reset();
    mRings.Empty();
    mEntityManager.Reset();
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Spawns every ring's entity pool
void USOLRingSubsystem::OnWorldBeginPlay(UWorld& world)
{
    Super::OnWorldBeginPlay(world);
    SpawnRingPools();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLRingSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLRingSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Builds every ring's static data and batch-creates its entity pool, each entity starting on a safe placeholder
// orbit so it is never left with a divide-by-zero zero-elements orbit before its first real assignment
void USOLRingSubsystem::SpawnRingPools()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLRingSubsystem::SpawnRingPools);
    if (!mEntityManager.IsValid() || !mRings.IsEmpty())
    {
        return;
    }

    const int32 groupsPerRing = (2 * SOL::RING_PATCH_WINDOW_RADIAL_RADIUS + 1)
        * (2 * SOL::RING_PATCH_WINDOW_ANGULAR_RADIUS + 1);
    const int32 entitiesPerRing = SOLRingPatch::PoolEntityCountPerRing();
    check(entitiesPerRing == groupsPerRing * SOLRingPatch::ROCKS_PER_CELL);

    // Same base fragment composition the belt uses (so the shared orbit processor matches both), plus
    // FSOLRingRockTag so ASOLRingVisuals's per-ring query can match only ring-pool entities at the archetype level
    // rather than iterating every minor body (belt included) and filtering per-entity (SDD 6 Amendment 11's
    // follow-up, 5e-iii)
    const UScriptStruct* const elements[] = {
        FSOLMinorBodyOrbitFragment::StaticStruct(),
        FSOLMinorBodyRenderFragment::StaticStruct(),
        FSOLMinorBodyStateFragment::StaticStruct(),
        FSOLRingRockTag::StaticStruct(),
    };
    const FMassArchetypeHandle archetype = mEntityManager->CreateArchetype(MakeArrayView(elements));

    const TArray<FSOLPlanetRingDef> rings = SOLPlanetRing::RealRings();
    mRings.Reserve(rings.Num());
    for (const FSOLPlanetRingDef& ringDef : rings)
    {
        FSOLRingState ring = BuildRingState(ringDef);
        if (ring.PlanetIndex == INDEX_NONE)
        {
            UE_LOG(LogSOL, Error, TEXT("RingSubsystem %s: host planet '%s' not in the registry; ring not spawned"),
                *GetName(), *ringDef.PlanetName.ToString());
            continue;
        }
        const double planetGM = BodyRegistry->GetRegistry().GetGM(ring.PlanetIndex);
        const FSOLSecularElements safeDefault = RingSafeDefaultElements(ringDef, planetGM);

        // No FSOLMinorBodyAppearanceFragment: ASOLAsteroidBeltVisuals's Mass query matches ANY entity with the
        // Render+State fragments and a const-shared Appearance fragment present, regardless of which subsystem
        // created the entity or its ParentBodyIndex - giving ring rocks an appearance fragment (any variant value)
        // would make the belt's already-shipped visuals actor try to render them into its own ISM arrays using the
        // ring pool's own InstanceIndex values, corrupting both populations' rendering. USOLMinorBodyOrbitProcessor's
        // query only requires Orbit+State, so omitting Appearance still lets the shared processor place these
        // entities correctly; a renderer-distinguishing mechanism (a Mass tag, most likely) is 5e-iii's job, once a
        // real ASOLRingVisuals exists to need one
        const FMassArchetypeSharedFragmentValues sharedValues;

        ring.Entities.Reserve(entitiesPerRing);
        const TSharedRef<FMassEntityManager::FEntityCreationContext> creation =
            mEntityManager->BatchCreateEntities(archetype, sharedValues, entitiesPerRing, ring.Entities);
        for (int32 instance = 0; instance < entitiesPerRing; ++instance)
        {
            const FMassEntityHandle entity = ring.Entities[instance];
            FSOLMinorBodyOrbitFragment& orbit = mEntityManager->GetFragmentDataChecked<FSOLMinorBodyOrbitFragment>(entity);
            orbit.Elements = safeDefault;
            orbit.ParentBodyIndex = ring.PlanetIndex;
            FSOLMinorBodyRenderFragment& render = mEntityManager->GetFragmentDataChecked<FSOLMinorBodyRenderFragment>(entity);
            render.RadiusM = 1.0;
            render.InstanceIndex = instance;
            render.bActive = false; // Never assigned a real cell yet; the fragment's own default is true (correct
                                     // for belt asteroids), so a ring entity needs this set explicitly at spawn
        }
        ring.GroupCells.Init(TOptional<FSOLRingCell>(), groupsPerRing);
        UE_LOG(LogSOL, Log, TEXT("RingSubsystem %s: %s ring pool spawned, %d entities in %d groups"), *GetName(),
            *ringDef.PlanetName.ToString(), entitiesPerRing, groupsPerRing);
        mRings.Add(MoveTemp(ring));
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds one ring's static FSOLRingState (gap bands, host planet lookup) before any entities exist
USOLRingSubsystem::FSOLRingState USOLRingSubsystem::BuildRingState(const FSOLPlanetRingDef& ringDef) const
{
    FSOLRingState state;
    state.RingDef = ringDef;
    state.PlanetIndex = BodyRegistry->GetRegistry().FindByName(ringDef.PlanetName);
    if (state.PlanetIndex == INDEX_NONE)
    {
        return state;
    }

    // Saturn's shepherd moons are the only registered moons orbiting within any ring (SDD 6 Amendment 4); every
    // other ring's gap list comes from KnownResonanceGapBandsM alone (none are currently known for the other three)
    if (ringDef.PlanetName == FName(SOL::BodyNames::SATURN))
    {
        const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
        const int32 panIndex = registry.FindByName(FName(SOL::BodyNames::PAN));
        const int32 daphnisIndex = registry.FindByName(FName(SOL::BodyNames::DAPHNIS));
        if (panIndex != INDEX_NONE)
        {
            state.MoonOrbitRadiiM.Add(registry.GetElements(panIndex).A0AU * SOL::AU_M);
            state.MoonGapHalfWidthsM.Add(SOLPlanetRing::SATURN_PAN_GAP_HALF_WIDTH_M);
        }
        if (daphnisIndex != INDEX_NONE)
        {
            state.MoonOrbitRadiiM.Add(registry.GetElements(daphnisIndex).A0AU * SOL::AU_M);
            state.MoonGapHalfWidthsM.Add(SOLPlanetRing::SATURN_DAPHNIS_GAP_HALF_WIDTH_M);
        }
    }
    state.GapBands = SOLPlanetRing::AllGapBandsM(ringDef, state.MoonOrbitRadiiM, state.MoonGapHalfWidthsM);

    // Everything outside the ring's physical extent counts as gap, so cells the activation margin admits past either
    // edge are thinned (or emptied) by GenerateCellRocks's existing gap-fraction math (SDD 6 Amendment 10)
    state.GapBands.Add(FSOLRadiusBandM{0.0, ringDef.InnerRadiusM});
    state.GapBands.Add(FSOLRadiusBandM{ringDef.OuterRadiusM, TNumericLimits<double>::Max()});
    return state;
}

//////////////////////////////////////////////////////////////////////////
// Checks the ship's position against every ring and reassigns groups where the desired cell window changed
void USOLRingSubsystem::UpdateRings(float /*realDeltaSeconds*/)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLRingSubsystem::UpdateRings);
    if (mRings.IsEmpty())
    {
        return;
    }
    const FVector3d shipPositionM = Anchor->GetObserverPositionM();
    const double secondsSinceJ2000 = SimClock->GetClock().GetSecondsSinceJ2000();
    for (FSOLRingState& ring : mRings)
    {
        UpdateRing(ring, shipPositionM, secondsSinceJ2000);
    }
}

//////////////////////////////////////////////////////////////////////////
// Checks one ring against the ship's current position and reassigns whichever groups the desired window no longer
// covers (every call, not just when the active cell changes - the window itself can drift, see SDD 6 Amendment 9)
void USOLRingSubsystem::UpdateRing(FSOLRingState& ring, const FVector3d& shipPositionM, const double secondsSinceJ2000)
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const FVector3d planetPositionM = registry.GetPositionM(ring.PlanetIndex);
    const double planetGM = registry.GetGM(ring.PlanetIndex);

    // Cached on the first update rather than in BuildRingState, which runs before the registry's first Update()
    if (!ring.bIsRingNormalCached)
    {
        ring.RingNormal = RingPlaneNormal(registry, ring.PlanetIndex);
        ring.bIsRingNormalCached = true;
    }
    const FVector3d& ringNormal = ring.RingNormal;

    ring.ActiveCell = SOLRingPatch::ComputeActiveCell(shipPositionM, planetPositionM, ringNormal, ring.RingDef,
        SOLRingPatch::ActivationMarginM(), SOLRingPatch::HalfThicknessM(), planetGM, secondsSinceJ2000);

    // Ship outside this ring and no group holding a cell: nothing to reassign, so skip the allocating window work
    if (!ring.ActiveCell.IsSet() && ring.AssignedGroupCount == 0)
    {
        return;
    }

    const TArray<FSOLRingCell> desiredWindow = ring.ActiveCell.IsSet()
        ? SOLRingPatch::ActiveCellWindow(ring.RingDef, ring.ActiveCell.GetValue(),
            SOL::RING_PATCH_WINDOW_RADIAL_RADIUS, SOL::RING_PATCH_WINDOW_ANGULAR_RADIUS, planetGM, secondsSinceJ2000)
        : TArray<FSOLRingCell>();

    const TArray<TOptional<FSOLRingCell>> newGroupCells = SOLRingPatch::ReassignPoolSlots(ring.GroupCells, desiredWindow);
    check(newGroupCells.Num() == ring.GroupCells.Num());
    for (int32 group = 0; group < newGroupCells.Num(); ++group)
    {
        if (newGroupCells[group] == ring.GroupCells[group])
        {
            continue;
        }
        // Keep AssignedGroupCount in step with the set/unset transitions for the early-out above
        ring.AssignedGroupCount += (newGroupCells[group].IsSet() ? 1 : 0) - (ring.GroupCells[group].IsSet() ? 1 : 0);
        ring.GroupCells[group] = newGroupCells[group];
        AssignGroup(ring, group, newGroupCells[group], ringNormal, secondsSinceJ2000);
    }
}

//////////////////////////////////////////////////////////////////////////
// Regenerates one group's entities for newCell, or marks them inactive (stale orbit left untouched, but no longer
// rendered - SDD 6 Amendment 10's "no inactive marker" gap, closed by ASOLRingVisuals's render fragment) if newCell
// is unset or is entirely a gap with no rocks to show
void USOLRingSubsystem::AssignGroup(FSOLRingState& ring, const int32 groupIndex, const TOptional<FSOLRingCell>& newCell,
    const FVector3d& ringNormal, const double secondsSinceJ2000)
{
    const int32 firstEntity = groupIndex * SOLRingPatch::ROCKS_PER_CELL;
    TArray<FSOLRingRockDef> rocks;
    if (newCell.IsSet())
    {
        const double planetGM = BodyRegistry->GetRegistry().GetGM(ring.PlanetIndex);
        rocks = SOLRingPatch::GenerateCellRocks(ring.RingDef, newCell.GetValue(), ringNormal, planetGM,
            SOLRingPatch::HalfThicknessM(), ring.GapBands, SOL::RING_PATCH_SEED, secondsSinceJ2000);
    }
    if (rocks.IsEmpty())
    {
        // Idle slot (no cell) or an all-gap cell: mark every entity in the group inactive so ASOLRingVisuals skips
        // it rather than rendering a stale duplicate of wherever it was last assigned
        for (int32 slot = 0; slot < SOLRingPatch::ROCKS_PER_CELL; ++slot)
        {
            const FMassEntityHandle entity = ring.Entities[firstEntity + slot];
            mEntityManager->GetFragmentDataChecked<FSOLMinorBodyRenderFragment>(entity).bActive = false;
        }
        return;
    }

    for (int32 slot = 0; slot < SOLRingPatch::ROCKS_PER_CELL; ++slot)
    {
        const FMassEntityHandle entity = ring.Entities[firstEntity + slot];
        FSOLMinorBodyRenderFragment& render = mEntityManager->GetFragmentDataChecked<FSOLMinorBodyRenderFragment>(entity);
        if (slot >= rocks.Num())
        {
            // A gap-reduced cell has fewer rocks than the group has entities; park the excess inactive instead of
            // duplicating an already-placed rock on top of itself (bActive gives this a real representation now,
            // so the duplicate-rendering workaround this comment used to describe is no longer needed)
            render.bActive = false;
            continue;
        }
        const FSOLRingRockDef& rock = rocks[slot];
        FSOLMinorBodyOrbitFragment& orbit = mEntityManager->GetFragmentDataChecked<FSOLMinorBodyOrbitFragment>(entity);
        orbit.Elements = rock.Elements;
        render.RadiusM = rock.RadiusM;
        render.bActive = true;
        ++render.Generation; // A new rock: readers must not treat the jump from the old one as motion
    }
}
