#!/usr/bin/env python3
"""Paint the level skies (art-bible.md Sizes, Sky) -> assets/gen/textures/sky_*.tex.

    python3 tools/skygen.py assets/src/sky/*.json -o assets/gen/textures [--preview DIR]

One JSON file per sky (assets/src/sky/<name>.json -> <out>/<name>.tex). A sky
is 320x104 px covering 90 degrees of view, repeated 4x per turn, so every
element is painted with x wrapped modulo 320: column 319 joins column 0 with no
seam. The bottom row (103) is the horizon.

The sky is painted in RGB using palette colours only. Then it goes through
tools/quantize.py (libbast nearest, no dithering), and the result is checked to
round-trip exactly. Painting rules (art-bible.md):
- Gradients are flat bands. Two adjacent entries of one ramp meet in a 2-row
  2x2 checker, and other joins are hard edges (title.bands).
- Light comes from the top-left: clouds get their highlight on top and left
  edges and shade on bottom and right, and pyramids and obelisks have a lit left
  face.
- The sun core is `white`, not `fb_sun`. The libbast quantizer never picks
  fullbright entries, and the renderer never shades the sky, so both look the
  same on screen.

JSON keys, drawn in this order (coordinates in sky pixels, colours are palette
entry names, all x wrap):
  bands   [[y, entry], ...]            top to bottom
  stars   {seed, count, y_max, colours} (title.stars)
  sun     {x, y, rings: [[r, entry], ...]}   largest ring first, core last
  clouds  [{fill, light, shade, puffs: [[cx, cy, rx, ry], ...]}, ...]
  layers  [{fill, lit, ground, items: [...]}, ...]   far layer first
     ground: rows ground..103 filled with `fill`
     item types (each may override fill/lit):
       pyramid  {x, top, slope: [dx, dy], cap, cap_fill, cap_lit, courses, flat}
                (flat: rows above it not drawn, an unfinished pyramid)
       obelisk  {x, top, w}
       block    {x, w, top, merlon: [w, gap]}
       crane    {x, top, mast_w, jib: [back, front], trolley, hook_y, weight: [w, h]}
       scaffold {x, w, top, bay}
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hud  # noqa: E402
import quantize  # noqa: E402
import title  # noqa: E402

W, H = 320, 104
HORIZON = H - 1


class SkyError(Exception):
    pass


class Sky(hud.Image8):
    """Index image whose x wraps (the sky repeats horizontally)."""

    def __init__(self, fill, shift=0):
        super().__init__(W, H, fill)
        self.shift = shift  # even: painted x lands in column (x + shift) % W

    def set(self, x, y, i):
        if 0 <= y < self.h:
            self.px[y * self.w + (x + self.shift) % self.w] = i

    def get(self, x, y):
        return self.px[y * self.w + (x + self.shift) % self.w]


def wrapdx(x, cx):
    """Signed horizontal distance from cx to x, shortest way around."""
    d = (x - cx) % W
    return d - W if d > W // 2 else d


def adjacent(pal, a, b):
    (ra, na), (rb, nb) = title.ramp_of(pal, a), title.ramp_of(pal, b)
    return ra == rb and na is not None and nb is not None and abs(na - nb) == 1


def sun(img, pal, spec):
    cx, cy = spec["x"], spec["y"]
    rings = [(r, pal(n)) for r, n in spec["rings"]]
    rmax = rings[0][0]
    for y in range(cy - rmax - 1, cy + rmax + 2):
        for x in range(cx - rmax - 1, cx + rmax + 2):
            dx, dy = x - cx, y - cy
            d2 = dx * dx + dy * dy
            k = None
            for j, (r, _) in enumerate(rings):
                if d2 <= r * r + r:
                    k = j
            if k is None:
                continue
            c = rings[k][1]
            # A ring meets the next one inside it in a checker when the two
            # entries are neighbours on one ramp.
            if k + 1 < len(rings):
                r1, c1 = rings[k + 1]
                if d2 <= (r1 + 1) * (r1 + 1) + r1 + 1 and adjacent(pal, c, c1) and (x + y) & 1:
                    c = c1
            img.set(x, y, c)


def clouds(img, pal, specs):
    for spec in specs:
        fill, light, shade = pal(spec["fill"]), pal(spec["light"]), pal(spec["shade"])
        puffs = spec["puffs"]

        def in_puff(x, y):
            for cx, cy, rx, ry in puffs:
                dx, dy = wrapdx(x, cx), y - cy
                if (dx * ry) ** 2 + (dy * rx) ** 2 <= (rx * ry) ** 2:
                    return True
            return False

        ys = [cy for _, cy, _, ry in puffs for cy in (cy - ry, cy + ry)]
        y0, y1 = max(0, min(ys)), min(H - 1, max(ys))
        raw = {(x, y) for y in range(y0, y1 + 1) for x in range(W) if in_puff(x, y)}
        # Drop 1 px ticks (an ellipse's lone top/bottom pixel): a cloud pixel
        # needs a horizontal neighbour.
        mask = {(x, y) for x, y in raw
                if ((x - 1) % W, y) in raw or ((x + 1) % W, y) in raw}

        def inside(x, y):
            return (x % W, y) in mask

        for y in range(y0, y1 + 1):
            for x in range(W):
                if not inside(x, y):
                    continue
                if not inside(x, y - 1) or not inside(x - 1, y):
                    c = light
                elif not inside(x, y + 1) or not inside(x + 1, y):
                    c = shade
                elif not inside(x, y - 2) and adjacent(pal, light, fill) and (x + y) & 1:
                    c = light
                else:
                    c = fill
                img.set(x, y, c)


def line(img, x0, y0, x1, y1, c):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        img.set(x0, y0, c)
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def rect(img, x, y, w, h, c):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            img.set(xx, yy, c)


def pyramid(img, pal, it, fill, lit):
    cx, top = it["x"], it["top"]
    sx, sy = it.get("slope", [4, 3])
    cap = it.get("cap", 0)
    cap_fill = pal(it["cap_fill"]) if cap else None
    cap_lit = pal(it["cap_lit"]) if cap else None
    courses = it.get("courses", 0)
    dark = title.step(pal, it["fill_name"], -1) if courses else None
    lit_dark = title.step(pal, it["lit_name"], -1) if courses else None
    for y in range(max(top, it.get("flat", 0)), H):
        half = (y - top) * sx // sy
        for x in range(cx - half, cx + half + 1):
            left = x < cx or (x == cx and y == top)
            if y - top < cap:
                c = cap_lit if left else cap_fill
            elif courses and (y - top - cap) % courses == courses - 1:
                c = lit_dark if left else dark
            else:
                c = lit if left else fill
            img.set(x, y, c)


def obelisk(img, it, fill, lit):
    x0, top, w = it["x"], it["top"], it["w"]
    tip = w // 2 + 1
    for y in range(top, H):
        if y - top < tip:
            half = (y - top)
            xs = range(x0 + w // 2 - half, x0 + w // 2 + half + 1)
        else:
            xs = range(x0, x0 + w)
        for x in xs:
            img.set(x, y, lit if x == xs[0] or (y - top < tip and x <= x0 + w // 2) else fill)


def block(img, it, fill, lit):
    x0, w, top = it["x"], it["w"], it["top"]
    rect(img, x0, top, w, H - top, fill)
    for y in range(top, H):
        img.set(x0, y, lit)
    m = it.get("merlon")
    if m:
        mw, gap = m
        x = x0
        while x + mw <= x0 + w:
            rect(img, x, top - mw, mw, mw, fill)
            for y in range(top - mw, top):
                img.set(x, y, lit)
            x += mw + gap


def crane(img, it, c):
    x, top, mw = it["x"], it["top"], it.get("mast_w", 5)
    back, front = it["jib"]
    # Mast: two chords and a zigzag lattice between them.
    for y in range(top, H):
        img.set(x, y, c)
        img.set(x + mw - 1, y, c)
    y, left = top + 3, True
    while y + mw - 1 < H:
        line(img, x, y, x + mw - 1, y + mw - 1, c) if left else line(img, x + mw - 1, y, x, y + mw - 1, c)
        y += mw - 1
        left = not left
    # Cat-head above the mast, pendant ties to both jib ends.
    apex = (x + mw // 2, top - 7)
    line(img, x, top, apex[0], apex[1], c)
    line(img, x + mw - 1, top, apex[0], apex[1], c)
    line(img, apex[0], apex[1], x - back, top, c)
    line(img, apex[0], apex[1], x + mw - 1 + front * 2 // 3, top, c)
    # Jib: top and bottom chords 3 px apart with triangles between.
    j0, j1 = x - back, x + mw - 1 + front
    for xx in range(j0, j1 + 1):
        img.set(xx, top, c)
        img.set(xx, top + 3, c)
    xx = j0
    while xx + 3 <= j1:
        line(img, xx, top + 3, xx + 3, top, c)
        line(img, xx + 3, top, xx + 6 if xx + 6 <= j1 else j1, top + 3, c)
        xx += 6
    # Counterweight on the short arm, cab under the jib, cable and hook.
    ww, wh = it.get("weight", [6, 5])
    rect(img, j0 + 1, top + 4, ww, wh, c)
    rect(img, x + mw, top + 4, 3, 3, c)
    t = x + mw - 1 + it["trolley"]
    rect(img, t - 1, top + 4, 3, 1, c)
    for y in range(top + 5, it["hook_y"]):
        img.set(t, y, c)
    rect(img, t - 1, it["hook_y"], 3, 2, c)


def scaffold(img, it, c):
    x0, w, top, bay = it["x"], it["w"], it["top"], it.get("bay", 8)
    for x in range(x0, x0 + w + 1, bay):
        for y in range(top - 2, H):
            img.set(x, y, c)
    k = 0
    for y in range(top, H, bay):
        for x in range(x0, x0 + w + 1):
            img.set(x, y, c)
        if y + bay < H:
            for x in range(x0, x0 + w, bay):
                if (x - x0) // bay % 2 == k % 2:
                    line(img, x, y, x + bay, y + bay, c)
        k += 1


def layer(img, pal, spec):
    for it in spec.get("items", []):
        fname, lname = it.get("fill", spec["fill"]), it.get("lit", spec.get("lit", spec["fill"]))
        fill, lit = pal(fname), pal(lname)
        kind = it["type"]
        if kind == "pyramid":
            pyramid(img, pal, dict(it, fill_name=fname, lit_name=lname), fill, lit)
        elif kind == "obelisk":
            obelisk(img, it, fill, lit)
        elif kind == "block":
            block(img, it, fill, lit)
        elif kind == "crane":
            crane(img, it, fill)
        elif kind == "scaffold":
            scaffold(img, it, fill)
        else:
            raise SkyError("unknown skyline item %r" % kind)
    if "ground" in spec:
        rect(img, 0, spec["ground"], W, H - spec["ground"], pal(spec["fill"]))


def paint(spec, pal, shift=0):
    """Return the painted Sky (palette indices) for one JSON spec. shift (even)
    paints the same sky rotated right by shift columns, for the seam test."""
    img = Sky(pal(spec["bands"][0][1]), shift)
    title.bands(img, pal, spec["bands"])
    if "stars" in spec:
        title.stars(img, pal, spec["stars"], lambda x, y: True)
    if "sun" in spec:
        sun(img, pal, spec["sun"])
    clouds(img, pal, spec.get("clouds", []))
    for ly in spec.get("layers", []):
        layer(img, pal, ly)
    return img


def to_tex(img, pal):
    """Painted indices -> RGBA -> tools/quantize.py; must round-trip exactly."""
    rgb = [rgb for rgb, _ in pal.entries]
    rgba = bytearray()
    for i in img.px:
        rgba += bytes(rgb[i]) + b"\xff"
    t = quantize.quantize_tex(bytes(rgba), W, H)
    for y in range(H):
        for x in range(W):
            if t.get(x, y) != img.get(x, y):
                raise SkyError("pixel %d,%d: painted %s, quantized to %s" % (
                    x, y, pal.entries[img.get(x, y)][1], pal.entries[t.get(x, y)][1]))
    return t


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("specs", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--preview", help="also write indexed PNG previews here")
    a = ap.parse_args(argv)
    pal = hud.Pal()
    os.makedirs(a.out, exist_ok=True)
    if a.preview:
        os.makedirs(a.preview, exist_ok=True)
    for path in a.specs:
        name = os.path.splitext(os.path.basename(path))[0]
        with open(path) as f:
            spec = json.load(f)
        try:
            img = paint(spec, pal)
            t = to_tex(img, pal)
        except (SkyError, hud.HudError) as e:
            sys.exit("skygen: %s: %s" % (path, e))
        t.save(os.path.join(a.out, name + ".tex"))
        if a.preview:
            img.to_pil(pal).save(os.path.join(a.preview, name + ".png"))
    print("skygen: %d sky texture(s) in %s" % (len(a.specs), a.out))


if __name__ == "__main__":
    main()
