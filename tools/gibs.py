#!/usr/bin/env python3
"""Offline gib-animation generator (GAME.md 6.2).

Usage:
  python3 tools/gibs.py <params.json> -i <source.tex> -o <outdir> [--preview <dir>] [-q]

Three stages, all in palette-index space (no RGB re-quantization):

1. Voronoi decomposition: K seed pixels picked among the opaque pixels of the
   source TEX (best-candidate sampling, so seeds spread evenly); every opaque
   pixel joins its nearest seed (squared distance, ties to the lower seed).
   Each cell is one chunk.
2. Wound bleeding: a pixel with a 4-neighbour in a higher-id chunk is a wound
   (depth 0); a breadth-first search inside each chunk gives every pixel its
   depth from the nearest wound. Pixels with depth < wound_depth are blended
   toward the blood ramp (a palette ramp name such as "red", "brown" or
   "cyan"): the target is the ramp entry whose Oklab lightness is closest to
   the pixel's lightness + wound_shade, mixed with the original colour by
   a = 1 - depth / wound_depth, and the result is the closest entry among
   {ramp entries, original index}. Deeper pixels keep their index.
3. Physics: each chunk starts at rest in place with its centroid, gets an
   outward velocity from the blast centre (force, lift, random spread angle,
   random speed jitter), a random spin, gravity and drag. Spin is applied in
   90-degree steps only (exact pixel rotation, nothing resampled). Chunks
   stop at the floor line (the source origin_y, i.e. the floor contact row of
   a bottom-centre sprite): they cannot go below it, bounce a little, then
   slide with floor friction and stop spinning.

Frame 0 is the intact sprite with its wounds (the crack frame); frame n is the
state after n * substeps simulation steps. Frames keep the source canvas,
optionally grown by "canvas": [w, h] around the bottom-centre origin so pieces
have room to fly. Output: <outdir>/<sprite>_gib_d0_f<n>.tex. --preview writes
the same frames as indexed PNGs plus <sprite>_gib_sheet.png (all frames in a
row, 1x).

Parameters (JSON; defaults in DEFAULTS):
  sprite        output name prefix (default: source file stem)
  source        source TEX path relative to assets/gen (used by build.d/50-gibs.sh)
  seed          random seed                       frames     frame count
  chunks        Voronoi chunk count K             substeps   simulation steps per frame
  force         outward speed, px/step            lift       extra upward speed, px/step
  jitter        speed randomisation, 0..1         spread     max direction change, degrees
  spin          max spin, degrees/step            gravity    px/step^2
  drag          velocity loss per step, 0..1      bounce     floor restitution, 0..1
  friction      horizontal speed kept per step on the floor, 0..1
  centre        blast centre as fractions [fx, fy] of the opaque bounding box
  wound_depth   wound width in px (0 = no wounds)  blood_ramp  palette ramp name
  wound_shade   Oklab lightness offset of wound colour (negative = darker)
  canvas        [w, h] output canvas (>= source), origin kept bottom-centre

The simulation runs in Python floats: this is an offline asset tool, not
gameplay logic (the engine only plays the baked frames). It uses only the
seeded random.Random and basic arithmetic plus math.cos/sin/hypot, so the
same parameters give byte-identical frames (ctest gibs_determinism).
"""
import argparse
import json
import math
import os
import random
import sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import palette as palmod  # noqa: E402

T = bast.TRANSPARENT

DEFAULTS = {
    "seed": 1,
    "frames": 8,
    "chunks": 8,
    "substeps": 2,
    "force": 2.0,
    "lift": 1.0,
    "jitter": 0.4,
    "spread": 25.0,
    "spin": 30.0,
    "gravity": 0.35,
    "drag": 0.04,
    "bounce": 0.25,
    "friction": 0.6,
    "centre": [0.5, 0.75],
    "wound_depth": 2,
    "blood_ramp": "red",
    "wound_shade": -0.08,
    "canvas": None,
}


class GibError(Exception):
    pass


