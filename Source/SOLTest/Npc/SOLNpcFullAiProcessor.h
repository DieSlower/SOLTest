/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"

#include "SOLNpcFullAiProcessor.generated.h"

class FSOLNpcCombat;
struct FSOLNpcCombatFragment;
struct FSOLNpcDeviationFragment;
struct FSOLNpcRailFragment;
struct FSOLNpcStateFragment;

// What one run of USOLNpcFullAiProcessor does (the subsystem runs Gather, steps FSOLNpcCombat, then runs Scatter)
enum class ESOLNpcFullAiPass : uint8
{
    Gather,         // Full ships' positions, velocities, deviations and combat state into FSOLNpcCombat
    Scatter,        // FSOLNpcCombat's new deviations and combat state back into the fragments
};

/**
 * Full-tier NPC AI (SDD 11 section 3.3, Part 10d): moves the Full ships between their Mass fragments and FSOLNpcCombat,
 * which steers them (pursuit, avoidance, leash, acceleration and turn limits) and runs their guns in parallel over a
 * flat ship list. Gathering into a flat list lets every ship see every other ship's position through one spatial grid;
 * the gather and scatter run in parallel over the chunks, each chunk at fixed slots (chunk order, so deterministic).
 * The deviation is the only motion the AI writes (the swappable motion seam, SDD 11 Amendment 1): position = rail
 * plus deviation.
 *
 * Not auto-registered: USOLNpcSubsystem runs it after the tier selection's commands are flushed.
 */
UCLASS()
class SOLTEST_API USOLNpcFullAiProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:

    // Per-run inputs
    struct FRunInputs
    {
        FSOLNpcCombat* Combat = nullptr;
        FVector3d FrameBodyM = FVector3d::ZeroVector;       // The frame body's position this frame (universe metres)
        FVector3d FrameBodyStepM = FVector3d::ZeroVector;   // Its move since last frame
        bool bHasFrameBodyStep = false;
        double RealDeltaS = 0.0;
    };

    // Registers the query and opts out of the processing phases
    USOLNpcFullAiProcessor();

    // Sets the next run's pass and inputs
    void SetPass(const ESOLNpcFullAiPass pass, const FRunInputs& inputs)
    {
        mPass = pass;
        mInputs = inputs;
    }

protected:

    // Declares the fragments both passes use
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& entityManager) override;

    // Runs the current pass (serial chunk walk, then parallel over the chunks)
    virtual void Execute(FMassEntityManager& entityManager, FMassExecutionContext& context) override;

private:

    // One Full chunk: raw fragment arrays (valid during the pass) and its first slot in the combat list
    struct FChunkView
    {
        const FSOLNpcRailFragment* Rails = nullptr;
        FSOLNpcStateFragment* States = nullptr;
        FSOLNpcDeviationFragment* Deviations = nullptr;
        FSOLNpcCombatFragment* Combats = nullptr;
        int32 Count = 0;
        int32 ClassIndex = 0;
        int32 FirstSlot = 0;
    };

    // Writes one chunk's ships into their combat-list slots
    void GatherChunk(const FChunkView& view) const;

    // Writes one chunk's ships back from the combat list
    void ScatterChunk(const FChunkView& view) const;

    FMassEntityQuery mQuery;                    // Full NPCs: rail (ro), state, deviation, combat (rw), class (shared)
    TArray<FChunkView> mChunks;                 // This pass's chunks (reused every frame)
    ESOLNpcFullAiPass mPass = ESOLNpcFullAiPass::Gather;
    FRunInputs mInputs;
};
