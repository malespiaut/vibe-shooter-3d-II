#!/usr/bin/env python3
"""Pack a directory into a VPAK archive (docs/design/tech.md, "Archives").

    tools/pak.py assets/gen -o assets/gen/game.pak
    tools/pak.py --list assets/gen/game.pak

Every regular file under <dir> goes in, except the output archive itself and
any other *.pak. Names are paths relative to <dir> with '/', sorted, so the
same input always gives a byte-identical archive. The format itself lives in
libbast (bast_pak_write), reached through tools/bast.py.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402


def collect(root, out_path):
    out_abs = os.path.abspath(out_path) if out_path else None
    entries = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for f in filenames:
            full = os.path.join(dirpath, f)
            if not os.path.isfile(full) or f.endswith(".pak"):
                continue
            if out_abs and os.path.abspath(full) == out_abs:
                continue
            name = os.path.relpath(full, root).replace(os.sep, "/")
            if len(name.encode()) > bast.PAK_NAME_MAX:
                raise bast.BastError("%s: name longer than %d chars" % (name, bast.PAK_NAME_MAX))
            entries.append(name)
    entries.sort()
    return entries


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dir", help="directory to pack (or archive with --list)")
    ap.add_argument("-o", "--out", help="output archive (default <dir>/game.pak)")
    ap.add_argument("--list", action="store_true", help="list an archive's entries")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args(argv)
    try:
        if a.list:
            for name, data in bast.pak_read(a.dir):
                print("%8d  %s" % (len(data), name))
            return 0
        out = a.out or os.path.join(a.dir, "game.pak")
        names = collect(a.dir, out)
        entries = []
        for n in names:
            with open(os.path.join(a.dir, n), "rb") as f:
                entries.append((n, f.read()))
        tmp = out + ".tmp"
        bast.pak_write(tmp, entries)
        os.replace(tmp, out)
    except (bast.BastError, OSError) as e:
        print("pak: %s" % e, file=sys.stderr)
        return 1
    if not a.quiet:
        total = sum(len(d) for _, d in entries)
        print("pak: %d file(s), %d bytes of data -> %s (%d bytes)"
              % (len(entries), total, out, os.path.getsize(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
