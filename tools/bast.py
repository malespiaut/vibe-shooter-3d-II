"""ctypes wrapper over libbast.so, the single implementation of the shared
formats (palette, Oklab nearest colour, TEX, SND, .vmap). No format or colour maths here:
everything goes through the C library.

Library lookup: $BAST_LIB if set, else <repo>/build/libbast.so.
Palette lookup: $BAST_PALETTE if set, else <repo>/assets/gen/palette.bin
(palette_flags.bin is read from the same directory). Both are built by
`cmake --build build`.

    import bast
    bast.nearest((255, 255, 255))            # -> 1
    t = bast.Tex.from_image(Image.open("a.png"))
    t.save("a.tex"); bast.Tex.load("a.tex").to_image().save("b.png")
    m = bast.Map.load("maps/m1test.vmap"); m.get_cell(3, 4)["dark"]
"""
import ctypes
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LIB = os.path.join(ROOT, "build", "libbast.so")
DEFAULT_PALETTE = os.path.join(ROOT, "assets", "gen", "palette.bin")

PAL_RESERVED = 0x01
PAL_FULLBRIGHT = 0x02
PAL_TRANSPARENT = 0x04
ALLOW_FULLBRIGHT = 0x01
ALLOW_TRANSPARENT = 0x02
TEX_HAS_TRANSPARENCY = 0x01
TEX_FULLBRIGHT = 0x02
TRANSPARENT = 255
SND_RATE = 22050


class BastError(Exception):
    pass


class _Palette(ctypes.Structure):
    _fields_ = [("rgb", (ctypes.c_uint8 * 3) * 256),
                ("flags", ctypes.c_uint8 * 256),
                ("lab", (ctypes.c_double * 3) * 256)]


class _Snd(ctypes.Structure):
    _fields_ = [("rate", ctypes.c_uint32), ("count", ctypes.c_uint32),
                ("samples", ctypes.POINTER(ctypes.c_int8))]


class _Tex(ctypes.Structure):
    _fields_ = [("width", ctypes.c_uint16), ("height", ctypes.c_uint16),
                ("origin_x", ctypes.c_int16), ("origin_y", ctypes.c_int16),
                ("flags", ctypes.c_uint8),
                ("pixels", ctypes.POINTER(ctypes.c_uint8))]


class _MapCell(ctypes.Structure):
    _fields_ = [("wall", ctypes.c_uint8), ("floor", ctypes.c_uint8),
                ("ceil", ctypes.c_uint8), ("dark", ctypes.c_uint8),
                ("dark_alt", ctypes.c_uint8), ("flags", ctypes.c_uint16),
                ("tag", ctypes.c_uint8), ("param", ctypes.c_uint8)]


class _MapEntity(ctypes.Structure):
    _fields_ = [("type", ctypes.c_uint16), ("x", ctypes.c_int32), ("y", ctypes.c_int32),
                ("angle", ctypes.c_uint16), ("flags", ctypes.c_uint16),
                ("tag", ctypes.c_uint8), ("param", ctypes.c_uint8),
                ("name", ctypes.c_char * 17)]


class _Map(ctypes.Structure):
    _fields_ = [("width", ctypes.c_uint8), ("height", ctypes.c_uint8),
                ("name", ctypes.c_char * 33),
                ("sky_tex", ctypes.c_uint8), ("music", ctypes.c_uint8),
                ("par_seconds", ctypes.c_uint16), ("texture_count", ctypes.c_uint16),
                ("textures", (ctypes.c_char * 17) * 256),
                ("cells", ctypes.POINTER(_MapCell)),
                ("entity_count", ctypes.c_uint16), ("entity_capacity", ctypes.c_uint16),
                ("entities", ctypes.POINTER(_MapEntity))]


_lib = None


