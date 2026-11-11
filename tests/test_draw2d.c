/* ctest draw2d: patch blits, fills, fonts and remaps (issue #44).
 * Usage: test_draw2d <assets/gen dir> <catlantis.gpl> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/draw2d.h"
#include "engine/video.h"

enum { GUARD = 4096, FB_BYTES = SCREEN_W * SCREEN_H, BG = 7, GUARD_BYTE = 0xA5 };

static uint8_t *mem;
static uint8_t *fb;

static void fb_reset(void)
{
    memset(mem, GUARD_BYTE, GUARD);
    memset(mem + GUARD + FB_BYTES, GUARD_BYTE, GUARD);
    memset(fb, BG, FB_BYTES);
}

static void guards_intact(void)
{
    for (int i = 0; i < GUARD; i++) {
        assert(mem[i] == GUARD_BYTE);
        assert(mem[GUARD + FB_BYTES + i] == GUARD_BYTE);
    }
}

static uint8_t px(int x, int y)
{
    return fb[y * SCREEN_W + x];
}

/* 20x10 patch: pixel (x, y) = 10 + (x + y) % 40, except a transparent
 * checker where (x + y) % 3 == 0. */
static bast_tex *make_patch(void)
{
    bast_tex *t = bast_tex_new(20, 10);
    assert(t);
    for (int x = 0; x < 20; x++)
        for (int y = 0; y < 10; y++)
            t->pixels[x * 10 + y] = (x + y) % 3 == 0 ? 255 : (uint8_t)(10 + (x + y) % 40);
    return t;
}

static uint8_t patch_at(int x, int y)
{
    return (x + y) % 3 == 0 ? 255 : (uint8_t)(10 + (x + y) % 40);
}

/* Draw the patch at (px0, py0) and check every screen pixel. */
static void check_blit(const bast_tex *t, int px0, int py0, const uint8_t *remap)
{
    fb_reset();
    if (remap)
        draw_patch_remap(fb, t, px0, py0, remap);
    else
        draw_patch(fb, t, px0, py0);
    guards_intact();
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            int u = x - px0, v = y - py0;
            uint8_t want = BG;
            if (u >= 0 && u < t->width && v >= 0 && v < t->height && patch_at(u, v) != 255)
                want = remap ? remap[patch_at(u, v)] : patch_at(u, v);
            assert(px(x, y) == want);
        }
}

static void test_patch_clipping(void)
{
    bast_tex *t = make_patch();
    /* Fully inside, each edge, each corner, fully outside, far outside. */
    const int pos[][2] = {
        {100, 100}, {-7, 50},   {310, 50},    {150, -4},  {150, 235},  {-5, -5},
        {315, 236}, {-5, 236},  {315, -3},    {-20, 50},  {320, 50},   {150, -10},
        {150, 240}, {-100000, 7}, {100000, 7}, {0, 0},     {300, 230},  {7, -100000},
    };
    for (size_t i = 0; i < sizeof pos / sizeof pos[0]; i++)
        check_blit(t, pos[i][0], pos[i][1], NULL);
    /* Transparent pixels leave the background intact: with BG 7 and patch
     * indices >= 10, a 255 written through would show as 255 (checked above
     * pixel by pixel); assert the count explicitly too. */
    fb_reset();
    draw_patch(fb, t, 100, 100);
    int bg = 0;
    for (int y = 100; y < 110; y++)
        for (int x = 100; x < 120; x++)
            bg += px(x, y) == BG;
    int transparent = 0;
    for (int x = 0; x < 20; x++)
        for (int y = 0; y < 10; y++)
            transparent += patch_at(x, y) == 255;
    assert(bg == transparent && bg > 0);
    /* Region outside the texture is clipped to it. */
    fb_reset();
    draw_patch_region(fb, t, -5, -5, 100, 100, 0, 0, NULL);
    guards_intact();
    assert(px(4, 4) == BG && px(5 + 1, 5) == patch_at(1, 0));
    assert(px(5 + 20, 5) == BG);
    bast_tex_free(t);
    printf("draw2d: patch clipping ok\n");
}

