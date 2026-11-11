#!/usr/bin/env python3
"""ctest `sprite_mutt`: the Mutt Trooper pre-rendered sprite set (issue #91).

Builds assets/src/sprites/mutt/mutt_model.py with Blender, renders it with
tools/blender_render.py (the same commands as tools/build.d/60-sprites.sh)
into a temp dir, makes the gib frames with tools/gibs.py from
assets/src/gibs/mutt.json, then checks (enemies.md, art-bible.md):
- exactly the 128 frames mutt_<anim>_d<0..7>_f<n>.tex for idle 2, walk 4,
  windup 2, attack 2, pain 1, death 5; every frame 48x64, origin 24,64;
- interior rule: an opaque pixel whose right and bottom neighbours are
  opaque (not the selective outline) is never `black`, `shadow_*` or any
  ramp's `_00`; outline pixels are a `_00` or a fullbright entry;
- colour budget: only olive, beige, steel plus fb_red / fb_yellow;
- the dominant ramp (olive) spans >= 8 entries in every standing frame;
- idle d0 opaque bounding box is 32x56 +-2 px, standing on the floor row;
  no frame touches the left, right or top canvas edge (nothing clipped);
- the wind-up LED (fb_red) shows on windup f1 only among windup frames, in
  d0; the muzzle flash (fb_yellow) on attack f0 only;
- 8 gib frames mutt_gib_d0_f<0..7>.tex; palcheck passes on everything.
Exit 77 (ctest SKIP) when `blender` is not installed.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
SRC = os.path.join(ROOT, "assets", "src", "sprites", "mutt")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import palette as gpl  # noqa: E402

W, H = 48, 64
ANIMS = {"idle": 2, "walk": 4, "windup": 2, "attack": 2, "pain": 1, "death": 5}
RAMPS = ("olive", "beige", "steel")
FB = ("fb_red", "fb_yellow")
SKIP = 77


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    assert p.returncode == 0, " ".join(cmd) + "\n" + p.stdout[-3000:] + p.stderr[-3000:]
    return p.stdout


def main():
    blender = shutil.which("blender")
    if not blender:
        print("sprite_mutt: SKIPPED, `blender` not found on PATH")
        return SKIP
    names = [n for _, n in gpl.load_gpl()]
    ramp_of = {}
    for i, n in enumerate(names):
        head, _, num = n.rpartition("_")
        if num.isdigit():
            ramp_of[i] = (head, int(num))
    fb = {names.index(n): n for n in FB}

    with tempfile.TemporaryDirectory() as d:
        mdl, out, gib = (os.path.join(d, s) for s in ("model", "sprites", "gibs"))
        run([blender, "-b", "--factory-startup", "--python",
             os.path.join(SRC, "mutt_model.py"), "--", mdl])
        shutil.copy(os.path.join(SRC, "sprite.json"), mdl)
        line = run([blender, "-b", os.path.join(mdl, "mutt.blend"), "--python",
                    os.path.join(TOOLS, "blender_render.py"), "--", "--sprite", "mutt",
                    "--canvas", "%dx%d" % (W, H), "--out", out])
        print([ln for ln in line.splitlines() if ln.startswith("blender_render:")][-1])

        want = sorted("mutt_%s_d%d_f%d.tex" % (a, dd, f)
                      for a, n in ANIMS.items() for dd in range(8) for f in range(n))
        got = sorted(os.listdir(out))
        assert len(want) == 128 and got == want, (len(got), set(got) ^ set(want))

        bad_interior = bad_outline = 0
        olive_min = 99
        for fn in want:
            t = bast.Tex.load(os.path.join(out, fn))
            assert (t.width, t.height) == (W, H), (fn, t.width, t.height)
            assert t.origin == (W // 2, H), (fn, t.origin)
            px = [[t.get(x, y) for x in range(W)] for y in range(H)]
            xs, ys, olive = [], [], set()
            for y in range(H):
                for x in range(W):
                    i = px[y][x]
                    if i == 255:
                        continue
                    xs.append(x)
                    ys.append(y)
                    assert i in fb or (i in ramp_of and ramp_of[i][0] in RAMPS), \
                        (fn, x, y, names[i])
                    if i in ramp_of and ramp_of[i][0] == "olive":
                        olive.add(i)
                    r = px[y][x + 1] if x + 1 < W else 255
                    b = px[y + 1][x] if y + 1 < H else 255
                    dark = (names[i] == "black" or names[i].startswith("shadow")
                            or (i in ramp_of and ramp_of[i][1] == 0))
                    if r == 255 or b == 255:
                        if not (i in fb or (i in ramp_of and ramp_of[i][1] == 0)):
                            bad_outline += 1
                    elif dark:
                        bad_interior += 1
            assert min(xs) > 0 and max(xs) < W - 1 and min(ys) > 0, \
                ("clipped at the canvas edge", fn, min(xs), max(xs), min(ys))
            if not fn.startswith("mutt_death"):
                assert max(ys) == H - 1, (fn, "not standing on the floor row")
                olive_min = min(olive_min, len(olive))
            if fn == "mutt_idle_d0_f0.tex":
                bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
                print("idle d0 bounding box: %dx%d" % (bw, bh))
                assert abs(bw - 32) <= 2 and abs(bh - 56) <= 2, (bw, bh)
            red = sum(row.count(names.index("fb_red")) for row in px)
            yel = sum(row.count(names.index("fb_yellow")) for row in px)
            if fn.startswith("mutt_windup_d0"):
                assert (red > 0) == fn.endswith("_f1.tex"), (fn, red)
            if fn.startswith("mutt_attack_d0") or fn.startswith("mutt_attack_d2"):
                assert (yel > 0) == fn.endswith("_f0.tex"), (fn, yel)
            if not fn.startswith(("mutt_windup", "mutt_attack")):
                assert red == 0 and yel == 0, (fn, red, yel)
        print("interior dark-end pixels: %d, non-_00 outline pixels: %d, "
              "olive entries (min over standing frames): %d"
              % (bad_interior, bad_outline, olive_min))
        assert bad_interior == 0 and bad_outline == 0
        assert olive_min >= 8, olive_min

        run([sys.executable, os.path.join(TOOLS, "gibs.py"), "-q",
             os.path.join(ROOT, "assets", "src", "gibs", "mutt.json"),
             "-i", os.path.join(out, "mutt_pain_d0_f0.tex"), "-o", gib])
        g = sorted(os.listdir(gib))
        assert g == ["mutt_gib_d0_f%d.tex" % k for k in range(8)], g
        print(run([sys.executable, os.path.join(TOOLS, "palcheck.py"), out, gib]).strip())
    print("sprite_mutt: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
