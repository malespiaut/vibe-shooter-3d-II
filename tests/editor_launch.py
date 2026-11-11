#!/usr/bin/env python3
"""ctest `editor_launch`: editor MVP 3 presenter logic, no wx.

- Validation (editor/validate.py) finds each problem injected into a copy of
  maps/m1test.vmap: no PLAYER_START, two of them, an open border cell, a door
  not between two solid cells; clicking a problem selects its cell.
- Launch (editor/launch.py, Presenter.launch / launch_headless) builds the
  right argv with the subprocess runner mocked, saves the map to a temp file
  without touching the model's path or dirty flag, reports a failed launch
  with its stderr, and refuses to launch an invalid map.
- Texture library (editor/textures.py) converts a TEX to RGB through the
  palette, nearest neighbour, and reads game.pak before the loose files.
Plain asserts. BAST_LIB overrides the libbast.so path."""
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from editor import launch as L  # noqa: E402
from editor import model as M  # noqa: E402
from editor import textures as T  # noqa: E402
from editor import validate as V  # noqa: E402
from editor.model import bast  # noqa: E402
from editor.presenter import Presenter  # noqa: E402

SRC = os.path.join(ROOT, "maps", "m1test.vmap")


def kinds(problems):
    return sorted(p.kind for p in problems)


def test_validation():
    p = Presenter(M.MapModel.open(SRC))
    m = p.model
    f = bast.cell_flags()
    assert p.validate() == [], [str(x) for x in p.problems]

    # No player start.
    start = [i for i, e in enumerate(m.entities) if e["type"] == 1]
    assert len(start) == 1
    saved = m.entity(start[0])
    m.remove_entity(start[0])
    probs = p.validate()
    assert kinds(probs) == ["player_start"], kinds(probs)
    assert probs[0].cell is None and "none" in probs[0].message
    m.undo()
    assert p.validate() == []

    # Two player starts: the problem points at the second one's cell.
    p.place_entity(3, 3, "PLAYER_START")
    probs = p.validate()
    assert kinds(probs) == ["player_start"] and probs[0].cell == (3, 3), probs
    assert "2" in probs[0].message
    m.undo()
    assert m.entity(start[0]) == saved

    # Open border cell (east edge, y = 5), and a corner.
    m.set_cell(23, 5, wall=0, flags=0)
    m.set_cell(0, 0, wall=0, flags=0)
    probs = p.validate()
    assert kinds(probs) == ["border", "border"], kinds(probs)
    assert sorted(x.cell for x in probs) == [(0, 0), (23, 5)]

    # Clicking a problem selects its cell (flags tool shows the selection).
    i = [x.cell for x in p.problems].index((23, 5))
    p.select_problem(i)
    assert p.cell_sel == (23, 5) and p.tool == "flags"
    m.undo()
    m.undo()
    assert p.validate() == []

    # Doors: (4, 16) is the corridor gap in the west wall row, solid W and E
    # -> valid. A door with solid N and S only is valid too: (5, 2) in the
    # start room once (5, 1) and (5, 3) are walls. (2, 2) in the open start
    # room -> invalid.
    gap = m.cell(4, 16)
    assert not gap["flags"] & f["SOLID"]
    m.set_cell(4, 16, flags=f["DOOR"])
    m.begin()
    for y in (1, 3):
        m.set_cell(5, y, wall=1, flags=f["SOLID"])
    m.set_cell(5, 2, flags=f["DOOR"])
    m.end()
    assert not m.cell(4, 2)["flags"] & f["SOLID"] and not m.cell(6, 2)["flags"] & f["SOLID"]
    assert p.validate() == [], [str(x) for x in p.problems]
    m.set_cell(2, 2, flags=f["DOOR"])
    probs = p.validate()
    assert kinds(probs) == ["door"] and probs[0].cell == (2, 2), probs
    # A door on the border (solid S+N neighbours) is a border problem only.
    m.set_cell(0, 5, flags=f["DOOR"])
    assert kinds(p.validate()) == ["border", "door"], kinds(p.problems)
    assert [x.cell for x in p.problems if x.kind == "border"] == [(0, 5)]
    for _ in range(4):
        m.undo()
    assert p.validate() == []
    p.select_problem(99)  # out of range: ignored
    print("validation: ok")


