#!/bin/sh
# Render every level sky in-game (issue #155): for each assets/src/sky/<name>.json,
# build maps/src/skytest.txt with `sky <name>` into a temporary map, play
# demos/sky-turn.demo on it headless (a 360-degree turn, one frame per 22.5
# degrees) and write <out>/<name>/frame_*.png plus a 4x4 contact sheet
# <out>/<name>_turn.png (1x) and the first frame at 3x, <out>/<name>_3x.png.
# Also <out>/<name>_seam.png: two copies of the sky texture side by side at 3x
# (column 319 meets column 0 in the middle).
#
# Needs: the CMake build, tools/build_assets.sh, ImageMagick.
#   tools/sky_shots.sh /tmp/skyshots
set -eu
cd "$(dirname "$0")/.."
out=${1:?usage: tools/sky_shots.sh <outdir>}
export BAST_LIB="${BAST_LIB:-$PWD/build/libbast.so}"
export BAST_PALETTE="$PWD/assets/gen/palette.bin"
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for json in assets/src/sky/*.json; do
    name=$(basename "$json" .json)
    sed "s/^sky .*/sky $name/" maps/src/skytest.txt > "$tmp/$name.txt"
    python3 tools/mkmap.py "$tmp/$name.txt" "$tmp/$name.vmap" > /dev/null
    rm -rf "${out:?}/$name"
    ./build/game --headless --demo demos/sky-turn.demo --map "$tmp/$name.vmap" \
        --frames-out "$out/$name" --frame-every 2 | tail -1
    magick montage "$out/$name"/frame_*.png -tile 4x4 -geometry +2+2 -background '#202020' \
        "$out/${name}_turn.png"
    magick "$out/$name/frame_000002.png" -filter point -resize 300% "$out/${name}_3x.png"
    python3 tools/tex2png.py "assets/gen/textures/$name.tex" "$tmp/$name.png" > /dev/null
    magick "$tmp/$name.png" "$tmp/$name.png" +append -filter point -resize 300% \
        "$out/${name}_seam.png"
done
echo "sky_shots: $(ls "$out"/*_turn.png | wc -l) skies in $out"
