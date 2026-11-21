#!/bin/sh
# Pre-render one sprite: assets/src/sprites/<name>/sprite.json (+ <name>.blend,
# or model.py that builds it) -> <out>/sprites/<name>_<anim>_d<dir>_f<frame>.tex
# through tools/blender_render.py. Shared by tools/build.d/60-sprites.sh and
# the CMake enemy_sprites target.
#   tools/render_sprite.sh <name> [<assets/gen dir>]
# A model.py sprite gets its .blend built into <out>/blend/<name>/ first.
# Logs: <out>/blend/<name>/model.log, <out>/sprites/.<name>.log.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
name=$1
out=${2:-$ROOT/assets/gen}
dir=$ROOT/assets/src/sprites/$name
json=$dir/sprite.json
[ -f "$json" ] || { echo "sprites: $json missing" >&2; exit 1; }
canvas=$(python3 -c 'import json,sys; c=json.load(open(sys.argv[1]))["canvas"]; print("%dx%d" % tuple(c))' "$json")
command -v blender >/dev/null || { echo "sprites: blender not found, needed for $name" >&2; exit 1; }
mkdir -p "$out/sprites"
blend=$dir/$name.blend
if [ -f "$dir/model.py" ]; then
    gen=$out/blend/$name
    rm -rf "$gen"
    mkdir -p "$gen"
    blender -b --factory-startup --python "$dir/model.py" -- "$gen" > "$gen/model.log" 2>&1 \
        && [ -f "$gen/$name.blend" ] \
        || { tail -20 "$gen/model.log" >&2; echo "sprites: $name model.py failed" >&2; exit 1; }
    blend=$gen/$name.blend
fi
blender -b "$blend" --python "$ROOT/tools/blender_render.py" -- \
    --sprite "$name" --canvas "$canvas" --out "$out/sprites" > "$out/sprites/.$name.log" 2>&1 \
    || { tail -20 "$out/sprites/.$name.log" >&2; echo "sprites: $name failed" >&2; exit 1; }
grep '^blender_render:' "$out/sprites/.$name.log"
