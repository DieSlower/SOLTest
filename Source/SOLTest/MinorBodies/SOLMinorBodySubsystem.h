/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "MinorBodies/SOLMinorBodyFragments.h"

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLMinorBodySubsystem.generated.h"

class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMinorBodyOrbitProcessor;
class USOLSimClockSubsystem;
struct FMassCommandBuffer;
struct FMassEntityManager;

/**
 * Owns the minor-body Mass entities (SDD 6 Appendix B; the asteroid belt for now) and places them every frame. At world
 * begin-play it generates the belt (SOLAsteroidBelt::GenerateBelt with SOL::ASTEROID_BELT_SEED) and batch-creates one
 * entity per asteroid, one batch per rendering variant (real named asteroids, then the procedural family fill), so each
 * variant's InstanceIndex runs 0..N-1 and maps straight onto its ISM component's instances.
 *
 * Each frame, from USOLAnchorSubsystem's OnBodiesUpdated (after the clock and the bodies advance, before the anchor
 * update and OnUniverseUpdated), it runs USOLMinorBodyOrbitProcessor with the registry's raw ecliptic body positions
 * and the clock's time, so ASOLAsteroidBeltVisuals reads this frame's positions from its OnUniverseUpdated handler.
 */
UCLASS()
class SOLTEST_API USOLMinorBodySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Declares its dependencies, prepares the orbit processor and hooks the per-frame body update
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Destroys the minor-body entities while the entity manager is still alive (before any subsystem deinitializes)
    virtual void PreDeinitialize() override;

    // Unhooks from the body update and releases the Mass objects
    virtual void Deinitialize() override;

    // Spawns the asteroid belt and places it at the current time
    virtual void OnWorldBeginPlay(UWorld& world) override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Returns how many entities were spawned for a rendering variant (SOLMinorBodyVariant), 0 for an unknown one
    int32 GetVariantCount(int32 variantIndex) const;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Generates the belt and batch-creates its entities, one batch per rendering variant
    void SpawnAsteroidBelt();

    // Places every minor body at the clock's current time; bound to the anchor subsystem's OnBodiesUpdated
    void UpdateOrbits(float realDeltaSeconds);

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLMinorBodyOrbitProcessor> OrbitProcessor;

    TSharedPtr<FMassEntityManager> mEntityManager;             // The world's default Mass entity manager
    TSharedPtr<FMassCommandBuffer> mCommandBuffer;             // Reused by every run so a step allocates none
    TArray<FMassEntityHandle> mEntities;                       // Every spawned minor body, for teardown
    int32 mVariantCounts[SOLMinorBodyVariant::COUNT] = {};     // Entities spawned per rendering variant
    FDelegateHandle mBodiesUpdatedHandle;                      // Binding to the anchor subsystem's OnBodiesUpdated
};
