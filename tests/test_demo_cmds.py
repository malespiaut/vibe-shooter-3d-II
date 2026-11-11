#!/usr/bin/env python3
"""ctest `demo_goto`, `demo_timeout`, `demo_expect`: high-level demo commands
(tech.md Demos) through build/game --headless on maps/m1test.vmap.

  test_demo_cmds.py <game exe> <tmp dir> goto|timeout|expect

goto:    goto @hall / @courtyard / tiles, each followed by expect tile=X,Y;
         exit 0, and the same demo twice gives the same STATS hash.
timeout: a goto to a reachable tile with too small a max times out after
         exactly max tics; a goto to an open tile sealed off by walls, one
         behind a closed door (no door motion yet), and one to a solid tile
         exit 2 at once. Every failure exits 2 naming the goto's line.
expect:  true expects of every op pass (exit 0); each false one exits 2
         naming its line, before any later tic runs.
"""
import os
import shutil
import subprocess
import sys

game, tmp, case = sys.argv[1:4]
tmp = os.path.join(tmp, case)
shutil.rmtree(tmp, ignore_errors=True)
os.makedirs(tmp)

HEADER = "vsdemo 1\nlevel 1\nskill 2\nseed 0\nmap m1test\n"


def run(name, body, *args):
    path = os.path.join(tmp, name + ".demo")
    with open(path, "w") as f:
        f.write(HEADER + body)
    return subprocess.run([game, "--headless", "--demo", path, *args],
                          capture_output=True, text=True)


def stats(p):
    last = p.stdout.strip().splitlines()[-1]
    assert last.startswith("STATS "), p.stdout
    return dict(kv.split("=", 1) for kv in last.split()[1:])


def test_goto():
    # Lines 6.. of the demo (the header is 5 lines).
    body = ("goto @hall\n"
            "expect tile=16,6\n"
            "goto @courtyard\n"
            "expect tile=19,18\n"
            "goto 2 2\n"           # back to the start room
            "expect tile=2,2\n"
            "goto 4 21 600\n"      # NW dark room, through the west corridor
            "expect tile=4,21\n")
    p = run("goto", body)
    assert p.returncode == 0, (p.returncode, p.stderr)
    s1 = stats(p)
    p2 = run("goto", body)
    assert p2.returncode == 0 and stats(p2) == s1, (s1, p2.stdout)
    # A wrong tile expectation after the same goto fails: the check is real.
    p = run("goto_wrong", "goto @hall\nexpect tile=16,7\n")
    assert p.returncode == 2, (p.returncode, p.stderr)
    assert "line 7: expect tile=16,7 failed: player tile is 16,6" in p.stderr, p.stderr
    # Unknown waypoint.
    p = run("goto_noname", "wait 2\ngoto @nowhere\n")
    assert p.returncode == 2 and "line 7: goto: no WAYPOINT named 'nowhere'" in p.stderr, p.stderr
    print("demo_goto: OK", s1["tic"], s1["hash"])


def test_timeout():
    # Reachable, but 30 tics is not enough to reach the courtyard.
    p = run("timeout", "wait 3\ngoto @courtyard 30\nexpect tic=0\n")
    assert p.returncode == 2, (p.returncode, p.stderr)
    assert "line 7: goto 19,18: timed out after 30 tics" in p.stderr, p.stderr
    # The tic count proves the goto ran exactly max tics, then stopped.
    assert stats(p)["tic"] == "33", p.stdout
    p = run("solid", "goto 0 0\n")
    assert p.returncode == 2 and "line 6: goto 0,0: no path" in p.stderr, p.stderr

    # tests/fixtures/nav_sealed.txt: open pockets the player cannot reach.
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    vmap = os.path.join(tmp, "nav_sealed.vmap")
    subprocess.run([sys.executable, os.path.join(root, "tools", "mkmap.py"),
                    os.path.join(root, "tests", "fixtures", "nav_sealed.txt"), vmap],
                   check=True, capture_output=True)
    for name, tile in (("sealed", "6 1"), ("door", "6 5")):
        p = run(name, "wait 2\ngoto " + tile + "\n", "--map", vmap)
        assert p.returncode == 2, (name, p.returncode, p.stderr)
        want = "line 7: goto %s: no path from tile 2,2 at tic 2" % tile.replace(" ", ",")
        assert want in p.stderr, (name, p.stderr)
    p = run("open", "goto 3 5\nexpect tile=3,5\n", "--map", vmap)
    assert p.returncode == 0, p.stderr
    print("demo_timeout: OK")


def test_expect():
    ok = ("expect tic=0\nexpect health=100\nexpect armor=0\nexpect health!=99\n"
          "expect health>99\nexpect health>=100\nexpect health<101\nexpect health<=100\n"
          "expect kills=0\nexpect items=0\nexpect secrets=0\nexpect exit=0\n"
          "expect tile=2,2\nexpect tile!=3,2\n"
          "wait 7\nexpect tic=7\nexpect tic>6\n")
    p = run("true", ok)
    assert p.returncode == 0, (p.returncode, p.stderr)
    assert stats(p)["tic"] == "7"
    falses = ["health=99", "health!=100", "health>100", "health>=101", "health<100",
              "health<=99", "tile=2,3", "tile!=2,2", "exit=1", "tic>1"]
    for i, e in enumerate(falses):
        p = run(f"false{i}", f"wait 1\nexpect tic=1\nexpect {e}\nwait 5\n")
        assert p.returncode == 2, (e, p.returncode, p.stderr)
        assert f"line 8: expect {e} failed" in p.stderr, (e, p.stderr)
        assert stats(p)["tic"] == "1", (e, p.stdout)  # stopped before `wait 5`
    print("demo_expect: OK")


{"goto": test_goto, "timeout": test_timeout, "expect": test_expect}[case]()
