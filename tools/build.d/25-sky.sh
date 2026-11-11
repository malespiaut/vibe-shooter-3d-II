#!/bin/sh
# Level skies (art-bible.md Sizes, Sky): assets/src/sky/*.json ->
# assets/gen/textures/sky_*.tex (tools/skygen.py paints, tools/quantize.py
# converts). Runs after 20-texgen.sh, which rebuilds assets/gen/textures from
# scratch, so no stale sky survives.
set -eu
cd "$(dirname "$0")/../.."
python3 tools/skygen.py assets/src/sky/*.json -o assets/gen/textures
