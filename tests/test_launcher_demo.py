#!/usr/bin/env python3
"""ctest `launcher_demo` (issue #93): demos/launcher-test.demo on
maps/launchertest.vmap kills three Mutt Troopers with one scarab and a
fourth with a barrel blast (STATS kills=4/4, every expect passes), gives the
same STATS hash and per-tic hashes twice; maps/launchertest.vmap matches its mkmap source.

    test_launcher_demo.py <build/game> <tmp dir>
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(game, demo, hash_log):
    p = subprocess.run([game, "--headless", "--demo", demo, "--hash-log", hash_log],
                       capture_output=True, text=True)
    assert p.returncode == 0, "exit %d: %s" % (p.returncode, p.stderr)
    stats = [l for l in p.stdout.splitlines() if l.startswith("STATS ")]
    assert len(stats) == 1, p.stdout
    return stats[0]


def main():
    game, tmp = sys.argv[1], sys.argv[2]
    os.makedirs(tmp, exist_ok=True)

    vmap = os.path.join(tmp, "launchertest.vmap")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkmap.py"),
                    os.path.join(ROOT, "maps", "src", "launchertest.txt"), vmap],
                   check=True, capture_output=True)
    with open(vmap, "rb") as a, open(os.path.join(ROOT, "maps", "launchertest.vmap"), "rb") as b:
        assert a.read() == b.read(), "maps/launchertest.vmap is stale: rerun mkmap.py"

    demo = os.path.join(ROOT, "demos", "launcher-test.demo")
    h1, h2 = os.path.join(tmp, "h1.log"), os.path.join(tmp, "h2.log")
    s1 = run(game, demo, h1)
    s2 = run(game, demo, h2)
    print(s1)
    assert " kills=4/4 " in s1, s1
    assert s1 == s2, (s1, s2)
    with open(h1) as a, open(h2) as b:
        assert a.read() == b.read(), "per-tic hashes differ between runs"
    print("launcher_demo: OK")


if __name__ == "__main__":
    main()
