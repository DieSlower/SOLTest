<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SOLTest — Game Mechanics, Controls & Tunables

A catalog of every player-facing mechanic, control binding and tunable value, with the exact numbers and the source location that implements it. Consult it first when reasoning about gameplay. **Keep it in sync** whenever a mechanic, binding or value changes, and update the in-game Controls / Info / About screen in the same change (see `CLAUDE.md`, "Game mechanics catalog & in-game info").

Status column: **Designed** = decided in an SDD, not built; **Built** = implemented and tested. Source references are filled in as parts land.

---

## 1. Controls

| Input | Action | Status | Source |
|---|---|---|---|
| Mouse (virtual joystick) | Mouse motion moves a hidden stick (OS cursor hidden and locked); its offset sets the yaw (X) / pitch (Y, mouse up = nose up) rate. Radius 0.35 x the smaller viewport side (252 px at 720p), dead zone 0.05, response exponent 1.5 (`SOLFlight::JoystickToRotation`); full deflection = 90°/s. Quaternion rotation, no gimbal lock. When the game window loses focus the stick recenters and thrust, roll and boost release; leaving the ship shows the cursor again | Built (1b) | `Ship/SOLShipPawn.cpp`, `SOLConstants.h` |
| Middle mouse | Recenter the virtual joystick (stops turning) | Built (1b) | `Ship/SOLShipPawn.cpp` |
| W / S | Forward / backward thrust | Built (1b) | `Ship/SOLShipPawn.cpp` |
| A / D | Strafe left / right | Built (1b) | `Ship/SOLShipPawn.cpp` |
| Space / Left Ctrl | Thrust up / down | Built (1b) | `Ship/SOLShipPawn.cpp` |
| Q / E | Roll left / right (120°/s) | Built (1b) | `Ship/SOLShipPawn.cpp` |
| Left Shift (hold) | Boost | Built (1b) | `Ship/SOLShipPawn.cpp` |
| Tab | Toggle flight-assist (starts ON) | Built (1b) | `Ship/SOLShipPawn.cpp` |
| Left Alt (hold) | Free-look: the stick freezes and the mouse orbits the camera (0.2°/px, pitch ±80°); release returns behind the ship | Built (1b) | `Ship/SOLShipPawn.cpp` |
| Mouse wheel | Step the speed cap by 10^0.1 per notch (10 notches per decade), 1 m/s to 0.5c; starts at 1000 m/s | Built (1b) | `Ship/SOLShipPawn.cpp`, `Flight/SOLFlight.cpp` |
| M | Lock the reference frame to the selected target (or the anchor/nearest body if none) and match its velocity; stays locked if the anchor or selection changes; again to release | Built (1b) | `Targeting/SOLTargetingSubsystem.cpp` |
| T / R / F / X | Select the target nearest the forward reticle (10° cone) / next farther / next nearer (by distance, wrapping) / clear | Built (1b) | `Targeting/SOLTargetingSubsystem.cpp`, `Flight/SOLTargeting.cpp` |
| F3 | Speed panel: typed speed (m/s, km/s, c) and body orbital speed list | Designed (SDD 2, reserved for 1c) | — |
| [ / ] / Backspace | Time-warp down / up / reset to 1x | Built (1b ship pawn; also on the debug spectator) | `Ship/SOLShipPawn.cpp` |
| O / P | Toggle body orbit lines / ship predicted path | Designed (SDD 2, reserved for 1c) | — |
| J | Open the zoomable jump map | Designed (SDD 1) | — |
| L | Toggle surface-lock (level to nearest body) | Designed (SDD 1) | — |

Weapons and target-drop bindings are decided per part and will be added here.

### 1.1 Debug and verification switches

The ship pawn (`Source/SOLTest/Ship/SOLShipPawn.cpp`) is the default pawn. `-SOLSpectator` swaps in the old free-fly verification camera (`Source/SOLTest/Game/SOLSpectatorPawn.cpp`), which rides the ship as a free-look camera (mouse look, wheel and warp keys; it does not move the ship). `-SOLStart=<Body>` and `-SOLAltitudeKm=<km>` choose where the ship spawns and `-SOLLookAt=<Body>` aims the spectator. `-SOLSmokeFlight` flies a scripted physics sequence (`Ship/SOLShipSmokeFlight.cpp`); `-SOLSmokeInput` drives the real input pipeline with injected key, mouse and wheel events (`Ship/SOLShipSmokeInput.cpp`); both log a PASS/FAIL per check and quit (`-SOLSmokeInput` after a screenshot). `-SOLSmokeShot=<s>` takes a screenshot after s seconds and quits.

