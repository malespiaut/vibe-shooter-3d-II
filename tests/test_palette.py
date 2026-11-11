#!/usr/bin/env python3
"""ctest `palcheck`: palette layout per art bible, palette build, palcheck fixtures.

Plain asserts, no framework. Run: python3 tests/test_palette.py
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
FIX = os.path.join(ROOT, "tests", "fixtures", "palcheck")
sys.path.insert(0, TOOLS)
import make_palette  # noqa: E402
import palette  # noqa: E402

# Art-bible table: ramp name, first index, count.
RAMPS = [("gray", 2, 16), ("steel", 18, 16), ("beige", 34, 16), ("sand", 50, 16),
         ("brown", 66, 16), ("gold", 82, 12), ("red", 94, 16), ("green", 110, 12),
         ("blue", 122, 12), ("violet", 134, 12), ("orange", 146, 12), ("cyan", 158, 12),
         ("olive", 170, 12), ("fur", 182, 12), ("pink", 194, 10), ("indigo", 204, 12)]
FB = ["fb_sun", "fb_yellow", "fb_gold", "fb_orange", "fb_fire", "fb_red", "fb_red_dim",
      "fb_cyan", "fb_cyan_dim", "fb_green", "fb_blue", "fb_violet", "fb_white",
      "fb_amber", "fb_pink"]
FB_HEX = ["FFFFF0", "FFF040", "FFD020", "FF9020", "FF5010", "FF2020", "B01010",
          "40FFF0", "10B0B0", "40FF40", "4080FF", "C060FF", "F0F0FF", "FFB040", "FF60A0"]


def lum(rgb):
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


def run(*args):
    p = subprocess.run([sys.executable] + list(args), capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def test_layout(e):
    names = [n for _, n in e]
    assert e[0] == ((0, 0, 0), "black")
    assert e[1] == ((255, 255, 255), "white")
    assert e[255] == ((255, 0, 255), "transparent")
    for ramp, first, count in RAMPS:
        for i in range(count):
            assert names[first + i] == "%s_%02d" % (ramp, i), (first + i, names[first + i])
        lums = [lum(e[first + i][0]) for i in range(count)]
        assert lums == sorted(lums) and len(set(lums)) == count, "%s not dark->light" % ramp
    assert sum(c for _, _, c in RAMPS) == 214
    for i in range(21):
        assert e[216 + i] == ((0, 0, 0), "reserved_%02d" % i)
    for i, rgb in enumerate([(2, 2, 1), (4, 3, 2), (7, 5, 3)]):
        assert e[237 + i] == (rgb, "shadow_%02d" % i)
    for i, (n, h) in enumerate(zip(FB, FB_HEX)):
        assert names[240 + i] == n
        assert "%02X%02X%02X" % e[240 + i][0] == h, n
    named = [rgb for rgb, n in e if not n.startswith("reserved_")]
    assert len(named) == len(set(named)), "duplicate colours"


def test_reproducible(e):
    assert make_palette.build() == e, "catlantis.gpl differs from make_palette.py output"


def test_build():
    with tempfile.TemporaryDirectory() as d:
        rc, out = run(os.path.join(TOOLS, "palette.py"), "--out-dir", d)
        assert rc == 0, out
        pal = open(os.path.join(d, "palette.bin"), "rb").read()
        fl = open(os.path.join(d, "palette_flags.bin"), "rb").read()
    assert len(pal) == 768 and len(fl) == 256
    assert pal[:6] == bytes([0, 0, 0, 255, 255, 255]) and pal[765:] == bytes([255, 0, 255])
    for i in range(256):
        want = (1 if 216 <= i <= 236 else 0) | (2 if 240 <= i <= 254 else 0) | (4 if i == 255 else 0)
        assert fl[i] == want, (i, fl[i])


def test_palcheck():
    pc = os.path.join(TOOLS, "palcheck.py")
    rc, out = run(pc, os.path.join(FIX, "good.png"))
    assert rc == 0 and "palcheck: OK" in out, out
    for name, needle in [("bad_rgb.png", "mode RGB"),
                         ("bad_color.png", "bad_color.png:6,2: index 40"),
                         ("bad_reserved.png", "bad_reserved.png:5,3: index 216")]:
        rc, out = run(pc, os.path.join(FIX, name))
        assert rc == 1 and needle in out, (name, rc, out)
    rc, out = run(pc, FIX)  # directory scan: 3 bad files, 3 problems
    assert rc == 1 and "3 problem(s) in 4 file(s)" in out, out


def main():
    e = palette.load_gpl()
    test_layout(e)
    test_reproducible(e)
    test_build()
    test_palcheck()
    print("test_palette: OK")


if __name__ == "__main__":
    main()
