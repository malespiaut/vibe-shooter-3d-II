#!/bin/sh
# Hand-drawn world sprites: assets/src/sprites/*.png (indexed, game palette)
# -> assets/gen/sprites/*.tex, origin bottom-centre (docs/design/tech.md, Sprites).
# Rebuilt from scratch so a removed source leaves no stale .tex.
set -eu
cd "$(dirname "$0")/../.."
rm -rf assets/gen/sprites
mkdir -p assets/gen/sprites
n=0
for f in assets/src/sprites/*.png; do
    [ -e "$f" ] || continue
    stem=$(basename "$f" .png)
    python3 - "$f" "assets/gen/sprites/$stem.tex" <<'PY'
import sys
sys.path.insert(0, "tools")
from PIL import Image
import bast
img = Image.open(sys.argv[1]); img.load()
t = bast.Tex.from_image(img, origin=(img.size[0] // 2, img.size[1]))
t.save(sys.argv[2])
PY
    n=$((n + 1))
done
echo "sprites: $n sprite(s) in assets/gen/sprites"
