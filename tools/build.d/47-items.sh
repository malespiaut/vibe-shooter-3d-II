#!/bin/sh
# Hand-drawn pickup sprites (items.md): assets/src/sprites/items/*.pix ->
# assets/gen/sprites/items/<name>_idle_d0_f<n>.tex (+ .png previews), origin
# bottom-centre. Runs after 45-sprites.sh, which wipes assets/gen/sprites;
# --clean rebuilds only the items/ subdirectory, so no stale file survives.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
python3 "$ROOT/tools/items.py" --src "$ROOT/assets/src/sprites/items" \
    -o "$ROOT/assets/gen/sprites/items" --clean
