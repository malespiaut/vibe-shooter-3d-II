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


# ---- hand-drawn style paws and forearms (art-bible "View models", as vm1/vm4)

def forearm(cv, a, b, w0, w1, bands, seed=0):
    """Forearm from wrist a=(x, y) to b (usually off-canvas), half-widths w0 at a
    and w1 at b. Cylinder shading: fur_10/11 on the screen-left (lit) edge down
    to fur_04/05 on the right edge. bands: axial positions 0..1 of curved tabby
    bands running across the arm (irregular spacing, curved like vm1)."""
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = -uy, ux                   # normal; flip so +s points to screen right
    if nx < 0:
        nx, ny = -nx, -ny
    pts = [(ax - nx * w0, ay - ny * w0), (ax + nx * w0, ay + ny * w0),
           (bx + nx * w1, by + ny * w1), (bx - nx * w1, by - ny * w1)]

    def fn(x, y):
        px, py = x + 0.5 - ax, y + 0.5 - ay
        t = (px * ux + py * uy) / L
        w = w0 + (w1 - w0) * max(0.0, min(1.0, t))
        s = (px * nx + py * ny) / w            # -1 left .. +1 right
        v = 10.6 - (s + 1) * 3.0                # 10.6 lit edge .. 4.6 shadow edge
        if s > 0.82:
            v -= 1                              # rim shadow on the right edge
        for i, tb in enumerate(bands):
            # band curves toward the wrist at the edges (ring seen from above)
            c = tb - 0.05 * (1 - s * s) * (1 if i % 2 else 0.7)
            thick = (1.3 + 0.5 * ((i + seed) % 3)) / L
            if abs(t - c) < thick * (1.0 - 0.35 * abs(s)) and abs(s) < 0.93:
                return fur(min(v, 6) - 2.5)
        return fur(v)
    cv.poly(pts, fn)


def paw_fist(cv, cx, cy, w, h, lobes, along=(0.0, -1.0), lobe_r=(3.2, 2.6),
             claws=True, body_dark=0, reach=None):
    """Back of a closed paw (vm1 style): a rounded-square body with shading from
    the top-left and a row of separate toe lobes on the side given by `along`
    (unit vector from body centre toward the knuckles). Lobes are separated by
    1 px fur_03 gaps and each gets a fur_11 knuckle highlight on its top-left."""
    ax, ay = along

    def body(x, y):
        dx, dy = (x + 0.5 - cx) / w, (y + 0.5 - cy) / h
        return dx, dy
    m = Image.new("L", (cv.w, cv.h), 0)
    d = m.load()
    for y in range(cv.h):
        for x in range(cv.w):
            dx, dy = body(x, y)
            if abs(dx) ** 3.2 + abs(dy) ** 3.2 <= 1.0:      # squircle, not a ball
                d[x, y] = 255

    def bfn(x, y):
        dx, dy = body(x, y)
        v = 8.6 - (dx + dy) * 2.4 - body_dark
        if abs(dx) ** 3.2 + abs(dy) ** 3.2 > 0.72:
            v -= 1.2 if (dx + dy) > -0.3 else -0.6
        # soft fur tuft line across the back of the paw
        if abs(dy * 0.9 - dx * 0.25 - 0.18) < 0.07 and abs(dx) < 0.6:
            v -= 2.5
        return fur(v)
    cv.fill_mask(m, bfn)
    # toe lobes along the knuckle side
    px, py = -ay, ax                       # along the knuckle row
    rx, ry = lobe_r
    n = len(lobes)
    for i, off in enumerate(lobes):
        k = h * 0.78 if reach is None else reach
        lx = cx + ax * k + px * off
        ly = cy + ay * k + py * off
        # gap ring first (1 px darker outline only toward neighbours / body)
        def gap(x, y, lx=lx, ly=ly):
            return "fur_03"
        cv.ellipse(lx, ly, rx + 0.9, ry + 0.9, lambda x, y, lx=lx, ly=ly: (
            "fur_03" if cv.get(x, y) is not None else None))
        def lobe(x, y, lx=lx, ly=ly, i=i):
            dx, dy = (x + 0.5 - lx) / rx, (y + 0.5 - ly) / ry
            v = 9.0 - (dx + dy) * 2.6 - i * 0.25 - body_dark
            if dx < -0.15 and dy < -0.15 and dx * dx + dy * dy < 0.55 and dx * dx + dy * dy > 0.12:
                v = 11
            return fur(v)
        cv.ellipse(lx, ly, rx, ry, lobe)
        if claws:
            tx, ty = lx + ax * (ry + 0.6), ly + ay * (ry + 0.6)
            cv.set(round(tx), round(ty), "beige_13")
            cv.set(round(tx - ax), round(ty - ay), "beige_09")


# ---- screen-space strips (round 4): parts laid out along a screen axis

