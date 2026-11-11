#!/usr/bin/env python3
"""Build the title screen (docs/design/hud.md Flow, Title) -> assets/gen/ui/title.tex.

Sources (assets/src/ui/):
  title.json  layout: sky and ground bands, pyramid geometry, positions, colours
  logo.txt    hand-drawn lettering grids ("set big" 22 rows, "set small" 10 rows)
  mau.pix     hand-drawn silhouette of Mau from behind ('#' body)

Everything is drawn in palette indices; nothing is quantized. The backdrop
(sky bands, stars, chrome pyramid, dunes) is generated from title.json; the
only dithering is a 2-row 2x2 checker between two adjacent entries of the same
ramp (art-bible.md). Light comes from the top-left: the pyramid's left face is
the lit one, the right face is in shade, and letters get their bevel highlight
on top-left edges.

"PRESS ANY KEY" is not baked in; the game blinks it in the menu font inside the
clear band y 200-220. --press draws it (menu font at y=204) into a separate
preview PNG to check the band, never into the TEX.

Usage: python3 tools/title.py [--src DIR] [-o OUT] [--press FILE.png]
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import hud  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(ROOT, "assets", "src", "ui")
DEFAULT_OUT = os.path.join(ROOT, "assets", "gen", "ui")
T = 255


class TitleError(Exception):
    pass


def ramp_of(pal, i):
    """('steel', 12) for steel_12."""
    name = pal.entries[i][1]
    base, _, nn = name.rpartition("_")
    return (base, int(nn)) if nn.isdigit() else (name, None)


def step(pal, name, d):
    """Entry d steps lighter (d > 0) or darker (d < 0) along name's ramp, clamped."""
    base, _, nn = name.rpartition("_")
    n = int(nn) + d
    while n > 0 and ("%s_%02d" % (base, n)) not in pal.index:
        n -= 1
    return pal("%s_%02d" % (base, max(n, 0)))


def bands(img, pal, spec, x0=0, x1=None):
    """Horizontal colour bands [[y, entry], ...] down to the bottom of img.
    Two adjacent entries of one ramp meet in a 2-row pixel checker."""
    x1 = img.w if x1 is None else x1
    ys = [y for y, _ in spec] + [img.h]
    for k, (y0, name) in enumerate(spec):
        c = pal(name)
        for y in range(y0, ys[k + 1]):
            for x in range(x0, x1):
                img.set(x, y, c)
        if k:
            prev = pal(spec[k - 1][1])
            (ra, na), (rb, nb) = ramp_of(pal, prev), ramp_of(pal, c)
            if ra == rb and na is not None and abs(na - nb) == 1:
                for y in (y0 - 1, y0):
                    for x in range(x0, x1):
                        if (x + y) & 1:
                            img.set(x, y, prev if y >= y0 else c)


def stars(img, pal, spec, keep):
    s = spec["seed"]
    cols = [pal(n) for n in spec["colours"]]
    placed = 0
    while placed < spec["count"]:
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        x = (s >> 8) % img.w
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        y = (s >> 8) % spec["y_max"]
        if not keep(x, y):
            continue
        img.set(x, y, cols[(s >> 4) % len(cols)])
        placed += 1


def lerp(a, b, num, den):
    return a + (b - a) * num // den if den else a


