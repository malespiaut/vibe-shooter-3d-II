#!/usr/bin/env python3
"""Pre-rendered particle effects from radial energy layers (GAME.md 6.3).

Usage: python3 tools/fx.py <params.json ...> -o <dir> [--preview <dir>]

Each params file describes one effect. Every frame accumulates the energy of
independent layers per pixel (core disc, rays, ring, sparkles, then a noise
modulation), and maps the energy directly to a palette-index ramp listed by
entry name: no RGB, no quantization, no blur, no alpha. Low energy becomes
index 255 (transparent). Frames are written as TEX through libbast
(tools/bast.py), named `<name>_<anim>_d0_f<n>.tex` (tech.md sprite naming).

Params (JSON):
  name, anim         file names: <name>_<anim>_d0_f<n>.tex
  size [w, h]        canvas in px; the effect centre is the canvas centre
  origin             "center" or "bottom" (bottom-centre anchor)
  frames             frame count
  mode               "oneshot" (rise, peak, decay) or "loop" (seamless)
  seed               integer; the output is a pure function of the params
  ramp               palette entry names, hottest first
  cutoff             energy below this is transparent (default 0.15)
  squash             vertical scale of the field, 1 = round (default 1)
  envelope           oneshot only: {"peak": 0.25, "decay": 1.5}
  ease               oneshot growth exponent: s = 1 - (1 - t)^ease (default 2)
  layers             list of layer objects, see below

Sizes are fractions of R = w / 2. A numeric layer value may be a pair [a, b]:
it moves from a to b with the growth s (oneshot: eased time; loop:
0.5 - 0.5 cos(2 pi p), so it pulses and returns).
  core      radius, intensity, soft (falloff exponent)
  rays      count, length, min_length (fraction of length), width (px),
            intensity, flicker (loop length wobble), spin (turns, integer
            for loops)
  ring      radius, width, intensity
  sparkles  count, radius (start), travel, arm (px), intensity,
            life [min, max] (oneshot)
  noise     scale (px per lattice cell), amount, octaves, drift (oneshot
            offset in cells), orbit (loop: radius in cells of the circular
            path through the noise field)

Loop mode samples the noise field along a circle and makes every time-varying
term periodic in p = f / frames, so frame `frames` equals frame 0 and the seam
is as smooth as any other step (tests/test_fx.py checks both).
"""
import argparse
import json
import math
import os
import random
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import palette  # noqa: E402

TRANSPARENT = 255


class FxError(Exception):
    pass


_names = None


def palette_names():
    """name -> index for every usable entry (not reserved, not transparent)."""
    global _names
    if _names is None:
        entries = palette.load_gpl()
        fl = palette.flags(entries)
        _names = {n: i for i, ((_, n), f) in enumerate(zip(entries, fl))
                  if not f & (palette.FLAG_RESERVED | palette.FLAG_TRANSPARENT)}
    return _names


def load_params(path):
    with open(path) as f:
        p = json.load(f)
    for key in ("name", "anim", "size", "frames", "mode", "ramp", "layers"):
        if key not in p:
            raise FxError("%s: missing %r" % (path, key))
    if p["mode"] not in ("oneshot", "loop"):
        raise FxError("%s: mode must be oneshot or loop" % path)
    names = palette_names()
    for n in p["ramp"]:
        if n not in names:
            raise FxError("%s: ramp entry %r is not a usable palette entry" % (path, n))
    w, h = p["size"]
    if not (1 <= w <= 1024 and 1 <= h <= 1024) or p["frames"] < 1:
        raise FxError("%s: bad size or frame count" % path)
    p.setdefault("seed", 1)
    p.setdefault("origin", "center")
    p["_path"] = path
    return p


# ---------------------------------------------------------------- noise

