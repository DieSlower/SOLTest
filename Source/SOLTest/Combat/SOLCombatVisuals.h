/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Combat/SOLCombatTypes.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SOLCombatVisuals.generated.h"

class UInstancedStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USOLAnchorSubsystem;
class USOLCombatSubsystem;
class USOLShipSubsystem;
struct FSOLRenderOrigin;

/**
 * Presentation of USOLCombatSubsystem (SDD 7, Part 7c). Spawned once by ASOLGameMode; redraws on the subsystem's
 * OnCombatUpdated, i.e. after the frame's combat update, so it always shows final positions and events.
 *
 * - Bolts: ONE GPU Niagara component (NS_SOLBolts) holding a fixed pool of COMBAT_MAX_BOLTS particles spawned once.
 *   Each frame the packed bolt positions (render cm) and directions are copied into two reused float3 scratch arrays
 *   and handed to the system's array data interfaces; particle i reads entry i, and particles past the live count are
 *   hidden by a visibility tag. The component sits at the Unreal origin with an identity transform, so its local space
 *   is render space. Streaks are aligned with each bolt's velocity relative to the player ship (the apparent motion).
 * - Targets: one instanced static mesh with one instance per target slot (COMBAT_MAX_TARGETS), per-instance custom
 *   data (shield fraction, health fraction, hit flash) for M_SOLTarget. Only the slot range that is, or was last frame,
 *   in use is re-placed; inactive slots are scaled to zero, and custom data is written only where it changed.
 * - One-shot effects: fixed pools of Niagara components per kind (impact, shield break, spark, explosion), the oldest
 *   reused for each new event; an effect rides with what it hit (the event's frame velocity) until its duration ends.
 *
 * Every universe position is converted with the frame's render-origin snapshot, never stored in render space between
 * frames, so origin rebases cannot strand anything. Every asset is optional: a missing one disables only its part.
 */
UCLASS()
class SOLTEST_API ASOLCombatVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root, the bolt Niagara component and the target instanced mesh
    ASOLCombatVisuals();

protected:

    // Loads the assets, sizes the pools and scratch arrays once, and subscribes to the combat update
    virtual void BeginPlay() override;

    // Unsubscribes from the combat update
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // One pooled one-shot effect component and where it is anchored in the universe
    struct FSOLEffectSlot
    {
        TObjectPtr<UNiagaraComponent> Component;
        FVector3d StartPositionM = FVector3d::ZeroVector;  // Universe position at the event
        FVector3d FrameVelocityMps = FVector3d::ZeroVector; // Universe velocity it rides with
        double AgeS = 0.0;
        bool bIsLive = false;
    };

    // A fixed pool of one effect kind
    struct FSOLEffectPool
    {
        TArray<FSOLEffectSlot> Slots;
        int32 NextSlot = 0;          // Oldest started slot: the next one reused
        double DurationS = 0.0;
        int32 StartedThisFrame = 0;  // New effects this frame; capped at the pool size
    };

    // Redraws bolts and targets and starts and moves the one-shot effects; bound to OnCombatUpdated
    void HandleCombatUpdated();

    // Copies the bolt arrays into the bolt Niagara system
    void UpdateBolts(const FSOLRenderOrigin& origin);

    // Re-places the in-use target slots and writes changed per-instance custom data
    void UpdateTargets(const FSOLRenderOrigin& origin);

    // Starts the one-shot effects of this frame's events
    void StartEventEffects();

    // Moves every live pooled effect with its frame and retires the ones past their duration
    void UpdateEffects(const FSOLRenderOrigin& origin, double deltaSeconds);

    // Creates a pool of components for one effect system (nothing if the system is missing)
    void CreatePool(FSOLEffectPool& pool, UNiagaraSystem* system, int32 size, double durationS, const TCHAR* name);

    // Starts one effect from a pool at a universe position; the direction (ecliptic) and tint are optional parameters
    void StartEffect(FSOLEffectPool& pool, const FSOLCombatEvent& event, const FVector3d& directionEcliptic,
        const FLinearColor* tint);

    // Returns the player ship's universe velocity (ecliptic), or zero without a ship
    FVector3d GetObserverVelocityMps() const;

    /** Scene root at the Unreal origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<USceneComponent> SceneRoot;

    /** GPU bolt renderer: the NS_SOLBolts system fed from the packed bolt arrays. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UNiagaraComponent> BoltNiagara;

    /** Targets: one instance per target slot. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UInstancedStaticMeshComponent> TargetMesh;

    /** Every pooled one-shot effect component, held here so the pools' raw slots stay referenced for GC. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UNiagaraComponent>> EffectComponents;

    UPROPERTY(Transient)
    TObjectPtr<USOLCombatSubsystem> Combat;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    // Bolts
    TArray<FVector3f> mBoltPositionsCm;        // Scratch, reserved to COMBAT_MAX_BOLTS once
    TArray<FVector3f> mBoltVelocitiesCmps;     // Scratch, reserved to COMBAT_MAX_BOLTS once
    int32 mLastBoltCount = 0;
    bool mIsBoltRendererEnabled = false;

    // Targets
    TArray<FTransform> mTargetTransforms;      // Scratch, reserved to the slot count once
    TArray<float> mTargetCustomData;           // Last written custom data, COMBAT_TARGET_CUSTOM_DATA_FLOATS per slot
    int32 mTargetRangeEnd = 0;                 // Slots [0, end) were placed last frame (the rest are already hidden)
    bool mIsTargetRendererEnabled = false;

    // One-shot effects
    FSOLEffectPool mImpactPool;
    FSOLEffectPool mShieldBreakPool;
    FSOLEffectPool mSparkPool;
    FSOLEffectPool mExplosionPool;

    FDelegateHandle mCombatUpdatedHandle;
};
