<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 52: Lane tunnel far field

**Issue:** [#52](https://github.com/DieSlower/SOLTest/issues/52). Plan:
[`Plans/52-lane-tunnel-far-field-plan.md`](../Plans/52-lane-tunnel-far-field-plan.md). Builds on SDD 38 (the NPC lane
lines and the line / point crossfade), SDD 49 (the guidance tunnel: frame, slots, fades, end rings, labels, renderer)
and SDD 50 (D23 guide thinning, the merge readout in the tunnel label, the section 15 profile baseline).
**Persistence change: none planned.** `SOL::Settings::SAVE_VERSION` stays 13 and `KEY_BINDING_LAYOUT_VERSION` stays 10.
No setting, key binding or `UPROPERTY` of a save class changes. If the work shows that one is needed, stop and report.
Status: **parts 52a (pure math, flat lattice and fades) and 52b (shader, component, lane line, mirror) implemented
2026-11-18, both uncommitted, with the 52b review fixes applied (section 12); 52c (tuning, profile, gallery, review)
next.
Grill-me done (D1-D5), questions Q1-Q14 answered by the user (D6-D19, 2026-11-18). The end-ring floor point is closed:
the user chose to rebuild `M_SOLNpcTunnel.uasset` with a proper new parameter (D16 amended). The vertex budget is
interim and revisited in #53 (D15).** The shader is back on the C++ math, so the "HLSL mirrors the C++" rule
is in force again (`SOLNpcTunnelMirror.h` and its tests pin it).

## 1. Problem

Today a lane has two looks: far away (beyond 100 lane radii) the two dashed lines of #38, and close up (inside 100 to
50 lane radii) the #49 tunnel of rings, guide dots and wall chevrons. The two differ in style, the hand-over band is a
visible change of language, and the dashed lines do not show the lane the way the tunnel does (rings, direction, end
rings). Since the #50 guide thinning (SDD 50 D23) the near tunnel already keeps one ring every 12 tunnel radii. Issue
#52 asks for the tunnel's own vocabulary at every distance: the same rings seen from inside, dots and chevrons at every
ring and halfway between, and a re-check of the sparse approach view after the thinning.

## 2. Decisions (user, grill-me 2026-11-18)

- **D1: Far field.** The dashed lane lines (#38) are **removed in the 3D view**; the **map view keeps its lines**.
  Rings, dots and chevrons carry the lane **at all distances**: the tunnel is no longer limited to the 100 -> 50 lane
  radii band (SDD 49 D1).
- **D2: Ring selection, option A, "rings that matter on screen".** **Flat 12 tunnel radii spacing everywhere**: it
  replaces the accordion (SDD 49 section 4.5) near the player too; the D23 every-6th-ring density equals this flat
  spacing. The rings are drawn as a **moving window of rings along the lane**: only rings whose **on-screen gap is
  large enough** and that are **near the view**, a **fixed vertex budget per lane**, **recentred as the player moves**.
  *Honest consequence (accepted by the user):* a real lane is 2,000 to 750,000 tunnel radii long (Earth-Moon 1,920
  tunnel radii empty / 640 busiest; Earth-Mars 18,600-133,000 / 6,200-44,400; Jupiter-Saturn 81,000-275,000;
  Uranus-Neptune about 757,000), so from very far away only the end rings show, nothing between them.
- **D3: Fade rule** (starting values; the curve is fixed by D9). Rings vanish by their **on-screen ring radius in pixels**: starting values **3 px = 50%
  transparent, 1 px = 90% transparent, fully gone below that**, to be tuned visually in game. **Dots and chevrons fade
  by their own on-screen size on the same curve and keep the readable minimum.**
- **D4: Dots and chevrons** at every ring and at the halfway points between rings (as in SDD 50 D23). The mock-up
  showed that ring-station chevrons are hard to see under the ring glow from far: **accepted for now, revisit in
  game**. Dots and chevrons are shown **only once the ring gap is wide enough on screen**.
- **D5: Gallery and profile.** The gallery images `Docs/Images/49-lane-tunnel-*.png` and `Docs/Images/50-lane-*.png`
  are **reshot at 1920x1080** when done; the tunnel is **re-profiled at the end** against the SDD 50 section 15
  baseline.

Answers to the open questions Q1-Q14 (user, 2026-11-18); where one changes a number it is applied in section 4:

- **D6 (Q1): Far lane ships stay hidden; the rings stand for the lane.** A ship smaller than 1 pixel is not visible.
  SDD 38 D9 ("a lane is never empty") is dropped in the flight view. The "points everywhere" option is measured once
  for the record only if that is cheap.
- **D7 (Q2): Two strengths.** The rings, dots and chevrons draw at **all distances** (`F` = 1 for drawing). The wall
  label and the merge readout keep the **old 100 -> 50 lane radii band** as a separate label strength.
- **D8 (Q3): Pixel sizes are 1080p-reference pixels scaled by the viewport height** (3 px at 1080p = 6 px at 4K), so the
  look and the window size are the same at every resolution.
- **D9 (Q4): The curve.** Solid (alpha 1) at **6 px**, **1 px = 90% transparent** (alpha 0.1), gone at **0.5 px**,
  **log-linear** between those points; the constants are tuned in game. This supersedes D3's 3 px = 50% point.
- **D10 (Q5): The pixel rule replaces** the 10-half-extent screen fade, the depth floor 0.2 and the 256-512 Rt detail
  fade. The D22 13-30 Rt chevron far fade **stays**.
- **D11 (Q6): Gap fades.** Rings fade as the ring gap shrinks from **3 to 2 px**; dots and chevrons draw only when the
  ring gap is **at least 20 px** (fading in around it). All constants, tuned in game. The lattice is **never
  subsampled** (rings beyond the limit fade, none are skipped).
- **D12 (Q7): Dots fade by their ring's radius on screen; chevrons by their world length on screen.**
- **D13 (Q8): The vertex shader places the window**; no per-frame mesh rebuild and no CPU window start.
- **D14 (Q9): End rings follow the pixel curve** with a **1 px drawn floor**; they are no longer exempt from it.
- **D15 (Q10): Vertex budget deferred to ticket #53** (lane network: more lanes, planet to next-closest planet, fastest
  A-to-Z routing, distance on/off, future stations and asteroids). #52 keeps **80,000 for 8 lanes as an INTERIM** and
  moves the number into `SOLConstants.h` (`NPC_TUNNEL_VERTEX_BUDGET`); the budget and the active-tunnel cap are
  revisited in #53.
