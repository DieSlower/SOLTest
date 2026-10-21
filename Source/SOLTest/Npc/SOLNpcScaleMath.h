/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "SOLConstants.h"

// Pure logic for the NPC scale demo (SDD 11, Part 10a): AI and render tier selection with hysteresis and budgets,
// population distribution and rails, the stress ramp and frame statistics, and the Demo-page option model. No UObjects,
// no world access, no global mutable state; the contract is in Docs/Plans/11-scale-and-performance-demo-plan.md.
namespace SOLNpcScale
{
    // AI tier, nearest first; the value is the tier index used by SelectTierIndex
    enum class EAiTier : uint8
    {
        Full = 0,
        Mid = 1,
        Far = 2,
    };

    // Render tier of a CPU-simulated (Mid or Full) ship, nearest first
    enum class ERenderTier : uint8
    {
        Mesh = 0,
        Instanced = 1,
        Point = 2,
    };

    // Which rail a ship rides
    enum class EPlacementKind : uint8
    {
        Battle,
        Lane,
        Cluster,
    };

    // Stress ramp phase
    enum class ERampPhase : uint8
    {
        Idle,
        Settling,
        Measuring,
        Done,
    };

    // A lane between two registry bodies
    struct FLaneDef
    {
        FName BodyA;
        FName BodyB;
        double RadiusM = 0.0;            // Lateral radius of the lane capsule
        double Weight = 0.0;             // Relative share of lane ships
    };

    // A cluster shell around a registry body
    struct FClusterDef
    {
        FName Body;
        double InnerRadiusM = 0.0;
        double OuterRadiusM = 0.0;
        double MaxInclinationRad = 0.0;
        double Weight = 0.0;             // Relative share of cluster ships
    };

    // Lanes and clusters of a population
    struct FLayout
    {
        TArray<FLaneDef> Lanes;
        TArray<FClusterDef> Clusters;
        double LaneMinSpeedMps = 0.0;
        double LaneMaxSpeedMps = 0.0;
    };

    // Battles near the player
    struct FBattleParams
    {
        int32 BattleCount = 1;
        double RadiusM = 0.0;            // Ball radius each battle's ships are spread over
        double MinDistanceM = 0.0;       // Battle centre distance range from the player
        double MaxDistanceM = 0.0;
    };

    // Ships per placement kind
    struct FCounts
    {
        int32 Battle = 0;
        int32 Lane = 0;
        int32 Cluster = 0;

        // Returns the population size
        int32 Total() const { return Battle + Lane + Cluster; }
    };

    // Kind and index within that kind of one NPC index
    struct FPlacementSlot
    {
        EPlacementKind Kind = EPlacementKind::Cluster;
        int32 LocalIndex = INDEX_NONE;
    };

    // Spawn parameters of one ship (fields of other kinds keep their defaults)
    struct FSpawn
    {
        EPlacementKind Kind = EPlacementKind::Cluster;
        int32 GroupIndex = INDEX_NONE;                          // Lane, cluster or battle index
        int32 Faction = 0;                                      // 0 or 1
        double LaneStartParam = 0.0;                            // Lane only
        double LaneDirection = 1.0;                             // Lane only: +1 or -1
        double LaneSpeedMps = 0.0;                              // Lane only
        FVector2d LaneLateralOffsetM = FVector2d::ZeroVector;   // Lane only
        double OrbitRadiusM = 0.0;                              // Cluster only
        double OrbitInclinationRad = 0.0;                       // Cluster only
        double OrbitNodeRad = 0.0;                              // Cluster only
        double OrbitPhaseRad = 0.0;                             // Cluster only
        FVector3d BattleSlotOffsetM = FVector3d::ZeroVector;    // Battle only
    };

    // Ramp state machine state
    struct FRampState
    {
        ERampPhase Phase = ERampPhase::Idle;
        int32 StageIndex = INDEX_NONE;
        double PhaseElapsedS = 0.0;
    };

