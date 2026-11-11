#!/usr/bin/env python3
"""Build assets/gen/colormap.bin: 32 shade rows x 256 palette indices.

Rule (docs/design/art-bible.md, "Colormap"; GAME.md 2.3), for row r in 0..31:
    f = (32 - r) / 32, t = r / 31
    target = (R*f, G*f*(1 - 0.10*t), B*f*(1 - 0.25*t))      # warm shift
    entry  = Oklab nearest via libbast (bast.nearest), which already skips
             reserved, fullbright and transparent entries, ties -> lowest.
Row 0 is identity. Fullbright entries and 255 map to themselves in every row.

Monotonic clamp: if the nearest entry for row r is brighter (higher Oklab L)
than the entry chosen for row r-1, row r keeps row r-1's entry. Pure nearest
can oscillate near black, where the palette has a gap between black (L 0) and
the darkest ramp entries (L ~0.14-0.17); the clamp guarantees that shading
never brightens with distance (issue #6 acceptance). The number of clamped
cells is printed.

Same-ramp shading (issue #109): entries of the ramps in SAME_RAMP (`blue`)
take the nearest entry among their own ramp, the `shadow` entries and `black`
only, by the same Oklab distance (ties -> lowest index). With the full
palette, the warm-shifted target of light blue finds no darker blue close
enough and lands on near-neutral `steel` from row ~14, so a Blue Ankh in shade
reads grey. `blue` is a key colour (items.md); restricting it to its own ramp
keeps its hue down to the shadow steps. The warm-shift target is unchanged.
Ramp membership comes from the entry names in the .gpl (`blue_NN`, ...).

The float target is rounded to whole sRGB values (floor(x + 0.5)) before the
lookup, because libbast's matcher (the only Oklab implementation, tech.md)
takes 8-bit sRGB. The error is at most half a step per channel.

Usage: python3 tools/colormap.py [--palette assets/gen/palette.bin] [--gpl FILE] [--out FILE]
(libbast.so from $BAST_LIB or build/libbast.so)
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import palette as gpl  # noqa: E402

ROWS = 32
# Key-colour ramps that shade within themselves (plus shadow_* and black).
SAME_RAMP = ("blue",)
DEFAULT_OUT = os.path.join(bast.ROOT, "assets", "gen", "colormap.bin")


def round_half_up(x):
    return int(math.floor(x + 0.5))


def target(rgb, row):
    f = (32 - row) / 32
    t = row / 31
    r, g, b = rgb
    return (r * f, g * f * (1 - 0.10 * t), b * f * (1 - 0.25 * t))


def ramp_candidates(names):
    """Map palette index -> candidate index list for SAME_RAMP entries."""
    if names is None:
        return {}
    out = {}
    for ramp in SAME_RAMP:
        own = [i for i, n in enumerate(names) if n.startswith(ramp + "_")]
        dark = [i for i, n in enumerate(names) if n.startswith("shadow_") or n == "black"]
        if not own:
            raise SystemExit("colormap: palette has no %s_* entries" % ramp)
        cands = sorted(own + dark)
        for i in own:
            out[i] = cands
    return out


def nearest_in(rgb, cands, lab):
    """Oklab-nearest of cands to rgb, ties -> lowest index (as libbast)."""
    t = bast.srgb_to_oklab(rgb)
    return min(cands, key=lambda j: (sum((t[k] - lab[j][k]) ** 2 for k in range(3)), j))


def build(pal, names=None):
    """Return (colormap bytes, number of clamped cells). names: the 256 .gpl
    entry names, needed for SAME_RAMP; None shades every entry against the
    whole palette."""
    colors, flags = pal.colors, pal.flags
    lab = [bast.srgb_to_oklab(c) for c in colors]
    lum = [c[0] for c in lab]
    same = ramp_candidates(names)
    out = bytearray(ROWS * 256)
    clamped = 0
    for row in range(ROWS):
        for i in range(256):
            if row == 0 or flags[i] & (bast.PAL_FULLBRIGHT | bast.PAL_TRANSPARENT):
                v = i
            else:
                rgb = tuple(round_half_up(c) for c in target(colors[i], row))
                v = nearest_in(rgb, same[i], lab) if i in same else pal.nearest(rgb)
                prev = out[(row - 1) * 256 + i]
                if lum[v] > lum[prev]:
                    v = prev
                    clamped += 1
            out[row * 256 + i] = v
    return bytes(out), clamped


def main(argv=None):
    ap = argparse.ArgumentParser(description="build the 32-row colormap")
    ap.add_argument("--palette", default=None, help="palette.bin (default: $BAST_PALETTE "
                    "or assets/gen/palette.bin; palette_flags.bin next to it)")
    ap.add_argument("--gpl", default=gpl.DEFAULT_GPL,
                    help="palette source, for ramp names (default: %(default)s)")
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args(argv)
    pal = bast.load_palette(args.palette)
    entries = gpl.load_gpl(args.gpl)
    if [rgb for rgb, _ in entries] != list(pal.colors):
        raise SystemExit("colormap: %s does not match the palette" % args.gpl)
    data, clamped = build(pal, [n for _, n in entries])
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    tmp = args.out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, args.out)
    print("colormap: wrote %s (%d bytes, %d cells clamped for monotonic L)"
          % (args.out, len(data), clamped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
