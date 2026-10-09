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
 * - Bolts: ONE CPU-simulated Niagara component (NS_SOLBolts; SDD 7 Amendment 2 says why not GPU) holding a fixed pool
 *   of COMBAT_BOLT_RENDER_CAP particles spawned once. Each frame the packed bolt positions (render cm) and directions
 *   are copied into two reused float3 scratch arrays and handed to the system's array data interfaces; particle i
 *   reads entry i, and particles past the live count are hidden by a visibility tag. While no bolt is live the
 *   component is paused and hidden, so its CPU sim costs nothing. The component sits at the Unreal origin with an
 *   identity transform, so its local space is render space. Streaks are aligned with each bolt's velocity relative to
 *   the player ship (the apparent motion). Bolts past the render cap still fly and hit, undrawn (logged once).
 * - Targets: one instanced static mesh with per-instance custom data (shield fraction, health fraction, hit flash) for
 *   M_SOLTarget; instance i is target slot i. Instances are added only as the highest used slot grows (a high-water
 *   mark, so a session that never drops a target never has any), and the component is hidden while no target is
 *   active. Only the slot range that is, or was last frame, in use is re-placed; inactive slots are scaled to zero, and
 *   custom data is written only where it changed.
 * - One-shot effects: fixed pools of Niagara components per kind (impact, shield break, spark, explosion), the oldest
 *   reused for each new event; an effect rides with what it hit until its duration ends: its position is recomputed
 *   every update from the event's frame body (FSOLCombatEvent), so it stays on its target at any time warp. Effects
 *   age with the combat update's own (hitch-clamped) delta.
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
        int32 FrameBody = INDEX_NONE;                        // The event's frame body
        FVector3d FrameOffsetM = FVector3d::ZeroVector;      // The event's offset from that body
        FVector3d RelativeVelocityMps = FVector3d::ZeroVector; // Its velocity relative to that body
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

    // Unpauses and shows the bolt system (some bolt is live), or pauses and hides it (none is)
    void SetBoltRendererRunning(bool bRunning);

    // Re-places the in-use target slots and writes changed per-instance custom data
    void UpdateTargets(const FSOLRenderOrigin& origin);

    // Adds hidden target instances so at least instanceCount exist (grows geometrically, up to the slot count)
    void EnsureTargetInstances(int32 instanceCount);

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

    /** Bolt renderer (CPU sim): the NS_SOLBolts system fed from the packed bolt arrays. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UNiagaraComponent> BoltNiagara;

    /** Targets: instance i is target slot i, up to the highest slot ever used. */
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
    TArray<FVector3f> mBoltPositionsCm;        // Scratch, reserved to COMBAT_BOLT_RENDER_CAP once
    TArray<FVector3f> mBoltVelocitiesCmps;     // Scratch, reserved to COMBAT_BOLT_RENDER_CAP once
    bool mIsBoltRendererEnabled = false;       // The bolt system loaded and was activated
    bool mIsBoltRendererRunning = false;       // Unpaused and visible (some bolt is live)
    bool mHasLoggedBoltRenderCap = false;      // The "bolts past the render cap" warning was logged

    // Targets
    TArray<FTransform> mTargetTransforms;      // Scratch, reserved to the slot count once
    TArray<float> mTargetCustomData;           // Last written custom data, COMBAT_TARGET_CUSTOM_DATA_FLOATS per slot
    int32 mTargetRangeEnd = 0;                 // Slots [0, end) were placed last frame (the rest are already hidden)
    bool mIsTargetRendererEnabled = false;
    bool mIsTargetMeshVisible = false;

    // One-shot effects
    FSOLEffectPool mImpactPool;
    FSOLEffectPool mShieldBreakPool;
    FSOLEffectPool mSparkPool;
    FSOLEffectPool mExplosionPool;

    FDelegateHandle mCombatUpdatedHandle;
};