    // Result of one ramp step
    struct FRampStep
    {
        FRampState State;
        bool bRecordFrame = false;       // This frame's time belongs to the stage's measurement
        bool bStageFinished = false;
        int32 FinishedStageIndex = INDEX_NONE;
    };

    // Summary of a stage's frame times
    struct FFrameStats
    {
        int32 Count = 0;
        double MeanMs = 0.0;
        double MinMs = 0.0;
        double P50Ms = 0.0;
        double P95Ms = 0.0;
        double P99Ms = 0.0;
        double MaxMs = 0.0;
    };

    // Demo-page options, in menu order
    enum class EDemoOption : uint8
    {
        NpcCount,
        LaneFraction,
        BattleShipCount,
        BattleCount,
        BattleRadiusM,
        BattleMinDistanceM,
        BattleMaxDistanceM,
        FullAiRadiusM,
        MidAiRadiusM,
        FullAiBudget,
        MidAiBudget,
        AiHysteresis,
        MeshRadiusM,
        InstancedRadiusM,
        MeshBudget,
        InstancedBudget,
        RenderHysteresis,
        NpcCombat,
        ShowStats,
        RampSettleS,
        RampMeasureS,
        Count
    };

    // Every Demo-page option; the initializers are the descriptor defaults (SDD 11 section 3.8)
    struct FDemoOptions
    {
        int32 NpcCount = 1000000;
        double LaneFraction = 0.4;
        int32 BattleShipCount = 2000;
        int32 BattleCount = 4;
        double BattleRadiusM = 3000.0;
        double BattleMinDistanceM = 5000.0;
        double BattleMaxDistanceM = 20000.0;
        double FullAiRadiusM = 50000.0;
        double MidAiRadiusM = 2000000.0;
        int32 FullAiBudget = 4000;
        int32 MidAiBudget = 100000;
        double AiHysteresis = 0.1;
        double MeshRadiusM = 2000.0;
        double InstancedRadiusM = 200000.0;
        int32 MeshBudget = 32;
        int32 InstancedBudget = 20000;
        double RenderHysteresis = 0.1;
        bool bNpcCombat = true;
        bool bShowStats = true;
        double RampSettleS = 5.0;
        double RampMeasureS = 10.0;
        uint32 Seed = SOL::NPC_DEFAULT_SEED;   // Not a menu option; untouched by SetOption / SanitizeOptions
    };

    // Range, step and default of one option
    struct FOptionDesc
    {
        EDemoOption Id;
        double Min;
        double Max;
        double Step;
        double Default;
        bool bInteger;
        bool bBool;
    };

    // What SanitizeOptions changed or warns about
    enum class EOptionIssue : uint32
    {
        None = 0,
        ValueClamped = 1 << 0,                // A field was non-finite or outside its descriptor range
        MidRadiusRaised = 1 << 1,
        InstancedRadiusLowered = 1 << 2,
        MeshRadiusLowered = 1 << 3,
        BattleShipsLimited = 1 << 4,          // BattleShipCount > NpcCount
        BattleDistanceRaised = 1 << 5,        // BattleMaxDistanceM < BattleMinDistanceM
        BattleShipsOverFullBudget = 1 << 6,   // Warning only, nothing changed
        BattlesBeyondFullAi = 1 << 7,         // Warning only: BattleMaxDistanceM + BattleRadiusM > FullAiRadiusM
        MidBudgetRaised = 1 << 8,             // MidAiBudget < FullAiBudget: raised to it
    };
    ENUM_CLASS_FLAGS(EOptionIssue);

    // Sanitized options and the issues found
    struct FSanitizeResult
    {
        FDemoOptions Options;
        EOptionIssue Issues = EOptionIssue::None;
    };

    // ---- Tiers ----

