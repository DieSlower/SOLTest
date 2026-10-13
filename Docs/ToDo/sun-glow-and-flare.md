<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Sun glow and flare

Requested by the user while reviewing issue #13 (star field quality): the Sun reads as too small on
screen, and its glow / lens flare should be enlarged so it feels like the dominant light source.

Findings when this was raised:

- The Sun disc itself is physically correct. `SOLRender::ComputePlacement` keeps the angular size
  when it compresses distance, and the registry radius is 6.957e8 m. From 1 AU the disc is about
  0.53 degrees across, so about 11 px at 1920 px wide with a 90 degree FOV. It looks small because
  the real Sun is small in the sky.
- So the change is the glow, not the disc: a larger bloom / halo and flare around the Sun (a
  screen-space or billboard glow whose size follows the Sun's apparent brightness, plus an optional
  lens flare), without changing the disc's true size.

When picked up: grill the look (glow radius vs. distance, lens flare style, interaction with the
smoothed star exposure and bloom intensity, GPU budget), then write the SDD as usual.