class FakeRunner:
    """Stands in for subprocess: records argv, plays back a result."""

    def __init__(self, rc=0, stderr=b"", frame=None):
        self.calls = []
        self.rc, self.stderr, self.frame = rc, stderr, frame

    def spawn(self, argv, errfile):
        self.calls.append(("spawn", list(argv)))
        assert os.path.exists(argv[argv.index("--map") + 1])
        errfile.write(self.stderr)
        errfile.flush()
        return FakeProc(self.rc)

    def run(self, argv, timeout):
        self.calls.append(("run", list(argv)))
        assert os.path.exists(argv[argv.index("--map") + 1])
        if self.frame and "--frames-out" in argv:
            d = argv[argv.index("--frames-out") + 1]
            with open(os.path.join(d, "frame_000070.png"), "wb") as f:
                f.write(self.frame)
        return self.rc, "STATS level=1 tic=70 hash=0\n", self.stderr.decode()


class FakeProc:
    def __init__(self, rc):
        self.rc = rc

    def poll(self):
        return self.rc


def test_argv():
    a = L.game_argv("/g/game", "/t/x.vmap")
    assert a == ["/g/game", "--map", "/t/x.vmap"], a
    a = L.game_argv("/g/game", "/t/x.vmap", skill=3)
    assert a == ["/g/game", "--map", "/t/x.vmap", "--skill", "3"], a
    a = L.headless_argv("/g/game", "/t/x.vmap", "/t/frames", skill=1)
    assert a == ["/g/game", "--headless", "--topdown", "--map", "/t/x.vmap",
                 "--max-tics", "70", "--frames-out", "/t/frames", "--frame-every", "70",
                 "--skill", "1"], a
    for bad in (0, 4):
        try:
            L.game_argv("g", "m", skill=bad)
            assert False, "skill %d accepted" % bad
        except ValueError:
            pass
    assert L.default_game_path() == os.environ.get(
        "VS3D_GAME", os.path.join(ROOT, "build", "game"))
    print("argv: ok")


def test_launch(tmp):
    p = Presenter(M.MapModel.open(SRC))
    m = p.model
    m.set_cell(2, 2, dark=5)  # unsaved edit: the launched map must carry it
    assert m.dirty
    r = FakeRunner()
    p.runner = r
    p.game_path = "/opt/vs3d/game"
    p.skill = 3
    p.launch()
    (kind, argv), = r.calls
    assert kind == "spawn"
    assert argv[:2] == ["/opt/vs3d/game", "--map"] and argv[3:] == ["--skill", "3"], argv
    tmpmap = argv[2]
    assert tmpmap.endswith(".vmap")
    assert bast.Map.load(tmpmap).get_cell(2, 2)["dark"] == 5
    assert m.path == SRC and m.dirty, "launch must not save over the open map"
    assert p.poll_launch() is None  # rc 0: nothing to report

    # Failure: non-zero exit, stderr reported.
    p.runner = FakeRunner(rc=1, stderr=b"game: x.vmap: bad magic\n")
    p.launch()
    err = p.poll_launch()
    assert err and "bad magic" in err and "exit 1" in err, err
    assert p.poll_launch() is None  # reported once

    # Spawn itself failing (no such executable).
    class Missing:
        def spawn(self, argv, errfile):
            raise FileNotFoundError(2, "No such file or directory", argv[0])
    p.runner = Missing()
    try:
        p.launch()
        assert False, "missing executable not reported"
    except L.LaunchError as e:
        assert "/opt/vs3d/game" in str(e), e

    # Headless test: argv, last frame path returned.
    png = b"\x89PNG fake"
    p.runner = FakeRunner(frame=png)
    p.skill = None
    frame = p.launch_headless()
    (kind, argv), = p.runner.calls
    assert kind == "run" and argv[1:3] == ["--headless", "--topdown"], argv
    assert "--skill" not in argv
    with open(frame, "rb") as f:
        assert f.read() == png
    p.runner = FakeRunner(rc=1, stderr=b"game: boom\n")
    try:
        p.launch_headless()
        assert False
    except L.LaunchError as e:
        assert "boom" in str(e)

    # Invalid map: launch refused before running anything.
    m.set_cell(0, 0, wall=0, flags=0)
    r = FakeRunner()
    p.runner = r
    for fn in (p.launch, p.launch_headless):
        try:
            fn()
            assert False, "invalid map launched"
        except L.LaunchError as e:
            assert "border" in str(e), e
    assert r.calls == [] and p.problems
    print("launch: ok")


