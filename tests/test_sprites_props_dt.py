#!/usr/bin/env python3
"""ctest sprites_props_dt: the dog-tech decorations of issue #128
(items.md Decorations, art-bible.md Sizes) built by tools/build.d/48-props.sh
into a clean temporary directory, plus the kibble barrel gibs.

Checks every frame: exact name set (no extras), size, frame count, origin
bottom-centre, opaque pixel on the bottom row, at most 3 ramps plus fb_*,
never black/white/shadow_*, no `_00` or fullbright pixel where the art bible
forbids it (`_00` only on outline pixels: one with a transparent or
off-canvas 8-neighbour, which covers outline pixels in concave corners; fullbright only on the
floodlight lens and rack LEDs), rack frames differ (LEDs blink), palcheck
passes, and tools/gibs.py turns the barrel into 8 kibble_barrel_gib frames.
Exit 77 (ctest SKIP) when `blender` is missing (the rack is pre-rendered).
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

# name -> (w, h, frames, fullbright entries allowed)
TABLE = {
    "kibble_barrel": (32, 40, 1, set()),
    "crate": (32, 32, 1, set()),
    "floodlight": (24, 64, 1, {"fb_white", "fb_amber"}),
    "dog_banner": (32, 64, 1, set()),
    "server_rack": (48, 64, 2, {"fb_green", "fb_cyan"}),
}
T = bast.TRANSPARENT


def check_frame(path, name, w, h, fb_ok, names):
    t = bast.Tex.load(path)
    assert (t.width, t.height) == (w, h), "%s is %dx%d, want %dx%d" % (name, t.width, t.height, w, h)
    assert t.origin == (w // 2, h), "%s origin %s" % (name, t.origin)
    px = lambda x, y: t.pixels[x * h + y]  # noqa: E731  (TEX is column-major)
    assert any(px(x, h - 1) != T for x in range(w)), "%s: empty bottom row" % name
    ramps, fbs = set(), set()
    for x in range(w):
        for y in range(h):
            i = px(x, y)
            if i == T:
                continue
            n = names[i]
            assert n not in ("black", "white") and not n.startswith("shadow"), \
                "%s uses %s at %d,%d" % (name, n, x, y)
            if n.startswith("fb_"):
                fbs.add(n)
                continue
            ramps.add(re.sub(r"_\d+$", "", n))
            if n.endswith("_00"):
                edge = any(not (0 <= x + dx < w and 0 <= y + dy < h) or px(x + dx, y + dy) == T
                           for dx in (-1, 0, 1) for dy in (-1, 0, 1))
                assert edge, "%s: %s inside the silhouette at %d,%d (black hole)" % (name, n, x, y)
    assert len(ramps) <= 3, "%s: ramps %s" % (name, sorted(ramps))
    assert fbs <= fb_ok, "%s: fullbright %s outside lights/LEDs" % (name, sorted(fbs - fb_ok))
    return t, ramps, fbs


def main():
    if not shutil.which("blender"):
        print("sprites_props_dt: SKIP, blender not installed")
        return 77
    names = [n for _, n in palette.load_gpl()]
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "props")
        os.makedirs(out)
        open(os.path.join(out, "stale.tex"), "wb").close()
        env = dict(os.environ, PROPS_OUT=out, PROPS_BLEND=os.path.join(tmp, "blend"))
        subprocess.run(["sh", os.path.join(ROOT, "tools", "build.d", "48-props.sh")],
                       check=True, env=env, cwd=ROOT)
        want = set()
        for sprite, (w, h, frames, fb_ok) in sorted(TABLE.items()):
            prev = None
            seen_fb = set()
            for f in range(frames):
                name = "%s_idle_d0_f%d" % (sprite, f)
                want.add(name + ".tex")
                path = os.path.join(out, name + ".tex")
                assert os.path.isfile(path), "missing %s" % name
                t, ramps, fbs = check_frame(path, name, w, h, fb_ok, names)
                seen_fb |= fbs
                if prev is not None:
                    assert prev.pixels != t.pixels, "%s: identical to previous frame" % name
                prev = t
                print("ok %-26s %dx%d ramps %s fb %s" % (name, w, h, ",".join(sorted(ramps)),
                                                         ",".join(sorted(fbs)) or "-"))
            assert seen_fb == fb_ok, "%s: fullbright %s, want %s" % (sprite, sorted(seen_fb),
                                                                    sorted(fb_ok))
        # The props directory is shared with the Egyptian set (#127): check
        # only this set's sprites, and that --clean removed the stale file.
        assert not os.path.exists(os.path.join(out, "stale.tex")), "stale.tex survived"
        got = {f for f in os.listdir(out)
               if f.endswith(".tex") and f.split("_idle_")[0] in TABLE}
        assert got == want, "unexpected/missing: %s" % sorted(got ^ want)
        assert len(want) == 6, len(want)
        # Barrel gibs (GAME.md 6.2).
        pj = os.path.join(ROOT, "assets", "src", "gibs", "kibble_barrel.json")
        with open(pj) as fh:
            params = json.load(fh)
        assert params["source"] == "sprites/props/kibble_barrel_idle_d0_f0.tex", params["source"]
        gib = os.path.join(tmp, "gibs")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gibs.py"), "-q", pj,
                        "-i", os.path.join(out, "kibble_barrel_idle_d0_f0.tex"), "-o", gib],
                       check=True)
        g = sorted(os.listdir(gib))
        assert g == ["kibble_barrel_gib_d0_f%d.tex" % i for i in range(8)], g
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), out, gib],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
    print("sprites_props_dt: OK, %d frames + 8 gib frames" % len(want))
    return 0


if __name__ == "__main__":
    sys.exit(main())
