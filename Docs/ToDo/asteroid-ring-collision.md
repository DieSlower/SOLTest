<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Asteroid and ring-particle collision

Deferred during the Part 5 (#6) grill: the user's stated intent is that ring asteroids
in particular should eventually be dangerous to fly through ("they are not harmless"),
but real collision against a large, continuously-moving Mass population (the asteroid
belt's ~51,000 bodies, and whatever near-field ring-particle count is active at once)
is a substantial feature of its own — swept collision at that scale, every frame,
against entities with no individual actor/collision component, is a different problem
than the existing single-ship-vs-registry-body swept-sphere collision
(`SOLFlight::ResolveSweptSphereCollision`).

If/when this is picked up:
- Likely needs spatial partitioning (a grid or octree over the Mass archetype) so the
  ship only tests collision against nearby minor bodies, not the whole population
  every frame.
- Decide what "colliding with an asteroid" means for the ship (damage, a hard stop
  like a planet surface, destruction of the asteroid, or some mix) — this is gameplay
  design, not just a physics problem, and probably overlaps with Part 6's combat/
  destruction work.
- Grill this properly when picked up.
