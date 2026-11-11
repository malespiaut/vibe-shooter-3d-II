#!/usr/bin/env python3
"""ctest `sprite_woofu`: the Woofu boss pre-rendered sprite sets (issue #126).

Builds assets/src/sprites/woofu/model.py and woofu2/model.py (the lidless
phase-2 variant) with Blender and renders both with tools/blender_render.py
(the same commands as tools/build.d/60-sprites.sh) into a temp dir, then
checks (enemies.md Boss and Animation list, art-bible.md):
- woofu_*: idle 2, walk 4, windup 2, attack 2, pain 1, phase 6, death 5 in
  8 directions (176 frames); woofu2_*: idle 2, walk 4, windup 2, attack 2,
  pain 1 (88 frames); every frame 128x112, origin 64,112; no gibs;
- interior rule: an opaque pixel whose right and bottom neighbours are
  opaque is never `black`, `shadow_*` or any ramp's `_00`; outline pixels
  (right/bottom edge) are a `_00` or fullbright;
- colour budget: only gold, blue, steel plus fb_cyan;
- the dominant ramp (gold) spans >= 8 entries in every standing frame;
- idle d0 opaque bounding box is about 96x96 (+-8 px), standing on the floor
  row; no frame touches the left, right or top canvas edge;
- the sceptre glow: windup f1 d0 shows more fb_cyan than windup f0 d0;
- the lid is gone after `phase`: woofu2 idle d0 differs from woofu idle d0
  and equals the lidless phase f5 pose in colour count of the steel cavity
  (more steel pixels than the lidded idle);
- the death f4 wreck is at most 48 px tall in every direction;
- palcheck passes on everything.
Exit 77 (ctest SKIP) when `blender` is not installed.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
SPRITES = os.path.join(ROOT, "assets", "src", "sprites")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import palette as gpl  # noqa: E402

W, H = 128, 112
BASE = {"idle": 2, "walk": 4, "windup": 2, "attack": 2, "pain": 1}
SETS = {"woofu": dict(BASE, phase=6, death=5), "woofu2": dict(BASE)}
RAMPS = ("gold", "blue", "steel")
FB = ("fb_cyan",)
SKIP = 77


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    assert p.returncode == 0, " ".join(cmd) + "\n" + p.stdout[-3000:] + p.stderr[-3000:]
    return p.stdout


def main():
    blender = shutil.which("blender")
    if not blender:
        print("sprite_woofu: SKIPPED, `blender` not found on PATH")
        return SKIP
    names = [n for _, n in gpl.load_gpl()]
    ramp_of = {}
    for i, n in enumerate(names):
        head, _, num = n.rpartition("_")
        if num.isdigit():
            ramp_of[i] = (head, int(num))
    fb = {names.index(n): n for n in FB}
    cyan = names.index("fb_cyan")

    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "sprites")
        for s in SETS:
            mdl = os.path.join(d, "model_" + s)
            run([blender, "-b", "--factory-startup", "--python",
                 os.path.join(SPRITES, s, "model.py"), "--", mdl])
            log = run([blender, "-b", os.path.join(mdl, s + ".blend"), "--python",
                       os.path.join(TOOLS, "blender_render.py"), "--", "--sprite", s,
                       "--canvas", "%dx%d" % (W, H), "--out", out])
            print([ln for ln in log.splitlines() if ln.startswith("blender_render:")][-1])

        want = sorted("%s_%s_d%d_f%d.tex" % (s, a, dd, f) for s, anims in SETS.items()
                      for a, n in anims.items() for dd in range(8) for f in range(n))
        got = sorted(os.listdir(out))
        assert got == want, (len(got), sorted(set(got) ^ set(want))[:10])
        n1 = sum(fn.startswith("woofu_") for fn in got)
        n2 = sum(fn.startswith("woofu2_") for fn in got)
        print("frames: woofu_* %d, woofu2_* %d" % (n1, n2))
        assert (n1, n2) == (176, 88)

        bad_interior = bad_outline = 0
        gold_min = 99
        stats = {}
        clipped = []
        for fn in want:
            t = bast.Tex.load(os.path.join(out, fn))
            assert (t.width, t.height) == (W, H), (fn, t.width, t.height)
            assert t.origin == (W // 2, H), (fn, t.origin)
            px = [[t.get(x, y) for x in range(W)] for y in range(H)]
            xs, ys, gold = [], [], set()
            steel = 0
            for y in range(H):
                for x in range(W):
                    i = px[y][x]
                    if i == 255:
                        continue
                    xs.append(x)
                    ys.append(y)
                    assert i in fb or (i in ramp_of and ramp_of[i][0] in RAMPS), \
                        (fn, x, y, names[i])
                    if i in ramp_of and ramp_of[i][0] == "gold":
                        gold.add(i)
                    if i in ramp_of and ramp_of[i][0] == "steel":
                        steel += 1
                    r = px[y][x + 1] if x + 1 < W else 255
                    b = px[y + 1][x] if y + 1 < H else 255
                    dark = (names[i] == "black" or names[i].startswith("shadow")
                            or (i in ramp_of and ramp_of[i][1] == 0))
                    if r == 255 or b == 255:
                        if not (i in fb or (i in ramp_of and ramp_of[i][1] == 0)):
                            bad_outline += 1
                    elif dark:
                        bad_interior += 1
            if not (min(xs) > 0 and max(xs) < W - 1 and min(ys) > 0):
                clipped.append((fn, min(xs), max(xs), min(ys)))
            if "_death_" in fn and fn.endswith("_f4.tex"):
                assert H - min(ys) <= 48, (fn, "wreck taller than 48 px", H - min(ys))
            if "_death_" not in fn:
                assert max(ys) == H - 1, (fn, "not standing on the floor row")
                gold_min = min(gold_min, len(gold))
            stats[fn] = {"cyan": sum(row.count(cyan) for row in px), "steel": steel,
                         "bbox": (max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)}
        assert not clipped, ("clipped at the canvas edge (x0, x1, y0)", clipped)
        bw, bh = stats["woofu_idle_d0_f0.tex"]["bbox"]
        print("idle d0 bounding box: %dx%d" % (bw, bh))
        assert abs(bw - 96) <= 8 and abs(bh - 96) <= 8, (bw, bh)
        for s in SETS:
            c0 = stats["%s_windup_d0_f0.tex" % s]["cyan"]
            c1 = stats["%s_windup_d0_f1.tex" % s]["cyan"]
            print("%s windup d0 fb_cyan pixels: f0 %d, f1 %d" % (s, c0, c1))
            assert c1 > c0 + 20, (s, c0, c1)
        st = (stats["woofu_idle_d0_f0.tex"]["steel"], stats["woofu2_idle_d0_f0.tex"]["steel"],
              stats["woofu_phase_d0_f5.tex"]["steel"])
        print("steel pixels in d0 (idle lidded, idle lidless, phase f5): %d %d %d" % st)
        assert st[1] > st[0] + 200 and st[2] > st[0] + 200, st
        print("interior dark-end pixels: %d, non-_00 outline pixels: %d, "
              "gold entries (min over standing frames): %d"
              % (bad_interior, bad_outline, gold_min))
        assert bad_interior == 0 and bad_outline == 0
        assert gold_min >= 8, gold_min
        print(run([sys.executable, os.path.join(TOOLS, "palcheck.py"), out]).strip())
    print("sprite_woofu: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
