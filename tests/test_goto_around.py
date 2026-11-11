#!/usr/bin/env python3
"""ctest `goto_around` (issue #161): demos/goto-around.demo on
maps/launchertest.vmap. goto routes around the AMBUSH Mutt standing on the
straight path and reaches tile 14,8 well under the 1050-tic default
timeout; the whole demo exits 0 and gives the same STATS line twice.

    test_goto_around.py <build/game> <tmp dir>
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEMO = os.path.join(ROOT, "demos", "goto-around.demo")


def run(game, demo):
    p = subprocess.run([game, "--headless", "--demo", demo], capture_output=True, text=True)
    assert p.returncode == 0, "exit %d: %s" % (p.returncode, p.stderr)
    stats = [l for l in p.stdout.splitlines() if l.startswith("STATS ")]
    assert len(stats) == 1, p.stdout
    return stats[0]


def main():
    game, tmp = sys.argv[1], sys.argv[2]
    os.makedirs(tmp, exist_ok=True)

    # The demo cut after its `expect tile=14,8` line: its STATS tic is the
    # tic goto arrived. The map line resolves against maps/ (absolute dir).
    with open(DEMO) as f:
        lines = f.read().splitlines(True)
    cut = [l for l in lines[:lines.index("expect tile=14,8\n") + 1]]
    head = os.path.join(tmp, "goto-around-head.demo")
    with open(head, "w") as f:
        f.writelines(cut)
    s = run(game, head)
    tic = int(dict(kv.split("=", 1) for kv in s.split()[1:])["tic"])
    print("arrived at tile 14,8 at tic %d: %s" % (tic, s))
    assert tic < 350, "goto took %d tics" % tic

    s1 = run(game, DEMO)
    s2 = run(game, DEMO)
    print(s1)
    assert s1 == s2, (s1, s2)
    print("goto_around: OK")


if __name__ == "__main__":
    main()
