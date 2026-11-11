#!/usr/bin/env python3
"""ctest `texgen_tile`: tools/texgen.py (GAME.md 6.1) on every
assets/src/textures/*.json.

1. Determinism: generating twice gives byte-identical TEX files.
2. Palette: every texel is a non-reserved entry; fullbright entries appear
   only where the parameter file's brightmap puts them.
3. Seams. Metric: texel luminance Y = 0.299 R + 0.587 G + 0.114 B of the
   palette colour. For a column pair (a, b), D(a, b) = mean over the rows of
   |Y(a, y) - Y(b, y)|. The seam pair is (63, 0); the interior pairs are
   (x, x + 1) for x in 0..62. The texture passes when D(seam) <= max of the
   interior D, i.e. the step across the tile edge is no larger than a step
   that already happens inside the texture. Walls must tile horizontally and
   floors in both axes; texgen wraps every layer in both axes, so every
   texture is checked across both seams (rows instead of columns for y).
   Negative control: a texture that does not tile (a left-to-right ramp)
   must fail the same metric, so the check can catch a seam.
4. Second base colour: every texture whose params define `base2` has at
   least one texel from the base2 ramp (`<ramp>_<lo>`..`<ramp>_<hi>`), so a
   threshold that silently disables it fails here. Negative control: the
   same params with an unreachable threshold must fail the check.
Plain asserts. Run after `cmake --build build`.
"""
import glob
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import palette as gpl  # noqa: E402
import texgen  # noqa: E402

SOURCES = sorted(glob.glob(os.path.join(ROOT, "assets", "src", "textures", "*.json")))


def seam_ok(Y):
    """Y: HxW luminance. (seam D, max interior D, ok) across the x seam."""
    d = np.abs(np.diff(Y, axis=1)).mean(axis=0)
    seam = np.abs(Y[:, -1] - Y[:, 0]).mean()
    return seam, d.max(), seam <= d.max()


def base2_count(idx, params, names):
    """Texels of `idx` that come from the params' base2 ramp."""
    b2 = params["base2"]
    want = {"%s_%02d" % (b2["ramp"], n) for n in range(int(b2["lo"]), int(b2["hi"]) + 1)}
    ramp = [i for i, nm in enumerate(names) if nm in want]
    return int(np.isin(idx, ramp).sum())


def run_texgen(outdir):
    r = subprocess.run([sys.executable, os.path.join(TOOLS, "texgen.py"), "-q"]
                       + SOURCES + ["-o", outdir], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def main():
    assert SOURCES, "no assets/src/textures/*.json"
    pal = bast.load_palette()
    colors = np.array(pal.colors, dtype=float)
    flags = pal.flags
    Ylut = colors @ np.array([0.299, 0.587, 0.114])
    names = [nm for _, nm in gpl.load_gpl()]

    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        run_texgen(a)
        run_texgen(b)
        for src in SOURCES:
            stem = os.path.splitext(os.path.basename(src))[0]
            fa, fb = os.path.join(a, stem + ".tex"), os.path.join(b, stem + ".tex")
            with open(fa, "rb") as f1, open(fb, "rb") as f2:
                assert f1.read() == f2.read(), "%s: not deterministic" % stem
            print("deterministic: %s" % stem)

            params = json.load(open(src))
            t = bast.Tex.load(fa)
            assert (t.width, t.height) == (64, 64), (stem, t.width, t.height)
            idx = np.frombuffer(bytes(t.pixels), np.uint8).reshape(t.width, t.height).T
            used = set(np.unique(idx).tolist())
            for i in used:
                assert not flags[i] & bast.PAL_RESERVED, (stem, i)
                assert i != bast.TRANSPARENT, (stem, i)
            fbr = {i for i in used if flags[i] & bast.PAL_FULLBRIGHT}
            assert len(fbr) <= len(params.get("brightmap", [])), (stem, fbr)

            if "base2" in params:
                n2 = base2_count(idx, params, names)
                print("base2 %s: %d %s texels (%.1f%%)"
                      % (stem, n2, params["base2"]["ramp"], 100.0 * n2 / idx.size))
                assert n2 > 0, "%s: base2 %s never used" % (stem, params["base2"]["ramp"])

            Y = Ylut[idx]
            seams = [("x", Y), ("y", Y.T)]
            for axis, img in seams:
                s, m, ok = seam_ok(img)
                print("tile %s %s: seam D %.2f, max interior D %.2f -> %s"
                      % (stem, axis, s, m, "ok" if ok else "SEAM"))
                assert ok, "%s: visible %s seam" % (stem, axis)

    # Negative control: a horizontal ramp has a big step at the tile edge.
    ramp = np.tile(np.linspace(0, 200, 64), (64, 1))
    s, m, ok = seam_ok(ramp)
    print("control ramp: seam D %.2f, max interior D %.2f -> %s" % (s, m, "ok" if ok else "SEAM"))
    assert not ok, "seam metric failed to detect a non-tiling texture"
    # Negative control: an unreachable threshold disables base2 and the
    # check above must see zero texels.
    tp = texgen.Pal(pal)
    for src in SOURCES:
        params = json.load(open(src))
        if "base2" in params:
            params["base2"]["threshold"] = 2.0
            for m in params["materials"].values():
                if m.get("base2", True) is not False:
                    m["base2"] = 2.0   # per-region overrides too
            idx, _ = texgen.generate(params, tp)
            assert base2_count(idx, params, names) == 0, src
            print("control base2 off %s: 0 texels -> caught"
                  % os.path.splitext(os.path.basename(src))[0])
    print("texgen_tile: OK (%d textures)" % len(SOURCES))


if __name__ == "__main__":
    main()
