#!/usr/bin/env python3
"""Write a 320x240 swatch PNG of the game palette (16x16 swatches of 20x15).

Swatch for index i sits at x = (i % 16) * 20, y = (i // 16) * 15, so a row
of the image is 16 consecutive indices. The PNG is indexed (mode P) with the
game palette. It is a review image, not a game asset: it shows the
reserved entries (with a 1 px white diagonal, visibly "not a colour yet"),
so palcheck rejects it by design.

Usage: python3 tools/palette_swatch.py [out.png] [--gpl FILE]
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import palette  # noqa: E402


def swatch(entries):
    fl = palette.flags(entries)
    px = np.zeros((240, 320), dtype=np.uint8)
    for i in range(256):
        x0, y0 = (i % 16) * 20, (i // 16) * 15
        px[y0:y0 + 15, x0:x0 + 20] = i
        if fl[i] & palette.FLAG_RESERVED:
            for k in range(15):
                px[y0 + k, x0 + k + 2] = 1
    img = Image.fromarray(px, mode="P")
    img.putpalette([c for rgb, _ in entries for c in rgb])
    return img


def main(argv=None):
    ap = argparse.ArgumentParser(description="palette swatch PNG")
    ap.add_argument("out", nargs="?",
                    default=os.path.join(palette.DEFAULT_OUT, "palette_swatch.png"))
    ap.add_argument("--gpl", default=palette.DEFAULT_GPL)
    args = ap.parse_args(argv)
    entries = palette.load_gpl(args.gpl)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    swatch(entries).save(args.out)
    print("palette_swatch: wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
