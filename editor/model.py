"""Editor model: a .vmap held by libbast through tools/bast.py, plus an undo
stack of commands (cell edits and entity add/set/remove). No format code here: load/save/cell access all go
through bast.Map, the single implementation (docs/design/tech.md)."""
import contextlib
import os
import sys

_TOOLS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools")
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)
import bast  # noqa: E402

MAX_SIZE = 64
MAX_DARK = 31
ENTNAME_LEN = 16
ENT_SKILL_ALL = 0x7
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
        self._ops = []  # entity operations of the open command
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

    # entities --------------------------------------------------------------
    @property
    def entity_count(self):
        return self.map.entity_count

    def entity(self, i):
        return self.map.get_entity(i)

    @property
    def entities(self):
        return self.map.entities

    def entities_at(self, x, y):
        """Indices of the entities standing in cell (x, y)."""
        return [i for i, e in enumerate(self.map.entities)
                if e["x"] >> 16 == x and e["y"] >> 16 == y]

    def _check_name(self, i, name):
        if len(name.encode()) > ENTNAME_LEN:
            raise ModelError("entity name %r longer than %d bytes" % (name, ENTNAME_LEN))
        if name:
            j = self.map.find_entity(name)
            if j is not None and j != i:
                raise ModelError("entity name %r already used by entity %d" % (name, j))

    def _check_entity(self, fields):
        if "type" in fields and not bast.entity_type_name(fields["type"]):
            raise ModelError("unknown entity type %r" % fields["type"])
        for f, lo, hi in (("tag", 0, 255), ("param", 0, 255), ("angle", 0, 65535),
                          ("flags", 0, 65535)):
            if f in fields and not lo <= fields[f] <= hi:
                raise ModelError("entity %s %r outside %d..%d" % (f, fields[f], lo, hi))
        for f in ("x", "y"):
            if f in fields:
                lim = (self.width if f == "x" else self.height) << 16
                if not 0 <= fields[f] < lim:
                    raise ModelError("entity %s outside the map" % f)

    def add_entity(self, type, x, y, **fields):
        """Append an entity (one undo step unless a command is open); returns
        its index. Flags default to all three skills."""
        data = dict(type=type, x=x, y=y, angle=0, flags=ENT_SKILL_ALL, tag=0, param=0,
                    name="")
        data.update(fields)
        self._check_entity(data)
        self._check_name(None, data["name"])
        with self._command():
            i = self.map.add_entity(**data)
            self._ops.append(("add", i, self.map.get_entity(i)))
        return i

    def set_entity(self, i, **fields):
        """Change fields of entity i (one undo step unless a command is open)."""
        if not 0 <= i < self.entity_count:
            raise ModelError("no entity %d" % i)
        self._check_entity(fields)
        if "name" in fields:
            self._check_name(i, fields["name"])
        with self._command():
            before = self.map.get_entity(i)
            self.map.set_entity(i, **fields)
            after = self.map.get_entity(i)
            if after != before:
                self._ops.append(("set", i, before, after))

    def remove_entity(self, i):
        if not 0 <= i < self.entity_count:
            raise ModelError("no entity %d" % i)
        with self._command():
            data = self.map.get_entity(i)
            self.map.remove_entity(i)
            self._ops.append(("del", i, data))

    def _insert_entity(self, i, data):
        """Put `data` back at index i, shifting later entities up."""
        last = self.map.add_entity(type=data["type"])
        for j in range(last, i, -1):
            self.map.set_entity(j, **self.map.get_entity(j - 1))
        self.map.set_entity(i, **data)

    def _apply_op(self, op, forward):
        kind, i = op[0], op[1]
        if kind == "cell":
            x, y = i
            self.map.set_cell(x, y, **(op[3] if forward else op[2]))
        elif kind == "set":
            self.map.set_entity(i, **(op[3] if forward else op[2]))
        elif (kind == "add") == forward:
            self._insert_entity(i, op[2])
        else:
            self.map.remove_entity(i)

    # editing ---------------------------------------------------------------
    def begin(self):
        if self._pending is not None:
            raise ModelError("command already open")
        self._pending = {}
        self._ops = []

    @contextlib.contextmanager
    def _command(self):
        oneshot = self._pending is None
        if oneshot:
            self.begin()
        try:
            yield
        finally:
            if oneshot:
                self.end()

    def set_cell(self, x, y, **fields):
        """Change one cell inside an open command (opens a one-shot command if
        none is open)."""
        if not self.inside(x, y):
            raise ModelError("cell (%d, %d) outside map" % (x, y))
        for f in ("dark", "dark_alt"):
            if f in fields and not 0 <= fields[f] <= MAX_DARK:
                raise ModelError("%s %r outside 0..%d" % (f, fields[f], MAX_DARK))
        for f in ("tag", "param", "wall", "floor", "ceil"):
            if f in fields and not 0 <= fields[f] <= 255:
                raise ModelError("%s %r outside 0..255" % (f, fields[f]))
        with self._command():
            if (x, y) not in self._pending:
                self._pending[(x, y)] = self.cell(x, y)
            self.map.set_cell(x, y, **fields)

    def end(self):
        """Close the open command; records it if anything actually changed.
        Returns the number of changed cells plus entity operations."""
        before, self._pending = self._pending, None
        ops, self._ops = self._ops, []
        if before is None:
            raise ModelError("no command open")
        cells = []
        for (x, y), b in before.items():
            a = self.cell(x, y)
            if a != b:
                cells.append(("cell", (x, y), b, a))
        cmd = cells + ops
        if cmd:
            self._undo.append(cmd)
            self._redo.clear()
            self.dirty = True
            self._notify("entities" if ops else "cells")
        return len(cmd)

    def can_undo(self):
        return bool(self._undo)

    def can_redo(self):
        return bool(self._redo)

    def undo(self):
        if not self._undo:
            return False
        cmd = self._undo.pop()
        for op in reversed(cmd):
            self._apply_op(op, False)
        self._redo.append(cmd)
        self.dirty = True
        self._notify("entities")
        return True

    def redo(self):
        if not self._redo:
            return False
        cmd = self._redo.pop()
        for op in cmd:
            self._apply_op(op, True)
        self._undo.append(cmd)
        self.dirty = True
        self._notify("entities")
        return True

    def _notify(self, what):
        for fn in list(self.listeners):
            fn(what)
