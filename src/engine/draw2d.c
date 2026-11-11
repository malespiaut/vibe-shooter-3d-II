#include "engine/draw2d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/video.h"

/* ---- ramps --------------------------------------------------------------- */

/* Paintable ramps of catlantis.gpl (art-bible.md Palette). Entries never move
 * once M1 landed; tests/test_draw2d.c checks this table against the .gpl.
 * Ramps later added to the reserved block go here when they are named. */
static const struct {
    const char *name;
    uint8_t first, count;
} RAMPS[] = {
    {"gray", 2, 16},    {"steel", 18, 16},  {"beige", 34, 16}, {"sand", 50, 16},
    {"brown", 66, 16},  {"gold", 82, 12},   {"red", 94, 16},   {"green", 110, 12},
    {"blue", 122, 12},  {"violet", 134, 12}, {"orange", 146, 12}, {"cyan", 158, 12},
    {"olive", 170, 12}, {"fur", 182, 12},   {"pink", 194, 10}, {"indigo", 204, 12},
};

enum { RAMP_COUNT = (int)(sizeof RAMPS / sizeof RAMPS[0]) };

int ramp_table_size(void)
{
    return RAMP_COUNT;
}

const char *ramp_table_entry(int i, int *first, int *count)
{
    if (i < 0 || i >= RAMP_COUNT)
        return NULL;
    if (first)
        *first = RAMPS[i].first;
    if (count)
        *count = RAMPS[i].count;
    return RAMPS[i].name;
}

int ramp_find(const char *name, int *first, int *count)
{
    for (int i = 0; i < RAMP_COUNT; i++) {
        if (strcmp(RAMPS[i].name, name) == 0) {
            ramp_table_entry(i, first, count);
            return 1;
        }
    }
    return 0;
}

void remap_identity(uint8_t remap[256])
{
    for (int i = 0; i < 256; i++)
        remap[i] = (uint8_t)i;
}

int remap_ramp(uint8_t remap[256], const char *from, const char *to)
{
    remap_identity(remap);
    int f0, fn, t0, tn;
    if (!ramp_find(from, &f0, &fn) || !ramp_find(to, &t0, &tn))
        return 0;
    for (int i = 0; i < fn; i++) {
        /* Position i of fn-1 -> nearest position of tn-1 (integer rounding). */
        int j = fn > 1 ? (2 * i * (tn - 1) + (fn - 1)) / (2 * (fn - 1)) : 0;
        remap[f0 + i] = (uint8_t)(t0 + j);
    }
    return 1;
}

/* ---- patches ------------------------------------------------------------- */

void draw_patch_region(uint8_t *fb, const bast_tex *tex, int sx, int sy, int w, int h, int x,
                       int y, const uint8_t *remap)
{
    if (!fb || !tex)
        return;
    /* Clip the source rectangle to the texture. */
    if (sx < 0) {
        w += sx;
        x -= sx;
        sx = 0;
    }
    if (sy < 0) {
        h += sy;
        y -= sy;
        sy = 0;
    }
    if (sx + w > tex->width)
        w = tex->width - sx;
    if (sy + h > tex->height)
        h = tex->height - sy;
    /* Clip the destination to the screen. */
    if (x < 0) {
        w += x;
        sx -= x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        sy -= y;
        y = 0;
    }
    if (x + w > SCREEN_W)
        w = SCREEN_W - x;
    if (y + h > SCREEN_H)
        h = SCREEN_H - y;
    if (w <= 0 || h <= 0)
        return;

    for (int c = 0; c < w; c++) {
        const uint8_t *src = tex->pixels + (size_t)(sx + c) * tex->height + sy;
        uint8_t *dst = fb + (size_t)y * SCREEN_W + x + c;
        if (remap) {
            for (int r = 0; r < h; r++, dst += SCREEN_W) {
                if (src[r] != 255)
                    *dst = remap[src[r]];
            }
        } else {
            for (int r = 0; r < h; r++, dst += SCREEN_W) {
                if (src[r] != 255)
                    *dst = src[r];
            }
        }
    }
}

void draw_patch(uint8_t *fb, const bast_tex *tex, int x, int y)
{
    if (tex)
        draw_patch_region(fb, tex, 0, 0, tex->width, tex->height, x, y, NULL);
}

void draw_patch_remap(uint8_t *fb, const bast_tex *tex, int x, int y, const uint8_t remap[256])
{
    if (tex)
        draw_patch_region(fb, tex, 0, 0, tex->width, tex->height, x, y, remap);
}

