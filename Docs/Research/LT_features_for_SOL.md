<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->
# Limit Theory features for SOL: flight, map orders, HUD look, object interaction, full catalogue

Research input, not an SDD. Written 2026-10-21 against SOLTest HEAD `0d065cf`. Everything picked from here still goes through `grill-me`, an SDD and a plan (`CLAUDE.md`).

**Sources read.** New engine port `D:\Development\LimitTheory` (Lua `script/`, `res/shader/`, `libphx/`, `Docs/CONTROLS.md`, `Docs/Game/*.md`). Original `D:\Development\LimitTheory-Original` (LTSL `resource/script/**/*.lts`, shaders `resource/shader/**/*.jsl`, C++ `src/liblt/`, `Docs/Game/*.md`, `Docs/SDDs`, `Docs/Plans`; the latter two only cover the port/build setup). SOLTest: `CLAUDE.md`, `Docs/ARCHITECTURE.md`, `Docs/GAME_MECHANICS.md`, `Docs/ToDo/*.md`, and all of `Docs/Research/` ([`SOLTest_vs_LimitTheory.md`](SOLTest_vs_LimitTheory.md) = **SvL**, [`SimResearch.md`](SimResearch.md) = **SR** with its F-001..F-370 catalogue and `SimResearchImages/`, [`STAR_FIELD_CELESTIALVAULT_AND_ATHYG.md`](STAR_FIELD_CELESTIALVAULT_AND_ATHYG.md), [`UE_PERFORMANCE_GUIDELINES.md`](UE_PERFORMANCE_GUIDELINES.md)). Paths below are relative to each tree's root: **N:** = new LT, **O:** = original LT, **S:** = SOLTest `Source/SOLTest/`.

Nothing was built or run; runtime behaviour inferred from code is marked *(inferred)*. Effort scale and assumptions are those of SvL 1.3 (S up to ~1 person-week, M 1-3, L 4-12, XL more).

---

## A. Ship motion in the NEW Limit Theory

### A.1 How the ship is flown

| Aspect | What the code does | Source |
|---|---|---|
| Input layer | Bindings are combinators (`Key`, `Pair`, `Or`, `And`, `MouseX`, `GamepadAxis`, `:delta()`, `:invert()`, `:setDeadzone()`), separating device from intent | N: `script/WIP/Control.lua`, `script/Game/Controls/ShipBindings.lua` |
| Thrust | W/S forward/back, A/D strafe; **no vertical thrust bound** (HUD passes `up = 0`); Q/E roll; LShift boost | `ShipBindings.lua`, `Game/Controls/HUD.lua` `HUD:controlThrust` |
| Steering | **Hold Space** + mouse. Yaw/pitch = `And(MouseX, Key Space)`. The value is the **cursor's absolute offset from the viewport centre** in NDC, `clamp(2(m - c.x)/c.sx - 1, -1, 1)`, not a delta; linear, no dead zone, no response curve. Without Space the mouse does not steer: the visible cursor then aims the turrets (ray from the cursor to `socketRangeMin`) | `WIP/Control.lua` `Control.MouseX:getRaw`, `HUD:controlTurrets` |
| Gamepad | Left stick thrust/strafe, right stick yaw/pitch (pitch inverted), LB/RB roll, LT boost (analog), RT fire, X lock, B clear, Y dock | `ShipBindings.lua` |
| Thrust model | `ThrustController:update` turns the -1..1 demands into Bullet forces and local torques: forward 8e6, right 8e6, up 2e6 (backward cap 2e6 declared but **not applied**, a noted bug), pitch/yaw torque 1e7, roll 2e7. Boost multiplies forward thrust by `1 + 2·boost` and drains the capacitor at `Config.game.boostCost` = 10/s (capacitor 100, recharge 10/s); no energy = no boost | `Game/Components/ThrustController.lua`, `Game/Entities/Ship.lua` (`addCapacitor(100, 10)`), `Config.App.lua` |
| Mass, drag | Mass `50·radius³`. `setDrag(0.75, 4.0)` maps to Bullet `setDamping(linear, angular)`. Bullet scales velocity by `(1 - damping)^dt` each step, so linear 0.75 = speed decays at `ln 4 ≈ 1.39 /s` (half-life 0.5 s): the ship coasts to rest in ~2-3 s and has a terminal speed of `(F/m)/1.39`. Stock Bullet clamps damping to [0, 1], so angular 4.0 behaves as 1.0: angular velocity is wiped every step and the ship turns only while torque is applied *(inferred; stock Bullet semantics, not run)* | `Ship.lua`, `libphx/src/RigidBody.cpp` `RigidBody_SetDrag` |
| "Flight assist" | **None as a mode.** The heavy drag *is* the assist: the ship is an arcade "space car" that stops when you release the keys. No Newtonian toggle, no speed cap, no reference frame. `Ship:getTopSpeed()` is a constant 100 used only for AI estimates | as above |
| HUD feedback | Reticle: 3 ticks, r 24-28 px, colour (0.1, 0.5, 1.0). The aim ring at the cursor offset is disabled (`if false`) | `HUD:drawReticle` |
| Target lock | Each frame the hostile/neutral (disposition < 0.5) tracker nearest the screen centre within 128 px is highlighted; **T** locks it, **G clears** | `HUD:drawTargets`, `HUD:controlTargetLock` |
| Autopilot | No player autopilot key except **F = dock** (pushes `Actions.DockAt` when a dockable is within `dockRange` 50). Orders to owned ships come from the Command panel (section B) | `HUD:onInput`, `HUD:onUpdate` |

### A.2 Autopilot / move-to (the AI uses the same thrust controller)

Every AI behaviour is an `Action` on a per-entity stack (`Game/Action.lua`; `onUpdateActive` runs the top action, `onUpdatePassive` runs all). All movement goes through one helper, `Action:flyToward(e, targetPos, targetForward, targetUp)`:

- `course = (targetPos - pos) - velocity · kLeadTime` with `kLeadTime = 1 s` (velocity damping by look-ahead).
- `forward = normalize(course + targetForward)`.
- Linear demands: `forward/right/up = ExpMap1Signed(2 · dot(axis, forward))`, where `ExpMap1Signed(x) = sign(x)(1 - e^-|x|)` (`libphx/src/PhxMath.cpp`).
- Angular: `yawPitch = cross(e.forward, forward)`, `roll = cross(e.up, targetUp)`, demands `ExpMap1Signed(±10 · …)`.
- Boost `1 - exp(-max(0, dist/150 - 1))` when `Config.game.aiUsesBoost`.
- The code itself flags this as a "major bottleneck" to push to C.