def test_textures(tmp):
    pal = bast.load_palette()
    d = os.path.join(tmp, "textures")
    os.makedirs(d)
    # 2x3 TEX, column-major, one transparent pixel.
    t = bast.Tex(2, 3, bytes([1, 2, 3, 4, 5, bast.TRANSPARENT]))
    t.save(os.path.join(d, "mytex.tex"))
    lib = T.TextureLibrary([os.path.join(tmp, "nope"), d], pak=None)
    assert lib.find("mytex") is not None and lib.find("absent") is None
    assert lib.find("") is None
    assert "mytex" in lib.available()
    w, h, rgb = lib.rgb("mytex")
    assert (w, h) == (2, 3) and len(rgb) == 2 * 3 * 3
    col = pal.colors

    def px(x, y):
        o = (y * w + x) * 3
        return tuple(rgb[o:o + 3])
    # Row-major output, y down as in the image; (x, y) = pixels[x*h + y].
    assert px(0, 0) == col[1] and px(0, 2) == col[3] and px(1, 0) == col[4]
    assert px(1, 2) == T.TRANSPARENT_RGB
    # Nearest-neighbour scale: every output pixel is one source pixel.
    w2, h2, big = lib.rgb("mytex", 4, 6)
    assert (w2, h2) == (4, 6) and tuple(big[0:3]) == col[1]
    assert tuple(big[(5 * 4 + 3) * 3:(5 * 4 + 3) * 3 + 3]) == T.TRANSPARENT_RGB
    assert tuple(big[(1 * 4 + 1) * 3:(1 * 4 + 1) * 3 + 3]) == col[1]
    # game.pak: a texture in the pak loads to the same RGB as the loose
    # file, the pak wins over a loose file of the same name, available()
    # lists the pak's TEX entries (any of the three prefixes) but not other
    # files, and a missing pak falls back to the loose dirs.
    with open(os.path.join(d, "mytex.tex"), "rb") as f:
        mytex = f.read()
    other = bast.Tex(1, 1, bytes([7]))
    other.save(os.path.join(tmp, "other.tex"))
    with open(os.path.join(tmp, "other.tex"), "rb") as f:
        other_b = f.read()
    pak = os.path.join(tmp, "game.pak")
    bast.pak_write(pak, [("textures/mytex.tex", mytex), ("test/shadow.tex", other_b),
                         ("flat.tex", other_b), ("sfx/x.snd", b"abc"),
                         ("fx/deep/no.tex", other_b)])
    plib = T.TextureLibrary([os.path.join(tmp, "nope")], pak=pak)
    assert plib.rgb("mytex") == (w, h, rgb)
    assert plib.find("shadow").pixels == bytearray([7]) and plib.find("flat")
    assert plib.find("x") is None and plib.find("no") is None
    assert plib.available() == ["flat", "mytex", "shadow"], plib.available()
    loose_shadow = bast.Tex(1, 1, bytes([9]))
    loose_shadow.save(os.path.join(d, "shadow.tex"))
    both = T.TextureLibrary([d], pak=pak)
    assert both.find("shadow").pixels == bytearray([7])  # pak first
    assert both.available() == ["flat", "mytex", "shadow"]
    assert T.TextureLibrary([d], pak=os.path.join(tmp, "none.pak")).find("shadow").pixels \
        == bytearray([9])
    # Default search path finds the generated textures, if built.
    real = T.TextureLibrary()
    gen = os.path.join(ROOT, "assets", "gen", "textures")
    if os.path.isdir(gen):
        names = [n[:-4] for n in os.listdir(gen) if n.endswith(".tex")]
        assert names and all(real.find(n) for n in names)
    print("textures: ok")


def main():
    test_validation()
    test_argv()
    with tempfile.TemporaryDirectory() as tmp:
        test_launch(tmp)
        test_textures(tmp)
    print("editor_launch: ok")


if __name__ == "__main__":
    main()
