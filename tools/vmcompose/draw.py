#!/usr/bin/env python3
"""Composer for the vm2 / vm3 view-model frames (issue #86, PR #107).

Builds simple boxes in gun space, projects them with a canted perspective
camera and shades faces with light from the top-left (art-bible), then writes
the .pix grids in assets/src/viewmodels. The .pix files are the source the
build reads; rerun
  python3 tools/vmcompose/vm2.py && python3 tools/vmcompose/vm3.py
only to regenerate them after changing the geometry here."""
import math
import os
import sys
from PIL import Image, ImageDraw

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "assets", "src", "viewmodels")


def load_chars():
    m = {}
    for line in open(SRC + "/legend.txt"):
        s = line.rstrip("\n")
        if not s or s.startswith("#"):
            continue
        ch, name = s[0], s[1:].strip()
        m[name] = ch
    return m


CH = load_chars()


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[None] * w for _ in range(h)]

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y][x]
        return None

    def set(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def mask_poly(self, pts):
        m = Image.new("L", (self.w, self.h), 0)
        ImageDraw.Draw(m).polygon([(round(x), round(y)) for x, y in pts], fill=255)
        return m

    def fill_mask(self, m, fn):
        d = m.load()
        for y in range(self.h):
            for x in range(self.w):
                if d[x, y]:
                    c = fn(x, y)
                    if c:
                        self.px[y][x] = c

    def poly(self, pts, fn):
        if isinstance(fn, str):
            c = fn
            fn = lambda x, y: c
        self.fill_mask(self.mask_poly(pts), fn)

    def ellipse(self, cx, cy, rx, ry, fn):
        m = Image.new("L", (self.w, self.h), 0)
        d = m.load()
        for y in range(self.h):
            for x in range(self.w):
                dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    d[x, y] = 255
        self.fill_mask(m, fn)

    def line(self, p0, p1, c):
        x0, y0 = p0
        x1, y1 = p1
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / n
            self.set(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), c)

    def write(self, path, name, header):
        lines = list(header)
        lines += ["name %s" % name, "size %d %d" % (self.w, self.h), "shadow steel_00", "grid"]
        for row in self.px:
            s = "".join(CH[c] if c else "." for c in row).rstrip(".")
            lines.append(s if s else ".")
        open(path, "w").write("\n".join(lines) + "\n")


def ramp(prefix, v, lo, hi):
    v = max(lo, min(hi, int(round(v))))
    return "%s_%02d" % (prefix, v)


def steel(v):
    return ramp("steel", v, 1, 15)


def fur(v):
    return ramp("fur", v, 1, 11)


def brown(v):
    return ramp("brown", v, 2, 13)


def gold(v):
    return ramp("gold", v, 2, 11)


def red(v):
    # red ramp only has odd steps in the legend
    opts = [3, 5, 7, 9, 11, 13]
    v = min(opts, key=lambda o: abs(o - v))
    return "red_%02d" % v


class Cam:
    """Gun-local (X right, Y up, Z forward) -> canvas pixels."""

    def __init__(self, f, cx, cy, off, yaw=0.0, pitch=0.0, roll=0.0):
        self.f, self.cx, self.cy, self.off = f, cx, cy, off
        self.cr, self.sr = math.cos(roll), math.sin(roll)
        self.cyw, self.syw = math.cos(yaw), math.sin(yaw)
        self.cp, self.sp = math.cos(pitch), math.sin(pitch)

    def __call__(self, p):
        x, y, z = p
        x, y = x * self.cr - y * self.sr, x * self.sr + y * self.cr
        x, z = x * self.cyw + z * self.syw, -x * self.syw + z * self.cyw
        y, z = y * self.cp - z * self.sp, y * self.sp + z * self.cp
        x += self.off[0]
        y += self.off[1]
        z += self.off[2]
        return (self.cx + self.f * x / z, self.cy - self.f * y / z)


def box(cv, cam, x0, x1, y0, y1, z0, z1, top, side, rear, faces="trs", left=None):
    """Draw a box. top/side/rear: fn(u, v) -> colour, u,v in 0..1 across the face
    (u along Z for top/side (0 = rear), v across)."""
    P = lambda x, y, z: cam((x, y, z))
    out = {}
    if "l" in faces and left:
        q = [P(x0, y1, z0), P(x0, y1, z1), P(x0, y0, z1), P(x0, y0, z0)]
        cv.poly(q, _uv(q, left))
        out["l"] = q
    if "s" in faces:
        q = [P(x1, y1, z0), P(x1, y1, z1), P(x1, y0, z1), P(x1, y0, z0)]
        cv.poly(q, _uv(q, side))
        out["s"] = q
    if "t" in faces:
        q = [P(x0, y1, z0), P(x0, y1, z1), P(x1, y1, z1), P(x1, y1, z0)]
        cv.poly(q, _uv(q, top))
        out["t"] = q
    if "r" in faces:
        q = [P(x0, y1, z0), P(x1, y1, z0), P(x1, y0, z0), P(x0, y0, z0)]
        cv.poly(q, _uv(q, rear))
        out["r"] = q
    return out


def _uv(q, fn):
    """Bilinear-ish inverse: approximate (u, v) for quad q = [a, b, c, d] where
    a->b is u (0->1) at v=0 and a->d is v."""
    a, b, c, d = q

    def inv(x, y):
        # brute force search on a coarse grid then refine
        best = (1e9, 0, 0)
        for i in range(11):
            for j in range(11):
                u, v = i / 10, j / 10
                px = (1 - v) * ((1 - u) * a[0] + u * b[0]) + v * ((1 - u) * d[0] + u * c[0])
                py = (1 - v) * ((1 - u) * a[1] + u * b[1]) + v * ((1 - u) * d[1] + u * c[1])
                e = (px - x - 0.5) ** 2 + (py - y - 0.5) ** 2
                if e < best[0]:
                    best = (e, u, v)
        return fn(best[1], best[2])
    return inv


def shade_ellipse(base, k, cx, cy, rx, ry, rampf, stripes=None):
    """Light from the top-left on an ellipse."""
    def fn(x, y):
        dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
        r2 = dx * dx + dy * dy
        v = base - k * (dx + dy) * 0.7 - (k * 0.6 if r2 > 0.75 else 0)
        if stripes and stripes(x, y):
            v -= 3
        return rampf(v)
    return fn
