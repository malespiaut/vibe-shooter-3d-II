#!/bin/sh
# Full-screen UI art (docs/design/hud.md Flow): assets/src/ui/ -> assets/gen/ui/title.tex,
# papyrus_panel.tex and tally_panel.tex (+ .png previews).
# Rebuilt from scratch so a removed source leaves no stale file.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
rm -rf "$ROOT/assets/gen/ui"
python3 "$ROOT/tools/title.py" --src "$ROOT/assets/src/ui" -o "$ROOT/assets/gen/ui"
python3 "$ROOT/tools/panels.py" --src "$ROOT/assets/src/ui" -o "$ROOT/assets/gen/ui"
