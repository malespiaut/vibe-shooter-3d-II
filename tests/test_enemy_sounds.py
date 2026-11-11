#!/usr/bin/env python3
"""ctest `enemy_sounds` (issue #170): enemy voices reach --sound-log.

Replays demos/ai-test.demo headless with --sound-log and checks the enemy
sounds against an independent oracle: `test_sound enemies` replays the same
demo in-process and prints every enemy state change per tic (game state,
not sound). States: thinker.h TH_STATE_*.
  sight:   exactly one `*_sight` per enemy, on the tic it left STAND alive,
           at its tile
  windup:  one `*_windup` per entry into WINDUP (same tic, same tile), and
           every `mutt_fire` / `chomp_bite` follows a windup at its position
  attack:  one `mutt_fire` / `chomp_bite` per WINDUP -> ATTACK
  pain:    every entry into PAIN has a `*_pain` on that tic at that tile
  death:   one `*_death` or `gib_splat` per kill (kills=5 -> 5 lines), the
           gib_splat exactly for the gibbed ones
Also checks that --sound-log leaves the STATS line unchanged.
  test_enemy_sounds.py <game exe> <test_sound exe> <repo root> <tmp dir>
"""
import collections
import os
import subprocess
import sys

game, helper, root, tmp = sys.argv[1:5]
os.makedirs(tmp, exist_ok=True)
STAND, DEAD, CHASE, WINDUP, ATTACK, PAIN = 0, 1, 3, 4, 5, 6
PREFIX = {16: "mutt", 17: "chomp"}  # bast_entity_type -> voice prefix
ATTACK_SND = {16: "mutt_fire", 17: "chomp_bite"}

demo = os.path.join(root, "demos", "ai-test.demo")
log = os.path.join(tmp, "ai-test.sounds")
p = subprocess.run([game, "--headless", "--demo", demo, "--sound-log", log],
                   capture_output=True, text=True)
assert p.returncode == 0, (p.returncode, p.stderr)
stats = p.stdout.strip().splitlines()[-1]
q = subprocess.run([game, "--headless", "--demo", demo], capture_output=True, text=True)
assert q.returncode == 0 and q.stdout.strip().splitlines()[-1] == stats, (stats, q.stdout)
assert "kills=5/5" in stats, stats

events = []  # (tic, name, x, y) for positional sounds
with open(log) as f:
    for line in f:
        tic, snd, x, y = line.split()
        if x != "-":
            events.append((int(tic), snd, float(x), float(y)))

o = subprocess.run([helper, "enemies", os.path.join(root, "maps"), demo],
                   capture_output=True, text=True, check=True)
changes = []  # (tic, index, type, before, after, tx, ty, gib)
for l in o.stdout.splitlines():
    tic, i, ty, b, a, x, y, gib = (int(v) for v in l.split())
    assert ty in PREFIX, ("unexpected enemy type in ai-test", ty)
    changes.append((tic, i, ty, b, a, x >> 16, y >> 16, gib))

enemy_names = {f"{p}_{s}" for p in PREFIX.values()
               for s in ("sight", "windup", "pain", "death")}
enemy_names |= set(ATTACK_SND.values()) | {"gib_splat"}
ev = [e for e in events if e[1] in enemy_names]
print(f"ai-test: {len(ev)} enemy sound events, {len(changes)} state changes; {stats}")
for e in ev:
    print("  ", e[0], e[1], f"{e[2]:.4f}", f"{e[3]:.4f}")


def lines(name, pred=lambda e: True):
    return collections.Counter((e[0], e[1], int(e[2]), int(e[3]))
                               for e in ev if e[1] == name and pred(e))


def want(rows):
    return collections.Counter(rows)


def check(label, got, exp):
    assert got == exp, (label, "got", sorted(got.elements()), "want", sorted(exp.elements()))
    print(f"{label}: {sum(got.values())} ok")


# sight: once per enemy, on the wake tic, at its tile.
woke = [c for c in changes if c[3] == STAND and c[4] != DEAD]
assert len({c[1] for c in woke}) == len(woke) == 5, woke
got = collections.Counter()
exp = collections.Counter()
for ty, pre in PREFIX.items():
    got += lines(pre + "_sight")
    exp += want((c[0], pre + "_sight", c[5], c[6]) for c in woke if c[2] == ty)
check("sight", got, exp)

# windup and attack.
got, exp = collections.Counter(), collections.Counter()
for ty, pre in PREFIX.items():
    got += lines(pre + "_windup")
    exp += want((c[0], pre + "_windup", c[5], c[6]) for c in changes
                if c[2] == ty and c[4] == WINDUP)
check("windup", got, exp)
got, exp = collections.Counter(), collections.Counter()
for ty, snd in ATTACK_SND.items():
    got += lines(snd)
    exp += want((c[0], snd, c[5], c[6]) for c in changes
                if c[2] == ty and c[3] == WINDUP and c[4] == ATTACK)
check("attack", got, exp)
assert sum(exp.values()) >= 1, "ai-test should contain at least one enemy attack"
for e in ev:
    if e[1] in ATTACK_SND.values():
        pre = e[1].split("_")[0]
        assert [w for w in ev if w[1] == pre + "_windup" and w[0] < e[0] and
                (w[2], w[3]) == (e[2], e[3])], ("attack without windup", e)

# pain: every entry into PAIN is voiced.
for c in changes:
    if c[4] == PAIN and c[3] != PAIN:
        snd = PREFIX[c[2]] + "_pain"
        assert lines(snd)[(c[0], snd, c[5], c[6])] >= 1, ("silent pain", c)

# death: one line per kill, gib_splat exactly for the gibs.
kills = [c for c in changes if c[4] == DEAD]
assert len(kills) == 5, kills
got, exp = collections.Counter(), collections.Counter()
for ty, pre in PREFIX.items():
    got += lines(pre + "_death")
    exp += want((c[0], pre + "_death", c[5], c[6]) for c in kills if c[2] == ty and not c[7])
got += lines("gib_splat")
exp += want((c[0], "gib_splat", c[5], c[6]) for c in kills if c[7])
check("death", got, exp)
assert sum(1 for c in kills if c[7]) >= 1, "ai-test should gib at least one enemy"
print("enemy_sounds: ok")
