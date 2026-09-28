<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Planetary perturbations / N-body integration

SDD 1 chose analytic Keplerian orbits with no perturbations. If long time-warp shows visible drift from real ephemerides, consider a correction layer (JPL secular terms, or numerically integrated major bodies).

- Must keep positions deterministic for any sim time (time-warp and jumps rely on it).
- Asteroid and ring particles would stay analytic.
