#!/usr/bin/env python3
"""Convert a PNG to TEX through libbast (tools/bast.py).

Indexed PNGs already in the game palette keep their indices; anything else is
matched by libbast nearest colour (no dithering, alpha < 128 -> index 255).

Usage: python3 tools/png2tex.py in.png out.tex [--origin X Y] [--fullbright]
"""
import argparse
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description="PNG -> TEX")
    ap.add_argument("png")
    ap.add_argument("tex")
    ap.add_argument("--origin", nargs=2, type=int, default=(0, 0), metavar=("X", "Y"),
                    help="sprite anchor in px from left / top (default 0 0)")
    ap.add_argument("--fullbright", action="store_true", help="set the TEX fullbright flag")
    args = ap.parse_args(argv)
    try:
        img = Image.open(args.png)
        img.load()
        t = bast.Tex.from_image(img, origin=args.origin, fullbright=args.fullbright)
        t.save(args.tex)
    except (OSError, bast.BastError) as e:
        print("png2tex: %s" % e, file=sys.stderr)
        return 1
    print("png2tex: %s -> %s (%dx%d, origin %d,%d, flags %d)"
          % (args.png, args.tex, t.width, t.height, t.origin[0], t.origin[1], t.flags))
    return 0


if __name__ == "__main__":
    sys.exit(main())