static void test_remap(void)
{
    uint8_t remap[256];
    int g0, gn, r0, rn;
    assert(ramp_find("gold", &g0, &gn) && g0 == 82 && gn == 12);
    assert(ramp_find("red", &r0, &rn) && r0 == 94 && rn == 16);
    assert(!ramp_find("nope", NULL, NULL));
    assert(!remap_ramp(remap, "gold", "nope"));
    for (int i = 0; i < 256; i++)
        assert(remap[i] == i);
    assert(remap_ramp(remap, "gold", "red"));
    for (int i = 0; i < 256; i++) {
        if (i >= g0 && i < g0 + gn) {
            assert(remap[i] >= r0 && remap[i] < r0 + rn);
            if (i > g0)
                assert(remap[i] > remap[i - 1]); /* order kept, no collapse */
        } else {
            assert(remap[i] == i); /* only remapped indices change */
        }
    }
    assert(remap[g0] == r0 && remap[g0 + gn - 1] == r0 + rn - 1);
    /* gray (16) -> pink (10): ends pinned, monotonic. */
    assert(remap_ramp(remap, "gray", "pink"));
    assert(remap[2] == 194 && remap[17] == 203);
    for (int i = 3; i < 18; i++)
        assert(remap[i] >= remap[i - 1]);

    /* draw_patch_remap: patch indices 10..49 cross gray (2-17), steel
     * (18-33), beige (34-49); remap steel -> olive. */
    assert(remap_ramp(remap, "steel", "olive"));
    bast_tex *t = make_patch();
    check_blit(t, 50, 60, remap);
    check_blit(t, -3, 233, remap);
    fb_reset();
    draw_patch_remap(fb, t, 50, 60, remap);
    int changed = 0;
    for (int y = 60; y < 70; y++)
        for (int x = 50; x < 70; x++) {
            uint8_t s = patch_at(x - 50, y - 60);
            if (s == 255)
                assert(px(x, y) == BG);
            else if (s >= 18 && s <= 33) {
                assert(px(x, y) >= 170 && px(x, y) <= 181);
                changed++;
            } else
                assert(px(x, y) == s);
        }
    assert(changed > 0);
    bast_tex_free(t);
    printf("draw2d: remap ok\n");
}

static void test_fill(void)
{
    fb_reset();
    draw_fill(fb, -50, -50, 400, 400, 3);
    guards_intact();
    for (int i = 0; i < FB_BYTES; i++)
        assert(fb[i] == 3);
    fb_reset();
    draw_fill(fb, 12, 9, 10, 8, 4); /* reversed corners, inclusive */
    guards_intact();
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            assert(px(x, y) == ((x >= 10 && x <= 12 && y >= 8 && y <= 9) ? 4 : BG));
    fb_reset();
    draw_fill(fb, 320, 0, 500, 239, 4);
    draw_fill(fb, 0, -9, 319, -1, 4);
    guards_intact();
    for (int i = 0; i < FB_BYTES; i++)
        assert(fb[i] == BG);
    printf("draw2d: fill ok\n");
}

/* Rightmost column with a non-background pixel in rows y..y+h-1. */
static int rightmost(int y, int h)
{
    for (int x = SCREEN_W - 1; x >= 0; x--)
        for (int r = y; r < y + h; r++)
            if (px(x, r) != BG)
                return x;
    return -1;
}

static int leftmost(int y, int h)
{
    for (int x = 0; x < SCREEN_W; x++)
        for (int r = y; r < y + h; r++)
            if (px(x, r) != BG)
                return x;
    return -1;
}

