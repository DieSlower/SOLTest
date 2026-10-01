<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Accurate per-body pole directions and precession

Deferred during the Part 3.5 (#12) planet-axial-rotation grill: that ticket gives each
body a real axial tilt *magnitude* (e.g. Earth 23.44°, Uranus 97.77°) but tilts every
body's pole within the same fixed reference plane, rather than each body's real
individual pole direction (right ascension/declination, from the IAU WGCCRE report or
similar), and models no precession (Earth's ~26,000-year axial precession, or the
faster precession of some other bodies).

Also noted during 12a's adversarial review: the tilt magnitudes used (SDD 12 §3.1.1)
are each body's obliquity relative to *its own orbital plane* (the standard published
figure), but they're applied in code as a tilt away from *ecliptic*-north — a body
whose own orbital plane is meaningfully inclined from the ecliptic (Mercury's ~7°
inclination is the largest of the eight) will have its true ecliptic-relative tilt
slightly different from the stored value. Within SDD 12 decision 3's stated
simplification, worth folding into the same future accuracy pass rather than treated
as a separate issue.

Concrete consequences found while adding moons in Part 5 (#6, SDD 6 §3.1): every moon's
orbit is tilted into its host's rendered equator using the host's fake fixed-plane pole
azimuth, so (a) a host's moons share one common, not-astronomically-real phase offset
from the real sky (their phases relative to *each other* stay correct), and (b)
Triton's forced spin pole sits ~35° from its real orbit normal, so a synchronously-locked
Triton will visibly nod/wobble once textured rather than holding steady. Both are
one-line, spot-identifiable symptoms of this same simplification — useful test cases for
whichever fix is chosen here.

If/when this is picked up:
- Store each body's real published pole RA/Dec (or the equivalent ecliptic-frame unit
  vector) instead of a single tilt-magnitude-in-a-fixed-plane.
- Precession would need a slow secondary rotation of the pole vector over time — decide
  whether it's worth modeling given this project's existing "no planetary
  perturbations" simplification for orbits (a similar-spirit simplification might mean
  leaving precession out even after this upgrade).
- Grill this properly when picked up; it's a small but real accuracy upgrade over
  SDD 12's simplification, not a new mechanic.
