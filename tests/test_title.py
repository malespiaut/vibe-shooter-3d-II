#!/usr/bin/env python3
"""ctest title_screen: tools/title.py builds the title screen (hud.md Flow,
Title) as a 320x240 opaque TEX that passes palcheck, keeps the PRESS ANY KEY
band (y 200-220) calm, and is byte-identical across two builds.

Output goes to a temporary directory; assets/gen is not touched.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bast  # noqa: E402


def build(out):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "title.py"), "-o", out], check=True)
    return os.path.join(out, "title.tex")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        a = build(os.path.join(tmp, "a"))
        b = build(os.path.join(tmp, "b"))
        t = bast.Tex.load(a)
        assert (t.width, t.height) == (320, 240), "title is %dx%d" % (t.width, t.height)
        assert bast.TRANSPARENT not in t.pixels, "title has a transparent pixel"
        assert not t.flags & bast.TEX_HAS_TRANSPARENCY, "title TEX is flagged transparent"
        assert open(a, "rb").read() == open(b, "rb").read(), "title build is not deterministic"
        # The blinking PRESS ANY KEY band: few distinct colours so the menu font reads.
        band = {t.get(x, y) for y in range(200, 221) for x in range(60, 260)}
        assert len(band) <= 4, "band y 200-220 is busy: %d colours" % len(band)
        print("ok title 320x240 opaque, deterministic, band colours %d" % len(band))
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "palcheck.py"), os.path.dirname(a)],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stdout + r.stderr
    return 0


if __name__ == "__main__":
    sys.exit(main())
