#!/bin/sh
# Pre-rendered sprites: every assets/src/sprites/<name>/sprite.json (with
# <name>.blend next to it) -> assets/gen/sprites/<name>_<anim>_d<dir>_f<frame>.tex
# through tools/blender_render.py. sprite.json carries "canvas": [W, H]
# (art-bible.md, Sizes). Rebuilt from scratch so a removed sprite leaves no
# stale .tex. No sprite yet is fine; a sprite without blender is an error.
set -eu
cd "$(dirname "$0")/../.."
rm -rf assets/gen/sprites
mkdir -p assets/gen/sprites
n=0
for json in assets/src/sprites/*/sprite.json; do
    [ -e "$json" ] || continue
    dir=$(dirname "$json")
    name=$(basename "$dir")
    canvas=$(python3 -c 'import json,sys; c=json.load(open(sys.argv[1]))["canvas"]; print("%dx%d" % tuple(c))' "$json")
    command -v blender >/dev/null || { echo "sprites: blender not found, needed for $name" >&2; exit 1; }
    blender -b "$dir/$name.blend" --python tools/blender_render.py -- \
        --sprite "$name" --canvas "$canvas" --out assets/gen/sprites > "assets/gen/sprites/.$name.log" 2>&1 \
        || { tail -20 "assets/gen/sprites/.$name.log" >&2; echo "sprites: $name failed" >&2; exit 1; }
    grep '^blender_render:' "assets/gen/sprites/.$name.log"
    n=$((n + 1))
done
echo "sprites: $n sprite(s), $(ls assets/gen/sprites | grep -c '\.tex$' || true) frames in assets/gen/sprites"
