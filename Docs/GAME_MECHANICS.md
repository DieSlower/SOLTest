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
| F3 | Speed panel (UMG, built in C++): odometer VALUE digits (10^6 .. 0.01) beside a UNIT field (m/s, km/s, c), seeded from the current cap. Up/Down or wheel step the highlighted digit with carry (on the unit field: cycle the unit), Left/Right move the highlight (right of the hundredths = unit field), U cycles the unit, Tab switches to the scrollable list of every body with its orbital speed (relative to its parent), where Up/Down select and Enter sets the cap to that body's speed. Each step applies to the ship at once (cap clamped 1 m/s to 0.5c). Enter (on the steppers), F3 or Esc close it. While open the ship's own control input is suspended (keys flushed, mapping removed, thrust/roll/boost/stick zeroed); the simulation keeps running | Built (1c) | `UI/SOLSpeedPanelWidget.cpp`, `Ship/SOLShipPawn.cpp` |
| [ / ] / Backspace | Time-warp down / up / reset to 1x | Built (1b ship pawn; also on the debug spectator) | `Ship/SOLShipPawn.cpp` |
| O | Toggle body orbit ellipses on the HUD (off by default): every planet's orbit sampled at 90 segments from its current secular elements (re-sampled every 5 s while shown), the selected target's orbit in yellow | Built (1c) | `UI/SOLFlightHud.cpp`, `UI/SOLOrbitLines.cpp` |
| P | Ship predicted path under gravity: key bound but a no-op (deferred, SDD 2 section 6) | Reserved (1c) | `Ship/SOLShipPawn.cpp` |
| - / = / Home | Radar zoom in / out by a factor of 10 (switches the radar to MANUAL range; a press that cannot change the range is a no-op) / back to AUTO range; see Radar below | Built (1c) | `Ship/SOLShipPawn.cpp`, `UI/SOLFlightHud.cpp` |
| J | Open the jump map (see section 4): the ship's own control input is suspended exactly as for F3 (keys flushed, mapping removed, thrust/roll/boost/stick zeroed; the ship coasts and the sim clock and warp keep running), the OS cursor is shown and free, and a dedicated map camera takes over the view. J again or Esc closes it and restores the ship's input, hidden cursor and chase camera. Destination picking and the jump itself are not built yet | Built (2b-2, camera only) | `Ship/SOLShipPawn.cpp`, `Map/SOLMapModeSubsystem.cpp` |
| Map: right drag | Orbit the map camera around its focus, "grab the scene" (the system follows the cursor): drag right turns the system right (camera yaw -0.005 rad/px), drag up tilts it toward edge-on (camera pitch -0.005 rad/px), pitch clamped to ±1.5 rad | Built (2b-2) | `Ship/SOLShipPawn.cpp`, `Map/SOLMapModeSubsystem.cpp`, `SOLConstants.h` |
| Map: middle drag, or Shift + right drag | Pan the focus across the ecliptic plane (never up/down), grab the scene: 0.0015 x camera distance per pixel. The focus starts on the ship when the map opens and then stays where the player leaves it (it does not follow the coasting ship) | Built (2b-2) | `Ship/SOLShipPawn.cpp`, `Map/SOLMapModeSubsystem.cpp`, `SOLConstants.h` |
| Map: left click / drag | Unbound for now (does nothing); reserved for the destination-picking gesture | Reserved (2d) | `Ship/SOLShipPawn.cpp` |
| Map: mouse wheel | Zoom: wheel forward = in, one notch = distance x/÷ 1.2, clamped 1,000 km to 6e13 m | Built (2b-2) | `Map/SOLMapModeSubsystem.cpp`, `Map/SOLMapCamera.h` |
| L | Toggle surface-lock (level to nearest body) | Designed (SDD 1) | — |

Weapons and target-drop bindings are decided per part and will be added here.

### 1.1 Debug and verification switches

The ship pawn (`Source/SOLTest/Ship/SOLShipPawn.cpp`) is the default pawn. `-SOLSpectator` swaps in the old free-fly verification camera (`Source/SOLTest/Game/SOLSpectatorPawn.cpp`), which rides the ship as a free-look camera (mouse look, wheel and warp keys; it does not move the ship). `-SOLStart=<Body>` and `-SOLAltitudeKm=<km>` choose where the ship spawns and `-SOLLookAt=<Body>` aims the spectator. `-SOLSmokeFlight` flies a scripted physics sequence (`Ship/SOLShipSmokeFlight.cpp`); `-SOLSmokeInput` drives the real input pipeline with injected key, mouse and wheel events (`Ship/SOLShipSmokeInput.cpp`); both log a PASS/FAIL per check and quit (`-SOLSmokeInput` after a screenshot). `-SOLSmokeHud` drives the HUD keys (T, O, P) and the F3 panel (panel keys through Slate), checks that W does nothing while the panel is open, and ends with two screenshots (HUD over Earth, orbit ellipses from 3 AU above the Sun) (`UI/SOLHudSmoke.cpp`). `-SOLSmokeMap` drives the jump map with injected J, W, mouse-button, mouse-delta, wheel and Esc events (open, ship input suspended, left drag does nothing, right-drag orbit, zoom, middle-drag and Shift+right-drag pan, close with J, ship input back, reopen, zoom in near Earth, close with Esc), logs a PASS/FAIL per check with each body's apparent pixel radius from the map camera, and ends with screenshots (plain map, map with orbit ellipses, close-up of Earth, ship view) (`Map/SOLMapSmoke.cpp`). `-SOLSmokeShot=<s>` takes a screenshot after s seconds and quits.

