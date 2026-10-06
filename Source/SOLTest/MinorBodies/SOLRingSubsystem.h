/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "MinorBodies/SOLPlanetRing.h"
#include "MinorBodies/SOLRingPatch.h"

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLRingSubsystem.generated.h"

class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMinorBodySubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
struct FMassEntityManager;

/**
 * Owns the near-field ring-rock Mass entities for all four ringed planets (SDD 6 Amendments 6-8, Appendix D): a
 * fixed-size pool per ring, reserved once at world begin-play, organized as contiguous GROUPS of
 * SOLRingPatch::ROCKS_PER_CELL entities (one group per cell the pool can represent at once - a cell holds many
 * rocks, so a "pool slot" in SOLRingPatch::ReassignPoolSlots's sense is one whole group, not one entity). EVERY
 * frame, from USOLShipSubsystem's OnShipsStepped (so the ship's position is this frame's, SDD 6 Amendment 10), it
 * checks the ship's position against each ring's volume (SOLRingPatch::ComputeActiveCell), recomputes the desired cell window (SOLRingPatch::ActiveCellWindow) and asks
 * SOLRingPatch::ReassignPoolSlots which groups need to change - not just when the active cell itself changes: per
 * SDD 6 Amendment 9, neighboring radial bands' co-rotating frames drift against each other continuously, so the
 * window's correct membership can shift even while the player's own active cell stays the same. Recomputing every
 * frame is cheap (ReassignPoolSlots leaves an unchanged group alone, and both functions are a few dozen cheap
 * operations over a ~25-cell window), so there is no reason to gate it behind a cell-change check. Reassigned
 * groups get their Mass fragment data rewritten in place (SOLRingPatch::GenerateCellRocks) - never creating or
 * destroying Mass entities at runtime, per CLAUDE.md's "pooled, not spawn-destroy" guidance.
 *
 * Does NOT run its own orbit-processor pass: ring-rock entities share FSOLMinorBodyOrbitFragment/
 * FSOLMinorBodyStateFragment with the asteroid belt, and USOLMinorBodySubsystem's existing per-frame run (also
 * bound to OnBodiesUpdated) already places every entity matching those fragments, belt and ring alike, since Mass
 * queries match by fragment composition, not by which subsystem created an entity - running a second processor
 * instance here would reprocess the same global entity set a second time, every frame, for nothing. This
 * subsystem therefore only manages entity DATA; it declares an explicit dependency on USOLMinorBodySubsystem
 * purely to document this relationship, not because it calls into it.
 *
 * No rendering yet (5e-iii adds ASOLRingVisuals); a group with no assigned cell (the player isn't in that ring, or
 * the entity has never been assigned since spawn) keeps whatever orbit it last had - harmless today with nothing
 * reading it, and the eventual renderer is expected to hide an inactive ring via SOLRingPatch::ComputeNearFieldAlpha
 * rather than this subsystem clearing data nothing observes.
 */
UCLASS()
class SOLTEST_API USOLRingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Declares its dependencies and hooks the per-frame cell-tracking update
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Destroys the ring-rock entities while the entity manager is still alive (before any subsystem deinitializes)
    virtual void PreDeinitialize() override;

    // Unhooks from the ship step and releases the Mass objects
    virtual void Deinitialize() override;

    // Spawns every ring's entity pool
    virtual void OnWorldBeginPlay(UWorld& world) override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // One ringed planet's static data (built once at spawn) and live pool state
    struct FSOLRingState
    {
        FSOLPlanetRingDef RingDef;
        int32 PlanetIndex = INDEX_NONE;
        FVector3d RingNormal = FVector3d::ZAxisVector; // Host pole direction, cached on the first UpdateRing (constant)
        bool bIsRingNormalCached = false;           // Set once RingNormal holds the real pole (not at BuildRingState)
        int32 AssignedGroupCount = 0;               // Number of set GroupCells entries, for UpdateRing's idle early-out
        TArray<double> MoonOrbitRadiiM;             // Shepherd moons' semi-major axes feeding AllGapBandsM, cached once
        TArray<double> MoonGapHalfWidthsM;          // Parallel to MoonOrbitRadiiM
        TArray<FSOLRadiusBandM> GapBands;           // AllGapBandsM's result plus the two outside-the-ring bands,
                                                     // cached once (gap geometry never changes)
        TArray<FMassEntityHandle> Entities;         // The fixed-size pool, grouped: group g is
                                                     // Entities[g*ROCKS_PER_CELL .. (g+1)*ROCKS_PER_CELL)
        TArray<TOptional<FSOLRingCell>> GroupCells; // One entry per group: which cell it currently represents
        TOptional<FSOLRingCell> ActiveCell;         // The ship's last-known active cell in this ring, if any
    };

    // Builds every ring's static data and batch-creates its entity pool, each entity starting on a safe placeholder
    // orbit (circular at the ring's own OuterRadiusM) so it is never left with a divide-by-zero zero-elements orbit
    void SpawnRingPools();

    // Builds one ring's static FSOLRingState (gap bands, host planet lookup) before any entities exist
    FSOLRingState BuildRingState(const FSOLPlanetRingDef& ringDef) const;

    // Checks the ship's position against every ring and reassigns groups where the desired cell window changed;
    // bound to the ship subsystem's OnShipsStepped
    void UpdateRings(float realDeltaSeconds);

    // Checks one ring against the ship's current position and reassigns whichever groups the desired window no
    // longer covers (every call, not just when the active cell changes - see the class comment, Amendment 9)
    void UpdateRing(FSOLRingState& ring, const FVector3d& shipPositionM, double secondsSinceJ2000);

    // Regenerates one group's entities for newCell (or leaves them on their stale orbit if newCell is unset)
    void AssignGroup(FSOLRingState& ring, int32 groupIndex, const TOptional<FSOLRingCell>& newCell,
        const FVector3d& ringNormal, double secondsSinceJ2000);

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ship;                       // Kept for Deinitialize's OnShipsStepped unbind

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLMinorBodySubsystem> MinorBodySubsystem;    // Dependency only - see class comment

    TSharedPtr<FMassEntityManager> mEntityManager;
    TArray<FSOLRingState> mRings;
    FDelegateHandle mShipsSteppedHandle;
};
