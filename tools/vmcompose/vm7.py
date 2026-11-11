import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw import *

HDR = [
    "# View model 7, Sphinx Howler (weapons.md): rear view of a riveted steel housing carrying a sphinx",
    "# head in a striped gold-and-lapis nemes (sandstone cat ears, braided queue), with the flared",
    "# gold-and-lapis bell of its muzzle spreading out behind the head toward the view centre.",
    "# Roar frames: the bell plates open and fullbright sound rings leave the rim.",
    "# Matches HUD icon weapon_7. Composed in tools/vmcompose/vm7.py, checked at 3x in game.",
]

W, H = 128, 80
KINDS = ("idle", "roar0", "roar1", "roar2")
OPEN = {"idle": 0.0, "roar0": 2.5, "roar1": 4.5, "roar2": 3.0}
DROP = {"idle": 0, "roar0": 1, "roar1": 3, "roar2": 2}
BX, BY = 64.0, 30.0           # bell centre (on the aim line toward (64, -24))


def frame(kind):
    cv = Canvas(W, H)
    dy = DROP[kind]
    by = BY + dy
    rx, ry = 31.0 + OPEN[kind], 15.0 + OPEN[kind] * 0.5

    # ---- sound rings first so the bell sits over their inner edge
    rings = {"roar0": [(3.0, "fb_amber")],
             "roar1": [(3.0, "fb_sun"), (8.0, "fb_amber")],
             "roar2": [(6.0, "fb_amber"), (12.0, "fb_amber")]}.get(kind, [])
    for k, (d, c) in enumerate(rings):
        for i in range(720):
            a = math.radians(i * 0.5)
            if math.sin(a) > 0.55:
                continue                         # rings leave the rim, not the housing
            if kind == "roar2" and (i // 24) % 2:
                continue                         # last ring breaking up
            x = BX + (rx + d) * math.cos(a)
            y = by + (ry + d * 0.55) * math.sin(a)
            cv.set(round(x - 0.5), round(y - 0.5), c)
            if d < 4 and kind == "roar1":
                cv.set(round(x - 0.5), round(y + 0.5), c)

    # ---- bell: outer surface seen from behind, radial lapis stripes on gold, dark lip
    def bell(x, y):
        u, v = (x + 0.5 - BX) / rx, (y + 0.5 - by) / ry
        r = math.hypot(u, v)
        a = math.atan2(v, u)
        lit = 9.0 - (u + v) * 2.5
        if r > 0.9:
            return gold(4 if u + v > -0.4 else 8)   # rolled lip
        if (int((a + math.pi) / (2 * math.pi) * 20) % 2) == 1:
            return blue(lit - 1)
        return gold(lit)
    cv.ellipse(BX, by, rx, ry, bell)
    if kind != "idle":
        # gaps between the bell plates as they open
        for k in range(10):
            a = (k + 0.5) / 10 * 2 * math.pi - math.pi
            for f in (0.62, 0.72, 0.82):
                cv.set(round(BX + rx * f * math.cos(a) - 0.5), round(by + ry * f * math.sin(a) - 0.5), "blue_02")

    # ---- forearms from the bottom corners (under the housing)
    forearm(cv, (34.0, 66.0 + dy), (8.0, 90.0), 7.0, 10.0, [0.18, 0.4, 0.62, 0.84], seed=1)
    forearm(cv, (94.0, 66.0 + dy), (120.0, 90.0), 7.0, 10.0, [0.2, 0.42, 0.6, 0.83], seed=0)

    # ---- steel housing: lit top face, dark front-right, seam and rivets
    top = [(40, 56 + dy), (88, 56 + dy), (96, 64 + dy), (32, 64 + dy)]
    cv.poly(top, lambda x, y: steel(13 - (x - 32) / 64 * 4))
    cv.poly([(32, 64 + dy), (96, 64 + dy), (98, 81 + dy), (30, 81 + dy)],
            lambda x, y: steel(9 - (x - 30) / 68 * 5 - (1 if x in (63, 64) else 0)))
    cv.line((32, 64 + dy), (96, 64 + dy), steel(14))
    for rxp in (36, 50, 78, 92):
        cv.set(rxp, 68 + dy, steel(14))
        cv.set(rxp + 1, 69 + dy, steel(2))
    # ---- sight notch on the housing top
    cv.poly([(60, 57 + dy), (68, 57 + dy), (68, 60 + dy), (60, 60 + dy)], lambda x, y: steel(4))

    # ---- sphinx head from behind: nemes dome with horizontal stripes, lappets, ears, queue
    hx, hy = 64.0, 44.0 + dy
    def nemes(x, y):
        u = (x + 0.5 - hx) / 20.0
        v = (y + 0.5 - hy)
        lit = 9.0 - u * 3.0 - (v / 14.0)
        if abs(u) > 0.88:
            lit -= 2
        if int(v + 20) % 4 < 2:
            return gold(lit)
        return blue(lit)
    cv.poly([(50, 30 + dy), (78, 30 + dy), (84, 40 + dy), (86, 58 + dy), (42, 58 + dy), (44, 40 + dy)], nemes)
    cv.ellipse(hx, 31.0 + dy, 14.5, 5.5, nemes)             # rounded crown
    # cat ears (sandstone, seen from behind) above the nemes
    for sgn in (-1, 1):
        ex = hx + sgn * 10.0
        cv.poly([(ex - 6, 30 + dy), (ex + 6, 30 + dy), (ex + sgn * 4, 14 + dy)],
                lambda x, y, ex=ex: sand(12 if x + 0.5 < ex - 1 else (9 if x + 0.5 < ex + 2 else 6)))
    # sandstone cheek tufts showing either side of the nemes
    for sgn in (-1, 1):
        cv.poly([(hx + sgn * 21, 40 + dy), (hx + sgn * 26, 44 + dy), (hx + sgn * 22, 48 + dy)],
                lambda x, y, sgn=sgn: sand(9 if sgn < 0 else 6))
    # braided queue down the back: gold bands
    def queue(t, s):
        return gold((10 if s < 0 else 6) - (3 if int(t * 12) % 3 == 0 else 0))
    band(cv, (hx, 44.0 + dy), (hx, 59.0 + dy), 3.0, 2.2, queue)

    # ---- paws gripping the housing sides, toes over the top edge
    paw_fist(cv, 36.0, 64.0 + dy, 7.5, 5.5, [-4.8, -1.6, 1.6, 4.8],
             along=(0.55, -0.83), lobe_r=(2.4, 2.4), reach=6.6)
    paw_fist(cv, 92.0, 64.0 + dy, 7.5, 5.5, [-4.8, -1.6, 1.6, 4.8],
             along=(-0.55, -0.83), lobe_r=(2.4, 2.4), reach=6.6, body_dark=0.6)
    fill_holes(cv, "fur_03")
    return cv


if __name__ == "__main__":
    for k in KINDS:
        frame(k).write("%s/vm7_%s.pix" % (SRC, k), "vm7_" + k, HDR + ["# %s." % k])
