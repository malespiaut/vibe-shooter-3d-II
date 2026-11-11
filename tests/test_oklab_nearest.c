/* libbast palette loading + Oklab nearest. Usage: test_oklab_nearest <palette.bin> */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"

static void test_game_palette(const char *path)
{
    bast_palette *pal = bast_palette_load(path);
    if (!pal)
        fprintf(stderr, "%s\n", bast_error());
    assert(pal);
    assert(pal->flags[255] == BAST_PAL_TRANSPARENT);
    assert(pal->rgb[255][0] == 255 && pal->rgb[255][1] == 0 && pal->rgb[255][2] == 255);

    int checked = 0;
    for (int i = 0; i < 256; i++) {
        uint8_t f = pal->flags[i];
        const uint8_t *c = pal->rgb[i];
        if (f & BAST_PAL_RESERVED) {
            /* Never returned, whatever is allowed. */
            int got = bast_palette_nearest_ex(pal, c[0], c[1], c[2],
                                              BAST_ALLOW_FULLBRIGHT | BAST_ALLOW_TRANSPARENT);
            assert(got != i);
            continue;
        }
        /* Every usable colour maps to itself once its class is allowed. */
        int got = bast_palette_nearest_ex(pal, c[0], c[1], c[2],
                                          BAST_ALLOW_FULLBRIGHT | BAST_ALLOW_TRANSPARENT);
        if (got != i)
            fprintf(stderr, "index %d maps to %d\n", i, got);
        assert(got == i);
        int def = bast_palette_nearest(pal, c[0], c[1], c[2]);
        if (f & (BAST_PAL_FULLBRIGHT | BAST_PAL_TRANSPARENT))
            assert(def != i && !(pal->flags[def] & 0x07));
        else
            assert(def == i);
        checked++;
    }
    assert(checked == 256 - 24);

    /* #FF00FF maps to 255 only when transparency is allowed. */
    assert(bast_palette_nearest_ex(pal, 255, 0, 255, BAST_ALLOW_TRANSPARENT) == 255);
    assert(bast_palette_nearest_ex(pal, 255, 0, 255,
                                   BAST_ALLOW_TRANSPARENT | BAST_ALLOW_FULLBRIGHT) == 255);
    assert(bast_palette_nearest(pal, 255, 0, 255) != 255);
    assert(bast_palette_nearest_ex(pal, 255, 0, 255, BAST_ALLOW_FULLBRIGHT) != 255);

    assert(bast_palette_nearest(pal, 255, 255, 255) == BAST_INDEX_WHITE);
    assert(bast_palette_nearest(pal, 0, 0, 0) == BAST_INDEX_BLACK);
    bast_palette_free(pal);
}

static void test_ties_and_flags(void)
{
    uint8_t rgb[768], fl[256];
    memset(rgb, 0, sizeof rgb);
    memset(fl, BAST_PAL_RESERVED, sizeof fl);
    /* Index 7 and 9 identical grey, 3 is the same grey but reserved,
     * 5 the same grey but fullbright. */
    const int same[] = {3, 5, 7, 9};
    for (int k = 0; k < 4; k++)
        memset(rgb + same[k] * 3, 128, 3);
    fl[3] = BAST_PAL_RESERVED;
    fl[5] = BAST_PAL_FULLBRIGHT;
    fl[7] = 0;
    fl[9] = 0;
    bast_palette *pal = bast_palette_from_memory(rgb, fl);
    assert(pal);
    assert(bast_palette_nearest(pal, 128, 128, 128) == 7);
    assert(bast_palette_nearest(pal, 200, 10, 10) == 7); /* only 7 and 9 usable */
    assert(bast_palette_nearest_ex(pal, 128, 128, 128, BAST_ALLOW_FULLBRIGHT) == 5);
    bast_palette_free(pal);
}

static void test_conversions(void)
{
    double lab[3];
    bast_srgb_to_oklab(255, 255, 255, lab);
    assert(fabs(lab[0] - 1.0) < 1e-4 && fabs(lab[1]) < 1e-4 && fabs(lab[2]) < 1e-4);
    bast_srgb_to_oklab(0, 0, 0, lab);
    assert(lab[0] == 0.0 && lab[1] == 0.0 && lab[2] == 0.0);
    /* Round trip of in-gamut colours is exact after rounding. */
    for (int v = 0; v < 256; v += 5) {
        uint8_t c[3] = {(uint8_t)v, (uint8_t)(255 - v), (uint8_t)(v / 2)}, out[3];
        bast_srgb_to_oklab(c[0], c[1], c[2], lab);
        bast_oklab_to_srgb(lab[0], lab[1], lab[2], out);
        assert(memcmp(c, out, 3) == 0);
    }
    /* Out of gamut: huge chroma is reduced, L outside 0..1 clamped. */
    uint8_t out[3];
    bast_oklab_to_srgb(2.0, 0.0, 0.0, out);
    assert(out[0] == 255 && out[1] == 255 && out[2] == 255);
    bast_oklab_to_srgb(0.6, 0.9, 0.0, out); /* very red-magenta, out of gamut */
    assert(out[0] > out[1] && out[0] > 100);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_ties_and_flags();
    test_conversions();
    test_game_palette(argv[1]);
    assert(bast_palette_load("/nonexistent/palette.bin") == NULL);
    printf("test_oklab_nearest: OK\n");
    return 0;
}
