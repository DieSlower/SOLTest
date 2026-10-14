<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Minor-body targeting

**Origin:** SDD 17 open question 1 (issue #17, orbit lines), answered 2026-10-14.

Today only registry bodies (planets, dwarf planets, moons, the Sun) and registered `ISOLTargetable`s can be targeted (SDD 2). The ~9,268 asteroid-belt and ring-rock Mass entities cannot.

Issue #17 already built the orbit-line side of this: `SOLOrbitLineMath::ResolveTargetLine` returns "show a transient highlighted line" for a target with an orbit but no permanent line, `ASOLOrbitLineVisuals` owns a spare transient line component, and `FLineKey { MinorBody, index }` is the key it expects. What is missing is the targeting side:

- Minor bodies as targeting candidates (nearest-in-cone selection over the Mass entities, probably through a coarse spatial query rather than a full scan; see the `BuildMinorBodyGrid` tech-debt note in `CLAUDE.md`).
- Radar and HUD support (name or catalogue label, distance, the target bracket).
- Mapping the selected minor body to `FLineKey { MinorBody, index }` in `ASOLOrbitLineVisuals` and supplying its elements, so its orbit line appears.
