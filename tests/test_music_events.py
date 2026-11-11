#!/usr/bin/env python3
"""ctest `music_events` (issue #132): the level track starts at tic 0.

- `--headless --warp 1` and `--warp 3` with --sound-log log `0 music
  level_eg` / `0 music level_docks` as their first line, once;
- a track that is not composed yet prints `music: <name> missing` once on
  stderr and the run still exits 0;
- music is presentation only: the STATS hash is the same with and without
  --sound-log, and a demo replays to the same hash;
- a death and restart (death-test.demo) starts the track over;
- with a display (xvfb), window mode starts and runs 70 tics with no audio
  device (SDL_AUDIO_DRIVER set to a driver that does not exist).

No levelN.vmap exists yet, so --map points at the M1 test map; --warp still
sets the level number, which picks the track (audio.md, Music).
  test_music_events.py <game exe> <source dir> <tmp dir>
"""
import os
import shutil
import subprocess
import sys

game, src, tmp = sys.argv[1:4]
shutil.rmtree(tmp, ignore_errors=True)
os.makedirs(tmp)
MAP = os.path.join(src, "maps", "m1test.vmap")
MUSIC = os.path.join(src, "assets", "src", "music")


def run(args, env=None):
    return subprocess.run([game, *args], capture_output=True, text=True, env=env, timeout=60)


def stats_hash(out):
    last = out.strip().splitlines()[-1]
    assert last.startswith("STATS "), last
    return last.split("hash=")[1].split()[0]


for level, track in ((1, "level_eg"), (3, "level_docks")):
    log = os.path.join(tmp, f"warp{level}.log")
    base = ["--headless", "--warp", str(level), "--map", MAP, "--max-tics", "70"]
    p = run(base + ["--sound-log", log])
    assert p.returncode == 0, (p.returncode, p.stderr)
    lines = open(log).read().splitlines()
    music = [l for l in lines if l.split()[1] == "music"]
    assert lines and lines[0] == f"0 music {track}", lines[:3]
    assert music == [f"0 music {track}"], music
    composed = os.path.exists(os.path.join(MUSIC, track + ".vmus"))
    assert composed, f"{track}.vmus is composed (#163) and must be found"
    missing = p.stderr.count(f"music: {track} missing")
    assert missing == (0 if composed else 1), p.stderr
    q = run(base)
    assert q.returncode == 0, q.stderr
    assert stats_hash(q.stdout) == stats_hash(p.stdout), "music changed the game hash"
    print(f"ok: warp {level}: {lines[0]!r}, composed={composed}, hash {stats_hash(p.stdout)}")

# Demo replayed twice with --sound-log: same hash, same log.
demo = os.path.join(tmp, "walk.demo")
with open(demo, "w") as f:
    f.write("vsdemo 1\nlevel 1\nskill 2\nseed 0\nmap m1test\n"
            "wait 5\nin 30 127 0 0 R\nin 20 0 64 -936 F\nwait 15\n")
runs = []
for i in range(2):
    log = os.path.join(tmp, f"demo{i}.log")
    p = run(["--headless", "--demo", demo, "--sound-log", log])
    assert p.returncode == 0, p.stderr
    runs.append((stats_hash(p.stdout), open(log).read()))
assert runs[0] == runs[1]
assert runs[0][1].splitlines()[0] == "0 music level_eg"
print("ok: demo replays to the same hash and sound log")

# Death and restart (demos/death-test.demo, on_death restart): the track
# starts over. The log's tic column is the level tic, which a restart rewinds
# to 0, so the restart line is `0 music level_eg` again, written between the
# first life's last event and the second life's first. The hash log has one
# line per tic run, so it gives the restart's run tic: tics run minus the
# level tic in STATS.
log = os.path.join(tmp, "death.log")
hlog = os.path.join(tmp, "death.hash")
p = run(["--headless", "--demo", os.path.join(src, "demos", "death-test.demo"),
         "--sound-log", log, "--hash-log", hlog])
assert p.returncode == 0, (p.returncode, p.stderr)
assert "missing" not in p.stderr, p.stderr
lines = open(log).read().splitlines()
music = [i for i, l in enumerate(lines) if l.split()[1] == "music"]
assert [lines[i] for i in music] == ["0 music level_eg"] * 2, [lines[i] for i in music]
first, second = music
assert first == 0 and second > 1, music
level_tic = int(p.stdout.strip().splitlines()[-1].split(" tic=")[1].split()[0])
restart_at = len(open(hlog).read().splitlines()) - level_tic
before = [int(l.split()[0]) for l in lines[1:second]]
after = [int(l.split()[0]) for l in lines[second + 1:]]
assert before and max(before) < restart_at and before[-1] > 0, before[-3:]
assert all(t < level_tic for t in after), after
print(f"ok: restart at run tic {restart_at}: second '{lines[second]}' "
      f"after first-life tic {before[-1]}, {len(after)} events after it")

if os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
    env = dict(os.environ, SDL_AUDIO_DRIVER="vs3d_no_such_driver")
    log = os.path.join(tmp, "window.log")
    p = run(["--warp", "1", "--map", MAP, "--max-tics", "70", "--sound-log", log], env)
    assert p.returncode == 0, (p.returncode, p.stderr)
    assert "audio disabled" in p.stderr, p.stderr
    assert " tic=70 " in p.stdout, p.stdout
    assert open(log).read().splitlines()[0] == "0 music level_eg"
    print("ok: window mode without an audio device runs 70 tics")
else:
    print("skip: no display for the window-mode check")
