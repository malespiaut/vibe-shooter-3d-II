import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 2, Ankh Pistol (weapons.md): 3/4 rear view, canted. Square steel slide of near-constant",
    "# width receding toward the view centre (lit top face on the left, shaded side face with the gold",
    "# inlay on the right, flat muzzle with a bushing ring and dark bore), gold ankh-loop rear sight",
    "# standing proud of the top face (open loop), steel frame, wooden grip wrapped by Mau's right paw.",
    "# Matches HUD icon weapon_2. Composed in tools/vmcompose/vm2.py, checked at 3x in game.",
]

# Screen-space layout (64x72 canvas, the view centre is at about (32, -32)).
REAR = (42.0, 50.0)          # top-rear edge of the slide (rear face below it)
AIM = (30.0, -32.0)
SLIDE = 34.0                 # top face length in px, rear edge -> muzzle
RFACE = 7.0                  # rear face height in px
W_REAR, W_MUZ = 7.0, 5.5     # half-widths: 14 px at the rear, 11 px at the muzzle
D, N, _ = axis(REAR, AIM)


def at(p, t, s=0.0):
    return (p[0] + D[0] * t + N[0] * s, p[1] + D[1] * t + N[1] * s)


def frame(kind):
    cv = Canvas(64, 72)
    back = 2.5 if kind == "fire1" else 0.0      # slide recoil toward the viewer
    rear = at(REAR, -back)

    # ---- wooden grip: down and right to the bottom edge, grain along its axis
    def grip(t, s):
        base = 11 - (s + 1) * 2.6
        k = int((s + 1) * 4.6)
        if k in (2, 6):
            base -= 3
        elif k == 4 and int(t * 9) % 3 == 0:
            base -= 1
        return brown(base)
    band(cv, (42.5, 54.0), (50.0, 75.0), 5.2, 6.4, grip)
    # ---- steel frame under the slide's rear: dark seam at the top, tang
    band(cv, at(REAR, -RFACE + 1.0), at(REAR, -RFACE - 5.0), W_REAR - 0.6, 6.0,
         lambda t, s: steel(3 if t < 0.2 else (10 if s < -0.55 else (5 if s > 0.6 else 7))))

    # ---- slide: rear face (near end) then the top / side faces up to the muzzle
    total = RFACE + SLIDE

    def slide(t, s):
        d = t * total                            # px from the bottom of the rear face
        if d < RFACE:                            # rear face
            if d > RFACE - 1.0:
                return steel(13)                 # top-rear edge catches the light
            if s < -0.78:
                return steel(11)
            if s > 0.8 or d < 1.0:
                return steel(4)
            if abs(s + 0.05) < 0.22 and 2.0 <= d < 5.0:
                return steel(2)                  # firing-pin channel
            return steel(8)
        u = (d - RFACE) / SLIDE                  # 0 rear .. 1 muzzle
        if d > total - 1.0:
            return steel(10 if s < -0.1 else 5)  # flat muzzle edge
        if s < -0.85:
            return steel(15 - u * 2)             # lit left rim
        if s < -0.18:
            return steel(13 - u * 2 - (s + 0.85) * 1.5)   # top face
        if s < -0.04:
            return steel(9 - u)                  # top / side edge
        if 0.32 <= s <= 0.52:
            return gold(8 - u * 3 if s < 0.42 else 5 - u * 2)   # inlay
        if s > 0.84:
            return steel(3)
        if 1.5 < d - RFACE < 7.5 and int(d - RFACE) % 2 == 0 and s > 0.0:
            return steel(3)                      # serrations
        return steel(6.5 - u * 2 - s)            # side face
    band(cv, at(rear, -RFACE), at(rear, SLIDE), W_REAR, W_MUZ, slide)

    # ---- muzzle: 4x4 bushing ring (lit top-left) around a 2x2 dark bore
    mx, my = at(rear, SLIDE - 3.5, 0.0)
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

    # ---- ankh rear sight standing up from the top face, proud of the silhouette
    bx, by = at(rear, 5.0, -W_REAR - 0.5)       # foot on the top-left edge
    ux, uy = -0.55, -0.84                      # "up" from the canted top face on screen
    L = math.hypot(ux, uy)
    ux, uy = ux / L, uy / L
    vx, vy = -uy, ux                            # across the sight
    for y in range(cv.h):
        for x in range(cv.w):
            px, py = x + 0.5 - bx, y + 0.5 - by
            a = px * ux + py * uy               # height above the top face
            c = px * vx + py * vy               # along the slide
            col = None
            if -1.0 <= a < 5.0 and abs(c) < 1.5:
                col = gold(10.5 if c < -0.5 else (7.5 if c < 0.5 else 4))   # stem
            if 2.0 <= a < 3.6 and abs(c) < 4.6:
                col = gold(10 if a < 2.8 else 5)                            # cross
            ea, ec = (a - 9.6) / 4.2, c / 3.3
            r = math.hypot(ea, ec)
            if r <= 1.0:
                if r >= 0.5:
                    col = gold(8.5 - (ec - ea) * 2.6)                       # loop
                else:
                    col = "hole"
            if col == "hole":
                cv.px[y][x] = None                                          # see-through
            elif col:
                cv.set(x, y, col)

    # ---- forearm, then Mau's right paw over the lower half of the grip
    gx, gy = 44.0, 68.0
    forearm(cv, (gx + 2, gy + 4), (gx + 34, 86), 8.0, 11.0, [0.18, 0.37, 0.52, 0.74, 0.9], seed=1)
    paw_fist(cv, gx + 3.5, gy + 2.0, 8.5, 7.5, [-6.0, -1.6, 2.8, 7.0],
             along=(-0.86, -0.5), lobe_r=(2.9, 2.5))
    tx, ty = gx + 5.5, gy - 6.0                 # thumb over the top of the grip
    cv.ellipse(tx, ty, 2.6, 3.4, lambda x, y: fur(9.6 - ((x + 0.5 - tx) + (y + 0.5 - ty)) * 0.9))
    cv.set(round(tx - 1), round(ty - 2), "fur_11")

    # ---- muzzle effects
    if kind == "fire0":
        fx, fy = at(rear, SLIDE + 6.5, -1.0)      # ankh charge in front of / above the muzzle
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
