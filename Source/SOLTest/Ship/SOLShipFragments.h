/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"

#include "CoreMinimal.h"
#include "Mass/EntityElementTypes.h"

#include "SOLShipFragments.generated.h"

/** Kinematic state of a ship (Unreal-handed universe frame, meters and m/s), advanced by the flight processor. */
USTRUCT()
struct FSOLShipStateFragment : public FMassFragment
{
    GENERATED_BODY()

    FSOLShipState State;                      // Position, velocity, orientation, angular velocity
    int32 ContactBodyIndex = INDEX_NONE;      // Body the ship rested on during the last step, or INDEX_NONE
    FSOLGMeterState GMeter;                   // Pilot G-tolerance meter and G-LOC state (SDD 18; not saved)
    double GLoadG = 0.0;                      // Pilot G-load (thrust plus surface contact) of the last substep
    FSOLGuidanceOutput Guidance;              // Autopilot guidance of the last substep (default when not engaged)
    bool bAutopilotArrived = false;           // The autopilot reached its target during the last step (never a chase)
};

/** Control input a ship flies with this frame (written by the player pawn, AI or a script). */
USTRUCT()
struct FSOLShipControlFragment : public FMassFragment
{
    GENERATED_BODY()

    FSOLShipControl Control;                  // Thrust, rotation, boost, assist, speed cap, reference velocity
    int32 AlignBodyIndex = INDEX_NONE;        // Surface-lock (SDD 4): body whose local vertical up aligns to, or none
    int32 AutopilotBodyIndex = INDEX_NONE;    // Autopilot order (SDD 18): body to fly to, or none
    FVector3d AutopilotOffsetDir = FVector3d::ZeroVector;  // Arrival side: unit vector from that body's centre
    FVector3d AutopilotPointOffsetM = FVector3d::ZeroVector;  // Point order (SDD 20): target offset from the body
    double AutopilotPassSpeedMps = 0.0;       // Point order: fly-through speed at the target (0 = stop there)
    double AutopilotCaptureRadiusM = 0.0;     // Point order: fly-through capture radius (0 = stop there)
    bool bAutopilotPoint = false;             // The order is a route waypoint (body + fixed offset), not a standoff
};

/**
 * Chase Down order and run state (SDD 32 section 3.8), carried only by the player ship's archetype (the flight
 * processor reads it as optional). The ship subsystem refreshes the order every step before the flight run; the target
 * is interpolated per substep as its frame body (the substep bodies) plus its offset from that body moving at its
 * frame-relative velocity, or with no frame body from its own position and velocity. The processor writes the run state
 * and the last substep's limit (defaults while no chase runs).
 */
USTRUCT()
struct FSOLShipChaseFragment : public FMassFragment
{
    GENERATED_BODY()

    bool bChase = false;                                    // Fly the chase (exclusive with an autopilot body order)
    int32 ChaseFrameBodyIndex = INDEX_NONE;                 // Body the target moves with (a body target: itself)
    FVector3d ChaseTargetM = FVector3d::ZeroVector;         // Target's end-of-frame position
    FVector3d ChaseTargetVelocityMps = FVector3d::ZeroVector;   // Target's velocity
    FVector3d ChaseFrameVelocityMps = FVector3d::ZeroVector;    // Its frame's velocity (the tail direction is relative)
    FVector3d ChaseTargetAccelMps2 = FVector3d::ZeroVector; // Target's own acceleration beyond its frame body's gravity
    FVector3d ChaseApproachDir = FVector3d::ZeroVector;     // Unit target-to-ship direction captured at engage
    double ChaseTrailM = 0.0;                               // Tail distance from the target's centre
    double ChasePlanGravityMps2 = 0.0;                      // Gravity magnitude at the tail point (planning budget)
    FSOLChasePolicy ChasePolicy;                            // The current plan's G levels and the meter floor
    FSOLChaseRunState ChaseRun;                             // Processor-written: cooling flag, last limit, activity
    FSOLChaseGLimit ChaseLimit;                             // Processor-written: last substep's limit, reserve, resume
};

/**
 * Flight-model tunables shared by every ship of one class (a const shared fragment: one copy per distinct value, read
 * once per chunk). Build it with Make so ParamsCrc is set: Mass de-duplicates shared values by the CRC of their
 * reflected properties, and FSOLFlightParams is plain C++, so ParamsCrc is the only reflected field that tells two
 * parameter sets apart. Changing a ship's parameters swaps its shared value (a chunk move, not an archetype change).
 */
USTRUCT()
struct FSOLShipParamsFragment : public FMassConstSharedFragment
{
    GENERATED_BODY()

    // Returns a fragment holding the given parameters with ParamsCrc computed from them
    static FSOLShipParamsFragment Make(const FSOLFlightParams& params)
    {
        FSOLShipParamsFragment fragment;
        fragment.Params = params;
        fragment.ParamsCrc = FCrc::MemCrc32(&params, sizeof(params));
        return fragment;
    }

    FSOLFlightParams Params;                  // Assist time constant, thrust, rotation rates, speed limits, radius

    /** CRC of Params' bytes; the reflected key Mass hashes to share identical parameter sets. */
    UPROPERTY()
    uint32 ParamsCrc = 0;
};

/** Marks the ship the local player flies. */
USTRUCT()
struct FSOLPlayerShipTag : public FMassTag
{
    GENERATED_BODY()
};