static void test_fonts(const char *dir)
{
    font *big = font_load(dir, "bigdigits");
    font *menu = font_load(dir, "menu");
    font *small = font_load(dir, "small");
    assert(big && menu && small);
    assert(!font_load(dir, "nope"));

    assert(text_width(big, "100%") == 4 * 12);
    assert(text_width(small, "C 120") == 5 * 6);
    assert(text_width(menu, "NEW GAME") == 8 * 10);
    assert(text_width(menu, "AB\nCDEF\nG") == 4 * 10);
    assert(text_width(menu, "") == 0);

    /* Right-aligned text ends exactly at x_right (hud.md: health digits
     * right-aligned to x=103). Every glyph's shadow fills the cell's last
     * column for digits / '%', so the last ink column is x_right. */
    fb_reset();
    draw_text_right(fb, big, 103, 210, "100%", NULL);
    guards_intact();
    assert(rightmost(210, 16) == 103);
    assert(leftmost(210, 16) >= 103 - 48 + 1);
    fb_reset();
    draw_text_right(fb, small, 103, 226, "200", NULL);
    assert(rightmost(226, 8) == 103);

    /* Missing glyphs: lower case -> upper case in Menu; others -> space. */
    uint8_t a[FB_BYTES];
    fb_reset();
    draw_text(fb, menu, 20, 20, "start", NULL);
    memcpy(a, fb, FB_BYTES);
    fb_reset();
    draw_text(fb, menu, 20, 20, "START", NULL);
    assert(memcmp(a, fb, FB_BYTES) == 0);
    fb_reset();
    draw_text(fb, big, 20, 20, "1A2", NULL);
    memcpy(a, fb, FB_BYTES);
    fb_reset();
    draw_text(fb, big, 20, 20, "1 2", NULL);
    assert(memcmp(a, fb, FB_BYTES) == 0);
    fb_reset();
    draw_text(fb, menu, 20, 20, "{~\x80\xff", NULL);
    for (int i = 0; i < FB_BYTES; i++)
        assert(fb[i] == BG);

    /* Text remap: gold -> red changes only gold pixels, into red. */
    uint8_t remap[256];
    assert(remap_ramp(remap, "gold", "red"));
    fb_reset();
    draw_text(fb, big, 0, 0, "88", NULL);
    memcpy(a, fb, FB_BYTES);
    fb_reset();
    draw_text(fb, big, 0, 0, "88", remap);
    int ink = 0;
    for (int i = 0; i < FB_BYTES; i++) {
        if (a[i] == BG) {
            assert(fb[i] == BG);
        } else {
            assert(a[i] >= 82 && a[i] <= 93); /* the sheet is gold only */
            assert(fb[i] == remap[a[i]] && fb[i] >= 94 && fb[i] <= 109);
            ink++;
        }
    }
    assert(ink > 0);

    /* Text clipped at every edge stays inside the buffer. */
    const int pos[][2] = {{-30, 100}, {300, 100}, {100, -10}, {100, 232}, {-5, -5}, {310, 235}};
    for (size_t i = 0; i < sizeof pos / sizeof pos[0]; i++) {
        fb_reset();
        draw_text(fb, menu, pos[i][0], pos[i][1], "CLIPPED TEXT\nLINE TWO", NULL);
        draw_text_right(fb, big, pos[i][0], pos[i][1], "-100%", remap);
        draw_text(fb, small, pos[i][0], pos[i][1], "small text", NULL);
        guards_intact();
    }

    font_free(big);
    font_free(menu);
    font_free(small);
    printf("draw2d: fonts ok\n");
}

/* The C ramp table matches catlantis.gpl: entries first..first+count-1 are
 * named <ramp>_00..<ramp>_<count-1>, and the neighbours are not. */
static void test_ramp_table(const char *gpl)
{
    char names[256][64];
    int n = 0;
    FILE *f = fopen(gpl, "r");
    assert(f);
    char line[256];
    while (fgets(line, sizeof line, f) && n < 256) {
        int r, g, b;
        if (sscanf(line, " %d %d %d %63s", &r, &g, &b, names[n]) == 4)
            n++;
    }
    fclose(f);
    assert(n == 256);
    for (int i = 0; i < ramp_table_size(); i++) {
        int first, count;
        const char *name = ramp_table_entry(i, &first, &count);
        assert(name && count > 0 && first + count <= 256);
        for (int k = 0; k < count; k++) {
            char want[64];
            snprintf(want, sizeof want, "%s_%02d", name, k);
            assert(strcmp(names[first + k], want) == 0);
        }
        char next[64];
        snprintf(next, sizeof next, "%s_%02d", name, count);
        assert(first + count == 256 || strcmp(names[first + count], next) != 0);
    }
    printf("draw2d: ramp table matches %s (%d ramps)\n", gpl, ramp_table_size());
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    mem = malloc(GUARD + FB_BYTES + GUARD);
    assert(mem);
    fb = mem + GUARD;
    test_ramp_table(argv[2]);
    test_patch_clipping();
    test_remap();
    test_fill();
    test_fonts(argv[1]);
    free(mem);
    printf("draw2d: all ok\n");
    return 0;
}