class ValueNoise:
    """Smooth 2D value noise on an integer lattice, hashed from the seed."""

    def __init__(self, seed, size=256):
        rnd = random.Random(seed)
        perm = list(range(size))
        rnd.shuffle(perm)
        self.perm = np.array(perm + perm, dtype=np.int64)
        self.vals = np.array([rnd.random() for _ in range(size)], dtype=np.float64)
        self.mask = size - 1

    def _lattice(self, ix, iy):
        m = self.mask
        return self.vals[self.perm[self.perm[ix & m] + (iy & m)]]

    def __call__(self, x, y):
        x0, y0 = np.floor(x), np.floor(y)
        fx, fy = x - x0, y - y0
        ix, iy = x0.astype(np.int64), y0.astype(np.int64)
        sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        a = self._lattice(ix, iy)
        b = self._lattice(ix + 1, iy)
        c = self._lattice(ix, iy + 1)
        d = self._lattice(ix + 1, iy + 1)
        top = a + (b - a) * sx
        bot = c + (d - c) * sx
        return top + (bot - top) * sy

    def fbm(self, x, y, octaves):
        total, amp, norm = 0.0, 1.0, 0.0
        for o in range(octaves):
            f = 2.0 ** o
            total = total + amp * self(x * f + 17.0 * o, y * f + 31.0 * o)
            norm += amp
            amp *= 0.5
        return total / norm


# ---------------------------------------------------------------- timing

def frame_time(p, f):
    """(t, s, env, phase): t time 0..1, s growth, env intensity envelope,
    phase loop angle in radians (0 for oneshot)."""
    n = p["frames"]
    if p["mode"] == "loop":
        ph = 2.0 * math.pi * ((f % n) / n)
        return (f % n) / n, 0.5 - 0.5 * math.cos(ph), 1.0, ph
    t = (f + 0.5) / n
    env = p.get("envelope", {})
    peak, decay = env.get("peak", 0.25), env.get("decay", 1.5)
    if t < peak:
        e = math.sqrt(t / peak)
    else:
        # The decay ends one frame after the last, so the last frame keeps a
        # wisp instead of being wasted on an empty canvas.
        e = max(0.0, 1.0 - (t - peak) / (1.0 + 1.0 / n - peak)) ** decay
    s = 1.0 - (1.0 - t) ** p.get("ease", 2.0)
    return t, s, e, 0.0


def val(v, s):
    if isinstance(v, (list, tuple)):
        return v[0] + (v[1] - v[0]) * s
    return float(v)


# ---------------------------------------------------------------- layers

def layer_core(L, g, s, rnd):
    r = max(1e-6, val(L.get("radius", 0.5), s) * g["R"])
    x = np.clip(1.0 - g["d"] / r, 0.0, 1.0)
    return val(L.get("intensity", 1.0), s) * x ** L.get("soft", 1.0)


def layer_rays(L, g, s, rnd, t, ph, loop):
    count = int(L.get("count", 8))
    length = val(L.get("length", 1.0), s) * g["R"]
    minl = L.get("min_length", 0.4)
    width = val(L.get("width", 1.5), s)
    inten = val(L.get("intensity", 1.0), s)
    flick = L.get("flicker", 0.0)
    spin = L.get("spin", 0.0) * 2.0 * math.pi * (t if not loop else ph / (2.0 * math.pi))
    out = np.zeros_like(g["d"])
    for k in range(count):
        a = 2.0 * math.pi * (k + rnd.uniform(-0.35, 0.35)) / count + spin
        lk = length * rnd.uniform(minl, 1.0)
        if loop and flick:
            ph1, ph2 = rnd.uniform(0, 2 * math.pi), rnd.uniform(0, 2 * math.pi)
            m = 0.6 * math.sin(ph + ph1) + 0.4 * math.sin(2 * ph + ph2)
            lk *= 1.0 + flick * m
        lk = max(lk, 1e-6)
        ca, sa = math.cos(a), math.sin(a)
        along = g["dx"] * ca + g["dy"] * sa
        perp = np.abs(-g["dx"] * sa + g["dy"] * ca)
        frac = np.clip(1.0 - along / lk, 0.0, 1.0)
        wid = width * frac + 0.35
        e = np.clip(1.0 - perp / wid, 0.0, 1.0) * frac
        out += np.where(along > 0, e, 0.0)
    return inten * out


