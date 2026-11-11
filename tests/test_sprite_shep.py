#!/usr/bin/env python3
"""ctest `sprite_shep`: the Shepherd Shieldbearer pre-rendered sprite set (issue #125).

Builds assets/src/sprites/shep/model.py with Blender, renders it with
tools/blender_render.py (the same commands as tools/build.d/60-sprites.sh)
into a temp dir, makes the gib frames with tools/gibs.py from
assets/src/gibs/shep.json, then checks (enemies.md, art-bible.md):
- exactly the 128 frames shep_<anim>_d<0..7>_f<n>.tex for idle 2, walk 4,
  windup 2, attack 2, pain 1, death 5; every frame 56x64, origin 28,64;
- interior rule: an opaque pixel whose right and bottom neighbours are
  opaque (not the selective outline) is never `black`, `shadow_*` or any
  ramp's `_00`; outline pixels are a `_00` or a fullbright entry;
- colour budget: only brown, beige, blue plus fb_blue / fb_white;
- each standing frame's dominant ramp (most pixels) spans >= 8 entries;
- idle d0 opaque bounding box is 40x60 +-2 px, standing on the floor row;
  no frame touches the left, right or top canvas edge (nothing clipped);
- the baton crackle (fb_white) shows on windup f1 only among windup frames
  in d0, and on attack f0 only; the shield rim (fb_blue) is lit in idle and
  off on the dropped shield (death f2-f4);
- 8 gib frames shep_gib_d0_f<0..7>.tex, none with an opaque pixel on row 0
  or in column 0 / 55, all chunks at rest by f7 (f6 == f7);
- the death f4 corpse is at most 24 px tall in every direction;
- the face clears the shield (idle/walk/pain d0, d1, d7): in the 8 rows
  above the shield rim's top row the brown/beige face spans >= 8x5 px with
  >= 12 beige (tan muzzle) pixels, and in d0/d1/d7 idle/walk no brown/beige
  pixel lies between the rim's left and right edge on any shield row;
- the shield face is shaded, not a flat slab (idle/walk d0): the pixels
  between the rim edges use >= 4 `blue` entries holding >= 5% each, and no
  single entry holds more than 50%;
- the dropped shield is not a floor sliver (death f2-f4, every direction):
  no run of more than 6 adjacent columns whose opaque pixels span at most
  2 rows (the edge-on 1-2 px line);
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
SRC = os.path.join(ROOT, "assets", "src", "sprites", "shep")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import palette as gpl  # noqa: E402

W, H = 56, 64
ANIMS = {"idle": 2, "walk": 4, "windup": 2, "attack": 2, "pain": 1, "death": 5}
RAMPS = ("brown", "beige", "blue")
FB = ("fb_blue", "fb_white")
SKIP = 77


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    assert p.returncode == 0, " ".join(cmd) + "\n" + p.stdout[-3000:] + p.stderr[-3000:]
    return p.stdout


def rim_spans(px, rim):
    """{row: (left, right)} of the fullbright shield rim, rows with >= 2."""
    sp = {}
    for y in range(H):
        xs = [x for x in range(W) if px[y][x] == rim]
        if len(xs) >= 2 and xs[-1] - xs[0] > 2:
            sp[y] = (xs[0], xs[-1])
    return sp


def sliver_run(px):
    """Longest run of adjacent columns whose opaque pixels span at most 2
    rows (a shield seen edge-on on the floor: a 1-2 px line)."""
    best = run = 0
    for x in range(W):
        col = [y for y in range(H) if px[y][x] != 255]
        if col and max(col) - min(col) < 2:
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


def main():
    blender = shutil.which("blender")
    if not blender:
        print("sprite_shep: SKIPPED, `blender` not found on PATH")
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
             os.path.join(SRC, "model.py"), "--", mdl])
        line = run([blender, "-b", os.path.join(mdl, "shep.blend"), "--python",
                    os.path.join(TOOLS, "blender_render.py"), "--", "--sprite", "shep",
                    "--canvas", "%dx%d" % (W, H), "--out", out])
        print([ln for ln in line.splitlines() if ln.startswith("blender_render:")][-1])

        want = sorted("shep_%s_d%d_f%d.tex" % (a, dd, f)
                      for a, n in ANIMS.items() for dd in range(8) for f in range(n))
        got = sorted(os.listdir(out))
        assert len(want) == 128 and got == want, (len(got), set(got) ^ set(want))

        bad_interior = bad_outline = 0
        face_min = (99, 99, 99)
        poke = 0
        shield_ok = []
        sliver_max = 0
        fur = {i for i, (rp, _) in ramp_of.items() if rp in ("brown", "beige")}
        tan = {i for i, (rp, _) in ramp_of.items() if rp == "beige"}
        blue = {i for i, (rp, _) in ramp_of.items() if rp == "blue"}
        dom_min = 99
        white, rim = names.index("fb_white"), names.index("fb_blue")
        for fn in want:
            t = bast.Tex.load(os.path.join(out, fn))
            assert (t.width, t.height) == (W, H), (fn, t.width, t.height)
            assert t.origin == (W // 2, H), (fn, t.origin)
            px = [[t.get(x, y) for x in range(W)] for y in range(H)]
            xs, ys = [], []
            count, entries = {}, {}
            for y in range(H):
                for x in range(W):
                    i = px[y][x]
                    if i == 255:
                        continue
                    xs.append(x)
                    ys.append(y)
                    assert i in fb or (i in ramp_of and ramp_of[i][0] in RAMPS), \
                        (fn, x, y, names[i])
                    if i in ramp_of:
                        rp = ramp_of[i][0]
                        count[rp] = count.get(rp, 0) + 1
                        entries.setdefault(rp, set()).add(i)
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
            if fn.startswith("shep_death") and fn.endswith("_f4.tex"):
                assert H - min(ys) <= 24, (fn, "corpse taller than 24 px", H - min(ys))
            if not fn.startswith("shep_death"):
                assert max(ys) == H - 1, (fn, "not standing on the floor row")
                dom = max(count, key=count.get)
                dom_min = min(dom_min, len(entries[dom]))
            if fn == "shep_idle_d0_f0.tex":
                bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
                print("idle d0 bounding box: %dx%d" % (bw, bh))
                assert abs(bw - 40) <= 2 and abs(bh - 60) <= 2, (bw, bh)
            dd = int(fn.rsplit("_d", 1)[1][0])
            anim = fn.split("_")[1]
            if anim in ("idle", "walk", "pain") and dd in (0, 1, 7):
                spans = rim_spans(px, names.index("fb_blue"))
                top = min(spans)
                fx = [x for y in range(top - 8, top) for x in range(W) if px[y][x] in fur]
                fy = [y for y in range(top - 8, top) for x in range(W) if px[y][x] in fur]
                nt = sum(px[y][x] in tan for y in range(top - 8, top) for x in range(W))
                fw = max(fx) - min(fx) + 1 if fx else 0
                fh = max(fy) - min(fy) + 1 if fy else 0
                face_min = tuple(min(a, b) for a, b in zip(face_min, (fw, fh, nt)))
                assert fw >= 8 and fh >= 5 and nt >= 12, (fn, "face hidden", fw, fh, nt)
                if anim != "pain":
                    n = sum(px[y][x] in fur for y, (a, b) in spans.items()
                            for x in range(a + 1, b))
                    poke += n
                    assert n == 0, (fn, "fur pixels inside the shield", n)
                if anim != "pain" and dd == 0:
                    hist = {}
                    for y, (a, b) in spans.items():
                        for x in range(a + 1, b):
                            if px[y][x] in blue:
                                hist[px[y][x]] = hist.get(px[y][x], 0) + 1
                    tot = sum(hist.values())
                    big = sum(c >= 0.05 * tot for c in hist.values())
                    share = max(hist.values()) / tot
                    shield_ok.append((fn, big, share))
                    assert big >= 4 and share <= 0.5, (fn, "flat shield face", big, share)
            if anim == "death" and fn[-6:] in ("f2.tex", "f3.tex", "f4.tex"):
                sr = sliver_run(px)
                sliver_max = max(sliver_max, sr)
                assert sr <= 6, (fn, "dropped shield is a floor sliver", sr)
            sp = sum(row.count(white) for row in px)
            rm = sum(row.count(rim) for row in px)
            # Baton crackle: windup f1 (the telegraph glow) and attack f0.
            if fn.startswith("shep_windup_d0"):
                assert (sp > 0) == fn.endswith("_f1.tex"), (fn, sp)
            if fn.startswith("shep_attack_d0") or fn.startswith("shep_attack_d2"):
                assert (sp > 0) == fn.endswith("_f0.tex"), (fn, sp)
            if not fn.startswith(("shep_windup", "shep_attack")):
                assert sp == 0, (fn, sp)
            # Shield rim: lit while the shield is held, off once it drops.
            if fn.startswith("shep_idle"):
                assert rm > 0, (fn, "no shield rim")
            if fn.startswith("shep_death") and fn[-6:] in ("f2.tex", "f3.tex", "f4.tex"):
                assert rm == 0, (fn, "rim still lit on the dropped shield", rm)
        print("interior dark-end pixels: %d, non-_00 outline pixels: %d, "
              "dominant-ramp entries (min over standing frames): %d"
              % (bad_interior, bad_outline, dom_min))
        print("face above the shield (min w, h, tan px): %d, %d, %d; fur pixels "
              "inside the shield: %d" % (face_min + (poke,)))
        for fn, big, share in shield_ok:
            print("%s shield face: %d blue entries >= 5%%, largest %.0f%%"
                  % (fn, big, share * 100))
        print("dropped shield: longest floor-sliver run %d columns" % sliver_max)
        assert bad_interior == 0 and bad_outline == 0
        assert dom_min >= 8, dom_min

        run([sys.executable, os.path.join(TOOLS, "gibs.py"), "-q",
             os.path.join(ROOT, "assets", "src", "gibs", "shep.json"),
             "-i", os.path.join(out, "shep_pain_d0_f0.tex"), "-o", gib])
        g = sorted(os.listdir(gib))
        assert g == ["shep_gib_d0_f%d.tex" % k for k in range(8)], g
        # Gib frames: nothing clipped (no opaque pixel on row 0 or in the
        # first/last column) and every chunk resting by f7 (f6 == f7).
        prev = None
        for fn in g:
            t = bast.Tex.load(os.path.join(gib, fn))
            assert (t.width, t.height) == (W, H), (fn, t.width, t.height)
            px = [[t.get(x, y) for x in range(W)] for y in range(H)]
            edge = (sum(px[0][x] != 255 for x in range(W)),
                    sum(px[y][0] != 255 for y in range(H)),
                    sum(px[y][W - 1] != 255 for y in range(H)))
            assert edge == (0, 0, 0), ("gib clipped (row 0, col 0, col %d)" % (W - 1), fn, edge)
            last, prev = prev, px
        assert last == prev, "gib chunks still moving between f6 and f7"
        print("gib frames: 0 edge pixels, f6 == f7 (at rest)")
        print(run([sys.executable, os.path.join(TOOLS, "palcheck.py"), out, gib]).strip())
    print("sprite_shep: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
