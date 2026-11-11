#!/usr/bin/env python3
"""ctest `pak_py`: tools/pak.py packs a temp directory twice (byte-identical
output), the archive reads back through libbast with sorted '/' names, the
output archive itself and other *.pak files are skipped, and over-long names
and duplicate entries are rejected. Plain asserts.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402


def run(*args):
    p = subprocess.run([sys.executable, os.path.join(TOOLS, "pak.py")] + list(args),
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def main():
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "gen")
        files = {
            "palette.bin": bytes(range(256)) * 3,
            "sfx/door_open.snd": b"SND1" + b"\x00" * 20,
            "sfx/a.snd": b"",
            "textures/z/deep.tex": b"\x00\x01\xff" * 100,
            "colormap.bin": os.urandom(8192),
        }
        for n, data in files.items():
            write(os.path.join(src, n), data)
        write(os.path.join(src, "old.pak"), b"VPAK junk")  # never packed

        # Default output <dir>/game.pak, then a second pack elsewhere: the
        # first archive sits in the tree but must not be packed.
        rc, out = run(src)
        assert rc == 0 and "pak: 5 file(s)" in out, out
        first = os.path.join(src, "game.pak")
        second = os.path.join(d, "again.pak")
        rc, out = run(src, "-o", second, "-q")
        assert rc == 0 and out == "", out
        a, b = open(first, "rb").read(), open(second, "rb").read()
        assert a == b, "pak output differs between runs"
        assert a[:4] == b"VPAK" and len(a) == 8 + 5 * 64 + sum(map(len, files.values()))

        got = bast.pak_read(first)
        assert [n for n, _ in got] == sorted(files), got
        assert dict(got) == files

        rc, out = run("--list", first)
        assert rc == 0 and "sfx/door_open.snd" in out and "     768  palette.bin" in out, out

        # Over-long name: refused, nothing written.
        write(os.path.join(src, "x" * 56), b"1")
        bad = os.path.join(d, "bad.pak")
        rc, out = run(src, "-o", bad)
        assert rc == 1 and "longer than 55" in out and not os.path.exists(bad), out

        # The wrapper rejects duplicates (bast_pak_write).
        try:
            bast.pak_write(bad, [("a", b"1"), ("a", b"2")])
            assert False, "expected BastError"
        except bast.BastError as e:
            assert "duplicate" in str(e)
        try:
            bast.pak_read(os.path.join(src, "old.pak"))
            assert False, "expected BastError"
        except bast.BastError as e:
            assert "VPAK" in str(e)
    print("test_pak: OK")


if __name__ == "__main__":
    main()