void draw_fill(uint8_t *fb, int x0, int y0, int x1, int y1, uint8_t idx)
{
    if (!fb)
        return;
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > SCREEN_W - 1)
        x1 = SCREEN_W - 1;
    if (y1 > SCREEN_H - 1)
        y1 = SCREEN_H - 1;
    if (x0 > x1 || y0 > y1)
        return;
    for (int y = y0; y <= y1; y++)
        memset(fb + (size_t)y * SCREEN_W + x0, idx, (size_t)(x1 - x0 + 1));
}

/* ---- fonts --------------------------------------------------------------- */

/* hud.md Fonts: cell size and glyph order of each sheet. */
static const struct {
    const char *name;
    int cell_w, cell_h;
    const char *chars; /* NULL = ASCII from `lo` to `hi` */
    int lo, hi;
} FONTS[] = {
    {"small", 6, 8, NULL, 32, 126},
    {"menu", 10, 14, NULL, 32, 95},
    {"bigdigits", 12, 16, "0123456789%/-", 0, 0},
};

font *font_from_tex(const char *name, bast_tex *tex)
{
    int k = -1;
    for (int i = 0; i < (int)(sizeof FONTS / sizeof FONTS[0]); i++) {
        if (name && strcmp(FONTS[i].name, name) == 0)
            k = i;
    }
    font *f = NULL;
    if (k < 0 || !tex || !(f = calloc(1, sizeof *f))) {
        bast_tex_free(tex);
        return NULL;
    }
    f->tex = tex;
    f->cell_w = FONTS[k].cell_w;
    f->cell_h = FONTS[k].cell_h;
    for (int c = 0; c < 128; c++)
        f->glyph[c] = -1;
    int n = 0;
    if (FONTS[k].chars) {
        for (const char *p = FONTS[k].chars; *p; p++)
            f->glyph[(unsigned char)*p] = (int16_t)n++;
    } else {
        for (int c = FONTS[k].lo; c <= FONTS[k].hi; c++)
            f->glyph[c] = (int16_t)n++;
    }
    int rows = (n + 15) / 16;
    if (tex->width != 16 * f->cell_w || tex->height != rows * f->cell_h) {
        font_free(f);
        return NULL;
    }
    /* Missing lower case -> upper case. */
    for (int c = 'a'; c <= 'z'; c++) {
        if (f->glyph[c] < 0)
            f->glyph[c] = f->glyph[c - 'a' + 'A'];
    }
    return f;
}

font *font_load(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/fonts/%s.tex", dir, name);
    bast_tex *t = bast_tex_load(path);
    if (!t)
        return NULL;
    return font_from_tex(name, t);
}

void font_free(font *f)
{
    if (!f)
        return;
    bast_tex_free(f->tex);
    free(f);
}

static int line_chars(const char *s)
{
    int n = 0;
    while (s[n] && s[n] != '\n')
        n++;
    return n;
}

int text_width(const font *f, const char *str)
{
    if (!f || !str)
        return 0;
    int best = 0;
    for (;;) {
        int n = line_chars(str);
        if (n * f->cell_w > best)
            best = n * f->cell_w;
        if (!str[n])
            return best;
        str += n + 1;
    }
}

static void draw_line(uint8_t *fb, const font *f, int x, int y, const char *s, int n,
                      const uint8_t *remap)
{
    for (int i = 0; i < n; i++, x += f->cell_w) {
        unsigned char c = (unsigned char)s[i];
        int g = c < 128 ? f->glyph[c] : -1;
        if (g < 0)
            continue; /* blank cell */
        draw_patch_region(fb, f->tex, (g % 16) * f->cell_w, (g / 16) * f->cell_h, f->cell_w,
                          f->cell_h, x, y, remap);
    }
}

void draw_text(uint8_t *fb, const font *f, int x, int y, const char *str, const uint8_t *remap)
{
    if (!fb || !f || !str)
        return;
    for (;;) {
        int n = line_chars(str);
        draw_line(fb, f, x, y, str, n, remap);
        if (!str[n])
            return;
        str += n + 1;
        y += f->cell_h;
    }
}

void draw_text_right(uint8_t *fb, const font *f, int x_right, int y, const char *str,
                     const uint8_t *remap)
{
    if (!fb || !f || !str)
        return;
    for (;;) {
        int n = line_chars(str);
        draw_line(fb, f, x_right + 1 - n * f->cell_w, y, str, n, remap);
        if (!str[n])
            return;
        str += n + 1;
        y += f->cell_h;
    }
}
