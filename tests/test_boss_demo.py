#!/usr/bin/env python3
"""ctest `boss_demo` (issue #97): demos/boss-test.demo on maps/bosstest.vmap
kills Woofu (and the Chompers of the first pod pair) with the Scarab
Launcher and the Eye of Ra Lance, walks through the exit door Woofu's death
opened and uses the exit (STATS kills=5/5 exit=1, health > 0, every expect
passes); the same STATS hash and per-tic hashes twice; the sound log shows
lid_blast, pod_open, woofu_death and the door opening 105 tics after the
death; maps/bosstest.vmap matches its mkmap source.

    test_boss_demo.py <build/game> <tmp dir>
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(game, demo, hash_log, sound_log):
    p = subprocess.run([game, "--headless", "--demo", demo, "--hash-log", hash_log,
                        "--sound-log", sound_log], capture_output=True, text=True)
    assert p.returncode == 0, "exit %d: %s %s" % (p.returncode, p.stdout, p.stderr)
    stats = [l for l in p.stdout.splitlines() if l.startswith("STATS ")]
    assert len(stats) == 1, p.stdout
    return stats[0]


def first(log, name):
    with open(log) as f:
        for line in f:
            tic, sfx = line.split()[:2]
            if sfx == name:
                return int(tic)
    return None


def main():
    game, tmp = sys.argv[1], sys.argv[2]
    os.makedirs(tmp, exist_ok=True)

    vmap = os.path.join(tmp, "bosstest.vmap")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkmap.py"),
                    os.path.join(ROOT, "maps", "src", "bosstest.txt"), vmap],
                   check=True, capture_output=True)
    with open(vmap, "rb") as a, open(os.path.join(ROOT, "maps", "bosstest.vmap"), "rb") as b:
        assert a.read() == b.read(), "maps/bosstest.vmap is stale: rerun mkmap.py"

    demo = os.path.join(ROOT, "demos", "boss-test.demo")
    h1, h2 = os.path.join(tmp, "h1.log"), os.path.join(tmp, "h2.log")
    s1 = run(game, demo, h1, os.path.join(tmp, "s1.log"))
    s2 = run(game, demo, h2, os.path.join(tmp, "s2.log"))
    print(s1)
    assert " kills=5/5 " in s1 and " exit=1 " in s1, s1
    health = int(s1.split(" health=")[1].split()[0])
    assert health > 0, s1
    assert s1 == s2, (s1, s2)
    with open(h1) as a, open(h2) as b:
        assert a.read() == b.read(), "per-tic hashes differ between runs"

    log = os.path.join(tmp, "s1.log")
    ev = {n: first(log, n) for n in ("lid_blast", "pod_open", "woofu_death", "door_open",
                                     "scarab_launch", "lance_fire")}
    print("first tics:", ev)
    assert all(v is not None for v in ev.values()), ev
    assert ev["lid_blast"] < ev["woofu_death"] and ev["pod_open"] < ev["woofu_death"]
    assert ev["door_open"] == ev["woofu_death"] + 105, ev
    print("boss_demo: OK")


if __name__ == "__main__":
    main()
