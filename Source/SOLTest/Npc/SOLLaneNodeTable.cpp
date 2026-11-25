/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLLaneNodeTable.h"

#include "MinorBodies/SOLAsteroidBelt.h"
#include "SOLConstants.h"
#include "Universe/SOLBodyRegistry.h"

#include <limits>

namespace
{
    // One registry node's static row (SDD 53 4.2): name, kind, population tier (O6), region radius (O5)
    struct FLaneNodeRow
    {
        const TCHAR* Name;
        ESOLLaneNodeKind Kind;
        double Tier;
        double RegionRadiusM;
    };

    // Nodes 0-13 in node order; the Moon's region radius is the Earth-Moon lane row (it has no partner lanes)
    const FLaneNodeRow LANE_NODE_ROWS[] = {
        { TEXT("Mercury"), ESOLLaneNodeKind::Planet, 2.0, 2.0e6 },
        { TEXT("Venus"), ESOLLaneNodeKind::Planet, 2.0, 2.0e6 },
        { TEXT("Earth"), ESOLLaneNodeKind::Planet, 10.0, 2.0e6 },
        { TEXT("Mars"), ESOLLaneNodeKind::Planet, 6.0, 3.0e6 },
        { TEXT("Jupiter"), ESOLLaneNodeKind::Planet, 2.0, 5.0e6 },
        { TEXT("Saturn"), ESOLLaneNodeKind::Planet, 2.0, 8.0e6 },
        { TEXT("Uranus"), ESOLLaneNodeKind::Planet, 1.0, 1.0e7 },
        { TEXT("Neptune"), ESOLLaneNodeKind::Planet, 1.0, 1.0e7 },
        { TEXT("Moon"), ESOLLaneNodeKind::Moon, 10.0, 2.0e5 },
        { TEXT("Ceres"), ESOLLaneNodeKind::DwarfPlanet, 4.0, 4.0e6 },
        { TEXT("Pluto"), ESOLLaneNodeKind::DwarfPlanet, 1.0, 1.0e7 },
        { TEXT("Haumea"), ESOLLaneNodeKind::DwarfPlanet, 1.0, 1.0e7 },
        { TEXT("Makemake"), ESOLLaneNodeKind::DwarfPlanet, 1.0, 1.0e7 },
        { TEXT("Eris"), ESOLLaneNodeKind::DwarfPlanet, 1.0, 1.0e7 },
    };
    static_assert(UE_ARRAY_COUNT(LANE_NODE_ROWS) == SOLLaneNodeTable::REGISTRY_NODE_COUNT,
        "one row per registry node");

