#!/usr/bin/env python3
"""Build the hand-drawn pickup sprites (docs/design/items.md) from .pix sources.

Sources: assets/src/sprites/items/*.pix, one file per frame, in the .pix format
of tools/hud.py (name, size, "= <char> <entry>", grid), with "shadow none"
implied, plus one directive of its own:

  outline <fb_entry> <entry>   outline colour next to a fullbright pixel

World sprites take a 1 px selective outline instead of the HUD drop shadow
(art-bible.md, Outline rule): every transparent pixel directly below or
directly right of art (the edges facing away from the top-left light) becomes
the darkest entry (<ramp>_00) of the ramp of the art pixel it borders. The
pixel above wins over the one to the left. Fullbright pixels have no ramp, so
a file that has art in fb_X on such an edge must say "outline fb_X <entry>",
or "outline fb_X none": then fb_X casts no outline (a flame or ember edge
meets transparency directly, art-bible.md Outline rule, #190), and the
transparent pixel takes the outline of the other neighbour (left) if that
one is a ramp pixel.
Leave the last column and row of art free where an outline should go.

Checks (exit 1 on failure): at most 3 ramps plus fb_* per sprite, never
black or white, no shadow_* or reserved entries (tools/hud.py Pal), the name
follows <sprite>_<anim>_d0_f<n> (tech.md, Sprite naming), at least one
opaque pixel on the bottom row.

Output: <out>/<name>.tex with origin bottom-centre (width // 2, height), and
<out>/<name>.png (indexed preview). --sheet writes a 1x contact sheet: every
frame on mid-gray (top) and on a sandstone floor TEX (bottom).

Usage: python3 tools/items.py [--src DIR] [-o OUT] [--clean] [--sheet FILE.png --floor TEX]
"""
import argparse
import glob
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import hud  # noqa: E402
import palette  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(ROOT, "assets", "src", "sprites", "items")
DEFAULT_OUT = os.path.join(ROOT, "assets", "gen", "sprites", "items")
T = hud.T
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*_[a-z]+_d0_f[0-9]+$")
RAMP_RE = re.compile(r"^([a-z]+)_[0-9]{2}$")


def ramp_of(name):
    """'sand_07' -> 'sand'; fb_* -> None; anything else is an error."""
    if name.startswith("fb_"):
        return None
    m = RAMP_RE.match(name)
    if not m:
        raise hud.HudError("%r is not a ramp entry (no black/white on sprites)" % name)
    return m.group(1)


def load_item(path, pal):
    fb_out = {}

    def outline(parts):
        if len(parts) != 3 or not parts[1].startswith("fb_"):
            raise hud.HudError("%s: need 'outline fb_<x> <entry>'" % path)
        fb_out[pal(parts[1])] = None if parts[2] == "none" else pal(parts[2])

    name, img = hud.load_pix(path, {".": T}, pal,
                             extra={"outline": outline}, shadow=None)
    if not NAME_RE.match(name):
        raise hud.HudError("%s: name %r is not <sprite>_<anim>_d0_f<n>" % (path, name))
    names = [n for _, n in pal.entries]
    w, h = img.w, img.h
    art = [[img.get(x, y) != T for x in range(w)] for y in range(h)]

    def edge_colour(i):
        if i in fb_out:
            return fb_out[i]
        r = ramp_of(names[i])
        if r is None:
            raise hud.HudError("%s: %s pixel on an outline edge needs 'outline %s <entry>'"
                               % (path, names[i], names[i]))
        return pal(r + "_00")

    for y in range(h):
        for x in range(w):
            if art[y][x]:
                continue
            for nx, ny in ((x, y - 1), (x - 1, y)):
                if nx < 0 or ny < 0 or not art[ny][nx]:
                    continue
                c = edge_colour(img.get(nx, ny))
                if c is not None:
                    img.set(x, y, c)
                    break

    ramps = set()
    for i in set(img.px):
        if i == T:
            continue
        r = ramp_of(names[i])
        if r is not None:
            ramps.add(r)
    if len(ramps) > 3:
        raise hud.HudError("%s: %d ramps %s, at most 3 plus fb_*" % (path, len(ramps), sorted(ramps)))
    if all(img.get(x, h - 1) == T for x in range(w)):
        raise hud.HudError("%s: bottom row is empty (sprites stand on the floor)" % path)
    return name, img, sorted(ramps)


def to_tex(img):
    t = img.to_tex()
    return bast.Tex(t.width, t.height, t.pixels, (img.w // 2, img.h), t.flags)


def build(src, pal):
    out = {}
    for path in sorted(glob.glob(os.path.join(src, "*.pix"))):
        name, img, ramps = load_item(path, pal)
        if name in out:
            raise hud.HudError("%s: duplicate name %s" % (path, name))
        out[name] = (img, ramps)
    if not out:
        raise hud.HudError("no .pix files in %s" % src)
    return out


def sheet(items, pal, floor_path):
    """Two bands: mid-gray, then the floor texture tiled. Frames of one
    sprite sit side by side; sprites in a row, bottoms aligned."""
    groups = {}
    for name in sorted(items):
        groups.setdefault(name.rsplit("_idle_", 1)[0], []).append(items[name][0])
    order = list(groups)
    pad = 4
    cells = []
    x = pad
    for g in order:
        for img in groups[g]:
            cells.append((x, img))
            x += img.w + 2
        x += pad
    width = x
    band = max(img.h for _, img in cells) + 2 * pad
    s = hud.Image8(width, 2 * band, pal("gray_08"))
    if floor_path:
        ft = bast.Tex.load(floor_path)
        for y in range(band, 2 * band):
            for xx in range(width):
                s.set(xx, y, ft.pixels[(xx % ft.width) * ft.height + (y - band) % ft.height])
    for b in range(2):
        base = b * band + band - pad
        for cx, img in cells:
            s.blit(img, cx, base - img.h)
    return s


def main(argv=None):
    ap = argparse.ArgumentParser(description="hand-drawn pickup sprites")
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("-o", "--out", default=DEFAULT_OUT)
    ap.add_argument("--clean", action="store_true",
                    help="delete the output directory first (no stale files)")
    ap.add_argument("--sheet", help="contact sheet PNG (1x)")
    ap.add_argument("--floor", help="floor TEX for the sheet's lower band")
    args = ap.parse_args(argv)
    try:
        pal = hud.Pal()
        items = build(args.src, pal)
        if args.clean and os.path.isdir(args.out):
            shutil.rmtree(args.out)
        os.makedirs(args.out, exist_ok=True)
        for name, (img, ramps) in sorted(items.items()):
            to_tex(img).save(os.path.join(args.out, name + ".tex"))
            img.to_pil(pal).save(os.path.join(args.out, name + ".png"), transparency=T)
        if args.sheet:
            sheet(items, pal, args.floor).to_pil(pal).save(args.sheet)
    except (OSError, ValueError, KeyError, hud.HudError, bast.BastError,
            palette.PaletteError) as e:
        print("items: %s" % e, file=sys.stderr)
        return 1
    print("items: %d sprite frames in %s" % (len(items), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
