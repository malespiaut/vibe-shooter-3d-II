import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 2, Ankh Pistol (weapons.md): 3/4 rear view, canted. Square steel slide of near-constant",
    "# width receding toward the view centre (lit top face on the left, shaded side face with the gold",
    "# inlay on the right, flat muzzle with a bushing ring and dark bore), gold ankh-loop rear sight",
    "# standing proud of the top face (top face seen through the loop), steel frame, wooden grip wrapped by Mau's right paw.",
    "# Matches HUD icon weapon_2. Composed in tools/vmcompose/vm2.py, checked at 3x in game.",
]

# Screen-space layout (64x72 canvas, the view centre is at about (32, -32)).
# Round 5: a chunky gun that fills the frame. The slide is 26 px across at the
# rear face, the wooden grip shows 10+ rows below the frame before the paw, and
# a steel trigger guard loop hangs in front of (screen left of) the grip.
REAR = (34.0, 41.0)          # top-rear edge of the slide (rear face below it)
AIM = (28.0, -40.0)
SLIDE = 30.0                 # top face length in px, rear edge -> muzzle
RFACE = 8.0                  # rear face height in px
W_REAR, W_MUZ = 13.0, 10.0   # half-widths: 26 px at the rear, 20 px at the muzzle
TOP = -0.08                  # s below this is the lit top face, above it the side face
D, N, _ = axis(REAR, AIM)


def at(p, t, s=0.0):
    return (p[0] + D[0] * t + N[0] * s, p[1] + D[1] * t + N[1] * s)


