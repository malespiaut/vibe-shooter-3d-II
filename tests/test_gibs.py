#!/usr/bin/env python3
"""ctest `gibs_determinism` and `gibs_pixels`: tools/gibs.py (GAME.md 6.2).

  test_gibs.py determinism   same params twice (in-process and through the CLI)
                             give byte-identical TEX files; another seed differs.
  test_gibs.py pixels        frame 0 has exactly the source's opaque pixel count,
                             later frames never more; no opaque pixel on or below
                             the floor line in any frame; wound pixels are blood
                             ramp entries or the original index; no reserved index.

Inputs: the shipped prop_pot (assets/src/sprites/prop_pot.png + assets/src/gibs/
prop_pot.json) and a synthetic 20x20 blob on its own canvas. Plain asserts.
"""
import filecmp
import os
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import gibs  # noqa: E402

T = bast.TRANSPARENT
POT_PNG = os.path.join(ROOT, "assets", "src", "sprites", "prop_pot.png")
POT_JSON = os.path.join(ROOT, "assets", "src", "gibs", "prop_pot.json")


def pot_tex():
    img = Image.open(POT_PNG)
    img.load()
    return bast.Tex.from_image(img, origin=(img.size[0] // 2, img.size[1]))


def blob_tex():
    """20x20 canvas, a filled disc of steel/sand stripes resting on the floor."""
    w = h = 20
    t = bast.Tex(w, h, bytes([T]) * (w * h), (w // 2, h), bast.TEX_HAS_TRANSPARENCY)
    for x in range(w):
        for y in range(h):
            if (x - 9.5) ** 2 + (y - 10.5) ** 2 <= 81:
                t.pixels[x * h + y] = 26 if (x + y) % 5 else 58  # steel_08 / sand_08
    return t


def blob_params(**kw):
    p = dict(gibs.DEFAULTS)
    p.update(seed=7, chunks=6, frames=10, force=3.0)
    p.update(kw)
    return p


def opaque(t):
    return sum(1 for v in t.pixels if v != T)


def check_frames(src, frames, p, label):
    n0 = opaque(src)
    assert len(frames) == p["frames"], label
    assert opaque(frames[0]) == n0, (label, opaque(frames[0]), n0)
    floor = frames[0].origin[1]
    pal = bast.load_palette()
    fl = pal.flags
    for f, t in enumerate(frames):
        n = opaque(t)
        assert n <= n0, (label, f, n, n0)
        h = t.height
        for i, v in enumerate(t.pixels):
            if v == T:
                continue
            y = i % h
            assert y < floor, (label, f, "pixel at", i // h, y, "floor", floor)
            assert not fl[v] & bast.PAL_RESERVED, (label, f, v)
        assert t.flags & bast.TEX_HAS_TRANSPARENCY
    # wounds: every changed pixel of frame 0 is a blood ramp entry
    ramp = set(gibs.ramp_entries(p["blood_ramp"]))
    sw, sh = src.width, src.height
    offx = frames[0].origin[0] - src.origin[0]
    offy = frames[0].origin[1] - src.origin[1]
    changed = 0
    for x in range(sw):
        for y in range(sh):
            a = src.get(x, y)
            b = frames[0].get(x + offx, y + offy)
            assert (a == T) == (b == T), (label, x, y)
            if a != b:
                changed += 1
                assert b in ramp, (label, x, y, a, b)
    assert changed > 0, (label, "no wound pixels")
    # something actually moved by the last frame
    assert frames[-1].pixels != frames[0].pixels, label
    print("%s: opaque %d -> %s, %d wound px, floor %d"
          % (label, n0, [opaque(t) for t in frames], changed, floor))


def test_pixels():
    src = pot_tex()
    p = gibs.load_params(POT_JSON)
    check_frames(src, gibs.generate(src, p), p, "prop_pot")
    for seed in range(1, 6):
        p = blob_params(seed=seed)
        check_frames(blob_tex(), gibs.generate(blob_tex(), p), p, "blob seed %d" % seed)
    p = blob_params(blood_ramp="cyan", canvas=[40, 30], gravity=0.6, frames=16)
    check_frames(blob_tex(), gibs.generate(blob_tex(), p), p, "blob cyan 40x30")


def test_determinism():
    src = pot_tex()
    p = gibs.load_params(POT_JSON)
    a = gibs.generate(src, p)
    b = gibs.generate(pot_tex(), gibs.load_params(POT_JSON))
    assert [bytes(t.pixels) for t in a] == [bytes(t.pixels) for t in b]
    q = dict(p, seed=p["seed"] + 1)
    c = gibs.generate(src, q)
    assert [bytes(t.pixels) for t in a] != [bytes(t.pixels) for t in c], "seed ignored"

    with tempfile.TemporaryDirectory() as d:
        srcf = os.path.join(d, "prop_pot.tex")
        src.save(srcf)
        outs = []
        for run in ("r1", "r2"):
            out = os.path.join(d, run)
            r = subprocess.run([sys.executable, os.path.join(TOOLS, "gibs.py"), POT_JSON,
                                "-i", srcf, "-o", out], capture_output=True, text=True)
            assert r.returncode == 0, r.stderr
            print(r.stdout.strip())
            outs.append(out)
        names = sorted(os.listdir(outs[0]))
        assert names == ["prop_pot_gib_d0_f%d.tex" % i for i in range(8)], names
        _, mismatch, errors = filecmp.cmpfiles(outs[0], outs[1], names, shallow=False)
        assert not mismatch and not errors, (mismatch, errors)
        # the CLI output is what generate() returns
        for i, n in enumerate(names):
            t = bast.Tex.load(os.path.join(outs[0], n))
            assert bytes(t.pixels) == bytes(a[i].pixels), n
            assert t.origin == a[i].origin, (t.origin, a[i].origin)
    print("determinism: %d frames identical across runs" % len(a))


if __name__ == "__main__":
    {"pixels": test_pixels, "determinism": test_determinism}[sys.argv[1]]()
    print("ok")
