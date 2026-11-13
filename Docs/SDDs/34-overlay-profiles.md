<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 34: Overlay profiles

Issue #34. Depends on SDD 22 (overlay table, Overlay Menu, `FOverlayPrefs` built for 5 profiles, key chords) and
SDD 43 (space dust, currently a per-profile `SpaceDust` row). Plan:
[`Docs/Plans/34-overlay-profiles-plan.md`](../Plans/34-overlay-profiles-plan.md).
Status: **all questions answered by the user (2026-11-03, section 2, D9-D19). Ready to build.**

## 1. Goal

The player keeps up to five overlay set-ups ("profiles") and switches between them with one key. They are edited in
the Ctrl+O Overlay Menu, which gets a column of vertical tabs on its left (Profile 1 to Profile 5). Profile 1 is the
default profile and always exists. Profiles 2-5 are off until the player turns them on, and turning one on starts it as
a copy of Profile 1. RightShift+O cycles through the enabled profiles, and a small HUD tag names the new one.

Together with this, two things change:
- The darkening of far overlays loses its key (Shift+O). It stays a row in the Overlay Menu, saved per profile.
- Space dust stops being an overlay. It moves to the Settings screen as one global on/off setting.

Out of scope: the hologram look for the menu (issue #42; the current style is used), renaming profiles, a profile
reset button, profile import/export.

## 2. Resolved decisions (grill-me, the user's answers)

| # | Decision |
|---|---|
| D1 | **Cycle key RightShift+O.** It cycles the enabled profiles. Left Shift+O **does nothing** (it used to toggle darkening). |
| D2 | **Darkening has no key.** It stays a toggle row of the Overlay Menu (the existing `Darkening` row), stored per profile. |
| D3 | **Fixed names** "Profile 1" to "Profile 5", shown as vertical tabs. A small HUD tag shows the active profile, briefly, when the player cycles. No rename UI. |
| D4 | **A profile holds** every overlay toggle row (icon types, orbit kinds, ships on radar, darkening, and any future row) **plus the master O state plus the Alt+O orbit-lines switch** (D18). Space dust is **not** in a profile. |
| D5 | **Space dust is not an overlay.** It leaves the Overlay Menu and the profiles and becomes one global Settings-screen setting. Its migrated value is profile 1's current `SpaceDust` state, and the `SpaceDust` key is removed from every saved profile. SDD 43, `GAME_MECHANICS.md` and the Controls / Info texts are updated to match. |
| D6 | **Persisted changes APPROVED (final, these ONLY):** `SOL::Settings::SAVE_VERSION` **9 -> 10** with a migration step; `KEY_BINDING_LAYOUT_VERSION` **7 -> 10** (D9; supersedes the earlier "7 -> 8"); new per-profile fields **`bEnabled`**, **`bMasterOn`** (D10) and **`bOrbitLinesOn`** (D18); the active-profile index (existing field, any enabled profile); the global dust flag **`bSpaceDust`**; the `SpaceDust` key removed from the profiles. Version and migrate, never discard. The player's custom keys are kept by the migration (except the dropped darkening key, D15). |
| D7 | Persistence follows the project rule: mutate in memory, write at lifecycle boundaries, skip unchanged writes. |
| D8 | Profile 1 is the default profile and cannot be disabled. The first enable of profile 2-5 copies profile 1's settings into it (D12 for re-enabling). At most 5 profiles (`SOL::Overlay::MAX_PROFILES`, already 5). |
| D9 | (Q1) `KEY_BINDING_LAYOUT_VERSION` becomes **10**, the new settings version, so every pre-#34 save (v1-v9) gets the one-time controls-changed notice. The test invariant "SAVE_VERSION > layout" becomes `>=`. |
| D10 | (Q2) Each profile's master O state **is saved** (new per-profile field `bMasterOn`, migrated to on). The pawn no longer forces the master on at flight start: each profile starts as saved. |
| D11 | (Q3) Clicking a tab of an enabled profile makes it the **active** profile immediately (what you edit is what flies). |
| D12 | (Q4) Profiles 2-5 are enabled **independently** (4 without 3 is allowed). The **first** enable of a profile copies Profile 1 into it; **re-enabling** a profile that was used before **restores its last values** (disabling keeps its content). Profile 1 is always enabled. Disabling the active profile falls back to Profile 1. |
| D13 | (Q5) The HUD tag is **brief only**: 2 s at full brightness, then a 0.5 s fade. It shows on every cycle-key press, including one that changes nothing (only Profile 1 enabled). |
| D14 | (Q6) The cycle key (RShift+O) works **in flight and inside the open Overlay Menu**; **PageUp / PageDown** switch tabs in the menu; nothing on the map. Left Shift+O does nothing. |
| D15 | (Q7) A player's custom key for the old Shift+O darkening action is **dropped** by the migration (the action is gone; the controls-changed notice tells the player). |
| D16 | (Q8) The space-dust checkbox goes in **Settings > Graphics** (after Vsync), a normal Apply / Revert setting, default on. Dust is global, out of the profiles; the migration carries Profile 1's current `SpaceDust` value over, then removes the key from the profiles. |
| D17 | (Q9) The Settings copy of the overlay list has the **full profile tabs** (every profile editable there). |
| D18 | (Q10, **changed by the user**) The Alt+O **orbit-lines switch is per profile** (stored in each profile, new field `bOrbitLinesOn`), not global. The migration copies the old global `bShowOrbitLines` into every stored profile. Darkening is a menu row stored per profile (D2). |
| D19 | (Q11) An **"Orbit Lines" row** on each profile's page of the Overlay Menu (so also in the Settings copy, with its tabs), placed above the four orbit-kind rows, which are shown **indented under it** like the icon types under "Icons". It edits the profile's `bOrbitLinesOn` (the same switch as Alt+O). The Settings page's separate **"Show orbit lines" checkbox is removed**. Confirmed with it: `UsedProfiles` is saved as a non-empty row map (no new field), and the legacy `bShowOrbitLines` save field stays, read only by the 9 -> 10 step. |

## 3. What exists today (checked in code at HEAD `870e4a7`)

- **Constants** (`SOLConstants.h`): `SOL::Settings::SAVE_VERSION = 9`, `SOL::Settings::KEY_BINDING_LAYOUT_VERSION = 7`,
  `SOL::Overlay::MAX_PROFILES = 5`. Both version values match the brief.
- **Overlay model** (`UI/SOLOverlayModel.{h,cpp}`, pure): 16 rows in `EOverlay` order. The last two are `Darkening`
  and `SpaceDust`. `SpaceDust` is the only row with `bIgnoresMaster = true`.
  - `FOverlayPrefs` holds `Masks[5]`, `ProfileCount` (always 1 today) and `ActiveProfile`.
  - `IsShown(prefs, bMasterOn, overlay)` takes the master as an argument.
- **Master O** is session state: `USOLSettingsSubsystem::mIsOverlayMasterOn`. The pawn sets it on at every flight start
  (`ASOLShipPawn::BeginPlay`, SDD 22 section 2.3).
- **Save** (`Menu/SOLSettingsSave.{h,cpp}`):
  - Each `FSOLOverlayProfileSave` holds a `TMap<FString, bool> Overlays` (row id -> checked). There are
    `ProfileCount` entries, and `ActiveOverlayProfile` is an index.
  - The migration chain ends with 8 -> 9, which adds `SpaceDust = true` to every stored profile.
- **Keys** (`Menu/SOLKeyBindings.{h,cpp}`):
  - There are 38 bindings. `ToggleDarkening` is `O` with `EKeyChord::Shift` (either Shift).
  - `EKeyChord` has the values None, Shift, Alt and Control.
  - `DefaultLayoutChangedSince(v)` is `v >= 1 && v < KEY_BINDING_LAYOUT_VERSION`. The constant is "the save schema
    that introduced the current default key layout".
- **Pawn input** (`Ship/SOLShipPawn.cpp`): `IA_ShiftChord` is mapped to both Shift keys, and `MapBindingPressed`
  chords a binding by its `EKeyChord`. Plain O stays silent under a modifier only because Enhanced Input blocks a plain
  mapping of a key while a chorded mapping of the same key fires. Left Shift is also thrust up.
- **Write points:**
  - the Overlay Menu's close (`ASOLShipPawn` -> `FlushSave`);
  - the Settings page's `EndVisit`;
  - shutdown (`USOLSettingsSubsystem::Deinitialize`, synchronous);
  - nothing at flight exit.

  The owed-write test compares the overlay prefs with what is on disk (`mWrittenOverlayPrefs`).
- **Dust consumer:** `USOLSpaceDustComponent` reads `SOLOverlay::IsEnabled(Settings->GetOverlayPrefs(),
  EOverlay::SpaceDust)` into `FSOLSpaceDustGate::bRowEnabled`. `-SOLSmokeDust` steps 9 and 10 click the row and
  press O.

## 4. Profile model (pure, `UI/SOLOverlayModel`, test-first)

### 4.1 Table changes

- **Remove `EOverlay::SpaceDust`** and its `OVERLAY_DEFS` row. The table goes back to **15 rows**, with `Darkening`
  last.
- **Remove `FOverlayDef::bIgnoresMaster`**: no row uses it any more. The master now governs every row.
- `Darkening` keeps its id, label and default (on). Its enum comment loses "(Shift+O)".
- `FromSavedProfile` already ignores unknown ids, so a stray `SpaceDust` key in a file is harmless. The migration
  removes it anyway (section 5).

### 4.2 Data

```cpp
struct FOverlayPrefs
{
    uint32 Masks[SOL::Overlay::MAX_PROFILES] = {};  // Rows per profile (bit i = row i checked)
    uint8 EnabledProfiles = 0x01;                   // Bit p = profile p enabled; bit 0 (Profile 1) always set
    uint8 UsedProfiles = 0x01;                      // Bit p = profile p has content (enabled at least once; D12)
    uint8 MasterOnProfiles = 0x1F;                  // Bit p = profile p's master O state (D4, D10)
    uint8 OrbitLinesProfiles = 0x1F;                // Bit p = profile p's Alt+O orbit-lines switch (D18)
    int32 ActiveProfile = 0;                        // Always an enabled profile
};
```

`ProfileCount` is removed: profiles 2-5 can be enabled in any combination (D12), so a count is not enough. The
profile index is 0-based in code. The name shown is "Profile <index + 1>", and the text lives in `SOLMenuText`, not
in the model.

`UsedProfiles` tells the first enable (copy Profile 1) from a re-enable (keep the slot's own content, D12). It is
not a new saved field: a used slot is saved with its full row map, an unused slot with an **empty** row map (section
5.2). An enabled profile is always used.

### 4.3 Functions (contract for the test author)

Unchanged:
- `GetDef`, `FindById`, `AllBitsMask`, `DefaultMask`, `ToSavedProfile`, `FromSavedProfile`.
- `IsEnabled`, `SetEnabled` and `Toggle`, which keep acting on the **active** profile.

Changed or new:

| Function | Contract | Test cases |
|---|---|---|
| `FOverlayPrefs MakeDefaultPrefs()` | All five masks `DefaultMask()`, `EnabledProfiles = 0x01`, `UsedProfiles = 0x01`, `MasterOnProfiles = 0x1F`, `OrbitLinesProfiles = 0x1F`, active 0 | Fields as listed; `IsProfileEnabled` true only for 0 |
| `bool IsProfileEnabled(const FOverlayPrefs&, int32 profile)` | True for profile 0 always; for 1-4, the bit; false out of range | 0 with a corrupt `EnabledProfiles = 0` -> true; -1 and 5 -> false |
| `bool IsProfileUsed(const FOverlayPrefs&, int32 profile)` | True for profile 0 always and for any enabled profile; for 1-4 otherwise, the `UsedProfiles` bit; false out of range | Fresh prefs: only 0; after enable + disable of 2: 2 is used |
| `bool SetProfileEnabled(FOverlayPrefs&, int32 profile, bool bEnabled)` | Returns whether anything changed. Profile 0 or out of range: no change. **First enable** (profile not used): copies `Masks[0]` and profile 0's master and orbit-lines bits into it, then sets its enabled and used bits. **Re-enable** (used, disabled): sets its enabled bit and keeps its own mask, master and orbit bits (D12). Enabling an enabled one: no change. Disabling an enabled one: clears its enabled bit only (content and used bit kept), and if it was active, the active profile becomes 0 | Enable 2 after editing profile 1 -> mask, master and orbit bits equal profile 1's; editing profile 2 afterwards leaves profile 1 untouched; enable twice -> second returns false and keeps the edits; disable active 3 -> active 0; disable 0 -> false; enable 4 with 2 and 3 off -> allowed (D12); edit 2, disable 2, edit 1, re-enable 2 -> profile 2's own edited values, not a copy of 1 (D12) |
| `bool SetActiveProfile(FOverlayPrefs&, int32 profile)` | Sets the active profile if it is enabled; returns whether it changed. A disabled or out-of-range profile leaves it unchanged | Disabled -> false, unchanged; enabled -> true; the same -> false |
| `int32 CycleActiveProfile(FOverlayPrefs&)` | Makes the next enabled profile after the active one active, wrapping 4 -> 0, and returns it. With only profile 0 enabled it stays 0 | {0} -> 0; {0,1} from 0 -> 1 -> 0; {0,2,4} from 0 -> 2 -> 4 -> 0; from a corrupt disabled active, the next enabled one |
| `bool IsMasterOn(const FOverlayPrefs&)`, `void SetMasterOn(FOverlayPrefs&, bool)`, `bool ToggleMaster(FOverlayPrefs&)` | Read or write the active profile's master bit (ToggleMaster returns the new state) | Toggle in profile 2 leaves profile 1's master alone; cycling back restores each profile's own master |
| `bool AreOrbitLinesOn(const FOverlayPrefs&)`, `void SetOrbitLinesOn(FOverlayPrefs&, bool)`, `bool ToggleOrbitLines(FOverlayPrefs&)` | Read or write the active profile's orbit-lines bit (D18; Toggle returns the new state) | Toggle in profile 2 leaves profile 1's switch alone; cycling back restores each profile's own switch |
| `bool IsShown(const FOverlayPrefs&, EOverlay)` | Row checked **and** the active profile's master on **and** parent checked (if any). Replaces the `bMasterOn` overload | Master off -> every row hidden (Darkening included); parent rule as before; per-profile master |
| `bool IsOrbitShown(const FOverlayPrefs&, EOverlay)` | `AreOrbitLinesOn && IsOrbitOverlay && IsShown` (the `bOrbitLinesOn` argument is gone, D18) | As today, with the master and the orbit switch taken from the active profile |
| `FOverlayPrefs Sanitized(const FOverlayPrefs&)` | `EnabledProfiles &= 0x1F` with bit 0 forced on; `UsedProfiles = (UsedProfiles | EnabledProfiles) & 0x1F` with bit 0 on; `MasterOnProfiles &= 0x1F`; `OrbitLinesProfiles &= 0x1F`; the active profile clamped to [0, 4], and set to 0 if that profile is not enabled; every mask `&= AllBitsMask()`; every **unused** slot reset to `DefaultMask()` with its master and orbit bits on. A used but disabled slot **keeps** its content (D12) | Garbage bits stripped; active 3 while 3 is disabled -> 0; an unused slot's custom mask -> default; a used disabled slot's custom mask kept; enabled-but-not-used bit -> used |
| `bool AreEqual(const FOverlayPrefs&, const FOverlayPrefs&)` | Equal after sanitizing: enabled, used, master and orbit bits, active, and all five masks | Differs by master only -> false; by orbit switch only -> false; only in an unused slot -> true; in a used disabled slot -> false |
| `float ProfileTagAlpha(double elapsedS)` | HUD tag fade (section 8): 1 for `0 <= t <= PROFILE_TAG_SHOW_S`, a linear fall to 0 over `PROFILE_TAG_FADE_S`, 0 after; negative or non-finite -> 0 | 0 -> 1; `SHOW` -> 1; `SHOW + FADE/2` -> 0.5; `SHOW + FADE` -> 0; -1 and NaN -> 0 |

### 4.4 New constants (`SOL::Overlay`)

| Constant | Proposed | Meaning |
|---|---|---|
| `PROFILE_TAG_SHOW_S` | 2.0 | The HUD tag at full brightness after a cycle |
| `PROFILE_TAG_FADE_S` | 0.5 | Then it fades out over this time |
| `ALL_PROFILES_MASK` | `0x1F` | `(1 << MAX_PROFILES) - 1`, with a `static_assert` that `MAX_PROFILES <= 8` (a `uint8` mask) |

## 5. Persistence (schema 10, APPROVED D6)

Rule: **version and migrate, never discard.** The persisted changes are listed exhaustively. The implementer reports
exactly which constants and fields it changed, and the main session checks the report against this section.

### 5.1 Fields

| Where | Change |
|---|---|
| `FSOLOverlayProfileSave` | + `UPROPERTY() bool bEnabled = false;` (profile enabled) |
| `FSOLOverlayProfileSave` | + `UPROPERTY() bool bMasterOn = true;` (D10, approved) |
| `FSOLOverlayProfileSave` | + `UPROPERTY() bool bOrbitLinesOn = true;` (D18, approved) |
| `USOLSettingsSave` | + `UPROPERTY() bool bSpaceDust = SOL::Settings::SPACE_DUST_DEFAULT;` (true), the global dust setting |
| `USOLSettingsSave::OverlayProfiles` | Layout unchanged. From schema 10 it is **always written with `MAX_PROFILES` (5) entries**, so each slot's enabled flag is explicit. A used slot (enabled now or before) is written with its full row map; an unused slot with an **empty** row map (that is how `UsedProfiles` is saved without a new field) |
| `USOLSettingsSave::ActiveOverlayProfile` | Unchanged field (0-based index), now any enabled profile |
| `USOLSettingsSave::bShowOrbitLines` | **Kept as a legacy field**, read only by the 9 -> 10 step (the source of every stored profile's `bOrbitLinesOn`). From schema 10 it is no longer read by `GetValues`; it is not removed, so an old file's value can always be migrated |
| `SOLSettingsModel::FSettingsValues` | + `bool bSpaceDust = SOL::Settings::SPACE_DUST_DEFAULT;`, joining `Sanitized`, `AreEqual` and `IsDirty` like every other field. `bShowOrbitLines` leaves `FSettingsValues` (the switch now lives in `OverlayPrefs.OrbitLinesProfiles`), together with `SetAppliedShowOrbitLines` and the orbit arguments of `SOLSettingsRules::IsSaveOwed` |
| `SOL::Settings::SAVE_VERSION` | 9 -> **10**; the schema comment gains the 9 -> 10 step |
| `SOL::Settings::KEY_BINDING_LAYOUT_VERSION` | 7 -> **10** (D9) |

### 5.2 Load (`USOLSettingsSave::GetValues`)

- Entry 0 is always enabled.
- Entry p (1-4) is enabled if `bEnabled` is true. A missing entry is disabled and unused, and entries beyond 5 are
  ignored.
- Entry p is **used** if it is enabled or its row map is not empty.
- The masks come from `FromSavedProfile`, the master bits from `bMasterOn`, the orbit-lines bits from
  `bOrbitLinesOn`.
- `bSpaceDust` is read as is.
- The model sanitizes the result (section 4.3).

### 5.3 Migration step 9 -> 10 (`MigrateToCurrentVersion`)

It runs in memory at load. The file is first rewritten at the next normal write point, with no extra write at load.
Ids are **literals** in the step: a migration stays fixed even if the live table changes, as in the 8 -> 9 step.

1. **Global dust.** `bSpaceDust` = the `SpaceDust` value of `OverlayProfiles[0]` if that entry exists and has the
   key, else `true` (the default).
   - A v8-or-older file reaches this step after 8 -> 9 has added `SpaceDust = true`, so it migrates to on.
2. **Remove `SpaceDust`** from every stored profile's map. Every other key is kept.
3. **Enable every stored entry.** In schemas 7-9 every stored entry was a live profile (the count was the number of
   entries, 1 in practice). Each gets `bEnabled = true` and `bMasterOn = true` (the old "on at flight start"
   behaviour, D10).
   - Entries are **not** padded to 5 here: a missing entry already reads as disabled and unused.
   - `ActiveOverlayProfile` is kept.
4. **Orbit lines per profile (D18).** Every stored entry gets `bOrbitLinesOn` = the file's global `bShowOrbitLines`
   (true if the file is so old that it never had the field: its default). The legacy field is left as it is.
5. **Key overrides (D15).** Remove the `ToggleDarkening` override if there is one, since that action no longer
   exists. Every other override is kept verbatim. `ResolveKeys` would ignore the stale id anyway, but the step makes
   the drop explicit.
6. `Version = 10`.

Running the step a second time is a no-op, because `Version` is then 10 and the step is skipped.

### 5.4 Writes (D7)

- **No direct write** from the cycle key, an O press, an Alt+O press, a menu click or a tab click. Each one only
  changes the applied prefs in memory (`SetAppliedOverlayPrefs`).
- The owed-write test compares the applied prefs with what is on disk with `AreEqual` (so an O or Alt+O press owes a
  write, D10 / D18). The separate orbit-lines comparison (`mWrittenShowOrbitLines`) goes away: the switch is part of
  the prefs. The rule becomes `SOLSettingsRules::IsSaveOwed(bool bWriteOwed, const SOLOverlay::FOverlayPrefs&
  applied, const SOLOverlay::FOverlayPrefs& written)` = `bWriteOwed || !SOLOverlay::AreEqual(applied, written)`.
- **Write points:** the Overlay Menu's close (as today), the Settings `EndVisit`, shutdown, and **new: `ASOLShipPawn`
  `EndPlay` calls `FlushSave()`**. Leaving flight is a lifecycle boundary in the project persistence standard, and the
  call writes only if a write is owed. A cycle followed by a crash before any boundary is lost (the accepted
  trade-off). On quit and PIE end (`EEndPlayReason::Quit` / `EndPlayInEditor`) `EndPlay` starts no write: the engine
  shuts the game instance down right after, and `USOLSettingsSubsystem::Deinitialize` writes the owed change then,
  synchronously, so no async write is still in flight on the same file.
- Several changes in one visit go out in one async save (`AsyncSaveGameToSlot`, as today).
- `bSpaceDust` is a normal Settings field (Apply / Revert, written at `EndVisit`).

## 6. Keys and Enhanced Input

### 6.1 Binding table (`SOLKeyBindings`)

- `EKeyChord` gains **`RightShift`**, appended after `Control` (the enum is not persisted). Its shown prefix is
  `"RShift+"` (`KEY_BINDING_CHORD_PREFIXES`).
- `EBinding::ToggleDarkening` is **replaced in place** by `EBinding::CycleOverlayProfile`:
  - id `CycleOverlayProfile`, default key `O`, chord `RightShift`;
  - area "Overlays", label "Cycle overlay profiles" (`SOLMenuText::KEY_BINDING_TEXTS`).
  - The binding count stays **38**.
- `CanBindKeyTo`: a `RightShift` chord refuses what a `Shift` chord refuses (every modifier key, and the left, right
  and middle mouse buttons).
- `FindConflict`: the `Shift` and `RightShift` chords **overlap**. Right Shift held satisfies both, so the same key
  under both conflicts. Other chord pairs stay independent.
- `IsShiftChord` keeps meaning `Shift` only. A new `IsRightShiftChord` is added only if a caller needs it.

### 6.2 Pawn mapping (`ASOLShipPawn`)

- A new `IA_RightShiftChord` (Boolean) is mapped to **`EKeys::RightShift` only**, in the ship's context.
  `MapBindingPressed` maps a `RightShift` binding with `SOLInput::MapChordedKey(..., RightShiftChordAction)`.
- **Left Shift+O does nothing (D1).** Without the old Shift+O mapping, nothing would block plain O while Left Shift is
  held, so Left Shift+O would toggle the master. The fix: every **plain** mapping whose key is also used by a
  `RightShift` chord binding gets an explicit `UInputTriggerChordBlocker` whose chord action is `IA_ShiftChord`
  (either Shift).
  - Plain O is then silent under either Shift. Right Shift+O fires only the cycle, Left Shift+O fires nothing, and
    Left Shift still thrusts up.
  - The implementer first reads the engine's chord blocking (`EnhancedPlayerInput.cpp`, `InjectChordBlockers`). It
    confirms that the auto-injected blocker for the Right Shift chord and the explicit blocker coexist, and that
    neither blocks the chorded cycle mapping itself. If this cannot be confirmed, it stops and reports back.
- `IA_OverlayDarkening`, its handler `OnToggleDarkeningAction` / `HandleToggleDarkening` and its `EBinding` switch case
  are removed. `IA_CycleOverlayProfile` is added, with `OnCycleOverlayProfileAction` -> `HandleCycleOverlayProfile`.
  The handler calls `Settings->CycleOverlayProfile()` and then tells the HUD to show the tag (section 8).
- AUTO's modifier grace (`SHIFT_CHORD_GRACE_S`) needs no change: Right Shift is not a thrust key by default.
- **Where it works (Q6):** the ship's context only, so not on the map (as O and Ctrl+O). The open Overlay Menu has
  UI-only focus, so it handles the cycle key itself (section 7).

### 6.3 Settings subsystem API (`USOLSettingsSubsystem`)

- `IsOverlayMasterOn()` / `SetOverlayMasterOn(bool)` keep their names but read and write the **active profile's**
  master bit (`SOLOverlay::IsMasterOn` / `SetMasterOn`) through `SetAppliedOverlayPrefs`. `mIsOverlayMasterOn` is
  removed.
- **Flight start (D10):** the `ASOLShipPawn::BeginPlay` call that forces the master on is removed, so each profile
  starts as saved.
- `AreOrbitLinesShown()` / `SetOrbitLinesShown(bool)` keep their names but read and write the **active profile's**
  orbit-lines bit (`SOLOverlay::AreOrbitLinesOn` / `SetOrbitLinesOn`) through `SetAppliedOverlayPrefs` (D18). Alt+O
  (`HandleToggleOrbitLines`) and the orbit-line visuals keep calling them. `mWrittenShowOrbitLines` is removed.
- New `int32 CycleOverlayProfile()` (returns the new active profile), `bool SetActiveOverlayProfile(int32)` and
  `bool SetOverlayProfileEnabled(int32, bool)`. Each applies at once, and the write waits for a write point.
- New `bool IsSpaceDustOn() const` (the applied value).
- `IsOverlayShown` / `IsOverlayOrbitShown` use the new model signatures.
  - Their callers are unchanged: the HUD, the orbit-line visuals and the icon layer.
  - `SOLOrbitLineVisuals.cpp:391` keeps calling `IsOverlayMasterOn()`.

### 6.4 Rebinding and conflicts

The Settings key list shows "RShift+O". Rebinding the cycle to another key keeps the Right Shift chord. A player
binding a Shift-chord action (Shift+M map, Shift+J jump map) to O, or the cycle to M or J, gets the usual conflict
flag through the overlap rule.

### 6.5 `KEY_BINDING_LAYOUT_VERSION` (D9)

The constant is "the save schema that introduced the current default key layout". `DefaultLayoutChangedSince(v)`
shows the one-time controls-changed notice for `1 <= v < constant`. The new layout ships with schema 10, so the
constant becomes **10** and every save older than #34 (v1-v9) gets the notice. The test invariant
"SAVE_VERSION > layout" becomes `>=`.

## 7. Overlay Menu (`USOLOverlayMenuWidget`, current style)

### 7.1 Layout

- A `UHorizontalBox` inside the existing panel: on the left a `UVerticalBox` of **5 tab rows**, on the right the
  existing row list.
- Tab text, in mono:
  - `"Profile N"`;
  - the active tab gets a `">"` marker and Secondary colour;
  - an enabled tab is Primary;
  - a disabled tab is dim with `"(off)"`.
- All in the current style: Primary text on a dark translucent panel, the highlighted entry in Secondary. The hologram
  look is #42.
- The title reads `OVERLAYS - Profile N  ({OverlayMenu} / Esc close)`.

### 7.2 The viewed profile

The widget keeps `mViewedProfile`.

- **Enabled tab:** clicking it makes it the active profile and shows its rows (**D11**: viewing = activating, so what
  you edit is what flies).
- **Disabled tab:** clicking it shows its page, which has a single row. Its text is truthful about D12:
  - never used: `"[ ] Enable (starts as a copy of Profile 1)"`;
  - used before: `"[ ] Enable (restores its last settings)"`.

  Toggling that row enables the profile (`SetProfileEnabled`), makes it active and shows its rows.
- **Pages of profiles 2-5:** each ends with a row `"[x] Profile enabled"`. Unchecking it disables the profile (its
  content is kept for a re-enable), and the view and the active profile go back to Profile 1. Profile 1 has no such
  row.
- **The row list** is the 15 table rows (Space Dust gone), plus the **"Orbit Lines" row** (D19), plus that enable
  row (profiles 2-5): 16 rows on Profile 1's page, 17 on the others.
  - The "Orbit Lines" row is not a table row (`EOverlay` is unchanged by it): it shows and toggles the viewed
    profile's orbit-lines bit (`AreOrbitLinesOn` / `SetOrbitLinesOn`, the same switch as Alt+O).
  - It sits just above "Planet Orbits". The four orbit-kind rows (Planet, Asteroid, Moon, Other Orbits) are drawn
    indented under it, the same way and with the same dimming as the icon types under "Icons". This is display
    only; the table's `ParentRow` of the orbit rows stays `INDEX_NONE`, and `IsOrbitShown` already requires the
    switch.
  - The Settings page's `ESOLSettingsToggle::OrbitLines` checkbox, its handler and its label are removed (D19).

### 7.3 Input

- **Mouse:** a click on a tab or a row (the hit test on cached geometry extends to the tabs). Hover highlights.
- **Keyboard:** Up / Down move between rows, Space / Enter toggle, **PageUp / PageDown move between tabs** (**D14**;
  PageUp = the tab above, PageDown = the tab below, with the same end behaviour as Up / Down in the row list), and
  Esc or Ctrl+O close. Moving onto a tab acts as clicking it (D11): an enabled profile becomes active, a disabled one
  shows its enable page and the active profile stays.
- **The cycle key** (Right Shift + the `CycleOverlayProfile` key) cycles the active profile inside the menu too, the
  view follows, and the HUD tag shows (**D13, D14**). The key is checked with the right-Shift modifier state of the
  `FKeyEvent`.

### 7.4 Embedded in Settings (D17)

The Settings page embeds the same widget with its tabs, so every profile can be edited there too. A click applies at
once and is written at `EndVisit`, as today. The embedded copy stays mouse-driven, as its rows are today (no keyboard
highlight): tabs are clicked; PageUp / PageDown and the cycle key belong to the Ctrl+O menu (D14).

### 7.5 Texts and widgets

- `OVERLAY_MENU_HINT` drops "(not Space Dust)" and adds "PgUp / PgDn: profile" and "{CycleOverlayProfile}: next
  profile". `OVERLAY_MENU_HINT_EMBEDDED` drops "(not Space Dust)" and says only what the mouse-driven copy does (7.4):
  click a tab to show a profile, and `{CycleOverlayProfile}` cycles in flight.
- The viewed tab has a faint highlight background, so a disabled profile's page shows which tab it belongs to (the
  active marker may be on another tab).
- The pawn sends the HUD tag through the HUD event bus (`ESOLHudCommand::ShowOverlayProfileTag`), keeping the rule
  that the pawn never includes the HUD.
- Enhanced Input note (from the engine source): a mapping with any chord trigger, a blocker included, is ordered
  among the chorded mappings, and auto blockers are injected only into same-key mappings after a chorded one. So
  `SOLInput::BlockPlainMappingsUnderChord` re-adds each Shift-blocked plain mapping at the end of the ship context.
- The widget builds all 5 tab widgets once in `NativeOnInitialized`, as `UPROPERTY(Transient)` `TObjectPtr` arrays
  like the rows.
- A profile switch re-reads the 16 row texts once. Nothing runs per frame.

## 8. HUD profile tag (`SOLFlightHud`)

- **Text:** `SOLMenuText::HUD_OVERLAY_PROFILE_TAG_FORMAT` = `"OVERLAYS: PROFILE {N}"`, in Secondary, in a small holo
  panel, centred under the controls-notice line (`DrawBufferCentered` + `AddHoloPanel`, as the controls notice).
- **When:** after every cycle-key press, including one that changes nothing (only Profile 1 enabled, so the tag says
  "PROFILE 1" and the player sees that the key worked).
  - It shows for `PROFILE_TAG_SHOW_S` and then fades over `PROFILE_TAG_FADE_S` (`ProfileTagAlpha`).
  - It is not shown at a menu tab click (the menu itself shows the profile), but it is shown by the cycle key inside
    the menu (D13: every cycle-key press).
  - **D13:** brief only (no permanent tag).
- **Trigger:** `HandleCycleOverlayProfile` calls a new `ASOLFlightHud::NotifyOverlayProfileShown(int32 profile)`,
  which stores the real time and formats the text **once** into a reserved buffer. No per-frame string building.
- **Gates:** as the rest of the HUD (hidden with the HUD, under the main menu and during a jump warp). The master O
  does not hide it (it is a notice, not an overlay).

## 9. Space dust moves to Settings (D5)

- **Model:** `EOverlay::SpaceDust` and `bIgnoresMaster` are removed (section 4.1). The dust is governed by
  `FSettingsValues::bSpaceDust`.
- **Settings page:**
  - A new `ESOLSettingsToggle::SpaceDust` checkbox, labelled "Space dust" (`SOLMenuText`), in the **Graphics**
    section after Vsync (**D16**).
  - It is a normal setting: pending until Apply, Revert discards, written at `EndVisit`.
  - No live preview is needed: Settings is reached from the pause menu or the main menu, where the dust is hidden
    anyway.
- **Component:**
  - `USOLSpaceDustComponent` reads `Settings->IsSpaceDustOn()` (the applied value).
  - Without settings it falls back to `SOL::Settings::SPACE_DUST_DEFAULT`, which replaces `mIsDefaultRowEnabled`.
  - `FSOLSpaceDustGate::bRowEnabled` is renamed `bSettingEnabled` (a mechanical rename; `SpaceDustMathTest` follows).
  - The `sol.SpaceDust` cvar's help text says "while the Settings 'Space dust' option is on".
- **Master O:** still has no effect on the dust (now trivially, since the dust is not an overlay).
- **`-SOLSmokeDust`:**
  - Step 9 ("overlay row") becomes "Settings option": set the pending `bSpaceDust` off and apply, check that the dust
    is hidden, set it on again, apply, then `EndVisit` with exactly one settings write.
  - The `DustSmoke_Menu.png` shot is replaced by `DustSmoke_Settings.png` (the Settings page with the option).
  - Step 10 (master O, no effect) stays.
- **SDD 43** gets Amendment 3: D5 / Q2 / Q3 / Q6 and section 6 are superseded by this SDD. The 8 -> 9 migration step
  stays in the chain as written.

## 10. Documentation updates (same change as the code)

- **`Docs/GAME_MECHANICS.md`:**
  - **O:** the active profile's master, per profile, saved with the profile (no longer forced on at flight start).
  - **Alt+O:** the active profile's orbit-lines switch, per profile, saved (D18); also the "Orbit Lines" row of the
    Overlay Menu (D19); no longer a Settings checkbox.
  - **Ctrl+O:** vertical profile tabs, enable / disable, the first-enable copy from Profile 1 and the re-enable
    restore, 15 rows, no Space Dust, PgUp / PgDn.
  - **Shift+O:** replaced by **RShift+O**, which cycles the enabled profiles with the HUD tag and its times. Left
    Shift+O does nothing.
  - **Darkening:** an Overlay Menu row only (the numbers stay).
  - **Space dust section 2.1:** the "Overlay row" row becomes "Settings option (Graphics, global, default on)". The
    gate table row says "Settings 'Space dust' off".
  - The saving notes get the new write point (leaving flight).
- **In-game text (`Menu/SOLMenuText.h`), so the player-facing text stays truthful:**
  - the Controls rows (`@ToggleDarkening` -> `@CycleOverlayProfile`, "Cycle overlay profiles (RShift+O)");
  - the `@OverlayMaster` row ("in the active profile");
  - the `@OverlayMenu` row (profiles, no space dust);
  - the `INFO_PROSE` overlays paragraph: profiles, the cycle, darkening as a menu row, and space dust as a Settings
    option (the dust sentence moves there);
  - the menu hints, the HUD tag format, the Settings label, and `KEY_BINDING_TEXTS`.
- **`Docs/ARCHITECTURE.md`:**
  - the `SOLOverlay` bullet (profiles with enabled and master bits, schema **10**; the "schema 7" there is stale);
  - the `USOLSettingsSubsystem` bullet (no session master, the dust flag, the pawn `EndPlay` write point);
  - the `Visuals/` dust bullet (reads Settings, not the overlay table);
  - the input notes (`IA_RightShiftChord`, the explicit Shift blocker on plain O);
  - `-SOLSmokeOverlay`'s description.
- **`Docs/SDDs/22-...`:** an Amendment 3 note that section 2.7 is delivered by #34, and that the Shift+O row of
  section 2.2 is replaced.
- **`Docs/TECH_DEBT.md`:**
  - the smoke-flake item names "Shift+O" (line 53). Update the wording to the new steps;
  - add any debt found.
- **`README.md` gallery:** add 2-3 new shots (`Docs/Images/34-*.png`) per the project rule:
  - `34-overlay-profile-tabs.png`: the menu with Profile 2 active and Profile 4 off;
  - `34-overlay-profile-tag.png`: the HUD tag after RShift+O;
  - optionally `34-settings-space-dust.png`.

  The feature list and smoke list mention profiles. The metrics come from `Tools\UpdateMetrics.bat`.
- **`Docs/Plans/STATUS.md`:** kept in sync by the coordinator.

## 11. Tests (written by a separate subagent from this contract, no implementation shown)

| File | Cases |
|---|---|
| `Tests/OverlayModelTest.cpp` (`SOLTest.OverlayModel.*`) | **Re-validate every test:** the table is 15 rows, `Darkening` last, `FindById("SpaceDust")` gives `INDEX_NONE`, a saved map with `SpaceDust` ignores it, and the SpaceDust / ignores-master tests are deleted. Then every row of section 4.3. |
| `Tests/SOLSettingsRulesTest.cpp` | The 9 -> 10 step: dust from profile 0 (true, false, missing key -> true, no profiles -> true); `SpaceDust` removed from every profile, other keys kept; every stored entry enabled with `bMasterOn` on; every stored entry's `bOrbitLinesOn` = the old global `bShowOrbitLines` (true and false); active index kept; the `ToggleDarkening` override dropped and other overrides verbatim; idempotent; a v6 and a v8 save through the whole chain end with dust on, profile 1 enabled and no `SpaceDust` key; the "schema is 9" pins become 10. `IsSaveOwed` loses its orbit arguments. |
| `Tests/SOLSettingsModelTest.cpp` | `bSpaceDust` default, `Sanitized`, `AreEqual`, `IsDirty`, `Revert`; overlay prefs equality now covers the enabled, used, active, master and orbit bits; `bShowOrbitLines` / `SetAppliedShowOrbitLines` tests removed or moved to the prefs. |
| Save round trip (`SOLSettingsRulesTest.cpp` or the existing save test) | `SetValues` writes 5 entries; an unused slot is written with an empty row map, a used disabled slot with its full map; `GetValues` reads enabled, used, master and orbit bits back unchanged. |
| `Tests/SOLKeyBindingsTest.cpp` | Still 38 bindings; `FindById("ToggleDarkening")` -> `INDEX_NONE`; `CycleOverlayProfile` default `O` with `RightShift`; shown "RShift+O"; `RightShift` refuses modifiers and mouse buttons; O / Alt+O / Ctrl+O / RShift+O do not conflict; Shift+X and RShift+X do; `ResolveKeys` ignores a stale `ToggleDarkening` override; the Controls cell rows. |
| `Tests/FlightModeMathTest.cpp` | Layout and schema pins (Q1); `DefaultLayoutChangedSince` cases for 7, 8, 9 and 10. |
| `Tests/SpaceDustMathTest.cpp` | The gate field rename only. |
| `Tests/SOLInfoTextTest.cpp` and the Controls-cell tests | Re-validate any expectation that names Shift+O, `ToggleDarkening` or the dust row. |

The pawn mapping, the widget and the HUD tag need a world, so `-SOLSmokeOverlay` covers them (section 12).

## 12. Smoke (`-SOLSmokeOverlay`, extended; smoke settings slot; real injected key events)

The existing steps stay, except the old Shift+O pair. New and changed steps:

1. **Start:** Profile 1 active, only Profile 1 enabled, master on, tag hidden.
2. **Right Shift+O with only Profile 1:** still active 0, the tag shows "PROFILE 1", nothing else changed.
3. **Left Shift+O:** the master, darkening, the active profile, the menu and every row unchanged; no tag (D1). Left
   Shift still thrusts (checked by `-SOLSmokeInput`).
4. **Ctrl+O:** the menu has 5 tabs, and Profile 1's page has 16 rows (the 15 table rows and "Orbit Lines"; Profile 1
   has no enable row). The four orbit rows are indented under "Orbit Lines".
5. **Click the Profile 3 tab:** its enable page (1 row). Click Enable: Profile 3 enabled and active, with masks equal
   to Profile 1's. Screenshot `OverlaySmoke_Tabs.png`.
6. **Click Planets in Profile 3:** unchecked in 3, still checked in 1.
7. **PageUp:** the view moves to Profile 2 (disabled, its enable page).
8. **Esc:** the menu closes with **exactly one** settings write.
9. **Right Shift+O:** active 3 -> 1 (wraps past disabled 4 and 5, and past 2). Planet icons count > 0. The tag shows
   "PROFILE 1". Screenshot `OverlaySmoke_Tag.png`.
10. **Right Shift+O:** back to Profile 3; planet icons 0.
11. **O in Profile 3:** its master off, 0 icons. Right Shift+O to Profile 1: master on, icons back (D4). Right
    Shift+O: Profile 3 still off.
12. **Alt+O in Profile 3:** its orbit lines off; Right Shift+O to Profile 1: orbit lines on; back to Profile 3: off
    (D18). Clicking the "Orbit Lines" row in the menu turns Profile 3's switch back on (D19).
13. **Disable Profile 3 in the menu:** active back to Profile 1. **Re-enable Profile 3:** its own values come back
    (Planets unchecked, master and orbit lines as left), not a copy of Profile 1 (D12).
14. **Leave flight** (pawn `EndPlay`): one write if owed, none if nothing changed since the last write. The smoke
    runs the pawn's `EndPlay` write point (`FlushSettingsAtLeave`) with a level-transition and a destroyed reason and
    counts the writes; with a quit / PIE-end reason it starts none, and the real quit's shutdown write is in the log.

It runs at 1280x720 and 1000x800, and the screenshots are looked at. Regression smokes: Input, Hud, Dust (updated
step 9), Interact, Map. The README shots come from these runs or a PIE session.

## 13. Performance

- **Event-driven only:** the cycle, the tab clicks and the O press are O(5) bit operations. The menu re-reads its
  16 rows on a profile switch, not per frame.
- **The HUD tag** is one centred text and one holo panel, only while visible. Its string is formatted once per cycle
  into a reserved buffer, so the frame has no `FString` building and no allocation.
- **`IsShown`** reads one more bit (the active master) than before. It is called per icon category, not per icon.
- **No disk I/O on any key press.** The write happens at boundaries only (section 5.4).
- **No new ticking objects.** Nothing scales with the entity count.

## 14. Questions

### 14.1 Resolved (the user's answers, 2026-11-03)

| # | Question | Answer |
|---|---|---|
| Q1 | `KEY_BINDING_LAYOUT_VERSION` value | **10** (D9), so every pre-#34 save gets the controls-changed notice; custom keys kept |
| Q2 | Master O per profile: persisted or session-only? | **Persisted**, new per-profile field `bMasterOn` approved (D10) |
| Q3 | Does a tab click activate the profile? | **Yes**, immediately (D11) |
| Q4 | Enable / disable rules | Independent enables; **first** enable copies Profile 1, **re-enable restores** the profile's last values; Profile 1 always on; disabling the active one falls back to Profile 1 (D12). This changes the earlier recommendation (re-enable = fresh copy) |
| Q5 | HUD tag | **Brief only**, 2 s + 0.5 s fade (D13) |
| Q6 | Where the cycle key works | **Flight and inside the menu**; PgUp / PgDn switch tabs in the menu; nothing on the map; Left Shift+O does nothing (D14) |
| Q7 | The custom Shift+O darkening key | **Dropped** (D15) |
| Q8 | Space dust in Settings | **Graphics**, normal Apply / Revert, default on; the migration carries Profile 1's value over (D16) |
| Q9 | Tabs in the Settings copy | **Yes** (D17) |
| Q10 | Alt+O orbit lines global or per profile? | **Changed by the user: per profile** (stored in each profile, field `bOrbitLinesOn`), not global (D18). This reverses the recommendation (global) |

| Q11 | (Raised by the Q10 change.) Where is a profile's orbit-lines switch edited besides Alt+O: a menu row, the Settings checkbox (now per active profile), both, or neither? | **(a)**: an "Orbit Lines" row on each profile's page (also in the Settings copy), the orbit-kind rows indented under it, and the Settings "Show orbit lines" checkbox removed (D19). The `UsedProfiles` encoding and the legacy `bShowOrbitLines` field were confirmed at the same time |

No question is open.

## 15. Risks

- **Enhanced Input chord blocking** (section 6.2): an explicit Shift blocker on plain O, next to an auto-injected Right
  Shift blocker, is new to this project. The engine source is read first, and the smoke's step 3 proves Left Shift+O
  is silent.
- **The Right Shift key on some layouts** (laptops, non-US): it may be absent or awkward. The binding is rebindable,
  but the chord stays Right Shift. Note it in the Controls text if found.
- **The Settings scroll layout:** five tabs beside the embedded list widen it. Check it at 1000x800.
- **The migration's use of the live table:** the existing 6 -> 7 step writes `ToSavedProfile(DefaultMask())` from the
  live table. Once `SpaceDust` is gone from the table, a v6 save gets no dust key at 7, and 8 -> 9 then adds it. The
  chain still ends correctly; a test pins this.

## 16. Note: the Lanes row (issue #38)

[SDD 38](38-npc-lane-lines.md) adds the "Lanes" row (id `Lanes`, default on, top level, before Darkening) to the
overlay table, so every profile now holds 16 rows. Settings save schema 12 -> 13: a frozen migration step adds
`"Lanes" = true` to every **used** profile's row map and leaves unused (empty) maps empty, so `UsedProfiles` is
unchanged (section 5.1's rule). The master O governs it, and the first enable of a profile copies Profile 1's bit, as
for every row. `KEY_BINDING_LAYOUT_VERSION` stays 10 (no new key).

Issue #49 ([SDD 49](49-lane-guidance-tunnel.md)) lets the same per-profile Lanes bit also show the lane tunnels and
their wall labels; nothing is added to the profile model or the save (schema 13 and layout 10 unchanged).
