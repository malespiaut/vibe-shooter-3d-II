#!/usr/bin/env python3
"""ctest `death_demo`: demos/death-test.demo on maps/aitest.vmap (#103, #139).

1. As written (`on_death restart`): the player dies, presses use after the
   death view, and the restarted level passes `expect health=100`; exit 0.
2. The same demo without the `on_death` line (default `end`): the death
   ends the run with health=0 exit=0 in STATS and exit code 2.
  test_death_demo.py <game exe> <repo root> <tmp dir>
"""
import os
import subprocess
import sys

game, root, tmp = sys.argv[1:4]
os.makedirs(tmp, exist_ok=True)
demo = os.path.join(root, "demos", "death-test.demo")


def run(path):
    p = subprocess.run([game, "--headless", "--demo", path],
                       capture_output=True, text=True)
    line = p.stdout.strip().splitlines()[-1]
    assert line.startswith("STATS "), line
    return p, dict(kv.split("=", 1) for kv in line.split()[1:]), line


p, s, line = run(demo)
print(line)
assert p.returncode == 0, (p.returncode, p.stderr)
assert s["health"] == "100" and s["kills"] == "0/5" and s["exit"] == "0", line

text = open(demo).read()
assert "on_death restart" in text
end_demo = os.path.join(tmp, "death-end.demo")
with open(end_demo, "w") as f:
    f.write(text.replace("on_death restart", ""))
p, s, line = run(end_demo)
print(line)
print(p.stderr.strip())
assert p.returncode == 2, (p.returncode, p.stderr)
assert s["health"] == "0" and s["exit"] == "0", line
assert "died" in p.stderr, p.stderr
print("death_demo: ok")
