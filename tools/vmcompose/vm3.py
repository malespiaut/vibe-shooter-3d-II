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
# Round 5: a heavy gun. Receiver 28 px across, barrel + tube pair 18-20 px
# across at the fore-end, a short left forearm, the yarn drum bolted onto the
# receiver's right edge, and fore-end grooves running along the barrels.
R0 = (70.0, 57.0)            # rear of the receiver
AIM = (56.0, -40.0)
D, N, _ = axis(R0, AIM)
REC = 17.0                   # receiver length (px along the axis)
GUN = 47.0                   # rear of receiver -> muzzle
FORE = {"idle": 22.0, "fire0": 22.0, "fire1": 22.0, "pump0": 19.5, "pump1": 17.5}
FORE_LEN = 19.0
BAR_S, TUBE_S = -4.6, 4.4    # across-axis centres of the barrel and the tube


def at(t, s=0.0, p=R0):
    return (p[0] + D[0] * t + N[0] * s, p[1] + D[1] * t + N[1] * s)


def frame(kind):
    cv = Canvas(112, 64)
    fz = FORE[kind]

    # ---- left forearm from the bottom-left up to the fore-end (drawn first, under the gun)
    pc = at(fz + FORE_LEN * 0.44, -9.0)
    forearm(cv, (pc[0] - 1, pc[1] + 4), (pc[0] - 22, 74), 6.0, 8.0,
            [0.16, 0.38, 0.6, 0.84], seed=2)

    # ---- barrel (left) and magazine tube (right): two cylinders split by a dark seam
    band(cv, at(REC - 1, -0.2), at(GUN, -0.2), 9.6, 8.4, lambda t, s: steel(2 if abs(s) < 0.12 else 1))
    band(cv, at(REC - 1, TUBE_S), at(GUN - 1.0, TUBE_S - 0.6), 4.2, 3.5,
         lambda t, s: cyl_steel(t, s, hi=12.5, lo=3))
    band(cv, at(REC - 1, BAR_S), at(GUN, BAR_S + 0.6), 4.6, 3.9,
         lambda t, s: cyl_steel(t, s, hi=13.5, lo=4))
    bores = ((GUN - 2.0, BAR_S + 0.6), (GUN - 3.0, TUBE_S - 0.6))
    # bores: 2x2 steel_01 in a lighter rim, barrel above the tube
    for (bt, bs) in bores:
        x, y = at(bt, bs)
        x, y = int(round(x)) - 1, int(round(y)) - 1
        for j in range(-1, 3):
            for i in range(-1, 3):
                cv.set(x + i, y + j, steel(11 if (i < 0 or j < 0) else 7))
        for j in range(2):
            for i in range(2):
                cv.set(x + i, y + j, "steel_01")

    # ---- wooden pump fore-end around both, grooves along the barrel axis
    def fore(t, s):
        if s < -0.84:
            v = 13
        elif s < -0.1:
            v = 12 - (s + 0.84) * 2
        else:
            v = 8.5 - s * 3.5
        if 0.1 < t < 0.9 and min(abs(s - g) for g in (-0.5, -0.05, 0.4)) < 0.07:
            v -= 4                                # long grooves
        if t < 0.06 or t > 0.95:
            v -= 2                                # rounded ends
        return brown(v)
    band(cv, at(fz, -0.2), at(fz + FORE_LEN, -0.2), 11.4, 10.6, fore)

    # ---- left paw lying over the top (lit, left) side of the fore-end
    before = [row[:] for row in cv.px]
    paw_fist(cv, pc[0] - 1.5, pc[1] + 0.3, 6.5, 4.6, [-3.4, -1.1, 1.1, 3.4],
             along=(N[0], N[1]), lobe_r=(2.6, 1.8), reach=6.8)
    frame.paw = {(x, y) for y in range(cv.h) for x in range(cv.w) if cv.px[y][x] != before[y][x]}

    # ---- grip / stock wood off the bottom edge, right paw on it
    def grip(t, s):
        k = int((s + 1) * 4.2)
        return brown(11 - (s + 1) * 2.4 - (3 if k in (2, 6) else 0))
    band(cv, at(2.0, 2.0), (86.0, 72.0), 7.0, 8.0, grip)

    # ---- receiver: wide, lit top face with the ejection port, gold inlay on the side
    def rec(t, s):
        d = t * REC
        if d > REC - 1.0:
            return steel(9 if s < 0 else 4)       # front edge
        if s < -0.9:
            return steel(15)
        if s < -0.1:
            if 4.0 <= d < 11.0 and -0.7 <= s < -0.3:
                if d < 5.0 or s < -0.62:
                    return steel(2)               # port shadow edge
                return steel(4) if s > -0.38 else steel(3)
            return steel(13.5 - (s + 0.9) * 2 - t)
        if s < 0.0:
            return steel(9)
        if 0.5 <= s <= 0.6:
            return gold(7.5 - t * 2)
        if s > 0.9:
            return steel(3)
        if 0.18 < s < 0.4 and int(d) % 4 == 1:
            return steel(4)                       # side-plate rivets / seams
        return steel(6.5 - s * 1.5 - t)
    band(cv, at(0.0), at(REC), 14.6, 13.4, rec)
    # rear edge of the receiver catches the light
    cv.line(at(0.4, -14.0), at(0.4, 13.6), steel(11))

    # ---- red yarn drum bolted onto the right edge of the receiver: a steel
    # bracket from the receiver to the drum's centre, and the ball overlaps the
    # receiver edge by 4+ px.
    dx, dy = at(9.0, 17.5)
    rx, ry = 7.6, 8.0
    band(cv, at(9.0, 10.0), (dx, dy), 1.6, 1.6, lambda t, s: steel(10 if s < 0 else 5))

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
    # hub: the bracket's bolt in the middle of the ball
    hx, hy = int(round(dx)) - 1, int(round(dy)) - 1
    for j in range(2):
        for i in range(2):
            cv.set(hx + i, hy + j, steel(12 if i + j == 0 else 6))

    # ---- right forearm + paw on the grip
    hx, hy = 86.0, 60.0
    forearm(cv, (hx + 2, hy + 3), (hx + 28, 80), 7.0, 10.0, [0.2, 0.42, 0.6, 0.83], seed=0)
    paw_fist(cv, hx, hy, 7.5, 5.0, [-4.8, -1.6, 1.6, 4.8],
             along=(-0.97, -0.24), lobe_r=(2.3, 2.3), reach=6.6)

    # ---- muzzle effects
    if kind == "fire0":
        fx, fy = at(GUN + 5.0, 0.0)
        flash(cv, fx, fy, 10.5, 5.5, "fb_sun", "fb_yellow", "fb_orange",
              rays=[(-1, -0.25, 3), (1, -0.25, 3), (-0.6, -1, 2), (0.6, -1, 2)], ray_c="fb_orange")
    if kind == "fire1":
        for (bt, bs) in bores:
            x, y = at(bt, bs)
            cv.set(int(round(x)) - 1, int(round(y)) - 2, "fb_orange")
    return cv


