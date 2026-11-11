#!/usr/bin/env python3
"""Level stats: per-skill enemy counts, enemy HP pool and item supply of a .vmap,
optionally checked against the docs/design/levels.md tables.

    python3 tools/levelstats.py maps/level3.vmap [--level 3] [--check] [--json]

Read-only. The map is loaded through tools/bast.py (libbast), never parsed a
second way. Exit codes: 0 ok, 1 file or argument error, 2 a --check failed.

Counting rules (see docs/design/levels.md "Difficulty curve"):
- An entity counts on skill N when its SKILLN flag is set.
- Enemy HP from enemies.md; Woofu counts 2400; summoned Chompers are not counted.
- Critical path = not SECRET_ONLY, not standing on a SECRET or PUSHWALL cell,
  and reachable from the player start without pushing a pushwall (a secret
  room usually flags only one cell SECRET, so its other items need this).
  Everything else is the secret supply, reported on its own line.
- Ammo is converted to average damage with the weapons.md per-ammo averages.
  Weapon pickups count their bundled ammo; the Litter Satchel one small pickup
  of each type. Guaranteed enemy drops (Mutt: a Charge Vial with 5 charges,
  enemies.md) are part of "ammo supplied" as levels.md defines it, and are
  also shown on their own.
- RESPAWN_ITEM counts once as the item in its param, and is flagged.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402

# ---- design data -----------------------------------------------------------

# enemies.md "Stats": type -> (short name, HP). Woofu counts as 2400.
ENEMIES = {
    16: ("mutt", 20),
    17: ("chomper", 15),
    18: ("pom", 45),
    19: ("dachshund", 70),
    20: ("mastiff", 400),
    21: ("shield", 90),
    22: ("woofu", 2400),
}
ENEMY_NAMES = [ENEMIES[t][0] for t in sorted(ENEMIES)]

# weapons.md "Ammo types" and "Behaviour notes"; per-ammo average damage from
# weapons.md "Average damage per unit of ammo".
AMMO_AVG = {"charges": 11.5, "shells": 49.0, "scarabs": 75.0, "cells": 24.0}
AMMO_ITEMS = {  # type -> (ammo, amount)
    32: ("charges", 10),   # Ankh Pistol (bundled ammo, src/game/weapon.c PICKUP_AMMO)
    33: ("shells", 8),     # Hairball Scattergun
    34: ("charges", 40),   # Sistrum Repeater
    35: ("scarabs", 4),    # Scarab Launcher
    36: ("cells", 60),     # Eye of Ra Lance
    37: ("cells", 50),     # Sphinx Howler
    54: ("charges", 10),   # Charge Vial
    55: ("charges", 50),   # Charge Jar
    56: ("shells", 4),     # Yarn Bundle
    57: ("shells", 20),    # Yarn Basket
    58: ("scarabs", 1),    # Scarab
    59: ("scarabs", 5),    # Scarab Crate
    60: ("cells", 20),     # Sun Shard
    61: ("cells", 100),    # Sun Orb
}
SATCHEL = 62  # Litter Satchel: one small pickup of each ammo type (items.md)
SATCHEL_AMMO = {"charges": 10, "shells": 4, "scarabs": 1, "cells": 20}
# enemies.md: a Mutt drops a Charge Vial with 5 charges on death.
DROPS = {16: ("charges", 5)}
# items.md "Health" and "Armour".
HEALTH_ITEMS = {48: 10, 49: 25, 50: 100}          # Fish Snack, Tuna Tin, Golden Sardine
ARMOUR_ITEMS = {51: 100, 52: 200, 53: 2}          # Linen Wrap, Scarab Plate, Bandage Scrap
KEYS = {63: "green", 64: "blue", 65: "violet"}
RESPAWN_ITEM = 6
PLAYER_START = 1

# docs/design/levels.md, section "Difficulty curve" (HP pool, ammo supplied,
# health, armour) plus the "Pools:" paragraph under it (per-type skill-2
# counts) and the "Overview" table (Par). Copy changes here when levels.md
# changes. L8 HP pool 4570 includes up to 12 summoned Chompers, which this
# tool does not count (4390 counted), still within 10%.
LEVELS = {
    1: {"enemies": {"mutt": 14}, "hp": 280, "ammo": 1150, "health": 60, "armour": 0,
        "par": 150},
    2: {"enemies": {"mutt": 18, "chomper": 10}, "hp": 510, "ammo": 1580, "health": 100,
        "armour": 100, "par": 240},
    3: {"enemies": {"mutt": 16, "chomper": 10, "pom": 8}, "hp": 830, "ammo": 2230,
        "health": 125, "armour": 100, "par": 300},
    4: {"enemies": {"mutt": 12, "chomper": 8, "pom": 8, "dachshund": 5}, "hp": 1070,
        "ammo": 2460, "health": 150, "armour": 100, "par": 360},
    5: {"enemies": {"mutt": 14, "chomper": 10, "pom": 10, "dachshund": 6, "mastiff": 2},
        "hp": 2100, "ammo": 4600, "health": 175, "armour": 200, "par": 420},
    6: {"enemies": {"mutt": 14, "chomper": 12, "pom": 10, "dachshund": 6, "shield": 8,
                    "mastiff": 2},
        "hp": 2850, "ammo": 5700, "health": 200, "armour": 200, "par": 480},
    7: {"enemies": {"mutt": 16, "chomper": 14, "pom": 12, "dachshund": 8, "shield": 10,
                    "mastiff": 4},
        "hp": 4130, "ammo": 7400, "health": 250, "armour": 300, "par": 540},
    8: {"enemies": {"mutt": 8, "chomper": 8, "pom": 6, "dachshund": 4, "shield": 4,
                    "mastiff": 2, "woofu": 1},
        "hp": 4570, "ammo": 8200, "health": 300, "armour": 200, "par": 420},
}
TOLERANCE = 0.10
KITTEN_FEWER = (0.15, 0.35)      # Kitten: 15-35% fewer enemies than skill 2
SABRETOOTH_MORE = (0.10, 0.30)   # Sabretooth: 10-30% more


# ---- analysis --------------------------------------------------------------

def _supply():
    return {"health": 0, "armour": 0,
            "ammo": {a: 0 for a in AMMO_AVG}, "ammo_dmg": 0.0, "respawn_items": 0}


def _add_item(s, t, respawn=False):
    if respawn:
        s["respawn_items"] += 1
    if t in HEALTH_ITEMS:
        s["health"] += HEALTH_ITEMS[t]
    elif t in ARMOUR_ITEMS:
        s["armour"] += ARMOUR_ITEMS[t]
    elif t in AMMO_ITEMS:
        a, n = AMMO_ITEMS[t]
        s["ammo"][a] += n
    elif t == SATCHEL:
        for a, n in SATCHEL_AMMO.items():
            s["ammo"][a] += n


def _finish(s):
    s["ammo_dmg"] = round(sum(AMMO_AVG[a] * n for a, n in s["ammo"].items()), 1)


def reachable(m, start, cf):
    """Cells the player can walk to from `start` without pushing a pushwall:
    4-way flood through cells that are not SOLID, BLOCK_ALL or PUSHWALL.
    Doors (any kind, keyed included) are passable: keys are on the critical
    path. None when there is no player start."""
    if start is None:
        return None
    block = cf["SOLID"] | cf["BLOCK_ALL"] | cf["PUSHWALL"]
    flags = [[m.get_cell(x, y)["flags"] for x in range(m.width)] for y in range(m.height)]
    seen = set()
    todo = [start]
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or not (0 <= x < m.width and 0 <= y < m.height):
            continue
        if flags[y][x] & block:
            continue
        seen.add((x, y))
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return seen


def analyse(m):
    """Stats dict for a bast.Map."""
    ef = bast.entity_flags()
    cf = bast.cell_flags()
    secret_cell = cf["SECRET"] | cf["PUSHWALL"]
    secrets = 0
    for y in range(m.height):
        for x in range(m.width):
            if m.get_cell(x, y)["flags"] & cf["SECRET"]:
                secrets += 1

    out = {"name": m.name, "width": m.width, "height": m.height,
           "par_seconds": m.par_seconds, "secrets": secrets, "player_start": None,
           "keys": [], "skills": {}}
    ents = m.entities
    for e in ents:
        if e["type"] == PLAYER_START and out["player_start"] is None:
            out["player_start"] = {"x": e["x"] >> 16, "y": e["y"] >> 16, "angle": e["angle"]}
        if e["type"] in KEYS:
            out["keys"].append(KEYS[e["type"]])
    ps = out["player_start"]
    reach = reachable(m, (ps["x"], ps["y"]) if ps else None, cf)

    for skill in (1, 2, 3):
        bit = ef["SKILL%d" % skill]
        counts = {n: 0 for n in ENEMY_NAMES}
        hp = 0
        crit, secret, drops = _supply(), _supply(), _supply()
        for e in ents:
            if not e["flags"] & bit:
                continue
            t = e["type"]
            if t in ENEMIES:
                name, ehp = ENEMIES[t]
                counts[name] += 1
                hp += ehp
                if t in DROPS:
                    a, n = DROPS[t]
                    drops["ammo"][a] += n
                continue
            respawn = t == RESPAWN_ITEM
            if respawn:
                t = e["param"]
            if not (t in HEALTH_ITEMS or t in ARMOUR_ITEMS or t in AMMO_ITEMS or t == SATCHEL):
                continue
            cx, cy = e["x"] >> 16, e["y"] >> 16
            on_secret = (0 <= cx < m.width and 0 <= cy < m.height
                         and m.get_cell(cx, cy)["flags"] & secret_cell)
            hidden = reach is not None and (cx, cy) not in reach
            if e["flags"] & ef["SECRET_ONLY"] or on_secret or hidden:
                _add_item(secret, t, respawn)
            else:
                _add_item(crit, t, respawn)
        for s in (crit, secret, drops):
            _finish(s)
        total = sum(counts.values())
        ammo = round(crit["ammo_dmg"] + drops["ammo_dmg"], 1)
        out["skills"][skill] = {
            "enemies": counts, "enemy_total": total, "hp_pool": hp,
            "critical": crit, "drops": drops, "secret": secret,
            "ammo_supplied": ammo,
            "ratio": round(ammo / hp, 2) if hp else None,
        }
    return out


# ---- output ----------------------------------------------------------------

def _ammo_str(s):
    return " ".join("%s=%d" % (a, n) for a, n in s["ammo"].items())


def report(st, path):
    lines = ["%s: \"%s\" %dx%d par=%d:%02d secrets=%d keys=%s" % (
        path, st["name"], st["width"], st["height"], st["par_seconds"] // 60,
        st["par_seconds"] % 60, st["secrets"], ",".join(st["keys"]) or "-")]
    ps = st["player_start"]
    lines.append("player start: " + ("(%d, %d) angle %d" % (ps["x"], ps["y"], ps["angle"])
                                     if ps else "MISSING"))
    for skill in (1, 2, 3):
        s = st["skills"][skill]
        c, sec, d = s["critical"], s["secret"], s["drops"]
        lines.append("skill %d: enemies=%d %s hp_pool=%d" % (
            skill, s["enemy_total"],
            " ".join("%s=%d" % (n, s["enemies"][n]) for n in ENEMY_NAMES), s["hp_pool"]))
        flag = " (respawn_items=%d, counted once)" % c["respawn_items"] if c["respawn_items"] else ""
        lines.append("  critical: health=%d armour=%d ammo_dmg=%.1f [%s]%s" % (
            c["health"], c["armour"], c["ammo_dmg"], _ammo_str(c), flag))
        lines.append("  drops:    ammo_dmg=%.1f [%s]" % (d["ammo_dmg"], _ammo_str(d)))
        flag = " (respawn_items=%d, counted once)" % sec["respawn_items"] if sec["respawn_items"] else ""
        lines.append("  secret:   health=%d armour=%d ammo_dmg=%.1f [%s]%s" % (
            sec["health"], sec["armour"], sec["ammo_dmg"], _ammo_str(sec), flag))
        lines.append("  ammo supplied=%.1f ratio=%s" % (
            s["ammo_supplied"], "%.2f" % s["ratio"] if s["ratio"] is not None else "-"))
    return "\n".join(lines)


def _within(actual, expected, tol=TOLERANCE):
    return abs(actual - expected) <= tol * expected


def check(st, level):
    """List of (ok, name, actual, expected) for the levels.md row of `level`."""
    row = LEVELS[level]
    s2 = st["skills"][2]
    res = []
    for n in ENEMY_NAMES:
        exp = row["enemies"].get(n, 0)
        res.append((_within(s2["enemies"][n], exp), "enemies_" + n, s2["enemies"][n], exp))
    res.append((_within(s2["hp_pool"], row["hp"]), "hp_pool", s2["hp_pool"], row["hp"]))
    res.append((_within(s2["ammo_supplied"], row["ammo"]), "ammo", s2["ammo_supplied"],
                row["ammo"]))
    h, a = s2["critical"]["health"], s2["critical"]["armour"]
    res.append((_within(h, row["health"]), "health", h, row["health"]))
    res.append((_within(a, row["armour"]), "armour", a, row["armour"]))
    res.append((_within(st["par_seconds"], row["par"]), "par", st["par_seconds"], row["par"]))
    n2 = s2["enemy_total"]
    k, sab = st["skills"][1]["enemy_total"], st["skills"][3]["enemy_total"]
    lo, hi = KITTEN_FEWER
    res.append((n2 * (1 - hi) <= k <= n2 * (1 - lo), "kitten_enemies", k,
                "%g..%g" % (n2 * (1 - hi), n2 * (1 - lo))))
    lo, hi = SABRETOOTH_MORE
    res.append((n2 * (1 + lo) <= sab <= n2 * (1 + hi), "sabretooth_enemies", sab,
                "%g..%g" % (n2 * (1 + lo), n2 * (1 + hi))))
    return res


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("map")
    ap.add_argument("--level", type=int, help="levels.md row (default: levelN in the filename)")
    ap.add_argument("--check", action="store_true", help="compare skill 2 with levels.md")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return 1 if e.code else 0
    try:
        st = analyse(bast.Map.load(args.map))
    except (bast.BastError, OSError) as e:
        print("levelstats: %s: %s" % (args.map, e), file=sys.stderr)
        return 1

    results = None
    if args.check:
        level = args.level
        if level is None:
            mt = re.match(r"level(\d+)\.vmap$", os.path.basename(args.map))
            level = int(mt.group(1)) if mt else None
        if level not in LEVELS:
            print("levelstats: --check needs a level 1..8 (--level, or a levelN.vmap name)",
                  file=sys.stderr)
            return 1
        results = check(st, level)
        st["check"] = {"level": level, "results": [
            {"ok": ok, "name": n, "actual": a, "expected": e} for ok, n, a, e in results]}

    if args.json:
        print(json.dumps(st, indent=2, sort_keys=True))
    else:
        print(report(st, args.map))
        if results is not None:
            for ok, n, a, e in results:
                print("%s %s %s %s" % ("OK" if ok else "FAIL", n, a, e))
    if results is not None and not all(r[0] for r in results):
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