| Action | Behaviour | Source |
|---|---|---|
| `MoveTo(target, range)` | `flyToward(target.pos)`, pops when `getMinDistance(target) <= range`. Target is an **entity**, never a point in space. `Config.debug.instantJobs` teleports instead (on in the shipped `Config.Local.lua`) | N: `script/Game/Actions/MoveTo.lua` |
| `Escort(target, offset)` | Flies to `target.toWorldScaled(offset) + target.velocity · 1 s`, aligned to the target's forward/up (formation flying) | `Actions/Escort.lua` |
| `Attack(target)` | Picks a random offset direction on the sphere (on the ship's side), radius `lerp(2R_t + R_e, socketRangeMin, u²)`, re-picked every 5-10 s or when reached; fires when the target is within ~75° of the nose, at random times (`rng:getExp()² < dt`) | `Actions/Attack.lua` |
| `DockAt` / `Undock` | Flies to `station.pos - up·(1.25 R_s + R_e)`, docks within 200 by reparenting the ship under the station | `Actions/DockAt.lua`, `Components/Dockable.lua` |
| `MineAt`, `Wait`, `Repeat`, `Think` | Mining at a yield, timers, loops, AI economic decision tick | `Actions/*.lua` |

### A.3 Cameras

- **Chase** (`script/Util/CameraChase.lua`): offset `posRel = (0, 4, -10)` scaled by `0.25 · radius · zoom`, looks at `lookAtRel = (0, 0, 1000)` in ship space. It **leans with angular velocity**: position shifted by `-4·ω_y` along right and `+4·ω_x` along up, camera up tilted by `0.2·ω_y` toward right. Wheel zoom: `radius *= exp(-0.1 · wheel)`, smoothed by `1 - exp(-8 dt)`, minimum 0.5. Transitions between cameras blend `posOffset/rotOffset` with `1 - exp(-10 dt)` (`Util/Camera.lua` `lerp`, `lerpFrom`). Otherwise the camera is rigid behind the ship.
- **Orbit** (`Util/CameraOrbit.lua`): yaw/pitch/radius/centre with exponential smoothing `rigidity = 8`, pitch clamped to ±0.49π, optional target-relative mode. Used by the Command, Debug and Dock panels.
- **Original LT for contrast** (`O: src/liblt/Game/Widget/HUD.cpp`, `resource/script/Widget/Handling.lts`): also Space + cursor offset, but with dead zone 0.2 and power 2 (`offset = n · saturate(2(|o| - 0.2)/0.8)²`), sensitivity 1.5, look/up targets smoothed (rigidity 6), the ship slews toward a camera "look target" (camera-led steering, ship follows), cockpit/chase viewpoints (middle mouse), wheel distance 1.5-8 radii, speed-scaled camera shake, Ctrl+W cruise, and **"autopilot overrides manual control"** whenever the ship has a current task. Drag is `force -= v·m·kLinearDrag` (`Component/Motion.cpp`), again arcade, not Newtonian.

### A.4 What SOL has today (for the mapping)

`S: Flight/SOLFlight.cpp`, `Flight/SOLFlight.h` (`FSOLFlightParams`), `Ship/SOLShipPawn.cpp`, `Ship/SOLShipFlightProcessor.cpp`:

- Mouse = always-on virtual joystick (hidden cursor, radius 0.35 of the short side, dead zone 0.05, exponent 1.5, 90°/s), Middle mouse recenters. Space/Ctrl = up/down thrust. Tab toggles assist/Newtonian.
- **Assist is not thrust-limited.** `SOLFlight.cpp` relaxes velocity exponentially toward `reference + thrust·cap` with `AssistTimeConstantS = 1.5 s`. Implied acceleration is `Δv / 1.5 s`, which is about 1e8 m/s² at the 0.5c cap. Gravity and drift are cancelled for free.
- Newtonian mode: 50 m/s² main, 30 m/s² strafe, ×3 boost, real gravity.
- Rotation: rate command with 0.15 s response, the same in both modes.
- NPCs (Part 10) use a separate point-mass steering (`S: Npc/SOLNpcAiMath.cpp`) with their own limits (fighter 80 m/s², 1.5 rad/s).

### A.5 Proposed full-Newtonian SOL model with three modes

**One physics core for every ship (player and NPC).** State = position, velocity, orientation and angular velocity in `FVector3d`. Forces = per-axis thrust bounded by the ship class (the current 50/30 m/s² and ×3 boost become `FSOLFlightParams` thrust limits; later mass-dependent once cargo exists, F-020) + gravity (already summed in the flight processor). Rotation stays rate-command (it reads well and is what LT's damped torque effectively produces), but with a bounded angular acceleration so turn rates are physical. All three modes only produce a **thrust + rotation demand**. Nothing writes velocity directly, so player and NPC obey the same envelope (LT's player/NPC symmetry, F-047).

| Mode | Player input means | Controller | LT counterpart |
|---|---|---|---|
| **1. Full manual** | WASD/Space/Ctrl = thrust per axis; mouse = rotation rate | Pass-through (today's Newtonian mode) | none (LT has no Newtonian mode) |
| **2. Assisted** | WASD = desired velocity relative to the reference frame (today's assist semantics) | **Thrust-limited velocity controller**: required `a = (v_target - v)/τ - g`, clamped per ship axis to the thrust envelope (gravity compensation consumes budget, as Elite does) | LT's drag "stops when you let go" feel, done physically |
| **3. Full auto** | Orders: Move to (point or body + range), Approach / Match velocity, Orbit, Escort/Follow (offset, lead time), Dock, Attack | Autopilot computing a thrust demand. Use a time-optimal **accelerate, flip, brake** (brachistochrone) leg with a relative-frame intercept, not LT's 1 s look-ahead heuristic, which only works with heavy drag. Hands back to mode 2 on arrival or on any manual input (LT original: autopilot owns the ship while a task exists) | `Action:flyToward`, `MoveTo`, `Escort`, `Attack`, `DockAt` |

**Steering gate (LT's Space idea).** Offer "hold or toggle a steer key": while active, the existing virtual stick drives rotation (keep SOL's dead zone and exponent, which are better than LT's linear cursor offset). While inactive, the **OS cursor is free** to click HUD icons, open the interaction menu (section D) and aim. This is what makes G-icons and the radial menu usable in flight. It conflicts with SOL's Space = thrust up: see the questions.

**Scale consequence (must be decided).** With thrust-limited assist, the 1 m/s to 0.5c cap is no longer reachable by thrust: 50 m/s² needs ~35 days to reach 0.5c and ~1.4 days to cross 1 AU. Long travel therefore needs the jump map (built), a separate **drive/supercruise regime** (`Docs/ToDo/supercruise-travel.md`), or autopilot legs that run under time warp. NPC autopilot legs should reuse Part 10's analytic rails for the far tiers (SvL 7.7-7.8).

**Camera additions worth taking from LT.** Angular-velocity lean (`CameraChase`), a smoothed transition blend between cameras (`Camera:lerpFrom`), and optional speed shake (original). SOL already has FOV widening with speed and rotation lag.

---

## B. Map and point-A-to-B orders (Homeworld style)

**What LT actually has in code.**

| Item | New LT | Original LT |
|---|---|---|
| Strategic/command view | `N: script/Game/Controls/CommandControl.lua` (623 lines). Orbit camera (RMB drag yaw/pitch 0.005 rad/px, wheel `exp(-0.1)`, **WASD pans on the horizontal plane** at `500·log(radius)`, smoothing 16/s); box select after 8 px, rect padded 10 px; Replace / Add (Shift) / Toggle (Ctrl) / Remove (Ctrl+Shift); 4 groups (Friendlies + 3), E store / Q recall; **F** focus on selection (fits the camera with `radius / tan(fov/2)`), **G** focus-lock; off-screen units as edge dots; unit marker colour from disposition | `O: resource/script/Widget/Map.lts`: **flat top-down** x/z projection, WASD pan, wheel zoom, double-click to focus, `Glyph_Grid` background, objects drawn with the same `HUD/WorldObject` widgets (so the radial menu works on the map). `App/map.lts`, `App/universe.lts`: region/system browsers |
| How an order is issued | Select units, **right-click a unit**, context window (`ContextEntries`): **Move To** opens a details window with a Range slider 0-1000 (default 100) and Apply; **Attack** is immediate. Each selected unit gets `pushAction(...)` | No player order UI. Orders exist only as AI `Task_*` (`O: src/liblt/Game/Task/Goto.cpp`, `Dock.cpp`, `Patrol.cpp`, ...) |
| Point in space / plane / height | **Not supported.** Targets are always entities | Not supported |
| Formation, stance | Details window lists Formation (Standard, Wedge) and Stance (Passive, Aggressive, Defensive) buttons, **not wired** | none |
| Debug system map | `N: script/Game/SystemMap.lua`: top-down x/z dot plot, P/O zoom | — |
| Hologram look | `N: DrawEx.Hologram` + `res/shader/fragment/ui/hologram.glsl` (3D mesh in a widget, see C.5). The tilted holographic command view and the action menu (Ship >, Attack >, Follow > with a Lead Time slider, Go To, EXECUTE) exist **only in the 2013 videos** (SR F-226, image `SimResearchImages/row-01-command-interface-action-menu.png`; SvL 8.9) | `O: shader/fragment/hologram.jsl` |

**Conclusion.** Neither LT tree implements Homeworld-style move-to-point with a plane and height. **SOL's jump map already does**: two-stage gesture, left-drag sets the XY offset in the ecliptic plane through a live reference with a cyan disc, Shift + mouse previews height with a guide line, Shift-click locks it (`S: Map/SOLMapModeSubsystem.cpp`, `Map/SOLMapPickOverlay.cpp`, `Map/SOLMapScreenPick.cpp`; GAME_MECHANICS section 4). Proposal:

1. **Order mode on the jump map.** The same pick produces a `FSOLMoveOrder {reference (body/ship/station), offset, height, arrival range, arrival mode (stop / match reference / orbit)}`. Enter = Jump (today), a new key or button = "Fly there" (mode 3 autopilot). Because the destination is relative to a live reference, it stays valid while bodies move (better than LT, which only targets entities).
2. **Selection and context orders** borrowed from `CommandControl`: click/box select own ships (just the player ship until fleets exist), right-click an object for its action menu (section D), groups, F focus / lock focus.
3. **Plane choice.** Default plane = ecliptic through the reference (today). Add the reference body's equatorial/orbital plane and "plane through the selected ship" as options; draw the plane as a grid using LT's two-scale grid glyph (C.4).
4. **Hologram presentation** (optional, cosmetic): tilt plus hologram material on body/ship proxies, grid, additive scanlines (SvL 8.9 rates the material S).

---

## C. Radar, HUD and UI visual effects (recipes for UE)

LT's look comes from **analytic signed-distance glow per primitive**, an **offscreen UI layer composited with grading, grain, scanlines and jitter**, additive blending, and colours doubled (`2 × colour`) so cores clip toward white. Bloom is **not** what makes the UI glow; the halo is in each primitive's falloff. That matters for UE, because Slate/UMG draws after post-processing and engine bloom never touches the UI.

### C.1 Glow-SDF primitives (every line, ring, arc, wedge, point, box)

Each primitive is a screen quad padded by 32-128 px (`N: script/UI/DrawEx.lua` `padLine = 64`, `padRing = 128`; `O: src/liblt/UI/Glyph/Line.cpp` `kExpansion = 32`), drawn additively. The fragment computes the pixel distance `d` to the shape and:

| Shader | Falloff (alpha multiplier) | File |
|---|---|---|
| Line (new) | `0.8·e^(-2·max(0, d-0.5)) + 0.2·e^(-(0.2d)^0.75)`, times `e^(-2(1-t))` along the line (fades toward p2) | N: `res/shader/fragment/ui/line.glsl` |
| Line (orig) | `0.7·e^(-2·max(0, d-0.5)) + 0.2·e^(-sqrt(0.7·max(0, d-0.5)))` | O: `resource/shader/fragment/ui/line.jsl` |
| Ring | `0.5·e^(-max(0, d-0.5)) + 0.4·e^(-(0.2d)^0.75)`, `d = |r - radius|`, colour ×2 | N: `ui/ring.glsl` |
| Wedge (bracket corners) | ring-sector distance with bevel 2, `0.5·e^(-max(0, d-0.5)) + 0.4·e^(-(0.3d)^0.75)` | N: `ui/wedge.glsl` |
| Arc (orig, with phase/extent) | `mix(e^(-d), e^(-1.5·max(0, |d|-0.5)), 0.75) + 0.2·e^(-(0.2d)^0.75)`, round caps 0.5 | O: `ui/arc.jsl` |
| Point/circle | `2·e^(-(64r)²) + e^(-(32r)^0.75)` (new); orig `2·e^(-4(48d)^0.75) + 0.2·e^(-12d)` | N: `ui/circle.glsl`, O: `ui/circle.jsl` |
| Box | rounded box radius 8, `0.6·e^(-0.5d) + 0.4·e^(-(0.3d)^0.75)` | N: `ui/box.glsl` |
| Hex | hexagon distance, `0.5·e^(-max(0, d-0.5)) + 0.3·e^(-(0.2d)^0.75)` | N: `ui/hex.glsl` |

The first term is a ~1 px anti-aliased core; the second, with a power below 1, is a long soft halo. The orig engine **batches every glyph into one vertex buffer per layer**, with shape parameters packed in vertex attributes (`Line.cpp` `LineVertex {p, color, uv, p1dir, originsize}`).

**UE mapping.** A custom Slate element using `FSlateDrawElement::MakeCustomVerts` with **one** UI-domain material that reads the shape type and parameters from vertex UVs/colour. That gives one draw for the whole HUD (Canvas `DrawLine` is aliased and costs a draw per line; per-primitive material instances would also cost a draw each). An alternative for UMG widgets is a single UI material with SDF math on a Retainer. Port the falloffs above verbatim; expose core/halo weights as constants in `SOLConstants.h`. This also lets the radar and orbit-line overlays share one look.

### C.2 UI composite: grading, grain, scanlines, random jitter

Original `Compositor_Basic(lines 0.05, noise 0.10, gradeBlue (0.40, 0.50, 0.75))`, applied to the whole HUD layer (`O: resource/script/Widget/HUD.lts` `Create`; C++ `src/liblt/UI/Compositor/Basic.cpp`; shader `resource/shader/fragment/ui/basic.jsl`), per pixel:

1. **Jitter / glitch** (horizontal row tearing): `offset.x = 0.002 · e^(-48·snoise(uv.y + age)) · sign(2·noise(uv.y + 5·age) - 1)`. Rare rows, chosen by a time-scrolling noise, shift by up to 0.2% of the width; the layer is sampled at `uv + offset`. `age` = accumulated time.
2. **Film grain**: `c *= 1 - noiseMag · log(1 - noise(128·uv, seed)²)`, with a new random `seed` every frame.
3. **Scanlines**: `c *= 1 + linesMag · e^(-2(0.5 + 0.5·sin(2·uv.y·height)))`.
4. **Tone curve**: `c = 1 - e^(-2·c^1.25)`; blue channel through a Bézier `(0, 0.40, 0.50, 0.75, 1)`.
5. **Dither** `-noise3/128`, then un-premultiply.

The new LT is milder (`N: res/shader/fragment/ui/composite.glsl`): grain `×(1 - 0.02·log(1 - noise))`, layer alpha `(1.5a)^0.75`, blue `pow 0.95`. Scene dither: `O: shader/fragment/post/dither.jsl` (±1/256).

**UE mapping.** Put the HUD in a Retainer Box (render target) with an effect material doing steps 1-5, using `Time` for `age` and a per-frame random scalar parameter. Keep it subtle and behind a setting: SR records the LT glitch as "too strong" (F-263) and requires effects never to hide tactical information. *Cost check needed*: a full-screen Retainer redraw every frame.

### C.3 Panels and the radial menu

- `ui/panel.jsl` (orig) / `panel.glsl` (new): rounded box `dbox` with bevel 8 + outer shadow `0.75·saturate(e^(-(0.2d)^0.75) - k)`, vertical gradient `mix(e^(-uv.y), 0.5, 0.5)`, top-centre sheen `0.17·grey·e^(-3·|uv - (0.5, 0)|)`, edge darkening `mix(c, 0.005, 1 - e^(-2d))`. `panelglow.glsl` = halo only (focus glow).
- `ui/radialpanel.jsl`: annulus sector between `r1 = 0.5R` and `r2 = R`, bevel, phase/extent, blue sheen `0.25·(0.4, 0.7, 1.0)`. Used by `RadialList` (D.2).

### C.4 Grid (map and hologram background)

`ui/grid.jsl` / `grid.glsl`: two scales (16 px and 64 px) of lines with `dfield(d) = e^(-2d²) + 0.2·e^(-(0.2d)^0.75)` plus bright dots at intersections, fading upward `2·e^(-2·uv.y)`. In UE it is a material on the move-order plane, in world space instead of screen space.

### C.5 Hologram

`N: res/shader/fragment/ui/hologram.glsl`: `alpha = uv.x · (1 - |N·V|) · (1 - 0.1·log(1 - noise(fragXY, time))) · (1 + 0.2·sin(π·fragY))`, colour ×2, additive. That is Fresnel rim + per-pixel time flicker + 1 px scanlines. As a UE unlit additive material (Fresnel, `Time` noise, `ScreenPosition` scanlines) it is S (SvL 8.9).

### C.6 Radar and minimap

**Neither LT tree has a 3D radar with plane lines.** The closest is the original **minimap** (`O: resource/script/Widget/HUD/Minimap.lts`):

- Objects are projected onto the camera's right/look plane (top-down relative to view).
- Distance is compressed `dir · (1 - e^(-|d|/5000))`.
- 8 concentric rings pulse as a sonar wave `alpha = 0.2·|sin(t - 2π·r)|^8`.
- Faint crosshair at 0.1 alpha; targeted contacts in the secondary colour; click opens the full map.

The **mini-scanner** (`HUD/Miniscanner.lts`, full `Widget/Scanner.lts`) draws a mirrored spectrum polyline with soft bars, animated by `0.25·(0.5 + 0.5·sin(3t + 32i/n))·(1 - t)`. Its output is blended toward a new sample once per second, and its audio loop's volume/pitch follow amplitude/frequency.

SOL already has a **3D radar** (`S: UI/SOLFlightHud.cpp` radar block, `UI/SOLRadarLayout.cpp`): tilted disc, height stalks, range modes, beyond-range contacts scaled to the range sphere. It is drawn with aliased Canvas lines in one colour. Proposed upgrade (Elite-style, LT-styled):

| Element | Proposal |
|---|---|
| Plane | Ship's horizontal plane (or the reference body's plane, a setting) as an ellipse with 2-3 range rings and fore/aft/side ticks, glow-SDF lines (C.1), LT sonar pulse on the rings |
| Stalks | From the blip's foot on the plane to the blip. **Above the plane: primary colour (0.2, 0.7, 1.0), solid stalk, filled blip. Below: secondary/amber (1.0, 0.3, 0.1) or dimmer, dashed stalk, hollow blip.** The foot is a small tick so height reads at a glance |
| Blip shape/colour | By kind (body, moon, asteroid, ship, station, target) and disposition (LT `Dispositions.GetColor` Bézier from hostile red to friendly green, `N: script/Game/Components/Dispositions.lua`; orig `Colors.lts` Friendly (0.25, 1, 0.25), Hostile (1, 0.1, 0.01)) |
| Selected | Larger, secondary colour, name label (exists), plus a lead/intercept marker in the main view (LT `Math.Impact` cross, `HUD:drawLock`; SR F-064) |

### C.7 Reticle and trackers (taken from the original)

`O: resource/script/Widget/Reticle/Default.lts`:

- 2- and 3-segment arcs at r 24/36.
- Left arc = speed as a fraction of top speed with a m/s readout.
- Right arc = hull integrity coloured `mix(Bad, Good, h²)` with 0-100% ticks.
- Arcs at r 112 for roll rate.
- Off-screen target direction ticks at r 164 with the target's icon at r 132.
- Hostile impact marker.

New LT trackers: corner **wedges** 4 px (all), 8 px (highlighted), 12 px (locked); a direction arrow at r 96 that fades in as the target leaves the centre (`N: HUD.lua` `drawTargets`, `drawLock`). SOL has a bracket plus edge arrow (GAME_MECHANICS 2), so these are refinements.

### C.8 Colours and fonts

Orig `Colors.lts`: Primary (0.2, 0.7, 1.0), Secondary (1.0, 0.3, 0.1), Good/Friendly (0.25, 1.0, 0.25), Bad (1.0, 0.1, 0.1), Hostile (1.0, 0.1, 0.01). New `Config.App.lua` `Config.ui.color` (accent (1, 0, 0.3), selection (1, 0.5, 0.1), control (0.2, 0.6, 1.0, 0.3), ...). Fonts: Share, Exo2Bold, NovaMono (N: `res/font`). Text is drawn additively for HUD prompts (`DrawEx.TextAdditive`).

---

## D. G-key icons, target selection and the per-object interaction menu (ORIGINAL LT)

### D.1 G-key world icons

`O: resource/script/Widget/HUD/WorldObjects.lts`, `HUD/WorldObject.lts`:

- **G toggles** a world-icon layer (`visible`, default **off**). F4 hides the whole HUD.
- **Eligible objects**: every interior object of the current system that has a component Interior, Navigable, Pilotable or Resources, or is Custom; for planets also their children (colonies). Shown only if `Camera_CanSee(object, 1000 × radius)`.
- **Placement**: 32 px widget at the projected centre, **clamped to the screen with a 4 px margin** (edge-pinned), hidden when behind the camera; `alpha = 1 - distance / (1000 × radius)`.
- **Icon**: `object.GetIcon` (vector icons built from glyphs: `O: resource/script/Icons.lts`, 405 lines, and C++ `src/liblt/Game/Icons.cpp`: Dock, Gear, Task_Mine, Task_Produce, Task_Research, ...). Colour Primary ×2; own objects Friendly; "Pirate" Hostile; hover or targeted = Secondary at full alpha. Tooltip = name.
- **Targeted**: an extra arc ring at 0.8 × size and the distance in km below.
- **Target selection**: hover + **T** toggles the object in the ship's **multi-target list** (`AddTarget`/`RemoveTarget`, sounds `ui/targetacquired.ogg` / `targetlost.ogg`). Backspace clears all targets (`O: src/liblt/Game/Widget/HUD.cpp`). Weapons aim at the nearest target with a lead solution.

### D.2 Interaction menu: how it is built

- `Component/ContextMenu.lts`: **right mouse press** over the icon spawns the menu at the cursor; releasing the right button closes it.
- `RadialList.lts`, radius 96: options are spaced equally around a ring starting at the top. While the cursor is more than 0.5R from the centre, the option nearest by angle is focused. An option fires on **right-button release** (`Components:ClickableRightRelease`), so one press-drag-release gesture picks it. Panels use `DrawPanelRadial` (C.3); sounds `ui/objectmenuopen.wav` / `objectmenuclose.wav`.
- Options come from `WorldObjectMenu.CreateChildren`, **conditional on the object's components**:

| Option | Shown when | What it does (in code) |
|---|---|---|
| Communications | object has an owner | message handled with an empty body (no-op) |
| Owner | object has an owner | opens the owner's info window, **commented out** (no-op) |
| Trade | object has a Market component | opens `Widget/Object/Market` |
| [DEV] Jump To | always | teleports the ship to `1.5R + 1000` in front of the object |
| [DEV] Fireworks! | always | `Object/Firework:ShootFrom` |
| Information | always | opens `ObjectInfo`, **commented out** (no-op) |
| Target | always | toggles it in the target list (as T) |

**So the original's per-object menu has no move/attack/dock/mine.** Those exist as AI tasks (`O: src/liblt/Game/Task/`: Buy, Custom, Destroy, Dock, Drill, Goto, LOD, Manage, Mine, Mint, Patrol, Pirate, Play, Produce, Research, Sell, Spawn, Transport, Wait). In the new LT they appear as the Command panel's **Move To / Attack** plus **F = dock** (sections A, B). The richer "Swiss army knife" menu was shown only in videos (SR F-226, F-260, F-276).

**Object info window** (`O: Widget/ObjectInfo.lts`, `Widget/Object/*.lts`): tab ribbon Overview, Assets, Cargo, Crew, Account, Market, Projects, Messages, Notes, Hardpoints, Database, Tasks (Database, Hardpoints and Tasks are 4-line stubs). Overview shows location, cargo, health, stats, current activity, faction and scanner signature, and has a 3D preview. Assets lists owned objects with health % and their current task.

### D.3 Proposed SOL version

- **Icons layer** on a toggle key (LT used G; **SOL's G currently drops a target**). It shows every targetable within a view distance scaled by radius: bodies, moons, belt asteroids and ring rocks once minor-body targeting exists (`Docs/ToDo/minor-body-targeting.md`), NPC ships, dropped targets, later stations. Edge-pinned and distance-faded, drawn with the C.1 primitives. Reuses the jump map's label placement and pick radius (`S: Map/SOLMapBodyOverlay.cpp`, `Map/SOLMapPickRadius.cpp`) and `ISOLTargetable`. With a free cursor (A.5 steer gate), click = select target (today T/R/F/X); hover shows a tooltip.
- **Interaction menu**: right-press radial (LT) or list (LT video style); options built per object type from a capability table (built = available today):

| Action | Body / moon | Asteroid / ring rock | NPC ship | Dropped target | Station (future) |
|---|---|---|---|---|---|
| Target / untarget | built | after ToDo | built (NPCs targetable? verify) | not yet targetable | yes |
| Lock frame / match velocity (M) | built | yes | yes | yes | yes |
| Info window | new | new | new | new | new |
| Fly to / approach (mode 3) | new | new | new | new | new |
| Orbit | new | — | — | — | — |
| Surface-lock / land | built (L) / ToDo | — | — | — | — |
| Jump here (map) | built | yes | — | — | yes |
| Follow / escort | — | — | own/friendly | — | — |
| Attack | — | — | hostile | yes (combat demo) | — |
| Dock / trade / comms | — | — | — | — | after stations, economy |
| Mine / scan | — | after mining | scan | — | — |

---

## E. Complete catalogue of LT features and systems (with prior evaluations merged)

Columns:

- **Where** = source location (N new, O original; "video" = only in SR's video/post evidence).
- **Prior** = SR F-IDs and SvL section, or a SOL doc.
- **SOL** = Built / Partial / ToDo (`Docs/ToDo`) / Planned (roadmap) / Absent / N/A (out of scope by design).
- **Effort** for SOL per SvL 1.3.
- **Fit** = H (high), M, L with a reason.
- **Decision** is left for the owner.

### E.1 Flight, camera, travel

| # | Feature | Where | Prior | SOL | Effort | Fit | Decision |
|---|---|---|---|---|---|---|---|
| 1 | Thrust-controller flight (normalised demands to forces/torques) | N `Components/ThrustController.lua` | F-001..003 | Built (different model) | — | H | |
| 2 | Arcade drag "assist" (linear 0.75, angular 4) | N `Ship.lua`; O `Component/Motion.cpp` | SvL 3 | N/A (replace by thrust-limited assist, A.5) | — | L | |
| 3 | Thrust-limited Newtonian core shared by player and NPCs | proposal (A.5) | F-047 | Partial (Newtonian mode; assist unlimited; NPC separate) | M-L | H | |
| 4 | Three flight modes (manual / assisted / auto) | proposal (A.5) | — | Partial (2 modes) | M | H | |
| 5 | Hold/toggle steer key + free cursor | N `ShipBindings.lua`; O `Game/Widget/HUD.cpp` | F-005 | Absent (always-on stick) | S-M | H | |
| 6 | Cursor-offset virtual joystick with dead zone and power curve | O `HUD.cpp` (0.2, 2) | F-005 | Built (0.05, 1.5) | — | H | |
| 7 | Camera-led steering (ship slews to the camera look target) | O `HUD.cpp`, `Handling.lts` | F-005, F-006 | Absent | S-M | M | |
| 8 | Boost with capacitor drain (10/s, cap 100, recharge 10/s) | N `ThrustController.lua`, `Components/Capacitor.lua` | F-019 | Partial (boost, no energy) | S | M (needs power system) | |
| 9 | Cruise mode (Ctrl+W, thrust latched, sound) | O `HUD.cpp` | F-004 | Absent; overlaps `ToDo/supercruise-travel.md` | S-M | M | |
| 10 | Supercruise / drive regime for interplanetary distances | SOL ToDo | SvL 4.1 | ToDo | M-L | H (needed once assist is thrust-limited) | |
| 11 | Power distribution / overcharge | video | F-018, F-235 | Absent | M | M | |
| 12 | Dynamic mass from cargo | video | F-020 | Absent | S after cargo | M | |
| 13 | Chase camera with angular-velocity lean, wheel zoom, smoothed transitions | N `Util/CameraChase.lua`, `Util/Camera.lua` | F-005 | Partial (chase, lag, FOV widening) | S | H | |
| 14 | Cockpit / chase viewpoint toggle, speed camera shake | O `HUD.cpp` | F-256 | Absent | S | M | |
| 15 | Orbit camera (command/dock/debug) | N `Util/CameraOrbit.lua` | — | Built (map camera) | — | H | |
| 16 | Autopilot actions MoveTo / Escort / Attack / DockAt / Undock / Wait / Repeat | N `Game/Actions/*.lua` | F-134, F-135 | Absent for player; NPC point-mass AI built (Part 10) | M-L | H | |
| 17 | Autopilot overrides manual input while a task runs | O `HUD.cpp` | — | Absent | S | H | |
| 18 | Warp lanes / warp rails with nodes and passengers | O `Object/WarpRail.lts`, `WarpNode.lts`, `Game/Object/Rail.cpp` | F-026..028, SvL 8.4 | Absent (jump map instead; NPC rails in Part 10) | M-L | M | |
| 19 | Wormholes / jump holes between systems | O `Game/Object/Wormhole.cpp` | F-029..031, SvL 8.3 | N/A (reinterpret as charted jumps) | M | L | |
| 20 | Docking (reparent to station, dock range 50/200, prompt) | N `Actions/DockAt.lua`, `Components/Dockable.lua`, `Controls/DockControl.lua` | F-009, F-010, F-244 | Absent (needs stations) | M | H | |
| 21 | Time acceleration (hold H, dev) | N `Config.debug.timeAccelFactor` | F-036 | Built (warp ladder, ahead) | — | H | |
| 22 | Surface landing / colony landing | video | F-032, F-034 | ToDo (`surface-landing.md`), Part 9 | L | M | |
| 23 | Asteroid bouncing / collision | video | F-007, F-008 | ToDo (`asteroid-ring-collision.md`) | M | H | |
| 24 | Gamepad flight and UI navigation | N `ShipBindings.lua`, `UI/NavGroup.lua`, `WIP/Gamepads.lua` | F-248 | Absent | M-L | M | |

### E.2 Combat

| # | Feature | Where | Prior | SOL | Effort | Fit | Decision |
|---|---|---|---|---|---|---|---|
| 25 | Turrets aiming at cursor ray or locked target with lead | N `Entities/Turret.lua`, `HUD:controlTurrets` | F-055 | Absent (fixed guns) | M | H | |
| 26 | Pulse projectiles, pooled, swept ray hit | N `Components/Projectiles.lua`, `Entities/Pulse.lua` | F-049 | Built (20k SoA, ahead) | — | H | |
| 27 | Beam, missile, rail, payload weapons | O `Game/Beam.cpp`, `Object/Missile.cpp`, `Payload.cpp`, `shader/fragment/beam.jsl`, `rail.jsl` | F-050..053 | Absent | M-L | H | |
| 28 | Shields as socketed objects; shield hit/explosion shaders | O `Object/Shield.cpp`, `shield.jsl`, `shield_explosion.jsl` | F-056..060, F-314 | Partial (target shields) | M | H | |
| 29 | Health / integrity, explodable (8 explosions), regen | N `Components/Health.lua`, `Explodable.lua`; O `Component/Integrity`, `Explodable` | F-021, F-061 | Partial (targets, NPCs) | M | H | |
| 30 | Lead indicator / impact marker (`Math.Impact`) | N `HUD:drawLock`; O `Reticle/Default.lts` | F-064 | Absent for player (NPCs lead-aim) | S | H | |
| 31 | Multi-target list, nearest target engaged | O `Component/Targets.cpp`, `HUD.cpp` | F-040 | Partial (single target) | S | M | |
| 32 | Dispositions / friend-foe colours | N `Components/Dispositions.lua`; O `Component/Opinion.h` | F-066, F-067, F-173 | Partial (NPC factions in Part 10) | S | H | |
| 33 | AI attack pattern (random standoff offsets, random fire) | N `Actions/Attack.lua` | F-069, F-072 | Built differently (Part 10 Full AI) | — | M | |
| 34 | AI combat test beds (`war`, `dogfight` apps) | O `App/war.lts`, `App/dogfight.lts` | F-075 | Partial (Demo page, stress ramp) | S-M | H | |
| 35 | Fireworks (dev) | O `Object/Firework.lts` | — | Absent | S | L | |

### E.3 Economy, industry, items

| # | Feature | Where | Prior | SOL | Effort | Fit | Decision |
|---|---|---|---|---|---|---|---|
| 36 | Credits / account | N `Components/Credits.lua`; O `Component/Account.cpp` | F-077, F-080, F-081 | Absent | S after save | H | |
| 37 | Inventory / cargo by mass | N `Components/Inventory.lua`; O `Component/Cargo.cpp` | F-094 | Absent | S-M | H | |
| 38 | Market order books, EMA price history | N `Components/Market.lua`, `Trader.lua`; O `Component/Market.cpp`, `Game/Order.cpp` | F-085..093, SvL 8.6 | Absent | L | M (LT code buggy) | |
| 39 | Station storage locker | O `Component/Storage.cpp` | F-043 | Absent | S | M | |
| 40 | Economy aggregation (factories, markets, traders, yields, jobs) | N `Components/Economy.lua`, `Flows.lua`, `Game/Flow.lua` | SvL 7, 8.6 | Absent | L-XL | M | |
| 41 | Factories / production recipes | N `Components/Factory.lua`, `Game/Production.lua` | F-119..123 | Absent | M | M | |
| 42 | Mining (yield, MineAt, Mine job, transfer unit, beam shader) | N `Components/Yield.lua`, `Actions/MineAt.lua`, `Jobs/Mine.lua`; O `Object/TransferUnit.cpp`, `transferbeam.jsl`, `Task/Mine.cpp`, `Drill.cpp` | F-103..109 | Absent | M | H (real asteroids) | |
| 43 | Transport / buy / sell jobs | N `Jobs/Transport.lua`; O `Task/Transport.cpp`, `Buy.cpp`, `Sell.cpp` | F-149 | Absent | M-L | M | |
| 44 | Drone bay / prospecting drones | O `Object/DroneBay.cpp`, `Object/Drone/` | F-110..112 | Absent | M | M | |
| 45 | Scanner (spectrum output, audio loop, mini-scanner) | O `Object/Scanner.cpp`, `Widget/Scanner.lts`, `HUD/Miniscanner.lts` | F-113..118 | Absent | L | H (real signatures) | |
| 46 | Item model (commodity, ore, ship/station/weapon/turret/thruster/shield/scanner/power-gen types, worker) | O `Game/Item/*.cpp`; N `Game/Item.lua`, `ItemCount.lua`, `Material.lua` | F-083, F-180 | Absent | M | H (foundation) | |
| 47 | Sockets / hardpoints / plugging items | N `Components/Sockets.lua`, `Game/Socket*.lua`; O `Component/Sockets.cpp`, `Pluggable.cpp` | F-023..025 | Absent | L | H | |
| 48 | Power generator objects | O `Object/PowerGenerator.cpp` | F-018 | Absent | M | M | |
| 49 | Blueprints, Tech Lab research, mint, assembly chips | O `Item/Blueprint.cpp`, `AssemblyChip.cpp`, `Object/TechLab.cpp`, `Task/Research.cpp`, `Mint.cpp` | F-183..192, SvL 8.7 | Absent | L | M | |
| 50 | Production Lab / produce task | O `Object/ProductionLab.cpp`, `Task/Produce.cpp` | F-124..126 | Absent | M | M | |
| 51 | Stations (procedural, markets, docking) | N `Entities/Station.lua`, `Gen/Station.lua`; O `Object/Station.cpp`, `Item/StationType` | F-127..129 | Absent | L + art | H | |
| 52 | Colonies (types, governors, spawn ships) | O `Object/Colony.cpp`, `Item/ColonyType.cpp`, `Object/Colony.lts`, `App/colony.lts` | F-130, F-131 | Absent | M | M (fiction on real bodies) | |
| 53 | Data items (damaged/destroyed) | O `Item/DataDamaged.cpp`, `DataDestroyed.cpp` | F-097, F-098 | Absent | S | L | |

### E.4 AI, factions, missions

| # | Feature | Where | Prior | SOL | Effort | Fit | Decision |
|---|---|---|---|---|---|---|---|
| 54 | Action/task stack per entity | N `Components/Actions.lua`, `Game/Action.lua`; O `Component/Tasks.cpp` | SvL 7.2 | Absent (NPC state is processors/fragments) | M | H | |
| 55 | Think: AI player picks best of 100 random jobs | N `Actions/Think.lua`, `Game/Job.lua` | F-140..149, SvL 7 | Absent | L | M (redesign per SvL 7.5) | |
| 56 | Task_Play top-level AI, Task_Manage, Task_LOD (abstract sim) | O `Task/Play.cpp`, `Manage.cpp`, `LOD.cpp` | F-136, F-137, F-150..153 | Partial (Part 10 tiers, no economy) | XL | H for tiers | |
| 57 | Patrol and pirate tasks | O `Task/Patrol.cpp`, `Pirate.cpp` | F-152, F-172 | Absent | M | H | |
| 58 | Spawn task (colonies spawning ships) | O `Task/Spawn.cpp` | F-156..158 | Absent | S-M | M | |
| 59 | Personality traits (7) | O `AI/Traits.h` | F-156 | Absent | S | M | |
| 60 | Factions as Players (AI/human), owner, assets | N `Entities/Player.lua`, `Components/Owner.lua`, `Assets.lua`; O `Game/Player.cpp`, `Component/Asset*.cpp` | F-164, F-170 | Partial (2 NPC factions) | M | H | |
| 61 | Opinion / disposition per object | O `Component/Opinion.h`; N `Dispositions.lua` | F-173, F-179 | Absent | S | H | |
| 62 | Projects with P/L | O `Component/Projects.cpp`, `Game/Project.cpp` | F-165..169 | Absent | L | M | |
| 63 | Missions, mission board | O `Component/Missions.h`, `MissionBoard.h`, `Game/Mission.cpp` | F-175..178 | Absent | M | H | |
| 64 | Messages / log / NLP-phrased news | N `Components/Messages.lua`; O `Component/Log.cpp`, `Game/NLP.cpp`, `AI/NLPPhrase.cpp`, `HUD/Log.lts` | F-138 | Absent | S-M | M | |
| 65 | History component | O `Component/History.cpp` | F-138, F-222, SvL 8.11 | Absent | S-M after aggregate sim | L | |
| 66 | Crew | O `Component/Crew.h` | — | Absent | M | L | |
| 67 | Proximity tracker / queries / detectable | O `Component/ProximityTracker.cpp`, `Queryable.cpp`, `Detectable.h` | F-070 | Partial (combat grid) | M | H | |
| 68 | Escort formations (100 escorts in new LT start) | N `App/LTheory.lua`, `Actions/Escort.lua` | F-134 | Absent | M | H | |

### E.5 World, generation, rendering, audio

| # | Feature | Where | Prior | SOL | Effort | Fit | Decision |
|---|---|---|---|---|---|---|---|
| 69 | Universe hierarchy / regions / infinite systems | O `Game/Universe.cpp`, `Object/Region.cpp`, `System.cpp` | F-208..210, SvL 8.1 | N/A | S (kernels) | L | |
| 70 | System generator (planet, belts, zones, stations, AI) | N `Gen/SystemGenerator.lua`, `Entities/System.lua`; O `Object/System.lts` | F-210 | N/A (real system) | — | L | |
| 71 | Zones (SDF shapes, asteroid/ice/gas fields) | N `Entities/Zone.lua`; O `Object/Zone.cpp`, `Component/Zoned.cpp` | F-200..202, F-272 | Partial (belt, ring streaming) | M | M | |
| 72 | Procedural asteroids (noise mesh, LOD), ice | N `Gen/Asteroid.lua`, `Components/VisibleLodMesh.lua`; O `Renderable/Asteroid.cpp`, `Ice.cpp` | F-199, F-203, F-302 | Partial (sphere ISM; LOD debt) | M | H | |
| 73 | Procedural ships (ShapeLib, fighter/capital, mount points) | N `Gen/Ship*.lua`, `Gen/ShapeLib/*`, `GenUtil.FindMountPoint` | F-212, F-221, SvL 8.8 | Absent (placeholder) | L | M (Part 8 art may replace) | |
| 74 | Procedural stations | N `Gen/Station.lua`; O `Item/StationType/Generate.lts` | F-213 | Absent | M-L | M | |
| 75 | Nebula / starfield / dust flecks and clouds | N `Gen/Nebula/*`, `Gen/Starfield.lua`, `Entities/Dust.lua`; O `Object/DustFlecks.cpp`, `dustclouds.jsl` | F-197, F-294..297 | Starfield Built (AT-HYG, ahead); nebula N/A; dust Absent | S-M (speed flecks) | M | |
| 76 | Planets, atmosphere, planet rings | N `Entities/Planet.lua`; O `planet.jsl`, `atmosphere.jsl`, `planetring.jsl` | F-204, F-207, F-300 | Built / Planned (Part 9) | — | H | |
| 77 | Name grammar | O `LTE/Grammar.cpp`, `gamedata/grammar`; N `Name.lua` | F-211 | Absent | S | H (stations, NPCs) | |
| 78 | Procedural vector icons | O `Icons.lts`, `Game/Icons.cpp` | F-219, F-317 | Absent | S | H (D.1) | |
| 79 | Deferred renderer, SMAA, SSAO, lens flares, imposters | O `Game/RenderPass/*`, `lensflare.jsl`, `imposter*.jsl` | F-288..311 | Built via UE / Sun flare ToDo (`sun-glow-and-flare.md`) | S-M | H | |
| 80 | Post stack (aberration, bloom, colour grade, vignette, motion and radial blur, lofi) | O `shader/fragment/post/*` | F-291..306 | Built via UE | — | H | |
| 81 | Thruster trails / glare, explosions, warp-rail and wormhole shaders | O `thruster_trail.jsl`, `vertex/thruster_glare.jsl`, `explosion.jsl`, `warprail.jsl`, `wormhole.jsl`; N `Entities/Thruster.lua`, `Explosion.lua` | F-309, F-312, F-316 | Partial (engine glow, explosions) | S-M | H (TSR smear debt applies) | |
| 82 | Dynamic lights from entities | N `Components/Light.lua` | F-298 | Partial | S-M | M | |
| 83 | Synth audio engine (signals, envelopes, filters), music engine | O `Audio/*`, `Module/MusicEngine*` | F-321, F-322 | Absent (sound classes only) | M + content | M | |
| 84 | Engine/ambience loops tied to speed; UI sounds (target, menu, scanner) | O `Object/Ship.lts`, `Widget/RadialList.lts`, `Scanner.lts` | F-323..325 | Partial (combat sounds) | S | H | |

### E.6 UI, HUD, tools

| # | Feature | Where | Prior | SOL | Effort | Fit | Decision |
|---|---|---|---|---|---|---|---|
| 85 | Glow-SDF primitive set, batched (C.1) | N `UI/DrawEx.lua`, `res/shader/fragment/ui/*`; O `src/liblt/UI/Glyph/*`, `shader/fragment/ui/*` | F-245, F-318 | Absent (aliased Canvas) | M | H | |
| 86 | UI layer composite: grade, grain, scanlines, row jitter (C.2) | O `UI/Compositor/Basic.cpp`, `ui/basic.jsl`; N `ui/composite.glsl` | F-257, F-263, F-280 | Absent | S-M | M (keep subtle) | |
| 87 | Panels with bevel/shadow/sheen, focus glow | `ui/panel*`, O `Widget/Component/FocusGlow.lts`, `FocusFade.lts` | F-245 | Absent (plain UMG) | S | H | |
| 88 | Radial context menu (press-drag-release) | O `Widget/RadialList.lts`, `Component/ContextMenu.lts`, `ui/radialpanel.jsl` | F-260 | Absent | M | H | |
| 89 | G-key world icons, edge-pinned, distance-faded | O `HUD/WorldObjects.lts`, `HUD/WorldObject.lts` | F-277, F-278 | Partial (map-only icons) | M | H | |
| 90 | Per-object action menu built from components | O `HUD/WorldObject.lts` `WorldObjectMenu` | F-226, F-276 | Absent | M | H | |
| 91 | Object info window with tabs | O `Widget/ObjectInfo.lts`, `Widget/Object/*` | F-231, F-233, F-276, F-279 | Absent | M | H | |
| 92 | Command (RTS) panel: select, groups, Move To / Attack, focus | N `Controls/CommandControl.lua` | F-226..230 | Absent (jump map has the pick gesture) | L | H | |
| 93 | Formation / stance options (unwired in LT) | N `CommandControl:buildDetailsPanel` | F-228 | Absent | M | M | |
| 94 | Control panels and selector bar (Ship / Command / Debug / Dock) | N `Controls/MasterControl.lua` | F-225 | Partial (map mode, menus) | S-M | M | |
| 95 | Reticle with speed / integrity / rotation arcs | O `Widget/Reticle/Default.lts` | F-261, F-242 | Partial (reticle, speed readout) | S | H | |
| 96 | Trackers (wedges, lock, direction arrow), dock prompt | N `Controls/HUD.lua` | F-040, F-244 | Partial | S | H | |
| 97 | Minimap with sonar rings | O `HUD/Minimap.lts` | F-282 | Partial (3D radar, ahead) | S | H | |
| 98 | 3D radar plane lines with up/down colours (C.6) | proposal | — | Partial (radar with stalks) | S-M | H | |
| 99 | HUD layout: sockets, container, log, targets, menu bar; F4 hide; B lock | O `Widget/HUD.lts`, `HUD/*.lts` | F-241, F-286 | Partial | M | M | |
| 100 | Market UI (3 panels, transaction slider) | O `Widget/Market/*.lts`, `App/market.lts` | F-264, F-269, F-281 | Absent | M | M | |
| 101 | Assets list / cargo screen | O `Widget/Object/Assets.lts`, `Cargo.lts` | F-232, F-239 | Absent | M | M | |
| 102 | System map (orig flat; new debug) | O `Widget/Map.lts`; N `Game/SystemMap.lua` | F-250, F-251 | Built (jump map, ahead) | — | H | |
| 103 | Hologram mesh shader and 3D-in-widget | N `DrawEx.Hologram`, `ui/hologram.glsl`; O `hologram.jsl` | F-290, F-308, SvL 8.9 | Absent | S (material), M (viewer) | M | |
| 104 | Two-scale UI grid | `ui/grid.*` | — | Absent | S | M | |
| 105 | Retained UI toolkit (NavGroup, Window, Slider, Collapsible, Graph) | N `script/UI/*` | F-252..259 | Built via UMG | — | H | |
| 106 | Settings registry and auto-generated debug UI | N `env/util/Settings.lua`, `GUI/DebugWindow.lua` | F-224 | Built (settings page) | — | H | |
| 107 | Dev panel, metrics bar, profiler, entity debug dump, widget inspector | N `GUI/DebugWindow.lua`, `DebugInspector.lua`; O `Widget/DevPanel/*`, `Dev/FrameInfo.lts` | F-339, F-161, F-162 | Partial (NPC stats overlay, `stat`) | S-M | H | |
| 108 | Model / image / text editors, font preview, observatory | O `Widget/ModelEditor/*`, `ImageEditor.lts`, `Observatory.lts` | F-341..356 | N/A (UE editor) | — | L | |
| 109 | Splash / loading screen, game menu | O `SplashScreen.lts`, `App/loading.lts`, `GameMenu.lts` | F-273 | Partial (main/pause menus) | S | M | |
| 110 | Script hot reload (LTSL / Lua) | O `LTE/Script.cpp`; N `Main.lua` | F-341..343, SvL 8.5 | N/A | — | L | |
| 111 | Physics debug overlays, instant-jobs debug | N `Config.debug.*` | — | Partial (smoke flags) | S | M | |
| 112 | Persistence | none in either LT | SvL 3 | Partial (settings only) | M | H | |

### E.7 SOL-side items already evaluated elsewhere (kept for completeness)

| # | Item | Prior | SOL | Effort | Note | Decision |
|---|---|---|---|---|---|---|
| 113 | Orbit-line quality: add an LT glow halo term, pulse on the highlighted line, fade per C.1 | GAME_MECHANICS `O` row, SDD 17 | Built (#17, AA dashed); polish Absent | S | LT has no orbit lines in either tree; only its glow profile is borrowable | |
| 114 | Minor-body targeting | `ToDo/minor-body-targeting.md` | ToDo | M | prerequisite for icons on asteroids | |
| 115 | Star diffraction spikes | `ToDo/star-diffraction-spikes.md` | ToDo | S | | |
| 116 | Sun glow and flare | `ToDo/sun-glow-and-flare.md` | ToDo | S-M | LT `lensflare.jsl` reference | |
| 117 | Accurate pole directions, planetary perturbations, adaptive tessellation | `ToDo/*.md` | ToDo | S-M each | not LT-related | |
| 118 | Editor/tooling gaps (data editor, live tuning) | SvL 5 | — | — | see SvL 5 | |
| 119 | UE performance checklist for every new system above | `UE_PERFORMANCE_GUIDELINES.md` | process | — | e.g. icons and radar: no per-frame allocation, one batched draw | |
| 120 | Star field via AT-HYG instead of CelestialVault | `STAR_FIELD_CELESTIALVAULT_AND_ATHYG.md` | Built | — | LT starfield is procedural; SOL ahead | |

SR features F-001..F-370 not named above are covered row by row in SvL chapter 4 with SvL's ratings; this table does not repeat them.

---

## F. Proposed ticket breakdown (dependency order)

| # | Ticket | Contents | Depends on | Effort |
|---|---|---|---|---|
| T1 | **Newtonian core + NPC motion rework** | Thrust-limited per-axis envelope and bounded angular acceleration in `SOLFlight`; assist becomes a thrust-limited velocity controller with gravity compensation; one shared controller library used by `USOLShipFlightProcessor` and the Part 10 Full-AI processor (replacing `SOLNpcAiMath` point-mass steering); tests for envelope, gravity compensation, dt invariance | — | L |
| T2 | **Long-range travel decision** | Supercruise/drive regime or autopilot-under-warp, since thrust-limited assist cannot reach the current caps (A.5) | T1 | M-L |
| T3 | **Three flight modes + controls** | Manual / Assisted / Auto mode switch, steer gate (hold/toggle) with free cursor, key-map changes (Space conflict), autopilot orders for the player ship (Fly to point/body, Approach/Match, Orbit, Follow, Dock later) with brachistochrone legs, chase-camera lean and transition blend; GAME_MECHANICS and in-game info sync | T1, T2 | L |
| T4 | **Map orders (Homeworld style)** | `FSOLMoveOrder` from the jump-map pick gesture, "Fly there" next to "Jump", plane options and grid, selection/groups/focus from `CommandControl`, order queue display, hologram material (optional) | T3 | M-L |
| T5 | **HUD rendering quality** | Batched glow-SDF Slate element (C.1) replacing Canvas lines for reticle, trackers, radar and map overlays; panel style; UI composite (grain/scanlines/jitter) as a setting; radar plane, stalks and up/down colours (C.6); lead marker | — (in parallel with T1; touches `UI/`, so break the `Ship/`-`UI/` include cycle first) | M-L |
| T6 | **G-key icons + interaction menu + info window** | Icon layer over all targetables (edge-pinned, distance-faded), click-to-target with the free cursor, radial/list action menu driven by a per-type capability table (D.3), object info window | T3 (free cursor), T5 (primitives), minor-body targeting ToDo for asteroids | M-L |
| T7 | **Orbit-line quality** | Glow halo term and highlight pulse in `SOLOrbitLine.ush`, consistent with T5 colours | T5 | S |
| T8+ | **Feature adoption** | Per the E table decisions, roughly in SvL 6.7 phase order: items + cargo + credits + save, weapons variety and turrets, stations and docking, mining and scanner, factions and missions, economy, research | T1-T6 as relevant | per row |

---

## Questions the owner must decide

1. **G key.** LT uses G for the world-icon toggle (and new LT uses G to clear the target); SOL's G drops a combat target. Rebind the drop-target key, or use another key for icons?
2. **Steer gate.** Hold or toggle a key to steer with the mouse (LT), or keep today's always-on stick and add a key that frees the cursor? Which key, given that SOL's Space is thrust up?
3. **Assist physics.** Make assist thrust-limited (truly Newtonian, A.5)? If yes, how is long travel done: supercruise, autopilot under warp, the jump map only, or a mix?
4. **Rotation model.** Keep rate-command rotation with a bounded angular acceleration, or move to torque/inertia for manual mode?
5. **Interaction menu style.** LT radial press-drag-release or a vertical list (LT video style)? Is it opened by right-click on icons only, or also on 3D objects?
6. **UI effects.** Adopt LT's grain, scanlines and row jitter at all, and at what default strength (SR noted LT's glitch was too strong)?
7. **HUD tech.** Replace the Canvas `AHUD` drawing with a batched Slate custom-vertex element (needed for glow quality at one draw call)?
8. **Map orders scope.** Before fleets exist, are map move-orders only for the player's own ship (autopilot), or should NPC/fleet command be designed in now?
9. **Radar colours.** Above/below-plane colour pair (proposed primary blue / secondary amber, dashed below) and which plane: ship horizontal, ecliptic or reference body equator?
