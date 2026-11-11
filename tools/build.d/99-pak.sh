#!/bin/sh
# Last step: pack everything under assets/gen/ into assets/gen/game.pak
# (docs/design/tech.md, "Archives"). The engine reads game.pak first and
# falls back to loose files.
set -eu
cd "$(dirname "$0")/../.."
python3 tools/pak.py assets/gen -o assets/gen/game.pak
