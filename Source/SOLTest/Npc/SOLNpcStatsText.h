/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Npc/SOLNpcScaleMath.h"

// Pure-logic text of the NPC stats overlay and the Demo page's numbers (SDD 11 section 3.8, Part 10e)
namespace SOLNpcStatsText
{
    // Everything the overlay shows (non-finite doubles count as 0)
    struct FOverlayInput
    {
        double FrameMs = 0.0;
        double FrameP95Ms = 0.0;
        double GameMs = 0.0;
        double RenderMs = 0.0;
        double GpuMs = 0.0;
        double NpcMs = 0.0;
        int32 Total = 0;
        int32 Far = 0;
        int32 Mid = 0;
        int32 Full = 0;
        int32 Point = 0;
        int32 Instanced = 0;
        int32 Mesh = 0;
        int32 Awake = 0;
        int32 TierChanges = 0;
        bool bCombat = true;
        int32 CombatModel = 0;
        int32 CombatShips = 0;
        int32 Alive = 0;
        int32 LiveBolts = 0;
        int32 RealBolts = 0;
        double UsedMemoryMB = 0.0;
        SOLNpcScale::ERampPhase RampPhase = SOLNpcScale::ERampPhase::Idle;
        int32 RampStageIndex = INDEX_NONE;
        int32 RampStageCount = 0;
        int32 RampStageNpcCount = 0;
        double RampPhaseElapsedS = 0.0;
        double RampPhaseLengthS = 0.0;
        FString RampResult;    // The last finished stage (FormatRampResult), empty if none
    };

    // Returns an integer with ',' between groups of three digits ("1,000,000")
    SOLTEST_API FString FormatGroupedInteger(int64 value);

    // Returns a finished ramp stage's short result ("250,000 ships p95 5.65 ms PASS")
    SOLTEST_API FString FormatRampResult(int32 npcCount, double p95Ms, bool bPass);

    // Returns a combat model's lower-case name (bolts, statistical, hybrid)
    SOLTEST_API FString CombatModelName(int32 model);

    // Returns the overlay's eight lines
    SOLTEST_API TArray<FString> FormatOverlayLines(const FOverlayInput& input);
}
