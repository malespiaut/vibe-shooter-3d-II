#!/usr/bin/env python3
"""ctest hud_faces_t345: the HUD build step writes the status-bar cat faces for
health tiers 3-5 (docs/design/hud.md "Cat face", issue #89): 30 TEX files
face_t<3..5>_<expr> in <out>/faces/, each 32x32 with transparency, all 52 faces
(tiers 1-5, dead, god) present and pairwise different, damage that builds up
tier to tier, no see-through holes inside the head, and palcheck passes.
Runs tools/hud.py into a temporary directory, never touching assets/gen.
"""
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import bast  # noqa: E402
import palette  # noqa: E402
from test_hud_faces import EXPRS, concave_ok, enclosed_transparent  # noqa: E402

NEW = ["face_t%d_%s" % (t, e) for t in (3, 4, 5) for e in EXPRS]
ALL = ["face_t%d_%s" % (t, e) for t in range(1, 6) for e in EXPRS] + ["face_dead", "face_god"]

# Legend entries that mark each tier's damage (assets/src/hud/faces/legend.txt).
BLOOD = {"red_06", "red_09"}
LINEN = {"gray_08", "gray_11", "gray_14"}
# Brow/dirt ink that must not be left beside the bandage pad (a stray dot).
INK = "steel_00"

# Concavities a tier 3-5 face may add on top of its tier 2 counterpart: the
# fresh notch cut in hurtr's flattened ear (25,7) and, at tier 5, the cheek
# indent under the flattened left ear (4,13), as in face_t1_hurtl.
EXTRA_OK = {"hurtr": {(25, 7)}}
T5_EXTRA_OK = {(4, 13)}


def allowed(name):
    t, e = int(name[6]), name.split("_", 2)[2]
    ok = set(concave_ok("face_t2_" + e)) | EXTRA_OK.get(e, set())
    if t == 5:
        ok |= T5_EXTRA_OK
    return ok


def main():
    assert len(NEW) == 30 and len(ALL) == 52
    names = {n: i for i, (_, n) in enumerate(palette.load_gpl())}
    blood = {names[n] for n in BLOOD}
    linen = {names[n] for n in LINEN}
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "hud")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "hud.py"), "-o", out, "--clean"],
                       check=True, stdout=subprocess.DEVNULL)
        faces = os.path.join(out, "faces")
        got = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(faces, "*.tex")))
        missing = sorted(set(ALL) - set(got))
        assert not missing, "missing faces: %s" % missing
        extra = sorted(n for n in set(got) - set(ALL) if n.startswith("face_"))
        assert not extra, "unexpected face files: %s" % extra
        data = {}
        for name in ALL:
            t = bast.Tex.load(os.path.join(faces, name + ".tex"))
            assert (t.width, t.height) == (32, 32), \
                "%s is %dx%d, want 32x32" % (name, t.width, t.height)
            assert t.flags & bast.TEX_HAS_TRANSPARENCY, "%s: no transparency" % name
            data[name] = bytes(t.pixels)
            assert data[name].count(bast.TRANSPARENT) < len(data[name]), "%s: empty" % name
        seen = {}
        for n, d in data.items():
            assert d not in seen, "%s is identical to %s" % (n, seen.get(d))
            seen[d] = n
        # Damage builds up: tiers 1-2 have no blood or linen, tier 3 bleeds,
        # tiers 4-5 bleed and wear the bandage; tier 5 differs from tier 4.
        for e in EXPRS:
            for t in (1, 2, 3, 4, 5):
                px = set(data["face_t%d_%s" % (t, e)])
                has_b, has_l = bool(px & blood), bool(px & linen)
                assert has_b == (t >= 3), "face_t%d_%s: blood %s" % (t, e, has_b)
                assert has_l == (t >= 4), "face_t%d_%s: bandage %s" % (t, e, has_l)
            # tier 4 keeps tier 3's blood pixels where the bandage does not cover them
            d3, d4 = data["face_t3_" + e], data["face_t4_" + e]
            kept = [i for i, c in enumerate(d3) if c in blood and d4[i] not in linen]
            assert all(d4[i] in blood for i in kept), "face_t4_%s lost tier 3 blood" % e
        # No leftover ink pixel 4-adjacent to linen in the bandaged tiers.
        # TEX pixels are column-major, so index i is grid (i // 32, i % 32).
        ink = names[INK]
        for n in NEW:
            if n[6] not in "45":
                continue
            d = data[n]
            near = sorted((i // 32, i % 32) for i, c in enumerate(d) if c == ink and any(
                0 <= x < 32 and 0 <= y < 32 and d[y * 32 + x] in linen
                for x, y in ((i % 32 - 1, i // 32), (i % 32 + 1, i // 32),
                             (i % 32, i // 32 - 1), (i % 32, i // 32 + 1))))
            assert not near, "%s: ink pixel(s) beside the bandage at %s" % (n, near)
        for n in NEW:
            bad = sorted(enclosed_transparent(data[n], 32, 32) - allowed(n))
            assert not bad, "%s: transparent hole(s) inside the head at %s" % (n, bad)
            print("ok %s 32x32" % n)
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), faces],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
    print("hud_faces_t345: OK, %d new faces, %d faces in total" % (len(NEW), len(ALL)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
