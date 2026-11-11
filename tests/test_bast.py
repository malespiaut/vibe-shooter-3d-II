#!/usr/bin/env python3
"""ctest `bast_py`: the Python ctypes wrapper over libbast.so, the TEX CLIs and
palcheck on .tex. Plain asserts. Run after `cmake --build build`:
python3 tests/test_bast.py  (BAST_LIB / BAST_PALETTE override the defaults)
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
FIX = os.path.join(ROOT, "tests", "fixtures", "palcheck")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402


def run(*args):
    p = subprocess.run([sys.executable] + list(args), capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def indices(path):
    img = Image.open(path)
    assert img.mode == "P", (path, img.mode)
    return np.asarray(img)


def test_nearest():
    assert bast.nearest((255, 255, 255)) == 1
    assert bast.nearest((0, 0, 0)) == 0
    assert bast.nearest((255, 0, 255)) != 255
    assert bast.nearest((255, 0, 255), transparent=True) == 255
    pal = bast.load_palette()
    assert pal is bast.load_palette()  # cached default
    assert pal.colors[255] == (255, 0, 255) and pal.flags[255] == bast.PAL_TRANSPARENT
    for i, (rgb, f) in enumerate(zip(pal.colors, pal.flags)):
        if not f & bast.PAL_RESERVED:
            assert pal.nearest(rgb, fullbright=True, transparent=True) == i, i


def test_png_tex_png(d):
    """Indexed fixture -> TEX -> PNG is pixel-identical (via the CLIs)."""
    src = os.path.join(FIX, "good.png")  # ramps, fullbright 240 and transparent 255
    tex, back = os.path.join(d, "good.tex"), os.path.join(d, "good_back.png")
    rc, out = run(os.path.join(TOOLS, "png2tex.py"), src, tex, "--origin", "8", "7")
    assert rc == 0, out
    rc, out = run(os.path.join(TOOLS, "tex2png.py"), tex, back)
    assert rc == 0 and "origin 8,7, flags 1" in out, out
    a, b = indices(src), indices(back)
    assert a.shape == b.shape and (a == b).all()
    img = Image.open(back)
    assert img.info.get("transparency") == 255
    pal = bast.load_palette()
    assert img.getpalette()[:768] == [c for rgb in pal.colors for c in rgb]
    # File really is column-major: byte 16 + x*h + y.
    raw = open(tex, "rb").read()
    h, w = a.shape
    assert raw[:4] == b"TEX1" and len(raw) == 16 + w * h
    for x in range(w):
        for y in range(h):
            assert raw[16 + x * h + y] == a[y, x]


def test_asymmetric_api(d):
    """Wrapper API round trip on a non-square, non-symmetric indexed image."""
    pal = bast.load_palette()
    px = (np.arange(5 * 3).reshape(3, 5) * 7 + 2) % 200  # usable ramp indices
    px[2, 4] = 255
    img = Image.fromarray(px.astype(np.uint8), mode="P")
    img.putpalette([c for rgb in pal.colors for c in rgb])
    t = bast.Tex.from_image(img, origin=(-1, 3), fullbright=True)
    assert (t.width, t.height, t.origin) == (5, 3, (-1, 3))
    assert t.flags == bast.TEX_HAS_TRANSPARENCY | bast.TEX_FULLBRIGHT
    assert t.get(4, 2) == 255 and t.get(1, 0) == px[0, 1]
    p = os.path.join(d, "asym.tex")
    t.save(p)
    u = bast.Tex.load(p)
    assert (u.width, u.height, u.origin, u.flags) == (5, 3, (-1, 3), t.flags)
    assert u.pixels == t.pixels
    assert (np.asarray(u.to_image()) == px).all()


def test_rgba(d):
    """Non-indexed images go through bast_tex_from_rgba."""
    pal = bast.load_palette()
    img = Image.new("RGBA", (2, 1))
    img.putdata([(250, 250, 250, 255), (10, 200, 10, 100)])
    t = bast.Tex.from_image(img)
    assert t.get(0, 0) == bast.nearest((250, 250, 250)) == 1
    assert t.get(1, 0) == 255 and t.flags == bast.TEX_HAS_TRANSPARENCY
    # A P image with a colour that differs from the game palette is matched, not copied.
    bad = Image.open(os.path.join(FIX, "bad_color.png"))
    t = bast.Tex.from_image(bad)
    assert all(not pal.flags[i] & bast.PAL_RESERVED for i in t.pixels)
    try:
        bast.Tex.load(os.path.join(d, "missing.tex"))
        assert False, "expected BastError"
    except bast.BastError as e:
        assert "missing.tex" in str(e)


def test_palcheck_tex(d):
    pc = os.path.join(TOOLS, "palcheck.py")
    good = os.path.join(d, "good.tex")
    rc, out = run(pc, good)
    assert rc == 0 and "palcheck: OK, 1 file(s)" in out, out
    t = bast.Tex.load(good)
    t.pixels[5 * t.height + 3] = 216  # (5,3) reserved
    bad = os.path.join(d, "bad_reserved.tex")
    t.save(bad)
    rc, out = run(pc, bad)
    assert rc == 1 and "bad_reserved.tex:5,3: index 216 (reserved_00) is reserved (1 px)" in out, out
    junk = os.path.join(d, "junk.tex")
    open(junk, "wb").write(b"TEX1junk")
    rc, out = run(pc, junk)
    assert rc == 1 and "cannot read TEX" in out, out


def main():
    test_nearest()
    with tempfile.TemporaryDirectory() as d:
        test_png_tex_png(d)
        test_asymmetric_api(d)
        test_rgba(d)
        test_palcheck_tex(d)
    print("test_bast: OK")


if __name__ == "__main__":
    main()
