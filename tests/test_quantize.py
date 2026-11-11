#!/usr/bin/env python3
"""ctest `quantize`: tools/quantize.py (RGBA -> TEX through libbast).

A 4x3 RGBA image with exact palette colours, one near-miss colour and one
alpha < 128 pixel must round-trip to the expected indices (255 for the
transparent one), with the origin and flags fields as asked. The TEX header
is checked byte by byte against docs/design/tech.md, independently of libbast.
Plain asserts. Run after `cmake --build build`.
"""
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import quantize  # noqa: E402

W, H = 4, 3
NEAR_MISS = (105, 92, 80)   # 2-3 steps off entry 40 (103, 94, 79)
NEAR_MISS_INDEX = 40
# Row-major expected indices; None marks the near-miss, "T" the transparent pixel.
LAYOUT = [
    [0, 1, 40, 80],
    [120, 160, None, 41],
    [200, "T", 10, 100],
]


def build_rgba(pal):
    rgba = bytearray()
    want = []
    for row in LAYOUT:
        for v in row:
            if v is None:
                rgba += bytes(NEAR_MISS) + b"\xff"
                want.append(NEAR_MISS_INDEX)
            elif v == "T":
                rgba += bytes((12, 200, 30, 100))  # alpha 100 < 128
                want.append(255)
            else:
                assert pal.flags[v] == 0, v  # plain shaded entry
                rgba += bytes(pal.colors[v]) + b"\xff"
                want.append(v)
    return bytes(rgba), want


def parse_tex(data):
    assert data[:4] == b"TEX1", data[:4]
    w, h, ox, oy, flags = struct.unpack_from("<HHhhB", data, 4)
    assert data[13:16] == b"\0\0\0"
    px = data[16:]
    assert len(px) == w * h
    rows = [px[x * h + y] for y in range(h) for x in range(w)]  # column-major -> row-major
    return w, h, (ox, oy), flags, rows


def test_quantize_rgba():
    pal = bast.load_palette()
    rgba, want = build_rgba(pal)
    data = quantize.quantize_rgba(rgba, W, H, origin=(2, 3), fullbright=False)
    w, h, origin, flags, rows = parse_tex(data)
    assert (w, h) == (W, H)
    assert origin == (2, 3), origin
    assert flags == bast.TEX_HAS_TRANSPARENCY, flags
    assert rows == want, (rows, want)

    data = quantize.quantize_rgba(rgba, W, H, origin="bottom-centre", fullbright=True)
    w, h, origin, flags, rows = parse_tex(data)
    assert origin == (W // 2, H), origin
    assert flags == bast.TEX_HAS_TRANSPARENCY | bast.TEX_FULLBRIGHT, flags
    assert rows == want

    # No transparent pixel -> no has_transparency flag; negative origin survives.
    opaque = bytes(pal.colors[7]) + b"\xff"
    _, _, origin, flags, rows = parse_tex(quantize.quantize_rgba(opaque, 1, 1, (-5, 7)))
    assert flags == 0 and rows == [7] and origin == (-5, 7)


def test_cli():
    pal = bast.load_palette()
    rgba, want = build_rgba(pal)
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "thing.png")
        Image.frombytes("RGBA", (W, H), rgba).save(src)
        out = os.path.join(d, "thing.tex")
        prev = os.path.join(d, "prev.png")
        p = subprocess.run([sys.executable, os.path.join(TOOLS, "quantize.py"), src,
                            "-o", out, "--origin", "bottom-centre", "--fullbright",
                            "--preview", prev], capture_output=True, text=True)
        assert p.returncode == 0, p.stdout + p.stderr
        with open(out, "rb") as f:
            w, h, origin, flags, rows = parse_tex(f.read())
        assert rows == want and origin == (2, 3) and flags == 3
        img = Image.open(prev)
        assert img.mode == "P" and img.size == (W, H)
        assert list(img.getdata()) == want
        raw = img.getpalette()
        assert tuple(raw[40 * 3:41 * 3]) == pal.colors[40]

        # Several inputs -> output directory, one <name>.tex each.
        src2 = os.path.join(d, "other.png")
        Image.frombytes("RGBA", (W, H), rgba).save(src2)
        outdir = os.path.join(d, "out")
        p = subprocess.run([sys.executable, os.path.join(TOOLS, "quantize.py"), src, src2,
                            "-o", outdir, "--origin", "1,2", "-q"],
                           capture_output=True, text=True)
        assert p.returncode == 0, p.stdout + p.stderr
        assert sorted(os.listdir(outdir)) == ["other.tex", "thing.tex"]
        t = bast.Tex.load(os.path.join(outdir, "other.tex"))
        assert t.origin == (1, 2)

        # Bad origin and missing input fail cleanly.
        p = subprocess.run([sys.executable, os.path.join(TOOLS, "quantize.py"), src,
                            "-o", out, "--origin", "nope"], capture_output=True, text=True)
        assert p.returncode != 0
        p = subprocess.run([sys.executable, os.path.join(TOOLS, "quantize.py"),
                            os.path.join(d, "missing.png"), "-o", out],
                           capture_output=True, text=True)
        assert p.returncode == 1 and "quantize:" in p.stderr


if __name__ == "__main__":
    test_quantize_rgba()
    test_cli()
    print("quantize: ok")
