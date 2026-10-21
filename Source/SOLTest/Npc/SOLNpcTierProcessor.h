/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcRail.h"

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "SOLNpcTierProcessor.generated.h"

struct FSOLNpcChunkWakeFragment;
struct FSOLNpcCombatFragment;
struct FSOLNpcDeviationFragment;
struct FSOLNpcRailFragment;
struct FSOLNpcStateFragment;

// One CPU-simulated (Mid or Full) ship, collected for the renderer
struct FSOLNpcCpuShip
{
    FVector3d PositionM = FVector3d::ZeroVector;      // Universe metres, ecliptic
    FVector3d PrevPositionM = FVector3d::ZeroVector;  // Last frame's position (equal to PositionM when unknown)
    uint8 Faction = 0;
    uint8 RenderTier = 2;                             // SOLNpcScale::ERenderTier
    uint8 AiTier = 1;                                 // SOLNpcScale::EAiTier
    FVector3f HeadingDirection = FVector3f::ZeroVector; // Unit steering heading (ecliptic) relative to the rail,
                                                      // or zero to take the heading from the motion
    uint8 bHidden = 0;                                // Dead (waiting to respawn): not drawn
    int32 NpcIndex = INDEX_NONE;                      // Population index (stable owner key for pooled meshes)
    float RailDistanceM = 0.0f;                       // Bare-rail distance from the viewpoint (the GPU far layer's
                                                      // test: at or beyond the Mid radius the GPU draws the point)
};

// What one run of USOLNpcTierProcessor does (the subsystem runs it once per pass, in this order each frame)
enum class ESOLNpcTierPass : uint8
{
    Evaluate,       // Rail positions and distances of awake candidate ships, and the budget distances (parallel)
    Select,         // AI and render tiers with the budgeted radii, tag changes deferred (parallel per chunk)
    Collect,        // Copies every Mid / Full ship for the renderer (parallel per chunk)
};

/**
 * NPC tier processor (SDD 11 section 3.2, Part 10b). Chunks are grouped by FSOLNpcGroupFragment, and the chunk filter
 * skips every chunk whose group is not a candidate (its lane capsule, cluster shell or battle ball lies outside the Mid
 * keep radius) unless the chunk is already Mid or Full, so 1M Far ships away from the player cost only the per-group
 * distance tests. Inside a candidate group, a Far ship sleeps (one compare per frame) until its group's wake clock (the
 * viewer's travel relative to the group plus the group's maximum ship speed times the rail time) has advanced by its
 * distance beyond the Mid keep radius, since it cannot have come closer than that. Awake candidates get their rail
 * evaluated (SOLNpcRail::EvaluateM, the motion model seam) plus their deviation; the subsystem then shrinks the tier
 * radii to the budgets (SOLNpcScale::BudgetedRadiusM) and the Select pass picks tiers with hysteresis, moving ships
 * between the Far, Mid and Full archetypes through the command buffer (in entity order, so deterministic). A dead
 * ship leaving the Full tier respawns at once (only the Full tier fights, SDD 11 Amendment 7).
 *
 * Not auto-registered: USOLNpcSubsystem runs each pass explicitly from OnUniverseUpdated.
 */
UCLASS()
class SOLTEST_API USOLNpcTierProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:

    // Registers the queries and opts out of the processing phases
    USOLNpcTierProcessor();

    // Per-frame inputs, owned by the subsystem; the views must outlive the run
    struct FFrameInputs
    {
        const FSOLNpcRailFrame* RailFrame = nullptr;
        TConstArrayView<uint8> GroupCandidate;        // 1 per candidate group
        TConstArrayView<double> GroupWakeClockM;      // Per group: viewer travel relative to the group plus the
                                                      // group's maximum ship speed times the rail time elapsed
        TArrayView<int32> GroupWakeDirty;             // Per group: 1 when a ship was demoted to Far (its new chunk's
                                                      // minimum wake key is stale): Evaluate visits every chunk of the
                                                      // group once; Select sets it, the subsystem clears it
        double MidKeepRadiusM = 0.0;                  // Nominal Mid radius times (1 + hysteresis)
        FVector3d ViewpointM = FVector3d::ZeroVector;
        double FullRadiusM = 0.0;                     // Budgeted
        double MidRadiusM = 0.0;                      // Budgeted
        double AiHysteresis = 0.0;
        double MeshRadiusM = 0.0;                     // Budgeted
        double InstancedRadiusM = 0.0;                // Budgeted
        double RenderHysteresis = 0.0;
    };

    // Sets the pass and inputs of the next run
    void SetPass(ESOLNpcTierPass pass, const FFrameInputs& inputs)
    {
        mPass = pass;
        mInputs = inputs;
    }

    // Sets the arrays the Evaluate and Collect passes fill. distancesM must hold at least one slot per NPC; Evaluate
    // writes the distances inside the Mid keep radius to its front (GetBudgetDistanceCount of them)
    void SetOutputs(TArray<double>* distancesM, TArray<FSOLNpcCpuShip>* cpuShips)
    {
        mDistancesM = distancesM;
        mCpuShips = cpuShips;
    }

    // Returns how many budget distances the last Evaluate pass wrote
    int32 GetBudgetDistanceCount() const { return mBudgetDistanceCount; }

    // Returns the ships per AI tier (Full, Mid) and per render tier (Mesh, Instanced) counted by the last Select pass
    int32 GetAiTierCount(int32 tier) const { return mAiCounts[tier]; }
    int32 GetRenderTierCount(int32 tier) const { return mRenderCounts[tier]; }

    // Returns the tier changes (promotions plus demotions) of the last Select pass
    int32 GetTierChanges() const { return mTierChanges; }

    // Returns the ships evaluated (awake) by the last Evaluate pass
    int32 GetEvaluatedCount() const { return mEvaluated; }

