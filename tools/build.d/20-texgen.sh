#!/bin/sh
# Procedural textures: assets/src/textures/*.json -> assets/gen/textures/*.tex
# (tools/texgen.py, GAME.md 6.1). Rebuilt from scratch so a removed parameter
# file leaves no stale .tex.
set -eu
cd "$(dirname "$0")/../.."
rm -rf assets/gen/textures
python3 tools/texgen.py -q assets/src/textures/*.json -o assets/gen/textures
echo "texgen: $(ls assets/gen/textures | wc -l) textures in assets/gen/textures"
