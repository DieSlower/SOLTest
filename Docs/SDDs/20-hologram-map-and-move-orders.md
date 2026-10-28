<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 20: Hologram map and move orders

Status: design agreed (grill-me complete); steps 1-4 (keys, route logic, map modes, route subsystem) built, see
Amendment 1; step 5 (route overlay, HUD route marker) built, see Amendment 2; steps 6-8 (hologram look) not yet. Depends on SDD 3 (jump map), SDD 18 (autopilot),
SDD 19 (flight modes). Issue #25 (map alignment and shimmer) is fixed first as its own commit.

## 1. Goal

Turn the jump map into a mode-based strategic map with a hologram look, and add ordered **Move** and **Patrol**
routes that the autopilot flies. Player ship only. Routes are visible on the map and in the flight view.

## 2. Decisions from the grill

### 2.1 Keys and modes

| Key | Where | Meaning |
|---|---|---|
| Shift+M | flight | Open the map (mode: last used, default Move) |
| Shift+J | flight | Open the map straight in Jump mode |
| J / M / P | map | Switch to Jump / Move / Patrol mode. J does nothing outside the map |
| M / J / P again | map | M again in Move mode: no-op. P again in Patrol mode: flips Loop / Ping-pong |
| Enter | map | Jump mode: jump as before. Move/Patrol: start the route |
| Esc | map | Close the map (it must not end PIE; the existing close action stays) |
| Ctrl+Right click | map, Move/Patrol | Undo the last waypoint (a plain right drag still orbits) |
| X | map | Clear the draft (and the jump pick in Jump mode) |
| Delete | map | Clear the active route |
| V (default, rebindable) | flight and map | Resume a paused route |
| P | flight | Toggle the route overlay (was a no-op placeholder) |

- Plain **M stays match-lock** in flight; only Shift+M opens the map, so no clash. A Shift-modified press must not
  also fire the plain-key action (and thrust-up on Left Shift is ignored while the map is open).
- `ToggleMap` (J) is replaced by an `OpenMap` binding (default M, requires Shift) and a `OpenMapJump` binding
  (default J, requires Shift). Settings SAVE_VERSION bump with migration of a rebound ToggleMap.
- R is taken (NextTarget), hence V for Resume (open item: user may choose another key).
- The ballistic "predicted path under gravity" is NOT built here (`Docs/ToDo/predicted-path.md`).

### 2.2 Route model

- A route is an ordered list of **waypoints**. A waypoint is a normal map pick: reference (body or ship frame) plus
  locked planar offset and height, so it **follows its reference body** (it moves with the planet).
- Unlimited waypoints. Drawing and planning must therefore be bounded by view and horizon, not by count: only legs
  near the camera/active leg are tessellated; the planner keeps the current and next leg only (lazy).
- Kinds: Move (A to B to C, ends), Patrol Loop (A, B, C, A, ...) and Patrol Ping-pong (A, B, C, B, A, B, ...).
  Patrol repeats until cancelled.
- Draft route (being planned) is dim; active route is bright with a progress marker; paused route is dim with a
  "PAUSED" tag. Routes stay visible until cleared. A finished Move stays shown dimmed until cleared.
- Starting a new route replaces the active one.

### 2.3 Flight

- Auto mode engages through the SDD 19 API. Intermediate waypoints are **flown through** (no stop). The planner picks
  a pass speed per waypoint from the turn angle so lateral acceleration stays within the sustained G budget
  (1.5 G for humans; hull limit for AI), with a capture radius after which the next leg starts. The last waypoint of a
  Move stops the ship (flip-and-burn to rest, existing planner) and the mode drops to Assisted.
- Manual thrust/steer cancels Auto (SDD 19 rule). The route is **kept paused**; Resume re-plans from the current state
  to the next waypoint.
- Time warp is never switched automatically (SDD 18).
- ETA/countdown per leg and for the whole Move; HUD AUTOPILOT marker shows the route progress.

### 2.4 Look

