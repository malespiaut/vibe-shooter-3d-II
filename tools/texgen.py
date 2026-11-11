#!/usr/bin/env python3
"""Procedural texture generator (GAME.md 6.1, docs/design/art-bible.md).

    python3 tools/texgen.py assets/src/textures/*.json -o assets/gen/textures
                            [--preview previews/] [-q]

Each `<name>.json` becomes `<outdir>/<name>.tex` (64x64 by default), and with
`--preview` an indexed `<previewdir>/<name>.png` carrying the game palette.
The parameter schema is documented in assets/src/textures/README.md.

Pipeline (GAME.md 6.1 steps):
 1. Load source maps: heightmap, region map and per-block tone are built from
    the primitives in the parameter file (bricks, panels, rivets, grooves,
    inset 1-bit glyph masks from assets/src/textures/glyphs/).
 2. Lighting: normals from the heightmap (central differences), Lambert term
    from the one fixed key light (top-left, 45 degrees, in front of the
    viewer), and heightfield shadows cast toward the bottom-right.
 3. Noise and grime: tileable value noise (lattice wraps at the tile edge);
    grime = cavity (blurred height minus height) plus noise.
 4. Material colours: every region names a ramp range of the base colour;
    where grime passes `base2.threshold` the second base colour's ramp is
    used instead. The shade value picks an entry of the ramp (optional 2x2
    checker between two adjacent entries, the only dithering the art bible
    allows).
 5. Brightmap regions keep their fullbright entry.
 6. Quantize through tools/quantize.py (libbast Oklab nearest).
 7. Export TEX through libbast.

Every operation works modulo the tile size (np.roll, wrapped lattices,
wrapped coordinates), so textures tile seamlessly in both axes. Determinism:
all randomness comes from numpy's PCG64 seeded with the file's `seed`.
Floating point is fine here: this is an offline tool, not gameplay.
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import palette as gpl  # noqa: E402
import quantize  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLYPH_DIR = os.path.join(ROOT, "assets", "src", "textures", "glyphs")

# Key light: from the top-left at 45 degrees, in front of the viewer. Image
# axes: +x right, +y down, +z out of the screen toward the viewer.
LIGHT = np.array([-1.0, -1.0, 1.41421356])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


class TexgenError(Exception):
    pass


# ---------------------------------------------------------------- palette

class Pal:
    def __init__(self, pal):
        entries = gpl.load_gpl()
        self.names = {name: i for i, (_, name) in enumerate(entries)}
        self.colors = np.array(pal.colors, dtype=np.uint8)
        self.flags = pal.flags

    def ramp(self, name, lo, hi):
        """Indices <name>_<lo> .. <name>_<hi> (inclusive), dark to light."""
        out = []
        for n in range(lo, hi + 1):
            key = "%s_%02d" % (name, n)
            if key not in self.names:
                raise TexgenError("no palette entry %s" % key)
            i = self.names[key]
            if self.flags[i] & (bast.PAL_RESERVED | bast.PAL_FULLBRIGHT | bast.PAL_TRANSPARENT):
                raise TexgenError("%s is not a plain shaded entry" % key)
            out.append(i)
        return out

    def fullbright(self, name):
        i = self.names.get(name)
        if i is None or not self.flags[i] & bast.PAL_FULLBRIGHT:
            raise TexgenError("%s is not a fullbright entry" % name)
        return i


# ---------------------------------------------------------------- noise

def value_noise(rng, size, cells, octaves=1):
    """Tileable value noise in [0, 1]: random lattice of `cells` x `cells`
    that wraps at the tile edge, smoothstep interpolation, octaves double
    the lattice and halve the amplitude."""
    out = np.zeros((size, size))
    amp, total = 1.0, 0.0
    for _ in range(octaves):
        if size % cells:
            raise TexgenError("noise cells %d must divide size %d" % (cells, size))
        grid = rng.random((cells, cells))
        step = size // cells
        t = (np.arange(size) % step) / step
        t = t * t * (3 - 2 * t)
        i0 = np.arange(size) // step
        i1 = (i0 + 1) % cells
        ty, tx = t[:, None], t[None, :]
        a = grid[i0[:, None], i0[None, :]]
        b = grid[i0[:, None], i1[None, :]]
        c = grid[i1[:, None], i0[None, :]]
        d = grid[i1[:, None], i1[None, :]]
        out += amp * ((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty)
        total += amp
        amp *= 0.5
        cells *= 2
        if cells > size:
            break
    return out / total


def box_blur(a, r):
    out = np.zeros_like(a)
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out += np.roll(a, (dy, dx), axis=(0, 1))
    return out / (2 * r + 1) ** 2


# ---------------------------------------------------------------- primitives

class Surface:
    """Heightmap, region map (names) and per-pixel tone offset."""

    def __init__(self, size, rng):
        self.size = size
        self.rng = rng
        self.h = np.zeros((size, size))
        self.region = np.full((size, size), "", dtype=object)
        self.tone = np.zeros((size, size))
        yy, xx = np.mgrid[0:size, 0:size]
        self.xx, self.yy = xx, yy

    def rect_profile(self, x, y, w, h, bevel):
        """(mask, profile) of a rectangle placed with wrap-around: profile
        rises 0..1 over `bevel` px from each edge (1 on the flat top)."""
        u = (self.xx - x) % self.size
        v = (self.yy - y) % self.size
        mask = (u < w) & (v < h)
        d = np.minimum(np.minimum(u + 0.5, w - u - 0.5), np.minimum(v + 0.5, h - v - 0.5))
        prof = np.clip(d / max(bevel, 1e-6), 0, 1) if bevel > 0 else np.ones_like(d)
        return mask, prof


def prim_bricks(s, p):
    rows, cols = int(p["rows"]), int(p["cols"])
    m = int(p.get("mortar", 2))
    bevel = float(p.get("bevel", 2))
    if s.size % rows or s.size % cols:
        raise TexgenError("bricks: rows and cols must divide the size")
    bh, bw = s.size // rows, s.size // cols
    jitter = float(p.get("jitter", 0.0))
    tone = float(p.get("tone", 0.0))
    stagger = float(p.get("offset", 0.5))
    nblocks = rows * cols
    hj = s.rng.uniform(-jitter, jitter, nblocks)
    tj = s.rng.uniform(-tone, tone, nblocks)
    for r in range(rows):
        off = int(round(r * stagger * bw)) % s.size
        for c in range(cols):
            mask, prof = s.rect_profile(off + c * bw, r * bh, bw - m, bh - m, bevel)
            b = r * cols + c
            s.h[mask] = prof[mask] * (1.0 + hj[b])
            s.region[mask] = p.get("region", "face")
            s.tone[mask] = tj[b]
    rest = s.region == ""
    s.h[rest] = 0.0
    s.region[rest] = p.get("mortar_region", "mortar")


def prim_panel(s, p):
    x, y, w, h = (int(v) for v in p["rect"])
    mask, prof = s.rect_profile(x, y, w, h, float(p.get("bevel", 2)))
    depth = float(p.get("depth", 1.0))   # > 0 raised, < 0 inset
    base = s.h.copy()
    s.h[mask] = base[mask] + prof[mask] * depth
    if "region" in p:
        s.region[mask] = p["region"]
    s.tone[mask] += float(p.get("tone", 0.0))


def prim_rivets(s, p):
    r = float(p.get("radius", 1.5))
    hgt = float(p.get("height", 0.8))
    for px, py in p["points"]:
        dx = (s.xx - px + s.size / 2) % s.size - s.size / 2
        dy = (s.yy - py + s.size / 2) % s.size - s.size / 2
        d2 = (dx * dx + dy * dy) / (r * r)
        mask = d2 < 1.0
        s.h[mask] += hgt * np.sqrt(1.0 - d2[mask])
        s.region[mask] = p.get("region", "rivet")


def prim_grooves(s, p):
    w = float(p.get("width", 1))
    depth = float(p.get("depth", 0.6))
    for x0, y0, x1, y1 in p["lines"]:
        if x0 != x1 and y0 != y1:
            raise TexgenError("grooves: lines must be horizontal or vertical")
        x0, x1 = sorted((x0, x1))
        y0, y1 = sorted((y0, y1))
        mask, _ = s.rect_profile(x0, y0, max(x1 - x0, w), max(y1 - y0, w), 0)
        s.h[mask] -= depth
        s.region[mask] = p.get("region", "groove")


def load_glyph(name):
    path = os.path.join(GLYPH_DIR, name)
    img = Image.open(path)
    img.load()
    return np.asarray(img.convert("1"), dtype=bool)  # True = white = carved


def prim_glyph(s, p):
    g = load_glyph(p["png"])
    gh, gw = g.shape
    x, y = int(p["x"]), int(p["y"])
    ys = (np.arange(gh)[:, None] + y) % s.size
    xs = (np.arange(gw)[None, :] + x) % s.size
    mask = np.zeros((s.size, s.size), dtype=bool)
    mask[ys, xs] = g
    s.h[mask] -= float(p.get("depth", 0.6))
    s.region[mask] = p.get("region", "glyph")


PRIMITIVES = {
    "bricks": prim_bricks,
    "panel": prim_panel,
    "rivets": prim_rivets,
    "grooves": prim_grooves,
    "glyph": prim_glyph,
}


# ---------------------------------------------------------------- pipeline

def shadow_map(h, steps, slope):
    """1 where a texel is hidden from the key light by higher texels toward
    the top-left (heightfield ray march along the diagonal), else 0."""
    sh = np.zeros_like(h)
    for k in range(1, steps + 1):
        occ = np.roll(h, (k, k), axis=(0, 1)) - k * slope
        sh = np.maximum(sh, (occ > h + 1e-6).astype(float))
    return sh


def generate(params, pal):
    """Parameter dict -> (indices HxW uint8, brightmap list of (mask, idx))."""
    size = int(params.get("size", 64))
    seed = int(params["seed"])
    rng = np.random.Generator(np.random.PCG64(seed))
    s = Surface(size, rng)

    # 1. source maps
    for layer in params["layers"]:
        kind = layer.get("type")
        if kind not in PRIMITIVES:
            raise TexgenError("unknown layer type %r" % kind)
        PRIMITIVES[kind](s, layer)
    if (s.region == "").any():
        s.region[s.region == ""] = "face"

    nz = params.get("noise", {})
    grain = value_noise(rng, size, int(nz.get("cells", 16)), int(nz.get("octaves", 3)))
    blotch = value_noise(rng, size, int(nz.get("blotch_cells", 4)), 2)
    h = s.h + (grain - 0.5) * float(nz.get("height", 0.0))

    # 2. lighting
    li = params.get("light", {})
    relief = float(li.get("relief", 1.0))
    dhdx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * relief
    dhdy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * relief
    n = np.stack([-dhdx, -dhdy, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    lambert = np.clip(n @ LIGHT, 0, 1) / LIGHT[2]   # 1.0 on a flat surface
    shadow = shadow_map(h, int(li.get("shadow_steps", 3)), float(li.get("shadow_slope", 0.35)))

    # 3. noise and grime
    gr = params.get("grime", {})
    cavity = np.clip(box_blur(h, int(gr.get("radius", 2))) - h, 0, None)
    cavity = cavity / (cavity.max() or 1.0)
    grime = np.clip(cavity * float(gr.get("cavity", 0.6))
                    + (blotch - 0.5) * float(gr.get("noise", 0.4))
                    + (grain - 0.5) * float(gr.get("grain", 0.2)), 0, 1)

    v = (float(li.get("contrast", 0.6)) * (lambert - 1.0)
         - float(li.get("shadow", 0.25)) * shadow
         + (grain - 0.5) * float(nz.get("amount", 0.15))
         + (blotch - 0.5) * float(nz.get("blotch", 0.1))
         + s.tone
         - grime * float(gr.get("darken", 0.15)))

    # 4. material colours
    mats = params["materials"]
    base2 = params.get("base2")
    out = np.zeros((size, size), dtype=np.int32)
    for reg in sorted(set(s.region.ravel().tolist())):
        if reg not in mats:
            raise TexgenError("region %r has no material" % reg)
        m = mats[reg]
        mask = s.region == reg
        val = np.clip(float(m.get("level", 0.5)) + v, 0, 1)
        use2 = np.zeros_like(mask)
        if base2 and m.get("base2", True):
            use2 = mask & (grime > float(base2.get("threshold", 0.6)))
        specs = [(pal.ramp(m["ramp"], int(m["lo"]), int(m["hi"])), mask & ~use2, m.get("dither"))]
        if use2.any():
            specs.append((pal.ramp(base2["ramp"], int(base2["lo"]), int(base2["hi"])), use2, False))
        for ramp, sel, dither in specs:
            if not sel.any():
                continue
            top = len(ramp) - 1
            pos = val * top
            lo = np.floor(pos).astype(int)
            frac = pos - lo
            pick = np.where(frac >= 0.5, np.minimum(lo + 1, top), lo)
            if dither:
                band = (frac > 0.375) & (frac < 0.625)
                checker = ((s.xx + s.yy) & 1) == 1
                pick = np.where(band, np.where(checker, np.minimum(lo + 1, top), lo), pick)
            out[sel] = np.array(ramp)[pick[sel]]

    # 5. brightmap
    bright = []
    for b in params.get("brightmap", []):
        idx = pal.fullbright(b["entry"])
        if "rect" in b:
            x, y, w, hh = (int(q) for q in b["rect"])
            mask, _ = s.rect_profile(x, y, w, hh, 0)
        elif "glyph" in b:
            g = load_glyph(b["glyph"])
            gh, gw = g.shape
            mask = np.zeros((size, size), dtype=bool)
            mask[(np.arange(gh)[:, None] + int(b["y"])) % size,
                 (np.arange(gw)[None, :] + int(b["x"])) % size] = g
        else:
            raise TexgenError("brightmap entry needs rect or glyph")
        bright.append((mask, idx))
    return out.astype(np.uint8), bright


def build_tex(params, pal_obj, pal):
    """Params -> bast.Tex (steps 1-7)."""
    idx, bright = generate(params, pal)
    size = idx.shape[0]
    rgb = pal.colors[idx]
    rgba = np.concatenate([rgb, np.full(idx.shape + (1,), 255, np.uint8)], axis=-1)
    # 6. quantize through libbast (Oklab nearest). Every colour is already an
    # exact palette entry, so this must be the identity; check it.
    t = quantize.quantize_tex(rgba.tobytes(), size, size, (0, 0), False, pal_obj)
    got = np.frombuffer(bytes(t.pixels), np.uint8).reshape(size, size).T
    if not np.array_equal(got, idx):
        bad = int((got != idx).sum())
        raise TexgenError("quantize changed %d texels (duplicate palette colour?)" % bad)
    # 5. brightmap regions keep their fullbright entry (libbast nearest never
    # returns fullbright indices, so they are stamped after quantization).
    final = idx.copy()
    for mask, i in bright:
        final[mask] = i
    t.pixels = bytearray(final.T.tobytes())  # column-major
    return t


def main(argv=None):
    ap = argparse.ArgumentParser(description="procedural textures (GAME.md 6.1)")
    ap.add_argument("inputs", nargs="+", metavar="params.json")
    ap.add_argument("-o", "--out", required=True, help="output directory")
    ap.add_argument("--preview", help="directory for indexed PNG previews")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)
    try:
        pal_obj = bast.load_palette()
        pal = Pal(pal_obj)
        os.makedirs(args.out, exist_ok=True)
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
        for src in args.inputs:
            stem = os.path.splitext(os.path.basename(src))[0]
            with open(src) as f:
                params = json.load(f)
            t = build_tex(params, pal_obj, pal)
            dst = os.path.join(args.out, stem + ".tex")
            t.save(dst)
            if args.preview:
                t.to_image(pal_obj).save(os.path.join(args.preview, stem + ".png"))
            if not args.quiet:
                print("texgen: %s -> %s (%dx%d, %d colours)"
                      % (src, dst, t.width, t.height, len(set(t.pixels))))
    except (OSError, ValueError, KeyError, TexgenError, bast.BastError) as e:
        print("texgen: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