| Input (spectator only) | Action | Value |
|---|---|---|
| Mouse | Look (yaw/pitch) | Engine default look scale |
| Mouse wheel | Step the spectator's own speed cap on a log scale | 3 steps per decade, 1 m/s to 0.5c; starts at 10 km/s |
| [ / ] / Backspace | Time-warp down / up / reset | See section 3 |

## 2. Flight

| Mechanic | Value / rule | Status |
|---|---|---|
| Flight model | Flight-assist by default (target velocity vs a reference frame, drift and gravity cancelled by thrusters); Tab toggles pure Newtonian. The ship is a Mass entity stepped on real time in substeps of at most 1/30 s (at most 16 per frame); a frame longer than 0.5 s (a hitch) is clamped to 0.5 s for both the sim clock and the ship, so the whole universe slows uniformly | Built (1b), `Flight/SOLFlight.cpp`, `Ship/SOLShipFlightProcessor.cpp` |
| Speed cap | Relative to the reference frame; starts at 1000 m/s; wheel steps x10^0.1, clamped 1 m/s to 0.5c (149,896 km/s); the F3 panel sets it digit by digit or from a body's orbital speed | Built (1b wheel, 1c F3), `Ship/SOLShipPawn.cpp`, `UI/SOLSpeedPanelWidget.cpp`, `SOLConstants.h` |
| Assist acceleration | Time constant 1.5 s (0.5 s with boost); boost doubles the target speed | Built (1b), `Flight/SOLFlight.h` |
| Newtonian acceleration | 50 m/s² forward/back, 30 m/s² strafe and vertical; boost 3x | Built (1b), `Flight/SOLFlight.h` |
| Rotation | Rate command, 90°/s pitch/yaw, 120°/s roll, 0.15 s response, quaternion (no gimbal lock), same in both modes | Built (1b), `Flight/SOLFlight.h` |
| Reference frame | Assist frame = anchor (nearest) body's velocity; M locks it to the selected target (else the anchor) until M is pressed again. Planets are targetable; future enemies/targets plug in via `ISOLTargetable` | Built (1b), `Targeting/SOLTargetingSubsystem.cpp` |
| Gravity | Sum over the Sun and all planets (applies in Newtonian mode; assist cancels it) | Built (1b), `Ship/SOLShipFlightProcessor.cpp` |
| Spawn | 10,000 km above Earth on its Sun-facing side, Earth's velocity plus circular-orbit velocity (4.93 km/s) | Built (1b), `Ship/SOLShipSubsystem.cpp` |
| Collision | Analytic swept sphere against every body incl. the Sun, per substep in the body's frame (no tunnelling, even at 0.5c or under warp): velocity into the surface zeroed, ship rests at its 10 m radius above the surface and can slide; no damage yet | Built (1b), `Ship/SOLShipFlightProcessor.cpp` |
| Ship | Placeholder ~21 m ship (16 m span) from engine primitives, light grey-blue hull, orange engine glow; 10 m collision radius | Built (1b), `Ship/SOLShipPawn.cpp` |
| Camera | Spring-arm chase cam 60 m behind, 15 m above; rotation lag speed 8 (exponential slerp applied after the ship's orientation update), no position lag; shadowless fill light on the camera at 10% of sunlight, tilted 45° down, so the ship's shadowed side stays visible; FOV 90° widening to 110° on a log scale of speed relative to the frame (100 m/s to 0.5c); Alt free-look | Built (1b), `Ship/SOLShipPawn.cpp`, `SOLConstants.h` |
| Speed readout | Relative to the active frame (locked target or anchor body), large, auto units (m/s under 1000, km/s under 0.01c, else c); frame name ([M LOCK] when locked) and absolute Sun-frame speed beneath; cap with a fill bar of speed/cap (orange above the cap); assist and boost state | Built (1c), `UI/SOLFlightHud.cpp`, `UI/SOLHudFormat.cpp` |
| Flight HUD | Canvas-drawn: center reticle; virtual-joystick circle, dead zone and stick dot; sim UTC date/time and warp (orange above 1x), anchor, nearest body and altitude (top left); selected target name, distance and relative speed (top right) with a corner bracket sized to its apparent radius, or an edge arrow when off-screen or behind; prograde (circle + ticks) and retrograde (circle + X) markers of the velocity relative to the frame (above 0.1 m/s) | Built (1c), `UI/SOLFlightHud.cpp` |
| Radar | 3D spherical scope (bottom right, tilted disc): every targetable, ship-relative, forward = up on the scope, height stalks, the selected target larger in yellow with its name, contacts behind the ship dimmer; every contact is plotted; one beyond the range has its whole ship-relative vector scaled back onto the range sphere, so it keeps its true direction (lateral offset and stalk in proportion) instead of pinning both to the maximum; inside 1% of the range positions collapse by a square root so they never overlap the center. **Range modes** (label under the scope in the shared distance format, e.g. `RADAR 10000.0 km (AUTO)`, `RADAR 1.0 km (MANUAL)`, `RADAR 1.0000 AU (MANUAL)`): AUTO (the default) = the floor, fixed and independent of where the contacts are: 10,000 km with no target selected, tightening to 1 km the moment a target is selected (back to 10,000 km when it is cleared); `-` zooms in and `=` zooms out by a factor of 10 per press and switches to MANUAL (a press that would leave the range unchanged, e.g. `-` already at the floor, is a no-op and keeps the current mode), clamped between the floor and 1 AU; Home returns to AUTO. The floor is re-evaluated every frame (also in MANUAL) | Built (1c), `UI/SOLFlightHud.cpp`, `UI/SOLRadarLayout.cpp`, `Ship/SOLShipPawn.cpp` |

## 3. Time

| Mechanic | Value / rule | Status |
|---|---|---|
| Sim clock start | Real current UTC date and time, 1x; stops while the game is paused | Built (1a), `Universe/SOLSimClockSubsystem.cpp` |
| Time-warp | Steps 1x, 10x, 100x, 1000x, 1 h/s, 1 d/s, 10 d/s, 30 d/s; ship physics always real-time, but the ship is carried with its reference frame (M-locked target, else the anchor body), so it stays put relative to that frame (in orbit or on its surface) at any warp; no auto-clamp | Built (1a), `Universe/SOLSimClock.cpp` |

## 4. Jump map (J)

| Mechanic | Value / rule | Status |
|---|---|---|
| Map camera | J opens a navigable 3D view of the live solar system from a separate camera (60° horizontal FOV). It opens centered on the ship's position at that moment, yaw 0, pitch 0.5 rad (looking down onto the ecliptic), 1e13 m (~67 AU) out, which frames Neptune's orbit. The universe is rendered from the map camera (render origin and far-body placement follow it), so bodies keep their true angular sizes; at the default distance every body is below one pixel (Sun ~0.08 px radius), so bodies are shown by the icons below. The flight HUD hides its reticle, joystick and velocity markers and shows the map's key hints | Built (2b-2), `Map/SOLMapModeSubsystem.cpp`, `Universe/SOLAnchorSubsystem.cpp`, `SOLConstants.h` |
| Body icons and labels | While the map is open, each body's apparent diameter from the map camera picks its look: at 4 px or less, only a flat disc in the body's own base color (10 px across at 720 p, scaled with the HUD) with a thin dark rim; at 32 px or more, only the real mesh; in between, the disc's opacity falls linearly from 1 to 0 and the disc grows to cover the mesh, so the mesh fades in under it without a pop. Every body with a visible disc gets its name beside it (right, else left, above or below; bigger bodies claim a spot first, and a name with no free spot is hidden until you zoom in), faded with the disc. No icons or labels outside the map | Built (2c), `Map/SOLMapBodyOverlay.cpp`, `Map/SOLMapBodyLod.h`, `SOLConstants.h` |
| Body pick radius | For picking a body on the map, each body counts as at least 12 px in radius on screen (at 720 p, scaled with the HUD), or its real radius when that is larger | Built (2c, used by 2d), `Map/SOLMapPickRadius.cpp`, `SOLConstants.h` |
| Destination picking | Two-stage drag gesture (SDD 3 section 3) | Designed |
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
| Render origin | Snaps to the render viewpoint (the observer, or the map camera while the jump map is open) on an anchor change, after 10 km of drift, or when the map closes; bodies beyond 1,000,000 km of the viewpoint are drawn closer with their angular size preserved | Built (1a; viewpoint 2b-2), `Universe/SOLAnchorSubsystem.cpp`, `SOLConstants.h` |

## 7. Combat

| Mechanic | Value / rule | Status |
|---|---|---|
| Weapons | Projectile bolts (not hitscan), ship velocity inherited, limited lifetime | Designed |
| Targets | Player-dropped; health and shields; destruction VFX and sound | Designed |
| Numbers (damage, rate of fire, range, target health) | To be set in Part 6 | Open |
