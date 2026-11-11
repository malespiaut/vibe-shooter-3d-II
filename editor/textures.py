"""Game textures for the editor (no wx): TEX files loaded through
tools/bast.py and turned into RGB through the game palette, nearest
neighbour only. The view wraps the bytes in wx bitmaps.

Search order: assets/gen/game.pak first, when it exists (VPAK, tech.md
"Archives", read through bast.pak_read), trying the entries
textures/<name>.tex, test/<name>.tex, then <name>.tex (pak names are paths
relative to assets/gen/). Then the loose files: assets/gen/textures/<name>.tex,
assets/gen/test/, assets/gen/. A TEX found in the pak is decoded by the same
libbast reader (bast.Tex.load) through a temporary file: no second parser."""
import os
import tempfile

from .model import bast

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "assets", "gen")
DEFAULT_DIRS = (os.path.join(GEN, "textures"), os.path.join(GEN, "test"), GEN)
DEFAULT_PAK = os.path.join(GEN, "game.pak")
PAK_PREFIXES = ("textures/", "test/", "")  # same order as DEFAULT_DIRS
TRANSPARENT_RGB = (255, 0, 255)  # index 255 shown as magenta, like tex2png previews


class TextureLibrary:
    def __init__(self, dirs=DEFAULT_DIRS, palette=None, pak=DEFAULT_PAK):
        self.dirs = list(dirs)
        self.pak = pak
        self._pak_entries = None  # name -> bytes, read once
        self._pal = palette
        self._tex = {}
        self._rgb = {}

    def palette(self):
        if self._pal is None:
            self._pal = bast.load_palette()
        return self._pal

    def _pak(self):
        """{entry name: bytes} of the pak, {} if there is none or it is
        unreadable (the loose files still work)."""
        if self._pak_entries is None:
            self._pak_entries = {}
            if self.pak and os.path.isfile(self.pak):
                try:
                    self._pak_entries = dict(bast.pak_read(self.pak))
                except bast.BastError:
                    self._pak_entries = {}
        return self._pak_entries

    def _pak_tex(self, name):
        """(found, Tex or None) for `name` from the pak."""
        entries = self._pak()
        for pre in PAK_PREFIXES:
            data = entries.get(pre + name + ".tex")
            if data is None:
                continue
            fd, tmp = tempfile.mkstemp(suffix=".tex")
            try:
                with os.fdopen(fd, "wb") as f:
                    f.write(data)
                return True, bast.Tex.load(tmp)
            except bast.BastError:
                return True, None
            finally:
                os.unlink(tmp)
        return False, None

    def find(self, name):
        """Tex for `name`, or None if no file / unreadable. Cached."""
        if not name:
            return None
        if name not in self._tex:
            found, t = self._pak_tex(name)
            for d in ([] if found else self.dirs):
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
        """Names of every .tex in the pak and the search path, sorted."""
        out = set()
        for e in self._pak():
            for pre in PAK_PREFIXES:
                rest = e[len(pre):]
                if e.startswith(pre) and e.endswith(".tex") and "/" not in rest:
                    out.add(rest[:-4])
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
        self._pak_entries = None
        self._tex.clear()
        self._rgb.clear()
