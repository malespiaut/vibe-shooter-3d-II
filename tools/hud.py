#!/usr/bin/env python3
"""Build the status bar panel and HUD icons (docs/design/hud.md, Status bar).

Sources (assets/src/hud/):
  legend.txt      shared character -> palette entry name legend
  *.pix           hand-drawn icons as explicit pixel grids (format below)
  statusbar.json  panel layout: hud.md boxes in panel-local coordinates

.pix format:
  # comment
  name icon_fish
  size 12 12
  shadow sand_01        HUD drop shadow colour (1 px, bottom-right); "none" = no shadow
  = X steel_03          add/override a legend character for this file
  grid
  <rows: one character per pixel, '.' transparent; short rows pad with '.'>

The drop shadow is baked in here (art-bible.md: HUD art uses a 1 px dark
shadow bottom-right, no outlines): every transparent pixel whose up-left
neighbour is opaque art becomes the shadow colour.

Output: <out>/<name>.tex (libbast) and <out>/<name>.png (indexed, game palette).

Usage: python3 tools/hud.py [--src DIR] [-o OUT] [--clean] [--mockup FILE.png] [--sheet FILE.png]
"""
import argparse
import glob
import json
import os
import shutil
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import palette  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(ROOT, "assets", "src", "hud")
DEFAULT_OUT = os.path.join(ROOT, "assets", "gen", "hud")
T = 255


class HudError(Exception):
    pass


class Pal:
    def __init__(self):
        self.entries = palette.load_gpl()
        self.flags = palette.flags(self.entries)
        self.index = {n: i for i, (_, n) in enumerate(self.entries)}

    def __call__(self, name):
        i = self.index.get(name)
        if i is None:
            raise HudError("unknown palette entry %r" % name)
        if self.flags[i] & palette.FLAG_RESERVED:
            raise HudError("palette entry %r is reserved" % name)
        if name.startswith("shadow_"):
            raise HudError("%r: assets do not paint with shadow_* (art-bible)" % name)
        return i


