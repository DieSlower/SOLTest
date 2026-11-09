<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# VR performance targets (stay-above numbers for a game that will not be tested on a headset)

**Project decision (2026-11-04): the stay-above target is the Varjo XR-4, see section 5a. The Index-tier figures
(section 5b) are secondary.**

Research date: 2026-11-04. Purpose: SOLTest has no VR support and the developer has no headset, but the game should hold
frame rates that a VR player would need, so that a future VR port (or just a very high-fps flat player) is not blocked by
performance. This file derives a "stay above" target and how to check it on a flat monitor.

Related: [`UE_PERFORMANCE_GUIDELINES.md`](UE_PERFORMANCE_GUIDELINES.md) is the C++/Mass/Nanite/Niagara review checklist. It
only touches VR in one line (item 61: Nanite is not for forward/VR rendering) and has no frame-time or resolution
targets, so this file does not overlap it; use the two together (the checklist says how to be cheap, this file says how
cheap is enough).

Confidence notes: panel specs come from Meta, HTC, Sony, Road to VR and Epic pages (linked). Where a figure came from a
forum or a single third-party estimate it is marked "(unverified)". Derived numbers are my own arithmetic.

## 1. Headset table

Panel = native display resolution per eye. Render = the per-eye target the runtime asks the game for by default (before
the user's supersampling slider). Pixels are millions, both eyes, for the panel (P) and the render target (R).

| Headset | Panel / eye | P (Mpx, 2 eyes) | Default render / eye | R (Mpx, 2 eyes) | Refresh rates | Notes |
|---|---|---|---|---|---|---|
| Meta Quest 2 | 1832x1920 | 7.03 | below panel at 1.0 scale (exact figure not found) | - | 72 / 90 / 120 | Standalone (Snapdragon XR2 Gen 1) |
| Meta Quest 3S | 1832x1920 | 7.03 | same panel as Quest 2 | - | 72 / 90 / 120 per Meta (UploadVR and Road to VR say 120 max) | Standalone (XR2 Gen 2) |
| Meta Quest 3 | 2064x2208 | 9.12 | 1680x1760 at render scale 1.0 (Meta docs, Unreal) | 5.91 | 72 / 90 / 120 (144 experimental since v70, one source) | Meta says about 1.2x scale matches the panel; standalone, or PC via Link / Air Link / Steam Link |
| Valve Index | 1440x1600 | 4.61 | 2016x2240 at SteamVR 100% (forum reports) | 9.03 | 80 / 90 / 120 / 144 | SteamVR 100% is already about 1.4x per axis over the panel |
| HTC Vive Pro 2 | 2448x2448 | 11.99 | not found | - | 90 / 120 (wired only, 90 over the wireless adapter) | |
| HTC Vive XR Elite | 1920x1920 | 7.37 | not found | - | 90 | Standalone or PC streaming |
| HP Reverb G2 | 2160x2160 | 9.33 | not found | - | 90 | WMR, reprojection handled by the WMR runtime |
| PlayStation VR2 | 2000x2040 | 8.16 | varies; many titles far below panel, foveated (eye tracking) | - | 90 / 120 | Console, fixed hardware; analysis of shipped games shows wide variation |
| **Varjo XR-4 series** (XR-4, Focal, Secure) | **3840x3744** | **28.75** | not published by Varjo (see section 5a) | - | 75 / 90 (Varjo Base setting) | **The project floor.** One mini-LED panel per eye |
| Pimax Crystal | 2880x2880 | 16.59 | not found | - | 72 / 90 / 120, up to 144-160 in beta | High-end PC reference |
| Apple Vision Pro | about 3660x3200 (iFixit-based estimate, unverified) | about 23 (Apple, both eyes) | not applicable (foveated, visionOS compositor) | - | 90 (normal), 96 (24 fps video), 100 (anti-flicker mode) | High-end reference, not a PC-VR target |

Render-resolution norm: runtimes render larger than the panel because the lens distortion pass shrinks the image toward
the edges, so a 1:1 render target looks soft in the centre. SteamVR's 100% (about 1.4x per axis on Index) and the PC
Link slider are applied on top of each other (Link's own render scale multiplies with SteamVR's), so a "100%" in the
tooling can mean very different real resolutions. Epic's own guidance (UE 4.19 onward) is that `vr.PixelDensity = 1`
means the headset's ideal resolution, that `r.ScreenPercentage` should stay at 100 in VR, and that the old
`r.ScreenPercentage = 100` rendered fewer pixels than `vr.PixelDensity = 1`.

## 2. What happens when you miss the frame time

- **Oculus / Meta Rift (PC), Asynchronous SpaceWarp (ASW):** when the app cannot hit 90, the runtime drops the app to
  45 fps and synthesizes every second frame so the headset still gets 90. Meta documents this as "almost halving" the
  CPU/GPU time needed, with artifacts, and head-locked content needs layers. Holding the full rate avoids the artifacts
  entirely. ASW is a safety net, not a target.
- **SteamVR motion smoothing:** the same idea. It extrapolates a synthetic frame when the app misses, and is more likely
  to trigger at higher refresh rates and resolutions (Index 120/144 Hz). Quality on Index is disputed (ghosting reports
  versus "works well" reports); treat it as a fallback.
- **Quest standalone:** the platform pins to a fixed refresh rate; missing it produces judder and Meta recommends
  choosing the lowest rate you can hold (72 or 90) rather than dropping frames. (Behaviour summarized from Meta's
  documentation conventions; not re-verified in this pass.)
- **PSVR2:** supports 90 and 120 Hz with eye-tracked foveated rendering; Sony's reprojection scheme was not confirmed by
  any source found (unverified).
- **Practical minimum:** nobody ships a "minimum acceptable fps" lower than the headset refresh. The sub-refresh modes
  above (45 fps with 90 Hz output, 60 fps with 120 Hz output) are degraded reprojected modes, and the Rift guidance is
  that min-spec machines may run at 45 with ASW while recommended machines hold 90 natively.

## 3. Frame-time budgets

budget (ms) = 1000 / Hz.

| Refresh | Frame budget | Half-rate (ASW-style) app budget |
|---|---|---|
| 60 | 16.67 ms | - |
| 72 | 13.89 ms | 27.8 ms (36 fps) |
| 80 | 12.50 ms | - |
| 90 | 11.11 ms | 22.2 ms (45 fps) |
| 120 | 8.33 ms | 16.7 ms (60 fps) |
| 144 | 6.94 ms | 13.9 ms (72 fps) |

The budget applies to both CPU (game + render thread) and GPU independently, and VR runtimes want some slack for the
compositor (distortion, late-latching, reprojection), so aim at 85-90% of the figure (my rule of thumb).

## 4. Pixel throughput against flat games

Reference flat load: 1080p60 = 2.07 Mpx x 60 = 124 Mpx/s. 1440p60 = 3.69 Mpx x 60 = 221 Mpx/s.

| Scenario (pixels both eyes x Hz) | Mpx per frame | Mpx/s | x 1080p60 | x 1440p60 |
|---|---|---|---|---|
| Quest 3 default render 5.91 Mpx @ 90 | 5.91 | 532 | 4.3 | 2.4 |
| Quest 3 default render @ 120 | 5.91 | 709 | 5.7 | 3.2 |
| Quest 2/3S panel 7.03 Mpx @ 90 | 7.03 | 633 | 5.1 | 2.9 |
| Index SteamVR 100% 9.03 Mpx @ 90 | 9.03 | 813 | 6.5 | 3.7 |
| Index SteamVR 100% @ 120 | 9.03 | 1083 | 8.7 | 4.9 |
| Index SteamVR 100% @ 144 | 9.03 | 1300 | 10.4 | 5.9 |
| Quest 3 panel 9.12 Mpx @ 90 | 9.12 | 821 | 6.6 | 3.7 |
| Reverb G2 panel 9.33 Mpx @ 90 | 9.33 | 840 | 6.8 | 3.8 |
| PSVR2 panel 8.16 Mpx @ 90 / 120 | 8.16 | 734 / 979 | 5.9 / 7.9 | 3.3 / 4.4 |
| Vive Pro 2 panel 11.99 Mpx @ 90 / 120 | 11.99 | 1079 / 1439 | 8.7 / 11.6 | 4.9 / 6.5 |
| **Varjo XR-4, ONE eye 14.38 Mpx @ 90** | 14.38 | 1294 | 10.4 | 5.9 |
| **Varjo XR-4, TWO eyes 28.75 Mpx @ 90** | 28.75 | 2588 | 20.8 | 11.7 |
| Varjo XR-4, one eye / two eyes @ 75 (MR mode) | 14.38 / 28.75 | 1078 / 2157 | 8.7 / 17.3 | 4.9 / 9.8 |
| Pimax Crystal panel 16.59 Mpx @ 90 / 120 | 16.59 | 1493 / 1991 | 12.0 / 16.0 | 6.8 / 9.0 |

Pixel count is a floor, not the whole cost: VR renders two views (culling and draw submission double unless instanced
stereo shares them), has no TAA-style temporal reuse (see section 6), and the runtime adds a compositor pass.

## 5. Recommended "stay above" target

### 5a. THE target: Varjo XR-4 (project decision, 2026-11-04)

The user decided the project's VR performance floor is the Varjo XR-4 resolution and frame rate, tested both as one eye
and as two eyes (the published resolution is per eye).

**Spec (all editions share it; Focal adds autofocus passthrough, Secure is the TAA-compliant radio-free variant; no
"Enterprise" edition was found on Varjo's pages):**

- Panel: 3840 x 3744 **per eye** (14.38 Mpx per eye, 28.75 Mpx both), dual mini-LED, 120 x 105 degree FOV, 51 PPD peak
  at the centre ([Varjo product page](https://varjo.com/products/xr-4), [Varjo resolution article](https://support.varjo.com/hc/en-us/resolution)).
  Road to VR's "14.4MP" figure for the Focal Edition matches 3840 x 3744 as a per-eye count
  ([Road to VR](https://roadtovr.com/varjo-xr-4-announcement-specs-price-release-date/)).
- No separate focus-area or peripheral panel: the XR-4 is one panel per eye. (A reseller page and one review describe a
  1920x1920 "focus" plus 2880x2720 peripheral layout, which is the older XR-3 design; Varjo's own pages contradict it, so
  it is ignored.) The "foveated focus area" in Varjo Base is a rendering/passthrough region driven by 200 Hz eye
  tracking, not a second display.
- Refresh: **90 Hz** (maximum 90 fps, "recommended for VR content") or **75 Hz** (larger foveated focus area, "recommended
  for mixed reality") selectable in Varjo Base 4.3 or later
  ([refresh rate](https://support.varjo.com/hc/en-us/refresh-rate), [frame rate](https://support.varjo.com/hc/en-us/frame-rate)).
  Some third-party listings mention 120 Hz experimental (community wiki only, unverified); not used.
- Render resolution that Varjo Base / SteamVR recommends: **not published**. Varjo's system-requirements page says only that
  the best-performance desktop tier renders at "native XR-4 Series resolution"
  ([system requirements](https://support.varjo.com/hc/en-us/xr-4-series-system-requirements)); its recommended GPUs are
  RTX 5090 / RTX PRO 6000 Blackwell (best) and RTX 3090 / 4080 / 5070 Ti (minimum). **Unsourced here:** any
  supersampling multiplier; the target therefore uses the panel resolution as the render resolution (a lower bound, since
  runtimes usually render above panel size; see section 1).

**Derived numbers (my arithmetic):**

| | Pixels per frame | Mpx/s at 90 Hz | x 1440p60 | Budget at 90 Hz | Budget at 75 Hz |
|---|---|---|---|---|---|
| ONE eye, 3840 x 3744 | 14.38 Mpx | 1294 | 5.9 | 11.11 ms | 13.33 ms |
| TWO eyes, 7680 x 3744 | 28.75 Mpx | 2588 | 11.7 (3.2x the Index figure below) | 11.11 ms | 13.33 ms |

**Test configurations (both required; the 90 Hz budget is the pass line, 75 Hz is not a target):**

| Config | Window / render size | Pixels | Pass criterion |
|---|---|---|---|
| `1eye` | 3840 x 3744 | 14.38 Mpx | p99 frame, Game thread and GPU each <= 10.0 ms (90% of 11.11 ms) |
| `2eyes` | 7680 x 3744 (side-by-side) | 28.75 Mpx | p99 frame, Game thread and GPU each <= 9.5 ms (10.0 ms minus about 5% for the second view's culling and draws that a single wide flat frame does not pay; rule of thumb, calibrate if a headset appears) |

`1eye` isolates per-pixel cost (what a single Varjo eye image costs). `2eyes` is the real pixel load and the harder test;
it is the one that decides pass or fail for the project, while `1eye` helps attribute a miss (pixel-bound if `1eye`
passes by a wide margin and `2eyes` fails on GPU only, CPU-bound if Game is over budget in both). The 7680 width is
within the 8192 texture-size limit of common GPUs but needs enough VRAM for the 28.75 Mpx G-buffer; if a card cannot
allocate it, report that instead of lowering the size.

### 5b. Secondary: PC-VR Index tier (kept for reference)

- Resolution: 2 x 2016 x 2240 (Index at SteamVR 100%, 9.0 Mpx total), also within about 1% of Quest 3 panel size
  (9.1 Mpx) and Reverb G2 (9.3 Mpx).
- Rate: 90 fps, frame budget 11.11 ms, **target p99 <= 10 ms** on both CPU and GPU (about 813 Mpx/s, 3.7x a 1440p60 game).
- Stretch tier: same resolution at 120 fps (8.33 ms, target p99 <= 7.5 ms, about 1.08 Gpx/s). 144 Hz (Index, 6.94 ms) is
  not a target; players at that tier lower the render scale.
- Floor: below 72 fps or above 13.9 ms is a miss even on the weakest PC-VR setups; a reprojected 45 fps is the
  degraded mode, not an acceptable result.

**Standalone note (Quest 2/3S/3, XR2 chips):** these are mobile GPUs, far weaker than a PC card, so a title like SOLTest
(a 1:1 solar system with Mass, Nanite, Niagara) is not realistically a standalone target. If it were: 72 Hz (13.9 ms) at
the default render scale (Quest 3: 1680x1760 per eye, 5.9 Mpx), mobile forward renderer, no Nanite (Nanite is
not for forward/VR rendering, see `UE_PERFORMANCE_GUIDELINES.md` item 61), and a draw-call budget far below the PC one. A
standalone port is a separate project, not something this gate covers.

## 6. Verifying without a headset (UE 5.8)

Nothing here replaces testing on hardware, but pixel throughput and frame time are measurable on a monitor.

0. **Primary (Varjo XR-4) runs.** Run each profile smoke twice: `-ResX=3840 -ResY=3744` (`1eye`) and
   `-ResX=7680 -ResY=3744` (`2eyes`), `r.ScreenPercentage 100`, a real RHI (no `-NullRHI`), packaged or `-game`. Record
   p99 frame, Game and GPU times per scene and compare to the section 5a criteria. The Index-equivalent steps below are the
   secondary check.
1. **Render at the equivalent resolution.** A side-by-side flat window of 4032 x 2240 equals 2 x 2016 x 2240 (Index tier). Run the
   game windowed or fullscreen at that size, e.g. `UnrealEditor-Cmd` / packaged exe with `-ResX=4032 -ResY=2240`
   (or `r.SetRes 4032x2240`) and `r.ScreenPercentage 100` (flat mode, so the cvar is fine; in a real VR session Epic says
   to use `vr.PixelDensity` instead). A cheaper first pass is 100% on a 4K display (3840x2160, 8.3 Mpx, about 92% of the target).
2. **Measure, do not guess.** `stat unit` (Game / Draw / GPU, versus the 11.1 ms budget), `stat gpu`, `ProfileGPU`, and
   Unreal Insights for the p99 frame, not the average. The existing profile smokes (`-SOLSmoke*`, e.g. the
   holo-menu profile smoke that reads `stat dumpave` and `ProfileGPU` back from the log) can be run at that resolution to
   give per-scene numbers. Use packaged or `-game` builds; editor viewport numbers are pessimistic.
3. **Add a stereo margin.** A flat frame at 9 Mpx does not pay for two views' culling and draws. Require roughly 15%
   headroom (my rule of thumb, to be calibrated if a headset ever becomes available): flat 9 Mpx p99 <= about 9.5 ms
   for the 90 fps target.
4. **Renderer features that differ in VR** (Epic forward-renderer documentation): the forward renderer is the VR
   recommendation (Epic measured about 22% faster than deferred in Robo Recall on a GTX 970, an old datapoint) and has
   Instanced Stereo support, but has no screen-space techniques (SSR, SSAO, contact shadows). MSAA is preferred over TAA
   because head-tracking motion blurs TAA, and costs about 25% GPU time versus TAA (Epic's figure; `r.MSAACount`).
   SOLTest targets Nanite and current UE rendering features on the default deferred path (the renderer configuration was
   not audited for this note), which are not VR-friendly, so the flat measurement at the VR resolution is an optimistic proxy: a real VR build would switch renderer and lose some effects.
   Test the flat build with the cheapest AA to avoid overstating headroom.
5. **UI/HUD in VR.** Screen-space UMG does not exist in a headset; the HUD, menus and map would need world-space widgets
   (Widget Component) or stereo layers (the compositor draws the layer at panel resolution without the game's render
   cost). No source was found in this pass for the UE 5.8 specifics; check Epic's VR docs when a VR ticket exists.

## 7. What this implies for menu and HUD budgets

- Current budgets (#42 holo menu: 0.1 ms; #46 solid menu: 0.2 ms CPU since 2026-11-06, was 0.1 ms, and 0.3 ms GPU)
  were set against a flat frame. Against 11.11 ms those are 1.8% CPU and 2.7% GPU of a 90 Hz frame (3.6% at 120 Hz),
  which is acceptable on its own.
- **Rescaled menu/HUD GPU budgets for the Varjo XR-4 target.** The resolution the #42/#46 budgets were measured at is
  not recorded in this note (assumed 1440p, 3.69 Mpx; verify). If the cost is fill-bound it scales with pixel count:
  0.3 ms x (14.38 / 3.69) = about 1.2 ms in `1eye`, and x (28.75 / 3.69) = about 2.4 ms in `2eyes` (10.8% / 21% of the
  11.11 ms frame, which is too much for a menu). Provisional ceilings until re-measured: solid menu (#46) <= 1.2 ms GPU
  at `1eye` and <= 2.4 ms at `2eyes`; holo menu (#42) scaled the same way from its own flat figure. CPU stays at the flat budget. The
  re-measurement at 3840x3744 and 7680x3744 replaces these; if the measured cost exceeds the ceilings, the UI moves to a
  stereo layer or a low-resolution world-space quad (below).
- (Older Index-tier reasoning.) GPU cost of full-screen UI scales with pixels. If the 0.3 ms was measured at 1440p (3.69 Mpx), the same material
  fill-bound at 9.03 Mpx is about 0.3 x 2.45 = 0.73 ms, and about 1.5 ms if the UI is drawn once per eye (2 views). Re-measure the UI smokes at
  4032x2240 before claiming the budget holds; if it does not, the UI becomes a stereo layer or a small world-space quad
  rendered to its own low-resolution target once per frame (and not per eye), so its cost is decoupled from the headset pixel count.
- CPU cost does not scale with resolution but does double with two views if the UI is drawn twice, so 0.1 ms stays 0.1 ms
  only for a layer/quad approach.
- **Measured (#46 part 46h, 2026-11-06, RTX 2080 Ti; SDD 46 section 13.2).** The #46 budget is now 0.2 ms CPU (moved
  by the user from 0.1 ms on 2026-11-06) and 0.3 ms GPU, and it was measured at 1920x1080 and 1280x720 (not 1440p as
  assumed above); the provisional ceilings stay 1.2 / 2.4 ms as set. The solid menus' own GPU (the game window's Slate
  UI pass) is 0.08-0.54 ms at 4K (3840x2160), 0.21-0.90 ms at one XR-4 eye (3840x3744) and 0.21-1.12 ms at two eyes
  (7680x3744, three screens of one run), all under the ceilings, so the stereo-layer / quad fallback is not needed for
  the menus. Their CPU extra is +0.01 to +0.16 ms against the flat baseline at every size, under 0.2 ms.
- **The whole frame does not reach either line here.** At 4K the p99 frame is 16.0-17.7 ms with a menu open, and at
  one XR-4 eye 27-33 ms, GPU-bound by the scene behind the menus (the game thread is 4.4-6.7 ms). That is project-level
  VR work (section 7a, `Docs/ToDo/vr-performance-gate.md`), not a menu cost.
- **Measured (#33 part 33f, 2026-11-09, RTX 2080 Ti; SDD 33 section 14.7).** The Help page of both menus (the Keyboard tab with the live diagram, with and without the hover popup, and the four HUD glossary tabs) in three runs each at 4K and one XR-4 eye (two eyes skipped, user decision): the menu's GPU is 0.43-0.59 ms at 4K and 0.70-1.00 ms at one eye (the pause menu's Keyboard tab with the popup the highest, 84% of the 1.2 ms ceiling), at most +0.09 / +0.11 ms over its menu's Settings screen at the same size; the CPU extra is +0.06 to +0.10 ms against the flat baseline. Inside the ceilings and the 0.2 / 0.3 ms budget. The 4K p99 frame is 15.7-17.3 ms (`proxy=MISS`, scene-bound as above). At the eye's near-square aspect the menus are laid out narrow and the Help page clips (outside its wide-screen design, `Docs/TECH_DEBT.md`), one more reason for a wide quad or stereo layer in VR.

### 7a. Proxy on this machine (user decision, 2026-11-06)

The development GPU (RTX 2080 Ti) is below Varjo's minimum, so the real XR-4 resolutions (3840x3744 per eye, 7680x3744
two eyes) cannot pass here: the scene alone costs about 25-32 ms of GPU at one eye (p99, with a menu open). Stand-in
rule: the game counts as VR-ready if it runs at 4K (3840x2160) at 100 Hz or better (frame p99 <= 10.0 ms) on this
machine. Optimisation for real XR-4 hardware happens later, on a different card. XR-4 one-eye and two-eye measurements
already taken stay recorded as informational. `-SOLSmokeSolidMenuProfile -ResX=3840 -ResY=2160` logs `proxy=PASS|MISS`
per pass; on 2026-11-06 it read MISS on all nine screens (p99 frame 16.0-17.7 ms, about 60 fps). On 2026-11-09 (#33 part 33f, 21 screens with the Help page) it read MISS on all of them again (15.7-17.3 ms).

## 8. Sources

- Varjo XR-4 product page (3840 x 3744 per eye, 120 x 105 FOV, 51 PPD, editions): <https://varjo.com/products/xr-4>
- Varjo support, resolution in VR/XR: <https://support.varjo.com/hc/en-us/resolution>
- Varjo support, refresh rate (75 / 90 Hz): <https://support.varjo.com/hc/en-us/refresh-rate>
- Varjo support, frame rate (aim for 90 fps): <https://support.varjo.com/hc/en-us/frame-rate>
- Varjo support, XR-4 system requirements (GPUs, native resolution): <https://support.varjo.com/hc/en-us/xr-4-series-system-requirements>
- Varjo blog, XR-4 series refresh (Focal / Secure, no Enterprise edition mentioned): <https://varjo.com/blog/why-were-refreshing-the-xr-4-series-for-mission-critical-training>
- Road to VR, XR-4 announcement (14.4 MP, 90 Hz, 51 PPD): <https://roadtovr.com/varjo-xr-4-announcement-specs-price-release-date/>
- Meta Quest comparison pages (panel 2064x2208 for Quest 3, 72/90/120 Hz): <https://www.meta.com/gb/quest/compare/>
- Mixed News, Quest 3 display details: <https://mixed-news.com/en/new-details-about-the-meta-quest-3-display/>
- UploadVR, Quest 3 resolution confirmed: <https://uploadvr.com/quest-3-resolution-confirmed-best-buy/>
- VR AR Wiki, Quest 3 (144 Hz experimental): <https://vrarwiki.com/wiki/Quest_3>
- Road to VR, Quest 3S vs Quest 3 vs Quest 2 specs: <https://roadtovr.com/quest-3s-quest-3-quest-2-specs-compared/>
- UploadVR, Quest 3S specs: <https://uploadvr.com/quest-3s-specs>
- Meta Unreal OS render scale (1680x1760 at 1.0 on Quest 3, ~1.2 matches the panel): <https://developers.meta.com/horizon/documentation/unreal/os-render-scale>
- Road to VR, SteamVR motion smoothing: <https://roadtovr.com/steamvr-motion-smoothing-asw-alex-vlachos/>
- UploadVR, SteamVR motion smoothing on AMD / Index: <https://uploadvr.com/steamvr-index-motion-smoothing-amd>
- Flight Simulator forum, Index 100% = 2016x2240 (forum report): <https://forums.flightsimulator.com/t/my-2070-super-vr-settings-and-suggestions-index-steamvr/321913>
- Meta developer blog, Asynchronous Spacewarp: <https://developers.meta.com/horizon/blog/asynchronous-spacewarp/>
- PCPer, ASW launch (45 fps mechanism): <https://pcper.com/2016/11/oculus-launches-asynchronous-spacewarp-45-fps-vr/>
- Engadget, ASW: <https://www.engadget.com/2016-10-06-you-don-t-need-a-ridiculous-pc-to-run-oculus-rift-anymore.html>
- HTC, Vive Pro 2 specs: <https://www.vive.com/us/product/vive-pro2/specs>
- HTC, Vive XR Elite (1920x1920, 90 Hz): <https://myshop.vive.com/vive_it/1920554.html>
- Road to VR, HP Reverb G2 review (2160x2160, 90 Hz): <https://roadtovr.com/hp-reverb-g2-review/>
- Road to VR, PSVR2 specs (2000x2040, 90/120 Hz, foveated): <https://roadtovr.com/sony-playstation-vr-2-announcement-psvr-2-specs-field-of-view/>
- Mixed News, PSVR2 game render resolutions: <https://mixed-news.com/en/playstation-vr-2-games-render-resolution-spreadsheet/>
- Pimax, Crystal Sim refresh rates: <https://pimax.com/pimax-launches-crystal-sim-more-refresh-rate-options-no-controllers-updated-appearance/>
- VR AR Wiki, Pimax Crystal: <https://vrarwiki.com/wiki/Pimax_Crystal>
- UploadVR, Apple Vision Pro specs: <https://uploadvr.com/apple-vision-pro-specs/>
- Mixed News, Vision Pro refresh rates: <https://mixed-news.com/en/apple-vision-pro-can-break-90-hertz/>
- Epic, forward shading renderer (MSAA, instanced stereo, limitations): <https://dev.epicgames.com/documentation/unreal-engine/forward-shading-renderer-in-unreal-engine>
- Epic, VR resolution settings in 4.19 (`vr.PixelDensity`): <https://www.unrealengine.com/en-US/blog/significant-changes-coming-to-vr-resolution-settings-in-4-19>
- Meta developer blog, Optimizing the UE4 renderer for VR: <https://developers.meta.com/horizon/blog/introducing-the-oculus-unreal-renderer/>
- Evetech, PC VR optimisation (Link render scale stacking, secondary): <https://www.evetech.co.za/optimize-pc-vr-gaming/e/3802>
