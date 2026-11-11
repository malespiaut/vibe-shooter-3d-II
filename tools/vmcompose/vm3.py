import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 3, Hairball Scattergun (weapons.md): 3/4 rear view, canted. Narrow steel receiver",
    "# (lit top face with the ejection port, shaded side with the gold inlay), separate barrel (left)",
    "# and magazine tube (right) receding toward the view centre with two bores at the tip, wooden",
    "# pump fore-end gripped from above by the left paw, red wound-yarn drum on the right of the",
    "# receiver, wooden grip wrapped by the right paw. Matches HUD icon weapon_3.",
    "# Composed in tools/vmcompose/vm3.py, checked at 3x in game.",
]

# Screen-space layout (112x64 canvas, the view centre is at about (56, -40)).
R0 = (77.0, 60.0)            # rear of the receiver
AIM = (57.0, -40.0)
D, N, _ = axis(R0, AIM)
REC = 18.0                   # receiver length (px along the axis)
GUN = 46.0                   # rear of receiver -> muzzle
FORE = {"idle": 25.0, "fire0": 25.0, "fire1": 25.0, "pump0": 21.0, "pump1": 18.0}
FORE_LEN = 11.0


def at(t, s=0.0, p=R0):
    return (p[0] + D[0] * t + N[0] * s, p[1] + D[1] * t + N[1] * s)


def frame(kind):
    cv = Canvas(112, 64)
    fz = FORE[kind]

    # ---- left forearm from the bottom-left up to the fore-end (drawn first, under the gun)
    pc = at(fz + FORE_LEN * 0.5, -11.5)
    forearm(cv, (pc[0] - 2, pc[1] + 4), (pc[0] - 40, 78), 7.0, 10.0,
            [0.12, 0.29, 0.47, 0.66, 0.85], seed=2)

    # ---- barrel (left) and magazine tube (right): two cylinders split by a dark seam
    band(cv, at(REC - 1, -0.2), at(GUN, -0.2), 6.2, 5.0, lambda t, s: steel(2 if abs(s) < 0.3 else 1))
    band(cv, at(REC - 1, 2.9), at(GUN - 1.0, 2.2), 2.4, 1.8, lambda t, s: cyl_steel(t, s, hi=12.5, lo=3))
    band(cv, at(REC - 1, -3.2), at(GUN, -2.6), 2.8, 2.2, lambda t, s: cyl_steel(t, s, hi=13.5, lo=4))
    # bores: 2x2 steel_01 in a lighter rim, barrel above the tube
    for (bt, bs) in ((GUN - 1.6, -2.6), (GUN - 2.6, 2.2)):
        x, y = at(bt, bs)
        x, y = int(round(x)) - 1, int(round(y)) - 1
        for j in range(-1, 3):
            for i in range(-1, 3):
                cv.set(x + i, y + j, steel(11 if (i < 0 or j < 0) else 7))
        for j in range(2):
            for i in range(2):
                cv.set(x + i, y + j, "steel_01")

    # ---- wooden pump fore-end around both, grooves across it
    def fore(t, s):
        g = 0.12 < t < 0.88 and int(t * 8) % 2 == 1
        if s < -0.8:
            v = 13
        elif s < -0.1:
            v = 12 - (s + 0.8) * 2
        else:
            v = 8.5 - s * 3.5
        if g:
            v -= 3.5
        if t < 0.08 or t > 0.94:
            v -= 1.5                              # rounded ends
        return brown(v)
    band(cv, at(fz, -0.3), at(fz + FORE_LEN, -0.3), 7.4, 6.8, fore)

    # ---- left paw lying over the top (lit, left) side of the fore-end
    paw_fist(cv, pc[0] - 1.0, pc[1] + 0.5, 7.0, 8.0, [-5.4, -1.8, 1.8, 5.4],
             along=(N[0], N[1]), lobe_r=(2.8, 2.7), reach=7.4)

    # ---- grip / stock wood off the bottom edge, right paw on it
    def grip(t, s):
        k = int((s + 1) * 4.2)
        return brown(11 - (s + 1) * 2.4 - (3 if k in (2, 6) else 0))
    band(cv, at(2.0, 1.0), (90.0, 72.0), 5.6, 6.6, grip)

    # ---- receiver: narrow, lit top face with the ejection port, gold inlay on the side
    def rec(t, s):
        d = t * REC
        if d > REC - 1.0:
            return steel(9 if s < 0 else 4)       # front edge
        if s < -0.85:
            return steel(15)
        if s < -0.12:
            if 4.0 <= d < 10.0 and -0.75 <= s < -0.3:
                return steel(2) if s < -0.62 else steel(3) if s < -0.45 else steel(2)  # port
            return steel(13.5 - (s + 0.85) * 2 - t)
        if s < 0.0:
            return steel(9)
        if 0.48 <= s <= 0.62:
            return gold(7 - t * 2)
        if s > 0.86:
            return steel(3)
        return steel(6.5 - s * 1.5 - t)
    band(cv, at(0.0), at(REC), 8.5, 7.8, rec)
    # rear edge of the receiver catches the light
    cv.line(at(0.4, -8.0), at(0.4, 7.5), steel(11))

    # ---- red yarn drum on the right of the receiver: wound strands + lit top-left
    dx, dy = at(13.0, 17.0)
    rx, ry = 7.6, 8.0

    def yarn(x, y):
        u, v = (x + 0.5 - dx) / rx, (y + 0.5 - dy) / ry
        lit = 10.5 - (u + v) * 3.2
        if math.hypot(u, v) > 0.82:
            lit -= 1.5
        if u < -0.15 and v < -0.15 and 0.2 < u * u + v * v < 0.5:
            lit = 13                               # highlight
        # wound strands: arcs that follow the ball's curvature
        for (ox, oy, R) in ((1.1, 1.2, 0.75), (1.1, 1.2, 1.2), (1.1, 1.2, 1.65),
                            (-1.6, 0.9, 1.5)):
            r = math.hypot(u - ox, v - oy)
            if abs(r - R) < 0.12:
                return red(min(lit, 9) - 4)
        return red(lit)
    cv.ellipse(dx, dy, rx, ry, yarn)

    # ---- right forearm + paw on the grip
    hx, hy = 86.0, 55.5
    forearm(cv, (hx + 2, hy + 3), (hx + 30, 80), 7.0, 10.0, [0.2, 0.42, 0.6, 0.83], seed=0)
    paw_fist(cv, hx, hy, 7.5, 6.0, [-5.2, -1.7, 1.7, 5.2],
             along=(-0.92, -0.39), lobe_r=(2.3, 2.4), reach=6.4)

    # ---- muzzle effects
    if kind == "fire0":
        fx, fy = at(GUN + 5.5, 0.0)
        flash(cv, fx, fy, 10.5, 5.5, "fb_sun", "fb_yellow", "fb_orange",
              rays=[(-1, -0.25, 3), (1, -0.25, 3), (-0.6, -1, 2), (0.6, -1, 2)], ray_c="fb_orange")
    if kind == "fire1":
        for (bt, bs) in ((GUN - 1.6, -2.6), (GUN - 2.6, 2.2)):
            x, y = at(bt, bs)
            cv.set(int(round(x)) - 1, int(round(y)) - 2, "fb_orange")
    return cv


def measure():
    print("vm3_fire0 fullbright px:", count_fb(frame("fire0")))


if __name__ == "__main__":
    for k in FORE:
        cv = frame(k)
        cv.write("%s/vm3_%s.pix" % (SRC, k), "vm3_" + k, HDR + ["# %s." % k])
    if "-m" in sys.argv:
        measure()