def lib():
    """Load libbast.so once and declare the prototypes."""
    global _lib
    if _lib is not None:
        return _lib
    path = os.environ.get("BAST_LIB") or DEFAULT_LIB
    if not os.path.exists(path):
        raise BastError("%s not found: run `cmake --build build` or set BAST_LIB" % path)
    L = ctypes.CDLL(os.path.abspath(path))
    P, T, S = ctypes.POINTER(_Palette), ctypes.POINTER(_Tex), ctypes.POINTER(_Snd)
    M = ctypes.POINTER(_Map)
    u8, c_int = ctypes.c_uint8, ctypes.c_int
    sig = {
        "bast_error": (ctypes.c_char_p, []),
        "bast_palette_load": (P, [ctypes.c_char_p]),
        "bast_palette_free": (None, [P]),
        "bast_palette_nearest_ex": (c_int, [P, u8, u8, u8, ctypes.c_uint]),
        "bast_srgb_to_oklab": (None, [u8, u8, u8, ctypes.c_double * 3]),
        "bast_oklab_to_srgb": (None, [ctypes.c_double, ctypes.c_double, ctypes.c_double,
                                      u8 * 3]),
        "bast_tex_new": (T, [c_int, c_int]),
        "bast_tex_load": (T, [ctypes.c_char_p]),
        "bast_tex_save": (c_int, [T, ctypes.c_char_p]),
        "bast_tex_free": (None, [T]),
        "bast_tex_from_rgba": (T, [P, ctypes.c_char_p, c_int, c_int]),
        "bast_snd_new": (S, [ctypes.c_int64]),
        "bast_snd_load": (S, [ctypes.c_char_p]),
        "bast_snd_save": (c_int, [S, ctypes.c_char_p]),
        "bast_snd_free": (None, [S]),
        "bast_map_new": (M, [c_int, c_int]),
        "bast_map_load": (M, [ctypes.c_char_p]),
        "bast_map_save": (c_int, [M, ctypes.c_char_p]),
        "bast_map_free": (None, [M]),
        "bast_map_cell_at": (ctypes.POINTER(_MapCell), [M, c_int, c_int]),
        "bast_map_add_entity": (c_int, [M]),
        "bast_map_remove_entity": (c_int, [M, c_int]),
        "bast_map_texture_index": (c_int, [M, ctypes.c_char_p]),
        "bast_entity_type_name": (ctypes.c_char_p, [c_int]),
        "bast_entity_type_from_name": (c_int, [ctypes.c_char_p]),
        "bast_cell_flag_name": (ctypes.c_char_p, [c_int]),
        "bast_entity_flag_name": (ctypes.c_char_p, [c_int]),
    }
    for name, (res, args) in sig.items():
        fn = getattr(L, name)
        fn.restype, fn.argtypes = res, args
    _lib = L
    return L


def _err():
    return lib().bast_error().decode("utf-8", "replace")


class Palette:
    """A loaded game palette (owns the C struct)."""

    def __init__(self, path=None):
        path = path or os.environ.get("BAST_PALETTE") or DEFAULT_PALETTE
        self._p = lib().bast_palette_load(os.fsencode(path))
        if not self._p:
            raise BastError(_err())
        self.path = path

    def __del__(self):
        if getattr(self, "_p", None) and _lib is not None:
            _lib.bast_palette_free(self._p)
            self._p = None

    @property
    def colors(self):
        """List of 256 (r, g, b) tuples."""
        return [tuple(c) for c in self._p.contents.rgb]

    @property
    def flags(self):
        return list(self._p.contents.flags)

    def nearest(self, rgb, fullbright=False, transparent=False):
        allow = (ALLOW_FULLBRIGHT if fullbright else 0) | (ALLOW_TRANSPARENT if transparent else 0)
        r, g, b = rgb
        return lib().bast_palette_nearest_ex(self._p, r, g, b, allow)


_default_palette = None


def load_palette(path=None):
    """Load a palette; with no path, return the cached default game palette."""
    global _default_palette
    if path is not None:
        return Palette(path)
    if _default_palette is None:
        _default_palette = Palette()
    return _default_palette