def measure():
    print("vm3_fire0 fullbright px:", count_fb(frame("fire0")))
    for k in FORE:
        cv = frame(k)
        paw = frame.paw
        # fore-end wood rows above / below the left paw, in the paw's columns
        cols = {x for x, y in paw}
        wood = lambda x, y: (cv.px[y][x] or "").startswith("brown")
        def run(x, y, dy):
            n = 0
            while 0 <= y < cv.h and wood(x, y):
                n, y = n + 1, y + dy
            return n
        lx = int(round(at(FORE[k] + FORE_LEN * 0.44, -9.0)[0] - 1.5 + N[0] * 6.8))
        above = run(lx, min(y for xx, y in paw if xx == lx) - 1, -1)
        below = run(lx, max(y for xx, y in paw if xx == lx) + 1, 1)
        drum = [(x, y) for y in range(cv.h) for x in range(cv.w) if (cv.px[y][x] or "").startswith("red")]
        def in_rec(x, y):
            px, py = x + 0.5 - R0[0], y + 0.5 - R0[1]
            t, s = px * D[0] + py * D[1], px * N[0] + py * N[1]
            return 0 <= t <= REC and abs(s) <= 14.6 - 1.2 * t / REC
        ov = sum(1 for p in drum if in_rec(*p))
        rows = {y for x, y in drum if in_rec(x, y)}
        print("vm3_%s: receiver %.0f px across at the rear, %.0f at the front; drum overlaps it "
              "by %d px over %d rows" % (k, 2 * 14.6, 2 * 13.4, ov, len(rows)))
        row = lambda y: [x for x in range(cv.w) if (cv.px[y][x] or "").startswith("steel")]
        y = int(round(at(GUN - 4.0)[1]))
        print("vm3_%s: (column x=%d) wood above paw %d rows, below %d rows; barrel+tube row %d: %d px"
              % (k, lx, above, below, y, max(row(y)) - min(row(y)) + 1))


if __name__ == "__main__":
    for k in FORE:
        cv = frame(k)
        cv.write("%s/vm3_%s.pix" % (SRC, k), "vm3_" + k, HDR + ["# %s." % k])
    if "-m" in sys.argv:
        measure()
