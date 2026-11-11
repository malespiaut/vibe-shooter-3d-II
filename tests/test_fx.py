#!/usr/bin/env python3
"""Tests for tools/fx.py (GAME.md 6.3 effect generator).

  test_fx.py loop         every loop effect: frame n (one past the last)
                          equals frame 0 bit for bit, and the seam step
                          n-1 -> 0 is no larger than the largest in-loop step.
  test_fx.py determinism  two separate runs of the CLI on every effect give
                          byte-identical TEX files; a different seed gives a
                          different effect; the output passes palcheck and
                          follows the naming contract.
"""
import filecmp
import glob
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bast  # noqa: E402
import fx  # noqa: E402

PARAMS = sorted(glob.glob(os.path.join(ROOT, "assets", "src", "fx", "*.json")))


def changed(a, b):
    return int(np.count_nonzero(a != b))


def test_loop():
    loops = [p for p in map(fx.load_params, PARAMS) if p["mode"] == "loop"]
    assert len(loops) >= 4, "expected the loop effects of #22"
    for p in loops:
        n = p["frames"]
        frames = [fx.render_frame(p, f) for f in range(n + 1)]
        assert np.array_equal(frames[n], frames[0]), "%s: frame %d != frame 0" % (p["name"], n)
        assert np.array_equal(frames[n + 0], fx.render_frame(p, 2 * n)), p["name"]
        steps = [changed(frames[i], frames[i + 1]) for i in range(n - 1)]
        seam = changed(frames[n - 1], frames[0])
        assert seam <= max(steps), "%s: seam step %d > max step %s" % (p["name"], seam, steps)
        assert max(steps) > 0, "%s: loop does not animate" % p["name"]
        print("fx_loop: %s ok (%d frames, steps %s, seam %d)" % (p["name"], n, steps, seam))


def run_cli(params, out):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "fx.py")] + params + ["-o", out],
                   check=True, stdout=subprocess.DEVNULL)


def test_determinism():
    assert len(PARAMS) >= 10, "expected the 10 effects of #22"
    with tempfile.TemporaryDirectory() as tmp:
        a, b, c = (os.path.join(tmp, d) for d in "abc")
        run_cli(PARAMS, a)
        run_cli(PARAMS, b)
        files = sorted(os.listdir(a))
        assert files == sorted(os.listdir(b))
        match, mismatch, errors = filecmp.cmpfiles(a, b, files, shallow=False)
        assert not mismatch and not errors, (mismatch, errors)
        # Naming contract and frame count, and every frame non-empty.
        for path in PARAMS:
            p = fx.load_params(path)
            for f in range(p["frames"]):
                name = "%s_%s_d0_f%d.tex" % (p["name"], p["anim"], f)
                assert name in files, name
                t = bast.Tex.load(os.path.join(a, name))
                assert (t.width, t.height) == tuple(p["size"]), name
                assert t.origin == fx.origin(p), name
                assert any(v != fx.TRANSPARENT for v in t.pixels), "%s is empty" % name
        assert len(files) == sum(fx.load_params(p)["frames"] for p in PARAMS)
        # A different seed changes the result.
        p = json.load(open(PARAMS[0]))
        p["seed"] += 1
        alt = os.path.join(tmp, "alt.json")
        json.dump(p, open(alt, "w"))
        run_cli([alt], c)
        diff = [f for f in os.listdir(c)
                if not filecmp.cmp(os.path.join(a, f), os.path.join(c, f), shallow=False)]
        assert diff, "seed has no effect"
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), a],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
        print("fx_determinism: %d files identical across runs; %s" % (len(files), r.stdout.strip()))


if __name__ == "__main__":
    {"loop": test_loop, "determinism": test_determinism}[sys.argv[1]]()
