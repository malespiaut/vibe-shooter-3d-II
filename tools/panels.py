#!/usr/bin/env python3
"""Build the full-screen text panels (docs/design/hud.md Flow, story.md):

  papyrus_panel  intro, story and ending text: a papyrus scroll between two
                 wooden rods on a sandstone wall. The 300x168 text box
                 (x 10-309, y 36-203; #102 decision) is clear papyrus.
  tally_panel    intermission tally: a sandstone stele with a gold-framed
                 header recess (level name) and a body recess for the
                 KILLS / ITEMS / SECRETS / TIME-PAR lines.

Sources (assets/src/ui/):
  papyrus_panel.json, tally_panel.json  layout and colours (palette names)
  panels/*.pix                          hand-drawn details (finial, clamp, eye),
                                        legend = assets/src/hud/legend.txt
Reuses tools/hud.py (palette names, .pix parser, rivet, index images).

Everything is palette indices; nothing is quantized. Light from the top-left
(lit top/left bevels, shadows bottom-right). Dithering only as a 2x2 checker
between two adjacent entries of one ramp, on large areas (art-bible.md).
Texture noise comes from a seeded LCG, so the output is identical everywhere.

Output: <out>/papyrus_panel.tex|png, <out>/tally_panel.tex|png.

Usage: python3 tools/panels.py [--src DIR] [-o OUT]
"""
import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import hud  # noqa: E402
import palette  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(ROOT, "assets", "src", "ui")
DEFAULT_OUT = os.path.join(ROOT, "assets", "gen", "ui")
HUD_SRC = os.path.join(ROOT, "assets", "src", "hud")
T = 255


class Lcg:
    """Deterministic 31-bit LCG (no dependence on Python's random module)."""

    def __init__(self, seed):
        self.s = seed & 0x7FFFFFFF

    def next(self, n):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.s >> 8) % n


def ramp(pal, name, i):
    return pal("%s_%02d" % (name, i))


