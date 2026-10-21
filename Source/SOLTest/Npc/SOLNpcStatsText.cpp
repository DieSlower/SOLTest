/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcStatsText.h"

#include "Npc/SOLNpcAiMath.h"

namespace
{
    // Lines of the overlay
    constexpr int32 STATS_TEXT_LINE_COUNT = 8;

    //////////////////////////////////////////////////////////////////////////
    // Returns the value, or 0 when it is NaN or infinite
    double StatsTextFinite(const double value)
    {
        return FMath::IsFinite(value) ? value : 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns an integer with ',' between groups of three digits
FString SOLNpcStatsText::FormatGroupedInteger(const int64 value)
{
    // Work on the magnitude as unsigned so INT64_MIN has no overflow
    const bool bNegative = value < 0;
    uint64 magnitude = bNegative ? static_cast<uint64>(-(value + 1)) + 1u : static_cast<uint64>(value);
    TCHAR buffer[32];
    int32 cursor = UE_ARRAY_COUNT(buffer) - 1;
    buffer[cursor] = TEXT('\0');
    int32 digits = 0;
    do
    {
        if (digits > 0 && digits % 3 == 0)
        {
            buffer[--cursor] = TEXT(',');
        }
        buffer[--cursor] = static_cast<TCHAR>(TEXT('0') + magnitude % 10u);
        magnitude /= 10u;
        ++digits;
    }
    while (magnitude > 0u);
    if (bNegative)
    {
        buffer[--cursor] = TEXT('-');
    }
    return FString(&buffer[cursor]);
}

//////////////////////////////////////////////////////////////////////////
// Returns a finished ramp stage's short result
FString SOLNpcStatsText::FormatRampResult(const int32 npcCount, const double p95Ms, const bool bPass)
{
    return FString::Printf(TEXT("%s ships p95 %.2f ms %s"), *FormatGroupedInteger(npcCount), StatsTextFinite(p95Ms),
        bPass ? TEXT("PASS") : TEXT("FAIL"));
}

//////////////////////////////////////////////////////////////////////////
// Returns a combat model's lower-case name
FString SOLNpcStatsText::CombatModelName(const int32 model)
{
    switch (SOLNpcAi::CombatModelFromInt(model))
    {
    case SOLNpcAi::ECombatModel::Statistical:
        return TEXT("statistical");
    case SOLNpcAi::ECombatModel::Hybrid:
        return TEXT("hybrid");
    case SOLNpcAi::ECombatModel::Bolts:
    default:
        return TEXT("bolts");
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the overlay's eight lines
TArray<FString> SOLNpcStatsText::FormatOverlayLines(const FOverlayInput& input)
{
    TArray<FString> lines;
    lines.Reserve(STATS_TEXT_LINE_COUNT);
    const double frameMs = StatsTextFinite(input.FrameMs);
    const double fps = frameMs > 0.0 ? 1000.0 / frameMs : 0.0;
    lines.Add(FString::Printf(TEXT("FPS %.0f  frame %.2f ms  p95 %.2f ms"), fps, frameMs,
        StatsTextFinite(input.FrameP95Ms)));
    lines.Add(FString::Printf(TEXT("game %.2f  render %.2f  GPU %.2f  NPC %.2f ms"), StatsTextFinite(input.GameMs),
        StatsTextFinite(input.RenderMs), StatsTextFinite(input.GpuMs), StatsTextFinite(input.NpcMs)));
    lines.Add(FString::Printf(TEXT("NPCs %s  far %s  mid %s  full %s"), *FormatGroupedInteger(input.Total),
        *FormatGroupedInteger(input.Far), *FormatGroupedInteger(input.Mid), *FormatGroupedInteger(input.Full)));
    lines.Add(FString::Printf(TEXT("render  point %s  instanced %s  mesh %s"), *FormatGroupedInteger(input.Point),
        *FormatGroupedInteger(input.Instanced), *FormatGroupedInteger(input.Mesh)));
    lines.Add(FString::Printf(TEXT("awake %s  tier changes %s"), *FormatGroupedInteger(input.Awake),
        *FormatGroupedInteger(input.TierChanges)));
    lines.Add(input.bCombat
        ? FString::Printf(TEXT("combat %s  alive %s/%s  bolts %s (%s real)"), *CombatModelName(input.CombatModel),
            *FormatGroupedInteger(input.Alive), *FormatGroupedInteger(input.CombatShips),
            *FormatGroupedInteger(input.LiveBolts), *FormatGroupedInteger(input.RealBolts))
        : FString(TEXT("combat off")));
    lines.Add(FString::Printf(TEXT("memory %s MB"),
        *FormatGroupedInteger(FMath::RoundToInt64(StatsTextFinite(input.UsedMemoryMB)))));

    // Ramp line: phase, stage progress, and the last finished stage's result
    FString ramp;
    switch (input.RampPhase)
    {
    case SOLNpcScale::ERampPhase::Settling:
    case SOLNpcScale::ERampPhase::Measuring:
        ramp = FString::Printf(TEXT("ramp stage %d/%d  %s ships  %s %.0f/%.0f s"), input.RampStageIndex + 1,
            input.RampStageCount, *FormatGroupedInteger(input.RampStageNpcCount),
            input.RampPhase == SOLNpcScale::ERampPhase::Settling ? TEXT("settling") : TEXT("measuring"),
            StatsTextFinite(input.RampPhaseElapsedS), StatsTextFinite(input.RampPhaseLengthS));
        break;
    case SOLNpcScale::ERampPhase::Done:
        ramp = TEXT("ramp done");
        break;
    case SOLNpcScale::ERampPhase::Idle:
    default:
        ramp = TEXT("ramp idle");
        break;
    }
    if (!input.RampResult.IsEmpty())
    {
        ramp += TEXT("  last ");
        ramp += input.RampResult;
    }
    lines.Add(MoveTemp(ramp));
    return lines;
}
