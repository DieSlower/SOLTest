<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 8 — HUD, menus, controls/info/about screens, settings

Ticket `#8` (roadmap Part 7). Plan of record: [`Docs/Plans/8-hud-menus-settings-plan.md`](../Plans/8-hud-menus-settings-plan.md).
Refines SDD 1 and must not contradict it. Decisions were resolved with the user in a `grill-me` session on 2026-10-09.

## 1. Problem

The game has a flight HUD and a hint line but no menus: no way to start from a menu, pause, read what the controls and mechanics are,
change settings or quit cleanly, and CLAUDE.md's rule that an in-game info screen mirrors the mechanics has no screen to update.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Menu set | A full main menu plus an Esc pause menu. |
| 2 | Main menu flow | Same map, a menu state over the live solar system: slow orbiting camera around a body, ship not yet spawned. Play spawns the ship (default start Earth orbit, honoring `-SOLStart`); a start-body picker offers any body. Entries: Play, Start location, Controls, Settings, About, Quit. |
| 3 | Pause menu | Esc opens tabs: Resume, Controls, Info, Settings, About, Quit to menu. Opening it does NOT pause the simulation (user choice): the sim and flight keep running, flight input is suspended while it is open (like the map's `SuspendShipControl`). |
| 4 | UI technology | UMG widgets created in C++ (no Blueprint widget assets), layout and formatting logic as pure functions with automation tests; matches the existing `SOLSpeedPanelWidget`. The flight HUD stays on `ASOLFlightHud` (canvas). |
| 5 | Settings content | Graphics (resolution, window mode, quality preset, vsync via `UGameUserSettings`), audio (master / SFX / music volume), controls (mouse sensitivity, invert Y, key rebinding via Enhanced Input user settings), gameplay (HUD scale/visibility, default flight assist). |
| 6 | Persistence | Per the project persistence standard: mutate in memory, one batched write at menu close, skip when unchanged, read once at boot, async where it is a `USaveGame`. A versioned schema for any custom save data. |
| 7 | Info screen | Generated from the real `SOLConstants.h` values (damage, shield, regen, speed caps, warp limits, ...) plus short static prose kept in one file, so it cannot go stale; `GAME_MECHANICS.md` stays the reference catalog. |
| 8 | Controls screen | Lists every binding, read from the live Enhanced Input mappings (so rebinding is reflected) with a static label per action. |

## 3. Design

- Pure logic (test-first): `SOLMenuState` (menu state machine: MainMenu, Playing, Paused and legal transitions, input suspension rules),
  `SOLInfoText` (formats constants into display lines), `SOLSettingsModel` (clamping, dirty tracking, apply/revert, batch-save decision),
  `SOLBindingLabel` (key display names).
- Runtime: `USOLMenuSubsystem` (owns state and the widget stack), widgets `SOLMainMenuWidget`, `SOLPauseMenuWidget`, tab widgets,
  `USOLSettingsSubsystem` (wraps `UGameUserSettings` plus a `USOLSettingsSave`), game-mode changes so the ship spawn waits for Play,
  a menu camera pawn orbiting a body.
- Verification: automation tests for the pure logic, PIE smoke with screenshots of each screen, adversarial opus review.

## 4. Open questions

None deferred. The Esc key currently has no other binding to conflict with (verify when building).

## Amendment 1 — Parts 8b/8c as built (2026-10-09)

- **Ship lifetime.** The ship entity no longer exists at begin play when the main menu shows. `USOLShipSubsystem::SpawnPlayerShip(startBody)` is called by Play (start body from the picker, default `-SOLStart` or Earth) and `DespawnPlayerShip()` by Quit to menu (it also clears the surface-lock and the frame-carry history). Quit to menu also clears the selected target and any M frame lock. Dropped combat targets and live bolts are left in the world (they are world objects, not the ship's).
- **Pawns.** The game mode's default pawn class follows the menu state: `ASOLMenuCameraPawn` in the main menu, otherwise the ship pawn (or the `-SOLSpectator` camera). `USOLMenuSubsystem` swaps pawns itself (destroy the old pawn, spawn, `Possess`) instead of `RestartPlayer`, so no PlayerStart is needed. The menu camera is the observer: it sits 4.5 radii from the start body on its lit side, sways slowly, and moves to a newly picked body at once.
- **Esc.** `IA_ShipPause` (Esc) lives in the ship's mapping context only, so while the jump map is open its own Esc (in `IMC_Map`) closes the map and the pause cannot open. In the pause menu, Esc reaches the focused widget (UI-only input) and resumes. Requests are queued and applied on the next frame (outside Slate and Enhanced Input callbacks).
- **HUD.** The flight HUD is not drawn while the main menu or the pause menu shows.
- **Controls screen (decision 8 deviation).** The Controls page is a static table (`SOLMenuText::CONTROL_ROWS`, engine key names shown through `SOLBindingLabel`), not read from the live mappings: the ship's mapping context is created by the pawn, which does not exist in the main menu. Part 8d replaces it with the live mappings when rebinding lands. `SOLBindingLabel` gained four display names (Hyphen, BackSpace, MouseWheelAxis, Mouse2D); its API is unchanged.
- **Command line.** The menu is skipped when any other `-SOL*` switch is present (all smoke, demo and start flags), so verification runs start in flight exactly as before. New: `-SOLMenu` (force the menu), `-SOLNoMenu` (skip it), `-SOLMenuSmoke=<all|screens|flow>` (scripted walk with screenshots; `screens` is the same as `all`, `flow` takes none).
- **Settings tab** is a heading-only placeholder until 8d.
