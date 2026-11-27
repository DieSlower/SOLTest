/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLLaneNodeTable.h"
#include "Npc/SOLNpcBlockMoves.h"
#include "Npc/SOLNpcCombat.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcTierProcessor.h"
#include "Traffic/SOLLaneNetworkSchedule.h"

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "MassArchetypeTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLNpcSubsystem.generated.h"

class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLNpcFullAiProcessor;
class USOLNpcMidProcessor;
class USOLNpcStatsWidget;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
struct FMassCommandBuffer;
struct FMassEntityManager;

// Fired after the NPCs are updated for the frame (positions and tiers final)
DECLARE_MULTICAST_DELEGATE(FSOLOnNpcsUpdated);

// One lane slot's open window this frame (SDD 53 5.4, seconds since J2000) and its traffic active flag (Q22, P5): the
// inputs of the group texture's row 6 (SOLLaneNetwork::LaneWindowTexel, written by ASOLNpcVisuals at its GPU epoch)
struct FSOLNpcLaneSlotWindow
{
    double OpenStartS = 0.0;
    double CloseS = 0.0;
    bool bHasWindow = false;
    bool bActive = false;
    bool bOpenNow = false;      // Open at this frame's sim time (LaneOpenAt): the tunnel open fade's target (Q9, 53d)
};

// Per-frame NPC statistics (the 10b measurements; the 10e overlay later)
struct FSOLNpcStats
{
    int32 Total = 0;
    int32 Full = 0;
    int32 Mid = 0;
    int32 Far = 0;
    int32 Mesh = 0;
    int32 Instanced = 0;
    int32 Point = 0;                    // CPU-fed points (Mid or Full beyond the instanced radius)
    int32 Candidates = 0;               // Ships evaluated this frame
    int32 CandidateGroups = 0;
    int32 TierChanges = 0;
    double EvaluateMs = 0.0;
    double BudgetMs = 0.0;
    double SelectMs = 0.0;              // Select pass plus the command flush
    double MidMs = 0.0;
    double FullAiMs = 0.0;              // Full AI gather, combat step and scatter
    double CollectMs = 0.0;
    double TotalMs = 0.0;               // Whole NPC update on the game thread
    double FullRadiusM = 0.0;           // Budgeted radii this frame
    double MidRadiusM = 0.0;
    double MeshRadiusM = 0.0;           // Derived from the pixel thresholds (SDD 54 4.5), then budgeted
    double InstancedRadiusM = 0.0;
    bool bMeshBudgetBound = false;      // The fixed mesh pool shrank the mesh radius this frame (a reported limit)
    bool bInstancedBudgetBound = false; // The fixed hull pool shrank the hull radius this frame
    FSOLNpcCombatStats Combat;          // Full AI and NPC combat (Part 10d)
};

/**
 * Owns the NPC ship population (SDD 11, Part 10b): spawns it in batches from SOLNpcScale::MakeSpawn (one const shared
 * group fragment per lane, cluster or battle, so chunks are grouped by group), keeps the per-population rail array the
 * GPU far layer uploads once, and runs the tier and Mid processors explicitly every frame on the anchor subsystem's
 * OnUniverseUpdated (after the bodies, ship and render origin are final), then broadcasts OnNpcsUpdated for the
 * visuals. The population is (re)spawned lazily once the player ship exists, from -SOLNpcCount=<n>, the
 * sol.Npc.Count console command or the stress ramp (-SOLNpcRamp, sol.Npc.Ramp).
 */