- Map view stack (all three): hologram proxy bodies replace the lit meshes, a post-process tints and adds scanlines and
  flicker over everything, and an overlay draws the ecliptic grid plane, vertical stems from bodies and waypoints to the
  plane, and the route lines and markers in the HUD palette.
- Camera: the existing free orbit camera and defaults are unchanged.
- Icons stay the current plain discs; icon shapes and categories wait for #22.
- The hologram radar and the hologram look for the rest of the flight HUD (target, info, boost) are **#21**. #20 defines
  the shared hologram style (colours, glow falloffs, scanline/flicker parameters) in one place (`SOLConstants.h`
  `SOL::Hologram`) so #21 reuses it. Cockpit 3D in-world screens: `Docs/ToDo/cockpit-3d-screens.md`.

LT reference (from the research, Limit Theory source): hologram fragment = Fresnel rim `1 - |N.V|` x time noise
`1 - 0.1 log(1 - noise)` x 1 px scanlines `1 + 0.2 sin(PI y)`, additive, colour x2; contour bands
`2 exp(-4 |spec - k|)`; grid with 16 and 64 unit scales and dfield `exp(-2 d^2) + 0.2 exp(-(0.2 d)^0.75)`, fade
`2 exp(-2 uv.y)`, Primary at 0.1 alpha; glow line `0.8 exp(-2 max(0, d - 0.5)) + 0.2 exp(-(0.2 d)^0.75)` with the
directional fade `exp(-2 (1 - t))` (used for route direction). Palette: Primary (0.2, 0.7, 1.0), Secondary
(1.0, 0.3, 0.1), Good (0.25, 1.0, 0.25). LT has no route drawing, so the styles below are new:

| Element | Style |
|---|---|
| Active route leg | solid Primary glow line, directional fade along each leg, ring marker at each waypoint |
| Draft route | same at ~0.4 alpha; the segment to the cursor in Secondary |
| Paused route | ~0.3 alpha plus tag |
| Patrol loop closing leg | dashed |
| Waypoint below the plane | hollow marker, Secondary tint; above the plane Primary |

### 2.5 Related tickets

- #25 first (separate commit), then #20 feature work.
- #21 owns the hologram radar and all-HUD hologram treatment.
- #22 owns icon shapes, the flight overlay layer and the radial menu (it will also issue Move orders to a body).

## 3. Architecture (outline)

- `Map/SOLRoute*` pure logic: route data, waypoint resolution, leg planner with corner speed
  (`SOLRoutePlanner`), patrol sequencing, progress and ETA. Test-first.
- `Map/SOLMapModeSubsystem` gets a mode state (Jump/Move/Patrol), draft route, and forwards to a
  `USOLRouteSubsystem` that owns the active route and drives the autopilot (extending `Flight/SOLFlight*` with a
  waypoint order and a pass-speed target).
- `Map/SOLMapRouteOverlay` draws routes (map and flight) via the same batched approach as `SOLMapPickOverlay`.
- `Visuals/SOLHologram*` (proxy materials, post-process, grid plane) with `Tools/Map/create_hologram_assets.py`.
- Input: new actions on the pawn (map modes, ctrl+right click, delete, resume, route toggle); `SOLKeyBindings` rows and
  `SOLMenuText` entries; Controls/Info screens and `Docs/GAME_MECHANICS.md` updated in the same change.

## 4. Open questions / follow-ups

- Resume key V is a proposal (R is taken).
- Exact corner-speed formula and capture radius are tuning values chosen in the plan (documented in GAME_MECHANICS).
- Map-close key repeat and Ctrl+right-click vs orbit drag threshold need a hands-on PIE check.
- Drag-to-move waypoints, formation/fleet orders and NPC orders are out of scope.

## 5. Test plan

Pure-logic tests (route model, planner corner speed, patrol sequencing, mode state machine, key bindings and the
settings migration) authored by a subagent from the contract; smokes for map mode switching, route start/pause/resume
and the overlay; full suite; adversarial opus review.

