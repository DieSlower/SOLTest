<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 51: Placeholder main thruster plume

**Issue:** [#51](https://github.com/DieSlower/SOLTest/issues/51). SDD:
[`SDDs/51-thruster-plume.md`](../SDDs/51-thruster-plume.md).
Status: **drafted 2026-11-20 from the grill-me decisions D1-D10. Blocked on the SDD's open questions Q1-Q26 (section
11); no part starts before they are answered, and asset creation (Q20) needs the user's explicit approval.**
**Persistence: none.** `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay. If Q12 is answered "new setting",
that becomes a separate persistence part (version and migrate, user-approved bump, `opus` / xhigh); any other finding
that needs a schema change is stopped and reported.

## Parts

Each part is built by its own subagent (context under about 35%), test first where it adds or changes pure functions,
then reviewed by a fresh adversarial `opus` subagent (performance and style, with the TECH_DEBT items listed in SDD 51
section 3), then committed and pushed (`Issue #51: Part 51x - ...`), with `Docs/Plans/STATUS.md` and the metrics updated
in the same commit. Temporary screenshots, mock-ups and scratch renders are deleted once used.

### 51-0: answers

1. The main session puts Q1-Q26 to the user (`AskUserQuestion`, recommended answer first); the SDD records each answer
   as D11 onward, removes the matching *provisional* marks and freezes the test contract (SDD section 7). The near-set defaults and pixel thresholds stay test parameters until 51e-0.

### 51a: pure math (test first)

1. Test author (`opus`, high, spec only): write `Source/SOLTest/Tests/ThrusterMathTest.cpp` (`SOLTest.Thruster.*`) from
   SDD 51 section 7 cases 1-11 (case 12 waits for 51b's mirror).
2. Run `Tools\RunTests.bat -Build -Filter SOLTest.Thruster`; confirm the cases fail for the right reason.
3. Implement `Visuals/SOLThrusterMath.{h,cpp}` (`SOLThruster::`) and the constants in `SOLConstants.h` (`G_REF` tied
   to `WAR_POWER_PULSE_G`, colour key points, history size, near-set defaults, nozzle table, `PLAYER_HULL_LENGTH_M`).
4. Green run; full suite green.

### 51b: assets (only after the user approves Q20)

1. Asset script (`opus`, high): `Tools/Fx/create_thruster_assets.py` (or the approved extension of
   `Tools/Npc/create_npc_assets.py`): the plume and glow materials (ramp, brightness, knots, streaks, WPO length from
   per-instance data), the procedural noise texture, the cone / glow geometry (under Q1 A, the plume section on the NPC
   hull meshes), the ribbon Niagara system. Paths in `SOL::Paths`. No paid content (D10).
2. The material mirror `.ush` (if used) and test case 12 (`MaterialMirror`), test first.
3. Run the script headless; record the compiled instruction counts.

### 51c: NPC plumes

1. Thrust plumbing (`opus`, high): `MainThrustG` per CPU ship from the Q2 answer (Collect pass for Mid lane ships, the
   Full AI for Full ships, the Q3 rule for non-lane ships); no new allocation, nothing saved; re-validate
   `NpcTrafficMath.ThrustState`, `NpcRail.*` and `Npc.HullFlip.*`.
2. Renderer (`opus`, high): the Q1 path in `Npc/SOLNpcVisuals.cpp` (instanced hulls and the mesh pool), per-instance
   `c` and `b` written in the existing parallel placement pass, pixel-size fade (Q14, Q15).
3. Smokes: `-SOLSmokeNpcTraffic`, `-SOLSmokeLaneMerge`, `-SOLSmokeAutoLanes`; PIE screenshots of a lane showing burn,
   dark flip window and brake.

### 51d: player plume

1. `MainThrustG` on `FSOLShipStateFragment` from the flight processor (Q7, Q8), War power colour override (Q10).
2. Two nozzle plumes on the pawn per the nozzle table; the existing engine-glow spheres per Q19.
3. Re-validate the flight and War power tests; PIE screenshots at idle, 1 G, 3 G, boost (Q6) and War power.

### 51e-0: ribbon distance mock-up (user tuning step, D7)

1. Render ribbons at **50, 20 and 10 km** (and close up) at 1920x1080, scratchpad only, and show the user, so the near
   radius `R`, `K` and the pixel thresholds are chosen from pictures. Record the answers in the SDD; delete the mock-ups
   afterwards.

### 51e: ribbons

1. History ring buffers and the near-set selection (pure parts already in 51a) wired to the Niagara ribbon system;
   player always, `K` NPCs within `R`, scaled by the Q12 hookup; pixel-size LOD.
2. PIE screenshots: the player turning, an NPC flipping close by (the ribbon must curve, not break).

### 51f: tuning, profile, docs, gallery

1. In-game tuning with the user (colours, knot look, streak speed, LOD thresholds).
2. Profile against the SDD section 8 gates (Earth spawn view at 1M default budgets, a busy lane view, the player boost
   and War power view) at 1920x1080 with `stat` / Unreal Insights; 4K and 1280x720 spot checks. **VR record only:**
   one-eye and two-eye XR-4 resolution numbers written to the SDD, not gated.
3. Docs: `GAME_MECHANICS.md` (Thruster plume row, Lane traffic and War power notes), the in-game Info text,
   `ARCHITECTURE.md`, `STATUS.md`, `TECH_DEBT.md` for anything accepted; README gallery 2-3 shots at 1920x1080.
4. Whole-ticket adversarial review; fixes; full suite and smokes re-run; commit and push.

## Tests to write or re-validate

| Test | Part | Action |
|---|---|---|
| `SOLTest.Thruster.*` (cases 1-11) | 51a | new, test first, spec only |
| `SOLTest.Thruster.MaterialMirror` | 51b | new, if a `.ush` mirror is used |
| `SOLTest.NpcTrafficMath.ThrustState` | 51c | re-read against the plume's use (contract unchanged) |
| `SOLTest.NpcRail.*`, `SOLTest.Npc.HullFlip.*` | 51c | re-validate if the Collect pass changes |
| Flight / War power tests (`SOLTest.WarPowerMove.*`, flight model) | 51d | re-validate if `FSOLShipStateFragment` gains a field |

## Performance gates (provisional, SDD 51 Q21)

- NPC redraw CPU delta at most 0.10 ms (Earth spawn view, 1M NPCs, default budgets).
- Plume GPU at most 0.20 ms at 1920x1080 (busy lane view, player boost view).
- Ribbons at most 0.05 ms CPU.
- The default-budget 1M worst case still passes 60 fps (SDD 11 Amendment 6).
- VR one-eye / two-eye: recorded only, not gated (no VR work until after procedural terrain).

## Risks

See SDD 51 section 9: instanced-hull upload cost, translucent smear without motion vectors, the third-person camera
looking down the player's plume, red-orange vs faction 1 orange, the nominal-profile coast, the ribbon frame (Q4), additive
overdraw in busy lanes, script-only assets, true-length plumes on inflated hulls.

## Change log

- 2026-11-20: drafted with SDD 51; parts 51-0 to 51f; blocked on Q1-Q26.
