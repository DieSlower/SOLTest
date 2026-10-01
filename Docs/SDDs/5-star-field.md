<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 5 — Star field

Ticket `#5` ("Part 4: Star field (AT-HYG)"). Plan of record:
[`Docs/Plans/5-star-field-plan.md`](../Plans/5-star-field-plan.md).

Refines SDD 1's Star field row and the supporting research in
[`Docs/Research/STAR_FIELD_CELESTIALVAULT_AND_ATHYG.md`](../Research/STAR_FIELD_CELESTIALVAULT_AND_ATHYG.md).
Must not contradict [`Docs/SDDs/1-solar-system-architecture.md`](1-solar-system-architecture.md).
All decisions below were resolved with the user during a `grill-me` session on 2026-09-30.

---

## 1. Problem

Give the ship a real, ~2.55-million-star sky (the AT-HYG catalog) that looks
physically correct from anywhere in the solar system, at frame-rate cost close to
zero, without rendering 2.5 million individual draw primitives.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Catalog sourcing | AT-HYG is fetched fresh (not present anywhere in the project or `D:\MediaLib`) from its public, actively-maintained release: **`https://codeberg.org/astronexus/athyg`** (the project moved off GitHub to Codeberg; the GitHub mirror is no longer maintained), file `data/athyg_40.csv.gz` (confirmed during 4a — the repo's own README inconsistently calls it `athyg_v40.csv.gz` in places; the real path has no `v`). The file is stored in Git LFS, so the working download URL is the `/media/branch/main/...` path, not `/raw/...` (which only returns a 134-byte LFS pointer). 2,558,654 rows, 199.7 MB compressed. **HYG v4.4** (`https://codeberg.org/astronexus/hyg`, same author/license, also Git LFS) is used as a secondary source for the ~500 brightest stars' Johnson V/B-V, since Tycho photometry saturates for them (see the revision history below). Both are licensed **CC BY-SA 4.0** — using them requires attribution, and a derivative work (our baked cubemap/bright-star data) must carry the same license. This doesn't block building/testing, but needs an attribution line in whatever credits/info screen Part 7 builds, and is a real consideration if this game ever ships commercially (flagged to the user, not unilaterally resolved here). |
| 2 | Hybrid rendering split (reaffirmed) | Bright stars (mag < 8, exact count depends on the real data — expect roughly 40,000-50,000, not the ~9,000 naked-eye figure) render as individual sprites; everything fainter is baked offline into an HDR cubemap. Rendering all 2.5M individually (by any technique) was considered and rejected: the vast majority are sub-pixel at any reasonable resolution, causing aliasing/shimmer no instancing technique fixes, and 2.5M overlapping additive-blended quads is real overdraw cost that a single O(1)-per-pixel cubemap sample avoids entirely. |
| 3 | Bright-star rendering technique | **Instanced Static Mesh** (`UInstancedStaticMeshComponent`), camera-facing quads, one instance per bright star, custom data per instance (direction, magnitude-derived size/brightness, color) — reusing CelestialVault's proven technique at a similar scale, not Niagara. At tens of thousands of *static, never-simulated* instances, Niagara's per-frame simulation/compute-dispatch machinery buys nothing (nothing here ever moves or needs GPU simulation) and is strictly more overhead than a plain instanced draw call. Niagara would be the better *instancing* technology only if literally rendering millions of dynamically-culled sprites (decision 2's rejected path) — moot once that path was rejected. |
| 4 | Diffraction spikes | **Deferred.** Bright stars render as simple round/soft sprites for this part; no cross/starburst flare. Tracked as a follow-up ([`Docs/ToDo/star-diffraction-spikes.md`](../ToDo/star-diffraction-spikes.md)) — addable later by swapping the material without touching the data pipeline. |
| 5 | CelestialVault reuse | **Copy the needed assets, don't enable the plugin.** `T_MilkyWay` and the handful of needed material functions (`MF_BillboardSizeByPixelUnits`, `MF_ScalePlaneToMinScreenPixels`, `MF_DirectionToLatLong`, and the star quad mesh/material as a reference for decision 3) are copied into `Content/SOL/` by the project's own content-creation tooling. `SOLTest.uproject` never lists CelestialVault or its DaySequence dependency — no beta-plugin risk, no unrelated actor/tick machinery ever loaded, no ongoing coupling to how that plugin changes. |
| 6 | Offline pipeline architecture | **External Python (numpy) does the heavy numeric work; a small Unreal-Python step imports the results.** Splatting ~2.5M stars' flux onto cubemap texels correctly (spreading each star's light across the texels its actual sub-pixel position covers, not just dumping it into the nearest texel) needs vectorized math to run in reasonable time — numpy does the whole catalog in seconds. Doing the same rigorous per-star computation in Unreal's embedded, non-numpy Python would be impractically slow, creating real pressure to cut the algorithm's quality (nearest-texel-only splatting, lower precision) just to make it finish — exactly the corners that would visibly degrade the baked result. The external step writes bright-star data (a compact binary/CSV) and 6 cubemap face images (HDR); a small script in the `Tools/CreateSOLContent.py` style then imports those into a `UDataAsset` and a `TextureCube` via Unreal's `unreal` Python module. |
| 7 | Bake parameters | Magnitude cutoff **8.0** (matches SDD 1's "~mag < 8" as an exact value). Cubemap **2048×2048 per face** (6 faces), compressed HDR (BC6H) — standard skybox-quality resolution; the Milky Way's diffuse glow has no sharp detail to resolve, so higher resolution buys little. |
| 8 | Star color | AT-HYG's `ci` column is **not** B-V for 99.9% of rows (those sourced from Tycho, `mag_src=T`): it is Tycho `BT-VT`, a different photometric system, confirmed during 4a (e.g. Aldebaran's `ci` is 1.777 against its true B-V of 1.54). Converted via the standard Tycho-to-Johnson relations from ESA's Hipparcos and Tycho Catalogues (1997), Vol. 1 §1.3 eq. 1.3.20 (`V = VT - 0.090*(BT-VT)`, `B-V = 0.850*(BT-VT)`, with `BT-VT` clamped to [-0.3, 2.4] against noisy faint-star outliers) before the B-V-to-RGB step (a standard blackbody approximation: Ballesteros 2012 for B-V→temperature, then a Planck-law/CIE-1931 fit for temperature→RGB). Done once at bake time, not a per-frame runtime cost. Rows without color data (a small fraction) fall back to a spectral-type-based mean B-V, then neutral white as a last resort. |
| 9 | Stellar proper motion | **Not modeled.** Stars are baked at a fixed epoch (J2000, matching every other astronomical fact in this project) and never move relative to each other, for the life of the game. Real proper motion is imperceptible at any timescale a play session or even a very long time-warp session would reach; this is the same class of simplification as "no planetary perturbations" in SDD 1. |
| 10 | Parallax | **Not modeled — correctly, not as a simplification.** AT-HYG stars are light-years away; the solar system is at most ~80 AU across (~0.0013 light-years). The resulting parallax is physically negligible from anywhere the ship can be, so the star field is rendered as camera-**orientation**-locked only: it translates to follow the camera/observer's position every frame (so it never clips or drifts oddly) but never needs to account for *where* in the solar system the ship actually is — no Kepler math, no anchor/render-origin conversion, unlike every body-rendering system in this project. |

## 3. Design

### 3.1 Offline pipeline (Appendix A)

New `Tools/StarField/` directory (raw catalog data and intermediate working files live
under a gitignored `Tools/StarField/data/` subfolder — not committed, regenerated from
a documented URL, the same spirit as not committing engine binaries):

1. `fetch_athyg.py` (or a documented manual step): downloads the current AT-HYG
   release CSV.
2. `bake_star_field.py` (numpy): parses the CSV, for each star computes:
   - **Direction** (unit vector, ecliptic J2000 frame): RA/Dec (equatorial) →
     ecliptic via the standard J2000 obliquity rotation (23.4393°) — the same
     target frame every other astronomical fact in this project already uses.
   - **Color** (RGB, linear): B-V index → RGB (decision 8).
   - Splits at mag 8.0 (decision 7):
     - **Bright stars** (mag < 8): written as a compact binary/CSV (direction, a
       magnitude-derived brightness/size scalar, RGB) — no distance-compensation
       math needed at runtime (decision 10: every bright star is always rendered at
       the same fixed radius from the camera, unlike CelestialVault's finite vault
       sphere, so size/brightness bakes to a final screen-space-equivalent value
       once, not recomputed per frame).
     - **Faint stars** (mag ≥ 8): each star's flux is splatted onto the cubemap
       texels its direction's sub-pixel footprint actually covers (not
       nearest-texel), accumulated per face, written as 6 HDR face images.
