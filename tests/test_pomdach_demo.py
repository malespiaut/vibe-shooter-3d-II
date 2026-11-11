#!/usr/bin/env python3
"""ctest `pomdach_demo` (issue #95): demos/pomdach-test.demo on
maps/pomdachtest.vmap kills two Pomeranian Plasmagunners and a Dachshund
Howitzer with the repeater while strafing (STATS kills=3/3, health > 0, every
expect passes), gives the same STATS hash and per-tic hashes twice;
maps/pomdachtest.vmap matches its mkmap source.

    test_pomdach_demo.py <build/game> <tmp dir>
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

    vmap = os.path.join(tmp, "pomdachtest.vmap")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkmap.py"),
                    os.path.join(ROOT, "maps", "src", "pomdachtest.txt"), vmap],
                   check=True, capture_output=True)
    with open(vmap, "rb") as a, open(os.path.join(ROOT, "maps", "pomdachtest.vmap"), "rb") as b:
        assert a.read() == b.read(), "maps/pomdachtest.vmap is stale: rerun mkmap.py"

    demo = os.path.join(ROOT, "demos", "pomdach-test.demo")
    h1, h2 = os.path.join(tmp, "h1.log"), os.path.join(tmp, "h2.log")
    s1 = run(game, demo, h1)
    s2 = run(game, demo, h2)
    print(s1)
    assert " kills=3/3 " in s1, s1
    health = int(s1.split(" health=")[1].split()[0])
    assert health > 0, s1
    assert s1 == s2, (s1, s2)
    with open(h1) as a, open(h2) as b:
        assert a.read() == b.read(), "per-tic hashes differ between runs"
    print("pomdach_demo: OK")


if __name__ == "__main__":
    main()
