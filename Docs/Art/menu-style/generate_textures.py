# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Regenerates the solid menu textures (SDD 46 section 10) into Docs/Art/menu-style/textures/. Deterministic (fixed seed).
# Run: python Docs/Art/menu-style/generate_textures.py   (needs Pillow and numpy)
import os
import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "textures")
RNG = np.random.default_rng(46)


# Saves an array (0-1 floats) as an 8-bit PNG in the textures folder
def save(name, array, mode):
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray((np.clip(array, 0, 1) * 255 + 0.5).astype(np.uint8), mode).save(os.path.join(OUT, name))


# Brushed grain: gaussian noise blurred along X with a circular (wrapping) kernel, so it tiles both ways; mean 0.5
def brushed(size=512, length=110):
    noise = RNG.normal(0, 1, (size, size))
    kernel = np.zeros(size)
    kernel[:length] = 1.0 / length
    kernel = np.roll(kernel, -length // 2)
    row = np.fft.rfft(noise, axis=1) * np.fft.rfft(kernel)[None, :]
    streak = np.fft.irfft(row, n=size, axis=1)
    fine = RNG.normal(0, 1, (size, size))
    value = streak / streak.std() * 0.55 + fine / fine.std() * 0.18
    return 0.5 + value * 0.12


# Octagonal rivet with a bevel shaded from the top-left light, alpha outside the octagon
def rivet(size=32):
    ys, xs = np.mgrid[0:size, 0:size]
    x = (xs + 0.5 - size / 2) / (size / 2)
    y = (ys + 0.5 - size / 2) / (size / 2)
    octa = np.maximum(np.maximum(abs(x), abs(y)), (abs(x) + abs(y)) / 1.4142) # octagon distance, 1 at the edge
    alpha = np.clip((0.94 - octa) * size / 2, 0, 1)
    rim = np.clip((octa - 0.55) / 0.4, 0, 1)
    light = -(x + y) / 2.83
    shade = 0.62 + 0.30 * light * rim + 0.10 * (1 - rim) - 0.14 * rim * (light < 0)
    rgba = np.stack([shade, shade, shade, alpha], axis=-1)
    return rgba


# Vent strip: three-quarters opaque dark slots on a transparent strip (white = slot, tinted in the material), tiles in X
def vent(width=128, height=16):
    mask = np.zeros((height, width))
    period = 16
    for x in range(width):
        if (x % period) in range(3, 13):
            mask[3:height - 3, x] = 1.0
    # soften the slot ends by a one pixel vertical falloff
    mask[3, :] *= 0.6
    mask[height - 4, :] *= 0.6
    return mask


# Hazard stripe mask: 45 degree diagonal bars (white = amber bar), tiles in X and Y
def hazard(width=128, height=16):
    ys, xs = np.mgrid[0:height, 0:width]
    return (((xs + ys) % 16) < 8).astype(float)


save("brushed-gray-512.png", brushed(), "L")
save("rivet-32.png", rivet(), "RGBA")
save("vent-strip-128x16.png", vent(), "L")
save("hazard-128x16.png", hazard(), "L")