def nearest(rgb, fullbright=False, transparent=False, palette=None):
    """Nearest game palette index (Oklab), skipping reserved entries and,
    unless asked, fullbright and transparent ones. Ties go to the lowest index."""
    return (palette or load_palette()).nearest(rgb, fullbright, transparent)


def srgb_to_oklab(rgb):
    out = (ctypes.c_double * 3)()
    lib().bast_srgb_to_oklab(rgb[0], rgb[1], rgb[2], out)
    return tuple(out)


def oklab_to_srgb(lab):
    """Gamut-mapped (chroma reduction at constant L and hue), rounded sRGB."""
    out = (ctypes.c_uint8 * 3)()
    lib().bast_oklab_to_srgb(lab[0], lab[1], lab[2], out)
    return tuple(out)


class Tex:
    """A TEX image held in Python. `pixels` is a bytearray in the file's
    COLUMN-MAJOR order: pixel (x, y) is pixels[x * height + y]."""

    def __init__(self, width, height, pixels=None, origin=(0, 0), flags=0):
        if not (1 <= width <= 1024 and 1 <= height <= 1024):
            raise BastError("bad TEX size %dx%d" % (width, height))
        self.width, self.height = width, height
        self.pixels = bytearray(pixels if pixels is not None else width * height)
        if len(self.pixels) != width * height:
            raise BastError("pixel count %d != %dx%d" % (len(self.pixels), width, height))
        self.origin = tuple(origin)
        self.flags = flags

    def get(self, x, y):
        return self.pixels[x * self.height + y]

    @classmethod
    def _from_c(cls, t):
        c = t.contents
        n = c.width * c.height
        px = ctypes.string_at(c.pixels, n)
        out = cls(c.width, c.height, px, (c.origin_x, c.origin_y), c.flags)
        lib().bast_tex_free(t)
        return out

    @classmethod
    def load(cls, path):
        t = lib().bast_tex_load(os.fsencode(path))
        if not t:
            raise BastError(_err())
        return cls._from_c(t)

    def save(self, path):
        L = lib()
        t = L.bast_tex_new(self.width, self.height)
        if not t:
            raise BastError(_err())
        try:
            c = t.contents
            c.origin_x, c.origin_y = self.origin
            c.flags = self.flags
            ctypes.memmove(c.pixels, bytes(self.pixels), len(self.pixels))
            if L.bast_tex_save(t, os.fsencode(path)) != 0:
                raise BastError(_err())
        finally:
            L.bast_tex_free(t)

    @classmethod
    def from_rgba(cls, rgba, width, height, palette=None):
        """Row-major RGBA8 bytes -> Tex via bast_tex_from_rgba (alpha < 128 ->
        255, Oklab nearest, no dithering)."""
        pal = palette or load_palette()
        if len(rgba) != width * height * 4:
            raise BastError("rgba is %d bytes, need %d" % (len(rgba), width * height * 4))
        t = lib().bast_tex_from_rgba(pal._p, bytes(rgba), width, height)
        if not t:
            raise BastError(_err())
        return cls._from_c(t)

    @classmethod
    def from_image(cls, img, palette=None, origin=(0, 0), fullbright=False):
        """PIL image -> Tex.

        An indexed (P) image whose used entries all match the game palette
        (same index, same RGB, not reserved) keeps its indices as they are,
        so fullbright and transparent indices survive. Anything else is
        converted to RGBA and matched by bast_tex_from_rgba."""
        pal = palette or load_palette()
        w, h = img.size
        t = None
        if img.mode == "P":
            t = cls._from_indexed(img, pal)
        if t is None:
            t = cls.from_rgba(img.convert("RGBA").tobytes(), w, h, pal)
        t.origin = tuple(origin)
        if fullbright:
            t.flags |= TEX_FULLBRIGHT
        return t

    @classmethod
    def _from_indexed(cls, img, pal):
        w, h = img.size
        raw = img.getpalette() or []
        colors, fl = pal.colors, pal.flags
        idx = img.tobytes()  # row-major
        for i in set(idx):
            if fl[i] & PAL_RESERVED or tuple(raw[i * 3:i * 3 + 3]) != colors[i]:
                return None
        px = bytearray(w * h)
        for y in range(h):
            row = idx[y * w:(y + 1) * w]
            px[y::h] = row  # column-major: (x, y) -> x * h + y
        flags = TEX_HAS_TRANSPARENCY if TRANSPARENT in set(idx) else 0
        return cls(w, h, px, (0, 0), flags)

    def to_image(self, palette=None):
        """Indexed PIL image carrying the full game palette; index 255 is
        marked transparent in the PNG."""
        from PIL import Image
        pal = palette or load_palette()
        w, h = self.width, self.height
        rows = bytearray(w * h)
        for y in range(h):
            rows[y * w:(y + 1) * w] = self.pixels[y::h]
        img = Image.frombytes("P", (w, h), bytes(rows))
        img.putpalette([c for rgb in pal.colors for c in rgb])
        img.info["transparency"] = TRANSPARENT
        return img


