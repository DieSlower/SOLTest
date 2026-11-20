<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 52: Lane tunnel far field

**Issue:** [#52](https://github.com/DieSlower/SOLTest/issues/52). SDD:
[`SDDs/52-lane-tunnel-far-field.md`](../SDDs/52-lane-tunnel-far-field.md).
Status: **52a and 52b implemented (2026-11-18, committed in `1611c19`), 52b review fixes applied, 52c (tuning,
profile, docs, gallery) done 2026-11-19 (uncommitted); the whole-ticket review is left.** Q1-Q14 answered by the user (SDD 52 D6-D19); the 52a test contract
is [`52a-tunnel-far-field-contract.md`](52a-tunnel-far-field-contract.md), its tests `SOLTest.Npc.TunnelFarField.*`. The vertex budget (80,000 for 8 lanes) is interim, revisited in #53 (D15).
**Persistence: none.** `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay; any finding that needs a change is
stopped and reported.

## Parts

Each part is built by its own subagent (context under about 35%), test first where it adds or changes pure functions,
then reviewed by a fresh adversarial `opus` subagent (performance and style), then committed and pushed
(`Issue #52: Part 52x - ...`), with `Docs/Plans/STATUS.md` and the metrics updated in the same commit.

### 52a: pure math, flat lattice and fades (test first)

1. Test author (`opus`, high, spec only): extend `Source/SOLTest/Tests/NpcTunnelMathTest.cpp` with SDD 52 section 7
   cases 1-11 (`FlatSpacing`, `DirectPhase`, `RingWindow`, `NoPops`, `RadiusFadeCurve`, `RingGapPx`, `DetailFade`,
   `FloatPathAtMaxLength`, `HonestFarView`, `SlotMesh` / budget, `Constants`); re-pin the accordion cases.
2. Run `Tools\RunTests.bat -Build -Filter SOLTest.NpcTunnelMath`; confirm the new cases fail for the right reason.
3. Implement in `Npc/SOLNpcTunnelMath.{h,cpp}` and `SOLConstants.h`: flat `S0`, direct phase, the window slot count,
   `RingRadiusFade`, `RingGapPx` and the gap fades, the detail size fade, LOD bands, the budget constant `NPC_TUNNEL_VERTEX_BUDGET` = 80,000 in `SOLConstants.h` (D15, interim, #53 revisits). Remove
   or simplify the accordion functions; report which.
4. Green run; re-validate every existing `NpcTunnelMath` case against the new contract (delete stale ones).

**52a progress (2026-11-18): steps 1-4 done.** The test author wrote `Tests/NpcTunnelFarFieldTest.cpp`
(`SOLTest.Npc.TunnelFarField.*`, run with `-Filter SOLTest.Npc.TunnelFarField`) from the contract; the math, constants
and removals are listed in SDD 52's change log (52a entry). `SOLTest.NpcTunnelMath` re-validation:

| Case | Action |
|---|---|
| `Constants` | rho, the 8 Rt zone, the 12 / 6 slot pins, depth, 256-512 Rt detail and 0.5 end-ring rows dropped; the rest kept |
| `RingSpacing`, `RingWarp`, `RingPhase`, `StepRingPhase`, `RingSlotFloatPath` | deleted (`TunnelFarField.FlatSpacing`, `DirectPhase`, `FloatPathAtMaxLength`) |
| `NoPopSweep`, `LaneFixedNearRings`, `RingSlotCoverage`, `DetailSlotCoverage`, `EndRing` | deleted (`TunnelFarField.NoPops*`, `NeverSubsampled`, `RingWindow`, `DetailFade`, `EndRings`) |
| `GuideThinning` | re-pinned to the flat lattice: rings on it, dots / chevrons at offsets 0 and 0.5, ring count within the window, axis rings 12 Rt apart (was e^1.5), out-of-range indices, tiny lane, end rings; the pre-thinning warp subset dropped |
| `Fades` | depth and detail fade rows dropped; near, end, cap and detail-cap rows kept with the new slot counts |
| `NearFadeRadial` | slot inputs carry the reference focal; the foot ring from outside is full (was "only the depth fade") |
| `ShapeFades` | screen-fade rows dropped; detail near fade, chevron far fade and dart kept |
| `DotRowSegment` | re-pinned to the gap gate's reach (720 Rt; the "nothing to draw" cases moved from 600 to 800 Rt) |
| `SlotMesh` | formula kept, bands checked by constant name (five bands); the 2,884 pin, band pins and test-local budget dropped (`TunnelFarField.SlotMeshBudget`) |
| `TexelCrossfade`, `VisibilityRules` | unchanged (52b) |

The render path is left as the plan says: the component sets the phase with `RingLatticePhase` and builds the larger
slot mesh, and the #49 shader keeps reading its old parameters (kept constants listed in SDD 52) until 52b.
`-SOLSmokeNpcTunnel` compiles (its chevron check now passes a focal length, its approach and fly checks read the flat
spacing) but was not run in 52a.

**52b must:** rewrite `SOLNpcTunnel.ush` onto the 52a math (linear warp `w x S0`, `RingRadiusFade`, `RingGapPx` and the
gap fades, the D12 detail sizes, end rings on the curve drawn at `EndRingDrawnRadiusPx`, the focal length in reference
px per view); pass the new constants through free vector components (`Layout0.y` rho, `Style3.x`, `Shape.w`, `Layout1`
and `Layout3` become free); then remove `NPC_TUNNEL_RING_SPACING_PER_DISTANCE`, `NPC_TUNNEL_DEPTH_FADE_*`,
`NPC_TUNNEL_DETAIL_FADE_START/END_RADII`, `NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS` and `NPC_TUNNEL_END_RING_MIN_ALPHA`;
plus steps 2-5 below.

### 52b: shader, component and lane line

1. `Shaders/Private/SOLNpcTunnel.ush`: mirror 52a (linear warp, the new fades, the window placed in the vertex shader (D13)); no `exp(2t)` path.
2. `Npc/SOLNpcTunnelComponent.cpp`: every drawable lane builds its frame every frame; `F` = 1 for drawing, the old 100-50 band as a separate label strength (D7); the texture every
   frame while the Lanes row is on; material parameters through free components at runtime, `M_SOLNpcTunnel.uasset` not rebuilt (D16; amended by the user in the review fixes: rebuilt once for `NpcTunnelLayout4`).
3. `ASOLNpcVisuals`: the lane ribbon draws only on the map (D1); far lane ships stay hidden, no crossfade in flight (D6).
4. Labels and merge readout on the old band (D7) (`PublishLabels`, `SelectLabelLanes`); tests case 12-13 first (`NpcLaneMathTest`,
   `LaneTunnelLabelTest`).
5. Full suite; `-SOLSmokeNpcTunnel` extended with the far views, `-SOLSmokeNpcLanes` re-pinned (line on the map, not
   in flight).

**52b progress (2026-11-18): steps 1-4 done (code and tests written, step 5's suite and smoke runs are the main
session's).** Details are in SDD 52's change log (52b entry); in short:

- Step 1: `SOLNpcTunnel.ush` is on the 52a math (flat lattice, pixel-radius and gap fades, end rings on the curve at a 1 px
  floor, focal length in reference px per view). New float mirror `Npc/SOLNpcTunnelMirror.h` with
  `Tests/NpcTunnelMirrorTest.cpp` (`SOLTest.Npc.TunnelMirror.Packing` / `Fades` / `Slots`).
- Step 2: the component builds every drawable lane every frame and draws it at every distance; the texture is uploaded
  every frame; new constants ride freed vector components through `SOLNpcTunnel::TunnelMaterialVectors()` (D16,
  `M_SOLNpcTunnel.uasset` not rebuilt); the ninth constant (end-ring drawn floor) rode texture row 3 `.w` until the
  review fixes (the user chose a material rebuild: `NpcTunnelLayout4`, D16 amended). Nine constants deleted from `SOLConstants.h` (listed in SDD 52).
- Step 3: the lane line draws only on the map (`ShouldDrawLaneLines(..., bMapOpen)`); far lane ships hidden in flight.
- Step 4: labels and the merge readout select by label strength (the old 100 -> 50 band, texture row 1 `.w`); the draw
  fade is row 3 `.z`.
- Step 5: `-SOLSmokeNpcTunnel` has far views F300, F1000, F3000 (no approach sweep from far to inside yet);
  `-SOLSmokeNpcLanes` re-pinned (line on the map only, no occluder writes in flight with tunnels hidden).

Re-pins in `SOLTest.NpcTunnelMath`: `TexelCrossfade` (label strength against draw fade, a far lane's strength 0 hides far
ships) and `VisibilityRules` (`TunnelDrawFade`, every drawable lane draws, upload every frame) were left unchanged by
52a and are re-pinned now; `WriteTunnelTexels` gained row 3. `SOLTest.NpcLaneMath`: new case 26 (`ShouldDrawLaneLines`
with `bMapOpen`) and the D6 rows in `ShipPointAlpha`. The in-game Info text and `GAME_MECHANICS.md` follow the new
behaviour. Not done: the profile run and the in-game tuning (52c).

### 52c: tuning, profile, docs, gallery

1. PIE / smoke screenshots for the D3 curve, the gap thresholds and the sparse approach view; the user tunes in game.
2. Re-profile per SDD 52 section 8 against the SDD 50 section 15 baseline (baseline worktree at the pre-#52 commit,
   interleaved runs); 4K and one XR-4 eye recorded only.
3. Docs sync: `GAME_MECHANICS.md` and the in-game Info / Controls text in the same change; `ARCHITECTURE.md`;
   `TECH_DEBT.md` (D21 points item, tunnel CPU item); amend SDD 38 / 49 / 50 in place (SDD 52 section 5).
4. D5: reshoot `Docs/Images/49-lane-tunnel-*.png` and `Docs/Images/50-lane-*.png` at 1920x1080; delete temporary
   shots.
5. Whole-ticket adversarial review; fixes; full suite and smokes; close #52.

**52c progress (2026-11-19): steps 1-4 done, step 5's review left to the main session.** Details in SDD 52 section 13.

- Step 1: tuning shots (variants K1-K3 at 1920x1080, scratch only); the user's decisions: K1 radius fade kept (6 / 1 /
  0.5 px), K2 ring gap fade 6 -> 4 px (was 3 -> 2), K3 detail gap band 10 -> 16 px with the gate at 12.6 px (was
  16 -> 25 round 20), which needs 51 detail slots per side (was 33), so the user raised the interim vertex budget to
  100,000 for 8 lanes (12,500 per lane; D15 amended, #53 still revisits). Applied in `SOLConstants.h`; re-pinned
  `TunnelFarField.Constants` and `.GapFades`, `.SlotMeshBudget`'s comment, `NpcTunnelMath.DotRowSegment` (reach 720 ->
  1,152 Rt) and `.GuideThinning` (on-axis ahead pairs stop at slots 2-3), and `TunnelFarField.DirectPhase` (a ring the
  other side does not draw counts as alpha 0). The mirror and the material read the constants by name, no change.
- Step 2: profile at 1920x1080 (three runs plus a discarded warm-up) and 2560x1440 (record only) on the working tree,
  compared with the SDD 49 section 12c and SDD 50 section 15 figures (no new baseline worktree; SDD 52 section 13).
- Step 3: `GAME_MECHANICS.md` (ring gap, detail band and slots, dot reach), SDD 49 / 50 notes, `TECH_DEBT.md` (window
  ends at the 1 px knee; wider aspect re-worded with 51 slots and the 100,000 budget; the points and CPU items
  re-measured). The in-game Info text quotes no tuned number, so it is unchanged.
- Step 4: README gallery reshot at 1920x1080 (`49-lane-tunnel-approach/inside/earth.png`, `50-lane-*.png`) and two
  new shots `52-lane-tunnel-far-field.png` (1,000 tunnel radii) and `52-lane-tunnel-approach.png` (300 tunnel radii).

## Tests to re-validate

- `SOLTest.NpcTunnelMath.*`: `Constants`, `SlotMesh`, `GuideThinning`, the spacing, warp, phase, coverage, cap /
  end / near / detail / screen fade cases, `TunnelRingScreenFade`, the float path.
- `SOLTest.NpcLaneMath.*`: cases 17 and 17b (`LaneLineFadeWithTunnel`, `LaneShipPointAlpha`).
- `SOLTest.LaneTunnelLabel.*` and `SOLTest.HudEventBus.*` (label snapshot).
- Smokes: `-SOLSmokeNpcTunnel` (39 checks), `-SOLSmokeNpcLanes` (35), `-SOLSmokeNpcTraffic`, `-SOLSmokeLaneMerge`,
  `-SOLSmokeAutoLanes`, `-SOLSmokeOverlay`, `-SOLSmokeHud`.

## Performance gates

At 1920x1080, 1M NPCs, all lanes: tunnel draw <= 0.15 ms GPU; tunnel update + labels <= 0.025 ms CPU on vs off (relaxed from 0.02 ms by the user, 2026-11-19)
(D18); no points gate needed, far ships hidden (D6, D19). 3840x2160 and one XR-4 eye (3840x3744): **recorded only; VR note: no VR work, no
two-eye run.**

## Risks

See SDD 52 section 9: the sparse far view, the vanishing-point pile-up, resolution dependence, the points cost, label
clutter, chevrons under the ring glow, low-segment far rings, the 8-lane budget, HLSL drift, the one asset rebuild
(D16 as amended).

## Change log

- 2026-11-18: first draft with the SDD; blocked on Q1-Q14.
- 2026-11-18: Q1-Q14 answered (SDD 52 D6-D19); block removed, 52a may start with the test-first contract.
- 2026-11-18: 52a implemented (flat lattice, direct phase, reference pixels, pixel-size and gap fades, end rings on the
  curve, five LOD bands, budget constant); `NpcTunnelMath` cases re-validated (table above); 52b next.
- 2026-11-18: 52b implemented (shader rewrite, float mirror and tests, every-lane component, label strength and draw
  fade, map-only lane lines, far ships hidden, far-view smokes); open point: end-ring floor in texture row 3 `.w`;
  SDD 38 / 49 / 50, `ARCHITECTURE.md`, `GAME_MECHANICS.md`, `TECH_DEBT.md` amended; 52c next.
- 2026-11-18: 52b review fixes (SDD 52 change log): `M_SOLNpcTunnel` rebuilt with `NpcTunnelLayout4` (user decision,
  D16 amended; `create_npc_assets.py -SOLRebuild -SOLOnly=M_SOLNpcTunnel`), the sub-pixel ring ribbon clamp
  (`RingRibbonInnerExtentPx`), label dot rows of every drawn lane in reach (`IsDotRowInReach`), segment distance once
  per lane, the vertex budget warning, test pins (end rings at 0.7 / 0.5 px, far end rings gone, mirror at the longest
  lane), doc corrections; the gap fade versus glow width is a tuning note for 52c.
- 2026-11-19: 52c: the user's tuning (K1 kept, K2 ring gap 6 -> 4 px, K3 detail band 10 -> 16 px, 51 detail slots,
  budget 100,000 for 8 lanes, D15 amended), tests re-pinned, profile, gallery reshoot plus two far-field shots, docs.
