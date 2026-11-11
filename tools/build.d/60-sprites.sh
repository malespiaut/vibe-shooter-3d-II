#!/bin/sh
# Pre-rendered sprites: every assets/src/sprites/<name>/sprite.json (with
# <name>.blend next to it) -> assets/gen/sprites/<name>_<anim>_d<dir>_f<frame>.tex
# through tools/blender_render.py. sprite.json carries "canvas": [W, H]
# (art-bible.md, Sizes). No sprite yet is fine; a sprite without blender is
# an error.
# 45-sprites.sh owns cleaning assets/gen/sprites (it runs first and rebuilds
# the directory from scratch). Do not rm -rf here: it would delete the
# hand-drawn sprites 45 just built.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p assets/gen/sprites
n=0
frames=0
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
    frames=$((frames + $(ls assets/gen/sprites | grep -cE "^${name}_.+_d[0-7]_f[0-9]+\.tex\$" || true)))
done
echo "sprites: $n sprite(s), $frames frames in assets/gen/sprites"
