/* png_writer, C half: writes a 320x240 indexed PNG of a known pattern
 * through the game palette (tests/test_png_writer.py re-reads it with
 * Pillow) and checks the CRC32 / Adler-32 helpers against known values.
 *   test_png_writer <palette.bin> <out.png> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/png.h"

int main(int argc, char **argv)
{
    assert(argc == 3);
    /* Reference values (zlib.crc32 / zlib.adler32 of "123456789"). */
    const uint8_t *s = (const uint8_t *)"123456789";
    assert(png_crc32(s, 9) == 0xCBF43926u);
    assert(png_adler32(s, 9) == 0x091E01DEu);

    uint8_t pal[768];
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    assert(fread(pal, 1, sizeof pal, f) == sizeof pal);
    fclose(f);

    /* Pattern the Python half recomputes: (x * 7 + y * 13) & 255. */
    enum { W = 320, H = 240 };
    static uint8_t px[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            px[y * W + x] = (uint8_t)((x * 7 + y * 13) & 255);
    assert(png_write_indexed(argv[2], px, W, H, pal));

    /* Bad dimensions are refused. */
    size_t n;
    assert(png_encode_indexed(px, 0, H, pal, &n) == NULL);
    printf("png_writer: wrote %s\n", argv[2]);
    return 0;
}
