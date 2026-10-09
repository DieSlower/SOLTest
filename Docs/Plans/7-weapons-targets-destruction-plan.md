<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 7 — Weapons, target drops, destruction VFX and audio

Design: [`Docs/SDDs/7-weapons-targets-destruction.md`](../SDDs/7-weapons-targets-destruction.md).

Labels: this is roadmap Part 6 (issue #7). Its sub-parts are labelled **7a-7f** everywhere in docs and code comments; the
commits already pushed call them "Part 6a/6b/6c" (same letters).

- [x] **7a — Pure logic, test-first.** Contract tests (separate opus agent): `SOLBoltMath`, `SOLDamageMath`, `SOLCombatGrid`,
  `SOLTargetDrop`; then implement to green.
- [x] **7b — Bolts and targets.** Combat subsystem with pooled structure-of-arrays bolts and targets (not Mass entities and
  no `USOLBoltProcessor`; SDD 7 Amendment 1), grids, input actions (fire, drop target), constants.
- [x] **7c — Visuals.** Bolt, impact, explosion and debris Niagara systems, target instanced meshes with hit flash, shield
  and health feedback. Done 2026-10-09: `ASOLCombatVisuals` + `/Game/SOL/Combat/` assets; verified with `-SOLCombatDemo -SOLSmokeShot=20`
  (bolt streaks, blue shield-hit sparks, explosion with shock ring, debris). See SDD 7 Amendment 2.
- [x] **7d — Audio.** Placeholder synthesized sounds, pooled playback. Done 2026-10-09: `ASOLCombatAudio`,
  `SOLCombatAudioRules` (+ `SOLCombatAudioRulesTest.cpp`), seven WAVs under `/Game/SOL/Combat/Audio/`; verified with
  `-SOLCombatDemo -SOLSmokeShot=22` (every reachable kind requested, no audio errors). See SDD 7 Amendment 3.
- [x] **7e — Asteroid and ring hits.** Bolts vs minor-body positions through the grid, absorbed with a spark puff. Shipped
  inside 7b's subsystem (logic) and 7c (spark effect).
- [x] **7f — Docs, review, verification.** `ARCHITECTURE.md`, `GAME_MECHANICS.md`, PIE smoke and `stat` evidence,
  adversarial opus review, fix, full suite, commit and push. There is no in-game Controls/Info screen yet (only the HUD
  key-hint line, which already lists `LMB fire` and `G drop target`); building one is out of scope here (CLAUDE.md tech
  debt). Review findings fixed 2026-10-09 (SDD 7 Amendment 4, `SOLCombatRules` + `SOLCombatRulesTest.cpp`,
  `-SOLCombatStress` measurements); remaining: main session's full suite run, then commit and push.
