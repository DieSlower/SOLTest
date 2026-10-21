/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcScaleMath.h"

namespace
{
    // Hash streams, one per hashed spawn field (any distinct values)
    enum ESOLNpcHashStream : uint32
    {
        STREAM_GROUP = 1,
        STREAM_FACTION,
        STREAM_LANE_START,
        STREAM_LANE_DIRECTION,
        STREAM_LANE_SPEED,
        STREAM_LANE_RADIUS,
        STREAM_LANE_ANGLE,
        STREAM_ORBIT_RADIUS,
        STREAM_ORBIT_INCLINATION,
        STREAM_ORBIT_NODE,
        STREAM_ORBIT_PHASE,
        STREAM_BALL_RADIUS,
        STREAM_BALL_Z,
        STREAM_BALL_AZIMUTH,
        STREAM_CENTER_DISTANCE,
        STREAM_CENTER_Z,
        STREAM_CENTER_AZIMUTH,
    };

    // Seed salt that separates battle-centre hashes from per-ship hashes
    constexpr uint32 BATTLE_CENTER_SALT = 0xB4771E5Eu;

    // Option descriptors, in EDemoOption order (SDD 11 section 3.8, metres and seconds)
    const SOLNpcScale::FOptionDesc OPTION_DESCRIPTORS[] = {
        // Id, Min, Max, Step, Default, bInteger, bBool
        { SOLNpcScale::EDemoOption::NpcCount, 0.0, 1000000.0, 0.0, 1000000.0, true, false },
        { SOLNpcScale::EDemoOption::LaneFraction, 0.0, 1.0, 0.05, 0.4, false, false },
        { SOLNpcScale::EDemoOption::BattleShipCount, 0.0, 20000.0, 500.0, 2000.0, true, false },
        { SOLNpcScale::EDemoOption::BattleCount, 1.0, 16.0, 1.0, 4.0, true, false },
        { SOLNpcScale::EDemoOption::BattleRadiusM, 500.0, 20000.0, 500.0, 3000.0, false, false },
        { SOLNpcScale::EDemoOption::BattleMinDistanceM, 1000.0, 200000.0, 1000.0, 5000.0, false, false },
        { SOLNpcScale::EDemoOption::BattleMaxDistanceM, 1000.0, 200000.0, 1000.0, 20000.0, false, false },
        { SOLNpcScale::EDemoOption::FullAiRadiusM, 5000.0, 500000.0, 5000.0, 50000.0, false, false },
        { SOLNpcScale::EDemoOption::MidAiRadiusM, 50000.0, 20000000.0, 50000.0, 2000000.0, false, false },
        { SOLNpcScale::EDemoOption::FullAiBudget, 0.0, 20000.0, 500.0, 4000.0, true, false },
        { SOLNpcScale::EDemoOption::MidAiBudget, 0.0, 500000.0, 10000.0, 100000.0, true, false },
        { SOLNpcScale::EDemoOption::AiHysteresis, 0.0, 0.5, 0.05, 0.1, false, false },
        { SOLNpcScale::EDemoOption::MeshRadiusM, 500.0, 20000.0, 500.0, 2000.0, false, false },
        { SOLNpcScale::EDemoOption::InstancedRadiusM, 1000.0, 2000000.0, 10000.0, 200000.0, false, false },
        { SOLNpcScale::EDemoOption::MeshBudget, 0.0, 256.0, 8.0, 32.0, true, false },
        { SOLNpcScale::EDemoOption::InstancedBudget, 0.0, 200000.0, 1000.0, 20000.0, true, false },
        { SOLNpcScale::EDemoOption::RenderHysteresis, 0.0, 0.5, 0.05, 0.1, false, false },
        { SOLNpcScale::EDemoOption::NpcCombat, 0.0, 1.0, 1.0, 1.0, false, true },
        { SOLNpcScale::EDemoOption::ShowStats, 0.0, 1.0, 1.0, 1.0, false, true },
        { SOLNpcScale::EDemoOption::RampSettleS, 1.0, 30.0, 1.0, 5.0, false, false },
        { SOLNpcScale::EDemoOption::RampMeasureS, 2.0, 60.0, 1.0, 10.0, false, false },
    };
    static_assert(UE_ARRAY_COUNT(OPTION_DESCRIPTORS) == static_cast<int32>(SOLNpcScale::EDemoOption::Count),
        "One descriptor per demo option");

