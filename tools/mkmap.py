#!/usr/bin/env python3
"""mkmap.py: build a .vmap from an ASCII layout (used until the editor exists).

    python3 tools/mkmap.py maps/src/<name>.txt maps/<name>.vmap

Everything is written through libbast (tools/bast.py); this script only
parses text. Spec of the binary format: docs/design/tech.md, "Map".

Input format (line based; a line whose first non-blank character is `#` is a
comment, except inside the layout; there are no trailing comments, so `#` can
be a legend character):

    name <map name, up to 32 chars, rest of the line>
    sky <texture>                   sky texture name (default: none)
    music <0..255>                  music track number (default 0)
    par <seconds>                   par time (default 0)

    legend <char> [key=value ...]   one per layout character
        wall=<tex>      wall texture (default none)
        floor=<tex>     floor texture (default none)
        ceil=<tex>      ceiling texture, `sky` (or omitted) = sky
        dark=<0..31>    darkness (default 0)
        dark_alt=<0..31> darkness after a LIGHTS tag (default: same as dark)
        flags=<A|B|..>  cell flag names (SOLID, DOOR, PUSHWALL, SWITCH, EXIT,
                        SECRET, DAMAGE, ONEWAY, START_OPEN, BLOCK_MONSTER,
                        BLOCK_ALL, TRIGGER), or a number
        tag=<0..255>  param=<0..255>
        Door fields, OR-ed into param (tech.md Map, door param):
        key=none|green|blue|violet      bits 0-1
        mode=auto|stay|switch|timed     bits 2-3
        side=north|east|south|west      bits 4-5, the ONEWAY opening side
      A texture value of `-` means none.

    layout                          starts the grid; one char per cell
    <row>                           first row is the NORTH edge, all rows
    ...                             the same width (1..64), up to 64 rows
    end                             ends the grid

    entity <TYPE> <tx> <ty> <angle_deg> [name] [flags=A|B] [tag=n] [param=n]
        TYPE is a libbast entity type name (PLAYER_START, WAYPOINT, ...).
        (tx, ty) is a tile; the entity sits at the tile centre. ty counts
        from the SOUTH (the last layout row is ty = 0), matching the map's
        y-grows-north coordinates and demo `goto <tx> <ty>`.
        angle_deg: integer degrees, 0 = east, counter-clockwise.
        flags default to SKILL1|SKILL2|SKILL3 (SKILL*, AMBUSH, SECRET_ONLY,
        PICKUP_TRIGGER). name (up to 16 chars) is stored in the name table.

The texture table lists textures in order of first appearance in the file
(sky line, then legend lines: wall, floor, ceil); entry 0 is none/sky. The
output is deterministic, so rebuilding gives identical bytes.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402

FIX_ONE = 65536

# Door param fields (tech.md Map): name -> (shift, value names).
DOOR_FIELDS = {
    "key": (0, ("none", "green", "blue", "violet")),
    "mode": (2, ("auto", "stay", "switch", "timed")),
    "side": (4, ("north", "east", "south", "west")),
}


class MapSyntaxError(Exception):
    pass


def _int(v, lo, hi, what, ln):
    try:
        n = int(v, 0)
    except ValueError:
        raise MapSyntaxError("line %d: %s %r is not an integer" % (ln, what, v))
    if not lo <= n <= hi:
        raise MapSyntaxError("line %d: %s %d outside %d..%d" % (ln, what, n, lo, hi))
    return n


def _flags(v, table, ln):
    if v[:1].isdigit():
        return _int(v, 0, 0xFFFF, "flags", ln)
    out = 0
    for name in v.split("|"):
        if name not in table:
            raise MapSyntaxError("line %d: unknown flag %r (known: %s)"
                                 % (ln, name, ", ".join(table)))
        out |= table[name]
    return out


def _strip_comment(line):
    s = line.rstrip("\n")
    return "" if s.lstrip().startswith("#") else s


def parse(text):
    """Parse the text format into a plain dict (no libbast needed except for
    flag and type names)."""
    cflags, eflags = bast.cell_flags(), bast.entity_flags()
    out = {"name": "", "sky": "", "music": 0, "par": 0, "legend": {},
           "rows": None, "entities": [], "texorder": []}

    def tex(name):
        if name not in ("", "-") and name not in out["texorder"]:
            if len(name.encode()) > 16:
                raise MapSyntaxError("line %d: texture name %r longer than 16" % (ln, name))
            out["texorder"].append(name)
        return "" if name == "-" else name

    lines = text.splitlines()
    ln = 0
    while ln < len(lines):
        raw = lines[ln]
        ln += 1
        s = _strip_comment(raw).strip()
        if not s:
            continue
        word, _, rest = s.partition(" ")
        rest = rest.strip()
        if word == "name":
            if len(rest.encode()) > 32:
                raise MapSyntaxError("line %d: map name longer than 32" % ln)
            out["name"] = rest
        elif word == "sky":
            out["sky"] = tex(rest)
        elif word == "music":
            out["music"] = _int(rest, 0, 255, "music", ln)
        elif word == "par":
            out["par"] = _int(rest, 0, 65535, "par", ln)
        elif word == "legend":
            toks = rest.split()
            if not toks or len(toks[0]) != 1:
                raise MapSyntaxError("line %d: legend needs a single character" % ln)
            ch = toks[0]
            if ch in out["legend"]:
                raise MapSyntaxError("line %d: legend %r defined twice" % (ln, ch))
            cell = {"wall": "", "floor": "", "ceil": "", "dark": 0, "dark_alt": None,
                    "flags": 0, "tag": 0, "param": 0}
            door_bits = 0
            for t in toks[1:]:
                k, eq, v = t.partition("=")
                if eq and k in DOOR_FIELDS:
                    shift, names = DOOR_FIELDS[k]
                    if v not in names:
                        raise MapSyntaxError("line %d: %s=%r (known: %s)"
                                             % (ln, k, v, ", ".join(names)))
                    door_bits |= names.index(v) << shift
                    continue
                if not eq or k not in cell:
                    raise MapSyntaxError("line %d: bad legend field %r" % (ln, t))
                if k in ("wall", "floor"):
                    cell[k] = tex(v)
                elif k == "ceil":
                    cell[k] = "" if v == "sky" else tex(v)
                elif k in ("dark", "dark_alt"):
                    cell[k] = _int(v, 0, 31, k, ln)
                elif k == "flags":
                    cell[k] = _flags(v, cflags, ln)
                else:
                    cell[k] = _int(v, 0, 255, k, ln)
            cell["param"] |= door_bits
            if cell["dark_alt"] is None:
                cell["dark_alt"] = cell["dark"]
            out["legend"][ch] = cell
        elif word == "layout":
            if out["rows"] is not None:
                raise MapSyntaxError("line %d: second layout" % ln)
            rows = []
            while True:
                if ln >= len(lines):
                    raise MapSyntaxError("layout without `end`")
                r = lines[ln].rstrip("\n").rstrip()
                ln += 1
                if r.strip() == "end":
                    break
                rows.append((ln, r))
            if not rows:
                raise MapSyntaxError("empty layout")
            w = len(rows[0][1])
            for rl, r in rows:
                if len(r) != w:
                    raise MapSyntaxError("line %d: row is %d wide, first row is %d"
                                         % (rl, len(r), w))
            if not (1 <= w <= 64 and len(rows) <= 64):
                raise MapSyntaxError("layout %dx%d outside 1..64" % (w, len(rows)))
            out["rows"] = rows
        elif word == "entity":
            toks = rest.split()
            if len(toks) < 4:
                raise MapSyntaxError("line %d: entity <type> <tx> <ty> <angle> [name]" % ln)
            try:
                etype = bast.entity_type(toks[0])
            except bast.BastError as e:
                raise MapSyntaxError("line %d: %s" % (ln, e))
            ent = {"type": etype, "tx": _int(toks[1], 0, 63, "tx", ln),
                   "ty": _int(toks[2], 0, 63, "ty", ln),
                   "angle": _int(toks[3], -36000, 36000, "angle", ln),
                   "name": "", "flags": 0x7, "tag": 0, "param": 0, "line": ln}
            for t in toks[4:]:
                k, eq, v = t.partition("=")
                if not eq:
                    if ent["name"]:
                        raise MapSyntaxError("line %d: two names" % ln)
                    if len(t.encode()) > 16:
                        raise MapSyntaxError("line %d: entity name longer than 16" % ln)
                    ent["name"] = t
                elif k == "flags":
                    ent["flags"] = _flags(v, eflags, ln)
                elif k in ("tag", "param"):
                    ent[k] = _int(v, 0, 255, k, ln)
                else:
                    raise MapSyntaxError("line %d: bad entity field %r" % (ln, t))
            out["entities"].append(ent)
        else:
            raise MapSyntaxError("line %d: unknown keyword %r" % (ln, word))
    if out["rows"] is None:
        raise MapSyntaxError("no layout")
    names = [e["name"] for e in out["entities"] if e["name"]]
    if len(names) != len(set(names)):
        raise MapSyntaxError("duplicate entity names")
    return out


def angle_units(deg):
    """Integer degrees -> angle_t (65536 per turn), rounded half up."""
    return ((deg * FIX_ONE * 2 + 360) // 720) % FIX_ONE


def build(text):
    p = parse(text)
    rows = p["rows"]
    w, h = len(rows[0][1]), len(rows)
    m = bast.Map(w, h)
    m.name = p["name"]
    m.music = p["music"]
    m.par_seconds = p["par"]
    for t in p["texorder"]:
        m.texture_index(t)
    m.sky_tex = m.texture_index(p["sky"])
    for r, (rl, row) in enumerate(rows):
        y = h - 1 - r  # first row is north
        for x, ch in enumerate(row):
            cell = p["legend"].get(ch)
            if cell is None:
                raise MapSyntaxError("line %d: %r at column %d has no legend" % (rl, ch, x))
            m.set_cell(x, y, wall=m.texture_index(cell["wall"]),
                       floor=m.texture_index(cell["floor"]),
                       ceil=m.texture_index(cell["ceil"]), dark=cell["dark"],
                       dark_alt=cell["dark_alt"], flags=cell["flags"], tag=cell["tag"],
                       param=cell["param"])
    for e in p["entities"]:
        if e["tx"] >= w or e["ty"] >= h:
            raise MapSyntaxError("line %d: entity tile (%d, %d) outside %dx%d"
                                 % (e["line"], e["tx"], e["ty"], w, h))
        m.add_entity(e["type"], x=e["tx"] * FIX_ONE + FIX_ONE // 2,
                     y=e["ty"] * FIX_ONE + FIX_ONE // 2, angle=angle_units(e["angle"]),
                     flags=e["flags"], tag=e["tag"], param=e["param"], name=e["name"])
    return m


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: mkmap.py <in.txt> <out.vmap>\n")
        return 2
    with open(argv[1], encoding="utf-8") as f:
        text = f.read()
    try:
        m = build(text)
        m.save(argv[2])
    except (MapSyntaxError, bast.BastError) as e:
        sys.stderr.write("mkmap: %s: %s\n" % (argv[1], e))
        return 1
    print("mkmap: %s -> %s (%dx%d, %d textures, %d entities)"
          % (argv[1], argv[2], m.width, m.height, len(m.textures), m.entity_count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
