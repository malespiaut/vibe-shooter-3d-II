#!/usr/bin/env python3
"""ctest `editor_roundtrip`: the editor model opens maps/m1test.vmap and saves
it byte-identically; the presenter, driven headlessly (no wx), paints a
rectangle, undoes and redoes it, paints a stroke, erases, uses the eyedropper,
and the model is checked after each step. MVP 2: light strokes, cell
flags/tag/param, a door's key and mode, three named entities placed, rotated,
moved, edited and deleted, all undoable, saved and reloaded identically. Plain asserts. BAST_LIB overrides
the libbast.so path."""
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from editor import model as M  # noqa: E402
from editor import presenter as P  # noqa: E402
from editor.presenter import Presenter  # noqa: E402

SRC = os.path.join(ROOT, "maps", "m1test.vmap")


def read(path):
    with open(path, "rb") as f:
        return f.read()


def snapshot(m):
    return [m.cell(x, y) for y in range(m.height) for x in range(m.width)]


def test_roundtrip(tmp):
    m = M.MapModel.open(SRC)
    assert (m.width, m.height) == (24, 24) and not m.dirty
    out = os.path.join(tmp, "rt.vmap")
    m.save(out)
    assert read(out) == read(SRC), "save after open is not byte-identical"
    assert m.path == out and not m.dirty


def test_roundtrip_decor(tmp):
    """maps/decortest.vmap holds every decoration type (#131): the editor
    names each one and saves it back byte-identical."""
    src = os.path.join(ROOT, "maps", "decortest.vmap")
    m = M.MapModel.open(src)
    out = os.path.join(tmp, "rt_decor.vmap")
    m.save(out)
    assert read(out) == read(src), "decortest: save after open is not byte-identical"
    from editor import entities as E
    for n in range(96, 106):
        assert E.category_of(n) == "decoration" and E.BY_NUMBER[n][1], n


def test_presenter(tmp):
    p = Presenter(M.MapModel.open(SRC))
    m = p.model
    solid = M.solid_flag()
    orig = snapshot(m)
    tex = m.textures
    t02 = tex.index("test02")

    # Rectangle of walls dragged from (10, 2) to (12, 4) inside the SE hall.
    p.set_layer("wall")
    p.set_texture(t02)
    p.set_tool("rect")
    p.on_press(12, 4)
    p.on_drag(11, 3)
    assert p.selection == (12, 4, 11, 3)
    p.on_release(10, 2)
    assert p.selection is None
    for y in range(2, 5):
        for x in range(10, 13):
            c = m.cell(x, y)
            assert c["wall"] == t02 and c["flags"] & solid, (x, y, c)
    assert m.cell(9, 2) == orig[2 * 24 + 9]
    assert m.dirty and m.can_undo()
    painted = snapshot(m)

    p.undo()
    assert snapshot(m) == orig, "undo did not restore the map"
    assert m.can_redo()
    p.redo()
    assert snapshot(m) == painted
    p.undo()

    # Saving after undo is byte-identical to the original file again.
    out = os.path.join(tmp, "undo.vmap")
    p.save(out)
    assert read(out) == read(SRC)

    # Floor stroke: one press/drag/release is one undo step.
    t06 = tex.index("test06")
    p.set_layer("floor")
    p.set_texture(t06)
    p.set_tool("paint")
    p.on_press(2, 2)
    p.on_drag(3, 2)
    p.on_drag(3, 2)
    p.on_drag(4, 2)
    p.on_release(4, 2)
    assert [m.cell(x, 2)["floor"] for x in (2, 3, 4)] == [t06] * 3
    p.undo()
    assert snapshot(m) == orig

    # Undo/redo are ignored while a stroke is held: the stroke stays one step.
    p.on_press(2, 2)
    p.undo()
    p.redo()
    p.on_drag(3, 2)
    p.on_release(3, 2)
    assert [m.cell(x, 2)["floor"] for x in (2, 3)] == [t06] * 2
    p.undo()
    assert snapshot(m) == orig

    # Ceiling erase = sky (0); eyedropper picks the layer value.
    p.set_layer("ceil")
    p.set_tool("erase")
    p.on_press(2, 2)
    p.on_release(2, 2)
    assert m.cell(2, 2)["ceil"] == 0
    p.set_tool("eyedropper")
    p.on_press(3, 3)
    assert p.texture == m.cell(3, 3)["ceil"] != 0

    # Erasing a wall clears SOLID too.
    p.set_layer("wall")
    p.set_tool("erase")
    p.on_press(0, 5)
    p.on_release(0, 5)
    c = m.cell(0, 5)
    assert c["wall"] == 0 and not c["flags"] & solid, c

    # Painting outside the map is ignored, no-op commands are not recorded.
    n = len(m._undo)
    p.on_press(99, 99)
    p.on_release(99, 99)
    p.set_tool("paint")
    p.set_texture(m.cell(0, 0)["wall"])
    p.on_press(0, 0)
    p.on_release(0, 0)
    assert len(m._undo) == n

    # Texture table: adding a name appends once and selects it.
    i = p.add_texture("brick01")
    assert m.textures[i] == "brick01" and p.texture == i
    assert p.add_texture("brick01") == i


