#!/usr/bin/env python3
"""Read catlantis.gpl and build assets/gen/palette.bin + palette_flags.bin.

palette.bin:       768 bytes, 256 x RGB.
palette_flags.bin: 256 bytes, bit0 reserved, bit1 fullbright, bit2 transparent.

Also importable: load_gpl() / flags() are the single palette reader for the
Python tools (palcheck, swatch, later colormap).

Usage: python3 tools/palette.py [--gpl FILE] [--out-dir DIR]
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_GPL = os.path.join(ROOT, "assets", "src", "palette", "catlantis.gpl")
DEFAULT_OUT = os.path.join(ROOT, "assets", "gen")

FLAG_RESERVED = 1 << 0
FLAG_FULLBRIGHT = 1 << 1
FLAG_TRANSPARENT = 1 << 2


class PaletteError(Exception):
    pass


def load_gpl(path=DEFAULT_GPL):
    """Return a list of 256 ((r, g, b), name) tuples."""
    entries = []
    with open(path) as f:
        lines = f.read().splitlines()
    if not lines or lines[0].strip() != "GIMP Palette":
        raise PaletteError("%s: not a GIMP palette" % path)
    for no, line in enumerate(lines[1:], start=2):
        s = line.strip()
        if not s or s.startswith("#") or ":" in s.split("\t")[0]:
            continue
        parts = s.split(None, 3)
        if len(parts) != 4:
            raise PaletteError("%s:%d: entry needs R G B name" % (path, no))
        try:
            rgb = tuple(int(p) for p in parts[:3])
        except ValueError:
            raise PaletteError("%s:%d: bad colour" % (path, no))
        if any(not 0 <= c <= 255 for c in rgb):
            raise PaletteError("%s:%d: colour out of range" % (path, no))
        entries.append((rgb, parts[3].strip()))
    if len(entries) != 256:
        raise PaletteError("%s: %d entries, need 256" % (path, len(entries)))
    names = [n for _, n in entries]
    if len(set(names)) != 256:
        raise PaletteError("%s: duplicate entry names" % path)
    return entries


def flags(entries):
    out = []
    for i, (_, name) in enumerate(entries):
        f = 0
        if name.startswith("reserved_"):
            f |= FLAG_RESERVED
        if name.startswith("fb_"):
            f |= FLAG_FULLBRIGHT
        if i == 255:
            f |= FLAG_TRANSPARENT
        out.append(f)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--gpl", default=DEFAULT_GPL)
    ap.add_argument("--out-dir", default=DEFAULT_OUT)
    args = ap.parse_args(argv)
    try:
        entries = load_gpl(args.gpl)
    except (OSError, PaletteError) as e:
        print("palette: %s" % e, file=sys.stderr)
        return 1
    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, "palette.bin"), "wb") as f:
        f.write(bytes(c for rgb, _ in entries for c in rgb))
    with open(os.path.join(args.out_dir, "palette_flags.bin"), "wb") as f:
        f.write(bytes(flags(entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
