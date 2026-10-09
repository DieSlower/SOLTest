<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 8 — HUD, menus, controls/info/about screens, settings

Design: [`Docs/SDDs/8-hud-menus-settings.md`](../SDDs/8-hud-menus-settings.md).

- [ ] **8a — Pure logic, test-first.** `SOLMenuState`, `SOLInfoText`, `SOLSettingsModel`, `SOLBindingLabel`; contract tests by a separate agent, then implement.
- [x] **8b — Menu subsystem, state and game-mode flow.** Menu state, deferred ship spawn until Play, menu camera, Esc handling, input suspension.
- [x] **8c — Widgets.** Main menu, pause menu, Controls, Info, About, start-body picker.
- [ ] **8d — Settings.** Settings subsystem and tab (graphics, audio, controls with rebinding, gameplay), persistence per the project standard.
- [ ] **8e — Docs, review, verification.** ARCHITECTURE.md, GAME_MECHANICS.md, PIE screenshots of every screen, adversarial opus review, fixes, full suite, commit and push per part.

## Progress notes

- **8b/8c done (2026-10-09).** `USOLMenuSubsystem`, `ASOLMenuCameraPawn`, `USOLMainMenuWidget`, `USOLPauseMenuWidget`, `USOLMenuButton`, `SOLMenuContent`, `SOLMenuText`, `FSOLMenuSmoke`; ship spawn deferred to Play, Esc pause, Quit to menu. Verified with `-SOLMenuSmoke=all` (18/18 checks, 13 screenshots in `Saved/Screenshots/WindowsEditor/MenuSmoke_*.png`) and `-SOLSmokeInput` (29/29, starts in flight as before). Deviations in SDD 8 Amendment 1. 8a's automation tests were written but not run by this part (the main session runs the suite).
- **Next:** 8d (settings), then 8e (adversarial review, full suite, commit).