3. `import_star_field.py` (Unreal Python, `-run=pythonscript`, mirrors
   `Tools/CreateSOLContent.py`'s idempotent/`-SOLRebuild` pattern): imports the 6
   HDR images as one `TextureCube`, and the bright-star binary as a new
   `USOLStarFieldData` (`UDataAsset`) under `Content/SOL/`. Also copies the
   CelestialVault assets named in decision 5 into `Content/SOL/` at this stage
   (one-time; re-run only if CelestialVault's source assets change, which they
   won't unless the engine version changes).

   **As built (4b).** Run it as `python Tools/StarField/import_star_field.py
   [-SOLRebuild]` with the editor closed. It creates `/Game/SOL/StarField/T_SOLStarFieldCube`
   (the DDS imported as a `TextureCube`: sRGB off, HDR Compressed / BC6H, mips generated
   by simple average because the bake ships mip 0 only, Skybox texture group) and
   `/Game/SOL/StarField/DA_SOLStarField` (a `USOLStarFieldData`, defined in
   `Source/SOLTest/StarField/SOLStarFieldData.h`: 45,653 `FSOLBrightStar` entries holding the
   ecliptic direction, V, flux and luminance-1 linear color, plus `CubeTexelSolidAngleSr` =
   (2/2048)² ≈ 9.54e-7 sr, the cutoff and reference magnitudes, and the CC BY-SA
   attribution). It also copies these CelestialVault assets to
   `/Game/SOL/StarField/CelestialVault/`: `T_MilkyWay`, `T_StarMask_Round` (the round sprite
   mask, decision 4), `MF_BillboardSizeByPixelUnits`, `MF_ScalePlaneToMinScreenPixels`,
   `MF_DirectionToLatLong`, `SM_Plane_FacingX` (the 100 cm star quad, facing +X; its material
   slot is cleared because the plugin's `MI_SolarSystemPlanets` isn't copied) and
   `M_Stars_EnergyConservative` (the plugin's star material, kept only as a reference for 4c;
   its mask texture is re-pointed at the copy). **Do not use it at runtime as it is.** It
   expects CelestialVault's absolute brightness units and per-instance data layout, but our
   data is flux relative to magnitude 8. A sprite of flux F drawn over an on-screen solid
   angle Ω emits F × `CubeTexelSolidAngleSr` / Ω, which is the contract in
   `SOLStarFieldData.h`. None of the three material functions depends
   on any other plugin asset. Paths are in `SOL::Paths` (`SOLConstants.h`).

   **Copying CelestialVault content: the temporary-enable procedure.** Unreal can only
   duplicate an asset whose content root is mounted, and `/CelestialVault/` is mounted only
   while the plugin is enabled. So the copy needs the plugin enabled for exactly one editor
   run. When run with a normal Python, the script does this itself:
   1. It writes the original `SOLTest.uproject` to `Saved/SOLTest.uproject.solbak`, then adds
      `CelestialVault` to its `Plugins`.
   2. It runs itself inside `UnrealEditor-Cmd -run=pythonscript`. That run is killed after
      `--timeout` seconds (default 3600).
   3. It restores the original bytes in a `finally` block and checks the file is
      byte-identical.

   If the driver itself is killed, the next run finds the `.solbak` backup and restores from
   it. The manual fallback is `git checkout -- SOLTest.uproject`, which the script prints
   before the risky step.

   Inside the editor run, each asset is copied with `EditorAssetLibrary.duplicate_asset`, and
   references between copies are re-pointed at the copies. Each saved copy must be known to
   the asset registry, must have a non-empty dependency list, and must not depend on anything
   under `/CelestialVault/`, `/Script/CelestialVault` or `/Script/DaySequence`. Otherwise the
   run fails. When all copies already exist, the script leaves the `.uproject` alone.

   A future re-copy (for example after an engine upgrade changes the plugin's assets) must go
   through the same script with `-SOLRebuild`, never by enabling the plugin by hand and
   leaving it enabled.

   **The lasting guard is `Source/SOLTest/Tests/StarFieldTest.cpp`** (`SOLTest.StarField.*`).
   It runs with every test run, with the plugin disabled. For every package under
   `/Game/SOL/StarField`, it requires the asset registry to know the package, a non-empty
   dependency list, none of the three prefixes above, and a successful load. It also checks
   the data asset (unit directions, brightest-first order, the solid-angle and attribution
   metadata) and the cube's compression and sRGB settings.

   Two limits apply. The registry does not record dependencies on always-loaded engine
   modules (`/Script/Engine`, `/Script/CoreUObject`). It does record any other module a
   package needs (`/Script/SOLTest`, `/Script/InterchangeEngine` show up), so a plugin class
   would show up as `/Script/CelestialVault`. A probe that treated `/Script/InterchangeEngine`
   as a leak made the test fail as expected. Text the registry never sees, such as a Custom
   node's shader-include path, is outside this check. Three copies do have Custom nodes
   (`MF_BillboardSizeByPixelUnits`, `MF_ScalePlaneToMinScreenPixels`,
   `M_Stars_EnergyConservative`). A byte scan in 4b found no include paths (no `.ush`, no
   `IncludeFilePaths`) and no `CelestialVault` text in them, so their HLSL is self-contained.

   **Remaining `CelestialVault` strings.** The copies still contain `CelestialVault` strings in
   their Interchange/`AssetImportData` metadata, which is not a package reference:
   - Epic's original source-file paths (`.../CelestialVaultDev/SourceAssets/...`).
   - In `T_MilkyWay`, `T_StarMask_Round` and `SM_Plane_FacingX`, the original import
     settings: a `contentImportPath` of `/CelestialVault/Textures` or `/CelestialVault/Meshes`,
     and a `ReferenceObject` string naming the original asset.

   Loading and cooking ignore them. They would only matter if someone reimported one of these
   assets, and these copies are never reimported (re-copy instead).
   The in-engine orientation of the cube was verified in 4b. Each of the six faces and
   an ecliptic equirectangular view were rendered through a material that samples the
   imported cube, and compared against the bake: see the revision history.

### 3.2 Runtime (Appendix B)

New feature folder `Source/SOLTest/StarField/`:

- `ASOLStarField` (engine actor): spawned by `ASOLGameMode` alongside
  `ASOLBodyVisuals` (same `IsSOLGameWorld`-gated pattern, same
  `AlwaysSpawn`/`Identity`-transform spawn call). Owns:
  - A `UInstancedStaticMeshComponent` populated once at `BeginPlay` from
    `USOLStarFieldData` (one `AddInstance` per bright star; the quad mesh and its
    unlit material are the copied-and-adapted CelestialVault assets, decision 5).
    The copied `SM_Plane_FacingX` has a `BodySetup` with one convex hull and the `BlockAll`
    collision profile (checked in 4b). The ISM component **must** set `NoCollision` and
    `SetCanEverAffectNavigation(false)`, or ~45,000 instances create that many physics
    bodies.
  - A large inverted sphere mesh (or an unlit full-screen technique — implementer's
    call, whichever is simpler to get right; a big sphere is the more
    conventional/lower-risk choice) with an unlit material sampling the baked
    `TextureCube` by local direction, oriented so the baked Milky Way/faint-star
    data reads correctly (the bake in Appendix A already puts everything in the
    ecliptic frame, matching every other body/orientation in the project, so no
    additional runtime rotation should be needed beyond the actor's own placement).
  - No tick: every frame, the actor's location is set to the observer's current
    render location (decision 10 — translate only, driven off
    `USOLAnchorSubsystem::OnUniverseUpdated`, same event every other visual actor
    already uses) and nothing else changes.
- No new pure-logic C++ module is anticipated: the astronomical math (RA/Dec→
  ecliptic, B-V→RGB, flux splatting) all happens once, offline, in Appendix A's
  Python. If the implementer finds a genuine need for a runtime pure-logic
  function (e.g., a magnitude→brightness curve that turns out to need runtime
  tuning rather than bake-time baking), it gets the normal TDD treatment; this is
  not expected to be the common case here.

### 3.3 Verification

No new `-SOL*` smoke-test flag or scripted checkpoints (no new player input or
mechanic, same as Part 12b) — verification is: the offline pipeline's own sanity
output (star counts, magnitude histogram, a rendered preview of the cubemap faces
inspected directly as images before import), then a PIE/`-game` screenshot check
(a few different look directions, including one that should show the Milky Way band)
inspected directly, per this project's established "look at the actual screenshot,
don't just trust a pass/fail log" practice for visual work.

## 4. Open questions

None deferred as blocking. Diffraction spikes (decision 4) tracked as a follow-up.

## 5. Revision history

- 2026-09-30: initial decisions from the `grill-me` session.
- 2026-09-30: 4a built and run against the real, full catalog (2,558,654 rows).
  Decisions 1 and 8 corrected with facts only discoverable by actually fetching and
  inspecting the data (exact file path/LFS URL, and that AT-HYG's `ci` column is
  Tycho BT-VT, not B-V, for 99.9% of rows). Results: 45,653 bright stars (mag<8,
  within the SDD's estimated range), 2,513,000 faint stars baked into the cubemap.
  Tone-mapped previews inspected directly (by the implementer and independently by
  the main session) show a correct Milky Way band with visible dust-lane structure.
  Open risk flagged by the implementer for 4b/4c to resolve: the exact orientation
  convention (axis mirroring) of Unreal's DDS cubemap import has not yet been
  verified in-engine — the bake includes a debug mode
  (`--include-bright-in-cube`) specifically to cross-check the cubemap's orientation
  against the independently-oriented bright-star sprites once both are in Unreal.
- 2026-09-30: adversarial review (ran the code, checked the hand-rolled DDS header
  byte-for-byte against UE 5.8's own `DDSFile.cpp` loader, independently re-derived
  the ecliptic rotation and photometric formulas) found nothing blocking. It did find
  the brightest ~500 stars (Sirius, Arcturus, Rigil Kentaurus, Capella, Vega, ...)
  were 0.1-0.56 mag too dim, since Tycho photometry saturates at the bright end and
  several also lack AT-HYG's own color column. Fixed by cross-referencing HYG v4.4
  (added as a secondary source, same author/license) for real Johnson V/B-V on stars
  below V 4, with a data-driven exception for blended multiple-star entries that would
  otherwise double-count flux (verified against the actual data, not assumed — a first
  simpler rule wrongly caught two real pairs). Also added a fast, validated placement
  self-check (deliberately-injected bugs — a half-texel offset, swapped bilinear
  weights, a wrong edge tap, a mirrored face axis — were all caught) that found and
  fixed a real, small (up to 0.18 texel) edge/corner splatting inaccuracy the original
  energy-conservation check couldn't have detected. Atomic writes and a versioned parse
  cache added for robustness. Previews re-inspected (by the implementer and
  independently by the main session): the Milky Way band and dust-lane structure are
  intact; a cosmetic pole-stretch artifact remains only in the equirectangular preview
  image (an unavoidable property of that projection, not a bug, and doesn't affect the
  actual cubemap data used at runtime).
- 2026-09-30: 4b built. `USOLStarFieldData` and `import_star_field.py` added. The script
  handles the CelestialVault copy with the temporary-enable procedure in §3.1.
  `SOLTest.uproject` is byte-identical to its pre-4b state afterwards. **The DDS
  orientation risk from 4a is closed.** The imported `TextureCube` was rendered in-engine
  with `DrawMaterialToRenderTarget` and a material that samples it along the D3D face-table
  direction of each face texel. Each render was compared with the bake's own cube data
  (8×8 box-averaged). Log-luminance correlation was 0.997–0.998 as-is on all six faces. Every
  mirrored, rotated, transposed or swapped-face alternative scored ≤ 0.50. The mean-brightness
  ratio was 0.992–0.996 (BC6H plus generated mips conserve energy). Against the tone-mapped
  preview PNGs, correlation was 0.81–0.88 as-is and ≤ 0.56 for the alternatives. An ecliptic
  equirectangular render (ecliptic → Unreal (x, −y, z), the bake preview's own mapping)
  correlated 0.93 with the bake. The side-by-side images were inspected directly: same
  Milky Way band and dust lanes, no mirroring.
- 2026-09-30: 4b adversarial-review fixes.
  - The import script's independence check now fails if the asset registry doesn't know a
    copy or has no dependencies for it. It also treats `/Script/CelestialVault` and
    `/Script/DaySequence` as leaks.
  - The `.uproject` procedure gained an on-disk backup with automatic recovery, a child-process
    timeout, and printed recovery steps.
  - Added `StarFieldTest.cpp` as the permanent guard. It passes with the plugin disabled, and
    a deliberate probe showed that it does fail when a leak is present.
  - Corrected the import-metadata description and the `CubeTexelSolidAngleSr` contract.
  - Marked the reference material as not for runtime use, and noted the quad mesh's collision
    for 4c.
