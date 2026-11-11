import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 2, Ankh Pistol (weapons.md): 3/4 rear view. Steel slide receding toward the",
    "# view centre (lit top face, shaded right face) with a gold inlay along its length, gold",
    "# ankh-loop rear sight standing on top, wooden grip wrapped by Mau's right paw.",
    "# Matches HUD icon weapon_2. Shapes composed in 3D and projected, then checked at 3x in game.",
]


def frame(kind):
    cv = Canvas(64, 72)
    cam = Cam(f=80, cx=42, cy=0, off=(-2.4, -5.6, 12.0), roll=0.18)
    P = lambda x, y, z: cam((x, y, z))
    sb = -2.0 if kind == "fire1" else 0.0       # slide recoil (toward the viewer)

    # ---- frame (lower receiver), fixed
    box(cv, cam, -1.5, 1.5, -1.1, 0.0, 0.6, 20.0,
        top=lambda u, v: steel(9),
        side=lambda u, v: steel(5 - (v > 0.7)),
        rear=lambda u, v: steel(7), faces="sr")
    # ---- grip (wood), slanted back toward the viewer, fixed
    gq_rear = [P(-1.1, -1.1, 0.5), P(1.1, -1.1, 0.5), P(1.1, -13, -3.4), P(-1.1, -13, -3.4)]
    gq_side = [P(1.1, -1.1, 0.5), P(1.1, -1.1, 3.2), P(1.1, -13, 0.4), P(1.1, -13, -3.4)]
    cv.poly(gq_side, lambda x, y: brown(5))
    cv.poly(gq_rear, lambda x, y: brown(10 - (x % 3 == 0 and y % 2 == 0) * 2))
    # ---- slide
    z0, z1 = 0.0 + sb, 24.0 + sb
    xa, xb, ya, yb = -1.7, 1.7, 0.0, 1.6

    def top(u, v):          # u along length, v across (0 = left)
        if v < 0.12:
            return steel(15)
        if 0.72 <= v <= 0.9:
            return gold(10 - u * 3 if v < 0.81 else 7 - u * 3)
        if v > 0.9:
            return steel(10)
        return steel(14 - v * 2 - u * 1.5)

    def side(u, v):         # v: 0 top edge -> 1 bottom edge
        if 0.32 <= v <= 0.58:
            return gold(8 - u * 2 if v < 0.45 else 5 - u * 2)
        if v < 0.15:
            return steel(9)
        return steel(6 - u * 1.5 - (v > 0.85))

    def rear(u, v):         # u across (0 = left), v down
        if u < 0.1 or v < 0.15:
            return steel(12)
        if 0.42 < u < 0.58 and 0.25 < v < 0.75:
            return steel(4)         # firing-pin channel
        return steel(8 - (v > 0.75) * 2)
    q = box(cv, cam, xa, xb, ya, yb, z0, z1, top, side, rear)
    # serrations on the side face near the rear
    for i in range(3):
        z = z0 + 1.0 + i * 0.9
        cv.line(P(xb, yb - 0.4, z), P(xb, ya + 0.4, z), steel(3))
    # barrel tip just visible at the muzzle end
    mx, my = P(0, yb, z1)
    cv.set(round(mx), round(my), steel(2))
    cv.set(round(mx) - 1, round(my), steel(2))

    # ---- ankh-loop rear sight, standing on the top rear of the slide
    bx, by = P(0, yb, z0 + 1.0)
    tx, ty = P(0, yb + 3.6, z0 + 1.0)
    bx, by, tx, ty = round(bx), round(by), round(tx), round(ty)
    cyb = by - (by - ty) * 0.42          # crossbar height
    # stem
    n = int(by - cyb)
    for i in range(n + 1):
        x = round(bx + (tx - bx) * i / (by - ty))
        y = by - i
        cv.set(x, y, gold(9))
        cv.set(x + 1, y, gold(5))
    cxx = round(bx + (tx - bx) * 0.42)
    cyb = round(cyb)
    for x in range(cxx - 4, cxx + 6):
        cv.set(x, cyb, gold(10 if x <= cxx else 8))
        cv.set(x, cyb + 1, gold(4))
    # loop (ring, open centre)
    lcx, lcy = (cxx + tx) / 2 + 0.6, (cyb + ty) / 2 - 0.3
    rx, ry = 3.4, (cyb - ty) / 2 + 0.3
    for y in range(int(lcy - ry) - 1, int(lcy + ry) + 2):
        for x in range(int(lcx - rx) - 1, int(lcx + rx) + 2):
            dx, dy = (x + 0.5 - lcx) / rx, (y + 0.5 - lcy) / ry
            r = math.hypot(dx, dy)
            if 0.5 <= r <= 1.0:
                cv.set(x, y, gold(7 - (dx + dy) * 2.5))
            elif r < 0.5:
                cv.px[y][x] = None if cv.get(x, y) is None else cv.get(x, y)

    # ---- Mau's right paw wrapping the grip, forearm off the bottom-right
    gx, gy = P(0, -3.6, 0.3)
    stripe = lambda x, y: ((x * 2 + y * 3) // 4) % 5 == 0
    # forearm
    arm = [(gx - 2, gy + 6), (gx + 13, gy - 1), (66, 60), (66, 72), (gx - 4, 72)]
    cv.poly(arm, lambda x, y: fur(max(5, 8 - (x - gx) * 0.07 - (y - gy) * 0.05) - (3 if stripe(x, y) else 0)))
    # back of the paw over the right side and back of the grip
    cv.ellipse(gx + 4.5, gy + 1.5, 9.5, 8.5, shade_ellipse(8.2, 3.4, gx + 4.5, gy + 1.5, 9.5, 8.5, fur,
                                                         stripes=lambda x, y: (x + y) % 7 == 0 and x > gx + 6))
    # curled fingers around the grip front, tips showing on the left side
    for i, (ox, oy) in enumerate([(-5.2, -4.6), (-6.2, -0.6), (-6.0, 3.4), (-4.6, 7.0)]):
        fx, fy = gx + ox, gy + oy
        cv.ellipse(fx, fy, 3.3, 2.3, shade_ellipse(9.2 - i * 0.6, 2.6, fx, fy, 3.3, 2.3, fur))
        # sheathed claw tip peeking out, pointing down-left
        cv.set(round(fx - 3.2), round(fy + 0.6), "beige_13")
        cv.set(round(fx - 3.2), round(fy + 1.4), "beige_09")
    # creases between fingers
    for oy in (-2.6, 1.4, 5.2):
        cv.line((gx - 8.5, gy + oy), (gx - 4, gy + oy + 0.6), fur(3))

    # ---- muzzle effects
    if kind == "fire0":
        fx, fy = round(mx), round(my) - 3
        pts = {(0, 0): ";", }
        for d in range(1, 6):
            for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                cv.set(fx + sx * d, fy + sy * d, "fb_cyan" if d < 3 else "fb_cyan_dim")
        for d in range(1, 4):
            for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                cv.set(fx + sx * d, fy + sy * d, "fb_cyan" if d < 2 else "fb_cyan_dim")
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                cv.set(fx + dx, fy + dy, "fb_white")
    if kind == "fire1":
        cv.set(round(mx), round(my), "fb_orange")
        cv.set(round(mx) - 1, round(my), "fb_fire")
    return cv


if __name__ == "__main__":
    for k in ("idle", "fire0", "fire1"):
        cv = frame(k)
        cv.write("%s/vm2_%s.pix" % (SRC, k), "vm2_" + k, HDR + ["# %s." % k])