def fill(img, x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            img.set(x, y, c)


def mirror(src, recolour=None):
    out = hud.Image8(src.w, src.h)
    for y in range(src.h):
        for x in range(src.w):
            i = src.get(src.w - 1 - x, y)
            out.set(x, y, recolour.get(i, i) if recolour and i != T else i)
    return out


# ---------------------------------------------------------------- shared parts

def stone_wall(img, pal, w):
    """Sandstone block wall: offset courses, lit top/left edge, dark joint."""
    r, base = w["ramp"], w["base"]
    bw, bh = w["block"]
    rng = Lcg(w["seed"])
    for y in range(img.h):
        for x in range(img.w):
            img.set(x, y, ramp(pal, r, base))
    for cy in range(0, img.h, bh):
        off = (cy // bh) % 2 * (bw // 2)
        for bx in range(-bw, img.w + bw, bw):
            x0 = bx + off
            tone = base + (rng.next(3) - 1)          # per-block tone, +-1
            fill(img, x0, cy, x0 + bw - 1, cy + bh - 1, ramp(pal, r, tone))
            # weathering: a few 2x2-checker pits with the next entry down
            for _ in range(rng.next(3)):
                px, py = x0 + 4 + rng.next(bw - 12), cy + 4 + rng.next(bh - 10)
                for yy in range(py, py + 4):
                    for xx in range(px, px + 6):
                        if (xx + yy) % 2 == 0:
                            img.set(xx, yy, ramp(pal, r, tone - 1))
            for x in range(x0, x0 + bw):
                img.set(x, cy, ramp(pal, r, w["joint"]))
                img.set(x, cy + 1, ramp(pal, r, w["lit"]))
                img.set(x, cy + bh - 1, ramp(pal, r, w["shade"]))
            for y in range(cy, cy + bh):
                img.set(x0, y, ramp(pal, r, w["joint"]))
                if y > cy:
                    img.set(x0 + 1, y, ramp(pal, r, w["lit"]))
                img.set(x0 + bw - 1, y, ramp(pal, r, w["shade"]))


def rivets(img, parts, spots):
    for x, y in spots:
        img.blit(parts["_rivet"], x, y)


# ---------------------------------------------------------------- papyrus

def build_papyrus(L, pal, parts):
    W, H = L["size"]
    img = hud.Image8(W, H)
    stone_wall(img, pal, L["wall"])

    s = L["sheet"]
    pr, base = s["ramp"], s["base"]
    x0, y0, x1, y1 = s["box"]
    rng = Lcg(s["seed"])
    # ragged left/right edges: per-row inset 0..2 that drifts slowly
    inset_l, inset_r = [], []
    a = b = 1
    for y in range(y0, y1 + 1):
        if rng.next(3) == 0:
            a = max(0, min(2, a + rng.next(3) - 1))
        if rng.next(3) == 0:
            b = max(0, min(2, b + rng.next(3) - 1))
        inset_l.append(a)
        inset_r.append(b)

    # vertical strips of pith, each one flat tone base or base+1 (adjacent)
    strips = []
    x = x0
    while x <= x1:
        sw = s["strip"][0] + rng.next(s["strip"][1] - s["strip"][0] + 1)
        strips.append((x, min(x + sw - 1, x1), base + rng.next(2)))
        x += sw
    for sx0, sx1, tone in strips:
        fill(img, sx0, y0, sx1, y1, ramp(pal, pr, tone))
        # broken seam on the strip's left: base-1, gaps where the pith overlaps
        y = y0
        while y <= y1:
            run = 6 + rng.next(30)
            if rng.next(2):
                for yy in range(y, min(y + run, y1 + 1)):
                    img.set(sx0, yy, ramp(pal, pr, base - 1))
            y += run
    # horizontal fibres of the cross layer: short faint dashes
    for _ in range(s["fibres"]):
        fx = x0 + rng.next(x1 - x0)
        fy = y0 + rng.next(y1 - y0)
        ln = 5 + rng.next(18)
        c = ramp(pal, pr, base - 1 if rng.next(3) else base + 2)
        for xx in range(fx, min(fx + ln, x1 + 1)):
            img.set(xx, fy, c)
    # aged edges: darker band along every edge, darkest at the rim
    for i, y in enumerate(range(y0, y1 + 1)):
        l, r = x0 + inset_l[i], x1 - inset_r[i]
        for xx in range(x0, l):
            img.set(xx, y, T)
        for xx in range(r + 1, x1 + 1):
            img.set(xx, y, T)
        for d, tone in enumerate(s["edge"]):
            if (d + y) % 3 or d == 0:
                img.set(l + d, y, ramp(pal, pr, tone))
                img.set(r - d, y, ramp(pal, pr, tone))
    # re-lay the wall under the torn edge, then the sheet's cast shadow
    wall = hud.Image8(W, H)
    stone_wall(wall, pal, L["wall"])
    for y in range(H):
        for x in range(W):
            if img.get(x, y) == T:
                img.set(x, y, wall.get(x, y))
    for i, y in enumerate(range(y0, y1 + 1)):
        r = x1 - inset_r[i]
        for d in (1, 2):
            img.set(r + d, y + 1, ramp(pal, L["wall"]["ramp"], L["wall"]["joint"]))

    # rods: wooden cylinders lit from the top, gold bands, finials, clamps
    rd = L["rods"]
    fin = parts["finial"]
    gold_dark = {pal("gold_%02d" % i): pal("gold_%02d" % max(2, i - 2)) for i in range(2, 12)}
    fin_r = mirror(fin, gold_dark)
    for ry in rd["y"]:
        rx0, rx1 = rd["x"]
        shades = rd["shades"]
        # shadow the rod throws on the sheet/wall (bottom-right)
        for x in range(rx0 + 2, rx1 + 2):
            for d in (0, 1):
                yy = ry + len(shades) + d
                img.set(x, yy, darker(pal, img.get(x, yy), 3 - d))
        for j, tone in enumerate(shades):
            for x in range(rx0, rx1 + 1):
                img.set(x, ry + j, ramp(pal, rd["ramp"], tone))
        # wood grain: long dark streaks along the rod
        rng2 = Lcg(rd["seed"] + ry)
        for _ in range(rd["grain"]):
            gx = rx0 + rng2.next(rx1 - rx0 - 20)
            gy = ry + 2 + rng2.next(len(shades) - 4)
            for xx in range(gx, gx + 8 + rng2.next(24)):
                img.set(xx, gy, ramp(pal, rd["ramp"], shades[gy - ry] - 2))
        for bx in rd["bands"]:
            for j in range(len(shades)):
                g = rd["band_shades"][j]
                img.set(bx, ry + j, ramp(pal, "gold", g))
                img.set(bx + 1, ry + j, ramp(pal, "gold", max(2, g - 2)))
        fy = ry + len(shades) // 2 - fin.h // 2
        img.blit(fin, rx0 - fin.w + 2, fy)
        img.blit(fin_r, rx1 - 1, fy)
        for cx in rd["clamps"]:
            img.blit(parts["clamp"], cx, ry - 3)
    rivets(img, parts, L["rivets"])
    return img


def darker(pal, idx, n):
    """Same ramp, n entries darker (clamped at _01: _00 is the outline)."""
    if idx == T:
        return idx
    r, i = pal.entries[idx][1].rsplit("_", 1)
    return ramp(pal, r, max(1, int(i) - n))


# ---------------------------------------------------------------- tally

def recess(img, pal, box, r):
    """Sunken box framed by a gold inlay line, like the status bar recesses."""
    x0, y0, x1, y1 = box
    for y in range(y0 - 1, y1 + 2):
        for x in range(x0 - 1, x1 + 2):
            if x0 <= x <= x1 and y0 <= y <= y1:
                if y == y0 or x == x0:
                    c = ramp(pal, r["ramp"], r["shadow"])
                elif y == y1 or x == x1:
                    c = ramp(pal, r["ramp"], r["lip"])
                else:
                    c = ramp(pal, r["ramp"], r["fill"])
            elif x == x0 - 1 or y == y0 - 1:
                c = pal("gold_09")
            else:
                c = pal("gold_05")
            img.set(x, y, c)
    img.set(x0 - 1, y1 + 1, pal("gold_07"))
    img.set(x1 + 1, y0 - 1, pal("gold_07"))


def frieze(img, pal, x0, x1, y0, period):
    half = period // 2
    for x in range(x0, x1 + 1):
        p = (x - x0) % period
        dy = p if p <= half else period - p
        img.set(x, y0 + dy, pal("gold_09"))
        img.set(x, y0 + dy + 1, pal("gold_05"))


def build_tally(L, pal, parts):
    W, H = L["size"]
    img = hud.Image8(W, H)
    stone_wall(img, pal, L["wall"])
    st = L["stele"]
    r = st["ramp"]
    x0, y0, x1, y1 = st["box"]
    # stele: slab with 2x2-checker grain patches and a bevel
    fill(img, x0, y0, x1, y1, ramp(pal, r, st["base"]))
    rng = Lcg(st["seed"])
    for _ in range(st["grain"]):
        gx = x0 + 4 + rng.next(x1 - x0 - 40)
        gy = y0 + 4 + rng.next(y1 - y0 - 20)
        gw, gh = 8 + rng.next(30), 2 + rng.next(6)
        lo = st["base"] - 1 + rng.next(2)
        for y in range(gy, gy + gh):
            for x in range(gx, gx + gw):
                if (x + y) % 2 == 0:
                    img.set(x, y, ramp(pal, r, lo + 1))
                else:
                    img.set(x, y, ramp(pal, r, lo))
    for x in range(x0, x1 + 1):
        img.set(x, y0, ramp(pal, r, 14))
        img.set(x, y0 + 1, ramp(pal, r, 12))
        img.set(x, y1 - 1, ramp(pal, r, 7))
        img.set(x, y1, ramp(pal, r, 4))
    for y in range(y0 + 1, y1):
        img.set(x0, y, ramp(pal, r, 13))
        img.set(x0 + 1, y, ramp(pal, r, 12))
        img.set(x1 - 1, y, ramp(pal, r, 7))
        img.set(x1, y, ramp(pal, r, 6))
    # cast shadow of the stele on the wall (bottom-right)
    wr = L["wall"]["ramp"]
    for x in range(x0 + 2, x1 + 3):
        img.set(x, y1 + 1, ramp(pal, wr, L["wall"]["joint"]))
        img.set(x, y1 + 2, ramp(pal, wr, L["wall"]["joint"]))
    for y in range(y0 + 2, y1 + 3):
        img.set(x1 + 1, y, ramp(pal, wr, L["wall"]["joint"]))
        img.set(x1 + 2, y, ramp(pal, wr, L["wall"]["joint"]))
    # course joints across the slab (between the recesses)
    for jy in st["joints"]:
        for x in range(x0 + 2, x1 - 1):
            img.set(x, jy, ramp(pal, r, 6))
            img.set(x, jy + 1, ramp(pal, r, 12))
    for f in L["friezes"]:
        frieze(img, pal, f[0], f[1], f[2], f[3])
    for name in ("header", "body"):
        recess(img, pal, L[name], L["recess"])
    for ex, ey, flip in L["eyes"]:
        e = parts["eye"]
        img.blit(mirror(e) if flip else e, ex, ey)
    # Dominion steel: straps over the slab ends, then rivets
    for sx0, sx1 in L["straps"]:
        for y in range(y0, y1 + 1):
            for x in range(sx0, sx1 + 1):
                c = "steel_12" if x == sx0 else "steel_04" if x == sx1 else "steel_08"
                if y == y0:
                    c = "steel_13"
                elif y == y1:
                    c = "steel_03"
                img.set(x, y, pal(c))
            img.set(sx1 + 1, y, ramp(pal, r, 6))
    rivets(img, parts, L["rivets"])
    return img


# ---------------------------------------------------------------- main

def load_parts(src, pal):
    legend = hud.load_legend(os.path.join(HUD_SRC, "legend.txt"), pal)
    parts = {}
    for path in [os.path.join(HUD_SRC, "rivet.pix")] + \
            sorted(glob.glob(os.path.join(src, "panels", "*.pix"))):
        name, img = hud.load_pix(path, legend, pal)
        parts[name] = img
    return parts


def build(src, pal):
    parts = load_parts(src, pal)
    out = {}
    with open(os.path.join(src, "papyrus_panel.json")) as f:
        out["papyrus_panel"] = build_papyrus(json.load(f), pal, parts)
    with open(os.path.join(src, "tally_panel.json")) as f:
        out["tally_panel"] = build_tally(json.load(f), pal, parts)
    for k, v in out.items():
        if T in v.px:
            raise hud.HudError("%s: transparent pixel left in a full-screen panel" % k)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="papyrus and tally panels")
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("-o", "--out", default=DEFAULT_OUT)
    args = ap.parse_args(argv)
    try:
        pal = hud.Pal()
        out = build(args.src, pal)
        os.makedirs(args.out, exist_ok=True)
        for name, img in sorted(out.items()):
            img.to_tex().save(os.path.join(args.out, name + ".tex"))
            img.to_pil(pal).save(os.path.join(args.out, name + ".png"))
    except (OSError, ValueError, KeyError, hud.HudError, bast.BastError,
            palette.PaletteError) as e:
        print("panels: %s" % e, file=sys.stderr)
        return 1
    print("panels: %d images in %s" % (len(out), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
