#!/usr/bin/env python3
"""ctest `demo_combat_fire`, `demo_combat_clear`, `demo_combat_notarget`:
the combat demo commands (tech.md Demos; issue #98) through
build/game --headless.

  test_demo_combat.py <game exe> <tmp dir> fire|clear|notarget

fire:     tests/fixtures/combat_one.txt: `fire nearest` turns from north to the
          standing Mutt 6 units east and kills it with the pistol, ignoring a
          nearer Mutt sealed behind a wall (kills=1/2); a too small max exits 2
          with the timeout message.
clear:    maps/combattest.vmap at skill 2: `clear` kills 3 Mutt Troopers and 3
          Chihuahua Chompers without dying (kills=6, health>0).
notarget: `fire nearest` and `face nearest` on a map with no enemy exit 2 with
          "<cmd>: no visible enemy at tic N".
Every passing demo is replayed twice with --hash-log; the logs must match.
"""
import filecmp
import os
import shutil
import subprocess
import sys

game, tmp, case = sys.argv[1:4]
tmp = os.path.join(tmp, case)
shutil.rmtree(tmp, ignore_errors=True)
os.makedirs(tmp)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def header(skill=2, mapname="combattest"):
    return f"vsdemo 1\nlevel 1\nskill {skill}\nseed 0\nmap {mapname}\n"


def fixture(name):
    vmap = os.path.join(tmp, name + ".vmap")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkmap.py"),
                    os.path.join(ROOT, "tests", "fixtures", name + ".txt"), vmap],
                   check=True, capture_output=True)
    return vmap


def run(name, text, *args):
    path = os.path.join(tmp, name + ".demo")
    with open(path, "w") as f:
        f.write(text)
    return subprocess.run([game, "--headless", "--demo", path, *args],
                          capture_output=True, text=True)


def stats(p):
    last = p.stdout.strip().splitlines()[-1]
    assert last.startswith("STATS "), p.stdout
    return dict(kv.split("=", 1) for kv in last.split()[1:])


def run_twice(name, text, *args):
    """Runs a passing demo twice with --hash-log; the logs must be equal."""
    logs = []
    for r in "ab":
        log = os.path.join(tmp, f"{name}_{r}.log")
        p = run(name, text, "--hash-log", log, *args)
        assert p.returncode == 0, (name, p.returncode, p.stderr)
        logs.append(log)
    assert filecmp.cmp(logs[0], logs[1], shallow=False), name
    with open(logs[0]) as f:
        n = sum(1 for _ in f)
    return p, n


def test_fire():
    vmap = fixture("combat_one")
    body = header() + "expect kills=0\nface nearest\nfire nearest\nexpect kills=1\nexpect health>0\n"
    p, n = run_twice("fire", body, "--map", vmap)
    s = stats(p)
    assert s["kills"] == "1/2", s  # the sealed Mutt was never the target
    print("demo_combat_fire: OK", s["tic"], s["hash"], f"hash log {n} lines identical")
    # 10 tics: turned, maybe fired once, not dead yet.
    p = run("fire_short", header() + "fire nearest 10\n", "--map", vmap)
    assert p.returncode == 2, (p.returncode, p.stderr)
    assert "line 6: fire nearest: timed out after 10 tics, thinker 1 still alive" in p.stderr, \
        p.stderr
    assert stats(p)["tic"] == "10", p.stdout


def test_clear():
    body = header() + "expect kills=0\nclear\nexpect kills=6\nexpect health>0\n"
    p, n = run_twice("clear", body)
    s = stats(p)
    assert s["kills"] == "6/6" and int(s["health"]) > 0, s
    print("demo_combat_clear: OK", s["tic"], s["hash"], "health", s["health"],
          f"hash log {n} lines identical")
    # Out of reach: a 3-unit radius sees nobody, so clear is done at once.
    p = run("clear_r3", header() + "clear 3\nexpect tic=0\nexpect kills=0\n")
    assert p.returncode == 0, p.stderr
    # Timeout names the line and what is left.
    p = run("clear_short", header() + "wait 1\nclear 10 20\n")
    assert p.returncode == 2, (p.returncode, p.stderr)
    assert "line 7: clear: timed out after 20 tics with" in p.stderr, p.stderr


def test_notarget():
    vmap = fixture("nav_sealed")  # no enemies at all
    for cmd in ("fire nearest", "face nearest"):
        p = run("none", header() + "wait 4\n" + cmd + "\nwait 10\n", "--map", vmap)
        assert p.returncode == 2, (cmd, p.returncode, p.stderr)
        want = f"line 7: {cmd}: no visible enemy at tic 4"
        assert want in p.stderr, (cmd, p.stderr)
        assert stats(p)["tic"] == "4", p.stdout
    print("demo_combat_notarget: OK")


{"fire": test_fire, "clear": test_clear, "notarget": test_notarget}[case]()
