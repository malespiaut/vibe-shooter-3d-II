#!/usr/bin/env python3
"""Build assets/gen/colormap.bin: 32 shade rows x 256 palette indices.

Rule (docs/design/art-bible.md, "Colormap"; GAME.md 2.3), for row r in 0..31:
    f = (32 - r) / 32, t = r / 31
    target = (R*f, G*f*(1 - 0.10*t), B*f*(1 - 0.25*t))      # warm shift
    entry  = Oklab nearest via libbast (bast.nearest), which already skips
             reserved, fullbright and transparent entries, ties -> lowest.
Row 0 is identity. Fullbright entries and 255 map to themselves in every row.

Monotonic clamp: if the nearest entry for row r is brighter (higher Oklab L)
than the entry chosen for row r-1, row r keeps row r-1's entry. Pure nearest
can oscillate near black, where the palette has a gap between black (L 0) and
the darkest ramp entries (L ~0.14-0.17); the clamp guarantees that shading
never brightens with distance (issue #6 acceptance). The number of clamped
cells is printed.

The float target is rounded to whole sRGB values (floor(x + 0.5)) before the
lookup, because libbast's matcher (the only Oklab implementation, tech.md)
takes 8-bit sRGB. The error is at most half a step per channel.

Usage: python3 tools/colormap.py [--palette assets/gen/palette.bin] [--out FILE]
(libbast.so from $BAST_LIB or build/libbast.so)
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402

ROWS = 32
DEFAULT_OUT = os.path.join(bast.ROOT, "assets", "gen", "colormap.bin")


def round_half_up(x):
    return int(math.floor(x + 0.5))


def target(rgb, row):
    f = (32 - row) / 32
    t = row / 31
    r, g, b = rgb
    return (r * f, g * f * (1 - 0.10 * t), b * f * (1 - 0.25 * t))


def build(pal):
    """Return (colormap bytes, number of clamped cells)."""
    colors, flags = pal.colors, pal.flags
    lum = [bast.srgb_to_oklab(c)[0] for c in colors]
    out = bytearray(ROWS * 256)
    clamped = 0
    for row in range(ROWS):
        for i in range(256):
            if row == 0 or flags[i] & (bast.PAL_FULLBRIGHT | bast.PAL_TRANSPARENT):
                v = i
            else:
                v = pal.nearest(tuple(round_half_up(c) for c in target(colors[i], row)))
                prev = out[(row - 1) * 256 + i]
                if lum[v] > lum[prev]:
                    v = prev
                    clamped += 1
            out[row * 256 + i] = v
    return bytes(out), clamped


def main(argv=None):
    ap = argparse.ArgumentParser(description="build the 32-row colormap")
    ap.add_argument("--palette", default=None, help="palette.bin (default: $BAST_PALETTE "
                    "or assets/gen/palette.bin; palette_flags.bin next to it)")
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args(argv)
    pal = bast.load_palette(args.palette)
    data, clamped = build(pal)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    tmp = args.out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, args.out)
    print("colormap: wrote %s (%d bytes, %d cells clamped for monotonic L)"
          % (args.out, len(data), clamped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