def frame(kind):
    cv = Canvas(64, 72)
    back = 2.5 if kind == "fire1" else 0.0      # slide recoil toward the viewer
    rear = at(REAR, -back)
    fbot = at(REAR, -RFACE - 3.0)               # bottom of the steel frame

    # ---- wooden grip: down and right to the bottom edge, grain along its axis
    def grip(t, s):
        base = 11.5 - (s + 1) * 2.8
        k = int((s + 1) * 4.6)
        if k in (2, 6):
            base -= 3                            # checkering grooves
        elif k == 4 and int(t * 12) % 3 == 0:
            base -= 1.5
        if s < -0.8:
            base = 12.5                          # lit front edge
        if t < 0.06:
            base -= 3                            # shadow under the frame
        return brown(base)
    band(cv, (fbot[0] + 2.0, fbot[1] - 1.0), (fbot[0] + 13.0, 76.0), 7.4, 8.2, grip)

    # ---- trigger guard: a 1 px steel loop hanging in front of the grip
    gx0, gy0 = int(round(fbot[0])) - 12, int(round(fbot[1]))
    for i in range(8):
        for j in range(7):
            edge = i in (0, 7) or j == 6
            if edge and not (j == 6 and i in (0, 7)):
                cv.set(gx0 + i, gy0 + j, steel(12 if i == 0 else (6 if j == 6 else 9)))
    cv.set(gx0 + 1, gy0 + 6, steel(8))
    cv.set(gx0 + 6, gy0 + 6, steel(6))
    for j in range(0, 4):                       # trigger blade
        cv.set(gx0 + 4 - (j > 2), gy0 + j, steel(10 if j < 2 else 7))

    # ---- steel frame under the slide's rear: dark seam at the top
    band(cv, at(REAR, -RFACE + 0.5), fbot, W_REAR - 0.5, W_REAR - 1.5,
         lambda t, s: steel(2 if t < 0.25 else (11 if s < -0.8 else (4 if s > 0.75 else 7))))

    # ---- slide: rear face (near end) then the top / side faces up to the muzzle
    total = RFACE + SLIDE

    def slide(t, s):
        d = t * total                            # px from the bottom of the rear face
        if d < RFACE:                            # rear face
            if d > RFACE - 1.0:
                return steel(13)                 # top-rear edge catches the light
            if s < -0.85:
                return steel(11)
            if s > 0.86 or d < 1.0:
                return steel(4)
            if abs(s + 0.05) < 0.14 and 2.0 <= d < 6.0:
                return steel(2)                  # firing-pin channel
            if abs(s - TOP) < 0.04:
                return steel(6)
            return steel(9 if s < TOP else 7)
        u = (d - RFACE) / SLIDE                  # 0 rear .. 1 muzzle
        if d > total - 1.0:
            return steel(10 if s < TOP else 5)   # flat muzzle edge
        if s < -0.9:
            return steel(15 - u * 2)             # lit left rim
        if s < TOP - 0.06:
            return steel(13 - u * 2 - (s + 0.9) * 1.6)   # top face
        if s < TOP + 0.06:
            return steel(8 - u)                  # top / side edge
        if 0.36 <= s <= 0.5:
            return gold(8.5 - u * 3 if s < 0.43 else 5.5 - u * 2)   # inlay
        if s > 0.88:
            return steel(3)
        if 1.5 < d - RFACE < 9.5 and int(d - RFACE) % 2 == 0 and s > 0.05:
            return steel(3)                      # serrations
        return steel(7 - u * 2 - s * 1.5)        # side face
    band(cv, at(rear, -RFACE), at(rear, SLIDE), W_REAR, W_MUZ, slide)

    # ---- muzzle: 4x4 bushing ring (lit top-left) around a 2x2 dark bore
    mx, my = at(rear, SLIDE - 3.5, -2.0)
    mx, my = int(round(mx)) - 2, int(round(my)) - 2
    for j in range(-1, 5):                      # dark collar so the ring reads at 1x
        for i in range(-1, 5):
            cv.set(mx + i, my + j, "steel_04")
    ring = ["LMLJ", "M..H", "J..G", "IHGF"]
    for j, row in enumerate(ring):
        for i, ch in enumerate(row):
            c = "steel_01" if ch == "." else "steel_%02d" % (ord(ch) - ord("A") + 1)
            cv.set(mx + i, my + j, c)
    muzzle = (mx + 2, my + 2)

    # ---- ankh rear sight: upright on the top face at the rear end, stem rooted
    # just in front of the rear edge, loop pointing up-screen toward the muzzle.
    bx, by = at(rear, 0.5, -6.5)
    ux, uy = D                                  # "up" along the slide on screen
    vx, vy = -N[0], -N[1]
    ank = {}
    for y in range(cv.h):
        for x in range(cv.w):
            px, py = x + 0.5 - bx, y + 0.5 - by
            a = px * ux + py * uy               # height along the slide
            c = -(px * vx + py * vy)            # across, + = screen right
            col = None
            if 0.0 <= a < 7.0 and abs(c) < 1.2:
                col = gold(10.5 if c < 0 else 6)                            # stem
            if 3.4 <= a < 5.0 and abs(c) < 4.4:
                col = gold(10 if a < 4.2 else 5)                            # cross
            ea, ec = (a - 10.6) / 4.4, c / (3.5 - max(0.0, 9.0 - a) * 0.28)
            r = math.hypot(ea, ec)
            if r <= 1.0:
                col = gold(8.5 + (ea - ec) * 2.4) if r >= 0.5 else "hole"   # loop
            if col:
                ank[(x, y)] = col
    for (x, y), col in ank.items():             # dark outline so gold reads on steel
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (x + ox, y + oy) not in ank:
                cv.set(x + ox, y + oy, "steel_02")
    for (x, y), col in ank.items():
        if col == "hole":
            # the loop stands over the top face: the lit top-face steel shows
            # through it, with a 1 px shadow along its inner lower-right edge
            ring = lambda ox, oy: ank.get((x + ox, y + oy), "hole") != "hole"
            if ring(0, 1):
                cv.set(x, y, steel(7))
            elif ring(1, 0) or ring(1, 1):
                cv.set(x, y, steel(8))
            else:
                cv.set(x, y, steel(12))
        else:
            cv.set(x, y, col)

    # ---- forearm, then Mau's right paw over the lower half of the grip
    gx, gy = 46.0, 70.5
    forearm(cv, (gx + 2, gy + 3), (gx + 30, 88), 8.0, 11.0, [0.18, 0.37, 0.52, 0.74, 0.9], seed=1)
    paw_fist(cv, gx + 1.0, gy + 1.5, 8.0, 5.5, [-4.4, -1.5, 1.5, 4.4],
             along=(-0.96, -0.28), lobe_r=(2.6, 2.2), reach=7.2)

    # ---- muzzle effects
    if kind == "fire0":
        fx, fy = at(rear, SLIDE + 4.5, -1.5)      # ankh charge in front of / above the muzzle
        flash(cv, fx, fy, 6.0, 6.0, "fb_white", "fb_cyan", "fb_cyan_dim",
              rays=[(1, 1, 2), (-1, 1, 2), (1, -1, 2), (-1, -1, 2), (0, -1, 2)], ray_c="fb_cyan_dim")
    if kind == "fire1":
        cv.set(muzzle[0] - 1, muzzle[1] - 1, "fb_cyan_dim")
        cv.set(muzzle[0], muzzle[1] - 1, "fb_cyan_dim")
    return cv


def measure():
    cv = frame("idle")
    print("vm2 slide: rear face %.1f px across, muzzle %.1f px across (%.0f%% narrower)"
          % (2 * W_REAR, 2 * W_MUZ, 100 * (1 - W_MUZ / W_REAR)))
    print("vm2_fire0 fullbright px:", count_fb(frame("fire0")))


if __name__ == "__main__":
    for k in ("idle", "fire0", "fire1"):
        cv = frame(k)
        cv.write("%s/vm2_%s.pix" % (SRC, k), "vm2_" + k, HDR + ["# %s." % k])
    if "-m" in sys.argv:
        measure()
