#!/usr/bin/env python3
"""Review image of the colormap for the art director: 320x240 indexed PNG
carrying the game palette.

Layout: two blocks. Top block shows palette indices 0..127, bottom block
128..255. In a block, column c (2 px wide) is index base + c, and shade row r
(3 px tall, row 0 at the top = full light, row 31 at the bottom = darkest)
shows colormap[r][index]. Blocks sit at x = 32, y = 16 and y = 128 on a black
(index 0) background.

Usage: python3 tools/colormap_preview.py [out.png] [--colormap FILE] [--palette FILE]
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import colormap  # noqa: E402

COL_W, ROW_H, X0, Y0 = 2, 3, 32, (16, 128)


def preview(cmap, pal):
    rows = np.frombuffer(cmap, dtype=np.uint8).reshape(colormap.ROWS, 256)
    px = np.zeros((240, 320), dtype=np.uint8)
    for half in range(2):
        block = rows[:, half * 128:(half + 1) * 128]
        block = np.repeat(np.repeat(block, ROW_H, axis=0), COL_W, axis=1)
        y = Y0[half]
        px[y:y + block.shape[0], X0:X0 + block.shape[1]] = block
    img = Image.fromarray(px, mode="P")
    img.putpalette([c for rgb in pal.colors for c in rgb])
    return img


def main(argv=None):
    ap = argparse.ArgumentParser(description="colormap preview PNG")
    ap.add_argument("out", nargs="?",
                    default=os.path.join(bast.ROOT, "assets", "gen", "colormap_preview.png"))
    ap.add_argument("--colormap", default=colormap.DEFAULT_OUT)
    ap.add_argument("--palette", default=None)
    args = ap.parse_args(argv)
    with open(args.colormap, "rb") as f:
        cmap = f.read()
    if len(cmap) != colormap.ROWS * 256:
        sys.exit("colormap_preview: %s is %d bytes, need %d"
                 % (args.colormap, len(cmap), colormap.ROWS * 256))
    pal = bast.load_palette(args.palette)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    preview(cmap, pal).save(args.out)
    print("colormap_preview: wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
