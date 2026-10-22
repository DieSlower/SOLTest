/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Combat/SOLCombatGrid.h"
#include "Npc/SOLNpcAiMath.h"

#include "CoreMinimal.h"

// One Full-tier NPC ship as gathered for the frame (positions and velocities relative to the frame body)
struct FSOLNpcAiShip
{
    FVector3d PositionM = FVector3d::ZeroVector;            // This frame's position (rail plus deviation)
    FVector3d VelocityMps = FVector3d::ZeroVector;          // Velocity relative to the frame body (per real second)
    FVector3d DeviationM = FVector3d::ZeroVector;           // Deviation before the step
    FVector3d DeviationVelocityMps = FVector3d::ZeroVector;
    FVector3d NewDeviationM = FVector3d::ZeroVector;        // Deviation after the step (written by Step)
    FVector3d NewDeviationVelocityMps = FVector3d::ZeroVector;
    FVector3d AimDirection = FVector3d::ZeroVector;         // Gun aim for this frame's shots
    FVector3d RailVelocityMps = FVector3d::ZeroVector;      // The rail's velocity relative to its group's anchors
    FVector3d UpHint = FVector3d::ZeroVector;               // Away from the group's body (the attitude's desired up)
    FQuat4d Orientation = FQuat4d::Identity;                // Attitude (read and written by Step; rail-frame axes =
    FVector3d AngularVelocityRadPerS = FVector3d::ZeroVector; // universe ecliptic axes)
    int32 NpcIndex = INDEX_NONE;
    int32 TargetNpc = INDEX_NONE;
    int32 ClassIndex = 0;
    int32 Shots = 0;                                        // Shots fired this frame (written by Step)
    float Health = 0.0f;
    float RespawnS = 0.0f;
    float CooldownS = 0.0f;
    float CameraDistanceM = 0.0f;
    uint8 Faction = 0;
    uint8 bAlive = 1;
    uint8 bRespawned = 0;                                   // Respawned this frame: back on its rail, no AI yet
};

// What an NPC combat event reports (for the pooled effects near the camera)
enum class ESOLNpcCombatEventType : uint8
{
    Hit,            // A bolt (or a statistical shot) struck a ship
    Killed,         // A ship died (explosion)
};

// One NPC combat event, relative to the frame body
struct FSOLNpcCombatEvent
{
    ESOLNpcCombatEventType Type = ESOLNpcCombatEventType::Hit;
    FVector3d PositionM = FVector3d::ZeroVector;
    FVector3d DirectionM = FVector3d::ZeroVector;           // The bolt's direction for hits, else zero
};

// Per-frame inputs of FSOLNpcCombat::Step
struct FSOLNpcCombatInputs
{
    double DeltaS = 0.0;                                    // Real seconds
    FVector3d CameraPositionM = FVector3d::ZeroVector;      // Relative to the frame body
    SOLNpcAi::ECombatModel Model = SOLNpcAi::ECombatModel::Bolts;
    bool bCombatEnabled = true;
    uint32 Seed = 0;
};

// NPC combat statistics of the last step
struct FSOLNpcCombatStats
{
    int32 Ships = 0;
    int32 Alive = 0;
    int32 LiveBolts = 0;
    int32 RealBolts = 0;                                    // Swept bolts among the live ones
    int32 Shots = 0;                                        // Shots fired this frame
    int32 StatisticalShots = 0;
    int32 Hits = 0;
    int32 Kills = 0;
    int32 Respawns = 0;
    int32 DroppedBolts = 0;                                 // Shots dropped this frame: the bolt pool was full
    double AiMs = 0.0;                                      // Grids plus the parallel AI step
    double BoltMs = 0.0;                                    // Bolt advance, sweeps and hit resolution
    double FireMs = 0.0;                                    // New shots (bolt spawns and statistical rolls)
};

/**
 * Full-tier NPC AI and NPC-vs-NPC combat (SDD 11 section 3.6, Part 10d): owns the per-frame ship list gathered by
 * USOLNpcFullAiProcessor, flies every ship in parallel (SOLNpcAi: pursuit, separation through a spatial grid and
 * leash give a desired velocity; SOLNpcAi::StepFullShip flies it 6-DOF under the class thrust envelope with the nose
 * on the lead aim near the target, SDD 18 section 3.7), runs the guns (SOLFireCadence, fired inside a cone around the
 * nose), and resolves shots with the selected combat model: real bolts swept against the ships (a structure-of-arrays
 * pool reusing SOLBoltMath and SOLCombatGrid, like USOLCombatSubsystem), statistical hits with cosmetic tracers near the camera, or the hybrid of both. Everything is
 * relative to one frame body (the battles' body), in real time. Plain C++, owned by USOLNpcSubsystem; NPCs never touch
 * the player's combat (SDD 11 decision 4).
 */
