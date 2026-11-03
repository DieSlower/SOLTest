<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Space dust: speed-cue particles

Research for the "Space dust: speed-cue particles" ticket. Goal: tiny particles stream past the ship so the player feels motion. The apparent speed only says "we are moving" (compressed, saturating, not the true speed), and a ship that is stopped relative to its reference frame shows no particles.

## How other games do it (what could be verified)

Web search found little first-hand developer documentation. Findings and their confidence:

- **Original Elite (1984, BBC Micro):** documented in detail. Every dust particle has its own 3D position; each frame it moves toward the viewer by the current speed (z decreases), is pushed away from the screen centre proportional to speed and inverse depth (perspective), and is rotated by pitch and roll. Particles that leave the view are respawned. Confirmed by the annotated source breakdown.
- **Elite Dangerous:** no developer documentation found. Community posts suggest a screen-space or layered effect with separate dust intensity in supercruise vs normal flight (a HUD mod exposes two brightness settings). Treat as unverified.
- **Freelancer, Everspace, Star Citizen, X4, EVE, No Man's Sky, Homeworld, Limit Theory:** no implementation details found. Do not cite these as references; general knowledge only is that most use camera-attached particle fields or a screen-space layer.
- **Player reception:** dust is divisive. Star Wars: Squadrons players say it helps judge drift (especially in VR); Elite Dangerous and Space Engineers 2 players ask for it to be reduced or removable. Conclusion: provide an intensity setting and an off switch.
- **Generic tutorial pattern:** spawn particles in a volume around the player, move them past the player at a rate tied to player speed, respawn at the far edge when they leave. One stated purpose is letting the eye separate camera motion from ship motion.

## Technical approaches in Unreal

Search found no complete Niagara reference for this exact case; the following is general engineering knowledge and must be verified in-editor.

1. **Niagara GPU emitter in a camera/ship-local box.** Emitter attached to the ship (or camera), particles positioned in a box of side L. Each particle's position wraps with a modulo on the box, so no spawning or killing is needed: position = frac((p0 - offset) / L) * L - L/2, where `offset` is the accumulated "apparent travel" (see mapping below). Fixed particle count, zero allocation, no per-particle CPU work.
2. **Pure-shader / material approach.** Instanced static mesh or a single Niagara mesh/sprite renderer whose world position offset applies the same wrap. Cheapest; same math.
3. **Velocity-aligned streaks.** Sprite renderer with facing mode "velocity" (Niagara Quick Start documents the Velocity facing mode) stretched by apparent speed; streak length grows with the compressed speed value.
4. **Screen-space post-process layer** (the likely Elite Dangerous style): cheap, no particles, but no parallax or depth.
5. **Mass entities:** not recommended. Thousands of cosmetic points do not need entity simulation; the project's "Mass for everything" rule is about gameplay entities. Flag as an SDD decision.

## Floating origin and scale

The universe is double precision with render-origin rebasing (see `Docs/ARCHITECTURE.md`). Dust should live entirely in a small local frame attached to the ship/camera, in single precision, so no universe coordinate ever reaches it and rebasing cannot cause a pop. The only input is a scalar or vector "velocity relative to the reference frame", supplied each frame from the ship state as a Niagara user parameter. The accumulated travel offset must be kept small (wrapped modulo L on the CPU) to avoid float precision loss over long sessions.

## Speed mapping (compressed, saturating)

Let v = ship speed relative to its reference frame (m/s). Apparent speed:

- `a = A_max * (1 - exp(-v / v_ref))` (smooth saturating), or `A_max * tanh(v / v_ref)`, or `A_max * log1p(v / v0) / log1p(v_cap / v0)` (log-compressed, keeps distinguishing orders of magnitude).
- **Stopped means invisible:** opacity = smoothstep(v_min, v_min * 2, v) so particles fade fully out below a small threshold (v_min), with no residual drift. The dead zone avoids shimmer from tiny velocities.
- Streak length may scale with `a`; at very high v, optionally fade alpha slightly to avoid a solid wall of lines.
- Direction: opposite to the ship's velocity vector (relative to the reference frame, not to the ship's facing), expressed in ship/camera space so that strafing and drifting show correctly.

## Performance notes

- Budget: a few thousand GPU sprites (for example 2k to 10k) is negligible; the wrap-in-shader approach has no CPU cost and no GC.
- Use a fixed-count GPU emitter with fixed bounds, a small sprite material (unlit, translucent or additive; avoid lit translucency), no collision, no sorting, no per-particle events.
- Set emitter fixed bounds to the local box so culling is stable. Disable when speed is below threshold (deactivate the system instead of drawing invisible particles).
- Provide an intensity/density scalability setting tied to the settings menu and effects quality.
- Verify with `stat Niagara`, `stat GPU` or Unreal Insights, not by guesswork.

## Open design questions for grill-me

1. **Technique:** GPU Niagara emitter with in-shader wrap (recommended) vs. mesh instancing vs. screen-space post-process.
2. **Speed mapping curve and constants:** exponential, tanh or log; v_ref, A_max, and the stopped threshold v_min.
3. **Reference frame:** relative to what exactly? Parent body, star, current region anchor, targeted object? Behavior while orbiting or co-moving with a planet (zero relative speed means no dust).
4. **Look:** points vs velocity-aligned streaks; color, size and brightness; whether near and far layers (parallax) are wanted.
5. **Camera/third person:** volume centered on the ship or on the camera; behavior during camera zoom and free-look; interaction with level-to-surface flight and atmosphere.
6. **Interactions:** jump/warp (transition to stronger streaks?), time warp, map view, and the menu or hologram screens (hide dust there).
7. **Settings and persistence:** player-facing on/off and intensity slider in the settings menu; if added to the settings save schema it needs a versioned migration approval.
8. **Scope of owner entity:** only the player ship, or also NPC ships and the observer camera.

## Sources

- [Elite: stardust in the front view (BBC Elite source breakdown)](https://elite.bbcelite.com/deep_dives/stardust_in_the_front_view.html)
- [Elite: stardust in the side views](https://elite.bbcelite.com/deep_dives/stardust_in_the_side_views.html)
- [Creating a motion feeling in a game, a.k.a. space dust (Panda3D forum)](https://discourse.panda3d.org/t/creating-a-motion-feeling-in-a-game-a-k-a-space-dust/12295)
- [Best way to produce space dust? (Unreal Engine forums)](https://forums.unrealengine.com/t/best-way-to-produce-space-dust/126449)
- [Space dust / Elite Dangerous player discussions (Frontier forums)](https://forums.frontier.co.uk/threads/more-space-dust.591200)
- [Star Wars: Squadrons discussion on drift dust (Steam)](https://steamcommunity.com/app/1222730/discussions/0/2940245647599192309)
- [Space Engineers 2 request to remove speed particles](https://support.keenswh.com/spaceengineers2/pc/topic/52999-please-remove-the-speed-particles-aka-space-rain-aka-warp-speed-effects)
- [Warframe: space scenes really need space dust](https://forums.warframe.com/topic/267929-space-scenes-really-need-space-dust/)
- [Niagara Quick Start (UE 4.27 docs, Velocity facing mode)](https://docs.unrealengine.com/4.27/RenderingAndGraphics/Niagara/QuickStart)
- [Epic: tutorials for Niagara effects](https://dev.epicgames.com/documentation/unreal-engine/tutorials-for-niagara-effects-in-unreal-engine?lang=en-US)
