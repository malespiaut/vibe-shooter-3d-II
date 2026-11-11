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

Output: <out>/testNN.tex through libbast (tools/bast.py), NN = 01..count,
plus <out>/testsky.tex: the 320x104 sky fixture (issue #14, art-bible.md
"Sky": 320 px = 90 degrees of view, repeats 4x per turn). It is a vertical
gradient down the indigo ramp, a white tick line every 32 px with the
tick number 0..9 beside it, near the top (so a sky offset can be read off a frame), and a
full-height steel line at column 0 marking the wrap seam.

Test sprite (issue #15), also a fixture: testspr_idle_d<D>_f0.tex for
D = 0..7, the TEST_SPRITE entity's 8 direction frames (tech.md Sprite
naming). 64x64, origin bottom-centre (32, 64), HAS_TRANSPARENCY:
- a transparent border on the left, right and top (the bottom row stands
  on the floor), and a transparent notch in the top edge;
- a panel in ramp D with a 1 px darkest-shade outline;
- the direction number D in large white digits, black-shadowed;
- a white "nose" block low on the panel at the side the thing faces as
  seen from the camera (centre in d0, screen left in d2, screen right in
  d6, absent in d3..d5 where it faces away), so walking around it reads
  as rotation as well as numbers.

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
    draw_digits(img, "%02d" % n, 3, 3, DIGIT_SCALE)
    return img


TRANSPARENT = 255
SPRITE_BORDER = 6
# sin(d * 45 deg) * 2 rounded: nose offset in units of 9 px; None = facing away.
NOSE = {0: 0, 1: -1, 2: -2, 6: 2, 7: 1}


def draw_digits(img, text, ox, oy, scale):
    for k, ch in enumerate(text):
        x0 = ox + k * (3 + 1) * scale
        for gy, row in enumerate(DIGITS[ch]):
            for gx, c in enumerate(row):
                if c != "#":
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        x = x0 + gx * scale + sx
                        y = oy + gy * scale + sy
                        if x + 1 < SIZE and y + 1 < SIZE and img[y + 1][x + 1] != WHITE:
                            img[y + 1][x + 1] = BLACK
                        img[y][x] = WHITE


def sprite_pattern(d):
    """SIZE x SIZE row-major palette indices for test sprite direction d."""
    first, length = RAMPS[d % len(RAMPS)]
    bg, edge = first + length // 2 + 1, first
    b = SPRITE_BORDER
    img = [[TRANSPARENT] * SIZE for _ in range(SIZE)]
    for y in range(b, SIZE):
        for x in range(b, SIZE - b):
            outline = x in (b, SIZE - b - 1) or y in (b, SIZE - 1)
            img[y][x] = edge if outline else bg
    # Notch in the top edge: transparency inside the outline.
    for y in range(b, b + 6):
        for x in range(28, 36):
            img[y][x] = TRANSPARENT
    for x in range(27, 37):
        img[b + 6][x] = edge
    for y in range(b, b + 7):
        img[y][27] = img[y][36] = edge
    # Direction digit, 3x5 glyph at scale 5: 15x25 px, centred.
    draw_digits(img, str(d), (SIZE - 15) // 2, 16, 5)
    if d in NOSE:
        cx = 32 + NOSE[d] * 9
        for y in range(48, 58):
            for x in range(cx - 5, cx + 5):
                img[y][x] = WHITE
    return img


SKY_W, SKY_H = 320, 104
INDIGO = (204, 12)  # night/dusk sky ramp, darkest first (art-bible.md)


def draw_digit(img, ch, ox, oy, colour):
    for gy, row in enumerate(DIGITS[ch]):
        for gx, c in enumerate(row):
            if c != "#":
                continue
            for sy in range(DIGIT_SCALE):
                for sx in range(DIGIT_SCALE):
                    img[oy + gy * DIGIT_SCALE + sy][ox + gx * DIGIT_SCALE + sx] = colour


def sky_pattern():
    """Return a SKY_H x SKY_W row-major list of palette indices."""
    first, length = INDIGO
    img = [[first + y * length // SKY_H for _ in range(SKY_W)] for y in range(SKY_H)]
    for t in range(SKY_W // 32):
        x0 = t * 32
        for y in range(4, 40):
            img[y][x0] = WHITE
        draw_digit(img, str(t), x0 + 6, 6, WHITE)
    for y in range(SKY_H):
        img[y][0] = RAMPS[0][0] + 12  # steel: the wrap seam
    return img


def to_tex(img, origin=(0, 0), flags=0):
    h, w = len(img), len(img[0])
    px = bytearray(w * h)
    for x in range(w):
        for y in range(h):
            px[x * h + y] = img[y][x]  # TEX is column-major
    return bast.Tex(w, h, px, origin, flags)


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
        to_tex(sky_pattern()).save(os.path.join(args.out, "testsky.tex"))
        for d in range(8):
            to_tex(sprite_pattern(d), (SIZE // 2, SIZE), bast.TEX_HAS_TRANSPARENCY).save(
                os.path.join(args.out, "testspr_idle_d%d_f0.tex" % d))
    except bast.BastError as e:
        print("testpattern: %s" % e, file=sys.stderr)
        return 1
    if not args.quiet:
        print("testpattern: %d textures, testsky and 8 sprite frames in %s"
              % (args.count, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
