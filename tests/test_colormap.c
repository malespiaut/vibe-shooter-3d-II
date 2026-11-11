/* Colormap: generated table + engine loader. Usage: test_colormap <assets/gen dir> */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"

static int index_of(const bast_palette *pal, const char *gpl, const char *name)
{
    /* Find the named entry in catlantis.gpl: the n-th colour line is index n. */
    FILE *f = fopen(gpl, "r");
    assert(f);
    char line[256];
    int idx = -1, n = 0;
    while (fgets(line, sizeof line, f)) {
        int r, g, b;
        char nm[128];
        if (sscanf(line, " %d %d %d %127s", &r, &g, &b, nm) == 4) {
            if (strcmp(nm, name) == 0) {
                idx = n;
                assert(pal->rgb[n][0] == r && pal->rgb[n][1] == g && pal->rgb[n][2] == b);
                break;
            }
            n++;
        }
    }
    fclose(f);
    assert(idx >= 0);
    return idx;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const char *dir = argv[1];
    char path[1024];

    /* Size on disk. */
    snprintf(path, sizeof path, "%s/colormap.bin", dir);
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    assert(ftell(f) == 8192);
    fclose(f);

    if (!colormap_load(dir))
        fprintf(stderr, "%s\n", colormap_error());
    assert(colormap_load(dir));

    snprintf(path, sizeof path, "%s/palette.bin", dir);
    bast_palette *pal = bast_palette_load(path);
    assert(pal);
    assert(memcmp(colormap_palette(), pal->rgb, 768) == 0);

    /* Row 0 identity. */
    for (int i = 0; i < 256; i++)
        assert(colormap_row(0)[i] == i);
    /* Clamping. */
    assert(colormap_row(-5) == colormap_row(0));
    assert(colormap_row(99) == colormap_row(31));

    for (int i = 0; i < 256; i++) {
        int fixed = (pal->flags[i] & (BAST_PAL_FULLBRIGHT | BAST_PAL_TRANSPARENT)) != 0;
        double prev_l = pal->lab[i][0];
        for (int r = 1; r < COLORMAP_ROWS; r++) {
            int v = colormap_row(r)[i];
            if (fixed) {
                assert(v == i); /* fullbright and 255 never shade */
                continue;
            }
            /* Shaded entries are usable colours only. */
            assert(!(pal->flags[v] & 0x07));
            /* Never brighter than the row above. */
            double l = pal->lab[v][0];
            if (l > prev_l + 1e-12)
                fprintf(stderr, "index %d row %d: L %.4f > %.4f\n", i, r, l, prev_l);
            assert(l <= prev_l + 1e-12);
            prev_l = l;
        }
    }
    for (int i = 240; i <= 255; i++)
        assert(pal->flags[i] & (BAST_PAL_FULLBRIGHT | BAST_PAL_TRANSPARENT));

    /* Warm shift (art bible): row 16 of steel_12 is warmer (higher Oklab b)
     * than the plain scaled colour's nearest entry. */
    int s12 = index_of(pal, argv[2], "steel_12");
    const uint8_t *c = pal->rgb[s12];
    double fac = (32.0 - 16) / 32;
    int plain = bast_palette_nearest(pal, (uint8_t)floor(c[0] * fac + 0.5),
                                     (uint8_t)floor(c[1] * fac + 0.5),
                                     (uint8_t)floor(c[2] * fac + 0.5));
    int warm = colormap_row(16)[s12];
    printf("steel_12=%d row16 warm=%d (b=%.4f) plain=%d (b=%.4f)\n", s12, warm,
           pal->lab[warm][2], plain, pal->lab[plain][2]);
    assert(pal->lab[warm][2] > pal->lab[plain][2]);

    /* Key colour in shade (issue #109): blue_07..blue_11 keep a blue hue
     * (Oklab b < 0) and chroma >= 0.04 through row 20. */
    for (int k = 7; k <= 11; k++) {
        char nm[16];
        snprintf(nm, sizeof nm, "blue_%02d", k);
        int bi = index_of(pal, argv[2], nm);
        for (int r = 0; r <= 20; r++) {
            int v = colormap_row(r)[bi];
            double a = pal->lab[v][1], b = pal->lab[v][2];
            double ch = sqrt(a * a + b * b);
            if (!(b < 0 && ch >= 0.04))
                fprintf(stderr, "%s row %d -> %d: chroma %.3f b %.3f\n", nm, r, v, ch, b);
            assert(b < 0 && ch >= 0.04);
        }
    }

    /* Loader rejects bad input and keeps the old tables. */
    static uint8_t bad[COLORMAP_BYTES];
    assert(!colormap_set(pal->rgb[0], bad, sizeof bad - 1));
    assert(!colormap_set(pal->rgb[0], bad, sizeof bad)); /* row 0 not identity */
    assert(colormap_row(0)[200] == 200);
    assert(!colormap_load("/nonexistent"));

    bast_palette_free(pal);
    printf("colormap: ok\n");
    return 0;
}
