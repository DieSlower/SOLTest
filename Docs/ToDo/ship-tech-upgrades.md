<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Pilot and ship tech upgrades

Deferred from issue #18 (SDD 18 decision 25 / open question N6). The flight core already expresses the pilot's G
tolerance and the ship's thrust as data (`SOLNewtonian::FSOLPilotGLimits` and `FSOLThrustEnvelope`), and the defaults
are the untrained baseline. Nothing upgrades them yet.

- **Pilot G tolerance:** equipment (G-suit, training, implants) raises `SustainedG` (default 1.5 G) and lengthens
  `TimeToEmptyAtMaxGS` (default 10 s at 9 G). Raising `SustainedG` shortens every autopilot trip by `sqrt(old/new)`.
- **Ship thrust:** engine and thruster upgrades raise the per-axis box (3 / 1.5 / 2 / 2 G), the boost multiplier and
  the structural total cap.
- **Persistence:** the upgrades are saved with the ship/pilot; the G meter itself stays unsaved (full on load).
- Needs an equipment / ship-loadout system and its save schema first (versioned, per the persistence rules).
