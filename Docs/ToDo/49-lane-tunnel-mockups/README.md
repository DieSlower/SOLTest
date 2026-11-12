<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Ticket #49 lane tunnel mock-ups

These are the user-approved mock-ups for ticket #49 (lane guidance tunnel). `tunnel8` is the
chosen look:

- Bright HUD cyan rings (`CYAN_COLOR`) with 8 longitudinal guide dots.
- Forward/outbound traffic on the right, inbound on the left.
- Rings and dots stream toward the player at the player's speed.
- Neutral teal-white wedge ships.
- Two versions:
  - `tunnel8_rings_symbols.gif`: floor-strip chevrons.
  - `tunnel8_rings_dots_sides.gif`: left/right wall chevrons at 3 and 9 o'clock, with muted
    green/red guide dots.

The #49 grill-me (2026-11-11) chose `tunnel8_rings_dots_sides.gif` (side-wall chevrons with
green/red guide dots); the design is [`SDD 49`](../../SDDs/49-lane-guidance-tunnel.md).

The scripts `tunnels.py` .. `tunnels8.py` regenerate the GIFs with Pillow (`python -I tunnels8.py`;
`tunnels8.py` imports the earlier ones).

This folder is deleted when #49 is built.
