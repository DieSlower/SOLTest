<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Asteroid and ring-particle collision

Deferred during the Part 5 (#6) grill: the user's stated intent is that ring asteroids
in particular should eventually be dangerous to fly through ("they are not harmless"),
but real collision against a large, continuously-moving Mass population (the asteroid
belt's ~9,268 bodies — see SDD 6 Amendment 3 for the corrected count — and whatever
near-field ring-particle count is active at once) is a substantial feature of its own —
swept collision at that scale, every frame, against entities with no individual
actor/collision component, is a different problem than the existing single-ship-vs-
registry-body swept-sphere collision (`SOLFlight::ResolveSweptSphereCollision`).

**Re-confirmed during 5e's grill (SDD 6 Amendment 6)**, after the near-field ring
design grew from a sparse belt-like population into a denser, player-streamed patch
(~50-150 m rock spacing) specifically meant to feel like a real, flyable ring: the
user explicitly re-confirmed collision stays deferred even so. Worth noting for
whoever picks this up next: 5e ships a ring you can fly straight through with zero
consequence, which reads as more obviously "fake" now that the near field is dense
enough to look and feel like a real hazard — the cost of staying collision-less went
up, even though the decision to defer didn't change.

If/when this is picked up:
- Likely needs spatial partitioning (a grid or octree over the Mass archetype) so the
  ship only tests collision against nearby minor bodies, not the whole population
  every frame.
- Decide what "colliding with an asteroid" means for the ship (damage, a hard stop
  like a planet surface, destruction of the asteroid, or some mix) — this is gameplay
  design, not just a physics problem, and probably overlaps with Part 6's combat/
  destruction work.
- Grill this properly when picked up.
