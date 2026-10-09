/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Combat/SOLCombatGrid.h"
#include "Combat/SOLCombatTypes.h"
#include "Combat/SOLDamageMath.h"
#include "Flight/SOLFlight.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "MassEntityQuery.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLCombatSubsystem.generated.h"

class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMinorBodySubsystem;
class USOLShipSubsystem;
class USOLTargetingSubsystem;
struct FMassEntityManager;
struct FMassExecutionContext;

// Fired at the end of every combat update; this frame's bolts, targets and events are final and readable
DECLARE_MULTICAST_DELEGATE(FSOLOnCombatUpdated);

/**
 * Pulse-gun bolts, dropped targets and their damage (SDD 7, Part 7b; 7e's asteroid and ring-rock hits).
 *
 * DATA LAYOUT (deviation from SDD 7 decision 3, which named Mass entities): bolts and targets are pooled
 * structure-of-arrays inside this subsystem, not Mass entities. A bolt's whole job is a cross-population interaction
 * (sweep against the target grid, then write a TARGET's shield/health), which in Mass needs random-access writes to
 * another archetype's fragments from inside a processor; bolts are created and destroyed eight times a second each,
 * which in Mass is either command-buffer archetype churn or an active-flag pool the processor still iterates; and the
 * GPU bolt renderer (7c) wants exactly one packed position array, which a Mass chunk layout would have to copy out
 * anyway. SoA arrays reserved once (COMBAT_MAX_BOLTS, COMBAT_MAX_TARGETS) give the same cache-friendly iteration with
 * no per-frame allocation, a parallel sweep, and zero-copy read access for the visuals. Minor bodies stay Mass
 * entities owned elsewhere; this subsystem only reads them through its own query.
 *
 * FRAMES. Every position the API takes or returns is universe metres in the ecliptic frame (like the observer and the
 * minor bodies; NOT the Unreal-handed ship frame). Internally each bolt and target is stored RELATIVE TO A BODY: the
 * player's reference-frame body when it was fired or dropped (the M lock if it is a body, else the anchor). So a target
 * stays at rest in that frame and a bolt keeps its frame's motion, including the time-warp motion the ship itself is
 * carried by, exactly as the ship is. Bolts integrate in real time, like the ship. LIMITATION: when the reference is
 * M-locked to a non-body targetable, bolts and targets are stored relative to the anchor body instead, so under time
 * warp they get the anchor's carry while the ship gets the targetable's (no ISOLTargetable implementer exists yet, so
 * this cannot happen today; CLAUDE.md tech debt). Events carry their frame body and offset (FSOLCombatEvent), so effects
 * and sounds follow the same carry. A bolt farther than COMBAT_BOLT_MAX_RANGE_M from the ship is removed.
 *
 * SWEEPS. Hit tests run in a ship-centred sweep frame (each position minus the player ship's position at the same
 * instant), so co-moving things (bolts, targets, co-orbiting ring rocks) have short per-frame segments whatever their
 * absolute speed. Each bolt's segment (previous to current position) is swept against targets and minor bodies MOVING
 * linearly over the frame (SOLBoltMath::SweptSphereHitMoving), not against their end positions: a belt asteroid can move
 * hundreds of metres a frame relative to the ship while a ring rock is a few metres across. Minor-body previous
 * positions come from this subsystem's own snapshot of the last update; anything that moved more than
 * COMBAT_MAX_SWEEP_STEP_M in one frame, or a ring rock whose render-fragment Generation changed (reassigned to another
 * cell), is treated as having teleported (tested at its new position only), and a bolt whose own segment is that long
 * skips hit tests for the frame. A new bolt's first segment runs from its muzzle. Only minor bodies inside the
 * bounding box of this frame's bolt segments enter the grid, and the whole minor-body pass is skipped while no bolt is
 * live (while one is, every minor body is still scanned once a frame for the snapshot: CLAUDE.md tech debt). The dense
 * GPU-only ring rock layer (Niagara) has no CPU positions, so bolts pass through it; only belt asteroids and the ring
 * pool's active rocks absorb bolts.
 *
 * ORDER. The update runs on USOLAnchorSubsystem::OnUniverseUpdated, i.e. strictly after OnBodiesUpdated has finished
 * (the ship step, the ring-pool streaming on OnShipsStepped and the minor-body orbit run, in whatever order they bind),
 * so this frame's ship, bodies and minor bodies are all final whatever their bind order. That makes the order explicit
 * rather than relying on bind order among OnBodiesUpdated listeners (CLAUDE.md tech debt). Visuals and audio that read
 * this subsystem bind to OnCombatUpdated, not beside it on OnUniverseUpdated, for the same reason. The frame's real
 * (hitch-clamped) delta is captured from OnBodiesUpdated, which only stores it.
 *
 * Within one update: advance targets (and regenerate shields), apply a pending player drop, advance bolts, fire the
 * player's guns (each new bolt swept from its muzzle this frame), build the grids, sweep (parallel), apply hits in bolt
 * order (damage, flash, events, removals; a bolt whose target an earlier bolt destroyed is re-swept on the game thread
 * and may hit what lies beyond), then broadcast OnCombatUpdated. Queued events stay readable until the next update.
 */
