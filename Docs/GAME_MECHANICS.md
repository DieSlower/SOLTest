<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SOLTest — Game Mechanics, Controls & Tunables

A catalog of every player-facing mechanic, control binding and tunable value, with the exact numbers and the source location that implements it. Consult it first when reasoning about gameplay. **Keep it in sync** whenever a mechanic, binding or value changes, and update the in-game Controls / Info / About screen in the same change (see `CLAUDE.md`, "Game mechanics catalog & in-game info").

Status column: **Designed** = decided in an SDD, not built; **Built** = implemented and tested. Nothing is built yet; source references get filled in as parts land.

---

## 1. Controls

| Input | Action | Status | Source |
|---|---|---|---|
| Mouse (virtual joystick) | Offset from screen center sets pitch/yaw turn rate; reticle and dead zone; quaternion rotation, no gimbal lock | Designed (SDD 2) | — |
| W / S | Forward / backward thrust | Designed (SDD 2) | — |
| A / D | Strafe left / right | Designed (SDD 2) | — |
| Space / Ctrl | Thrust up / down | Designed (SDD 2) | — |
| Q / E | Roll | Designed (SDD 2) | — |
| Shift | Boost | Designed (SDD 2) | — |
| Tab | Toggle flight-assist | Designed (SDD 2) | — |
| Alt (hold) | Free-look camera | Designed (SDD 2) | — |
| Mouse wheel | Step speed cap on a log scale (1 m/s to 0.5c) | Designed (SDD 2) | — |
| M | Match velocity of the selected target (or nearest body if none) and use it as reference frame; again to release | Designed (SDD 2) | — |
| T / R / F / X | Select target under reticle / next / previous / clear | Designed (SDD 2) | — |
| F3 | Speed panel: typed speed (m/s, km/s, c) and body orbital speed list | Designed (SDD 2) | — |
| [ / ] / Backspace | Time-warp down / up / reset to 1x | Built (1a, debug spectator) | `Game/SOLSpectatorPawn.cpp` |
| O / P | Toggle body orbit lines / ship predicted path | Designed (SDD 2) | — |
| J | Open the zoomable jump map | Designed (SDD 1) | — |
| L | Toggle surface-lock (level to nearest body) | Designed (SDD 1) | — |

Weapons, target-drop, speed-setting, flight-assist toggle and time-warp bindings are decided per part and will be added here.

### 1.1 Debug spectator (sub-part 1a, temporary until the ship lands in 1b)

A free-fly verification camera (`Source/SOLTest/Game/SOLSpectatorPawn.cpp`), the current default pawn.

| Input | Action | Value |
|---|---|---|
| W / S, A / D, Space / Ctrl | Move forward/back, left/right, up/down relative to the view | At the speed cap |
| Mouse | Look (yaw/pitch) | Engine default look scale |
| Mouse wheel | Step the speed cap on a log scale | 3 steps per decade, 1 m/s to 0.5c (149,896 km/s); starts at step 12 = 10 km/s |
| [ / ] / Backspace | Time-warp down / up / reset | See section 3 |
| Command line | `-SOLStart=<Body>` `-SOLAltitudeKm=<km>` `-SOLLookAt=<Body>` | Default: Earth, 10,000 km, looking at the start body |

## 2. Flight

| Mechanic | Value / rule | Status |
|---|---|---|
| Flight model | Flight-assist by default (target velocity vs a reference frame, drift and gravity cancelled by thrusters); toggle to pure Newtonian | Designed |
| Speed cap | Log scale 1 m/s to 0.5c; set by wheel, by matching the target's speed, or typed | Designed |
| Assist acceleration | ~1-2 s time constant to reach the target velocity at any magnitude | Designed |
| Newtonian acceleration | Fixed maximum acceleration in m/s² (value set in Part 1b) | Open |
| Gravity | Ship is pulled by nearby body gravity | Designed |
| Spawn | ~10,000 km above Earth, Earth's velocity plus circular-orbit velocity | Designed |
| Collision | Stop at the surface (velocity into surface zeroed, altitude clamped); no damage yet | Designed |
| Camera | Chase cam with lag, FOV widens with speed, Alt free-look | Designed |
| Speed readout | Relative to the reference frame (target or anchor body); absolute Sun-frame speed shown smaller | Designed |

## 3. Time

| Mechanic | Value / rule | Status |
|---|---|---|
| Sim clock start | Real current UTC date and time, 1x; stops while the game is paused | Built (1a), `Universe/SOLSimClockSubsystem.cpp` |
| Time-warp | Steps 1x, 10x, 100x, 1000x, 1 h/s, 1 d/s, 10 d/s, 30 d/s; ship physics always real-time; no auto-clamp | Built (1a), `Universe/SOLSimClock.cpp` |

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
| Anchor switch | Only when distance to a new body is under 25% of its distance to the current anchor (or second-closest body); a teleport re-picks the plain nearest body | Built (1a), `Universe/SOLAnchor.cpp`, `Universe/SOLAnchorSubsystem.cpp` |
| Render origin | Snaps to the observer on an anchor change or after 10 km of drift; bodies beyond 1,000,000 km are drawn closer with their angular size preserved | Built (1a), `SOLConstants.h` |

## 7. Combat

| Mechanic | Value / rule | Status |
|---|---|---|
| Weapons | Projectile bolts (not hitscan), ship velocity inherited, limited lifetime | Designed |
| Targets | Player-dropped; health and shields; destruction VFX and sound | Designed |
| Numbers (damage, rate of fire, range, target health) | To be set in Part 6 | Open |