- **D16 (Q11): `M_SOLNpcTunnel.uasset` is not rebuilt**; new parameters go only through free vector components, set at
  runtime. *Amended by the user (2026-11-18, 52b review): the ninth constant, the end-ring minimum drawn radius, gets a
  proper parameter, so the asset is rebuilt once (`Tools/Npc/create_npc_assets.py -SOLRebuild -SOLOnly=M_SOLNpcTunnel`)
  with the new vector `NpcTunnelLayout4` = (end-ring minimum drawn px, ribbon inner fraction, 0, 0); the other far-field
  constants keep riding the freed components, and no constant rides the tunnel texture.*
- **D17 (Q12): The sparse approach view is judged by screenshots only**; the smoke asserts no minimum ring count.
- **D18 (Q13): The 0.02 ms CPU on vs off gate stays** (report if missed, SDD 49 D20 rule).
- **D19 (Q14): The +0.10 ms points gate is not needed**, since far ships stay hidden (D6).

## 3. What exists today (code survey, HEAD `d06b96d`)

| Piece | Where | Facts that matter here |
|---|---|---|
| Tunnel math | `Npc/SOLNpcTunnelMath.{h,cpp}` (`SOLNpcTunnel::`) | `TunnelFade` (100 -> 50 lane radii, log-linear), `RingSpacingM` / `RingWarp` / `RingWarpInverse` (accordion, `max(S0, rho sqrt(x^2 + h^2))`), `AdvanceRingPhase` / `InitialRingPhase` / `StepRingPhase` (phase accumulated from the foot arc change), `RingSlot` / `DotSlot` / `ChevronSlot` / `EndRingSlot`, `RingCapFade` / `DetailCapFade`, `TunnelDepthFade` (floor 0.2 at 65,536 Rt), `TunnelDetailFade` (256-512 Rt), `TunnelChevronFarFade` (13-30 Rt, D22), `TunnelScreenSizeFade` / `TunnelRingScreenFade` (10 ribbon half extents, end rings exempt), `RingSegmentsForSlot`, `TunnelSlotMeshVertexCount` (2,884), `WriteTunnelTexels` (64 x 4 texels, row 3 `.zw` unused) |
| Constants | `SOLConstants.h` lines ~2679-2803 | `NPC_TUNNEL_RING_BASE_SPACING_RADII` 12, `NPC_TUNNEL_RING_SPACING_PER_DISTANCE` 1.5 (both tied to `NPC_TUNNEL_RING_KEEP_EVERY` 6 by a `static_assert`), `NPC_TUNNEL_RING_SLOTS_PER_SIDE` 12, `NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE` 6, LOD bands 2 / 3 / 5 with 96 / 48 / 24 / 12 segments; `static_assert(NPC_TUNNEL_FADE_START_RADII == NPC_LANE_FADE_NEAR_RADII)` ties the tunnel band to the #38 close-up fade |
| Vertex budget | `Tests/NpcTunnelMathTest.cpp:3654` (`eightLaneVertexBudget = 80000`), SDD 49 section 4.7 | **The 80,000-vertex budget for 8 lanes is a test constant, not a `SOLConstants.h` value** (the brief assumed the latter). It assumes 8 lanes; the lane capacity is `NPC_GROUP_TEXTURE_WIDTH` (64) |
| Component | `Npc/SOLNpcTunnelComponent.cpp` (`UpdateTunnels`) | Per lane: segment distance; a lane with `TunnelFade` 0 builds no frame (49c CPU cut); feed, phase step, label side; instances rewritten only when the set of lanes with fade > 0 changes; texture uploaded while a tunnel shows; `PublishLabels` |
| Shader | `Shaders/Private/SOLNpcTunnel.ush` (`SOLNT_*`) | Float mirror; `SOLNT_RingWarpInverse` uses `exp(2t)` (the float overflow ceiling past about 22 slots at `h` = 100 Rt, SDD 49 section 4.5); placement by `SOLOL_PlaceLinePointCm`; parameters in `NpcTunnelStyle0-3`, `Layout0-3`, `Shape`, `Glow0-1` (`Layout0` = base spacing, rho, ring slots, gap slots) |
| Lane line | `ASOLNpcVisuals`, `M_SOLNpcLanes`, `SOLNpcLane.ush` | Line alpha `LaneLineFade(d, R) x (1 - F)`; lane-ship point alpha `1 - LaneLineFade x (1 - F)` (SDD 38 D9, SDD 49 Q5: the two sum to 1, a lane is never empty); the map shows the lines (SDD 38 D18), tunnels are hidden there (SDD 49 Q6, `F` = 0) |
| Labels | `PublishLabels`, `UI/SOLLaneTunnelLabel`, `UI/SOLFlightHud` | One label per lane with `F` > 0, strongest by `F`, at most 4; carries the #50 merge readout (SDD 50 section 4.8) |

**Known debt not to re-report** (grep of `Docs/TECH_DEBT.md`): the D21 points cost inside a tunnel (+0.25-0.39 ms
GPU at 1080p); the tunnel CPU cost at the D14 budget (0.016-0.022 ms on vs off); the labels avoid the previous frame's
panel boxes and no UMG widgets; `Npc` leaning on the orbit lines' actor-side code (`SetLanesDrawn`); the `Npc <-> Menu`
folder cycle; the six copies of the smoke view-size helper; the one-eye menu costs.

## 4. Design

Everything in this section follows from D1-D5. Where a value or a rule was **not** given by the user it is marked
*provisional* and points at the open question that decides it (section 11). Provisional arithmetic uses 1920x1080,
90 degree horizontal field of view, focal length `f` about 960 px.

### 4.1 Overview

