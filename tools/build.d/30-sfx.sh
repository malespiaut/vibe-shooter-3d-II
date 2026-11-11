#!/bin/sh
# Sound effects: assets/src/sfx/*.json -> assets/gen/sfx/*.snd (tools/sfx.py).
# Rebuilt from scratch so a removed parameter file leaves no stale .snd.
set -eu
cd "$(dirname "$0")/../.."
rm -rf assets/gen/sfx
python3 tools/sfx.py -q assets/src/sfx/*.json -o assets/gen/sfx
echo "sfx: $(ls assets/gen/sfx | wc -l) effects in assets/gen/sfx"
