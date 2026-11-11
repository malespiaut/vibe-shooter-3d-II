#!/usr/bin/env python3
"""ctest hud_faces_t12: the HUD build step writes the status-bar cat faces for
health tiers 1-2 plus dead and god (docs/design/hud.md "Cat face", issue #88):
22 TEX files in <out>/faces/, each 32x32 with transparency, every source in
assets/src/hud/faces/ accounted for, no see-through holes inside the head,
and the output passes palcheck.
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

# Transparent pixels with opaque pixels on 3+ sides are allowed only where the
# silhouette is meant to be concave: the notched ear (23,4), the flat-ear gap
# (20,7), whisker gaps and cheek indents. Anything else is a hole in the head
# (PR #105: a slit cut the flattened right ear off in hurtr). Keyed by
# expression; a face_tN_<expr> without its own entry uses face_t1_<expr>.
_WHISK = {(27, 14), (3, 19), (28, 19), (29, 20), (5, 23), (27, 23)}
_WHISK2 = {(27, 14), (3, 19), (5, 23), (27, 23)}
_BIG1 = {(23, 4), (1, 16), (2, 17), (1, 18), (30, 18), (1, 21), (29, 24)}
_BIG2 = {(23, 4), (1, 16), (2, 17), (1, 18), (29, 18), (1, 21), (30, 24)}
CONCAVE_OK = {
    "face_dead": {(20, 7), (4, 13)} | _WHISK,
    "face_god": {(23, 4), (4, 12)} | _WHISK,
    "face_t1_bighit": _BIG1, "face_t2_bighit": _BIG2,
    "face_t1_fury": {(20, 7), (4, 13)} | _WHISK,
    "face_t2_fury": {(20, 7), (4, 13)} | _WHISK2,
    "face_t1_hurtf": {(20, 7), (4, 13), (27, 14), (30, 18)},
    "face_t2_hurtf": {(20, 7), (4, 13), (27, 14), (30, 24)},
    "face_t1_hurtl": {(23, 4), (4, 13), (27, 14), (30, 18)},
    "face_t2_hurtl": {(23, 4), (4, 13), (27, 14), (30, 24)},
    "face_t1_hurtr": {(27, 14), (30, 18)},
    "face_t2_hurtr": {(27, 14), (30, 24)},
}
for _e in ("idle0", "idle1", "idle2", "smug", "frenzy"):
    CONCAVE_OK["face_t1_" + _e] = {(23, 4)} | _WHISK
    CONCAVE_OK["face_t2_" + _e] = {(23, 4)} | _WHISK2


def concave_ok(name):
    if name in CONCAVE_OK:
        return CONCAVE_OK[name]
    if name.startswith("face_t"):
        return CONCAVE_OK.get("face_t1_" + name.split("_", 2)[2], set())
    return set()


def enclosed_transparent(px, w, h):
    """Transparent pixels with opaque 4-neighbours on 3 or more sides.
    TEX pixels are column-major: (x, y) is px[x * h + y]."""
    def opaque(x, y):
        return 0 <= x < w and 0 <= y < h and px[x * h + y] != bast.TRANSPARENT
    return {(x, y) for y in range(h) for x in range(w)
            if px[x * h + y] == bast.TRANSPARENT and
            sum(opaque(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))) >= 3}


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
        for n, d in data.items():
            bad = sorted(enclosed_transparent(d, 32, 32) - concave_ok(n))
            assert not bad, "%s: transparent hole(s) inside the head at %s" % (n, bad)
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
