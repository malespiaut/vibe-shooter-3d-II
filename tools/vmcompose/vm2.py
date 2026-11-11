import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 2, Ankh Pistol (weapons.md): 3/4 rear view. Long steel slide receding toward the",
    "# view centre (lit top face, shaded right face, dark bore at the muzzle) with a gold inlay, gold",
    "# ankh-loop rear sight on top, steel frame and trigger guard under it, wooden grip (grain along",
    "# its axis) wrapped by Mau's right paw (toe lobes curled round the front of the grip).",
    "# Matches HUD icon weapon_2. Composed in tools/vmcompose/vm2.py, checked at 3x in game.",
]

# Slide: rear face 2.6 wide at z=14 -> ~13 px; 30 units long -> ~29 px on screen.
SW, SH, SL = 1.3, 1.6, 30.0


def frame(kind):
    cv = Canvas(64, 72)
    cam = Cam(f=70, cx=27, cy=-12, off=(2.3, -10.5, 14.0), roll=0.06)
    P = lambda x, y, z: cam((x, y, z))
    sb = -1.6 if kind == "fire1" else 0.0       # slide recoil (toward the viewer)

    # ---- grip (wood), raked back toward the viewer, grain along its axis
    g_top = (-1.0, 1.0, 0.0, 2.6)                # x0, x1, z_rear, z_front at the top
    yb = -8.0                                   # grip bottom (off the canvas)
    rake = -2.4
    gr = [P(-1.0, -1.0, 0.0), P(1.0, -1.0, 0.0), P(1.0, yb, rake), P(-1.0, yb, rake)]
    gs = [P(1.0, -1.0, 0.0), P(1.0, -1.0, 2.6), P(1.0, yb, 2.6 + rake), P(1.0, yb, rake)]

    def grain(x, y, base):
        # lines along the grip axis: dark every 3rd column, broken by a slow wave
        k = (x * 3 + (y // 5)) % 7
        return brown(base - (3 if k == 0 else (1 if k == 4 else 0)))
    cv.poly(gs, lambda x, y: grain(x, y, 6))
    cv.poly(gr, lambda x, y: grain(x, y, 10))
    # ---- frame (lower receiver) under the slide, and the trigger guard
    box(cv, cam, -1.0, 1.0, -1.2, 0.0, 0.0, 15.0,
        top=lambda u, v: steel(9),
        side=lambda u, v: steel(6 - u * 2 - (v > 0.7)),
        rear=lambda u, v: steel(8 - (v > 0.6) * 2), faces="sr")
    # ---- slide
    z0, z1 = 0.0 + sb, SL + sb
    xa, xb, ya, yb2 = -SW, SW, 0.0, SH

    def top(u, v):          # u along length, v across (0 = left)
        if v < 0.14:
            return steel(15 - u * 2)
        if 0.80 <= v <= 0.92:
            return gold(8 - u * 3)
        if v > 0.92:
            return steel(10 - u * 2)
        return steel(13 - v * 2 - u * 3)

    def side(u, v):         # v: 0 top edge -> 1 bottom edge
        if 0.30 <= v <= 0.55:
            return gold(7 - u * 3 if v < 0.43 else 4 - u * 2)
        if v < 0.14:
            return steel(10 - u * 2)
        return steel(6 - u * 2 - (v > 0.85))

    def rear(u, v):         # u across (0 = left), v down
        if u < 0.1 or v < 0.15:
            return steel(12)
        if 0.40 < u < 0.60 and 0.30 < v < 0.75:
            return steel(3)         # firing-pin channel
        if u > 0.9 or v > 0.82:
            return steel(5)
        return steel(8)
    box(cv, cam, xa, xb, ya, yb2, z0, z1, top, side, rear)
    # slide/frame seam and the frame's beavertail tang under the slide rear
    cv.line(P(-1.0, 0.0, z0), P(1.0, 0.0, z0), steel(2))
    cv.line(P(-0.7, -0.5, -0.6), P(0.7, -0.5, -0.6), steel(11))
    cv.line(P(-0.7, -0.9, -0.6), P(0.7, -0.9, -0.6), steel(6))
    # serrations on the side face near the rear
    for i in range(3):
        z = z0 + 1.2 + i * 1.2
        cv.line(P(xb, yb2 - 0.3, z), P(xb, ya + 0.3, z), steel(3))
    # muzzle: barrel bushing ring and dark bore at the far end
    mx, my = P(0, yb2 * 0.55, z1)
    mx, my = round(mx), round(my)
    for ddx, ddy, c in ((-1, -1, steel(9)), (0, -1, steel(9)), (1, -1, steel(7)),
                        (-1, 0, steel(2)), (0, 0, steel(1)), (1, 0, steel(2)),
                        (-1, 1, steel(5)), (0, 1, steel(4)), (1, 1, steel(4))):
        cv.set(mx + ddx, my - 1 + ddy, c)
    # front sight post
    fx, fy = P(0, yb2, z1 - 1.5)
    cv.set(round(fx), round(fy) - 1, steel(13))

    # ---- ankh-loop rear sight on the rear of the slide, skewed to follow the slide
    bx, by = P(0, yb2, z0 + 1.6)
    bx, by = round(bx), round(by)
    lean = (27 - bx) / 40.0                   # leans toward the vanishing point
    stem = 6
    for i in range(stem):
        x = round(bx + lean * i)
        cv.set(x - 1, by - i, gold(11))
        cv.set(x, by - i, gold(8))
        cv.set(x + 1, by - i, gold(3))
    cyb = by - 3
    cxx = round(bx + lean * 3)
    for x in range(cxx - 3, cxx + 5):
        cv.set(x, cyb + (1 if x > cxx + 2 else 0), gold(10 if x <= cxx else 7))
        cv.set(x, cyb + 1 + (1 if x > cxx + 2 else 0), gold(4))
    lcx, lcy = cxx + 0.9 + lean * 3, cyb - 3.4
    rx, ry = 2.6, 3.2
    for y in range(int(lcy - ry) - 1, int(lcy + ry) + 2):
        for x in range(int(lcx - rx) - 1, int(lcx + rx) + 2):
            ddx, ddy = (x + 0.5 - lcx) / rx, (y + 0.5 - lcy) / ry
            r = math.hypot(ddx, ddy)
            if 0.5 <= r <= 1.0:
                cv.set(x, y, gold(8 - (ddx + ddy) * 3))
            elif r < 0.5 and cv.get(x, y) and cv.get(x, y).startswith("gold"):
                cv.set(x, y, steel(11))

    # ---- forearm, then Mau's right paw over the lower half of the grip
    gx, gy = P(0.4, -3.6, -0.8)
    forearm(cv, (gx + 2, gy + 4), (gx + 34, 84), 8.0, 11.0, [0.18, 0.37, 0.52, 0.74, 0.9], seed=1)
    paw_fist(cv, gx + 3.5, gy + 2.0, 8.5, 7.5, [-6.0, -1.6, 2.8, 7.0],
             along=(-0.86, -0.5), lobe_r=(2.9, 2.5))
    # thumb over the top of the grip, toward the frame
    tx, ty = gx + 5.5, gy - 6.0
    cv.ellipse(tx, ty, 2.6, 3.4, lambda x, y: fur(9.6 - ((x + 0.5 - tx) + (y + 0.5 - ty)) * 0.9))
    cv.set(round(tx - 1), round(ty - 2), "fur_11")

    # ---- muzzle effects
    if kind == "fire0":
        fx, fy = mx, my - 4
        for d in range(1, 6):
            for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                cv.set(fx + sx * d, fy + sy * d, "fb_cyan" if d < 3 else "fb_cyan_dim")
        for d in range(1, 4):
            for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                cv.set(fx + sx * d, fy + sy * d, "fb_cyan" if d < 2 else "fb_cyan_dim")
        for ddx in (-1, 0, 1):
            for ddy in (-1, 0, 1):
                cv.set(fx + ddx, fy + ddy, "fb_white")
    if kind == "fire1":
        cv.set(mx, my - 1, "fb_orange")
        cv.set(mx - 1, my - 1, "fb_fire")
    return cv


if __name__ == "__main__":
    for k in ("idle", "fire0", "fire1"):
        cv = frame(k)
        cv.write("%s/vm2_%s.pix" % (SRC, k), "vm2_" + k, HDR + ["# %s." % k])