## Amendment 1: steps 1-4 as built (2026-10-28)

What shifted or was decided during implementation (none of it changes a grill answer):

- **Keys.** `ToggleMap` is replaced by the Shift chords `OpenMap` (M) and `OpenMapJump` (J), plus `Resume` (V) and
  `ToggleRouteOverlay` (P, rebindable; P is no longer reserved). The chords use `UInputTriggerChordAction` on an
  `IA_ShiftChord` action (Left/Right Shift) in both contexts, and Enhanced Input's automatic chord blockers keep plain
  M (match-lock) and the map's plain J/M/P from firing with Shift held. On the map Shift+M closes it (the "existing close
  action" kept beside Esc) and Shift+J switches to Jump mode. `FBindingDef::bShiftChord`; a chord and a plain binding on
  one key do not conflict; chords refuse Shift and the mouse buttons; Resume (also mapped on the map) refuses the map's
  fixed keys (now including Delete, J, M, P). Settings schema 4 (`KEY_BINDING_LAYOUT_VERSION` 4): a rebound ToggleMap
  moves to OpenMapJump. Shown keys come from `SOLKeyBindings::BindingDisplayName` ("Shift+M").
- **Shift as thrust up.** Left Shift is also thrust up, so pressing Shift+M in AUTO would cancel the autopilot. In AUTO,
  flight input held only on Shift keys counts after `SOL::Route::SHIFT_CHORD_GRACE_S` (0.5 s). Still to check hands-on.
- **Ship-referenced picks.** A waypoint must follow a body, so a pick with the ship as reference is re-expressed
  relative to the anchor body at that moment. A waypoint closer to its body than the autopilot's geometric standoff
  (1.25 radii + 10 km) is pushed out to it at route start.
- **Corner planner.** The turn is an arc at the corner G budget (player: sustained 1.5 G capped by the hull; AI: hull)
  whose tangent length is at most 25% of the shorter adjacent leg and at least 1 km; pass speed
  `sqrt(a * tangent / tan(turn/2))` capped at the cruise cap, capture radius = that tangent length. Turns of 150 deg or
  more (and ping-pong ends) are full stops. Only the current waypoint is planned, when its leg starts.
- **Guidance.** `FSOLGuidanceParams` gains `PassSpeedMps` and `CaptureRadiusM`: the braking curve is
  `sqrt(stop(d - capture)^2 + pass^2)` (the pass speed is reached at the capture radius, where the next leg starts),
  and arrival is a swept check of the step's straight-line motion against the capture radius. A captured fly-through
  coasts through the rest of its frame (no homing back). The waypoint order is `USOLShipSubsystem::EngageAutopilotPoint`
  (body + fixed offset); a pass-through order keeps AUTO on arrival; arrivals carry an order serial so the route only
  advances on its own orders.
- **Route subsystem.** `USOLRouteSubsystem` listens to `OnShipsStepped` (another bind-order-dependent listener of that
  event). A one-waypoint route of any kind is a Move (a patrol needs two to repeat). The Move's remaining-time estimate
  of the later legs is built once per start (suffix sums, O(waypoints)); ETAs use the plan acceleration without gravity
  (optimistic). StartRoute requires pilot control, as Resume does.
- **Draft.** The draft route survives closing the map (X clears it); a mode change drops the pick in progress.
- **Verification hooks.** `sol.Route <Status|Resume|Clear>`, `-SOLSmokeRoute` (modes, picks, undo, start, pause,
  resume, overlay toggle, Delete, Shift+J / Esc, and a scripted 2-waypoint Move flown through its 90 deg corner to rest).
- **Open.** The overlay and HUD route marker (step 5) are not built, so P only flips a flag. `-SOLMenuSmoke=all` has one
  failure ("thrust flies the ship", relative speed falls 4876 -> 4839 m/s at Mars) that does not involve any code this
  change touched; `-SOLSmokeJump` reports anchor Deimos instead of Mars at arrival (ephemeris-dependent; jump code
  untouched). Both left for follow-up.

