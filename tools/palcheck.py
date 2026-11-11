#!/usr/bin/env python3
"""Check that images conform to the game palette.

Usage: python3 tools/palcheck.py [--gpl FILE] <files or dirs>...

Directories are scanned recursively for *.png and *.tex. For each PNG, fails on:
- image mode other than P (indexed);
- a used index whose PNG palette entry differs from the game palette;
- a used index that the game palette marks reserved.
One line per problem: "<file>:<x>,<y>: <message>" (first pixel using the
index, plus how many pixels share the problem). Exit 1 if any problem,
else exit 0 with a summary line.

For each .tex (read through libbast, tools/bast.py), fails on a file that
does not parse and on a used index the game palette marks reserved. TEX
stores bare indices, so there is no colour to compare.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import palette  # noqa: E402


def check_png(path, entries, fl):
    problems = []
    try:
        img = Image.open(path)
        img.load()
    except Exception as e:  # unreadable or not an image
        return ["%s: cannot read image: %s" % (path, e)]
    if img.mode != "P":
        return ["%s: mode %s, must be indexed (P)" % (path, img.mode)]
    px = np.asarray(img)
    raw = img.getpalette() or []
    npal = len(raw) // 3
    used, counts = np.unique(px, return_counts=True)
    for idx, cnt in zip(used.tolist(), counts.tolist()):
        ys, xs = np.nonzero(px == idx)
        where = "%s:%d,%d" % (path, xs[0], ys[0])
        name = entries[idx][1]
        if fl[idx] & palette.FLAG_RESERVED:
            problems.append("%s: index %d (%s) is reserved (%d px)"
                            % (where, idx, name, cnt))
            continue
        if idx >= npal:
            problems.append("%s: index %d has no PNG palette entry (%d px)"
                            % (where, idx, cnt))
            continue
        got = tuple(raw[idx * 3:idx * 3 + 3])
        want = entries[idx][0]
        if got != want:
            problems.append(
                "%s: index %d is #%02X%02X%02X, game palette %s is #%02X%02X%02X (%d px)"
                % ((where, idx) + got + (name,) + want + (cnt,)))
    return problems


def check_tex(path, entries, fl):
    try:
        t = bast.Tex.load(path)
    except bast.BastError as e:
        return ["%s: cannot read TEX: %s" % (path, e)]
    h = t.height
    counts = {}
    first = {}
    for i, idx in enumerate(t.pixels):  # column-major: i = x * h + y
        counts[idx] = counts.get(idx, 0) + 1
        if idx not in first:
            first[idx] = (i // h, i % h)
    problems = []
    for idx in sorted(counts):
        if fl[idx] & palette.FLAG_RESERVED:
            x, y = first[idx]
            problems.append("%s:%d,%d: index %d (%s) is reserved (%d px)"
                            % (path, x, y, idx, entries[idx][1], counts[idx]))
    return problems


def collect(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, names in os.walk(p):
                dirs.sort()
                for n in sorted(names):
                    if n.lower().endswith((".png", ".tex")):
                        files.append(os.path.join(root, n))
        else:
            files.append(p)
    return files


def main(argv=None):
    ap = argparse.ArgumentParser(description="game palette checker")
    ap.add_argument("--gpl", default=palette.DEFAULT_GPL)
    ap.add_argument("paths", nargs="+")
    args = ap.parse_args(argv)
    try:
        entries = palette.load_gpl(args.gpl)
    except (OSError, palette.PaletteError) as e:
        print("palcheck: %s" % e, file=sys.stderr)
        return 1
    fl = palette.flags(entries)
    files = collect(args.paths)
    problems = []
    for f in files:
        if not os.path.exists(f):
            problems.append("%s: no such file" % f)
        elif f.lower().endswith(".tex"):
            problems.extend(check_tex(f, entries, fl))
        else:
            problems.extend(check_png(f, entries, fl))
    for p in problems:
        print(p)
    if problems:
        print("palcheck: FAIL, %d problem(s) in %d file(s)" % (len(problems), len(files)))
        return 1
    print("palcheck: OK, %d file(s) checked" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
