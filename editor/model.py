"""Editor model: a .vmap held by libbast through tools/bast.py, plus an undo
stack of cell edits. No format code here: load/save/cell access all go
through bast.Map, the single implementation (docs/design/tech.md)."""
import os
import sys

_TOOLS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools")
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)
import bast  # noqa: E402

MAX_SIZE = 64
LAYERS = ("wall", "floor", "ceil")
SOLID = None  # filled lazily from libbast's flag table


def solid_flag():
    global SOLID
    if SOLID is None:
        SOLID = bast.cell_flags()["SOLID"]
    return SOLID


class ModelError(Exception):
    pass


class MapModel:
    """One open map. Cell edits are grouped into commands (a paint stroke or a
    rectangle fill is one command) that undo/redo as a unit."""

    def __init__(self, bmap, path=None):
        self.map = bmap
        self.path = path
        self.dirty = False
        self._undo = []
        self._redo = []
        self._pending = None  # {(x, y): before} while a command is open
        self._pending_after = None
        self.listeners = []

    # construction / files -------------------------------------------------
    @classmethod
    def new(cls, width, height, name="Untitled"):
        if not (1 <= width <= MAX_SIZE and 1 <= height <= MAX_SIZE):
            raise ModelError("map size %dx%d outside 1..%d" % (width, height, MAX_SIZE))
        m = bast.Map(width, height)
        m.name = name
        return cls(m)

    @classmethod
    def open(cls, path):
        try:
            return cls(bast.Map.load(path), path)
        except bast.BastError as e:
            raise ModelError(str(e))

    def save(self, path=None):
        path = path or self.path
        if not path:
            raise ModelError("no file name")
        try:
            self.map.save(path)
        except bast.BastError as e:
            raise ModelError(str(e))
        self.path = path
        self.dirty = False
        self._notify("saved")

    # queries ---------------------------------------------------------------
    width = property(lambda self: self.map.width)
    height = property(lambda self: self.map.height)
    name = property(lambda self: self.map.name)

    @property
    def textures(self):
        return self.map.textures

    def inside(self, x, y):
        return 0 <= x < self.width and 0 <= y < self.height

    def cell(self, x, y):
        return self.map.get_cell(x, y)

    def texture_name(self, index):
        t = self.textures
        return t[index] if 0 <= index < len(t) else "?%d" % index

    # texture table ---------------------------------------------------------
    def add_texture(self, name):
        """Append a texture name (or find it); returns its index. Not undoable:
        the table only grows, and an unused entry is harmless."""
        name = name.strip()
        if not name:
            raise ModelError("empty texture name")
        if len(name.encode()) > 16:
            raise ModelError("texture name %r longer than 16 bytes" % name)
        before = len(self.textures)
        try:
            i = self.map.texture_index(name)
        except bast.BastError as e:
            raise ModelError(str(e))
        if len(self.textures) != before:
            self.dirty = True
            self._notify("textures")
        return i

    # editing ---------------------------------------------------------------
    def begin(self):
        if self._pending is not None:
            raise ModelError("command already open")
        self._pending = {}

    def set_cell(self, x, y, **fields):
        """Change one cell inside an open command (opens a one-shot command if
        none is open)."""
        oneshot = self._pending is None
        if oneshot:
            self.begin()
        if not self.inside(x, y):
            raise ModelError("cell (%d, %d) outside map" % (x, y))
        if (x, y) not in self._pending:
            self._pending[(x, y)] = self.cell(x, y)
        self.map.set_cell(x, y, **fields)
        if oneshot:
            self.end()

    def end(self):
        """Close the open command; records it if anything actually changed."""
        before, self._pending = self._pending, None
        if before is None:
            raise ModelError("no command open")
        cmd = []
        for (x, y), b in before.items():
            a = self.cell(x, y)
            if a != b:
                cmd.append((x, y, b, a))
        if cmd:
            self._undo.append(cmd)
            self._redo.clear()
            self.dirty = True
            self._notify("cells")
        return len(cmd)

    def can_undo(self):
        return bool(self._undo)

    def can_redo(self):
        return bool(self._redo)

    def undo(self):
        if not self._undo:
            return False
        cmd = self._undo.pop()
        for x, y, b, _a in cmd:
            self.map.set_cell(x, y, **b)
        self._redo.append(cmd)
        self.dirty = True
        self._notify("cells")
        return True

    def redo(self):
        if not self._redo:
            return False
        cmd = self._redo.pop()
        for x, y, _b, a in cmd:
            self.map.set_cell(x, y, **a)
        self._undo.append(cmd)
        self.dirty = True
        self._notify("cells")
        return True

    def _notify(self, what):
        for fn in list(self.listeners):
            fn(what)
