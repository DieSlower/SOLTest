<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SOLTest — Game Mechanics, Controls & Tunables

A catalog of every player-facing mechanic, control binding and tunable value, with the exact numbers and the source location that implements it. Consult it first when reasoning about gameplay. **Keep it in sync** whenever a mechanic, binding or value changes, and update the in-game Controls / Info / About screen in the same change (see `CLAUDE.md`, "Game mechanics catalog & in-game info").

Status column: **Designed** = decided in an SDD, not built; **Built** = implemented and tested. Nothing is built yet; source references get filled in as parts land.

---

## 1. Controls

| Input | Action | Status | Source |
|---|---|---|---|
| W / A / S / D + mouse | Fly the ship (third-person). Default UE-style bindings; flight-assist sets a target velocity | Designed (SDD 1) | — |
| J | Open the zoomable jump map | Designed (SDD 1) | — |
| L | Toggle surface-lock (level to nearest body) | Designed (SDD 1) | — |

Weapons, target-drop, speed-setting, flight-assist toggle and time-warp bindings are decided per part and will be added here.

## 2. Flight

| Mechanic | Value / rule | Status |
|---|---|---|
| Flight model | Flight-assist by default (target velocity vs a reference frame, drift and gravity cancelled by thrusters); toggle to pure Newtonian | Designed |
| Speed cap | Settable: match a planet's speed, or typed value | Designed |
| Gravity | Ship is pulled by nearby body gravity | Designed |

## 3. Time

| Mechanic | Value / rule | Status |
|---|---|---|
| Sim clock start | Real current date, 1x | Designed |
| Time-warp | Adjustable multiplier on bodies; ship physics always real-time | Designed |

## 4. Jump map (J)

| Mechanic | Value / rule | Status |
|---|---|---|
| Map | Zoomable, pick a destination | Designed |
| Warp animation | ~2-4 s with FX and sound | Designed |
| Arrival | Velocity matched to the nearest body; origin re-anchored | Designed |

## 5. Surface-lock (L)

| Mechanic | Value / rule | Status |
|---|---|---|
| Range | Within 1000 m of a body's surface | Designed |
| Alignment time | 5 s smooth rotation so the body's center is "down" | Designed |
| Behavior after aligning | Follows curvature until L pressed again or altitude exceeds ~1000 m (with hysteresis and warning) | Designed |
| No body in range | HUD hint only | Designed |

## 6. Origin anchoring

| Mechanic | Value / rule | Status |
|---|---|---|
| Anchor switch | Only when distance to a new body is under 25% of its distance to the current anchor (or second-closest body) | Designed |

## 7. Combat

| Mechanic | Value / rule | Status |
|---|---|---|
| Weapons | Projectile bolts (not hitscan), ship velocity inherited, limited lifetime | Designed |
| Targets | Player-dropped; health and shields; destruction VFX and sound | Designed |
| Numbers (damage, rate of fire, range, target health) | To be set in Part 6 | Open |
