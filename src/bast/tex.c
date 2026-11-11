/* TEX: 16-byte little-endian header + column-major palette indices.
 * docs/design/tech.md, "TEX (textures, sprites, HUD art, fonts)". */
#include "bast_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int valid_dim(int v)
{
    return v >= 1 && v <= BAST_TEX_MAX_DIM;
}

bast_tex *bast_tex_new(int width, int height)
{
    if (!valid_dim(width) || !valid_dim(height)) {
        bast_set_error("bad TEX size %dx%d (1..%d)", width, height, BAST_TEX_MAX_DIM);
        return NULL;
    }
    bast_tex *t = calloc(1, sizeof *t);
    uint8_t *px = calloc((size_t)width * (size_t)height, 1);
    if (!t || !px) {
        free(t);
        free(px);
        bast_set_error("out of memory");
        return NULL;
    }
    t->width = (uint16_t)width;
    t->height = (uint16_t)height;
    t->pixels = px;
    return t;
}

void bast_tex_free(bast_tex *tex)
{
    if (tex) {
        free(tex->pixels);
        free(tex);
    }
}

static unsigned rd16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static void wr16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8 & 0xff);
}

bast_tex *bast_tex_from_memory(const uint8_t *d, int64_t size)
{
    if (size < BAST_TEX_HEADER_SIZE || memcmp(d, "TEX1", 4) != 0) {
        bast_set_error("not a TEX1 file");
        return NULL;
    }
    int w = (int)rd16(d + 4), h = (int)rd16(d + 6);
    if (!valid_dim(w) || !valid_dim(h)) {
        bast_set_error("bad TEX size %dx%d", w, h);
        return NULL;
    }
    if (d[13] || d[14] || d[15]) {
        bast_set_error("TEX reserved header bytes not zero");
        return NULL;
    }
    if (size != BAST_TEX_HEADER_SIZE + (int64_t)w * h) {
        bast_set_error("TEX is %lld bytes, %dx%d needs %lld", (long long)size, w, h,
                       (long long)(BAST_TEX_HEADER_SIZE + (int64_t)w * h));
        return NULL;
    }
    bast_tex *t = bast_tex_new(w, h);
    if (!t)
        return NULL;
    t->origin_x = (int16_t)((int32_t)rd16(d + 8) - ((rd16(d + 8) & 0x8000u) ? 0x10000 : 0));
    t->origin_y = (int16_t)((int32_t)rd16(d + 10) - ((rd16(d + 10) & 0x8000u) ? 0x10000 : 0));
    t->flags = d[12];
    memcpy(t->pixels, d + BAST_TEX_HEADER_SIZE, (size_t)w * h);
    return t;
}

bast_tex *bast_tex_load(const char *path)
{
    int64_t size = 0;
    uint8_t *d = bast_read_file(path, &size);
    if (!d)
        return NULL;
    bast_tex *t = bast_tex_from_memory(d, size);
    if (!t) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s", bast_error());
        bast_set_error("%s: %s", path, msg);
    }
    free(d);
    return t;
}

int bast_tex_save(const bast_tex *t, const char *path)
{
    if (!t || !valid_dim(t->width) || !valid_dim(t->height) || !t->pixels) {
        bast_set_error("%s: invalid texture", path);
        return -1;
    }
    uint8_t hdr[BAST_TEX_HEADER_SIZE] = {'T', 'E', 'X', '1'};
    wr16(hdr + 4, t->width);
    wr16(hdr + 6, t->height);
    wr16(hdr + 8, (uint16_t)t->origin_x);
    wr16(hdr + 10, (uint16_t)t->origin_y);
    hdr[12] = t->flags;
    FILE *f = fopen(path, "wb");
    if (!f) {
        bast_set_error("%s: %s", path, strerror(errno));
        return -1;
    }
    size_t n = (size_t)t->width * t->height;
    int ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr && fwrite(t->pixels, 1, n, f) == n;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        bast_set_error("%s: write error", path);
        return -1;
    }
    return 0;
}

bast_tex *bast_tex_from_rgba(const bast_palette *pal, const uint8_t *rgba, int w, int h)
{
    bast_tex *t = bast_tex_new(w, h);
    if (!t)
        return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *p = rgba + ((size_t)y * w + x) * 4;
            uint8_t idx;
            if (p[3] < 128) {
                idx = BAST_INDEX_TRANSPARENT;
                t->flags |= BAST_TEX_HAS_TRANSPARENCY;
            } else {
                idx = (uint8_t)bast_palette_nearest(pal, p[0], p[1], p[2]);
            }
            t->pixels[(size_t)x * h + y] = idx;
        }
    }
    return t;
}