def load_legend(path, pal):
    leg = {}
    with open(path) as f:
        for no, line in enumerate(f, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = s.split()
            if len(parts) != 2 or len(parts[0]) != 1:
                raise HudError("%s:%d: need '<char> <entry>'" % (path, no))
            leg[parts[0]] = pal(parts[1])
    return leg


class Image8:
    """Row-major index image."""

    def __init__(self, w, h, fill=T):
        self.w, self.h = w, h
        self.px = bytearray([fill]) * (w * h)

    def get(self, x, y):
        return self.px[y * self.w + x]

    def set(self, x, y, i):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = i

    def blit(self, src, ox, oy):
        for y in range(src.h):
            for x in range(src.w):
                i = src.get(x, y)
                if i != T:
                    self.set(ox + x, oy + y, i)

    def to_pil(self, pal):
        img = Image.frombytes("P", (self.w, self.h), bytes(self.px))
        img.putpalette([c for rgb, _ in pal.entries for c in rgb])
        img.info["transparency"] = T
        return img

    def to_tex(self):
        h = self.h
        col = bytearray(self.w * h)
        for y in range(h):
            col[y::h] = self.px[y * self.w:(y + 1) * self.w]
        flags = bast.TEX_HAS_TRANSPARENCY if T in self.px else 0
        return bast.Tex(self.w, h, col, (0, 0), flags)


def load_pix(path, legend, pal):
    leg = dict(legend)
    name = size = None
    shadow = pal("sand_01")
    rows = None
    with open(path) as f:
        lines = f.read().splitlines()
    for no, line in enumerate(lines, 1):
        if rows is not None:
            if line.strip() == "" and no == len(lines):
                continue
            rows.append((no, line.rstrip()))
            continue
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        parts = s.split()
        if parts[0] == "name":
            name = parts[1]
        elif parts[0] == "size":
            size = (int(parts[1]), int(parts[2]))
        elif parts[0] == "shadow":
            shadow = None if parts[1] == "none" else pal(parts[1])
        elif parts[0] == "=":
            leg[parts[1]] = pal(parts[2])
        elif parts[0] == "grid":
            rows = []
        else:
            raise HudError("%s:%d: unknown directive %r" % (path, no, parts[0]))
    if not name or not size or rows is None:
        raise HudError("%s: needs name, size and grid" % path)
    w, h = size
    if len(rows) > h:
        raise HudError("%s: %d rows, size says %d" % (path, len(rows), h))
    img = Image8(w, h)
    art = [[False] * w for _ in range(h)]
    for y, (no, row) in enumerate(rows):
        if len(row) > w:
            raise HudError("%s:%d: row is %d wide, size says %d" % (path, no, len(row), w))
        for x, ch in enumerate(row):
            if ch not in leg:
                raise HudError("%s:%d: character %r not in legend" % (path, no, ch))
            img.set(x, y, leg[ch])
            art[y][x] = leg[ch] != T
    if shadow is not None:
        for y in range(1, h):
            for x in range(1, w):
                if not art[y][x] and art[y - 1][x - 1]:
                    img.set(x, y, shadow)
    return name, img


# ---------------------------------------------------------------- panel

def build_panel(layout, pal, legend_pix):
    w, h = layout["size"]
    img = Image8(w, h)
    n = pal

    # Sandstone slab: sand_10 with hand-placed 2x2 checker grain patches
    # (adjacent entries sand_10/sand_11 and sand_09/sand_10, art-bible dithering rule).
    for y in range(h):
        for x in range(w):
            img.set(x, y, n("sand_10"))
    for x0, y0, x1, y1, lo, hi in layout["grain"]:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                img.set(x, y, n(hi) if (x // 1 + y // 1) % 2 == 0 else n(lo))
    # Stone joints between the slab blocks (dark line, lit line to its right).
    for x in layout["joints"]:
        for y in range(2, h - 2):
            img.set(x, y, n("sand_06"))
            img.set(x + 1, y, n("sand_12"))
    # Slab bevel: lit top/left, shaded bottom/right.
    for x in range(w):
        img.set(x, 0, n("sand_14"))
        img.set(x, 1, n("sand_12"))
        img.set(x, h - 2, n("sand_07"))
        img.set(x, h - 1, n("sand_04"))
    for y in range(1, h - 1):
        img.set(0, y, n("sand_13"))
        img.set(w - 1, y, n("sand_06"))

    # Gold inlay frieze: zigzag (water) band in the spare stone.
    for x0, y0, x1, period in layout["friezes"]:
        half = period // 2
        for x in range(x0, x1 + 1):
            p = (x - x0) % period
            dy = p if p <= half else period - p
            img.set(x, y0 + dy, n("gold_09"))
            img.set(x, y0 + dy + 1, n("gold_05"))

    # Recessed boxes, each framed by a gold inlay line one pixel outside.
    for name, (x0, y0, x1, y1) in layout["recesses"].items():
        for y in range(y0 - 1, y1 + 2):
            for x in range(x0 - 1, x1 + 2):
                if x0 <= x <= x1 and y0 <= y <= y1:
                    continue
                if x <= x0 - 1 or y <= y0 - 1:
                    c = "gold_09"
                else:
                    c = "gold_05"
                if (x == x0 - 1 and y == y1 + 1) or (x == x1 + 1 and y == y0 - 1):
                    c = "gold_07"
                img.set(x, y, n(c))
    for name, (x0, y0, x1, y1) in layout["recesses"].items():
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if y == y0 or x == x0:
                    c = "sand_00"          # shadowed inner lip (light from top-left)
                elif y == y1 or x == x1:
                    c = "sand_05"          # lit inner lip
                else:
                    c = "sand_02"
                img.set(x, y, n(c))

    # Key slots: a faint sunken ankh-sized niche marks each of the three slots.
    for sx, sy in layout["key_slots"]:
        for y in range(sy, sy + 16):
            for x in range(sx, sx + 12):
                img.set(x, y, n("sand_01"))
        for x in range(sx, sx + 12):
            img.set(x, sy + 16, n("sand_04"))
        for y in range(sy, sy + 17):
            img.set(sx + 12, y, n("sand_04"))

    # Powerup bar groove (drawn over at runtime).
    x0, y0, x1, y1 = layout["groove"]
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            img.set(x, y, n("sand_01" if y == y0 else "sand_02"))
    for x in range(x0 - 1, x1 + 2):
        img.set(x, y0 - 1, n("sand_06"))
        img.set(x, y1 + 1, n("sand_13"))
    img.set(x0 - 1, y0, n("sand_06"))
    img.set(x0 - 1, y1, n("sand_06"))
    img.set(x1 + 1, y0, n("sand_13"))
    img.set(x1 + 1, y1, n("sand_13"))

    # Face cartouche: gold oval (superellipse) ring, dark interior.
    fx0, fy0, fx1, fy1 = layout["face"]
    cx2, cy2 = fx0 + fx1 + 1, fy0 + fy1 + 1       # doubled centre, integer
    rx2, ry2 = fx1 - fx0 + 1, fy1 - fy0 + 1       # doubled radii

    def inside(x, y, shrink):
        # ((2x+1-cx2)/(rx2-2s))^4 + ((2y+1-cy2)/(ry2-2s))^4 <= 1, in integers
        ax, ay = 2 * x + 1 - cx2, 2 * y + 1 - cy2
        rx, ry = rx2 - 2 * shrink, ry2 - 2 * shrink
        return (ax ** 4) * (ry ** 4) + (ay ** 4) * (rx ** 4) <= (rx ** 4) * (ry ** 4)

    ring = ["gold_10", "gold_08", "gold_06", "gold_04"]
    for y in range(fy0, fy1 + 1):
        for x in range(fx0, fx1 + 1):
            if not inside(x, y, 0):
                continue
            ax, ay = 2 * x + 1 - cx2, 2 * y + 1 - cy2
            # light from the top-left: facing = -(ax+ay)/(|ax|+|ay|), scaled to 0..3
            tot = abs(ax) + abs(ay) or 1
            s = (-(ax + ay) * 4) // tot           # -4..4
            tone = 0 if s >= 2 else 1 if s >= 0 else 2 if s >= -2 else 3
            if not inside(x, y, 1):
                img.set(x, y, n(ring[tone]))
            elif not inside(x, y, 2):
                img.set(x, y, n(ring[min(tone + 1, 3)]))
            elif not inside(x, y, 3):
                img.set(x, y, n("gold_02" if tone <= 1 else "gold_07"))
            else:
                img.set(x, y, n("sand_02"))

    # Dominion steel: straps bolted over the stone, then rivets.
    for x0, x1 in layout["straps"]:
        for y in range(h):
            for x in range(x0, x1 + 1):
                c = "steel_12" if x == x0 else "steel_04" if x == x1 else "steel_08"
                if y == 0:
                    c = "steel_13"
                elif y == h - 1:
                    c = "steel_03"
                img.set(x, y, n(c))
            img.set(x1 + 1, y, n("sand_05"))      # cast shadow on the stone
    for x0, y0, x1, y1 in layout["plates"]:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                c = "steel_07"
                if y == y0 or x == x0:
                    c = "steel_11"
                if y == y1 or x == x1:
                    c = "steel_04"
                img.set(x, y, n(c))
        for x in range(x0 + 1, x1 + 2):
            img.set(x, y1 + 1, n("sand_05"))
        for y in range(y0 + 1, y1 + 2):
            img.set(x1 + 1, y, n("sand_05"))
    rivet = legend_pix["_rivet"]
    for x, y in layout["rivets"]:
        img.blit(rivet, x, y)
    return img


# ---------------------------------------------------------------- build

def build(src, pal):
    legend = load_legend(os.path.join(src, "legend.txt"), pal)
    sprites = {}
    for path in sorted(glob.glob(os.path.join(src, "*.pix"))):
        name, img = load_pix(path, legend, pal)
        if name in sprites:
            raise HudError("%s: duplicate name %s" % (path, name))
        sprites[name] = img
    with open(os.path.join(src, "statusbar.json")) as f:
        layout = json.load(f)
    out = {"statusbar": build_panel(layout, pal, sprites)}
    for k, v in sprites.items():
        if not k.startswith("_"):
            out[k] = v
    return out, layout


def mockup(assets, layout, keys, weapon):
    """320x240: black 3D view, panel at y=208, icons at their hud.md positions."""
    m = Image8(320, 240, 0)
    by = 240 - layout["size"][1]
    m.blit(assets["statusbar"], 0, by)
    for name, (x, y) in layout["icons"].items():
        m.blit(assets[name], x, by + y)
    m.blit(assets[weapon], layout["weapon_icon"][0], by + layout["weapon_icon"][1])
    for (sx, sy), k in zip(layout["key_slots"], keys):
        m.blit(assets[k], sx, by + sy)
    return m


def sheet(assets, pal):
    names = [k for k in sorted(assets) if k != "statusbar"]
    cell_w, cell_h, cols = 52, 24, 6
    rows = (len(names) + cols - 1) // cols
    s = Image8(cols * cell_w + 4, rows * cell_h + 4, pal("sand_10"))
    for i, k in enumerate(names):
        a = assets[k]
        x = 4 + (i % cols) * cell_w
        y = 4 + (i // cols) * cell_h
        s.blit(a, x, y)
    return s


def main(argv=None):
    ap = argparse.ArgumentParser(description="status bar and HUD icons")
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("-o", "--out", default=DEFAULT_OUT)
    ap.add_argument("--mockup", action="append", default=[],
                    help="FILE.png[:key,key,key[:weapon]] 320x240 mock-up")
    ap.add_argument("--sheet", help="icon sheet PNG (1x)")
    ap.add_argument("--clean", action="store_true",
                    help="delete the output directory first (no stale files)")
    args = ap.parse_args(argv)
    try:
        pal = Pal()
        assets, layout = build(args.src, pal)
        if args.clean and os.path.isdir(args.out):
            shutil.rmtree(args.out)
        os.makedirs(args.out, exist_ok=True)
        for name, img in sorted(assets.items()):
            img.to_tex().save(os.path.join(args.out, name + ".tex"))
            img.to_pil(pal).save(os.path.join(args.out, name + ".png"),
                                 transparency=T)
        for spec in args.mockup:
            parts = spec.split(":")
            keys = parts[1].split(",") if len(parts) > 1 else \
                ["key_green", "key_blue", "key_violet"]
            weapon = parts[2] if len(parts) > 2 else "weapon_2"
            mockup(assets, layout, keys, weapon).to_pil(pal).save(parts[0], transparency=T)
        if args.sheet:
            sheet(assets, pal).to_pil(pal).save(args.sheet, transparency=T)
    except (OSError, ValueError, KeyError, HudError, bast.BastError,
            palette.PaletteError) as e:
        print("hud: %s" % e, file=sys.stderr)
        return 1
    print("hud: %d images in %s" % (len(assets), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
