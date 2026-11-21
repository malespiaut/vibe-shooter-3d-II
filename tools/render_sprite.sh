#!/bin/sh
# Render one pre-rendered sprite set through Blender, as tools/build.d/60-sprites.sh
# and 48-props.sh do for every set. Used by the CMake asset targets (#224).
#
#   tools/render_sprite.sh SRC_DIR OUT_DIR BLEND_DIR
#
# SRC_DIR holds sprite.json plus <name>.blend or model.py (name = basename of
# SRC_DIR). A model.py first builds BLEND_DIR/<name>.blend. Frames go to
# OUT_DIR/<name>_<anim>_d<dir>_f<n>.tex. Blender logs stay in BLEND_DIR; only
# the summary line is printed, and the log tail on failure.
set -eu
src=$1
out=$2
gen=$3
name=$(basename "$src")
here=$(cd "$(dirname "$0")" && pwd)
command -v blender >/dev/null || { echo "render_sprite: blender not found, needed for $name" >&2; exit 1; }
canvas=$(python3 -c 'import json,sys; c=json.load(open(sys.argv[1]))["canvas"]; print("%dx%d" % tuple(c))' "$src/sprite.json")
rm -rf "$gen"
mkdir -p "$gen" "$out"
blend="$src/$name.blend"
if [ -f "$src/model.py" ]; then
    blender -b --factory-startup --python "$src/model.py" -- "$gen" > "$gen/model.log" 2>&1 \
        && [ -f "$gen/$name.blend" ] \
        || { tail -20 "$gen/model.log" >&2; echo "render_sprite: $name model.py failed" >&2; exit 1; }
    blend="$gen/$name.blend"
fi
blender -b "$blend" --python "$here/blender_render.py" -- \
    --sprite "$name" --canvas "$canvas" --out "$out" > "$gen/render.log" 2>&1 \
    || { tail -20 "$gen/render.log" >&2; echo "render_sprite: $name render failed" >&2; exit 1; }
grep '^blender_render:' "$gen/render.log"