The tunnel becomes the lane's only look in the flight view (D1). The ring lattice is fixed to the lane at a flat
`S0 = 12 Rt` spacing (D2). A fixed slot mesh per lane draws a window of that lattice around the camera's foot point;
each ring's alpha is the product of the existing near, end and cap fades and the new pixel-radius fade (D3) and gap
fade (D2). Dots and chevrons ride the same lattice at offsets 0 and 0.5 (D4). The lane line component draws only while
the jump map is open (D1). No ship moves, no gameplay changes (SDD 49 D2 stands).

### 4.2 Flat lattice and phase (D2)

- **Ring arcs.** Rings sit at lane arcs `a_k = k S0` from node A (`k` integer), restricted to strictly between the end
  rings. With a constant spacing every ring is fixed to the lane at every distance: the accordion's slide (SDD 49
  section 4.5, "accepted trade-off") disappears.
- **Phase.** `Phi = frac(a_F / S0)` computed **directly in double** each frame from the unclamped foot arc `a_F`; no
  accumulated history is needed (at the longest lane `a_F / S0` is below about 40,000, so double keeps `Phi` to about
  1e-11). The ring at warp coordinate `w` lies at axial offset `x = w S0` from the foot point; ring slot `k` ahead is
  `w = k + 1 - Phi`, behind `w = -(k + Phi)`, as today with the warp made linear. `RingWarp`, `RingWarpInverse`,
  `RingSpacingM`, `AdvanceRingPhase`, `InitialRingPhase` and `StepRingPhase` lose their accordion branch or are
  removed (section 9; the implementer reports which).
- **Float overflow ceiling gone.** With `x = w S0` the shader no longer evaluates `exp(2t)`, so the SDD 49 section 4.5
  limit (about 22 slots per side) no longer bounds the slot count.
- **Precision.** The shader receives only camera-relative values: the foot point relative to the viewpoint, `Phi` in
  [0, 1), and slot offsets bounded by the window (`|x| <= SLOTS_PER_SIDE x S0`, provisionally about 1,000 Rt). A float
  offset of 1,000 Rt carries about 1e-4 Rt of error. The end-ring arcs relative to the foot arc can reach about
  4.5e12 m on Uranus-Neptune (float error about 3e5 m, under 0.03 Rt at its 1e7 m radius) and are placed with the
  far-depth compression. Tests in section 7 pin this at the maximum lane length.

### 4.3 The moving window (D2)

- **Window = the slot range around the foot point.** The slot mesh keeps one ring slot per lattice ring within
  `SLOTS_PER_SIDE` spacings of the foot point on each side, so it is recentred automatically as the foot point moves
  (the phase shifts the labels, the ring set stays). A ring enters or leaves the window only at alpha 0, through the
  cap fade `clamp(SLOTS_PER_SIDE - |w|, 0, 1)` (as today). No CPU mesh rebuild: the mesh is fixed; only the per-lane
  texels change per frame. *The vertex shader places the window (D13).*
- **"Near the view" and the window size (provisional).** A ring of radius `Rt` at distance `d` is `r_px = f Rt / d`
  px. D3 removes it below 1 px, so no ring farther than `d_max = f Rt / 1 px` (about 960 Rt at 1080p) can show.
  Seen from the side the visible lane section spans about `+-d`, so a window of about `d_max / S0` = 80 slots per side
  covers every ring that can pass D3 at 1080p. D8 makes the pixels 1080p-reference pixels, so the window is 80 per side at
  every resolution.
- **"On-screen gap large enough" (D2).** Defined here as a pure function `RingGapPx(x, h, Rt, S0, pxPerRad)`: the
  larger of (a) the angle between the view rays to the centres of the ring and its next neighbour away from the
  camera, and (b) the difference of their angular radii, times the focal length in px. Looking down the axis (a) is 0
  and (b) is `f Rt S0 / (d (d + S0))` (about 1.2 px at 100 Rt from inside); from the side (a) dominates and is about
  `S0 / Rt` = 12 times the ring radius in px, so there the pixel-radius fade (D3) removes rings first and the gap rule
  matters only along the axis. The ring's alpha is multiplied by a **gap fade** of `RingGapPx`; its threshold is the D11 fade
  from 3 to 2 px (never a cut). The gap fade replaces the subsampling a nested scheme would
  use: the lattice is never thinned, rings beyond the gap limit fade out (D11).
- **Level of detail.** Slot index grows with the axial offset from the foot point, so it is a lower bound on the
  ring's distance; LOD stays by slot (provisional bands: slots 0-1 96 segments, 2 48, 3-4 24, 5-9 12, 10 and up 8;
  end rings 96). Seen from the side, near slots are over-tessellated (harmless).
- **Provisional vertex estimate.** Rings, 80 slots per side with the bands above: about 3,950; end rings 388; dots in
  16 gap slots per side (8 rows x 2 per gap x 4 vertices): about 2,050; chevrons (2 walls x 2 per gap x 4): about 510.
  About 6,900 per lane, about 55,000 for 8 lanes, under the 80,000 test budget. The detail slot count depends on the D11 gap gate (20 px)
  and D12; the budget is the interim 80,000 for 8 lanes in `SOLConstants.h` (D15), revisited in #53.

### 4.4 Fades (D3, D4)

