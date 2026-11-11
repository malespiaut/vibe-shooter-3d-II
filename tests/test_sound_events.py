#!/usr/bin/env python3
"""ctest `sound_events` (issue #118): game sounds reach --sound-log.

Runs demos headless with --sound-log and checks the expected names at the
expected tics. The tics come from an independent oracle: `test_sound trace`
replays the same demo in-process and prints, per tic, whether use was
pressed and the player's key bits (game state, not sound).
  door-test:   door_open on the use tic, at door D (2.5, 4.5)
  door-locked: door_locked on the use tic at door G (4.5, 4.5), no door_open
  items-test:  key_pickup on each tic a key bit appears, plus item_pickup,
               weapon_pickup and powerup_pickup somewhere in the run
  launcher-test: one scarab_launch (local), then one positional
               scarab_explode and one positional barrel_explode
Also checks that --sound-log leaves the STATS hash unchanged.
  test_sound_events.py <game exe> <test_sound exe> <repo root> <tmp dir>
"""
import os
import subprocess
import sys

game, helper, root, tmp = sys.argv[1:5]
os.makedirs(tmp, exist_ok=True)
maps = os.path.join(root, "maps")


def run(name):
    demo = os.path.join(root, "demos", name + ".demo")
    log = os.path.join(tmp, name + ".sounds")
    p = subprocess.run([game, "--headless", "--demo", demo, "--sound-log", log],
                       capture_output=True, text=True)
    assert p.returncode == 0, (name, p.returncode, p.stderr)
    stats = p.stdout.strip().splitlines()[-1]
    q = subprocess.run([game, "--headless", "--demo", demo], capture_output=True, text=True)
    assert q.returncode == 0 and q.stdout.strip().splitlines()[-1] == stats, (stats, q.stdout)
    events = []
    with open(log) as f:
        for line in f:
            tic, snd, x, y = line.split()
            events.append((int(tic), snd, x, y))
    t = subprocess.run([helper, "trace", maps, demo], capture_output=True, text=True,
                       check=True)
    trace = [tuple(int(v) for v in l.split()) for l in t.stdout.splitlines()]
    print(f"{name}: {len(events)} events, {len(trace)} tics; {stats}")
    for e in events:
        print("  ", *e)
    return events, trace


def at(events, tic, snd):
    return [e for e in events if e[0] == tic and e[1] == snd]


# door-test: the one use opens door D.
ev, tr = run("door-test")
uses = [t for t, use, _ in tr if use]
assert len(uses) == 1, uses
e = at(ev, uses[0], "door_open")
assert e and e[0][2:] == ("2.5000", "4.5000"), (uses, ev)
assert not [x for x in ev if x[1] in ("door_locked", "noway")], ev

# door-locked: green door refuses without the key.
ev, tr = run("door-locked")
uses = [t for t, use, keys in tr if use]
assert len(uses) == 1 and tr[-1][2] == 0, (uses, tr[-1])
e = at(ev, uses[0], "door_locked")
assert e and e[0][2:] == ("4.5000", "4.5000"), (uses, ev)
assert not [x for x in ev if x[1] == "door_open"], ev

# items-test: a key_pickup on every tic a key bit appears, and only then.
ev, tr = run("items-test")
key_tics = [t for i, (t, _, keys) in enumerate(tr) if keys & ~(tr[i - 1][2] if i else 0)]
assert len(key_tics) == 3, key_tics
assert [x[0] for x in ev if x[1] == "key_pickup"] == key_tics, (key_tics, ev)
names = {x[1] for x in ev}
for n in ("item_pickup", "weapon_pickup", "powerup_pickup"):
    assert n in names, (n, names)

# launcher-test: one scarab fired, one impact, one barrel set off by the pistol.
ev, tr = run("launcher-test")
for n in ("scarab_launch", "scarab_explode", "barrel_explode"):
    assert len([x for x in ev if x[1] == n]) == 1, (n, ev)
la = [x for x in ev if x[1] == "scarab_launch"][0]
ex = [x for x in ev if x[1] == "scarab_explode"][0]
assert la[2:] == ("-", "-") and ex[2] != "-" and ex[0] > la[0], (la, ex)
assert [x for x in ev if x[1] == "barrel_explode"][0][2] != "-", ev
print("sound_events: ok")