class SOLTEST_API FSOLNpcCombat
{
public:

    // Creates the grids and reserves the pools
    FSOLNpcCombat();

    // Clears every ship, bolt and event, and sizes the population index map
    void Reset(int32 populationCount);

    // Starts a frame's gather: forgets last frame's ships (keeps allocations)
    void BeginGather();

    // Sizes this frame's ship list (every slot must then be written with SetShip)
    void SetShipCount(int32 count);

    // Writes one gathered ship into its slot and maps its population index to it (thread-safe for distinct slots)
    void SetShip(int32 slot, const FSOLNpcAiShip& ship);

    // Steers, fires and resolves combat for the gathered ships
    void Step(const FSOLNpcCombatInputs& inputs);

    // Returns the gathered ship of a population index after Step, or nullptr
    const FSOLNpcAiShip* FindShip(int32 npcIndex) const;

    // Ends the frame: clears the population index map entries of this frame's ships
    void EndFrame();

    // Returns the live bolts (relative to the frame body) for the renderer
    TConstArrayView<FVector3d> GetBoltPositionsM() const { return mBoltPositionsM; }
    TConstArrayView<FVector3d> GetBoltVelocitiesMps() const { return mBoltVelocitiesMps; }

    // Returns this frame's events (relative to the frame body)
    TConstArrayView<FSOLNpcCombatEvent> GetEvents() const { return mEvents; }

    // Returns the last step's statistics
    const FSOLNpcCombatStats& GetStats() const { return mStats; }

private:

    // Per-task scratch of the parallel passes (kept across frames, so the queries allocate nothing once warm)
    struct FScratch
    {
        TArray<int32> Ids;
        TArray<SOLNpcAi::FTargetCandidate> Candidates;
        TArray<FVector3d> Neighbours;
    };

    // Flies one ship and decides its shots (parallel; reads the other ships, writes only this one)
    void StepShip(FScratch& scratch, int32 slot, const FSOLNpcCombatInputs& inputs);

    // Advances, sweeps and resolves the bolts
    void StepBolts(double deltaS);

    // Turns this frame's shots into bolts or statistical hits
    void FireShots(const FSOLNpcCombatInputs& inputs);

    // Damages a ship and records the hit (and the kill)
    void DamageShip(int32 slot, double damage, const FVector3d& positionM, const FVector3d& directionM);

    // Appends a bolt to the pool (dropped when full)
    void AddBolt(const FVector3d& positionM, const FVector3d& velocityMps, double lifetimeS, float damage,
        uint8 faction, bool bReal);

    // Removes the bolt at an index (swap with the last)
    void RemoveBoltAtSwap(int32 bolt);

    // Appends an event (dropped beyond the per-frame cap)
    void AddEvent(ESOLNpcCombatEventType type, const FVector3d& positionM, const FVector3d& directionM);

    TArray<FSOLNpcAiShip> mShips;
    TArray<int32> mSlotOfNpc;                       // Population index -> this frame's ship slot, or INDEX_NONE
    SOLCombatGrid::FGrid mTargetGrid;               // Alive ships, coarse (target search)
    SOLCombatGrid::FGrid mShipGrid;                 // Alive ships' swept hit spheres, fine (separation, bolt sweeps)
    TArray<FSOLThrustEnvelope> mClassEnvelopes;     // SOLNpcAi::ClassEnvelope per class (built once)
    TArray<FScratch> mScratch;                      // One per parallel task

    // Bolt pool (structure of arrays, relative to the frame body)
    TArray<FVector3d> mBoltPositionsM;
    TArray<FVector3d> mBoltVelocitiesMps;
    TArray<float> mBoltLifetimeS;
    TArray<float> mBoltDamage;
    TArray<uint8> mBoltFaction;
    TArray<uint8> mBoltReal;                        // 1: swept against the ships; 0: cosmetic tracer
    TArray<int32> mBoltHitSlot;                     // This frame's sweep result per bolt (ship slot or INDEX_NONE)
    TArray<FVector3d> mBoltHitPositionM;

    TArray<FSOLNpcCombatEvent> mEvents;
    FSOLNpcCombatStats mStats;
    uint32 mFrame = 0;
};
