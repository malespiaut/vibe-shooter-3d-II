#!/usr/bin/env python3
"""Numbered 64x64 test-pattern textures for the renderer (issue #13).

TEST FIXTURE, not game art: these never ship in a level. They exist so the
M1 renderer can be checked for orientation, scale, straight edges and
texture swimming before any real wall art exists. maps/m1test.vmap refers
to them by name (test01, test02, ...).

Each texture testNN is drawn directly in palette indices (no colour
matching, so every pixel is in the palette and never reserved):
- a background from one ramp (testNN picks ramp NN), mid shade;
- a 1 px border in the ramp's darkest shade and 16 px grid lines a few
  shades darker than the background;
- a white arrow in the centre pointing up (checks vertical orientation);
- the texture number in white digits at the top left, readable left to
  right (checks that every face is drawn unmirrored).

Output: <out>/testNN.tex through libbast (tools/bast.py), NN = 01..count.

Usage: python3 tools/testpattern.py [-o assets/gen/test] [--count 8]
(libbast.so from $BAST_LIB or build/libbast.so)
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402

SIZE = 64
WHITE = 1  # palette index 1 is white (tech.md Palette)
BLACK = 0

# (first index, length) of palette ramps in catlantis.gpl order, darkest first.
RAMPS = [
    (18, 16),   # steel
    (50, 16),   # sand
    (66, 16),   # brown
    (94, 16),   # red
    (110, 12),  # green
    (122, 12),  # blue
    (134, 12),  # violet
    (146, 12),  # orange
    (158, 12),  # cyan
    (170, 12),  # olive
]

# 3x5 digit glyphs, rows top to bottom, '#' = set.
DIGITS = {
    "0": ["###", "#.#", "#.#", "#.#", "###"],
    "1": [".#.", "##.", ".#.", ".#.", "###"],
    "2": ["###", "..#", "###", "#..", "###"],
    "3": ["###", "..#", ".##", "..#", "###"],
    "4": ["#.#", "#.#", "###", "..#", "..#"],
    "5": ["###", "#..", "###", "..#", "###"],
    "6": ["###", "#..", "###", "#.#", "###"],
    "7": ["###", "..#", ".#.", ".#.", ".#."],
    "8": ["###", "#.#", "###", "#.#", "###"],
    "9": ["###", "#.#", "###", "..#", "###"],
}
DIGIT_SCALE = 3


def pattern(n):
    """Return a SIZE x SIZE row-major list of palette indices for testNN."""
    first, length = RAMPS[(n - 1) % len(RAMPS)]
    bg = first + length // 2 + 1
    grid = first + length // 2 - 3
    border = first
    img = [[bg] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            if x % 16 == 0 or y % 16 == 0:
                img[y][x] = grid
            if x in (0, SIZE - 1) or y in (0, SIZE - 1):
                img[y][x] = border

    # Arrow pointing up, centred on x = 31.5: head rows 18..29, shaft 30..47.
    for i in range(12):
        y = 18 + i
        for x in range(31 - i, 33 + i):
            img[y][x] = WHITE
    for y in range(30, 48):
        for x in range(28, 36):
            img[y][x] = WHITE

    # Texture number, two digits, top left, black-shadowed for contrast.
    text = "%02d" % n
    for k, ch in enumerate(text):
        ox = 3 + k * (3 + 1) * DIGIT_SCALE
        oy = 3
        for gy, row in enumerate(DIGITS[ch]):
            for gx, c in enumerate(row):
                if c != "#":
                    continue
                for sy in range(DIGIT_SCALE):
                    for sx in range(DIGIT_SCALE):
                        x = ox + gx * DIGIT_SCALE + sx
                        y = oy + gy * DIGIT_SCALE + sy
                        if x + 1 < SIZE and y + 1 < SIZE and img[y + 1][x + 1] != WHITE:
                            img[y + 1][x + 1] = BLACK
                        img[y][x] = WHITE
    return img


def to_tex(img):
    px = bytearray(SIZE * SIZE)
    for x in range(SIZE):
        for y in range(SIZE):
            px[x * SIZE + y] = img[y][x]  # TEX is column-major
    return bast.Tex(SIZE, SIZE, px)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-o", "--out", default="assets/gen/test")
    ap.add_argument("--count", type=int, default=8)
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)
    if not 1 <= args.count <= 99:
        print("testpattern: --count must be 1..99", file=sys.stderr)
        return 1
    os.makedirs(args.out, exist_ok=True)
    try:
        for n in range(1, args.count + 1):
            to_tex(pattern(n)).save(os.path.join(args.out, "test%02d.tex" % n))
    except bast.BastError as e:
        print("testpattern: %s" % e, file=sys.stderr)
        return 1
    if not args.quiet:
        print("testpattern: %d textures in %s" % (args.count, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
