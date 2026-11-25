/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcScaleMath.h"
#include "Traffic/SOLLaneNetworkMath.h"
#include "Universe/SOLKepler.h"

#include "CoreMinimal.h"

class FSOLBodyRegistry;
struct FSOLAsteroidDef;

// The lane network's v1 nodes (SDD 53 section 4.2): the 8 planets, the Moon, 5 dwarf planets from the body registry
// and the 10 largest belt asteroids from SOLAsteroidBelt::RealAsteroids() (the Npc -> MinorBodies edge), with their
// population tiers (O6), region radii (O5) and aphelia (L0, D16). The single source of the tier values: the default
// layout's base-lane weights read TierOf too
namespace SOLLaneNodeTable
{
    inline constexpr int32 REGISTRY_NODE_COUNT = 14;   // Nodes 0-13 come from the registry
    inline constexpr int32 ASTEROID_NODE_COUNT = 10;   // Nodes 14-23 are the 10 largest belt asteroids
    inline constexpr int32 NODE_COUNT = REGISTRY_NODE_COUNT + ASTEROID_NODE_COUNT;
    inline constexpr int32 FIRST_ASTEROID_NODE = REGISTRY_NODE_COUNT;
    inline constexpr int32 EARTH_NODE = 2;             // The island links' root (O2)
    inline constexpr int32 MOON_NODE = 8;
    inline constexpr double ASTEROID_TIER = 3.0;       // Every belt asteroid node (O6)
    inline constexpr double ASTEROID_REGION_RADIUS_M = 4.0e6;   // The belt's region radius (O5)

    // The node table built for one population, plus what the node positions need
    struct FTable
    {
        TArray<FSOLLaneNode> Nodes;                     // Node order of SDD 53 4.2
        TArray<FName> Names;                            // Registry name or the asteroid's designation ("4 Vesta")
        TArray<FSOLSecularElements> AsteroidElements;   // Per asteroid node, Sun-relative (node - FIRST_ASTEROID_NODE)
        TArray<double> MaxSpeedMps;                     // Per node, perihelion speed x NPC_LANE_NODE_SPEED_MARGIN
        TArray<uint8> SunChild;                         // Per registry node, 1 when its registry parent is the Sun
        TArray<FSOLLanePair> BaseLanes;                 // The layout's lanes as node pairs, in slot order (Q23)
        double SunGM = 0.0;                             // For the asteroid propagation
        bool bValid = false;                            // Every node and base lane was found
    };

    // Returns a registry node's population tier by body name (O6), 0 when the body is not a lane node
    SOLTEST_API double TierOf(FName bodyName);

    // Builds the node table at a sim time (aphelia and speeds from the elements then) from the registry, the belt's
    // real asteroids (the first 10, the largest) and the layout's base lanes
    SOLTEST_API FTable Build(const FSOLBodyRegistry& registry, TConstArrayView<FSOLAsteroidDef> realAsteroids,
        TConstArrayView<SOLNpcScale::FLaneDef> baseLanes, double secondsSinceJ2000);

    // Writes every node's Sun-frame position at a sim time (registry chain, Kepler for the asteroids); NaN for a node
    // the table could not resolve. The Moon adds its Earth-relative position to Earth's (one Kepler solve per node).
    // Reads only const data, so it may run on worker threads (the schedule's parallel rebuild)
    SOLTEST_API void PositionsAt(const FTable& table, const FSOLBodyRegistry& registry, double secondsSinceJ2000,
        TArrayView<FVector3d> outPositionsM);

    // Writes only the asteroid nodes' Sun-frame positions at a sim time into a node-indexed array (the same values as
    // PositionsAt), leaving the registry nodes' entries untouched
    SOLTEST_API void AsteroidPositionsAt(const FTable& table, double secondsSinceJ2000,
        TArrayView<FVector3d> outPositionsM);
}
