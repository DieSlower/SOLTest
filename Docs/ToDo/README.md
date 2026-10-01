<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# ToDo — Future Planned Items

A lightweight backlog of ideas and follow-up work that's been identified but not yet
scheduled as a ticket/branch. Unlike [`Docs/SDDs/`](../SDDs/) and
[`Docs/Plans/`](../Plans/) (design docs and plans for work that already has an issue
number), these are pre-ticket: the "we should do this eventually" list.

Not the same thing as the **Known tech debt to revisit** section in
[`../../CLAUDE.md`](../../CLAUDE.md) — that list is imperfections in code that already
exists (a shortcut taken, a cleanup deferred). This folder is for **new capabilities or
follow-up features** that haven't been built yet.

## Naming

`<short-content-slug>.md` — no issue number, since these are pre-ticket. Example:
`supercruise-travel.md`.

## Lifecycle

When an item here is picked up, it graduates into a normal ticket: create the issue and
branch, write the SDD under `Docs/SDDs/<issue-number>-<slug>.md` (the ToDo file's content is
a good starting point, not a substitute — run `grill-me` as usual), and once the SDD
exists, delete the ToDo file rather than letting the same idea live in two places.

An item can also close without being built, if the problem it was tracking stops
existing. Delete the file either way — the reasoning stays discoverable in whichever
SDD's *Revision history* prompted the closure; this folder is a live backlog, not an
archive.

## Items

- [`supercruise-travel.md`](supercruise-travel.md) — optional long-travel, very-high-speed
  flight regime (Elite-style supercruise) layered on top of the jump map.
- [`planetary-perturbations.md`](planetary-perturbations.md) — optional gravitational
  perturbations or N-body integration for bodies, beyond pure Keplerian orbits.
- [`surface-landing.md`](surface-landing.md) — manual and auto (from-orbit) ship
  landing, building on Part 3's surface-lock.
- [`accurate-pole-directions.md`](accurate-pole-directions.md) — real per-body axial
  pole directions and precession, deferred from Part 3.5's simplified fixed-plane tilt.
- [`star-diffraction-spikes.md`](star-diffraction-spikes.md) — the camera-lens
  cross/starburst flare on bright stars, deferred from Part 4's star field.
- [`asteroid-ring-collision.md`](asteroid-ring-collision.md) — real collision against
  asteroid-belt and ring-particle bodies, deferred from Part 5.
