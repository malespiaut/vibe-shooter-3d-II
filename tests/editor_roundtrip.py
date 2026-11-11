#!/usr/bin/env python3
"""ctest `editor_roundtrip`: the editor model opens maps/m1test.vmap and saves
it byte-identically; the presenter, driven headlessly (no wx), paints a
rectangle, undoes and redoes it, paints a stroke, erases, uses the eyedropper,
and the model is checked after each step. Plain asserts. BAST_LIB overrides
the libbast.so path."""
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from editor import model as M  # noqa: E402
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


def main():
    with tempfile.TemporaryDirectory() as tmp:
        test_roundtrip(tmp)
        test_presenter(tmp)
        test_new_map(tmp)
    print("editor_roundtrip: ok")


if __name__ == "__main__":
    main()
