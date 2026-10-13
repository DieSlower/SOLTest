<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 13 — Star field quality: realistic size, sharpness and brightness

Ticket `#13`. Plan: [`Docs/Plans/13-star-field-quality-plan.md`](../Plans/13-star-field-quality-plan.md). Follows SDD 5 (star field, issue #5). Decisions resolved with the user in a `grill-me` session on 2026-10-12.

## 1. Problem

Observed in play: the generated (sprite) stars look larger than real stars, the stars on the cubemap texture are too blurry, and everything seems too bright.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Approach | Staged. Stage A (first): keep the current layers (45,653 bright-star sprites plus the faint-star cubemap) and fix them: clamp sprite apparent size, correct the magnitude-to-brightness mapping, re-sample/re-bake the cubemap sharper with proper filtering and dim it. Stage B (only if stars are still too big after A): sub-pixel, flux-conserving points; evaluate replacing the cubemap with procedural/catalogue points (rank 3 on cost) only after measuring what it costs. |
| 2 | Exposure | Stars follow the scene's auto-exposure (eye-adaptation realism: they fade when a bright planet or the Sun fills the view) with a bounded min/max exposure range so they never blow out in dark sky. The existing exposure setup is the starting point; the root cause of "too bright" must be established (star mapping vs adaptation in empty space) before tuning. |
| 3 | Cost | Runtime cost must stay at or below today's (one static instanced draw plus one cubemap pass, nothing per frame). Any stage B step is measured (GPU ms) before it is adopted. |

## 3. Acceptance

- Before/after screenshots at several FOVs; bright stars read as sharp points at 1:1, faint field not blurry; brightness follows magnitude and the adaptation behaviour in decision 2.
- A documented magnitude-to-pixel-brightness mapping in this SDD, with any new pure mapping functions unit-tested.
- Measured GPU cost recorded in an amendment.

## 4. Open questions

Stage B scope (sub-pixel points and/or procedural) is decided after stage A's screenshots. (Answered in Amendment 1, A1.4: not needed for size or sharpness.)

## Amendment 1 — Stage A as built (13a diagnosis, 13b fixes), 2026-10-12

A later requirement from the user is folded in here: stars must not change apparent size when the camera zooms or the FOV changes (they are point sources, ~1-2 px), on both layers, independent of resolution and screen percentage.

### A1.1 Diagnosis (13a)

Reproduced with standalone `-game` runs at 1280×720 (`-SOLSpectator -SOLStart=Earth`, plus `-SOLLookAt=Jupiter` for dark sky, `-SOLLookAt=Sun`, and Earth filling the view). Before screenshots: `Saved/StarShots/before_dark.png`, `before_dark_fov30.png`, `before_sun.png`, `before_earth.png`, `before_dark_noadapt.png` (local, `Saved/` is not committed). Measured from the PNGs with a small numpy script (peaks, linear peak values, half-maximum areas).

| Symptom | Cause found |
|---|---|
| Too bright | **The magnitude mapping, not exposure.** The exposure was fixed (`ASOLBodyVisuals`: `AEM_Manual`, bias 0; `SOL_Test` has no post-process volume), and a dark-sky shot with `ShowFlag.EyeAdaptation 0` gave the same statistics as the normal one. SDD 5 put flux 1.0 at **V 8** (below naked-eye visibility) at about one white pixel, so every naked-eye star saturated and the cube's V 8-10 stars formed a grey haze: dark sky at 90° had 8,471 peaks above 0.01 linear, 1,445 above 0.5 display, background 0.017, mean display 0.060. |
| Too large | Sprites were a fixed **0.006 rad** across (~6 px at 1080p, 4 px at 720p) with a soft falloff: the brightest 40 had a 21 px half-maximum area at 90°, and because the size was angular they grew with zoom (79.5 px at 30°). |
| Blurry cube | The cube's Skybox texture group uses a point mip filter: at 1280×720 / 90° the LOD (~0.6) rounds to **mip 1** (half resolution); when magnified (narrow FOV) the bilinear-splatted 2×2 texel blobs grow with the zoom. The texture settings themselves are fine (2048²/face, BC6H, sRGB off, LOD bias 0, no max size). |

### A1.2 Decisions as built (13b)

1. **Pixel-flux units.** A star's *pixel flux* (the sum of its pixels' emissive values at the dark-sky exposure, before tonemapping) is `flux × BrightnessScale` at every FOV, resolution and screen percentage. Both layers draw a star as the same kernel `saturate(1 - (d/R)^2)^2` of diameter `STAR_POINT_DIAMETER_PX` = **2.5 output pixels** (half-maximum width ~1.35 px), so its peak is `flux × scale / (π R² / 3)` (`SOLStarBrightness::PointPeakPerFlux`). Zooming changes neither the size nor the brightness of a star (naked-eye behaviour); only the number of stars in view changes. This replaces SDD 5's fixed angular size and its "emissive × solid angle" contract, which makes point stars brighten as the FOV narrows.
2. **Magnitude mapping.** `BrightnessScale = 10^(-0.4 (FluxReferenceMagnitude − STAR_FIELD_WHITE_MAGNITUDE))` with **`STAR_FIELD_WHITE_MAGNITUDE` = 2.5**: a V 2.5 star's pixels sum to 1.0 (one display-white pixel's worth). Pogson below it: pixel flux = `10^(-0.4 (V − 2.5))`, peak = 0.61 × that. Calibration (pinned by `SOLTest.StarBrightness.Calibration`): the naked-eye limit V 6.5 peaks at 0.0153 linear (just visible on an sRGB display after the filmic toe), V 8 (the sprite/cube split) at 0.0039 (threshold), and the peak reaches 1.0 at V ≈ 2.0, so about 50 stars on the whole sky saturate. Measured: ~900 stars above 8/255 in a 90° × 59° frame, matching the ~1,000 naked-eye stars expected in that 11% of the sky. (A first pass at V 1.0 was judged against a mis-set exposure, see 5, and was far too dim at the real one.)
3. **Sprites** (`M_SOLStarSprite`): quads are built at `STAR_SPRITE_ANGULAR_DIAMETER_RAD` = 0.02 rad (bounds only) and the vertex shader resizes each one every frame with world position offset (instance-space offset × (kernel / built size − 1)). Kernel angular diameter = `PointDiameterPx` × pixel angle, with pixel angle = `2 / (ViewToClipNoAA[0][0] × output width)` × `cos^1.5` of the angle off the view axis. Output width is the render width over the resolution fraction, so screen percentage does not change the size (caveat: with a non-temporal upscaler at a low screen percentage the kernel is under 2 render pixels and can shimmer; TSR resolves it). Off axis a rectilinear pixel subtends cos² of the center angle radially and cos tangentially (solid angle cos³); the `cos^1.5` factor is area-preserving: the kernel keeps the same pixel area, so the same peak and pixel flux, across the frame. It does not make edge stars round: they stay stretched radially by 1/√cos on screen (about 1.5× at the 135° warp's edges). The resize factor is clamped at 0 so a quad never grows past its built size (its bounds).
4. **Cube** (`M_SOLStarSky`, Custom HLSL), chosen **per pixel** (`SOLStarBrightness::CubePointBlend` of that pixel's own angular size against the local texel's; texels shrink as (1 + s²)^-0.75 toward face edges): while the pixel is at least one texel across (blend 0; 1 at ≤ 0.8 texel) it takes **one hardware-filtered sample** with the cube's own sampler, as SDD 5 did, times `pixel sr / CubeTexelSolidAngleSr` (pixel flux; the sr is clamped at 1e-12 in HLSL, and `ASOLStarField` skips the sky with an error if the data's face size or texel sr is not positive). Where texels are magnified (narrow FOV, high resolution, or the off-axis parts of a 1080p 90° frame) it **rebuilds point stars from the bake's bilinear splat**: the pixel fetches its own texel cell (4 texels) and, unless that cell's light above the local background (the lower of the cell minimum and a point-sampled mip-2 value, so a star centered in the cell is not mistaken for background) would peak below 1e-3 linear (invisible), the 4×4 texels around it. The per-channel minimum is the diffuse level (drawn as a faint glow), and each of the 3×3 cells whose 2×2 sum is a local maximum holds a star (flux = sum − diffuse, position = weighted centroid), drawn as the same kernel as the sprites. The local-maximum test removes the ghost points that cells sharing part of a splat drew in a first version. Texels past a face edge are fetched along their own unclamped direction, which lands on the neighbouring face, so no texel is counted twice at a seam (the clamped version double-counted there).
5. **Exposure** (decision 2, revised after the adversarial review with the user): `ASOLBodyVisuals` keeps **`AEM_Manual`** and sets the exposure bias itself every universe update (`UpdateExposure`, pure math in `Visuals/SOLSceneExposure`, tested in `SOLTest.SceneExposure.*`). It does not use the engine's eye adaptation, for two reasons. While the ship moves fast the render-origin rebase sets `bPrevTransformsReset` every frame, which forces the histogram to its target each frame (no adaptation, it snaps). And the histogram only reacted to the Sun, never to a lit planet. The estimate uses only what is known about the scene:
   - **View mean radiance** = Σ over bodies of (share of the view covered by its disc) × (its mean radiance). The share is the disc's solid angle over the view's (`4 asin(sin a sin b)`), with the disc counted by how much of its diameter overlaps the equal-solid-angle view cone. Mean radiance = emissive luminance + mean albedo luminance (halfway between its two appearance colors) × `SunIlluminanceLux / π` × the Lambert disc mean `(2/3)(sin α + (π − α) cos α)/π` at phase angle α, i.e. what `M_SOLBody` draws (ambient term left out). The sky contributes ~0.
   - **Target** = `EXPOSURE_MIN_EV100` (0, the old fixed exposure) while the mean is at or below `EXPOSURE_KEY_RADIANCE` = 0.05, `0 + log2(mean / 0.05)` above it, capped at `EXPOSURE_MAX_EV100` = 2 (4×). Bias = −EV.
   - **Smoothing**: `ev = target + (ev − target) e^(−dt/τ)`, τ = `EXPOSURE_TIME_CONSTANT_SECONDS` = 0.4 s (within 10% in ~1 s), frame-rate independent; the first frame starts at the target.

   Effect (screenshots with `ShowFlag.EyeAdaptation 0` as the reference, which disables the bias): dark sky unchanged (bias 0); Earth from 1,500 km, lit and filling the view, ≈ 1.7 EV darker; Jupiter from 50,000 km ≈ 2 EV (cap); the Sun from 0.13 AU (`-SOLStart=Sun -SOLAltitudeKm=20000000`) ≈ 1.4 EV, so the stars around it fade; the Sun from 0.4 AU or more (under 3e-4 of the view) does not adapt. As the user accepted, adapting also dims the bright body itself. The first version (histogram exposure, with min/max converted for `r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange` = off) and its `BoundedExposureScale` / `AutoExposureBrightnessSetting` helpers are gone.
