/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "MassArchetypeTypes.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "SOLShipFlightProcessor.generated.h"

struct FSOLBodyFrameCache;

// Per-frame inputs the ship subsystem hands the flight processor before each run (Unreal-handed universe frame)
struct FSOLShipFrameInputs
{
    TConstArrayView<FVector3d> PrevBodyPositionsM;          // Last frame's body positions; empty on the first frame
    FVector3d CarryPositionM = FVector3d::ZeroVector;       // Time-warp carry of the reference frame for this frame
    FVector3d CarryVelocityMps = FVector3d::ZeroVector;     // Velocity carry of the reference frame for this frame
    double WarpFactor = 1.0;                                // Sim seconds per real second during this frame
};

/**
 * Advances every ship one frame on REAL time, in substeps of at most SOL::SHIP_MAX_SUBSTEP_S: time-warp carry, gravity
 * and swept sphere collision against the bodies where they were during each substep (SOLFlight::BodyPositionAtSubstep).
 *
 * Time-warp: the bodies moved warp x farther this frame than real-time flight accounts for. The ship subsystem passes
 * the reference frame's carry (SOLFlight::FrameCarryDisplacement for position, and the same for velocity with the
 * frame's average acceleration), which every ship receives: the velocity carry at the frame start, the position carry
 * spread evenly over the substeps, so a ship co-moves with its frame while its own flight stays real-time.
 *
 * Substep bodies interpolate linearly between last frame's and this frame's positions (BodyPositionAtSubstep with the
 * frame's average velocity), computed once per run into a reusable scratch array shared read-only by all chunks. The
 * instantaneous velocity is not used there: under warp the curved orbit makes End - V * warp * dt miss last frame's
 * position by ~r * (w * warp * dt)^2 / 2 (about 6 km for Earth at 1 d/s and 60 fps). Collision uses each body's
 * instantaneous velocity.
 *
 * Chunks run with ParallelForEachEntityChunk: each job writes only its own entities' state; everything shared (body
 * cache, scratch positions, frame inputs, the const shared params) is read-only during the run. The query issues no
 * commands, so per-job command buffers are disabled.
 *
 * Not auto-registered with the Mass processing phases: USOLShipSubsystem runs it with UE::Mass::Executor::Run from
 * USOLAnchorSubsystem's tick, between the body update and the anchor update, which a processing phase cannot
 * guarantee (phases tick in tick groups; the universe update is a tickable subsystem that ticks after them).
 */
UCLASS()
class SOLTEST_API USOLShipFlightProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:

    // Registers the query, opts out of the automatic processing phases and builds the chunk callback once
    USOLShipFlightProcessor();

    // Points the processor at the body data it reads each run and sizes the substep scratch once for it
    void SetBodyCache(const FSOLBodyFrameCache* bodyCache);

    // Sets the per-frame inputs for the next run; the viewed arrays must stay valid until that run ends
    void SetFrameInputs(const FSOLShipFrameInputs& inputs) { mFrame = inputs; }

protected:

    // Declares the fragments the flight step reads and writes
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& entityManager) override;

    // Prepares the substep body positions, then steps every matching chunk in parallel with the context's delta time
    virtual void Execute(FMassEntityManager& entityManager, FMassExecutionContext& context) override;

private:

    // Fills the scratch array with every body's position at each substep boundary of the frame
    void PrepareSubstepBodyPositions(double frameS);

    // Steps the ships of one chunk: carry, gravity, flight step and swept collision per substep
    void ProcessChunk(FMassExecutionContext& context) const;

    FMassEntityQuery mQuery;                            // Ships: state (rw), control (ro), params (const shared)
    FMassExecuteFunction mChunkFunction;                // Built once so Execute does not allocate a TFunction
    const FSOLBodyFrameCache* mBodyCache = nullptr;     // Unreal-handed body data, owned by the registry subsystem
    FSOLShipFrameInputs mFrame;                         // Carry, warp and last frame's bodies for the current run
    TArray<FVector3d> mSubstepBodyPositionsM;           // [boundary * bodyCount + body], sized once, rewritten per run
    FVector3d mCarryPerSubstepM = FVector3d::ZeroVector; // Position carry added after each substep
    double mSubstepS = 0.0;                             // Substep length for the current run
    int32 mSubstepCount = 0;                            // Substeps for the current run
};
