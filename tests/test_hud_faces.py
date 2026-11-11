#!/usr/bin/env python3
"""ctest hud_faces_t12: the HUD build step writes the status-bar cat faces for
health tiers 1-2 plus dead and god (docs/design/hud.md "Cat face", issue #88):
22 TEX files in <out>/faces/, each 32x32 with transparency, every source in
assets/src/hud/faces/ accounted for, and the output passes palcheck.
Runs tools/hud.py into a temporary directory, never touching assets/gen.
"""
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bast  # noqa: E402

EXPRS = ["idle0", "idle1", "idle2", "hurtl", "hurtr", "hurtf",
         "bighit", "fury", "smug", "frenzy"]
EXPECTED = ["face_t%d_%s" % (t, e) for t in (1, 2) for e in EXPRS] + ["face_dead", "face_god"]


def main():
    assert len(EXPECTED) == 22
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "hud")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "hud.py"), "-o", out, "--clean"],
                       check=True)
        faces = os.path.join(out, "faces")
        got = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(faces, "*.tex")))
        missing = sorted(set(EXPECTED) - set(got))
        assert not missing, "missing faces: %s" % missing
        for name in EXPECTED:
            t = bast.Tex.load(os.path.join(faces, name + ".tex"))
            assert (t.width, t.height) == (32, 32), \
                "%s is %dx%d, want 32x32" % (name, t.width, t.height)
            assert t.flags & bast.TEX_HAS_TRANSPARENCY, "%s: no transparency" % name
            px = bytes(t.pixels)
            assert px.count(bast.TRANSPARENT) < len(px), "%s: empty" % name
        # every pair of faces must differ (no copy left unedited)
        data = {n: bytes(bast.Tex.load(os.path.join(faces, n + ".tex")).pixels) for n in EXPECTED}
        seen = {}
        for n, d in data.items():
            assert d not in seen, "%s is identical to %s" % (n, seen.get(d))
            seen[d] = n
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), faces],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
        for n in EXPECTED:
            print("ok %s 32x32" % n)
    print("hud_faces_t12: OK, %d faces" % len(EXPECTED))
    return 0


if __name__ == "__main__":
    sys.exit(main())
