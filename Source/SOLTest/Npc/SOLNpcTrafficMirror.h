/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

// The C++ float copy of the point shader's lane evaluation, SOLNTR_LaneShipRelM in Shaders/Private/SOLNpcTraffic.ush
// (SDD 50 section 4.6), statement for statement and in the same order, so the shader's precision can be measured on
// the CPU. One copy serves SOLTest.NpcRail.GpuLaneFloatMirror / GpuLaneSmoothness, the -SOLSmokeNpcTraffic hand-over
// check and ASOLNpcVisuals' CPU point hand-over (SDD 50 risk R9: no second copy to drift). A change to the shader must
// change this function.
// Float model: the error-free sums (the inbound boundary's Fast2Sum, the order of the large and moving terms) hold only
// under /fp:precise, which UBT uses for editor targets (the tests and the smoke run there; the shader keeps them with
// precise). A game build compiles with /fp:fast, which may fold them: the visuals' hand-over then sees a rounding of
// the epoch terms (milliseconds of motion), far below its blend
namespace SOLNpcTrafficMirror
{
    // Where the CPU point layer draws a lane ship (CpuPointHandOver)
    struct FHandOver
    {
        bool bCpuDraws = false;        // Inside the Mid radius by the shader's own test (else the far layer draws it)
        FVector3f RelM = FVector3f::ZeroVector; // The drawn position relative to the viewpoint (m, ecliptic)
    };

    // Returns the CPU point hand-over of a lane ship (SDD 50 section 4.6): the CPU draws it exactly where the point
    // shader hides it (the float mirror's length below the Mid radius, the shader's length(relM) < P0.y), at the
    // mirror's position at the Mid radius, smoothstepped to the double position cpuRelM by hullStartM (the instanced
    // hull radius), so the hand-over neither pops nor doubles or drops the point
    inline FHandOver CpuPointHandOver(const FVector3f& mirrorRelM, const FVector3d& cpuRelM, const float midRadiusM,
        const double hullStartM)
    {
        FHandOver result;
        result.RelM = mirrorRelM;
        const float mirrorM = mirrorRelM.Size();
        result.bCpuDraws = mirrorM < midRadiusM;
        if (result.bCpuDraws)
        {
            const double midM = static_cast<double>(midRadiusM);
            const double mirrorWeight = FMath::SmoothStep(FMath::Min(hullStartM, midM), midM,
                static_cast<double>(mirrorM));
            result.RelM = FVector3f(cpuRelM + (FVector3d(mirrorRelM) - cpuRelM) * mirrorWeight);
        }
        return result;
    }

    // Returns the end-zone ramp P(s) = s^3 - s^4 / 2 in float (SOLNTR_Ramp)
    inline float Ramp(const float s)
    {
        return s * s * s - 0.5f * s * s * s * s;
    }

