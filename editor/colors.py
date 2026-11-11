"""Swatch colour per texture index, taken from the game palette (via
tools/bast.py) so the editor shows in-palette colours. Index 0 (none / sky)
has fixed colours. No wx here."""
from .model import bast

NONE_RGB = (24, 24, 28)
SKY_RGB = (110, 170, 220)
_cache = None


def _usable():
    global _cache
    if _cache is None:
        try:
            pal = bast.load_palette()
            cols, flags = pal.colors, pal.flags
            mask = bast.PAL_RESERVED | bast.PAL_TRANSPARENT | bast.PAL_FULLBRIGHT
            # Mid-bright entries only, so swatches read against the grid lines.
            _cache = [c for i, c in enumerate(cols)
                      if not flags[i] & mask and 60 <= sum(c) // 3 <= 210]
        except bast.BastError:
            _cache = []
        if not _cache:
            _cache = [(160, 120, 80), (90, 140, 90), (120, 120, 170), (170, 90, 90)]
    return _cache


def texture_rgb(index, sky=False):
    if index == 0:
        return SKY_RGB if sky else NONE_RGB
    cols = _usable()
    return cols[(index * 47 + 11) % len(cols)]


def dark_grey(dark):
    """Grey level for the light view: cell darkness 0 (bright) .. 31 (black),
    linear like the colormap rows."""
    return 255 - dark * 255 // 31
