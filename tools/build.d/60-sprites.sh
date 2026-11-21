#!/bin/sh
# Pre-rendered sprites (one tools/render_sprite.sh call each): every assets/src/sprites/<name>/sprite.json (with
# <name>.blend next to it) -> assets/gen/sprites/<name>_<anim>_d<dir>_f<frame>.tex
# through tools/blender_render.py. A sprite whose model is a generator script
# (<name>/model.py instead of a committed .blend) first gets its .blend built
# into assets/gen/blend/<name>/ (model.py copies sprite.json next to it).
# sprite.json carries "canvas": [W, H] (art-bible.md, Sizes). No sprite yet is fine; a sprite without blender is
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
    sh tools/render_sprite.sh "$name"
    n=$((n + 1))
    frames=$((frames + $(ls assets/gen/sprites | grep -cE "^${name}_.+_d[0-7]_f[0-9]+\.tex\$" || true)))
done
echo "sprites: $n sprite(s), $frames frames in assets/gen/sprites"
