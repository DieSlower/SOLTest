<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 38: NPC lanes as one animated hologram line per lane, with an overlay-menu toggle

**Issue:** [#38](https://github.com/DieSlower/SOLTest/issues/38). Plan:
[`Plans/38-npc-lane-lines-plan.md`](../Plans/38-npc-lane-lines-plan.md). Amends SDD 11 (section 3.4 and Amendment 2:
the far point layer) and SDD 22 / SDD 34 (the overlay table gains a row). Uses SDD 17 (orbit-line ribbon technique and
shader library) and SDD 37 (the HUD glow scale `g`). Leaves an extension point for issue
[#49](https://github.com/DieSlower/SOLTest/issues/49) (lane guidance tunnel), which is **not** designed here.
Persistence change: settings save schema 12 -> 13 (approved, D1).
Status: **part 38a built (2026-11-11):** the pure lane math (`Npc/SOLNpcLaneMath`), the Lanes row of the Overlay Menu
and settings schema 13; the renderer (38b) and the tuning and docs (38c) are next. Grill-me done 2026-11-11 (D1-D6);
the draft's open questions Q1-Q18 were answered by the user the same day (D7-D24, section 2); D25 (the glow scaled with
the viewport height) was decided by the user after the 38a review. No decision is open.

## 1. Problem

In the NPC demo (SDD 11) 40% of the ships ride the 8 lanes (Mercury-Venus, Venus-Earth, Earth-Moon, Earth-Mars,
Mars-Jupiter, Jupiter-Saturn, Saturn-Uranus, Uranus-Neptune). The far layer (`M_SOLNpcPoints`, GPU rail points) draws
each one as an additive point of at least 2 px. At 100k-1M ships the points of a lane merge into a plain, nearly white
band between the planets: it clutters the view and says nothing about traffic or movement.

## 2. Decisions (user, grill-me 2026-11-11)

- **D1: Schema bump APPROVED.** `SOL::Settings::SAVE_VERSION` 12 -> 13 with a migration: existing overlay profiles
  keep every value, the new Lanes flag is on in every profile. Version and migrate, never discard.
  `KEY_BINDING_LAYOUT_VERSION` stays 10: the Lanes row is a row of the Overlay Menu, with **no new key**. (If a binding
  turns out to be needed, the implementer stops and reports; it does not choose one.)
- **D2: Far look = "dashes".** Each lane is drawn as **two thin parallel dashed lines whose dashes flow in opposite
  directions**: outbound on one, inbound on the other. Colours from the hologram palette, never plain white: **outbound
  teal-white (#9FD6D0 family)**, **inbound cyan (#3FD0E0 family)**. **Keep right** (user, 2026-11-11, stated for the
  #49 tunnel and applied here so the two agree): forward / outbound traffic is on the **right**, inbound on the
  **left**; on screen each line lies to the right of its own direction of travel (section 4.6).
- **D3: Width and brightness follow the lane's ship count,** log scaled, with sane minimum and maximum clamps.
- **D4: Close-up: the line fades out where the ships resolve,** a plain distance-based fade. The fly-through guidance
  tunnel is issue #49, not part of #38.
- **D5: "Lanes" is a row of the Overlay Menu (Ctrl+O), on by default, stored per overlay profile** like the other rows
  (changed in memory at once, written at the next settings write point, disk writes minimized per the persistence
  rules). The dashes follow the HUD glow setting (SDD 37 `GlowScale`, the `g` scale).
- **D6: The ships stay in the simulation;** only their far-field rendering as a merged line changes (the individual
  far-field points of lane ships are suppressed or replaced, per this design).

Answers to the first draft's open questions (user, 2026-11-11; all as recommended unless stated):

- **D7 (was Q1): Rendering route** = a world-space ribbon pair in `ASOLNpcVisuals` with a new `M_SOLNpcLanes`, placed
  on the GPU from the existing group texture, reusing the orbit-line shader library (section 4.8). Not the HUD shape
  batch, not Niagara.
- **D8 (was Q2): Endpoints** = the two body centres from `GroupPointAM / BM` every frame; the parts inside or behind a
  planet are hidden by the analytic occluders, no trimming (section 4.3).
- **D9 (was Q3): Crossfade.** The line fades out while the lane ships' far points fade in as separate dots up close
  (`M_SOLNpcPoints`, lane kind only, point alpha = 1 - line fade), so a lane is never empty at any distance
  (section 4.5).
- **D10 (was Q4): Lanes off** (the row unchecked) restores the lane ship points exactly as today.
- **D11 (was Q5): The master O turns Lanes off too,** like every row.
- **D12 (was Q6): Fade:** line full beyond 400 lane radii, gone inside 100, linear in log distance between.
- **D13 (was Q7): Ship counts** by a histogram once per population, folded into the existing rail-fill `ParallelFor`;
  per frame only the lanes' values (O(lanes) at any ship count).
- **D14 (was Q8): Width and brightness:** weight log-linear from 100 to 100,000 ships; each of the two dashed lines
  1.0-2.5 px at 1080p, scaled by the viewport height; gap 1.5 px (scaled); brightness 0.35-1.0; lanes with no ships
  are not drawn.
- **D15 (was Q9): Dashes** fixed to the lane (lane-parameter units, the orbit lines' multi-scale levels), steady flow:
  64 base dashes per lane, 50% duty, 0.75 dash periods per second at every zoom; outbound = body A to body B (layout
  order); keep right on screen (D2); real-time clock that ignores time warp and stops while the game is paused.
- **D16 (was Q10): Colour after tone mapping:** no HUD colour gain; the emissive scale is tuned on the screenshots so
  the brightest core keeps its hue.
- **D17 (was Q11): Overlay row** label "Lanes" (id `"Lanes"`), top level, between "Ships on Radar" and "Darkening"
  (Darkening moves to index 15 and stays last).
- **D18 (was Q12): Map:** the lanes show on the hologram map too, under the route overlay, following the same row
  (section 5).
- **D19 (was Q13): Wording** as in section 7; no Help glossary entry.
- **D20 (was Q14): Tooling:** a new `-SOLSmokeNpcLanes` flag and a developer cvar `sol.NpcLanes 0|1` (default 1),
  both documented in `ARCHITECTURE.md`.
- **D21 (was Q15): Overlay Menu at one XR-4 eye:** measured; if the extra row pushes it over the provisional 1.2 ms
  ceiling, it is recorded in `Docs/TECH_DEBT.md` and the VR targets doc, not fixed (no VR work before procedural
  terrain).
- **D22 (was Q16): Budgets, gated at 1920x1080 only:** lanes <= 0.15 ms GPU (all lanes, 1M ships), NPC translucency
  (points + lanes) at most +0.05 ms over the before figure, `ASOLNpcVisuals::Redraw` at most +0.02 ms CPU; 4K and one
  eye measured and recorded, not gated.
- **D23 (was Q17): Module dependency:** `ASOLNpcVisuals` reads `USOLSettingsSubsystem` directly (the Lanes row, the
  live HUD glow): a new dashed `Npc -> Menu` edge, like `Ship -> Menu` and `UI -> Menu` (38b). The pure lane math
  (38a) also reuses two pure headers of other folders, which adds the edges `Npc -> UI` (`UI/SOLHudShapeMath`: the HUD
  glow profile and its sanitizer) and `Npc -> OrbitLines` (`OrbitLines/SOLOrbitLineMath`: the dash coverage); neither
  folder includes `Npc/`.
- **D24 (was Q18): Occluders:** the lanes share the orbit lines' parameter collection, and `ASOLOrbitLineVisuals`
  writes the occluders whenever orbit lines **or** lanes are drawn.
- **D25 (user, after the 38a review, 2026-11-11): the glow scales with the viewport height** like the line widths, so a
  lane looks the same at 1280x720, 1920x1080 and 3840x2160: the glow profile's distance past the line's core is measured
  in reference pixels (divided by `height / NPC_LANE_REFERENCE_HEIGHT_PX`), and the ribbon's half extent puts its edge
  where the scaled glow has its 1080p pad value at every height, so the pad never clips the glow. At 1080p both are
  exactly the D14 / section 4.7 values.

## 3. What exists today (code survey, HEAD `0eae5b2`)

| Piece | Where | Relevant facts |
|---|---|---|
| Lane rails | `Npc/SOLNpcRail.h`, `Npc/SOLNpcScaleMath.cpp` (`DefaultLayout`, `MakeSpawn`, `LaneBasis`) | A lane ship's rail is `TriangleWave01(P0 + P1 t)` along A-B plus a lateral offset inside the lane radius (2e5 m Earth-Moon to 1e7 m outer lanes). Each ship picks its lane by a weighted hash (weights 0.5-2.0), and its direction (+1 / -1) by hash; on the triangle wave it reverses at each end, so about half of a lane's ships fly each way at any time |
| Group table and frame | `FSOLNpcGroupTable` (`LaneBodyA/B`, `LaneRadiusM`), `FSOLNpcRailFrame::GroupPointAM / GroupPointBM` | Built every frame by `SOLNpcRail::BuildFrame` from the registry body positions (lanes first in the global group index). Lane endpoints are the two **body centres** |
| Far layer | `ASOLNpcVisuals::UpdateFarPoints`, `M_SOLNpcPoints` (`Tools/Npc/create_npc_assets.py`) | Not Niagara (SDD 11 Amendment 2): a material-driven instanced block mesh. Per frame only a 64x2 float **group texture** (A in row 0, B in row 1, relative to the viewpoint; **the `.w` channels are unused, written 0**) and four vector parameters change. The vertex shader evaluates every rail (kind 1 = lane), hides ships inside the Mid radius, keeps a 2 px floor, brightness `(2,000 km / d)^0.5` floored at 4%, faction tint blue / orange, additive |
| Ship counts per lane | nowhere | Not stored. The spawn buckets (`mSpawnBucketStarts`, (group, class) order) and the rail array (`GetRails()`, `Kind`, `Group`) both hold them implicitly |
| Orbit lines | `OrbitLines/` (SDD 17), `Shaders/Private/SOLOrbitLine.ush` | The project's proven world-space line technique: camera-facing ribbon expanded in the vertex shader, constant pixel width scaled by viewport height (1.5 px at 1080p), analytic AA (`LineCoverage`), multi-scale world-anchored dashes (`MultiScaleDashCoverage`), log-distance fade, far-depth compression per vertex (`PlaceLinePointCm`), analytic true-scale planet occlusion (8 spheres in the `OrbitLine*` material parameter collection), translucent unlit, depth test off, sort priority 100. **The occluders are only written while the orbit lines are drawn** (`HandleUniverseUpdated` returns early when they are off) |
| Overlay table | `UI/SOLOverlayModel.{h,cpp}` | 15 rows in `EOverlay` order, `Darkening` last (index 14). A profile mask is a `uint32` (bit = row). Persisted **by row id** in each profile's `TMap<FString, bool> Overlays`; `FromSavedProfile` gives a missing id its table default. `IsShown` requires the active profile's master O (SDD 34: the master governs every row) |
| Overlay Menu | `UI/SOLOverlayMenuWidget.*` | Rows built from the table (`ROW_WIDGET_COUNT = OVERLAY_COUNT + 2`: the table, the "Orbit Lines" row, the profile-enable row); also embedded in Settings > Overlays |
| Settings save | `Menu/SOLSettingsSave.cpp` | Schema 12. Migration steps are frozen (literal ids). An **unused** profile slot is stored with an **empty** row map; that is how `UsedProfiles` is persisted (SDD 34 section 5.1), so a migration must never add a key to an empty map |
| HUD glow | `SOLHudShapeMath::HudGlowScale`, `GlowProfile(d, g)`, `FSOLGlowScaleFeed` (SDD 37) | `g = HudGlow setting x 0.3`; the HUD glow profile is a core plus a halo whose weight scales with `g` |
| Text | `Menu/SOLMenuText.h` | Controls row for `@OverlayMenu` lists the row kinds; the Info "Overlay profiles" paragraph and the Demo paragraph; `Docs/PLAYER_GUIDE.md` is generated from these texts |

Related tickets: #39 (NPC ships vanish at distance, no pixel or brightness floor) will make the lane line the only far
cue of lane traffic; #49 builds on this ticket's close-up hand-off (section 4.10).

## 4. Design

### 4.1 Overview

Each lane with traffic is drawn as **one world-space ribbon pair** (the two dashed lines of D2) between its two body
centres, built and placed entirely on the GPU from the NPC group texture the far layer already uploads every frame. The
line is **faded by the true distance from the camera** to the lane point under each pixel, in units of the lane radius
(D4); the far points of the **lane ships** are cross-faded the other way, so where the line is fully on, a lane ship's
far point is not drawn, and where the line has faded out, the ships show as today (D6). Cluster and battle ships, and
every CPU tier (Mid / Full within the Mid radius), are untouched.

Route (D7): this world-space renderer in `ASOLNpcVisuals`, reusing the orbit-line shader
library. The rest of this section describes that route.

### 4.2 Lane frame (CPU, pure, `Npc/SOLNpcLaneMath`)

- **Ship counts per lane (D13).** `CountLaneShips(rails, laneCount, outCounts)` counts the rails of kind Lane per lane.
  Run once per population, folded into `ASOLNpcVisuals::FillRailTexels`' existing `ParallelFor` (per-task local counts,
  merged at the end), so it adds no pass over the 1M rails. Counts are constant for a population (destroyed ships
  respawn at their rail slot).
- **`FSOLNpcLaneFrame`** (plain data, per lane): endpoints A and B (universe metres, from `GroupPointAM / BM`), lane
  radius, ship count, traffic weight (section 4.4), drawn flag (at least one ship and both bodies resolved). Built each
  frame by `BuildLaneFrame(groups, railFrame, counts, outFrame)`: 8 lanes, arrays sized once per population, no
  allocation per frame. It is the **extension point for #49** (section 4.10), exposed by `ASOLNpcVisuals::GetLaneFrame()`.
- **GPU feed.** The lane data rides in the group texture's free `.w` channels, written in `UpdateFarPoints` with the
  rest of the row (no new texture, no new upload): row 0 `.w` = lane radius (m), row 1 `.w` = traffic weight in [0, 1],
  or -1 for a lane that is not drawn. Non-lane groups keep 0.

### 4.3 Endpoints follow the bodies (D8)

The endpoints are the body centres from `FSOLNpcRailFrame` (the same data the rails use, D-note of the issue), so the
line follows the bodies every frame for free: the group texture is already rewritten each frame. The line is **not**
trimmed at the bodies' surfaces: the part inside or behind a planet is hidden by the analytic true-scale occluder
spheres (SDD 17 section 4.3; a point inside a sphere is occluded because the camera-to-point segment enters it).
Float precision: endpoints are viewpoint-relative floats (as for the rails): ~270 km at 30 AU, far below a pixel at
that distance; the close-up part, where float error would matter, is faded out (section 4.5).

### 4.4 Width and brightness from the ship count (D3, D14)

- **Traffic weight** `w = clamp(log10(n / N_LO) / log10(N_HI / N_LO), 0, 1)` for `n` ships on the lane;
  `n <= 0` -> the lane is not drawn. Proposed `N_LO` = 100, `N_HI` = 100,000 (at 1M ships the default layout puts
  ~44k-89k ships on each lane, at 100k ~4.4k-8.9k, at 10k ~440-890).
- **Line width** (each of the two lines) `= lerp(W_MIN, W_MAX, w)` px at 1080p, scaled by the viewport height like the
  orbit lines (SDD 17 Q4): proposed 1.0-2.5 px at 1080p (0.67-1.67 px at 720p, 2-5 px at 4K). A line under 1 px keeps
  a 1 px footprint at proportionally lower intensity (as `SOLOrbitLineMath::LineCoverage`), so thin lanes do not flicker.
- **Separation:** the two line centres sit at `+-0.5 x (width + GAP)` px from the lane's projected centre line,
  `GAP` = 1.5 px at 1080p (scaled), so they never overlap.
- **Brightness** `= lerp(B_MIN, B_MAX, w)`, proposed 0.35-1.0, times the colour (section 4.7).

### 4.5 Close-up fade and the lane ships' points (D4, D6, D9, D10, D12)

- **Line fade** `LaneLineFade(d, R)`: 0 for `d <= FADE_NEAR x R`, 1 for `d >= FADE_FAR x R`, linear in `log d` between;
  `d` is the true distance from the camera to the lane point under the pixel, `R` the lane radius. Proposed
  `FADE_NEAR` = 100, `FADE_FAR` = 400 lane radii. Derivation (1920x1080, 90 degree FOV, ~960 px per radian): the lane's
  apparent width is `2R / d x 960` px, so the ships spread over about 19 px at 100 R (they read as individual points)
  and about 5 px at 400 R (they merge). The fade therefore runs exactly over the band where the points stop merging.
  It is distance based (D4) and resolution independent; at 4K the ships are still slightly merged where the line has
  gone (accepted, D12).
- **Ship points** (`M_SOLNpcPoints`, rail mode, kind 1 only): alpha `= 1 - LaneLineFade(d_ship, R)` while the lanes are
  shown, 1 otherwise; an alpha of 0 returns the hidden offset (no raster cost). `d_ship` is the shader's existing
  `length(relM)`; `R` comes from the group texture (section 4.2). The two fades are complements at the same distance, so
  the total brightness of a lane is continuous across the crossfade.
- **Never touches the CPU tiers.** `FADE_NEAR x R` is at least 2e7 m (Earth-Moon), always far beyond the Mid radius
  (2e6 m), so every lane ship close enough to be CPU-simulated already shows as a point or hull, as today.
- **Lanes off** (the row unchecked or the master O off): the line is not drawn and every lane ship point draws exactly
  as today (D10).

### 4.6 Dashes and their flow (D2, D15)

- **Two lines per lane:** the **outbound** line carries traffic from body A to body B (the layout's order: sunward body
  first for the planet lanes, Earth first for Earth-Moon and Earth-Mars), the **inbound** line from B to A. Side rule
  "keep right": seen on screen, each line lies to the right of its own direction of travel, so the outbound line is on
  the screen-space right of the projected A -> B direction (`LaneOutboundSideNormal`, section 6) and the rule holds
  from any viewpoint.
- **World-anchored dashes:** the dash pattern is fixed in lane-parameter units (base count `DASH_BASE_COUNT` = 64 per
  lane, duty 0.5), with the orbit lines' multi-scale scheme (SDD 17 Amendment 1: the period halves in power-of-two
  levels while a dash is longer than 24 px on screen, up to level 8, then fades to solid between 200 and 800 px), so the
  dashes neither swim when the camera moves nor vanish in a gap.
- **Flow:** each level's phase is `s x DASH_BASE_COUNT x 2^k - direction x f`, where `f = frac(FLOW x t)` is one flow
  phase in dash periods, `FLOW` = 0.75 periods per second (proposed), `direction` +1 outbound (dashes move toward B),
  -1 inbound. Because the flow is added after the level scaling, the dashes move by the same fraction of their own
  on-screen period at every zoom. `f` is computed on the CPU in double and wrapped, so the shader never sees a large
  time. The clock is real time, independent of time warp, and stops while the game is paused (D15).
- `LaneDashCoverage(s, f, direction, footprint)` mirrors `MultiScaleDashCoverage` with the flow applied per level; with
  `f = 0` it equals `MultiScaleDashCoverage(s x DASH_BASE_COUNT, ...)` exactly (a test pins this).

### 4.7 Look, colour and HUD glow (D2, D5)

- **Colours:** `NPC_LANE_OUTBOUND_COLOR` = sRGB #9FD6D0 (teal-white), `NPC_LANE_INBOUND_COLOR` = sRGB #3FD0E0 (cyan),
  stored as `FColor` and converted to linear once. No `HUD_COLOR_GAIN` (that gain clips cores toward white); the
  emissive scale (`NPC_LANE_EMISSIVE`, tuned on screenshots) is chosen so the brightest lane core keeps its hue after
  tone mapping (checked on the screenshots, section 8).
- **Profile across the line** `LaneLineProfile(dPx, width, g, height)`: the HUD glow profile of SDD 37
  (`GlowProfile(d', g)`, core plus a halo of weight `HUD_GLOW_HALO_WEIGHT x g`) shifted out by the line's half width,
  its distance past the core in reference pixels (D25: divided by the viewport scale `height / 1080`), times
  `min(width, 1)` for sub-pixel lines. So at `g = 0` a lane is a crisp line with no halo, it glows like the HUD at the
  player's HUD glow level (D5), and the glow covers the same share of the screen at every resolution.
- **Glow feed:** `ASOLNpcVisuals` keeps an `FSOLGlowScaleFeed` and pushes `GlowScale` into the lane material only when
  the live `HudGlow` changes (so the Settings slider previews at once, as for the HUD). The "HUD and menu effects" and
  "Hologram menu effects" sliders do not affect lanes.
- **Ribbon half extent** `= max(width, 1) / 2 + NPC_LANE_GLOW_PAD_PX` at 1080p (12 px): the halo at the largest
  reachable `g` (0.6) is under 1% there, so the 32 px HUD pad is not needed and the pixel cost stays low. At other
  heights (D25) the edge sits at the same point of the scaled glow:
  `max(width, 1) / 2 - HUD_GLOW_CORE_HALF_PX + (NPC_LANE_GLOW_PAD_PX + HUD_GLOW_CORE_HALF_PX) x scale` (never less
  than `max(width, 1) / 2`).

### 4.8 Renderer (`ASOLNpcVisuals`, `M_SOLNpcLanes`)

- **Mesh, built once per population** (`BuildLaneMesh`, like `BuildPointBlockMesh`): per drawn lane, per line (out,
  in), `NPC_LANE_SEGMENTS + 1` samples (proposed 64 segments), two vertices each (side -1 / +1). UV0 = (sample
  parameter `u`, side), UV1 = (lane group index, line index). Positions are zero; bounds cover all render space (never
  culled). 8 lanes x 2 lines x 65 x 2 = 2,080 vertices. One `UStaticMeshComponent` (`LaneLines`, no shadow, no
  collision, no ray tracing, `TranslucencySortPriority` just above the far points).
- **Vertex shader** (`Shaders/Private/SOLNpcLane.ush`, mirrored by `SOLNpcLaneMath`): reads A and B (viewpoint
  relative) and the lane's `.w` data from `NpcGroupTex`; a lane with weight -1 returns the hidden offset. The sample
  point `A + u (B - A)` is placed with the far-depth compression (`PlaceLinePointCm`, so it lands on the same pixel as
  the true point), expanded along the ribbon side direction by the line's centre offset plus the ribbon half extent.
  A straight lane projects to a straight screen line, and every placed vertex lies on its true ray, so the chords stay
  on that line at any segment count; the segments only serve clipping and the ribbon side direction.
- **Pixel shader:** the lane parameter `s` and the true distance under the pixel are computed **analytically** from the
  pixel's view ray and the segment A-B (`ClosestLaneParam`), not interpolated, so the fade and the dash phase are
  exact even where a long chord passes near the camera. Output: `colour x brightness x LaneLineProfile x
  LaneDashCoverage x LaneLineFade x (1 - occluded)` (additive, unlit, translucent, depth test off like the orbit lines,
  so the occlusion is the analytic sphere test plus the orbit lines' 1:1-zone scene-depth rule).
- **Occluders (D24):** the lane material reads the orbit lines' parameter collection (`OrbitLine*`, 8 spheres and the
  scene-depth trust limit). `ASOLOrbitLineVisuals` today skips writing them while its lines are hidden; its occluder
  update is changed to run whenever orbit lines **or** lanes are drawn (one bool the lane renderer publishes, or the
  occluder update moved into a small shared helper both call; implementer's choice within that rule).
- **Per frame (CPU):** `UpdateFarPoints` writes the `.w` lane data with the group rows; the point material gets one
  more vector parameter `NpcParams4` = (lanes shown 0/1, fade near radii, fade far radii, 0), the lane material one
  `NpcLaneParams0` = (flow phase `f`, viewport-height scale, fade near radii, fade far radii) (the colours, widths and
  dash constants are set once at `BeginPlay`); `GlowScale` only on change. The
  lane component's visibility follows (row shown and at least one drawn lane); with no population nothing changes from
  today. Reads `USOLSettingsSubsystem::IsOverlayShown(EOverlay::Lanes)` and the live `HudGlow` (D23: a new dashed
  `Npc -> Menu` dependency, like `Ship -> Menu` and `UI -> Menu`); without a settings subsystem the lanes are on at 100%
  glow. A verification override (`BeginLaneVerificationOverride`, like `ASOLOrbitLineVisuals::BeginVerificationOverride`)
  lets smokes toggle lanes without reading or writing the player's settings.
- **Assets:** `M_SOLNpcLanes` added to `Tools/Npc/create_npc_assets.py` (rebuilt with `-SOLRebuild`, editor closed),
  path `SOL::Paths::NPC_LANES_MATERIAL`; `M_SOLNpcPoints` gains the crossfade (kind 1 only) and the `NpcParams4` input.
- **Developer switch (D20):** cvar `sol.NpcLanes 0|1` (default 1) hides the lane line and restores the points, for A/B
  profiling, like `sol.OrbitLines`.

### 4.9 Overlay row and persistence (D1, D5, D11, D17)

- **Table:** new `EOverlay::Lanes`, id `"Lanes"`, label `"Lanes"`, default **on**, top level (parent `INDEX_NONE`),
  placed after "Ships on Radar" and before "Darkening" (D17; `OVERLAY_COUNT` 15 -> 16, Darkening moves
  to index 15 and stays last). The Overlay Menu and its Settings copy show the row with no widget code change
  (rows come from the table; `ROW_WIDGET_COUNT` follows). The master O governs it like every row (D11).
- **Write points unchanged:** a click or Space on the row only changes the applied prefs in memory
  (`SetAppliedOverlayPrefs`); the file is written once at the Overlay Menu's close, the Settings `EndVisit`, the pawn's
  `EndPlay` or shutdown, and only if a write is owed (`IsSaveOwed`: the prefs differ from what is on disk).
- **Schema 13 (D1).** Exactly these persisted changes, nothing else:

  | What | Change |
  |---|---|
  | `SOL::Settings::SAVE_VERSION` | 12 -> **13**; its comment gains "13: the Lanes overlay row written into every used profile, SDD 38" |
  | `SOL::Settings::KEY_BINDING_LAYOUT_VERSION` | **unchanged, 10** (no binding; `DefaultLayoutChangedSince(12)` stays false: no controls-changed notice) |
  | `FSOLOverlayProfileSave` / `USOLSettingsSave` UPROPERTYs | **none added or removed** |
  | Profile row maps (`FSOLOverlayProfileSave::Overlays`) | gain the key `"Lanes"` (bool), written by `ToSavedProfile` like every row |
  | Migration step 12 -> 13 | For every stored profile entry whose row map is **not empty** (a used slot): add `"Lanes" = true` if the key is missing; an existing value is kept. Entries with an **empty** map (unused slots) are left empty, so they stay unused (adding a key would silently turn them into used profiles). Entries are not padded. Every other key and value, `bEnabled`, `bMasterOn`, `bOrbitLinesOn`, `ActiveOverlayProfile`, the key overrides and every other field are kept as loaded. `Version = 13`. Frozen: the id is the literal `"Lanes"`, as in the 8 -> 9 step |

  Net effect: the Lanes row is on in every profile (used slots by the migration; unused slots and newly enabled
  profiles through the table default and `Sanitized` / the first-enable copy). A migrated file is written back once at
  the next write point (existing `mIsSaveOwed`); an older build that meets a v13 file ignores it
  (`ClassifySaveVersion`, existing behaviour). Note: the frozen 6 -> 7 step builds its profile from the live
  `DefaultMask()`, so a v6-or-older chain already holds `"Lanes"` before 12 -> 13 (the keep-existing rule covers it).

### 4.10 Extension point for #49 (not designed here)

#38 ends at the close-up fade. It leaves, for the tunnel ticket: (a) `FSOLNpcLaneFrame` (per-lane endpoints, radius,
count, weight, drawn flag) built each frame and exposed by `ASOLNpcVisuals::GetLaneFrame()`; (b) the pure
`ClosestLaneParam` / `LaneLineFade` functions, whose near band (inside `FADE_NEAR x R`) is where a tunnel would take
over; (c) the Lanes row as the switch the tunnel can also obey. Tunnel geometry, trigger distance and its relation to
the profiles and glow are #49's grill-me.

### 4.11 What does not change

Rails, tiers, budgets, the simulation and the stats (D6); cluster and battle ship points; CPU points, instanced and
meshed hulls; NPC combat; orbit lines' look; the HUD; key bindings and the key layout version; every Settings page
control (the embedded Overlays tab gains the row from the table).

## 5. Map, route overlay and other views (D18)

The lanes show on the hologram map too, following the same row; the HUD-drawn route overlay is drawn over them (it is
screen space). SDD 20 hides some real actors in the map view: the implementer first checks whether `ASOLNpcVisuals`
(or just its lane component) is hidden there today and, if so, keeps the lane component visible on the map (the far
points may stay as they are there). The finding and the result are recorded here. Nothing changes in the main menu (no
NPCs exist there).

## 6. Contract for the test author

Pure functions and plain data only. Names are proposals the implementer keeps unless a reason is reported back.
Tolerance 1e-9 for doubles unless stated. Constants are referenced by name, never hard-coded in the tests, except
where a case says "proposed value" (then the test also asserts the constant's value, so a tuning change is a visible
test edit).

**New file `Tests/NpcLaneMathTest.cpp` (`SOLTest.NpcLaneMath.*`), `Npc/SOLNpcLaneMath.h`, namespace `SOLNpcLane`:**

1. **Constants** (`SOLConstants.h`, proposed values): `NPC_LANE_FADE_NEAR_RADII` 100, `NPC_LANE_FADE_FAR_RADII` 400
   (far > near > 1), `NPC_LANE_TRAFFIC_LOW` 100, `NPC_LANE_TRAFFIC_HIGH` 100,000, `NPC_LANE_WIDTH_MIN_PX` 1.0,
   `NPC_LANE_WIDTH_MAX_PX` 2.5, `NPC_LANE_GAP_PX` 1.5, `NPC_LANE_REFERENCE_HEIGHT_PX` 1080, `NPC_LANE_BRIGHTNESS_MIN`
   0.35, `NPC_LANE_BRIGHTNESS_MAX` 1.0, `NPC_LANE_DASH_BASE_COUNT` 64, `NPC_LANE_DASH_DUTY` 0.5,
   `NPC_LANE_DASH_FLOW_PERIODS_PER_S` 0.75, `NPC_LANE_GLOW_PAD_PX` 12, `NPC_LANE_SEGMENTS` 64;
   `NPC_LANE_OUTBOUND_COLOR == FColor(0x9F, 0xD6, 0xD0)`, `NPC_LANE_INBOUND_COLOR == FColor(0x3F, 0xD0, 0xE0)`, and
   neither is grey or white (max channel minus min channel >= 0.1 in linear).
2. **`double LaneLineFade(double distanceM, double laneRadiusM)`**: 0 at and below `NEAR x R` (also for d = 0 and
   negative d); 1 at and above `FAR x R` and for +inf; exactly 0.5 at `sqrt(NEAR x FAR) x R` (200 R with the proposed
   values); linear in `log d` (check 3 interior points against the closed form); non-decreasing in d; independent of
   scale (`fade(k d, k R) == fade(d, R)` for k = 1e3); NaN distance -> 1; NaN, 0, negative or infinite radius -> 1 (no
   close-up band, the line stays).
3. **`double LaneShipPointAlpha(double distanceM, double laneRadiusM, bool bLanesShown)`**: shown -> `1 -
   LaneLineFade(d, R)` (so the sum with the fade is 1 at every sampled d); not shown -> 1 at every d (today's look).
4. **`double LaneTrafficWeight(int32 shipCount)`** and **`bool IsLaneDrawn(int32 shipCount)`**: count <= 0 -> weight
   0 and not drawn; 1 and 100 -> 0 (drawn); 1,000 -> 1/3; 10,000 -> 2/3 (1e-9); 100,000 and 1,000,000 -> 1;
   non-decreasing; `IsLaneDrawn` true for any count >= 1.
5. **`double LaneLineWidthPx(double weight, double viewportHeightPx)`**: `lerp(WIDTH_MIN, WIDTH_MAX, clamp(w, 0, 1))
   x height / REFERENCE_HEIGHT`; at 1080: w 0 -> 1.0, 1 -> 2.5, 0.5 -> 1.75; w -1 -> 1.0, w 3 -> 2.5, NaN w -> as 0;
   height 720 -> x 2/3, 2160 -> x 2; height 0, negative or NaN -> the reference height.
6. **`double LaneLineBrightness(double weight)`**: `lerp(BRIGHTNESS_MIN, BRIGHTNESS_MAX, clamp(w))`; NaN -> MIN;
   non-decreasing.
7. **`double LaneLineCenterOffsetPx(double widthPx, double viewportHeightPx)`**: `0.5 x (widthPx + GAP x height /
   REFERENCE)`; for every width in {0.5, 1, 2.5, 5} the two lines' cores do not overlap (`2 x offset - widthPx >=
   GAP x scale - 1e-12`); bad height as in case 5.
8. **`double LaneRibbonHalfExtentPx(double widthPx, double viewportHeightPx)`** (D25): with `scale = height /
   REFERENCE` (a bad height as in case 5), `max(max(width, 1) / 2, max(width, 1) / 2 - HUD_GLOW_CORE_HALF_PX +
   (GLOW_PAD + HUD_GLOW_CORE_HALF_PX) x scale)`; at 1080 exactly `max(width, 1) / 2 + GLOW_PAD`; always
   >= `max(width, 1) / 2`.
9. **`double LaneLineProfile(double distanceFromCenterPx, double widthPx, double glowScale, double
   viewportHeightPx)`** (D25): with `scale = height / REFERENCE` (a bad height as in case 5) and
   `d' = max(0, |d| - (max(width, 1) / 2 - HUD_GLOW_CORE_HALF_PX)) / scale`, result
   `= SOLHudShapeMath::GlowProfile(d', g) x min(width, 1)` (g sanitized as `SanitizeGlowScale`); 0 for a non-finite d
   or a non-finite or non-positive width. At 1080: at d = 0 -> `min(width, 1)` for every g in {0, 0.3, 1,
   `HUD_GLOW_SCALE_MAX`}; symmetric in d; non-increasing in |d|; non-decreasing in g only from just past the core edge
   `max(width, 1) / 2` outward (measured: from about 0.04 px past it for every g in [0, 2]; closer in, the halo's
   weight does not yet make up for the core weight it takes; the test samples from 0.25 px past the edge); at `g = 0` below 0.01 from 1 px past the core edge; below 0.01 at
   `|d| = max(width, 1) / 2 + GLOW_PAD` (measured from the line's own centre, the 1080p pad) for every g up to
   `2 x HUD_GLOW_DEFAULT_LEVEL` (the largest g the HUD glow setting can reach); width 1 and g = 1 equals
   `GlowProfile(|d|, 1)` exactly. At 720 and 2160: the closed form; `min(width, 1)` at the centre; resolution
   independence (the value at `max(width, 1) / 2 - CORE_HALF + x x scale` equals the 1080 value at
   `max(width, 1) / 2 - CORE_HALF + x` for the same width); below 0.01 at `LaneRibbonHalfExtentPx(width, height)` for
   every g up to `2 x HUD_GLOW_DEFAULT_LEVEL`.
10. **`double LaneDashFlowPhase(double timeS)`**: `frac(FLOW x t)` in [0, 1), computed in double; t = 0 -> 0;
    t = 1 / FLOW -> within 1e-9 of 0 modulo 1 (the result itself always < 1); t = 0.5 / FLOW -> 0.5; t = 1e9 s stays in [0, 1) and equals the
    closed form within 1e-6; negative t -> in [0, 1) (`frac` of a negative number wraps up); NaN or inf -> 0.
11. **`double LaneDashPhase(double laneParam, int32 level, double flowPhase, int32 direction)`**:
    `laneParam x DASH_BASE_COUNT x 2^level - sign(direction) x flowPhase`, level clamped to
    [0, `ORBIT_LINE_DASH_MAX_LEVEL`], direction 0 treated as +1. Motion check: for outbound, the phase at
    `(s + DASH_BASE_COUNT^-1 x 2^-level x df, f + df)` equals the phase at `(s, f)` (a dash feature moves toward B, s
    increasing, as f grows); for inbound the feature moves toward A (s decreasing). Both directions give the same
    phase at f = 0.
12. **`double LaneDashCoverage(double laneParam, double flowPhase, int32 direction, double phaseFootprint)`**: with
    `flowPhase == 0` it equals `SOLOrbitLineMath::MultiScaleDashCoverage(laneParam x DASH_BASE_COUNT, DASH_DUTY,
    phaseFootprint, <the orbit lines' level and solid constants>)` for a sweep of 20 (s, footprint) pairs covering
    level 0, a crossfade, the top level and the solid fade; in [0, 1] always; NaN or non-positive footprint -> a
    level-0 point sample of `LaneDashPhase(s, 0, f, dir)` (a square wave of duty `DASH_DUTY`); shifting f by a whole
    period (1.0) leaves the coverage unchanged; inside a pure level-k regime, shifting f by `delta` equals shifting the
    level-k phase by `delta` (same value as the point sample at the shifted phase).
13. **`double ClosestLaneParam(const FVector3d& aRelM, const FVector3d& bRelM, const FVector3d& rayDirection)`**: the
    parameter in [0, 1] of the point on segment A-B (camera at the origin) closest to the ray from the origin along
    `rayDirection` (normalized inside). Cases: A = (1e9, -1e9, 0), B = (1e9, 1e9, 0), ray (1, 0, 0) -> 0.5; ray toward
    B -> 1 (within 1e-6); ray pointing away from the segment (the closest ray point is the origin) -> the parameter
    of the segment point nearest the origin; ray parallel to A-B -> the parameter of the segment point nearest the
    origin, clamped; degenerate segment (A == B) -> 0; zero or NaN ray, NaN endpoint -> 0; true solar-system scale
    (A at 1 AU, B at 1.5 AU) keeps 1e-9 relative accuracy in double. And **`double LaneDistanceAtParamM(aRelM, bRelM,
    s)`** = `|A + s (B - A)|`, s clamped to [0, 1].
14. **`FVector2D LaneOutboundSideNormal(const FVector2D& screenA, const FVector2D& screenB)`** (screen x right, y
    down): the unit normal on the right of the travel direction A -> B; A -> B = (+1, 0) -> (0, +1); (0, -1) -> (+1, 0);
    (-1, 0) -> (0, -1); scaled inputs give the same unit result; A == B or NaN -> (0, 0). The inbound line uses the
    negated normal (case asserts `LaneOutboundSideNormal(B, A) == -LaneOutboundSideNormal(A, B)`).
15. **`void CountLaneShips(TConstArrayView<FSOLNpcRail> rails, int32 laneCount, TArray<int32>& outCounts)`**:
    `outCounts` reset to `laneCount` zeros (none for laneCount <= 0); rails with `Kind == EPlacementKind::Lane` and
    `Group < laneCount` are counted per `Group`; battle and cluster rails and lane rails with an out-of-range group are
    ignored; an empty rail array gives all zeros; counting the same rails twice (fresh call) gives the same result.
16. **`FSOLNpcLaneFrame` and `void BuildLaneFrame(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame,
    TConstArrayView<int32> counts, FSOLNpcLaneFrame& outFrame)`**: one entry per lane (`groups.LaneCount`); entry i
    has `A == frame.GroupPointAM[i]`, `B == frame.GroupPointBM[i]`, `RadiusM == groups.LaneRadiusM[i]`,
    `ShipCount == counts[i]`, `TrafficWeight == LaneTrafficWeight(counts[i])`, `bDrawn == IsLaneDrawn(counts[i])` and
    both bodies resolved (`LaneBodyA/B[i] != INDEX_NONE`); a lane past the end of `counts` or of the frame arrays is not
    drawn (count 0); calling it twice with the same sizes does not reallocate (`outFrame`'s array data pointer
    unchanged).

**Overlay model (`OverlayModelTest.cpp`; re-validate every existing case):**

17. `OVERLAY_COUNT == 16`; `EOverlay::Lanes` has id `"Lanes"`, label `"Lanes"`, default on, parent `INDEX_NONE`; it
    sits after `ShipsOnRadar` and before `Darkening` (index 14; Darkening index 15 and still the last row, D17);
    `FindById("lanes")` finds it;
    `DefaultMask()` and every mask of `MakeDefaultPrefs()` include it; `ToSavedProfile` writes 16 keys including
    `"Lanes"`; `FromSavedProfile` of a 15-key map without `"Lanes"` (a v12 profile) -> Lanes on and every other row as
    in the map; `{"Lanes": false}` -> Lanes off, the rest default; `IsShown(Lanes)` false with the active profile's
    master off and true with it on and the row checked; toggling Lanes in profile 2 leaves profile 1's bit;
    `SetProfileEnabled`'s first enable copies profile 1's Lanes bit; `IsOrbitOverlay(Lanes)` and `IsOrbitShown(prefs,
    Lanes)` false. The existing "fifteen rows" and "Darkening at index 14" assertions are updated, not deleted.

**Settings and migration (`SOLSettingsRulesTest.cpp`, `SOLSettingsModelTest.cpp`, `FlightModeMathTest.cpp`):**

18. `SAVE_VERSION == 13`; `KEY_BINDING_LAYOUT_VERSION == 10`; `ClassifySaveVersion(12, 13) == Migrate`, `(13, 13) ==
    Use`, `(14, 13) == UseDefaults`; `DefaultLayoutChangedSince(12) == false` (and the existing cases for 1-9 true,
    10-11 false unchanged).
19. **Full v12 case:** a v12 save with every scalar field non-default (volumes, sensitivity, invert, HUD scale and
    visibility, assist, vsync and quality with their chosen flags, HUD effects 0.3, hologram menu effects 1.7, HUD glow
    0.35, steer at start, chase margin, space dust off), five profile entries: 0 enabled with a custom 15-key map, 1
    unused (empty map, disabled), 2 enabled with a custom map, master off and orbit lines off, 3 disabled but used
    (non-empty map), 4 unused; active profile 2; two key overrides. After `MigrateToCurrentVersion`: `Version == 13`;
    entries 0, 2 and 3 each hold `"Lanes" == true` plus every original key with its original value; entries 1 and 4
    still have **empty** maps; every `bEnabled`, `bMasterOn`, `bOrbitLinesOn`, the active profile, the key overrides
    and every scalar field identical; `GetValues()` then gives `UsedProfiles` = {0, 2, 3} (unchanged by the
    migration) and Lanes enabled in profiles 0, 2 and 3.
20. A v12 entry that already holds `"Lanes" == false` keeps false; a v12 save with **no** profile entries gains none;
    entries are not padded to five.
21. **Chains:** v1, v6, v9, v10 and v11 saves migrate to 13 with `"Lanes" == true` in every non-empty profile map and
    every earlier step's result unchanged (the existing chain tests' expected final version moves from 12 to 13; the
    v11 chain still ends with `HudGlow == 1.0`).
22. A v13 save is left alone by `MigrateToCurrentVersion`; `SetValues` / `GetValues` round-trip Lanes off in profile 0
    and on in enabled profile 2, and unused slots stay empty maps.
23. `SOLSettingsRules::IsSaveOwed`: toggling Lanes in the applied prefs owes a write against the written prefs;
    toggling it back does not (`AreEqual`); toggling Lanes in a used, disabled profile also owes one.

**Text (`SOLTest.InfoText.*`, `SOLTest.PlayerGuide.UpToDate`):**

24. The Info prose and the `@OverlayMenu` Controls row contain the lane wording of section 7 with every `{Token}`
    resolving to a binding (existing token check); the generated player guide is regenerated and up to date.

Not unit-testable (checked by smoke and screenshots, section 8): the two materials, the ribbon placement, the
crossfade on screen, the occlusion, the colours after tone mapping.

## 7. Player-facing text and docs

Wording (D19):

- **Overlay Menu row:** `[x] Lanes`.
- **Controls, `@OverlayMenu` row:** "... (icons per kind, orbits per kind, ships on radar, NPC lanes, darkening); also
  in Settings".
- **Info, overlay paragraph** (after the orbit-lines sentence): "With NPC ships out (the Demo page), each lane between
  two bodies is drawn as a pair of dashed hologram lines, teal-white for traffic leaving the inner body and cyan for
  traffic coming back, the dashes flowing each way; a busier lane is wider and brighter. Close to a lane the lines fade
  out and its ships show one by one. The Lanes row of the Overlay Menu ({OverlayMenu}) turns them off (the ships then
  show as points, as before); the lines glow with the HUD glow setting."
- **Info, Demo paragraph:** "... farther ones follow simpler motion and are drawn as points, or as lane lines (see the
  overlays)."
- **Help glossary:** no new entry (D19; the glossary pictures HUD symbols and five map crops).
- `Docs/GAME_MECHANICS.md`: a new "Lanes" row in the overlay table (default on, per profile, master O, schema 13), a
  new "Lane lines" row in section 9 (every tunable of section 6 case 1, the fade, mapping, dashes, colours, glow, side
  rule, `sol.NpcLanes`), the Render tiers row (lane ship far points cross-faded), the Settings schema note.
- `Docs/PLAYER_GUIDE.md` regenerated with `Tools\WritePlayerGuide.bat` (the Controls and Info text change).

Doc amendments in the same change set: SDD 11 Amendment 8 (lane lines and the point crossfade, measured), SDD 22 and
SDD 34 a one-line note each (the Lanes row, schema 13), SDD 17 a note (the shared occluders and shader library),
`Docs/ARCHITECTURE.md` (`Npc/` row: `SOLNpcLaneMath`, the lane renderer, `M_SOLNpcLanes`, `SOLNpcLane.ush`; the
dashed `Npc -> Menu` edge in the module graph and its sentence; `OrbitLines/` occluder note; settings schema 13; the
new `-SOLSmokeNpcLanes` flag and `sol.NpcLanes` cvar), `Docs/Plans/STATUS.md` (#38 in progress, then done),
`Docs/TECH_DEBT.md` (new debt found; the existing items in section 9 are not re-reported).

## 8. Verification

1. **Tests:** the section 6 contract (TDD, written by a separate agent from this contract only, red for the right
   reason first), then the full suite (`Tools\RunTests.bat`). Re-validated: `OverlayModelTest.cpp` (row count and
   Darkening index), every "schema 12" assertion in `SOLSettingsRulesTest.cpp` and `FlightModeMathTest.cpp`, the
   migration chain tests, `NpcScaleMath` / `NpcRail` (unchanged contract), `OrbitLinePacking` / `OrbitLineLogic` if the
   occluder update moves.
2. **New smoke `-SOLSmokeNpcLanes`** (D20; 1M NPCs, fixed seed, lane verification override so the player's settings
   are neither read nor written; HUD glow set as a pending value, never applied, as in SDD 37 D14). Views: V1 the
   default spawn 10,000 km above Earth (Earth-Moon and Earth-Mars lanes: crossfade near Earth, lines toward the Moon
   and Mars); V2 0.15 AU off Venus (SDD 11's converging-lanes view); V3 3 AU above the Sun (all 8 lanes); V4 inside
   the Earth-Mars lane halfway (points around the camera, lines both ways); V5 a lane passing behind a planet
   (occlusion). Per view: Lanes on and off, HUD glow 0% / 100% / 200%. Checks (logged PASS / FAIL): the lane
   component's visibility equals (row shown and a drawn lane); every drawn lane's group-texture `.w` values equal
   `BuildLaneFrame`'s radius and weight; the points material's lanes flag follows the row; the pushed glow scale is
   `HudGlowScale(live)` and returns on revert; the flow phase advances between frames and not while paused;
   `sol.NpcLanes 0` hides the lines and restores the points.
3. **Screenshots** of V1-V5 at **1280x720, 1920x1080 and 3840x2160 (4K proxy)**, looked at: two parallel lines in the
   two colours, never white, dashes flowing opposite ways, the outbound line on the right of A -> B, busy lanes
   wider and brighter, the line fading near the camera while the points appear (no bright seam or gap in the
   crossfade), the lane hidden behind planets, 0% glow crisp, 200% softer, no shimmer of the dashes while the camera
   moves. Tuning (fade radii, widths, brightness, emissive, flow, dash count) is done on these shots and recorded in an
   amendment; the candidate shots are deleted, 2-3 kept for the README gallery.
4. **Existing smokes:** `-SOLSmokeOverlay` (smoke slot, real key events) gains: the Lanes row exists in every
   profile, Space on it toggles `IsOverlayShown(Lanes)` and the lane component, O master off hides the lanes, profile
   cycling restores each profile's own Lanes bit, and closing the menu writes once; `-SOLSmokeHoloMenu` and
   `-SOLSmokeSolidMenu` (the Ctrl+O menu and the Settings Overlays tab with one more row fit at 1280x720 and
   1920x1080, TECH_DEBT's framed-padding item in mind); `-SOLSmokeHud` unchanged.
5. **Persistence:** a v12 settings file in the smoke slot loads with Lanes on in every profile, its unused slots stay
   unused, and it is written back once as v13 at the next write point with every other value intact; toggling the
   Lanes row twice and closing writes nothing.
6. **PIE smoke check** via unreal-mcp when the editor is open (Demo 1M, Ctrl+O Lanes off / on, HUD glow slider); noted
   in the plan if it could not be run.
7. **Profiling** (CLAUDE.md review items 9 and 11, `VR_PERFORMANCE_TARGETS.md` sections 6-7a): `ProfileGPU` (the
   translucency pass and the two materials) and `stat unit` / Insights (`ASOLNpcVisuals::Redraw`,
   `UploadPopulation`) in standalone `-game`, vsync off, `-corelimit=16`, 1M NPCs, views V2 and V3, with `sol.NpcLanes`
   0 and 1, three runs each (medians), at 1920x1080, 3840x2160 and one XR-4 eye (3840x3744; two eyes skipped as in
   #33 / #37 unless the user asks). Before (the commit before #38) and after. The 1M `-SOLNpcRamp` must still PASS
   every stage. `-SOLSmokeHoloMenuProfile` P2 (the Overlay Menu with one more row) at the same three sizes (D21).

Temporary screenshots and captures are deleted once looked at; only README gallery shots are kept.

## 9. Performance

- **CPU per frame:** O(lanes), independent of the ship count: 8 lane entries (`BuildLaneFrame`) and 16 more floats in
  the group texels already written and uploaded, one vector parameter for each of the two materials, `GlowScale` only
  on a change, one wrapped double for the flow. No allocation (arrays sized per population), no new tick, no new
  UObject per frame. **Per population:** the lane-count histogram inside the existing rail-fill `ParallelFor` (per-task
  counts) and one small mesh build (about 2,000 vertices). Budget: `ASOLNpcVisuals::Redraw` +0.02 ms or less at 1M.
- **GPU:** the lane ribbons are ~2,000 vertices; the pixel cost is the ribbon area: at 1080p with all 8 lanes across
  the screen about 8 x 2 x 2,000 px x 2 x 13.25 px ~ 0.85 Mpx of a cheap additive shader (closest-point math, one log
  for the fade, two dash levels, the two-exp glow profile, up to 8 ray-sphere tests). Expected well under 0.1 ms at
  1080p, scaling with pixels at 4K and one eye. The far layer gets cheaper: lane ships beyond `FADE_FAR x R` return
  the hidden offset after the rail evaluation (vertex cost unchanged, their raster and pixel cost gone; ~40% of the
  points at the default lane fraction). Budgets (D22, gated at 1920x1080 only): lanes <= 0.15 ms GPU at 1920x1080 in V3 with 1M; NPC
  translucency (points + lanes) no more than +0.05 ms over the before figure at 1920x1080; recorded (not gated) at 4K
  and one eye, where the whole frame is already scene-bound (`VR_PERFORMANCE_TARGETS.md` section 7a).
- **1M scaling:** nothing in the lane path is per ship on the CPU; per ship on the GPU it is one branch and one log
  in the far-point vertex shader for lane ships only.
- **VR (record only, no VR work before procedural terrain):** lanes are world-space geometry, so they render per eye
  with correct depth and need no HUD layer; the per-eye cost is the ribbon area. The Overlay Menu's one-eye cost was
  1.152 ms (96% of the 1.2 ms ceiling, SDD 37 section 8); one more row may cross it; if so it is recorded, not fixed (D21).
- **Known debt not to re-report:** `Docs/TECH_DEBT.md` "NPC worst case with every Demo option at its maximum", the
  render-thread spikes of the 10f soak, the Overlay Menu's framed paddings, the embedded Overlay list's full-row
  refresh, the speed panel's one-eye cost, and `ASOLOrbitLineVisuals`' drift rebuilds.

## 10. Open questions

None. Q1-Q18 of the first draft were answered by the user on 2026-11-11 (D7-D24, section 2). Alternatives considered
and rejected with them, for the record: the HUD glow-shape batch as the lane renderer (no planet occlusion or depth,
per-frame CPU projection and clipping, no depth in a future VR stereo layer) and Niagara ribbons (not authorable from
editor Python, SDD 11 Amendment 2) (D7); trimming the lanes at the bodies' surfaces (D8); hiding every far lane ship
while lanes are on, which leaves an empty band near a lane (D9); lane ships hidden with Lanes off (D10); lanes
ignoring the master O (D11); fade thresholds on the lane's apparent pixel width (D12); expected or spawn-bucket ship
counts (D13); fixed pixel widths (D14); screen-space dashes, real-speed dashes, a clock running while paused (D15); an
appended row or the label "NPC Lanes" (D17); lanes hidden on the map (D18); a Help glossary entry (D19); no new smoke
flag or cvar (D20); compacting the Overlay Menu in #38 (D21); a push of the settings from the game mode (D23); a
separate occluder set for the lanes (D24).
## 11. Risks

- The crossfade's two halves (line and points) have different looks (smooth line vs dots); the screenshots may show a
  band where the lane looks dimmer or busier. Lever: the fade radii and the line brightness.
- Additive cores can tone-map toward white (D2 forbids white): watched in the screenshots (D16).
- Multi-scale levels flowing at different on-screen speeds during a level crossfade may shimmer slightly; lever: the
  flow speed or a narrower crossfade.
- The Overlay Menu with one more row may come closer to the framed paddings at 720p (TECH_DEBT) and to the one-eye
  ceiling (recorded per D21).
- `M_SOLNpcPoints` and `M_SOLNpcLanes` HLSL mirror `SOLNpcLaneMath`; a change to one must change the other (noted in
  both file headers and `ARCHITECTURE.md`, as for the rails).

## 12. Change log

- 2026-11-11: first draft from grill-me D1-D6; open questions Q1-Q18 reported to the main session.
- 2026-11-11: Q1-Q18 answered by the user (D7-D24, all as recommended; D9 states the crossfade so a lane is never empty, D22 gates the budgets at 1920x1080 only); D2 gains the keep-right rule shared with #49 (outbound on the right, inbound on the left); section 5 (map) and section 10 updated; ready to build.
- 2026-11-11: part 38a built; after its review the user decided D25 (the glow scaled with the viewport height: cases 8 and 9 updated, 720p and 4K cases added), D23 gained the `Npc -> UI` and `Npc -> OrbitLines` edges of the pure math, and case 9's "non-decreasing in g" was narrowed to just past the core edge.