UCLASS()
class SOLTEST_API USOLCombatSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Declares its dependencies, reserves the pools and hooks the per-frame update
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Unhooks from the universe update and empties the bolt, target, snapshot and event arrays
    virtual void Deinitialize() override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Sets whether the player's fire trigger is held; the guns fire at the cadence rate on the next updates
    void SetPlayerTriggerHeld(bool bHeld) { mIsPlayerTriggerHeld = bHeld; }

    // Sets the player's aim: the camera's offset from the ship and its forward direction (ecliptic, meters)
    void SetPlayerAim(const FVector3d& cameraOffsetM, const FVector3d& cameraForward);

    // Requests a target drop ahead of the player ship; the next update performs it with that frame's ship pose
    void RequestPlayerTargetDrop() { mIsPlayerDropRequested = true; }

    // Spawns a bolt at a universe position (ecliptic m) with a universe velocity (m/s); false at the bolt cap
    bool FireBolt(const FVector3d& muzzlePositionM, const FVector3d& velocityMps, int32 owner = SOLCombat::PLAYER_OWNER);

    // Drops a target ahead of a ship (ecliptic position and forward), at rest in the player's reference frame; honours
    // the drop cooldown and evicts the oldest target when the pool is full. Returns the target's slot, or INDEX_NONE
    int32 DropTarget(const FVector3d& shipPositionM, const FVector3d& shipForward);

    // Returns the number of live bolts (the length of the bolt arrays)
    int32 GetBoltCount() const { return mBoltPositionsM.Num(); }

    // Returns every live bolt's position (ecliptic universe meters) as of the last update, packed
    TConstArrayView<FVector3d> GetBoltPositionsM() const { return mBoltPositionsM; }

    // Returns every live bolt's velocity (ecliptic universe m/s, not relative to anything) as of the last update
    TConstArrayView<FVector3d> GetBoltVelocitiesMps() const { return mBoltVelocitiesMps; }

    // Returns every live bolt's owner (SOLCombat::PLAYER_OWNER for the player)
    TConstArrayView<int32> GetBoltOwners() const { return mBoltOwners; }

    // Returns the size of the target pool; every target array below has this length, indexed by slot
    int32 GetTargetSlotCount() const { return mTargetActive.Num(); }

    // Returns how many target slots are in use
    int32 GetActiveTargetCount() const { return mActiveTargetCount; }

    // Returns, per slot, whether a target occupies it (other slots' values are stale)
    TConstArrayView<bool> GetTargetActive() const { return mTargetActive; }

    // Returns, per slot, the target's position (ecliptic universe meters) as of the last update
    TConstArrayView<FVector3d> GetTargetPositionsM() const { return mTargetPositionsM; }

    // Returns, per slot, the shield left as a fraction of SOL::COMBAT_TARGET_SHIELD
    TConstArrayView<float> GetTargetShieldFractions() const { return mTargetShieldFractions; }

    // Returns, per slot, the health left as a fraction of SOL::COMBAT_TARGET_HEALTH
    TConstArrayView<float> GetTargetHealthFractions() const { return mTargetHealthFractions; }

    // Returns, per slot, the hit flash: 1 on the frame of a hit, fading linearly to 0 over COMBAT_HIT_FLASH_DURATION_S
    TConstArrayView<float> GetTargetHitFlash() const { return mTargetHitFlash; }

    // Returns the radius every target is hit-tested with (meters)
    double GetTargetRadiusM() const;

    // Returns the events queued by the last update (and by any call since); readable until the next update
    TConstArrayView<FSOLCombatEvent> GetEvents() const { return mEvents; }

    // Returns how many events the last update could not queue because the event cap was reached
    int32 GetDroppedEventCount() const { return mDroppedEventCount; }

    // Returns a body's current position (ecliptic), or the origin for INDEX_NONE; with an event's FrameBody, this is
    // what effects and sounds recompute the event's position from each frame (see FSOLCombatEvent)
    FVector3d GetFrameBodyPositionM(int32 bodyIndex) const;

    // Returns the real, hitch-clamped seconds the last update advanced by (what effects and sounds age with)
    double GetLastUpdateDeltaS() const { return mLastUpdateDeltaS; }

    // Returns how many targets have been dropped since the world began (verification)
    int32 GetTargetDropCount() const { return mTargetDropCount; }

    // Returns the slot of the most recent drop, or INDEX_NONE before the first (verification)
    int32 GetLastDroppedTargetSlot() const { return mLastDroppedTargetSlot; }

    // Returns how many targets bolts have destroyed since the world began (verification)
    int32 GetTargetsDestroyedCount() const { return mTargetsDestroyedCount; }

    // Fired at the end of every update; bind visuals and audio here
    FSOLOnCombatUpdated& OnCombatUpdated() { return mOnCombatUpdated; }

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Per-task scratch of the parallel sweep: the grid query output, reserved once and reused every frame
    struct FSOLSweepScratch
    {
        TArray<int32> CandidateIds;
    };

    // What a bolt's sweep hit first this frame
    enum class ESOLBoltHit : uint8
    {
        None,
        Target,
        MinorBody,
    };

    // Stores this frame's hitch-clamped real delta; bound to the anchor's OnBodiesUpdated (does no other work)
    void CaptureFrameDelta(float realDeltaSeconds) { mFrameDeltaS = realDeltaSeconds; }

    // Runs one combat frame (see the class comment); bound to the anchor's OnUniverseUpdated
    void Update();

    // Returns the body bolts and targets are stored relative to: the player's reference body, else the anchor, else none
    int32 ResolveFrameBody() const;

    // Returns a body's current velocity (ecliptic), or zero for INDEX_NONE
    FVector3d GetFrameBodyVelocityMps(int32 bodyIndex) const;

    // Moves every active target with its frame, regenerates its shield, fades its hit flash and computes its sweep
    // segment for this frame
    void AdvanceTargets(double dt);

    // Moves every bolt with its frame, ages it (expiring it past COMBAT_BOLT_MAX_RANGE_M from the ship), computes its
    // sweep segment and grows the frame's bolt bounding box
    void AdvanceBolts(double dt);

    // Appends this frame's sweep scratch for the bolt just added at the end of the arrays (segment in the sweep frame)
    void AppendBoltSweep(const FVector3d& sweepStartM, const FVector3d& sweepEndM);

    // Fills the target grid with every active target's swept bounding sphere
    void BuildTargetGrid();

    // Snapshots the minor bodies and fills the minor-body grid with those whose swept sphere reaches the bolt box
    void BuildMinorBodyGrid();

    // Reads one chunk of minor bodies into the snapshot and the grid
    void ProcessMinorBodyChunk(FMassExecutionContext& context);

    // Sweeps one bolt against the grids and records its earliest hit on an active target or a minor body (runs on
    // worker threads; writes only bolt index)
    void SweepBolt(FSOLSweepScratch& scratch, int32 boltIndex);

    // Applies each bolt's recorded hit in bolt order (re-sweeping a bolt whose target an earlier bolt destroyed), then
    // removes the hit and expired bolts
    void ApplyBoltHits();

    // Returns where a bolt's recorded hit is at the end of this frame (universe meters): the contact point on what it
    // hit, carried along with it, so an effect started there sits on the object (not where its frame was mid-frame)
    FVector3d ComputeHitPositionM(int32 boltIndex) const;

    // Fires the player's guns for this frame at the cadence rate, alternating muzzles (needs the ship's state); each new
    // bolt's first segment, from its muzzle to where it has flown since it was fired, joins this frame's sweep
    void FirePlayerGuns(double dt, bool bHasShip, const FSOLShipState& shipState);

    // Refreshes the per-slot shield and health fractions the visuals read
    void RefreshTargetFractions();

    // Removes a target from its slot (no event)
    void DeactivateTarget(int32 slot);

    // Queues one event unless the cap was reached; it is held relative to frameBody, moving at relativeVelocityMps in it
    void AddEvent(ESOLCombatEventType type, const FVector3d& positionM, const FVector3d& velocityMps, int32 frameBody,
        const FVector3d& relativeVelocityMps, int32 targetSlot, int32 owner);

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    UPROPERTY(Transient)
    TObjectPtr<USOLMinorBodySubsystem> MinorBodies;    // Dependency only: its entities are read through mMinorBodyQuery

    // Bolts: packed structure-of-arrays, reserved to COMBAT_MAX_BOLTS once, compacted in place when bolts die
    TArray<int32> mBoltFrameBodies;                    // Body each bolt is stored relative to (INDEX_NONE: Sun frame)
    TArray<FVector3d> mBoltRelPositionsM;              // Position relative to that body
    TArray<FVector3d> mBoltRelVelocitiesMps;           // Velocity relative to that body
    TArray<double> mBoltLifetimesS;                    // Remaining lifetime
    TArray<int32> mBoltOwners;                         // Shooter id
    TArray<FVector3d> mBoltPositionsM;                 // Universe position at the last update (visuals; next sweep start)
    TArray<FVector3d> mBoltVelocitiesMps;              // Universe velocity at the last update (visuals)
    TArray<FVector3d> mBoltSweepStartsM;               // Per-frame scratch: segment start in the sweep frame
    TArray<FVector3d> mBoltSweepEndsM;                 // Per-frame scratch: segment end in the sweep frame
    TArray<bool> mBoltSweepValid;                      // Per-frame scratch: false when the segment is too long to sweep
    TArray<ESOLBoltHit> mBoltHitKinds;                 // Per-frame scratch: what the sweep hit first
    TArray<int32> mBoltHitIndices;                     // Per-frame scratch: target slot or minor-body snapshot index
    TArray<double> mBoltHitTimes;                      // Per-frame scratch: hit time along the segment, [0,1]

    // Targets: a fixed pool of COMBAT_MAX_TARGETS slots, so the visuals can map slot to instance one to one
    TArray<bool> mTargetActive;
    TArray<int32> mTargetFrameBodies;
    TArray<FVector3d> mTargetRelPositionsM;
    TArray<FVector3d> mTargetRelVelocitiesMps;         // Zero when the frame is a body (at rest in it)
    TArray<FVector3d> mTargetPositionsM;               // Universe position at the last update
    TArray<FVector3d> mTargetSweepStartsM;             // Per-frame scratch, sweep frame
    TArray<FVector3d> mTargetSweepEndsM;               // Per-frame scratch, sweep frame
    TArray<SOLDamageMath::FDamageState> mTargetDamage;
    TArray<double> mTargetSpawnTimesS;                 // Combat time of each drop, for oldest-first eviction
    TArray<float> mTargetHitFlash;
    TArray<float> mTargetShieldFractions;
    TArray<float> mTargetHealthFractions;
    int32 mActiveTargetCount = 0;

    // Minor bodies: a snapshot indexed by the query's (stable) iteration order, refreshed only while bolts are live
    TSharedPtr<FMassEntityManager> mEntityManager;
    FMassEntityQuery mMinorBodyQuery;                  // Minor-body state and render fragments, read-only
    FMassExecuteFunction mMinorBodyChunkFunction;      // Built once so a frame does not allocate one
    TArray<FMassEntityHandle> mMinorHandles;           // Entity seen at each index at the last snapshot
    TArray<FVector3d> mMinorPositionsM;                // Universe position at the last snapshot
    TArray<uint16> mMinorGenerations;                  // Render-fragment Generation at the last snapshot
    TArray<FVector3d> mMinorSweepStartsM;              // Sweep-frame segment of each body entered in the grid this frame
    TArray<FVector3d> mMinorSweepEndsM;
    TArray<double> mMinorHitRadiiM;
    int32 mMinorScanIndex = 0;                         // Running index while the chunks are scanned
    bool mIsMinorSnapshotFresh = false;                // The snapshot was taken at the previous update

    // Broad phase and the parallel sweep's per-task scratch
    SOLCombatGrid::FGrid mTargetGrid{ SOL::COMBAT_TARGET_GRID_CELL_M };
    SOLCombatGrid::FGrid mMinorBodyGrid{ SOL::COMBAT_MINOR_BODY_GRID_CELL_M };
    TArray<FSOLSweepScratch> mSweepScratch;

    // This frame's sweep frame: the player ship's universe position now and at the previous update
    FVector3d mSweepOriginM = FVector3d::ZeroVector;
    FVector3d mPrevSweepOriginM = FVector3d::ZeroVector;
    bool mHasPrevSweepOrigin = false;
    FBox3d mBoltSweepBounds = FBox3d(ForceInit);       // This frame's bolt segments' bounding box (sweep frame)

    // Player gun and drop input
    FVector3d mAimCameraOffsetM = FVector3d::ZeroVector;
    FVector3d mAimForward = FVector3d::ZeroVector;
    double mGunCooldownS = 0.0;
    int32 mNextGunSide = 0;                            // 0 = right muzzle, 1 = left; alternates every shot
    bool mHasAim = false;
    bool mIsPlayerTriggerHeld = false;
    bool mIsPlayerDropRequested = false;

    // Clock, events and delegates
    double mCombatTimeS = 0.0;                         // Real seconds of combat updates since the world began
    double mLastDropTimeS = -1.0e30;
    float mFrameDeltaS = 0.0f;
    double mLastUpdateDeltaS = 0.0;                    // dt of the last update
    int32 mTargetDropCount = 0;                        // Verification counters
    int32 mLastDroppedTargetSlot = INDEX_NONE;
    int32 mTargetsDestroyedCount = 0;
    TArray<FSOLCombatEvent> mEvents;
    int32 mPublishedEventCount = 0;                    // Events already broadcast (removed at the next update)
    int32 mDroppedEventCount = 0;
    FSOLOnCombatUpdated mOnCombatUpdated;
    FDelegateHandle mBodiesUpdatedHandle;
    FDelegateHandle mUniverseUpdatedHandle;
};
