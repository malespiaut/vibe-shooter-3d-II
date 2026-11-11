#!/usr/bin/env python3
"""ctest `sfx_names` (issue #118): src/game/sfx_names.h lists exactly the
effects of assets/src/sfx/*.json, sorted, one SFX(name) row each.
  test_sfx_names.py <repo root>
"""
import glob
import os
import re
import sys

root = sys.argv[1]
want = sorted(os.path.basename(p)[:-5]
              for p in glob.glob(os.path.join(root, "assets/src/sfx/*.json")))
with open(os.path.join(root, "src/game/sfx_names.h")) as f:
    have = re.findall(r"^SFX\((\w+)\)$", f.read(), re.M)
missing = sorted(set(want) - set(have))
extra = sorted(set(have) - set(want))
assert not missing and not extra, f"missing from sfx_names.h: {missing}; not in assets/src/sfx: {extra}"
assert have == want, "sfx_names.h must be sorted by name"
print(f"sfx_names: {len(have)} effects match assets/src/sfx")
