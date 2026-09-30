<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Ship landing (manual and auto)

Raised during the Part 3 (#4) surface-lock grill and deliberately kept out of that
ticket's scope: surface-lock (L) is an orientation aid only (ship "up" tracks away
from the locked body while flying near it), not a landing mechanic. It rides on the
existing generic sphere collision (`SOLFlight::ResolveSweptSphereCollision`), which
just stops the ship at contact and removes inward velocity — no parked/landed state.

Two related follow-up capabilities to build once this is picked up:

- **Manual landing.** A pilot-flown touchdown: while surface-locked and close to the
  surface, some control (existing thrust, or a dedicated assist) lets the player bring
  the ship to rest on the surface and have it register as "landed" (zero relative
  velocity, held in place relative to the body, likely requiring the body's own
  rotation from the (separate, also-not-yet-built) planet-spin ticket to be accounted
  for so a landed ship rotates with the ground rather than drifting).
- **Auto-landing from orbit.** An autopilot sequence: from a stable orbit or cruise, the
  player commands automatic descent and touchdown near/at a target point, without
  manual flying, ending in the same "landed" state as a manual landing.

Both need a real "landed" ship state (distinct from flying), most likely as a new
field on `FSOLShipState` or a parallel flag the ship subsystem tracks, plus rules for
what re-enables flight (any thrust input? a dedicated "launch" action?). Real terrain
(Part 9, PCG/Nanite) will also change what "the surface" means for a landing point —
this may be worth resequencing after Part 9 rather than building it against the
placeholder analytic spheres.

Grill this properly when picked up: it touches the flight model, the ship state
schema, and possibly the HUD/input scheme non-trivially.
