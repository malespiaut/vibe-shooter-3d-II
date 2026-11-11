#!/usr/bin/env python3
"""ctest hud_assets: the HUD build step (tools/build.d/50-hud.sh) regenerates
every status bar / icon TEX listed in docs/design/hud.md with the exact size,
from a clean output directory, and the output passes palcheck.

The build step is run with its output redirected to a temporary copy of the
tree layout (tools/hud.py -o), so the test never touches assets/gen.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bast  # noqa: E402

EXPECTED = {
    "statusbar": (320, 32),
    "icon_fish": (12, 12),
    "icon_scarab": (8, 8),
    "key_green": (12, 16),
    "key_blue": (12, 16),
    "key_violet": (12, 16),
    "key_socket": (12, 16),
    "ammo_charge": (6, 6),
    "ammo_yarn": (6, 6),
    "ammo_scarab": (6, 6),
    "ammo_sun": (6, 6),
    "menu_ankh": (12, 16),
}
EXPECTED.update({"weapon_%d" % i: (48, 20) for i in range(1, 8)})


def main():
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "hud")
        os.makedirs(out)
        stale = os.path.join(out, "stale.tex")
        open(stale, "wb").close()
        # Same command as the build step, pointed at the temp dir.
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "hud.py"), "-o", out, "--clean"],
                       check=True)
        for name, (w, h) in sorted(EXPECTED.items()):
            path = os.path.join(out, name + ".tex")
            assert os.path.isfile(path), "missing %s" % path
            t = bast.Tex.load(path)
            assert (t.width, t.height) == (w, h), \
                "%s is %dx%d, want %dx%d" % (name, t.width, t.height, w, h)
            if name != "statusbar":
                assert t.flags & bast.TEX_HAS_TRANSPARENCY, "%s: icon has no transparency" % name
            else:
                assert bast.TRANSPARENT not in t.pixels, "statusbar must be opaque"
            print("ok %-12s %dx%d" % (name, w, h))
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), out],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
        assert not os.path.exists(stale), "--clean left a stale file"
        script = open(os.path.join(ROOT, "tools", "build.d", "50-hud.sh")).read()
        assert "--clean" in script, "build step must rebuild from a clean directory"
    print("hud_assets: OK, %d files" % len(EXPECTED))
    return 0


if __name__ == "__main__":
    sys.exit(main())
