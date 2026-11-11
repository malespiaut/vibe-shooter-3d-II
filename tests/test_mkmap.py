#!/usr/bin/env python3
"""ctest `mkmap_py`: tools/mkmap.py on maps/src/m1test.txt, loaded back through
bast.Map; saving again is byte-identical; the committed maps/m1test.vmap is
up to date. Plain asserts. BAST_LIB overrides the libbast.so path.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import mkmap  # noqa: E402

SRC = os.path.join(ROOT, "maps", "src", "m1test.txt")
COMMITTED = os.path.join(ROOT, "maps", "m1test.vmap")


def read(path):
    with open(path, "rb") as f:
        return f.read()


def test_m1test(tmp):
    out = os.path.join(tmp, "m1test.vmap")
    p = subprocess.run([sys.executable, os.path.join(TOOLS, "mkmap.py"), SRC, out],
                       capture_output=True, text=True)
    assert p.returncode == 0, p.stdout + p.stderr
    m = bast.Map.load(out)
    assert (m.width, m.height) == (24, 24)
    assert m.name == "M1 Test" and m.par_seconds == 60 and m.music == 0
    tex = m.textures
    assert tex[0] == "" and tex[m.sky_tex] == "testsky"
    flags = bast.cell_flags()
    # Corner (0, 0) is solid wall test01.
    c = m.get_cell(0, 0)
    assert c["flags"] == flags["SOLID"] and tex[c["wall"]] == "test01", c
    # (2, 2): start room floor, dark 0.
    c = m.get_cell(2, 2)
    assert c == {"wall": 0, "floor": tex.index("test04"), "ceil": tex.index("test05"),
                 "dark": 0, "dark_alt": 0, "flags": 0, "tag": 0, "param": 0}, c
    # (1, 22): NW dark room, dark 16, lights-on 0.
    c = m.get_cell(1, 22)
    assert c["dark"] == 16 and c["dark_alt"] == 0 and tex[c["floor"]] == "test06", c
    # (10, 20): west corridor, dark 8.
    assert m.get_cell(10, 20)["dark"] == 8
    # (17, 18): courtyard sky cell; (16, 21) a pillar under open sky.
    assert m.get_cell(17, 18)["ceil"] == 0 and m.get_cell(17, 18)["flags"] == 0
    c = m.get_cell(16, 21)
    assert c["flags"] == flags["SOLID"] and tex[c["wall"]] == "test03", c
    # Darkness values 0, 8, 16 all occur; a sky area exists.
    cells = [m.get_cell(x, y) for y in range(24) for x in range(24)]
    floors = [c for c in cells if not c["flags"] & flags["SOLID"]]
    assert {c["dark"] for c in floors} == {0, 8, 16}
    assert sum(1 for c in floors if c["ceil"] == 0) >= 20
    # Entities.
    assert m.entity_count == 3
    ents = m.entities
    start = ents[0]
    assert start["type"] == bast.entity_type("PLAYER_START")
    assert (start["x"], start["y"], start["angle"]) == (2 * 65536 + 32768, 2 * 65536 + 32768,
                                                        16384)
    wps = [e for e in ents if e["type"] == bast.entity_type("WAYPOINT")]
    assert sorted(e["name"] for e in wps) == ["courtyard", "hall"]
    hall = ents[m.find_entity("hall")]
    assert (hall["x"] >> 16, hall["y"] >> 16) == (16, 6)
    for e in ents:  # nobody stands in a wall
        assert not m.get_cell(e["x"] >> 16, e["y"] >> 16)["flags"] & flags["SOLID"]
    # Saving again is byte-identical, and the committed map is up to date.
    again = os.path.join(tmp, "again.vmap")
    m.save(again)
    assert read(again) == read(out)
    assert read(out) == read(COMMITTED), "maps/m1test.vmap is stale: rerun mkmap.py"


def test_doortest(tmp):
    out = os.path.join(tmp, "doortest.vmap")
    m = mkmap.build(open(os.path.join(ROOT, "maps", "src", "doortest.txt")).read())
    m.save(out)
    flags = bast.cell_flags()
    door = flags["DOOR"]
    # Door param: key bits 0-1, mode bits 2-3, ONEWAY side bits 4-5.
    want = {2: (door, 0), 4: (door, 1), 6: (door | flags["ONEWAY"], 2 << 4),
            8: (door, 1 << 2), 10: (door | flags["START_OPEN"], 0)}
    for x, (f, param) in want.items():
        c = m.get_cell(x, 4)
        assert (c["flags"], c["param"]) == (f, param), (x, c)
    assert read(out) == read(os.path.join(ROOT, "maps", "doortest.vmap")), \
        "maps/doortest.vmap is stale: rerun mkmap.py"


def test_binding(tmp):
    m = bast.Map(3, 2)
    m.name = "tiny"
    i = m.texture_index("brick")
    assert i == 1 and m.texture_index("brick") == 1 and m.textures == ["", "brick"]
    m.set_cell(2, 1, wall=i, flags=bast.cell_flags()["DOOR"], dark=31, tag=4, param=0x05)
    k = m.add_entity("LIGHTS", x=-5, y=7, tag=9)
    assert bast.entity_type_name(m.get_entity(k)["type"]) == "LIGHTS"
    assert m.get_entity(k)["tag"] == 9 and m.get_entity(k)["flags"] == 7
    m.add_entity(bast.entity_type("WAYPOINT"), name="x" * 16)
    path = os.path.join(tmp, "tiny.vmap")
    m.save(path)
    r = bast.Map.load(path)
    assert r.get_cell(2, 1) == m.get_cell(2, 1) and r.entities == m.entities
    assert r.entities[0]["x"] == -5 and r.entities[1]["name"] == "x" * 16
    r.remove_entity(0)
    assert r.entity_count == 1
    for bad in (lambda: m.get_cell(3, 0), lambda: bast.entity_type("NOPE"),
                lambda: setattr(m, "name", "n" * 33),
                lambda: m.set_entity(0, name="y" * 17)):
        try:
            bad()
        except bast.BastError:
            pass
        else:
            raise AssertionError("no error")
    m.set_cell(0, 0, dark=32)
    try:
        m.save(path)
    except bast.BastError as e:
        assert "dark" in str(e)
    else:
        raise AssertionError("saved dark=32")


def test_errors():
    good = "legend . floor=a\nlayout\n..\n..\nend\n"
    assert mkmap.build(good).width == 2
    for text, msg in [
        ("legend . floor=a\nlayout\n..\n.\nend\n", "wide"),
        ("legend . floor=a\nlayout\n.x\nend\n", "no legend"),
        ("legend . dark=40\nlayout\n.\nend\n", "dark"),
        ("legend . flags=SOLDI\nlayout\n.\nend\n", "unknown flag"),
        (good + "entity ZOMBIE 0 0 0\n", "unknown entity type"),
        (good + "entity WAYPOINT 2 0 0\n", "outside"),
        (good + "entity WAYPOINT 0 0 0 a\nentity WAYPOINT 1 0 0 a\n", "duplicate"),
        ("legend . floor=a\nlayout\n.\n", "end"),
        ("legend . key=gold\nlayout\n.\nend\n", "key="),
    ]:
        try:
            mkmap.build(text)
        except mkmap.MapSyntaxError as e:
            assert msg in str(e), (msg, str(e))
        else:
            raise AssertionError("accepted: %r" % text)
    assert [mkmap.angle_units(d) for d in (0, 90, 180, 270, 360, -90, 45, 1)] == \
        [0, 16384, 32768, 49152, 0, 49152, 8192, 182]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        test_m1test(tmp)
        test_doortest(tmp)
        test_binding(tmp)
    test_errors()
    print("mkmap_py: ok")


if __name__ == "__main__":
    main()
