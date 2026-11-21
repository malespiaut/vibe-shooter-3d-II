import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 5, Scarab Launcher (weapons.md): 3/4 rear view of a heavy gold tube with steel band",
    "# rings and a flared steel muzzle collar, a lapis scarab seated in the bore, a wooden grip under",
    "# the right paw and the left paw cradling the tube. Matches HUD icon weapon_5.",
    "# Composed in tools/vmcompose/vm5.py, checked at 3x in game.",
]

W, H = 112, 72
AIM = (56.0, H - 104.0)      # horizon centre in canvas coordinates
GUN = 60.0                   # rear (off the bottom edge) -> muzzle face
DROP = {"idle": 0, "fire0": 0, "fire1": 3, "reload0": 6}


def frame(kind):
    cv = Canvas(W, H)
    R0 = (66.0, 78.0 + DROP[kind])
    D, N, _ = axis(R0, AIM)

    def at(t, s=0.0):
        return (R0[0] + D[0] * t + N[0] * s, R0[1] + D[1] * t + N[1] * s)

    # ---- left forearm (under the gun)
    if kind == "reload0":
        lp = at(GUN + 1.0, -13.0)
        forearm(cv, (lp[0] - 2, lp[1] + 5), (-4.0, 84.0), 6.0, 9.0, [0.15, 0.37, 0.58, 0.8], seed=1)
    else:
        lp = at(24.0, -15.5)
        forearm(cv, (lp[0] - 2, lp[1] + 4), (lp[0] - 26, 84.0), 6.5, 9.0, [0.18, 0.4, 0.63, 0.85], seed=2)

    # ---- wooden grip down to the bottom-right edge
    def grip(t, s):
        k = int((s + 1) * 4.2)
        return brown(11 - (s + 1) * 2.4 - (3 if k in (2, 6) else 0))
    band(cv, at(12.0, 6.0), (90.0, 80.0), 6.5, 8.0, grip)

    # ---- gold tube with steel rings
    rings = ((9.0, 12.5), (28.0, 31.0), (45.0, 47.5))
    gold_c = cyl(gold, 9.6, 2.5)
    steel_c = cyl(steel, 13.0, 3.0)

    def tube(t, s):
        d = t * GUN
        for a, b in rings:
            if a <= d < b:
                c = steel_c(t, s)
                return steel(5) if d >= b - 1 else c
        if d > GUN - 7.5:                      # flared collar
            c = steel_c(t, s)
            return steel(4) if d < GUN - 6.5 else c
        return gold_c(t, s)
    band(cv, at(0.0), at(GUN - 7.5), 20.0, 9.0, tube)
    band(cv, at(GUN - 7.5), at(GUN), 10.5, 11.5, tube)
    # scarab inlay between the first two rings: a small lapis beetle on the gold
    sx, sy = at(20.0, -4.0)
    scarab(cv, sx, sy, 3.6, 3.8)

    # ---- muzzle face: steel ring, dark bore, scarab (idle) seen inside
    mx, my = at(GUN)
    rx, ry = 11.5, 5.0
    cv.ellipse(mx, my, rx, ry, lambda x, y: steel(12 - ((x + 0.5 - mx) / rx + (y + 0.5 - my) / ry) * 4))
    cv.ellipse(mx, my - 0.5, rx - 3.0, ry - 1.8, lambda x, y: steel(2 if (y + 0.5 - my) < -1.2 else 1))
    if kind == "idle":
        scarab(cv, mx, my - 0.2, 4.6, 2.6)

    # ---- left paw: over the tube's lit side, toes toward screen right
    if kind == "reload0":
        # pushing a fresh scarab into the bore from above
        scarab(cv, mx + 1.0, my - 3.5, 6.0, 4.4)
        paw_fist(cv, lp[0], lp[1], 6.5, 4.8, [-3.4, -1.1, 1.1, 3.4],
                 along=(0.55, -0.83), lobe_r=(2.6, 1.9), reach=6.8)
    else:
        paw_fist(cv, lp[0], lp[1], 6.5, 4.8, [-3.4, -1.1, 1.1, 3.4],
                 along=(0.62, -0.78), lobe_r=(2.4, 2.4), reach=6.2)

    # ---- right forearm + paw on the grip
    hx, hy = 87.0, 63.0 + DROP[kind] * 0.5
    forearm(cv, (hx + 2, hy + 3), (hx + 28, 84.0), 7.0, 10.0, [0.2, 0.42, 0.6, 0.83], seed=0)
    paw_fist(cv, hx, hy, 7.5, 5.0, [-4.8, -1.6, 1.6, 4.8],
             along=(-0.97, -0.24), lobe_r=(2.3, 2.3), reach=6.6)

    # ---- muzzle effects (fullbright only here)
    if kind == "fire0":
        flash(cv, mx, my - 6.0, 15.0, 9.0, "fb_white", "fb_yellow", "fb_orange",
              rays=[(-1, -0.3, 4), (1, -0.3, 4), (-0.5, -1, 3), (0.5, -1, 3), (0, -1, 4)],
              ray_c="fb_fire")
    if kind == "fire1":
        flash(cv, mx, my - 1.5, 7.0, 2.8, "fb_orange", "fb_fire", "fb_fire")
    fill_holes(cv, "fur_03")
    return cv


if __name__ == "__main__":
    for k in DROP:
        frame(k).write("%s/vm5_%s.pix" % (SRC, k), "vm5_" + k, HDR + ["# %s." % k])
