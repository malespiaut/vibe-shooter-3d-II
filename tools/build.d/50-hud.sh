#!/bin/sh
# Status bar panel and HUD icons (hud.md): assets/src/hud/ -> assets/gen/hud/*.tex (+ .png previews).
# Rebuilt from scratch so a removed source leaves no stale file.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
python3 "$ROOT/tools/hud.py" --src "$ROOT/assets/src/hud" -o "$ROOT/assets/gen/hud" --clean
