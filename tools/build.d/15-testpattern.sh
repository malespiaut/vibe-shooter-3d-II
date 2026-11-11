#!/bin/sh
# Renderer test-pattern textures (test fixture, never shipped in a level):
# tools/testpattern.py -> assets/gen/test/test01..08.tex.
set -eu
cd "$(dirname "$0")/../.."
rm -rf assets/gen/test
python3 tools/testpattern.py --count 8 -o assets/gen/test
