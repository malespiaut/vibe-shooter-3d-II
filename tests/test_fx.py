#!/usr/bin/env python3
"""Tests for tools/fx.py (GAME.md 6.3 effect generator).

  test_fx.py loop         every loop effect: frame n (one past the last)
                          equals frame 0 bit for bit, and the seam step
                          n-1 -> 0 is no larger than the largest in-loop step.
  test_fx.py determinism  two separate runs of the CLI on every effect give
                          byte-identical TEX files; a different seed gives a
                          different effect; the output passes palcheck and
                          follows the naming contract.
  test_fx.py shapes       art-director rules: explosion ignition frames carry
                          no gray/brown and have a lobed silhouette; the
                          reticle ring is solid, closed fb_red, fb_red_dim
                          only in runs of >= 3, no inner shape under 8x4.
"""
import math
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


def components(mask, diag=True):
    """Connected components of a boolean mask as lists of (y, x)."""
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    nb = [(-1, 0), (1, 0), (0, -1), (0, 1)]
    if diag:
        nb += [(-1, -1), (-1, 1), (1, -1), (1, 1)]
    out = []
    for y0, x0 in zip(*np.nonzero(mask)):
        if seen[y0, x0]:
            continue
        comp, stack = [], [(y0, x0)]
        seen[y0, x0] = True
        while stack:
            y, x = stack.pop()
            comp.append((y, x))
            for dy, dx in nb:
                yy, xx = y + dy, x + dx
                if 0 <= yy < h and 0 <= xx < w and mask[yy, xx] and not seen[yy, xx]:
                    seen[yy, xx] = True
                    stack.append((yy, xx))
        out.append(comp)
    return out


def outer_radii(idx):
    """Over 32 angles from the canvas centre: (farthest opaque pixel,
    first transparent pixel), in px, stepping 0.25 px."""
    h, w = idx.shape
    cx, cy = w / 2.0, h / 2.0
    far, body = [], []
    for k in range(32):
        a = 2 * math.pi * k / 32
        r, last, first = 0.0, 0.0, None
        while True:
            x, y = cx + math.cos(a) * r, cy + math.sin(a) * r
            if not (0 <= x < w and 0 <= y < h):
                break
            if idx[int(y), int(x)] != fx.TRANSPARENT:
                last = r
            elif first is None:
                first = r
            r += 0.25
        far.append(last)
        body.append(r if first is None else first)
    return max(far) - min(far), max(body) - min(body)


def test_shapes():
    names = fx.palette_names()
    by_index = {v: k for k, v in names.items()}
    params = {p["name"]: p for p in map(fx.load_params, PARAMS)}
    # Explosions: ignition (frames 0, 1) has no smoke colours; frames 1 and 2
    # have a lobed outer edge (variation over 32 angles).
    for name, need in (("explosion_small", 4.0), ("explosion_big", 6.0)):
        p = params[name]
        for f in range(p["frames"]):
            idx = fx.render_frame(p, f)
            if f < 2:
                smoke = sorted({by_index[i] for i in np.unique(idx) if i != fx.TRANSPARENT
                                and by_index[i].startswith(("gray_", "brown_"))})
                assert not smoke, "%s f%d: smoke colours at ignition %s" % (name, f, smoke)
            if f in (1, 2):
                far, body = outer_radii(idx)
                assert far >= need and body >= need, \
                    "%s f%d: outer radius varies %.2f/%.2f px < %.0f" % (name, f, far, body, need)
                print("fx_shapes: %s f%d outer radius variation %.2f px (body %.2f px), need >= %.0f"
                      % (name, f, far, body, need))
    # Reticle.
    p = params["reticle"]
    red, dim = names["fb_red"], names["fb_red_dim"]
    w, h = p["size"]
    for f in range(p["frames"]):
        idx = fx.render_frame(p, f)
        tag = "reticle f%d" % f
        # The outer ring is one 8-connected fb_red component touching all four
        # canvas edges, and it is closed: a 4-connected flood from the corner
        # through everything else never reaches the centre.
        rc = [c for c in components(idx == red) if len(c) > 8]
        ring = max(rc, key=len)
        ys, xs = zip(*ring)
        assert (min(xs), max(xs), min(ys), max(ys)) == (0, w - 1, 0, h - 1), tag
        outside = next(c for c in components(idx != red, diag=False) if (0, 0) in c)
        assert (h // 2, w // 2) not in outside, "%s: outer ring is broken" % tag
        assert not any(idx[y, x] != red for y, x in ring), tag
        # fb_red_dim only in runs of >= 3 px.
        for c in components(idx == dim):
            assert len(c) >= 3, "%s: fb_red_dim run of %d px at %s" % (tag, len(c), c[0])
        # Apart from the outer ring and the centre dot, every shape is >= 8x4.
        for c in components(idx != fx.TRANSPARENT):
            ys, xs = zip(*c)
            bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
            if (h // 2, w // 2) in c and bw <= 2 and bh <= 2:
                continue
            assert bw >= 8 and bh >= 4, "%s: inner shape %dx%d < 8x4" % (tag, bw, bh)
    print("fx_shapes: reticle ok (%d frames)" % p["frames"])


if __name__ == "__main__":
    {"loop": test_loop, "determinism": test_determinism, "shapes": test_shapes}[sys.argv[1]]()
