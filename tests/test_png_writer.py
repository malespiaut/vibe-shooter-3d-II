#!/usr/bin/env python3
"""ctest `png_writer`: the engine's PNG writer (src/engine/png.c).

Runs build/test_png_writer, then re-reads the PNG with Pillow: mode P,
320x240, palette equal to palette.bin, every pixel equal to the pattern.
  test_png_writer.py <test_png_writer exe> <palette.bin> <tmp dir>
"""
import os
import subprocess
import sys
import zlib

from PIL import Image

exe, palette_bin, tmp = sys.argv[1:4]
os.makedirs(tmp, exist_ok=True)
out = os.path.join(tmp, "png_writer.png")
subprocess.run([exe, palette_bin, out], check=True)

pal = open(palette_bin, "rb").read()
assert len(pal) == 768

# Byte-level: signature, chunk order, every CRC valid.
data = open(out, "rb").read()
assert data[:8] == b"\x89PNG\r\n\x1a\n"
pos, types = 8, []
while pos < len(data):
    n = int.from_bytes(data[pos:pos + 4], "big")
    t = data[pos + 4:pos + 8]
    body = data[pos + 8:pos + 8 + n]
    crc = int.from_bytes(data[pos + 8 + n:pos + 12 + n], "big")
    assert zlib.crc32(t + body) == crc, f"bad CRC in {t}"
    types.append(t)
    pos += 12 + n
assert types == [b"IHDR", b"PLTE", b"IDAT", b"IEND"], types

img = Image.open(out)
img.load()
assert img.mode == "P", img.mode
assert img.size == (320, 240), img.size
assert bytes(img.getpalette()[:768]) == pal, "palette differs from palette.bin"
W, H = img.size
want = bytes(((x * 7 + y * 13) & 255) for y in range(H) for x in range(W))
assert img.tobytes() == want, "pixels differ"
print("png_writer: OK (mode P, 320x240, palette and pixels equal)")
