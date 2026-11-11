<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 37: HUD glow setting (lower default glow, Settings slider 0-200%)

SDD: [`SDDs/37-hud-glow-setting.md`](../SDDs/37-hud-glow-setting.md). Issue
[#37](https://github.com/DieSlower/SOLTest/issues/37). Decisions D1-D17 are in SDD section 2. The default level (D4)
was picked by the user from the step 9 screenshots on 2026-11-11: `HUD_GLOW_DEFAULT_LEVEL = 0.3`. No question is
pending.

**Schema change (D1, approved):** `SOL::Settings::SAVE_VERSION` 11 -> 12 (the settings save has this one version
constant); new field `USOLSettingsSave::HudGlow` (float, default `HUD_GLOW_DEFAULT` = 1.0) and
`SOLSettingsModel::FSettingsValues::HudGlow`; migration step 11 -> 12 sets `HudGlow = 1.0` and keeps every other field.
`KEY_BINDING_LAYOUT_VERSION` stays 10; no key binding, override or overlay-profile layout changes. The persistence
subagent must report exactly which version constants and fields it changed; the main session checks the report
against D1 before treating the step as done.

## Commit parts (three, in this order)

Convention `Issue #37: Part 37x - <description>`. Each part is committed and pushed on `master` only once its tests
pass, its smokes pass, and its own adversarial performance and style review's findings are fixed (CLAUDE.md "Git").

1. **37a - settings, glow math, material and HUD feed** (steps 2-6): schema 12 and the "HUD glow" row; the glow
   math; `M_SOLHudShape` `GlowScale`; the HUD layer, its text and the route overlay follow the slider. The slider works
   end to end at the placeholder level 0.5 (replaced by the user's pick, 0.3, in 37c).
2. **37b - card readouts and Help glossary** (steps 7-8): the per-shape glow override and per-item text alpha (D10),
   the glossary push (D11).
3. **37c - chosen default level, verification and docs** (steps 9-14): candidate screenshots and the user's pick, the
   final smokes, screenshots and profile, every doc, the player guide, the README gallery; closes #37.

## Files to change

| File | Change | Part |
|---|---|---|
| `Source/SOLTest/SOLConstants.h` | `SOL::Settings`: `HUD_GLOW_MIN/MAX/DEFAULT/STEP`, `SAVE_VERSION` 12 and its comment. `SOL::Hologram`: `HUD_GLOW_DEFAULT_LEVEL` (0.5 placeholder in 37a-37b, 0.3 from 37c: the user's pick, D4), `HUD_GLOW_CORE_FALLOFF_MAX` (6, D8), `HUD_GLOW_SCALE_MAX` 2; comment blocks above `GLOW_HALO_INTENSITY`, `HUD_GLOW_CORE_HALF_PX` and `HUD_TEXT_GLOW_ALPHA` say these are the `g = 1` reference values. `SOL::HudShapeMaterialParams::GLOW_SCALE` | 37a (level fixed in 37c) |
| `Source/SOLTest/Menu/SOLSettingsModel.h` / `.cpp` | `FSettingsValues::HudGlow`; `Sanitized` (`ClampOrDefault`) and `AreEqual` | 37a |
| `Source/SOLTest/Menu/SOLSettingsSave.h` / `.cpp` | `UPROPERTY() float HudGlow`; `SetValues` / `GetValues`; frozen migration step 11 -> 12 | 37a |
| `Source/SOLTest/Menu/SOLSettingsPageWidget.h` / `.cpp` | `ESOLSettingsSlider::HudGlow` after `HudEffects` (D5), `SETTINGS_SLIDERS` entry (percent), get / apply cases, `addSlider` row between HUD and menu effects and Hologram menu effects, `HandleHudGlowChanged` | 37a |
| `Source/SOLTest/Menu/SOLSettingsTabs.cpp` | `SETTINGS_SLIDER_TABS` entry (Gameplay) | 37a |
| `Source/SOLTest/Menu/SOLMenuText.h` | `SETTINGS_HUD_GLOW` "HUD glow", `SETTINGS_HINT`, the flight HUD Info sentence (D6) | 37a |
| `Source/SOLTest/UI/SOLHudShapeMath.h` / `.cpp` | `HudGlowScale`, `SanitizeGlowScale`, `GlowHaloWeight`, `GlowCoreWeight`, `GlowCoreFalloff`, `GlowProfile(d, g)` (the one-argument `GlowProfile` stays the `g = 1` reference), `TextGlowAlpha` | 37a |
| `Source/SOLTest/Map/SOLRouteOverlayMath.h` / `.cpp` | `RouteHaloIntensity(g)` | 37a |
| `Source/SOLTest/Map/SOLMapRouteOverlay.h` / `.cpp` | `FSOLRouteOverlayInputs::GlowScale` (default 1); `AppendGlowPiece` scales the halo and skips the halo triangles at 0 | 37a |
| `Source/SOLTest/UI/SOLHudShapeLayer.h` / `.cpp` | `SetTextGlowAlpha` (default `HUD_TEXT_GLOW_ALPHA`); `PaintTexts` uses the item's alpha if set, else the layer's, and drops the outline at 0 | 37a (item alpha 37b) |
| `Source/SOLTest/UI/SOLFlightHud.h` / `.cpp` | `mHudGlowScale`, `mShapeGlowScale`; `BeginShapeFrame` pushes `GlowScale` and the HUD layer's text alpha on change; route inputs `GlowScale`; `GetShapeGlowScale()` for the smoke | 37a |
| `Tools/UI/create_hud_shape_material.py`, `Content/SOL/UI/M_SOLHudShape.uasset` | `GlowScale` parameter (default 1.0), `CoreFMax` pin, new glow line (SDD 4.3); header comment; asset rebuilt with `-SOLRebuild`, editor closed | 37a (`GlowOverride` / Customized UV 2 in 37b) |
| `Source/SOLTest/UI/SOLHudShapeMath.h` / `.cpp` | (review, D16 / D17) `FSOLGlowScaleFeed`: the glow scale of a setting, a change reported once | 37a |
| `Source/SOLTest/UI/SOLHologramCursorSubsystem.h` / `.cpp` | (D17) the pointer's MID gets `GlowScale` from the live HUD glow, pushed on change; `GetPushedGlowScale` | 37a |
| `Source/SOLTest/Menu/SOLHudGlowSample.h` / `.cpp` (new), `Menu/SOLSettingsPageWidget.*`, `Menu/SOLMenuText.h`, `SOLConstants.h` | (D16) the Settings glow sample (own MID, retained layer) under the HUD glow row, fed from the page tick, its indent kept in step with the narrow layout; `SETTINGS_GLOW_SAMPLE*` constants and text; the Info sentence per D17 | 37a |
| `Source/SOLTest/Map/SOLRouteOverlayMath.*`, `Map/SOLMapRouteOverlay.*`, `Map/SOLRouteOverlaySmoke.*` | (review) `GlowPieceTriangles`, `SOL::Route::OVERLAY_GLOW_*_TRIANGLES`, the overlay's last glow scale and halo for the smoke; smoke wiring and 0% checks | 37a |
| `Source/SOLTest/UI/SOLHudShapeBatch.h` / `.cpp` | `SetGlowOverride` / `ClearGlowOverride`, written into `MaterialTexCoords` by every `Add*`, cleared by `Reset` | 37b |
| `Source/SOLTest/UI/SOLHudTextList.h` / `.cpp` | `FSOLHudTextItem::GlowAlpha` (-1 = layer's), `SetGlowAlphaOverride` / `ClearGlowAlphaOverride` | 37b |
| `Source/SOLTest/UI/SOLFlightHud.cpp` | `BeginCardContent` / `EndCardContent` set and clear both overrides with `mHudGlowScale` | 37b |
| `Source/SOLTest/Menu/SOLSolidMenuSubsystem.h` / `.cpp`, `Source/SOLTest/Menu/SOLHudSymbolView.cpp` | `FSOLSolidMenuParamCache` caches the glow; `WriteHudShapeParams` writes `GLOW_SCALE`; symbol views set their text layer's glow alpha on change | 37b |
| `Source/SOLTest/UI/SOLHudSmoke.h` / `.cpp` | 37a: "glow scale follows the live value and reverts" check. 37b: card content override check. 37c: glow phases for the candidate and final screenshots (D14) | 37a-37c |
| `Source/SOLTest/Menu/SOLSolidMenuSmoke.cpp` | Gameplay slider list (`u-*` width check) and row counts include `HudGlow`; 37b: glossary glow push check | 37a, 37b |
| Tests: `SOLSettingsModelTest.cpp`, `SOLSettingsRulesTest.cpp`, `FlightModeMathTest.cpp`, `HudShapesTest.cpp`, `RouteOverlayMathTest.cpp`, `SolidSettingsTest.cpp` | SDD section 5 cases 1-12, 16, 17; schema 11 -> 12 in the existing assertions; re-validation of every existing case in the touched files | 37a |
| Tests: `HudShapesTest.cpp` (batch), `HudPanelsTest.cpp` (text list), `SolidMenuKitTest.cpp` (param cache) | SDD section 5 cases 13-15; existing batch, text-list and cache cases re-validated | 37b |
| `Docs/GAME_MECHANICS.md` | New Settings row; Flight HUD and Route overlay rows' glow numbers; glossary paragraph | 37c |
| `Docs/SDDs/21-hud-hologram-rendering.md`, SDD 20, 33, 42 | SDD 21 Amendment 5 (as built) and status line; one-line pointers in SDD 20 section 2.4, SDD 42, SDD 33 | 37c |
| `Docs/ARCHITECTURE.md` | Flight HUD, shape layer, solid menu subsystem and symbol view entries; settings schema 12 | 37c |
| `Docs/PLAYER_GUIDE.md`, `Docs/Images/` | Regenerated with `Tools\WritePlayerGuide.bat` at the chosen level (D11) | 37c |
| `Docs/Plans/STATUS.md` | #37 in progress (done), Done with results | 37c |
| `README.md`, `Docs/PROJECT_METRICS.md`, `Docs/Images/` | `Tools\UpdateMetrics.bat` before each commit; 2-3 gallery shots (e.g. 0% and 100%) | 37a-37c |

## Steps

| Step | What | Who (model / effort) | Status |
|---|---|---|---|
| 1 | SDD, this plan, STATUS #37 in progress | opus / high | Done |
| 1b | Q1-Q11 answered by the user (D5-D15); SDD, plan and contract updated | main session, opus / high | Done |
| **37a** | | | |
| 2 | TDD: SDD section 5 cases 1-12, 16, 17 written from the contract only (no implementation shown), including the schema 12 updates of existing assertions | opus / high | Done |
| 3 | `Tools\RunTests.bat -Build -Filter` `SOLTest.SettingsModel`, `SettingsRules`, `FlightModeMath`, `HudShapes`, `RouteOverlayMath`, `SolidSettings`: fail for the right reason (missing `HudGlow`, constants, functions; version 11 != 12), not a typo | sonnet / medium | Done |
| 4 | Persistence: constants, model, save, migration 11 -> 12, page row and tab, text (guardrails: version and migrate, never discard; change only what D1 approved; report every version constant and field changed) | opus / xhigh | Done |
| 5 | Glow math, material script and asset rebuild (editor closed), HUD feed, layer text alpha, route halo, smoke check; tests green, full suite green; `-SOLSmokeHud` (both sizes), `-SOLSmokeRouteOverlay`, `-SOLSmokeSolidMenu` PASS | opus / high | Done |
| 6 | Adversarial performance and style review of 37a (fresh opus / xhigh, report only; brief: changed files, SDD 37, style guide, performance guidelines, the TECH_DEBT items in SDD section 8, the persistence report); fixes; re-check against SDD 37; full suite again; `Tools\UpdateMetrics.bat`; commit and push 37a. Review done (no Critical / High); user decisions D16 (Settings glow sample), D17 (the pointer follows) and the D14 correction; findings 2-11 fixed (finding 4, an Info caveat for the card readouts, left for 37b, which makes them follow) | opus / xhigh, then opus / high | Done |
| **37b** | | | |
| 7 | Engine check first (re-read `SlateVertexShader.usf` UV channel 4 and `SlateElementPixelShader.usf` `FillTexCoords`; build the material with `NumCustomizedUVs` 3 and Customized UV 2 = vertex `TexCoord[4]`; prove with one smoke screenshot that a card readout's glow changes and its frame's does not). **If not viable: stop and report to the main session (D10); no other route.** Then TDD cases 13-15 (opus / high, contract only), red for the right reason | opus / high | Done |
| 8 | Batch override, text-list item alpha, card content wiring, glossary cache and pushes; the Info sentence and `GAME_MECHANICS.md` say the card readouts and the glossary follow (37a review finding 4); tests and full suite green; `-SOLSmokeHud`, `-SOLSmokeHoloMenu`, `-SOLSmokeSolidMenu` PASS; review of 37b (fresh opus / xhigh), fixes, suite again; metrics; commit and push 37b. (The 37b review is folded into the 37c review; a careful self-review was done) | opus / high, review opus / xhigh | Done |
| **37c** | | | |
| 9 | **D4 candidate screenshots** (D14): `-SOLSmokeHud` glow phases at 1920x1080 with the placeholder level 0.5: slider 60% / 100% / 140% (= 30% / 50% / 70% of today's) and 200% (= today's baseline), each over the lit Earth (bright) and facing away (dark), target card shown, plus the route overlay; the 0% shot for `HUD_GLOW_CORE_FALLOFF_MAX`. Handed to the main session, which shows them to the user and asks for the level (`AskUserQuestion`); the user's pick goes into `HUD_GLOW_DEFAULT_LEVEL`; candidate shots deleted | sonnet / medium, then main session | Done (user: 30%, level 0.3) |
| 10 | Smokes at 1280x720 and 1920x1080 (`-SOLSmokeHud`, `-SOLSmokeRouteOverlay`, `-SOLSmokeHoloMenu`, `-SOLSmokeSolidMenu`), final screenshots at 0 / 100 / 200% bright and dark at both sizes, the radial menu at 0% (D3) and a glossary tab at 0% and 100% (D11), looked at; persistence check (v11 file -> v12 written once; no write without Apply); PIE check via unreal-mcp if the editor is open | sonnet / medium | Done |
| 11 | Profile (SDD section 8, D15): before and after, 0 / 100 / 200%, at 1920x1080, 3840x2160 and 3840x3744 (one XR-4 eye); numbers into SDD 21 Amendment 5 and SDD 37 | sonnet / medium | Done |
| 12 | Docs: `GAME_MECHANICS.md`, in-game Info / Settings text check, SDD 21 Amendment 5, SDD 20 / 33 / 42 pointers, `ARCHITECTURE.md`, `Tools\WritePlayerGuide.bat` and `SOLTest.PlayerGuide`, SDD 37 as built | sonnet / low-medium | Done |
| 13 | Adversarial review of 37c's diff (fresh opus / xhigh, report only); fixes; re-check against SDD 37; full suite and smokes again | opus / xhigh, then fixes | Done |
| 14 | README gallery shots (2-3), temp screenshots deleted, `Tools\UpdateMetrics.bat`, STATUS #37 Done, `graphify update .`, commit and push 37c; close #37 | sonnet / low | Done |

## Test contract (summary; full contract in SDD section 5)

- Settings: `HUD_GLOW_MIN/MAX/DEFAULT/STEP` = 0 / 2 / 1 / 0.05 (41 positions); `HudGlow` default 1; sanitize clamps,
  NaN -> 1, +-inf -> ends, independent of the other sliders; dirty / revert / commit like the other floats; percent
  formatting 0% / 5% / 100% / 200%; `TabOfSlider(HudGlow) == Gameplay`, after `HudEffects`.
- Save: `SAVE_VERSION == 12`, `KEY_BINDING_LAYOUT_VERSION == 10`; v11 -> 12 sets `HudGlow = 1` and keeps every other
  field; chains from v1 / v5 / v6 / v10 end at 12 with `HudGlow = 1`; a v12 file keeps its value; v13 ignored; no
  controls-changed notice for v11.
- Glow math (reference units, `g = 1` = post-#31): `HudGlowScale(s) = clamp(s) x HUD_GLOW_DEFAULT_LEVEL` with
  `0 < LEVEL < 1`; halo weight `0.1 g`; core weight `1 - 0.1 g`; core falloff `3 / max(g, 3 / FALLOFF_MAX)`;
  `GlowProfile(d, 1) == GlowProfile(d)`; `GlowProfile(0, g) == 1`; at `g = 0` the profile is 1 inside the core and
  below 1% at 1.5 px; non-decreasing in g outside the core, non-increasing in d, below 1% at the pad;
  `TextGlowAlpha(g) = clamp(0.18 g, 0, 1)`; `RouteHaloIntensity(g) = 0.15 g`; NaN g -> 1, negative -> 0, clamp at
  `HUD_GLOW_SCALE_MAX` 2.
- Overrides (37b): batch vertices carry `MaterialTexCoords (g, 1)` while an override is set, `(0, 0)` otherwise
  (cleared by `Clear` and `Reset`, sanitized); text items carry `GlowAlpha` (-1 = layer's) the same way; the glossary
  param cache reports a change when only the glow changes.

## Performance notes

No per-frame allocation or new tick; the material parameter and text alphas are pushed only when the value changes;
a select and about three extra ALU ops per HUD pixel plus one more interpolated float2; the route overlay draws fewer
triangles at 0%. HUD cost must stay within +0.01 ms GPU of the before figure at 1920x1080 and inside the one-eye
ceiling (1.2 ms); measured with ProfileGPU / `stat dumpave` at 1920x1080, 4K and one XR-4 eye (D15), not assumed.

## Progress notes

- 2026-11-10: SDD and plan drafted from D1-D4; STATUS #37 set in progress; open questions Q1-Q11 reported.
- 2026-11-10: Q1-Q11 answered as recommended (D5-D15). Design and contract extended for the card readouts (per-shape
  override through `MaterialTexCoords` and Customized UV 2, after reading the UE 5.8 Slate shaders) and the glossary.
  Three commit parts fixed (37a, 37b, 37c). Only the default level (D4) remains open, decided at step 9. Next: step 2.
- 2026-11-10: 37a steps 2-5 done. Schema 11 -> 12 (`SOL::Settings::SAVE_VERSION`, new saved field `HudGlow`, a v11
  save migrates to 100%, nothing else touched). Glow math, `GlowScale` material parameter (asset rebuilt), HUD feed,
  layer text alpha and route halo in; `-SOLSmokeHud` pending glow phase uses 200% (g = 1, the issue #31 look) with a
  `HudSmoke_GlowPending` screenshot before the revert (new `GlowShot` phase). Full suite 1789/1789; `-SOLSmokeHud`
  32/32 at 1280x720 and 1920x1080, `-SOLSmokeRouteOverlay` 25/25, `-SOLSmokeSolidMenu` 275/275. Next: step 6
  (adversarial review of 37a, then metrics, commit and push).
- 2026-11-10: step 6. Review: no Critical / High. User decisions: D16 (a live glow sample on the Settings page) and
  D17 (the pointer's glow follows HUD glow), D14 corrected (the real save slot). Built: `FSOLGlowScaleFeed`, the
  pointer's glow push, `USOLHudGlowSample` under the HUD glow row, `GlowPieceTriangles` and the overlay's glow
  accessors, the Info sentence (D17), the `GAME_MECHANICS.md` labels and wording (findings 5, 7), constant comments,
  line widths, the material node position (asset rebuilt). Tests first: `SOLTest.HudShapes.GlowScaleFeed`,
  `SOLTest.HudShapeLayer.TextGlowAlpha`, `SOLTest.RouteOverlayMath.GlowPieceTriangles` (red on the missing symbols,
  then green). Smokes: `-SOLSmokeSolidMenu` 294/294 (the sample at 1280x720, 1920x1080 and windowed fullscreen at the
  display's size; this machine has no 4K display, its largest view is 2560x1440, and a windowed 3840x2160 is clamped
  to 3840x1512, where an earlier run's layout checks also passed), `-SOLSmokeRouteOverlay` 29/29 (340 triangles at
  100%, 180 at 0%), `-SOLSmokeHud` 32/32 at both sizes. Finding 4 (an Info caveat for the card readouts) is left for
  37b, which makes them follow.
- 2026-11-10: 37b steps 7-8 done. Engine check: the UE 5.8 Slate shaders re-read, `M_SOLHudShape` rebuilt with
  `NumCustomizedUVs` 3 and Customized UV 2 = vertex `TexCoord[4]` (connected through the new editor-only
  `USOLEditorAuthoring::ConnectMaterialCustomizedUV`: the enum entries are hidden from Python); proven by screenshots
  (the speed card's G bar halo changes between 100% and 200%, its corner tick does not). Tests (written beforehand by
  another agent) green: `SOLTest.HudShapes.BatchGlowOverride*`, `SOLTest.HudPanels.TextListGlowAlpha*`,
  `SOLTest.HudShapeLayer.ResetFrameClearsOverrides`, `SOLTest.SolidMenuKit.ParamCache.GlowChange`; the Info text test
  extended. Built: batch override, text item alpha, `BeginReadoutGlow` / `EndReadoutGlow` (date, speed, target and
  info cards), glossary cache and pushes, the Info sentence and `GAME_MECHANICS.md`, the player guide regenerated.
  Smokes: `-SOLSmokeHud` 34/34 at 1280x720 and 1920x1080 (new `l-card-glow-live` / `-revert`), `-SOLSmokeHoloMenu`
  86/86, `-SOLSmokeSolidMenu` 296/296 (new `w-help-glow-0` / `-revert`). Next: step 9 (37c, the D4 candidate
  screenshots).
- 2026-11-11: 37c steps 9-12 done. Step 9: the user picked 30% from the candidate screenshots, so
  `HUD_GLOW_DEFAULT_LEVEL` = 0.3 (no test changed; candidate shots deleted). Step 10: `-SOLSmokeHud` gained the glow
  series (0 / 100 / 200%, Earth and dark) and the F3 speed panel at 0% and the saved glow (45/45 at 1280x720 and
  1920x1080), `-SOLSmokeRouteOverlay` a 0% shot (29/29), `-SOLSmokeHoloMenu` 86/86, `-SOLSmokeSolidMenu` 296/296;
  full suite 1800/1800; `HUD_GLOW_CORE_FALLOFF_MAX` kept at 6; PIE via unreal-mcp not run (editor closed). Step 11:
  `-SOLSmokeHoloMenuProfile` gained P5 (0%) and P6 (200%) and logs the glow scale; three interleaved runs per build at
  1920x1080, 3840x2160 and 3840x3744 against a pre-#37 worktree (`D:\Development\UnrealProjects\SOLTest_pre37`, at
  `5388c2f`; remove with `git worktree remove` once 37c is committed): +0.001 to +0.003 ms GPU at 1920x1080, flight HUD
  0.32 ms at one eye; table in SDD 37 section 8 and SDD 21 Amendment 5. Step 12: docs (GAME_MECHANICS, ARCHITECTURE,
  SDD 20 / 21 / 33 / 42, SDD 37 as built); `Tools\WritePlayerGuide.ps1` in full (route crops re-captured and
  re-imported, guide text unchanged, Help images redrawn); two README gallery images (`37-hud-glow-compare.png`,
  `37-hud-glow-default.png`); STATUS row; two new TECH_DEBT items (the glossary's pictured card corner marks follow
  the HUD glow; `PushParams` rewrites every MID on a glow-only change). Next: step 13 (adversarial review of the 37c
  diff), then step 14.
- 2026-11-11: steps 13-14 done. Whole-ticket adversarial review: no Critical / High. Fixed: SDD 37 section 8 one-eye
  figure (up to +2.8%, P1) and the pre-#37 build's FAIL / `valid=no` verdicts explained; `GlowMenuSavedHold` so the
  saved-glow panel shot is not taken while closing; `HUDSHAPES_OVERRIDE_SCALE` 0.45 (distinct from the level, with a
  `static_assert`); the Info sentence names the glossary's two fixed route pictures (test phrase added; the guide is
  unaffected); `Materials/*` includes inside `WITH_EDITOR` in `SOLEditorAuthoring.cpp`; long lines, one-line function
  headers, the GlowLive comment and the Info wraps; GAME_MECHANICS reference glow labelled as the g = 1 reference.
  Recorded: the speed panel above the 1.2 ms one-eye ceiling (TECH_DEBT, VR targets doc section 7). Rebuilt; suite
  1800/1800, `-SOLSmokeHud` 45/45 at both sizes, `-SOLSmokeHoloMenu` 86/86, `-SOLSmokeSolidMenu` 296/296. Pre-#37
  worktree removed. Metrics, STATUS Done, commit and push; #37 closed.
