/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "SOLNpcMidProcessor.generated.h"

/**
 * Mid-tier NPC motion (SDD 11 section 3.1, Part 10b; SDD 18 section 3.7): the deviation half of the motion model for
 * Mid ships (Full ships are flown by USOLNpcFullAiProcessor and FSOLNpcCombat). Each Mid ship is a point mass in its
 * rail's frame that flies back to the rail with the Newtonian rendezvous guidance (SOLNpcAi::StepMidReturn) under its
 * class budget (lateral thrust, at most the class G cap), integrated in real time; its position (already rail plus the
 * old deviation from USOLNpcTierProcessor's Evaluate pass) moves by the change. Ships at rest on their rail are
 * skipped. No Mid ship is dead (a dead Full ship respawns when it leaves the Full tier). Parallel chunks; each job
 * writes only its own entities.
 *
 * Not auto-registered: USOLNpcSubsystem runs it after the tier selection's commands are flushed.
 */
UCLASS()
class SOLTEST_API USOLNpcMidProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:

    // Registers the query, opts out of the processing phases and builds the chunk callback once
    USOLNpcMidProcessor();

    // Sets the real delta time of the next run (seconds)
    void SetRealDeltaSeconds(const double realDeltaSeconds) { mRealDeltaSeconds = realDeltaSeconds; }

protected:

    // Declares the fragments the deviation step reads and writes
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& entityManager) override;

    // Steps every Mid chunk in parallel
    virtual void Execute(FMassEntityManager& entityManager, FMassExecutionContext& context) override;

private:

    // Steps the deviations of one chunk and moves the positions by the change
    void ProcessChunk(FMassExecutionContext& context) const;

    FMassEntityQuery mQuery;                    // Mid NPCs: deviation, state (rw), class (shared)
    FMassExecuteFunction mChunkFunction;        // Built once so Execute does not allocate a TFunction
    double mRealDeltaSeconds = 0.0;
};
