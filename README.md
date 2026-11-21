# Vibe Shooter 3D II: Paws of Fury

A retro first-person shooter set in **Catlantis**, a sun-baked parody of ancient Egypt run by cats.
The cybernetic dog-men of the **Kennel Dominion** occupied the city overnight, and their overlord
Pharaoh-Overlord Woofu wants to turn the Great Sphinx into the Great Sphound. You play Sergeant
Mau of the Royal Whisker Guard, the one guard who was napping in a sunbeam during the invasion.
Eight levels of fast keys-and-doors shooting take Mau from the sandstone city gates, through museums,
docks, tombs and dog-food refineries, to Woofu's throne.

It renders at 320x240 with a 256-colour palette in software. The code is C17 with SDL3 as the
platform layer only (window, input, audio buffer, files). The renderer, mixer, file formats and game
are written from scratch. `GAME.md` is the specification, and `docs/design/` holds the design
documents (pitch, story, levels, enemies, weapons, items, HUD, audio, art bible, tech, roadmap).

## Dependencies

- A C17 compiler (gcc or clang), CMake, Ninja, pkg-config
- SDL3 (development package, found through CMake `find_package(SDL3 CONFIG)`)
- Python 3 with Pillow and numpy (asset pipeline, `tools/`)
- Blender (`blender -b`): `tools/build_assets.sh` uses it to render the pre-rendered enemy sprites
  (`tools/build.d/60-sprites.sh`) and the 3D props (`tools/build.d/48-props.sh`). It is required
  to regenerate the assets.
- wxPython, only for the map editor (`editor/`)

## Build and run

From the repository root, in this order:

```
cmake -S . -B build -G Ninja && cmake --build build   # game, libbast.so, tests
tools/build_assets.sh                                 # every asset into assets/gen/ (not committed)
python3 tools/palcheck.py assets/gen                  # palette check, must end "palcheck: OK"
ctest --test-dir build --output-on-failure            # unit, integration, determinism tests
```

Run `tools/build_assets.sh` before `ctest`: some tests load the generated assets.
`tools/README.md` lists every asset step.

Play:

```
build/game                                   # window mode: title screen, then the menus
build/game --map maps/m1test.vmap --skill 3  # straight into any map file; skill 1-3 (default 2)
build/game --warp 1 --skill 2                # straight into level N, maps/levelN.vmap (1-8)
```

With no `--map`, `--warp` or `--record`, the game opens on the title screen. Any key leads to the
main menu (New Game, Controls, Quit). New Game asks for the skill, then starts play on the M1 test
map `maps/m1test.vmap` as level 1 (the default map when no flag picks one).

`--warp N` loads `maps/levelN.vmap`. Levels 1, 2, 5, 6 and 7 exist today. The others are
milestone M4 work and exit with `game: .../maps/levelN.vmap: No such file or directory` (exit code
1) until they land.

Headless demo playback, with one 320x240 indexed PNG per 35 tics:

```
build/game --headless --demo demos/m1-walk.demo --frames-out /tmp/frames --frame-every 35
```

The last stdout line is the `STATS ...` line. All flags, exit codes and the other verification
commands are listed in `docs/design/roadmap.md`, under "Verification tooling contract".

To check that a fresh clone builds with the commands above, run `tools/check_fresh_clone.sh
[<repo-url-or-path>] [<workdir>]`. It clones with `--no-local` into a temporary directory, runs the
four commands in order and ends with `FRESH CLONE OK`. It takes several minutes, mostly because of
Blender.

Map editor: `python3 -m editor [maps/<name>.vmap]` (needs `build/libbast.so`).

## Controls

Window mode (`src/main.c`, `docs/design/hud.md`):

| Action | Keys |
|---|---|
| Move | W/S or up/down arrows; strafe A/D |
| Turn | mouse, or left/right arrows |
| Fire | left mouse button, or Ctrl |
| Weapon | 1-7 |
| Use (doors, switches; restart after death) | E or Space |
| Run | Shift |
| Pause menu (Resume, Restart Level, Controls, Quit to Title) | Esc |
| Quit | close the window, or Quit from the main menu |

In the menus, up/down (or W/S) move the cursor, Enter or Space selects and Esc goes back.

## Layout

```
src/engine/ src/game/ src/platform/   C17 sources (SDL3 only in src/platform/)
src/bast/                             libbast.so: shared formats for the Python tools
tools/                                Python asset pipeline + ctypes wrapper (tools/bast.py)
editor/                               wxPython map editor
assets/src/                           palette, source art, .blend, generator parameters
assets/gen/                           generated assets (ignored, rebuilt by tools/build_assets.sh)
maps/  tests/  demos/  docs/design/
agents/roles/*.md                     role prompts for the agent team
```

Design documents: [`docs/design/`](docs/design/). Team rules: [`CLAUDE.md`](CLAUDE.md).