    // Returns the tier in [0, N] for a distance against N promote radii, keeping the current tier up to R*(1+h)
    SOLTEST_API int32 SelectTierIndex(int32 currentTier, double distanceM, TConstArrayView<double> promoteRadiiM,
        double hysteresisFraction);

    // Returns the AI tier for a distance with hysteresis (radii {full, mid})
    SOLTEST_API EAiTier SelectAiTier(EAiTier current, double distanceM, double fullRadiusM, double midRadiusM,
        double hysteresisFraction);

    // Returns the render tier of a CPU ship for a distance with hysteresis (radii {mesh, instanced})
    SOLTEST_API ERenderTier SelectRenderTier(ERenderTier current, double distanceM, double meshRadiusM,
        double instancedRadiusM, double hysteresisFraction);

    // Returns the largest radius <= nominal that keeps at most budget distances inside its keep radius
    SOLTEST_API double BudgetedRadiusM(TConstArrayView<double> distancesM, double nominalRadiusM,
        double hysteresisFraction, int32 budget, TArray<double>& scratch);

    // Returns the value that would sit at index nth (clamped into range) if values were sorted ascending; quickselect,
    // linear on average, reorders values. Returns 0 for an empty array
    SOLTEST_API double SelectNth(TArray<double>& values, int32 nth);

    // ---- Temporal cull (SDD 11 Amendments 1 and 6) ----

    // Returns whether a Far chunk takes part in a tier pass: its group is a candidate and either the group's chunk
    // minimums are stale (Evaluate only) or the chunk's smallest wake key has been reached by the group's wake clock
    SOLTEST_API bool IsFarChunkActive(bool bGroupCandidate, bool bGroupWakeDirty, bool bEvaluatePass,
        double chunkMinWakeKeyM, double groupWakeClockM);

    // Clears the wake-dirty flag of every candidate group (the ones Evaluate visited); the others keep theirs until
    // they become candidates
    SOLTEST_API void ClearVisitedWakeDirty(TArrayView<int32> groupWakeDirty, TConstArrayView<uint8> groupCandidate);

    // Returns an upper bound on how fast a lane ship moves relative to the lane's endpoint A: the lane speed scaled by
    // the lane's current over nominal length (the rail's rate is per nominal length), plus the endpoints' relative
    // speed, which also turns the lateral offset (at most laneRadiusM) about A
    SOLTEST_API double LaneShipSpeedBoundMps(double laneMaxSpeedMps, double nominalLengthM, double currentLengthM,
        double laneRadiusM, double endpointRelativeSpeedMps);

    // ---- Distribution and rails ----

    // Returns a deterministic, uniform value in [0, 1) for (seed, index, stream)
    SOLTEST_API double HashUnit(uint32 seed, uint32 index, uint32 stream);

    // Returns the weighted pick for a unit value, or INDEX_NONE when no weight is positive
    SOLTEST_API int32 PickWeighted(double unit, TConstArrayView<double> weights);

    // Splits a population into battle, lane and cluster ships
    SOLTEST_API FCounts SplitCounts(int32 total, int32 battleShips, double laneFraction);

    // Returns the placement kind and local index of an NPC index
    SOLTEST_API FPlacementSlot ClassifyIndex(int32 npcIndex, const FCounts& counts);

    // Returns the deterministic spawn parameters of one NPC index
    SOLTEST_API FSpawn MakeSpawn(uint32 seed, int32 npcIndex, const FCounts& counts, const FLayout& layout,
        const FBattleParams& battle);

    // Returns a battle centre's offset from the player's frame
    SOLTEST_API FVector3d BattleCenterOffsetM(uint32 seed, int32 battleIndex, const FBattleParams& battle);

    // Returns a period-2 triangle wave in [0, 1]
    SOLTEST_API double TriangleWave01(double x);

    // Returns a lane ship's parameter along its lane at a time
    SOLTEST_API double LaneParamAt(const FSpawn& spawn, double nominalLaneLengthM, double timeS);