class Snd:
    """A SND1 sound effect: 22050 Hz signed 8-bit mono. `samples` is a bytes
    object holding the raw int8 PCM (two's complement)."""

    def __init__(self, samples=b"", rate=SND_RATE):
        if rate != SND_RATE:
            raise BastError("SND rate must be %d" % SND_RATE)
        self.rate = rate
        self.samples = bytes(samples)

    def __len__(self):
        return len(self.samples)

    @classmethod
    def load(cls, path):
        L = lib()
        s = L.bast_snd_load(os.fsencode(path))
        if not s:
            raise BastError(_err())
        try:
            c = s.contents
            return cls(ctypes.string_at(c.samples, c.count), c.rate)
        finally:
            L.bast_snd_free(s)

    def save(self, path):
        L = lib()
        s = L.bast_snd_new(len(self.samples))
        if not s:
            raise BastError(_err())
        try:
            ctypes.memmove(s.contents.samples, self.samples, len(self.samples))
            if L.bast_snd_save(s, os.fsencode(path)) != 0:
                raise BastError(_err())
        finally:
            L.bast_snd_free(s)


# ---- map (.vmap) ---------------------------------------------------------

CELL_FIELDS = ("wall", "floor", "ceil", "dark", "dark_alt", "flags", "tag", "param")
ENTITY_FIELDS = ("type", "x", "y", "angle", "flags", "tag", "param", "name")


def entity_type_name(t):
    """'PLAYER_START' etc. for a type number, None if unknown."""
    n = lib().bast_entity_type_name(t)
    return n.decode() if n else None


def entity_type(name_or_number):
    """Type number from a name ('WAYPOINT') or a number; BastError if unknown."""
    if isinstance(name_or_number, int):
        return name_or_number
    t = lib().bast_entity_type_from_name(name_or_number.encode())
    if not t:
        raise BastError("unknown entity type %r" % name_or_number)
    return t


def _flag_table(fn):
    out = {}
    for bit in range(16):
        n = fn(bit)
        if n:
            out[n.decode()] = 1 << bit
    return out


def cell_flags():
    """{'SOLID': 0x0001, ...} as defined by libbast."""
    return _flag_table(lib().bast_cell_flag_name)


def entity_flags():
    """{'SKILL1': 0x0001, ..., 'PICKUP_TRIGGER': 0x0020} as defined by libbast."""
    return _flag_table(lib().bast_entity_flag_name)


