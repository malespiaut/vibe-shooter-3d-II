"""Map checks run before save and launch (no wx). Each problem names the
cell it is about, so the view can list problems and select the cell on click.

Rules (issue #28, GAME.md 7):
- exactly one PLAYER_START entity;
- the map is closed: every border cell is SOLID;
- a DOOR cell sits between two SOLID cells, west+east or south+north."""
from . import entities as E
from .model import bast

PLAYER_START = E.BY_NAME["PLAYER_START"][0]


class Problem:
    def __init__(self, kind, cell, message):
        self.kind = kind        # "player_start" | "border" | "door"
        self.cell = cell        # (x, y) or None
        self.message = message

    def __str__(self):
        where = "(%d, %d) " % self.cell if self.cell else ""
        return where + self.message

    __repr__ = __str__


def validate(model):
    flags = bast.cell_flags()
    solid, door = flags["SOLID"], flags["DOOR"]
    w, h = model.width, model.height
    out = []

    starts = [e for e in model.entities if e["type"] == PLAYER_START]
    if not starts:
        out.append(Problem("player_start", None, "no PLAYER_START (need exactly one, found none)"))
    elif len(starts) > 1:
        for e in starts[1:]:
            out.append(Problem("player_start", (e["x"] >> 16, e["y"] >> 16),
                               "%d PLAYER_START entities (need exactly one)" % len(starts)))

    def is_solid(x, y):
        return 0 <= x < w and 0 <= y < h and bool(model.cell(x, y)["flags"] & solid)

    for y in range(h):
        for x in range(w):
            c = model.cell(x, y)
            border = x in (0, w - 1) or y in (0, h - 1)
            if border and not c["flags"] & solid:
                out.append(Problem("border", (x, y), "border cell not solid: map is open"))
            if c["flags"] & door and not (
                    (is_solid(x - 1, y) and is_solid(x + 1, y)) or
                    (is_solid(x, y - 1) and is_solid(x, y + 1))):
                out.append(Problem("door", (x, y),
                                   "door not between two solid cells (W+E or S+N)"))
    return out
