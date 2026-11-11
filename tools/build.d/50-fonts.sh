#!/bin/sh
# Game fonts (docs/design/hud.md Fonts): the indexed PNG sheets exported from
# the hand-drawn glyph data (tools/fontsheet.py) -> assets/gen/fonts/*.tex
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
rm -rf "$ROOT/assets/gen/fonts"
mkdir -p "$ROOT/assets/gen/fonts"
for f in small menu bigdigits; do
    python3 "$ROOT/tools/png2tex.py" "$ROOT/assets/src/fonts/$f.png" "$ROOT/assets/gen/fonts/$f.tex"
done