class Map:
    """A .vmap held by libbast (owns the C struct). Cells are (x, y) with y
    growing north, (0, 0) south-west. Cell and entity values are plain dicts
    with the keys in CELL_FIELDS / ENTITY_FIELDS; entity x, y are 16.16."""

    def __init__(self, width=None, height=None, _ptr=None):
        if _ptr is None:
            _ptr = lib().bast_map_new(width, height)
            if not _ptr:
                raise BastError(_err())
        self._m = _ptr

    def __del__(self):
        if getattr(self, "_m", None) and _lib is not None:
            _lib.bast_map_free(self._m)
            self._m = None

    @classmethod
    def load(cls, path):
        m = lib().bast_map_load(os.fsencode(path))
        if not m:
            raise BastError(_err())
        return cls(_ptr=m)

    def save(self, path):
        if lib().bast_map_save(self._m, os.fsencode(path)) != 0:
            raise BastError(_err())

    @property
    def _c(self):
        return self._m.contents

    width = property(lambda self: self._c.width)
    height = property(lambda self: self._c.height)

    def _hdr(field, encoded=False):  # noqa: N805 (descriptor factory)
        def get(self):
            v = getattr(self._c, field)
            return v.decode() if encoded else v

        def put(self, v):
            if encoded:
                v = v.encode()
                if len(v) > 32:
                    raise BastError("map name %r longer than 32" % v)
            setattr(self._c, field, v)
        return property(get, put)

    name = _hdr("name", encoded=True)
    sky_tex = _hdr("sky_tex")
    music = _hdr("music")
    par_seconds = _hdr("par_seconds")
    del _hdr

    @property
    def textures(self):
        """Texture name table; entry 0 is '' (none / sky)."""
        c = self._c
        return [c.textures[i].value.decode() for i in range(c.texture_count)]

    def texture_index(self, name):
        """Index of `name` in the table, appending it if new ('' -> 0)."""
        i = lib().bast_map_texture_index(self._m, name.encode())
        if i < 0:
            raise BastError(_err())
        return i

    def _cell(self, x, y):
        p = lib().bast_map_cell_at(self._m, x, y)
        if not p:
            raise BastError("cell (%d, %d) outside %dx%d map" % (x, y, self.width, self.height))
        return p.contents

    def get_cell(self, x, y):
        c = self._cell(x, y)
        return {f: getattr(c, f) for f in CELL_FIELDS}

    def set_cell(self, x, y, **fields):
        c = self._cell(x, y)
        for f, v in fields.items():
            if f not in CELL_FIELDS:
                raise BastError("unknown cell field %r" % f)
            setattr(c, f, v)

    @property
    def entity_count(self):
        return self._c.entity_count

    def _ent(self, i):
        if not 0 <= i < self._c.entity_count:
            raise BastError("no entity %d" % i)
        return self._c.entities[i]

    def get_entity(self, i):
        e = self._ent(i)
        d = {f: getattr(e, f) for f in ENTITY_FIELDS}
        d["name"] = d["name"].decode()
        return d

    def set_entity(self, i, **fields):
        e = self._ent(i)
        for f, v in fields.items():
            if f not in ENTITY_FIELDS:
                raise BastError("unknown entity field %r" % f)
            if f == "type":
                v = entity_type(v)
            if f == "name":
                v = v.encode()
                if len(v) > 16:
                    raise BastError("entity name %r longer than 16" % v)
            setattr(e, f, v)

    @property
    def entities(self):
        return [self.get_entity(i) for i in range(self.entity_count)]

    def add_entity(self, type, x=0, y=0, angle=0, flags=0x7, tag=0, param=0, name=""):
        """Append an entity (type is a name or number), return its index.
        Flags default to all three skills."""
        i = lib().bast_map_add_entity(self._m)
        if i < 0:
            raise BastError(_err())
        self.set_entity(i, type=type, x=x, y=y, angle=angle, flags=flags, tag=tag,
                        param=param, name=name)
        return i

    def remove_entity(self, i):
        if lib().bast_map_remove_entity(self._m, i) != 0:
            raise BastError(_err())

    def find_entity(self, name):
        """Index of the entity called `name`, or None."""
        for i in range(self.entity_count):
            if self._c.entities[i].name.decode() == name:
                return i
        return None