6. **Verification flag `-SOLStarSmoke=<fov>,...`** (`ASOLGameMode`): locks look input once, sets each FOV in turn and takes one screenshot of the same view per FOV (separate runs drifted with the mouse, so the same stars could not be compared), then quits on its own timer (a pending `-SOLSmokeShot` is left alone). The lock only holds for `-SOLSpectator`: the ship pawn's Enhanced Input look does not go through the controller's look input.

### A1.3 Results

FOV series (`-SOLStarSmoke=5,20,45,60,90,110,135`, 1280×720, `Saved/StarShots/after_fov*.png`), the same stars tracked across FOVs (linear peak / half-maximum area):

| Star | 135° | 110° | 90° | 60° | 45° | 20° |
|---|---|---|---|---|---|---|
| A | 0.68 / 2 px | 0.66 / 1 px | 0.59 / 3 px | 0.59 / 2 px | 0.56 / 4 px | 0.56 / 2 px |
| B | 0.58 / 2 px | 0.53 / 2 px | 0.47 / 2 px | 0.43 / 2 px | 0.44 / 3 px | — |
| C | 0.30 / 1 px | 0.26 / 1 px | 0.26 / 1 px | 0.23 / 1 px | 0.21 / 2 px | — |
| D (faint) | 0.058 / 1 px | 0.027 / 3 px | 0.033 / 3 px | 0.026 / 2 px | 0.048 / 1 px | 0.032 / 2 px |

