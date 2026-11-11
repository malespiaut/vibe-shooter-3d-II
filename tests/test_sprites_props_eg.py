#!/usr/bin/env python3
"""ctest sprites_props_eg: the Egyptian decoration props of issue #127
(items.md Decorations; art-bible.md Sizes, Outline rule, colour budget).

Hand-drawn props are built with tools/items.py from assets/src/sprites/props
(as tools/build.d/48-props.sh does); the pre-rendered cat statue
(assets/src/sprites/props/cat_statue/model.py) is built by running
tools/build.d/48-props.sh itself, all into temporary directories. The props
directory is shared with the dog-tech set (#128, sprites_props_dt), so only
the frames of the props named here are checked. Checks:
- names <prop>_idle_d0_f<n>, sizes, frame counts (brazier 4, others 1), and
  the cat statue renders d0 only (sprite.json "dirs": 1);
- origin bottom-centre, opaque pixels on the bottom row;
- at most 3 ramps plus fb_*, never black, white or shadow_*;
- fullbright only on the brazier, and every brazier frame has fb_fire,
  fb_amber and fb_gold; brazier frames all differ (animated);
- outline: every opaque pixel with a transparent pixel above or to its left
  is never a _00 (lit edges carry no outline), and on the hand-drawn props
  every transparent pixel right of or below art is never black: the right
  and bottom silhouette is the ramp's _00 (pixels right/below an opaque one
  that are themselves the last opaque pixel use _00);
- fullbright carries no outline (#190): no _00 pixel is 4-adjacent to an
  fb_* pixel, so flame and ember edges meet transparency directly;
- the cat statue sits on its plinth: on rows 50-55 no transparent pixel is
  enclosed horizontally within the silhouette, and rows 52-53 are solid
  from x=6 to x=25 with no _00 except on the right silhouette edge, and
  no transparent pixel anywhere is enclosed on all four sides;
- no black holes: a _00 pixel whose four neighbours are all opaque must
  touch another _00 (an outline running into a crevice), never sit alone
  inside the art;
- palcheck passes.
The statue part exits 77 (ctest SKIP) if `blender` is missing; the
hand-drawn part always runs first.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bast  # noqa: E402
import palette  # noqa: E402

TR = bast.TRANSPARENT
HAND = {"obelisk_stub": (32, 64, 1), "pillar_sand": (32, 64, 1), "brazier": (24, 40, 4)}
STATUE = ("cat_statue", 32, 64)
FIRE = {"fb_fire", "fb_amber", "fb_gold"}


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    assert p.returncode == 0, " ".join(cmd) + "\n" + p.stdout[-3000:] + p.stderr[-3000:]
    return p.stdout


def check(path, name, w, h, names):
    t = bast.Tex.load(path)
    assert (t.width, t.height) == (w, h), "%s is %dx%d, want %dx%d" % (name, t.width, t.height, w, h)
    assert t.origin == (w // 2, h), "%s origin %s" % (name, t.origin)
    px = lambda x, y: t.pixels[x * h + y] if 0 <= x < w and 0 <= y < h else TR
    assert any(px(x, h - 1) != TR for x in range(w)), "%s: empty bottom row" % name
    used = set(t.pixels) - {TR}
    ramps, fb = set(), set()
    for i in used:
        n = names[i]
        assert n not in ("black", "white") and not n.startswith("shadow") \
            and not n.startswith("reserved"), "%s uses %s" % (name, n)
        if n.startswith("fb_"):
            fb.add(n)
        else:
            ramps.add(re.sub(r"_\d+$", "", n))
    assert len(ramps) <= 3, "%s: ramps %s" % (name, sorted(ramps))
    for x in range(w):
        for y in range(h):
            i = px(x, y)
            if i == TR:
                continue
            n = names[i]
            nb = [px(x - 1, y), px(x + 1, y), px(x, y - 1), px(x, y + 1)]
            if n.endswith("_00"):
                assert not any(j != TR and names[j].startswith("fb_") for j in nb), \
                    "%s: outline %s at (%d,%d) touches fullbright (fire has no outline)" % (
                        name, n, x, y)
            if n.endswith("_00"):
                assert TR in nb or any(names[j].endswith("_00") for j in nb), \
                    "%s: isolated interior %s at (%d,%d) (black hole)" % (name, n, x, y)
                assert px(x - 1, y) != TR or px(x, y - 1) != TR or \
                    px(x + 1, y) == TR or px(x, y + 1) == TR, \
                    "%s: outline on a lit (top-left) edge at (%d,%d)" % (name, x, y)
            # right/bottom silhouette edge: the pixel itself (pre-rendered)
            # or the outline drawn next to art (hand-drawn) is a _00 or fb.
            # (the canvas border is not an edge: the bottom row stands on
            # the floor, as for the pickups)
            if ((x + 1 < w and px(x + 1, y) == TR) or (y + 1 < h and px(x, y + 1) == TR)) \
                    and not n.startswith("fb_"):
                assert n.endswith("_00"), "%s: right/bottom edge %s at (%d,%d) is not _00" % (
                    name, n, x, y)
    return t, sorted(ramps), fb


def check_base(t, name, w, h, names):
    """The statue sits on its plinth: no gap between the body and the
    plinth top (art director, PR #160)."""
    px = lambda x, y: t.pixels[x * h + y] if 0 <= x < w and 0 <= y < h else TR
    for y in range(50, 56):
        xs = [x for x in range(w) if px(x, y) != TR]
        assert xs, "%s: row %d empty" % (name, y)
        holes = [x for x in range(xs[0], xs[-1] + 1) if px(x, y) == TR]
        assert not holes, "%s: row %d transparent at x=%s inside the silhouette" % (name, y, holes)
    for x in range(w):
        for y in range(h):
            if px(x, y) == TR:
                assert TR in (px(x - 1, y), px(x + 1, y), px(x, y - 1), px(x, y + 1)), \
                    "%s: enclosed transparent hole at (%d,%d)" % (name, x, y)
    for y in (52, 53):
        for x in range(6, 26):
            i = px(x, y)
            assert i != TR, "%s: hole at (%d,%d) above the plinth" % (name, x, y)
            if names[i].endswith("_00"):
                assert px(x + 1, y) == TR, "%s: interior %s at (%d,%d)" % (name, names[i], x, y)


def main():
    names = [n for _, n in palette.load_gpl()]
    rc = 0
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "props")
        os.makedirs(out)
        open(os.path.join(out, "stale.tex"), "wb").close()
        run([sys.executable, os.path.join(ROOT, "tools", "items.py"),
             "--src", os.path.join(ROOT, "assets", "src", "sprites", "props"), "-o", out, "--clean"])
        assert not os.path.exists(os.path.join(out, "stale.tex")), "--clean left a stale file"
        want = set()
        for prop, (w, h, frames) in sorted(HAND.items()):
            prev = None
            for f in range(frames):
                name = "%s_idle_d0_f%d" % (prop, f)
                want.add(name + ".tex")
                t, ramps, fb = check(os.path.join(out, name + ".tex"), name, w, h, names)
                if prop == "brazier":
                    assert fb == FIRE, "%s: fullbright %s, want %s" % (name, sorted(fb), sorted(FIRE))
                    if prev is not None:
                        assert prev.pixels != t.pixels, "%s identical to previous frame" % name
                    prev = t
                else:
                    assert not fb, "%s: fullbright %s on a non-light prop" % (name, sorted(fb))
                print("ok %-24s %dx%d ramps %s %s" % (name, w, h, ",".join(ramps), " ".join(sorted(fb))))
        got = {f for f in os.listdir(out) if f.endswith(".tex") and f.split("_idle_")[0] in HAND}
        assert got == want, "unexpected/missing: %s" % sorted(got ^ want)

        name, w, h = STATUE
        src = os.path.join(ROOT, "assets", "src", "sprites", "props", name)
        with open(os.path.join(src, "sprite.json")) as fh:
            cfg = json.load(fh)
        assert cfg.get("dirs") == 1 and "subdir" not in cfg, cfg
        assert cfg["canvas"] == [w, h], cfg["canvas"]
        if not shutil.which("blender"):
            print("sprites_props_eg: cat_statue SKIPPED, `blender` not found on PATH")
            rc = 77
        else:
            out = os.path.join(tmp, "step")
            env = dict(os.environ, PROPS_OUT=out, PROPS_BLEND=os.path.join(tmp, "blend"))
            p = subprocess.run(["sh", os.path.join(ROOT, "tools", "build.d", "48-props.sh")],
                               capture_output=True, text=True, env=env, cwd=ROOT)
            assert p.returncode == 0, "48-props.sh failed\n" + p.stdout[-3000:] + p.stderr[-3000:]
            for f in sorted(want):  # the build step writes the same hand-drawn frames
                with open(os.path.join(tmp, "props", f), "rb") as a, open(os.path.join(out, f), "rb") as b:
                    assert a.read() == b.read(), "48-props.sh output differs for %s" % f
            got = {f for f in os.listdir(out) if f.startswith(name)}
            assert got == {name + "_idle_d0_f0.tex"}, "statue frames: %s" % sorted(got)
            t, ramps, fb = check(os.path.join(out, name + "_idle_d0_f0.tex"), name, w, h, names)
            assert not fb, "statue has fullbright %s" % sorted(fb)
            assert ramps == ["gold", "sand", "steel"], ramps
            check_base(t, name, w, h, names)
            print("ok %-24s %dx%d ramps %s" % (name + "_idle_d0_f0", w, h, ",".join(ramps)))
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), out],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
    print("sprites_props_eg: OK")
    return rc


if __name__ == "__main__":
    sys.exit(main())
