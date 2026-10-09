<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 8 — HUD, menus, controls/info/about screens, settings

Design: [`Docs/SDDs/8-hud-menus-settings.md`](../SDDs/8-hud-menus-settings.md).

- [ ] **8a — Pure logic, test-first.** `SOLMenuState`, `SOLInfoText`, `SOLSettingsModel`, `SOLBindingLabel`; contract tests by a separate agent, then implement.
- [ ] **8b — Menu subsystem, state and game-mode flow.** Menu state, deferred ship spawn until Play, menu camera, Esc handling, input suspension.
- [ ] **8c — Widgets.** Main menu, pause menu, Controls, Info, About, start-body picker.
- [ ] **8d — Settings.** Settings subsystem and tab (graphics, audio, controls with rebinding, gameplay), persistence per the project standard.
- [ ] **8e — Docs, review, verification.** ARCHITECTURE.md, GAME_MECHANICS.md, PIE screenshots of every screen, adversarial opus review, fixes, full suite, commit and push per part.