| Input (spectator only) | Action | Value |
|---|---|---|
| Mouse | Look (yaw/pitch) | Engine default look scale |
| Mouse wheel | Step the spectator's own speed cap on a log scale | 3 steps per decade, 1 m/s to 0.5c; starts at 10 km/s |
| [ / ] / Backspace | Time-warp down / up / reset | See section 3 |

## 2. Flight

| Mechanic | Value / rule | Status |
|---|---|---|
| Flight model | Flight-assist by default (target velocity vs a reference frame, drift and gravity cancelled by thrusters); Tab toggles pure Newtonian. The ship is a Mass entity stepped on real time in substeps of at most 1/30 s (at most 16 per frame); a frame longer than 0.5 s (a hitch) is clamped to 0.5 s for both the sim clock and the ship, so the whole universe slows uniformly | Built (1b), `Flight/SOLFlight.cpp`, `Ship/SOLShipFlightProcessor.cpp` |
| Speed cap | Relative to the reference frame; starts at 1000 m/s; wheel steps x10^0.1, clamped 1 m/s to 0.5c (149,896 km/s). Typed entry (F3) comes in 1c | Built (1b wheel), `Ship/SOLShipPawn.cpp`, `SOLConstants.h` |
| Assist acceleration | Time constant 1.5 s (0.5 s with boost); boost doubles the target speed | Built (1b), `Flight/SOLFlight.h` |
| Newtonian acceleration | 50 m/s² forward/back, 30 m/s² strafe and vertical; boost 3x | Built (1b), `Flight/SOLFlight.h` |
| Rotation | Rate command, 90°/s pitch/yaw, 120°/s roll, 0.15 s response, quaternion (no gimbal lock), same in both modes | Built (1b), `Flight/SOLFlight.h` |
| Reference frame | Assist frame = anchor (nearest) body's velocity; M locks it to the selected target (else the anchor) until M is pressed again. Planets are targetable; future enemies/targets plug in via `ISOLTargetable` | Built (1b), `Targeting/SOLTargetingSubsystem.cpp` |
| Gravity | Sum over the Sun and all planets (applies in Newtonian mode; assist cancels it) | Built (1b), `Ship/SOLShipFlightProcessor.cpp` |
| Spawn | 10,000 km above Earth on its Sun-facing side, Earth's velocity plus circular-orbit velocity (4.93 km/s) | Built (1b), `Ship/SOLShipSubsystem.cpp` |
| Collision | Analytic swept sphere against every body incl. the Sun, per substep in the body's frame (no tunnelling, even at 0.5c or under warp): velocity into the surface zeroed, ship rests at its 10 m radius above the surface and can slide; no damage yet | Built (1b), `Ship/SOLShipFlightProcessor.cpp` |
| Ship | Placeholder ~21 m ship (16 m span) from engine primitives, light grey-blue hull, orange engine glow; 10 m collision radius | Built (1b), `Ship/SOLShipPawn.cpp` |
| Camera | Spring-arm chase cam 60 m behind, 15 m above; rotation lag speed 8 (exponential slerp applied after the ship's orientation update), no position lag; shadowless fill light on the camera at 10% of sunlight, tilted 45° down, so the ship's shadowed side stays visible; FOV 90° widening to 110° on a log scale of speed relative to the frame (100 m/s to 0.5c); Alt free-look | Built (1b), `Ship/SOLShipPawn.cpp`, `SOLConstants.h` |
| Speed readout | Relative to the active frame (locked target or anchor body) with the frame name; absolute Sun-frame speed beside it; selected target's distance and relative speed (temporary debug HUD until 1c) | Built (1b debug HUD), `UI/SOLDebugHUD.cpp` |

## 3. Time

| Mechanic | Value / rule | Status |
|---|---|---|
| Sim clock start | Real current UTC date and time, 1x; stops while the game is paused | Built (1a), `Universe/SOLSimClockSubsystem.cpp` |
| Time-warp | Steps 1x, 10x, 100x, 1000x, 1 h/s, 1 d/s, 10 d/s, 30 d/s; ship physics always real-time, but the ship is carried with its reference frame (M-locked target, else the anchor body), so it stays put relative to that frame (in orbit or on its surface) at any warp; no auto-clamp | Built (1a), `Universe/SOLSimClock.cpp` |

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
