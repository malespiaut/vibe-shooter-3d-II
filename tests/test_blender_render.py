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
- ramps: every opaque pixel's index lies in its material's ramp (material
  pass from --raw); outline (right/bottom silhouette) pixels are exactly that
  ramp's `_00`; no other pixel is `black`, `shadow_*` or a `_00`, in all 8
  directions and both frames;
- exposure: each material's brightest f0 pixel reaches entry >= 09 of its
  ramp.
The post-pass helpers are unit-tested without Blender first. Exit 77 (ctest
SKIP) with a message when `blender` is not installed.
"""
import json
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
    sand = br.ramp_entries(entries, "sand")
    assert [names[i] for i in sand] == ["sand_%02d" % k for k in range(16)]
    assert br.ramp_entries(entries, "gold")[0] == names.index("gold_00")
    assert br.ramp_entries(entries, "fb_red") == [names.index("fb_red")]
    for bad in ("shadow", "reserved", "nope", "black"):
        try:
            br.ramp_entries(entries, bad)
            assert False, "ramp %r accepted" % bad
        except ValueError:
            pass
    # Colour budget: 3 ramps plus fb_* pass, a 4th ramp is refused.
    ok = br.resolve_ramps(entries, {"a": "sand", "b": "gold", "c": "red", "d": "sand",
                                    "eye": "fb_red", "gem": "fb_cyan"})
    assert ok["d"] == sand
    for bad in ({"a": "sand", "b": "gold", "c": "red", "d": "steel"}, {}, None):
        try:
            br.resolve_ramps(entries, bad)
            assert False, "ramps %r accepted" % (bad,)
        except ValueError:
            pass

    pal = bast.load_palette()
    quant = br.RampQuantizer(pal)
    red = br.ramp_entries(entries, "red")
    # A neutral gray on material 1 (sand) stays in sand, never steel/gray.
    # 3x3 opaque block at (1..3, 1..3) of a 5x5 canvas.
    w = h = 5
    img = np.zeros((h, w, 4), dtype=np.uint8)
    img[1:4, 1:4] = (90, 90, 90, 255)
    img[1, 1] = (10, 10, 10, 255)            # near black, interior? no: top-left
    mat = np.zeros((h, w), dtype=int)
    mat[1:4, 1:4] = 1
    mat[1, 3] = 2                            # right edge pixel on red
    t = tex_from_bytes(br.postprocess(img.tobytes(), mat, w, h, {1: sand, 2: red}, quant))
    assert t.origin == (w // 2, h) and t.flags & bast.TEX_HAS_TRANSPARENCY
    for y in range(h):
        for x in range(w):
            i = t.get(x, y)
            if mat[y, x] == 0:
                assert i == 255, (x, y, i)
            elif (x, y) in ((3, 1), (3, 2), (3, 3), (1, 3), (2, 3)):  # outline
                want = red[0] if mat[y, x] == 2 else sand[0]
                assert i == want, (x, y, names[i])
            else:  # interior: own ramp, never _00 (even the near-black pixel)
                assert i in sand[1:], (x, y, names[i])
    # No ramp for a material: error. Partial alpha: error.
    for m, a in ((3, 255), (1, 128)):
        bad = img.copy()
        bad[2, 2, 3] = a
        mm = mat.copy()
        mm[2, 2] = m
        try:
            br.postprocess(bad.tobytes(), mm, w, h, {1: sand, 2: red}, quant)
            assert False, "accepted material %d alpha %d" % (m, a)
        except ValueError:
            pass

    # Edge contrast: transparent neighbours never bleed in.
    rgb = np.full((3, 3, 3), 100.0)
    op = np.zeros((3, 3), dtype=bool)
    op[1, 1] = True
    assert (br.edge_contrast(rgb, op)[1, 1] == 100).all()


def tex_from_bytes(data):
    with tempfile.NamedTemporaryFile(suffix=".tex", delete=False) as f:
        f.write(data)
    try:
        return bast.Tex.load(f.name)
    finally:
        os.unlink(f.name)


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
        for n in sorted(os.listdir(raw)):
            if not n.endswith(".png") or n.endswith("_mat.png"):
                continue
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

        # Ramps: material pass -> each pixel on its own ramp; outline = _00;
        # nothing else dark-ends (black, shadow_*, _00); exposure >= 09.
        with open(os.path.join(raw, "materials.json")) as f:
            mat_name = {int(k): v for k, v in json.load(f).items()}
        with open(os.path.join(fx, "sprite.json")) as f:
            ramp_cfg = json.load(f)["ramps"]
        assert sorted(mat_name.values()) == sorted(ramp_cfg), (mat_name, ramp_cfg)
        ramp_of = {k: br.ramp_entries(entries, ramp_cfg[m]) for k, m in mat_name.items()}
        top = {m: 0 for m in ramp_cfg}
        bad = unlit = 0
        quant = br.RampQuantizer(bast.load_palette())
        for k, t in tex.items():
            mat = np.asarray(Image.open(os.path.join(raw, "arrow_bob_%s_mat.png" % k)))
            rgb = np.asarray(Image.open(os.path.join(raw, "arrow_bob_%s.png" % k)).convert("RGB"))
            for x in range(W):
                for y in range(H):
                    i = t.get(x, y)
                    if i == 255:
                        assert mat[y, x] == 0, (k, x, y, int(mat[y, x]))
                        continue
                    ramp = ramp_of[int(mat[y, x])]
                    assert i in ramp, (k, x, y, mat_name[int(mat[y, x])], names[i])
                    r = t.get(x + 1, y) if x + 1 < W else 255
                    b = t.get(x, y + 1) if y + 1 < H else 255
                    if r == 255 or b == 255:
                        assert i == ramp[0], (k, x, y, names[i])
                    else:
                        if i == ramp[0] or names[i] == "black" or names[i].startswith("shadow"):
                            bad += 1
                        # The light itself, before _00 is reserved for the
                        # outline: an unlit face would land on _00 here.
                        if quant.nearest(tuple(int(v) for v in rgb[y, x]), ramp) == ramp[0]:
                            unlit += 1
                    if k.endswith("f0"):
                        m = mat_name[int(mat[y, x])]
                        top[m] = max(top[m], ramp.index(i))
        print("brightest ramp entry f0: " + " ".join(
            "%s_%02d" % (ramp_cfg[m], top[m]) for m in sorted(top)))
        assert bad == 0, "%d interior pixel(s) are black/shadow/_00" % bad
        assert unlit == 0, "%d interior pixel(s) lit only to _00 (black holes)" % unlit
        for m, v in top.items():
            assert v >= 9, (m, v)

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
