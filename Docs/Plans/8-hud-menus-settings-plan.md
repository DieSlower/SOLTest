<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 8 — HUD, menus, controls/info/about screens, settings

Design: [`Docs/SDDs/8-hud-menus-settings.md`](../SDDs/8-hud-menus-settings.md).

- [ ] **8a — Pure logic, test-first.** `SOLMenuState`, `SOLInfoText`, `SOLSettingsModel`, `SOLBindingLabel`; contract tests by a separate agent, then implement.
- [x] **8b — Menu subsystem, state and game-mode flow.** Menu state, deferred ship spawn until Play, menu camera, Esc handling, input suspension.
- [x] **8c — Widgets.** Main menu, pause menu, Controls, Info, About, start-body picker.
- [x] **8d — Settings.** Settings subsystem and tab (graphics, audio, controls with rebinding, gameplay), persistence per the project standard.
- [x] **8e — Docs, review, verification.** ARCHITECTURE.md, GAME_MECHANICS.md, PIE screenshots of every screen, adversarial opus review, fixes, full suite, commit and push per part.

## Progress notes

- **8b/8c done (2026-10-09).** `USOLMenuSubsystem`, `ASOLMenuCameraPawn`, `USOLMainMenuWidget`, `USOLPauseMenuWidget`, `USOLMenuButton`, `SOLMenuContent`, `SOLMenuText`, `FSOLMenuSmoke`; ship spawn deferred to Play, Esc pause, Quit to menu. Verified with `-SOLMenuSmoke=all` (18/18 checks, 13 screenshots in `Saved/Screenshots/WindowsEditor/MenuSmoke_*.png`) and `-SOLSmokeInput` (29/29, starts in flight as before). Deviations in SDD 8 Amendment 1. 8a's automation tests were written but not run by this part (the main session runs the suite).
- **8d done (2026-10-09).** `USOLSettingsSubsystem` + `USOLSettingsSave` (slot `SOLSettings`, schema 1), `USOLSettingsPageWidget` in both menus, pure `SOLKeyBindings` and `SOLSettingsRules` with tests (`SOLTest.KeyBindings.*`, `SOLTest.SettingsRules.*`, `SOLTest.HudFormat.KeyHints`), sound classes `SC_SOLMaster/Sfx/Music` (`Tools/CreateSOLAudioClasses.py`, the combat waves now use `SC_SOLSfx`), the pawn's keys from the settings, HUD scale/visibility and live key hints, the Sun light's forward-shading priority (fixes the "multiple directional lights" on-screen warning). Verified with `-SOLMenuSmoke=all` (40/40 checks, screenshots `MenuSmoke_04b/04c/09b/09c` added) and the build (no warnings). Tests run in the editor (2026-10-12): SOLTest.KeyBindings/SettingsRules/HudFormat/SettingsModel/BindingLabel/MenuState/InfoText, 60/60 passed; the full suite is left for 8e. Decisions and deviations in SDD 8 Amendment 2.
- **8e review fixes done (2026-10-12).** All 14 adversarial-review findings fixed (SDD 8 Amendment 3): rebind capture through a Slate input pre-processor, boot graphics decision (`DecideBootGraphics`, chosen flags in the save), map-toggle key refusal, key-naming texts follow rebinds, async-save failure/in-flight handling, corrupt key names dropped, Esc repeat, ring-start assist, volume mix pop, jump cancel on Quit to menu, `OnSettingsChanged` removed, style moves. New tests: `SOLTest.SettingsRules.BootGraphics`, `SOLTest.KeyBindings.MapToggleKeys/InvalidKeyNames/FormatKeyText/ControlsActions`, `SOLTest.HudFormat.KeyTexts`, `SOLTest.InfoText.FollowsRebinds`; the changed topics (KeyBindings, SettingsRules, SettingsModel, HudFormat, InfoText, BindingLabel, MenuState) ran in the editor, 67/67 passed. `-SOLMenuSmoke=all` 41/41 (new: rebind capture through real Slate events with the focus on the Resume tab).
- **Next:** 8e (full suite, commit).
