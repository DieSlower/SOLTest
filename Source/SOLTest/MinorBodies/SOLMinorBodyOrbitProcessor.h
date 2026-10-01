/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "MassArchetypeTypes.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "SOLMinorBodyOrbitProcessor.generated.h"

/**
 * Places every minor body (SDD 6 Appendix B) at the sim clock's current time: purely kinematic, one Kepler solve per
 * entity per run (SOLMinorBodyOrbit::ComputePositionM), with no gravity, collision or control input. Positions are in
 * raw ecliptic universe meters, offset from the parent's position in FSOLBodyRegistry::GetPositionsM (not the
 * Unreal-handed FSOLBodyFrameCache, which is the ship-physics frame), so the render placement converts them exactly
 * once, like every registry body.
 *
 * Chunks run with ParallelForEachEntityChunk: each job writes only its own entities' state fragment; the parent
 * positions and the time are read-only during the run. The query issues no commands, so per-job command buffers are
 * disabled.
 *
 * Not auto-registered with the Mass processing phases: USOLMinorBodySubsystem runs it with UE::Mass::Executor::Run
 * from USOLAnchorSubsystem's OnBodiesUpdated, after the bodies advance and before OnUniverseUpdated, so the belt
 * visuals read this frame's positions.
 */
UCLASS()
class SOLTEST_API USOLMinorBodyOrbitProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:

    // Registers the query, opts out of the automatic processing phases and builds the chunk callback once
    USOLMinorBodyOrbitProcessor();

    // Sets the parents' positions (raw ecliptic meters, registry order) for the next run; the view must outlive it
    void SetParentPositionsM(const TConstArrayView<FVector3d> parentPositionsM)
    {
        mParentPositionsM = parentPositionsM;
    }

    // Sets the sim time (seconds since J2000) the next run places the bodies at
    void SetSecondsSinceJ2000(const double secondsSinceJ2000) { mSecondsSinceJ2000 = secondsSinceJ2000; }

protected:

    // Declares the fragments the orbit step reads and writes
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& entityManager) override;

    // Places every matching chunk's bodies in parallel
    virtual void Execute(FMassEntityManager& entityManager, FMassExecutionContext& context) override;

private:

    // Places the bodies of one chunk at the current time, relative to their parents
    void ProcessChunk(FMassExecutionContext& context) const;

    FMassEntityQuery mQuery;                            // Minor bodies: orbit (ro), state (rw)
    FMassExecuteFunction mChunkFunction;                // Built once so Execute does not allocate a TFunction
    TConstArrayView<FVector3d> mParentPositionsM;       // Registry body positions (raw ecliptic meters) for this run
    double mSecondsSinceJ2000 = 0.0;                    // Sim time for this run
};