The brightest 40 have a **2 px median half-maximum area (max 4-5) at every FOV from 20° to 110°** (before: 21 px at 90°, 79.5 px at 30°). The spread in peak is sub-pixel sampling phase (TSR); summed flux stays within about ±10%. At 135° the whole-frame statistic is dominated by the Sun entering the frame; at 5° the field holds almost no visible stars, as it should. The `-SOLSmokeJump` warp (135° mid-warp) passes its warp checks; its two failures (`g3` anchor is Deimos, `h` post-warp FOV 92.5° vs 91.9°) are in the arrival and FOV-restore logic, which this change does not touch.

Re-shot after the review fixes (per-pixel blend, seam and early-out changes, own exposure): the same stars match the table within sampling noise (star A 0.60-0.74 peak, 2-4 px at every FOV 20°-135°), and the brightest-40 median half-maximum area is 2-3 px from 20° to 110°. Dark sky at 90°: 134 peaks above 0.01 linear (before 8,471), 9 above 0.5 display (before 1,445), background 0 (before 0.017). After screenshots: `after_fov90.png`, `after_fov20.png`, `after_earth.png`, `after_sun.png`, `after_galactic_fov20.png` / `after_galactic_fov90.png` (toward Venus), exposure pairs `x_<case>.png` / `x_<case>_off.png`; 4× crops `before_dark_crop4x.png` / `after_fov90_crop4x.png`.

