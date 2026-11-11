#!/usr/bin/env python3
"""ctest `levelstats`: builds a small map through tools/bast.py with known
entities (skill flags, a SECRET_ONLY item, an item on a SECRET cell, an item
on a PUSHWALL cell, one in a sealed room behind a pushwall, one behind a door,
a RESPAWN_ITEM, a weapon, the Satchel) and asserts the
exact numbers tools/levelstats.py reports, then that --check returns 2 on a
mismatch and 0 on a map built to match the levels.md row for level 1.
Plain asserts. BAST_LIB overrides the libbast.so path.
"""
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402

TOOL = os.path.join(TOOLS, "levelstats.py")
U = 1 << 16


def at(x, y):
    return {"x": x * U + U // 2, "y": y * U + U // 2}


def build_map(path):
    cf, ef = bast.cell_flags(), bast.entity_flags()
    S1, S2, S3 = ef["SKILL1"], ef["SKILL2"], ef["SKILL3"]
    m = bast.Map(16, 8)
    m.name = "Stats Test"
    m.par_seconds = 150
    m.set_cell(10, 5, flags=cf["SECRET"])
    m.set_cell(11, 5, flags=cf["SECRET"])
    m.set_cell(12, 5, flags=cf["PUSHWALL"])
    # East rooms: column x=13 is wall with a door at (13, 2) and a pushwall
    # at (13, 6); row y=4 splits x=14..15 into a door room (north) and a
    # sealed secret room (south) whose cells carry no SECRET flag.
    for y in range(8):
        m.set_cell(13, y, flags=cf["SOLID"])
    m.set_cell(13, 2, flags=cf["DOOR"])
    m.set_cell(13, 6, flags=cf["PUSHWALL"])
    m.set_cell(14, 4, flags=cf["SOLID"])
    m.set_cell(15, 4, flags=cf["SOLID"])
    m.add_entity("PLAYER_START", angle=16384, **at(2, 3))
    # Enemies: 4 Mutts on all skills, 1 Mutt skill 2+3, 1 Chomper skill 3 only,
    # 1 Pom on all skills, 1 Woofu skill 2 only.
    for i in range(4):
        m.add_entity("MUTT_TROOPER", flags=S1 | S2 | S3, **at(3 + i, 1))
    m.add_entity("MUTT_TROOPER", flags=S2 | S3, **at(8, 1))
    m.add_entity("CHIHUAHUA_CHOMPER", flags=S3, **at(9, 1))
    m.add_entity("POMERANIAN_PLASMAGUNNER", flags=S1 | S2 | S3, **at(10, 1))
    m.add_entity("WOOFU", flags=S2, **at(12, 2))
    # Critical path items (all skills unless noted).
    m.add_entity("FISH_SNACK", **at(3, 3))                    # +10 health
    m.add_entity("TUNA_TIN", flags=S1 | S2, **at(4, 3))       # +25, not on skill 3
    m.add_entity("LINEN_WRAP", **at(5, 3))                    # 100 armour
    m.add_entity("CHARGE_VIAL", **at(6, 3))                   # 10 charges
    m.add_entity("HAIRBALL_SCATTERGUN", **at(7, 3))           # 8 shells bundled
    m.add_entity("LITTER_SATCHEL", **at(8, 3))                # 10/4/1/20
    m.add_entity("RESPAWN_ITEM", param=bast.entity_type("SUN_SHARD"), **at(9, 3))  # 20 cells once
    m.add_entity("GREEN_ANKH", **at(10, 3))
    m.add_entity("CATNIP_FRENZY", **at(11, 3))                # ignored by supply
    # Secret supply: SECRET_ONLY flag, on a SECRET cell, on a PUSHWALL cell.
    m.add_entity("SCARAB_PLATE", flags=S1 | S2 | S3 | ef["SECRET_ONLY"], **at(4, 5))
    m.add_entity("GOLDEN_SARDINE", **at(10, 5))
    m.add_entity("CHARGE_JAR", **at(12, 5))
    m.add_entity("TUNA_TIN", **at(14, 1))       # behind a door: critical path
    m.add_entity("YARN_BUNDLE", **at(15, 6))    # sealed behind a pushwall: secret
    m.save(path)


def run(*args):
    p = subprocess.run([sys.executable, TOOL] + list(args), capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def test_report(path):
    rc, out, err = run(path, "--json")
    assert rc == 0, (rc, out, err)
    st = json.loads(out)
    assert (st["width"], st["height"], st["par_seconds"]) == (16, 8, 150)
    assert st["secrets"] == 2, st["secrets"]
    assert st["keys"] == ["green"]
    assert st["player_start"] == {"x": 2, "y": 3, "angle": 16384}
    s1, s2, s3 = (st["skills"][k] for k in ("1", "2", "3"))

    assert s1["enemy_total"] == 5 and s1["enemies"]["mutt"] == 4 and s1["enemies"]["pom"] == 1
    assert s1["hp_pool"] == 4 * 20 + 45
    assert s2["enemy_total"] == 7 and s2["enemies"]["mutt"] == 5 and s2["enemies"]["woofu"] == 1
    assert s2["enemies"]["chomper"] == 0
    assert s2["hp_pool"] == 5 * 20 + 45 + 2400
    assert s3["enemy_total"] == 7 and s3["enemies"]["chomper"] == 1 and s3["enemies"]["woofu"] == 0
    assert s3["hp_pool"] == 5 * 20 + 15 + 45

    c2 = s2["critical"]
    assert c2["health"] == 60 and c2["armour"] == 100, c2
    assert c2["ammo"] == {"charges": 20, "shells": 12, "scarabs": 1, "cells": 40}, c2["ammo"]
    assert c2["respawn_items"] == 1
    crit_dmg = 20 * 11.5 + 12 * 49 + 75 + 40 * 24
    assert c2["ammo_dmg"] == crit_dmg, c2["ammo_dmg"]
    assert s3["critical"]["health"] == 35
    assert s2["drops"]["ammo"]["charges"] == 25 and s1["drops"]["ammo"]["charges"] == 20
    assert s2["ammo_supplied"] == crit_dmg + 25 * 11.5, s2["ammo_supplied"]
    assert s2["ratio"] == round((crit_dmg + 25 * 11.5) / 2545, 2)

    sec = s2["secret"]
    assert sec["health"] == 100 and sec["armour"] == 200, sec
    assert sec["ammo"]["charges"] == 50 and sec["ammo"]["shells"] == 4, sec
    assert sec["ammo_dmg"] == 50 * 11.5 + 4 * 49, sec

    # Text report exits 0 and flags the respawn item.
    rc, out, err = run(path)
    assert rc == 0, (rc, out, err)
    assert "skill 2: enemies=7 mutt=5" in out and "hp_pool=2545" in out, out
    assert "respawn_items=1, counted once" in out, out


def test_check_mismatch(path):
    rc, out, err = run(path, "--check", "--level", "1")
    assert rc == 2, (rc, out, err)
    assert "FAIL enemies_mutt 5 14" in out, out
    assert "FAIL enemies_woofu 1 0" in out, out
    assert "OK par 150 150" in out, out
    # No level from the file name and no --level: argument error.
    rc, out, err = run(path, "--check")
    assert rc == 1, (rc, out, err)
    rc, out, err = run(path + ".missing")
    assert rc == 1, (rc, out, err)


def test_check_pass(tmp):
    """A map that matches levels.md row 1: 14 Mutts (skill 2), 10 on Kitten,
    17 on Sabretooth; 60 health, 0 armour; 3 vials + 14 drops = 1150."""
    ef = bast.entity_flags()
    S1, S2, S3 = ef["SKILL1"], ef["SKILL2"], ef["SKILL3"]
    m = bast.Map(24, 24)
    m.par_seconds = 150
    m.add_entity("PLAYER_START", **at(1, 1))
    for i in range(17):
        fl = S3 | (S2 if i < 14 else 0) | (S1 if i < 10 else 0)
        m.add_entity("MUTT_TROOPER", flags=fl, **at(2 + i, 10))
    for i in range(3):
        m.add_entity("CHARGE_VIAL", **at(2 + i, 5))
    for i in range(6):
        m.add_entity("FISH_SNACK", **at(2 + i, 6))
    path = os.path.join(tmp, "level1.vmap")
    m.save(path)
    rc, out, err = run(path, "--check")
    assert rc == 0, (rc, out, err)
    assert "OK ammo 1150.0 1150" in out and "OK kitten_enemies 10" in out, out
    assert "FAIL" not in out, out


def main():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "stats.vmap")
        build_map(path)
        test_report(path)
        test_check_mismatch(path)
        test_check_pass(tmp)
    print("levelstats: ok")


if __name__ == "__main__":
    main()
