#!/bin/sh
# Decorations (items.md Decorations): hand-drawn props from
# assets/src/sprites/props/*.pix through tools/items.py, then every
# pre-rendered prop assets/src/sprites/props/<name>/model.py + sprite.json
# through tools/blender_render.py, all into assets/gen/sprites/props/
# (<name>_<anim>_d<dir>_f<n>.tex, origin bottom-centre). Runs after
# 45-sprites.sh, which wipes assets/gen/sprites; items.py --clean rebuilds
# props/ from scratch first, so no stale frame survives.
set -eu
cd "$(dirname "$0")/../.."
out=${PROPS_OUT:-assets/gen/sprites/props}   # PROPS_OUT: ctest sprites_props_dt
python3 tools/items.py --src assets/src/sprites/props -o "$out" --clean
n=0
for json in assets/src/sprites/props/*/sprite.json; do
    [ -e "$json" ] || continue
    dir=$(dirname "$json")
    name=$(basename "$dir")
    canvas=$(python3 -c 'import json,sys; c=json.load(open(sys.argv[1]))["canvas"]; print("%dx%d" % tuple(c))' "$json")
    command -v blender >/dev/null || { echo "props: blender not found, needed for $name" >&2; exit 1; }
    gen="${PROPS_BLEND:-assets/gen/blend/props}/$name"
    rm -rf "$gen"
    mkdir -p "$gen"
    blender -b --factory-startup --python "$dir/model.py" -- "$gen" > "$gen/model.log" 2>&1 \
        && [ -f "$gen/$name.blend" ] \
        || { tail -20 "$gen/model.log" >&2; echo "props: $name model.py failed" >&2; exit 1; }
    blender -b "$gen/$name.blend" --python tools/blender_render.py -- \
        --sprite "$name" --canvas "$canvas" --out "$out" > "$gen/render.log" 2>&1 \
        || { tail -20 "$gen/render.log" >&2; echo "props: $name render failed" >&2; exit 1; }
    grep '^blender_render:' "$gen/render.log"
    n=$((n + 1))
done
echo "props: $n pre-rendered prop(s), $(ls "$out" | grep -c '\.tex$') frames in $out"