    //////////////////////////////////////////////////////////////////////////
    // Returns the perihelion speed of an orbit, sqrt(GM (1 + e) / (a (1 - e))); 0 for a degenerate orbit
    double LaneNodePerihelionSpeedMps(const FSOLKeplerElements& elements, const double gm)
    {
        const double periapsisM = elements.SemiMajorAxisM * (1.0 - elements.Eccentricity);
        if (!(gm > 0.0) || !(periapsisM > 0.0))
        {
            return 0.0;
        }
        return FMath::Sqrt(gm * (1.0 + elements.Eccentricity) / periapsisM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an orbit's apoapsis distance a (1 + e)
    double LaneNodeApoapsisM(const FSOLKeplerElements& elements)
    {
        return elements.SemiMajorAxisM * (1.0 + elements.Eccentricity);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an asteroid node's Sun-relative Kepler position (SOLMinorBodyOrbit's sum without the velocity terms),
    // NaN when its elements are missing
    FVector3d LaneNodeAsteroidPositionM(const SOLLaneNodeTable::FTable& table, const int32 node,
        const double centuries)
    {
        const int32 row = node - SOLLaneNodeTable::FIRST_ASTEROID_NODE;
        return table.AsteroidElements.IsValidIndex(row)
            ? SOLKepler::ElementsToPositionM(table.AsteroidElements[row].AtCenturies(centuries), table.SunGM, 0.0)
            : FVector3d(std::numeric_limits<double>::quiet_NaN());
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a registry node's population tier by body name, 0 when the body is not a lane node
double SOLLaneNodeTable::TierOf(const FName bodyName)
{
    for (const FLaneNodeRow& row : LANE_NODE_ROWS)
    {
        if (bodyName == FName(row.Name))
        {
            return row.Tier;
        }
    }
    return 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Builds the 24-node table at a sim time from the registry, the belt's real asteroids and the base lanes
SOLLaneNodeTable::FTable SOLLaneNodeTable::Build(const FSOLBodyRegistry& registry,
    TConstArrayView<FSOLAsteroidDef> realAsteroids, TConstArrayView<SOLNpcScale::FLaneDef> baseLanes,
    const double secondsSinceJ2000)
{
    FTable table;
    table.Nodes.Reserve(NODE_COUNT);
    table.Names.Reserve(NODE_COUNT);
    table.MaxSpeedMps.Reserve(NODE_COUNT);
    const double centuries = secondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;
    bool bValid = realAsteroids.Num() >= ASTEROID_NODE_COUNT;

    // Registry nodes: aphelion about the parent (the Sun, or Earth for the Moon), speed bound from the perihelion
    // speed (the Moon adds Earth's)
    for (const FLaneNodeRow& row : LANE_NODE_ROWS)
    {
        FSOLLaneNode& node = table.Nodes.AddDefaulted_GetRef();
        table.Names.Add(FName(row.Name));
        node.Kind = row.Kind;
        node.BodyIndex = registry.FindByName(FName(row.Name));
        node.PopulationTier = row.Tier;
        node.RegionRadiusM = row.RegionRadiusM;
        double speedMps = 0.0;
        if (node.BodyIndex != INDEX_NONE && registry.GetParent(node.BodyIndex) != INDEX_NONE)
        {
            const FSOLKeplerElements elements = registry.GetElements(node.BodyIndex).AtCenturies(centuries);
            node.ClearanceRadiusM = registry.GetRadiusM(node.BodyIndex);
            node.AphelionM = LaneNodeApoapsisM(elements);
            speedMps = LaneNodePerihelionSpeedMps(elements, registry.GetOrbitGM(node.BodyIndex));
        }
        else
        {
            bValid = false;
        }
        table.MaxSpeedMps.Add(speedMps);
    }
    table.Nodes[MOON_NODE].ParentNode = EARTH_NODE;
    table.MaxSpeedMps[MOON_NODE] += table.MaxSpeedMps[EARTH_NODE];
    const int32 earthBody = table.Nodes[EARTH_NODE].BodyIndex;
    const int32 sunBody = earthBody != INDEX_NONE ? registry.GetParent(earthBody) : INDEX_NONE;
    table.SunGM = sunBody != INDEX_NONE ? registry.GetGM(sunBody) : 0.0;
    bValid = bValid && table.SunGM > 0.0;

    // Which registry nodes orbit the Sun directly (their position is their Sun-relative one)
    table.SunChild.Init(0, REGISTRY_NODE_COUNT);
    for (int32 index = 0; index < REGISTRY_NODE_COUNT; ++index)
    {
        const int32 body = table.Nodes[index].BodyIndex;
        table.SunChild[index] = body != INDEX_NONE && sunBody != INDEX_NONE && registry.GetParent(body) == sunBody
            && registry.GetParent(sunBody) == INDEX_NONE ? 1 : 0;
    }

    // Asteroid nodes: the catalogue's first rows (it is sorted by diameter, largest first), elements from the belt
    for (int32 index = 0; index < ASTEROID_NODE_COUNT && index < realAsteroids.Num(); ++index)
    {
        const FSOLAsteroidDef& asteroid = realAsteroids[index];
        const FSOLKeplerElements elements = asteroid.Elements.AtCenturies(centuries);
        FSOLLaneNode& node = table.Nodes.AddDefaulted_GetRef();
        table.Names.Add(asteroid.Name);
        node.Kind = ESOLLaneNodeKind::Asteroid;
        node.SourceIndex = index;
        node.ClearanceRadiusM = asteroid.RadiusM;
        node.AphelionM = LaneNodeApoapsisM(elements);
        node.RegionRadiusM = ASTEROID_REGION_RADIUS_M;
        node.PopulationTier = ASTEROID_TIER;
        table.AsteroidElements.Add(asteroid.Elements);
        table.MaxSpeedMps.Add(LaneNodePerihelionSpeedMps(elements, table.SunGM));
    }
    for (double& speedMps : table.MaxSpeedMps)
    {
        speedMps *= SOL::NPC_LANE_NODE_SPEED_MARGIN;
    }

    // Base lanes as sorted node pairs, in the layout's (slot) order
    table.BaseLanes.Reserve(baseLanes.Num());
    for (const SOLNpcScale::FLaneDef& lane : baseLanes)
    {
        const int32 a = table.Names.IndexOfByKey(lane.BodyA);
        const int32 b = table.Names.IndexOfByKey(lane.BodyB);
        FSOLLanePair& pair = table.BaseLanes.AddDefaulted_GetRef();
        pair.NodeA = FMath::Min(a, b);
        pair.NodeB = FMath::Max(a, b);
        bValid = bValid && a != INDEX_NONE && b != INDEX_NONE && a != b;
    }
    table.bValid = bValid && table.Nodes.Num() == NODE_COUNT;
    return table;
}

//////////////////////////////////////////////////////////////////////////
// Writes every node's Sun-frame position at a sim time
void SOLLaneNodeTable::PositionsAt(const FTable& table, const FSOLBodyRegistry& registry,
    const double secondsSinceJ2000, TArrayView<FVector3d> outPositionsM)
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    const int32 count = FMath::Min(table.Nodes.Num(), outPositionsM.Num());
    const double centuries = secondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;
    for (int32 index = 0; index < count; ++index)
    {
        const FSOLLaneNode& node = table.Nodes[index];
        if (node.Kind == ESOLLaneNodeKind::Asteroid)
        {
            outPositionsM[index] = LaneNodeAsteroidPositionM(table, index, centuries);
        }
        else if (node.BodyIndex == INDEX_NONE)
        {
            outPositionsM[index] = FVector3d(nan);
        }
        else if (node.ParentNode >= 0 && node.ParentNode < index)
        {
            // A moon adds its parent-relative position to its parent node's, already written (no second chain walk)
            outPositionsM[index] = outPositionsM[node.ParentNode]
                + registry.ComputeRelativePositionAtM(node.BodyIndex, secondsSinceJ2000);
        }
        else if (table.SunChild.IsValidIndex(index) && table.SunChild[index] != 0)
        {
            outPositionsM[index] = registry.ComputeRelativePositionAtM(node.BodyIndex, secondsSinceJ2000);
        }
        else
        {
            outPositionsM[index] = registry.ComputePositionAtM(node.BodyIndex, secondsSinceJ2000);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes only the asteroid nodes' Sun-frame positions at a sim time into a node-indexed array
void SOLLaneNodeTable::AsteroidPositionsAt(const FTable& table, const double secondsSinceJ2000,
    TArrayView<FVector3d> outPositionsM)
{
    const int32 count = FMath::Min(table.Nodes.Num(), outPositionsM.Num());
    const double centuries = secondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;
    for (int32 index = FIRST_ASTEROID_NODE; index < count; ++index)
    {
        if (table.Nodes[index].Kind == ESOLLaneNodeKind::Asteroid)
        {
            outPositionsM[index] = LaneNodeAsteroidPositionM(table, index, centuries);
        }
    }
}
