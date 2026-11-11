"""Game textures for the editor (no wx): TEX files loaded through
tools/bast.py and turned into RGB through the game palette, nearest
neighbour only. The view wraps the bytes in wx bitmaps.

Search order: assets/gen/textures/<name>.tex, then assets/gen/test/, then
assets/gen/. game.pak is not read: no pak format exists on main yet (tech.md
"Archives", tools/pak.py is a later issue) and the engine itself loads loose
files from assets/gen/ until then."""
import os

from .model import bast

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "assets", "gen")
DEFAULT_DIRS = (os.path.join(GEN, "textures"), os.path.join(GEN, "test"), GEN)
TRANSPARENT_RGB = (255, 0, 255)  # index 255 shown as magenta, like tex2png previews


class TextureLibrary:
    def __init__(self, dirs=DEFAULT_DIRS, palette=None):
        self.dirs = list(dirs)
        self._pal = palette
        self._tex = {}
        self._rgb = {}

    def palette(self):
        if self._pal is None:
            self._pal = bast.load_palette()
        return self._pal

    def find(self, name):
        """Tex for `name`, or None if no file / unreadable. Cached."""
        if not name:
            return None
        if name not in self._tex:
            t = None
            for d in self.dirs:
                p = os.path.join(d, name + ".tex")
                if os.path.isfile(p):
                    try:
                        t = bast.Tex.load(p)
                    except bast.BastError:
                        t = None
                    break
            self._tex[name] = t
        return self._tex[name]

    def available(self):
        """Names of every .tex in the search path, sorted, first dir wins."""
        out = set()
        for d in self.dirs:
            if os.path.isdir(d):
                out.update(n[:-4] for n in os.listdir(d) if n.endswith(".tex"))
        return sorted(out)

    def rgb(self, name, width=None, height=None):
        """(w, h, bytes) row-major RGB of texture `name` scaled to w x h by
        nearest neighbour (default: native size), or None."""
        t = self.find(name)
        if t is None:
            return None
        w, h = width or t.width, height or t.height
        key = (name, w, h)
        if key not in self._rgb:
            cols = self.palette().colors
            lut = [bytes(c) for c in cols]
            lut[bast.TRANSPARENT] = bytes(TRANSPARENT_RGB)
            px, th = t.pixels, t.height
            xs = [(x * t.width // w) * th for x in range(w)]
            out = bytearray()
            for y in range(h):
                sy = y * th // h
                out += b"".join(lut[px[sx + sy]] for sx in xs)
            self._rgb[key] = bytes(out)
        return w, h, self._rgb[key]

    def clear(self):
        self._tex.clear()
        self._rgb.clear()
