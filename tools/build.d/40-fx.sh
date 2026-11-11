#!/bin/sh
# Pre-rendered particle effects (GAME.md 6.3): assets/src/fx/*.json -> assets/gen/fx/*.tex
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
rm -rf "$ROOT/assets/gen/fx"
python3 "$ROOT/tools/fx.py" "$ROOT"/assets/src/fx/*.json -o "$ROOT/assets/gen/fx"