    // Returns a deterministic right-handed basis (U, V) perpendicular to an axis
    SOLTEST_API void LaneBasis(const FVector3d& axis, FVector3d& outU, FVector3d& outV);

    // Returns a lane ship's position from the lane endpoints, its parameter and lateral offset
    SOLTEST_API FVector3d LanePositionM(const FVector3d& endpointAM, const FVector3d& endpointBM, double laneParam,
        const FVector2d& lateralOffsetM);

    // Returns a cluster ship's circular-orbit offset from its body
    SOLTEST_API FVector3d ClusterOffsetM(const FSpawn& spawn, double bodyGM, double timeS);

    // Returns the distance from a point to a closed segment
    SOLTEST_API double DistancePointToSegmentM(const FVector3d& p, const FVector3d& a, const FVector3d& b);

    // Returns the distance from a point to a lane capsule (0 inside)
    SOLTEST_API double DistanceToLaneM(const FVector3d& p, const FVector3d& a, const FVector3d& b, double laneRadiusM);

    // Returns the distance from a point to a spherical shell (0 inside)
    SOLTEST_API double DistanceToShellM(const FVector3d& p, const FVector3d& center, double innerRadiusM,
        double outerRadiusM);

    // Returns the default demo layout (lanes between neighbouring planets, clusters around bodies and the belt)
    SOLTEST_API FLayout DefaultLayout();

    // ---- Ramp and frame statistics ----

    // Returns the ramp's stage counts up to maxCount
    SOLTEST_API TArray<int32> BuildRampStages(int32 maxCount);

    // Returns the state that starts a ramp
    SOLTEST_API FRampState StartRamp(int32 stageCount);

    // Advances the ramp by one real frame
    SOLTEST_API FRampStep StepRamp(const FRampState& state, int32 stageCount, double settleS, double measureS,
        double realDtS);

    // Returns the NPC count the ramp wants now, or INDEX_NONE to keep the current count
    SOLTEST_API int32 StageNpcCount(const FRampState& state, TConstArrayView<int32> stages);

    // Returns the nearest-rank percentile of an ascending array
    SOLTEST_API double PercentileNearestRank(TConstArrayView<double> sortedAscending, double fraction);

    // Drops invalid frame times, sorts the rest in place and summarizes them
    SOLTEST_API FFrameStats SummarizeFrameTimes(TArray<double>& inOutFrameTimesMs);

    // Returns whether a stage's 95th-percentile frame time meets a frame-rate target
    SOLTEST_API bool MeetsFrameTarget(const FFrameStats& stats, double targetFps);

    // Returns the log line of one finished ramp stage
    SOLTEST_API FString FormatRampStageLine(int32 npcCount, const FFrameStats& stats, double usedPhysicalMB,
        bool bPass);

    // ---- Demo options ----

    // Returns one descriptor per option, in enum order
    SOLTEST_API TConstArrayView<FOptionDesc> OptionDescriptors();

    // Returns every option at its default
    SOLTEST_API FDemoOptions DefaultOptions();

    // Returns an option's value as a double (bools 0 or 1)
    SOLTEST_API double GetOption(const FDemoOptions& options, EDemoOption id);

    // Clamps every option to its range and applies the cross-option rules
    SOLTEST_API FSanitizeResult SanitizeOptions(const FDemoOptions& options);

    // Sets one option (clamped, rounded) and sanitizes the result
    SOLTEST_API FDemoOptions SetOption(const FDemoOptions& options, EDemoOption id, double value);

    // Steps one option up or down by its step, preset or toggle
    SOLTEST_API FDemoOptions StepOption(const FDemoOptions& options, EDemoOption id, int32 direction);

    // Parses an NPC count such as "250000", "250k", "1M" or "1.5k", or returns INDEX_NONE
    SOLTEST_API int32 ParseNpcCount(const FString& text);
}