**GPU cost** (`ProfileGPU` through `-SOLSmokeConsole`, game window in the foreground, 1920×1080 render; the sky draws in BasePass, the sprites in RenderTranslucency). A/B with SDD 5's sky material swapped back in, same build and view. The window cannot exceed the desktop, so "4K density" is 1080p at FOV 53.13°, which has a 4K 90° frame's pixel angle (2/3840 rad), so the whole frame is in point mode. A real 4K frame has 4× the pixels, so about 4× the difference.

| BasePass, 1080p | SDD 5 sky | Final sky | Difference |
|---|---|---|---|
| 90°, toward Jupiter (sparse sky) | 0.281-0.286 ms | 0.315-0.316 ms | +0.03 ms |
| 90°, toward Venus (Scorpius/Sagittarius, densest direction found) | 0.268-0.273 ms | 0.289 ms (one 0.82 ms outlier) | +0.02 ms |
| 53.13° toward Venus (4K density, all point mode) | 0.199-0.206 ms | 0.258 ms | +0.055 ms (≈ +0.22 ms at real 4K) |
| Same, star search disabled (skip path only, diagnostic) | 0.199-0.206 ms | 0.204-0.205 ms | 0 |

Sprites (RenderTranslucency) 0.040-0.049 ms, unchanged. The skip path costs the same as SDD 5's single sample; the difference is the 16-texel star search in waves that hold a cube star brighter than V ≈ 9.5. At 1080p 90° it runs only off axis, where pixels are smaller than texels; the per-pixel blend the review asked for (instead of one mode per frame) is what brought it into the normal view. **This exceeds decision 3 ("at or below today's") by +0.02-0.03 ms in normal play and about +0.2 ms at 4K**, for sharp point stars instead of blurred, zoom-growing texel blobs; it needs the user's acceptance (A1.5). The exposure is now a few dozen game-thread operations per frame on 9 bodies, with no GPU cost; the first version's histogram pass (~0.026 ms) is gone.

### A1.4 Stage B

Not needed for size or sharpness: both layers already draw 2.5 px point stars at every FOV and resolution. What remains of stage B's motivation is the cube's resolution limit for *faint* stars under strong zoom: positions quantised to the splat; two stars in one texel cell merge into one point; BC6H compression error (shared 4×4 block endpoints) shifts a faint star's flux and centroid a little and can leave a faint false maximum in a block with a bright star; and in the 0.8-1 texel blend zone both modes draw at partial weight, so a star can briefly show as a faint point plus a faint filtered smudge (a "ghost") while zooming through it. Those stars are at or below visibility with the new mapping. Revisit only if a high zoom (e.g. a telescope view) is added.

**Accepted: the faint background is not FOV-invariant.** Point stars keep their size and brightness at any zoom, but the unresolved background does not behave like a point source. In filtered mode a pixel's value is the summed flux of every faint star in it, so a wide FOV or low resolution (more sky per pixel) makes the background glow brighter per pixel. In point mode the diffuse level is drawn as glow while resolvable stars are split out as points. That is the physically expected surface-brightness behaviour of unresolved light, at or below the display threshold here, and is left as is.

### A1.5 Open points for the user

- Decision 3: accept the sky's extra cost (+0.02-0.03 ms at 1080p 90°, about +0.2 ms in full point mode at 4K, A1.3) or trade some of it back (a higher skip level drops more faint cube stars; a per-frame mode instead of per pixel brings back blurry off-axis texels).
- `STAR_FIELD_WHITE_MAGNITUDE` (2.5), `STAR_POINT_DIAMETER_PX` (2.5), `EXPOSURE_KEY_RADIANCE` (0.05) and `EXPOSURE_TIME_CONSTANT_SECONDS` (0.4) are the look knobs.
