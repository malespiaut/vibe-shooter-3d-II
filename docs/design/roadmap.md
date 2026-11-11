# Roadmap

Milestones from `CLAUDE.md`. Each issue fits one 60-minute agent run. Exit criteria are verified
by the planning designer before closing a milestone (commands below, real output posted).

## M1 Engine core

Tooling first, then the renderer. Exit criteria:
- Clean build with `-std=c17 -Wall -Wextra -Werror` (gcc and clang), `ctest` green.
- `tools/palcheck.py` passes on the palette and every committed indexed asset; colormap and TEX
  round-trip tests pass; `tools/bast.py` loads `libbast.so`.
- `build/game --headless --demo demos/m1-walk.demo --frames-out /tmp/f` writes PNGs showing a
  textured, distance-shaded test map with floors, ceilings and billboard sprites at 320x240, and
  prints the stats line.
- `ctest -R determinism` passes (two replays, identical per-tic hashes).
- The game window opens, renders the test map at an integer scale and the player walks with
  keyboard and mouse.

## M2 Asset pipeline & editor

- `tools/quantize.py` (RGBA PNG -> TEX through libbast), `tools/texgen.py` (procedural textures,
  GAME.md 6.1) with the full EG and DT wall/floor sets (about 24 wall, 10 floor/ceiling textures) as
  parameter files, `tools/blender_render.py` (8-direction enemy rendering, GAME.md 4.1), `tools/gibs.py`
  (6.2), `tools/fx.py` (6.3), `tools/sfx.py`, `tools/pak.py`.
- `tools/build_assets.sh` regenerates every asset into `assets/gen/` from a fresh clone.
- Editor MVP (`editor/`, wxPython, MVP pattern, model through `tools/bast.py`): paint cells, light,
  flags and properties; place and edit entities; save/load `.vmap`; launch the level in the game.
- Exit: fresh clone -> `tools/build_assets.sh` -> palcheck clean on all of `assets/gen/`; editor
  round-trips a map byte-identically (`ctest -R editor_roundtrip`); "launch" runs the game on it.

## M3 Gameplay systems

Player combat and weapons 1-7, all enemy AI and the boss, doors (all kinds), keys, switches, tags,
pushwalls, triggers (kill, pickup, lights), damage floors, barrels, items and powerups, HUD and
status face, menus and the whole flow (title to credits), intermission tally, skill levels,
death/restart, mixer and sound effects, music sequencer, high-level demo commands that need enemies
(`face nearest`, `fire nearest`, `clear`).
- Exit: a `demos/m3-arena.demo` on a test map exercises every weapon and enemy without `--god` and
  passes its `expect` lines; determinism test covers it; the flow demo `demos/flow.demo` goes title ->
  level 1 start -> pause -> quit to title in headless mode with frames attached.

## M4 Content: levels 1-8

One issue per level (or two for levels 5-8: layout, then population), plus the art for each
enemy, prop set and texture set as it is needed (art issues land before the level that introduces
them).
- Every level issue ships `maps/levelN.vmap` and `demos/levelN-complete.demo`, registered in
  `ctest` as `complete_levelN`.
- Exit: `ctest -R complete_level` passes for all 8, the playtester has played every level, counts
  and par match `levels.md` within 10%.

## M5 Polish & audio

All sound effects and the 9 music tracks, title/intermission/ending art, the cat face set, balance
passes against the difficulty table, playtest bugs. Exit: no open `bug`; every M5 issue from this
section filed and closed.

## Episode 1

`Final acceptance` issue (no developer; the playtester verifies). Checklist: every level completed
by its demo via `ctest`; full flow title -> credits works; `ctest` passes from a clean build;
palcheck passes on every asset; a fresh clone builds and regenerates every asset with the documented
commands.

---

## Verification tooling contract

These names are fixed. Every issue, PR, review and playtest uses them exactly.

| Purpose | Command |
|---|---|
| Configure + build | `cmake -S . -B build -G Ninja && cmake --build build` |
| Unit and integration tests | `ctest --test-dir build --output-on-failure` |
| Game binary | `build/game` (window mode by default) |
| Headless demo with frames | `build/game --headless --demo <file.demo> --frames-out <dir> [--frame-every N]` |
| Record a demo while playing | `build/game --record <file.demo> [--warp N] [--skill N]` |
| Dev flags | `--warp <level>`, `--skill <1-3>`, `--god`, `--map <file.vmap>`, `--max-tics N`, `--topdown` (dev overhead view), `--hash-log <file>` (one hex hash per tic), `--sound-log <file>` (one line per sound event: `tic name x y`, `- -` for local player sounds; `tic music <name>` when the music changes, `music -` on stop) |
| Stats line (stdout, last line of every headless run) | `STATS level=1 tic=5250 hash=0123456789abcdef health=87 armor=0 kills=12/14 items=5/9 secrets=1/2 exit=1 god=0` |
| Exit codes | 0 run finished, 1 error (bad file/args), 2 an `expect` failed, a high-level command timed out, or the player died (headless, unless the demo says `on_death restart`) |
| Palette check | `python3 tools/palcheck.py <files or dirs>` (exit 1 on any off-palette pixel, non-indexed PNG, or TEX index that is reserved) |
| Colormap | `python3 tools/colormap.py` -> `assets/gen/colormap.bin` (also built by CMake) |
| Palette build | `python3 tools/palette.py` -> `assets/gen/palette.bin`, `palette_flags.bin` (also built by CMake) |
| Python bindings | `tools/bast.py` (ctypes over `build/libbast.so`; `BAST_LIB` env var overrides the path) |
| Test map from ASCII | `python3 tools/mkmap.py maps/src/<name>.txt maps/<name>.vmap` |
| Determinism | `ctest --test-dir build -R determinism` (each registered demo replayed twice with `--hash-log`, logs compared byte for byte) |
| Level completion | `ctest --test-dir build -R complete_level` (`complete_levelN` runs `build/game --headless --demo demos/levelN-complete.demo --frames-out <tmp> --frame-every 35` and checks exit code 0 and `exit=1 god=0` in the stats line) |
| Level stats | `python3 tools/levelstats.py maps/levelN.vmap [--level N] [--check] [--json]` (per-skill enemy counts, HP pool, critical-path/secret supply, ratio; `--check` compares skill 2 with `levels.md` within 10%, Kitten 15-35% fewer and Sabretooth 10-30% more enemies; exit 2 on a failed check). ctest `levelstats_levelN` runs `--check` on every `maps/level[1-8].vmap` |
| Contact sheet for evidence | `magick montage <dir>/*.png -filter point -tile 6x -geometry +2+2 sheet.png` |

Frames are named `frame_<tic, 6 digits>.png`, 320x240 indexed PNG written through the palette by
the engine itself (small built-in PNG writer, stored deflate blocks are fine).

### Completion demo rules

- One per level: `demos/levelN-complete.demo`, header `level N`, `skill 2`, `seed 0`.
- No `--god`, noclip or invulnerability; the test checks `god=0`.
- Use high-level commands (`goto @waypoint`, `clear`, `fire nearest`, `use`) so balance and AI tuning
  do not desync it. Raw `in` lines only for short precise moves.
- Ends with `expect exit=1`. A tuning PR that breaks any completion demo must fix it in the same PR.

### Determinism rules

Registered demos: `m1-walk` (M1), `m3-arena` (M3), every `levelN-complete` (M4). The determinism test
must pass on gcc and clang builds.
