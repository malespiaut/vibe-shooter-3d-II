#!/bin/sh
# Music: assets/src/music/*.vmus -> assets/gen/music/*.vmus (docs/design/audio.md,
# "Music"). Songs ship as text; each one is parsed by the engine's sequencer
# (build/vmus_render, one second rendered to /dev/null) so a broken song stops
# the build. Rebuilt from scratch so a removed song leaves no stale copy.
set -eu
cd "$(dirname "$0")/../.."
render="$(dirname "$BAST_LIB")/vmus_render"
if [ ! -x "$render" ]; then
    echo "music: $render missing; run: cmake --build build" >&2
    exit 1
fi
rm -rf assets/gen/music
mkdir -p assets/gen/music
for s in assets/src/music/*.vmus; do
    "$render" "$s" /dev/null 1 >/dev/null
    cp "$s" assets/gen/music/
done
echo "music: $(ls assets/gen/music | wc -l) songs in assets/gen/music"
