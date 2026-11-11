#!/usr/bin/env python3
"""ctest `sprite_dach`: the Dachshund Howitzer sprite set (issue #123).

Builds the .blend from assets/src/sprites/dach/model.py exactly as
tools/build.d/60-sprites.sh does, renders it with tools/blender_render.py
into a temporary directory (never assets/gen), then checks against
docs/design/enemies.md and art-bible.md:
- exactly the 128 frames of the animation list (idle 2, walk 4, windup 2,
  attack 2, pain 1, death 5; 8 directions), 72x48, origin bottom-centre;
- nothing touches the left/right/top canvas edge (no clipped parts);
- side view idle f0 (d2 and d6) bounding box about 64x32 (61..67 x 30..34),
  standing on the bottom row; the death corpse is low (at most 20 px tall);
- colour budget: brown, steel plus fb_red only;
- no black holes: no interior (non-outline) pixel is black, shadow_* or a
  ramp's _00; outline pixels (right/bottom edge) are their ramp's _00;
- dominant ramp `brown` spans 8+ entries in every frame and reaches the
  upper half (>= brown_10) in every idle/walk frame;
- the fb_red muzzle glow shows in every windup frame and direction and in
  attack f0, never in idle, walk or the corpse;
- windup is a held pose (f0 == f1); walk frames all differ;
- gibs: tools/gibs.py on assets/src/gibs/dach.json writes 8 frames;
- palcheck passes on the TEX files.
Exit 77 (ctest SKIP) when `blender` is not installed.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
SRC = os.path.join(ROOT, "assets", "src", "sprites", "dach")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import palette as gpl  # noqa: E402

W, H = 72, 48
ANIMS = {"idle": 2, "walk": 4, "windup": 2, "attack": 2, "pain": 1, "death": 5}
RAMPS = {"brown", "steel"}
SKIP = 77


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    assert p.returncode == 0, " ".join(cmd) + "\n" + p.stdout[-3000:] + p.stderr[-3000:]
    return p.stdout


def opaque(t):
    return [(x, y, t.get(x, y)) for x in range(t.width) for y in range(t.height)
            if t.get(x, y) != 255]


def main():
    blender = shutil.which("blender")
    if not blender:
        print("sprite_dach: SKIPPED, `blender` not found on PATH")
        return SKIP
    with open(os.path.join(SRC, "sprite.json")) as f:
        cfg = json.load(f)
    assert cfg["canvas"] == [W, H], cfg["canvas"]
    entries = gpl.load_gpl()
    names = [n for _, n in entries]
    ramp_name = {}
    for i, n in enumerate(names):
        base, _, num = n.rpartition("_")
        if num.isdigit():
            ramp_name[i] = base
    used = set(cfg["ramps"].values())
    assert used - {"fb_red"} == RAMPS, used

    with tempfile.TemporaryDirectory() as d:
        gen, out = os.path.join(d, "blend"), os.path.join(d, "out")
        run([blender, "-b", "--factory-startup", "--python",
             os.path.join(SRC, "model.py"), "--", gen])
        log = run([blender, "-b", os.path.join(gen, "dach.blend"), "--python",
                   os.path.join(TOOLS, "blender_render.py"), "--", "--sprite", "dach",
                   "--canvas", "%dx%d" % (W, H), "--out", out])
        print([ln for ln in log.splitlines() if ln.startswith("blender_render:")][-1])

        want = sorted("dach_%s_d%d_f%d.tex" % (a, dd, f)
                      for a, n in ANIMS.items() for dd in range(8) for f in range(n))
        assert len(want) == 128
        got = sorted(os.listdir(out))
        assert got == want, sorted(set(got) ^ set(want))

        tex = {}
        for n in want:
            t = bast.Tex.load(os.path.join(out, n))
            assert (t.width, t.height) == (W, H), n
            assert t.origin == (W // 2, H), (n, t.origin)
            tex[n[len("dach_"):-4]] = t

        span_min, top_min = 99, 99
        for k, t in tex.items():
            px = opaque(t)
            assert px, k
            # Not clipped by the canvas.
            edge = [(x, y) for x, y, _ in px if x in (0, W - 1) or y == 0]
            assert not edge, (k, edge[:4])
            brown = []
            for x, y, i in px:
                n = names[i]
                if n == "fb_red":
                    continue
                r = ramp_name.get(i)
                assert r in RAMPS, (k, x, y, n)
                right = t.get(x + 1, y) if x + 1 < W else 255
                below = t.get(x, y + 1) if y + 1 < H else 255
                if right == 255 or below == 255:
                    assert n == r + "_00", (k, x, y, n)
                else:
                    assert not n.endswith("_00") and not n.startswith("shadow"), (k, x, y, n)
                if r == "brown":
                    brown.append(int(n[len("brown_"):]))
            span = max(brown) - min(brown) + 1
            span_min = min(span_min, span)
            assert span >= 8, (k, sorted(set(brown)))
            if k.startswith(("idle", "walk")):
                top_min = min(top_min, max(brown))
                assert max(brown) >= 10, (k, max(brown))
                assert max(y for _, y, _ in px) == H - 1, k  # stands on the floor

        def bbox(t):
            px = opaque(t)
            xs, ys = [p[0] for p in px], [p[1] for p in px]
            return max(xs) - min(xs) + 1, max(ys) - min(ys) + 1

        sides = [bbox(tex["idle_d%d_f0" % dd]) for dd in (2, 6)]
        print("side idle bbox %s, front idle %dx%d, corpse d0 %dx%d, brown span min %d, "
              "idle/walk top brown_%02d" % (" ".join("%dx%d" % b for b in sides),
                                            *bbox(tex["idle_d0_f0"]), *bbox(tex["death_d0_f4"]),
                                            span_min, top_min))
        for bw, bh in sides:
            assert 61 <= bw <= 67 and 30 <= bh <= 34, (bw, bh)
        for dd in range(8):
            assert bbox(tex["death_d%d_f4" % dd])[1] <= 20, dd
        fb = names.index("fb_red")

        def glows(k):
            return any(i == fb for _, _, i in opaque(tex[k]))

        for dd in range(8):
            for f in range(2):
                assert glows("windup_d%d_f%d" % (dd, f)), (dd, f)
            assert glows("attack_d%d_f0" % dd), dd
            assert not glows("death_d%d_f4" % dd), dd
            for f in range(2):
                assert not glows("idle_d%d_f%d" % (dd, f)), (dd, f)
            for f in range(4):
                assert not glows("walk_d%d_f%d" % (dd, f)), (dd, f)
        for dd in range(8):
            assert opaque(tex["windup_d%d_f0" % dd]) == opaque(tex["windup_d%d_f1" % dd])
            walk = [opaque(tex["walk_d%d_f%d" % (dd, f)]) for f in range(4)]
            assert all(walk[a] != walk[b] for a in range(4) for b in range(a + 1, 4)), dd

        # Gibs from the pain frame.
        gibs = os.path.join(d, "gibs")
        gib_src = os.path.join(ROOT, "assets", "src", "gibs", "dach.json")
        with open(gib_src) as f:
            g = json.load(f)
        assert g["sprite"] == "dach" and g["source"] == "sprites/dach_pain_d0_f0.tex"
        assert g["canvas"] == [W, H]
        run([sys.executable, os.path.join(TOOLS, "gibs.py"), "-q", gib_src,
             "-i", os.path.join(out, "dach_pain_d0_f0.tex"), "-o", gibs])
        assert sorted(os.listdir(gibs)) == ["dach_gib_d0_f%d.tex" % i for i in range(8)]

        print(run([sys.executable, os.path.join(TOOLS, "palcheck.py"), out, gibs]).strip())
    print("sprite_dach: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