def load_params(path):
    with open(path) as f:
        raw = json.load(f)
    p = dict(DEFAULTS)
    for k, v in raw.items():
        if k.startswith("_"):  # comments
            continue
        if k not in DEFAULTS and k not in ("sprite", "source"):
            raise GibError("%s: unknown parameter %r" % (path, k))
        p[k] = v
    if p["frames"] < 1 or p["chunks"] < 1 or p["substeps"] < 1:
        raise GibError("%s: frames, chunks and substeps must be >= 1" % path)
    if p["wound_depth"] < 0:
        raise GibError("%s: wound_depth must be >= 0" % path)
    return p


# ---------------------------------------------------------------- palette

def ramp_entries(ramp, gpl=None):
    """Indices of palette entries named <ramp>_NN, dark to light."""
    entries = palmod.load_gpl(gpl or palmod.DEFAULT_GPL)
    out = [i for i, (_, name) in enumerate(entries)
           if name.startswith(ramp + "_") and name[len(ramp) + 1:].isdigit()]
    if not out:
        raise GibError("no palette ramp named %r" % ramp)
    return out


class WoundColours:
    """Maps (source index, blend amount) -> palette index, via Oklab from libbast."""

    def __init__(self, ramp, shade, pal):
        self.ramp = ramp_entries(ramp)
        cols = pal.colors
        self.lab = [bast.srgb_to_oklab(c) for c in cols]
        self.shade = shade
        self.cache = {}

    def pick(self, src, a):
        key = (src, a)
        if key in self.cache:
            return self.cache[key]
        ls = self.lab[src]
        want_l = ls[0] + self.shade
        # ramp entry closest in lightness (ties: darker)
        tgt = min(self.ramp, key=lambda i: (abs(self.lab[i][0] - want_l), i))
        lt = self.lab[tgt]
        mix = tuple(ls[k] * (1 - a) + lt[k] * a for k in range(3))
        best = None
        for i in self.ramp + [src]:
            li = self.lab[i]
            d = sum((li[k] - mix[k]) ** 2 for k in range(3))
            if best is None or d < best[0] or (d == best[0] and i < best[1]):
                best = (d, i)
        self.cache[key] = best[1]
        return best[1]


# ---------------------------------------------------------------- stage 1