    //////////////////////////////////////////////////////////////////////////
    // Returns the hysteresis fraction h' (h when finite and >= 0, else 0)
    double SanitizedHysteresis(double hysteresisFraction)
    {
        return (FMath::IsFinite(hysteresisFraction) && hysteresisFraction >= 0.0) ? hysteresisFraction : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the value that would sit at index nth if values were sorted ascending (quickselect, reorders values)
    double NpcSelectNth(TArray<double>& values, const int32 nth)
    {
        int32 lo = 0;
        int32 hi = values.Num() - 1;
        double* const data = values.GetData();
        while (lo < hi)
        {
            // Median-of-three pivot, then a Hoare partition of [lo, hi]
            const int32 mid = lo + (hi - lo) / 2;
            const double a = data[lo];
            const double b = data[mid];
            const double c = data[hi];
            const double pivot = FMath::Max(FMath::Min(a, b), FMath::Min(FMath::Max(a, b), c));
            int32 i = lo;
            int32 j = hi;
            while (i <= j)
            {
                while (data[i] < pivot)
                {
                    ++i;
                }
                while (data[j] > pivot)
                {
                    --j;
                }
                if (i <= j)
                {
                    Swap(data[i], data[j]);
                    ++i;
                    --j;
                }
            }

            // Keep only the side that holds nth ([lo, j] <= pivot <= [i, hi]; between them every value equals it)
            if (nth <= j)
            {
                hi = j;
            }
            else if (nth >= i)
            {
                lo = i;
            }
            else
            {
                return data[nth];
            }
        }
        return data[nth];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a well-mixed 32-bit hash of one value (lowbias32 finalizer)
    uint32 NpcMix32(uint32 x)
    {
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether an option id is a valid option
    bool IsValidOption(SOLNpcScale::EDemoOption id)
    {
        return static_cast<uint32>(id) < static_cast<uint32>(SOLNpcScale::EDemoOption::Count);
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes a value into an option's field (integers round to nearest, bools become value != 0)
    void WriteOption(SOLNpcScale::FDemoOptions& options, SOLNpcScale::EDemoOption id, double value)
    {
        using SOLNpcScale::EDemoOption;
        const int32 asInt = static_cast<int32>(FMath::FloorToDouble(value + 0.5));
        switch (id)
        {
        case EDemoOption::NpcCount: options.NpcCount = asInt; break;
        case EDemoOption::LaneFraction: options.LaneFraction = value; break;
        case EDemoOption::BattleShipCount: options.BattleShipCount = asInt; break;
        case EDemoOption::BattleCount: options.BattleCount = asInt; break;
        case EDemoOption::BattleRadiusM: options.BattleRadiusM = value; break;
        case EDemoOption::BattleMinDistanceM: options.BattleMinDistanceM = value; break;
        case EDemoOption::BattleMaxDistanceM: options.BattleMaxDistanceM = value; break;
        case EDemoOption::FullAiRadiusM: options.FullAiRadiusM = value; break;
        case EDemoOption::MidAiRadiusM: options.MidAiRadiusM = value; break;
        case EDemoOption::FullAiBudget: options.FullAiBudget = asInt; break;
        case EDemoOption::MidAiBudget: options.MidAiBudget = asInt; break;
        case EDemoOption::AiHysteresis: options.AiHysteresis = value; break;
        case EDemoOption::MeshRadiusM: options.MeshRadiusM = value; break;
        case EDemoOption::InstancedRadiusM: options.InstancedRadiusM = value; break;
        case EDemoOption::MeshBudget: options.MeshBudget = asInt; break;
        case EDemoOption::InstancedBudget: options.InstancedBudget = asInt; break;
        case EDemoOption::RenderHysteresis: options.RenderHysteresis = value; break;
        case EDemoOption::NpcCombat: options.bNpcCombat = value != 0.0; break;
        case EDemoOption::ShowStats: options.bShowStats = value != 0.0; break;
        case EDemoOption::RampSettleS: options.RampSettleS = value; break;
        case EDemoOption::RampMeasureS: options.RampMeasureS = value; break;
        default: break;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a uniform unit vector from two unit values (z uniform in [-1, 1], azimuth uniform)
    FVector3d UnitSphereDirection(double unitZ, double unitAzimuth)
    {
        const double z = 1.0 - 2.0 * unitZ;
        const double rho = FMath::Sqrt(FMath::Max(1.0 - z * z, 0.0));
        double sinA = 0.0;
        double cosA = 0.0;
        FMath::SinCos(&sinA, &cosA, 2.0 * UE_DOUBLE_PI * unitAzimuth);
        return FVector3d(rho * cosA, rho * sinA, z);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the first tier whose (keep or promote) radius contains the distance, or N beyond every radius
int32 SOLNpcScale::SelectTierIndex(int32 currentTier, double distanceM, TConstArrayView<double> promoteRadiiM,
    double hysteresisFraction)
{
    const int32 tierCount = promoteRadiiM.Num();
    if (!FMath::IsFinite(distanceM))
    {
        return tierCount;
    }
    const double distance = FMath::Max(distanceM, 0.0);
    const double keepScale = 1.0 + SanitizedHysteresis(hysteresisFraction);
    const bool bHasTier = currentTier >= 0 && currentTier <= tierCount;

    // First match wins; a ship already at tier k or nearer keeps tier k up to R_k * (1 + h')
    for (int32 tier = 0; tier < tierCount; ++tier)
    {
        const double radius = promoteRadiiM[tier];
        if (!(radius > 0.0))
        {
            continue;
        }
        const double keepRadius = (bHasTier && currentTier <= tier) ? radius * keepScale : radius;
        if (distance < keepRadius)
        {
            return tier;
        }
    }
    return tierCount;
}

//////////////////////////////////////////////////////////////////////////
// Selects Full / Mid / Far from the two AI radii
SOLNpcScale::EAiTier SOLNpcScale::SelectAiTier(EAiTier current, double distanceM, double fullRadiusM,
    double midRadiusM, double hysteresisFraction)
{
    const double radii[] = { fullRadiusM, midRadiusM };
    return static_cast<EAiTier>(SelectTierIndex(static_cast<int32>(current), distanceM, radii, hysteresisFraction));
}

//////////////////////////////////////////////////////////////////////////
// Selects Mesh / Instanced / Point from the two render radii
SOLNpcScale::ERenderTier SOLNpcScale::SelectRenderTier(ERenderTier current, double distanceM, double meshRadiusM,
    double instancedRadiusM, double hysteresisFraction)
{
    const double radii[] = { meshRadiusM, instancedRadiusM };
    return static_cast<ERenderTier>(SelectTierIndex(static_cast<int32>(current), distanceM, radii,
        hysteresisFraction));
}

//////////////////////////////////////////////////////////////////////////
// Shrinks a tier radius so that at most budget valid distances fall inside its keep radius
double SOLNpcScale::BudgetedRadiusM(TConstArrayView<double> distancesM, double nominalRadiusM,
    double hysteresisFraction, int32 budget, TArray<double>& scratch)
{
    const double keepScale = 1.0 + SanitizedHysteresis(hysteresisFraction);
    const double keepRadius = nominalRadiusM * keepScale;
    const int32 allowed = FMath::Max(budget, 0);

    // Count first: a budget that is not exceeded needs no copy at all
    int32 inside = 0;
    for (const double distanceM : distancesM)
    {
        inside += (FMath::IsFinite(distanceM) && FMath::Max(distanceM, 0.0) < keepRadius) ? 1 : 0;
    }
    if (inside <= allowed)
    {
        return nominalRadiusM;
    }

    // Collect only the distances inside the nominal keep radius: s[b] is one of them whenever the budget is exceeded
    scratch.Reset();
    for (const double distanceM : distancesM)
    {
        if (!FMath::IsFinite(distanceM))
        {
            continue;
        }
        const double distance = FMath::Max(distanceM, 0.0);
        if (distance < keepRadius)
        {
            scratch.Add(distance);
        }
    }
    if (scratch.Num() <= allowed)
    {
        return nominalRadiusM;
    }

    // s[b] by selection (linear on average) instead of a full sort (SDD 11 Amendment 6: the sort of ~60k distances
    // four times a frame cost ~3 ms)
    return FMath::Min(nominalRadiusM, NpcSelectNth(scratch, allowed) / keepScale);
}

//////////////////////////////////////////////////////////////////////////
// Returns the nth smallest value (nth clamped into range) by quickselect; 0 for an empty array
double SOLNpcScale::SelectNth(TArray<double>& values, const int32 nth)
{
    if (values.IsEmpty())
    {
        return 0.0;
    }
    return NpcSelectNth(values, FMath::Clamp(nth, 0, values.Num() - 1));
}

//////////////////////////////////////////////////////////////////////////
// A Far chunk runs when its group is a candidate and it may hold an awake ship (or its minimum is stale)
bool SOLNpcScale::IsFarChunkActive(const bool bGroupCandidate, const bool bGroupWakeDirty, const bool bEvaluatePass,
    const double chunkMinWakeKeyM, const double groupWakeClockM)
{
    if (!bGroupCandidate)
    {
        return false;
    }
    return (bEvaluatePass && bGroupWakeDirty) || chunkMinWakeKeyM <= groupWakeClockM;
}

//////////////////////////////////////////////////////////////////////////
// Clears the dirty flag of every group Evaluate visited (the candidates)
void SOLNpcScale::ClearVisitedWakeDirty(TArrayView<int32> groupWakeDirty, TConstArrayView<uint8> groupCandidate)
{
    const int32 count = FMath::Min(groupWakeDirty.Num(), groupCandidate.Num());
    for (int32 group = 0; group < count; ++group)
    {
        if (groupCandidate[group] != 0)
        {
            groupWakeDirty[group] = 0;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Lane speed scaled to the current length, plus the endpoints' relative motion (along the lane and turning the
// lateral offset about A)
double SOLNpcScale::LaneShipSpeedBoundMps(const double laneMaxSpeedMps, const double nominalLengthM,
    const double currentLengthM, const double laneRadiusM, const double endpointRelativeSpeedMps)
{
    const double lengthScale = nominalLengthM > 0.0 ? FMath::Max(currentLengthM, 0.0) / nominalLengthM : 1.0;
    const double turnScale = currentLengthM > 0.0 ? 1.0 + FMath::Max(laneRadiusM, 0.0) / currentLengthM : 1.0;
    return FMath::Abs(laneMaxSpeedMps) * lengthScale + FMath::Abs(endpointRelativeSpeedMps) * turnScale;
}

//////////////////////////////////////////////////////////////////////////
// Hashes (seed, index, stream) through chained 32-bit mixes into [0, 1)
double SOLNpcScale::HashUnit(uint32 seed, uint32 index, uint32 stream)
{
    const uint32 streamHash = NpcMix32(stream * 0x9E3779B9u + 0x632BE5ABu);
    const uint32 seedHash = NpcMix32(seed ^ streamHash);
    const uint32 hash = NpcMix32(index + seedHash);
    return static_cast<double>(hash) * (1.0 / 4294967296.0);
}

//////////////////////////////////////////////////////////////////////////
// Picks the smallest index whose cumulative weight exceeds unit * total
int32 SOLNpcScale::PickWeighted(double unit, TConstArrayView<double> weights)
{
    // Total of the usable (finite, positive) weights
    double total = 0.0;
    int32 lastPositive = INDEX_NONE;
    for (int32 index = 0; index < weights.Num(); ++index)
    {
        const double weight = weights[index];
        if (FMath::IsFinite(weight) && weight > 0.0)
        {
            total += weight;
            lastPositive = index;
        }
    }
    if (!(total > 0.0))
    {
        return INDEX_NONE;
    }

    const double u = FMath::IsNaN(unit) ? 0.0 : FMath::Clamp(unit, 0.0, 1.0);
    const double target = u * total;
    double cumulative = 0.0;
    for (int32 index = 0; index < weights.Num(); ++index)
    {
        const double weight = weights[index];
        if (!(FMath::IsFinite(weight) && weight > 0.0))
        {
            continue;
        }
        cumulative += weight;
        if (target < cumulative)
        {
            return index;
        }
    }
    return lastPositive;
}

//////////////////////////////////////////////////////////////////////////
// Battle ships first, then the rest split between lanes and clusters
SOLNpcScale::FCounts SOLNpcScale::SplitCounts(int32 total, int32 battleShips, double laneFraction)
{
    FCounts counts;
    const int32 totalShips = FMath::Max(total, 0);
    counts.Battle = FMath::Clamp(battleShips, 0, totalShips);
    const int32 rest = totalShips - counts.Battle;
    const double fraction = FMath::IsNaN(laneFraction) ? 0.0 : FMath::Clamp(laneFraction, 0.0, 1.0);
    counts.Lane = FMath::Clamp(static_cast<int32>(FMath::FloorToDouble(static_cast<double>(rest) * fraction + 0.5)),
        0, rest);
    counts.Cluster = rest - counts.Lane;
    return counts;
}

//////////////////////////////////////////////////////////////////////////
// Maps an NPC index to its placement kind and the index within that kind
SOLNpcScale::FPlacementSlot SOLNpcScale::ClassifyIndex(int32 npcIndex, const FCounts& counts)
{
    FPlacementSlot slot;
    if (npcIndex < 0 || npcIndex >= counts.Total())
    {
        return slot;
    }
    if (npcIndex < counts.Battle)
    {
        slot.Kind = EPlacementKind::Battle;
        slot.LocalIndex = npcIndex;
    }
    else if (npcIndex < counts.Battle + counts.Lane)
    {
        slot.Kind = EPlacementKind::Lane;
        slot.LocalIndex = npcIndex - counts.Battle;
    }
    else
    {
        slot.Kind = EPlacementKind::Cluster;
        slot.LocalIndex = npcIndex - counts.Battle - counts.Lane;
    }
    return slot;
}

//////////////////////////////////////////////////////////////////////////
// Builds one ship's spawn parameters from per-index hashes
SOLNpcScale::FSpawn SOLNpcScale::MakeSpawn(uint32 seed, int32 npcIndex, const FCounts& counts, const FLayout& layout,
    const FBattleParams& battle)
{
    FSpawn spawn;
    const FPlacementSlot slot = ClassifyIndex(npcIndex, counts);
    if (slot.LocalIndex == INDEX_NONE)
    {
        return spawn;
    }
    spawn.Kind = slot.Kind;
    const uint32 index = static_cast<uint32>(npcIndex);

    // Battle: round-robin over battles, factions alternate per round, slot uniform in the ball's volume
    if (slot.Kind == EPlacementKind::Battle)
    {
        const int32 battles = FMath::Max(battle.BattleCount, 1);
        spawn.GroupIndex = slot.LocalIndex % battles;
        spawn.Faction = (slot.LocalIndex / battles) % 2;
        if (FMath::IsFinite(battle.RadiusM) && battle.RadiusM > 0.0)
        {
            const double radius = battle.RadiusM * FMath::Min(FMath::Pow(HashUnit(seed, index, STREAM_BALL_RADIUS),
                1.0 / 3.0), 1.0);
            spawn.BattleSlotOffsetM = UnitSphereDirection(HashUnit(seed, index, STREAM_BALL_Z),
                HashUnit(seed, index, STREAM_BALL_AZIMUTH)) * radius;
        }
        return spawn;
    }

    // Lane: weighted lane, faction, start parameter, direction, speed, lateral offset uniform over the disc
    if (slot.Kind == EPlacementKind::Lane)
    {
        TArray<double, TInlineAllocator<32>> weights;
        for (const FLaneDef& lane : layout.Lanes)
        {
            weights.Add(lane.Weight);
        }
        const int32 laneIndex = PickWeighted(HashUnit(seed, index, STREAM_GROUP), weights);
        if (laneIndex == INDEX_NONE)
        {
            return spawn;
        }
        spawn.GroupIndex = laneIndex;
        spawn.Faction = HashUnit(seed, index, STREAM_FACTION) < 0.5 ? 0 : 1;
        spawn.LaneStartParam = HashUnit(seed, index, STREAM_LANE_START);
        spawn.LaneDirection = HashUnit(seed, index, STREAM_LANE_DIRECTION) < 0.5 ? 1.0 : -1.0;
        const double speedLo = FMath::Min(layout.LaneMinSpeedMps, layout.LaneMaxSpeedMps);
        const double speedHi = FMath::Max(layout.LaneMinSpeedMps, layout.LaneMaxSpeedMps);
        spawn.LaneSpeedMps = speedLo + (speedHi - speedLo) * HashUnit(seed, index, STREAM_LANE_SPEED);
        const double laneRadius = layout.Lanes[laneIndex].RadiusM;
        if (FMath::IsFinite(laneRadius) && laneRadius > 0.0)
        {
            const double radius = laneRadius * FMath::Sqrt(HashUnit(seed, index, STREAM_LANE_RADIUS));
            double sinA = 0.0;
            double cosA = 0.0;
            FMath::SinCos(&sinA, &cosA, 2.0 * UE_DOUBLE_PI * HashUnit(seed, index, STREAM_LANE_ANGLE));
            spawn.LaneLateralOffsetM = FVector2d(radius * cosA, radius * sinA);
        }
        return spawn;
    }

    // Cluster: weighted cluster, faction, orbit radius, inclination, node and phase
    TArray<double, TInlineAllocator<32>> weights;
    for (const FClusterDef& cluster : layout.Clusters)
    {
        weights.Add(cluster.Weight);
    }
    const int32 clusterIndex = PickWeighted(HashUnit(seed, index, STREAM_GROUP), weights);
    if (clusterIndex == INDEX_NONE)
    {
        return spawn;
    }
    const FClusterDef& cluster = layout.Clusters[clusterIndex];
    spawn.GroupIndex = clusterIndex;
    spawn.Faction = HashUnit(seed, index, STREAM_FACTION) < 0.5 ? 0 : 1;
    const double radiusLo = FMath::Min(cluster.InnerRadiusM, cluster.OuterRadiusM);
    const double radiusHi = FMath::Max(cluster.InnerRadiusM, cluster.OuterRadiusM);
    spawn.OrbitRadiusM = radiusLo + (radiusHi - radiusLo) * HashUnit(seed, index, STREAM_ORBIT_RADIUS);
    const double maxInclination = (cluster.MaxInclinationRad > 0.0) ? cluster.MaxInclinationRad : 0.0;
    spawn.OrbitInclinationRad = maxInclination > 0.0
        ? FMath::Min(maxInclination * HashUnit(seed, index, STREAM_ORBIT_INCLINATION), maxInclination) : 0.0;
    spawn.OrbitNodeRad = 2.0 * UE_DOUBLE_PI * HashUnit(seed, index, STREAM_ORBIT_NODE);
    spawn.OrbitPhaseRad = 2.0 * UE_DOUBLE_PI * HashUnit(seed, index, STREAM_ORBIT_PHASE);
    return spawn;
}

//////////////////////////////////////////////////////////////////////////
// Places a battle centre at a hashed distance and uniform direction from the player's frame
FVector3d SOLNpcScale::BattleCenterOffsetM(uint32 seed, int32 battleIndex, const FBattleParams& battle)
{
    const double minD = (FMath::IsFinite(battle.MinDistanceM) && battle.MinDistanceM > 0.0) ? battle.MinDistanceM : 0.0;
    const double maxD = (FMath::IsFinite(battle.MaxDistanceM) && battle.MaxDistanceM > 0.0) ? battle.MaxDistanceM : 0.0;
    const double lo = FMath::Min(minD, maxD);
    const double hi = FMath::Max(minD, maxD);
    const uint32 centerSeed = seed ^ BATTLE_CENTER_SALT;
    const uint32 index = static_cast<uint32>(battleIndex);
    const double distance = FMath::Min(lo + (hi - lo) * HashUnit(centerSeed, index, STREAM_CENTER_DISTANCE), hi);
    return UnitSphereDirection(HashUnit(centerSeed, index, STREAM_CENTER_Z),
        HashUnit(centerSeed, index, STREAM_CENTER_AZIMUTH)) * distance;
}

//////////////////////////////////////////////////////////////////////////
// Folds x into a period-2 triangle wave in [0, 1]
double SOLNpcScale::TriangleWave01(double x)
{
    if (!FMath::IsFinite(x))
    {
        return 0.0;
    }
    const double m = x - 2.0 * FMath::FloorToDouble(x / 2.0);
    return m <= 1.0 ? m : 2.0 - m;
}

//////////////////////////////////////////////////////////////////////////
// Moves a lane ship back and forth along its lane at its fixed speed
double SOLNpcScale::LaneParamAt(const FSpawn& spawn, double nominalLaneLengthM, double timeS)
{
    if (!FMath::IsFinite(nominalLaneLengthM) || !(nominalLaneLengthM > 0.0) || !FMath::IsFinite(timeS))
    {
        return TriangleWave01(spawn.LaneStartParam);
    }
    return TriangleWave01(spawn.LaneStartParam + spawn.LaneDirection * spawn.LaneSpeedMps * timeS
        / nominalLaneLengthM);
}

//////////////////////////////////////////////////////////////////////////
// Builds a right-handed (U, V, axis) basis from a reference axis that is not near-parallel to the lane
void SOLNpcScale::LaneBasis(const FVector3d& axis, FVector3d& outU, FVector3d& outV)
{
    const double length = axis.Size();
    if (!FMath::IsFinite(length) || !(length > 0.0))
    {
        outU = FVector3d(1.0, 0.0, 0.0);
        outV = FVector3d(0.0, 1.0, 0.0);
        return;
    }
    const FVector3d a = axis / length;
    const FVector3d reference = FMath::Abs(a.Z) < 0.9 ? FVector3d(0.0, 0.0, 1.0) : FVector3d(1.0, 0.0, 0.0);
    outU = FVector3d::CrossProduct(reference, a).GetSafeNormal();
    outV = FVector3d::CrossProduct(a, outU);
}

//////////////////////////////////////////////////////////////////////////
// Interpolates along the lane and adds the lateral offset in the lane's basis
FVector3d SOLNpcScale::LanePositionM(const FVector3d& endpointAM, const FVector3d& endpointBM, double laneParam,
    const FVector2d& lateralOffsetM)
{
    const FVector3d axis = endpointBM - endpointAM;
    FVector3d u;
    FVector3d v;
    LaneBasis(axis, u, v);
    return endpointAM + axis * laneParam + u * lateralOffsetM.X + v * lateralOffsetM.Y;
}

//////////////////////////////////////////////////////////////////////////
// Evaluates the ship's circular Keplerian orbit around its body at a time
FVector3d SOLNpcScale::ClusterOffsetM(const FSpawn& spawn, double bodyGM, double timeS)
{
    const double r = spawn.OrbitRadiusM;
    if (!FMath::IsFinite(r) || !(r > 0.0))
    {
        return FVector3d::ZeroVector;
    }
    const double meanMotion = (FMath::IsFinite(bodyGM) && bodyGM > 0.0) ? FMath::Sqrt(bodyGM / (r * r * r)) : 0.0;
    const double t = FMath::IsFinite(timeS) ? timeS : 0.0;
    const double theta = spawn.OrbitPhaseRad + meanMotion * t;

    double sinI = 0.0;
    double cosI = 0.0;
    double sinNode = 0.0;
    double cosNode = 0.0;
    double sinTheta = 0.0;
    double cosTheta = 0.0;
    FMath::SinCos(&sinI, &cosI, spawn.OrbitInclinationRad);
    FMath::SinCos(&sinNode, &cosNode, spawn.OrbitNodeRad);
    FMath::SinCos(&sinTheta, &cosTheta, theta);
    const FVector3d p(cosNode, sinNode, 0.0);
    const FVector3d q(-cosI * sinNode, cosI * cosNode, sinI);
    return (p * cosTheta + q * sinTheta) * r;
}

//////////////////////////////////////////////////////////////////////////
// Projects the point onto the segment, clamped to its ends
double SOLNpcScale::DistancePointToSegmentM(const FVector3d& p, const FVector3d& a, const FVector3d& b)
{
    const FVector3d ab = b - a;
    const double lengthSquared = ab.SizeSquared();
    if (!(lengthSquared > 0.0))
    {
        return (p - a).Size();
    }
    const double t = FMath::Clamp(FVector3d::DotProduct(p - a, ab) / lengthSquared, 0.0, 1.0);
    return (p - (a + ab * t)).Size();
}

//////////////////////////////////////////////////////////////////////////
// Distance to the segment minus the lane radius, never negative
double SOLNpcScale::DistanceToLaneM(const FVector3d& p, const FVector3d& a, const FVector3d& b, double laneRadiusM)
{
    const double radius = FMath::IsFinite(laneRadiusM) ? FMath::Max(laneRadiusM, 0.0) : 0.0;
    return FMath::Max(0.0, DistancePointToSegmentM(p, a, b) - radius);
}

//////////////////////////////////////////////////////////////////////////
// Distance to the shell between the inner and outer radii, 0 inside it
double SOLNpcScale::DistanceToShellM(const FVector3d& p, const FVector3d& center, double innerRadiusM,
    double outerRadiusM)
{
    const double d = (p - center).Size();
    const double lo = FMath::Min(innerRadiusM, outerRadiusM);
    const double hi = FMath::Max(innerRadiusM, outerRadiusM);
    if (d < lo)
    {
        return lo - d;
    }
    if (d > hi)
    {
        return d - hi;
    }
    return 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Lanes between neighbouring planets plus Earth-Moon and Earth-Mars, clusters around planets, the Moon and the belt
SOLNpcScale::FLayout SOLNpcScale::DefaultLayout()
{
    FLayout layout;

    // Lanes: body A, body B, capsule radius, weight (busier inner-system lanes weigh more)
    struct FLaneRow
    {
        const TCHAR* BodyA;
        const TCHAR* BodyB;
        double RadiusM;
        double Weight;
    };
    const FLaneRow lanes[] = {
        { TEXT("Mercury"), TEXT("Venus"), 2.0e6, 1.0 },
        { TEXT("Venus"), TEXT("Earth"), 2.0e6, 1.5 },
        { TEXT("Earth"), TEXT("Moon"), 2.0e5, 1.0 },
        { TEXT("Earth"), TEXT("Mars"), 3.0e6, 2.0 },
        { TEXT("Mars"), TEXT("Jupiter"), 5.0e6, 1.5 },
        { TEXT("Jupiter"), TEXT("Saturn"), 8.0e6, 1.0 },
        { TEXT("Saturn"), TEXT("Uranus"), 1.0e7, 0.5 },
        { TEXT("Uranus"), TEXT("Neptune"), 1.0e7, 0.5 },
    };
    for (const FLaneRow& row : lanes)
    {
        FLaneDef lane;
        lane.BodyA = FName(row.BodyA);
        lane.BodyB = FName(row.BodyB);
        lane.RadiusM = row.RadiusM;
        lane.Weight = row.Weight;
        layout.Lanes.Add(lane);
    }

    // Clusters: body, inner and outer orbit radius, inclination cap, weight
    struct FClusterRow
    {
        const TCHAR* Body;
        double InnerRadiusM;
        double OuterRadiusM;
        double MaxInclinationRad;
        double Weight;
    };
    const FClusterRow clusters[] = {
        { TEXT("Mercury"), 3.0e6, 2.0e7, 0.6, 0.5 },
        { TEXT("Venus"), 7.0e6, 4.0e7, 0.6, 0.8 },
        { TEXT("Earth"), 7.0e6, 6.0e7, 0.8, 2.0 },
        { TEXT("Moon"), 2.0e6, 1.5e7, 0.8, 0.8 },
        { TEXT("Mars"), 4.0e6, 3.0e7, 0.6, 1.2 },
        { TEXT("Jupiter"), 8.0e7, 2.0e9, 0.4, 1.5 },
        { TEXT("Saturn"), 7.0e7, 1.5e9, 0.4, 1.2 },
        { TEXT("Uranus"), 3.0e7, 6.0e8, 0.4, 0.5 },
        { TEXT("Neptune"), 3.0e7, 6.0e8, 0.4, 0.5 },
        { TEXT("Sun"), 2.1 * SOL::AU_M, 3.3 * SOL::AU_M, 0.15, 3.0 },   // The asteroid belt
    };
    for (const FClusterRow& row : clusters)
    {
        FClusterDef cluster;
        cluster.Body = FName(row.Body);
        cluster.InnerRadiusM = row.InnerRadiusM;
        cluster.OuterRadiusM = row.OuterRadiusM;
        cluster.MaxInclinationRad = row.MaxInclinationRad;
        cluster.Weight = row.Weight;
        layout.Clusters.Add(cluster);
    }

    layout.LaneMinSpeedMps = 2.0e4;
    layout.LaneMaxSpeedMps = 1.5e5;
    return layout;
}

//////////////////////////////////////////////////////////////////////////
// Every standard stage below the maximum, then the maximum itself
TArray<int32> SOLNpcScale::BuildRampStages(int32 maxCount)
{
    TArray<int32> stages;
    const int32 maximum = FMath::Min(maxCount, SOL::NPC_MAX_COUNT);
    if (maximum <= 0)
    {
        return stages;
    }
    for (const int32 stage : SOL::NPC_RAMP_STAGE_COUNTS)
    {
        if (stage < maximum)
        {
            stages.Add(stage);
        }
    }
    stages.Add(maximum);
    return stages;
}

//////////////////////////////////////////////////////////////////////////
// Starts settling the first stage, or is done at once with no stages
SOLNpcScale::FRampState SOLNpcScale::StartRamp(int32 stageCount)
{
    FRampState state;
    if (stageCount <= 0)
    {
        state.Phase = ERampPhase::Done;
        return state;
    }
    state.Phase = ERampPhase::Settling;
    state.StageIndex = 0;
    return state;
}

//////////////////////////////////////////////////////////////////////////
// Settles each stage, then measures it, then moves on; at most one transition per step
SOLNpcScale::FRampStep SOLNpcScale::StepRamp(const FRampState& state, int32 stageCount, double settleS,
    double measureS, double realDtS)
{
    FRampStep step;
    step.State = state;
    const bool bValidDt = FMath::IsFinite(realDtS) && realDtS > 0.0;
    const bool bActive = state.Phase == ERampPhase::Settling || state.Phase == ERampPhase::Measuring;
    if (!bValidDt || !bActive || state.StageIndex < 0 || state.StageIndex >= stageCount)
    {
        return step;
    }
    const double settle = (FMath::IsFinite(settleS) && settleS > 0.0) ? settleS : 0.0;
    const double measure = (FMath::IsFinite(measureS) && measureS > 0.0) ? measureS : 0.0;

    // Settling: wait, then start measuring from zero
    if (state.Phase == ERampPhase::Settling)
    {
        step.State.PhaseElapsedS = state.PhaseElapsedS + realDtS;
        if (step.State.PhaseElapsedS >= settle)
        {
            step.State.Phase = ERampPhase::Measuring;
            step.State.PhaseElapsedS = 0.0;
        }
        return step;
    }

    // Measuring: record this frame, finish the stage once the measure time has passed
    step.bRecordFrame = true;
    step.State.PhaseElapsedS = state.PhaseElapsedS + realDtS;
    if (step.State.PhaseElapsedS >= measure)
    {
        step.bStageFinished = true;
        step.FinishedStageIndex = state.StageIndex;
        step.State.PhaseElapsedS = 0.0;
        if (state.StageIndex + 1 < stageCount)
        {
            step.State.Phase = ERampPhase::Settling;
            step.State.StageIndex = state.StageIndex + 1;
        }
        else
        {
            step.State.Phase = ERampPhase::Done;
            step.State.StageIndex = INDEX_NONE;
        }
    }
    return step;
}

//////////////////////////////////////////////////////////////////////////
// Returns the active stage's count while the ramp runs
int32 SOLNpcScale::StageNpcCount(const FRampState& state, TConstArrayView<int32> stages)
{
    const bool bActive = state.Phase == ERampPhase::Settling || state.Phase == ERampPhase::Measuring;
    if (!bActive || !stages.IsValidIndex(state.StageIndex))
    {
        return INDEX_NONE;
    }
    return stages[state.StageIndex];
}

//////////////////////////////////////////////////////////////////////////
// Nearest-rank percentile with a small epsilon so exact ranks are not pushed up by rounding
double SOLNpcScale::PercentileNearestRank(TConstArrayView<double> sortedAscending, double fraction)
{
    const int32 count = sortedAscending.Num();
    if (count == 0)
    {
        return 0.0;
    }
    if (FMath::IsNaN(fraction) || fraction <= 0.0)
    {
        return sortedAscending[0];
    }
    if (fraction >= 1.0)
    {
        return sortedAscending[count - 1];
    }
    const double rank = FMath::CeilToDouble(fraction * static_cast<double>(count) - 1.0e-9);
    const int32 index = FMath::Clamp(static_cast<int32>(rank), 1, count) - 1;
    return sortedAscending[index];
}

//////////////////////////////////////////////////////////////////////////
// Filters, sorts and summarizes a stage's frame times
SOLNpcScale::FFrameStats SOLNpcScale::SummarizeFrameTimes(TArray<double>& inOutFrameTimesMs)
{
    FFrameStats stats;
    inOutFrameTimesMs.RemoveAll([](double value)
    {
        return !FMath::IsFinite(value) || value < 0.0;
    });
    inOutFrameTimesMs.Sort();
    stats.Count = inOutFrameTimesMs.Num();
    if (stats.Count == 0)
    {
        return stats;
    }
    double sum = 0.0;
    for (const double value : inOutFrameTimesMs)
    {
        sum += value;
    }
    stats.MeanMs = sum / static_cast<double>(stats.Count);
    stats.MinMs = inOutFrameTimesMs[0];
    stats.MaxMs = inOutFrameTimesMs.Last();
    stats.P50Ms = PercentileNearestRank(inOutFrameTimesMs, 0.50);
    stats.P95Ms = PercentileNearestRank(inOutFrameTimesMs, 0.95);
    stats.P99Ms = PercentileNearestRank(inOutFrameTimesMs, 0.99);
    return stats;
}

//////////////////////////////////////////////////////////////////////////
// A stage passes when its 95th-percentile frame time fits the target frame time
bool SOLNpcScale::MeetsFrameTarget(const FFrameStats& stats, double targetFps)
{
    if (stats.Count <= 0 || !FMath::IsFinite(targetFps) || !(targetFps > 0.0))
    {
        return false;
    }
    return stats.P95Ms <= SOL::MILLISECONDS_PER_SECOND / targetFps;
}

//////////////////////////////////////////////////////////////////////////
// Formats the one-line ramp stage summary the benchmark greps for
FString SOLNpcScale::FormatRampStageLine(int32 npcCount, const FFrameStats& stats, double usedPhysicalMB, bool bPass)
{
    const double fps = stats.MeanMs > 0.0 ? SOL::MILLISECONDS_PER_SECOND / stats.MeanMs : 0.0;
    return FString::Printf(TEXT("NpcRamp stage=%d frames=%d meanMs=%.2f p50Ms=%.2f p95Ms=%.2f p99Ms=%.2f maxMs=%.2f ")
        TEXT("fps=%.1f memMB=%.0f result=%s"), npcCount, stats.Count, stats.MeanMs, stats.P50Ms, stats.P95Ms,
        stats.P99Ms, stats.MaxMs, fps, usedPhysicalMB, bPass ? TEXT("PASS") : TEXT("FAIL"));
}

//////////////////////////////////////////////////////////////////////////
// Returns the static descriptor table
TConstArrayView<SOLNpcScale::FOptionDesc> SOLNpcScale::OptionDescriptors()
{
    return TConstArrayView<FOptionDesc>(OPTION_DESCRIPTORS, UE_ARRAY_COUNT(OPTION_DESCRIPTORS));
}

//////////////////////////////////////////////////////////////////////////
// The member initializers already hold every descriptor default
SOLNpcScale::FDemoOptions SOLNpcScale::DefaultOptions()
{
    return FDemoOptions();
}

//////////////////////////////////////////////////////////////////////////
// Reads one option as a double
double SOLNpcScale::GetOption(const FDemoOptions& options, EDemoOption id)
{
    switch (id)
    {
    case EDemoOption::NpcCount: return options.NpcCount;
    case EDemoOption::LaneFraction: return options.LaneFraction;
    case EDemoOption::BattleShipCount: return options.BattleShipCount;
    case EDemoOption::BattleCount: return options.BattleCount;
    case EDemoOption::BattleRadiusM: return options.BattleRadiusM;
    case EDemoOption::BattleMinDistanceM: return options.BattleMinDistanceM;
    case EDemoOption::BattleMaxDistanceM: return options.BattleMaxDistanceM;
    case EDemoOption::FullAiRadiusM: return options.FullAiRadiusM;
    case EDemoOption::MidAiRadiusM: return options.MidAiRadiusM;
    case EDemoOption::FullAiBudget: return options.FullAiBudget;
    case EDemoOption::MidAiBudget: return options.MidAiBudget;
    case EDemoOption::AiHysteresis: return options.AiHysteresis;
    case EDemoOption::MeshRadiusM: return options.MeshRadiusM;
    case EDemoOption::InstancedRadiusM: return options.InstancedRadiusM;
    case EDemoOption::MeshBudget: return options.MeshBudget;
    case EDemoOption::InstancedBudget: return options.InstancedBudget;
    case EDemoOption::RenderHysteresis: return options.RenderHysteresis;
    case EDemoOption::NpcCombat: return options.bNpcCombat ? 1.0 : 0.0;
    case EDemoOption::ShowStats: return options.bShowStats ? 1.0 : 0.0;
    case EDemoOption::RampSettleS: return options.RampSettleS;
    case EDemoOption::RampMeasureS: return options.RampMeasureS;
    default: return 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Range clamp first, then the cross-option rules in order, then the warnings
SOLNpcScale::FSanitizeResult SOLNpcScale::SanitizeOptions(const FDemoOptions& options)
{
    FSanitizeResult result;
    result.Options = options;
    FDemoOptions& out = result.Options;

    // (1) Reset non-finite fields to their default and clamp the rest into range
    for (const FOptionDesc& desc : OptionDescriptors())
    {
        const double value = GetOption(out, desc.Id);
        const double fixedValue = FMath::IsFinite(value) ? FMath::Clamp(value, desc.Min, desc.Max) : desc.Default;
        if (fixedValue != value)
        {
            WriteOption(out, desc.Id, fixedValue);
            result.Issues |= EOptionIssue::ValueClamped;
        }
    }

    // (2) - (7) Cross-option rules
    if (out.MidAiRadiusM < out.FullAiRadiusM)
    {
        out.MidAiRadiusM = out.FullAiRadiusM;
        result.Issues |= EOptionIssue::MidRadiusRaised;
    }
    if (out.InstancedRadiusM > out.MidAiRadiusM)
    {
        out.InstancedRadiusM = out.MidAiRadiusM;
        result.Issues |= EOptionIssue::InstancedRadiusLowered;
    }
    if (out.MeshRadiusM > out.InstancedRadiusM)
    {
        out.MeshRadiusM = out.InstancedRadiusM;
        result.Issues |= EOptionIssue::MeshRadiusLowered;
    }
    if (out.BattleShipCount > out.NpcCount)
    {
        out.BattleShipCount = out.NpcCount;
        result.Issues |= EOptionIssue::BattleShipsLimited;
    }
    if (out.BattleMaxDistanceM < out.BattleMinDistanceM)
    {
        out.BattleMaxDistanceM = out.BattleMinDistanceM;
        result.Issues |= EOptionIssue::BattleDistanceRaised;
    }
    if (out.MidAiBudget < out.FullAiBudget)
    {
        out.MidAiBudget = out.FullAiBudget;
        result.Issues |= EOptionIssue::MidBudgetRaised;
    }

    // Warnings only
    if (out.BattleShipCount > out.FullAiBudget)
    {
        result.Issues |= EOptionIssue::BattleShipsOverFullBudget;
    }
    if (out.BattleMaxDistanceM + out.BattleRadiusM > out.FullAiRadiusM)
    {
        result.Issues |= EOptionIssue::BattlesBeyondFullAi;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Clamps, rounds and writes one option, then sanitizes the whole set
SOLNpcScale::FDemoOptions SOLNpcScale::SetOption(const FDemoOptions& options, EDemoOption id, double value)
{
    if (!IsValidOption(id) || FMath::IsNaN(value))
    {
        return options;
    }
    const FOptionDesc& desc = OptionDescriptors()[static_cast<int32>(id)];
    double clamped = FMath::Clamp(value, desc.Min, desc.Max);
    if (desc.bInteger)
    {
        clamped = FMath::FloorToDouble(clamped + 0.5);
    }
    // A bool takes the raw value's truth (so -3 is on), not the clamped one
    FDemoOptions changed = options;
    WriteOption(changed, id, desc.bBool ? (value != 0.0 ? 1.0 : 0.0) : clamped);
    return SanitizeOptions(changed).Options;
}

//////////////////////////////////////////////////////////////////////////
// Toggles bools, moves the count between presets and steps every other option
SOLNpcScale::FDemoOptions SOLNpcScale::StepOption(const FDemoOptions& options, EDemoOption id, int32 direction)
{
    if (direction == 0 || !IsValidOption(id))
    {
        return options;
    }
    const FOptionDesc& desc = OptionDescriptors()[static_cast<int32>(id)];
    const double current = GetOption(options, id);
    if (desc.bBool)
    {
        return SetOption(options, id, current != 0.0 ? 0.0 : 1.0);
    }

    // NpcCount: the nearest preset strictly above or below
    if (id == EDemoOption::NpcCount)
    {
        int32 target = options.NpcCount;
        if (direction > 0)
        {
            for (const int32 preset : SOL::NPC_COUNT_PRESETS)
            {
                if (preset > options.NpcCount)
                {
                    target = preset;
                    break;
                }
            }
        }
        else
        {
            for (int32 index = UE_ARRAY_COUNT(SOL::NPC_COUNT_PRESETS) - 1; index >= 0; --index)
            {
                if (SOL::NPC_COUNT_PRESETS[index] < options.NpcCount)
                {
                    target = SOL::NPC_COUNT_PRESETS[index];
                    break;
                }
            }
        }
        return SetOption(options, id, static_cast<double>(target));
    }
    return SetOption(options, id, current + (direction > 0 ? desc.Step : -desc.Step));
}

//////////////////////////////////////////////////////////////////////////
// Parses digits with an optional decimal point and k / M suffix into a whole count in range
int32 SOLNpcScale::ParseNpcCount(const FString& text)
{
    const FString trimmed = text.TrimStartAndEnd();
    FString integerDigits;
    FString fractionDigits;
    bool bSeenPoint = false;
    int32 exponent = 0;
    int32 index = 0;

    // Digits with at most one decimal point
    for (; index < trimmed.Len(); ++index)
    {
        const TCHAR c = trimmed[index];
        if (FChar::IsDigit(c))
        {
            (bSeenPoint ? fractionDigits : integerDigits).AppendChar(c);
        }
        else if (c == TEXT('.') && !bSeenPoint)
        {
            bSeenPoint = true;
        }
        else
        {
            break;
        }
    }
    if (integerDigits.IsEmpty() && fractionDigits.IsEmpty())
    {
        return INDEX_NONE;
    }

    // Optional single suffix, then nothing else
    if (index < trimmed.Len())
    {
        const TCHAR suffix = trimmed[index];
        if (suffix == TEXT('k') || suffix == TEXT('K'))
        {
            exponent = 3;
        }
        else if (suffix == TEXT('m') || suffix == TEXT('M'))
        {
            exponent = 6;
        }
        else
        {
            return INDEX_NONE;
        }
        ++index;
    }
    if (index != trimmed.Len())
    {
        return INDEX_NONE;
    }

    // Whole only if the significant fraction digits fit within the suffix's power of ten
    while (!fractionDigits.IsEmpty() && fractionDigits[fractionDigits.Len() - 1] == TEXT('0'))
    {
        fractionDigits.LeftChopInline(1);
    }
    if (fractionDigits.Len() > exponent)
    {
        return INDEX_NONE;
    }
    int32 firstNonZero = 0;
    while (firstNonZero < integerDigits.Len() && integerDigits[firstNonZero] == TEXT('0'))
    {
        ++firstNonZero;
    }
    if (integerDigits.Len() - firstNonZero > 7)
    {
        return INDEX_NONE;
    }

    // Assemble the value in 64-bit integers
    int64 value = 0;
    for (int32 digit = firstNonZero; digit < integerDigits.Len(); ++digit)
    {
        value = value * 10 + (integerDigits[digit] - TEXT('0'));
    }
    for (int32 power = 0; power < exponent; ++power)
    {
        value *= 10;
    }
    int64 fraction = 0;
    for (const TCHAR c : fractionDigits)
    {
        fraction = fraction * 10 + (c - TEXT('0'));
    }
    for (int32 power = fractionDigits.Len(); power < exponent; ++power)
    {
        fraction *= 10;
    }
    value += fraction;
    if (value > SOL::NPC_MAX_COUNT)
    {
        return INDEX_NONE;
    }
    return static_cast<int32>(value);
}
