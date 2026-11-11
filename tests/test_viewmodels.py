#!/usr/bin/env python3
"""ctest viewmodels_1_4: the view-model build step (tools/build.d/55-viewmodels.sh)
writes every frame of weapons 1-4 listed in issue #86 / weapons.md with the
exact size, bottom-centre origin and transparency, from a clean output
directory, and the output passes palcheck. Muzzle flashes use fullbright
entries; the claws never do. The pistol shows the background through no closed
opening except its trigger guard.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bast  # noqa: E402
import palette  # noqa: E402

TABLE = {
    1: ((96, 64), ["idle", "swipe0", "swipe1", "swipe2"]),
    2: ((64, 72), ["idle", "fire0", "fire1"]),
    3: ((112, 64), ["idle", "fire0", "fire1", "pump0", "pump1"]),
    4: ((96, 80), ["idle", "fire0", "fire1"]),
}
# Where a weapon may show the background through a closed opening (inclusive x0, y0, x1, y1).
# vm2: only inside the trigger guard loop. Anywhere else (e.g. the ankh loop over the slide)
# an enclosed transparent pixel is a hole through the gun.
OPENINGS = {2: (24, 52, 32, 60)}
FLASH = {"vm2_fire0", "vm3_fire0", "vm4_fire0", "vm4_fire1"}


def enclosed_holes(px, w, h):
    """Transparent pixels not 4-connected to the frame border: holes through the art
    that would show whatever is behind the gun."""
    T = bast.TRANSPARENT
    seen = set()
    stack = [(x, y) for x in range(w) for y in (0, h - 1)] + \
            [(x, y) for y in range(h) for x in (0, w - 1)]
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h) or px[x * h + y] != T:
            continue
        seen.add((x, y))
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return [(x, y) for x in range(w) for y in range(h)
            if px[x * h + y] == T and (x, y) not in seen]


def main():
    entries = palette.load_gpl()
    fb = {i for i, (_, n) in enumerate(entries) if n.startswith("fb_")}
    script = os.path.join(ROOT, "tools", "build.d", "55-viewmodels.sh")
    assert "viewmodels.py" in open(script).read() and "--clean" in open(script).read()
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "viewmodels")
        os.makedirs(out)
        stale = os.path.join(out, "vm9_stale.tex")
        open(stale, "wb").close()
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "viewmodels.py"),
                        "-o", out, "--clean"], check=True)
        assert not os.path.exists(stale), "--clean left a stale file"
        want = set()
        for n, ((w, h), frames) in sorted(TABLE.items()):
            for f in frames:
                name = "vm%d_%s" % (n, f)
                want.add(name + ".tex")
                t = bast.Tex.load(os.path.join(out, name + ".tex"))
                assert (t.width, t.height) == (w, h), \
                    "%s is %dx%d, want %dx%d" % (name, t.width, t.height, w, h)
                assert t.origin == (w // 2, h), "%s origin %s, want bottom-centre" % (name, t.origin)
                assert t.flags & bast.TEX_HAS_TRANSPARENCY, "%s: no transparency" % name
                px = bytes(t.pixels)
                # art reaches the bottom edge (the screen edge cuts it off in game)
                assert any(px[x * h + h - 1] != bast.TRANSPARENT for x in range(w)), \
                    "%s: bottom row is empty" % name
                if n in OPENINGS:
                    x0, y0, x1, y1 = OPENINGS[n]
                    holes = [(x, y) for x, y in enclosed_holes(px, w, h)
                             if not (x0 <= x <= x1 and y0 <= y <= y1)]
                    assert not holes, "%s: %d transparent px enclosed by art, e.g. %s" \
                        % (name, len(holes), holes[:4])
                has_fb = any(p in fb for p in set(px))
                assert has_fb == (name in FLASH or name in ("vm2_fire1", "vm3_fire1")), \
                    "%s: fullbright use %s unexpected" % (name, has_fb)
                print("ok %-11s %dx%d" % (name, w, h))
        got = {f for f in os.listdir(out) if f.endswith(".tex")}
        assert got == want, "frame set differs: extra %s missing %s" % (got - want, want - got)
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), out],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
    print("viewmodels_1_4: %d frames ok" % len(want))


if __name__ == "__main__":
    main()
