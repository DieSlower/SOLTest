<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Adaptive tessellation for the Sun and planets

Requested by the user: as the player gets close, the Sun and planets should become more finely
tessellated so they always look like a perfectly round surface. Today, flying low, sharp polygon
edges are sometimes visible, mostly on the silhouette (limb).

Notes from earlier work: the faceted limb was seen at Saturn from about 40,000 km while testing the
gas giant near layer (issue #14, SDD 14 Amendment 3); the body meshes are the engine sphere
(`SOL::BODY_MESH_RADIUS_CM`) scaled per frame in `ASOLBodyVisuals`.

When picked up: grill the approach (a higher-density sphere or a small LOD set swapped by
apparent size, Nanite, or vertex-shader displacement of a spherical mesh to the exact radius),
budget, interaction with the gas giant deck/haze shells and the ring shadows, then write the SDD.
