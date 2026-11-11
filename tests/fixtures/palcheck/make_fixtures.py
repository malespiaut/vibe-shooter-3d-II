#!/usr/bin/env python3
"""Regenerate the palcheck test fixtures (committed, tiny) from catlantis.gpl.

good.png         indexed, game palette, uses ramps, fullbright and 255
bad_rgb.png      RGB image (not indexed)
bad_color.png    indexed, used entry 40 (beige_06) has a wrong colour
bad_reserved.png indexed, game palette, uses reserved index 216 at (5,3)
"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
import palette  # noqa: E402


def indexed(px, pal):
    img = Image.fromarray(px.astype(np.uint8), mode="P")
    img.putpalette(pal)
    return img


def main():
    entries = palette.load_gpl()
    pal = [c for rgb, _ in entries for c in rgb]
    px = np.zeros((8, 16), dtype=np.uint8)
    for y in range(8):
        for x in range(16):
            px[y, x] = [2, 18, 34, 50, 94, 110, 240, 255][y] + (x if y < 6 else 0)
    indexed(px, pal).save(os.path.join(HERE, "good.png"))

    rgb = Image.new("RGB", (16, 8))
    rgb.putdata([tuple(pal[i * 3:i * 3 + 3]) for i in px.flatten().tolist()])
    rgb.save(os.path.join(HERE, "bad_rgb.png"))

    wrong = list(pal)
    wrong[40 * 3:40 * 3 + 3] = [1, 2, 3]
    indexed(px, wrong).save(os.path.join(HERE, "bad_color.png"))

    res = px.copy()
    res[3, 5] = 216
    indexed(res, pal).save(os.path.join(HERE, "bad_reserved.png"))


if __name__ == "__main__":
    main()
