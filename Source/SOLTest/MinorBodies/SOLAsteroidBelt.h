/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Universe/SOLKepler.h"

#include "CoreMinimal.h"

// One asteroid-belt body: a named real asteroid (Name set, FamilyId == INDEX_NONE) or a procedural family-cluster
// fill member (Name == NAME_None, FamilyId is its cluster's index)
struct SOLTEST_API FSOLAsteroidDef
{
    FName Name;                                // NAME_None for a procedural fill member
    FSOLSecularElements Elements;              // Sun-relative orbital elements (every rate term other than
                                                // LDotDegPerCy is 0 for every asteroid row)
    double RadiusM = 0.0;                      // mean radius, meters
    int32 FamilyId = INDEX_NONE;               // procedural cluster index (0-based, generation order); INDEX_NONE
                                                // for a real asteroid
};

// An inclusive-low, exclusive-high semi-major-axis band (AU) excluded from procedural cluster placement
struct SOLTEST_API FSOLAxisBandAU
{
    double LowAU = 0.0;
    double HighAU = 0.0;
};

// Pure-logic generation of the asteroid belt (SDD 6 Section 3.2 / Amendment 2): ~500 real named asteroids plus a
// deterministic procedural "family cluster" fill. No Mass/UE dependency here — Source/SOLTest/MinorBodies' Mass
// archetype (5c) consumes this module's output to populate entities; this module only produces orbital-element data.
//
// Belt ranges used by the procedural generator (SDD 6 Section 2, decision 3): semi-major axis 2.1-3.3 AU,
// eccentricity 0.0-0.3, inclination 0-25 deg — typical main-asteroid-belt bounds, not a hard physical limit (a
// handful of real asteroids fall slightly outside; the ~500 real asteroids in RealAsteroids() are NOT clamped to
// these ranges, only the procedural cluster centers are).
namespace SOLAsteroidBelt
{
    // Real-belt semi-major-axis bounds procedural cluster centers are placed within (AU)
    SOLTEST_API extern const double BELT_MIN_SEMI_MAJOR_AXIS_AU;
    SOLTEST_API extern const double BELT_MAX_SEMI_MAJOR_AXIS_AU;

    // Procedural cluster center bounds on eccentricity and inclination (deg)
    SOLTEST_API extern const double BELT_MAX_ECCENTRICITY;
    SOLTEST_API extern const double BELT_MAX_INCLINATION_DEG;

    // Default family-cluster generation parameters (SDD 6 decision 3): ~200-400 clusters of 10-50 members each,
    // within 20-100 km of their cluster center
    SOLTEST_API extern const int32 DEFAULT_CLUSTER_COUNT;
    SOLTEST_API extern const int32 DEFAULT_MIN_MEMBERS_PER_CLUSTER;
    SOLTEST_API extern const int32 DEFAULT_MAX_MEMBERS_PER_CLUSTER;
    SOLTEST_API extern const double DEFAULT_MIN_CLUSTER_RADIUS_M;
    SOLTEST_API extern const double DEFAULT_MAX_CLUSTER_RADIUS_M;

    // Returns the ~500 real named asteroids: fixed data, no randomness, every call returns the identical list in the
    // identical order. Every entry has a non-empty Name, FamilyId == INDEX_NONE, RadiusM > 0, and Elements with every
    // rate term (ADotAUPerCy, EDotPerCy, IDotDegPerCy, LongPeriDotDegPerCy, LongNodeDotDegPerCy) other than
    // LDotDegPerCy equal to 0 (epoch elements only, consistent with every other minor/dwarf body in this project).
    // Sourced from a real public catalog (JPL Small-Body Database or equivalent), cited in the .cpp's sourcing
    // comment.
    SOLTEST_API TArray<FSOLAsteroidDef> RealAsteroids();

    // Returns the real Kirkwood gap bands (semi-major-axis bands, AU, excluded from procedural cluster placement):
    // the 3:1, 5:2 and 2:1 Jupiter mean-motion resonances, approximately centered at 2.50, 2.82 and 3.27 AU. Always
    // returns the same 3 bands in ascending-AU order.
    SOLTEST_API TArray<FSOLAxisBandAU> KirkwoodGapBandsAU();

