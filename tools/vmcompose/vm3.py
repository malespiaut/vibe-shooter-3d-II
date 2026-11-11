import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 3, Hairball Scattergun (weapons.md): 3/4 rear view from the right. Steel receiver",
    "# (lit top, shaded right face) with a tapered barrel and magazine tube receding toward the",
    "# view centre, wooden pump fore-end clasped by the left paw, red yarn drum under the receiver,",
    "# wooden grip wrapped by the right paw. Matches HUD icon weapon_3. Composed in 3D and projected, checked at 3x.",
]

FORE = {"idle": 21.0, "fire0": 21.0, "fire1": 21.0, "pump0": 9.4, "pump1": 6.0}


def frame(kind):
    cv = Canvas(112, 64)
    cam = Cam(f=140, cx=42, cy=-2, off=(3.4, -5.5, 17.0), yaw=-0.22, roll=0.55)
    P = lambda x, y, z: cam((x, y, z))
    fz = FORE[kind]

    # ---- magazine tube (under the barrel), far part first
    box(cv, cam, -0.7, 0.7, -0.2, 1.2, 4.4, 31.0,
        top=lambda u, v: steel(9 - u * 2),
        side=lambda u, v: steel(5 - u * 1.5 - (v > 0.6)),
        rear=lambda u, v: steel(6), faces="ts")
    # action rod on the right, exposed between receiver and fore-end
    box(cv, cam, 0.7, 1.15, -0.1, 0.45, 4.4, fz + 1.0,
        top=lambda u, v: steel(14), side=lambda u, v: steel(9), rear=lambda u, v: steel(6), faces="ts")
    # ---- barrel: on top of the tube, tapers with distance
    box(cv, cam, -1.0, 1.0, 1.1, 2.7, 4.4, 34.0,
        top=lambda u, v: steel(15) if v < 0.2 else steel(13 - v * 2 - u * 2.5),
        side=lambda u, v: steel(7 - u * 2 - (v > 0.7) * 1.5),
        rear=lambda u, v: steel(6))
    # muzzle bore
    mx, my = P(0, 2.0, 34.0)
    cv.set(round(mx), round(my) - 1, steel(1))
    cv.set(round(mx) - 1, round(my) - 1, steel(2))
    # ---- wood fore-end (pump), grooves across it
    def fe_top(u, v):
        g = int(u * 7) % 2 == 1 and 0.15 < u < 0.85
        return brown(13 - v * 3 - (4 if g else 0))

    def fe_side(u, v):
        g = int(u * 7) % 2 == 1 and 0.15 < u < 0.85
        return brown(6 - v * 2 - (2 if g else 0))

    box(cv, cam, -1.7, 1.7, -0.9, 1.4, fz, fz + 6.0, fe_top, fe_side,
        rear=lambda u, v: brown(8 if v < 0.3 else 6), left=lambda u, v: brown(9))
    # ---- left paw clasping the fore-end from below-left, forearm off the bottom-left
    fx, fy = P(-1.2, -1.8, fz + 2.6)
    stripe = lambda x, y: ((x * 3 - y * 2) // 4) % 6 == 0
    arm = [(fx - 4, fy - 2), (fx + 4, fy + 4), (fx - 14, 66), (fx - 40, 66)]
    cv.poly(arm, lambda x, y: fur(max(5, 8 - (fx - x) * 0.04 - (y - fy) * 0.04) - (3 if stripe(x, y) else 0)))
    cv.ellipse(fx, fy + 1, 6.0, 4.6, shade_ellipse(8.4, 3.0, fx, fy + 1, 6.0, 4.6, fur))
    # fingers wrapped over the left side of the fore-end
    for i in range(3):
        ox, oy = P(-1.9, 0.4, fz + 1.2 + i * 1.7)
        ox += 0.5
        cv.ellipse(ox, oy, 2.2, 1.9, shade_ellipse(9.6 - i * 0.5, 2.4, ox, oy, 2.2, 1.9, fur))
        cv.set(round(ox + 1.8), round(oy - 1.0), "beige_13")      # sheathed claw tips
    # ---- receiver
    def rec_top(u, v):
        if v < 0.1:
            return steel(15)
        if 0.45 < v < 0.6:
            return steel(10)              # ejection rib
        return steel(14 - v * 2 - u * 1.5)

    def rec_side(u, v):
        if 0.35 < u < 0.75 and 0.25 < v < 0.55:
            return steel(2) if v < 0.35 else steel(3)    # ejection port
        if 0.66 < v < 0.76:
            return gold(7 - u * 2)        # gold inlay, like the HUD icon
        return steel(7 - u * 1.5 - (v > 0.85))

    def rec_rear(u, v):
        if u < 0.1 or v < 0.12:
            return steel(12)
        return steel(9 - (v > 0.7) * 2)
    box(cv, cam, -1.3, 1.3, 0.6, 2.6, 0.0, 4.4, rec_top, rec_side, rec_rear)

    # ---- yarn drum under the receiver, right side
    dx, dy = P(1.6, -1.2, 3.4)
    def yarn(x, y):
        ddx, ddy = (x + 0.5 - dx) / 9.0, (y + 0.5 - dy) / 8.5
        v = 9 - (ddx + ddy) * 4.5
        if (x * 2 + y * 3) % 7 == 0 or (x * 3 - y) % 9 == 0:
            v -= 3
        return red(v)
    cv.ellipse(dx, dy, 9.0, 8.5, yarn)
    # ---- grip (wood) behind the drum, wrapped by the right paw
    gx, gy = P(0.0, 0.7, 0.6)
    cv.poly([(gx - 5, gy - 3), (gx + 3, gy - 4), (gx + 12, 66), (gx + 1, 66)],
            lambda x, y: brown(11 - (x - gx - (y - gy) * 0.4) * 0.5))
    hx, hy = gx + 8, gy + 16
    harm = [(hx - 3, hy), (hx + 9, hy - 6), (114, 50), (114, 66), (hx - 6, 66)]
    cv.poly(harm, lambda x, y: fur(max(5, 8 - (x - hx) * 0.06) - (3 if stripe(x, y) else 0)))
    cv.ellipse(hx + 2, hy - 1, 8, 7, shade_ellipse(8.0, 3.0, hx + 2, hy - 1, 8, 7, fur))
    for i in range(3):            # fingers curled round the grip front (left side)
        ox, oy = hx - 6.0, hy - 6 + i * 4.2
        cv.ellipse(ox, oy, 3.0, 2.2, shade_ellipse(9.0 - i * 0.5, 2.4, ox, oy, 3.0, 2.2, fur))
        cv.set(round(ox - 3), round(oy + 1), "beige_11")

    # ---- muzzle effects
    if kind == "fire0":
        cx_, cy_ = round(mx), round(my) - 2
        for d in range(1, 7):
            for sx, sy in ((1, 0), (-1, 0), (0, -1), (1, -1), (-1, -1), (0, 1)):
                if sy and sx and d > 4:
                    continue
                cv.set(cx_ + sx * d, cy_ + sy * d, "fb_sun" if d < 3 else ("fb_yellow" if d < 5 else "fb_orange"))
        for ddx in (-1, 0, 1):
            for ddy in (-1, 0, 1):
                cv.set(cx_ + ddx, cy_ + ddy, "fb_sun")
    if kind == "fire1":
        cv.set(round(mx), round(my) - 1, "fb_orange")
        cv.set(round(mx) - 1, round(my) - 1, "fb_fire")
    return cv


if __name__ == "__main__":
    for k in FORE:
        cv = frame(k)
        cv.write("%s/vm3_%s.pix" % (SRC, k), "vm3_" + k, HDR + ["# %s." % k])