protected:

    // Declares the fragments each pass reads and writes
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& entityManager) override;

    // Runs the current pass
    virtual void Execute(FMassEntityManager& entityManager, FMassExecutionContext& context) override;

private:

    // One chunk taking part in a pass: raw fragment arrays, valid until the pass's commands flush
    struct FChunkView
    {
        const FSOLNpcRailFragment* Rails = nullptr;
        FSOLNpcStateFragment* States = nullptr;
        FSOLNpcDeviationFragment* Deviations = nullptr;
        FSOLNpcCombatFragment* Combats = nullptr;      // Select: respawn on leaving Full; Collect: read only
        const FMassEntityHandle* Entities = nullptr;
        FSOLNpcChunkWakeFragment* Wake = nullptr;
        int32 Count = 0;
        int32 Group = 0;
        int32 ClassIndex = 0;                   // Select: the chunk's ship class (respawn health)
        int32 Offset = 0;                       // Collect: first index in the CPU ship list
        uint8 Tier = 2;                         // AI tier of the chunk's archetype (SOLNpcScale::EAiTier)
    };

    // A deferred archetype move found by the Select pass
    struct FTierMove
    {
        FMassEntityHandle Entity;
        uint8 FromTier = 2;
        uint8 ToTier = 2;
    };

    // Returns whether a chunk takes part this frame: already CPU-simulated, or a candidate group with an awake ship
    bool IsChunkActive(const FMassExecutionContext& context) const;

    // Records one chunk's view (serial chunk walk; the passes then run over mChunks with ParallelFor)
    void AddChunkView(FMassExecutionContext& context, bool bCollect);

    // Evaluates rails and distances of one chunk's awake ships, appends the budget distances, refreshes its minimum
    void EvaluateChunk(const FChunkView& view);

    // Selects tiers of one chunk and records its archetype moves
    void SelectChunk(const FChunkView& view);

    // Copies one Mid / Full chunk's ships into the CPU ship list at its offset
    void CollectChunk(const FChunkView& view) const;

    FMassEntityQuery mEvaluateQuery;            // All NPCs, chunk-filtered: rail (ro), deviation, state, wake (rw)
    FMassEntityQuery mSelectQuery;              // All NPCs, chunk-filtered: state, deviation, combat, wake (rw)
    FMassEntityQuery mCollectQuery;             // Mid or Full NPCs: rail, combat (ro), state, deviation (rw)
    TArray<FChunkView> mChunks;                 // This pass's chunks (reused every frame)
    TArray<FTierMove> mMoves;                   // Select's archetype moves (reused; sized to the visited ships)
    int32 mMoveCount = 0;
    ESOLNpcTierPass mPass = ESOLNpcTierPass::Evaluate;
    FFrameInputs mInputs;
    TArray<double>* mDistancesM = nullptr;
    TArray<FSOLNpcCpuShip>* mCpuShips = nullptr;
    int32 mAiCounts[2] = {};                    // Full, Mid (summed with atomic adds per chunk)
    int32 mRenderCounts[2] = {};                // Mesh, Instanced
    int32 mTierChanges = 0;
    int32 mEvaluated = 0;
    int32 mBudgetDistanceCount = 0;
};