def test_new_map(tmp):
    p = Presenter()
    p.new_map(64, 64, "Fresh")
    m = p.model
    assert (m.width, m.height, m.name) == (64, 64, "Fresh")
    try:
        p.new_map(65, 10)
        raise AssertionError("65 wide accepted")
    except M.ModelError:
        pass
    t = p.add_texture("wall_a")
    p.set_layer("wall")
    p.fill_rect(0, 0, 63, 0)
    out = os.path.join(tmp, "new.vmap")
    p.save(out)
    back = M.MapModel.open(out)
    assert back.textures == ["", "wall_a"] and back.cell(63, 0)["wall"] == t
    assert back.cell(0, 1)["wall"] == 0


def ents(m):
    return [m.entity(i) for i in range(m.entity_count)]


def test_mvp2(tmp):
    """Light, flags, door param and entities through the presenter; every
    edit undoes; the result saves and reloads with identical values."""
    p = Presenter(M.MapModel.open(SRC))
    m = p.model
    cf = M.bast.cell_flags()
    orig_cells, orig_ents = snapshot(m), ents(m)

    # Light: a dark stroke and a dark_alt stroke, each one undo step.
    p.set_tool("light")
    p.set_light_field("dark")
    p.set_light_value(20)
    p.on_press(2, 2)
    p.on_drag(3, 2)
    p.on_drag(4, 2)
    p.on_release(4, 2)
    assert [m.cell(x, 2)["dark"] for x in (2, 3, 4)] == [20] * 3
    p.set_light_field("dark_alt")
    p.set_light_value(31)
    p.on_press(2, 3)
    p.on_release(2, 3)
    assert m.cell(2, 3)["dark_alt"] == 31
    try:
        p.set_light_value(32)
        raise AssertionError("dark 32 accepted")
    except ValueError:
        pass
    p.set_view_mode("light")
    n = len(m._undo)
    p.undo()
    assert m.cell(2, 3)["dark_alt"] == orig_cells[3 * 24 + 2]["dark_alt"]
    p.redo()
    assert len(m._undo) == n

    # Flags tool: select a cell, toggle flags, tag, param, door key and mode.
    p.set_tool("flags")
    p.on_press(5, 5)
    p.on_release(5, 5)
    assert p.cell_sel == (5, 5)
    p.set_cell_flag("DOOR", True)
    p.set_cell_flag("ONEWAY", True)
    assert p.is_door()
    p.set_cell_values(tag=7)
    p.set_door(key=2)          # blue
    p.set_door(mode=3)         # timed
    c = m.cell(5, 5)
    assert c["flags"] & cf["DOOR"] and c["flags"] & cf["ONEWAY"]
    assert c["tag"] == 7 and c["param"] == 2 | (3 << 2), c
    assert P.door_key(c["param"]) == 2 and P.door_mode(c["param"]) == 3
    p.set_cell_flag("ONEWAY", False)
    assert not m.cell(5, 5)["flags"] & cf["ONEWAY"]
    p.undo()                    # ONEWAY back on
    assert m.cell(5, 5)["flags"] & cf["ONEWAY"]
    p.undo()                    # mode back to 0
    assert P.door_mode(m.cell(5, 5)["param"]) == 0
    p.redo()
    p.redo()
    p.select_cell(6, 5)
    p.set_cell_flag("SECRET", True)
    p.set_cell_flag("TRIGGER", True)
    p.set_cell_values(tag=9, param=4)
    p.select_cell(7, 5)
    p.set_cell_flag("DAMAGE", True)
    p.set_cell_values(param=10)

    # Entities: place three (with names), rotate, set properties, move, delete.
    p.set_tool("entity")
    p.set_entity_type("WAYPOINT")
    base = m.entity_count
    p.on_press(8, 8)
    p.on_drag(9, 8)
    p.on_release(9, 9)          # place + drag is one step
    a = p.entity_sel
    assert a == base and m.entity(a)["x"] == (9 << 16) | 0x8000
    assert m.entity(a)["y"] == (9 << 16) | 0x8000
    p.set_entity_props(name="altar")
    p.undo()
    assert m.entity(a)["name"] == ""
    p.undo()
    assert m.entity_count == base, "place+drag did not undo as one step"
    p.redo()
    p.redo()
    assert m.entity(a)["name"] == "altar"

    p.set_entity_type("MUTT_TROOPER")
    p.on_press(4, 4)
    p.on_release(4, 4)
    b = p.entity_sel
    p.rotate_entity(3)          # north-west
    p.set_entity_flag("SKILL1", False)
    p.set_entity_flag("AMBUSH", True)
    p.set_entity_props(tag=7, name="gate_guard")
    e = m.entity(b)
    assert e["type"] == 16 and e["angle"] == 3 * 8192 and P.entity_dir(e["angle"]) == 3
    assert e["flags"] == 0x2 | 0x4 | 0x8 and e["tag"] == 7, e

    c3 = p.place_entity(6, 6, "KILL_TRIGGER", tag=7, param=9)
    p.set_entity_props(name="guards_dead")
    p.set_entity_flag("SECRET_ONLY", True)
    # Duplicate names are refused (demo goto @name needs one target).
    try:
        p.set_entity_props(name="altar")
        raise AssertionError("duplicate name accepted")
    except M.ModelError:
        pass

    # Clicking an existing entity selects it and drags it, no new entity.
    n = m.entity_count
    p.on_press(4, 4)
    assert p.entity_sel == b and m.entity_count == n
    p.on_drag(4, 3)
    p.on_release(4, 3)
    assert m.entity(b)["y"] == (3 << 16) | 0x8000
    p.undo()
    assert m.entity(b)["y"] == (4 << 16) | 0x8000

    # Delete the first original entity and undo: order and values restored.
    before = ents(m)
    p.select_entity(0)
    p.delete_entity()
    assert m.entity_count == n - 1 and p.entity_sel is None
    p.undo()
    assert ents(m) == before
    p.redo()
    p.undo()

    want_cells, want_ents = snapshot(m), ents(m)
    assert want_ents[c3]["name"] == "guards_dead" and want_ents[c3]["flags"] & 0x10

    out = os.path.join(tmp, "mvp2.vmap")
    p.save(out)
    back = M.MapModel.open(out)
    assert snapshot(back) == want_cells, "cells differ after reload"
    assert ents(back) == want_ents, "entities differ after reload"
    assert back.cell(5, 5)["param"] == 2 | (3 << 2) and back.cell(2, 2)["dark"] == 20
    named = {e["name"] for e in ents(back) if e["name"]}
    assert {"altar", "gate_guard", "guards_dead"} <= named, named

    # Undo everything: back to the original map, byte-identical on save.
    while m.can_undo():
        p.undo()
    assert snapshot(m) == orig_cells and ents(m) == orig_ents
    out2 = os.path.join(tmp, "mvp2_undone.vmap")
    p.save(out2)
    assert read(out2) == read(SRC)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        test_roundtrip(tmp)
        test_roundtrip_decor(tmp)
        test_presenter(tmp)
        test_new_map(tmp)
        test_mvp2(tmp)
    print("editor_roundtrip: ok")


if __name__ == "__main__":
    main()
