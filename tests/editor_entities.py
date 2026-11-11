#!/usr/bin/env python3
"""ctest `editor_entities`: the editor's entity type table (editor/entities.py)
matches `enum bast_entity_type` in src/bast/bast.h exactly (numbers and
names, in order), and matches libbast's runtime name table. Plain asserts.
BAST_LIB overrides the libbast.so path."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from editor import entities as E  # noqa: E402
from editor.model import bast  # noqa: E402

HEADER = os.path.join(ROOT, "src", "bast", "bast.h")


def header_enum():
    with open(HEADER) as f:
        text = f.read()
    body = re.search(r"enum\s+bast_entity_type\s*\{(.*?)\};", text, re.S)
    assert body, "enum bast_entity_type not found in bast.h"
    body = re.sub(r"/\*.*?\*/", "", body.group(1), flags=re.S)
    out = []
    for name, value in re.findall(r"BAST_ENT_(\w+)\s*=\s*(\d+)", body):
        out.append((name, int(value)))
    # Every enumerator must carry an explicit number (format contract).
    n_items = len([s for s in body.split(",") if s.strip()])
    assert n_items == len(out), "enumerator without explicit value in bast.h"
    return out


def main():
    enum = header_enum()
    special = {"NONE", "TYPE_LIMIT"}
    c_types = [(v, n) for n, v in enum if n not in special]
    limit = dict(enum)["TYPE_LIMIT"]
    py_types = [(t[0], t[1]) for t in E.TYPES]

    missing = sorted(set(c_types) - set(py_types))
    extra = sorted(set(py_types) - set(c_types))
    assert not missing, "in bast.h but not editor/entities.py: %r" % missing
    assert not extra, "in editor/entities.py but not bast.h: %r" % extra
    assert py_types == c_types, "same entries, different order"
    assert len({t[0] for t in E.TYPES}) == len(E.TYPES), "duplicate numbers"
    assert all(0 < t[0] < limit for t in E.TYPES), "type outside 1..TYPE_LIMIT-1"

    for t in E.TYPES:
        assert t[2] == E.category_of(t[0]) and t[2] in E.CATEGORIES, t
        assert t[3], t

    # libbast's runtime table agrees for every number in range.
    for n in range(limit + 32):
        want = E.BY_NUMBER[n][1] if n in E.BY_NUMBER else None
        assert bast.entity_type_name(n) == want, (n, bast.entity_type_name(n), want)
    for name in E.NAMED_TYPES:
        assert name in E.BY_NAME, name

    print("editor_entities: ok (%d types)" % len(E.TYPES))


if __name__ == "__main__":
    main()
