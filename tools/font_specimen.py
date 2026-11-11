#!/usr/bin/env python3
"""Draw a 320x240 font specimen from the font TEX sheets (preview tool).

Loads assets/gen/fonts/{menu,small,bigdigits}.tex through libbast and blits
text on a sandstone background with fixed cell advance, the way the game
will: the Menu pangram, the full Small ASCII set and Big-digit numbers.

Usage: python3 tools/font_specimen.py [--fonts DIR] out.png
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import fontsheet  # noqa: E402
import palette  # noqa: E402

W, H = 320, 240


class Canvas:
    def __init__(self, bg):
        self.px = bytearray([bg]) * (W * H)

    def text(self, font, sheet, s, x0, y0):
        spec = fontsheet.FONTS[font]
        cw, ch = spec["cell"]
        x = x0
        for c in s:
            i = spec["chars"].find(c)
            if i >= 0:
                sx, sy = (i % fontsheet.COLS) * cw, (i // fontsheet.COLS) * ch
                for yy in range(ch):
                    for xx in range(cw):
                        v = sheet.get(sx + xx, sy + yy)
                        if v != fontsheet.TRANSPARENT and 0 <= x + xx < W and 0 <= y0 + yy < H:
                            self.px[(y0 + yy) * W + x + xx] = v
            x += cw
        return x

    def image(self):
        t = bast.Tex(W, H)
        for y in range(H):
            t.pixels[y::H] = self.px[y * W:(y + 1) * W]
        return t.to_image()


def main(argv=None):
    ap = argparse.ArgumentParser(description="font specimen")
    ap.add_argument("--fonts", default=os.path.join(fontsheet.ROOT, "assets", "gen", "fonts"))
    ap.add_argument("out")
    args = ap.parse_args(argv)
    sheets = {f: bast.Tex.load(os.path.join(args.fonts, f + ".tex")) for f in fontsheet.FONTS}
    names = [n for _, n in palette.load_gpl()]
    cv = Canvas(names.index("sand_09"))
    m = "menu"
    y = 6
    for line in ("THE QUICK BROWN CAT", "JUMPS OVER THE LAZY", "DOG 0123456789"):
        cv.text(m, sheets[m], line, 8, y)
        y += 16
    cv.text(m, sheets[m], "!\"#$%&'()*+,-./:;<=>?@[\\]^_", 8, y)
    y += 20
    chars = fontsheet.FONTS["small"]["chars"]
    for k in range(0, len(chars), 48):
        cv.text("small", sheets["small"], chars[k:k + 48], 8, y)
        y += 10
    y += 2
    cv.text("small", sheets["small"], "HEALTH  ARMOR  AMMO  Kills 12/40  Secrets 3/5", 8, y)
    y += 14
    x = cv.text("bigdigits", sheets["bigdigits"], "100%", 8, y)
    x = cv.text("bigdigits", sheets["bigdigits"], "50/200", x + 12, y)
    cv.text("bigdigits", sheets["bigdigits"], "-", x + 12, y)
    y += 22
    cv.text("bigdigits", sheets["bigdigits"], "0123456789%/-", 8, y)
    cv.image().save(args.out, transparency=fontsheet.TRANSPARENT)
    print("font_specimen: %s (%dx%d)" % (args.out, W, H))
    return 0


if __name__ == "__main__":
    sys.exit(main())
