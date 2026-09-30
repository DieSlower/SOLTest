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
};

/** Control input a ship flies with this frame (written by the player pawn, AI or a script). */
USTRUCT()
struct FSOLShipControlFragment : public FMassFragment
{
    GENERATED_BODY()

    FSOLShipControl Control;                  // Thrust, rotation, boost, assist, speed cap, reference velocity
    int32 AlignBodyIndex = INDEX_NONE;        // Surface-lock (SDD 4): body whose local vertical up aligns to, or none
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
