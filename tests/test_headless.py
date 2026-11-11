#!/usr/bin/env python3
"""ctest `headless_smoke`: build/game --headless (roadmap.md Verification
tooling contract).

- a 70-tic raw demo with --frames-out writes exactly 70 frames, named
  frame_000001.png .. frame_000070.png, 320x240 mode P, and the last stdout
  line is a STATS line matching the contract format; --hash-log has one hash
  per tic and its last line is the STATS hash;
- --frame-every 35 writes frames 35 and 70 only;
- a demo with an unknown command exits 1 naming the line;
- --warp to a level that does not exist exits 1.
  test_headless.py <game exe> <tmp dir>
"""
import os
import re
import shutil
import subprocess
import sys

from PIL import Image

game, tmp = sys.argv[1:3]
shutil.rmtree(tmp, ignore_errors=True)
os.makedirs(tmp)

STATS_RE = re.compile(
    r"^STATS level=\d+ tic=\d+ hash=[0-9a-f]{16} health=-?\d+ armor=\d+ "
    r"kills=\d+/\d+ items=\d+/\d+ secrets=\d+/\d+ exit=[01] god=[01]$")

demo = os.path.join(tmp, "smoke.demo")
with open(demo, "w") as f:
    f.write("vsdemo 1\nlevel 1\nskill 2\nseed 0\nmap m1test\n"
            "wait 5\nin 30 127 0 0 R\nin 20 0 64 -936 F\nwait 15\n")

def run(*args):
    return subprocess.run([game, "--headless", *args], capture_output=True, text=True)

frames = os.path.join(tmp, "frames")
hashes = os.path.join(tmp, "hash.log")
p = run("--demo", demo, "--frames-out", frames, "--hash-log", hashes)
assert p.returncode == 0, (p.returncode, p.stderr)
last = p.stdout.strip().splitlines()[-1]
assert STATS_RE.match(last), last
assert " tic=70 " in last and " level=1 " in last and " god=0" in last, last
names = sorted(os.listdir(frames))
assert names == [f"frame_{t:06d}.png" for t in range(1, 71)], names[:3]
for n in (names[0], names[-1]):
    im = Image.open(os.path.join(frames, n))
    assert im.mode == "P" and im.size == (320, 240), (n, im.mode, im.size)
log = open(hashes).read().split()
assert len(log) == 70 and all(re.fullmatch(r"[0-9a-f]{16}", h) for h in log)
assert last.split("hash=")[1].split()[0] == log[-1]

# Same demo again: same final state.
p2 = run("--demo", demo, "--god")
assert p2.returncode == 0
last2 = p2.stdout.strip().splitlines()[-1]
assert last2.replace("god=1", "god=0") == last, (last2, last)

every = os.path.join(tmp, "every")
p = run("--demo", demo, "--frames-out", every, "--frame-every", "35")
assert p.returncode == 0, p.stderr
assert sorted(os.listdir(every)) == ["frame_000035.png", "frame_000070.png"]

# --max-tics stops early.
p = run("--demo", demo, "--max-tics", "12")
assert p.returncode == 0 and " tic=12 " in p.stdout.splitlines()[-1], p.stdout

bad = os.path.join(tmp, "bad.demo")
with open(bad, "w") as f:
    f.write("vsdemo 1\nmap m1test\nwait 3\nteleport 4 4\n")
p = run("--demo", bad)
assert p.returncode == 1, p.returncode
assert "line 4" in p.stderr and "teleport" in p.stderr, p.stderr
assert "STATS" not in p.stdout

p = run("--demo", demo, "--warp", "98")
assert p.returncode == 1 and "level98.vmap" in p.stderr, (p.returncode, p.stderr)

print("headless_smoke: OK;", last)
