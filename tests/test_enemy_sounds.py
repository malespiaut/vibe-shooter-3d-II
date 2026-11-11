#!/usr/bin/env python3
"""ctest `enemy_sounds` (issue #170): enemy voices reach --sound-log.

Replays demos (ai-test, pomdach-test, lance-howler-test) headless with --sound-log and checks the enemy sounds against
an independent oracle: `test_sound enemies` replays the same demo in-process
and prints game state only (no sound): every enemy state change per tic,
every tic the player's health dropped, and every plasma bolt / mortar
reticle spawned (with the owner's position). States: thinker.h TH_STATE_*.

Each enemy attack sound has one owner (ai.c):
  sight:   exactly one `*_sight` per enemy, on the tic it left STAND alive,
           at its tile
  windup:  one wind-up sound per entry into WINDUP (same tic, same tile);
           the Dachshund's is `reticle_beep` at its own tile
  attack:  `mutt_fire` on every WINDUP -> ATTACK (hit or miss);
           `chomp_bite` on a WINDUP -> ATTACK that damaged the player;
           `plasma_fire` exactly once per bolt, at the Pomeranian, on the
           bolt's spawn tic; `mortar_launch` exactly once per reticle, at
           the Dachshund, and nowhere else (so never on a headbutt)
  pain:    every entry into PAIN has a `*_pain` on that tic at that tile
  death:   one `*_death` or `gib_splat` per kill, the gib_splat exactly
           for the gibbed ones
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
MUTT, CHOMP, POM, DACH = 16, 17, 18, 19
PREFIX = {MUTT: "mutt", CHOMP: "chomp", POM: "pom", DACH: "dach"}
WINDUP_SND = {MUTT: "mutt_windup", CHOMP: "chomp_windup", POM: "pom_windup",
              DACH: "reticle_beep"}


def run_demo(name, kills):
    demo = os.path.join(root, "demos", name + ".demo")
    log = os.path.join(tmp, name + ".sounds")
    p = subprocess.run([game, "--headless", "--demo", demo, "--sound-log", log],
                       capture_output=True, text=True)
    assert p.returncode == 0, (p.returncode, p.stderr)
    stats = p.stdout.strip().splitlines()[-1]
    q = subprocess.run([game, "--headless", "--demo", demo], capture_output=True, text=True)
    assert q.returncode == 0 and q.stdout.strip().splitlines()[-1] == stats, (stats, q.stdout)
    assert f"kills={kills}" in stats, stats

    events = []  # (tic, name, tx, ty) for positional sounds
    with open(log) as f:
        for line in f:
            v = line.split()
            if len(v) == 4 and v[2] != "-":
                events.append((int(v[0]), v[1], int(float(v[2])), int(float(v[3]))))

    o = subprocess.run([helper, "enemies", os.path.join(root, "maps"), demo],
                       capture_output=True, text=True, check=True)
    changes = []  # (tic, index, type, before, after, tx, ty, gib)
    hurt = set()  # tics on which the player lost health
    spawns = []   # (tic, kind, owner, otx, oty)
    for l in o.stdout.splitlines():
        v = l.split()
        if v[0] == "player":
            hurt.add(int(v[1]))
        elif v[0] == "spawn":
            spawns.append((int(v[1]), v[2], int(v[3]), int(v[4]) >> 16, int(v[5]) >> 16))
        else:
            tic, i, ty, b, a, x, y, gib = (int(n) for n in v)
            assert ty in PREFIX, (name, "unexpected enemy type", ty)
            changes.append((tic, i, ty, b, a, x >> 16, y >> 16, gib))
    print(f"{name}: {len(events)} positional sounds, {len(changes)} enemy state changes, "
          f"{len(spawns)} bolt/reticle spawns; {stats}")
    return events, changes, hurt, spawns


def count(events, snd, where=None):
    return collections.Counter((e[0], e[1], e[2], e[3]) for e in events
                               if e[1] == snd and (where is None or (e[2], e[3]) in where))


def check(label, got, exp):
    assert got == exp, (label, "got", sorted(got.elements()), "want", sorted(exp.elements()))
    print(f"  {label}: {sum(got.values())} ok")
    return sum(got.values())


def check_demo(name, kills, types):
    ev, changes, hurt, spawns = run_demo(name, kills)
    mine = [c for c in changes if c[2] in types]
    W = collections.Counter

    # sight: once per enemy, on the wake tic, at its tile.
    woke = [c for c in changes if c[3] == STAND and c[4] != DEAD]
    assert len({c[1] for c in woke}) == len(woke), woke
    for ty in types:
        snd = PREFIX[ty] + "_sight"
        check(snd, count(ev, snd), W((c[0], snd, c[5], c[6]) for c in woke if c[2] == ty))

    # windup: one per entry into WINDUP, at the enemy.
    windups = {}
    for ty in types:
        snd = WINDUP_SND[ty]
        exp = W((c[0], snd, c[5], c[6]) for c in mine if c[2] == ty and c[4] == WINDUP)
        if ty == DACH:  # reticle_beep also beeps at the reticles: only the Dachshund's tile
            got = W(k for k in count(ev, snd).elements() if k in exp)
            # and no extra beep at a Dachshund tile on its wind-up tic
            assert all(count(ev, snd)[k] == exp[k] for k in exp), (snd, exp)
        else:
            got = count(ev, snd)
        windups[ty] = check(snd, got, exp)

    # attack sounds, one owner each.
    fires = {}
    if MUTT in types:
        exp = W((c[0], "mutt_fire", c[5], c[6]) for c in mine
                if c[2] == MUTT and c[3] == WINDUP and c[4] == ATTACK)
        fires[MUTT] = check("mutt_fire (every shot)", count(ev, "mutt_fire"), exp)
    if CHOMP in types:
        exp = W((c[0], "chomp_bite", c[5], c[6]) for c in mine
                if c[2] == CHOMP and c[3] == WINDUP and c[4] == ATTACK and c[0] in hurt)
        fires[CHOMP] = check("chomp_bite (hits)", count(ev, "chomp_bite"), exp)
    if POM in types:
        exp = W((s[0], "plasma_fire", s[3], s[4]) for s in spawns if s[1] == "bolt")
        fires[POM] = check("plasma_fire (one per bolt)", count(ev, "plasma_fire"), exp)
    if DACH in types:
        exp = W((s[0], "mortar_launch", s[3], s[4]) for s in spawns if s[1] == "reticle")
        fires[DACH] = check("mortar_launch (one per reticle)", count(ev, "mortar_launch"), exp)

    # pain: every entry into PAIN is voiced.
    for c in mine:
        if c[4] == PAIN and c[3] != PAIN:
            snd = PREFIX[c[2]] + "_pain"
            assert count(ev, snd)[(c[0], snd, c[5], c[6])] >= 1, ("silent pain", c)

    # death: one line per kill, gib_splat exactly for the gibs.
    dead = [c for c in mine if c[4] == DEAD]
    got, exp = W(), W()
    for ty in types:
        snd = PREFIX[ty] + "_death"
        got += count(ev, snd)
        exp += W((c[0], snd, c[5], c[6]) for c in dead if c[2] == ty and not c[7])
    got += count(ev, "gib_splat")
    exp += W((c[0], "gib_splat", c[5], c[6]) for c in dead if c[7])
    check("death", got, exp)
    return windups, fires, dead


w, f, dead = check_demo("ai-test", "5/5", (MUTT, CHOMP))
assert f[MUTT] >= 1 and len(dead) == 5 and any(c[7] for c in dead), (f, dead)
w, f, dead = check_demo("pomdach-test", "3/3", (POM, DACH))
assert w[POM] >= 1 and w[DACH] >= 1 and f[POM] >= 2 and f[DACH] >= 1, (w, f)
assert len(dead) == 3, dead
w, f, dead = check_demo("lance-howler-test", "13/13", (MUTT, CHOMP))
assert w[CHOMP] >= 1 and f[CHOMP] >= 1, (w, f)  # Chompers that bite
print("enemy_sounds: ok")