def opaque_pixels(tex):
    h = tex.height
    return [(i // h, i % h) for i, v in enumerate(tex.pixels) if v != T]


def pick_seeds(opaque, k, rng, candidates=10):
    """Best-candidate sampling: each new seed is the farthest (from existing
    seeds) of `candidates` random opaque pixels. Deterministic for a seeded rng."""
    k = min(k, len(opaque))
    seeds = [opaque[rng.randrange(len(opaque))]]
    while len(seeds) < k:
        best = None
        for _ in range(candidates):
            c = opaque[rng.randrange(len(opaque))]
            if c in seeds:
                continue
            d = min((c[0] - s[0]) ** 2 + (c[1] - s[1]) ** 2 for s in seeds)
            if best is None or d > best[0]:
                best = (d, c)
        if best is None:  # every candidate already a seed: take the first free pixel
            best = (0, next(p for p in opaque if p not in seeds))
        seeds.append(best[1])
    return seeds


def voronoi(opaque, seeds):
    """{(x, y): chunk id} for every opaque pixel."""
    cell = {}
    for (x, y) in opaque:
        best = None
        for i, (sx, sy) in enumerate(seeds):
            d = (x - sx) ** 2 + (y - sy) ** 2
            if best is None or d < best[0]:
                best = (d, i)
        cell[(x, y)] = best[1]
    return cell


# ---------------------------------------------------------------- stage 2

N4 = ((1, 0), (-1, 0), (0, 1), (0, -1))


def wound_depths(cell):
    """{(x, y): depth} from the chunk boundaries (BFS inside each chunk).
    Only the lower-id side of a boundary is depth 0, so a crack between two
    chunks is 1 px wide in the intact frame. Pixels of a chunk that has no
    higher-id neighbour are absent."""
    depth = {}
    q = deque()
    for (x, y), c in cell.items():
        for dx, dy in N4:
            o = cell.get((x + dx, y + dy))
            if o is not None and o > c:
                depth[(x, y)] = 0
                q.append((x, y))
                break
    while q:
        x, y = q.popleft()
        c = cell[(x, y)]
        for dx, dy in N4:
            n = (x + dx, y + dy)
            if cell.get(n) == c and n not in depth:
                depth[n] = depth[(x, y)] + 1
                q.append(n)
    return depth


def bleed(tex, cell, depth, wound_depth, colours):
    """{(x, y): index} after the wound blend."""
    out = {}
    for (x, y) in cell:
        v = tex.get(x, y)
        d = depth.get((x, y))
        if colours is not None and d is not None and d < wound_depth:
            a = 1.0 - d / wound_depth
            v = colours.pick(v, a)
        out[(x, y)] = v
    return out


# ---------------------------------------------------------------- stage 3

def rotate(dx, dy, q):
    """Rotate an integer offset by q quarter turns (exact)."""
    q &= 3
    if q == 0:
        return dx, dy
    if q == 1:
        return -dy, dx
    if q == 2:
        return -dx, -dy
    return dy, -dx


class Chunk:
    def __init__(self, cid, pixels, colours):
        self.id = cid
        n = len(pixels)
        cx = sum(p[0] for p in pixels) / n
        cy = sum(p[1] for p in pixels) / n
        self.centroid = (cx, cy)
        # integer pivot; pixels stored as offsets from it
        self.px, self.py = round(cx), round(cy)
        self.offsets = [(x - self.px, y - self.py, colours[(x, y)]) for (x, y) in pixels]
        self.x, self.y = float(self.px), float(self.py)
        self.vx = self.vy = 0.0
        self.angle = 0.0
        self.spin = 0.0
        self.on_floor = False

    def quarter(self):
        return int(math.floor(self.angle / 90.0 + 0.5)) & 3

    def max_dy(self, q):
        return max(rotate(dx, dy, q)[1] for dx, dy, _ in self.offsets)


def launch(chunks, opaque, p, rng):
    xs = [x for x, _ in opaque]
    ys = [y for _, y in opaque]
    ex = min(xs) + (max(xs) - min(xs)) * p["centre"][0]
    ey = min(ys) + (max(ys) - min(ys)) * p["centre"][1]
    for c in chunks:
        dx, dy = c.centroid[0] - ex, c.centroid[1] - ey
        n = math.hypot(dx, dy)
        if n < 1e-9:
            dx, dy, n = 0.0, -1.0, 1.0
        dx, dy = dx / n, dy / n
        ang = math.radians(rng.uniform(-p["spread"], p["spread"]))
        ca, sa = math.cos(ang), math.sin(ang)
        dx, dy = dx * ca - dy * sa, dx * sa + dy * ca
        speed = p["force"] * (1.0 + p["jitter"] * rng.uniform(-1.0, 1.0))
        c.vx = dx * speed
        c.vy = dy * speed - p["lift"]
        c.spin = rng.uniform(-p["spin"], p["spin"])


def step(chunks, p, floor):
    keep = 1.0 - p["drag"]
    for c in chunks:
        c.vy += p["gravity"]
        c.vx *= keep
        c.vy *= keep
        c.x += c.vx
        c.y += c.vy
        c.angle += c.spin
        lim = floor - 1 - c.max_dy(c.quarter())
        if c.y >= lim:
            c.y = float(lim)
            if c.vy > 0:
                c.vy = -c.vy * p["bounce"]
                if abs(c.vy) < 0.5:
                    c.vy = 0.0
            c.vx *= p["friction"]
            c.spin = 0.0
            c.on_floor = True


def render(chunks, w, h, floor):
    """Column-major pixel bytes; chunks drawn in id order, later ones on top.
    Integer positions are clamped so no pixel lands on or below the floor row."""
    px = bytearray([T]) * (w * h)
    for c in chunks:
        q = c.quarter()
        ix = int(math.floor(c.x + 0.5))
        iy = min(int(math.floor(c.y + 0.5)), floor - 1 - c.max_dy(q))
        for dx, dy, v in c.offsets:
            rx, ry = rotate(dx, dy, q)
            x, y = ix + rx, iy + ry
            if 0 <= x < w and 0 <= y < h:
                px[x * h + y] = v
    return px


# ---------------------------------------------------------------- driver

def generate(tex, p, pal=None):
    """Source bast.Tex + params -> list of bast.Tex frames."""
    pal = pal or bast.load_palette()
    opaque = opaque_pixels(tex)
    if not opaque:
        raise GibError("source has no opaque pixel")
    rng = random.Random(p["seed"])

    # grow the canvas around the bottom-centre origin
    sw, sh = tex.width, tex.height
    ox, oy = tex.origin
    if (ox, oy) == (0, 0):
        ox, oy = sw // 2, sh  # unset origin: treat as a bottom-centre world sprite
    if p["canvas"]:
        cw, ch = int(p["canvas"][0]), int(p["canvas"][1])
        if cw < sw or ch < sh:
            raise GibError("canvas %dx%d smaller than source %dx%d" % (cw, ch, sw, sh))
        offx, offy = cw // 2 - ox, ch - oy
        if offx < 0 or offx + sw > cw or offy < 0:
            raise GibError("canvas %dx%d cannot hold the source around its origin" % (cw, ch))
    else:
        cw, ch, offx, offy = sw, sh, 0, 0
    nox, noy = ox + offx, oy + offy
    floor = noy
    if any(y + offy >= floor for _, y in opaque):
        raise GibError("source has pixels below its floor line (origin_y %d)" % oy)

    seeds = pick_seeds(opaque, p["chunks"], rng)
    cell = voronoi(opaque, seeds)
    depth = wound_depths(cell)
    colours = None
    if p["wound_depth"] > 0:
        colours = WoundColours(p["blood_ramp"], p["wound_shade"], pal)
    bled = bleed(tex, cell, depth, p["wound_depth"], colours)

    members = [[] for _ in seeds]
    for (x, y), c in sorted(cell.items()):
        members[c].append((x + offx, y + offy))
    shifted = {(x + offx, y + offy): v for (x, y), v in bled.items()}
    chunks = [Chunk(i, m, shifted) for i, m in enumerate(members) if m]
    launch(chunks, opaque, p, rng)

    flags = bast.TEX_HAS_TRANSPARENCY | (tex.flags & bast.TEX_FULLBRIGHT)
    frames = []
    for f in range(p["frames"]):
        if f > 0:
            for _ in range(p["substeps"]):
                step(chunks, p, floor)
        frames.append(bast.Tex(cw, ch, render(chunks, cw, ch, floor), (nox, noy), flags))
    return frames


def sheet(frames, pal):
    from PIL import Image
    w, h = frames[0].width, frames[0].height
    img = Image.new("P", (w * len(frames), h), T)
    for i, t in enumerate(frames):
        img.paste(t.to_image(pal), (i * w, 0))
    img.putpalette([c for rgb in pal.colors for c in rgb])
    img.info["transparency"] = T
    return img


def main(argv=None):
    ap = argparse.ArgumentParser(description="Voronoi gib animation generator (GAME.md 6.2)")
    ap.add_argument("params", help="parameter JSON")
    ap.add_argument("-i", "--input", required=True, help="source TEX")
    ap.add_argument("-o", "--out", required=True, help="output directory")
    ap.add_argument("--preview", help="directory for indexed PNG previews and a sheet")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)
    try:
        p = load_params(args.params)
        name = p.get("sprite") or os.path.splitext(os.path.basename(args.input))[0]
        pal = bast.load_palette()
        src = bast.Tex.load(args.input)
        frames = generate(src, p, pal)
        os.makedirs(args.out, exist_ok=True)
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
        for f, t in enumerate(frames):
            stem = "%s_gib_d0_f%d" % (name, f)
            t.save(os.path.join(args.out, stem + ".tex"))
            if args.preview:
                t.to_image(pal).save(os.path.join(args.preview, stem + ".png"),
                                     transparency=T)
        if args.preview:
            sheet(frames, pal).save(os.path.join(args.preview, name + "_gib_sheet.png"),
                                    transparency=T)
        if not args.quiet:
            n0 = sum(1 for v in src.pixels if v != T)
            counts = [sum(1 for v in t.pixels if v != T) for t in frames]
            print("gibs: %s -> %s/%s_gib_d0_f0..%d.tex (%dx%d, %d chunks, opaque %d -> %s)"
                  % (args.input, args.out, name, len(frames) - 1, frames[0].width,
                     frames[0].height, min(p["chunks"], n0), n0,
                     " ".join(str(c) for c in counts)))
    except (OSError, ValueError, GibError, bast.BastError) as e:
        print("gibs: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
