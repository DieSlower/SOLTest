<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 7 — Weapons, target drops, destruction VFX and audio

Design: [`Docs/SDDs/7-weapons-targets-destruction.md`](../SDDs/7-weapons-targets-destruction.md).

- [x] **7a — Pure logic, test-first.** Contract tests (separate opus agent): `SOLBoltMath`, `SOLDamageMath`, `SOLCombatGrid`,
  `SOLTargetDrop`; then implement to green.
- [x] **7b — Mass bolts and targets.** Fragments, `USOLBoltProcessor`, combat subsystem (pools, grid), input action
  (fire, drop target), constants.
- [x] **7c — Visuals.** Bolt, impact, explosion and debris Niagara systems, target instanced meshes with hit flash, shield
  and health feedback. Done 2026-10-09: `ASOLCombatVisuals` + `/Game/SOL/Combat/` assets; verified with `-SOLCombatDemo -SOLSmokeShot=20`
  (bolt streaks, blue shield-hit sparks, explosion with shock ring, debris). See SDD 7 Amendment 2.
- [ ] **7d — Audio.** Placeholder synthesized sounds, pooled playback.
- [ ] **7e — Asteroid and ring hits.** Bolts vs minor-body positions through the grid, absorbed with a spark puff.
- [ ] **7f — Docs, review, verification.** `ARCHITECTURE.md`, `GAME_MECHANICS.md`, in-game controls screen, PIE smoke and
  `stat` evidence, adversarial opus review, fix, full suite, commit and push each part.