UCLASS()
class SOLTEST_API USOLNpcSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Declares its dependencies, prepares the processors, reads the command line and hooks the universe update
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Destroys the NPC entities while the entity manager is still alive
    virtual void PreDeinitialize() override;

    // Unhooks from the universe update and releases the Mass objects
    virtual void Deinitialize() override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Requests a population size; it is (re)spawned at the next update once the player ship exists
    void RequestNpcCount(int32 count);

    // Starts the stress ramp up to maxCount (SDD 11 section 3.8); bQuitWhenDone quits the game after the last stage
    void StartRamp(int32 maxCount, bool bQuitWhenDone);

    // Stops the ramp (if any) and despawns every NPC
    void Stop();

    // Turns the player ship to face a battle centre (verification screenshots; sol.Npc.FaceBattle)
    void FaceBattle(int32 battleIndex);

    // Sets the battle ship count and respawns the population (sol.Npc.BattleShips, -SOLNpcBattleShips=)
    void SetBattleShipCount(int32 count);

    // Starts a demo with these options (the Demo page's Start / Apply, Part 10e): sets the options and the combat
    // model, shows or hides the stats overlay per the options, then respawns options.NpcCount ships at the next update
    // (once the player ship exists), or runs the stress ramp up to that count when bRamp
    void StartDemo(const SOLNpcScale::FDemoOptions& options, int32 combatModel, bool bRamp);

    // Sets one demo option (sanitized as on the Demo page) and respawns a live population (sol.Npc.Option)
    void SetOptionValue(SOLNpcScale::EDemoOption option, double value);

    // Returns the NPC combat model in use (the Demo page's, or sol.Npc.CombatModel once the console changes it)
    int32 GetCombatModel() const { return mCombatModel; }

    // Shows or hides the stats overlay (sol.Npc.Stats, Ctrl+F2)
    void SetStatsVisible(bool bVisible);

    // Returns whether the stats overlay is on
    bool IsStatsVisible() const { return mIsStatsVisible; }

    // Returns the overlay widget, or nullptr before it was first shown (verification)
    USOLNpcStatsWidget* GetStatsWidget() const { return StatsWidget; }

    // Returns the ramp state, its stage counts and the last finished stage's result
    const SOLNpcScale::FRampState& GetRampState() const { return mRamp; }
    TConstArrayView<int32> GetRampStages() const { return mRampStages; }
    const FString& GetLastRampResult() const { return mLastRampResult; }

    // Returns the NPC count currently requested (0 when stopped)
    int32 GetRequestedCount() const { return mRequestedCount; }

    // Returns the plume gate: live ships nearer than this, however drawn, get their flame data in the Collect pass (SDD
    // 51 section 4.2, R4; SDD 54 4.7: SOLThruster::NpcThrustGateM of the view, the ribbon reach included while ribbons
    // are on; public for the traffic smoke's V2 setup)
    double ViewPlumeGateM() const;

    // Returns the Full AI and NPC combat state (bolts and events relative to GetCombatFrameBodyM)
    const FSOLNpcCombat& GetCombat() const { return mCombat; }

    // Returns this frame's position of the NPC combat frame body (the battles' body), universe metres
    const FVector3d& GetCombatFrameBodyM() const { return mCombatFrameBodyM; }

    // Returns the current options
    const SOLNpcScale::FDemoOptions& GetOptions() const { return mOptions; }

    // Returns the rails of the current population (index = NPC index), and a revision bumped by every respawn
    TConstArrayView<FSOLNpcRail> GetRails() const { return mRails; }
    uint32 GetPopulationRevision() const { return mPopulationRevision; }

    // Returns the group table and this frame's rail frame (group points in universe metres)
    const FSOLNpcGroupTable& GetGroups() const { return mGroups; }
    const FSOLNpcRailFrame& GetRailFrame() const { return mRailFrame; }

    // Returns this frame's Mid radius margin per group (a lane's hand-over margin, SDD 50 section 4.6; 0 otherwise)
    TConstArrayView<double> GetGroupMidMarginsM() const { return mGroupMidMarginM; }

    // Returns this frame's CPU-simulated (Mid and Full) ships, for the renderer
    TConstArrayView<FSOLNpcCpuShip> GetCpuShips() const { return mCpuShips; }

    // Returns this frame's lane slot windows and active flags, per slot (empty without a lane network)
    TConstArrayView<FSOLNpcLaneSlotWindow> GetLaneSlotWindows() const { return mLaneSlotWindows; }

    // Sets the group columns whose tunnel open fade is above 0 (1 = still visible; SDD 53 4.5, contract: a slot whose
    // tunnel is fading is held): a closed network column keeps its slot and its end points until its 3 s fade is over.
    // ASOLNpcVisuals writes it after each tunnel update; a column that stops fading re-checks the pending releases
    void SetTunnelFadingColumns(TConstArrayView<uint8> fadingColumns);

    // Block moves (SDD 53 4.5, 53c2b). A revision bumped by every per-step move, with that move's lane-local ship
    // ranges (the partial rail upload); a revision bumped by a time jump's full re-share (one full rail upload); the
    // NPC index of the first lane ship (lane ships follow the battle ships); the lane ships per lane column
    uint32 GetBlockMoveRevision() const { return mBlockMoveRevision; }
    TConstArrayView<FSOLShipRange> GetBlockMoveRanges() const { return mBlockMoveRanges; }
    uint32 GetRailRevision() const { return mRailRevision; }
    int32 GetLaneShipFirstIndex() const { return mLaneSpawn.Counts.Battle; }
    TConstArrayView<int32> GetColumnLaneShips() const { return mColumnLaneShips; }

    // Returns the sim time (seconds since J2000) of the population's rail epoch
    double GetEpochSecondsSinceJ2000() const { return mEpochSecondsSinceJ2000; }

    // Returns this frame's statistics
    const FSOLNpcStats& GetStats() const { return mStats; }

    // Fired after every NPC update
    FSOLOnNpcsUpdated& OnNpcsUpdated() { return mOnNpcsUpdated; }

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Destroys every NPC entity and clears the population
    void Despawn();

    // Spawns a population of count ships around the current viewpoint
    void Spawn(int32 count);

    // Creates the next slice of a pending spawn's entities (at most NPC_SPAWN_ENTITIES_PER_FRAME per call)
    void ContinueSpawn();

    // On a population's first spawn, puts the player ship at rest relative to the battles' body, so the battles formed
    // around it stay around it (an explicit exception to SDD 18 decision 1; skipped while the autopilot flies)
    // (since SDD 18 an assisted ship only brakes at 1.5 G, so it would otherwise keep its spawn orbit and leave them)
    void MatchShipToBattleBody();

    // Returns the angle one screen pixel spans, from the player camera (the Mid-to-Far snap gate)
    double ViewPixelAngleRad() const;

    // Writes the player camera's horizontal FOV (degrees) and the viewport size (px), with reference fallbacks
    void ViewCamera(double& outFovDeg, FVector2D& outViewportPx) const;

    // Resolves the layout's body names and fills the group table (battles around the current anchor)
    void BuildGroups(const SOLNpcScale::FLayout& layout, const SOLNpcScale::FBattleParams& battle);

    // Per-frame update: respawn if requested, tiers, Mid motion, stats, ramp; bound to OnUniverseUpdated
    void Update();

    // Runs the Full AI and NPC combat for this frame (gather, step, scatter)
    void UpdateFullAi(double realDeltaS);

    // Advances the ramp by one frame and logs finished stages
    void StepRamp(double realDeltaS);

    // Logs the periodic NpcStats line
    void LogStats(double realDeltaS);

    // Records the frame time and refreshes the stats overlay's text at 4 Hz while it is on
    void UpdateOverlay(double realDeltaS);

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcStatsWidget> StatsWidget;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ship;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcTierProcessor> TierProcessor;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcMidProcessor> MidProcessor;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcFullAiProcessor> FullAiProcessor;

    TSharedPtr<FMassEntityManager> mEntityManager;          // The world's default Mass entity manager
    TSharedPtr<FMassCommandBuffer> mCommandBuffer;          // Reused by every run
    TArray<FMassEntityHandle> mEntities;                    // Every spawned NPC
    TArray<int32> mSpawnOrder;                              // Pending spawn: NPC indices in (group, class) bucket order
    TArray<int32> mSpawnBucketStarts;                       // ...each bucket's first position in mSpawnOrder
    FMassArchetypeHandle mSpawnArchetype;                   // ...the archetype the entities are created in
    int32 mSpawnCreated = 0;                                // ...entities created so far
    double mSpawnStartS = 0.0;                              // ...when Spawn started (platform seconds)
    double mSpawnMaxSliceMs = 0.0;                          // ...the longest Spawn / ContinueSpawn call
    int32 mSpawnSlices = 0;
    TArray<FSOLNpcRail> mRails;                             // Rail per NPC index (the GPU far layer's upload source)
    FSOLNpcGroupTable mGroups;
    FSOLNpcRailFrame mRailFrame;
    TArray<uint8> mGroupCandidate;                          // Per group: 1 when within the Mid keep radius
    TArray<double> mDistancesM;                             // Budget distances (one slot per NPC; front filled)
    TArray<double> mGroupMaxSpeedMps;                       // Per cluster / battle group: bound on a ship's speed
                                                            // relative to the group point, per rail (sim) second
                                                            // (lanes: recomputed each frame, LaneWakeSpeedBoundMps)
    TArray<double> mGroupTravelM;                           // Per group: viewer path length relative to the group
    TArray<double> mGroupShipTravelM;                       // Per group: integral of the ship speed bound over the
                                                            // rail time steps (the ships' possible travel)
    TArray<FVector3d> mGroupPrevViewM;                      // Per group: last viewpoint relative to the group point
    TArray<int32> mGroupWakeDirty;                          // Per group: chunk wake minimums stale (a demotion or a
                                                            // spawn slice); cleared once Evaluate visited the group
    TArray<double> mGroupWakeClockM;                        // Per group: viewer travel + ship travel
    TArray<double> mGroupMidMarginM;                        // Per group: Mid radius margin this frame (lanes:
                                                            // SOLNpcRail::LaneHandOverMarginM; clusters, battles 0)
    double mLastRailTimeS = 0.0;
    bool mHasGroupPrevView = false;
    TArray<double> mBudgetScratch[2];                       // BudgetedRadiusM working storage (one per thread)
    TArray<FSOLNpcCpuShip> mCpuShips;                       // Mid and Full ships for the renderer
    FSOLNpcCombat mCombat;                                  // Full AI and NPC combat (Part 10d)
    FVector3d mCombatFrameBodyM = FVector3d::ZeroVector;    // The battles' body this frame (universe metres)
    FVector3d mPrevCombatFrameBodyM = FVector3d::ZeroVector;
    bool mHasPrevCombatFrameBody = false;
    bool mIsRespawnRequested = false;                       // Respawn at the next update (an option changed)
    int32 mPendingFaceBattle = INDEX_NONE;                  // -SOLNpcFaceBattle: battle to face once the frame is built
    SOLNpcScale::FDemoOptions mOptions;
    FSOLNpcStats mStats;
    FSOLOnNpcsUpdated mOnNpcsUpdated;
    FDelegateHandle mUniverseUpdatedHandle;
    double mEpochSecondsSinceJ2000 = 0.0;                   // Sim time of the population's rail epoch
    int32 mRequestedCount = 0;
    int32 mSpawnedCount = 0;
    uint32 mPopulationRevision = 0;
    double mStatsLogElapsedS = 0.0;
    int32 mStatsLogFrames = 0;
    double mStatsLogNpcMsSum = 0.0;
    double mStatsLogNpcMsMax = 0.0;
    double mStatsLogFrameMsSum = 0.0;
    double mStatsLogFrameMsMax = 0.0;                       // Longest frame of the period (soak spikes)
    double mStatsLogFullAiMsSum = 0.0;
    double mStatsLogPhaseMaxMs[7] = {};                     // Per-phase maxima of the log window (NpcPhaseMax)
    double mStatsLogGpuMsSum = 0.0;
    double mStatsLogGameMsSum = 0.0;

    // Ramp state (SOLNpcScale ramp functions)
    SOLNpcScale::FRampState mRamp;
    TArray<int32> mRampStages;
    TArray<double> mRampFrameMs;
    TArray<double> mRampNpcMs;
    bool mIsRampQuitWhenDone = false;
    FString mLastRampResult;                                // Last finished stage (overlay)

    // Combat model in use: the Demo page's choice, or sol.Npc.CombatModel when the console last changed it. The console
    // variable itself is never written, so nothing outlives this world (SDD 11 Amendment 7)
    int32 mCombatModel = SOL::NPC_COMBAT_DEFAULT_MODEL;
    int32 mLastSeenCVarCombatModel = SOL::NPC_COMBAT_DEFAULT_MODEL;

    // The sim clock's time-set count last seen: a change (any SetJulianDate) marks a lane network time jump
    // (SDD 53 4.3)
    uint32 mSeenClockTimeSetCount = 0;

    // Lane network wiring (SDD 53 4.5, parts 53c2a, 53c2b). Lane ships sit in ship blocks owned by slots; blocks move
    // at the network's epoch steps (StepLaneBlocks), and every slot that owns blocks is held
    SOLLaneNodeTable::FTable mNodeTable;                    // The spawn's node table (asteroid node positions)
    bool mHasNodeEndpoints = false;                         // A lane column ends on a node without a registry body
    TArray<FVector3d> mNodePositionsM;                      // Per node, this frame's asteroid positions (scratch)
    TArray<int32> mBlockSlot;                               // Per ship block, its slot (INDEX_NONE = parked)
    TArray<double> mGroupOpenStartRailS;                    // Per lane column: its slot's window in rail time (NaN
    TArray<double> mGroupCloseRailS;                        // = none); empty without a network
    TArray<FSOLNpcLaneSlotWindow> mLaneSlotWindows;         // Per slot: this frame's window and active flag (row 6)
    TArray<int32> mColumnCpuShips;                          // Per column: this frame's CPU-tier ships (scratch)
    TArray<uint8> mColumnTunnelFading;                      // Per column: tunnel open fade above 0 (SetTunnelFading...)
    uint64 mTunnelFadingFrame = 0;                          // Frame of the last SetTunnelFadingColumns
    static constexpr uint64 NPC_TUNNEL_FADING_MAX_AGE_FRAMES = 2; // Older fading reports hold no slot

    // Block moves (53c2b)
    SOLNpcBlockMoves::FLaneSpawnInputs mLaneSpawn;          // The spawn inputs lane rails are rebuilt from
    bool mHasLaneNetwork = false;                           // The population's lane ships follow a lane network
    TArray<int32> mBlockInWall;                             // Per block: in-wall ships (CountBlockSpawns)
    TArray<int32> mBlockHalo;                               // Per block: halo ships
    TArray<int32> mBlockShips;                              // Per block: ships (the last block partial)
    TArray<FMassEntityHandle> mLaneEntities;                // Per lane-local ship index: its entity (ContinueSpawn)
    TArray<FSOLLanePair> mBlockSlotLanes;                   // Per slot: its lane at the last block step
    TArray<uint8> mBlockSlotInSet;                          // Per slot: its lane in the set at the last block step
    TArray<FSOLLanePair> mSlotLanesNow;                     // Per slot: its lane now (scratch)
    TArray<uint8> mSlotInSetNow;                            // Per slot: in the set now (scratch)
    TArray<double> mSlotWeightsNow;                         // Per slot: LaneWeight now, 0 outside the set (scratch)
    TArray<int32> mSlotCpuShips;                            // Per slot: CPU-tier ships (scratch)
    FSOLShipBlockStep mBlockStep;                           // The last block step
    TArray<FSOLShipRange> mBlockMoveRanges;                 // The last move's lane-local ship ranges
    TArray<int32> mColumnLaneShips;                         // Per lane column: lane ships (block sizes summed)
    uint32 mBlockMoveRevision = 0;
    uint32 mRailRevision = 0;
    bool mIsBlockStepPending = false;                       // The network stepped while a spawn was pending
    bool mIsBlockReshareRequested = false;                  // A time jump's exact re-share is due
    bool mHasPendingRelease = false;                        // A slot owns blocks while its lane is out of the set
    bool mHasFadeHolds = false;                             // A slot out of the set is held only by its tunnel fade
    int32 mBlockStepRuns = 0;                               // Block steps run past the early-out (logged at despawn)

    // Runs the block step after the network's advance (releases, episode opens and takes, holds; a time jump's
    // exact re-share); deferred while a spawn is pending
    void StepLaneBlocks(ESOLLaneNetworkAdvance advance);

    // Writes each slot's lane, in-set flag and weight now (the scratch arrays); returns the slot count
    int32 ReadSlotsNow();

    // Re-rails and re-buckets the moved blocks' ships after the opening slots' columns are refreshed; a full re-share
    // bumps the rail revision, a step the move revision
    void ApplyBlockMoves(TConstArrayView<int32> movedBlocks, TConstArrayView<uint8> slotOpens, bool bFullReshare,
        double startS);

    // Refreshes a slot's column at its episode open: end points and radius (a network slot), and the profile frozen
    // from its blocks' in-wall count (SlotLaneProfile); the column's largest peak is reset
    void RefreshSlotColumn(int32 slot, const FSOLLanePair& lane, int32 inWall, int32 halo);

    // Returns a lane node's end point index: its registry body, else registry count + node
    int32 NodeEndpoint(int32 node) const;

    // Configures the lane network for the spawn epoch and advances it there (the slots exist before the rails);
    // false without a network (node table incomplete)
    bool ConfigureLaneNetwork(const SOLNpcScale::FLayout& layout);

    // Fills the slot columns (end points, radii) and returns each slot's traffic weight over the spawn epoch's set
    void BuildSlotColumns(const SOLNpcScale::FLayout& layout, bool bNetwork, TArray<double>& outSlotWeights);

    // Returns this frame's node positions for the end points past the registry (only the asteroid nodes evaluated),
    // empty when no lane column ends on one
    TConstArrayView<FVector3d> LaneEndpointPositions();

    // Writes this frame's slot windows (rail time per column for the tiers, J2000 per slot for row 6) and the active
    // flags (SDD 53 Q22, P5)
    void UpdateLaneWindows(double railTimeS, const FVector3d& viewpointM);

    // Stats overlay (Part 10e)
    bool mIsStatsVisible = false;
    TArray<double> mOverlayFrameMs;                         // Ring of the last NPC_STATS_FRAME_WINDOW frame times
    TArray<double> mOverlayScratch;                         // Sorted copy for the p95 (reserved once)
    int32 mOverlayFrameCursor = 0;
    double mOverlayRefreshElapsedS = 0.0;
};
