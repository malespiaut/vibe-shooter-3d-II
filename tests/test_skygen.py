#!/usr/bin/env python3
"""tools/skygen.py: the four level skies (issue #155, art-bible.md Sizes, Sky).

- Every sky in assets/src/sky paints at 320x104, round-trips through
  tools/quantize.py unchanged, and uses no fullbright, reserved, shadow or
  transparent entry.
- Seamless horizontally: painting with every x shifted by 160 gives exactly
  the unshifted sky rotated by 160 columns, so the 319 -> 0 join is painted like
  any interior column pair. Also the number of rows that differ across that
  join is no larger than across the busiest interior column pair.
"""
import glob
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import hud  # noqa: E402
import palette  # noqa: E402
import skygen  # noqa: E402

W, H = skygen.W, skygen.H


def col(img, x):
    return [img.px[y * W + x] for y in range(H)]


def diff(a, b):
    return sum(1 for p, q in zip(a, b) if p != q)


def main():
    pal = hud.Pal()
    specs = sorted(glob.glob(os.path.join(ROOT, "assets", "src", "sky", "*.json")))
    names = [os.path.splitext(os.path.basename(p))[0] for p in specs]
    assert names == ["sky_dawn", "sky_dusk", "sky_midday", "sky_sunset"], names
    for path, name in zip(specs, names):
        with open(path) as f:
            spec = json.load(f)
        img = skygen.paint(spec, pal)
        assert (img.w, img.h) == (320, 104), (name, img.w, img.h)
        t = skygen.to_tex(img, pal)  # raises on any quantize mismatch
        assert (t.width, t.height) == (320, 104)
        for i in set(img.px):
            n = pal.entries[i][1]
            fl = pal.flags[i]
            assert not fl & (palette.FLAG_RESERVED | palette.FLAG_FULLBRIGHT), (name, n)
            assert i != 255 and not n.startswith("shadow_"), (name, n)

        shifted = skygen.paint(spec, pal, shift=160)
        for y in range(H):
            row = img.px[y * W:(y + 1) * W]
            rolled = row[-160:] + row[:-160]
            assert shifted.px[y * W:(y + 1) * W] == rolled, (name, "row", y)

        # Shifting proves the painter treats the join like any other column
        # pair, but an element whose left end sits at x = 0 could still draw
        # a line there (a full-width wall's lit edge did). Fail on a vertical
        # run of 4+ rows where column 0 differs from both its neighbours,
        # which match each other. Checker dither rows come in pairs, so they
        # never make such a run.
        a, b, c = col(img, W - 1), col(img, 0), col(img, 1)
        run = 0
        for y in range(H):
            run = run + 1 if (b[y] != a[y] and a[y] == c[y]) else 0
            assert run < 4, (name, "vertical line at the 319|0 join ending row", y)
        seam = diff(col(img, W - 1), col(img, 0))
        inner = max(diff(col(img, x), col(img, x + 1)) for x in range(W - 1))
        assert seam <= inner, (name, seam, inner)
        print("%s: %d entries, seam 319|0 differs in %d rows (busiest interior pair %d)"
              % (name, len(set(img.px)), seam, inner))
    print("test_skygen: ok")


if __name__ == "__main__":
    main()
