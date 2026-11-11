#include "engine/colormap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/files.h"

static uint8_t palette_rgb[768];
static uint8_t table[COLORMAP_BYTES];
static uint8_t lit[256]; /* see colormap_dim_remap() */
static char error_msg[1200] = "colormap not loaded";

const char *colormap_error(void)
{
    return error_msg;
}

int colormap_set(const uint8_t *rgb, const uint8_t *cmap, size_t size)
{
    if (size != COLORMAP_BYTES) {
        snprintf(error_msg, sizeof error_msg, "colormap is %zu bytes, need %d", size,
                 COLORMAP_BYTES);
        return 0;
    }
    for (int i = 0; i < 256; i++) {
        if (cmap[i] != i) {
            snprintf(error_msg, sizeof error_msg, "colormap row 0 is not identity at %d", i);
            return 0;
        }
    }
    memcpy(palette_rgb, rgb, sizeof palette_rgb);
    memcpy(table, cmap, sizeof table);
    for (int i = 0; i < 256; i++)
        lit[i] = (uint8_t)i;
    return 1;
}

/* Reads one asset through files.c (game.pak first, loose fallback) and checks
 * its size. NULL on failure, with error_msg set. */
static uint8_t *read_exact(const char *name, int64_t want)
{
    int64_t n = 0;
    uint8_t *d = files_read(name, &n);
    if (!d) {
        snprintf(error_msg, sizeof error_msg, "%s", files_error());
        return NULL;
    }
    if (n != want) {
        snprintf(error_msg, sizeof error_msg, "%s: %lld bytes, need %lld", name, (long long)n,
                 (long long)want);
        free(d);
        return NULL;
    }
    return d;
}

int colormap_load(const char *dir)
{
    if (!files_init(dir)) {
        snprintf(error_msg, sizeof error_msg, "%s", files_error());
        return 0;
    }
    uint8_t *rgb = read_exact("palette.bin", 768);
    uint8_t *flags = rgb ? read_exact("palette_flags.bin", 256) : NULL;
    bast_palette *pal = flags ? bast_palette_from_memory(rgb, flags) : NULL;
    uint8_t *cmap = pal ? read_exact("colormap.bin", COLORMAP_BYTES) : NULL;
    int ok = cmap != NULL;
    if (ok) {
        uint8_t prgb[768];
        memcpy(prgb, pal->rgb, sizeof prgb);
        ok = colormap_set(prgb, cmap, COLORMAP_BYTES);
        /* Fullbright entries get a lit stand-in so menus can dim them. */
        for (int i = 0; ok && i < 256; i++)
            if (pal->flags[i] & BAST_PAL_FULLBRIGHT)
                lit[i] = (uint8_t)bast_palette_nearest(pal, pal->rgb[i][0], pal->rgb[i][1],
                                                       pal->rgb[i][2]);
    } else if (flags && !pal) {
        snprintf(error_msg, sizeof error_msg, "palette: %s", bast_error());
    }
    bast_palette_free(pal);
    free(cmap);
    free(flags);
    free(rgb);
    return ok;
}

const uint8_t *colormap_palette(void)
{
    return palette_rgb;
}

const uint8_t *colormap_row(int r)
{
    if (r < 0)
        r = 0;
    if (r > COLORMAP_ROWS - 1)
        r = COLORMAP_ROWS - 1;
    return table + r * 256;
}

void colormap_dim_remap(int r, uint8_t out[256])
{
    const uint8_t *row = colormap_row(r);
    for (int i = 0; i < 256; i++)
        out[i] = row[lit[i]];
}