def layer_ring(L, g, s, rnd):
    r = val(L.get("radius", 0.8), s) * g["R"]
    w = max(1e-6, val(L.get("width", 2.0), s))
    return val(L.get("intensity", 1.0), s) * np.clip(1.0 - np.abs(g["d"] - r) / w, 0.0, 1.0)


def layer_sparkles(L, g, s, rnd, t, ph, loop):
    out = np.zeros_like(g["d"])
    h, w = out.shape
    count = int(L.get("count", 6))
    r0 = L.get("radius", 0.2) * g["R"]
    travel = L.get("travel", 0.7) * g["R"]
    arm = int(L.get("arm", 1))
    inten = L.get("intensity", 1.0)
    lmin, lmax = L.get("life", [0.3, 0.6])
    for _ in range(count):
        a = rnd.uniform(0, 2 * math.pi)
        spd = rnd.uniform(0.6, 1.0)
        if loop:
            age = (ph / (2 * math.pi) + rnd.random()) % 1.0
        else:
            birth, life = rnd.uniform(0.0, 0.45), rnd.uniform(lmin, lmax)
            age = (t - birth) / life
            if not 0.0 <= age < 1.0:
                continue
        rad = r0 + travel * spd * age
        cx = int(math.floor(g["cx"] + math.cos(a) * rad))
        cy = int(math.floor(g["cy"] + math.sin(a) * rad * g["squash"]))
        e = inten * (1.0 - age) ** 0.5
        # Loop sparkles fade in too, so wrap-around never pops.
        if loop:
            e *= min(1.0, age * 5.0)
        for dx, dy, k in [(0, 0, 1.0)] + [(sx * i, sy * i, 0.6 / i)
                                          for i in range(1, arm + 1)
                                          for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1))]:
            x, y = cx + dx, cy + dy
            if 0 <= x < w and 0 <= y < h:
                out[y, x] += e * k
    return out


def noise_factor(L, g, noise, t, ph, loop):
    sc = L.get("scale", 4.0)
    x, y = g["dx"] / sc, g["dy"] / sc
    if loop:
        orb = L.get("orbit", 1.5)
        x = x + orb * math.cos(ph)
        y = y + orb * math.sin(ph)
    else:
        x = x + L.get("drift", 1.0) * t
    n = noise.fbm(x + 50.0, y + 50.0, int(L.get("octaves", 2)))
    return 1.0 + L.get("amount", 0.5) * (2.0 * n - 1.0)


# ---------------------------------------------------------------- render

def grid(p):
    w, h = p["size"]
    sq = p.get("squash", 1.0)
    cx, cy = w / 2.0, h / 2.0
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    dx = xs + 0.5 - cx
    dy = (ys + 0.5 - cy) / sq
    return {"dx": dx, "dy": dy, "d": np.sqrt(dx * dx + dy * dy),
            "R": w / 2.0, "cx": cx, "cy": cy, "squash": sq}


def energy(p, f):
    """Per-pixel energy of frame f (f may be >= frames for loops)."""
    g = grid(p)
    t, s, env, ph = frame_time(p, f)
    loop = p["mode"] == "loop"
    E = np.zeros_like(g["d"])
    noise_layers = []
    for i, L in enumerate(p["layers"]):
        # One RNG per layer, reseeded every frame: the random layout (ray
        # angles, sparkle paths) is identical in every frame.
        rnd = random.Random(p["seed"] * 1000003 + i)
        kind = L["type"]
        if kind == "core":
            E += layer_core(L, g, s, rnd)
        elif kind == "rays":
            E += layer_rays(L, g, s, rnd, t, ph, loop)
        elif kind == "ring":
            E += layer_ring(L, g, s, rnd)
        elif kind == "sparkles":
            E += layer_sparkles(L, g, s, rnd, t, ph, loop)
        elif kind == "noise":
            noise_layers.append((L, ValueNoise(p["seed"] * 7919 + i)))
        else:
            raise FxError("%s: unknown layer type %r" % (p["_path"], kind))
    for L, nz in noise_layers:
        E *= noise_factor(L, g, nz, t, ph, loop)
    return E * env


