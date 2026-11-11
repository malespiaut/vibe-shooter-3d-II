#!/usr/bin/env python3
"""ctest `sprite_pom`: the Pomeranian Plasmagunner sprite set (issue #122).

Builds the .blend from assets/src/sprites/pom/model.py exactly as
tools/build.d/60-sprites.sh does, renders it with tools/blender_render.py
into a temporary directory (never assets/gen), then checks against
docs/design/enemies.md and art-bible.md:
- exactly the 128 frames of the animation list (idle 2, walk 4, windup 2,
  attack 2, pain 1, death 5; 8 directions), 56x56, origin bottom-centre;
- nothing touches the left/right/top canvas edge (no clipped parts);
- front view idle f0 bounding box about 40x48 (38..42 x 46..50), standing on
  the bottom row; the death corpse is low (at most 20 px tall);
- colour budget: orange, cyan, steel plus fb_cyan only;
- no black holes: no interior (non-outline) pixel is black, shadow_* or a
  ramp's _00; outline pixels (right/bottom edge) are their ramp's _00;
- dominant ramp `orange` spans 8+ entries in every frame and reaches the
  upper half (>= orange_09) in every idle/walk frame;
- fb_cyan charge glow shows on windup f1 and on the attack shot (attack f0)
  in every view that sees the cannon (d0, d1, d6, d7), and nowhere else (idle, walk, windup
  f0, attack f1, pain, death);
- ears taper to a point: in idle d2/d6 (profile) every run of opaque pixels
  in the top 3 rows is at most 2 px wide;
- broken fur outline: idle d0 and d2 f0 show at least 8 tufts in the top
  half (thin protrusions removed by a 3x3 morphological opening, counted as
  4-connected blobs);
- walk frames all differ;
- gibs: tools/gibs.py on assets/src/gibs/pom.json writes 8 frames;
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
SRC = os.path.join(ROOT, "assets", "src", "sprites", "pom")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import palette as gpl  # noqa: E402

W, H = 56, 56
ANIMS = {"idle": 2, "walk": 4, "windup": 2, "attack": 2, "pain": 1, "death": 5}
RAMPS = {"orange", "cyan", "steel"}
FB = "fb_cyan"
SKIP = 77


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    assert p.returncode == 0, " ".join(cmd) + "\n" + p.stdout[-3000:] + p.stderr[-3000:]
    return p.stdout


def opaque(t):
    return [(x, y, t.get(x, y)) for x in range(t.width) for y in range(t.height)
            if t.get(x, y) != 255]


def mask(t):
    return {(x, y) for x, y, _ in opaque(t)}


def top_runs(t, rows=3):
    """Widths of the opaque runs in the top `rows` rows."""
    m = mask(t)
    top = min(y for _, y in m)
    widths = []
    for y in range(top, top + rows):
        xs = sorted(x for x, yy in m if yy == y)
        run = 1
        for a, b in zip(xs, xs[1:] + [None]):
            if b == a + 1:
                run += 1
            else:
                widths.append(run)
                run = 1
    return widths


def tufts(t):
    """Thin protrusions of the silhouette in its top half: pixels a 3x3
    opening (erode, dilate) removes, counted as 4-connected blobs."""
    m = mask(t)
    nb = [(a, b) for a in (-1, 0, 1) for b in (-1, 0, 1)]
    er = {(x, y) for x, y in m if all((x + a, y + b) in m for a, b in nb)}
    op = {(x + a, y + b) for x, y in er for a, b in nb}
    ys = [y for _, y in m]
    mid = (min(ys) + max(ys)) / 2
    rest = {p for p in m - op if p[1] < mid}
    n = 0
    while rest:
        st = [rest.pop()]
        n += 1
        while st:
            x, y = st.pop()
            for q in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if q in rest:
                    rest.remove(q)
                    st.append(q)
    return n


def main():
    blender = shutil.which("blender")
    if not blender:
        print("sprite_pom: SKIPPED, `blender` not found on PATH")
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
    assert used - {FB} == RAMPS, used

    with tempfile.TemporaryDirectory() as d:
        gen, out = os.path.join(d, "blend"), os.path.join(d, "out")
        run([blender, "-b", "--factory-startup", "--python",
             os.path.join(SRC, "model.py"), "--", gen])
        log = run([blender, "-b", os.path.join(gen, "pom.blend"), "--python",
                   os.path.join(TOOLS, "blender_render.py"), "--", "--sprite", "pom",
                   "--canvas", "%dx%d" % (W, H), "--out", out])
        print([ln for ln in log.splitlines() if ln.startswith("blender_render:")][-1])

        want = sorted("pom_%s_d%d_f%d.tex" % (a, dd, f)
                      for a, n in ANIMS.items() for dd in range(8) for f in range(n))
        assert len(want) == 128
        got = sorted(os.listdir(out))
        assert got == want, sorted(set(got) ^ set(want))

        tex = {}
        for n in want:
            t = bast.Tex.load(os.path.join(out, n))
            assert (t.width, t.height) == (W, H), n
            assert t.origin == (W // 2, H), (n, t.origin)
            tex[n[len("pom_"):-4]] = t

        span_min, top_min = 99, 99
        for k, t in tex.items():
            px = opaque(t)
            assert px, k
            # Not clipped by the canvas.
            edge = [(x, y) for x, y, _ in px if x in (0, W - 1) or y == 0]
            assert not edge, (k, edge[:4])
            orange = []
            for x, y, i in px:
                n = names[i]
                if n == FB:
                    continue
                r = ramp_name.get(i)
                assert r in RAMPS, (k, x, y, n)
                right = t.get(x + 1, y) if x + 1 < W else 255
                below = t.get(x, y + 1) if y + 1 < H else 255
                if right == 255 or below == 255:
                    assert n == r + "_00", (k, x, y, n)
                else:
                    assert not n.endswith("_00") and not n.startswith("shadow"), (k, x, y, n)
                if r == "orange":
                    orange.append(int(n[len("orange_"):]))
            span = max(orange) - min(orange) + 1
            span_min = min(span_min, span)
            assert span >= 8, (k, sorted(set(orange)))
            if k.startswith(("idle", "walk")):
                top_min = min(top_min, max(orange))
                assert max(orange) >= 9, (k, max(orange))
                assert max(y for _, y, _ in px) == H - 1, k  # stands on the floor

        def bbox(t):
            px = opaque(t)
            xs, ys = [p[0] for p in px], [p[1] for p in px]
            return max(xs) - min(xs) + 1, max(ys) - min(ys) + 1

        bw, bh = bbox(tex["idle_d0_f0"])
        print("front idle bbox %dx%d, corpse d0 %dx%d, orange span min %d, idle/walk top orange_%02d"
              % (bw, bh, *bbox(tex["death_d0_f4"]), span_min, top_min))
        assert 38 <= bw <= 42 and 46 <= bh <= 50, (bw, bh)
        for dd in range(8):
            assert bbox(tex["death_d%d_f4" % dd])[1] <= 20, dd
        fb = names.index(FB)
        glow = {k for k, t in tex.items() if any(i == fb for _, _, i in opaque(t))}
        for k in ("windup", "attack"):
            for dd in (0, 1, 6, 7):  # the views that see the cannon (dog's right)
                assert "%s_d%d_f%d" % (k, dd, 1 if k == "windup" else 0) in glow, (k, dd)
        stray = sorted(k for k in glow if not k.startswith(("windup", "attack"))
                       or k.endswith("_f1") and k.startswith("attack")
                       or k.endswith("_f0") and k.startswith("windup"))
        assert not stray, stray
        print("fb_cyan glow in %d frames (windup f1, attack f0 only)" % len(glow))
        for k in ("idle_d2_f0", "idle_d6_f0", "idle_d2_f1", "idle_d6_f1"):
            w = top_runs(tex[k])
            assert max(w) <= 2, (k, w)
        nt = {k: tufts(tex[k]) for k in ("idle_d0_f0", "idle_d2_f0")}
        print("ear tips: top 3 rows of idle d2/d6 at most 2 px wide; tufts in the top half %s" % nt)
        assert all(n >= 8 for n in nt.values()), nt
        for dd in range(8):
            walk = [opaque(tex["walk_d%d_f%d" % (dd, f)]) for f in range(4)]
            assert all(walk[a] != walk[b] for a in range(4) for b in range(a + 1, 4)), dd

        # Gibs from the pain frame.
        gibs = os.path.join(d, "gibs")
        gib_src = os.path.join(ROOT, "assets", "src", "gibs", "pom.json")
        with open(gib_src) as f:
            g = json.load(f)
        assert g["sprite"] == "pom" and g["source"] == "sprites/pom_pain_d0_f0.tex"
        assert g["canvas"] == [W, H]
        run([sys.executable, os.path.join(TOOLS, "gibs.py"), "-q", gib_src,
             "-i", os.path.join(out, "pom_pain_d0_f0.tex"), "-o", gibs])
        assert sorted(os.listdir(gibs)) == ["pom_gib_d0_f%d.tex" % i for i in range(8)]

        print(run([sys.executable, os.path.join(TOOLS, "palcheck.py"), out, gibs]).strip())
    print("sprite_pom: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
