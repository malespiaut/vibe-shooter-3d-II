#!/usr/bin/env python3
"""ctest sprites_items: tools/items.py (the tools/build.d/47-items.sh step)
builds every pickup sprite frame of docs/design/items.md from a clean output
directory: each file exists with the table's size, origin bottom-centre, at
least one opaque pixel on the bottom row, at most 3 ramps plus fb_*, never
black or white, and the output passes palcheck.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bast  # noqa: E402
import palette  # noqa: E402

# items.md: name -> (w, h, frames)
TABLE = {
    "fish": (16, 12, 1), "tuna": (24, 16, 1), "sardine": (24, 16, 2),
    "linen": (24, 16, 1), "plate": (32, 20, 2), "bandage": (8, 8, 1),
    "key_green": (16, 24, 2), "key_blue": (16, 24, 2), "key_violet": (16, 24, 2),
    "catnip": (16, 20, 2), "amulet": (16, 16, 1), "ninth": (16, 24, 1),
    # Ammo, Litter Satchel and weapon pickups (#85).
    "vial": (8, 16, 1), "jar": (16, 20, 1), "yarn": (16, 12, 1),
    "basket": (24, 20, 1), "scarab": (12, 10, 1), "crate_scarab": (28, 20, 1),
    "shard": (12, 16, 1), "orb": (20, 20, 2), "satchel": (24, 20, 1),
    "wpn3": (32, 11, 1), "wpn4": (32, 15, 1), "wpn5": (32, 11, 1),
    "wpn6": (32, 11, 1), "wpn7": (32, 14, 1),
}


def main():
    names = [n for _, n in palette.load_gpl()]
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "items")
        os.makedirs(out)
        stale = os.path.join(out, "stale.tex")
        open(stale, "wb").close()
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "items.py"),
                        "-o", out, "--clean"], check=True)
        assert not os.path.exists(stale), "--clean left a stale file"
        want = set()
        for sprite, (w, h, frames) in sorted(TABLE.items()):
            for f in range(frames):
                name = "%s_idle_d0_f%d" % (sprite, f)
                want.add(name + ".tex")
                path = os.path.join(out, name + ".tex")
                assert os.path.isfile(path), "missing %s" % name
                t = bast.Tex.load(path)
                assert (t.width, t.height) == (w, h), \
                    "%s is %dx%d, want %dx%d" % (name, t.width, t.height, w, h)
                assert t.origin == (w // 2, h), "%s origin %s, want bottom-centre %s" % (
                    name, t.origin, (w // 2, h))
                bottom = [t.pixels[x * h + h - 1] for x in range(w)]
                assert any(i != bast.TRANSPARENT for i in bottom), "%s: empty bottom row" % name
                used = set(t.pixels) - {bast.TRANSPARENT}
                ramps = set()
                for i in used:
                    n = names[i]
                    assert n not in ("black", "white"), "%s uses %s" % (name, n)
                    if not n.startswith("fb_"):
                        ramps.add(re.sub(r"_\d+$", "", n))
                assert len(ramps) <= 3, "%s: ramps %s" % (name, sorted(ramps))
                # Animated pickups really shimmer: frames differ.
                if f > 0:
                    prev = bast.Tex.load(os.path.join(out, "%s_idle_d0_f%d.tex" % (sprite, f - 1)))
                    assert prev.pixels != t.pixels, "%s: frame identical to previous" % name
                print("ok %-22s %dx%d ramps %s" % (name, w, h, ",".join(sorted(ramps))))
        got = {f for f in os.listdir(out) if f.endswith(".tex")}
        assert got == want, "unexpected/missing: %s" % sorted(got ^ want)
        assert len(want) == 33, len(want)
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), out],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
    print("sprites_items: OK, %d frames" % len(want))


if __name__ == "__main__":
    main()
