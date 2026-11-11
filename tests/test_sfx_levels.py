#!/usr/bin/env python3
"""ctest `sfx_levels` (issue #51): the sound-effect mix baked by tools/sfx.py.

Renders every assets/src/sfx/*.json, prints the effective RMS / peak dBFS of
each (the same table as `tools/sfx.py --report`) and asserts the rules of
docs/design/audio.md, "Mix levels":
  - every effect is in the table exactly once and its level_db matches it;
  - no peak above -1.2 dBFS (PEAK = 112);
  - an effect lands within 1 dB of its target unless peak-limited, and only
    Explosions may be peak-limited;
  - barrel_explode is the loudest World effect;
  - menu_move and tally_tick are at least 6 dB below item_pickup;
  - every enemy windup is within 1 dB of the others.
Plain asserts.
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import sfx  # noqa: E402

PARAMS = sorted(glob.glob(os.path.join(ROOT, "assets", "src", "sfx", "*.json")))
AUDIO_MD = os.path.join(ROOT, "docs", "design", "audio.md")


def md_tables():
    """(mix group -> (level, [effects]), source group -> [effects])."""
    with open(AUDIO_MD) as f:
        text = f.read()
    se = text[text.index("## Sound effects"):text.index("### Mix levels")]
    ml = text[text.index("### Mix levels"):text.index("## Music")]
    source = {}
    for line in se.splitlines():
        m = re.match(r"\| (\w[^|]*?) \| (`.+) \|$", line)
        if m:
            source[m.group(1)] = re.findall(r"`(\w+)`", m.group(2))
    mix = {}
    for line in ml.splitlines():
        m = re.match(r"\| (\w[^|]*?) \| (-?\d+) \| (.+) \|$", line)
        if m:
            mix[m.group(1)] = (int(m.group(2)), re.findall(r"`(\w+)`", m.group(3)))
    assert mix and source, "audio.md tables not found"
    return mix, source


def main():
    mix, source = md_tables()
    target, group = {}, {}
    for g, (lvl, effects) in mix.items():
        for e in effects:
            assert e not in target, ("listed twice in Mix levels", e)
            target[e], group[e] = lvl, g
    names = [os.path.basename(p)[:-5] for p in PARAMS]
    assert sorted(target) == sorted(names), (
        "Mix levels vs assets/src/sfx", sorted(set(target) ^ set(names)))
    assert sorted(sum(source.values(), [])) == sorted(names), "Sound effects table"

    res = {}
    print("%-16s %-28s %7s %7s %7s" % ("effect", "mix group", "target", "rms", "peak"))
    for p, n in zip(PARAMS, names):
        with open(p) as f:
            params = json.load(f)
        assert params.get("level_db") == target[n], (n, params.get("level_db"), target[n])
        pcm = sfx.synth(params)
        rms, peak, limited = sfx.measure(pcm)
        res[n] = rms
        print("%-16s %-28s %7.1f %7.1f %7.1f%s" % (n, group[n], target[n], rms, peak,
                                                   "  peak-limited" if limited else ""))
        assert peak <= sfx.db(sfx.PEAK / sfx.FULL_SCALE) + 1e-9, (n, peak)
        if limited:
            assert group[n] == "Explosions", ("only Explosions may be peak-limited", n, rms)
            assert rms < target[n] + 1.0, (n, rms)
        else:
            assert abs(rms - target[n]) <= 1.0, (n, rms, target[n])

    world = {e: res[e] for e in source["World"]}
    loudest = max(world, key=world.get)
    assert loudest == "barrel_explode", ("loudest World effect", loudest, world[loudest])
    for ui in ("menu_move", "tally_tick"):
        assert res[ui] <= res["item_pickup"] - 6.0, (ui, res[ui], res["item_pickup"])
    windups = {e: res[e] for e in names if e.endswith("_windup")}
    assert len(windups) >= 5, windups
    spread = max(windups.values()) - min(windups.values())
    assert spread <= 1.0, ("windup spread", windups)
    print("sfx_levels: ok (%d effects, %.1f dB spread, windups within %.1f dB, "
          "barrel_explode %.1f vs loudest other World %.1f, menu_move %.1f / tally_tick %.1f "
          "vs item_pickup %.1f)" % (
              len(names), max(res.values()) - min(res.values()), spread, res["barrel_explode"],
              max(v for k, v in world.items() if k != "barrel_explode"),
              res["menu_move"], res["tally_tick"], res["item_pickup"]))


if __name__ == "__main__":
    main()
