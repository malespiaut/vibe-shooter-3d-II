#!/usr/bin/env python3
"""Build the game fonts' indexed PNG sheets from hand-drawn glyph data.

The letterforms are drawn by hand as pixel data in assets/src/fonts/<font>.txt
('#' ink, '.' empty, one block per glyph; ';' starts a comment line). This script only colours them:
each ink pixel gets an entry of the font's ramp (vertical gradient plus a
bevel lit from the top-left: lighter on edges facing up/left, darker on edges
facing down/right, docs/design/art-bible.md), then a 1 px shadow is baked in
one pixel down-right, in the darkest entry of the same ramp, so colour
variants are a plain 256-byte remap of one ramp (docs/design/hud.md Fonts).

Sheet layout: 16 cells per row, first cell = first glyph of the set, glyphs
in set order, letterform at the cell's top-left, everything else index 255.

Usage: python3 tools/fontsheet.py [-o DIR] [font ...]   (default: all fonts,
written to assets/src/fonts/<font>.png)
"""
import argparse
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import palette  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "src", "fonts")
COLS = 16
TRANSPARENT = 255


def _small_shade(x, y, n):
    # 5x7: flat light gray, a touch brighter on the upper half (top light).
    return 15 if y <= 2 else (14 if y <= 4 else 13)


def _menu_shade(x, y, n):
    # 9x12 gold: gradient gold_11 (top) .. gold_06 (bottom); 2 px strokes
    # split into a lit left/top half and a shaded right/bottom half.
    v = 11 - (y * 5) // 11
    v += (not n["up"]) + (not n["left"]) - (not n["down"]) - (not n["right"])
    return max(2, min(11, v))


def _big_shade(x, y, n):
    # 11x15 gold block numerals: gradient gold_10 .. gold_06, 3 px strokes
    # with a 1 px bevel: +2 on up/left edges, -2 on down/right edges.
    v = 10 - (y * 4) // 14
    v += 2 * ((not n["up"]) + (not n["left"]) - (not n["down"]) - (not n["right"]))
    return max(2, min(11, v))


# name: cell size, letterform size, glyph set, ramp, ramp shade function
FONTS = {
    "small": dict(cell=(6, 8), glyph=(5, 7), chars="".join(map(chr, range(32, 127))),
                  ramp="gray", shade=_small_shade),
    "menu": dict(cell=(10, 14), glyph=(9, 12), chars="".join(map(chr, range(32, 96))),
                 ramp="gold", shade=_menu_shade),
    "bigdigits": dict(cell=(12, 16), glyph=(11, 15), chars="0123456789%/-",
                      ramp="gold", shade=_big_shade),
}


class FontError(Exception):
    pass


def parse(path, gw, gh):
    """glyph data file -> {char: [row strings]} in file order."""
    glyphs = {}
    cur = None
    with open(path, encoding="utf-8") as f:
        for ln, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith(";"):
                continue
            if line.startswith("= "):
                ch = chr(int(line.split()[1], 16))
                if ch in glyphs:
                    raise FontError("%s:%d: glyph %r twice" % (path, ln, ch))
                cur = glyphs[ch] = []
                continue
            if cur is None or len(line) != gw or set(line) - set("#."):
                raise FontError("%s:%d: bad glyph row %r (want %d of '#.')" % (path, ln, line, gw))
            cur.append(line)
    for ch, rows in glyphs.items():
        if len(rows) != gh:
            raise FontError("%s: glyph %r has %d rows, want %d" % (path, ch, len(rows), gh))
    return glyphs


def ramp_indices(name):
    entries = palette.load_gpl()
    idx = [i for i, (_, n) in enumerate(entries) if n.startswith(name + "_")]
    if not idx:
        raise FontError("no palette ramp %r" % name)
    return idx


def render(font, data_path=None):
    """Font name -> indexed PIL image in the game palette."""
    spec = FONTS[font]
    cw, ch = spec["cell"]
    gw, gh = spec["glyph"]
    chars = spec["chars"]
    glyphs = parse(data_path or os.path.join(SRC, font + ".txt"), gw, gh)
    if list(glyphs) != list(chars):
        missing = [c for c in chars if c not in glyphs]
        extra = [c for c in glyphs if c not in chars]
        raise FontError("%s: glyph set mismatch (missing %r, extra %r, or wrong order)"
                        % (font, "".join(missing), "".join(extra)))
    ramp = ramp_indices(spec["ramp"])
    rows = (len(chars) + COLS - 1) // COLS
    w, h = COLS * cw, rows * ch
    px = bytearray([TRANSPARENT]) * (w * h)
    for i, c in enumerate(chars):
        ox, oy = (i % COLS) * cw, (i // COLS) * ch
        g = glyphs[c]

        def ink(x, y):
            return 0 <= x < gw and 0 <= y < gh and g[y][x] == "#"
        # shadow first, letterform on top
        for y in range(gh):
            for x in range(gw):
                if ink(x, y) and not ink(x + 1, y + 1):
                    px[(oy + y + 1) * w + ox + x + 1] = ramp[0]
        for y in range(gh):
            for x in range(gw):
                if ink(x, y):
                    n = dict(up=ink(x, y - 1), down=ink(x, y + 1),
                             left=ink(x - 1, y), right=ink(x + 1, y))
                    px[(oy + y) * w + ox + x] = ramp[spec["shade"](x, y, n)]
    img = Image.frombytes("P", (w, h), bytes(px))
    img.putpalette([c for rgb, _ in palette.load_gpl() for c in rgb])
    img.info["transparency"] = TRANSPARENT
    return img


def main(argv=None):
    ap = argparse.ArgumentParser(description="hand-drawn font data -> indexed PNG sheets")
    ap.add_argument("-o", "--out", default=SRC, help="output directory (default assets/src/fonts)")
    ap.add_argument("fonts", nargs="*", default=sorted(FONTS))
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    for f in args.fonts:
        if f not in FONTS:
            print("fontsheet: unknown font %r" % f, file=sys.stderr)
            return 1
        try:
            img = render(f)
        except (OSError, FontError, palette.PaletteError) as e:
            print("fontsheet: %s" % e, file=sys.stderr)
            return 1
        out = os.path.join(args.out, f + ".png")
        img.save(out, transparency=TRANSPARENT)
        print("fontsheet: %s -> %s (%dx%d, %d glyphs)"
              % (f, out, img.width, img.height, len(FONTS[f]["chars"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
