<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 20: Hologram map and move orders

Status: design agreed (grill-me complete), not yet implemented. Depends on SDD 3 (jump map), SDD 18 (autopilot),
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
