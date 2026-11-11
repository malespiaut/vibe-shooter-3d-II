#!/usr/bin/env python3
"""ctest viewmodels_5_7: the view-model build step writes every frame of weapons
5-7 listed in issue #87 / weapons.md (Launcher idle/fire0/fire1/reload0, Lance
idle/charge0/charge1/fire0/fire1, Howler idle/roar0..2) with the exact size,
bottom-centre origin and transparency, art on the bottom row, no transparent
hole enclosed by the art, and the output passes palcheck. Fullbright entries
appear only in flash, charge, beam and roar frames, never in idle or reload.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import bast  # noqa: E402
import palette  # noqa: E402
from test_viewmodels import enclosed_holes  # noqa: E402

TABLE = {
    5: ((112, 72), ["idle", "fire0", "fire1", "reload0"]),
    6: ((96, 80), ["idle", "charge0", "charge1", "fire0", "fire1"]),
    7: ((128, 80), ["idle", "roar0", "roar1", "roar2"]),
}
NO_FB = {"vm5_idle", "vm5_reload0", "vm6_idle", "vm7_idle"}


def main():
    entries = palette.load_gpl()
    fb = {i for i, (_, n) in enumerate(entries) if n.startswith("fb_")}
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "viewmodels")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "viewmodels.py"),
                        "-o", out, "--clean"], check=True)
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
                assert any(px[x * h + h - 1] != bast.TRANSPARENT for x in range(w)), \
                    "%s: bottom row is empty" % name
                holes = enclosed_holes(px, w, h)
                assert not holes, "%s: %d transparent px enclosed by art, e.g. %s" \
                    % (name, len(holes), holes[:4])
                has_fb = any(p in fb for p in set(px))
                assert has_fb == (name not in NO_FB), \
                    "%s: fullbright use %s unexpected" % (name, has_fb)
                print("ok %-12s %dx%d" % (name, w, h))
        got = {f for f in os.listdir(out) if f.endswith(".tex") and f[2] in "567"}
        assert got == want, "frame set differs: extra %s missing %s" % (got - want, want - got)
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), out],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
    print("viewmodels_5_7: %d frames ok" % len(want))


if __name__ == "__main__":
    main()
