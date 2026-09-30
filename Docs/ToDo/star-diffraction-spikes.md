<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Star diffraction spikes

Deferred during the Part 4 (#5) star-field grill: bright stars render as simple
round/soft sprites for now. CelestialVault's own star material already supports a
diffraction-spike (cross/starburst) look, which reads as more "photographic" for the
brightest stars.

If/when this is picked up: adding it should only mean swapping/extending the bright-star
material (`Content/SOL/` copy of the relevant CelestialVault material) — the data
pipeline (`Tools/StarField/`) and the `UInstancedStaticMeshComponent` setup shouldn't
need to change. Grill any real design questions (e.g. does spike intensity scale with
magnitude, is it view-angle-dependent) when picked up.
