#!/usr/bin/env python3
"""ctest `fonts`: the three font sheets (docs/design/hud.md Fonts).

- The committed indexed PNGs in assets/src/fonts/ match what
  tools/fontsheet.py makes from the hand-drawn glyph data (no drift).
- Each PNG converts with tools/png2tex.py; the TEX loads through libbast with
  width = 16 cells, height = ceil(glyphs / 16) cells.
- Every glyph cell except space has ink; cells past the glyph count are empty;
  only the font's ramp and index 255 are used; the 1 px shadow is the ramp's
  darkest entry and sits bottom-right of the letterform (never top-left).

Plain asserts. Run: python3 tests/test_fonts.py
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bast  # noqa: E402
import fontsheet  # noqa: E402

from PIL import Image  # noqa: E402

EXPECT = {"small": (96, 48, 95), "menu": (160, 56, 64), "bigdigits": (192, 16, 13)}


def check(font, tex_path):
    spec = fontsheet.FONTS[font]
    cw, ch = spec["cell"]
    n = len(spec["chars"])
    t = bast.Tex.load(tex_path)
    rows = (n + 15) // 16
    assert (t.width, t.height) == (16 * cw, rows * ch), (font, t.width, t.height)
    assert (t.width, t.height, n) == EXPECT[font], font
    ramp = fontsheet.ramp_indices(spec["ramp"])
    allowed = set(ramp) | {fontsheet.TRANSPARENT}
    used = set(t.pixels)
    assert used <= allowed, (font, sorted(used - allowed))
    for i in range(rows * 16):
        ox, oy = (i % 16) * cw, (i // 16) * ch
        cell = [[t.get(ox + x, oy + y) for x in range(cw)] for y in range(ch)]
        ink = sum(v != fontsheet.TRANSPARENT for r in cell for v in r)
        if i >= n or spec["chars"][i] == " ":
            assert ink == 0, (font, i, "should be empty")
            continue
        c = spec["chars"][i]
        assert ink > 0, (font, c, "glyph is empty")
        shadow = [(x, y) for y in range(ch) for x in range(cw) if cell[y][x] == ramp[0]]
        assert shadow, (font, c, "no shadow")
        for x, y in shadow:
            # a shadow pixel is cast by the letterform pixel up-left of it
            assert x > 0 and y > 0, (font, c, x, y)
            src = cell[y - 1][x - 1]
            assert src not in (fontsheet.TRANSPARENT, ramp[0]), (font, c, x, y)
    print("fonts: %s ok (%dx%d, %d glyphs, %d indices)" % (font, t.width, t.height, n, len(used)))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        for font in sorted(fontsheet.FONTS):
            committed = os.path.join(fontsheet.SRC, font + ".png")
            a = Image.open(committed)
            assert a.mode == "P", committed
            b = fontsheet.render(font)
            assert a.size == b.size and a.tobytes() == b.tobytes(), \
                "%s differs from tools/fontsheet.py output; rerun it" % committed
            tex = os.path.join(tmp, font + ".tex")
            subprocess.run([sys.executable, os.path.join(ROOT, "tools", "png2tex.py"),
                            committed, tex], check=True, stdout=subprocess.DEVNULL)
            check(font, tex)
    print("fonts: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