- **Pixel-radius fade** `RingRadiusFade(r_px)` (D3): in 1080p-reference pixels (D8): alpha 1 at 6 px, 0.1 at 1 px, 0 at
  0.5 px, linear in log px between these points (no pop at 1 px); constants tuned in game (D9).
  `r_px` is the ring radius at its centre (`Rt` x pixels per metre at the centre, as 49c's `TunnelRingScreenFade`).
- **What it replaces.** D10: the 10-half-extent screen fade (49c), the depth fade floor 0.2 and the 256-512 Rt detail
  fade are removed; the 13-30 Rt chevron far fade (D22) stays.
- **Dots and chevrons** fade "by their own on-screen size on the same curve and keep the readable minimum" (D3). A
  dot's world size is 0.034 Rt (3 px at about 11 Rt, 1 px at about 33 Rt); the drawn dot never goes below the 3 px
  floor; a chevron is 0.52 Rt long (3 px at about 166 Rt). Dots fade by their ring's radius on screen, chevrons by their world length on screen (D12).
- **Gap gate for details (D4).** Dots and chevrons draw only where the ring gap (above) is wide enough; the threshold is a ring gap of at
  least 20 px, fading in around it (D11).
- **Kept as built:** the near fade with its radial weight (D17), the end fade (0.5 spacings), the cap fades, the
  dots' and chevrons' near fade by their own distance, the per-lane tunnel fade as a master multiplier.

### 4.5 End rings, labels, ships, occlusion (D1)

- **End rings.** Unchanged geometry (3 tunnel radii outside each surface). Today they are exempt from the screen-size
  fade and keep an alpha floor of 0.5. With tunnels at all distances that exemption makes every lane's two end rings
  visible from anywhere in the solar system (16 marks for 8 lanes). D14: the end rings take the pixel curve with a 1 px drawn floor, so a far lane
  is not marked by 16 permanent rings.
- **Tunnel fade `F`.** Today `F` (100 -> 50 lane radii) gates everything the tunnel draws, picks the label lanes and
  their strength, and drives the line / point crossfade. With D1 the tunnel draws at all distances, so `F` no longer
  gates geometry. D7: `F` = 1 everywhere in the flight view for drawing; the old 100 -> 50 band survives
  only as the separate label strength.
- **Labels.** One label per lane with `F` > 0, at most 4, strongest first. If `F` is 1 everywhere every lane competes
  for a label from anywhere. D7: the labels keep the old 100 -> 50 band as a separate label strength. The merge readout inside the label (SDD 50 section 4.8) follows the label rule.
- **Lane ships far away.** With the line gone the crossfade's "line stands for the far ships" half is gone too. Either
  every lane ship draws as a point at every distance (the `F` = 1 branch of today's formula; the D21 points cost, now
  on every lane all the time, unmeasured), or far lane ships stay hidden and SDD 38 D9 ("a lane is never empty") is
  dropped in the flight view, the rings standing for the lane. D6: far lane ships stay hidden (option b).
- **Occlusion.** Unchanged: the shared `MPC_SOLOrbitLine` occluders plus the scene-depth rule; `SetLanesDrawn` is
  reported as "lane lines or tunnels drawn" (`IsLaneOverlayDrawn`), so the occluders keep being written. With the
  tunnels always drawn they are written every frame the Lanes row is on.
- **Guide dots world-sized near** (49c) stay; their far behaviour follows D12.
- **Map.** The tunnels stay hidden on the jump map (SDD 49 Q6); the lane line component draws only while the map is
  open, with its #38 close-up fade unchanged there.

### 4.6 Renderer and assets

- **Slot mesh.** Built once at `BeginPlay` as today, with the new slot counts and bands; UVs are mesh data, so the
  layout change needs no asset change.
- **Instances.** Every resolved, drawable lane gets an instance whenever the tunnels are shown (8 in the default
  layout, up to 64). The rewrite-on-change rule stays.
- **Texture.** Uploaded every frame while the Lanes row is on (today only while a tunnel shows). Row 3 `.z` carries the
  draw fade, `.w` is unused (0) (the shader places the window, D13).
- **Material.** The `.ush` changes need no asset rebuild (the Custom node includes the file). New tunables can either
  reuse free or obsolete vector components (`Layout0.y` rho becomes free; `Style3.x` the half-extent count, free since D10), set at runtime from `SOLConstants.h` as 12d did, D16: parameters only. *As amended (user, 52b review): one rebuild added `NpcTunnelLayout4` (end-ring floor, ribbon inner fraction; `.zw` free for later tunables).*
- **Lane line.** `ASOLNpcVisuals` hides the lane ribbon in the flight view and shows it on the map; the group texture
  and the points material are unchanged (D6: far ships hidden, no crossfade in flight).

### 4.7 Per-frame cost (provisional)

- **CPU.** The 49c "out-of-range lane builds no frame" cut no longer applies: every lane builds its frame every frame,
  the cost measured in 49c's forced all-lanes case (tunnel update about 0.017 ms at 1080p). The window needs no CPU
  work in the shader option, or one closed form per lane in the CPU option (under 1 us for 8 lanes). The lane line's
  flight-view work goes away. Gate: D14's 0.02 ms on vs off (section 8).
- **GPU.** About 55,000 vertices for 8 lanes (provisional, mostly early-out hidden); pixel work: the near rings as
  today plus the far rings at 1-3 px. From inside a tube the gap fade bounds the pile-up at the vanishing point. The
  lane line's flight-view draw goes away. The points draw is not extended to far ships (D6).

### 4.8 What does not change

The tunnel basis and keep-right sides (SDD 49 4.3), the tunnel radius by traffic (D8), the ring and dot colours and
widths (4.6), the chevron dart shape, the end-ring position, the occluders, the HUD glow feed, the map's lane lines,
the lane traffic and every gameplay rule, the save schema.

### 4.9 Terms (as built in 52b)

There is no separate glossary document for the developer docs (the player-facing glossary is the in-game Help page and
the generated `PLAYER_GUIDE.md`), so the terms of this ticket are defined here.

- **Label strength:** `TunnelFade` on the old 100 -> 50 lane radii band (0 at 100, 1 at 50, log-linear). Tunnel texture
  row 1 `.w`. It gates the wall labels, the #50 merge readout and the lane-ship points, and is still read by the
  lane-line and point materials (D7). Not a drawing gate.
- **Draw fade:** `TunnelDrawFade(shown)`, 1 for every drawable lane while tunnels are shown. Tunnel texture row 3 `.z`.
  The master multiplier of everything a tunnel draws (D7).
- **Reference pixel (1080p):** an actual pixel scaled by `NPC_LANE_REFERENCE_HEIGHT_PX` / viewport height, so the fade
  curve and the window are the same at 720p, 1080p and 4K (D8). The focal length in reference pixels is computed per
  view in the shader from `ViewToClip[0][0]`.
- **Ring lattice (flat lattice):** rings at lane arcs `k S0` from node A, `S0` = 12 tunnel radii at every distance,
  fixed to the lane (D2). Replaces the #49 accordion warp, which is superseded (`RingWarp` and friends are gone).
- **Ring phase:** `frac(foot arc / S0)`, computed directly in double each frame (no accumulated history).
- **Ring gap (`RingGapPx`):** the on-screen distance between a ring and its next neighbour, in reference pixels. Rings
  fade as it shrinks from 3 to 2 px, dots and chevrons as it shrinks from 25 to 16 px (D11).
- **Window (ring slots):** the fixed slot mesh of 80 ring slots and 33 detail slots per side of the player's foot point
  on the lane. The vertex shader places each slot on the lattice (D13), so the window follows the player without a mesh
  rebuild.
- **Knee:** the 1 px point of the radius fade curve (alpha 0.1), between solid at 6 px and gone at 0.5 px (D9).

## 5. SDD 38 / 49 / 50 sections that change

Recorded here; those SDDs are amended in place when the parts are built, not in this draft.

- **SDD 38:** D9 and D10 (the crossfade without a flight-view line, Q1), section 4.5 (close-up fade, now map only),
  D18 (the map keeps the lines, the flight view does not).
- **SDD 49:** D1 (trigger and hand-over), D7 (ring spacing and visibility), D12 amendment (end-ring exemption, D14), D13
  (smooth spacing: superseded by the flat lattice), D22 (chevron far fade stays, D10), SDD 49 Q5 (line fade with the tunnel, D6 / D7), Q11 (detail fade, D10), sections 4.2, 4.5 (accordion replaced), 4.7 (slots, LOD, budget), 4.9 (labels, D7),
  9, 10, 12c (screen fade), 12d (thinning: its density is kept as the flat spacing).
- **SDD 50:** D23 (thinning now the flat spacing everywhere), section 4.8 (the merge readout follows the label rule),
  section 15 (baseline for the re-profile), section 16 (the "decided separately under #52" note).

## 6. Player-facing text and docs

- `Docs/GAME_MECHANICS.md`: the lane lines are map only; the tunnel at every distance; ring spacing, the fade rule,
  dots and chevrons. The in-game Info / Controls text says the same in the same change (CLAUDE.md rule).
- `Docs/ARCHITECTURE.md`: the lane line component's map-only role, the tunnel at every distance.
- `Docs/TECH_DEBT.md`: re-word the D21 points item per D6; re-word the tunnel CPU item after the re-profile.
- `README.md` gallery: D5 reshoots, `Docs/Images/49-lane-tunnel-*.png` and `50-lane-*.png` at 1920x1080.

## 7. Test list (test first; contract frozen after section 11 is answered)

Pure functions (`Source/SOLTest/Tests/NpcTunnelMathTest.cpp`, `SOLTest.NpcTunnelMath.*`; authored by a subagent from
this contract, not from the implementation):

1. **FlatSpacing:** ring arcs `k S0` from node A, `S0` = 12 Rt; every ring strictly between the end rings; spacing
   independent of the camera distance and `h` (the accordion is gone).
2. **DirectPhase:** `Phi = frac(a_F / S0)` in [0, 1) for arcs 0 .. 4.5e12 m and negative arcs; continuity across the
   wrap (slot labels shift by one, the ring set is unchanged).
3. **RingWindow:** for camera positions inside, beside and far from the tube, every lattice ring with alpha > 0 is
   drawn by exactly one slot; a ring enters or leaves the window only at alpha 0 (sweeps along and across the lane,
   12,000 steps, as SDD 49 case 11).
4. **NoPops:** each lattice ring's alpha is continuous in the camera position along and across the lane and in the
   viewport height (the D3 curve, the gap fade, the cap and end fades).
5. **RadiusFadeCurve:** the D3 points (3 px -> 0.5, 1 px -> 0.1, 0 below), monotone, the upper knee and the ramp
   below 1 px per D9 (6 px = 1, 1 px = 0.1, 0.5 px = 0, log-linear); NaN and negative inputs.
6. **RingGapPx:** on the axis equals `f Rt S0 / (d (d + S0))`; from the side about `S0 / Rt` times the ring radius
   in px; monotone in distance; the gap fade threshold per D11 (rings 3 to 2 px, details at least 20 px).
7. **DetailFade:** dots and chevrons follow the D9 curve on the D12 sizes (dot by ring radius, chevron by world length); the dot never drawn below the 3 px
   floor; details gated by the D4 gap threshold.
8. **FloatPathAtMaxLength:** the float slot offset (the shader path) against the double reference on a 757,000-tunnel
   radii lane (Uranus-Neptune at its minimum radius), the camera near node A, the middle and node B: within 1e-3 Rt
   for rings within 64 Rt of the foot, within 1e-5 of the distance beyond; end-ring arcs at 4.5e12 m within 0.05 Rt.
9. **HonestFarView:** from a camera far from a lane (beyond `d_max`), no repeating ring has alpha > 0; the end rings
   per D14 (pixel curve, 1 px drawn floor).
10. **SlotMesh / Budget:** `TunnelSlotMeshVertexCount()` times 8 lanes under `NPC_TUNNEL_VERTEX_BUDGET` = 80,000 (D15, interim); bands ordered; the
    window covers `d_max / S0` per side at the reference height.
11. **Constants:** the new constants and `static_assert`s (base spacing tied to `KEEP_EVERY`, rho removed or unused).
12. **LaneLine / crossfade** (`NpcLaneMathTest.cpp`): the flight-view line alpha 0; the map line alpha unchanged; the
    no far lane-ship points in flight (D6).
13. **Labels** (`LaneTunnelLabelTest.cpp`, `SelectLabelLanes`): the label strength on the old 100 -> 50 band (D7); at most 4; the merge
    readout still placed in its label.

Smokes: `-SOLSmokeNpcTunnel` gains far views (beside Earth-Mars at about 300, 1,000 and 3,000 Rt; an inner-system view
where only end rings show; the approach sweep from far to inside) and asserts the window coverage, the ring count per
view and the no-label rule per D7, no minimum ring count asserted (D17); `-SOLSmokeNpcLanes` asserts the line on the map and absent in flight.

## 8. Verification and performance gates

- Full suite `Tools\RunTests.bat`; smokes NpcTunnel, NpcLanes, NpcTraffic, LaneMerge, AutoLanes, Overlay, Hud at
  1280x720, 1920x1080 and 4K (the supported resolutions); `GameUserSettings.ini` MD5 unchanged.
- **Gates at 1920x1080, 1M NPCs, all lanes** (SDD 49 D14, now the normal case): tunnel draw <= 0.15 ms GPU; tunnel
  update + HUD labels <= 0.02 ms CPU on vs off (`sol.NpcTunnel 0`, SDD 49 D20 method); the points draw against the
  SDD 50 section 15 baseline, no points gate (D6, D19). Method as SDD 50 section 15 (interleaved runs, baseline worktree at the
  pre-#52 commit, `-SOLSmokeNpcTunnelProfile`).
- **Recorded only:** 3840x2160 and one XR-4 eye (3840x3744). **VR note: record only, no VR work, no two-eye run**
  (no VR work before procedural terrain).
- PIE / smoke screenshots for the D3 tuning (the user tunes the curve in game) and the sparse approach view; D5
  gallery reshoots at 1920x1080. Temporary screenshots are deleted after use.
- Adversarial performance / style review by a fresh `opus` subagent after the code (CLAUDE.md).

## 9. Risks

- **Sparse far view (D2, accepted):** from far only the end rings show; mid distances show a short run of rings near
  the foot point. Re-check on screenshots.
- **Vanishing-point pile-up from inside:** D3's 1-3 px tail is far more lenient than 49c's 30 px screen fade; the
  white core 49c removed can return. Lever: the gap fade (D11).
- **Resolution dependence:** with literal pixels the window and the look differ between 1080p and 4K (D8: reference pixels scaled by height, so resolved).
- **Points cost (D6):** avoided, far ships stay hidden; measuring the "points everywhere" option is optional.
- **Label clutter:** avoided by the separate label strength (D7).
- **Chevrons under the ring glow from far (D4, accepted, revisit in game).**
- **Low-segment far rings:** an 8-segment ring at 5-10 px may read as a polygon; lever: the bands.
- **Lane capacity:** the budget is for 8 lanes; a 64-lane layout would need 8 times the vertices (interim, revisited in #53, D15).
- **HLSL mirror drift:** the `.ush` mirrors the C++; both headers say so.
- **One asset rebuild (D16 as amended):** `NpcTunnelLayout4` added; its `.zw` are free for later tunables; beyond those,
  stop and ask before another rebuild.

## 10. Persistence

None. `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay; no setting, key binding or profile row changes (the
Lanes row and master O govern the tunnel as today). Any finding that needs one is stopped and reported.

## 11. Open questions

None. Q1-Q14 were answered by the user on 2026-11-18 and are recorded as decisions D6-D19 in section 2 (Q1 = D6, Q2 = D7,
Q3 = D8, Q4 = D9, Q5 = D10, Q6 = D11, Q7 = D12, Q8 = D13, Q9 = D14, Q10 = D15, Q11 = D16, Q12 = D17, Q13 = D18,
Q14 = D19). The only deferral is the vertex budget and active-tunnel cap, handed to ticket #53 (D15).

**Closed point from 52b:** the end-ring drawn floor (`NPC_TUNNEL_END_RING_MIN_DRAWN_PX`) rode tunnel texture row 3 `.w`
(eight free components for nine constants). The user chose the material rebuild (2026-11-18): it is now
`NpcTunnelLayout4.x` (D16 amended, section 12 review-fixes entry).

## 12. Change log

- 2026-11-18: first draft from grill-me D1-D5 (user); the design derived from them, open questions Q1-Q14 for the
  user; no code, no persistence change.
- 2026-11-18: Q1-Q14 answered by the user and moved to the decisions as D6-D19; section 4 updated where an answer
  changes a number (curve 6 / 1 / 0.5 px log-linear, 20 px detail gate, 3-2 px ring gap fade, window 80 per side at
  every resolution, end rings on the curve with a 1 px floor, interim 80,000 budget in `SOLConstants.h`); open
  questions removed, ready for test-first (52a).
- 2026-11-18: **part 52a built** against the test-first contract
  [`Plans/52a-tunnel-far-field-contract.md`](../Plans/52a-tunnel-far-field-contract.md). The tests are in
  `Source/SOLTest/Tests/NpcTunnelFarFieldTest.cpp` under `SOLTest.Npc.TunnelFarField.*` (not `SOLTest.NpcTunnelMath`,
  as section 7 first said); the invalidated `SOLTest.NpcTunnelMath` cases were deleted or re-pinned (plan 52 lists
  them). Values and decisions taken in 52a:
  - **End-ring alpha (user, 2026-11-18):** the end rings follow the pixel curve all the way and fade to 0 at 0.5 px like
    every other ring; no held 10% alpha. They are drawn at `EndRingDrawnRadiusPx` (1 px floor, D14).
  - **Detail gap band:** `NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX` 16 and `_FULL_PX` 25 (20 px the log midpoint; start
    values, tuned in game).
  - **Window and LOD:** 80 ring slots per side; 33 detail slots per side (the least that keeps the gap gate shut beyond
    `DETAIL_SLOTS - 3` spacings at the reference focal, with a `static_assert`); ring bands 96 / 48 / 24 / 12 / 8
    segments at slots 0-1 / 2 / 3-4 / 5-9 / 10+ (new `NPC_TUNNEL_RING_SEGMENTS_FARTHEST`, `NPC_TUNNEL_RING_FAR_SLOTS`).
    Slot mesh 9,620 vertices per lane, 76,960 for 8 lanes, under the interim `NPC_TUNNEL_VERTEX_BUDGET` 80,000.
  - **New pure functions** (`SOLNpcTunnel::`): `FocalLengthRefPx`, `ToReferencePx`, `RingLatticeSpacingM`,
    `RingLatticePhase`, `RingRadiusPx`, `ChevronLengthPx`, `DotDrawnSizePx`, `EndRingDrawnRadiusPx`, `RingRadiusFade`,
    `RingGapPx`, `RingGapFade`, `DetailGapFade`, `TunnelDrawFade`; `FSOLNpcTunnelSlotInput::FocalRefPx` (no slot draws
    without a positive focal); new signatures `RingSlotOffsetM(slot, phase, Rt)` and `EndRingAlpha(x, h, Rt, f)`.
  - **Removed:** `RingSpacingM`, `RingWarp`, `RingWarpInverse`, `AdvanceRingPhase`, `InitialRingPhase`,
    `StepRingPhase`, `TunnelDepthFade`, `TunnelDetailFade`, `TunnelScreenSizeFade`, `TunnelRingScreenFade`. The
    component now sets the phase with `RingLatticePhase(foot arc)` each frame (its per-lane phase history is gone).
  - **Dot-row reach** (the label avoidance rows): the rows end where the gap gate's reach ends,
    `NPC_TUNNEL_REFERENCE_FOCAL_PX x S0 / NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX` of true distance (720 Rt; `RingGapPx`
    never exceeds `f S0 / d`), replacing the 512 Rt detail-fade end. 52b may revisit it with the label work.
  - **Kept for the pre-52b shader only** (no C++ math reads them; 52b removes them with the shader rewrite):
    `NPC_TUNNEL_RING_SPACING_PER_DISTANCE` (rho, `Layout0.y`; the contract lists it as removed, so this is a recorded
    deviation), `NPC_TUNNEL_DEPTH_FADE_*`, `NPC_TUNNEL_DETAIL_FADE_START/END_RADII`, `NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS`
    and `NPC_TUNNEL_END_RING_MIN_ALPHA`. Until 52b the shader draws the #49 accordion with these fades on the new slot
    counts (reasoned from the source, not rendered: past about 22 slots its `exp(2t)` overflows to inf / NaN, which
    fails the end-arc test, so those slots are hidden rather than drawn wrong); the phase it receives is the flat
    lattice's, identical to the #49 phase while the camera is within 8 Rt of the axis (farther out the accordion's far
    rings slide at a slightly different rate until 52b).
  - **Assumption for 52b:** the window and the detail coverage hold for a focal length up to the reference (960 px, a
    16:9 view at 90 to 110 degrees). A wider aspect at the same horizontal FOV (21:9) has a longer focal in reference
    px and would need more slots.
- 2026-11-18: **part 52b built** (shader, component, lane line, float mirror; the decisions D1-D19 are untouched). Not
  yet committed. What was done:
  - **Shader** `Shaders/Private/SOLNpcTunnel.ush` rewritten onto the 52a math: flat ring lattice `x = w S0` with `S0` = 12
    tunnel radii everywhere (accordion and `exp(2t)` path gone), `RingRadiusFade` (6 px solid, 1 px = 0.1, 0.5 px gone,
    log-linear, 1080p-reference pixels), `RingGapPx` with the ring gap fade (3 -> 2 px) and the dot / chevron gap gate
    (16 -> 25 px, 20 px midpoint), dots fade by their ring's radius, chevrons by their length (D12), the chevron far fade
    13-30 Rt kept (D22), end rings on the pixel curve drawn at `max(radius, 1 px)` (D14), the focal length in reference
    px computed per view in the shader from `ViewToClip[0][0]`. **Removed from the shader:** the depth fade (floor 0.2),
    the 256-512 Rt detail fade, the 49c 10-half-extent screen fade, the end-ring 0.5 alpha floor and its exemption.
  - **Float CPU mirror** `Source/SOLTest/Npc/SOLNpcTunnelMirror.h`: a statement-for-statement float copy of the shader,
    tested against the double `SOLNpcTunnel` functions in `Source/SOLTest/Tests/NpcTunnelMirrorTest.cpp`
    (`SOLTest.Npc.TunnelMirror.Packing`, `.Fades`, `.Slots`).
  - **No asset rebuild (D16).** `M_SOLNpcTunnel.uasset` is untouched. The new constants ride freed vector components,
    packed by the new pure function `SOLNpcTunnel::TunnelMaterialVectors()` (`Tools/Npc/create_npc_assets.py` defaults
    now name the same constants, script only, not re-run):

    | Component | Was | Now |
    |---|---|---|
    | `Layout0.y` | rho (accordion) | `NPC_TUNNEL_RING_GAP_FADE_FULL_PX` |
    | `Style3.x` | screen fade half extents | `NPC_TUNNEL_RING_GAP_FADE_GONE_PX` |
    | `Shape.w` | end ring min alpha | `NPC_TUNNEL_RADIUS_FADE_GONE_PX` |
    | `Layout1.yzw` | depth fade start / end / floor | `NPC_TUNNEL_RADIUS_FADE_SOLID_PX`, `_KNEE_PX`, `_KNEE_ALPHA` |
    | `Layout3.xy` | detail fade start / end | `NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX`, `_FULL_PX` |

  - **OPEN POINT for the user (deviation from D16, "vector components only"; closed by the review-fixes entry below:
    the user chose a material rebuild):** nine new constants but only eight free components. The ninth, `NPC_TUNNEL_END_RING_MIN_DRAWN_PX`, goes **per lane in tunnel texture row 3 `.w`**. The
    alternatives are a rebuild of `M_SOLNpcTunnel.uasset` (against D16) or a hard-wired 1 px in the shader (no tuning
    from `SOLConstants.h`). Awaiting the user's confirmation.
  - **Tunnel texture layout now** (64 x 4): row 0 foot relative to the viewpoint and `Rt`; row 1 axis and **label
    strength** (`TunnelFade` on the old 100 -> 50 lane radii band, still read by the lane-line and point materials);
    row 2 up and phase; row 3 end arcs A and B, **draw fade** (`TunnelDrawFade(shown)` = 1) and the end-ring drawn
    floor. `FSOLNpcTunnelLaneData` gained `DrawFade`; new `ReadTunnelDrawFadeTexel`.
  - **Component** `USOLNpcTunnelComponent`: every drawable lane builds its frame every frame and is drawn at every
    distance (instances = all drawable lanes; the 49c "out-of-range lane builds no frame" cut is gone); the texture is
    uploaded every frame while tunnels draw; labels and the #50 merge readout select by label strength (the old band,
    D7); the label snapshot is cleared once when no lane has strength. `sol.NpcTunnel 0` still hides the tunnels. The
    verification force-all-lanes forces label strength 1 (labels and lane-ship points everywhere, the all-lanes budget
    case).
  - **Lane lines and ships** (`ASOLNpcVisuals`): `SOLNpcLane::ShouldDrawLaneLines` takes `bMapOpen`; the dashed lines
    draw **only on the jump map** (D1), in flight they are gone. Far lane ships stay hidden in flight (D6): the point
    crossfade stays on while lanes are shown: the point alpha is 1 - LaneLineFade(d) x (1 - strength) per ship at distance d from the camera, so with strength 0 (the camera beyond 100 lane radii of the lane) a lane ship shows inside 100 lane radii of the camera, fades out between 100 and 400 lane radii (`NPC_LANE_FADE_NEAR_RADII` / `NPC_LANE_FADE_FAR_RADII`) and is hidden beyond; a lane with strength above 0 (the camera within the old band) draws every one of its ships at least at that strength at any distance, as since #49. SDD 38 D9 is dropped in the flight view.
  - **Removed constants** (`SOLConstants.h`): `NPC_TUNNEL_RING_SPACING_PER_DISTANCE`, `NPC_TUNNEL_DEPTH_FADE_START_RADII`,
    `_END_RADII`, `_FLOOR`, `NPC_TUNNEL_DETAIL_FADE_START_RADII`, `_END_RADII`, `NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS`,
    `NPC_TUNNEL_END_RING_MIN_ALPHA` (the 52a "kept for the pre-52b shader" list is now empty).
  - **Smokes:** `-SOLSmokeNpcTunnel` gained far views F300, F1000 and F3000 (beside the Earth-Mars quarter point at 300,
    1,000 and 3,000 tunnel radii looking down at the lane). Checks: the tunnel drawn with draw fade 1, label strength 0
    and no label, the lit ring count logged with no minimum (D17), the window's outermost slots at most the knee alpha,
    the end-ring alphas logged, the lane line hidden. `CheckFeed` now also checks draw fade 1 and label strength read
    back, far-ship point alpha = strength, and the lane line hidden in flight. `-SOLSmokeNpcLanes`: the lane line is
    visible only on the map, `c-occluders` expects no lane occluder writes in flight with tunnels hidden, `g-map-lanes`
    expects writes on the map.
  - **Tests re-pinned or added:** `NpcLaneMathTest` case 26 (`ShouldDrawLaneLines` with `bMapOpen`, flight rows) and the
    D6 rows in `ShipPointAlpha`; `NpcTunnelMathTest` `WriteTunnelTexels` (row 3), `VisibilityRules` (`TunnelDrawFade`,
    all drawable lanes draw, upload every frame) and `TexelCrossfade` (strength against draw fade, a far lane's strength
    0 hides far ships). The in-game Info text (`SOLMenuText.h`) was updated in the same change.
  - **Not done in 52b:** no far-to-inside approach sweep in the smoke (section 7 lists one for the smokes), no profile
    run against the SDD 50 section 15 baseline (52c), the D3 curve and gap constants are not tuned in game yet (52c).
- 2026-11-18: **52b review fixes** (still uncommitted; decisions D1-D19 unchanged except D16 as amended by the user):
  - **D16 amended (user): `M_SOLNpcTunnel.uasset` rebuilt** with the new vector parameter `NpcTunnelLayout4` =
    (`NPC_TUNNEL_END_RING_MIN_DRAWN_PX`, `NPC_TUNNEL_RIBBON_INNER_MAX_RADIUS_FRACTION`, 0, 0), `SOLNT_Vertex` gained the
    `layout4` argument, `create_npc_assets.py` gained the parameter and a `-SOLOnly=<material>` switch so only the tunnel
    material is rebuilt; tunnel texture row 3 `.w` is unused (0). `SOL::NpcTunnelMaterialParams::LAYOUT4` is in
    `VECTOR_NAMES`, so `SOLTest.NpcLaneVisuals.TunnelMaterial` checks the rebuilt asset has it.
  - **Sub-pixel ring brightness (high):** a ring smaller on screen than its ribbon folded its quads into overlapping
    bowties (the inward vertices crossed the centre) and glared. The inward ribbon vertex (its side toward the placed
    ring centre) now reaches at most `NPC_TUNNEL_RIBBON_INNER_MAX_RADIUS_FRACTION` (0.9) of the ring's on-screen radius,
    measured on the placed geometry; the outward vertex and every vertex of a ring larger than its ribbon are unchanged
    (bit for bit). New pure `SOLNpcTunnel::RingRibbonInnerExtentPx`, its float copy in the mirror and
    `SOLTest.Npc.TunnelMirror.RibbonInner`. The radius is read from the placed geometry, which the far-depth compression
    distorts beyond `DEFAULT_MAX_RENDER_DISTANCE_CM` (1e9 m); a ring larger than its ribbon (0.9 r above the 12.5 px
    half extent) is within about 69 tunnel radii, under 7e8 m for the largest tunnel radius (about 1e7 m), so only
    rings already being clamped can be compressed (margin about 1.4x; a larger tunnel radius would need the lateral
    radius instead). Screenshots (1920x1080, F1000 / T1 against the pre-fix set): the sub-pixel ring strip along the
    lane drops from mean luminance 14 to 2 (no pixel above 128, was 28-55), the far-end streak in T1 from 36 to 27; the
    large rings' total brightness is within 0.4-1.2% (per-pixel differences are 1 px shifts, run-to-run AA jitter).
  - **Label dot rows:** the labels keep clear of the dot rows of every drawn tunnel whose lane segment is within the gap
    gate's reach (new `SOLNpcTunnel::IsDotRowInReach`), not only of the lanes with label strength.
  - **Per-frame cost:** `LaneSegmentDistanceM` is computed once per lane per frame (`FSOLNpcTunnelLaneData::
    SegmentDistanceM`, filled by `BuildTunnelLaneData`); a warning is logged when the drawn lanes exceed
    `NPC_TUNNEL_VERTEX_BUDGET_LANES` (on a change of the drawn set only).
  - **Tests:** end rings pinned at 0.7 px (on the curve) and 0 at 0.5 px; `HonestFarView` asserts the end rings from
    beyond `d_max` are gone; mirror slot cases at the longest lane (Uranus-Neptune, near node A, the middle, node B).
  - **TUNING note for 52c (numbers not changed):** the ring gap fade (full 3 px, gone 2 px, D11) is far narrower than a
    ring's drawn glow (ribbon half extent about 12.5 px at 1080p, the core 1.5 px plus the halo), so rings whose gap is
    still above 3 px overlap each other's glow and add up toward the vanishing point. Judge on the 52c screenshots
    whether the gap fade should start nearer the glow width; any change is the user's call.
