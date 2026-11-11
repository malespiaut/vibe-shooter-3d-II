#!/bin/sh
# First-person view-model frames (weapons.md, View-model animation frames):
# assets/src/viewmodels/*.pix -> assets/gen/viewmodels/vm<N>_<frame>.tex (+ .png previews),
# origin bottom-centre. Rebuilt from scratch so a removed source leaves no stale file.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
python3 "$ROOT/tools/viewmodels.py" --src "$ROOT/assets/src/viewmodels" -o "$ROOT/assets/gen/viewmodels" --clean