def axis(a, b):
    """Unit vector along a->b and its normal pointing to screen right."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = -uy, ux
    if nx < 0:
        nx, ny = -nx, -ny
    return (ux, uy), (nx, ny), L


def band(cv, a, b, w0, w1, fn):
    """Straight strip from a to b with half-widths w0 at a and w1 at b and flat
    (perpendicular) ends. fn(t, s): t 0..1 along a->b, s -1 (screen left) ..
    +1 (screen right) across; returns a colour or None."""
    (ux, uy), (nx, ny), L = axis(a, b)
    ax, ay = a
    bx, by = b
    pts = [(ax - nx * w0, ay - ny * w0), (ax + nx * w0, ay + ny * w0),
           (bx + nx * w1, by + ny * w1), (bx - nx * w1, by - ny * w1)]

    def f(x, y):
        px, py = x + 0.5 - ax, y + 0.5 - ay
        t = (px * ux + py * uy) / L
        w = w0 + (w1 - w0) * max(0.0, min(1.0, t))
        s = max(-1.0, min(1.0, (px * nx + py * ny) / w))
        return fn(max(0.0, min(1.0, t)), s)
    cv.poly(pts, f)


def cyl_steel(t, s, hi=13.5, lo=3.0, fade=2.0):
    """Steel cylinder seen side-on, lit from the left: a steel_13+ highlight line
    near the left edge, falling to dark on the right; slightly darker far away."""
    if s < -0.82:
        v = hi - 4
    elif s < -0.35:
        v = hi + 1.5
    else:
        v = hi - 1.5 - (s + 0.35) / 1.35 * (hi - 1.5 - lo)
    return steel(v - t * fade)


def flash(cv, cx, cy, rx, ry, core, mid, rim, rays=(), ray_c=None):
    """Fullbright muzzle flash: an elliptical burst (core / mid / rim rings)
    plus straight rays [(dx, dy, length)]."""
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            r = math.hypot(dx, dy)
            # slightly star-shaped edge
            edge = 1.0 - 0.18 * abs(math.cos(4 * math.atan2(dy, dx)))
            if r <= edge:
                cv.set(x, y, core if r < 0.38 else (mid if r < 0.7 else rim))
    for dx, dy, n in rays:
        L = math.hypot(dx, dy)
        for i in range(n):
            k = (min(rx, ry) * 0.6 + i)
            cv.set(round(cx + dx / L * k - 0.5), round(cy + dy / L * k - 0.5), ray_c or rim)


def count_fb(cv):
    return sum(1 for row in cv.px for c in row if c and c.startswith("fb_"))


# ---- ramps added for weapons 5-7 (issue #87)

def blue(v):
    opts = [2, 4, 6, 8, 10]
    v = min(opts, key=lambda o: abs(o - v))
    return "blue_%02d" % v


def sand(v):
    opts = [3, 6, 9, 12]
    v = min(opts, key=lambda o: abs(o - v))
    return "sand_%02d" % v


def cyl(rampf, hi, lo):
    """Generic cylinder seen side-on, lit from the left (as cyl_steel)."""
    def fn(t, s):
        if s < -0.82:
            v = hi - 3
        elif s < -0.4:
            v = hi + 1
        else:
            v = hi - 1 - (s + 0.4) / 1.4 * (hi - 1 - lo)
        return rampf(v - t * 1.5)
    return fn


def scarab(cv, cx, cy, rx, ry):
    """Lapis scarab seen from behind / above: domed wing cases split by a dark
    seam, a top-left highlight, gold head plate toward -y, little dark legs."""
    for k in (-1, 1):
        for i in range(2):
            cv.set(round(cx + k * (rx + 0.3)), round(cy - ry * 0.3 + i * ry * 0.6), "blue_02")
    def fn(x, y):
        u, v = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
        if abs(u) < 0.13 and v > -0.55:
            return "blue_02"
        if v < -0.62:
            return gold(8 - (u + v) * 2)
        lit = 7.5 - (u + v) * 3
        if u < -0.2 and v < 0.1 and -0.55 < v and u > -0.75:
            lit = 10
        if u * u + v * v > 0.7:
            lit -= 2
        return blue(lit)
    cv.ellipse(cx, cy, rx, ry, fn)


def fill_holes(cv, c):
    """Fill transparent pixels the art encloses (gaps between an arm and the
    gun) with colour c, so no background shows through the weapon. Pixels the
    .pix loader turns into the 1 px drop shadow count as art."""
    shadow = {(x, y) for y in range(1, cv.h) for x in range(1, cv.w)
              if cv.px[y][x] is None and cv.px[y - 1][x - 1] is not None}
    seen = set()
    stack = [(x, y) for x in range(cv.w) for y in (0, cv.h - 1)] + \
            [(x, y) for y in range(cv.h) for x in (0, cv.w - 1)]
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < cv.w and 0 <= y < cv.h) or cv.px[y][x] is not None or (x, y) in shadow:
            continue
        seen.add((x, y))
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    for y in range(cv.h):
        for x in range(cv.w):
            if cv.px[y][x] is None and (x, y) not in seen and (x, y) not in shadow:
                cv.px[y][x] = c
