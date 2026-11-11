#!/bin/sh
# Gib animations (GAME.md 6.2): every assets/src/gibs/*.json whose "source"
# (path relative to assets/gen) exists -> assets/gen/gibs/<sprite>_gib_d0_f<n>.tex.
# Parameter files whose source is not built yet are skipped and listed.
# Rebuilt from scratch so a removed parameter file leaves no stale frame.
set -eu
cd "$(dirname "$0")/../.."
rm -rf assets/gen/gibs
mkdir -p assets/gen/gibs
built=0
skipped=""
for p in assets/src/gibs/*.json; do
    [ -e "$p" ] || continue
    src=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["source"])' "$p")
    if [ -f "assets/gen/$src" ]; then
        python3 tools/gibs.py -q "$p" -i "assets/gen/$src" -o assets/gen/gibs
        built=$((built + 1))
    else
        skipped="$skipped $(basename "$p")"
    fi
done
echo "gibs: $built animation(s), $(ls assets/gen/gibs | wc -l) frames in assets/gen/gibs${skipped:+; skipped (source missing):$skipped}"