## Amendment 2: step 5 as built (route overlay and HUD route marker)

- **Drawing.** `FSOLMapRouteOverlay` (owned by `ASOLFlightHud`) draws on the map always and in flight while P is on.
  Everything is one additive `FCanvasTriangleItem` batch (canvas lines ignore alpha, so brightness is carried in RGB):
  each leg is a glow line (bright core plus a halo fading to nothing) tessellated into pieces of at most 32 px (16 per
  leg) so the LT fade `exp(-2 (1 - t))` reads along it; a Ping-pong travelling back fades the other way. Arrowheads sit
  at each leg's true middle (two opposite ones for Ping-pong); a Loop's closing leg is dashed (10 px / 7 px at 720 px).
  Rings at waypoints: Primary with a centre dot above the ecliptic plane through the ship (map: the camera focus),
  hollow Secondary below; the current target's ring is larger. Alphas: active 1, draft 0.4, paused 0.3 with a PAUSED
  tag at the target, finished 0.3. The ship has a leg to the route's current target (the "progress" leg). The draft's
  segment to the cursor (Secondary, 0.8) runs from the last draft waypoint, or the ship, to the live drag point.
- **Projection.** The HUD's view (horizontal-FOV focal length, issue #25) as the pick overlay. Points are placed at
  their true (uncompressed) offset from the render viewpoint plus the viewpoint's render location: the far-field
  compression is radial from the viewpoint (where the map camera is), so this projects where the bodies' compressed
  placement does (markers within 0.5 px of the engine's projection in the smoke) while depths stay true, so the
  near-plane clip, the perspective-correct fade parameter and the arrows at each leg's true middle are exact for far
  legs too (review fix). Each leg is clipped to the near plane, then to the screen (Liang-Barsky), before tessellation,
  and stops short of the rings at its own on-screen ends so a hollow ring stays hollow.
- **Bounds.** Legs are visited in priority order around the active leg (draft and finished: the last leg), at most
  512 looked at and 128 drawn per route; markers over the visited range, nearest the target first, at most 256. One
  batch of at most 16,384 triangles (reserved once), filled in order: the draft's cursor segment, the route (ship leg,
  legs, markers) within 75%, then the draft in the rest, so the plan being edited is never starved. Pure helpers (leg indexing, active leg, priority order, clipping, fade, tessellation, dashes, plane test,
  style alphas) are `SOLRouteOverlayMath`, tested in `SOLTest.RouteOverlayMath` (written from the contract by a
  separate agent).
- **HUD marker.** One line under the autopilot block (or where it would be): kind, Move leg n/N and percent or patrol
  waypoint and legs flown, ETA (Move: to the end; patrol: next waypoint), `PAUSED   <Resume> resume` in Secondary, or
  `COMPLETE` dim. If it would overlap the key-hint lines (measured each frame) it moves below them. Known: the existing
  AUTOPILOT line at 64 px already overlaps the long flight key-hint line horizontally at 1280x720 (not changed here).
- **Glow style.** Core and halo widths and the halo intensity are `SOL::Hologram` (shared with #21). The halo is a
  linear ramp from 0.3 of the core to nothing, a cheap stand-in for LT's `0.8 exp(..) + 0.2 exp(-(0.2 d)^0.75)` profile.
- **Review leftovers (accepted).** The closing leg's dash phase is anchored to the visible start, so dashes can crawl
  while the camera moves; the key-hint lines are measured (`StrLen`) every frame for the overlap check.
- **Verification.** `-SOLSmokeRouteOverlay` (25 checks, markers within 2 px of the engine's
  `ProjectWorldLocationToScreen`, screenshots in flight, map and with a draft).
- A unity-build clash (`IsFiniteVector` in two anonymous namespaces) surfaced when the new files reshuffled the blobs;
  `SOLOrbitLineMath.cpp`'s copy is renamed `OrbitLineIsFiniteVector`.