    // Returns a lane ship's position relative to the viewpoint as the point shader computes it, all in float: railA /
    // railB the ship's two GPU rail texels (SOLNpcRail::GpuRailTexels), rows the lane column's six group texels as
    // uploaded (rows 0-1 ASOLNpcVisuals, rows 2-5 SOLNpcRail::WriteLaneTrafficTexels), timeS the shader's rail time
    // since the GPU epoch
    inline FVector3f LaneShipRelM(const FVector4f& railA, const FVector4f& railB, const FVector4f* rows,
        const float timeS)
    {
        const FVector3f a(rows[0].X, rows[0].Y, rows[0].Z);
        const FVector3f b(rows[1].X, rows[1].Y, rows[1].Z);
        const FVector3f up(rows[2].X, rows[2].Y, rows[2].Z);
        const float wallMps = rows[2].W;
        const FVector3f right(rows[3].X, rows[3].Y, rows[3].Z);
        const float uTurnDepthM = rows[3].W;
        const float piOverTurn = rows[4].X;
        const float endArcAM = rows[4].Y;
        const float nominalLegM = rows[4].Z;
        const float zoneM = rows[4].W;
        const float endArcBM = rows[5].X;
        const float turnS = rows[5].Y;
        const float stretch = rows[5].Z;
        const float inverseZone = rows[5].W;
        const float tauHighS = railA.Y;
        const float dv = railA.Z;
        const float upOffsetM = railA.W;
        const float rightOffsetM = railB.X;
        const float accelS = railB.Y;
        const float tauLowS = railB.Z;
        const float legS = railB.W;

        // A lane that is not drivable or a ship without a leg: the midpoint disc at rest
        if (!(nominalLegM > 0.0f && legS > 0.0f))
        {
            return 0.5f * (a + b) + up * upOffsetM + right * rightOffsetM;
        }

        // The ship's leg constants
        const FVector3f axis = FVector3f::CrossProduct(up, right);
        const float peak = wallMps + dv;
        const float alpha = accelS > 0.0f ? dv / accelS : 0.0f;

        // T_leg's float rounding from the packed value's fraction (SOLNpcRail::GpuRailTexels)
        const float legLowS = (railA.X - FMath::RoundHalfToEven(railA.X))
            * (2.0f * FMath::AsFloat(FMath::AsUInt(legS) & 0x7F800000u) * 1.1920928955078125e-7f);

        // Segment constants, one float subtraction at a time; the inbound start's rounding and T_leg's carried
        const float movingS = tauLowS + timeS;
        const float wraps = FMath::Max(FMath::FloorToFloat((tauHighS + movingS) / (2.0f * (legS + turnS))), 0.0f);
        const float cycleConstS = (tauHighS - wraps * (2.0f * legS)) - wraps * (2.0f * turnS);
        const float turnBConstS = cycleConstS - legS;
        const float inboundConstS = turnBConstS - turnS;
        const float inboundLowS = (turnBConstS - inboundConstS) - turnS;
        const float turnAConstS = inboundConstS - legS;
        const float cycleMovingS = movingS - wraps * (2.0f * legLowS);
        const float turnBMovingS = cycleMovingS - legLowS;
        const float inboundMovingS = turnBMovingS + inboundLowS;
        const float turnAMovingS = inboundMovingS - legLowS;
        const bool bSegment0 = turnBConstS + turnBMovingS < 0.0f;
        const bool bSegment1 = !bSegment0 && inboundConstS + inboundMovingS < 0.0f;
        const bool bSegment2 = !bSegment0 && !bSegment1 && turnAConstS + turnAMovingS < 0.0f;
        const bool bLeg = bSegment0 || bSegment2;
        const float segmentConstS = bSegment0 ? cycleConstS
            : (bSegment1 ? turnBConstS : (bSegment2 ? inboundConstS : turnAConstS));
        const float segmentMovingS = bSegment0 ? cycleMovingS
            : (bSegment1 ? turnBMovingS : (bSegment2 ? inboundMovingS : turnAMovingS));
        const float lengthS = bLeg ? legS : turnS;
        const float toEndConstS = lengthS - segmentConstS;
        const float toEndMovingS = (bLeg ? legLowS : 0.0f) - segmentMovingS;
        const float fromStartS = FMath::Clamp(segmentConstS + segmentMovingS, 0.0f, lengthS);
        const float fromEndS = FMath::Clamp(toEndConstS + toEndMovingS, 0.0f, lengthS);

        // U-turns, both evaluated and selected
        const float theta = fromStartS * piOverTurn;
        const float sinTheta = FMath::Sin(theta);
        const float cosTheta = FMath::Cos(theta);
        const FVector3f turnB = b - axis * (endArcBM - uTurnDepthM * sinTheta) + up * upOffsetM
            + right * (cosTheta * rightOffsetM);
        const FVector3f turnA = a + axis * (endArcAM - uTurnDepthM * sinTheta) + up * upOffsetM
            - right * (cosTheta * rightOffsetM);

        // Legs: the arc from the nearer end ring (in time) as the epoch constant plus the moving part (accelerating
        // while |m| <= s, else direct from s; coasting always)
        const bool bFirstHalf = fromStartS <= fromEndS;
        const float s = bFirstHalf ? fromStartS : fromEndS;
        const float sConstS = bFirstHalf ? segmentConstS : toEndConstS;
        const float sMovingS = bFirstHalf ? segmentMovingS : toEndMovingS;
        const bool bAccelerate = s <= accelS;
        const bool bSplit = FMath::Abs(sMovingS) <= s;
        const float arcConstM = bAccelerate ? (bSplit ? wallMps * sConstS + 0.5f * alpha * sConstS * sConstS
            : wallMps * s + 0.5f * alpha * s * s) : peak * sConstS - 0.5f * dv * accelS;
        const float arcMovingM = bAccelerate ? (bSplit ? (wallMps + alpha * sConstS) * sMovingS
            + 0.5f * alpha * sMovingS * sMovingS : 0.0f) : peak * sMovingS;
        const float arcM = arcConstM + arcMovingM;

        // The metric map, split the same way: in a zone by the ramp's exact difference quotient while |d| <= 1
        const bool bZone = arcM < zoneM;
        const float scale = 1.0f + stretch;
        const float beyondZoneM = 0.5f * stretch * zoneM;
        const float u = arcConstM * inverseZone;
        const float d = arcMovingM * inverseZone;
        const bool bZoneSplit = bSplit && FMath::Abs(d) <= 1.0f;
        const float zoneSlope = (3.0f - 2.0f * u) * u * u
            + d * ((3.0f - 3.0f * u) * u + d * ((1.0f - 2.0f * u) - 0.5f * d));
        const float fromRingConstM = bZone ? (bZoneSplit ? arcConstM + stretch * zoneM * Ramp(u)
            : arcM + stretch * zoneM * Ramp(arcM * inverseZone)) : scale * arcConstM - beyondZoneM;
        const float fromRingMovingM = bZone ? (bZoneSplit ? arcMovingM * (1.0f + stretch * zoneSlope) : 0.0f)
            : scale * arcMovingM;

        // From that end: the large terms first, then the lateral and the moving part
        const bool bOutbound = bSegment0;
        const FVector3f lateral = up * upOffsetM + right * (bOutbound ? rightOffsetM : -rightOffsetM);
        const bool bFromA = bOutbound == bFirstHalf;
        const FVector3f ringPosition = bFromA ? a + axis * (endArcAM + fromRingConstM)
            : b - axis * (endArcBM + fromRingConstM);
        const FVector3f legPosition = (ringPosition + lateral)
            + axis * (bFromA ? fromRingMovingM : -fromRingMovingM);
        return bSegment1 ? turnB : (bLeg ? legPosition : turnA);
    }