    // Returns true if semiMajorAxisAU falls inside any band from KirkwoodGapBandsAU() (LowAU inclusive, HighAU
    // exclusive)
    SOLTEST_API bool IsInKirkwoodGap(double semiMajorAxisAU);

    // Returns the clusterCount cluster-center orbital elements that GenerateFamilyFill(seed, clusterCount, ...)
    // uses as the template every member of that cluster is scattered around (element [i] is cluster i's center,
    // 0-based). Every center's semi-major axis is within [BELT_MIN_SEMI_MAJOR_AXIS_AU, BELT_MAX_SEMI_MAJOR_AXIS_AU)
    // and NOT in any Kirkwood gap band (IsInKirkwoodGap returns false for it); eccentricity is within
    // [0, BELT_MAX_ECCENTRICITY] and inclination within [0, BELT_MAX_INCLINATION_DEG]. Every rate term other than
    // LDotDegPerCy is 0. Deterministic: the same seed and clusterCount always return the identical list. Calling
    // this with the same seed/clusterCount GenerateFamilyFill(seed, clusterCount, ...) was called with is guaranteed
    // to return exactly the centers that call actually used, regardless of the member-count/cluster-radius
    // parameters passed to GenerateFamilyFill (those parameters do not affect cluster placement, only each
    // cluster's member scatter).
    SOLTEST_API TArray<FSOLSecularElements> ClusterCenters(int32 seed, int32 clusterCount);

    // Deterministically generates the procedural family-cluster fill: clusterCount clusters (see ClusterCenters()),
    // each independently given a member count uniformly distributed in [minMembersPerCluster, maxMembersPerCluster]
    // (inclusive) and a cluster radius uniformly distributed in [minClusterRadiusM, maxClusterRadiusM] (inclusive),
    // then scattering that many procedural members at random offsets of magnitude at most the cluster's own radius
    // from the cluster center. Returned members appear grouped by cluster in ascending FamilyId order (0 first);
    // every member has Name == NAME_None, FamilyId in [0, clusterCount), and RadiusM > 0.
    //
    // Physical cohesion (the gameplay point of a cluster): for every member, SOLKepler::ElementsToState(member's
    // elements, the Sun's GM, elapsedSeconds) and SOLKepler::ElementsToState(that member's cluster center's
    // elements from ClusterCenters(), the Sun's GM, elapsedSeconds) are within 2x that cluster's own radius bound of
    // each other for every elapsedSeconds within +/- 100 years of J2000 (evaluated via FSOLSecularElements::
    // AtCenturies before calling ElementsToState, same as every other orbital body in this project) - not just at
    // J2000 itself. This holds exactly, not just approximately: every member shares its cluster center's A0AU and
    // LDotDegPerCy exactly (only the other elements differ), so the member-to-center separation is an exact
    // periodic function of time with period 360/LDotDegPerCy, bounded for all time by construction (the member's
    // offset is shrunk at generation time until its worst-case separation over one full period fits the 2x bound) -
    // a real guarantee of this function's generation method, not a loose aspiration.
    //
    // Determinism: the same seed, clusterCount, minMembersPerCluster, maxMembersPerCluster, minClusterRadiusM and
    // maxClusterRadiusM always produce the identical list (same members, same order, same values) on every call,
    // in this process or a fresh one.
    SOLTEST_API TArray<FSOLAsteroidDef> GenerateFamilyFill(int32 seed, int32 clusterCount, int32 minMembersPerCluster,
        int32 maxMembersPerCluster, double minClusterRadiusM, double maxClusterRadiusM);

    // Returns RealAsteroids() followed by GenerateFamilyFill(seed, DEFAULT_CLUSTER_COUNT,
    // DEFAULT_MIN_MEMBERS_PER_CLUSTER, DEFAULT_MAX_MEMBERS_PER_CLUSTER, DEFAULT_MIN_CLUSTER_RADIUS_M,
    // DEFAULT_MAX_CLUSTER_RADIUS_M) — the project's default-configured belt. Deterministic for a given seed, same
    // guarantees as the two functions it composes.
    SOLTEST_API TArray<FSOLAsteroidDef> GenerateBelt(int32 seed);
}