def pyramid(img, pal, p):
    ax, ay = p["apex"]
    by, lx, rx, mx = p["base_y"], p["left_x"], p["right_x"], p["ridge_x"]
    h = by - ay
    cap = p["cap_rows"]
    course = p["course"]
    lit = ["steel_15", "steel_14", "steel_13", "steel_12", "steel_11"]
    lit_low = ["steel_06", "steel_07", "steel_08", "steel_09", "steel_10", "steel_11"]
    shade = ["steel_10", "steel_09", "steel_08", "steel_07"]
    shade_low = ["steel_03", "steel_04", "steel_05", "steel_06"]
    for y in range(ay, by + 1):
        t = y - ay
        xl, xr, xm = lerp(ax, lx, t, h), lerp(ax, rx, t, h), lerp(ax, mx, t, h)
        for x in range(xl, xr + 1):
            left = x <= xm
            if t < cap:
                # Gold capstone: lit gold on the left, shaded gold on the right.
                if t == cap - 1:
                    c = "gold_03"
                elif left:
                    c = "gold_11" if x == xl or x == xm else ("gold_10" if t < cap // 2 else "gold_09")
                else:
                    c = "gold_06" if t < cap // 2 else "gold_05"
                img.set(x, y, pal(c))
                continue
            # Chrome: the face mirrors the dusk. The reflected horizon is a
            # line tilted up toward the outer edge (the face leans back).
            d = (xm - x) if left else (x - xm)
            hz = ay + h * 47 // 100 - d // 5
            if y < hz - 1:
                ramp = lit if left else shade
                c = ramp[min(len(ramp) - 1, (y - ay - cap) * len(ramp) // max(1, hz - 1 - ay - cap))]
            elif y < hz + 1:
                c = ("orange_11" if y == hz - 1 else "orange_09") if left else \
                    ("cyan_08" if y == hz - 1 else "cyan_05")
            else:
                ramp = lit_low if left else shade_low
                c = ramp[min(len(ramp) - 1, (y - hz - 1) * len(ramp) // max(1, by - hz))]
            ci = pal(c)
            if (by - y) % course == 0 and y < by and not (hz - 1 <= y <= hz):
                ci = step(pal, c, -2)  # course seam
            if x == xl:
                ci = pal("steel_15")  # lit outer edge (top-left light)
            elif x == xm:
                ci = pal("white") if t < h * 47 // 100 else pal("steel_13")  # ridge catches the light
            elif x == xr:
                ci = pal("steel_01")
            img.set(x, y, ci)
    # Apex glint (fullbright: a light source).
    for d in range(-4, 5):
        c = pal("fb_sun") if abs(d) <= 1 else pal("fb_yellow")
        if abs(d) <= 3:
            img.set(ax + d, ay, c)
        img.set(ax, ay + d, c)
    img.set(ax - 1, ay - 1, pal("fb_gold"))
    img.set(ax + 1, ay - 1, pal("fb_gold"))
    img.set(ax - 1, ay + 1, pal("fb_gold"))
    img.set(ax + 1, ay + 1, pal("fb_gold"))


def dune(img, pal, d, cx):
    """Foreground dune ridge: highest under Mau, falling gently to both sides."""
    fill, edge, lip = pal(d["fill"]), pal(d["edge"]), pal(d["lip"])
    for x in range(img.w):
        top = d["y"] + (x - cx) * (x - cx) // 4000
        for y in range(top, img.h):
            img.set(x, y, fill)
        img.set(x, top, edge)
        img.set(x, top + 1, lip)


def load_grid(path):
    meta, rows = {}, None
    for line in open(path).read().splitlines():
        if rows is not None:
            rows.append(line.rstrip())
            continue
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        parts = s.split()
        if parts[0] == "grid":
            rows = []
        else:
            meta[parts[0]] = parts[1:]
    while rows and not rows[-1]:
        rows.pop()
    if rows is None:
        raise TitleError("%s: no grid" % path)
    return meta, rows


def mau(img, pal, src, m):
    meta, rows = load_grid(os.path.join(src, m["pix"]))
    w, h = int(meta["size"][0]), int(meta["size"][1])
    if len(rows) != h or max(len(r) for r in rows) > w:
        raise TitleError("%s: grid is not %dx%d" % (m["pix"], w, h))
    on = lambda x, y: 0 <= y < h and 0 <= x < len(rows[y]) and rows[y][x] == "#"
    ox, oy = m["feet"][0] - w // 2, m["feet"][1] - h
    fill, edge = pal(m["fill"]), pal(m["edge"])
    for y in range(h):
        for x in range(len(rows[y])):
            ch = rows[y][x]
            if ch == ".":
                continue
            if ch != "#":
                raise TitleError("%s: unknown pixel %r" % (m["pix"], ch))
            lit = not on(x - 1, y) or not on(x, y - 1)
            img.set(ox + x, oy + y, edge if lit else fill)


def load_logo(path):
    sets, cur, glyph = {}, None, None
    for no, line in enumerate(open(path).read().splitlines(), 1):
        s = line.rstrip()
        if not s or s.startswith("# ") or s == "#":  # comments: "# text"; glyph rows have no spaces
            continue
        parts = s.split()
        if parts[0] == "set":
            cur = sets.setdefault(parts[1], {"h": int(parts[2]), "g": {}})
        elif parts[0] == "glyph":
            glyph = cur["g"].setdefault(parts[1], [])
        else:
            if set(s) - {"#", "."}:
                raise TitleError("%s:%d: bad glyph row %r" % (path, no, s))
            glyph.append(s)
    for name, st in sets.items():
        for ch, rows in st["g"].items():
            if len(rows) != st["h"]:
                raise TitleError("%s: glyph %s/%s has %d rows, want %d" % (path, name, ch, len(rows), st["h"]))
    return sets


# Row shading per finish: top-lit metal, a sharp reflected horizon just below
# the middle, then the warm ground reflection rising toward the bottom.
FINISH = {
    "chrome": ["steel_15", "steel_15", "steel_14", "steel_14", "steel_13", "steel_13", "steel_12",
               "steel_12", "steel_11", "steel_10", "steel_09", "steel_08", "indigo_03", "steel_03",
               "steel_04", "steel_05", "steel_06", "orange_06", "orange_07", "orange_08", "orange_09",
               "orange_10"],
    "gold": ["gold_11", "gold_11", "gold_10", "gold_10", "gold_10", "gold_09", "gold_09", "gold_08",
             "gold_08", "gold_07", "gold_07", "gold_06", "gold_01", "gold_02", "gold_03", "gold_04",
             "gold_05", "gold_06", "gold_07", "gold_08", "gold_09", "gold_10"],
    "red": ["red_15", "red_14", "red_13", "red_12", "red_11", "red_04", "red_06", "red_08", "red_10",
            "red_12"],
}
BEVEL = {"chrome": "white", "gold": "white", "red": "pink_09"}


def text_width(st, text, spacing, space):
    w = 0
    for ch in text:
        w += space if ch == " " else max(len(r) for r in st["g"][ch]) + spacing
    return w - spacing if text and text[-1] != " " else w


def lettering(img, pal, st, runs, y, spacing, space, outline):
    """runs: [(text, finish)], centred on the screen as one line."""
    total = sum(text_width(st, t, spacing, space) for t, _ in runs)
    x = (img.w - total) // 2
    mask = {}
    for text, finish in runs:
        shade = [pal(n) for n in FINISH[finish]]
        if len(shade) != st["h"]:
            raise TitleError("finish %s has %d rows for %d-row glyphs" % (finish, len(shade), st["h"]))
        bevel = pal(BEVEL[finish])
        for ch in text:
            if ch == " ":
                x += space
                continue
            rows = st["g"][ch]
            on = lambda gx, gy: 0 <= gy < len(rows) and 0 <= gx < len(rows[gy]) and rows[gy][gx] == "#"
            for gy, r in enumerate(rows):
                for gx, c in enumerate(r):
                    if c == "#":
                        top_left = not on(gx - 1, gy) or not on(gx, gy - 1)
                        mask[(x + gx, y + gy)] = bevel if top_left and gy < st["h"] // 2 else shade[gy]
            x += max(len(r) for r in rows) + spacing
    # 1 px outline all round plus a 1 px drop shadow bottom-right: never black.
    o = pal(outline)
    ring = set()
    for (px, py) in mask:
        for dx in (-1, 0, 1, 2):
            for dy in (-1, 0, 1, 2):
                if (dx == 2 or dy == 2) and (dx < 0 or dy < 0):
                    continue
                ring.add((px + dx, py + dy))
    for p in ring - set(mask):
        img.set(p[0], p[1], o)
    for (px, py), c in mask.items():
        img.set(px, py, c)


def build(src):
    pal = hud.Pal()
    with open(os.path.join(src, "title.json")) as f:
        L = json.load(f)
    W, H = L["size"]
    img = hud.Image8(W, H)
    bands(img, pal, L["sky"])
    logo = load_logo(os.path.join(src, "logo.txt"))
    lg, sub = L["logo"], L["subtitle"]
    sub_y1 = sub["y"] + logo["small"]["h"] + 2
    stars(img, pal, L["stars"], lambda x, y: not (lg["y"] - 3 <= y <= sub_y1))
    bands(img, pal, [[y, n] for y, n in L["ground"]])
    pyramid(img, pal, L["pyramid"])
    dune(img, pal, L["dune"], L["mau"]["feet"][0])
    mau(img, pal, src, L["mau"])
    lettering(img, pal, logo["big"], [tuple(r) for r in lg["lines"]], lg["y"], lg["spacing"], lg["space"],
              lg["outline"])
    lettering(img, pal, logo["small"], [(sub["text"], sub["ramp"])], sub["y"], sub["spacing"], sub["space"],
              lg["outline"])
    if T in img.px:
        raise TitleError("title has transparent pixels")
    return pal, img


def press_preview(pal, img, out):
    """Copy of the title with PRESS ANY KEY in the menu font at y=204 (evidence only)."""
    import fontsheet
    sheet = bast.Tex.load(os.path.join(ROOT, "assets", "gen", "fonts", "menu.tex"))
    spec = fontsheet.FONTS["menu"]
    cw, ch = spec["cell"]
    text = "PRESS ANY KEY"
    x = (img.w - len(text) * cw) // 2
    cp = hud.Image8(img.w, img.h)
    cp.px[:] = img.px
    for c in text:
        i = spec["chars"].find(c)
        sx, sy = (i % fontsheet.COLS) * cw, (i // fontsheet.COLS) * ch
        for yy in range(ch):
            for xx in range(cw):
                v = sheet.get(sx + xx, sy + yy)
                if v != T:
                    cp.set(x + xx, 204 + yy, v)
        x += cw
    cp.to_pil(pal).save(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="build the title screen")
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("-o", "--out", default=DEFAULT_OUT)
    ap.add_argument("--press", help="also write a preview PNG with PRESS ANY KEY at y=204")
    args = ap.parse_args(argv)
    try:
        pal, img = build(args.src)
    except (TitleError, hud.HudError, KeyError) as e:
        print("title: %s" % e, file=sys.stderr)
        return 1
    os.makedirs(args.out, exist_ok=True)
    img.to_tex().save(os.path.join(args.out, "title.tex"))
    img.to_pil(pal).save(os.path.join(args.out, "title.png"))
    if args.press:
        press_preview(pal, img, args.press)
    print("title: %s/title.tex (%dx%d, %d colours)" % (args.out, img.w, img.h, len(set(img.px))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