def render_frame(p, f):
    """Frame f as a (h, w) uint8 array of palette indices."""
    E = energy(p, f)
    names = palette_names()
    ramp = np.array([names[n] for n in p["ramp"]], dtype=np.uint8)
    cut = p.get("cutoff", 0.15)
    x = np.clip((E - cut) / (1.0 - cut), 0.0, 1.0)
    k = np.minimum(len(ramp) - 1, np.floor((1.0 - x) * len(ramp)).astype(np.int64))
    return np.where(E < cut, TRANSPARENT, ramp[k]).astype(np.uint8)


def origin(p):
    w, h = p["size"]
    return (w // 2, h) if p["origin"] == "bottom" else (w // 2, h // 2)


def to_tex(p, idx):
    h, w = idx.shape
    fl = palette.flags(palette.load_gpl())
    used = set(np.unique(idx).tolist()) - {TRANSPARENT}
    flags = bast.TEX_HAS_TRANSPARENCY if TRANSPARENT in idx else 0
    if used and all(fl[i] & palette.FLAG_FULLBRIGHT for i in used):
        flags |= bast.TEX_FULLBRIGHT
    return bast.Tex(w, h, np.ascontiguousarray(idx.T).tobytes(), origin(p), flags)


def frame_name(p, f):
    return "%s_%s_d0_f%d.tex" % (p["name"], p["anim"], f)


def contact_sheet(p, frames, scale=3, gap=2):
    """All frames at `scale`x, a row on dark gray_02, a row on sand_08,
    as an indexed PNG in the game palette."""
    names = palette_names()
    entries = palette.load_gpl()
    h, w = frames[0].shape
    cols = len(frames)
    W = gap + cols * (w + gap)
    H = gap + 2 * (h + gap)
    sheet = np.zeros((H, W), dtype=np.uint8)
    for row, bg in enumerate(("gray_02", "sand_08")):
        sheet[gap // 2 + row * (h + gap):gap // 2 + (row + 1) * (h + gap), :] = names[bg]
        for c, fr in enumerate(frames):
            y0, x0 = gap + row * (h + gap), gap + c * (w + gap)
            cell = sheet[y0:y0 + h, x0:x0 + w]
            np.copyto(cell, fr, where=fr != TRANSPARENT)
    img = Image.fromarray(sheet, "P")
    img.putpalette([c for rgb, _ in entries for c in rgb])
    return img.resize((W * scale, H * scale), Image.NEAREST)


def build(p, out_dir, preview_dir=None):
    os.makedirs(out_dir, exist_ok=True)
    frames = [render_frame(p, f) for f in range(p["frames"])]
    for f, idx in enumerate(frames):
        to_tex(p, idx).save(os.path.join(out_dir, frame_name(p, f)))
    if preview_dir:
        os.makedirs(preview_dir, exist_ok=True)
        contact_sheet(p, frames).save(os.path.join(preview_dir, "%s.png" % p["name"]))
    return frames


def main(argv=None):
    ap = argparse.ArgumentParser(description="energy-layer effect generator (GAME.md 6.3)")
    ap.add_argument("params", nargs="+")
    ap.add_argument("-o", "--out", required=True, help="output directory for TEX frames")
    ap.add_argument("--preview", help="write a 3x contact sheet PNG per effect here")
    args = ap.parse_args(argv)
    try:
        for path in args.params:
            p = load_params(path)
            frames = build(p, args.out, args.preview)
            used = sorted(set(np.unique(np.stack(frames)).tolist()) - {TRANSPARENT})
            print("fx: %s -> %d x %dx%d %s, %d indices"
                  % (p["name"], len(frames), p["size"][0], p["size"][1], p["mode"], len(used)))
    except (OSError, ValueError, KeyError, FxError, bast.BastError) as e:
        print("fx: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
