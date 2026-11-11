import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 6, Eye of Ra Lance (weapons.md): 3/4 rear view of a long steel rifle barrel through a",
    "# gold sun disc whose rear face carries a lapis eye (the charge indicator), a forked gold emitter",
    "# at the muzzle, wooden grip under the right paw, left paw on the barrel. Matches HUD icon weapon_6.",
    "# Composed in tools/vmcompose/vm6.py, checked at 3x in game.",
]

W, H = 96, 80
AIM = (48.0, H - 104.0)
GUN = 66.0
DISC = 37.0
KINDS = ("idle", "charge0", "charge1", "fire0", "fire1")
DROP = {"idle": 0, "charge0": 0, "charge1": 0, "fire0": 2, "fire1": 3}


def frame(kind):
    cv = Canvas(W, H)
    R0 = (55.0, 86.0 + DROP[kind])
    D, N, _ = axis(R0, AIM)

    def at(t, s=0.0):
        return (R0[0] + D[0] * t + N[0] * s, R0[1] + D[1] * t + N[1] * s)

    # ---- left forearm, under everything
    lp = at(11.0, -10.5)
    forearm(cv, (lp[0] - 2, lp[1] + 4), (lp[0] - 26, 92.0), 6.5, 9.0, [0.18, 0.4, 0.63, 0.85], seed=2)

    # ---- wooden grip to the bottom-right
    def grip(t, s):
        k = int((s + 1) * 4.2)
        return brown(11 - (s + 1) * 2.4 - (3 if k in (2, 6) else 0))
    band(cv, at(8.0, 4.0), (80.0, 88.0), 5.5, 7.0, grip)

    # ---- barrel: steel, a lit receiver block at the rear then a long thin barrel
    band(cv, at(0.0), at(14.0), 9.5, 8.0, cyl(steel, 12.5, 3.0))
    cv.line(at(14.0, -8.0), at(14.0, 8.0), steel(4))
    band(cv, at(14.0), at(GUN - 6.0), 4.6, 2.6, lambda t, s: cyl_steel(t, s, hi=13.0, lo=3.0))

    # ---- forked gold emitter: two prongs either side of the muzzle
    for k in (-1, 1):
        band(cv, at(GUN - 9.0, k * 3.0), at(GUN + 3.0, k * 4.6), 1.6, 1.2,
             lambda t, s: gold(9.5 if s < 0 else 5.5))
    band(cv, at(GUN - 9.0), at(GUN - 6.5), 5.0, 4.8, lambda t, s: gold(10 - (s + 1) * 3))
    bx, by = at(GUN - 6.0)
    cv.set(round(bx) - 1, round(by) - 1, "steel_01")
    cv.set(round(bx), round(by) - 1, "steel_01")
    cv.set(round(bx) - 1, round(by), "steel_02")

    # ---- sun disc on the barrel, rear face toward us, with the lapis eye
    cx, cy = at(DISC)
    rx, ry = 15.0, 13.5
    cv.ellipse(cx + 1.0, cy + 1.0, rx, ry, lambda x, y: gold(2.5))          # rim thickness
    def disc(x, y):
        u, v = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
        r = math.hypot(u, v)
        lit = 8.5 - (u + v) * 2.2
        if r > 0.86:
            lit = 10.5 if u + v < -0.3 else 5           # bevelled rim catches the light
        elif r > 0.78:
            lit -= 2.5                                   # groove inside the rim
        return gold(lit)
    cv.ellipse(cx, cy, rx, ry, disc)
    # the eye: almond of lapis with a dark pupil, gold lids, a tear line below
    glow = {"charge0": "fb_cyan_dim", "charge1": "fb_cyan", "fire0": "fb_white", "fire1": "fb_cyan"}.get(kind)
    ex, ey = cx, cy - 0.5
    def eye(x, y):
        u, v = (x + 0.5 - ex) / 9.0, (y + 0.5 - ey) / 4.0
        if abs(v) > 0.78 - 0.25 * abs(u):
            return gold(3)                               # lids
        if u * u + (v * 1.3) ** 2 < 0.2:
            if glow:
                return glow if u * u + v * v > 0.04 or kind == "charge0" else "fb_white"
            return "blue_02" if u * u + v * v < 0.1 else "blue_04"
        return blue(9 - (u + v) * 3)
    cv.poly([(ex - 9, ey), (ex - 4, ey - 4), (ex + 4, ey - 4), (ex + 9, ey),
             (ex + 4, ey + 4), (ex - 4, ey + 4)], eye)
    for i in range(6):
        cv.set(round(ex - 2 + i * 0.3), round(ey + 4 + i), gold(3))
    cv.line((ex + 9, ey), (ex + 11, ey + 1), gold(3))

    # ---- left paw on the barrel below the disc, toes up-right over the steel
    paw_fist(cv, lp[0], lp[1], 6.5, 4.8, [-3.4, -1.1, 1.1, 3.4],
             along=(0.62, -0.78), lobe_r=(2.4, 2.4), reach=6.2)
    # ---- right forearm + paw on the grip
    hx, hy = 76.0, 70.0 + DROP[kind] * 0.5
    forearm(cv, (hx + 2, hy + 3), (hx + 26, 92.0), 7.0, 10.0, [0.2, 0.42, 0.6, 0.83], seed=0)
    paw_fist(cv, hx, hy, 7.5, 5.0, [-4.8, -1.6, 1.6, 4.8],
             along=(-0.97, -0.24), lobe_r=(2.3, 2.3), reach=6.6)

    # ---- emitter effects (fullbright only)
    mx, my = at(GUN + 2.0)
    if kind == "charge0":
        for d in (-1, 0, 1):
            cv.set(round(mx + d), round(my), "fb_cyan_dim")
    if kind == "charge1":
        flash(cv, mx, my, 4.0, 3.0, "fb_white", "fb_cyan", "fb_cyan_dim")
    if kind in ("fire0", "fire1"):
        # beam up the axis to the top edge
        wide = 2.6 if kind == "fire0" else 1.4
        band(cv, at(GUN - 2.0), at(GUN + 60.0), wide, wide * 1.4,
             lambda t, s: ("fb_white" if abs(s) < 0.45 else "fb_cyan") if kind == "fire0"
             else ("fb_cyan" if abs(s) < 0.5 else "fb_cyan_dim"))
        if kind == "fire0":
            flash(cv, mx, my, 7.0, 5.0, "fb_white", "fb_cyan", "fb_cyan_dim",
                  rays=[(-1, 0.2, 3), (1, 0.2, 3)], ray_c="fb_cyan_dim")
    fill_holes(cv, "fur_03")
    return cv


if __name__ == "__main__":
    for k in KINDS:
        frame(k).write("%s/vm6_%s.pix" % (SRC, k), "vm6_" + k, HDR + ["# %s." % k])