    // Returns whether the point shader draws a lane ship at timeS by its slot's window (SOLNTR_LaneShipWindowDrawn, SDD
    // 53 section 5.4, contract case 25): railA / railB its GPU rail texels, rows the column's NPC_GROUP_TEXTURE_ROWS
    // group texels as uploaded (row 6 = (open start, close, active, 0) relative to the GPU epoch; the active flag is
    // not read here). The float form of SOLLaneNetwork::LaneShipInSpace: on a leg that leg decides, in a U-turn the leg
    // after it (P1); drawn iff the deciding leg departs at or after the open start and arrives by the close. The
    // departure is built from per-epoch constants only (the epoch parts, tau0 low, T_leg's rounding and the integer
    // wrap count, never t), so the decision is the same on every frame of a span; a turn's departure is the same float
    // expression as the leg after it (TurnAtB: the inbound constants, TurnAtA: the outbound constants of the next wrap)
    inline bool LaneShipWindowDrawn(const FVector4f& railA, const FVector4f& railB, const FVector4f* rows,
        const float timeS)
    {
        const float nominalLegM = rows[4].Z;
        const float turnS = rows[5].Y;
        const float openStartS = rows[6].X;
        const float closeS = rows[6].Y;
        const float tauHighS = railA.Y;
        const float tauLowS = railB.Z;
        const float legS = railB.W;

        // A parked ship (free-pool block, negative leg time) is never drawn; a lane that is not drivable or a ship
        // without a leg (the midpoint disc) is drawn while the window is open
        if (legS < 0.0f)
        {
            return false;
        }
        if (!(nominalLegM > 0.0f && legS > 0.0f))
        {
            return openStartS <= timeS && timeS < closeS;
        }

        // T_leg's float rounding and the segment selection, as SOLNTR_LaneShipRelM computes them
        const float legLowS = (railA.X - FMath::RoundHalfToEven(railA.X))
            * (2.0f * FMath::AsFloat(FMath::AsUInt(legS) & 0x7F800000u) * 1.1920928955078125e-7f);
        const float movingS = tauLowS + timeS;
        const float wraps = FMath::Max(FMath::FloorToFloat((tauHighS + movingS) / (2.0f * (legS + turnS))), 0.0f);
        const float cycleConstS = (tauHighS - wraps * (2.0f * legS)) - wraps * (2.0f * turnS);
        const float turnBConstS = cycleConstS - legS;
        const float inboundConstS = turnBConstS - turnS;
        const float inboundLowS = (turnBConstS - inboundConstS) - turnS;
        const float turnAConstS = inboundConstS - legS;
        const float cycleMovingS = movingS - wraps * (2.0f * legLowS);
        const float turnBMovingS = cycleMovingS - legLowS;
        const float inboundMovingS = turnBMovingS + inboundLowS;
        const float turnAMovingS = inboundMovingS - legLowS;
        const bool bSegment0 = turnBConstS + turnBMovingS < 0.0f;
        const bool bSegment1 = !bSegment0 && inboundConstS + inboundMovingS < 0.0f;
        const bool bSegment2 = !bSegment0 && !bSegment1 && turnAConstS + turnAMovingS < 0.0f;

        // The deciding leg's departure relative to the GPU epoch from the moving parts without t: the outbound leg of
        // this wrap (segment 0), the inbound leg (segments 1 and 2), or the outbound leg of the next wrap (segment 3)
        const float nextWraps = wraps + 1.0f;
        const float nextCycleConstS = (tauHighS - nextWraps * (2.0f * legS)) - nextWraps * (2.0f * turnS);
        const float cycleLowS = tauLowS - wraps * (2.0f * legLowS);
        const float inboundLowMovingS = (cycleLowS - legLowS) + inboundLowS;
        const float nextCycleLowS = tauLowS - nextWraps * (2.0f * legLowS);
        const float departS = bSegment0 ? -cycleConstS - cycleLowS
            : ((bSegment1 || bSegment2) ? -inboundConstS - inboundLowMovingS : -nextCycleConstS - nextCycleLowS);
        const float arriveS = (departS + legS) + legLowS;
        return departS >= openStartS && arriveS <= closeS;
    }

    // Returns whether the point vertex shader draws a lane ship at all (SOLNTR_LaneShipDrawn; SDD 53 Q21, Q22, P5; plan
    // 53d step 4): the slot's active flag (row 6 .z, 1 = active) and LaneShipWindowDrawn; an inactive lane's ships are
    // hidden always, with the Lanes overlay on or off (no overlay input)
    inline bool LaneShipPointDrawn(const FVector4f& railA, const FVector4f& railB, const FVector4f* rows,
        const float timeS)
    {
        return rows[SOL::NPC_GROUP_WINDOW_ROW].Z > 0.5f && LaneShipWindowDrawn(railA, railB, rows, timeS);
    }
}
