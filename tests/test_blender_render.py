#!/usr/bin/env python3
"""ctest `blender_render`: tools/blender_render.py, the 8-direction sprite pipeline.

Builds the asymmetric arrow fixture (tests/fixtures/make_arrow_blend.py) with
Blender, renders it, and checks against docs/design/tech.md and art-bible.md:
- exactly 16 TEX named arrow_bob_d<0..7>_f<0..1>.tex, origin bottom-centre;
- d0/d4 mirror-opposite (the +X wing's opaque-pixel centroid flips sides);
- d2 (camera 90 deg counter-clockwise from the front) sees the +X wing face-on,
  d6 does not; the head points left in d2, right in d6;
- f1 is the frame-2 pose (raised 4 px);
- raw renders have alpha 0 or 255 only; palcheck passes on TEX and previews;
- outline: every right/bottom silhouette pixel is a `<ramp>_00` entry.
The post-pass helpers are unit-tested without Blender first. Exit 77 (ctest
SKIP) with a message when `blender` is not installed.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import blender_render as br  # noqa: E402
import palette as gpl  # noqa: E402

W, H = 64, 64
SKIP = 77


def test_helpers():
    op = np.zeros((4, 5), dtype=bool)
    op[1:3, 1:4] = True
    m = br.outline_mask(op)
    want = np.zeros_like(op)
    want[1, 3] = want[2, 1] = want[2, 2] = want[2, 3] = True  # right column + bottom row
    assert (m == want).all(), m.astype(int)
    full = np.ones((2, 2), dtype=bool)  # canvas edge counts as outside
    assert br.outline_mask(full).tolist() == [[False, True], [True, True]]

    entries = gpl.load_gpl()
    names = [n for _, n in entries]
    dark = br.ramp_darkest_table(entries)
    assert dark[names.index("sand_09")] == names.index("sand_00")
    assert dark[names.index("red_00")] == names.index("red_00")
    assert dark[names.index("white")] == names.index("gray_00")
    assert dark[names.index("fb_red")] == names.index("fb_red")
    assert dark[255] == 255

    # Edge contrast: transparent neighbours never bleed in.
    rgb = np.full((3, 3, 3), 100.0)
    op = np.zeros((3, 3), dtype=bool)
    op[1, 1] = True
    assert (br.edge_contrast(rgb, op)[1, 1] == 100).all()

    # Partial alpha is refused.
    rgba = bytes([10, 10, 10, 128])
    try:
        br.postprocess(rgba, 1, 1, bast.load_palette(), dark)
        assert False, "partial alpha accepted"
    except ValueError:
        pass


def centroid_x(t):
    xs = [x for x in range(t.width) for y in range(t.height) if t.get(x, y) != 255]
    assert xs
    return sum(x + 0.5 for x in xs) / len(xs)


def bottom_row(t):
    return max(y for x in range(t.width) for y in range(t.height) if t.get(x, y) != 255)


def count_ramp(t, lo, hi):
    return sum(1 for x in range(t.width) for y in range(t.height) if lo <= t.get(x, y) <= hi)


def test_render(blender):
    entries = gpl.load_gpl()
    names = [n for _, n in entries]
    red_lo, red_hi = names.index("red_00"), names.index("red_15")
    with tempfile.TemporaryDirectory() as d:
        fx, out, prev, raw = (os.path.join(d, s) for s in ("fx", "out", "prev", "raw"))
        p = subprocess.run([blender, "-b", "--factory-startup", "--python",
                            os.path.join(ROOT, "tests", "fixtures", "make_arrow_blend.py"),
                            "--", fx], capture_output=True, text=True)
        assert p.returncode == 0 and os.path.exists(os.path.join(fx, "arrow.blend")), \
            p.stdout[-2000:] + p.stderr[-2000:]
        p = subprocess.run([blender, "-b", os.path.join(fx, "arrow.blend"), "--python",
                            os.path.join(TOOLS, "blender_render.py"), "--",
                            "--sprite", "arrow", "--canvas", "%dx%d" % (W, H), "--out", out,
                            "--preview", prev, "--raw", raw],
                           capture_output=True, text=True)
        assert p.returncode == 0, p.stdout[-3000:] + p.stderr[-3000:]
        line = [ln for ln in p.stdout.splitlines() if ln.startswith("blender_render:")]
        assert line, p.stdout[-3000:]
        print(line[-1])

        want = sorted("arrow_bob_d%d_f%d.tex" % (dd, f) for dd in range(8) for f in range(2))
        assert sorted(os.listdir(out)) == want, sorted(os.listdir(out))
        tex = {}
        for n in want:
            t = bast.Tex.load(os.path.join(out, n))
            assert (t.width, t.height) == (W, H)
            assert t.origin == (W // 2, H), (n, t.origin)
            assert t.flags & bast.TEX_HAS_TRANSPARENCY
            tex[n[len("arrow_bob_"):-4]] = t

        # No AA mush: raw renders are binary in alpha.
        for n in os.listdir(raw):
            a = np.asarray(Image.open(os.path.join(raw, n)).convert("RGBA"))[..., 3]
            bad = np.count_nonzero((a > 0) & (a < 255))
            assert bad == 0, (n, bad)

        c = W / 2
        cx = {k: centroid_x(t) - c for k, t in tex.items()}
        print("centroid offsets f0:", " ".join("d%d=%+.2f" % (dd, cx["d%d_f0" % dd])
                                               for dd in range(8)))
        # d0 faces the camera, the +X wing (the arrow's left) is on screen right;
        # d4 is the back view: same silhouette mirrored.
        assert cx["d0_f0"] > 1.0 and cx["d4_f0"] < -1.0, cx
        assert abs(cx["d0_f0"] + cx["d4_f0"]) < 0.5, cx
        # d2: camera 90 deg CCW from the front, at +X: head (-Y) left, tall tail
        # right, wing face-on. d6 is the mirror and the wing is hidden.
        assert cx["d2_f0"] > 1.0 and cx["d6_f0"] < -1.0, cx
        assert abs(cx["d2_f0"] + cx["d6_f0"]) < 0.5, cx
        red2, red6 = count_ramp(tex["d2_f0"], red_lo, red_hi), count_ramp(tex["d6_f0"], red_lo, red_hi)
        print("red wing pixels: d2=%d d6=%d" % (red2, red6))
        assert red2 >= 30 and red6 == 0, (red2, red6)
        # Head (gold) left in d2, right in d6: the outermost opaque column.
        gold = range(names.index("gold_00"), names.index("gold_11") + 1)
        for k, side in (("d2_f0", min), ("d6_f0", max)):
            t = tex[k]
            col = side(x for x in range(W) for y in range(H) if t.get(x, y) != 255)
            px = [t.get(col, y) for y in range(H) if t.get(col, y) != 255]
            assert all(i in gold for i in px), (k, col, [names[i] for i in px])
        # Frame order: f1 is the frame-2 pose, 4 px higher.
        for dd in range(8):
            b0, b1 = bottom_row(tex["d%d_f0" % dd]), bottom_row(tex["d%d_f1" % dd])
            assert b0 == H - 1 and b1 == H - 5, (dd, b0, b1)

        # Selective outline: right/bottom silhouette pixels use <ramp>_00.
        for k, t in tex.items():
            for x in range(W):
                for y in range(H):
                    i = t.get(x, y)
                    if i == 255:
                        continue
                    r = t.get(x + 1, y) if x + 1 < W else 255
                    b = t.get(x, y + 1) if y + 1 < H else 255
                    if r == 255 or b == 255:
                        assert names[i].endswith("_00") or names[i] == "black", (k, x, y, names[i])

        p = subprocess.run([sys.executable, os.path.join(TOOLS, "palcheck.py"), out, prev],
                           capture_output=True, text=True)
        print(p.stdout.strip())
        assert p.returncode == 0, p.stdout + p.stderr


def main():
    test_helpers()
    blender = shutil.which("blender")
    if not blender:
        print("blender_render: SKIPPED, `blender` not found on PATH")
        return SKIP
    test_render(blender)
    print("blender_render: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
