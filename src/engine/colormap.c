#include "engine/colormap.h"

#include <stdio.h>
#include <string.h>

#include "bast.h"

static uint8_t palette_rgb[768];
static uint8_t table[COLORMAP_BYTES];
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
    return 1;
}

int colormap_load(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/palette.bin", dir);
    bast_palette *pal = bast_palette_load(path);
    if (!pal) {
        snprintf(error_msg, sizeof error_msg, "%s", bast_error());
        return 0;
    }
    uint8_t rgb[768];
    memcpy(rgb, pal->rgb, sizeof rgb);
    bast_palette_free(pal);

    snprintf(path, sizeof path, "%s/colormap.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(error_msg, sizeof error_msg, "%s: cannot open", path);
        return 0;
    }
    static uint8_t buf[COLORMAP_BYTES + 1];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n != COLORMAP_BYTES) {
        snprintf(error_msg, sizeof error_msg, "%s: %zu bytes, need %d", path, n,
                 COLORMAP_BYTES);
        return 0;
    }
    return colormap_set(rgb, buf, n);
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
