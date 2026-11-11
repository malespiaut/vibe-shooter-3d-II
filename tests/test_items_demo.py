#!/usr/bin/env python3
"""ctest `items_demo`: demos/items-test.demo on maps/itemstest.vmap (#64).

Runs the demo headless twice at skill 2: the final STATS line shows
health=200 armor=200 items=20/20, and both runs give the same hash. At
skill 3 the skill-3-only Scarab Plate spawns too (items=20/21).
  test_items_demo.py <game exe> <repo root>
"""
import os
import subprocess
import sys

game, root = sys.argv[1:3]
demo = os.path.join(root, "demos", "items-test.demo")


def stats(*extra):
    p = subprocess.run([game, "--headless", "--demo", demo, *extra],
                       capture_output=True, text=True)
    assert p.returncode == 0, (p.returncode, p.stderr)
    line = p.stdout.strip().splitlines()[-1]
    assert line.startswith("STATS "), line
    return dict(kv.split("=", 1) for kv in line.split()[1:]), line


a, la = stats()
b, lb = stats()
print(la)
print(lb)
assert a["health"] == "200" and a["armor"] == "200" and a["items"] == "20/20", la
assert a["hash"] == b["hash"] and la == lb, (la, lb)
c, lc = stats("--skill", "3")
print(lc)
assert c["items"] == "20/21", lc
print("items_demo: ok")
