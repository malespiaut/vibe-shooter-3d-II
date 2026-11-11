#!/bin/sh
# Regenerate every asset into assets/gen/ from a fresh clone.
#
# Needs the CMake build first (libbast.so):
#   cmake -S . -B build -G Ninja && cmake --build build && tools/build_assets.sh
#
# Order: palette, colormap, then every tools/build.d/*.sh in sorted order,
# then palcheck over assets/gen. Stops on the first failure (exit != 0).
# BAST_LIB overrides the library path (default build/libbast.so).
set -eu
cd "$(dirname "$0")/.."

export BAST_LIB="${BAST_LIB:-$PWD/build/libbast.so}"
export BAST_PALETTE="$PWD/assets/gen/palette.bin"
if [ ! -f "$BAST_LIB" ]; then
    echo "build_assets: $BAST_LIB missing; run: cmake -S . -B build -G Ninja && cmake --build build" >&2
    exit 1
fi

step() {
    echo "== $*"
    "$@" || { rc=$?; echo "build_assets: FAILED ($rc): $*" >&2; exit "$rc"; }
}

step python3 tools/palette.py
step python3 tools/colormap.py
for s in tools/build.d/*.sh; do
    [ -e "$s" ] || continue
    step sh "$s"
done
step python3 tools/palcheck.py assets/gen
echo "build_assets: ok"
