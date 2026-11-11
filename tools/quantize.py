#!/usr/bin/env python3
"""Quantize RGBA images to TEX through libbast (GAME.md 4.2).

Every pixel goes through bast_tex_from_rgba() (tools/bast.py): alpha < 128
becomes index 255 (and sets the TEX has_transparency flag), anything else is
the libbast nearest palette entry (no dithering). No colour maths here.

Usage:
  python3 tools/quantize.py in.png -o out.tex [--origin X,Y | --origin bottom-centre]
                            [--fullbright] [--preview preview.png]
  python3 tools/quantize.py a.png b.png ... -o outdir/ [--preview previewdir/]

With several inputs (or when -o names an existing directory or ends with
"/"), each <name>.png is written as <outdir>/<name>.tex and --preview is a
directory too. --origin bottom-centre is (width // 2, height): the floor
contact point of a world sprite (docs/design/tech.md, Sprites), in px from
the left / top edge. --preview writes the quantized result as an indexed PNG
carrying the game palette, index 255 marked transparent.

Importable: quantize_rgba(rgba, w, h, origin, fullbright) -> TEX file bytes.
"""
import argparse
import os
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402

BOTTOM_CENTRE = "bottom-centre"


def resolve_origin(origin, w, h):
    """origin is (x, y) or "bottom-centre"; returns (x, y)."""
    if origin == BOTTOM_CENTRE:
        return (w // 2, h)
    x, y = origin
    return (int(x), int(y))


def quantize_tex(rgba, w, h, origin=(0, 0), fullbright=False, palette=None):
    """Row-major RGBA8 bytes -> bast.Tex (libbast nearest, alpha < 128 -> 255)."""
    t = bast.Tex.from_rgba(rgba, w, h, palette)
    t.origin = resolve_origin(origin, w, h)
    if fullbright:
        t.flags |= bast.TEX_FULLBRIGHT
    return t


def tex_bytes(t):
    """Serialize a bast.Tex through libbast (bast_tex_save) and return the bytes."""
    fd, path = tempfile.mkstemp(suffix=".tex")
    os.close(fd)
    try:
        t.save(path)
        with open(path, "rb") as f:
            return f.read()
    finally:
        os.unlink(path)


def quantize_rgba(rgba, w, h, origin=(0, 0), fullbright=False, palette=None):
    """Row-major RGBA8 bytes -> complete TEX file contents (bytes)."""
    return tex_bytes(quantize_tex(rgba, w, h, origin, fullbright, palette))


def quantize_file(src, dst, origin=(0, 0), fullbright=False, preview=None, palette=None):
    img = Image.open(src)
    img.load()
    w, h = img.size
    t = quantize_tex(img.convert("RGBA").tobytes(), w, h, origin, fullbright, palette)
    t.save(dst)
    if preview:
        t.to_image(palette).save(preview)
    return t


def parse_origin(s):
    if s in (BOTTOM_CENTRE, "bottom-center"):
        return BOTTOM_CENTRE
    try:
        x, y = s.split(",")
        return (int(x), int(y))
    except ValueError:
        raise argparse.ArgumentTypeError("origin must be X,Y or bottom-centre")


def _is_dir_arg(p):
    return p.endswith(os.sep) or p.endswith("/") or os.path.isdir(p)


def main(argv=None):
    ap = argparse.ArgumentParser(description="RGBA PNG -> TEX through libbast")
    ap.add_argument("inputs", nargs="+", metavar="in.png")
    ap.add_argument("-o", "--out", required=True, help="output .tex, or a directory")
    ap.add_argument("--origin", type=parse_origin, default=(0, 0),
                    help="X,Y in px from left/top, or bottom-centre (default 0,0)")
    ap.add_argument("--fullbright", action="store_true", help="set the TEX fullbright flag")
    ap.add_argument("--preview", help="indexed preview PNG (a directory with several inputs)")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)

    to_dir = len(args.inputs) > 1 or _is_dir_arg(args.out)
    try:
        pal = bast.load_palette()
        if to_dir:
            os.makedirs(args.out, exist_ok=True)
            if args.preview:
                os.makedirs(args.preview, exist_ok=True)
        for src in args.inputs:
            stem = os.path.splitext(os.path.basename(src))[0]
            dst = os.path.join(args.out, stem + ".tex") if to_dir else args.out
            prev = args.preview
            if prev and to_dir:
                prev = os.path.join(args.preview, stem + ".png")
            t = quantize_file(src, dst, args.origin, args.fullbright, prev, pal)
            if not args.quiet:
                print("quantize: %s -> %s (%dx%d, origin %d,%d, flags %d)"
                      % (src, dst, t.width, t.height, t.origin[0], t.origin[1], t.flags))
    except (OSError, bast.BastError) as e:
        print("quantize: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
