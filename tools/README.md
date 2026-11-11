# tools/

Python asset pipeline. Every shared format (palette, Oklab nearest colour, TEX, SND, .vmap) is
implemented once, in C, in `libbast.so` (`src/bast/`); the scripts here reach it only through the
ctypes wrapper `tools/bast.py`. No colour or format maths in Python. Python dependencies: Pillow
and numpy only.

Build `libbast.so` first: `cmake -S . -B build -G Ninja && cmake --build build`.
`BAST_LIB` overrides the library path (default `build/libbast.so`), `BAST_PALETTE` the palette
(default `assets/gen/palette.bin`).

## Regenerate every asset: `build_assets.sh`

```
git clean -xdf assets/gen
cmake -S . -B build -G Ninja && cmake --build build
tools/build_assets.sh
```

Runs, in order, stopping at the first failure with its exit code:

1. `tools/palette.py`: `assets/src/palette/catlantis.gpl` -> `assets/gen/palette.bin` + `palette_flags.bin`.
2. `tools/colormap.py`: -> `assets/gen/colormap.bin`.
3. every `tools/build.d/*.sh`, in sorted (`NN-` prefix) order, each run with `sh`.
4. `python3 tools/palcheck.py assets/gen`: must end `palcheck: OK`.

Ends with `build_assets: ok`. Everything it writes lives under `assets/gen/`, which is not
committed.

### Adding an asset tool

Do not edit `build_assets.sh`. Add `tools/build.d/NN-<tool>.sh` (executable, `#!/bin/sh`,
`set -eu`, `cd "$(dirname "$0")/../.."` to start at the repo root). It must regenerate its whole
output directory under `assets/gen/` from `assets/src/` (remove it first, so a deleted source leaves
no stale file) and print a one-line summary. Pick `NN` after the steps it depends on.
`BAST_LIB` and `BAST_PALETTE` are already exported. Existing steps:

| Step | Output |
|------|--------|
| `20-texgen.sh` | `assets/gen/textures/*.tex` from `assets/src/textures/*.json` (`tools/texgen.py`) |
| `30-sfx.sh` | `assets/gen/sfx/*.snd` from `assets/src/sfx/*.json` |

## Quantize RGBA to TEX: `quantize.py`

The RGBA -> TEX step every art tool uses (GAME.md 4.2). Each pixel goes through
`bast_tex_from_rgba()`: alpha < 128 becomes index 255 and sets the TEX `has_transparency` flag;
any other pixel gets the Oklab-nearest palette entry (reserved, fullbright and transparent entries
are never chosen; no dithering). The input is always converted to RGBA first, so the result
depends only on the pixel colours.

```
python3 tools/quantize.py in.png -o out.tex [--origin X,Y | --origin bottom-centre] \
                          [--fullbright] [--preview preview.png] [-q]
python3 tools/quantize.py a.png b.png ... -o outdir/ [--preview previewdir/]
```

- With several inputs, or when `-o` is an existing directory or ends with `/`, each `<name>.png`
  becomes `<outdir>/<name>.tex`, and `--preview` is a directory of `<name>.png`.
- `--origin X,Y`: sprite anchor in px from the left / top edge (default `0,0`).
  `--origin bottom-centre` is `(width // 2, height)`: the floor contact point of a world sprite
  (`docs/design/tech.md`, Sprites).
- `--fullbright`: sets the TEX fullbright flag (the renderer skips the colormap).
- `--preview`: writes the result as an indexed PNG carrying the game palette (index 255 marked
  transparent), for review and palcheck.

From Python:

```python
import quantize
data = quantize.quantize_rgba(rgba, w, h, origin="bottom-centre", fullbright=False)  # TEX bytes
t = quantize.quantize_tex(rgba, w, h, (0, 0))   # bast.Tex, if you want to keep working on it
```

`rgba` is row-major RGBA8 (`Image.convert("RGBA").tobytes()`).

## Other tools

| Tool | Purpose |
|------|---------|
| `bast.py` | ctypes wrapper over `libbast.so` |
| `palette.py` | palette build step; `load_gpl()` is the Python palette reader |
| `make_palette.py` | regenerates `catlantis.gpl` from the art-bible ramp plan |
| `palette_swatch.py` | 320x240 review swatch of the palette |
| `colormap.py`, `colormap_preview.py` | colormap build step and its review image |
| `palcheck.py` | checks PNG/TEX files against the game palette |
| `png2tex.py`, `tex2png.py` | single-file TEX conversion; `png2tex` keeps the indices of a PNG already in the game palette (hand-drawn art using fullbright entries) |
| `sfx.py`, `sfx_sheet.py` | sound-effect synthesiser and its review sheet |
| `mkmap.py` | ASCII layout -> `.vmap` |
| `gen_trig.c` | build-time trig table generator |
