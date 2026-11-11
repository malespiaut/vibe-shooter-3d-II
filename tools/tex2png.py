#!/usr/bin/env python3
"""Convert a TEX to an indexed PNG carrying the game palette (index 255 marked
transparent), through libbast (tools/bast.py).

Usage: python3 tools/tex2png.py in.tex out.png [--scale N]
"""
import argparse
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description="TEX -> PNG")
    ap.add_argument("tex")
    ap.add_argument("png")
    ap.add_argument("--scale", type=int, default=1, help="nearest-neighbour enlargement")
    args = ap.parse_args(argv)
    try:
        t = bast.Tex.load(args.tex)
        img = t.to_image()
        if args.scale > 1:
            img = img.resize((t.width * args.scale, t.height * args.scale), Image.NEAREST)
            img.info["transparency"] = bast.TRANSPARENT
        img.save(args.png)
    except (OSError, bast.BastError) as e:
        print("tex2png: %s" % e, file=sys.stderr)
        return 1
    print("tex2png: %s -> %s (%dx%d, origin %d,%d, flags %d)"
          % (args.tex, args.png, t.width, t.height, t.origin[0], t.origin[1], t.flags))
    return 0


if __name__ == "__main__":
    sys.exit(main())
